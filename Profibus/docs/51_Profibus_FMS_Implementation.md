# Profibus FMS Implementation

## Detailed Description

**Profibus FMS (Fieldbus Message Specification)** is one of the three main Profibus protocol variants, alongside DP (Decentralized Periphery) and PA (Process Automation). FMS is designed for complex peer-to-peer communication between intelligent field devices, controllers, and engineering workstations at the cell and field levels of factory automation.

### Key Characteristics

**Communication Model:**
- **Peer-to-peer communication**: Unlike the master-slave model of Profibus DP, FMS supports equal communication rights between devices
- **Client-server relationships**: Devices can act as both clients and servers simultaneously
- **Acyclic communication**: Message-oriented rather than cyclic data exchange
- **Object-oriented**: Uses virtual field devices (VFDs) and communication objects

**FMS Services:**
FMS provides a rich set of application layer services including:
- **Variable Access**: Read, Write, InformationReport
- **Domain Management**: Upload, Download, DeleteDomain
- **Program Invocation**: Start, Stop, Resume, Reset
- **Event Management**: EventNotification, AcknowledgeEventNotification
- **VFD Support**: Identify, Status, GetOV (Object Dictionary)
- **Context Management**: Initiate, Abort

**Use Cases:**
- Complex automation systems requiring flexible communication
- Integration of heterogeneous devices from different manufacturers
- Engineering and diagnostic applications
- Manufacturing execution systems (MES) integration
- Data logging and recipe management

### Architecture

FMS operates on top of the Profibus application layer and uses:
- **Virtual Field Device (VFD)**: Logical representation of device functionality
- **Communication Objects**: Named data objects accessible via FMS services
- **Object Dictionary (OD)**: Directory of all accessible objects
- **Communication Relationships**: Defined connections between devices

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// FMS Protocol Data Unit Structure
typedef struct {
    uint8_t pdu_type;
    uint8_t service;
    uint16_t invoke_id;
    uint8_t *data;
    uint16_t data_length;
} fms_pdu_t;

// FMS Service Codes
#define FMS_INITIATE           0x00
#define FMS_READ               0x04
#define FMS_WRITE              0x05
#define FMS_INFORMATION_REPORT 0x06
#define FMS_GET_OD             0x0C
#define FMS_IDENTIFY           0x0E
#define FMS_STATUS             0x0D

// FMS Object Types
typedef enum {
    FMS_OBJ_SIMPLE_VAR,
    FMS_OBJ_ARRAY,
    FMS_OBJ_RECORD,
    FMS_OBJ_DOMAIN
} fms_object_type_t;

// FMS Communication Object
typedef struct {
    char name[32];
    fms_object_type_t type;
    uint16_t index;
    void *data;
    uint16_t size;
    bool readable;
    bool writable;
} fms_object_t;

// FMS Virtual Field Device
typedef struct {
    uint8_t device_address;
    char vendor_name[32];
    char model_name[32];
    uint16_t revision;
    fms_object_t *objects;
    uint16_t object_count;
    uint16_t next_invoke_id;
} fms_vfd_t;

// Initialize FMS Virtual Field Device
void fms_vfd_init(fms_vfd_t *vfd, uint8_t address, const char *vendor, 
                  const char *model) {
    vfd->device_address = address;
    strncpy(vfd->vendor_name, vendor, sizeof(vfd->vendor_name) - 1);
    strncpy(vfd->model_name, model, sizeof(vfd->model_name) - 1);
    vfd->revision = 0x0100;
    vfd->object_count = 0;
    vfd->next_invoke_id = 1;
}

// Register FMS Object in Object Dictionary
int fms_register_object(fms_vfd_t *vfd, const char *name, 
                        fms_object_type_t type, void *data, 
                        uint16_t size, bool readable, bool writable) {
    if (vfd->object_count >= 256) {
        return -1; // Object dictionary full
    }
    
    fms_object_t *obj = &vfd->objects[vfd->object_count];
    strncpy(obj->name, name, sizeof(obj->name) - 1);
    obj->type = type;
    obj->index = vfd->object_count;
    obj->data = data;
    obj->size = size;
    obj->readable = readable;
    obj->writable = writable;
    
    vfd->object_count++;
    return obj->index;
}

