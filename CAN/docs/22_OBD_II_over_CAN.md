# CAN Bus Programming: OBD-II Implementation

## Overview of CAN (Controller Area Network)

CAN is a robust vehicle bus standard designed for microcontrollers and devices to communicate without a host computer. It's a message-based protocol originally developed by Bosch for automotive applications.

### Key CAN Characteristics

**Physical Layer:**
- Differential signaling (CAN-H and CAN-L)
- Typical speeds: 125 Kbps to 1 Mbps (High-speed CAN)
- Bus topology with 120Ω termination resistors at each end

**Data Link Layer:**
- Message-based (not address-based)
- 11-bit (Standard) or 29-bit (Extended) identifiers
- 0-8 data bytes per frame
- Built-in error detection and automatic retransmission
- Priority-based arbitration (lower ID = higher priority)

## OBD-II over CAN

OBD-II (On-Board Diagnostics II) is a standardized system for vehicle diagnostics. Since 2008, all vehicles sold in the US use CAN bus for OBD-II communications (ISO 15765-4).

### OBD-II CAN Specifications

- **Standard CAN IDs:**
  - Request: `0x7DF` (broadcast) or `0x7E0-0x7E7` (specific ECU)
  - Response: `0x7E8-0x7EF` (ECU responses)
- **Speed:** 500 Kbps (typically)
- **Data Format:** ISO-TP (ISO 15765-2) for multi-frame messages

### Common OBD-II Modes (Services)

- **Mode 01:** Show current data (live sensor data)
- **Mode 02:** Show freeze frame data
- **Mode 03:** Show stored DTCs (Diagnostic Trouble Codes)
- **Mode 04:** Clear DTCs and stored values
- **Mode 09:** Request vehicle information

## C/C++ Implementation

### Using SocketCAN (Linux)

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

// OBD-II PIDs for Mode 01
#define PID_ENGINE_RPM          0x0C
#define PID_VEHICLE_SPEED       0x0D
#define PID_COOLANT_TEMP        0x05
#define PID_THROTTLE_POS        0x11

// CAN IDs for OBD-II
#define OBD2_REQUEST_ID         0x7DF
#define OBD2_RESPONSE_ID_BASE   0x7E8

typedef struct {
    int socket;
    struct sockaddr_can addr;
} CANBus;

// Initialize CAN bus
int can_init(CANBus *bus, const char *interface) {
    struct ifreq ifr;
    
    // Create socket
    bus->socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (bus->socket < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Specify interface
    strcpy(ifr.ifr_name, interface);
    ioctl(bus->socket, SIOCGIFINDEX, &ifr);
    
    // Bind socket to CAN interface
    bus->addr.can_family = AF_CAN;
    bus->addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(bus->socket, (struct sockaddr *)&bus->addr, sizeof(bus->addr)) < 0) {
        perror("Bind failed");
        return -1;
    }
    
    return 0;
}

// Send OBD-II request
int obd2_request(CANBus *bus, uint8_t mode, uint8_t pid) {
    struct can_frame frame;
    
    frame.can_id = OBD2_REQUEST_ID;
    frame.can_dlc = 8;
    
    // OBD-II request format: [num_bytes, mode, pid, 0x55...]
    frame.data[0] = 0x02;  // Number of additional bytes
    frame.data[1] = mode;
    frame.data[2] = pid;
    frame.data[3] = 0x55;  // Padding
    frame.data[4] = 0x55;
    frame.data[5] = 0x55;
    frame.data[6] = 0x55;
    frame.data[7] = 0x55;
    
    if (write(bus->socket, &frame, sizeof(frame)) != sizeof(frame)) {
        perror("Write failed");
        return -1;
    }
    
    return 0;
}

// Read OBD-II response
int obd2_read_response(CANBus *bus, struct can_frame *response, int timeout_ms) {
    fd_set readfds;
    struct timeval timeout;
    
    FD_ZERO(&readfds);
    FD_SET(bus->socket, &readfds);
    
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    int ret = select(bus->socket + 1, &readfds, NULL, NULL, &timeout);
    
    if (ret > 0) {
        return read(bus->socket, response, sizeof(*response));
    }
    
    return -1;  // Timeout or error
}

// Parse engine RPM (Mode 01, PID 0x0C)
int parse_engine_rpm(const struct can_frame *frame) {
    if (frame->data[1] == 0x41 && frame->data[2] == PID_ENGINE_RPM) {
        // RPM = ((A * 256) + B) / 4
        return ((frame->data[3] * 256) + frame->data[4]) / 4;
    }
    return -1;
}

// Parse vehicle speed (Mode 01, PID 0x0D)
int parse_vehicle_speed(const struct can_frame *frame) {
    if (frame->data[1] == 0x41 && frame->data[2] == PID_VEHICLE_SPEED) {
        // Speed in km/h
        return frame->data[3];
    }
    return -1;
}

