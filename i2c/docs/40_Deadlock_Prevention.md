# I2C Deadlock Prevention in Multi-Master Configurations

## Overview

In multi-master I2C configurations, **deadlock** occurs when two or more masters attempt to communicate simultaneously, leading to a situation where neither can proceed. This happens because I2C uses a shared bus with clock stretching and arbitration mechanisms that can cause masters to wait indefinitely for each other.

## Common Deadlock Scenarios

### 1. **Clock Stretching Deadlock**
When a slave holds SCL low (clock stretching) while a master is also trying to control the clock, both devices can end up waiting for each other.

### 2. **Arbitration Deadlock**
Multiple masters start transmission simultaneously, and after losing arbitration, they retry indefinitely without proper backoff strategies.

### 3. **Bus Lockup**
A master crashes mid-transaction while holding SDA low, preventing other masters from accessing the bus.

## Prevention Strategies

### 1. **Timeout Mechanisms**
Always implement timeouts to detect hung transactions.

### 2. **Proper Arbitration Handling**
Implement exponential backoff after losing arbitration.

### 3. **Bus Recovery Procedures**
Implement clock pulse generation to recover from lockup states.

### 4. **Mutex/Semaphore Protection**
Use OS-level synchronization primitives in multi-threaded environments.

---

## C/C++ Implementation Examples

### Basic Timeout Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define I2C_TIMEOUT_MS 1000
#define I2C_MAX_RETRIES 3
#define I2C_BACKOFF_BASE_MS 10

typedef enum {
    I2C_SUCCESS = 0,
    I2C_ERROR_TIMEOUT,
    I2C_ERROR_ARBITRATION_LOST,
    I2C_ERROR_NACK,
    I2C_ERROR_BUS_BUSY
} i2c_status_t;

typedef struct {
    volatile uint32_t *control_reg;
    volatile uint32_t *status_reg;
    volatile uint32_t *data_reg;
    uint32_t clock_speed;
} i2c_master_t;

// Get current time in milliseconds
static uint32_t get_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

// Wait for a condition with timeout
static i2c_status_t wait_for_condition(
    volatile uint32_t *status_reg,
    uint32_t mask,
    uint32_t expected,
    uint32_t timeout_ms
) {
    uint32_t start = get_tick_ms();
    
    while (((*status_reg & mask) != expected)) {
        if ((get_tick_ms() - start) > timeout_ms) {
            return I2C_ERROR_TIMEOUT;
        }
    }
    
    return I2C_SUCCESS;
}

// Check if arbitration was lost
static bool check_arbitration_lost(volatile uint32_t *status_reg) {
    return (*status_reg & (1 << 3)); // Bit 3: Arbitration lost flag
}

// Exponential backoff delay
static void backoff_delay(uint32_t retry_count) {
    uint32_t delay_ms = I2C_BACKOFF_BASE_MS * (1 << retry_count);
    if (delay_ms > 1000) delay_ms = 1000; // Cap at 1 second
    
    struct timespec ts = {
        .tv_sec = delay_ms / 1000,
        .tv_nsec = (delay_ms % 1000) * 1000000
    };
    nanosleep(&ts, NULL);
}

