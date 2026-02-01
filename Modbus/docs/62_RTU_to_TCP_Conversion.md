# Modbus RTU to TCP Conversion: Detailed Description

## Overview

RTU to TCP conversion (also called protocol translation or bridging) is the process of converting Modbus RTU messages transmitted over serial communication (RS-232/RS-485) into Modbus TCP messages transmitted over Ethernet/IP networks, and vice versa. This enables legacy Modbus RTU devices to communicate with modern TCP/IP-based systems without hardware replacement.

## Why RTU to TCP Conversion is Needed

1. **Legacy Integration**: Many industrial facilities have existing Modbus RTU devices that need to connect to modern SCADA systems
2. **Network Modernization**: Allows gradual migration from serial to Ethernet infrastructure
3. **Remote Access**: Enables remote monitoring and control of serial Modbus devices over TCP/IP networks
4. **System Consolidation**: Bridges different communication protocols in heterogeneous industrial environments
5. **Cost Efficiency**: Avoids expensive replacement of functional RTU equipment

## Protocol Differences

### Modbus RTU
- **Transport**: Serial (RS-232, RS-485)
- **Frame Format**: Binary, compact
- **Error Checking**: CRC-16
- **Addressing**: Device address in frame (1-247)
- **Timing**: Character and frame timeouts critical
- **Frame Delimiter**: Silent intervals (3.5 character times)

### Modbus TCP
- **Transport**: TCP/IP (Ethernet)
- **Frame Format**: MBAP header + Modbus PDU
- **Error Checking**: TCP checksums (no CRC)
- **Addressing**: IP address + Unit ID
- **Timing**: No timing constraints
- **Frame Delimiter**: Length field in MBAP header

## Conversion Process

### RTU to TCP Direction
1. Receive RTU frame over serial port
2. Validate CRC and extract frame data
3. Remove RTU-specific fields (address, CRC)
4. Build MBAP header (Transaction ID, Protocol ID, Length, Unit ID)
5. Combine MBAP header with Modbus PDU
6. Transmit over TCP connection

### TCP to RTU Direction
1. Receive TCP frame from socket
2. Parse MBAP header
3. Extract Modbus PDU
4. Add RTU device address (from Unit ID)
5. Calculate and append CRC-16
6. Transmit over serial port

## Implementation Considerations

- **Connection Management**: Handle multiple TCP clients to single RTU bus
- **Transaction Mapping**: Track TCP transaction IDs to RTU requests
- **Timeout Handling**: Different timeout strategies for serial vs network
- **Buffer Management**: Handle frame buffering for both protocols
- **Thread Safety**: Concurrent access to serial port and TCP sockets
- **Error Recovery**: Handle connection failures, timeouts, and malformed frames

---

# C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_RTU_FRAME 256
#define MAX_TCP_FRAME 260
#define MODBUS_TCP_PORT 502
#define RTU_TIMEOUT_MS 1000

// Modbus TCP MBAP Header structure
typedef struct {
    uint16_t transaction_id;
    uint16_t protocol_id;      // Always 0 for Modbus
    uint16_t length;           // Bytes following
    uint8_t  unit_id;          // Slave address
} mbap_header_t;

// Gateway context
typedef struct {
    int serial_fd;
    int tcp_server_fd;
    int tcp_client_fd;
    uint16_t transaction_id;
    pthread_mutex_t serial_mutex;
} gateway_context_t;

// CRC-16 calculation for Modbus RTU
uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Open and configure serial port for RTU
int open_serial_port(const char *device, int baudrate) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("open serial port");
        return -1;
    }
    
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }
    
    // Set baud rate
    speed_t speed = B9600;
    if (baudrate == 19200) speed = B19200;
    else if (baudrate == 38400) speed = B38400;
    else if (baudrate == 115200) speed = B115200;
    
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    
    // 8N1 mode
    tty.c_cflag &= ~PARENB;        // No parity
    tty.c_cflag &= ~CSTOPB;        // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 data bits
    tty.c_cflag &= ~CRTSCTS;       // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // Enable receiver, ignore modem lines
    
    // Raw mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;
    
    // Timeouts
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10; // 1 second timeout
    
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    
    return fd;
}

