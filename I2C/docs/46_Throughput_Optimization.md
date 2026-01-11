# I²C Throughput Optimization

## Overview

I²C throughput optimization focuses on maximizing data transfer rates within the constraints of the I²C protocol. While I²C is not the fastest bus (typically 100 kHz to 3.4 MHz), careful optimization can significantly improve performance through batching, burst operations, and efficient protocol usage.

## Key Concepts

**Theoretical Limits**: At 400 kHz (Fast Mode), the theoretical maximum is about 50 KB/s, but practical throughput is often 30-40 KB/s due to protocol overhead (start/stop conditions, addressing, ACK bits).

**Protocol Overhead**: Each I²C transaction includes address bytes, ACK/NACK bits, and start/stop conditions. Minimizing the number of transactions through batching reduces this overhead significantly.

**Burst Transfers**: Many I²C devices support auto-incrementing registers, allowing multiple consecutive registers to be read or written in a single transaction.

## Optimization Strategies

### 1. Batching Operations
Group multiple register accesses into single transactions rather than performing individual read/write operations.

### 2. Burst Mode
Use devices' auto-increment features to transfer blocks of data without repeated addressing overhead.

### 3. Clock Speed Optimization
Use the highest supported clock speed that maintains signal integrity across your hardware setup.

### 4. DMA Integration
On capable microcontrollers, use DMA to handle I²C transfers without CPU intervention.

## C/C++ Implementation

