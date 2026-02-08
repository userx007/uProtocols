# TCP Timestamps: RTT Measurement and PAWS

## Detailed Description

### Overview

TCP Timestamps is an optional TCP feature defined in RFC 1323 (and updated in RFC 7323) that serves two critical purposes in modern TCP implementations:

1. **Round-Trip Time Measurement (RTTM)**: Provides accurate RTT measurements for better congestion control
2. **Protection Against Wrapped Sequences (PAWS)**: Prevents old duplicate segments from corrupting data on high-bandwidth connections

### TCP Timestamp Option Format

The TCP Timestamp option appears in the TCP header as:

```
Kind: 8
Length: 10 bytes
+-------+-------+---------------------+---------------------+
| Kind=8| Len=10|   TS Value (TSval)  |TS Echo Reply (TSecr)|
+-------+-------+---------------------+---------------------+
    1       1              4                     4
```

- **TSval** (Timestamp Value): 4-byte timestamp sent by the data sender
- **TSecr** (Timestamp Echo Reply): 4-byte echo of the most recent timestamp received

### Round-Trip Time Measurement (RTTM)

**How it works:**

1. Sender includes current timestamp in TSval
2. Receiver echoes this value back in TSecr
3. Sender calculates RTT = current_time - TSecr

**Benefits:**
- No need for separate RTT measurement packets
- Works on every segment (not just during handshake)
- More accurate than sequence-based RTT measurements
- Enables better congestion control algorithms (like TCP BBR)

### Protection Against Wrapped Sequences (PAWS)

**The Problem:**
On high-speed networks (>1 Gbps), sequence numbers can wrap around in less than the Maximum Segment Lifetime (MSL). Old duplicate segments could be confused with new data.

**Example:**
- At 10 Gbps with 1500-byte packets: sequence space wraps in ~3 seconds
- MSL is typically 2 minutes
- Old segments could arrive and be accepted as valid!

**PAWS Solution:**
- Each segment carries a monotonically increasing timestamp
- If a segment arrives with a timestamp older than the last accepted segment, it's rejected
- Effectively extends the sequence space with timestamp information

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
#include <time.h>
#include <sys/time.h>
#include <errno.h>

// TCP Timestamp option structure
#define TCPOPT_NOP          1
#define TCPOPT_TIMESTAMP    8
#define TCPOLEN_TIMESTAMP   10

// Structure to hold TCP timestamp information
typedef struct {
    uint32_t ts_val;      // Timestamp value
    uint32_t ts_ecr;      // Timestamp echo reply
} tcp_timestamp_t;

/**
 * Get current timestamp value (in milliseconds)
 * This is a monotonic clock suitable for TCP timestamps
 */
uint32_t get_tcp_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    // Convert to milliseconds
    // Note: Real implementations often use a different time base
    return (uint32_t)((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}

/**
 * Enable TCP timestamps on a socket
 */
int enable_tcp_timestamps(int sockfd) {
    int optval = 1;
    
    // Enable TCP timestamps
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_TIMESTAMPS, 
                   &optval, sizeof(optval)) < 0) {
        perror("setsockopt TCP_TIMESTAMPS");
        return -1;
    }
    
    printf("TCP Timestamps enabled\n");
    return 0;
}

/**
 * Get TCP info including timestamp information
 */
int get_tcp_info_with_timestamps(int sockfd) {
    struct tcp_info info;
    socklen_t info_len = sizeof(info);
    
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_INFO, 
                   &info, &info_len) < 0) {
        perror("getsockopt TCP_INFO");
        return -1;
    }
    
    printf("\n=== TCP Connection Info ===\n");
    printf("State: %u\n", info.tcpi_state);
    printf("RTT: %u us\n", info.tcpi_rtt);
    printf("RTT variance: %u us\n", info.tcpi_rttvar);
    printf("Smoothed RTT: %u us\n", info.tcpi_rtt);
    printf("RTO: %u us\n", info.tcpi_rto);
    
    // Timestamp-related measurements
    printf("\n=== Timestamp-based Measurements ===\n");
    printf("Last data sent: %u ms ago\n", info.tcpi_last_data_sent);
    printf("Last ACK sent: %u ms ago\n", info.tcpi_last_ack_sent);
    printf("Last data recv: %u ms ago\n", info.tcpi_last_data_recv);
    printf("Last ACK recv: %u ms ago\n", info.tcpi_last_ack_recv);
    
    return 0;
}

