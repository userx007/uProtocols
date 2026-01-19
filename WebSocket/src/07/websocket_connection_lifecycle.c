#include <libwebsockets.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

// Connection state tracking
typedef enum {
    STATE_CONNECTING,
    STATE_OPEN,
    STATE_CLOSING,
    STATE_CLOSED
} connection_state_t;

// Per-session data structure
struct session_data {
    connection_state_t state;
    int ping_count;
    time_t last_ping;
    char *pending_message;
};

static int interrupted = 0;

// Signal handler for graceful shutdown
static void sigint_handler(int sig) {
    interrupted = 1;
}

// WebSocket protocol callback - handles all lifecycle events
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {
    struct session_data *session = (struct session_data *)user;
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("Connection error: %s\n", in ? (char *)in : "unknown");
            if (session) {
                session->state = STATE_CLOSED;
            }
            return -1;
            
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Connection established\n");
            session->state = STATE_OPEN;
            session->ping_count = 0;
            session->last_ping = time(NULL);
            session->pending_message = NULL;
            
            // Request periodic callback for ping/pong
            lws_callback_on_writable(wsi);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            printf("Received message (%zu bytes): %.*s\n", len, (int)len, (char *)in);
            
            // Echo the message back
            if (session->state == STATE_OPEN) {
                size_t msg_len = len;
                session->pending_message = malloc(LWS_PRE + msg_len + 1);
                if (session->pending_message) {
                    memcpy(&session->pending_message[LWS_PRE], in, msg_len);
                    session->pending_message[LWS_PRE + msg_len] = '\0';
                    lws_callback_on_writable(wsi);
                }
            }
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (session->state == STATE_OPEN) {
                // Send pending message if available
                if (session->pending_message) {
                    size_t msg_len = strlen(&session->pending_message[LWS_PRE]);
                    int written = lws_write(wsi, 
                                          (unsigned char *)&session->pending_message[LWS_PRE],
                                          msg_len, LWS_WRITE_TEXT);
                    if (written < 0) {
                        printf("Write error\n");
                        return -1;
                    }
                    printf("Sent message (%d bytes)\n", written);
                    free(session->pending_message);
                    session->pending_message = NULL;
                }
                
                // Send periodic ping
                time_t now = time(NULL);
                if (now - session->last_ping > 30) {
                    unsigned char ping_payload[125];
                    snprintf((char *)ping_payload, sizeof(ping_payload), 
                            "ping-%d", session->ping_count++);
                    lws_write(wsi, ping_payload, strlen((char *)ping_payload), 
                             LWS_WRITE_PING);
                    printf("Sent ping: %s\n", ping_payload);
                    session->last_ping = now;
                }
                
                // Request next writable callback
                lws_callback_on_writable(wsi);
            }
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
            printf("Received pong: %.*s\n", (int)len, (char *)in);
            break;
            
        case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
            printf("Peer initiated close: code=%d, reason=%.*s\n",
                   ((unsigned short *)in)[0], (int)(len - 2), (char *)in + 2);
            session->state = STATE_CLOSING;
            break;
            
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("Connection closed\n");
            session->state = STATE_CLOSED;
            if (session->pending_message) {
                free(session->pending_message);
                session->pending_message = NULL;
            }
            break;
            
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
            printf("Appending handshake headers\n");
            session->state = STATE_CONNECTING;
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Protocol definition
static struct lws_protocols protocols[] = {
    {
        "websocket-lifecycle-protocol",
        callback_websocket,
        sizeof(struct session_data),
        4096,
        0, NULL, 0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

int main(int argc, char **argv) {
    struct lws_context_creation_info info;
    struct lws_client_connect_info connect_info;
    struct lws_context *context;
    struct lws *wsi;
    int n = 0;
    
    signal(SIGINT, sigint_handler);
    
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    
    printf("Connecting to server...\n");
    
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = context;
    connect_info.address = "echo.websocket.org";
    connect_info.port = 443;
    connect_info.path = "/";
    connect_info.host = connect_info.address;
    connect_info.origin = connect_info.address;
    connect_info.protocol = protocols[0].name;
    connect_info.ssl_connection = LWSSSLFLAG_SELFSIGNED_OK;
    
    wsi = lws_client_connect_via_info(&connect_info);
    if (!wsi) {
        fprintf(stderr, "Connection failed\n");
        lws_context_destroy(context);
        return 1;
    }
    
    // Main event loop
    while (n >= 0 && !interrupted) {
        n = lws_service(context, 1000);
    }
    
    // Graceful shutdown
    printf("\nInitiating graceful shutdown...\n");
    lws_context_destroy(context);
    
    return 0;
}