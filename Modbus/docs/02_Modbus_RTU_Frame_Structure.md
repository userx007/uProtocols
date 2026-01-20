# Modbus RTU Frame Structure

## Overview

Modbus RTU (Remote Terminal Unit) is a serial communication protocol that uses a compact binary representation for data transmission. Unlike Modbus ASCII, RTU mode transmits data in 8-bit binary format, making it more efficient in terms of bandwidth and transmission speed. The RTU frame structure is fundamental to understanding how Modbus devices communicate over serial lines (RS-232, RS-485, etc.).

## Frame Structure Components

A Modbus RTU frame consists of four essential components transmitted in a specific sequence:

1. **Slave Address (1 byte)**: Identifies the target device
2. **Function Code (1 byte)**: Specifies the operation to perform
3. **Data (N bytes)**: Contains request/response specific information
4. **CRC-16 (2 bytes)**: Error checking mechanism

### Frame Timing Requirements

In RTU mode, frames are separated by silent intervals of at least 3.5 character times. This silence marks the beginning and end of a frame. At 9600 baud with 11 bits per character (1 start, 8 data, 1 parity, 1 stop), this equates to approximately 4ms of silence.

## Detailed Component Analysis

### 1. Slave Address (Device ID)

- **Size**: 1 byte (8 bits)
- **Range**: 1-247 for individual devices, 0 for broadcast
- **Purpose**: Identifies which device should respond to the request

Valid slave addresses range from 1 to 247. Address 0 is reserved for broadcast messages (no response expected). Addresses 248-255 are reserved for future use.

### 2. Function Code

- **Size**: 1 byte
- **Range**: 1-127 for requests, 128-255 for error responses
- **Purpose**: Defines the action the server should perform

Common function codes include:
- **0x01**: Read Coils (discrete outputs)
- **0x02**: Read Discrete Inputs
- **0x03**: Read Holding Registers
- **0x04**: Read Input Registers
- **0x05**: Write Single Coil
- **0x06**: Write Single Register
- **0x0F**: Write Multiple Coils
- **0x10**: Write Multiple Registers

When an error occurs, the server responds with the function code + 128 (sets the MSB), followed by an exception code.

### 3. Data Field

The data field structure varies depending on the function code. It contains information such as register addresses, quantities, and values.

**Example for Read Holding Registers (0x03)**:
- Starting Address (2 bytes): First register to read
- Quantity (2 bytes): Number of registers to read

**Example Response**:
- Byte Count (1 byte): Number of data bytes following
- Register Values (N bytes): Actual register data

### 4. CRC-16 Error Checking

Modbus RTU uses CRC-16-IBM (also known as CRC-16-ANSI) for error detection. The CRC is calculated over the entire frame (address + function code + data) and appended as two bytes in **little-endian** format (low byte first, then high byte).

**CRC-16 Algorithm Characteristics**:
- Polynomial: 0xA001 (reversed 0x8005)
- Initial value: 0xFFFF
- Final XOR: None
- Reflects input and output

## Frame Examples

### Example 1: Read 10 Holding Registers Starting at Address 100

**Request Frame**:
```
[0x01] [0x03] [0x00] [0x64] [0x00] [0x0A] [CRC_Low] [CRC_High]
```
- Slave Address: 0x01 (Device 1)
- Function Code: 0x03 (Read Holding Registers)
- Starting Address: 0x0064 (100 decimal)
- Quantity: 0x000A (10 registers)
- CRC: Calculated over all preceding bytes

**Response Frame**:
```
[0x01] [0x03] [0x14] [Data...20 bytes] [CRC_Low] [CRC_High]
```
- Slave Address: 0x01
- Function Code: 0x03
- Byte Count: 0x14 (20 bytes = 10 registers × 2 bytes)
- Register Values: 20 bytes of data
- CRC: Calculated over all preceding bytes

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>

// CRC-16 Modbus calculation
uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

// Modbus RTU frame structure
typedef struct {
    uint8_t slave_address;
    uint8_t function_code;
    uint8_t data[252];  // Maximum data size
    size_t data_length;
    uint16_t crc;
} modbus_rtu_frame_t;

