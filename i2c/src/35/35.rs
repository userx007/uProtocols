// lib.rs - Abstract Interface using Traits

use std::time::Duration;

// Error types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cError {
    Timeout,
    Nack,
    BusError,
    InvalidParam,
    NotInitialized,
    Io(std::io::ErrorKind),
}

impl std::fmt::Display for I2cError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            I2cError::Timeout => write!(f, "I2C timeout"),
            I2cError::Nack => write!(f, "I2C NACK received"),
            I2cError::BusError => write!(f, "I2C bus error"),
            I2cError::InvalidParam => write!(f, "Invalid parameter"),
            I2cError::NotInitialized => write!(f, "I2C not initialized"),
            I2cError::Io(kind) => write!(f, "IO error: {:?}", kind),
        }
    }
}

impl std::error::Error for I2cError {}

pub type Result<T> = std::result::Result<T, I2cError>;

// Configuration structure
#[derive(Debug, Clone)]
pub struct I2cConfig {
    pub clock_speed: u32,     // Clock speed in Hz
    pub address_mode: u8,     // 7 or 10 bit addressing
    pub timeout: Duration,
}

impl Default for I2cConfig {
    fn default() -> Self {
        Self {
            clock_speed: 100_000,  // 100 kHz
            address_mode: 7,
            timeout: Duration::from_millis(1000),
        }
    }
}

// Core I2C trait - defines the abstraction interface
pub trait I2cBus {
    /// Write data to a device
    fn write(&mut self, addr: u8, data: &[u8]) -> Result<()>;
    
    /// Read data from a device
    fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<()>;
    
    /// Write then read (common for register access)
    fn write_read(&mut self, addr: u8, write_data: &[u8], read_buffer: &mut [u8]) -> Result<()>;
    
    /// Check if device is present on the bus
    fn is_device_ready(&mut self, addr: u8) -> bool;
}

// Extended trait for register-based operations
pub trait I2cRegister: I2cBus {
    /// Write a single byte to a register
    fn write_reg(&mut self, addr: u8, reg: u8, value: u8) -> Result<()> {
        self.write(addr, &[reg, value])
    }
    
    /// Read a single byte from a register
    fn read_reg(&mut self, addr: u8, reg: u8) -> Result<u8> {
        let mut buffer = [0u8; 1];
        self.write_read(addr, &[reg], &mut buffer)?;
        Ok(buffer[0])
    }
    
    /// Write multiple bytes to consecutive registers
    fn write_regs(&mut self, addr: u8, reg: u8, data: &[u8]) -> Result<()> {
        let mut buffer = vec![reg];
        buffer.extend_from_slice(data);
        self.write(addr, &buffer)
    }
    
    /// Read multiple bytes from consecutive registers
    fn read_regs(&mut self, addr: u8, reg: u8, buffer: &mut [u8]) -> Result<()> {
        self.write_read(addr, &[reg], buffer)
    }
    
    /// Modify bits in a register (read-modify-write)
    fn modify_reg<F>(&mut self, addr: u8, reg: u8, f: F) -> Result<()>
    where
        F: FnOnce(u8) -> u8,
    {
        let value = self.read_reg(addr, reg)?;
        let new_value = f(value);
        self.write_reg(addr, reg, new_value)
    }
}

// Auto-implement I2cRegister for any type that implements I2cBus
impl<T: I2cBus> I2cRegister for T {}

// ============================================================================
// PLATFORM IMPLEMENTATION 1: Linux using i2c-dev
// ============================================================================

#[cfg(target_os = "linux")]
pub mod linux {
    use super::*;
    use std::fs::{File, OpenOptions};
    use std::os::unix::io::AsRawFd;
    
    const I2C_SLAVE: u64 = 0x0703;
    
    pub struct LinuxI2c {
        file: File,
        timeout: Duration,
        current_addr: Option<u8>,
    }
    
    impl LinuxI2c {
        pub fn new(bus: u8, config: I2cConfig) -> Result<Self> {
            let path = format!("/dev/i2c-{}", bus);
            let file = OpenOptions::new()
                .read(true)
                .write(true)
                .open(&path)
                .map_err(|e| I2cError::Io(e.kind()))?;
            
            Ok(Self {
                file,
                timeout: config.timeout,
                current_addr: None,
            })
        }
        
        fn set_slave_addr(&mut self, addr: u8) -> Result<()> {
            if self.current_addr == Some(addr) {
                return Ok(());
            }
            
            unsafe {
                if libc::ioctl(self.file.as_raw_fd(), I2C_SLAVE, addr as libc::c_ulong) < 0 {
                    return Err(I2cError::Nack);
                }
            }
            
            self.current_addr = Some(addr);
            Ok(())
        }
    }
    
