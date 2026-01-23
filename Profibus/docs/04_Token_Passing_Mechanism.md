# Token Passing Mechanism in Profibus

## Overview

The Token Passing Mechanism is a critical component of Profibus networks that enables deterministic, fair access to the communication medium among multiple master devices. Unlike simple master-slave architectures, Profibus supports multi-master configurations where several active stations need to coordinate bus access without collisions.

## Core Concepts

### Token Ring Protocol

In Profibus, masters form a logical token ring. A special frame called a "token" circulates among the masters in ascending order of their station addresses. Only the master currently holding the token has the right to initiate communication on the bus. This ensures:

- **Deterministic behavior**: Predictable response times
- **Fair access**: Each master gets its turn
- **Collision avoidance**: Only one master transmits at a time
- **Scalability**: Supports multiple masters (up to 32 active stations)

### Key Components

**Token Frame**: A small frame passed between masters containing:
- Source address (SA)
- Destination address (DA)
- Frame control byte

**Token Rotation Time (TTR)**: The actual time it takes for the token to complete one full cycle through all masters.

**Target Token Rotation Time (TTRT)**: The configured maximum acceptable token rotation time.

**Bus Arbitration**: The process by which masters coordinate who can access the bus and when.

### State Machine

Each master operates a state machine with these primary states:
- **OFFLINE**: Not participating in token ring
- **LISTEN_TOKEN**: Monitoring token passing
- **ACTIVE_IDLE**: In ring, waiting for token
- **TOKEN_HOLDER**: Has token, can transmit

## Programming Examples

### C/C++ Implementation

