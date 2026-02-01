# Modbus UDP Transport Layer

## Detailed Description

The Modbus UDP Transport Layer represents an alternative to the traditional TCP-based Modbus communication, utilizing the User Datagram Protocol (UDP) for data transmission. This transport mechanism is particularly valuable in industrial automation scenarios where specific network characteristics such as multicast capability, reduced protocol overhead, or connectionless communication are beneficial.

### Core Concepts

**UDP vs. TCP for Modbus:**
- **UDP** is connectionless, meaning no handshake or connection establishment is required
- **Lower overhead** compared to TCP due to simpler header structure and no connection management
- **No guaranteed delivery** - packets may be lost without notification
- **Multicast/broadcast support** - enables one-to-many communication patterns
- **Faster transmission** for small packets due to reduced protocol complexity

**Typical Use Cases:**
1. **Multicast scenarios** - Broadcasting data to multiple devices simultaneously
2. **High-speed polling** - Rapid status updates where occasional data loss is acceptable
3. **Low-latency requirements** - Time-critical applications where TCP overhead is prohibitive
4. **Resource-constrained networks** - Reducing bandwidth consumption
5. **One-way data transmission** - Monitoring applications where acknowledgment isn't critical

### Protocol Structure

Modbus UDP uses the same Application Data Unit (ADU) structure as Modbus TCP but transmitted over UDP:

```
[MBAP Header (7 bytes)][Function Code (1 byte)][Data (variable)]
```

**MBAP Header (Modbus Application Protocol):**
- Transaction ID (2 bytes): For matching requests/responses
- Protocol ID (2 bytes): Always 0x0000 for Modbus
- Length (2 bytes): Number of following bytes
- Unit ID (1 byte): Slave device identifier

**Default Port:** 502 (same as Modbus TCP)

## C/C++ Implementation

### Client Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>

#define MODBUS_UDP_PORT 502
#define BUFFER_SIZE 260

// Modbus function codes
#define MODBUS_FC_READ_HOLDING_REGISTERS 0x03
#define MODBUS_FC_WRITE_SINGLE_REGISTER 0x06

typedef struct {
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint16_t length;
    uint8_t unit_id;
} modbus_mbap_header_t;

typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;
    uint16_t transaction_id;
} modbus_udp_context_t;

// Initialize Modbus UDP context
modbus_udp_context_t* modbus_udp_init(const char* ip_address, int port) {
    modbus_udp_context_t* ctx = (modbus_udp_context_t*)malloc(sizeof(modbus_udp_context_t));
    if (!ctx) return NULL;
    
    // Create UDP socket
    ctx->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->sockfd < 0) {
        free(ctx);
        return NULL;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(ctx->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Configure server address
    memset(&ctx->server_addr, 0, sizeof(ctx->server_addr));
    ctx->server_addr.sin_family = AF_INET;
    ctx->server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip_address, &ctx->server_addr.sin_addr);
    
    ctx->transaction_id = 1;
    
    return ctx;
}

// Build MBAP header
void build_mbap_header(uint8_t* buffer, uint16_t transaction_id, 
                       uint16_t length, uint8_t unit_id) {
    buffer[0] = (transaction_id >> 8) & 0xFF;
    buffer[1] = transaction_id & 0xFF;
    buffer[2] = 0x00; // Protocol ID (always 0)
    buffer[3] = 0x00;
    buffer[4] = (length >> 8) & 0xFF;
    buffer[5] = length & 0xFF;
    buffer[6] = unit_id;
}

// Read holding registers
int modbus_udp_read_holding_registers(modbus_udp_context_t* ctx, 
                                       uint8_t unit_id,
                                       uint16_t start_addr, 
                                       uint16_t num_registers,
                                       uint16_t* dest) {
    uint8_t request[12];
    uint8_t response[BUFFER_SIZE];
    
    // Build MBAP header
    build_mbap_header(request, ctx->transaction_id, 6, unit_id);
    
    // Build PDU (Protocol Data Unit)
    request[7] = MODBUS_FC_READ_HOLDING_REGISTERS;
    request[8] = (start_addr >> 8) & 0xFF;
    request[9] = start_addr & 0xFF;
    request[10] = (num_registers >> 8) & 0xFF;
    request[11] = num_registers & 0xFF;
    
    // Send request
    ssize_t sent = sendto(ctx->sockfd, request, 12, 0,
                          (struct sockaddr*)&ctx->server_addr, 
                          sizeof(ctx->server_addr));
    if (sent < 0) {
        perror("sendto failed");
        return -1;
    }
    
    // Receive response
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t received = recvfrom(ctx->sockfd, response, BUFFER_SIZE, 0,
                                (struct sockaddr*)&from_addr, &from_len);
    
    if (received < 0) {
        perror("recvfrom failed or timeout");
        return -1;
    }
    
    // Verify response
    if (received < 9) {
        fprintf(stderr, "Response too short\n");
        return -1;
    }
    
    // Check for exception
    if (response[7] & 0x80) {
        fprintf(stderr, "Modbus exception: 0x%02X\n", response[8]);
        return -1;
    }
    
    // Extract register values
    uint8_t byte_count = response[8];
    if (byte_count != num_registers * 2) {
        fprintf(stderr, "Unexpected byte count\n");
        return -1;
    }
    
    for (int i = 0; i < num_registers; i++) {
        dest[i] = (response[9 + i*2] << 8) | response[10 + i*2];
    }
    
    ctx->transaction_id++;
    return num_registers;
}

