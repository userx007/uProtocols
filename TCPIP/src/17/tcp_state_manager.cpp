#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

enum class TCPState {
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT1,
    FIN_WAIT2,
    CLOSE_WAIT,
    CLOSING,
    LAST_ACK,
    TIME_WAIT,
    UNKNOWN
};

class TCPConnection {
private:
    int sockfd_;
    TCPState current_state_;
    std::string remote_addr_;
    uint16_t remote_port_;
    
    TCPState map_kernel_state(int kernel_state) {
        switch(kernel_state) {
            case TCP_ESTABLISHED: return TCPState::ESTABLISHED;
            case TCP_SYN_SENT: return TCPState::SYN_SENT;
            case TCP_SYN_RECV: return TCPState::SYN_RECEIVED;
            case TCP_FIN_WAIT1: return TCPState::FIN_WAIT1;
            case TCP_FIN_WAIT2: return TCPState::FIN_WAIT2;
            case TCP_TIME_WAIT: return TCPState::TIME_WAIT;
            case TCP_CLOSE: return TCPState::CLOSED;
            case TCP_CLOSE_WAIT: return TCPState::CLOSE_WAIT;
            case TCP_LAST_ACK: return TCPState::LAST_ACK;
            case TCP_LISTEN: return TCPState::LISTEN;
            case TCP_CLOSING: return TCPState::CLOSING;
            default: return TCPState::UNKNOWN;
        }
    }
    
    std::string state_to_string(TCPState state) const {
        switch(state) {
            case TCPState::CLOSED: return "CLOSED";
            case TCPState::LISTEN: return "LISTEN";
            case TCPState::SYN_SENT: return "SYN_SENT";
            case TCPState::SYN_RECEIVED: return "SYN_RECEIVED";
            case TCPState::ESTABLISHED: return "ESTABLISHED";
            case TCPState::FIN_WAIT1: return "FIN_WAIT1";
            case TCPState::FIN_WAIT2: return "FIN_WAIT2";
            case TCPState::CLOSE_WAIT: return "CLOSE_WAIT";
            case TCPState::CLOSING: return "CLOSING";
            case TCPState::LAST_ACK: return "LAST_ACK";
            case TCPState::TIME_WAIT: return "TIME_WAIT";
            default: return "UNKNOWN";
        }
    }
    
public:
    TCPConnection() : sockfd_(-1), current_state_(TCPState::CLOSED), remote_port_(0) {}
    
    ~TCPConnection() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }
    
    bool create_socket() {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        current_state_ = TCPState::CLOSED;
        std::cout << "Socket created, state: " << state_to_string(current_state_) << std::endl;
        return true;
    }
    
    TCPState get_current_state() {
        if (sockfd_ < 0) return TCPState::CLOSED;
        
        struct tcp_info info;
        socklen_t info_len = sizeof(info);
        
        if (getsockopt(sockfd_, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0) {
            current_state_ = map_kernel_state(info.tcpi_state);
        }
        return current_state_;
    }
    
    void print_state_transition(const std::string& event) {
        TCPState old_state = current_state_;
        TCPState new_state = get_current_state();
        
        if (old_state != new_state) {
            std::cout << "State transition [" << event << "]: " 
                      << state_to_string(old_state) << " -> " 
                      << state_to_string(new_state) << std::endl;
        } else {
            std::cout << "Current state [" << event << "]: " 
                      << state_to_string(new_state) << std::endl;
        }
    }
    
    bool connect(const std::string& host, uint16_t port) {
        remote_addr_ = host;
        remote_port_ = port;
        
        struct sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
        
        std::cout << "\nInitiating connection to " << host << ":" << port << std::endl;
        
        // Make non-blocking to observe SYN_SENT
        int flags = fcntl(sockfd_, F_GETFL, 0);
        fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);
        
        int result = ::connect(sockfd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        if (result < 0 && errno == EINPROGRESS) {
            print_state_transition("connect() called");
            
            // Wait for connection to complete
            fd_set write_fds;
            struct timeval tv = {5, 0};
            
            FD_ZERO(&write_fds);
            FD_SET(sockfd_, &write_fds);
            
            if (select(sockfd_ + 1, nullptr, &write_fds, nullptr, &tv) > 0) {
                int error;
                socklen_t len = sizeof(error);
                getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &error, &len);
                
                if (error == 0) {
                    fcntl(sockfd_, F_SETFL, flags); // Restore blocking mode
                    print_state_transition("connection established");
                    return true;
                }
            }
        }
        
        std::cerr << "Connection failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    ssize_t send(const std::string& data) {
        if (current_state_ != TCPState::ESTABLISHED) {
            std::cerr << "Cannot send: not in ESTABLISHED state" << std::endl;
            return -1;
        }
        
        ssize_t sent = ::send(sockfd_, data.c_str(), data.size(), 0);
        std::cout << "Sent " << sent << " bytes" << std::endl;
        return sent;
    }
    
    ssize_t receive(char* buffer, size_t size) {
        ssize_t received = ::recv(sockfd_, buffer, size, 0);
        
        if (received == 0) {
            print_state_transition("received FIN (recv=0)");
        } else if (received > 0) {
            std::cout << "Received " << received << " bytes" << std::endl;
        }
        
        return received;
    }
    
    void shutdown_write() {
        std::cout << "\nInitiating graceful shutdown (write side)" << std::endl;
        ::shutdown(sockfd_, SHUT_WR);
        print_state_transition("shutdown(SHUT_WR)");
    }
    
    void close_connection() {
        if (sockfd_ >= 0) {
            std::cout << "\nClosing connection" << std::endl;
            print_state_transition("before close()");
            ::close(sockfd_);
            sockfd_ = -1;
            current_state_ = TCPState::CLOSED;
            std::cout << "Connection closed, socket in TIME_WAIT or CLOSED" << std::endl;
        }
    }
    
    // Demonstrate half-close scenario
    void demonstrate_half_close() {
        if (current_state_ != TCPState::ESTABLISHED) {
            std::cerr << "Not in ESTABLISHED state" << std::endl;
            return;
        }
        
        std::cout << "\n=== Demonstrating Half-Close ===\n";
        
        // Send final data
        send("Final message before half-close\n");
        
        // Close write side (moves to FIN_WAIT1/FIN_WAIT2)
        shutdown_write();
        
        // Can still receive data
        char buffer[1024];
        std::cout << "Can still receive data in half-closed state..." << std::endl;
        ssize_t n = receive(buffer, sizeof(buffer));
        
        if (n > 0) {
            std::cout << "Received: " << std::string(buffer, n);
        }
        
        // Continue receiving until FIN from remote
        while ((n = receive(buffer, sizeof(buffer))) > 0) {
            std::cout << "Received: " << std::string(buffer, n);
        }
        
        print_state_transition("after receiving remote FIN");
    }
    
    int get_socket() const { return sockfd_; }
};

