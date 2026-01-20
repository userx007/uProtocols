# SPI Data Validation

## Overview

Data validation in SPI (Serial Peripheral Interface) communication is critical for ensuring the integrity and reliability of transmitted data between master and slave devices. Unlike protocols like I2C or UART that have built-in error detection, SPI has no inherent error checking mechanism. Therefore, applications must implement their own validation strategies.

## Key Concepts

### Why Data Validation Matters in SPI

1. **No Built-in Error Detection**: SPI transmits data without parity bits or automatic error checking
2. **Electrical Noise**: Signal integrity issues can corrupt data on MISO/MOSI lines
3. **Timing Issues**: Clock skew or setup/hold violations can cause bit errors
4. **Critical Applications**: Sensor data, flash memory operations, and control systems require verified data

### Common Validation Techniques

1. **Checksums**: Simple arithmetic sum or XOR-based verification
2. **CRC (Cyclic Redundancy Check)**: Polynomial-based error detection
3. **Acknowledgments**: Bidirectional confirmation of successful transmission
4. **Redundant Transmission**: Sending data multiple times for comparison
5. **Register Readback**: Writing then reading back for verification

## Implementation Approaches

### 1. Checksum Validation

The simplest approach adds all bytes and transmits the sum as verification.

**Advantages**: Low overhead, easy to implement
**Disadvantages**: Doesn't detect certain multi-bit errors or reordered bytes

### 2. CRC Validation

More robust polynomial-based checking that detects burst errors and most common corruption patterns.

**Common CRC Variants**:
- CRC-8: Single byte, good for short messages
- CRC-16: Two bytes, standard for many protocols
- CRC-32: Four bytes, highest reliability

### 3. Acknowledgment Protocol

After receiving data, the slave sends back an ACK/NAK byte to confirm or reject the transmission.

## C Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// SPI hardware abstraction (platform-specific)
extern void spi_transmit(uint8_t data);
extern uint8_t spi_receive(void);
extern void spi_select(void);
extern void spi_deselect(void);

// ============================================
// Checksum-based validation
// ============================================

uint8_t calculate_checksum(const uint8_t *data, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum += data[i];
    }
    return checksum;
}

bool spi_send_with_checksum(const uint8_t *data, size_t length) {
    spi_select();
    
    // Send length
    spi_transmit((uint8_t)length);
    
    // Send data
    for (size_t i = 0; i < length; i++) {
        spi_transmit(data[i]);
    }
    
    // Calculate and send checksum
    uint8_t checksum = calculate_checksum(data, length);
    spi_transmit(checksum);
    
    // Wait for acknowledgment
    uint8_t ack = spi_receive();
    
    spi_deselect();
    
    return (ack == 0xAA); // 0xAA = ACK, 0x55 = NAK
}

bool spi_receive_with_checksum(uint8_t *buffer, size_t max_length, 
                                size_t *actual_length) {
    spi_select();
    
    // Receive length
    uint8_t length = spi_receive();
    if (length > max_length) {
        spi_transmit(0x55); // NAK
        spi_deselect();
        return false;
    }
    
    // Receive data
    for (size_t i = 0; i < length; i++) {
        buffer[i] = spi_receive();
    }
    
    // Receive checksum
    uint8_t received_checksum = spi_receive();
    uint8_t calculated_checksum = calculate_checksum(buffer, length);
    
    // Validate and send acknowledgment
    if (received_checksum == calculated_checksum) {
        spi_transmit(0xAA); // ACK
        *actual_length = length;
        spi_deselect();
        return true;
    } else {
        spi_transmit(0x55); // NAK
        spi_deselect();
        return false;
    }
}

// ============================================
// CRC-8 validation (polynomial 0x07)
// ============================================

uint8_t crc8_table[256];

void crc8_init(void) {
    for (uint16_t i = 0; i < 256; i++) {
        uint8_t crc = i;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
        crc8_table[i] = crc;
    }
}

