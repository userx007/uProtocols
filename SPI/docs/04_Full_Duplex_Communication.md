# Full Duplex Communication in SPI

## Detailed Description

Full duplex communication is one of the defining characteristics of the Serial Peripheral Interface (SPI) protocol. Unlike half-duplex protocols where data flows in only one direction at a time, **full duplex allows simultaneous bidirectional data transfer** between the master and slave devices on separate data lines (MOSI and MISO).

### How It Works

In SPI's full duplex mode:
- **MOSI (Master Out Slave In)**: The master transmits data to the slave
- **MISO (Master In Slave Out)**: The slave transmits data to the master
- **Both happen simultaneously**: Every clock cycle exchanges data in both directions

This means that when the master sends a byte, it simultaneously receives a byte from the slave. This is fundamentally different from protocols like I2C or UART where communication is typically half-duplex or requires turn-taking.

### Protocol Design Implications

1. **Data Exchange Pattern**: Every transmission is inherently a data exchange. Even if you only want to read from a slave, you must send dummy bytes. Conversely, when writing to a slave, you receive data (which may be ignored).

2. **Efficiency**: Full duplex enables higher throughput as both devices can communicate simultaneously without waiting for line turnaround.

3. **Register Access**: Common pattern for accessing device registers involves sending a command/address byte while ignoring the received byte, then sending dummy bytes while reading the response.

4. **Clock Control**: The master controls the clock, so even though communication is bidirectional, the master dictates the timing of all exchanges.

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware-specific SPI register definitions (example for ARM Cortex-M)
#define SPI1_DR   (*(volatile uint32_t*)0x4001300C)  // Data register
#define SPI1_SR   (*(volatile uint32_t*)0x40013008)  // Status register
#define SPI_SR_TXE  (1 << 1)  // Transmit buffer empty
#define SPI_SR_RXNE (1 << 0)  // Receive buffer not empty
#define SPI_SR_BSY  (1 << 7)  // Busy flag

/**
 * Full duplex SPI transfer function
 * Sends and receives data simultaneously
 */
uint8_t spi_transfer_byte(uint8_t tx_data) {
    // Wait until transmit buffer is empty
    while (!(SPI1_SR & SPI_SR_TXE));
    
    // Write data to be transmitted
    SPI1_DR = tx_data;
    
    // Wait until data is received
    while (!(SPI1_SR & SPI_SR_RXNE));
    
    // Read and return received data
    return (uint8_t)SPI1_DR;
}

/**
 * Transfer multiple bytes in full duplex mode
 * tx_buffer: data to send
 * rx_buffer: buffer for received data
 * length: number of bytes to transfer
 */
void spi_transfer_buffer(const uint8_t* tx_buffer, uint8_t* rx_buffer, size_t length) {
    for (size_t i = 0; i < length; i++) {
        rx_buffer[i] = spi_transfer_byte(tx_buffer[i]);
    }
    
    // Wait until SPI is not busy
    while (SPI1_SR & SPI_SR_BSY);
}

/**
 * Read from SPI slave (send dummy bytes)
 */
void spi_read(uint8_t* rx_buffer, size_t length) {
    for (size_t i = 0; i < length; i++) {
        rx_buffer[i] = spi_transfer_byte(0xFF);  // Send dummy byte
    }
}

/**
 * Write to SPI slave (ignore received data)
 */
void spi_write(const uint8_t* tx_buffer, size_t length) {
    for (size_t i = 0; i < length; i++) {
        (void)spi_transfer_byte(tx_buffer[i]);  // Discard received data
    }
}

/**
 * Example: Read from a sensor register
 * Demonstrates the read command pattern
 */
uint8_t read_sensor_register(uint8_t reg_address) {
    uint8_t tx_data[2];
    uint8_t rx_data[2];
    
    // First byte: read command (register address with read bit)
    tx_data[0] = reg_address | 0x80;  // Set MSB for read operation
    tx_data[1] = 0x00;  // Dummy byte to clock in response
    
    spi_transfer_buffer(tx_data, rx_data, 2);
    
    // First received byte is usually garbage during command transmission
    // Second byte contains the register value
    return rx_data[1];
}

/**
 * Example: Simultaneous read-write transaction
 * Useful for devices that stream data continuously
 */
typedef struct {
    uint8_t command;
    uint8_t data;
} spi_packet_t;

spi_packet_t spi_exchange_packet(spi_packet_t tx_packet) {
    spi_packet_t rx_packet;
    uint8_t* tx_ptr = (uint8_t*)&tx_packet;
    uint8_t* rx_ptr = (uint8_t*)&rx_packet;
    
    spi_transfer_buffer(tx_ptr, rx_ptr, sizeof(spi_packet_t));
    
    return rx_packet;
}
```

### Rust Implementation

```rust
/// SPI full duplex transfer trait
pub trait SpiFullDuplex {
    type Error;
    
    /// Transfer a single byte (send and receive simultaneously)
    fn transfer_byte(&mut self, tx_byte: u8) -> Result<u8, Self::Error>;
    
    /// Transfer multiple bytes
    fn transfer<'w>(&mut self, buffer: &'w mut [u8]) -> Result<&'w [u8], Self::Error>;
    
    /// Transfer with separate TX and RX buffers
    fn transfer_split(&mut self, tx_buffer: &[u8], rx_buffer: &mut [u8]) 
        -> Result<(), Self::Error>;
}

/// Example SPI implementation for embedded hardware
pub struct SpiDevice {
    // Hardware register pointers would go here
    data_register: *mut u32,
    status_register: *const u32,
}

impl SpiDevice {
    const SR_TXE: u32 = 1 << 1;
    const SR_RXNE: u32 = 1 << 0;
    const SR_BSY: u32 = 1 << 7;
    