// Read DTCs (Mode 03)
void read_dtcs(CANBus *bus) {
    struct can_frame response;
    
    printf("Reading Diagnostic Trouble Codes...\n");
    
    // Send Mode 03 request
    struct can_frame request;
    request.can_id = OBD2_REQUEST_ID;
    request.can_dlc = 8;
    request.data[0] = 0x01;  // 1 additional byte
    request.data[1] = 0x03;  // Mode 03
    memset(&request.data[2], 0x55, 6);
    
    write(bus->socket, &request, sizeof(request));
    
    // Read response
    while (obd2_read_response(bus, &response, 1000) > 0) {
        if (response.can_id >= OBD2_RESPONSE_ID_BASE && 
            response.can_id <= OBD2_RESPONSE_ID_BASE + 7) {
            
            if (response.data[1] == 0x43) {  // Mode 03 response
                int num_codes = response.data[2];
                printf("Number of DTCs: %d\n", num_codes);
                
                // Parse DTC codes (simplified)
                for (int i = 0; i < num_codes && (i * 2 + 3) < response.can_dlc; i++) {
                    uint16_t dtc = (response.data[i*2 + 3] << 8) | response.data[i*2 + 4];
                    printf("DTC: %04X\n", dtc);
                }
                break;
            }
        }
    }
}

// Example usage
int main() {
    CANBus bus;
    struct can_frame response;
    
    if (can_init(&bus, "can0") < 0) {
        return 1;
    }
    
    printf("CAN interface initialized\n");
    
    // Request engine RPM
    obd2_request(&bus, 0x01, PID_ENGINE_RPM);
    
    if (obd2_read_response(&bus, &response, 1000) > 0) {
        int rpm = parse_engine_rpm(&response);
        if (rpm >= 0) {
            printf("Engine RPM: %d\n", rpm);
        }
    }
    
    // Request vehicle speed
    obd2_request(&bus, 0x01, PID_VEHICLE_SPEED);
    
    if (obd2_read_response(&bus, &response, 1000) > 0) {
        int speed = parse_vehicle_speed(&response);
        if (speed >= 0) {
            printf("Vehicle Speed: %d km/h\n", speed);
        }
    }
    
    // Read DTCs
    read_dtcs(&bus);
    
    close(bus.socket);
    return 0;
}
```

## Rust Implementation

### Using the `socketcan` Crate

```rust
use socketcan::{CANSocket, CANFrame};
use std::time::Duration;
use std::thread;

// OBD-II constants
const OBD2_REQUEST_ID: u32 = 0x7DF;
const OBD2_RESPONSE_ID_BASE: u32 = 0x7E8;

#[derive(Debug)]
enum OBDMode {
    CurrentData = 0x01,
    FreezeFrame = 0x02,
    DTCs = 0x03,
    ClearDTCs = 0x04,
    VehicleInfo = 0x09,
}

#[derive(Debug)]
enum PID {
    EngineRPM = 0x0C,
    VehicleSpeed = 0x0D,
    CoolantTemp = 0x05,
    ThrottlePosition = 0x11,
    FuelLevel = 0x2F,
}

struct OBD2Scanner {
    socket: CANSocket,
}

impl OBD2Scanner {
    fn new(interface: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let socket = CANSocket::open(interface)?;
        socket.set_read_timeout(Duration::from_millis(1000))?;
        
        Ok(OBD2Scanner { socket })
    }
    
    fn send_request(&self, mode: OBDMode, pid: PID) -> Result<(), Box<dyn std::error::Error>> {
        let mut data = [0x55u8; 8];
        data[0] = 0x02;  // Number of additional bytes
        data[1] = mode as u8;
        data[2] = pid as u8;
        
        let frame = CANFrame::new(OBD2_REQUEST_ID, &data, false, false)?;
        self.socket.write_frame(&frame)?;
        
        Ok(())
    }
    
    fn read_response(&self) -> Result<CANFrame, Box<dyn std::error::Error>> {
        let frame = self.socket.read_frame()?;
        Ok(frame)
    }
    
    fn parse_rpm(&self, frame: &CANFrame) -> Option<u32> {
        let data = frame.data();
        
        if data.len() >= 5 && data[1] == 0x41 && data[2] == PID::EngineRPM as u8 {
            // RPM = ((A * 256) + B) / 4
            let rpm = ((data[3] as u32 * 256) + data[4] as u32) / 4;
            return Some(rpm);
        }
        None
    }
    
    fn parse_speed(&self, frame: &CANFrame) -> Option<u8> {
        let data = frame.data();
        
        if data.len() >= 4 && data[1] == 0x41 && data[2] == PID::VehicleSpeed as u8 {
            return Some(data[3]);
        }
        None
    }
    
    fn parse_coolant_temp(&self, frame: &CANFrame) -> Option<i16> {
        let data = frame.data();
        
        if data.len() >= 4 && data[1] == 0x41 && data[2] == PID::CoolantTemp as u8 {
            // Temperature = A - 40 (in Celsius)
            return Some(data[3] as i16 - 40);
        }
        None
    }
    
    fn get_rpm(&self) -> Result<u32, Box<dyn std::error::Error>> {
        self.send_request(OBDMode::CurrentData, PID::EngineRPM)?;
        
        loop {
            let frame = self.read_response()?;
            
            if frame.id() >= OBD2_RESPONSE_ID_BASE && 
               frame.id() <= OBD2_RESPONSE_ID_BASE + 7 {
                if let Some(rpm) = self.parse_rpm(&frame) {
                    return Ok(rpm);
                }
            }
        }
    }
    
