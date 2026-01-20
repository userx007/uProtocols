# SD Card SPI Mode - Detailed Technical Guide

## Overview

SD cards can operate in two primary modes: **SD bus mode** (1-bit or 4-bit) and **SPI mode**. SPI mode is particularly valuable for embedded systems because it requires fewer GPIO pins (typically 4-6) and uses the ubiquitous SPI protocol, making it compatible with virtually any microcontroller. This makes SD cards in SPI mode ideal for data logging, sensor data storage, configuration file management, and embedded file systems.

## Understanding SPI (Serial Peripheral Interface)

SPI is a synchronous serial communication protocol that uses a master-slave architecture. Key characteristics:

- **Full-duplex communication**: Simultaneous data transmission in both directions
- **Clock-driven**: Master generates clock signal, ensuring synchronization
- **Multiple slave support**: Using chip select (CS) lines
- **High speed**: Can operate from hundreds of kHz to tens of MHz

### SPI Signal Lines

1. **MOSI** (Master Out Slave In): Data from master to slave
2. **MISO** (Master In Slave Out): Data from slave to master
3. **SCK** (Serial Clock): Clock signal from master
4. **CS/SS** (Chip Select/Slave Select): Enables specific slave device

## SD Card SPI Mode Communication

### Initialization Sequence

The SD card initialization in SPI mode follows a specific sequence:

1. Apply power and wait for stabilization (1-10ms)
2. Send at least 74 clock pulses with CS high
3. Assert CS low and send CMD0 (GO_IDLE_STATE)
4. Send CMD8 (SEND_IF_COND) for voltage validation
5. Repeatedly send ACMD41 until card is ready
6. Read card capacity with CMD58
7. Set block length if needed (CMD16)

### SD Card Commands

Commands are 6 bytes:
- **Byte 0**: Command index (0x40 | command_number)
- **Bytes 1-4**: 32-bit argument
- **Byte 5**: CRC7 checksum + stop bit

### Response Types

- **R1**: 1 byte response (most common)
- **R3**: R1 + 4 bytes (OCR register)
- **R7**: R1 + 4 bytes (voltage/pattern)

## C/C++ Implementation

### Basic SD Card Driver (Bare Metal)

