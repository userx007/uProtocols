# SPI Resource Locking

## Overview

Resource locking in SPI systems addresses the critical challenge of managing concurrent access to a shared SPI bus. Since SPI is a bus protocol where multiple peripheral devices share the same communication lines (MISO, MOSI, SCK), proper synchronization mechanisms are essential to prevent race conditions, data corruption, and bus conflicts when multiple threads, processes, or interrupt handlers attempt to communicate with SPI devices simultaneously.

## Core Concepts

### The Shared Resource Problem

SPI buses present unique challenges:
- **Shared bus lines**: Multiple devices connect to the same MISO, MOSI, and SCK lines
- **Chip Select (CS) management**: Each device requires exclusive CS assertion during transactions
- **Transaction atomicity**: Multi-byte transfers must complete without interruption
- **Timing sensitivity**: SPI communication requires precise timing that can be disrupted by context switches

### Synchronization Primitives

**Mutex (Mutual Exclusion)**
- Provides exclusive access to the SPI bus
- Ensures only one thread can execute SPI transactions at a time
- Supports priority inheritance to prevent priority inversion
- Typically used for thread-level synchronization

**Semaphore**
- Can limit access to a specific number of concurrent users
- Binary semaphores function similarly to mutexes
- Counting semaphores can manage resource pools
- Often used for both thread and interrupt context synchronization

## C/C++ Implementation

### Basic Mutex Protection

```c
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

// SPI bus structure with protection
typedef struct {
    int fd;                    // File descriptor for SPI device
    pthread_mutex_t lock;      // Mutex for bus protection
    uint32_t speed_hz;
    uint8_t bits_per_word;
} spi_bus_t;

// Initialize SPI bus with mutex
int spi_bus_init(spi_bus_t *bus, const char *device) {
    pthread_mutexattr_t attr;
    
    // Initialize mutex with priority inheritance
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    
    if (pthread_mutex_init(&bus->lock, &attr) != 0) {
        perror("Mutex initialization failed");
        pthread_mutexattr_destroy(&attr);
        return -1;
    }
    
    pthread_mutexattr_destroy(&attr);
    
    // Open SPI device (platform-specific)
    bus->fd = open(device, O_RDWR);
    if (bus->fd < 0) {
        pthread_mutex_destroy(&bus->lock);
        return -1;
    }
    
    bus->speed_hz = 1000000;  // 1 MHz default
    bus->bits_per_word = 8;
    
    return 0;
}

// Thread-safe SPI transfer
int spi_transfer_safe(spi_bus_t *bus, uint8_t *tx_buf, 
                      uint8_t *rx_buf, size_t len) {
    int ret;
    
    // Acquire lock
    pthread_mutex_lock(&bus->lock);
    
    // Perform SPI transfer (simplified)
    // In real implementation, use ioctl with SPI_IOC_MESSAGE
    for (size_t i = 0; i < len; i++) {
        // Simulate SPI transfer
        if (rx_buf) {
            rx_buf[i] = tx_buf ? tx_buf[i] : 0;
        }
    }
    ret = len;
    
    // Release lock
    pthread_mutex_unlock(&bus->lock);
    
    return ret;
}

// Device-specific transfer with CS control
int spi_device_transfer(spi_bus_t *bus, int cs_pin, 
                        uint8_t *tx_buf, uint8_t *rx_buf, size_t len) {
    int ret;
    
    pthread_mutex_lock(&bus->lock);
    
    // Assert chip select (active low)
    gpio_write(cs_pin, 0);
    
    // Perform transfer
    ret = spi_raw_transfer(bus->fd, tx_buf, rx_buf, len);
    
    // Deassert chip select
    gpio_write(cs_pin, 1);
    
    pthread_mutex_unlock(&bus->lock);
    
    return ret;
}

// Cleanup
void spi_bus_destroy(spi_bus_t *bus) {
    pthread_mutex_destroy(&bus->lock);
    if (bus->fd >= 0) {
        close(bus->fd);
    }
}
```

### Advanced: Timeout and Error Handling

