// I2C Throughput Optimization Examples in Rust
// Demonstrates batching, burst operations, and zero-copy techniques

use embedded_hal::i2c::{I2c, Operation};
use core::time::Duration;

// ============================================================================
// ERROR TYPES
// ============================================================================

#[derive(Debug)]
pub enum I2cOptError {
    BusError,
    BufferTooSmall,
    Timeout,
    InvalidParameter,
}

type Result<T> = core::result::Result<T, I2cOptError>;

// ============================================================================
// NAIVE APPROACH - Low Throughput
// ============================================================================

pub struct NaiveI2cReader<I: I2c> {
    i2c: I,
}

impl<I: I2c> NaiveI2cReader<I> {
    pub fn new(i2c: I) -> Self {
        Self { i2c }
    }
    
    /// Read multiple registers individually (SLOW)
    /// Each register requires a separate I2C transaction
    pub fn read_registers_naive(
        &mut self,
        device_addr: u8,
        start_reg: u8,
        buffer: &mut [u8],
    ) -> Result<()> {
        for (i, byte) in buffer.iter_mut().enumerate() {
            let reg = start_reg + i as u8;
            
            // START + ADDR + REG + RESTART + ADDR + DATA + STOP per iteration
            self.i2c
                .write_read(device_addr, &[reg], core::slice::from_mut(byte))
                .map_err(|_| I2cOptError::BusError)?;
        }
        Ok(())
    }
    
    /// Write multiple registers individually (SLOW)
    pub fn write_registers_naive(
        &mut self,
        device_addr: u8,
        start_reg: u8,
        values: &[u8],
    ) -> Result<()> {
        for (i, &value) in values.iter().enumerate() {
            let reg = start_reg + i as u8;
            let buffer = [reg, value];
            
            // START + ADDR + REG + DATA + STOP per iteration
            self.i2c
                .write(device_addr, &buffer)
                .map_err(|_| I2cOptError::BusError)?;
        }
        Ok(())
    }
}

// ============================================================================
// OPTIMIZED BURST TRANSFERS
// ============================================================================

pub struct BurstI2cTransfer<I: I2c> {
    i2c: I,
}

impl<I: I2c> BurstI2cTransfer<I> {
    pub fn new(i2c: I) -> Self {
        Self { i2c }
    }
    
    /// Read multiple consecutive registers in one transaction (FAST)
    /// Single transaction: START + ADDR + REG + RESTART + ADDR + DATA[0..n] + STOP
    pub fn read_burst(
        &mut self,
        device_addr: u8,
        start_reg: u8,
        buffer: &mut [u8],
    ) -> Result<()> {
        self.i2c
            .write_read(device_addr, &[start_reg], buffer)
            .map_err(|_| I2cOptError::BusError)
    }
    
    /// Write multiple consecutive registers in one transaction (FAST)
    pub fn write_burst(
        &mut self,
        device_addr: u8,
        start_reg: u8,
        values: &[u8],
    ) -> Result<()> {
        let mut buffer = [0u8; 256];
        
        if values.len() > 255 {
            return Err(I2cOptError::BufferTooSmall);
        }
        
        buffer[0] = start_reg;
        buffer[1..=values.len()].copy_from_slice(values);
        
        self.i2c
            .write(device_addr, &buffer[..values.len() + 1])
            .map_err(|_| I2cOptError::BusError)
    }
    
    /// Zero-copy write using pre-formatted buffer
    /// Buffer must contain: [REG, DATA0, DATA1, ...]
    pub fn write_burst_zerocopy(
        &mut self,
        device_addr: u8,
        buffer_with_reg: &[u8],
    ) -> Result<()> {
        self.i2c
            .write(device_addr, buffer_with_reg)
            .map_err(|_| I2cOptError::BusError)
    }
}

// ============================================================================
// BATCHED OPERATIONS WITH TRANSACTION API
// ============================================================================

pub struct BatchedI2cTransfer<I: I2c> {
    i2c: I,
}

impl<I: I2c> BatchedI2cTransfer<I> {
    pub fn new(i2c: I) -> Self {
        Self { i2c }
    }
    
    /// Perform multiple read operations in batched transaction
    pub fn read_multiple_registers(
        &mut self,
        device_addr: u8,
        registers: &[u8],
        buffers: &mut [&mut [u8]],
    ) -> Result<()> {
        if registers.len() != buffers.len() {
            return Err(I2cOptError::InvalidParameter);
        }
        
        for (reg, buffer) in registers.iter().zip(buffers.iter_mut()) {
            self.i2c
                .write_read(device_addr, core::slice::from_ref(reg), buffer)
                .map_err(|_| I2cOptError::BusError)?;
        }
        
        Ok(())
    }
}

// ============================================================================
// REGISTER ABSTRACTION FOR TYPE-SAFE BURST OPERATIONS
// ============================================================================

