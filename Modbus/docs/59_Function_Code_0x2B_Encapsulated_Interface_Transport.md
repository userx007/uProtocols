# Function Code 0x2B: Encapsulated Interface Transport

## Overview

Function Code 0x2B (43 decimal) is the **Encapsulated Interface Transport** function in Modbus. It serves as a container mechanism for tunneling various protocols and accessing specialized device information through Modbus. The most common use is the **Modbus Encapsulated Interface (MEI)** for device identification, which allows standardized access to vendor information, product codes, and device metadata.

## Technical Details

### Frame Structure

**Request Format:**
```
[Device Address][0x2B][MEI Type][Additional Data...]
```

**Response Format:**
```
[Device Address][0x2B][MEI Type][Response Data...][CRC/LRC]
```

### MEI Types

The MEI Type byte determines the encapsulated function:

- **0x0E**: Read Device Identification (most common)
- **0x0D**: CANopen General Reference
- **0x0D-0x0F**: Reserved for other encapsulated interfaces

### Read Device Identification (MEI Type 0x0E)

This is the primary use case for Function 0x2B.

**Request Structure:**
```
[Address][0x2B][0x0E][Read Device ID Code][Object ID]
```

**Read Device ID Codes:**
- **0x01**: Basic identification (mandatory objects)
- **0x02**: Regular identification (basic + common objects)
- **0x03**: Extended identification (all available objects)
- **0x04**: Specific object identification

**Standard Object IDs:**
- **0x00**: VendorName
- **0x01**: ProductCode
- **0x02**: MajorMinorRevision
- **0x03**: VendorUrl
- **0x04**: ProductName
- **0x05**: ModelName
- **0x06**: UserApplicationName
- **0x80-0xFF**: Device-specific objects

**Response Structure:**
```
[Address][0x2B][0x0E][Read Device ID Code][Conformity Level][More Follows][Next Object ID][Number of Objects][Object ID][Object Length][Object Value]...
```

## C/C++ Implementation

### Basic Device Identification Request

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Modbus MEI defines
#define MODBUS_FC_ENCAPSULATED_INTERFACE 0x2B
#define MEI_TYPE_READ_DEVICE_ID 0x0E

// Read Device ID codes
#define READ_DEVICE_ID_BASIC 0x01
#define READ_DEVICE_ID_REGULAR 0x02
#define READ_DEVICE_ID_EXTENDED 0x03
#define READ_DEVICE_ID_SPECIFIC 0x04

// Standard Object IDs
#define OBJ_VENDOR_NAME 0x00
#define OBJ_PRODUCT_CODE 0x01
#define OBJ_MAJOR_MINOR_REV 0x02
#define OBJ_VENDOR_URL 0x03
#define OBJ_PRODUCT_NAME 0x04
#define OBJ_MODEL_NAME 0x05
#define OBJ_USER_APP_NAME 0x06

typedef struct {
    uint8_t device_addr;
    uint8_t function_code;
    uint8_t mei_type;
    uint8_t read_device_id;
    uint8_t object_id;
} modbus_mei_request_t;

typedef struct {
    uint8_t object_id;
    uint8_t length;
    char value[256];
} device_id_object_t;

typedef struct {
    uint8_t device_addr;
    uint8_t function_code;
    uint8_t mei_type;
    uint8_t read_device_id;
    uint8_t conformity_level;
    uint8_t more_follows;
    uint8_t next_object_id;
    uint8_t num_objects;
    device_id_object_t objects[32];
} modbus_mei_response_t;