// I2C write with deadlock prevention
i2c_status_t i2c_write_safe(
    i2c_master_t *master,
    uint8_t slave_addr,
    const uint8_t *data,
    size_t len
) {
    i2c_status_t status;
    uint32_t retry_count = 0;
    
    while (retry_count < I2C_MAX_RETRIES) {
        // Wait for bus to be free
        status = wait_for_condition(
            master->status_reg,
            (1 << 5), // Bit 5: Bus busy flag
            0,
            I2C_TIMEOUT_MS
        );
        
        if (status != I2C_SUCCESS) {
            return status;
        }
        
        // Generate START condition
        *(master->control_reg) |= (1 << 0); // START bit
        
        // Wait for START to complete
        status = wait_for_condition(
            master->status_reg,
            (1 << 1), // Bit 1: START complete
            (1 << 1),
            I2C_TIMEOUT_MS
        );
        
        if (status != I2C_SUCCESS) {
            goto error_recovery;
        }
        
        // Send slave address with write bit
        *(master->data_reg) = (slave_addr << 1) | 0;
        
        // Wait for ACK
        status = wait_for_condition(
            master->status_reg,
            (1 << 2), // Bit 2: Transfer complete
            (1 << 2),
            I2C_TIMEOUT_MS
        );
        
        if (status != I2C_SUCCESS) {
            goto error_recovery;
        }
        
        // Check for arbitration loss
        if (check_arbitration_lost(master->status_reg)) {
            retry_count++;
            backoff_delay(retry_count);
            continue; // Retry transaction
        }
        
        // Check for NACK
        if (*(master->status_reg) & (1 << 4)) {
            status = I2C_ERROR_NACK;
            goto error_recovery;
        }
        
        // Send data bytes
        for (size_t i = 0; i < len; i++) {
            *(master->data_reg) = data[i];
            
            status = wait_for_condition(
                master->status_reg,
                (1 << 2),
                (1 << 2),
                I2C_TIMEOUT_MS
            );
            
            if (status != I2C_SUCCESS) {
                goto error_recovery;
            }
            
            if (check_arbitration_lost(master->status_reg)) {
                retry_count++;
                backoff_delay(retry_count);
                goto retry_transaction;
            }
        }
        
        // Generate STOP condition
        *(master->control_reg) |= (1 << 1); // STOP bit
        
        return I2C_SUCCESS;
        
retry_transaction:
        continue;
        
error_recovery:
        // Generate STOP to release bus
        *(master->control_reg) |= (1 << 1);
        return status;
    }
    
    return I2C_ERROR_ARBITRATION_LOST;
}
```

### Bus Recovery Implementation

```c
#include <stdint.h>
#include <stdbool.h>

#define I2C_RECOVERY_CLOCKS 9

// GPIO access for manual bus recovery
typedef struct {
    volatile uint32_t *scl_port;
    volatile uint32_t *sda_port;
    uint32_t scl_pin;
    uint32_t sda_pin;
} i2c_gpio_t;

// Bus recovery by generating clock pulses
bool i2c_bus_recovery(i2c_gpio_t *gpio) {
    // Switch pins to GPIO mode
    // Configure SCL as output, SDA as input
    
    for (int i = 0; i < I2C_RECOVERY_CLOCKS; i++) {
        // Generate clock pulse
        *(gpio->scl_port) &= ~(1 << gpio->scl_pin); // SCL low
        delay_us(5);
        *(gpio->scl_port) |= (1 << gpio->scl_pin);  // SCL high
        delay_us(5);
        
        // Check if SDA is released
        if (*(gpio->sda_port) & (1 << gpio->sda_pin)) {
            // SDA is high, bus recovered
            // Generate STOP condition
            *(gpio->sda_port) &= ~(1 << gpio->sda_pin); // SDA low
            delay_us(5);
            *(gpio->scl_port) |= (1 << gpio->scl_pin);  // SCL high
            delay_us(5);
            *(gpio->sda_port) |= (1 << gpio->sda_pin);  // SDA high
            delay_us(5);
            
            // Switch back to I2C mode
            return true;
        }
    }
    
    return false; // Recovery failed
}
```

### Thread-Safe Multi-Master Implementation (C++)

```cpp
#include <mutex>
#include <chrono>
#include <thread>
#include <random>

class I2CMaster {
private:
    static std::timed_mutex bus_mutex;
    i2c_master_t hw_interface;
    std::mt19937 rng;
    
    static constexpr int MAX_RETRIES = 3;
    static constexpr int BASE_BACKOFF_MS = 10;
    static constexpr int LOCK_TIMEOUT_MS = 1000;
    
