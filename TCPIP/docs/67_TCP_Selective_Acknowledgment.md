# TCP Selective Acknowledgment (SACK)

## Detailed Description

### Overview

TCP Selective Acknowledgment (SACK) is an extension to TCP that allows a receiver to inform the sender about all segments that have been successfully received, even when there are gaps due to packet loss. This mechanism significantly improves TCP performance, especially in networks with high packet loss or long delay-bandwidth products.

### The Problem SACK Solves

In traditional TCP (without SACK), the receiver can only acknowledge the highest in-order byte received. When multiple packets are lost in a window, TCP must either:

1. **Go-Back-N**: Retransmit all packets from the first lost one
2. **Wait for timeouts**: Suffer from slow retransmission of each lost packet

**Example scenario without SACK:**
- Sender transmits segments: 1, 2, 3, 4, 5, 6, 7, 8
- Segments 3 and 5 are lost
- Receiver can only ACK segment 2 (cumulative ACK)
- Sender doesn't know that segments 4, 6, 7, 8 were received
- Result: Unnecessary retransmissions

### How SACK Works

SACK allows the receiver to acknowledge non-contiguous blocks of data:

1. **SACK-Permitted Option**: Negotiated during the TCP handshake (SYN packets)
2. **SACK Option**: Used in subsequent ACKs to report received segments
3. **Selective Retransmission**: Sender only retransmits the missing segments

**SACK Option Format:**
```
+--------+--------+
|  Kind  | Length |
+--------+--------+
|   5    | Variable|
+--------+--------+
|     Left Edge of 1st Block     |
+--------------------------------+
|     Right Edge of 1st Block    |
+--------------------------------+
|     Left Edge of 2nd Block     |
+--------------------------------+
|     Right Edge of 2nd Block    |
+--------------------------------+
...
```

- **Kind**: 5 (SACK option)
- **Length**: 2 + 8*N bytes (N = number of SACK blocks, max 3-4 typically)
- **Edges**: 32-bit sequence numbers

### Benefits

1. **Faster Recovery**: Reduces retransmission time by 20-40% in lossy networks
2. **Better Bandwidth Utilization**: Avoids unnecessary retransmissions
3. **Improved Performance**: Especially beneficial for high-latency or high-loss links

---

## C/C++ Programming Examples

### Example 1: Enabling SACK on a Socket (Linux)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <arpa/inet.h>

int create_tcp_socket_with_sack() {
    int sockfd;
    int optval = 1;
    
    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    // Enable SACK (on Linux, SACK is typically enabled by default)
    // Check if SACK is supported
    int sack_enabled = 1;
    socklen_t len = sizeof(sack_enabled);
    
    // On Linux, you can check SACK status via getsockopt
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_SACK_ENABLE, 
                   &sack_enabled, &len) == 0) {
        printf("SACK is %s\n", sack_enabled ? "enabled" : "disabled");
    }
    
    // Note: SACK negotiation happens during handshake automatically
    // if both sides support it
    
    return sockfd;
}

int main() {
    int sockfd = create_tcp_socket_with_sack();
    
    if (sockfd < 0) {
        return 1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, 
                sizeof(server_addr)) < 0) {
        perror("connect failed");
        close(sockfd);
        return 1;
    }
    
    printf("Connected successfully with SACK support\n");
    
    // Your data transmission code here
    
    close(sockfd);
    return 0;
}
```

### Example 2: Checking SACK Statistics (Linux)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <linux/tcp.h>
#include <unistd.h>

void print_tcp_info(int sockfd) {
    struct tcp_info info;
    socklen_t info_len = sizeof(info);
    
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, 
                   &info, &info_len) == 0) {
        printf("\n=== TCP Connection Info ===\n");
        printf("State: %u\n", info.tcpi_state);
        printf("Retransmits: %u\n", info.tcpi_retransmits);
        printf("Unacknowledged packets: %u\n", info.tcpi_unacked);
        printf("SACK'd packets: %u\n", info.tcpi_sacked);
        printf("Lost packets: %u\n", info.tcpi_lost);
        printf("Retrans packets: %u\n", info.tcpi_retrans);
        printf("RTT: %u us\n", info.tcpi_rtt);
        printf("RTT variance: %u us\n", info.tcpi_rttvar);
        printf("Send congestion window: %u\n", info.tcpi_snd_cwnd);
        printf("Smoothed RTT: %u us\n", info.tcpi_rtt);
    } else {
        perror("getsockopt TCP_INFO failed");
    }
}

// Example usage in a client
int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    // ... connect to server ...
    
    // Send some data
    const char *data = "Hello with SACK support!";
    send(sockfd, data, strlen(data), 0);
    
    // Check TCP info including SACK stats
    print_tcp_info(sockfd);
    
    close(sockfd);
    return 0;
}
```