    impl I2cBus for LinuxI2c {
        fn write(&mut self, addr: u8, data: &[u8]) -> Result<()> {
            self.set_slave_addr(addr)?;
            
            use std::io::Write;
            self.file.write_all(data)
                .map_err(|e| I2cError::Io(e.kind()))
        }
        
        fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<()> {
            self.set_slave_addr(addr)?;
            
            use std::io::Read;
            self.file.read_exact(buffer)
                .map_err(|e| I2cError::Io(e.kind()))
        }
        
        fn write_read(&mut self, addr: u8, write_data: &[u8], read_buffer: &mut [u8]) -> Result<()> {
            self.write(addr, write_data)?;
            self.read(addr, read_buffer)
        }
        
        fn is_device_ready(&mut self, addr: u8) -> bool {
            self.set_slave_addr(addr).is_ok()
        }
    }
}

// ============================================================================
// PLATFORM IMPLEMENTATION 2: Embedded HAL (for embedded-hal ecosystem)
// ============================================================================

#[cfg(feature = "embedded-hal")]
pub mod embedded {
    use super::*;
    use embedded_hal::i2c::{I2c as EmbeddedI2c, ErrorType};
    
    /// Adapter that wraps any embedded-hal I2C implementation
    pub struct EmbeddedI2cAdapter<I2C> {
        i2c: I2C,
        timeout: Duration,
    }
    
    impl<I2C> EmbeddedI2cAdapter<I2C>
    where
        I2C: EmbeddedI2c,
    {
        pub fn new(i2c: I2C, config: I2cConfig) -> Self {
            Self {
                i2c,
                timeout: config.timeout,
            }
        }
    }
    
    impl<I2C> I2cBus for EmbeddedI2cAdapter<I2C>
    where
        I2C: EmbeddedI2c,
    {
        fn write(&mut self, addr: u8, data: &[u8]) -> Result<()> {
            self.i2c.write(addr, data)
                .map_err(|_| I2cError::BusError)
        }
        
        fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<()> {
            self.i2c.read(addr, buffer)
                .map_err(|_| I2cError::BusError)
        }
        
        fn write_read(&mut self, addr: u8, write_data: &[u8], read_buffer: &mut [u8]) -> Result<()> {
            self.i2c.write_read(addr, write_data, read_buffer)
                .map_err(|_| I2cError::BusError)
        }
        
        fn is_device_ready(&mut self, addr: u8) -> bool {
            // Try a zero-byte write
            self.i2c.write(addr, &[]).is_ok()
        }
    }
}

// ============================================================================
// MOCK IMPLEMENTATION - For Testing
// ============================================================================

#[cfg(test)]
pub mod mock {
    use super::*;
    use std::collections::HashMap;
    
    /// Mock I2C bus for unit testing
    pub struct MockI2c {
        devices: HashMap<u8, HashMap<u8, u8>>,  // addr -> (reg -> value)
        write_log: Vec<(u8, Vec<u8>)>,
        should_fail: bool,
    }
    
    impl MockI2c {
        pub fn new() -> Self {
            Self {
                devices: HashMap::new(),
                write_log: Vec::new(),
                should_fail: false,
            }
        }
        
        /// Add a device with initial register values
        pub fn add_device(&mut self, addr: u8, registers: HashMap<u8, u8>) {
            self.devices.insert(addr, registers);
        }
        
        /// Set whether operations should fail
        pub fn set_should_fail(&mut self, fail: bool) {
            self.should_fail = fail;
        }
        
        /// Get the write log for verification
        pub fn get_write_log(&self) -> &[(u8, Vec<u8>)] {
            &self.write_log
        }
    }
    
    impl I2cBus for MockI2c {
        fn write(&mut self, addr: u8, data: &[u8]) -> Result<()> {
            if self.should_fail {
                return Err(I2cError::BusError);
            }
            
            self.write_log.push((addr, data.to_vec()));
            
            if let Some(device) = self.devices.get_mut(&addr) {
                // Assume first byte is register, rest is data
                if data.len() >= 2 {
                    let reg = data[0];
                    for (i, &value) in data[1..].iter().enumerate() {
                        device.insert(reg + i as u8, value);
                    }
                }
                Ok(())
            } else {
                Err(I2cError::Nack)
            }
        }
        
        fn read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<()> {
            if self.should_fail {
                return Err(I2cError::BusError);
            }
            
            if self.devices.contains_key(&addr) {
                buffer.fill(0xFF);  // Return dummy data
                Ok(())
            } else {
                Err(I2cError::Nack)
            }
        }
        
