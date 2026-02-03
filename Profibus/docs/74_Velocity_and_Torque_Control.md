# Velocity and Torque Control in Profibus

## Overview

Velocity and torque control in Profibus networks is primarily implemented through the **PROFIdrive** profile, which is a standardized application profile for drives and motion control. PROFIdrive defines a consistent interface for controlling motors, servo drives, and frequency converters, enabling interoperable communication between controllers and drive systems from different manufacturers.

## Core Concepts

### PROFIdrive Profile

PROFIdrive standardizes communication for drive technology across multiple levels:

- **Telegram types**: Predefined cyclic data structures for different control modes
- **Parameter objects**: Standardized parameter numbers for configuration and monitoring
- **Control words and status words**: Bitfields for command/status exchange
- **Setpoint and actual values**: Speed, torque, position references and feedbacks

### Control Modes

1. **Velocity Control (Speed Control)**
   - Maintains constant motor speed regardless of load variations
   - Uses PROFIdrive telegram types 1, 2, or 3
   - Primary setpoint: speed reference (n_set)
   - Secondary constraints: torque limits

2. **Torque Control**
   - Directly controls motor torque output
   - Useful for tension control, force control applications
   - Uses torque setpoint (T_set)
   - Speed limits can be applied as constraints

### Key Parameters

PROFIdrive defines standardized parameter numbers (PNU):

- **P930-P969**: Speed-related parameters
- **P1000-P1099**: Torque-related parameters
- **P2000**: Control word
- **P2001**: Status word
- **Control Word bits**: Enable, coast stop, quick stop, operation mode selection
- **Status Word bits**: Ready, operation enabled, fault, warning

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>

// PROFIdrive Telegram 1 structure (basic speed control)
typedef struct {
    uint16_t control_word;      // STW1 (Control Word 1)
    int16_t  speed_setpoint;    // NSOLL_A (Speed setpoint)
} __attribute__((packed)) ProfiDrive_Telegram1_Write;

typedef struct {
    uint16_t status_word;       // ZSW1 (Status Word 1)
    int16_t  speed_actual;      // NIST_A (Actual speed)
} __attribute__((packed)) ProfiDrive_Telegram1_Read;

// PROFIdrive Telegram 3 structure (speed + torque control)
typedef struct {
    uint16_t control_word;      // STW1
    int16_t  speed_setpoint;    // NSOLL_A
    int16_t  torque_limit_pos;  // MPOS (Positive torque limit)
    int16_t  torque_limit_neg;  // MNEG (Negative torque limit)
} __attribute__((packed)) ProfiDrive_Telegram3_Write;

typedef struct {
    uint16_t status_word;       // ZSW1
    int16_t  speed_actual;      // NIST_A
    int16_t  torque_actual;     // MIST (Actual torque)
} __attribute__((packed)) ProfiDrive_Telegram3_Read;

// Control Word bit definitions
#define STW1_ON                 (1 << 0)   // Switch on
#define STW1_NO_OFF2            (1 << 1)   // No coast stop
#define STW1_NO_OFF3            (1 << 2)   // No quick stop
#define STW1_ENABLE_OPERATION   (1 << 3)   // Enable operation
#define STW1_FAULT_RESET        (1 << 7)   // Fault acknowledge

// Status Word bit definitions
#define ZSW1_READY_TO_SWITCH_ON (1 << 0)   // Ready to switch on
#define ZSW1_READY_TO_OPERATE   (1 << 1)   // Ready to operate
#define ZSW1_OPERATION_ENABLED  (1 << 2)   // Operation enabled
#define ZSW1_FAULT              (1 << 3)   // Fault present
#define ZSW1_NO_OFF2            (1 << 4)   // No coast stop active
#define ZSW1_NO_OFF3            (1 << 5)   // No quick stop active
#define ZSW1_SWITCH_ON_LOCKED   (1 << 6)   // Switch on inhibited