// Build a Read Holding Registers request
int build_read_holding_registers(modbus_rtu_frame_t *frame,
                                  uint8_t slave_addr,
                                  uint16_t start_addr,
                                  uint16_t quantity) {
    if (quantity < 1 || quantity > 125) {
        return -1;  // Invalid quantity
    }
    
    frame->slave_address = slave_addr;
    frame->function_code = 0x03;
    
    // Pack data (big-endian for Modbus)
    frame->data[0] = (start_addr >> 8) & 0xFF;
    frame->data[1] = start_addr & 0xFF;
    frame->data[2] = (quantity >> 8) & 0xFF;
    frame->data[3] = quantity & 0xFF;
    frame->data_length = 4;
    
    // Calculate CRC
    uint8_t temp_buffer[256];
    temp_buffer[0] = frame->slave_address;
    temp_buffer[1] = frame->function_code;
    memcpy(&temp_buffer[2], frame->data, frame->data_length);
    
    frame->crc = calculate_crc16(temp_buffer, 2 + frame->data_length);
    
    return 0;
}

// Serialize frame to byte array
size_t serialize_frame(const modbus_rtu_frame_t *frame, uint8_t *buffer) {
    size_t index = 0;
    
    buffer[index++] = frame->slave_address;
    buffer[index++] = frame->function_code;
    memcpy(&buffer[index], frame->data, frame->data_length);
    index += frame->data_length;
    
    // CRC in little-endian (low byte first)
    buffer[index++] = frame->crc & 0xFF;
    buffer[index++] = (frame->crc >> 8) & 0xFF;
    
    return index;
}

// Verify CRC of received frame
int verify_frame_crc(const uint8_t *buffer, size_t length) {
    if (length < 4) {
        return -1;  // Frame too short
    }
    
    // Extract received CRC (little-endian)
    uint16_t received_crc = buffer[length - 2] | (buffer[length - 1] << 8);
    
    // Calculate CRC over frame (excluding CRC bytes)
    uint16_t calculated_crc = calculate_crc16(buffer, length - 2);
    
    return (received_crc == calculated_crc) ? 0 : -1;
}

// Example usage
#include <stdio.h>

int main() {
    modbus_rtu_frame_t frame;
    uint8_t buffer[256];
    
    // Build request to read 10 registers from address 100 on slave 1
    if (build_read_holding_registers(&frame, 1, 100, 10) == 0) {
        size_t frame_size = serialize_frame(&frame, buffer);
        
        printf("Modbus RTU Frame (%zu bytes):\n", frame_size);
        for (size_t i = 0; i < frame_size; i++) {
            printf("%02X ", buffer[i]);
        }
        printf("\n");
        
        // Verify CRC
        if (verify_frame_crc(buffer, frame_size) == 0) {
            printf("CRC verification: PASSED\n");
        } else {
            printf("CRC verification: FAILED\n");
        }
    }
    
    return 0;
}
```

### C++ Object-Oriented Implementation

```cpp
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <iostream>
#include <iomanip>

class ModbusRTU {
public:
    enum FunctionCode {
        READ_COILS = 0x01,
        READ_DISCRETE_INPUTS = 0x02,
        READ_HOLDING_REGISTERS = 0x03,
        READ_INPUT_REGISTERS = 0x04,
        WRITE_SINGLE_COIL = 0x05,
        WRITE_SINGLE_REGISTER = 0x06,
        WRITE_MULTIPLE_COILS = 0x0F,
        WRITE_MULTIPLE_REGISTERS = 0x10
    };
    
    class Frame {
    private:
        uint8_t slave_address_;
        uint8_t function_code_;
        std::vector<uint8_t> data_;
        uint16_t crc_;
        
        static uint16_t calculateCRC(const std::vector<uint8_t>& buffer) {
            uint16_t crc = 0xFFFF;
            
            for (uint8_t byte : buffer) {
                crc ^= byte;
                
                for (int i = 0; i < 8; i++) {
                    if (crc & 0x0001) {
                        crc >>= 1;
                        crc ^= 0xA001;
                    } else {
                        crc >>= 1;
                    }
                }
            }
            
            return crc;
        }
        
    public:
        Frame(uint8_t slave, uint8_t function, const std::vector<uint8_t>& data)
            : slave_address_(slave), function_code_(function), data_(data) {
            updateCRC();
        }
        
        void updateCRC() {
            std::vector<uint8_t> temp;
            temp.push_back(slave_address_);
            temp.push_back(function_code_);
            temp.insert(temp.end(), data_.begin(), data_.end());
            crc_ = calculateCRC(temp);
        }
        
        std::vector<uint8_t> serialize() const {
            std::vector<uint8_t> buffer;
            buffer.push_back(slave_address_);
            buffer.push_back(function_code_);
            buffer.insert(buffer.end(), data_.begin(), data_.end());
            buffer.push_back(crc_ & 0xFF);        // Low byte
            buffer.push_back((crc_ >> 8) & 0xFF); // High byte
            return buffer;
        }
        