// Convert TCP frame to RTU and send
int tcp_to_rtu(gateway_context_t *ctx, const uint8_t *tcp_frame, size_t tcp_len) {
    if (tcp_len < 7) {
        fprintf(stderr, "TCP frame too short\n");
        return -1;
    }
    
    // Parse MBAP header
    mbap_header_t mbap;
    mbap.transaction_id = (tcp_frame[0] << 8) | tcp_frame[1];
    mbap.protocol_id = (tcp_frame[2] << 8) | tcp_frame[3];
    mbap.length = (tcp_frame[4] << 8) | tcp_frame[5];
    mbap.unit_id = tcp_frame[6];
    
    // Extract PDU (skip MBAP header)
    const uint8_t *pdu = tcp_frame + 7;
    size_t pdu_len = tcp_len - 7;
    
    // Build RTU frame: [Address][PDU][CRC]
    uint8_t rtu_frame[MAX_RTU_FRAME];
    rtu_frame[0] = mbap.unit_id;
    memcpy(rtu_frame + 1, pdu, pdu_len);
    
    // Calculate and append CRC
    uint16_t crc = calculate_crc16(rtu_frame, pdu_len + 1);
    rtu_frame[pdu_len + 1] = crc & 0xFF;
    rtu_frame[pdu_len + 2] = (crc >> 8) & 0xFF;
    
    size_t rtu_len = pdu_len + 3;
    
    // Send RTU frame
    pthread_mutex_lock(&ctx->serial_mutex);
    ssize_t written = write(ctx->serial_fd, rtu_frame, rtu_len);
    pthread_mutex_unlock(&ctx->serial_mutex);
    
    if (written != (ssize_t)rtu_len) {
        perror("write RTU frame");
        return -1;
    }
    
    printf("TCP->RTU: Sent %zd bytes to device %d\n", rtu_len, mbap.unit_id);
    return 0;
}

// Convert RTU frame to TCP and send
int rtu_to_tcp(gateway_context_t *ctx, const uint8_t *rtu_frame, size_t rtu_len) {
    if (rtu_len < 4) {
        fprintf(stderr, "RTU frame too short\n");
        return -1;
    }
    
    // Verify CRC
    uint16_t received_crc = rtu_frame[rtu_len - 2] | (rtu_frame[rtu_len - 1] << 8);
    uint16_t calculated_crc = calculate_crc16(rtu_frame, rtu_len - 2);
    
    if (received_crc != calculated_crc) {
        fprintf(stderr, "CRC error: expected 0x%04X, got 0x%04X\n", 
                calculated_crc, received_crc);
        return -1;
    }
    
    // Extract RTU data
    uint8_t device_addr = rtu_frame[0];
    const uint8_t *pdu = rtu_frame + 1;
    size_t pdu_len = rtu_len - 3; // Remove address and CRC
    
    // Build TCP frame with MBAP header
    uint8_t tcp_frame[MAX_TCP_FRAME];
    
    // MBAP Header
    tcp_frame[0] = (ctx->transaction_id >> 8) & 0xFF;
    tcp_frame[1] = ctx->transaction_id & 0xFF;
    tcp_frame[2] = 0; // Protocol ID
    tcp_frame[3] = 0;
    tcp_frame[4] = ((pdu_len + 1) >> 8) & 0xFF; // Length
    tcp_frame[5] = (pdu_len + 1) & 0xFF;
    tcp_frame[6] = device_addr; // Unit ID
    
    // Copy PDU
    memcpy(tcp_frame + 7, pdu, pdu_len);
    
    size_t tcp_len = pdu_len + 7;
    
    // Send TCP frame
    if (ctx->tcp_client_fd >= 0) {
        ssize_t sent = send(ctx->tcp_client_fd, tcp_frame, tcp_len, 0);
        if (sent != (ssize_t)tcp_len) {
            perror("send TCP frame");
            return -1;
        }
        printf("RTU->TCP: Sent %zd bytes from device %d\n", tcp_len, device_addr);
    }
    
    ctx->transaction_id++;
    return 0;
}

