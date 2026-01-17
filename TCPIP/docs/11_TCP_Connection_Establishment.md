# TCP Connection Establishment: Three-Way Handshake and State Transitions

## Overview

TCP connection establishment uses a **three-way handshake** to synchronize sequence numbers and establish a reliable bidirectional communication channel between client and server. This process ensures both parties are ready to exchange data and agree on initial sequence numbers (ISNs) for tracking data segments.

## The Three-Way Handshake Process

The handshake involves three packet exchanges:

1. **SYN (Synchronize)**: Client sends a SYN packet with its initial sequence number
2. **SYN-ACK (Synchronize-Acknowledge)**: Server responds with its own SYN and acknowledges the client's SYN
3. **ACK (Acknowledge)**: Client acknowledges the server's SYN

```
Client                                Server
  |                                      |
  |  SYN (seq=x)                         |
  |------------------------------------->|
  |                                      |
  |            SYN-ACK (seq=y, ack=x+1)  |
  |<-------------------------------------|
  |                                      |
  |  ACK (ack=y+1)                       |
  |------------------------------------->|
  |                                      |
  |        Connection Established        |
```

## TCP State Transitions

During connection establishment, both endpoints transition through several states:

**Client States:**
- CLOSED → SYN_SENT → ESTABLISHED

**Server States:**
- CLOSED → LISTEN → SYN_RECEIVED → ESTABLISHED

## Code Examples

### C Implementation

Here's a basic TCP server and client demonstrating the three-way handshake:

```c
// TCP Server - Connection Establishment
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#define PORT 8080
#define BACKLOG 5

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int opt = 1;
    
    // Create socket - State: CLOSED
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Listen - State: LISTEN
    if (listen(server_fd, BACKLOG) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d (State: LISTEN)\n", PORT);
    
    // Accept connection - Three-way handshake occurs here
    // State transitions: LISTEN → SYN_RECEIVED → ESTABLISHED
    printf("Waiting for connection...\n");
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("Accept failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Connection established with %s:%d (State: ESTABLISHED)\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));
    
    // Get TCP info to see connection state
    struct tcp_info info;
    socklen_t info_len = sizeof(info);
    if (getsockopt(client_fd, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0) {
        printf("TCP State: %d (1=ESTABLISHED)\n", info.tcpi_state);
    }
    
    // Communication would happen here
    char buffer[1024];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Received: %s\n", buffer);
    }
    
    // Cleanup
    close(client_fd);
    close(server_fd);
    
    return 0;
}
```

```c
// TCP Client - Connection Establishment
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080

int main() {
    int sock_fd;
    struct sockaddr_in server_addr;
    
    // Create socket - State: CLOSED
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Socket created (State: CLOSED)\n");
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    
    // Connect - Three-way handshake happens here
    // State transitions: CLOSED → SYN_SENT → ESTABLISHED
    printf("Initiating connection (State: SYN_SENT)...\n");
    
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to server (State: ESTABLISHED)\n");
    
    // Get TCP info
    struct tcp_info info;
    socklen_t info_len = sizeof(info);
    if (getsockopt(sock_fd, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0) {
        printf("TCP State: %d (1=ESTABLISHED)\n", info.tcpi_state);
        printf("RTT: %u microseconds\n", info.tcpi_rtt);
    }
    
    // Send data
    const char* message = "Hello from TCP client!";
    if (send(sock_fd, message, strlen(message), 0) < 0) {
        perror("Send failed");
    } else {
        printf("Message sent: %s\n", message);
    }
    
    // Cleanup
    close(sock_fd);
    
    return 0;
}
```

### C++ Implementation