uint8_t calculate_crc8(const uint8_t *data, size_t length) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < length; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

bool spi_send_with_crc8(const uint8_t *data, size_t length) {
    spi_select();
    
    // Send data
    for (size_t i = 0; i < length; i++) {
        spi_transmit(data[i]);
    }
    
    // Calculate and send CRC
    uint8_t crc = calculate_crc8(data, length);
    spi_transmit(crc);
    
    // Wait for acknowledgment
    uint8_t ack = spi_receive();
    
    spi_deselect();
    
    return (ack == 0xAA);
}

// ============================================
// Register readback validation
// ============================================

typedef struct {
    uint8_t reg_addr;
    uint8_t value;
} spi_register_t;

bool spi_write_register_verified(uint8_t reg_addr, uint8_t value, 
                                  uint8_t max_retries) {
    for (uint8_t attempt = 0; attempt < max_retries; attempt++) {
        // Write register
        spi_select();
        spi_transmit(0x02); // Write command
        spi_transmit(reg_addr);
        spi_transmit(value);
        spi_deselect();
        
        // Small delay for write to complete
        for (volatile int i = 0; i < 1000; i++);
        
        // Read back
        spi_select();
        spi_transmit(0x03); // Read command
        spi_transmit(reg_addr);
        uint8_t readback = spi_receive();
        spi_deselect();
        
        // Verify
        if (readback == value) {
            return true;
        }
    }
    
    return false; // Failed after all retries
}

// ============================================
// Frame-based protocol with validation
// ============================================

#define FRAME_START_BYTE 0x7E
#define FRAME_END_BYTE   0x7F
#define MAX_PAYLOAD_SIZE 64

typedef struct {
    uint8_t start;
    uint8_t length;
    uint8_t payload[MAX_PAYLOAD_SIZE];
    uint8_t crc;
    uint8_t end;
} spi_frame_t;

bool spi_send_frame(const uint8_t *payload, size_t length) {
    if (length > MAX_PAYLOAD_SIZE) return false;
    
    spi_frame_t frame;
    frame.start = FRAME_START_BYTE;
    frame.length = (uint8_t)length;
    memcpy(frame.payload, payload, length);
    frame.crc = calculate_crc8(payload, length);
    frame.end = FRAME_END_BYTE;
    
    spi_select();
    
    // Send frame
    spi_transmit(frame.start);
    spi_transmit(frame.length);
    for (size_t i = 0; i < length; i++) {
        spi_transmit(frame.payload[i]);
    }
    spi_transmit(frame.crc);
    spi_transmit(frame.end);
    
    // Wait for ACK
    uint8_t ack = spi_receive();
    
    spi_deselect();
    
    return (ack == 0xAA);
}

bool spi_receive_frame(uint8_t *payload, size_t *length) {
    spi_select();
    
    // Check start byte
    uint8_t start = spi_receive();
    if (start != FRAME_START_BYTE) {
        spi_transmit(0x55); // NAK
        spi_deselect();
        return false;
    }
    
    // Get length
    uint8_t len = spi_receive();
    if (len > MAX_PAYLOAD_SIZE) {
        spi_transmit(0x55); // NAK
        spi_deselect();
        return false;
    }
    
    // Receive payload
    for (size_t i = 0; i < len; i++) {
        payload[i] = spi_receive();
    }
    
    // Receive CRC and end byte
    uint8_t received_crc = spi_receive();
    uint8_t end = spi_receive();
    
    // Validate
    uint8_t calculated_crc = calculate_crc8(payload, len);
    
    if (end == FRAME_END_BYTE && received_crc == calculated_crc) {
        spi_transmit(0xAA); // ACK
        *length = len;
        spi_deselect();
        return true;
    } else {
        spi_transmit(0x55); // NAK
        spi_deselect();
        return false;
    }
}
```

## C++ Implementation

```cpp
#include <cstdint>
#include <vector>
#include <array>
#include <optional>
#include <algorithm>

