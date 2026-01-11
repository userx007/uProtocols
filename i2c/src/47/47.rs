/*!
 * I2C Low Power Implementation in Rust
 * Demonstrates power-saving techniques using embedded-hal traits
 */

#![no_std]

use embedded_hal::blocking::i2c::{Write, WriteRead, Read};
use embedded_hal::blocking::delay::DelayMs;
use core::fmt;

// Error types for power management
#[derive(Debug, Clone, Copy)]
pub enum PowerError {
    I2cError,
    InvalidState,
    Timeout,
}

impl fmt::Display for PowerError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            PowerError::I2cError => write!(f, "I2C communication error"),
            PowerError::InvalidState => write!(f, "Invalid power state"),
            PowerError::Timeout => write!(f, "Operation timeout"),
        }
    }
}

/// Power state management
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PowerState {
    Active,
    LowPower,
    Sleep,
    DeepSleep,
}

/// Power manager configuration
pub struct PowerConfig {
    pub wake_interval_ms: u32,
    pub i2c_clock_khz: u32,
    pub use_dma: bool,
    pub pullup_control: bool,
}

impl Default for PowerConfig {
    fn default() -> Self {
        PowerConfig {
            wake_interval_ms: 60_000, // 60 seconds
            i2c_clock_khz: 100,        // 100kHz standard mode
            use_dma: false,
            pullup_control: false,
        }
    }
}

/// Low-power I2C device manager
pub struct LowPowerI2C<I2C, DELAY> {
    i2c: Option<I2C>,
    delay: DELAY,
    config: PowerConfig,
    state: PowerState,
    transaction_count: u32,
}

impl<I2C, DELAY, E> LowPowerI2C<I2C, DELAY>
where
    I2C: Write<Error = E> + WriteRead<Error = E> + Read<Error = E>,
    DELAY: DelayMs<u32>,
{
    /// Create a new low-power I2C manager
    pub fn new(i2c: I2C, delay: DELAY, config: PowerConfig) -> Self {
        LowPowerI2C {
            i2c: Some(i2c),
            delay,
            config,
            state: PowerState::Active,
            transaction_count: 0,
        }
    }

    /// Enable I2C peripheral (wake from sleep)
    pub fn enable(&mut self) -> Result<(), PowerError> {
        match self.state {
            PowerState::Active => Ok(()),
            _ => {
                // In a real implementation, reconfigure I2C peripheral
                self.state = PowerState::Active;
                self.delay.delay_ms(1); // Stabilization time
                Ok(())
            }
        }
    }

    /// Disable I2C peripheral for power savings
    pub fn disable(&mut self) -> Result<(), PowerError> {
        if self.state == PowerState::Active {
            // In a real implementation, deinitialize I2C and set pins to analog
            self.state = PowerState::LowPower;
            Ok(())
        } else {
            Err(PowerError::InvalidState)
        }
    }

    /// Burst read - multiple bytes in single transaction
    pub fn burst_read(&mut self, addr: u8, reg: u8, buffer: &mut [u8]) 
        -> Result<(), PowerError> {
        
        self.enable()?;
        
        let i2c = self.i2c.as_mut().ok_or(PowerError::InvalidState)?;
        
        // Perform write-read transaction
        i2c.write_read(addr, &[reg], buffer)
            .map_err(|_| PowerError::I2cError)?;
        
        self.transaction_count += 1;
        Ok(())
    }

    /// Burst write - multiple bytes in single transaction
    pub fn burst_write(&mut self, addr: u8, data: &[u8]) -> Result<(), PowerError> {
        self.enable()?;
        
        let i2c = self.i2c.as_mut().ok_or(PowerError::InvalidState)?;
        
        i2c.write(addr, data)
            .map_err(|_| PowerError::I2cError)?;
        
        self.transaction_count += 1;
        Ok(())
    }

    /// Optimized sensor read with automatic power management
    pub fn read_sensor_low_power(&mut self, addr: u8, reg: u8, buffer: &mut [u8]) 
        -> Result<(), PowerError> {
        
        // 1. Wake up I2C
        self.enable()?;
        
        // 2. Perform burst read
        self.burst_read(addr, reg, buffer)?;
        
        // 3. Return to low power immediately
        self.disable()?;
        
        Ok(())
    }

    /// Get current power state
    pub fn power_state(&self) -> PowerState {
        self.state
    }

    /// Get transaction statistics
    pub fn transaction_count(&self) -> u32 {
        self.transaction_count
    }

    /// Enter sleep mode (releases I2C peripheral)
    pub fn enter_sleep(&mut self) -> Result<I2C, PowerError> {
        self.state = PowerState::Sleep;
        self.i2c.take().ok_or(PowerError::InvalidState)
    }

    /// Wake from sleep mode (reclaims I2C peripheral)
    pub fn wake_from_sleep(&mut self, i2c: I2C) {
        self.i2c = Some(i2c);
        self.state = PowerState::Active;
    }
}

