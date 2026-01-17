# Timeout Management in Network Programming

## Overview

Timeout management is critical for building robust network applications that don't hang indefinitely waiting for operations to complete. Without proper timeouts, applications can become unresponsive when dealing with slow networks, crashed peers, or network partitions.

There are two primary approaches to timeout management:

1. **Socket-level timeouts** using `SO_RCVTIMEO` and `SO_SNDTIMEO` socket options
2. **Application-level timeouts** using non-blocking I/O with select/poll/epoll or higher-level abstractions

## Socket-Level Timeouts

### SO_RCVTIMEO
Sets the timeout for blocking receive operations. If data isn't received within the specified time, the call returns with an error (`EAGAIN` or `EWOULDBLOCK`).

### SO_SNDTIMEO
Sets the timeout for blocking send operations. If the send buffer doesn't have enough space within the specified time, the call returns with an error.

Both timeouts are specified using a `struct timeval` (or `DWORD` milliseconds on Windows).

## C/C++ Examples

### Setting Socket Timeouts

```c
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int set_socket_timeouts(int sockfd, int recv_timeout_sec, int send_timeout_sec) {
    struct timeval recv_tv;
    recv_tv.tv_sec = recv_timeout_sec;
    recv_tv.tv_usec = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &recv_tv, sizeof(recv_tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        return -1;
    }
    
    struct timeval send_tv;
    send_tv.tv_sec = send_timeout_sec;
    send_tv.tv_usec = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv)) < 0) {
        perror("setsockopt SO_SNDTIMEO");
        return -1;
    }
    
    return 0;
}

// Example usage with error handling
void example_blocking_recv_with_timeout(int sockfd) {
    char buffer[1024];
    
    // Set 5 second receive timeout
    set_socket_timeouts(sockfd, 5, 5);
    
    ssize_t n = recv(sockfd, buffer, sizeof(buffer), 0);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("Receive timeout occurred\n");
        } else {
            perror("recv");
        }
    } else if (n == 0) {
        printf("Connection closed by peer\n");
    } else {
        printf("Received %zd bytes\n", n);
    }
}
```

### Application-Level Timeout with select()

```c
#include <sys/select.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int recv_with_timeout(int sockfd, void *buffer, size_t len, int timeout_sec) {
    fd_set read_fds;
    struct timeval tv;
    
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    
    // Wait for socket to become readable
    int ret = select(sockfd + 1, &read_fds, NULL, NULL, &tv);
    
    if (ret < 0) {
        perror("select");
        return -1;
    } else if (ret == 0) {
        // Timeout occurred
        printf("Select timeout\n");
        errno = ETIMEDOUT;
        return -1;
    }
    
    // Socket is readable, perform the recv
    return recv(sockfd, buffer, len, 0);
}
```

### C++ with RAII Timeout Guard

```cpp
#include <sys/socket.h>
#include <sys/time.h>
#include <stdexcept>
#include <chrono>

class SocketTimeoutGuard {
private:
    int sockfd_;
    struct timeval old_recv_tv_;
    struct timeval old_send_tv_;
    
public:
    SocketTimeoutGuard(int sockfd, std::chrono::seconds timeout) : sockfd_(sockfd) {
        socklen_t len = sizeof(old_recv_tv_);
        getsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &old_recv_tv_, &len);
        getsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &old_send_tv_, &len);
        
        struct timeval tv;
        tv.tv_sec = timeout.count();
        tv.tv_usec = 0;
        
        if (setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0 ||
            setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
            throw std::runtime_error("Failed to set socket timeout");
        }
    }
    
    ~SocketTimeoutGuard() {
        // Restore original timeouts
        setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &old_recv_tv_, sizeof(old_recv_tv_));
        setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &old_send_tv_, sizeof(old_send_tv_));
    }
    
    // Prevent copying
    SocketTimeoutGuard(const SocketTimeoutGuard&) = delete;
    SocketTimeoutGuard& operator=(const SocketTimeoutGuard&) = delete;
};

// Usage
void send_with_scoped_timeout(int sockfd, const char* data, size_t len) {
    SocketTimeoutGuard guard(sockfd, std::chrono::seconds(10));
    
    ssize_t sent = send(sockfd, data, len, 0);
    if (sent < 0) {
        throw std::runtime_error("Send failed or timed out");
    }
}
```

## Rust Examples

### Using std::net with read_timeout/write_timeout

```rust
use std::net::TcpStream;
use std::time::Duration;
use std::io::{Read, Write, ErrorKind};

fn set_socket_timeouts(stream: &TcpStream) -> std::io::Result<()> {
    // Set read timeout
    stream.set_read_timeout(Some(Duration::from_secs(5)))?;
    
    // Set write timeout
    stream.set_write_timeout(Some(Duration::from_secs(5)))?;
    
    Ok(())
}

fn recv_with_timeout(stream: &mut TcpStream) -> std::io::Result<Vec<u8>> {
    stream.set_read_timeout(Some(Duration::from_secs(10)))?;
    
    let mut buffer = vec![0u8; 1024];
    
    match stream.read(&mut buffer) {
        Ok(n) => {
            buffer.truncate(n);
            Ok(buffer)
        }
        Err(e) if e.kind() == ErrorKind::WouldBlock || e.kind() == ErrorKind::TimedOut => {
            println!("Read timeout occurred");
            Err(e)
        }
        Err(e) => Err(e),
    }
}

fn send_with_timeout(stream: &mut TcpStream, data: &[u8]) -> std::io::Result<()> {
    stream.set_write_timeout(Some(Duration::from_secs(5)))?;
    
    match stream.write_all(data) {
        Ok(_) => Ok(()),
        Err(e) if e.kind() == ErrorKind::WouldBlock || e.kind() == ErrorKind::TimedOut => {
            println!("Write timeout occurred");
            Err(e)
        }
        Err(e) => Err(e),
    }
}
```

