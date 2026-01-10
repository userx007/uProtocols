/*
 * I2C Error Detection Implementation in Rust
 * Demonstrates type-safe error handling with Rust's Result type
 */

use core::fmt;
use core::time::Duration;

// Error types using Rust's type system
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cError {
    /// No acknowledge received from slave
    Nack,
    /// Communication timeout
    Timeout,
    /// Arbitration lost to another master
    ArbitrationLost,
    /// Bus error (invalid START/STOP condition)
    BusError,
    /// Bus is busy
    Busy,
    /// Buffer overflow
    Overflow,
    /// Invalid parameter provided
    InvalidParam,
    /// Clock stretching timeout
    ClockStretchTimeout,
}

impl fmt::Display for I2cError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            I2cError::Nack => write!(f, "No acknowledge received"),
            I2cError::Timeout => write!(f, "Communication timeout"),
            I2cError::ArbitrationLost => write!(f, "Arbitration lost"),
            I2cError::BusError => write!(f, "Bus error detected"),
            I2cError::Busy => write!(f, "Bus is busy"),
            I2cError::Overflow => write!(f, "Buffer overflow"),
            I2cError::InvalidParam => write!(f, "Invalid parameter"),
            I2cError::ClockStretchTimeout => write!(f, "Clock stretching timeout"),
        }
    }
}

// Transaction state
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cState {
    Idle,
    Transmit,
    Receive,
    Error,
}

// Status register flags
#[repr(u32)]
pub enum StatusFlag {
    StartBit = 1 << 0,
    AddressSent = 1 << 1,
    ByteTransferFinished = 1 << 2,
    TxEmpty = 1 << 7,
    RxNotEmpty = 1 << 6,
    AckFailure = 1 << 10,
    ArbitrationLost = 1 << 9,
    BusError = 1 << 8,
    Timeout = 1 << 14,
}

// Memory-mapped I2C peripheral registers
#[repr(C)]
pub struct I2cRegisters {
    cr1: u32,      // Control register 1
    cr2: u32,      // Control register 2
    sr1: u32,      // Status register 1
    sr2: u32,      // Status register 2
    dr: u32,       // Data register
}

// Control register 1 bits
const CR1_PE: u32 = 1 << 0;    // Peripheral enable
const CR1_START: u32 = 1 << 8; // Start generation
const CR1_STOP: u32 = 1 << 9;  // Stop generation
const CR1_ACK: u32 = 1 << 10;  // Acknowledge enable

// Safe wrapper around I2C peripheral
pub struct I2cBus {
    registers: *mut I2cRegisters,
    timeout_ms: u32,
    state: I2cState,
}

// Implement Send to allow transfer between threads (if needed)
unsafe impl Send for I2cBus {}

impl I2cBus {
    /// Create a new I2C bus instance
    /// 
    /// # Safety
    /// `base_addr` must point to valid I2C peripheral registers
    pub unsafe fn new(base_addr: usize, timeout_ms: u32) -> Self {
        Self {
            registers: base_addr as *mut I2cRegisters,
            timeout_ms,
            state: I2cState::Idle,
        }
    }

    /// Get reference to registers (unsafe operation)
    #[inline]
    fn regs(&self) -> &I2cRegisters {
        unsafe { &*self.registers }
    }

    /// Get mutable reference to registers (unsafe operation)
    #[inline]
    fn regs_mut(&mut self) -> &mut I2cRegisters {
        unsafe { &mut *self.registers }
    }

    /// Check for acknowledge failure
    fn check_ack_error(&mut self) -> Result<(), I2cError> {
        let sr1 = self.regs().sr1;
        if sr1 & (StatusFlag::AckFailure as u32) != 0 {
            // Clear ACK failure flag
            self.regs_mut().sr1 &= !(StatusFlag::AckFailure as u32);
            
            // Generate STOP to release bus
            self.regs_mut().cr1 |= CR1_STOP;
            
            self.state = I2cState::Error;
            Err(I2cError::Nack)
        } else {
            Ok(())
        }
    }

    /// Check for arbitration loss
    fn check_arbitration_error(&mut self) -> Result<(), I2cError> {
        let sr1 = self.regs().sr1;
        if sr1 & (StatusFlag::ArbitrationLost as u32) != 0 {
            // Clear arbitration lost flag
            self.regs_mut().sr1 &= !(StatusFlag::ArbitrationLost as u32);
            
            self.state = I2cState::Error;
            Err(I2cError::ArbitrationLost)
        } else {
            Ok(())
        }
    }