/// Example sensor driver with power management
pub struct LowPowerSensor<I2C, DELAY> {
    i2c_mgr: LowPowerI2C<I2C, DELAY>,
    device_addr: u8,
}

impl<I2C, DELAY, E> LowPowerSensor<I2C, DELAY>
where
    I2C: Write<Error = E> + WriteRead<Error = E> + Read<Error = E>,
    DELAY: DelayMs<u32>,
{
    pub fn new(i2c: I2C, delay: DELAY, device_addr: u8) -> Self {
        let config = PowerConfig::default();
        let i2c_mgr = LowPowerI2C::new(i2c, delay, config);
        
        LowPowerSensor {
            i2c_mgr,
            device_addr,
        }
    }

    /// Read temperature (example)
    pub fn read_temperature(&mut self) -> Result<f32, PowerError> {
        let mut buffer = [0u8; 2];
        
        // Read 2 bytes starting from register 0x00
        self.i2c_mgr.read_sensor_low_power(
            self.device_addr,
            0x00,
            &mut buffer
        )?;
        
        // Convert to temperature (example conversion)
        let raw = u16::from_be_bytes(buffer);
        let temp = (raw as f32) * 0.0625;
        
        Ok(temp)
    }

    /// Read multiple sensor values in batch
    pub fn read_all_sensors(&mut self) -> Result<SensorData, PowerError> {
        let mut buffer = [0u8; 12];
        
        // Single burst read of all sensor registers
        self.i2c_mgr.read_sensor_low_power(
            self.device_addr,
            0x00,
            &mut buffer
        )?;
        
        Ok(SensorData::from_bytes(&buffer))
    }

    /// Configure sensor for low-power operation
    pub fn configure_low_power_mode(&mut self) -> Result<(), PowerError> {
        // Example: Set sensor to low-power mode with reduced sampling rate
        let config_data = [
            0x01,  // Config register
            0x20,  // Low power mode, 1Hz sample rate
        ];
        
        self.i2c_mgr.burst_write(self.device_addr, &config_data)?;
        
        Ok(())
    }

    /// Disable I2C between readings
    pub fn sleep(&mut self) -> Result<(), PowerError> {
        self.i2c_mgr.disable()
    }

    /// Wake up for next reading
    pub fn wake(&mut self) -> Result<(), PowerError> {
        self.i2c_mgr.enable()
    }
}

/// Sensor data structure
#[derive(Debug, Clone, Copy)]
pub struct SensorData {
    pub temperature: f32,
    pub humidity: f32,
    pub pressure: f32,
}

impl SensorData {
    fn from_bytes(bytes: &[u8]) -> Self {
        // Example parsing
        let temp_raw = u16::from_be_bytes([bytes[0], bytes[1]]);
        let hum_raw = u16::from_be_bytes([bytes[4], bytes[5]]);
        let pres_raw = u32::from_be_bytes([0, bytes[8], bytes[9], bytes[10]]);
        
        SensorData {
            temperature: (temp_raw as f32) * 0.0625,
            humidity: (hum_raw as f32) / 655.36,
            pressure: (pres_raw as f32) / 100.0,
        }
    }
}

