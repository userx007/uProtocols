# I2C Device ID Reading

## Overview

Device ID reading is a fundamental I2C operation that allows a master device to identify connected slaves by reading manufacturer codes, device IDs, and revision information from standardized registers. This capability is crucial for:

- **Auto-detection**: Discovering and identifying devices on the I2C bus
- **Driver initialization**: Ensuring the correct driver is loaded for a device
- **Version checking**: Verifying firmware/hardware revisions
- **Diagnostics**: Confirming device presence and correct operation

## Common ID Register Schemes

Different manufacturers use various approaches for device identification:

### 1. **Single WHO_AM_I Register**
Many sensors (especially IMUs, accelerometers, magnetometers) use a single read-only register containing a fixed device ID.

**Example**: MPU6050 has WHO_AM_I at register 0x75 returning 0x68

### 2. **Multiple ID Registers**
Some devices use separate registers for manufacturer ID, device ID, and revision.

**Example**: 
- Manufacturer ID: Register 0xFE
- Device ID: Register 0xFF
- Revision: Register 0x01

### 3. **JEDEC Standard (JEP106)**
High-end devices may implement the JEDEC JEP106 standard for manufacturer identification codes.

## C/C++ Implementation

### Basic Device ID Reading

```c
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// Device-specific constants
#define MPU6050_ADDR        0x68
#define MPU6050_WHO_AM_I    0x75
#define MPU6050_EXPECTED_ID 0x68

#define BME280_ADDR         0x76
#define BME280_CHIP_ID_REG  0xD0
#define BME280_EXPECTED_ID  0x60

/**
 * Read a single byte from an I2C device register
 */
int i2c_read_byte(int file, uint8_t reg_addr, uint8_t *data) {
    // Write register address
    if (write(file, &reg_addr, 1) != 1) {
        perror("Failed to write register address");
        return -1;
    }
    
    // Read data
    if (read(file, data, 1) != 1) {
        perror("Failed to read data");
        return -1;
    }
    
    return 0;
}

/**
 * Read device ID and verify it matches expected value
 */
int verify_device_id(int file, uint8_t id_register, uint8_t expected_id) {
    uint8_t device_id;
    
    if (i2c_read_byte(file, id_register, &device_id) < 0) {
        return -1;
    }
    
    printf("Device ID: 0x%02X (Expected: 0x%02X)\n", device_id, expected_id);
    
    if (device_id == expected_id) {
        printf("Device verification successful!\n");
        return 0;
    } else {
        printf("Device verification failed!\n");
        return -1;
    }
}

/**
 * Example: MPU6050 identification
 */
int identify_mpu6050(const char *i2c_device) {
    int file;
    
    // Open I2C bus
    file = open(i2c_device, O_RDWR);
    if (file < 0) {
        perror("Failed to open I2C bus");
        return -1;
    }
    
    // Set slave address
    if (ioctl(file, I2C_SLAVE, MPU6050_ADDR) < 0) {
        perror("Failed to set I2C slave address");
        close(file);
        return -1;
    }
    
    // Verify device
    int result = verify_device_id(file, MPU6050_WHO_AM_I, MPU6050_EXPECTED_ID);
    
    close(file);
    return result;
}
```

### Multi-Register Device Information

