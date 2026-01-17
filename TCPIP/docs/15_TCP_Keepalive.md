# TCP Keepalive: Detecting Dead Connections

## Overview

TCP Keepalive is a mechanism that allows TCP connections to detect when the remote peer has become unreachable due to network failures, system crashes, or other issues. Without keepalive, a TCP connection can remain in an established state indefinitely even if the remote host is no longer available, wasting system resources and potentially causing application hangs.

Keepalive works by periodically sending probe packets to the remote peer when the connection has been idle for a specified time. If the peer responds, the connection is still alive. If no response is received after a configured number of retries, the connection is considered dead and can be closed.

## How TCP Keepalive Works

The keepalive mechanism involves three key parameters:

1. **Keepalive Time**: The duration of idle time before the first keepalive probe is sent
2. **Keepalive Interval**: The time between subsequent keepalive probes if no response is received
3. **Keepalive Probes**: The number of unacknowledged probes before declaring the connection dead

The sequence of events:
1. Connection remains idle for the keepalive time period
2. TCP sends the first keepalive probe
3. If no ACK is received, TCP waits for the keepalive interval
4. TCP sends another probe (repeats up to the probe count)
5. If all probes fail, the connection is terminated with an error

## Use Cases

TCP Keepalive is particularly useful for:

- Long-lived connections that may experience extended idle periods (e.g., database connections, persistent HTTP connections)
- Detecting network failures or crashed peers that didn't send a FIN packet
- Preventing firewall or NAT timeout on idle connections
- Freeing resources held by dead connections
- Server applications that need to detect disconnected clients

## C/C++ Implementation

### Basic Socket Setup with Keepalive

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

int enable_keepalive(int sockfd) {
    int optval = 1;
    
    // Enable TCP keepalive
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
        perror("setsockopt SO_KEEPALIVE");
        return -1;
    }
    
    printf("TCP Keepalive enabled\n");
    return 0;
}

int configure_keepalive_params(int sockfd, int idle_time, int interval, int probe_count) {
    // Set the time (in seconds) the connection needs to remain idle before TCP starts
    // sending keepalive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle_time, sizeof(idle_time)) < 0) {
        perror("setsockopt TCP_KEEPIDLE");
        return -1;
    }
    
    // Set the time (in seconds) between individual keepalive probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) < 0) {
        perror("setsockopt TCP_KEEPINTVL");
        return -1;
    }
    
    // Set the maximum number of keepalive probes TCP should send before dropping the connection
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &probe_count, sizeof(probe_count)) < 0) {
        perror("setsockopt TCP_KEEPCNT");
        return -1;
    }
    
    printf("Keepalive configured: idle=%ds, interval=%ds, probes=%d\n", 
           idle_time, interval, probe_count);
    return 0;
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    
    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    // Enable keepalive
    if (enable_keepalive(sockfd) < 0) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // Configure keepalive parameters:
    // - Start probing after 60 seconds of idle time
    // - Send probes every 10 seconds
    // - Close connection after 5 failed probes
    if (configure_keepalive_params(sockfd, 60, 10, 5) < 0) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    
    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected successfully\n");
    
    // Simulate keeping connection alive
    printf("Connection idle, keepalive will monitor it...\n");
    sleep(120); // Sleep for 2 minutes
    
    close(sockfd);
    return 0;
}
```

### Server Example with Keepalive

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define PORT 8080
#define BACKLOG 5

int setup_server_socket_with_keepalive() {
    int server_fd, opt = 1;
    struct sockaddr_in address;
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt SO_REUSEADDR");
        exit(EXIT_FAILURE);
    }
    
    // Enable keepalive
    if (setsockopt(server_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt))) {
        perror("setsockopt SO_KEEPALIVE");
        exit(EXIT_FAILURE);
    }
    
    // Configure keepalive: 30s idle, 5s interval, 3 probes
    int idle = 30, interval = 5, probes = 3;
    setsockopt(server_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(server_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(server_fd, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof(probes));
    
    // Bind socket
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d with keepalive enabled\n", PORT);
    return server_fd;
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[1024];
    
    server_fd = setup_server_socket_with_keepalive();
    
    while (1) {
        printf("Waiting for connections...\n");
        
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Client connected: %s:%d\n", client_ip, ntohs(client_addr.sin_port));
        
        // Read data (or timeout/error if connection dies)
        ssize_t bytes_read;
        while ((bytes_read = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            printf("Received: %s", buffer);
        }
        
        if (bytes_read == 0) {
            printf("Client disconnected gracefully\n");
        } else {
            printf("Connection error (possibly detected by keepalive)\n");
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    return 0;
}
```

