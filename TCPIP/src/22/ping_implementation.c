#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>

#define PACKET_SIZE 64
#define TIMEOUT_SEC 2

// Calculate checksum for ICMP packet
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

// Send ICMP echo request
int send_ping(int sockfd, struct sockaddr_in *addr, int seq) {
    char packet[PACKET_SIZE];
    struct icmp *icmp_hdr = (struct icmp *)packet;
    
    memset(packet, 0, PACKET_SIZE);
    
    // Fill in ICMP header
    icmp_hdr->icmp_type = ICMP_ECHO;
    icmp_hdr->icmp_code = 0;
    icmp_hdr->icmp_id = getpid();
    icmp_hdr->icmp_seq = seq;
    
    // Fill data section with pattern
    for (int i = sizeof(struct icmp); i < PACKET_SIZE; i++)
        packet[i] = i;
    
    // Calculate checksum
    icmp_hdr->icmp_cksum = 0;
    icmp_hdr->icmp_cksum = checksum(packet, PACKET_SIZE);
    
    // Send packet
    if (sendto(sockfd, packet, PACKET_SIZE, 0, 
               (struct sockaddr *)addr, sizeof(*addr)) <= 0) {
        perror("sendto failed");
        return -1;
    }
    
    return 0;
}

// Receive ICMP echo reply
int receive_ping(int sockfd, struct sockaddr_in *addr, double *rtt) {
    char buffer[1024];
    struct timeval tv_start, tv_end;
    socklen_t addr_len = sizeof(*addr);
    
    gettimeofday(&tv_start, NULL);
    
    int n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                     (struct sockaddr *)addr, &addr_len);
    
    gettimeofday(&tv_end, NULL);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("Request timeout\n");
            return -1;
        }
        perror("recvfrom failed");
        return -1;
    }
    
    // Calculate round-trip time
    *rtt = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0 +
           (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
    
    // Extract IP and ICMP headers
    struct ip *ip_hdr = (struct ip *)buffer;
    int ip_hdr_len = ip_hdr->ip_hl << 2;
    struct icmp *icmp_hdr = (struct icmp *)(buffer + ip_hdr_len);
    
    // Verify it's an echo reply for our process
    if (icmp_hdr->icmp_type == ICMP_ECHOREPLY && 
        icmp_hdr->icmp_id == getpid()) {
        return icmp_hdr->icmp_seq;
    }
    
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <hostname>\n", argv[0]);
        return 1;
    }
    
    // Resolve hostname
    struct hostent *host = gethostbyname(argv[1]);
    if (!host) {
        fprintf(stderr, "Unknown host: %s\n", argv[1]);
        return 1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = *(struct in_addr *)host->h_addr;
    
    printf("PING %s (%s): %d data bytes\n", 
           argv[1], inet_ntoa(addr.sin_addr), PACKET_SIZE);
    
    // Create raw socket (requires root/CAP_NET_RAW)
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket failed (need root privileges)");
        return 1;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Send pings
    int packets_sent = 0, packets_received = 0;
    double total_rtt = 0.0;
    
    for (int seq = 0; seq < 4; seq++) {
        if (send_ping(sockfd, &addr, seq) < 0)
            continue;
        
        packets_sent++;
        
        double rtt;
        int recv_seq = receive_ping(sockfd, &addr, &rtt);
        
        if (recv_seq >= 0) {
            packets_received++;
            total_rtt += rtt;
            printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%.3f ms\n",
                   PACKET_SIZE, inet_ntoa(addr.sin_addr), recv_seq, 64, rtt);
        }
        
        sleep(1);
    }
    
    // Print statistics
    printf("\n--- %s ping statistics ---\n", argv[1]);
    printf("%d packets transmitted, %d received, %.0f%% packet loss\n",
           packets_sent, packets_received, 
           100.0 * (packets_sent - packets_received) / packets_sent);
    
    if (packets_received > 0) {
        printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n",
               total_rtt / packets_received,
               total_rtt / packets_received,
               total_rtt / packets_received);
    }
    
    close(sockfd);
    return 0;
}