# UDP Reliability Patterns: Building Reliable Protocols on Top of UDP

## Overview

UDP (User Datagram Protocol) is an unreliable, connectionless transport protocol that provides no guarantees about packet delivery, ordering, or duplicate prevention. However, many applications need UDP's low latency and lightweight nature while also requiring some level of reliability. This is where UDP reliability patterns come in—techniques for building custom reliability mechanisms on top of UDP.

## Why Build Reliability on UDP?

While TCP provides built-in reliability, there are compelling reasons to implement reliability at the application layer over UDP:

- **Lower latency**: Avoid TCP's head-of-line blocking where one lost packet delays all subsequent data
- **Custom reliability**: Implement only the reliability features you need (partial reliability, selective retransmission)
- **Connection flexibility**: Handle multiple logical connections over one UDP socket
- **Congestion control tuning**: Implement domain-specific congestion control algorithms
- **NAT traversal**: UDP works better with hole-punching techniques

Common use cases include real-time gaming, video streaming (QUIC), VoIP, and financial trading systems.

## Core Reliability Mechanisms

### 1. Sequence Numbers

Every packet gets a unique sequence number to detect loss and reordering:

```c
typedef struct {
    uint32_t sequence;
    uint16_t ack;
    uint16_t ack_bits;  // Bitfield for selective ACK
    uint64_t timestamp;
    uint16_t data_length;
    uint8_t data[];
} udp_packet_t;
```

### 2. Acknowledgments (ACKs)

The receiver confirms packet receipt, allowing the sender to detect loss:

- **Immediate ACKs**: Send ACK for every received packet
- **Cumulative ACKs**: Acknowledge highest contiguous sequence received
- **Selective ACKs (SACK)**: Use bitfields to acknowledge non-contiguous packets

### 3. Retransmission

When packet loss is detected (via missing ACKs or explicit NAKs), retransmit the data:

- **Timeout-based**: Retransmit if ACK not received within RTO (Retransmission Timeout)
- **Fast retransmit**: Retransmit on duplicate ACKs
- **Selective retransmission**: Only resend unacknowledged packets

## Code Examples

### C Implementation: Basic Reliable UDP

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

#define MAX_PACKET_SIZE 1400
#define WINDOW_SIZE 32
#define TIMEOUT_MS 100
#define MAX_RETRIES 5

// Packet structure
typedef struct {
    uint32_t seq;
    uint32_t ack;
    uint32_t ack_bits;  // Selective ACK bitfield
    uint64_t timestamp;
    uint16_t data_len;
    uint8_t flags;
    uint8_t data[MAX_PACKET_SIZE];
} packet_t;

// Pending packet for retransmission
typedef struct {
    packet_t packet;
    uint64_t send_time;
    int retries;
    int acked;
} pending_packet_t;

// Reliable UDP context
typedef struct {
    int sockfd;
    struct sockaddr_in remote_addr;
    
    // Send state
    uint32_t send_seq;
    pending_packet_t send_window[WINDOW_SIZE];
    
    // Receive state
    uint32_t recv_seq;
    uint32_t recv_ack_bits;
    uint8_t recv_buffer[WINDOW_SIZE][MAX_PACKET_SIZE];
    int recv_lengths[WINDOW_SIZE];
} rudp_ctx_t;

// Get current time in milliseconds
uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Initialize reliable UDP context
rudp_ctx_t* rudp_init(const char* remote_ip, int remote_port) {
    rudp_ctx_t* ctx = calloc(1, sizeof(rudp_ctx_t));
    
    ctx->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->sockfd < 0) {
        perror("socket");
        free(ctx);
        return NULL;
    }
    
    // Set non-blocking
    struct timeval tv = {0, 10000}; // 10ms timeout
    setsockopt(ctx->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    ctx->remote_addr.sin_family = AF_INET;
    ctx->remote_addr.sin_port = htons(remote_port);
    inet_pton(AF_INET, remote_ip, &ctx->remote_addr.sin_addr);
    
    ctx->send_seq = 0;
    ctx->recv_seq = 0;
    ctx->recv_ack_bits = 0;
    
    return ctx;
}

