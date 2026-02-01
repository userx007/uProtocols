# Modbus Function Code 0x0B: Get Comm Event Counter

## Detailed Description

Function Code 0x0B (Get Comm Event Counter) is a **diagnostic function** used to monitor the health and activity of Modbus serial communication networks. It retrieves a status word and an event counter from a Modbus server (slave device), providing valuable insights into communication reliability and device responsiveness.

### Purpose and Use Cases

This function serves several critical purposes:

- **Network Health Monitoring**: Track communication activity to detect silent failures or communication degradation
- **Diagnostic Troubleshooting**: Identify devices that have stopped responding or are experiencing communication issues
- **Activity Verification**: Confirm that devices are processing messages even when they don't generate data changes
- **Performance Metrics**: Collect statistics on message processing for network analysis

### How It Works

The event counter increments each time the device successfully processes **any** Modbus message addressed to it, regardless of whether the operation succeeds or fails. This creates an activity heartbeat that monitors can track.

**The Status Word** contains two key pieces of information:
- **Bit 15 (0x8000)**: Busy flag - indicates the device is processing a long-duration command
- **Bits 0-14**: Reserved for future use (typically set to 0)

**The Event Counter** is a 16-bit value (0-65535) that:
- Increments with each successfully processed message
- Wraps around from 65535 to 0
- Persists until device power cycle or explicit reset
- Does NOT increment for broadcast messages

### Message Structure

**Request (4 bytes)**:
- **Function Code**: 0x0B (1 byte)
- No additional data required

**Response (6 bytes)**:
- **Function Code**: 0x0B (1 byte)
- **Status Word**: 2 bytes (big-endian)
- **Event Count**: 2 bytes (big-endian)

**Exception Response (3 bytes)**:
- **Function Code**: 0x8B (0x0B + 0x80)
- **Exception Code**: 1 byte

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Modbus exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION      0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS  0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE    0x03
#define MODBUS_EXCEPTION_SLAVE_DEVICE_FAILURE  0x04

// Function code
#define MODBUS_FC_GET_COMM_EVENT_COUNTER       0x0B

// Status word bit masks
#define MODBUS_STATUS_BUSY                     0x8000

// Communication event counter structure
typedef struct {
    uint16_t status_word;
    uint16_t event_count;
} modbus_comm_event_counter_t;

// Global event counter (would be part of device state)
static modbus_comm_event_counter_t comm_event_data = {0, 0};

/**
 * Set the busy flag in the status word
 */
void modbus_set_busy(bool busy) {
    if (busy) {
        comm_event_data.status_word |= MODBUS_STATUS_BUSY;
    } else {
        comm_event_data.status_word &= ~MODBUS_STATUS_BUSY;
    }
}

/**
 * Increment the communication event counter
 * Called after successfully processing any Modbus message
 */
void modbus_increment_event_counter(void) {
    comm_event_data.event_count++;
    // Counter automatically wraps at 65535 due to uint16_t
}

/**
 * Reset event counter (typically on power-up or explicit command)
 */
void modbus_reset_event_counter(void) {
    comm_event_data.event_count = 0;
    comm_event_data.status_word = 0;
}

/**
 * SERVER: Process Get Comm Event Counter request
 * 
 * @param request: Pointer to request buffer
 * @param request_len: Length of request
 * @param response: Pointer to response buffer
 * @return: Length of response, or negative on error
 */
int modbus_get_comm_event_counter_server(
    const uint8_t *request,
    size_t request_len,
    uint8_t *response
) {
    // Validate request length (function code only)
    if (request_len != 1) {
        response[0] = MODBUS_FC_GET_COMM_EVENT_COUNTER | 0x80;
        response[1] = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        return 2;
    }

    // Build response
    response[0] = MODBUS_FC_GET_COMM_EVENT_COUNTER;
    
    // Status word (big-endian)
    response[1] = (comm_event_data.status_word >> 8) & 0xFF;
    response[2] = comm_event_data.status_word & 0xFF;
    
    // Event count (big-endian)
    response[3] = (comm_event_data.event_count >> 8) & 0xFF;
    response[4] = comm_event_data.event_count & 0xFF;
    
    return 5;
}

