# Multiple Client Handling in Modbus

## Overview

Multiple client handling is a critical capability for Modbus servers that need to serve multiple clients simultaneously. In industrial automation environments, it's common for several devices, HMIs (Human-Machine Interfaces), SCADA systems, or monitoring applications to query the same Modbus server concurrently. Without proper concurrent connection handling, these systems would have to wait in queue, leading to delays and potential timeout issues.

## Core Concepts

### Connection Types

**Modbus TCP** uses TCP/IP sockets, where each client maintains a persistent connection to the server. The server must handle multiple socket connections simultaneously.

**Modbus RTU over TCP** can also support multiple clients, though the underlying serial protocol may have limitations when bridging to serial devices.

### Concurrency Approaches

There are several architectural patterns for handling multiple clients:

1. **Multi-threading**: Each client connection gets its own thread
2. **Thread pooling**: A fixed pool of threads services multiple connections
3. **Asynchronous I/O**: Event-driven, non-blocking I/O using select/poll/epoll
4. **Hybrid approaches**: Combining threading with async I/O

### Synchronization Challenges

When multiple clients access the same data registers simultaneously, you need to protect shared resources:
- Register/coil data structures
- Device state information
- Transaction counters
- Connection management structures

## C/C++ Implementation

Here's a multi-threaded Modbus TCP server implementation using POSIX threads:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

#define MODBUS_PORT 502
#define MAX_CLIENTS 10
#define BUFFER_SIZE 260
#define MAX_REGISTERS 100

// Shared data structure with mutex protection
typedef struct {
    uint16_t holding_registers[MAX_REGISTERS];
    uint16_t input_registers[MAX_REGISTERS];
    uint8_t coils[MAX_REGISTERS];
    pthread_mutex_t mutex;
} modbus_data_t;

// Client connection context
typedef struct {
    int socket;
    struct sockaddr_in address;
    modbus_data_t *shared_data;
    int client_id;
} client_context_t;

// Global shared data
modbus_data_t g_modbus_data;

// Initialize shared data
void init_modbus_data(modbus_data_t *data) {
    pthread_mutex_init(&data->mutex, NULL);
    memset(data->holding_registers, 0, sizeof(data->holding_registers));
    memset(data->input_registers, 0, sizeof(data->input_registers));
    memset(data->coils, 0, sizeof(data->coils));
    
    // Initialize with some test data
    for (int i = 0; i < 10; i++) {
        data->holding_registers[i] = i * 100;
        data->input_registers[i] = i * 50;
    }
}

// Parse Modbus TCP request and generate response
int process_modbus_request(uint8_t *request, int req_len, 
                          uint8_t *response, modbus_data_t *data) {
    if (req_len < 8) return -1;
    
    // MBAP Header: Transaction ID (2) + Protocol ID (2) + Length (2)
    uint16_t transaction_id = (request[0] << 8) | request[1];
    uint16_t protocol_id = (request[2] << 8) | request[3];
    uint8_t unit_id = request[6];
    uint8_t function_code = request[7];
    
    // Copy MBAP header to response
    memcpy(response, request, 6);
    response[6] = unit_id;
    response[7] = function_code;
    
    int response_length = 8;
    
    pthread_mutex_lock(&data->mutex);
    
    switch (function_code) {
        case 0x03: { // Read Holding Registers
            uint16_t start_addr = (request[8] << 8) | request[9];
            uint16_t quantity = (request[10] << 8) | request[11];
            
            if (start_addr + quantity > MAX_REGISTERS) {
                response[7] = function_code | 0x80; // Exception
                response[8] = 0x02; // Illegal data address
                response_length = 9;
                break;
            }
            
            response[8] = quantity * 2; // Byte count
            for (int i = 0; i < quantity; i++) {
                uint16_t value = data->holding_registers[start_addr + i];
                response[9 + i*2] = (value >> 8) & 0xFF;
                response[10 + i*2] = value & 0xFF;
            }
            response_length = 9 + quantity * 2;
            break;
        }
        
        case 0x06: { // Write Single Register
            uint16_t addr = (request[8] << 8) | request[9];
            uint16_t value = (request[10] << 8) | request[11];
            
            if (addr >= MAX_REGISTERS) {
                response[7] = function_code | 0x80;
                response[8] = 0x02;
                response_length = 9;
                break;
            }
            
            data->holding_registers[addr] = value;
            memcpy(response + 8, request + 8, 4); // Echo back
            response_length = 12;
            break;
        }
        
        case 0x10: { // Write Multiple Registers
            uint16_t start_addr = (request[8] << 8) | request[9];
            uint16_t quantity = (request[10] << 8) | request[11];
            uint8_t byte_count = request[12];
            
            if (start_addr + quantity > MAX_REGISTERS) {
                response[7] = function_code | 0x80;
                response[8] = 0x02;
                response_length = 9;
                break;
            }
            
            for (int i = 0; i < quantity; i++) {
                uint16_t value = (request[13 + i*2] << 8) | request[14 + i*2];
                data->holding_registers[start_addr + i] = value;
            }
            
            memcpy(response + 8, request + 8, 4); // Echo address and quantity
            response_length = 12;
            break;
        }
        
        default:
            response[7] = function_code | 0x80;
            response[8] = 0x01; // Illegal function
            response_length = 9;
    }
    
    pthread_mutex_unlock(&data->mutex);
    
    // Update length field in MBAP header
    uint16_t length = response_length - 6;
    response[4] = (length >> 8) & 0xFF;
    response[5] = length & 0xFF;
    
    return response_length;
}

