# Controller Area Network (CAN) - Comprehensive Guide

## What is CAN?

**Controller Area Network (CAN)** is a robust vehicle bus standard designed to allow microcontrollers and devices to communicate with each other without a host computer. Originally developed by Bosch in the 1980s for automotive applications, CAN has become the de facto standard for in-vehicle networking.

### Key Characteristics

- **Multi-master broadcast bus**: Any node can transmit when the bus is idle
- **Message-based protocol**: Data is transmitted in frames with identifiers
- **Priority-based arbitration**: Messages with lower IDs have higher priority
- **Error detection**: Built-in CRC, frame checks, and acknowledgment mechanisms
- **Differential signaling**: Uses CAN_H and CAN_L lines for noise immunity
- **Speed ranges**: From 10 Kbps to 1 Mbps (Classical CAN), up to 8 Mbps (CAN FD)

### CAN Frame Structure

A standard CAN frame consists of:
- **Start of Frame (SOF)**: 1 bit
- **Identifier**: 11 bits (standard) or 29 bits (extended)
- **RTR (Remote Transmission Request)**: 1 bit
- **Control Field**: 6 bits
- **Data Field**: 0-8 bytes (0-64 bytes in CAN FD)
- **CRC**: 15 bits + delimiter
- **ACK**: 2 bits
- **End of Frame (EOF)**: 7 bits

## CAN Programming in C/C++

### Example 1: Linux SocketCAN Interface

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

// Initialize CAN socket
int can_init(const char *ifname) {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;

    // Create socket
    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Specify CAN interface
    strcpy(ifr.ifr_name, ifname);
    ioctl(s, SIOCGIFINDEX, &ifr);

    // Bind socket to CAN interface
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(s);
        return -1;
    }

    return s;
}

