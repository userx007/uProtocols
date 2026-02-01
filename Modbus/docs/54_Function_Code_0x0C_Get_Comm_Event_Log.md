# Modbus Function Code 0x0C: Get Comm Event Log

## Detailed Description

**Function Code 0x0C (Get Comm Event Log)** is a diagnostic function in the Modbus protocol that retrieves a detailed communication event log from a slave device. This function provides comprehensive information about the device's communication history, including statistics and timestamped events that can be used for troubleshooting, performance monitoring, and system diagnostics.

### Purpose and Use Cases

The Get Comm Event Log function serves several critical purposes:

1. **Diagnostics and Troubleshooting**: Identify communication errors, timeouts, and protocol violations
2. **Performance Monitoring**: Track message counts and communication patterns
3. **Event History**: Retrieve timestamped events showing what operations the slave has performed
4. **System Health**: Monitor the overall health of Modbus communication networks

### Request Format

The request for Function Code 0x0C is simple and contains no data:

```
[Slave Address][Function Code 0x0C]
```

**Fields:**
- **Slave Address** (1 byte): Address of the target slave device
- **Function Code** (1 byte): 0x0C

### Response Format

The response is more complex and contains multiple components:

```
[Slave Address][Function Code 0x0C][Byte Count][Status][Event Count][Message Count][Events...]
```

**Response Fields:**

1. **Slave Address** (1 byte): Address of the responding slave
2. **Function Code** (1 byte): 0x0C (echo of request)
3. **Byte Count** (1 byte): Total number of bytes following this field
4. **Status** (2 bytes): Current status of the slave device
   - **Bit 15 (0x8000)**: Busy flag
   - **Bit 14 (0x4000)**: Event log full
   - **Bits 8-13**: Reserved
   - **Bits 0-7**: Event log byte count
5. **Event Count** (2 bytes): Number of events in the slave's event log
6. **Message Count** (2 bytes): Number of messages processed by the slave
7. **Events** (variable): Array of event bytes, each representing a specific event

### Event Byte Structure

Each event byte in the log can represent:
- **Communication events**: Restart, diagnostics, etc.
- **Modbus send events**: Successful message transmissions
- **Modbus receive events**: Message receptions
- **Enter listen only mode events**

Event byte format:
- **Bit 7**: 0 = Modbus event, 1 = Communication event
- **Bits 0-6**: Event code specific to the event type

### Status Word Details

The 16-bit status word provides:
- **High Byte (bits 8-15)**: System status flags
- **Low Byte (bits 0-7)**: Number of bytes in event log

---

## C/C++ Implementation

### Header File (modbus_event_log.h)

```c
#ifndef MODBUS_EVENT_LOG_H
#define MODBUS_EVENT_LOG_H

#include <stdint.h>
#include <stdbool.h>

// Modbus Function Code
#define MODBUS_FC_GET_COMM_EVENT_LOG 0x0C

// Status word bit masks
#define STATUS_BUSY_FLAG        0x8000
#define STATUS_EVENT_LOG_FULL   0x4000
#define STATUS_BYTE_COUNT_MASK  0x00FF

// Event type masks
#define EVENT_COMM_EVENT        0x80
#define EVENT_CODE_MASK         0x7F

// Communication event codes
#define EVENT_INITIATED_COMM    0x00
#define EVENT_RESTART           0x01
#define EVENT_ENTER_LISTEN_ONLY 0x04

// Maximum event log size
#define MAX_EVENT_LOG_SIZE      64

// Structure to hold event log response
typedef struct {
    uint8_t slave_address;
    uint8_t function_code;
    uint8_t byte_count;
    uint16_t status;
    uint16_t event_count;
    uint16_t message_count;
    uint8_t events[MAX_EVENT_LOG_SIZE];
    uint8_t event_length;
} modbus_event_log_t;

// Function prototypes
int modbus_build_get_event_log_request(uint8_t slave_address, 
                                       uint8_t *request, 
                                       size_t max_len);

int modbus_parse_event_log_response(const uint8_t *response, 
                                    size_t response_len,
                                    modbus_event_log_t *event_log);

bool modbus_is_slave_busy(const modbus_event_log_t *event_log);
bool modbus_is_event_log_full(const modbus_event_log_t *event_log);
uint8_t modbus_get_event_log_byte_count(const modbus_event_log_t *event_log);

void modbus_print_event_log(const modbus_event_log_t *event_log);

#endif // MODBUS_EVENT_LOG_H
```

