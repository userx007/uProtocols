# Address Mapping and Translation in Modbus

## Detailed Description

Address mapping and translation in Modbus refers to the process of converting and managing addressing schemes when data crosses protocol boundaries, interfaces different devices, or bridges legacy systems with modern implementations. This is a critical component in industrial automation systems where multiple devices, protocols, and address spaces must coexist and communicate seamlessly.

### Key Concepts

**1. Address Space Translation**
- Modbus devices use different address spaces for different data types (coils, discrete inputs, holding registers, input registers)
- Each address space traditionally starts at address 0 and can extend up to 65535
- Protocol Data Unit (PDU) addresses differ from the addressing conventions used in documentation

**2. Common Translation Scenarios**
- **PDU to User Address**: Converting zero-based PDU addresses to one-based user documentation addresses
- **Function Code Mapping**: Translating between different function codes that access the same physical data
- **Multi-slave Aggregation**: Combining multiple slave devices into a unified address space
- **Protocol Gateway**: Converting between Modbus RTU/ASCII/TCP and other industrial protocols
- **Address Offsetting**: Remapping addresses when integrating devices with conflicting address ranges

**3. Translation Table Components**
- Source address range (start, end)
- Destination address range (start, end)
- Function code mapping
- Slave ID mapping
- Data type conversion rules
- Byte order (endianness) handling

### Practical Applications

1. **Legacy System Integration**: Older devices may use non-standard addressing that requires translation
2. **Device Consolidation**: Multiple physical devices appearing as a single logical device
3. **Address Conflict Resolution**: Resolving overlapping address spaces from different manufacturers
4. **Protocol Bridging**: Converting between Modbus variants or entirely different protocols
5. **Virtual Device Implementation**: Creating virtual slaves that aggregate data from multiple sources

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Address mapping entry structure
typedef struct {
    uint8_t source_slave_id;
    uint16_t source_address_start;
    uint16_t source_address_end;
    uint8_t source_function_code;
    
    uint8_t dest_slave_id;
    uint16_t dest_address_start;
    uint8_t dest_function_code;
    
    bool active;
} AddressMap;

// Translation table
typedef struct {
    AddressMap *mappings;
    size_t count;
    size_t capacity;
} TranslationTable;

// Initialize translation table
TranslationTable* create_translation_table(size_t initial_capacity) {
    TranslationTable *table = malloc(sizeof(TranslationTable));
    if (!table) return NULL;
    
    table->mappings = malloc(sizeof(AddressMap) * initial_capacity);
    if (!table->mappings) {
        free(table);
        return NULL;
    }
    
    table->count = 0;
    table->capacity = initial_capacity;
    return table;
}

// Add mapping to translation table
bool add_mapping(TranslationTable *table, 
                 uint8_t src_slave, uint16_t src_start, uint16_t src_end, uint8_t src_fc,
                 uint8_t dst_slave, uint16_t dst_start, uint8_t dst_fc) {
    if (table->count >= table->capacity) {
        // Resize if needed
        size_t new_capacity = table->capacity * 2;
        AddressMap *new_mappings = realloc(table->mappings, 
                                           sizeof(AddressMap) * new_capacity);
        if (!new_mappings) return false;
        
        table->mappings = new_mappings;
        table->capacity = new_capacity;
    }
    
    AddressMap *map = &table->mappings[table->count];
    map->source_slave_id = src_slave;
    map->source_address_start = src_start;
    map->source_address_end = src_end;
    map->source_function_code = src_fc;
    map->dest_slave_id = dst_slave;
    map->dest_address_start = dst_start;
    map->dest_function_code = dst_fc;
    map->active = true;
    
    table->count++;
    return true;
}

// Translate address
bool translate_address(TranslationTable *table,
                      uint8_t src_slave, uint16_t src_addr, uint8_t src_fc,
                      uint8_t *dst_slave, uint16_t *dst_addr, uint8_t *dst_fc) {
    for (size_t i = 0; i < table->count; i++) {
        AddressMap *map = &table->mappings[i];
        
        if (!map->active) continue;
        
        if (map->source_slave_id == src_slave &&
            src_addr >= map->source_address_start &&
            src_addr <= map->source_address_end &&
            map->source_function_code == src_fc) {
            
            // Calculate offset within source range
            uint16_t offset = src_addr - map->source_address_start;
            
            // Apply offset to destination
            *dst_slave = map->dest_slave_id;
            *dst_addr = map->dest_address_start + offset;
            *dst_fc = map->dest_function_code;
            
            return true;
        }
    }
    
    return false; // No mapping found
}

