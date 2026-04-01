# 62. I2C-Tools Usage

**Tool Reference** — all four utilities explained with flags, real command examples, and annotated sample output showing how to interpret results (e.g., `UU` vs `--` vs a detected address).

**C/C++ Programming** covers six progressively deeper examples:
1. Opening a bus and setting the slave address via `ioctl`
2. Raw byte and multi-byte register reads
3. Single-byte writes and read-modify-write with bitmask
4. Combined write+read using `I2C_RDWR` with `struct i2c_msg` (repeated START, no STOP between)
5. SMBus helper functions via `libi2c`
6. A full C++ RAII device class with typed methods

**Rust Programming** covers five examples:
1. `linux-embedded-hal` with `embedded-hal` traits (recommended approach)
2. An idiomatic device driver struct wrapping an `I2C: I2c` generic
3. `i2cdev` crate for lower-level SMBus access
4. Error handling and retry patterns
5. Bus scanning in Rust (mimicking `i2cdetect`)

**Debugging Workflow** — a structured 5-step process (detect → dump → read → write → verify) plus useful shell one-liners for continuous monitoring and logging.


## Command-line Utilities for I2C Development and Debugging

---

## Table of Contents

1. [Introduction](#introduction)
2. [What is I2C?](#what-is-i2c)
3. [The i2c-tools Package](#the-i2c-tools-package)
4. [Tool Reference](#tool-reference)
   - [i2cdetect](#i2cdetect)
   - [i2cdump](#i2cdump)
   - [i2cget](#i2cget)
   - [i2cset](#i2cset)
5. [Programming I2C in C/C++](#programming-i2c-in-cc)
6. [Programming I2C in Rust](#programming-i2c-in-rust)
7. [Practical Debugging Workflow](#practical-debugging-workflow)
8. [Summary](#summary)

---

## Introduction

I2C (Inter-Integrated Circuit) is one of the most widely used serial communication protocols in embedded systems and Linux-based hardware projects. The **i2c-tools** package provides a suite of command-line utilities that enable developers to inspect, test, read, and write to I2C devices directly from a Linux shell — without writing a single line of code.

These tools are indispensable during hardware bring-up, firmware debugging, sensor validation, and any situation where you want to interact with an I2C bus or peripheral manually. They are lightweight, scriptable, and integrate well into automated test pipelines.

---

## What is I2C?

I2C is a two-wire, master-slave (now called controller-target) serial protocol originally developed by Philips (now NXP). It uses:

- **SDA** — Serial Data Line (bidirectional)
- **SCL** — Serial Clock Line (driven by the controller)

Key characteristics:

| Feature | Value |
|---|---|
| Wire count | 2 (SDA + SCL) |
| Topology | Multi-master, multi-slave |
| Address width | 7-bit (standard) or 10-bit (extended) |
| Speed | Standard (100 kHz), Fast (400 kHz), Fast+ (1 MHz), High-speed (3.4 MHz) |
| Protocol | Start condition, address + R/W bit, ACK/NACK, data bytes, Stop condition |

On Linux, the kernel exposes I2C busses as device files: `/dev/i2c-0`, `/dev/i2c-1`, etc.

---

## The i2c-tools Package

### Installation

```bash
# Debian/Ubuntu/Raspberry Pi OS
sudo apt-get install i2c-tools

# Fedora/RHEL/CentOS
sudo dnf install i2c-tools

# Arch Linux
sudo pacman -S i2c-tools

# Alpine Linux
apk add i2c-tools
```

### Enabling I2C on Raspberry Pi

```bash
# Via raspi-config
sudo raspi-config
# Navigate to: Interface Options -> I2C -> Enable

# Or manually in /boot/config.txt
echo "dtparam=i2c_arm=on" | sudo tee -a /boot/config.txt
sudo modprobe i2c-dev
```

### Checking Available I2C Busses

```bash
ls /dev/i2c-*
# Output: /dev/i2c-0  /dev/i2c-1  /dev/i2c-2

# Or using i2cdetect's list mode
i2cdetect -l
# Output:
# i2c-0   i2c       DesignWare HDMI                 I2C adapter
# i2c-1   i2c       bcm2835 (i2c@7e804000)          I2C adapter
```

### Permissions

By default, `/dev/i2c-*` devices require root access. To allow non-root users:

```bash
# Add user to i2c group
sudo usermod -aG i2c $USER

# Set group ownership and permissions
sudo chown root:i2c /dev/i2c-1
sudo chmod 660 /dev/i2c-1

# Or create a udev rule (persistent across reboots)
echo 'KERNEL=="i2c-[0-9]*", GROUP="i2c", MODE="0660"' | \
    sudo tee /etc/udev/rules.d/99-i2c.rules
sudo udevadm control --reload-rules
```

---

## Tool Reference

---

### i2cdetect

`i2cdetect` scans an I2C bus and reports which addresses have responding devices. It is the first tool to reach for when you connect a new device.

#### Syntax

```
i2cdetect [-y] [-a] [-q|-r] <i2cbus> [<first> [<last>]]
i2cdetect -l
i2cdetect -F <i2cbus>
```

| Flag | Meaning |
|---|---|
| `-y` | Disable interactive prompt (for scripting) |
| `-a` | Scan all addresses (0x00–0x7F, including reserved) |
| `-q` | Use quick write (SMBus quick command) for probing |
| `-r` | Use read byte for probing (safer for some devices) |
| `-l` | List available I2C busses |
| `-F` | Show bus functionality flags |

#### Examples

```bash
# Scan bus 1, interactive mode (prompts before each write)
i2cdetect 1

# Scan bus 1 non-interactively (for scripts)
i2cdetect -y 1

# Scan only addresses 0x40 to 0x4F
i2cdetect -y 1 0x40 0x4F

# Scan all 128 addresses including reserved range
i2cdetect -y -a 1

# Use read byte probing (safer for EEPROMs and ADCs)
i2cdetect -y -r 1
```

#### Sample Output

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- 77
```

In this output, two devices are detected:
- `0x68` — an MPU-6050 gyroscope/accelerometer (default address)
- `0x77` — a BMP280 pressure sensor (SDO pin = VCC)

Cells marked `--` indicate no device responded. Cells marked `UU` indicate a device is present but already claimed by a kernel driver.

---

### i2cdump

`i2cdump` reads all (or a range of) registers from an I2C device and displays them in a hex dump format. It is used to inspect the full register map of a device.

#### Syntax

```
i2cdump [-y] [-f] [-r <first>-<last>] <i2cbus> <address> [<mode>]
```

| Mode | Meaning |
|---|---|
| `b` | Byte reads (default) |
| `w` | Word reads (16-bit, little-endian) |
| `W` | Word reads (16-bit, big-endian) |
| `s` | SMBus block read |
| `i` | I2C block read |
| `c` | Consecutive byte reads (no register address sent) |

| Flag | Meaning |
|---|---|
| `-y` | Skip confirmation prompt |
| `-f` | Force access even if driver is loaded |
| `-r <first>-<last>` | Read only a register range |

#### Examples

```bash
# Dump all registers of device at address 0x68 on bus 1
i2cdump -y 1 0x68

# Dump only registers 0x00 to 0x1F
i2cdump -y 1 0x68 -r 0x00-0x1F

# Word-mode dump (16-bit reads)
i2cdump -y 1 0x68 w

# Force access even if kernel driver owns the device
i2cdump -y -f 1 0x68
```

#### Sample Output

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f    0123456789abcdef
00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................
10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................
20: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    ................
...
70: 00 00 00 00 00 00 41 00 00 00 00 00 00 00 00 68    ......A.......h
```

The rightmost column shows ASCII characters where printable, and the hex grid shows the raw register values. Register `0x75` returning `0x68` is the WHO_AM_I register of the MPU-6050, confirming correct device identity.

---

### i2cget

`i2cget` reads a single byte or word from a specific register of an I2C device.

#### Syntax

```
i2cget [-y] [-f] <i2cbus> <chip-address> [<data-address> [<mode>]]
```

| Mode | Meaning |
|---|---|
| `b` | Read byte (default) |
| `w` | Read word (16-bit, little-endian) |
| `c` | Read byte without sending register address |

#### Examples

```bash
# Read register 0x75 (WHO_AM_I) from MPU-6050 at address 0x68 on bus 1
i2cget -y 1 0x68 0x75

# Read a 16-bit word from register 0x3B (accel X high byte)
i2cget -y 1 0x68 0x3B w

# Read a byte without specifying a register (current address pointer)
i2cget -y 1 0x68 c

# Force access even if kernel driver owns the device
i2cget -y -f 1 0x68 0x75
```

#### Sample Output

```bash
$ i2cget -y 1 0x68 0x75
0x68

$ i2cget -y 1 0x68 0x3B w
0x0102
```

This is useful for spot-checking individual registers during development — for example, verifying that a configuration register was written correctly, or reading a sensor measurement.

---

### i2cset

`i2cset` writes a byte, word, or block of data to a register on an I2C device. It is used to configure device registers, trigger measurements, or change operational modes.

#### Syntax

```
i2cset [-y] [-f] [-m <mask>] <i2cbus> <chip-address> <data-address> <value> [<mode>]
```

| Mode | Meaning |
|---|---|
| `b` | Write byte (default) |
| `w` | Write word (16-bit, little-endian) |
| `s` | Write SMBus block |
| `i` | Write I2C block |
| `c` | Write byte without sending register address |

| Flag | Meaning |
|---|---|
| `-y` | Skip confirmation |
| `-f` | Force even if kernel driver is loaded |
| `-m <mask>` | Apply bitmask (only write bits set in mask) |

#### Examples

```bash
# Write 0x00 to PWR_MGMT_1 (reg 0x6B) on MPU-6050 — wakes up the device
i2cset -y 1 0x68 0x6B 0x00

# Write a word (16-bit) to register 0x00 on a DAC at address 0x62
i2cset -y 1 0x62 0x00 0x0800 w

# Write with bitmask — only flip bit 1 of register 0x10
i2cset -y 1 0x68 0x10 0x02 -m 0x02

# Write 3 bytes as an I2C block to register 0x00
i2cset -y 1 0x40 0x00 0x12 0x34 0x56 i

# Force write even if driver owns the device
i2cset -y -f 1 0x68 0x6B 0x80
```

The `-m` (mask) flag is particularly powerful: it performs a read-modify-write operation, reading the current register value, masking in only the bits specified, and writing back the result — avoiding unintentional changes to other bits.

---

## Programming I2C in C/C++

Linux exposes I2C devices through `/dev/i2c-*` character devices. There are two primary approaches:

1. **ioctl-based raw access** — direct kernel interface, full control
2. **SMBus helper functions** — via `<linux/i2c-dev.h>`, easier for standard transactions

### Required Headers and Libraries

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
```

No external library is needed — everything is provided by the Linux kernel headers.

---

### Example 1: Opening a Bus and Setting the Target Address

```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

int i2c_open(int bus_number, uint8_t device_address) {
    char bus_path[32];
    snprintf(bus_path, sizeof(bus_path), "/dev/i2c-%d", bus_number);

    int fd = open(bus_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open I2C bus");
        return -1;
    }

    // Set the slave address for subsequent reads/writes
    if (ioctl(fd, I2C_SLAVE, device_address) < 0) {
        perror("Failed to set I2C slave address");
        close(fd);
        return -1;
    }

    return fd;
}

int main(void) {
    int fd = i2c_open(1, 0x68);  // Bus 1, MPU-6050
    if (fd < 0) return 1;

    printf("I2C bus opened successfully\n");

    close(fd);
    return 0;
}
```

---

### Example 2: Reading a Register (Raw ioctl)

```c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/**
 * Read a single byte from a specific register.
 * This performs a "write register address, then read" transaction,
 * which is the standard I2C random-read protocol.
 */
int i2c_read_byte(int fd, uint8_t reg_addr, uint8_t *value) {
    // First write the register address
    if (write(fd, &reg_addr, 1) != 1) {
        perror("Failed to write register address");
        return -1;
    }

    // Then read the response
    if (read(fd, value, 1) != 1) {
        perror("Failed to read from I2C device");
        return -1;
    }

    return 0;
}

/**
 * Read multiple consecutive registers.
 */
int i2c_read_bytes(int fd, uint8_t reg_addr, uint8_t *buffer, size_t length) {
    if (write(fd, &reg_addr, 1) != 1) {
        perror("Failed to write register address");
        return -1;
    }

    ssize_t bytes_read = read(fd, buffer, length);
    if (bytes_read != (ssize_t)length) {
        perror("Failed to read bytes from I2C device");
        return -1;
    }

    return 0;
}

int main(void) {
    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    if (ioctl(fd, I2C_SLAVE, 0x68) < 0) { perror("ioctl"); close(fd); return 1; }

    // Read WHO_AM_I register (0x75) from MPU-6050
    uint8_t who_am_i = 0;
    if (i2c_read_byte(fd, 0x75, &who_am_i) == 0) {
        printf("WHO_AM_I = 0x%02X (expected 0x68)\n", who_am_i);
    }

    // Read 6 bytes of accelerometer data starting at register 0x3B
    uint8_t accel_data[6] = {0};
    if (i2c_read_bytes(fd, 0x3B, accel_data, 6) == 0) {
        int16_t accel_x = (int16_t)((accel_data[0] << 8) | accel_data[1]);
        int16_t accel_y = (int16_t)((accel_data[2] << 8) | accel_data[3]);
        int16_t accel_z = (int16_t)((accel_data[4] << 8) | accel_data[5]);
        printf("Accel X: %d, Y: %d, Z: %d\n", accel_x, accel_y, accel_z);
    }

    close(fd);
    return 0;
}
```

---

### Example 3: Writing a Register

```c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/**
 * Write a single byte to a specific register.
 */
int i2c_write_byte(int fd, uint8_t reg_addr, uint8_t value) {
    uint8_t buf[2] = { reg_addr, value };
    if (write(fd, buf, 2) != 2) {
        perror("Failed to write to I2C device");
        return -1;
    }
    return 0;
}

/**
 * Read-Modify-Write: change only specific bits of a register.
 */
int i2c_write_bits(int fd, uint8_t reg_addr, uint8_t mask, uint8_t bits) {
    uint8_t current_value = 0;

    // Read current register value
    if (write(fd, &reg_addr, 1) != 1) return -1;
    if (read(fd, &current_value, 1) != 1) return -1;

    // Modify only the masked bits
    uint8_t new_value = (current_value & ~mask) | (bits & mask);

    // Write back
    return i2c_write_byte(fd, reg_addr, new_value);
}

int main(void) {
    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    if (ioctl(fd, I2C_SLAVE, 0x68) < 0) { perror("ioctl"); close(fd); return 1; }

    // Wake up MPU-6050: write 0x00 to PWR_MGMT_1 (reg 0x6B) to clear sleep bit
    if (i2c_write_byte(fd, 0x6B, 0x00) == 0) {
        printf("MPU-6050 woken up successfully\n");
    }

    // Set gyroscope full-scale range to ±500°/s using bitmask
    // Register 0x1B, bits [4:3], value 0x01 = ±500°/s
    if (i2c_write_bits(fd, 0x1B, 0x18, 0x08) == 0) {
        printf("Gyro range set to ±500°/s\n");
    }

    close(fd);
    return 0;
}
```

---

### Example 4: Using i2c_msg and I2C_RDWR for Combined Transactions

This approach sends a combined write+read without a Stop condition between them (called a "repeated start"), which is required by many devices:

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/**
 * Perform a combined write-then-read using I2C_RDWR ioctl.
 * This sends a repeated START between write and read — no STOP in between.
 * Required by some devices (e.g., BMP280, SI7021).
 */
int i2c_read_register_combined(int fd, uint8_t dev_addr,
                                uint8_t reg_addr,
                                uint8_t *buffer, size_t length) {
    struct i2c_msg messages[2];
    struct i2c_rdwr_ioctl_data ioctl_data;

    // Message 1: write the register address
    messages[0].addr  = dev_addr;
    messages[0].flags = 0;              // Write
    messages[0].len   = 1;
    messages[0].buf   = &reg_addr;

    // Message 2: read the response
    messages[1].addr  = dev_addr;
    messages[1].flags = I2C_M_RD;      // Read flag
    messages[1].len   = (uint16_t)length;
    messages[1].buf   = buffer;

    ioctl_data.msgs  = messages;
    ioctl_data.nmsgs = 2;

    if (ioctl(fd, I2C_RDWR, &ioctl_data) < 0) {
        perror("I2C_RDWR ioctl failed");
        return -1;
    }

    return 0;
}

int main(void) {
    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    // With I2C_RDWR, we do NOT call ioctl(I2C_SLAVE) — address is in the msg struct
    uint8_t calib_data[24] = {0};

    // Read BMP280 calibration registers (0x88 to 0x9F) at address 0x77
    if (i2c_read_register_combined(fd, 0x77, 0x88, calib_data, 24) == 0) {
        uint16_t dig_T1 = (uint16_t)(calib_data[1] << 8) | calib_data[0];
        int16_t  dig_T2 = (int16_t)((calib_data[3] << 8) | calib_data[2]);
        printf("BMP280 dig_T1 = %u, dig_T2 = %d\n", dig_T1, dig_T2);
    }

    close(fd);
    return 0;
}
```

---

### Example 5: SMBus Helper Functions (C)

The `smbus.h` helpers wrap common patterns in a cleaner API:

```c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// These functions are thin wrappers around I2C_SMBUS ioctl.
// They are available via the i2c-tools userspace library (libi2c).
// Link with: gcc myapp.c -li2c

#include <i2c/smbus.h>   // From i2c-tools development package

int main(void) {
    int fd = open("/dev/i2c-1", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    if (ioctl(fd, I2C_SLAVE, 0x68) < 0) { perror("ioctl"); close(fd); return 1; }

    // Read a single byte register
    int32_t result = i2c_smbus_read_byte_data(fd, 0x75);
    if (result >= 0) {
        printf("WHO_AM_I = 0x%02X\n", (uint8_t)result);
    }

    // Write a byte to a register
    if (i2c_smbus_write_byte_data(fd, 0x6B, 0x00) < 0) {
        perror("smbus write failed");
    }

    // Read a 16-bit word (little-endian)
    int32_t word = i2c_smbus_read_word_data(fd, 0x3B);
    if (word >= 0) {
        printf("Word at 0x3B = 0x%04X\n", (uint16_t)word);
    }

    // Read a block (up to 32 bytes)
    uint8_t block[32];
    int32_t count = i2c_smbus_read_i2c_block_data(fd, 0x3B, 6, block);
    if (count == 6) {
        printf("Read %d bytes from 0x3B\n", count);
    }

    close(fd);
    return 0;
}
```

---

### Example 6: C++ I2C Device Class

```cpp
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

class I2CDevice {
public:
    I2CDevice(int bus, uint8_t address)
        : address_(address)
    {
        std::string path = "/dev/i2c-" + std::to_string(bus);
        fd_ = open(path.c_str(), O_RDWR);
        if (fd_ < 0) {
            throw std::runtime_error("Cannot open I2C bus: " + path);
        }
        if (ioctl(fd_, I2C_SLAVE, address) < 0) {
            close(fd_);
            throw std::runtime_error("Cannot set I2C slave address");
        }
    }

    ~I2CDevice() {
        if (fd_ >= 0) close(fd_);
    }

    // Disable copy
    I2CDevice(const I2CDevice&) = delete;
    I2CDevice& operator=(const I2CDevice&) = delete;

    uint8_t readByte(uint8_t reg) {
        write_(reg);
        uint8_t value = 0;
        if (read(fd_, &value, 1) != 1)
            throw std::runtime_error("I2C read byte failed");
        return value;
    }

    std::vector<uint8_t> readBytes(uint8_t reg, size_t count) {
        write_(reg);
        std::vector<uint8_t> buffer(count);
        if (read(fd_, buffer.data(), count) != static_cast<ssize_t>(count))
            throw std::runtime_error("I2C read bytes failed");
        return buffer;
    }

    void writeByte(uint8_t reg, uint8_t value) {
        uint8_t buf[2] = { reg, value };
        if (write(fd_, buf, 2) != 2)
            throw std::runtime_error("I2C write byte failed");
    }

    // Read-Modify-Write with bitmask
    void writeBits(uint8_t reg, uint8_t mask, uint8_t bits) {
        uint8_t current = readByte(reg);
        writeByte(reg, (current & ~mask) | (bits & mask));
    }

    int16_t readInt16BE(uint8_t reg) {
        auto data = readBytes(reg, 2);
        return static_cast<int16_t>((data[0] << 8) | data[1]);
    }

private:
    void write_(uint8_t reg) {
        if (write(fd_, &reg, 1) != 1)
            throw std::runtime_error("I2C register address write failed");
    }

    int     fd_;
    uint8_t address_;
};

// Example: Read accelerometer data from MPU-6050
int main() {
    try {
        I2CDevice mpu(1, 0x68);  // Bus 1, MPU-6050

        // Wake the device
        mpu.writeByte(0x6B, 0x00);

        // Read WHO_AM_I
        uint8_t who = mpu.readByte(0x75);
        std::cout << "WHO_AM_I = 0x" << std::hex << (int)who << std::dec << "\n";

        // Read accelerometer XYZ
        int16_t ax = mpu.readInt16BE(0x3B);
        int16_t ay = mpu.readInt16BE(0x3D);
        int16_t az = mpu.readInt16BE(0x3F);

        // Scale to g (±2g default range, 16384 LSB/g)
        double gx = ax / 16384.0;
        double gy = ay / 16384.0;
        double gz = az / 16384.0;

        std::cout << "Accel: X=" << gx << "g  Y=" << gy << "g  Z=" << gz << "g\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

Compile with:

```bash
g++ -std=c++17 -o i2c_example i2c_example.cpp
```

---

## Programming I2C in Rust

Rust offers two popular approaches for I2C on Linux:

1. **`linux-embedded-hal`** — implements the `embedded-hal` traits over Linux I2C devices (recommended, portable)
2. **Direct `nix`/`libc` syscalls** — for low-level control when maximum flexibility is needed

### Cargo Dependencies

```toml
[dependencies]
linux-embedded-hal = "0.4"
embedded-hal = "1.0"

# Optional: for I2C device register abstraction
i2cdev = "0.6"

# For low-level approach
nix = { version = "0.27", features = ["ioctl"] }
libc = "0.2"
```

---

### Example 1: Using linux-embedded-hal (Recommended)

```rust
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open I2C bus 1
    let mut i2c = I2cdev::new("/dev/i2c-1")?;

    let dev_addr: u8 = 0x68; // MPU-6050

    // --- Wake up the device ---
    // Write: [register_address, value]
    i2c.write(dev_addr, &[0x6B, 0x00])?;
    println!("MPU-6050 woken up");

    // --- Read WHO_AM_I (register 0x75) ---
    let mut who_am_i = [0u8; 1];
    // write_read: sends register address, then reads without STOP in between
    i2c.write_read(dev_addr, &[0x75], &mut who_am_i)?;
    println!("WHO_AM_I = 0x{:02X} (expected 0x68)", who_am_i[0]);

    // --- Read 6 bytes of accelerometer data ---
    let mut accel_raw = [0u8; 6];
    i2c.write_read(dev_addr, &[0x3B], &mut accel_raw)?;

    let ax = i16::from_be_bytes([accel_raw[0], accel_raw[1]]);
    let ay = i16::from_be_bytes([accel_raw[2], accel_raw[3]]);
    let az = i16::from_be_bytes([accel_raw[4], accel_raw[5]]);

    // Convert to g (default ±2g range, 16384 LSB/g)
    println!(
        "Accel: X={:.3}g  Y={:.3}g  Z={:.3}g",
        ax as f32 / 16384.0,
        ay as f32 / 16384.0,
        az as f32 / 16384.0
    );

    Ok(())
}
```

---

### Example 2: I2C Device Driver Pattern in Rust

This example shows idiomatic Rust: wrapping the I2C bus in a device struct that owns its operations.

```rust
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;

const MPU6050_ADDR: u8 = 0x68;

// Register map
mod reg {
    pub const PWR_MGMT_1:  u8 = 0x6B;
    pub const WHO_AM_I:    u8 = 0x75;
    pub const GYRO_CONFIG: u8 = 0x1B;
    pub const ACCEL_XOUT_H: u8 = 0x3B;
    pub const GYRO_XOUT_H:  u8 = 0x43;
}

#[derive(Debug)]
pub struct Mpu6050<I2C> {
    i2c: I2C,
    addr: u8,
}

#[derive(Debug)]
pub struct AccelReading {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

#[derive(Debug)]
pub struct GyroReading {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl<I2C: I2c> Mpu6050<I2C> {
    pub fn new(i2c: I2C) -> Self {
        Self { i2c, addr: MPU6050_ADDR }
    }

    pub fn init(&mut self) -> Result<(), I2C::Error> {
        // Clear sleep mode
        self.write_register(reg::PWR_MGMT_1, 0x00)?;
        Ok(())
    }

    pub fn who_am_i(&mut self) -> Result<u8, I2C::Error> {
        self.read_register(reg::WHO_AM_I)
    }

    pub fn read_accel(&mut self) -> Result<AccelReading, I2C::Error> {
        let raw = self.read_bytes::<6>(reg::ACCEL_XOUT_H)?;
        let scale = 16384.0_f32; // ±2g range default

        Ok(AccelReading {
            x: i16::from_be_bytes([raw[0], raw[1]]) as f32 / scale,
            y: i16::from_be_bytes([raw[2], raw[3]]) as f32 / scale,
            z: i16::from_be_bytes([raw[4], raw[5]]) as f32 / scale,
        })
    }

    pub fn read_gyro(&mut self) -> Result<GyroReading, I2C::Error> {
        let raw = self.read_bytes::<6>(reg::GYRO_XOUT_H)?;
        let scale = 131.0_f32; // ±250°/s range default

        Ok(GyroReading {
            x: i16::from_be_bytes([raw[0], raw[1]]) as f32 / scale,
            y: i16::from_be_bytes([raw[2], raw[3]]) as f32 / scale,
            z: i16::from_be_bytes([raw[4], raw[5]]) as f32 / scale,
        })
    }

    // --- Private helpers ---

    fn write_register(&mut self, reg: u8, value: u8) -> Result<(), I2C::Error> {
        self.i2c.write(self.addr, &[reg, value])
    }

    fn read_register(&mut self, reg: u8) -> Result<u8, I2C::Error> {
        let mut buf = [0u8; 1];
        self.i2c.write_read(self.addr, &[reg], &mut buf)?;
        Ok(buf[0])
    }

    fn read_bytes<const N: usize>(&mut self, reg: u8) -> Result<[u8; N], I2C::Error> {
        let mut buf = [0u8; N];
        self.i2c.write_read(self.addr, &[reg], &mut buf)?;
        Ok(buf)
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let i2c = I2cdev::new("/dev/i2c-1")?;
    let mut sensor = Mpu6050::new(i2c);

    sensor.init()?;

    let id = sensor.who_am_i()?;
    println!("WHO_AM_I = 0x{id:02X}");

    let accel = sensor.read_accel()?;
    println!("Accel: X={:.3}g  Y={:.3}g  Z={:.3}g", accel.x, accel.y, accel.z);

    let gyro = sensor.read_gyro()?;
    println!("Gyro: X={:.2}°/s  Y={:.2}°/s  Z={:.2}°/s", gyro.x, gyro.y, gyro.z);

    Ok(())
}
```

---

### Example 3: Using the i2cdev Crate (Low-Level)

The `i2cdev` crate provides lower-level access closer to the kernel interface:

```rust
use i2cdev::core::*;
use i2cdev::linux::{LinuxI2CDevice, LinuxI2CError};

fn read_register(device: &mut LinuxI2CDevice, reg: u8)
    -> Result<u8, LinuxI2CError>
{
    device.smbus_read_byte_data(reg)
}

fn write_register(device: &mut LinuxI2CDevice, reg: u8, value: u8)
    -> Result<(), LinuxI2CError>
{
    device.smbus_write_byte_data(reg, value)
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open device at address 0x68 on bus 1
    let mut device = LinuxI2CDevice::new("/dev/i2c-1", 0x68)?;

    // Wake up MPU-6050
    write_register(&mut device, 0x6B, 0x00)?;

    // Read WHO_AM_I
    let id = read_register(&mut device, 0x75)?;
    println!("Device ID: 0x{id:02X}");

    // Read accelerometer block using SMBus block read
    let data = device.smbus_read_i2c_block_data(0x3B, 6)?;
    let ax = i16::from_be_bytes([data[0], data[1]]);
    println!("Raw Accel X: {ax}");

    Ok(())
}
```

---

### Example 4: Error Handling and Retry Pattern in Rust

```rust
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;
use std::time::Duration;
use std::thread;

/// Read a register with retry on transient errors.
fn read_with_retry<I2C: I2c>(
    i2c: &mut I2C,
    addr: u8,
    reg: u8,
    retries: u32,
) -> Result<u8, I2C::Error> {
    let mut last_err = None;

    for attempt in 0..=retries {
        if attempt > 0 {
            thread::sleep(Duration::from_millis(10));
            eprintln!("Retry {attempt}/{retries}...");
        }

        let mut buf = [0u8; 1];
        match i2c.write_read(addr, &[reg], &mut buf) {
            Ok(_) => return Ok(buf[0]),
            Err(e) => last_err = Some(e),
        }
    }

    Err(last_err.unwrap())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut i2c = I2cdev::new("/dev/i2c-1")?;

    match read_with_retry(&mut i2c, 0x68, 0x75, 3) {
        Ok(val) => println!("WHO_AM_I = 0x{val:02X}"),
        Err(e)  => eprintln!("Failed after retries: {e:?}"),
    }

    Ok(())
}
```

---

### Example 5: Scanning the Bus in Rust (Mimicking i2cdetect)

```rust
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;

fn scan_bus(i2c: &mut I2cdev) {
    println!("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");

    for row in 0..8u8 {
        print!("{:02x}: ", row * 16);
        for col in 0..16u8 {
            let addr = row * 16 + col;

            // Skip reserved address ranges (0x00-0x02, 0x78-0x7F)
            if addr < 0x03 || addr > 0x77 {
                print!("   ");
                continue;
            }

            // Probe by attempting a 0-byte write
            let mut buf = [0u8; 1];
            match i2c.read(addr, &mut buf) {
                Ok(_) => print!("{:02x} ", addr),
                Err(_) => print!("-- "),
            }
        }
        println!();
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut i2c = I2cdev::new("/dev/i2c-1")?;
    println!("Scanning I2C bus 1...\n");
    scan_bus(&mut i2c);
    Ok(())
}
```

---

## Practical Debugging Workflow

The typical workflow when working with a new I2C device follows these steps:

### Step 1 — Detect the device

```bash
i2cdetect -y 1
```

Confirm your device shows up at its expected address. If it doesn't appear:
- Check power and ground connections
- Check SDA/SCL wiring
- Verify pull-up resistors are present (typically 4.7kΩ)
- Try `-r` flag for safer probing

### Step 2 — Dump the register map

```bash
i2cdump -y 1 0x68
```

Compare the output against your device's datasheet. Look for:
- Identity/ID registers matching expected values
- Configuration registers at power-on defaults
- Unexpected 0xFF patterns (may indicate no device or wrong address)

### Step 3 — Read a known register

```bash
i2cget -y 1 0x68 0x75
# Should return 0x68 for MPU-6050 WHO_AM_I register
```

This validates the device is alive and register reads work correctly.

### Step 4 — Write a configuration

```bash
# Wake up MPU-6050 (clear sleep bit in PWR_MGMT_1)
i2cset -y 1 0x68 0x6B 0x00

# Verify it was written correctly
i2cget -y 1 0x68 0x6B
```

### Step 5 — Verify with a dump after configuration

```bash
i2cdump -y 1 0x68 -r 0x60-0x6F
```

This shows the power management registers and confirms your write took effect.

### Useful Shell Snippets

```bash
# Continuously read a sensor register every second
watch -n 1 "i2cget -y 1 0x68 0x41"  # Temperature high byte

# Log sensor readings to file
for i in $(seq 1 100); do
    val=$(i2cget -y 1 0x68 0x41)
    echo "$(date +%s.%N) $val" >> sensor_log.txt
    sleep 0.1
done

# Check bus speed and capabilities
i2cdetect -F 1

# Find all I2C devices across all buses
for bus in /dev/i2c-*; do
    num=$(echo $bus | grep -oP '\d+')
    echo "=== Bus $num ==="
    i2cdetect -y $num 2>/dev/null
done
```

---

## Summary

The **i2c-tools** suite provides four essential command-line utilities for working with I2C hardware on Linux:

| Tool | Purpose | Key Use Case |
|---|---|---|
| `i2cdetect` | Scan bus for responding devices | First tool to run on new hardware |
| `i2cdump` | Read all registers of a device | Full register map inspection |
| `i2cget` | Read a single register value | Spot-check values, verify writes |
| `i2cset` | Write a value to a register | Configuration, device initialization |

All four tools share common flags: `-y` suppresses interactive prompts for scripting, and `-f` forces access even when a kernel driver owns the device.

**In C/C++**, I2C access is achieved via the `/dev/i2c-*` character device using `open()`, `ioctl()` with `I2C_SLAVE`, and `read()`/`write()` system calls. For combined transactions without a STOP condition, the `I2C_RDWR` ioctl with `struct i2c_msg` arrays is used. The SMBus helper functions (via `libi2c`) offer a higher-level API for common operations.

**In Rust**, the `linux-embedded-hal` crate is the idiomatic choice: it implements the `embedded-hal` I2C traits over the Linux kernel interface, producing code that is portable to bare-metal targets as well. The `write_read()` method handles the write-then-read pattern with a repeated start natively.

The recommended debugging workflow is always: `i2cdetect` → `i2cdump` → `i2cget` → `i2cset` → verify. Combining these tools with scripting enables automated hardware bring-up and regression testing without writing any application code.