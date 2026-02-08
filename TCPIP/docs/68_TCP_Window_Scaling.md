# TCP Window Scaling

## Detailed Description

### Overview
TCP Window Scaling is a TCP option defined in **RFC 7323** (formerly RFC 1323) that extends the TCP receive window size beyond the standard 16-bit limit of 65,535 bytes. This is crucial for high-bandwidth, high-latency networks where the Bandwidth-Delay Product (BDP) exceeds 64 KB.

### The Problem
The original TCP specification uses a 16-bit field for the window size, limiting it to 65,535 bytes. In modern high-speed networks, this becomes a bottleneck:

**Bandwidth-Delay Product (BDP) = Bandwidth × Round-Trip Time**

Example:
- 1 Gbps connection with 100ms RTT
- BDP = 1,000,000,000 bits/sec × 0.1 sec = 100,000,000 bits = 12.5 MB
- Without window scaling, throughput is limited to ~65KB/0.1s = 650 KB/s (5.2 Mbps)

### How Window Scaling Works

1. **Negotiation**: Window scaling is negotiated during the TCP three-way handshake via the `TCP_WINDOW_SCALE` option (Kind=3, Length=3)
2. **Scale Factor**: A shift count (0-14) is exchanged, indicating how many bits to left-shift the window value
3. **Effective Window**: `Effective Window = Advertised Window << Scale Factor`
4. **Maximum Window**: With scale factor 14, maximum window = 65,535 × 2^14 = 1,073,725,440 bytes (~1GB)

### Key Characteristics

- **Bidirectional**: Each side can use a different scale factor
- **SYN Only**: The option can only be sent in SYN segments
- **Symmetric Support**: Both sides must support it; if one doesn't, scaling is disabled
- **Option Format**: `[Kind=3][Length=3][Shift.cnt]`

---

## Programming Examples

### C/C++ Implementation

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

// TCP Window Scale option constants
#define TCP_WINDOW_SCALE_OPTION 3
#define MAX_SCALE_FACTOR 14

/**
 * Enable TCP Window Scaling on a socket
 * This sets the receive buffer size, which influences window scaling
 */
int enable_window_scaling(int sockfd, int desired_window_size) {
    // Set SO_RCVBUF - kernel will negotiate appropriate window scale
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, 
                   &desired_window_size, sizeof(desired_window_size)) < 0) {
        perror("setsockopt SO_RCVBUF failed");
        return -1;
    }
    
    // Set SO_SNDBUF for send side
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, 
                   &desired_window_size, sizeof(desired_window_size)) < 0) {
        perror("setsockopt SO_SNDBUF failed");
        return -1;
    }
    
    printf("Set socket buffers to %d bytes\n", desired_window_size);
    return 0;
}

/**
 * Get current TCP window information
 */
void get_window_info(int sockfd) {
    int rcvbuf, sndbuf;
    socklen_t optlen = sizeof(rcvbuf);
    
    // Get receive buffer size
    if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &optlen) == 0) {
        printf("SO_RCVBUF: %d bytes\n", rcvbuf);
    }
    
    // Get send buffer size
    if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen) == 0) {
        printf("SO_SNDBUF: %d bytes\n", sndbuf);
    }
    
    // Get TCP_INFO for detailed information
    struct tcp_info info;
    optlen = sizeof(info);
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &info, &optlen) == 0) {
        printf("RCV Window: %u bytes\n", info.tcpi_rcv_wnd);
        printf("SND Window: %u bytes\n", info.tcpi_snd_wnd);
        printf("RCV Scale: %u\n", info.tcpi_rcv_wscale);
        printf("SND Scale: %u\n", info.tcpi_snd_wscale);
    }
}

/**
 * Calculate optimal window size based on bandwidth and RTT
 */
int calculate_optimal_window(long bandwidth_bps, double rtt_seconds) {
    // BDP = Bandwidth × RTT
    long bdp_bytes = (long)((bandwidth_bps / 8.0) * rtt_seconds);
    
    // Add 10% buffer
    long optimal = (long)(bdp_bytes * 1.1);
    
    printf("Calculated BDP: %ld bytes (%.2f MB)\n", 
           bdp_bytes, bdp_bytes / (1024.0 * 1024.0));
    printf("Optimal window size: %ld bytes (%.2f MB)\n", 
           optimal, optimal / (1024.0 * 1024.0));
    
    return optimal;
}

