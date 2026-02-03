# Telegram Types for Drives in Profibus

## Detailed Description

Profibus telegram types for drives are **standardized communication formats** defined in the **PROFIdrive profile** (IEC 61800-7) that specify how controllers communicate with variable speed drives and servo drives. These telegrams define the structure and content of cyclic data exchange between a PLC/controller and drive systems.

### What Are Telegram Types?

Telegram types are **predefined parameter word and process data word arrangements** that determine:
- **Control words (PZD)**: Commands sent from controller to drive
- **Status words (PZD)**: Feedback from drive to controller
- **Setpoint values**: Speed, position, torque references
- **Actual values**: Current speed, position, torque feedback

### Standard Telegram Types (1-7)

**Telegram 1** (Basic Speed Control)
- **Structure**: 2 PZD (Process Data Words)
- **Use**: Simple speed control without monitoring
- **Content**: Control word + speed setpoint → Status word + actual speed

**Telegram 2** (Extended Speed Control)
- **Structure**: 4 PZD
- **Use**: Speed control with additional monitoring
- **Content**: Adds actual current/torque feedback

**Telegram 3** (Speed Control with Encoder)
- **Structure**: 5 PZD
- **Use**: Speed control with position feedback
- **Content**: Includes encoder position value

**Telegram 4** (Positioning with Travel Block)
- **Structure**: 6 PZD
- **Use**: Simple positioning tasks
- **Content**: Position setpoint and actual position

**Telegram 5** (Extended Positioning)
- **Structure**: 9 PZD
- **Use**: Advanced positioning with velocity control
- **Content**: Position, speed, and override parameters

**Telegram 6** (Positioning with Encoder)
- **Structure**: 7 PZD
- **Use**: Positioning with separate encoder feedback

**Telegram 7** (Extended Positioning with Encoder)
- **Structure**: 10 PZD
- **Use**: Most comprehensive positioning control

---

## C/C++ Programming Examples

### Telegram 1 Implementation (Basic Speed Control)

```c
#include <stdint.h>
#include <stdbool.h>

// Profibus Telegram 1 Structure
typedef struct {
    uint16_t control_word;      // STW1 (Steuerwort 1)
    int16_t  speed_setpoint;    // NSOLL-A (Speed setpoint)
} Telegram1_Output;

typedef struct {
    uint16_t status_word;       // ZSW1 (Zustandswort 1)
    int16_t  speed_actual;      // NIST-A (Actual speed)
} Telegram1_Input;

// Control Word (STW1) Bit Definitions
#define STW1_ON1                (1 << 0)   // ON command
#define STW1_NO_OFF2            (1 << 1)   // No OFF2 (coast stop)
#define STW1_NO_OFF3            (1 << 2)   // No OFF3 (quick stop)
#define STW1_ENABLE_OPERATION   (1 << 3)   // Enable operation
#define STW1_RAMP_FUNCTION_GEN  (1 << 4)   // Enable ramp function generator
#define STW1_UNFREEZE_RAMP      (1 << 5)   // Continue ramp function generator
#define STW1_ENABLE_SETPOINT    (1 << 6)   // Enable speed setpoint
#define STW1_FAULT_ACK          (1 << 7)   // Acknowledge fault

// Status Word (ZSW1) Bit Definitions
#define ZSW1_READY_TO_SWITCH_ON (1 << 0)   // Ready to switch on
#define ZSW1_READY_TO_OPERATE   (1 << 1)   // Ready to operate
#define ZSW1_OPERATION_ENABLED  (1 << 2)   // Operation enabled
#define ZSW1_FAULT              (1 << 3)   // Fault condition
#define ZSW1_NO_OFF2            (1 << 4)   // OFF2 not active
#define ZSW1_NO_OFF3            (1 << 5)   // OFF3 not active
#define ZSW1_SWITCH_ON_INHIBIT  (1 << 6)   // Switch on inhibited
#define ZSW1_WARNING            (1 << 7)   // Warning active

// Drive Control Class
class ProfibusDriver {
private:
    Telegram1_Output output_data;
    Telegram1_Input input_data;
    
public:
    ProfibusDriver() {
        output_data.control_word = 0;
        output_data.speed_setpoint = 0;
    }
    
    // Enable the drive
    void enableDrive() {
        output_data.control_word = 
            STW1_ON1 | 
            STW1_NO_OFF2 | 
            STW1_NO_OFF3 | 
            STW1_ENABLE_OPERATION |
            STW1_RAMP_FUNCTION_GEN |
            STW1_UNFREEZE_RAMP |
            STW1_ENABLE_SETPOINT;
    }
    
    // Set speed in RPM (scaled to 0x4000 = 100%)
    void setSpeed(float rpm_percentage) {
        // 0x4000 (16384) = 100% of rated speed
        int16_t scaled_speed = (int16_t)(rpm_percentage / 100.0 * 0x4000);
        output_data.speed_setpoint = scaled_speed;
    }
    
    // Acknowledge fault
    void acknowledgeFault() {
        output_data.control_word |= STW1_FAULT_ACK;
    }
    
    // Check if drive is ready
    bool isReadyToOperate() {
        return (input_data.status_word & ZSW1_READY_TO_OPERATE) != 0;
    }
    
    // Check for fault
    bool hasFault() {
        return (input_data.status_word & ZSW1_FAULT) != 0;
    }
    
    // Get actual speed
    float getActualSpeed() {
        // Convert from 0x4000 scale to percentage
        return (input_data.speed_actual / 16384.0) * 100.0;
    }
    
    // Send/Receive cyclic data (called by Profibus stack)
    void cyclicExchange(uint8_t* tx_buffer, uint8_t* rx_buffer) {
        // Send output data (controller to drive)
        memcpy(tx_buffer, &output_data, sizeof(Telegram1_Output));
        
        // Receive input data (drive to controller)
        memcpy(&input_data, rx_buffer, sizeof(Telegram1_Input));
    }
};
```