// Client handler thread
void *client_handler(void *arg) {
    client_context_t *ctx = (client_context_t *)arg;
    uint8_t buffer[BUFFER_SIZE];
    uint8_t response[BUFFER_SIZE];
    
    printf("Client %d connected from %s:%d\n", 
           ctx->client_id,
           inet_ntoa(ctx->address.sin_addr),
           ntohs(ctx->address.sin_port));
    
    while (1) {
        int bytes_received = recv(ctx->socket, buffer, BUFFER_SIZE, 0);
        
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("Client %d disconnected\n", ctx->client_id);
            } else {
                perror("recv failed");
            }
            break;
        }
        
        printf("Client %d: Received %d bytes\n", ctx->client_id, bytes_received);
        
        int response_len = process_modbus_request(buffer, bytes_received, 
                                                  response, ctx->shared_data);
        
        if (response_len > 0) {
            if (send(ctx->socket, response, response_len, 0) < 0) {
                perror("send failed");
                break;
            }
            printf("Client %d: Sent %d bytes\n", ctx->client_id, response_len);
        }
    }
    
    close(ctx->socket);
    free(ctx);
    return NULL;
}

// Main server
int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;
    int client_count = 0;
    
    // Initialize shared data
    init_modbus_data(&g_modbus_data);
    
    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Allow socket reuse
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(MODBUS_PORT);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, 
             sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        close(server_socket);
        return 1;
    }
    
    printf("Modbus TCP Server listening on port %d\n", MODBUS_PORT);
    
    // Accept client connections
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, 
                              &client_len);
        
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        // Create client context
        client_context_t *ctx = malloc(sizeof(client_context_t));
        ctx->socket = client_socket;
        ctx->address = client_addr;
        ctx->shared_data = &g_modbus_data;
        ctx->client_id = ++client_count;
        
        // Spawn handler thread
        if (pthread_create(&thread_id, NULL, client_handler, ctx) != 0) {
            perror("Thread creation failed");
            close(client_socket);
            free(ctx);
            continue;
        }
        
        // Detach thread so resources are freed automatically
        pthread_detach(thread_id);
    }
    
    close(server_socket);
    pthread_mutex_destroy(&g_modbus_data.mutex);
    return 0;
}
```

## Rust Implementation

Here's an equivalent implementation in Rust using async/await with Tokio:

```rust
use std::sync::Arc;
use tokio::sync::RwLock;
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::collections::HashMap;

const MODBUS_PORT: u16 = 502;
const MAX_REGISTERS: usize = 100;
const BUFFER_SIZE: usize = 260;

// Shared Modbus data with async-safe locking
#[derive(Clone)]
struct ModbusData {
    holding_registers: Arc<RwLock<Vec<u16>>>,
    input_registers: Arc<RwLock<Vec<u16>>>,
    coils: Arc<RwLock<Vec<bool>>>,
}

impl ModbusData {
    fn new() -> Self {
        let mut holding = vec![0u16; MAX_REGISTERS];
        let mut input = vec![0u16; MAX_REGISTERS];
        
        // Initialize with test data
        for i in 0..10 {
            holding[i] = (i as u16) * 100;
            input[i] = (i as u16) * 50;
        }
        
        ModbusData {
            holding_registers: Arc::new(RwLock::new(holding)),
            input_registers: Arc::new(RwLock::new(input)),
            coils: Arc::new(RwLock::new(vec![false; MAX_REGISTERS])),
        }
    }
}

