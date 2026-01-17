# TCP Connection Termination

## Detailed Description

TCP connection termination is the process of gracefully closing an established TCP connection. Unlike the three-way handshake used for connection establishment, TCP uses a **four-way handshake** (also called four-way close) to ensure both sides have finished transmitting data before the connection is fully closed.

### The Four-Way Handshake

The four-way handshake consists of the following steps:

1. **FIN from Client (Active Close)**
   - The application initiating the close calls `close()` or `shutdown()`
   - TCP sends a FIN (Finish) segment to the peer
   - The connection enters FIN_WAIT_1 state

2. **ACK from Server (Passive Close)**
   - The server receives the FIN and sends an ACK
   - The server's connection moves to CLOSE_WAIT state
   - The client's connection moves to FIN_WAIT_2 state
   - At this point, the connection is half-closed: the client can no longer send data, but the server can still send data to the client

3. **FIN from Server**
   - When the server application finishes sending data and closes its socket, TCP sends a FIN
   - The server's connection enters LAST_ACK state

4. **ACK from Client**
   - The client receives the FIN and sends an ACK
   - The client enters TIME_WAIT state
   - The server receives the ACK and closes the connection completely

### TIME_WAIT State

The TIME_WAIT state is a critical aspect of TCP connection termination. After sending the final ACK, the active closer (the side that initiated the close) enters TIME_WAIT for **2 × MSL (Maximum Segment Lifetime)**, typically 1-4 minutes.

**Why TIME_WAIT exists:**

- **Reliable connection termination**: If the final ACK is lost, the passive closer will retransmit its FIN. The active closer must remain in TIME_WAIT to handle this retransmission.
- **Prevent old duplicate segments**: Ensures that old packets from the closed connection don't interfere with a new connection using the same port pair (same source IP, source port, destination IP, destination port).

**TIME_WAIT implications:**

- The socket remains bound to its port during TIME_WAIT
- On servers handling many connections, this can lead to port exhaustion
- The SO_REUSEADDR socket option can help manage this issue

### Graceful vs Abortive Shutdown

**Graceful Shutdown**: Uses the four-way handshake, ensuring all data is transmitted and acknowledged before closing. This is the normal way to close a connection.

**Abortive Shutdown**: Sends an RST (Reset) segment immediately, discarding any unsent or unacknowledged data. This happens when:
- `close()` is called with unsent data and SO_LINGER is set to 0
- An application crashes
- A protocol error occurs

### Half-Close

TCP supports half-close, where one direction of the connection is closed while the other remains open. This is achieved using `shutdown()` with SHUT_WR, which sends a FIN but still allows receiving data.

## Code Examples

### C/C++ Examples

#### Basic Connection Termination

```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

// Simple graceful close
void simple_close(int sockfd) {
    // close() will send FIN and enter the four-way handshake
    if (close(sockfd) < 0) {
        perror("close");
    }
}

// Graceful shutdown with error checking
int graceful_shutdown(int sockfd) {
    char buffer[1024];
    ssize_t n;
    
    // Shutdown write side (sends FIN)
    if (shutdown(sockfd, SHUT_WR) < 0) {
        perror("shutdown");
        return -1;
    }
    
    printf("FIN sent, draining remaining data...\n");
    
    // Continue reading until peer closes (receives FIN)
    while ((n = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        printf("Received %zd bytes after shutdown\n", n);
        // Process or discard data
    }
    
    if (n < 0) {
        perror("recv");
        return -1;
    }
    
    printf("Peer closed connection, closing socket\n");
    close(sockfd);
    return 0;
}
```

#### SO_LINGER Option

