# Profibus Master-Slave Communication

## Overview

Profibus operates on a **master-slave communication architecture** where masters actively control bus access and slaves respond only when requested. This hierarchical structure ensures deterministic, real-time communication critical for industrial automation.

## Master Classes

### Master Class 1 (DP Master Class 1)
**Primary automation controller** - typically a PLC or DCS that:
- Executes cyclic data exchange with slaves
- Maintains real-time control loops
- Handles process-critical I/O operations
- Operates with strict timing requirements (typically 1-10ms cycles)

### Master Class 2 (DP Master Class 2)
**Engineering/diagnostic tool** - typically HMI, programming devices, or diagnostic tools that:
- Performs acyclic (non-cyclic) communication
- Reads diagnostic data and parameters
- Configures slaves and downloads parameters
- Does NOT interfere with Class 1 cyclic communication

## Communication Patterns

**Class 1**: Cyclic, deterministic data exchange (inputs/outputs)
**Class 2**: Acyclic, event-driven requests (diagnostics, configuration)

Both can coexist on the same bus without conflicts due to Profibus token-passing protocol.

---

## Code Examples

```c
/*
 * Profibus Master-Slave Communication Example (C)
 * Demonstrates Class 1 and Class 2 master operations
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Profibus Frame Types */
#define PROFIBUS_FRAME_DATA     0x68
#define PROFIBUS_FRAME_TOKEN    0xDC
#define PROFIBUS_FRAME_ACK      0xE5

/* Function Codes */
#define FC_SRD_HIGH    0x0D  /* Send and Request Data (High Priority) */
#define FC_SRD_LOW     0x0C  /* Send and Request Data (Low Priority) */
#define FC_SDN_HIGH    0x0F  /* Send Data with No Acknowledge (High) */
#define FC_MSRD        0x5D  /* Master Send Read Data (Class 2) */

/* Maximum frame sizes */
#define MAX_DATA_LEN   244
#define MAX_SLAVES     126

/* Profibus telegram structure */
typedef struct {
    uint8_t start_delimiter;
    uint8_t length;
    uint8_t length_repeat;
    uint8_t start_delimiter2;
    uint8_t destination_addr;
    uint8_t source_addr;
    uint8_t function_code;
    uint8_t data[MAX_DATA_LEN];
    uint8_t data_len;
    uint8_t checksum;
    uint8_t end_delimiter;
} profibus_frame_t;

/* Slave device state */
typedef struct {
    uint8_t address;
    bool active;
    uint8_t input_data[32];   /* Process input data */
    uint8_t output_data[32];  /* Process output data */
    uint8_t input_len;
    uint8_t output_len;
    uint32_t cycle_count;
    uint16_t diag_status;
} slave_device_t;

/* Master Class 1 - Cyclic communication */
typedef struct {
    uint8_t master_addr;
    slave_device_t slaves[MAX_SLAVES];
    uint8_t slave_count;
    uint32_t cycle_time_us;  /* Cycle time in microseconds */
    bool bus_active;
} dp_master_class1_t;

/* Master Class 2 - Acyclic communication */
typedef struct {
    uint8_t master_addr;
    bool active;
} dp_master_class2_t;

/* Calculate Profibus checksum */
uint8_t calculate_checksum(profibus_frame_t *frame) {
    uint8_t sum = frame->destination_addr + frame->source_addr + frame->function_code;
    for (int i = 0; i < frame->data_len; i++) {
        sum += frame->data[i];
    }
    return sum;
}

/* Build a Profibus frame */
void build_frame(profibus_frame_t *frame, uint8_t dest, uint8_t src, 
                 uint8_t fc, uint8_t *data, uint8_t data_len) {
    frame->start_delimiter = PROFIBUS_FRAME_DATA;
    frame->length = data_len + 3;  /* DA + SA + FC + data */
    frame->length_repeat = frame->length;
    frame->start_delimiter2 = PROFIBUS_FRAME_DATA;
    frame->destination_addr = dest;
    frame->source_addr = src;
    frame->function_code = fc;
    frame->data_len = data_len;
    
    if (data != NULL && data_len > 0) {
        memcpy(frame->data, data, data_len);
    }
    
    frame->checksum = calculate_checksum(frame);
    frame->end_delimiter = 0x16;
}

/* Master Class 1: Cyclic data exchange */
int master_class1_cyclic_exchange(dp_master_class1_t *master, uint8_t slave_idx) {
    if (slave_idx >= master->slave_count) {
        return -1;
    }
    
    slave_device_t *slave = &master->slaves[slave_idx];
    if (!slave->active) {
        return -1;
    }
    
    profibus_frame_t frame;
    
    /* Send output data to slave and request input data */
    build_frame(&frame, slave->address, master->master_addr, 
                FC_SRD_HIGH, slave->output_data, slave->output_len);
    
    printf("[Class1] Cyclic exchange with slave %d:\n", slave->address);
    printf("  Sending %d bytes output data\n", slave->output_len);
    
    /* In real implementation, would send frame and wait for response */
    /* Simulating response reception */
    
    /* Slave responds with input data */
    slave->cycle_count++;
    
    printf("  Received %d bytes input data (cycle: %u)\n", 
           slave->input_len, slave->cycle_count);
    
    return 0;
}

/* Master Class 1: Run cyclic scan of all slaves */
void master_class1_run_cycle(dp_master_class1_t *master) {
    printf("\n=== Master Class 1 Cycle Start ===\n");
    
    for (int i = 0; i < master->slave_count; i++) {
        if (master->slaves[i].active) {
            master_class1_cyclic_exchange(master, i);
        }
    }
    
    printf("=== Master Class 1 Cycle Complete ===\n");
}

/* Master Class 2: Read slave diagnostics (acyclic) */
int master_class2_read_diagnostics(dp_master_class2_t *master, 
                                   uint8_t slave_addr, 
                                   uint8_t *diag_buffer, 
                                   uint8_t *diag_len) {
    profibus_frame_t frame;
    uint8_t request[2] = {0x3D, 0x00};  /* Read diagnosis command */
    
    build_frame(&frame, slave_addr, master->master_addr, 
                FC_MSRD, request, 2);
    
    printf("\n[Class2] Reading diagnostics from slave %d\n", slave_addr);
    
    /* In real implementation, would send frame and wait for response */
    /* Simulating diagnostic data */
    diag_buffer[0] = 0x00;  /* Station status OK */
    diag_buffer[1] = 0x00;  /* No errors */
    diag_buffer[2] = slave_addr;
    *diag_len = 3;
    
    printf("  Diagnostic status: OK\n");
    
    return 0;
}

/* Master Class 2: Write parameter to slave (acyclic) */
int master_class2_write_parameter(dp_master_class2_t *master,
                                  uint8_t slave_addr,
                                  uint8_t param_id,
                                  uint8_t *param_data,
                                  uint8_t param_len) {
    profibus_frame_t frame;
    uint8_t request[MAX_DATA_LEN];
    
    request[0] = 0x3E;  /* Write parameter command */
    request[1] = param_id;
    memcpy(&request[2], param_data, param_len);
    
    build_frame(&frame, slave_addr, master->master_addr,
                FC_MSRD, request, param_len + 2);
    
    printf("\n[Class2] Writing parameter %d to slave %d\n", param_id, slave_addr);
    printf("  Parameter data length: %d bytes\n", param_len);
    
    return 0;
}

/* Initialize Master Class 1 */
void init_master_class1(dp_master_class1_t *master, uint8_t addr) {
    master->master_addr = addr;
    master->slave_count = 0;
    master->cycle_time_us = 5000;  /* 5ms default cycle */
    master->bus_active = true;
    memset(master->slaves, 0, sizeof(master->slaves));
}

/* Add slave to Master Class 1 configuration */
void add_slave_to_master(dp_master_class1_t *master, uint8_t slave_addr,
                        uint8_t input_len, uint8_t output_len) {
    if (master->slave_count >= MAX_SLAVES) return;
    
    slave_device_t *slave = &master->slaves[master->slave_count];
    slave->address = slave_addr;
    slave->active = true;
    slave->input_len = input_len;
    slave->output_len = output_len;
    slave->cycle_count = 0;
    slave->diag_status = 0;
    
    master->slave_count++;
    printf("Added slave %d (In: %d bytes, Out: %d bytes)\n",
           slave_addr, input_len, output_len);
}

/* Main demonstration */
int main(void) {
    printf("Profibus DP Master-Slave Communication Demo\n");
    printf("==========================================\n\n");
    
    /* Initialize Master Class 1 (PLC) */
    dp_master_class1_t master1;
    init_master_class1(&master1, 2);  /* Master address 2 */
    
    /* Configure slaves */
    add_slave_to_master(&master1, 10, 4, 2);  /* Slave 10: 4 in, 2 out */
    add_slave_to_master(&master1, 11, 8, 4);  /* Slave 11: 8 in, 4 out */
    add_slave_to_master(&master1, 12, 2, 2);  /* Slave 12: 2 in, 2 out */
    
    /* Simulate some output data */
    master1.slaves[0].output_data[0] = 0xFF;
    master1.slaves[0].output_data[1] = 0xAA;
    
    /* Run several Class 1 cyclic exchanges */
    for (int cycle = 0; cycle < 3; cycle++) {
        master_class1_run_cycle(&master1);
    }
    
    /* Initialize Master Class 2 (HMI/Engineering tool) */
    dp_master_class2_t master2;
    master2.master_addr = 1;  /* Master address 1 */
    master2.active = true;
    
    /* Class 2 acyclic operations */
    uint8_t diag_data[32];
    uint8_t diag_len;
    
    master_class2_read_diagnostics(&master2, 10, diag_data, &diag_len);
    
    uint8_t new_param[4] = {0x01, 0x02, 0x03, 0x04};
    master_class2_write_parameter(&master2, 11, 5, new_param, 4);
    
    printf("\n=== Both masters operate concurrently ===\n");
    printf("Class 1 handles real-time I/O\n");
    printf("Class 2 handles configuration/diagnostics\n");
    
    return 0;
}
```