/**
 * CLIENT: Build Get Comm Event Counter request
 * 
 * @param request: Pointer to request buffer (min 1 byte)
 * @return: Length of request
 */
int modbus_build_get_comm_event_counter_request(uint8_t *request) {
    request[0] = MODBUS_FC_GET_COMM_EVENT_COUNTER;
    return 1;
}

/**
 * CLIENT: Parse Get Comm Event Counter response
 * 
 * @param response: Pointer to response buffer
 * @param response_len: Length of response
 * @param result: Pointer to structure to store results
 * @return: 0 on success, negative on error
 */
int modbus_parse_get_comm_event_counter_response(
    const uint8_t *response,
    size_t response_len,
    modbus_comm_event_counter_t *result
) {
    // Check for exception response
    if (response[0] & 0x80) {
        return -(int)response[1]; // Return negative exception code
    }
    
    // Validate response length
    if (response_len != 5) {
        return -1;
    }
    
    // Validate function code
    if (response[0] != MODBUS_FC_GET_COMM_EVENT_COUNTER) {
        return -1;
    }
    
    // Parse status word (big-endian)
    result->status_word = ((uint16_t)response[1] << 8) | response[2];
    
    // Parse event count (big-endian)
    result->event_count = ((uint16_t)response[3] << 8) | response[4];
    
    return 0;
}

// Example usage
void example_usage(void) {
    uint8_t request[256];
    uint8_t response[256];
    modbus_comm_event_counter_t result;
    
    // CLIENT: Build and send request
    int req_len = modbus_build_get_comm_event_counter_request(request);
    // ... send request over network ...
    
    // SERVER: Process request
    int resp_len = modbus_get_comm_event_counter_server(request, req_len, response);
    // ... send response over network ...
    
    // CLIENT: Parse response
    if (modbus_parse_get_comm_event_counter_response(response, resp_len, &result) == 0) {
        printf("Status: 0x%04X, Event Count: %u\n", 
               result.status_word, result.event_count);
        
        if (result.status_word & MODBUS_STATUS_BUSY) {
            printf("Device is BUSY\n");
        }
    }
}

/**
 * Monitor function to detect inactive devices
 * Call periodically to check if device is responding
 */
typedef struct {
    uint16_t last_event_count;
    uint32_t polls_without_change;
    bool device_active;
} device_monitor_t;

void monitor_device_health(
    device_monitor_t *monitor,
    const modbus_comm_event_counter_t *current_data
) {
    if (current_data->event_count != monitor->last_event_count) {
        // Device is processing messages
        monitor->polls_without_change = 0;
        monitor->device_active = true;
        monitor->last_event_count = current_data->event_count;
    } else {
        // No change detected
        monitor->polls_without_change++;
        
        // Consider device inactive after 10 polls with no change
        if (monitor->polls_without_change > 10) {
            monitor->device_active = false;
        }
    }
}
```

## Rust Implementation

```rust
use std::io::{self, Error, ErrorKind};

// Function code constant
const MODBUS_FC_GET_COMM_EVENT_COUNTER: u8 = 0x0B;

// Status word bit masks
const MODBUS_STATUS_BUSY: u16 = 0x8000;

// Exception codes
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum ModbusException {
    IllegalFunction = 0x01,
    IllegalDataAddress = 0x02,
    IllegalDataValue = 0x03,
    SlaveDeviceFailure = 0x04,
}

/// Communication event counter data structure
#[derive(Debug, Clone, Copy, Default)]
pub struct CommEventCounter {
    pub status_word: u16,
    pub event_count: u16,
}

impl CommEventCounter {
    /// Create a new CommEventCounter with default values
    pub fn new() -> Self {
        Self {
            status_word: 0,
            event_count: 0,
        }
    }

