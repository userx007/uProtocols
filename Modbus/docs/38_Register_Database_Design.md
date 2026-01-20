# Register Database Design for Modbus

## Overview

A Register Database is a structured data storage system that efficiently organizes and manages Modbus data points (coils, discrete inputs, holding registers, and input registers). It acts as the central repository for all Modbus-accessible data in a server/slave device, providing fast lookups, organized storage, and validation mechanisms.

## Core Concepts

### Why a Register Database?

In Modbus implementations, you need to manage four distinct data areas:
- **Coils** (0x): Read/write binary outputs
- **Discrete Inputs** (1x): Read-only binary inputs
- **Input Registers** (3x): Read-only 16-bit values
- **Holding Registers** (4x): Read/write 16-bit values

A well-designed register database provides:
- **Fast lookups** for read/write operations
- **Memory efficiency** through appropriate data structures
- **Data validation** and boundary checking
- **Metadata storage** (units, scaling, descriptions)
- **Access control** (read-only, write-only, read-write)
- **Change notifications** for monitoring updates

### Design Considerations

1. **Sparse vs. Dense Address Spaces**: Decide whether to allocate continuous memory or use sparse maps
2. **Thread Safety**: Handle concurrent access from multiple Modbus clients
3. **Persistence**: Optional saving/loading of register values
4. **Callbacks**: Trigger functions on read/write operations
5. **Value Mapping**: Handle data type conversions and scaling

## C/C++ Implementation

### Basic Structure with Dense Arrays

```c
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>

#define MAX_COILS 10000
#define MAX_DISCRETE_INPUTS 10000
#define MAX_HOLDING_REGISTERS 10000
#define MAX_INPUT_REGISTERS 10000

typedef struct {
    bool coils[MAX_COILS];
    bool discrete_inputs[MAX_DISCRETE_INPUTS];
    uint16_t holding_registers[MAX_HOLDING_REGISTERS];
    uint16_t input_registers[MAX_INPUT_REGISTERS];
    pthread_mutex_t lock;
} modbus_register_db_t;

// Initialize the database
void modbus_db_init(modbus_register_db_t *db) {
    memset(db->coils, 0, sizeof(db->coils));
    memset(db->discrete_inputs, 0, sizeof(db->discrete_inputs));
    memset(db->holding_registers, 0, sizeof(db->holding_registers));
    memset(db->input_registers, 0, sizeof(db->input_registers));
    pthread_mutex_init(&db->lock, NULL);
}

// Read coil with bounds checking
int modbus_db_read_coil(modbus_register_db_t *db, uint16_t address, bool *value) {
    if (address >= MAX_COILS) return -1;
    
    pthread_mutex_lock(&db->lock);
    *value = db->coils[address];
    pthread_mutex_unlock(&db->lock);
    return 0;
}

// Write holding register with bounds checking
int modbus_db_write_holding(modbus_register_db_t *db, uint16_t address, uint16_t value) {
    if (address >= MAX_HOLDING_REGISTERS) return -1;
    
    pthread_mutex_lock(&db->lock);
    db->holding_registers[address] = value;
    pthread_mutex_unlock(&db->lock);
    return 0;
}

// Bulk read for efficiency
int modbus_db_read_holdings_bulk(modbus_register_db_t *db, uint16_t start_addr, 
                                   uint16_t count, uint16_t *dest) {
    if (start_addr + count > MAX_HOLDING_REGISTERS) return -1;
    
    pthread_mutex_lock(&db->lock);
    memcpy(dest, &db->holding_registers[start_addr], count * sizeof(uint16_t));
    pthread_mutex_unlock(&db->lock);
    return 0;
}
```

### Advanced C++ Implementation with Callbacks