    void exponential_backoff(int retry_count) {
        std::uniform_int_distribution<int> dist(0, 50); // Add jitter
        int delay = BASE_BACKOFF_MS * (1 << retry_count) + dist(rng);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
    
public:
    I2CMaster() : rng(std::random_device{}()) {}
    
    enum class Status {
        Success,
        Timeout,
        ArbitrationLost,
        Nack,
        LockTimeout
    };
    
    Status write(uint8_t slave_addr, const uint8_t* data, size_t len) {
        // Try to acquire bus mutex with timeout
        auto timeout = std::chrono::milliseconds(LOCK_TIMEOUT_MS);
        
        if (!bus_mutex.try_lock_for(timeout)) {
            return Status::LockTimeout;
        }
        
        std::lock_guard<std::timed_mutex> lock(bus_mutex, std::adopt_lock);
        
        for (int retry = 0; retry < MAX_RETRIES; retry++) {
            Status result = perform_write(slave_addr, data, len);
            
            if (result == Status::Success) {
                return Status::Success;
            }
            
            if (result == Status::ArbitrationLost) {
                exponential_backoff(retry);
                continue; // Retry
            }
            
            // Other errors - don't retry
            return result;
        }
        
        return Status::ArbitrationLost;
    }
    
private:
    Status perform_write(uint8_t slave_addr, const uint8_t* data, size_t len) {
        // Hardware-level I2C transaction
        // (Similar to C implementation above)
        return Status::Success;
    }
};

// Static mutex shared by all I2C master instances
std::timed_mutex I2CMaster::bus_mutex;
```

---

## Rust Implementation Examples

### Safe I2C with Timeout and Retry Logic

```rust
use std::time::{Duration, Instant};
use std::thread;
use rand::Rng;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cError {
    Timeout,
    ArbitrationLost,
    Nack,
    BusBusy,
}

pub type I2cResult<T> = Result<T, I2cError>;

const I2C_TIMEOUT: Duration = Duration::from_millis(1000);
const MAX_RETRIES: u32 = 3;
const BASE_BACKOFF_MS: u64 = 10;

pub struct I2cMaster {
    // Hardware register pointers (using volatile)
    control_reg: *mut u32,
    status_reg: *mut u32,
    data_reg: *mut u32,
}

// Implement Send and Sync for multi-threaded use
unsafe impl Send for I2cMaster {}
unsafe impl Sync for I2cMaster {}

impl I2cMaster {
    pub fn new(base_addr: usize) -> Self {
        Self {
            control_reg: base_addr as *mut u32,
            status_reg: (base_addr + 4) as *mut u32,
            data_reg: (base_addr + 8) as *mut u32,
        }
    }
    
    // Wait for a condition with timeout
    fn wait_for_condition(
        &self,
        mask: u32,
        expected: u32,
        timeout: Duration,
    ) -> I2cResult<()> {
        let start = Instant::now();
        
        while unsafe { (*self.status_reg & mask) != expected } {
            if start.elapsed() > timeout {
                return Err(I2cError::Timeout);
            }
            // Yield to avoid busy-waiting
            thread::yield_now();
        }
        
        Ok(())
    }
    
    // Check if arbitration was lost
    fn check_arbitration_lost(&self) -> bool {
        unsafe { (*self.status_reg & (1 << 3)) != 0 }
    }
    
    // Exponential backoff with jitter
    fn backoff_delay(retry_count: u32) {
        let mut rng = rand::thread_rng();
        let base_delay = BASE_BACKOFF_MS * (1_u64 << retry_count);
        let jitter = rng.gen_range(0..50);
        let delay = base_delay.min(1000) + jitter;
        
        thread::sleep(Duration::from_millis(delay));
    }
    
    // Safe I2C write with deadlock prevention
    pub fn write(&mut self, slave_addr: u8, data: &[u8]) -> I2cResult<()> {
        let mut retry_count = 0;
        
        while retry_count < MAX_RETRIES {
            // Wait for bus to be free
            self.wait_for_condition(1 << 5, 0, I2C_TIMEOUT)?;
            
            // Generate START condition
            unsafe {
                *self.control_reg |= 1 << 0;
            }
            
            // Wait for START to complete
            self.wait_for_condition(1 << 1, 1 << 1, I2C_TIMEOUT)?;
            
            // Send slave address with write bit
            unsafe {
                *self.data_reg = ((slave_addr << 1) | 0) as u32;
            }
            
            // Wait for transfer complete
            self.wait_for_condition(1 << 2, 1 << 2, I2C_TIMEOUT)?;
            
            // Check for arbitration loss
            if self.check_arbitration_lost() {
                retry_count += 1;
                Self::backoff_delay(retry_count);
                continue; // Retry transaction
            }
            
            // Check for NACK
            if unsafe { (*self.status_reg & (1 << 4)) != 0 } {
                self.generate_stop();
                return Err(I2cError::Nack);
            }
            
            // Send data bytes
            for &byte in data {
                unsafe {
                    *self.data_reg = byte as u32;
                }
                
                self.wait_for_condition(1 << 2, 1 << 2, I2C_TIMEOUT)?;
                
                if self.check_arbitration_lost() {
                    retry_count += 1;
                    Self::backoff_delay(retry_count);
                    continue; // Retry entire transaction
                }
            }
            
            // Generate STOP condition
            self.generate_stop();
            
            return Ok(());
        }
        
        Err(I2cError::ArbitrationLost)
    }
    
