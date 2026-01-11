/*
 * SPI Signal Lines - Rust Implementation
 * Demonstrates type-safe, zero-cost abstractions for SPI control
 * Uses embedded-hal traits for hardware abstraction
 */

#![no_std]

use core::ptr::{read_volatile, write_volatile};

/// SPI Mode - Clock Polarity and Phase
/// 
/// Mode 0 (CPOL=0, CPHA=0):
///   - Clock idle state: LOW
///   - SCK samples on rising edge, shifts on falling edge
///   - Most common mode
/// 
/// Mode 1 (CPOL=0, CPHA=1):
///   - Clock idle state: LOW
///   - SCK shifts on rising edge, samples on falling edge
/// 
/// Mode 2 (CPOL=1, CPHA=0):
///   - Clock idle state: HIGH
///   - SCK samples on falling edge, shifts on rising edge
/// 
/// Mode 3 (CPOL=1, CPHA=1):
///   - Clock idle state: HIGH
///   - SCK shifts on falling edge, samples on rising edge
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SPIMode {
    Mode0 = 0,  // CPOL=0, CPHA=0
    Mode1 = 1,  // CPOL=0, CPHA=1
    Mode2 = 2,  // CPOL=1, CPHA=0
    Mode3 = 3,  // CPOL=1, CPHA=1
}

impl SPIMode {
    /// Get clock polarity (CPOL)
    /// Controls the idle state of SCK line
    pub fn clock_polarity(&self) -> bool {
        (*self as u8 & 0b10) != 0
    }
    
    /// Get clock phase (CPHA)
    /// Controls when data is sampled on SCK edge
    pub fn clock_phase(&self) -> bool {
        (*self as u8 & 0b01) != 0
    }
}

/// Bit order for data transmission on MOSI/MISO
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum BitOrder {
    MsbFirst,  // Standard: bit 7 first
    LsbFirst,  // Uncommon: bit 0 first
}

/// SPI Clock frequency
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ClockSpeed {
    Khz125 = 125_000,
    Khz250 = 250_000,
    Khz500 = 500_000,
    Mhz1   = 1_000_000,
    Mhz2   = 2_000_000,
    Mhz4   = 4_000_000,
    Mhz8   = 8_000_000,
    Mhz10  = 10_000_000,
}

/// SPI Configuration
#[derive(Debug, Clone, Copy)]
pub struct SPIConfig {
    pub mode: SPIMode,
    pub clock_speed: ClockSpeed,
    pub bit_order: BitOrder,
    
    // Signal timing parameters (nanoseconds)
    pub cs_setup_time_ns: u32,   // Time from CS↓ to first SCK edge
    pub cs_hold_time_ns: u32,    // Time from last SCK edge to CS↑
    pub inter_byte_delay_ns: u32, // Delay between bytes
}

impl Default for SPIConfig {
    fn default() -> Self {
        Self {
            mode: SPIMode::Mode0,
            clock_speed: ClockSpeed::Mhz1,
            bit_order: BitOrder::MsbFirst,
            cs_setup_time_ns: 100,
            cs_hold_time_ns: 100,
            inter_byte_delay_ns: 0,
        }
    }
}

/// Hardware register abstraction
#[repr(C)]
struct SPIRegisters {
    cr1: u32,   // Control register 1 - configures CPOL, CPHA, clock
    cr2: u32,   // Control register 2
    sr: u32,    // Status register - TXE, RXNE, BSY flags
    dr: u32,    // Data register - interfaces with MOSI/MISO
}

/// SPI Status Register flags
mod sr_flags {
    pub const RXNE: u32 = 1 << 0;  // Receive buffer Not Empty (MISO has data)
    pub const TXE: u32  = 1 << 1;  // Transmit buffer Empty (ready for MOSI)
    pub const BSY: u32  = 1 << 7;  // Busy (SCK actively clocking)
}

/// SPI Control Register 1 flags
mod cr1_flags {
    pub const CPHA: u32     = 1 << 0;  // Clock phase
    pub const CPOL: u32     = 1 << 1;  // Clock polarity
    pub const MSTR: u32     = 1 << 2;  // Master mode
    pub const BR_SHIFT: u32 = 3;       // Baud rate control shift
    pub const SPE: u32      = 1 << 6;  // SPI enable
    pub const LSBFIRST: u32 = 1 << 7;  // Frame format
    pub const SSI: u32      = 1 << 8;  // Internal slave select
    pub const SSM: u32      = 1 << 9;  // Software slave management
}

