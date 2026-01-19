#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>

#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096
#define PORT 8080

// Set socket to non-blocking mode
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Create and configure listening socket
int create_server_socket(int port) {
    int server_fd;
    struct sockaddr_in addr;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return -1;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

// Accept new connections
void handle_new_connection(int epoll_fd, int server_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;
    struct epoll_event ev;

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // No more connections
            }
            perror("accept");
            break;
        }

        printf("New connection from %s:%d (fd=%d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               client_fd);

        set_nonblocking(client_fd);

        // Add to epoll with edge-triggered mode
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
            perror("epoll_ctl: client_fd");
            close(client_fd);
        }
    }
}

// Handle data from client
void handle_client_data(int epoll_fd, int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t count;

    while (1) {
        count = read(client_fd, buffer, sizeof(buffer));
        
        if (count == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Read all available data
                break;
            }
            perror("read");
            goto close_conn;
        } else if (count == 0) {
            // Connection closed
            printf("Connection closed (fd=%d)\n", client_fd);
            goto close_conn;
        }

        printf("Received %zd bytes from fd=%d\n", count, client_fd);

        // Echo back (simplified WebSocket frame handling)
        ssize_t written = 0;
        while (written < count) {
            ssize_t n = write(client_fd, buffer + written, count - written);
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Would block, modify epoll to wait for writable
                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                    ev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &ev);
                    break;
                }
                perror("write");
                goto close_conn;
            }
            written += n;
        }
    }
    return;

close_conn:
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
}

int main() {
    int server_fd, epoll_fd;
    struct epoll_event ev, events[MAX_EVENTS];
    int nfds;

    // Create server socket
    server_fd = create_server_socket(PORT);
    if (server_fd == -1) {
        exit(EXIT_FAILURE);
    }
    set_nonblocking(server_fd);

    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // Add server socket to epoll
    ev.events = EPOLLIN | EPOLLET; // Edge-triggered
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        exit(EXIT_FAILURE);
    }

    printf("WebSocket server listening on port %d\n", PORT);

    // Event loop
    while (1) {
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_fd) {
                // New connection(s)
                handle_new_connection(epoll_fd, server_fd);
            } else {
                // Data from existing connection
                handle_client_data(epoll_fd, events[i].data.fd);
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return 0;
}