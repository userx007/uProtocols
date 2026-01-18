# SO_LINGER and Connection Cleanup

## Detailed Description

`SO_LINGER` is a critical socket option that controls what happens when you close a socket that still has unsent data in its buffers or unread data waiting to be received. Understanding and properly configuring this option is essential for graceful connection termination and preventing data loss in network applications.

### The Problem SO_LINGER Solves

When you call `close()` on a socket, the default behavior is to return immediately, even if there's still data queued for transmission. The socket then enters a background cleanup process. This can lead to several issues:

1. **Data Loss**: The application may think data was sent successfully, but it's still sitting in buffers
2. **Uncontrolled Shutdown**: No way to ensure a clean, orderly connection termination
3. **TIME_WAIT State**: Sockets may linger in TIME_WAIT state, consuming resources

### How SO_LINGER Works

The `SO_LINGER` option uses a structure that contains two fields:

- **l_onoff**: Enable (1) or disable (0) lingering
- **l_linger**: Timeout value in seconds (when enabled)

There are three distinct behaviors based on how you configure SO_LINGER:

**1. Default Behavior (l_onoff = 0)**
- `close()` returns immediately
- TCP attempts to send remaining data in the background
- Socket enters TIME_WAIT state normally

**2. Graceful Linger (l_onoff = 1, l_linger > 0)**
- `close()` blocks until all data is sent and acknowledged OR timeout expires
- Allows orderly shutdown with guarantees
- Returns -1 with EWOULDBLOCK/EAGAIN if timeout occurs

**3. Abortive Close (l_onoff = 1, l_linger = 0)**
- `close()` sends RST (reset) instead of FIN
- Discards any pending data
- Avoids TIME_WAIT state
- Hard termination - not recommended for normal use

### TIME_WAIT and Connection Cleanup

The TIME_WAIT state is a normal part of TCP connection termination. When you close a connection, it remains in TIME_WAIT for typically 2*MSL (Maximum Segment Lifetime, often 60-120 seconds) to ensure:
- Delayed packets from the connection don't interfere with new connections
- The remote side receives the final ACK

SO_LINGER with a zero timeout bypasses TIME_WAIT by sending RST, but this can cause issues with reliable data delivery and should be used sparingly.

## C/C++ Code Examples

```c

```

### Example 1: Basic SO_LINGER Configuration

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// Demonstrates three different SO_LINGER configurations

void set_linger_default(int sockfd) {
    struct linger sl;
    sl.l_onoff = 0;  // Disable linger
    sl.l_linger = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) < 0) {
        perror("setsockopt SO_LINGER (default)");
    } else {
        printf("SO_LINGER: Default behavior (l_onoff=0)\n");
        printf("  - close() returns immediately\n");
        printf("  - TCP sends data in background\n");
        printf("  - Normal TIME_WAIT state\n\n");
    }
}

void set_linger_graceful(int sockfd, int timeout_seconds) {
    struct linger sl;
    sl.l_onoff = 1;  // Enable linger
    sl.l_linger = timeout_seconds;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) < 0) {
        perror("setsockopt SO_LINGER (graceful)");
    } else {
        printf("SO_LINGER: Graceful linger (l_onoff=1, l_linger=%d)\n", 
               timeout_seconds);
        printf("  - close() blocks up to %d seconds\n", timeout_seconds);
        printf("  - Waits for data to be sent and ACKed\n");
        printf("  - Returns -1 on timeout\n\n");
    }
}

void set_linger_abortive(int sockfd) {
    struct linger sl;
    sl.l_onoff = 1;  // Enable linger
    sl.l_linger = 0; // Zero timeout = abortive close
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) < 0) {
        perror("setsockopt SO_LINGER (abortive)");
    } else {
        printf("SO_LINGER: Abortive close (l_onoff=1, l_linger=0)\n");
        printf("  - Sends RST instead of FIN\n");
        printf("  - Discards pending data\n");
        printf("  - Avoids TIME_WAIT state\n");
        printf("  - WARNING: Can lose data!\n\n");
    }
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }
    
    printf("=== SO_LINGER Configuration Examples ===\n\n");
    
    // Demonstrate each configuration
    set_linger_default(sockfd);
    set_linger_graceful(sockfd, 10);
    set_linger_abortive(sockfd);
    
    // Query current SO_LINGER setting
    struct linger current_linger;
    socklen_t optlen = sizeof(current_linger);
    
    if (getsockopt(sockfd, SOL_SOCKET, SO_LINGER, 
                   &current_linger, &optlen) == 0) {
        printf("Current SO_LINGER setting:\n");
        printf("  l_onoff  = %d\n", current_linger.l_onoff);
        printf("  l_linger = %d seconds\n", current_linger.l_linger);
    }
    
    close(sockfd);
    return 0;
}
```

### Example 2: Graceful Server Shutdown with SO_LINGER

```c
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
```

### Example 3: Comparison of Close Behaviors

```cpp
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
```

## Rust Code Examples

### Example 1: Basic SO_LINGER Configuration in Rust

```rust
use std::net::TcpStream;
use std::os::fd::AsRawFd;
use std::time::Duration;