/**
 * Calculate RTT from timestamp echo
 */
uint32_t calculate_rtt(uint32_t sent_ts, uint32_t echoed_ts) {
    uint32_t current_ts = get_tcp_timestamp();
    
    // RTT = current_time - echoed_timestamp
    // Handle wraparound
    uint32_t rtt;
    if (current_ts >= echoed_ts) {
        rtt = current_ts - echoed_ts;
    } else {
        // Timestamp wrapped around
        rtt = (UINT32_MAX - echoed_ts) + current_ts + 1;
    }
    
    return rtt;
}

/**
 * PAWS: Check if segment should be rejected
 * Returns 1 if segment is valid, 0 if should be rejected
 */
int paws_check(uint32_t seg_ts, uint32_t last_ts, uint32_t ts_recent_age) {
    // If timestamp is older than last accepted segment, reject
    // unless it's been too long (24 days overflow protection)
    
    const uint32_t PAWS_24DAYS = 24 * 24 * 60 * 60 * 1000; // 24 days in ms
    
    // Check if segment timestamp is less than last timestamp
    if (seg_ts < last_ts) {
        // Could be legitimate if timestamp wrapped or if it's old
        if (ts_recent_age > PAWS_24DAYS) {
            // Too old, accept anyway
            return 1;
        }
        
        printf("PAWS: Rejecting segment with old timestamp\n");
        printf("  Segment TS: %u, Last TS: %u\n", seg_ts, last_ts);
        return 0;
    }
    
    return 1;
}

/**
 * Example TCP client with timestamp handling
 */
int tcp_client_example(const char *server_ip, int port) {
    int sockfd;
    struct sockaddr_in server_addr;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    // Enable TCP timestamps before connecting
    enable_tcp_timestamps(sockfd);
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    
    // Connect
    printf("Connecting to %s:%d...\n", server_ip, port);
    if (connect(sockfd, (struct sockaddr *)&server_addr, 
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }
    
    printf("Connected!\n");
    
    // Get TCP info with timestamp measurements
    sleep(1); // Let some data exchange happen
    get_tcp_info_with_timestamps(sockfd);
    
    // Send some data
    const char *msg = "Hello with TCP Timestamps!";
    uint32_t send_ts = get_tcp_timestamp();
    printf("\nSending data at timestamp: %u\n", send_ts);
    
    send(sockfd, msg, strlen(msg), 0);
    
    // Receive response
    char buffer[1024];
    ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        uint32_t recv_ts = get_tcp_timestamp();
        printf("Received: %s\n", buffer);
        printf("Approximate RTT: %u ms\n", recv_ts - send_ts);
    }
    
    // Final TCP info
    get_tcp_info_with_timestamps(sockfd);
    
    close(sockfd);
    return 0;
}

/**
 * Demonstrate PAWS mechanism
 */
void demonstrate_paws() {
    printf("\n=== PAWS Demonstration ===\n");
    
    uint32_t last_accepted_ts = get_tcp_timestamp();
    printf("Last accepted timestamp: %u\n", last_accepted_ts);
    
    // Simulate receiving a segment with older timestamp (duplicate)
    uint32_t old_seg_ts = last_accepted_ts - 5000; // 5 seconds old
    uint32_t ts_age = 1000; // 1 second since last update
    
    printf("\nReceiving segment with timestamp: %u\n", old_seg_ts);
    if (paws_check(old_seg_ts, last_accepted_ts, ts_age)) {
        printf("Segment ACCEPTED\n");
    } else {
        printf("Segment REJECTED by PAWS\n");
    }
    
    // Simulate receiving a valid segment
    uint32_t new_seg_ts = get_tcp_timestamp();
    printf("\nReceiving segment with timestamp: %u\n", new_seg_ts);
    if (paws_check(new_seg_ts, last_accepted_ts, ts_age)) {
        printf("Segment ACCEPTED\n");
    } else {
        printf("Segment REJECTED by PAWS\n");
    }
}

