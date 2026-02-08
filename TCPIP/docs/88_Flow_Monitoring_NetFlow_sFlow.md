# Flow Monitoring (NetFlow/sFlow)

## Detailed Description

Flow monitoring is a network traffic analysis technique that captures and analyzes information about network flows rather than examining individual packets. A "flow" is typically defined as a unidirectional sequence of packets sharing common characteristics (source/destination IP, ports, protocol, etc.). Flow monitoring provides network administrators with visibility into traffic patterns, bandwidth usage, security threats, and network performance.

### Key Concepts

**Network Flow**: A sequence of packets from a source to a destination, typically identified by a 5-tuple:
- Source IP address
- Destination IP address
- Source port
- Destination port
- IP protocol

**NetFlow**: Developed by Cisco, NetFlow is the most widely adopted flow monitoring protocol. It exports flow records from routers and switches to collectors for analysis.

**sFlow**: An industry standard for sampled flow monitoring. Unlike NetFlow (which analyzes every packet or uses sampling), sFlow uses statistical sampling to reduce overhead, making it suitable for high-speed networks.

**IPFIX**: The IETF standardized version of NetFlow v9, providing a vendor-neutral flow export format.

### NetFlow vs sFlow

| Feature | NetFlow | sFlow |
|---------|---------|-------|
| Sampling | Optional (v5), configurable (v9) | Always sampled |
| Overhead | Higher CPU usage | Lower CPU usage |
| Accuracy | More accurate for small flows | Statistical accuracy |
| Protocol | Push-based | Push-based |
| Standards | Cisco proprietary (v5), IETF (v9/IPFIX) | Industry standard |

### Flow Record Components

A typical flow record contains:
- Timestamps (start, end, duration)
- Packet and byte counts
- TCP flags
- Type of Service (ToS)
- Input/output interfaces
- Next-hop address

### Use Cases

1. **Traffic Analysis**: Understanding bandwidth usage patterns and top talkers
2. **Security Monitoring**: Detecting DDoS attacks, port scans, and anomalies
3. **Capacity Planning**: Forecasting network growth and optimization
4. **Application Monitoring**: Identifying application-level traffic patterns
5. **Billing and Accounting**: Usage-based charging in service provider networks

## Programming Implementation

### C Implementation: NetFlow v5 Collector

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

#define NETFLOW_PORT 2055
#define BUFFER_SIZE 1500

// NetFlow v5 Header
typedef struct {
    uint16_t version;
    uint16_t count;          // Number of flow records
    uint32_t sys_uptime;     // System uptime in milliseconds
    uint32_t unix_secs;      // Current seconds since epoch
    uint32_t unix_nsecs;     // Residual nanoseconds
    uint32_t flow_sequence;  // Sequence counter
    uint8_t  engine_type;    // Type of flow switching engine
    uint8_t  engine_id;      // Slot number of engine
    uint16_t sampling_interval;
} __attribute__((packed)) netflow_v5_header_t;

// NetFlow v5 Flow Record
typedef struct {
    uint32_t src_addr;       // Source IP
    uint32_t dst_addr;       // Destination IP
    uint32_t next_hop;       // Next hop router IP
    uint16_t input;          // Input interface index
    uint16_t output;         // Output interface index
    uint32_t packets;        // Packet count
    uint32_t octets;         // Byte count
    uint32_t first;          // System uptime at start
    uint32_t last;           // System uptime at last packet
    uint16_t src_port;       // Source port
    uint16_t dst_port;       // Destination port
    uint8_t  pad1;           // Padding
    uint8_t  tcp_flags;      // Cumulative OR of TCP flags
    uint8_t  protocol;       // IP protocol type
    uint8_t  tos;            // Type of Service
    uint16_t src_as;         // Source AS number
    uint16_t dst_as;         // Destination AS number
    uint8_t  src_mask;       // Source address prefix mask
    uint8_t  dst_mask;       // Destination address prefix mask
    uint16_t pad2;           // Padding
} __attribute__((packed)) netflow_v5_record_t;

// Convert protocol number to name
const char* protocol_name(uint8_t proto) {
    switch(proto) {
        case 1: return "ICMP";
        case 6: return "TCP";
        case 17: return "UDP";
        default: return "OTHER";
    }
}