### Application-Level Timeout with Tokio (Async)

```rust
use tokio::net::TcpStream;
use tokio::time::{timeout, Duration};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::io;

async fn recv_with_timeout_async(stream: &mut TcpStream) -> io::Result<Vec<u8>> {
    let mut buffer = vec![0u8; 1024];
    
    match timeout(Duration::from_secs(5), stream.read(&mut buffer)).await {
        Ok(Ok(n)) => {
            buffer.truncate(n);
            Ok(buffer)
        }
        Ok(Err(e)) => Err(e),
        Err(_) => {
            println!("Operation timed out");
            Err(io::Error::new(io::ErrorKind::TimedOut, "timeout"))
        }
    }
}

async fn send_with_timeout_async(stream: &mut TcpStream, data: &[u8]) -> io::Result<()> {
    match timeout(Duration::from_secs(5), stream.write_all(data)).await {
        Ok(result) => result,
        Err(_) => {
            println!("Write operation timed out");
            Err(io::Error::new(io::ErrorKind::TimedOut, "timeout"))
        }
    }
}

// Complete example with connection timeout
async fn connect_with_timeout(addr: &str) -> io::Result<TcpStream> {
    match timeout(Duration::from_secs(10), TcpStream::connect(addr)).await {
        Ok(result) => result,
        Err(_) => Err(io::Error::new(io::ErrorKind::TimedOut, "connection timeout")),
    }
}
```

### Custom Timeout Manager

```rust
use std::net::TcpStream;
use std::time::Duration;
use std::io;

pub struct TimeoutManager {
    read_timeout: Option<Duration>,
    write_timeout: Option<Duration>,
}

impl TimeoutManager {
    pub fn new(read_timeout: Duration, write_timeout: Duration) -> Self {
        Self {
            read_timeout: Some(read_timeout),
            write_timeout: Some(write_timeout),
        }
    }
    
    pub fn apply_to_stream(&self, stream: &TcpStream) -> io::Result<()> {
        stream.set_read_timeout(self.read_timeout)?;
        stream.set_write_timeout(self.write_timeout)?;
        Ok(())
    }
    
    pub fn with_custom_read_timeout(&self, stream: &TcpStream, timeout: Duration) -> io::Result<()> {
        stream.set_read_timeout(Some(timeout))
    }
}

// RAII-style timeout guard
pub struct TimeoutGuard<'a> {
    stream: &'a TcpStream,
    old_read: Option<Duration>,
    old_write: Option<Duration>,
}

impl<'a> TimeoutGuard<'a> {
    pub fn new(stream: &'a TcpStream, timeout: Duration) -> io::Result<Self> {
        let old_read = stream.read_timeout()?;
        let old_write = stream.write_timeout()?;
        
        stream.set_read_timeout(Some(timeout))?;
        stream.set_write_timeout(Some(timeout))?;
        
        Ok(Self {
            stream,
            old_read,
            old_write,
        })
    }
}

impl<'a> Drop for TimeoutGuard<'a> {
    fn drop(&mut self) {
        let _ = self.stream.set_read_timeout(self.old_read);
        let _ = self.stream.set_write_timeout(self.old_write);
    }
}

// Usage example
fn example_with_guard(stream: &TcpStream) -> io::Result<()> {
    use std::io::Read;
    
    let _guard = TimeoutGuard::new(stream, Duration::from_secs(5))?;
    
    let mut buffer = [0u8; 1024];
    let mut stream_ref = stream.try_clone()?;
    stream_ref.read(&mut buffer)?;
    
    // Timeouts automatically restored when _guard goes out of scope
    Ok(())
}
```

## Summary

**Timeout management** is essential for preventing network applications from hanging indefinitely. The main approaches are:

1. **Socket-level timeouts (`SO_RCVTIMEO`/`SO_SNDTIMEO`)**: Simple to implement, apply globally to all blocking operations on a socket. Best for straightforward scenarios where consistent timeouts are acceptable.

2. **Application-level timeouts**: Provide fine-grained control using mechanisms like `select()`, `poll()`, or async runtimes. Better for complex scenarios requiring per-operation timeouts or integration with event loops.

**Key considerations:**
- Socket-level timeouts affect all blocking operations; application-level timeouts offer per-operation control
- Always handle timeout errors (`EAGAIN`, `EWOULDBLOCK`, `ETIMEDOUT`) appropriately
- In C/C++, use `select()`/`poll()` or RAII guards for flexible timeout management
- In Rust, leverage `Duration` types and async/await with `tokio::time::timeout` for elegant timeout handling
- Connection establishment, reads, and writes all benefit from timeout protection
- Consider using RAII patterns to ensure timeouts are properly restored after temporary changes

Proper timeout management makes applications resilient to network issues, unresponsive peers, and denial-of-service scenarios while maintaining responsiveness for legitimate users.