# SPI Command-Response Protocols: Detailed Guide

## Introduction

Command-response protocols over SPI establish structured, predictable communication patterns between master and slave devices. Unlike simple data transfers, these protocols define how commands are formatted, transmitted, acknowledged, and how responses are structured and validated. This approach is essential for reliable communication with complex peripherals like displays, sensors, flash memory, and ADCs.

## Core Concepts

### Protocol Structure

A typical command-response protocol consists of:

1. **Command Phase**: Master sends a command byte or sequence
2. **Address/Parameter Phase**: Optional addressing or parameter data
3. **Response Phase**: Slave provides status, acknowledgment, or data
4. **Data Phase**: Bulk data transfer if needed

### Key Design Considerations

- **Command framing**: Clear start/end delimiters or fixed-length commands
- **Error detection**: Checksums, CRCs, or parity bits
- **Flow control**: Handling device busy states and timing requirements
- **State management**: Tracking protocol state across transactions
- **Timeout handling**: Recovering from failed communications

## C/C++ Implementation

Here's a comprehensive implementation of a command-response protocol framework:

```c
// spi_protocol.h
#ifndef SPI_PROTOCOL_H
#define SPI_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

// Protocol definitions
#define SPI_CMD_READ_REG        0x01
#define SPI_CMD_WRITE_REG       0x02
#define SPI_CMD_READ_BUFFER     0x03
#define SPI_CMD_WRITE_BUFFER    0x04
#define SPI_CMD_GET_STATUS      0x05
#define SPI_CMD_RESET           0x06

// Status codes
#define SPI_STATUS_OK           0x00
#define SPI_STATUS_BUSY         0x01
#define SPI_STATUS_ERROR        0x02
#define SPI_STATUS_INVALID_CMD  0x03
#define SPI_STATUS_CRC_ERROR    0x04

// Protocol constants
#define SPI_MAX_PAYLOAD         256
#define SPI_TIMEOUT_MS          1000
#define SPI_DUMMY_BYTE          0xFF

// Command structure
typedef struct {
    uint8_t cmd;
    uint8_t addr;
    uint16_t length;
    uint8_t checksum;
} __attribute__((packed)) spi_command_t;

// Response structure
typedef struct {
    uint8_t status;
    uint16_t data_length;
    uint8_t checksum;
} __attribute__((packed)) spi_response_t;

// Protocol state
typedef enum {
    SPI_STATE_IDLE,
    SPI_STATE_CMD_SENT,
    SPI_STATE_WAITING_RESPONSE,
    SPI_STATE_DATA_TRANSFER,
    SPI_STATE_ERROR
} spi_protocol_state_t;

// Protocol context
typedef struct {
    spi_protocol_state_t state;
    uint32_t timeout_ms;
    uint32_t last_transaction_time;
    uint8_t retry_count;
    uint8_t max_retries;
} spi_protocol_ctx_t;

// Function prototypes
void spi_protocol_init(spi_protocol_ctx_t *ctx);
uint8_t spi_calculate_checksum(const uint8_t *data, size_t len);
bool spi_verify_checksum(const uint8_t *data, size_t len, uint8_t checksum);
int spi_send_command(spi_protocol_ctx_t *ctx, const spi_command_t *cmd);
int spi_receive_response(spi_protocol_ctx_t *ctx, spi_response_t *resp);
int spi_read_register(spi_protocol_ctx_t *ctx, uint8_t addr, uint8_t *value);
int spi_write_register(spi_protocol_ctx_t *ctx, uint8_t addr, uint8_t value);
int spi_read_buffer(spi_protocol_ctx_t *ctx, uint8_t addr, 
                    uint8_t *buffer, uint16_t length);
int spi_write_buffer(spi_protocol_ctx_t *ctx, uint8_t addr,
                     const uint8_t *buffer, uint16_t length);

#endif // SPI_PROTOCOL_H
```