### Telegram 5 Implementation (Extended Positioning)

```c
// Telegram 5 Structure (9 PZD)
typedef struct {
    uint16_t control_word_1;         // STW1
    uint16_t control_word_2;         // STW2
    int32_t  position_setpoint;      // MDI position
    int16_t  override;               // Speed override
    int16_t  reserved;               // Reserved
} Telegram5_Output;

typedef struct {
    uint16_t status_word_1;          // ZSW1
    uint16_t status_word_2;          // ZSW2
    int32_t  position_actual_1;      // XIST1 (actual position)
    int16_t  reserved1;              // Reserved
    int16_t  reserved2;              // Reserved
} Telegram5_Input;

// Control Word 2 (STW2) Bit Definitions
#define STW2_TRAVERSE_BLOCK_POSITIVE (1 << 0)
#define STW2_TRAVERSE_BLOCK_NEGATIVE (1 << 1)
#define STW2_REFERENCE_POINT_SET     (1 << 4)
#define STW2_START_MDI              (1 << 12)

class PositioningDrive {
private:
    Telegram5_Output output;
    Telegram5_Input input;
    
public:
    PositioningDrive() {
        memset(&output, 0, sizeof(output));
    }
    
    void moveToPosition(int32_t target_position, uint8_t speed_override) {
        // Enable positioning
        output.control_word_1 = 
            STW1_ON1 | STW1_NO_OFF2 | STW1_NO_OFF3 | 
            STW1_ENABLE_OPERATION | STW1_ENABLE_SETPOINT;
        
        // Start MDI (Manual Data Input) positioning
        output.control_word_2 = STW2_START_MDI;
        
        // Set target position (in encoder increments)
        output.position_setpoint = target_position;
        
        // Set speed override (0x4000 = 100%)
        output.override = (speed_override * 0x4000) / 100;
    }
    
    int32_t getCurrentPosition() {
        return input.position_actual_1;
    }
    
    bool isPositionReached() {
        // Check if target position reached (ZSW2 bit 10)
        return (input.status_word_2 & (1 << 10)) != 0;
    }
};
```

---

## Rust Programming Examples

### Telegram 1 in Rust (Safe Implementation)

