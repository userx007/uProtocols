# Network Management (NM) in Automotive Systems

Network Management (NM) is a critical component in automotive CAN networks that handles the coordination of sleep/wake mechanisms, node monitoring, and power management across the vehicle's electronic control units (ECUs). It ensures efficient power consumption while maintaining network availability when needed.

## Overview

Network Management addresses several key challenges in automotive systems:

- **Power Management**: Minimizing current consumption when the vehicle is off
- **Coordinated Sleep/Wake**: Ensuring all ECUs transition to sleep mode together
- **Network Monitoring**: Detecting node failures and network health
- **Partial Networking**: Allowing selective wake-up of network segments
- **Bus-Off Recovery**: Managing error recovery and network stability

## NM Protocol Types

### OSEK/VDX Direct NM
An older standard focused on token-based communication where each node gets a turn to transmit.

### AUTOSAR NM
The modern standard used in most contemporary vehicles, featuring:
- **Decentralized coordination** without a master node
- **State machine-based** operation
- **Configurable timers** for sleep/wake transitions
- **Support for partial networking**

## NM State Machine

The AUTOSAR NM defines several states:

1. **Bus-Sleep Mode**: Network is asleep, no communication
2. **Prepare Bus-Sleep Mode**: Transition period before sleep
3. **Ready Sleep Mode**: Ready to sleep, waiting for network silence
4. **Normal Operation Mode**: Active communication
5. **Repeat Message Mode**: Periodic NM message transmission

## Key Concepts

### NM Messages
Special CAN messages transmitted periodically to indicate node presence:
- **CAN ID**: Typically in the range 0x400-0x4FF
- **Data Length**: 8 bytes
- **Content**: Node ID, control bits, user data

### Timers
- **NM Timeout Timer**: Maximum time without NM message before sleep
- **Wait Bus-Sleep Timer**: Delay before entering bus-sleep
- **Repeat Message Timer**: Duration of repeat message transmission

### Wake-up Sources
- **CAN Wake-up**: Activity detected on CAN bus
- **Local Wake-up**: Internal ECU event (button press, sensor trigger)
- **External Wake-up**: Signal from another bus or wake line

## C/C++ Implementation

### Basic NM Message Structure

```c
#include <stdint.h>
#include <stdbool.h>

// NM Message Structure (8 bytes)
typedef struct {
    uint8_t source_node_id;      // Byte 0: Source node identifier
    uint8_t control_bits;        // Byte 1: Control bit vector
    uint8_t user_data[6];        // Bytes 2-7: Application data
} NM_Message_t;

// Control Bit Definitions
#define NM_CTRL_REPEAT_MSG_REQ   (1 << 0)  // Repeat message request
#define NM_CTRL_ACTIVE_WAKEUP    (1 << 4)  // Active wake-up bit
#define NM_CTRL_PARTIAL_NET      (1 << 6)  // Partial networking info

// NM States
typedef enum {
    NM_STATE_BUS_SLEEP,
    NM_STATE_PREPARE_BUS_SLEEP,
    NM_STATE_READY_SLEEP,
    NM_STATE_NORMAL_OPERATION,
    NM_STATE_REPEAT_MESSAGE
} NM_State_t;

// NM Configuration
typedef struct {
    uint8_t node_id;
    uint32_t msg_cycle_time_ms;      // Typical: 500ms
    uint32_t timeout_time_ms;        // Typical: 1500ms
    uint32_t wait_bus_sleep_time_ms; // Typical: 5000ms
    uint32_t repeat_msg_time_ms;     // Typical: 2000ms
} NM_Config_t;

// NM Runtime Data
typedef struct {
    NM_State_t state;
    uint32_t timer_msg_cycle;
    uint32_t timer_timeout;
    uint32_t timer_wait_bus_sleep;
    uint32_t timer_repeat_msg;
    bool network_requested;
    bool repeat_msg_requested;
    uint8_t active_nodes[32];        // Bit field for 256 nodes
} NM_Runtime_t;
```

### NM State Machine Implementation