```c
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>

// Configure linger behavior
void set_linger_option(int sockfd, int onoff, int linger_time) {
    struct linger ling;
    
    ling.l_onoff = onoff;     // 0 = off, 1 = on
    ling.l_linger = linger_time; // linger time in seconds
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling)) < 0) {
        perror("setsockopt SO_LINGER");
    }
}

void demonstrate_linger_modes(int sockfd) {
    // Mode 1: Default behavior (graceful close)
    // l_onoff = 0: close() returns immediately, 
    // TCP tries to send remaining data in background
    struct linger ling1 = {0, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling1, sizeof(ling1));
    
    // Mode 2: Graceful close with timeout
    // l_onoff = 1, l_linger > 0: close() blocks until data sent or timeout
    struct linger ling2 = {1, 10}; // 10 second timeout
    setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling2, sizeof(ling2));
    
    // Mode 3: Abortive close (RST)
    // l_onoff = 1, l_linger = 0: send RST immediately, discard data
    struct linger ling3 = {1, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &ling3, sizeof(ling3));
    // close(sockfd); would send RST instead of FIN
}
```

#### SO_REUSEADDR for TIME_WAIT

```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int create_server_socket_with_reuse(int port) {
    int sockfd;
    int reuse = 1;
    struct sockaddr_in addr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    // Allow reuse of local addresses (helps with TIME_WAIT)
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
                   &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(sockfd);
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, 10) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}
```

#### Complete Client Example with Proper Shutdown

```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int client_with_graceful_close(const char* server_ip, int port) {
    int sockfd;
    struct sockaddr_in server_addr;
    char send_buffer[] = "Hello, Server!";
    char recv_buffer[1024];
    ssize_t n;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return -1;
    }
    
    // Connect
    if (connect(sockfd, (struct sockaddr*)&server_addr, 
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    
    printf("Connected to server\n");
    
    // Send data
    if (send(sockfd, send_buffer, strlen(send_buffer), 0) < 0) {
        perror("send");
        close(sockfd);
        return -1;
    }
    
    // Half-close: shutdown write side (sends FIN)
    if (shutdown(sockfd, SHUT_WR) < 0) {
        perror("shutdown");
        close(sockfd);
        return -1;
    }
    
    printf("Write side closed, waiting for server response\n");
    
    // Read response until server closes (receives server's FIN)
    while ((n = recv(sockfd, recv_buffer, sizeof(recv_buffer) - 1, 0)) > 0) {
        recv_buffer[n] = '\0';
        printf("Received: %s\n", recv_buffer);
    }
    
    if (n < 0) {
        perror("recv");
    } else {
        printf("Server closed connection\n");
    }
    
    // Complete the close (sends final ACK, enters TIME_WAIT)
    close(sockfd);
    printf("Connection fully closed, entering TIME_WAIT\n");
    
    return 0;
}
```

### Rust Examples

#### Basic Connection Termination

```rust
use std::net::TcpStream;
use std::io::{Read, Write};

// Simple graceful close - drop automatically closes
fn simple_close() -> std::io::Result<()> {
    let mut stream = TcpStream::connect("127.0.0.1:8080")?;
    stream.write_all(b"Hello")?;
    
    // When stream goes out of scope, drop() is called
    // which performs graceful shutdown
    Ok(())
    // Stream is closed here (FIN sent)
}

// Explicit shutdown
fn explicit_shutdown() -> std::io::Result<()> {
    use std::net::Shutdown;
    
    let mut stream = TcpStream::connect("127.0.0.1:8080")?;
    stream.write_all(b"Hello")?;
    
    // Shutdown write side (sends FIN)
    stream.shutdown(Shutdown::Write)?;
    println!("Write side closed");
    
    // Can still read from the stream
    let mut buffer = [0u8; 1024];
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => {
                println!("Peer closed connection");
                break;
            }
            Ok(n) => {
                println!("Received {} bytes after shutdown", n);
            }
            Err(e) => {
                eprintln!("Read error: {}", e);
                break;
            }
        }
    }
    
    // Full close happens when stream is dropped
    Ok(())
}
```

#### Half-Close Pattern

