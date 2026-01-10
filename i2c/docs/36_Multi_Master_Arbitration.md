# Multi-Master Arbitration in I2C

## Overview

Multi-master arbitration is a crucial feature of the I2C protocol that allows multiple master devices to coexist on the same bus without conflicts. When two or more masters attempt to control the bus simultaneously, the I2C protocol uses a deterministic arbitration mechanism to decide which master wins control while ensuring no data corruption occurs.

## How I2C Arbitration Works

The I2C arbitration mechanism relies on the **wired-AND** nature of the bus, where both SDA (data) and SCL (clock) lines are open-drain. The key principles are:

1. **Clock Synchronization**: All masters synchronize their clocks during arbitration
2. **Data Line Monitoring**: Masters continuously monitor the SDA line while transmitting
3. **Loss Detection**: A master loses arbitration when it writes a HIGH (1) but reads a LOW (0)
4. **Graceful退出**: The losing master immediately stops driving the bus and becomes a slave or waits

### Arbitration Process

During address transmission and data transmission:
- Each master compares the bit it transmits with the actual bus state
- If a master transmits `1` (releases SDA) but another master transmits `0` (pulls SDA low), the `0` wins
- The master that transmitted `1` detects the conflict and loses arbitration
- The losing master immediately stops transmission and switches to slave mode or waits to retry

## C/C++ Implementation Examples

### Example 1: Basic Multi-Master Arbitration Detection (Bare Metal)

