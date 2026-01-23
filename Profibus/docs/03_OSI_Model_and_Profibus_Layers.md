# OSI Model and Profibus Layers

## Detailed Description

Profibus (Process Field Bus) is a standardized industrial communication protocol that implements a selective subset of the OSI (Open Systems Interconnection) model. Unlike general-purpose networking protocols that implement all seven OSI layers, Profibus focuses on layers 1, 2, and 7, which are most relevant for industrial automation and control systems.

### Why Only Three Layers?

Industrial communication systems prioritize determinism, real-time performance, and efficiency over feature richness. By implementing only the essential layers, Profibus achieves:

- **Minimal latency** - Critical for time-sensitive control loops
- **Predictable timing** - Essential for synchronized industrial processes
- **Reduced overhead** - More bandwidth available for actual data
- **Simplified implementation** - Lower cost and complexity

### Layer 1: Physical Layer

The Physical Layer defines the electrical and mechanical characteristics of the network. Profibus supports multiple physical layer variants:

**RS-485 Transmission:**
- Data rates: 9.6 kbps to 12 Mbps
- Distance: Up to 1200m (at 93.75 kbps) without repeaters
- Topology: Bus topology with active termination
- Connector: 9-pin D-sub connector

**Fiber Optic (Profibus-DP-FO):**
- Electromagnetic immunity
- Extended distances up to 15 km between segments
- Higher noise resistance in harsh environments

The physical layer handles bit encoding (NRZ - Non-Return to Zero), voltage levels, and cable specifications.

### Layer 2: Data Link Layer (FDL - Field Data Link)

The Data Link Layer is the heart of Profibus communication, implementing the token-passing protocol and master-slave communication:

**Token Passing Mechanism:**
- Multiple masters can coexist on the network
- A logical token circulates among masters
- Only the token holder can initiate communication
- Ensures deterministic access without collisions

**Frame Types:**
- SD1: Request frames without data
- SD2: Variable-length data frames
- SD3: Fixed-length data frames (8 bytes)
- SD4: Single character acknowledgment
- SC: Short acknowledgment

**Error Detection:**
- Hamming distance for frame identification
- Checksum (FCS - Frame Check Sequence)
- Timeout mechanisms

### Layer 7: Application Layer (FMS/DP/PA)

The Application Layer defines how data is structured and interpreted. Profibus has three main profiles:

**Profibus-DP (Decentralized Peripherals):**
- Optimized for high-speed cyclic data exchange
- Used for sensors, actuators, and I/O modules
- Cycle times as low as 1-2 ms
- Master-slave architecture

**Profibus-FMS (Fieldbus Message Specification):**
- Acyclic communication for complex tasks
- Event-driven messaging
- Used for cell-level communication

**Profibus-PA (Process Automation):**
- Intrinsically safe for hazardous areas
- IEC 61158-2 physical layer (MBP - Manchester Bus Powered)
- Supports power over the bus

## Code Examples

### C/C++ Implementation