### Implementation File (modbus_event_log.c)

```c
#include "modbus_event_log.h"
#include <stdio.h>
#include <string.h>

/**
 * Build a Get Comm Event Log request
 * 
 * @param slave_address Modbus slave address
 * @param request Buffer to store the request
 * @param max_len Maximum buffer length
 * @return Number of bytes in request, or -1 on error
 */
int modbus_build_get_event_log_request(uint8_t slave_address, 
                                       uint8_t *request, 
                                       size_t max_len) {
    if (request == NULL || max_len < 2) {
        return -1;
    }
    
    request[0] = slave_address;
    request[1] = MODBUS_FC_GET_COMM_EVENT_LOG;
    
    return 2;
}

/**
 * Parse a Get Comm Event Log response
 * 
 * @param response Response buffer
 * @param response_len Length of response
 * @param event_log Structure to store parsed data
 * @return 0 on success, -1 on error
 */
int modbus_parse_event_log_response(const uint8_t *response, 
                                    size_t response_len,
                                    modbus_event_log_t *event_log) {
    if (response == NULL || event_log == NULL || response_len < 8) {
        return -1;
    }
    
    // Check function code
    if (response[1] != MODBUS_FC_GET_COMM_EVENT_LOG) {
        return -1;
    }
    
    event_log->slave_address = response[0];
    event_log->function_code = response[1];
    event_log->byte_count = response[2];
    
    // Parse status (big-endian)
    event_log->status = ((uint16_t)response[3] << 8) | response[4];
    
    // Parse event count (big-endian)
    event_log->event_count = ((uint16_t)response[5] << 8) | response[6];
    
    // Parse message count (big-endian)
    event_log->message_count = ((uint16_t)response[7] << 8) | response[8];
    
    // Parse events
    event_log->event_length = 0;
    size_t events_available = response_len - 9;
    size_t events_to_copy = (events_available < MAX_EVENT_LOG_SIZE) ? 
                            events_available : MAX_EVENT_LOG_SIZE;
    
    if (events_to_copy > 0) {
        memcpy(event_log->events, &response[9], events_to_copy);
        event_log->event_length = events_to_copy;
    }
    
    return 0;
}

/**
 * Check if slave is busy
 */
bool modbus_is_slave_busy(const modbus_event_log_t *event_log) {
    return (event_log->status & STATUS_BUSY_FLAG) != 0;
}

/**
 * Check if event log is full
 */
bool modbus_is_event_log_full(const modbus_event_log_t *event_log) {
    return (event_log->status & STATUS_EVENT_LOG_FULL) != 0;
}

/**
 * Get event log byte count from status
 */
uint8_t modbus_get_event_log_byte_count(const modbus_event_log_t *event_log) {
    return (uint8_t)(event_log->status & STATUS_BYTE_COUNT_MASK);
}

/**
 * Print event log information
 */
void modbus_print_event_log(const modbus_event_log_t *event_log) {
    printf("Modbus Event Log:\n");
    printf("  Slave Address: 0x%02X\n", event_log->slave_address);
    printf("  Status: 0x%04X\n", event_log->status);
    printf("    Busy: %s\n", modbus_is_slave_busy(event_log) ? "Yes" : "No");
    printf("    Log Full: %s\n", modbus_is_event_log_full(event_log) ? "Yes" : "No");
    printf("    Byte Count: %d\n", modbus_get_event_log_byte_count(event_log));
    printf("  Event Count: %u\n", event_log->event_count);
    printf("  Message Count: %u\n", event_log->message_count);
    printf("  Events (%d bytes):\n", event_log->event_length);
    
    for (int i = 0; i < event_log->event_length; i++) {
        uint8_t event = event_log->events[i];
        bool is_comm_event = (event & EVENT_COMM_EVENT) != 0;
        uint8_t code = event & EVENT_CODE_MASK;
        
        printf("    [%2d] 0x%02X - ", i, event);
        
        if (is_comm_event) {
            printf("Communication Event, Code: 0x%02X", code);
            switch (code) {
                case EVENT_INITIATED_COMM:
                    printf(" (Initiated Communication)");
                    break;
                case EVENT_RESTART:
                    printf(" (Restart)");
                    break;
                case EVENT_ENTER_LISTEN_ONLY:
                    printf(" (Enter Listen Only Mode)");
                    break;
            }
        } else {
            printf("Modbus Event, Code: 0x%02X", code);
        }
        printf("\n");
    }
}
```

