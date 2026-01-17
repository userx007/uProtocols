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