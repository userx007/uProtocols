# Modbus Protocol Overview

## Introduction

Modbus is one of the oldest and most widely deployed industrial communication protocols in the world. Developed by Modicon (now Schneider Electric) in 1979, it has become a de facto standard for connecting industrial electronic devices. Its longevity and widespread adoption stem from its simplicity, openness, and reliability.

## Historical Context

The Modbus protocol was created by Dick Morley and the Modicon team to enable communication between programmable logic controllers (PLCs). Initially designed for serial communication over RS-232 and RS-485 networks, Modbus has evolved to support modern Ethernet-based networks. The protocol specification was made publicly available, allowing manufacturers worldwide to implement Modbus compatibility in their devices without licensing fees.

## Modbus Architecture: Master-Slave and Client-Server Models

Modbus operates on a master-slave (also called client-server in modern terminology) communication model:

- **Master/Client**: The device that initiates requests and controls the communication flow
- **Slave/Server**: Devices that respond to master requests and provide data or execute commands

In traditional Modbus RTU/ASCII networks, only the master initiates communication. Slaves wait for queries and respond accordingly. In Modbus TCP, the terminology shifted to client-server to align with standard networking conventions, though the fundamental request-response pattern remains the same.

## Modbus Protocol Versions

### 1. Modbus RTU (Remote Terminal Unit)

Modbus RTU is the most common implementation for serial communications. It uses binary encoding for compact data representation and efficient transmission.

**Key Characteristics:**
- Binary encoding of data
- Uses Cyclical Redundancy Check (CRC) for error detection
- More efficient than ASCII variant
- Typically runs on RS-485 or RS-232 physical layers
- Frame format: [Device Address][Function Code][Data][CRC]
- Silent interval (3.5 character times) marks message boundaries

**Frame Structure:**
```
| Address | Function | Data      | CRC-16 |
| 1 byte  | 1 byte   | N bytes   | 2 bytes|
```

### 2. Modbus ASCII

Modbus ASCII uses ASCII characters for data representation, making it human-readable but less efficient than RTU.

**Key Characteristics:**
- ASCII encoding (each byte represented by two ASCII characters)
- Uses Longitudinal Redundancy Check (LRC) for error detection
- Easier to debug with terminal programs
- Less efficient (roughly twice the message length of RTU)
- Frame format begins with ':' and ends with CR-LF
- Less commonly used in modern implementations

**Frame Structure:**
```
| Start | Address | Function | Data      | LRC    | End    |
| ':'   | 2 chars | 2 chars  | N chars   | 2 chars| CR-LF  |
```

### 3. Modbus TCP/IP

Modbus TCP encapsulates the Modbus protocol within TCP/IP packets for Ethernet networks.

**Key Characteristics:**
- Uses TCP port 502 by default
- No CRC needed (TCP handles error checking)
- Adds MBAP (Modbus Application Protocol) header
- Supports multiple simultaneous connections
- Scalable to large networks
- Frame format: [MBAP Header][Function Code][Data]

**MBAP Header Structure:**
```
| Transaction ID | Protocol ID | Length | Unit ID |
| 2 bytes       | 2 bytes     | 2 bytes| 1 byte  |
```

## Data Model and Function Codes

Modbus organizes data into four primary tables:

1. **Discrete Inputs**: Single-bit read-only values (sensor states)
2. **Coils**: Single-bit read-write values (relay outputs)
3. **Input Registers**: 16-bit read-only values (analog inputs)
4. **Holding Registers**: 16-bit read-write values (configuration, setpoints)

Common function codes include:
- **0x01**: Read Coils
- **0x02**: Read Discrete Inputs
- **0x03**: Read Holding Registers
- **0x04**: Read Input Registers
- **0x05**: Write Single Coil
- **0x06**: Write Single Register
- **0x0F**: Write Multiple Coils
- **0x10**: Write Multiple Registers

## Code Examples

### C/C++ Example: Modbus RTU CRC Calculation

