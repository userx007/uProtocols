# Error Detection and Recovery in Modbus

## Overview

Error detection and recovery is critical for reliable Modbus communication, especially in industrial environments with electrical noise, long cable runs, and unreliable network conditions. Modbus implements multiple layers of error detection to ensure data integrity and provides mechanisms for recovering from communication failures.

## Error Detection Mechanisms

### 1. CRC (Cyclic Redundancy Check)

Modbus RTU uses CRC-16 for error detection. The CRC is calculated over the entire message (excluding the CRC itself) and appended as the last two bytes.

**CRC-16 Algorithm Details:**
- Polynomial: 0xA001 (reversed representation of 0x8005)
- Initial value: 0xFFFF
- The CRC is transmitted with the low-order byte first, then high-order byte

### 2. Timeout Detection

Timeouts occur when:
- No response is received within the expected timeframe
- Incomplete messages are received
- Inter-character delays exceed the frame timeout (RTU: 3.5 character times)

### 3. Frame Validation

Additional checks include:
- Slave address validation
- Function code validation
- Data length verification
- Exception response handling

## Common Error Types

1. **CRC Errors**: Data corruption during transmission
2. **Timeout Errors**: Device not responding or network delays
3. **Exception Responses**: Device reports an error condition
4. **Framing Errors**: Invalid message structure
5. **Overrun/Underrun**: Buffer management issues

---

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// CRC-16 lookup table for faster calculation
static const uint16_t crc_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    // ... (full table would contain all 256 entries)
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40
};

// Calculate CRC-16 for Modbus RTU
uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; i++) {
        uint8_t index = crc ^ data[i];
        crc = (crc >> 8) ^ crc_table[index];
    }
    
    return crc;
}

// Verify CRC of received message
bool verify_crc(const uint8_t *message, size_t length) {
    if (length < 4) return false; // Minimum: slave_id + func + crc(2)
    
    uint16_t received_crc = (message[length - 1] << 8) | message[length - 2];
    uint16_t calculated_crc = calculate_crc16(message, length - 2);
    
    return received_crc == calculated_crc;
}

// Error types
typedef enum {
    MODBUS_SUCCESS = 0,
    MODBUS_ERROR_CRC = 1,
    MODBUS_ERROR_TIMEOUT = 2,
    MODBUS_ERROR_EXCEPTION = 3,
    MODBUS_ERROR_INVALID_RESPONSE = 4,
    MODBUS_ERROR_CONNECTION = 5
} modbus_error_t;

// Retry configuration
typedef struct {
    int max_retries;
    int timeout_ms;
    int retry_delay_ms;
} modbus_retry_config_t;

// Modbus context with error handling
typedef struct {
    int fd;                          // File descriptor for serial port
    modbus_retry_config_t retry_cfg;
    uint32_t crc_error_count;
    uint32_t timeout_count;
    uint32_t exception_count;
} modbus_context_t;

