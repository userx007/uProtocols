# Network Debugging Tools

Network debugging tools are essential for diagnosing connectivity issues, analyzing traffic patterns, detecting security problems, and understanding network behavior. These tools operate at different layers of the network stack and provide various perspectives on network activity.

## Core Tools Overview

### tcpdump
A command-line packet analyzer that captures and displays network packets in real-time. It uses libpcap to capture packets and supports Berkeley Packet Filter (BPF) syntax for filtering.

**Key capabilities:**
- Captures packets at the data link layer
- Powerful filtering with BPF expressions
- Can write captures to pcap files for later analysis
- Minimal overhead, suitable for production environments

### Wireshark
A graphical network protocol analyzer that provides deep packet inspection with a user-friendly interface. It can dissect hundreds of protocols and is the GUI counterpart to tshark (terminal Wireshark).

**Key capabilities:**
- Protocol dissection and analysis
- Follow TCP streams
- Export data in various formats
- Powerful display filters

### netstat
A legacy tool (being replaced by `ss`) that displays network connections, routing tables, interface statistics, and protocol statistics.

**Key capabilities:**
- Shows active connections
- Displays listening ports
- Shows routing tables
- Provides protocol statistics

### ss (Socket Statistics)
The modern replacement for netstat, faster and more feature-rich. It queries the kernel's TCP stack directly rather than reading /proc files.

**Key capabilities:**
- Faster than netstat for large connection counts
- Shows more detailed socket information
- Supports filtering with expressions
- Shows internal TCP state information

### strace
A system call tracer that monitors interactions between processes and the kernel, including network-related system calls.

**Key capabilities:**
- Traces system calls (socket, connect, send, recv, etc.)
- Shows file descriptor operations
- Reveals timing information
- Helps debug application-level network issues

## C/C++ Examples

### Using libpcap (tcpdump's library)

```c
#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/if_ether.h>

// Packet handler callback
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, 
                   const u_char *packet) {
    struct ether_header *eth_header;
    struct ip *ip_header;
    struct tcphdr *tcp_header;
    
    printf("\n=== Packet captured ===\n");
    printf("Packet length: %d bytes\n", pkthdr->len);
    printf("Capture length: %d bytes\n", pkthdr->caplen);
    
    // Parse Ethernet header
    eth_header = (struct ether_header *)packet;
    
    // Check if it's an IP packet
    if (ntohs(eth_header->ether_type) == ETHERTYPE_IP) {
        ip_header = (struct ip *)(packet + sizeof(struct ether_header));
        
        char src_ip[INET_ADDRSTRLEN];
        char dst_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ip_header->ip_src), src_ip, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &(ip_header->ip_dst), dst_ip, INET_ADDRSTRLEN);
        
        printf("IP: %s -> %s\n", src_ip, dst_ip);
        printf("Protocol: %d\n", ip_header->ip_p);
        
        // If TCP packet
        if (ip_header->ip_p == IPPROTO_TCP) {
            tcp_header = (struct tcphdr *)(packet + sizeof(struct ether_header) + 
                                          (ip_header->ip_hl * 4));
            printf("TCP: %d -> %d\n", 
                   ntohs(tcp_header->th_sport), 
                   ntohs(tcp_header->th_dport));
            printf("Flags: SYN=%d ACK=%d FIN=%d RST=%d\n",
                   tcp_header->th_flags & TH_SYN ? 1 : 0,
                   tcp_header->th_flags & TH_ACK ? 1 : 0,
                   tcp_header->th_flags & TH_FIN ? 1 : 0,
                   tcp_header->th_flags & TH_RST ? 1 : 0);
        }
    }
}

int main(int argc, char *argv[]) {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle;
    struct bpf_program fp;
    char filter_exp[] = "tcp port 80";  // BPF filter
    bpf_u_int32 net, mask;
    
    // Find a device
    char *dev = pcap_lookupdev(errbuf);
    if (dev == NULL) {
        fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
        return 1;
    }
    
    printf("Capturing on device: %s\n", dev);
    
    // Get network and mask
    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
        fprintf(stderr, "Can't get netmask: %s\n", errbuf);
        net = 0;
        mask = 0;
    }
    
    // Open device for capturing
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        return 1;
    }
    
    // Compile and apply filter
    if (pcap_compile(handle, &fp, filter_exp, 0, net) == -1) {
        fprintf(stderr, "Couldn't parse filter %s: %s\n", 
                filter_exp, pcap_geterr(handle));
        return 1;
    }
    
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "Couldn't install filter %s: %s\n", 
                filter_exp, pcap_geterr(handle));
        return 1;
    }
    
    // Start capturing (capture 10 packets)
    pcap_loop(handle, 10, packet_handler, NULL);
    
    // Cleanup
    pcap_freecode(&fp);
    pcap_close(handle);
    
    return 0;
}
```

