# Quality of Service (QoS) in TCP/IP Networks

## Detailed Description

Quality of Service (QoS) is a set of technologies and techniques used to manage network traffic and ensure reliable delivery of high-priority data streams. QoS allows network administrators to prioritize certain types of traffic over others, control bandwidth allocation, and maintain service levels for critical applications.

### Core Concepts

**Type of Service (TOS) and Differentiated Services Code Point (DSCP)**

The IP header contains a field originally called Type of Service (TOS), which has evolved into the Differentiated Services (DiffServ) field. This 8-bit field is structured as:
- 6 bits for DSCP (Differentiated Services Code Point)
- 2 bits for ECN (Explicit Congestion Notification)

DSCP values define how packets should be treated by network devices. Common DSCP classes include:
- **Best Effort (BE)**: Default, no special treatment (DSCP 0)
- **Expedited Forwarding (EF)**: Low latency, low jitter for voice traffic (DSCP 46)
- **Assured Forwarding (AF)**: Multiple classes with drop precedence levels
- **Class Selector (CS)**: Backward compatible with IP Precedence

**Traffic Shaping**

Traffic shaping controls the rate at which packets are transmitted to ensure smooth, predictable network behavior. Key techniques include:
- **Token Bucket**: Allows bursts up to a limit while maintaining average rate
- **Leaky Bucket**: Enforces strict constant rate output
- **Rate Limiting**: Caps maximum bandwidth usage

**Priority Queuing**

Network devices use multiple queues to separate traffic by priority:
- **Strict Priority Queuing**: Higher priority queues always serviced first
- **Weighted Fair Queuing (WFQ)**: Bandwidth distributed proportionally
- **Class-Based Weighted Fair Queuing (CBWFQ)**: Combines classification with WFQ

---

## C/C++ Code Examples

### Setting DSCP/TOS Values on a Socket

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>

// DSCP values (shift left 2 bits for TOS field)
#define DSCP_EF  (46 << 2)  // Expedited Forwarding
#define DSCP_AF41 (34 << 2)  // Assured Forwarding Class 4, Low Drop
#define DSCP_CS0  (0 << 2)   // Best Effort

int set_socket_qos(int sockfd, int dscp_value) {
    int tos = dscp_value;
    
    // Set IP_TOS socket option
    if (setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
        perror("setsockopt IP_TOS failed");
        return -1;
    }
    
    printf("Set DSCP value to %d (TOS: 0x%02X)\n", dscp_value >> 2, tos);
    return 0;
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    
    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }
    
    // Set QoS for VoIP traffic (Expedited Forwarding)
    if (set_socket_qos(sockfd, DSCP_EF) < 0) {
        close(sockfd);
        return 1;
    }
    
    // Configure destination
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5060);
    inet_pton(AF_INET, "192.168.1.100", &server_addr.sin_addr);
    
    // Send data with QoS marking
    const char *message = "VoIP packet with EF marking";
    sendto(sockfd, message, strlen(message), 0,
           (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    close(sockfd);
    return 0;
}
```

### Token Bucket Traffic Shaper

```cpp
#include <iostream>
#include <chrono>
#include <thread>
#include <queue>
#include <mutex>

class TokenBucket {
private:
    size_t capacity;           // Maximum tokens (burst size)
    size_t tokens;             // Current tokens available
    size_t rate;               // Tokens per second
    std::chrono::steady_clock::time_point last_update;
    std::mutex mtx;

public:
    TokenBucket(size_t rate_per_sec, size_t burst_size) 
        : capacity(burst_size), tokens(burst_size), rate(rate_per_sec),
          last_update(std::chrono::steady_clock::now()) {}
    
    void refill() {
        std::lock_guard<std::mutex> lock(mtx);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_update).count();
        
        // Add tokens based on elapsed time
        size_t new_tokens = (elapsed * rate) / 1000;
        tokens = std::min(capacity, tokens + new_tokens);
        last_update = now;
    }
    
    bool consume(size_t num_tokens) {
        std::lock_guard<std::mutex> lock(mtx);
        if (tokens >= num_tokens) {
            tokens -= num_tokens;
            return true;
        }
        return false;
    }
    
    bool try_send_packet(size_t packet_size) {
        refill();
        return consume(packet_size);
    }
};

