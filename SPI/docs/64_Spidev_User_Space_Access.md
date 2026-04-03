# 64. Spidev User-space Access

**Structure:**
- SPI protocol fundamentals (modes, CPOL/CPHA table, signal lines)
- Linux kernel SPI stack diagram (user-space → spidev → SPI core → hardware)
- Enabling spidev via Kconfig and Device Tree (DTS examples)
- Full `ioctl` API reference table with the `spi_ioc_transfer` struct explained

**C/C++ Code Examples:**
- `spi_basic.c` — open, configure (mode, bits, speed), verify settings
- `spi_transfer.c` — full-duplex transfer and two-phase write-then-read as a single `SPI_IOC_MESSAGE(2)`
- `mcp3008.c` — real-world MCP3008 10-bit ADC driver with channel reading and voltage conversion
- `SpiDevice.hpp` — a C++ RAII class wrapper with `transfer()`, `write()`, and `write_then_read()` methods

**Rust Code Examples:**
- Basic configuration and transfer using the `spidev` crate
- Multi-transfer batching (chained write + read)
- Full MCP3008 Rust driver as a proper struct with `read_channel_raw()`, `read_channel_voltage()`, and `read_all_channels()`
- Idiomatic Rust error handling with a custom `SpiError` enum and `From<io::Error>` impl
- Raw `libc::ioctl` approach (no external crate) for constrained environments

**Advanced Topics & Troubleshooting:**
- Batching multiple transfers for performance
- Extended mode flags (`SPI_CS_HIGH`, `SPI_3WIRE`, `SPI_LOOP`, etc.)
- udev rules for non-root access
- Troubleshooting table covering the most common failure modes


## Using `/dev/spidevX.Y` for Application-Level SPI Communication

---

## Table of Contents

