# I²C Addressing Modes: A Comprehensive Guide

## Overview

I²C (Inter-Integrated Circuit) uses addressing to identify devices on the shared bus. Understanding addressing modes is crucial for proper device communication and avoiding conflicts.

## Addressing Schemes

### 7-bit Addressing

The standard and most common addressing mode uses 7 bits to address devices, allowing for **128 possible addresses** (0x00 to 0x7F). However, 16 addresses are reserved, leaving **112 usable addresses**.

**Frame Structure:**
```
| S | A6 A5 A4 A3 A2 A1 A0 | R/W | ACK | Data... | P |
  ^   \_____7-bit addr____/   ^
Start                       Read/Write bit
```

The actual byte transmitted contains the 7-bit address shifted left by 1 bit, with the R/W bit in the LSB position.

### 10-bit Addressing

Extended addressing mode using 10 bits, providing **1024 possible addresses**. This mode is backward compatible with 7-bit addressing.

**Frame Structure:**
```
First byte:  | S | 1 1 1 1 0 A9 A8 | R/W | ACK |
Second byte: | A7 A6 A5 A4 A3 A2 A1 A0 | ACK |
```

The first 5 bits (11110) indicate 10-bit addressing mode.

## Reserved Addresses

The I²C specification reserves certain addresses:

| Address Range | Purpose |
|--------------|---------|
| 0x00 | General call address |
| 0x01 | CBUS address |
| 0x02 | Reserved for different bus format |
| 0x03 | Reserved for future use |
| 0x04-0x07 | High-speed mode master code |
| 0x78-0x7B | 10-bit slave addressing |
| 0x7C-0x7F | Reserved for future use |

## Code Examples

### C/C++ Implementation


