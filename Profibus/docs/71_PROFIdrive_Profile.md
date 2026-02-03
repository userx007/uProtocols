# PROFIdrive Profile: Detailed Description

## Overview

**PROFIdrive** is a standardized application profile within the PROFIBUS and PROFINET ecosystem specifically designed for drive technology and motion control applications. It provides a vendor-independent interface for controlling drives (motors, frequency converters, servo drives) and enables interoperability between automation systems and drive equipment from different manufacturers.

## Core Concepts

### 1. **Profile Classes**

PROFIdrive defines several profile classes based on application complexity:

- **Class 1**: Standard drives (basic speed/torque control)
- **Class 2**: Standard drives with technological functions
- **Class 3**: Positioning drives (point-to-point motion)
- **Class 4**: Motion control drives (coordinated multi-axis motion)
- **Class 5**: Central motion control (synchronized motion across multiple drives)

### 2. **Telegram Structure**

PROFIdrive uses standardized telegram formats for cyclic data exchange:

- **Process Data Objects (PDOs)**: Cyclic real-time data (control words, status words, setpoints, actual values)
- **Parameter Data Objects**: Acyclic parameter access
- **Standard Telegram Types**: Predefined telegram structures (e.g., Telegram 1, 9, 110, etc.)

### 3. **Control and Status Words**

- **Control Word (STW)**: Commands from controller to drive (start, stop, enable, fault reset)
- **Status Word (ZSW)**: Drive state feedback (ready, enabled, fault, warning)

### 4. **State Machine**

PROFIdrive implements a standardized state machine with states like:
- Switch-on disabled
- Ready to switch on
- Switched on
- Operation enabled
- Fault
- Quick stop active

---

## Programming Examples

### C/C++ Implementation