// Send request with retry logic
modbus_error_t modbus_send_request_with_retry(
    modbus_context_t *ctx,
    const uint8_t *request,
    size_t request_len,
    uint8_t *response,
    size_t *response_len,
    size_t max_response_len
) {
    modbus_error_t result;
    int retry_count = 0;
    
    while (retry_count <= ctx->retry_cfg.max_retries) {
        // Send request
        ssize_t sent = write(ctx->fd, request, request_len);
        if (sent != (ssize_t)request_len) {
            return MODBUS_ERROR_CONNECTION;
        }
        
        // Wait for response with timeout
        fd_set read_fds;
        struct timeval timeout;
        timeout.tv_sec = ctx->retry_cfg.timeout_ms / 1000;
        timeout.tv_usec = (ctx->retry_cfg.timeout_ms % 1000) * 1000;
        
        FD_ZERO(&read_fds);
        FD_SET(ctx->fd, &read_fds);
        
        int select_result = select(ctx->fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (select_result == 0) {
            // Timeout occurred
            ctx->timeout_count++;
            result = MODBUS_ERROR_TIMEOUT;
            
            if (retry_count < ctx->retry_cfg.max_retries) {
                printf("Timeout on attempt %d, retrying...\n", retry_count + 1);
                usleep(ctx->retry_cfg.retry_delay_ms * 1000);
                retry_count++;
                continue;
            }
            return result;
        }
        
        if (select_result < 0) {
            return MODBUS_ERROR_CONNECTION;
        }
        
        // Read response
        ssize_t bytes_read = read(ctx->fd, response, max_response_len);
        if (bytes_read < 0) {
            return MODBUS_ERROR_CONNECTION;
        }
        
        *response_len = bytes_read;
        
        // Verify CRC
        if (!verify_crc(response, *response_len)) {
            ctx->crc_error_count++;
            result = MODBUS_ERROR_CRC;
            
            if (retry_count < ctx->retry_cfg.max_retries) {
                printf("CRC error on attempt %d, retrying...\n", retry_count + 1);
                usleep(ctx->retry_cfg.retry_delay_ms * 1000);
                retry_count++;
                continue;
            }
            return result;
        }
        
        // Check for exception response
        if (response[1] & 0x80) {
            ctx->exception_count++;
            printf("Modbus exception: 0x%02X\n", response[2]);
            return MODBUS_ERROR_EXCEPTION;
        }
        
        // Success
        return MODBUS_SUCCESS;
    }
    
    return result;
}

// Read holding registers with full error handling
modbus_error_t modbus_read_holding_registers(
    modbus_context_t *ctx,
    uint8_t slave_id,
    uint16_t start_address,
    uint16_t quantity,
    uint16_t *values
) {
    uint8_t request[8];
    uint8_t response[256];
    size_t response_len;
    
    // Build request
    request[0] = slave_id;
    request[1] = 0x03; // Read Holding Registers
    request[2] = (start_address >> 8) & 0xFF;
    request[3] = start_address & 0xFF;
    request[4] = (quantity >> 8) & 0xFF;
    request[5] = quantity & 0xFF;
    
    // Calculate and append CRC
    uint16_t crc = calculate_crc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    // Send with retry
    modbus_error_t result = modbus_send_request_with_retry(
        ctx, request, 8, response, &response_len, sizeof(response)
    );
    
    if (result != MODBUS_SUCCESS) {
        return result;
    }
    
    // Parse response
    uint8_t byte_count = response[2];
    if (byte_count != quantity * 2) {
        return MODBUS_ERROR_INVALID_RESPONSE;
    }
    
    for (uint16_t i = 0; i < quantity; i++) {
        values[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
    }
    
    return MODBUS_SUCCESS;
}

// Example usage
int main() {
    modbus_context_t ctx = {
        .fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY),
        .retry_cfg = {
            .max_retries = 3,
            .timeout_ms = 1000,
            .retry_delay_ms = 100
        },
        .crc_error_count = 0,
        .timeout_count = 0,
        .exception_count = 0
    };
    
    uint16_t registers[10];
    modbus_error_t result = modbus_read_holding_registers(&ctx, 1, 0, 10, registers);
    
    if (result == MODBUS_SUCCESS) {
        printf("Successfully read registers\n");
    } else {
        printf("Error: %d\n", result);
    }
    
    printf("Statistics - CRC errors: %u, Timeouts: %u, Exceptions: %u\n",
           ctx.crc_error_count, ctx.timeout_count, ctx.exception_count);
    
    close(ctx.fd);
    return 0;
}
```

---

## Rust Implementation

```rust
use std::io::{self, Read, Write};
use std::time::{Duration, Instant};
use serialport::SerialPort;

// CRC-16 calculation for Modbus
fn calculate_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    
    for byte in data {
        crc ^= *byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    crc
}

// Verify CRC of received message
fn verify_crc(message: &[u8]) -> bool {
    if message.len() < 4 {
        return false;
    }
    
    let received_crc = u16::from_le_bytes([
        message[message.len() - 2],
        message[message.len() - 1]
    ]);
    
    let calculated_crc = calculate_crc16(&message[..message.len() - 2]);
    
    received_crc == calculated_crc
}

// Error types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ModbusError {
    CrcError,
    Timeout,
    Exception(u8),
    InvalidResponse,
    IoError,
    ConnectionError,
}