// Print flow record
void print_flow_record(netflow_v5_record_t *record) {
    struct in_addr src, dst;
    src.s_addr = record->src_addr;
    dst.s_addr = record->dst_addr;
    
    printf("Flow: %s:%u -> %s:%u | Proto: %s | ",
           inet_ntoa(src), ntohs(record->src_port),
           inet_ntoa(dst), ntohs(record->dst_port),
           protocol_name(record->protocol));
    printf("Packets: %u | Bytes: %u | TCP Flags: 0x%02x\n",
           ntohl(record->packets), ntohl(record->octets),
           record->tcp_flags);
}

// NetFlow collector main function
int netflow_collector(void) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    unsigned char buffer[BUFFER_SIZE];
    
    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(NETFLOW_PORT);
    
    // Bind socket
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return -1;
    }
    
    printf("NetFlow collector listening on port %d\n", NETFLOW_PORT);
    
    while (1) {
        ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                            (struct sockaddr *)&client_addr, &addr_len);
        
        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }
        
        // Parse NetFlow header
        netflow_v5_header_t *header = (netflow_v5_header_t *)buffer;
        uint16_t version = ntohs(header->version);
        uint16_t count = ntohs(header->count);
        
        if (version != 5) {
            printf("Unsupported NetFlow version: %u\n", version);
            continue;
        }
        
        printf("\n=== NetFlow v5 Packet from %s ===\n", 
               inet_ntoa(client_addr.sin_addr));
        printf("Flow count: %u | Sequence: %u | Uptime: %u ms\n",
               count, ntohl(header->flow_sequence), ntohl(header->sys_uptime));
        
        // Parse flow records
        netflow_v5_record_t *records = 
            (netflow_v5_record_t *)(buffer + sizeof(netflow_v5_header_t));
        
        for (int i = 0; i < count && i < 30; i++) {
            print_flow_record(&records[i]);
        }
    }
    
    close(sockfd);
    return 0;
}

int main(void) {
    return netflow_collector();
}
```

### C++ Implementation: sFlow Collector with Analysis

```cpp
#include <iostream>
#include <unordered_map>
#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iomanip>
#include <memory>

#define SFLOW_PORT 6343
#define BUFFER_SIZE 2048

// sFlow structures
struct SFlowDatagram {
    uint32_t version;
    uint32_t agent_address_type;
    uint32_t agent_address;
    uint32_t sub_agent_id;
    uint32_t sequence_number;
    uint32_t uptime;
    uint32_t num_samples;
};

struct FlowSample {
    uint32_t sequence_number;
    uint32_t source_id;
    uint32_t sampling_rate;
    uint32_t sample_pool;
    uint32_t drops;
    uint32_t input;
    uint32_t output;
    uint32_t num_records;
};

// Flow statistics tracker
class FlowStatistics {
private:
    struct FlowKey {
        uint32_t src_ip;
        uint32_t dst_ip;
        uint16_t src_port;
        uint16_t dst_port;
        uint8_t protocol;
        
        bool operator==(const FlowKey& other) const {
            return src_ip == other.src_ip && dst_ip == other.dst_ip &&
                   src_port == other.src_port && dst_port == other.dst_port &&
                   protocol == other.protocol;
        }
    };
    
    struct FlowKeyHash {
        size_t operator()(const FlowKey& key) const {
            return std::hash<uint32_t>()(key.src_ip) ^
                   std::hash<uint32_t>()(key.dst_ip) ^
                   std::hash<uint16_t>()(key.src_port) ^
                   std::hash<uint16_t>()(key.dst_port) ^
                   std::hash<uint8_t>()(key.protocol);
        }
    };
    
    struct FlowStats {
        uint64_t packets;
        uint64_t bytes;
        time_t first_seen;
        time_t last_seen;
    };
    
