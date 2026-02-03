# FMS Virtual Field Device

## Detailed Description

### Overview

**Fieldbus Message Specification (FMS)** Virtual Field Devices (VFDs) represent a sophisticated abstraction layer in Profibus FMS networks. A VFD is a logical representation of a physical device that provides standardized access to device data and functions through an object-oriented model. This concept enables uniform communication regardless of the underlying hardware implementation.

### Core Concepts

**Virtual Field Device Architecture:**

A VFD consists of three main components:

1. **Object Dictionary (OD)**: A structured database containing all accessible objects within the device, organized hierarchically with unique identifiers
2. **Communication Objects**: Standardized data structures representing device variables, parameters, and functions
3. **FMS Services**: A set of communication services for reading, writing, and managing objects

**Object Dictionary Structure:**

The OD is organized using a hierarchical addressing scheme:
- **Vendor-specific objects**: Custom objects defined by device manufacturers
- **Standardized objects**: Common objects defined by Profibus specifications
- **Dynamic objects**: Objects that can be created or deleted during runtime

Each object has attributes including:
- Object index (unique identifier)
- Data type
- Access rights (read, write, execute)
- Object description
- Value and status

### Communication Services

FMS provides services for VFD interaction:

- **Read**: Retrieve object values
- **Write**: Modify object values
- **Information Report**: Unsolicited event notifications
- **Identify**: Query device capabilities
- **GetOV (Get Object Values)**: Batch read operations
- **PutOV (Put Object Values)**: Batch write operations

## Programming Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// FMS Object Dictionary Entry
typedef struct {
    uint16_t index;           // Object index
    uint8_t  data_type;       // Data type (BOOLEAN, INTEGER, etc.)
    uint8_t  access_rights;   // Read/Write permissions
    uint16_t length;          // Data length in bytes
    void*    data_ptr;        // Pointer to actual data
    char     description[64]; // Object description
} fms_od_entry_t;

// Access rights definitions
#define FMS_ACCESS_READ   0x01
#define FMS_ACCESS_WRITE  0x02
#define FMS_ACCESS_EXEC   0x04

// Data type definitions
#define FMS_TYPE_BOOLEAN  0x01
#define FMS_TYPE_INTEGER  0x02
#define FMS_TYPE_UNSIGNED 0x03
#define FMS_TYPE_FLOAT    0x04
#define FMS_TYPE_STRING   0x05

// FMS VFD Context
typedef struct {
    fms_od_entry_t* od_table;
    uint16_t        od_size;
    uint8_t         device_status;
    char            device_name[32];
} fms_vfd_t;

// Initialize Virtual Field Device
bool fms_vfd_init(fms_vfd_t* vfd, const char* name, uint16_t od_capacity) {
    if (!vfd || !name) return false;
    
    vfd->od_table = (fms_od_entry_t*)malloc(od_capacity * sizeof(fms_od_entry_t));
    if (!vfd->od_table) return false;
    
    vfd->od_size = 0;
    vfd->device_status = 0;
    strncpy(vfd->device_name, name, sizeof(vfd->device_name) - 1);
    
    return true;
}

// Add object to Object Dictionary
bool fms_od_add_object(fms_vfd_t* vfd, uint16_t index, uint8_t data_type,
                       uint8_t access, void* data_ptr, uint16_t length,
                       const char* desc) {
    if (!vfd || !data_ptr) return false;
    
    fms_od_entry_t* entry = &vfd->od_table[vfd->od_size];
    entry->index = index;
    entry->data_type = data_type;
    entry->access_rights = access;
    entry->length = length;
    entry->data_ptr = data_ptr;
    strncpy(entry->description, desc, sizeof(entry->description) - 1);
    
    vfd->od_size++;
    return true;
}

// Find object in OD by index
fms_od_entry_t* fms_od_find_object(fms_vfd_t* vfd, uint16_t index) {
    for (uint16_t i = 0; i < vfd->od_size; i++) {
        if (vfd->od_table[i].index == index) {
            return &vfd->od_table[i];
        }
    }
    return NULL;
}

// FMS Read Service
int fms_read(fms_vfd_t* vfd, uint16_t index, void* buffer, uint16_t* length) {
    fms_od_entry_t* obj = fms_od_find_object(vfd, index);
    
    if (!obj) return -1; // Object not found
    if (!(obj->access_rights & FMS_ACCESS_READ)) return -2; // No read access
    
    if (*length < obj->length) {
        *length = obj->length;
        return -3; // Buffer too small
    }
    
    memcpy(buffer, obj->data_ptr, obj->length);
    *length = obj->length;
    return 0; // Success
}

