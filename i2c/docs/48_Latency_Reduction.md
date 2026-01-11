# I²C Latency Reduction

## Overview

Latency in I²C communications refers to the time delay between initiating a transaction and completing it. In time-critical systems like industrial control, automotive applications, and real-time sensor networks, minimizing this latency is crucial for system responsiveness and determinism.

## Sources of I²C Latency

**1. Clock Stretching**
When slave devices hold SCL low to pause communication while processing data.

**2. Arbitration and Bus Contention**
In multi-master systems, arbitration delays occur when multiple masters attempt simultaneous access.

**3. Interrupt Handling Overhead**
Context switching and ISR execution time add latency in interrupt-driven implementations.

**4. Protocol Overhead**
Address frames, ACK/NACK bits, and repeated starts add fixed overhead to each transaction.

**5. DMA Setup Time**
Configuration and setup of DMA transfers can introduce delays.

**6. Software Processing**
Application layer processing between transactions.

## Latency Reduction Techniques

### 1. Use DMA Instead of Interrupt-Driven I/O

DMA eliminates per-byte interrupt overhead, significantly reducing CPU involvement and latency.

**C Example (STM32):**

```c
#include "stm32f4xx_hal.h"

I2C_HandleTypeDef hi2c1;
DMA_HandleTypeDef hdma_i2c1_tx;
DMA_HandleTypeDef hdma_i2c1_rx;

// Initialize I2C with DMA
void I2C_DMA_Init(void) {
    // Enable clocks
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    
    // Configure I2C
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000;  // Fast mode
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    // DMA configuration for TX
    hdma_i2c1_tx.Instance = DMA1_Stream6;
    hdma_i2c1_tx.Init.Channel = DMA_CHANNEL_1;
    hdma_i2c1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_i2c1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_i2c1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_i2c1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_i2c1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_i2c1_tx.Init.Mode = DMA_NORMAL;
    hdma_i2c1_tx.Init.Priority = DMA_PRIORITY_HIGH;
    
    HAL_DMA_Init(&hdma_i2c1_tx);
    __HAL_LINKDMA(&hi2c1, hdmatx, hdma_i2c1_tx);
    
    // Similar for RX DMA
    hdma_i2c1_rx.Instance = DMA1_Stream0;
    hdma_i2c1_rx.Init.Channel = DMA_CHANNEL_1;
    hdma_i2c1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_i2c1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_i2c1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_i2c1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_i2c1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_i2c1_rx.Init.Mode = DMA_NORMAL;
    hdma_i2c1_rx.Init.Priority = DMA_PRIORITY_HIGH;
    
    HAL_DMA_Init(&hdma_i2c1_rx);
    __HAL_LINKDMA(&hi2c1, hdmarx, hdma_i2c1_rx);
    
    HAL_I2C_Init(&hi2c1);
}

// Fast DMA read with minimal latency
HAL_StatusTypeDef Fast_I2C_Read(uint16_t dev_addr, uint8_t reg_addr, 
                                 uint8_t* data, uint16_t size) {
    // Write register address first
    if (HAL_I2C_Master_Transmit(&hi2c1, dev_addr, &reg_addr, 1, 100) != HAL_OK) {
        return HAL_ERROR;
    }
    
    // Read data using DMA
    return HAL_I2C_Master_Receive_DMA(&hi2c1, dev_addr, data, size);
}
```

### 2. Increase Bus Speed

Running at higher speeds (Fast Mode 400kHz, Fast Mode Plus 1MHz, or High Speed 3.4MHz) directly reduces transmission time.

**C++ Example with Speed Configuration:**