class SPIDevice {
protected:
    virtual void select() = 0;
    virtual void deselect() = 0;
    virtual void transmit(uint8_t data) = 0;
    virtual uint8_t receive() = 0;
    
public:
    virtual ~SPIDevice() = default;
};

// ============================================
// CRC Calculator Template
// ============================================

template<typename CRCType, CRCType Polynomial>
class CRCCalculator {
private:
    std::array<CRCType, 256> table;
    
    void buildTable() {
        constexpr size_t width = sizeof(CRCType) * 8;
        constexpr CRCType msb = static_cast<CRCType>(1) << (width - 1);
        
        for (size_t i = 0; i < 256; i++) {
            CRCType crc = static_cast<CRCType>(i) << (width - 8);
            
            for (size_t j = 0; j < 8; j++) {
                if (crc & msb) {
                    crc = (crc << 1) ^ Polynomial;
                } else {
                    crc <<= 1;
                }
            }
            
            table[i] = crc;
        }
    }
    
public:
    CRCCalculator() {
        buildTable();
    }
    
    CRCType calculate(const uint8_t* data, size_t length, 
                      CRCType initial = 0) const {
        CRCType crc = initial;
        constexpr size_t width = sizeof(CRCType) * 8;
        
        for (size_t i = 0; i < length; i++) {
            uint8_t index = (crc >> (width - 8)) ^ data[i];
            crc = (crc << 8) ^ table[index];
        }
        
        return crc;
    }
    
    CRCType calculate(const std::vector<uint8_t>& data, 
                      CRCType initial = 0) const {
        return calculate(data.data(), data.size(), initial);
    }
};

// Common CRC types
using CRC8 = CRCCalculator<uint8_t, 0x07>;
using CRC16 = CRCCalculator<uint16_t, 0x1021>;
using CRC32 = CRCCalculator<uint32_t, 0x04C11DB7>;

// ============================================
// Validated SPI Transaction
// ============================================

class ValidatedSPITransaction {
public:
    enum class ValidationMethod {
        CHECKSUM,
        CRC8,
        CRC16,
        CRC32
    };
    
    enum class AckResponse : uint8_t {
        ACK = 0xAA,
        NAK = 0x55,
        TIMEOUT = 0xFF
    };
    
private:
    SPIDevice& device;
    ValidationMethod method;
    CRC8 crc8_calc;
    CRC16 crc16_calc;
    CRC32 crc32_calc;
    
    template<typename T>
    std::vector<uint8_t> serializeCRC(T crc) const {
        std::vector<uint8_t> bytes(sizeof(T));
        for (size_t i = 0; i < sizeof(T); i++) {
            bytes[sizeof(T) - 1 - i] = static_cast<uint8_t>(crc >> (i * 8));
        }
        return bytes;
    }
    
    uint8_t calculateChecksum(const std::vector<uint8_t>& data) const {
        return std::accumulate(data.begin(), data.end(), uint8_t(0));
    }
    
public:
    ValidatedSPITransaction(SPIDevice& dev, 
                           ValidationMethod m = ValidationMethod::CRC8)
        : device(dev), method(m) {}
    
    bool sendValidated(const std::vector<uint8_t>& data) {
        device.select();
        
        // Send length
        device.transmit(static_cast<uint8_t>(data.size()));
        
        // Send data
        for (uint8_t byte : data) {
            device.transmit(byte);
        }
        
        // Send validation bytes
        switch (method) {
            case ValidationMethod::CHECKSUM: {
                uint8_t checksum = calculateChecksum(data);
                device.transmit(checksum);
                break;
            }
            case ValidationMethod::CRC8: {
                uint8_t crc = crc8_calc.calculate(data);
                device.transmit(crc);
                break;
            }
            case ValidationMethod::CRC16: {
                uint16_t crc = crc16_calc.calculate(data);
                auto bytes = serializeCRC(crc);
                for (uint8_t byte : bytes) {
                    device.transmit(byte);
                }
                break;
            }
            case ValidationMethod::CRC32: {
                uint32_t crc = crc32_calc.calculate(data);
                auto bytes = serializeCRC(crc);
                for (uint8_t byte : bytes) {
                    device.transmit(byte);
                }
                break;
            }
        }
        
        // Wait for acknowledgment
        AckResponse ack = static_cast<AckResponse>(device.receive());
        
        device.deselect();
        
        return (ack == AckResponse::ACK);
    }
    