// Import libc for direct socket option manipulation
extern crate libc;

#[derive(Debug)]
enum LingerMode {
    Default,
    Graceful(Duration),
    Abortive,
}

struct SocketLinger {
    stream: TcpStream,
}

impl SocketLinger {
    fn new(stream: TcpStream) -> Self {
        Self { stream }
    }
    
    fn set_linger(&self, mode: LingerMode) -> Result<(), std::io::Error> {
        let fd = self.stream.as_raw_fd();
        
        let linger_struct = match mode {
            LingerMode::Default => {
                println!("Setting DEFAULT linger mode:");
                println!("  - close() returns immediately");
                println!("  - TCP sends data in background");
                println!("  - Normal TIME_WAIT state");
                
                libc::linger {
                    l_onoff: 0,
                    l_linger: 0,
                }
            }
            LingerMode::Graceful(timeout) => {
                let seconds = timeout.as_secs() as i32;
                println!("Setting GRACEFUL linger mode:");
                println!("  - close() blocks up to {} seconds", seconds);
                println!("  - Waits for data to be sent and ACKed");
                println!("  - Returns error on timeout");
                
                libc::linger {
                    l_onoff: 1,
                    l_linger: seconds,
                }
            }
            LingerMode::Abortive => {
                println!("Setting ABORTIVE linger mode:");
                println!("  - Sends RST instead of FIN");
                println!("  - Discards pending data");
                println!("  - Avoids TIME_WAIT state");
                println!("  - WARNING: Can lose data!");
                
                libc::linger {
                    l_onoff: 1,
                    l_linger: 0,
                }
            }
        };
        
        unsafe {
            let result = libc::setsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_LINGER,
                &linger_struct as *const _ as *const libc::c_void,
                std::mem::size_of::<libc::linger>() as libc::socklen_t,
            );
            
            if result < 0 {
                return Err(std::io::Error::last_os_error());
            }
        }
        
        Ok(())
    }
    
    fn get_linger(&self) -> Result<(bool, i32), std::io::Error> {
        let fd = self.stream.as_raw_fd();
        let mut linger_struct = libc::linger {
            l_onoff: 0,
            l_linger: 0,
        };
        let mut len = std::mem::size_of::<libc::linger>() as libc::socklen_t;
        
        unsafe {
            let result = libc::getsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_LINGER,
                &mut linger_struct as *mut _ as *mut libc::c_void,
                &mut len,
            );
            
            if result < 0 {
                return Err(std::io::Error::last_os_error());
            }
        }
        
        Ok((linger_struct.l_onoff != 0, linger_struct.l_linger))
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("=== SO_LINGER Configuration Examples ===\n");
    
    // Create a socket (not connected, just for demonstration)
    let stream = TcpStream::connect("127.0.0.1:1")
        .or_else(|_| {
            // If connection fails, create a socket anyway for demonstration
            // This is just for showing the API
            use std::net::{SocketAddr, TcpListener};
            let listener = TcpListener::bind("127.0.0.1:0")?;
            let addr = listener.local_addr()?;
            
            // Create a connection to ourselves
            let handle = std::thread::spawn(move || {
                listener.accept().ok()
            });
            
            let stream = TcpStream::connect(addr)?;
            handle.join().ok();
            Ok(stream)
        })?;
    
    let socket = SocketLinger::new(stream);
    
    // Demonstrate DEFAULT mode
    println!("\n--- Configuration 1 ---");
    socket.set_linger(LingerMode::Default)?;
    let (enabled, timeout) = socket.get_linger()?;
    println!("Current: l_onoff={}, l_linger={}\n", enabled, timeout);
    
    // Demonstrate GRACEFUL mode
    println!("--- Configuration 2 ---");
    socket.set_linger(LingerMode::Graceful(Duration::from_secs(10)))?;
    let (enabled, timeout) = socket.get_linger()?;
    println!("Current: l_onoff={}, l_linger={}\n", enabled, timeout);
    
    // Demonstrate ABORTIVE mode
    println!("--- Configuration 3 ---");
    socket.set_linger(LingerMode::Abortive)?;
    let (enabled, timeout) = socket.get_linger()?;
    println!("Current: l_onoff={}, l_linger={}\n", enabled, timeout);
    
    println!("\n=== Recommendations ===");
    println!("• Use DEFAULT for most applications");
    println!("• Use GRACEFUL when data delivery is critical");
    println!("• Use ABORTIVE only in special cases (testing, emergency shutdown)");
    
    Ok(())
}
```

### Example 2: Graceful Shutdown with Tokio

```rust
// Cargo.toml dependencies:
// tokio = { version = "1", features = ["full"] }
// libc = "0.2"