        fn write_read(&mut self, addr: u8, write_data: &[u8], read_buffer: &mut [u8]) -> Result<()> {
            if self.should_fail {
                return Err(I2cError::BusError);
            }
            
            if let Some(device) = self.devices.get(&addr) {
                // Assume write_data[0] is register address
                if !write_data.is_empty() {
                    let reg = write_data[0];
                    for (i, byte) in read_buffer.iter_mut().enumerate() {
                        *byte = *device.get(&(reg + i as u8)).unwrap_or(&0xFF);
                    }
                }
                Ok(())
            } else {
                Err(I2cError::Nack)
            }
        }
        
        fn is_device_ready(&mut self, addr: u8) -> bool {
            !self.should_fail && self.devices.contains_key(&addr)
        }
    }
}

// ============================================================================
// APPLICATION CODE - Platform Independent!
// ============================================================================

/// Example sensor driver that uses the abstraction
pub struct Bme280<I2C> {
    i2c: I2C,
    addr: u8,
}

impl<I2C: I2cBus> Bme280<I2C> {
    const CHIP_ID_REG: u8 = 0xD0;
    const CTRL_MEAS_REG: u8 = 0xF4;
    const EXPECTED_CHIP_ID: u8 = 0x60;
    
    pub fn new(i2c: I2C, addr: u8) -> Result<Self> {
        let mut sensor = Self { i2c, addr };
        
        // Verify chip ID
        let chip_id = sensor.i2c.read_reg(sensor.addr, Self::CHIP_ID_REG)?;
        if chip_id != Self::EXPECTED_CHIP_ID {
            return Err(I2cError::InvalidParam);
        }
        
        Ok(sensor)
    }
    
    pub fn set_mode(&mut self, mode: u8) -> Result<()> {
        self.i2c.write_reg(self.addr, Self::CTRL_MEAS_REG, mode)
    }
    
    pub fn read_temperature(&mut self) -> Result<f32> {
        // Simplified - real implementation would read temp registers
        let mut data = [0u8; 3];
        self.i2c.read_regs(self.addr, 0xFA, &mut data)?;
        
        let raw = ((data[0] as u32) << 12) | ((data[1] as u32) << 4) | ((data[2] as u32) >> 4);
        Ok(raw as f32 / 100.0)  // Simplified conversion
    }
}

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

#[cfg(target_os = "linux")]
fn example_linux() -> Result<()> {
    use linux::LinuxI2c;
    
    let config = I2cConfig::default();
    let mut i2c = LinuxI2c::new(1, config)?;
    
    let mut sensor = Bme280::new(i2c, 0x76)?;
    sensor.set_mode(0x27)?;
    
    let temp = sensor.read_temperature()?;
    println!("Temperature: {:.2}°C", temp);
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use mock::MockI2c;
    use std::collections::HashMap;
    
    #[test]
    fn test_sensor_with_mock() {
        let mut i2c = MockI2c::new();
        
        // Set up mock device
        let mut regs = HashMap::new();
        regs.insert(0xD0, 0x60);  // Chip ID
        regs.insert(0xF4, 0x00);  // Control register
        i2c.add_device(0x76, regs);
        
        // Test sensor initialization
        let mut sensor = Bme280::new(i2c, 0x76).unwrap();
        
        // Test setting mode
        assert!(sensor.set_mode(0x27).is_ok());
    }
    
    #[test]
    fn test_register_operations() {
        let mut i2c = MockI2c::new();
        let mut regs = HashMap::new();
        regs.insert(0x10, 0x42);
        i2c.add_device(0x50, regs);
        
        // Test read
        let value = i2c.read_reg(0x50, 0x10).unwrap();
        assert_eq!(value, 0x42);
        
        // Test write
        i2c.write_reg(0x50, 0x10, 0x99).unwrap();
        let value = i2c.read_reg(0x50, 0x10).unwrap();
        assert_eq!(value, 0x99);
        
        // Test modify
        i2c.modify_reg(0x50, 0x10, |v| v | 0x01).unwrap();
        let value = i2c.read_reg(0x50, 0x10).unwrap();
        assert_eq!(value, 0x99 | 0x01);
    }
}

// Main function showing platform selection
fn main() -> Result<()> {
    #[cfg(target_os = "linux")]
    {
        println!("Running on Linux platform");
        example_linux()?;
    }
    
    #[cfg(not(target_os = "linux"))]
    {
        println!("Platform not supported in this example");
    }
    
    Ok(())
}