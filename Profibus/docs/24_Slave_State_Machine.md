# Profibus DP Slave State Machine

## Overview

The Profibus DP (Decentralized Periphery) slave state machine defines the operational states and transitions that a slave device undergoes during its lifecycle on a Profibus network. Understanding these states is crucial for proper slave implementation and diagnostics.

## The Five Primary States

### 1. **Offline State**
The slave is not participating in network communication. This occurs when:
- The device is powered off or not connected
- Communication has been lost or interrupted
- The slave has been explicitly taken offline by the master

### 2. **Stop State**
The slave is communicating with the master but is not processing I/O data. In this state:
- Diagnostic and parameter data can be exchanged
- Configuration and parameterization occur
- Outputs are typically set to safe/default values
- The slave responds to the master but doesn't update process data

### 3. **Clear State**
A transitional state where:
- The slave clears its outputs to safe states (typically zero or predefined safe values)
- Input data continues to be read
- The slave is preparing to enter operational mode
- This ensures a controlled transition to active operation

### 4. **Operate State**
The fully operational state where:
- Cyclic input/output data exchange occurs
- The slave performs its intended control functions
- Process data is actively updated
- Normal productive operation is taking place

### 5. **Wait-Prm State** (Implicit)
An intermediate state waiting for parameterization from the master before transitioning to other states.

## State Transitions

The transitions between states are controlled by commands from the DP master (Class 1):

```
Power On → Offline → Wait-Prm → Stop → Clear → Operate
                ↑         ↓         ↓       ↓       ↓
                └─────────┴─────────┴───────┴───────┘
                   (Various error conditions)
```

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>

// Profibus DP Slave States
typedef enum {
    DP_STATE_OFFLINE = 0,
    DP_STATE_WAIT_PRM,
    DP_STATE_STOP,
    DP_STATE_CLEAR,
    DP_STATE_OPERATE
} dp_slave_state_t;

// State machine context
typedef struct {
    dp_slave_state_t current_state;
    dp_slave_state_t previous_state;
    uint32_t state_entry_time;
    bool watchdog_active;
    uint16_t watchdog_timeout_ms;
} dp_slave_context_t;

// Function prototypes
void dp_slave_init(dp_slave_context_t* ctx);
void dp_slave_process(dp_slave_context_t* ctx);
void dp_slave_set_outputs_safe(void);
void dp_slave_clear_outputs(void);
void dp_slave_update_io(void);

// State machine initialization
void dp_slave_init(dp_slave_context_t* ctx) {
    ctx->current_state = DP_STATE_OFFLINE;
    ctx->previous_state = DP_STATE_OFFLINE;
    ctx->state_entry_time = 0;
    ctx->watchdog_active = false;
    ctx->watchdog_timeout_ms = 1000;
}

// Handle state transitions based on master commands
void dp_slave_handle_command(dp_slave_context_t* ctx, uint8_t command) {
    dp_slave_state_t new_state = ctx->current_state;
    
    switch (command) {
        case 0x00: // Master sends "Set_Prm" telegram
            if (ctx->current_state == DP_STATE_OFFLINE ||
                ctx->current_state == DP_STATE_WAIT_PRM) {
                new_state = DP_STATE_STOP;
            }
            break;
            
        case 0x01: // Master sends "Clear" command
            if (ctx->current_state == DP_STATE_STOP) {
                new_state = DP_STATE_CLEAR;
                dp_slave_clear_outputs();
            }
            break;
            
        case 0x02: // Master sends "Operate" command
            if (ctx->current_state == DP_STATE_CLEAR ||
                ctx->current_state == DP_STATE_STOP) {
                new_state = DP_STATE_OPERATE;
            }
            break;
            
        case 0x03: // Master sends "Stop" command
            new_state = DP_STATE_STOP;
            dp_slave_set_outputs_safe();
            break;
            
        case 0xFF: // Communication loss
            new_state = DP_STATE_OFFLINE;
            dp_slave_set_outputs_safe();
            break;
    }
    
    if (new_state != ctx->current_state) {
        ctx->previous_state = ctx->current_state;
        ctx->current_state = new_state;
        ctx->state_entry_time = get_system_time_ms();
    }
}

