# RSS and RPS: Receive Side Scaling and Receive Packet Steering

## Overview

RSS (Receive Side Scaling) and RPS (Receive Packet Steering) are techniques designed to distribute network packet processing across multiple CPU cores in modern multi-core systems. They address the bottleneck that occurs when a single CPU core must handle all incoming network traffic, enabling better throughput and lower latency.

**RSS** is a hardware-based solution implemented in network interface cards (NICs), while **RPS** is a software-based alternative that provides similar functionality when hardware RSS isn't available.

## Core Concepts

### Receive Side Scaling (RSS)

RSS uses the NIC hardware to distribute incoming packets across multiple receive queues, with each queue typically handled by a different CPU core. The NIC computes a hash value based on packet header fields (IP addresses, ports) and uses this hash to assign packets to specific queues.

**Key benefits:**
- Hardware offloading reduces CPU overhead
- Maintains packet ordering for flows (packets belonging to the same connection go to the same queue)
- Interrupt distribution across cores
- Better cache locality since the same core processes packets from the same flow

### Receive Packet Steering (RPS)

RPS implements similar logic in software, distributing packets to CPU cores after the NIC driver receives them. It's useful for NICs that don't support RSS or when you need more fine-grained control than hardware provides.

**Key benefits:**
- Works with any NIC
- More flexible configuration
- Can be combined with RSS for additional steering
- Useful for virtualized environments

## Programming and Configuration

### C/C++ Examples

#### Checking RSS Capabilities

```c
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <unistd.h>

// Check if NIC supports RSS
int check_rss_support(const char *ifname) {
    int sock;
    struct ifreq ifr;
    struct ethtool_rxfh rxfh;
    
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    
    memset(&rxfh, 0, sizeof(rxfh));
    rxfh.cmd = ETHTOOL_GRSSH;
    rxfh.indir_size = 0;
    rxfh.key_size = 0;
    
    ifr.ifr_data = (void *)&rxfh;
    
    if (ioctl(sock, SIOCETHTOOL, &ifr) < 0) {
        perror("ioctl ETHTOOL_GRSSH");
        close(sock);
        return -1;
    }
    
    printf("RSS supported on %s\n", ifname);
    printf("Hash function: %u\n", rxfh.hfunc);
    printf("Indirection table size: %u\n", rxfh.indir_size);
    printf("Hash key size: %u\n", rxfh.key_size);
    
    close(sock);
    return 0;
}
```

#### Configuring RSS Hash Key

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

int set_rss_hash_key(const char *ifname, const unsigned char *key, 
                     size_t key_len) {
    int sock;
    struct ifreq ifr;
    struct ethtool_rxfh *rxfh;
    size_t struct_size;
    
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    // Allocate memory for ethtool_rxfh structure plus key
    struct_size = sizeof(*rxfh) + key_len;
    rxfh = (struct ethtool_rxfh *)calloc(1, struct_size);
    if (!rxfh) {
        close(sock);
        return -1;
    }
    
    rxfh->cmd = ETHTOOL_SRSSH;
    rxfh->key_size = key_len;
    rxfh->hfunc = ETH_RSS_HASH_TOP; // Toeplitz hash
    
    // Copy the hash key
    memcpy(rxfh->rss_config, key, key_len);
    
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_data = (void *)rxfh;
    
    if (ioctl(sock, SIOCETHTOOL, &ifr) < 0) {
        perror("ioctl ETHTOOL_SRSSH");
        free(rxfh);
        close(sock);
        return -1;
    }
    
    printf("RSS hash key set successfully\n");
    
    free(rxfh);
    close(sock);
    return 0;
}
```

#### Setting CPU Affinity for RPS

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Configure RPS for a network interface queue
int configure_rps(const char *ifname, int queue, unsigned int cpu_mask) {
    char path[256];
    FILE *fp;
    
    // Path to RPS configuration for the queue
    snprintf(path, sizeof(path), 
             "/sys/class/net/%s/queues/rx-%d/rps_cpus", 
             ifname, queue);
    
    fp = fopen(path, "w");
    if (!fp) {
        perror("fopen");
        return -1;
    }
    
    // Write CPU mask in hex format
    fprintf(fp, "%x", cpu_mask);
    fclose(fp);
    
    printf("RPS configured for %s queue %d: CPU mask 0x%x\n", 
           ifname, queue, cpu_mask);
    
    return 0;
}

// Example: Configure RPS to use CPUs 0-3 (mask 0xF)
int main() {
    const char *interface = "eth0";
    int queue = 0;
    unsigned int cpu_mask = 0xF; // CPUs 0, 1, 2, 3
    
    configure_rps(interface, queue, cpu_mask);
    
    return 0;
}
```

#### Multi-threaded Packet Processing with CPU Affinity