use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::os::fd::AsRawFd;
use std::time::{Duration, Instant};

/// Sets SO_LINGER option on a TcpStream
fn set_linger(stream: &TcpStream, timeout_secs: Option<u32>) -> std::io::Result<()> {
    let fd = stream.as_raw_fd();
    
    let linger = match timeout_secs {
        Some(secs) => libc::linger {
            l_onoff: 1,
            l_linger: secs as i32,
        },
        None => libc::linger {
            l_onoff: 0,
            l_linger: 0,
        },
    };
    
    unsafe {
        let result = libc::setsockopt(
            fd,
            libc::SOL_SOCKET,
            libc::SO_LINGER,
            &linger as *const _ as *const libc::c_void,
            std::mem::size_of::<libc::linger>() as libc::socklen_t,
        );
        
        if result < 0 {
            return Err(std::io::Error::last_os_error());
        }
    }
    
    Ok(())
}

/// Gracefully close a connection with proper cleanup
async fn graceful_close(
    mut stream: TcpStream,
    context: &str
) -> std::io::Result<()> {
    println!("[{}] Initiating graceful shutdown...", context);
    
    // Set SO_LINGER before closing
    const LINGER_TIMEOUT: u32 = 5;
    set_linger(&stream, Some(LINGER_TIMEOUT))?;
    
    // Shutdown the write half (send FIN)
    stream.shutdown().await?;
    println!("[{}] Sent FIN, waiting for peer acknowledgment...", context);
    
    // Read until EOF (peer closes their write side)
    let mut buffer = [0u8; 1024];
    loop {
        match stream.read(&mut buffer).await {
            Ok(0) => {
                println!("[{}] Received EOF from peer", context);
                break;
            }
            Ok(n) => {
                println!("[{}] Draining {} bytes from peer", context, n);
            }
            Err(e) => {
                println!("[{}] Error reading: {}", context, e);
                break;
            }
        }
    }
    
    // Now close the socket (with linger)
    let start = Instant::now();
    drop(stream);
    let elapsed = start.elapsed();
    
    println!("[{}] Socket closed after {:?}", context, elapsed);
    println!("[{}] Graceful shutdown complete", context);
    
    Ok(())
}

/// Handle a client connection with guaranteed data delivery
async fn handle_client(stream: TcpStream, addr: std::net::SocketAddr) {
    println!("New connection from: {}", addr);
    
    let mut stream = stream;
    let mut buffer = [0u8; 1024];
    
    // Read request
    match stream.read(&mut buffer).await {
        Ok(n) if n > 0 => {
            let request = String::from_utf8_lossy(&buffer[..n]);
            println!("Received: {}", request.trim());
            
            // Prepare response
            let response = format!(
                "HTTP/1.1 200 OK\r\n\
                 Content-Type: text/plain\r\n\
                 Content-Length: 30\r\n\
                 Connection: close\r\n\
                 \r\n\
                 Graceful shutdown successful!\n"
            );
            
            // Send response with error handling
            match stream.write_all(response.as_bytes()).await {
                Ok(_) => {
                    println!("Response sent successfully");
                    
                    // Flush to ensure data is sent
                    if let Err(e) = stream.flush().await {
                        eprintln!("Flush error: {}", e);
                    }
                }
                Err(e) => {
                    eprintln!("Send error: {}", e);
                }
            }
        }
        Ok(_) => println!("Client closed connection immediately"),
        Err(e) => eprintln!("Read error: {}", e),
    }
    
    // Perform graceful close
    if let Err(e) = graceful_close(stream, &format!("Client {}", addr)).await {
        eprintln!("Error during graceful close: {}", e);
    }
}