// CRC16 calculation for Modbus RTU
uint16_t calculate_crc16(uint8_t *buffer, int length) {
    uint16_t crc = 0xFFFF;
    
    for (int i = 0; i < length; i++) {
        crc ^= buffer[i];
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

// Build MEI Read Device ID request
int build_mei_request(uint8_t *buffer, uint8_t device_addr, 
                      uint8_t read_code, uint8_t object_id) {
    int index = 0;
    
    buffer[index++] = device_addr;
    buffer[index++] = MODBUS_FC_ENCAPSULATED_INTERFACE;
    buffer[index++] = MEI_TYPE_READ_DEVICE_ID;
    buffer[index++] = read_code;
    buffer[index++] = object_id;
    
    // Add CRC for RTU
    uint16_t crc = calculate_crc16(buffer, index);
    buffer[index++] = crc & 0xFF;
    buffer[index++] = (crc >> 8) & 0xFF;
    
    return index;
}

// Parse MEI response
int parse_mei_response(uint8_t *buffer, int length, 
                       modbus_mei_response_t *response) {
    if (length < 8) return -1;
    
    int index = 0;
    response->device_addr = buffer[index++];
    response->function_code = buffer[index++];
    
    if (response->function_code != MODBUS_FC_ENCAPSULATED_INTERFACE) {
        return -1;
    }
    
    response->mei_type = buffer[index++];
    response->read_device_id = buffer[index++];
    response->conformity_level = buffer[index++];
    response->more_follows = buffer[index++];
    response->next_object_id = buffer[index++];
    response->num_objects = buffer[index++];
    
    for (int i = 0; i < response->num_objects && index < length - 2; i++) {
        response->objects[i].object_id = buffer[index++];
        response->objects[i].length = buffer[index++];
        
        int obj_len = response->objects[i].length;
        if (obj_len > 255) obj_len = 255;
        
        memcpy(response->objects[i].value, &buffer[index], obj_len);
        response->objects[i].value[obj_len] = '\0';
        index += obj_len;
    }
    
    return 0;
}

// Example usage
int main() {
    uint8_t request[256];
    uint8_t response_buffer[256];
    modbus_mei_response_t response;
    
    // Build request for basic device identification
    int req_len = build_mei_request(request, 0x01, 
                                     READ_DEVICE_ID_BASIC, 0x00);
    
    printf("MEI Request (%d bytes): ", req_len);
    for (int i = 0; i < req_len; i++) {
        printf("%02X ", request[i]);
    }
    printf("\n\n");
    
    // Simulate response (normally received from device)
    uint8_t simulated_response[] = {
        0x01, 0x2B, 0x0E, 0x01, 0x01, 0x00, 0x00, 0x03,
        0x00, 0x0A, 'A','C','M','E',' ','C','o','r','p','.', 
        0x01, 0x08, 'P','R','D','-','1','2','3','4',
        0x02, 0x05, 'V','1','.','0','0',
        0x00, 0x00  // CRC placeholder
    };
    
    // Parse the response
    if (parse_mei_response(simulated_response, 
                          sizeof(simulated_response), &response) == 0) {
        printf("Device Identification Response:\n");
        printf("  Conformity Level: 0x%02X\n", response.conformity_level);
        printf("  Number of Objects: %d\n", response.num_objects);
        
        for (int i = 0; i < response.num_objects; i++) {
            const char *obj_name = "Unknown";
            switch (response.objects[i].object_id) {
                case OBJ_VENDOR_NAME: obj_name = "Vendor Name"; break;
                case OBJ_PRODUCT_CODE: obj_name = "Product Code"; break;
                case OBJ_MAJOR_MINOR_REV: obj_name = "Revision"; break;
                case OBJ_VENDOR_URL: obj_name = "Vendor URL"; break;
                case OBJ_PRODUCT_NAME: obj_name = "Product Name"; break;
            }
            
            printf("  %s (0x%02X): %s\n", 
                   obj_name, 
                   response.objects[i].object_id,
                   response.objects[i].value);
        }
    }
    
    return 0;
}
```

### Advanced C++ Implementation with Class Structure

```cpp
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <map>

class ModbusMEI {
public:
    enum class ReadDeviceIDCode : uint8_t {
        Basic = 0x01,
        Regular = 0x02,
        Extended = 0x03,
        Specific = 0x04
    };
    
    enum class ObjectID : uint8_t {
        VendorName = 0x00,
        ProductCode = 0x01,
        MajorMinorRevision = 0x02,
        VendorUrl = 0x03,
        ProductName = 0x04,
        ModelName = 0x05,
        UserApplicationName = 0x06
    };
    
    struct DeviceObject {
        uint8_t id;
        std::string value;
    };
    
private:
    static constexpr uint8_t FUNCTION_CODE = 0x2B;
    static constexpr uint8_t MEI_TYPE_DEVICE_ID = 0x0E;
    
    uint8_t deviceAddress_;
    
    static uint16_t calculateCRC16(const std::vector<uint8_t>& data) {
        uint16_t crc = 0xFFFF;
        
        for (uint8_t byte : data) {
            crc ^= byte;
            for (int i = 0; i < 8; i++) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }
    
public:
    ModbusMEI(uint8_t deviceAddress) : deviceAddress_(deviceAddress) {}
    
    std::vector<uint8_t> buildReadDeviceIDRequest(
        ReadDeviceIDCode readCode, 
        uint8_t objectId = 0x00) {
        
        std::vector<uint8_t> request;
        request.push_back(deviceAddress_);
        request.push_back(FUNCTION_CODE);
        request.push_back(MEI_TYPE_DEVICE_ID);
        request.push_back(static_cast<uint8_t>(readCode));
        request.push_back(objectId);
        
        uint16_t crc = calculateCRC16(request);
        request.push_back(crc & 0xFF);
        request.push_back((crc >> 8) & 0xFF);
        
        return request;
    }
    
    std::map<uint8_t, std::string> parseResponse(
        const std::vector<uint8_t>& response) {
        
        std::map<uint8_t, std::string> objects;
        
        if (response.size() < 8) {
            throw std::runtime_error("Response too short");
        }
        
        size_t index = 0;
        uint8_t addr = response[index++];
        uint8_t func = response[index++];
        
        if (func != FUNCTION_CODE) {
            throw std::runtime_error("Invalid function code");
        }
        
        uint8_t meiType = response[index++];
        uint8_t readDeviceID = response[index++];
        uint8_t conformityLevel = response[index++];
        uint8_t moreFollows = response[index++];
        uint8_t nextObjectID = response[index++];
        uint8_t numObjects = response[index++];
        
        for (int i = 0; i < numObjects && index < response.size() - 2; i++) {
            uint8_t objId = response[index++];
            uint8_t objLen = response[index++];
            
            std::string value(response.begin() + index, 
                            response.begin() + index + objLen);
            objects[objId] = value;
            index += objLen;
        }
        
        return objects;
    }
    
    static std::string getObjectName(ObjectID id) {
        static const std::map<ObjectID, std::string> names = {
            {ObjectID::VendorName, "Vendor Name"},
            {ObjectID::ProductCode, "Product Code"},
            {ObjectID::MajorMinorRevision, "Revision"},
            {ObjectID::VendorUrl, "Vendor URL"},
            {ObjectID::ProductName, "Product Name"},
            {ObjectID::ModelName, "Model Name"},
            {ObjectID::UserApplicationName, "User Application Name"}
        };
        
        auto it = names.find(id);
        return (it != names.end()) ? it->second : "Unknown";
    }
};

// Example usage
int main() {
    ModbusMEI mei(0x01);
    
    // Build request
    auto request = mei.buildReadDeviceIDRequest(
        ModbusMEI::ReadDeviceIDCode::Basic);
    
    std::cout << "Request: ";
    for (auto byte : request) {
        printf("%02X ", byte);
    }
    std::cout << "\n\n";
    
    // Simulate response
    std::vector<uint8_t> response = {
        0x01, 0x2B, 0x0E, 0x01, 0x01, 0x00, 0x00, 0x03,
        0x00, 0x0A, 'A','C','M','E',' ','C','o','r','p','.',
        0x01, 0x08, 'P','R','D','-','1','2','3','4',
        0x02, 0x05, 'V','1','.','0','0'
    };
    
    try {
        auto objects = mei.parseResponse(response);
        
        std::cout << "Device Identification:\n";
        for (const auto& [id, value] : objects) {
            std::cout << "  " << ModbusMEI::getObjectName(
                static_cast<ModbusMEI::ObjectID>(id))
                     << " (0x" << std::hex << (int)id << "): " 
                     << value << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::collections::HashMap;

const MODBUS_FC_ENCAPSULATED: u8 = 0x2B;
const MEI_TYPE_READ_DEVICE_ID: u8 = 0x0E;

#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum ReadDeviceIDCode {
    Basic = 0x01,
    Regular = 0x02,
    Extended = 0x03,
    Specific = 0x04,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum ObjectID {
    VendorName = 0x00,
    ProductCode = 0x01,
    MajorMinorRevision = 0x02,
    VendorUrl = 0x03,
    ProductName = 0x04,
    ModelName = 0x05,
    UserApplicationName = 0x06,
}

impl ObjectID {
    pub fn name(&self) -> &'static str {
        match self {
            ObjectID::VendorName => "Vendor Name",
            ObjectID::ProductCode => "Product Code",
            ObjectID::MajorMinorRevision => "Revision",
            ObjectID::VendorUrl => "Vendor URL",
            ObjectID::ProductName => "Product Name",
            ObjectID::ModelName => "Model Name",
            ObjectID::UserApplicationName => "User Application Name",
        }
    }
    
    pub fn from_u8(value: u8) -> Option<Self> {
        match value {
            0x00 => Some(ObjectID::VendorName),
            0x01 => Some(ObjectID::ProductCode),
            0x02 => Some(ObjectID::MajorMinorRevision),
            0x03 => Some(ObjectID::VendorUrl),
            0x04 => Some(ObjectID::ProductName),
            0x05 => Some(ObjectID::ModelName),
            0x06 => Some(ObjectID::UserApplicationName),
            _ => None,
        }
    }
}

#[derive(Debug)]
pub struct DeviceIdentification {
    pub conformity_level: u8,
    pub more_follows: bool,
    pub next_object_id: u8,
    pub objects: HashMap<u8, String>,
}

pub struct ModbusMEI {
    device_address: u8,
}

impl ModbusMEI {
    pub fn new(device_address: u8) -> Self {
        Self { device_address }
    }
    
    fn calculate_crc16(data: &[u8]) -> u16 {
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
    
    pub fn build_read_device_id_request(
        &self,
        read_code: ReadDeviceIDCode,
        object_id: u8,
    ) -> Vec<u8> {
        let mut request = Vec::new();
        
        request.push(self.device_address);
        request.push(MODBUS_FC_ENCAPSULATED);
        request.push(MEI_TYPE_READ_DEVICE_ID);
        request.push(read_code as u8);
        request.push(object_id);
        
        let crc = Self::calculate_crc16(&request);
        request.push((crc & 0xFF) as u8);
        request.push(((crc >> 8) & 0xFF) as u8);
        
        request
    }
    
    pub fn parse_response(
        &self,
        response: &[u8],
    ) -> Result<DeviceIdentification, String> {
        if response.len() < 8 {
            return Err("Response too short".to_string());
        }
        
        let mut index = 0;
        let addr = response[index];
        index += 1;
        let func = response[index];
        index += 1;
        
        if func != MODBUS_FC_ENCAPSULATED {
            return Err(format!("Invalid function code: 0x{:02X}", func));
        }
        
        let mei_type = response[index];
        index += 1;
        let read_device_id = response[index];
        index += 1;
        let conformity_level = response[index];
        index += 1;
        let more_follows = response[index];
        index += 1;
        let next_object_id = response[index];
        index += 1;
        let num_objects = response[index];
        index += 1;
        
        let mut objects = HashMap::new();
        
        for _ in 0..num_objects {
            if index + 2 > response.len() {
                break;
            }
            
            let obj_id = response[index];
            index += 1;
            let obj_len = response[index] as usize;
            index += 1;
            
            if index + obj_len > response.len() {
                break;
            }
            
            let value = String::from_utf8_lossy(
                &response[index..index + obj_len]
            ).to_string();
            
            objects.insert(obj_id, value);
            index += obj_len;
        }
        
        Ok(DeviceIdentification {
            conformity_level,
            more_follows: more_follows != 0,
            next_object_id,
            objects,
        })
    }
}

fn main() {
    let mei = ModbusMEI::new(0x01);
    
    // Build request for basic device identification
    let request = mei.build_read_device_id_request(
        ReadDeviceIDCode::Basic,
        0x00
    );
    
    print!("Request: ");
    for byte in &request {
        print!("{:02X} ", byte);
    }
    println!("\n");
    
    // Simulate response
    let response: Vec<u8> = vec![
        0x01, 0x2B, 0x0E, 0x01, 0x01, 0x00, 0x00, 0x03,
        0x00, 0x0A, b'A', b'C', b'M', b'E', b' ', b'C', b'o', b'r', b'p', b'.',
        0x01, 0x08, b'P', b'R', b'D', b'-', b'1', b'2', b'3', b'4',
        0x02, 0x05, b'V', b'1', b'.', b'0', b'0',
    ];
    
    match mei.parse_response(&response) {
        Ok(device_id) => {
            println!("Device Identification:");
            println!("  Conformity Level: 0x{:02X}", device_id.conformity_level);
            println!("  More Follows: {}", device_id.more_follows);
            println!("  Number of Objects: {}\n", device_id.objects.len());
            
            for (obj_id, value) in &device_id.objects {
                let name = ObjectID::from_u8(*obj_id)
                    .map(|o| o.name())
                    .unwrap_or("Unknown");
                    
                println!("  {} (0x{:02X}): {}", name, obj_id, value);
            }
        }
        Err(e) => eprintln!("Error parsing response: {}", e),
    }
}
```

### Async Rust Implementation with Tokio

```rust
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use std::io;

pub struct AsyncModbusMEI {
    stream: TcpStream,
    device_address: u8,
}

impl AsyncModbusMEI {
    pub async fn connect(
        addr: &str,
        device_address: u8,
    ) -> io::Result<Self> {
        let stream = TcpStream::connect(addr).await?;
        Ok(Self {
            stream,
            device_address,
        })
    }
    
    pub async fn read_device_identification(
        &mut self,
        read_code: ReadDeviceIDCode,
    ) -> io::Result<DeviceIdentification> {
        let mei = ModbusMEI::new(self.device_address);
        let request = mei.build_read_device_id_request(read_code, 0x00);
        
        // Send request
        self.stream.write_all(&request).await?;
        
        // Read response
        let mut response = vec![0u8; 256];
        let n = self.stream.read(&mut response).await?;
        response.truncate(n);
        
        mei.parse_response(&response)
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))
    }
}

// Example async usage
#[tokio::main]
async fn main() -> io::Result<()> {
    let mut mei = AsyncModbusMEI::connect("192.168.1.100:502", 0x01).await?;
    
    let device_id = mei.read_device_identification(ReadDeviceIDCode::Basic).await?;
    
    println!("Device Information:");
    for (obj_id, value) in &device_id.objects {
        println!("  0x{:02X}: {}", obj_id, value);
    }
    
    Ok(())
}
```

## Summary

**Function Code 0x2B (Encapsulated Interface Transport)** provides a standardized mechanism for accessing device metadata and tunneling other protocols through Modbus. The primary use case is **Read Device Identification (MEI Type 0x0E)**, which allows clients to retrieve vendor information, product codes, firmware versions, and other identifying data from Modbus devices.

**Key Points:**
- Acts as a container for multiple encapsulated interfaces (MEI types)
- Most commonly used for device identification (MEI 0x0E)
- Supports three conformity levels: Basic, Regular, and Extended
- Provides standardized object IDs for common device attributes
- Enables discovery and inventory of Modbus devices without proprietary protocols
- Useful for device commissioning, diagnostics, and asset management

The implementations shown demonstrate building MEI requests, parsing responses, and extracting device information in both synchronous and asynchronous contexts across C/C++ and Rust, providing a solid foundation for integrating device identification capabilities into Modbus applications.