    fn generate_stop(&mut self) {
        unsafe {
            *self.control_reg |= 1 << 1; // STOP bit
        }
    }
}
```

### Thread-Safe Multi-Master with Mutex

```rust
use std::sync::{Arc, Mutex, MutexGuard};
use std::time::Duration;

pub struct SharedI2cBus {
    master: Arc<Mutex<I2cMaster>>,
}

impl SharedI2cBus {
    pub fn new(base_addr: usize) -> Self {
        Self {
            master: Arc::new(Mutex::new(I2cMaster::new(base_addr))),
        }
    }
    
    pub fn clone(&self) -> Self {
        Self {
            master: Arc::clone(&self.master),
        }
    }
    
    // Try to acquire bus with timeout
    pub fn try_lock(&self, timeout: Duration) -> Option<MutexGuard<I2cMaster>> {
        let start = Instant::now();
        
        loop {
            match self.master.try_lock() {
                Ok(guard) => return Some(guard),
                Err(_) => {
                    if start.elapsed() > timeout {
                        return None;
                    }
                    thread::sleep(Duration::from_millis(10));
                }
            }
        }
    }
    
    pub fn write(&self, slave_addr: u8, data: &[u8]) -> I2cResult<()> {
        let timeout = Duration::from_millis(1000);
        
        let mut master = self.try_lock(timeout)
            .ok_or(I2cError::Timeout)?;
        
        master.write(slave_addr, data)
    }
}

// Usage example with multiple threads
fn multi_threaded_example() {
    let bus = SharedI2cBus::new(0x4000_0000);
    
    let bus1 = bus.clone();
    let handle1 = thread::spawn(move || {
        bus1.write(0x50, &[0x01, 0x02, 0x03]).unwrap();
    });
    
    let bus2 = bus.clone();
    let handle2 = thread::spawn(move || {
        bus2.write(0x51, &[0x04, 0x05, 0x06]).unwrap();
    });
    
    handle1.join().unwrap();
    handle2.join().unwrap();
}
```

### Bus Recovery in Rust

```rust
pub struct I2cGpio {
    scl_port: *mut u32,
    sda_port: *mut u32,
    scl_pin: u32,
    sda_pin: u32,
}

impl I2cGpio {
    const RECOVERY_CLOCKS: u32 = 9;
    
    pub fn bus_recovery(&mut self) -> bool {
        // Switch to GPIO mode and configure pins
        
        for _ in 0..Self::RECOVERY_CLOCKS {
            // Generate clock pulse
            self.set_scl_low();
            self.delay_us(5);
            self.set_scl_high();
            self.delay_us(5);
            
            // Check if SDA is released
            if self.read_sda() {
                // Generate STOP condition
                self.set_sda_low();
                self.delay_us(5);
                self.set_scl_high();
                self.delay_us(5);
                self.set_sda_high();
                self.delay_us(5);
                
                // Switch back to I2C mode
                return true;
            }
        }
        
        false // Recovery failed
    }
    
    fn set_scl_low(&mut self) {
        unsafe {
            *self.scl_port &= !(1 << self.scl_pin);
        }
    }
    
    fn set_scl_high(&mut self) {
        unsafe {
            *self.scl_port |= 1 << self.scl_pin;
        }
    }
    
    fn set_sda_low(&mut self) {
        unsafe {
            *self.sda_port &= !(1 << self.sda_pin);
        }
    }
    
    fn set_sda_high(&mut self) {
        unsafe {
            *self.sda_port |= 1 << self.sda_pin;
        }
    }
    
    fn read_sda(&self) -> bool {
        unsafe { (*self.sda_port & (1 << self.sda_pin)) != 0 }
    }
    
    fn delay_us(&self, us: u32) {
        thread::sleep(Duration::from_micros(us as u64));
    }
}
```

## Key Takeaways

1. **Always use timeouts** - Never wait indefinitely for bus conditions
2. **Implement exponential backoff** - After arbitration loss, wait progressively longer
3. **Add jitter to retries** - Prevents synchronized retry collisions
4. **Use mutex protection** - In multi-threaded environments, protect bus access
5. **Implement bus recovery** - Handle hardware lockup scenarios
6. **Limit retry attempts** - Prevent infinite retry loops
7. **Proper error handling** - Distinguish between recoverable and fatal errors

These implementations provide robust deadlock prevention suitable for production multi-master I2C systems.