// Write single register
int modbus_udp_write_single_register(modbus_udp_context_t* ctx,
                                      uint8_t unit_id,
                                      uint16_t addr,
                                      uint16_t value) {
    uint8_t request[12];
    uint8_t response[BUFFER_SIZE];
    
    // Build MBAP header
    build_mbap_header(request, ctx->transaction_id, 6, unit_id);
    
    // Build PDU
    request[7] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    request[8] = (addr >> 8) & 0xFF;
    request[9] = addr & 0xFF;
    request[10] = (value >> 8) & 0xFF;
    request[11] = value & 0xFF;
    
    // Send request
    sendto(ctx->sockfd, request, 12, 0,
           (struct sockaddr*)&ctx->server_addr, sizeof(ctx->server_addr));
    
    // Receive response
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t received = recvfrom(ctx->sockfd, response, BUFFER_SIZE, 0,
                                (struct sockaddr*)&from_addr, &from_len);
    
    if (received < 0) return -1;
    
    // Verify echo response
    if (memcmp(request, response, 12) != 0) {
        fprintf(stderr, "Response mismatch\n");
        return -1;
    }
    
    ctx->transaction_id++;
    return 0;
}

// Multicast read example
int modbus_udp_multicast_read(const char* multicast_group, int port,
                               uint8_t unit_id, uint16_t start_addr,
                               uint16_t num_registers) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return -1;
    
    // Enable multicast
    struct ip_mreq mreq;
    inet_pton(AF_INET, multicast_group, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt multicast failed");
        close(sockfd);
        return -1;
    }
    
    // Bind to multicast port
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return -1;
    }
    
    printf("Listening for multicast Modbus on %s:%d\n", multicast_group, port);
    
    uint8_t buffer[BUFFER_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    // Receive multicast packets
    while (1) {
        ssize_t received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                                    (struct sockaddr*)&from_addr, &from_len);
        if (received > 0) {
            printf("Received %zd bytes from %s\n", received, 
                   inet_ntoa(from_addr.sin_addr));
            // Process Modbus data here
        }
    }
    
    close(sockfd);
    return 0;
}

// Cleanup
void modbus_udp_close(modbus_udp_context_t* ctx) {
    if (ctx) {
        close(ctx->sockfd);
        free(ctx);
    }
}

// Example usage
int main() {
    modbus_udp_context_t* ctx = modbus_udp_init("192.168.1.100", MODBUS_UDP_PORT);
    if (!ctx) {
        fprintf(stderr, "Failed to initialize Modbus UDP\n");
        return 1;
    }
    
    uint16_t registers[10];
    
    // Read 10 holding registers starting at address 0
    int result = modbus_udp_read_holding_registers(ctx, 1, 0, 10, registers);
    if (result > 0) {
        printf("Read %d registers:\n", result);
        for (int i = 0; i < result; i++) {
            printf("Register %d: %u\n", i, registers[i]);
        }
    }
    
    // Write a single register
    if (modbus_udp_write_single_register(ctx, 1, 0, 1234) == 0) {
        printf("Successfully wrote register\n");
    }
    
    modbus_udp_close(ctx);
    return 0;
}
```

## Rust Implementation

```rust
use std::net::{UdpSocket, SocketAddr, Ipv4Addr};
use std::time::Duration;
use std::io::{self, ErrorKind};

// Modbus function codes
const MODBUS_FC_READ_HOLDING_REGISTERS: u8 = 0x03;
const MODBUS_FC_WRITE_SINGLE_REGISTER: u8 = 0x06;
const MODBUS_UDP_PORT: u16 = 502;

#[derive(Debug)]
pub struct ModbusUdpContext {
    socket: UdpSocket,
    server_addr: SocketAddr,
    transaction_id: u16,
}

