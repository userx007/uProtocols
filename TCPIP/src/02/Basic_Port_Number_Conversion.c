#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>

int main() {
    // Host byte order values
    uint16_t host_port = 8080;
    uint32_t host_ip = 0xC0A80001; // 192.168.0.1
    
    // Convert to network byte order
    uint16_t net_port = htons(host_port);
    uint32_t net_ip = htonl(host_ip);
    
    printf("Host Byte Order:\n");
    printf("  Port: %u (0x%04X)\n", host_port, host_port);
    printf("  IP:   0x%08X\n\n", host_ip);
    
    printf("Network Byte Order:\n");
    printf("  Port: %u (0x%04X)\n", net_port, net_port);
    printf("  IP:   0x%08X\n\n", net_ip);
    
    // Convert back to host byte order
    uint16_t converted_port = ntohs(net_port);
    uint32_t converted_ip = ntohl(net_ip);
    
    printf("Converted Back:\n");
    printf("  Port: %u (0x%04X)\n", converted_port, converted_port);
    printf("  IP:   0x%08X\n", converted_ip);
    
    // Verify conversions are correct
    if (converted_port == host_port && converted_ip == host_ip) {
        printf("\nConversion successful!\n");
    }
    
    return 0;
}