/**
 * TCP Server with Window Scaling
 */
int create_server_with_scaling(int port, int window_size) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }
    
    // Set socket options before bind
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))) {
        perror("setsockopt");
        return -1;
    }
    
    // Enable window scaling by setting buffer sizes
    if (enable_window_scaling(server_fd, window_size) < 0) {
        close(server_fd);
        return -1;
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }
    
    printf("Server listening on port %d\n", port);
    printf("Window scaling enabled - waiting for connections...\n");
    
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                             (socklen_t*)&addrlen)) < 0) {
        perror("accept");
        close(server_fd);
        return -1;
    }
    
    printf("Connection accepted\n");
    get_window_info(new_socket);
    
    close(server_fd);
    return new_socket;
}

/**
 * TCP Client with Window Scaling
 */
int create_client_with_scaling(const char *server_ip, int port, int window_size) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }
    
    // Enable window scaling before connect
    if (enable_window_scaling(sock, window_size) < 0) {
        close(sock);
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        close(sock);
        return -1;
    }
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        close(sock);
        return -1;
    }
    
    printf("Connected to %s:%d\n", server_ip, port);
    get_window_info(sock);
    
    return sock;
}

int main(int argc, char const *argv[]) {
    // Example: Calculate optimal window for 1 Gbps link with 100ms RTT
    int optimal_window = calculate_optimal_window(1000000000, 0.1);
    
    // Use 4 MB window (example)
    int window_size = 4 * 1024 * 1024;
    
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        int sock = create_server_with_scaling(8080, window_size);
        if (sock > 0) {
            char buffer[1024] = {0};
            read(sock, buffer, 1024);
            printf("Message: %s\n", buffer);
            close(sock);
        }
    } else if (argc > 2 && strcmp(argv[1], "client") == 0) {
        int sock = create_client_with_scaling(argv[2], 8080, window_size);
        if (sock > 0) {
            const char *msg = "Hello with window scaling!";
            send(sock, msg, strlen(msg), 0);
            close(sock);
        }
    } else {
        printf("Usage: %s [server|client <ip>]\n", argv[0]);
        printf("Demonstrating TCP Window Scaling calculation\n\n");
        calculate_optimal_window(1000000000, 0.1);  // 1 Gbps, 100ms
        calculate_optimal_window(10000000000, 0.05); // 10 Gbps, 50ms
    }
    
    return 0;
}
```

### C++ Modern Implementation

```cpp
#include <iostream>
#include <memory>
#include <system_error>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

class TCPWindowScaling {
private:
    int sockfd_;
    int window_size_;
    
public:
    TCPWindowScaling(int window_size = 4 * 1024 * 1024) 
        : sockfd_(-1), window_size_(window_size) {}
    