// Process Modbus request and generate response
async fn process_modbus_request(
    request: &[u8],
    data: &ModbusData,
) -> Result<Vec<u8>, String> {
    if request.len() < 8 {
        return Err("Request too short".to_string());
    }
    
    // Parse MBAP header
    let transaction_id = u16::from_be_bytes([request[0], request[1]]);
    let protocol_id = u16::from_be_bytes([request[2], request[3]]);
    let unit_id = request[6];
    let function_code = request[7];
    
    // Start building response with MBAP header
    let mut response = Vec::new();
    response.extend_from_slice(&request[0..6]); // Copy MBAP header
    response.push(unit_id);
    response.push(function_code);
    
    match function_code {
        0x03 => { // Read Holding Registers
            let start_addr = u16::from_be_bytes([request[8], request[9]]) as usize;
            let quantity = u16::from_be_bytes([request[10], request[11]]) as usize;
            
            if start_addr + quantity > MAX_REGISTERS {
                response[7] = function_code | 0x80;
                response.push(0x02); // Illegal data address
            } else {
                let registers = data.holding_registers.read().await;
                response.push((quantity * 2) as u8); // Byte count
                
                for i in 0..quantity {
                    let value = registers[start_addr + i];
                    response.extend_from_slice(&value.to_be_bytes());
                }
            }
        }
        
        0x04 => { // Read Input Registers
            let start_addr = u16::from_be_bytes([request[8], request[9]]) as usize;
            let quantity = u16::from_be_bytes([request[10], request[11]]) as usize;
            
            if start_addr + quantity > MAX_REGISTERS {
                response[7] = function_code | 0x80;
                response.push(0x02);
            } else {
                let registers = data.input_registers.read().await;
                response.push((quantity * 2) as u8);
                
                for i in 0..quantity {
                    let value = registers[start_addr + i];
                    response.extend_from_slice(&value.to_be_bytes());
                }
            }
        }
        
        0x06 => { // Write Single Register
            let addr = u16::from_be_bytes([request[8], request[9]]) as usize;
            let value = u16::from_be_bytes([request[10], request[11]]);
            
            if addr >= MAX_REGISTERS {
                response[7] = function_code | 0x80;
                response.push(0x02);
            } else {
                let mut registers = data.holding_registers.write().await;
                registers[addr] = value;
                response.extend_from_slice(&request[8..12]); // Echo back
            }
        }
        
        0x10 => { // Write Multiple Registers
            let start_addr = u16::from_be_bytes([request[8], request[9]]) as usize;
            let quantity = u16::from_be_bytes([request[10], request[11]]) as usize;
            let byte_count = request[12] as usize;
            
            if start_addr + quantity > MAX_REGISTERS {
                response[7] = function_code | 0x80;
                response.push(0x02);
            } else {
                let mut registers = data.holding_registers.write().await;
                
                for i in 0..quantity {
                    let offset = 13 + i * 2;
                    let value = u16::from_be_bytes([request[offset], request[offset + 1]]);
                    registers[start_addr + i] = value;
                }
                
                response.extend_from_slice(&request[8..12]); // Echo address and quantity
            }
        }
        
        _ => {
            response[7] = function_code | 0x80;
            response.push(0x01); // Illegal function
        }
    }
    
    // Update length field in MBAP header
    let length = (response.len() - 6) as u16;
    response[4..6].copy_from_slice(&length.to_be_bytes());
    
    Ok(response)
}

// Handle individual client connection
async fn handle_client(
    mut stream: TcpStream,
    addr: std::net::SocketAddr,
    data: ModbusData,
    client_id: usize,
) {
    println!("Client {} connected from {}", client_id, addr);
    
    let mut buffer = vec![0u8; BUFFER_SIZE];
    
    loop {
        match stream.read(&mut buffer).await {
            Ok(0) => {
                println!("Client {} disconnected", client_id);
                break;
            }
            Ok(n) => {
                println!("Client {}: Received {} bytes", client_id, n);
                
                match process_modbus_request(&buffer[..n], &data).await {
                    Ok(response) => {
                        if let Err(e) = stream.write_all(&response).await {
                            eprintln!("Client {}: Send error: {}", client_id, e);
                            break;
                        }
                        println!("Client {}: Sent {} bytes", client_id, response.len());
                    }
                    Err(e) => {
                        eprintln!("Client {}: Processing error: {}", client_id, e);
                    }
                }
            }
            Err(e) => {
                eprintln!("Client {}: Read error: {}", client_id, e);
                break;
            }
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let listener = TcpListener::bind(format!("0.0.0.0:{}", MODBUS_PORT)).await?;
    println!("Modbus TCP Server listening on port {}", MODBUS_PORT);
    
    let shared_data = ModbusData::new();
    let mut client_count = 0usize;
    
    loop {
        let (stream, addr) = listener.accept().await?;
        let data = shared_data.clone();
        client_count += 1;
        let current_client = client_count;
        
        // Spawn a new task for each client
        tokio::spawn(async move {
            handle_client(stream, addr, data, current_client).await;
        });
    }
}
```

## Summary

**Multiple client handling** in Modbus servers enables concurrent access from multiple devices or applications to the same data source. The key implementation considerations include:

**Architecture choices**: Multi-threading provides isolation between clients but requires careful synchronization, while async I/O offers better scalability with lower overhead for high connection counts.

**Thread safety**: Shared register data must be protected with mutexes (C/C++) or RwLocks (Rust) to prevent race conditions when multiple clients read/write simultaneously.

**Resource management**: Connection limits, buffer management, and proper cleanup of client resources are essential to prevent resource exhaustion.

**Performance trade-offs**: Threading has higher memory overhead but simpler programming model; async I/O scales better but requires event-driven thinking.

The C implementation uses POSIX threads with mutex-protected shared data, while the Rust implementation leverages Tokio's async runtime with RwLock for safe concurrent access. Both approaches ensure that multiple clients can query and modify Modbus registers without data corruption, making them suitable for real-world industrial automation scenarios where multiple monitoring and control systems need simultaneous access to the same Modbus server.