```c
#include <stdint.h>
#include <stdio.h>

// Calculate Modbus RTU CRC-16
uint16_t modbus_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i];
        
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

// Build a Modbus RTU request to read holding registers
void build_read_holding_registers(uint8_t *frame, uint8_t slave_id, 
                                   uint16_t start_addr, uint16_t quantity) {
    frame[0] = slave_id;
    frame[1] = 0x03;  // Function code: Read Holding Registers
    frame[2] = (start_addr >> 8) & 0xFF;  // Start address high byte
    frame[3] = start_addr & 0xFF;          // Start address low byte
    frame[4] = (quantity >> 8) & 0xFF;     // Quantity high byte
    frame[5] = quantity & 0xFF;            // Quantity low byte
    
    // Calculate and append CRC
    uint16_t crc = modbus_crc16(frame, 6);
    frame[6] = crc & 0xFF;         // CRC low byte
    frame[7] = (crc >> 8) & 0xFF;  // CRC high byte
}

int main() {
    uint8_t request[8];
    
    // Read 10 holding registers starting at address 100 from slave 1
    build_read_holding_registers(request, 1, 100, 10);
    
    printf("Modbus RTU Request: ");
    for (int i = 0; i < 8; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n");
    
    return 0;
}
```

### C++ Example: Modbus TCP Client

```cpp
#include <iostream>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

class ModbusTCPClient {
private:
    int sock;
    uint16_t transaction_id;
    
public:
    ModbusTCPClient() : sock(-1), transaction_id(0) {}
    
    bool connect(const char* ip, uint16_t port = 502) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        
        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        inet_pton(AF_INET, ip, &server.sin_addr);
        
        return ::connect(sock, (struct sockaddr*)&server, sizeof(server)) >= 0;
    }
    
    std::vector<uint16_t> read_holding_registers(uint8_t unit_id, 
                                                   uint16_t start_addr, 
                                                   uint16_t quantity) {
        std::vector<uint8_t> request(12);
        
        // MBAP Header
        request[0] = (transaction_id >> 8) & 0xFF;
        request[1] = transaction_id & 0xFF;
        transaction_id++;
        request[2] = 0x00;  // Protocol ID
        request[3] = 0x00;
        request[4] = 0x00;  // Length (6 bytes following)
        request[5] = 0x06;
        request[6] = unit_id;
        
        // PDU (Protocol Data Unit)
        request[7] = 0x03;  // Function code
        request[8] = (start_addr >> 8) & 0xFF;
        request[9] = start_addr & 0xFF;
        request[10] = (quantity >> 8) & 0xFF;
        request[11] = quantity & 0xFF;
        
        send(sock, request.data(), request.size(), 0);
        
        std::vector<uint8_t> response(9 + quantity * 2);
        recv(sock, response.data(), response.size(), 0);
        
        std::vector<uint16_t> registers;
        for (int i = 0; i < quantity; i++) {
            uint16_t value = (response[9 + i*2] << 8) | response[10 + i*2];
            registers.push_back(value);
        }
        
        return registers;
    }
    
    ~ModbusTCPClient() {
        if (sock >= 0) close(sock);
    }
};

int main() {
    ModbusTCPClient client;
    
    if (client.connect("192.168.1.100")) {
        auto registers = client.read_holding_registers(1, 0, 5);
        
        std::cout << "Read " << registers.size() << " registers:" << std::endl;
        for (size_t i = 0; i < registers.size(); i++) {
            std::cout << "Register " << i << ": " << registers[i] << std::endl;
        }
    }
    
    return 0;
}
```

### Rust Example: Modbus Frame Parser