```cpp
#include <iostream>
#include <thread>
#include <vector>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class PacketProcessor {
private:
    int cpu_id;
    int sock_fd;
    
public:
    PacketProcessor(int cpu) : cpu_id(cpu), sock_fd(-1) {}
    
    // Set thread affinity to specific CPU
    void set_cpu_affinity() {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        
        pthread_t thread = pthread_self();
        int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
        
        if (result != 0) {
            std::cerr << "Failed to set CPU affinity for CPU " << cpu_id << std::endl;
        } else {
            std::cout << "Thread bound to CPU " << cpu_id << std::endl;
        }
    }
    
    // Process packets on dedicated core
    void process_packets(int socket_fd) {
        set_cpu_affinity();
        
        char buffer[65536];
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        while (true) {
            ssize_t received = recvfrom(socket_fd, buffer, sizeof(buffer), 0,
                                       (struct sockaddr*)&client_addr, &addr_len);
            
            if (received < 0) {
                perror("recvfrom");
                continue;
            }
            
            // Process packet (simplified)
            // In real implementation, this would parse headers, etc.
            std::cout << "CPU " << cpu_id << " processed " << received 
                     << " bytes\n";
        }
    }
};

int main() {
    const int num_threads = 4; // Match number of RSS queues
    std::vector<std::thread> threads;
    
    // Create socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    
    // Enable SO_REUSEPORT to allow multiple threads to bind to same port
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    // Launch processing threads
    for (int i = 0; i < num_threads; i++) {
        PacketProcessor processor(i);
        threads.emplace_back([processor, sock]() mutable {
            processor.process_packets(sock);
        });
    }
    
    // Wait for threads
    for (auto& t : threads) {
        t.join();
    }
    
    close(sock);
    return 0;
}
```

### Rust Examples

#### Reading RSS Configuration

```rust
use std::fs;
use std::path::Path;

#[derive(Debug)]
struct RssConfig {
    cpu_mask: String,
    flow_count: u32,
}

fn read_rps_config(interface: &str, queue: u32) -> Result<RssConfig, std::io::Error> {
    let rps_path = format!("/sys/class/net/{}/queues/rx-{}/rps_cpus", 
                           interface, queue);
    let flow_path = format!("/sys/class/net/{}/queues/rx-{}/rps_flow_cnt", 
                            interface, queue);
    
    let cpu_mask = fs::read_to_string(&rps_path)?
        .trim()
        .to_string();
    
    let flow_count = fs::read_to_string(&flow_path)?
        .trim()
        .parse()
        .unwrap_or(0);
    
    Ok(RssConfig {
        cpu_mask,
        flow_count,
    })
}

fn main() {
    match read_rps_config("eth0", 0) {
        Ok(config) => {
            println!("RPS Configuration:");
            println!("  CPU Mask: {}", config.cpu_mask);
            println!("  Flow Count: {}", config.flow_count);
        }
        Err(e) => eprintln!("Error reading RPS config: {}", e),
    }
}
```

#### Configuring RPS

```rust
use std::fs::File;
use std::io::Write;

fn configure_rps(interface: &str, queue: u32, cpu_mask: &str) -> std::io::Result<()> {
    let path = format!("/sys/class/net/{}/queues/rx-{}/rps_cpus", 
                       interface, queue);
    
    let mut file = File::create(&path)?;
    file.write_all(cpu_mask.as_bytes())?;
    
    println!("RPS configured for {} queue {}: CPU mask {}", 
             interface, queue, cpu_mask);
    
    Ok(())
}

fn configure_rps_flow_limit(interface: &str, queue: u32, 
                           flow_count: u32) -> std::io::Result<()> {
    let path = format!("/sys/class/net/{}/queues/rx-{}/rps_flow_cnt", 
                       interface, queue);
    
    let mut file = File::create(&path)?;
    file.write_all(flow_count.to_string().as_bytes())?;
    
    println!("RPS flow limit set to {} for {} queue {}", 
             flow_count, interface, queue);
    
    Ok(())
}

fn main() -> std::io::Result<()> {
    let interface = "eth0";
    
    // Configure RPS to use CPUs 0-3 (hex mask f)
    configure_rps(interface, 0, "f")?;
    
    // Set flow limit to 32768
    configure_rps_flow_limit(interface, 0, 32768)?;
    
    Ok(())
}
```

#### High-Performance Packet Processor with Thread Affinity

