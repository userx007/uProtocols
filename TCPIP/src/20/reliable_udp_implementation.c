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