```cpp
// Profibus OSI Model Implementation in C++
// Demonstrates Layer 1 (Physical), Layer 2 (Data Link), and Layer 7 (Application)

#include <cstdint>
#include <cstring>
#include <vector>
#include <array>

// ==================== LAYER 1: PHYSICAL LAYER ====================

enum class BaudRate {
    BAUD_9600 = 9600,
    BAUD_19200 = 19200,
    BAUD_93750 = 93750,
    BAUD_187500 = 187500,
    BAUD_500000 = 500000,
    BAUD_1500000 = 1500000,
    BAUD_12000000 = 12000000
};

class PhysicalLayer {
private:
    BaudRate baudRate;
    uint8_t stationAddress;
    
public:
    PhysicalLayer(BaudRate rate, uint8_t address) 
        : baudRate(rate), stationAddress(address) {}
    
    // Simulate bit transmission (in real hardware, this would interact with UART/SPI)
    bool transmitBit(bool bit) {
        // RS-485 differential signaling simulation
        // In real implementation: configure GPIO pins, timing, etc.
        return true; // Success
    }
    
    // Calculate maximum segment length based on baud rate
    uint16_t getMaxSegmentLength() const {
        switch(baudRate) {
            case BaudRate::BAUD_9600: return 1200;
            case BaudRate::BAUD_19200: return 1200;
            case BaudRate::BAUD_93750: return 1200;
            case BaudRate::BAUD_187500: return 1000;
            case BaudRate::BAUD_500000: return 400;
            case BaudRate::BAUD_1500000: return 200;
            case BaudRate::BAUD_12000000: return 100;
            default: return 0;
        }
    }
};

// ==================== LAYER 2: DATA LINK LAYER ====================

enum class FrameType : uint8_t {
    SD1 = 0x10,  // Request frame without data
    SD2 = 0x68,  // Variable length data frame
    SD3 = 0xA2,  // Fixed length data frame (8 bytes)
    SD4 = 0xDC,  // Token frame
    SC = 0xE5    // Short acknowledgment
};

enum class FrameControl : uint8_t {
    REQUEST_FDL_STATUS = 0x49,
    REQUEST_DATA_LOW = 0x5C,
    REQUEST_DATA_HIGH = 0x5D,
    SEND_DATA_NO_ACK = 0x5E,
    SEND_DATA_WITH_ACK = 0x5F,
    TOKEN_PASS = 0xDC
};

struct ProfibusFrame {
    FrameType frameType;
    uint8_t destinationAddress;
    uint8_t sourceAddress;
    FrameControl functionCode;
    std::vector<uint8_t> data;
    uint8_t frameChecksum;
    
    // Calculate FCS (Frame Check Sequence)
    uint8_t calculateFCS() const {
        uint8_t fcs = destinationAddress + sourceAddress + 
                      static_cast<uint8_t>(functionCode);
        for(auto byte : data) {
            fcs += byte;
        }
        return fcs;
    }
};

class DataLinkLayer {
private:
    uint8_t stationAddress;
    bool hasToken;
    uint8_t tokenRotationTime; // ms
    
public:
    DataLinkLayer(uint8_t address) 
        : stationAddress(address), hasToken(false), tokenRotationTime(10) {}
    
    // Build a Profibus frame (SD2 - variable length)
    ProfibusFrame buildDataFrame(uint8_t destAddr, 
                                  const std::vector<uint8_t>& payload) {
        ProfibusFrame frame;
        frame.frameType = FrameType::SD2;
        frame.destinationAddress = destAddr;
        frame.sourceAddress = stationAddress;
        frame.functionCode = FrameControl::SEND_DATA_WITH_ACK;
        frame.data = payload;
        frame.frameChecksum = frame.calculateFCS();
        return frame;
    }
    
    // Serialize frame to byte array
    std::vector<uint8_t> serializeFrame(const ProfibusFrame& frame) {
        std::vector<uint8_t> bytes;
        
        if(frame.frameType == FrameType::SD2) {
            bytes.push_back(static_cast<uint8_t>(FrameType::SD2));
            uint8_t len = frame.data.size() + 3; // DA + SA + FC
            bytes.push_back(len);
            bytes.push_back(len); // Repeated length
            bytes.push_back(static_cast<uint8_t>(FrameType::SD2));
            bytes.push_back(frame.destinationAddress);
            bytes.push_back(frame.sourceAddress);
            bytes.push_back(static_cast<uint8_t>(frame.functionCode));
            
            for(auto byte : frame.data) {
                bytes.push_back(byte);
            }
            
            bytes.push_back(frame.frameChecksum);
            bytes.push_back(0x16); // End delimiter
        }
        
        return bytes;
    }
    
    // Token management
    void passToken(uint8_t nextStation) {
        hasToken = false;
        // Send token frame to next station
    }
    
    bool validateFrame(const ProfibusFrame& frame) {
        return frame.calculateFCS() == frame.frameChecksum;
    }
};

// ==================== LAYER 7: APPLICATION LAYER ====================

// Profibus-DP (Decentralized Peripherals) implementation
enum class DPServiceType : uint8_t {
    READ_INPUT = 0x01,
    READ_OUTPUT = 0x02,
    WRITE_OUTPUT = 0x03,
    DIAGNOSTIC_REQUEST = 0x3D,
    SLAVE_DIAGNOSIS = 0x3E
};

struct DPCyclicData {
    uint8_t slaveAddress;
    std::vector<uint8_t> inputData;   // Process input image
    std::vector<uint8_t> outputData;  // Process output image
};

class ApplicationLayer_DP {
private:
    std::vector<DPCyclicData> slaveData;
    uint16_t cycleTime; // microseconds
    
public:
    ApplicationLayer_DP(uint16_t cycleTimeUs = 2000) 
        : cycleTime(cycleTimeUs) {}
    
    // Master reads input data from slave
    std::vector<uint8_t> readSlaveInputs(uint8_t slaveAddr) {
        for(auto& slave : slaveData) {
            if(slave.slaveAddress == slaveAddr) {
                return slave.inputData;
            }
        }
        return {};
    }
    
    // Master writes output data to slave
    bool writeSlaveOutputs(uint8_t slaveAddr, 
                          const std::vector<uint8_t>& outputs) {
        for(auto& slave : slaveData) {
            if(slave.slaveAddress == slaveAddr) {
                slave.outputData = outputs;
                return true;
            }
        }
        return false;
    }
    
    // Register a DP slave
    void registerSlave(uint8_t address, size_t inputBytes, 
                      size_t outputBytes) {
        DPCyclicData slave;
        slave.slaveAddress = address;
        slave.inputData.resize(inputBytes, 0);
        slave.outputData.resize(outputBytes, 0);
        slaveData.push_back(slave);
    }
    
    // Diagnostic data structure
    struct DiagnosticData {
        uint8_t stationStatus1;
        uint8_t stationStatus2;
        uint8_t stationStatus3;
        uint8_t masterAddress;
        uint16_t identNumber;
        
        bool isOperational() const {
            return (stationStatus1 & 0x01) == 0; // Bit 0: 0=OK, 1=Not ready
        }
    };
    
    DiagnosticData requestDiagnostic(uint8_t slaveAddr) {
        DiagnosticData diag{};
        // In real implementation, send diagnostic request frame
        // and parse response
        diag.stationStatus1 = 0x00; // Operational
        diag.masterAddress = 0x02;
        diag.identNumber = 0x1234;
        return diag;
    }
};

// ==================== COMPLETE PROFIBUS STACK ====================

class ProfibusStack {
private:
    PhysicalLayer physical;
    DataLinkLayer dataLink;
    ApplicationLayer_DP application;
    
public:
    ProfibusStack(uint8_t stationAddr, BaudRate rate)
        : physical(rate, stationAddr),
          dataLink(stationAddr),
          application(2000) {}
    
    // High-level function: Send cyclic data to slave
    bool sendCyclicData(uint8_t slaveAddr, 
                       const std::vector<uint8_t>& data) {
        // Layer 7: Prepare application data
        application.writeSlaveOutputs(slaveAddr, data);
        
        // Layer 2: Build frame
        auto frame = dataLink.buildDataFrame(slaveAddr, data);
        
        // Layer 2: Serialize
        auto bytes = dataLink.serializeFrame(frame);
        
        // Layer 1: Transmit (simplified)
        for(auto byte : bytes) {
            for(int bit = 0; bit < 8; bit++) {
                physical.transmitBit((byte >> bit) & 0x01);
            }
        }
        
        return true;
    }
    
    // Register a slave device
    void addSlave(uint8_t address, size_t inBytes, size_t outBytes) {
        application.registerSlave(address, inBytes, outBytes);
    }
};

// ==================== USAGE EXAMPLE ====================

int main() {
    // Create Profibus master at station 2, 500 kbps
    ProfibusStack master(2, BaudRate::BAUD_500000);
    
    // Register slave at address 5 with 4 input bytes, 2 output bytes
    master.addSlave(5, 4, 2);
    
    // Send cyclic output data to slave
    std::vector<uint8_t> outputData = {0xAA, 0x55};
    master.sendCyclicData(5, outputData);
    
    return 0;
}
```