### Example 3: C++ SACK-Aware TCP Client

```cpp
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

class TCPClient {
private:
    int sockfd;
    struct sockaddr_in server_addr;
    
public:
    TCPClient(const std::string& ip, int port) : sockfd(-1) {
        // Create socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed");
        }
        
        // Setup server address
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr) <= 0) {
            close(sockfd);
            throw std::runtime_error("Invalid address");
        }
    }
    
    ~TCPClient() {
        if (sockfd >= 0) {
            close(sockfd);
        }
    }
    
    void connect_to_server() {
        if (connect(sockfd, (struct sockaddr*)&server_addr, 
                   sizeof(server_addr)) < 0) {
            throw std::runtime_error("Connection failed");
        }
        std::cout << "Connected to server" << std::endl;
    }
    
    bool is_sack_enabled() {
        int sack_enabled = 0;
        socklen_t len = sizeof(sack_enabled);
        
        if (getsockopt(sockfd, IPPROTO_TCP, TCP_SACK_ENABLE, 
                      &sack_enabled, &len) == 0) {
            return sack_enabled != 0;
        }
        return false;
    }
    
    void send_data(const std::string& data) {
        ssize_t sent = send(sockfd, data.c_str(), data.length(), 0);
        if (sent < 0) {
            throw std::runtime_error("Send failed");
        }
        std::cout << "Sent " << sent << " bytes" << std::endl;
    }
    
    std::string receive_data(size_t buffer_size = 1024) {
        char buffer[buffer_size];
        ssize_t received = recv(sockfd, buffer, buffer_size - 1, 0);
        
        if (received < 0) {
            throw std::runtime_error("Receive failed");
        }
        
        buffer[received] = '\0';
        return std::string(buffer);
    }
    
    void print_connection_info() {
        std::cout << "\n=== Connection Information ===" << std::endl;
        std::cout << "SACK Enabled: " 
                  << (is_sack_enabled() ? "Yes" : "No") << std::endl;
        
        struct tcp_info info;
        socklen_t info_len = sizeof(info);
        
        if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, 
                      &info, &info_len) == 0) {
            std::cout << "Retransmits: " << info.tcpi_retransmits << std::endl;
            std::cout << "SACK'd packets: " << info.tcpi_sacked << std::endl;
            std::cout << "Lost packets: " << info.tcpi_lost << std::endl;
            std::cout << "RTT: " << info.tcpi_rtt << " us" << std::endl;
        }
    }
};

int main() {
    try {
        TCPClient client("127.0.0.1", 8080);
        client.connect_to_server();
        
        client.print_connection_info();
        
        client.send_data("Hello, SACK world!");
        std::string response = client.receive_data();
        
        std::cout << "Received: " << response << std::endl;
        
        client.print_connection_info();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

---

## Rust Programming Examples

### Example 1: Basic TCP Client with SACK (Rust)

```rust
use std::io::{Read, Write};
use std::net::TcpStream;
use std::os::unix::io::AsRawFd;

// Constants for socket options (Linux)
const IPPROTO_TCP: i32 = 6;
const TCP_INFO: i32 = 11;
const TCP_SACK_ENABLE: i32 = 12; // Platform-specific

