# DPV1 Alarms and Events in Profibus

## Overview

DPV1 (Decentralized Periphery Version 1) extends the basic Profibus DP protocol with advanced features, including a sophisticated alarm and event handling mechanism. This allows intelligent slaves (devices) to asynchronously notify the master (controller) of important status changes, diagnostic information, or critical events without waiting for cyclic polling.

## What Are DPV1 Alarms?

DPV1 defines several alarm types that slaves can send to masters:

- **Status Alarms**: Notify about device state changes (e.g., operational → maintenance mode)
- **Update Alarms**: Signal that diagnostic data has changed and should be read
- **Manufacturer-Specific Alarms**: Custom alarms defined by device vendors for proprietary functionality

These alarms are transmitted acyclically (outside the normal cyclic data exchange) and have higher priority, ensuring time-critical information reaches the master promptly.

## Alarm Structure

Each DPV1 alarm contains:
- **Alarm Type**: Identifies the category (status, update, manufacturer)
- **Slot Number**: Physical or logical module location
- **Specifier**: Additional classification information
- **Sequence Number**: For tracking and acknowledgment
- **Diagnostic Data**: Optional payload with detailed information

## Programming Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// DPV1 Alarm Types
typedef enum {
    DPV1_ALARM_TYPE_DIAGNOSTIC = 0x00,
    DPV1_ALARM_TYPE_PROCESS = 0x01,
    DPV1_ALARM_TYPE_PULL = 0x02,
    DPV1_ALARM_TYPE_PLUG = 0x03,
    DPV1_ALARM_TYPE_STATUS = 0x04,
    DPV1_ALARM_TYPE_UPDATE = 0x05,
    DPV1_ALARM_TYPE_MANUFACTURER_MIN = 0x20,
    DPV1_ALARM_TYPE_MANUFACTURER_MAX = 0x7F
} dpv1_alarm_type_t;

// Alarm Specifier bits
#define DPV1_SPEC_SEQ_MASK    0x0F
#define DPV1_SPEC_APPEARS     0x10
#define DPV1_SPEC_DISAPPEARS  0x00

// DPV1 Alarm structure
typedef struct {
    uint8_t alarm_type;
    uint8_t slot_number;
    uint8_t specifier;
    uint8_t sequence_number;
    uint16_t diag_data_len;
    uint8_t diag_data[244];  // Max diagnostic data
} dpv1_alarm_t;

// Alarm handler callback type
typedef void (*alarm_handler_fn)(uint8_t slave_addr, const dpv1_alarm_t *alarm);

// Global alarm handler registry
typedef struct {
    uint8_t slave_addr;
    alarm_handler_fn handler;
} alarm_handler_entry_t;

#define MAX_ALARM_HANDLERS 16
static alarm_handler_entry_t g_alarm_handlers[MAX_ALARM_HANDLERS];
static int g_handler_count = 0;

// Register an alarm handler for a specific slave
bool dpv1_register_alarm_handler(uint8_t slave_addr, alarm_handler_fn handler) {
    if (g_handler_count >= MAX_ALARM_HANDLERS) {
        return false;
    }
    
    g_alarm_handlers[g_handler_count].slave_addr = slave_addr;
    g_alarm_handlers[g_handler_count].handler = handler;
    g_handler_count++;
    
    return true;
}

// Process incoming alarm from slave
void dpv1_process_alarm(uint8_t slave_addr, const uint8_t *alarm_frame, 
                        uint16_t frame_len) {
    if (frame_len < 4) {
        return;  // Invalid alarm frame
    }
    
    dpv1_alarm_t alarm;
    alarm.alarm_type = alarm_frame[0];
    alarm.slot_number = alarm_frame[1];
    alarm.specifier = alarm_frame[2];
    alarm.sequence_number = alarm_frame[3];
    
    // Extract diagnostic data if present
    if (frame_len > 4) {
        alarm.diag_data_len = frame_len - 4;
        if (alarm.diag_data_len > sizeof(alarm.diag_data)) {
            alarm.diag_data_len = sizeof(alarm.diag_data);
        }
        memcpy(alarm.diag_data, &alarm_frame[4], alarm.diag_data_len);
    } else {
        alarm.diag_data_len = 0;
    }
    
    // Call registered handlers
    for (int i = 0; i < g_handler_count; i++) {
        if (g_alarm_handlers[i].slave_addr == slave_addr) {
            g_alarm_handlers[i].handler(slave_addr, &alarm);
        }
    }
}

// Example: Status alarm handler
void handle_status_alarm(uint8_t slave_addr, const dpv1_alarm_t *alarm) {
    printf("Status alarm from slave %d:\n", slave_addr);
    printf("  Slot: %d\n", alarm->slot_number);
    printf("  Sequence: %d\n", alarm->sequence_number);
    
    bool appears = (alarm->specifier & DPV1_SPEC_APPEARS) != 0;
    printf("  Status: %s\n", appears ? "Appears" : "Disappears");
    
    // Process diagnostic data
    if (alarm->diag_data_len > 0) {
        printf("  Diagnostic data (%d bytes): ", alarm->diag_data_len);
        for (int i = 0; i < alarm->diag_data_len; i++) {
            printf("%02X ", alarm->diag_data[i]);
        }
        printf("\n");
    }
}