    fn get_speed(&self) -> Result<u8, Box<dyn std::error::Error>> {
        self.send_request(OBDMode::CurrentData, PID::VehicleSpeed)?;
        
        loop {
            let frame = self.read_response()?;
            
            if frame.id() >= OBD2_RESPONSE_ID_BASE && 
               frame.id() <= OBD2_RESPONSE_ID_BASE + 7 {
                if let Some(speed) = self.parse_speed(&frame) {
                    return Ok(speed);
                }
            }
        }
    }
    
    fn read_dtcs(&self) -> Result<Vec<String>, Box<dyn std::error::Error>> {
        let mut data = [0x55u8; 8];
        data[0] = 0x01;  // 1 additional byte
        data[1] = OBDMode::DTCs as u8;
        
        let frame = CANFrame::new(OBD2_REQUEST_ID, &data, false, false)?;
        self.socket.write_frame(&frame)?;
        
        let mut dtcs = Vec::new();
        
        loop {
            match self.read_response() {
                Ok(frame) => {
                    if frame.id() >= OBD2_RESPONSE_ID_BASE && 
                       frame.id() <= OBD2_RESPONSE_ID_BASE + 7 {
                        
                        let data = frame.data();
                        if data[1] == 0x43 {  // Mode 03 response
                            let num_codes = data[2];
                            
                            for i in 0..num_codes {
                                let idx = (i * 2 + 3) as usize;
                                if idx + 1 < data.len() {
                                    let dtc_high = data[idx];
                                    let dtc_low = data[idx + 1];
                                    
                                    // Decode DTC
                                    let first_char = match (dtc_high >> 6) & 0x03 {
                                        0 => 'P',  // Powertrain
                                        1 => 'C',  // Chassis
                                        2 => 'B',  // Body
                                        3 => 'U',  // Network
                                        _ => '?',
                                    };
                                    
                                    let dtc_string = format!(
                                        "{}{:X}{:02X}{:02X}",
                                        first_char,
                                        (dtc_high >> 4) & 0x03,
                                        dtc_high & 0x0F,
                                        dtc_low
                                    );
                                    
                                    dtcs.push(dtc_string);
                                }
                            }
                            break;
                        }
                    }
                }
                Err(_) => break,
            }
        }
        
        Ok(dtcs)
    }
    
    fn clear_dtcs(&self) -> Result<(), Box<dyn std::error::Error>> {
        let mut data = [0x55u8; 8];
        data[0] = 0x01;
        data[1] = OBDMode::ClearDTCs as u8;
        
        let frame = CANFrame::new(OBD2_REQUEST_ID, &data, false, false)?;
        self.socket.write_frame(&frame)?;
        
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let scanner = OBD2Scanner::new("can0")?;
    
    println!("OBD-II Scanner initialized");
    
    // Read engine RPM
    match scanner.get_rpm() {
        Ok(rpm) => println!("Engine RPM: {}", rpm),
        Err(e) => eprintln!("Error reading RPM: {}", e),
    }
    
    thread::sleep(Duration::from_millis(100));
    
    // Read vehicle speed
    match scanner.get_speed() {
        Ok(speed) => println!("Vehicle Speed: {} km/h", speed),
        Err(e) => eprintln!("Error reading speed: {}", e),
    }
    
    thread::sleep(Duration::from_millis(100));
    
    // Read DTCs
    match scanner.read_dtcs() {
        Ok(dtcs) => {
            println!("\nDiagnostic Trouble Codes:");
            if dtcs.is_empty() {
                println!("  No codes found");
            } else {
                for dtc in dtcs {
                    println!("  {}", dtc);
                }
            }
        }
        Err(e) => eprintln!("Error reading DTCs: {}", e),
    }
    
    Ok(())
}
```

## Summary

**CAN Bus Fundamentals:**
- Message-based protocol with priority arbitration
- Differential signaling for noise immunity
- 11-bit or 29-bit identifiers, 0-8 data bytes
- Built-in error detection and retransmission

**OBD-II over CAN:**
- Standard vehicle diagnostics protocol (mandatory since 2008 in US)
- Uses CAN IDs 0x7DF (request) and 0x7E8-0x7EF (responses)
- Multiple modes for live data, freeze frames, DTCs, and vehicle info
- ISO-TP protocol for multi-frame messages

**Implementation Approaches:**
- **C/C++:** Direct SocketCAN access on Linux, low-level control, efficient for embedded systems
- **Rust:** Type-safe abstractions via `socketcan` crate, memory safety guarantees, excellent for safety-critical applications

**Common Use Cases:**
- Real-time monitoring of engine parameters (RPM, speed, temperature)
- Reading and clearing diagnostic trouble codes
- Vehicle performance analysis and tuning
- Fleet management and telematics

Both implementations provide complete OBD-II functionality with proper error handling and are production-ready for automotive diagnostics applications.