```rust
use std::net::TcpStream;
use std::io::{Read, Write, Result};
use std::net::Shutdown;

fn half_close_example(mut stream: TcpStream) -> Result<()> {
    // Send request data
    stream.write_all(b"GET /data HTTP/1.1\r\n\r\n")?;
    
    // Close write side to signal we're done sending (FIN)
    stream.shutdown(Shutdown::Write)?;
    println!("Sent FIN, no more data will be sent");
    
    // Read response until peer closes (receives FIN)
    let mut response = Vec::new();
    let mut buffer = [0u8; 4096];
    
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => {
                // Peer closed connection (received FIN)
                println!("Received FIN from peer");
                break;
            }
            Ok(n) => {
                response.extend_from_slice(&buffer[..n]);
                println!("Received {} bytes", n);
            }
            Err(e) => {
                eprintln!("Read error: {}", e);
                return Err(e);
            }
        }
    }
    
    println!("Total received: {} bytes", response.len());
    
    // Stream is fully closed when dropped (final ACK, TIME_WAIT)
    Ok(())
}
```

#### SO_LINGER Configuration

```rust
use std::net::TcpStream;
use std::time::Duration;
use std::io::Result;

#[cfg(unix)]
fn set_linger_unix(stream: &TcpStream, linger: Option<Duration>) -> Result<()> {
    use std::os::unix::io::AsRawFd;
    use libc::{setsockopt, SOL_SOCKET, SO_LINGER, linger as LingerStruct};
    use std::mem;
    
    let fd = stream.as_raw_fd();
    
    let linger_struct = match linger {
        Some(dur) => LingerStruct {
            l_onoff: 1,
            l_linger: dur.as_secs() as i32,
        },
        None => LingerStruct {
            l_onoff: 0,
            l_linger: 0,
        },
    };
    
    let ret = unsafe {
        setsockopt(
            fd,
            SOL_SOCKET,
            SO_LINGER,
            &linger_struct as *const _ as *const _,
            mem::size_of::<LingerStruct>() as u32,
        )
    };
    
    if ret != 0 {
        Err(std::io::Error::last_os_error())
    } else {
        Ok(())
    }
}

fn linger_examples() -> Result<()> {
    let stream = TcpStream::connect("127.0.0.1:8080")?;
    
    // Example 1: Default (graceful close, non-blocking)
    // close() returns immediately, TCP sends data in background
    
    // Example 2: Graceful with timeout
    #[cfg(unix)]
    set_linger_unix(&stream, Some(Duration::from_secs(10)))?;
    // close() blocks up to 10 seconds waiting for data to be sent
    
    // Example 3: Abortive close (RST)
    #[cfg(unix)]
    set_linger_unix(&stream, Some(Duration::from_secs(0)))?;
    // close() sends RST immediately, discarding unsent data
    
    Ok(())
}
```

#### SO_REUSEADDR for Server

```rust
use std::net::{TcpListener, SocketAddr};
use std::io::Result;

#[cfg(unix)]
fn create_reusable_listener(addr: &str) -> Result<TcpListener> {
    use std::os::unix::io::AsRawFd;
    use libc::{setsockopt, SOL_SOCKET, SO_REUSEADDR};
    use std::mem;
    
    let listener = TcpListener::bind(addr)?;
    let fd = listener.as_raw_fd();
    
    let optval: libc::c_int = 1;
    let ret = unsafe {
        setsockopt(
            fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &optval as *const _ as *const _,
            mem::size_of::<libc::c_int>() as u32,
        )
    };
    
    if ret != 0 {
        return Err(std::io::Error::last_os_error());
    }
    
    Ok(listener)
}

// Using socket2 crate (recommended approach)
use socket2::{Socket, Domain, Type, Protocol};

fn create_reusable_listener_socket2(addr: &str) -> Result<TcpListener> {
    let addr: SocketAddr = addr.parse().unwrap();
    
    let socket = Socket::new(
        Domain::IPV4,
        Type::STREAM,
        Some(Protocol::TCP),
    )?;
    
    // Set SO_REUSEADDR to allow binding to TIME_WAIT sockets
    socket.set_reuse_address(true)?;
    
    // Optional: SO_REUSEPORT (Linux-specific)
    #[cfg(target_os = "linux")]
    socket.set_reuse_port(true)?;
    
    socket.bind(&addr.into())?;
    socket.listen(128)?;
    
    Ok(socket.into())
}
```