/// GPIO Pin abstraction for CS line control
pub struct CSPin {
    port: *mut u32,
    pin: u8,
}

impl CSPin {
    pub fn new(port: *mut u32, pin: u8) -> Self {
        Self { port, pin }
    }
    
    /// Assert CS line (active LOW)
    /// Begins SPI transaction by pulling CS↓
    #[inline]
    pub fn select(&mut self) {
        unsafe {
            // Set pin LOW using BSRR register (Bit Set Reset Register)
            let bsrr = self.port.offset(4);
            write_volatile(bsrr, 1 << (self.pin + 16));
        }
    }
    
    /// Deassert CS line (inactive HIGH)
    /// Ends SPI transaction by pulling CS↑
    #[inline]
    pub fn deselect(&mut self) {
        unsafe {
            let bsrr = self.port.offset(4);
            write_volatile(bsrr, 1 << self.pin);
        }
    }
}

/// SPI Master Controller
/// Manages all four signal lines: MOSI, MISO, SCK, CS
pub struct SPIMaster {
    regs: *mut SPIRegisters,
    cs_pin: CSPin,
    config: SPIConfig,
}

impl SPIMaster {
    /// Create new SPI master instance
    pub fn new(base_addr: usize, cs_pin: CSPin, config: SPIConfig) -> Self {
        let mut spi = Self {
            regs: base_addr as *mut SPIRegisters,
            cs_pin,
            config,
        };
        spi.init();
        spi
    }
    
    /// Initialize SPI hardware
    /// Configures signal line behavior and timing
    fn init(&mut self) {
        let mut cr1_val: u32 = 0;
        
        // Configure as master (generates SCK)
        cr1_val |= cr1_flags::MSTR;
        
        // Set clock polarity and phase (controls SCK behavior)
        if self.config.mode.clock_phase() {
            cr1_val |= cr1_flags::CPHA;
        }
        if self.config.mode.clock_polarity() {
            cr1_val |= cr1_flags::CPOL;
        }
        
        // Set bit order (controls MOSI/MISO bit sequence)
        if self.config.bit_order == BitOrder::LsbFirst {
            cr1_val |= cr1_flags::LSBFIRST;
        }
        
        // Calculate baud rate divider for SCK frequency
        let sys_clock = 16_000_000u32; // Example: 16 MHz
        let target_freq = self.config.clock_speed as u32;
        let mut divider = 0u32;
        let mut test_freq = sys_clock;
        
        while test_freq > target_freq && divider < 7 {
            test_freq >>= 1;
            divider += 1;
        }
        cr1_val |= (divider & 0x7) << cr1_flags::BR_SHIFT;
        
        // Software slave management (manual CS control)
        cr1_val |= cr1_flags::SSM | cr1_flags::SSI;
        
        unsafe {
            write_volatile(&mut (*self.regs).cr1, cr1_val);
            
            // Enable SPI peripheral
            let mut cr1 = read_volatile(&(*self.regs).cr1);
            cr1 |= cr1_flags::SPE;
            write_volatile(&mut (*self.regs).cr1, cr1);
        }
        
        // Initialize CS high (inactive)
        self.cs_pin.deselect();
    }
    
    /// Wait for transmit buffer empty
    /// Ensures MOSI line is ready for next byte
    #[inline]
    fn wait_tx_empty(&self) {
        unsafe {
            while (read_volatile(&(*self.regs).sr) & sr_flags::TXE) == 0 {}
        }
    }
    
    /// Wait for receive buffer not empty
    /// Ensures MISO data has been received
    #[inline]
    fn wait_rx_ready(&self) {
        unsafe {
            while (read_volatile(&(*self.regs).sr) & sr_flags::RXNE) == 0 {}
        }
    }
    
    /// Wait for SPI not busy
    /// Ensures SCK has finished clocking
    #[inline]
    fn wait_not_busy(&self) {
        unsafe {
            while (read_volatile(&(*self.regs).sr) & sr_flags::BSY) != 0 {}
        }
    }
    
    /// Transfer single byte (full-duplex)
    /// 
    /// Signal line activity:
    /// 1. Write to DR → MOSI starts outputting bits
    /// 2. Master generates 8 SCK pulses
    /// 3. Each SCK edge: MOSI shifts out, MISO shifts in
    /// 4. After 8 clocks: received byte available in DR
    pub fn transfer_byte(&mut self, tx_byte: u8) -> u8 {
        self.wait_tx_empty();
        
        // Write triggers MOSI output and SCK generation
        unsafe {
            write_volatile(&mut (*self.regs).dr, tx_byte as u32);
        }
        
        self.wait_rx_ready();
        
        // Read data received on MISO
        unsafe {
            read_volatile(&(*self.regs).dr) as u8
        }
    }
    