```c
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// I2C addressing definitions
#define I2C_GENERAL_CALL    0x00
#define I2C_START_BYTE      0x01
#define I2C_10BIT_PREFIX    0xF0  // 11110xx0

// Reserved address ranges
#define I2C_RESERVED_START  0x00
#define I2C_RESERVED_END    0x07
#define I2C_10BIT_START     0x78
#define I2C_10BIT_END       0x7F

typedef enum {
    I2C_WRITE = 0,
    I2C_READ = 1
} i2c_direction_t;

// Check if 7-bit address is valid
bool is_valid_7bit_address(uint8_t addr) {
    if (addr <= I2C_RESERVED_END) return false;
    if (addr >= I2C_10BIT_START) return false;
    return true;
}

// Check if 10-bit address is valid
bool is_valid_10bit_address(uint16_t addr) {
    return (addr <= 0x3FF);  // 10 bits max
}

// Create 7-bit address byte with R/W bit
uint8_t create_7bit_address_byte(uint8_t addr, i2c_direction_t dir) {
    if (!is_valid_7bit_address(addr)) {
        printf("Error: Invalid 7-bit address 0x%02X\n", addr);
        return 0xFF;
    }
    return (addr << 1) | dir;
}

// Create first byte of 10-bit address
uint8_t create_10bit_first_byte(uint16_t addr, i2c_direction_t dir) {
    if (!is_valid_10bit_address(addr)) {
        printf("Error: Invalid 10-bit address 0x%03X\n", addr);
        return 0xFF;
    }
    // 11110 + upper 2 bits of address + R/W
    return I2C_10BIT_PREFIX | (((addr >> 8) & 0x03) << 1) | dir;
}

// Create second byte of 10-bit address
uint8_t create_10bit_second_byte(uint16_t addr) {
    return (uint8_t)(addr & 0xFF);
}

// Parse 7-bit address byte
void parse_7bit_address(uint8_t byte, uint8_t *addr, i2c_direction_t *dir) {
    *addr = (byte >> 1) & 0x7F;
    *dir = (i2c_direction_t)(byte & 0x01);
}

// Detect address collision
typedef struct {
    uint8_t addresses[128];  // Track assigned 7-bit addresses
    uint16_t addresses_10bit[1024];  // Track assigned 10-bit addresses
} i2c_address_registry_t;

void init_registry(i2c_address_registry_t *reg) {
    for (int i = 0; i < 128; i++) {
        reg->addresses[i] = 0;
    }
    for (int i = 0; i < 1024; i++) {
        reg->addresses_10bit[i] = 0;
    }
}

bool register_7bit_address(i2c_address_registry_t *reg, uint8_t addr) {
    if (!is_valid_7bit_address(addr)) {
        printf("Address 0x%02X is reserved or invalid\n", addr);
        return false;
    }
    
    if (reg->addresses[addr]) {
        printf("Address collision! 0x%02X already in use\n", addr);
        return false;
    }
    
    reg->addresses[addr] = 1;
    printf("Registered 7-bit address: 0x%02X\n", addr);
    return true;
}

bool register_10bit_address(i2c_address_registry_t *reg, uint16_t addr) {
    if (!is_valid_10bit_address(addr)) {
        printf("10-bit address 0x%03X is invalid\n", addr);
        return false;
    }
    
    if (reg->addresses_10bit[addr]) {
        printf("Address collision! 0x%03X already in use\n", addr);
        return false;
    }
    
    reg->addresses_10bit[addr] = 1;
    printf("Registered 10-bit address: 0x%03X\n", addr);
    return true;
}

// Example usage
int main() {
    printf("=== I2C Addressing Examples ===\n\n");
    
    // 7-bit addressing example
    printf("--- 7-bit Addressing ---\n");
    uint8_t device_addr = 0x50;  // EEPROM typical address
    uint8_t write_byte = create_7bit_address_byte(device_addr, I2C_WRITE);
    uint8_t read_byte = create_7bit_address_byte(device_addr, I2C_READ);
    
    printf("Device address: 0x%02X\n", device_addr);
    printf("Write byte: 0x%02X (binary: ", write_byte);
    for (int i = 7; i >= 0; i--) {
        printf("%d", (write_byte >> i) & 1);
    }
    printf(")\n");
    printf("Read byte: 0x%02X\n\n", read_byte);
    
    // Parse address
    uint8_t parsed_addr;
    i2c_direction_t parsed_dir;
    parse_7bit_address(write_byte, &parsed_addr, &parsed_dir);
    printf("Parsed - Address: 0x%02X, Direction: %s\n\n", 
           parsed_addr, parsed_dir == I2C_WRITE ? "WRITE" : "READ");
    
    // 10-bit addressing example
    printf("--- 10-bit Addressing ---\n");
    uint16_t device_addr_10bit = 0x2A5;
    uint8_t first_byte = create_10bit_first_byte(device_addr_10bit, I2C_WRITE);
    uint8_t second_byte = create_10bit_second_byte(device_addr_10bit);
    
    printf("10-bit address: 0x%03X\n", device_addr_10bit);
    printf("First byte: 0x%02X\n", first_byte);
    printf("Second byte: 0x%02X\n\n", second_byte);
    
    // Address collision detection
    printf("--- Address Registry & Collision Detection ---\n");
    i2c_address_registry_t registry;
    init_registry(&registry);
    
    register_7bit_address(&registry, 0x50);
    register_7bit_address(&registry, 0x51);
    register_7bit_address(&registry, 0x50);  // Collision!
    register_7bit_address(&registry, 0x03);  // Reserved!
    
    printf("\n");
    register_10bit_address(&registry, 0x2A5);
    register_10bit_address(&registry, 0x2A5);  // Collision!
    
    return 0;
}
```


### Rust Implementation