```rust
use std::mem;

// Control word bit flags
#[repr(u16)]
#[derive(Debug, Clone, Copy)]
pub enum ControlWordBits {
    On1 = 1 << 0,
    NoOff2 = 1 << 1,
    NoOff3 = 1 << 2,
    EnableOperation = 1 << 3,
    RampFunctionGen = 1 << 4,
    UnfreezeRamp = 1 << 5,
    EnableSetpoint = 1 << 6,
    FaultAck = 1 << 7,
}

// Status word bit flags
#[repr(u16)]
#[derive(Debug, Clone, Copy)]
pub enum StatusWordBits {
    ReadyToSwitchOn = 1 << 0,
    ReadyToOperate = 1 << 1,
    OperationEnabled = 1 << 2,
    Fault = 1 << 3,
    NoOff2 = 1 << 4,
    NoOff3 = 1 << 5,
    SwitchOnInhibit = 1 << 6,
    Warning = 1 << 7,
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

pub struct ProfibusDriver {
    output: Telegram1Output,
    input: Telegram1Input,
}

impl ProfibusDriver {
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
        }
    }
    
    /// Enable the drive with standard control sequence
    pub fn enable_drive(&mut self) {
        self.output.control_word = 
            ControlWordBits::On1 as u16 |
            ControlWordBits::NoOff2 as u16 |
            ControlWordBits::NoOff3 as u16 |
            ControlWordBits::EnableOperation as u16 |
            ControlWordBits::RampFunctionGen as u16 |
            ControlWordBits::UnfreezeRamp as u16 |
            ControlWordBits::EnableSetpoint as u16;
    }
    
    /// Disable the drive
    pub fn disable_drive(&mut self) {
        self.output.control_word = 0;
    }
    
    /// Set speed as percentage (-100.0 to 100.0)
    pub fn set_speed(&mut self, percentage: f32) {
        // Clamp to valid range
        let clamped = percentage.clamp(-100.0, 100.0);
        // Scale: 0x4000 (16384) = 100%
        self.output.speed_setpoint = (clamped / 100.0 * 16384.0) as i16;
    }
    
    /// Acknowledge any active fault
    pub fn acknowledge_fault(&mut self) {
        self.output.control_word |= ControlWordBits::FaultAck as u16;
    }
    
    /// Check if drive is ready to operate
    pub fn is_ready_to_operate(&self) -> bool {
        (self.input.status_word & StatusWordBits::ReadyToOperate as u16) != 0
    }
    
    /// Check if drive has a fault
    pub fn has_fault(&self) -> bool {
        (self.input.status_word & StatusWordBits::Fault as u16) != 0
    }
    
    /// Get actual speed as percentage
    pub fn get_actual_speed(&self) -> f32 {
        (self.input.speed_actual as f32 / 16384.0) * 100.0
    }
    
    /// Perform cyclic data exchange
    pub fn cyclic_exchange(&mut self, rx_buffer: &[u8]) -> Vec<u8> {
        // Parse received data
        if rx_buffer.len() >= mem::size_of::<Telegram1Input>() {
            unsafe {
                self.input = std::ptr::read_unaligned(
                    rx_buffer.as_ptr() as *const Telegram1Input
                );
            }
        }
        
        // Prepare transmit data
        let tx_data = unsafe {
            std::slice::from_raw_parts(
                &self.output as *const Telegram1Output as *const u8,
                mem::size_of::<Telegram1Output>()
            )
        };
        
        tx_data.to_vec()
    }
}
```

### Telegram 5 in Rust (Positioning Control)

```rust
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct Telegram5Output {
    pub control_word_1: u16,
    pub control_word_2: u16,
    pub position_setpoint: i32,
    pub override_value: i16,
    pub reserved: i16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct Telegram5Input {
    pub status_word_1: u16,
    pub status_word_2: u16,
    pub position_actual: i32,
    pub reserved1: i16,
    pub reserved2: i16,
}

pub struct PositioningDrive {
    output: Telegram5Output,
    input: Telegram5Input,
}

impl PositioningDrive {
    pub fn new() -> Self {
        Self {
            output: Telegram5Output {
                control_word_1: 0,
                control_word_2: 0,
                position_setpoint: 0,
                override_value: 0,
                reserved: 0,
            },
            input: Telegram5Input {
                status_word_1: 0,
                status_word_2: 0,
                position_actual: 0,
                reserved1: 0,
                reserved2: 0,
            },
        }
    }
    
    /// Start absolute positioning move
    pub fn move_to_position(&mut self, target: i32, speed_percent: u8) {
        // Enable drive
        self.output.control_word_1 = 0x047F; // Standard enable bits
        
        // Start MDI positioning
        self.output.control_word_2 = 1 << 12; // STW2_START_MDI
        
        // Set target position
        self.output.position_setpoint = target;
        
        // Set speed override
        let speed = speed_percent.min(100) as i16;
        self.output.override_value = (speed * 0x4000) / 100;
    }
    
    /// Get current position
    pub fn get_position(&self) -> i32 {
        self.input.position_actual
    }
    
    /// Check if position has been reached
    pub fn is_position_reached(&self) -> bool {
        (self.input.status_word_2 & (1 << 10)) != 0
    }
    
    /// Check if drive is moving
    pub fn is_moving(&self) -> bool {
        (self.input.status_word_2 & (1 << 13)) != 0
    }
}

// Usage example
fn main() {
    let mut drive = PositioningDrive::new();
    
    // Move to position 10000 at 75% speed
    drive.move_to_position(10000, 75);
    
    // In cyclic task, check status
    loop {
        // Exchange data with drive...
        
        if drive.is_position_reached() {
            println!("Position reached: {}", drive.get_position());
            break;
        }
        
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
}
```

---

## Summary

**Profibus Telegram Types for Drives** are standardized data exchange formats that enable interoperable communication between controllers and drives. Key points:

- **Telegram 1-2**: Basic/extended speed control with minimal process data
- **Telegram 3**: Speed control with position feedback (encoder)
- **Telegram 4-7**: Positioning control with varying complexity levels
- **PZD Structure**: Process Data Words containing control/status information
- **Scaling**: Speed/torque typically use 0x4000 (16384) as 100% reference
- **Control Sequence**: ON1 → NO_OFF2/3 → ENABLE_OPERATION → ENABLE_SETPOINT
- **Implementation**: Requires bit-level manipulation of control/status words and proper data structure packing

The telegram type selection depends on the application: simple variable speed drives use Telegram 1-3, while servo positioning requires Telegram 4-7. Proper implementation requires understanding the PROFIdrive profile state machine and cyclic data exchange timing.