### Example Usage (main.c)

```c
#include "modbus_event_log.h"
#include <stdio.h>
#include <string.h>

// Simulated serial communication functions
int modbus_send(const uint8_t *data, size_t len);
int modbus_receive(uint8_t *data, size_t max_len);

int main() {
    uint8_t request[256];
    uint8_t response[256];
    modbus_event_log_t event_log;
    uint8_t slave_address = 0x01;
    
    // Build the request
    int req_len = modbus_build_get_event_log_request(slave_address, 
                                                      request, 
                                                      sizeof(request));
    
    if (req_len < 0) {
        fprintf(stderr, "Failed to build request\n");
        return 1;
    }
    
    printf("Sending Get Comm Event Log request to slave 0x%02X\n", slave_address);
    
    // Send request (simulated)
    if (modbus_send(request, req_len) < 0) {
        fprintf(stderr, "Failed to send request\n");
        return 1;
    }
    
    // Receive response (simulated)
    int resp_len = modbus_receive(response, sizeof(response));
    if (resp_len < 0) {
        fprintf(stderr, "Failed to receive response\n");
        return 1;
    }
    
    // Parse response
    if (modbus_parse_event_log_response(response, resp_len, &event_log) < 0) {
        fprintf(stderr, "Failed to parse response\n");
        return 1;
    }
    
    // Display event log
    modbus_print_event_log(&event_log);
    
    // Check specific conditions
    if (modbus_is_slave_busy(&event_log)) {
        printf("\nWarning: Slave device is currently busy!\n");
    }
    
    if (modbus_is_event_log_full(&event_log)) {
        printf("\nWarning: Event log is full, older events may have been overwritten!\n");
    }
    
    return 0;
}

// Simulated functions (replace with actual serial/TCP implementation)
int modbus_send(const uint8_t *data, size_t len) {
    printf("TX: ");
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
    return len;
}

int modbus_receive(uint8_t *data, size_t max_len) {
    // Simulated response
    uint8_t simulated_response[] = {
        0x01,       // Slave address
        0x0C,       // Function code
        0x08,       // Byte count
        0x00, 0x08, // Status (8 bytes in log)
        0x01, 0x08, // Event count (264)
        0x01, 0x20, // Message count (288)
        0x00,       // Event: Modbus event
        0x81,       // Event: Communication restart
        0x04        // Event: Modbus send
    };
    
    size_t resp_len = sizeof(simulated_response);
    if (resp_len > max_len) {
        return -1;
    }
    
    memcpy(data, simulated_response, resp_len);
    
    printf("RX: ");
    for (size_t i = 0; i < resp_len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
    
    return resp_len;
}
```

---

## Rust Implementation

