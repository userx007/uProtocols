# Clock Polarity and Phase in SPI Communication

## Overview

Clock Polarity (CPOL) and Clock Phase (CPHA) are two fundamental configuration parameters that define the timing relationship between the clock signal and data transmission in SPI communication. Together, they create four distinct SPI modes that determine when data is sampled and when it transitions.

## Understanding CPOL and CPHA

### Clock Polarity (CPOL)

CPOL determines the idle state of the clock line when no data is being transmitted:

- **CPOL = 0**: Clock is LOW when idle (default state is 0)
- **CPOL = 1**: Clock is HIGH when idle (default state is 1)

### Clock Phase (CPHA)

CPHA determines when data is sampled relative to the clock edge:

- **CPHA = 0**: Data is sampled on the **first** (leading) clock edge and shifted on the **second** (trailing) edge
- **CPHA = 1**: Data is sampled on the **second** (trailing) clock edge and shifted on the **first** (leading) edge

## The Four SPI Modes

The combination of CPOL and CPHA creates four distinct modes:

| Mode | CPOL | CPHA | Clock Idle | Sample Edge | Shift Edge |
|------|------|------|------------|-------------|------------|
| 0    | 0    | 0    | LOW        | Rising      | Falling    |
| 1    | 0    | 1    | LOW        | Falling     | Rising     |
| 2    | 1    | 0    | HIGH       | Falling     | Rising     |
| 3    | 1    | 1    | HIGH       | Rising      | Falling    |

### Mode 0 (CPOL=0, CPHA=0)
- Clock idles LOW
- Data sampled on rising edge
- Data shifted out on falling edge
- Most commonly used mode

### Mode 1 (CPOL=0, CPHA=1)
- Clock idles LOW
- Data sampled on falling edge
- Data shifted out on rising edge

### Mode 2 (CPOL=1, CPHA=0)
- Clock idles HIGH
- Data sampled on falling edge
- Data shifted out on rising edge

### Mode 3 (CPOL=1, CPHA=1)
- Clock idles HIGH
- Data sampled on rising edge
- Data shifted out on falling edge

## Timing Relationships

The timing diagram shows the critical relationship between clock edges and data:

```
CPHA=0 (Sample on first edge):
        ___     ___     ___     ___
SCLK  _|   |___|   |___|   |___|   |___  (CPOL=0)
DATA  ===X===X===X===X===X===X===X===X===
         ^   ^   ^   ^   ^   ^   ^   ^
         |   |   |   |   |   |   |   |
      Sample  Sample  Sample  Sample

CPHA=1 (Sample on second edge):
        ___     ___     ___     ___
SCLK  _|   |___|   |___|   |___|   |___  (CPOL=0)
DATA  ===X===X===X===X===X===X===X===X===
             ^       ^       ^       ^
             |       |       |       |
          Sample  Sample  Sample  Sample
```

## C/C++ Implementation Examples

### Example 1: Basic SPI Mode Configuration (Bare Metal)