```c
/*
 * Profibus Token Passing Mechanism Implementation in C
 * Demonstrates token ring protocol and bus arbitration
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Token frame structure
typedef struct {
    uint8_t sd;           // Start delimiter (0xDC)
    uint8_t da;           // Destination address
    uint8_t sa;           // Source address
    uint8_t ed;           // End delimiter (0x16)
} TokenFrame;

// Master station states
typedef enum {
    STATE_OFFLINE,
    STATE_LISTEN_TOKEN,
    STATE_ACTIVE_IDLE,
    STATE_TOKEN_HOLDER,
    STATE_WAIT_FOR_RESPONSE
} MasterState;

// Master station configuration
typedef struct {
    uint8_t station_addr;
    uint8_t next_station;
    uint8_t prev_station;
    MasterState state;
    uint32_t token_hold_time;      // microseconds
    uint32_t slot_time;             // microseconds
    uint32_t target_rotation_time; // microseconds
    uint32_t actual_rotation_time; // microseconds
    uint32_t last_token_time;      // timestamp
    bool in_ring;
} ProfibusStation;

// Function prototypes
void init_station(ProfibusStation *station, uint8_t addr);
void send_token(ProfibusStation *station, uint8_t dest_addr);
bool receive_token(ProfibusStation *station, TokenFrame *frame);
void handle_token_timeout(ProfibusStation *station);
void update_token_rotation_time(ProfibusStation *station, uint32_t current_time);
void perform_bus_arbitration(ProfibusStation *station);

// Initialize a Profibus master station
void init_station(ProfibusStation *station, uint8_t addr) {
    station->station_addr = addr;
    station->next_station = 0xFF;  // Unknown initially
    station->prev_station = 0xFF;
    station->state = STATE_OFFLINE;
    station->token_hold_time = 100000;      // 100ms default
    station->slot_time = 37000;              // 37ms default
    station->target_rotation_time = 1000000; // 1s default
    station->actual_rotation_time = 0;
    station->last_token_time = 0;
    station->in_ring = false;
}

// Send token to next master in ring
void send_token(ProfibusStation *station, uint8_t dest_addr) {
    TokenFrame token;
    
    token.sd = 0xDC;  // Token start delimiter
    token.sa = station->station_addr;
    token.da = dest_addr;
    token.ed = 0x16;  // End delimiter
    
    // Simulate sending token frame
    printf("Station %d: Sending token to station %d\n", 
           station->station_addr, dest_addr);
    
    // In real implementation, write to UART or SPI
    // uart_write(&token, sizeof(TokenFrame));
    
    station->state = STATE_ACTIVE_IDLE;
}

// Receive and validate token frame
bool receive_token(ProfibusStation *station, TokenFrame *frame) {
    // Validate token frame
    if (frame->sd != 0xDC || frame->ed != 0x16) {
        return false;
    }
    
    // Check if token is addressed to this station
    if (frame->da != station->station_addr) {
        return false;
    }
    
    printf("Station %d: Received token from station %d\n", 
           station->station_addr, frame->sa);
    
    station->state = STATE_TOKEN_HOLDER;
    station->prev_station = frame->sa;
    
    return true;
}

// Calculate and update token rotation time
void update_token_rotation_time(ProfibusStation *station, uint32_t current_time) {
    if (station->last_token_time > 0) {
        station->actual_rotation_time = current_time - station->last_token_time;
        
        printf("Station %d: Token rotation time = %u us (Target: %u us)\n",
               station->station_addr, 
               station->actual_rotation_time,
               station->target_rotation_time);
        
        // Check for rotation time violation
        if (station->actual_rotation_time > station->target_rotation_time) {
            printf("WARNING: Token rotation time exceeded!\n");
        }
    }
    
    station->last_token_time = current_time;
}

// Perform bus arbitration when joining the ring
void perform_bus_arbitration(ProfibusStation *station) {
    printf("Station %d: Performing bus arbitration\n", station->station_addr);
    
    // Listen for token passing
    station->state = STATE_LISTEN_TOKEN;
    
    // Gap monitoring - detect gaps in token ring
    // If no token seen for 2 * slot_time, attempt to claim bus
    uint32_t gap_timeout = 2 * station->slot_time;
    
    // Simplified arbitration logic
    // Real implementation would monitor bus activity
    
    // After successful arbitration
    station->in_ring = true;
    station->state = STATE_ACTIVE_IDLE;
    
    printf("Station %d: Successfully joined token ring\n", station->station_addr);
}

// Handle token timeout (token lost scenario)
void handle_token_timeout(ProfibusStation *station) {
    printf("Station %d: Token timeout detected\n", station->station_addr);
    
    // Attempt to regenerate token ring
    station->state = STATE_LISTEN_TOKEN;
    
    // Wait for gap and claim token
    uint32_t timeout = station->slot_time * (256 - station->station_addr);
    
    // After timeout, generate new token
    if (station->next_station != 0xFF) {
        send_token(station, station->next_station);
    }
}

// Process token ring operation
void process_token_ring(ProfibusStation *station, uint32_t current_time) {
    switch (station->state) {
        case STATE_OFFLINE:
            // Attempt to join ring
            perform_bus_arbitration(station);
            break;
            
        case STATE_LISTEN_TOKEN:
            // Monitor for token passing
            break;
            
        case STATE_ACTIVE_IDLE:
            // Waiting to receive token
            break;
            
        case STATE_TOKEN_HOLDER:
            // We have the token - perform master operations
            printf("Station %d: Holding token, can transmit\n", station->station_addr);
            
            // Update rotation time
            update_token_rotation_time(station, current_time);
            
            // Perform slave communication here
            // poll_slaves(station);
            
            // Pass token to next station
            if (station->next_station != 0xFF) {
                send_token(station, station->next_station);
            }
            break;
            
        case STATE_WAIT_FOR_RESPONSE:
            // Waiting for slave response
            break;
    }
}

// Example usage
int main(void) {
    ProfibusStation station1, station2, station3;
    
    // Initialize three masters in a ring
    init_station(&station1, 5);
    init_station(&station2, 10);
    init_station(&station3, 15);
    
    // Configure ring topology
    station1.next_station = 10;
    station2.next_station = 15;
    station3.next_station = 5;
    
    // Simulate token ring operation
    printf("=== Profibus Token Ring Simulation ===\n\n");
    
    // Station 1 gets initial token
    station1.state = STATE_TOKEN_HOLDER;
    station1.in_ring = true;
    
    // Simulate token passing
    uint32_t time = 0;
    
    process_token_ring(&station1, time);
    time += 150000;
    
    TokenFrame token = {0xDC, 10, 5, 0x16};
    receive_token(&station2, &token);
    process_token_ring(&station2, time);
    time += 150000;
    
    token.da = 15;
    token.sa = 10;
    receive_token(&station3, &token);
    process_token_ring(&station3, time);
    
    return 0;
}
```

### Rust Implementation