    std::optional<std::vector<uint8_t>> receiveValidated(size_t max_length) {
        device.select();
        
        // Receive length
        uint8_t length = device.receive();
        if (length > max_length) {
            device.transmit(static_cast<uint8_t>(AckResponse::NAK));
            device.deselect();
            return std::nullopt;
        }
        
        // Receive data
        std::vector<uint8_t> data(length);
        for (size_t i = 0; i < length; i++) {
            data[i] = device.receive();
        }
        
        // Receive and validate
        bool valid = false;
        
        switch (method) {
            case ValidationMethod::CHECKSUM: {
                uint8_t received = device.receive();
                uint8_t calculated = calculateChecksum(data);
                valid = (received == calculated);
                break;
            }
            case ValidationMethod::CRC8: {
                uint8_t received = device.receive();
                uint8_t calculated = crc8_calc.calculate(data);
                valid = (received == calculated);
                break;
            }
            case ValidationMethod::CRC16: {
                uint16_t received = 0;
                for (size_t i = 0; i < 2; i++) {
                    received = (received << 8) | device.receive();
                }
                uint16_t calculated = crc16_calc.calculate(data);
                valid = (received == calculated);
                break;
            }
            case ValidationMethod::CRC32: {
                uint32_t received = 0;
                for (size_t i = 0; i < 4; i++) {
                    received = (received << 8) | device.receive();
                }
                uint32_t calculated = crc32_calc.calculate(data);
                valid = (received == calculated);
                break;
            }
        }
        
        // Send acknowledgment
        if (valid) {
            device.transmit(static_cast<uint8_t>(AckResponse::ACK));
            device.deselect();
            return data;
        } else {
            device.transmit(static_cast<uint8_t>(AckResponse::NAK));
            device.deselect();
            return std::nullopt;
        }
    }
};

// ============================================
// Retry Mechanism with Validation
// ============================================

class ReliableSPITransaction {
private:
    ValidatedSPITransaction& transaction;
    uint8_t max_retries;
    uint32_t retry_delay_ms;
    
public:
    ReliableSPITransaction(ValidatedSPITransaction& txn, 
                          uint8_t retries = 3,
                          uint32_t delay_ms = 10)
        : transaction(txn), max_retries(retries), retry_delay_ms(delay_ms) {}
    
    bool sendReliable(const std::vector<uint8_t>& data) {
        for (uint8_t attempt = 0; attempt < max_retries; attempt++) {
            if (transaction.sendValidated(data)) {
                return true;
            }
            
            // Delay before retry (platform-specific implementation needed)
            // delay_ms(retry_delay_ms);
        }
        return false;
    }
    
    std::optional<std::vector<uint8_t>> receiveReliable(size_t max_length) {
        for (uint8_t attempt = 0; attempt < max_retries; attempt++) {
            auto result = transaction.receiveValidated(max_length);
            if (result.has_value()) {
                return result;
            }
            
            // Delay before retry
            // delay_ms(retry_delay_ms);
        }
        return std::nullopt;
    }
};
```

## Rust Implementation

```rust
// ============================================
// CRC Calculation Traits and Implementations
// ============================================

trait CrcCalculator {
    type CrcType: Copy + Default;
    
    fn calculate(&self, data: &[u8]) -> Self::CrcType;
}

