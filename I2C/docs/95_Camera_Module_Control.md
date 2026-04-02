# 95. Camera Module Control via I2C

**Architecture & Theory** — separation of I2C control bus vs high-speed image data bus, sensor address schemes, and the three register width variants (8-bit/8-bit, 16-bit/8-bit, 16-bit/16-bit).

**Key Sensor Coverage** — OV5640, IMX219, OV2640, IMX477, AR0234, MT9V032 with their I2C addresses and register formats in a reference table.

**C/C++ Examples** — a portable HAL abstraction layer, 16-bit register read/write/modify, register table engine with delay support, full OV5640 driver (init, exposure, gain, frame rate, streaming), Linux kernel regmap pattern, and retry logic.

**Rust Examples** — generic `embedded-hal` accessor with marker traits, OV5640 and IMX219 drivers, async/embassy variant, and a **typestate pattern** that enforces valid state transitions (standby ↔ streaming) at compile time.

**Summary** — consolidates all concepts including protocol mechanics, initialisation sequencing, live register updates, and robustness strategies.

> **Configuring image sensors and camera modules using I2C control buses**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [Common Camera Sensors and Their I2C Interfaces](#common-camera-sensors-and-their-i2c-interfaces)
4. [I2C Register Map Concepts](#i2c-register-map-concepts)
5. [Initialization and Reset Sequences](#initialization-and-reset-sequences)
6. [Exposure and Gain Control](#exposure-and-gain-control)
7. [Resolution and Format Configuration](#resolution-and-format-configuration)
8. [Frame Rate Control](#frame-rate-control)
9. [White Balance and Color Correction](#white-balance-and-color-correction)
10. [Streaming Start/Stop](#streaming-startstop)
11. [C/C++ Implementation Examples](#cc-implementation-examples)
12. [Rust Implementation Examples](#rust-implementation-examples)
13. [Error Handling Strategies](#error-handling-strategies)
14. [Summary](#summary)

---

## Introduction

Camera modules used in embedded systems — from industrial machine vision to consumer smartphones — rely on a dedicated **I2C control bus** to configure the image sensor, independent of the high-speed data path (MIPI CSI-2, DVP parallel, or LVDS) used for image data. The I2C bus is low-speed (typically 100 kHz–400 kHz, sometimes 1 MHz Fast-mode Plus) and carries only configuration commands: register reads/writes that govern every aspect of sensor behaviour.

This separation of concerns is fundamental:

- **I2C control bus** — configuration, status, identification (slow, bidirectional, few pins)
- **Image data bus** — pixel data, synchronisation signals (fast, unidirectional, many lanes)

Mastering I2C camera control means understanding the sensor's register map, its timing requirements, and the proper sequencing of initialisation, streaming, and reconfiguration commands.

---

## Architecture Overview

```
Host SoC / MCU
┌────────────────────────────────────────┐
│  Application Processor                 │
│                                        │
│  ┌──────────┐    ┌──────────────────┐  │
│  │ I2C      │    │  CSI-2 / DVP     │  │
│  │ Master   │    │  Receiver        │  │
│  └────┬─────┘    └────────┬─────────┘  │
└───────┼───────────────────┼────────────┘
        │ SDA/SCL           │ DATA/CLK Lanes
        │                   │
┌───────┼───────────────────┼────────────┐
│       ▼       Camera Module            │
│  ┌──────────┐    ┌──────────────────┐  │
│  │ I2C      │    │  Image Sensor    │  │
│  │ Slave    │◄──►│  Register File   │  │
│  └──────────┘    └──────────────────┘  │
│                  ┌──────────────────┐  │
│                  │  Pixel Array &   │  │
│                  │  ISP Pipeline    │  │
│                  └────────┬─────────┘  │
└───────────────────────────┼────────────┘
                            │ Pixel Data
                            ▼
                     Host CSI-2 / DVP Rx
```

The I2C slave address of the sensor is typically fixed in silicon but can sometimes be changed via a hardware pin (SADDR). Common addresses: `0x10`, `0x36`, `0x3C`, `0x48`, `0x6C`, `0x6E`.

---

## Common Camera Sensors and Their I2C Interfaces

| Sensor | I2C Address | Register Width | Data Width | Notes |
|---|---|---|---|---|
| OV2640 | 0x30 / 0x21 | 8-bit addr, 8-bit data | 8-bit | Popular in ESP32-CAM |
| OV5640 | 0x3C / 0x3D | 16-bit addr, 8-bit data | 8-bit | 5MP, MIPI/DVP |
| IMX219 | 0x10 | 16-bit addr, 8-bit data | 8-bit | Raspberry Pi Cam v2 |
| IMX477 | 0x1A | 16-bit addr, 8-bit data | 8-bit | Raspberry Pi HQ Cam |
| AR0234 | 0x10 | 16-bit addr, 16-bit data | 16-bit | Global shutter |
| MT9V032 | 0x48–0x4F | 8-bit addr, 16-bit data | 16-bit | Machine vision |
| GC2145 | 0x3C | 8-bit addr, 8-bit data | 8-bit | Budget modules |

> **Note:** The register address width and data width vary per sensor and must be handled correctly in driver code.

---

## I2C Register Map Concepts

Camera sensor register maps typically follow these conventions:

### Register Address Schemes

**8-bit address, 8-bit data** (e.g., OV2640):
```
START | SLAVE_ADDR+W | ACK | REG_ADDR[7:0] | ACK | DATA[7:0] | ACK | STOP
```

**16-bit address, 8-bit data** (e.g., OV5640, IMX219):
```
START | SLAVE_ADDR+W | ACK | REG_ADDR[15:8] | ACK | REG_ADDR[7:0] | ACK | DATA[7:0] | ACK | STOP
```

**16-bit address, 16-bit data** (e.g., AR0234):
```
START | SLAVE_ADDR+W | ACK | REG_ADDR[15:8] | ACK | REG_ADDR[7:0] | ACK | DATA[15:8] | ACK | DATA[7:0] | ACK | STOP
```

### Register Table Representation

Sensor drivers encode initialisation sequences as register tables:

```c
typedef struct {
    uint16_t reg;
    uint8_t  val;
} sensor_reg_t;

// Sentinel to end a table
#define REG_TABLE_END  { 0xFFFF, 0xFF }

// Delay marker (special sentinel)
#define REG_DELAY      { 0xFFFE, 0x00 }  // val = delay in ms
```

---

## Initialization and Reset Sequences

Every sensor requires a specific power-on and reset sequence before I2C communication is valid. Skipping these steps causes hung buses or incorrect register states.

### Typical Power-On Sequence

```
1. Assert PWDN (power-down, active high on many sensors)
2. Apply AVDD, DOVDD, DVDD supplies (order matters, check datasheet)
3. Wait ≥1 ms for supplies to stabilise
4. Deassert PWDN
5. Assert XCLK / MCLK (input clock, typically 24 MHz)
6. Wait ≥1 ms
7. Assert RESETB (reset, active low on most sensors)
8. Wait ≥5 ms
9. Deassert RESETB (release reset)
10. Wait ≥20 ms — sensor internal initialisation
11. I2C communication is now valid
12. Write software reset register (e.g., 0x3008 = 0x80 on OV5640)
13. Wait ≥5 ms
14. Write full initialisation register table
```

---

## Exposure and Gain Control

The exposure triangle for image sensors consists of:

- **Integration time** (shutter) — how many row periods the pixel integrates charge
- **Analogue gain** — amplification before ADC
- **Digital gain** — multiplication after ADC

These are the most frequently written registers during Auto-Exposure (AE) control loops.

### OV5640 Exposure Registers (example)

| Register | Description |
|---|---|
| 0x3500 | Exposure [19:16] |
| 0x3501 | Exposure [15:8] |
| 0x3502 | Exposure [7:0] (unit: 1/16 row) |
| 0x350A | Analogue gain [9:8] |
| 0x350B | Analogue gain [7:0] |

---

## Resolution and Format Configuration

Sensors support multiple output modes selected via I2C. Switching modes requires writing a pre-validated register table and then restarting the streaming pipeline.

### Common OV5640 Output Modes

| Mode | Resolution | Frame Rate | MIPI Lanes |
|---|---|---|---|
| QVGA | 320×240 | 120fps | 1 |
| VGA | 640×480 | 90fps | 1 |
| 720p | 1280×720 | 60fps | 2 |
| 1080p | 1920×1080 | 30fps | 2 |
| 5MP | 2592×1944 | 15fps | 2 |

Output formats (pixel format register):
- `0x60` = RAW8
- `0x61` = RAW10
- `0x30` = YUV422 (YUYV)
- `0x60` + ISP = JPEG

---

## Frame Rate Control

Frame rate is controlled by adjusting the **total horizontal and vertical blanking** periods. The key registers are VTS (Vertical Total Size) and HTS (Horizontal Total Size).

```
Frame Rate = PCLK / (HTS × VTS)
```

Increasing VTS reduces frame rate (adds vertical blanking). Increasing HTS adds horizontal blanking. Changing VTS during streaming for frame rate adjustment (rather than stopping/starting) is a common technique for smooth AE integration time extension.

---

## White Balance and Color Correction

Manual white balance is controlled through per-channel (R, G, B) gain registers. Auto white balance (AWB) is typically handled by the sensor's internal ISP but can be overridden.

### OV5640 Manual AWB Registers

| Register | Description |
|---|---|
| 0x3400 | Red gain [11:8] |
| 0x3401 | Red gain [7:0] |
| 0x3402 | Green gain [11:8] |
| 0x3403 | Green gain [7:0] |
| 0x3404 | Blue gain [11:8] |
| 0x3405 | Blue gain [7:0] |
| 0x3406 | AWB manual enable (bit 0) |

---

## Streaming Start/Stop

Starting and stopping pixel data output is controlled via I2C, separate from the image data lane interface. Proper sequencing prevents partial frames and lane desync.

---

## C/C++ Implementation Examples

### 1. Platform-Agnostic I2C Abstraction Layer

```c
// camera_i2c.h — Portable I2C HAL for camera control
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum {
    CAM_I2C_OK     =  0,
    CAM_I2C_NACK   = -1,
    CAM_I2C_TIMEOUT= -2,
    CAM_I2C_ERROR  = -3,
} cam_i2c_status_t;

// User implements these four functions for their platform
typedef struct {
    cam_i2c_status_t (*write)(uint8_t dev_addr, const uint8_t *buf, size_t len, void *ctx);
    cam_i2c_status_t (*read) (uint8_t dev_addr, uint8_t *buf, size_t len, void *ctx);
    void             (*delay_ms)(uint32_t ms, void *ctx);
    void             *ctx;  // platform context (e.g., I2C handle)
} cam_i2c_hal_t;
```

### 2. 16-bit Address, 8-bit Data Register Access (OV5640 / IMX219 style)

```c
// camera_reg16.c — 16-bit register address, 8-bit data access

#include "camera_i2c.h"
#include <string.h>

/**
 * Write a single 8-bit register at a 16-bit address.
 *
 * Wire format: [ADDR_H] [ADDR_L] [DATA]
 */
cam_i2c_status_t cam_reg16_write8(const cam_i2c_hal_t *hal,
                                   uint8_t  dev_addr,
                                   uint16_t reg,
                                   uint8_t  val)
{
    uint8_t buf[3] = {
        (uint8_t)(reg >> 8),   // Address high byte
        (uint8_t)(reg & 0xFF), // Address low byte
        val                    // Data
    };
    return hal->write(dev_addr, buf, sizeof(buf), hal->ctx);
}

/**
 * Read a single 8-bit register at a 16-bit address.
 *
 * Wire format (write phase): [ADDR_H] [ADDR_L]
 * Wire format (read phase):  [DATA]
 */
cam_i2c_status_t cam_reg16_read8(const cam_i2c_hal_t *hal,
                                  uint8_t  dev_addr,
                                  uint16_t reg,
                                  uint8_t  *val)
{
    uint8_t addr[2] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF)
    };

    cam_i2c_status_t st = hal->write(dev_addr, addr, sizeof(addr), hal->ctx);
    if (st != CAM_I2C_OK) return st;

    return hal->read(dev_addr, val, 1, hal->ctx);
}

/**
 * Modify bits in a register using a mask.
 *
 * @param mask   Bits to modify (1 = modify, 0 = preserve)
 * @param val    New values for the masked bits
 */
cam_i2c_status_t cam_reg16_modify8(const cam_i2c_hal_t *hal,
                                    uint8_t  dev_addr,
                                    uint16_t reg,
                                    uint8_t  mask,
                                    uint8_t  val)
{
    uint8_t current;
    cam_i2c_status_t st = cam_reg16_read8(hal, dev_addr, reg, &current);
    if (st != CAM_I2C_OK) return st;

    current = (current & ~mask) | (val & mask);
    return cam_reg16_write8(hal, dev_addr, reg, current);
}
```

### 3. Register Table Write with Delay Support

```c
// reg_table.c — Bulk register table write engine

#include "camera_i2c.h"
#include "camera_reg16.h"

#define REG_ADDR_END    0xFFFF   // End of table sentinel
#define REG_ADDR_DELAY  0xFFFE   // Delay: val = milliseconds

typedef struct {
    uint16_t reg;
    uint8_t  val;
} reg_entry_t;

/**
 * Write an entire register initialisation table.
 * Handles delay entries and stops at sentinel.
 *
 * Returns CAM_I2C_OK if all writes succeed,
 * or the first error encountered.
 */
cam_i2c_status_t cam_write_reg_table(const cam_i2c_hal_t *hal,
                                      uint8_t             dev_addr,
                                      const reg_entry_t  *table)
{
    cam_i2c_status_t st = CAM_I2C_OK;

    for (size_t i = 0; table[i].reg != REG_ADDR_END; i++) {
        if (table[i].reg == REG_ADDR_DELAY) {
            hal->delay_ms(table[i].val, hal->ctx);
            continue;
        }

        st = cam_reg16_write8(hal, dev_addr, table[i].reg, table[i].val);
        if (st != CAM_I2C_OK) {
            // Log: "Failed at reg 0x%04X = 0x%02X", table[i].reg, table[i].val
            return st;
        }
    }
    return CAM_I2C_OK;
}
```

### 4. OV5640 Driver — Init, Exposure, and Streaming Control

```c
// ov5640.c — OV5640 5MP image sensor driver

#include "camera_i2c.h"
#include "camera_reg16.h"
#include "reg_table.h"
#include <stdint.h>
#include <stdbool.h>

#define OV5640_I2C_ADDR     0x3C   // Default (SADDR=0); 0x3D if SADDR=1

// Key OV5640 registers
#define OV5640_REG_CHIP_ID_H    0x300A
#define OV5640_REG_CHIP_ID_L    0x300B
#define OV5640_REG_SW_RESET     0x3008
#define OV5640_REG_MIPI_CTRL00  0x300E
#define OV5640_REG_SYS_CTRL0    0x3008

// Streaming control
#define OV5640_REG_TIMING_TC_REG21  0x3821
#define OV5640_REG_STREAM_ON        0x4202  // 0x00 = stream, 0xFF = stop

// AEC/AGC registers
#define OV5640_REG_AEC_PK_EXPO_H    0x3500  // [19:16]
#define OV5640_REG_AEC_PK_EXPO_M    0x3501  // [15:8]
#define OV5640_REG_AEC_PK_EXPO_L    0x3502  // [7:0] unit = 1/16 line
#define OV5640_REG_AEC_PK_REAL_GAIN 0x350A  // [9:8]
#define OV5640_REG_AEC_PK_REAL_GAIN_L 0x350B // [7:0]

// VTS for frame rate control
#define OV5640_REG_TIMING_VTS_H     0x380E
#define OV5640_REG_TIMING_VTS_L     0x380F


// Abbreviated 1080p30 initialisation table (illustrative subset)
static const reg_entry_t ov5640_init_1080p[] = {
    // Software reset
    { 0x3008, 0x42 },  // Power down
    { 0xFFFE, 0x05 },  // Delay 5 ms
    { 0x3008, 0x02 },  // Normal operation

    // System clock
    { 0x3103, 0x11 },
    { 0x3035, 0x21 },  // PLL pre-divider
    { 0x3036, 0x69 },  // PLL multiplier = 105
    { 0x3C07, 0x07 },

    // MIPI 2-lane, 1080p timing
    { 0x3808, 0x07 },  // H_SIZE[11:8] = 1920
    { 0x3809, 0x80 },  // H_SIZE[7:0]
    { 0x380A, 0x04 },  // V_SIZE[11:8] = 1080
    { 0x380B, 0x38 },  // V_SIZE[7:0]
    { 0x380C, 0x0B },  // HTS[11:8] = total line width
    { 0x380D, 0x1C },  // HTS[7:0]
    { 0x380E, 0x07 },  // VTS[9:8]
    { 0x380F, 0xB0 },  // VTS[7:0]  → 30fps

    // Output format: YUV422
    { 0x4300, 0x30 },
    { 0x501F, 0x00 },

    // ISP enable
    { 0x5000, 0xA7 },
    { 0x5001, 0xA3 },

    { REG_ADDR_END, 0x00 }  // Sentinel
};


typedef struct {
    const cam_i2c_hal_t *hal;
    uint8_t              addr;
    uint16_t             current_vts;
    bool                 streaming;
} ov5640_t;


/**
 * Verify sensor identity by reading Chip ID registers.
 * OV5640 Chip ID = 0x5640.
 */
cam_i2c_status_t ov5640_probe(ov5640_t *cam)
{
    uint8_t id_h, id_l;
    cam_i2c_status_t st;

    st = cam_reg16_read8(cam->hal, cam->addr, OV5640_REG_CHIP_ID_H, &id_h);
    if (st != CAM_I2C_OK) return st;

    st = cam_reg16_read8(cam->hal, cam->addr, OV5640_REG_CHIP_ID_L, &id_l);
    if (st != CAM_I2C_OK) return st;

    uint16_t chip_id = ((uint16_t)id_h << 8) | id_l;
    if (chip_id != 0x5640) return CAM_I2C_ERROR;  // Wrong sensor

    return CAM_I2C_OK;
}


/**
 * Initialise OV5640 for 1080p30 YUV422 output.
 */
cam_i2c_status_t ov5640_init(ov5640_t *cam)
{
    cam_i2c_status_t st = ov5640_probe(cam);
    if (st != CAM_I2C_OK) return st;

    st = cam_write_reg_table(cam->hal, cam->addr, ov5640_init_1080p);
    if (st != CAM_I2C_OK) return st;

    cam->current_vts = 0x07B0;  // Matches table above
    cam->streaming   = false;
    return CAM_I2C_OK;
}


/**
 * Start pixel data output on the image data bus.
 */
cam_i2c_status_t ov5640_stream_start(ov5640_t *cam)
{
    cam_i2c_status_t st = cam_reg16_write8(cam->hal, cam->addr,
                                            OV5640_REG_STREAM_ON, 0x00);
    if (st == CAM_I2C_OK) cam->streaming = true;
    return st;
}


/**
 * Stop pixel data output.
 */
cam_i2c_status_t ov5640_stream_stop(ov5640_t *cam)
{
    cam_i2c_status_t st = cam_reg16_write8(cam->hal, cam->addr,
                                            OV5640_REG_STREAM_ON, 0xFF);
    if (st == CAM_I2C_OK) cam->streaming = false;
    return st;
}


/**
 * Set integration time (exposure) in units of 1/16 of a line period.
 *
 * Valid range: 1 to (VTS - 4) * 16
 * Must be applied in V-blank for glitch-free update.
 *
 * @param exposure_lines  Exposure in 1/16-line units
 */
cam_i2c_status_t ov5640_set_exposure(ov5640_t *cam, uint32_t exposure_lines)
{
    // Clamp to sensor limits
    uint32_t max_exp = ((uint32_t)cam->current_vts - 4U) << 4;
    if (exposure_lines > max_exp) exposure_lines = max_exp;
    if (exposure_lines < 1)       exposure_lines = 1;

    cam_i2c_status_t st;

    // Write MSB first; sensor latches all three on the final write
    st = cam_reg16_write8(cam->hal, cam->addr,
                          OV5640_REG_AEC_PK_EXPO_H,
                          (uint8_t)((exposure_lines >> 16) & 0x0F));
    if (st != CAM_I2C_OK) return st;

    st = cam_reg16_write8(cam->hal, cam->addr,
                          OV5640_REG_AEC_PK_EXPO_M,
                          (uint8_t)((exposure_lines >> 8) & 0xFF));
    if (st != CAM_I2C_OK) return st;

    return cam_reg16_write8(cam->hal, cam->addr,
                            OV5640_REG_AEC_PK_EXPO_L,
                            (uint8_t)(exposure_lines & 0xFF));
}


/**
 * Set analogue gain.
 *
 * @param gain_x16  Gain * 16, so 16 = 1×, 32 = 2×, 128 = 8×
 *                  Valid range: 16 – 511 (1× to ~32×)
 */
cam_i2c_status_t ov5640_set_gain(ov5640_t *cam, uint16_t gain_x16)
{
    if (gain_x16 > 0x1FF) gain_x16 = 0x1FF;
    if (gain_x16 < 0x010) gain_x16 = 0x010;

    cam_i2c_status_t st;

    st = cam_reg16_write8(cam->hal, cam->addr,
                          OV5640_REG_AEC_PK_REAL_GAIN,
                          (uint8_t)((gain_x16 >> 8) & 0x03));
    if (st != CAM_I2C_OK) return st;

    return cam_reg16_write8(cam->hal, cam->addr,
                            OV5640_REG_AEC_PK_REAL_GAIN_L,
                            (uint8_t)(gain_x16 & 0xFF));
}


/**
 * Change frame rate by adjusting VTS (Vertical Total Size).
 *
 * Only valid while streaming (modifies VTS in real-time).
 * Sensor PCLK and HTS are assumed constant from the init table.
 *
 * @param fps  Target frame rate (1–120)
 */
cam_i2c_status_t ov5640_set_framerate(ov5640_t *cam, uint8_t fps)
{
    // Example: PCLK = 84 MHz, HTS = 0x0B1C = 2844 clocks
    const uint32_t pclk_hz  = 84000000UL;
    const uint16_t hts      = 0x0B1C;
    const uint16_t vts_min  = 1084;     // Must be >= active lines + blanking

    if (fps == 0) fps = 1;

    uint32_t vts = pclk_hz / ((uint32_t)hts * fps);
    if (vts < vts_min) vts = vts_min;
    if (vts > 0xFFFF)  vts = 0xFFFF;

    cam_i2c_status_t st;

    st = cam_reg16_write8(cam->hal, cam->addr,
                          OV5640_REG_TIMING_VTS_H, (uint8_t)(vts >> 8));
    if (st != CAM_I2C_OK) return st;

    st = cam_reg16_write8(cam->hal, cam->addr,
                          OV5640_REG_TIMING_VTS_L, (uint8_t)(vts & 0xFF));
    if (st == CAM_I2C_OK) cam->current_vts = (uint16_t)vts;
    return st;
}
```

### 5. Linux Kernel-Style I2C Regmap Usage (C)

```c
// ov5640_kernel.c — Kernel driver pattern using regmap

#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

struct ov5640_dev {
    struct i2c_client   *client;
    struct regmap       *regmap;
    struct v4l2_subdev   sd;
    // ... additional state
};

static const struct regmap_config ov5640_regmap_config = {
    .reg_bits      = 16,  // 16-bit register addresses
    .val_bits      =  8,  // 8-bit values
    .max_register  = 0x6FFF,
    .cache_type    = REGCACHE_NONE,
};

static int ov5640_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    struct ov5640_dev *sensor;
    int ret;

    sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
    if (!sensor) return -ENOMEM;

    sensor->client = client;
    sensor->regmap = devm_regmap_init_i2c(client, &ov5640_regmap_config);
    if (IS_ERR(sensor->regmap)) {
        dev_err(&client->dev, "regmap init failed\n");
        return PTR_ERR(sensor->regmap);
    }

    // Verify chip ID
    unsigned int chip_id_h, chip_id_l;
    ret  = regmap_read(sensor->regmap, 0x300A, &chip_id_h);
    ret |= regmap_read(sensor->regmap, 0x300B, &chip_id_l);
    if (ret || (((chip_id_h << 8) | chip_id_l) != 0x5640)) {
        dev_err(&client->dev, "unexpected chip ID 0x%04X\n",
                (chip_id_h << 8) | chip_id_l);
        return -ENODEV;
    }

    // Example: modify a register field — enable AWB
    // Bit 0 of 0x3400 = manual AWB enable (1 = manual, 0 = auto)
    ret = regmap_update_bits(sensor->regmap, 0x3406,
                             BIT(0),  // mask
                             0);      // value: 0 = auto AWB
    if (ret) return ret;

    dev_info(&client->dev, "OV5640 found, chip ID 0x5640\n");
    return 0;
}
```

---

## Rust Implementation Examples

### 1. I2C Camera Driver using `embedded-hal`

```rust
// camera_i2c.rs — Generic camera I2C driver using embedded-hal traits

use embedded_hal::i2c::{I2c, SevenBitAddress};
use core::marker::PhantomData;

/// Error type for camera I2C operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CameraError<E> {
    /// Underlying I2C bus error
    I2c(E),
    /// Sensor returned unexpected chip ID
    WrongChipId { expected: u16, got: u16 },
    /// Parameter out of valid range
    InvalidParameter,
}

impl<E> From<E> for CameraError<E> {
    fn from(e: E) -> Self {
        CameraError::I2c(e)
    }
}

/// Marker trait for sensors with 16-bit register addresses and 8-bit data.
pub trait Reg16Data8 {
    const I2C_ADDR: SevenBitAddress;
    const CHIP_ID: u16;
    const REG_CHIP_ID_H: u16;
    const REG_CHIP_ID_L: u16;
}

/// Generic I2C camera register accessor for 16-bit address, 8-bit data sensors.
pub struct CameraReg16<I2C, SENSOR> {
    i2c: I2C,
    addr: SevenBitAddress,
    _sensor: PhantomData<SENSOR>,
}

impl<I2C, SENSOR, E> CameraReg16<I2C, SENSOR>
where
    I2C: I2c<SevenBitAddress, Error = E>,
    SENSOR: Reg16Data8,
{
    /// Create a new instance, consuming the I2C bus.
    pub fn new(i2c: I2C) -> Self {
        Self {
            i2c,
            addr: SENSOR::I2C_ADDR,
            _sensor: PhantomData,
        }
    }

    /// Write a single 8-bit value to a 16-bit register address.
    pub fn write_reg(&mut self, reg: u16, val: u8) -> Result<(), CameraError<E>> {
        let buf = [
            (reg >> 8) as u8,   // Address high byte
            (reg & 0xFF) as u8, // Address low byte
            val,                // Data
        ];
        self.i2c.write(self.addr, &buf).map_err(CameraError::I2c)
    }

    /// Read a single 8-bit value from a 16-bit register address.
    pub fn read_reg(&mut self, reg: u16) -> Result<u8, CameraError<E>> {
        let addr_buf = [(reg >> 8) as u8, (reg & 0xFF) as u8];
        let mut data = [0u8];

        // write_read performs a repeated START between the two phases
        self.i2c
            .write_read(self.addr, &addr_buf, &mut data)
            .map_err(CameraError::I2c)?;

        Ok(data[0])
    }

    /// Read-modify-write: apply `mask` and `val` to a register.
    ///
    /// Bits set in `mask` are replaced with corresponding bits from `val`.
    pub fn modify_reg(&mut self, reg: u16, mask: u8, val: u8) -> Result<(), CameraError<E>> {
        let current = self.read_reg(reg)?;
        let updated = (current & !mask) | (val & mask);
        self.write_reg(reg, updated)
    }

    /// Verify the sensor's chip ID.
    pub fn verify_chip_id(&mut self) -> Result<(), CameraError<E>> {
        let id_h = self.read_reg(SENSOR::REG_CHIP_ID_H)?;
        let id_l = self.read_reg(SENSOR::REG_CHIP_ID_L)?;
        let chip_id = ((id_h as u16) << 8) | (id_l as u16);

        if chip_id != SENSOR::CHIP_ID {
            return Err(CameraError::WrongChipId {
                expected: SENSOR::CHIP_ID,
                got: chip_id,
            });
        }
        Ok(())
    }
}
```

### 2. OV5640 Sensor Type and Register Table

```rust
// ov5640.rs — OV5640 sensor definition and driver

use crate::camera_i2c::{CameraError, CameraReg16, Reg16Data8};
use embedded_hal::i2c::{I2c, SevenBitAddress};
use embedded_hal::delay::DelayNs;

/// OV5640 sensor marker type.
pub struct Ov5640;

impl Reg16Data8 for Ov5640 {
    const I2C_ADDR: SevenBitAddress = 0x3C;
    const CHIP_ID: u16              = 0x5640;
    const REG_CHIP_ID_H: u16        = 0x300A;
    const REG_CHIP_ID_L: u16        = 0x300B;
}

/// Sentinel values in register tables.
const REG_END:   u16 = 0xFFFF;  // End of table
const REG_DELAY: u16 = 0xFFFE;  // val = delay in ms

/// A single entry in a register initialisation table.
#[derive(Copy, Clone)]
pub struct RegEntry {
    pub reg: u16,
    pub val: u8,
}

impl RegEntry {
    pub const fn new(reg: u16, val: u8) -> Self {
        Self { reg, val }
    }
    pub const fn delay(ms: u8) -> Self {
        Self { reg: REG_DELAY, val: ms }
    }
    pub const fn end() -> Self {
        Self { reg: REG_END, val: 0 }
    }
}

/// Abbreviated 1080p30 initialisation table.
pub static OV5640_INIT_1080P30: &[RegEntry] = &[
    RegEntry::new(0x3008, 0x42),  // Power down
    RegEntry::delay(5),           // Wait 5 ms
    RegEntry::new(0x3008, 0x02),  // Normal mode

    // PLL configuration for 84 MHz PCLK
    RegEntry::new(0x3103, 0x11),
    RegEntry::new(0x3035, 0x21),
    RegEntry::new(0x3036, 0x69),
    RegEntry::new(0x3C07, 0x07),

    // 1920×1080 active window
    RegEntry::new(0x3808, 0x07),
    RegEntry::new(0x3809, 0x80),
    RegEntry::new(0x380A, 0x04),
    RegEntry::new(0x380B, 0x38),

    // Timing: HTS=2844, VTS=1968 → ~30fps
    RegEntry::new(0x380C, 0x0B),
    RegEntry::new(0x380D, 0x1C),
    RegEntry::new(0x380E, 0x07),
    RegEntry::new(0x380F, 0xB0),

    // YUV422 output
    RegEntry::new(0x4300, 0x30),
    RegEntry::new(0x501F, 0x00),

    // Enable ISP
    RegEntry::new(0x5000, 0xA7),
    RegEntry::new(0x5001, 0xA3),

    RegEntry::end(),
];


/// OV5640 high-level driver.
pub struct Ov5640Driver<I2C, DELAY> {
    regs:        CameraReg16<I2C, Ov5640>,
    delay:       DELAY,
    current_vts: u16,
    streaming:   bool,
}

impl<I2C, DELAY, E> Ov5640Driver<I2C, DELAY>
where
    I2C: I2c<SevenBitAddress, Error = E>,
    DELAY: DelayNs,
{
    pub fn new(i2c: I2C, delay: DELAY) -> Self {
        Self {
            regs: CameraReg16::new(i2c),
            delay,
            current_vts: 0x07B0,
            streaming: false,
        }
    }

    /// Write an entire register table, handling delays and the end sentinel.
    fn write_table(&mut self, table: &[RegEntry]) -> Result<(), CameraError<E>> {
        for entry in table {
            match entry.reg {
                REG_END   => break,
                REG_DELAY => self.delay.delay_ms(entry.val as u32),
                reg       => self.regs.write_reg(reg, entry.val)?,
            }
        }
        Ok(())
    }

    /// Probe and initialise the sensor for 1080p30 YUV422.
    pub fn init(&mut self) -> Result<(), CameraError<E>> {
        self.regs.verify_chip_id()?;
        self.write_table(OV5640_INIT_1080P30)?;
        self.current_vts = 0x07B0;
        self.streaming   = false;
        Ok(())
    }

    /// Begin streaming pixel data.
    pub fn stream_start(&mut self) -> Result<(), CameraError<E>> {
        self.regs.write_reg(0x4202, 0x00)?;
        self.streaming = true;
        Ok(())
    }

    /// Halt pixel data output.
    pub fn stream_stop(&mut self) -> Result<(), CameraError<E>> {
        self.regs.write_reg(0x4202, 0xFF)?;
        self.streaming = false;
        Ok(())
    }

    /// Set integration time.
    ///
    /// `exposure_lines`: units of 1/16 line period.
    /// Automatically clamped to the valid range for the current VTS.
    pub fn set_exposure(&mut self, exposure_lines: u32) -> Result<(), CameraError<E>> {
        let max_exp = ((self.current_vts as u32).saturating_sub(4)) << 4;
        let exp = exposure_lines.clamp(1, max_exp);

        self.regs.write_reg(0x3500, ((exp >> 16) & 0x0F) as u8)?;
        self.regs.write_reg(0x3501, ((exp >>  8) & 0xFF) as u8)?;
        self.regs.write_reg(0x3502, ( exp        & 0xFF) as u8)?;
        Ok(())
    }

    /// Set analogue gain.
    ///
    /// `gain_x16`: gain multiplied by 16. Range 16 (1×) to 511 (~32×).
    pub fn set_gain(&mut self, gain_x16: u16) -> Result<(), CameraError<E>> {
        let gain = gain_x16.clamp(0x010, 0x1FF);
        self.regs.write_reg(0x350A, ((gain >> 8) & 0x03) as u8)?;
        self.regs.write_reg(0x350B,  (gain       & 0xFF) as u8)?;
        Ok(())
    }

    /// Adjust frame rate by changing VTS.
    ///
    /// Assumes PCLK = 84 MHz, HTS = 2844. Valid fps range: 1–120.
    pub fn set_framerate(&mut self, fps: u8) -> Result<(), CameraError<E>> {
        if fps == 0 {
            return Err(CameraError::InvalidParameter);
        }
        const PCLK_HZ: u32 = 84_000_000;
        const HTS: u32     = 2844;
        const VTS_MIN: u16 = 1084;

        let vts = (PCLK_HZ / (HTS * fps as u32))
            .clamp(VTS_MIN as u32, 0xFFFF) as u16;

        self.regs.write_reg(0x380E, (vts >> 8) as u8)?;
        self.regs.write_reg(0x380F, (vts & 0xFF) as u8)?;
        self.current_vts = vts;
        Ok(())
    }

    /// Set manual white balance gains (R, G, B).
    ///
    /// Values are in Q4.8 fixed point (256 = 1.0×).
    /// Disables auto white balance.
    pub fn set_white_balance(
        &mut self,
        r_gain: u16,
        g_gain: u16,
        b_gain: u16,
    ) -> Result<(), CameraError<E>> {
        // Disable AWB
        self.regs.modify_reg(0x3406, 0x01, 0x01)?;

        self.regs.write_reg(0x3400, ((r_gain >> 8) & 0x0F) as u8)?;
        self.regs.write_reg(0x3401,  (r_gain       & 0xFF) as u8)?;
        self.regs.write_reg(0x3402, ((g_gain >> 8) & 0x0F) as u8)?;
        self.regs.write_reg(0x3403,  (g_gain       & 0xFF) as u8)?;
        self.regs.write_reg(0x3404, ((b_gain >> 8) & 0x0F) as u8)?;
        self.regs.write_reg(0x3405,  (b_gain       & 0xFF) as u8)?;
        Ok(())
    }

    /// Re-enable automatic white balance.
    pub fn enable_auto_wb(&mut self) -> Result<(), CameraError<E>> {
        self.regs.modify_reg(0x3406, 0x01, 0x00)
    }
}
```

### 3. IMX219 Driver (Raspberry Pi Camera v2 — no_std)

```rust
// imx219.rs — IMX219 sensor driver for no_std environments

use embedded_hal::i2c::{I2c, SevenBitAddress};

const IMX219_ADDR: SevenBitAddress = 0x10;

/// IMX219 key registers
const IMX219_REG_CHIP_ID:       u16 = 0x0000; // returns 0x0219
const IMX219_REG_MODE_SEL:      u16 = 0x0100; // 0x00 = standby, 0x01 = streaming
const IMX219_REG_SW_RESET:      u16 = 0x0103; // 0x01 = reset
const IMX219_REG_COARSE_INT_H:  u16 = 0x015A; // Integration time [15:8]
const IMX219_REG_COARSE_INT_L:  u16 = 0x015B; // Integration time [7:0]
const IMX219_REG_ANA_GAIN:      u16 = 0x0157; // Analogue gain [7:0]
const IMX219_REG_DIG_GAIN_H:    u16 = 0x0158; // Digital gain [11:8]
const IMX219_REG_DIG_GAIN_L:    u16 = 0x0159; // Digital gain [7:0]

pub struct Imx219<I2C> {
    i2c: I2C,
}

impl<I2C, E> Imx219<I2C>
where
    I2C: I2c<SevenBitAddress, Error = E>,
{
    pub fn new(i2c: I2C) -> Self {
        Self { i2c }
    }

    fn write(&mut self, reg: u16, val: u8) -> Result<(), E> {
        self.i2c.write(IMX219_ADDR, &[
            (reg >> 8) as u8,
            (reg & 0xFF) as u8,
            val,
        ])
    }

    fn read(&mut self, reg: u16) -> Result<u8, E> {
        let mut buf = [0u8];
        self.i2c.write_read(
            IMX219_ADDR,
            &[(reg >> 8) as u8, (reg & 0xFF) as u8],
            &mut buf,
        )?;
        Ok(buf[0])
    }

    fn read_u16(&mut self, reg_h: u16, reg_l: u16) -> Result<u16, E> {
        let h = self.read(reg_h)? as u16;
        let l = self.read(reg_l)? as u16;
        Ok((h << 8) | l)
    }

    /// Verify sensor identity (Chip ID must be 0x0219).
    pub fn check_id(&mut self) -> Result<bool, E> {
        let id = self.read_u16(IMX219_REG_CHIP_ID, IMX219_REG_CHIP_ID + 1)?;
        Ok(id == 0x0219)
    }

    /// Issue a software reset and enter standby.
    pub fn software_reset(&mut self) -> Result<(), E> {
        self.write(IMX219_REG_SW_RESET, 0x01)?;
        // Caller must delay ≥ 10 ms before any further I2C access
        Ok(())
    }

    /// Enter streaming mode (pixel data output begins).
    pub fn start_streaming(&mut self) -> Result<(), E> {
        self.write(IMX219_REG_MODE_SEL, 0x01)
    }

    /// Enter standby mode.
    pub fn stop_streaming(&mut self) -> Result<(), E> {
        self.write(IMX219_REG_MODE_SEL, 0x00)
    }

    /// Set coarse integration time (exposure) in line periods.
    ///
    /// Maximum value = frame_length_lines - 4.
    pub fn set_coarse_integration_time(&mut self, lines: u16) -> Result<(), E> {
        self.write(IMX219_REG_COARSE_INT_H, (lines >> 8) as u8)?;
        self.write(IMX219_REG_COARSE_INT_L, (lines & 0xFF) as u8)
    }

    /// Set analogue gain.
    ///
    /// IMX219 uses a linear code: 0 = 1×, 232 ≈ 10.7×.
    /// gain_code = 256 - 256/gain_linear
    pub fn set_analogue_gain(&mut self, gain_code: u8) -> Result<(), E> {
        self.write(IMX219_REG_ANA_GAIN, gain_code)
    }

    /// Set digital gain (applied after ADC).
    ///
    /// Format: U8.8 fixed point. 0x0100 = 1.0×, 0x0200 = 2.0×.
    /// Valid range: 0x0100 to 0x0FFF.
    pub fn set_digital_gain(&mut self, gain_u8_8: u16) -> Result<(), E> {
        let clamped = gain_u8_8.clamp(0x0100, 0x0FFF);
        self.write(IMX219_REG_DIG_GAIN_H, ((clamped >> 8) & 0x0F) as u8)?;
        self.write(IMX219_REG_DIG_GAIN_L, (clamped & 0xFF) as u8)
    }
}
```

### 4. Async I2C Camera Control with `embassy`

```rust
// ov5640_async.rs — Async I2C camera control using embassy-hal

use embassy_embedded_hal::shared_bus::asynch::i2c::I2cDevice;
use embassy_hal_internal::into_ref;
use embedded_hal_async::i2c::I2c;

const OV5640_ADDR: u8 = 0x3C;

pub struct Ov5640Async<I2C> {
    i2c: I2C,
}

impl<I2C, E> Ov5640Async<I2C>
where
    I2C: I2c<Error = E>,
{
    pub fn new(i2c: I2C) -> Self {
        Self { i2c }
    }

    async fn write_reg(&mut self, reg: u16, val: u8) -> Result<(), E> {
        let buf = [(reg >> 8) as u8, (reg & 0xFF) as u8, val];
        self.i2c.write(OV5640_ADDR, &buf).await
    }

    async fn read_reg(&mut self, reg: u16) -> Result<u8, E> {
        let addr = [(reg >> 8) as u8, (reg & 0xFF) as u8];
        let mut data = [0u8];
        self.i2c.write_read(OV5640_ADDR, &addr, &mut data).await?;
        Ok(data[0])
    }

    /// Non-blocking initialisation sequence using async I2C.
    pub async fn init(&mut self) -> Result<(), E> {
        // Software reset
        self.write_reg(0x3008, 0x82).await?;
        embassy_time::Timer::after_millis(5).await;

        // Verify chip ID
        let id_h = self.read_reg(0x300A).await?;
        let id_l = self.read_reg(0x300B).await?;
        // In a real driver: assert_eq!((id_h, id_l), (0x56, 0x40));

        // Write abbreviated init sequence (async version)
        let init_regs: &[(u16, u8)] = &[
            (0x3008, 0x42),
            (0x3103, 0x11),
            (0x3035, 0x21),
            (0x3036, 0x69),
            (0x3808, 0x07), (0x3809, 0x80),
            (0x380A, 0x04), (0x380B, 0x38),
            (0x380C, 0x0B), (0x380D, 0x1C),
            (0x380E, 0x07), (0x380F, 0xB0),
        ];

        for &(reg, val) in init_regs {
            self.write_reg(reg, val).await?;
        }

        embassy_time::Timer::after_millis(20).await;
        self.write_reg(0x3008, 0x02).await?;  // Exit standby

        Ok(())
    }

    pub async fn start_streaming(&mut self) -> Result<(), E> {
        self.write_reg(0x4202, 0x00).await
    }

    pub async fn stop_streaming(&mut self) -> Result<(), E> {
        self.write_reg(0x4202, 0xFF).await
    }
}
```

---

## Error Handling Strategies

Robust camera I2C drivers must handle several failure classes:

### I2C Bus Errors

```c
// C: Retry transient bus errors (e.g., clock stretching timeout)
static cam_i2c_status_t cam_write_with_retry(const cam_i2c_hal_t *hal,
                                              uint8_t  dev_addr,
                                              uint16_t reg,
                                              uint8_t  val,
                                              int      max_retries)
{
    for (int i = 0; i < max_retries; i++) {
        cam_i2c_status_t st = cam_reg16_write8(hal, dev_addr, reg, val);
        if (st == CAM_I2C_OK) return CAM_I2C_OK;
        if (st == CAM_I2C_NACK) return st;  // Hard fail — don't retry NACK
        hal->delay_ms(1, hal->ctx);          // Brief back-off before retry
    }
    return CAM_I2C_TIMEOUT;
}
```

```rust
// Rust: Map sensor errors to application-level context
use core::fmt;

#[derive(Debug)]
pub enum AppError<E: fmt::Debug> {
    Camera(CameraError<E>),
    Config(&'static str),
}

fn configure_sensor<I2C, E>(driver: &mut Ov5640Driver<I2C, impl embedded_hal::delay::DelayNs>)
    -> Result<(), AppError<E>>
where
    I2C: I2c<SevenBitAddress, Error = E>,
    E: fmt::Debug,
{
    driver.init()
        .map_err(AppError::Camera)?;

    driver.set_framerate(30)
        .map_err(AppError::Camera)?;

    driver.stream_start()
        .map_err(AppError::Camera)?;

    Ok(())
}
```

### Sensor State Machine Guard

```rust
// Prevent illegal state transitions at compile time using typestate pattern

pub struct Standby;
pub struct Streaming;

pub struct TypedCamera<I2C, STATE> {
    inner: Ov5640Driver<I2C, embassy_time::Delay>,
    _state: core::marker::PhantomData<STATE>,
}

impl<I2C, E> TypedCamera<I2C, Standby>
where
    I2C: I2c<SevenBitAddress, Error = E>,
{
    /// Can only call start_streaming from Standby state.
    pub fn start_streaming(mut self)
        -> Result<TypedCamera<I2C, Streaming>, (Self, CameraError<E>)>
    {
        match self.inner.stream_start() {
            Ok(()) => Ok(TypedCamera { inner: self.inner, _state: core::marker::PhantomData }),
            Err(e) => Err((self, e)),
        }
    }
}

impl<I2C, E> TypedCamera<I2C, Streaming>
where
    I2C: I2c<SevenBitAddress, Error = E>,
{
    /// Exposure can only be set while streaming (safe to modify live).
    pub fn set_exposure(&mut self, lines: u32) -> Result<(), CameraError<E>> {
        self.inner.set_exposure(lines)
    }

    /// Can only call stop_streaming from Streaming state.
    pub fn stop_streaming(mut self)
        -> Result<TypedCamera<I2C, Standby>, (Self, CameraError<E>)>
    {
        match self.inner.stream_stop() {
            Ok(()) => Ok(TypedCamera { inner: self.inner, _state: core::marker::PhantomData }),
            Err(e) => Err((self, e)),
        }
    }
}
// Note: set_framerate() is intentionally unavailable in the Streaming typestate —
// the caller must stop, reconfigure, then restart.
```

---

## Summary

Camera module control via I2C is a well-established pattern in embedded systems where a slow, bidirectional I2C bus carries all configuration traffic while a separate high-speed data bus delivers pixel frames. The key points to master are:

**Protocol Mechanics.** Register address width (8-bit vs 16-bit) and data width (8-bit vs 16-bit) vary per sensor family and must be matched exactly in driver code. A repeated START between the address write and data read phase is mandatory for register reads.

**Initialisation Sequencing.** Every sensor demands a specific power supply ramp order, PWDN/RESET assertion sequence, and mandatory settling delays before I2C is valid. These are non-negotiable and sensor-specific.

**Register Table Driven Design.** Initialisation sequences are best stored as static tables of `(address, value)` pairs with special sentinel entries for delays and end-of-table. This approach is both compact and portable across host platforms.

**Live Register Updates.** Exposure, gain, white balance, and frame rate can be changed while streaming but must be written in the correct multi-register order (MSB first, sensor latches on LSB write) and ideally timed to V-blank boundaries to avoid visible artefacts.

**C/C++ Patterns.** A thin HAL abstraction with four callbacks (`write`, `read`, `delay_ms`, `ctx`) makes drivers portable across bare-metal, RTOS, and Linux kernel environments. Linux drivers can use `regmap_init_i2c` to reduce boilerplate.

**Rust Patterns.** The `embedded-hal` I2C trait provides zero-cost portability across all hardware targets. The typestate pattern enforces valid sensor state transitions at compile time — for example, preventing exposure changes from a stopped sensor. Async drivers built on `embassy` enable non-blocking camera control in concurrent firmware.

**Robustness.** Transient I2C errors warrant a retry loop with back-off. NACK responses (sensor busy or wrong address) should not be retried. Chip ID verification at probe time catches wiring errors early. In safety-critical applications, periodic register readback can detect single-event upset corruption.

---

*Document covers: OV5640, IMX219, OV2640, IMX477, AR0234 sensor families. Applicable to bare-metal MCU, RTOS, Linux kernel, and no_std Rust environments.*