fn main() -> std::io::Result<()> {
    // Connect to server
    let mut stream = TcpStream::connect("127.0.0.1:8080")?;
    println!("Connected to server");
    
    // Check if SACK is enabled
    match is_sack_enabled(&stream) {
        Ok(true) => println!("SACK is enabled"),
        Ok(false) => println!("SACK is disabled"),
        Err(e) => println!("Could not check SACK status: {}", e),
    }
    
    // Send data
    let message = b"Hello from Rust with SACK support!";
    stream.write_all(message)?;
    println!("Sent {} bytes", message.len());
    
    // Receive response
    let mut buffer = [0u8; 1024];
    let n = stream.read(&mut buffer)?;
    println!("Received: {}", String::from_utf8_lossy(&buffer[..n]));
    
    // Print connection statistics
    print_tcp_stats(&stream)?;
    
    Ok(())
}

#[cfg(target_os = "linux")]
fn is_sack_enabled(stream: &TcpStream) -> std::io::Result<bool> {
    use std::mem;
    
    let fd = stream.as_raw_fd();
    let mut sack_enabled: i32 = 0;
    let mut len = mem::size_of::<i32>() as u32;
    
    unsafe {
        let ret = libc::getsockopt(
            fd,
            IPPROTO_TCP,
            TCP_SACK_ENABLE,
            &mut sack_enabled as *mut _ as *mut libc::c_void,
            &mut len as *mut u32,
        );
        
        if ret == 0 {
            Ok(sack_enabled != 0)
        } else {
            Err(std::io::Error::last_os_error())
        }
    }
}

#[cfg(not(target_os = "linux"))]
fn is_sack_enabled(_stream: &TcpStream) -> std::io::Result<bool> {
    // SACK checking not implemented for this platform
    Ok(false)
}

#[cfg(target_os = "linux")]
fn print_tcp_stats(stream: &TcpStream) -> std::io::Result<()> {
    use std::mem;
    
    #[repr(C)]
    struct TcpInfo {
        state: u8,
        ca_state: u8,
        retransmits: u8,
        probes: u8,
        backoff: u8,
        options: u8,
        snd_wscale_rcv_wscale: u8,
        delivery_rate_app_limited_fastopen_client_fail: u8,
        rto: u32,
        ato: u32,
        snd_mss: u32,
        rcv_mss: u32,
        unacked: u32,
        sacked: u32,
        lost: u32,
        retrans: u32,
        fackets: u32,
        last_data_sent: u32,
        last_ack_sent: u32,
        last_data_recv: u32,
        last_ack_recv: u32,
        pmtu: u32,
        rcv_ssthresh: u32,
        rtt: u32,
        rttvar: u32,
        snd_ssthresh: u32,
        snd_cwnd: u32,
        advmss: u32,
        reordering: u32,
        // ... more fields exist but omitted for brevity
    }
    
    let fd = stream.as_raw_fd();
    let mut info: TcpInfo = unsafe { mem::zeroed() };
    let mut len = mem::size_of::<TcpInfo>() as u32;
    
    unsafe {
        let ret = libc::getsockopt(
            fd,
            IPPROTO_TCP,
            TCP_INFO,
            &mut info as *mut _ as *mut libc::c_void,
            &mut len as *mut u32,
        );
        
        if ret == 0 {
            println!("\n=== TCP Connection Statistics ===");
            println!("State: {}", info.state);
            println!("Retransmits: {}", info.retransmits);
            println!("Unacknowledged packets: {}", info.unacked);
            println!("SACK'd packets: {}", info.sacked);
            println!("Lost packets: {}", info.lost);
            println!("Retrans packets: {}", info.retrans);
            println!("RTT: {} μs", info.rtt);
            println!("RTT variance: {} μs", info.rttvar);
            println!("Congestion window: {}", info.snd_cwnd);
            Ok(())
        } else {
            Err(std::io::Error::last_os_error())
        }
    }
}