// Main state machine processing
void dp_slave_process(dp_slave_context_t* ctx) {
    switch (ctx->current_state) {
        case DP_STATE_OFFLINE:
            // Wait for connection
            dp_slave_set_outputs_safe();
            break;
            
        case DP_STATE_WAIT_PRM:
            // Wait for parameterization from master
            dp_slave_set_outputs_safe();
            break;
            
        case DP_STATE_STOP:
            // Accept diagnostics and parameterization
            // Outputs remain in safe state
            dp_slave_set_outputs_safe();
            break;
            
        case DP_STATE_CLEAR:
            // Clear all outputs, prepare for operation
            dp_slave_clear_outputs();
            // Auto-transition check could be implemented here
            break;
            
        case DP_STATE_OPERATE:
            // Normal cyclic I/O operation
            dp_slave_update_io();
            
            // Watchdog monitoring
            if (ctx->watchdog_active) {
                uint32_t elapsed = get_system_time_ms() - ctx->state_entry_time;
                if (elapsed > ctx->watchdog_timeout_ms) {
                    // Watchdog timeout - transition to safe state
                    ctx->current_state = DP_STATE_STOP;
                    dp_slave_set_outputs_safe();
                }
            }
            break;
    }
}

// Helper function stubs
void dp_slave_set_outputs_safe(void) {
    // Set all outputs to safe/default values
    // Implementation specific to hardware
}

void dp_slave_clear_outputs(void) {
    // Clear outputs (typically set to 0)
    // Implementation specific to hardware
}

void dp_slave_update_io(void) {
    // Read inputs and write outputs
    // Cyclic data exchange
}

uint32_t get_system_time_ms(void) {
    // Return current system time in milliseconds
    return 0; // Placeholder
}
```

### Rust Implementation

```rust
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum DpSlaveState {
    Offline,
    WaitPrm,
    Stop,
    Clear,
    Operate,
}

#[derive(Debug, Clone, Copy)]
pub enum DpMasterCommand {
    SetPrm,
    Clear,
    Operate,
    Stop,
    CommLoss,
}

pub struct DpSlaveContext {
    current_state: DpSlaveState,
    previous_state: DpSlaveState,
    state_entry_time: Instant,
    watchdog_active: bool,
    watchdog_timeout: Duration,
}

impl DpSlaveContext {
    pub fn new() -> Self {
        Self {
            current_state: DpSlaveState::Offline,
            previous_state: DpSlaveState::Offline,
            state_entry_time: Instant::now(),
            watchdog_active: false,
            watchdog_timeout: Duration::from_millis(1000),
        }
    }

    pub fn current_state(&self) -> DpSlaveState {
        self.current_state
    }

    pub fn handle_command(&mut self, command: DpMasterCommand) {
        let new_state = match (self.current_state, command) {
            // Set_Prm transitions
            (DpSlaveState::Offline, DpMasterCommand::SetPrm) 
            | (DpSlaveState::WaitPrm, DpMasterCommand::SetPrm) => {
                DpSlaveState::Stop
            }

            // Clear command
            (DpSlaveState::Stop, DpMasterCommand::Clear) => {
                Self::clear_outputs();
                DpSlaveState::Clear
            }

            // Operate command
            (DpSlaveState::Clear, DpMasterCommand::Operate)
            | (DpSlaveState::Stop, DpMasterCommand::Operate) => {
                DpSlaveState::Operate
            }

            // Stop command - can happen from any state
            (_, DpMasterCommand::Stop) => {
                Self::set_outputs_safe();
                DpSlaveState::Stop
            }

            // Communication loss - return to offline
            (_, DpMasterCommand::CommLoss) => {
                Self::set_outputs_safe();
                DpSlaveState::Offline
            }

            // No valid transition
            _ => self.current_state,
        };

        if new_state != self.current_state {
            self.transition_to(new_state);
        }
    }

