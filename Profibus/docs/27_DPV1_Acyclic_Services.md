# DPV1 Acyclic Services: Profibus Protocol Deep Dive

## Overview

DPV1 (Decentralized Periphery Version 1) acyclic services are part of the Profibus DP protocol extension that enables non-cyclic communication between masters and slaves. Unlike cyclic data exchange which happens continuously at fixed intervals, acyclic services provide on-demand access for parameterization, diagnostics, and alarm handling.

## What Are Acyclic Services?

In Profibus networks, communication typically occurs in two modes:

**Cyclic communication** handles real-time process data (inputs/outputs) exchanged regularly during each bus cycle. **Acyclic communication** handles sporadic operations like reading configuration parameters, writing device settings, retrieving diagnostic information, or handling alarms and events.

DPV1 acyclic services use a request-response model where the master initiates a service and the slave responds. These services run alongside cyclic data exchange without disrupting real-time operations.

## Key DPV1 Acyclic Service Types

The main acyclic services include:

**MSAC1_Read** - Read data from slave's address space for diagnostics or parameter retrieval
**MSAC1_Write** - Write configuration or parameter data to slave devices  
**Data_Transport** - Transfer larger data blocks for firmware updates or recipe downloads
**Alarm handling** - Process status change notifications and alarm acknowledgments

These services access different slave memory areas: input data, output data, diagnosis data, and parameter data.

## Implementation Architecture

A typical implementation requires several components:

- Service request queue management
- State machine for tracking request/response cycles  
- Timeout handling for failed transactions
- Buffer management for data transfer
- Integration with cyclic data exchange logic

## C/C++ Implementation Example

Here's a comprehensive C++ implementation showing the core structure:

```cpp
#include <stdint.h>
#include <queue>
#include <memory>
#include <chrono>

// DPV1 Service Types
enum class DPV1ServiceType {
    READ = 0x01,
    WRITE = 0x02,
    DATA_TRANSPORT = 0x03,
    ALARM_ACK = 0x04
};

// Service Request Status
enum class ServiceStatus {
    IDLE,
    PENDING,
    IN_PROGRESS,
    COMPLETED,
    ERROR,
    TIMEOUT
};

// DPV1 Service Request Structure
struct DPV1ServiceRequest {
    uint8_t slave_address;
    DPV1ServiceType service_type;
    uint8_t slot;           // Slot number in slave
    uint16_t index;         // Parameter index
    uint8_t* data;          // Data buffer
    uint16_t data_length;
    ServiceStatus status;
    std::chrono::steady_clock::time_point timestamp;
    uint32_t timeout_ms;
    
    DPV1ServiceRequest() : data(nullptr), data_length(0), 
                          timeout_ms(1000), status(ServiceStatus::IDLE) {}
    ~DPV1ServiceRequest() { delete[] data; }
};

// DPV1 Acyclic Service Manager
class DPV1AcyclicManager {
private:
    std::queue<std::shared_ptr<DPV1ServiceRequest>> request_queue;
    std::shared_ptr<DPV1ServiceRequest> current_request;
    uint8_t max_parallel_services;
    
    // Profibus DP frame structure
    struct DPFrame {
        uint8_t start_delimiter;
        uint8_t destination_address;
        uint8_t source_address;
        uint8_t function_code;
        uint8_t* data_unit;
        uint16_t data_length;
        uint8_t fcs;  // Frame Check Sequence
        uint8_t end_delimiter;
    };
    
public:
    DPV1AcyclicManager() : max_parallel_services(1) {}
    
    // Add a read service request
    bool addReadRequest(uint8_t slave_addr, uint8_t slot, 
                       uint16_t index, uint16_t length) {
        auto request = std::make_shared<DPV1ServiceRequest>();
        request->slave_address = slave_addr;
        request->service_type = DPV1ServiceType::READ;
        request->slot = slot;
        request->index = index;
        request->data = new uint8_t[length];
        request->data_length = length;
        request->status = ServiceStatus::PENDING;
        
        request_queue.push(request);
        return true;
    }
    
    // Add a write service request
    bool addWriteRequest(uint8_t slave_addr, uint8_t slot,
                        uint16_t index, const uint8_t* data, 
                        uint16_t length) {
        auto request = std::make_shared<DPV1ServiceRequest>();
        request->slave_address = slave_addr;
        request->service_type = DPV1ServiceType::WRITE;
        request->slot = slot;
        request->index = index;
        request->data = new uint8_t[length];
        memcpy(request->data, data, length);
        request->data_length = length;
        request->status = ServiceStatus::PENDING;
        
        request_queue.push(request);
        return true;
    }
    
    // Build DPV1 Read Request Frame
    DPFrame buildReadRequest(const DPV1ServiceRequest& req) {
        DPFrame frame;
        frame.start_delimiter = 0x68;  // SD2 - Start delimiter
        frame.destination_address = req.slave_address;
        frame.source_address = 0x00;   // Master address
        frame.function_code = 0x5D;    // MSAC1 Request
        
        // Data Unit for Read Service
        uint16_t du_length = 8;
        frame.data_unit = new uint8_t[du_length];
        frame.data_length = du_length;
        
        // Function code and slot/index
        frame.data_unit[0] = 0x5E;  // DPV1 Read
        frame.data_unit[1] = req.slot;
        frame.data_unit[2] = (req.index >> 8) & 0xFF;  // Index high
        frame.data_unit[3] = req.index & 0xFF;         // Index low
        frame.data_unit[4] = (req.data_length >> 8) & 0xFF;
        frame.data_unit[5] = req.data_length & 0xFF;
        frame.data_unit[6] = 0x00;  // Reference (optional)
        frame.data_unit[7] = 0x00;
        
        frame.fcs = calculateFCS(frame);
        frame.end_delimiter = 0x16;
        
        return frame;
    }
    
    // Build DPV1 Write Request Frame
    DPFrame buildWriteRequest(const DPV1ServiceRequest& req) {
        DPFrame frame;
        frame.start_delimiter = 0x68;
        frame.destination_address = req.slave_address;
        frame.source_address = 0x00;
        frame.function_code = 0x5D;
        
        uint16_t du_length = 6 + req.data_length;
        frame.data_unit = new uint8_t[du_length];
        frame.data_length = du_length;
        
        frame.data_unit[0] = 0x5F;  // DPV1 Write
        frame.data_unit[1] = req.slot;
        frame.data_unit[2] = (req.index >> 8) & 0xFF;
        frame.data_unit[3] = req.index & 0xFF;
        frame.data_unit[4] = (req.data_length >> 8) & 0xFF;
        frame.data_unit[5] = req.data_length & 0xFF;
        
        // Copy data payload
        memcpy(&frame.data_unit[6], req.data, req.data_length);
        
        frame.fcs = calculateFCS(frame);
        frame.end_delimiter = 0x16;
        
        return frame;
    }
    
    // Process service queue
    void processServices() {
        // Check for timeout on current request
        if (current_request && 
            current_request->status == ServiceStatus::IN_PROGRESS) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>
                          (now - current_request->timestamp).count();
            
            if (elapsed > current_request->timeout_ms) {
                current_request->status = ServiceStatus::TIMEOUT;
                current_request.reset();
            }
        }
        
        // Start new request if available
        if (!current_request && !request_queue.empty()) {
            current_request = request_queue.front();
            request_queue.pop();
            
            current_request->status = ServiceStatus::IN_PROGRESS;
            current_request->timestamp = std::chrono::steady_clock::now();
            
            // Build and send appropriate frame
            DPFrame frame;
            if (current_request->service_type == DPV1ServiceType::READ) {
                frame = buildReadRequest(*current_request);
            } else if (current_request->service_type == DPV1ServiceType::WRITE) {
                frame = buildWriteRequest(*current_request);
            }
            
            sendFrame(frame);
            delete[] frame.data_unit;
        }
    }
    
    // Handle received response
    void handleResponse(const uint8_t* response_data, uint16_t length) {
        if (!current_request) return;
        
        // Parse response header
        uint8_t function_code = response_data[0];
        uint8_t slot = response_data[1];
        
        if (function_code == 0xDE) {  // Positive response for Read
            uint16_t data_length = (response_data[2] << 8) | response_data[3];
            memcpy(current_request->data, &response_data[4], data_length);
            current_request->status = ServiceStatus::COMPLETED;
        } else if (function_code == 0xDF) {  // Positive response for Write
            current_request->status = ServiceStatus::COMPLETED;
        } else {
            current_request->status = ServiceStatus::ERROR;
        }
        
        current_request.reset();
    }
    
private:
    uint8_t calculateFCS(const DPFrame& frame) {
        uint8_t fcs = frame.destination_address + frame.source_address + 
                      frame.function_code;
        for (uint16_t i = 0; i < frame.data_length; i++) {
            fcs += frame.data_unit[i];
        }
        return fcs;
    }
    
    void sendFrame(const DPFrame& frame) {
        // Platform-specific serial/bus transmission
        // This would interface with actual Profibus hardware driver
    }
};

// Example usage
int main() {
    DPV1AcyclicManager manager;
    
    // Read diagnostic data from slave 3, slot 1, index 0x1000
    manager.addReadRequest(3, 1, 0x1000, 64);
    
    // Write configuration parameter to slave 3, slot 1, index 0x2000
    uint8_t config_data[] = {0x01, 0x02, 0x03, 0x04};
    manager.addWriteRequest(3, 1, 0x2000, config_data, sizeof(config_data));
    
    // Process services in main loop
    while (true) {
        manager.processServices();
        // Handle other cyclic tasks
    }
    
    return 0;
}
```