```cpp
#include <cstdint>
#include <chrono>

class LowLatencyI2C {
private:
    volatile uint32_t* i2c_base;
    uint32_t bus_speed;
    
    // Calculate timing parameters for minimal latency
    void ConfigureTiming(uint32_t target_speed) {
        // For STM32-style timing register
        // These values depend on peripheral clock and target speed
        uint32_t presc = 0;
        uint32_t scldel = 0;
        uint32_t sdadel = 0;
        uint32_t sclh = 0;
        uint32_t scll = 0;
        
        if (target_speed == 1000000) {  // 1MHz Fast Mode Plus
            presc = 0;
            scldel = 2;
            sdadel = 0;
            sclh = 4;
            scll = 9;
        } else if (target_speed == 400000) {  // 400kHz Fast Mode
            presc = 1;
            scldel = 3;
            sdadel = 2;
            sclh = 9;
            scll = 19;
        }
        
        // Configure timing register (pseudo-code)
        uint32_t timing = (presc << 28) | (scldel << 20) | 
                         (sdadel << 16) | (sclh << 8) | scll;
        i2c_base[0x04] = timing;  // TIMINGR offset
    }
    
public:
    LowLatencyI2C(void* base_addr, uint32_t speed) 
        : i2c_base(static_cast<volatile uint32_t*>(base_addr)),
          bus_speed(speed) {
        ConfigureTiming(speed);
    }
    
    // Optimized write with timing measurement
    uint32_t FastWrite(uint8_t device_addr, const uint8_t* data, size_t len) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Generate START condition
        i2c_base[0x00] |= (1 << 13);  // START bit in CR2
        
        // Send device address with write bit
        i2c_base[0x08] = (device_addr << 1) | 0x00;
        
        // Wait for address ACK with timeout
        uint32_t timeout = 1000;
        while (!(i2c_base[0x18] & 0x02) && timeout--);  // ADDR flag
        
        // Transmit data bytes
        for (size_t i = 0; i < len; i++) {
            i2c_base[0x28] = data[i];  // TXDR
            timeout = 1000;
            while (!(i2c_base[0x18] & 0x01) && timeout--);  // TXE flag
        }
        
        // Generate STOP condition
        i2c_base[0x00] |= (1 << 14);  // STOP bit
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        return duration.count();
    }
    
    // Burst read optimized for latency
    bool BurstRead(uint8_t device_addr, uint8_t reg_addr, 
                   uint8_t* buffer, size_t len) {
        // Combined write-read transaction (no STOP in between)
        // This reduces latency by ~100-200µs compared to separate transactions
        
        // Write register address
        i2c_base[0x00] |= (1 << 13);  // START
        i2c_base[0x08] = (device_addr << 1) | 0x00;  // Write
        
        uint32_t timeout = 1000;
        while (!(i2c_base[0x18] & 0x02) && timeout--);
        
        i2c_base[0x28] = reg_addr;
        timeout = 1000;
        while (!(i2c_base[0x18] & 0x01) && timeout--);
        
        // Repeated START for read
        i2c_base[0x00] |= (1 << 13);  // Repeated START
        i2c_base[0x08] = (device_addr << 1) | 0x01;  // Read
        
        timeout = 1000;
        while (!(i2c_base[0x18] & 0x02) && timeout--);
        
        // Read data
        for (size_t i = 0; i < len; i++) {
            timeout = 1000;
            while (!(i2c_base[0x18] & 0x04) && timeout--);  // RXNE flag
            buffer[i] = i2c_base[0x24];  // RXDR
        }
        
        i2c_base[0x00] |= (1 << 14);  // STOP
        
        return true;
    }
};
```

### 3. Disable Clock Stretching (When Possible)

Some microcontrollers allow disabling clock stretching to prevent slaves from adding latency.

**C Example:**

