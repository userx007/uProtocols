#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <liburing.h>

#define QUEUE_DEPTH 256
#define BUFFER_SIZE 4096
#define PORT 8080

enum {
    EVENT_ACCEPT,
    EVENT_READ,
    EVENT_WRITE
};

typedef struct {
    int event_type;
    int fd;
    char *buffer;
    size_t len;
} conn_info;

conn_info* create_conn_info(int event_type, int fd) {
    conn_info *info = malloc(sizeof(conn_info));
    info->event_type = event_type;
    info->fd = fd;
    info->buffer = malloc(BUFFER_SIZE);
    info->len = 0;
    return info;
}

void free_conn_info(conn_info *info) {
    if (info->buffer) free(info->buffer);
    free(info);
}

int setup_listening_socket(int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    int enable = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(port);

    if (bind(sock_fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind");
        return -1;
    }

    if (listen(sock_fd, 128) < 0) {
        perror("listen");
        return -1;
    }

    return sock_fd;
}

void add_accept_request(struct io_uring *ring, int server_fd, 
                       struct sockaddr_in *client_addr, socklen_t *client_len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    conn_info *info = create_conn_info(EVENT_ACCEPT, server_fd);
    
    io_uring_prep_accept(sqe, server_fd, (struct sockaddr*)client_addr, 
                        client_len, 0);
    io_uring_sqe_set_data(sqe, info);
}

void add_read_request(struct io_uring *ring, int client_fd) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    conn_info *info = create_conn_info(EVENT_READ, client_fd);
    
    io_uring_prep_recv(sqe, client_fd, info->buffer, BUFFER_SIZE, 0);
    io_uring_sqe_set_data(sqe, info);
}

void add_write_request(struct io_uring *ring, int client_fd, 
                      char *buffer, size_t len) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    conn_info *info = create_conn_info(EVENT_WRITE, client_fd);
    
    memcpy(info->buffer, buffer, len);
    info->len = len;
    
    io_uring_prep_send(sqe, client_fd, info->buffer, len, 0);
    io_uring_sqe_set_data(sqe, info);
}

int main() {
    struct io_uring ring;
    struct io_uring_cqe *cqe;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Initialize io_uring
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        return 1;
    }

    // Setup listening socket
    int server_fd = setup_listening_socket(PORT);
    if (server_fd < 0) {
        return 1;
    }

    printf("Echo server listening on port %d\n", PORT);

    // Add initial accept request
    add_accept_request(&ring, server_fd, &client_addr, &client_len);
    io_uring_submit(&ring);

    // Event loop
    while (1) {
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            perror("io_uring_wait_cqe");
            break;
        }

        conn_info *info = (conn_info*)io_uring_cqe_get_data(cqe);
        int result = cqe->res;

        if (result < 0) {
            fprintf(stderr, "Operation failed: %s\n", strerror(-result));
            if (info->event_type != EVENT_ACCEPT) {
                close(info->fd);
            }
            free_conn_info(info);
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        switch (info->event_type) {
            case EVENT_ACCEPT: {
                int client_fd = result;
                printf("Accepted new connection: fd=%d\n", client_fd);
                
                // Add read request for new client
                add_read_request(&ring, client_fd);
                
                // Add another accept request
                add_accept_request(&ring, server_fd, &client_addr, &client_len);
                io_uring_submit(&ring);
                break;
            }

            case EVENT_READ: {
                if (result == 0) {
                    // Client closed connection
                    printf("Client closed connection: fd=%d\n", info->fd);
                    close(info->fd);
                } else {
                    printf("Received %d bytes from fd=%d\n", result, info->fd);
                    
                    // Echo back the data
                    add_write_request(&ring, info->fd, info->buffer, result);
                    io_uring_submit(&ring);
                }
                break;
            }

            case EVENT_WRITE: {
                printf("Sent %d bytes to fd=%d\n", result, info->fd);
                
                // Continue reading from this client
                add_read_request(&ring, info->fd);
                io_uring_submit(&ring);
                break;
            }
        }

        free_conn_info(info);
        io_uring_cqe_seen(&ring, cqe);
    }

    close(server_fd);
    io_uring_queue_exit(&ring);
    return 0;
}

// Compile: gcc -o echo_server echo_server.c -luring
// Run: ./echo_server
// Test: telnet localhost 8080