// Send CAN frame
int can_send(int socket, uint32_t id, uint8_t *data, uint8_t len) {
    struct can_frame frame;
    
    frame.can_id = id;
    frame.can_dlc = len;
    memcpy(frame.data, data, len);

    if (write(socket, &frame, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
        perror("Write failed");
        return -1;
    }
    
    return 0;
}

// Receive CAN frame
int can_receive(int socket, struct can_frame *frame) {
    int nbytes = read(socket, frame, sizeof(struct can_frame));
    
    if (nbytes < 0) {
        perror("Read failed");
        return -1;
    }
    
    if (nbytes < sizeof(struct can_frame)) {
        fprintf(stderr, "Incomplete CAN frame\n");
        return -1;
    }
    
    return 0;
}

// Example usage
int main() {
    int s;
    struct can_frame frame;
    uint8_t tx_data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    // Initialize CAN interface "can0"
    s = can_init("can0");
    if (s < 0) {
        return 1;
    }

    // Send a message
    printf("Sending CAN message with ID 0x123\n");
    can_send(s, 0x123, tx_data, 8);

    // Receive messages
    printf("Waiting for CAN messages...\n");
    while (1) {
        if (can_receive(s, &frame) == 0) {
            printf("Received ID: 0x%03X, DLC: %d, Data: ", 
                   frame.can_id, frame.can_dlc);
            for (int i = 0; i < frame.can_dlc; i++) {
                printf("%02X ", frame.data[i]);
            }
            printf("\n");
        }
    }

    close(s);
    return 0;
}
```

### Example 2: C++ CAN Driver Class

```cpp
#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

class CANDriver {
private:
    int sock_;
    std::string interface_;

public:
    CANDriver(const std::string& interface) : interface_(interface), sock_(-1) {}
    
    ~CANDriver() {
        if (sock_ >= 0) {
            close(sock_);
        }
    }

    bool init() {
        struct sockaddr_can addr;
        struct ifreq ifr;

        sock_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (sock_ < 0) {
            std::cerr << "Error creating socket" << std::endl;
            return false;
        }

        std::strcpy(ifr.ifr_name, interface_.c_str());
        ioctl(sock_, SIOCGIFINDEX, &ifr);

        std::memset(&addr, 0, sizeof(addr));
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;

        if (bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Error binding socket" << std::endl;
            close(sock_);
            sock_ = -1;
            return false;
        }

        return true;
    }

    bool send(uint32_t id, const std::vector<uint8_t>& data) {
        if (data.size() > 8) {
            std::cerr << "Data too long for standard CAN frame" << std::endl;
            return false;
        }

        struct can_frame frame;
        frame.can_id = id;
        frame.can_dlc = data.size();
        std::memcpy(frame.data, data.data(), data.size());

        if (write(sock_, &frame, sizeof(frame)) != sizeof(frame)) {
            std::cerr << "Error writing to socket" << std::endl;
            return false;
        }

        return true;
    }

    bool receive(uint32_t& id, std::vector<uint8_t>& data, int timeout_ms = -1) {
        struct can_frame frame;
        
        if (timeout_ms > 0) {
            fd_set readfds;
            struct timeval timeout;
            
            FD_ZERO(&readfds);
            FD_SET(sock_, &readfds);
            
            timeout.tv_sec = timeout_ms / 1000;
            timeout.tv_usec = (timeout_ms % 1000) * 1000;
            
            int ret = select(sock_ + 1, &readfds, NULL, NULL, &timeout);
            if (ret <= 0) {
                return false;
            }
        }

        int nbytes = read(sock_, &frame, sizeof(frame));
        if (nbytes < sizeof(frame)) {
            return false;
        }

        id = frame.can_id;
        data.assign(frame.data, frame.data + frame.can_dlc);
        
        return true;
    }

    bool setFilter(uint32_t id, uint32_t mask) {
        struct can_filter filter;
        filter.can_id = id;
        filter.can_mask = mask;

        if (setsockopt(sock_, SOL_CAN_RAW, CAN_RAW_FILTER, 
                       &filter, sizeof(filter)) < 0) {
            std::cerr << "Error setting filter" << std::endl;
            return false;
        }

        return true;
    }
};

// Example usage
int main() {
    CANDriver can("can0");
    
    if (!can.init()) {
        std::cerr << "Failed to initialize CAN interface" << std::endl;
        return 1;
    }

    // Set filter to receive only messages with ID 0x100-0x1FF
    can.setFilter(0x100, 0x700);

    // Send a message
    std::vector<uint8_t> tx_data = {0xDE, 0xAD, 0xBE, 0xEF};
    can.send(0x123, tx_data);

    // Receive messages
    uint32_t rx_id;
    std::vector<uint8_t> rx_data;
    
    while (can.receive(rx_id, rx_data, 1000)) {
        std::cout << "Received ID: 0x" << std::hex << rx_id 
                  << ", Data: ";
        for (auto byte : rx_data) {
            std::cout << std::hex << (int)byte << " ";
        }
        std::cout << std::endl;
    }

    return 0;
}
```

## CAN Programming in Rust

### Example 1: Using socketcan-rs Crate

```rust
// Add to Cargo.toml:
// [dependencies]
// socketcan = "3.0"

use socketcan::{CANSocket, CANFrame, CANSocketOpenError};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open CAN socket
    let socket = CANSocket::open("can0")?;
    
    // Set read timeout
    socket.set_read_timeout(Duration::from_millis(1000))?;
    
    // Create and send a CAN frame
    let frame = CANFrame::new(
        0x123,                              // CAN ID
        &[0x01, 0x02, 0x03, 0x04],         // Data
        false,                              // Not RTR
        false,                              // Not error frame
    )?;
    
    socket.write_frame(&frame)?;
    println!("Sent frame: {:?}", frame);
    
    // Receive frames
    loop {
        match socket.read_frame() {
            Ok(frame) => {
                println!("Received ID: 0x{:03X}", frame.id());
                println!("Data: {:02X?}", frame.data());
            }
            Err(e) => {
                eprintln!("Error reading frame: {}", e);
            }
        }
    }
}
```

### Example 2: Safe CAN Driver Wrapper

```rust
use socketcan::{CANSocket, CANFrame, CANFilter};
use std::time::Duration;
use std::error::Error;

pub struct CANDriver {
    socket: CANSocket,
    interface: String,
}

impl CANDriver {
    pub fn new(interface: &str) -> Result<Self, Box<dyn Error>> {
        let socket = CANSocket::open(interface)?;
        
        Ok(CANDriver {
            socket,
            interface: interface.to_string(),
        })
    }

    pub fn set_timeout(&self, timeout: Duration) -> Result<(), Box<dyn Error>> {
        self.socket.set_read_timeout(timeout)?;
        Ok(())
    }

    pub fn set_filter(&self, id: u32, mask: u32) -> Result<(), Box<dyn Error>> {
        let filter = CANFilter::new(id, mask);
        self.socket.set_filters(&[filter])?;
        Ok(())
    }

