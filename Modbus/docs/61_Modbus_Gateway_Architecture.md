# Modbus Gateway Architecture

## Detailed Description

A Modbus Gateway is a critical infrastructure component that enables communication between different Modbus network types (RTU, ASCII, and TCP/IP) and protocols. It acts as a protocol translator and message router, allowing devices using different Modbus variants to communicate seamlessly within industrial automation systems.

### Core Concepts

**Gateway Functions:**
- Protocol translation between Modbus RTU, ASCII, and TCP
- Message routing and forwarding
- Data buffering and synchronization
- Network isolation and segmentation
- Protocol-specific frame conversion
- Connection management for multiple clients/servers

**Architecture Components:**

1. **Network Interfaces**: Physical connections for serial (RS-232/RS-485) and Ethernet networks
2. **Protocol Handlers**: Independent modules for each Modbus variant
3. **Routing Engine**: Directs messages between networks based on device addressing
4. **Translation Layer**: Converts protocol data units (PDUs) between formats
5. **Configuration Manager**: Stores device mappings and routing rules
6. **Buffer Manager**: Handles asynchronous communication timing differences

**Common Use Cases:**
- Connecting legacy RTU devices to modern TCP/IP networks
- Integrating multi-vendor systems using different Modbus variants
- Creating network hierarchies with RTU/ASCII subnets
- Providing remote access to serial Modbus devices via Ethernet

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// Protocol types
typedef enum {
    MODBUS_RTU,
    MODBUS_ASCII,
    MODBUS_TCP
} ModbusProtocol;

// Device route mapping
typedef struct {
    uint8_t slave_id;
    ModbusProtocol source_protocol;
    ModbusProtocol dest_protocol;
    char dest_interface[64];
    int dest_port;
} RouteEntry;

// Message structure for gateway queue
typedef struct {
    uint8_t slave_id;
    uint8_t function_code;
    uint8_t data[256];
    size_t data_len;
    ModbusProtocol source_protocol;
    void *response_context;
} ModbusMessage;

// Gateway configuration
typedef struct {
    RouteEntry routes[256];
    int route_count;
    pthread_mutex_t route_lock;
    
    // Protocol-specific buffers
    uint8_t rtu_buffer[512];
    uint8_t ascii_buffer[512];
    uint8_t tcp_buffer[512];
    
    // Connection tracking
    int tcp_socket;
    int rtu_fd;
    bool running;
} ModbusGateway;

// CRC16 for Modbus RTU
uint16_t modbus_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
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

// LRC for Modbus ASCII
uint8_t modbus_lrc(const uint8_t *data, size_t len) {
    uint8_t lrc = 0;
    for (size_t i = 0; i < len; i++) {
        lrc += data[i];
    }
    return (uint8_t)(-(int8_t)lrc);
}

// Convert RTU frame to TCP
int rtu_to_tcp(const uint8_t *rtu_frame, size_t rtu_len, 
               uint8_t *tcp_frame, uint16_t transaction_id) {
    if (rtu_len < 4) return -1; // Invalid RTU frame
    
    // MBAP Header
    tcp_frame[0] = (transaction_id >> 8) & 0xFF;
    tcp_frame[1] = transaction_id & 0xFF;
    tcp_frame[2] = 0x00; // Protocol ID
    tcp_frame[3] = 0x00;
    
    // Length (slave ID + function + data, no CRC)
    uint16_t length = rtu_len - 2; // Exclude CRC
    tcp_frame[4] = (length >> 8) & 0xFF;
    tcp_frame[5] = length & 0xFF;
    
    // Copy slave ID, function code, and data (exclude CRC)
    memcpy(&tcp_frame[6], rtu_frame, rtu_len - 2);
    
    return 6 + length;
}