```c
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t OAR1;
    volatile uint32_t OAR2;
    volatile uint32_t TIMINGR;
    volatile uint32_t TIMEOUTR;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t PECR;
    volatile uint32_t RXDR;
    volatile uint32_t TXDR;
} I2C_TypeDef;

#define I2C_CR1_NOSTRETCH  (1 << 17)
#define I2C_CR1_PE         (1 << 0)

void I2C_DisableClockStretching(I2C_TypeDef* i2c) {
    // Disable peripheral first
    i2c->CR1 &= ~I2C_CR1_PE;
    
    // Enable no-stretch mode
    i2c->CR1 |= I2C_CR1_NOSTRETCH;
    
    // Re-enable peripheral
    i2c->CR1 |= I2C_CR1_PE;
}

// Ultra-low latency write without clock stretching
bool NoStretch_Write(I2C_TypeDef* i2c, uint8_t addr, 
                     const uint8_t* data, uint8_t len) {
    // With clock stretching disabled, we must ensure
    // we can write data fast enough
    
    // Start condition
    i2c->CR2 = (addr << 1) | (len << 16) | (1 << 13);  // AUTOEND
    i2c->CR2 |= (1 << 13);  // START
    
    for (uint8_t i = 0; i < len; i++) {
        // Poll for TX buffer empty - must be fast!
        uint32_t timeout = 10000;
        while (!(i2c->ISR & 0x01) && --timeout);
        
        if (timeout == 0) return false;
        
        i2c->TXDR = data[i];
    }
    
    // Wait for completion
    while (!(i2c->ISR & 0x20));  // STOPF
    i2c->ICR = 0x20;  // Clear STOPF
    
    return true;
}
```

### 4. Transaction Batching and Pipelining

Group multiple register reads/writes into single transactions to reduce overhead.

**C++ Example:**

```cpp
#include <vector>
#include <array>

class BatchedI2C {
private:
    int fd;  // Linux I2C file descriptor
    
public:
    struct Transaction {
        uint8_t device_addr;
        uint8_t reg_addr;
        uint8_t* data;
        uint16_t length;
        bool is_read;
    };
    
    // Batch multiple transactions to minimize latency
    bool ExecuteBatch(const std::vector<Transaction>& transactions) {
        for (const auto& trans : transactions) {
            if (trans.is_read) {
                // Combined write-read for each transaction
                struct i2c_msg msgs[2] = {
                    {
                        .addr = trans.device_addr,
                        .flags = 0,  // Write
                        .len = 1,
                        .buf = &trans.reg_addr
                    },
                    {
                        .addr = trans.device_addr,
                        .flags = I2C_M_RD,  // Read
                        .len = trans.length,
                        .buf = trans.data
                    }
                };
                
                struct i2c_rdwr_ioctl_data ioctl_data = {
                    .msgs = msgs,
                    .nmsgs = 2
                };
                
                if (ioctl(fd, I2C_RDWR, &ioctl_data) < 0) {
                    return false;
                }
            } else {
                // Write transaction
                uint8_t buffer[trans.length + 1];
                buffer[0] = trans.reg_addr;
                memcpy(&buffer[1], trans.data, trans.length);
                
                struct i2c_msg msg = {
                    .addr = trans.device_addr,
                    .flags = 0,
                    .len = static_cast<uint16_t>(trans.length + 1),
                    .buf = buffer
                };
                
                struct i2c_rdwr_ioctl_data ioctl_data = {
                    .msgs = &msg,
                    .nmsgs = 1
                };
                
                if (ioctl(fd, I2C_RDWR, &ioctl_data) < 0) {
                    return false;
                }
            }
        }
        return true;
    }
    
    // Read multiple registers in one transaction
    bool MultiRegisterRead(uint8_t device_addr, uint8_t start_reg, 
                          uint8_t* buffer, uint8_t count) {
        // Single transaction reading consecutive registers
        // Saves ~200µs per additional register vs individual reads
        
        struct i2c_msg msgs[2] = {
            {
                .addr = device_addr,
                .flags = 0,
                .len = 1,
                .buf = &start_reg
            },
            {
                .addr = device_addr,
                .flags = I2C_M_RD,
                .len = count,
                .buf = buffer
            }
        };
        
        struct i2c_rdwr_ioctl_data ioctl_data = {
            .msgs = msgs,
            .nmsgs = 2
        };
        
        return ioctl(fd, I2C_RDWR, &ioctl_data) >= 0;
    }
};
```