// FMS Write Service
int fms_write(fms_vfd_t* vfd, uint16_t index, const void* data, uint16_t length) {
    fms_od_entry_t* obj = fms_od_find_object(vfd, index);
    
    if (!obj) return -1; // Object not found
    if (!(obj->access_rights & FMS_ACCESS_WRITE)) return -2; // No write access
    if (length != obj->length) return -3; // Length mismatch
    
    memcpy(obj->data_ptr, data, length);
    return 0; // Success
}

// Example usage
int main() {
    fms_vfd_t vfd;
    fms_vfd_init(&vfd, "TemperatureSensor", 100);
    
    // Device variables
    float temperature = 25.5f;
    uint16_t sensor_status = 0x0001;
    char sensor_name[32] = "PT100-Sensor-01";
    
    // Add objects to OD
    fms_od_add_object(&vfd, 0x1000, FMS_TYPE_FLOAT, 
                      FMS_ACCESS_READ, &temperature, 
                      sizeof(temperature), "Current Temperature");
    
    fms_od_add_object(&vfd, 0x1001, FMS_TYPE_UNSIGNED,
                      FMS_ACCESS_READ | FMS_ACCESS_WRITE, &sensor_status,
                      sizeof(sensor_status), "Sensor Status");
    
    fms_od_add_object(&vfd, 0x1002, FMS_TYPE_STRING,
                      FMS_ACCESS_READ, sensor_name,
                      strlen(sensor_name) + 1, "Sensor Name");
    
    // Read temperature
    float read_temp;
    uint16_t read_len = sizeof(read_temp);
    if (fms_read(&vfd, 0x1000, &read_temp, &read_len) == 0) {
        printf("Temperature: %.2f°C\n", read_temp);
    }
    
    // Write status
    uint16_t new_status = 0x0002;
    if (fms_write(&vfd, 0x1001, &new_status, sizeof(new_status)) == 0) {
        printf("Status updated successfully\n");
    }
    
    free(vfd.od_table);
    return 0;
}
```

### C++ Object-Oriented Implementation

```cpp
#include <iostream>
#include <map>
#include <memory>
#include <variant>
#include <string>
#include <vector>

namespace profibus {

// FMS Data types using std::variant
using FmsValue = std::variant<bool, int32_t, uint32_t, float, std::string>;

enum class AccessRight : uint8_t {
    Read = 0x01,
    Write = 0x02,
    ReadWrite = 0x03,
    Execute = 0x04
};

// Object Dictionary Entry
class OdEntry {
private:
    uint16_t index_;
    FmsValue value_;
    AccessRight access_;
    std::string description_;

public:
    OdEntry(uint16_t index, FmsValue value, AccessRight access, 
            const std::string& desc)
        : index_(index), value_(value), access_(access), description_(desc) {}
    
    uint16_t getIndex() const { return index_; }
    const std::string& getDescription() const { return description_; }
    
    bool canRead() const {
        return static_cast<uint8_t>(access_) & static_cast<uint8_t>(AccessRight::Read);
    }
    
    bool canWrite() const {
        return static_cast<uint8_t>(access_) & static_cast<uint8_t>(AccessRight::Write);
    }
    
    template<typename T>
    bool read(T& value) const {
        if (!canRead()) return false;
        if (auto* val = std::get_if<T>(&value_)) {
            value = *val;
            return true;
        }
        return false;
    }
    
    template<typename T>
    bool write(const T& value) {
        if (!canWrite()) return false;
        if (std::holds_alternative<T>(value_)) {
            value_ = value;
            return true;
        }
        return false;
    }
};

// Virtual Field Device
class VirtualFieldDevice {
private:
    std::string name_;
    std::map<uint16_t, std::unique_ptr<OdEntry>> object_dictionary_;
    uint8_t status_;

public:
    VirtualFieldDevice(const std::string& name) 
        : name_(name), status_(0) {}
    
    bool addObject(uint16_t index, FmsValue initial_value, 
                   AccessRight access, const std::string& description) {
        if (object_dictionary_.find(index) != object_dictionary_.end()) {
            return false; // Object already exists
        }
        
        object_dictionary_[index] = std::make_unique<OdEntry>(
            index, initial_value, access, description
        );
        return true;
    }
    