```c
#include <stdint.h>

// SPI register definitions (example for a typical microcontroller)
#define SPI_CR1     (*(volatile uint32_t*)0x40013000)
#define SPI_CR2     (*(volatile uint32_t*)0x40013004)
#define SPI_SR      (*(volatile uint32_t*)0x40013008)
#define SPI_DR      (*(volatile uint32_t*)0x4001300C)

// SPI Control Register 1 bits
#define SPI_CR1_CPHA    (1 << 0)  // Clock phase
#define SPI_CR1_CPOL    (1 << 1)  // Clock polarity
#define SPI_CR1_MSTR    (1 << 2)  // Master selection
#define SPI_CR1_SPE     (1 << 6)  // SPI enable
#define SPI_CR1_BR_DIV2 (0 << 3)  // Baud rate: fPCLK/2

typedef enum {
    SPI_MODE_0 = 0,  // CPOL=0, CPHA=0
    SPI_MODE_1 = 1,  // CPOL=0, CPHA=1
    SPI_MODE_2 = 2,  // CPOL=1, CPHA=0
    SPI_MODE_3 = 3   // CPOL=1, CPHA=1
} spi_mode_t;

void spi_init(spi_mode_t mode) {
    // Disable SPI before configuration
    SPI_CR1 &= ~SPI_CR1_SPE;
    
    // Clear CPOL and CPHA bits
    SPI_CR1 &= ~(SPI_CR1_CPOL | SPI_CR1_CPHA);
    
    // Set mode based on CPOL and CPHA
    switch(mode) {
        case SPI_MODE_0:
            // CPOL=0, CPHA=0 (already cleared)
            break;
        case SPI_MODE_1:
            SPI_CR1 |= SPI_CR1_CPHA;  // CPOL=0, CPHA=1
            break;
        case SPI_MODE_2:
            SPI_CR1 |= SPI_CR1_CPOL;  // CPOL=1, CPHA=0
            break;
        case SPI_MODE_3:
            SPI_CR1 |= (SPI_CR1_CPOL | SPI_CR1_CPHA);  // CPOL=1, CPHA=1
            break;
    }
    
    // Configure as master with appropriate baud rate
    SPI_CR1 |= SPI_CR1_MSTR | SPI_CR1_BR_DIV2;
    
    // Enable SPI
    SPI_CR1 |= SPI_CR1_SPE;
}

uint8_t spi_transfer(uint8_t data) {
    // Wait until transmit buffer is empty
    while (!(SPI_SR & (1 << 1)));
    
    // Write data to data register
    SPI_DR = data;
    
    // Wait until receive buffer has data
    while (!(SPI_SR & (1 << 0)));
    
    // Read and return received data
    return (uint8_t)SPI_DR;
}
```

### Example 2: Linux SPI Device with Mode Configuration

```c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

typedef struct {
    int fd;
    uint8_t mode;
    uint8_t bits_per_word;
    uint32_t speed_hz;
} spi_device_t;

int spi_open(spi_device_t *dev, const char *device, uint8_t mode, 
             uint32_t speed_hz) {
    int ret;
    
    // Open SPI device
    dev->fd = open(device, O_RDWR);
    if (dev->fd < 0) {
        perror("Failed to open SPI device");
        return -1;
    }
    
    // Set SPI mode (includes CPOL and CPHA)
    dev->mode = mode;
    ret = ioctl(dev->fd, SPI_IOC_WR_MODE, &dev->mode);
    if (ret < 0) {
        perror("Failed to set SPI mode");
        close(dev->fd);
        return -1;
    }
    
    // Set bits per word
    dev->bits_per_word = 8;
    ret = ioctl(dev->fd, SPI_IOC_WR_BITS_PER_WORD, &dev->bits_per_word);
    if (ret < 0) {
        perror("Failed to set bits per word");
        close(dev->fd);
        return -1;
    }
    
    // Set max speed
    dev->speed_hz = speed_hz;
    ret = ioctl(dev->fd, SPI_IOC_WR_MAX_SPEED_HZ, &dev->speed_hz);
    if (ret < 0) {
        perror("Failed to set max speed");
        close(dev->fd);
        return -1;
    }
    
    return 0;
}

int spi_transfer_data(spi_device_t *dev, uint8_t *tx_buf, uint8_t *rx_buf, 
                      size_t len) {
    struct spi_ioc_transfer transfer = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_buf,
        .len = len,
        .speed_hz = dev->speed_hz,
        .bits_per_word = dev->bits_per_word,
        .delay_usecs = 0,
    };
    
    int ret = ioctl(dev->fd, SPI_IOC_MESSAGE(1), &transfer);
    if (ret < 0) {
        perror("SPI transfer failed");
        return -1;
    }
    
    return ret;
}

void spi_close(spi_device_t *dev) {
    close(dev->fd);
}

// Example usage
int main() {
    spi_device_t spi_dev;
    uint8_t tx_data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t rx_data[4] = {0};
    
    // Open SPI device in Mode 0 (CPOL=0, CPHA=0)
    if (spi_open(&spi_dev, "/dev/spidev0.0", SPI_MODE_0, 1000000) < 0) {
        return -1;
    }
    
    printf("SPI Mode: %d\n", spi_dev.mode);
    printf("Transmitting: ");
    for (int i = 0; i < 4; i++) {
        printf("0x%02X ", tx_data[i]);
    }
    printf("\n");
    
    // Perform transfer
    if (spi_transfer_data(&spi_dev, tx_data, rx_data, 4) < 0) {
        spi_close(&spi_dev);
        return -1;
    }
    
    printf("Received: ");
    for (int i = 0; i < 4; i++) {
        printf("0x%02X ", rx_data[i]);
    }
    printf("\n");
    
    spi_close(&spi_dev);
    return 0;
}
```

