#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 1024

volatile sig_atomic_t should_exit = 0;

void signal_handler(int signum) {
    should_exit = 1;
}

int main() {
    int listen_fd, client_fd;
    struct sockaddr_in server_addr;
    struct pollfd fds[2];
    char buffer[BUFFER_SIZE];
    
    // Set up signal handling
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Create listening socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    
    if (listen(listen_fd, 5) < 0) {
        perror("listen");
        exit(1);
    }
    
    printf("Server listening on port %d. Press Ctrl+C to exit.\n", PORT);
    
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    
    // Block SIGINT and SIGTERM during normal operation
    // They'll only be delivered during ppoll()
    sigset_t sigmask, origmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGTERM);
    sigprocmask(SIG_BLOCK, &sigmask, &origmask);
    
    // Timeout for ppoll (5 seconds)
    struct timespec timeout;
    timeout.tv_sec = 5;
    timeout.tv_nsec = 0;
    
    int nfds = 1;
    
    while (!should_exit) {
        // ppoll atomically unblocks signals during wait
        // This prevents race condition where signal arrives
        // right before we start waiting
        int ret = ppoll(fds, nfds, &timeout, &origmask);
        
        if (ret < 0) {
            if (errno == EINTR) {
                // Interrupted by signal - check should_exit
                printf("\nReceived signal, shutting down...\n");
                break;
            }
            perror("ppoll");
            break;
        }
        
        if (ret == 0) {
            // Timeout - can perform periodic tasks here
            printf("ppoll timeout - still alive\n");
            continue;
        }
        
        // Check listening socket
        if (fds[0].revents & POLLIN) {
            client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd < 0) {
                perror("accept");
                continue;
            }
            
            printf("New client connected (fd=%d)\n", client_fd);
            
            if (nfds < 2) {
                fds[1].fd = client_fd;
                fds[1].events = POLLIN;
                nfds = 2;
            } else {
                printf("Already have a client, rejecting\n");
                close(client_fd);
            }
        }
        
        // Check client socket
        if (nfds > 1 && (fds[1].revents & POLLIN)) {
            ssize_t n = recv(fds[1].fd, buffer, BUFFER_SIZE - 1, 0);
            
            if (n <= 0) {
                printf("Client disconnected\n");
                close(fds[1].fd);
                nfds = 1;
            } else {
                buffer[n] = '\0';
                printf("Received: %s", buffer);
                send(fds[1].fd, buffer, n, 0);
            }
        }
        
        // Check for errors
        if (nfds > 1 && (fds[1].revents & (POLLERR | POLLHUP))) {
            printf("Client error/hangup\n");
            close(fds[1].fd);
            nfds = 1;
        }
    }
    
    // Cleanup
    if (nfds > 1) {
        close(fds[1].fd);
    }
    close(listen_fd);
    
    printf("Server shut down cleanly\n");
    return 0;
}