```c
#include <stdint.h>
#include <stdbool.h>

// SD Card Commands
#define CMD0    0   // GO_IDLE_STATE
#define CMD8    8   // SEND_IF_COND
#define CMD17   17  // READ_SINGLE_BLOCK
#define CMD24   24  // WRITE_BLOCK
#define CMD55   55  // APP_CMD
#define CMD58   58  // READ_OCR
#define ACMD41  41  // SD_SEND_OP_COND

// Response tokens
#define R1_IDLE_STATE       0x01
#define R1_READY            0x00
#define START_BLOCK_TOKEN   0xFE
#define DATA_ACCEPTED       0x05

// Timeout values
#define SD_INIT_TIMEOUT     1000
#define SD_READ_TIMEOUT     10000
#define SD_WRITE_TIMEOUT    50000

// Card types
typedef enum {
    CARD_TYPE_UNKNOWN = 0,
    CARD_TYPE_SD_V1,
    CARD_TYPE_SD_V2,
    CARD_TYPE_SDHC
} sd_card_type_t;

typedef struct {
    sd_card_type_t type;
    uint32_t capacity;
    bool initialized;
} sd_card_t;

// Platform-specific SPI functions (must be implemented)
extern void spi_init(void);
extern uint8_t spi_transfer(uint8_t data);
extern void cs_low(void);
extern void cs_high(void);
extern void delay_ms(uint32_t ms);

// Calculate CRC7 for SD commands
uint8_t crc7(uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x09) : (crc << 1);
        }
    }
    return (crc >> 1) & 0x7F;
}

// Send command to SD card
uint8_t sd_send_command(uint8_t cmd, uint32_t arg) {
    uint8_t buf[6];
    uint8_t response;
    uint16_t timeout = 0xFF;
    
    // Prepare command packet
    buf[0] = 0x40 | cmd;
    buf[1] = (arg >> 24) & 0xFF;
    buf[2] = (arg >> 16) & 0xFF;
    buf[3] = (arg >> 8) & 0xFF;
    buf[4] = arg & 0xFF;
    
    // Calculate CRC (only critical for CMD0 and CMD8)
    if (cmd == CMD0) {
        buf[5] = 0x95;  // Valid CRC for CMD0
    } else if (cmd == CMD8) {
        buf[5] = 0x87;  // Valid CRC for CMD8(0x1AA)
    } else {
        buf[5] = (crc7(buf, 5) << 1) | 0x01;
    }
    
    // Send command
    for (int i = 0; i < 6; i++) {
        spi_transfer(buf[i]);
    }
    
    // Wait for response (MSB = 0)
    do {
        response = spi_transfer(0xFF);
    } while ((response & 0x80) && --timeout);
    
    return response;
}

// Initialize SD card
bool sd_init(sd_card_t *card) {
    uint8_t response;
    uint16_t timeout;
    uint8_t ocr[4];
    
    card->initialized = false;
    card->type = CARD_TYPE_UNKNOWN;
    
    // Initialize SPI peripheral
    spi_init();
    cs_high();
    
    // Send 80 clock pulses with CS high
    for (int i = 0; i < 10; i++) {
        spi_transfer(0xFF);
    }
    delay_ms(1);
    
    // Enter SPI mode
    cs_low();
    response = sd_send_command(CMD0, 0);
    cs_high();
    
    if (response != R1_IDLE_STATE) {
        return false;
    }
    
    // Check voltage range (CMD8)
    cs_low();
    response = sd_send_command(CMD8, 0x1AA);
    
    if (response == R1_IDLE_STATE) {
        // SD Card v2.0 or later
        for (int i = 0; i < 4; i++) {
            ocr[i] = spi_transfer(0xFF);
        }
        cs_high();
        
        // Verify voltage and check pattern
        if ((ocr[2] & 0x0F) != 0x01 || ocr[3] != 0xAA) {
            return false;
        }
        
        // Initialize card (ACMD41 with HCS bit)
        timeout = SD_INIT_TIMEOUT;
        do {
            cs_low();
            sd_send_command(CMD55, 0);
            response = sd_send_command(ACMD41, 0x40000000);
            cs_high();
            
            if (response == R1_READY) {
                break;
            }
            delay_ms(1);
        } while (--timeout);
        
        if (timeout == 0) {
            return false;
        }
        
        // Read OCR to check card capacity
        cs_low();
        response = sd_send_command(CMD58, 0);
        if (response == R1_READY) {
            for (int i = 0; i < 4; i++) {
                ocr[i] = spi_transfer(0xFF);
            }
            
            // Check CCS bit (Card Capacity Status)
            if (ocr[0] & 0x40) {
                card->type = CARD_TYPE_SDHC;
            } else {
                card->type = CARD_TYPE_SD_V2;
            }
        }
        cs_high();
    } else {
        // SD Card v1.x or MMC
        cs_high();
        card->type = CARD_TYPE_SD_V1;
        
        // Initialize with ACMD41
        timeout = SD_INIT_TIMEOUT;
        do {
            cs_low();
            sd_send_command(CMD55, 0);
            response = sd_send_command(ACMD41, 0);
            cs_high();
            
            if (response == R1_READY) {
                break;
            }
            delay_ms(1);
        } while (--timeout);
        
        if (timeout == 0) {
            return false;
        }
    }
    
    card->initialized = true;
    return true;
}

// Read a single 512-byte block
bool sd_read_block(sd_card_t *card, uint32_t block_addr, uint8_t *buffer) {
    uint8_t response;
    uint16_t timeout;
    
    if (!card->initialized) {
        return false;
    }
    
    // For non-SDHC cards, convert block address to byte address
    if (card->type != CARD_TYPE_SDHC) {
        block_addr *= 512;
    }
    
    cs_low();
    response = sd_send_command(CMD17, block_addr);
    
    if (response != R1_READY) {
        cs_high();
        return false;
    }
    
    // Wait for start block token
    timeout = SD_READ_TIMEOUT;
    do {
        response = spi_transfer(0xFF);
    } while (response != START_BLOCK_TOKEN && --timeout);
    
    if (timeout == 0) {
        cs_high();
        return false;
    }
    
    // Read 512 bytes of data
    for (int i = 0; i < 512; i++) {
        buffer[i] = spi_transfer(0xFF);
    }
    
    // Read 16-bit CRC (ignored in SPI mode)
    spi_transfer(0xFF);
    spi_transfer(0xFF);
    
    cs_high();
    spi_transfer(0xFF);  // Extra clock cycles
    
    return true;
}

// Write a single 512-byte block
bool sd_write_block(sd_card_t *card, uint32_t block_addr, const uint8_t *buffer) {
    uint8_t response;
    uint16_t timeout;
    
    if (!card->initialized) {
        return false;
    }
    
    // For non-SDHC cards, convert block address to byte address
    if (card->type != CARD_TYPE_SDHC) {
        block_addr *= 512;
    }
    
    cs_low();
    response = sd_send_command(CMD24, block_addr);
    
    if (response != R1_READY) {
        cs_high();
        return false;
    }
    
    // Send start block token
    spi_transfer(START_BLOCK_TOKEN);
    
    // Write 512 bytes of data
    for (int i = 0; i < 512; i++) {
        spi_transfer(buffer[i]);
    }
    
    // Send dummy CRC
    spi_transfer(0xFF);
    spi_transfer(0xFF);
    
    // Check data response token
    response = spi_transfer(0xFF);
    if ((response & 0x1F) != DATA_ACCEPTED) {
        cs_high();
        return false;
    }
    
    // Wait for card to finish writing (busy signal)
    timeout = SD_WRITE_TIMEOUT;
    while (spi_transfer(0xFF) == 0x00 && --timeout);
    
    cs_high();
    spi_transfer(0xFF);  // Extra clock cycles
    
    return (timeout != 0);
}
```