// Send a packet with reliability
int rudp_send(rudp_ctx_t* ctx, const uint8_t* data, size_t len) {
    if (len > MAX_PACKET_SIZE) return -1;
    
    // Find available slot in send window
    int slot = ctx->send_seq % WINDOW_SIZE;
    pending_packet_t* pending = &ctx->send_window[slot];
    
    // Check if window is full
    if (!pending->acked && pending->packet.seq >= ctx->send_seq - WINDOW_SIZE) {
        return -2; // Window full
    }
    
    // Prepare packet
    packet_t* pkt = &pending->packet;
    pkt->seq = ctx->send_seq++;
    pkt->ack = ctx->recv_seq;
    pkt->ack_bits = ctx->recv_ack_bits;
    pkt->timestamp = get_time_ms();
    pkt->data_len = len;
    pkt->flags = 0;
    memcpy(pkt->data, data, len);
    
    // Send packet
    ssize_t sent = sendto(ctx->sockfd, pkt, 
                          sizeof(packet_t) - MAX_PACKET_SIZE + len,
                          0, (struct sockaddr*)&ctx->remote_addr, 
                          sizeof(ctx->remote_addr));
    
    if (sent < 0) {
        perror("sendto");
        return -1;
    }
    
    // Track for retransmission
    pending->send_time = pkt->timestamp;
    pending->retries = 0;
    pending->acked = 0;
    
    return 0;
}

// Process received ACKs
void rudp_process_ack(rudp_ctx_t* ctx, packet_t* pkt) {
    uint32_t ack = pkt->ack;
    uint32_t ack_bits = pkt->ack_bits;
    
    // Mark acknowledged packets
    for (int i = 0; i < WINDOW_SIZE; i++) {
        pending_packet_t* pending = &ctx->send_window[i];
        if (pending->acked) continue;
        
        uint32_t seq = pending->packet.seq;
        
        // Check if this sequence is acknowledged
        if (seq == ack) {
            pending->acked = 1;
        } else if (seq < ack && (ack - seq) <= 32) {
            // Check selective ACK bitfield
            int bit_pos = ack - seq - 1;
            if (ack_bits & (1 << bit_pos)) {
                pending->acked = 1;
            }
        }
    }
}

// Receive and process packets
int rudp_recv(rudp_ctx_t* ctx, uint8_t* buffer, size_t max_len) {
    packet_t pkt;
    struct sockaddr_in from_addr;
    socklen_t addr_len = sizeof(from_addr);
    
    ssize_t received = recvfrom(ctx->sockfd, &pkt, sizeof(pkt), 0,
                                (struct sockaddr*)&from_addr, &addr_len);
    
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // No data available
        }
        return -1;
    }
    
    // Process ACKs in received packet
    rudp_process_ack(ctx, &pkt);
    
    // Handle received data
    if (pkt.data_len > 0) {
        uint32_t seq_diff = pkt.seq - ctx->recv_seq;
        
        if (seq_diff == 0) {
            // Expected sequence - deliver immediately
            memcpy(buffer, pkt.data, pkt.data_len);
            ctx->recv_seq++;
            
            // Update ACK bitfield
            ctx->recv_ack_bits = 0;
            
            return pkt.data_len;
        } else if (seq_diff < WINDOW_SIZE) {
            // Out-of-order packet - buffer it
            int slot = pkt.seq % WINDOW_SIZE;
            memcpy(ctx->recv_buffer[slot], pkt.data, pkt.data_len);
            ctx->recv_lengths[slot] = pkt.data_len;
            
            // Set bit in ACK bitfield
            ctx->recv_ack_bits |= (1 << (seq_diff - 1));
        }
    }
    
    return 0;
}

// Retransmit timed-out packets
void rudp_retransmit(rudp_ctx_t* ctx) {
    uint64_t now = get_time_ms();
    
    for (int i = 0; i < WINDOW_SIZE; i++) {
        pending_packet_t* pending = &ctx->send_window[i];
        
        if (pending->acked) continue;
        if (pending->packet.seq >= ctx->send_seq) continue;
        
        // Check timeout
        if (now - pending->send_time > TIMEOUT_MS) {
            if (pending->retries >= MAX_RETRIES) {
                printf("Packet %u exceeded max retries\n", pending->packet.seq);
                pending->acked = 1; // Give up
                continue;
            }
            
            // Retransmit
            pending->packet.timestamp = now;
            sendto(ctx->sockfd, &pending->packet,
                   sizeof(packet_t) - MAX_PACKET_SIZE + pending->packet.data_len,
                   0, (struct sockaddr*)&ctx->remote_addr,
                   sizeof(ctx->remote_addr));
            
            pending->send_time = now;
            pending->retries++;
            
            printf("Retransmitting packet %u (attempt %d)\n", 
                   pending->packet.seq, pending->retries);
        }
    }
}

