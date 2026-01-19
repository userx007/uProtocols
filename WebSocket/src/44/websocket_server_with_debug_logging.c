#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <time.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define DEBUG_LOG(fmt, ...) \
    do { \
        time_t now = time(NULL); \
        char timebuf[26]; \
        ctime_r(&now, timebuf); \
        timebuf[24] = '\0'; \
        fprintf(stderr, "[%s] DEBUG: " fmt "\n", timebuf, ##__VA_ARGS__); \
    } while(0)

// Base64 encode function
char* base64_encode(const unsigned char* input, int length) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    
    char *result = (char*)malloc(bufferPtr->length + 1);
    memcpy(result, bufferPtr->data, bufferPtr->length);
    result[bufferPtr->length] = '\0';
    
    BIO_free_all(bio);
    return result;
}

// Generate WebSocket accept key
char* generate_accept_key(const char* client_key) {
    DEBUG_LOG("Generating accept key for client key: %s", client_key);
    
    const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concat[256];
    snprintf(concat, sizeof(concat), "%s%s", client_key, magic);
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)concat, strlen(concat), hash);
    
    char* accept_key = base64_encode(hash, SHA_DIGEST_LENGTH);
    DEBUG_LOG("Generated accept key: %s", accept_key);
    return accept_key;
}

// Parse WebSocket frame
int parse_ws_frame(unsigned char* buffer, int len, char* payload) {
    DEBUG_LOG("Parsing WebSocket frame of length: %d", len);
    
    if (len < 2) {
        DEBUG_LOG("Frame too short: %d bytes", len);
        return -1;
    }
    
    unsigned char fin = (buffer[0] & 0x80) >> 7;
    unsigned char opcode = buffer[0] & 0x0F;
    unsigned char masked = (buffer[1] & 0x80) >> 7;
    unsigned long long payload_len = buffer[1] & 0x7F;
    
    DEBUG_LOG("FIN: %d, Opcode: %d, Masked: %d, Payload len: %llu", 
              fin, opcode, masked, payload_len);
    
    int offset = 2;
    
    if (payload_len == 126) {
        payload_len = (buffer[2] << 8) | buffer[3];
        offset = 4;
        DEBUG_LOG("Extended payload length (16-bit): %llu", payload_len);
    } else if (payload_len == 127) {
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | buffer[2 + i];
        }
        offset = 10;
        DEBUG_LOG("Extended payload length (64-bit): %llu", payload_len);
    }
    
    unsigned char mask[4];
    if (masked) {
        memcpy(mask, buffer + offset, 4);
        offset += 4;
        DEBUG_LOG("Masking key: %02x %02x %02x %02x", 
                  mask[0], mask[1], mask[2], mask[3]);
    }
    
    // Unmask payload
    for (unsigned long long i = 0; i < payload_len; i++) {
        payload[i] = buffer[offset + i] ^ mask[i % 4];
    }
    payload[payload_len] = '\0';
    
    DEBUG_LOG("Decoded payload: %s", payload);
    
    // Handle control frames
    if (opcode == 0x8) {
        DEBUG_LOG("Close frame received");
        return -2;
    } else if (opcode == 0x9) {
        DEBUG_LOG("Ping frame received");
        return -3;
    } else if (opcode == 0xA) {
        DEBUG_LOG("Pong frame received");
        return -4;
    }
    
    return payload_len;
}

// Create WebSocket frame
int create_ws_frame(const char* payload, unsigned char* frame) {
    int payload_len = strlen(payload);
    int frame_len = 0;
    
    DEBUG_LOG("Creating WebSocket frame for payload: %s", payload);
    
    frame[0] = 0x81; // FIN + text frame
    
    if (payload_len <= 125) {
        frame[1] = payload_len;
        frame_len = 2;
    } else if (payload_len <= 65535) {
        frame[1] = 126;
        frame[2] = (payload_len >> 8) & 0xFF;
        frame[3] = payload_len & 0xFF;
        frame_len = 4;
    } else {
        frame[1] = 127;
        for (int i = 0; i < 8; i++) {
            frame[2 + i] = (payload_len >> (56 - i * 8)) & 0xFF;
        }
        frame_len = 10;
    }
    
    memcpy(frame + frame_len, payload, payload_len);
    frame_len += payload_len;
    
    DEBUG_LOG("Created frame of %d bytes", frame_len);
    return frame_len;
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read;
    
    DEBUG_LOG("New client connected, socket fd: %d", client_sock);
    
    // Read HTTP upgrade request
    bytes_read = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read <= 0) {
        DEBUG_LOG("Failed to read upgrade request: %s", strerror(errno));
        close(client_sock);
        return;
    }
    buffer[bytes_read] = '\0';
    
    DEBUG_LOG("Received upgrade request:\n%s", buffer);
    
    // Extract Sec-WebSocket-Key
    char* key_start = strstr(buffer, "Sec-WebSocket-Key: ");
    if (!key_start) {
        DEBUG_LOG("Missing Sec-WebSocket-Key header");
        close(client_sock);
        return;
    }
    
    key_start += strlen("Sec-WebSocket-Key: ");
    char* key_end = strstr(key_start, "\r\n");
    char client_key[256];
    strncpy(client_key, key_start, key_end - key_start);
    client_key[key_end - key_start] = '\0';
    
    // Generate response
    char* accept_key = generate_accept_key(client_key);
    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             accept_key);
    
    DEBUG_LOG("Sending upgrade response:\n%s", response);
    send(client_sock, response, strlen(response), 0);
    free(accept_key);
    
    // Handle WebSocket frames
    while (1) {
        bytes_read = recv(client_sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            DEBUG_LOG("Connection closed or error: %s", 
                      bytes_read == 0 ? "EOF" : strerror(errno));
            break;
        }
        
        DEBUG_LOG("Received %d bytes", bytes_read);
        
        char payload[BUFFER_SIZE];
        int result = parse_ws_frame((unsigned char*)buffer, bytes_read, payload);
        
        if (result == -2) {
            DEBUG_LOG("Client requested close");
            break;
        } else if (result == -3) {
            DEBUG_LOG("Responding to ping with pong");
            unsigned char pong[2] = {0x8A, 0x00}; // Pong frame
            send(client_sock, pong, 2, 0);
        } else if (result > 0) {
            DEBUG_LOG("Echoing message back to client");
            unsigned char frame[BUFFER_SIZE];
            int frame_len = create_ws_frame(payload, frame);
            send(client_sock, frame, frame_len, 0);
        }
    }
    
    DEBUG_LOG("Closing client connection");
    close(client_sock);
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    DEBUG_LOG("Starting WebSocket server on port %d", PORT);
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    if (listen(server_sock, 5) < 0) {
        perror("Listen failed");
        exit(1);
    }
    
    DEBUG_LOG("Server listening, waiting for connections...");
    
    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            DEBUG_LOG("Accept failed: %s", strerror(errno));
            continue;
        }
        
        DEBUG_LOG("Accepted connection from %s:%d",
                  inet_ntoa(client_addr.sin_addr),
                  ntohs(client_addr.sin_port));
        
        handle_client(client_sock);
    }
    
    close(server_sock);
    return 0;
}