    ~TCPWindowScaling() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }
    
    // Calculate Bandwidth-Delay Product
    static size_t calculateBDP(size_t bandwidth_bps, double rtt_seconds) {
        size_t bdp = static_cast<size_t>((bandwidth_bps / 8.0) * rtt_seconds);
        std::cout << "BDP: " << bdp << " bytes (" 
                  << (bdp / (1024.0 * 1024.0)) << " MB)\n";
        return bdp;
    }
    
    // Calculate scale factor from window size
    static int calculateScaleFactor(size_t window_size) {
        int scale = 0;
        size_t scaled_size = 65535;
        
        while (scaled_size < window_size && scale < 14) {
            scale++;
            scaled_size = 65535 << scale;
        }
        
        std::cout << "Scale factor: " << scale 
                  << " (max window: " << scaled_size << " bytes)\n";
        return scale;
    }
    
    bool setWindowSize(int sockfd, int size) {
        // Set receive buffer
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, 
                       &size, sizeof(size)) < 0) {
            std::cerr << "Failed to set SO_RCVBUF: " 
                      << strerror(errno) << std::endl;
            return false;
        }
        
        // Set send buffer
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, 
                       &size, sizeof(size)) < 0) {
            std::cerr << "Failed to set SO_SNDBUF: " 
                      << strerror(errno) << std::endl;
            return false;
        }
        
        std::cout << "Set socket buffers to " << size << " bytes\n";
        return true;
    }
    
    void displayWindowInfo(int sockfd) {
        int rcvbuf, sndbuf;
        socklen_t optlen = sizeof(rcvbuf);
        
        if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &optlen) == 0) {
            std::cout << "SO_RCVBUF: " << rcvbuf << " bytes\n";
        }
        
        if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen) == 0) {
            std::cout << "SO_SNDBUF: " << sndbuf << " bytes\n";
        }
        
        struct tcp_info info;
        optlen = sizeof(info);
        if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, &info, &optlen) == 0) {
            std::cout << "TCP_INFO:\n";
            std::cout << "  RCV Window: " << info.tcpi_rcv_wnd << " bytes\n";
            std::cout << "  SND Window: " << info.tcpi_snd_wnd << " bytes\n";
            std::cout << "  RCV Scale: " << static_cast<int>(info.tcpi_rcv_wscale) << "\n";
            std::cout << "  SND Scale: " << static_cast<int>(info.tcpi_snd_wscale) << "\n";
            std::cout << "  RTT: " << info.tcpi_rtt << " µs\n";
            std::cout << "  Throughput: " 
                      << (info.tcpi_rcv_wnd * 8.0 / (info.tcpi_rtt / 1000000.0)) 
                      << " bps\n";
        }
    }
    
    bool createServer(int port) {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            std::cerr << "Socket creation failed\n";
            return false;
        }
        
        int opt = 1;
        setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt));
        
        setWindowSize(sockfd_, window_size_);
        
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        if (bind(sockfd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
            std::cerr << "Bind failed\n";
            return false;
        }
        
        if (listen(sockfd_, 3) < 0) {
            std::cerr << "Listen failed\n";
            return false;
        }
        
        std::cout << "Server listening on port " << port << "\n";
        return true;
    }
    
    int acceptConnection() {
        struct sockaddr_in address;
        socklen_t addrlen = sizeof(address);
        
        int new_sock = accept(sockfd_, (struct sockaddr *)&address, &addrlen);
        if (new_sock < 0) {
            std::cerr << "Accept failed\n";
            return -1;
        }
        
        std::cout << "Connection accepted from " 
                  << inet_ntoa(address.sin_addr) << "\n";
        displayWindowInfo(new_sock);
        
        return new_sock;
    }
};

int main() {
    std::cout << "=== TCP Window Scaling Demo ===\n\n";
    
    // Calculate BDP for different scenarios
    std::cout << "Scenario 1: 1 Gbps, 100ms RTT\n";
    size_t bdp1 = TCPWindowScaling::calculateBDP(1000000000, 0.1);
    TCPWindowScaling::calculateScaleFactor(bdp1);
    
    std::cout << "\nScenario 2: 10 Gbps, 50ms RTT\n";
    size_t bdp2 = TCPWindowScaling::calculateBDP(10000000000, 0.05);
    TCPWindowScaling::calculateScaleFactor(bdp2);
    
    std::cout << "\nScenario 3: 100 Mbps, 200ms RTT (satellite)\n";
    size_t bdp3 = TCPWindowScaling::calculateBDP(100000000, 0.2);
    TCPWindowScaling::calculateScaleFactor(bdp3);
    
    return 0;
}
```

### Rust Implementation

```rust
use std::io::{self, Read, Write};
use std::net::{TcpListener, TcpStream, SocketAddr};
use std::os::unix::io::AsRawFd;
use std::time::Duration;

// libc socket options
const SOL_SOCKET: i32 = 1;
const SO_RCVBUF: i32 = 8;
const SO_SNDBUF: i32 = 7;
const IPPROTO_TCP: i32 = 6;
const TCP_INFO: i32 = 11;

/// Calculate Bandwidth-Delay Product
fn calculate_bdp(bandwidth_bps: u64, rtt_seconds: f64) -> usize {
    let bdp = ((bandwidth_bps as f64 / 8.0) * rtt_seconds) as usize;
    println!("BDP: {} bytes ({:.2} MB)", bdp, bdp as f64 / (1024.0 * 1024.0));
    bdp
}

