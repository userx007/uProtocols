# Function Code 0x16: Mask Write Register

## Detailed Description

Modbus Function Code 0x16 (22 decimal) is used to perform bitwise AND-OR masking operations on a single holding register. This function allows selective modification of specific bits within a register without affecting other bits, making it ideal for controlling individual flags or settings within a register.

### How It Works

The operation follows this sequence:
1. Read the current value of the target register
2. Apply AND mask to clear specific bits
3. Apply OR mask to set specific bits
4. Write the result back to the register

The mathematical formula is:
```
Result = (Current_Value AND And_Mask) OR (Or_Mask AND (NOT And_Mask))
```

### Request Structure

| Field | Length | Description |
|-------|--------|-------------|
| Function Code | 1 byte | 0x16 (22) |
| Reference Address | 2 bytes | Address of the target register |
| And_Mask | 2 bytes | Mask for clearing bits (AND operation) |
| Or_Mask | 2 bytes | Mask for setting bits (OR operation) |

**Total Request Length**: 7 bytes (excluding any transport protocol overhead)

### Response Structure

The response mirrors the request structure, confirming the operation:

| Field | Length | Description |
|-------|--------|-------------|
| Function Code | 1 byte | 0x16 (22) |
| Reference Address | 2 bytes | Address of the modified register |
| And_Mask | 2 bytes | Echo of the AND mask used |
| Or_Mask | 2 bytes | Echo of the OR mask used |

**Total Response Length**: 7 bytes

### Use Cases

- Toggling individual control bits without affecting others
- Setting multiple flags in a configuration register
- Clearing specific error flags while preserving others
- Implementing bit-level device control in PLC/SCADA systems

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MODBUS_FC_MASK_WRITE_REGISTER 0x16

// Structure for Mask Write Register request
typedef struct {
    uint8_t function_code;
    uint16_t reference_address;
    uint16_t and_mask;
    uint16_t or_mask;
} modbus_mask_write_req_t;

// Structure for Mask Write Register response
typedef struct {
    uint8_t function_code;
    uint16_t reference_address;
    uint16_t and_mask;
    uint16_t or_mask;
} modbus_mask_write_resp_t;

// Convert 16-bit value to big-endian (network byte order)
uint16_t htons_custom(uint16_t value) {
    return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
}

// Build Mask Write Register request
int build_mask_write_request(uint8_t *buffer, uint16_t address, 
                             uint16_t and_mask, uint16_t or_mask) {
    buffer[0] = MODBUS_FC_MASK_WRITE_REGISTER;
    buffer[1] = (address >> 8) & 0xFF;  // Address high byte
    buffer[2] = address & 0xFF;          // Address low byte
    buffer[3] = (and_mask >> 8) & 0xFF;  // AND mask high byte
    buffer[4] = and_mask & 0xFF;         // AND mask low byte
    buffer[5] = (or_mask >> 8) & 0xFF;   // OR mask high byte
    buffer[6] = or_mask & 0xFF;          // OR mask low byte
    
    return 7;  // Return PDU length
}

// Parse Mask Write Register response
int parse_mask_write_response(const uint8_t *buffer, int length,
                              modbus_mask_write_resp_t *response) {
    if (length < 7) {
        return -1;  // Invalid response length
    }
    
    if (buffer[0] != MODBUS_FC_MASK_WRITE_REGISTER) {
        return -2;  // Invalid function code
    }
    
    response->function_code = buffer[0];
    response->reference_address = (buffer[1] << 8) | buffer[2];
    response->and_mask = (buffer[3] << 8) | buffer[4];
    response->or_mask = (buffer[5] << 8) | buffer[6];
    
    return 0;  // Success
}

// Server-side: Process mask write register operation
int process_mask_write_register(uint16_t *registers, int num_registers,
                                uint16_t address, uint16_t and_mask,
                                uint16_t or_mask) {
    if (address >= num_registers) {
        return -1;  // Invalid address
    }
    
    // Perform AND-OR masking operation
    uint16_t current_value = registers[address];
    registers[address] = (current_value & and_mask) | (or_mask & ~and_mask);
    
    return 0;  // Success
}