### Network Statistics Tool (Similar to netstat/ss)

```cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <arpa/inet.h>

struct SocketInfo {
    std::string local_addr;
    uint16_t local_port;
    std::string remote_addr;
    uint16_t remote_port;
    std::string state;
    int inode;
};

// Parse /proc/net/tcp
std::vector<SocketInfo> get_tcp_sockets() {
    std::vector<SocketInfo> sockets;
    std::ifstream tcp_file("/proc/net/tcp");
    std::string line;
    
    // Skip header
    std::getline(tcp_file, line);
    
    while (std::getline(tcp_file, line)) {
        std::istringstream iss(line);
        SocketInfo info;
        std::string local, remote, state_hex;
        int sl;
        
        iss >> sl >> local >> remote >> state_hex;
        
        // Parse local address:port
        size_t colon = local.find(':');
        std::string local_addr_hex = local.substr(0, colon);
        std::string local_port_hex = local.substr(colon + 1);
        
        // Parse remote address:port
        colon = remote.find(':');
        std::string remote_addr_hex = remote.substr(0, colon);
        std::string remote_port_hex = remote.substr(colon + 1);
        
        // Convert hex to readable format
        unsigned long addr_val = std::stoul(local_addr_hex, nullptr, 16);
        struct in_addr addr;
        addr.s_addr = addr_val;
        info.local_addr = inet_ntoa(addr);
        info.local_port = std::stoul(local_port_hex, nullptr, 16);
        
        addr_val = std::stoul(remote_addr_hex, nullptr, 16);
        addr.s_addr = addr_val;
        info.remote_addr = inet_ntoa(addr);
        info.remote_port = std::stoul(remote_port_hex, nullptr, 16);
        
        // State mapping
        int state = std::stoi(state_hex, nullptr, 16);
        const char* states[] = {
            "", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1",
            "FIN_WAIT2", "TIME_WAIT", "CLOSE", "CLOSE_WAIT", "LAST_ACK",
            "LISTEN", "CLOSING"
        };
        info.state = (state < 12) ? states[state] : "UNKNOWN";
        
        sockets.push_back(info);
    }
    
    return sockets;
}

void display_sockets(const std::vector<SocketInfo>& sockets) {
    std::cout << std::left << std::setw(25) << "Local Address"
              << std::setw(25) << "Remote Address"
              << std::setw(15) << "State" << "\n";
    std::cout << std::string(65, '-') << "\n";
    
    for (const auto& s : sockets) {
        std::cout << std::setw(25) 
                  << (s.local_addr + ":" + std::to_string(s.local_port))
                  << std::setw(25)
                  << (s.remote_addr + ":" + std::to_string(s.remote_port))
                  << std::setw(15) << s.state << "\n";
    }
}

int main() {
    std::cout << "=== TCP Socket Statistics ===\n\n";
    auto sockets = get_tcp_sockets();
    display_sockets(sockets);
    return 0;
}
```

### System Call Tracing (strace-like functionality)

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/reg.h>
#include <sys/syscall.h>