### 5. Use Polling Instead of Interrupts for Short Transactions

For very short transactions, polling can be faster than interrupt overhead.

**Rust Example:**

```rust
use std::time::Instant;

pub struct LowLatencyI2C {
    device: i2c_linux::I2c<std::fs::File>,
}

impl LowLatencyI2C {
    pub fn new(bus: u8) -> Result<Self, Box<dyn std::error::Error>> {
        let mut device = i2c_linux::I2c::from_path(format!("/dev/i2c-{}", bus))?;
        Ok(Self { device })
    }
    
    /// Polling-based write for minimal latency on short transactions
    pub fn fast_write(&mut self, addr: u16, data: &[u8]) -> Result<u64, Box<dyn std::error::Error>> {
        let start = Instant::now();
        
        self.device.set_slave_address(addr)?;
        self.device.write(data)?;
        
        let elapsed = start.elapsed();
        Ok(elapsed.as_micros() as u64)
    }
    
    /// Ultra-low latency register read using combined transaction
    pub fn fast_register_read(&mut self, addr: u16, reg: u8, buffer: &mut [u8]) 
        -> Result<u64, Box<dyn std::error::Error>> {
        let start = Instant::now();
        
        self.device.set_slave_address(addr)?;
        
        // Combined write-read transaction (no STOP between)
        self.device.write(&[reg])?;
        self.device.read(buffer)?;
        
        let elapsed = start.elapsed();
        Ok(elapsed.as_micros() as u64)
    }
    
    /// Burst read for multiple consecutive registers
    pub fn burst_read(&mut self, addr: u16, start_reg: u8, buffer: &mut [u8])
        -> Result<(), Box<dyn std::error::Error>> {
        self.device.set_slave_address(addr)?;
        
        // Write starting register
        self.device.write(&[start_reg])?;
        
        // Read all data in one transaction
        self.device.read(buffer)?;
        
        Ok(())
    }
}

/// Latency measurement and benchmarking
pub struct LatencyBenchmark {
    measurements: Vec<u64>,
}

impl LatencyBenchmark {
    pub fn new() -> Self {
        Self {
            measurements: Vec::with_capacity(1000),
        }
    }
    
    pub fn measure_transaction<F>(&mut self, mut transaction: F) 
        where F: FnMut() -> Result<(), Box<dyn std::error::Error>> {
        let start = Instant::now();
        
        if transaction().is_ok() {
            let elapsed = start.elapsed().as_micros() as u64;
            self.measurements.push(elapsed);
        }
    }
    
    pub fn statistics(&self) -> TransactionStats {
        if self.measurements.is_empty() {
            return TransactionStats::default();
        }
        
        let mut sorted = self.measurements.clone();
        sorted.sort_unstable();
        
        let sum: u64 = sorted.iter().sum();
        let count = sorted.len();
        
        TransactionStats {
            min: sorted[0],
            max: sorted[count - 1],
            avg: sum / count as u64,
            p50: sorted[count / 2],
            p95: sorted[(count * 95) / 100],
            p99: sorted[(count * 99) / 100],
        }
    }
}

#[derive(Default, Debug)]
pub struct TransactionStats {
    pub min: u64,
    pub max: u64,
    pub avg: u64,
    pub p50: u64,
    pub p95: u64,
    pub p99: u64,
}

// Example usage
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut i2c = LowLatencyI2C::new(1)?;
    let mut benchmark = LatencyBenchmark::new();
    
    // Benchmark 1000 transactions
    for _ in 0..1000 {
        benchmark.measure_transaction(|| {
            let mut buffer = [0u8; 6];
            i2c.burst_read(0x68, 0x3B, &mut buffer)?;
            Ok(())
        });
    }
    
    let stats = benchmark.statistics();
    println!("Transaction Latency Statistics (µs):");
    println!("  Min: {} µs", stats.min);
    println!("  Avg: {} µs", stats.avg);
    println!("  P50: {} µs", stats.p50);
    println!("  P95: {} µs", stats.p95);
    println!("  P99: {} µs", stats.p99);
    println!("  Max: {} µs", stats.max);
    
    Ok(())
}
```

