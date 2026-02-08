/*
 * ARP Protocol Implementation in C
 * Demonstrates ARP packet creation, parsing, and cache management
 * Requires root privileges and raw socket access
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <time.h>

// ARP Header Structure (28 bytes for IPv4 over Ethernet)
struct arp_header {
    uint16_t hw_type;           // Hardware type (1 = Ethernet)
    uint16_t proto_type;        // Protocol type (0x0800 = IPv4)
    uint8_t  hw_addr_len;       // Hardware address length (6 for MAC)
    uint8_t  proto_addr_len;    // Protocol address length (4 for IPv4)
    uint16_t operation;         // Operation (1 = Request, 2 = Reply)
    uint8_t  sender_hw_addr[6]; // Sender MAC address
    uint8_t  sender_proto_addr[4]; // Sender IP address
    uint8_t  target_hw_addr[6]; // Target MAC address
    uint8_t  target_proto_addr[4]; // Target IP address
} __attribute__((packed));

// Complete Ethernet frame with ARP payload
struct arp_packet {
    struct ethhdr eth_header;
    struct arp_header arp;
} __attribute__((packed));

// ARP Cache Entry
typedef struct arp_cache_entry {
    uint32_t ip_addr;
    uint8_t mac_addr[6];
    time_t timestamp;
    int is_static;
    struct arp_cache_entry *next;
} arp_cache_entry_t;

// ARP Cache
typedef struct {
    arp_cache_entry_t *head;
    int timeout_seconds;
} arp_cache_t;

// Initialize ARP cache
arp_cache_t* arp_cache_init(int timeout) {
    arp_cache_t *cache = malloc(sizeof(arp_cache_t));
    cache->head = NULL;
    cache->timeout_seconds = timeout;
    return cache;
}

// Add entry to ARP cache
void arp_cache_add(arp_cache_t *cache, uint32_t ip, uint8_t *mac, int is_static) {
    // Check if entry exists
    arp_cache_entry_t *current = cache->head;
    while (current) {
        if (current->ip_addr == ip) {
            // Update existing entry
            memcpy(current->mac_addr, mac, 6);
            current->timestamp = time(NULL);
            current->is_static = is_static;
            return;
        }
        current = current->next;
    }
    
    // Create new entry
    arp_cache_entry_t *entry = malloc(sizeof(arp_cache_entry_t));
    entry->ip_addr = ip;
    memcpy(entry->mac_addr, mac, 6);
    entry->timestamp = time(NULL);
    entry->is_static = is_static;
    entry->next = cache->head;
    cache->head = entry;
}

// Lookup MAC address in cache
int arp_cache_lookup(arp_cache_t *cache, uint32_t ip, uint8_t *mac_out) {
    time_t now = time(NULL);
    arp_cache_entry_t *current = cache->head;
    
    while (current) {
        if (current->ip_addr == ip) {
            // Check if entry is expired
            if (current->is_static || 
                (now - current->timestamp) < cache->timeout_seconds) {
                memcpy(mac_out, current->mac_addr, 6);
                return 1; // Found
            }
        }
        current = current->next;
    }
    return 0; // Not found
}

// Remove expired entries
void arp_cache_cleanup(arp_cache_t *cache) {
    time_t now = time(NULL);
    arp_cache_entry_t *current = cache->head;
    arp_cache_entry_t *prev = NULL;
    
    while (current) {
        if (!current->is_static && 
            (now - current->timestamp) >= cache->timeout_seconds) {
            // Remove expired entry
            if (prev) {
                prev->next = current->next;
            } else {
                cache->head = current->next;
            }
            arp_cache_entry_t *to_free = current;
            current = current->next;
            free(to_free);
        } else {
            prev = current;
            current = current->next;
        }
    }
}

// Print ARP cache
void arp_cache_print(arp_cache_t *cache) {
    printf("\nARP Cache Contents:\n");
    printf("%-15s %-17s %-10s\n", "IP Address", "MAC Address", "Type");
    printf("================================================\n");
    
    arp_cache_entry_t *current = cache->head;
    while (current) {
        struct in_addr ip;
        ip.s_addr = current->ip_addr;
        printf("%-15s %02x:%02x:%02x:%02x:%02x:%02x %-10s\n",
               inet_ntoa(ip),
               current->mac_addr[0], current->mac_addr[1],
               current->mac_addr[2], current->mac_addr[3],
               current->mac_addr[4], current->mac_addr[5],
               current->is_static ? "Static" : "Dynamic");
        current = current->next;
    }
}

// Create ARP request packet
void create_arp_request(struct arp_packet *packet,
                       const char *src_ip,
                       const uint8_t *src_mac,
                       const char *target_ip) {
    // Ethernet header
    memset(packet->eth_header.h_dest, 0xff, 6); // Broadcast MAC
    memcpy(packet->eth_header.h_source, src_mac, 6);
    packet->eth_header.h_proto = htons(ETH_P_ARP);
    
    // ARP header
    packet->arp.hw_type = htons(1); // Ethernet
    packet->arp.proto_type = htons(ETH_P_IP);
    packet->arp.hw_addr_len = 6;
    packet->arp.proto_addr_len = 4;
    packet->arp.operation = htons(1); // ARP Request
    
    // Sender addresses
    memcpy(packet->arp.sender_hw_addr, src_mac, 6);
    inet_pton(AF_INET, src_ip, packet->arp.sender_proto_addr);
    
    // Target addresses
    memset(packet->arp.target_hw_addr, 0, 6); // Unknown
    inet_pton(AF_INET, target_ip, packet->arp.target_proto_addr);
}

// Create ARP reply packet
void create_arp_reply(struct arp_packet *packet,
                     const char *src_ip,
                     const uint8_t *src_mac,
                     const char *target_ip,
                     const uint8_t *target_mac) {
    // Ethernet header
    memcpy(packet->eth_header.h_dest, target_mac, 6);
    memcpy(packet->eth_header.h_source, src_mac, 6);
    packet->eth_header.h_proto = htons(ETH_P_ARP);
    
    // ARP header
    packet->arp.hw_type = htons(1);
    packet->arp.proto_type = htons(ETH_P_IP);
    packet->arp.hw_addr_len = 6;
    packet->arp.proto_addr_len = 4;
    packet->arp.operation = htons(2); // ARP Reply
    
    // Sender addresses
    memcpy(packet->arp.sender_hw_addr, src_mac, 6);
    inet_pton(AF_INET, src_ip, packet->arp.sender_proto_addr);
    
    // Target addresses
    memcpy(packet->arp.target_hw_addr, target_mac, 6);
    inet_pton(AF_INET, target_ip, packet->arp.target_proto_addr);
}

// Create Gratuitous ARP packet
void create_gratuitous_arp(struct arp_packet *packet,
                          const char *ip,
                          const uint8_t *mac) {
    // Ethernet header - broadcast
    memset(packet->eth_header.h_dest, 0xff, 6);
    memcpy(packet->eth_header.h_source, mac, 6);
    packet->eth_header.h_proto = htons(ETH_P_ARP);
    
    // ARP header
    packet->arp.hw_type = htons(1);
    packet->arp.proto_type = htons(ETH_P_IP);
    packet->arp.hw_addr_len = 6;
    packet->arp.proto_addr_len = 4;
    packet->arp.operation = htons(2); // ARP Reply (some use Request)
    
    // Both sender and target have same IP
    memcpy(packet->arp.sender_hw_addr, mac, 6);
    inet_pton(AF_INET, ip, packet->arp.sender_proto_addr);
    memset(packet->arp.target_hw_addr, 0, 6);
    inet_pton(AF_INET, ip, packet->arp.target_proto_addr);
}

// Parse ARP packet
void parse_arp_packet(const struct arp_packet *packet) {
    char sender_ip[INET_ADDRSTRLEN];
    char target_ip[INET_ADDRSTRLEN];
    
    inet_ntop(AF_INET, packet->arp.sender_proto_addr, sender_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, packet->arp.target_proto_addr, target_ip, INET_ADDRSTRLEN);
    
    uint16_t op = ntohs(packet->arp.operation);
    
    printf("\n--- ARP Packet ---\n");
    printf("Operation: %s\n", op == 1 ? "Request" : "Reply");
    printf("Sender MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           packet->arp.sender_hw_addr[0], packet->arp.sender_hw_addr[1],
           packet->arp.sender_hw_addr[2], packet->arp.sender_hw_addr[3],
           packet->arp.sender_hw_addr[4], packet->arp.sender_hw_addr[5]);
    printf("Sender IP: %s\n", sender_ip);
    printf("Target MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           packet->arp.target_hw_addr[0], packet->arp.target_hw_addr[1],
           packet->arp.target_hw_addr[2], packet->arp.target_hw_addr[3],
           packet->arp.target_hw_addr[4], packet->arp.target_hw_addr[5]);
    printf("Target IP: %s\n", target_ip);
}

// Send ARP packet
int send_arp_packet(const char *interface, const struct arp_packet *packet) {
    int sockfd;
    struct ifreq ifr;
    struct sockaddr_ll addr;
    
    // Create raw socket
    sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    // Get interface index
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(sockfd);
        return -1;
    }
    
    // Set up destination address
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_ifindex = ifr.ifr_ifindex;
    addr.sll_halen = 6;
    memcpy(addr.sll_addr, packet->eth_header.h_dest, 6);
    
    // Send packet
    if (sendto(sockfd, packet, sizeof(struct arp_packet), 0,
               (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("sendto");
        close(sockfd);
        return -1;
    }
    
    close(sockfd);
    return 0;
}

// Get MAC address of interface
int get_mac_address(const char *interface, uint8_t *mac) {
    int sockfd;
    struct ifreq ifr;
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) < 0) {
        perror("ioctl SIOCGIFHWADDR");
        close(sockfd);
        return -1;
    }
    
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    close(sockfd);
    return 0;
}

// Example usage
int main(int argc, char *argv[]) {
    if (geteuid() != 0) {
        fprintf(stderr, "This program requires root privileges\n");
        return 1;
    }
    
    // Example: Create ARP cache
    arp_cache_t *cache = arp_cache_init(300); // 5 minutes timeout
    
    // Add some example entries
    uint8_t mac1[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t mac2[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    
    struct in_addr ip1, ip2;
    inet_pton(AF_INET, "192.168.1.100", &ip1);
    inet_pton(AF_INET, "192.168.1.1", &ip2);
    
    arp_cache_add(cache, ip1.s_addr, mac1, 0); // Dynamic entry
    arp_cache_add(cache, ip2.s_addr, mac2, 1); // Static entry
    
    // Print cache
    arp_cache_print(cache);
    
    // Example: Create and display ARP request
    const char *interface = "eth0"; // Change to your interface
    uint8_t src_mac[6];
    
    if (get_mac_address(interface, src_mac) == 0) {
        struct arp_packet request;
        create_arp_request(&request, "192.168.1.10", src_mac, "192.168.1.1");
        
        printf("\nCreated ARP Request:\n");
        parse_arp_packet(&request);
        
        // Uncomment to actually send the packet
        // send_arp_packet(interface, &request);
        
        // Example: Create Gratuitous ARP
        struct arp_packet garp;
        create_gratuitous_arp(&garp, "192.168.1.10", src_mac);
        printf("\nCreated Gratuitous ARP:\n");
        parse_arp_packet(&garp);
    }
    
    // Cleanup
    arp_cache_cleanup(cache);
    
    return 0;
}