```cpp
#include <map>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

enum class RegisterType {
    COIL,
    DISCRETE_INPUT,
    HOLDING_REGISTER,
    INPUT_REGISTER
};

struct RegisterMetadata {
    std::string description;
    std::string units;
    float scale_factor = 1.0f;
    float offset = 0.0f;
    bool read_only = false;
};

class ModbusRegisterDatabase {
private:
    // Sparse storage for flexibility
    std::map<uint16_t, bool> coils_;
    std::map<uint16_t, bool> discrete_inputs_;
    std::map<uint16_t, uint16_t> holding_registers_;
    std::map<uint16_t, uint16_t> input_registers_;
    
    // Metadata storage
    std::map<uint16_t, RegisterMetadata> holding_metadata_;
    std::map<uint16_t, RegisterMetadata> input_metadata_;
    
    // Callbacks
    using WriteCallback = std::function<void(uint16_t, uint16_t)>;
    std::map<uint16_t, WriteCallback> write_callbacks_;
    
    mutable std::mutex mutex_;

public:
    // Register a callback for write operations
    void register_write_callback(uint16_t address, WriteCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        write_callbacks_[address] = callback;
    }
    
    // Set metadata for a register
    void set_metadata(RegisterType type, uint16_t address, 
                     const RegisterMetadata& metadata) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (type == RegisterType::HOLDING_REGISTER) {
            holding_metadata_[address] = metadata;
        } else if (type == RegisterType::INPUT_REGISTER) {
            input_metadata_[address] = metadata;
        }
    }
    
    // Write holding register with callback support
    bool write_holding_register(uint16_t address, uint16_t value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if read-only
        auto meta_it = holding_metadata_.find(address);
        if (meta_it != holding_metadata_.end() && meta_it->second.read_only) {
            return false;
        }
        
        holding_registers_[address] = value;
        
        // Trigger callback if registered
        auto cb_it = write_callbacks_.find(address);
        if (cb_it != write_callbacks_.end()) {
            cb_it->second(address, value);
        }
        
        return true;
    }
    
    // Read with scaling
    std::optional<float> read_holding_scaled(uint16_t address) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto val_it = holding_registers_.find(address);
        if (val_it == holding_registers_.end()) {
            return std::nullopt;
        }
        
        float scaled_value = static_cast<float>(val_it->second);
        
        auto meta_it = holding_metadata_.find(address);
        if (meta_it != holding_metadata_.end()) {
            scaled_value = (scaled_value * meta_it->second.scale_factor) 
                         + meta_it->second.offset;
        }
        
        return scaled_value;
    }
    
    // Bulk read for Modbus response
    std::vector<uint16_t> read_holding_registers(uint16_t start, uint16_t count) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<uint16_t> result;
        result.reserve(count);
        
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t addr = start + i;
            auto it = holding_registers_.find(addr);
            result.push_back(it != holding_registers_.end() ? it->second : 0);
        }
        
        return result;
    }
    
    // Coil operations with bit packing
    void write_coils(uint16_t start, const std::vector<bool>& values) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < values.size(); ++i) {
            coils_[start + i] = values[i];
        }
    }
    
    std::vector<bool> read_coils(uint16_t start, uint16_t count) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<bool> result;
        result.reserve(count);
        
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t addr = start + i;
            auto it = coils_.find(addr);
            result.push_back(it != coils_.end() ? it->second : false);
        }
        
        return result;
    }
};

// Example usage
int main() {
    ModbusRegisterDatabase db;
    
    // Set up metadata
    RegisterMetadata temp_metadata;
    temp_metadata.description = "Temperature Sensor";
    temp_metadata.units = "°C";
    temp_metadata.scale_factor = 0.1f;  // Raw value / 10
    temp_metadata.offset = -40.0f;
    db.set_metadata(RegisterType::HOLDING_REGISTER, 100, temp_metadata);
    
    // Register a callback
    db.register_write_callback(200, [](uint16_t addr, uint16_t value) {
        printf("Register %u written with value %u\n", addr, value);
    });
    
    // Write and read
    db.write_holding_register(100, 450);  // Raw value 450
    auto temp = db.read_holding_scaled(100);  // Returns 5.0°C
    
    if (temp) {
        printf("Temperature: %.1f°C\n", *temp);
    }
    
    return 0;
}
```