```rust
use std::io::{self, Write};

#[derive(Debug)]
pub enum ModbusError {
    InvalidCrc,
    InvalidLength,
    InvalidFunctionCode,
}

#[derive(Debug)]
pub struct ModbusRtuFrame {
    pub slave_id: u8,
    pub function_code: u8,
    pub data: Vec<u8>,
}

impl ModbusRtuFrame {
    // Calculate Modbus RTU CRC-16
    pub fn calculate_crc(data: &[u8]) -> u16 {
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
    
    // Parse a Modbus RTU frame
    pub fn parse(raw_frame: &[u8]) -> Result<Self, ModbusError> {
        if raw_frame.len() < 4 {
            return Err(ModbusError::InvalidLength);
        }
        
        let data_len = raw_frame.len() - 2;
        let received_crc = u16::from_le_bytes([
            raw_frame[data_len],
            raw_frame[data_len + 1],
        ]);
        
        let calculated_crc = Self::calculate_crc(&raw_frame[..data_len]);
        
        if received_crc != calculated_crc {
            return Err(ModbusError::InvalidCrc);
        }
        
        Ok(ModbusRtuFrame {
            slave_id: raw_frame[0],
            function_code: raw_frame[1],
            data: raw_frame[2..data_len].to_vec(),
        })
    }
    
    // Build a read holding registers request
    pub fn read_holding_registers(
        slave_id: u8,
        start_addr: u16,
        quantity: u16,
    ) -> Vec<u8> {
        let mut frame = Vec::with_capacity(8);
        
        frame.push(slave_id);
        frame.push(0x03); // Function code
        frame.extend_from_slice(&start_addr.to_be_bytes());
        frame.extend_from_slice(&quantity.to_be_bytes());
        
        let crc = Self::calculate_crc(&frame);
        frame.extend_from_slice(&crc.to_le_bytes());
        
        frame
    }
}

fn main() -> io::Result<()> {
    // Create a request to read 10 holding registers from address 100
    let request = ModbusRtuFrame::read_holding_registers(1, 100, 10);
    
    print!("Modbus RTU Request: ");
    for byte in &request {
        print!("{:02X} ", byte);
    }
    println!();
    
    // Simulate parsing a response
    let response = vec![
        0x01, 0x03, 0x14, // Slave ID, Function, Byte count
        0x00, 0x64, 0x00, 0x65, 0x00, 0x66, 0x00, 0x67, 0x00, 0x68,
        0x00, 0x69, 0x00, 0x6A, 0x00, 0x6B, 0x00, 0x6C, 0x00, 0x6D,
        0xB8, 0x44, // CRC (example)
    ];
    
    match ModbusRtuFrame::parse(&response) {
        Ok(frame) => {
            println!("\nParsed frame:");
            println!("  Slave ID: {}", frame.slave_id);
            println!("  Function: 0x{:02X}", frame.function_code);
            println!("  Data length: {} bytes", frame.data.len());
        }
        Err(e) => println!("Parse error: {:?}", e),
    }
    
    Ok(())
}
```

### Rust Example: Async Modbus TCP Client

```rust
use tokio::net::TcpStream;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use std::io;

pub struct ModbusTcpClient {
    stream: TcpStream,
    transaction_id: u16,
}

impl ModbusTcpClient {
    pub async fn connect(addr: &str) -> io::Result<Self> {
        let stream = TcpStream::connect(addr).await?;
        Ok(ModbusTcpClient {
            stream,
            transaction_id: 0,
        })
    }
    
    pub async fn read_holding_registers(
        &mut self,
        unit_id: u8,
        start_addr: u16,
        quantity: u16,
    ) -> io::Result<Vec<u16>> {
        let mut request = Vec::with_capacity(12);
        
        // MBAP Header
        request.extend_from_slice(&self.transaction_id.to_be_bytes());
        self.transaction_id = self.transaction_id.wrapping_add(1);
        request.extend_from_slice(&0u16.to_be_bytes()); // Protocol ID
        request.extend_from_slice(&6u16.to_be_bytes()); // Length
        request.push(unit_id);
        
        // PDU
        request.push(0x03); // Function code
        request.extend_from_slice(&start_addr.to_be_bytes());
        request.extend_from_slice(&quantity.to_be_bytes());
        
        self.stream.write_all(&request).await?;
        
        let mut response = vec![0u8; 9 + (quantity as usize * 2)];
        self.stream.read_exact(&mut response).await?;
        
        let mut registers = Vec::new();
        for i in 0..quantity as usize {
            let value = u16::from_be_bytes([
                response[9 + i * 2],
                response[10 + i * 2],
            ]);
            registers.push(value);
        }
        
        Ok(registers)
    }
}

#[tokio::main]
async fn main() -> io::Result<()> {
    let mut client = ModbusTcpClient::connect("127.0.0.1:502").await?;
    
    match client.read_holding_registers(1, 0, 5).await {
        Ok(registers) => {
            println!("Read {} registers:", registers.len());
            for (i, value) in registers.iter().enumerate() {
                println!("  Register {}: {}", i, value);
            }
        }
        Err(e) => println!("Error: {}", e),
    }
    
    Ok(())
}
```

## Summary

Modbus remains a cornerstone protocol in industrial automation due to its simplicity, openness, and versatility. The three main variants—RTU, ASCII, and TCP—serve different use cases while maintaining the same fundamental request-response communication model. RTU offers efficiency for serial communications, ASCII provides readability for debugging, and TCP enables modern networked industrial systems. Understanding the protocol's architecture, frame structures, and implementation patterns is essential for developing robust industrial communication systems. The provided code examples in C/C++ and Rust demonstrate practical implementations of CRC calculation, frame construction, and client communication across different Modbus variants, offering a foundation for building industrial automation applications.