```c
#include <stdint.h>
#include <stdbool.h>

// Hardware register definitions (example for generic MCU)
#define I2C_BASE_ADDR 0x40005400
#define I2C_CR1   (*(volatile uint32_t*)(I2C_BASE_ADDR + 0x00))
#define I2C_SR1   (*(volatile uint32_t*)(I2C_BASE_ADDR + 0x14))
#define I2C_DR    (*(volatile uint32_t*)(I2C_BASE_ADDR + 0x10))

// Status register bits
#define I2C_SR1_ARLO  (1 << 9)  // Arbitration Lost
#define I2C_SR1_BUSY  (1 << 1)  // Bus Busy
#define I2C_SR1_SB    (1 << 0)  // Start Bit sent

// Control register bits
#define I2C_CR1_START (1 << 8)  // Generate START
#define I2C_CR1_STOP  (1 << 9)  // Generate STOP
#define I2C_CR1_PE    (1 << 0)  // Peripheral Enable

typedef enum {
    I2C_SUCCESS = 0,
    I2C_ERROR_ARBITRATION_LOST,
    I2C_ERROR_BUS_BUSY,
    I2C_ERROR_TIMEOUT
} i2c_status_t;

typedef struct {
    volatile bool arbitration_lost;
    volatile bool bus_busy;
    uint32_t retry_count;
    uint32_t max_retries;
} i2c_master_ctx_t;

// Initialize multi-master context
void i2c_multimaster_init(i2c_master_ctx_t* ctx, uint32_t max_retries) {
    ctx->arbitration_lost = false;
    ctx->bus_busy = false;
    ctx->retry_count = 0;
    ctx->max_retries = max_retries;
}

// Check for arbitration loss
bool i2c_check_arbitration_lost(void) {
    return (I2C_SR1 & I2C_SR1_ARLO) != 0;
}

// Clear arbitration lost flag
void i2c_clear_arbitration_lost(void) {
    I2C_SR1 &= ~I2C_SR1_ARLO;
}

// Wait for bus to become free
i2c_status_t i2c_wait_bus_free(uint32_t timeout_ms) {
    uint32_t start_time = get_tick_count(); // Platform-specific timer
    
    while (I2C_SR1 & I2C_SR1_BUSY) {
        if ((get_tick_count() - start_time) > timeout_ms) {
            return I2C_ERROR_TIMEOUT;
        }
    }
    return I2C_SUCCESS;
}

// Generate START condition with arbitration checking
i2c_status_t i2c_generate_start_with_arbitration(i2c_master_ctx_t* ctx) {
    // Wait for bus to be free
    i2c_status_t status = i2c_wait_bus_free(100);
    if (status != I2C_SUCCESS) {
        return status;
    }
    
    // Generate START condition
    I2C_CR1 |= I2C_CR1_START;
    
    // Wait for START bit to be sent
    uint32_t timeout = 1000;
    while (!(I2C_SR1 & I2C_SR1_SB) && timeout--) {
        // Check for arbitration loss during START generation
        if (i2c_check_arbitration_lost()) {
            i2c_clear_arbitration_lost();
            ctx->arbitration_lost = true;
            return I2C_ERROR_ARBITRATION_LOST;
        }
    }
    
    if (timeout == 0) {
        return I2C_ERROR_TIMEOUT;
    }
    
    return I2C_SUCCESS;
}

// Send address with arbitration detection
i2c_status_t i2c_send_address_with_arbitration(uint8_t address, bool read, 
                                               i2c_master_ctx_t* ctx) {
    uint8_t addr_byte = (address << 1) | (read ? 1 : 0);
    
    // Write address to data register
    I2C_DR = addr_byte;
    
    // Wait for address to be sent
    uint32_t timeout = 1000;
    while (timeout--) {
        // Check for arbitration loss
        if (i2c_check_arbitration_lost()) {
            i2c_clear_arbitration_lost();
            ctx->arbitration_lost = true;
            return I2C_ERROR_ARBITRATION_LOST;
        }
        
        // Check if address was sent successfully
        if (I2C_SR1 & (1 << 1)) { // ADDR bit set
            return I2C_SUCCESS;
        }
    }
    
    return I2C_ERROR_TIMEOUT;
}

// Multi-master write with automatic retry on arbitration loss
i2c_status_t i2c_multimaster_write(i2c_master_ctx_t* ctx, uint8_t slave_addr,
                                   const uint8_t* data, uint16_t len) {
    i2c_status_t status;
    
    ctx->retry_count = 0;
    
    while (ctx->retry_count < ctx->max_retries) {
        ctx->arbitration_lost = false;
        
        // Generate START
        status = i2c_generate_start_with_arbitration(ctx);
        if (status == I2C_ERROR_ARBITRATION_LOST) {
            ctx->retry_count++;
            delay_ms(1 + (ctx->retry_count * 2)); // Exponential backoff
            continue;
        } else if (status != I2C_SUCCESS) {
            return status;
        }
        
        // Send address
        status = i2c_send_address_with_arbitration(slave_addr, false, ctx);
        if (status == I2C_ERROR_ARBITRATION_LOST) {
            ctx->retry_count++;
            delay_ms(1 + (ctx->retry_count * 2));
            continue;
        } else if (status != I2C_SUCCESS) {
            I2C_CR1 |= I2C_CR1_STOP;
            return status;
        }
        
        // Send data bytes
        for (uint16_t i = 0; i < len; i++) {
            I2C_DR = data[i];
            
            timeout = 1000;
            while (timeout--) {
                if (i2c_check_arbitration_lost()) {
                    i2c_clear_arbitration_lost();
                    ctx->arbitration_lost = true;
                    break;
                }
                
                if (I2C_SR1 & (1 << 7)) { // TxE bit set
                    break;
                }
            }
            
            if (ctx->arbitration_lost) {
                ctx->retry_count++;
                delay_ms(1 + (ctx->retry_count * 2));
                break;
            }
        }
        
        // If no arbitration loss occurred, we're done
        if (!ctx->arbitration_lost) {
            I2C_CR1 |= I2C_CR1_STOP;
            return I2C_SUCCESS;
        }
    }
    
    return I2C_ERROR_ARBITRATION_LOST;
}
```

### Example 2: Linux I2C Multi-Master (Using i2c-dev)

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <string.h>

#define MAX_RETRIES 5
#define RETRY_DELAY_US 1000

