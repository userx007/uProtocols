# CAN (Controller Area Network) Programming

## Overview

Controller Area Network (CAN) is a robust vehicle bus standard designed to allow microcontrollers and devices to communicate with each other without a host computer. Originally developed by Bosch in the 1980s for automotive applications, CAN has become the de facto standard for in-vehicle networking.

## Key Characteristics

**Architecture:**
- Multi-master broadcast serial bus
- Message-based protocol (no node addressing)
- Two-wire differential signaling (CAN_H and CAN_L)
- Maximum theoretical speed: 1 Mbps (Classical CAN)
- CAN FD (Flexible Data-rate): up to 8 Mbps

**Frame Types:**
- **Data Frame**: Carries data from transmitter to receiver
- **Remote Frame**: Requests data transmission
- **Error Frame**: Signals error detection
- **Overload Frame**: Introduces delay between frames

**Message Priority:**
- Based on arbitration field (CAN ID)
- Lower ID = higher priority
- Non-destructive bitwise arbitration

**Data Length:**
- Classical CAN: 0-8 bytes
- CAN FD: 0-64 bytes

## CAN Frame Structure (Standard 11-bit ID)

```
┌─────────────┬──────┬───┬───┬────┬──────────┬─────┬────────┬───┬────┬───────┐
│ Start of    │ ID   │RTR│IDE│ r0 │   DLC    │Data │  CRC   │ACK│EOF │  IFS  │
│ Frame (SOF) │(11b) │   │   │    │  (4b)    │0-8B │ (15b)  │   │    │       │
└─────────────┴──────┴───┴───┴────┴──────────┴─────┴────────┴───┴────┴───────┘
```

## C/C++ Programming Examples

### Example 1: Linux SocketCAN (C)

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

int main(void) {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;
    
    // Create socket
    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("Socket");
        return 1;
    }
    
    // Specify CAN interface
    strcpy(ifr.ifr_name, "can0");
    ioctl(s, SIOCGIFINDEX, &ifr);
    
    // Bind socket to CAN interface
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind");
        return 1;
    }
    
    // Prepare CAN frame
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
    
    // Send frame
    if (write(s, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
        perror("Write");
        return 1;
    }
    
    printf("Sent CAN frame with ID 0x%X\n", frame.can_id);
    
    close(s);
    return 0;
}
```

### Example 2: Receiving CAN Messages (C)

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

void print_can_frame(struct can_frame *frame) {
    printf("ID: 0x%03X  DLC: %d  Data: ", frame->can_id, frame->can_dlc);
    for (int i = 0; i < frame->can_dlc; i++) {
        printf("%02X ", frame->data[i]);
    }
    printf("\n");
}

int main(void) {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;
    ssize_t nbytes;
    
    // Create socket
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    
    // Setup interface
    strcpy(ifr.ifr_name, "can0");
    ioctl(s, SIOCGIFINDEX, &ifr);
    
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(s, (struct sockaddr *)&addr, sizeof(addr));
    
    printf("Listening for CAN frames...\n");
    
    // Receive loop
    while (1) {
        nbytes = read(s, &frame, sizeof(struct can_frame));
        
        if (nbytes < 0) {
            perror("Read");
            return 1;
        }
        
        if (nbytes < sizeof(struct can_frame)) {
            fprintf(stderr, "Incomplete CAN frame\n");
            continue;
        }
        
        print_can_frame(&frame);
    }
    
    close(s);
    return 0;
}
```

### Example 3: C++ CAN Wrapper Class

```cpp
#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

class CANSocket {
private:
    int socket_;
    std::string interface_;
    
public:
    CANSocket(const std::string& interface) : interface_(interface), socket_(-1) {
        // Create socket
        socket_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (socket_ < 0) {
            throw std::runtime_error("Failed to create CAN socket");
        }
        
        // Setup interface
        struct ifreq ifr;
        std::strcpy(ifr.ifr_name, interface_.c_str());
        if (ioctl(socket_, SIOCGIFINDEX, &ifr) < 0) {
            close(socket_);
            throw std::runtime_error("Failed to find interface: " + interface_);
        }
        
        // Bind socket
        struct sockaddr_can addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        
        if (bind(socket_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(socket_);
            throw std::runtime_error("Failed to bind socket");
        }
    }
    
    ~CANSocket() {
        if (socket_ >= 0) {
            close(socket_);
        }
    }
    
    void send(uint32_t id, const uint8_t* data, uint8_t length) {
        if (length > 8) {
            throw std::invalid_argument("Data length exceeds 8 bytes");
        }
        
        struct can_frame frame;
        frame.can_id = id;
        frame.can_dlc = length;
        std::memcpy(frame.data, data, length);
        
        if (write(socket_, &frame, sizeof(frame)) != sizeof(frame)) {
            throw std::runtime_error("Failed to send CAN frame");
        }
    }
    
    bool receive(can_frame& frame, int timeout_ms = -1) {
        if (timeout_ms >= 0) {
            fd_set readfds;
            struct timeval timeout;
            
            FD_ZERO(&readfds);
            FD_SET(socket_, &readfds);
            
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            
            int ret = select(socket_ + 1, &readfds, nullptr, nullptr, &timeout);
            if (ret <= 0) return false;
        }
        
        ssize_t nbytes = read(socket_, &frame, sizeof(frame));
        return nbytes == sizeof(frame);
    }
};

// Usage example
int main() {
    try {
        CANSocket can("can0");
        
        // Send a message
        uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
        can.send(0x100, data, 4);
        std::cout << "Message sent\n";
        
        // Receive messages
        struct can_frame frame;
        while (can.receive(frame, 1000)) {
            std::cout << "Received ID: 0x" << std::hex << frame.can_id 
                      << " Data: ";
            for (int i = 0; i < frame.can_dlc; i++) {
                std::cout << std::hex << (int)frame.data[i] << " ";
            }
            std::cout << std::dec << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
```