```c
// Global NM data
static NM_Config_t nm_config;
static NM_Runtime_t nm_runtime;

void NM_Init(uint8_t node_id) {
    // Configure NM parameters
    nm_config.node_id = node_id;
    nm_config.msg_cycle_time_ms = 500;
    nm_config.timeout_time_ms = 1500;
    nm_config.wait_bus_sleep_time_ms = 5000;
    nm_config.repeat_msg_time_ms = 2000;
    
    // Initialize runtime state
    nm_runtime.state = NM_STATE_BUS_SLEEP;
    nm_runtime.network_requested = false;
    nm_runtime.repeat_msg_requested = false;
    
    // Clear active nodes
    for (int i = 0; i < 32; i++) {
        nm_runtime.active_nodes[i] = 0;
    }
}

void NM_NetworkRequest(void) {
    nm_runtime.network_requested = true;
    
    if (nm_runtime.state == NM_STATE_BUS_SLEEP) {
        // Transition to Repeat Message state
        nm_runtime.state = NM_STATE_REPEAT_MESSAGE;
        nm_runtime.timer_repeat_msg = nm_config.repeat_msg_time_ms;
        nm_runtime.timer_msg_cycle = 0;
        
        // Send NM message immediately
        NM_SendMessage(true);
    }
}

void NM_NetworkRelease(void) {
    nm_runtime.network_requested = false;
}

void NM_SendMessage(bool active_wakeup) {
    NM_Message_t msg;
    
    msg.source_node_id = nm_config.node_id;
    msg.control_bits = 0;
    
    if (active_wakeup) {
        msg.control_bits |= NM_CTRL_ACTIVE_WAKEUP;
    }
    
    if (nm_runtime.repeat_msg_requested) {
        msg.control_bits |= NM_CTRL_REPEAT_MSG_REQ;
    }
    
    // User data can carry application-specific information
    for (int i = 0; i < 6; i++) {
        msg.user_data[i] = 0x00;
    }
    
    // Send via CAN (pseudo-code)
    uint32_t nm_can_id = 0x400 + nm_config.node_id;
    CAN_Transmit(nm_can_id, (uint8_t*)&msg, 8);
}

void NM_ReceiveMessage(uint32_t can_id, uint8_t* data, uint8_t length) {
    if (length != 8) return;
    
    NM_Message_t* msg = (NM_Message_t*)data;
    
    // Mark node as active
    uint8_t node_id = msg->source_node_id;
    nm_runtime.active_nodes[node_id / 8] |= (1 << (node_id % 8));
    
    // Reset timeout timer
    nm_runtime.timer_timeout = nm_config.timeout_time_ms;
    
    // Check for repeat message request
    if (msg->control_bits & NM_CTRL_REPEAT_MSG_REQ) {
        if (nm_runtime.state == NM_STATE_READY_SLEEP) {
            // Transition back to Repeat Message state
            nm_runtime.state = NM_STATE_REPEAT_MESSAGE;
            nm_runtime.timer_repeat_msg = nm_config.repeat_msg_time_ms;
        }
    }
    
    // Wake-up on active wake-up bit
    if (msg->control_bits & NM_CTRL_ACTIVE_WAKEUP) {
        if (nm_runtime.state == NM_STATE_BUS_SLEEP ||
            nm_runtime.state == NM_STATE_PREPARE_BUS_SLEEP) {
            NM_NetworkRequest();
        }
    }
}

void NM_MainFunction(uint32_t elapsed_ms) {
    // Update timers
    if (nm_runtime.timer_msg_cycle > 0) {
        nm_runtime.timer_msg_cycle = (nm_runtime.timer_msg_cycle > elapsed_ms) ?
                                      (nm_runtime.timer_msg_cycle - elapsed_ms) : 0;
    }
    
    if (nm_runtime.timer_timeout > 0) {
        nm_runtime.timer_timeout = (nm_runtime.timer_timeout > elapsed_ms) ?
                                    (nm_runtime.timer_timeout - elapsed_ms) : 0;
    }
    
    if (nm_runtime.timer_wait_bus_sleep > 0) {
        nm_runtime.timer_wait_bus_sleep = (nm_runtime.timer_wait_bus_sleep > elapsed_ms) ?
                                          (nm_runtime.timer_wait_bus_sleep - elapsed_ms) : 0;
    }
    
    if (nm_runtime.timer_repeat_msg > 0) {
        nm_runtime.timer_repeat_msg = (nm_runtime.timer_repeat_msg > elapsed_ms) ?
                                       (nm_runtime.timer_repeat_msg - elapsed_ms) : 0;
    }
    
    // State machine
    switch (nm_runtime.state) {
        case NM_STATE_BUS_SLEEP:
            // Waiting for wake-up (handled in NetworkRequest or ReceiveMessage)
            break;
            
        case NM_STATE_REPEAT_MESSAGE:
            // Send NM messages periodically
            if (nm_runtime.timer_msg_cycle == 0) {
                NM_SendMessage(false);
                nm_runtime.timer_msg_cycle = nm_config.msg_cycle_time_ms;
            }
            
            // Check for transition to Normal Operation
            if (nm_runtime.timer_repeat_msg == 0) {
                nm_runtime.state = NM_STATE_NORMAL_OPERATION;
                nm_runtime.timer_timeout = nm_config.timeout_time_ms;
            }
            break;
            
        case NM_STATE_NORMAL_OPERATION:
            // Send NM messages periodically
            if (nm_runtime.timer_msg_cycle == 0) {
                NM_SendMessage(false);
                nm_runtime.timer_msg_cycle = nm_config.msg_cycle_time_ms;
            }
            
            // Check for transition to Ready Sleep
            if (!nm_runtime.network_requested && nm_runtime.timer_timeout == 0) {
                nm_runtime.state = NM_STATE_READY_SLEEP;
                nm_runtime.timer_wait_bus_sleep = nm_config.wait_bus_sleep_time_ms;
            }
            break;
            
        case NM_STATE_READY_SLEEP:
            // Still send NM messages
            if (nm_runtime.timer_msg_cycle == 0) {
                NM_SendMessage(false);
                nm_runtime.timer_msg_cycle = nm_config.msg_cycle_time_ms;
            }
            
            // Check for transition to Prepare Bus-Sleep
            if (nm_runtime.timer_wait_bus_sleep == 0) {
                nm_runtime.state = NM_STATE_PREPARE_BUS_SLEEP;
                
                // Notify application to prepare for sleep
                // Application_PrepareSleep();
            }
            break;
            
        case NM_STATE_PREPARE_BUS_SLEEP:
            // Stop sending NM messages
            // Transition to Bus-Sleep after CAN controller confirms silence
            nm_runtime.state = NM_STATE_BUS_SLEEP;
            
            // Put CAN controller in sleep mode
            // CAN_Sleep();
            break;
    }
}
```

