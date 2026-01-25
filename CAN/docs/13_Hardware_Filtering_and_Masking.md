# Hardware Filtering and Masking in CAN

## Detailed Description

Hardware filtering and masking is a crucial feature in CAN controller implementation that allows the microcontroller to automatically reject unwanted CAN messages at the hardware level, before they reach the CPU or trigger interrupts. This significantly reduces CPU overhead and improves system efficiency, especially in networks with high message traffic.

### How Hardware Filtering Works

CAN controllers typically implement acceptance filtering using two registers:
- **Acceptance Code (Filter)**: Defines the pattern of identifier bits to match
- **Acceptance Mask**: Specifies which bits of the identifier should be checked (0 = don't care, 1 = must match)

The hardware compares incoming message identifiers against the filter configuration:
```
Accept Message = (Incoming_ID & Mask) == (Filter & Mask)
```

### Filter Types

1. **Single Filter Mode**: One acceptance filter for all messages
2. **Dual Filter Mode**: Two independent filters
3. **List Mode**: Multiple exact ID matches
4. **Range Mode**: Accept IDs within a specific range

### Benefits

- **Reduced CPU Load**: Only relevant messages trigger interrupts
- **Lower Latency**: Faster response to important messages
- **Power Efficiency**: Less processing means lower power consumption
- **Simplified Software**: Application doesn't need to filter messages in software

## C/C++ Implementation Examples

### Example 1: STM32 HAL with Standard CAN (11-bit identifiers)

```c
#include "stm32f4xx_hal.h"

CAN_HandleTypeDef hcan1;

// Initialize CAN with hardware filtering
HAL_StatusTypeDef CAN_Init_With_Filter(void) {
    CAN_FilterTypeDef sFilterConfig;
    
    // Configure CAN parameters (baud rate, etc.)
    hcan1.Instance = CAN1;
    hcan1.Init.Prescaler = 4;
    hcan1.Init.Mode = CAN_MODE_NORMAL;
    hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;
    hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan1.Init.TimeTriggeredMode = DISABLE;
    hcan1.Init.AutoBusOff = ENABLE;
    hcan1.Init.AutoWakeUp = DISABLE;
    hcan1.Init.AutoRetransmission = ENABLE;
    hcan1.Init.ReceiveFifoLocked = DISABLE;
    hcan1.Init.TransmitFifoPriority = DISABLE;
    
    if (HAL_CAN_Init(&hcan1) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Configure acceptance filter
    // Accept only messages with IDs 0x100-0x10F
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    
    // Filter: 0x100 (shifted left by 21 bits for standard ID)
    sFilterConfig.FilterIdHigh = (0x100 << 5);
    sFilterConfig.FilterIdLow = 0x0000;
    
    // Mask: Check upper 7 bits (0x7F0), ignore lower 4 bits
    sFilterConfig.FilterMaskIdHigh = (0x7F0 << 5);
    sFilterConfig.FilterMaskIdLow = 0x0000;
    
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;
    
    if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Start CAN
    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Activate notifications
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        return HAL_ERROR;
    }
    
    return HAL_OK;
}

// Example: Multiple filters for specific IDs
HAL_StatusTypeDef CAN_Configure_Multiple_Filters(void) {
    CAN_FilterTypeDef sFilterConfig;
    
    // Filter Bank 0: Accept ID 0x123 and 0x456
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDLIST;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    
    // First ID: 0x123
    sFilterConfig.FilterIdHigh = (0x123 << 5);
    sFilterConfig.FilterIdLow = 0x0000;
    
    // Second ID: 0x456
    sFilterConfig.FilterMaskIdHigh = (0x456 << 5);
    sFilterConfig.FilterMaskIdLow = 0x0000;
    
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;
    
    if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Filter Bank 1: Accept range 0x200-0x2FF
    sFilterConfig.FilterBank = 1;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterIdHigh = (0x200 << 5);
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = (0x700 << 5);  // Mask for upper 3 bits
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO1;
    
    return HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig);
}
```

### Example 2: Extended CAN (29-bit identifiers)

```c
// Configure filter for extended CAN IDs
HAL_StatusTypeDef CAN_Configure_Extended_Filter(void) {
    CAN_FilterTypeDef sFilterConfig;
    
    // Accept extended IDs from 0x18FF0000 to 0x18FFFFFF
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    
    // Extended ID format: ID[28:0] | IDE | RTR | 0
    uint32_t filter_id = 0x18FF0000;
    uint32_t filter_mask = 0x1FFF0000;  // Check upper bits only
    
    // Pack into high/low registers
    sFilterConfig.FilterIdHigh = ((filter_id << 3) | (1 << 2)) >> 16;  // IDE=1
    sFilterConfig.FilterIdLow = (filter_id << 3) | (1 << 2);
    
    sFilterConfig.FilterMaskIdHigh = ((filter_mask << 3) | (1 << 2)) >> 16;
    sFilterConfig.FilterMaskIdLow = (filter_mask << 3) | (1 << 2);
    
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;
    
    return HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig);
}
```

### Example 3: MCP2515 CAN Controller (SPI-based)

```cpp
#include <SPI.h>
#include <mcp2515.h>

MCP2515 mcp2515(10);  // CS pin 10

void setup() {
    Serial.begin(115200);
    SPI.begin();
    
    // Initialize MCP2515
    mcp2515.reset();
    mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
    mcp2515.setNormalMode();
    
    // Configure hardware filters
    // RXB0: Accept only ID 0x100 with mask 0x7FF (exact match)
    mcp2515.setFilterMask(MCP2515::MASK0, false, 0x7FF);
    mcp2515.setFilter(MCP2515::RXF0, false, 0x100);
    mcp2515.setFilter(MCP2515::RXF1, false, 0x101);
    
    // RXB1: Accept range 0x200-0x2FF
    mcp2515.setFilterMask(MCP2515::MASK1, false, 0x700);
    mcp2515.setFilter(MCP2515::RXF2, false, 0x200);
    mcp2515.setFilter(MCP2515::RXF3, false, 0x200);
    mcp2515.setFilter(MCP2515::RXF4, false, 0x200);
    mcp2515.setFilter(MCP2515::RXF5, false, 0x200);
}

void loop() {
    struct can_frame frame;
    
    // Only filtered messages will be received
    if (mcp2515.readMessage(&frame) == MCP2515::ERROR_OK) {
        Serial.print("Received CAN ID: 0x");
        Serial.println(frame.can_id, HEX);
        
        // Process message
        processCANMessage(&frame);
    }
}

void processCANMessage(struct can_frame* frame) {
    // Handle filtered messages
    if (frame->can_id >= 0x100 && frame->can_id <= 0x101) {
        // High priority messages
        handleHighPriorityMessage(frame);
    } else if (frame->can_id >= 0x200 && frame->can_id <= 0x2FF) {
        // Sensor data messages
        handleSensorData(frame);
    }
}
```

### Example 4: Linux SocketCAN Hardware Filtering

```c
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <unistd.h>

int setup_can_filter(const char* interface) {
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_filter rfilter[3];
    
    // Create socket
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Get interface index
    strcpy(ifr.ifr_name, interface);
    ioctl(s, SIOCGIFINDEX, &ifr);
    
    // Bind to CAN interface
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        return -1;
    }
    
    // Configure hardware filters
    // Filter 0: Accept ID 0x100 exactly
    rfilter[0].can_id = 0x100;
    rfilter[0].can_mask = 0x7FF;  // All bits must match
    
    // Filter 1: Accept IDs 0x200-0x2FF
    rfilter[1].can_id = 0x200;
    rfilter[1].can_mask = 0x700;  // Only check upper 3 nibbles
    
    // Filter 2: Accept extended IDs 0x18FF**** 
    rfilter[2].can_id = 0x18FF0000 | CAN_EFF_FLAG;
    rfilter[2].can_mask = 0x1FFF0000 | CAN_EFF_FLAG;
    
    // Apply filters
    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, 
                   &rfilter, sizeof(rfilter)) < 0) {
        perror("Failed to set filters");
        return -1;
    }
    
    printf("Hardware filters configured successfully\n");
    return s;
}

void receive_filtered_messages(int socket) {
    struct can_frame frame;
    int nbytes;
    
    while (1) {
        nbytes = read(socket, &frame, sizeof(struct can_frame));
        
        if (nbytes < 0) {
            perror("Read error");
            break;
        }
        
        if (nbytes < sizeof(struct can_frame)) {
            fprintf(stderr, "Incomplete CAN frame\n");
            continue;
        }
        
        // Only filtered messages reach here
        printf("RX: ID=0x%03X DLC=%d Data=", 
               frame.can_id & CAN_EFF_MASK, frame.can_dlc);
        
        for (int i = 0; i < frame.can_dlc; i++) {
            printf("%02X ", frame.data[i]);
        }
        printf("\n");
    }
}

int main() {
    int can_socket = setup_can_filter("can0");
    
    if (can_socket < 0) {
        return 1;
    }
    
    receive_filtered_messages(can_socket);
    
    close(can_socket);
    return 0;
}
```

## Rust Implementation Examples

### Example 1: Using socketcan crate with Hardware Filtering

```rust
use socketcan::{CanSocket, CanFrame, CanFilter};
use std::time::Duration;

fn setup_can_with_filters() -> Result<CanSocket, Box<dyn std::error::Error>> {
    // Open CAN socket
    let socket = CanSocket::open("can0")?;
    
    // Configure hardware filters
    let mut filters = Vec::new();
    
    // Filter 1: Accept exactly ID 0x100
    filters.push(CanFilter::new(0x100, 0x7FF)?);
    
    // Filter 2: Accept IDs 0x200-0x2FF
    filters.push(CanFilter::new(0x200, 0x700)?);
    
    // Filter 3: Accept extended IDs starting with 0x18FF
    let extended_id = 0x18FF0000 | socketcan::EFF_FLAG;
    let extended_mask = 0x1FFF0000 | socketcan::EFF_FLAG;
    filters.push(CanFilter::new(extended_id, extended_mask)?);
    
    // Apply filters to socket
    socket.set_filters(&filters)?;
    
    // Set receive timeout
    socket.set_read_timeout(Duration::from_millis(100))?;
    
    println!("Hardware filters configured:");
    println!("  - Exact match: 0x100");
    println!("  - Range: 0x200-0x2FF");
    println!("  - Extended: 0x18FF****");
    
    Ok(socket)
}

fn receive_and_process_messages(socket: &CanSocket) -> Result<(), Box<dyn std::error::Error>> {
    loop {
        match socket.read_frame() {
            Ok(frame) => {
                // Only filtered messages arrive here
                process_filtered_frame(&frame);
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                // Timeout, continue
                continue;
            }
            Err(e) => {
                eprintln!("Error reading frame: {}", e);
                return Err(Box::new(e));
            }
        }
    }
}

fn process_filtered_frame(frame: &CanFrame) {
    let id = frame.id();
    let data = frame.data();
    
    println!("Received CAN message:");
    println!("  ID: 0x{:X}", id);
    println!("  DLC: {}", data.len());
    print!("  Data: ");
    for byte in data {
        print!("{:02X} ", byte);
    }
    println!();
    
    // Route based on ID ranges
    match id {
        0x100 => handle_control_message(data),
        0x200..=0x2FF => handle_sensor_message(id, data),
        _ if id >= 0x18FF0000 && id <= 0x18FFFFFF => handle_j1939_message(id, data),
        _ => println!("  Unknown message type"),
    }
}

fn handle_control_message(data: &[u8]) {
    println!("  -> Control command received");
    // Process control message
}

fn handle_sensor_message(id: u32, data: &[u8]) {
    let sensor_id = id & 0xFF;
    println!("  -> Sensor {} data", sensor_id);
    // Process sensor data
}

fn handle_j1939_message(id: u32, data: &[u8]) {
    let pgn = (id >> 8) & 0x1FFFF;
    println!("  -> J1939 PGN: 0x{:X}", pgn);
    // Process J1939 message
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("CAN Hardware Filtering Example");
    
    let socket = setup_can_with_filters()?;
    receive_and_process_messages(&socket)?;
    
    Ok(())
}
```

### Example 2: Advanced Filter Management

```rust
use socketcan::{CanSocket, CanFilter, CanFrame};
use std::collections::HashMap;

pub struct FilteredCanInterface {
    socket: CanSocket,
    active_filters: HashMap<String, CanFilter>,
}

impl FilteredCanInterface {
    pub fn new(interface: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let socket = CanSocket::open(interface)?;
        
        Ok(Self {
            socket,
            active_filters: HashMap::new(),
        })
    }
    
    /// Add a filter by name for easy management
    pub fn add_filter(&mut self, name: &str, id: u32, mask: u32) 
        -> Result<(), Box<dyn std::error::Error>> {
        
        let filter = CanFilter::new(id, mask)?;
        self.active_filters.insert(name.to_string(), filter);
        self.apply_filters()?;
        
        println!("Added filter '{}': ID=0x{:X}, Mask=0x{:X}", name, id, mask);
        Ok(())
    }
    
    /// Remove a filter by name
    pub fn remove_filter(&mut self, name: &str) -> Result<(), Box<dyn std::error::Error>> {
        if self.active_filters.remove(name).is_some() {
            self.apply_filters()?;
            println!("Removed filter '{}'", name);
        }
        Ok(())
    }
    
    /// Apply all active filters to the socket
    fn apply_filters(&self) -> Result<(), Box<dyn std::error::Error>> {
        let filters: Vec<&CanFilter> = self.active_filters.values().collect();
        
        if filters.is_empty() {
            // No filters = accept all
            println!("Warning: No filters active, accepting all messages");
        }
        
        self.socket.set_filters(&filters)?;
        Ok(())
    }
    
    /// Configure common filter patterns
    pub fn add_exact_match(&mut self, name: &str, id: u32) 
        -> Result<(), Box<dyn std::error::Error>> {
        self.add_filter(name, id, 0x7FF)  // Standard ID exact match
    }
    
    pub fn add_range(&mut self, name: &str, base_id: u32, range_bits: u32) 
        -> Result<(), Box<dyn std::error::Error>> {
        let mask = !(range_bits - 1) & 0x7FF;
        self.add_filter(name, base_id, mask)
    }
    
    pub fn add_extended_range(&mut self, name: &str, base_id: u32, mask: u32) 
        -> Result<(), Box<dyn std::error::Error>> {
        let extended_id = base_id | socketcan::EFF_FLAG;
        let extended_mask = mask | socketcan::EFF_FLAG;
        self.add_filter(name, extended_id, extended_mask)
    }
    
    /// Clear all filters (accept all messages)
    pub fn clear_filters(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        self.active_filters.clear();
        // Setting empty filter list accepts all
        self.socket.set_filters(&[])?;
        println!("All filters cleared");
        Ok(())
    }
    
    /// Receive filtered frame with timeout
    pub fn receive_frame(&self) -> Result<CanFrame, std::io::Error> {
        self.socket.read_frame()
    }
    
    /// List all active filters
    pub fn list_filters(&self) {
        println!("Active filters:");
        for (name, filter) in &self.active_filters {
            println!("  - {}: ID=0x{:X}, Mask=0x{:X}", 
                     name, filter.can_id(), filter.can_mask());
        }
    }
}

// Usage example
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut can = FilteredCanInterface::new("can0")?;
    
    // Add various filters
    can.add_exact_match("heartbeat", 0x100)?;
    can.add_range("sensors", 0x200, 16)?;  // 0x200-0x20F
    can.add_extended_range("j1939_engine", 0x18F00400, 0x1FFFF000)?;
    
    can.list_filters();
    
    // Receive filtered messages
    println!("\nReceiving filtered messages...");
    for _ in 0..10 {
        match can.receive_frame() {
            Ok(frame) => {
                println!("RX: ID=0x{:X}", frame.id());
            }
            Err(e) => eprintln!("Error: {}", e),
        }
    }
    
    // Dynamically modify filters
    can.remove_filter("heartbeat")?;
    can.add_exact_match("control", 0x150)?;
    
    Ok(())
}
```

### Example 3: Filter Configuration Builder Pattern

```rust
use socketcan::{CanSocket, CanFilter};

pub struct CanFilterBuilder {
    filters: Vec<CanFilter>,
}

impl CanFilterBuilder {
    pub fn new() -> Self {
        Self {
            filters: Vec::new(),
        }
    }
    
    /// Add exact ID match
    pub fn exact_id(mut self, id: u32) -> Result<Self, Box<dyn std::error::Error>> {
        self.filters.push(CanFilter::new(id, 0x7FF)?);
        Ok(self)
    }
    
    /// Add ID range (inclusive)
    pub fn id_range(mut self, start: u32, end: u32) 
        -> Result<Self, Box<dyn std::error::Error>> {
        
        // Calculate mask to cover the range
        let diff = end - start;
        let mask_bits = 11 - diff.leading_zeros();
        let mask = (!((1 << mask_bits) - 1)) & 0x7FF;
        
        self.filters.push(CanFilter::new(start, mask)?);
        Ok(self)
    }
    
    /// Add extended ID with custom mask
    pub fn extended_id(mut self, id: u32, mask: u32) 
        -> Result<Self, Box<dyn std::error::Error>> {
        
        let ext_id = id | socketcan::EFF_FLAG;
        let ext_mask = mask | socketcan::EFF_FLAG;
        self.filters.push(CanFilter::new(ext_id, ext_mask)?);
        Ok(self)
    }
    
    /// Accept all messages (no filtering)
    pub fn accept_all(self) -> Self {
        // Empty filter list = accept all
        Self {
            filters: Vec::new(),
        }
    }
    
    /// Build and apply to socket
    pub fn build(self, socket: &CanSocket) -> Result<(), Box<dyn std::error::Error>> {
        socket.set_filters(&self.filters)?;
        
        if self.filters.is_empty() {
            println!("No filters configured - accepting all messages");
        } else {
            println!("Configured {} filter(s)", self.filters.len());
        }
        
        Ok(())
    }
}

// Usage
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let socket = CanSocket::open("can0")?;
    
    // Configure filters using builder pattern
    CanFilterBuilder::new()
        .exact_id(0x100)?
        .exact_id(0x101)?
        .id_range(0x200, 0x2FF)?
        .extended_id(0x18FF0000, 0x1FFF0000)?
        .build(&socket)?;
    
    // Now receive only filtered messages
    loop {
        let frame = socket.read_frame()?;
        println!("Filtered message: ID=0x{:X}", frame.id());
    }
}
```

## Summary

**Hardware Filtering and Masking** is an essential CAN feature that offloads message filtering from software to hardware, providing significant performance and efficiency benefits:

### Key Concepts:
- **Acceptance Filters**: Define which message IDs to accept
- **Acceptance Masks**: Specify which ID bits must match (1) vs. don't care (0)
- **Filter Equation**: `(ID & Mask) == (Filter & Mask)` determines acceptance
- **Multiple Filter Banks**: Most controllers support 2-14 independent filters

### Implementation Approaches:
- **Exact Match**: Mask = 0x7FF (standard) or 0x1FFFFFFF (extended) - accepts only specific IDs
- **Range Filtering**: Partial mask to accept ID ranges (e.g., 0x200-0x2FF)
- **List Mode**: Multiple specific IDs using separate filter banks
- **Mixed Standard/Extended**: Separate filters for 11-bit and 29-bit identifiers

### Benefits:
- **90%+ CPU load reduction** in high-traffic networks
- **Lower interrupt overhead** - only relevant messages trigger interrupts
- **Improved real-time performance** for critical messages
- **Reduced power consumption** from less processing

### Best Practices:
- Configure filters during initialization before starting CAN
- Use exact match filters for critical, low-frequency messages
- Use range filters for related message groups (sensors, actuators)
- Combine hardware and software filtering for complex logic
- Test filter configuration thoroughly to avoid missing important messages
- Document filter configuration clearly in code comments

Hardware filtering is particularly valuable in automotive, industrial automation, and real-time systems where deterministic behavior and low latency are critical.