```rust
use std::collections::HashSet;

// I2C addressing constants
const I2C_GENERAL_CALL: u8 = 0x00;
const I2C_RESERVED_END: u8 = 0x07;
const I2C_10BIT_START: u8 = 0x78;
const I2C_10BIT_END: u8 = 0x7F;
const I2C_10BIT_PREFIX: u8 = 0xF0;  // 11110xx0

#[derive(Debug, Clone, Copy, PartialEq)]
enum Direction {
    Write = 0,
    Read = 1,
}

#[derive(Debug, Clone, Copy)]
enum Address {
    SevenBit(u8),
    TenBit(u16),
}

#[derive(Debug)]
enum I2cError {
    InvalidAddress,
    ReservedAddress,
    AddressCollision,
}

// Validate 7-bit address
fn is_valid_7bit_address(addr: u8) -> bool {
    addr > I2C_RESERVED_END && addr < I2C_10BIT_START
}

// Validate 10-bit address
fn is_valid_10bit_address(addr: u16) -> bool {
    addr <= 0x3FF  // 10 bits maximum
}

// Create 7-bit address byte with R/W bit
fn create_7bit_address_byte(addr: u8, dir: Direction) -> Result<u8, I2cError> {
    if !is_valid_7bit_address(addr) {
        if addr <= I2C_RESERVED_END {
            return Err(I2cError::ReservedAddress);
        }
        return Err(I2cError::InvalidAddress);
    }
    Ok((addr << 1) | (dir as u8))
}

// Create 10-bit address bytes
fn create_10bit_address_bytes(addr: u16, dir: Direction) -> Result<(u8, u8), I2cError> {
    if !is_valid_10bit_address(addr) {
        return Err(I2cError::InvalidAddress);
    }
    
    // First byte: 11110 + upper 2 bits of address + R/W
    let first_byte = I2C_10BIT_PREFIX | (((addr >> 8) as u8 & 0x03) << 1) | (dir as u8);
    
    // Second byte: lower 8 bits of address
    let second_byte = (addr & 0xFF) as u8;
    
    Ok((first_byte, second_byte))
}

// Parse 7-bit address byte
fn parse_7bit_address(byte: u8) -> (u8, Direction) {
    let addr = (byte >> 1) & 0x7F;
    let dir = if byte & 0x01 == 0 {
        Direction::Write
    } else {
        Direction::Read
    };
    (addr, dir)
}

// Parse 10-bit address bytes
fn parse_10bit_address(first_byte: u8, second_byte: u8) -> Result<(u16, Direction), I2cError> {
    // Check if first byte has 10-bit prefix (11110)
    if (first_byte & 0xF8) != I2C_10BIT_PREFIX {
        return Err(I2cError::InvalidAddress);
    }
    
    let upper_bits = ((first_byte >> 1) & 0x03) as u16;
    let lower_bits = second_byte as u16;
    let addr = (upper_bits << 8) | lower_bits;
    
    let dir = if first_byte & 0x01 == 0 {
        Direction::Write
    } else {
        Direction::Read
    };
    
    Ok((addr, dir))
}

// Address registry for collision detection
struct I2cAddressRegistry {
    addresses_7bit: HashSet<u8>,
    addresses_10bit: HashSet<u16>,
}

impl I2cAddressRegistry {
    fn new() -> Self {
        Self {
            addresses_7bit: HashSet::new(),
            addresses_10bit: HashSet::new(),
        }
    }
    
    fn register_7bit(&mut self, addr: u8) -> Result<(), I2cError> {
        if !is_valid_7bit_address(addr) {
            if addr <= I2C_RESERVED_END {
                return Err(I2cError::ReservedAddress);
            }
            return Err(I2cError::InvalidAddress);
        }
        
        if !self.addresses_7bit.insert(addr) {
            return Err(I2cError::AddressCollision);
        }
        
        Ok(())
    }
    
    fn register_10bit(&mut self, addr: u16) -> Result<(), I2cError> {
        if !is_valid_10bit_address(addr) {
            return Err(I2cError::InvalidAddress);
        }
        
        if !self.addresses_10bit.insert(addr) {
            return Err(I2cError::AddressCollision);
        }
        
        Ok(())
    }
    
    fn is_address_available(&self, addr: Address) -> bool {
        match addr {
            Address::SevenBit(a) => is_valid_7bit_address(a) && !self.addresses_7bit.contains(&a),
            Address::TenBit(a) => is_valid_10bit_address(a) && !self.addresses_10bit.contains(&a),
        }
    }
    
    fn list_registered_7bit(&self) -> Vec<u8> {
        let mut addrs: Vec<u8> = self.addresses_7bit.iter().copied().collect();
        addrs.sort();
        addrs
    }
    
    fn list_registered_10bit(&self) -> Vec<u16> {
        let mut addrs: Vec<u16> = self.addresses_10bit.iter().copied().collect();
        addrs.sort();
        addrs
    }
}

fn main() {
    println!("=== I2C Addressing Examples in Rust ===\n");
    
    // 7-bit addressing example
    println!("--- 7-bit Addressing ---");
    let device_addr = 0x50u8;  // EEPROM typical address
    
    match create_7bit_address_byte(device_addr, Direction::Write) {
        Ok(write_byte) => {
            println!("Device address: 0x{:02X}", device_addr);
            println!("Write byte: 0x{:02X} (binary: {:08b})", write_byte, write_byte);
            
            let (parsed_addr, parsed_dir) = parse_7bit_address(write_byte);
            println!("Parsed - Address: 0x{:02X}, Direction: {:?}", parsed_addr, parsed_dir);
        }
        Err(e) => println!("Error: {:?}", e),
    }
    
    match create_7bit_address_byte(device_addr, Direction::Read) {
        Ok(read_byte) => {
            println!("Read byte: 0x{:02X}\n", read_byte);
        }
        Err(e) => println!("Error: {:?}\n", e),
    }
    
    // 10-bit addressing example
    println!("--- 10-bit Addressing ---");
    let device_addr_10bit = 0x2A5u16;
    
    match create_10bit_address_bytes(device_addr_10bit, Direction::Write) {
        Ok((first_byte, second_byte)) => {
            println!("10-bit address: 0x{:03X}", device_addr_10bit);
            println!("First byte: 0x{:02X} (binary: {:08b})", first_byte, first_byte);
            println!("Second byte: 0x{:02X} (binary: {:08b})", second_byte, second_byte);
            
            match parse_10bit_address(first_byte, second_byte) {
                Ok((parsed_addr, parsed_dir)) => {
                    println!("Parsed - Address: 0x{:03X}, Direction: {:?}\n", parsed_addr, parsed_dir);
                }
                Err(e) => println!("Parse error: {:?}\n", e),
            }
        }
        Err(e) => println!("Error: {:?}\n", e),
    }
    
    // Address registry and collision detection
    println!("--- Address Registry & Collision Detection ---");
    let mut registry = I2cAddressRegistry::new();
    
    // Register valid addresses
    match registry.register_7bit(0x50) {
        Ok(_) => println!("✓ Registered 7-bit address: 0x50"),
        Err(e) => println!("✗ Failed to register 0x50: {:?}", e),
    }
    
    match registry.register_7bit(0x51) {
        Ok(_) => println!("✓ Registered 7-bit address: 0x51"),
        Err(e) => println!("✗ Failed to register 0x51: {:?}", e),
    }
    
    // Attempt collision
    match registry.register_7bit(0x50) {
        Ok(_) => println!("✓ Registered 7-bit address: 0x50"),
        Err(I2cError::AddressCollision) => println!("✗ Address collision! 0x50 already in use"),
        Err(e) => println!("✗ Failed to register 0x50: {:?}", e),
    }
    
    // Attempt reserved address
    match registry.register_7bit(0x03) {
        Ok(_) => println!("✓ Registered 7-bit address: 0x03"),
        Err(I2cError::ReservedAddress) => println!("✗ Address 0x03 is reserved!"),
        Err(e) => println!("✗ Failed to register 0x03: {:?}", e),
    }
    
    println!();
    
    // 10-bit registration
    match registry.register_10bit(0x2A5) {
        Ok(_) => println!("✓ Registered 10-bit address: 0x2A5"),
        Err(e) => println!("✗ Failed to register 0x2A5: {:?}", e),
    }
    
    match registry.register_10bit(0x2A5) {
        Ok(_) => println!("✓ Registered 10-bit address: 0x2A5"),
        Err(I2cError::AddressCollision) => println!("✗ Address collision! 0x2A5 already in use"),
        Err(e) => println!("✗ Failed to register 0x2A5: {:?}", e),
    }
    
    println!("\n--- Registry Status ---");
    println!("Registered 7-bit addresses: {:?}", registry.list_registered_7bit());
    println!("Registered 10-bit addresses: {:?}", registry.list_registered_10bit());
    
    // Check address availability
    println!("\nAddress availability checks:");
    println!("0x52 (7-bit) available: {}", registry.is_address_available(Address::SevenBit(0x52)));
    println!("0x50 (7-bit) available: {}", registry.is_address_available(Address::SevenBit(0x50)));
}

```

