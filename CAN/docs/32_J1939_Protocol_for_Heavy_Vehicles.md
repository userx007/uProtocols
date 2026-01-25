# CAN (Controller Area Network) and J1939 Protocol

## CAN Bus Overview

CAN is a robust vehicle bus standard designed to allow microcontrollers and devices to communicate with each other without a host computer. It was originally developed by Bosch for automotive applications.

### Key Characteristics

- **Multi-master, message-based protocol**: Any node can transmit when the bus is idle
- **Differential signaling**: Uses CAN_H and CAN_L lines (typically 2.5V ± 2V)
- **Bitrates**: Common rates are 125 kbps, 250 kbps, 500 kbps, and 1 Mbps
- **Message priority**: Lower identifier values have higher priority
- **Error detection**: CRC, frame checks, acknowledgment, and bit monitoring
- **Maximum bus length**: ~40m at 1 Mbps, ~500m at 125 kbps

### CAN Frame Structure

**Standard CAN 2.0A (11-bit identifier):**
- SOF (Start of Frame): 1 bit
- Identifier: 11 bits
- RTR (Remote Transmission Request): 1 bit
- Control: 6 bits
- Data: 0-8 bytes
- CRC: 16 bits
- ACK: 2 bits
- EOF (End of Frame): 7 bits

**Extended CAN 2.0B (29-bit identifier):**
- Uses 29-bit identifier for more message types

## J1939 Protocol

J1939 is a higher-layer protocol based on CAN bus, specifically designed for heavy-duty vehicles like trucks, buses, and construction equipment. It defines how data is packaged, addressed, and transmitted.

### J1939 Features

- **29-bit CAN identifier** broken down into:
  - Priority (3 bits)
  - Reserved/Data Page (1 bit each)
  - PDU Format (8 bits)
  - PDU Specific (8 bits)
  - Source Address (8 bits)

- **Parameter Group Numbers (PGNs)**: Standardized message identifiers
- **Transport Protocol**: For messages larger than 8 bytes
- **Address claiming**: Dynamic address assignment
- **Diagnostic messages**: Standard fault codes (DTCs)

### Common PGNs

- **PGN 61444 (0xF004)**: Electronic Engine Controller 1 (engine speed, torque)
- **PGN 65265 (0xFEF1)**: Cruise Control/Vehicle Speed
- **PGN 65262 (0xFEEE)**: Engine Temperature
- **PGN 65263 (0xFEEF)**: Engine Fluid Level/Pressure

## C/C++ Programming Examples

### Basic SocketCAN Example (Linux)

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
    
    // Create socket
    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("Socket");
        return 1;
    }
    
    // Specify can0 interface
    strcpy(ifr.ifr_name, "can0");
    ioctl(s, SIOCGIFINDEX, &ifr);
    
    // Bind socket to can0
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind");
        return 1;
    }
    
    // Send a frame
    frame.can_id = 0x123;  // Standard CAN ID
    frame.can_dlc = 8;     // Data length
    frame.data[0] = 0x11;
    frame.data[1] = 0x22;
    frame.data[2] = 0x33;
    frame.data[3] = 0x44;
    frame.data[4] = 0x55;
    frame.data[5] = 0x66;
    frame.data[6] = 0x77;
    frame.data[7] = 0x88;
    
    if (write(s, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
        perror("Write");
        return 1;
    }
    
    printf("CAN frame sent successfully\n");
    
    // Receive a frame
    if (read(s, &frame, sizeof(struct can_frame)) < 0) {
        perror("Read");
        return 1;
    }
    
    printf("Received CAN ID: 0x%X, DLC: %d\n", frame.can_id, frame.can_dlc);
    printf("Data: ");
    for (int i = 0; i < frame.can_dlc; i++) {
        printf("%02X ", frame.data[i]);
    }
    printf("\n");
    
    close(s);
    return 0;
}
```

### J1939 Message Encoding/Decoding (C++)

```cpp
#include <iostream>
#include <cstdint>
#include <iomanip>

class J1939Message {
private:
    uint8_t priority;
    uint8_t dataPage;
    uint8_t pduFormat;
    uint8_t pduSpecific;
    uint8_t sourceAddress;
    
public:
    J1939Message(uint32_t canId) {
        sourceAddress = canId & 0xFF;
        pduSpecific = (canId >> 8) & 0xFF;
        pduFormat = (canId >> 16) & 0xFF;
        dataPage = (canId >> 24) & 0x01;
        priority = (canId >> 26) & 0x07;
    }
    
    J1939Message(uint8_t pri, uint8_t dp, uint8_t pf, uint8_t ps, uint8_t sa)
        : priority(pri), dataPage(dp), pduFormat(pf), 
          pduSpecific(ps), sourceAddress(sa) {}
    
