# I2C Clock Stretching: Detailed Technical Guide

## Overview

Clock stretching is a mechanism in I2C (Inter-Integrated Circuit) communication that allows a slave device to slow down or pause the bus master by holding the SCL (Serial Clock Line) LOW. This gives the slave additional time to process data, prepare responses, or complete internal operations before continuing communication.

## How Clock Stretching Works

### Basic Principle

In standard I2C operation:
1. The master controls the SCL line and generates clock pulses
2. During clock stretching, the slave can hold SCL LOW after the master releases it
3. The master detects this and waits until the slave releases SCL before continuing
4. This implements a hardware-based flow control mechanism

### Clock Stretching Scenarios

**Common situations requiring clock stretching:**
- Slave needs time to process received data
- Slave is fetching data from slow memory or peripherals
- Slave is performing analog-to-digital conversions
- Slave microcontroller is handling interrupts
- Slave needs to complete write operations to flash/EEPROM

### Timing Diagram

```
Master releases SCL HIGH:     ___________
                          ___|           |___
                          
Slave holds SCL LOW:      _______
(clock stretching)            |_______|_______
                          
                          <-stretch->
                          period
```

## Implementation Details

### Master Side Considerations

**Detection Logic:**
The master must implement SCL monitoring:
1. After releasing SCL HIGH, check if it actually went HIGH
2. If SCL remains LOW, a slave is stretching
3. Wait (with timeout) until SCL goes HIGH
4. Continue normal operation

**Timeout Handling:**
Masters should implement timeouts to detect:
- Malfunctioning slaves holding SCL indefinitely
- Bus lockup conditions
- Disconnected or powered-off slaves

### Slave Side Considerations

**When to Stretch:**
- After ACK/NACK bit during data reception
- Before sending data bytes
- During address matching if processing is needed

**Duration Limits:**
- Keep stretching periods as short as possible
- Some masters have strict timeout limits (typically 1-25ms)
- Extended stretching can cause bus congestion

## Code Examples

### C/C++ Implementation

#### Master Implementation with Clock Stretching Detection

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware abstraction macros (adjust for your platform)
#define SCL_LOW()       GPIO_CLR(SCL_PIN)
#define SCL_HIGH()      GPIO_SET(SCL_PIN)
#define SCL_READ()      GPIO_READ(SCL_PIN)
#define SDA_LOW()       GPIO_CLR(SDA_PIN)
#define SDA_HIGH()      GPIO_SET(SDA_PIN)
#define SDA_READ()      GPIO_READ(SDA_PIN)
#define DELAY_US(x)     delay_microseconds(x)

// Configuration
#define I2C_CLOCK_STRETCH_TIMEOUT_US  10000  // 10ms timeout
#define I2C_HALF_PERIOD_US            5      // For 100kHz I2C

typedef enum {
    I2C_OK = 0,
    I2C_ERROR_NACK,
    I2C_ERROR_TIMEOUT,
    I2C_ERROR_BUS_BUSY
} i2c_status_t;

/**
 * Wait for SCL to go HIGH (clock stretching support)
 * Returns true if successful, false on timeout
 */
bool i2c_wait_for_scl_high(void) {
    uint32_t timeout = I2C_CLOCK_STRETCH_TIMEOUT_US;
    
    while (SCL_READ() == 0) {
        DELAY_US(1);
        if (--timeout == 0) {
            return false;  // Clock stretch timeout
        }
    }
    return true;
}

/**
 * Generate SCL clock pulse with clock stretching support
 */
i2c_status_t i2c_clock_pulse(void) {
    // Release SCL (set HIGH)
    SCL_HIGH();
    DELAY_US(I2C_HALF_PERIOD_US);
    
    // Wait for slave to release SCL if stretching
    if (!i2c_wait_for_scl_high()) {
        return I2C_ERROR_TIMEOUT;
    }
    
    DELAY_US(I2C_HALF_PERIOD_US);
    
    // Pull SCL LOW for next phase
    SCL_LOW();
    DELAY_US(I2C_HALF_PERIOD_US);
    
    return I2C_OK;
}

/**
 * Write a byte with clock stretching support
 */
