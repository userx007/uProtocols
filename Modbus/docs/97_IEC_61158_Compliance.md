# IEC 61158 Compliance - Modbus as Part of the Fieldbus Standard Family

## Detailed Description

IEC 61158 is an international standard for digital communication in industrial automation environments, specifically for fieldbus systems. This comprehensive standard defines multiple fieldbus types, and Modbus TCP/IP is recognized as one of the compliant protocols within this family.

### Understanding IEC 61158

IEC 61158 is actually a series of standards organized into multiple parts:

- **IEC 61158-1 through IEC 61158-6**: Cover different aspects including physical layer, data link layer, application layer, and network management
- **Type Classifications**: The standard recognizes multiple fieldbus "types" (originally 8, now expanded to over 20), each representing different industrial communication protocols

### Modbus and IEC 61158

Modbus TCP/IP is classified as **Type 18** in the IEC 61158 standard family. This recognition validates Modbus as a standardized industrial fieldbus protocol suitable for industrial automation applications.

**Key aspects of compliance:**

1. **Protocol Stack Alignment**: Modbus TCP follows the layered architecture defined in IEC 61158
2. **Interoperability**: Compliance ensures devices from different manufacturers can communicate
3. **Industrial Suitability**: Validates the protocol for use in industrial control systems
4. **Safety and Reliability**: Meets industrial-grade communication requirements

### Compliance Requirements

For a Modbus implementation to be IEC 61158 Type 18 compliant, it must adhere to:

- **Physical Layer**: Typically Ethernet (IEEE 802.3)
- **Transport Layer**: TCP/IP
- **Application Layer**: Modbus Application Protocol (MBAP)
- **Port**: Standard TCP port 502
- **Data Encoding**: Big-endian (network byte order)
- **Function Code Support**: Minimum required function codes (0x01-0x06, 0x0F, 0x10)

## Programming Examples

### C/C++ Implementation

Here's a compliant Modbus TCP implementation in C++:

```cpp
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

// IEC 61158 Type 18 (Modbus TCP) compliant implementation

// MBAP Header structure per IEC 61158
struct ModbusTCPHeader {
    uint16_t transaction_id;  // Transaction identifier
    uint16_t protocol_id;     // Protocol identifier (0x0000 for Modbus)
    uint16_t length;          // Length of following bytes
    uint8_t  unit_id;         // Unit identifier (slave address)
};

class ModbusTCPClient {
private:
    int sock;
    uint16_t transaction_id;
    
    // Convert to network byte order (big-endian) per IEC 61158
    void encodeHeader(uint8_t* buffer, uint8_t unit_id, uint16_t length) {
        ModbusTCPHeader header;
        header.transaction_id = htons(transaction_id++);
        header.protocol_id = htons(0x0000);
        header.length = htons(length);
        header.unit_id = unit_id;
        
        memcpy(buffer, &header.transaction_id, 2);
        memcpy(buffer + 2, &header.protocol_id, 2);
        memcpy(buffer + 4, &header.length, 2);
        buffer[6] = header.unit_id;
    }
    
public:
    ModbusTCPClient() : sock(-1), transaction_id(0) {}
    
    // Connect to Modbus server on standard port 502 (IEC 61158 compliant)
    bool connect(const char* ip_address, int port = 502) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << "Socket creation failed\n";
            return false;
        }
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address\n";
            return false;
        }
        
        if (::connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed\n";
            return false;
        }
        
        return true;
    }
    
    // Read Holding Registers (Function Code 0x03) - IEC 61158 required function
    bool readHoldingRegisters(uint8_t unit_id, uint16_t start_addr, 
                             uint16_t quantity, uint16_t* values) {
        if (quantity < 1 || quantity > 125) {
            std::cerr << "Invalid quantity (must be 1-125)\n";
            return false;
        }
        
        uint8_t request[12];
        
        // MBAP Header (7 bytes)
        encodeHeader(request, unit_id, 6);  // 6 bytes follow header
        
        // PDU (Protocol Data Unit)
        request[7] = 0x03;  // Function code: Read Holding Registers
        request[8] = (start_addr >> 8) & 0xFF;  // Start address high byte
        request[9] = start_addr & 0xFF;          // Start address low byte
        request[10] = (quantity >> 8) & 0xFF;    // Quantity high byte
        request[11] = quantity & 0xFF;           // Quantity low byte
        
        // Send request
        if (send(sock, request, 12, 0) != 12) {
            std::cerr << "Send failed\n";
            return false;
        }
        
        // Receive response
        uint8_t response[260];  // Max response size
        int bytes_received = recv(sock, response, sizeof(response), 0);
        
        if (bytes_received < 9) {
            std::cerr << "Invalid response\n";
            return false;
        }
        
        // Verify MBAP header
        uint16_t resp_protocol = (response[2] << 8) | response[3];
        if (resp_protocol != 0x0000) {
            std::cerr << "Invalid protocol ID\n";
            return false;
        }
        
        // Check function code
        if (response[7] != 0x03) {
            if (response[7] == 0x83) {
                std::cerr << "Exception response: " << (int)response[8] << "\n";
            }
            return false;
        }
        
        uint8_t byte_count = response[8];
        if (byte_count != quantity * 2) {
            std::cerr << "Invalid byte count\n";
            return false;
        }
        
        // Extract register values (big-endian per IEC 61158)
        for (int i = 0; i < quantity; i++) {
            values[i] = (response[9 + i*2] << 8) | response[10 + i*2];
        }
        
        return true;
    }
    
    // Write Single Register (Function Code 0x06) - IEC 61158 required function
    bool writeSingleRegister(uint8_t unit_id, uint16_t addr, uint16_t value) {
        uint8_t request[12];
        
        // MBAP Header
        encodeHeader(request, unit_id, 6);
        
        // PDU
        request[7] = 0x06;   // Function code: Write Single Register
        request[8] = (addr >> 8) & 0xFF;
        request[9] = addr & 0xFF;
        request[10] = (value >> 8) & 0xFF;
        request[11] = value & 0xFF;
        
        if (send(sock, request, 12, 0) != 12) {
            return false;
        }
        
        uint8_t response[12];
        int bytes_received = recv(sock, response, sizeof(response), 0);
        
        return (bytes_received == 12 && response[7] == 0x06);
    }
    
    ~ModbusTCPClient() {
        if (sock >= 0) {
            close(sock);
        }
    }
};

// Example usage
int main() {
    ModbusTCPClient client;
    
    // Connect to Modbus server on standard port 502
    if (!client.connect("192.168.1.100")) {
        return 1;
    }
    
    std::cout << "Connected to IEC 61158 Type 18 compliant Modbus server\n";
    
    // Read 10 holding registers starting at address 0
    uint16_t registers[10];
    if (client.readHoldingRegisters(1, 0, 10, registers)) {
        std::cout << "Successfully read registers:\n";
        for (int i = 0; i < 10; i++) {
            std::cout << "Register " << i << ": " << registers[i] << "\n";
        }
    }
    
    // Write a single register
    if (client.writeSingleRegister(1, 0, 1234)) {
        std::cout << "Successfully wrote register\n";
    }
    
    return 0;
}
```

### Rust Implementation

Here's an IEC 61158 compliant Modbus implementation in Rust:

```rust
use std::io::{self, Read, Write};
use std::net::TcpStream;

// IEC 61158 Type 18 (Modbus TCP) compliant implementation

const MODBUS_TCP_PORT: u16 = 502;  // Standard port per IEC 61158
const PROTOCOL_ID: u16 = 0x0000;    // Modbus protocol identifier

#[derive(Debug)]
pub enum ModbusError {
    IoError(io::Error),
    InvalidResponse,
    ExceptionResponse(u8),
    InvalidQuantity,
}

impl From<io::Error> for ModbusError {
    fn from(error: io::Error) -> Self {
        ModbusError::IoError(error)
    }
}

/// MBAP Header structure per IEC 61158
#[derive(Debug, Clone)]
struct MBAPHeader {
    transaction_id: u16,
    protocol_id: u16,
    length: u16,
    unit_id: u8,
}

impl MBAPHeader {
    fn new(transaction_id: u16, unit_id: u8, pdu_length: u16) -> Self {
        MBAPHeader {
            transaction_id,
            protocol_id: PROTOCOL_ID,
            length: pdu_length + 1,  // PDU length + unit_id
            unit_id,
        }
    }
    
    /// Encode header to bytes (big-endian per IEC 61158)
    fn to_bytes(&self) -> [u8; 7] {
        let mut bytes = [0u8; 7];
        bytes[0..2].copy_from_slice(&self.transaction_id.to_be_bytes());
        bytes[2..4].copy_from_slice(&self.protocol_id.to_be_bytes());
        bytes[4..6].copy_from_slice(&self.length.to_be_bytes());
        bytes[6] = self.unit_id;
        bytes
    }
    
    /// Decode header from bytes
    fn from_bytes(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < 7 {
            return None;
        }
        
        Some(MBAPHeader {
            transaction_id: u16::from_be_bytes([bytes[0], bytes[1]]),
            protocol_id: u16::from_be_bytes([bytes[2], bytes[3]]),
            length: u16::from_be_bytes([bytes[4], bytes[5]]),
            unit_id: bytes[6],
        })
    }
}

pub struct ModbusTCPClient {
    stream: TcpStream,
    transaction_id: u16,
}

impl ModbusTCPClient {
    /// Connect to Modbus TCP server (IEC 61158 Type 18)
    pub fn connect(address: &str) -> Result<Self, ModbusError> {
        let full_address = if address.contains(':') {
            address.to_string()
        } else {
            format!("{}:{}", address, MODBUS_TCP_PORT)
        };
        
        let stream = TcpStream::connect(full_address)?;
        
        Ok(ModbusTCPClient {
            stream,
            transaction_id: 0,
        })
    }
    
    fn get_next_transaction_id(&mut self) -> u16 {
        let id = self.transaction_id;
        self.transaction_id = self.transaction_id.wrapping_add(1);
        id
    }
    
    /// Read Holding Registers (Function Code 0x03)
    /// IEC 61158 compliant implementation
    pub fn read_holding_registers(
        &mut self,
        unit_id: u8,
        start_address: u16,
        quantity: u16,
    ) -> Result<Vec<u16>, ModbusError> {
        // Validate quantity per Modbus specification
        if quantity < 1 || quantity > 125 {
            return Err(ModbusError::InvalidQuantity);
        }
        
        let transaction_id = self.get_next_transaction_id();
        let header = MBAPHeader::new(transaction_id, unit_id, 5);
        
        // Build request
        let mut request = Vec::new();
        request.extend_from_slice(&header.to_bytes());
        request.push(0x03);  // Function code
        request.extend_from_slice(&start_address.to_be_bytes());
        request.extend_from_slice(&quantity.to_be_bytes());
        
        // Send request
        self.stream.write_all(&request)?;
        
        // Read response header
        let mut response_header = [0u8; 7];
        self.stream.read_exact(&mut response_header)?;
        
        let resp_header = MBAPHeader::from_bytes(&response_header)
            .ok_or(ModbusError::InvalidResponse)?;
        
        // Verify protocol ID (IEC 61158 compliance)
        if resp_header.protocol_id != PROTOCOL_ID {
            return Err(ModbusError::InvalidResponse);
        }
        
        // Read PDU
        let pdu_length = (resp_header.length - 1) as usize;
        let mut pdu = vec![0u8; pdu_length];
        self.stream.read_exact(&mut pdu)?;
        
        // Check function code
        let function_code = pdu[0];
        if function_code == 0x83 {
            // Exception response
            return Err(ModbusError::ExceptionResponse(pdu[1]));
        }
        
        if function_code != 0x03 {
            return Err(ModbusError::InvalidResponse);
        }
        
        let byte_count = pdu[1] as usize;
        if byte_count != (quantity as usize * 2) {
            return Err(ModbusError::InvalidResponse);
        }
        
        // Extract register values (big-endian per IEC 61158)
        let mut registers = Vec::new();
        for i in 0..quantity as usize {
            let offset = 2 + i * 2;
            let value = u16::from_be_bytes([pdu[offset], pdu[offset + 1]]);
            registers.push(value);
        }
        
        Ok(registers)
    }
    
    /// Write Single Register (Function Code 0x06)
    pub fn write_single_register(
        &mut self,
        unit_id: u8,
        address: u16,
        value: u16,
    ) -> Result<(), ModbusError> {
        let transaction_id = self.get_next_transaction_id();
        let header = MBAPHeader::new(transaction_id, unit_id, 5);
        
        let mut request = Vec::new();
        request.extend_from_slice(&header.to_bytes());
        request.push(0x06);  // Function code
        request.extend_from_slice(&address.to_be_bytes());
        request.extend_from_slice(&value.to_be_bytes());
        
        self.stream.write_all(&request)?;
        
        // Read response
        let mut response = [0u8; 12];
        self.stream.read_exact(&mut response)?;
        
        let resp_header = MBAPHeader::from_bytes(&response[..7])
            .ok_or(ModbusError::InvalidResponse)?;
        
        if resp_header.protocol_id != PROTOCOL_ID {
            return Err(ModbusError::InvalidResponse);
        }
        
        if response[7] == 0x86 {
            return Err(ModbusError::ExceptionResponse(response[8]));
        }
        
        if response[7] != 0x06 {
            return Err(ModbusError::InvalidResponse);
        }
        
        Ok(())
    }
    
    /// Write Multiple Registers (Function Code 0x10)
    pub fn write_multiple_registers(
        &mut self,
        unit_id: u8,
        start_address: u16,
        values: &[u16],
    ) -> Result<(), ModbusError> {
        if values.is_empty() || values.len() > 123 {
            return Err(ModbusError::InvalidQuantity);
        }
        
        let quantity = values.len() as u16;
        let byte_count = (quantity * 2) as u8;
        let pdu_length = 6 + byte_count as u16;
        
        let transaction_id = self.get_next_transaction_id();
        let header = MBAPHeader::new(transaction_id, unit_id, pdu_length);
        
        let mut request = Vec::new();
        request.extend_from_slice(&header.to_bytes());
        request.push(0x10);  // Function code
        request.extend_from_slice(&start_address.to_be_bytes());
        request.extend_from_slice(&quantity.to_be_bytes());
        request.push(byte_count);
        
        // Add register values (big-endian)
        for value in values {
            request.extend_from_slice(&value.to_be_bytes());
        }
        
        self.stream.write_all(&request)?;
        
        // Read response
        let mut response = [0u8; 12];
        self.stream.read_exact(&mut response)?;
        
        if response[7] == 0x90 {
            return Err(ModbusError::ExceptionResponse(response[8]));
        }
        
        if response[7] != 0x10 {
            return Err(ModbusError::InvalidResponse);
        }
        
        Ok(())
    }
}

// Example usage
fn main() -> Result<(), ModbusError> {
    println!("IEC 61158 Type 18 (Modbus TCP) Client");
    
    // Connect to Modbus server
    let mut client = ModbusTCPClient::connect("192.168.1.100:502")?;
    println!("Connected to IEC 61158 compliant Modbus server");
    
    // Read holding registers
    match client.read_holding_registers(1, 0, 10) {
        Ok(registers) => {
            println!("Successfully read {} registers:", registers.len());
            for (i, value) in registers.iter().enumerate() {
                println!("  Register {}: {}", i, value);
            }
        }
        Err(e) => println!("Error reading registers: {:?}", e),
    }
    
    // Write single register
    match client.write_single_register(1, 0, 1234) {
        Ok(_) => println!("Successfully wrote register"),
        Err(e) => println!("Error writing register: {:?}", e),
    }
    
    // Write multiple registers
    let values = vec![100, 200, 300, 400, 500];
    match client.write_multiple_registers(1, 10, &values) {
        Ok(_) => println!("Successfully wrote {} registers", values.len()),
        Err(e) => println!("Error writing registers: {:?}", e),
    }
    
    Ok(())
}
```

## Summary

**IEC 61158 Compliance for Modbus** establishes Modbus TCP as a recognized industrial fieldbus protocol (Type 18) within the international standard for industrial automation communication. This compliance validates Modbus for use in safety-critical and industrial environments.

**Key Points:**

- **Standardization**: Modbus TCP is officially recognized as IEC 61158 Type 18
- **Interoperability**: Ensures devices from different manufacturers can communicate reliably
- **Requirements**: Compliance mandates specific protocol stack implementation, standard port 502, big-endian encoding, and support for core function codes
- **Industrial Grade**: Meets requirements for reliability, safety, and performance in industrial settings

**Implementation Essentials:**

1. Use TCP port 502 (standard)
2. Implement MBAP header with proper byte ordering (big-endian)
3. Support minimum required function codes (0x01-0x06, 0x0F, 0x10)
4. Follow protocol identifier conventions (0x0000 for Modbus)
5. Handle exceptions and errors according to specification

The code examples demonstrate compliant implementations that adhere to IEC 61158 requirements for network byte order, protocol structure, and standard function code support.