```c
// I2C Throughput Optimization Examples in C/C++
// Demonstrates batching, burst operations, and performance measurement

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Platform-specific I2C HAL (example interface)
typedef struct {
    void* handle;
    uint32_t clock_speed;
} i2c_bus_t;

// Mock HAL functions (replace with actual platform HAL)
extern int hal_i2c_write(i2c_bus_t* bus, uint8_t addr, const uint8_t* data, size_t len);
extern int hal_i2c_read(i2c_bus_t* bus, uint8_t addr, uint8_t* data, size_t len);
extern int hal_i2c_write_read(i2c_bus_t* bus, uint8_t addr, 
                               const uint8_t* tx_data, size_t tx_len,
                               uint8_t* rx_data, size_t rx_len);
extern uint32_t hal_get_tick_ms(void);

// ============================================================================
// NAIVE APPROACH - Low Throughput
// ============================================================================

// Read multiple registers individually (SLOW)
int read_registers_naive(i2c_bus_t* bus, uint8_t device_addr, 
                          uint8_t start_reg, uint8_t* values, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint8_t reg = start_reg + i;
        
        // Each iteration: START + ADDR + REG + RESTART + ADDR + DATA + STOP
        if (hal_i2c_write_read(bus, device_addr, &reg, 1, &values[i], 1) != 0) {
            return -1;
        }
    }
    return 0;
}

// Write multiple registers individually (SLOW)
int write_registers_naive(i2c_bus_t* bus, uint8_t device_addr,
                          uint8_t start_reg, const uint8_t* values, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint8_t buffer[2] = {start_reg + i, values[i]};
        
        // Each iteration: START + ADDR + REG + DATA + STOP
        if (hal_i2c_write(bus, device_addr, buffer, 2) != 0) {
            return -1;
        }
    }
    return 0;
}

// ============================================================================
// OPTIMIZED APPROACH - Burst Transfers
// ============================================================================

// Read multiple consecutive registers in one transaction (FAST)
int read_registers_burst(i2c_bus_t* bus, uint8_t device_addr,
                         uint8_t start_reg, uint8_t* values, size_t count) {
    // Single transaction: START + ADDR + REG + RESTART + ADDR + DATA[0..n] + STOP
    return hal_i2c_write_read(bus, device_addr, &start_reg, 1, values, count);
}

// Write multiple consecutive registers in one transaction (FAST)
int write_registers_burst(i2c_bus_t* bus, uint8_t device_addr,
                          uint8_t start_reg, const uint8_t* values, size_t count) {
    uint8_t buffer[256]; // Adjust size based on max burst length
    
    if (count > 255) return -1; // Buffer overflow protection
    
    buffer[0] = start_reg;
    memcpy(&buffer[1], values, count);
    
    // Single transaction: START + ADDR + REG + DATA[0..n] + STOP
    return hal_i2c_write(bus, device_addr, buffer, count + 1);
}

// ============================================================================
// BATCHED OPERATIONS - Group Multiple Non-Consecutive Registers
// ============================================================================

typedef struct {
    uint8_t reg_addr;
    uint8_t value;
} reg_value_pair_t;

// Write multiple non-consecutive registers efficiently
int write_registers_batched(i2c_bus_t* bus, uint8_t device_addr,
                            const reg_value_pair_t* pairs, size_t count) {
    uint8_t buffer[512]; // Adjust based on needs
    size_t pos = 0;
    
    // Pack all register writes into a single buffer
    for (size_t i = 0; i < count; i++) {
        if (pos + 2 > sizeof(buffer)) return -1;
        buffer[pos++] = pairs[i].reg_addr;
        buffer[pos++] = pairs[i].value;
    }
    
    // Note: This assumes device supports writing reg-value pairs
    // Some devices require separate transactions
    return hal_i2c_write(bus, device_addr, buffer, pos);
}

// ============================================================================
// DMA-BASED TRANSFERS (Platform Specific)
// ============================================================================

typedef struct {
    volatile bool complete;
    int error;
} i2c_dma_transfer_t;

// Callback for DMA completion (called from interrupt context)
void i2c_dma_callback(i2c_dma_transfer_t* transfer, int error) {
    transfer->error = error;
    transfer->complete = true;
}

// Initiate non-blocking DMA transfer
int read_registers_dma(i2c_bus_t* bus, uint8_t device_addr,
                       uint8_t start_reg, uint8_t* values, size_t count,
                       i2c_dma_transfer_t* transfer) {
    transfer->complete = false;
    transfer->error = 0;
    
    // Platform-specific DMA setup would go here
    // Example: hal_i2c_read_dma(bus, device_addr, start_reg, values, count, callback);
    
    return 0;
}

// Wait for DMA transfer to complete
int wait_for_dma(i2c_dma_transfer_t* transfer, uint32_t timeout_ms) {
    uint32_t start = hal_get_tick_ms();
    
    while (!transfer->complete) {
        if (hal_get_tick_ms() - start > timeout_ms) {
            return -1; // Timeout
        }
        // Could use WFI/sleep here to save power
    }
    
    return transfer->error;
}

// ============================================================================
// PERFORMANCE BENCHMARKING
// ============================================================================

typedef struct {
    uint32_t duration_ms;
    size_t bytes_transferred;
    float throughput_kbps;
    uint32_t transactions;
} benchmark_result_t;

benchmark_result_t benchmark_read(i2c_bus_t* bus, uint8_t device_addr,
                                  uint8_t start_reg, size_t count, bool use_burst) {
    uint8_t buffer[256];
    benchmark_result_t result = {0};
    
    uint32_t start = hal_get_tick_ms();
    
    if (use_burst) {
        read_registers_burst(bus, device_addr, start_reg, buffer, count);
        result.transactions = 1;
    } else {
        read_registers_naive(bus, device_addr, start_reg, buffer, count);
        result.transactions = count;
    }
    
    uint32_t end = hal_get_tick_ms();
    
    result.duration_ms = end - start;
    result.bytes_transferred = count;
    result.throughput_kbps = (count * 8.0f) / (result.duration_ms);
    
    return result;
}

// ============================================================================
// EXAMPLE: IMU SENSOR WITH BURST READ
// ============================================================================

#define IMU_ADDR 0x68
#define IMU_REG_ACCEL_X_H 0x3B
#define IMU_DATA_SIZE 14  // 6 accel + 2 temp + 6 gyro bytes

typedef struct {
    int16_t accel_x, accel_y, accel_z;
    int16_t temp;
    int16_t gyro_x, gyro_y, gyro_z;
} imu_data_t;

// Optimized: Read all sensor data in single burst
int read_imu_burst(i2c_bus_t* bus, imu_data_t* data) {
    uint8_t buffer[IMU_DATA_SIZE];
    
    // Single I2C transaction reads all 14 bytes
    if (read_registers_burst(bus, IMU_ADDR, IMU_REG_ACCEL_X_H, 
                            buffer, IMU_DATA_SIZE) != 0) {
        return -1;
    }
    
    // Parse big-endian sensor data
    data->accel_x = (buffer[0] << 8) | buffer[1];
    data->accel_y = (buffer[2] << 8) | buffer[3];
    data->accel_z = (buffer[4] << 8) | buffer[5];
    data->temp = (buffer[6] << 8) | buffer[7];
    data->gyro_x = (buffer[8] << 8) | buffer[9];
    data->gyro_y = (buffer[10] << 8) | buffer[11];
    data->gyro_z = (buffer[12] << 8) | buffer[13];
    
    return 0;
}

// ============================================================================
// CLOCK SPEED OPTIMIZATION
// ============================================================================

typedef enum {
    I2C_SPEED_STANDARD = 100000,   // 100 kHz
    I2C_SPEED_FAST = 400000,       // 400 kHz
    I2C_SPEED_FAST_PLUS = 1000000, // 1 MHz
    I2C_SPEED_HIGH = 3400000       // 3.4 MHz
} i2c_speed_t;

int set_optimal_speed(i2c_bus_t* bus, i2c_speed_t desired_speed) {
    // Platform-specific implementation
    // Should verify signal integrity and device support
    bus->clock_speed = desired_speed;
    
    // Example: hal_i2c_set_clock(bus->handle, desired_speed);
    
    return 0;
}

// ============================================================================
// USAGE EXAMPLE
// ============================================================================

void example_usage(void) {
    i2c_bus_t bus = {0};
    bus.clock_speed = I2C_SPEED_FAST;
    
    // Configure for maximum throughput
    set_optimal_speed(&bus, I2C_SPEED_FAST);
    
    // Benchmark comparison
    benchmark_result_t naive = benchmark_read(&bus, IMU_ADDR, 0x00, 100, false);
    benchmark_result_t burst = benchmark_read(&bus, IMU_ADDR, 0x00, 100, true);
    
    // Burst mode typically 5-10x faster for sequential reads
    
    // Read IMU data efficiently
    imu_data_t imu;
    read_imu_burst(&bus, &imu);
    
    // Batch write configuration registers
    reg_value_pair_t config[] = {
        {0x6B, 0x00},  // PWR_MGMT_1: Wake up
        {0x1B, 0x18},  // GYRO_CONFIG: ±2000°/s
        {0x1C, 0x10},  // ACCEL_CONFIG: ±8g
    };
    write_registers_batched(&bus, IMU_ADDR, config, 3);
}
```

