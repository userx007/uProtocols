// Cargo.toml dependencies:
// [dependencies]
// i2cdev = "0.6"

use i2cdev::core::I2CDevice;
use i2cdev::linux::{LinuxI2CDevice, LinuxI2CError};
use std::thread;
use std::time::Duration;

const I2C_BUS: &str = "/dev/i2c-1";
const FIRST_ADDR: u16 = 0x03;
const LAST_ADDR: u16 = 0x77;

fn scan_address(bus: &str, addr: u16) -> Result<bool, LinuxI2CError> {
    // Create a device at this address
    let mut dev = LinuxI2CDevice::new(bus, addr)?;
    
    // Try to read a single byte
    // If the device exists, this will succeed or fail gracefully
    // If no device, we'll get an I/O error
    match dev.smbus_read_byte() {
        Ok(_) => Ok(true),
        Err(e) => {
            // Check if it's a "no such device" error
            match e {
                LinuxI2CError::Nix(_) => Ok(false),
                _ => Err(e),
            }
        }
    }
}

fn scan_i2c_bus(bus: &str) {
    println!("\nScanning I2C bus {}...\n", bus);
    println!("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
    
    let mut device_count = 0;
    let mut found_devices = Vec::new();
    
    for addr in 0..=0x7F {
        // Print row header
        if addr % 16 == 0 {
            print!("{:02x}: ", addr);
        }
        
        // Skip reserved addresses
        if addr < FIRST_ADDR || addr > LAST_ADDR {
            print!("   ");
        } else {
            match scan_address(bus, addr) {
                Ok(true) => {
                    print!("{:02x} ", addr);
                    device_count += 1;
                    found_devices.push(addr);
                }
                Ok(false) => {
                    print!("-- ");
                }
                Err(_) => {
                    print!("?? ");
                }
            }
        }
        
        // New line every 16 addresses
        if (addr + 1) % 16 == 0 {
            println!();
        }
    }
    
    println!();
    println!("Scan complete. Found {} device(s).", device_count);
    
    if !found_devices.is_empty() {
        println!("\nDevices found at addresses:");
        for addr in found_devices {
            println!("  0x{:02X} ({})", addr, addr);
            if let Some(name) = guess_device_name(addr) {
                println!("    Possibly: {}", name);
            }
        }
    }
}

fn guess_device_name(addr: u16) -> Option<&'static str> {
    match addr {
        0x27 | 0x3C | 0x3D => Some("OLED Display (SSD1306/SH1106)"),
        0x48..=0x4F => Some("ADC/Sensor (ADS1115/TMP102)"),
        0x50..=0x57 => Some("EEPROM (24C series)"),
        0x68 => Some("IMU/RTC (MPU6050/DS1307)"),
        0x76 | 0x77 => Some("Pressure Sensor (BMP280/BME280)"),
        _ => None,
    }
}

fn main() {
    println!("=== Rust I2C Bus Scanner ===");
    println!("Bus: {}", I2C_BUS);
    
    loop {
        scan_i2c_bus(I2C_BUS);
        
        println!("\nWaiting 5 seconds before next scan...");
        thread::sleep(Duration::from_secs(5));
    }
}

/*
 * Build: cargo build --release
 * Run: sudo ./target/release/i2c_scanner
 * 
 * Note: Requires root/sudo permissions to access I2C devices
 */