### Checking Current Keepalive Settings

```c
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

void print_keepalive_settings(int sockfd) {
    int keepalive, idle, interval, count;
    socklen_t optlen = sizeof(int);
    
    // Get SO_KEEPALIVE status
    if (getsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, &optlen) == 0) {
        printf("SO_KEEPALIVE: %s\n", keepalive ? "enabled" : "disabled");
    }
    
    // Get TCP_KEEPIDLE
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, &optlen) == 0) {
        printf("TCP_KEEPIDLE: %d seconds\n", idle);
    }
    
    // Get TCP_KEEPINTVL
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, &optlen) == 0) {
        printf("TCP_KEEPINTVL: %d seconds\n", interval);
    }
    
    // Get TCP_KEEPCNT
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &count, &optlen) == 0) {
        printf("TCP_KEEPCNT: %d probes\n", count);
    }
    
    printf("Total time before timeout: %d seconds\n", idle + (interval * count));
}
```

## Rust Implementation

### Basic Keepalive Configuration

```rust
use std::io::{self, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::time::Duration;

// For socket2 crate features
use socket2::{Socket, Domain, Type, Protocol, TcpKeepalive};

fn configure_keepalive(stream: &TcpStream) -> io::Result<()> {
    let socket = Socket::from(stream.try_clone()?);
    
    // Create keepalive configuration
    let keepalive = TcpKeepalive::new()
        .with_time(Duration::from_secs(60))      // idle time before first probe
        .with_interval(Duration::from_secs(10)); // interval between probes
    
    // Note: probe count is platform-specific and may need additional configuration
    // On Linux, you can set it via raw socket options
    
    socket.set_tcp_keepalive(&keepalive)?;
    
    println!("Keepalive configured: idle=60s, interval=10s");
    Ok(())
}

fn main() -> io::Result<()> {
    let stream = TcpStream::connect("127.0.0.1:8080")?;
    
    configure_keepalive(&stream)?;
    
    println!("Connected with keepalive enabled");
    
    // Keep connection open
    std::thread::sleep(Duration::from_secs(120));
    
    Ok(())
}
```

### Complete Server with Keepalive

```rust
use std::io::{self, Read, Write, ErrorKind};
use std::net::{TcpListener, TcpStream, SocketAddr};
use std::time::Duration;
use socket2::{Socket, Domain, Type, Protocol, TcpKeepalive};

fn enable_keepalive_on_socket(stream: &TcpStream) -> io::Result<()> {
    let socket = Socket::from(stream.try_clone()?);
    
    // Configure TCP keepalive
    let keepalive = TcpKeepalive::new()
        .with_time(Duration::from_secs(30))      // Start probing after 30s idle
        .with_interval(Duration::from_secs(5));  // Probe every 5s
    
    socket.set_tcp_keepalive(&keepalive)?;
    
    // Set TCP_KEEPCNT on Linux (requires raw socket options)
    #[cfg(target_os = "linux")]
    {
        use std::os::unix::io::AsRawFd;
        let fd = stream.as_raw_fd();
        let probe_count: libc::c_int = 3;
        
        unsafe {
            if libc::setsockopt(
                fd,
                libc::IPPROTO_TCP,
                libc::TCP_KEEPCNT,
                &probe_count as *const _ as *const libc::c_void,
                std::mem::size_of::<libc::c_int>() as libc::socklen_t,
            ) != 0 {
                return Err(io::Error::last_os_error());
            }
        }
    }
    
    println!("Keepalive enabled: 30s idle, 5s interval, 3 probes");
    Ok(())
}

fn handle_client(mut stream: TcpStream, addr: SocketAddr) -> io::Result<()> {
    println!("Client connected: {}", addr);
    
    // Enable keepalive for this connection
    enable_keepalive_on_socket(&stream)?;
    
    let mut buffer = [0u8; 1024];
    
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => {
                println!("Client {} disconnected gracefully", addr);
                break;
            }
            Ok(n) => {
                let data = &buffer[..n];
                print!("Received from {}: {}", addr, String::from_utf8_lossy(data));
                
                // Echo back
                stream.write_all(data)?;
            }
            Err(e) if e.kind() == ErrorKind::ConnectionReset => {
                println!("Connection reset by peer (possibly keepalive timeout): {}", addr);
                break;
            }
            Err(e) => {
                eprintln!("Error reading from {}: {}", addr, e);
                break;
            }
        }
    }
    
    Ok(())
}

fn main() -> io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080")?;
    println!("Server listening on 127.0.0.1:8080");
    
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                let addr = stream.peer_addr()?;
                
                // Spawn thread to handle client
                std::thread::spawn(move || {
                    if let Err(e) = handle_client(stream, addr) {
                        eprintln!("Error handling client {}: {}", addr, e);
                    }
                });
            }
            Err(e) => {
                eprintln!("Error accepting connection: {}", e);
            }
        }
    }
    
    Ok(())
}
```