```cpp
/*
 * Profibus Master-Slave Communication Example (C++)
 * Object-oriented approach with Class 1 and Class 2 masters
 */

#include <iostream>
#include <vector>
#include <memory>
#include <cstring>
#include <cstdint>
#include <string>
#include <chrono>

// Profibus frame types and function codes
enum class FrameType : uint8_t {
    DATA = 0x68,
    TOKEN = 0xDC,
    ACK = 0xE5
};

enum class FunctionCode : uint8_t {
    SRD_HIGH = 0x0D,    // Send and Request Data (High Priority)
    SRD_LOW = 0x0C,     // Send and Request Data (Low Priority)
    SDN_HIGH = 0x0F,    // Send Data No Acknowledge (High)
    MSRD = 0x5D         // Master Send Read Data (Class 2)
};

// Profibus telegram class
class ProfibusFrame {
public:
    static constexpr size_t MAX_DATA_LEN = 244;
    
    uint8_t destination;
    uint8_t source;
    FunctionCode function;
    std::vector<uint8_t> data;
    
    ProfibusFrame(uint8_t dest, uint8_t src, FunctionCode fc)
        : destination(dest), source(src), function(fc) {}
    
    void setData(const std::vector<uint8_t>& newData) {
        data = newData;
    }
    
    uint8_t calculateChecksum() const {
        uint8_t sum = destination + source + static_cast<uint8_t>(function);
        for (uint8_t byte : data) {
            sum += byte;
        }
        return sum;
    }
    
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> frame;
        uint8_t length = 3 + data.size(); // DA + SA + FC + data
        
        frame.push_back(static_cast<uint8_t>(FrameType::DATA));
        frame.push_back(length);
        frame.push_back(length); // length repeat
        frame.push_back(static_cast<uint8_t>(FrameType::DATA));
        frame.push_back(destination);
        frame.push_back(source);
        frame.push_back(static_cast<uint8_t>(function));
        
        frame.insert(frame.end(), data.begin(), data.end());
        frame.push_back(calculateChecksum());
        frame.push_back(0x16); // End delimiter
        
        return frame;
    }
};

// Base Slave Device class
class SlaveDevice {
protected:
    uint8_t address_;
    bool active_;
    std::vector<uint8_t> inputData_;
    std::vector<uint8_t> outputData_;
    uint32_t cycleCount_;
    uint16_t diagnosticStatus_;
    
public:
    SlaveDevice(uint8_t addr, size_t inputLen, size_t outputLen)
        : address_(addr), active_(true), cycleCount_(0), diagnosticStatus_(0) {
        inputData_.resize(inputLen, 0);
        outputData_.resize(outputLen, 0);
    }
    
    virtual ~SlaveDevice() = default;
    
    uint8_t getAddress() const { return address_; }
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }
    
    const std::vector<uint8_t>& getInputData() const { return inputData_; }
    const std::vector<uint8_t>& getOutputData() const { return outputData_; }
    
    void setOutputData(const std::vector<uint8_t>& data) {
        outputData_ = data;
    }
    
    void updateInputData(const std::vector<uint8_t>& data) {
        inputData_ = data;
    }
    
    void incrementCycle() { cycleCount_++; }
    uint32_t getCycleCount() const { return cycleCount_; }
    
    uint16_t getDiagnosticStatus() const { return diagnosticStatus_; }
    void setDiagnosticStatus(uint16_t status) { diagnosticStatus_ = status; }
};

// Master Class 1 - Cyclic data exchange
class DPMasterClass1 {
private:
    uint8_t masterAddress_;
    std::vector<std::shared_ptr<SlaveDevice>> slaves_;
    uint32_t cycleTimeUs_;
    bool busActive_;
    
public:
    DPMasterClass1(uint8_t address, uint32_t cycleTimeUs = 5000)
        : masterAddress_(address), cycleTimeUs_(cycleTimeUs), busActive_(true) {}
    
    void addSlave(std::shared_ptr<SlaveDevice> slave) {
        slaves_.push_back(slave);
        std::cout << "Class1: Added slave " << static_cast<int>(slave->getAddress())
                  << " (In: " << slave->getInputData().size()
                  << " bytes, Out: " << slave->getOutputData().size() 
                  << " bytes)" << std::endl;
    }
    
    bool cyclicExchange(std::shared_ptr<SlaveDevice> slave) {
        if (!slave->isActive()) {
            return false;
        }
        
        // Create request frame with output data
        ProfibusFrame request(slave->getAddress(), masterAddress_, 
                             FunctionCode::SRD_HIGH);
        request.setData(slave->getOutputData());
        
        std::cout << "[Class1] Cyclic exchange with slave " 
                  << static_cast<int>(slave->getAddress()) << ":" << std::endl;
        std::cout << "  Sending " << slave->getOutputData().size() 
                  << " bytes output data" << std::endl;
        
        // In real implementation: send frame and wait for response
        // Simulate response with input data
        slave->incrementCycle();
        
        std::cout << "  Received " << slave->getInputData().size() 
                  << " bytes input data (cycle: " 
                  << slave->getCycleCount() << ")" << std::endl;
        
        return true;
    }
    
    void runCycle() {
        std::cout << "\n=== Master Class 1 Cycle Start ===" << std::endl;
        
        for (auto& slave : slaves_) {
            cyclicExchange(slave);
        }
        
        std::cout << "=== Master Class 1 Cycle Complete ===" << std::endl;
    }
    
    uint32_t getCycleTime() const { return cycleTimeUs_; }
    void setCycleTime(uint32_t timeUs) { cycleTimeUs_ = timeUs; }
};

// Master Class 2 - Acyclic communication (diagnostics, configuration)
class DPMasterClass2 {
private:
    uint8_t masterAddress_;
    bool active_;
    
public:
    DPMasterClass2(uint8_t address) 
        : masterAddress_(address), active_(true) {}
    
    struct DiagnosticData {
        uint8_t stationStatus;
        uint8_t errorFlags;
        uint8_t slaveAddress;
        std::vector<uint8_t> extendedDiag;
    };
    
    DiagnosticData readDiagnostics(uint8_t slaveAddress) {
        ProfibusFrame request(slaveAddress, masterAddress_, 
                             FunctionCode::MSRD);
        std::vector<uint8_t> reqData = {0x3D, 0x00}; // Read diagnosis command
        request.setData(reqData);
        
        std::cout << "\n[Class2] Reading diagnostics from slave " 
                  << static_cast<int>(slaveAddress) << std::endl;
        
        // In real implementation: send frame and parse response
        DiagnosticData diag;
        diag.stationStatus = 0x00;  // OK
        diag.errorFlags = 0x00;     // No errors
        diag.slaveAddress = slaveAddress;
        
        std::cout << "  Station status: OK" << std::endl;
        std::cout << "  Error flags: 0x00" << std::endl;
        
        return diag;
    }
    
    bool writeParameter(uint8_t slaveAddress, uint8_t parameterId, 
                       const std::vector<uint8_t>& paramData) {
        ProfibusFrame request(slaveAddress, masterAddress_, 
                             FunctionCode::MSRD);
        
        std::vector<uint8_t> reqData;
        reqData.push_back(0x3E);  // Write parameter command
        reqData.push_back(parameterId);
        reqData.insert(reqData.end(), paramData.begin(), paramData.end());
        request.setData(reqData);
        
        std::cout << "\n[Class2] Writing parameter " 
                  << static_cast<int>(parameterId) << " to slave " 
                  << static_cast<int>(slaveAddress) << std::endl;
        std::cout << "  Parameter data length: " << paramData.size() 
                  << " bytes" << std::endl;
        
        // In real implementation: send frame and check response
        return true;
    }
    
    bool readConfiguration(uint8_t slaveAddress, 
                          std::vector<uint8_t>& configData) {
        ProfibusFrame request(slaveAddress, masterAddress_, 
                             FunctionCode::MSRD);
        std::vector<uint8_t> reqData = {0x3C, 0x00}; // Read config command
        request.setData(reqData);
        
        std::cout << "\n[Class2] Reading configuration from slave " 
                  << static_cast<int>(slaveAddress) << std::endl;
        
        // Simulate configuration data
        configData = {0x01, 0x02, 0x03, 0x04};
        
        std::cout << "  Configuration length: " << configData.size() 
                  << " bytes" << std::endl;
        
        return true;
    }
};

// Demonstration
int main() {
    std::cout << "Profibus DP Master-Slave Communication Demo (C++)" << std::endl;
    std::cout << "=================================================" << std::endl << std::endl;
    
    // Create Master Class 1 (PLC controller)
    DPMasterClass1 masterClass1(2, 5000); // Address 2, 5ms cycle
    
    // Create and configure slaves
    auto slave1 = std::make_shared<SlaveDevice>(10, 4, 2); // 4 in, 2 out
    auto slave2 = std::make_shared<SlaveDevice>(11, 8, 4); // 8 in, 4 out
    auto slave3 = std::make_shared<SlaveDevice>(12, 2, 2); // 2 in, 2 out
    
    masterClass1.addSlave(slave1);
    masterClass1.addSlave(slave2);
    masterClass1.addSlave(slave3);
    
    // Set some output data
    slave1->setOutputData({0xFF, 0xAA});
    slave2->setOutputData({0x01, 0x02, 0x03, 0x04});
    
    // Run cyclic communication
    std::cout << "\n--- Running Class 1 Cyclic Communication ---" << std::endl;
    for (int i = 0; i < 3; i++) {
        masterClass1.runCycle();
    }
    
    // Create Master Class 2 (Engineering tool/HMI)
    DPMasterClass2 masterClass2(1); // Address 1
    
    std::cout << "\n--- Running Class 2 Acyclic Communication ---" << std::endl;
    
    // Read diagnostics from slave
    auto diagnostics = masterClass2.readDiagnostics(10);
    
    // Write parameter to slave
    std::vector<uint8_t> paramData = {0x12, 0x34, 0x56, 0x78};
    masterClass2.writeParameter(11, 5, paramData);
    
    // Read configuration
    std::vector<uint8_t> configData;
    masterClass2.readConfiguration(12, configData);
    
    std::cout << "\n=== Concurrent Master Operation ===" << std::endl;
    std::cout << "Class 1 Master: Real-time cyclic I/O exchange" << std::endl;
    std::cout << "Class 2 Master: Acyclic diagnostics and configuration" << std::endl;
    std::cout << "Both masters coexist without interference" << std::endl;
    
    return 0;
}
```