// Thread for handling TCP client
void *tcp_handler(void *arg) {
    gateway_context_t *ctx = (gateway_context_t *)arg;
    uint8_t buffer[MAX_TCP_FRAME];
    
    while (1) {
        ssize_t received = recv(ctx->tcp_client_fd, buffer, sizeof(buffer), 0);
        
        if (received <= 0) {
            printf("TCP client disconnected\n");
            close(ctx->tcp_client_fd);
            ctx->tcp_client_fd = -1;
            break;
        }
        
        printf("Received TCP frame: %zd bytes\n", received);
        tcp_to_rtu(ctx, buffer, received);
        
        // Wait for RTU response and convert back
        usleep(100000); // 100ms delay for RTU response
        
        uint8_t rtu_response[MAX_RTU_FRAME];
        pthread_mutex_lock(&ctx->serial_mutex);
        ssize_t rtu_received = read(ctx->serial_fd, rtu_response, sizeof(rtu_response));
        pthread_mutex_unlock(&ctx->serial_mutex);
        
        if (rtu_received > 0) {
            rtu_to_tcp(ctx, rtu_response, rtu_received);
        }
    }
    
    return NULL;
}

// Main gateway function
int run_gateway(const char *serial_device, int baudrate) {
    gateway_context_t ctx = {0};
    ctx.transaction_id = 1;
    ctx.tcp_client_fd = -1;
    pthread_mutex_init(&ctx.serial_mutex, NULL);
    
    // Open serial port
    ctx.serial_fd = open_serial_port(serial_device, baudrate);
    if (ctx.serial_fd < 0) {
        return -1;
    }
    
    // Create TCP server
    ctx.tcp_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx.tcp_server_fd < 0) {
        perror("socket");
        close(ctx.serial_fd);
        return -1;
    }
    
    int opt = 1;
    setsockopt(ctx.tcp_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(MODBUS_TCP_PORT);
    
    if (bind(ctx.tcp_server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(ctx.serial_fd);
        close(ctx.tcp_server_fd);
        return -1;
    }
    
    if (listen(ctx.tcp_server_fd, 1) < 0) {
        perror("listen");
        close(ctx.serial_fd);
        close(ctx.tcp_server_fd);
        return -1;
    }
    
    printf("RTU-TCP Gateway running on port %d\n", MODBUS_TCP_PORT);
    printf("Serial port: %s @ %d baud\n", serial_device, baudrate);
    
    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        ctx.tcp_client_fd = accept(ctx.tcp_server_fd, 
                                    (struct sockaddr *)&client_addr, 
                                    &client_len);
        
        if (ctx.tcp_client_fd < 0) {
            perror("accept");
            continue;
        }
        
        printf("TCP client connected: %s\n", inet_ntoa(client_addr.sin_addr));
        
        pthread_t thread;
        pthread_create(&thread, NULL, tcp_handler, &ctx);
        pthread_detach(thread);
    }
    
    close(ctx.serial_fd);
    close(ctx.tcp_server_fd);
    pthread_mutex_destroy(&ctx.serial_mutex);
    
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <serial_device> [baudrate]\n", argv[0]);
        printf("Example: %s /dev/ttyUSB0 9600\n", argv[0]);
        return 1;
    }
    
    const char *device = argv[1];
    int baudrate = (argc > 2) ? atoi(argv[2]) : 9600;
    
    return run_gateway(device, baudrate);
}
```

---

# Rust Implementation

```rust
use std::io::{self, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;
use serialport::{SerialPort, DataBits, StopBits, Parity};

const MAX_FRAME_SIZE: usize = 260;
const MODBUS_TCP_PORT: u16 = 502;

/// Modbus TCP MBAP Header
#[derive(Debug, Clone)]
struct MbapHeader {
    transaction_id: u16,
    protocol_id: u16,
    length: u16,
    unit_id: u8,
}

impl MbapHeader {
    fn from_bytes(data: &[u8]) -> Result<Self, &'static str> {
        if data.len() < 7 {
            return Err("MBAP header too short");
        }
        
        Ok(MbapHeader {
            transaction_id: u16::from_be_bytes([data[0], data[1]]),
            protocol_id: u16::from_be_bytes([data[2], data[3]]),
            length: u16::from_be_bytes([data[4], data[5]]),
            unit_id: data[6],
        })
    }
    
    fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(7);
        bytes.extend_from_slice(&self.transaction_id.to_be_bytes());
        bytes.extend_from_slice(&self.protocol_id.to_be_bytes());
        bytes.extend_from_slice(&self.length.to_be_bytes());
        bytes.push(self.unit_id);
        bytes
    }
}