### Data Logging Example

```cpp
#include <stdio.h>
#include <string.h>
#include <time.h>

class DataLogger {
private:
    sd_card_t card;
    uint32_t current_block;
    uint8_t buffer[512];
    uint16_t buffer_offset;
    
public:
    DataLogger() : current_block(0), buffer_offset(0) {
        memset(buffer, 0, sizeof(buffer));
    }
    
    bool begin() {
        if (!sd_init(&card)) {
            return false;
        }
        
        // Read first block to check for existing data
        if (sd_read_block(&card, 0, buffer)) {
            // Find last written entry
            // (Implementation depends on your data format)
        }
        
        return true;
    }
    
    bool logData(const char* sensor_name, float value) {
        char entry[64];
        time_t now = time(NULL);
        
        // Format: timestamp,sensor,value\n
        snprintf(entry, sizeof(entry), "%lu,%s,%.2f\n", 
                 now, sensor_name, value);
        
        size_t entry_len = strlen(entry);
        
        // Check if entry fits in current buffer
        if (buffer_offset + entry_len > 512) {
            // Flush current buffer to SD card
            if (!flush()) {
                return false;
            }
        }
        
        // Add entry to buffer
        memcpy(&buffer[buffer_offset], entry, entry_len);
        buffer_offset += entry_len;
        
        return true;
    }
    
    bool flush() {
        if (buffer_offset == 0) {
            return true;
        }
        
        // Fill remainder with zeros
        if (buffer_offset < 512) {
            memset(&buffer[buffer_offset], 0, 512 - buffer_offset);
        }
        
        // Write to SD card
        if (!sd_write_block(&card, current_block, buffer)) {
            return false;
        }
        
        // Prepare for next block
        current_block++;
        buffer_offset = 0;
        memset(buffer, 0, sizeof(buffer));
        
        return true;
    }
    
    void readLogs(uint32_t start_block, uint32_t num_blocks) {
        uint8_t read_buffer[512];
        
        for (uint32_t block = start_block; 
             block < start_block + num_blocks; block++) {
            
            if (sd_read_block(&card, block, read_buffer)) {
                // Parse and display entries
                printf("Block %lu:\n%s\n", block, read_buffer);
            }
        }
    }
};

// Usage example
void sensor_logging_example() {
    DataLogger logger;
    
    if (!logger.begin()) {
        printf("Failed to initialize SD card!\n");
        return;
    }
    
    // Log sensor data
    for (int i = 0; i < 100; i++) {
        float temperature = 20.0 + (i * 0.1);
        float humidity = 50.0 + (i * 0.05);
        
        logger.logData("temp", temperature);
        logger.logData("humidity", humidity);
        
        delay_ms(1000);  // 1 second interval
    }
    
    // Ensure all data is written
    logger.flush();
}
```