// Build FMS Read Request
int fms_build_read_request(fms_vfd_t *vfd, const char *object_name, 
                           uint8_t *buffer, uint16_t buffer_size) {
    // Find object by name
    fms_object_t *obj = NULL;
    for (int i = 0; i < vfd->object_count; i++) {
        if (strcmp(vfd->objects[i].name, object_name) == 0) {
            obj = &vfd->objects[i];
            break;
        }
    }
    
    if (!obj || !obj->readable) {
        return -1;
    }
    
    // Build FMS PDU
    uint16_t offset = 0;
    buffer[offset++] = 0x40; // PDU Type: Request
    buffer[offset++] = FMS_READ;
    buffer[offset++] = (vfd->next_invoke_id >> 8) & 0xFF;
    buffer[offset++] = vfd->next_invoke_id & 0xFF;
    vfd->next_invoke_id++;
    
    // Add object index
    buffer[offset++] = (obj->index >> 8) & 0xFF;
    buffer[offset++] = obj->index & 0xFF;
    
    return offset;
}

// Process FMS Read Response
int fms_process_read_response(fms_vfd_t *vfd, uint8_t *response, 
                               uint16_t response_len, void *dest, 
                               uint16_t dest_size) {
    if (response_len < 6) {
        return -1; // Invalid response
    }
    
    uint8_t pdu_type = response[0];
    uint8_t service = response[1];
    uint16_t invoke_id = (response[2] << 8) | response[3];
    
    if (pdu_type != 0x41 || service != (FMS_READ | 0x80)) {
        return -1; // Not a read response
    }
    
    uint16_t data_len = (response[4] << 8) | response[5];
    if (data_len > dest_size || (data_len + 6) > response_len) {
        return -1; // Data too large
    }
    
    memcpy(dest, &response[6], data_len);
    return data_len;
}

// Build FMS Write Request
int fms_build_write_request(fms_vfd_t *vfd, const char *object_name,
                            void *data, uint16_t data_size,
                            uint8_t *buffer, uint16_t buffer_size) {
    // Find object by name
    fms_object_t *obj = NULL;
    for (int i = 0; i < vfd->object_count; i++) {
        if (strcmp(vfd->objects[i].name, object_name) == 0) {
            obj = &vfd->objects[i];
            break;
        }
    }
    
    if (!obj || !obj->writable) {
        return -1;
    }
    
    if (data_size + 8 > buffer_size) {
        return -1; // Buffer too small
    }
    
    // Build FMS PDU
    uint16_t offset = 0;
    buffer[offset++] = 0x40; // PDU Type: Request
    buffer[offset++] = FMS_WRITE;
    buffer[offset++] = (vfd->next_invoke_id >> 8) & 0xFF;
    buffer[offset++] = vfd->next_invoke_id & 0xFF;
    vfd->next_invoke_id++;
    
    // Add object index
    buffer[offset++] = (obj->index >> 8) & 0xFF;
    buffer[offset++] = obj->index & 0xFF;
    
    // Add data length
    buffer[offset++] = (data_size >> 8) & 0xFF;
    buffer[offset++] = data_size & 0xFF;
    
    // Add data
    memcpy(&buffer[offset], data, data_size);
    offset += data_size;
    
    return offset;
}

// Example: Temperature Control Application
typedef struct {
    float temperature;
    float setpoint;
    uint16_t status;
    char alarm_text[64];
} temp_controller_t;

int main() {
    // Allocate objects array
    fms_object_t objects[10];
    
    // Initialize VFD
    fms_vfd_t vfd;
    fms_vfd_init(&vfd, 5, "ACME Corp", "TempController-3000");
    vfd.objects = objects;
    
    // Create controller data
    temp_controller_t controller = {
        .temperature = 25.5,
        .setpoint = 30.0,
        .status = 0x0001,
        .alarm_text = "Normal Operation"
    };
    
    // Register objects
    fms_register_object(&vfd, "Temperature", FMS_OBJ_SIMPLE_VAR,
                       &controller.temperature, sizeof(float), true, false);
    fms_register_object(&vfd, "Setpoint", FMS_OBJ_SIMPLE_VAR,
                       &controller.setpoint, sizeof(float), true, true);
    fms_register_object(&vfd, "Status", FMS_OBJ_SIMPLE_VAR,
                       &controller.status, sizeof(uint16_t), true, false);
    
    // Build read request for temperature
    uint8_t request_buffer[256];
    int req_len = fms_build_read_request(&vfd, "Temperature", 
                                         request_buffer, sizeof(request_buffer));
    
    // Simulate response (normally received from network)
    uint8_t response[256] = {
        0x41, 0x84, 0x00, 0x01, // Response header
        0x00, 0x04, // Data length
        0x41, 0xCC, 0x00, 0x00  // Float data (25.5 in IEEE 754)
    };
    
    // Process response
    float received_temp;
    int data_len = fms_process_read_response(&vfd, response, 10, 
                                             &received_temp, sizeof(float));
    
    return 0;
}
```

## Rust Implementation

```rust
use std::collections::HashMap;
use std::fmt;