### Advanced: Custom Keepalive Configuration

```rust
use std::io;
use std::net::TcpStream;
use std::time::Duration;
use socket2::{Socket, TcpKeepalive};

#[derive(Debug, Clone)]
pub struct KeepaliveConfig {
    pub idle_time: Duration,
    pub interval: Duration,
    pub probe_count: u32,
}

impl Default for KeepaliveConfig {
    fn default() -> Self {
        Self {
            idle_time: Duration::from_secs(60),
            interval: Duration::from_secs(10),
            probe_count: 5,
        }
    }
}

pub fn apply_keepalive(stream: &TcpStream, config: &KeepaliveConfig) -> io::Result<()> {
    let socket = Socket::from(stream.try_clone()?);
    
    let keepalive = TcpKeepalive::new()
        .with_time(config.idle_time)
        .with_interval(config.interval);
    
    socket.set_tcp_keepalive(&keepalive)?;
    
    // Platform-specific probe count configuration
    #[cfg(target_os = "linux")]
    {
        set_tcp_keepcnt_linux(stream, config.probe_count)?;
    }
    
    #[cfg(target_os = "macos")]
    {
        set_tcp_keepcnt_macos(stream, config.probe_count)?;
    }
    
    println!(
        "Keepalive configured: idle={:?}, interval={:?}, probes={}",
        config.idle_time, config.interval, config.probe_count
    );
    
    let total_timeout = config.idle_time + (config.interval * config.probe_count);
    println!("Total timeout before connection closed: {:?}", total_timeout);
    
    Ok(())
}

#[cfg(target_os = "linux")]
fn set_tcp_keepcnt_linux(stream: &TcpStream, count: u32) -> io::Result<()> {
    use std::os::unix::io::AsRawFd;
    
    let fd = stream.as_raw_fd();
    let probe_count = count as libc::c_int;
    
    unsafe {
        if libc::setsockopt(
            fd,
            libc::IPPROTO_TCP,
            libc::TCP_KEEPCNT,
            &probe_count as *const _ as *const libc::c_void,
            std::mem::size_of::<libc::c_int>() as libc::socklen_t,
        ) != 0 {
            return Err(io::Error::last_os_error());
        }
    }
    
    Ok(())
}

#[cfg(target_os = "macos")]
fn set_tcp_keepcnt_macos(stream: &TcpStream, count: u32) -> io::Result<()> {
    use std::os::unix::io::AsRawFd;
    
    let fd = stream.as_raw_fd();
    let probe_count = count as libc::c_int;
    
    unsafe {
        if libc::setsockopt(
            fd,
            libc::IPPROTO_TCP,
            libc::TCP_KEEPCNT,
            &probe_count as *const _ as *const libc::c_void,
            std::mem::size_of::<libc::c_int>() as libc::socklen_t,
        ) != 0 {
            return Err(io::Error::last_os_error());
        }
    }
    
    Ok(())
}

// Example usage
fn main() -> io::Result<()> {
    let stream = TcpStream::connect("127.0.0.1:8080")?;
    
    let config = KeepaliveConfig {
        idle_time: Duration::from_secs(30),
        interval: Duration::from_secs(5),
        probe_count: 3,
    };
    
    apply_keepalive(&stream, &config)?;
    
    println!("Connection established with custom keepalive");
    
    // Simulate long-lived connection
    std::thread::sleep(Duration::from_secs(120));
    
    Ok(())
}
```

