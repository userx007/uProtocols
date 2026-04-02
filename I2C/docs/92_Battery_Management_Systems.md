# 92. Battery Management Systems

**Architecture & Concepts** — SoC/SoH measurement methods (coulomb counting, voltage-based, Impedance Track), protection parameters (OVP/UVP/OCP/OTP), and the distinction between I2C and SMBus (PEC, timeouts, SBS standard).

**IC Register Maps** — Detailed register tables for the TI BQ27220 fuel gauge and notes on BQ25895 charger and Maxim DS2782.

**C/C++ Code**
- Platform abstraction layer for Linux `/dev/i2c-N` using `I2C_RDWR` ioctl
- Full BQ27220 driver with typed structs and flag decoding
- C++ polling monitor with threshold alerting via callback
- SMBus PEC (CRC-8 poly 0x07) implementation with frame verification

**Rust Code**
- Typed register descriptors and constants
- Driver generic over `embedded-hal::I2c` — works on both Linux and bare-metal (Cortex-M/STM32/nRF52) without changes
- Threshold alert system using a `Vec<Alert>` pattern
- `no_std` bare-metal example using `stm32l4xx-hal`

**Advanced Topics** — Data Flash writes, calibration procedures, multi-cell balancing, and USB-C PD integration.

**Summary Table & Design Checklist** — Quick reference for protocol parameters, I2C addressing, pull-up sizing, and production considerations.

## I2C Communication with Smart Batteries and Fuel Gauge ICs

---

## Table of Contents