```rust
/*
 * Profibus Master-Slave Communication Example (Rust)
 * Safe, concurrent implementation with type-safe enums
 */

use std::sync::{Arc, Mutex};
use std::collections::HashMap;

// Profibus frame types
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
enum FrameType {
    Data = 0x68,
    Token = 0xDC,
    Ack = 0xE5,
}

// Function codes
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
enum FunctionCode {
    SrdHigh = 0x0D,  // Send and Request Data (High Priority)
    SrdLow = 0x0C,   // Send and Request Data (Low Priority)
    SdnHigh = 0x0F,  // Send Data No Acknowledge (High)
    Msrd = 0x5D,     // Master Send Read Data (Class 2)
}

// Profibus frame structure
#[derive(Debug, Clone)]
struct ProfibusFrame {
    destination: u8,
    source: u8,
    function_code: FunctionCode,
    data: Vec<u8>,
}

impl ProfibusFrame {
    const MAX_DATA_LEN: usize = 244;
    
    fn new(destination: u8, source: u8, function_code: FunctionCode) -> Self {
        Self {
            destination,
            source,
            function_code,
            data: Vec::new(),
        }
    }
    
    fn with_data(mut self, data: Vec<u8>) -> Self {
        self.data = data;
        self
    }
    
    fn calculate_checksum(&self) -> u8 {
        let mut sum: u8 = self.destination
            .wrapping_add(self.source)
            .wrapping_add(self.function_code as u8);
        
        for byte in &self.data {
            sum = sum.wrapping_add(*byte);
        }
        
        sum
    }
    
    fn serialize(&self) -> Vec<u8> {
        let length = (3 + self.data.len()) as u8; // DA + SA + FC + data
        let mut frame = Vec::new();
        
        frame.push(FrameType::Data as u8);
        frame.push(length);
        frame.push(length); // length repeat
        frame.push(FrameType::Data as u8);
        frame.push(self.destination);
        frame.push(self.source);
        frame.push(self.function_code as u8);
        frame.extend_from_slice(&self.data);
        frame.push(self.calculate_checksum());
        frame.push(0x16); // End delimiter
        
        frame
    }
}

// Slave device structure
#[derive(Debug, Clone)]
struct SlaveDevice {
    address: u8,
    active: bool,
    input_data: Vec<u8>,
    output_data: Vec<u8>,
    cycle_count: u32,
    diagnostic_status: u16,
}

impl SlaveDevice {
    fn new(address: u8, input_len: usize, output_len: usize) -> Self {
        Self {
            address,
            active: true,
            input_data: vec![0; input_len],
            output_data: vec![0; output_len],
            cycle_count: 0,
            diagnostic_status: 0,
        }
    }
    
    fn set_output_data(&mut self, data: Vec<u8>) {
        self.output_data = data;
    }
    
    fn increment_cycle(&mut self) {
        self.cycle_count = self.cycle_count.wrapping_add(1);
    }
}

// Master Class 1 - Cyclic communication
struct DpMasterClass1 {
    master_address: u8,
    slaves: HashMap<u8, Arc<Mutex<SlaveDevice>>>,
    cycle_time_us: u32,
    bus_active: bool,
}

impl DpMasterClass1 {
    fn new(master_address: u8, cycle_time_us: u32) -> Self {
        Self {
            master_address,
            slaves: HashMap::new(),
            cycle_time_us,
            bus_active: true,
        }
    }
    
    fn add_slave(&mut self, slave: SlaveDevice) {
        let address = slave.address;
        println!(
            "Class1: Added slave {} (In: {} bytes, Out: {} bytes)",
            address,
            slave.input_data.len(),
            slave.output_data.len()
        );
        self.slaves.insert(address, Arc::new(Mutex::new(slave)));
    }
    
    fn cyclic_exchange(&self, slave_address: u8) -> Result<(), String> {
        let slave = self.slaves
            .get(&slave_address)
            .ok_or_else(|| format!("Slave {} not found", slave_address))?;
        
        let mut slave_lock = slave.lock().unwrap();
        
        if !slave_lock.active {
            return Err(format!("Slave {} is not active", slave_address));
        }
        
        // Create request frame with output data
        let frame = ProfibusFrame::new(
            slave_address,
            self.master_address,
            FunctionCode::SrdHigh,
        ).with_data(slave_lock.output_data.clone());
        
        println!("[Class1] Cyclic exchange with slave {}:", slave_address);
        println!("  Sending {} bytes output data", slave_lock.output_data.len());
        
        // In real implementation: send frame.serialize() and wait for response
        // Simulate response reception
        slave_lock.increment_cycle();
        
        println!(
            "  Received {} bytes input data (cycle: {})",
            slave_lock.input_data.len(),
            slave_lock.cycle_count
        );
        
        Ok(())
    }
    
    fn run_cycle(&self) {
        println!("\n=== Master Class 1 Cycle Start ===");
        
        let mut addresses: Vec<u8> = self.slaves.keys().copied().collect();
        addresses.sort();
        
        for address in addresses {
            if let Err(e) = self.cyclic_exchange(address) {
                eprintln!("Error in cyclic exchange: {}", e);
            }
        }
        
        println!("=== Master Class 1 Cycle Complete ===");
    }
}

// Diagnostic data structure
#[derive(Debug)]
struct DiagnosticData {
    station_status: u8,
    error_flags: u8,
    slave_address: u8,
    extended_diag: Vec<u8>,
}

// Master Class 2 - Acyclic communication
struct DpMasterClass2 {
    master_address: u8,
    active: bool,
}

impl DpMasterClass2 {
    fn new(master_address: u8) -> Self {
        Self {
            master_address,
            active: true,
        }
    }
    
    fn read_diagnostics(&self, slave_address: u8) -> Result<DiagnosticData, String> {
        let frame = ProfibusFrame::new(
            slave_address,
            self.master_address,
            FunctionCode::Msrd,
        ).with_data(vec![0x3D, 0x00]); // Read diagnosis command
        
        println!("\n[Class2] Reading diagnostics from slave {}", slave_address);
        
        // In real implementation: send frame.serialize() and parse response
        // Simulate diagnostic data
        let diag = DiagnosticData {
            station_status: 0x00,  // OK
            error_flags: 0x00,     // No errors
            slave_address,
            extended_diag: vec![],
        };
        
        println!("  Station status: OK");
        println!("  Error flags: 0x{:02X}", diag.error_flags);
        
        Ok(diag)
    }
    
    fn write_parameter(
        &self,
        slave_address: u8,
        parameter_id: u8,
        param_data: &[u8],
    ) -> Result<(), String> {
        let mut request_data = vec![0x3E, parameter_id]; // Write parameter command
        request_data.extend_from_slice(param_data);
        
        let frame = ProfibusFrame::new(
            slave_address,
            self.master_address,
            FunctionCode::Msrd,
        ).with_data(request_data);
        
        println!(
            "\n[Class2] Writing parameter {} to slave {}",
            parameter_id, slave_address
        );
        println!("  Parameter data length: {} bytes", param_data.len());
        
        // In real implementation: send frame.serialize() and check response
        Ok(())
    }
    
    fn read_configuration(&self, slave_address: u8) -> Result<Vec<u8>, String> {
        let frame = ProfibusFrame::new(
            slave_address,
            self.master_address,
            FunctionCode::Msrd,
        ).with_data(vec![0x3C, 0x00]); // Read config command
        
        println!("\n[Class2] Reading configuration from slave {}", slave_address);
        
        // Simulate configuration data
        let config_data = vec![0x01, 0x02, 0x03, 0x04];
        
        println!("  Configuration length: {} bytes", config_data.len());
        
        Ok(config_data)
    }
}

// Main demonstration
fn main() {
    println!("Profibus DP Master-Slave Communication Demo (Rust)");
    println!("=================================================\n");
    
    // Create Master Class 1 (PLC controller)
    let mut master_class1 = DpMasterClass1::new(2, 5000); // Address 2, 5ms cycle
    
    // Create and configure slaves
    let mut slave1 = SlaveDevice::new(10, 4, 2); // 4 in, 2 out
    let mut slave2 = SlaveDevice::new(11, 8, 4); // 8 in, 4 out
    let slave3 = SlaveDevice::new(12, 2, 2);     // 2 in, 2 out
    
    // Set some output data
    slave1.set_output_data(vec![0xFF, 0xAA]);
    slave2.set_output_data(vec![0x01, 0x02, 0x03, 0x04]);
    
    master_class1.add_slave(slave1);
    master_class1.add_slave(slave2);
    master_class1.add_slave(slave3);
    
    // Run cyclic communication
    println!("\n--- Running Class 1 Cyclic Communication ---");
    for _ in 0..3 {
        master_class1.run_cycle();
    }
    
    // Create Master Class 2 (Engineering tool/HMI)
    let master_class2 = DpMasterClass2::new(1); // Address 1
    
    println!("\n--- Running Class 2 Acyclic Communication ---");
    
    // Read diagnostics from slave
    match master_class2.read_diagnostics(10) {
        Ok(diag) => println!("Diagnostics received successfully"),
        Err(e) => eprintln!("Error reading diagnostics: {}", e),
    }
    
    // Write parameter to slave
    let param_data = vec![0x12, 0x34, 0x56, 0x78];
    if let Err(e) = master_class2.write_parameter(11, 5, &param_data) {
        eprintln!("Error writing parameter: {}", e);
    }
    
    // Read configuration
    match master_class2.read_configuration(12) {
        Ok(config) => println!("Configuration: {:?}", config),
        Err(e) => eprintln!("Error reading configuration: {}", e),
    }
    
    println!("\n=== Concurrent Master Operation ===");
    println!("Class 1 Master: Real-time cyclic I/O exchange");
    println!("Class 2 Master: Acyclic diagnostics and configuration");
    println!("Both masters coexist without interference");
}
```

