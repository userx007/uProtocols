# Network Metrics Collection: Bandwidth, Latency, Packet Loss, and Jitter Measurement

## Detailed Description

Network metrics collection is the process of gathering quantitative data about network performance characteristics. These metrics are essential for monitoring network health, diagnosing issues, optimizing performance, and ensuring quality of service (QoS). The four primary metrics are:

### 1. **Bandwidth**
Bandwidth measures the maximum data transfer rate of a network connection, typically expressed in bits per second (bps). It represents the theoretical capacity of the link. In practice, we often measure **throughput**, which is the actual data transfer rate achieved, accounting for protocol overhead, congestion, and other real-world factors.

### 2. **Latency (Round-Trip Time)**
Latency is the time delay between sending a packet and receiving a response. It's typically measured as Round-Trip Time (RTT) - the time for a packet to travel from source to destination and back. Latency is influenced by physical distance, routing paths, processing delays, and network congestion.

### 3. **Packet Loss**
Packet loss occurs when data packets fail to reach their destination. It's expressed as a percentage of packets lost versus packets sent. Causes include network congestion, hardware failures, routing errors, and buffer overflows. Even small amounts of packet loss can significantly impact application performance, especially for real-time protocols.

### 4. **Jitter**
Jitter measures the variation in packet arrival times. While latency measures average delay, jitter measures the inconsistency of that delay. High jitter causes problems for real-time applications like VoIP and video streaming, as packets arrive at irregular intervals, making smooth playback difficult.

## Programming Implementation

### C Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <math.h>

#define PACKET_SIZE 64
#define MAX_SAMPLES 100

// Structure to hold network metrics
typedef struct {
    double min_latency;
    double max_latency;
    double avg_latency;
    double jitter;
    int packets_sent;
    int packets_received;
    double packet_loss_percent;
    double bandwidth_mbps;
} NetworkMetrics;

// Calculate checksum for ICMP packets
unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    
    if (len == 1)
        sum += *(unsigned char *)buf;
    
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    
    return result;
}

// Measure latency using ICMP echo (requires root privileges)
int measure_latency(const char *host, NetworkMetrics *metrics) {
    int sockfd;
    struct sockaddr_in addr;
    struct icmp *icmp_hdr;
    char send_buf[PACKET_SIZE];
    char recv_buf[PACKET_SIZE];
    struct timeval start, end, timeout;
    double latencies[MAX_SAMPLES];
    int samples = 10;
    
    // Create raw socket
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("Socket creation failed (try running as root)");
        return -1;
    }
    
    // Set socket timeout
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Setup destination address
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host);
    
    metrics->packets_sent = 0;
    metrics->packets_received = 0;
    double sum_latency = 0.0;
    metrics->min_latency = 999999.0;
    metrics->max_latency = 0.0;
    
    printf("Measuring latency to %s...\n", host);
    
    for (int i = 0; i < samples; i++) {
        // Prepare ICMP packet
        memset(send_buf, 0, PACKET_SIZE);
        icmp_hdr = (struct icmp *)send_buf;
        icmp_hdr->icmp_type = ICMP_ECHO;
        icmp_hdr->icmp_code = 0;
        icmp_hdr->icmp_id = getpid();
        icmp_hdr->icmp_seq = i;
        icmp_hdr->icmp_cksum = 0;
        icmp_hdr->icmp_cksum = checksum(icmp_hdr, PACKET_SIZE);
        
        // Send packet and record time
        gettimeofday(&start, NULL);
        if (sendto(sockfd, send_buf, PACKET_SIZE, 0, 
                   (struct sockaddr *)&addr, sizeof(addr)) <= 0) {
            perror("Send failed");
            continue;
        }
        metrics->packets_sent++;
        
        // Receive response
        socklen_t addr_len = sizeof(addr);
        int recv_len = recvfrom(sockfd, recv_buf, PACKET_SIZE, 0,
                                (struct sockaddr *)&addr, &addr_len);
        gettimeofday(&end, NULL);
        
        if (recv_len > 0) {
            metrics->packets_received++;
            
            // Calculate latency in milliseconds
            double latency = (end.tv_sec - start.tv_sec) * 1000.0 + 
                           (end.tv_usec - start.tv_usec) / 1000.0;
            latencies[i] = latency;
            sum_latency += latency;
            
            if (latency < metrics->min_latency) metrics->min_latency = latency;
            if (latency > metrics->max_latency) metrics->max_latency = latency;
            
            printf("Reply from %s: seq=%d time=%.2f ms\n", host, i, latency);
        } else {
            printf("Request timeout for seq=%d\n", i);
            latencies[i] = -1.0; // Mark as lost
        }
        
        usleep(100000); // 100ms between pings
    }
    
    // Calculate average latency
    metrics->avg_latency = sum_latency / metrics->packets_received;
    
    // Calculate jitter (standard deviation of latency differences)
    double jitter_sum = 0.0;
    int jitter_samples = 0;
    for (int i = 1; i < samples; i++) {
        if (latencies[i] > 0 && latencies[i-1] > 0) {
            double diff = fabs(latencies[i] - latencies[i-1]);
            jitter_sum += diff;
            jitter_samples++;
        }
    }
    metrics->jitter = jitter_samples > 0 ? jitter_sum / jitter_samples : 0.0;
    
    // Calculate packet loss
    metrics->packet_loss_percent = 
        ((double)(metrics->packets_sent - metrics->packets_received) / 
         metrics->packets_sent) * 100.0;
    
    close(sockfd);
    return 0;
}