pub trait RegisterBlock {
    const START_ADDR: u8;
    const SIZE: usize;
    
    fn from_bytes(bytes: &[u8]) -> Self;
    fn to_bytes(&self) -> [u8; Self::SIZE];
}

pub struct TypedBurstReader<I: I2c> {
    i2c: I,
}

impl<I: I2c> TypedBurstReader<I> {
    pub fn new(i2c: I) -> Self {
        Self { i2c }
    }
    
    /// Read entire register block with type safety
    pub fn read_block<R: RegisterBlock>(
        &mut self,
        device_addr: u8,
    ) -> Result<R> {
        let mut buffer = [0u8; 32];
        
        if R::SIZE > buffer.len() {
            return Err(I2cOptError::BufferTooSmall);
        }
        
        self.i2c
            .write_read(device_addr, &[R::START_ADDR], &mut buffer[..R::SIZE])
            .map_err(|_| I2cOptError::BusError)?;
        
        Ok(R::from_bytes(&buffer[..R::SIZE]))
    }
    
    /// Write entire register block
    pub fn write_block<R: RegisterBlock>(
        &mut self,
        device_addr: u8,
        block: &R,
    ) -> Result<()> {
        let data = block.to_bytes();
        let mut buffer = [0u8; 33];
        
        buffer[0] = R::START_ADDR;
        buffer[1..=R::SIZE].copy_from_slice(&data);
        
        self.i2c
            .write(device_addr, &buffer[..R::SIZE + 1])
            .map_err(|_| I2cOptError::BusError)
    }
}

// ============================================================================
// EXAMPLE: IMU SENSOR DATA STRUCTURE
// ============================================================================

#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct ImuData {
    pub accel_x: i16,
    pub accel_y: i16,
    pub accel_z: i16,
    pub temp: i16,
    pub gyro_x: i16,
    pub gyro_y: i16,
    pub gyro_z: i16,
}

impl RegisterBlock for ImuData {
    const START_ADDR: u8 = 0x3B;
    const SIZE: usize = 14;
    
    fn from_bytes(bytes: &[u8]) -> Self {
        Self {
            accel_x: i16::from_be_bytes([bytes[0], bytes[1]]),
            accel_y: i16::from_be_bytes([bytes[2], bytes[3]]),
            accel_z: i16::from_be_bytes([bytes[4], bytes[5]]),
            temp: i16::from_be_bytes([bytes[6], bytes[7]]),
            gyro_x: i16::from_be_bytes([bytes[8], bytes[9]]),
            gyro_y: i16::from_be_bytes([bytes[10], bytes[11]]),
            gyro_z: i16::from_be_bytes([bytes[12], bytes[13]]),
        }
    }
    
    fn to_bytes(&self) -> [u8; Self::SIZE] {
        let mut bytes = [0u8; 14];
        bytes[0..2].copy_from_slice(&self.accel_x.to_be_bytes());
        bytes[2..4].copy_from_slice(&self.accel_y.to_be_bytes());
        bytes[4..6].copy_from_slice(&self.accel_z.to_be_bytes());
        bytes[6..8].copy_from_slice(&self.temp.to_be_bytes());
        bytes[8..10].copy_from_slice(&self.gyro_x.to_be_bytes());
        bytes[10..12].copy_from_slice(&self.gyro_y.to_be_bytes());
        bytes[12..14].copy_from_slice(&self.gyro_z.to_be_bytes());
        bytes
    }
}

pub struct ImuSensor<I: I2c> {
    reader: TypedBurstReader<I>,
    device_addr: u8,
}

impl<I: I2c> ImuSensor<I> {
    const DEVICE_ADDR: u8 = 0x68;
    
    pub fn new(i2c: I) -> Self {
        Self {
            reader: TypedBurstReader::new(i2c),
            device_addr: Self::DEVICE_ADDR,
        }
    }
    
    /// Read all IMU data in single burst transaction
    pub fn read_all(&mut self) -> Result<ImuData> {
        self.reader.read_block(self.device_addr)
    }
    
    /// Convert raw sensor data to physical units
    pub fn to_physical_units(&self, raw: &ImuData) -> PhysicalImuData {
        const ACCEL_SCALE: f32 = 8.0 / 32768.0; // ±8g range
        const GYRO_SCALE: f32 = 2000.0 / 32768.0; // ±2000°/s range
        const TEMP_SCALE: f32 = 1.0 / 340.0;
        const TEMP_OFFSET: f32 = 36.53;
        
        PhysicalImuData {
            accel_x_g: raw.accel_x as f32 * ACCEL_SCALE,
            accel_y_g: raw.accel_y as f32 * ACCEL_SCALE,
            accel_z_g: raw.accel_z as f32 * ACCEL_SCALE,
            temp_c: raw.temp as f32 * TEMP_SCALE + TEMP_OFFSET,
            gyro_x_dps: raw.gyro_x as f32 * GYRO_SCALE,
            gyro_y_dps: raw.gyro_y as f32 * GYRO_SCALE,
            gyro_z_dps: raw.gyro_z as f32 * GYRO_SCALE,
        }
    }
}

