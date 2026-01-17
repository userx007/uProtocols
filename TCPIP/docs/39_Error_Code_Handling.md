# TCP/IP Error Code Handling: A Comprehensive Guide

## Overview

Error handling in TCP/IP programming is critical for building robust network applications. The operating system communicates errors through error codes (errno on POSIX systems), and understanding how to properly handle these codes—especially `EINTR`, `EAGAIN`/`EWOULDBLOCK`, and implementing recovery strategies—is essential for reliable network communication.

## Key Error Codes

### EINTR (Interrupted System Call)
Occurs when a system call is interrupted by a signal before it completes. The operation should typically be retried.

### EAGAIN/EWOULDBLOCK (Resource Temporarily Unavailable)
Indicates that a non-blocking operation cannot complete immediately but may succeed if retried later. On most systems, `EAGAIN` and `EWOULDBLOCK` have the same value.

### Other Common Errors
- **ECONNREFUSED**: Connection actively refused by remote host
- **ETIMEDOUT**: Connection attempt timed out
- **EPIPE**: Broken pipe (writing to closed socket)
- **ECONNRESET**: Connection reset by peer
- **EHOSTUNREACH**: No route to host

## C/C++ Implementation

### Basic Error Handling Pattern

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

// Safe wrapper for send() with EINTR handling
ssize_t send_with_retry(int sockfd, const void *buf, size_t len, int flags) {
    ssize_t result;
    const char *ptr = (const char *)buf;
    size_t remaining = len;
    
    while (remaining > 0) {
        result = send(sockfd, ptr, remaining, flags);
        
        if (result < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, retry
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, wait and retry (simplified)
                usleep(1000); // Wait 1ms
                continue;
            } else if (errno == EPIPE || errno == ECONNRESET) {
                // Connection broken, cannot recover
                perror("Connection broken");
                return -1;
            } else {
                // Other error
                perror("send error");
                return -1;
            }
        }
        
        ptr += result;
        remaining -= result;
    }
    
    return len;
}

// Safe wrapper for recv() with EINTR handling
ssize_t recv_with_retry(int sockfd, void *buf, size_t len, int flags) {
    ssize_t result;
    
    while (1) {
        result = recv(sockfd, buf, len, flags);
        
        if (result < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, retry
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block on non-blocking socket
                return 0; // Return 0 to indicate no data available
            } else if (errno == ECONNRESET) {
                fprintf(stderr, "Connection reset by peer\n");
                return -1;
            } else {
                perror("recv error");
                return -1;
            }
        }
        
        // result >= 0
        return result;
    }
}

// Safe wrapper for connect() with timeout and retry
int connect_with_timeout(int sockfd, const struct sockaddr *addr, 
                         socklen_t addrlen, int timeout_sec) {
    int flags, result;
    fd_set write_fds;
    struct timeval tv;
    int error;
    socklen_t len = sizeof(error);
    
    // Set non-blocking
    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    // Attempt connection
    result = connect(sockfd, addr, addrlen);
    
    if (result < 0) {
        if (errno == EINPROGRESS) {
            // Connection in progress
            FD_ZERO(&write_fds);
            FD_SET(sockfd, &write_fds);
            tv.tv_sec = timeout_sec;
            tv.tv_usec = 0;
            
            result = select(sockfd + 1, NULL, &write_fds, NULL, &tv);
            
            if (result < 0 && errno != EINTR) {
                perror("select error");
                return -1;
            } else if (result == 0) {
                errno = ETIMEDOUT;
                return -1;
            } else {
                // Check for connection error
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                    return -1;
                }
                if (error != 0) {
                    errno = error;
                    return -1;
                }
            }
        } else if (errno == EINTR) {
            // Retry on EINTR (simplified - may need better handling)
            return connect_with_timeout(sockfd, addr, addrlen, timeout_sec);
        } else {
            return -1;
        }
    }
    
    // Restore blocking mode
    fcntl(sockfd, F_SETFL, flags);
    return 0;
}