// Example usage
int main() {
    rudp_ctx_t* sender = rudp_init("127.0.0.1", 9999);
    if (!sender) return 1;
    
    const char* messages[] = {
        "Hello, reliable UDP!",
        "This is packet 2",
        "And packet 3",
        "Final packet"
    };
    
    // Send messages
    for (int i = 0; i < 4; i++) {
        if (rudp_send(sender, (uint8_t*)messages[i], strlen(messages[i])) < 0) {
            printf("Send failed for message %d\n", i);
        }
        usleep(10000); // 10ms delay
    }
    
    // Process ACKs and retransmissions
    for (int i = 0; i < 100; i++) {
        uint8_t recv_buf[MAX_PACKET_SIZE];
        int len = rudp_recv(sender, recv_buf, MAX_PACKET_SIZE);
        
        if (len > 0) {
            recv_buf[len] = '\0';
            printf("Received: %s\n", recv_buf);
        }
        
        rudp_retransmit(sender);
        usleep(10000); // 10ms
    }
    
    close(sender->sockfd);
    free(sender);
    
    return 0;
}
```

### C++ Implementation: Modern Reliable UDP with RAII

```cpp
#include <iostream>
#include <vector>
#include <queue>
#include <chrono>
#include <memory>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std::chrono;

constexpr size_t MAX_PACKET_SIZE = 1400;
constexpr size_t WINDOW_SIZE = 64;
constexpr auto TIMEOUT = milliseconds(100);
constexpr int MAX_RETRIES = 5;

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t sequence;
    uint32_t ack;
    uint64_t ack_bitfield;  // 64-bit selective ACK
    uint64_t timestamp_us;
    uint16_t data_length;
    uint8_t flags;
    uint8_t reserved;
};
#pragma pack(pop)

struct Packet {
    PacketHeader header;
    std::vector<uint8_t> data;
    
    Packet() = default;
    
    Packet(uint32_t seq, const uint8_t* buf, size_t len) {
        header.sequence = seq;
        header.data_length = static_cast<uint16_t>(len);
        header.flags = 0;
        data.assign(buf, buf + len);
    }
};

struct PendingPacket {
    Packet packet;
    steady_clock::time_point send_time;
    int retry_count = 0;
    bool acked = false;
};

class ReliableUDP {
private:
    int sockfd_;
    sockaddr_in remote_addr_;
    
    // Send state
    uint32_t send_sequence_ = 0;
    std::vector<PendingPacket> send_window_;
    
    // Receive state
    uint32_t recv_sequence_ = 0;
    uint64_t recv_ack_bitfield_ = 0;
    std::unordered_map<uint32_t, Packet> recv_buffer_;
    
    // Statistics
    struct Stats {
        uint64_t packets_sent = 0;
        uint64_t packets_received = 0;
        uint64_t retransmissions = 0;
        uint64_t acks_received = 0;
    } stats_;

public:
    ReliableUDP(const std::string& remote_ip, uint16_t remote_port) 
        : send_window_(WINDOW_SIZE) {
        
        sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd_ < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        // Set socket to non-blocking
        timeval tv{0, 10000}; // 10ms
        setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        remote_addr_.sin_family = AF_INET;
        remote_addr_.sin_port = htons(remote_port);
        inet_pton(AF_INET, remote_ip.c_str(), &remote_addr_.sin_addr);
    }
    
    ~ReliableUDP() {
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
    }
    
    // Send data reliably
    bool send(const uint8_t* data, size_t length) {
        if (length > MAX_PACKET_SIZE) return false;
        
        uint32_t slot = send_sequence_ % WINDOW_SIZE;
        auto& pending = send_window_[slot];
        
        // Check if window is full
        if (!pending.acked && 
            pending.packet.header.sequence >= send_sequence_ - WINDOW_SIZE) {
            return false; // Window full
        }
        
        // Create packet
        pending.packet = Packet(send_sequence_++, data, length);
        pending.packet.header.ack = recv_sequence_;
        pending.packet.header.ack_bitfield = recv_ack_bitfield_;
        pending.packet.header.timestamp_us = 
            duration_cast<microseconds>(
                steady_clock::now().time_since_epoch()
            ).count();
        
        // Send packet
        if (!transmitPacket(pending.packet)) {
            return false;
        }
        
        pending.send_time = steady_clock::now();
        pending.retry_count = 0;
        pending.acked = false;
        
        stats_.packets_sent++;
        return true;
    }
    