impl std::fmt::Display for ModbusError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            ModbusError::CrcError => write!(f, "CRC verification failed"),
            ModbusError::Timeout => write!(f, "Request timeout"),
            ModbusError::Exception(code) => write!(f, "Modbus exception: 0x{:02X}", code),
            ModbusError::InvalidResponse => write!(f, "Invalid response received"),
            ModbusError::IoError => write!(f, "I/O error"),
            ModbusError::ConnectionError => write!(f, "Connection error"),
        }
    }
}

impl std::error::Error for ModbusError {}

// Retry configuration
#[derive(Debug, Clone)]
pub struct RetryConfig {
    pub max_retries: u32,
    pub timeout: Duration,
    pub retry_delay: Duration,
}

impl Default for RetryConfig {
    fn default() -> Self {
        Self {
            max_retries: 3,
            timeout: Duration::from_millis(1000),
            retry_delay: Duration::from_millis(100),
        }
    }
}

// Statistics tracking
#[derive(Debug, Default)]
pub struct ModbusStatistics {
    pub crc_errors: u32,
    pub timeouts: u32,
    pub exceptions: u32,
    pub successful_requests: u32,
}

// Modbus context with error handling
pub struct ModbusContext {
    port: Box<dyn SerialPort>,
    retry_config: RetryConfig,
    pub statistics: ModbusStatistics,
}

impl ModbusContext {
    pub fn new(port: Box<dyn SerialPort>, retry_config: RetryConfig) -> Self {
        Self {
            port,
            retry_config,
            statistics: ModbusStatistics::default(),
        }
    }
    
    // Send request with automatic retry
    fn send_request_with_retry(
        &mut self,
        request: &[u8],
        max_response_len: usize,
    ) -> Result<Vec<u8>, ModbusError> {
        let mut retry_count = 0;
        
        loop {
            // Send request
            self.port.write_all(request)
                .map_err(|_| ModbusError::ConnectionError)?;
            
            self.port.flush()
                .map_err(|_| ModbusError::ConnectionError)?;
            
            // Set read timeout
            self.port.set_timeout(self.retry_config.timeout)
                .map_err(|_| ModbusError::ConnectionError)?;
            
            // Read response
            let mut response = vec![0u8; max_response_len];
            let start_time = Instant::now();
            let mut total_read = 0;
            
            while total_read < 4 && start_time.elapsed() < self.retry_config.timeout {
                match self.port.read(&mut response[total_read..]) {
                    Ok(n) if n > 0 => {
                        total_read += n;
                        
                        // Check if we have a complete message
                        if total_read >= 5 {
                            let expected_len = if response[1] & 0x80 != 0 {
                                5 // Exception response
                            } else {
                                match response[1] {
                                    0x03 | 0x04 => 5 + response[2] as usize,
                                    0x06 | 0x10 => 8,
                                    _ => 8,
                                }
                            };
                            
                            if total_read >= expected_len {
                                response.truncate(expected_len);
                                break;
                            }
                        }
                    }
                    Ok(_) => continue,
                    Err(ref e) if e.kind() == io::ErrorKind::TimedOut => break,
                    Err(_) => {
                        if retry_count < self.retry_config.max_retries {
                            retry_count += 1;
                            std::thread::sleep(self.retry_config.retry_delay);
                            continue;
                        }
                        return Err(ModbusError::IoError);
                    }
                }
            }
            
            if total_read < 5 {
                self.statistics.timeouts += 1;
                
                if retry_count < self.retry_config.max_retries {
                    println!("Timeout on attempt {}, retrying...", retry_count + 1);
                    retry_count += 1;
                    std::thread::sleep(self.retry_config.retry_delay);
                    continue;
                }
                return Err(ModbusError::Timeout);
            }
            
            response.truncate(total_read);
            
            // Verify CRC
            if !verify_crc(&response) {
                self.statistics.crc_errors += 1;
                
                if retry_count < self.retry_config.max_retries {
                    println!("CRC error on attempt {}, retrying...", retry_count + 1);
                    retry_count += 1;
                    std::thread::sleep(self.retry_config.retry_delay);
                    continue;
                }
                return Err(ModbusError::CrcError);
            }
            
            // Check for exception response
            if response[1] & 0x80 != 0 {
                self.statistics.exceptions += 1;
                return Err(ModbusError::Exception(response[2]));
            }
            
            // Success
            self.statistics.successful_requests += 1;
            return Ok(response);
        }
    }
    