#### Complete Server with Proper Connection Management

```rust
use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write, Result};
use std::thread;
use std::net::Shutdown;

fn handle_client(mut stream: TcpStream) -> Result<()> {
    println!("New client connected: {:?}", stream.peer_addr()?);
    
    let mut buffer = [0u8; 1024];
    
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => {
                // Client sent FIN
                println!("Client closed connection (received FIN)");
                break;
            }
            Ok(n) => {
                println!("Received {} bytes", n);
                
                // Echo back
                stream.write_all(&buffer[..n])?;
            }
            Err(e) => {
                eprintln!("Read error: {}", e);
                break;
            }
        }
    }
    
    // Send final response and close write side
    stream.write_all(b"Goodbye\n")?;
    stream.shutdown(Shutdown::Write)?;
    println!("Sent FIN to client");
    
    // Optional: drain any remaining data
    let mut discard = [0u8; 256];
    while let Ok(n) = stream.read(&mut discard) {
        if n == 0 {
            break;
        }
        println!("Discarded {} bytes after shutdown", n);
    }
    
    println!("Connection fully closed");
    // Stream dropped here - final ACK sent, enters TIME_WAIT
    
    Ok(())
}

fn run_server(addr: &str) -> Result<()> {
    let listener = TcpListener::bind(addr)?;
    println!("Server listening on {}", addr);
    
    for stream_result in listener.incoming() {
        match stream_result {
            Ok(stream) => {
                thread::spawn(|| {
                    if let Err(e) = handle_client(stream) {
                        eprintln!("Error handling client: {}", e);
                    }
                });
            }
            Err(e) => {
                eprintln!("Connection failed: {}", e);
            }
        }
    }
    
    Ok(())
}
```

#### Async Rust Example (Tokio)

```rust
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};

async fn handle_client_async(mut stream: TcpStream) -> std::io::Result<()> {
    println!("Client connected: {:?}", stream.peer_addr()?);
    
    let mut buffer = vec![0u8; 1024];
    
    loop {
        let n = stream.read(&mut buffer).await?;
        
        if n == 0 {
            // Client closed (received FIN)
            println!("Client closed connection");
            break;
        }
        
        // Echo back
        stream.write_all(&buffer[..n]).await?;
    }
    
    // Graceful shutdown
    stream.shutdown().await?;
    println!("Connection closed gracefully");
    
    Ok(())
}

#[tokio::main]
async fn main() -> std::io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;
    println!("Async server listening on 127.0.0.1:8080");
    
    loop {
        let (stream, addr) = listener.accept().await?;
        println!("Accepted connection from {}", addr);
        
        tokio::spawn(async move {
            if let Err(e) = handle_client_async(stream).await {
                eprintln!("Error: {}", e);
            }
        });
    }
}
```

## Summary

**TCP Connection Termination** ensures reliable and orderly connection closure through a four-way handshake process. The active closer initiates termination by sending a FIN segment, which is acknowledged by the passive closer. The passive closer then sends its own FIN when ready, which is acknowledged by the active closer. This bidirectional closing process allows both sides to finish transmitting data before the connection is fully terminated.

The **TIME_WAIT state** is essential for reliable termination, lasting 2×MSL (typically 1-4 minutes) to handle lost ACKs and prevent old segments from interfering with new connections. While TIME_WAIT can cause port exhaustion on busy servers, options like SO_REUSEADDR help manage this issue.

**Key takeaways**: Use `shutdown()` for half-close scenarios where one-way communication continues; configure SO_LINGER to control close behavior (graceful vs abortive); enable SO_REUSEADDR on servers to handle TIME_WAIT efficiently; and always drain remaining data after shutdown to ensure clean termination. Proper connection termination prevents resource leaks, ensures data integrity, and maintains protocol reliability in TCP applications.