    // Receive data
    std::vector<uint8_t> receive() {
        std::vector<uint8_t> result;
        
        // Receive from socket
        std::vector<uint8_t> buffer(sizeof(PacketHeader) + MAX_PACKET_SIZE);
        sockaddr_in from_addr;
        socklen_t addr_len = sizeof(from_addr);
        
        ssize_t received = recvfrom(sockfd_, buffer.data(), buffer.size(), 0,
                                    reinterpret_cast<sockaddr*>(&from_addr),
                                    &addr_len);
        
        if (received < static_cast<ssize_t>(sizeof(PacketHeader))) {
            return result; // No valid packet
        }
        
        // Parse packet
        Packet pkt;
        memcpy(&pkt.header, buffer.data(), sizeof(PacketHeader));
        
        if (pkt.header.data_length > 0) {
            pkt.data.assign(buffer.begin() + sizeof(PacketHeader),
                           buffer.begin() + sizeof(PacketHeader) + 
                           pkt.header.data_length);
        }
        
        // Process ACKs
        processAcknowledgments(pkt.header);
        
        // Handle received data
        if (!pkt.data.empty()) {
            stats_.packets_received++;
            result = processReceivedPacket(std::move(pkt));
        }
        
        return result;
    }
    
    // Retransmit timed-out packets
    void processRetransmissions() {
        auto now = steady_clock::now();
        
        for (auto& pending : send_window_) {
            if (pending.acked) continue;
            if (pending.packet.header.sequence >= send_sequence_) continue;
            
            auto elapsed = duration_cast<milliseconds>(now - pending.send_time);
            
            if (elapsed > TIMEOUT) {
                if (pending.retry_count >= MAX_RETRIES) {
                    std::cerr << "Packet " << pending.packet.header.sequence 
                              << " exceeded max retries\n";
                    pending.acked = true; // Give up
                    continue;
                }
                
                // Retransmit
                pending.packet.header.timestamp_us = 
                    duration_cast<microseconds>(
                        now.time_since_epoch()
                    ).count();
                
                if (transmitPacket(pending.packet)) {
                    pending.send_time = now;
                    pending.retry_count++;
                    stats_.retransmissions++;
                    
                    std::cout << "Retransmitting seq=" 
                              << pending.packet.header.sequence 
                              << " (attempt " << pending.retry_count << ")\n";
                }
            }
        }
    }
    
    // Get statistics
    const Stats& getStats() const { return stats_; }
    
    void printStats() const {
        std::cout << "\n=== Reliable UDP Statistics ===\n"
                  << "Packets sent: " << stats_.packets_sent << "\n"
                  << "Packets received: " << stats_.packets_received << "\n"
                  << "Retransmissions: " << stats_.retransmissions << "\n"
                  << "ACKs received: " << stats_.acks_received << "\n"
                  << "Loss rate: " 
                  << (stats_.packets_sent > 0 ? 
                      (100.0 * stats_.retransmissions / stats_.packets_sent) : 0)
                  << "%\n";
    }

private:
    bool transmitPacket(const Packet& pkt) {
        std::vector<uint8_t> buffer(sizeof(PacketHeader) + pkt.data.size());
        memcpy(buffer.data(), &pkt.header, sizeof(PacketHeader));
        memcpy(buffer.data() + sizeof(PacketHeader), 
               pkt.data.data(), pkt.data.size());
        
        ssize_t sent = sendto(sockfd_, buffer.data(), buffer.size(), 0,
                              reinterpret_cast<const sockaddr*>(&remote_addr_),
                              sizeof(remote_addr_));
        
        return sent == static_cast<ssize_t>(buffer.size());
    }
    
    void processAcknowledgments(const PacketHeader& header) {
        stats_.acks_received++;
        
        for (auto& pending : send_window_) {
            if (pending.acked) continue;
            
            uint32_t seq = pending.packet.header.sequence;
            
            // Check direct ACK
            if (seq == header.ack) {
                pending.acked = true;
            }
            // Check selective ACK bitfield
            else if (seq < header.ack && (header.ack - seq) <= 64) {
                int bit_pos = header.ack - seq - 1;
                if (header.ack_bitfield & (1ULL << bit_pos)) {
                    pending.acked = true;
                }
            }
        }
    }
    
