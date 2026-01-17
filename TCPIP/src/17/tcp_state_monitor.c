#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>

// TCP state names for display
const char* tcp_state_name(int state) {
    switch(state) {
        case TCP_ESTABLISHED: return "ESTABLISHED";
        case TCP_SYN_SENT: return "SYN_SENT";
        case TCP_SYN_RECV: return "SYN_RECV";
        case TCP_FIN_WAIT1: return "FIN_WAIT1";
        case TCP_FIN_WAIT2: return "FIN_WAIT2";
        case TCP_TIME_WAIT: return "TIME_WAIT";
        case TCP_CLOSE: return "CLOSE";
        case TCP_CLOSE_WAIT: return "CLOSE_WAIT";
        case TCP_LAST_ACK: return "LAST_ACK";
        case TCP_LISTEN: return "LISTEN";
        case TCP_CLOSING: return "CLOSING";
        default: return "UNKNOWN";
    }
}

// Get current TCP state of a socket
int get_tcp_state(int sockfd) {
    struct tcp_info info;
    socklen_t info_len = sizeof(info);
    
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0) {
        return info.tcpi_state;
    }
    return -1;
}

// Client example: Connect and monitor state transitions
void client_example(const char* host, int port) {
    int sockfd;
    struct sockaddr_in server_addr;
    
    printf("=== CLIENT: Monitoring TCP State Transitions ===\n");
    
    // Create socket (CLOSED state)
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return;
    }
    printf("State after socket(): CLOSED\n");
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &server_addr.sin_addr);
    
    // Make socket non-blocking to observe SYN_SENT
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    // Initiate connection (moves to SYN_SENT)
    printf("\nCalling connect()...\n");
    int result = connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (result < 0 && errno == EINPROGRESS) {
        int state = get_tcp_state(sockfd);
        printf("State during connect(): %s\n", tcp_state_name(state));
        
        // Wait for connection to complete
        fd_set write_fds;
        struct timeval tv = {5, 0}; // 5 second timeout
        
        FD_ZERO(&write_fds);
        FD_SET(sockfd, &write_fds);
        
        if (select(sockfd + 1, NULL, &write_fds, NULL, &tv) > 0) {
            int error;
            socklen_t len = sizeof(error);
            getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
            
            if (error == 0) {
                state = get_tcp_state(sockfd);
                printf("State after connect completes: %s\n", tcp_state_name(state));
                
                // Make blocking again
                fcntl(sockfd, F_SETFL, flags);
                
                // Send some data
                const char* msg = "Hello from client\n";
                send(sockfd, msg, strlen(msg), 0);
                
                // Initiate graceful close (moves to FIN_WAIT1)
                printf("\nCalling close()...\n");
                shutdown(sockfd, SHUT_WR);
                
                state = get_tcp_state(sockfd);
                printf("State after shutdown(WR): %s\n", tcp_state_name(state));
                
                // Read remaining data
                char buffer[1024];
                ssize_t n;
                while ((n = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
                    printf("Received: %.*s", (int)n, buffer);
                }
                
                state = get_tcp_state(sockfd);
                printf("State after recv returns 0: %s\n", tcp_state_name(state));
            }
        }
    }
    
    close(sockfd);
    printf("State after close(): CLOSED (eventually TIME_WAIT)\n");
}

// Server example: Accept and monitor states
void server_example(int port) {
    int listen_fd, conn_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    printf("=== SERVER: Monitoring TCP State Transitions ===\n");
    
    // Create socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return;
    }
    
    // Set SO_REUSEADDR to avoid TIME_WAIT issues
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // Bind
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return;
    }
    
    // Listen (moves to LISTEN state)
    listen(listen_fd, 5);
    printf("State after listen(): LISTEN\n");
    printf("Waiting for connections on port %d...\n", port);
    
    // Accept connection
    conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (conn_fd < 0) {
        perror("accept");
        close(listen_fd);
        return;
    }
    
    int state = get_tcp_state(conn_fd);
    printf("State after accept(): %s\n", tcp_state_name(state));
    
    // Receive data
    char buffer[1024];
    ssize_t n = recv(conn_fd, buffer, sizeof(buffer), 0);
    if (n > 0) {
        printf("Received: %.*s", (int)n, buffer);
    }
    
    // Check if client initiated close
    if (n == 0 || recv(conn_fd, buffer, sizeof(buffer), MSG_PEEK) == 0) {
        state = get_tcp_state(conn_fd);
        printf("State after client closes: %s\n", tcp_state_name(state));
    }
    
    // Server closes (moves to LAST_ACK)
    close(conn_fd);
    printf("State after close(): LAST_ACK -> CLOSED\n");
    
    close(listen_fd);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("  Server mode: %s server <port>\n", argv[0]);
        printf("  Client mode: %s client <host> <port>\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "server") == 0) {
        int port = argc > 2 ? atoi(argv[2]) : 8080;
        server_example(port);
    } else if (strcmp(argv[1], "client") == 0) {
        const char* host = argc > 2 ? argv[2] : "127.0.0.1";
        int port = argc > 3 ? atoi(argv[3]) : 8080;
        client_example(host, port);
    }
    
    return 0;
}