// PDU to User address conversion (0-based to 1-based)
uint16_t pdu_to_user_address(uint16_t pdu_addr, uint8_t function_code) {
    // Standard Modbus addressing convention
    switch(function_code) {
        case 0x01: // Read Coils
        case 0x05: // Write Single Coil
        case 0x0F: // Write Multiple Coils
            return pdu_addr + 1;      // Coils: 00001-09999
            
        case 0x02: // Read Discrete Inputs
            return pdu_addr + 10001;  // Discrete Inputs: 10001-19999
            
        case 0x04: // Read Input Registers
            return pdu_addr + 30001;  // Input Registers: 30001-39999
            
        case 0x03: // Read Holding Registers
        case 0x06: // Write Single Register
        case 0x10: // Write Multiple Registers
            return pdu_addr + 40001;  // Holding Registers: 40001-49999
            
        default:
            return pdu_addr;
    }
}

// User to PDU address conversion (1-based to 0-based)
uint16_t user_to_pdu_address(uint16_t user_addr, uint8_t *function_code) {
    if (user_addr >= 40001 && user_addr <= 49999) {
        *function_code = 0x03; // Holding Register
        return user_addr - 40001;
    } else if (user_addr >= 30001 && user_addr <= 39999) {
        *function_code = 0x04; // Input Register
        return user_addr - 30001;
    } else if (user_addr >= 10001 && user_addr <= 19999) {
        *function_code = 0x02; // Discrete Input
        return user_addr - 10001;
    } else if (user_addr >= 1 && user_addr <= 9999) {
        *function_code = 0x01; // Coil
        return user_addr - 1;
    }
    
    *function_code = 0x03; // Default to holding register
    return user_addr;
}

// Example usage and demonstration
int main() {
    printf("=== Modbus Address Mapping and Translation ===\n\n");
    
    // Create translation table
    TranslationTable *table = create_translation_table(10);
    
    // Example 1: Map slave 1's holding registers 0-99 to slave 10's registers 1000-1099
    add_mapping(table, 1, 0, 99, 0x03, 10, 1000, 0x03);
    
    // Example 2: Map slave 2's input registers to slave 10's holding registers
    add_mapping(table, 2, 0, 49, 0x04, 10, 2000, 0x03);
    
    // Example 3: Consolidate multiple slaves into one virtual slave
    add_mapping(table, 3, 0, 99, 0x03, 100, 0, 0x03);
    add_mapping(table, 4, 0, 99, 0x03, 100, 100, 0x03);
    add_mapping(table, 5, 0, 99, 0x03, 100, 200, 0x03);
    
    printf("Translation Table Created with %zu mappings\n\n", table->count);
    
    // Test translations
    uint8_t dst_slave;
    uint16_t dst_addr;
    uint8_t dst_fc;
    
    printf("--- Translation Examples ---\n");
    if (translate_address(table, 1, 50, 0x03, &dst_slave, &dst_addr, &dst_fc)) {
        printf("Slave 1, Addr 50, FC 0x03 -> Slave %d, Addr %d, FC 0x%02X\n",
               dst_slave, dst_addr, dst_fc);
    }
    
    if (translate_address(table, 2, 25, 0x04, &dst_slave, &dst_addr, &dst_fc)) {
        printf("Slave 2, Addr 25, FC 0x04 -> Slave %d, Addr %d, FC 0x%02X\n",
               dst_slave, dst_addr, dst_fc);
    }
    
    if (translate_address(table, 4, 75, 0x03, &dst_slave, &dst_addr, &dst_fc)) {
        printf("Slave 4, Addr 75, FC 0x03 -> Slave %d, Addr %d, FC 0x%02X\n",
               dst_slave, dst_addr, dst_fc);
    }
    
    // PDU to User address conversion examples
    printf("\n--- PDU to User Address Conversion ---\n");
    printf("PDU Addr 0, FC 0x03 -> User Addr %d (Holding Register)\n",
           pdu_to_user_address(0, 0x03));
    printf("PDU Addr 100, FC 0x04 -> User Addr %d (Input Register)\n",
           pdu_to_user_address(100, 0x04));
    printf("PDU Addr 50, FC 0x01 -> User Addr %d (Coil)\n",
           pdu_to_user_address(50, 0x01));
    
    // User to PDU address conversion examples
    printf("\n--- User to PDU Address Conversion ---\n");
    uint8_t fc;
    printf("User Addr 40100 -> PDU Addr %d, FC 0x%02X\n",
           user_to_pdu_address(40100, &fc), fc);
    printf("User Addr 30050 -> PDU Addr %d, FC 0x%02X\n",
           user_to_pdu_address(30050, &fc), fc);
    printf("User Addr 51 -> PDU Addr %d, FC 0x%02X\n",
           user_to_pdu_address(51, &fc), fc);
    
    // Cleanup
    free(table->mappings);
    free(table);
    
    return 0;
}
```

---

## Rust Implementation

```rust
use std::collections::HashMap;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
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