    std::vector<uint8_t> processReceivedPacket(Packet pkt) {
        uint32_t seq = pkt.header.sequence;
        
        if (seq == recv_sequence_) {
            // In-order packet - deliver immediately
            recv_sequence_++;
            recv_ack_bitfield_ = 0;
            
            // Check if buffered packets can now be delivered
            // (In a complete implementation, would deliver buffered packets)
            
            return std::move(pkt.data);
        }
        else if (seq > recv_sequence_ && seq < recv_sequence_ + 64) {
            // Out-of-order packet - buffer it
            recv_buffer_[seq] = std::move(pkt);
            
            // Update ACK bitfield
            uint32_t bit_pos = seq - recv_sequence_ - 1;
            recv_ack_bitfield_ |= (1ULL << bit_pos);
        }
        
        return {};
    }
};

// Example usage
int main() {
    try {
        ReliableUDP rudp("127.0.0.1", 9999);
        
        // Send test messages
        std::vector<std::string> messages = {
            "Hello, reliable UDP with C++!",
            "This is message number 2",
            "Third message incoming",
            "Fourth and final test message"
        };
        
        for (size_t i = 0; i < messages.size(); ++i) {
            const auto& msg = messages[i];
            if (!rudp.send(reinterpret_cast<const uint8_t*>(msg.data()), 
                          msg.length())) {
                std::cerr << "Failed to send message " << i << "\n";
            }
            std::this_thread::sleep_for(milliseconds(10));
        }
        
        // Process for a while
        for (int i = 0; i < 100; ++i) {
            auto data = rudp.receive();
            if (!data.empty()) {
                std::string msg(data.begin(), data.end());
                std::cout << "Received: " << msg << "\n";
            }
            
            rudp.processRetransmissions();
            std::this_thread::sleep_for(milliseconds(10));
        }
        
        rudp.printStats();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
```

### Rust Implementation: Safe and Modern

```rust
use std::collections::{HashMap, VecDeque};
use std::net::{UdpSocket, SocketAddr};
use std::time::{Duration, Instant};
use std::io::{self, ErrorKind};

const MAX_PACKET_SIZE: usize = 1400;
const WINDOW_SIZE: usize = 64;
const TIMEOUT: Duration = Duration::from_millis(100);
const MAX_RETRIES: u8 = 5;

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct PacketHeader {
    sequence: u32,
    ack: u32,
    ack_bitfield: u64,
    timestamp_us: u64,
    data_length: u16,
    flags: u8,
    reserved: u8,
}

#[derive(Debug, Clone)]
struct Packet {
    header: PacketHeader,
    data: Vec<u8>,
}

impl Packet {
    fn new(sequence: u32, data: &[u8]) -> Self {
        Self {
            header: PacketHeader {
                sequence,
                ack: 0,
                ack_bitfield: 0,
                timestamp_us: 0,
                data_length: data.len() as u16,
                flags: 0,
                reserved: 0,
            },
            data: data.to_vec(),
        }
    }
    
    fn to_bytes(&self) -> Vec<u8> {
        let header_bytes = unsafe {
            std::slice::from_raw_parts(
                &self.header as *const _ as *const u8,
                std::mem::size_of::<PacketHeader>(),
            )
        };
        
        let mut bytes = Vec::with_capacity(header_bytes.len() + self.data.len());
        bytes.extend_from_slice(header_bytes);
        bytes.extend_from_slice(&self.data);
        bytes
    }
    
    fn from_bytes(bytes: &[u8]) -> io::Result<Self> {
        if bytes.len() < std::mem::size_of::<PacketHeader>() {
            return Err(io::Error::new(ErrorKind::InvalidData, "Packet too small"));
        }
        
        let header = unsafe {
            std::ptr::read_unaligned(bytes.as_ptr() as *const PacketHeader)
        };
        
        let data_start = std::mem::size_of::<PacketHeader>();
        let data_end = data_start + header.data_length as usize;
        
        if bytes.len() < data_end {
            return Err(io::Error::new(ErrorKind::InvalidData, "Incomplete packet"));
        }
        
        Ok(Self {
            header,
            data: bytes[data_start..data_end].to_vec(),
        })
    }
}

#[derive(Debug)]
struct PendingPacket {
    packet: Packet,
    send_time: Instant,
    retry_count: u8,
    acked: bool,
}

#[derive(Debug, Default)]
struct Statistics {
    packets_sent: u64,
    packets_received: u64,
    retransmissions: u64,
    acks_received: u64,
    packets_dropped: u64,
}

pub struct ReliableUdp {
    socket: UdpSocket,
    remote_addr: SocketAddr,
    
    // Send state
    send_sequence: u32,
    send_window: VecDeque<PendingPacket>,
    
    // Receive state
    recv_sequence: u32,
    recv_ack_bitfield: u64,
    recv_buffer: HashMap<u32, Packet>,
    
    // Statistics
    stats: Statistics,
}

impl ReliableUdp {
    pub fn new(local_addr: &str, remote_addr: &str) -> io::Result<Self> {
        let socket = UdpSocket::bind(local_addr)?;
        socket.set_nonblocking(true)?;
        socket.set_read_timeout(Some(Duration::from_millis(10)))?;
        
        let remote: SocketAddr = remote_addr.parse()
            .map_err(|e| io::Error::new(ErrorKind::InvalidInput, e))?;
        
        let mut send_window = VecDeque::with_capacity(WINDOW_SIZE);
        for _ in 0..WINDOW_SIZE {
            send_window.push_back(PendingPacket {
                packet: Packet::new(0, &[]),
                send_time: Instant::now(),
                retry_count: 0,
                acked: true,
            });
        }
        
        Ok(Self {
            socket,
            remote_addr: remote,
            send_sequence: 0,
            send_window,
            recv_sequence: 0,
            recv_ack_bitfield: 0,
            recv_buffer: HashMap::new(),
            stats: Statistics::default(),
        })
    }
    
    pub fn send(&mut self, data: &[u8]) -> io::Result<()> {
        if data.len() > MAX_PACKET_SIZE {
            return Err(io::Error::new(
                ErrorKind::InvalidInput,
                "Data exceeds max packet size"
            ));
        }
        
        let slot = (self.send_sequence as usize) % WINDOW_SIZE;
        let pending = &self.send_window[slot];
        
        // Check if window is full
        if !pending.acked && 
           pending.packet.header.sequence >= self.send_sequence.saturating_sub(WINDOW_SIZE as u32) {
            return Err(io::Error::new(ErrorKind::WouldBlock, "Send window full"));
        }
        
        // Create new packet
        let mut packet = Packet::new(self.send_sequence, data);
        packet.header.ack = self.recv_sequence;
        packet.header.ack_bitfield = self.recv_ack_bitfield;
        packet.header.timestamp_us = Self::get_timestamp_us();
        
        self.send_sequence += 1;
        
        // Transmit packet
        self.transmit_packet(&packet)?;
        
        // Store in send window
        let pending = &mut self.send_window[slot];
        pending.packet = packet;
        pending.send_time = Instant::now();
        pending.retry_count = 0;
        pending.acked = false;
        
        self.stats.packets_sent += 1;
        
        Ok(())
    }
    
    pub fn receive(&mut self) -> io::Result<Option<Vec<u8>>> {
        let mut buffer = vec![0u8; MAX_PACKET_SIZE + 256];
        
        match self.socket.recv_from(&mut buffer) {
            Ok((size, _addr)) => {
                let packet = Packet::from_bytes(&buffer[..size])?;
                
                // Process ACKs
                self.process_acknowledgments(&packet.header);
                
                // Handle received data
                if !packet.data.is_empty() {
                    self.stats.packets_received += 1;
                    return Ok(self.process_received_packet(packet));
                }
                
                Ok(None)
            }
            Err(e) if e.kind() == ErrorKind::WouldBlock => Ok(None),
            Err(e) => Err(e),
        }
    }
    
    pub fn process_retransmissions(&mut self) -> io::Result<()> {
        let now = Instant::now();
        
        for i in 0..WINDOW_SIZE {
            let pending = &self.send_window[i];
            
            if pending.acked || pending.packet.header.sequence >= self.send_sequence {
                continue;
            }
            
            let elapsed = now.duration_since(pending.send_time);
            
            if elapsed > TIMEOUT {
                if pending.retry_count >= MAX_RETRIES {
                    println!("Packet {} exceeded max retries, dropping",
                            pending.packet.header.sequence);
                    self.send_window[i].acked = true;
                    self.stats.packets_dropped += 1;
                    continue;
                }
                
                // Update timestamp and retransmit
                let mut packet = pending.packet.clone();
                packet.header.timestamp_us = Self::get_timestamp_us();
                
                if let Err(e) = self.transmit_packet(&packet) {
                    eprintln!("Retransmission failed: {}", e);
                    continue;
                }
                
                self.send_window[i].packet = packet;
                self.send_window[i].send_time = now;
                self.send_window[i].retry_count += 1;
                self.stats.retransmissions += 1;
                
                println!("Retransmitting seq={} (attempt {})",
                        self.send_window[i].packet.header.sequence,
                        self.send_window[i].retry_count);
            }
        }
        
        Ok(())
    }
    
    pub fn stats(&self) -> &Statistics {
        &self.stats
    }
    
    pub fn print_stats(&self) {
        println!("\n=== Reliable UDP Statistics ===");
        println!("Packets sent: {}", self.stats.packets_sent);
        println!("Packets received: {}", self.stats.packets_received);
        println!("Retransmissions: {}", self.stats.retransmissions);
        println!("ACKs received: {}", self.stats.acks_received);
        println!("Packets dropped: {}", self.stats.packets_dropped);
        
        if self.stats.packets_sent > 0 {
            let loss_rate = (100.0 * self.stats.retransmissions as f64) 
                          / self.stats.packets_sent as f64;
            println!("Loss rate: {:.2}%", loss_rate);
        }
    }
    
    fn transmit_packet(&self, packet: &Packet) -> io::Result<()> {
        let bytes = packet.to_bytes();
        self.socket.send_to(&bytes, self.remote_addr)?;
        Ok(())
    }
    
    fn process_acknowledgments(&mut self, header: &PacketHeader) {
        self.stats.acks_received += 1;
        
        for pending in &mut self.send_window {
            if pending.acked {
                continue;
            }
            
            let seq = pending.packet.header.sequence;
            
            // Direct ACK
            if seq == header.ack {
                pending.acked = true;
            }
            // Selective ACK bitfield
            else if seq < header.ack && (header.ack - seq) <= 64 {
                let bit_pos = header.ack - seq - 1;
                if (header.ack_bitfield & (1u64 << bit_pos)) != 0 {
                    pending.acked = true;
                }
            }
        }
    }
    
    fn process_received_packet(&mut self, packet: Packet) -> Option<Vec<u8>> {
        let seq = packet.header.sequence;
        
        if seq == self.recv_sequence {
            // In-order packet - deliver immediately
            self.recv_sequence += 1;
            self.recv_ack_bitfield = 0;
            
            // Try to deliver buffered packets
            let mut result = vec![packet.data];
            while let Some(buffered) = self.recv_buffer.remove(&self.recv_sequence) {
                result.push(buffered.data);
                self.recv_sequence += 1;
            }
            
            // Return first packet (simplified)
            return Some(result.into_iter().next().unwrap());
        }
        else if seq > self.recv_sequence && seq < self.recv_sequence + 64 {
            // Out-of-order packet - buffer it
            let bit_pos = seq - self.recv_sequence - 1;
            self.recv_ack_bitfield |= 1u64 << bit_pos;
            self.recv_buffer.insert(seq, packet);
        }
        
        None
    }
    
    fn get_timestamp_us() -> u64 {
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_micros() as u64
    }
}

fn main() -> io::Result<()> {
    let mut rudp = ReliableUdp::new("127.0.0.1:0", "127.0.0.1:9999")?;
    
    let messages = vec![
        "Hello from Rust reliable UDP!",
        "Second message with reliability",
        "Third packet incoming",
        "Final test message",
    ];
    
    // Send messages
    for (i, msg) in messages.iter().enumerate() {
        match rudp.send(msg.as_bytes()) {
            Ok(_) => println!("Sent message {}", i),
            Err(e) => eprintln!("Failed to send message {}: {}", i, e),
        }
        std::thread::sleep(Duration::from_millis(10));
    }
    
    // Process for a while
    for _ in 0..100 {
        if let Some(data) = rudp.receive()? {
            if let Ok(msg) = String::from_utf8(data) {
                println!("Received: {}", msg);
            }
        }
        
        rudp.process_retransmissions()?;
        std::thread::sleep(Duration::from_millis(10));
    }
    
    rudp.print_stats();
    
    Ok(())
}
```

## Advanced Reliability Patterns

### 1. **Sliding Window Protocol**
Allows multiple unacknowledged packets in flight, improving throughput over stop-and-wait.

**Key concepts:**
- Send window: Range of sequences that can be transmitted
- Receive window: Range of sequences that can be accepted
- Window advancement: Moves forward as ACKs arrive

### 2. **Selective Acknowledgment (SACK)**
Uses bitfields to acknowledge non-contiguous sequences, enabling efficient selective retransmission. In the examples above, a 32-bit or 64-bit field acknowledges up to 32 or 64 packets relative to the ACK sequence.

### 3. **Fast Retransmit**
Retransmits immediately upon receiving duplicate ACKs rather than waiting for timeout, reducing latency.

### 4. **Congestion Control**
Implement basic congestion control to avoid overwhelming the network:

```cpp
class CongestionController {
    size_t cwnd = 1;          // Congestion window
    size_t ssthresh = 64;     // Slow start threshold
    
    void onAck() {
        if (cwnd < ssthresh) {
            cwnd *= 2;  // Slow start
        } else {
            cwnd += 1;  // Congestion avoidance
        }
    }
    
    void onLoss() {
        ssthresh = cwnd / 2;
        cwnd = 1;  // Back to slow start
    }
};
```

### 5. **RTT Estimation and Adaptive Timeout**
Dynamically adjust timeout based on measured round-trip time:

```rust
struct RttEstimator {
    srtt: f64,  // Smoothed RTT
    rttvar: f64, // RTT variation
}

impl RttEstimator {
    fn update(&mut self, measured_rtt: f64) {
        const ALPHA: f64 = 0.125;
        const BETA: f64 = 0.25;
        
        self.rttvar = (1.0 - BETA) * self.rttvar + 
                      BETA * (self.srtt - measured_rtt).abs();
        self.srtt = (1.0 - ALPHA) * self.srtt + ALPHA * measured_rtt;
    }
    
    fn timeout(&self) -> Duration {
        Duration::from_secs_f64(self.srtt + 4.0 * self.rttvar)
    }
}
```

### 6. **Forward Error Correction (FEC)**
Send redundant data so receivers can recover from loss without retransmission:

```c
// Simple XOR-based FEC
void create_fec_packet(uint8_t* packets[], int n, uint8_t* fec_packet) {
    memset(fec_packet, 0, PACKET_SIZE);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < PACKET_SIZE; j++) {
            fec_packet[j] ^= packets[i][j];
        }
    }
}
```

## Real-World Protocols Using UDP Reliability

**QUIC (Quick UDP Internet Connections)**: Modern transport protocol using UDP with built-in reliability, used by HTTP/3. Features include stream multiplexing, 0-RTT connection establishment, and advanced loss recovery.

**RTP (Real-time Transport Protocol)**: For voice and video, uses sequence numbers but often accepts some packet loss rather than retransmitting old data.

**RUDP (Reliable UDP)**: Various implementations exist for different use cases like gaming (ENet), VoIP (SIP over UDP), and IoT protocols (CoAP).

**Custom Game Protocols**: Games like Fortnite and Call of Duty use custom UDP reliability with application-specific prioritization—position updates are critical, while cosmetic effects can be dropped.

## Summary

Building reliability on top of UDP gives you fine-grained control over the trade-offs between latency, bandwidth, and reliability. The key mechanisms are:

1. **Sequence numbers** for detecting loss and reordering
2. **Acknowledgments** (cumulative or selective) to confirm delivery
3. **Retransmission** with timeout or fast retransmit strategies
4. **Sliding windows** to maintain throughput with multiple in-flight packets
5. **RTT estimation** for adaptive timeouts
6. **Congestion control** to be a good network citizen

The code examples demonstrate practical implementations in C, C++, and Rust, showing how to structure the packet format, manage send/receive windows, handle acknowledgments, and implement retransmission logic. Each implementation includes selective acknowledgment using bitfields, timeout-based retransmission, and basic statistics tracking.

When building your own reliable UDP protocol, start simple with stop-and-wait, then progressively add sliding windows, selective ACK, and congestion control as needed. Always measure and optimize for your specific use case—sometimes partial reliability or unordered delivery is acceptable and leads to better performance than TCP's strict guarantees.