#[cfg(not(target_os = "linux"))]
fn print_tcp_stats(_stream: &TcpStream) -> std::io::Result<()> {
    println!("TCP statistics not available on this platform");
    Ok(())
}
```

### Example 2: Rust TCP Server with SACK Monitoring

```rust
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::thread;
use std::time::Duration;

fn handle_client(mut stream: TcpStream) -> std::io::Result<()> {
    let peer_addr = stream.peer_addr()?;
    println!("New connection from: {}", peer_addr);
    
    // Check SACK status
    #[cfg(target_os = "linux")]
    {
        match check_sack_negotiated(&stream) {
            Ok(true) => println!("SACK negotiated with {}", peer_addr),
            Ok(false) => println!("SACK not negotiated with {}", peer_addr),
            Err(_) => println!("Could not check SACK status"),
        }
    }
    
    let mut buffer = [0u8; 4096];
    
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => {
                println!("Client {} disconnected", peer_addr);
                break;
            }
            Ok(n) => {
                println!("Received {} bytes from {}", n, peer_addr);
                
                // Echo back
                stream.write_all(&buffer[..n])?;
                
                // Print stats periodically
                #[cfg(target_os = "linux")]
                print_sack_stats(&stream)?;
            }
            Err(e) => {
                eprintln!("Error reading from {}: {}", peer_addr, e);
                break;
            }
        }
        
        thread::sleep(Duration::from_millis(10));
    }
    
    Ok(())
}

#[cfg(target_os = "linux")]
fn check_sack_negotiated(stream: &TcpStream) -> std::io::Result<bool> {
    use std::os::unix::io::AsRawFd;
    
    let fd = stream.as_raw_fd();
    let mut options: i32 = 0;
    let mut len = std::mem::size_of::<i32>() as u32;
    
    unsafe {
        let ret = libc::getsockopt(
            fd,
            6, // IPPROTO_TCP
            12, // TCP_SACK_ENABLE
            &mut options as *mut _ as *mut libc::c_void,
            &mut len,
        );
        
        if ret == 0 {
            Ok((options & 1) != 0)
        } else {
            Err(std::io::Error::last_os_error())
        }
    }
}

#[cfg(target_os = "linux")]
fn print_sack_stats(stream: &TcpStream) -> std::io::Result<()> {
    use std::os::unix::io::AsRawFd;
    
    let fd = stream.as_raw_fd();
    
    // Simplified tcp_info structure
    #[repr(C)]
    struct TcpInfoSimple {
        _padding: [u8; 16],
        unacked: u32,
        sacked: u32,
        lost: u32,
        retrans: u32,
    }
    
    let mut info: TcpInfoSimple = unsafe { std::mem::zeroed() };
    let mut len = std::mem::size_of::<TcpInfoSimple>() as u32;
    
    unsafe {
        let ret = libc::getsockopt(
            fd,
            6, // IPPROTO_TCP
            11, // TCP_INFO
            &mut info as *mut _ as *mut libc::c_void,
            &mut len,
        );
        
        if ret == 0 && info.sacked > 0 {
            println!("  → SACK stats: sacked={}, lost={}, retrans={}", 
                    info.sacked, info.lost, info.retrans);
        }
    }
    
    Ok(())
}

