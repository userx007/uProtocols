# TCP State Machine: Complete Guide

## Overview

The TCP state machine defines the lifecycle of a TCP connection through various states, from establishment to termination. Understanding these states is crucial for robust network application design, debugging connection issues, and implementing proper error handling.

## TCP State Diagram

```
                              +---------+
                              |  CLOSED |
                              +---------+
                                   |
                    passive OPEN   |   active OPEN
                    (listen())     |   (connect())
                                   |
                    +--------------|----------------+
                    |              |                |
                    v              v                v
              +---------+    +---------+      +---------+
              |  LISTEN |    | SYN_SENT|      |  (CLOSED)|
              +---------+    +---------+      +---------+
                    |              |
        rcv SYN     |              |   rcv SYN+ACK
        snd SYN+ACK |              |   snd ACK
                    v              v
              +---------+    +---------+
              | SYN_RCVD|    |ESTABLISHED|
              +---------+    +---------+
                    |              |
           rcv ACK  |              |   close()
                    v              v
              +---------+    +---------+
              |ESTABLISHED   | FIN_WAIT_1|
              +---------+    +---------+
                    |              |
                    |              v
                    |        +---------+
                    |        |FIN_WAIT_2|
                    |        +---------+
                    |              |
                    v              v
              +---------+    +---------+
              |CLOSE_WAIT|   |TIME_WAIT|
              +---------+    +---------+
                    |              |
                    v              v
              +---------+    +---------+
              | LAST_ACK|    |  CLOSED |
              +---------+    +---------+
```

## TCP States Explained

### 1. CLOSED
- **Description**: No connection exists. Initial state before any operation.
- **Application Impact**: Socket is available for new connections.

### 2. LISTEN
- **Description**: Server is waiting for incoming connection requests.
- **Entry**: Server calls `listen()` after `bind()`.
- **Application Impact**: Server can accept multiple incoming connections.

### 3. SYN_SENT
- **Description**: Client sent SYN and is waiting for matching SYN+ACK.
- **Entry**: Client calls `connect()`.
- **Application Impact**: Connection attempt in progress; blocking or non-blocking behavior depends on socket mode.

### 4. SYN_RCVD (SYN_RECEIVED)
- **Description**: Server received SYN, sent SYN+ACK, waiting for ACK.
- **Entry**: Server accepts SYN packet.
- **Application Impact**: Vulnerable to SYN flood attacks; half-open connection.

### 5. ESTABLISHED
- **Description**: Connection is fully established; data transfer can occur.
- **Entry**: Three-way handshake completed.
- **Application Impact**: Normal read/write operations available.

### 6. FIN_WAIT_1
- **Description**: Local side initiated close; waiting for FIN acknowledgment.
- **Entry**: Application calls `close()` or `shutdown(SHUT_WR)`.
- **Application Impact**: Can still receive data; sending is disabled.

### 7. FIN_WAIT_2
- **Description**: Received ACK for our FIN; waiting for remote FIN.
- **Application Impact**: Half-closed state; can receive until remote closes.

### 8. CLOSE_WAIT
- **Description**: Remote side closed; local side must close too.
- **Entry**: Received FIN from remote.
- **Application Impact**: Application should close socket; resource leak if forgotten.

### 9. CLOSING
- **Description**: Both sides initiated close simultaneously.
- **Entry**: Sent FIN, received FIN before ACK of our FIN.
- **Application Impact**: Rare state; waiting for final ACK.

### 10. LAST_ACK
- **Description**: Waiting for final ACK after sending FIN in response to remote FIN.
- **Entry**: Application closed after receiving remote FIN.
- **Application Impact**: Connection about to fully close.

### 11. TIME_WAIT
- **Description**: Waiting 2*MSL (Maximum Segment Lifetime) to ensure remote received ACK.
- **Duration**: Typically 30-120 seconds (2*MSL).
- **Application Impact**: Socket remains in kernel; can't immediately reuse address without SO_REUSEADDR.