### Example 3: C++ Class-Based SPI with Mode Selection

```cpp
#include <cstdint>
#include <stdexcept>
#include <vector>

class SPIDevice {
public:
    enum class Mode {
        MODE_0 = 0,  // CPOL=0, CPHA=0
        MODE_1 = 1,  // CPOL=0, CPHA=1
        MODE_2 = 2,  // CPOL=1, CPHA=0
        MODE_3 = 3   // CPOL=1, CPHA=1
    };
    
    enum class BitOrder {
        MSB_FIRST,
        LSB_FIRST
    };

private:
    Mode mode_;
    uint32_t clock_speed_;
    BitOrder bit_order_;
    bool initialized_;
    
    // Hardware register pointers (example)
    volatile uint32_t* control_reg_;
    volatile uint32_t* data_reg_;
    volatile uint32_t* status_reg_;

public:
    SPIDevice(Mode mode = Mode::MODE_0, uint32_t clock_speed = 1000000)
        : mode_(mode), 
          clock_speed_(clock_speed),
          bit_order_(BitOrder::MSB_FIRST),
          initialized_(false) {
    }
    
    void initialize() {
        if (initialized_) {
            throw std::runtime_error("SPI already initialized");
        }
        
        // Configure clock polarity and phase
        configureModeRegisters();
        
        // Set clock speed
        setClockSpeed(clock_speed_);
        
        initialized_ = true;
    }
    
    void setMode(Mode mode) {
        mode_ = mode;
        if (initialized_) {
            configureModeRegisters();
        }
    }
    
    Mode getMode() const {
        return mode_;
    }
    
    std::string getModeDescription() const {
        switch (mode_) {
            case Mode::MODE_0:
                return "Mode 0: CPOL=0, CPHA=0 (Sample: Rising, Shift: Falling)";
            case Mode::MODE_1:
                return "Mode 1: CPOL=0, CPHA=1 (Sample: Falling, Shift: Rising)";
            case Mode::MODE_2:
                return "Mode 2: CPOL=1, CPHA=0 (Sample: Falling, Shift: Rising)";
            case Mode::MODE_3:
                return "Mode 3: CPOL=1, CPHA=1 (Sample: Rising, Shift: Falling)";
            default:
                return "Unknown mode";
        }
    }
    
    uint8_t transfer(uint8_t data) {
        if (!initialized_) {
            throw std::runtime_error("SPI not initialized");
        }
        
        // Simulated transfer logic
        // In real hardware, this would interact with registers
        return data ^ 0xFF;  // Example: return inverted data
    }
    
    std::vector<uint8_t> transfer(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> received;
        received.reserve(data.size());
        
        for (uint8_t byte : data) {
            received.push_back(transfer(byte));
        }
        
        return received;
    }

private:
    void configureModeRegisters() {
        // This would configure actual hardware registers
        // Example pseudocode:
        uint32_t config = 0;
        
        // Set CPOL bit
        if (static_cast<int>(mode_) & 0x02) {
            config |= (1 << 1);  // CPOL = 1
        }
        
        // Set CPHA bit
        if (static_cast<int>(mode_) & 0x01) {
            config |= (1 << 0);  // CPHA = 1
        }
        
        // Write to control register
        // *control_reg_ = config;
    }
    
    void setClockSpeed(uint32_t speed) {
        clock_speed_ = speed;
        // Configure clock divider based on speed
    }
};

// Example usage
int main() {
    try {
        SPIDevice spi(SPIDevice::Mode::MODE_0, 1000000);
        spi.initialize();
        
        std::cout << spi.getModeDescription() << std::endl;
        
        // Transfer single byte
        uint8_t response = spi.transfer(0x42);
        std::cout << "Sent: 0x42, Received: 0x" 
                  << std::hex << static_cast<int>(response) << std::endl;
        
        // Transfer multiple bytes
        std::vector<uint8_t> tx_data = {0x01, 0x02, 0x03, 0x04};
        auto rx_data = spi.transfer(tx_data);
        
        std::cout << "Received " << rx_data.size() << " bytes" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Implementation Examples

### Example 1: Embedded Rust SPI with Mode Configuration

```rust
#![no_std]