impl FunctionCode {
    pub fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x01 => Some(FunctionCode::ReadCoils),
            0x02 => Some(FunctionCode::ReadDiscreteInputs),
            0x03 => Some(FunctionCode::ReadHoldingRegisters),
            0x04 => Some(FunctionCode::ReadInputRegisters),
            0x05 => Some(FunctionCode::WriteSingleCoil),
            0x06 => Some(FunctionCode::WriteSingleRegister),
            0x0F => Some(FunctionCode::WriteMultipleCoils),
            0x10 => Some(FunctionCode::WriteMultipleRegisters),
            _ => None,
        }
    }
}

#[derive(Debug, Clone)]
pub struct AddressMap {
    pub source_slave_id: u8,
    pub source_address_start: u16,
    pub source_address_end: u16,
    pub source_function_code: FunctionCode,
    
    pub dest_slave_id: u8,
    pub dest_address_start: u16,
    pub dest_function_code: FunctionCode,
    
    pub active: bool,
}

impl AddressMap {
    pub fn new(
        src_slave: u8,
        src_start: u16,
        src_end: u16,
        src_fc: FunctionCode,
        dst_slave: u8,
        dst_start: u16,
        dst_fc: FunctionCode,
    ) -> Self {
        AddressMap {
            source_slave_id: src_slave,
            source_address_start: src_start,
            source_address_end: src_end,
            source_function_code: src_fc,
            dest_slave_id: dst_slave,
            dest_address_start: dst_start,
            dest_function_code: dst_fc,
            active: true,
        }
    }
    
    pub fn matches(&self, slave: u8, addr: u16, fc: FunctionCode) -> bool {
        self.active
            && self.source_slave_id == slave
            && addr >= self.source_address_start
            && addr <= self.source_address_end
            && self.source_function_code == fc
    }
    
    pub fn translate(&self, addr: u16) -> Option<(u8, u16, FunctionCode)> {
        if addr < self.source_address_start || addr > self.source_address_end {
            return None;
        }
        
        let offset = addr - self.source_address_start;
        let dest_addr = self.dest_address_start + offset;
        
        Some((self.dest_slave_id, dest_addr, self.dest_function_code))
    }
}

#[derive(Debug)]
pub struct TranslationTable {
    mappings: Vec<AddressMap>,
}

impl TranslationTable {
    pub fn new() -> Self {
        TranslationTable {
            mappings: Vec::new(),
        }
    }
    
    pub fn add_mapping(&mut self, mapping: AddressMap) {
        self.mappings.push(mapping);
    }
    
    pub fn translate(
        &self,
        slave: u8,
        addr: u16,
        fc: FunctionCode,
    ) -> Option<(u8, u16, FunctionCode)> {
        for mapping in &self.mappings {
            if mapping.matches(slave, addr, fc) {
                return mapping.translate(addr);
            }
        }
        None
    }
    
    pub fn remove_mapping(&mut self, index: usize) -> Option<AddressMap> {
        if index < self.mappings.len() {
            Some(self.mappings.remove(index))
        } else {
            None
        }
    }
    