void trace_syscalls(pid_t child_pid) {
    int status;
    int in_syscall = 0;
    
    waitpid(child_pid, &status, 0);
    ptrace(PTRACE_SETOPTIONS, child_pid, 0, PTRACE_O_TRACESYSGOOD);
    
    while (1) {
        ptrace(PTRACE_SYSCALL, child_pid, 0, 0);
        waitpid(child_pid, &status, 0);
        
        if (WIFEXITED(status))
            break;
        
        struct user_regs_struct regs;
        ptrace(PTRACE_GETREGS, child_pid, 0, &regs);
        
        if (!in_syscall) {
            // Entering syscall
            long syscall_num = regs.orig_rax;
            
            // Print network-related syscalls
            switch (syscall_num) {
                case SYS_socket:
                    printf("socket(domain=%ld, type=%ld, protocol=%ld)\n",
                           regs.rdi, regs.rsi, regs.rdx);
                    break;
                case SYS_connect:
                    printf("connect(sockfd=%ld, addr=0x%lx, addrlen=%ld)\n",
                           regs.rdi, regs.rsi, regs.rdx);
                    break;
                case SYS_bind:
                    printf("bind(sockfd=%ld, addr=0x%lx, addrlen=%ld)\n",
                           regs.rdi, regs.rsi, regs.rdx);
                    break;
                case SYS_listen:
                    printf("listen(sockfd=%ld, backlog=%ld)\n",
                           regs.rdi, regs.rsi);
                    break;
                case SYS_accept:
                    printf("accept(sockfd=%ld, addr=0x%lx, addrlen=0x%lx)\n",
                           regs.rdi, regs.rsi, regs.rdx);
                    break;
                case SYS_sendto:
                    printf("sendto(sockfd=%ld, buf=0x%lx, len=%ld, flags=%ld)\n",
                           regs.rdi, regs.rsi, regs.rdx, regs.r10);
                    break;
                case SYS_recvfrom:
                    printf("recvfrom(sockfd=%ld, buf=0x%lx, len=%ld, flags=%ld)\n",
                           regs.rdi, regs.rsi, regs.rdx, regs.r10);
                    break;
            }
            in_syscall = 1;
        } else {
            // Exiting syscall - get return value
            long ret = regs.rax;
            if (ret < 0) {
                printf("  = %ld (error)\n", ret);
            } else {
                printf("  = %ld\n", ret);
            }
            in_syscall = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program> [args...]\n", argv[0]);
        return 1;
    }
    
    pid_t child_pid = fork();
    
    if (child_pid == 0) {
        // Child process
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        execvp(argv[1], argv + 1);
        perror("execvp");
        exit(1);
    } else {
        // Parent process - trace the child
        printf("=== Tracing network syscalls for PID %d ===\n", child_pid);
        trace_syscalls(child_pid);
    }
    
    return 0;
}
```

## Rust Examples

### Packet Capture using pcap crate

```rust
use pcap::{Capture, Device};
use pnet::packet::ethernet::{EtherTypes, EthernetPacket};
use pnet::packet::ip::IpNextHeaderProtocols;
use pnet::packet::ipv4::Ipv4Packet;
use pnet::packet::tcp::TcpPacket;
use pnet::packet::Packet;

fn handle_packet(packet: &[u8]) {
    if let Some(eth_packet) = EthernetPacket::new(packet) {
        println!("\n=== Packet Captured ===");
        println!("Ethernet: {} -> {}", 
                 eth_packet.get_source(), 
                 eth_packet.get_destination());
        
        match eth_packet.get_ethertype() {
            EtherTypes::Ipv4 => {
                if let Some(ipv4) = Ipv4Packet::new(eth_packet.payload()) {
                    println!("IPv4: {} -> {}", 
                             ipv4.get_source(), 
                             ipv4.get_destination());
                    
                    match ipv4.get_next_level_protocol() {
                        IpNextHeaderProtocols::Tcp => {
                            if let Some(tcp) = TcpPacket::new(ipv4.payload()) {
                                println!("TCP: {} -> {}", 
                                         tcp.get_source(), 
                                         tcp.get_destination());
                                println!("Flags: SYN={} ACK={} FIN={} RST={}",
                                         tcp.get_flags() & 0x02 != 0,
                                         tcp.get_flags() & 0x10 != 0,
                                         tcp.get_flags() & 0x01 != 0,
                                         tcp.get_flags() & 0x04 != 0);
                                println!("Seq: {}, Ack: {}", 
                                         tcp.get_sequence(), 
                                         tcp.get_acknowledgement());
                            }
                        }
                        IpNextHeaderProtocols::Udp => {
                            println!("Protocol: UDP");
                        }
                        _ => {
                            println!("Protocol: {}", ipv4.get_next_level_protocol());
                        }
                    }
                }
            }
            _ => {
                println!("Non-IPv4 packet");
            }
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Find the default network device
    let device = Device::lookup()?.unwrap_or_else(|| {
        Device::list()
            .expect("Failed to list devices")
            .into_iter()
            .next()
            .expect("No devices found")
    });
    
    println!("Capturing on device: {}", device.name);
    
    // Open the device for capturing
    let mut cap = Capture::from_device(device)?
        .promisc(true)
        .snaplen(65535)
        .timeout(1000)
        .open()?;
    
    // Set BPF filter
    cap.filter("tcp port 80 or tcp port 443", true)?;
    
    println!("Starting packet capture...\n");
    
    // Capture packets
    for _ in 0..10 {
        match cap.next_packet() {
            Ok(packet) => {
                handle_packet(packet.data);
            }
            Err(e) => {
                eprintln!("Error capturing packet: {}", e);
            }
        }
    }
    
    Ok(())
}
```

### Socket Statistics (ss-like tool)

```rust
use std::fs;
use std::net::{IpAddr, Ipv4Addr};

#[derive(Debug)]
struct SocketInfo {
    local_addr: String,
    local_port: u16,
    remote_addr: String,
    remote_port: u16,
    state: String,
}

impl SocketInfo {
    fn parse_hex_addr(hex: &str) -> (Ipv4Addr, u16) {
        let parts: Vec<&str> = hex.split(':').collect();
        
        // Parse address (little-endian hex)
        let addr_hex = u32::from_str_radix(parts[0], 16).unwrap();
        let addr = Ipv4Addr::from(addr_hex.to_le());
        
        // Parse port
        let port = u16::from_str_radix(parts[1], 16).unwrap();
        
        (addr, port)
    }
    
    fn from_proc_line(line: &str) -> Option<Self> {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 4 {
            return None;
        }
        
        let (local_addr, local_port) = Self::parse_hex_addr(parts[1]);
        let (remote_addr, remote_port) = Self::parse_hex_addr(parts[2]);
        
        let state_num = u8::from_str_radix(parts[3], 16).ok()?;
        let state = match state_num {
            0x01 => "ESTABLISHED",
            0x02 => "SYN_SENT",
            0x03 => "SYN_RECV",
            0x04 => "FIN_WAIT1",
            0x05 => "FIN_WAIT2",
            0x06 => "TIME_WAIT",
            0x07 => "CLOSE",
            0x08 => "CLOSE_WAIT",
            0x09 => "LAST_ACK",
            0x0A => "LISTEN",
            0x0B => "CLOSING",
            _ => "UNKNOWN",
        };
        
        Some(SocketInfo {
            local_addr: local_addr.to_string(),
            local_port,
            remote_addr: remote_addr.to_string(),
            remote_port,
            state: state.to_string(),
        })
    }
}

fn get_tcp_sockets() -> Result<Vec<SocketInfo>, std::io::Error> {
    let content = fs::read_to_string("/proc/net/tcp")?;
    let mut sockets = Vec::new();
    
    for (i, line) in content.lines().enumerate() {
        if i == 0 {
            continue; // Skip header
        }
        
        if let Some(info) = SocketInfo::from_proc_line(line) {
            sockets.push(info);
        }
    }
    
    Ok(sockets)
}

fn display_sockets(sockets: &[SocketInfo]) {
    println!("{:<25} {:<25} {:<15}", "Local Address", "Remote Address", "State");
    println!("{}", "-".repeat(65));
    
    for socket in sockets {
        println!("{:<25} {:<25} {:<15}",
                 format!("{}:{}", socket.local_addr, socket.local_port),
                 format!("{}:{}", socket.remote_addr, socket.remote_port),
                 socket.state);
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("=== TCP Socket Statistics ===\n");
    
    let sockets = get_tcp_sockets()?;
    display_sockets(&sockets);
    
    println!("\nTotal connections: {}", sockets.len());
    
    Ok(())
}
```

### Network Traffic Monitor

```rust
use std::collections::HashMap;
use std::net::IpAddr;
use std::time::{Duration, Instant};
use pcap::{Capture, Device};
use pnet::packet::ethernet::EthernetPacket;
use pnet::packet::ipv4::Ipv4Packet;
use pnet::packet::Packet;

#[derive(Debug, Default)]
struct TrafficStats {
    packet_count: u64,
    byte_count: u64,
    last_seen: Option<Instant>,
}

struct NetworkMonitor {
    stats: HashMap<IpAddr, TrafficStats>,
    start_time: Instant,
}

impl NetworkMonitor {
    fn new() -> Self {
        NetworkMonitor {
            stats: HashMap::new(),
            start_time: Instant::now(),
        }
    }
    
    fn process_packet(&mut self, packet: &[u8]) {
        if let Some(eth) = EthernetPacket::new(packet) {
            if let Some(ipv4) = Ipv4Packet::new(eth.payload()) {
                let src = IpAddr::V4(ipv4.get_source());
                let dst = IpAddr::V4(ipv4.get_destination());
                let length = ipv4.get_total_length() as u64;
                
                // Update source stats
                let src_stats = self.stats.entry(src).or_default();
                src_stats.packet_count += 1;
                src_stats.byte_count += length;
                src_stats.last_seen = Some(Instant::now());
                
                // Update destination stats
                let dst_stats = self.stats.entry(dst).or_default();
                dst_stats.packet_count += 1;
                dst_stats.byte_count += length;
                dst_stats.last_seen = Some(Instant::now());
            }
        }
    }
    
    fn display_stats(&self) {
        let elapsed = self.start_time.elapsed();
        
        println!("\n{}", "=".repeat(80));
        println!("Network Traffic Statistics (runtime: {:.2}s)", elapsed.as_secs_f64());
        println!("{}", "=".repeat(80));
        println!("{:<20} {:<15} {:<15} {:<20}", 
                 "IP Address", "Packets", "Bytes", "Avg Rate (KB/s)");
        println!("{}", "-".repeat(80));
        
        let mut sorted_stats: Vec<_> = self.stats.iter().collect();
        sorted_stats.sort_by(|a, b| b.1.byte_count.cmp(&a.1.byte_count));
        
        for (addr, stats) in sorted_stats.iter().take(20) {
            let rate = (stats.byte_count as f64 / 1024.0) / elapsed.as_secs_f64();
            println!("{:<20} {:<15} {:<15} {:<20.2}",
                     addr,
                     stats.packet_count,
                     stats.byte_count,
                     rate);
        }
        
        let total_bytes: u64 = self.stats.values().map(|s| s.byte_count).sum();
        let total_packets: u64 = self.stats.values().map(|s| s.packet_count).sum();
        
        println!("{}", "-".repeat(80));
        println!("Total: {} packets, {} bytes ({:.2} KB/s average)",
                 total_packets,
                 total_bytes,
                 (total_bytes as f64 / 1024.0) / elapsed.as_secs_f64());
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let device = Device::lookup()?.unwrap();
    println!("Monitoring traffic on: {}", device.name);
    
    let mut cap = Capture::from_device(device)?
        .promisc(true)
        .snaplen(65535)
        .timeout(1000)
        .open()?;
    
    let mut monitor = NetworkMonitor::new();
    let mut last_display = Instant::now();
    
    println!("Starting network monitoring... (Ctrl+C to stop)");
    
    loop {
        match cap.next_packet() {
            Ok(packet) => {
                monitor.process_packet(packet.data);
            }
            Err(_) => {
                // Timeout - continue
            }
        }
        
        // Display stats every 5 seconds
        if last_display.elapsed() >= Duration::from_secs(5) {
            monitor.display_stats();
            last_display = Instant::now();
        }
    }
}
```

## Summary

Network debugging tools provide crucial visibility into network operations at different levels:

**tcpdump/libpcap** offers low-level packet capture with minimal overhead, ideal for production troubleshooting and automated analysis. It operates at the data link layer and can capture raw packets with powerful BPF filtering.

**Wireshark** provides comprehensive protocol analysis with a graphical interface, making it excellent for deep packet inspection and learning protocols. It's the go-to tool for understanding complex network interactions.

**netstat/ss** give socket-level visibility, showing active connections, listening ports, and TCP states. The modern `ss` tool is faster and provides more detailed information by querying the kernel directly rather than parsing /proc files.

**strace** reveals the system call interface between applications and the kernel, helping debug application-level network issues by showing exactly what calls are being made and their results.

When debugging network issues, you typically start with high-level tools (ss/netstat) to identify problematic connections, use packet capture (tcpdump/Wireshark) to analyze the actual traffic, and employ system call tracing (strace) to understand application behavior. Each tool provides a different perspective on the same network stack, and combining them effectively is key to resolving complex networking problems.