## Rust Implementation

```rust
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
```

## Performance Comparison

Here's what you can expect from these optimization techniques:

### Throughput Gains
- **Naive approach** (individual transactions): ~10-15 KB/s at 400 kHz
- **Burst transfers**: ~35-45 KB/s at 400 kHz (3-4x improvement)
- **DMA with burst**: ~40-50 KB/s at 400 kHz (minimal CPU overhead)

### Transaction Overhead
Reading 14 bytes of IMU data:
- **Naive**: 14 transactions × ~500 µs = 7 ms
- **Burst**: 1 transaction × ~600 µs = 0.6 ms (11x faster)

## Key Takeaways

1. **Burst Mode is Essential**: Always use burst reads/writes for consecutive registers. This single optimization provides the biggest performance gain.

2. **Minimize Transactions**: Each I²C transaction has overhead (start condition, addressing, stop condition). Batching reduces this overhead dramatically.

3. **Use DMA When Available**: For large transfers, DMA frees the CPU and can improve system responsiveness without sacrificing throughput.

4. **Clock Speed Matters**: Use the highest reliable clock speed, but verify signal integrity. Going from 100 kHz to 400 kHz provides a 4x theoretical improvement.

5. **Zero-Copy Techniques**: In Rust, pre-format buffers to avoid intermediate copies. This is particularly important in embedded systems with limited RAM.

6. **Know Your Device**: Check datasheets for auto-increment support and maximum burst length limitations.

The combination of these techniques can improve I²C throughput by an order of magnitude while reducing CPU usage, making them essential for any performance-critical I²C application.