```rust
/*
 * Profibus Token Passing Mechanism Implementation in Rust
 * Type-safe implementation with ownership guarantees
 */

use std::time::{Duration, Instant};

// Token frame structure
#[derive(Debug, Clone, Copy)]
struct TokenFrame {
    sd: u8,  // Start delimiter (0xDC)
    da: u8,  // Destination address
    sa: u8,  // Source address
    ed: u8,  // End delimiter (0x16)
}

impl TokenFrame {
    fn new(source: u8, dest: u8) -> Self {
        TokenFrame {
            sd: 0xDC,
            da: dest,
            sa: source,
            ed: 0x16,
        }
    }

    fn is_valid(&self) -> bool {
        self.sd == 0xDC && self.ed == 0x16
    }
}

// Master station states
#[derive(Debug, Clone, Copy, PartialEq)]
enum MasterState {
    Offline,
    ListenToken,
    ActiveIdle,
    TokenHolder,
    WaitForResponse,
}

// Token ring configuration
#[derive(Debug, Clone)]
struct TokenConfig {
    target_rotation_time: Duration,
    token_hold_time: Duration,
    slot_time: Duration,
}

impl Default for TokenConfig {
    fn default() -> Self {
        TokenConfig {
            target_rotation_time: Duration::from_millis(1000),
            token_hold_time: Duration::from_millis(100),
            slot_time: Duration::from_micros(37000),
        }
    }
}

// Profibus master station
#[derive(Debug)]
struct ProfibusStation {
    station_addr: u8,
    next_station: Option<u8>,
    prev_station: Option<u8>,
    state: MasterState,
    config: TokenConfig,
    actual_rotation_time: Option<Duration>,
    last_token_time: Option<Instant>,
    in_ring: bool,
}

impl ProfibusStation {
    fn new(addr: u8, config: TokenConfig) -> Self {
        ProfibusStation {
            station_addr: addr,
            next_station: None,
            prev_station: None,
            state: MasterState::Offline,
            config,
            actual_rotation_time: None,
            last_token_time: None,
            in_ring: false,
        }
    }

    fn send_token(&mut self, dest_addr: u8) -> Result<(), String> {
        let token = TokenFrame::new(self.station_addr, dest_addr);
        
        println!(
            "Station {}: Sending token to station {}",
            self.station_addr, dest_addr
        );
        
        // In real implementation, send via serial interface
        // self.interface.write(&token)?;
        
        self.state = MasterState::ActiveIdle;
        Ok(())
    }

    fn receive_token(&mut self, frame: &TokenFrame) -> Result<(), String> {
        if !frame.is_valid() {
            return Err("Invalid token frame".to_string());
        }

        if frame.da != self.station_addr {
            return Err("Token not addressed to this station".to_string());
        }

        println!(
            "Station {}: Received token from station {}",
            self.station_addr, frame.sa
        );

        self.state = MasterState::TokenHolder;
        self.prev_station = Some(frame.sa);
        
        Ok(())
    }

    fn update_rotation_time(&mut self, current_time: Instant) {
        if let Some(last_time) = self.last_token_time {
            let rotation_time = current_time.duration_since(last_time);
            self.actual_rotation_time = Some(rotation_time);

            println!(
                "Station {}: Token rotation time = {:?} (Target: {:?})",
                self.station_addr, rotation_time, self.config.target_rotation_time
            );

            if rotation_time > self.config.target_rotation_time {
                println!("WARNING: Token rotation time exceeded!");
            }
        }

        self.last_token_time = Some(current_time);
    }

    fn perform_bus_arbitration(&mut self) -> Result<(), String> {
        println!("Station {}: Performing bus arbitration", self.station_addr);

        self.state = MasterState::ListenToken;

        // Gap monitoring logic
        let gap_timeout = self.config.slot_time * 2;
        
        // Simulate successful arbitration
        self.in_ring = true;
        self.state = MasterState::ActiveIdle;

        println!("Station {}: Successfully joined token ring", self.station_addr);
        Ok(())
    }

    fn handle_token_timeout(&mut self) -> Result<(), String> {
        println!("Station {}: Token timeout detected", self.station_addr);

        // Regenerate token ring
        self.state = MasterState::ListenToken;

        // Calculate timeout based on station address
        let timeout_factor = 256 - self.station_addr as u32;
        let timeout = self.config.slot_time * timeout_factor;

        // Generate new token after timeout
        if let Some(next) = self.next_station {
            self.send_token(next)?;
        }

        Ok(())
    }

    fn process_token_holder_state(&mut self, current_time: Instant) -> Result<(), String> {
        println!("Station {}: Holding token, can transmit", self.station_addr);

        self.update_rotation_time(current_time);

        // Perform slave communication
        // self.poll_slaves()?;

        // Pass token to next station
        if let Some(next) = self.next_station {
            self.send_token(next)?;
        }

        Ok(())
    }

    fn process(&mut self, current_time: Instant) -> Result<(), String> {
        match self.state {
            MasterState::Offline => {
                self.perform_bus_arbitration()?;
            }
            MasterState::ListenToken => {
                // Monitor for token passing
            }
            MasterState::ActiveIdle => {
                // Waiting to receive token
            }
            MasterState::TokenHolder => {
                self.process_token_holder_state(current_time)?;
            }
            MasterState::WaitForResponse => {
                // Waiting for slave response
            }
        }
        Ok(())
    }
}

// Token ring manager for coordinating multiple masters
struct TokenRing {
    masters: Vec<ProfibusStation>,
}

impl TokenRing {
    fn new() -> Self {
        TokenRing {
            masters: Vec::new(),
        }
    }

    fn add_master(&mut self, mut station: ProfibusStation) {
        // Add to logical ring
        if !self.masters.is_empty() {
            let last_idx = self.masters.len() - 1;
            let last_addr = self.masters[last_idx].station_addr;
            
            self.masters[last_idx].next_station = Some(station.station_addr);
            station.prev_station = Some(last_addr);
            
            // Close the ring
            if self.masters.len() == 1 {
                station.next_station = Some(self.masters[0].station_addr);
                self.masters[0].prev_station = Some(station.station_addr);
            }
        }
        
        self.masters.push(station);
    }

    fn close_ring(&mut self) {
        if self.masters.len() >= 2 {
            let first_addr = self.masters[0].station_addr;
            let last_idx = self.masters.len() - 1;
            let last_addr = self.masters[last_idx].station_addr;
            
            self.masters[last_idx].next_station = Some(first_addr);
            self.masters[0].prev_station = Some(last_addr);
        }
    }

    fn simulate(&mut self) {
        println!("=== Profibus Token Ring Simulation ===\n");

        if self.masters.is_empty() {
            return;
        }

        // Give initial token to first master
        self.masters[0].state = MasterState::TokenHolder;
        self.masters[0].in_ring = true;

        let start_time = Instant::now();
        let mut current_time = start_time;

        for i in 0..self.masters.len() {
            if let Err(e) = self.masters[i].process(current_time) {
                eprintln!("Error processing station {}: {}", self.masters[i].station_addr, e);
            }
            
            current_time += Duration::from_millis(150);

            // Simulate token reception for next master
            if i < self.masters.len() - 1 {
                let next_idx = (i + 1) % self.masters.len();
                let token = TokenFrame::new(
                    self.masters[i].station_addr,
                    self.masters[next_idx].station_addr,
                );
                
                if let Err(e) = self.masters[next_idx].receive_token(&token) {
                    eprintln!("Error: {}", e);
                }
            }
        }
    }
}

fn main() {
    let config = TokenConfig::default();

    let mut ring = TokenRing::new();
    
    ring.add_master(ProfibusStation::new(5, config.clone()));
    ring.add_master(ProfibusStation::new(10, config.clone()));
    ring.add_master(ProfibusStation::new(15, config.clone()));
    
    ring.close_ring();
    ring.simulate();
}
```

