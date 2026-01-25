# CAN Arbitration and Priority

## Detailed Description

CAN (Controller Area Network) uses a sophisticated **non-destructive bitwise arbitration** mechanism to resolve bus access conflicts when multiple nodes attempt to transmit simultaneously. This is one of CAN's most elegant features, ensuring deterministic message priority without data loss.

### How Arbitration Works

**1. Carrier Sense Multiple Access (CSMA/CA)**
- CAN uses CSMA/CA (Carrier Sense Multiple Access with Collision Avoidance)
- Nodes monitor the bus before transmitting
- If the bus is idle, any node can begin transmission

**2. Bitwise Arbitration Process**
During transmission, each node:
- Transmits its message bit-by-bit
- Simultaneously reads back what's on the bus
- Compares transmitted bit with the bus state

**3. Dominant vs. Recessive Bits**
- **Dominant bit (0)**: Actively drives the bus low (CAN_H high, CAN_L low)
- **Recessive bit (1)**: Releases the bus, allowing it to float high
- If one node transmits dominant (0) and another transmits recessive (1), the dominant bit wins

**4. Arbitration Field**
- Arbitration occurs during the **Identifier field** (11-bit for Standard, 29-bit for Extended)
- Lower identifier values have **higher priority** (0x000 beats 0x7FF)
- The node transmitting the lowest identifier wins arbitration
- Losing nodes automatically become receivers without retransmission delay

**5. Non-Destructive Nature**
- The winning message continues transmission uninterrupted
- No bandwidth is wasted on collision recovery
- Losing nodes know exactly which message won and can receive it

### Priority Scheme

```
Higher Priority (wins arbitration)
    ↓
0x000 ← Highest priority
0x001
0x002
...
0x100 ← Emergency messages
0x200 ← Critical sensor data
0x300 ← Control commands
...
0x600 ← Status updates
0x700 ← Diagnostic data
0x7FF ← Lowest priority (Standard CAN)
    ↓
Lower Priority (loses arbitration)
```

### Timing Considerations

- **Bit time**: All nodes must agree on bit timing (typically 125 kbps to 1 Mbps)
- **Propagation delay**: Signal must propagate across entire network within one bit time
- **Sample point**: Typically at 75-87.5% of bit time to allow for arbitration

---

## C/C++ Code Examples

### Example 1: Priority-Based Message Structure

```c
#include <stdint.h>
#include <stdbool.h>

// CAN message structure
typedef struct {
    uint32_t id;          // 11-bit or 29-bit identifier
    uint8_t  dlc;         // Data Length Code (0-8)
    uint8_t  data[8];     // Payload
    bool     extended;    // Standard or Extended frame
    bool     rtr;         // Remote Transmission Request
} can_message_t;

// Priority definitions (lower ID = higher priority)
#define CAN_PRIORITY_EMERGENCY      0x000
#define CAN_PRIORITY_CRITICAL       0x100
#define CAN_PRIORITY_HIGH           0x200
#define CAN_PRIORITY_NORMAL         0x400
#define CAN_PRIORITY_LOW            0x600
#define CAN_PRIORITY_DEBUG          0x700

// Message type definitions with priority
#define CAN_MSG_EMERGENCY_STOP      (CAN_PRIORITY_EMERGENCY + 0x01)
#define CAN_MSG_BRAKE_REQUEST       (CAN_PRIORITY_CRITICAL + 0x10)
#define CAN_MSG_ENGINE_RPM          (CAN_PRIORITY_HIGH + 0x20)
#define CAN_MSG_TEMPERATURE         (CAN_PRIORITY_NORMAL + 0x30)
#define CAN_MSG_STATUS_UPDATE       (CAN_PRIORITY_LOW + 0x40)

// Create a high-priority emergency message
can_message_t create_emergency_message(void) {
    can_message_t msg = {
        .id = CAN_MSG_EMERGENCY_STOP,
        .dlc = 2,
        .data = {0xFF, 0xFF, 0, 0, 0, 0, 0, 0},
        .extended = false,
        .rtr = false
    };
    return msg;
}

// Create a lower-priority status message
can_message_t create_status_message(uint8_t status_code) {
    can_message_t msg = {
        .id = CAN_MSG_STATUS_UPDATE,
        .dlc = 1,
        .data = {status_code, 0, 0, 0, 0, 0, 0, 0},
        .extended = false,
        .rtr = false
    };
    return msg;
}
```