// Typical embedded HAL traits
pub trait SpiMode {
    const CPOL: bool;
    const CPHA: bool;
}

pub struct Mode0;
impl SpiMode for Mode0 {
    const CPOL: bool = false;
    const CPHA: bool = false;
}

pub struct Mode1;
impl SpiMode for Mode1 {
    const CPOL: bool = false;
    const CPHA: bool = true;
}

pub struct Mode2;
impl SpiMode for Mode2 {
    const CPOL: bool = true;
    const CPHA: bool = false;
}

pub struct Mode3;
impl SpiMode for Mode3 {
    const CPOL: bool = true;
    const CPHA: bool = true;
}

pub struct SpiConfig<MODE: SpiMode> {
    clock_speed: u32,
    _mode: core::marker::PhantomData<MODE>,
}

impl<MODE: SpiMode> SpiConfig<MODE> {
    pub fn new(clock_speed: u32) -> Self {
        Self {
            clock_speed,
            _mode: core::marker::PhantomData,
        }
    }
    
    pub fn cpol(&self) -> bool {
        MODE::CPOL
    }
    
    pub fn cpha(&self) -> bool {
        MODE::CPHA
    }
    
    pub fn mode_description(&self) -> &'static str {
        match (MODE::CPOL, MODE::CPHA) {
            (false, false) => "Mode 0: CPOL=0, CPHA=0",
            (false, true) => "Mode 1: CPOL=0, CPHA=1",
            (true, false) => "Mode 2: CPOL=1, CPHA=0",
            (true, true) => "Mode 3: CPOL=1, CPHA=1",
        }
    }
}

// Hardware SPI peripheral abstraction
pub struct Spi<MODE: SpiMode> {
    config: SpiConfig<MODE>,
    // Register addresses would go here
}

impl<MODE: SpiMode> Spi<MODE> {
    pub fn new(config: SpiConfig<MODE>) -> Self {
        let mut spi = Self { config };
        spi.configure_hardware();
        spi
    }
    
    fn configure_hardware(&mut self) {
        // Pseudo-code for hardware configuration
        // let control_reg = unsafe { &mut *(0x4001_3000 as *mut u32) };
        
        let mut cr1: u32 = 0;
        
        if MODE::CPOL {
            cr1 |= 1 << 1; // Set CPOL bit
        }
        
        if MODE::CPHA {
            cr1 |= 1 << 0; // Set CPHA bit
        }
        
        cr1 |= 1 << 2; // Master mode
        cr1 |= 1 << 6; // Enable SPI
        
        // *control_reg = cr1;
    }
    
    pub fn transfer(&mut self, data: u8) -> u8 {
        // Pseudo-code for SPI transfer
        // let data_reg = unsafe { &mut *(0x4001_300C as *mut u32) };
        // let status_reg = unsafe { &*(0x4001_3008 as *const u32) };
        
        // Wait for TX buffer empty
        // while (*status_reg & (1 << 1)) == 0 {}
        
        // Write data
        // *data_reg = data as u32;
        
        // Wait for RX buffer full
        // while (*status_reg & (1 << 0)) == 0 {}
        
        // Read and return
        // (*data_reg & 0xFF) as u8
        
        data ^ 0xFF // Placeholder
    }
    
    pub fn transfer_multiple(&mut self, tx_data: &[u8], rx_data: &mut [u8]) {
        let len = core::cmp::min(tx_data.len(), rx_data.len());
        
        for i in 0..len {
            rx_data[i] = self.transfer(tx_data[i]);
        }
    }
}

// Example usage (in an embedded context)
pub fn example_usage() {
    // Create SPI in Mode 0
    let config_mode0 = SpiConfig::<Mode0>::new(1_000_000);
    let mut spi_mode0 = Spi::new(config_mode0);
    
    // Transfer single byte
    let response = spi_mode0.transfer(0x42);
    
    // Transfer multiple bytes
    let tx_buffer = [0x01, 0x02, 0x03, 0x04];
    let mut rx_buffer = [0u8; 4];
    spi_mode0.transfer_multiple(&tx_buffer, &mut rx_buffer);
    
    // Create SPI in Mode 3
    let config_mode3 = SpiConfig::<Mode3>::new(2_000_000);
    let mut spi_mode3 = Spi::new(config_mode3);
    let _ = spi_mode3.transfer(0xAA);
}
```

### Example 2: Rust Linux SPI with spidev

```rust
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Write};
use std::os::unix::io::AsRawFd;