// Example usage
int main() {
    uint8_t request[256];
    uint8_t response[256];
    uint16_t holding_registers[100] = {0};
    
    // Initialize register 10 with value 0x00F2
    holding_registers[10] = 0x00F2;
    
    printf("Initial register value: 0x%04X\n", holding_registers[10]);
    
    // Example: Set bit 2, clear bit 1
    uint16_t and_mask = 0xFFFD;  // Clear bit 1 (binary: 1111 1111 1111 1101)
    uint16_t or_mask = 0x0004;   // Set bit 2   (binary: 0000 0000 0000 0100)
    
    // Build request
    int req_len = build_mask_write_request(request, 10, and_mask, or_mask);
    
    printf("\nRequest PDU (hex): ");
    for (int i = 0; i < req_len; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n");
    
    // Process on server side
    process_mask_write_register(holding_registers, 100, 10, and_mask, or_mask);
    
    printf("Modified register value: 0x%04X\n", holding_registers[10]);
    
    // Build response (echo request)
    memcpy(response, request, req_len);
    
    // Parse response
    modbus_mask_write_resp_t resp;
    if (parse_mask_write_response(response, req_len, &resp) == 0) {
        printf("\nResponse parsed successfully:\n");
        printf("  Address: %u\n", resp.reference_address);
        printf("  AND Mask: 0x%04X\n", resp.and_mask);
        printf("  OR Mask: 0x%04X\n", resp.or_mask);
    }
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::error::Error;
use std::fmt;

const MODBUS_FC_MASK_WRITE_REGISTER: u8 = 0x16;

#[derive(Debug, Clone)]
pub struct ModbusError {
    message: String,
}

impl ModbusError {
    fn new(msg: &str) -> Self {
        ModbusError {
            message: msg.to_string(),
        }
    }
}

impl fmt::Display for ModbusError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "Modbus Error: {}", self.message)
    }
}

impl Error for ModbusError {}

#[derive(Debug, Clone)]
pub struct MaskWriteRequest {
    pub function_code: u8,
    pub reference_address: u16,
    pub and_mask: u16,
    pub or_mask: u16,
}

#[derive(Debug, Clone)]
pub struct MaskWriteResponse {
    pub function_code: u8,
    pub reference_address: u16,
    pub and_mask: u16,
    pub or_mask: u16,
}

impl MaskWriteRequest {
    /// Create a new Mask Write Register request
    pub fn new(address: u16, and_mask: u16, or_mask: u16) -> Self {
        MaskWriteRequest {
            function_code: MODBUS_FC_MASK_WRITE_REGISTER,
            reference_address: address,
            and_mask,
            or_mask,
        }
    }

    /// Serialize the request to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        vec![
            self.function_code,
            (self.reference_address >> 8) as u8,
            (self.reference_address & 0xFF) as u8,
            (self.and_mask >> 8) as u8,
            (self.and_mask & 0xFF) as u8,
            (self.or_mask >> 8) as u8,
            (self.or_mask & 0xFF) as u8,
        ]
    }

    /// Parse request from bytes
    pub fn from_bytes(data: &[u8]) -> Result<Self, ModbusError> {
        if data.len() < 7 {
            return Err(ModbusError::new("Request too short"));
        }

        if data[0] != MODBUS_FC_MASK_WRITE_REGISTER {
            return Err(ModbusError::new("Invalid function code"));
        }

        Ok(MaskWriteRequest {
            function_code: data[0],
            reference_address: u16::from_be_bytes([data[1], data[2]]),
            and_mask: u16::from_be_bytes([data[3], data[4]]),
            or_mask: u16::from_be_bytes([data[5], data[6]]),
        })
    }
}

impl MaskWriteResponse {
    /// Create a new Mask Write Register response
    pub fn new(address: u16, and_mask: u16, or_mask: u16) -> Self {
        MaskWriteResponse {
            function_code: MODBUS_FC_MASK_WRITE_REGISTER,
            reference_address: address,
            and_mask,
            or_mask,
        }
    }

    /// Serialize the response to bytes
    pub fn to_bytes(&self) -> Vec<u8> {
        vec![
            self.function_code,
            (self.reference_address >> 8) as u8,
            (self.reference_address & 0xFF) as u8,
            (self.and_mask >> 8) as u8,
            (self.and_mask & 0xFF) as u8,
            (self.or_mask >> 8) as u8,
            (self.or_mask & 0xFF) as u8,
        ]
    }

    /// Parse response from bytes
    pub fn from_bytes(data: &[u8]) -> Result<Self, ModbusError> {
        if data.len() < 7 {
            return Err(ModbusError::new("Response too short"));
        }

        if data[0] != MODBUS_FC_MASK_WRITE_REGISTER {
            return Err(ModbusError::new("Invalid function code"));
        }

        Ok(MaskWriteResponse {
            function_code: data[0],
            reference_address: u16::from_be_bytes([data[1], data[2]]),
            and_mask: u16::from_be_bytes([data[3], data[4]]),
            or_mask: u16::from_be_bytes([data[5], data[6]]),
        })
    }
}