```c
#include <string.h>

typedef struct {
    uint8_t manufacturer_id;
    uint8_t device_id;
    uint8_t revision;
    char manufacturer_name[32];
    char device_name[32];
} device_info_t;

/**
 * Read multiple device identification registers
 */
int read_device_info(int file, device_info_t *info) {
    // Clear the structure
    memset(info, 0, sizeof(device_info_t));
    
    // Read manufacturer ID (example register 0xFE)
    if (i2c_read_byte(file, 0xFE, &info->manufacturer_id) < 0) {
        return -1;
    }
    
    // Read device ID (example register 0xFF)
    if (i2c_read_byte(file, 0xFF, &info->device_id) < 0) {
        return -1;
    }
    
    // Read revision (example register 0x01)
    if (i2c_read_byte(file, 0x01, &info->revision) < 0) {
        return -1;
    }
    
    // Decode manufacturer (example mapping)
    switch (info->manufacturer_id) {
        case 0x01:
            strncpy(info->manufacturer_name, "Bosch", sizeof(info->manufacturer_name));
            break;
        case 0x02:
            strncpy(info->manufacturer_name, "STMicroelectronics", sizeof(info->manufacturer_name));
            break;
        default:
            snprintf(info->manufacturer_name, sizeof(info->manufacturer_name), 
                     "Unknown (0x%02X)", info->manufacturer_id);
    }
    
    // Decode device
    switch (info->device_id) {
        case 0x60:
            strncpy(info->device_name, "BME280", sizeof(info->device_name));
            break;
        case 0x68:
            strncpy(info->device_name, "MPU6050", sizeof(info->device_name));
            break;
        default:
            snprintf(info->device_name, sizeof(info->device_name), 
                     "Unknown (0x%02X)", info->device_id);
    }
    
    return 0;
}

void print_device_info(const device_info_t *info) {
    printf("=== Device Information ===\n");
    printf("Manufacturer: %s (ID: 0x%02X)\n", 
           info->manufacturer_name, info->manufacturer_id);
    printf("Device: %s (ID: 0x%02X)\n", 
           info->device_name, info->device_id);
    printf("Revision: 0x%02X\n", info->revision);
    printf("========================\n");
}
```

### Bus Scanning with ID Verification

```c
#define I2C_ADDR_MIN 0x08
#define I2C_ADDR_MAX 0x77

/**
 * Scan I2C bus and attempt to read device IDs
 */
void scan_and_identify_devices(const char *i2c_device) {
    int file;
    uint8_t addr;
    
    file = open(i2c_device, O_RDWR);
    if (file < 0) {
        perror("Failed to open I2C bus");
        return;
    }
    
    printf("Scanning I2C bus for devices...\n\n");
    
    for (addr = I2C_ADDR_MIN; addr <= I2C_ADDR_MAX; addr++) {
        if (ioctl(file, I2C_SLAVE, addr) < 0) {
            continue; // Address not available
        }
        
        // Try to read a byte (simple presence test)
        uint8_t test_byte;
        if (read(file, &test_byte, 1) == 1) {
            printf("Device found at address 0x%02X\n", addr);
            
            // Try common WHO_AM_I register addresses
            uint8_t who_am_i_registers[] = {0x75, 0xD0, 0x0F, 0xFF};
            
            for (int i = 0; i < sizeof(who_am_i_registers); i++) {
                uint8_t id;
                if (i2c_read_byte(file, who_am_i_registers[i], &id) == 0) {
                    printf("  Register 0x%02X: 0x%02X\n", 
                           who_am_i_registers[i], id);
                }
            }
            printf("\n");
        }
    }
    
    close(file);
}
```

## Rust Implementation

### Basic Device ID Reading