```c
// spi_protocol.c
#include "spi_protocol.h"
#include <string.h>

// Low-level SPI functions (platform-specific)
extern void spi_cs_low(void);
extern void spi_cs_high(void);
extern uint8_t spi_transfer_byte(uint8_t data);
extern void spi_transfer_buffer(const uint8_t *tx, uint8_t *rx, size_t len);
extern uint32_t get_tick_ms(void);
extern void delay_ms(uint32_t ms);

void spi_protocol_init(spi_protocol_ctx_t *ctx) {
    ctx->state = SPI_STATE_IDLE;
    ctx->timeout_ms = SPI_TIMEOUT_MS;
    ctx->last_transaction_time = 0;
    ctx->retry_count = 0;
    ctx->max_retries = 3;
}

// Simple checksum calculation (XOR-based)
uint8_t spi_calculate_checksum(const uint8_t *data, size_t len) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

bool spi_verify_checksum(const uint8_t *data, size_t len, uint8_t checksum) {
    return (spi_calculate_checksum(data, len) == checksum);
}

int spi_send_command(spi_protocol_ctx_t *ctx, const spi_command_t *cmd) {
    if (ctx->state != SPI_STATE_IDLE) {
        return -1; // Protocol not in idle state
    }
    
    uint8_t tx_buffer[sizeof(spi_command_t)];
    memcpy(tx_buffer, cmd, sizeof(spi_command_t));
    
    // Calculate checksum
    tx_buffer[sizeof(spi_command_t) - 1] = 
        spi_calculate_checksum(tx_buffer, sizeof(spi_command_t) - 1);
    
    spi_cs_low();
    spi_transfer_buffer(tx_buffer, NULL, sizeof(spi_command_t));
    
    ctx->state = SPI_STATE_CMD_SENT;
    ctx->last_transaction_time = get_tick_ms();
    
    return 0;
}

int spi_receive_response(spi_protocol_ctx_t *ctx, spi_response_t *resp) {
    if (ctx->state != SPI_STATE_CMD_SENT) {
        return -1;
    }
    
    uint8_t rx_buffer[sizeof(spi_response_t)];
    uint32_t start_time = get_tick_ms();
    
    // Wait for slave to be ready (poll status byte)
    while (get_tick_ms() - start_time < ctx->timeout_ms) {
        uint8_t status = spi_transfer_byte(SPI_DUMMY_BYTE);
        if (status != SPI_STATUS_BUSY) {
            // Read rest of response
            rx_buffer[0] = status;
            for (size_t i = 1; i < sizeof(spi_response_t); i++) {
                rx_buffer[i] = spi_transfer_byte(SPI_DUMMY_BYTE);
            }
            
            spi_cs_high();
            
            // Verify checksum
            if (!spi_verify_checksum(rx_buffer, sizeof(spi_response_t) - 1,
                                     rx_buffer[sizeof(spi_response_t) - 1])) {
                ctx->state = SPI_STATE_ERROR;
                return -2; // Checksum error
            }
            
            memcpy(resp, rx_buffer, sizeof(spi_response_t));
            ctx->state = SPI_STATE_IDLE;
            return 0;
        }
        delay_ms(1);
    }
    
    spi_cs_high();
    ctx->state = SPI_STATE_ERROR;
    return -3; // Timeout
}

int spi_read_register(spi_protocol_ctx_t *ctx, uint8_t addr, uint8_t *value) {
    spi_command_t cmd = {
        .cmd = SPI_CMD_READ_REG,
        .addr = addr,
        .length = 1,
        .checksum = 0
    };
    
    for (uint8_t retry = 0; retry <= ctx->max_retries; retry++) {
        if (spi_send_command(ctx, &cmd) != 0) {
            continue;
        }
        
        spi_response_t resp;
        if (spi_receive_response(ctx, &resp) != 0) {
            ctx->state = SPI_STATE_IDLE; // Reset for retry
            continue;
        }
        
        if (resp.status != SPI_STATUS_OK) {
            return -1;
        }
        
        // Read data byte
        spi_cs_low();
        *value = spi_transfer_byte(SPI_DUMMY_BYTE);
        spi_cs_high();
        
        return 0;
    }
    
    return -4; // Max retries exceeded
}

int spi_write_register(spi_protocol_ctx_t *ctx, uint8_t addr, uint8_t value) {
    spi_command_t cmd = {
        .cmd = SPI_CMD_WRITE_REG,
        .addr = addr,
        .length = 1,
        .checksum = 0
    };
    
    for (uint8_t retry = 0; retry <= ctx->max_retries; retry++) {
        if (spi_send_command(ctx, &cmd) != 0) {
            continue;
        }
        
        // Send data byte
        spi_transfer_byte(value);
        spi_cs_high();
        
        ctx->state = SPI_STATE_CMD_SENT;
        
        spi_response_t resp;
        if (spi_receive_response(ctx, &resp) != 0) {
            ctx->state = SPI_STATE_IDLE;
            continue;
        }
        
        if (resp.status == SPI_STATUS_OK) {
            return 0;
        }
    }
    
    return -1;
}

int spi_read_buffer(spi_protocol_ctx_t *ctx, uint8_t addr,
                    uint8_t *buffer, uint16_t length) {
    if (length > SPI_MAX_PAYLOAD) {
        return -1;
    }
    
    spi_command_t cmd = {
        .cmd = SPI_CMD_READ_BUFFER,
        .addr = addr,
        .length = length,
        .checksum = 0
    };
    
    if (spi_send_command(ctx, &cmd) != 0) {
        return -1;
    }
    
    spi_response_t resp;
    if (spi_receive_response(ctx, &resp) != 0) {
        return -1;
    }
    
    if (resp.status != SPI_STATUS_OK || resp.data_length != length) {
        return -1;
    }
    
    // Read data
    spi_cs_low();
    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = spi_transfer_byte(SPI_DUMMY_BYTE);
    }
    
    // Read and verify data checksum
    uint8_t data_checksum = spi_transfer_byte(SPI_DUMMY_BYTE);
    spi_cs_high();
    
    if (!spi_verify_checksum(buffer, length, data_checksum)) {
        return -2;
    }
    
    return 0;
}

int spi_write_buffer(spi_protocol_ctx_t *ctx, uint8_t addr,
                     const uint8_t *buffer, uint16_t length) {
    if (length > SPI_MAX_PAYLOAD) {
        return -1;
    }
    
    spi_command_t cmd = {
        .cmd = SPI_CMD_WRITE_BUFFER,
        .addr = addr,
        .length = length,
        .checksum = 0
    };
    
    if (spi_send_command(ctx, &cmd) != 0) {
        return -1;
    }
    
    // Send data
    for (uint16_t i = 0; i < length; i++) {
        spi_transfer_byte(buffer[i]);
    }
    
    // Send data checksum
    uint8_t data_checksum = spi_calculate_checksum(buffer, length);
    spi_transfer_byte(data_checksum);
    spi_cs_high();
    
    ctx->state = SPI_STATE_CMD_SENT;
    
    spi_response_t resp;
    if (spi_receive_response(ctx, &resp) != 0) {
        return -1;
    }
    
    return (resp.status == SPI_STATUS_OK) ? 0 : -1;
}
```