```cpp
// TCP Server with State Monitoring (C++)
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdexcept>

class TCPServer {
private:
    int server_fd;
    int port;
    
    const char* getStateName(int state) {
        switch(state) {
            case TCP_ESTABLISHED: return "ESTABLISHED";
            case TCP_SYN_SENT: return "SYN_SENT";
            case TCP_SYN_RECV: return "SYN_RECV";
            case TCP_LISTEN: return "LISTEN";
            case TCP_CLOSE: return "CLOSE";
            default: return "UNKNOWN";
        }
    }
    
public:
    TCPServer(int port) : server_fd(-1), port(port) {}
    
    ~TCPServer() {
        if (server_fd >= 0) {
            close(server_fd);
        }
    }
    
    void start() {
        struct sockaddr_in address;
        int opt = 1;
        
        // Create socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::runtime_error("Socket creation failed");
        }
        
        std::cout << "Socket created (State: CLOSED)" << std::endl;
        
        // Set socket options
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            throw std::runtime_error("setsockopt failed");
        }
        
        // Bind
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            throw std::runtime_error("Bind failed");
        }
        
        // Listen
        if (listen(server_fd, 5) < 0) {
            throw std::runtime_error("Listen failed");
        }
        
        std::cout << "Server listening on port " << port 
                  << " (State: LISTEN)" << std::endl;
    }
    
    int acceptConnection() {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        std::cout << "Waiting for connection..." << std::endl;
        std::cout << "Server State: LISTEN → (waiting for SYN)" << std::endl;
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            throw std::runtime_error("Accept failed");
        }
        
        std::cout << "Three-way handshake completed!" << std::endl;
        std::cout << "Connection established with " 
                  << inet_ntoa(client_addr.sin_addr) << ":" 
                  << ntohs(client_addr.sin_port) << std::endl;
        
        // Get connection info
        struct tcp_info info;
        socklen_t info_len = sizeof(info);
        if (getsockopt(client_fd, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0) {
            std::cout << "TCP State: " << getStateName(info.tcpi_state) << std::endl;
            std::cout << "RTT: " << info.tcpi_rtt << " microseconds" << std::endl;
        }
        
        return client_fd;
    }
};

int main() {
    try {
        TCPServer server(8080);
        server.start();
        
        int client = server.acceptConnection();
        
        // Handle client...
        char buffer[1024];
        ssize_t bytes = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            std::cout << "Received: " << buffer << std::endl;
        }
        
        close(client);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### Rust Implementation

```rust
// TCP Server with State Monitoring (Rust)
use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write, Result};
use std::time::Duration;

fn main() -> Result<()> {
    // Create and bind listener - State: CLOSED → LISTEN
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    println!("Server listening on 127.0.0.1:8080 (State: LISTEN)");
    
    // Set socket options
    listener.set_nonblocking(false)?;
    
    println!("Waiting for incoming connections...");
    
    // Accept connection - Three-way handshake occurs here
    // State: LISTEN → SYN_RECEIVED → ESTABLISHED
    for stream in listener.incoming() {
        match stream {
            Ok(mut stream) => {
                println!("\nThree-way handshake completed!");
                println!("Connection established with: {}", 
                         stream.peer_addr()?);
                println!("State: ESTABLISHED");
                
                // Set TCP options
                stream.set_read_timeout(Some(Duration::from_secs(5)))?;
                stream.set_nodelay(true)?; // Disable Nagle's algorithm
                
                // Handle the connection
                handle_client(&mut stream)?;
            }
            Err(e) => {
                eprintln!("Connection failed: {}", e);
            }
        }
    }
    
    Ok(())
}

fn handle_client(stream: &mut TcpStream) -> Result<()> {
    let mut buffer = [0u8; 1024];
    
    // Read data
    match stream.read(&mut buffer) {
        Ok(n) if n > 0 => {
            let received = String::from_utf8_lossy(&buffer[..n]);
            println!("Received {} bytes: {}", n, received);
            
            // Echo back
            stream.write_all(&buffer[..n])?;
            println!("Echoed data back to client");
        }
        Ok(_) => println!("Client closed connection"),
        Err(e) => eprintln!("Read error: {}", e),
    }
    
    Ok(())
}
```

```rust
// TCP Client with Connection Monitoring (Rust)
use std::net::TcpStream;
use std::io::{Write, Read, Result};
use std::time::Duration;