fn main() -> std::io::Result<()> {
    let listener = TcpListener::bind("0.0.0.0:8080")?;
    println!("Server listening on port 8080");
    println!("SACK support will be negotiated with clients\n");
    
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                thread::spawn(move || {
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

### Example 3: Rust Async TCP with Tokio and SACK

```rust
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let listener = TcpListener::bind("0.0.0.0:8080").await?;
    println!("Async server listening on port 8080");
    
    loop {
        let (socket, addr) = listener.accept().await?;
        println!("New connection from: {}", addr);
        
        tokio::spawn(async move {
            if let Err(e) = handle_connection(socket).await {
                eprintln!("Error handling connection: {}", e);
            }
        });
    }
}

async fn handle_connection(mut socket: TcpStream) -> Result<(), Box<dyn Error>> {
    // Check SACK on the underlying socket
    #[cfg(target_os = "linux")]
    {
        let std_socket = socket.into_std()?;
        check_and_print_sack(&std_socket)?;
        socket = TcpStream::from_std(std_socket)?;
    }
    
    let mut buffer = vec![0u8; 8192];
    
    loop {
        let n = socket.read(&mut buffer).await?;
        
        if n == 0 {
            println!("Connection closed");
            break;
        }
        
        println!("Received {} bytes", n);
        
        // Echo back
        socket.write_all(&buffer[..n]).await?;
        
        // Periodic stats
        #[cfg(target_os = "linux")]
        {
            let std_socket = socket.into_std()?;
            let _ = print_connection_stats(&std_socket);
            socket = TcpStream::from_std(std_socket)?;
        }
    }
    
    Ok(())
}

#[cfg(target_os = "linux")]
fn check_and_print_sack(socket: &std::net::TcpStream) -> Result<(), Box<dyn Error>> {
    use std::os::unix::io::AsRawFd;
    
    let fd = socket.as_raw_fd();
    let mut sack_enabled: i32 = 0;
    let mut len = std::mem::size_of::<i32>() as u32;
    
    unsafe {
        libc::getsockopt(
            fd,
            6, // IPPROTO_TCP
            12, // TCP_SACK_ENABLE
            &mut sack_enabled as *mut _ as *mut libc::c_void,
            &mut len,
        );
    }
    
    println!("SACK is {}", if sack_enabled != 0 { "enabled" } else { "disabled" });
    Ok(())
}

#[cfg(target_os = "linux")]
fn print_connection_stats(socket: &std::net::TcpStream) -> Result<(), Box<dyn Error>> {
    use std::os::unix::io::AsRawFd;
    
    let fd = socket.as_raw_fd();
    
    // Minimal tcp_info for SACK stats
    #[repr(C)]
    struct TcpInfoMin {
        _pad: [u8; 16],
        unacked: u32,
        sacked: u32,
        lost: u32,
    }
    
    let mut info: TcpInfoMin = unsafe { std::mem::zeroed() };
    let mut len = std::mem::size_of::<TcpInfoMin>() as u32;
    
    unsafe {
        libc::getsockopt(
            fd,
            6, // IPPROTO_TCP
            11, // TCP_INFO
            &mut info as *mut _ as *mut libc::c_void,
            &mut len,
        );
    }
    
    if info.sacked > 0 || info.lost > 0 {
        println!("Stats: unacked={}, sacked={}, lost={}", 
                info.unacked, info.sacked, info.lost);
    }
    
    Ok(())
}
```

---

## Summary

**TCP Selective Acknowledgment (SACK)** is a critical TCP extension that dramatically improves performance in lossy network conditions by allowing receivers to inform senders about all successfully received segments, not just contiguous ones.

### Key Points:

1. **Purpose**: Enables efficient retransmission by identifying specific missing segments rather than retransmitting entire windows

2. **Mechanism**: 
   - Negotiated during TCP handshake with SACK-Permitted option
   - SACK blocks specify ranges of received data using sequence numbers
   - Sender selectively retransmits only the missing segments

3. **Performance Benefits**:
   - 20-40% improvement in recovery time for packet loss
   - Reduced bandwidth waste from unnecessary retransmissions
   - Better performance on high-latency or high-loss networks

4. **Implementation**:
   - Enabled by default on most modern operating systems
   - Transparent to applications (handled by TCP stack)
   - Can be monitored via socket options (`TCP_INFO`, `TCP_SACK_ENABLE`)

5. **Programming Considerations**:
   - Applications typically don't need to explicitly manage SACK
   - Statistics can be retrieved for monitoring and debugging
   - Platform-specific APIs required for detailed SACK information

SACK is one of the most important TCP optimizations for modern networks, especially relevant for video streaming, file transfers, and any application requiring reliable delivery over imperfect networks. It's universally supported and transparently provides significant performance improvements without requiring application changes.