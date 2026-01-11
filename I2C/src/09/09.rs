/// I2C Multi-Master Bus Arbitration Implementation in Rust
/// Demonstrates type-safe collision detection and error handling

use core::fmt;

/// Arbitration result
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ArbitrationResult {
    Won,
    Lost,
    InProgress,
}

/// I2C errors including arbitration loss
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cError {
    ArbitrationLost,
    Nack,
    BusBusy,
    Timeout,
    MaxRetriesExceeded,
}

impl fmt::Display for I2cError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            I2cError::ArbitrationLost => write!(f, "Arbitration lost to another master"),
            I2cError::Nack => write!(f, "NACK received from slave"),
            I2cError::BusBusy => write!(f, "Bus is busy"),
            I2cError::Timeout => write!(f, "Operation timed out"),
            I2cError::MaxRetriesExceeded => write!(f, "Maximum retry attempts exceeded"),
        }
    }
}

/// Hardware abstraction layer trait for I2C pins
pub trait I2cHal {
    fn sda_write(&mut self, state: bool);
    fn sda_read(&self) -> bool;
    fn scl_write(&mut self, state: bool);
    fn scl_read(&self) -> bool;
    fn delay(&self);
}

/// I2C Multi-Master controller
pub struct I2cMultiMaster<HAL: I2cHal> {
    hal: HAL,
    arb_state: ArbitrationResult,
    retry_count: u32,
    max_retries: u32,
    is_master: bool,
}

impl<HAL: I2cHal> I2cMultiMaster<HAL> {
    /// Create a new multi-master I2C controller
    pub fn new(hal: HAL, max_retries: u32) -> Self {
        Self {
            hal,
            arb_state: ArbitrationResult::InProgress,
            retry_count: 0,
            max_retries,
            is_master: true,
        }
    }

    /// Write a single bit with arbitration checking
    fn write_bit_with_arbitration(&mut self, bit: bool) -> Result<(), I2cError> {
        // Release SCL
        self.hal.scl_write(true);
        self.hal.delay();

        // Wait for any clock stretching
        let mut timeout = 1000;
        while !self.hal.scl_read() {
            timeout -= 1;
            if timeout == 0 {
                return Err(I2cError::Timeout);
            }
        }

        // Write the bit
        self.hal.sda_write(bit);
        self.hal.delay();

        // Read back actual bus state
        let actual_bit = self.hal.sda_read();

        // Arbitration check: if we wrote 1 but read 0, we lost
        if bit && !actual_bit {
            self.arb_state = ArbitrationResult::Lost;
            self.is_master = false;
            return Err(I2cError::ArbitrationLost);
        }

        // Pull SCL low for next bit
        self.hal.scl_write(false);
        self.hal.delay();

        Ok(())
    }

    /// Generate START condition with arbitration awareness
    pub fn start(&mut self) -> Result<(), I2cError> {
        // Ensure bus is idle
        self.hal.sda_write(true);
        self.hal.scl_write(true);
        self.hal.delay();

        // Check if bus is busy
        if !self.hal.sda_read() || !self.hal.scl_read() {
            return Err(I2cError::BusBusy);
        }

        // Generate START: SDA falls while SCL is HIGH
        self.hal.sda_write(false);
        self.hal.delay();
        self.hal.scl_write(false);
        self.hal.delay();

        self.arb_state = ArbitrationResult::InProgress;
        Ok(())
    }

    /// Write a byte with arbitration checking
    pub fn write_byte(&mut self, byte: u8) -> Result<bool, I2cError> {
        // Send 8 bits, MSB first
        for i in (0..8).rev() {
            let bit = (byte >> i) & 0x01 != 0;
            self.write_bit_with_arbitration(bit)?;
        }

        // Read ACK/NACK
        self.hal.scl_write(true);
        self.hal.sda_write(true); // Release SDA for ACK
        self.hal.delay();

        let ack = !self.hal.sda_read(); // ACK is LOW

        self.hal.scl_write(false);
        self.hal.delay();

        if ack {
            self.arb_state = ArbitrationResult::Won;
        }

        Ok(ack)
    }

    /// Read a byte from the bus
    pub fn read_byte(&mut self, send_ack: bool) -> Result<u8, I2cError> {
        let mut byte = 0u8;

        // Release SDA so slave can control it
        self.hal.sda_write(true);

        // Read 8 bits
        for _ in 0..8 {
            byte <<= 1;

            self.hal.scl_write(true);
            self.hal.delay();

            if self.hal.sda_read() {
                byte |= 0x01;
            }

            self.hal.scl_write(false);
            self.hal.delay();
        }

        // Send ACK or NACK
        self.hal.sda_write(!send_ack);
        self.hal.scl_write(true);
        self.hal.delay();
        self.hal.scl_write(false);
        self.hal.delay();

        Ok(byte)
    }