### Example 2: Arbitration Simulation

```cpp
#include <iostream>
#include <vector>
#include <algorithm>

class CANNode {
private:
    uint32_t nodeId;
    uint32_t messageId;
    std::vector<bool> messageBits;
    bool isActive;

public:
    CANNode(uint32_t nId, uint32_t mId) 
        : nodeId(nId), messageId(mId), isActive(true) {
        // Convert message ID to bit array (11 bits for standard CAN)
        for (int i = 10; i >= 0; i--) {
            messageBits.push_back((mId >> i) & 1);
        }
    }

    // Transmit a bit and check arbitration
    bool transmitBit(int bitPosition, bool busBit) {
        if (!isActive) return false;
        
        bool myBit = messageBits[bitPosition];
        
        // Check if we lost arbitration
        if (myBit == 1 && busBit == 0) {
            // We transmitted recessive, but bus is dominant
            isActive = false;
            std::cout << "Node " << nodeId << " (ID: 0x" << std::hex 
                      << messageId << ") lost arbitration at bit " 
                      << std::dec << bitPosition << std::endl;
            return false;
        }
        
        return isActive;
    }

    bool getIsActive() const { return isActive; }
    uint32_t getMessageId() const { return messageId; }
    uint32_t getNodeId() const { return nodeId; }
    bool getBit(int position) const { return messageBits[position]; }
};

// Simulate CAN arbitration
void simulateArbitration(std::vector<CANNode>& nodes) {
    std::cout << "\n=== CAN Arbitration Simulation ===" << std::endl;
    std::cout << "Starting nodes:" << std::endl;
    for (const auto& node : nodes) {
        std::cout << "  Node " << node.getNodeId() 
                  << " with message ID: 0x" << std::hex 
                  << node.getMessageId() << std::dec << std::endl;
    }
    
    // Arbitrate bit-by-bit (11 bits for standard CAN)
    for (int bitPos = 0; bitPos < 11; bitPos++) {
        // Determine bus state (dominant wins)
        bool busBit = true; // Start as recessive
        for (const auto& node : nodes) {
            if (node.getIsActive() && node.getBit(bitPos) == 0) {
                busBit = false; // Any dominant bit makes bus dominant
                break;
            }
        }
        
        // Each node checks arbitration
        for (auto& node : nodes) {
            node.transmitBit(bitPos, busBit);
        }
        
        // Check if arbitration is complete
        int activeCount = 0;
        for (const auto& node : nodes) {
            if (node.getIsActive()) activeCount++;
        }
        if (activeCount == 1) break;
    }
    
    // Find winner
    for (const auto& node : nodes) {
        if (node.getIsActive()) {
            std::cout << "\n*** Winner: Node " << node.getNodeId() 
                      << " with message ID: 0x" << std::hex 
                      << node.getMessageId() << std::dec 
                      << " ***" << std::endl;
        }
    }
}

int main() {
    std::vector<CANNode> nodes;
    nodes.push_back(CANNode(1, 0x350)); // Normal priority
    nodes.push_back(CANNode(2, 0x100)); // Critical priority
    nodes.push_back(CANNode(3, 0x200)); // High priority
    nodes.push_back(CANNode(4, 0x001)); // Emergency priority
    
    simulateArbitration(nodes);
    return 0;
}
```

### Example 3: SocketCAN Priority Management (Linux)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>

// Send high-priority message
int send_priority_message(int sock, uint32_t id, uint8_t *data, uint8_t len) {
    struct can_frame frame;
    
    frame.can_id = id;  // Lower ID = higher priority
    frame.can_dlc = len;
    memcpy(frame.data, data, len);
    
    int bytes_sent = write(sock, &frame, sizeof(frame));
    if (bytes_sent != sizeof(frame)) {
        perror("CAN send failed");
        return -1;
    }
    
    return 0;
}