```cpp
#include <chrono>
#include <mutex>
#include <system_error>

class SPIBus {
private:
    int fd_;
    mutable std::timed_mutex mutex_;
    uint32_t speed_hz_;
    
public:
    SPIBus(const std::string& device, uint32_t speed = 1000000)
        : speed_hz_(speed) {
        fd_ = open(device.c_str(), O_RDWR);
        if (fd_ < 0) {
            throw std::system_error(errno, std::system_category(),
                                   "Failed to open SPI device");
        }
    }
    
    ~SPIBus() {
        if (fd_ >= 0) close(fd_);
    }
    
    // Transfer with timeout
    bool transfer(const std::vector<uint8_t>& tx_data,
                  std::vector<uint8_t>& rx_data,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        
        // Try to acquire lock with timeout
        if (!mutex_.try_lock_for(timeout)) {
            return false;  // Timeout occurred
        }
        
        // RAII-style lock management
        std::lock_guard<std::timed_mutex> lock(mutex_, std::adopt_lock);
        
        rx_data.resize(tx_data.size());
        
        // Perform SPI transfer
        struct spi_ioc_transfer tr = {};
        tr.tx_buf = reinterpret_cast<uintptr_t>(tx_data.data());
        tr.rx_buf = reinterpret_cast<uintptr_t>(rx_data.data());
        tr.len = tx_data.size();
        tr.speed_hz = speed_hz_;
        tr.bits_per_word = 8;
        
        int ret = ioctl(fd_, SPI_IOC_MESSAGE(1), &tr);
        return ret >= 0;
    }
    
    // Scoped device access
    class DeviceGuard {
    private:
        SPIBus& bus_;
        int cs_pin_;
        bool locked_;
        
    public:
        DeviceGuard(SPIBus& bus, int cs_pin) 
            : bus_(bus), cs_pin_(cs_pin), locked_(false) {
            bus_.mutex_.lock();
            locked_ = true;
            gpio_write(cs_pin_, 0);  // Assert CS
        }
        
        ~DeviceGuard() {
            if (locked_) {
                gpio_write(cs_pin_, 1);  // Deassert CS
                bus_.mutex_.unlock();
            }
        }
        
        // Prevent copying
        DeviceGuard(const DeviceGuard&) = delete;
        DeviceGuard& operator=(const DeviceGuard&) = delete;
    };
};

// Usage example
void read_sensor(SPIBus& spi_bus, int sensor_cs) {
    SPIBus::DeviceGuard guard(spi_bus, sensor_cs);
    
    std::vector<uint8_t> cmd = {0x03, 0x00};  // Read command
    std::vector<uint8_t> response;
    
    // CS is automatically managed by DeviceGuard
    spi_bus.transfer(cmd, response);
    
    // Process response...
} // CS automatically deasserted when guard goes out of scope
```

## Rust Implementation

### Using std::sync::Mutex

```rust
use std::sync::{Arc, Mutex};
use std::time::Duration;
use std::thread;

// SPI bus with mutex protection
pub struct SpiBus {
    device_path: String,
    speed_hz: u32,
    // In real implementation, would hold file descriptor or device handle
    lock: Arc<Mutex<()>>,
}

impl SpiBus {
    pub fn new(device_path: &str, speed_hz: u32) -> Result<Self, std::io::Error> {
        Ok(SpiBus {
            device_path: device_path.to_string(),
            speed_hz,
            lock: Arc::new(Mutex::new(())),
        })
    }
    
    // Thread-safe SPI transfer
    pub fn transfer(&self, tx_buf: &[u8], rx_buf: &mut [u8]) -> Result<usize, String> {
        // Acquire lock - blocks until available
        let _guard = self.lock.lock()
            .map_err(|e| format!("Failed to acquire lock: {}", e))?;
        
        // Critical section: perform SPI transfer
        // The lock is automatically released when _guard goes out of scope
        self.raw_transfer(tx_buf, rx_buf)
    }
    
    // Try to transfer with timeout
    pub fn try_transfer(&self, tx_buf: &[u8], rx_buf: &mut [u8], 
                        timeout: Duration) -> Result<usize, String> {
        let start = std::time::Instant::now();
        
        // Polling approach for timeout (std Mutex doesn't have try_lock_for)
        loop {
            if let Ok(_guard) = self.lock.try_lock() {
                return self.raw_transfer(tx_buf, rx_buf);
            }
            
            if start.elapsed() > timeout {
                return Err("Transfer timeout".to_string());
            }
            
            thread::sleep(Duration::from_micros(100));
        }
    }
    
    fn raw_transfer(&self, tx_buf: &[u8], rx_buf: &mut [u8]) -> Result<usize, String> {
        // Simulate SPI transfer
        let len = tx_buf.len().min(rx_buf.len());
        rx_buf[..len].copy_from_slice(&tx_buf[..len]);
        Ok(len)
    }
    
    // Clone for sharing across threads
    pub fn clone_handle(&self) -> SpiBus {
        SpiBus {
            device_path: self.device_path.clone(),
            speed_hz: self.speed_hz,
            lock: Arc::clone(&self.lock),
        }
    }
}

// Device-specific wrapper with CS management
pub struct SpiDevice {
    bus: SpiBus,
    cs_pin: u32,
}

impl SpiDevice {
    pub fn new(bus: SpiBus, cs_pin: u32) -> Self {
        SpiDevice { bus, cs_pin }
    }
    
    pub fn transfer(&self, tx_buf: &[u8], rx_buf: &mut [u8]) -> Result<usize, String> {
        // Acquire bus lock
        let _guard = self.bus.lock.lock()
            .map_err(|e| format!("Lock failed: {}", e))?;
        
        // Assert chip select
        gpio_write(self.cs_pin, false)?;
        
        // Perform transfer
        let result = self.bus.raw_transfer(tx_buf, rx_buf);
        
        // Deassert chip select
        gpio_write(self.cs_pin, true)?;
        
        result
    }
}

// Helper function (placeholder)
fn gpio_write(pin: u32, value: bool) -> Result<(), String> {
    // Platform-specific GPIO implementation
    Ok(())
}
```