    /// Generate STOP condition
    pub fn stop(&mut self) {
        self.hal.sda_write(false);
        self.hal.delay();
        self.hal.scl_write(true);
        self.hal.delay();
        self.hal.sda_write(true);
        self.hal.delay();

        self.is_master = true;
    }

    /// Write data to a slave with automatic retry on arbitration loss
    pub fn write_with_retry(&mut self, addr: u8, data: &[u8]) -> Result<(), I2cError> {
        self.retry_count = 0;

        while self.retry_count < self.max_retries {
            match self.write_internal(addr, data) {
                Ok(()) => return Ok(()),
                Err(I2cError::ArbitrationLost) => {
                    // Arbitration lost, retry after delay
                    self.retry_count += 1;
                    self.hal.delay();
                    continue;
                }
                Err(I2cError::BusBusy) => {
                    // Bus busy, retry
                    self.retry_count += 1;
                    self.hal.delay();
                    continue;
                }
                Err(e) => return Err(e),
            }
        }

        Err(I2cError::MaxRetriesExceeded)
    }

    /// Internal write implementation
    fn write_internal(&mut self, addr: u8, data: &[u8]) -> Result<(), I2cError> {
        // Send START
        self.start()?;

        // Send address with write bit (0)
        let addr_byte = (addr << 1) | 0;
        if !self.write_byte(addr_byte)? {
            self.stop();
            return Err(I2cError::Nack);
        }

        // Send data bytes
        for &byte in data {
            if !self.write_byte(byte)? {
                self.stop();
                return Err(I2cError::Nack);
            }
        }

        self.stop();
        Ok(())
    }

    /// Read data from a slave with automatic retry
    pub fn read_with_retry(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), I2cError> {
        self.retry_count = 0;

        while self.retry_count < self.max_retries {
            match self.read_internal(addr, buffer) {
                Ok(()) => return Ok(()),
                Err(I2cError::ArbitrationLost) | Err(I2cError::BusBusy) => {
                    self.retry_count += 1;
                    self.hal.delay();
                    continue;
                }
                Err(e) => return Err(e),
            }
        }

        Err(I2cError::MaxRetriesExceeded)
    }

    /// Internal read implementation
    fn read_internal(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), I2cError> {
        // Send START
        self.start()?;

        // Send address with read bit (1)
        let addr_byte = (addr << 1) | 1;
        if !self.write_byte(addr_byte)? {
            self.stop();
            return Err(I2cError::Nack);
        }

        // Read bytes
        let last_idx = buffer.len() - 1;
        for (i, byte) in buffer.iter_mut().enumerate() {
            let send_ack = i != last_idx; // NACK on last byte
            *byte = self.read_byte(send_ack)?;
        }

        self.stop();
        Ok(())
    }

    /// Get current arbitration state
    pub fn arbitration_state(&self) -> ArbitrationResult {
        self.arb_state
    }

    /// Check if currently acting as master
    pub fn is_master(&self) -> bool {
        self.is_master
    }
}

// Example mock HAL implementation for testing
pub struct MockHal {
    sda_state: bool,
    scl_state: bool,
    sda_external: bool, // Simulates another master's output
}

impl MockHal {
    pub fn new() -> Self {
        Self {
            sda_state: true,
            scl_state: true,
            sda_external: true,
        }
    }

    pub fn set_external_sda(&mut self, state: bool) {
        self.sda_external = state;
    }
}

impl I2cHal for MockHal {
    fn sda_write(&mut self, state: bool) {
        self.sda_state = state;
    }

    fn sda_read(&self) -> bool {
        // Wired-AND: both internal and external must be HIGH
        self.sda_state && self.sda_external
    }

    fn scl_write(&mut self, state: bool) {
        self.scl_state = state;
    }

    fn scl_read(&self) -> bool {
        self.scl_state
    }

    fn delay(&self) {
        // Mock delay
    }
}

// Example usage
fn example_usage() {
    let hal = MockHal::new();
    let mut i2c = I2cMultiMaster::new(hal, 3);

    let data = [0x12, 0x34, 0x56];
    match i2c.write_with_retry(0x50, &data) {
        Ok(()) => println!("Write successful"),
        Err(e) => println!("Write failed: {}", e),
    }

    let mut buffer = [0u8; 4];
    match i2c.read_with_retry(0x50, &mut buffer) {
        Ok(()) => println!("Read data: {:02X?}", buffer),
        Err(e) => println!("Read failed: {}", e),
    }
}