## Rust Implementation Example

Here's a Rust implementation with strong type safety and memory management:

```rust
use std::collections::VecDeque;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq)]
enum DPV1ServiceType {
    Read = 0x5E,
    Write = 0x5F,
    DataTransport = 0x60,
    AlarmAck = 0x61,
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum ServiceStatus {
    Idle,
    Pending,
    InProgress,
    Completed,
    Error,
    Timeout,
}

#[derive(Debug)]
struct DPV1ServiceRequest {
    slave_address: u8,
    service_type: DPV1ServiceType,
    slot: u8,
    index: u16,
    data: Vec<u8>,
    status: ServiceStatus,
    timestamp: Option<Instant>,
    timeout: Duration,
}

impl DPV1ServiceRequest {
    fn new_read(slave_addr: u8, slot: u8, index: u16, length: usize) -> Self {
        Self {
            slave_address: slave_addr,
            service_type: DPV1ServiceType::Read,
            slot,
            index,
            data: vec![0; length],
            status: ServiceStatus::Pending,
            timestamp: None,
            timeout: Duration::from_millis(1000),
        }
    }
    
    fn new_write(slave_addr: u8, slot: u8, index: u16, data: Vec<u8>) -> Self {
        Self {
            slave_address: slave_addr,
            service_type: DPV1ServiceType::Write,
            slot,
            index,
            data,
            status: ServiceStatus::Pending,
            timestamp: None,
            timeout: Duration::from_millis(1000),
        }
    }
}

#[derive(Debug)]
struct DPFrame {
    start_delimiter: u8,
    destination_address: u8,
    source_address: u8,
    function_code: u8,
    data_unit: Vec<u8>,
    fcs: u8,
    end_delimiter: u8,
}

impl DPFrame {
    fn calculate_fcs(&self) -> u8 {
        let mut fcs = self.destination_address
            .wrapping_add(self.source_address)
            .wrapping_add(self.function_code);
        
        for byte in &self.data_unit {
            fcs = fcs.wrapping_add(*byte);
        }
        fcs
    }
    
    fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();
        bytes.push(self.start_delimiter);
        bytes.push(self.data_unit.len() as u8);
        bytes.push(self.data_unit.len() as u8); // Repeated for SD2
        bytes.push(self.start_delimiter);
        bytes.push(self.destination_address);
        bytes.push(self.source_address);
        bytes.push(self.function_code);
        bytes.extend_from_slice(&self.data_unit);
        bytes.push(self.fcs);
        bytes.push(self.end_delimiter);
        bytes
    }
}

struct DPV1AcyclicManager {
    request_queue: VecDeque<DPV1ServiceRequest>,
    current_request: Option<DPV1ServiceRequest>,
    max_parallel_services: usize,
}

impl DPV1AcyclicManager {
    fn new() -> Self {
        Self {
            request_queue: VecDeque::new(),
            current_request: None,
            max_parallel_services: 1,
        }
    }
    
    fn add_read_request(&mut self, slave_addr: u8, slot: u8, 
                       index: u16, length: usize) {
        let request = DPV1ServiceRequest::new_read(slave_addr, slot, index, length);
        self.request_queue.push_back(request);
    }
    
    fn add_write_request(&mut self, slave_addr: u8, slot: u8,
                        index: u16, data: Vec<u8>) {
        let request = DPV1ServiceRequest::new_write(slave_addr, slot, index, data);
        self.request_queue.push_back(request);
    }
    
    fn build_read_request(&self, req: &DPV1ServiceRequest) -> DPFrame {
        let mut data_unit = Vec::new();
        data_unit.push(DPV1ServiceType::Read as u8);
        data_unit.push(req.slot);
        data_unit.push((req.index >> 8) as u8);
        data_unit.push((req.index & 0xFF) as u8);
        data_unit.push((req.data.len() >> 8) as u8);
        data_unit.push((req.data.len() & 0xFF) as u8);
        data_unit.push(0x00); // Reference high
        data_unit.push(0x00); // Reference low
        
        let mut frame = DPFrame {
            start_delimiter: 0x68,
            destination_address: req.slave_address,
            source_address: 0x00,
            function_code: 0x5D, // MSAC1 Request
            data_unit,
            fcs: 0,
            end_delimiter: 0x16,
        };
        
        frame.fcs = frame.calculate_fcs();
        frame
    }
    
    fn build_write_request(&self, req: &DPV1ServiceRequest) -> DPFrame {
        let mut data_unit = Vec::new();
        data_unit.push(DPV1ServiceType::Write as u8);
        data_unit.push(req.slot);
        data_unit.push((req.index >> 8) as u8);
        data_unit.push((req.index & 0xFF) as u8);
        data_unit.push((req.data.len() >> 8) as u8);
        data_unit.push((req.data.len() & 0xFF) as u8);
        data_unit.extend_from_slice(&req.data);
        
        let mut frame = DPFrame {
            start_delimiter: 0x68,
            destination_address: req.slave_address,
            source_address: 0x00,
            function_code: 0x5D,
            data_unit,
            fcs: 0,
            end_delimiter: 0x16,
        };
        
        frame.fcs = frame.calculate_fcs();
        frame
    }
    
    fn process_services(&mut self) -> Option<Vec<u8>> {
        // Check timeout on current request
        if let Some(ref mut request) = self.current_request {
            if request.status == ServiceStatus::InProgress {
                if let Some(timestamp) = request.timestamp {
                    if timestamp.elapsed() > request.timeout {
                        request.status = ServiceStatus::Timeout;
                        self.current_request = None;
                    }
                }
            }
        }
        
        // Start new request if no current request
        if self.current_request.is_none() {
            if let Some(mut request) = self.request_queue.pop_front() {
                request.status = ServiceStatus::InProgress;
                request.timestamp = Some(Instant::now());
                
                let frame = match request.service_type {
                    DPV1ServiceType::Read => self.build_read_request(&request),
                    DPV1ServiceType::Write => self.build_write_request(&request),
                    _ => return None,
                };
                
                self.current_request = Some(request);
                return Some(frame.to_bytes());
            }
        }
        
        None
    }
    
    fn handle_response(&mut self, response_data: &[u8]) -> Result<(), String> {
        if let Some(ref mut request) = self.current_request {
            if response_data.len() < 2 {
                return Err("Response too short".to_string());
            }
            
            let function_code = response_data[0];
            
            match function_code {
                0xDE => {  // Positive Read Response
                    if response_data.len() >= 4 {
                        let data_length = ((response_data[2] as usize) << 8) 
                                        | (response_data[3] as usize);
                        if response_data.len() >= 4 + data_length {
                            request.data.copy_from_slice(
                                &response_data[4..4 + data_length]
                            );
                            request.status = ServiceStatus::Completed;
                        }
                    }
                }
                0xDF => {  // Positive Write Response
                    request.status = ServiceStatus::Completed;
                }
                _ => {
                    request.status = ServiceStatus::Error;
                    return Err(format!("Error response code: 0x{:02X}", function_code));
                }
            }
            
            self.current_request = None;
            Ok(())
        } else {
            Err("No current request".to_string())
        }
    }
    
    fn get_completed_requests(&self) -> Vec<&DPV1ServiceRequest> {
        // In a real implementation, you'd track completed requests
        vec![]
    }
}

// Example usage
fn main() {
    let mut manager = DPV1AcyclicManager::new();
    
    // Add read request for diagnostics
    manager.add_read_request(3, 1, 0x1000, 64);
    
    // Add write request for configuration
    let config_data = vec![0x01, 0x02, 0x03, 0x04];
    manager.add_write_request(3, 1, 0x2000, config_data);
    
    // Main processing loop
    loop {
        if let Some(frame_bytes) = manager.process_services() {
            println!("Sending frame: {} bytes", frame_bytes.len());
            // Send frame to Profibus hardware interface
            // send_to_profibus(&frame_bytes);
        }
        
        // Simulate receiving response
        // if let Some(response) = receive_from_profibus() {
        //     match manager.handle_response(&response) {
        //         Ok(_) => println!("Service completed successfully"),
        //         Err(e) => eprintln!("Service error: {}", e),
        //     }
        // }
        
        std::thread::sleep(Duration::from_millis(10));
    }
}
```

## Summary

DPV1 acyclic services extend Profibus DP with non-cyclic, on-demand communication capabilities essential for device configuration, diagnostics, and maintenance. The implementation requires careful state management, timeout handling, and proper frame construction following the Profibus protocol specifications.

Key implementation considerations include managing a service request queue, building protocol-compliant frames with correct function codes and data structures, handling timeouts and error conditions gracefully, integrating with cyclic communication without disruption, and supporting multiple concurrent service requests when hardware permits.

Both C++ and Rust implementations demonstrate the core architecture: request queuing, frame building for read/write operations, response parsing, and status tracking. The Rust version leverages strong typing and ownership for memory safety, while the C++ version provides fine-grained control suitable for embedded systems. These acyclic services are critical for industrial automation systems requiring runtime parameterization and diagnostic capabilities beyond simple I/O data exchange.