fn main() -> Result<()> {
    println!("Initiating TCP connection (State: CLOSED → SYN_SENT)");
    
    // Connect - Three-way handshake happens here
    // State transitions: CLOSED → SYN_SENT → ESTABLISHED
    match TcpStream::connect("127.0.0.1:8080") {
        Ok(mut stream) => {
            println!("Three-way handshake completed!");
            println!("Connected to server (State: ESTABLISHED)");
            println!("Local address: {}", stream.local_addr()?);
            println!("Remote address: {}", stream.peer_addr()?);
            
            // Set socket options
            stream.set_read_timeout(Some(Duration::from_secs(5)))?;
            stream.set_write_timeout(Some(Duration::from_secs(5)))?;
            stream.set_nodelay(true)?;
            
            // Send data
            let message = b"Hello from Rust TCP client!";
            stream.write_all(message)?;
            println!("Sent: {}", String::from_utf8_lossy(message));
            
            // Read response
            let mut buffer = [0u8; 1024];
            match stream.read(&mut buffer) {
                Ok(n) if n > 0 => {
                    println!("Received: {}", String::from_utf8_lossy(&buffer[..n]));
                }
                Ok(_) => println!("Server closed connection"),
                Err(e) => eprintln!("Read error: {}", e),
            }
        }
        Err(e) => {
            eprintln!("Connection failed (State: remained CLOSED): {}", e);
            return Err(e);
        }
    }
    
    Ok(())
}
```

```rust
// Advanced: Custom TCP State Machine Visualization
use std::net::TcpStream;
use std::io::Result;
use std::fmt;

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

impl fmt::Display for TcpState {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

struct TcpConnection {
    state: TcpState,
    stream: Option<TcpStream>,
}

impl TcpConnection {
    fn new() -> Self {
        println!("TCP Connection initialized");
        TcpConnection {
            state: TcpState::Closed,
            stream: None,
        }
    }
    
    fn connect(&mut self, addr: &str) -> Result<()> {
        println!("State transition: {} → {}", self.state, TcpState::SynSent);
        self.state = TcpState::SynSent;
        
        println!("Sending SYN packet...");
        
        // Actual connection attempt
        match TcpStream::connect(addr) {
            Ok(stream) => {
                println!("Received SYN-ACK from server");
                println!("Sending ACK packet...");
                println!("State transition: {} → {}", 
                         self.state, TcpState::Established);
                
                self.state = TcpState::Established;
                self.stream = Some(stream);
                println!("Connection established!");
                Ok(())
            }
            Err(e) => {
                println!("Connection failed, reverting to CLOSED state");
                self.state = TcpState::Closed;
                Err(e)
            }
        }
    }
    
    fn get_state(&self) -> TcpState {
        self.state
    }
}

fn main() -> Result<()> {
    let mut connection = TcpConnection::new();
    
    println!("Current state: {}", connection.get_state());
    
    match connection.connect("127.0.0.1:8080") {
        Ok(_) => {
            println!("Final state: {}", connection.get_state());
        }
        Err(e) => {
            eprintln!("Error: {}", e);
            println!("Final state: {}", connection.get_state());
        }
    }
    
    Ok(())
}
```

## Key Technical Details

**Sequence Numbers**: Each side chooses a random initial sequence number (ISN) to prevent sequence number prediction attacks. The ISN is typically derived from a timer or random number generator.

**Acknowledgment Numbers**: The ACK number is always the next expected sequence number (received sequence + 1).

**Timeout and Retransmission**: If the SYN or SYN-ACK is lost, TCP retransmits with exponential backoff. Most implementations try multiple times before giving up (typically 5-7 attempts).

**SYN Cookies**: Modern servers use SYN cookies to defend against SYN flood attacks, allowing stateless connection handling until the final ACK.

**TCP Options**: During the handshake, both sides can negotiate options like Maximum Segment Size (MSS), Window Scale, Selective Acknowledgment (SACK), and timestamps.

## Summary

TCP connection establishment through the three-way handshake is a fundamental mechanism ensuring reliable communication. The process involves precise state transitions on both client and server sides, with the client moving from CLOSED → SYN_SENT → ESTABLISHED and the server transitioning from LISTEN → SYN_RECEIVED → ESTABLISHED. The handshake synchronizes sequence numbers, negotiates TCP options, and establishes a bidirectional reliable channel. Both endpoints must successfully complete all three steps before data transmission begins. The examples in C, C++, and Rust demonstrate how operating systems abstract this complexity through socket APIs, with `connect()` and `accept()` handling the handshake automatically while exposing connection state information through socket options for monitoring and debugging purposes.