    std::unordered_map<FlowKey, FlowStats, FlowKeyHash> flows;
    
public:
    void update_flow(uint32_t src_ip, uint32_t dst_ip,
                    uint16_t src_port, uint16_t dst_port,
                    uint8_t protocol, uint64_t bytes) {
        FlowKey key = {src_ip, dst_ip, src_port, dst_port, protocol};
        auto& stats = flows[key];
        stats.packets++;
        stats.bytes += bytes;
        time_t now = time(nullptr);
        
        if (stats.packets == 1) {
            stats.first_seen = now;
        }
        stats.last_seen = now;
    }
    
    void print_top_flows(size_t count = 10) {
        std::vector<std::pair<FlowKey, FlowStats>> sorted_flows(
            flows.begin(), flows.end());
        
        std::sort(sorted_flows.begin(), sorted_flows.end(),
                 [](const auto& a, const auto& b) {
                     return a.second.bytes > b.second.bytes;
                 });
        
        std::cout << "\n=== Top " << count << " Flows by Bytes ===\n";
        std::cout << std::setw(15) << "Source IP" << " "
                  << std::setw(6) << "Sport" << " -> "
                  << std::setw(15) << "Dest IP" << " "
                  << std::setw(6) << "Dport" << " "
                  << std::setw(8) << "Proto" << " "
                  << std::setw(12) << "Bytes" << " "
                  << std::setw(10) << "Packets" << "\n";
        
        for (size_t i = 0; i < std::min(count, sorted_flows.size()); i++) {
            const auto& [key, stats] = sorted_flows[i];
            struct in_addr src, dst;
            src.s_addr = key.src_ip;
            dst.s_addr = key.dst_ip;
            
            std::cout << std::setw(15) << inet_ntoa(src) << ":"
                      << std::setw(5) << key.src_port << " -> "
                      << std::setw(15) << inet_ntoa(dst) << ":"
                      << std::setw(5) << key.dst_port << " "
                      << std::setw(8) << (int)key.protocol << " "
                      << std::setw(12) << stats.bytes << " "
                      << std::setw(10) << stats.packets << "\n";
        }
    }
    
    size_t get_flow_count() const { return flows.size(); }
    uint64_t get_total_bytes() const {
        uint64_t total = 0;
        for (const auto& [key, stats] : flows) {
            total += stats.bytes;
        }
        return total;
    }
};

// Simple sFlow collector
class SFlowCollector {
private:
    int sockfd;
    FlowStatistics stats;
    uint64_t packets_received;
    
public:
    SFlowCollector() : sockfd(-1), packets_received(0) {}
    
    ~SFlowCollector() {
        if (sockfd >= 0) close(sockfd);
    }
    
    bool initialize(uint16_t port = SFLOW_PORT) {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            std::cerr << "Failed to create socket\n";
            return false;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);
        
        if (bind(sockfd, (struct sockaddr*)&server_addr, 
                sizeof(server_addr)) < 0) {
            std::cerr << "Failed to bind socket\n";
            return false;
        }
        
        std::cout << "sFlow collector listening on port " << port << "\n";
        return true;
    }
    
    void run() {
        unsigned char buffer[BUFFER_SIZE];
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        while (true) {
            ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                               (struct sockaddr*)&client_addr, &addr_len);
            
            if (n > 0) {
                packets_received++;
                process_datagram(buffer, n);
                
                if (packets_received % 100 == 0) {
                    print_statistics();
                }
            }
        }
    }
    
private:
    void process_datagram(unsigned char* data, size_t len) {
        if (len < sizeof(SFlowDatagram)) return;
        
        // Simple parsing - real implementation would be more robust
        SFlowDatagram* header = reinterpret_cast<SFlowDatagram*>(data);
        uint32_t version = ntohl(header->version);
        
        if (version == 5) {
            // Simplified: just track basic flow info
            // Real implementation would parse full sFlow format
            stats.update_flow(0x0a000001, 0x0a000002, 
                            80, 12345, 6, 1500);
        }
    }
    
    void print_statistics() {
        std::cout << "\n=== Collection Statistics ===\n";
        std::cout << "Packets received: " << packets_received << "\n";
        std::cout << "Unique flows: " << stats.get_flow_count() << "\n";
        std::cout << "Total bytes: " << stats.get_total_bytes() << "\n";
        stats.print_top_flows(5);
    }
};