```cpp
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// PROFIdrive Control Word (STW1) bit definitions
#define STW1_ON                 (1 << 0)   // Switch on
#define STW1_NO_OFF2            (1 << 1)   // No coast stop
#define STW1_NO_OFF3            (1 << 2)   // No quick stop
#define STW1_ENABLE_OPERATION   (1 << 3)   // Enable operation
#define STW1_FAULT_RESET        (1 << 7)   // Fault acknowledgment
#define STW1_JOG1               (1 << 8)   // Jog bit 1
#define STW1_JOG2               (1 << 9)   // Jog bit 2
#define STW1_CONTROL_BY_PLC     (1 << 10)  // PLC control

// PROFIdrive Status Word (ZSW1) bit definitions
#define ZSW1_READY_TO_SWITCH_ON (1 << 0)   // Ready to switch on
#define ZSW1_READY              (1 << 1)   // Ready
#define ZSW1_OPERATION_ENABLED  (1 << 2)   // Operation enabled
#define ZSW1_FAULT              (1 << 3)   // Fault active
#define ZSW1_OFF2_ACTIVE        (1 << 4)   // Coast stop active
#define ZSW1_OFF3_ACTIVE        (1 << 5)   // Quick stop active
#define ZSW1_SWITCH_ON_INHIBIT  (1 << 6)   // Switch-on inhibit
#define ZSW1_WARNING            (1 << 7)   // Warning
#define ZSW1_CONTROL_REQUESTED  (1 << 10)  // Control requested

// Drive states
typedef enum {
    DRIVE_STATE_NOT_READY = 0,
    DRIVE_STATE_SWITCH_ON_DISABLED,
    DRIVE_STATE_READY_TO_SWITCH_ON,
    DRIVE_STATE_SWITCHED_ON,
    DRIVE_STATE_OPERATION_ENABLED,
    DRIVE_STATE_QUICK_STOP_ACTIVE,
    DRIVE_STATE_FAULT_REACTION_ACTIVE,
    DRIVE_STATE_FAULT
} DriveState;

// Standard Telegram 1 structure (most common)
typedef struct {
    uint16_t control_word;      // STW1
    int16_t  speed_setpoint;    // NSOLL_A (speed setpoint in RPM or %)
} __attribute__((packed)) PROFIdriveTelegram1_Out;

typedef struct {
    uint16_t status_word;       // ZSW1
    int16_t  speed_actual;      // NIST_A (actual speed)
} __attribute__((packed)) PROFIdriveTelegram1_In;

// PROFIdrive drive controller class
class PROFIDriveController {
private:
    PROFIdriveTelegram1_Out output_data;
    PROFIdriveTelegram1_In input_data;
    DriveState current_state;
    
public:
    PROFIDriveController() {
        memset(&output_data, 0, sizeof(output_data));
        memset(&input_data, 0, sizeof(input_data));
        current_state = DRIVE_STATE_NOT_READY;
    }
    
    // Decode drive state from status word
    DriveState getDriveState(uint16_t status_word) {
        // State machine logic based on ZSW1 bits
        if (status_word & ZSW1_FAULT) {
            return DRIVE_STATE_FAULT;
        }
        if (!(status_word & ZSW1_READY_TO_SWITCH_ON)) {
            if (status_word & ZSW1_SWITCH_ON_INHIBIT) {
                return DRIVE_STATE_SWITCH_ON_DISABLED;
            }
            return DRIVE_STATE_NOT_READY;
        }
        if (status_word & ZSW1_OPERATION_ENABLED) {
            return DRIVE_STATE_OPERATION_ENABLED;
        }
        if (status_word & ZSW1_READY) {
            return DRIVE_STATE_SWITCHED_ON;
        }
        if (status_word & ZSW1_OFF3_ACTIVE) {
            return DRIVE_STATE_QUICK_STOP_ACTIVE;
        }
        return DRIVE_STATE_READY_TO_SWITCH_ON;
    }
    
    // Enable drive operation
    bool enableDrive() {
        output_data.control_word = STW1_ON | 
                                   STW1_NO_OFF2 | 
                                   STW1_NO_OFF3 | 
                                   STW1_ENABLE_OPERATION |
                                   STW1_CONTROL_BY_PLC;
        return true;
    }
    
    // Disable drive
    void disableDrive() {
        output_data.control_word = 0;
    }
    
    // Reset fault
    void resetFault() {
        output_data.control_word |= STW1_FAULT_RESET;
    }
    
    // Set speed setpoint (in 0.01% of rated speed)
    void setSpeedSetpoint(int16_t speed) {
        output_data.speed_setpoint = speed;
    }
    
    // Get actual speed
    int16_t getActualSpeed() {
        return input_data.speed_actual;
    }
    
    // Process cyclic data exchange
    void cyclicUpdate(const uint8_t* input_buffer, uint8_t* output_buffer) {
        // Read input data from drive
        memcpy(&input_data, input_buffer, sizeof(input_data));
        
        // Update current state
        current_state = getDriveState(input_data.status_word);
        
        // Write output data to drive
        memcpy(output_buffer, &output_data, sizeof(output_data));
    }
    
    // Check if drive is ready for operation
    bool isOperational() {
        return current_state == DRIVE_STATE_OPERATION_ENABLED;
    }
    
    // Check if fault is active
    bool hasFault() {
        return current_state == DRIVE_STATE_FAULT;
    }
    
    // Get status word
    uint16_t getStatusWord() {
        return input_data.status_word;
    }
};

// Example usage
void example_profidrive_control() {
    PROFIDriveController drive;
    uint8_t input_buffer[4];
    uint8_t output_buffer[4];
    
    // Simulation loop
    for (int cycle = 0; cycle < 100; cycle++) {
        // Simulate receiving data from drive
        // (In real application, this would come from PROFIBUS/PROFINET)
        
        // Process cyclic data
        drive.cyclicUpdate(input_buffer, output_buffer);
        
        // State-based control logic
        if (drive.hasFault()) {
            printf("Drive fault detected! Resetting...\n");
            drive.resetFault();
        } else if (!drive.isOperational()) {
            printf("Enabling drive...\n");
            drive.enableDrive();
        } else {
            // Drive is operational, set speed
            int16_t target_speed = 3000; // 30.00% of rated speed
            drive.setSpeedSetpoint(target_speed);
            printf("Speed setpoint: %d, Actual: %d\n", 
                   target_speed, drive.getActualSpeed());
        }
        
        // Cyclic interval (e.g., 10ms)
        // usleep(10000);
    }
}
```

### Advanced C++ with Telegram 111 (Positioning)

```cpp
#include <vector>
#include <memory>

// Telegram 111 for positioning drives (Class 3)
typedef struct {
    uint16_t control_word;           // STW1
    uint16_t control_word_profidrive; // STW2 (PROFIdrive specific)
    int32_t  position_setpoint;      // Target position
    int32_t  velocity_setpoint;      // Target velocity
} __attribute__((packed)) PROFIdriveTelegram111_Out;

typedef struct {
    uint16_t status_word;            // ZSW1
    uint16_t status_word_profidrive; // ZSW2
    int32_t  position_actual;        // Actual position
    int32_t  velocity_actual;        // Actual velocity
} __attribute__((packed)) PROFIdriveTelegram111_In;

class PositioningDrive {
private:
    PROFIdriveTelegram111_Out tx_data;
    PROFIdriveTelegram111_In rx_data;
    
    // Motion profile parameters
    int32_t max_velocity;
    int32_t acceleration;
    int32_t deceleration;
    
public:
    PositioningDrive(int32_t max_vel = 1000000, 
                     int32_t accel = 100000, 
                     int32_t decel = 100000) 
        : max_velocity(max_vel), acceleration(accel), deceleration(decel) {
        memset(&tx_data, 0, sizeof(tx_data));
        memset(&rx_data, 0, sizeof(rx_data));
    }
    
    // Absolute positioning
    void moveAbsolute(int32_t target_position) {
        tx_data.position_setpoint = target_position;
        tx_data.velocity_setpoint = max_velocity;
        // Set task bits in STW2 for absolute positioning
        tx_data.control_word_profidrive |= (1 << 6); // Start task
    }
    
    // Relative positioning
    void moveRelative(int32_t distance) {
        int32_t current_pos = rx_data.position_actual;
        moveAbsolute(current_pos + distance);
    }
    
    // Velocity mode
    void setVelocity(int32_t velocity) {
        tx_data.velocity_setpoint = velocity;
        // Switch to velocity mode
        tx_data.control_word_profidrive &= ~(1 << 7); // Clear positioning bit
    }
    
    // Check if positioning is complete
    bool isPositionReached() {
        return (rx_data.status_word_profidrive & (1 << 10)) != 0;
    }
    
    int32_t getActualPosition() {
        return rx_data.position_actual;
    }
    
    int32_t getActualVelocity() {
        return rx_data.velocity_actual;
    }
};
```