1. [Introduction](#introduction)
2. [SPI Protocol Overview](#spi-protocol-overview)
3. [Linux Kernel SPI Subsystem](#linux-kernel-spi-subsystem)
4. [Enabling Spidev](#enabling-spidev)
5. [Device File Naming Convention](#device-file-naming-convention)
6. [Spidev IOCTL Interface](#spidev-ioctl-interface)
7. [Programming in C/C++](#programming-in-cc)
8. [Programming in Rust](#programming-in-rust)
9. [Advanced Topics](#advanced-topics)
10. [Troubleshooting](#troubleshooting)
11. [Summary](#summary)

---

## Introduction

The Linux `spidev` driver provides a user-space interface to SPI (Serial Peripheral Interface) bus controllers. Rather than requiring a kernel driver for every SPI-attached device, `spidev` exposes raw SPI bus access through character device files named `/dev/spidevX.Y`, where `X` is the SPI bus number and `Y` is the chip-select (device) number.

This mechanism is invaluable for:

- Rapid prototyping with SPI sensors, ADCs, DACs, displays, and memory chips
- Custom or proprietary SPI device communication without writing kernel modules
- Testing and debugging SPI hardware from user-space scripts and applications
- Embedded Linux applications (Raspberry Pi, BeagleBone, custom SoCs) that need direct hardware control

The spidev interface is stable, well-documented, and available on virtually all Linux-capable embedded platforms.

---

## SPI Protocol Overview

SPI is a synchronous, full-duplex serial communication protocol commonly used in embedded systems. Key characteristics:

- **Four signal lines**: SCLK (clock), MOSI (Master Out Slave In), MISO (Master In Slave Out), CS/SS (Chip Select, active-low)
- **Full duplex**: Data is simultaneously transmitted and received
- **Master-slave architecture**: The master controls the clock and chip select
- **No addressing**: Device selection is done via dedicated CS lines

### SPI Modes

SPI has four modes defined by Clock Polarity (CPOL) and Clock Phase (CPHA):

| Mode | CPOL | CPHA | Clock Idle | Data Sampled |
|------|------|------|------------|--------------|
| 0    | 0    | 0    | Low        | Rising edge  |
| 1    | 0    | 1    | Low        | Falling edge |
| 2    | 1    | 0    | High       | Falling edge |
| 3    | 1    | 1    | High       | Rising edge  |

---

## Linux Kernel SPI Subsystem

The Linux SPI stack has several layers:

```
[ User-space Application ]
         |
[ /dev/spidevX.Y character device ]
         |
[ spidev kernel driver ]
         |
[ SPI core subsystem (spi.h) ]
         |
[ SPI bus controller driver (e.g., spi-bcm2835, spi-omap2, spi-imx) ]
         |
[ Physical SPI hardware ]
```

The `spidev` module bridges the user-space and the kernel SPI core, translating `ioctl()` calls into `spi_message` structures that the SPI core processes.

---

## Enabling Spidev

### Kernel Configuration

Ensure `CONFIG_SPI_SPIDEV=y` or `=m` in your kernel configuration:

```
Device Drivers --->
  [*] SPI support --->
    <*> User mode SPI device driver support
```

### Device Tree Configuration

On modern Linux systems, spidev devices are declared in the Device Tree (DTS):

```dts
/* Example: Raspberry Pi style DTS fragment */
&spi0 {
    status = "okay";
    #address-cells = <1>;
    #size-cells = <0>;

    spidev0: spidev@0 {
        compatible = "rohm,dh2228fv";  /* or "spidev" on older kernels */
        reg = <0>;                      /* chip select 0 */
        spi-max-frequency = <10000000>; /* 10 MHz */
    };

    spidev1: spidev@1 {
        compatible = "rohm,dh2228fv";
        reg = <1>;                      /* chip select 1 */
        spi-max-frequency = <5000000>;  /* 5 MHz */
    };
};
```

> **Note**: Newer kernels (5.x+) require a real compatible string like `"rohm,dh2228fv"` or `"linux,spidev"`. Using just `"spidev"` may generate a warning. Some distributions (e.g., Raspberry Pi OS) configure this automatically via `raspi-config` or `/boot/config.txt`:
> ```
> dtparam=spi=on
> ```

### Verifying the Device

```bash
# List available spidev devices
ls -la /dev/spidev*

# Example output:
# crw-rw---- 1 root spi 153, 0 Jan 1 00:00 /dev/spidev0.0
# crw-rw---- 1 root spi 153, 1 Jan 1 00:00 /dev/spidev0.1

# Add user to spi group for access without sudo
sudo usermod -a -G spi $USER
```

---

## Device File Naming Convention

```
/dev/spidevX.Y
         │  │
         │  └─ Y: Chip Select number (0, 1, 2, ...)
         └──── X: SPI Bus number (0, 1, 2, ...)
```

| Device         | Meaning                                  |
|----------------|------------------------------------------|
| `/dev/spidev0.0` | SPI bus 0, chip select 0               |
| `/dev/spidev0.1` | SPI bus 0, chip select 1               |
| `/dev/spidev1.0` | SPI bus 1 (second controller), CS 0    |
| `/dev/spidev2.0` | SPI bus 2 (third controller), CS 0     |

---

## Spidev IOCTL Interface

Spidev is controlled via `ioctl()` calls using definitions from `<linux/spi/spidev.h>`. The most important IOCTLs are:

| IOCTL                        | Direction | Description                              |
|------------------------------|-----------|------------------------------------------|
| `SPI_IOC_RD_MODE`            | Read      | Get SPI mode (0–3)                       |
| `SPI_IOC_WR_MODE`            | Write     | Set SPI mode (0–3)                       |
| `SPI_IOC_RD_MODE32`          | Read      | Get extended SPI mode flags              |
| `SPI_IOC_WR_MODE32`          | Write     | Set extended SPI mode flags              |
| `SPI_IOC_RD_LSB_FIRST`       | Read      | Get bit order (0=MSB first)              |
| `SPI_IOC_WR_LSB_FIRST`       | Write     | Set bit order                            |
| `SPI_IOC_RD_BITS_PER_WORD`   | Read      | Get word size in bits (default 8)        |
| `SPI_IOC_WR_BITS_PER_WORD`   | Write     | Set word size in bits                    |
| `SPI_IOC_RD_MAX_SPEED_HZ`    | Read      | Get maximum bus speed                    |
| `SPI_IOC_WR_MAX_SPEED_HZ`    | Write     | Set maximum bus speed                    |
| `SPI_IOC_MESSAGE(n)`         | Write     | Submit n transfers in a single message   |

### The `spi_ioc_transfer` Structure

The core of all SPI transfers:

```c
struct spi_ioc_transfer {
    __u64   tx_buf;        /* Pointer to transmit buffer (or 0 for NOP) */
    __u64   rx_buf;        /* Pointer to receive buffer  (or 0 for discard) */
    __u32   len;           /* Length of tx and rx buffers in bytes */
    __u32   speed_hz;      /* Temporary override of bus speed (0 = use default) */
    __u16   delay_usecs;   /* Delay after transfer before CS change */
    __u8    bits_per_word; /* Bits per word (0 = use default) */
    __u8    cs_change;     /* Deassert CS after this transfer? */
    __u8    tx_nbits;      /* Number of bits for TX (1, 2, 4) */
    __u8    rx_nbits;      /* Number of bits for RX (1, 2, 4) */
    __u16   pad;           /* Reserved */
};
```

---

## Programming in C/C++

### Basic Setup and Configuration

```c
/*
 * spi_basic.c
 * Basic SPI device open, configure, and close example.
 * Compile: gcc -o spi_basic spi_basic.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <errno.h>

#define SPI_DEVICE      "/dev/spidev0.0"
#define SPI_MODE        SPI_MODE_0   /* CPOL=0, CPHA=0 */
#define SPI_BITS        8            /* 8 bits per word */
#define SPI_SPEED_HZ    1000000      /* 1 MHz */

int spi_open_and_configure(const char *device, uint8_t mode,
                            uint8_t bits, uint32_t speed) {
    int fd;

    /* Open the SPI device */
    fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Failed to open SPI device");
        return -1;
    }

    /* Set SPI mode (CPOL, CPHA) */
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("Failed to set SPI mode");
        close(fd);
        return -1;
    }

    /* Verify the mode was set correctly */
    uint8_t actual_mode;
    if (ioctl(fd, SPI_IOC_RD_MODE, &actual_mode) < 0) {
        perror("Failed to read SPI mode");
        close(fd);
        return -1;
    }
    printf("SPI mode set to: %u\n", actual_mode);

    /* Set bits per word */
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        perror("Failed to set bits per word");
        close(fd);
        return -1;
    }

    /* Set maximum clock speed */
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("Failed to set SPI speed");
        close(fd);
        return -1;
    }

    uint32_t actual_speed;
    if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &actual_speed) < 0) {
        perror("Failed to read SPI speed");
        close(fd);
        return -1;
    }
    printf("SPI speed set to: %u Hz\n", actual_speed);

    return fd;
}

int main(void) {
    int fd = spi_open_and_configure(SPI_DEVICE, SPI_MODE, SPI_BITS, SPI_SPEED_HZ);
    if (fd < 0) {
        return EXIT_FAILURE;
    }

    printf("SPI device configured successfully.\n");
    close(fd);
    return EXIT_SUCCESS;
}
```

---

### Full-Duplex Transfer

```c
/*
 * spi_transfer.c
 * Full-duplex SPI transfer using SPI_IOC_MESSAGE.
 * In full-duplex, tx and rx happen simultaneously.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>

/*
 * spi_transfer - Perform a single full-duplex SPI transfer.
 *
 * @fd:     Open file descriptor for spidev
 * @tx:     Transmit buffer (send this data)
 * @rx:     Receive buffer (received data goes here)
 * @len:    Number of bytes to transfer
 * @speed:  Transfer speed in Hz (0 = use device default)
 *
 * Returns 0 on success, -1 on error.
 */
int spi_transfer(int fd, const uint8_t *tx, uint8_t *rx,
                 size_t len, uint32_t speed) {
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = len,
        .speed_hz      = speed,
        .delay_usecs   = 0,
        .bits_per_word = 8,
        .cs_change     = 0,
    };

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
        perror("SPI_IOC_MESSAGE failed");
        return -1;
    }
    return 0;
}

/*
 * spi_write_then_read - Write command bytes, then read response.
 * CS stays asserted between the two phases (cs_change = 0).
 * This is done as a single SPI_IOC_MESSAGE with 2 transfers.
 */
int spi_write_then_read(int fd, const uint8_t *cmd, size_t cmd_len,
                         uint8_t *resp, size_t resp_len) {
    struct spi_ioc_transfer tr[2] = {
        {
            /* Phase 1: Write command */
            .tx_buf        = (unsigned long)cmd,
            .rx_buf        = 0,           /* discard MISO during command */
            .len           = cmd_len,
            .speed_hz      = 0,
            .delay_usecs   = 0,
            .bits_per_word = 8,
            .cs_change     = 0,           /* keep CS asserted */
        },
        {
            /* Phase 2: Read response */
            .tx_buf        = 0,           /* MOSI = 0x00 during read */
            .rx_buf        = (unsigned long)resp,
            .len           = resp_len,
            .speed_hz      = 0,
            .delay_usecs   = 0,
            .bits_per_word = 8,
            .cs_change     = 0,
        },
    };

    int ret = ioctl(fd, SPI_IOC_MESSAGE(2), tr);
    if (ret < 0) {
        perror("SPI_IOC_MESSAGE(2) failed");
        return -1;
    }
    return 0;
}

int main(void) {
    int fd = open("/dev/spidev0.0", O_RDWR);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    uint8_t mode  = SPI_MODE_0;
    uint8_t bits  = 8;
    uint32_t speed = 500000; /* 500 kHz */

    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    /* Example: full-duplex loopback test */
    uint8_t tx[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t rx[sizeof(tx)];
    memset(rx, 0, sizeof(rx));

    if (spi_transfer(fd, tx, rx, sizeof(tx), speed) == 0) {
        printf("TX: ");
        for (size_t i = 0; i < sizeof(tx); i++) printf("%02X ", tx[i]);
        printf("\nRX: ");
        for (size_t i = 0; i < sizeof(rx); i++) printf("%02X ", rx[i]);
        printf("\n");
    }

    /* Example: write command, then read 3-byte response */
    uint8_t cmd[]  = { 0x9F };       /* Common "Read ID" command */
    uint8_t resp[3] = { 0 };

    if (spi_write_then_read(fd, cmd, sizeof(cmd), resp, sizeof(resp)) == 0) {
        printf("Device ID: %02X %02X %02X\n", resp[0], resp[1], resp[2]);
    }

    close(fd);
    return EXIT_SUCCESS;
}
```

---

### Real-World Example: Reading an MCP3008 ADC

The MCP3008 is an 8-channel, 10-bit SPI ADC (analog-to-digital converter) commonly used with Raspberry Pi and similar boards.

```c
/*
 * mcp3008.c
 * Read analog values from a Microchip MCP3008 10-bit ADC via SPI.
 *
 * MCP3008 SPI protocol (3-byte full-duplex):
 *   Byte 0: 0x01            (start bit)
 *   Byte 1: 0x80 | (ch<<4) (single-ended, channel select)
 *   Byte 2: 0x00            (don't care)
 *
 *   Result is in the lower 10 bits of: ((rx[1] & 0x03) << 8) | rx[2]
 *
 * Wiring (SPI Mode 0, max 3.6 MHz at 5V, 1.35 MHz at 2.7V):
 *   MCP3008 VDD/VREF -> 3.3V
 *   MCP3008 AGND/DGND -> GND
 *   MCP3008 CLK  -> SPI SCLK
 *   MCP3008 DOUT -> SPI MISO
 *   MCP3008 DIN  -> SPI MOSI
 *   MCP3008 CS   -> SPI CS0
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>

#define MCP3008_DEVICE  "/dev/spidev0.0"
#define MCP3008_SPEED   1350000    /* 1.35 MHz safe for 3.3V operation */
#define MCP3008_CHANNELS 8

/*
 * mcp3008_read_channel - Read a single channel from MCP3008.
 * @fd:      Open spidev file descriptor
 * @channel: Channel number 0-7
 * Returns: 10-bit ADC value (0-1023), or -1 on error
 */
int mcp3008_read_channel(int fd, uint8_t channel) {
    if (channel > 7) return -1;

    uint8_t tx[3] = {
        0x01,                    /* Start bit */
        0x80 | (channel << 4),  /* Single-ended, channel select */
        0x00                     /* Don't care */
    };
    uint8_t rx[3] = { 0 };

    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = 3,
        .speed_hz      = MCP3008_SPEED,
        .delay_usecs   = 0,
        .bits_per_word = 8,
        .cs_change     = 0,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("ioctl SPI_IOC_MESSAGE");
        return -1;
    }

    /* Extract the 10-bit result */
    int value = ((rx[1] & 0x03) << 8) | rx[2];
    return value;
}

int main(void) {
    int fd = open(MCP3008_DEVICE, O_RDWR);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    uint8_t mode  = SPI_MODE_0;
    uint8_t bits  = 8;
    uint32_t speed = MCP3008_SPEED;

    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    printf("MCP3008 ADC Readings (10-bit, 0-1023):\n");
    printf("%-10s %-10s %-12s\n", "Channel", "Raw Value", "Voltage (3.3V)");
    printf("--------------------------------------\n");

    for (int ch = 0; ch < MCP3008_CHANNELS; ch++) {
        int raw = mcp3008_read_channel(fd, ch);
        if (raw < 0) {
            fprintf(stderr, "Failed to read channel %d\n", ch);
            continue;
        }
        double voltage = (raw / 1023.0) * 3.3;
        printf("CH%-8d %-10d %.4f V\n", ch, raw, voltage);
    }

    close(fd);
    return EXIT_SUCCESS;
}
```

---

### C++ Class Wrapper for Spidev

```cpp
/*
 * SpiDevice.hpp
 * A C++ RAII wrapper around the spidev Linux interface.
 * Provides clean resource management and a simple transfer API.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

class SpiDevice {
public:
    struct Config {
        uint8_t  mode         = SPI_MODE_0;
        uint8_t  bits_per_word = 8;
        uint32_t speed_hz     = 1'000'000; /* 1 MHz default */
        bool     lsb_first    = false;
    };

    /* Constructor: opens and configures the SPI device */
    explicit SpiDevice(const std::string &device, const Config &config = {})
        : device_(device), config_(config)
    {
        fd_ = ::open(device.c_str(), O_RDWR);
        if (fd_ < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "Failed to open " + device);
        }

        apply_config(config);
    }

    /* Destructor: RAII close */
    ~SpiDevice() {
        if (fd_ >= 0) ::close(fd_);
    }

    /* Non-copyable */
    SpiDevice(const SpiDevice &) = delete;
    SpiDevice &operator=(const SpiDevice &) = delete;

    /* Move-constructible */
    SpiDevice(SpiDevice &&other) noexcept
        : fd_(other.fd_), device_(std::move(other.device_)),
          config_(other.config_)
    {
        other.fd_ = -1;
    }

    /*
     * transfer() - Full-duplex SPI transfer.
     * @tx: bytes to transmit (MOSI)
     * Returns vector of received bytes (MISO), same length as tx.
     */
    std::vector<uint8_t> transfer(const std::vector<uint8_t> &tx) {
        std::vector<uint8_t> rx(tx.size(), 0x00);

        struct spi_ioc_transfer tr{};
        tr.tx_buf        = reinterpret_cast<unsigned long>(tx.data());
        tr.rx_buf        = reinterpret_cast<unsigned long>(rx.data());
        tr.len           = static_cast<uint32_t>(tx.size());
        tr.speed_hz      = config_.speed_hz;
        tr.delay_usecs   = 0;
        tr.bits_per_word = config_.bits_per_word;
        tr.cs_change     = 0;

        if (::ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "SPI transfer failed");
        }
        return rx;
    }

    /*
     * write() - Write-only transfer (discards MISO).
     */
    void write(const std::vector<uint8_t> &data) {
        struct spi_ioc_transfer tr{};
        tr.tx_buf        = reinterpret_cast<unsigned long>(data.data());
        tr.rx_buf        = 0;
        tr.len           = static_cast<uint32_t>(data.size());
        tr.speed_hz      = config_.speed_hz;
        tr.bits_per_word = config_.bits_per_word;

        if (::ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "SPI write failed");
        }
    }

    /*
     * write_then_read() - Write command bytes, then read response.
     * CS remains asserted between the two phases.
     */
    std::vector<uint8_t> write_then_read(const std::vector<uint8_t> &cmd,
                                          size_t read_len) {
        std::vector<uint8_t> rx(read_len, 0x00);

        struct spi_ioc_transfer tr[2]{};

        /* Phase 1: Write command */
        tr[0].tx_buf        = reinterpret_cast<unsigned long>(cmd.data());
        tr[0].rx_buf        = 0;
        tr[0].len           = static_cast<uint32_t>(cmd.size());
        tr[0].speed_hz      = config_.speed_hz;
        tr[0].bits_per_word = config_.bits_per_word;
        tr[0].cs_change     = 0;

        /* Phase 2: Read response */
        tr[1].tx_buf        = 0;
        tr[1].rx_buf        = reinterpret_cast<unsigned long>(rx.data());
        tr[1].len           = static_cast<uint32_t>(read_len);
        tr[1].speed_hz      = config_.speed_hz;
        tr[1].bits_per_word = config_.bits_per_word;
        tr[1].cs_change     = 0;

        if (::ioctl(fd_, SPI_IOC_MESSAGE(2), tr) < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "SPI write_then_read failed");
        }
        return rx;
    }

    const std::string &device() const { return device_; }
    int fd() const { return fd_; }

private:
    void apply_config(const Config &cfg) {
        auto check = [&](int ret, const char *msg) {
            if (ret < 0) {
                throw std::system_error(errno, std::generic_category(), msg);
            }
        };

        check(::ioctl(fd_, SPI_IOC_WR_MODE, &cfg.mode),
              "Failed to set SPI mode");
        check(::ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &cfg.bits_per_word),
              "Failed to set bits per word");
        check(::ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &cfg.speed_hz),
              "Failed to set SPI speed");

        uint8_t lsb = cfg.lsb_first ? 1 : 0;
        check(::ioctl(fd_, SPI_IOC_WR_LSB_FIRST, &lsb),
              "Failed to set bit order");
    }

    int         fd_     = -1;
    std::string device_;
    Config      config_;
};

/*
 * SpiDevice.cpp usage example:
 *
 *   SpiDevice::Config cfg;
 *   cfg.mode     = SPI_MODE_0;
 *   cfg.speed_hz = 2'000'000;  // 2 MHz
 *
 *   SpiDevice spi("/dev/spidev0.0", cfg);
 *
 *   // Read device ID (e.g., W25Q flash chip)
 *   auto id = spi.write_then_read({0x9F}, 3);
 *   printf("Manufacturer: %02X, Device: %02X%02X\n", id[0], id[1], id[2]);
 *
 *   // Full-duplex echo
 *   auto rx = spi.transfer({0xDE, 0xAD, 0xBE, 0xEF});
 */
```

---

## Programming in Rust

### Dependencies

Add to `Cargo.toml`:

```toml
[package]
name = "spidev-example"
version = "0.1.0"
edition = "2021"

[dependencies]
spidev = "0.6"          # Safe Rust spidev wrapper
libc    = "0.2"
```

The [`spidev`](https://crates.io/crates/spidev) crate provides a safe, idiomatic Rust API over the raw Linux spidev ioctl interface.

---

### Basic Configuration and Transfer in Rust

```rust
// src/main.rs
// Basic SPI configuration and full-duplex transfer using the spidev crate.

use spidev::{Spidev, SpidevOptions, SpiModeFlags, SpidevTransfer};
use std::io;

fn configure_spi(device: &str) -> io::Result<Spidev> {
    let mut spi = Spidev::open(device)?;

    let options = SpidevOptions::new()
        .bits_per_word(8)
        .max_speed_hz(1_000_000)         // 1 MHz
        .mode(SpiModeFlags::SPI_MODE_0)  // CPOL=0, CPHA=0
        .build();

    spi.configure(&options)?;
    Ok(spi)
}

fn spi_full_duplex_transfer(
    spi: &mut Spidev,
    tx_data: &[u8],
) -> io::Result<Vec<u8>> {
    let mut rx_data = vec![0u8; tx_data.len()];

    {
        // SpidevTransfer holds references, so it must be in its own scope
        let mut transfer = SpidevTransfer::read_write(tx_data, &mut rx_data);
        spi.transfer(&mut transfer)?;
    }

    Ok(rx_data)
}

fn main() -> io::Result<()> {
    let mut spi = configure_spi("/dev/spidev0.0")?;

    println!("SPI device opened and configured.");

    // Full-duplex loopback test
    let tx = vec![0xDE, 0xAD, 0xBE, 0xEF];
    let rx = spi_full_duplex_transfer(&mut spi, &tx)?;

    print!("TX: ");
    for b in &tx { print!("{:02X} ", b); }
    println!();

    print!("RX: ");
    for b in &rx { print!("{:02X} ", b); }
    println!();

    Ok(())
}
```

---

### Multiple Transfers in a Single Message (Rust)

```rust
// src/multi_transfer.rs
// Demonstrates chained transfers (write command, then read response)
// with CS held asserted between them, using the spidev crate.

use spidev::{Spidev, SpidevOptions, SpiModeFlags, SpidevTransfer};
use std::io;

fn write_then_read(
    spi: &mut Spidev,
    cmd: &[u8],
    read_len: usize,
) -> io::Result<Vec<u8>> {
    let mut rx = vec![0u8; read_len];

    // Build two transfers: write and read
    // The spidev crate submits them as a single SPI_IOC_MESSAGE,
    // keeping CS asserted throughout.
    let mut write_transfer = SpidevTransfer::write(cmd);
    let mut read_transfer  = SpidevTransfer::read(&mut rx);

    spi.transfer_multiple(&mut [write_transfer, read_transfer])?;

    Ok(rx)
}

pub fn run_example() -> io::Result<()> {
    let mut spi = Spidev::open("/dev/spidev0.0")?;

    spi.configure(
        &SpidevOptions::new()
            .bits_per_word(8)
            .max_speed_hz(2_000_000)
            .mode(SpiModeFlags::SPI_MODE_0)
            .build(),
    )?;

    // Read JEDEC ID from W25Qxx flash (command 0x9F, 3-byte response)
    let jedec_cmd = [0x9Fu8];
    let id = write_then_read(&mut spi, &jedec_cmd, 3)?;

    println!(
        "JEDEC ID -> Manufacturer: {:02X}, Type: {:02X}, Capacity: {:02X}",
        id[0], id[1], id[2]
    );

    Ok(())
}
```

---

### MCP3008 ADC Driver in Rust

```rust
// src/mcp3008.rs
// MCP3008 8-channel 10-bit SPI ADC driver in Rust.
// Protocol: 3-byte full-duplex transfer.

use spidev::{Spidev, SpidevOptions, SpiModeFlags, SpidevTransfer};
use std::io;

pub struct Mcp3008 {
    spi: Spidev,
    vref: f64,   // Reference voltage (typically 3.3 V)
}

impl Mcp3008 {
    const MAX_CHANNELS: u8  = 8;
    const RESOLUTION: f64   = 1023.0; // 10-bit ADC
    const MAX_SPEED_HZ: u32 = 1_350_000; // 1.35 MHz at 3.3V

    pub fn new(device: &str, vref: f64) -> io::Result<Self> {
        let mut spi = Spidev::open(device)?;

        spi.configure(
            &SpidevOptions::new()
                .bits_per_word(8)
                .max_speed_hz(Self::MAX_SPEED_HZ)
                .mode(SpiModeFlags::SPI_MODE_0)
                .build(),
        )?;

        Ok(Self { spi, vref })
    }

    /// Read raw 10-bit ADC value for the given channel (0–7).
    pub fn read_channel_raw(&mut self, channel: u8) -> io::Result<u16> {
        if channel >= Self::MAX_CHANNELS {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("Channel {} out of range (0-7)", channel),
            ));
        }

        let tx = [
            0x01u8,                          // Start bit
            0x80 | (channel << 4),           // Single-ended, channel select
            0x00u8,                          // Don't care
        ];
        let mut rx = [0u8; 3];

        {
            let mut transfer = SpidevTransfer::read_write(&tx, &mut rx);
            self.spi.transfer(&mut transfer)?;
        }

        // Extract 10-bit result: bits [1:0] of rx[1] + all of rx[2]
        let value = (((rx[1] & 0x03) as u16) << 8) | rx[2] as u16;
        Ok(value)
    }

    /// Read channel and convert to voltage.
    pub fn read_channel_voltage(&mut self, channel: u8) -> io::Result<f64> {
        let raw = self.read_channel_raw(channel)?;
        Ok((raw as f64 / Self::RESOLUTION) * self.vref)
    }

    /// Read all 8 channels, returns (raw, voltage) pairs.
    pub fn read_all_channels(&mut self) -> io::Result<Vec<(u16, f64)>> {
        (0..Self::MAX_CHANNELS)
            .map(|ch| {
                let raw = self.read_channel_raw(ch)?;
                let volt = (raw as f64 / Self::RESOLUTION) * self.vref;
                Ok((raw, volt))
            })
            .collect()
    }
}

fn main() -> io::Result<()> {
    let mut adc = Mcp3008::new("/dev/spidev0.0", 3.3)?;

    println!("{:<8} {:<12} {}", "Channel", "Raw (0-1023)", "Voltage");
    println!("{}", "-".repeat(35));

    let readings = adc.read_all_channels()?;
    for (ch, (raw, volt)) in readings.iter().enumerate() {
        println!("CH{:<6}  {:<12} {:.4} V", ch, raw, volt);
    }

    Ok(())
}
```

---

### Rust Error Handling and Custom Error Type

```rust
// src/spi_error.rs
// Demonstrates idiomatic Rust error handling for SPI operations.

use std::fmt;
use std::io;

#[derive(Debug)]
pub enum SpiError {
    Io(io::Error),
    InvalidChannel(u8),
    TransferFailed { expected: usize, got: usize },
    ConfigurationError(String),
}

impl fmt::Display for SpiError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SpiError::Io(e)              => write!(f, "I/O error: {}", e),
            SpiError::InvalidChannel(ch) => write!(f, "Invalid channel: {} (max 7)", ch),
            SpiError::TransferFailed { expected, got } =>
                write!(f, "Transfer failed: expected {} bytes, got {}", expected, got),
            SpiError::ConfigurationError(msg) =>
                write!(f, "SPI configuration error: {}", msg),
        }
    }
}

impl std::error::Error for SpiError {}

impl From<io::Error> for SpiError {
    fn from(e: io::Error) -> Self {
        SpiError::Io(e)
    }
}

// Example usage with ? operator and custom error type
use spidev::{Spidev, SpidevOptions, SpiModeFlags, SpidevTransfer};

pub struct RobustSpiDevice {
    spi: Spidev,
}

impl RobustSpiDevice {
    pub fn open(device: &str, speed_hz: u32) -> Result<Self, SpiError> {
        let mut spi = Spidev::open(device)?;  // io::Error -> SpiError via From

        if speed_hz == 0 || speed_hz > 100_000_000 {
            return Err(SpiError::ConfigurationError(
                format!("Speed {} Hz is out of valid range", speed_hz)
            ));
        }

        spi.configure(
            &SpidevOptions::new()
                .max_speed_hz(speed_hz)
                .mode(SpiModeFlags::SPI_MODE_0)
                .bits_per_word(8)
                .build(),
        )?;

        Ok(Self { spi })
    }

    pub fn transfer(&mut self, data: &[u8]) -> Result<Vec<u8>, SpiError> {
        let mut rx = vec![0u8; data.len()];
        {
            let mut t = SpidevTransfer::read_write(data, &mut rx);
            self.spi.transfer(&mut t)?;
        }
        Ok(rx)
    }
}
```

---

### Raw ioctl Approach in Rust (No External Crate)

For scenarios where the `spidev` crate is not available, you can use raw `ioctl` calls via `libc`:

```rust
// src/raw_ioctl.rs
// Raw SPI access using libc ioctl, without the spidev crate.
// Useful in bare-metal or constrained build environments.

use libc::{c_int, ioctl, open, close, O_RDWR};
use std::ffi::CString;
use std::mem;

// spidev ioctl definitions (from <linux/spi/spidev.h>)
const SPI_IOC_MAGIC: u8 = b'k';
const SPI_IOC_TYPE_MESSAGE: u8 = 0;

// _IOW macro equivalent
fn spi_ioc_wr_mode() -> u64 {
    nix::request_code_write!(SPI_IOC_MAGIC, 1, mem::size_of::<u8>())
}

#[repr(C)]
struct SpiIocTransfer {
    tx_buf:        u64,
    rx_buf:        u64,
    len:           u32,
    speed_hz:      u32,
    delay_usecs:   u16,
    bits_per_word: u8,
    cs_change:     u8,
    tx_nbits:      u8,
    rx_nbits:      u8,
    pad:           u16,
}

// Note: In production, prefer the spidev crate over raw ioctl usage.
// This example illustrates what happens under the hood.
```

---

## Advanced Topics

### Multi-Transfer Batching for Performance

When communicating at high speed with a device, submitting multiple transfers in a single `SPI_IOC_MESSAGE(n)` call reduces the overhead of multiple kernel context switches:

```c
/* batch_transfer.c
 * Send 4 independent SPI transactions in a single ioctl call.
 * Each uses cs_change=1 to deassert CS between transactions.
 */

#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>

#define NUM_TRANSFERS 4

int spi_batch_transfer(int fd, uint8_t tx[][8], uint8_t rx[][8],
                        size_t len, int n) {
    struct spi_ioc_transfer tr[NUM_TRANSFERS];
    memset(tr, 0, sizeof(tr));

    for (int i = 0; i < n; i++) {
        tr[i].tx_buf        = (unsigned long)tx[i];
        tr[i].rx_buf        = (unsigned long)rx[i];
        tr[i].len           = len;
        tr[i].speed_hz      = 0;    /* use device default */
        tr[i].bits_per_word = 8;
        /* cs_change=1 deaserts CS after each individual transfer */
        tr[i].cs_change     = (i < n - 1) ? 1 : 0;
    }

    return ioctl(fd, SPI_IOC_MESSAGE(NUM_TRANSFERS), tr);
}
```

### Extended Mode Flags

Beyond basic mode 0–3, the `SPI_IOC_WR_MODE32` ioctl accepts additional flags:

```c
/* Extended SPI mode flags (from <linux/spi/spi.h>) */
uint32_t mode32 = SPI_MODE_0
                | SPI_CS_HIGH       /* CS active HIGH instead of LOW */
                | SPI_NO_CS        /* No CS (single device, SW-managed) */
                | SPI_3WIRE        /* MOSI/MISO on same wire (half-duplex) */
                | SPI_LOOP         /* Loopback mode (for testing) */
                | SPI_LSB_FIRST;   /* LSB shifted out first */

ioctl(fd, SPI_IOC_WR_MODE32, &mode32);
```

### Permissions and udev Rules

To avoid running as root, create a udev rule:

```bash
# /etc/udev/rules.d/99-spidev.rules
SUBSYSTEM=="spidev", GROUP="spi", MODE="0660"
```

Then create the group and add your user:

```bash
sudo groupadd spi
sudo usermod -a -G spi $USER
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

## Troubleshooting

| Symptom | Likely Cause | Solution |
|---------|-------------|----------|
| `/dev/spidevX.Y` does not exist | spidev not enabled in kernel or DTS | Enable `CONFIG_SPI_SPIDEV`, add DTS node |
| `open()` returns ENOENT | Wrong device path or bus number | Check `ls /dev/spidev*` |
| `open()` returns EACCES | Insufficient permissions | Add user to `spi` group, or use udev rule |
| `ioctl()` returns EINVAL | Invalid mode, speed, or length | Check mode (0–3), positive speed, valid buf pointers |
| All RX bytes are 0x00 | No loopback / MISO not connected | Check wiring; hardware may need MOSI→MISO jumper for testing |
| Data corruption at high speed | Trace impedance, signal integrity | Reduce speed; add series resistors |
| CS not toggling | `cs_change=0` with multiple transfers | Set `cs_change=1` between independent transactions |
| Kernel warning: "spidev is not designed to be used" | Compatible string `"spidev"` in DTS | Use `"rohm,dh2228fv"` or `"linux,spidev"` |

---

## Summary

The `spidev` interface (`/dev/spidevX.Y`) provides a robust, stable, and kernel-driver-free path to SPI communication from Linux user-space applications.

**Core concepts covered:**

- **Device naming**: `/dev/spidevX.Y` maps to SPI bus `X`, chip select `Y`
- **Configuration**: Mode (0–3 for CPOL/CPHA), speed in Hz, bits per word, bit order are all set via `ioctl()`
- **Transfers**: The `spi_ioc_transfer` structure and `SPI_IOC_MESSAGE(n)` ioctl form the backbone of all data exchange — supporting single-phase, two-phase (write-then-read), and batched multi-transfer messages
- **C/C++**: Direct `ioctl()` calls offer maximum control; a C++ RAII wrapper provides safer, more ergonomic access with automatic resource cleanup
- **Rust**: The `spidev` crate wraps the ioctl interface idiomatically, with proper error propagation via `std::io::Error` or custom error types
- **Real device example**: The MCP3008 ADC demonstrates the 3-byte full-duplex protocol pattern used by countless SPI devices

**When to use spidev vs. a kernel driver:**

| Use Case | Recommendation |
|----------|---------------|
| Prototyping, one-off tools, test jigs | `spidev` — fast to iterate, no kernel build needed |
| Production embedded product, high throughput, DMA | Kernel driver — lower latency, better integration |
| SPI device already has a kernel driver | Use the kernel driver via its sysfs/character interface |
| Need sub-millisecond timing guarantees | Kernel driver or PREEMPT_RT patched kernel |

The spidev approach strikes an excellent balance between flexibility and simplicity for the vast majority of user-space SPI communication tasks in embedded Linux systems.

---

*Document: Linux SPI Topic 64 — Spidev User-space Access*
*Covers: C, C++, Rust | Kernel: Linux 5.x+ | Interface: `/dev/spidevX.Y`*