1. [Introduction](#introduction)
2. [Key Concepts and Architecture](#key-concepts-and-architecture)
3. [The SMBus Protocol](#the-smbus-protocol)
4. [Common ICs and Register Maps](#common-ics-and-register-maps)
5. [C/C++ Implementation](#cc-implementation)
6. [Rust Implementation](#rust-implementation)
7. [Advanced Topics](#advanced-topics)
8. [Summary](#summary)

---

## Introduction

Battery Management Systems (BMS) are critical subsystems in portable electronics, electric vehicles, industrial equipment, and IoT devices. They are responsible for monitoring, protecting, and optimizing the performance and lifetime of rechargeable battery cells. A modern BMS typically communicates over **I2C** (Inter-Integrated Circuit) or the closely related **SMBus** (System Management Bus), allowing a host microcontroller or processor to query real-time battery parameters, configure protection thresholds, and respond to fault conditions.

I2C-based battery management is built upon a well-defined hierarchy:

- **Smart Battery** — a rechargeable cell or pack with an embedded controller that exposes a standardized command set
- **Fuel Gauge IC** — a dedicated silicon device (e.g., Texas Instruments BQ27xxx, Maxim DS2782, Microchip MCP73871) that models battery capacity, state-of-charge (SoC), and state-of-health (SoH) in real time
- **Battery Charger IC** — manages the charging profile (CC/CV phases), reports status over I2C
- **Host System** — MCU or SoC that acts as I2C master, polls the battery and charger ICs, and takes protective or user-interface action

---

## Key Concepts and Architecture

### State of Charge (SoC)

SoC is expressed as a percentage (0–100%) representing the remaining energy relative to a fully charged cell. Fuel gauge ICs derive SoC through one or a combination of methods:

| Method | Description | Typical IC Support |
|---|---|---|
| **Coulomb Counting** | Integrates current in/out over time | BQ27220, DS2782 |
| **Voltage-based** | Maps OCV to SoC via lookup table | BQ27000, simple gauges |
| **Impedance Track™** | Models cell impedance vs. SoC/temp | BQ27z561, BQ27742 |
| **Hybrid** | Combines coulomb counting + OCV correction | Most modern ICs |

### State of Health (SoH)

SoH (0–100%) quantifies battery degradation — primarily capacity fade and internal resistance rise — relative to the cell's original specification. ICs with advanced algorithms (Impedance Track) update SoH autonomously after each charge cycle.

### Protection Parameters

A BMS enforces hardware-level protections such as:

- **OVP** — Overvoltage Protection (charging cutoff)
- **UVP** — Undervoltage Protection (discharge cutoff)
- **OCP** — Overcurrent Protection (both charge and discharge)
- **OTP** — Overtemperature Protection (thermistor-based)
- **Short-Circuit Protection** — instantaneous high-current cutoff via FET gate driver

### I2C vs. SMBus

While I2C and SMBus share the same two-wire physical layer (SDA + SCL), SMBus introduces:

- Mandatory timeout (25–35 ms bus idle before reset)
- Defined voltage levels for 3.3 V operation
- **PEC** (Packet Error Code) — a CRC-8 byte appended to each transaction for data integrity
- Standardized command set for smart batteries (SBS — Smart Battery Specification)

Most fuel gauge and smart battery ICs operate at **100 kHz** (Standard Mode). Some support **400 kHz** (Fast Mode).

---

## Common ICs and Register Maps

### Texas Instruments BQ27220 (Single-Cell Fuel Gauge)

I2C address: **0x55** (fixed)

| Register (hex) | Name | Size | Description |
|---|---|---|---|
| `0x00` | Control | 2 B | Subcommand gateway |
| `0x02` | AtRate | 2 B | Hypothetical current for time-remaining calculation |
| `0x04` | AtRateTimeToEmpty | 2 B | Minutes to empty at AtRate |
| `0x06` | Temperature | 2 B | Cell temperature in 0.1 K units |
| `0x08` | Voltage | 2 B | Cell voltage in mV |
| `0x0A` | Flags | 2 B | Status flags (CHGS, FC, FD, OTC, OTD…) |
| `0x0C` | NominalAvailableCapacity | 2 B | mAh, temperature-compensated |
| `0x0E` | FullAvailableCapacity | 2 B | mAh |
| `0x10` | RemainingCapacity | 2 B | mAh |
| `0x12` | FullChargeCapacity | 2 B | mAh |
| `0x14` | AverageCurrent | 2 B | mA (signed, negative = discharging) |
| `0x16` | TimeToEmpty | 2 B | Minutes |
| `0x18` | TimeToFull | 2 B | Minutes |
| `0x1C` | StateOfCharge | 2 B | % (0–100) |
| `0x1E` | StateOfHealth | 2 B | % |
| `0x28` | ChargingVoltage | 2 B | Recommended charge voltage in mV |
| `0x2A` | ChargingCurrent | 2 B | Recommended charge current in mA |

### Texas Instruments BQ25895 (Charger IC)

I2C address: **0x6A** (or 0x6B depending on ADDR pin)

Key registers include VBUS status, charging status, fault flags, input current limit, charge voltage and current target.

### Maxim DS2782 (Standalone Fuel Gauge)

I2C address: **0x34**

Maintains an analog front-end with integrated current-sense amplifier and temperature sensor. Uses a simple register map compatible with DS2781.

---

## C/C++ Implementation

### 1. Platform Abstraction Layer

A clean BMS driver separates I2C primitives from battery logic. The example below targets Linux (using `/dev/i2c-N` via `<linux/i2c-dev.h>`), but the same pattern applies to bare-metal MCUs using HAL functions.

```c
// bms_i2c.h — I2C abstraction for BMS drivers
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int fd;           // Linux: file descriptor for /dev/i2c-N
    uint8_t address;  // 7-bit I2C device address
    bool use_pec;     // Enable SMBus Packet Error Code checking
} i2c_device_t;

// Returns 0 on success, negative errno on failure
int i2c_open(i2c_device_t *dev, int bus, uint8_t addr);
void i2c_close(i2c_device_t *dev);

int i2c_read_word(const i2c_device_t *dev, uint8_t reg, uint16_t *out);
int i2c_write_word(const i2c_device_t *dev, uint8_t reg, uint16_t value);
int i2c_read_block(const i2c_device_t *dev, uint8_t reg,
                   uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
```

```c
// bms_i2c.c — Linux implementation
#include "bms_i2c.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int i2c_open(i2c_device_t *dev, int bus, uint8_t addr) {
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    dev->fd = open(path, O_RDWR);
    if (dev->fd < 0) return -errno;

    if (ioctl(dev->fd, I2C_SLAVE, addr) < 0) {
        close(dev->fd);
        return -errno;
    }
    dev->address = addr;
    return 0;
}

void i2c_close(i2c_device_t *dev) {
    if (dev->fd >= 0) close(dev->fd);
    dev->fd = -1;
}

int i2c_read_word(const i2c_device_t *dev, uint8_t reg, uint16_t *out) {
    // SMBus Read Word: START | addr+W | reg | RSTART | addr+R | low | high | STOP
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data data;
    uint8_t rx[2];

    msgs[0].addr  = dev->address;
    msgs[0].flags = 0;             // write
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    msgs[1].addr  = dev->address;
    msgs[1].flags = I2C_M_RD;     // read
    msgs[1].len   = 2;
    msgs[1].buf   = rx;

    data.msgs  = msgs;
    data.nmsgs = 2;

    if (ioctl(dev->fd, I2C_RDWR, &data) < 0) return -errno;

    // SMBus / SBS: little-endian 16-bit
    *out = (uint16_t)(rx[0] | (rx[1] << 8));
    return 0;
}

int i2c_write_word(const i2c_device_t *dev, uint8_t reg, uint16_t value) {
    uint8_t buf[3] = { reg, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    struct i2c_msg msg = {
        .addr  = dev->address,
        .flags = 0,
        .len   = 3,
        .buf   = buf
    };
    struct i2c_rdwr_ioctl_data data = { .msgs = &msg, .nmsgs = 1 };
    return (ioctl(dev->fd, I2C_RDWR, &data) < 0) ? -errno : 0;
}

int i2c_read_block(const i2c_device_t *dev, uint8_t reg,
                   uint8_t *buf, size_t len) {
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data data;

    msgs[0].addr  = dev->address;
    msgs[0].flags = 0;
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    msgs[1].addr  = dev->address;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = (uint16_t)len;
    msgs[1].buf   = buf;

    data.msgs  = msgs;
    data.nmsgs = 2;
    return (ioctl(dev->fd, I2C_RDWR, &data) < 0) ? -errno : 0;
}
```

---

### 2. BQ27220 Fuel Gauge Driver (C)

```c
// bq27220.h
#pragma once
#include "bms_i2c.h"
#include <stdbool.h>

#define BQ27220_ADDR        0x55

// Standard SBS command registers
#define BQ27220_REG_TEMP    0x06   // 0.1 K
#define BQ27220_REG_VOLT    0x08   // mV
#define BQ27220_REG_FLAGS   0x0A
#define BQ27220_REG_RC      0x10   // RemainingCapacity [mAh]
#define BQ27220_REG_FCC     0x12   // FullChargeCapacity [mAh]
#define BQ27220_REG_AI      0x14   // AverageCurrent [mA, signed]
#define BQ27220_REG_TTE     0x16   // TimeToEmpty [min]
#define BQ27220_REG_TTF     0x18   // TimeToFull [min]
#define BQ27220_REG_SOC     0x1C   // StateOfCharge [%]
#define BQ27220_REG_SOH     0x1E   // StateOfHealth [%]

// Flags register bits
#define BQ27220_FLAG_CHGS   (1 << 8)   // Charging state
#define BQ27220_FLAG_FC     (1 << 9)   // Full-charged
#define BQ27220_FLAG_FD     (1 << 4)   // Full-discharged
#define BQ27220_FLAG_OTC    (1 << 15)  // Overtemperature during charge
#define BQ27220_FLAG_OTD    (1 << 14)  // Overtemperature during discharge

typedef struct {
    uint16_t voltage_mv;
    int16_t  current_ma;    // negative = discharging
    float    temperature_c;
    uint8_t  soc_pct;
    uint8_t  soh_pct;
    uint16_t remaining_mah;
    uint16_t full_charge_mah;
    uint16_t tte_min;        // time to empty
    uint16_t ttf_min;        // time to full
    uint16_t flags;
} bq27220_data_t;

int bq27220_init(i2c_device_t *dev, int i2c_bus);
int bq27220_read_all(const i2c_device_t *dev, bq27220_data_t *data);
bool bq27220_is_charging(const bq27220_data_t *data);
bool bq27220_is_full(const bq27220_data_t *data);
void bq27220_print(const bq27220_data_t *data);
```

```c
// bq27220.c
#include "bq27220.h"
#include <stdio.h>
#include <string.h>

int bq27220_init(i2c_device_t *dev, int i2c_bus) {
    return i2c_open(dev, i2c_bus, BQ27220_ADDR);
}

int bq27220_read_all(const i2c_device_t *dev, bq27220_data_t *data) {
    uint16_t raw;
    int rc;

    memset(data, 0, sizeof(*data));

    if ((rc = i2c_read_word(dev, BQ27220_REG_VOLT, &raw)) < 0) return rc;
    data->voltage_mv = raw;

    if ((rc = i2c_read_word(dev, BQ27220_REG_AI, &raw)) < 0) return rc;
    data->current_ma = (int16_t)raw;  // signed 16-bit

    if ((rc = i2c_read_word(dev, BQ27220_REG_TEMP, &raw)) < 0) return rc;
    // SBS temperature: 0.1 K increments; subtract 2731.5 for Celsius
    data->temperature_c = (float)raw / 10.0f - 273.15f;

    if ((rc = i2c_read_word(dev, BQ27220_REG_SOC, &raw)) < 0) return rc;
    data->soc_pct = (uint8_t)raw;

    if ((rc = i2c_read_word(dev, BQ27220_REG_SOH, &raw)) < 0) return rc;
    data->soh_pct = (uint8_t)raw;

    if ((rc = i2c_read_word(dev, BQ27220_REG_RC, &raw)) < 0) return rc;
    data->remaining_mah = raw;

    if ((rc = i2c_read_word(dev, BQ27220_REG_FCC, &raw)) < 0) return rc;
    data->full_charge_mah = raw;

    if ((rc = i2c_read_word(dev, BQ27220_REG_TTE, &raw)) < 0) return rc;
    data->tte_min = raw;

    if ((rc = i2c_read_word(dev, BQ27220_REG_TTF, &raw)) < 0) return rc;
    data->ttf_min = raw;

    if ((rc = i2c_read_word(dev, BQ27220_REG_FLAGS, &raw)) < 0) return rc;
    data->flags = raw;

    return 0;
}

bool bq27220_is_charging(const bq27220_data_t *data) {
    return (data->flags & BQ27220_FLAG_CHGS) != 0;
}

bool bq27220_is_full(const bq27220_data_t *data) {
    return (data->flags & BQ27220_FLAG_FC) != 0;
}

void bq27220_print(const bq27220_data_t *data) {
    printf("=== BQ27220 Battery Status ===\n");
    printf("  Voltage     : %u mV\n",      data->voltage_mv);
    printf("  Current     : %d mA (%s)\n", data->current_ma,
           data->current_ma < 0 ? "discharging" : "charging");
    printf("  Temperature : %.1f °C\n",    data->temperature_c);
    printf("  SoC         : %u %%\n",      data->soc_pct);
    printf("  SoH         : %u %%\n",      data->soh_pct);
    printf("  Remaining   : %u mAh / %u mAh\n",
           data->remaining_mah, data->full_charge_mah);
    if (bq27220_is_charging(data))
        printf("  Time to Full: %u min\n",  data->ttf_min);
    else
        printf("  Time to Empty:%u min\n",  data->tte_min);
    printf("  Flags       : 0x%04X%s%s%s\n",
           data->flags,
           bq27220_is_charging(data) ? " [CHARGING]" : "",
           bq27220_is_full(data)     ? " [FULL]"     : "",
           (data->flags & BQ27220_FLAG_FD) ? " [DEPLETED]" : "");
}
```

---

### 3. BMS Monitoring Loop (C++)

```cpp
// bms_monitor.cpp — C++ polling loop with threshold alerting
#include "bq27220.h"
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <functional>

class BmsMonitor {
public:
    struct Config {
        int    i2c_bus      = 1;
        float  low_temp_c   = 0.0f;
        float  high_temp_c  = 45.0f;
        uint8_t low_soc_pct = 10;
        uint8_t low_soh_pct = 70;
        std::chrono::seconds poll_interval{5};
    };

    using AlertCallback = std::function<void(const std::string&, const bq27220_data_t&)>;

    explicit BmsMonitor(const Config& cfg, AlertCallback alert_cb)
        : cfg_(cfg), alert_cb_(alert_cb) {}

    ~BmsMonitor() { i2c_close(&dev_); }

    void start() {
        if (int rc = bq27220_init(&dev_, cfg_.i2c_bus); rc < 0)
            throw std::runtime_error("Failed to open I2C device");

        running_ = true;
        while (running_) {
            bq27220_data_t data{};
            if (int rc = bq27220_read_all(&dev_, &data); rc < 0) {
                std::cerr << "Read error: " << rc << "\n";
            } else {
                bq27220_print(&data);
                check_thresholds(data);
            }
            std::this_thread::sleep_for(cfg_.poll_interval);
        }
    }

    void stop() { running_ = false; }

private:
    Config          cfg_;
    AlertCallback   alert_cb_;
    i2c_device_t    dev_{};
    volatile bool   running_{false};

    void check_thresholds(const bq27220_data_t& d) {
        if (d.soc_pct <= cfg_.low_soc_pct)
            alert_cb_("LOW_SOC", d);
        if (d.soh_pct <= cfg_.low_soh_pct)
            alert_cb_("LOW_SOH", d);
        if (d.temperature_c < cfg_.low_temp_c)
            alert_cb_("UNDER_TEMP", d);
        if (d.temperature_c > cfg_.high_temp_c)
            alert_cb_("OVER_TEMP", d);
    }
};

int main() {
    BmsMonitor::Config cfg;
    cfg.i2c_bus       = 1;
    cfg.low_soc_pct   = 15;
    cfg.poll_interval = std::chrono::seconds(10);

    BmsMonitor monitor(cfg, [](const std::string& code, const bq27220_data_t& d) {
        std::cout << "[ALERT] " << code
                  << " | SoC=" << (int)d.soc_pct
                  << "% Temp=" << d.temperature_c << "°C\n";
    });

    monitor.start();
    return 0;
}
```

---

### 4. SMBus PEC (CRC-8) in C

When the battery IC requires SMBus Packet Error Code validation, compute CRC-8 over the entire transaction frame before writing/after reading:

```c
// smbus_pec.c — CRC-8 with polynomial 0x07 (x^8 + x^2 + x + 1)
#include <stdint.h>
#include <stddef.h>

uint8_t smbus_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x07);
            else
                crc <<= 1;
        }
    }
    return crc;
}

// Example: SMBus Read Word with PEC verification
// Frame for PEC: [addr<<1|0, cmd, addr<<1|1, low_byte, high_byte, pec]
int smbus_read_word_pec(int fd, uint8_t dev_addr, uint8_t cmd, uint16_t *out) {
    // ... perform I2C transaction, receive 3 bytes (lo, hi, pec) ...
    uint8_t lo, hi, received_pec;
    // (omitted: actual I2C read into lo, hi, received_pec)

    uint8_t frame[5] = {
        (uint8_t)(dev_addr << 1),        // write address
        cmd,
        (uint8_t)((dev_addr << 1) | 1),  // read address
        lo,
        hi
    };
    uint8_t expected_pec = smbus_crc8(frame, 5);

    if (expected_pec != received_pec) return -1; // PEC mismatch

    *out = (uint16_t)(lo | (hi << 8));
    return 0;
}
```

---

## Rust Implementation

Rust's embedded ecosystem provides two key crates:

- **`embedded-hal`** — hardware abstraction traits (I2C, SPI, GPIO)
- **`linux-embedded-hal`** — `embedded-hal` implementation for Linux `/dev/i2c-N`
- **`bq27220`** — community crate for TI BQ fuel gauges (illustrative)

The example below demonstrates a complete, idiomatic Rust BMS driver using `embedded-hal` traits, making it portable across both Linux and bare-metal targets (e.g., STM32, nRF52).

### Cargo.toml

```toml
[package]
name    = "bms_driver"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal     = "1.0"
linux-embedded-hal = "0.4"
anyhow           = "1"
```

---

### I2C Register Abstraction

```rust
// src/register.rs
/// A typed register descriptor: address + width
#[derive(Clone, Copy)]
pub struct Reg {
    pub addr: u8,
}

impl Reg {
    pub const fn new(addr: u8) -> Self { Self { addr } }
}

/// All BQ27220 SBS registers
pub mod bq27220 {
    use super::Reg;
    pub const TEMPERATURE : Reg = Reg::new(0x06);
    pub const VOLTAGE      : Reg = Reg::new(0x08);
    pub const FLAGS        : Reg = Reg::new(0x0A);
    pub const REM_CAPACITY : Reg = Reg::new(0x10);
    pub const FULL_CAPACITY: Reg = Reg::new(0x12);
    pub const AVG_CURRENT  : Reg = Reg::new(0x14);
    pub const TIME_TO_EMPTY: Reg = Reg::new(0x16);
    pub const TIME_TO_FULL : Reg = Reg::new(0x18);
    pub const STATE_OF_CHARGE : Reg = Reg::new(0x1C);
    pub const STATE_OF_HEALTH : Reg = Reg::new(0x1E);

    pub const FLAG_CHARGING : u16 = 1 << 8;
    pub const FLAG_FULL     : u16 = 1 << 9;
    pub const FLAG_DEPLETED : u16 = 1 << 4;
    pub const FLAG_OTC      : u16 = 1 << 15;
    pub const FLAG_OTD      : u16 = 1 << 14;

    pub const I2C_ADDR: u8 = 0x55;
}
```

---

### Fuel Gauge Driver

```rust
// src/bq27220.rs
use embedded_hal::i2c::I2c;
use crate::register::bq27220::*;
use crate::register::Reg;

/// Battery data snapshot
#[derive(Debug, Default)]
pub struct BatteryData {
    pub voltage_mv:       u16,
    pub current_ma:       i16,   // negative = discharging
    pub temperature_c:    f32,
    pub soc_pct:          u8,
    pub soh_pct:          u8,
    pub remaining_mah:    u16,
    pub full_charge_mah:  u16,
    pub time_to_empty_min: u16,
    pub time_to_full_min:  u16,
    pub flags:            u16,
}

impl BatteryData {
    pub fn is_charging(&self) -> bool { self.flags & FLAG_CHARGING != 0 }
    pub fn is_full(&self)     -> bool { self.flags & FLAG_FULL     != 0 }
    pub fn is_depleted(&self) -> bool { self.flags & FLAG_DEPLETED != 0 }
    pub fn is_over_temp(&self)-> bool {
        self.flags & (FLAG_OTC | FLAG_OTD) != 0
    }
}

pub struct Bq27220<I2C> {
    i2c:  I2C,
    addr: u8,
}

impl<I2C, E> Bq27220<I2C>
where
    I2C: I2c<Error = E>,
    E:   core::fmt::Debug,
{
    pub fn new(i2c: I2C) -> Self {
        Self { i2c, addr: I2C_ADDR }
    }

    /// Release the underlying I2C bus
    pub fn release(self) -> I2C { self.i2c }

    /// Read a 16-bit little-endian register
    fn read_u16(&mut self, reg: Reg) -> Result<u16, E> {
        let mut buf = [0u8; 2];
        self.i2c.write_read(self.addr, &[reg.addr], &mut buf)?;
        Ok(u16::from_le_bytes(buf))
    }

    /// Read all standard parameters in one pass
    pub fn read_all(&mut self) -> Result<BatteryData, E> {
        let mut d = BatteryData::default();

        d.voltage_mv      = self.read_u16(VOLTAGE)?;
        d.current_ma      = self.read_u16(AVG_CURRENT)? as i16;

        let raw_temp      = self.read_u16(TEMPERATURE)?;
        d.temperature_c   = raw_temp as f32 / 10.0 - 273.15;

        d.soc_pct         = self.read_u16(STATE_OF_CHARGE)? as u8;
        d.soh_pct         = self.read_u16(STATE_OF_HEALTH)? as u8;
        d.remaining_mah   = self.read_u16(REM_CAPACITY)?;
        d.full_charge_mah = self.read_u16(FULL_CAPACITY)?;
        d.time_to_empty_min = self.read_u16(TIME_TO_EMPTY)?;
        d.time_to_full_min  = self.read_u16(TIME_TO_FULL)?;
        d.flags           = self.read_u16(FLAGS)?;

        Ok(d)
    }
}
```

---

### Linux Application Entry Point

```rust
// src/main.rs
mod register;
mod bq27220;

use anyhow::Result;
use linux_embedded_hal::I2cdev;
use std::time::Duration;
use std::thread;

fn main() -> Result<()> {
    let i2c = I2cdev::new("/dev/i2c-1")?;
    let mut gauge = bq27220::Bq27220::new(i2c);

    loop {
        match gauge.read_all() {
            Ok(data) => print_data(&data),
            Err(e)   => eprintln!("I2C error: {:?}", e),
        }
        thread::sleep(Duration::from_secs(5));
    }
}

fn print_data(d: &bq27220::BatteryData) {
    println!("══════════════════════════════");
    println!(" Voltage      : {} mV",      d.voltage_mv);
    println!(" Current      : {} mA ({})",
             d.current_ma,
             if d.current_ma < 0 { "discharging" } else { "charging" });
    println!(" Temperature  : {:.1} °C",   d.temperature_c);
    println!(" SoC          : {} %",        d.soc_pct);
    println!(" SoH          : {} %",        d.soh_pct);
    println!(" Capacity     : {} / {} mAh",
             d.remaining_mah, d.full_charge_mah);

    if d.is_charging() {
        println!(" Time to Full : {} min",  d.time_to_full_min);
    } else {
        println!(" Time to Empty: {} min",  d.time_to_empty_min);
    }

    let status: Vec<&str> = [
        d.is_charging().then_some("CHARGING"),
        d.is_full()    .then_some("FULL"),
        d.is_depleted().then_some("DEPLETED"),
        d.is_over_temp().then_some("OVERTEMP"),
    ].iter().flatten().copied().collect();

    if !status.is_empty() {
        println!(" Status       : [{}]", status.join(", "));
    }
}
```

---

### Threshold Alert System (Rust)

```rust
// src/alerting.rs
use crate::bq27220::BatteryData;

#[derive(Debug, PartialEq, Eq, Hash)]
pub enum Alert {
    LowSoC,
    LowSoH,
    OverTemperature,
    UnderTemperature,
    CellDepleted,
}

pub struct AlertConfig {
    pub low_soc_pct:    u8,
    pub low_soh_pct:    u8,
    pub max_temp_c:     f32,
    pub min_temp_c:     f32,
}

impl Default for AlertConfig {
    fn default() -> Self {
        Self {
            low_soc_pct:  15,
            low_soh_pct:  70,
            max_temp_c:   45.0,
            min_temp_c:    0.0,
        }
    }
}

pub fn evaluate(data: &BatteryData, cfg: &AlertConfig) -> Vec<Alert> {
    let mut alerts = Vec::new();

    if data.soc_pct <= cfg.low_soc_pct         { alerts.push(Alert::LowSoC); }
    if data.soh_pct <= cfg.low_soh_pct         { alerts.push(Alert::LowSoH); }
    if data.temperature_c > cfg.max_temp_c     { alerts.push(Alert::OverTemperature); }
    if data.temperature_c < cfg.min_temp_c     { alerts.push(Alert::UnderTemperature); }
    if data.is_depleted()                      { alerts.push(Alert::CellDepleted); }

    alerts
}
```

---

### no_std / Bare-Metal Rust (Cortex-M example)

For resource-constrained targets like an nRF52832 or STM32L4, the driver works unchanged because it is generic over `embedded-hal::I2c`. Only the concrete I2C type changes:

```rust
// Bare-metal entry (no_std) — e.g., with stm32l4xx-hal
#![no_std]
#![no_main]

use cortex_m_rt::entry;
use stm32l4xx_hal::{i2c::I2c, prelude::*};
use panic_halt as _;

// Import our driver (same code as before — fully portable)
use bms_driver::bq27220::Bq27220;

#[entry]
fn main() -> ! {
    let dp  = stm32l4xx_hal::stm32::Peripherals::take().unwrap();
    let mut rcc = dp.RCC.constrain();
    let clocks  = rcc.cfgr.freeze();

    let mut gpiob = dp.GPIOB.split(&mut rcc.ahb2);
    let scl = gpiob.pb6.into_alternate_open_drain(&mut gpiob.moder, &mut gpiob.otyper, &mut gpiob.afrl);
    let sda = gpiob.pb7.into_alternate_open_drain(&mut gpiob.moder, &mut gpiob.otyper, &mut gpiob.afrl);

    let i2c = I2c::i2c1(dp.I2C1, (scl, sda), 100.khz(), clocks, &mut rcc.apb1r1);
    let mut gauge = Bq27220::new(i2c);

    loop {
        if let Ok(data) = gauge.read_all() {
            // Log via RTT, UART, or update an OLED display
            let _ = data.soc_pct; // use SoC
        }
        cortex_m::asm::delay(8_000_000); // ~1 s at 8 MHz
    }
}
```

---

## Advanced Topics

### Writing Data Flash (Configuration)

Fuel gauge ICs store calibration parameters (design capacity, Qmax, cell chemistry) in non-volatile **Data Flash**. Updating these requires an authenticated write sequence:

```c
// Sequence for BQ27xxx Data Flash write (C example)
// 1. Enter ROM access mode
i2c_write_word(&dev, 0x00, 0x0013);  // Control: SET_CFGUPDATE
// 2. Poll Flags register until CFGUPUPDATE bit is set (bit 2 of high byte)
// 3. Write to Data Flash subclass / offset via BlockData commands
// 4. Seal the gauge: Control(0x0020)
```

### Gas Gauge Calibration

For production accuracy, perform:

1. **Current Calibration** — measure the shunt resistor value precisely and write it to Data Flash
2. **Board Offset Calibration** — account for PCB trace resistance
3. **Voltage Calibration** — compare ADC reading against a precision reference
4. **Temperature Calibration** — characterise NTC thermistor curve

### Multi-Cell Packs

For series cell configurations (e.g., 2S, 3S Li-ion), a dedicated **cell balancer IC** (e.g., BQ77915, LTC6811) sits in front of the fuel gauge. It communicates individual cell voltages over I2C/SPI to the host and drives passive (resistive) or active (switched-capacitor) balancing circuits.

### Power Delivery Integration (USB-C PD)

Modern devices integrate the BMS with USB-C Power Delivery controllers (e.g., FUSB302, CYPD3177). The MCU negotiates PD contracts over CC lines (not I2C), then programs the charger IC's input current limit and charge voltage via I2C based on the negotiated PDO.

---

## Summary

| Aspect | Key Point |
|---|---|
| **Protocol** | I2C (typically 100 kHz), often SMBus-compatible with PEC |
| **Primary IC type** | Fuel gauge (BQ27xxx, DS2782) + charger IC (BQ2589x) |
| **Standard** | SBS (Smart Battery Specification) defines register map |
| **Core registers** | Voltage, Current, Temperature, SoC, SoH, TTE, TTF, Flags |
| **Data width** | 16-bit, little-endian per SBS |
| **C/C++ approach** | Thin I2C HAL + register-mapped struct; C++ wraps in RAII class |
| **Rust approach** | Generic over `embedded-hal::I2c` trait → same driver on Linux and bare-metal |
| **PEC** | CRC-8 (poly 0x07) over full transaction frame; required by some ICs |
| **Data Flash** | Non-volatile configuration; requires authenticated multi-step write sequence |
| **Calibration** | Shunt resistance, board offset, voltage, and temperature must be characterised for ±1% accuracy |
| **Multi-cell** | Cell balancer IC (LTC6811, BQ77915) communicates individual cell data before aggregation |
| **Safety** | Always read Flags register first; act on OTC/OTD/FD before accessing other fields |

### Design Checklist

- [ ] Confirm I2C address (some ICs have configurable LSBs via ADDR pin)
- [ ] Verify pull-up resistor sizing: 4.7 kΩ for 100 kHz, 2.2 kΩ for 400 kHz
- [ ] Enable PEC if the IC supports it for noise immunity
- [ ] Read `Flags` register at startup and handle fault states before polling
- [ ] Store full-charge capacity and Design Capacity in Data Flash for accurate SoC
- [ ] Implement a watchdog or timeout on I2C reads to handle bus lockup
- [ ] Test at temperature extremes — SoC algorithms are temperature-dependent
- [ ] Respect write-protect sequences before updating Data Flash parameters

---

*Document: 92 — Battery Management Systems | I2C Series*