// Example usage
int main() {
    // Create token bucket: 1000 bytes/sec rate, 5000 byte burst
    TokenBucket shaper(1000, 5000);
    
    // Simulate sending packets
    for (int i = 0; i < 20; i++) {
        size_t packet_size = 500;  // 500 byte packet
        
        while (!shaper.try_send_packet(packet_size)) {
            std::cout << "Packet " << i << " waiting (rate limited)...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Packet " << i << " sent (" << packet_size << " bytes)\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    return 0;
}
```

### Priority Queue Implementation

```cpp
#include <iostream>
#include <queue>
#include <string>
#include <memory>

enum QoSClass {
    BEST_EFFORT = 0,
    ASSURED_FORWARDING = 1,
    EXPEDITED_FORWARDING = 2
};

struct Packet {
    std::string data;
    QoSClass priority;
    size_t size;
    
    Packet(const std::string& d, QoSClass p, size_t s) 
        : data(d), priority(p), size(s) {}
};

class PriorityQueueScheduler {
private:
    std::queue<std::shared_ptr<Packet>> ef_queue;   // Highest priority
    std::queue<std::shared_ptr<Packet>> af_queue;   // Medium priority
    std::queue<std::shared_ptr<Packet>> be_queue;   // Best effort
    
public:
    void enqueue(std::shared_ptr<Packet> pkt) {
        switch (pkt->priority) {
            case EXPEDITED_FORWARDING:
                ef_queue.push(pkt);
                std::cout << "Enqueued EF packet: " << pkt->data << "\n";
                break;
            case ASSURED_FORWARDING:
                af_queue.push(pkt);
                std::cout << "Enqueued AF packet: " << pkt->data << "\n";
                break;
            case BEST_EFFORT:
                be_queue.push(pkt);
                std::cout << "Enqueued BE packet: " << pkt->data << "\n";
                break;
        }
    }
    
    std::shared_ptr<Packet> dequeue() {
        // Strict priority: EF > AF > BE
        if (!ef_queue.empty()) {
            auto pkt = ef_queue.front();
            ef_queue.pop();
            std::cout << "Dequeued EF packet: " << pkt->data << "\n";
            return pkt;
        }
        
        if (!af_queue.empty()) {
            auto pkt = af_queue.front();
            af_queue.pop();
            std::cout << "Dequeued AF packet: " << pkt->data << "\n";
            return pkt;
        }
        
        if (!be_queue.empty()) {
            auto pkt = be_queue.front();
            be_queue.pop();
            std::cout << "Dequeued BE packet: " << pkt->data << "\n";
            return pkt;
        }
        
        return nullptr;
    }
    
    bool has_packets() const {
        return !ef_queue.empty() || !af_queue.empty() || !be_queue.empty();
    }
};

int main() {
    PriorityQueueScheduler scheduler;
    
    // Enqueue packets with different priorities
    scheduler.enqueue(std::make_shared<Packet>("Web traffic", BEST_EFFORT, 1500));
    scheduler.enqueue(std::make_shared<Packet>("VoIP call", EXPEDITED_FORWARDING, 200));
    scheduler.enqueue(std::make_shared<Packet>("Video stream", ASSURED_FORWARDING, 1400));
    scheduler.enqueue(std::make_shared<Packet>("Email", BEST_EFFORT, 1000));
    scheduler.enqueue(std::make_shared<Packet>("VoIP call 2", EXPEDITED_FORWARDING, 200));
    
    std::cout << "\n--- Dequeuing packets ---\n";
    // Process packets (EF packets come out first)
    while (scheduler.has_packets()) {
        scheduler.dequeue();
    }
    
    return 0;
}
```

---

## Rust Code Examples

### Setting DSCP Values

```rust
use std::net::{UdpSocket, SocketAddr};
use std::os::unix::io::AsRawFd;

// DSCP values
const DSCP_EF: i32 = 46 << 2;    // Expedited Forwarding
const DSCP_AF41: i32 = 34 << 2;  // Assured Forwarding
const DSCP_CS0: i32 = 0 << 2;    // Best Effort

fn set_socket_tos(socket: &UdpSocket, tos_value: i32) -> std::io::Result<()> {
    use libc::{setsockopt, IPPROTO_IP, IP_TOS, SOL_SOCKET};
    
    let fd = socket.as_raw_fd();
    let tos = tos_value as libc::c_int;
    
    unsafe {
        let ret = setsockopt(
            fd,
            IPPROTO_IP,
            IP_TOS,
            &tos as *const _ as *const libc::c_void,
            std::mem::size_of::<libc::c_int>() as libc::socklen_t,
        );
        
        if ret < 0 {
            return Err(std::io::Error::last_os_error());
        }
    }
    
    println!("Set DSCP to {} (TOS: 0x{:02X})", tos_value >> 2, tos_value);
    Ok(())
}

fn main() -> std::io::Result<()> {
    let socket = UdpSocket::bind("0.0.0.0:0")?;
    
    // Set QoS for real-time traffic
    set_socket_tos(&socket, DSCP_EF)?;
    
    let dest: SocketAddr = "192.168.1.100:5060".parse().unwrap();
    socket.send_to(b"VoIP packet with QoS marking", dest)?;
    
    println!("Packet sent with EF marking");
    Ok(())
}
```

### Token Bucket Traffic Shaper

```rust
use std::time::{Duration, Instant};
use std::thread;

struct TokenBucket {
    capacity: usize,        // Maximum tokens (burst size)
    tokens: f64,            // Current available tokens
    rate: f64,              // Tokens per second
    last_update: Instant,
}

impl TokenBucket {
    fn new(rate_per_sec: usize, burst_size: usize) -> Self {
        TokenBucket {
            capacity: burst_size,
            tokens: burst_size as f64,
            rate: rate_per_sec as f64,
            last_update: Instant::now(),
        }
    }
    
    fn refill(&mut self) {
        let now = Instant::now();
        let elapsed = now.duration_since(self.last_update).as_secs_f64();
        
        // Add tokens based on elapsed time
        let new_tokens = elapsed * self.rate;
        self.tokens = (self.tokens + new_tokens).min(self.capacity as f64);
        self.last_update = now;
    }
    
    fn consume(&mut self, num_tokens: usize) -> bool {
        if self.tokens >= num_tokens as f64 {
            self.tokens -= num_tokens as f64;
            true
        } else {
            false
        }
    }
    
    fn try_send_packet(&mut self, packet_size: usize) -> bool {
        self.refill();
        self.consume(packet_size)
    }
}

fn main() {
    // Create token bucket: 1000 bytes/sec, 5000 byte burst
    let mut shaper = TokenBucket::new(1000, 5000);
    
    // Simulate sending packets
    for i in 0..20 {
        let packet_size = 500;
        
        while !shaper.try_send_packet(packet_size) {
            println!("Packet {} waiting (rate limited)...", i);
            thread::sleep(Duration::from_millis(100));
        }
        
        println!("Packet {} sent ({} bytes)", i, packet_size);
        thread::sleep(Duration::from_millis(50));
    }
}
```

### Priority Queue Scheduler

```rust
use std::collections::VecDeque;

#[derive(Debug, Clone, Copy, PartialEq)]
enum QoSClass {
    BestEffort = 0,
    AssuredForwarding = 1,
    ExpeditedForwarding = 2,
}

#[derive(Debug)]
struct Packet {
    data: String,
    priority: QoSClass,
    size: usize,
}

impl Packet {
    fn new(data: String, priority: QoSClass, size: usize) -> Self {
        Packet { data, priority, size }
    }
}

struct PriorityQueueScheduler {
    ef_queue: VecDeque<Packet>,  // Highest priority
    af_queue: VecDeque<Packet>,  // Medium priority
    be_queue: VecDeque<Packet>,  // Best effort
}

impl PriorityQueueScheduler {
    fn new() -> Self {
        PriorityQueueScheduler {
            ef_queue: VecDeque::new(),
            af_queue: VecDeque::new(),
            be_queue: VecDeque::new(),
        }
    }
    
    fn enqueue(&mut self, packet: Packet) {
        match packet.priority {
            QoSClass::ExpeditedForwarding => {
                println!("Enqueued EF packet: {}", packet.data);
                self.ef_queue.push_back(packet);
            }
            QoSClass::AssuredForwarding => {
                println!("Enqueued AF packet: {}", packet.data);
                self.af_queue.push_back(packet);
            }
            QoSClass::BestEffort => {
                println!("Enqueued BE packet: {}", packet.data);
                self.be_queue.push_back(packet);
            }
        }
    }
    
    fn dequeue(&mut self) -> Option<Packet> {
        // Strict priority: EF > AF > BE
        if let Some(pkt) = self.ef_queue.pop_front() {
            println!("Dequeued EF packet: {}", pkt.data);
            return Some(pkt);
        }
        
        if let Some(pkt) = self.af_queue.pop_front() {
            println!("Dequeued AF packet: {}", pkt.data);
            return Some(pkt);
        }
        
        if let Some(pkt) = self.be_queue.pop_front() {
            println!("Dequeued BE packet: {}", pkt.data);
            return Some(pkt);
        }
        
        None
    }
    
    fn has_packets(&self) -> bool {
        !self.ef_queue.is_empty() || 
        !self.af_queue.is_empty() || 
        !self.be_queue.is_empty()
    }
}

fn main() {
    let mut scheduler = PriorityQueueScheduler::new();
    
    // Enqueue packets with different priorities
    scheduler.enqueue(Packet::new("Web traffic".to_string(), 
                                   QoSClass::BestEffort, 1500));
    scheduler.enqueue(Packet::new("VoIP call".to_string(), 
                                   QoSClass::ExpeditedForwarding, 200));
    scheduler.enqueue(Packet::new("Video stream".to_string(), 
                                   QoSClass::AssuredForwarding, 1400));
    scheduler.enqueue(Packet::new("Email".to_string(), 
                                   QoSClass::BestEffort, 1000));
    scheduler.enqueue(Packet::new("VoIP call 2".to_string(), 
                                   QoSClass::ExpeditedForwarding, 200));
    
    println!("\n--- Dequeuing packets ---");
    // Process packets (EF packets come out first)
    while scheduler.has_packets() {
        scheduler.dequeue();
    }
}
```

---

## Summary

Quality of Service (QoS) in TCP/IP networks provides mechanisms to prioritize and manage network traffic based on application requirements. The key components include the DSCP field in IP headers for packet marking, traffic shaping algorithms like token bucket to control transmission rates, and priority queuing mechanisms to ensure time-sensitive traffic receives preferential treatment.

DSCP values allow routers and switches to classify packets into different service classes, with Expedited Forwarding for latency-sensitive applications like VoIP, Assured Forwarding for important business traffic, and Best Effort for general traffic. Traffic shaping prevents network congestion by controlling burst sizes and enforcing rate limits, while priority queuing ensures high-priority packets are processed before lower-priority ones.

Implementing QoS requires coordination across the network stack: applications mark packets appropriately using socket options, traffic shapers enforce bandwidth policies, and schedulers determine packet transmission order. These techniques are essential for modern networks supporting diverse applications with varying performance requirements, from real-time voice and video to bulk data transfers.