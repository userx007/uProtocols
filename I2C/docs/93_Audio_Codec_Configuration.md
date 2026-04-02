# 93. Audio Codec Configuration — Controlling Audio Devices via I2C Control Interfaces

**What's included:**

- **Architecture Overview** — the dual-bus split between I2C (control) and I2S (audio data), with an ASCII block diagram of a typical codec's internal signal chain
- **Register Map Concepts** — flat vs. paged/banked maps, bit-field layouts, and auto-increment burst access
- **Initialization Workflow** — the 13-step power-up sequence that avoids pops and ensures correct operation
- **C/C++ Code** — low-level `i2c-dev` register primitives, WM8960 register definitions with SMBus 9-bit packing, volume/gain control with dB mapping, PLL configuration for 44.1 and 48 kHz, anti-pop power sequencing, and a full RAII C++ driver class with shadow register caching
- **Rust Code** — `embedded-hal`-based I2C abstraction, type-safe register maps with `bitflags`, a full `Wm8960<I2C>` generic driver, a `Tlv320Aic3104` driver demonstrating page-switching, and proper `thiserror` error propagation
- **Advanced Topics** — multi-byte register writes, page guard patterns, PLL fractional math, dynamic sample rate reconfiguration, and GPIO interrupt handling
- **Real-World Codec Examples** — TI TLV320AIC3x, Cirrus CS42L52, and Wolfson WM8960 with device-specific notes
- **Debugging** — `i2cdetect`/`i2cdump`/`i2cget` commands, oscilloscope signal checks, and a symptom/cause table for the most common failures
- **Summary** — concise recap of all key principles

---

## Table of Contents