## Rust Programming Examples

### Example 1: Basic CAN with socketcan-rs

```rust
use socketcan::{CANSocket, CANFrame};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open CAN socket
    let socket = CANSocket::open("can0")?;
    
    // Create and send a frame
    let frame = CANFrame::new(0x123, &[0x01, 0x02, 0x03, 0x04], false, false)?;
    socket.write_frame(&frame)?;
    println!("Sent frame with ID: 0x{:X}", frame.id());
    
    Ok(())
}
```

### Example 2: CAN Receiver with Filtering

```rust
use socketcan::{CANSocket, CANFrame, CANFilter};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let socket = CANSocket::open("can0")?;
    
    // Set up filter to accept only specific IDs
    let filter = CANFilter::new(0x100, 0x7F0)?; // Accept 0x100-0x10F
    socket.set_filter(&[filter])?;
    
    println!("Listening for CAN messages...");
    
    loop {
        match socket.read_frame() {
            Ok(frame) => {
                print!("ID: 0x{:03X}  DLC: {}  Data: ", frame.id(), frame.data().len());
                for byte in frame.data() {
                    print!("{:02X} ", byte);
                }
                println!();
            }
            Err(e) => eprintln!("Error reading frame: {}", e),
        }
    }
}
```

### Example 3: Comprehensive Rust CAN Handler

```rust
use socketcan::{CANSocket, CANFrame, CANSocketOpenError};
use std::time::{Duration, Instant};
use std::sync::{Arc, Mutex};
use std::thread;

struct CANHandler {
    socket: CANSocket,
    stats: Arc<Mutex<Statistics>>,
}

#[derive(Default)]
struct Statistics {
    frames_sent: u64,
    frames_received: u64,
    errors: u64,
}

impl CANHandler {
    fn new(interface: &str) -> Result<Self, CANSocketOpenError> {
        let socket = CANSocket::open(interface)?;
        
        // Set receive timeout
        socket.set_read_timeout(Duration::from_millis(100))?;
        
        Ok(CANHandler {
            socket,
            stats: Arc::new(Mutex::new(Statistics::default())),
        })
    }
    
    fn send_frame(&self, id: u32, data: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        let frame = CANFrame::new(id, data, false, false)?;
        self.socket.write_frame(&frame)?;
        
        let mut stats = self.stats.lock().unwrap();
        stats.frames_sent += 1;
        
        Ok(())
    }
    
    fn receive_loop(&self) {
        loop {
            match self.socket.read_frame() {
                Ok(frame) => {
                    self.process_frame(&frame);
                    
                    let mut stats = self.stats.lock().unwrap();
                    stats.frames_received += 1;
                }
                Err(e) => {
                    if e.kind() != std::io::ErrorKind::TimedOut {
                        eprintln!("Read error: {}", e);
                        let mut stats = self.stats.lock().unwrap();
                        stats.errors += 1;
                    }
                }
            }
        }
    }
    
    fn process_frame(&self, frame: &CANFrame) {
        match frame.id() {
            0x100 => self.handle_engine_data(frame),
            0x200 => self.handle_transmission_data(frame),
            0x300..=0x3FF => self.handle_body_control(frame),
            _ => println!("Unknown frame ID: 0x{:X}", frame.id()),
        }
    }
    
    fn handle_engine_data(&self, frame: &CANFrame) {
        if frame.data().len() >= 4 {
            let rpm = u16::from_be_bytes([frame.data()[0], frame.data()[1]]);
            let temp = i16::from_be_bytes([frame.data()[2], frame.data()[3]]);
            println!("Engine - RPM: {}, Temp: {}°C", rpm, temp);
        }
    }
    
    fn handle_transmission_data(&self, frame: &CANFrame) {
        if !frame.data().is_empty() {
            let gear = frame.data()[0];
            println!("Transmission - Gear: {}", gear);
        }
    }
    
    fn handle_body_control(&self, frame: &CANFrame) {
        println!("Body control message: ID 0x{:X}", frame.id());
    }
    
    fn print_statistics(&self) {
        let stats = self.stats.lock().unwrap();
        println!("\n=== CAN Statistics ===");
        println!("Frames sent:     {}", stats.frames_sent);
        println!("Frames received: {}", stats.frames_received);
        println!("Errors:          {}", stats.errors);
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let handler = Arc::new(CANHandler::new("can0")?);
    
    // Spawn receiver thread
    let handler_clone = Arc::clone(&handler);
    let receiver_thread = thread::spawn(move || {
        handler_clone.receive_loop();
    });
    
    // Send some test frames
    handler.send_frame(0x100, &[0x00, 0x64, 0x00, 0x5A])?; // RPM: 100, Temp: 90
    handler.send_frame(0x200, &[0x03])?; // Gear: 3
    
    thread::sleep(Duration::from_secs(5));
    
    handler.print_statistics();
    
    Ok(())
}
```