```rust
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Write};
use std::os::unix::io::AsRawFd;

// I2C ioctl constants
const I2C_SLAVE: u16 = 0x0703;

#[repr(C)]
struct I2cMsg {
    addr: u16,
    flags: u16,
    len: u16,
    buf: *mut u8,
}

/// I2C device wrapper
pub struct I2cDevice {
    file: File,
}

impl I2cDevice {
    /// Open an I2C device
    pub fn new(device_path: &str) -> io::Result<Self> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(device_path)?;
        
        Ok(I2cDevice { file })
    }
    
    /// Set the slave address
    pub fn set_slave_address(&self, address: u8) -> io::Result<()> {
        unsafe {
            if libc::ioctl(self.file.as_raw_fd(), I2C_SLAVE as _, address as libc::c_ulong) < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        Ok(())
    }
    
    /// Read a single byte from a register
    pub fn read_register(&mut self, register: u8) -> io::Result<u8> {
        // Write register address
        self.file.write_all(&[register])?;
        
        // Read data
        let mut buffer = [0u8; 1];
        self.file.read_exact(&mut buffer)?;
        
        Ok(buffer[0])
    }
    
    /// Read multiple bytes from consecutive registers
    pub fn read_registers(&mut self, register: u8, buffer: &mut [u8]) -> io::Result<()> {
        self.file.write_all(&[register])?;
        self.file.read_exact(buffer)?;
        Ok(())
    }
}

/// Device identification information
#[derive(Debug, Clone)]
pub struct DeviceInfo {
    pub manufacturer_id: u8,
    pub device_id: u8,
    pub revision: u8,
    pub manufacturer_name: String,
    pub device_name: String,
}

impl DeviceInfo {
    /// Create a new DeviceInfo with decoded names
    pub fn new(manufacturer_id: u8, device_id: u8, revision: u8) -> Self {
        let manufacturer_name = match manufacturer_id {
            0x01 => "Bosch".to_string(),
            0x02 => "STMicroelectronics".to_string(),
            0x20 => "Texas Instruments".to_string(),
            _ => format!("Unknown (0x{:02X})", manufacturer_id),
        };
        
        let device_name = match device_id {
            0x60 => "BME280".to_string(),
            0x68 => "MPU6050".to_string(),
            0x58 => "BNO055".to_string(),
            _ => format!("Unknown (0x{:02X})", device_id),
        };
        
        DeviceInfo {
            manufacturer_id,
            device_id,
            revision,
            manufacturer_name,
            device_name,
        }
    }
    
    /// Display device information
    pub fn display(&self) {
        println!("=== Device Information ===");
        println!("Manufacturer: {} (ID: 0x{:02X})", 
                 self.manufacturer_name, self.manufacturer_id);
        println!("Device: {} (ID: 0x{:02X})", 
                 self.device_name, self.device_id);
        println!("Revision: 0x{:02X}", self.revision);
        println!("========================");
    }
}

/// MPU6050 specific constants
pub mod mpu6050 {
    pub const ADDR: u8 = 0x68;
    pub const WHO_AM_I: u8 = 0x75;
    pub const EXPECTED_ID: u8 = 0x68;
}

/// BME280 specific constants
pub mod bme280 {
    pub const ADDR: u8 = 0x76;
    pub const CHIP_ID: u8 = 0xD0;
    pub const EXPECTED_ID: u8 = 0x60;
}

/// Verify device ID matches expected value
pub fn verify_device_id(
    device: &mut I2cDevice,
    address: u8,
    id_register: u8,
    expected_id: u8,
) -> io::Result<bool> {
    device.set_slave_address(address)?;
    let device_id = device.read_register(id_register)?;
    
    println!("Device ID: 0x{:02X} (Expected: 0x{:02X})", device_id, expected_id);
    
    Ok(device_id == expected_id)
}

/// Example: Identify MPU6050
pub fn identify_mpu6050(device_path: &str) -> io::Result<()> {
    let mut device = I2cDevice::new(device_path)?;
    
    let verified = verify_device_id(
        &mut device,
        mpu6050::ADDR,
        mpu6050::WHO_AM_I,
        mpu6050::EXPECTED_ID,
    )?;
    
    if verified {
        println!("MPU6050 verification successful!");
        Ok(())
    } else {
        Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Device ID verification failed",
        ))
    }
}
```

### Advanced Device Identification

```rust
use std::collections::HashMap;

/// Device descriptor with identification registers
#[derive(Debug, Clone)]
pub struct DeviceDescriptor {
    pub name: &'static str,
    pub addresses: Vec<u8>,
    pub id_register: u8,
    pub expected_id: u8,
}

/// Device registry for known I2C devices
pub struct DeviceRegistry {
    devices: Vec<DeviceDescriptor>,
}

impl DeviceRegistry {
    /// Create a new registry with common devices
    pub fn new() -> Self {
        let devices = vec![
            DeviceDescriptor {
                name: "MPU6050",
                addresses: vec![0x68, 0x69],
                id_register: 0x75,
                expected_id: 0x68,
            },
            DeviceDescriptor {
                name: "BME280",
                addresses: vec![0x76, 0x77],
                id_register: 0xD0,
                expected_id: 0x60,
            },
            DeviceDescriptor {
                name: "BMP280",
                addresses: vec![0x76, 0x77],
                id_register: 0xD0,
                expected_id: 0x58,
            },
            DeviceDescriptor {
                name: "LSM6DS3",
                addresses: vec![0x6A, 0x6B],
                id_register: 0x0F,
                expected_id: 0x69,
            },
        ];
        
        DeviceRegistry { devices }
    }
    
    /// Try to identify a device at a given address
    pub fn identify_device(
        &self,
        i2c_device: &mut I2cDevice,
        address: u8,
    ) -> Option<String> {
        for descriptor in &self.devices {
            if !descriptor.addresses.contains(&address) {
                continue;
            }
            
            if let Ok(id) = i2c_device.read_register(descriptor.id_register) {
                if id == descriptor.expected_id {
                    return Some(descriptor.name.to_string());
                }
            }
        }
        None
    }
}

/// Scan I2C bus and identify all devices
pub fn scan_and_identify(device_path: &str) -> io::Result<HashMap<u8, String>> {
    let mut device = I2cDevice::new(device_path)?;
    let registry = DeviceRegistry::new();
    let mut found_devices = HashMap::new();
    
    println!("Scanning I2C bus for devices...\n");
    
    for address in 0x08..=0x77 {
        if device.set_slave_address(address).is_err() {
            continue;
        }
        
        // Test if device responds
        if device.read_register(0x00).is_ok() {
            println!("Device found at address 0x{:02X}", address);
            
            // Try to identify
            if let Some(name) = registry.identify_device(&mut device, address) {
                println!("  Identified as: {}", name);
                found_devices.insert(address, name);
            } else {
                println!("  Unknown device");
                found_devices.insert(address, "Unknown".to_string());
            }
        }
    }
    
    Ok(found_devices)
}
```

