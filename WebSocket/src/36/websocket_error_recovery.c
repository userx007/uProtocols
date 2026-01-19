#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

#define MAX_BACKOFF 32000  // 32 seconds max backoff
#define INITIAL_BACKOFF 1000  // 1 second initial backoff
#define MAX_RETRIES 10
#define PING_INTERVAL 30  // seconds
#define PONG_TIMEOUT 10   // seconds

typedef enum {
    CONN_DISCONNECTED,
    CONN_CONNECTING,
    CONN_CONNECTED,
    CONN_ERROR,
    CONN_RECONNECTING
} ConnectionState;

typedef struct {
    int sockfd;
    ConnectionState state;
    int retry_count;
    int backoff_ms;
    time_t last_ping;
    time_t last_pong;
    char host[256];
    int port;
    bool should_reconnect;
} WebSocketConnection;

// Message queue for buffering during disconnection
typedef struct MessageNode {
    char *data;
    size_t len;
    struct MessageNode *next;
} MessageNode;

typedef struct {
    MessageNode *head;
    MessageNode *tail;
    size_t count;
} MessageQueue;

// Ignore SIGPIPE to handle broken pipes gracefully
void setup_signal_handlers() {
    signal(SIGPIPE, SIG_IGN);
}

// Initialize message queue
void queue_init(MessageQueue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
}

// Enqueue message
bool queue_push(MessageQueue *q, const char *data, size_t len) {
    MessageNode *node = malloc(sizeof(MessageNode));
    if (!node) return false;
    
    node->data = malloc(len);
    if (!node->data) {
        free(node);
        return false;
    }
    
    memcpy(node->data, data, len);
    node->len = len;
    node->next = NULL;
    
    if (q->tail) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = q->tail = node;
    }
    q->count++;
    return true;
}

// Dequeue message
MessageNode* queue_pop(MessageQueue *q) {
    if (!q->head) return NULL;
    
    MessageNode *node = q->head;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    return node;
}

// Calculate next backoff delay with exponential backoff
int calculate_backoff(int current_backoff) {
    int next = current_backoff * 2;
    return (next > MAX_BACKOFF) ? MAX_BACKOFF : next;
}

// Initialize WebSocket connection structure
void ws_connection_init(WebSocketConnection *conn, const char *host, int port) {
    conn->sockfd = -1;
    conn->state = CONN_DISCONNECTED;
    conn->retry_count = 0;
    conn->backoff_ms = INITIAL_BACKOFF;
    conn->last_ping = 0;
    conn->last_pong = 0;
    strncpy(conn->host, host, sizeof(conn->host) - 1);
    conn->port = port;
    conn->should_reconnect = true;
}

// Handle specific socket errors
const char* get_error_description(int error_code) {
    switch (error_code) {
        case EPIPE: return "Broken pipe - remote closed connection";
        case ECONNRESET: return "Connection reset by peer";
        case ETIMEDOUT: return "Connection timed out";
        case ENETUNREACH: return "Network unreachable";
        case EHOSTUNREACH: return "Host unreachable";
        case ECONNREFUSED: return "Connection refused";
        default: return strerror(error_code);
    }
}

// Attempt to connect to WebSocket server
int ws_connect(WebSocketConnection *conn) {
    struct addrinfo hints, *result, *rp;
    char port_str[6];
    int sock = -1;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    snprintf(port_str, sizeof(port_str), "%d", conn->port);
    
    // Resolve hostname
    if (getaddrinfo(conn->host, port_str, &hints, &result) != 0) {
        fprintf(stderr, "DNS resolution failed for %s\n", conn->host);
        return -1;
    }
    
    // Try each address until success
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;
        
        // Set socket timeout
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  // Success
        }
        
        fprintf(stderr, "Connect failed: %s\n", get_error_description(errno));
        close(sock);
        sock = -1;
    }
    
    freeaddrinfo(result);
    
    if (sock == -1) {
        fprintf(stderr, "Failed to connect to %s:%d\n", conn->host, conn->port);
        return -1;
    }
    
    conn->sockfd = sock;
    return 0;
}