    template<typename T>
    bool readObject(uint16_t index, T& value) const {
        auto it = object_dictionary_.find(index);
        if (it == object_dictionary_.end()) return false;
        return it->second->read(value);
    }
    
    template<typename T>
    bool writeObject(uint16_t index, const T& value) {
        auto it = object_dictionary_.find(index);
        if (it == object_dictionary_.end()) return false;
        return it->second->write(value);
    }
    
    std::vector<uint16_t> listObjects() const {
        std::vector<uint16_t> indices;
        for (const auto& [index, entry] : object_dictionary_) {
            indices.push_back(index);
        }
        return indices;
    }
    
    void printObjectDictionary() const {
        std::cout << "VFD: " << name_ << "\n";
        std::cout << "Object Dictionary:\n";
        for (const auto& [index, entry] : object_dictionary_) {
            std::cout << "  [0x" << std::hex << index << std::dec << "] " 
                      << entry->getDescription() << "\n";
        }
    }
};

} // namespace profibus

// Example usage
int main() {
    using namespace profibus;
    
    VirtualFieldDevice vfd("PressureSensor");
    
    // Add objects
    vfd.addObject(0x2000, 101.3f, AccessRight::Read, "Pressure Value [kPa]");
    vfd.addObject(0x2001, uint32_t(0), AccessRight::ReadWrite, "Alarm Threshold");
    vfd.addObject(0x2002, std::string("PS-2024-001"), AccessRight::Read, "Serial Number");
    vfd.addObject(0x2003, true, AccessRight::ReadWrite, "Sensor Enabled");
    
    // Read operations
    float pressure;
    if (vfd.readObject(0x2000, pressure)) {
        std::cout << "Pressure: " << pressure << " kPa\n";
    }
    
    std::string serial;
    if (vfd.readObject(0x2002, serial)) {
        std::cout << "Serial: " << serial << "\n";
    }
    
    // Write operations
    uint32_t threshold = 150;
    if (vfd.writeObject(0x2001, threshold)) {
        std::cout << "Threshold updated\n";
    }
    
    vfd.printObjectDictionary();
    
    return 0;
}
```

### Rust Implementation

```rust
use std::collections::HashMap;
use std::fmt;

// FMS Value types
#[derive(Debug, Clone)]
pub enum FmsValue {
    Boolean(bool),
    Integer(i32),
    Unsigned(u32),
    Float(f32),
    String(String),
}

// Access rights
#[derive(Debug, Clone, Copy)]
pub struct AccessRight {
    flags: u8,
}

impl AccessRight {
    pub const READ: u8 = 0x01;
    pub const WRITE: u8 = 0x02;
    pub const EXECUTE: u8 = 0x04;
    
    pub fn new(flags: u8) -> Self {
        Self { flags }
    }
    
    pub fn can_read(&self) -> bool {
        self.flags & Self::READ != 0
    }
    
    pub fn can_write(&self) -> bool {
        self.flags & Self::WRITE != 0
    }
    
    pub fn read_write() -> Self {
        Self::new(Self::READ | Self::WRITE)
    }
    
    pub fn read_only() -> Self {
        Self::new(Self::READ)
    }
}

// Object Dictionary Entry
#[derive(Debug, Clone)]
pub struct OdEntry {
    index: u16,
    value: FmsValue,
    access: AccessRight,
    description: String,
}

impl OdEntry {
    pub fn new(index: u16, value: FmsValue, access: AccessRight, description: String) -> Self {
        Self {
            index,
            value,
            access,
            description,
        }
    }
    
    pub fn read(&self) -> Result<FmsValue, &'static str> {
        if !self.access.can_read() {
            return Err("No read access");
        }
        Ok(self.value.clone())
    }
    
    pub fn write(&mut self, value: FmsValue) -> Result<(), &'static str> {
        if !self.access.can_write() {
            return Err("No write access");
        }
        
        // Type checking
        if std::mem::discriminant(&self.value) != std::mem::discriminant(&value) {
            return Err("Type mismatch");
        }
        
        self.value = value;
        Ok(())
    }
}

// Virtual Field Device
pub struct VirtualFieldDevice {
    name: String,
    object_dictionary: HashMap<u16, OdEntry>,
    status: u8,
}

impl VirtualFieldDevice {
    pub fn new(name: String) -> Self {
        Self {
            name,
            object_dictionary: HashMap::new(),
            status: 0,
        }
    }
    
