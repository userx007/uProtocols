# 94. Touchscreen Controllers — I2C Communication

**Structure:** 13 sections spanning ~500 lines of documentation and code.

**Hardware coverage:** FT6236/FT6206, Goodix GT911, CST816S, and ILITEK ILI2130 — with register maps, I2C addresses, and signal descriptions for each.

**C/C++ examples include:**
- A platform-independent I2C HAL abstraction layer
- Full FT6236 driver (`.h` + `.c`) with init, touch read, threshold/rate config
- Full GT911 C++ class with 16-bit register addressing, burst reads, config checksum, and the critical buffer-clear acknowledgement pattern
- Interrupt-driven touch manager with ISR + main-loop pattern and RTOS semaphore variant

**Rust examples include:**
- FT6236 driver generic over `embedded-hal 1.0` `I2c` trait, with type-safe `Gesture` and `TouchEvent` enums
- GT911 driver using `heapless::Vec` for no-alloc multi-point storage, 16-bit register helpers, and config write
- Async FT6236 driver using `embedded-hal-async` for Embassy/async-RTOS integration

**Additional topics:** coordinate transform math, pinch-to-zoom gesture algorithm, GT911 address selection via RST/INT sequencing, sensitivity tuning table, and a troubleshooting reference table.

## Reading Multi-Touch Data and Configuring Touch Parameters over I2C

---

## Table of Contents

