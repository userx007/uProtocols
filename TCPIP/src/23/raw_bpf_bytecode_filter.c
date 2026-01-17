#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pcap.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/*
 * This example demonstrates creating BPF filters at the bytecode level.
 * The filter matches TCP packets on port 80 (HTTP).
 * 
 * BPF instruction format:
 * - opcode: operation to perform
 * - jt: jump offset if true
 * - jf: jump offset if false
 * - k: generic field (offset, value, etc.)
 */

void print_packet_info(const u_char *packet, int len) {
    struct ip *ip_header;
    struct tcphdr *tcp_header;
    
    // Skip Ethernet header (14 bytes)
    ip_header = (struct ip *)(packet + 14);
    
    if (ip_header->ip_p == IPPROTO_TCP) {
        tcp_header = (struct tcphdr *)(packet + 14 + (ip_header->ip_hl * 4));
        
        char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ip_header->ip_src), src_ip, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &(ip_header->ip_dst), dst_ip, INET_ADDRSTRLEN);
        
        printf("TCP: %s:%d -> %s:%d\n",
               src_ip, ntohs(tcp_header->th_sport),
               dst_ip, ntohs(tcp_header->th_dport));
    }
}

void packet_handler(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes) {
    static int count = 0;
    printf("[%d] ", ++count);
    print_packet_info(bytes, h->len);
}

int main() {
    pcap_t *handle;
    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program filter_compiled;
    struct bpf_insn raw_filter[] = {
        /*
         * Hand-crafted BPF filter for TCP port 80
         * This is equivalent to the tcpdump filter: "tcp port 80"
         */
        
        // Load the Ethernet type at offset 12 (2 bytes)
        { 0x28, 0, 0, 0x0000000c },  // ldh [12]
        
        // Check if it's IPv4 (0x0800)
        { 0x15, 0, 8, 0x00000800 },  // jeq #0x800, true:0, false:8
        
        // Load IP protocol at offset 23 (1 byte after Ethernet header + 9 bytes into IP)
        { 0x30, 0, 0, 0x00000017 },  // ldb [23]
        
        // Check if it's TCP (protocol 6)
        { 0x15, 0, 6, 0x00000006 },  // jeq #0x6, true:0, false:6
        
        // Load IP header length (needed to find TCP header)
        { 0x28, 0, 0, 0x00000014 },  // ldh [20]
        
        // Mask off fragment offset and reserved bits
        { 0x45, 4, 0, 0x00001fff },  // jset #0x1fff, true:4, false:0
        
        // Load X with IP header length * 4
        { 0xb1, 0, 0, 0x0000000e },  // ldxb 4*([14]&0xf)
        
        // Load source port
        { 0x48, 0, 0, 0x0000000e },  // ldh [x + 14]
        
        // Check if source port is 80
        { 0x15, 0, 1, 0x00000050 },  // jeq #80, true:0, false:1
        
        // Return full packet (accept)
        { 0x06, 0, 0, 0x00040000 },  // ret #262144
        
        // Load destination port
        { 0x48, 0, 0, 0x00000010 },  // ldh [x + 16]
        
        // Check if destination port is 80
        { 0x15, 0, 1, 0x00000050 },  // jeq #80, true:0, false:1
        
        // Return full packet (accept)
        { 0x06, 0, 0, 0x00040000 },  // ret #262144
        
        // Return 0 (reject)
        { 0x06, 0, 0, 0x00000000 },  // ret #0
    };
    
    // Alternative: Use pcap_compile for easier filter creation
    char filter_exp[] = "tcp port 80";
    
    printf("BPF Raw Bytecode Example\n");
    printf("=========================\n\n");
    
    // Open device
    char *dev = pcap_lookupdev(errbuf);
    if (dev == NULL) {
        fprintf(stderr, "Error finding device: %s\n", errbuf);
        return 1;
    }
    
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
        return 1;
    }
    
    printf("Using device: %s\n", dev);
    
    // Option 1: Use raw BPF bytecode
    printf("\n--- Using Raw BPF Bytecode ---\n");
    filter_compiled.bf_len = sizeof(raw_filter) / sizeof(struct bpf_insn);
    filter_compiled.bf_insns = raw_filter;
    
    if (pcap_setfilter(handle, &filter_compiled) == -1) {
        fprintf(stderr, "Error setting raw filter: %s\n", pcap_geterr(handle));
        return 1;
    }
    
    printf("Raw BPF filter installed (%d instructions)\n", filter_compiled.bf_len);
    printf("Capturing TCP port 80 packets...\n\n");
    
    pcap_loop(handle, 5, packet_handler, NULL);
    
    // Option 2: Compare with compiled filter
    printf("\n--- Using Compiled Filter Expression ---\n");
    bpf_u_int32 net, mask;
    if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) {
        net = 0;
        mask = 0;
    }
    
    struct bpf_program compiled;
    if (pcap_compile(handle, &compiled, filter_exp, 1, mask) == -1) {
        fprintf(stderr, "Error compiling filter: %s\n", pcap_geterr(handle));
        return 1;
    }
    
    printf("Compiled filter: %s\n", filter_exp);
    printf("Number of BPF instructions: %d\n\n", compiled.bf_len);
    
    // Print the generated BPF bytecode
    printf("Generated BPF bytecode:\n");
    for (int i = 0; i < compiled.bf_len && i < 20; i++) {
        printf("  [%2d] code=0x%02x jt=%d jf=%d k=0x%08x\n",
               i,
               compiled.bf_insns[i].code,
               compiled.bf_insns[i].jt,
               compiled.bf_insns[i].jf,
               compiled.bf_insns[i].k);
    }
    
    pcap_freecode(&compiled);
    pcap_close(handle);
    
    printf("\nCapture complete.\n");
    return 0;
}

// Compile: gcc -o raw_bpf_bytecode_filter raw_bpf_bytecode_filter.c -lpcap
// Run: sudo ./raw_bpf_bytecode_filter