// Convert TCP frame to RTU
int tcp_to_rtu(const uint8_t *tcp_frame, size_t tcp_len, uint8_t *rtu_frame) {
    if (tcp_len < 8) return -1; // Invalid TCP frame
    
    // Extract length from MBAP header
    uint16_t length = (tcp_frame[4] << 8) | tcp_frame[5];
    
    // Copy PDU (slave ID + function + data)
    memcpy(rtu_frame, &tcp_frame[6], length);
    
    // Calculate and append CRC
    uint16_t crc = modbus_crc16(rtu_frame, length);
    rtu_frame[length] = crc & 0xFF;
    rtu_frame[length + 1] = (crc >> 8) & 0xFF;
    
    return length + 2;
}

// Convert RTU to ASCII
int rtu_to_ascii(const uint8_t *rtu_frame, size_t rtu_len, uint8_t *ascii_frame) {
    if (rtu_len < 4) return -1;
    
    int pos = 0;
    ascii_frame[pos++] = ':'; // Start character
    
    // Convert each byte to ASCII hex (exclude CRC)
    for (size_t i = 0; i < rtu_len - 2; i++) {
        sprintf((char*)&ascii_frame[pos], "%02X", rtu_frame[i]);
        pos += 2;
    }
    
    // Calculate LRC on original data (no CRC)
    uint8_t lrc = modbus_lrc(rtu_frame, rtu_len - 2);
    sprintf((char*)&ascii_frame[pos], "%02X", lrc);
    pos += 2;
    
    // End characters
    ascii_frame[pos++] = '\r';
    ascii_frame[pos++] = '\n';
    
    return pos;
}

// Initialize gateway
ModbusGateway* gateway_init(void) {
    ModbusGateway *gw = (ModbusGateway*)malloc(sizeof(ModbusGateway));
    if (!gw) return NULL;
    
    memset(gw, 0, sizeof(ModbusGateway));
    pthread_mutex_init(&gw->route_lock, NULL);
    gw->running = true;
    
    return gw;
}

// Add routing entry
bool gateway_add_route(ModbusGateway *gw, uint8_t slave_id,
                       ModbusProtocol src, ModbusProtocol dest,
                       const char *dest_interface, int dest_port) {
    pthread_mutex_lock(&gw->route_lock);
    
    if (gw->route_count >= 256) {
        pthread_mutex_unlock(&gw->route_lock);
        return false;
    }
    
    RouteEntry *route = &gw->routes[gw->route_count++];
    route->slave_id = slave_id;
    route->source_protocol = src;
    route->dest_protocol = dest;
    strncpy(route->dest_interface, dest_interface, sizeof(route->dest_interface) - 1);
    route->dest_port = dest_port;
    
    pthread_mutex_unlock(&gw->route_lock);
    return true;
}

// Find route for slave ID
RouteEntry* gateway_find_route(ModbusGateway *gw, uint8_t slave_id, 
                               ModbusProtocol source) {
    pthread_mutex_lock(&gw->route_lock);
    
    for (int i = 0; i < gw->route_count; i++) {
        if (gw->routes[i].slave_id == slave_id && 
            gw->routes[i].source_protocol == source) {
            pthread_mutex_unlock(&gw->route_lock);
            return &gw->routes[i];
        }
    }
    
    pthread_mutex_unlock(&gw->route_lock);
    return NULL;
}

// Process and route message
int gateway_route_message(ModbusGateway *gw, const uint8_t *frame, 
                          size_t frame_len, ModbusProtocol source,
                          uint8_t *output, ModbusProtocol *output_protocol) {
    // Extract slave ID (first byte for RTU/ASCII, byte 6 for TCP)
    uint8_t slave_id = (source == MODBUS_TCP) ? frame[6] : frame[0];
    
    // Find routing entry
    RouteEntry *route = gateway_find_route(gw, slave_id, source);
    if (!route) {
        fprintf(stderr, "No route found for slave %d\n", slave_id);
        return -1;
    }
    
    *output_protocol = route->dest_protocol;
    int output_len = 0;
    
    // Perform protocol conversion
    if (source == MODBUS_RTU && route->dest_protocol == MODBUS_TCP) {
        static uint16_t transaction_id = 0;
        output_len = rtu_to_tcp(frame, frame_len, output, transaction_id++);
    } 
    else if (source == MODBUS_TCP && route->dest_protocol == MODBUS_RTU) {
        output_len = tcp_to_rtu(frame, frame_len, output);
    }
    else if (source == MODBUS_RTU && route->dest_protocol == MODBUS_ASCII) {
        output_len = rtu_to_ascii(frame, frame_len, output);
    }
    else {
        // Same protocol, direct copy
        memcpy(output, frame, frame_len);
        output_len = frame_len;
    }
    
    return output_len;
}