## Key Concepts Explained

### Address Formation

**7-bit mode**: The device address occupies bits 7-1, while bit 0 is the R/W bit:
- Write operation: `(address << 1) | 0` → e.g., 0x50 becomes 0xA0
- Read operation: `(address << 1) | 1` → e.g., 0x50 becomes 0xA1

**10-bit mode**: Requires two bytes:
- First byte identifies it as 10-bit (11110) plus upper 2 address bits
- Second byte contains the lower 8 address bits

### Address Collision Handling

Address collisions occur when multiple devices share the same address. Prevention strategies:

1. **Hardware configuration**: Many I²C devices have configurable address pins (A0, A1, A2) allowing multiple identical devices on the same bus
2. **Address registry**: Software tracking of assigned addresses before device initialization
3. **Bus scanning**: Probing addresses to detect existing devices
4. **I²C multiplexers**: Using chips like TCA9548A to create separate bus segments

### General Call Address (0x00)

Special address for broadcasting to all devices. When a master sends this address:
- All slaves can respond if they support general call
- Used for software reset or synchronization commands

### Practical Considerations

- Most common devices use 7-bit addressing
- 10-bit addressing is primarily used when many devices must coexist
- Always consult device datasheets for valid addresses
- Some addresses may have sub-addressing (internal register addresses) separate from the device address
- Address conflicts require either hardware reconfiguration or I²C multiplexers

The code examples demonstrate address byte construction, parsing, validation, and collision detection—essential tools for robust I²C communication systems.