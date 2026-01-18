#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <chrono>
#include <thread>
#include <errno.h>

class SocketManager {
private:
    int sockfd_;
    
public:
    enum class LingerMode {
        DEFAULT,
        GRACEFUL,
        ABORTIVE
    };
    
    SocketManager() : sockfd_(-1) {}
    
    ~SocketManager() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }
    
    bool create() {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            std::cerr << "Socket creation failed: " 
                     << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }
    
    bool connect(const char* ip, int port) {
        struct sockaddr_in serv_addr;
        std::memset(&serv_addr, 0, sizeof(serv_addr));
        
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address" << std::endl;
            return false;
        }
        
        if (::connect(sockfd_, (struct sockaddr*)&serv_addr, 
                     sizeof(serv_addr)) < 0) {
            std::cerr << "Connection failed: " 
                     << strerror(errno) << std::endl;
            return false;
        }
        
        return true;
    }
    
    bool setLinger(LingerMode mode, int timeout_seconds = 0) {
        struct linger sl;
        
        switch (mode) {
            case LingerMode::DEFAULT:
                sl.l_onoff = 0;
                sl.l_linger = 0;
                std::cout << "Setting DEFAULT linger mode" << std::endl;
                break;
                
            case LingerMode::GRACEFUL:
                sl.l_onoff = 1;
                sl.l_linger = timeout_seconds;
                std::cout << "Setting GRACEFUL linger mode ("
                         << timeout_seconds << "s timeout)" << std::endl;
                break;
                
            case LingerMode::ABORTIVE:
                sl.l_onoff = 1;
                sl.l_linger = 0;
                std::cout << "Setting ABORTIVE linger mode (RST)" 
                         << std::endl;
                break;
        }
        
        if (setsockopt(sockfd_, SOL_SOCKET, SO_LINGER, 
                      &sl, sizeof(sl)) < 0) {
            std::cerr << "setsockopt failed: " 
                     << strerror(errno) << std::endl;
            return false;
        }
        
        return true;
    }
    
    bool send(const std::string& data) {
        size_t total_sent = 0;
        const char* ptr = data.c_str();
        size_t len = data.length();
        
        while (total_sent < len) {
            ssize_t sent = ::send(sockfd_, ptr + total_sent, 
                                 len - total_sent, 0);
            if (sent < 0) {
                std::cerr << "Send failed: " 
                         << strerror(errno) << std::endl;
                return false;
            }
            total_sent += sent;
        }
        
        std::cout << "Sent " << total_sent << " bytes" << std::endl;
        return true;
    }
    
    void closeSocket() {
        if (sockfd_ < 0) return;
        
        std::cout << "Closing socket..." << std::endl;
        
        auto start = std::chrono::steady_clock::now();
        int result = close(sockfd_);
        auto end = std::chrono::steady_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>
                       (end - start);
        
        if (result < 0) {
            std::cerr << "Close failed: " << strerror(errno) << std::endl;
            std::cerr << "Error code: " << errno << std::endl;
        } else {
            std::cout << "Socket closed successfully" << std::endl;
        }
        
        std::cout << "Close took " << duration.count() 
                 << " milliseconds" << std::endl;
        
        sockfd_ = -1;
    }
    
    int getFd() const { return sockfd_; }
};

void demonstrateLingerMode(SocketManager::LingerMode mode, 
                          const char* description) {
    std::cout << "\n=== " << description << " ===" << std::endl;
    
    SocketManager sm;
    if (!sm.create()) {
        return;
    }
    
    // Set linger mode before connecting
    switch (mode) {
        case SocketManager::LingerMode::DEFAULT:
            sm.setLinger(mode);
            break;
        case SocketManager::LingerMode::GRACEFUL:
            sm.setLinger(mode, 5);
            break;
        case SocketManager::LingerMode::ABORTIVE:
            sm.setLinger(mode);
            break;
    }
    
    // For demonstration - would connect to actual server
    // sm.connect("127.0.0.1", 8080);
    // sm.send("Hello, Server!");
    
    std::cout << "Simulating data transmission..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    sm.closeSocket();
}

int main() {
    std::cout << "SO_LINGER Behavior Comparison\n" << std::endl;
    
    // Demonstrate different linger modes
    demonstrateLingerMode(SocketManager::LingerMode::DEFAULT,
                         "Default Behavior");
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    demonstrateLingerMode(SocketManager::LingerMode::GRACEFUL,
                         "Graceful Close (5s timeout)");
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    demonstrateLingerMode(SocketManager::LingerMode::ABORTIVE,
                         "Abortive Close (RST)");
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "DEFAULT: close() returns immediately, "
              << "data sent in background" << std::endl;
    std::cout << "GRACEFUL: close() blocks until data ACKed "
              << "or timeout" << std::endl;
    std::cout << "ABORTIVE: close() sends RST, discards data, "
              << "no TIME_WAIT" << std::endl;
    
    return 0;
}