### 6. Optimize Interrupt Priority and Handlers

**C Example with NVIC Configuration:**

```c
#include "stm32f4xx.h"

// Configure highest priority for I2C interrupts
void I2C_HighPriorityInit(void) {
    // Set I2C event interrupt to highest priority (0)
    NVIC_SetPriority(I2C1_EV_IRQn, 0);
    NVIC_EnableIRQ(I2C1_EV_IRQn);
    
    // Error interrupt also high priority
    NVIC_SetPriority(I2C1_ER_IRQn, 0);
    NVIC_EnableIRQ(I2C1_ER_IRQn);
}

// Minimal ISR for lowest latency
volatile uint8_t* tx_buffer_ptr;
volatile uint8_t tx_count;
volatile bool tx_complete;

void I2C1_EV_IRQHandler(void) {
    uint32_t sr1 = I2C1->SR1;
    
    if (sr1 & I2C_SR1_TXE) {  // TX buffer empty
        if (tx_count > 0) {
            I2C1->DR = *tx_buffer_ptr++;
            tx_count--;
        } else {
            I2C1->CR1 |= I2C_CR1_STOP;
            tx_complete = true;
        }
    }
}

// Fast polling-based alternative for time-critical paths
inline bool I2C_FastWrite_Polling(uint8_t addr, const uint8_t* data, uint8_t len) {
    // Generate START
    I2C1->CR1 |= I2C_CR1_START;
    while (!(I2C1->SR1 & I2C_SR1_SB));
    
    // Send address
    I2C1->DR = addr << 1;
    while (!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR2;  // Clear ADDR flag
    
    // Send data
    for (uint8_t i = 0; i < len; i++) {
        while (!(I2C1->SR1 & I2C_SR1_TXE));
        I2C1->DR = data[i];
    }
    
    // Wait for BTF and send STOP
    while (!(I2C1->SR1 & I2C_SR1_BTF));
    I2C1->CR1 |= I2C_CR1_STOP;
    
    return true;
}
```

### 7. Pre-compute and Cache Values

**Rust Example with Caching:**

```rust
use std::collections::HashMap;
use std::time::{Duration, Instant};

pub struct CachedI2C {
    device: i2c_linux::I2c<std::fs::File>,
    cache: HashMap<(u16, u8), (Vec<u8>, Instant)>,
    cache_ttl: Duration,
}

impl CachedI2C {
    pub fn new(bus: u8, cache_ttl_ms: u64) -> Result<Self, Box<dyn std::error::Error>> {
        let device = i2c_linux::I2c::from_path(format!("/dev/i2c-{}", bus))?;
        
        Ok(Self {
            device,
            cache: HashMap::new(),
            cache_ttl: Duration::from_millis(cache_ttl_ms),
        })
    }
    
    /// Read with caching for frequently accessed registers
    pub fn cached_read(&mut self, addr: u16, reg: u8, buffer: &mut [u8]) 
        -> Result<bool, Box<dyn std::error::Error>> {
        let key = (addr, reg);
        let now = Instant::now();
        
        // Check cache
        if let Some((cached_data, timestamp)) = self.cache.get(&key) {
            if now.duration_since(*timestamp) < self.cache_ttl {
                buffer.copy_from_slice(&cached_data[..buffer.len()]);
                return Ok(true);  // Cache hit
            }
        }
        
        // Cache miss - perform actual I2C transaction
        self.device.set_slave_address(addr)?;
        self.device.write(&[reg])?;
        self.device.read(buffer)?;
        
        // Update cache
        self.cache.insert(key, (buffer.to_vec(), now));
        
        Ok(false)  // Cache miss
    }
    
    /// Clear cache for specific register
    pub fn invalidate_cache(&mut self, addr: u16, reg: u8) {
        self.cache.remove(&(addr, reg));
    }
    
    /// Clear entire cache
    pub fn clear_cache(&mut self) {
        self.cache.clear();
    }
}
```

