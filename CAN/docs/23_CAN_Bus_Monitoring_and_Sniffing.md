# CAN Bus Monitoring and Sniffing

## Overview

CAN (Controller Area Network) bus monitoring and sniffing involves passively observing and analyzing network traffic without actively participating in communication. This is essential for debugging, reverse engineering, security analysis, and understanding vehicle or industrial system behavior.

## Core Concepts

### Passive Observation
Unlike active CAN communication, monitoring involves:
- **Non-intrusive listening**: Reading frames without transmitting
- **Traffic capture**: Recording all bus activity for analysis
- **Protocol decoding**: Interpreting raw frames into meaningful data
- **Pattern recognition**: Identifying periodic messages, state changes, and anomalies

### Key Monitoring Capabilities
- **Frame capture**: Recording CAN IDs, data payloads, timestamps
- **Statistical analysis**: Message frequency, bus load, error rates
- **Filtering**: Isolating specific CAN IDs or data patterns
- **Triggering**: Capturing data based on specific conditions
- **Replay**: Recording and retransmitting captured traffic

## Hardware Requirements

### CAN Interface Options
1. **USB-to-CAN adapters** (PEAK PCAN-USB, Kvaser, CANable)
2. **Embedded boards** (Raspberry Pi with MCP2515, Arduino with CAN shield)
3. **Professional tools** (Vector CANalyzer, Intrepid neoVI)
4. **OBD-II dongles** for automotive applications

### Connection Considerations
- Proper termination (120Ω resistors at bus ends)
- Correct baud rate detection (common: 125k, 250k, 500k, 1Mbps)
- Electrical isolation for safety
- Dual-channel monitoring for multi-bus systems

## Programming Examples

### C/C++ Example: Linux SocketCAN Sniffer

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <time.h>

#define CAN_INTERFACE "can0"

typedef struct {
    __u32 can_id;
    unsigned int count;
    struct timespec last_seen;
} CanIdStats;