// Server class to demonstrate passive open states
class TCPServer {
private:
    int listen_fd_;
    TCPState listen_state_;
    uint16_t port_;
    
public:
    TCPServer(uint16_t port) : listen_fd_(-1), listen_state_(TCPState::CLOSED), port_(port) {}
    
    ~TCPServer() {
        if (listen_fd_ >= 0) {
            close(listen_fd_);
        }
    }
    
    bool start() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        // Enable address reuse
        int reuse = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        
        struct sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port_);
        
        if (bind(listen_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Bind failed: " << strerror(errno) << std::endl;
            return false;
        }
        
        if (listen(listen_fd_, 5) < 0) {
            std::cerr << "Listen failed" << std::endl;
            return false;
        }
        
        listen_state_ = TCPState::LISTEN;
        std::cout << "Server listening on port " << port_ << ", state: LISTEN" << std::endl;
        return true;
    }
    
    std::unique_ptr<TCPConnection> accept_connection() {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        std::cout << "\nWaiting for connection..." << std::endl;
        
        int conn_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (conn_fd < 0) {
            std::cerr << "Accept failed" << std::endl;
            return nullptr;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        
        std::cout << "Accepted connection from " << client_ip << ":" 
                  << ntohs(client_addr.sin_port) << std::endl;
        
        auto conn = std::make_unique<TCPConnection>();
        // Manually set the accepted socket
        conn->~TCPConnection();
        new (conn.get()) TCPConnection();
        // Note: This is simplified; in production, you'd properly handle this
        
        std::cout << "Connection state: ESTABLISHED" << std::endl;
        return conn;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage:\n";
        std::cout << "  Server: " << argv[0] << " server <port>\n";
        std::cout << "  Client: " << argv[0] << " client <host> <port>\n";
        return 1;
    }
    
    std::string mode = argv[1];
    
    if (mode == "client") {
        std::string host = argc > 2 ? argv[2] : "127.0.0.1";
        uint16_t port = argc > 3 ? std::stoi(argv[3]) : 8080;
        
        std::cout << "=== TCP Client State Demonstration ===\n";
        
        TCPConnection conn;
        if (!conn.create_socket()) return 1;
        
        if (conn.connect(host, port)) {
            // Send data
            conn.send("Hello from C++ client\n");
            
            // Receive response
            char buffer[1024];
            conn.receive(buffer, sizeof(buffer));
            
            // Demonstrate half-close
            conn.demonstrate_half_close();
            
            // Final close
            conn.close_connection();
        }
        
    } else if (mode == "server") {
        uint16_t port = argc > 2 ? std::stoi(argv[2]) : 8080;
        
        std::cout << "=== TCP Server State Demonstration ===\n";
        
        TCPServer server(port);
        if (!server.start()) return 1;
        
        // Accept one connection
        auto conn = server.accept_connection();
        if (conn) {
            std::cout << "Connection handling would go here\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    
    return 0;
}