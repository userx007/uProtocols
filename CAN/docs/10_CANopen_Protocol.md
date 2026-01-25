# CAN (Controller Area Network) and CANopen Protocol

## Overview of CAN

**Controller Area Network (CAN)** is a robust vehicle bus standard designed to allow microcontrollers and devices to communicate with each other without a host computer. Originally developed by Bosch in the 1980s for automotive applications, CAN has become widely adopted in industrial automation, medical equipment, and embedded systems.

### Key Features of CAN

- **Multi-master architecture**: Any node can initiate communication when the bus is idle
- **Message-based protocol**: Data is broadcast to all nodes; each node filters messages based on identifiers
- **Priority-based arbitration**: Non-destructive bitwise arbitration ensures high-priority messages are transmitted first
- **Error detection**: Comprehensive error detection and fault confinement mechanisms
- **Reliable**: Built-in acknowledgment, cyclic redundancy check (CRC), and automatic retransmission

### CAN Frame Structure

A standard CAN data frame consists of:
- **Start of Frame (SOF)**: 1 bit indicating frame start
- **Identifier**: 11 bits (standard) or 29 bits (extended) for message priority and identification
- **RTR (Remote Transmission Request)**: 1 bit
- **Control Field**: 6 bits including data length code (DLC)
- **Data Field**: 0-8 bytes of payload
- **CRC**: 15 bits for error detection
- **ACK**: 2 bits for acknowledgment
- **End of Frame (EOF)**: 7 bits

## CANopen Protocol

**CANopen** is a higher-layer protocol built on top of CAN, standardized as EN 50325-4. It provides a comprehensive communication profile for industrial automation, defining device models, communication objects, and network management services.

### Core Concepts

#### 1. Object Dictionary (OD)
A structured data table in each CANopen device containing all parameters, organized by 16-bit index and 8-bit sub-index. Standard indices include:
- **0x1000-0x1FFF**: Communication parameters
- **0x2000-0x5FFF**: Manufacturer-specific
- **0x6000-0x9FFF**: Standardized device profiles
- **0xA000-0xFFFF**: Reserved

#### 2. Process Data Objects (PDO)
Fast, real-time data transfer without protocol overhead. PDOs can be:
- **TPDO (Transmit PDO)**: Data sent by the device
- **RPDO (Receive PDO)**: Data received by the device

PDOs are event-triggered or time-triggered and support up to 512 PDOs per device.

#### 3. Service Data Objects (SDO)
Confirmed, segmented access to the object dictionary for configuration and parameter reading/writing. SDOs use a client-server model with acknowledgment for reliability.

#### 4. Network Management (NMT)
Controls the state of CANopen devices:
- **Initialization**: Device startup
- **Pre-operational**: Configuration possible, no PDO communication
- **Operational**: Normal operation with PDO communication
- **Stopped**: No communication except NMT

#### 5. Emergency Messages (EMCY)
High-priority, event-triggered error notifications for immediate fault reporting.

## C/C++ Programming Examples

### Basic CAN Frame Transmission (Linux SocketCAN)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int main() {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;
    
    // Create CAN socket
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Specify CAN interface
    strcpy(ifr.ifr_name, "can0");
    ioctl(s, SIOCGIFINDEX, &ifr);
    
    // Bind socket to CAN interface
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    // Prepare CAN frame
    frame.can_id = 0x123;  // Standard 11-bit identifier
    frame.can_dlc = 8;     // Data length
    frame.data[0] = 0x11;
    frame.data[1] = 0x22;
    frame.data[2] = 0x33;
    frame.data[3] = 0x44;
    frame.data[4] = 0x55;
    frame.data[5] = 0x66;
    frame.data[6] = 0x77;
    frame.data[7] = 0x88;
    
    // Send frame
    if (write(s, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
        perror("Write failed");
        return 1;
    }
    
    printf("CAN frame sent successfully\n");
    close(s);
    return 0;
}
```

### CANopen SDO Client (C++)

```cpp
#include <iostream>
#include <cstdint>
#include <vector>

class CANopenSDO {
private:
    int can_socket;
    uint8_t node_id;
    