### Rust Implementation## Summary

```rust
// Profibus OSI Model Implementation in Rust
// Demonstrates Layer 1 (Physical), Layer 2 (Data Link), and Layer 7 (Application)

use std::collections::HashMap;

// ==================== LAYER 1: PHYSICAL LAYER ====================

#[derive(Debug, Clone, Copy)]
pub enum BaudRate {
    Baud9600 = 9600,
    Baud19200 = 19200,
    Baud93750 = 93750,
    Baud187500 = 187500,
    Baud500000 = 500000,
    Baud1500000 = 1500000,
    Baud12000000 = 12000000,
}

pub struct PhysicalLayer {
    baud_rate: BaudRate,
    station_address: u8,
}

impl PhysicalLayer {
    pub fn new(baud_rate: BaudRate, station_address: u8) -> Self {
        PhysicalLayer {
            baud_rate,
            station_address,
        }
    }
    
    /// Simulate bit transmission over RS-485
    pub fn transmit_bit(&self, bit: bool) -> Result<(), &'static str> {
        // In real hardware: configure UART/GPIO pins
        // For simulation, we just return success
        Ok(())
    }
    
    /// Calculate maximum segment length based on baud rate (in meters)
    pub fn max_segment_length(&self) -> u16 {
        match self.baud_rate {
            BaudRate::Baud9600 | BaudRate::Baud19200 | BaudRate::Baud93750 => 1200,
            BaudRate::Baud187500 => 1000,
            BaudRate::Baud500000 => 400,
            BaudRate::Baud1500000 => 200,
            BaudRate::Baud12000000 => 100,
        }
    }
}

// ==================== LAYER 2: DATA LINK LAYER ====================

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum FrameType {
    SD1 = 0x10,  // Request frame without data
    SD2 = 0x68,  // Variable length data frame
    SD3 = 0xA2,  // Fixed length data frame (8 bytes)
    SD4 = 0xDC,  // Token frame
    SC = 0xE5,   // Short acknowledgment
}

#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum FrameControl {
    RequestFdlStatus = 0x49,
    RequestDataLow = 0x5C,
    RequestDataHigh = 0x5D,
    SendDataNoAck = 0x5E,
    SendDataWithAck = 0x5F,
    TokenPass = 0xDC,
}

#[derive(Debug, Clone)]
pub struct ProfibusFrame {
    pub frame_type: FrameType,
    pub destination_address: u8,
    pub source_address: u8,
    pub function_code: FrameControl,
    pub data: Vec<u8>,
    pub frame_checksum: u8,
}

impl ProfibusFrame {
    /// Calculate Frame Check Sequence (FCS)
    pub fn calculate_fcs(&self) -> u8 {
        let mut fcs = self.destination_address
            .wrapping_add(self.source_address)
            .wrapping_add(self.function_code as u8);
        
        for &byte in &self.data {
            fcs = fcs.wrapping_add(byte);
        }
        
        fcs
    }
    
    /// Validate frame integrity
    pub fn is_valid(&self) -> bool {
        self.calculate_fcs() == self.frame_checksum
    }
}

pub struct DataLinkLayer {
    station_address: u8,
    has_token: bool,
    token_rotation_time_ms: u8,
}

impl DataLinkLayer {
    pub fn new(station_address: u8) -> Self {
        DataLinkLayer {
            station_address,
            has_token: false,
            token_rotation_time_ms: 10,
        }
    }
    
    /// Build a variable-length data frame (SD2)
    pub fn build_data_frame(&self, dest_addr: u8, payload: Vec<u8>) -> ProfibusFrame {
        let mut frame = ProfibusFrame {
            frame_type: FrameType::SD2,
            destination_address: dest_addr,
            source_address: self.station_address,
            function_code: FrameControl::SendDataWithAck,
            data: payload,
            frame_checksum: 0,
        };
        
        frame.frame_checksum = frame.calculate_fcs();
        frame
    }
    
    /// Serialize frame to byte vector
    pub fn serialize_frame(&self, frame: &ProfibusFrame) -> Vec<u8> {
        let mut bytes = Vec::new();
        
        match frame.frame_type {
            FrameType::SD2 => {
                bytes.push(FrameType::SD2 as u8);
                let len = (frame.data.len() + 3) as u8; // DA + SA + FC
                bytes.push(len);
                bytes.push(len); // Repeated length
                bytes.push(FrameType::SD2 as u8);
                bytes.push(frame.destination_address);
                bytes.push(frame.source_address);
                bytes.push(frame.function_code as u8);
                bytes.extend_from_slice(&frame.data);
                bytes.push(frame.frame_checksum);
                bytes.push(0x16); // End delimiter
            }
            FrameType::SD3 => {
                bytes.push(FrameType::SD3 as u8);
                bytes.push(frame.destination_address);
                bytes.push(frame.source_address);
                bytes.push(frame.function_code as u8);
                bytes.extend_from_slice(&frame.data[..8.min(frame.data.len())]);
                bytes.push(frame.frame_checksum);
                bytes.push(0x16);
            }
            _ => {} // Handle other frame types as needed
        }
        
        bytes
    }
    
    /// Pass token to next station
    pub fn pass_token(&mut self, next_station: u8) {
        self.has_token = false;
        // In real implementation: send token frame
    }
    
    pub fn has_token(&self) -> bool {
        self.has_token
    }
}

// ==================== LAYER 7: APPLICATION LAYER ====================

#[derive(Debug, Clone, Copy)]
#[repr(u8)]
pub enum DPServiceType {
    ReadInput = 0x01,
    ReadOutput = 0x02,
    WriteOutput = 0x03,
    DiagnosticRequest = 0x3D,
    SlaveDiagnosis = 0x3E,
}

#[derive(Debug, Clone)]
pub struct DPCyclicData {
    pub slave_address: u8,
    pub input_data: Vec<u8>,  // Process input image
    pub output_data: Vec<u8>, // Process output image
}

#[derive(Debug, Clone, Copy)]
pub struct DiagnosticData {
    pub station_status_1: u8,
    pub station_status_2: u8,
    pub station_status_3: u8,
    pub master_address: u8,
    pub ident_number: u16,
}

impl DiagnosticData {
    pub fn is_operational(&self) -> bool {
        (self.station_status_1 & 0x01) == 0 // Bit 0: 0=OK, 1=Not ready
    }
    
    pub fn has_config_fault(&self) -> bool {
        (self.station_status_1 & 0x02) != 0
    }
    
    pub fn has_ext_diagnostic(&self) -> bool {
        (self.station_status_1 & 0x04) != 0
    }
}

pub struct ApplicationLayerDP {
    slave_data: HashMap<u8, DPCyclicData>,
    cycle_time_us: u16,
}

impl ApplicationLayerDP {
    pub fn new(cycle_time_us: u16) -> Self {
        ApplicationLayerDP {
            slave_data: HashMap::new(),
            cycle_time_us,
        }
    }
    
    /// Register a DP slave device
    pub fn register_slave(&mut self, address: u8, input_bytes: usize, output_bytes: usize) {
        let slave = DPCyclicData {
            slave_address: address,
            input_data: vec![0; input_bytes],
            output_data: vec![0; output_bytes],
        };
        self.slave_data.insert(address, slave);
    }
    
    /// Read input data from slave
    pub fn read_slave_inputs(&self, slave_addr: u8) -> Option<Vec<u8>> {
        self.slave_data
            .get(&slave_addr)
            .map(|slave| slave.input_data.clone())
    }
    
    /// Write output data to slave
    pub fn write_slave_outputs(&mut self, slave_addr: u8, outputs: Vec<u8>) -> Result<(), &'static str> {
        match self.slave_data.get_mut(&slave_addr) {
            Some(slave) => {
                slave.output_data = outputs;
                Ok(())
            }
            None => Err("Slave not found"),
        }
    }
    
    /// Request diagnostic information from slave
    pub fn request_diagnostic(&self, slave_addr: u8) -> DiagnosticData {
        // In real implementation, send diagnostic request frame
        // and parse response
        DiagnosticData {
            station_status_1: 0x00, // Operational
            station_status_2: 0x00,
            station_status_3: 0x00,
            master_address: 0x02,
            ident_number: 0x1234,
        }
    }
    
    pub fn get_cycle_time(&self) -> u16 {
        self.cycle_time_us
    }
}

// ==================== COMPLETE PROFIBUS STACK ====================

pub struct ProfibusStack {
    physical: PhysicalLayer,
    data_link: DataLinkLayer,
    application: ApplicationLayerDP,
}

impl ProfibusStack {
    pub fn new(station_addr: u8, baud_rate: BaudRate) -> Self {
        ProfibusStack {
            physical: PhysicalLayer::new(baud_rate, station_addr),
            data_link: DataLinkLayer::new(station_addr),
            application: ApplicationLayerDP::new(2000), // 2ms cycle time
        }
    }
    
    /// Add a slave device to the network
    pub fn add_slave(&mut self, address: u8, input_bytes: usize, output_bytes: usize) {
        self.application.register_slave(address, input_bytes, output_bytes);
    }
    
    /// Send cyclic data to slave (complete stack traversal)
    pub fn send_cyclic_data(&mut self, slave_addr: u8, data: Vec<u8>) -> Result<(), &'static str> {
        // Layer 7: Prepare application data
        self.application.write_slave_outputs(slave_addr, data.clone())?;
        
        // Layer 2: Build frame
        let frame = self.data_link.build_data_frame(slave_addr, data);
        
        // Layer 2: Validate
        if !frame.is_valid() {
            return Err("Invalid frame checksum");
        }
        
        // Layer 2: Serialize
        let bytes = self.data_link.serialize_frame(&frame);
        
        // Layer 1: Transmit bit by bit
        for byte in bytes {
            for bit_pos in 0..8 {
                let bit = (byte >> bit_pos) & 0x01 != 0;
                self.physical.transmit_bit(bit)?;
            }
        }
        
        Ok(())
    }
    
    /// Read inputs from slave
    pub fn read_slave_inputs(&self, slave_addr: u8) -> Option<Vec<u8>> {
        self.application.read_slave_inputs(slave_addr)
    }
    
    /// Get diagnostic information
    pub fn get_diagnostics(&self, slave_addr: u8) -> DiagnosticData {
        self.application.request_diagnostic(slave_addr)
    }
}

// ==================== USAGE EXAMPLE ====================

fn main() {
    // Create Profibus master at station 2 with 500 kbps
    let mut master = ProfibusStack::new(2, BaudRate::Baud500000);
    
    // Register slave at address 5: 4 input bytes, 2 output bytes
    master.add_slave(5, 4, 2);
    
    // Send cyclic output data to slave
    let output_data = vec![0xAA, 0x55];
    match master.send_cyclic_data(5, output_data) {
        Ok(_) => println!("Data sent successfully"),
        Err(e) => eprintln!("Error: {}", e),
    }
    
    // Read diagnostic data
    let diag = master.get_diagnostics(5);
    println!("Slave operational: {}", diag.is_operational());
    
    // Attempt to read inputs
    if let Some(inputs) = master.read_slave_inputs(5) {
        println!("Input data: {:?}", inputs);
    }
}
```

**Profibus implements a selective three-layer OSI model** optimized for industrial automation:

**Layer 1 (Physical)** defines the electrical characteristics using RS-485 or fiber optic transmission, supporting data rates from 9.6 kbps to 12 Mbps with distances up to 1200m depending on baud rate.

**Layer 2 (Data Link/FDL)** implements deterministic communication through token-passing among masters and master-slave communication. It defines multiple frame types (SD1, SD2, SD3, SD4, SC) with error detection via checksums and provides collision-free medium access.

**Layer 7 (Application)** comes in three profiles: Profibus-DP for high-speed cyclic I/O exchange (1-2ms cycles), Profibus-FMS for complex acyclic messaging, and Profibus-PA for process automation in hazardous areas.

By omitting OSI layers 3-6 (Network, Transport, Session, Presentation), Profibus achieves the deterministic, real-time performance required for industrial control systems while maintaining simplicity and efficiency. The code examples demonstrate how each layer builds upon the previous one, from raw bit transmission through frame construction to application-level cyclic data exchange and diagnostics.