// Measure bandwidth using TCP connection
int measure_bandwidth(const char *host, int port, NetworkMetrics *metrics) {
    int sockfd;
    struct sockaddr_in addr;
    char buffer[8192];
    struct timeval start, end;
    long bytes_transferred = 0;
    int transfer_duration = 5; // seconds
    
    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Setup server address
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);
    
    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Connection failed");
        close(sockfd);
        return -1;
    }
    
    printf("Measuring bandwidth to %s:%d...\n", host, port);
    
    // Send data and measure throughput
    memset(buffer, 'A', sizeof(buffer));
    gettimeofday(&start, NULL);
    
    time_t start_time = time(NULL);
    while (time(NULL) - start_time < transfer_duration) {
        int sent = send(sockfd, buffer, sizeof(buffer), 0);
        if (sent < 0) {
            perror("Send failed");
            break;
        }
        bytes_transferred += sent;
    }
    
    gettimeofday(&end, NULL);
    
    // Calculate bandwidth
    double elapsed = (end.tv_sec - start.tv_sec) + 
                    (end.tv_usec - start.tv_usec) / 1000000.0;
    double bits_transferred = bytes_transferred * 8.0;
    metrics->bandwidth_mbps = (bits_transferred / elapsed) / (1024.0 * 1024.0);
    
    printf("Transferred %ld bytes in %.2f seconds\n", bytes_transferred, elapsed);
    
    close(sockfd);
    return 0;
}

// Print metrics summary
void print_metrics(NetworkMetrics *metrics) {
    printf("\n=== Network Metrics Summary ===\n");
    printf("Latency:\n");
    printf("  Min: %.2f ms\n", metrics->min_latency);
    printf("  Avg: %.2f ms\n", metrics->avg_latency);
    printf("  Max: %.2f ms\n", metrics->max_latency);
    printf("Jitter: %.2f ms\n", metrics->jitter);
    printf("Packet Loss: %.2f%% (%d/%d packets)\n", 
           metrics->packet_loss_percent,
           metrics->packets_sent - metrics->packets_received,
           metrics->packets_sent);
    printf("Bandwidth: %.2f Mbps\n", metrics->bandwidth_mbps);
    printf("===============================\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <host_ip> [port]\n", argv[0]);
        printf("Note: Latency measurement requires root privileges\n");
        return 1;
    }
    
    NetworkMetrics metrics = {0};
    const char *host = argv[1];
    int port = argc > 2 ? atoi(argv[2]) : 80;
    
    // Measure latency and jitter
    if (measure_latency(host, &metrics) < 0) {
        printf("Warning: Latency measurement failed\n");
    }
    
    // Measure bandwidth (optional, requires a listening server)
    // measure_bandwidth(host, port, &metrics);
    
    print_metrics(&metrics);
    
    return 0;
}
```

### C++ Implementation with Modern Features

```cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