## Code Examples

### C Implementation: Monitoring TCP States

```c
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
```

### C++ Implementation: Advanced State Management

```cpp
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
```

### Rust Implementation: Safe State Management

```rust
use std::io::{self, Read, Write};
use std::net::{TcpListener, TcpStream, Shutdown};
use std::time::Duration;
use std::thread;

#[derive(Debug, Clone, Copy, PartialEq)]
enum TcpState {
    Closed,
    Listen,
    SynSent,
    SynReceived,
    Established,
    FinWait1,
    FinWait2,
    CloseWait,
    Closing,
    LastAck,
    TimeWait,
}

impl TcpState {
    fn as_str(&self) -> &'static str {
        match self {
            TcpState::Closed => "CLOSED",
            TcpState::Listen => "LISTEN",
            TcpState::SynSent => "SYN_SENT",
            TcpState::SynReceived => "SYN_RECEIVED",
            TcpState::Established => "ESTABLISHED",
            TcpState::FinWait1 => "FIN_WAIT1",
            TcpState::FinWait2 => "FIN_WAIT2",
            TcpState::CloseWait => "CLOSE_WAIT",
            TcpState::Closing => "CLOSING",
            TcpState::LastAck => "LAST_ACK",
            TcpState::TimeWait => "TIME_WAIT",
        }
    }
}

/// Wrapper around TcpStream that tracks state transitions
struct TcpConnection {
    stream: TcpStream,
    current_state: TcpState,
    peer_addr: String,
}

impl TcpConnection {
    fn new(stream: TcpStream) -> io::Result<Self> {
        let peer_addr = stream.peer_addr()?.to_string();
        
        Ok(TcpConnection {
            stream,
            current_state: TcpState::Established,
            peer_addr,
        })
    }
    
    fn from_connect(addr: &str) -> io::Result<Self> {
        println!("Initiating connection to {}", addr);
        println!("State transition: CLOSED -> SYN_SENT");
        
        // TcpStream::connect handles the three-way handshake
        let stream = TcpStream::connect(addr)?;
        
        println!("State transition: SYN_SENT -> ESTABLISHED");
        
        let peer_addr = stream.peer_addr()?.to_string();
        
        Ok(TcpConnection {
            stream,
            current_state: TcpState::Established,
            peer_addr,
        })
    }
    
    fn print_state(&self, event: &str) {
        println!("[{}] Current state: {} (peer: {})", 
                 event, self.current_state.as_str(), self.peer_addr);
    }
    
    fn state_transition(&mut self, event: &str, new_state: TcpState) {
        if self.current_state != new_state {
            println!("State transition [{}]: {} -> {}", 
                     event, self.current_state.as_str(), new_state.as_str());
            self.current_state = new_state;
        }
    }
    
    fn send_data(&mut self, data: &[u8]) -> io::Result<usize> {
        if self.current_state != TcpState::Established {
            return Err(io::Error::new(
                io::ErrorKind::NotConnected,
                format!("Cannot send in {} state", self.current_state.as_str())
            ));
        }
        
        let sent = self.stream.write(data)?;
        self.stream.flush()?;
        println!("Sent {} bytes", sent);
        Ok(sent)
    }
    
    fn receive_data(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        match self.stream.read(buffer) {
            Ok(0) => {
                // Received FIN from remote
                self.state_transition("received FIN", TcpState::CloseWait);
                Ok(0)
            }
            Ok(n) => {
                println!("Received {} bytes", n);
                Ok(n)
            }
            Err(e) => Err(e),
        }
    }
    
    fn shutdown_write(&mut self) -> io::Result<()> {
        println!("\nInitiating graceful shutdown (write side)");
        
        // This sends FIN, transitioning to FIN_WAIT1
        self.state_transition("shutdown(Write)", TcpState::FinWait1);
        self.stream.shutdown(Shutdown::Write)?;
        
        // After receiving ACK for our FIN, moves to FIN_WAIT2
        thread::sleep(Duration::from_millis(100));
        self.state_transition("received ACK for FIN", TcpState::FinWait2);
        
        Ok(())
    }
    
    fn close(mut self) {
        println!("\nClosing connection");
        self.print_state("before close");
        
        // Drop causes the connection to close
        // This sends FIN if not already sent
        drop(self.stream);
        
        println!("Connection closed, entering TIME_WAIT state (2*MSL wait)");
    }
    
    /// Demonstrates half-close scenario
    fn demonstrate_half_close(&mut self) -> io::Result<()> {
        println!("\n=== Demonstrating Half-Close ===");
        
        // Send final message
        self.send_data(b"Final message before half-close\n")?;
        
        // Close write side (send FIN)
        self.shutdown_write()?;
        
        // Can still receive data
        println!("Can still receive data in half-closed state...");
        let mut buffer = [0u8; 1024];
        
        loop {
            match self.receive_data(&mut buffer) {
                Ok(0) => {
                    println!("Received FIN from remote, connection fully closed");
                    break;
                }
                Ok(n) => {
                    let data = String::from_utf8_lossy(&buffer[..n]);
                    println!("Received in half-closed state: {}", data);
                }
                Err(e) => {
                    println!("Error receiving: {}", e);
                    break;
                }
            }
        }
        
        Ok(())
    }
}

/// Server that demonstrates passive open states
struct TcpServer {
    listener: TcpListener,
    state: TcpState,
}

impl TcpServer {
    fn new(addr: &str) -> io::Result<Self> {
        println!("Creating server socket");
        println!("State: CLOSED");
        
        let listener = TcpListener::bind(addr)?;
        println!("Bound to {}", addr);
        
        // SO_REUSEADDR is set by default in Rust
        
        println!("State transition: CLOSED -> LISTEN");
        
        Ok(TcpServer {
            listener,
            state: TcpState::Listen,
        })
    }
    
    fn accept(&mut self) -> io::Result<TcpConnection> {
        println!("\nWaiting for connection (state: {})...", self.state.as_str());
        
        let (stream, addr) = self.listener.accept()?;
        
        println!("Accepted connection from {}", addr);
        println!("State transition: LISTEN -> SYN_RECEIVED -> ESTABLISHED");
        
        TcpConnection::new(stream)
    }
}

/// Client demonstration
fn run_client(server_addr: &str) -> io::Result<()> {
    println!("=== TCP Client State Demonstration ===\n");
    
    // Connect (CLOSED -> SYN_SENT -> ESTABLISHED)
    let mut conn = TcpConnection::from_connect(server_addr)?;
    conn.print_state("after connect");
    
    // Set timeouts
    conn.stream.set_read_timeout(Some(Duration::from_secs(5)))?;
    conn.stream.set_write_timeout(Some(Duration::from_secs(5)))?;
    
    // Send some data
    conn.send_data(b"Hello from Rust client\n")?;
    
    // Receive response
    let mut buffer = [0u8; 1024];
    if let Ok(n) = conn.receive_data(&mut buffer) {
        if n > 0 {
            let response = String::from_utf8_lossy(&buffer[..n]);
            println!("Received response: {}", response);
        }
    }
    
    // Demonstrate half-close
    conn.demonstrate_half_close()?;
    
    // Full close
    conn.close();
    
    println!("\nClient finished, socket in TIME_WAIT/CLOSED");
    
    Ok(())
}

/// Server demonstration
fn run_server(bind_addr: &str) -> io::Result<()> {
    println!("=== TCP Server State Demonstration ===\n");
    
    let mut server = TcpServer::new(bind_addr)?;
    
    println!("Server ready on {}", bind_addr);
    
    // Accept one connection
    let mut conn = server.accept()?;
    conn.print_state("after accept");
    
    // Receive data
    let mut buffer = [0u8; 1024];
    
    loop {
        match conn.receive_data(&mut buffer) {
            Ok(0) => {
                // Client closed (received FIN)
                println!("\nClient initiated close");
                conn.print_state("received FIN");
                
                // Server should close too to avoid staying in CLOSE_WAIT
                println!("Server closing to transition from CLOSE_WAIT");
                
                // Send any final data
                conn.send_data(b"Goodbye from server\n")?;
                
                // Close connection (sends FIN, moves to LAST_ACK)
                println!("State: CLOSE_WAIT -> LAST_ACK -> CLOSED");
                drop(conn);
                
                break;
            }
            Ok(n) => {
                let data = String::from_utf8_lossy(&buffer[..n]);
                println!("Received: {}", data);
                
                // Echo back
                conn.send_data(&buffer[..n])?;
            }
            Err(e) => {
                println!("Error: {}", e);
                break;
            }
        }
    }
    
    println!("\nServer finished");
    
    Ok(())
}

/// Demonstrates TIME_WAIT state and SO_REUSEADDR
fn demonstrate_time_wait() -> io::Result<()> {
    println!("\n=== Demonstrating TIME_WAIT State ===\n");
    
    let addr = "127.0.0.1:9999";
    
    println!("Creating first server on {}", addr);
    {
        let _server = TcpServer::new(addr)?;
        println!("Server in LISTEN state");
        // Server drops here, goes to CLOSED
    }
    
    println!("\nServer closed");
    println!("Attempting to immediately rebind to same address...");
    
    // This works because Rust sets SO_REUSEADDR by default
    match TcpServer::new(addr) {
        Ok(_) => println!("SUCCESS: Rebound immediately (SO_REUSEADDR enabled)"),
        Err(e) => println!("FAILED: {} (would need SO_REUSEADDR)", e),
    }
    
    Ok(())
}

fn main() -> io::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        println!("Usage:");
        println!("  Server: {} server [addr:port]", args[0]);
        println!("  Client: {} client [addr:port]", args[0]);
        println!("  TIME_WAIT demo: {} timewait", args[0]);
        return Ok(());
    }
    
    match args[1].as_str() {
        "server" => {
            let addr = if args.len() > 2 { &args[2] } else { "127.0.0.1:8080" };
            run_server(addr)?;
        }
        "client" => {
            let addr = if args.len() > 2 { &args[2] } else { "127.0.0.1:8080" };
            run_client(addr)?;
        }
        "timewait" => {
            demonstrate_time_wait()?;
        }
        _ => {
            println!("Unknown command: {}", args[1]);
        }
    }
    
    Ok(())
}
```