#[derive(Debug)]
pub enum ModbusError {
    IoError(io::Error),
    ProtocolError(String),
    ExceptionResponse(u8),
    Timeout,
}

impl From<io::Error> for ModbusError {
    fn from(err: io::Error) -> Self {
        ModbusError::IoError(err)
    }
}

impl ModbusUdpContext {
    /// Create new Modbus UDP context
    pub fn new(ip: &str, port: u16) -> Result<Self, ModbusError> {
        let local_addr: SocketAddr = "0.0.0.0:0".parse().unwrap();
        let socket = UdpSocket::bind(local_addr)?;
        
        // Set timeout
        socket.set_read_timeout(Some(Duration::from_secs(2)))?;
        socket.set_write_timeout(Some(Duration::from_secs(2)))?;
        
        let server_addr: SocketAddr = format!("{}:{}", ip, port)
            .parse()
            .map_err(|e| ModbusError::ProtocolError(format!("Invalid address: {}", e)))?;
        
        Ok(ModbusUdpContext {
            socket,
            server_addr,
            transaction_id: 1,
        })
    }
    
    /// Build MBAP header
    fn build_mbap_header(&self, length: u16, unit_id: u8) -> [u8; 7] {
        let mut header = [0u8; 7];
        header[0] = (self.transaction_id >> 8) as u8;
        header[1] = self.transaction_id as u8;
        header[2] = 0x00; // Protocol ID
        header[3] = 0x00;
        header[4] = (length >> 8) as u8;
        header[5] = length as u8;
        header[6] = unit_id;
        header
    }
    
    /// Read holding registers
    pub fn read_holding_registers(
        &mut self,
        unit_id: u8,
        start_addr: u16,
        num_registers: u16,
    ) -> Result<Vec<u16>, ModbusError> {
        let mut request = Vec::with_capacity(12);
        
        // MBAP header
        request.extend_from_slice(&self.build_mbap_header(6, unit_id));
        
        // PDU
        request.push(MODBUS_FC_READ_HOLDING_REGISTERS);
        request.push((start_addr >> 8) as u8);
        request.push(start_addr as u8);
        request.push((num_registers >> 8) as u8);
        request.push(num_registers as u8);
        
        // Send request
        self.socket.send_to(&request, self.server_addr)?;
        
        // Receive response
        let mut response = [0u8; 260];
        let (received, _) = self.socket.recv_from(&mut response)
            .map_err(|e| {
                if e.kind() == ErrorKind::WouldBlock || e.kind() == ErrorKind::TimedOut {
                    ModbusError::Timeout
                } else {
                    ModbusError::IoError(e)
                }
            })?;
        
        if received < 9 {
            return Err(ModbusError::ProtocolError("Response too short".into()));
        }
        
        // Check for exception
        if response[7] & 0x80 != 0 {
            return Err(ModbusError::ExceptionResponse(response[8]));
        }
        
        // Extract register values
        let byte_count = response[8] as usize;
        if byte_count != num_registers as usize * 2 {
            return Err(ModbusError::ProtocolError("Unexpected byte count".into()));
        }
        
        let mut registers = Vec::with_capacity(num_registers as usize);
        for i in 0..num_registers as usize {
            let value = ((response[9 + i * 2] as u16) << 8) | response[10 + i * 2] as u16;
            registers.push(value);
        }
        
        self.transaction_id = self.transaction_id.wrapping_add(1);
        Ok(registers)
    }
    
    /// Write single register
    pub fn write_single_register(
        &mut self,
        unit_id: u8,
        addr: u16,
        value: u16,
    ) -> Result<(), ModbusError> {
        let mut request = Vec::with_capacity(12);
        
        // MBAP header
        request.extend_from_slice(&self.build_mbap_header(6, unit_id));
        
        // PDU
        request.push(MODBUS_FC_WRITE_SINGLE_REGISTER);
        request.push((addr >> 8) as u8);
        request.push(addr as u8);
        request.push((value >> 8) as u8);
        request.push(value as u8);
        
        // Send request
        self.socket.send_to(&request, self.server_addr)?;
        
        // Receive response (echo)
        let mut response = [0u8; 260];
        let (received, _) = self.socket.recv_from(&mut response)?;
        
        if received < 12 {
            return Err(ModbusError::ProtocolError("Response too short".into()));
        }
        
        // Verify echo
        if &request[..12] != &response[..12] {
            return Err(ModbusError::ProtocolError("Response mismatch".into()));
        }
        
        self.transaction_id = self.transaction_id.wrapping_add(1);
        Ok(())
    }
}