    /// Transfer multiple bytes with automatic CS management
    /// 
    /// CS line: ‾‾‾\________________.../‾‾‾
    /// SCK line: ___/‾\__/‾\__ ... __/‾\___
    /// MOSI/MISO: continuous data transfer
    pub fn transfer(&mut self, tx_data: &[u8], rx_data: &mut [u8]) -> Result<(), &'static str> {
        if tx_data.len() != rx_data.len() {
            return Err("Buffer length mismatch");
        }
        
        // Assert CS line (LOW)
        self.cs_pin.select();
        self.delay_ns(self.config.cs_setup_time_ns);
        
        // Transfer all bytes
        // SCK generates continuous clock
        // MOSI and MISO transfer data on each clock edge
        for (i, &tx_byte) in tx_data.iter().enumerate() {
            rx_data[i] = self.transfer_byte(tx_byte);
            
            if self.config.inter_byte_delay_ns > 0 {
                self.delay_ns(self.config.inter_byte_delay_ns);
            }
        }
        
        // Ensure last byte completes before deasserting CS
        self.wait_not_busy();
        self.delay_ns(self.config.cs_hold_time_ns);
        
        // Deassert CS line (HIGH)
        self.cs_pin.deselect();
        
        Ok(())
    }
    
    /// Write-only transfer
    /// MISO data is ignored, MOSI sends data
    pub fn write(&mut self, data: &[u8]) -> Result<(), &'static str> {
        let mut dummy = [0u8; 256];
        let len = data.len().min(256);
        self.transfer(&data[..len], &mut dummy[..len])
    }
    
    /// Read-only transfer
    /// MOSI sends dummy bytes (0xFF) to generate SCK clock
    /// MISO receives actual data
    pub fn read(&mut self, buffer: &mut [u8]) -> Result<(), &'static str> {
        let dummy_tx = [0xFFu8; 256];
        let len = buffer.len().min(256);
        self.transfer(&dummy_tx[..len], buffer)
    }
    
    /// Execute transaction with automatic CS control
    pub fn transaction<F, R>(&mut self, f: F) -> R
    where
        F: FnOnce(&mut Self) -> R,
    {
        self.cs_pin.select();
        self.delay_ns(self.config.cs_setup_time_ns);
        
        let result = f(self);
        
        self.wait_not_busy();
        self.delay_ns(self.config.cs_hold_time_ns);
        self.cs_pin.deselect();
        
        result
    }
    
    /// Nanosecond delay (platform-specific implementation)
    #[inline]
    fn delay_ns(&self, ns: u32) {
        // Busy-wait implementation (adjust for actual CPU frequency)
        let cycles = (ns as u64 * 16) / 1000; // Assuming 16 MHz
        for _ in 0..cycles {
            core::hint::spin_loop();
        }
    }
}

/// Example: SPI Flash Memory Device Driver
/// Demonstrates real-world signal line usage patterns
pub struct SPIFlash<'a> {
    spi: &'a mut SPIMaster,
}

impl<'a> SPIFlash<'a> {
    // Flash command codes (transmitted on MOSI)
    const CMD_WRITE_ENABLE: u8  = 0x06;
    const CMD_READ_DATA: u8     = 0x03;
    const CMD_PAGE_PROGRAM: u8  = 0x02;
    const CMD_READ_STATUS: u8   = 0x05;
    const CMD_READ_ID: u8       = 0x9F;
    const CMD_CHIP_ERASE: u8    = 0xC7;
    
    pub fn new(spi: &'a mut SPIMaster) -> Self {
        Self { spi }
    }
    