// SPI mode constants (matching Linux kernel definitions)
const SPI_CPHA: u8 = 0x01;
const SPI_CPOL: u8 = 0x02;

const SPI_MODE_0: u8 = 0;
const SPI_MODE_1: u8 = SPI_CPHA;
const SPI_MODE_2: u8 = SPI_CPOL;
const SPI_MODE_3: u8 = SPI_CPOL | SPI_CPHA;

// ioctl constants
const SPI_IOC_MAGIC: u8 = b'k';
const SPI_IOC_WR_MODE: u8 = 1;
const SPI_IOC_WR_BITS_PER_WORD: u8 = 3;
const SPI_IOC_WR_MAX_SPEED_HZ: u8 = 4;

#[derive(Debug, Clone, Copy)]
pub enum SpiMode {
    Mode0 = SPI_MODE_0 as isize,
    Mode1 = SPI_MODE_1 as isize,
    Mode2 = SPI_MODE_2 as isize,
    Mode3 = SPI_MODE_3 as isize,
}

impl SpiMode {
    pub fn description(&self) -> &'static str {
        match self {
            SpiMode::Mode0 => "Mode 0: CPOL=0, CPHA=0 (Sample: Rising, Shift: Falling)",
            SpiMode::Mode1 => "Mode 1: CPOL=0, CPHA=1 (Sample: Falling, Shift: Rising)",
            SpiMode::Mode2 => "Mode 2: CPOL=1, CPHA=0 (Sample: Falling, Shift: Rising)",
            SpiMode::Mode3 => "Mode 3: CPOL=1, CPHA=1 (Sample: Rising, Shift: Falling)",
        }
    }
    
    pub fn cpol(&self) -> bool {
        (*self as u8 & SPI_CPOL) != 0
    }
    
    pub fn cpha(&self) -> bool {
        (*self as u8 & SPI_CPHA) != 0
    }
}

pub struct SpiDevice {
    file: File,
    mode: SpiMode,
    bits_per_word: u8,
    max_speed_hz: u32,
}

impl SpiDevice {
    pub fn new(device_path: &str, mode: SpiMode, speed_hz: u32) -> io::Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(device_path)?;
        
        let mut spi = SpiDevice {
            file,
            mode,
            bits_per_word: 8,
            max_speed_hz: speed_hz,
        };
        