    /// Check for bus error
    fn check_bus_error(&mut self) -> Result<(), I2cError> {
        let sr1 = self.regs().sr1;
        if sr1 & (StatusFlag::BusError as u32) != 0 {
            // Clear bus error flag
            self.regs_mut().sr1 &= !(StatusFlag::BusError as u32);
            
            self.state = I2cState::Error;
            Err(I2cError::BusError)
        } else {
            Ok(())
        }
    }

    /// Wait for a specific flag with timeout and error checking
    fn wait_flag(&mut self, flag: u32, state: bool) -> Result<(), I2cError> {
        let start = get_tick_count();
        let timeout_ticks = self.timeout_ms * 1000;

        loop {
            // Check for errors first
            self.check_ack_error()?;
            self.check_arbitration_error()?;
            self.check_bus_error()?;

            // Check if flag is in desired state
            let sr1 = self.regs().sr1;
            let flag_status = (sr1 & flag) != 0;
            if flag_status == state {
                return Ok(());
            }

            // Check timeout
            if (get_tick_count() - start) >= timeout_ticks {
                self.state = I2cState::Error;
                return Err(I2cError::Timeout);
            }
        }
    }

    /// Check if bus is busy
    fn is_busy(&self) -> bool {
        (self.regs().sr2 & (1 << 1)) != 0
    }

    /// Generate START condition
    fn start(&mut self) -> Result<(), I2cError> {
        if self.is_busy() {
            return Err(I2cError::Busy);
        }

        self.regs_mut().cr1 |= CR1_START;
        self.wait_flag(StatusFlag::StartBit as u32, true)?;
        Ok(())
    }

    /// Generate STOP condition
    fn stop(&mut self) {
        self.regs_mut().cr1 |= CR1_STOP;
        self.state = I2cState::Idle;
    }

    /// Send slave address
    fn send_address(&mut self, address: u8, read: bool) -> Result<(), I2cError> {
        let addr_byte = (address << 1) | if read { 1 } else { 0 };
        self.regs_mut().dr = addr_byte as u32;
        
        self.wait_flag(StatusFlag::AddressSent as u32, true)
            .map_err(|e| {
                if e == I2cError::Timeout || e == I2cError::Nack {
                    // Address not acknowledged - slave not present
                    self.stop();
                }
                e
            })?;

        // Clear ADDR flag by reading SR1 and SR2
        let _ = self.regs().sr1;
        let _ = self.regs().sr2;

        Ok(())
    }

    /// Write data to I2C slave
    pub fn write(&mut self, address: u8, data: &[u8]) -> Result<(), I2cError> {
        if data.is_empty() {
            return Err(I2cError::InvalidParam);
        }

        self.state = I2cState::Transmit;

        // Generate START condition
        self.start()?;

        // Send slave address with write bit
        self.send_address(address, false)?;

        // Transmit data bytes
        for (i, &byte) in data.iter().enumerate() {
            // Wait for TX buffer empty
            self.wait_flag(StatusFlag::TxEmpty as u32, true)
                .map_err(|e| {
                    self.stop();
                    e
                })?;

            // Write data byte
            self.regs_mut().dr = byte as u32;

            // Check for NACK after each byte
            if let Err(e) = self.check_ack_error() {
                return Err(e);
            }
        }

        // Wait for byte transfer finished
        self.wait_flag(StatusFlag::ByteTransferFinished as u32, true)?;

        // Generate STOP condition
        self.stop();

        Ok(())
    }

    /// Read data from I2C slave
    pub fn read(&mut self, address: u8, buffer: &mut [u8]) -> Result<(), I2cError> {
        if buffer.is_empty() {
            return Err(I2cError::InvalidParam);
        }

        self.state = I2cState::Receive;

        // Enable ACK
        self.regs_mut().cr1 |= CR1_ACK;

        // Generate START condition
        self.start()?;

        // Send slave address with read bit
        self.send_address(address, true)?;

        // Receive data bytes
        for (i, byte) in buffer.iter_mut().enumerate() {
            // Before last byte, disable ACK and generate STOP
            if i == buffer.len() - 1 {
                self.regs_mut().cr1 &= !CR1_ACK;
                self.regs_mut().cr1 |= CR1_STOP;
            }

            // Wait for RX buffer not empty
            self.wait_flag(StatusFlag::RxNotEmpty as u32, true)
                .map_err(|e| {
                    self.stop();
                    e
                })?;

            // Read data byte
            *byte = self.regs().dr as u8;
        }

        self.state = I2cState::Idle;
        Ok(())
    }