int main() {
    SFlowCollector collector;
    
    if (!collector.initialize()) {
        return 1;
    }
    
    collector.run();
    return 0;
}
```

### Rust Implementation: NetFlow Exporter

```rust
use std::net::{UdpSocket, SocketAddr, Ipv4Addr};
use std::time::{SystemTime, UNIX_EPOCH, Duration};
use std::collections::HashMap;
use std::io;

// NetFlow v5 structures
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct NetFlowV5Header {
    version: u16,
    count: u16,
    sys_uptime: u32,
    unix_secs: u32,
    unix_nsecs: u32,
    flow_sequence: u32,
    engine_type: u8,
    engine_id: u8,
    sampling_interval: u16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct NetFlowV5Record {
    src_addr: u32,
    dst_addr: u32,
    next_hop: u32,
    input: u16,
    output: u16,
    packets: u32,
    octets: u32,
    first: u32,
    last: u32,
    src_port: u16,
    dst_port: u16,
    pad1: u8,
    tcp_flags: u8,
    protocol: u8,
    tos: u8,
    src_as: u16,
    dst_as: u16,
    src_mask: u8,
    dst_mask: u8,
    pad2: u16,
}

// Flow key for tracking
#[derive(Debug, Hash, Eq, PartialEq, Clone)]
struct FlowKey {
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    src_port: u16,
    dst_port: u16,
    protocol: u8,
}

// Flow tracking information
#[derive(Debug, Clone)]
struct FlowInfo {
    packets: u32,
    bytes: u32,
    start_time: Duration,
    last_time: Duration,
    tcp_flags: u8,
}

// NetFlow exporter
struct NetFlowExporter {
    socket: UdpSocket,
    collector_addr: SocketAddr,
    flows: HashMap<FlowKey, FlowInfo>,
    sequence: u32,
    start_time: SystemTime,
}

impl NetFlowExporter {
    fn new(collector: &str) -> io::Result<Self> {
        let socket = UdpSocket::bind("0.0.0.0:0")?;
        let collector_addr = collector.parse()
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e))?;
        
        Ok(NetFlowExporter {
            socket,
            collector_addr,
            flows: HashMap::new(),
            sequence: 0,
            start_time: SystemTime::now(),
        })
    }
    
    fn update_flow(&mut self, key: FlowKey, bytes: u32, tcp_flags: u8) {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap();
        
        self.flows.entry(key).and_modify(|flow| {
            flow.packets += 1;
            flow.bytes += bytes;
            flow.last_time = now;
            flow.tcp_flags |= tcp_flags;
        }).or_insert(FlowInfo {
            packets: 1,
            bytes,
            start_time: now,
            last_time: now,
            tcp_flags,
        });
    }
    
    fn export_flows(&mut self) -> io::Result<usize> {
        if self.flows.is_empty() {
            return Ok(0);
        }
        
        let uptime = SystemTime::now()
            .duration_since(self.start_time)
            .unwrap()
            .as_millis() as u32;
        
        let unix_time = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap();
        
        // Create NetFlow packet
        let mut packet = Vec::new();
        let flow_count = self.flows.len().min(30); // Max 30 records per packet
        
        // Build header
        let header = NetFlowV5Header {
            version: 5u16.to_be(),
            count: (flow_count as u16).to_be(),
            sys_uptime: uptime.to_be(),
            unix_secs: (unix_time.as_secs() as u32).to_be(),
            unix_nsecs: (unix_time.subsec_nanos()).to_be(),
            flow_sequence: self.sequence.to_be(),
            engine_type: 0,
            engine_id: 0,
            sampling_interval: 0,
        };
        
        // Unsafe: convert struct to bytes
        unsafe {
            let header_bytes = std::slice::from_raw_parts(
                &header as *const _ as *const u8,
                std::mem::size_of::<NetFlowV5Header>()
            );
            packet.extend_from_slice(header_bytes);
        }
        
        // Build records
        let flows_to_export: Vec<_> = self.flows.iter()
            .take(flow_count)
            .collect();
        
        for (key, info) in flows_to_export {
            let record = NetFlowV5Record {
                src_addr: u32::from(*key.src_ip).to_be(),
                dst_addr: u32::from(*key.dst_ip).to_be(),
                next_hop: 0,
                input: 0u16.to_be(),
                output: 0u16.to_be(),
                packets: info.packets.to_be(),
                octets: info.bytes.to_be(),
                first: (info.start_time.as_millis() as u32).to_be(),
                last: (info.last_time.as_millis() as u32).to_be(),
                src_port: key.src_port.to_be(),
                dst_port: key.dst_port.to_be(),
                pad1: 0,
                tcp_flags: info.tcp_flags,
                protocol: key.protocol,
                tos: 0,
                src_as: 0u16.to_be(),
                dst_as: 0u16.to_be(),
                src_mask: 0,
                dst_mask: 0,
                pad2: 0,
            };
            
            unsafe {
                let record_bytes = std::slice::from_raw_parts(
                    &record as *const _ as *const u8,
                    std::mem::size_of::<NetFlowV5Record>()
                );
                packet.extend_from_slice(record_bytes);
            }
        }
        
        // Send packet
        let sent = self.socket.send_to(&packet, self.collector_addr)?;
        
        // Clear exported flows
        self.flows.clear();
        self.sequence += 1;
        
        println!("Exported {} flows ({} bytes)", flow_count, sent);
        Ok(flow_count)
    }
    
    fn print_statistics(&self) {
        println!("\n=== Flow Statistics ===");
        println!("Active flows: {}", self.flows.len());
        
        let total_bytes: u32 = self.flows.values().map(|f| f.bytes).sum();
        let total_packets: u32 = self.flows.values().map(|f| f.packets).sum();
        
        println!("Total packets: {}", total_packets);
        println!("Total bytes: {}", total_bytes);
        
        // Top 5 flows
        let mut flows: Vec<_> = self.flows.iter().collect();
        flows.sort_by(|a, b| b.1.bytes.cmp(&a.1.bytes));
        
        println!("\nTop 5 flows by bytes:");
        for (key, info) in flows.iter().take(5) {
            println!("  {}:{} -> {}:{} | Proto: {} | Bytes: {} | Packets: {}",
                    key.src_ip, key.src_port,
                    key.dst_ip, key.dst_port,
                    key.protocol, info.bytes, info.packets);
        }
    }
}