struct Crc8 {
    table: [u8; 256],
}

impl Crc8 {
    fn new(polynomial: u8) -> Self {
        let mut table = [0u8; 256];
        
        for i in 0..256 {
            let mut crc = i as u8;
            for _ in 0..8 {
                if crc & 0x80 != 0 {
                    crc = (crc << 1) ^ polynomial;
                } else {
                    crc <<= 1;
                }
            }
            table[i] = crc;
        }
        
        Self { table }
    }
}

impl CrcCalculator for Crc8 {
    type CrcType = u8;
    
    fn calculate(&self, data: &[u8]) -> u8 {
        let mut crc = 0u8;
        for &byte in data {
            let index = (crc ^ byte) as usize;
            crc = self.table[index];
        }
        crc
    }
}

struct Crc16 {
    table: [u16; 256],
}

impl Crc16 {
    fn new(polynomial: u16) -> Self {
        let mut table = [0u16; 256];
        
        for i in 0..256 {
            let mut crc = (i as u16) << 8;
            for _ in 0..8 {
                if crc & 0x8000 != 0 {
                    crc = (crc << 1) ^ polynomial;
                } else {
                    crc <<= 1;
                }
            }
            table[i] = crc;
        }
        
        Self { table }
    }
}

impl CrcCalculator for Crc16 {
    type CrcType = u16;
    
    fn calculate(&self, data: &[u8]) -> u16 {
        let mut crc = 0u16;
        for &byte in data {
            let index = ((crc >> 8) as u8 ^ byte) as usize;
            crc = (crc << 8) ^ self.table[index];
        }
        crc
    }
}

// ============================================
// SPI Device Trait
// ============================================

trait SpiDevice {
    fn select(&mut self);
    fn deselect(&mut self);
    fn transmit(&mut self, data: u8);
    fn receive(&mut self) -> u8;
    
    fn transfer(&mut self, data: u8) -> u8 {
        self.transmit(data);
        self.receive()
    }
}

// ============================================
// Validation Methods
// ============================================

#[derive(Debug, Clone, Copy, PartialEq)]
enum ValidationMethod {
    Checksum,
    Crc8,
    Crc16,
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum AckResponse {
    Ack = 0xAA,
    Nak = 0x55,
}

impl TryFrom<u8> for AckResponse {
    type Error = ();
    
    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0xAA => Ok(AckResponse::Ack),
            0x55 => Ok(AckResponse::Nak),
            _ => Err(()),
        }
    }
}

// ============================================
// Validated SPI Transaction
// ============================================

struct ValidatedSpiTransaction<D: SpiDevice> {
    device: D,
    method: ValidationMethod,
    crc8: Crc8,
    crc16: Crc16,
}

impl<D: SpiDevice> ValidatedSpiTransaction<D> {
    fn new(device: D, method: ValidationMethod) -> Self {
        Self {
            device,
            method,
            crc8: Crc8::new(0x07),
            crc16: Crc16::new(0x1021),
        }
    }
    
    fn calculate_checksum(data: &[u8]) -> u8 {
        data.iter().fold(0u8, |acc, &x| acc.wrapping_add(x))
    }
    
    fn send_validated(&mut self, data: &[u8]) -> Result<(), ()> {
        if data.len() > 255 {
            return Err(());
        }
        
        self.device.select();
        
        // Send length
        self.device.transmit(data.len() as u8);
        
        // Send data
        for &byte in data {
            self.device.transmit(byte);
        }
        
        // Send validation
        match self.method {
            ValidationMethod::Checksum => {
                let checksum = Self::calculate_checksum(data);
                self.device.transmit(checksum);
            }
            ValidationMethod::Crc8 => {
                let crc = self.crc8.calculate(data);
                self.device.transmit(crc);
            }
            ValidationMethod::Crc16 => {
                let crc = self.crc16.calculate(data);
                self.device.transmit((crc >> 8) as u8);
                self.device.transmit(crc as u8);
            }
        }
        
        // Wait for acknowledgment
        let ack_byte = self.device.receive();
        let ack = AckResponse::try_from(ack_byte);
        
        self.device.deselect();
        
        match ack {
            Ok(AckResponse::Ack) => Ok(()),
            _ => Err(()),
        }
    }
    