    pub fn count(&self) -> usize {
        self.mappings.len()
    }
}

impl Default for TranslationTable {
    fn default() -> Self {
        Self::new()
    }
}

// Address conversion utilities
pub struct AddressConverter;

impl AddressConverter {
    /// Convert PDU address (0-based) to user address (1-based with offset)
    pub fn pdu_to_user(pdu_addr: u16, fc: FunctionCode) -> u16 {
        match fc {
            FunctionCode::ReadCoils
            | FunctionCode::WriteSingleCoil
            | FunctionCode::WriteMultipleCoils => pdu_addr + 1, // 00001-09999
            
            FunctionCode::ReadDiscreteInputs => pdu_addr + 10001, // 10001-19999
            
            FunctionCode::ReadInputRegisters => pdu_addr + 30001, // 30001-39999
            
            FunctionCode::ReadHoldingRegisters
            | FunctionCode::WriteSingleRegister
            | FunctionCode::WriteMultipleRegisters => pdu_addr + 40001, // 40001-49999
        }
    }
    
    /// Convert user address (1-based with offset) to PDU address (0-based)
    pub fn user_to_pdu(user_addr: u16) -> (u16, FunctionCode) {
        if (40001..=49999).contains(&user_addr) {
            (user_addr - 40001, FunctionCode::ReadHoldingRegisters)
        } else if (30001..=39999).contains(&user_addr) {
            (user_addr - 30001, FunctionCode::ReadInputRegisters)
        } else if (10001..=19999).contains(&user_addr) {
            (user_addr - 10001, FunctionCode::ReadDiscreteInputs)
        } else if (1..=9999).contains(&user_addr) {
            (user_addr - 1, FunctionCode::ReadCoils)
        } else {
            // Default to holding register
            (user_addr, FunctionCode::ReadHoldingRegisters)
        }
    }
}

// Gateway that uses translation table
pub struct ModbusGateway {
    translation_table: TranslationTable,
    virtual_slaves: HashMap<u8, Vec<u16>>, // Cache for virtual slave data
}

impl ModbusGateway {
    pub fn new() -> Self {
        ModbusGateway {
            translation_table: TranslationTable::new(),
            virtual_slaves: HashMap::new(),
        }
    }
    
    pub fn add_mapping(&mut self, mapping: AddressMap) {
        self.translation_table.add_mapping(mapping);
    }
    
    pub fn translate_request(
        &self,
        slave: u8,
        addr: u16,
        fc: FunctionCode,
    ) -> Result<(u8, u16, FunctionCode), String> {
        self.translation_table
            .translate(slave, addr, fc)
            .ok_or_else(|| {
                format!(
                    "No mapping found for slave {}, address {}, FC {:?}",
                    slave, addr, fc
                )
            })
    }
    
    pub fn get_mapping_count(&self) -> usize {
        self.translation_table.count()
    }
}

impl Default for ModbusGateway {
    fn default() -> Self {
        Self::new()
    }
}