        static bool verifyCRC(const std::vector<uint8_t>& frame) {
            if (frame.size() < 4) return false;
            
            uint16_t received = frame[frame.size() - 2] | 
                               (frame[frame.size() - 1] << 8);
            
            std::vector<uint8_t> data(frame.begin(), frame.end() - 2);
            uint16_t calculated = calculateCRC(data);
            
            return received == calculated;
        }
        
        uint8_t getSlaveAddress() const { return slave_address_; }
        uint8_t getFunctionCode() const { return function_code_; }
        const std::vector<uint8_t>& getData() const { return data_; }
    };
    
    // Factory method for Read Holding Registers request
    static Frame createReadHoldingRegisters(uint8_t slave, 
                                           uint16_t start_address,
                                           uint16_t quantity) {
        if (quantity < 1 || quantity > 125) {
            throw std::invalid_argument("Quantity must be 1-125");
        }
        
        std::vector<uint8_t> data;
        data.push_back((start_address >> 8) & 0xFF);
        data.push_back(start_address & 0xFF);
        data.push_back((quantity >> 8) & 0xFF);
        data.push_back(quantity & 0xFF);
        
        return Frame(slave, READ_HOLDING_REGISTERS, data);
    }
    
    // Factory method for Write Single Register request
    static Frame createWriteSingleRegister(uint8_t slave,
                                          uint16_t address,
                                          uint16_t value) {
        std::vector<uint8_t> data;
        data.push_back((address >> 8) & 0xFF);
        data.push_back(address & 0xFF);
        data.push_back((value >> 8) & 0xFF);
        data.push_back(value & 0xFF);
        
        return Frame(slave, WRITE_SINGLE_REGISTER, data);
    }
};

// Example usage
int main() {
    try {
        // Create a request to read 10 registers starting at address 100
        auto frame = ModbusRTU::createReadHoldingRegisters(1, 100, 10);
        auto buffer = frame.serialize();
        
        std::cout << "Modbus RTU Frame (" << buffer.size() << " bytes):\n";
        for (uint8_t byte : buffer) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                     << static_cast<int>(byte) << " ";
        }
        std::cout << "\n";
        
        // Verify CRC
        if (ModbusRTU::Frame::verifyCRC(buffer)) {
            std::cout << "CRC verification: PASSED\n";
        } else {
            std::cout << "CRC verification: FAILED\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
```

### Rust Implementation

```rust
use std::fmt;

/// CRC-16 Modbus calculation
fn calculate_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for &byte in data {
        crc ^= byte as u16;
        
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    crc
}

/// Modbus function codes
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum FunctionCode {
    ReadCoils = 0x01,
    ReadDiscreteInputs = 0x02,
    ReadHoldingRegisters = 0x03,
    ReadInputRegisters = 0x04,
    WriteSingleCoil = 0x05,
    WriteSingleRegister = 0x06,
    WriteMultipleCoils = 0x0F,
    WriteMultipleRegisters = 0x10,
}

impl From<FunctionCode> for u8 {
    fn from(fc: FunctionCode) -> u8 {
        fc as u8
    }
}

/// Modbus RTU Frame structure
#[derive(Debug, Clone)]
pub struct ModbusRTUFrame {
    slave_address: u8,
    function_code: u8,
    data: Vec<u8>,
    crc: u16,
}

impl ModbusRTUFrame {
    /// Create a new frame with automatic CRC calculation
    pub fn new(slave_address: u8, function_code: u8, data: Vec<u8>) -> Self {
        let mut frame = ModbusRTUFrame {
            slave_address,
            function_code,
            data,
            crc: 0,
        };
        frame.update_crc();
        frame
    }
    
    /// Update CRC based on current frame contents
    fn update_crc(&mut self) {
        let mut temp = Vec::new();
        temp.push(self.slave_address);
        temp.push(self.function_code);
        temp.extend_from_slice(&self.data);
        
        self.crc = calculate_crc16(&temp);
    }
    
    /// Serialize frame to byte vector
    pub fn serialize(&self) -> Vec<u8> {
        let mut buffer = Vec::new();
        buffer.push(self.slave_address);
        buffer.push(self.function_code);
        buffer.extend_from_slice(&self.data);
        buffer.push((self.crc & 0xFF) as u8);        // Low byte
        buffer.push(((self.crc >> 8) & 0xFF) as u8); // High byte
        buffer
    }
    
    /// Verify CRC of a received frame
    pub fn verify_crc(frame: &[u8]) -> bool {
        if frame.len() < 4 {
            return false;
        }
        
        let len = frame.len();
        let received_crc = frame[len - 2] as u16 | ((frame[len - 1] as u16) << 8);
        let calculated_crc = calculate_crc16(&frame[..len - 2]);
        
        received_crc == calculated_crc
    }
    
    /// Parse a frame from bytes
    pub fn parse(bytes: &[u8]) -> Result<Self, &'static str> {
        if bytes.len() < 4 {
            return Err("Frame too short");
        }
        
        if !Self::verify_crc(bytes) {
            return Err("CRC verification failed");
        }
        
        let slave_address = bytes[0];
        let function_code = bytes[1];
        let data = bytes[2..bytes.len() - 2].to_vec();
        let crc = bytes[bytes.len() - 2] as u16 | 
                 ((bytes[bytes.len() - 1] as u16) << 8);
        
        Ok(ModbusRTUFrame {
            slave_address,
            function_code,
            data,
            crc,
        })
    }
    
    pub fn slave_address(&self) -> u8 { self.slave_address }
    pub fn function_code(&self) -> u8 { self.function_code }
    pub fn data(&self) -> &[u8] { &self.data }
}

impl fmt::Display for ModbusRTUFrame {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Modbus RTU Frame: Slave={:#04X}, Function={:#04X}, Data=[",
               self.slave_address, self.function_code)?;
        for (i, byte) in self.data.iter().enumerate() {
            if i > 0 { write!(f, " ")?; }
            write!(f, "{:02X}", byte)?;
        }
        write!(f, "], CRC={:#06X}", self.crc)
    }
}