// Example usage
int main(void) {
    ModbusGateway *gateway = gateway_init();
    
    // Configure routing: RTU slave 1 -> TCP
    gateway_add_route(gateway, 1, MODBUS_RTU, MODBUS_TCP, 
                     "192.168.1.100", 502);
    
    // Configure routing: TCP slave 2 -> RTU
    gateway_add_route(gateway, 2, MODBUS_TCP, MODBUS_RTU, 
                     "/dev/ttyUSB0", 9600);
    
    // Simulate RTU frame (Read Holding Registers)
    uint8_t rtu_frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint16_t crc = modbus_crc16(rtu_frame, 6);
    rtu_frame[6] = crc & 0xFF;
    rtu_frame[7] = (crc >> 8) & 0xFF;
    
    uint8_t output[512];
    ModbusProtocol output_protocol;
    
    int len = gateway_route_message(gateway, rtu_frame, 8, MODBUS_RTU,
                                    output, &output_protocol);
    
    if (len > 0) {
        printf("Converted RTU->TCP, length: %d bytes\n", len);
        printf("MBAP Header: ");
        for (int i = 0; i < 6; i++) {
            printf("%02X ", output[i]);
        }
        printf("\nPDU: ");
        for (int i = 6; i < len; i++) {
            printf("%02X ", output[i]);
        }
        printf("\n");
    }
    
    free(gateway);
    return 0;
}
```

## Rust Implementation

```rust
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::io::{self, Error, ErrorKind};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum ModbusProtocol {
    RTU,
    ASCII,
    TCP,
}

#[derive(Debug, Clone)]
struct RouteEntry {
    slave_id: u8,
    source_protocol: ModbusProtocol,
    dest_protocol: ModbusProtocol,
    dest_interface: String,
    dest_port: u16,
}

struct ModbusGateway {
    routes: Arc<Mutex<HashMap<(u8, ModbusProtocol), RouteEntry>>>,
    transaction_id: Arc<Mutex<u16>>,
}

impl ModbusGateway {
    fn new() -> Self {
        ModbusGateway {
            routes: Arc::new(Mutex::new(HashMap::new())),
            transaction_id: Arc::new(Mutex::new(0)),
        }
    }

    fn add_route(
        &mut self,
        slave_id: u8,
        source: ModbusProtocol,
        dest: ModbusProtocol,
        interface: String,
        port: u16,
    ) {
        let route = RouteEntry {
            slave_id,
            source_protocol: source,
            dest_protocol: dest,
            dest_interface: interface,
            dest_port: port,
        };

        let mut routes = self.routes.lock().unwrap();
        routes.insert((slave_id, source), route);
    }

    fn find_route(&self, slave_id: u8, source: ModbusProtocol) -> Option<RouteEntry> {
        let routes = self.routes.lock().unwrap();
        routes.get(&(slave_id, source)).cloned()
    }

