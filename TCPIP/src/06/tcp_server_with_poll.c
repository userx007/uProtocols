#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

int main() {
    int listen_fd, new_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    // Array to hold poll file descriptors
    struct pollfd fds[MAX_CLIENTS];
    int nfds = 1;  // Start with just the listening socket
    
    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }
    
    // Bind to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    
    // Listen for connections
    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }
    
    printf("Server listening on port %d\n", PORT);
    
    // Initialize poll array
    // First entry is the listening socket
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    
    // Initialize remaining entries
    for (int i = 1; i < MAX_CLIENTS; i++) {
        fds[i].fd = -1;  // -1 indicates unused entry
    }
    
    // Main event loop
    while (1) {
        // Wait for events (timeout -1 means wait indefinitely)
        int ret = poll(fds, nfds, -1);
        
        if (ret < 0) {
            perror("poll");
            break;
        }
        
        // Check if listening socket has incoming connection
        if (fds[0].revents & POLLIN) {
            new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
            if (new_fd < 0) {
                perror("accept");
                continue;
            }
            
            printf("New connection from %s:%d (fd=%d)\n",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port),
                   new_fd);
            
            // Add new client to poll array
            int i;
            for (i = 1; i < MAX_CLIENTS; i++) {
                if (fds[i].fd < 0) {
                    fds[i].fd = new_fd;
                    fds[i].events = POLLIN;
                    break;
                }
            }
            
            if (i == MAX_CLIENTS) {
                printf("Too many clients, rejecting connection\n");
                close(new_fd);
            } else {
                if (i >= nfds) {
                    nfds = i + 1;
                }
            }
        }
        
        // Check all client sockets for data
        for (int i = 1; i < nfds; i++) {
            if (fds[i].fd < 0) {
                continue;
            }
            
            // Check for data to read
            if (fds[i].revents & POLLIN) {
                ssize_t n = recv(fds[i].fd, buffer, BUFFER_SIZE - 1, 0);
                
                if (n < 0) {
                    perror("recv");
                    close(fds[i].fd);
                    fds[i].fd = -1;
                } else if (n == 0) {
                    // Connection closed by client
                    printf("Client disconnected (fd=%d)\n", fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1;
                } else {
                    // Echo data back to client
                    buffer[n] = '\0';
                    printf("Received from fd=%d: %s", fds[i].fd, buffer);
                    send(fds[i].fd, buffer, n, 0);
                }
            }
            
            // Check for errors or hangup
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                printf("Client error/hangup (fd=%d)\n", fds[i].fd);
                close(fds[i].fd);
                fds[i].fd = -1;
            }
        }
        
        // Compact the array by removing inactive descriptors from the end
        while (nfds > 1 && fds[nfds - 1].fd < 0) {
            nfds--;
        }
    }
    
    // Cleanup
    for (int i = 0; i < nfds; i++) {
        if (fds[i].fd >= 0) {
            close(fds[i].fd);
        }
    }
    
    return 0;
}