// Example: Acknowledge alarm
bool dpv1_acknowledge_alarm(uint8_t slave_addr, uint8_t alarm_type, 
                            uint8_t sequence_number) {
    // Build DPV1 alarm acknowledgment frame
    uint8_t ack_frame[8];
    ack_frame[0] = 0x5C;  // DPV1 Read service
    ack_frame[1] = 0xB1;  // Alarm Acknowledge
    ack_frame[2] = alarm_type;
    ack_frame[3] = sequence_number;
    
    // Send acknowledgment to slave (implementation specific)
    // return profibus_send_acyclic(slave_addr, ack_frame, 4);
    
    printf("Acknowledging alarm type %d, seq %d for slave %d\n",
           alarm_type, sequence_number, slave_addr);
    return true;
}

// Example usage
int main() {
    // Register alarm handler for slave address 5
    dpv1_register_alarm_handler(5, handle_status_alarm);
    
    // Simulate receiving an alarm
    uint8_t alarm_data[] = {
        DPV1_ALARM_TYPE_STATUS,  // Status alarm
        0x01,                     // Slot 1
        0x10,                     // Appears
        0x05,                     // Sequence 5
        0xFF, 0x01, 0x80         // Diagnostic data
    };
    
    dpv1_process_alarm(5, alarm_data, sizeof(alarm_data));
    dpv1_acknowledge_alarm(5, DPV1_ALARM_TYPE_STATUS, 0x05);
    
    return 0;
}
```

### Rust Implementation

```rust
use std::collections::HashMap;
use std::sync::{Arc, Mutex};

// DPV1 Alarm Types
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Dpv1AlarmType {
    Diagnostic = 0x00,
    Process = 0x01,
    Pull = 0x02,
    Plug = 0x03,
    Status = 0x04,
    Update = 0x05,
    Manufacturer(u8), // 0x20-0x7F
}

impl From<u8> for Dpv1AlarmType {
    fn from(value: u8) -> Self {
        match value {
            0x00 => Self::Diagnostic,
            0x01 => Self::Process,
            0x02 => Self::Pull,
            0x03 => Self::Plug,
            0x04 => Self::Status,
            0x05 => Self::Update,
            0x20..=0x7F => Self::Manufacturer(value),
            _ => Self::Diagnostic, // Default fallback
        }
    }
}

// Alarm Specifier
#[derive(Debug, Clone)]
pub struct AlarmSpecifier {
    pub sequence: u8,
    pub appears: bool,
}

impl From<u8> for AlarmSpecifier {
    fn from(value: u8) -> Self {
        Self {
            sequence: value & 0x0F,
            appears: (value & 0x10) != 0,
        }
    }
}

// DPV1 Alarm structure
#[derive(Debug, Clone)]
pub struct Dpv1Alarm {
    pub alarm_type: Dpv1AlarmType,
    pub slot_number: u8,
    pub specifier: AlarmSpecifier,
    pub sequence_number: u8,
    pub diagnostic_data: Vec<u8>,
}

impl Dpv1Alarm {
    pub fn from_frame(frame: &[u8]) -> Result<Self, &'static str> {
        if frame.len() < 4 {
            return Err("Invalid alarm frame length");
        }

        Ok(Self {
            alarm_type: Dpv1AlarmType::from(frame[0]),
            slot_number: frame[1],
            specifier: AlarmSpecifier::from(frame[2]),
            sequence_number: frame[3],
            diagnostic_data: if frame.len() > 4 {
                frame[4..].to_vec()
            } else {
                Vec::new()
            },
        })
    }

    pub fn is_manufacturer_specific(&self) -> bool {
        matches!(self.alarm_type, Dpv1AlarmType::Manufacturer(_))
    }
}

// Alarm handler trait
pub trait AlarmHandler: Send + Sync {
    fn handle_alarm(&self, slave_addr: u8, alarm: &Dpv1Alarm);
}

// Alarm manager
pub struct Dpv1AlarmManager {
    handlers: Arc<Mutex<HashMap<u8, Vec<Box<dyn AlarmHandler>>>>>,
}