    // SDO command specifiers
    static constexpr uint8_t SDO_WRITE_REQUEST = 0x22;
    static constexpr uint8_t SDO_READ_REQUEST = 0x40;
    static constexpr uint8_t SDO_WRITE_RESPONSE = 0x60;
    static constexpr uint8_t SDO_READ_RESPONSE = 0x42;
    
public:
    CANopenSDO(int socket, uint8_t node) : can_socket(socket), node_id(node) {}
    
    // Write 32-bit value to object dictionary
    bool writeOD(uint16_t index, uint8_t subindex, uint32_t value) {
        struct can_frame frame;
        
        // SDO TX COB-ID: 0x600 + node_id
        frame.can_id = 0x600 + node_id;
        frame.can_dlc = 8;
        
        // SDO write request (expedited, 4 bytes)
        frame.data[0] = SDO_WRITE_REQUEST | 0x03; // 4 bytes specified
        frame.data[1] = index & 0xFF;              // Index low byte
        frame.data[2] = (index >> 8) & 0xFF;       // Index high byte
        frame.data[3] = subindex;                  // Sub-index
        frame.data[4] = value & 0xFF;              // Data byte 0
        frame.data[5] = (value >> 8) & 0xFF;       // Data byte 1
        frame.data[6] = (value >> 16) & 0xFF;      // Data byte 2
        frame.data[7] = (value >> 24) & 0xFF;      // Data byte 3
        
        if (write(can_socket, &frame, sizeof(frame)) != sizeof(frame)) {
            std::cerr << "Failed to send SDO write request" << std::endl;
            return false;
        }
        
        // Wait for response (simplified - should include timeout)
        if (read(can_socket, &frame, sizeof(frame)) < 0) {
            std::cerr << "Failed to receive SDO response" << std::endl;
            return false;
        }
        
        // Verify response
        if (frame.can_id == (0x580 + node_id) && 
            frame.data[0] == SDO_WRITE_RESPONSE) {
            std::cout << "SDO write successful" << std::endl;
            return true;
        }
        
        return false;
    }
    
    // Read 32-bit value from object dictionary
    bool readOD(uint16_t index, uint8_t subindex, uint32_t& value) {
        struct can_frame frame;
        
        // SDO TX COB-ID: 0x600 + node_id
        frame.can_id = 0x600 + node_id;
        frame.can_dlc = 8;
        
        // SDO read request
        frame.data[0] = SDO_READ_REQUEST;
        frame.data[1] = index & 0xFF;
        frame.data[2] = (index >> 8) & 0xFF;
        frame.data[3] = subindex;
        frame.data[4] = 0x00;
        frame.data[5] = 0x00;
        frame.data[6] = 0x00;
        frame.data[7] = 0x00;
        
        if (write(can_socket, &frame, sizeof(frame)) != sizeof(frame)) {
            std::cerr << "Failed to send SDO read request" << std::endl;
            return false;
        }
        
        // Wait for response
        if (read(can_socket, &frame, sizeof(frame)) < 0) {
            std::cerr << "Failed to receive SDO response" << std::endl;
            return false;
        }
        
        // Extract value from response
        if (frame.can_id == (0x580 + node_id) && 
            frame.data[0] == (SDO_READ_RESPONSE | 0x03)) {
            value = frame.data[4] | 
                   (frame.data[5] << 8) | 
                   (frame.data[6] << 16) | 
                   (frame.data[7] << 24);
            std::cout << "SDO read successful: 0x" << std::hex << value << std::endl;
            return true;
        }
        
        return false;
    }
};

// Example usage
int main() {
    // Assume CAN socket is already initialized
    int can_sock = /* initialize socket */;
    
    CANopenSDO sdo_client(can_sock, 5); // Node ID 5
    
    // Write to object 0x2000, sub-index 0
    uint32_t write_value = 0x12345678;
    sdo_client.writeOD(0x2000, 0x00, write_value);
    
    // Read from object 0x1000, sub-index 0 (Device Type)
    uint32_t device_type;
    sdo_client.readOD(0x1000, 0x00, device_type);
    
    return 0;
}
```

### CANopen PDO Transmission (C)

```c
#include <stdint.h>
#include <string.h>