// Send data with error handling
ssize_t ws_send_with_recovery(WebSocketConnection *conn, const char *data, 
                               size_t len, MessageQueue *queue) {
    if (conn->state != CONN_CONNECTED) {
        // Queue message for later delivery
        if (queue && queue_push(queue, data, len)) {
            printf("Message queued (connection down)\n");
            return len;
        }
        return -1;
    }
    
    ssize_t sent = send(conn->sockfd, data, len, MSG_NOSIGNAL);
    
    if (sent < 0) {
        int error = errno;
        fprintf(stderr, "Send error: %s\n", get_error_description(error));
        
        // Handle different error types
        switch (error) {
            case EPIPE:
            case ECONNRESET:
            case ETIMEDOUT:
                // Connection is dead, trigger reconnection
                conn->state = CONN_ERROR;
                if (queue) queue_push(queue, data, len);
                break;
            
            case EAGAIN:
            case EWOULDBLOCK:
                // Temporary error, can retry
                if (queue) queue_push(queue, data, len);
                break;
            
            default:
                fprintf(stderr, "Unhandled send error\n");
                break;
        }
    }
    
    return sent;
}

// Receive data with error detection
ssize_t ws_recv_with_detection(WebSocketConnection *conn, char *buffer, 
                                size_t len) {
    ssize_t received = recv(conn->sockfd, buffer, len, 0);
    
    if (received < 0) {
        int error = errno;
        fprintf(stderr, "Recv error: %s\n", get_error_description(error));
        
        if (error != EAGAIN && error != EWOULDBLOCK) {
            conn->state = CONN_ERROR;
        }
    } else if (received == 0) {
        // Graceful close
        printf("Connection closed by peer\n");
        conn->state = CONN_DISCONNECTED;
    }
    
    return received;
}

// Reconnection logic with exponential backoff
bool ws_reconnect(WebSocketConnection *conn, MessageQueue *queue) {
    if (conn->retry_count >= MAX_RETRIES) {
        fprintf(stderr, "Max retries exceeded\n");
        conn->should_reconnect = false;
        return false;
    }
    
    conn->state = CONN_RECONNECTING;
    
    // Wait with exponential backoff
    printf("Reconnecting in %d ms (attempt %d/%d)...\n", 
           conn->backoff_ms, conn->retry_count + 1, MAX_RETRIES);
    usleep(conn->backoff_ms * 1000);
    
    // Close old socket if open
    if (conn->sockfd >= 0) {
        close(conn->sockfd);
        conn->sockfd = -1;
    }
    
    // Attempt reconnection
    if (ws_connect(conn) == 0) {
        printf("Reconnected successfully!\n");
        conn->state = CONN_CONNECTED;
        conn->retry_count = 0;
        conn->backoff_ms = INITIAL_BACKOFF;
        conn->last_ping = time(NULL);
        conn->last_pong = time(NULL);
        
        // Flush queued messages
        MessageNode *node;
        while ((node = queue_pop(queue)) != NULL) {
            send(conn->sockfd, node->data, node->len, MSG_NOSIGNAL);
            free(node->data);
            free(node);
        }
        
        return true;
    }
    
    // Reconnection failed
    conn->retry_count++;
    conn->backoff_ms = calculate_backoff(conn->backoff_ms);
    return false;
}

// Heartbeat check
void ws_heartbeat_check(WebSocketConnection *conn) {
    if (conn->state != CONN_CONNECTED) return;
    
    time_t now = time(NULL);
    
    // Send ping if interval elapsed
    if (now - conn->last_ping >= PING_INTERVAL) {
        char ping[] = "PING";
        if (send(conn->sockfd, ping, sizeof(ping), MSG_NOSIGNAL) < 0) {
            conn->state = CONN_ERROR;
            return;
        }
        conn->last_ping = now;
    }
    
    // Check pong timeout
    if (now - conn->last_pong > PING_INTERVAL + PONG_TIMEOUT) {
        fprintf(stderr, "Pong timeout - connection dead\n");
        conn->state = CONN_ERROR;
    }
}

// Example usage
int main() {
    setup_signal_handlers();
    
    WebSocketConnection conn;
    MessageQueue queue;
    queue_init(&queue);
    
    ws_connection_init(&conn, "echo.websocket.org", 80);
    
    // Initial connection
    if (ws_connect(&conn) == 0) {
        conn.state = CONN_CONNECTED;
        printf("Connected successfully\n");
    }
    
    // Main loop with error recovery
    while (conn.should_reconnect) {
        if (conn.state == CONN_ERROR || conn.state == CONN_DISCONNECTED) {
            ws_reconnect(&conn, &queue);
            continue;
        }
        
        if (conn.state == CONN_CONNECTED) {
            ws_heartbeat_check(&conn);
            
            // Simulate sending data
            const char *msg = "Hello, WebSocket!";
            ws_send_with_recovery(&conn, msg, strlen(msg), &queue);
            
            sleep(1);
        }
    }
    
    // Cleanup
    if (conn.sockfd >= 0) close(conn.sockfd);
    
    return 0;
}