#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void demonstrate_server() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Setup address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Already in network byte order
    address.sin_port = htons(PORT); // Convert port to network byte order
    
    printf("Server Configuration:\n");
    printf("  Port (host order): %d\n", PORT);
    printf("  Port (network order): 0x%04X\n", address.sin_port);
    printf("  Address family: %d\n", address.sin_family);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Server bound to port %d\n", PORT);
    
    // Listen for connections
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Waiting for connections...\n");
    
    // Accept a connection (this would block in real scenario)
    // For demonstration, we'll just show the setup
    
    close(server_fd);
}

void demonstrate_client() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error\n");
        return;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT); // Convert to network byte order
    
    // Convert IPv4 address from text to binary in network byte order
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Invalid address\n");
        close(sock);
        return;
    }
    
    printf("Client Configuration:\n");
    printf("  Target Port: %d (host order)\n", PORT);
    printf("  Network Port: 0x%04X\n", serv_addr.sin_port);
    printf("  Target IP: 127.0.0.1\n");
    
    close(sock);
}

int main() {
    printf("=== Network Byte Order Socket Demonstration ===\n\n");
    
    printf("--- Server Setup ---\n");
    demonstrate_server();
    
    printf("\n--- Client Setup ---\n");
    demonstrate_client();
    
    return 0;
}