#define PDO_TRANSMISSION_TYPE_SYNC 0x01
#define PDO_TRANSMISSION_TYPE_ASYNC 0xFE

typedef struct {
    uint32_t cob_id;           // Communication Object ID
    uint8_t transmission_type;  // Synchronous or asynchronous
    uint16_t inhibit_time;      // Minimum time between transmissions
    uint16_t event_timer;       // Periodic transmission interval
} PDO_Config_t;

typedef struct {
    uint8_t data[8];
    uint8_t length;
} PDO_Data_t;

// Send TPDO (Transmit PDO)
int send_tpdo(int can_socket, uint8_t pdo_number, PDO_Data_t* pdo_data) {
    struct can_frame frame;
    
    // TPDO COB-IDs: 0x180, 0x280, 0x380, 0x480 + node_id
    uint16_t base_cob_id[] = {0x180, 0x280, 0x380, 0x480};
    uint8_t node_id = 5; // Example node ID
    
    if (pdo_number > 3) {
        return -1; // Invalid PDO number
    }
    
    frame.can_id = base_cob_id[pdo_number] + node_id;
    frame.can_dlc = pdo_data->length;
    memcpy(frame.data, pdo_data->data, pdo_data->length);
    
    return write(can_socket, &frame, sizeof(frame));
}

// Process received RPDO (Receive PDO)
void process_rpdo(struct can_frame* frame, uint8_t node_id) {
    uint16_t cob_id = frame->can_id;
    
    // Determine which RPDO this is
    uint16_t base_cob_id[] = {0x200, 0x300, 0x400, 0x500};
    
    for (int i = 0; i < 4; i++) {
        if (cob_id == (base_cob_id[i] + node_id)) {
            printf("Received RPDO%d: ", i + 1);
            for (int j = 0; j < frame->can_dlc; j++) {
                printf("%02X ", frame->data[j]);
            }
            printf("\n");
            
            // Process the data according to PDO mapping
            // This is application-specific
            break;
        }
    }
}

// Example: Mapping sensor values to TPDO1
void send_sensor_data(int can_socket) {
    PDO_Data_t pdo;
    
    // Example: Map 4 16-bit sensor values to 8 bytes
    uint16_t sensor1 = 1234;
    uint16_t sensor2 = 5678;
    uint16_t sensor3 = 9012;
    uint16_t sensor4 = 3456;
    
    pdo.data[0] = sensor1 & 0xFF;
    pdo.data[1] = (sensor1 >> 8) & 0xFF;
    pdo.data[2] = sensor2 & 0xFF;
    pdo.data[3] = (sensor2 >> 8) & 0xFF;
    pdo.data[4] = sensor3 & 0xFF;
    pdo.data[5] = (sensor3 >> 8) & 0xFF;
    pdo.data[6] = sensor4 & 0xFF;
    pdo.data[7] = (sensor4 >> 8) & 0xFF;
    pdo.length = 8;
    
    send_tpdo(can_socket, 0, &pdo); // Send TPDO1
}
```

## Rust Programming Examples

### Basic CAN Communication with socketcan Crate

```rust
use socketcan::{CANSocket, CANFrame};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open CAN interface
    let socket = CANSocket::open("can0")?;
    
    // Set timeout
    socket.set_read_timeout(Duration::from_millis(100))?;
    
    // Create and send CAN frame
    let frame = CANFrame::new(
        0x123,                                    // CAN ID
        &[0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88],  // Data
        false,                                    // Not RTR
        false,                                    // Standard frame
    )?;
    
    socket.write_frame(&frame)?;
    println!("CAN frame sent: ID=0x{:X}", frame.id());
    
    // Read CAN frames
    loop {
        match socket.read_frame() {
            Ok(frame) => {
                println!("Received frame: ID=0x{:X}, Data={:?}", 
                         frame.id(), frame.data());
            }
            Err(e) => {
                eprintln!("Error reading frame: {}", e);
                break;
            }
        }
    }
    
    Ok(())
}
```

### CANopen SDO Client in Rust

```rust
use socketcan::{CANSocket, CANFrame};
use std::time::Duration;