### Monitoring Active Nodes

```c
bool NM_IsNodeActive(uint8_t node_id) {
    if (node_id >= 256) return false;
    
    return (nm_runtime.active_nodes[node_id / 8] & (1 << (node_id % 8))) != 0;
}

uint8_t NM_GetActiveNodeCount(void) {
    uint8_t count = 0;
    
    for (int i = 0; i < 32; i++) {
        uint8_t byte = nm_runtime.active_nodes[i];
        // Count set bits
        while (byte) {
            count += byte & 1;
            byte >>= 1;
        }
    }
    
    return count;
}

void NM_PrintActiveNodes(void) {
    printf("Active nodes: ");
    for (int i = 0; i < 256; i++) {
        if (NM_IsNodeActive(i)) {
            printf("%d ", i);
        }
    }
    printf("\n");
}
```

## Rust Implementation

### NM Data Structures

```rust
use std::collections::HashSet;
use std::time::{Duration, Instant};

// NM Message Structure
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct NmMessage {
    source_node_id: u8,
    control_bits: u8,
    user_data: [u8; 6],
}

impl NmMessage {
    const CTRL_REPEAT_MSG_REQ: u8 = 1 << 0;
    const CTRL_ACTIVE_WAKEUP: u8 = 1 << 4;
    const CTRL_PARTIAL_NET: u8 = 1 << 6;
    
    pub fn new(node_id: u8, active_wakeup: bool, repeat_msg_req: bool) -> Self {
        let mut control_bits = 0u8;
        
        if active_wakeup {
            control_bits |= Self::CTRL_ACTIVE_WAKEUP;
        }
        
        if repeat_msg_req {
            control_bits |= Self::CTRL_REPEAT_MSG_REQ;
        }
        
        NmMessage {
            source_node_id: node_id,
            control_bits,
            user_data: [0; 6],
        }
    }
    
    pub fn has_active_wakeup(&self) -> bool {
        self.control_bits & Self::CTRL_ACTIVE_WAKEUP != 0
    }
    
    pub fn has_repeat_msg_request(&self) -> bool {
        self.control_bits & Self::CTRL_REPEAT_MSG_REQ != 0
    }
    
    pub fn to_bytes(&self) -> [u8; 8] {
        let mut bytes = [0u8; 8];
        bytes[0] = self.source_node_id;
        bytes[1] = self.control_bits;
        bytes[2..8].copy_from_slice(&self.user_data);
        bytes
    }
    
    pub fn from_bytes(data: &[u8]) -> Option<Self> {
        if data.len() != 8 {
            return None;
        }
        
        let mut user_data = [0u8; 6];
        user_data.copy_from_slice(&data[2..8]);
        
        Some(NmMessage {
            source_node_id: data[0],
            control_bits: data[1],
            user_data,
        })
    }
}

// NM States
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NmState {
    BusSleep,
    PrepareBusSleep,
    ReadySleep,
    NormalOperation,
    RepeatMessage,
}

// NM Configuration
#[derive(Debug, Clone)]
pub struct NmConfig {
    pub node_id: u8,
    pub msg_cycle_time: Duration,
    pub timeout_time: Duration,
    pub wait_bus_sleep_time: Duration,
    pub repeat_msg_time: Duration,
}

impl Default for NmConfig {
    fn default() -> Self {
        NmConfig {
            node_id: 0,
            msg_cycle_time: Duration::from_millis(500),
            timeout_time: Duration::from_millis(1500),
            wait_bus_sleep_time: Duration::from_millis(5000),
            repeat_msg_time: Duration::from_millis(2000),
        }
    }
}

// Timer helper
struct Timer {
    start: Option<Instant>,
    duration: Duration,
}

impl Timer {
    fn new(duration: Duration) -> Self {
        Timer {
            start: None,
            duration,
        }
    }
    
    fn start(&mut self) {
        self.start = Some(Instant::now());
    }
    
    fn stop(&mut self) {
        self.start = None;
    }
    
    fn is_expired(&self) -> bool {
        if let Some(start) = self.start {
            start.elapsed() >= self.duration
        } else {
            false
        }
    }
    
    fn is_running(&self) -> bool {
        self.start.is_some()
    }
}

// NM Manager
pub struct NetworkManager {
    config: NmConfig,
    state: NmState,
    network_requested: bool,
    repeat_msg_requested: bool,
    active_nodes: HashSet<u8>,
    
    timer_msg_cycle: Timer,
    timer_timeout: Timer,
    timer_wait_bus_sleep: Timer,
    timer_repeat_msg: Timer,
}

impl NetworkManager {
    pub fn new(config: NmConfig) -> Self {
        NetworkManager {
            timer_msg_cycle: Timer::new(config.msg_cycle_time),
            timer_timeout: Timer::new(config.timeout_time),
            timer_wait_bus_sleep: Timer::new(config.wait_bus_sleep_time),
            timer_repeat_msg: Timer::new(config.repeat_msg_time),
            config,
            state: NmState::BusSleep,
            network_requested: false,
            repeat_msg_requested: false,
            active_nodes: HashSet::new(),
        }
    }
    
    pub fn network_request(&mut self) -> Option<Vec<u8>> {
        self.network_requested = true;
        
        if self.state == NmState::BusSleep {
            self.state = NmState::RepeatMessage;
            self.timer_repeat_msg.start();
            self.timer_msg_cycle.start();
            
            // Send NM message immediately with active wake-up
            return Some(self.create_nm_message(true));
        }
        
        None
    }
    
    pub fn network_release(&mut self) {
        self.network_requested = false;
    }
    
    pub fn receive_message(&mut self, can_id: u32, data: &[u8]) -> Option<Vec<u8>> {
        if let Some(msg) = NmMessage::from_bytes(data) {
            // Mark node as active
            self.active_nodes.insert(msg.source_node_id);
            
            // Reset timeout timer
            self.timer_timeout.stop();
            self.timer_timeout.start();
            
            // Handle repeat message request
            if msg.has_repeat_msg_request() {
                if self.state == NmState::ReadySleep {
                    self.state = NmState::RepeatMessage;
                    self.timer_repeat_msg.start();
                }
            }
            
            // Handle active wake-up
            if msg.has_active_wakeup() {
                if self.state == NmState::BusSleep || 
                   self.state == NmState::PrepareBusSleep {
                    return self.network_request();
                }
            }
        }
        
        None
    }
    
    fn create_nm_message(&self, active_wakeup: bool) -> Vec<u8> {
        let msg = NmMessage::new(
            self.config.node_id,
            active_wakeup,
            self.repeat_msg_requested
        );
        msg.to_bytes().to_vec()
    }
    
    pub fn main_function(&mut self) -> Option<Vec<u8>> {
        let mut msg_to_send = None;
        
        match self.state {
            NmState::BusSleep => {
                // Waiting for wake-up
            }
            
            NmState::RepeatMessage => {
                // Send NM messages periodically
                if self.timer_msg_cycle.is_expired() {
                    msg_to_send = Some(self.create_nm_message(false));
                    self.timer_msg_cycle.start();
                }
                
                // Check for transition to Normal Operation
                if self.timer_repeat_msg.is_expired() {
                    self.state = NmState::NormalOperation;
                    self.timer_timeout.start();
                }
            }
            
            NmState::NormalOperation => {
                // Send NM messages periodically
                if self.timer_msg_cycle.is_expired() {
                    msg_to_send = Some(self.create_nm_message(false));
                    self.timer_msg_cycle.start();
                }
                
                // Check for transition to Ready Sleep
                if !self.network_requested && self.timer_timeout.is_expired() {
                    self.state = NmState::ReadySleep;
                    self.timer_wait_bus_sleep.start();
                }
            }
            
            NmState::ReadySleep => {
                // Still send NM messages
                if self.timer_msg_cycle.is_expired() {
                    msg_to_send = Some(self.create_nm_message(false));
                    self.timer_msg_cycle.start();
                }
                
                // Check for transition to Prepare Bus-Sleep
                if self.timer_wait_bus_sleep.is_expired() {
                    self.state = NmState::PrepareBusSleep;
                    println!("Preparing for bus sleep...");
                }
            }
            
            NmState::PrepareBusSleep => {
                // Stop all timers
                self.timer_msg_cycle.stop();
                self.timer_timeout.stop();
                self.timer_wait_bus_sleep.stop();
                
                // Transition to Bus-Sleep
                self.state = NmState::BusSleep;
                println!("Entering bus sleep mode");
            }
        }
        
        msg_to_send
    }
    
    pub fn get_state(&self) -> NmState {
        self.state
    }
    
    pub fn is_node_active(&self, node_id: u8) -> bool {
        self.active_nodes.contains(&node_id)
    }
    
    pub fn get_active_nodes(&self) -> Vec<u8> {
        let mut nodes: Vec<u8> = self.active_nodes.iter().copied().collect();
        nodes.sort();
        nodes
    }
}
```