## Summary

**Profibus Master-Slave Communication** implements a two-tier master architecture:

### Key Concepts

**Master Class 1 (DP Master Class 1)**
- Primary automation controller (PLC/DCS)
- Performs **cyclic, deterministic** data exchange with slaves
- Uses high-priority function codes (FC_SRD_HIGH: 0x0D)
- Typical cycle times: 1-10ms
- Handles real-time process I/O (sensors, actuators)

**Master Class 2 (DP Master Class 2)**  
- Engineering/diagnostic tool (HMI, configuration software)
- Performs **acyclic, event-driven** communication
- Uses MSRD function code (0x5D)
- Reads diagnostics, writes parameters, downloads configurations
- Does NOT interfere with Class 1 cyclic traffic

### Communication Flow
1. **Class 1 Cycle**: Master sends output data → Slave responds with input data → Repeat
2. **Class 2 Requests**: Master sends diagnostic/parameter request → Slave responds → Transaction complete

### Code Implementation Highlights

**C Implementation**: Low-level frame construction with manual memory management and checksum calculation

**C++ Implementation**: Object-oriented design with `ProfibusFrame`, `SlaveDevice`, and separate master classes using modern C++ features (smart pointers, vectors)

**Rust Implementation**: Type-safe enums, thread-safe `Arc<Mutex<>>` for slave devices, comprehensive error handling with `Result<T, E>`

All three implementations demonstrate:
- Frame serialization with proper Profibus telegram structure
- Cyclic data exchange (Class 1)
- Acyclic diagnostic/parameter operations (Class 2)
- Concurrent operation of both master types without conflicts

This dual-master architecture enables Profibus to maintain deterministic real-time control while allowing non-intrusive diagnostics and configuration.