## Technical Deep Dive

### Token Passing Algorithm

The token passing algorithm follows these steps:

1. **Ring Initialization**: Masters discover each other and establish the logical ring order
2. **Token Circulation**: Token passes from master to master in ascending address order
3. **Gap Detection**: Each master monitors for breaks in the ring
4. **Token Regeneration**: If token is lost, the ring regenerates it automatically

### Timing Parameters

**Slot Time**: The time a station waits before attempting to claim the bus during arbitration. Calculated as:
```
Slot_Time = 256 × Bit_Time + Gap_Time
```

**Token Hold Time**: Maximum time a master can hold the token before passing it on.

**Target Rotation Time (TRT)**: The maximum time allowed for one complete token rotation. Ensures real-time guarantees.

### Bus Arbitration Process

When a new master wants to join or the token is lost:

1. Master enters LISTEN mode
2. Monitors bus for token passing activity
3. Detects gap in token ring (no activity for slot_time × 2)
4. Waits for station-specific timeout: `(256 - station_address) × slot_time`
5. Claims bus and generates token if no other station claimed it first

This ensures the lowest-addressed station claims the bus first, preventing conflicts.

### Gap Maintenance

Masters periodically send "gap update" frames to discover new stations that want to join the ring. This allows dynamic ring reconfiguration without disrupting ongoing communication.

## Summary

The Token Passing Mechanism in Profibus provides deterministic, collision-free medium access for multi-master networks through a logical token ring. Key aspects include:

- **Fairness**: Each master gets equal opportunity to access the bus
- **Determinism**: Token rotation time provides predictable response times critical for industrial control
- **Fault Tolerance**: Automatic token regeneration handles lost tokens
- **Scalability**: Supports up to 32 active masters on one network
- **Dynamic Reconfiguration**: Masters can join/leave the ring during operation

The mechanism balances efficiency with robustness, making Profibus suitable for real-time industrial applications where timing guarantees are essential. Understanding token passing is fundamental for implementing Profibus masters and diagnosing network timing issues.