### Advanced: Parking Lot and RAII Guards

```rust
use parking_lot::{Mutex, MutexGuard};
use std::sync::Arc;
use std::time::Duration;

pub struct AdvancedSpiBus {
    fd: i32,
    speed_hz: u32,
    // parking_lot::Mutex is more efficient and supports timeouts
    lock: Arc<Mutex<BusState>>,
}

struct BusState {
    active_device: Option<u32>,
    transfer_count: u64,
}

impl AdvancedSpiBus {
    pub fn new(device_path: &str, speed_hz: u32) -> Result<Self, std::io::Error> {
        // Open device (simplified)
        let fd = 0; // Would be actual file descriptor
        
        Ok(AdvancedSpiBus {
            fd,
            speed_hz,
            lock: Arc::new(Mutex::new(BusState {
                active_device: None,
                transfer_count: 0,
            })),
        })
    }
    
    // Try to acquire bus with timeout
    pub fn try_lock(&self, timeout: Duration) -> Option<BusGuard> {
        self.lock.try_lock_for(timeout).map(|guard| {
            BusGuard {
                bus_fd: self.fd,
                speed_hz: self.speed_hz,
                _guard: guard,
            }
        })
    }
    
    // Acquire bus (blocking)
    pub fn lock(&self) -> BusGuard {
        let guard = self.lock.lock();
        BusGuard {
            bus_fd: self.fd,
            speed_hz: self.speed_hz,
            _guard: guard,
        }
    }
}

// RAII guard for bus access
pub struct BusGuard<'a> {
    bus_fd: i32,
    speed_hz: u32,
    _guard: MutexGuard<'a, BusState>,
}

impl<'a> BusGuard<'a> {
    pub fn transfer(&mut self, tx_buf: &[u8], rx_buf: &mut [u8]) -> Result<usize, String> {
        // Perform SPI transfer while holding the lock
        self._guard.transfer_count += 1;
        
        // Actual SPI transfer implementation
        let len = tx_buf.len().min(rx_buf.len());
        rx_buf[..len].copy_from_slice(&tx_buf[..len]);
        Ok(len)
    }
    
    pub fn transfer_with_cs(&mut self, cs_pin: u32, 
                            tx_buf: &[u8], rx_buf: &mut [u8]) 
                            -> Result<usize, String> {
        self._guard.active_device = Some(cs_pin);
        
        gpio_write(cs_pin, false)?;
        let result = self.transfer(tx_buf, rx_buf);
        gpio_write(cs_pin, true)?;
        
        self._guard.active_device = None;
        result
    }
}

// Usage example
fn concurrent_spi_access() {
    let bus = Arc::new(AdvancedSpiBus::new("/dev/spidev0.0", 1_000_000).unwrap());
    let mut handles = vec![];
    
    for i in 0..4 {
        let bus_clone = Arc::clone(&bus);
        let handle = thread::spawn(move || {
            // Try to acquire bus with timeout
            if let Some(mut guard) = bus_clone.try_lock(Duration::from_millis(100)) {
                let tx_data = vec![0xA0 + i, 0x00];
                let mut rx_data = vec![0u8; 2];
                
                match guard.transfer_with_cs(i as u32, &tx_data, &mut rx_data) {
                    Ok(len) => println!("Thread {} transferred {} bytes", i, len),
                    Err(e) => println!("Thread {} error: {}", i, e),
                }
            } else {
                println!("Thread {} timed out waiting for bus", i);
            }
        });
        handles.push(handle);
    }
    
    for handle in handles {
        handle.join().unwrap();
    }
}
```

## Summary

Resource locking for SPI buses is essential for reliable concurrent access in multi-threaded or multi-process embedded systems. Key takeaways:

**Critical Requirements:**
- **Mutual exclusion** prevents bus conflicts when multiple threads access SPI peripherals
- **Atomicity** ensures complete transaction integrity across multi-byte transfers
- **CS coordination** guarantees only one device is selected at a time

**Implementation Strategies:**
- **Mutexes** provide the primary mechanism for thread-level synchronization
- **RAII patterns** (C++ and Rust) ensure locks are properly released even during errors
- **Timeout mechanisms** prevent deadlocks in real-time systems
- **Priority inheritance** (where available) prevents priority inversion issues

**Best Practices:**
- Always lock at the transaction level, not individual byte transfers
- Use scoped guards to prevent lock leaks
- Implement timeout mechanisms for fault tolerance
- Consider using lightweight synchronization primitives (like parking_lot in Rust) for better performance
- Document lock ordering to prevent deadlocks when multiple resources are involved

**Trade-offs:**
- Locking adds overhead but ensures correctness
- Fine-grained locking improves concurrency but increases complexity
- Coarse-grained locking is simpler but may reduce throughput

Proper resource locking transforms SPI from a shared hardware resource into a safe, concurrent communication interface suitable for complex embedded applications.