    pub fn send(&self, id: u32, data: &[u8]) -> Result<(), Box<dyn Error>> {
        if data.len() > 8 {
            return Err("Data exceeds 8 bytes".into());
        }

        let frame = CANFrame::new(id, data, false, false)?;
        self.socket.write_frame(&frame)?;
        
        Ok(())
    }

    pub fn receive(&self) -> Result<(u32, Vec<u8>), Box<dyn Error>> {
        let frame = self.socket.read_frame()?;
        let id = frame.id();
        let data = frame.data().to_vec();
        
        Ok((id, data))
    }

    pub fn send_extended(&self, id: u32, data: &[u8]) -> Result<(), Box<dyn Error>> {
        if data.len() > 8 {
            return Err("Data exceeds 8 bytes".into());
        }

        let mut frame = CANFrame::new(id, data, false, false)?;
        frame.set_eff(); // Set extended frame format
        self.socket.write_frame(&frame)?;
        
        Ok(())
    }
}

// Example: Periodic message sender
use std::thread;

fn periodic_sender(interface: &str, id: u32, period_ms: u64) -> Result<(), Box<dyn Error>> {
    let driver = CANDriver::new(interface)?;
    let mut counter: u8 = 0;

    loop {
        let data = [counter, 0x00, 0x00, 0x00];
        driver.send(id, &data)?;
        println!("Sent counter: {}", counter);
        
        counter = counter.wrapping_add(1);
        thread::sleep(Duration::from_millis(period_ms));
    }
}

// Example: Message receiver with filtering
fn filtered_receiver(interface: &str) -> Result<(), Box<dyn Error>> {
    let driver = CANDriver::new(interface)?;
    
    // Only receive messages with IDs 0x100-0x1FF
    driver.set_filter(0x100, 0x700)?;
    driver.set_timeout(Duration::from_secs(1))?;

    println!("Listening for CAN messages (ID range: 0x100-0x1FF)...");

    loop {
        match driver.receive() {
            Ok((id, data)) => {
                println!("ID: 0x{:03X}, Data: {:02X?}", id, data);
            }
            Err(e) => {
                eprintln!("Receive error: {}", e);
            }
        }
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let interface = "can0";
    
    // Spawn sender thread
    let sender_handle = thread::spawn(move || {
        periodic_sender(interface, 0x123, 100).unwrap();
    });
    
    // Run receiver in main thread
    filtered_receiver(interface)?;
    
    sender_handle.join().unwrap();
    
    Ok(())
}
```

### Example 3: Async CAN with Tokio

```rust
// Add to Cargo.toml:
// [dependencies]
// tokio = { version = "1", features = ["full"] }
// socketcan = "3.0"

use socketcan::{CANSocket, CANFrame};
use tokio::time::{interval, Duration};
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let socket = CANSocket::open("can0")?;
    socket.set_nonblocking(true)?;

    // Async sender task
    let sender = tokio::spawn(async move {
        let socket = CANSocket::open("can0").unwrap();
        let mut interval = interval(Duration::from_millis(100));
        let mut counter: u8 = 0;

        loop {
            interval.tick().await;
            
            let data = [counter, 0x00, 0x00, 0x00];
            let frame = CANFrame::new(0x200, &data, false, false).unwrap();
            
            if let Err(e) = socket.write_frame(&frame) {
                eprintln!("Send error: {}", e);
            } else {
                println!("Sent: {}", counter);
            }
            
            counter = counter.wrapping_add(1);
        }
    });

    // Async receiver task
    let receiver = tokio::spawn(async move {
        let socket = CANSocket::open("can0").unwrap();
        
        loop {
            match socket.read_frame() {
                Ok(frame) => {
                    println!("Received ID: 0x{:03X}, Data: {:02X?}", 
                             frame.id(), frame.data());
                }
                Err(_) => {
                    tokio::time::sleep(Duration::from_millis(10)).await;
                }
            }
        }
    });

    tokio::try_join!(sender, receiver)?;
    
    Ok(())
}
```

## Secure Onboard Communication (SecOC)

**SecOC** is an AUTOSAR standard that provides cryptographic protection for CAN and other automotive network messages to prevent unauthorized access and message manipulation.

### Key Features

1. **Message Authentication**: Uses HMAC or CMAC to verify message integrity
2. **Freshness Values**: Counters or timestamps to prevent replay attacks
3. **Truncated MAC**: Reduced MAC size to fit within CAN frame constraints
4. **Key Management**: Secure storage and distribution of cryptographic keys

### SecOC Message Structure

A SecOC-protected message contains:
- **Payload**: Original data (up to 48 bits for CAN)
- **Freshness Value**: Counter/timestamp (typically 8-64 bits)
- **Authenticator**: Truncated MAC (typically 24-64 bits)

### Conceptual Implementation (Pseudocode)

```cpp
// SecOC Protected Message Transmission
struct SecOCMessage {
    uint32_t messageId;
    uint8_t payload[6];      // Reduced from 8 to fit authenticator
    uint8_t freshnessValue;
    uint16_t authenticator;  // Truncated MAC (24 bits)
};