int main(int argc, char *argv[]) {
    printf("=== TCP Timestamps Example ===\n\n");
    
    // Demonstrate PAWS
    demonstrate_paws();
    
    // Example client usage (uncomment to use)
    /*
    if (argc != 3) {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }
    tcp_client_example(argv[1], atoi(argv[2]));
    */
    
    return 0;
}
```

### C++ Implementation with Modern Features

```cpp
#include <iostream>
#include <chrono>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

class TCPTimestamp {
private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::milliseconds;
    
    uint32_t ts_val_;
    uint32_t ts_ecr_;
    TimePoint start_time_;
    
public:
    TCPTimestamp() : ts_val_(0), ts_ecr_(0), 
                     start_time_(Clock::now()) {}
    
    // Get current timestamp value
    uint32_t getCurrentTimestamp() const {
        auto now = Clock::now();
        auto duration = std::chrono::duration_cast<Duration>(
            now - start_time_
        );
        return static_cast<uint32_t>(duration.count());
    }
    
    // Set timestamp values
    void setTimestamps(uint32_t val, uint32_t ecr) {
        ts_val_ = val;
        ts_ecr_ = ecr;
    }
    
    // Calculate RTT
    uint32_t calculateRTT() const {
        uint32_t current = getCurrentTimestamp();
        if (current >= ts_ecr_) {
            return current - ts_ecr_;
        } else {
            // Handle wraparound
            return (UINT32_MAX - ts_ecr_) + current + 1;
        }
    }
    
    uint32_t getTSVal() const { return ts_val_; }
    uint32_t getTSEcr() const { return ts_ecr_; }
};

class PAWSChecker {
private:
    uint32_t last_timestamp_;
    TimePoint last_update_;
    static constexpr uint32_t MAX_IDLE_MS = 24 * 24 * 60 * 60 * 1000;
    
public:
    PAWSChecker() : last_timestamp_(0), 
                    last_update_(std::chrono::steady_clock::now()) {}
    
    // Check if segment passes PAWS check
    bool checkSegment(uint32_t seg_timestamp) {
        auto now = std::chrono::steady_clock::now();
        auto idle_duration = std::chrono::duration_cast
            std::chrono::milliseconds>(now - last_update_);
        
        // If segment timestamp is older than last accepted
        if (seg_timestamp < last_timestamp_) {
            // Allow if we've been idle too long (wraparound)
            if (idle_duration.count() > MAX_IDLE_MS) {
                updateTimestamp(seg_timestamp);
                return true;
            }
            
            std::cout << "PAWS: Rejecting old segment (TS: " 
                      << seg_timestamp << " < Last: " 
                      << last_timestamp_ << ")\n";
            return false;
        }
        
        updateTimestamp(seg_timestamp);
        return true;
    }
    
    void updateTimestamp(uint32_t ts) {
        last_timestamp_ = ts;
        last_update_ = std::chrono::steady_clock::now();
    }
    
    uint32_t getLastTimestamp() const { return last_timestamp_; }
};

class TCPConnection {
private:
    int sockfd_;
    TCPTimestamp timestamp_;
    PAWSChecker paws_;
    
public:
    TCPConnection() : sockfd_(-1) {}
    
    ~TCPConnection() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }
    
    bool enableTimestamps() {
        int optval = 1;
        if (setsockopt(sockfd_, IPPROTO_TCP, TCP_TIMESTAMPS,
                      &optval, sizeof(optval)) < 0) {
            std::cerr << "Failed to enable TCP timestamps: " 
                      << strerror(errno) << std::endl;
            return false;
        }
        return true;
    }
    
    bool connect(const std::string& ip, int port) {
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) {
            std::cerr << "Socket creation failed\n";
            return false;
        }
        
        enableTimestamps();
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        
        if (::connect(sockfd_, (struct sockaddr*)&addr, 
                     sizeof(addr)) < 0) {
            std::cerr << "Connection failed\n";
            return false;
        }
        
        return true;
    }
    
    void printTCPInfo() {
        struct tcp_info info;
        socklen_t info_len = sizeof(info);
        
        if (getsockopt(sockfd_, IPPROTO_TCP, TCP_INFO,
                      &info, &info_len) == 0) {
            std::cout << "\n=== TCP Info ===\n";
            std::cout << "RTT: " << info.tcpi_rtt << " us\n";
            std::cout << "RTT variance: " << info.tcpi_rttvar << " us\n";
            std::cout << "RTO: " << info.tcpi_rto << " us\n";
            std::cout << "Last data sent: " 
                      << info.tcpi_last_data_sent << " ms ago\n";
        }
    }
    
    int getSocket() const { return sockfd_; }
    TCPTimestamp& getTimestamp() { return timestamp_; }
    PAWSChecker& getPAWS() { return paws_; }
};