i2c_status_t i2c_write_byte(uint8_t data) {
    i2c_status_t status;
    
    // Send 8 bits, MSB first
    for (int i = 7; i >= 0; i--) {
        // Set data bit
        if (data & (1 << i)) {
            SDA_HIGH();
        } else {
            SDA_LOW();
        }
        DELAY_US(I2C_HALF_PERIOD_US);
        
        // Clock pulse (slave may stretch here)
        status = i2c_clock_pulse();
        if (status != I2C_OK) {
            return status;
        }
    }
    
    // Release SDA for ACK/NACK
    SDA_HIGH();
    DELAY_US(I2C_HALF_PERIOD_US);
    
    // Clock pulse for ACK bit (slave may stretch here)
    SCL_HIGH();
    DELAY_US(I2C_HALF_PERIOD_US);
    
    if (!i2c_wait_for_scl_high()) {
        return I2C_ERROR_TIMEOUT;
    }
    
    // Read ACK/NACK
    bool ack = (SDA_READ() == 0);
    DELAY_US(I2C_HALF_PERIOD_US);
    
    SCL_LOW();
    DELAY_US(I2C_HALF_PERIOD_US);
    
    return ack ? I2C_OK : I2C_ERROR_NACK;
}

/**
 * Read a byte with clock stretching support
 */
i2c_status_t i2c_read_byte(uint8_t *data, bool send_ack) {
    uint8_t byte = 0;
    i2c_status_t status;
    
    // Release SDA so slave can control it
    SDA_HIGH();
    
    // Read 8 bits
    for (int i = 7; i >= 0; i--) {
        DELAY_US(I2C_HALF_PERIOD_US);
        
        // Clock HIGH (slave may stretch before providing data)
        SCL_HIGH();
        DELAY_US(I2C_HALF_PERIOD_US);
        
        if (!i2c_wait_for_scl_high()) {
            return I2C_ERROR_TIMEOUT;
        }
        
        // Read bit
        if (SDA_READ()) {
            byte |= (1 << i);
        }
        
        DELAY_US(I2C_HALF_PERIOD_US);
        SCL_LOW();
        DELAY_US(I2C_HALF_PERIOD_US);
    }
    
    *data = byte;
    
    // Send ACK or NACK
    if (send_ack) {
        SDA_LOW();   // ACK
    } else {
        SDA_HIGH();  // NACK
    }
    DELAY_US(I2C_HALF_PERIOD_US);
    
    status = i2c_clock_pulse();
    
    SDA_HIGH();  // Release SDA
    
    return status;
}
```

#### Slave Implementation with Clock Stretching

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware specific - adjust for your platform
#define I2C_SLAVE_ADDR  0x48

// State machine for slave
typedef enum {
    STATE_IDLE,
    STATE_ADDR_MATCHED,
    STATE_DATA_RX,
    STATE_DATA_TX
} i2c_slave_state_t;

volatile i2c_slave_state_t slave_state = STATE_IDLE;
volatile uint8_t rx_buffer[32];
volatile uint8_t tx_buffer[32];
volatile uint8_t rx_index = 0;
volatile uint8_t tx_index = 0;
volatile bool data_ready = false;

/**
 * Perform clock stretching (hold SCL LOW)
 * Call this function when slave needs processing time
 */
void i2c_slave_stretch_clock(void) {
    // Configure SCL as output and pull LOW
    // This holds the clock line until released
    SCL_AS_OUTPUT();
    SCL_LOW();
}

/**
 * Release clock stretching
 */
void i2c_slave_release_clock(void) {
    // Configure SCL as input (or open-drain HIGH)
    // This allows master to control the clock
    SCL_AS_INPUT();
}

/**
 * Simulated data processing that requires clock stretching
 */
void i2c_slave_process_data(uint8_t data) {
    // Stretch clock while processing
    i2c_slave_stretch_clock();
    
    // Simulate slow operation (e.g., EEPROM write, ADC read)
    perform_slow_operation(data);
    
    // Processing complete, release clock
    i2c_slave_release_clock();
    
    data_ready = true;
}

/**
 * I2C slave interrupt handler (pseudo-code)
 * Actual implementation depends on your microcontroller
 */
void I2C_Slave_IRQHandler(void) {
    uint32_t status = I2C_GET_STATUS();
    
    if (status & I2C_ADDR_MATCHED) {
        // Address matched
        slave_state = STATE_ADDR_MATCHED;
        rx_index = 0;
        tx_index = 0;
        
        if (status & I2C_READ_REQUEST) {
            // Master wants to read - prepare data
            if (!data_ready) {
                // Need time to prepare data - stretch clock
                i2c_slave_stretch_clock();
                prepare_tx_data();
                i2c_slave_release_clock();
            }
            slave_state = STATE_DATA_TX;
            I2C_SEND_BYTE(tx_buffer[tx_index++]);
        } else {
            // Master wants to write
            slave_state = STATE_DATA_RX;
        }
        I2C_CLEAR_ADDR_FLAG();
    }
    
    if (status & I2C_RX_NOT_EMPTY) {
        // Received data byte
        uint8_t data = I2C_READ_BYTE();
        
        if (rx_index < sizeof(rx_buffer)) {
            rx_buffer[rx_index++] = data;
            
            // If processing needed, stretch clock
            if (needs_processing(data)) {
                i2c_slave_process_data(data);
            }
        }
    }
    
    if (status & I2C_TX_EMPTY) {
        // Master requesting more data
        if (tx_index < sizeof(tx_buffer)) {
            I2C_SEND_BYTE(tx_buffer[tx_index++]);
        } else {
            // No more data, send dummy byte
            I2C_SEND_BYTE(0xFF);
        }
    }
    
    if (status & I2C_STOP_DETECTED) {
        // Communication ended
        slave_state = STATE_IDLE;
        I2C_CLEAR_STOP_FLAG();
    }
}
```