#[tokio::main]
async fn main() -> std::io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("Server listening on 127.0.0.1:8080");
    println!("Using SO_LINGER for guaranteed data delivery\n");
    
    // Accept connections
    loop {
        match listener.accept().await {
            Ok((stream, addr)) => {
                // Spawn a task for each connection
                tokio::spawn(async move {
                    handle_client(stream, addr).await;
                });
            }
            Err(e) => {
                eprintln!("Accept error: {}", e);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[tokio::test]
    async fn test_graceful_shutdown() {
        // Start a test server
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();
        
        // Spawn server task
        tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            graceful_close(stream, "Test").await.ok();
        });
        
        // Connect client
        let mut client = TcpStream::connect(addr).await.unwrap();
        
        // Send some data
        client.write_all(b"Test data").await.unwrap();
        
        // Read response (should be EOF after graceful close)
        let mut buffer = [0u8; 1024];
        let n = client.read(&mut buffer).await.unwrap();
        assert_eq!(n, 0, "Should receive EOF");
    }
}
```

### Example 3: Socket Wrapper with Linger Control

```rust
use std::net::TcpStream;
use std::os::fd::AsRawFd;
use std::time::{Duration, Instant};
use std::io::{Read, Write};

#[derive(Debug, Clone, Copy)]
pub enum LingerOption {
    /// Default behavior: close() returns immediately
    Disabled,
    /// Graceful close with timeout
    Enabled { timeout: Duration },
    /// Abortive close (RST)
    Abortive,
}

pub struct ManagedSocket {
    stream: Option<TcpStream>,
    linger: LingerOption,
}

impl ManagedSocket {
    pub fn new(stream: TcpStream, linger: LingerOption) -> std::io::Result<Self> {
        let socket = Self {
            stream: Some(stream),
            linger,
        };
        
        // Apply linger setting
        socket.apply_linger()?;
        Ok(socket)
    }
    
    fn apply_linger(&self) -> std::io::Result<()> {
        let stream = self.stream.as_ref().ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::NotConnected, "Socket closed")
        })?;
        
        let fd = stream.as_raw_fd();
        
        let linger_struct = match self.linger {
            LingerOption::Disabled => libc::linger {
                l_onoff: 0,
                l_linger: 0,
            },
            LingerOption::Enabled { timeout } => libc::linger {
                l_onoff: 1,
                l_linger: timeout.as_secs() as i32,
            },
            LingerOption::Abortive => libc::linger {
                l_onoff: 1,
                l_linger: 0,
            },
        };
        
        unsafe {
            let result = libc::setsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_LINGER,
                &linger_struct as *const _ as *const libc::c_void,
                std::mem::size_of::<libc::linger>() as libc::socklen_t,
            );
            
            if result < 0 {
                return Err(std::io::Error::last_os_error());
            }
        }
        
        Ok(())
    }
    
    pub fn send_all(&mut self, data: &[u8]) -> std::io::Result<usize> {
        let stream = self.stream.as_mut().ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::NotConnected, "Socket closed")
        })?;
        
        stream.write_all(data)?;
        stream.flush()?;
        Ok(data.len())
    }
    
    pub fn receive(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        let stream = self.stream.as_mut().ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::NotConnected, "Socket closed")
        })?;
        
        stream.read(buffer)
    }
    
    pub fn close(mut self) -> Result<Duration, std::io::Error> {
        if let Some(stream) = self.stream.take() {
            println!("Closing socket with linger: {:?}", self.linger);
            
            let start = Instant::now();
            drop(stream);
            let elapsed = start.elapsed();
            
            match self.linger {
                LingerOption::Disabled => {
                    println!("Socket closed immediately (background cleanup)");
                }
                LingerOption::Enabled { timeout } => {
                    println!("Socket closed after {:?} (max: {:?})", 
                            elapsed, timeout);
                    if elapsed >= timeout {
                        println!("WARNING: Linger timeout reached!");
                    }
                }
                LingerOption::Abortive => {
                    println!("Socket aborted with RST");
                }
            }
            
            Ok(elapsed)
        } else {
            Err(std::io::Error::new(
                std::io::ErrorKind::NotConnected,
                "Socket already closed"
            ))
        }
    }
}

impl Drop for ManagedSocket {
    fn drop(&mut self) {
        if self.stream.is_some() {
            println!("WARNING: ManagedSocket dropped without explicit close!");
            println!("Consider calling close() for controlled cleanup");
        }
    }
}