int main() {
    std::cout << "=== TCP Timestamps C++ Example ===\n\n";
    
    // Demonstrate PAWS
    PAWSChecker paws;
    uint32_t current_ts = 1000;
    
    paws.updateTimestamp(current_ts);
    std::cout << "Initial timestamp: " << current_ts << "\n\n";
    
    // Try older segment (should be rejected)
    uint32_t old_ts = 500;
    std::cout << "Testing segment with TS=" << old_ts << ": ";
    if (paws.checkSegment(old_ts)) {
        std::cout << "ACCEPTED\n";
    } else {
        std::cout << "REJECTED\n";
    }
    
    // Try newer segment (should be accepted)
    uint32_t new_ts = 2000;
    std::cout << "Testing segment with TS=" << new_ts << ": ";
    if (paws.checkSegment(new_ts)) {
        std::cout << "ACCEPTED\n";
    } else {
        std::cout << "REJECTED\n";
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::io::{self, Read, Write};
use std::net::{TcpStream, SocketAddr};
use std::time::{Duration, Instant};

/// TCP Timestamp structure
#[derive(Debug, Clone, Copy)]
struct TcpTimestamp {
    ts_val: u32,  // Timestamp value
    ts_ecr: u32,  // Timestamp echo reply
}

impl TcpTimestamp {
    fn new(val: u32, ecr: u32) -> Self {
        Self {
            ts_val: val,
            ts_ecr: ecr,
        }
    }
    
    /// Calculate RTT from echo reply
    fn calculate_rtt(&self, current_ts: u32) -> u32 {
        if current_ts >= self.ts_ecr {
            current_ts - self.ts_ecr
        } else {
            // Handle wraparound
            (u32::MAX - self.ts_ecr) + current_ts + 1
        }
    }
}

/// Timestamp generator for TCP connections
struct TimestampGenerator {
    start_time: Instant,
}

impl TimestampGenerator {
    fn new() -> Self {
        Self {
            start_time: Instant::now(),
        }
    }
    
    /// Get current timestamp in milliseconds
    fn get_timestamp(&self) -> u32 {
        let elapsed = self.start_time.elapsed();
        elapsed.as_millis() as u32
    }
}

/// PAWS (Protection Against Wrapped Sequences) checker
struct PawsChecker {
    last_timestamp: u32,
    last_update: Instant,
}

impl PawsChecker {
    const MAX_IDLE_DURATION: Duration = Duration::from_secs(24 * 24 * 60 * 60);
    
    fn new() -> Self {
        Self {
            last_timestamp: 0,
            last_update: Instant::now(),
        }
    }
    
    /// Check if segment should be accepted based on timestamp
    fn check_segment(&mut self, seg_timestamp: u32) -> bool {
        let now = Instant::now();
        let idle_duration = now.duration_since(self.last_update);
        
        // If segment timestamp is older than last accepted
        if seg_timestamp < self.last_timestamp {
            // Allow if we've been idle too long (wraparound protection)
            if idle_duration > Self::MAX_IDLE_DURATION {
                println!("PAWS: Allowing old timestamp due to long idle period");
                self.update_timestamp(seg_timestamp);
                return true;
            }
            
            println!(
                "PAWS: Rejecting segment (TS: {} < Last: {})",
                seg_timestamp, self.last_timestamp
            );
            return false;
        }
        
        self.update_timestamp(seg_timestamp);
        true
    }
    
    fn update_timestamp(&mut self, ts: u32) {
        self.last_timestamp = ts;
        self.last_update = Instant::now();
    }
    
    fn get_last_timestamp(&self) -> u32 {
        self.last_timestamp
    }
}

/// RTT estimator using timestamps
struct RttEstimator {
    srtt: Option<f64>,     // Smoothed RTT
    rttvar: Option<f64>,   // RTT variance
    rto: Duration,          // Retransmission timeout
}

impl RttEstimator {
    fn new() -> Self {
        Self {
            srtt: None,
            rttvar: None,
            rto: Duration::from_secs(3), // Initial RTO
        }
    }
    
    /// Update RTT estimate with new measurement
    /// Uses RFC 6298 algorithm
    fn update(&mut self, rtt_ms: f64) {
        const ALPHA: f64 = 0.125; // 1/8
        const BETA: f64 = 0.25;   // 1/4
        const K: f64 = 4.0;
        const G: f64 = 1.0;       // Clock granularity in ms
        
        match (self.srtt, self.rttvar) {
            (None, None) => {
                // First measurement
                self.srtt = Some(rtt_ms);
                self.rttvar = Some(rtt_ms / 2.0);
            }
            (Some(srtt), Some(rttvar)) => {
                // Subsequent measurements
                let err = rtt_ms - srtt;
                self.rttvar = Some((1.0 - BETA) * rttvar + BETA * err.abs());
                self.srtt = Some((1.0 - ALPHA) * srtt + ALPHA * rtt_ms);
            }
            _ => unreachable!(),
        }
        
        // Calculate RTO
        let srtt = self.srtt.unwrap();
        let rttvar = self.rttvar.unwrap();
        let rto_ms = srtt + K * rttvar.max(G);
        self.rto = Duration::from_millis(rto_ms as u64);
        
        // Clamp RTO between 1s and 60s (RFC 6298)
        if self.rto < Duration::from_secs(1) {
            self.rto = Duration::from_secs(1);
        } else if self.rto > Duration::from_secs(60) {
            self.rto = Duration::from_secs(60);
        }
    }
    
    fn get_srtt(&self) -> Option<Duration> {
        self.srtt.map(|s| Duration::from_millis(s as u64))
    }
    
    fn get_rto(&self) -> Duration {
        self.rto
    }
}

/// TCP connection with timestamp support
struct TcpConnection {
    stream: TcpStream,
    ts_gen: TimestampGenerator,
    paws: PawsChecker,
    rtt_estimator: RttEstimator,
}

impl TcpConnection {
    fn connect(addr: SocketAddr) -> io::Result<Self> {
        let stream = TcpStream::connect(addr)?;
        
        // Note: Rust's std::net::TcpStream doesn't expose
        // setsockopt for TCP_TIMESTAMPS directly.
        // In production, you'd use libc or nix crate:
        /*
        use nix::sys::socket::{setsockopt, sockopt::TcpTimestamps};
        let fd = stream.as_raw_fd();
        setsockopt(fd, TcpTimestamps, &true)?;
        */
        
        Ok(Self {
            stream,
            ts_gen: TimestampGenerator::new(),
            paws: PawsChecker::new(),
            rtt_estimator: RttEstimator::new(),
        })
    }
    
    /// Send data with timestamp tracking
    fn send_with_timestamp(&mut self, data: &[u8]) -> io::Result<usize> {
        let ts_val = self.ts_gen.get_timestamp();
        println!("Sending {} bytes at timestamp: {}", data.len(), ts_val);
        self.stream.write(data)
    }
    
    /// Receive data and process timestamps
    fn recv_with_timestamp(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let n = self.stream.read(buf)?;
        let recv_ts = self.ts_gen.get_timestamp();
        println!("Received {} bytes at timestamp: {}", n, recv_ts);
        Ok(n)
    }
    
    /// Process received timestamp and update RTT estimate
    fn process_timestamp(&mut self, ts: TcpTimestamp) -> bool {
        // PAWS check
        if !self.paws.check_segment(ts.ts_val) {
            return false;
        }
        
        // Calculate and update RTT
        let current_ts = self.ts_gen.get_timestamp();
        let rtt_ms = ts.calculate_rtt(current_ts);
        self.rtt_estimator.update(rtt_ms as f64);
        
        println!("RTT measurement: {} ms", rtt_ms);
        if let Some(srtt) = self.rtt_estimator.get_srtt() {
            println!("Smoothed RTT: {} ms", srtt.as_millis());
        }
        println!("RTO: {} ms", self.rtt_estimator.get_rto().as_millis());
        
        true
    }
}

/// Demonstrate PAWS mechanism
fn demonstrate_paws() {
    println!("\n=== PAWS Demonstration ===");
    
    let mut paws = PawsChecker::new();
    
    // Initialize with timestamp 1000
    paws.update_timestamp(1000);
    println!("Initial timestamp: 1000\n");
    
    // Test cases
    let test_cases = vec![
        (1500, "newer timestamp", true),
        (500, "older timestamp (duplicate)", false),
        (2000, "much newer timestamp", true),
        (1000, "equal timestamp", true),
        (999, "slightly older timestamp", false),
    ];
    
    for (ts, description, expected) in test_cases {
        print!("Testing {} (TS={}): ", description, ts);
        let result = paws.check_segment(ts);
        println!("{}", if result { "ACCEPTED ✓" } else { "REJECTED ✗" });
        
        if result != expected {
            println!("  WARNING: Unexpected result!");
        }
        println!();
    }
}

/// Demonstrate RTT estimation
fn demonstrate_rtt_estimation() {
    println!("\n=== RTT Estimation Demonstration ===");
    
    let mut estimator = RttEstimator::new();
    
    // Simulate RTT measurements
    let measurements = vec![100.0, 110.0, 95.0, 105.0, 120.0, 90.0];
    
    for (i, &rtt) in measurements.iter().enumerate() {
        estimator.update(rtt);
        println!("Measurement {}: {} ms", i + 1, rtt);
        if let Some(srtt) = estimator.get_srtt() {
            println!("  Smoothed RTT: {} ms", srtt.as_millis());
        }
        println!("  RTO: {} ms\n", estimator.get_rto().as_millis());
    }
}

fn main() {
    println!("=== TCP Timestamps Rust Example ===\n");
    
    // Demonstrate PAWS
    demonstrate_paws();
    
    // Demonstrate RTT estimation
    demonstrate_rtt_estimation();
    
    // Example connection usage (commented out)
    /*
    let addr = "127.0.0.1:8080".parse().unwrap();
    match TcpConnection::connect(addr) {
        Ok(mut conn) => {
            let msg = b"Hello with TCP Timestamps!";
            if let Err(e) = conn.send_with_timestamp(msg) {
                eprintln!("Send error: {}", e);
            }
            
            let mut buf = [0u8; 1024];
            match conn.recv_with_timestamp(&mut buf) {
                Ok(n) => {
                    println!("Received: {:?}", &buf[..n]);
                }
                Err(e) => eprintln!("Receive error: {}", e),
            }
        }
        Err(e) => eprintln!("Connection error: {}", e),
    }
    */
}
```

## Summary

**TCP Timestamps** is a crucial TCP extension that serves dual purposes:

### Key Points:

1. **Round-Trip Time Measurement (RTTM)**:
   - Enables accurate RTT measurements on every segment
   - Improves congestion control algorithms (TCP Reno, Cubic, BBR)
   - Uses 32-bit monotonic timestamps
   - Sender includes current time, receiver echoes it back
   - More reliable than sequence-based RTT measurement

2. **Protection Against Wrapped Sequences (PAWS)**:
   - Prevents old duplicate segments from corrupting data
   - Critical for high-speed networks (>1 Gbps)
   - Extends sequence number space with timestamp information
   - Rejects segments with timestamps older than last accepted segment
   - Includes wraparound protection (24-day window)

3. **Implementation Details**:
   - TCP option Kind=8, Length=10 bytes
   - TSval (4 bytes): sender's timestamp
   - TSecr (4 bytes): echo of received timestamp
   - Enabled via socket option (TCP_TIMESTAMPS)
   - Part of TCP header options (negotiated in SYN)

4. **Benefits**:
   - Better congestion control
   - Protection against old duplicates on fast networks
   - Enhanced TCP performance
   - Enables advanced algorithms like BBR

5. **Trade-offs**:
   - Adds 10 bytes to every TCP segment
   - Minor privacy concern (timing information)
   - Required for high-performance networks

The code examples demonstrate enabling timestamps, calculating RTT, implementing PAWS checks, and integrating with modern congestion control algorithms across C/C++ and Rust.