// Example: Client with comprehensive error handling
int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[1024];
    const char *message = "Hello, Server!";
    ssize_t bytes;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // Connect with timeout
    if (connect_with_timeout(sockfd, (struct sockaddr *)&server_addr, 
                             sizeof(server_addr), 5) < 0) {
        fprintf(stderr, "Connection failed: %s\n", strerror(errno));
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to server\n");
    
    // Send data with retry
    if (send_with_retry(sockfd, message, strlen(message), 0) < 0) {
        fprintf(stderr, "Send failed\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // Receive response with retry
    bytes = recv_with_retry(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes < 0) {
        fprintf(stderr, "Receive failed\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    } else if (bytes == 0) {
        printf("Connection closed by server\n");
    } else {
        buffer[bytes] = '\0';
        printf("Received: %s\n", buffer);
    }
    
    close(sockfd);
    return 0;
}
```

### C++ RAII Approach

```cpp
#include <iostream>
#include <system_error>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

class SocketError : public std::runtime_error {
public:
    explicit SocketError(const std::string& msg, int err)
        : std::runtime_error(msg + ": " + std::strerror(err)), 
          error_code(err) {}
    
    int error_code;
};

class Socket {
private:
    int fd_;
    
public:
    Socket() : fd_(-1) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            throw SocketError("Failed to create socket", errno);
        }
    }
    
    ~Socket() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    
    // Delete copy constructor and assignment
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    // Move constructor
    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    
    int get_fd() const { return fd_; }
    
    void connect_with_retry(const std::string& ip, int port, int max_retries = 3) {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            throw SocketError("Invalid IP address", errno);
        }
        
        int attempts = 0;
        while (attempts < max_retries) {
            if (::connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                return; // Success
            }
            
            if (errno == EINTR) {
                continue; // Retry immediately on interrupt
            } else if (errno == ECONNREFUSED || errno == ETIMEDOUT) {
                attempts++;
                if (attempts < max_retries) {
                    std::cerr << "Connection attempt " << attempts 
                              << " failed, retrying...\n";
                    sleep(1);
                }
            } else {
                throw SocketError("Connection failed", errno);
            }
        }
        
        throw SocketError("Max connection retries exceeded", errno);
    }
    
    size_t send_all(const void* data, size_t len) {
        const char* ptr = static_cast<const char*>(data);
        size_t remaining = len;
        
        while (remaining > 0) {
            ssize_t sent = ::send(fd_, ptr, remaining, MSG_NOSIGNAL);
            
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    usleep(1000);
                    continue;
                } else {
                    throw SocketError("Send failed", errno);
                }
            }
            
            ptr += sent;
            remaining -= sent;
        }
        
        return len;
    }
    
    ssize_t recv_with_retry(void* buffer, size_t len) {
        while (true) {
            ssize_t received = ::recv(fd_, buffer, len, 0);
            
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return 0; // No data available
                } else {
                    throw SocketError("Receive failed", errno);
                }
            }
            
            return received;
        }
    }
};
```

## Rust Implementation

Rust's type system and `Result` type provide excellent error handling mechanisms for network programming.

```rust
use std::io::{self, Read, Write, ErrorKind};
use std::net::{TcpStream, ToSocketAddrs};
use std::time::Duration;
use std::thread;

/// Custom error type for network operations
#[derive(Debug)]
pub enum NetworkError {
    IoError(io::Error),
    ConnectionFailed(String),
    Timeout,
    MaxRetriesExceeded,
}

impl From<io::Error> for NetworkError {
    fn from(err: io::Error) -> Self {
        NetworkError::IoError(err)
    }
}