// Example usage
fn example_usage() -> std::io::Result<()> {
    println!("=== ManagedSocket Examples ===\n");
    
    // Example 1: Default behavior
    println!("--- Example 1: Default Linger ---");
    {
        let stream = create_test_socket()?;
        let mut socket = ManagedSocket::new(stream, LingerOption::Disabled)?;
        socket.send_all(b"Hello with default linger")?;
        socket.close()?;
    }
    
    println!("\n--- Example 2: Graceful Linger ---");
    {
        let stream = create_test_socket()?;
        let mut socket = ManagedSocket::new(
            stream,
            LingerOption::Enabled {
                timeout: Duration::from_secs(5),
            }
        )?;
        socket.send_all(b"Hello with graceful linger")?;
        socket.close()?;
    }
    
    println!("\n--- Example 3: Abortive Close ---");
    {
        let stream = create_test_socket()?;
        let mut socket = ManagedSocket::new(stream, LingerOption::Abortive)?;
        socket.send_all(b"Hello with abortive close")?;
        socket.close()?;
    }
    
    Ok(())
}

// Helper to create a test socket
fn create_test_socket() -> std::io::Result<TcpStream> {
    use std::net::TcpListener;
    
    // Create a listener on a random port
    let listener = TcpListener::bind("127.0.0.1:0")?;
    let addr = listener.local_addr()?;
    
    // Connect to ourselves
    let handle = std::thread::spawn(move || {
        listener.accept().ok()
    });
    
    let stream = TcpStream::connect(addr)?;
    handle.join().ok();
    
    Ok(stream)
}

fn main() -> std::io::Result<()> {
    example_usage()?;
    
    println!("\n=== Best Practices ===");
    println!("✓ Use Disabled for most cases (default behavior)");
    println!("✓ Use Enabled with timeout when data delivery is critical");
    println!("✓ Use Abortive sparingly (testing, emergency situations)");
    println!("✓ Always call close() explicitly for controlled cleanup");
    println!("✓ Handle timeout errors gracefully");
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_managed_socket_creation() {
        let stream = create_test_socket().unwrap();
        let socket = ManagedSocket::new(
            stream,
            LingerOption::Enabled {
                timeout: Duration::from_secs(1),
            }
        );
        assert!(socket.is_ok());
    }
    
    #[test]
    fn test_send_receive() {
        let stream = create_test_socket().unwrap();
        let mut socket = ManagedSocket::new(stream, LingerOption::Disabled).unwrap();
        
        let data = b"Test message";
        let sent = socket.send_all(data).unwrap();
        assert_eq!(sent, data.len());
    }
    
    #[test]
    fn test_close_timing() {
        let stream = create_test_socket().unwrap();
        let socket = ManagedSocket::new(
            stream,
            LingerOption::Enabled {
                timeout: Duration::from_secs(1),
            }
        ).unwrap();
        
        let elapsed = socket.close().unwrap();
        assert!(elapsed <= Duration::from_secs(2));
    }
}
```

## Summary

**SO_LINGER** is a socket option that provides fine-grained control over connection termination behavior, addressing the critical question: "What happens to pending data when I close a socket?"

### Key Takeaways:

**Three Operating Modes:**
1. **Default (l_onoff=0)**: `close()` returns immediately; TCP handles cleanup in the background; normal TIME_WAIT state
2. **Graceful (l_onoff=1, l_linger>0)**: `close()` blocks until data is acknowledged or timeout expires; provides delivery guarantees
3. **Abortive (l_onoff=1, l_linger=0)**: Sends RST to terminate immediately; discards pending data; avoids TIME_WAIT but risks data loss

**When to Use Each Mode:**
- **Default**: Appropriate for most applications where the OS can handle cleanup adequately
- **Graceful**: Critical when you need guaranteed data delivery (database connections, file transfers, financial transactions)
- **Abortive**: Rare cases only (testing, emergency shutdowns, avoiding port exhaustion in specific scenarios)

**Critical Considerations:**
- SO_LINGER with timeout can cause `close()` to block, potentially hanging your application
- Abortive closes bypass proper TCP shutdown, which can confuse peers and cause data loss
- TIME_WAIT exists for good reasons—avoiding it should be done carefully
- Always handle timeout errors when using graceful linger
- Combine with `shutdown()` for half-close scenarios to signal intent before final close

**Best Practices:**
- Set SO_LINGER before closing the socket
- Use non-zero timeouts (5-10 seconds) for graceful closes to prevent indefinite blocking
- Consider application-level acknowledgments for critical data instead of relying solely on SO_LINGER
- Monitor close duration to detect network issues
- Document your linger strategy in code comments

SO_LINGER is a powerful tool for controlling connection lifecycle, but it requires careful consideration of your application's reliability requirements and acceptable latency tradeoffs.