pub struct CANopenSDO {
    socket: CANSocket,
    node_id: u8,
}

#[derive(Debug)]
pub enum SDOError {
    SocketError(std::io::Error),
    Timeout,
    InvalidResponse,
    AbortTransfer(u32),
}

impl From<std::io::Error> for SDOError {
    fn from(err: std::io::Error) -> Self {
        SDOError::SocketError(err)
    }
}

impl CANopenSDO {
    const SDO_WRITE_REQUEST: u8 = 0x22;
    const SDO_READ_REQUEST: u8 = 0x40;
    const SDO_WRITE_RESPONSE: u8 = 0x60;
    const SDO_READ_RESPONSE: u8 = 0x42;
    const SDO_ABORT: u8 = 0x80;
    
    pub fn new(interface: &str, node_id: u8) -> Result<Self, SDOError> {
        let socket = CANSocket::open(interface)?;
        socket.set_read_timeout(Duration::from_millis(1000))?;
        
        Ok(CANopenSDO { socket, node_id })
    }
    
    pub fn write_u32(&self, index: u16, subindex: u8, value: u32) -> Result<(), SDOError> {
        // Construct SDO write request
        let mut data = [0u8; 8];
        data[0] = Self::SDO_WRITE_REQUEST | 0x03; // Expedited, 4 bytes
        data[1] = (index & 0xFF) as u8;            // Index low
        data[2] = ((index >> 8) & 0xFF) as u8;     // Index high
        data[3] = subindex;                        // Sub-index
        data[4] = (value & 0xFF) as u8;            // Data byte 0
        data[5] = ((value >> 8) & 0xFF) as u8;     // Data byte 1
        data[6] = ((value >> 16) & 0xFF) as u8;    // Data byte 2
        data[7] = ((value >> 24) & 0xFF) as u8;    // Data byte 3
        
        let tx_cob_id = 0x600 + self.node_id as u32;
        let frame = CANFrame::new(tx_cob_id, &data, false, false)
            .map_err(|e| SDOError::SocketError(e.into()))?;
        
        self.socket.write_frame(&frame)?;
        
        // Wait for response
        let response = self.socket.read_frame()?;
        let rx_cob_id = 0x580 + self.node_id as u32;
        
        if response.id() == rx_cob_id {
            match response.data()[0] {
                Self::SDO_WRITE_RESPONSE => Ok(()),
                Self::SDO_ABORT => {
                    let abort_code = u32::from_le_bytes([
                        response.data()[4],
                        response.data()[5],
                        response.data()[6],
                        response.data()[7],
                    ]);
                    Err(SDOError::AbortTransfer(abort_code))
                }
                _ => Err(SDOError::InvalidResponse),
            }
        } else {
            Err(SDOError::InvalidResponse)
        }
    }
    
    pub fn read_u32(&self, index: u16, subindex: u8) -> Result<u32, SDOError> {
        // Construct SDO read request
        let mut data = [0u8; 8];
        data[0] = Self::SDO_READ_REQUEST;
        data[1] = (index & 0xFF) as u8;
        data[2] = ((index >> 8) & 0xFF) as u8;
        data[3] = subindex;
        
        let tx_cob_id = 0x600 + self.node_id as u32;
        let frame = CANFrame::new(tx_cob_id, &data, false, false)
            .map_err(|e| SDOError::SocketError(e.into()))?;
        
        self.socket.write_frame(&frame)?;
        
        // Wait for response
        let response = self.socket.read_frame()?;
        let rx_cob_id = 0x580 + self.node_id as u32;
        
        if response.id() == rx_cob_id {
            match response.data()[0] & 0xE0 {
                0x40 => { // SDO read response
                    let value = u32::from_le_bytes([
                        response.data()[4],
                        response.data()[5],
                        response.data()[6],
                        response.data()[7],
                    ]);
                    Ok(value)
                }
                0x80 => { // Abort
                    let abort_code = u32::from_le_bytes([
                        response.data()[4],
                        response.data()[5],
                        response.data()[6],
                        response.data()[7],
                    ]);
                    Err(SDOError::AbortTransfer(abort_code))
                }
                _ => Err(SDOError::InvalidResponse),
            }
        } else {
            Err(SDOError::InvalidResponse)
        }
    }
}