int main() {
    int sock;
    struct sockaddr_can addr;
    struct ifreq ifr;
    
    // Create socket
    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    // Specify interface
    strcpy(ifr.ifr_name, "can0");
    ioctl(sock, SIOCGIFINDEX, &ifr);
    
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    // Bind socket
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    // Send messages with different priorities
    uint8_t emergency_data[] = {0xFF, 0xFF};
    uint8_t normal_data[] = {0x42};
    
    printf("Sending high-priority emergency message (ID: 0x001)\n");
    send_priority_message(sock, 0x001, emergency_data, 2);
    
    printf("Sending low-priority normal message (ID: 0x600)\n");
    send_priority_message(sock, 0x600, normal_data, 1);
    
    close(sock);
    return 0;
}
```

---

## Rust Code Examples

### Example 1: Message Priority System

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CanId(u32);

impl CanId {
    pub fn new_standard(id: u16) -> Result<Self, &'static str> {
        if id > 0x7FF {
            Err("Standard CAN ID must be 11 bits (0x000-0x7FF)")
        } else {
            Ok(CanId(id as u32))
        }
    }
    
    pub fn new_extended(id: u32) -> Result<Self, &'static str> {
        if id > 0x1FFFFFFF {
            Err("Extended CAN ID must be 29 bits")
        } else {
            Ok(CanId(id | 0x80000000)) // Set extended bit
        }
    }
    
    pub fn is_extended(&self) -> bool {
        (self.0 & 0x80000000) != 0
    }
    
    pub fn raw_id(&self) -> u32 {
        if self.is_extended() {
            self.0 & 0x1FFFFFFF
        } else {
            self.0 & 0x7FF
        }
    }
}

// Implement ordering for priority (lower ID = higher priority)
impl PartialOrd for CanId {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for CanId {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        // Lower ID has higher priority, so reverse comparison
        other.raw_id().cmp(&self.raw_id())
    }
}

#[derive(Debug, Clone)]
pub struct CanMessage {
    pub id: CanId,
    pub data: Vec<u8>,
    pub rtr: bool,
}

// Priority levels
pub mod priority {
    use super::CanId;
    
    pub const EMERGENCY: CanId = CanId(0x000);
    pub const CRITICAL: CanId = CanId(0x100);
    pub const HIGH: CanId = CanId(0x200);
    pub const NORMAL: CanId = CanId(0x400);
    pub const LOW: CanId = CanId(0x600);
    pub const DEBUG: CanId = CanId(0x700);
}

impl CanMessage {
    pub fn new(id: CanId, data: Vec<u8>) -> Result<Self, &'static str> {
        if data.len() > 8 {
            Err("CAN data length must be 0-8 bytes")
        } else {
            Ok(CanMessage {
                id,
                data,
                rtr: false,
            })
        }
    }
    
    pub fn with_priority(priority_base: CanId, offset: u16, data: Vec<u8>) 
        -> Result<Self, &'static str> {
        let id = CanId::new_standard(priority_base.raw_id() as u16 + offset)?;
        Self::new(id, data)
    }
}

fn main() {
    // Create messages with different priorities
    let emergency = CanMessage::new(
        priority::EMERGENCY,
        vec![0xFF, 0xFF]
    ).unwrap();
    
    let status = CanMessage::new(
        priority::LOW,
        vec![0x42]
    ).unwrap();
    
    let critical = CanMessage::new(
        priority::CRITICAL,
        vec![0x01, 0x02, 0x03]
    ).unwrap();
    
    // Demonstrate priority ordering
    let mut messages = vec![status.clone(), critical.clone(), emergency.clone()];
    messages.sort_by(|a, b| a.id.cmp(&b.id));
    
    println!("Messages sorted by priority (highest first):");
    for (i, msg) in messages.iter().enumerate() {
        println!("  {}. ID: 0x{:03X}, Data: {:?}", 
                 i + 1, msg.id.raw_id(), msg.data);
    }
}
```

### Example 2: Arbitration Simulation in Rust

