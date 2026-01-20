# Thread Safety in SPI Programming

## Overview

Thread safety in SPI (Serial Peripheral Interface) programming is critical when multiple threads or tasks attempt to access the same SPI bus simultaneously. Without proper protection mechanisms, concurrent access can lead to corrupted data, incomplete transactions, and unpredictable behavior.

## Why Thread Safety Matters in SPI

SPI communication involves multiple steps that must execute atomically:
1. Selecting the chip/slave device (CS pin assertion)
2. Transmitting/receiving data
3. Deselecting the device (CS pin deassertion)

If another thread interrupts this sequence, the SPI bus state becomes inconsistent, potentially sending data to the wrong device or mixing data from multiple transactions.

## Common Thread Safety Mechanisms

### 1. Mutexes (Mutual Exclusion Locks)
The most common approach for protecting SPI resources. A mutex ensures only one thread can access the SPI bus at a time.

### 2. Semaphores
Similar to mutexes but can allow counted access. Binary semaphores work like mutexes.

### 3. Critical Sections
Disable interrupts temporarily to create an atomic operation window (embedded systems).

### 4. RTOS Task Notifications
Lightweight signaling mechanisms in real-time operating systems.

## Code Examples

### C Example (FreeRTOS)

```c
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"

// SPI mutex handle
static SemaphoreHandle_t spi_mutex = NULL;

// Initialize SPI with thread safety
bool spi_init_threadsafe(void) {
    // Create mutex for SPI protection
    spi_mutex = xSemaphoreCreateMutex();
    if (spi_mutex == NULL) {
        return false;
    }
    
    // Initialize hardware SPI
    // (Hardware-specific initialization code here)
    
    return true;
}

// Thread-safe SPI transfer function
bool spi_transfer_threadsafe(uint8_t *tx_data, uint8_t *rx_data, 
                              size_t length, uint8_t cs_pin) {
    // Attempt to take mutex with timeout
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false; // Timeout - couldn't acquire lock
    }
    
    // Critical section - SPI bus is now protected
    gpio_set_low(cs_pin); // Assert chip select
    
    for (size_t i = 0; i < length; i++) {
        // Hardware-specific SPI transfer
        while (!(SPI_STATUS & SPI_TX_READY));
        SPI_DATA = tx_data[i];
        
        while (!(SPI_STATUS & SPI_RX_READY));
        rx_data[i] = SPI_DATA;
    }
    
    gpio_set_high(cs_pin); // Deassert chip select
    
    // Release mutex
    xSemaphoreGive(spi_mutex);
    
    return true;
}

// Example: Reading from sensor in multiple tasks
void sensor_task_1(void *params) {
    uint8_t tx_buf[4] = {0x80, 0x00, 0x00, 0x00}; // Read command
    uint8_t rx_buf[4];
    
    while (1) {
        if (spi_transfer_threadsafe(tx_buf, rx_buf, 4, SENSOR1_CS)) {
            // Process sensor data
            uint16_t value = (rx_buf[2] << 8) | rx_buf[3];
            // ... use value ...
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void sensor_task_2(void *params) {
    uint8_t tx_buf[4] = {0x80, 0x00, 0x00, 0x00};
    uint8_t rx_buf[4];
    
    while (1) {
        if (spi_transfer_threadsafe(tx_buf, rx_buf, 4, SENSOR2_CS)) {
            uint16_t value = (rx_buf[2] << 8) | rx_buf[3];
            // ... use value ...
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
```

### C++ Example (Modern C++ with std::mutex)