// Multi-master write with arbitration handling
int i2c_multimaster_write(int fd, uint8_t slave_addr, 
                          const uint8_t* data, size_t len) {
    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data msgset;
    int retry_count = 0;
    int ret;
    
    msg.addr = slave_addr;
    msg.flags = 0; // Write operation
    msg.len = len;
    msg.buf = (uint8_t*)data;
    
    msgset.msgs = &msg;
    msgset.nmsgs = 1;
    
    while (retry_count < MAX_RETRIES) {
        ret = ioctl(fd, I2C_RDWR, &msgset);
        
        if (ret == 1) {
            // Success
            return 0;
        }
        
        // Check if error was due to arbitration loss
        if (errno == EAGAIN || errno == EBUSY) {
            retry_count++;
            printf("Arbitration lost, retry %d/%d\n", retry_count, MAX_RETRIES);
            
            // Exponential backoff
            usleep(RETRY_DELAY_US * (1 << retry_count));
            continue;
        }
        
        // Other error
        fprintf(stderr, "I2C write error: %s\n", strerror(errno));
        return -1;
    }
    
    fprintf(stderr, "I2C write failed after %d retries\n", MAX_RETRIES);
    return -1;
}

// Multi-master read with arbitration handling
int i2c_multimaster_read(int fd, uint8_t slave_addr, 
                         uint8_t* buffer, size_t len) {
    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data msgset;
    int retry_count = 0;
    int ret;
    
    msg.addr = slave_addr;
    msg.flags = I2C_M_RD; // Read operation
    msg.len = len;
    msg.buf = buffer;
    
    msgset.msgs = &msg;
    msgset.nmsgs = 1;
    
    while (retry_count < MAX_RETRIES) {
        ret = ioctl(fd, I2C_RDWR, &msgset);
        
        if (ret == 1) {
            return 0;
        }
        
        if (errno == EAGAIN || errno == EBUSY) {
            retry_count++;
            printf("Arbitration lost during read, retry %d/%d\n", 
                   retry_count, MAX_RETRIES);
            usleep(RETRY_DELAY_US * (1 << retry_count));
            continue;
        }
        
        fprintf(stderr, "I2C read error: %s\n", strerror(errno));
        return -1;
    }
    
    return -1;
}

// Example usage
int main(void) {
    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0) {
        perror("Failed to open I2C device");
        return 1;
    }
    
    uint8_t write_data[] = {0x10, 0x20, 0x30};
    uint8_t read_buffer[16];
    
    // Perform multi-master write
    if (i2c_multimaster_write(fd, 0x50, write_data, sizeof(write_data)) == 0) {
        printf("Write successful\n");
    }
    
    // Perform multi-master read
    if (i2c_multimaster_read(fd, 0x50, read_buffer, sizeof(read_buffer)) == 0) {
        printf("Read successful\n");
    }
    
    close(fd);
    return 0;
}
```

## Rust Implementation Examples

### Example 1: Embedded Rust with Arbitration Detection

```rust
use embedded_hal::blocking::i2c::{Write, Read};
use core::fmt;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2cError {
    ArbitrationLost,
    BusBusy,
    Timeout,
    Nack,
    Other,
}

impl fmt::Display for I2cError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            I2cError::ArbitrationLost => write!(f, "Arbitration lost"),
            I2cError::BusBusy => write!(f, "Bus busy"),
            I2cError::Timeout => write!(f, "Timeout"),
            I2cError::Nack => write!(f, "NACK received"),
            I2cError::Other => write!(f, "Other error"),
        }
    }
}

pub struct MultiMasterConfig {
    max_retries: u32,
    base_delay_us: u32,
}

impl Default for MultiMasterConfig {
    fn default() -> Self {
        Self {
            max_retries: 5,
            base_delay_us: 1000,
        }
    }
}

pub struct MultiMasterI2c<I2C> {
    i2c: I2C,
    config: MultiMasterConfig,
    retry_count: u32,
}