    fn receive_validated(&mut self, max_length: usize) -> Result<Vec<u8>, ()> {
        self.device.select();
        
        // Receive length
        let length = self.device.receive() as usize;
        if length > max_length {
            self.device.transmit(AckResponse::Nak as u8);
            self.device.deselect();
            return Err(());
        }
        
        // Receive data
        let mut data = Vec::with_capacity(length);
        for _ in 0..length {
            data.push(self.device.receive());
        }
        
        // Receive and validate
        let valid = match self.method {
            ValidationMethod::Checksum => {
                let received = self.device.receive();
                let calculated = Self::calculate_checksum(&data);
                received == calculated
            }
            ValidationMethod::Crc8 => {
                let received = self.device.receive();
                let calculated = self.crc8.calculate(&data);
                received == calculated
            }
            ValidationMethod::Crc16 => {
                let high = self.device.receive() as u16;
                let low = self.device.receive() as u16;
                let received = (high << 8) | low;
                let calculated = self.crc16.calculate(&data);
                received == calculated
            }
        };
        
        // Send acknowledgment
        if valid {
            self.device.transmit(AckResponse::Ack as u8);
            self.device.deselect();
            Ok(data)
        } else {
            self.device.transmit(AckResponse::Nak as u8);
            self.device.deselect();
            Err(())
        }
    }
}

// ============================================
// Reliable Transaction with Retries
// ============================================

struct ReliableSpiTransaction<D: SpiDevice> {
    transaction: ValidatedSpiTransaction<D>,
    max_retries: u8,
}

impl<D: SpiDevice> ReliableSpiTransaction<D> {
    fn new(device: D, method: ValidationMethod, max_retries: u8) -> Self {
        Self {
            transaction: ValidatedSpiTransaction::new(device, method),
            max_retries,
        }
    }
    
    fn send_reliable(&mut self, data: &[u8]) -> Result<(), ()> {
        for attempt in 0..self.max_retries {
            match self.transaction.send_validated(data) {
                Ok(()) => return Ok(()),
                Err(()) if attempt < self.max_retries - 1 => {
                    // Small delay before retry (platform-specific)
                    continue;
                }
                Err(()) => return Err(()),
            }
        }
        Err(())
    }
    
    fn receive_reliable(&mut self, max_length: usize) -> Result<Vec<u8>, ()> {
        for attempt in 0..self.max_retries {
            match self.transaction.receive_validated(max_length) {
                Ok(data) => return Ok(data),
                Err(()) if attempt < self.max_retries - 1 => {
                    continue;
                }
                Err(()) => return Err(()),
            }
        }
        Err(())
    }
}

// ============================================
// Frame-based Protocol
// ============================================

const FRAME_START: u8 = 0x7E;
const FRAME_END: u8 = 0x7F;
const MAX_PAYLOAD: usize = 64;

struct SpiFrame {
    payload: Vec<u8>,
}

impl SpiFrame {
    fn new(payload: Vec<u8>) -> Result<Self, ()> {
        if payload.len() > MAX_PAYLOAD {
            return Err(());
        }
        Ok(Self { payload })
    }
    
    fn send<D: SpiDevice>(&self, device: &mut D, crc_calc: &Crc8) -> Result<(), ()> {
        device.select();
        
        // Send frame structure
        device.transmit(FRAME_START);
        device.transmit(self.payload.len() as u8);
        
        for &byte in &self.payload {
            device.transmit(byte);
        }
        
        let crc = crc_calc.calculate(&self.payload);
        device.transmit(crc);
        device.transmit(FRAME_END);
        
        // Wait for ACK
        let ack = AckResponse::try_from(device.receive());
        
        device.deselect();
        
        match ack {
            Ok(AckResponse::Ack) => Ok(()),
            _ => Err(()),
        }
    }
    