    // Read holding registers with full error handling
    pub fn read_holding_registers(
        &mut self,
        slave_id: u8,
        start_address: u16,
        quantity: u16,
    ) -> Result<Vec<u16>, ModbusError> {
        // Build request
        let mut request = vec![
            slave_id,
            0x03, // Function code: Read Holding Registers
            (start_address >> 8) as u8,
            (start_address & 0xFF) as u8,
            (quantity >> 8) as u8,
            (quantity & 0xFF) as u8,
        ];
        
        // Calculate and append CRC
        let crc = calculate_crc16(&request);
        request.push((crc & 0xFF) as u8);
        request.push((crc >> 8) as u8);
        
        // Send with retry
        let response = self.send_request_with_retry(&request, 256)?;
        
        // Parse response
        if response.len() < 5 {
            return Err(ModbusError::InvalidResponse);
        }
        
        let byte_count = response[2] as usize;
        if byte_count != quantity as usize * 2 || response.len() < 3 + byte_count + 2 {
            return Err(ModbusError::InvalidResponse);
        }
        
        let mut values = Vec::with_capacity(quantity as usize);
        for i in 0..quantity as usize {
            let high = response[3 + i * 2] as u16;
            let low = response[4 + i * 2] as u16;
            values.push((high << 8) | low);
        }
        
        Ok(values)
    }
    
    pub fn print_statistics(&self) {
        println!("Modbus Statistics:");
        println!("  Successful requests: {}", self.statistics.successful_requests);
        println!("  CRC errors: {}", self.statistics.crc_errors);
        println!("  Timeouts: {}", self.statistics.timeouts);
        println!("  Exceptions: {}", self.statistics.exceptions);
    }
}

// Example usage
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let port = serialport::new("/dev/ttyUSB0", 9600)
        .timeout(Duration::from_millis(1000))
        .open()?;
    
    let retry_config = RetryConfig {
        max_retries: 3,
        timeout: Duration::from_millis(1000),
        retry_delay: Duration::from_millis(100),
    };
    
    let mut ctx = ModbusContext::new(port, retry_config);
    
    match ctx.read_holding_registers(1, 0, 10) {
        Ok(registers) => {
            println!("Successfully read {} registers:", registers.len());
            for (i, value) in registers.iter().enumerate() {
                println!("  Register {}: {}", i, value);
            }
        }
        Err(e) => {
            eprintln!("Error reading registers: {}", e);
        }
    }
    
    ctx.print_statistics();
    
    Ok(())
}
```

---

## Summary

**Error Detection and Recovery** in Modbus is essential for maintaining reliable communication in industrial environments. The key components include:

1. **CRC-16 Verification**: Detects data corruption using polynomial-based checksums, ensuring message integrity across noisy communication channels.

2. **Timeout Management**: Handles unresponsive devices by implementing configurable timeouts and detecting incomplete transmissions.

3. **Automatic Retry Logic**: Recovers from transient errors by retrying failed requests with configurable delays and maximum attempt limits.

4. **Exception Handling**: Properly processes Modbus exception responses that indicate device-side errors or invalid requests.

5. **Statistics Tracking**: Monitors communication health by counting errors, helping diagnose persistent problems and optimize retry strategies.

Both implementations demonstrate production-ready patterns: the C version uses POSIX select() for timeouts and manual buffer management, while the Rust version leverages the serialport crate with type-safe error handling. Both include configurable retry logic, comprehensive error classification, and statistical monitoring. These patterns ensure robust operation in challenging industrial environments where communication failures are inevitable but must be handled gracefully without data loss or system instability.