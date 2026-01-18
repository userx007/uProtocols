#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define LINGER_TIMEOUT 5

// Gracefully close a connection with SO_LINGER
int graceful_close(int sockfd, const char* context) {
    struct linger sl;
    sl.l_onoff = 1;
    sl.l_linger = LINGER_TIMEOUT;
    
    // Set SO_LINGER before closing
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) < 0) {
        perror("setsockopt SO_LINGER");
        close(sockfd);
        return -1;
    }
    
    printf("[%s] Closing socket with %d second linger timeout...\n", 
           context, LINGER_TIMEOUT);
    
    time_t start = time(NULL);
    int result = close(sockfd);
    time_t elapsed = time(NULL) - start;
    
    if (result < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            printf("[%s] Close timed out after %ld seconds\n", 
                   context, elapsed);
            printf("[%s] Warning: Some data may not have been delivered\n", 
                   context);
        } else {
            perror("close");
        }
        return -1;
    }
    
    printf("[%s] Socket closed successfully after %ld seconds\n", 
           context, elapsed);
    printf("[%s] All data sent and acknowledged\n", context);
    return 0;
}

// Send data with verification
int send_all(int sockfd, const char* data, size_t len) {
    size_t total_sent = 0;
    
    while (total_sent < len) {
        ssize_t sent = send(sockfd, data + total_sent, 
                           len - total_sent, 0);
        if (sent < 0) {
            perror("send");
            return -1;
        }
        total_sent += sent;
    }
    
    printf("Sent %zu bytes\n", total_sent);
    return 0;
}

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    
    // Receive request
    ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (received < 0) {
        perror("recv");
        close(client_fd);
        return;
    }
    
    buffer[received] = '\0';
    printf("Received: %s\n", buffer);
    
    // Prepare response
    const char* response = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 28\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Graceful shutdown complete!\n";
    
    // Send response
    if (send_all(client_fd, response, strlen(response)) < 0) {
        close(client_fd);
        return;
    }
    
    // Shutdown write side (half-close)
    if (shutdown(client_fd, SHUT_WR) < 0) {
        perror("shutdown");
    } else {
        printf("Shutdown write side, waiting for client to close...\n");
        
        // Read until client closes (EOF)
        while (recv(client_fd, buffer, sizeof(buffer), 0) > 0) {
            // Drain any remaining data
        }
    }
    
    // Now close with linger for guaranteed delivery
    graceful_close(client_fd, "Client");
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Enable SO_REUSEADDR to quickly restart server
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, 
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d\n", PORT);
    printf("Using SO_LINGER for graceful connection cleanup\n\n");
    
    // Accept one connection for demonstration
    if ((client_fd = accept(server_fd, (struct sockaddr*)&address, 
                           &addrlen)) < 0) {
        perror("accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Client connected\n");
    handle_client(client_fd);
    
    // Close server socket gracefully too
    graceful_close(server_fd, "Server");
    
    return 0;
}