// FMS Service Codes
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum FmsService {
    Initiate = 0x00,
    Read = 0x04,
    Write = 0x05,
    InformationReport = 0x06,
    GetOD = 0x0C,
    Status = 0x0D,
    Identify = 0x0E,
}

// FMS Object Types
#[derive(Debug, Clone, Copy)]
pub enum FmsObjectType {
    SimpleVariable,
    Array,
    Record,
    Domain,
}

// FMS Communication Object
pub struct FmsObject {
    name: String,
    obj_type: FmsObjectType,
    index: u16,
    data: Vec<u8>,
    readable: bool,
    writable: bool,
}

impl FmsObject {
    pub fn new(
        name: &str,
        obj_type: FmsObjectType,
        index: u16,
        readable: bool,
        writable: bool,
    ) -> Self {
        Self {
            name: name.to_string(),
            obj_type,
            index,
            data: Vec::new(),
            readable,
            writable,
        }
    }

    pub fn set_data(&mut self, data: Vec<u8>) {
        self.data = data;
    }

    pub fn get_data(&self) -> &[u8] {
        &self.data
    }
}

// FMS Protocol Data Unit
#[derive(Debug)]
pub struct FmsPdu {
    pdu_type: u8,
    service: FmsService,
    invoke_id: u16,
    data: Vec<u8>,
}

impl FmsPdu {
    pub fn new_request(service: FmsService, invoke_id: u16, data: Vec<u8>) -> Self {
        Self {
            pdu_type: 0x40, // Request PDU
            service,
            invoke_id,
            data,
        }
    }

    pub fn new_response(service: FmsService, invoke_id: u16, data: Vec<u8>) -> Self {
        Self {
            pdu_type: 0x41, // Response PDU
            service,
            invoke_id,
            data,
        }
    }

    pub fn encode(&self) -> Vec<u8> {
        let mut buffer = Vec::new();
        buffer.push(self.pdu_type);
        buffer.push(self.service as u8);
        buffer.push((self.invoke_id >> 8) as u8);
        buffer.push(self.invoke_id as u8);
        buffer.extend_from_slice(&self.data);
        buffer
    }

    pub fn decode(buffer: &[u8]) -> Result<Self, &'static str> {
        if buffer.len() < 4 {
            return Err("Buffer too short");
        }

        let pdu_type = buffer[0];
        let service_code = buffer[1] & 0x7F;
        let invoke_id = ((buffer[2] as u16) << 8) | (buffer[3] as u16);
        let data = buffer[4..].to_vec();

        let service = match service_code {
            0x00 => FmsService::Initiate,
            0x04 => FmsService::Read,
            0x05 => FmsService::Write,
            0x06 => FmsService::InformationReport,
            0x0C => FmsService::GetOD,
            0x0D => FmsService::Status,
            0x0E => FmsService::Identify,
            _ => return Err("Unknown service code"),
        };

        Ok(Self {
            pdu_type,
            service,
            invoke_id,
            data,
        })
    }
}

// FMS Virtual Field Device
pub struct FmsVfd {
    device_address: u8,
    vendor_name: String,
    model_name: String,
    revision: u16,
    objects: HashMap<String, FmsObject>,
    object_by_index: HashMap<u16, String>,
    next_invoke_id: u16,
}

impl FmsVfd {
    pub fn new(address: u8, vendor: &str, model: &str) -> Self {
        Self {
            device_address: address,
            vendor_name: vendor.to_string(),
            model_name: model.to_string(),
            revision: 0x0100,
            objects: HashMap::new(),
            object_by_index: HashMap::new(),
            next_invoke_id: 1,
        }
    }

    pub fn register_object(&mut self, object: FmsObject) -> Result<u16, &'static str> {
        let index = object.index;
        let name = object.name.clone();

        if self.objects.contains_key(&name) {
            return Err("Object already exists");
        }