    fn route_message(
        &self,
        frame: &[u8],
        source: ModbusProtocol,
    ) -> io::Result<(Vec<u8>, ModbusProtocol)> {
        // Extract slave ID
        let slave_id = match source {
            ModbusProtocol::TCP => {
                if frame.len() < 7 {
                    return Err(Error::new(ErrorKind::InvalidData, "Invalid TCP frame"));
                }
                frame[6]
            }
            _ => {
                if frame.is_empty() {
                    return Err(Error::new(ErrorKind::InvalidData, "Empty frame"));
                }
                frame[0]
            }
        };

        // Find route
        let route = self.find_route(slave_id, source)
            .ok_or_else(|| Error::new(ErrorKind::NotFound, "No route found"))?;

        // Convert protocol
        let output = match (source, route.dest_protocol) {
            (ModbusProtocol::RTU, ModbusProtocol::TCP) => {
                let mut tid = self.transaction_id.lock().unwrap();
                let result = Self::rtu_to_tcp(frame, *tid)?;
                *tid = tid.wrapping_add(1);
                result
            }
            (ModbusProtocol::TCP, ModbusProtocol::RTU) => Self::tcp_to_rtu(frame)?,
            (ModbusProtocol::RTU, ModbusProtocol::ASCII) => Self::rtu_to_ascii(frame)?,
            (ModbusProtocol::ASCII, ModbusProtocol::RTU) => Self::ascii_to_rtu(frame)?,
            _ => frame.to_vec(), // Same protocol
        };

        Ok((output, route.dest_protocol))
    }