## Application Design Implications

### 1. **CLOSE_WAIT Accumulation**
- **Problem**: Server receives FIN but application doesn't close socket
- **Symptom**: Growing number of connections in CLOSE_WAIT state
- **Solution**: Always close sockets after detecting remote closure (recv returns 0)

```c
// Proper handling of remote closure
while ((n = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
    process_data(buffer, n);
}

if (n == 0) {
    // Remote closed - MUST close our end too
    close(sockfd);  // Prevents CLOSE_WAIT accumulation
}
```

### 2. **TIME_WAIT State Management**

**Why TIME_WAIT Exists:**
- Ensures remote receives final ACK
- Prevents old duplicate packets from confusing new connections
- Duration: 2*MSL (typically 60-120 seconds)

**Handling TIME_WAIT:**

```c
// Server: Enable SO_REUSEADDR to rebind quickly
int reuse = 1;
setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

// This allows binding to TIME_WAIT addresses
bind(listenfd, ...);
```

### 3. **Half-Close Pattern**

Useful for protocols where one side finishes sending but continues receiving:

```rust
// Rust example
conn.stream.shutdown(Shutdown::Write)?; // Send FIN
// Can still receive data
while let Ok(n) = conn.stream.read(&mut buffer) {
    if n == 0 break; // Remote also closed
}
```