        self.object_by_index.insert(index, name.clone());
        self.objects.insert(name, object);
        Ok(index)
    }

    pub fn build_read_request(&mut self, object_name: &str) -> Result<FmsPdu, &'static str> {
        let object = self.objects.get(object_name)
            .ok_or("Object not found")?;

        if !object.readable {
            return Err("Object not readable");
        }

        let mut data = Vec::new();
        data.push((object.index >> 8) as u8);
        data.push(object.index as u8);

        let invoke_id = self.next_invoke_id;
        self.next_invoke_id = self.next_invoke_id.wrapping_add(1);

        Ok(FmsPdu::new_request(FmsService::Read, invoke_id, data))
    }

    pub fn build_write_request(
        &mut self,
        object_name: &str,
        data: Vec<u8>,
    ) -> Result<FmsPdu, &'static str> {
        let object = self.objects.get(object_name)
            .ok_or("Object not found")?;

        if !object.writable {
            return Err("Object not writable");
        }

        let mut pdu_data = Vec::new();
        pdu_data.push((object.index >> 8) as u8);
        pdu_data.push(object.index as u8);
        pdu_data.push((data.len() >> 8) as u8);
        pdu_data.push(data.len() as u8);
        pdu_data.extend_from_slice(&data);

        let invoke_id = self.next_invoke_id;
        self.next_invoke_id = self.next_invoke_id.wrapping_add(1);

        Ok(FmsPdu::new_request(FmsService::Write, invoke_id, pdu_data))
    }

    pub fn process_read_request(&self, pdu: &FmsPdu) -> Result<FmsPdu, &'static str> {
        if pdu.data.len() < 2 {
            return Err("Invalid read request");
        }

        let index = ((pdu.data[0] as u16) << 8) | (pdu.data[1] as u16);
        let object_name = self.object_by_index.get(&index)
            .ok_or("Object not found")?;
        let object = self.objects.get(object_name)
            .ok_or("Object not found")?;

        if !object.readable {
            return Err("Object not readable");
        }

        let mut response_data = Vec::new();
        response_data.push((object.data.len() >> 8) as u8);
        response_data.push(object.data.len() as u8);
        response_data.extend_from_slice(&object.data);

        Ok(FmsPdu::new_response(FmsService::Read, pdu.invoke_id, response_data))
    }

    pub fn process_write_request(&mut self, pdu: &FmsPdu) -> Result<FmsPdu, &'static str> {
        if pdu.data.len() < 4 {
            return Err("Invalid write request");
        }

        let index = ((pdu.data[0] as u16) << 8) | (pdu.data[1] as u16);
        let data_len = ((pdu.data[2] as usize) << 8) | (pdu.data[3] as usize);

        if pdu.data.len() < 4 + data_len {
            return Err("Incomplete data");
        }

        let object_name = self.object_by_index.get(&index)
            .ok_or("Object not found")?;
        let object = self.objects.get_mut(object_name)
            .ok_or("Object not found")?;

        if !object.writable {
            return Err("Object not writable");
        }

        object.data = pdu.data[4..4 + data_len].to_vec();

        Ok(FmsPdu::new_response(FmsService::Write, pdu.invoke_id, Vec::new()))
    }
}

// Example: Temperature Controller Application
fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create VFD
    let mut vfd = FmsVfd::new(5, "ACME Corp", "TempController-3000");

    // Register temperature object (read-only)
    let mut temp_obj = FmsObject::new("Temperature", FmsObjectType::SimpleVariable, 0, true, false);
    temp_obj.set_data(25.5f32.to_le_bytes().to_vec());
    vfd.register_object(temp_obj)?;

    // Register setpoint object (read-write)
    let mut setpoint_obj = FmsObject::new("Setpoint", FmsObjectType::SimpleVariable, 1, true, true);
    setpoint_obj.set_data(30.0f32.to_le_bytes().to_vec());
    vfd.register_object(setpoint_obj)?;

    // Build read request
    let read_request = vfd.build_read_request("Temperature")?;
    println!("Read Request: {:?}", read_request);

    let encoded_request = read_request.encode();
    println!("Encoded Request: {:02X?}", encoded_request);

    // Process read request (simulating server side)
    let response = vfd.process_read_request(&read_request)?;
    println!("Response: {:?}", response);

    // Build write request
    let new_setpoint = 35.0f32.to_le_bytes().to_vec();
    let write_request = vfd.build_write_request("Setpoint", new_setpoint)?;
    println!("Write Request: {:?}", write_request);

    // Process write request
    let write_response = vfd.process_write_request(&write_request)?;
    println!("Write Response: {:?}", write_response);

    Ok(())
}
```

## Summary

**Profibus FMS (Fieldbus Message Specification)** provides sophisticated peer-to-peer communication capabilities for complex industrial automation systems. Unlike the simpler master-slave Profibus DP, FMS supports flexible client-server relationships with rich application services including variable access, domain management, program invocation, and event handling.

**Key advantages** include object-oriented design through Virtual Field Devices (VFDs), comprehensive service set for complex operations, vendor-neutral communication, and support for engineering and diagnostic applications. However, FMS has **higher complexity** than DP, requiring more processing power and memory, which has led to its gradual replacement by more modern protocols like PROFINET.

The implementations shown demonstrate core FMS concepts: building and processing read/write requests, managing object dictionaries, and handling PDU encoding/decoding. In production systems, you would integrate these with actual Profibus physical layer drivers, add comprehensive error handling, implement security features, and handle the full FMS service set including domain transfers and event notifications.