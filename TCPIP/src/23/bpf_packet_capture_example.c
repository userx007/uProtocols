#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pcap.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

// Callback function for packet processing
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
        
        // Check if it's TCP
        if (ip_header->ip_p == IPPROTO_TCP) {
            tcp_header = (struct tcphdr *)(packet + sizeof(struct ether_header) + 
                                           (ip_header->ip_hl * 4));
            printf("TCP: %d -> %d\n", ntohs(tcp_header->th_sport), 
                   ntohs(tcp_header->th_dport));
        }
    }
}

int main(int argc, char *argv[]) {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle;
    struct bpf_program filter;
    char filter_exp[] = "tcp port 80 or tcp port 443";
    bpf_u_int32 net, mask;
    char *dev;
    
    // Find default device
    dev = pcap_lookupdev(errbuf);
    if (dev == NULL) {
        fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
        return 1;
    }
    printf("Device: %s\n", dev);
    
    // Get network address and mask
    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
        fprintf(stderr, "Couldn't get netmask for device %s: %s\n", dev, errbuf);
        net = 0;
        mask = 0;
    }
    
    // Open device for packet capture
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        return 1;
    }
    
    // Compile BPF filter
    if (pcap_compile(handle, &filter, filter_exp, 0, net) == -1) {
        fprintf(stderr, "Couldn't parse filter %s: %s\n", 
                filter_exp, pcap_geterr(handle));
        return 1;
    }
    
    // Apply BPF filter
    if (pcap_setfilter(handle, &filter) == -1) {
        fprintf(stderr, "Couldn't install filter %s: %s\n", 
                filter_exp, pcap_geterr(handle));
        return 1;
    }
    
    printf("Capturing packets with filter: %s\n", filter_exp);
    printf("Press Ctrl+C to stop...\n\n");
    
    // Capture 10 packets
    pcap_loop(handle, 10, packet_handler, NULL);
    
    // Cleanup
    pcap_freecode(&filter);
    pcap_close(handle);
    
    printf("\nCapture complete.\n");
    return 0;
}

// Compile: gcc -o bpf_packet_capture_example bpf_packet_capture_example.c -lpcap
// Run: sudo ./bpf_packet_capture_example