    /// Write then read (combined transaction)
    pub fn write_read(
        &mut self,
        address: u8,
        write_data: &[u8],
        read_buffer: &mut [u8],
    ) -> Result<(), I2cError> {
        if write_data.is_empty() || read_buffer.is_empty() {
            return Err(I2cError::InvalidParam);
        }

        // Write phase
        self.start()?;
        self.send_address(address, false)?;

        for &byte in write_data {
            self.wait_flag(StatusFlag::TxEmpty as u32, true)?;
            self.regs_mut().dr = byte as u32;
            self.check_ack_error()?;
        }

        // Wait for byte transfer finished
        self.wait_flag(StatusFlag::ByteTransferFinished as u32, true)?;

        // Read phase - repeated START
        self.start()?;
        self.send_address(address, true)?;

        for (i, byte) in read_buffer.iter_mut().enumerate() {
            if i == read_buffer.len() - 1 {
                self.regs_mut().cr1 &= !CR1_ACK;
                self.regs_mut().cr1 |= CR1_STOP;
            }

            self.wait_flag(StatusFlag::RxNotEmpty as u32, true)?;
            *byte = self.regs().dr as u8;
        }

        self.state = I2cState::Idle;
        Ok(())
    }

    /// Attempt bus recovery for stuck condition
    pub fn recover(&mut self) -> Result<(), I2cError> {
        // Disable peripheral
        self.regs_mut().cr1 &= !CR1_PE;

        // Toggle SCL 9 times (platform-specific GPIO manipulation)
        for _ in 0..9 {
            // toggle_scl();
            delay_us(5);
        }

        // Re-enable peripheral
        self.regs_mut().cr1 |= CR1_PE;

        self.state = I2cState::Idle;
        Ok(())
    }

    /// Get current bus state
    pub fn state(&self) -> I2cState {
        self.state
    }
}

// Example usage with proper error handling
pub fn example_usage() -> Result<(), I2cError> {
    const I2C1_BASE: usize = 0x40005400;
    const SLAVE_ADDR: u8 = 0x50; // EEPROM address

    // Create I2C bus with 100ms timeout
    let mut i2c = unsafe { I2cBus::new(I2C1_BASE, 100) };

    // Write operation with error handling
    let write_data = [0x00, 0x10, 0xAA, 0xBB, 0xCC];
    match i2c.write(SLAVE_ADDR, &write_data) {
        Ok(_) => println!("Write successful"),
        Err(I2cError::Nack) => {
            println!("Slave not responding - check connections");
            return Err(I2cError::Nack);
        }
        Err(I2cError::Timeout) => {
            println!("Timeout - attempting bus recovery");
            i2c.recover()?;
            return Err(I2cError::Timeout);
        }
        Err(e) => {
            println!("Write error: {}", e);
            return Err(e);
        }
    }

    // Delay for EEPROM write cycle
    delay_ms(5);

    // Read operation
    let mut read_buffer = [0u8; 3];
    i2c.read(SLAVE_ADDR, &mut read_buffer)?;

    println!("Read data: {:02X?}", read_buffer);

    Ok(())
}

// Example with write-read combined transaction
pub fn example_register_read() -> Result<(), I2cError> {
    const I2C1_BASE: usize = 0x40005400;
    const SENSOR_ADDR: u8 = 0x68; // Example IMU address

    let mut i2c = unsafe { I2cBus::new(I2C1_BASE, 100) };

    // Read from register 0x75 (WHO_AM_I register)
    let register = [0x75];
    let mut who_am_i = [0u8; 1];

    i2c.write_read(SENSOR_ADDR, &register, &mut who_am_i)?;

    println!("WHO_AM_I: 0x{:02X}", who_am_i[0]);

    Ok(())
}

// Platform-specific helper functions (to be implemented)
fn get_tick_count() -> u32 {
    // Return current tick count from system timer
    0 // Placeholder
}

fn delay_ms(ms: u32) {
    // Platform-specific delay
}

fn delay_us(us: u32) {
    // Platform-specific microsecond delay
}

fn println!(fmt: &str, args: impl std::fmt::Display) {
    // Platform-specific print
}

// Unit tests
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_display() {
        assert_eq!(format!("{}", I2cError::Nack), "No acknowledge received");
        assert_eq!(format!("{}", I2cError::Timeout), "Communication timeout");
    }

    #[test]
    fn test_state_transitions() {
        let mut state = I2cState::Idle;
        assert_eq!(state, I2cState::Idle);
        
        state = I2cState::Transmit;
        assert_eq!(state, I2cState::Transmit);
    }
}