impl<I2C> MultiMasterI2c<I2C> {
    pub fn new(i2c: I2C, config: MultiMasterConfig) -> Self {
        Self {
            i2c,
            config,
            retry_count: 0,
        }
    }
    
    pub fn get_retry_count(&self) -> u32 {
        self.retry_count
    }
    
    pub fn reset_retry_count(&mut self) {
        self.retry_count = 0;
    }
    
    fn calculate_backoff_delay(&self, retry: u32) -> u32 {
        self.config.base_delay_us * (1 << retry.min(4))
    }
}

// Implementation for write operations with arbitration handling
impl<I2C, E> MultiMasterI2c<I2C>
where
    I2C: Write<Error = E>,
    E: Into<I2cError>,
{
    pub fn write_with_arbitration(
        &mut self,
        address: u8,
        bytes: &[u8],
    ) -> Result<(), I2cError> {
        self.retry_count = 0;
        
        while self.retry_count < self.config.max_retries {
            match self.i2c.write(address, bytes) {
                Ok(_) => {
                    return Ok(());
                }
                Err(e) => {
                    let error: I2cError = e.into();
                    
                    if error == I2cError::ArbitrationLost || error == I2cError::BusBusy {
                        self.retry_count += 1;
                        
                        // Log retry attempt (in embedded systems, use defmt or similar)
                        #[cfg(feature = "defmt")]
                        defmt::debug!(
                            "Arbitration lost, retry {}/{}",
                            self.retry_count,
                            self.config.max_retries
                        );
                        
                        // Exponential backoff
                        let delay = self.calculate_backoff_delay(self.retry_count);
                        delay_us(delay);
                        
                        continue;
                    }
                    
                    // Non-recoverable error
                    return Err(error);
                }
            }
        }
        
        Err(I2cError::ArbitrationLost)
    }
}

// Implementation for read operations with arbitration handling
impl<I2C, E> MultiMasterI2c<I2C>
where
    I2C: Read<Error = E>,
    E: Into<I2cError>,
{
    pub fn read_with_arbitration(
        &mut self,
        address: u8,
        buffer: &mut [u8],
    ) -> Result<(), I2cError> {
        self.retry_count = 0;
        
        while self.retry_count < self.config.max_retries {
            match self.i2c.read(address, buffer) {
                Ok(_) => {
                    return Ok(());
                }
                Err(e) => {
                    let error: I2cError = e.into();
                    
                    if error == I2cError::ArbitrationLost || error == I2cError::BusBusy {
                        self.retry_count += 1;
                        
                        let delay = self.calculate_backoff_delay(self.retry_count);
                        delay_us(delay);
                        
                        continue;
                    }
                    
                    return Err(error);
                }
            }
        }
        
        Err(I2cError::ArbitrationLost)
    }
}

// Platform-specific delay function (example)
fn delay_us(us: u32) {
    // Implementation depends on target platform
    // For Cortex-M, you might use cortex_m::asm::delay()
    // For std, you might use std::thread::sleep()
    #[cfg(feature = "std")]
    std::thread::sleep(std::time::Duration::from_micros(us as u64));
}

// Example usage with STM32 HAL (pseudo-code)
#[cfg(feature = "stm32-example")]
fn example_usage() {
    use stm32f4xx_hal::{i2c::I2c, pac, prelude::*};
    
    let dp = pac::Peripherals::take().unwrap();
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.freeze();
    
    let gpiob = dp.GPIOB.split();
    let scl = gpiob.pb8.into_alternate_open_drain();
    let sda = gpiob.pb9.into_alternate_open_drain();
    
    let i2c = I2c::new(dp.I2C1, (scl, sda), 100.kHz(), &clocks);
    
    let mut multi_master = MultiMasterI2c::new(
        i2c,
        MultiMasterConfig::default(),
    );
    
    let data = [0x10, 0x20, 0x30];
    match multi_master.write_with_arbitration(0x50, &data) {
        Ok(_) => {
            // Write successful
        }
        Err(e) => {
            // Handle error
        }
    }
}
```

### Example 2: Linux I2C Multi-Master in Rust

```rust
use std::fs::{File, OpenOptions};
use std::os::unix::io::AsRawFd;
use std::io::{self, Error, ErrorKind};
use std::time::Duration;
use std::thread;