```rust
use std::sync::Arc;
use std::thread;
use std::net::UdpSocket;
use core_affinity;

struct PacketProcessor {
    cpu_id: usize,
    port: u16,
}

impl PacketProcessor {
    fn new(cpu_id: usize, port: u16) -> Self {
        PacketProcessor { cpu_id, port }
    }
    
    fn process_packets(&self) {
        // Set CPU affinity
        let core_ids = core_affinity::get_core_ids().unwrap();
        if self.cpu_id < core_ids.len() {
            core_affinity::set_for_current(core_ids[self.cpu_id]);
            println!("Thread bound to CPU {}", self.cpu_id);
        }
        
        // Create socket with SO_REUSEPORT
        let socket = UdpSocket::bind(format!("0.0.0.0:{}", self.port))
            .expect("Failed to bind socket");
        
        // Set SO_REUSEPORT socket option (platform-specific)
        #[cfg(target_os = "linux")]
        {
            use std::os::unix::io::AsRawFd;
            use libc::{setsockopt, SOL_SOCKET, SO_REUSEPORT};
            
            let optval: libc::c_int = 1;
            unsafe {
                setsockopt(
                    socket.as_raw_fd(),
                    SOL_SOCKET,
                    SO_REUSEPORT,
                    &optval as *const _ as *const libc::c_void,
                    std::mem::size_of_val(&optval) as libc::socklen_t,
                );
            }
        }
        
        let mut buffer = [0u8; 65536];
        
        loop {
            match socket.recv_from(&mut buffer) {
                Ok((size, src)) => {
                    println!("CPU {} received {} bytes from {}", 
                             self.cpu_id, size, src);
                    
                    // Process packet here
                    // This is where actual packet parsing would occur
                }
                Err(e) => eprintln!("Error receiving: {}", e),
            }
        }
    }
}

fn main() {
    let num_cores = 4;
    let port = 12345;
    let mut handles = vec![];
    
    for cpu_id in 0..num_cores {
        let processor = PacketProcessor::new(cpu_id, port);
        
        let handle = thread::spawn(move || {
            processor.process_packets();
        });
        
        handles.push(handle);
    }
    
    // Wait for all threads
    for handle in handles {
        handle.join().unwrap();
    }
}
```

#### RSS Hash Function Implementation

```rust
// Toeplitz hash implementation (used by RSS)
struct ToeplitzHash {
    key: Vec<u8>,
}

impl ToeplitzHash {
    fn new(key: Vec<u8>) -> Self {
        ToeplitzHash { key }
    }
    
    fn compute_hash(&self, input: &[u8]) -> u32 {
        let mut hash: u32 = 0;
        let mut key_bits = Vec::new();
        
        // Convert key bytes to bits
        for byte in &self.key {
            for i in (0..8).rev() {
                key_bits.push((byte >> i) & 1);
            }
        }
        
        let mut key_index = 0;
        
        // Process each input byte
        for byte in input {
            for i in (0..8).rev() {
                let input_bit = (byte >> i) & 1;
                
                if input_bit == 1 && key_index < key_bits.len() {
                    // XOR with next 32 key bits
                    for j in 0..32 {
                        if key_index + j < key_bits.len() {
                            let bit_pos = 31 - j;
                            hash ^= (key_bits[key_index + j] as u32) << bit_pos;
                        }
                    }
                }
                
                key_index += 1;
                if key_index >= key_bits.len() {
                    break;
                }
            }
        }
        
        hash
    }
    
    // Compute hash for TCP/UDP packet
    fn hash_flow(&self, src_ip: u32, dst_ip: u32, 
                 src_port: u16, dst_port: u16) -> u32 {
        let mut input = Vec::new();
        
        input.extend_from_slice(&src_ip.to_be_bytes());
        input.extend_from_slice(&dst_ip.to_be_bytes());
        input.extend_from_slice(&src_port.to_be_bytes());
        input.extend_from_slice(&dst_port.to_be_bytes());
        
        self.compute_hash(&input)
    }
}

fn main() {
    // Example RSS key (40 bytes, typical for Intel NICs)
    let rss_key = vec![
        0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
        0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
        0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
        0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
        0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
    ];
    
    let hasher = ToeplitzHash::new(rss_key);
    
    // Example: Hash a TCP flow
    let src_ip: u32 = 0xC0A80101; // 192.168.1.1
    let dst_ip: u32 = 0xC0A80102; // 192.168.1.2
    let src_port: u16 = 12345;
    let dst_port: u16 = 80;
    
    let hash = hasher.hash_flow(src_ip, dst_ip, src_port, dst_port);
    
    println!("RSS Hash: 0x{:08x}", hash);
    
    // Determine queue (assuming 4 queues)
    let num_queues = 4;
    let queue = hash as usize % num_queues;
    println!("Assigned to queue: {}", queue);
}
```

## Summary

RSS and RPS are essential techniques for scaling network performance on multi-core systems. RSS leverages NIC hardware to distribute packets across multiple receive queues based on flow hashing, while RPS provides similar functionality in software. Both maintain flow affinity to ensure packets from the same connection are processed by the same CPU core, preserving cache locality and packet ordering.

Key takeaways:
- **RSS** is hardware-based, offloading hash computation to the NIC with lower CPU overhead
- **RPS** is software-based, providing flexibility when hardware support isn't available
- Both use hash functions (typically Toeplitz) on packet headers to distribute traffic
- Proper configuration requires setting CPU masks and understanding your workload
- Combining with SO_REUSEPORT allows multiple threads to efficiently process distributed packets
- Essential for high-throughput applications like web servers, load balancers, and network appliances

Modern high-performance network applications should leverage these features to maximize throughput and minimize latency by fully utilizing available CPU resources.