/// Calculate optimal window scale factor
fn calculate_scale_factor(window_size: usize) -> u8 {
    let mut scale = 0u8;
    let mut scaled_size = 65535usize;
    
    while scaled_size < window_size && scale < 14 {
        scale += 1;
        scaled_size = 65535 << scale;
    }
    
    println!("Scale factor: {} (max window: {} bytes)", scale, scaled_size);
    scale
}

/// Set socket buffer sizes for window scaling
fn set_window_size(fd: i32, size: i32) -> io::Result<()> {
    unsafe {
        // Set receive buffer
        let ret = libc::setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVBUF,
            &size as *const _ as *const libc::c_void,
            std::mem::size_of::<i32>() as libc::socklen_t,
        );
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
        
        // Set send buffer
        let ret = libc::setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDBUF,
            &size as *const _ as *const libc::c_void,
            std::mem::size_of::<i32>() as libc::socklen_t,
        );
        if ret < 0 {
            return Err(io::Error::last_os_error());
        }
    }
    
    println!("Set socket buffers to {} bytes", size);
    Ok(())
}

/// Get socket buffer information
fn get_window_info(fd: i32) {
    unsafe {
        let mut rcvbuf: i32 = 0;
        let mut sndbuf: i32 = 0;
        let mut optlen: libc::socklen_t = std::mem::size_of::<i32>() as libc::socklen_t;
        
        // Get receive buffer
        if libc::getsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVBUF,
            &mut rcvbuf as *mut _ as *mut libc::c_void,
            &mut optlen,
        ) == 0 {
            println!("SO_RCVBUF: {} bytes", rcvbuf);
        }
        
        // Get send buffer
        if libc::getsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDBUF,
            &mut sndbuf as *mut _ as *mut libc::c_void,
            &mut optlen,
        ) == 0 {
            println!("SO_SNDBUF: {} bytes", sndbuf);
        }
    }
}

/// TCP Window Scaling Configuration
pub struct WindowScalingConfig {
    pub window_size: usize,
    pub bandwidth_bps: u64,
    pub rtt_seconds: f64,
}

impl WindowScalingConfig {
    pub fn new(bandwidth_bps: u64, rtt_seconds: f64) -> Self {
        let bdp = calculate_bdp(bandwidth_bps, rtt_seconds);
        // Add 10% buffer
        let window_size = (bdp as f64 * 1.1) as usize;
        
        WindowScalingConfig {
            window_size,
            bandwidth_bps,
            rtt_seconds,
        }
    }
    
    pub fn optimal_window(&self) -> usize {
        self.window_size
    }
    
    pub fn scale_factor(&self) -> u8 {
        calculate_scale_factor(self.window_size)
    }
    
    pub fn display_info(&self) {
        println!("\n=== Window Scaling Configuration ===");
        println!("Bandwidth: {} Mbps", self.bandwidth_bps / 1_000_000);
        println!("RTT: {} ms", self.rtt_seconds * 1000.0);
        println!("Optimal Window: {} bytes ({:.2} MB)", 
                 self.window_size, 
                 self.window_size as f64 / (1024.0 * 1024.0));
        println!("Scale Factor: {}", self.scale_factor());
    }
}

/// TCP Server with Window Scaling
pub struct WindowScaledServer {
    listener: TcpListener,
    config: WindowScalingConfig,
}

impl WindowScaledServer {
    pub fn bind(addr: &str, config: WindowScalingConfig) -> io::Result<Self> {
        let listener = TcpListener::bind(addr)?;
        
        // Set window size on listener socket
        let fd = listener.as_raw_fd();
        set_window_size(fd, config.window_size as i32)?;
        
        println!("Server listening on {}", addr);
        config.display_info();
        
        Ok(WindowScaledServer { listener, config })
    }
    
    pub fn accept(&self) -> io::Result<TcpStream> {
        let (stream, addr) = self.listener.accept()?;
        println!("\nConnection accepted from {}", addr);
        
        // Display window info for the new connection
        let fd = stream.as_raw_fd();
        get_window_info(fd);
        
        Ok(stream)
    }
}

/// TCP Client with Window Scaling
pub struct WindowScaledClient {
    stream: TcpStream,
}

