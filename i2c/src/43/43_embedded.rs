// Example for STM32/ESP32/RP2040 using embedded-hal
// Cargo.toml dependencies (adjust for your platform):
// [dependencies]
// embedded-hal = "0.2"
// cortex-m = "0.7"
// cortex-m-rt = "0.7"
// panic-halt = "0.2"
// # Add your specific HAL crate here (e.g., stm32f4xx-hal, rp2040-hal)

#![no_std]
#![no_main]

use core::fmt::Write;
use embedded_hal::blocking::i2c::{Write as I2CWrite, WriteRead};
use panic_halt as _;

// These would be imported from your specific HAL
// use stm32f4xx_hal::{prelude::*, i2c::I2c, serial::Serial};
// use rp2040_hal::{i2c::I2C, uart::UartPeripheral};

const FIRST_ADDR: u8 = 0x03;
const LAST_ADDR: u8 = 0x77;

/// Scan the I2C bus for devices
pub fn scan_i2c_bus<I2C, UART>(i2c: &mut I2C, uart: &mut UART) 
where
    I2C: I2CWrite + WriteRead,
    UART: Write,
{
    writeln!(uart, "\r\nScanning I2C bus...").ok();
    writeln!(uart, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f").ok();
    
    let mut device_count = 0;
    
    for addr in 0..=0x7F {
        // Print row header
        if addr % 16 == 0 {
            write!(uart, "{:02x}: ", addr).ok();
        }
        
        // Skip reserved addresses
        if addr < FIRST_ADDR || addr > LAST_ADDR {
            write!(uart, "   ").ok();
        } else {
            // Attempt to write 0 bytes to check for ACK
            let result = i2c.write(addr, &[]);
            
            match result {
                Ok(_) => {
                    write!(uart, "{:02x} ", addr).ok();
                    device_count += 1;
                }
                Err(_) => {
                    write!(uart, "-- ").ok();
                }
            }
        }
        
        // New line every 16 addresses
        if (addr + 1) % 16 == 0 {
            writeln!(uart, "").ok();
        }
    }
    
    writeln!(uart, "\r\nScan complete. Found {} device(s).", device_count).ok();
}

/// Alternative: Scan with detailed error reporting
pub fn scan_i2c_bus_detailed<I2C, UART>(i2c: &mut I2C, uart: &mut UART) 
where
    I2C: WriteRead,
    UART: Write,
{
    writeln!(uart, "\r\n=== Detailed I2C Scan ===").ok();
    
    let mut found_devices = heapless::Vec::<u8, 128>::new();
    
    for addr in FIRST_ADDR..=LAST_ADDR {
        // Try to read one byte from address 0x00
        let mut buffer = [0u8; 1];
        let result = i2c.write_read(addr, &[0x00], &mut buffer);
        
        if result.is_ok() {
            writeln!(uart, "Device found at 0x{:02X}", addr).ok();
            found_devices.push(addr).ok();
        }
    }
    
    writeln!(uart, "\r\nTotal devices found: {}", found_devices.len()).ok();
    
    // Print common device identifications
    for &addr in found_devices.iter() {
        if let Some(name) = identify_device(addr) {
            writeln!(uart, "  0x{:02X}: {}", addr, name).ok();
        }
    }
}

/// Identify common I2C devices by address
fn identify_device(addr: u8) -> Option<&'static str> {
    match addr {
        0x1E => Some("HMC5883L Magnetometer"),
        0x27 => Some("LCD Display with I2C backpack"),
        0x3C | 0x3D => Some("OLED Display (SSD1306)"),
        0x40 => Some("Si7021 Temp/Humidity or INA219"),
        0x48..=0x4B => Some("ADS1115 ADC"),
        0x4C..=0x4F => Some("TMP102 Temperature Sensor"),
        0x50..=0x57 => Some("EEPROM (24C series)"),
        0x68 => Some("MPU6050/9250 IMU or DS1307 RTC"),
        0x69 => Some("MPU6050/9250 IMU (alt address)"),
        0x76 | 0x77 => Some("BMP280/BME280 Pressure Sensor"),
        _ => None,
    }
}

/// Read WHO_AM_I or chip ID register for verification
pub fn verify_device<I2C>(i2c: &mut I2C, addr: u8, reg: u8) -> Result<u8, ()>
where
    I2C: WriteRead,
{
    let mut buffer = [0u8; 1];
    i2c.write_read(addr, &[reg], &mut buffer)
        .map(|_| buffer[0])
        .map_err(|_| ())
}

/*
 * Usage example in main():
 * 
 * #[entry]
 * fn main() -> ! {
 *     let dp = pac::Peripherals::take().unwrap();
 *     let cp = cortex_m::Peripherals::take().unwrap();
 *     
 *     // Initialize clocks, GPIO, I2C, and UART...
 *     // let mut i2c = I2c::new(...);
 *     // let mut uart = Serial::new(...);
 *     
 *     loop {
 *         scan_i2c_bus(&mut i2c, &mut uart);
 *         delay.delay_ms(5000u32);
 *     }
 * }
 */