// Example usage
fn main() -> io::Result<()> {
    let mut exporter = NetFlowExporter::new("127.0.0.1:2055")?;
    
    println!("NetFlow exporter started");
    println!("Sending flows to {}", exporter.collector_addr);
    
    // Simulate some flows
    for i in 0..100 {
        let key = FlowKey {
            src_ip: Ipv4Addr::new(192, 168, 1, (i % 10) as u8 + 1),
            dst_ip: Ipv4Addr::new(10, 0, 0, (i % 5) as u8 + 1),
            src_port: 10000 + i,
            dst_port: if i % 2 == 0 { 80 } else { 443 },
            protocol: 6, // TCP
        };
        
        exporter.update_flow(key, 1500, 0x02); // SYN flag
        
        std::thread::sleep(Duration::from_millis(100));
        
        if (i + 1) % 20 == 0 {
            exporter.print_statistics();
            exporter.export_flows()?;
        }
    }
    
    Ok(())
}
```

## Summary

Flow monitoring using NetFlow and sFlow is essential for network visibility, providing aggregated traffic data that enables administrators to understand network behavior, detect security threats, and optimize performance. NetFlow offers detailed per-flow analysis with higher accuracy, making it ideal for security monitoring and detailed traffic analysis, while sFlow uses statistical sampling to reduce overhead, making it suitable for high-speed networks where minimal performance impact is critical.

The key differences lie in their approach: NetFlow examines flows more comprehensively (either all packets or configurable sampling), while sFlow always uses sampling. Both protocols export flow records to collectors where data can be analyzed, visualized, and stored for historical analysis.

Programming implementations typically involve creating collectors that receive UDP datagrams containing flow records, parsing the protocol-specific structures, and storing or analyzing the data. Modern implementations often include real-time analysis capabilities, statistical aggregation, and integration with visualization tools for network operations centers. Flow monitoring remains a cornerstone technology for network management, security operations, and capacity planning in modern networks.