    /// Check if the busy flag is set
    pub fn is_busy(&self) -> bool {
        (self.status_word & MODBUS_STATUS_BUSY) != 0
    }

    /// Set or clear the busy flag
    pub fn set_busy(&mut self, busy: bool) {
        if busy {
            self.status_word |= MODBUS_STATUS_BUSY;
        } else {
            self.status_word &= !MODBUS_STATUS_BUSY;
        }
    }

    /// Increment the event counter (wraps automatically)
    pub fn increment(&mut self) {
        self.event_count = self.event_count.wrapping_add(1);
    }

    /// Reset counter and status
    pub fn reset(&mut self) {
        self.status_word = 0;
        self.event_count = 0;
    }
}

/// SERVER: Process Get Comm Event Counter request
pub fn process_get_comm_event_counter_request(
    request: &[u8],
    event_data: &CommEventCounter,
) -> Result<Vec<u8>, ModbusException> {
    // Validate request length (only function code)
    if request.len() != 1 {
        return Err(ModbusException::IllegalDataValue);
    }

    // Build response
    let mut response = Vec::with_capacity(5);
    response.push(MODBUS_FC_GET_COMM_EVENT_COUNTER);
    
    // Status word (big-endian)
    response.extend_from_slice(&event_data.status_word.to_be_bytes());
    
    // Event count (big-endian)
    response.extend_from_slice(&event_data.event_count.to_be_bytes());

    Ok(response)
}

/// CLIENT: Build Get Comm Event Counter request
pub fn build_get_comm_event_counter_request() -> Vec<u8> {
    vec![MODBUS_FC_GET_COMM_EVENT_COUNTER]
}

/// CLIENT: Parse Get Comm Event Counter response
pub fn parse_get_comm_event_counter_response(
    response: &[u8]
) -> io::Result<CommEventCounter> {
    // Check for exception response
    if response.is_empty() {
        return Err(Error::new(ErrorKind::InvalidData, "Empty response"));
    }

    if response[0] & 0x80 != 0 {
        let exception_code = if response.len() > 1 { response[1] } else { 0 };
        return Err(Error::new(
            ErrorKind::Other,
            format!("Modbus exception: 0x{:02X}", exception_code)
        ));
    }

    // Validate response length
    if response.len() != 5 {
        return Err(Error::new(
            ErrorKind::InvalidData,
            format!("Invalid response length: expected 5, got {}", response.len())
        ));
    }

    // Validate function code
    if response[0] != MODBUS_FC_GET_COMM_EVENT_COUNTER {
        return Err(Error::new(
            ErrorKind::InvalidData,
            "Invalid function code in response"
        ));
    }

    // Parse status word (big-endian)
    let status_word = u16::from_be_bytes([response[1], response[2]]);
    
    // Parse event count (big-endian)
    let event_count = u16::from_be_bytes([response[3], response[4]]);

    Ok(CommEventCounter {
        status_word,
        event_count,
    })
}

/// Device health monitor for tracking communication activity
#[derive(Debug)]
pub struct DeviceMonitor {
    last_event_count: u16,
    polls_without_change: u32,
    device_active: bool,
}

impl DeviceMonitor {
    pub fn new() -> Self {
        Self {
            last_event_count: 0,
            polls_without_change: 0,
            device_active: true,
        }
    }

    /// Update monitor with new event counter data
    /// Returns true if device is considered active
    pub fn update(&mut self, current_data: &CommEventCounter) -> bool {
        if current_data.event_count != self.last_event_count {
            // Device is processing messages
            self.polls_without_change = 0;
            self.device_active = true;
            self.last_event_count = current_data.event_count;
        } else {
            // No change detected
            self.polls_without_change += 1;
            
            // Consider device inactive after 10 polls with no change
            if self.polls_without_change > 10 {
                self.device_active = false;
            }
        }
        
        self.device_active
    }

    pub fn is_active(&self) -> bool {
        self.device_active
    }