### Async Tokio Example

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use socket2::{Socket, TcpKeepalive};
use std::io;
use std::time::Duration;

async fn configure_keepalive_tokio(stream: &TcpStream) -> io::Result<()> {
    let socket = Socket::from(stream.as_ref().try_clone()?);
    
    let keepalive = TcpKeepalive::new()
        .with_time(Duration::from_secs(60))
        .with_interval(Duration::from_secs(10));
    
    socket.set_tcp_keepalive(&keepalive)?;
    
    Ok(())
}

async fn handle_connection(mut stream: TcpStream) -> io::Result<()> {
    let addr = stream.peer_addr()?;
    println!("New connection from: {}", addr);
    
    configure_keepalive_tokio(&stream).await?;
    
    let mut buffer = vec![0u8; 1024];
    
    loop {
        let n = stream.read(&mut buffer).await?;
        
        if n == 0 {
            println!("Connection closed: {}", addr);
            break;
        }
        
        stream.write_all(&buffer[..n]).await?;
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("Async server listening on 127.0.0.1:8080");
    
    loop {
        let (stream, _) = listener.accept().await?;
        
        tokio::spawn(async move {
            if let Err(e) = handle_connection(stream).await {
                eprintln!("Connection error: {}", e);
            }
        });
    }
}
```

## Platform Differences

TCP Keepalive behavior varies across operating systems:

**Linux:**
- Default idle time: 7200 seconds (2 hours)
- Default interval: 75 seconds
- Default probe count: 9
- Total default timeout: ~2 hours 11 minutes

**Windows:**
- Default idle time: 7200 seconds (2 hours)
- Default interval: 1000 milliseconds (1 second)
- Default probe count: 10
- Uses different API: `WSAIoctl` with `SIO_KEEPALIVE_VALS`

**macOS:**
- Default idle time: 7200 seconds
- Default interval: 75 seconds
- Default probe count: 8

## Best Practices

1. **Always enable keepalive for long-lived connections** - Database connections, persistent HTTP connections, and service-to-service communication benefit greatly

2. **Choose appropriate timeouts** - Balance between detecting failures quickly and avoiding false positives due to temporary network congestion

3. **Consider your application requirements** - Real-time applications need faster detection (30-60s idle), while batch processes can use longer intervals

4. **Account for NAT/firewall timeouts** - Many NATs drop idle connections after 60-300 seconds; keepalive should probe before this timeout

5. **Monitor keepalive failures** - Log when connections are closed due to keepalive timeouts for debugging

6. **Don't rely solely on keepalive for application-level health checks** - Keepalive only detects network failures, not application-level issues

7. **Be aware of system defaults** - Most systems have very long default keepalive times (2 hours), which is often too long for modern applications

## Summary

TCP Keepalive is a critical mechanism for maintaining reliable long-lived TCP connections. It works by sending periodic probe packets during idle periods to detect dead connections that haven't been properly closed. The mechanism is controlled by three parameters: idle time before probing begins, interval between probes, and number of probes before declaring the connection dead.

Both C/C++ and Rust provide straightforward APIs for enabling and configuring keepalive through socket options. The key socket options are `SO_KEEPALIVE` to enable the feature, and platform-specific options like `TCP_KEEPIDLE`, `TCP_KEEPINTVL`, and `TCP_KEEPCNT` to fine-tune the behavior. While default keepalive values are typically very conservative (2+ hours), most applications benefit from more aggressive settings (30-120 seconds) to quickly detect and recover from connection failures.

Proper keepalive configuration is essential for preventing resource leaks, detecting network failures promptly, and ensuring robust distributed systems. However, it should complement, not replace, application-level health checking and timeout mechanisms.