```rust
// modbus_event_log.rs

use std::fmt;

/// Modbus Function Code for Get Comm Event Log
pub const MODBUS_FC_GET_COMM_EVENT_LOG: u8 = 0x0C;

/// Status word bit masks
pub const STATUS_BUSY_FLAG: u16 = 0x8000;
pub const STATUS_EVENT_LOG_FULL: u16 = 0x4000;
pub const STATUS_BYTE_COUNT_MASK: u16 = 0x00FF;

/// Event type masks
pub const EVENT_COMM_EVENT: u8 = 0x80;
pub const EVENT_CODE_MASK: u8 = 0x7F;

/// Communication event codes
pub const EVENT_INITIATED_COMM: u8 = 0x00;
pub const EVENT_RESTART: u8 = 0x01;
pub const EVENT_ENTER_LISTEN_ONLY: u8 = 0x04;

/// Maximum event log size
pub const MAX_EVENT_LOG_SIZE: usize = 64;

#[derive(Debug, Clone)]
pub struct ModbusEventLog {
    pub slave_address: u8,
    pub function_code: u8,
    pub byte_count: u8,
    pub status: u16,
    pub event_count: u16,
    pub message_count: u16,
    pub events: Vec<u8>,
}

#[derive(Debug)]
pub enum ModbusError {
    InvalidLength,
    InvalidFunctionCode,
    BufferTooSmall,
}

impl fmt::Display for ModbusError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ModbusError::InvalidLength => write!(f, "Invalid response length"),
            ModbusError::InvalidFunctionCode => write!(f, "Invalid function code"),
            ModbusError::BufferTooSmall => write!(f, "Buffer too small"),
        }
    }
}

impl std::error::Error for ModbusError {}

impl ModbusEventLog {
    /// Build a Get Comm Event Log request
    pub fn build_request(slave_address: u8) -> Vec<u8> {
        vec![slave_address, MODBUS_FC_GET_COMM_EVENT_LOG]
    }

    /// Parse a Get Comm Event Log response
    pub fn parse_response(response: &[u8]) -> Result<Self, ModbusError> {
        if response.len() < 9 {
            return Err(ModbusError::InvalidLength);
        }

        if response[1] != MODBUS_FC_GET_COMM_EVENT_LOG {
            return Err(ModbusError::InvalidFunctionCode);
        }

        let slave_address = response[0];
        let function_code = response[1];
        let byte_count = response[2];
        
        // Parse big-endian 16-bit values
        let status = u16::from_be_bytes([response[3], response[4]]);
        let event_count = u16::from_be_bytes([response[5], response[6]]);
        let message_count = u16::from_be_bytes([response[7], response[8]]);
        
        // Extract events
        let events = response[9..].to_vec();

        Ok(ModbusEventLog {
            slave_address,
            function_code,
            byte_count,
            status,
            event_count,
            message_count,
            events,
        })
    }

    /// Check if slave is busy
    pub fn is_slave_busy(&self) -> bool {
        (self.status & STATUS_BUSY_FLAG) != 0
    }

    /// Check if event log is full
    pub fn is_event_log_full(&self) -> bool {
        (self.status & STATUS_EVENT_LOG_FULL) != 0
    }

    /// Get event log byte count from status
    pub fn get_event_log_byte_count(&self) -> u8 {
        (self.status & STATUS_BYTE_COUNT_MASK) as u8
    }

    /// Decode an event byte
    pub fn decode_event(event_byte: u8) -> (bool, u8, &'static str) {
        let is_comm_event = (event_byte & EVENT_COMM_EVENT) != 0;
        let code = event_byte & EVENT_CODE_MASK;
        
        let description = if is_comm_event {
            match code {
                EVENT_INITIATED_COMM => "Initiated Communication",
                EVENT_RESTART => "Restart",
                EVENT_ENTER_LISTEN_ONLY => "Enter Listen Only Mode",
                _ => "Unknown Communication Event",
            }
        } else {
            "Modbus Event"
        };
        
        (is_comm_event, code, description)
    }

    /// Pretty print the event log
    pub fn print(&self) {
        println!("Modbus Event Log:");
        println!("  Slave Address: 0x{:02X}", self.slave_address);
        println!("  Status: 0x{:04X}", self.status);
        println!("    Busy: {}", self.is_slave_busy());
        println!("    Log Full: {}", self.is_event_log_full());
        println!("    Byte Count: {}", self.get_event_log_byte_count());
        println!("  Event Count: {}", self.event_count);
        println!("  Message Count: {}", self.message_count);
        println!("  Events ({} bytes):", self.events.len());
        
        for (i, &event) in self.events.iter().enumerate() {
            let (is_comm, code, desc) = Self::decode_event(event);
            println!(
                "    [{:2}] 0x{:02X} - {} Event, Code: 0x{:02X} ({})",
                i,
                event,
                if is_comm { "Communication" } else { "Modbus" },
                code,
                desc
            );
        }
    }
}

// Example usage module
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_build_request() {
        let request = ModbusEventLog::build_request(0x01);
        assert_eq!(request, vec![0x01, 0x0C]);
    }

    #[test]
    fn test_parse_response() {
        let response = vec![
            0x01,       // Slave address
            0x0C,       // Function code
            0x08,       // Byte count
            0x00, 0x08, // Status
            0x01, 0x08, // Event count
            0x01, 0x20, // Message count
            0x00,       // Event
            0x81,       // Event
            0x04,       // Event
        ];

        let event_log = ModbusEventLog::parse_response(&response).unwrap();
        
        assert_eq!(event_log.slave_address, 0x01);
        assert_eq!(event_log.status, 0x0008);
        assert_eq!(event_log.event_count, 264);
        assert_eq!(event_log.message_count, 288);
        assert_eq!(event_log.events.len(), 3);
    }

    #[test]
    fn test_status_flags() {
        let response = vec![
            0x01, 0x0C, 0x08,
            0x80, 0x08, // Status with busy flag
            0x00, 0x00,
            0x00, 0x00,
        ];

        let event_log = ModbusEventLog::parse_response(&response).unwrap();
        assert!(event_log.is_slave_busy());
        assert!(!event_log.is_event_log_full());
    }
}

// Main example
fn main() {
    // Build request
    let slave_address = 0x01;
    let request = ModbusEventLog::build_request(slave_address);
    
    println!("Request: {:02X?}", request);
    
    // Simulated response
    let response = vec![
        0x01,       // Slave address
        0x0C,       // Function code
        0x08,       // Byte count
        0x00, 0x08, // Status (8 bytes in log)
        0x01, 0x08, // Event count (264)
        0x01, 0x20, // Message count (288)
        0x00,       // Event: Modbus event
        0x81,       // Event: Communication restart
        0x04,       // Event: Modbus send
    ];
    
    println!("Response: {:02X?}", response);
    
    // Parse response
    match ModbusEventLog::parse_response(&response) {
        Ok(event_log) => {
            event_log.print();
            
            if event_log.is_slave_busy() {
                println!("\nWarning: Slave device is currently busy!");
            }
            
            if event_log.is_event_log_full() {
                println!("\nWarning: Event log is full!");
            }
        }
        Err(e) => eprintln!("Error parsing response: {}", e),
    }
}
```

