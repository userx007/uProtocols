#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>

#define MAX_HOPS 30
#define PACKET_SIZE 64
#define TIMEOUT_SEC 2
#define DEST_PORT 33434  // Standard traceroute port

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

// Send UDP probe packet with specific TTL
int send_probe(int sockfd, struct sockaddr_in *addr, int ttl, int seq) {
    char packet[PACKET_SIZE];
    
    // Set TTL for this packet
    if (setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("setsockopt TTL failed");
        return -1;
    }
    
    memset(packet, 0, PACKET_SIZE);
    
    // Fill with sequence number for identification
    *(int *)packet = seq;
    
    struct sockaddr_in dest = *addr;
    dest.sin_port = htons(DEST_PORT + seq);
    
    if (sendto(sockfd, packet, PACKET_SIZE, 0, 
               (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        perror("sendto failed");
        return -1;
    }
    
    return 0;
}

// Receive ICMP response (Time Exceeded or Dest Unreachable)
int receive_response(int icmp_sockfd, struct sockaddr_in *from_addr, 
                     double *rtt, int expected_seq) {
    char buffer[1024];
    struct timeval tv_start, tv_end;
    socklen_t addr_len = sizeof(*from_addr);
    
    gettimeofday(&tv_start, NULL);
    
    int n = recvfrom(icmp_sockfd, buffer, sizeof(buffer), 0,
                     (struct sockaddr *)from_addr, &addr_len);
    
    gettimeofday(&tv_end, NULL);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1;  // Timeout
        }
        perror("recvfrom failed");
        return -1;
    }
    
    // Calculate RTT
    *rtt = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0 +
           (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
    
    // Parse IP header
    struct ip *ip_hdr = (struct ip *)buffer;
    int ip_hdr_len = ip_hdr->ip_hl << 2;
    
    // Parse ICMP header
    struct icmp *icmp_hdr = (struct icmp *)(buffer + ip_hdr_len);
    
    // Check ICMP type
    if (icmp_hdr->icmp_type == ICMP_TIMXCEED) {
        return 0;  // Time exceeded - intermediate router
    } else if (icmp_hdr->icmp_type == ICMP_UNREACH) {
        return 1;  // Destination unreachable - final destination
    }
    
    return -1;  // Other ICMP type
}

// Resolve IP address to hostname
char* reverse_dns(struct in_addr addr) {
    static char hostname[256];
    struct hostent *host;
    
    host = gethostbyaddr(&addr, sizeof(addr), AF_INET);
    
    if (host && host->h_name) {
        snprintf(hostname, sizeof(hostname), "%s (%s)", 
                 host->h_name, inet_ntoa(addr));
    } else {
        snprintf(hostname, sizeof(hostname), "%s", inet_ntoa(addr));
    }
    
    return hostname;
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
    
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr = *(struct in_addr *)host->h_addr;
    
    printf("traceroute to %s (%s), %d hops max, %d byte packets\n",
           argv[1], inet_ntoa(dest_addr.sin_addr), MAX_HOPS, PACKET_SIZE);
    
    // Create UDP socket for sending probes
    int udp_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sockfd < 0) {
        perror("UDP socket creation failed");
        return 1;
    }
    
    // Create raw ICMP socket for receiving responses (requires root)
    int icmp_sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (icmp_sockfd < 0) {
        perror("ICMP socket creation failed (need root privileges)");
        close(udp_sockfd);
        return 1;
    }
    
    // Set timeout on ICMP socket
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(icmp_sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Traceroute loop
    int reached_dest = 0;
    
    for (int ttl = 1; ttl <= MAX_HOPS && !reached_dest; ttl++) {
        printf("%2d  ", ttl);
        fflush(stdout);
        
        int got_response = 0;
        struct sockaddr_in from_addr;
        double rtt;
        
        // Send 3 probes per hop
        for (int probe = 0; probe < 3; probe++) {
            int seq = ttl * 1000 + probe;
            
            if (send_probe(udp_sockfd, &dest_addr, ttl, seq) < 0) {
                printf("* ");
                continue;
            }
            
            int result = receive_response(icmp_sockfd, &from_addr, &rtt, seq);
            
            if (result >= 0) {
                if (!got_response) {
                    // Print hostname/IP for first response
                    char *hostname = reverse_dns(from_addr.sin_addr);
                    printf("%s  ", hostname);
                    got_response = 1;
                }
                
                printf("%.3f ms  ", rtt);
                
                // Check if we reached destination
                if (result == 1 || 
                    from_addr.sin_addr.s_addr == dest_addr.sin_addr.s_addr) {
                    reached_dest = 1;
                }
            } else {
                printf("* ");
            }
            
            fflush(stdout);
        }
        
        printf("\n");
    }
    
    close(udp_sockfd);
    close(icmp_sockfd);
    
    return 0;
}