### Example 4: Async Rust CAN with Tokio

```rust
use socketcan::{tokio::CANSocket, CANFrame};
use tokio::time::{sleep, Duration};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut socket = CANSocket::open("can0").await?;
    
    // Spawn sender task
    let mut sender_socket = CANSocket::open("can0").await?;
    tokio::spawn(async move {
        let mut counter = 0u8;
        loop {
            let frame = CANFrame::new(0x456, &[counter], false, false).unwrap();
            if let Err(e) = sender_socket.write_frame(frame).await {
                eprintln!("Send error: {}", e);
            }
            counter = counter.wrapping_add(1);
            sleep(Duration::from_millis(100)).await;
        }
    });
    
    // Receiver loop
    println!("Listening for CAN frames...");
    loop {
        match socket.read_frame().await {
            Ok(frame) => {
                println!("Received: ID=0x{:X}, Data={:?}", 
                         frame.id(), frame.data());
            }
            Err(e) => eprintln!("Receive error: {}", e),
        }
    }
}
```

## CAN and UDS Integration

UDS (Unified Diagnostic Services, ISO 14229) operates on top of CAN transport protocols. Here's a simple example:

```rust
use socketcan::{CANSocket, CANFrame};

struct UDSClient {
    socket: CANSocket,
    request_id: u32,
    response_id: u32,
}

impl UDSClient {
    fn new(interface: &str, req_id: u32, resp_id: u32) 
        -> Result<Self, Box<dyn std::error::Error>> {
        Ok(UDSClient {
            socket: CANSocket::open(interface)?,
            request_id: req_id,
            response_id: resp_id,
        })
    }
    
    // UDS Service 0x10: Diagnostic Session Control
    fn start_diagnostic_session(&self, session_type: u8) 
        -> Result<(), Box<dyn std::error::Error>> {
        let data = vec![0x02, 0x10, session_type]; // Length, SID, sub-function
        let frame = CANFrame::new(self.request_id, &data, false, false)?;
        self.socket.write_frame(&frame)?;
        println!("Started diagnostic session: 0x{:02X}", session_type);
        Ok(())
    }
    
    // UDS Service 0x22: Read Data By Identifier
    fn read_data_by_id(&self, data_id: u16) 
        -> Result<(), Box<dyn std::error::Error>> {
        let data = vec![
            0x03, // Length
            0x22, // SID
            (data_id >> 8) as u8,
            (data_id & 0xFF) as u8,
        ];
        let frame = CANFrame::new(self.request_id, &data, false, false)?;
        self.socket.write_frame(&frame)?;
        println!("Reading data ID: 0x{:04X}", data_id);
        Ok(())
    }
}
```

## Summary

**CAN Protocol Essentials:**
- Multi-master broadcast network with message-based communication
- Uses arbitration based on message ID for priority handling
- Supports 11-bit (standard) and 29-bit (extended) identifiers
- Classical CAN: up to 8 bytes at 1 Mbps; CAN FD: up to 64 bytes at 8 Mbps
- Built-in error detection and automatic retransmission

**Programming Approaches:**
- **C/Linux**: SocketCAN provides native kernel support with standard socket API
- **C++**: Object-oriented wrappers simplify resource management and error handling
- **Rust**: Type-safe implementations with `socketcan` crate, supporting both sync and async patterns

**Key Considerations:**
- Filter configuration reduces processing overhead for high-traffic networks
- Proper error handling is critical for robust automotive applications
- UDS and other higher-layer protocols build upon CAN transport
- Real-time constraints require careful thread/task management
- Bus monitoring and statistics help diagnose network issues

CAN remains the backbone of automotive communication, providing reliable, real-time data exchange essential for safety-critical vehicle systems and diagnostic protocols like UDS.