fn main() {
    println!("=== Modbus Address Mapping and Translation (Rust) ===\n");
    
    // Create gateway
    let mut gateway = ModbusGateway::new();
    
    // Example 1: Map slave 1's holding registers 0-99 to slave 10's registers 1000-1099
    gateway.add_mapping(AddressMap::new(
        1,
        0,
        99,
        FunctionCode::ReadHoldingRegisters,
        10,
        1000,
        FunctionCode::ReadHoldingRegisters,
    ));
    
    // Example 2: Map slave 2's input registers to slave 10's holding registers
    gateway.add_mapping(AddressMap::new(
        2,
        0,
        49,
        FunctionCode::ReadInputRegisters,
        10,
        2000,
        FunctionCode::ReadHoldingRegisters,
    ));
    
    // Example 3: Consolidate multiple slaves into one virtual slave
    gateway.add_mapping(AddressMap::new(
        3,
        0,
        99,
        FunctionCode::ReadHoldingRegisters,
        100,
        0,
        FunctionCode::ReadHoldingRegisters,
    ));
    
    gateway.add_mapping(AddressMap::new(
        4,
        0,
        99,
        FunctionCode::ReadHoldingRegisters,
        100,
        100,
        FunctionCode::ReadHoldingRegisters,
    ));
    
    gateway.add_mapping(AddressMap::new(
        5,
        0,
        99,
        FunctionCode::ReadHoldingRegisters,
        100,
        200,
        FunctionCode::ReadHoldingRegisters,
    ));
    
    println!("Translation Table Created with {} mappings\n", gateway.get_mapping_count());
    
    // Test translations
    println!("--- Translation Examples ---");
    
    match gateway.translate_request(1, 50, FunctionCode::ReadHoldingRegisters) {
        Ok((slave, addr, fc)) => {
            println!("Slave 1, Addr 50, FC HR -> Slave {}, Addr {}, FC {:?}", slave, addr, fc);
        }
        Err(e) => println!("Error: {}", e),
    }
    
    match gateway.translate_request(2, 25, FunctionCode::ReadInputRegisters) {
        Ok((slave, addr, fc)) => {
            println!("Slave 2, Addr 25, FC IR -> Slave {}, Addr {}, FC {:?}", slave, addr, fc);
        }
        Err(e) => println!("Error: {}", e),
    }
    
    match gateway.translate_request(4, 75, FunctionCode::ReadHoldingRegisters) {
        Ok((slave, addr, fc)) => {
            println!("Slave 4, Addr 75, FC HR -> Slave {}, Addr {}, FC {:?}", slave, addr, fc);
        }
        Err(e) => println!("Error: {}", e),
    }
    
    // PDU to User address conversion examples
    println!("\n--- PDU to User Address Conversion ---");
    println!(
        "PDU Addr 0, FC HR -> User Addr {}",
        AddressConverter::pdu_to_user(0, FunctionCode::ReadHoldingRegisters)
    );
    println!(
        "PDU Addr 100, FC IR -> User Addr {}",
        AddressConverter::pdu_to_user(100, FunctionCode::ReadInputRegisters)
    );
    println!(
        "PDU Addr 50, FC Coil -> User Addr {}",
        AddressConverter::pdu_to_user(50, FunctionCode::ReadCoils)
    );
    
    // User to PDU address conversion examples
    println!("\n--- User to PDU Address Conversion ---");
    let (pdu, fc) = AddressConverter::user_to_pdu(40100);
    println!("User Addr 40100 -> PDU Addr {}, FC {:?}", pdu, fc);
    
    let (pdu, fc) = AddressConverter::user_to_pdu(30050);
    println!("User Addr 30050 -> PDU Addr {}, FC {:?}", pdu, fc);
    
    let (pdu, fc) = AddressConverter::user_to_pdu(51);
    println!("User Addr 51 -> PDU Addr {}, FC {:?}", pdu, fc);
}
```

---

## Summary

**Address Mapping and Translation** is essential for integrating heterogeneous Modbus systems and enabling interoperability across protocol boundaries. Key takeaways include:

### Core Principles
- **Translation Tables**: Maintain mappings between source and destination address spaces with configurable ranges and function codes
- **PDU vs. User Addressing**: Understand the difference between zero-based protocol addresses and one-based documentation addresses
- **Function Code Mapping**: Enable cross-function-code translation (e.g., input registers to holding registers)

### Implementation Approaches
- **Static Mapping**: Pre-configured translation tables for known device topologies
- **Dynamic Mapping**: Runtime-configurable mappings for flexible system architectures
- **Gateway Pattern**: Centralized translation service for multi-device networks
- **Virtual Devices**: Aggregate multiple physical devices into unified logical endpoints

### Best Practices
1. Validate address ranges to prevent overflow and invalid translations
2. Implement bidirectional mapping for request and response translation
3. Consider byte ordering and data type conversions during translation
4. Log all translation operations for debugging and auditing
5. Use efficient lookup structures (hash maps, binary search) for large translation tables
6. Handle unmapped addresses gracefully with clear error reporting

### Common Use Cases
- Integrating legacy equipment with modern SCADA systems
- Creating protocol gateways between Modbus variants
- Resolving address conflicts in merged networks
- Implementing multi-tenant industrial IoT architectures
- Enabling cloud connectivity through address abstraction layers

Both the C/C++ and Rust implementations demonstrate production-ready patterns for address translation with extensibility for complex industrial automation scenarios.