### 4. **Non-Blocking Connect**

Monitor SYN_SENT state for timeout handling:

```c
// Set non-blocking
fcntl(sockfd, F_SETFL, O_NONBLOCK);

// Initiate connect (returns immediately)
connect(sockfd, addr, addrlen); // Returns -1, errno = EINPROGRESS

// Monitor with select/poll
fd_set wfds;
FD_ZERO(&wfds);
FD_SET(sockfd, &wfds);

struct timeval tv = {5, 0}; // 5 second timeout
if (select(sockfd + 1, NULL, &wfds, NULL, &tv) > 0) {
    // Check for errors
    int error;
    socklen_t len = sizeof(error);
    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    
    if (error == 0) {
        // Connection successful (ESTABLISHED)
    }
}
```

### 5. **SYN Flood Protection**

**Problem**: Attackers send many SYNs, exhausting SYN_RCVD queue

**Solutions:**
```c
// Tune SYN backlog
listen(sockfd, SOMAXCONN); // Use maximum

// Enable SYN cookies (Linux kernel parameter)
// echo 1 > /proc/sys/net/ipv4/tcp_syncookies

// Monitor SYN queue
struct tcp_info info;
socklen_t len = sizeof(info);
getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &info, &len);
// Check info.tcpi_unacked for pending SYNs
```

