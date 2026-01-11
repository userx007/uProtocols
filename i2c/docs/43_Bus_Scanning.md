# I2C Bus Scanning: Comprehensive Guide

I2C bus scanning is a fundamental diagnostic technique for discovering which devices are connected to an I2C bus. Since I2C devices have 7-bit or 10-bit addresses, you can systematically probe each possible address to see which ones respond with an acknowledgment (ACK).

## How Bus Scanning Works

The I2C protocol requires devices to acknowledge their presence when addressed. During a bus scan:

1. The master sends a START condition
2. The master transmits an address with the read/write bit
3. If a device exists at that address, it sends an ACK (pulls SDA low)
4. If no device exists, the line stays high (NACK)
5. The master sends a STOP condition

By iterating through all valid addresses (typically 0x03 to 0x77 for 7-bit addressing, excluding reserved addresses), you can build a map of connected devices.

## Reserved I2C Addresses

Certain addresses are reserved by the I2C specification:
- **0x00-0x07**: Reserved addresses (including general call)
- **0x78-0x7F**: Reserved for 10-bit addressing and special purposes

## C/C++ Implementation Examples

### Example 1: Linux I2C Bus Scanner (using i2c-dev)

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <string.h>

#define I2C_BUS "/dev/i2c-1"  // Change based on your system
#define FIRST_ADDR 0x03       // First valid address
#define LAST_ADDR 0x77        // Last valid address

int main() {
    int file;
    int addr;
    int found_count = 0;
    
    // Open I2C bus
    file = open(I2C_BUS, O_RDWR);
    if (file < 0) {
        perror("Failed to open I2C bus");
        return 1;
    }
    
    printf("Scanning I2C bus %s...\n\n", I2C_BUS);
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    printf("00:          ");
    
    for (addr = 0; addr <= 0x7F; addr++) {
        // Print row header
        if (addr % 16 == 0 && addr != 0) {
            printf("\n%02x: ", addr);
        }
        
        // Skip reserved addresses
        if (addr < FIRST_ADDR || addr > LAST_ADDR) {
            printf("   ");
            continue;
        }
        
        // Try to communicate with device at this address
        if (ioctl(file, I2C_SLAVE, addr) < 0) {
            printf("-- ");
            continue;
        }
        
        // Attempt a read operation to check for ACK
        // Some devices may not like this, but it's the standard approach
        uint8_t buffer;
        int result = read(file, &buffer, 1);
        
        if (result >= 0 || errno == ENXIO) {
            // ENXIO means the address was NACKed (no device)
            if (errno == ENXIO) {
                printf("-- ");
            } else {
                // Device responded!
                printf("%02x ", addr);
                found_count++;
            }
        } else {
            printf("-- ");
        }
        
        errno = 0;  // Reset errno for next iteration
    }
    
    printf("\n\n");
    printf("Scan complete. Found %d device(s).\n", found_count);
    
    close(file);
    return 0;
}

/*
 * Compile: gcc -o i2c_scanner i2c_scanner.c
 * Run: sudo ./i2c_scanner
 * 
 * Note: Requires root/sudo permissions to access I2C devices
 */
```

### Example 2: Arduino-Style I2C Scanner (C++)

```c
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for serial port to connect (Leonardo/Micro)
  
  Wire.begin();
  
  Serial.println("\nI2C Bus Scanner");
  Serial.println("===============");
}