---

### Rust Implementation

```rust
use std::mem;

// PROFIdrive Control Word bits
const STW1_ON: u16 = 1 << 0;
const STW1_NO_OFF2: u16 = 1 << 1;
const STW1_NO_OFF3: u16 = 1 << 2;
const STW1_ENABLE_OPERATION: u16 = 1 << 3;
const STW1_FAULT_RESET: u16 = 1 << 7;
const STW1_CONTROL_BY_PLC: u16 = 1 << 10;

// PROFIdrive Status Word bits
const ZSW1_READY_TO_SWITCH_ON: u16 = 1 << 0;
const ZSW1_READY: u16 = 1 << 1;
const ZSW1_OPERATION_ENABLED: u16 = 1 << 2;
const ZSW1_FAULT: u16 = 1 << 3;
const ZSW1_OFF3_ACTIVE: u16 = 1 << 5;
const ZSW1_SWITCH_ON_INHIBIT: u16 = 1 << 6;

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum DriveState {
    NotReady = 0,
    SwitchOnDisabled,
    ReadyToSwitchOn,
    SwitchedOn,
    OperationEnabled,
    QuickStopActive,
    FaultReactionActive,
    Fault,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct Telegram1Output {
    pub control_word: u16,
    pub speed_setpoint: i16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct Telegram1Input {
    pub status_word: u16,
    pub speed_actual: i16,
}

pub struct PROFIDriveController {
    output: Telegram1Output,
    input: Telegram1Input,
    state: DriveState,
}

impl PROFIDriveController {
    pub fn new() -> Self {
        Self {
            output: Telegram1Output {
                control_word: 0,
                speed_setpoint: 0,
            },
            input: Telegram1Input {
                status_word: 0,
                speed_actual: 0,
            },
            state: DriveState::NotReady,
        }
    }

    /// Decode drive state from status word
    pub fn decode_state(status_word: u16) -> DriveState {
        if status_word & ZSW1_FAULT != 0 {
            return DriveState::Fault;
        }
        if status_word & ZSW1_READY_TO_SWITCH_ON == 0 {
            if status_word & ZSW1_SWITCH_ON_INHIBIT != 0 {
                return DriveState::SwitchOnDisabled;
            }
            return DriveState::NotReady;
        }
        if status_word & ZSW1_OPERATION_ENABLED != 0 {
            return DriveState::OperationEnabled;
        }
        if status_word & ZSW1_READY != 0 {
            return DriveState::SwitchedOn;
        }
        if status_word & ZSW1_OFF3_ACTIVE != 0 {
            return DriveState::QuickStopActive;
        }
        DriveState::ReadyToSwitchOn
    }

    /// Enable drive operation
    pub fn enable(&mut self) {
        self.output.control_word = STW1_ON
            | STW1_NO_OFF2
            | STW1_NO_OFF3
            | STW1_ENABLE_OPERATION
            | STW1_CONTROL_BY_PLC;
    }

    /// Disable drive
    pub fn disable(&mut self) {
        self.output.control_word = 0;
    }

    /// Reset fault condition
    pub fn reset_fault(&mut self) {
        self.output.control_word |= STW1_FAULT_RESET;
    }

    /// Set speed setpoint (in 0.01% of rated speed)
    pub fn set_speed(&mut self, speed: i16) {
        self.output.speed_setpoint = speed;
    }

    /// Get actual speed
    pub fn get_actual_speed(&self) -> i16 {
        self.input.speed_actual
    }

    /// Process cyclic data exchange
    pub fn cyclic_update(&mut self, input_buffer: &[u8], output_buffer: &mut [u8]) {
        // Read input data from drive
        if input_buffer.len() >= mem::size_of::<Telegram1Input>() {
            unsafe {
                self.input = *(input_buffer.as_ptr() as *const Telegram1Input);
            }
        }

        // Update current state
        self.state = Self::decode_state(self.input.status_word);

        // Write output data to drive
        if output_buffer.len() >= mem::size_of::<Telegram1Output>() {
            unsafe {
                let ptr = output_buffer.as_mut_ptr() as *mut Telegram1Output;
                *ptr = self.output;
            }
        }
    }

    /// Check if drive is operational
    pub fn is_operational(&self) -> bool {
        self.state == DriveState::OperationEnabled
    }

    /// Check if fault is active
    pub fn has_fault(&self) -> bool {
        self.state == DriveState::Fault
    }

    pub fn get_state(&self) -> DriveState {
        self.state
    }

    pub fn get_status_word(&self) -> u16 {
        self.input.status_word
    }
}

// Positioning drive with Telegram 111
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct Telegram111Output {
    pub control_word: u16,
    pub control_word_profidrive: u16,
    pub position_setpoint: i32,
    pub velocity_setpoint: i32,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct Telegram111Input {
    pub status_word: u16,
    pub status_word_profidrive: u16,
    pub position_actual: i32,
    pub velocity_actual: i32,
}

pub struct PositioningDrive {
    output: Telegram111Output,
    input: Telegram111Input,
    max_velocity: i32,
}

impl PositioningDrive {
    pub fn new(max_velocity: i32) -> Self {
        Self {
            output: Telegram111Output {
                control_word: 0,
                control_word_profidrive: 0,
                position_setpoint: 0,
                velocity_setpoint: 0,
            },
            input: Telegram111Input {
                status_word: 0,
                status_word_profidrive: 0,
                position_actual: 0,
                velocity_actual: 0,
            },
            max_velocity,
        }
    }

    /// Move to absolute position
    pub fn move_absolute(&mut self, target_position: i32) {
        self.output.position_setpoint = target_position;
        self.output.velocity_setpoint = self.max_velocity;
        // Set task start bit
        self.output.control_word_profidrive |= 1 << 6;
    }

    /// Move relative distance
    pub fn move_relative(&mut self, distance: i32) {
        let current_pos = self.input.position_actual;
        self.move_absolute(current_pos + distance);
    }

    /// Check if target position is reached
    pub fn is_position_reached(&self) -> bool {
        (self.input.status_word_profidrive & (1 << 10)) != 0
    }

    pub fn get_actual_position(&self) -> i32 {
        self.input.position_actual
    }

    pub fn get_actual_velocity(&self) -> i32 {
        self.input.velocity_actual
    }
}

// Example usage
fn example_usage() {
    let mut drive = PROFIDriveController::new();
    let mut input_buffer = vec![0u8; 4];
    let mut output_buffer = vec![0u8; 4];

    // Cyclic control loop
    for _cycle in 0..100 {
        drive.cyclic_update(&input_buffer, &mut output_buffer);

        match drive.get_state() {
            DriveState::Fault => {
                println!("Fault detected! Resetting...");
                drive.reset_fault();
            }
            DriveState::OperationEnabled => {
                // Set speed to 50% (5000 in 0.01% units)
                drive.set_speed(5000);
                println!(
                    "Actual speed: {} (0.01%)",
                    drive.get_actual_speed()
                );
            }
            _ => {
                println!("Enabling drive...");
                drive.enable();
            }
        }

        // Simulate cyclic interval
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
}
```