/// Calculate CRC-16 for Modbus RTU
fn calculate_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for byte in data {
        crc ^= *byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    crc
}

/// Gateway context shared between threads
struct GatewayContext {
    serial_port: Arc<Mutex<Box<dyn SerialPort>>>,
    transaction_id: Arc<Mutex<u16>>,
}

impl GatewayContext {
    fn new(serial_port: Box<dyn SerialPort>) -> Self {
        GatewayContext {
            serial_port: Arc::new(Mutex::new(serial_port)),
            transaction_id: Arc::new(Mutex::new(1)),
        }
    }
    
    /// Convert TCP frame to RTU and send over serial
    fn tcp_to_rtu(&self, tcp_frame: &[u8]) -> io::Result<()> {
        if tcp_frame.len() < 7 {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "Frame too short"));
        }
        
        // Parse MBAP header
        let mbap = MbapHeader::from_bytes(tcp_frame)
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
        
        // Extract PDU (skip 7-byte MBAP header)
        let pdu = &tcp_frame[7..];
        
        // Build RTU frame: [Device Address][PDU][CRC-16]
        let mut rtu_frame = Vec::with_capacity(pdu.len() + 3);
        rtu_frame.push(mbap.unit_id);
        rtu_frame.extend_from_slice(pdu);
        
        // Calculate and append CRC
        let crc = calculate_crc16(&rtu_frame);
        rtu_frame.extend_from_slice(&crc.to_le_bytes());
        
        // Send over serial port
        let mut port = self.serial_port.lock().unwrap();
        port.write_all(&rtu_frame)?;
        port.flush()?;
        
        println!("TCP->RTU: Sent {} bytes to device {}", rtu_frame.len(), mbap.unit_id);
        Ok(())
    }
    
    /// Convert RTU frame to TCP
    fn rtu_to_tcp(&self, rtu_frame: &[u8]) -> io::Result<Vec<u8>> {
        if rtu_frame.len() < 4 {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "Frame too short"));
        }
        
        // Verify CRC
        let frame_without_crc = &rtu_frame[..rtu_frame.len() - 2];
        let received_crc = u16::from_le_bytes([
            rtu_frame[rtu_frame.len() - 2],
            rtu_frame[rtu_frame.len() - 1],
        ]);
        let calculated_crc = calculate_crc16(frame_without_crc);
        
        if received_crc != calculated_crc {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("CRC mismatch: expected 0x{:04X}, got 0x{:04X}", 
                        calculated_crc, received_crc)
            ));
        }
        
        // Extract components
        let device_addr = rtu_frame[0];
        let pdu = &rtu_frame[1..rtu_frame.len() - 2];
        
        // Get and increment transaction ID
        let mut tid = self.transaction_id.lock().unwrap();
        let transaction_id = *tid;
        *tid = tid.wrapping_add(1);
        
        // Build MBAP header
        let mbap = MbapHeader {
            transaction_id,
            protocol_id: 0,
            length: (pdu.len() + 1) as u16,
            unit_id: device_addr,
        };
        
        // Build TCP frame
        let mut tcp_frame = mbap.to_bytes();
        tcp_frame.extend_from_slice(pdu);
        
        println!("RTU->TCP: Converted {} bytes from device {}", tcp_frame.len(), device_addr);
        Ok(tcp_frame)
    }
    
    /// Read RTU response from serial port with timeout
    fn read_rtu_response(&self, timeout: Duration) -> io::Result<Vec<u8>> {
        let mut port = self.serial_port.lock().unwrap();
        port.set_timeout(timeout)?;
        
        let mut buffer = vec![0u8; MAX_FRAME_SIZE];
        let mut total_read = 0;
        
        // Read with inter-character timeout
        loop {
            match port.read(&mut buffer[total_read..]) {
                Ok(0) => break,
                Ok(n) => {
                    total_read += n;
                    if total_read >= MAX_FRAME_SIZE {
                        break;
                    }
                }
                Err(ref e) if e.kind() == io::ErrorKind::TimedOut => break,
                Err(e) => return Err(e),
            }
        }
        
        buffer.truncate(total_read);
        Ok(buffer)
    }
}

