#include <iostream>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>

constexpr int MAX_EVENTS = 1024;
constexpr int BUFFER_SIZE = 4096;
constexpr int PORT = 8080;

class WebSocketServer {
private:
    int server_fd;
    int kqueue_fd;
    std::unordered_map<int, std::vector<char>> write_buffers;

    bool setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) return false;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    }

    int createServerSocket(int port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == -1) {
            perror("socket");
            return -1;
        }

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("bind");
            close(fd);
            return -1;
        }

        if (listen(fd, SOMAXCONN) == -1) {
            perror("listen");
            close(fd);
            return -1;
        }

        return fd;
    }

    void handleNewConnection() {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        while (true) {
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                perror("accept");
                break;
            }

            std::cout << "New connection from " 
                      << inet_ntoa(client_addr.sin_addr) << ":" 
                      << ntohs(client_addr.sin_port) 
                      << " (fd=" << client_fd << ")\n";

            setNonBlocking(client_fd);

            // Register for read events
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
            if (kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr) == -1) {
                perror("kevent: add client");
                close(client_fd);
            }
        }
    }

    void handleClientRead(int client_fd) {
        char buffer[BUFFER_SIZE];
        
        while (true) {
            ssize_t count = read(client_fd, buffer, sizeof(buffer));
            
            if (count == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                perror("read");
                closeConnection(client_fd);
                return;
            } else if (count == 0) {
                std::cout << "Connection closed (fd=" << client_fd << ")\n";
                closeConnection(client_fd);
                return;
            }

            std::cout << "Received " << count << " bytes from fd=" << client_fd << "\n";

            // Queue data for echo (simplified WebSocket handling)
            auto& write_buf = write_buffers[client_fd];
            write_buf.insert(write_buf.end(), buffer, buffer + count);

            // Register for write events
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
            kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr);
        }
    }

    void handleClientWrite(int client_fd) {
        auto it = write_buffers.find(client_fd);
        if (it == write_buffers.end() || it->second.empty()) {
            // No data to write, disable write events
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr);
            return;
        }

        auto& buffer = it->second;
        ssize_t written = write(client_fd, buffer.data(), buffer.size());

        if (written == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("write");
                closeConnection(client_fd);
            }
            return;
        }

        // Remove written data
        buffer.erase(buffer.begin(), buffer.begin() + written);

        if (buffer.empty()) {
            write_buffers.erase(it);
            // Disable write events
            struct kevent ev;
            EV_SET(&ev, client_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr);
        }
    }

    void closeConnection(int fd) {
        struct kevent ev[2];
        EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        kevent(kqueue_fd, ev, 2, nullptr, 0, nullptr);
        
        write_buffers.erase(fd);
        close(fd);
    }

public:
    WebSocketServer() : server_fd(-1), kqueue_fd(-1) {}

    ~WebSocketServer() {
        if (server_fd != -1) close(server_fd);
        if (kqueue_fd != -1) close(kqueue_fd);
    }

    bool start(int port) {
        server_fd = createServerSocket(port);
        if (server_fd == -1) return false;
        
        setNonBlocking(server_fd);

        kqueue_fd = kqueue();
        if (kqueue_fd == -1) {
            perror("kqueue");
            return false;
        }

        // Register server socket for read events
        struct kevent ev;
        EV_SET(&ev, server_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (kevent(kqueue_fd, &ev, 1, nullptr, 0, nullptr) == -1) {
            perror("kevent: add server");
            return false;
        }

        std::cout << "WebSocket server listening on port " << port << "\n";
        return true;
    }

    void run() {
        struct kevent events[MAX_EVENTS];

        while (true) {
            int nfds = kevent(kqueue_fd, nullptr, 0, events, MAX_EVENTS, nullptr);
            if (nfds == -1) {
                perror("kevent wait");
                break;
            }

            for (int i = 0; i < nfds; i++) {
                int fd = static_cast<int>(events[i].ident);

                if (fd == server_fd) {
                    handleNewConnection();
                } else if (events[i].filter == EVFILT_READ) {
                    handleClientRead(fd);
                } else if (events[i].filter == EVFILT_WRITE) {
                    handleClientWrite(fd);
                }
            }
        }
    }
};

int main() {
    WebSocketServer server;
    if (!server.start(PORT)) {
        return EXIT_FAILURE;
    }
    server.run();
    return 0;
}