/// Send all data with retry logic for EINTR and EAGAIN
pub fn send_with_retry(stream: &mut TcpStream, data: &[u8]) -> Result<(), NetworkError> {
    let mut sent = 0;
    
    while sent < data.len() {
        match stream.write(&data[sent..]) {
            Ok(n) => {
                sent += n;
            }
            Err(e) if e.kind() == ErrorKind::Interrupted => {
                // EINTR: retry immediately
                continue;
            }
            Err(e) if e.kind() == ErrorKind::WouldBlock => {
                // EAGAIN/EWOULDBLOCK: wait briefly and retry
                thread::sleep(Duration::from_millis(1));
                continue;
            }
            Err(e) if e.kind() == ErrorKind::BrokenPipe => {
                return Err(NetworkError::ConnectionFailed("Broken pipe".to_string()));
            }
            Err(e) if e.kind() == ErrorKind::ConnectionReset => {
                return Err(NetworkError::ConnectionFailed("Connection reset".to_string()));
            }
            Err(e) => {
                return Err(NetworkError::IoError(e));
            }
        }
    }
    
    Ok(())
}

/// Receive data with retry logic for EINTR
pub fn recv_with_retry(stream: &mut TcpStream, buffer: &mut [u8]) -> Result<usize, NetworkError> {
    loop {
        match stream.read(buffer) {
            Ok(n) => return Ok(n),
            Err(e) if e.kind() == ErrorKind::Interrupted => {
                // EINTR: retry immediately
                continue;
            }
            Err(e) if e.kind() == ErrorKind::WouldBlock => {
                // EAGAIN/EWOULDBLOCK on non-blocking socket
                return Ok(0);
            }
            Err(e) if e.kind() == ErrorKind::ConnectionReset => {
                return Err(NetworkError::ConnectionFailed("Connection reset".to_string()));
            }
            Err(e) => {
                return Err(NetworkError::IoError(e));
            }
        }
    }
}

/// Connect with timeout and retry logic
pub fn connect_with_retry<A: ToSocketAddrs>(
    addr: A,
    timeout: Duration,
    max_retries: u32,
) -> Result<TcpStream, NetworkError> {
    let mut attempts = 0;
    
    loop {
        match TcpStream::connect_timeout(
            &addr.to_socket_addrs()?.next().ok_or_else(|| {
                NetworkError::ConnectionFailed("Invalid address".to_string())
            })?,
            timeout,
        ) {
            Ok(stream) => {
                stream.set_read_timeout(Some(Duration::from_secs(30)))?;
                stream.set_write_timeout(Some(Duration::from_secs(30)))?;
                return Ok(stream);
            }
            Err(e) if e.kind() == ErrorKind::Interrupted => {
                // EINTR: retry immediately without counting
                continue;
            }
            Err(e) if e.kind() == ErrorKind::ConnectionRefused 
                    || e.kind() == ErrorKind::TimedOut => {
                attempts += 1;
                if attempts >= max_retries {
                    return Err(NetworkError::MaxRetriesExceeded);
                }
                eprintln!("Connection attempt {} failed, retrying...", attempts);
                thread::sleep(Duration::from_secs(1));
            }
            Err(e) => {
                return Err(NetworkError::IoError(e));
            }
        }
    }
}

/// Resilient TCP client example
pub struct ResilientClient {
    stream: Option<TcpStream>,
    addr: String,
}

impl ResilientClient {
    pub fn new(addr: String) -> Self {
        ResilientClient {
            stream: None,
            addr,
        }
    }
    
    /// Ensure connection is established
    fn ensure_connected(&mut self) -> Result<(), NetworkError> {
        if self.stream.is_none() {
            self.stream = Some(connect_with_retry(
                &self.addr,
                Duration::from_secs(5),
                3,
            )?);
        }
        Ok(())
    }
    