    uint32_t getCanId() const {
        return ((uint32_t)priority << 26) |
               ((uint32_t)dataPage << 24) |
               ((uint32_t)pduFormat << 16) |
               ((uint32_t)pduSpecific << 8) |
               sourceAddress;
    }
    
    uint32_t getPGN() const {
        if (pduFormat < 240) {
            // PDU1 format - destination specific
            return ((uint32_t)dataPage << 16) | 
                   ((uint32_t)pduFormat << 8);
        } else {
            // PDU2 format - broadcast
            return ((uint32_t)dataPage << 16) | 
                   ((uint32_t)pduFormat << 8) | 
                   pduSpecific;
        }
    }
    
    void print() const {
        std::cout << "CAN ID: 0x" << std::hex << std::setw(8) 
                  << std::setfill('0') << getCanId() << std::dec << "\n";
        std::cout << "PGN: " << getPGN() << " (0x" << std::hex 
                  << getPGN() << std::dec << ")\n";
        std::cout << "Priority: " << (int)priority << "\n";
        std::cout << "Source Address: 0x" << std::hex 
                  << (int)sourceAddress << std::dec << "\n";
    }
};

// Decode engine speed from PGN 61444
uint16_t decodeEngineSpeed(const uint8_t* data) {
    // Engine speed is bytes 3-4, 0.125 rpm per bit
    uint16_t rawSpeed = (data[4] << 8) | data[3];
    return rawSpeed; // Return raw value, multiply by 0.125 for actual RPM
}

// Encode engine speed for PGN 61444
void encodeEngineSpeed(uint8_t* data, uint16_t rpm) {
    // Convert RPM to raw value (divide by 0.125)
    uint16_t rawSpeed = rpm * 8; // rpm / 0.125 = rpm * 8
    data[3] = rawSpeed & 0xFF;
    data[4] = (rawSpeed >> 8) & 0xFF;
}