```cpp
#include <mutex>
#include <vector>
#include <cstdint>
#include <chrono>

class ThreadSafeSPI {
private:
    std::mutex spi_mutex_;
    int spi_fd_;  // File descriptor for Linux SPI device
    
public:
    ThreadSafeSPI(const char* device) {
        // Open SPI device (Linux example)
        spi_fd_ = open(device, O_RDWR);
        // Configure SPI mode, speed, etc.
    }
    
    ~ThreadSafeSPI() {
        if (spi_fd_ >= 0) {
            close(spi_fd_);
        }
    }
    
    // Thread-safe transfer with RAII lock guard
    bool transfer(const std::vector<uint8_t>& tx_data,
                  std::vector<uint8_t>& rx_data,
                  uint8_t cs_pin) {
        // Lock guard automatically acquires and releases mutex
        std::lock_guard<std::mutex> lock(spi_mutex_);
        
        rx_data.resize(tx_data.size());
        
        // Assert chip select
        gpio_write(cs_pin, 0);
        
        // Perform SPI transfer (Linux ioctl example)
        struct spi_ioc_transfer transfer = {};
        transfer.tx_buf = reinterpret_cast<uintptr_t>(tx_data.data());
        transfer.rx_buf = reinterpret_cast<uintptr_t>(rx_data.data());
        transfer.len = tx_data.size();
        transfer.speed_hz = 1000000;
        transfer.bits_per_word = 8;
        
        int ret = ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &transfer);
        
        // Deassert chip select
        gpio_write(cs_pin, 1);
        
        return ret >= 0;
    }
    
    // Try-lock version with timeout
    template<typename Rep, typename Period>
    bool transfer_with_timeout(
            const std::vector<uint8_t>& tx_data,
            std::vector<uint8_t>& rx_data,
            uint8_t cs_pin,
            const std::chrono::duration<Rep, Period>& timeout) {
        
        std::unique_lock<std::mutex> lock(spi_mutex_, timeout);
        
        if (!lock.owns_lock()) {
            return false; // Timeout occurred
        }
        
        rx_data.resize(tx_data.size());
        gpio_write(cs_pin, 0);
        
        // ... SPI transfer logic ...
        
        gpio_write(cs_pin, 1);
        return true;
    }
};

// Usage example
void worker_thread(ThreadSafeSPI& spi, uint8_t cs_pin) {
    std::vector<uint8_t> tx = {0x03, 0x00, 0x10, 0x00};
    std::vector<uint8_t> rx;
    
    while (true) {
        if (spi.transfer(tx, rx, cs_pin)) {
            // Process received data
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

### Rust Example (Using std::sync::Mutex)

```rust
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

// SPI device abstraction
struct SpiDevice {
    // In real implementation, this would be a file descriptor or HAL object
    device_fd: i32,
}

impl SpiDevice {
    fn new(device_path: &str) -> Self {
        // Open and configure SPI device
        SpiDevice { device_fd: 0 }
    }
    
    // Low-level transfer (not thread-safe by itself)
    fn raw_transfer(&mut self, tx_data: &[u8], rx_data: &mut [u8], cs_pin: u8) -> Result<(), String> {
        // Assert CS
        gpio_write(cs_pin, false)?;
        
        // Perform SPI transfer
        for i in 0..tx_data.len() {
            // Hardware-specific transfer logic
            rx_data[i] = self.spi_transfer_byte(tx_data[i])?;
        }
        
        // Deassert CS
        gpio_write(cs_pin, true)?;
        Ok(())
    }
    
    fn spi_transfer_byte(&mut self, byte: u8) -> Result<u8, String> {
        // Placeholder for actual hardware interaction
        Ok(byte)
    }
}

// Thread-safe wrapper using Mutex
struct ThreadSafeSpi {
    device: Arc<Mutex<SpiDevice>>,
}

impl ThreadSafeSpi {
    fn new(device_path: &str) -> Self {
        ThreadSafeSpi {
            device: Arc::new(Mutex::new(SpiDevice::new(device_path))),
        }
    }
    
    // Thread-safe transfer method
    fn transfer(&self, tx_data: &[u8], cs_pin: u8) -> Result<Vec<u8>, String> {
        // Lock the mutex - blocks until available
        let mut device = self.device.lock()
            .map_err(|e| format!("Mutex poisoned: {}", e))?;
        
        let mut rx_data = vec![0u8; tx_data.len()];
        device.raw_transfer(tx_data, &mut rx_data, cs_pin)?;
        
        Ok(rx_data)
        // Mutex automatically released when `device` goes out of scope
    }
    