    fn receive<D: SpiDevice>(device: &mut D, crc_calc: &Crc8) -> Result<Self, ()> {
        device.select();
        
        // Check start byte
        if device.receive() != FRAME_START {
            device.transmit(AckResponse::Nak as u8);
            device.deselect();
            return Err(());
        }
        
        // Get length
        let length = device.receive() as usize;
        if length > MAX_PAYLOAD {
            device.transmit(AckResponse::Nak as u8);
            device.deselect();
            return Err(());
        }
        
        // Receive payload
        let mut payload = Vec::with_capacity(length);
        for _ in 0..length {
            payload.push(device.receive());
        }
        
        // Validate
        let received_crc = device.receive();
        let end_byte = device.receive();
        let calculated_crc = crc_calc.calculate(&payload);
        
        if end_byte == FRAME_END && received_crc == calculated_crc {
            device.transmit(AckResponse::Ack as u8);
            device.deselect();
            Ok(Self { payload })
        } else {
            device.transmit(AckResponse::Nak as u8);
            device.deselect();
            Err(())
        }
    }
}

// ============================================
// Usage Example
// ============================================

#[cfg(test)]
mod tests {
    use super::*;
    
    // Mock SPI device for testing
    struct MockSpiDevice {
        tx_buffer: Vec<u8>,
        rx_buffer: Vec<u8>,
        rx_index: usize,
    }
    
    impl MockSpiDevice {
        fn new(rx_data: Vec<u8>) -> Self {
            Self {
                tx_buffer: Vec::new(),
                rx_buffer: rx_data,
                rx_index: 0,
            }
        }
    }
    
    impl SpiDevice for MockSpiDevice {
        fn select(&mut self) {}
        fn deselect(&mut self) {}
        
        fn transmit(&mut self, data: u8) {
            self.tx_buffer.push(data);
        }
        
        fn receive(&mut self) -> u8 {
            if self.rx_index < self.rx_buffer.len() {
                let data = self.rx_buffer[self.rx_index];
                self.rx_index += 1;
                data
            } else {
                0
            }
        }
    }
    
    #[test]
    fn test_crc8_calculation() {
        let crc8 = Crc8::new(0x07);
        let data = vec![0x01, 0x02, 0x03, 0x04];
        let crc = crc8.calculate(&data);
        assert_ne!(crc, 0); // Should produce non-zero CRC
    }
}
```

## Summary

**SPI Data Validation** is essential for reliable communication in systems lacking built-in error detection. Key validation approaches include:

**Validation Techniques**:
- **Checksums**: Simple byte summation for basic integrity checking
- **CRC-8/16/32**: Polynomial-based detection for robust error identification
- **Acknowledgments**: Bidirectional confirmation (ACK/NAK) after transmission
- **Register Readback**: Write-then-read verification for critical operations
- **Frame Protocols**: Structured packets with start/end bytes and validation

**Implementation Considerations**:
- Choose validation method based on reliability requirements and overhead tolerance
- CRC provides superior error detection compared to simple checksums
- Implement retry mechanisms for critical data transfers
- Use table-based CRC for performance efficiency
- Balance validation overhead against transmission speed requirements

**Best Practices**:
- Always validate writes to configuration registers through readback
- Implement timeout mechanisms alongside acknowledgments
- Use framing protocols for complex multi-byte transactions
- Consider combining multiple validation methods for critical systems
- Document expected validation overhead in system timing budgets

The code examples demonstrate complete implementations from simple checksum validation to sophisticated frame-based protocols with CRC verification and automatic retries, providing a foundation for building reliable SPI communication systems.