/// Multicast UDP receiver
pub struct ModbusUdpMulticast {
    socket: UdpSocket,
}

impl ModbusUdpMulticast {
    /// Create multicast receiver
    pub fn new(multicast_addr: &str, port: u16) -> Result<Self, ModbusError> {
        let bind_addr: SocketAddr = format!("0.0.0.0:{}", port)
            .parse()
            .map_err(|e| ModbusError::ProtocolError(format!("Invalid address: {}", e)))?;
        
        let socket = UdpSocket::bind(bind_addr)?;
        
        // Join multicast group
        let multicast_ip: Ipv4Addr = multicast_addr.parse()
            .map_err(|e| ModbusError::ProtocolError(format!("Invalid multicast IP: {}", e)))?;
        
        socket.join_multicast_v4(&multicast_ip, &Ipv4Addr::UNSPECIFIED)?;
        
        println!("Listening for multicast on {}:{}", multicast_addr, port);
        
        Ok(ModbusUdpMulticast { socket })
    }
    
    /// Receive multicast packet
    pub fn receive(&self) -> Result<(Vec<u8>, SocketAddr), ModbusError> {
        let mut buffer = [0u8; 260];
        let (size, src) = self.socket.recv_from(&mut buffer)?;
        Ok((buffer[..size].to_vec(), src))
    }
}

// Example usage
fn main() -> Result<(), ModbusError> {
    // Standard UDP client
    let mut ctx = ModbusUdpContext::new("192.168.1.100", MODBUS_UDP_PORT)?;
    
    // Read holding registers
    match ctx.read_holding_registers(1, 0, 10) {
        Ok(registers) => {
            println!("Read {} registers:", registers.len());
            for (i, value) in registers.iter().enumerate() {
                println!("Register {}: {}", i, value);
            }
        }
        Err(e) => eprintln!("Read error: {:?}", e),
    }
    
    // Write single register
    match ctx.write_single_register(1, 0, 1234) {
        Ok(_) => println!("Successfully wrote register"),
        Err(e) => eprintln!("Write error: {:?}", e),
    }
    
    // Multicast example (commented out to avoid blocking)
    // let multicast = ModbusUdpMulticast::new("239.0.0.1", 502)?;
    // loop {
    //     match multicast.receive() {
    //         Ok((data, src)) => {
    //             println!("Received {} bytes from {}", data.len(), src);
    //         }
    //         Err(e) => eprintln!("Receive error: {:?}", e),
    //     }
    // }
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_mbap_header() {
        let ctx = ModbusUdpContext {
            socket: UdpSocket::bind("0.0.0.0:0").unwrap(),
            server_addr: "127.0.0.1:502".parse().unwrap(),
            transaction_id: 0x1234,
        };
        
        let header = ctx.build_mbap_header(6, 1);
        assert_eq!(header[0], 0x12);
        assert_eq!(header[1], 0x34);
        assert_eq!(header[2], 0x00);
        assert_eq!(header[3], 0x00);
        assert_eq!(header[4], 0x00);
        assert_eq!(header[5], 0x06);
        assert_eq!(header[6], 0x01);
    }
}
```

## Summary

**Modbus UDP Transport Layer** provides a lightweight, connectionless alternative to TCP-based Modbus communication, particularly suited for scenarios requiring multicast capabilities, reduced protocol overhead, or high-speed polling applications.

**Key Advantages:**
- **Lower latency** - No connection establishment overhead
- **Multicast support** - One-to-many communication patterns
- **Reduced bandwidth** - Simpler protocol headers
- **Faster polling** - Ideal for high-frequency status updates
- **Simplified implementation** - No connection state management

**Key Disadvantages:**
- **No delivery guarantee** - Packets can be lost without notification
- **No built-in reliability** - Application must handle retransmission
- **No flow control** - Can lead to buffer overflow in high-traffic scenarios
- **Security concerns** - More vulnerable to spoofing attacks

**Best Practices:**
1. Implement application-level acknowledgments for critical operations
2. Use sequence numbers (transaction IDs) to detect lost packets
3. Set appropriate socket timeouts
4. Consider UDP only for non-critical monitoring or where occasional data loss is acceptable
5. Implement security measures (IPsec, VPN) for sensitive deployments
6. Use multicast wisely to avoid network congestion

**Typical Applications:**
- Real-time monitoring dashboards
- Distributed sensor networks with periodic updates
- Broadcasting setpoints to multiple devices simultaneously
- High-speed data acquisition where occasional sample loss is tolerable
- Embedded systems with limited TCP/IP stack capabilities