        spi.configure()?;
        Ok(spi)
    }
    
    fn configure(&mut self) -> io::Result<()> {
        // Set SPI mode
        let mode_value = self.mode as u8;
        unsafe {
            let ret = libc::ioctl(
                self.file.as_raw_fd(),
                request_code!(WRITE, SPI_IOC_MAGIC, SPI_IOC_WR_MODE, 1),
                &mode_value,
            );
            if ret < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        // Set bits per word
        unsafe {
            let ret = libc::ioctl(
                self.file.as_raw_fd(),
                request_code!(WRITE, SPI_IOC_MAGIC, SPI_IOC_WR_BITS_PER_WORD, 1),
                &self.bits_per_word,
            );
            if ret < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        // Set max speed
        unsafe {
            let ret = libc::ioctl(
                self.file.as_raw_fd(),
                request_code!(WRITE, SPI_IOC_MAGIC, SPI_IOC_WR_MAX_SPEED_HZ, 4),
                &self.max_speed_hz,
            );
            if ret < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        
        Ok(())
    }
    
    pub fn transfer(&mut self, tx_data: &[u8]) -> io::Result<Vec<u8>> {
        let mut rx_data = vec![0u8; tx_data.len()];
        
        // Use write/read for simple transfers
        self.file.write_all(tx_data)?;
        self.file.read_exact(&mut rx_data)?;
        
        Ok(rx_data)
    }
    
    pub fn get_mode(&self) -> SpiMode {
        self.mode
    }
    
    pub fn set_mode(&mut self, mode: SpiMode) -> io::Result<()> {
        self.mode = mode;
        self.configure()
    }
}

// Helper macro for ioctl request codes
macro_rules! request_code {
    (WRITE, $magic:expr, $nr:expr, $size:expr) => {
        (2u32 << 30) | (($magic as u32) << 8) | ($nr as u32) | (($size as u32) << 16)
    };
}

// Example usage
fn main() -> io::Result<()> {
    let mut spi = SpiDevice::new("/dev/spidev0.0", SpiMode::Mode0, 1_000_000)?;
    
    println!("SPI Configuration:");
    println!("  {}", spi.get_mode().description());
    println!("  CPOL: {}", spi.get_mode().cpol());
    println!("  CPHA: {}", spi.get_mode().cpha());
    
    let tx_data = vec![0xAA, 0xBB, 0xCC, 0xDD];
    println!("\nTransmitting: {:02X?}", tx_data);
    
    let rx_data = spi.transfer(&tx_data)?;
    println!("Received: {:02X?}", rx_data);
    
    // Change to Mode 3
    println!("\nChanging to Mode 3...");
    spi.set_mode(SpiMode::Mode3)?;
    println!("  {}", spi.get_mode().description());
    
    Ok(())
}
```

### Example 3: High-Level Rust SPI with embedded-hal Traits

```rust
use embedded_hal::spi::{Mode, Phase, Polarity};

// Define SPI modes using embedded-hal traits
pub const MODE_0: Mode = Mode {
    polarity: Polarity::IdleLow,
    phase: Phase::CaptureOnFirstTransition,
};

pub const MODE_1: Mode = Mode {
    polarity: Polarity::IdleLow,
    phase: Phase::CaptureOnSecondTransition,
};

pub const MODE_2: Mode = Mode {
    polarity: Polarity::IdleHigh,
    phase: Phase::CaptureOnFirstTransition,
};

pub const MODE_3: Mode = Mode {
    polarity: Polarity::IdleHigh,
    phase: Phase::CaptureOnSecondTransition,
};

pub struct SpiConfig {
    pub mode: Mode,
    pub frequency: u32,
}

impl SpiConfig {
    pub fn mode_description(&self) -> String {
        let mode_num = match (self.mode.polarity, self.mode.phase) {
            (Polarity::IdleLow, Phase::CaptureOnFirstTransition) => 0,
            (Polarity::IdleLow, Phase::CaptureOnSecondTransition) => 1,
            (Polarity::IdleHigh, Phase::CaptureOnFirstTransition) => 2,
            (Polarity::IdleHigh, Phase::CaptureOnSecondTransition) => 3,
        };
        
        format!(
            "Mode {}: CPOL={}, CPHA={}",
            mode_num,
            match self.mode.polarity {
                Polarity::IdleLow => 0,
                Polarity::IdleHigh => 1,
            },
            match self.mode.phase {
                Phase::CaptureOnFirstTransition => 0,
                Phase::CaptureOnSecondTransition => 1,
            }
        )
    }
}

// Example of device-specific driver that requires specific SPI mode
pub struct AccelerometerLIS3DH<SPI> {
    spi: SPI,
}

impl<SPI> AccelerometerLIS3DH<SPI>
where
    SPI: embedded_hal::spi::SpiDevice,
{
    // LIS3DH requires SPI Mode 3 (CPOL=1, CPHA=1)
    pub fn new(spi: SPI) -> Self {
        Self { spi }
    }
    
    pub fn read_who_am_i(&mut self) -> Result<u8, SPI::Error> {
        let mut buffer = [0x0F | 0x80, 0x00]; // Read register 0x0F
        self.spi.transfer_in_place(&mut buffer)?;
        Ok(buffer[1])
    }
}

fn main() {
    // Configuration examples for different devices
    
    // SD Card typically uses Mode 0
    let sd_config = SpiConfig {
        mode: MODE_0,
        frequency: 25_000_000,
    };
    println!("SD Card: {}", sd_config.mode_description());
    
    // Some accelerometers use Mode 3
    let accel_config = SpiConfig {
        mode: MODE_3,
        frequency: 10_000_000,
    };
    println!("Accelerometer: {}", accel_config.