/// Modbus server holding registers
pub struct ModbusServer {
    holding_registers: Vec<u16>,
}

impl ModbusServer {
    pub fn new(size: usize) -> Self {
        ModbusServer {
            holding_registers: vec![0; size],
        }
    }

    /// Process mask write register request
    pub fn process_mask_write(
        &mut self,
        address: u16,
        and_mask: u16,
        or_mask: u16,
    ) -> Result<(), ModbusError> {
        let addr = address as usize;
        
        if addr >= self.holding_registers.len() {
            return Err(ModbusError::new("Address out of range"));
        }

        let current_value = self.holding_registers[addr];
        
        // Perform AND-OR masking operation
        self.holding_registers[addr] = (current_value & and_mask) | (or_mask & !and_mask);

        Ok(())
    }

    /// Get register value
    pub fn get_register(&self, address: u16) -> Option<u16> {
        self.holding_registers.get(address as usize).copied()
    }

    /// Set register value
    pub fn set_register(&mut self, address: u16, value: u16) -> Result<(), ModbusError> {
        let addr = address as usize;
        
        if addr >= self.holding_registers.len() {
            return Err(ModbusError::new("Address out of range"));
        }

        self.holding_registers[addr] = value;
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    // Create a Modbus server with 100 registers
    let mut server = ModbusServer::new(100);

    // Initialize register 10 with value 0x00F2
    server.set_register(10, 0x00F2)?;
    println!("Initial register value: 0x{:04X}", server.get_register(10).unwrap());

    // Create a mask write request
    // Set bit 2, clear bit 1
    let and_mask = 0xFFFD; // Clear bit 1 (binary: 1111 1111 1111 1101)
    let or_mask = 0x0004;  // Set bit 2   (binary: 0000 0000 0000 0100)

    let request = MaskWriteRequest::new(10, and_mask, or_mask);
    
    println!("\nRequest details:");
    println!("  Address: {}", request.reference_address);
    println!("  AND Mask: 0x{:04X}", request.and_mask);
    println!("  OR Mask: 0x{:04X}", request.or_mask);

    // Serialize request
    let request_bytes = request.to_bytes();
    print!("\nRequest PDU (hex): ");
    for byte in &request_bytes {
        print!("{:02X} ", byte);
    }
    println!();

    // Process the request on the server
    server.process_mask_write(
        request.reference_address,
        request.and_mask,
        request.or_mask,
    )?;

    println!("\nModified register value: 0x{:04X}", server.get_register(10).unwrap());

    // Create response
    let response = MaskWriteResponse::new(
        request.reference_address,
        request.and_mask,
        request.or_mask,
    );

    // Serialize response
    let response_bytes = response.to_bytes();
    print!("\nResponse PDU (hex): ");
    for byte in &response_bytes {
        print!("{:02X} ", byte);
    }
    println!();

    // Parse response
    let parsed_response = MaskWriteResponse::from_bytes(&response_bytes)?;
    println!("\nParsed response:");
    println!("  Address: {}", parsed_response.reference_address);
    println!("  AND Mask: 0x{:04X}", parsed_response.and_mask);
    println!("  OR Mask: 0x{:04X}", parsed_response.or_mask);

    Ok(())
}
```

---

## Summary

**Function Code 0x16 (Mask Write Register)** provides an atomic operation for modifying specific bits within a Modbus holding register without affecting other bits. This function uses two masks: an AND mask to clear bits and an OR mask to set bits, applying the formula `Result = (Current AND And_Mask) OR (Or_Mask AND NOT And_Mask)`.

**Key characteristics**: The request and response are identical in structure (7 bytes each), both containing the function code, register address, AND mask, and OR mask. This function is particularly valuable in industrial automation where individual control flags need to be manipulated independently within packed register formats.

**Common applications** include toggling device control bits, managing status flags in PLCs, updating configuration registers, and implementing bit-level state machines. The atomic nature of the operation ensures register integrity in multi-threaded or networked environments where race conditions could otherwise occur during read-modify-write sequences.

Both code examples demonstrate complete client and server implementations with proper byte ordering, error handling, and the core masking algorithm, making them production-ready starting points for Modbus applications.