### Rust Implementation

#### Master Implementation with Clock Stretching

```rust
use std::time::{Duration, Instant};

/// I2C error types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cError {
    Nack,
    ClockStretchTimeout,
    BusBusy,
    InvalidAddress,
}

/// I2C master configuration
pub struct I2cConfig {
    pub clock_frequency_hz: u32,
    pub clock_stretch_timeout: Duration,
}

impl Default for I2cConfig {
    fn default() -> Self {
        Self {
            clock_frequency_hz: 100_000,  // 100 kHz
            clock_stretch_timeout: Duration::from_millis(10),
        }
    }
}

/// Trait for GPIO operations (platform-specific implementation)
pub trait GpioPin {
    fn set_high(&mut self);
    fn set_low(&mut self);
    fn is_high(&self) -> bool;
    fn is_low(&self) -> bool;
}

/// I2C master with clock stretching support
pub struct I2cMaster<SCL: GpioPin, SDA: GpioPin> {
    scl: SCL,
    sda: SDA,
    config: I2cConfig,
    half_period: Duration,
}

impl<SCL: GpioPin, SDA: GpioPin> I2cMaster<SCL, SDA> {
    /// Create new I2C master instance
    pub fn new(scl: SCL, sda: SDA, config: I2cConfig) -> Self {
        let half_period = Duration::from_nanos(
            500_000_000 / config.clock_frequency_hz as u64
        );
        
        Self {
            scl,
            sda,
            config,
            half_period,
        }
    }
    
    /// Wait for SCL to go high with clock stretching support
    fn wait_for_scl_high(&self) -> Result<(), I2cError> {
        let start = Instant::now();
        
        while self.scl.is_low() {
            if start.elapsed() > self.config.clock_stretch_timeout {
                return Err(I2cError::ClockStretchTimeout);
            }
            // Small delay to avoid tight polling
            std::thread::sleep(Duration::from_micros(1));
        }
        
        Ok(())
    }
    
    /// Delay helper
    fn delay(&self, duration: Duration) {
        std::thread::sleep(duration);
    }
    
    /// Generate a clock pulse with stretching support
    fn clock_pulse(&mut self) -> Result<(), I2cError> {
        // Release SCL
        self.scl.set_high();
        self.delay(self.half_period);
        
        // Wait for slave to release if stretching
        self.wait_for_scl_high()?;
        
        self.delay(self.half_period);
        
        // Pull SCL low
        self.scl.set_low();
        self.delay(self.half_period);
        
        Ok(())
    }
    
    /// Send start condition
    pub fn start(&mut self) -> Result<(), I2cError> {
        // Initial state: both lines high
        self.sda.set_high();
        self.scl.set_high();
        self.delay(self.half_period);
        
        // Check if bus is free
        if self.sda.is_low() || self.scl.is_low() {
            return Err(I2cError::BusBusy);
        }
        
        // START: SDA falls while SCL is high
        self.sda.set_low();
        self.delay(self.half_period);
        self.scl.set_low();
        self.delay(self.half_period);
        
        Ok(())
    }
    
    /// Send stop condition
    pub fn stop(&mut self) {
        self.sda.set_low();
        self.delay(self.half_period);
        self.scl.set_high();
        self.delay(self.half_period);
        
        // STOP: SDA rises while SCL is high
        self.sda.set_high();
        self.delay(self.half_period);
    }
    
    /// Write a byte with clock stretching support
    pub fn write_byte(&mut self, data: u8) -> Result<bool, I2cError> {
        // Send 8 bits, MSB first
        for i in (0..8).rev() {
            if (data & (1 << i)) != 0 {
                self.sda.set_high();
            } else {
                self.sda.set_low();
            }
            self.delay(self.half_period);
            
            // Clock pulse (slave may stretch here)
            self.clock_pulse()?;
        }
        
        // Release SDA for ACK/NACK
        self.sda.set_high();
        self.delay(self.half_period);
        
        // Clock high for ACK bit
        self.scl.set_high();
        self.delay(self.half_period);
        
        // Wait for clock stretching
        self.wait_for_scl_high()?;
        
        // Read ACK (LOW = ACK, HIGH = NACK)
        let ack = self.sda.is_low();
        
        self.delay(self.half_period);
        self.scl.set_low();
        self.delay(self.half_period);
        
        Ok(ack)
    }
    
    /// Read a byte with clock stretching support
    pub fn read_byte(&mut self, send_ack: bool) -> Result<u8, I2cError> {
        let mut byte: u8 = 0;
        
        // Release SDA
        self.sda.set_high();
        
        // Read 8 bits
        for i in (0..8).rev() {
            self.delay(self.half_period);
            
            // Clock high
            self.scl.set_high();
            self.delay(self.half_period);
            
            // Wait for slave if stretching
            self.wait_for_scl_high()?;
            
            // Read bit
            if self.sda.is_high() {
                byte |= 1 << i;
            }
            
            self.delay(self.half_period);
            self.scl.set_low();
            self.delay(self.half_period);
        }
        
        // Send ACK or NACK
        if send_ack {
            self.sda.set_low();  // ACK
        } else {
            self.sda.set_high(); // NACK
        }
        self.delay(self.half_period);
        
        self.clock_pulse()?;
        
        self.sda.set_high(); // Release SDA
        
        Ok(byte)
    }
    
    /// Write data to slave device
    pub fn write(&mut self, addr: u8, data: &[u8]) -> Result<(), I2cError> {
        if addr > 0x7F {
            return Err(I2cError::InvalidAddress);
        }
        
        self.start()?;
        
        // Send address with write bit
        let ack = self.write_byte((addr << 1) | 0)?;
        if !ack {
            self.stop();
            return Err(I2cError::Nack);
        }
        
        // Send data bytes
        for &byte in data {
            let ack = self.write_byte(byte)?;
            if !ack {
                self.stop();
                return Err(I2cError::Nack);
            }
        }
        
        self.stop();
        Ok(())
    }
    
    /// Read data from slave device
    pub fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), I2cError> {
        if addr > 0x7F {
            return Err(I2cError::InvalidAddress);
        }
        
        self.start()?;
        
        // Send address with read bit
        let ack = self.write_byte((addr << 1) | 1)?;
        if !ack {
            self.stop();
            return Err(I2cError::Nack);
        }
        
        // Read data bytes
        let len = buffer.len();
        for (i, byte_ref) in buffer.iter_mut().enumerate() {
            let send_ack = i < len - 1;  // NACK on last byte
            *byte_ref = self.read_byte(send_ack)?;
        }
        
        self.stop();
        Ok(())
    }
}
```