```rust
use std::fmt;

#[derive(Clone)]
struct CanNode {
    node_id: u32,
    message_id: u32,
    message_bits: Vec<bool>,
    is_active: bool,
}

impl CanNode {
    fn new(node_id: u32, message_id: u32) -> Self {
        // Convert message ID to bit vector (11 bits, MSB first)
        let mut bits = Vec::new();
        for i in (0..11).rev() {
            bits.push(((message_id >> i) & 1) == 1);
        }
        
        CanNode {
            node_id,
            message_id,
            message_bits: bits,
            is_active: true,
        }
    }
    
    fn transmit_bit(&mut self, bit_position: usize, bus_bit: bool) -> bool {
        if !self.is_active {
            return false;
        }
        
        let my_bit = self.message_bits[bit_position];
        
        // Recessive (1) loses to dominant (0)
        if my_bit && !bus_bit {
            self.is_active = false;
            println!("Node {} (ID: 0x{:03X}) lost arbitration at bit {}", 
                     self.node_id, self.message_id, bit_position);
            return false;
        }
        
        self.is_active
    }
    
    fn get_bit(&self, position: usize) -> bool {
        self.message_bits[position]
    }
}

impl fmt::Display for CanNode {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "Node {} (ID: 0x{:03X})", self.node_id, self.message_id)
    }
}

fn simulate_arbitration(nodes: &mut Vec<CanNode>) {
    println!("\n=== CAN Arbitration Simulation ===");
    println!("Starting nodes:");
    for node in nodes.iter() {
        println!("  {}", node);
    }
    
    // Arbitrate bit-by-bit
    for bit_pos in 0..11 {
        // Determine bus state (any dominant bit makes bus dominant)
        let bus_bit = nodes.iter()
            .filter(|n| n.is_active)
            .all(|n| n.get_bit(bit_pos));
        
        // Each node checks arbitration
        for node in nodes.iter_mut() {
            node.transmit_bit(bit_pos, bus_bit);
        }
        
        // Check if only one node remains
        let active_count = nodes.iter().filter(|n| n.is_active).count();
        if active_count == 1 {
            break;
        }
    }
    
    // Find and announce winner
    if let Some(winner) = nodes.iter().find(|n| n.is_active) {
        println!("\n*** Winner: {} ***", winner);
    }
}

fn main() {
    let mut nodes = vec![
        CanNode::new(1, 0x350), // Normal priority
        CanNode::new(2, 0x100), // Critical priority
        CanNode::new(3, 0x200), // High priority
        CanNode::new(4, 0x001), // Emergency priority
    ];
    
    simulate_arbitration(&mut nodes);
}
```

### Example 3: Using socketcan-rs Library

```rust
// Cargo.toml dependency: socketcan = "3.0"

use socketcan::{CanSocket, Socket, Frame, StandardId};
use std::time::Duration;

fn send_priority_messages() -> Result<(), Box<dyn std::error::Error>> {
    // Open CAN socket
    let socket = CanSocket::open("can0")?;
    socket.set_write_timeout(Duration::from_millis(100))?;
    
    // Define priority levels
    const EMERGENCY_ID: u16 = 0x001;
    const CRITICAL_ID: u16 = 0x100;
    const NORMAL_ID: u16 = 0x400;
    const LOW_ID: u16 = 0x600;
    
    // Send high-priority emergency message
    let emergency_frame = Frame::new(
        StandardId::new(EMERGENCY_ID).unwrap(),
        &[0xFF, 0xFF]
    ).unwrap();
    
    println!("Sending emergency message (ID: 0x{:03X})", EMERGENCY_ID);
    socket.write_frame(&emergency_frame)?;
    
    // Send lower-priority messages
    let normal_frame = Frame::new(
        StandardId::new(NORMAL_ID).unwrap(),
        &[0x42, 0x43, 0x44]
    ).unwrap();
    
    println!("Sending normal message (ID: 0x{:03X})", NORMAL_ID);
    socket.write_frame(&normal_frame)?;
    
    // If both messages are sent simultaneously by different nodes,
    // the emergency message (0x001) will win arbitration over
    // the normal message (0x400)
    
    Ok(())
}

fn main() {
    if let Err(e) = send_priority_messages() {
        eprintln!("Error: {}", e);
    }
}
```

---

## Summary

**CAN Arbitration and Priority** is a deterministic, non-destructive mechanism that allows multiple nodes to contend for bus access without collisions or retransmissions:

### Key Points:

1. **Bitwise Arbitration**: Messages compete bit-by-bit during transmission of the identifier field
2. **Dominant vs. Recessive**: Dominant bits (0) override recessive bits (1) on the bus
3. **Priority by Identifier**: Lower numerical IDs have higher priority (0x000 > 0x7FF)
4. **Non-Destructive**: The winning message continues uninterrupted; losers become receivers
5. **Deterministic**: The same set of competing messages always produces the same winner
6. **Real-Time Capable**: Higher-priority messages always get through first, enabling predictable real-time behavior
7. **No Bandwidth Loss**: Unlike collision-based systems (Ethernet), no time is wasted on retransmissions

This arbitration scheme makes CAN ideal for safety-critical and real-time applications where emergency messages must take precedence over routine status updates, such as automotive, industrial automation, and medical devices.