#[derive(Debug)]
pub struct PhysicalImuData {
    pub accel_x_g: f32,
    pub accel_y_g: f32,
    pub accel_z_g: f32,
    pub temp_c: f32,
    pub gyro_x_dps: f32,
    pub gyro_y_dps: f32,
    pub gyro_z_dps: f32,
}

// ============================================================================
// PERFORMANCE BENCHMARKING
// ============================================================================

#[derive(Debug)]
pub struct BenchmarkResult {
    pub duration: Duration,
    pub bytes_transferred: usize,
    pub transactions: usize,
    pub throughput_kbps: f32,
}

pub struct I2cBenchmark<I: I2c> {
    i2c: I,
}

impl<I: I2c> I2cBenchmark<I> {
    pub fn new(i2c: I) -> Self {
        Self { i2c }
    }
    
    /// Compare naive vs burst read performance
    pub fn benchmark_read_methods(
        &mut self,
        device_addr: u8,
        start_reg: u8,
        count: usize,
    ) -> (BenchmarkResult, BenchmarkResult) {
        let mut buffer = [0u8; 256];
        let data = &mut buffer[..count];
        
        // Benchmark naive approach
        let start = Self::get_time();
        let mut naive_reader = NaiveI2cReader::new(&mut self.i2c);
        let _ = naive_reader.read_registers_naive(device_addr, start_reg, data);
        let naive_duration = Self::get_time() - start;
        
        let naive_result = BenchmarkResult {
            duration: naive_duration,
            bytes_transferred: count,
            transactions: count,
            throughput_kbps: Self::calculate_throughput(count, naive_duration),
        };
        
        // Benchmark burst approach
        let start = Self::get_time();
        let mut burst_reader = BurstI2cTransfer::new(&mut self.i2c);
        let _ = burst_reader.read_burst(device_addr, start_reg, data);
        let burst_duration = Self::get_time() - start;
        
        let burst_result = BenchmarkResult {
            duration: burst_duration,
            bytes_transferred: count,
            transactions: 1,
            throughput_kbps: Self::calculate_throughput(count, burst_duration),
        };
        
        (naive_result, burst_result)
    }
    
    fn get_time() -> Duration {
        // Platform-specific timer implementation
        Duration::from_millis(0) // Placeholder
    }
    
    fn calculate_throughput(bytes: usize, duration: Duration) -> f32 {
        let bits = (bytes * 8) as f32;
        let seconds = duration.as_secs_f32();
        if seconds > 0.0 {
            bits / seconds / 1000.0
        } else {
            0.0
        }
    }
}

// ============================================================================
// ASYNC/DMA SUPPORT (with embassy or similar)
// ============================================================================

#[cfg(feature = "async")]
pub mod async_i2c {
    use super::*;
    use embedded_hal_async::i2c::I2c as AsyncI2c;
    
    pub struct AsyncBurstReader<I: AsyncI2c> {
        i2c: I,
    }
    
    impl<I: AsyncI2c> AsyncBurstReader<I> {
        pub fn new(i2c: I) -> Self {
            Self { i2c }
        }
        
        /// Async burst read (often uses DMA under the hood)
        pub async fn read_burst_async(
            &mut self,
            device_addr: u8,
            start_reg: u8,
            buffer: &mut [u8],
        ) -> Result<()> {
            self.i2c
                .write_read(device_addr, &[start_reg], buffer)
                .await
                .map_err(|_| I2cOptError::BusError)
        }
        
        /// Async burst write
        pub async fn write_burst_async(
            &mut self,
            device_addr: u8,
            buffer_with_reg: &[u8],
        ) -> Result<()> {
            self.i2c
                .write(device_addr, buffer_with_reg)
                .await
                .map_err(|_| I2cOptError::BusError)
        }
    }
}

// ============================================================================
// USAGE EXAMPLE
// ============================================================================

#[cfg(feature = "example")]
pub fn example_usage<I: I2c>(i2c: I) -> Result<()> {
    // Create IMU sensor driver with burst reads
    let mut imu = ImuSensor::new(i2c);
    
    // Read all sensor data in single transaction (14 bytes)
    let raw_data = imu.read_all()?;
    
    // Convert to physical units
    let physical_data = imu.to_physical_units(&raw_data);
    
    // Access data
    println!("Acceleration: ({:.2}, {:.2}, {:.2}) g",
             physical_data.accel_x_g,
             physical_data.accel_y_g,
             physical_data.accel_z_g);
    
    println!("Gyroscope: ({:.2}, {:.2}, {:.2}) °/s",
             physical_data.gyro_x_dps,
             physical_data.gyro_y_dps,
             physical_data.gyro_z_dps);
    
    Ok(())
}