### 8. Hardware-Specific Optimizations

**Rust Example with Direct Register Access:**

```rust
use core::ptr::{read_volatile, write_volatile};

// Direct hardware access for absolute minimum latency
pub struct DirectI2C {
    base_addr: usize,
}

impl DirectI2C {
    const CR1_OFFSET: usize = 0x00;
    const CR2_OFFSET: usize = 0x04;
    const ISR_OFFSET: usize = 0x18;
    const TXDR_OFFSET: usize = 0x28;
    const RXDR_OFFSET: usize = 0x24;
    
    pub unsafe fn new(base_addr: usize) -> Self {
        Self { base_addr }
    }
    
    #[inline(always)]
    unsafe fn write_reg(&self, offset: usize, value: u32) {
        write_volatile((self.base_addr + offset) as *mut u32, value);
    }
    
    #[inline(always)]
    unsafe fn read_reg(&self, offset: usize) -> u32 {
        read_volatile((self.base_addr + offset) as *const u32)
    }
    
    /// Ultra-low latency write with direct register manipulation
    pub unsafe fn ultra_fast_write(&self, addr: u8, data: &[u8]) -> bool {
        // Configure transfer
        let cr2_val = ((addr as u32) << 1) | 
                      ((data.len() as u32) << 16) | 
                      (1 << 13) |  // START
                      (1 << 25);   // AUTOEND
        
        self.write_reg(Self::CR2_OFFSET, cr2_val);
        
        // Transmit data with minimal overhead
        for &byte in data {
            // Wait for TXIS flag (bit 1)
            let mut timeout = 10000u32;
            while (self.read_reg(Self::ISR_OFFSET) & 0x02) == 0 {
                timeout -= 1;
                if timeout == 0 { return false; }
            }
            
            self.write_reg(Self::TXDR_OFFSET, byte as u32);
        }
        
        // Wait for STOPF (bit 5)
        let mut timeout = 10000u32;
        while (self.read_reg(Self::ISR_OFFSET) & 0x20) == 0 {
            timeout -= 1;
            if timeout == 0 { return false; }
        }
        
        true
    }
}
```

## Latency Comparison

Typical latency ranges for different approaches:

| Technique | Latency Range | Use Case |
|-----------|---------------|----------|
| Interrupt-driven (per-byte) | 500-2000 µs | General purpose |
| Interrupt-driven (buffered) | 200-800 µs | Moderate performance |
| DMA-based | 100-400 µs | High throughput |
| Polling (short transactions) | 50-200 µs | Time-critical |
| Direct register + no stretch | 20-100 µs | Ultra-low latency |
| Cached reads | 1-10 µs | Frequently accessed data |

## Best Practices

1. **Profile your transactions** - Measure actual latency to identify bottlenecks
2. **Use the fastest bus speed** your hardware supports reliably
3. **Batch transactions** when possible to amortize overhead
4. **Consider DMA** for transactions > 16 bytes
5. **Use polling** for very short (<8 bytes) time-critical transactions
6. **Disable clock stretching** only if slaves can keep up
7. **Prioritize I2C interrupts** appropriately in your system
8. **Cache static or slow-changing data** to avoid unnecessary reads
9. **Minimize processing** between transactions in interrupt handlers
10. **Use combined write-read** transactions instead of separate operations

These techniques can reduce I2C transaction latency from milliseconds down to tens of microseconds, making I²C viable even for time-critical applications.