// Convert engineering units (RPM) to PROFIdrive normalized values
int16_t rpm_to_profidrive(float rpm, float max_rpm) {
    // PROFIdrive typically uses 16384 = 100% (0x4000)
    float normalized = (rpm / max_rpm) * 16384.0f;
    if (normalized > 32767.0f) normalized = 32767.0f;
    if (normalized < -32768.0f) normalized = -32768.0f;
    return (int16_t)normalized;
}

float profidrive_to_rpm(int16_t value, float max_rpm) {
    return ((float)value / 16384.0f) * max_rpm;
}

// Convert torque percentage to PROFIdrive values
int16_t torque_percent_to_profidrive(float torque_percent) {
    // 16384 = 100%
    float normalized = (torque_percent / 100.0f) * 16384.0f;
    if (normalized > 32767.0f) normalized = 32767.0f;
    if (normalized < -32768.0f) normalized = -32768.0f;
    return (int16_t)normalized;
}

// Drive state machine handler
typedef enum {
    DRIVE_STATE_NOT_READY,
    DRIVE_STATE_SWITCH_ON_DISABLED,
    DRIVE_STATE_READY_TO_SWITCH_ON,
    DRIVE_STATE_SWITCHED_ON,
    DRIVE_STATE_OPERATION_ENABLED,
    DRIVE_STATE_FAULT
} DriveState;

DriveState get_drive_state(uint16_t status_word) {
    if (status_word & ZSW1_FAULT) {
        return DRIVE_STATE_FAULT;
    }
    if ((status_word & 0x6F) == 0x00) {
        return DRIVE_STATE_NOT_READY;
    }
    if ((status_word & 0x4F) == 0x40) {
        return DRIVE_STATE_SWITCH_ON_DISABLED;
    }
    if ((status_word & 0x6F) == 0x21) {
        return DRIVE_STATE_READY_TO_SWITCH_ON;
    }
    if ((status_word & 0x6F) == 0x23) {
        return DRIVE_STATE_SWITCHED_ON;
    }
    if ((status_word & 0x6F) == 0x27) {
        return DRIVE_STATE_OPERATION_ENABLED;
    }
    return DRIVE_STATE_NOT_READY;
}

// Example: Enable drive and set speed
bool enable_drive_and_set_speed(ProfiDrive_Telegram1_Write* output,
                                 const ProfiDrive_Telegram1_Read* input,
                                 float target_rpm, float max_rpm) {
    DriveState state = get_drive_state(input->status_word);
    
    switch (state) {
        case DRIVE_STATE_FAULT:
            // Reset fault
            output->control_word = STW1_FAULT_RESET;
            output->speed_setpoint = 0;
            return false;
            
        case DRIVE_STATE_SWITCH_ON_DISABLED:
        case DRIVE_STATE_NOT_READY:
            // Prepare to switch on
            output->control_word = 0;
            output->speed_setpoint = 0;
            return false;
            
        case DRIVE_STATE_READY_TO_SWITCH_ON:
            // Switch on
            output->control_word = STW1_ON | STW1_NO_OFF2 | STW1_NO_OFF3;
            output->speed_setpoint = 0;
            return false;
            
        case DRIVE_STATE_SWITCHED_ON:
            // Enable operation
            output->control_word = STW1_ON | STW1_NO_OFF2 | 
                                  STW1_NO_OFF3 | STW1_ENABLE_OPERATION;
            output->speed_setpoint = 0;
            return false;
            
        case DRIVE_STATE_OPERATION_ENABLED:
            // Set speed
            output->control_word = STW1_ON | STW1_NO_OFF2 | 
                                  STW1_NO_OFF3 | STW1_ENABLE_OPERATION;
            output->speed_setpoint = rpm_to_profidrive(target_rpm, max_rpm);
            return true;
    }
    
    return false;
}

// Example: Velocity control with torque limiting (Telegram 3)
void velocity_control_with_torque_limit(ProfiDrive_Telegram3_Write* output,
                                        float target_rpm, float max_rpm,
                                        float torque_limit_percent) {
    output->control_word = STW1_ON | STW1_NO_OFF2 | 
                          STW1_NO_OFF3 | STW1_ENABLE_OPERATION;
    output->speed_setpoint = rpm_to_profidrive(target_rpm, max_rpm);
    output->torque_limit_pos = torque_percent_to_profidrive(torque_limit_percent);
    output->torque_limit_neg = torque_percent_to_profidrive(torque_limit_percent);
}
```

## Rust Implementation

```rust
use std::io;