### Advanced Rust Example with Async Support

```rust
// async_modbus_event_log.rs
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

pub struct ModbusClient {
    stream: TcpStream,
}

impl ModbusClient {
    pub async fn connect(addr: &str) -> std::io::Result<Self> {
        let stream = TcpStream::connect(addr).await?;
        Ok(ModbusClient { stream })
    }

    pub async fn get_comm_event_log(
        &mut self,
        slave_address: u8,
    ) -> Result<ModbusEventLog, Box<dyn std::error::Error>> {
        // Build request
        let request = ModbusEventLog::build_request(slave_address);
        
        // Send request
        self.stream.write_all(&request).await?;
        
        // Receive response
        let mut response = vec![0u8; 256];
        let n = self.stream.read(&mut response).await?;
        response.truncate(n);
        
        // Parse and return
        Ok(ModbusEventLog::parse_response(&response)?)
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut client = ModbusClient::connect("192.168.1.100:502").await?;
    
    let event_log = client.get_comm_event_log(0x01).await?;
    event_log.print();
    
    Ok(())
}
```

---

## Summary

**Modbus Function Code 0x0C (Get Comm Event Log)** is a powerful diagnostic tool that provides detailed insights into a slave device's communication history and status. This function is particularly valuable for:

### Key Features:
- **Comprehensive Diagnostics**: Returns status, event counts, message counts, and detailed event logs
- **Event Tracking**: Records communication events, Modbus transactions, and system state changes
- **Status Information**: Provides real-time status including busy flag and log full indicator
- **Performance Monitoring**: Message counts help track communication throughput

### Typical Applications:
1. **System Troubleshooting**: Identifying communication issues and protocol violations
2. **Performance Analysis**: Monitoring message traffic and event patterns
3. **Predictive Maintenance**: Detecting anomalies before they cause failures
4. **Compliance and Auditing**: Maintaining records of device operations

### Implementation Considerations:
- Event logs have limited capacity and may wrap around when full
- The status word provides critical information about log state
- Event interpretation requires understanding device-specific event codes
- Both C/C++ and Rust implementations demonstrate proper big-endian byte ordering
- Error handling is essential due to variable response lengths

The provided code examples demonstrate robust implementations in both C/C++ and Rust, with features including proper parsing, error handling, and comprehensive event decoding. The Rust implementation additionally shows modern async/await patterns for non-blocking I/O operations.