    /// Send data with automatic reconnection on failure
    pub fn send(&mut self, data: &[u8]) -> Result<(), NetworkError> {
        self.ensure_connected()?;
        
        if let Some(ref mut stream) = self.stream {
            match send_with_retry(stream, data) {
                Ok(_) => Ok(()),
                Err(NetworkError::ConnectionFailed(_)) => {
                    // Try to reconnect once
                    self.stream = None;
                    self.ensure_connected()?;
                    if let Some(ref mut stream) = self.stream {
                        send_with_retry(stream, data)
                    } else {
                        Err(NetworkError::ConnectionFailed("Reconnection failed".to_string()))
                    }
                }
                Err(e) => Err(e),
            }
        } else {
            Err(NetworkError::ConnectionFailed("No connection".to_string()))
        }
    }
    
    /// Receive data
    pub fn recv(&mut self, buffer: &mut [u8]) -> Result<usize, NetworkError> {
        self.ensure_connected()?;
        
        if let Some(ref mut stream) = self.stream {
            recv_with_retry(stream, buffer)
        } else {
            Err(NetworkError::ConnectionFailed("No connection".to_string()))
        }
    }
}

// Example usage
fn main() -> Result<(), NetworkError> {
    let mut client = ResilientClient::new("127.0.0.1:8080".to_string());
    
    let message = b"Hello, Server!";
    client.send(message)?;
    println!("Sent: {:?}", std::str::from_utf8(message).unwrap());
    
    let mut buffer = vec![0u8; 1024];
    let n = client.recv(&mut buffer)?;
    
    if n > 0 {
        println!("Received: {:?}", std::str::from_utf8(&buffer[..n]).unwrap());
    } else {
        println!("Connection closed by server");
    }
    
    Ok(())
}
```

### Rust with Tokio (Async)

```rust
use tokio::net::TcpStream;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::time::{timeout, Duration, sleep};
use std::io;

/// Async send with retry logic
pub async fn async_send_with_retry(
    stream: &mut TcpStream,
    data: &[u8],
) -> io::Result<()> {
    let mut sent = 0;
    
    while sent < data.len() {
        match stream.write(&data[sent..]).await {
            Ok(n) => sent += n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                sleep(Duration::from_millis(1)).await;
                continue;
            }
            Err(e) => return Err(e),
        }
    }
    
    Ok(())
}

/// Async connect with retry
pub async fn async_connect_with_retry(
    addr: &str,
    max_retries: u32,
) -> io::Result<TcpStream> {
    let mut attempts = 0;
    
    loop {
        match timeout(Duration::from_secs(5), TcpStream::connect(addr)).await {
            Ok(Ok(stream)) => return Ok(stream),
            Ok(Err(e)) if e.kind() == io::ErrorKind::ConnectionRefused => {
                attempts += 1;
                if attempts >= max_retries {
                    return Err(io::Error::new(
                        io::ErrorKind::ConnectionRefused,
                        "Max retries exceeded"
                    ));
                }
                eprintln!("Retry attempt {}", attempts);
                sleep(Duration::from_secs(1)).await;
            }
            Ok(Err(e)) => return Err(e),
            Err(_) => {
                attempts += 1;
                if attempts >= max_retries {
                    return Err(io::Error::new(io::ErrorKind::TimedOut, "Connection timeout"));
                }
            }
        }
    }
}
```

## Summary

**Error code handling in TCP/IP programming** requires careful attention to temporary vs permanent failures:

1. **EINTR Handling**: Always retry interrupted system calls immediately without counting as failures
2. **EAGAIN/EWOULDBLOCK**: For non-blocking sockets, wait briefly and retry or use event notification mechanisms (select/poll/epoll)
3. **Fatal Errors**: EPIPE, ECONNRESET, ECONNREFUSED typically require connection re-establishment
4. **Retry Strategies**: Implement exponential backoff for connection attempts, with maximum retry limits
5. **Timeout Management**: Set appropriate timeouts for connect, send, and recv operations
6. **Language Idioms**: 
   - C: Explicit errno checking with wrapper functions
   - C++: RAII and exceptions for resource management
   - Rust: Result types and pattern matching for comprehensive error handling

Proper error handling transforms fragile network code into production-ready, resilient systems that gracefully handle network instability and temporary failures.