## Rust Implementation

### Type-Safe Register Database

```rust
use std::collections::HashMap;
use std::sync::{Arc, RwLock};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RegisterType {
    Coil,
    DiscreteInput,
    HoldingRegister,
    InputRegister,
}

#[derive(Debug, Clone)]
pub struct RegisterMetadata {
    pub description: String,
    pub units: Option<String>,
    pub scale_factor: f32,
    pub offset: f32,
    pub read_only: bool,
}

impl Default for RegisterMetadata {
    fn default() -> Self {
        Self {
            description: String::new(),
            units: None,
            scale_factor: 1.0,
            offset: 0.0,
            read_only: false,
        }
    }
}

pub type WriteCallback = Arc<dyn Fn(u16, u16) + Send + Sync>;

pub struct ModbusRegisterDatabase {
    coils: RwLock<HashMap<u16, bool>>,
    discrete_inputs: RwLock<HashMap<u16, bool>>,
    holding_registers: RwLock<HashMap<u16, u16>>,
    input_registers: RwLock<HashMap<u16, u16>>,
    
    holding_metadata: RwLock<HashMap<u16, RegisterMetadata>>,
    input_metadata: RwLock<HashMap<u16, RegisterMetadata>>,
    
    write_callbacks: RwLock<HashMap<u16, WriteCallback>>,
}

impl ModbusRegisterDatabase {
    pub fn new() -> Self {
        Self {
            coils: RwLock::new(HashMap::new()),
            discrete_inputs: RwLock::new(HashMap::new()),
            holding_registers: RwLock::new(HashMap::new()),
            input_registers: RwLock::new(HashMap::new()),
            holding_metadata: RwLock::new(HashMap::new()),
            input_metadata: RwLock::new(HashMap::new()),
            write_callbacks: RwLock::new(HashMap::new()),
        }
    }
    
    /// Write a holding register with validation and callbacks
    pub fn write_holding_register(&self, address: u16, value: u16) -> Result<(), &'static str> {
        // Check metadata for read-only flag
        {
            let metadata = self.holding_metadata.read().unwrap();
            if let Some(meta) = metadata.get(&address) {
                if meta.read_only {
                    return Err("Register is read-only");
                }
            }
        }
        
        // Write the value
        {
            let mut registers = self.holding_registers.write().unwrap();
            registers.insert(address, value);
        }
        
        // Execute callback if registered
        {
            let callbacks = self.write_callbacks.read().unwrap();
            if let Some(callback) = callbacks.get(&address) {
                callback(address, value);
            }
        }
        
        Ok(())
    }
    
    /// Read a holding register (raw value)
    pub fn read_holding_register(&self, address: u16) -> Option<u16> {
        let registers = self.holding_registers.read().unwrap();
        registers.get(&address).copied()
    }
    
    /// Read a holding register with scaling applied
    pub fn read_holding_scaled(&self, address: u16) -> Option<f32> {
        let registers = self.holding_registers.read().unwrap();
        let value = *registers.get(&address)?;
        
        let metadata = self.holding_metadata.read().unwrap();
        let scaled = if let Some(meta) = metadata.get(&address) {
            (value as f32) * meta.scale_factor + meta.offset
        } else {
            value as f32
        };
        
        Some(scaled)
    }
    
    /// Bulk read for Modbus responses
    pub fn read_holding_registers(&self, start: u16, count: u16) -> Vec<u16> {
        let registers = self.holding_registers.read().unwrap();
        (0..count)
            .map(|i| {
                let addr = start.wrapping_add(i);
                registers.get(&addr).copied().unwrap_or(0)
            })
            .collect()
    }
    
    /// Bulk write holding registers
    pub fn write_holding_registers(&self, start: u16, values: &[u16]) -> Result<(), &'static str> {
        for (i, &value) in values.iter().enumerate() {
            let addr = start.wrapping_add(i as u16);
            self.write_holding_register(addr, value)?;
        }
        Ok(())
    }
    
    /// Set metadata for a register
    pub fn set_metadata(&self, reg_type: RegisterType, address: u16, metadata: RegisterMetadata) {
        match reg_type {
            RegisterType::HoldingRegister => {
                let mut meta = self.holding_metadata.write().unwrap();
                meta.insert(address, metadata);
            }
            RegisterType::InputRegister => {
                let mut meta = self.input_metadata.write().unwrap();
                meta.insert(address, metadata);
            }
            _ => {}
        }
    }
    
    /// Register a callback for write operations
    pub fn register_write_callback<F>(&self, address: u16, callback: F)
    where
        F: Fn(u16, u16) + Send + Sync + 'static,
    {
        let mut callbacks = self.write_callbacks.write().unwrap();
        callbacks.insert(address, Arc::new(callback));
    }
    
    /// Coil operations
    pub fn write_coils(&self, start: u16, values: &[bool]) {
        let mut coils = self.coils.write().unwrap();
        for (i, &value) in values.iter().enumerate() {
            let addr = start.wrapping_add(i as u16);
            coils.insert(addr, value);
        }
    }
    
    pub fn read_coils(&self, start: u16, count: u16) -> Vec<bool> {
        let coils = self.coils.read().unwrap();
        (0..count)
            .map(|i| {
                let addr = start.wrapping_add(i);
                coils.get(&addr).copied().unwrap_or(false)
            })
            .collect()
    }
    
    /// Input register operations (read-only by design)
    pub fn set_input_register(&self, address: u16, value: u16) {
        let mut registers = self.input_registers.write().unwrap();
        registers.insert(address, value);
    }
    
    pub fn read_input_registers(&self, start: u16, count: u16) -> Vec<u16> {
        let registers = self.input_registers.read().unwrap();
        (0..count)
            .map(|i| {
                let addr = start.wrapping_add(i);
                registers.get(&addr).copied().unwrap_or(0)
            })
            .collect()
    }
}

// Example usage
fn main() {
    let db = ModbusRegisterDatabase::new();
    
    // Set up metadata for temperature sensor
    let temp_metadata = RegisterMetadata {
        description: "Temperature Sensor".to_string(),
        units: Some("°C".to_string()),
        scale_factor: 0.1,
        offset: -40.0,
        read_only: false,
    };
    db.set_metadata(RegisterType::HoldingRegister, 100, temp_metadata);
    
    // Register a callback
    db.register_write_callback(200, |addr, value| {
        println!("Register {} written with value {}", addr, value);
    });
    
    // Write and read
    db.write_holding_register(100, 450).unwrap();
    if let Some(temp) = db.read_holding_scaled(100) {
        println!("Temperature: {:.1}°C", temp);  // Prints: Temperature: 5.0°C
    }
    
    // Bulk operations
    db.write_holding_registers(1000, &[10, 20, 30, 40]).unwrap();
    let values = db.read_holding_registers(1000, 4);
    println!("Bulk read: {:?}", values);
}
```

## Summary

A well-designed Modbus Register Database is essential for efficient server implementations. Key design principles include:

**Architecture Choices**: Use dense arrays for continuous address spaces (memory-efficient, fast access) or sparse hash maps for scattered addresses (flexible, memory-efficient for sparse data).

**Thread Safety**: Implement proper locking mechanisms (mutexes in C/C++, RwLock in Rust) to handle concurrent client access safely.

**Advanced Features**: Support metadata (units, scaling, descriptions), callbacks for monitoring changes, validation rules (bounds checking, read-only enforcement), and bulk operations for performance.

**Type Safety**: Rust provides compile-time guarantees through its type system and ownership model, while C/C++ requires manual bounds checking and careful memory management.

**Performance Optimization**: Bulk read/write operations minimize lock contention, sparse storage reduces memory for large address spaces with few actual registers, and callback systems enable event-driven architectures.

A robust register database transforms raw Modbus addresses into a meaningful, maintainable data layer that supports industrial applications ranging from simple sensors to complex SCADA systems.