## Rust Implementation

```rust
use core::fmt;

// SD Card command definitions
const CMD0: u8 = 0;   // GO_IDLE_STATE
const CMD8: u8 = 8;   // SEND_IF_COND
const CMD17: u8 = 17; // READ_SINGLE_BLOCK
const CMD24: u8 = 24; // WRITE_BLOCK
const CMD55: u8 = 55; // APP_CMD
const CMD58: u8 = 58; // READ_OCR
const ACMD41: u8 = 41; // SD_SEND_OP_COND

// Response and token values
const R1_IDLE_STATE: u8 = 0x01;
const R1_READY: u8 = 0x00;
const START_BLOCK_TOKEN: u8 = 0xFE;
const DATA_ACCEPTED: u8 = 0x05;

// Timeouts
const INIT_TIMEOUT: u16 = 1000;
const READ_TIMEOUT: u16 = 10000;
const WRITE_TIMEOUT: u16 = 50000;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum CardType {
    Unknown,
    SdV1,
    SdV2,
    Sdhc,
}

#[derive(Debug)]
pub enum SdError {
    InitFailed,
    CommandFailed,
    ReadTimeout,
    WriteTimeout,
    NotInitialized,
    InvalidResponse,
}

// Trait for SPI operations (implement for your platform)
pub trait SpiInterface {
    fn transfer(&mut self, data: u8) -> u8;
    fn cs_low(&mut self);
    fn cs_high(&mut self);
    fn delay_ms(&mut self, ms: u32);
}

pub struct SdCard<SPI> {
    spi: SPI,
    card_type: CardType,
    initialized: bool,
}

impl<SPI: SpiInterface> SdCard<SPI> {
    pub fn new(spi: SPI) -> Self {
        SdCard {
            spi,
            card_type: CardType::Unknown,
            initialized: false,
        }
    }
    
    // Calculate CRC7 checksum
    fn crc7(data: &[u8]) -> u8 {
        let mut crc: u8 = 0;
        
        for &byte in data {
            crc ^= byte;
            for _ in 0..8 {
                crc = if crc & 0x80 != 0 {
                    (crc << 1) ^ 0x09
                } else {
                    crc << 1
                };
            }
        }
        
        (crc >> 1) & 0x7F
    }
    
    // Send command to SD card
    fn send_command(&mut self, cmd: u8, arg: u32) -> Result<u8, SdError> {
        let mut buf = [0u8; 6];
        
        // Build command packet
        buf[0] = 0x40 | cmd;
        buf[1] = (arg >> 24) as u8;
        buf[2] = (arg >> 16) as u8;
        buf[3] = (arg >> 8) as u8;
        buf[4] = arg as u8;
        
        // Set CRC
        buf[5] = match cmd {
            CMD0 => 0x95, // Valid CRC for CMD0
            CMD8 => 0x87, // Valid CRC for CMD8(0x1AA)
            _ => (Self::crc7(&buf[0..5]) << 1) | 0x01,
        };
        
        // Send command bytes
        for &byte in &buf {
            self.spi.transfer(byte);
        }
        
        // Wait for response (MSB = 0)
        for _ in 0..255 {
            let response = self.spi.transfer(0xFF);
            if response & 0x80 == 0 {
                return Ok(response);
            }
        }
        
        Err(SdError::CommandFailed)
    }
    
    // Initialize SD card in SPI mode
    pub fn init(&mut self) -> Result<(), SdError> {
        self.initialized = false;
        self.card_type = CardType::Unknown;
        
        self.spi.cs_high();
        
        // Send 80 clock pulses with CS high
        for _ in 0..10 {
            self.spi.transfer(0xFF);
        }
        self.spi.delay_ms(1);
        
        // Enter SPI mode with CMD0
        self.spi.cs_low();
        let response = self.send_command(CMD0, 0)?;
        self.spi.cs_high();
        
        if response != R1_IDLE_STATE {
            return Err(SdError::InitFailed);
        }
        
        // Check voltage range (CMD8)
        self.spi.cs_low();
        let response = self.send_command(CMD8, 0x1AA)?;
        
        if response == R1_IDLE_STATE {
            // SD Card v2.0+
            let mut ocr = [0u8; 4];
            for i in 0..4 {
                ocr[i] = self.spi.transfer(0xFF);
            }
            self.spi.cs_high();
            
            // Verify voltage and pattern
            if (ocr[2] & 0x0F) != 0x01 || ocr[3] != 0xAA {
                return Err(SdError::InvalidResponse);
            }
            
            // Initialize with ACMD41 (HCS bit set)
            for _ in 0..INIT_TIMEOUT {
                self.spi.cs_low();
                self.send_command(CMD55, 0)?;
                let r = self.send_command(ACMD41, 0x40000000)?;
                self.spi.cs_high();
                
                if r == R1_READY {
                    break;
                }
                self.spi.delay_ms(1);
            }
            
            // Read OCR to determine card capacity
            self.spi.cs_low();
            let response = self.send_command(CMD58, 0)?;
            if response == R1_READY {
                let mut ocr = [0u8; 4];
                for i in 0..4 {
                    ocr[i] = self.spi.transfer(0xFF);
                }
                
                // Check CCS bit (Card Capacity Status)
                self.card_type = if ocr[0] & 0x40 != 0 {
                    CardType::Sdhc
                } else {
                    CardType::SdV2
                };
            }
            self.spi.cs_high();
        } else {
            // SD Card v1.x
            self.spi.cs_high();
            self.card_type = CardType::SdV1;
            
            // Initialize with ACMD41
            for _ in 0..INIT_TIMEOUT {
                self.spi.cs_low();
                self.send_command(CMD55, 0)?;
                let r = self.send_command(ACMD41, 0)?;
                self.spi.cs_high();
                
                if r == R1_READY {
                    break;
                }
                self.spi.delay_ms(1);
            }
        }
        
        self.initialized = true;
        Ok(())
    }
    
    // Read a single 512-byte block
    pub fn read_block(&mut self, block_addr: u32, buffer: &mut [u8; 512]) 
        -> Result<(), SdError> {
        
        if !self.initialized {
            return Err(SdError::NotInitialized);
        }
        
        // Convert address for non-SDHC cards
        let addr = if self.card_type == CardType::Sdhc {
            block_addr
        } else {
            block_addr * 512
        };
        
        self.spi.cs_low();
        let response = self.send_command(CMD17, addr)?;
        
        if response != R1_READY {
            self.spi.cs_high();
            return Err(SdError::CommandFailed);
        }
        
        // Wait for start block token
        let mut timeout = READ_TIMEOUT;
        loop {
            let token = self.spi.transfer(0xFF);
            if token == START_BLOCK_TOKEN {
                break;
            }
            timeout -= 1;
            if timeout == 0 {
                self.spi.cs_high();
                return Err(SdError::ReadTimeout);
            }
        }
        
        // Read 512 bytes
        for byte in buffer.iter_mut() {
            *byte = self.spi.transfer(0xFF);
        }
        
        // Read CRC (ignored)
        self.spi.transfer(0xFF);
        self.spi.transfer(0xFF);
        
        self.spi.cs_high();
        self.spi.transfer(0xFF); // Extra clocks
        
        Ok(())
    }
    
    // Write a single 512-byte block
    pub fn write_block(&mut self, block_addr: u32, buffer: &[u8; 512]) 
        -> Result<(), SdError> {
        
        if !self.initialized {
            return Err(SdError::NotInitialized);
        }
        
        // Convert address for non-SDHC cards
        let addr = if self.card_type == CardType::Sdhc {
            block_addr
        } else {
            block_addr * 512
        };
        
        self.spi.cs_low();
        let response = self.send_command(CMD24, addr)?;
        
        if response != R1_READY {
            self.spi.cs_high();
            return Err(SdError::CommandFailed);
        }
        
        // Send start block token
        self.spi.transfer(START_BLOCK_TOKEN);
        
        // Write 512 bytes
        for &byte in buffer {
            self.spi.transfer(byte);
        }
        
        // Send dummy CRC
        self.spi.transfer(0xFF);
        self.spi.transfer(0xFF);
        
        // Check data response
        let response = self.spi.transfer(0xFF);
        if (response & 0x1F) != DATA_ACCEPTED {
            self.spi.cs_high();
            return Err(SdError::WriteTimeout);
        }
        
        // Wait while card is busy
        let mut timeout = WRITE_TIMEOUT;
        while self.spi.transfer(0xFF) == 0x00 {
            timeout -= 1;
            if timeout == 0 {
                self.spi.cs_high();
                return Err(SdError::WriteTimeout);
            }
        }
        
        self.spi.cs_high();
        self.spi.transfer(0xFF); // Extra clocks
        
        Ok(())
    }
    
    pub fn card_type(&self) -> CardType {
        self.card_type
    }
    
    pub fn is_initialized(&self) -> bool {
        self.initialized
    }
}

// Data logger implementation
pub struct DataLogger<SPI> {
    sd: SdCard<SPI>,
    current_block: u32,
    buffer: [u8; 512],
    buffer_offset: usize,
}

impl<SPI: SpiInterface> DataLogger<SPI> {
    pub fn new(spi: SPI) -> Self {
        DataLogger {
            sd: SdCard::new(spi),
            current_block: 0,
            buffer: [0; 512],
            buffer_offset: 0,
        }
    }
    
    pub fn init(&mut self) -> Result<(), SdError> {
        self.sd.init()
    }
    
    pub fn log_entry(&mut self, entry: &[u8]) -> Result<(), SdError> {
        // Check if entry fits in current buffer
        if self.buffer_offset + entry.len() > 512 {
            self.flush()?;
        }
        
        // Copy entry to buffer
        self.buffer[self.buffer_offset..self.buffer_offset + entry.len()]
            .copy_from_slice(entry);
        self.buffer_offset += entry.len();
        
        Ok(())
    }
    
    pub fn flush(&mut self) -> Result<(), SdError> {
        if self.buffer_offset == 0 {
            return Ok(());
        }
        
        // Fill remainder with zeros
        for i in self.buffer_offset..512 {
            self.buffer[i] = 0;
        }
        
        // Write to SD card
        self.sd.write_block(self.current_block, &self.buffer)?;
        
        // Prepare for next block
        self.current_block += 1;
        self.buffer_offset = 0;
        self.buffer.fill(0);
        
        Ok(())
    }
}
```