1. [Introduction](#introduction)
2. [Hardware Architecture](#hardware-architecture)
3. [I2C Communication Basics for Touch Controllers](#i2c-communication-basics)
4. [Common Touchscreen Controller ICs](#common-touchscreen-controller-ics)
5. [Register Map Overview](#register-map-overview)
6. [Multi-Touch Protocol](#multi-touch-protocol)
7. [Programming in C/C++](#programming-in-cc)
8. [Programming in Rust](#programming-in-rust)
9. [Configuration and Calibration](#configuration-and-calibration)
10. [Interrupt Handling](#interrupt-handling)
11. [Gesture Recognition](#gesture-recognition)
12. [Troubleshooting](#troubleshooting)
13. [Summary](#summary)

---

## Introduction

Touchscreen controllers are dedicated ICs that translate raw capacitive or resistive sensor signals into precise touch coordinate data. They interface with a host microcontroller or processor almost universally over I2C, making them a very common I2C peripheral in embedded and consumer electronics design.

Modern touchscreen controllers support:

- **Multi-touch** — tracking up to 10 simultaneous touch points
- **Gesture recognition** — swipes, pinch-to-zoom, tap/double-tap
- **Configurable sensitivity** — threshold tuning, noise filtering
- **Interrupt-driven reporting** — efficient host notification via a dedicated INT pin
- **Firmware updates** — reprogrammable controllers for field upgrades

Understanding how to read multi-touch data and configure touch parameters over I2C is essential for any embedded developer working with display-integrated systems, HMI panels, kiosks, or portable devices.

---

## Hardware Architecture

```
┌──────────────────────────────────────────────────────────┐
│                  Host MCU / SoC                          │
│                                                          │
│   I2C Master        GPIO Input        GPIO Output        │
│   (SDA / SCL)       (INT pin)         (RESET pin)        │
└────────┬───────────────┬──────────────────┬──────────────┘
         │               │                  │
         │ I2C Bus       │ Interrupt        │ Reset
         │               │                  │
┌────────▼───────────────▼──────────────────▼──────────────┐
│              Touchscreen Controller IC                   │
│         (e.g. FT6236, GT911, CST816S, ILITEK)            │
│                                                          │
│  ┌─────────────┐   ┌──────────────┐   ┌──────────────┐   │
│  │  Touch AFE  │   │  DSP / MCU   │   │  I2C Slave   │   │
│  │ (Capacitive │──▶│  (Filtering, │──▶│  Interface   │   │
│  │  Sensing)   │   │  Tracking)   │   │  Registers   │   │
│  └─────────────┘   └──────────────┘   └──────────────┘   │
│                                                          │
└─────────────────────────┬────────────────────────────────┘
                          │
              ┌───────────▼──────────────┐
              │   Capacitive Touch Panel │
              │   (ITO sensor matrix)    │
              └──────────────────────────┘
```

**Key signals:**

| Signal | Direction   | Description                                       |
|--------|-------------|---------------------------------------------------|
| SDA    | Bidirectional | I2C data line (requires pull-up, typically 4.7kΩ) |
| SCL    | Host → IC   | I2C clock line (requires pull-up)                 |
| INT    | IC → Host   | Active-low interrupt: new touch data available    |
| RST    | Host → IC   | Active-low hardware reset                         |
| VCC    | Power       | Typically 1.8V or 3.3V                            |

---

## I2C Communication Basics

Touchscreen controllers operate as **I2C slave devices** with a fixed 7-bit address. The host writes to a register address, then reads back data.

**Typical I2C addresses:**

| Controller | Default Address | Alternate Address |
|------------|-----------------|-------------------|
| FT6236/FT6206 | 0x38         | —                 |
| Goodix GT911  | 0x5D         | 0x14              |
| CST816S       | 0x15         | —                 |
| ILITEK ILI2130| 0x41         | —                 |
| Azoteq IQS5xx | 0x74         | Configurable      |

**Standard register read sequence:**

```
START → ADDR+W → REG_ADDR → REPEATED START → ADDR+R → DATA[0..N] → STOP
```

**Standard register write sequence:**

```
START → ADDR+W → REG_ADDR → DATA[0..N] → STOP
```

I2C clock speeds used: 100 kHz (Standard), 400 kHz (Fast Mode). Most touch controllers support Fast Mode (400 kHz).

---

## Common Touchscreen Controller ICs

### FocalTech FT6236 / FT6206

A widely used capacitive touch controller found in many maker and embedded display modules. Supports up to 2 simultaneous touch points. Simple register interface.

- Address: `0x38`
- Max touch points: 2
- Interrupt: Active-low, open-drain
- Chip ID register: `0xA3` (returns `0x36` for FT6236)

### Goodix GT911

High-end controller used in tablets, industrial panels, and smartphones. Supports up to 10 simultaneous touch points, gesture detection, and configuration via I2C.

- Addresses: `0x5D` or `0x14` (set by INT/RST initialization sequence)
- Max touch points: 10
- Resolution: Up to 4096 × 4096
- Configuration data: 186-byte config block written to registers

### CST816S / CST816T

Compact ultra-low-power controller common in small round/square displays (e.g. round watch-style displays). Supports basic gestures (swipe, single tap, double tap).

- Address: `0x15`
- Max touch points: 1
- Gesture register: Encodes gesture ID directly

### ILITEK ILI2130

Industrial-grade multi-touch controller with USB and I2C interfaces, used in HMI panels.

---

## Register Map Overview

### FT6236 Key Registers

| Register | Address | Description                                  |
|----------|---------|----------------------------------------------|
| DEV_MODE | 0x00    | Device mode (normal, test)                   |
| GEST_ID  | 0x01    | Detected gesture ID                          |
| TD_STATUS| 0x02    | Number of touch points detected (0–2)        |
| P1_XH    | 0x03    | Touch point 1: X high byte + event flag      |
| P1_XL    | 0x04    | Touch point 1: X low byte                    |
| P1_YH    | 0x05    | Touch point 1: Y high byte + touch ID        |
| P1_YL    | 0x06    | Touch point 1: Y low byte                    |
| P1_WEIGHT| 0x07    | Touch point 1: pressure/weight               |
| P1_MISC  | 0x08    | Touch point 1: touch area                    |
| P2_XH    | 0x09    | Touch point 2: X high byte                   |
| ...      | ...     | (same pattern for P2)                        |
| TH_GROUP | 0x80    | Touch threshold sensitivity                  |
| CTRL     | 0x86    | Power control (active/monitor/standby)       |
| PERIOD_ACTIVE | 0x88 | Report rate in active mode (ms)            |
| CHIP_ID  | 0xA3    | Chip identification (0x36 = FT6236)          |
| FIRMID   | 0xA6    | Firmware version                             |

### Goodix GT911 Key Registers

| Register   | Address | Description                                        |
|------------|---------|----------------------------------------------------|
| CONFIG     | 0x8047  | Start of 186-byte configuration block              |
| PRODUCT_ID | 0x8140  | Product ID string (4 bytes, ASCII "911\0")         |
| FW_VERSION | 0x8144  | Firmware version (2 bytes)                         |
| X_RESOLUTION| 0x8146 | Max X resolution (2 bytes, little-endian)          |
| Y_RESOLUTION| 0x8148 | Max Y resolution (2 bytes, little-endian)          |
| TOUCH_NUM  | 0x814A  | Number of supported touch points (1–10)            |
| STATUS     | 0x814E  | Buffer status + touch count                        |
| TOUCH_DATA | 0x814F  | Touch point data (8 bytes per point)               |
| CONFIG_CHKSUM | 0x80FF | Configuration checksum                           |
| CONFIG_FRESH  | 0x8100 | Write 1 to commit configuration                  |

---

## Multi-Touch Protocol

### FT6236 Touch Data Structure

Each touch point occupies 6 bytes. For N points, read from register `0x02` (N+1 bytes including the TD_STATUS byte):

```
Byte 0:  TD_STATUS    [7:0]  = number of touch points (0–2)
Byte 1:  P1_XH        [7:6]  = event flag (0=down,1=up,2=contact,3=reserved)
                      [3:0]  = X[11:8]
Byte 2:  P1_XL        [7:0]  = X[7:0]
Byte 3:  P1_YH        [7:4]  = Touch ID (0–9)
                      [3:0]  = Y[11:8]
Byte 4:  P1_YL        [7:0]  = Y[7:0]
Byte 5:  P1_WEIGHT    [7:0]  = Touch pressure (0–255)
Byte 6:  P1_MISC      [7:4]  = Touch area size
```

X coordinate = `((P1_XH & 0x0F) << 8) | P1_XL`  
Y coordinate = `((P1_YH & 0x0F) << 8) | P1_YL`  
Event flag   = `(P1_XH >> 6) & 0x03`

### Goodix GT911 Touch Data Structure

Status register `0x814E`:

```
Bit 7:   Buffer ready (1 = new data available)
Bit 6:   Large detect
Bit 4:   Have key
Bits 3:0: Number of valid touch points
```

After reading, host must **clear bit 7** of `0x814E` by writing `0x00` to acknowledge.

Each touch record at `0x814F + (i * 8)`:

```
Byte 0:  Track ID      [7:0]
Byte 1:  X_LOW         [7:0]
Byte 2:  X_HIGH        [7:0]   → X = (X_HIGH << 8) | X_LOW
Byte 3:  Y_LOW         [7:0]
Byte 4:  Y_HIGH        [7:0]   → Y = (Y_HIGH << 8) | Y_LOW
Byte 5:  Size_LOW      [7:0]
Byte 6:  Size_HIGH     [7:0]
Byte 7:  Reserved
```

---

## Programming in C/C++

### Platform Abstraction Layer

```c
/* i2c_hal.h — platform-independent I2C HAL */
#ifndef I2C_HAL_H
#define I2C_HAL_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    I2C_OK    = 0,
    I2C_ERROR = 1,
    I2C_BUSY  = 2,
    I2C_TIMEOUT = 3
} i2c_status_t;

/**
 * Write bytes to a device register.
 * @param dev_addr  7-bit I2C device address
 * @param reg_addr  Register address (8-bit or 16-bit depending on device)
 * @param data      Pointer to data to write
 * @param len       Number of bytes
 */
i2c_status_t i2c_write_reg(uint8_t dev_addr, uint16_t reg_addr,
                            const uint8_t *data, uint16_t len);

/**
 * Read bytes from a device register.
 * @param dev_addr  7-bit I2C device address
 * @param reg_addr  Register address
 * @param data      Buffer to store received data
 * @param len       Number of bytes to read
 */
i2c_status_t i2c_read_reg(uint8_t dev_addr, uint16_t reg_addr,
                           uint8_t *data, uint16_t len);

void delay_ms(uint32_t ms);

#endif /* I2C_HAL_H */
```

---

### FT6236 Driver in C

```c
/* ft6236.h */
#ifndef FT6236_H
#define FT6236_H

#include <stdint.h>
#include <stdbool.h>
#include "i2c_hal.h"

#define FT6236_I2C_ADDR         0x38

/* Core registers */
#define FT6236_REG_DEV_MODE     0x00
#define FT6236_REG_GEST_ID      0x01
#define FT6236_REG_TD_STATUS    0x02
#define FT6236_REG_P1_XH        0x03
#define FT6236_REG_TH_GROUP     0x80
#define FT6236_REG_CTRL         0x86
#define FT6236_REG_PERIOD_ACTIVE 0x88
#define FT6236_REG_CHIP_ID      0xA3
#define FT6236_REG_FIRMID       0xA6

/* Gesture IDs */
#define FT6236_GEST_NONE         0x00
#define FT6236_GEST_MOVE_UP      0x10
#define FT6236_GEST_MOVE_LEFT    0x14
#define FT6236_GEST_MOVE_DOWN    0x18
#define FT6236_GEST_MOVE_RIGHT   0x1C
#define FT6236_GEST_ZOOM_IN      0x48
#define FT6236_GEST_ZOOM_OUT     0x49

/* Event flags */
#define FT6236_EVENT_PRESS_DOWN  0x00
#define FT6236_EVENT_LIFT_UP     0x01
#define FT6236_EVENT_CONTACT     0x02

#define FT6236_MAX_TOUCH_POINTS  2

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  id;
    uint8_t  event;     /* press/lift/contact */
    uint8_t  weight;    /* touch pressure */
    uint8_t  area;      /* touch area size */
    bool     valid;
} ft6236_touch_point_t;

typedef struct {
    uint8_t              num_touches;
    uint8_t              gesture;
    ft6236_touch_point_t points[FT6236_MAX_TOUCH_POINTS];
} ft6236_touch_data_t;

typedef struct {
    uint8_t chip_id;
    uint8_t firmware_version;
    uint8_t threshold;      /* touch detection threshold */
    uint8_t report_rate_ms; /* active mode report rate   */
} ft6236_config_t;

/* Driver API */
bool            ft6236_init(ft6236_config_t *config);
bool            ft6236_read_touch(ft6236_touch_data_t *data);
bool            ft6236_set_threshold(uint8_t threshold);
bool            ft6236_set_report_rate(uint8_t rate_ms);
bool            ft6236_get_firmware_version(uint8_t *version);
uint8_t         ft6236_get_chip_id(void);

#endif /* FT6236_H */
```

```c
/* ft6236.c */
#include "ft6236.h"
#include <string.h>

bool ft6236_init(ft6236_config_t *config)
{
    uint8_t chip_id = 0;

    /* Read and verify chip ID */
    if (i2c_read_reg(FT6236_I2C_ADDR, FT6236_REG_CHIP_ID, &chip_id, 1) != I2C_OK) {
        return false;
    }
    if (config) {
        config->chip_id = chip_id;
    }

    /* Read firmware version */
    if (config) {
        i2c_read_reg(FT6236_I2C_ADDR, FT6236_REG_FIRMID,
                     &config->firmware_version, 1);
    }

    /* Set touch threshold (lower = more sensitive) */
    uint8_t threshold = config ? config->threshold : 22;
    if (i2c_write_reg(FT6236_I2C_ADDR, FT6236_REG_TH_GROUP,
                      &threshold, 1) != I2C_OK) {
        return false;
    }

    /* Set active report rate */
    uint8_t rate = config ? config->report_rate_ms : 14; /* ~70 Hz */
    i2c_write_reg(FT6236_I2C_ADDR, FT6236_REG_PERIOD_ACTIVE, &rate, 1);

    /* Enter normal operating mode */
    uint8_t mode = 0x00;
    i2c_write_reg(FT6236_I2C_ADDR, FT6236_REG_DEV_MODE, &mode, 1);

    return true;
}

bool ft6236_read_touch(ft6236_touch_data_t *data)
{
    /*
     * Read 15 bytes starting at GEST_ID (0x01):
     *  [0]      GEST_ID
     *  [1]      TD_STATUS (touch count)
     *  [2..7]   Point 1 data (XH, XL, YH, YL, WEIGHT, MISC)
     *  [8..13]  Point 2 data
     */
    uint8_t buf[14];
    if (i2c_read_reg(FT6236_I2C_ADDR, FT6236_REG_GEST_ID,
                     buf, sizeof(buf)) != I2C_OK) {
        return false;
    }

    memset(data, 0, sizeof(*data));

    data->gesture    = buf[0];
    data->num_touches = buf[1] & 0x0F;

    if (data->num_touches > FT6236_MAX_TOUCH_POINTS) {
        data->num_touches = FT6236_MAX_TOUCH_POINTS;
    }

    for (uint8_t i = 0; i < data->num_touches; i++) {
        const uint8_t *p = &buf[2 + (i * 6)];
        ft6236_touch_point_t *pt = &data->points[i];

        pt->event  = (p[0] >> 6) & 0x03;
        pt->x      = (uint16_t)((p[0] & 0x0F) << 8) | p[1];
        pt->id     = (p[2] >> 4) & 0x0F;
        pt->y      = (uint16_t)((p[2] & 0x0F) << 8) | p[3];
        pt->weight = p[4];
        pt->area   = (p[5] >> 4) & 0x0F;
        pt->valid  = true;
    }

    return true;
}

bool ft6236_set_threshold(uint8_t threshold)
{
    return i2c_write_reg(FT6236_I2C_ADDR, FT6236_REG_TH_GROUP,
                         &threshold, 1) == I2C_OK;
}

bool ft6236_set_report_rate(uint8_t rate_ms)
{
    return i2c_write_reg(FT6236_I2C_ADDR, FT6236_REG_PERIOD_ACTIVE,
                         &rate_ms, 1) == I2C_OK;
}

uint8_t ft6236_get_chip_id(void)
{
    uint8_t id = 0;
    i2c_read_reg(FT6236_I2C_ADDR, FT6236_REG_CHIP_ID, &id, 1);
    return id;
}
```

---

### Goodix GT911 Driver in C/C++

```cpp
/* gt911.hpp */
#pragma once
#include <cstdint>
#include <array>
#include <optional>
#include "i2c_hal.h"

namespace gt911 {

constexpr uint8_t  ADDR_PRIMARY   = 0x5D;
constexpr uint8_t  ADDR_SECONDARY = 0x14;
constexpr uint16_t REG_PRODUCT_ID = 0x8140;
constexpr uint16_t REG_FW_VERSION = 0x8144;
constexpr uint16_t REG_X_RESOLUTION = 0x8146;
constexpr uint16_t REG_Y_RESOLUTION = 0x8148;
constexpr uint16_t REG_STATUS     = 0x814E;
constexpr uint16_t REG_TOUCH_DATA = 0x814F;
constexpr uint16_t REG_CONFIG     = 0x8047;
constexpr uint16_t REG_CONFIG_CHKSUM = 0x80FF;
constexpr uint16_t REG_CONFIG_FRESH  = 0x8100;
constexpr uint8_t  MAX_TOUCH_POINTS  = 10;
constexpr uint8_t  TOUCH_RECORD_SIZE = 8;

/* Status register bits */
constexpr uint8_t STATUS_BUFFER_READY = 0x80;
constexpr uint8_t STATUS_LARGE_DETECT = 0x40;
constexpr uint8_t STATUS_HAVE_KEY     = 0x10;
constexpr uint8_t STATUS_TOUCH_COUNT  = 0x0F;

struct TouchPoint {
    uint8_t  id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
    bool     valid;
};

struct TouchData {
    uint8_t    num_touches;
    bool       large_touch;
    std::array<TouchPoint, MAX_TOUCH_POINTS> points;
};

struct DeviceInfo {
    char     product_id[5];   /* e.g. "911\0" */
    uint16_t fw_version;
    uint16_t x_resolution;
    uint16_t y_resolution;
    uint8_t  touch_count;
};

class GT911 {
public:
    explicit GT911(uint8_t i2c_addr = ADDR_PRIMARY)
        : addr_(i2c_addr) {}

    /**
     * Initialize the GT911.
     * The INT and RST pins must be driven before calling this to select
     * the desired I2C address (see address initialization procedure).
     */
    bool init(uint8_t rst_pin, uint8_t int_pin);

    /**
     * Read touch data. Returns nullopt if no new data or on error.
     * Automatically clears the buffer-ready flag after reading.
     */
    std::optional<TouchData> read_touch();

    DeviceInfo get_device_info();

    /* Configuration */
    bool set_resolution(uint16_t width, uint16_t height);
    bool set_touch_count(uint8_t count);
    bool write_config(const uint8_t *cfg, uint16_t len);

    /* Power management */
    bool enter_sleep();
    bool wake_up();

private:
    uint8_t addr_;

    i2c_status_t read16(uint16_t reg, uint8_t *buf, uint16_t len);
    i2c_status_t write16(uint16_t reg, const uint8_t *buf, uint16_t len);
    uint8_t compute_config_checksum(const uint8_t *cfg, uint16_t len);
};

} // namespace gt911
```

```cpp
/* gt911.cpp */
#include "gt911.hpp"
#include <cstring>

namespace gt911 {

/*
 * GT911 Address Selection via RST/INT sequence:
 *
 * To select 0x5D: Drive INT low, then pulse RST low→high
 * To select 0x14: Drive INT high, then pulse RST low→high
 *
 * This must happen during chip power-on/reset.
 */
bool GT911::init(uint8_t rst_pin, uint8_t int_pin)
{
    /* Platform-specific GPIO setup omitted; implement for your HAL */
    (void)rst_pin; (void)int_pin;

    delay_ms(200); /* Allow controller to stabilize after reset */

    /* Verify product ID */
    uint8_t pid[4] = {};
    if (read16(REG_PRODUCT_ID, pid, 4) != I2C_OK) {
        return false;
    }
    /* Product ID for GT911 should be "911" */
    return (pid[0] == '9' && pid[1] == '1' && pid[2] == '1');
}

std::optional<TouchData> GT911::read_touch()
{
    uint8_t status = 0;
    if (read16(REG_STATUS, &status, 1) != I2C_OK) {
        return std::nullopt;
    }

    /* Check if buffer is ready */
    if (!(status & STATUS_BUFFER_READY)) {
        return std::nullopt;
    }

    TouchData data{};
    data.num_touches = status & STATUS_TOUCH_COUNT;
    data.large_touch = (status & STATUS_LARGE_DETECT) != 0;

    if (data.num_touches > MAX_TOUCH_POINTS) {
        data.num_touches = MAX_TOUCH_POINTS;
    }

    if (data.num_touches > 0) {
        /* Read all touch records in one I2C transaction */
        uint8_t raw[MAX_TOUCH_POINTS * TOUCH_RECORD_SIZE] = {};
        uint16_t bytes_to_read = data.num_touches * TOUCH_RECORD_SIZE;

        if (read16(REG_TOUCH_DATA, raw, bytes_to_read) != I2C_OK) {
            return std::nullopt;
        }

        for (uint8_t i = 0; i < data.num_touches; i++) {
            const uint8_t *rec = &raw[i * TOUCH_RECORD_SIZE];
            TouchPoint &pt = data.points[i];

            pt.id    = rec[0];
            pt.x     = (uint16_t)(rec[1]) | ((uint16_t)(rec[2]) << 8);
            pt.y     = (uint16_t)(rec[3]) | ((uint16_t)(rec[4]) << 8);
            pt.size  = (uint16_t)(rec[5]) | ((uint16_t)(rec[6]) << 8);
            pt.valid = true;
        }
    }

    /* CRITICAL: Clear buffer-ready flag by writing 0x00 to status register */
    uint8_t clear = 0x00;
    write16(REG_STATUS, &clear, 1);

    return data;
}

DeviceInfo GT911::get_device_info()
{
    DeviceInfo info{};
    uint8_t buf[6] = {};

    read16(REG_PRODUCT_ID, reinterpret_cast<uint8_t*>(info.product_id), 4);
    info.product_id[4] = '\0';

    read16(REG_FW_VERSION, buf, 2);
    info.fw_version = (uint16_t)(buf[0]) | ((uint16_t)(buf[1]) << 8);

    read16(REG_X_RESOLUTION, buf, 2);
    info.x_resolution = (uint16_t)(buf[0]) | ((uint16_t)(buf[1]) << 8);

    read16(REG_Y_RESOLUTION, buf, 2);
    info.y_resolution = (uint16_t)(buf[0]) | ((uint16_t)(buf[1]) << 8);

    return info;
}

uint8_t GT911::compute_config_checksum(const uint8_t *cfg, uint16_t len)
{
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += cfg[i];
    }
    return (~sum + 1); /* Two's complement negation */
}

bool GT911::write_config(const uint8_t *cfg, uint16_t len)
{
    if (write16(REG_CONFIG, cfg, len) != I2C_OK) {
        return false;
    }

    uint8_t chk = compute_config_checksum(cfg, len);
    if (write16(REG_CONFIG_CHKSUM, &chk, 1) != I2C_OK) {
        return false;
    }

    uint8_t fresh = 0x01;
    return write16(REG_CONFIG_FRESH, &fresh, 1) == I2C_OK;
}

i2c_status_t GT911::read16(uint16_t reg, uint8_t *buf, uint16_t len)
{
    /*
     * GT911 uses 16-bit register addressing.
     * Send: START → ADDR+W → REG_HIGH → REG_LOW
     *       → REPEATED START → ADDR+R → DATA → STOP
     */
    uint8_t reg_bytes[2] = {
        static_cast<uint8_t>(reg >> 8),
        static_cast<uint8_t>(reg & 0xFF)
    };
    /* This example uses a 16-bit register wrapper; implement in your HAL */
    return i2c_read_reg(addr_, reg, buf, len);
}

i2c_status_t GT911::write16(uint16_t reg, const uint8_t *buf, uint16_t len)
{
    return i2c_write_reg(addr_, reg, buf, len);
}

} // namespace gt911
```

---

### Interrupt-Driven Touch Reading in C

```c
/* touch_manager.c — Interrupt-driven touch with FT6236 */
#include "ft6236.h"
#include <stdbool.h>

/* Application-level callback type */
typedef void (*touch_event_cb_t)(const ft6236_touch_data_t *data);

static volatile bool touch_int_pending = false;
static touch_event_cb_t touch_callback  = NULL;

/* Called from GPIO interrupt handler (ISR context) */
void TOUCH_INT_IRQHandler(void)
{
    touch_int_pending = true;
    /* Clear EXTI pending flag — platform specific */
}

void touch_manager_init(touch_event_cb_t callback)
{
    touch_callback = callback;

    ft6236_config_t config = {
        .threshold      = 22,   /* Default sensitivity */
        .report_rate_ms = 10,   /* ~100 Hz */
    };
    ft6236_init(&config);

    /* Configure INT pin as falling-edge interrupt — platform specific */
    /* gpio_set_interrupt(TOUCH_INT_PIN, GPIO_FALLING_EDGE); */
}

/* Call this from main loop or RTOS task */
void touch_manager_process(void)
{
    if (!touch_int_pending) {
        return;
    }
    touch_int_pending = false;

    ft6236_touch_data_t data;
    if (ft6236_read_touch(&data) && data.num_touches > 0) {
        if (touch_callback) {
            touch_callback(&data);
        }
    }
}

/* Example callback */
static void on_touch_event(const ft6236_touch_data_t *data)
{
    for (uint8_t i = 0; i < data->num_touches; i++) {
        const ft6236_touch_point_t *pt = &data->points[i];
        /* Handle touch: pt->x, pt->y, pt->event */
        (void)pt;
    }
}
```

---

## Programming in Rust

### Dependencies (Cargo.toml)

```toml
[dependencies]
embedded-hal = "1.0"
# For async: embedded-hal-async = "1.0"
# For std/Linux development: linux-embedded-hal = "0.4"
```

---

### FT6236 Driver in Rust

```rust
// ft6236.rs — FT6236 touch controller driver using embedded-hal

use embedded_hal::i2c::I2c;

pub const FT6236_ADDR: u8 = 0x38;

/// Register addresses
mod reg {
    pub const DEV_MODE:      u8 = 0x00;
    pub const GEST_ID:       u8 = 0x01;
    pub const TD_STATUS:     u8 = 0x02;
    pub const P1_XH:         u8 = 0x03;
    pub const TH_GROUP:      u8 = 0x80;
    pub const CTRL:          u8 = 0x86;
    pub const PERIOD_ACTIVE: u8 = 0x88;
    pub const CHIP_ID:       u8 = 0xA3;
    pub const FIRMID:        u8 = 0xA6;
}

/// Gesture IDs reported by the FT6236
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Gesture {
    None,
    SwipeUp,
    SwipeLeft,
    SwipeDown,
    SwipeRight,
    ZoomIn,
    ZoomOut,
    Unknown(u8),
}

impl From<u8> for Gesture {
    fn from(byte: u8) -> Self {
        match byte {
            0x00 => Gesture::None,
            0x10 => Gesture::SwipeUp,
            0x14 => Gesture::SwipeLeft,
            0x18 => Gesture::SwipeDown,
            0x1C => Gesture::SwipeRight,
            0x48 => Gesture::ZoomIn,
            0x49 => Gesture::ZoomOut,
            other => Gesture::Unknown(other),
        }
    }
}

/// Touch event type
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TouchEvent {
    PressDown,
    LiftUp,
    Contact,
    Unknown,
}

impl From<u8> for TouchEvent {
    fn from(bits: u8) -> Self {
        match bits & 0x03 {
            0 => TouchEvent::PressDown,
            1 => TouchEvent::LiftUp,
            2 => TouchEvent::Contact,
            _ => TouchEvent::Unknown,
        }
    }
}

/// A single touch contact point
#[derive(Debug, Clone, Copy)]
pub struct TouchPoint {
    pub id:     u8,
    pub x:      u16,
    pub y:      u16,
    pub event:  TouchEvent,
    pub weight: u8,
    pub area:   u8,
}

/// Complete touch data frame
#[derive(Debug)]
pub struct TouchData {
    pub gesture:      Gesture,
    pub num_touches:  u8,
    pub points:       [Option<TouchPoint>; 2],
}

/// Driver configuration
pub struct Ft6236Config {
    pub threshold:      u8, // Touch sensitivity threshold (default: 22)
    pub report_rate_ms: u8, // Active mode report rate in ms (default: 14 → ~70 Hz)
}

impl Default for Ft6236Config {
    fn default() -> Self {
        Self {
            threshold:      22,
            report_rate_ms: 14,
        }
    }
}

/// FT6236 driver
pub struct Ft6236<I2C> {
    i2c:    I2C,
    addr:   u8,
}

impl<I2C: I2c> Ft6236<I2C> {
    /// Create a new driver instance bound to an I2C bus
    pub fn new(i2c: I2C) -> Self {
        Self { i2c, addr: FT6236_ADDR }
    }

    /// Initialize the controller with the given configuration
    pub fn init(&mut self, config: &Ft6236Config) -> Result<u8, I2C::Error> {
        /* Read chip ID */
        let chip_id = self.read_reg(reg::CHIP_ID)?;

        /* Set touch detection threshold */
        self.write_reg(reg::TH_GROUP, config.threshold)?;

        /* Set active report rate */
        self.write_reg(reg::PERIOD_ACTIVE, config.report_rate_ms)?;

        /* Enter normal operating mode */
        self.write_reg(reg::DEV_MODE, 0x00)?;

        Ok(chip_id)
    }

    /// Read current touch data. Call this when INT goes low, or poll.
    pub fn read_touch(&mut self) -> Result<TouchData, I2C::Error> {
        /*
         * Burst-read 14 bytes starting at GEST_ID register.
         * Layout:
         *   buf[0]  = GEST_ID
         *   buf[1]  = TD_STATUS (touch count)
         *   buf[2..7]  = Point 1 (XH, XL, YH, YL, WEIGHT, MISC)
         *   buf[8..13] = Point 2
         */
        let mut buf = [0u8; 14];
        self.i2c.write_read(
            self.addr,
            &[reg::GEST_ID],
            &mut buf,
        )?;

        let gesture     = Gesture::from(buf[0]);
        let num_touches = (buf[1] & 0x0F).min(2);
        let mut points  = [None; 2];

        for i in 0..num_touches as usize {
            let offset = 2 + i * 6;
            let raw = &buf[offset..offset + 6];

            points[i] = Some(TouchPoint {
                event:  TouchEvent::from(raw[0] >> 6),
                x:      (((raw[0] & 0x0F) as u16) << 8) | raw[1] as u16,
                id:     (raw[2] >> 4) & 0x0F,
                y:      (((raw[2] & 0x0F) as u16) << 8) | raw[3] as u16,
                weight: raw[4],
                area:   (raw[5] >> 4) & 0x0F,
            });
        }

        Ok(TouchData { gesture, num_touches, points })
    }

    /// Adjust touch sensitivity. Lower = more sensitive (range: 0–255).
    pub fn set_threshold(&mut self, threshold: u8) -> Result<(), I2C::Error> {
        self.write_reg(reg::TH_GROUP, threshold)
    }

    /// Set active mode report interval in milliseconds.
    pub fn set_report_rate(&mut self, rate_ms: u8) -> Result<(), I2C::Error> {
        self.write_reg(reg::PERIOD_ACTIVE, rate_ms)
    }

    /// Enter low-power monitor mode.
    pub fn enter_monitor_mode(&mut self) -> Result<(), I2C::Error> {
        self.write_reg(reg::CTRL, 0x01)
    }

    /// Return the underlying I2C bus (for bus sharing).
    pub fn release(self) -> I2C {
        self.i2c
    }

    /* ---------- private helpers ---------- */

    fn read_reg(&mut self, reg: u8) -> Result<u8, I2C::Error> {
        let mut buf = [0u8; 1];
        self.i2c.write_read(self.addr, &[reg], &mut buf)?;
        Ok(buf[0])
    }

    fn write_reg(&mut self, reg: u8, value: u8) -> Result<(), I2C::Error> {
        self.i2c.write(self.addr, &[reg, value])
    }
}
```

---

### GT911 Driver in Rust

```rust
// gt911.rs — Goodix GT911 multi-touch driver (up to 10 points)

use embedded_hal::i2c::I2c;

pub const GT911_ADDR_PRIMARY:   u8 = 0x5D;
pub const GT911_ADDR_SECONDARY: u8 = 0x14;

mod reg {
    pub const PRODUCT_ID:     u16 = 0x8140;
    pub const FW_VERSION:     u16 = 0x8144;
    pub const X_RESOLUTION:   u16 = 0x8146;
    pub const Y_RESOLUTION:   u16 = 0x8148;
    pub const STATUS:         u16 = 0x814E;
    pub const TOUCH_DATA:     u16 = 0x814F;
    pub const CONFIG:         u16 = 0x8047;
    pub const CONFIG_CHKSUM:  u16 = 0x80FF;
    pub const CONFIG_FRESH:   u16 = 0x8100;
}

const MAX_POINTS:    usize = 10;
const RECORD_BYTES:  usize = 8;
const STATUS_READY:  u8    = 0x80;

#[derive(Debug, Clone, Copy)]
pub struct TouchPoint {
    pub id:   u8,
    pub x:    u16,
    pub y:    u16,
    pub size: u16,
}

#[derive(Debug)]
pub struct TouchData {
    pub num_touches: u8,
    pub large_touch: bool,
    pub points:      heapless::Vec<TouchPoint, MAX_POINTS>,
}

#[derive(Debug)]
pub struct DeviceInfo {
    pub product_id:   [u8; 4],
    pub fw_version:   u16,
    pub x_resolution: u16,
    pub y_resolution: u16,
}

pub struct Gt911<I2C> {
    i2c:  I2C,
    addr: u8,
}

impl<I2C: I2c> Gt911<I2C> {
    pub fn new(i2c: I2C, addr: u8) -> Self {
        Self { i2c, addr }
    }

    /// Initialize and verify the device. Returns the product ID string.
    pub fn init(&mut self) -> Result<[u8; 4], I2C::Error> {
        let mut pid = [0u8; 4];
        self.read16(reg::PRODUCT_ID, &mut pid)?;
        Ok(pid)
    }

    /// Poll for touch data. Returns None if no new data.
    /// Clears the buffer-ready flag automatically on success.
    pub fn read_touch(&mut self) -> Result<Option<TouchData>, I2C::Error> {
        let mut status_buf = [0u8; 1];
        self.read16(reg::STATUS, &mut status_buf)?;
        let status = status_buf[0];

        if status & STATUS_READY == 0 {
            return Ok(None);
        }

        let num_touches = (status & 0x0F).min(MAX_POINTS as u8);
        let large_touch = (status & 0x40) != 0;
        let mut points  = heapless::Vec::new();

        if num_touches > 0 {
            let byte_count = num_touches as usize * RECORD_BYTES;
            let mut raw = [0u8; MAX_POINTS * RECORD_BYTES];
            self.read16(reg::TOUCH_DATA, &mut raw[..byte_count])?;

            for i in 0..num_touches as usize {
                let off = i * RECORD_BYTES;
                let rec = &raw[off..off + RECORD_BYTES];
                let _ = points.push(TouchPoint {
                    id:   rec[0],
                    x:    (rec[1] as u16) | ((rec[2] as u16) << 8),
                    y:    (rec[3] as u16) | ((rec[4] as u16) << 8),
                    size: (rec[5] as u16) | ((rec[6] as u16) << 8),
                });
            }
        }

        /* Clear the buffer-ready flag */
        self.write16(reg::STATUS, &[0x00])?;

        Ok(Some(TouchData { num_touches, large_touch, points }))
    }

    /// Read controller identification info.
    pub fn device_info(&mut self) -> Result<DeviceInfo, I2C::Error> {
        let mut info = DeviceInfo {
            product_id:   [0; 4],
            fw_version:   0,
            x_resolution: 0,
            y_resolution: 0,
        };

        self.read16(reg::PRODUCT_ID, &mut info.product_id)?;

        let mut buf2 = [0u8; 2];
        self.read16(reg::FW_VERSION, &mut buf2)?;
        info.fw_version = u16::from_le_bytes(buf2);

        self.read16(reg::X_RESOLUTION, &mut buf2)?;
        info.x_resolution = u16::from_le_bytes(buf2);

        self.read16(reg::Y_RESOLUTION, &mut buf2)?;
        info.y_resolution = u16::from_le_bytes(buf2);

        Ok(info)
    }

    /// Write a full configuration block (186 bytes) and commit it.
    pub fn write_config(&mut self, config: &[u8]) -> Result<(), I2C::Error> {
        self.write16(reg::CONFIG, config)?;

        let chksum = Self::config_checksum(config);
        self.write16(reg::CONFIG_CHKSUM, &[chksum])?;
        self.write16(reg::CONFIG_FRESH, &[0x01])?;
        Ok(())
    }

    pub fn release(self) -> I2C {
        self.i2c
    }

    /* ---------- helpers ---------- */

    fn read16(&mut self, reg: u16, buf: &mut [u8]) -> Result<(), I2C::Error> {
        let addr_bytes = reg.to_be_bytes();
        self.i2c.write_read(self.addr, &addr_bytes, buf)
    }

    fn write16(&mut self, reg: u16, data: &[u8]) -> Result<(), I2C::Error> {
        /* Build [REG_HIGH, REG_LOW, data...] in a fixed-size buffer */
        let reg_bytes = reg.to_be_bytes();
        let mut frame = [0u8; 2 + 200]; // enough for any single write
        frame[0] = reg_bytes[0];
        frame[1] = reg_bytes[1];
        frame[2..2 + data.len()].copy_from_slice(data);
        self.i2c.write(self.addr, &frame[..2 + data.len()])
    }

    fn config_checksum(cfg: &[u8]) -> u8 {
        let sum: u8 = cfg.iter().fold(0u8, |acc, &b| acc.wrapping_add(b));
        (!sum).wrapping_add(1) // Two's complement negation
    }
}
```

---

### Async Touch Driver in Rust (embassy / embedded-hal-async)

```rust
// ft6236_async.rs — Async FT6236 driver using embedded-hal-async

use embedded_hal_async::i2c::I2c;

pub struct Ft6236Async<I2C> {
    i2c:  I2C,
    addr: u8,
}

impl<I2C: I2c> Ft6236Async<I2C> {
    pub fn new(i2c: I2C) -> Self {
        Self { i2c, addr: 0x38 }
    }

    pub async fn init(&mut self, threshold: u8) -> Result<u8, I2C::Error> {
        let chip_id = self.read_reg(0xA3).await?;
        self.write_reg(0x80, threshold).await?;
        self.write_reg(0x88, 14).await?; /* 14 ms → ~70 Hz */
        self.write_reg(0x00, 0x00).await?;
        Ok(chip_id)
    }

    pub async fn read_touch(&mut self) -> Result<(u8, u16, u16), I2C::Error> {
        let mut buf = [0u8; 7];
        self.i2c.write_read(self.addr, &[0x02], &mut buf).await?;

        let count = buf[0] & 0x0F;
        let x = (((buf[1] & 0x0F) as u16) << 8) | buf[2] as u16;
        let y = (((buf[3] & 0x0F) as u16) << 8) | buf[4] as u16;
        Ok((count, x, y))
    }

    async fn read_reg(&mut self, reg: u8) -> Result<u8, I2C::Error> {
        let mut buf = [0u8];
        self.i2c.write_read(self.addr, &[reg], &mut buf).await?;
        Ok(buf[0])
    }

    async fn write_reg(&mut self, reg: u8, val: u8) -> Result<(), I2C::Error> {
        self.i2c.write(self.addr, &[reg, val]).await
    }
}
```

---

## Configuration and Calibration

### FT6236 Sensitivity Tuning

The `TH_GROUP` register controls touch detection sensitivity:

| Value | Sensitivity      | Use Case                          |
|-------|------------------|-----------------------------------|
| 10–15 | Very high         | Gloved or stylus input            |
| 16–30 | Normal (default: 22) | Bare finger, general purpose   |
| 31–50 | Low               | Noisy environments, reduce false triggers |
| 51+   | Very low          | High-vibration industrial panels  |

```c
/* Increase sensitivity for glove use */
ft6236_set_threshold(12);

/* Reduce false triggers in noisy environment */
ft6236_set_threshold(40);
```

### GT911 Configuration Block

The GT911's 186-byte configuration block covers:

- Touch resolution (X/Y)
- Number of tracked points
- Noise rejection filters
- Coordinate flip/swap (for mounted orientations)
- Gesture detection enable/disable
- Refresh rate

```c
/*
 * Example: flip X-axis for 180° rotated display mount.
 * Byte offset 0x804D[0] = X/Y swap
 * Byte offset 0x804D[3] = X mirror
 * After modifying, recompute checksum and write CONFIG_FRESH = 1.
 */
uint8_t config[186];
/* ... read existing config from device ... */
config[0x06] |= 0x01;  /* Byte 6 in config block: enable X-mirror */
gt911_write_config(&gt911_dev, config, sizeof(config));
```

### Display Coordinate Mapping

Touch controllers report coordinates in their own native space, which may differ from display pixel coordinates:

```c
/**
 * Map raw touch coordinates to screen coordinates.
 * Handles rotation and axis flip for panel mounting.
 */
typedef struct {
    uint16_t touch_width;   /* Controller native width  */
    uint16_t touch_height;  /* Controller native height */
    uint16_t screen_width;  /* Display pixel width      */
    uint16_t screen_height; /* Display pixel height     */
    bool     swap_xy;
    bool     flip_x;
    bool     flip_y;
} touch_transform_t;

void touch_transform(const touch_transform_t *t,
                     uint16_t raw_x, uint16_t raw_y,
                     uint16_t *screen_x, uint16_t *screen_y)
{
    uint16_t x = raw_x;
    uint16_t y = raw_y;

    if (t->swap_xy) {
        uint16_t tmp = x; x = y; y = tmp;
    }
    if (t->flip_x) {
        x = t->touch_width - 1 - x;
    }
    if (t->flip_y) {
        y = t->touch_height - 1 - y;
    }

    /* Scale to screen resolution */
    *screen_x = (uint32_t)x * t->screen_width  / t->touch_width;
    *screen_y = (uint32_t)y * t->screen_height / t->touch_height;
}
```

---

## Interrupt Handling

The INT pin is open-drain and active-low. It is asserted by the controller when a touch event occurs and new data is ready in the registers. Best practice is to use a falling-edge interrupt to trigger a read, rather than polling.

**Hardware connection:**
```
Touch Controller INT ──── 10kΩ pull-up to VCC ──── MCU GPIO (INPUT, EXTI)
```

**Debounce consideration:** Hardware-based INT from touch controllers is already debounced internally. Software debouncing on the INT line is generally not required.

**ARM Cortex-M NVIC example (pseudo-code):**

```c
void EXTI_TOUCH_IRQHandler(void)
{
    if (EXTI->PR & TOUCH_INT_PIN_MASK) {
        EXTI->PR = TOUCH_INT_PIN_MASK; /* Clear pending */
        touch_flag = 1;               /* Signal main loop */
    }
}
```

**RTOS semaphore pattern:**

```c
/* ISR */
void TOUCH_INT_IRQHandler(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(touch_semaphore, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* Touch task */
void touch_task(void *param)
{
    for (;;) {
        xSemaphoreTake(touch_semaphore, portMAX_DELAY);
        ft6236_touch_data_t data;
        if (ft6236_read_touch(&data)) {
            /* Dispatch touch events */
        }
    }
}
```

---

## Gesture Recognition

### FT6236 Built-in Gestures

The FT6236 reports gestures in the `GEST_ID` register automatically:

```c
void handle_gesture(uint8_t gesture_id)
{
    switch (gesture_id) {
        case FT6236_GEST_MOVE_UP:    ui_scroll(SCROLL_UP);    break;
        case FT6236_GEST_MOVE_DOWN:  ui_scroll(SCROLL_DOWN);  break;
        case FT6236_GEST_MOVE_LEFT:  ui_navigate(NAV_FORWARD); break;
        case FT6236_GEST_MOVE_RIGHT: ui_navigate(NAV_BACK);    break;
        case FT6236_GEST_ZOOM_IN:    ui_zoom(1);              break;
        case FT6236_GEST_ZOOM_OUT:   ui_zoom(-1);             break;
        default: break;
    }
}
```

### Software Pinch-to-Zoom from Two-Point Data

For controllers that report raw multi-touch data without gesture inference:

```c
#include <math.h>

typedef struct { float x, y; } vec2_t;

static float point_distance(vec2_t a, vec2_t b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

/* Pinch-to-zoom state */
static float  pinch_start_distance = 0.0f;
static float  current_scale        = 1.0f;

void process_pinch(const ft6236_touch_data_t *data)
{
    if (data->num_touches != 2) {
        pinch_start_distance = 0.0f;
        return;
    }

    vec2_t p0 = { data->points[0].x, data->points[0].y };
    vec2_t p1 = { data->points[1].x, data->points[1].y };
    float dist = point_distance(p0, p1);

    if (pinch_start_distance == 0.0f) {
        pinch_start_distance = dist;
        return;
    }

    float scale_delta = dist / pinch_start_distance;
    pinch_start_distance = dist;
    current_scale *= scale_delta;

    ui_apply_scale(current_scale);
}
```

---

## Troubleshooting

| Symptom | Likely Cause | Resolution |
|---------|-------------|------------|
| No I2C ACK | Wrong address, missing pull-ups, wrong VCC level | Verify address, add 4.7kΩ pull-ups, check 1.8V vs 3.3V |
| Reads all 0xFF | Controller in reset state | Toggle RST pin, add power-on delay (200ms) |
| Coordinates always (0,0) | TD_STATUS = 0 despite INT firing | Verify INT polarity; ensure status register is read correctly |
| Ghost touches / jitter | Threshold too low | Increase `TH_GROUP` value |
| Missing touches | Threshold too high | Decrease `TH_GROUP` value |
| GT911 no ACK | Address mismatch | Verify INT/RST initialization sequence for 0x5D vs 0x14 |
| GT911 stale data | Forgetting to clear status register | Always write 0x00 to `0x814E` after every read |
| Coordinates mirrored | Panel mounted inverted | Apply coordinate transform (flip/swap) in software |
| High I2C error rate | Bus capacitance too high, speed too fast | Reduce to 100 kHz, shorten traces, add bus buffer |

---

## Summary

Touchscreen controllers are a foundational I2C peripheral class in embedded systems. The key takeaways are:

**Protocol fundamentals:** These devices use a simple register-based I2C interface with 8-bit (FT6236) or 16-bit (GT911) register addressing. Multi-touch data is read in a burst from a contiguous register block, making a single I2C transaction sufficient per frame.

**Interrupt-driven design:** Always use the INT pin with a falling-edge interrupt rather than polling. This minimizes CPU load and provides low-latency response. In RTOS environments, use semaphore signalling from the ISR to wake a dedicated touch task.

**Buffer management (GT911):** The GT911 requires the host to explicitly acknowledge receipt of data by clearing the buffer-ready bit (`0x814E = 0x00`). Failing to do this causes the controller to stop generating new interrupts and report stale data.

**Address selection (GT911):** The GT911 determines its I2C address from the state of the INT pin during the RST de-assert sequence. This must be performed correctly at power-on; the address cannot be changed via software afterwards.

**Coordinate transforms:** Raw touch coordinates are in the controller's native space and often require scaling, mirroring, or axis swapping to match the display orientation and resolution.

**Sensitivity configuration:** Both threshold values and report rates should be tuned for the application. High-vibration or noisy environments need higher thresholds; stylus or gloved input may need lower thresholds.

**Rust embedded-hal integration:** The `embedded-hal` I2C trait (`write_read`, `write`) maps directly to touchscreen register operations. Drivers are generic over any `I2c` implementation, enabling use on any MCU platform with a HAL crate. Async variants using `embedded-hal-async` integrate cleanly with Embassy and other async RTOS frameworks.

**Performance:** At 400 kHz I2C, reading 14 bytes from FT6236 (one full multi-touch frame) takes approximately 0.5 ms, well within even the most demanding 200 Hz refresh requirements.

---

*Document covers: FT6236, FT6206, Goodix GT911, CST816S touch controllers. Code examples target bare-metal C/C++ with HAL abstraction and Rust with `embedded-hal 1.0`. Async Rust examples target Embassy.*