    pub fn add_object(
        &mut self,
        index: u16,
        value: FmsValue,
        access: AccessRight,
        description: String,
    ) -> Result<(), &'static str> {
        if self.object_dictionary.contains_key(&index) {
            return Err("Object already exists");
        }
        
        self.object_dictionary.insert(
            index,
            OdEntry::new(index, value, access, description),
        );
        Ok(())
    }
    
    pub fn read_object(&self, index: u16) -> Result<FmsValue, &'static str> {
        self.object_dictionary
            .get(&index)
            .ok_or("Object not found")?
            .read()
    }
    
    pub fn write_object(&mut self, index: u16, value: FmsValue) -> Result<(), &'static str> {
        self.object_dictionary
            .get_mut(&index)
            .ok_or("Object not found")?
            .write(value)
    }
    
    pub fn list_objects(&self) -> Vec<u16> {
        let mut indices: Vec<u16> = self.object_dictionary.keys().copied().collect();
        indices.sort();
        indices
    }
    
    pub fn print_od(&self) {
        println!("VFD: {}", self.name);
        println!("Object Dictionary:");
        
        let mut indices = self.list_objects();
        for index in indices {
            if let Some(entry) = self.object_dictionary.get(&index) {
                println!("  [0x{:04X}] {}", index, entry.description);
            }
        }
    }
}

impl fmt::Display for FmsValue {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            FmsValue::Boolean(v) => write!(f, "{}", v),
            FmsValue::Integer(v) => write!(f, "{}", v),
            FmsValue::Unsigned(v) => write!(f, "{}", v),
            FmsValue::Float(v) => write!(f, "{:.2}", v),
            FmsValue::String(v) => write!(f, "{}", v),
        }
    }
}

// Example usage
fn main() {
    let mut vfd = VirtualFieldDevice::new("FlowSensor".to_string());
    
    // Add objects to dictionary
    vfd.add_object(
        0x3000,
        FmsValue::Float(45.7),
        AccessRight::read_only(),
        "Flow Rate [L/min]".to_string(),
    ).unwrap();
    
    vfd.add_object(
        0x3001,
        FmsValue::Unsigned(100),
        AccessRight::read_write(),
        "Max Flow Alarm".to_string(),
    ).unwrap();
    
    vfd.add_object(
        0x3002,
        FmsValue::Boolean(true),
        AccessRight::read_write(),
        "Sensor Active".to_string(),
    ).unwrap();
    
    vfd.add_object(
        0x3003,
        FmsValue::String("FS-2025-042".to_string()),
        AccessRight::read_only(),
        "Device ID".to_string(),
    ).unwrap();
    
    // Read operations
    match vfd.read_object(0x3000) {
        Ok(value) => println!("Flow Rate: {}", value),
        Err(e) => println!("Error: {}", e),
    }
    
    match vfd.read_object(0x3003) {
        Ok(value) => println!("Device ID: {}", value),
        Err(e) => println!("Error: {}", e),
    }
    
    // Write operation
    match vfd.write_object(0x3001, FmsValue::Unsigned(120)) {
        Ok(_) => println!("Max flow alarm updated"),
        Err(e) => println!("Error: {}", e),
    }
    
    // Try to write to read-only object (should fail)
    match vfd.write_object(0x3000, FmsValue::Float(50.0)) {
        Ok(_) => println!("Write succeeded"),
        Err(e) => println!("Write failed: {}", e),
    }
    
    // Print OD
    vfd.print_od();
}
```

## Summary

**FMS Virtual Field Devices** provide a standardized, object-oriented abstraction for accessing device data in Profibus FMS networks. The VFD model uses an **Object Dictionary** as a central registry of all accessible device parameters, variables, and functions, each identified by a unique index and characterized by data type and access rights.

Key features include:
- **Uniform access** to diverse device types through standardized FMS services (Read, Write, GetOV, PutOV)
- **Hierarchical organization** of device data in the Object Dictionary
- **Access control** through configurable read/write/execute permissions
- **Type safety** with defined data types for each object

The programming examples demonstrate implementing VFD functionality in three languages: C provides low-level control suitable for embedded systems, C++ offers object-oriented abstractions with type safety through variants, and Rust ensures memory safety and type correctness through its enum and Result types. All implementations share the core concepts of an indexed object dictionary with access-controlled entries, enabling interoperable device communication in industrial automation networks.