## Summary

**SD Card SPI Mode** provides a practical, accessible method for data storage in embedded systems:

### Key Advantages
- **Simple Interface**: Only 4 wires (MISO, MOSI, SCK, CS) required
- **Universal Compatibility**: Works with any microcontroller supporting SPI
- **Cost-Effective**: SD cards are inexpensive and widely available
- **Large Capacity**: Modern cards offer gigabytes to terabytes of storage
- **Non-volatile**: Data persists without power

### Common Applications
- **Data Logging**: Temperature, pressure, accelerometer readings
- **Configuration Storage**: System settings and calibration data
- **Audio Recording**: WAV file storage for voice recorders
- **Image Capture**: JPEG storage for embedded cameras
- **Firmware Updates**: Storing and loading new firmware versions

### Performance Considerations
- **Speed**: SPI mode is slower than native SD mode (typically 400kHz-25MHz)
- **Block Operations**: Reading/writing in 512-byte blocks is most efficient
- **Wear Leveling**: Consider FAT file system to distribute writes
- **Power Consumption**: Disable card when not in use to save power

### Best Practices
1. Always check return values and handle errors
2. Implement proper timeout mechanisms
3. Use buffering to minimize write operations
4. Implement a file system (FAT32) for complex applications
5. Add CRC checking for critical data integrity
6. Consider wear leveling for frequently updated data

This guide provides the foundation for implementing robust SD card storage in embedded applications, enabling sophisticated data logging and storage capabilities with minimal hardware complexity.