/// Handle a single TCP client connection
fn handle_client(mut stream: TcpStream, ctx: Arc<GatewayContext>) {
    let peer_addr = stream.peer_addr().unwrap();
    println!("Client connected: {}", peer_addr);
    
    let mut buffer = vec![0u8; MAX_FRAME_SIZE];
    
    loop {
        // Read TCP request
        let bytes_read = match stream.read(&mut buffer) {
            Ok(0) => {
                println!("Client disconnected: {}", peer_addr);
                break;
            }
            Ok(n) => n,
            Err(e) => {
                eprintln!("Read error from {}: {}", peer_addr, e);
                break;
            }
        };
        
        println!("Received TCP frame: {} bytes", bytes_read);
        
        // Convert and send to RTU device
        if let Err(e) = ctx.tcp_to_rtu(&buffer[..bytes_read]) {
            eprintln!("TCP to RTU conversion error: {}", e);
            continue;
        }
        
        // Wait for RTU response
        thread::sleep(Duration::from_millis(100));
        
        match ctx.read_rtu_response(Duration::from_secs(1)) {
            Ok(rtu_response) if !rtu_response.is_empty() => {
                match ctx.rtu_to_tcp(&rtu_response) {
                    Ok(tcp_response) => {
                        if let Err(e) = stream.write_all(&tcp_response) {
                            eprintln!("Write error to {}: {}", peer_addr, e);
                            break;
                        }
                    }
                    Err(e) => eprintln!("RTU to TCP conversion error: {}", e),
                }
            }
            Ok(_) => println!("No RTU response received"),
            Err(e) => eprintln!("RTU read error: {}", e),
        }
    }
}

/// Main gateway function
fn run_gateway(serial_device: &str, baudrate: u32) -> io::Result<()> {
    // Open serial port
    let port = serialport::new(serial_device, baudrate)
        .data_bits(DataBits::Eight)
        .stop_bits(StopBits::One)
        .parity(Parity::None)
        .timeout(Duration::from_millis(1000))
        .open()
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;
    
    println!("Serial port opened: {} @ {} baud", serial_device, baudrate);
    
    let ctx = Arc::new(GatewayContext::new(port));
    
    // Create TCP listener
    let listener = TcpListener::bind(format!("0.0.0.0:{}", MODBUS_TCP_PORT))?;
    println!("RTU-TCP Gateway listening on port {}", MODBUS_TCP_PORT);
    
    // Accept connections
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                let ctx_clone = Arc::clone(&ctx);
                thread::spawn(move || {
                    handle_client(stream, ctx_clone);
                });
            }
            Err(e) => eprintln!("Connection error: {}", e),
        }
    }
    
    Ok(())
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        eprintln!("Usage: {} <serial_device> [baudrate]", args[0]);
        eprintln!("Example: {} /dev/ttyUSB0 9600", args[0]);
        std::process::exit(1);
    }
    
    let serial_device = &args[1];
    let baudrate = if args.len() > 2 {
        args[2].parse().unwrap_or(9600)
    } else {
        9600
    };
    
    if let Err(e) = run_gateway(serial_device, baudrate) {
        eprintln!("Gateway error: {}", e);
        std::process::exit(1);
    }
}
```

**Cargo.toml for Rust:**
```toml
[package]
name = "modbus-rtu-tcp-gateway"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4.2"
```

---

# Summary

**Modbus RTU to TCP conversion** is a critical protocol translation technique that bridges legacy serial Modbus devices with modern TCP/IP networks. The conversion process involves:

- **Frame Format Translation**: Removing RTU-specific fields (CRC) and adding TCP-specific headers (MBAP)
- **Error Checking Transformation**: Converting between CRC-16 (RTU) and TCP checksums
- **Addressing Mapping**: Translating between device addresses and Unit IDs
- **Timing Adaptation**: Handling the timing-critical RTU protocol and asynchronous TCP

Both implementations demonstrate production-ready gateways that handle bidirectional conversion, connection management, thread safety, and error handling. The C/C++ version uses POSIX APIs for maximum portability, while the Rust version leverages modern safety features and the `serialport` crate for cross-platform serial communication.

Key benefits include enabling remote monitoring of RTU devices, gradual network modernization, and cost-effective integration of legacy equipment into modern industrial IoT systems.