```cpp
// Example usage
#include "spi_protocol.h"
#include <stdio.h>

int main() {
    spi_protocol_ctx_t spi_ctx;
    spi_protocol_init(&spi_ctx);
    
    // Read a register
    uint8_t reg_value;
    if (spi_read_register(&spi_ctx, 0x10, &reg_value) == 0) {
        printf("Register 0x10 value: 0x%02X\n", reg_value);
    } else {
        printf("Failed to read register\n");
    }
    
    // Write a register
    if (spi_write_register(&spi_ctx, 0x20, 0xAB) == 0) {
        printf("Successfully wrote to register 0x20\n");
    }
    
    // Read buffer
    uint8_t rx_buffer[64];
    if (spi_read_buffer(&spi_ctx, 0x00, rx_buffer, 64) == 0) {
        printf("Read 64 bytes from buffer\n");
    }
    
    // Write buffer
    uint8_t tx_buffer[32] = {0x01, 0x02, 0x03, /* ... */};
    if (spi_write_buffer(&spi_ctx, 0x00, tx_buffer, 32) == 0) {
        printf("Wrote 32 bytes to buffer\n");
    }
    
    return 0;
}
```

## Rust Implementation

Here's a safe, idiomatic Rust implementation with comprehensive error handling:

```rust
// spi_protocol.rs
use std::time::{Duration, Instant};

// Command definitions
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpiCommand {
    ReadRegister = 0x01,
    WriteRegister = 0x02,
    ReadBuffer = 0x03,
    WriteBuffer = 0x04,
    GetStatus = 0x05,
    Reset = 0x06,
}

// Status codes
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SpiStatus {
    Ok = 0x00,
    Busy = 0x01,
    Error = 0x02,
    InvalidCommand = 0x03,
    CrcError = 0x04,
}

impl TryFrom<u8> for SpiStatus {
    type Error = ProtocolError;
    
    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0x00 => Ok(SpiStatus::Ok),
            0x01 => Ok(SpiStatus::Busy),
            0x02 => Ok(SpiStatus::Error),
            0x03 => Ok(SpiStatus::InvalidCommand),
            0x04 => Ok(SpiStatus::CrcError),
            _ => Err(ProtocolError::InvalidStatus(value)),
        }
    }
}

// Error types
#[derive(Debug)]
pub enum ProtocolError {
    InvalidState,
    ChecksumMismatch,
    Timeout,
    MaxRetriesExceeded,
    InvalidStatus(u8),
    DeviceError(SpiStatus),
    BufferTooLarge,
    SpiError,
}

// Command packet structure
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct CommandPacket {
    cmd: u8,
    addr: u8,
    length: u16,
    checksum: u8,
}

impl CommandPacket {
    fn new(cmd: SpiCommand, addr: u8, length: u16) -> Self {
        let mut packet = Self {
            cmd: cmd as u8,
            addr,
            length,
            checksum: 0,
        };
        packet.checksum = Self::calculate_checksum(&packet);
        packet
    }
    
    fn calculate_checksum(packet: &Self) -> u8 {
        let bytes = unsafe {
            std::slice::from_raw_parts(
                packet as *const Self as *const u8,
                std::mem::size_of::<Self>() - 1
            )
        };
        bytes.iter().fold(0u8, |acc, &b| acc ^ b)
    }
    
    fn as_bytes(&self) -> &[u8] {
        unsafe {
            std::slice::from_raw_parts(
                self as *const Self as *const u8,
                std::mem::size_of::<Self>()
            )
        }
    }
}

// Response packet structure
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct ResponsePacket {
    status: u8,
    data_length: u16,
    checksum: u8,
}

impl ResponsePacket {
    fn from_bytes(bytes: &[u8]) -> Result<Self, ProtocolError> {
        if bytes.len() != std::mem::size_of::<Self>() {
            return Err(ProtocolError::SpiError);
        }
        
        let packet = unsafe {
            std::ptr::read_unaligned(bytes.as_ptr() as *const Self)
        };
        
        // Verify checksum
        let calculated = Self::calculate_checksum(&packet);
        if calculated != packet.checksum {
            return Err(ProtocolError::ChecksumMismatch);
        }
        
        Ok(packet)
    }
    
    fn calculate_checksum(packet: &Self) -> u8 {
        let bytes = unsafe {
            std::slice::from_raw_parts(
                packet as *const Self as *const u8,
                std::mem::size_of::<Self>() - 1
            )
        };
        bytes.iter().fold(0u8, |acc, &b| acc ^ b)
    }
}

// Protocol state
#[derive(Debug, PartialEq)]
enum ProtocolState {
    Idle,
    CommandSent,
    WaitingResponse,
    DataTransfer,
    Error,
}

// SPI interface trait
pub trait SpiInterface {
    fn cs_low(&mut self);
    fn cs_high(&mut self);
    fn transfer_byte(&mut self, data: u8) -> u8;
    fn transfer(&mut self, tx: &[u8], rx: &mut [u8]);
}

// Protocol handler
pub struct SpiProtocol<S: SpiInterface> {
    spi: S,
    state: ProtocolState,
    timeout: Duration,
    max_retries: u8,
}

impl<S: SpiInterface> SpiProtocol<S> {
    pub fn new(spi: S) -> Self {
        Self {
            spi,
            state: ProtocolState::Idle,
            timeout: Duration::from_millis(1000),
            max_retries: 3,
        }
    }
    
    pub fn set_timeout(&mut self, timeout: Duration) {
        self.timeout = timeout;
    }
    
    fn calculate_data_checksum(data: &[u8]) -> u8 {
        data.iter().fold(0u8, |acc, &b| acc ^ b)
    }
    
    fn send_command(&mut self, packet: &CommandPacket) -> Result<(), ProtocolError> {
        if self.state != ProtocolState::Idle {
            return Err(ProtocolError::InvalidState);
        }
        
        self.spi.cs_low();
        
        let mut dummy = vec![0u8; packet.as_bytes().len()];
        self.spi.transfer(packet.as_bytes(), &mut dummy);
        
        self.state = ProtocolState::CommandSent;
        Ok(())
    }
    
    fn receive_response(&mut self) -> Result<ResponsePacket, ProtocolError> {
        if self.state != ProtocolState::CommandSent {
            return Err(ProtocolError::InvalidState);
        }
        
        let start = Instant::now();
        let dummy_byte = 0xFF;
        
        // Poll for non-busy status
        loop {
            if start.elapsed() > self.timeout {
                self.spi.cs_high();
                self.state = ProtocolState::Error;
                return Err(ProtocolError::Timeout);
            }
            
            let status_byte = self.spi.transfer_byte(dummy_byte);
            let status = SpiStatus::try_from(status_byte)?;
            
            if status != SpiStatus::Busy {
                // Read rest of response
                let mut rx_buffer = vec![0u8; std::mem::size_of::<ResponsePacket>()];
                rx_buffer[0] = status_byte;
                
                for i in 1..rx_buffer.len() {
                    rx_buffer[i] = self.spi.transfer_byte(dummy_byte);
                }
                
                self.spi.cs_high();
                self.state = ProtocolState::Idle;
                
                return ResponsePacket::from_bytes(&rx_buffer);
            }
            
            std::thread::sleep(Duration::from_millis(1));
        }
    }
    
    pub fn read_register(&mut self, addr: u8) -> Result<u8, ProtocolError> {
        for _ in 0..=self.max_retries {
            let cmd = CommandPacket::new(SpiCommand::ReadRegister, addr, 1);
            
            if let Err(_) = self.send_command(&cmd) {
                self.state = ProtocolState::Idle;
                continue;
            }
            
            match self.receive_response() {
                Ok(resp) => {
                    let status = SpiStatus::try_from(resp.status)?;
                    if status != SpiStatus::Ok {
                        return Err(ProtocolError::DeviceError(status));
                    }
                    
                    self.spi.cs_low();
                    let value = self.spi.transfer_byte(0xFF);
                    self.spi.cs_high();
                    
                    return Ok(value);
                }
                Err(_) => {
                    self.state = ProtocolState::Idle;
                    continue;
                }
            }
        }
        
        Err(ProtocolError::MaxRetriesExceeded)
    }
    
    pub fn write_register(&mut self, addr: u8, value: u8) -> Result<(), ProtocolError> {
        for _ in 0..=self.max_retries {
            let cmd = CommandPacket::new(SpiCommand::WriteRegister, addr, 1);
            
            if let Err(_) = self.send_command(&cmd) {
                self.state = ProtocolState::Idle;
                continue;
            }
            
            self.spi.transfer_byte(value);
            self.spi.cs_high();
            
            self.state = ProtocolState::CommandSent;
            
            match self.receive_response() {
                Ok(resp) => {
                    let status = SpiStatus::try_from(resp.status)?;
                    if status == SpiStatus::Ok {
                        return Ok(());
                    }
                    return Err(ProtocolError::DeviceError(status));
                }
                Err(_) => {
                    self.state = ProtocolState::Idle;
                    continue;
                }
            }
        }
        
        Err(ProtocolError::MaxRetriesExceeded)
    }
    
    pub fn read_buffer(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), ProtocolError> {
        if buffer.len() > 256 {
            return Err(ProtocolError::BufferTooLarge);
        }
        
        let cmd = CommandPacket::new(SpiCommand::ReadBuffer, addr, buffer.len() as u16);
        self.send_command(&cmd)?;
        
        let resp = self.receive_response()?;
        let status = SpiStatus::try_from(resp.status)?;
        
        if status != SpiStatus::Ok || resp.data_length != buffer.len() as u16 {
            return Err(ProtocolError::DeviceError(status));
        }
        
        self.spi.cs_low();
        for byte in buffer.iter_mut() {
            *byte = self.spi.transfer_byte(0xFF);
        }
        
        let received_checksum = self.spi.transfer_byte(0xFF);
        self.spi.cs_high();
        
        let calculated_checksum = Self::calculate_data_checksum(buffer);
        if calculated_checksum != received_checksum {
            return Err(ProtocolError::ChecksumMismatch);
        }
        
        Ok(())
    }
    
    pub fn write_buffer(&mut self, addr: u8, buffer: &[u8]) -> Result<(), ProtocolError> {
        if buffer.len() > 256 {
            return Err(ProtocolError::BufferTooLarge);
        }
        
        let cmd = CommandPacket::new(SpiCommand::WriteBuffer, addr, buffer.len() as u16);
        self.send_command(&cmd)?;
        
        for &byte in buffer {
            self.spi.transfer_byte(byte);
        }
        
        let checksum = Self::calculate_data_checksum(buffer);
        self.spi.transfer_byte(checksum);
        self.spi.cs_high();
        
        self.state = ProtocolState::CommandSent;
        
        let resp = self.receive_response()?;
        let status = SpiStatus::try_from(resp.status)?;
        
        if status == SpiStatus::Ok {
            Ok(())
        } else {
            Err(ProtocolError::DeviceError(status))
        }
    }
}

// Example usage
#[cfg(test)]
mod tests {
    use super::*;
    
    struct MockSpi {
        responses: Vec<u8>,
        index: usize,
    }
    
    impl MockSpi {
        fn new(responses: Vec<u8>) -> Self {
            Self { responses, index: 0 }
        }
    }
    
    impl SpiInterface for MockSpi {
        fn cs_low(&mut self) {}
        fn cs_high(&mut self) {}
        
        fn transfer_byte(&mut self, _data: u8) -> u8 {
            if self.index < self.responses.len() {
                let byte = self.responses[self.index];
                self.index += 1;
                byte
            } else {
                0xFF
            }
        }
        
        fn transfer(&mut self, _tx: &[u8], rx: &mut [u8]) {
            for byte in rx {
                *byte = self.transfer_byte(0);
            }
        }
    }
    
    #[test]
    fn test_read_register() {
        let mock_spi = MockSpi::new(vec![
            0x00, 0x01, 0x00, 0x00, // Response: OK, length=1
            0xAB, // Register value
        ]);
        
        let mut protocol = SpiProtocol::new(mock_spi);
        let value = protocol.read_register(0x10).unwrap();
        assert_eq!(value, 0xAB);
    }
}
```

## Summary

**Command-response protocols over SPI** provide structured, reliable communication with peripheral devices through well-defined message exchanges. Key aspects include:

**Core Components:**
- Command packets with addressing and parameter data
- Response packets with status codes and error indicators
- Checksum/CRC validation for data integrity
- State machine management for protocol flow
- Timeout and retry mechanisms for robustness

**Implementation Features:**
- The C/C++ implementation demonstrates low-level control with packed structures and direct hardware access, suitable for embedded systems
- The Rust implementation provides type safety, comprehensive error handling through Result types, and memory safety guarantees
- Both implementations include retry logic, checksum validation, and structured error reporting

**Best Practices:**
- Use fixed-length command structures for predictable timing
- Implement checksums or CRCs for all data transfers
- Include status polling to handle slave device busy states
- Design with timeout and retry mechanisms for production robustness
- Maintain clear state machines to track protocol phases
- Provide comprehensive error codes for debugging

This protocol pattern is essential for communicating with complex SPI peripherals like sensor ICs, flash memory, displays, and data acquisition systems where reliable command execution and data validation are critical.