void loop() {
  byte error, address;
  int deviceCount = 0;
  
  Serial.println("\nScanning I2C bus...");
  Serial.println("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
  
  for (address = 0; address <= 0x7F; address++) {
    // Print row header
    if (address % 16 == 0) {
      Serial.print(address < 0x10 ? "0" : "");
      Serial.print(address, HEX);
      Serial.print(": ");
    }
    
    // Skip reserved addresses
    if (address < 0x03 || address > 0x77) {
      Serial.print("   ");
    } else {
      // Attempt to communicate with device
      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      
      if (error == 0) {
        // Device found
        Serial.print(address < 0x10 ? " 0" : " ");
        Serial.print(address, HEX);
        deviceCount++;
      } else if (error == 4) {
        // Unknown error
        Serial.print(" ??");
      } else {
        // No device at this address
        Serial.print(" --");
      }
    }
    
    // New line every 16 addresses
    if ((address + 1) % 16 == 0) {
      Serial.println();
    }
  }
  
  Serial.println();
  Serial.print("Scan complete. Found ");
  Serial.print(deviceCount);
  Serial.println(" device(s).");
  
  if (deviceCount > 0) {
    Serial.println("\nCommon I2C device addresses:");
    Serial.println("  0x27, 0x3C, 0x3D: OLED displays");
    Serial.println("  0x48-0x4F: ADS1115, TMP102");
    Serial.println("  0x50-0x57: EEPROM");
    Serial.println("  0x68: MPU6050, DS1307 RTC");
    Serial.println("  0x76, 0x77: BMP280, BME280");
  }
  
  // Wait 5 seconds before next scan
  delay(5000);
}

/*
 * Upload to Arduino board
 * Open Serial Monitor at 115200 baud
 * 
 * Wire.endTransmission() error codes:
 *   0: Success
 *   1: Data too long for transmit buffer
 *   2: NACK on address transmission
 *   3: NACK on data transmission
 *   4: Other error
 */
```

### Example 3: Raspberry Pi Pico (C/C++ with Pico SDK)

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// I2C configuration
#define I2C_PORT i2c0
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5
#define I2C_FREQ 100000  // 100 kHz

void scan_i2c_bus() {
    printf("\nScanning I2C bus...\n");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
    
    int device_count = 0;
    
    for (int addr = 0; addr <= 0x7F; addr++) {
        // Print row header
        if (addr % 16 == 0) {
            printf("%02x: ", addr);
        }
        
        // Skip reserved addresses
        if (addr < 0x03 || addr > 0x77) {
            printf("   ");
        } else {
            // Try to write 0 bytes to the address
            // If device exists, it will ACK
            uint8_t dummy_data = 0;
            int ret = i2c_write_timeout_us(I2C_PORT, addr, &dummy_data, 0, false, 1000);
            
            if (ret >= 0) {
                printf("%02x ", addr);
                device_count++;
            } else {
                printf("-- ");
            }
        }
        
        // New line every 16 addresses
        if ((addr + 1) % 16 == 0) {
            printf("\n");
        }
    }
    
    printf("\nScan complete. Found %d device(s).\n", device_count);
}

void print_device_info(uint8_t addr) {
    printf("\nDevice at 0x%02X:\n", addr);
    
    // Try to read identification if possible
    // This is device-specific; here's a generic approach
    uint8_t reg = 0x00;
    uint8_t data[4];
    
    int ret = i2c_write_blocking(I2C_PORT, addr, &reg, 1, true);
    if (ret > 0) {
        ret = i2c_read_blocking(I2C_PORT, addr, data, 4, false);
        if (ret > 0) {
            printf("  First 4 bytes: 0x%02X 0x%02X 0x%02X 0x%02X\n", 
                   data[0], data[1], data[2], data[3]);
        }
    }
}

int main() {
    // Initialize stdio for USB output
    stdio_init_all();
    
    // Wait for USB serial connection
    sleep_ms(2000);
    
    printf("\n=== Raspberry Pi Pico I2C Scanner ===\n");
    
    // Initialize I2C
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    
    printf("I2C initialized on pins SDA=%d, SCL=%d at %d Hz\n", 
           I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ);
    
    while (true) {
        scan_i2c_bus();
        
        printf("\nPress any key to scan again...\n");
        getchar();
    }
    
    return 0;
}

/*
 * CMakeLists.txt additions:
 * 
 * target_link_libraries(i2c_scanner
 *     pico_stdlib
 *     hardware_i2c
 * )
 * 
 * pico_enable_stdio_usb(i2c_scanner 1)
 * pico_enable_stdio_uart(i2c_scanner 0)
 */
```

## Rust Implementation Examples

### Example 4: Linux I2C Scanner (Rust with i2cdev crate)

```rs
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
```

### Example 5: Embedded Rust Scanner (embedded-hal)

```rs
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
```

## Advanced Techniques

### 1. **Speed Scanning**
For faster scanning, you can use different approaches:

```c
// Fast scan using write operations only
for (addr = 0x03; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    error = Wire.endTransmission(true); // Send STOP
    if (error == 0) {
        // Device found
    }
}
```

### 2. **10-bit Address Scanning**
For devices using 10-bit addressing:

```c
// 10-bit addresses range from 0x000 to 0x3FF
for (uint16_t addr = 0; addr <= 0x3FF; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
        printf("10-bit device at 0x%03X\n", addr);
    }
}
```

### 3. **Non-blocking Scan (Rust async)**

```rust
async fn scan_i2c_async<I2C>(i2c: &mut I2C) -> Vec<u8>
where
    I2C: AsyncWrite,
{
    let mut devices = Vec::new();
    
    for addr in 0x03..=0x77 {
        if i2c.write(addr, &[]).await.is_ok() {
            devices.push(addr);
        }
    }
    
    devices
}
```

## Best Practices

1. **Pull-up Resistors**: Ensure proper pull-up resistors (typically 4.7kΩ) are connected to SDA and SCL lines

2. **Bus Speed**: Start scanning at standard speed (100 kHz) before trying fast mode (400 kHz)

3. **Error Handling**: Some sensitive devices may not respond well to scanning. Consider:
   - Skipping known problematic addresses
   - Using gentler probing methods (read vs write)
   - Adding delays between scans

4. **Power Considerations**: Ensure all devices are properly powered before scanning

5. **Multiple Buses**: If your system has multiple I2C buses, scan each separately

## Common Pitfalls

- **Clock stretching**: Some devices use clock stretching which may timeout during scanning
- **Write-only devices**: Devices that only support write operations won't respond to read probes
- **Sleep mode**: Devices in low-power mode may not respond until awakened
- **Multi-master conflicts**: Scanning while another master is active can cause bus collisions

## Debugging Output Example

A typical scan output looks like:
```
Scanning I2C bus...
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- 27 -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- 3C -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- -- -- -- -- -- 57 -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- 77

Found 5 devices: 0x27, 0x3C, 0x57, 0x68, 0x77
```

This shows devices at addresses 0x27 (LCD), 0x3C (OLED), 0x57 (EEPROM), 0x68 (IMU/RTC), and 0x77 (pressure sensor).