class NetworkMetricsCollector {
private:
    struct MetricsData {
        double min_latency = std::numeric_limits<double>::max();
        double max_latency = 0.0;
        double avg_latency = 0.0;
        double jitter = 0.0;
        int packets_sent = 0;
        int packets_received = 0;
        double packet_loss_percent = 0.0;
        double bandwidth_mbps = 0.0;
    };
    
    std::vector<double> latency_samples;
    MetricsData metrics;
    
public:
    // Measure latency using TCP connection time
    bool measureLatency(const std::string& host, int port, int samples = 10) {
        std::cout << "Measuring latency to " << host << ":" << port << "...\n";
        
        for (int i = 0; i < samples; i++) {
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) {
                std::cerr << "Socket creation failed\n";
                continue;
            }
            
            // Set timeout
            struct timeval timeout;
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            
            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
            
            metrics.packets_sent++;
            
            // Measure connection time
            auto start = std::chrono::high_resolution_clock::now();
            int result = connect(sockfd, (struct sockaddr*)&addr, sizeof(addr));
            auto end = std::chrono::high_resolution_clock::now();
            
            if (result == 0) {
                metrics.packets_received++;
                
                double latency = std::chrono::duration<double, std::milli>(end - start).count();
                latency_samples.push_back(latency);
                
                metrics.min_latency = std::min(metrics.min_latency, latency);
                metrics.max_latency = std::max(metrics.max_latency, latency);
                
                std::cout << "Sample " << i << ": " << latency << " ms\n";
            } else {
                std::cout << "Sample " << i << ": timeout\n";
            }
            
            close(sockfd);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (latency_samples.empty()) {
            return false;
        }
        
        calculateMetrics();
        return true;
    }
    
    // Measure bandwidth by sending data
    bool measureBandwidth(const std::string& host, int port, int duration_sec = 5) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Socket creation failed\n";
            return false;
        }
        
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        
        if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Connection failed\n";
            close(sockfd);
            return false;
        }
        
        std::cout << "Measuring bandwidth...\n";
        
        std::vector<char> buffer(65536, 'A');
        long long bytes_transferred = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        auto end_time = start + std::chrono::seconds(duration_sec);
        
        while (std::chrono::high_resolution_clock::now() < end_time) {
            ssize_t sent = send(sockfd, buffer.data(), buffer.size(), 0);
            if (sent < 0) {
                break;
            }
            bytes_transferred += sent;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        
        double bits_transferred = bytes_transferred * 8.0;
        metrics.bandwidth_mbps = (bits_transferred / elapsed) / (1024.0 * 1024.0);
        
        std::cout << "Transferred " << bytes_transferred << " bytes in " 
                  << elapsed << " seconds\n";
        
        close(sockfd);
        return true;
    }
    
    void printMetrics() const {
        std::cout << "\n=== Network Metrics Summary ===\n";
        std::cout << "Latency:\n";
        std::cout << "  Min: " << metrics.min_latency << " ms\n";
        std::cout << "  Avg: " << metrics.avg_latency << " ms\n";
        std::cout << "  Max: " << metrics.max_latency << " ms\n";
        std::cout << "Jitter: " << metrics.jitter << " ms\n";
        std::cout << "Packet Loss: " << metrics.packet_loss_percent << "% ("
                  << (metrics.packets_sent - metrics.packets_received) << "/"
                  << metrics.packets_sent << " packets)\n";
        std::cout << "Bandwidth: " << metrics.bandwidth_mbps << " Mbps\n";
        std::cout << "===============================\n";
    }
    
    const MetricsData& getMetrics() const { return metrics; }
    