// Calculate time difference in milliseconds
double time_diff_ms(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000.0 + 
           (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

int main(void) {
    int sock;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;
    
    // Statistics tracking
    CanIdStats stats[2048] = {0};  // Max CAN IDs
    int stats_count = 0;
    
    // Create socket
    if ((sock = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Specify CAN interface
    strcpy(ifr.ifr_name, CAN_INTERFACE);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    
    // Bind socket to CAN interface
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    printf("CAN Bus Monitor started on %s\n", CAN_INTERFACE);
    printf("ID       DLC  Data                          Count  Interval(ms)\n");
    printf("---------------------------------------------------------------\n");
    
    // Main monitoring loop
    while (1) {
        ssize_t nbytes = read(sock, &frame, sizeof(struct can_frame));
        
        if (nbytes < 0) {
            perror("Read failed");
            continue;
        }
        
        if (nbytes < sizeof(struct can_frame)) {
            fprintf(stderr, "Incomplete CAN frame\n");
            continue;
        }
        
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        
        // Find or create statistics entry
        int stat_idx = -1;
        for (int i = 0; i < stats_count; i++) {
            if (stats[i].can_id == frame.can_id) {
                stat_idx = i;
                break;
            }
        }
        
        if (stat_idx == -1 && stats_count < 2048) {
            stat_idx = stats_count++;
            stats[stat_idx].can_id = frame.can_id;
            stats[stat_idx].count = 0;
            stats[stat_idx].last_seen = current_time;
        }
        
        // Calculate interval
        double interval = 0.0;
        if (stat_idx >= 0 && stats[stat_idx].count > 0) {
            interval = time_diff_ms(&stats[stat_idx].last_seen, &current_time);
            stats[stat_idx].last_seen = current_time;
            stats[stat_idx].count++;
        } else if (stat_idx >= 0) {
            stats[stat_idx].count = 1;
        }
        
        // Print frame details
        printf("0x%03X    %d    ", frame.can_id, frame.can_dlc);
        for (int i = 0; i < frame.can_dlc; i++) {
            printf("%02X ", frame.data[i]);
        }
        for (int i = frame.can_dlc; i < 8; i++) {
            printf("   ");
        }
        
        if (stat_idx >= 0) {
            printf(" %5d  %8.2f\n", stats[stat_idx].count, interval);
        } else {
            printf("\n");
        }
    }
    
    close(sock);
    return 0;
}
```

### C++ Example: Advanced Frame Filtering

```cpp
#include <iostream>
#include <vector>
#include <map>
#include <chrono>
#include <iomanip>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>

class CANMonitor {
private:
    int socket_fd;
    std::string interface_name;
    
    struct FrameStats {
        uint32_t count;
        std::chrono::steady_clock::time_point last_seen;
        std::vector<uint8_t> last_data;
        double avg_interval_ms;
    };
    
    std::map<uint32_t, FrameStats> statistics;
    
    // Filter configuration
    std::vector<uint32_t> id_whitelist;
    std::vector<uint32_t> id_blacklist;
    bool use_whitelist = false;
    bool use_blacklist = false;
    
public:
    CANMonitor(const std::string& iface) : interface_name(iface) {
        socket_fd = -1;
    }
    
    ~CANMonitor() {
        if (socket_fd >= 0) {
            close(socket_fd);
        }
    }
    
    bool initialize() {
        socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (socket_fd < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        struct ifreq ifr;
        strcpy(ifr.ifr_name, interface_name.c_str());
        ioctl(socket_fd, SIOCGIFINDEX, &ifr);
        
        struct sockaddr_can addr = {};
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        
        if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Failed to bind socket" << std::endl;
            return false;
        }
        
        // Set receive timeout
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        
        return true;
    }
    
    void setWhitelist(const std::vector<uint32_t>& ids) {
        id_whitelist = ids;
        use_whitelist = !ids.empty();
    }
    
    void setBlacklist(const std::vector<uint32_t>& ids) {
        id_blacklist = ids;
        use_blacklist = !ids.empty();
    }
    
    bool shouldProcess(uint32_t can_id) {
        if (use_whitelist) {
            return std::find(id_whitelist.begin(), id_whitelist.end(), can_id) 
                   != id_whitelist.end();
        }
        if (use_blacklist) {
            return std::find(id_blacklist.begin(), id_blacklist.end(), can_id) 
                   == id_blacklist.end();
        }
        return true;
    }
    
    void processFrame(const can_frame& frame) {
        auto now = std::chrono::steady_clock::now();
        auto& stats = statistics[frame.can_id];
        
        if (stats.count > 0) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - stats.last_seen).count();
            
            // Running average of interval
            stats.avg_interval_ms = (stats.avg_interval_ms * (stats.count - 1) 
                                    + duration) / stats.count;
        }
        
        stats.count++;
        stats.last_seen = now;
        stats.last_data.assign(frame.data, frame.data + frame.can_dlc);
    }
    
    void displayFrame(const can_frame& frame) {
        std::cout << "0x" << std::hex << std::setw(3) << std::setfill('0') 
                  << frame.can_id << std::dec << "  [" << (int)frame.can_dlc << "]  ";
        
        for (int i = 0; i < frame.can_dlc; i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << (int)frame.data[i] << " ";
        }
        
        auto& stats = statistics[frame.can_id];
        std::cout << "  Count: " << std::dec << stats.count;
        if (stats.count > 1) {
            std::cout << "  Avg: " << std::fixed << std::setprecision(2) 
                      << stats.avg_interval_ms << "ms";
        }
        std::cout << std::endl;
    }
    
    void monitor(int duration_seconds = 0) {
        auto start_time = std::chrono::steady_clock::now();
        
        std::cout << "Monitoring CAN bus on " << interface_name << std::endl;
        std::cout << "Press Ctrl+C to stop..." << std::endl << std::endl;
        
        while (true) {
            can_frame frame;
            ssize_t nbytes = read(socket_fd, &frame, sizeof(frame));
            
            if (nbytes > 0) {
                if (shouldProcess(frame.can_id)) {
                    processFrame(frame);
                    displayFrame(frame);
                }
            }
            
            if (duration_seconds > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                if (elapsed >= duration_seconds) break;
            }
        }
    }
    
    void printStatistics() {
        std::cout << "\n=== CAN Bus Statistics ===" << std::endl;
        std::cout << "Total unique IDs: " << statistics.size() << std::endl;
        std::cout << "\nID      Count    Avg Interval" << std::endl;
        std::cout << "--------------------------------" << std::endl;
        
        for (const auto& [id, stats] : statistics) {
            std::cout << "0x" << std::hex << std::setw(3) << std::setfill('0') 
                      << id << std::dec << "  " << std::setw(6) << stats.count;
            if (stats.count > 1) {
                std::cout << "  " << std::fixed << std::setprecision(2) 
                          << stats.avg_interval_ms << "ms";
            }
            std::cout << std::endl;
        }
    }
};

int main() {
    CANMonitor monitor("can0");
    
    if (!monitor.initialize()) {
        return 1;
    }
    
    // Example: Monitor only specific IDs
    monitor.setWhitelist({0x100, 0x200, 0x300});
    
    monitor.monitor(30);  // Monitor for 30 seconds
    monitor.printStatistics();
    
    return 0;
}
```

### Rust Example: Asynchronous CAN Monitoring

```rust
use socketcan::{CanSocket, Socket, CanFrame};
use std::collections::HashMap;
use std::time::{Duration, Instant};
use std::io;

#[derive(Debug, Clone)]
struct FrameStatistics {
    count: u64,
    last_seen: Instant,
    last_data: Vec<u8>,
    intervals: Vec<Duration>,
    min_interval: Option<Duration>,
    max_interval: Option<Duration>,
}

impl FrameStatistics {
    fn new() -> Self {
        FrameStatistics {
            count: 0,
            last_seen: Instant::now(),
            last_data: Vec::new(),
            intervals: Vec::new(),
            min_interval: None,
            max_interval: None,
        }
    }
    
    fn update(&mut self, frame_data: &[u8]) {
        let now = Instant::now();
        
        if self.count > 0 {
            let interval = now.duration_since(self.last_seen);
            self.intervals.push(interval);
            
            self.min_interval = Some(
                self.min_interval.map_or(interval, |min| min.min(interval))
            );
            self.max_interval = Some(
                self.max_interval.map_or(interval, |max| max.max(interval))
            );
        }
        
        self.count += 1;
        self.last_seen = now;
        self.last_data = frame_data.to_vec();
    }
    
    fn average_interval(&self) -> Option<Duration> {
        if self.intervals.is_empty() {
            return None;
        }
        
        let total: Duration = self.intervals.iter().sum();
        Some(total / self.intervals.len() as u32)
    }
}

struct CanMonitor {
    socket: CanSocket,
    statistics: HashMap<u32, FrameStatistics>,
    filter_ids: Option<Vec<u32>>,
    verbose: bool,
}

impl CanMonitor {
    fn new(interface: &str) -> io::Result<Self> {
        let socket = CanSocket::open(interface)?;
        
        // Set non-blocking mode
        socket.set_nonblocking(false)?;
        
        Ok(CanMonitor {
            socket,
            statistics: HashMap::new(),
            filter_ids: None,
            verbose: true,
        })
    }
    
    fn set_filter(&mut self, ids: Vec<u32>) {
        self.filter_ids = Some(ids);
    }
    
    fn should_process(&self, can_id: u32) -> bool {
        match &self.filter_ids {
            Some(filters) => filters.contains(&can_id),
            None => true,
        }
    }
    
    fn process_frame(&mut self, frame: &CanFrame) {
        let can_id = frame.id();
        
        if !self.should_process(can_id) {
            return;
        }
        
        let stats = self.statistics
            .entry(can_id)
            .or_insert_with(FrameStatistics::new);
        
        stats.update(frame.data());
        
        if self.verbose {
            self.display_frame(frame, stats);
        }
    }
    
    fn display_frame(&self, frame: &CanFrame, stats: &FrameStatistics) {
        print!("0x{:03X}  [{:1}]  ", frame.id(), frame.data().len());
        
        for byte in frame.data() {
            print!("{:02X} ", byte);
        }
        
        // Pad to 8 bytes width
        for _ in frame.data().len()..8 {
            print!("   ");
        }
        
        print!(" Count: {:5}", stats.count);
        
        if let Some(avg) = stats.average_interval() {
            print!("  Avg: {:6.2}ms", avg.as_secs_f64() * 1000.0);
        }
        
        if let Some(min) = stats.min_interval {
            print!("  Min: {:6.2}ms", min.as_secs_f64() * 1000.0);
        }
        
        println!();
    }
    
    fn monitor(&mut self, duration: Option<Duration>) -> io::Result<()> {
        println!("CAN Bus Monitor started");
        println!("ID     DLC  Data                       Count  Avg(ms)  Min(ms)");
        println!("--------------------------------------------------------------------");
        
        let start_time = Instant::now();
        
        loop {
            match self.socket.read_frame() {
                Ok(frame) => {
                    self.process_frame(&frame);
                }
                Err(e) => {
                    eprintln!("Error reading frame: {}", e);
                    continue;
                }
            }
            
            // Check duration limit
            if let Some(dur) = duration {
                if start_time.elapsed() >= dur {
                    break;
                }
            }
        }
        
        Ok(())
    }
    
    fn print_statistics(&self) {
        println!("\n=== CAN Bus Statistics ===");
        println!("Total unique IDs: {}", self.statistics.len());
        println!("\nID      Count    Avg(ms)   Min(ms)   Max(ms)   Last Data");
        println!("---------------------------------------------------------------");
        
        let mut sorted_stats: Vec<_> = self.statistics.iter().collect();
        sorted_stats.sort_by_key(|(id, _)| *id);
        
        for (id, stats) in sorted_stats {
            print!("0x{:03X}  {:6}  ", id, stats.count);
            
            if let Some(avg) = stats.average_interval() {
                print!("{:8.2}  ", avg.as_secs_f64() * 1000.0);
            } else {
                print!("    -     ");
            }
            
            if let Some(min) = stats.min_interval {
                print!("{:8.2}  ", min.as_secs_f64() * 1000.0);
            } else {
                print!("    -     ");
            }
            
            if let Some(max) = stats.max_interval {
                print!("{:8.2}  ", max.as_secs_f64() * 1000.0);
            } else {
                print!("    -     ");
            }
            
            // Print last data
            for byte in &stats.last_data {
                print!("{:02X} ", byte);
            }
            
            println!();
        }
    }
    
    fn detect_anomalies(&self) {
        println!("\n=== Anomaly Detection ===");
        
        for (id, stats) in &self.statistics {
            if stats.count < 10 {
                continue;  // Need sufficient data
            }
            
            if let (Some(avg), Some(min), Some(max)) = 
                (stats.average_interval(), stats.min_interval, stats.max_interval) {
                
                let variance_ratio = max.as_secs_f64() / min.as_secs_f64();
                
                if variance_ratio > 10.0 {
                    println!("0x{:03X}: High timing variance (ratio: {:.1})", 
                             id, variance_ratio);
                }
                
                // Detect sudden stops (no recent activity)
                let since_last = stats.last_seen.elapsed();
                if since_last > avg * 5 {
                    println!("0x{:03X}: No activity for {:.1}ms (expected ~{:.1}ms)", 
                             id, 
                             since_last.as_secs_f64() * 1000.0,
                             avg.as_secs_f64() * 1000.0);
                }
            }
        }
    }
}

fn main() -> io::Result<()> {
    let mut monitor = CanMonitor::new("can0")?;
    
    // Optional: Filter specific IDs
    // monitor.set_filter(vec![0x100, 0x200, 0x300]);
    
    // Monitor for 60 seconds
    monitor.monitor(Some(Duration::from_secs(60)))?;
    
    // Display statistics
    monitor.print_statistics();
    monitor.detect_anomalies();
    
    Ok(())
}
```

## Analysis Techniques

### Traffic Pattern Analysis
- **Periodic messages**: Identify regular broadcasts (e.g., sensor data every 100ms)
- **Event-driven messages**: Detect state changes (button presses, error conditions)
- **Response patterns**: Map request-response pairs
- **Bus load calculation**: Measure bandwidth utilization

### Reverse Engineering
- **ID mapping**: Correlate CAN IDs with vehicle/system functions
- **Data interpretation**: Decode signal values (speed, temperature, etc.)
- **State machines**: Understand protocol sequences
- **Checksums/counters**: Identify validation mechanisms

### Security Analysis
- **Replay attack detection**: Identify repeated message sequences
- **Fuzzing preparation**: Capture baseline traffic before injection
- **Authentication bypass**: Look for unprotected critical messages
- **Rate limiting**: Observe normal message frequencies

## Tools and Software

### Open Source
- **can-utils** (Linux command-line tools)
- **Wireshark** with SocketCAN plugin
- **SavvyCAN** (cross-platform analyzer)
- **CANgaroo** (visualization tool)

### Commercial
- **Vector CANalyzer/CANoe**
- **Intrepid Vehicle Spy**
- **PEAK PCAN-View**

## Summary

CAN bus monitoring and sniffing is a fundamental skill for automotive security researchers, embedded systems engineers, and industrial automation specialists. By passively observing network traffic, you can understand system behavior, identify communication patterns, and detect anomalies without disrupting operations.

**Key capabilities include:**
- Non-intrusive traffic capture and analysis
- Statistical profiling of message patterns and timing
- Protocol reverse engineering and signal decoding
- Security vulnerability assessment and anomaly detection

**The provided code examples demonstrate:**
- **C/Linux SocketCAN**: Low-level frame capture with interval tracking
- **C++ OOP approach**: Advanced filtering, statistics, and pattern analysis
- **Rust async monitoring**: Type-safe implementation with comprehensive metrics

**Best practices:**
- Always use proper electrical isolation when connecting to live systems
- Respect legal and ethical boundaries (only analyze systems you own or have authorization to test)
- Document discovered CAN IDs and their functions for future reference
- Combine passive monitoring with active testing for complete security assessments

Effective CAN monitoring enables deep system understanding, making it invaluable for debugging, optimization, security research, and reverse engineering automotive and industrial control systems.