// Example usage
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let sdo = CANopenSDO::new("can0", 5)?;
    
    // Write value to object dictionary
    sdo.write_u32(0x2000, 0x00, 0x12345678)?;
    println!("SDO write successful");
    
    // Read device type (object 0x1000)
    let device_type = sdo.read_u32(0x1000, 0x00)?;
    println!("Device type: 0x{:08X}", device_type);
    
    Ok(())
}
```

### CANopen NMT State Machine

```rust
use socketcan::{CANSocket, CANFrame};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum NMTState {
    Initialization,
    PreOperational,
    Operational,
    Stopped,
}

#[derive(Debug, Clone, Copy)]
pub enum NMTCommand {
    Start = 0x01,
    Stop = 0x02,
    EnterPreOperational = 0x80,
    ResetNode = 0x81,
    ResetCommunication = 0x82,
}

pub struct CANopenNMT {
    socket: CANSocket,
}

impl CANopenNMT {
    const NMT_COB_ID: u32 = 0x000;
    
    pub fn new(interface: &str) -> Result<Self, std::io::Error> {
        let socket = CANSocket::open(interface)?;
        Ok(CANopenNMT { socket })
    }
    
    pub fn send_command(&self, node_id: u8, command: NMTCommand) -> Result<(), std::io::Error> {
        let data = [command as u8, node_id];
        let frame = CANFrame::new(Self::NMT_COB_ID, &data, false, false)?;
        self.socket.write_frame(&frame)?;
        Ok(())
    }
    
    pub fn start_node(&self, node_id: u8) -> Result<(), std::io::Error> {
        self.send_command(node_id, NMTCommand::Start)
    }
    
    pub fn stop_node(&self, node_id: u8) -> Result<(), std::io::Error> {
        self.send_command(node_id, NMTCommand::Stop)
    }
    
    pub fn enter_preop(&self, node_id: u8) -> Result<(), std::io::Error> {
        self.send_command(node_id, NMTCommand::EnterPreOperational)
    }
    
    pub fn reset_node(&self, node_id: u8) -> Result<(), std::io::Error> {
        self.send_command(node_id, NMTCommand::ResetNode)
    }
    
    pub fn reset_communication(&self, node_id: u8) -> Result<(), std::io::Error> {
        self.send_command(node_id, NMTCommand::ResetCommunication)
    }
}

// Example usage
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let nmt = CANopenNMT::new("can0")?;
    
    // Reset node 5
    nmt.reset_node(5)?;
    std::thread::sleep(std::time::Duration::from_millis(100));
    
    // Enter pre-operational state
    nmt.enter_preop(5)?;
    println!("Node 5 in pre-operational state");
    
    // Configure device using SDO here...
    
    // Start node (enter operational state)
    nmt.start_node(5)?;
    println!("Node 5 started (operational)");
    
    Ok(())
}
```

## Summary

**CAN (Controller Area Network)** is a robust, multi-master serial bus standard designed for reliable communication in noisy environments. It uses message-based communication with priority-based arbitration, making it ideal for real-time applications in automotive, industrial automation, and embedded systems.

**CANopen** extends CAN with a comprehensive higher-layer protocol that provides:

1. **Object Dictionary**: Standardized data organization for device parameters and configuration
2. **PDOs (Process Data Objects)**: Fast, real-time data exchange with minimal overhead for cyclic or event-driven communication
3. **SDOs (Service Data Objects)**: Confirmed access to object dictionary entries for configuration and diagnostics
4. **NMT (Network Management)**: State machine control for device initialization, configuration, and operation
5. **Emergency Messages**: High-priority error reporting for immediate fault notification

The protocol is widely used in industrial automation (PLCopen, CiA specifications), medical equipment, and motion control systems. Both C/C++ and Rust provide excellent support for CAN/CANopen development through SocketCAN on Linux, with Rust offering additional memory safety guarantees and modern language features. The code examples demonstrate core functionality including frame transmission, SDO client operations, PDO handling, and NMT state management, providing a foundation for building robust CANopen applications.