private:
    void calculateMetrics() {
        if (latency_samples.empty()) return;
        
        // Average latency
        metrics.avg_latency = std::accumulate(latency_samples.begin(), 
                                             latency_samples.end(), 0.0) / 
                             latency_samples.size();
        
        // Jitter (average of consecutive differences)
        double jitter_sum = 0.0;
        for (size_t i = 1; i < latency_samples.size(); i++) {
            jitter_sum += std::abs(latency_samples[i] - latency_samples[i-1]);
        }
        metrics.jitter = latency_samples.size() > 1 ? 
                        jitter_sum / (latency_samples.size() - 1) : 0.0;
        
        // Packet loss
        metrics.packet_loss_percent = 
            ((double)(metrics.packets_sent - metrics.packets_received) / 
             metrics.packets_sent) * 100.0;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <host> <port>\n";
        return 1;
    }
    
    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    
    NetworkMetricsCollector collector;
    
    if (collector.measureLatency(host, port, 10)) {
        collector.printMetrics();
    } else {
        std::cerr << "Failed to measure latency\n";
    }
    
    // Optionally measure bandwidth
    // collector.measureBandwidth(host, port, 5);
    // collector.printMetrics();
    
    return 0;
}
```

### Rust Implementation

```rust
use std::net::{TcpStream, SocketAddr, IpAddr};
use std::time::{Duration, Instant};
use std::io::{Write, Read};
use std::thread;

#[derive(Debug, Clone)]
pub struct NetworkMetrics {
    pub min_latency: f64,
    pub max_latency: f64,
    pub avg_latency: f64,
    pub jitter: f64,
    pub packets_sent: usize,
    pub packets_received: usize,
    pub packet_loss_percent: f64,
    pub bandwidth_mbps: f64,
}

impl Default for NetworkMetrics {
    fn default() -> Self {
        NetworkMetrics {
            min_latency: f64::MAX,
            max_latency: 0.0,
            avg_latency: 0.0,
            jitter: 0.0,
            packets_sent: 0,
            packets_received: 0,
            packet_loss_percent: 0.0,
            bandwidth_mbps: 0.0,
        }
    }
}

pub struct NetworkMetricsCollector {
    latency_samples: Vec<f64>,
    metrics: NetworkMetrics,
}

impl NetworkMetricsCollector {
    pub fn new() -> Self {
        NetworkMetricsCollector {
            latency_samples: Vec::new(),
            metrics: NetworkMetrics::default(),
        }
    }
    
    /// Measure latency by connecting to a TCP endpoint
    pub fn measure_latency(&mut self, host: &str, port: u16, samples: usize) -> Result<(), Box<dyn std::error::Error>> {
        println!("Measuring latency to {}:{}...", host, port);
        
        let addr: SocketAddr = format!("{}:{}", host, port).parse()?;
        
        for i in 0..samples {
            self.metrics.packets_sent += 1;
            
            let start = Instant::now();
            let result = TcpStream::connect_timeout(&addr, Duration::from_secs(2));
            let elapsed = start.elapsed();
            
            match result {
                Ok(_stream) => {
                    self.metrics.packets_received += 1;
                    
                    let latency = elapsed.as_secs_f64() * 1000.0; // Convert to ms
                    self.latency_samples.push(latency);
                    
                    if latency < self.metrics.min_latency {
                        self.metrics.min_latency = latency;
                    }
                    if latency > self.metrics.max_latency {
                        self.metrics.max_latency = latency;
                    }
                    
                    println!("Sample {}: {:.2} ms", i, latency);
                }
                Err(e) => {
                    println!("Sample {}: timeout/error - {}", i, e);
                }
            }
            
            thread::sleep(Duration::from_millis(100));
        }
        
        if !self.latency_samples.is_empty() {
            self.calculate_metrics();
            Ok(())
        } else {
            Err("No successful samples".into())
        }
    }
    