### 6. **Graceful Shutdown Sequence**

```cpp
// Proper shutdown sequence
void graceful_close(int sockfd) {
    // 1. Stop sending
    shutdown(sockfd, SHUT_WR);  // -> FIN_WAIT1
    
    // 2. Drain receive buffer
    char buffer[1024];
    while (recv(sockfd, buffer, sizeof(buffer), 0) > 0) {
        // Discard or process
    }
    
    // 3. Close socket
    close(sockfd);  // -> TIME_WAIT or CLOSED
}
```

### 7. **State Monitoring for Debugging**

```c
void print_tcp_info(int sockfd) {
    struct tcp_info info;
    socklen_t len = sizeof(info);
    
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &info, &len) == 0) {
        printf("State: %d\n", info.tcpi_state);
        printf("RTT: %u us\n", info.tcpi_rtt);
        printf("Retransmits: %u\n", info.tcpi_retransmits);
        printf("Unacked segments: %u\n", info.tcpi_unacked);
    }
}
```

## Common Issues and Solutions

### Issue 1: "Address already in use"
**Cause**: Previous connection in TIME_WAIT
**Solution**: Use SO_REUSEADDR on servers

### Issue 2: Connections stuck in CLOSE_WAIT
**Cause**: Application not closing after receiving FIN
**Solution**: Always close when recv() returns 0

### Issue 3: Connection timeouts during high load
**Cause**: SYN queue overflow
**Solution**: Increase listen backlog, enable SYN cookies

### Issue 4: Data loss during shutdown
**Cause**: Abrupt close() with unsent data
**Solution**: Use shutdown(SHUT_WR) before close()

## Summary

The TCP state machine defines 11 distinct states that a connection transitions through from creation to termination. Understanding these states is essential for:

1. **Proper Resource Management**: Avoiding CLOSE_WAIT accumulation and handling TIME_WAIT appropriately
2. **Graceful Shutdowns**: Implementing half-close patterns for bidirectional protocols
3. **Error Handling**: Detecting and recovering from connection failures at each state
4. **Performance Tuning**: Managing SYN queues and connection timeouts
5. **Security**: Protecting against SYN flood attacks and other state-related vulnerabilities

Key takeaways:
- Always close sockets after detecting remote closure to prevent CLOSE_WAIT leaks
- Use SO_REUSEADDR on servers to handle TIME_WAIT states
- Implement proper shutdown sequences with shutdown() before close()
- Monitor connection states for debugging and performance analysis
- Handle non-blocking connects carefully to detect failures early

The state machine ensures reliable, ordered data delivery while managing connection lifecycle events. Application developers must understand these transitions to build robust network applications that properly handle connection establishment, data transfer, and graceful termination.