    fn transition_to(&mut self, new_state: DpSlaveState) {
        println!(
            "State transition: {:?} -> {:?}",
            self.current_state, new_state
        );
        self.previous_state = self.current_state;
        self.current_state = new_state;
        self.state_entry_time = Instant::now();
    }

    pub fn process(&mut self) -> Result<(), String> {
        match self.current_state {
            DpSlaveState::Offline => {
                Self::set_outputs_safe();
                // Wait for connection
            }

            DpSlaveState::WaitPrm => {
                Self::set_outputs_safe();
                // Wait for parameterization
            }

            DpSlaveState::Stop => {
                Self::set_outputs_safe();
                // Accept diagnostics and parameters
            }

            DpSlaveState::Clear => {
                Self::clear_outputs();
                // Outputs cleared, ready for operation
            }

            DpSlaveState::Operate => {
                Self::update_io()?;

                // Check watchdog
                if self.watchdog_active {
                    let elapsed = self.state_entry_time.elapsed();
                    if elapsed > self.watchdog_timeout {
                        println!("Watchdog timeout! Transitioning to STOP");
                        self.transition_to(DpSlaveState::Stop);
                        Self::set_outputs_safe();
                    }
                }
            }
        }

        Ok(())
    }

    pub fn enable_watchdog(&mut self, timeout_ms: u64) {
        self.watchdog_active = true;
        self.watchdog_timeout = Duration::from_millis(timeout_ms);
    }

    pub fn refresh_watchdog(&mut self) {
        self.state_entry_time = Instant::now();
    }

    // Hardware interface functions (to be implemented)
    fn set_outputs_safe() {
        println!("Setting outputs to safe state");
        // Implementation specific to hardware
    }

    fn clear_outputs() {
        println!("Clearing all outputs");
        // Set outputs to zero or safe default
    }

    fn update_io() -> Result<(), String> {
        // Cyclic I/O data exchange
        // Read inputs, write outputs
        Ok(())
    }
}

impl Default for DpSlaveContext {
    fn default() -> Self {
        Self::new()
    }
}

// Example usage
fn main() {
    let mut slave = DpSlaveContext::new();
    slave.enable_watchdog(1000);

    // Simulate state transitions
    println!("Initial state: {:?}", slave.current_state());

    slave.handle_command(DpMasterCommand::SetPrm);
    println!("After SetPrm: {:?}", slave.current_state());

    slave.handle_command(DpMasterCommand::Clear);
    println!("After Clear: {:?}", slave.current_state());

    slave.handle_command(DpMasterCommand::Operate);
    println!("After Operate: {:?}", slave.current_state());

    // Process state machine
    for _ in 0..5 {
        if let Err(e) = slave.process() {
            eprintln!("Error processing: {}", e);
        }
        slave.refresh_watchdog();
        std::thread::sleep(Duration::from_millis(100));
    }

    // Simulate stop command
    slave.handle_command(DpMasterCommand::Stop);
    println!("After Stop: {:?}", slave.current_state());
}
```

## Key Implementation Considerations

1. **Watchdog Monitoring**: In Operate state, implement watchdog timers to detect communication failures and automatically transition to a safe state.

2. **Output Safety**: Always ensure outputs are set to safe values when leaving Operate state or during communication failures.

3. **State Persistence**: Some implementations may need to persist state information across power cycles for diagnostic purposes.

4. **Diagnostic Information**: Each state should maintain diagnostic data that can be reported to the master, including state transition history and error conditions.

5. **Thread Safety**: In multi-threaded implementations, ensure state transitions are atomic and properly synchronized.

## Summary

The Profibus DP slave state machine provides a robust framework for managing slave device lifecycle and ensuring safe operation. The five primary states (Offline, Wait-Prm, Stop, Clear, and Operate) represent distinct operational modes with specific behaviors and responsibilities. Proper implementation requires careful attention to state transitions, output safety during non-operational states, and watchdog monitoring during active operation. Understanding this state machine is essential for developing compliant Profibus DP slave devices and for diagnosing communication issues in existing installations. The state machine ensures predictable behavior and provides the master with clear control over slave operations while maintaining system safety through controlled transitions and fail-safe mechanisms.