### Complete Example with Error Handling

```rust
use thiserror::Error;

#[derive(Error, Debug)]
pub enum I2cError {
    #[error("I/O error: {0}")]
    Io(#[from] io::Error),
    
    #[error("Device not found at address 0x{0:02X}")]
    DeviceNotFound(u8),
    
    #[error("Invalid device ID: expected 0x{expected:02X}, got 0x{actual:02X}")]
    InvalidDeviceId { expected: u8, actual: u8 },
    
    #[error("Register read failed")]
    RegisterReadFailed,
}

pub type Result<T> = std::result::Result<T, I2cError>;

/// Read and verify device information
pub fn read_device_info(
    device_path: &str,
    address: u8,
    mfg_reg: u8,
    dev_reg: u8,
    rev_reg: u8,
) -> Result<DeviceInfo> {
    let mut device = I2cDevice::new(device_path)?;
    device.set_slave_address(address)?;
    
    let manufacturer_id = device.read_register(mfg_reg)
        .map_err(|_| I2cError::RegisterReadFailed)?;
    
    let device_id = device.read_register(dev_reg)
        .map_err(|_| I2cError::RegisterReadFailed)?;
    
    let revision = device.read_register(rev_reg)
        .map_err(|_| I2cError::RegisterReadFailed)?;
    
    Ok(DeviceInfo::new(manufacturer_id, device_id, revision))
}

/// Main example
fn main() -> Result<()> {
    let device_path = "/dev/i2c-1";
    
    // Example 1: Verify MPU6050
    println!("Example 1: Verifying MPU6050");
    let mut device = I2cDevice::new(device_path)?;
    match verify_device_id(&mut device, mpu6050::ADDR, mpu6050::WHO_AM_I, mpu6050::EXPECTED_ID)? {
        true => println!("✓ MPU6050 verified successfully\n"),
        false => println!("✗ MPU6050 verification failed\n"),
    }
    
    // Example 2: Scan and identify all devices
    println!("Example 2: Scanning I2C bus");
    let devices = scan_and_identify(device_path)?;
    println!("\nFound {} device(s)", devices.len());
    
    Ok(())
}
```

## Best Practices

1. **Always verify device IDs** during initialization to ensure correct hardware is connected
2. **Handle missing devices gracefully** - don't assume a device is present
3. **Store expected IDs as constants** for maintainability
4. **Check revision numbers** when firmware-specific features are used
5. **Implement timeouts** when reading ID registers to avoid hanging
6. **Cache device information** after initial read to reduce bus traffic
7. **Support multiple possible addresses** as many devices have configurable addresses

## Common Pitfalls

- Not checking return values when reading ID registers
- Hardcoding device addresses without verification
- Assuming all devices have WHO_AM_I registers at the same location
- Forgetting that some devices require power-up delays before ID registers are valid
- Not handling devices that return 0xFF or 0x00 for unimplemented registers