class SecOCManager {
private:
    uint8_t key[16];         // AES-128 key
    uint64_t freshnessCounter;

public:
    // Generate authenticator for message
    uint32_t generateMAC(uint32_t msgId, const uint8_t* data, 
                         uint8_t len, uint64_t freshness) {
        // Concatenate message components
        uint8_t buffer[256];
        int pos = 0;
        
        memcpy(&buffer[pos], &msgId, 4); pos += 4;
        memcpy(&buffer[pos], data, len); pos += len;
        memcpy(&buffer[pos], &freshness, 8); pos += 8;
        
        // Calculate CMAC using AES-128
        uint8_t mac[16];
        aes_cmac(key, buffer, pos, mac);
        
        // Truncate to 24 bits
        uint32_t truncated = (mac[0] << 16) | (mac[1] << 8) | mac[2];
        return truncated;
    }
    
    // Send secured message
    void sendSecured(CANDriver& can, uint32_t id, const uint8_t* data, uint8_t len) {
        freshnessCounter++;
        
        uint32_t mac = generateMAC(id, data, len, freshnessCounter);
        
        // Pack into CAN frame: [data(6) | freshness(1) | mac(2)]
        uint8_t frame[8];
        memcpy(frame, data, 6);
        frame[6] = (uint8_t)(freshnessCounter & 0xFF);
        frame[7] = (uint8_t)((mac >> 16) & 0xFF);
        // Additional MAC bytes in another frame or extended frame
        
        can.send(id, frame, 8);
    }
    
    // Verify received message
    bool verifyMessage(uint32_t id, const uint8_t* frame) {
        uint8_t payload[6];
        memcpy(payload, frame, 6);
        
        uint8_t receivedFreshness = frame[6];
        uint32_t receivedMAC = (frame[7] << 16); // Simplified
        
        // Check freshness (prevent replay)
        if (receivedFreshness <= lastFreshness[id]) {
            return false; // Replay attack
        }
        
        // Recalculate MAC
        uint32_t calculatedMAC = generateMAC(id, payload, 6, receivedFreshness);
        
        // Compare
        if (calculatedMAC == receivedMAC) {
            lastFreshness[id] = receivedFreshness;
            return true;
        }
        
        return false;
    }
};
```

### Security Benefits

- **Authentication**: Ensures messages come from legitimate sources
- **Integrity**: Detects tampering or corruption
- **Replay Protection**: Freshness values prevent old messages from being reused
- **Minimal Overhead**: Optimized for resource-constrained automotive ECUs

---

## Summary

**CAN (Controller Area Network)** is a robust, message-based communication protocol widely used in automotive and industrial applications. It features multi-master capability, priority-based arbitration, and excellent error detection mechanisms.

**Programming CAN** can be accomplished across multiple languages:
- **C/C++**: Uses SocketCAN on Linux for direct, low-level control with high performance
- **Rust**: Provides memory-safe abstractions through crates like `socketcan-rs`, with excellent async support via Tokio

**Secure Onboard Communication (SecOC)** is an AUTOSAR standard that adds cryptographic protection to CAN messages using:
- Message Authentication Codes (MAC) for integrity verification
- Freshness values to prevent replay attacks
- Efficient truncation schemes to fit within CAN frame size constraints
- Key management for secure communication between ECUs

SecOC addresses critical security vulnerabilities in traditional CAN networks, protecting against spoofing, replay attacks, and unauthorized message injection—essential for modern connected and autonomous vehicles where cyber security is paramount.