1. [Introduction](#introduction)
2. [I2C in the Audio Domain](#i2c-in-the-audio-domain)
3. [Audio Codec Architecture Overview](#audio-codec-architecture-overview)
4. [I2C Register Map Concepts](#i2c-register-map-concepts)
5. [Typical Configuration Workflow](#typical-configuration-workflow)
6. [C/C++ Implementation](#cc-implementation)
   - [Low-Level I2C Register Access (Linux i2c-dev)](#low-level-i2c-register-access-linux-i2c-dev)
   - [Codec Initialization Sequence](#codec-initialization-sequence)
   - [Volume and Gain Control](#volume-and-gain-control)
   - [Sample Rate and Format Configuration](#sample-rate-and-format-configuration)
   - [Power Management](#power-management)
   - [Full Codec Driver Class (C++)](#full-codec-driver-class-c)
7. [Rust Implementation](#rust-implementation)
   - [I2C Abstraction with embedded-hal](#i2c-abstraction-with-embedded-hal)
   - [Codec Register Map in Rust](#codec-register-map-in-rust)
   - [Initialization and Configuration](#initialization-and-configuration)
   - [Error Handling Patterns](#error-handling-patterns)
   - [Full Rust Codec Driver](#full-rust-codec-driver)
8. [Advanced Topics](#advanced-topics)
   - [Multi-Byte Register Access](#multi-byte-register-access)
   - [Page/Bank Switching](#pagebank-switching)
   - [Audio PLL Configuration](#audio-pll-configuration)
   - [Dynamic Reconfiguration (Mute/Unmute)](#dynamic-reconfiguration-muteunmute)
   - [Interrupt Handling via GPIO](#interrupt-handling-via-gpio)
9. [Real-World Codec Examples](#real-world-codec-examples)
   - [TI TLV320AIC3x Series](#ti-tlv320aic3x-series)
   - [Cirrus Logic CS42L52](#cirrus-logic-cs42l52)
   - [Wolfson/Cirrus WM8960](#wolfsoncirrus-wm8960)
10. [Debugging and Diagnostics](#debugging-and-diagnostics)
11. [Summary](#summary)

---

## Introduction

Audio codecs (coder-decoders) are mixed-signal ICs that convert between analog audio signals and digital PCM (Pulse Code Modulation) streams. They are found in smartphones, embedded systems, IoT devices, automotive infotainment units, professional audio equipment, and development boards.

Modern audio codecs use a **dual-bus architecture**:

| Bus | Purpose |
|-----|---------|
| **I2S / TDM / PCM** | High-speed digital audio data (samples at e.g. 44.1 kHz, 48 kHz, 192 kHz) |
| **I2C (or SPI)** | Low-speed control interface — register reads/writes for configuration |

The I2C control interface is used to configure every aspect of the codec: gain stages, sample rates, power domains, routing matrices, PLLs, equalizers, dynamic range compressors, and more. This document focuses entirely on the **I2C control side**.

---

## I2C in the Audio Domain

Audio codecs typically appear on the I2C bus as slave devices with a **7-bit address** (commonly selectable via hardware pins, e.g. `0x18`, `0x1A`, `0x1C`, `0x34`). The bus speed is usually **Standard Mode (100 kHz)** or **Fast Mode (400 kHz)** — audio data does not flow over I2C, so high I2C speed is not critical.

### Typical I2C Transactions for Codec Control

**Single-byte register write:**
```
START | ADDR+W | REG_ADDR | DATA_BYTE | STOP
```

**Single-byte register read:**
```
START | ADDR+W | REG_ADDR | REPEATED START | ADDR+R | DATA_BYTE | NACK | STOP
```

**Multi-byte (burst) register write:**
```
START | ADDR+W | REG_ADDR | DATA[0] | DATA[1] | ... | DATA[n] | STOP
```

Some codecs auto-increment the register address on burst writes; others require explicit address management.

---

## Audio Codec Architecture Overview

A typical stereo audio codec contains the following functional blocks, all configurable over I2C:

```
                  ┌───────────────────────────────────────────────┐
  MIC_IN ────────►│  PGA (Preamp/Gain)  ──► ADC ──► Digital Filter│──► I2S TX
  LINE_IN ───────►│  Mixer / Router                               │
                  │                                               │
  I2S RX ────────►│  Digital Filter ──► DAC ──► Output Mixer      │──► LINE_OUT
                  │                          └──► Headphone Amp   │──► HP_OUT
                  │                                               │
                  │  PLL / MCLK Generator                         │
                  │  Power Management (LDOs, Bias)                │
                  │                                               │
                  │◄────────────── I2C Control Bus ───────────────│
                  └───────────────────────────────────────────────┘
```

Each block is controlled through a **register map** — a flat or paged address space of 8-bit or 16-bit registers.

---

## I2C Register Map Concepts

### Flat Register Map

Simple codecs expose all registers in a single 8-bit address space (0x00–0xFF). Each register address maps directly to a configuration field.

```
Reg 0x00: Chip ID (read-only)
Reg 0x01: Power Control
Reg 0x02: DAC Volume Left
Reg 0x03: DAC Volume Right
Reg 0x04: ADC Gain
Reg 0x05: Interface Format (I2S/PCM/TDM, word length)
...
```

### Paged/Banked Register Map

More complex codecs (e.g., TI TLV320AIC3x) use a **page register** to switch between banks of 128 registers, effectively providing thousands of configuration registers.

```
Write 0x00 = 0x01  → switch to Page 1
Write 0x24 = 0x80  → configure register 0x24 on Page 1
Write 0x00 = 0x00  → switch back to Page 0
```

### Bit-Field Layout

Within each register, specific bits control individual features:

```
Register 0x01 — Power Control
  Bit 7:   POWER_ON      (1 = device active)
  Bit 6:   DAC_PWR       (1 = DAC powered)
  Bit 5:   ADC_PWR       (1 = ADC powered)
  Bit 4:   PLL_PWR       (1 = PLL active)
  Bit 3-2: VMID_SEL      (00=off, 01=50kΩ, 10=250kΩ, 11=5kΩ)
  Bit 1:   VREF_PWR      (1 = VREF buffer on)
  Bit 0:   Reserved
```

---

## Typical Configuration Workflow

A well-structured codec initialization follows this sequence:

```
1. Hardware Reset (assert RESET pin low, then high)
2. Wait for power-up time (typ. 1–10 ms)
3. Verify Chip ID register
4. Configure VMID / Bias / Reference voltages
5. Configure PLL (if MCLK != target sample rate * 256)
6. Set audio interface format (I2S, PCM, word length, BCLK polarity)
7. Set sample rate / oversampling ratio
8. Configure input routing (which inputs → ADC)
9. Set ADC gain / PGA gain
10. Configure output routing (DAC → which outputs)
11. Set DAC volume / output gain
12. Power up individual blocks (ADC, DAC, mixers, amplifiers)
13. Enable output (unmute)
```

Skipping or reordering steps often causes pops, clicks, noise, or silent output.

---

## C/C++ Implementation

### Low-Level I2C Register Access (Linux i2c-dev)

```c
// i2c_codec.h — Low-level I2C register access for Linux i2c-dev
#ifndef I2C_CODEC_H
#define I2C_CODEC_H

#include <stdint.h>
#include <stdbool.h>

// Linux headers for i2c-dev
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

typedef struct {
    int     fd;         // File descriptor for /dev/i2c-N
    uint8_t addr;       // 7-bit I2C slave address
} i2c_dev_t;

// Open I2C bus and set slave address
static inline int i2c_open(i2c_dev_t *dev, const char *bus_path, uint8_t addr) {
    dev->addr = addr;
    dev->fd = open(bus_path, O_RDWR);
    if (dev->fd < 0) {
        perror("i2c_open: open");
        return -1;
    }
    if (ioctl(dev->fd, I2C_SLAVE, addr) < 0) {
        perror("i2c_open: ioctl I2C_SLAVE");
        close(dev->fd);
        return -1;
    }
    return 0;
}

static inline void i2c_close(i2c_dev_t *dev) {
    if (dev->fd >= 0) close(dev->fd);
    dev->fd = -1;
}

// Write a single byte to a register
static inline int i2c_write_reg(i2c_dev_t *dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    if (write(dev->fd, buf, 2) != 2) {
        fprintf(stderr, "i2c_write_reg: reg=0x%02X val=0x%02X failed: %s\n",
                reg, val, strerror(errno));
        return -1;
    }
    return 0;
}

// Read a single byte from a register
static inline int i2c_read_reg(i2c_dev_t *dev, uint8_t reg, uint8_t *val) {
    if (write(dev->fd, &reg, 1) != 1) {
        fprintf(stderr, "i2c_read_reg: write reg addr failed\n");
        return -1;
    }
    if (read(dev->fd, val, 1) != 1) {
        fprintf(stderr, "i2c_read_reg: read failed\n");
        return -1;
    }
    return 0;
}

// Read-modify-write: update only specified bits
static inline int i2c_rmw_reg(i2c_dev_t *dev, uint8_t reg,
                               uint8_t mask, uint8_t val) {
    uint8_t current;
    if (i2c_read_reg(dev, reg, &current) < 0) return -1;
    current = (current & ~mask) | (val & mask);
    return i2c_write_reg(dev, reg, current);
}

// Burst write: write N bytes starting at reg (auto-increment assumed)
static inline int i2c_write_burst(i2c_dev_t *dev, uint8_t reg,
                                  const uint8_t *data, size_t len) {
    uint8_t buf[256];
    if (len + 1 > sizeof(buf)) return -1;
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    if (write(dev->fd, buf, len + 1) != (ssize_t)(len + 1)) {
        perror("i2c_write_burst");
        return -1;
    }
    return 0;
}

#endif // I2C_CODEC_H
```

---

### Codec Initialization Sequence

```c
// codec_wm8960.h — Register definitions for Wolfson WM8960
#ifndef CODEC_WM8960_H
#define CODEC_WM8960_H

// WM8960 I2C address (ADCDAT pin selects LSB)
#define WM8960_I2C_ADDR_0   0x1A    // ADDR pin = 0
#define WM8960_I2C_ADDR_1   0x1B    // ADDR pin = 1

// WM8960 Register Addresses (7-bit address, 9-bit data — uses SMBus word writes)
#define WM8960_REG_LEFT_INPUT_VOL       0x00
#define WM8960_REG_RIGHT_INPUT_VOL      0x01
#define WM8960_REG_LOUT1_VOL            0x02
#define WM8960_REG_ROUT1_VOL            0x03
#define WM8960_REG_CLOCKING_1           0x04
#define WM8960_REG_ADC_DAC_CTL          0x05
#define WM8960_REG_AUDIO_IFACE          0x07
#define WM8960_REG_CLOCKING_2           0x08
#define WM8960_REG_AUDIO_IFACE_2        0x09
#define WM8960_REG_LEFT_DAC_VOL         0x0A
#define WM8960_REG_RIGHT_DAC_VOL        0x0B
#define WM8960_REG_RESET                0x0F
#define WM8960_REG_3D_CTL               0x10
#define WM8960_REG_ALC_1                0x11
#define WM8960_REG_ALC_2                0x12
#define WM8960_REG_ALC_3                0x13
#define WM8960_REG_NOISE_GATE           0x14
#define WM8960_REG_LEFT_ADC_VOL         0x15
#define WM8960_REG_RIGHT_ADC_VOL        0x16
#define WM8960_REG_ADDITIONAL_CTL_1     0x17
#define WM8960_REG_ADDITIONAL_CTL_2     0x18
#define WM8960_REG_PWR_MGMT_1          0x19
#define WM8960_REG_PWR_MGMT_2          0x1A
#define WM8960_REG_ADDITIONAL_CTL_3     0x1B
#define WM8960_REG_ANTI_POP_1          0x1C
#define WM8960_REG_ANTI_POP_2          0x1D
#define WM8960_REG_LEFT_MIXER_1         0x22
#define WM8960_REG_LEFT_MIXER_2         0x23
#define WM8960_REG_RIGHT_MIXER_1        0x24
#define WM8960_REG_RIGHT_MIXER_2        0x25
#define WM8960_REG_LOUT2_VOL            0x28
#define WM8960_REG_ROUT2_VOL            0x29
#define WM8960_REG_PWR_MGMT_3          0x2F
#define WM8960_REG_PLL_1                0x34
#define WM8960_REG_PLL_2                0x35
#define WM8960_REG_PLL_3                0x36
#define WM8960_REG_PLL_4                0x37

// Bit masks for PWR_MGMT_1 (0x19)
#define WM8960_VMID_SEL_MASK    0x180   // Bits 8:7
#define WM8960_VMID_50K         0x080
#define WM8960_VMID_250K        0x100
#define WM8960_VMID_5K          0x180
#define WM8960_VREF             (1 << 6)
#define WM8960_AINL             (1 << 5)
#define WM8960_AINR             (1 << 4)
#define WM8960_ADCL             (1 << 3)
#define WM8960_ADCR             (1 << 2)
#define WM8960_MICB             (1 << 1)
#define WM8960_DIGENB           (1 << 0)

// Bit masks for PWR_MGMT_2 (0x1A)
#define WM8960_DACL             (1 << 8)
#define WM8960_DACR             (1 << 7)
#define WM8960_LOUT1            (1 << 6)
#define WM8960_ROUT1            (1 << 5)
#define WM8960_SPKL             (1 << 4)
#define WM8960_SPKR             (1 << 3)
#define WM8960_OUT3             (1 << 1)
#define WM8960_PLL_EN           (1 << 0)

// Audio Interface (REG 0x07) bits
#define WM8960_ALRSWAP          (1 << 8)
#define WM8960_BCLKINV          (1 << 7)
#define WM8960_MS               (1 << 6)   // 1=master, 0=slave
#define WM8960_DLRSWAP          (1 << 5)
#define WM8960_LRP              (1 << 4)
#define WM8960_WL_16BIT         (0 << 2)
#define WM8960_WL_20BIT         (1 << 2)
#define WM8960_WL_24BIT         (2 << 2)
#define WM8960_WL_32BIT         (3 << 2)
#define WM8960_FORMAT_I2S       (2 << 0)
#define WM8960_FORMAT_LEFT_J    (1 << 0)
#define WM8960_FORMAT_PCM_A     (3 << 0)

#endif // CODEC_WM8960_H
```

> **Note:** The WM8960 uses 9-bit register data packed into SMBus word writes. The upper 2 bits of each word carry the register address bits [0], and the remaining bits carry the 9-bit value. The Linux i2c-dev `i2c_smbus_write_word_data()` call handles this, or you can construct the 2-byte payload manually.

---

### Volume and Gain Control

```c
#include <stdint.h>
#include "i2c_codec.h"
#include "codec_wm8960.h"

// WM8960 uses 9-bit registers. Pack reg(7-bit) + val(9-bit) into 2 bytes.
// Byte 0: reg[6:0] | val[8]
// Byte 1: val[7:0]
static int wm8960_write(i2c_dev_t *dev, uint8_t reg, uint16_t val) {
    uint8_t buf[2];
    buf[0] = ((reg & 0x7F) << 1) | ((val >> 8) & 0x01);
    buf[1] = (uint8_t)(val & 0xFF);
    if (write(dev->fd, buf, 2) != 2) {
        fprintf(stderr, "wm8960_write: reg=0x%02X val=0x%03X failed\n", reg, val);
        return -1;
    }
    return 0;
}

// Set DAC output volume: 0 = -127 dB, 255 = +0 dB
// val range: 0x00–0xFF
int wm8960_set_dac_volume(i2c_dev_t *dev, uint8_t left_vol, uint8_t right_vol) {
    // Bit 8 (DACVU) = volume update enable; write left first, then right with update
    int ret = 0;
    ret |= wm8960_write(dev, WM8960_REG_LEFT_DAC_VOL,  (uint16_t)left_vol);
    ret |= wm8960_write(dev, WM8960_REG_RIGHT_DAC_VOL, (uint16_t)right_vol | 0x100);
    return ret;
}

// Set headphone output volume: 0x30 = -73 dB, 0x7F = +6 dB
int wm8960_set_hp_volume(i2c_dev_t *dev, uint8_t left_vol, uint8_t right_vol) {
    int ret = 0;
    ret |= wm8960_write(dev, WM8960_REG_LOUT1_VOL, (uint16_t)left_vol & 0x7F);
    ret |= wm8960_write(dev, WM8960_REG_ROUT1_VOL, ((uint16_t)right_vol & 0x7F) | 0x100);
    return ret;
}

// Set microphone PGA input gain: 0x00 = 0 dB, 0x3F = +63 dB (1 dB steps)
int wm8960_set_mic_gain(i2c_dev_t *dev, uint8_t gain_db) {
    if (gain_db > 63) gain_db = 63;
    int ret = 0;
    // Bit 8 = IPVU (input PGA volume update)
    ret |= wm8960_write(dev, WM8960_REG_LEFT_INPUT_VOL,  0x100 | (gain_db & 0x3F));
    ret |= wm8960_write(dev, WM8960_REG_RIGHT_INPUT_VOL, 0x100 | (gain_db & 0x3F));
    return ret;
}

// Mute/unmute DAC
int wm8960_dac_mute(i2c_dev_t *dev, bool mute) {
    // ADC_DAC_CTL reg 0x05, bit 3 = DACMU
    uint16_t val = mute ? (1 << 3) : 0;
    return wm8960_write(dev, WM8960_REG_ADC_DAC_CTL, val);
}
```

---

### Sample Rate and Format Configuration

```c
// Configure WM8960 for 48 kHz, I2S, 16-bit, codec slave mode
// Assumes MCLK = 12.288 MHz (= 48000 * 256)
int wm8960_configure_48k_i2s_slave(i2c_dev_t *dev) {
    int ret = 0;

    // Audio interface: I2S format, 16-bit, slave mode (MS=0)
    ret |= wm8960_write(dev, WM8960_REG_AUDIO_IFACE,
                        WM8960_FORMAT_I2S | WM8960_WL_16BIT);

    // Clocking: SYSCLK from MCLK directly (no PLL), sample rate = 48 kHz
    // CLKDIV2=0 (SYSCLK = MCLK), SR[4:2] = 000 for 48kHz with 256*Fs MCLK
    ret |= wm8960_write(dev, WM8960_REG_CLOCKING_1, 0x000);

    return ret;
}

// Configure WM8960 PLL for 44.1 kHz from 12 MHz MCLK
// Target: SYSCLK = 11.2896 MHz = 44100 * 256
// PLL: Fout = 11.2896 MHz, Fin = 12 MHz
// N = 7, K = 0x86C226 (fractional)
int wm8960_configure_pll_44k1(i2c_dev_t *dev) {
    int ret = 0;

    // Enable PLL
    // PLL_1: PLLEN=1, SDM=1 (fractional), PLLPRESCALE=0 (÷1), PLLN[3:0]=7
    ret |= wm8960_write(dev, WM8960_REG_PLL_1, 0x0037);

    // PLLK[23:16] = 0x86
    ret |= wm8960_write(dev, WM8960_REG_PLL_2, 0x0086);

    // PLLK[15:8] = 0xC2
    ret |= wm8960_write(dev, WM8960_REG_PLL_3, 0x00C2);

    // PLLK[7:0] = 0x26
    ret |= wm8960_write(dev, WM8960_REG_PLL_4, 0x0026);

    // Clocking: use PLL output as SYSCLK, CLKDIV2=0
    ret |= wm8960_write(dev, WM8960_REG_CLOCKING_1, 0x001); // CLKSEL=1 (PLL)

    // Audio interface: I2S, 16-bit, slave
    ret |= wm8960_write(dev, WM8960_REG_AUDIO_IFACE,
                        WM8960_FORMAT_I2S | WM8960_WL_16BIT);
    return ret;
}
```

---

### Power Management

```c
// Proper power-up sequence for WM8960 (avoids audible pops)
int wm8960_powerup_sequence(i2c_dev_t *dev) {
    int ret = 0;

    // Step 1: Enable VMID with high resistance (slow charge)
    ret |= wm8960_write(dev, WM8960_REG_PWR_MGMT_1,
                        WM8960_VMID_50K | WM8960_VREF);

    // Step 2: Wait for VMID to stabilize (typ. 400 ms for anti-pop)
    usleep(400000);

    // Step 3: Enable analog inputs and bias
    ret |= wm8960_write(dev, WM8960_REG_PWR_MGMT_1,
                        WM8960_VMID_250K | WM8960_VREF |
                        WM8960_AINL | WM8960_AINR | WM8960_MICB);

    // Step 4: Enable ADC and DAC
    ret |= wm8960_write(dev, WM8960_REG_PWR_MGMT_1,
                        WM8960_VMID_250K | WM8960_VREF |
                        WM8960_AINL | WM8960_AINR | WM8960_MICB |
                        WM8960_ADCL | WM8960_ADCR);

    ret |= wm8960_write(dev, WM8960_REG_PWR_MGMT_2,
                        WM8960_DACL | WM8960_DACR |
                        WM8960_LOUT1 | WM8960_ROUT1);

    // Step 5: Enable output mixers (PWR_MGMT_3)
    ret |= wm8960_write(dev, WM8960_REG_PWR_MGMT_3, 0x00C); // LOMIX + ROMIX

    // Step 6: Connect DAC to output mixers
    ret |= wm8960_write(dev, WM8960_REG_LEFT_MIXER_1,  0x100); // LD2LO
    ret |= wm8960_write(dev, WM8960_REG_RIGHT_MIXER_2, 0x100); // RD2RO

    // Step 7: Unmute DAC
    ret |= wm8960_dac_mute(dev, false);

    // Step 8: Set default volumes
    ret |= wm8960_set_dac_volume(dev, 0xFF, 0xFF);   // 0 dB
    ret |= wm8960_set_hp_volume(dev, 0x79, 0x79);    // 0 dB headphone

    return ret;
}

// Controlled power-down (avoids pops)
int wm8960_powerdown_sequence(i2c_dev_t *dev) {
    int ret = 0;

    // Mute first
    ret |= wm8960_dac_mute(dev, true);
    usleep(10000); // 10 ms

    // Disable outputs
    ret |= wm8960_write(dev, WM8960_REG_PWR_MGMT_2, 0x000);

    // Disable ADC/DAC
    ret |= wm8960_write(dev, WM8960_REG_PWR_MGMT_3, 0x000);

    // Disable VREF and VMID last
    ret |= wm8960_write(dev, WM8960_REG_PWR_MGMT_1, 0x000);

    return ret;
}
```

---

### Full Codec Driver Class (C++)

```cpp
// WM8960Codec.hpp — C++ RAII driver for WM8960 audio codec
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <array>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

class WM8960Codec {
public:
    enum class SampleRate : uint8_t {
        SR_8000  = 0,
        SR_11025 = 1,
        SR_12000 = 2,
        SR_16000 = 3,
        SR_22050 = 4,
        SR_24000 = 5,
        SR_32000 = 6,
        SR_44100 = 7,
        SR_48000 = 8,
        SR_88200 = 9,
        SR_96000 = 10,
    };

    enum class WordLength : uint16_t {
        BITS_16 = 0x00,
        BITS_20 = 0x04,
        BITS_24 = 0x08,
        BITS_32 = 0x0C,
    };

    explicit WM8960Codec(const std::string &bus, uint8_t addr = 0x1A)
        : i2c_addr_(addr)
    {
        fd_ = open(bus.c_str(), O_RDWR);
        if (fd_ < 0)
            throw std::runtime_error("Cannot open I2C bus: " + bus);

        if (ioctl(fd_, I2C_SLAVE, addr) < 0) {
            close(fd_);
            throw std::runtime_error("Cannot set I2C slave address");
        }

        // Software reset
        writeReg(0x0F, 0x000);
        usleep(10000);

        // Load register shadow with reset defaults
        regs_.fill(0);
    }

    ~WM8960Codec() {
        if (fd_ >= 0) close(fd_);
    }

    // Non-copyable, movable
    WM8960Codec(const WM8960Codec &) = delete;
    WM8960Codec &operator=(const WM8960Codec &) = delete;

    void configure(SampleRate sr, WordLength wl, bool masterMode = false) {
        uint16_t iface_reg = 0x002; // I2S format
        iface_reg |= static_cast<uint16_t>(wl);
        if (masterMode) iface_reg |= (1 << 6);

        writeReg(0x07, iface_reg);
        configureClock(sr);
    }

    void setDacVolume(float db_left, float db_right) {
        // WM8960 DAC volume: 0xFF = 0 dB, each step = 0.5 dB, min = 0x01 = -127 dB
        uint8_t l = dbToVolReg(db_left, -127.0f, 0.0f, 0.5f, 0x01, 0xFF);
        uint8_t r = dbToVolReg(db_right, -127.0f, 0.0f, 0.5f, 0x01, 0xFF);
        writeReg(0x0A, l);
        writeReg(0x0B, (uint16_t)r | 0x100); // DACVU on right channel write
    }

    void setHeadphoneVolume(float db_left, float db_right) {
        // HP vol: 0x30 = -73 dB, 0x7F = +6 dB, steps = 1 dB
        uint8_t l = dbToVolReg(db_left, -73.0f, 6.0f, 1.0f, 0x30, 0x7F);
        uint8_t r = dbToVolReg(db_right, -73.0f, 6.0f, 1.0f, 0x30, 0x7F);
        writeReg(0x02, l);
        writeReg(0x03, (uint16_t)r | 0x100);
    }

    void mute(bool enable) {
        // DACMU bit in ADC_DAC_CTL reg
        updateReg(0x05, 0x008, enable ? 0x008 : 0x000);
    }

    void powerUp() {
        // Enable VMID + VREF
        writeReg(0x19, 0x0C0); // VMID_50K + VREF
        usleep(400000);
        // Enable all blocks
        writeReg(0x19, 0x0FE);
        writeReg(0x1A, 0x1E0); // DACL+DACR+LOUT1+ROUT1
        writeReg(0x2F, 0x00C); // LOMIX+ROMIX
        writeReg(0x22, 0x100); // DAC -> Left mixer
        writeReg(0x25, 0x100); // DAC -> Right mixer
        mute(false);
    }

    void powerDown() {
        mute(true);
        usleep(10000);
        writeReg(0x1A, 0x000);
        writeReg(0x2F, 0x000);
        writeReg(0x19, 0x000);
    }

private:
    int fd_;
    uint8_t i2c_addr_;
    std::array<uint16_t, 56> regs_; // Shadow register cache

    void writeReg(uint8_t reg, uint16_t val) {
        uint8_t buf[2];
        buf[0] = ((reg & 0x7F) << 1) | ((val >> 8) & 0x01);
        buf[1] = static_cast<uint8_t>(val & 0xFF);
        if (write(fd_, buf, 2) != 2)
            throw std::runtime_error("I2C write failed");
        if (reg < regs_.size()) regs_[reg] = val;
    }

    void updateReg(uint8_t reg, uint16_t mask, uint16_t val) {
        uint16_t current = (reg < regs_.size()) ? regs_[reg] : 0;
        writeReg(reg, (current & ~mask) | (val & mask));
    }

    void configureClock(SampleRate sr) {
        // For simplicity: assume MCLK = 12.288 MHz for 48 kHz family,
        // 11.2896 MHz for 44.1 kHz family. PLL config omitted for brevity.
        switch (sr) {
            case SampleRate::SR_48000:
                writeReg(0x04, 0x000); break;
            case SampleRate::SR_44100:
                writeReg(0x04, 0x000); break;
            default:
                break;
        }
    }

    static uint8_t dbToVolReg(float db, float min_db, float max_db,
                               float step, uint8_t reg_min, uint8_t reg_max) {
        if (db <= min_db) return reg_min;
        if (db >= max_db) return reg_max;
        float steps = (db - min_db) / step;
        int reg = reg_min + static_cast<int>(steps + 0.5f);
        if (reg > reg_max) reg = reg_max;
        return static_cast<uint8_t>(reg);
    }
};
```

---

## Rust Implementation

### I2C Abstraction with embedded-hal

```toml
# Cargo.toml
[package]
name = "audio-codec-i2c"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal = "1.0"
linux-embedded-hal = "0.4"
thiserror = "1.0"
log = "0.4"
```

```rust
// src/i2c_hal.rs — Thin wrapper around embedded-hal I2C for codec drivers
use embedded_hal::i2c::{I2c, SevenBitAddress};

/// Writes a single byte to a codec register
pub fn write_reg<I2C, E>(
    i2c: &mut I2C,
    addr: SevenBitAddress,
    reg: u8,
    val: u8,
) -> Result<(), E>
where
    I2C: I2c<SevenBitAddress, Error = E>,
{
    i2c.write(addr, &[reg, val])
}

/// Reads a single byte from a codec register
pub fn read_reg<I2C, E>(
    i2c: &mut I2C,
    addr: SevenBitAddress,
    reg: u8,
) -> Result<u8, E>
where
    I2C: I2c<SevenBitAddress, Error = E>,
{
    let mut buf = [0u8; 1];
    i2c.write_read(addr, &[reg], &mut buf)?;
    Ok(buf[0])
}

/// Read-modify-write: update only the bits specified by `mask`
pub fn rmw_reg<I2C, E>(
    i2c: &mut I2C,
    addr: SevenBitAddress,
    reg: u8,
    mask: u8,
    val: u8,
) -> Result<(), E>
where
    I2C: I2c<SevenBitAddress, Error = E>,
{
    let current = read_reg(i2c, addr, reg)?;
    let new_val = (current & !mask) | (val & mask);
    write_reg(i2c, addr, reg, new_val)
}

/// Write a 16-bit value packed as two bytes (for 9-bit register codecs like WM8960)
pub fn write_reg16<I2C, E>(
    i2c: &mut I2C,
    addr: SevenBitAddress,
    reg: u8,
    val: u16,
) -> Result<(), E>
where
    I2C: I2c<SevenBitAddress, Error = E>,
{
    let byte0 = ((reg & 0x7F) << 1) | (((val >> 8) & 0x01) as u8);
    let byte1 = (val & 0xFF) as u8;
    i2c.write(addr, &[byte0, byte1])
}
```

---

### Codec Register Map in Rust

```rust
// src/wm8960_regs.rs — Type-safe register definitions
#![allow(non_upper_case_globals)]
use bitflags::bitflags;

/// WM8960 register addresses
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Reg {
    LeftInputVol    = 0x00,
    RightInputVol   = 0x01,
    Lout1Vol        = 0x02,
    Rout1Vol        = 0x03,
    Clocking1       = 0x04,
    AdcDacCtl       = 0x05,
    AudioIface      = 0x07,
    Clocking2       = 0x08,
    LeftDacVol      = 0x0A,
    RightDacVol     = 0x0B,
    Reset           = 0x0F,
    PwrMgmt1        = 0x19,
    PwrMgmt2        = 0x1A,
    AntiPop1        = 0x1C,
    AntiPop2        = 0x1D,
    LeftMixer1      = 0x22,
    LeftMixer2      = 0x23,
    RightMixer1     = 0x24,
    RightMixer2     = 0x25,
    Lout2Vol        = 0x28,
    Rout2Vol        = 0x29,
    PwrMgmt3        = 0x2F,
    Pll1            = 0x34,
    Pll2            = 0x35,
    Pll3            = 0x36,
    Pll4            = 0x37,
}

bitflags! {
    /// Power Management 1 register bits
    #[derive(Debug, Clone, Copy)]
    pub struct PwrMgmt1Flags: u16 {
        const VMID_50K   = 0x080;
        const VMID_250K  = 0x100;
        const VMID_5K    = 0x180;
        const VREF       = 0x040;
        const AINL       = 0x020;
        const AINR       = 0x010;
        const ADCL       = 0x008;
        const ADCR       = 0x004;
        const MICB       = 0x002;
    }
}

bitflags! {
    /// Power Management 2 register bits
    #[derive(Debug, Clone, Copy)]
    pub struct PwrMgmt2Flags: u16 {
        const DACL  = 0x100;
        const DACR  = 0x080;
        const LOUT1 = 0x040;
        const ROUT1 = 0x020;
        const SPKL  = 0x010;
        const SPKR  = 0x008;
    }
}

/// Audio interface format
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AudioFormat {
    RightJustified = 0x00,
    LeftJustified  = 0x01,
    I2S            = 0x02,
    PcmA           = 0x03,
}

/// PCM word length
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WordLength {
    Bits16 = 0x00,
    Bits20 = 0x04,
    Bits24 = 0x08,
    Bits32 = 0x0C,
}
```

---

### Initialization and Configuration

```rust
// src/codec.rs — Main codec driver
use embedded_hal::i2c::{I2c, SevenBitAddress};
use std::thread::sleep;
use std::time::Duration;
use crate::i2c_hal::{write_reg16, read_reg};
use crate::wm8960_regs::*;

#[derive(thiserror::Error, Debug)]
pub enum CodecError<E: std::fmt::Debug> {
    #[error("I2C bus error: {0:?}")]
    I2c(E),
    #[error("Invalid parameter: {0}")]
    InvalidParam(&'static str),
    #[error("Device not found or wrong ID")]
    DeviceNotFound,
}

pub struct Wm8960<I2C> {
    i2c: I2C,
    addr: SevenBitAddress,
    /// Shadow copy of write-only registers
    reg_cache: [u16; 56],
}

impl<I2C, E> Wm8960<I2C>
where
    I2C: I2c<SevenBitAddress, Error = E>,
    E: std::fmt::Debug,
{
    pub fn new(i2c: I2C, addr: SevenBitAddress) -> Result<Self, CodecError<E>> {
        let mut codec = Wm8960 {
            i2c,
            addr,
            reg_cache: [0u16; 56],
        };

        // Software reset
        codec.write_reg(Reg::Reset as u8, 0x000)?;
        sleep(Duration::from_millis(10));

        Ok(codec)
    }

    fn write_reg(&mut self, reg: u8, val: u16) -> Result<(), CodecError<E>> {
        write_reg16(&mut self.i2c, self.addr, reg, val)
            .map_err(CodecError::I2c)?;
        if (reg as usize) < self.reg_cache.len() {
            self.reg_cache[reg as usize] = val;
        }
        Ok(())
    }

    fn update_reg(&mut self, reg: u8, mask: u16, val: u16) -> Result<(), CodecError<E>> {
        let current = if (reg as usize) < self.reg_cache.len() {
            self.reg_cache[reg as usize]
        } else {
            0
        };
        self.write_reg(reg, (current & !mask) | (val & mask))
    }

    /// Configure audio interface format and word length (slave mode)
    pub fn configure_interface(
        &mut self,
        fmt: AudioFormat,
        wl: WordLength,
        master: bool,
    ) -> Result<(), CodecError<E>> {
        let mut val = (fmt as u16) | (wl as u16);
        if master { val |= 1 << 6; }
        self.write_reg(Reg::AudioIface as u8, val)
    }

    /// Set DAC volume in dB (-127.0 to 0.0 dB, 0.5 dB steps)
    pub fn set_dac_volume(&mut self, left_db: f32, right_db: f32)
        -> Result<(), CodecError<E>>
    {
        let l = Self::db_to_dac_reg(left_db);
        let r = Self::db_to_dac_reg(right_db);
        self.write_reg(Reg::LeftDacVol as u8, l as u16)?;
        // DACVU bit triggers both channels to update simultaneously
        self.write_reg(Reg::RightDacVol as u8, (r as u16) | 0x100)?;
        Ok(())
    }

    /// Set headphone volume (-73.0 to +6.0 dB, 1 dB steps)
    pub fn set_hp_volume(&mut self, left_db: f32, right_db: f32)
        -> Result<(), CodecError<E>>
    {
        let l = Self::db_to_hp_reg(left_db);
        let r = Self::db_to_hp_reg(right_db);
        self.write_reg(Reg::Lout1Vol as u8, l as u16)?;
        self.write_reg(Reg::Rout1Vol as u8, (r as u16) | 0x100)?;
        Ok(())
    }

    /// Mute or unmute the DAC
    pub fn set_mute(&mut self, muted: bool) -> Result<(), CodecError<E>> {
        self.update_reg(Reg::AdcDacCtl as u8, 0x008,
                        if muted { 0x008 } else { 0x000 })
    }

    /// Full power-up sequence with anti-pop
    pub fn power_up(&mut self) -> Result<(), CodecError<E>> {
        use PwrMgmt1Flags as P1;
        use PwrMgmt2Flags as P2;

        // Phase 1: Charge VMID slowly to reduce pop
        self.write_reg(Reg::PwrMgmt1 as u8,
                       (P1::VMID_50K | P1::VREF).bits())?;
        sleep(Duration::from_millis(400));

        // Phase 2: Switch to normal VMID impedance, enable analog inputs
        self.write_reg(Reg::PwrMgmt1 as u8,
            (P1::VMID_250K | P1::VREF | P1::AINL | P1::AINR |
             P1::ADCL | P1::ADCR | P1::MICB).bits())?;

        // Phase 3: Enable DAC + output stages
        self.write_reg(Reg::PwrMgmt2 as u8,
            (P2::DACL | P2::DACR | P2::LOUT1 | P2::ROUT1).bits())?;

        // Phase 4: Enable output mixers
        self.write_reg(Reg::PwrMgmt3 as u8, 0x00C)?;

        // Phase 5: Route DAC to output mixers
        self.write_reg(Reg::LeftMixer1 as u8, 0x100)?;  // LD2LO
        self.write_reg(Reg::RightMixer2 as u8, 0x100)?; // RD2RO

        // Phase 6: Unmute at nominal volume
        self.set_mute(false)?;
        self.set_dac_volume(0.0, 0.0)?;
        self.set_hp_volume(0.0, 0.0)?;

        Ok(())
    }

    /// Controlled power-down
    pub fn power_down(&mut self) -> Result<(), CodecError<E>> {
        self.set_mute(true)?;
        sleep(Duration::from_millis(10));
        self.write_reg(Reg::PwrMgmt2 as u8, 0x000)?;
        self.write_reg(Reg::PwrMgmt3 as u8, 0x000)?;
        self.write_reg(Reg::PwrMgmt1 as u8, 0x000)?;
        Ok(())
    }

    fn db_to_dac_reg(db: f32) -> u8 {
        if db <= -127.0 { return 0x01; }
        if db >= 0.0    { return 0xFF; }
        let steps = (db + 127.0) / 0.5;
        (0x01 + steps.round() as u8).min(0xFF)
    }

    fn db_to_hp_reg(db: f32) -> u8 {
        if db <= -73.0 { return 0x30; }
        if db >= 6.0   { return 0x7F; }
        let steps = (db + 73.0).round() as u8;
        0x30u8.saturating_add(steps).min(0x7F)
    }
}
```

---

### Error Handling Patterns

```rust
// src/main.rs — Example usage with proper error propagation
use linux_embedded_hal::I2cdev;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open Linux I2C bus
    let i2c = I2cdev::new("/dev/i2c-1")?;

    // Construct codec driver (resets the device on creation)
    let mut codec = Wm8960::new(i2c, 0x1A)
        .map_err(|e| format!("Codec init failed: {:?}", e))?;

    // Configure for 48 kHz I2S 16-bit slave mode
    codec.configure_interface(
        AudioFormat::I2S,
        WordLength::Bits16,
        false, // slave mode
    )?;

    // Power up with anti-pop sequence
    codec.power_up()?;

    // Set volumes
    codec.set_dac_volume(-6.0, -6.0)?;   // -6 dB DAC
    codec.set_hp_volume(-3.0, -3.0)?;    // -3 dB headphone

    println!("Codec configured and running.");

    // ... audio playback happens on I2S bus ...

    // Clean shutdown
    codec.power_down()?;

    Ok(())
}
```

---

### Full Rust Codec Driver

```rust
// src/tlv320aic3x.rs — TI TLV320AIC3104 codec driver with page switching
// Demonstrates paged register map handling

use embedded_hal::i2c::{I2c, SevenBitAddress};
use std::thread::sleep;
use std::time::Duration;

const TLV320_PAGE_SELECT: u8 = 0x00;

/// Page 0 register addresses
mod page0 {
    pub const SOFT_RESET:        u8 = 0x01;
    pub const CODEC_DATAPATH:    u8 = 0x07;
    pub const AUDIO_SERIAL_CTRL: u8 = 0x08;
    pub const AUDIO_SERIAL_CTRL2: u8 = 0x09;
    pub const PLL_PROG_A:        u8 = 0x03;
    pub const PLL_PROG_B:        u8 = 0x04;
    pub const NDAC:              u8 = 0x0B;
    pub const MDAC:              u8 = 0x0C;
    pub const NADC:              u8 = 0x12;
    pub const MADC:              u8 = 0x13;
    pub const DAC_CHANNEL_SETUP: u8 = 0x3F;
    pub const LEFT_DAC_VOL:      u8 = 0x41;
    pub const RIGHT_DAC_VOL:     u8 = 0x42;
    pub const LEFT_AGC_A:        u8 = 0x56;
    pub const HEADSET_DETECT:    u8 = 0x67;
}

/// Page 1 register addresses
mod page1 {
    pub const HP_DRIVER_CTRL:    u8 = 0x1F;
    pub const SPK_AMP_CTRL:      u8 = 0x20;
    pub const HP_VOL_CTRL:       u8 = 0x23;
    pub const SPK_VOL_CTRL:      u8 = 0x25;
    pub const INPUT_CM_SETTING:  u8 = 0x78;
}

pub struct Tlv320Aic3104<I2C> {
    i2c: I2C,
    addr: SevenBitAddress,
    current_page: u8,
}

impl<I2C, E> Tlv320Aic3104<I2C>
where
    I2C: I2c<SevenBitAddress, Error = E>,
    E: std::fmt::Debug,
{
    pub fn new(i2c: I2C, addr: SevenBitAddress) -> Self {
        Tlv320Aic3104 { i2c, addr, current_page: 0 }
    }

    /// Switch to a register page (0 or 1)
    fn set_page(&mut self, page: u8) -> Result<(), E> {
        if self.current_page != page {
            self.i2c.write(self.addr, &[TLV320_PAGE_SELECT, page])?;
            self.current_page = page;
        }
        Ok(())
    }

    /// Write to a page-0 register
    fn write_p0(&mut self, reg: u8, val: u8) -> Result<(), E> {
        self.set_page(0)?;
        self.i2c.write(self.addr, &[reg, val])
    }

    /// Write to a page-1 register
    fn write_p1(&mut self, reg: u8, val: u8) -> Result<(), E> {
        self.set_page(1)?;
        self.i2c.write(self.addr, &[reg, val])
    }

    /// Read from a page-0 register
    fn read_p0(&mut self, reg: u8) -> Result<u8, E> {
        self.set_page(0)?;
        let mut buf = [0u8];
        self.i2c.write_read(self.addr, &[reg], &mut buf)?;
        Ok(buf[0])
    }

    /// Software reset
    pub fn reset(&mut self) -> Result<(), E> {
        self.write_p0(page0::SOFT_RESET, 0x01)?;
        sleep(Duration::from_millis(10));
        self.current_page = 0;
        Ok(())
    }

    /// Configure PLL for 48 kHz from 12 MHz MCLK
    /// PLLCLK_IN = MCLK, CODEC_CLKIN = PLL_CLK
    /// P=1, R=1, J=8, D=1920 → PLLCLK = 98.304 MHz → CODEC_CLK = 98.304 MHz
    /// NDAC=2, MDAC=128 → DAC_MOD_CLK = 384000 Hz (= 48000*8)
    pub fn configure_pll_48k(&mut self) -> Result<(), E> {
        // PLL_PROG_A: PLLP=1, PLLR=1, PLLJ=8
        self.write_p0(page0::PLL_PROG_A, 0x91)?; // P=1, R=1, J=8
        // PLL_PROG_B: PLLD_MSB[5:0]=0, PLLD_LSB[7:0]=0x780 (D=1920=0x780)
        self.write_p0(page0::PLL_PROG_B, 0x07)?; // D MSB
        // (register 0x05 for D LSB)
        self.i2c.write(self.addr, &[0x05, 0x80])?;

        // NDAC = 2 (NDAC_VAL=2, NDAC_PWR=1)
        self.write_p0(page0::NDAC, 0x82)?;
        // MDAC = 128
        self.write_p0(page0::MDAC, 0x80)?;

        // Audio data word length = 16-bit, I2S, slave
        self.write_p0(page0::AUDIO_SERIAL_CTRL, 0x00)?;

        Ok(())
    }

    /// Set DAC volume: val 0x00 = 0 dB, 0xA9 = -127.5 dB, steps 0.5 dB
    pub fn set_dac_volume(&mut self, left: u8, right: u8) -> Result<(), E> {
        self.write_p0(page0::LEFT_DAC_VOL,  left)?;
        self.write_p0(page0::RIGHT_DAC_VOL, right)
    }

    /// Configure headphone driver (Page 1)
    pub fn configure_headphone(&mut self, gain_db: u8) -> Result<(), E> {
        // HP_DRIVER_CTRL: enable HP driver
        self.write_p1(page1::HP_DRIVER_CTRL, 0x04)?;

        // HP_VOL_CTRL: mute=0, gain
        let vol_reg = (gain_db & 0x3F) << 1; // bits [6:1] = gain
        self.write_p1(page1::HP_VOL_CTRL, vol_reg)
    }

    /// Full initialization
    pub fn init(&mut self) -> Result<(), E> {
        self.reset()?;
        self.configure_pll_48k()?;

        // Power up DAC
        self.write_p0(page0::DAC_CHANNEL_SETUP, 0xD4)?; // Left+Right DAC on, soft-step

        // Route DAC to outputs via mixer (register 0x25)
        self.i2c.write(self.addr, &[0x25, 0x40])?; // LDAC → Left output mixer

        self.set_dac_volume(0x00, 0x00)?;  // 0 dB
        self.configure_headphone(0)?;       // 0 dB HP gain

        Ok(())
    }
}
```

---

## Advanced Topics

### Multi-Byte Register Access

Some codec parameters span multiple registers (e.g., 16-bit sample counters or 24-bit PLL fractional values). Always write the most significant byte last, or use an atomic burst write if the codec supports it:

```c
// C: Write 24-bit PLL K value across three registers
static int write_pll_k(i2c_dev_t *dev, uint32_t k) {
    int ret = 0;
    ret |= i2c_write_reg(dev, 0x35, (k >> 16) & 0xFF); // K[23:16]
    ret |= i2c_write_reg(dev, 0x36, (k >>  8) & 0xFF); // K[15:8]
    ret |= i2c_write_reg(dev, 0x37, (k      ) & 0xFF); // K[7:0]
    return ret;
}
```

```rust
// Rust: burst write for multi-byte PLL config
fn write_pll_k<I2C: I2c<SevenBitAddress, Error = E>, E>(
    i2c: &mut I2C,
    addr: SevenBitAddress,
    k: u32,
) -> Result<(), E> {
    // Registers 0x35, 0x36, 0x37 — auto-increment assumed
    let payload = [
        0x35u8,
        ((k >> 16) & 0xFF) as u8,
        ((k >>  8) & 0xFF) as u8,
        ( k        & 0xFF) as u8,
    ];
    i2c.write(addr, &payload)
}
```

---

### Page/Bank Switching

```c
// C: RAII-style page guard for TLV320AIC3x
typedef struct {
    i2c_dev_t *dev;
    uint8_t    prev_page;
} page_guard_t;

static int switch_page(i2c_dev_t *dev, uint8_t page) {
    return i2c_write_reg(dev, 0x00, page);
}

static void page_guard_enter(page_guard_t *g, i2c_dev_t *dev,
                              uint8_t page, uint8_t current_page) {
    g->dev = dev;
    g->prev_page = current_page;
    switch_page(dev, page);
}

static void page_guard_exit(page_guard_t *g) {
    switch_page(g->dev, g->prev_page);
}

// Usage:
// page_guard_t pg;
// page_guard_enter(&pg, &dev, 1, current_page);
//   i2c_write_reg(&dev, 0x23, hp_vol);
// page_guard_exit(&pg);
```

---

### Audio PLL Configuration

The PLL translates an available MCLK frequency (e.g., 12 MHz from a crystal oscillator) to the precise SYSCLK needed for a target sample rate:

```
SYSCLK = (MCLK / P) * (N + D/10000) * R

Where:
  P = pre-divider (1 or 2)
  N = integer multiplier
  D = fractional multiplier (0–9999)
  R = post-multiplier (1, 2, 4, or 8)
```

For 48 kHz with SYSCLK = 12.288 MHz (= 48000 × 256) from MCLK = 12 MHz:
- P=1, R=1, N=8, D=1920 → 12 MHz × 8.192 = 98.304 MHz → divide by 8 = 12.288 MHz

```c
// C: Compute and program PLL for target SYSCLK
typedef struct {
    uint8_t  P;    // Pre-divider
    uint8_t  R;    // Post-multiplier
    uint8_t  J;    // Integer part of N
    uint16_t D;    // Fractional part (0-9999)
} pll_config_t;

// Pre-computed table for common rates with 12 MHz MCLK
static const pll_config_t pll_table[] = {
    // sample_rate, SYSCLK,   P, R,  J,    D
    { 1, 1,  8, 1920 }, // 48.0 kHz → SYSCLK = 12.288 MHz
    { 1, 1,  7, 5264 }, // 44.1 kHz → SYSCLK = 11.2896 MHz
    { 1, 1,  6, 8267 }, // 32.0 kHz → SYSCLK =  8.192 MHz
};
```

---

### Dynamic Reconfiguration (Mute/Unmute)

Changing sample rates or routing while audio is playing causes audible artifacts. The proper sequence is:

```c
// C: Safe dynamic reconfiguration
int codec_reconfigure(i2c_dev_t *dev, uint32_t new_sample_rate) {
    // 1. Soft-mute DAC
    i2c_write_reg(dev, WM8960_REG_ADC_DAC_CTL, 0x08); // DACMU=1

    // 2. Disable outputs
    i2c_write_reg(dev, WM8960_REG_PWR_MGMT_2, 0x180); // Keep DAC, disable HP

    // 3. Stop clocks (CLKDIV2 or PLL_EN = 0)
    i2c_write_reg(dev, WM8960_REG_CLOCKING_1, 0x000);

    // 4. Apply new configuration
    if (new_sample_rate == 44100)
        wm8960_configure_pll_44k1(dev);
    else
        wm8960_configure_48k_i2s_slave(dev);

    // 5. Re-enable outputs
    i2c_write_reg(dev, WM8960_REG_PWR_MGMT_2, 0x1E0);

    // 6. Unmute
    usleep(1000); // brief settling
    i2c_write_reg(dev, WM8960_REG_ADC_DAC_CTL, 0x000); // DACMU=0

    return 0;
}
```

---

### Interrupt Handling via GPIO

Some codecs assert an interrupt pin (active low) for events like headphone insertion, jack detection, overcurrent, or PLL lock. The GPIO IRQ should trigger an I2C status register read:

```c
// Interrupt status register read (codec-specific)
void codec_irq_handler(i2c_dev_t *dev) {
    uint8_t status;
    i2c_read_reg(dev, 0x67, &status); // Headset detect / interrupt status

    if (status & 0x01) {
        printf("Headphone inserted\n");
        // Re-route output, update impedance detection
    }
    if (status & 0x02) {
        printf("Microphone inserted\n");
        // Enable microphone bias, update routing
    }

    // Clear interrupt by writing 1 to status bits
    i2c_write_reg(dev, 0x67, status & 0x03);
}
```

---

## Real-World Codec Examples

### TI TLV320AIC3x Series

- **Interface:** I2C control (address: 0x18/0x19/0x1A/0x1B via ADDR pins), I2S/TDM audio
- **Register map:** 2-page architecture (128 regs/page)
- **Key feature:** Miniature DSP with biquad filters, dynamic range compression, de-emphasis
- **Common use:** BeagleBone Black (AIC3104), automotive head units, portable audio

```c
// TLV320 — enable stereo differential microphone input
void tlv320_enable_mic_differential(i2c_dev_t *dev) {
    // Page 0, reg 0x0F: Left input level, select MIC1LP/MIC1LM differential
    i2c_write_reg(dev, 0x0F, 0x04); // MIC1LP to left ADC, 0 dB
    i2c_write_reg(dev, 0x11, 0x04); // MIC1RP to right ADC, 0 dB
    // Enable MIC bias at AVDD (reg 0x19)
    i2c_write_reg(dev, 0x19, 0x04); // MICBIAS = AVDD
}
```

### Cirrus Logic CS42L52

- **Interface:** I2C (0x4A/0x4B), I2S audio
- **Notable:** Integrated Class D speaker amplifier + headphone amp in single IC
- **Register map:** Flat, 8-bit addresses 0x01–0x34

```c
// CS42L52 — Basic stereo playback init (simplified)
void cs42l52_init(i2c_dev_t *dev) {
    // Power control 1: power up all blocks
    i2c_write_reg(dev, 0x02, 0x00); // PDNHPA=0, PDNSPK=0, etc.
    // Interface control: I2S slave, 16-bit
    i2c_write_reg(dev, 0x06, 0x04); // I2S format
    // DAC output routing: DAC to HP and SPK
    i2c_write_reg(dev, 0x0E, 0x00); // No swap
    // Master volume: 0 dB
    i2c_write_reg(dev, 0x20, 0x00); // Master A vol = 0 dB
    i2c_write_reg(dev, 0x21, 0x00); // Master B vol = 0 dB
}
```

### Wolfson/Cirrus WM8960

- **Interface:** I2C (0x1A/0x1B), I2S audio  
- **Notable:** 9-bit register data (write-only), requires shadow cache for RMW  
- **Common use:** Raspberry Pi (via HiFiBerry, WM8960 HAT), evaluation boards  
- See full driver above for register definitions and initialization

---

## Debugging and Diagnostics

### Linux Command-Line Tools

```bash
# Scan I2C bus for devices
i2cdetect -y 1

# Read chip ID register (e.g., WM8960 reg 0x00 via SMBus word read)
i2cget -y 1 0x1a 0x00 w

# Dump all registers via i2cdump
i2cdump -y 1 0x1a b

# Write a single register (e.g., software reset)
i2cset -y 1 0x1a 0x0f 0x00 w
```

### Oscilloscope / Logic Analyzer Checks

| Signal | Expected |
|--------|---------|
| SDA/SCL | Clean transitions, proper ACK bits |
| MCLK | Correct frequency (e.g., 12.288 MHz for 48 kHz) |
| BCLK | `sample_rate × channels × bit_depth` (e.g., 3.072 MHz for 48k stereo 32-bit) |
| LRCLK | Exact sample rate (e.g., 48 kHz ±10 ppm) |
| I2S DATA | Valid PCM frames aligned to LRCLK edges |

### Common Issues and Causes

| Symptom | Likely Cause |
|---------|-------------|
| No I2C ACK | Wrong address, codec not powered, SDA/SCL swapped |
| Correct I2C but silent output | Routing not configured, DACMU=1, output mixer disabled |
| Loud hiss/noise | VMID not stable, floating input, missing bypass capacitor |
| Audible pop on power-up | VMID not pre-charged, outputs enabled before VREF stable |
| Distorted audio | Clipping (gain too high), sample rate mismatch, PLL unlocked |
| Phase/channel swap | I2S LRCLK polarity incorrect, LRP bit wrong |
| Intermittent dropout | I2C noise on long cables, missing pull-ups, shared bus contention |

---

## Summary

Audio codec configuration over I2C is a multi-layered task that combines knowledge of analog signal chains, digital audio protocols, and embedded register programming. The key principles are:

**Architecture:** Audio codecs use I2C solely for control (registers), while the actual PCM audio flows on a separate I2S/TDM bus. The I2C bus configures PLLs, gain stages, routing matrices, sample rates, and power domains.

**Register Access:** Most codecs use 8-bit register addresses with 8-bit or 9-bit data values. Complex codecs use paged register maps (e.g., TLV320AIC3x with 2 pages of 128 registers each). Read-modify-write is essential for changing individual bit fields, but write-only registers require a software shadow cache.

**Initialization Order:** The power-up sequence is critically important. VMID must be pre-charged before analog circuits are enabled; outputs should be muted during configuration and only unmuted after all settings are stable. This prevents audible pops and protects speaker drivers.

**Clock Configuration:** The PLL must be programmed to generate the precise SYSCLK needed for the target sample rate. PLL fractional dividers (N.D values) allow generation of 44.1 kHz family clocks from non-integer-multiple MCLKs.

**C/C++:** The Linux `i2c-dev` interface provides direct register access via `write()` and `read()` system calls. For microcontrollers, HAL-specific I2C APIs are used. A C++ RAII class with a register shadow cache provides clean, safe codec management.

**Rust:** The `embedded-hal` I2C trait enables portable codec drivers that work across Linux (`linux-embedded-hal`), STM32 (`stm32f4xx-hal`), nRF52, RP2040, and other platforms without code changes. Rust's type system enforces correct use of register bit fields, and `thiserror` provides ergonomic error propagation.

**Dynamic Changes:** Sample rate switching requires muting, disabling clocks, reconfiguring, re-enabling, and unmuting in strict order to avoid audible artifacts.

**Debugging:** Logic analyzers confirm correct I2C transactions and audio bus signals. Linux tools (`i2cdetect`, `i2cdump`, `i2cget`) enable rapid register inspection without writing code.

Mastering audio codec I2C configuration is essential for bringing up embedded audio subsystems in consumer electronics, automotive, IoT, and professional audio applications.

---

*Document: 93 — Audio Codec Configuration | I2C Embedded Systems Series*