// PROFIdrive telegram structures
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct ProfiDriveTelegram1Write {
    pub control_word: u16,      // STW1
    pub speed_setpoint: i16,    // NSOLL_A
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct ProfiDriveTelegram1Read {
    pub status_word: u16,       // ZSW1
    pub speed_actual: i16,      // NIST_A
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct ProfiDriveTelegram3Write {
    pub control_word: u16,
    pub speed_setpoint: i16,
    pub torque_limit_pos: i16,
    pub torque_limit_neg: i16,
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct ProfiDriveTelegram3Read {
    pub status_word: u16,
    pub speed_actual: i16,
    pub torque_actual: i16,
}

// Control word bits
pub mod control_word {
    pub const ON: u16 = 1 << 0;
    pub const NO_OFF2: u16 = 1 << 1;
    pub const NO_OFF3: u16 = 1 << 2;
    pub const ENABLE_OPERATION: u16 = 1 << 3;
    pub const FAULT_RESET: u16 = 1 << 7;
}

// Status word bits
pub mod status_word {
    pub const READY_TO_SWITCH_ON: u16 = 1 << 0;
    pub const READY_TO_OPERATE: u16 = 1 << 1;
    pub const OPERATION_ENABLED: u16 = 1 << 2;
    pub const FAULT: u16 = 1 << 3;
    pub const NO_OFF2: u16 = 1 << 4;
    pub const NO_OFF3: u16 = 1 << 5;
    pub const SWITCH_ON_LOCKED: u16 = 1 << 6;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DriveState {
    NotReady,
    SwitchOnDisabled,
    ReadyToSwitchOn,
    SwitchedOn,
    OperationEnabled,
    Fault,
}

pub struct DriveController {
    max_rpm: f32,
    max_torque_nm: f32,
}

impl DriveController {
    pub fn new(max_rpm: f32, max_torque_nm: f32) -> Self {
        Self {
            max_rpm,
            max_torque_nm,
        }
    }

    /// Convert RPM to PROFIdrive normalized value
    pub fn rpm_to_profidrive(&self, rpm: f32) -> i16 {
        let normalized = (rpm / self.max_rpm) * 16384.0;
        normalized.clamp(-32768.0, 32767.0) as i16
    }

    /// Convert PROFIdrive value to RPM
    pub fn profidrive_to_rpm(&self, value: i16) -> f32 {
        (value as f32 / 16384.0) * self.max_rpm
    }

    /// Convert torque percentage to PROFIdrive value
    pub fn torque_percent_to_profidrive(&self, torque_percent: f32) -> i16 {
        let normalized = (torque_percent / 100.0) * 16384.0;
        normalized.clamp(-32768.0, 32767.0) as i16
    }

    /// Convert PROFIdrive value to torque percentage
    pub fn profidrive_to_torque_percent(&self, value: i16) -> f32 {
        (value as f32 / 16384.0) * 100.0
    }

    /// Determine drive state from status word
    pub fn get_drive_state(&self, status_word: u16) -> DriveState {
        if status_word & status_word::FAULT != 0 {
            return DriveState::Fault;
        }

        match status_word & 0x6F {
            0x00 => DriveState::NotReady,
            0x21 => DriveState::ReadyToSwitchOn,
            0x23 => DriveState::SwitchedOn,
            0x27 => DriveState::OperationEnabled,
            _ => {
                if status_word & 0x4F == 0x40 {
                    DriveState::SwitchOnDisabled
                } else {
                    DriveState::NotReady
                }
            }
        }
    }

    /// State machine for enabling drive and setting speed
    pub fn enable_and_set_speed(
        &self,
        input: &ProfiDriveTelegram1Read,
        target_rpm: f32,
    ) -> (ProfiDriveTelegram1Write, bool) {
        let state = self.get_drive_state(input.status_word);
        
        let (control_word, speed_setpoint, ready) = match state {
            DriveState::Fault => {
                (control_word::FAULT_RESET, 0, false)
            }
            DriveState::NotReady | DriveState::SwitchOnDisabled => {
                (0, 0, false)
            }
            DriveState::ReadyToSwitchOn => {
                (control_word::ON | control_word::NO_OFF2 | control_word::NO_OFF3, 
                 0, false)
            }
            DriveState::SwitchedOn => {
                (control_word::ON | control_word::NO_OFF2 | 
                 control_word::NO_OFF3 | control_word::ENABLE_OPERATION,
                 0, false)
            }
            DriveState::OperationEnabled => {
                (control_word::ON | control_word::NO_OFF2 | 
                 control_word::NO_OFF3 | control_word::ENABLE_OPERATION,
                 self.rpm_to_profidrive(target_rpm), true)
            }
        };

        (ProfiDriveTelegram1Write { control_word, speed_setpoint }, ready)
    }

    /// Velocity control with torque limiting
    pub fn velocity_with_torque_limit(
        &self,
        target_rpm: f32,
        torque_limit_percent: f32,
    ) -> ProfiDriveTelegram3Write {
        ProfiDriveTelegram3Write {
            control_word: control_word::ON | control_word::NO_OFF2 | 
                         control_word::NO_OFF3 | control_word::ENABLE_OPERATION,
            speed_setpoint: self.rpm_to_profidrive(target_rpm),
            torque_limit_pos: self.torque_percent_to_profidrive(torque_limit_percent),
            torque_limit_neg: self.torque_percent_to_profidrive(torque_limit_percent),
        }
    }

    /// Emergency stop
    pub fn emergency_stop(&self) -> ProfiDriveTelegram1Write {
        ProfiDriveTelegram1Write {
            control_word: 0, // Remove all enable bits
            speed_setpoint: 0,
        }
    }

    /// Quick stop (controlled deceleration)
    pub fn quick_stop(&self) -> ProfiDriveTelegram1Write {
        ProfiDriveTelegram1Write {
            control_word: control_word::ON | control_word::NO_OFF2, // OFF3 not set
            speed_setpoint: 0,
        }
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_velocity_control() {
        let controller = DriveController::new(3000.0, 100.0);
        
        // Simulate drive ready state
        let input = ProfiDriveTelegram1Read {
            status_word: 0x27, // Operation enabled
            speed_actual: 0,
        };

        let (output, ready) = controller.enable_and_set_speed(&input, 1500.0);
        
        assert!(ready);
        assert_eq!(output.speed_setpoint, controller.rpm_to_profidrive(1500.0));
    }

    #[test]
    fn test_torque_limiting() {
        let controller = DriveController::new(3000.0, 100.0);
        
        let output = controller.velocity_with_torque_limit(1500.0, 80.0);
        
        assert_eq!(output.speed_setpoint, controller.rpm_to_profidrive(1500.0));
        assert_eq!(output.torque_limit_pos, controller.torque_percent_to_profidrive(80.0));
    }
}
```

## Summary

**Velocity and Torque Control** in Profibus via PROFIdrive provides a standardized, manufacturer-independent interface for drive systems:

**Key Features:**
- **Standardized telegrams** define cyclic data exchange formats for different control modes
- **Control/status words** provide state machine management and fault handling
- **Normalized values** (16384 = 100%) ensure consistent scaling across vendors
- **Dual-mode operation** supports both velocity control with torque limiting and pure torque control

**Implementation Considerations:**
- State machine sequencing is critical for safe drive operation
- Conversion between engineering units and PROFIdrive normalized values must account for device-specific maximum values
- Torque limiting in velocity mode enables safe operation under load constraints
- Real-time cyclic communication ensures precise control with deterministic behavior

**Applications:**
- Conveyor systems with variable speed requirements
- Servo positioning with velocity profiling
- Tension control in winding/unwinding applications
- Coordinated multi-axis motion control
- Process automation requiring precise speed regulation

The PROFIdrive profile's standardization enables seamless integration of drives from multiple manufacturers while maintaining consistent control behavior and diagnostic capabilities across the entire system.