    /// Measure bandwidth by sending data over TCP
    pub fn measure_bandwidth(&mut self, host: &str, port: u16, duration_secs: u64) -> Result<(), Box<dyn std::error::Error>> {
        println!("Measuring bandwidth to {}:{}...", host, port);
        
        let addr: SocketAddr = format!("{}:{}", host, port).parse()?;
        let mut stream = TcpStream::connect_timeout(&addr, Duration::from_secs(5))?;
        
        stream.set_write_timeout(Some(Duration::from_secs(1)))?;
        
        let buffer = vec![b'A'; 65536];
        let mut bytes_transferred: u64 = 0;
        
        let start = Instant::now();
        let end_time = start + Duration::from_secs(duration_secs);
        
        while Instant::now() < end_time {
            match stream.write(&buffer) {
                Ok(sent) => {
                    bytes_transferred += sent as u64;
                }
                Err(e) => {
                    eprintln!("Write error: {}", e);
                    break;
                }
            }
        }
        
        let elapsed = start.elapsed().as_secs_f64();
        
        let bits_transferred = bytes_transferred as f64 * 8.0;
        self.metrics.bandwidth_mbps = (bits_transferred / elapsed) / (1024.0 * 1024.0);
        
        println!("Transferred {} bytes in {:.2} seconds", bytes_transferred, elapsed);
        
        Ok(())
    }
    
    /// Measure packet loss by sending multiple probe packets
    pub fn measure_packet_loss(&mut self, host: &str, port: u16, count: usize) -> Result<(), Box<dyn std::error::Error>> {
        println!("Measuring packet loss to {}:{}...", host, port);
        
        let addr: SocketAddr = format!("{}:{}", host, port).parse()?;
        let mut successful = 0;
        
        for i in 0..count {
            match TcpStream::connect_timeout(&addr, Duration::from_secs(1)) {
                Ok(_) => {
                    successful += 1;
                    print!(".");
                }
                Err(_) => {
                    print!("X");
                }
            }
            
            if (i + 1) % 50 == 0 {
                println!();
            }
            
            thread::sleep(Duration::from_millis(50));
        }
        
        println!();
        
        self.metrics.packets_sent = count;
        self.metrics.packets_received = successful;
        self.metrics.packet_loss_percent = 
            ((count - successful) as f64 / count as f64) * 100.0;
        
        Ok(())
    }
    
    fn calculate_metrics(&mut self) {
        if self.latency_samples.is_empty() {
            return;
        }
        
        // Calculate average latency
        let sum: f64 = self.latency_samples.iter().sum();
        self.metrics.avg_latency = sum / self.latency_samples.len() as f64;
        
        // Calculate jitter (average of consecutive differences)
        let mut jitter_sum = 0.0;
        for i in 1..self.latency_samples.len() {
            jitter_sum += (self.latency_samples[i] - self.latency_samples[i - 1]).abs();
        }
        self.metrics.jitter = if self.latency_samples.len() > 1 {
            jitter_sum / (self.latency_samples.len() - 1) as f64
        } else {
            0.0
        };
        
        // Calculate packet loss percentage
        self.metrics.packet_loss_percent = 
            ((self.metrics.packets_sent - self.metrics.packets_received) as f64 / 
             self.metrics.packets_sent as f64) * 100.0;
    }
    
    pub fn get_metrics(&self) -> &NetworkMetrics {
        &self.metrics
    }
    