/// Power-optimized periodic reading pattern
pub struct PeriodicReader<I2C, DELAY> {
    sensor: LowPowerSensor<I2C, DELAY>,
    interval_ms: u32,
    reading_count: u32,
}

impl<I2C, DELAY, E> PeriodicReader<I2C, DELAY>
where
    I2C: Write<Error = E> + WriteRead<Error = E> + Read<Error = E>,
    DELAY: DelayMs<u32>,
{
    pub fn new(sensor: LowPowerSensor<I2C, DELAY>, interval_ms: u32) -> Self {
        PeriodicReader {
            sensor,
            interval_ms,
            reading_count: 0,
        }
    }

    /// Perform one reading cycle
    pub fn read_cycle(&mut self) -> Result<SensorData, PowerError> {
        // Wake sensor and I2C
        self.sensor.wake()?;
        
        // Read all sensor data in one burst
        let data = self.sensor.read_all_sensors()?;
        
        // Return to sleep
        self.sensor.sleep()?;
        
        self.reading_count += 1;
        
        Ok(data)
    }

    /// Main loop for battery-powered operation
    pub fn run_loop<F>(&mut self, mut process_fn: F) -> !
    where
        F: FnMut(SensorData),
    {
        loop {
            match self.read_cycle() {
                Ok(data) => {
                    process_fn(data);
                }
                Err(e) => {
                    // Handle error (could log, retry, etc.)
                }
            }
            
            // Sleep until next reading
            // In real implementation, use RTC alarm or low-power timer
            // This is a placeholder
            cortex_m::asm::wfi(); // Wait for interrupt
        }
    }

    pub fn reading_count(&self) -> u32 {
        self.reading_count
    }
}

/// Calculate estimated power consumption
pub struct PowerEstimator {
    i2c_freq_khz: u32,
    pullup_resistance_kohm: u32,
    supply_voltage_mv: u32,
}

impl PowerEstimator {
    pub fn new(i2c_freq_khz: u32, pullup_resistance_kohm: u32, 
               supply_voltage_mv: u32) -> Self {
        PowerEstimator {
            i2c_freq_khz,
            pullup_resistance_kohm,
            supply_voltage_mv,
        }
    }

    /// Calculate pull-up resistor power (when line is low)
    pub fn pullup_power_uw(&self) -> u32 {
        // P = V² / R (in microwatts)
        let v_sq = self.supply_voltage_mv * self.supply_voltage_mv;
        v_sq / self.pullup_resistance_kohm
    }

    /// Estimate average power during I2C transaction
    pub fn transaction_power_estimate_uw(&self, bytes: u32, duty_cycle: f32) -> u32 {
        // Simplified estimation
        let pullup_power = self.pullup_power_uw();
        let active_power = pullup_power * 2; // Both SDA and SCL
        
        (active_power as f32 * duty_cycle) as u32
    }
}

// Example usage function
#[cfg(test)]
mod example {
    use super::*;

    pub fn usage_example() {
        // This demonstrates the API, actual types would come from HAL
        
        // 1. Create power estimator
        let estimator = PowerEstimator::new(100, 10, 3300);
        let pullup_power = estimator.pullup_power_uw();
        // pullup_power = 1089 µW per resistor
        
        // 2. Configure sensor for low power
        // let mut sensor = LowPowerSensor::new(i2c, delay, 0x48);
        // sensor.configure_low_power_mode().unwrap();
        
        // 3. Set up periodic reader
        // let mut reader = PeriodicReader::new(sensor, 60_000);
        
        // 4. Run main loop
        // reader.run_loop(|data| {
        //     // Process sensor data
        //     // Send via radio, log to flash, etc.
        // });
    }
}