impl Dpv1AlarmManager {
    pub fn new() -> Self {
        Self {
            handlers: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    pub fn register_handler(&self, slave_addr: u8, handler: Box<dyn AlarmHandler>) {
        let mut handlers = self.handlers.lock().unwrap();
        handlers.entry(slave_addr).or_insert_with(Vec::new).push(handler);
    }

    pub fn process_alarm(&self, slave_addr: u8, frame: &[u8]) -> Result<(), &'static str> {
        let alarm = Dpv1Alarm::from_frame(frame)?;

        let handlers = self.handlers.lock().unwrap();
        if let Some(slave_handlers) = handlers.get(&slave_addr) {
            for handler in slave_handlers {
                handler.handle_alarm(slave_addr, &alarm);
            }
        }

        Ok(())
    }

    pub fn acknowledge_alarm(&self, slave_addr: u8, alarm_type: Dpv1AlarmType, 
                            sequence: u8) -> Result<(), &'static str> {
        println!("Acknowledging alarm type {:?}, seq {} for slave {}",
                 alarm_type, sequence, slave_addr);
        
        // Build acknowledgment frame
        let ack_frame = vec![
            0x5C,  // DPV1 Read service
            0xB1,  // Alarm Acknowledge
            alarm_type as u8,
            sequence,
        ];

        // Send to slave (implementation specific)
        // profibus_send_acyclic(slave_addr, &ack_frame)?;

        Ok(())
    }
}

// Example alarm handler implementation
struct StatusAlarmHandler;

impl AlarmHandler for StatusAlarmHandler {
    fn handle_alarm(&self, slave_addr: u8, alarm: &Dpv1Alarm) {
        if matches!(alarm.alarm_type, Dpv1AlarmType::Status) {
            println!("Status alarm from slave {}:", slave_addr);
            println!("  Slot: {}", alarm.slot_number);
            println!("  Sequence: {}", alarm.sequence_number);
            println!("  Status: {}", 
                     if alarm.specifier.appears { "Appears" } else { "Disappears" });

            if !alarm.diagnostic_data.is_empty() {
                print!("  Diagnostic data ({} bytes): ", alarm.diagnostic_data.len());
                for byte in &alarm.diagnostic_data {
                    print!("{:02X} ", byte);
                }
                println!();
            }
        }
    }
}

// Manufacturer-specific alarm handler
struct ManufacturerAlarmHandler {
    vendor_id: u16,
}

impl AlarmHandler for ManufacturerAlarmHandler {
    fn handle_alarm(&self, slave_addr: u8, alarm: &Dpv1Alarm) {
        if let Dpv1AlarmType::Manufacturer(code) = alarm.alarm_type {
            println!("Manufacturer alarm 0x{:02X} from slave {} (vendor 0x{:04X})",
                     code, slave_addr, self.vendor_id);
            
            // Parse vendor-specific diagnostic data
            self.parse_vendor_diagnostics(&alarm.diagnostic_data);
        }
    }
}

impl ManufacturerAlarmHandler {
    fn new(vendor_id: u16) -> Self {
        Self { vendor_id }
    }

    fn parse_vendor_diagnostics(&self, data: &[u8]) {
        // Vendor-specific parsing logic
        println!("  Vendor diagnostic data: {:?}", data);
    }
}

// Example usage
fn main() {
    let alarm_manager = Dpv1AlarmManager::new();

    // Register handlers
    alarm_manager.register_handler(5, Box::new(StatusAlarmHandler));
    alarm_manager.register_handler(5, Box::new(ManufacturerAlarmHandler::new(0x002A)));

    // Simulate receiving alarms
    let status_alarm = vec![
        0x04,  // Status alarm
        0x01,  // Slot 1
        0x10,  // Appears
        0x05,  // Sequence 5
        0xFF, 0x01, 0x80,  // Diagnostic data
    ];

    let mfg_alarm = vec![
        0x25,  // Manufacturer alarm 0x25
        0x02,  // Slot 2
        0x00,  // Specifier
        0x08,  // Sequence 8
        0xDE, 0xAD, 0xBE, 0xEF,  // Vendor data
    ];

    alarm_manager.process_alarm(5, &status_alarm).unwrap();
    alarm_manager.acknowledge_alarm(5, Dpv1AlarmType::Status, 0x05).unwrap();

    alarm_manager.process_alarm(5, &mfg_alarm).unwrap();
    alarm_manager.acknowledge_alarm(5, Dpv1AlarmType::Manufacturer(0x25), 0x08).unwrap();
}
```

## Summary

DPV1 alarms and events provide a robust mechanism for intelligent Profibus slaves to notify masters of critical conditions asynchronously. The three main alarm types—status, update, and manufacturer-specific—allow devices to communicate state changes, diagnostic updates, and custom events without waiting for cyclic polling.

Key implementation considerations include proper alarm acknowledgment, sequence number tracking to prevent duplicates, and timeout handling for unacknowledged alarms. The C/C++ examples demonstrate callback-based handling suitable for embedded systems, while the Rust implementation showcases type-safe enum handling and trait-based extensibility. Both approaches emphasize registering handlers per slave address and processing alarms with their associated diagnostic data.

This event-driven architecture significantly improves system responsiveness compared to pure cyclic polling, enabling faster fault detection and more efficient bandwidth utilization in industrial automation networks.