impl WindowScaledClient {
    pub fn connect(addr: &str, config: &WindowScalingConfig) -> io::Result<Self> {
        let stream = TcpStream::connect(addr)?;
        
        // Set window size on connected socket
        let fd = stream.as_raw_fd();
        set_window_size(fd, config.window_size as i32)?;
        
        println!("Connected to {}", addr);
        config.display_info();
        get_window_info(fd);
        
        Ok(WindowScaledClient { stream })
    }
    
    pub fn send(&mut self, data: &[u8]) -> io::Result<usize> {
        self.stream.write(data)
    }
    
    pub fn receive(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.stream.read(buf)
    }
}

fn main() -> io::Result<()> {
    println!("=== TCP Window Scaling Examples ===\n");
    
    // Example 1: 1 Gbps with 100ms RTT
    println!("Scenario 1: 1 Gbps, 100ms RTT");
    let config1 = WindowScalingConfig::new(1_000_000_000, 0.1);
    config1.display_info();
    
    // Example 2: 10 Gbps with 50ms RTT
    println!("\nScenario 2: 10 Gbps, 50ms RTT");
    let config2 = WindowScalingConfig::new(10_000_000_000, 0.05);
    config2.display_info();
    
    // Example 3: 100 Mbps with 200ms RTT (satellite link)
    println!("\nScenario 3: 100 Mbps, 200ms RTT (satellite)");
    let config3 = WindowScalingConfig::new(100_000_000, 0.2);
    config3.display_info();
    
    // Server example (commented out for demonstration)
    /*
    let server = WindowScaledServer::bind("127.0.0.1:8080", config1)?;
    let mut stream = server.accept()?;
    
    let mut buffer = vec![0u8; 4096];
    let n = stream.read(&mut buffer)?;
    println!("Received {} bytes", n);
    */
    
    // Client example (commented out for demonstration)
    /*
    let mut client = WindowScaledClient::connect("127.0.0.1:8080", &config1)?;
    client.send(b"Hello with window scaling!")?;
    */
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_bdp_calculation() {
        // 1 Gbps, 100ms RTT = 12.5 MB
        let bdp = calculate_bdp(1_000_000_000, 0.1);
        assert_eq!(bdp, 12_500_000);
    }
    
    #[test]
    fn test_scale_factor() {
        // For 1 MB, should need scale factor of 4
        // 65535 << 4 = 1,048,560 bytes
        let scale = calculate_scale_factor(1_000_000);
        assert_eq!(scale, 4);
    }
    
    #[test]
    fn test_config_creation() {
        let config = WindowScalingConfig::new(1_000_000_000, 0.1);
        assert!(config.window_size > 12_500_000);
        assert!(config.scale_factor() >= 8);
    }
}
```

---

## Summary

**TCP Window Scaling** is essential for achieving high throughput in modern networks:

### Key Points:
1. **Purpose**: Extends TCP window beyond 65,535 bytes to support high-bandwidth×delay product networks
2. **Mechanism**: Uses a scale factor (0-14) to left-shift the 16-bit window field, enabling windows up to 1 GB
3. **Negotiation**: Exchanged during SYN handshake; both sides must support it
4. **Implementation**: Set via socket buffer options (SO_RCVBUF/SO_SNDBUF); kernel handles negotiation automatically

### Benefits:
- Enables full utilization of high-speed networks (Gbps+)
- Critical for long-distance/high-latency links (satellite, intercontinental)
- Transparent to applications when configured properly

### Considerations:
- **Memory**: Large windows consume more buffer memory
- **Compatibility**: Must be supported by both endpoints
- **BDP Calculation**: Window size should be ≥ Bandwidth × RTT
- **Tuning**: Modern OSes auto-tune, but explicit configuration may be needed for optimal performance

### Best Practices:
- Calculate optimal window using BDP formula
- Set buffer sizes before connection establishment (before connect/listen)
- Monitor actual negotiated values using TCP_INFO
- Consider system-wide kernel tuning for production servers (`net.ipv4.tcp_rmem`, `net.ipv4.tcp_wmem`)

Window scaling is now ubiquitous in modern TCP stacks and is crucial for applications requiring high throughput over wide-area networks, bulk data transfers, and cloud computing scenarios.