    pub fn polls_without_change(&self) -> u32 {
        self.polls_without_change
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_comm_event_counter_operations() {
        let mut counter = CommEventCounter::new();
        
        assert_eq!(counter.event_count, 0);
        assert_eq!(counter.is_busy(), false);
        
        counter.set_busy(true);
        assert_eq!(counter.is_busy(), true);
        
        counter.increment();
        assert_eq!(counter.event_count, 1);
        
        counter.reset();
        assert_eq!(counter.event_count, 0);
        assert_eq!(counter.is_busy(), false);
    }

    #[test]
    fn test_request_response_cycle() {
        let mut event_data = CommEventCounter::new();
        event_data.event_count = 42;
        event_data.set_busy(true);

        // Build request
        let request = build_get_comm_event_counter_request();
        assert_eq!(request.len(), 1);

        // Process request
        let response = process_get_comm_event_counter_request(&request, &event_data)
            .expect("Failed to process request");
        assert_eq!(response.len(), 5);

        // Parse response
        let result = parse_get_comm_event_counter_response(&response)
            .expect("Failed to parse response");
        
        assert_eq!(result.event_count, 42);
        assert_eq!(result.is_busy(), true);
    }

    #[test]
    fn test_counter_wrapping() {
        let mut counter = CommEventCounter::new();
        counter.event_count = 65535;
        
        counter.increment();
        assert_eq!(counter.event_count, 0); // Wraps to 0
    }

    #[test]
    fn test_device_monitor() {
        let mut monitor = DeviceMonitor::new();
        let mut counter = CommEventCounter::new();
        
        // Initial state
        assert_eq!(monitor.is_active(), true);
        
        // Simulate 5 polls with no change
        for _ in 0..5 {
            monitor.update(&counter);
        }
        assert_eq!(monitor.is_active(), true);
        assert_eq!(monitor.polls_without_change(), 5);
        
        // Simulate 6 more polls (total 11) - should become inactive
        for _ in 0..6 {
            monitor.update(&counter);
        }
        assert_eq!(monitor.is_active(), false);
        
        // Increment counter - should become active again
        counter.increment();
        monitor.update(&counter);
        assert_eq!(monitor.is_active(), true);
        assert_eq!(monitor.polls_without_change(), 0);
    }
}

fn main() {
    // Example: Client-side usage
    let request = build_get_comm_event_counter_request();
    println!("Request: {:02X?}", request);
    
    // Simulate server response
    let mut server_data = CommEventCounter::new();
    server_data.event_count = 1234;
    server_data.set_busy(false);
    
    match process_get_comm_event_counter_request(&request, &server_data) {
        Ok(response) => {
            println!("Response: {:02X?}", response);
            
            match parse_get_comm_event_counter_response(&response) {
                Ok(result) => {
                    println!("Status Word: 0x{:04X}", result.status_word);
                    println!("Event Count: {}", result.event_count);
                    println!("Device Busy: {}", result.is_busy());
                }
                Err(e) => println!("Parse error: {}", e),
            }
        }
        Err(e) => println!("Processing error: {:?}", e),
    }
}
```

## Summary

**Modbus Function Code 0x0B (Get Comm Event Counter)** is a lightweight diagnostic tool for monitoring Modbus network health. It returns a 16-bit event counter that increments with each processed message and a status word indicating device busy state.

**Key Characteristics:**
- **Minimal overhead**: Single-byte request, 5-byte response
- **Passive monitoring**: No impact on device operation
- **Wraparound counter**: Automatically resets at 65,535
- **Activity heartbeat**: Detects silent failures and communication issues

**Common Applications:**
- Network health dashboards
- Automated device availability monitoring
- Troubleshooting communication problems
- Performance metrics collection

**Implementation Highlights:**
- Simple request/response structure
- Big-endian byte ordering
- Counter persists until power cycle
- Often used with Function Code 0x0C (Get Comm Event Log) for comprehensive diagnostics

This function is particularly valuable in industrial environments where detecting failed or unresponsive devices quickly is critical for maintaining system reliability.