    /// Create new SPI device instance
    pub fn new(base_address: usize) -> Self {
        Self {
            data_register: (base_address + 0x0C) as *mut u32,
            status_register: (base_address + 0x08) as *const u32,
        }
    }
    
    /// Check if transmit buffer is empty
    fn is_tx_empty(&self) -> bool {
        unsafe { (*self.status_register) & Self::SR_TXE != 0 }
    }
    
    /// Check if receive buffer has data
    fn is_rx_not_empty(&self) -> bool {
        unsafe { (*self.status_register) & Self::SR_RXNE != 0 }
    }
    
    /// Check if SPI is busy
    fn is_busy(&self) -> bool {
        unsafe { (*self.status_register) & Self::SR_BSY != 0 }
    }
}

#[derive(Debug)]
pub enum SpiError {
    Timeout,
    HardwareError,
}

impl SpiFullDuplex for SpiDevice {
    type Error = SpiError;
    
    fn transfer_byte(&mut self, tx_byte: u8) -> Result<u8, Self::Error> {
        // Wait for TX buffer to be empty
        let mut timeout = 10000;
        while !self.is_tx_empty() {
            timeout -= 1;
            if timeout == 0 {
                return Err(SpiError::Timeout);
            }
        }
        
        // Write data to transmit
        unsafe { 
            *self.data_register = tx_byte as u32;
        }
        
        // Wait for RX buffer to have data
        timeout = 10000;
        while !self.is_rx_not_empty() {
            timeout -= 1;
            if timeout == 0 {
                return Err(SpiError::Timeout);
            }
        }
        
        // Read and return received data
        let rx_byte = unsafe { *self.data_register as u8 };
        Ok(rx_byte)
    }
    
    fn transfer<'w>(&mut self, buffer: &'w mut [u8]) -> Result<&'w [u8], Self::Error> {
        for byte in buffer.iter_mut() {
            *byte = self.transfer_byte(*byte)?;
        }
        
        // Wait until not busy
        let mut timeout = 10000;
        while self.is_busy() {
            timeout -= 1;
            if timeout == 0 {
                return Err(SpiError::Timeout);
            }
        }
        
        Ok(buffer)
    }
    
    fn transfer_split(&mut self, tx_buffer: &[u8], rx_buffer: &mut [u8]) 
        -> Result<(), Self::Error> {
        let len = tx_buffer.len().min(rx_buffer.len());
        
        for i in 0..len {
            rx_buffer[i] = self.transfer_byte(tx_buffer[i])?;
        }
        
        Ok(())
    }
}

/// High-level SPI operations
pub struct SpiOperations<T: SpiFullDuplex> {
    device: T,
}

impl<T: SpiFullDuplex> SpiOperations<T> {
    pub fn new(device: T) -> Self {
        Self { device }
    }
    
    /// Read-only operation (sends dummy bytes)
    pub fn read(&mut self, buffer: &mut [u8]) -> Result<(), T::Error> {
        for byte in buffer.iter_mut() {
            *byte = self.device.transfer_byte(0xFF)?;
        }
        Ok(())
    }
    
    /// Write-only operation (discards received data)
    pub fn write(&mut self, buffer: &[u8]) -> Result<(), T::Error> {
        for &byte in buffer {
            let _ = self.device.transfer_byte(byte)?;
        }
        Ok(())
    }
    
    /// Read from a device register
    pub fn read_register(&mut self, reg_addr: u8) -> Result<u8, T::Error> {
        // Send read command (address with read bit set)
        let _ = self.device.transfer_byte(reg_addr | 0x80)?;
        
        // Send dummy byte and receive register value
        let value = self.device.transfer_byte(0x00)?;
        
        Ok(value)
    }
    
    /// Write to a device register
    pub fn write_register(&mut self, reg_addr: u8, value: u8) -> Result<(), T::Error> {
        // Send write command
        let _ = self.device.transfer_byte(reg_addr & 0x7F)?;
        
        // Send register value
        let _ = self.device.transfer_byte(value)?;
        
        Ok(())
    }
}

/// Example: Streaming full duplex communication
pub struct StreamingSensor<T: SpiFullDuplex> {
    spi: T,
}

impl<T: SpiFullDuplex> StreamingSensor<T> {
    pub fn new(spi: T) -> Self {
        Self { spi }
    }
    
    /// Simultaneously send command and receive sensor data
    pub fn exchange_data(&mut self, command: u8) -> Result<u8, T::Error> {
        // In full duplex, both send command and receive data happen together
        self.spi.transfer_byte(command)
    }
    
    /// Continuous streaming: send commands while reading previous results
    pub fn stream_exchange(&mut self, commands: &[u8], results: &mut [u8]) 
        -> Result<(), T::Error> {
        self.spi.transfer_split(commands, results)
    }
}
```

## Summary

**Full duplex communication** is SPI's ability to transmit and receive data simultaneously on separate data lines (MOSI and MISO). This fundamental characteristic distinguishes SPI from many other serial protocols and has important implications:

- **Every SPI transaction is bidirectional**: Data flows in both directions during every clock cycle
- **Master controls timing**: Even though communication is bidirectional, the master's clock controls all exchanges
- **Dummy data is common**: Read operations require sending dummy bytes; write operations receive (often ignored) data
- **Higher efficiency**: Simultaneous bidirectional transfer enables better throughput than half-duplex protocols
- **Protocol patterns**: Common operations include register reads (command + dummy byte), register writes (command + data), and true simultaneous exchanges for streaming applications

Understanding full duplex operation is essential for efficient SPI protocol design and implementation, particularly when developing drivers for sensors, memory devices, and other SPI peripherals.