int main() {
    // Example: Engine Controller 1 (PGN 61444)
    // Priority 3, Data Page 0, PF=0xF0, PS=0x04, SA=0x00
    J1939Message msg(3, 0, 0xF0, 0x04, 0x00);
    
    std::cout << "J1939 Message Example:\n";
    msg.print();
    
    // Decode example
    uint8_t rxData[8] = {0xFF, 0xFF, 0xFF, 0x40, 0x0E, 0xFF, 0xFF, 0xFF};
    uint16_t rawSpeed = decodeEngineSpeed(rxData);
    double engineRPM = rawSpeed * 0.125;
    std::cout << "\nDecoded Engine Speed: " << engineRPM << " RPM\n";
    
    // Encode example
    uint8_t txData[8] = {0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF};
    encodeEngineSpeed(txData, 1500); // 1500 RPM
    std::cout << "Encoded data for 1500 RPM: ";
    for (int i = 0; i < 8; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)txData[i] << " ";
    }
    std::cout << std::dec << "\n";
    
    return 0;
}
```

## Rust Programming Examples

### Basic CAN Communication (using socketcan crate)

```rust
use socketcan::{CANSocket, CANFrame, CANFilter};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open CAN socket
    let socket = CANSocket::open("can0")?;
    
    // Set receive timeout
    socket.set_read_timeout(Duration::from_millis(1000))?;
    
    // Send a CAN frame
    let frame = CANFrame::new(0x123, &[0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88], false, false)?;
    socket.write_frame(&frame)?;
    println!("CAN frame sent successfully");
    
    // Apply filter for specific CAN IDs
    let filter = CANFilter::new(0x100, 0x700)?; // Accept IDs 0x100-0x1FF
    socket.set_filter(&[filter])?;
    
    // Receive CAN frames
    loop {
        match socket.read_frame() {
            Ok(frame) => {
                println!("Received CAN ID: 0x{:X}, DLC: {}", frame.id(), frame.data().len());
                print!("Data: ");
                for byte in frame.data() {
                    print!("{:02X} ", byte);
                }
                println!();
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

### J1939 Implementation in Rust

```rust
use std::fmt;

#[derive(Debug, Clone, Copy)]
pub struct J1939Message {
    priority: u8,
    data_page: u8,
    pdu_format: u8,
    pdu_specific: u8,
    source_address: u8,
}

impl J1939Message {
    pub fn new(priority: u8, data_page: u8, pdu_format: u8, 
               pdu_specific: u8, source_address: u8) -> Self {
        J1939Message {
            priority: priority & 0x07,
            data_page: data_page & 0x01,
            pdu_format,
            pdu_specific,
            source_address,
        }
    }
    
    pub fn from_can_id(can_id: u32) -> Self {
        J1939Message {
            source_address: (can_id & 0xFF) as u8,
            pdu_specific: ((can_id >> 8) & 0xFF) as u8,
            pdu_format: ((can_id >> 16) & 0xFF) as u8,
            data_page: ((can_id >> 24) & 0x01) as u8,
            priority: ((can_id >> 26) & 0x07) as u8,
        }
    }
    
    pub fn to_can_id(&self) -> u32 {
        ((self.priority as u32) << 26) |
        ((self.data_page as u32) << 24) |
        ((self.pdu_format as u32) << 16) |
        ((self.pdu_specific as u32) << 8) |
        (self.source_address as u32)
    }
    
    pub fn get_pgn(&self) -> u32 {
        if self.pdu_format < 240 {
            // PDU1 format - destination specific
            ((self.data_page as u32) << 16) | 
            ((self.pdu_format as u32) << 8)
        } else {
            // PDU2 format - broadcast
            ((self.data_page as u32) << 16) | 
            ((self.pdu_format as u32) << 8) | 
            (self.pdu_specific as u32)
        }
    }
    
    pub fn is_broadcast(&self) -> bool {
        self.pdu_format >= 240
    }
    
    pub fn get_destination(&self) -> Option<u8> {
        if self.pdu_format < 240 {
            Some(self.pdu_specific)
        } else {
            None
        }
    }
}

impl fmt::Display for J1939Message {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "CAN ID: 0x{:08X}, PGN: {} (0x{:X}), Priority: {}, SA: 0x{:02X}",
               self.to_can_id(), self.get_pgn(), self.get_pgn(), 
               self.priority, self.source_address)
    }
}

// J1939 data decoding functions
pub struct EngineData {
    pub speed_rpm: f32,
    pub torque_percent: f32,
}

impl EngineData {
    // Decode PGN 61444 - Electronic Engine Controller 1
    pub fn from_eec1(data: &[u8]) -> Option<Self> {
        if data.len() < 8 {
            return None;
        }
        
        // Engine speed - bytes 3-4, 0.125 rpm/bit
        let raw_speed = u16::from_le_bytes([data[3], data[4]]);
        let speed_rpm = if raw_speed == 0xFFFF {
            f32::NAN
        } else {
            raw_speed as f32 * 0.125
        };
        
        // Engine torque - bytes 2, 125% offset, 1% per bit
        let torque_percent = if data[2] == 0xFF {
            f32::NAN
        } else {
            data[2] as f32 - 125.0
        };
        
        Some(EngineData {
            speed_rpm,
            torque_percent,
        })
    }
    
    // Encode to PGN 61444
    pub fn to_eec1(&self) -> [u8; 8] {
        let mut data = [0xFF; 8];
        
        // Encode engine speed
        if self.speed_rpm.is_finite() {
            let raw_speed = (self.speed_rpm / 0.125) as u16;
            data[3..5].copy_from_slice(&raw_speed.to_le_bytes());
        }
        
        // Encode torque
        if self.torque_percent.is_finite() {
            data[2] = (self.torque_percent + 125.0) as u8;
        }
        
        data
    }
}

fn main() {
    // Create J1939 message for Engine Controller 1
    let msg = J1939Message::new(3, 0, 0xF0, 0x04, 0x00);
    println!("{}", msg);
    println!("Is broadcast: {}", msg.is_broadcast());
    
    // Decode engine data
    let rx_data = [0xFF, 0xFF, 0xFF, 0x40, 0x0E, 0xFF, 0xFF, 0xFF];
    if let Some(engine) = EngineData::from_eec1(&rx_data) {
        println!("\nEngine Speed: {:.2} RPM", engine.speed_rpm);
        println!("Engine Torque: {:.1}%", engine.torque_percent);
    }
    
    // Encode engine data
    let engine_data = EngineData {
        speed_rpm: 1500.0,
        torque_percent: 45.0,
    };
    let tx_data = engine_data.to_eec1();
    print!("\nEncoded data: ");
    for byte in &tx_data {
        print!("{:02X} ", byte);
    }
    println!();
}
```

## Summary

**CAN Bus** is a robust, multi-master serial communication protocol widely used in automotive and industrial applications. It features message-based communication with built-in prioritization, error detection, and fault confinement capabilities.

**J1939** extends CAN with a standardized higher-layer protocol specifically for heavy-duty vehicles. It defines Parameter Group Numbers (PGNs) for common vehicle parameters, transport protocols for larger messages, and diagnostic capabilities. The 29-bit identifier is structured to include priority, PGN components, and source addressing.

**Programming CAN/J1939** requires understanding both the hardware layer (CAN frames, timing, bit stuffing) and application layer (message encoding/decoding, PGN interpretation). In Linux environments, SocketCAN provides a network-like interface for CAN communication. Modern languages like Rust offer memory-safe abstractions while C/C++ provides low-level control. Key programming tasks include proper frame construction, identifier decoding, data scaling/offset application, and handling transport protocol segmentation for messages exceeding 8 bytes.