### Example Usage

```rust
use std::thread;
use std::time::Duration;

fn main() {
    // Create NM configuration
    let mut config = NmConfig::default();
    config.node_id = 10;
    
    // Create Network Manager
    let mut nm = NetworkManager::new(config);
    
    println!("Network Manager initialized for Node {}", 10);
    println!("Initial state: {:?}", nm.get_state());
    
    // Request network
    println!("\nRequesting network...");
    if let Some(msg) = nm.network_request() {
        println!("Sending NM message: {:02X?}", msg);
    }
    
    println!("State after request: {:?}", nm.get_state());
    
    // Simulate main loop
    for i in 0..20 {
        thread::sleep(Duration::from_millis(100));
        
        // Simulate receiving NM messages from other nodes
        if i == 5 {
            let remote_msg = NmMessage::new(20, false, false);
            println!("\nReceived NM message from Node 20");
            nm.receive_message(0x414, &remote_msg.to_bytes());
        }
        
        if i == 10 {
            let remote_msg = NmMessage::new(30, false, false);
            println!("Received NM message from Node 30");
            nm.receive_message(0x41E, &remote_msg.to_bytes());
        }
        
        // Call main function
        if let Some(msg) = nm.main_function() {
            println!("Cycle {}: Sending NM message: {:02X?}", i, msg);
        }
        
        // Print active nodes periodically
        if i % 5 == 0 {
            println!("Active nodes: {:?}", nm.get_active_nodes());
        }
    }
    
    // Release network
    println!("\nReleasing network...");
    nm.network_release();
    
    // Continue running to observe sleep transition
    for i in 0..50 {
        thread::sleep(Duration::from_millis(100));
        
        if let Some(msg) = nm.main_function() {
            println!("Cycle {}: Sending NM message: {:02X?}", i, msg);
        }
        
        if i % 10 == 0 {
            println!("State: {:?}", nm.get_state());
        }
    }
}
```