/// Builder for Read Holding Registers request
pub fn read_holding_registers(
    slave: u8,
    start_address: u16,
    quantity: u16
) -> Result<ModbusRTUFrame, &'static str> {
    if quantity < 1 || quantity > 125 {
        return Err("Quantity must be between 1 and 125");
    }
    
    let mut data = Vec::new();
    data.push((start_address >> 8) as u8);
    data.push((start_address & 0xFF) as u8);
    data.push((quantity >> 8) as u8);
    data.push((quantity & 0xFF) as u8);
    
    Ok(ModbusRTUFrame::new(slave, FunctionCode::ReadHoldingRegisters.into(), data))
}

/// Builder for Write Single Register request
pub fn write_single_register(
    slave: u8,
    address: u16,
    value: u16
) -> ModbusRTUFrame {
    let mut data = Vec::new();
    data.push((address >> 8) as u8);
    data.push((address & 0xFF) as u8);
    data.push((value >> 8) as u8);
    data.push((value & 0xFF) as u8);
    
    ModbusRTUFrame::new(slave, FunctionCode::WriteSingleRegister.into(), data)
}

// Example usage
fn main() {
    // Create a Read Holding Registers request
    match read_holding_registers(1, 100, 10) {
        Ok(frame) => {
            println!("{}", frame);
            
            let buffer = frame.serialize();
            print!("Serialized ({} bytes): ", buffer.len());
            for byte in &buffer {
                print!("{:02X} ", byte);
            }
            println!();
            
            // Verify CRC
            if ModbusRTUFrame::verify_crc(&buffer) {
                println!("CRC verification: PASSED");
            } else {
                println!("CRC verification: FAILED");
            }
            
            // Parse it back
            match ModbusRTUFrame::parse(&buffer) {
                Ok(parsed) => println!("Parsed successfully: {}", parsed),
                Err(e) => println!("Parse error: {}", e),
            }
        }
        Err(e) => println!("Error creating frame: {}", e),
    }
    
    // Create a Write Single Register request
    let write_frame = write_single_register(1, 200, 1234);
    println!("\n{}", write_frame);
    
    let write_buffer = write_frame.serialize();
    print!("Serialized ({} bytes): ", write_buffer.len());
    for byte in &write_buffer {
        print!("{:02X} ", byte);
    }
    println!();
}
```

## Summary

The Modbus RTU frame structure is the foundation of Modbus serial communication. Each frame consists of a slave address identifying the target device, a function code specifying the operation, a variable-length data field containing operation-specific information, and a CRC-16 checksum for error detection. The protocol requires precise timing with 3.5 character time gaps between frames.

The CRC-16 algorithm ensures data integrity by using polynomial 0xA001 with an initial value of 0xFFFF, transmitted in little-endian format. Understanding frame construction and CRC calculation is essential for implementing reliable Modbus communication. The code examples demonstrate how to build, serialize, and verify Modbus RTU frames in C, C++, and Rust, providing practical implementations for creating requests like reading holding registers and writing single registers. These implementations handle byte ordering correctly (big-endian for addresses and values, little-endian for CRC) and include proper error checking mechanisms.