    // CRC16 calculation for Modbus RTU
    fn crc16(data: &[u8]) -> u16 {
        let mut crc: u16 = 0xFFFF;
        for &byte in data {
            crc ^= byte as u16;
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

    // LRC calculation for Modbus ASCII
    fn lrc(data: &[u8]) -> u8 {
        let sum: u8 = data.iter().fold(0u8, |acc, &x| acc.wrapping_add(x));
        (-(sum as i8)) as u8
    }

    // Convert RTU to TCP
    fn rtu_to_tcp(rtu_frame: &[u8], transaction_id: u16) -> io::Result<Vec<u8>> {
        if rtu_frame.len() < 4 {
            return Err(Error::new(ErrorKind::InvalidData, "RTU frame too short"));
        }

        let mut tcp_frame = Vec::with_capacity(rtu_frame.len() + 4);

        // MBAP Header
        tcp_frame.push((transaction_id >> 8) as u8);
        tcp_frame.push((transaction_id & 0xFF) as u8);
        tcp_frame.push(0x00); // Protocol ID
        tcp_frame.push(0x00);

        // Length (exclude CRC from RTU)
        let length = (rtu_frame.len() - 2) as u16;
        tcp_frame.push((length >> 8) as u8);
        tcp_frame.push((length & 0xFF) as u8);

        // PDU (slave ID + function + data, no CRC)
        tcp_frame.extend_from_slice(&rtu_frame[..rtu_frame.len() - 2]);

        Ok(tcp_frame)
    }

    // Convert TCP to RTU
    fn tcp_to_rtu(tcp_frame: &[u8]) -> io::Result<Vec<u8>> {
        if tcp_frame.len() < 8 {
            return Err(Error::new(ErrorKind::InvalidData, "TCP frame too short"));
        }

        // Extract length from MBAP header
        let length = ((tcp_frame[4] as usize) << 8) | (tcp_frame[5] as usize);

        let mut rtu_frame = Vec::with_capacity(length + 2);
        rtu_frame.extend_from_slice(&tcp_frame[6..6 + length]);

        // Calculate and append CRC
        let crc = Self::crc16(&rtu_frame);
        rtu_frame.push((crc & 0xFF) as u8);
        rtu_frame.push((crc >> 8) as u8);

        Ok(rtu_frame)
    }

    // Convert RTU to ASCII
    fn rtu_to_ascii(rtu_frame: &[u8]) -> io::Result<Vec<u8>> {
        if rtu_frame.len() < 4 {
            return Err(Error::new(ErrorKind::InvalidData, "RTU frame too short"));
        }

        let mut ascii_frame = Vec::new();
        ascii_frame.push(b':'); // Start character

        // Convert to ASCII hex (exclude CRC)
        for &byte in &rtu_frame[..rtu_frame.len() - 2] {
            ascii_frame.extend_from_slice(format!("{:02X}", byte).as_bytes());
        }

        // Calculate and append LRC
        let lrc = Self::lrc(&rtu_frame[..rtu_frame.len() - 2]);
        ascii_frame.extend_from_slice(format!("{:02X}", lrc).as_bytes());

        // End characters
        ascii_frame.push(b'\r');
        ascii_frame.push(b'\n');

        Ok(ascii_frame)
    }

    // Convert ASCII to RTU
    fn ascii_to_rtu(ascii_frame: &[u8]) -> io::Result<Vec<u8>> {
        if ascii_frame.len() < 5 || ascii_frame[0] != b':' {
            return Err(Error::new(ErrorKind::InvalidData, "Invalid ASCII frame"));
        }

        // Remove start/end characters and parse hex
        let hex_data = &ascii_frame[1..ascii_frame.len() - 2];
        let mut rtu_frame = Vec::new();

        for chunk in hex_data.chunks(2) {
            if chunk.len() == 2 {
                let hex_str = std::str::from_utf8(chunk)
                    .map_err(|_| Error::new(ErrorKind::InvalidData, "Invalid hex"))?;
                let byte = u8::from_str_radix(hex_str, 16)
                    .map_err(|_| Error::new(ErrorKind::InvalidData, "Invalid hex value"))?;
                rtu_frame.push(byte);
            }
        }

        // Remove LRC and add CRC
        if !rtu_frame.is_empty() {
            rtu_frame.pop(); // Remove LRC
        }

        let crc = Self::crc16(&rtu_frame);
        rtu_frame.push((crc & 0xFF) as u8);
        rtu_frame.push((crc >> 8) as u8);

        Ok(rtu_frame)
    }
}

fn main() {
    let mut gateway = ModbusGateway::new();

    // Configure routing
    gateway.add_route(1, ModbusProtocol::RTU, ModbusProtocol::TCP,
                     "192.168.1.100".to_string(), 502);
    gateway.add_route(2, ModbusProtocol::TCP, ModbusProtocol::RTU,
                     "/dev/ttyUSB0".to_string(), 9600);

    // Simulate RTU frame (Read Holding Registers)
    let mut rtu_frame = vec![0x01, 0x03, 0x00, 0x00, 0x00, 0x0A];
    let crc = ModbusGateway::crc16(&rtu_frame);
    rtu_frame.push((crc & 0xFF) as u8);
    rtu_frame.push((crc >> 8) as u8);

    match gateway.route_message(&rtu_frame, ModbusProtocol::RTU) {
        Ok((output, protocol)) => {
            println!("Converted RTU->TCP, length: {} bytes", output.len());
            print!("MBAP Header: ");
            for byte in &output[..6] {
                print!("{:02X} ", byte);
            }
            println!();
            print!("PDU: ");
            for byte in &output[6..] {
                print!("{:02X} ", byte);
            }
            println!("\nProtocol: {:?}", protocol);
        }
        Err(e) => eprintln!("Error: {}", e),
    }
}
```

## Summary

Modbus Gateway Architecture enables seamless communication across heterogeneous industrial networks by bridging RTU, ASCII, and TCP protocols. The gateway performs protocol translation, message routing, and network segmentation.

**Key implementation aspects include**: CRC16/LRC checksum conversion, MBAP header construction for TCP, frame format transformation, routing table management with slave ID mappings, and thread-safe concurrent access to routing configurations.

**The C/C++ implementation** demonstrates low-level protocol conversion with explicit buffer management, CRC/LRC calculation functions, and routing logic using mutexes for thread safety.

**The Rust implementation** leverages safe concurrency with Arc/Mutex, error handling with Result types, and idiomatic protocol conversion methods while maintaining zero-copy operations where possible.

Both implementations provide foundation for production gateways requiring bidirectional translation, connection pooling, and efficient message forwarding in multi-protocol Modbus environments common in industrial IoT and SCADA systems.