    pub fn print_metrics(&self) {
        println!("\n=== Network Metrics Summary ===");
        println!("Latency:");
        println!("  Min: {:.2} ms", self.metrics.min_latency);
        println!("  Avg: {:.2} ms", self.metrics.avg_latency);
        println!("  Max: {:.2} ms", self.metrics.max_latency);
        println!("Jitter: {:.2} ms", self.metrics.jitter);
        println!("Packet Loss: {:.2}% ({}/{} packets)", 
                 self.metrics.packet_loss_percent,
                 self.metrics.packets_sent - self.metrics.packets_received,
                 self.metrics.packets_sent);
        println!("Bandwidth: {:.2} Mbps", self.metrics.bandwidth_mbps);
        println!("===============================");
    }
}

// Example usage with real-time monitoring
pub struct RealtimeMonitor {
    collector: NetworkMetricsCollector,
    history: Vec<NetworkMetrics>,
}

impl RealtimeMonitor {
    pub fn new() -> Self {
        RealtimeMonitor {
            collector: NetworkMetricsCollector::new(),
            history: Vec::new(),
        }
    }
    
    pub fn monitor(&mut self, host: &str, port: u16, interval_secs: u64, iterations: usize) -> Result<(), Box<dyn std::error::Error>> {
        for i in 0..iterations {
            println!("\n--- Iteration {} ---", i + 1);
            
            self.collector = NetworkMetricsCollector::new();
            self.collector.measure_latency(host, port, 5)?;
            self.collector.print_metrics();
            
            self.history.push(self.collector.get_metrics().clone());
            
            if i < iterations - 1 {
                thread::sleep(Duration::from_secs(interval_secs));
            }
        }
        
        self.print_summary();
        Ok(())
    }
    
    fn print_summary(&self) {
        if self.history.is_empty() {
            return;
        }
        
        println!("\n=== Historical Summary ===");
        
        let avg_latencies: Vec<f64> = self.history.iter().map(|m| m.avg_latency).collect();
        let avg_of_avgs: f64 = avg_latencies.iter().sum::<f64>() / avg_latencies.len() as f64;
        
        let total_loss: f64 = self.history.iter().map(|m| m.packet_loss_percent).sum();
        let avg_loss = total_loss / self.history.len() as f64;
        
        println!("Overall Average Latency: {:.2} ms", avg_of_avgs);
        println!("Average Packet Loss: {:.2}%", avg_loss);
        println!("Total Measurements: {}", self.history.len());
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 3 {
        println!("Usage: {} <host> <port>", args[0]);
        println!("Example: {} 8.8.8.8 53", args[0]);
        return Ok(());
    }
    
    let host = &args[1];
    let port: u16 = args[2].parse()?;
    
    // Single measurement
    let mut collector = NetworkMetricsCollector::new();
    
    if let Err(e) = collector.measure_latency(host, port, 10) {
        eprintln!("Latency measurement failed: {}", e);
    } else {
        collector.print_metrics();
    }
    
    // Uncomment for continuous monitoring
    // let mut monitor = RealtimeMonitor::new();
    // monitor.monitor(host, port, 5, 3)?;
    
    Ok(())
}
```

## Summary

Network metrics collection involves measuring four critical performance indicators:

**Key Metrics:**
- **Bandwidth/Throughput**: Measures data transfer capacity; implemented by sending large amounts of data and timing the transfer
- **Latency**: Measures round-trip delay; typically measured using ICMP echo (ping) or TCP connection time
- **Packet Loss**: Percentage of packets that fail to arrive; calculated by comparing sent versus received packets
- **Jitter**: Variation in latency; computed as the average difference between consecutive latency measurements

**Implementation Approaches:**
- C provides low-level access with raw sockets for ICMP-based measurements (requires root)
- C++ offers modern abstractions with STL containers and chrono for precise timing
- Rust provides memory safety and excellent error handling through Result types

**Practical Applications:**
- Network performance monitoring and troubleshooting
- QoS validation for VoIP and video streaming services
- SLA compliance verification
- Capacity planning and network optimization
- Real-time alerting for degraded network conditions

These metrics form the foundation of network observability, enabling proactive management and ensuring optimal application performance across diverse network conditions.