---

## Summary

**PROFIdrive** is a comprehensive, vendor-neutral application profile that standardizes drive control in industrial automation. Key takeaways:

### **Benefits:**
- **Interoperability**: Mix drives from different manufacturers with consistent programming
- **Scalability**: From simple speed control to complex multi-axis motion coordination
- **Standardization**: Predefined telegrams reduce engineering effort
- **State machine**: Clear, predictable drive behavior
- **Diagnostic capabilities**: Standardized fault handling and status reporting

### **Technical Highlights:**
- **Control/Status Words**: Bitwise encoded commands and feedback
- **Telegram Types**: Optimized data structures for different applications (Telegram 1 for simple drives, 111 for positioning, etc.)
- **Profile Classes**: Tiered functionality (Class 1-5) for various complexity levels
- **Cyclic Communication**: Real-time control with deterministic timing
- **Parameter Access**: Acyclic read/write for configuration

### **Common Use Cases:**
- Variable frequency drives (VFDs) for pump/fan control
- Servo drives for precision positioning
- Coordinated multi-axis motion (robotics, packaging)
- Centralized motion control systems
- Process automation with speed-controlled motors

The code examples demonstrate basic speed control, state machine handling, fault management, and positioning functionality, providing a foundation for implementing PROFIdrive communication in both embedded systems (C/C++) and modern applications (Rust).