#### Rust Slave Implementation (Embedded HAL)

```rust
use core::cell::RefCell;
use cortex_m::interrupt::Mutex;

/// Slave state machine
#[derive(Debug, Clone, Copy, PartialEq)]
enum SlaveState {
    Idle,
    AddressMatched,
    ReceivingData,
    SendingData,
}

/// I2C Slave with clock stretching capability
pub struct I2cSlave<I2C> {
    i2c: I2C,
    address: u8,
    rx_buffer: [u8; 32],
    tx_buffer: [u8; 32],
    rx_index: usize,
    tx_index: usize,
    state: SlaveState,
}

impl<I2C> I2cSlave<I2C>
where
    I2C: I2cSlaveHardware,
{
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self {
            i2c,
            address,
            rx_buffer: [0; 32],
            tx_buffer: [0; 32],
            rx_index: 0,
            tx_index: 0,
            state: SlaveState::Idle,
        }
    }
    
    /// Enable clock stretching
    pub fn enable_clock_stretching(&mut self) {
        self.i2c.enable_clock_stretch();
    }
    
    /// Disable clock stretching
    pub fn disable_clock_stretching(&mut self) {
        self.i2c.disable_clock_stretch();
    }
    
    /// Process received data (may trigger clock stretching)
    fn process_received_data(&mut self, data: u8) {
        // Stretch clock while processing
        self.i2c.enable_clock_stretch();
        
        // Simulate slow operation
        self.perform_slow_operation(data);
        
        // Release clock
        self.i2c.disable_clock_stretch();
    }
    
    /// Prepare transmit data (may trigger clock stretching)
    fn prepare_transmit_data(&mut self) {
        // Stretch clock while preparing
        self.i2c.enable_clock_stretch();
        
        // Fetch data from slow source
        self.fetch_data_from_sensor();
        
        // Release clock
        self.i2c.disable_clock_stretch();
    }
    
    fn perform_slow_operation(&mut self, data: u8) {
        // Placeholder for actual processing
        self.rx_buffer[self.rx_index] = data;
        self.rx_index += 1;
    }
    
    fn fetch_data_from_sensor(&mut self) {
        // Placeholder for data fetch
        self.tx_buffer[0] = 0xAB;
        self.tx_buffer[1] = 0xCD;
        self.tx_index = 0;
    }
    
    /// Handle I2C events (called from interrupt handler)
    pub fn handle_event(&mut self, event: I2cEvent) {
        match event {
            I2cEvent::AddressMatched(direction) => {
                self.state = SlaveState::AddressMatched;
                self.rx_index = 0;
                self.tx_index = 0;
                
                if direction == TransferDirection::Read {
                    // Master wants to read
                    self.prepare_transmit_data();
                    self.state = SlaveState::SendingData;
                    self.i2c.send_byte(self.tx_buffer[self.tx_index]);
                    self.tx_index += 1;
                } else {
                    // Master wants to write
                    self.state = SlaveState::ReceivingData;
                }
            }
            
            I2cEvent::DataReceived(data) => {
                if self.rx_index < self.rx_buffer.len() {
                    // Process data (may stretch clock)
                    self.process_received_data(data);
                }
            }
            
            I2cEvent::TransmitRequested => {
                if self.tx_index < self.tx_buffer.len() {
                    self.i2c.send_byte(self.tx_buffer[self.tx_index]);
                    self.tx_index += 1;
                } else {
                    // Send dummy byte
                    self.i2c.send_byte(0xFF);
                }
            }
            
            I2cEvent::StopCondition => {
                self.state = SlaveState::Idle;
            }
        }
    }
}

/// Hardware abstraction trait for I2C slave
pub trait I2cSlaveHardware {
    fn enable_clock_stretch(&mut self);
    fn disable_clock_stretch(&mut self);
    fn send_byte(&mut self, data: u8);
}

/// Event types from I2C hardware
#[derive(Debug, Clone, Copy)]
pub enum I2cEvent {
    AddressMatched(TransferDirection),
    DataReceived(u8),
    TransmitRequested,
    StopCondition,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum TransferDirection {
    Write,
    Read,
}
```

## Best Practices and Considerations

**For Masters:**
- Always implement clock stretch detection with reasonable timeouts
- Don't assume instantaneous SCL transitions
- Handle timeout errors gracefully
- Consider retry logic for timeout scenarios

**For Slaves:**
- Minimize clock stretching duration
- Only stretch when absolutely necessary
- Test with different master implementations
- Document maximum stretch times in datasheets

**System Design:**
- Some USB-to-I2C adapters don't support clock stretching properly
- Fast Mode Plus (1 MHz) may have limited stretching support
- Check compatibility between all devices on the bus
- Consider alternative flow control if stretching is problematic

**Debugging:**
- Use logic analyzers to measure actual stretch durations
- Monitor for timeout errors in system logs
- Test worst-case processing delays
- Verify behavior under different bus loads

Clock stretching is a powerful but sometimes problematic feature. When implemented correctly, it provides elegant hardware flow control. However, incompatibilities between devices can cause difficult-to-diagnose issues, so thorough testing is essential.