// I2C ioctl commands
const I2C_RETRIES: u16 = 0x0701;
const I2C_TIMEOUT: u16 = 0x0702;
const I2C_SLAVE: u16 = 0x0703;
const I2C_RDWR: u16 = 0x0707;

// I2C message flags
const I2C_M_RD: u16 = 0x0001;

#[repr(C)]
struct I2cMsg {
    addr: u16,
    flags: u16,
    len: u16,
    buf: *mut u8,
}

#[repr(C)]
struct I2cRdwrIoctlData {
    msgs: *mut I2cMsg,
    nmsgs: u32,
}

pub struct MultiMasterI2c {
    file: File,
    max_retries: u32,
    base_delay_ms: u64,
}

impl MultiMasterI2c {
    pub fn new(device_path: &str, max_retries: u32, base_delay_ms: u64) 
        -> io::Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(device_path)?;
        
        Ok(Self {
            file,
            max_retries,
            base_delay_ms,
        })
    }
    
    fn calculate_backoff_delay(&self, retry: u32) -> Duration {
        let multiplier = 1 << retry.min(4);
        Duration::from_millis(self.base_delay_ms * multiplier as u64)
    }
    
    fn is_arbitration_error(&self, error: &io::Error) -> bool {
        if let Some(code) = error.raw_os_error() {
            // EAGAIN (11) or EBUSY (16) typically indicate arbitration loss
            code == 11 || code == 16
        } else {
            false
        }
    }
    
    pub fn write_with_arbitration(&self, addr: u8, data: &[u8]) 
        -> io::Result<()> {
        let mut retry_count = 0;
        
        while retry_count < self.max_retries {
            let mut msg = I2cMsg {
                addr: addr as u16,
                flags: 0,
                len: data.len() as u16,
                buf: data.as_ptr() as *mut u8,
            };
            
            let mut msgset = I2cRdwrIoctlData {
                msgs: &mut msg,
                nmsgs: 1,
            };
            
            let result = unsafe {
                libc::ioctl(
                    self.file.as_raw_fd(),
                    I2C_RDWR as libc::c_ulong,
                    &mut msgset,
                )
            };
            
            if result >= 0 {
                return Ok(());
            }
            
            let error = Error::last_os_error();
            
            if self.is_arbitration_error(&error) {
                retry_count += 1;
                println!(
                    "Arbitration lost on write, retry {}/{}",
                    retry_count, self.max_retries
                );
                
                let delay = self.calculate_backoff_delay(retry_count);
                thread::sleep(delay);
                continue;
            }
            
            return Err(error);
        }
        
        Err(Error::new(
            ErrorKind::TimedOut,
            format!("Failed after {} retries due to arbitration loss", 
                    self.max_retries),
        ))
    }
    
    pub fn read_with_arbitration(&self, addr: u8, buffer: &mut [u8]) 
        -> io::Result<()> {
        let mut retry_count = 0;
        
        while retry_count < self.max_retries {
            let mut msg = I2cMsg {
                addr: addr as u16,
                flags: I2C_M_RD,
                len: buffer.len() as u16,
                buf: buffer.as_mut_ptr(),
            };
            
            let mut msgset = I2cRdwrIoctlData {
                msgs: &mut msg,
                nmsgs: 1,
            };
            
            let result = unsafe {
                libc::ioctl(
                    self.file.as_raw_fd(),
                    I2C_RDWR as libc::c_ulong,
                    &mut msgset,
                )
            };
            
            if result >= 0 {
                return Ok(());
            }
            
            let error = Error::last_os_error();
            
            if self.is_arbitration_error(&error) {
                retry_count += 1;
                println!(
                    "Arbitration lost on read, retry {}/{}",
                    retry_count, self.max_retries
                );
                
                let delay = self.calculate_backoff_delay(retry_count);
                thread::sleep(delay);
                continue;
            }
            
            return Err(error);
        }
        
        Err(Error::new(
            ErrorKind::TimedOut,
            format!("Failed after {} retries due to arbitration loss", 
                    self.max_retries),
        ))
    }
    
    pub fn write_read_with_arbitration(
        &self,
        addr: u8,
        write_data: &[u8],
        read_buffer: &mut [u8],
    ) -> io::Result<()> {
        let mut retry_count = 0;
        
        while retry_count < self.max_retries {
            let mut msgs = [
                I2cMsg {
                    addr: addr as u16,
                    flags: 0,
                    len: write_data.len() as u16,
                    buf: write_data.as_ptr() as *mut u8,
                },
                I2cMsg {
                    addr: addr as u16,
                    flags: I2C_M_RD,
                    len: read_buffer.len() as u16,
                    buf: read_buffer.as_mut_ptr(),
                },
            ];
            
            let mut msgset = I2cRdwrIoctlData {
                msgs: msgs.as_mut_ptr(),
                nmsgs: 2,
            };
            
            let result = unsafe {
                libc::ioctl(
                    self.file.as_raw_fd(),
                    I2C_RDWR as libc::c_ulong,
                    &mut msgset,
                )
            };
            
            if result >= 0 {
                return Ok(());
            }
            
            let error = Error::last_os_error();
            
            if self.is_arbitration_error(&error) {
                retry_count += 1;
                println!(
                    "Arbitration lost on write-read, retry {}/{}",
                    retry_count, self.max_retries
                );
                
                let delay = self.calculate_backoff_delay(retry_count);
                thread::sleep(delay);
                continue;
            }
            
            return Err(error);
        }
        
        Err(Error::new(
            ErrorKind::TimedOut,
            "Failed after retries due to arbitration loss",
        ))
    }
}