    // Try-lock version with timeout
    fn transfer_with_timeout(
        &self,
        tx_data: &[u8],
        cs_pin: u8,
        timeout: Duration,
    ) -> Result<Vec<u8>, String> {
        use std::time::Instant;
        
        let start = Instant::now();
        
        // Try to acquire lock with timeout
        loop {
            match self.device.try_lock() {
                Ok(mut device) => {
                    let mut rx_data = vec![0u8; tx_data.len()];
                    device.raw_transfer(tx_data, &mut rx_data, cs_pin)?;
                    return Ok(rx_data);
                }
                Err(_) => {
                    if start.elapsed() >= timeout {
                        return Err("Timeout acquiring SPI lock".to_string());
                    }
                    thread::sleep(Duration::from_millis(1));
                }
            }
        }
    }
    
    // Clone method for sharing across threads
    fn clone_handle(&self) -> Self {
        ThreadSafeSpi {
            device: Arc::clone(&self.device),
        }
    }
}

// Helper functions (placeholder implementations)
fn gpio_write(pin: u8, state: bool) -> Result<(), String> {
    Ok(())
}

// Usage example with multiple threads
fn main() {
    let spi = ThreadSafeSpi::new("/dev/spidev0.0");
    
    // Spawn multiple threads accessing the same SPI bus
    let mut handles = vec![];
    
    for i in 0..3 {
        let spi_clone = spi.clone_handle();
        let cs_pin = 10 + i;
        
        let handle = thread::spawn(move || {
            loop {
                let tx_data = vec![0x80, 0x00, 0x00, 0x00];
                
                match spi_clone.transfer(&tx_data, cs_pin) {
                    Ok(rx_data) => {
                        let value = (rx_data[2] as u16) << 8 | rx_data[3] as u16;
                        println!("Thread {}: Read value {}", i, value);
                    }
                    Err(e) => eprintln!("Thread {}: Error - {}", i, e),
                }
                
                thread::sleep(Duration::from_millis(100 * (i as u64 + 1)));
            }
        });
        
        handles.push(handle);
    }
    
    // Wait for threads (in this example, they run forever)
    for handle in handles {
        handle.join().unwrap();
    }
}
```

## Best Practices

### 1. **Minimize Lock Duration**
Hold locks only during actual SPI transactions, not during data processing.

### 2. **Use Timeout Mechanisms**
Always use timeouts when acquiring locks to prevent deadlocks.

### 3. **Consistent Lock Ordering**
If using multiple locks, always acquire them in the same order to prevent deadlocks.

### 4. **Consider Priority Inversion**
In RTOS environments, use priority inheritance mutexes to prevent priority inversion.

### 5. **Avoid Nested Locking**
Don't call other functions that might lock the same mutex while holding a lock.

### 6. **Use RAII in C++/Rust**
Lock guards automatically release locks, preventing forgotten unlocks.

## Common Pitfalls

- **Forgetting to release locks** - Use RAII or ensure all code paths release
- **Interrupt context issues** - Don't use blocking mutexes in ISRs
- **Deadlocks** - Occur when threads wait for each other's locks
- **Race conditions on CS pins** - Ensure CS control is inside critical sections
- **Performance degradation** - Excessive locking reduces concurrency benefits

## Summary

Thread safety in SPI programming ensures reliable communication when multiple execution contexts share the same SPI bus. The key mechanisms include mutexes, semaphores, and critical sections. Modern implementations leverage RAII patterns (C++/Rust) to guarantee lock release, while embedded systems often use RTOS primitives like FreeRTOS semaphores. Proper thread safety requires protecting the entire transaction sequence (CS assertion, data transfer, CS deassertion) as an atomic operation, implementing timeouts to prevent deadlocks, and minimizing lock duration for optimal performance. Whether using C with FreeRTOS, C++ with std::mutex, or Rust's type-safe concurrency primitives, the fundamental principle remains: serialize access to shared SPI resources while maintaining system responsiveness and preventing data corruption.