## Advanced Features

### Partial Networking

Partial networking allows selective wake-up of network segments, reducing power consumption:

```c
// Partial Network Information (PNI)
typedef struct {
    uint8_t pni_filter[6];  // Bit field for requested partial networks
} PN_Info_t;

bool NM_CheckPartialNetworkMatch(NM_Message_t* msg, PN_Info_t* local_pni) {
    // Check if any requested partial network matches
    for (int i = 0; i < 6; i++) {
        if (msg->user_data[i] & local_pni->pni_filter[i]) {
            return true;  // Wake-up this node
        }
    }
    return false;  // Stay asleep
}
```

### Coordinated Shutdown

```rust
pub struct CoordinatedShutdown {
    shutdown_requested: bool,
    nodes_ready: HashSet<u8>,
    expected_nodes: HashSet<u8>,
}

impl CoordinatedShutdown {
    pub fn request_shutdown(&mut self) {
        self.shutdown_requested = true;
        self.nodes_ready.clear();
    }
    
    pub fn node_ready(&mut self, node_id: u8) {
        self.nodes_ready.insert(node_id);
    }
    
    pub fn all_nodes_ready(&self) -> bool {
        self.shutdown_requested && 
        self.nodes_ready == self.expected_nodes
    }
}
```

## Summary

**Network Management (NM)** is essential for automotive CAN networks, providing coordinated sleep/wake mechanisms and power management. Key aspects include:

- **State-based operation** with well-defined transitions between Bus-Sleep, Repeat Message, Normal Operation, Ready Sleep, and Prepare Bus-Sleep modes
- **Periodic NM messages** broadcast by each node to indicate presence and maintain network awareness
- **Configurable timers** control state transitions and ensure coordinated network behavior
- **Active node monitoring** through received NM messages enables detection of network participants
- **Power optimization** by ensuring all nodes sleep together when network communication isn't needed
- **Wake-up coordination** using active wake-up bits in NM messages to bring sleeping nodes online
- **Partial networking support** for selective wake-up of network segments, further reducing power consumption

Both C/C++ and Rust implementations demonstrate the core NM functionality with state machines, timer management, and message handling. The examples show how NM integrates with CAN communication to provide robust, power-efficient network coordination in automotive systems.