// Example usage
fn main() -> io::Result<()> {
    let i2c = MultiMasterI2c::new("/dev/i2c-1", 5, 10)?;
    
    let write_data = [0x10, 0x20, 0x30];
    i2c.write_with_arbitration(0x50, &write_data)?;
    println!("Write successful");
    
    let mut read_buffer = [0u8; 16];
    i2c.read_with_arbitration(0x50, &mut read_buffer)?;
    println!("Read successful: {:?}", read_buffer);
    
    Ok(())
}
```

## Key Considerations for Multi-Master Systems

### 1. **Arbitration Strategy**
- **Immediate Retry**: May cause repeated collisions if both masters retry immediately
- **Random Backoff**: Reduces collision probability but adds unpredictability
- **Exponential Backoff**: Balances retry speed with collision avoidance
- **Priority-based**: Use different slave addresses to give natural priority

### 2. **Clock Stretching**
In multi-master systems, clock stretching can interact with arbitration:
- Slower masters can hold SCL low, forcing faster masters to wait
- All masters must support clock stretching properly

### 3. **Bus Recovery**
If arbitration fails repeatedly:
- Check for stuck bus conditions (SDA or SCL held low)
- Implement bus recovery procedures (send clock pulses to clear stuck slaves)
- Consider hardware reset mechanisms

### 4. **Testing Multi-Master Systems**
- Use logic analyzers to observe arbitration in real-time
- Create intentional collision scenarios during testing
- Verify proper behavior under high bus contention
- Test with different timing parameters and loads

### 5. **Design Guidelines**
- Keep retry counts reasonable (3-10 retries typically sufficient)
- Implement exponential backoff to reduce repeated collisions
- Monitor arbitration loss rates to identify system issues
- Consider using different I2C speeds for different masters if possible
- Document which devices are masters and their priority requirements

The multi-master capability makes I2C flexible for complex systems where multiple processors or controllers need to share a common bus, but proper arbitration handling is essential for reliable operation.