    /// Read manufacturer and device ID
    /// 
    /// Signal sequence:
    /// CS↓ → [CMD on MOSI] → [3 bytes on MISO] → CS↑
    pub fn read_id(&mut self) -> Result<[u8; 3], &'static str> {
        self.spi.transaction(|spi| {
            // Send command on MOSI
            spi.transfer_byte(Self::CMD_READ_ID);
            
            // Read response from MISO (send dummy bytes for clock)
            let manufacturer = spi.transfer_byte(0xFF);
            let device_id_high = spi.transfer_byte(0xFF);
            let device_id_low = spi.transfer_byte(0xFF);
            
            Ok([manufacturer, device_id_high, device_id_low])
        })
    }
    
    /// Read data from flash memory
    /// 
    /// Signal sequence:
    /// CS↓ → [CMD+ADDR on MOSI] → [DATA on MISO] → CS↑
    pub fn read(&mut self, address: u32, buffer: &mut [u8]) -> Result<(), &'static str> {
        if buffer.is_empty() {
            return Ok(());
        }
        
        self.spi.transaction(|spi| {
            // Command phase - MOSI active
            spi.transfer_byte(Self::CMD_READ_DATA);
            
            // Address phase - MOSI active (24-bit address)
            spi.transfer_byte((address >> 16) as u8);
            spi.transfer_byte((address >> 8) as u8);
            spi.transfer_byte(address as u8);
            
            // Data phase - MISO active
            // Send dummy bytes on MOSI to generate SCK clock
            for byte in buffer.iter_mut() {
                *byte = spi.transfer_byte(0xFF);
            }
            
            Ok(())
        })
    }
    
    /// Write data to flash (requires write enable first)
    /// 
    /// Signal sequence:
    /// CS↓ → [WRITE_ENABLE on MOSI] → CS↑
    /// CS↓ → [PAGE_PROGRAM+ADDR+DATA on MOSI] → CS↑
    pub fn write(&mut self, address: u32, data: &[u8]) -> Result<(), &'static str> {
        if data.is_empty() || data.len() > 256 {
            return Err("Invalid data length");
        }
        
        // Enable write - separate transaction
        self.spi.transaction(|spi| {
            spi.transfer_byte(Self::CMD_WRITE_ENABLE);
        });
        
        // Program page - all data sent on MOSI
        self.spi.transaction(|spi| {
            spi.transfer_byte(Self::CMD_PAGE_PROGRAM);
            spi.transfer_byte((address >> 16) as u8);
            spi.transfer_byte((address >> 8) as u8);
            spi.transfer_byte(address as u8);
            
            for &byte in data {
                spi.transfer_byte(byte);
            }
            
            Ok(())
        })
    }
    
    /// Read status register
    /// Shows continuous MISO reading pattern
    pub fn read_status(&mut self) -> Result<u8, &'static str> {
        self.spi.transaction(|spi| {
            spi.transfer_byte(Self::CMD_READ_STATUS);
            Ok(spi.transfer_byte(0xFF))
        })
    }
    
    /// Wait for write/erase operation to complete
    /// Polls status register via MISO
    pub fn wait_ready(&mut self) -> Result<(), &'static str> {
        loop {
            let status = self.read_status()?;
            if (status & 0x01) == 0 {  // WIP bit
                break;
            }
            // Small delay between polls
            for _ in 0..1000 {
                core::hint::spin_loop();
            }
        }
        Ok(())
    }
}

/// Example usage demonstrating signal line interactions
#[allow(dead_code)]
fn example_usage() {
    // Configure SPI with specific timing
    let config = SPIConfig {
        mode: SPIMode::Mode0,           // CPOL=0, CPHA=0
        clock_speed: ClockSpeed::Mhz1,  // 1 MHz SCK
        bit_order: BitOrder::MsbFirst,
        cs_setup_time_ns: 100,
        cs_hold_time_ns: 100,
        inter_byte_delay_ns: 0,
    };
    
    // Create CS pin controller
    let cs_pin = CSPin::new(0x4001_0800 as *mut u32, 4);
    
    // Create SPI master
    let mut spi = SPIMaster::new(0x4001_3000, cs_pin, config);
    
    // Create flash device driver
    let mut flash = SPIFlash::new(&mut spi);
    
    // Read device ID
    if let Ok(id) = flash.read_id() {
        // Signal activity: CS↓ → CMD → 3 bytes → CS↑
        // MOSI sends command, MISO returns ID bytes
    }
    
    // Read 256 bytes from address 0x1000
    let mut buffer = [0u8; 256];
    if let Ok(()) = flash.read(0x1000, &mut buffer) {
        // Signal activity: CS↓ → CMD+ADDR → 256 bytes → CS↑
        // MOSI sends command+address, MISO returns data
    }
    
    // Write data
    let write_data = [0xAA, 0xBB, 0xCC, 0xDD];
    if let Ok(()) = flash.write(0x2000, &write_data) {
        // Signal activity:
        // Transaction 1: CS↓ → WRITE_ENABLE → CS↑
        // Transaction 2: CS↓ → PAGE_PROGRAM+ADDR+DATA → CS↑
        // All data sent on MOSI
    }
}