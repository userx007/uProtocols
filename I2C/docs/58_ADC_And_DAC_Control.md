# 58. ADC and DAC Control via I2C

**Structure overview:**
- **Fundamentals** — resolution, reference voltage, sampling rate, single-ended vs. differential, PGA
- **I2C Protocol** — transaction patterns, register-based vs. command-based devices, address selection
- **Chip Survey** — comparison table of common I2C ADC/DAC parts (ADS1115, MCP3421, MCP4725, MCP4728, etc.)
- **ADS1115 Deep Dive** — full register map, config bit fields, MUX/PGA tables, conversion sequence
- **MCP4725 Deep Dive** — write command format, EEPROM persistence, output voltage formula

**Code examples provided:**
- **C** — low-level `i2c_helper` using Linux `/dev/i2c-X`, full ADS1115 driver with single-shot polling, full MCP4725 driver with EEPROM support, and a sine-wave sweep demo
- **C++** — RAII wrapper class around the C driver with exception-based error handling
- **Rust** — `embedded-hal`-based ADS1115 and MCP4725 drivers (portable across Linux and bare-metal), a loopback sweep test, and a continuous-mode pattern for interrupt-driven sampling

**Advanced topics** cover multi-device bus sharing (up to 16 channels), noise reduction, two-point calibration, clock stretching, output buffering, and DMA.


## Configuring and Reading from I2C Analog-to-Digital and Digital-to-Analog Converters

---

## Table of Contents

1. [Introduction](#introduction)
2. [Fundamentals: ADC and DAC Concepts](#fundamentals)
3. [I2C Protocol Recap for ADC/DAC](#i2c-protocol-recap)
4. [Common I2C ADC/DAC Chips](#common-chips)
5. [ADC over I2C: ADS1115 Deep Dive](#ads1115)
6. [DAC over I2C: MCP4725 Deep Dive](#mcp4725)
7. [C/C++ Implementation](#c-cpp)
8. [Rust Implementation](#rust)
9. [Advanced Topics](#advanced-topics)
10. [Summary](#summary)

---

## 1. Introduction

Most microcontrollers and single-board computers (SBCs) feature general-purpose digital I/O and serial interfaces but frequently lack sufficient or high-quality analog input/output channels. External **Analog-to-Digital Converters (ADCs)** and **Digital-to-Analog Converters (DACs)** connected over I2C solve this problem elegantly:

- **ADC** — Converts a continuous analog voltage or current into a discrete digital representation that the CPU can process.
- **DAC** — Takes a digital value from the CPU and converts it into a proportional analog voltage or current to drive actuators, audio signals, reference voltages, etc.

I2C is particularly well-suited for ADC/DAC peripherals because:

- Only two signal wires are needed (SDA + SCL), saving pins.
- Multiple converters can share the same bus (each with a unique 7-bit address).
- Standard speeds (100 kHz standard, 400 kHz fast mode) are more than sufficient for typical sensor sampling rates.
- Address pins on most chips allow 2–4 devices of the same type on one bus.

---

## 2. Fundamentals: ADC and DAC Concepts 

### 2.1 Resolution and Reference Voltage

Resolution is expressed in **bits**. An *n*-bit converter produces 2ⁿ discrete steps:

| Bits | Steps  | LSB at 3.3 V | LSB at 5.0 V |
|------|--------|-------------|-------------|
| 8    | 256    | ~12.9 mV    | ~19.6 mV    |
| 10   | 1 024  | ~3.2 mV     | ~4.9 mV     |
| 12   | 4 096  | ~0.81 mV    | ~1.22 mV    |
| 16   | 65 536 | ~50 µV      | ~76 µV      |

The **reference voltage (VREF)** defines the full-scale range. Many I2C ADCs use an internal reference or derive VREF from VDD.

**Formula — ADC raw to voltage:**

```
V = (raw_code / (2^bits - 1)) × VREF
```

**Formula — Voltage to DAC code:**

```
code = round((V_out / VREF) × (2^bits - 1))
```

### 2.2 Sampling Rate

The I2C bus speed limits the maximum useful sample rate. At 400 kHz fast mode, transferring a 16-bit result with addressing and ACKs takes roughly 3–4 bytes → ~60–80 µs per transaction, giving a theoretical ceiling near 12 000–16 000 samples/s. Most I2C ADCs internally configure programmable data rates and assert a conversion-ready signal (ALERT/RDY pin) when a result is available.

### 2.3 Single-Ended vs. Differential

- **Single-ended** — measures voltage relative to GND. Simple wiring, susceptible to common-mode noise.
- **Differential** — measures the voltage *difference* between two inputs. Rejects common-mode noise; required for precision measurements and current sensing (via shunt resistors).

### 2.4 Gain / Programmable Gain Amplifier (PGA)

Many I2C ADCs include an internal PGA that amplifies the input before conversion, effectively scaling the full-scale range to match small signals and increasing effective resolution.

---

## 3. I2C Protocol Recap for ADC/DAC 

### 3.1 General Transaction Pattern

```
START | ADDR(7-bit) + W | ACK | REG/CMD byte | ACK | DATA byte(s) | ACK | STOP
START | ADDR(7-bit) + R | ACK | DATA byte(s) | NAK | STOP
```

### 3.2 Register-Based vs. Command-Based Devices

| Style          | Example Chips        | Mechanism                                              |
|----------------|----------------------|-------------------------------------------------------|
| Register-based | ADS1115, MCP3421     | Write pointer to a register, then read N bytes        |
| Command-based  | MCP4725, PCF8591     | Single write transaction sets output; read returns raw|

### 3.3 Address Selection

Most chips expose 1–2 address pins (ADDR, A0, A1) wired HIGH or LOW (or to VDD/GND/SCL/SDA on some chips), selecting among 2–4 possible 7-bit base addresses:

```
ADS1115 ADDR pin → GND: 0x48 | VDD: 0x49 | SDA: 0x4A | SCL: 0x4B
MCP4725 A2 hardwired, A1:A0 selectable → 0x60–0x67
```

---

## 4. Common I2C ADC/DAC Chips 

| Part      | Type | Bits | Channels | Max Rate     | PGA | Notes                           |
|-----------|------|------|----------|--------------|-----|---------------------------------|
| ADS1115   | ADC  | 16   | 4 SE / 2 diff | 860 SPS | Yes (×⅔–×16) | Most popular general-purpose    |
| ADS1015   | ADC  | 12   | 4 SE / 2 diff | 3 300 SPS | Yes | Faster, lower-res version       |
| MCP3221   | ADC  | 12   | 1 SE    | 22.3 kSPS    | No  | Tiny SOT-23, 1-address option   |
| MCP3421   | ADC  | 18   | 1 diff  | 3.75 SPS     | Yes | Ultra-high resolution           |
| PCF8591   | ADC+DAC | 8 | 4 ADC + 1 DAC | ~11 kSPS | No | Combined, simple hobby chip  |
| MCP4725   | DAC  | 12   | 1        | ~400 kHz I2C | N/A | Has onboard EEPROM              |
| MCP4728   | DAC  | 12   | 4        | ~400 kHz I2C | N/A | Quad DAC, internal VREF option  |
| DAC7678   | DAC  | 12   | 8        | ~400 kHz I2C | N/A | Octal, rail-to-rail output      |

---

## 5. ADC over I2C: ADS1115 Deep Dive 

The **ADS1115** (Texas Instruments) is a 16-bit, 4-channel, delta-sigma ADC with an integrated PGA and comparator. It is the de-facto standard for precision I2C ADC work.

### 5.1 Register Map

| Register        | Pointer | Size   | Purpose                              |
|-----------------|---------|--------|--------------------------------------|
| Conversion      | 0x00    | 16-bit | Holds the latest conversion result   |
| Config          | 0x01    | 16-bit | Sets MUX, PGA, mode, rate, comparator|
| Lo_thresh       | 0x02    | 16-bit | Comparator low threshold             |
| Hi_thresh       | 0x03    | 16-bit | Comparator high threshold / RDY pin  |

### 5.2 Config Register Bit Fields

```
Bit 15    : OS — Operational status / single-shot trigger (write 1 to start)
Bits 14:12: MUX[2:0] — Input multiplexer selection
Bits 11:9 : PGA[2:0] — Programmable gain amplifier
Bit 8     : MODE — 0 = continuous, 1 = single-shot
Bits 7:5  : DR[2:0] — Data rate (8/16/32/64/128/250/475/860 SPS)
Bit 4     : COMP_MODE
Bit 3     : COMP_POL
Bit 2     : COMP_LAT
Bits 1:0  : COMP_QUE — Disable comparator = 0b11
```

**MUX selections (common):**

| MUX value | Measurement          |
|-----------|----------------------|
| 0b100     | AIN0 vs GND (single-ended) |
| 0b101     | AIN1 vs GND          |
| 0b110     | AIN2 vs GND          |
| 0b111     | AIN3 vs GND          |
| 0b000     | AIN0 − AIN1 (diff)   |
| 0b001     | AIN0 − AIN3 (diff)   |

**PGA full-scale ranges:**

| PGA | Full-Scale Range |
|-----|-----------------|
| 0b000 | ±6.144 V      |
| 0b001 | ±4.096 V      |
| 0b010 | ±2.048 V (default) |
| 0b011 | ±1.024 V      |
| 0b100 | ±0.512 V      |
| 0b101 | ±0.256 V      |

> ⚠️ **Note:** The ADS1115 input must never exceed VDD + 0.3 V regardless of PGA setting. PGA gain only affects what input range maps to full-scale code, not absolute maximum ratings.

### 5.3 Conversion Sequence (Single-Shot Mode)

```
1. Write Config register with OS=1, desired MUX, PGA, MODE=1, DR
2. Wait for conversion time (1/DR seconds, e.g. 8 ms at 128 SPS)
   OR poll OS bit until it reads 1 (conversion complete)
   OR wire ALERT/RDY pin and wait for interrupt
3. Set pointer register to 0x00
4. Read 2 bytes MSB first → signed 16-bit two's-complement result
5. Multiply by LSB size (FSR / 32768) to get voltage
```

---

## 6. DAC over I2C: MCP4725 Deep Dive 

The **MCP4725** (Microchip) is a single-channel 12-bit DAC with an I2C interface and non-volatile EEPROM to store the power-up output value.

### 6.1 Write Commands

The device accepts a 3-byte write transaction:

```
Byte 0 (command byte):
  Bits 7:6 — Command: 00 = Fast Write, 01 = Write DAC, 11 = Write DAC + EEPROM
  Bits 5:4 — Power-down: 00 = normal operation
  Bits 3:0 — D11..D8 (upper 4 bits of 12-bit code) [for non-fast commands]

Byte 1: D7..D0 (bits 11:4 in fast mode; bits 7:0 in normal mode)
Byte 2: D3..D0 followed by 4 zero bits [normal write mode only]
```

**Fast Write mode** (two payload bytes):

```
Byte 0: 0x0F & (code >> 8)   — command 0b00, PD=00, upper nibble
Byte 1: code & 0xFF           — lower 8 bits
```

### 6.2 EEPROM Write

Writing to EEPROM causes the MCP4725 to retain the output value across power cycles. The I2C transaction is identical to a normal write but with command bits `0b11`. The write takes up to 25 ms; the device NACKs during the write cycle.

### 6.3 Output Voltage Formula

```
V_out = (code / 4096) × VREF
```

With VREF = 3.3 V and code = 2048: V_out = 1.65 V

---

## 7. C/C++ Implementation 

The examples below target Linux (using `/dev/i2c-X` via the `i2c-dev` kernel module) and are equally applicable on Raspberry Pi, BeagleBone, or any embedded Linux platform. The same logic maps directly to bare-metal MCUs using their vendor HAL I2C functions.

### 7.1 I2C Helper: Open Bus and Low-Level Read/Write

```c
// i2c_helper.h
#pragma once
#include <stdint.h>

int  i2c_open(const char *bus, uint8_t addr);
void i2c_close(int fd);
int  i2c_write_reg16(int fd, uint8_t reg, uint16_t value);
int  i2c_read_reg16(int fd, uint8_t reg, int16_t *out);
```

```c
// i2c_helper.c
#include "i2c_helper.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int i2c_open(const char *bus, uint8_t addr) {
    int fd = open(bus, O_RDWR);
    if (fd < 0) {
        perror("i2c_open: open");
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        perror("i2c_open: ioctl I2C_SLAVE");
        close(fd);
        return -1;
    }
    return fd;
}

void i2c_close(int fd) {
    if (fd >= 0) close(fd);
}

/* Write a 16-bit big-endian value to a register pointer */
int i2c_write_reg16(int fd, uint8_t reg, uint16_t value) {
    uint8_t buf[3] = {
        reg,
        (uint8_t)(value >> 8),   // MSB first
        (uint8_t)(value & 0xFF)
    };
    if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
        perror("i2c_write_reg16");
        return -1;
    }
    return 0;
}

/* Set pointer register then read back 16-bit big-endian signed value */
int i2c_read_reg16(int fd, uint8_t reg, int16_t *out) {
    // Set pointer
    if (write(fd, &reg, 1) != 1) {
        perror("i2c_read_reg16: set pointer");
        return -1;
    }
    uint8_t buf[2];
    if (read(fd, buf, 2) != 2) {
        perror("i2c_read_reg16: read");
        return -1;
    }
    *out = (int16_t)((buf[0] << 8) | buf[1]);
    return 0;
}
```

---

### 7.2 ADS1115 ADC Driver in C

```c
// ads1115.h
#pragma once
#include <stdint.h>

#define ADS1115_REG_CONVERSION  0x00
#define ADS1115_REG_CONFIG      0x01

/* PGA — Full-Scale Range */
typedef enum {
    ADS1115_PGA_6_144V = 0x0000,
    ADS1115_PGA_4_096V = 0x0200,
    ADS1115_PGA_2_048V = 0x0400, /* default */
    ADS1115_PGA_1_024V = 0x0600,
    ADS1115_PGA_0_512V = 0x0800,
    ADS1115_PGA_0_256V = 0x0A00,
} ads1115_pga_t;

/* Single-ended channel multiplexer */
typedef enum {
    ADS1115_MUX_AIN0 = 0x4000,
    ADS1115_MUX_AIN1 = 0x5000,
    ADS1115_MUX_AIN2 = 0x6000,
    ADS1115_MUX_AIN3 = 0x7000,
} ads1115_mux_t;

/* Data rate */
typedef enum {
    ADS1115_DR_8SPS   = 0x0000,
    ADS1115_DR_16SPS  = 0x0020,
    ADS1115_DR_32SPS  = 0x0040,
    ADS1115_DR_64SPS  = 0x0060,
    ADS1115_DR_128SPS = 0x0080, /* default */
    ADS1115_DR_250SPS = 0x00A0,
    ADS1115_DR_475SPS = 0x00C0,
    ADS1115_DR_860SPS = 0x00E0,
} ads1115_dr_t;

typedef struct {
    int           fd;
    ads1115_pga_t pga;
    ads1115_dr_t  dr;
} ads1115_t;

int    ads1115_init(ads1115_t *dev, const char *bus, uint8_t addr,
                    ads1115_pga_t pga, ads1115_dr_t dr);
void   ads1115_close(ads1115_t *dev);
int    ads1115_read_raw(ads1115_t *dev, ads1115_mux_t mux, int16_t *raw);
double ads1115_raw_to_volts(const ads1115_t *dev, int16_t raw);
int    ads1115_read_volts(ads1115_t *dev, ads1115_mux_t mux, double *volts);
```

```c
// ads1115.c
#include "ads1115.h"
#include "i2c_helper.h"
#include <unistd.h>
#include <time.h>
#include <stdio.h>

/* Full-scale voltage for each PGA setting (millivolts) */
static const double pga_fsr_mv[] = {
    6144.0, 6144.0, /* 0x0000 — repeated for enum alignment */
    4096.0,
    2048.0,
    1024.0,
    512.0,
    256.0,
};

static double fsr_for_pga(ads1115_pga_t pga) {
    /* PGA enum values are 0x0000, 0x0200 … 0x0A00 → index 0–5 */
    return pga_fsr_mv[pga >> 9] / 1000.0;
}

int ads1115_init(ads1115_t *dev, const char *bus, uint8_t addr,
                 ads1115_pga_t pga, ads1115_dr_t dr) {
    dev->fd  = i2c_open(bus, addr);
    dev->pga = pga;
    dev->dr  = dr;
    return (dev->fd < 0) ? -1 : 0;
}

void ads1115_close(ads1115_t *dev) {
    i2c_close(dev->fd);
    dev->fd = -1;
}

int ads1115_read_raw(ads1115_t *dev, ads1115_mux_t mux, int16_t *raw) {
    /* Build config register:
       OS=1 (start single-shot) | MUX | PGA | MODE=1 (single-shot) | DR | COMP_QUE=11 */
    uint16_t config = 0x8000          /* OS: start conversion   */
                    | (uint16_t)mux   /* Input multiplexer       */
                    | (uint16_t)dev->pga
                    | 0x0100          /* Single-shot mode        */
                    | (uint16_t)dev->dr
                    | 0x0003;         /* Disable comparator      */

    if (i2c_write_reg16(dev->fd, ADS1115_REG_CONFIG, config) < 0)
        return -1;

    /* Wait for conversion: poll OS bit (bit 15 of config register = 1 when idle) */
    int16_t cfg_readback;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 }; /* 1 ms poll */
    for (int i = 0; i < 200; i++) {
        nanosleep(&ts, NULL);
        if (i2c_read_reg16(dev->fd, ADS1115_REG_CONFIG, &cfg_readback) < 0)
            return -1;
        if (cfg_readback & (int16_t)0x8000)
            break; /* Conversion complete */
    }

    return i2c_read_reg16(dev->fd, ADS1115_REG_CONVERSION, raw);
}

double ads1115_raw_to_volts(const ads1115_t *dev, int16_t raw) {
    double fsr = fsr_for_pga(dev->pga);
    /* Full-scale positive = 32767, full-scale negative = -32768 */
    return (double)raw * fsr / 32768.0;
}

int ads1115_read_volts(ads1115_t *dev, ads1115_mux_t mux, double *volts) {
    int16_t raw;
    if (ads1115_read_raw(dev, mux, &raw) < 0) return -1;
    *volts = ads1115_raw_to_volts(dev, raw);
    return 0;
}
```

**ADS1115 usage example:**

```c
// main_adc.c
#include <stdio.h>
#include "ads1115.h"

int main(void) {
    ads1115_t adc;
    if (ads1115_init(&adc, "/dev/i2c-1", 0x48,
                     ADS1115_PGA_4_096V, ADS1115_DR_128SPS) < 0) {
        fprintf(stderr, "ADS1115 init failed\n");
        return 1;
    }

    /* Read all four single-ended channels */
    ads1115_mux_t channels[] = {
        ADS1115_MUX_AIN0, ADS1115_MUX_AIN1,
        ADS1115_MUX_AIN2, ADS1115_MUX_AIN3
    };
    const char *names[] = { "AIN0", "AIN1", "AIN2", "AIN3" };

    for (int ch = 0; ch < 4; ch++) {
        double v;
        if (ads1115_read_volts(&adc, channels[ch], &v) == 0)
            printf("%-5s = %+.4f V\n", names[ch], v);
        else
            fprintf(stderr, "Read error on %s\n", names[ch]);
    }

    ads1115_close(&adc);
    return 0;
}
```

---

### 7.3 MCP4725 DAC Driver in C

```c
// mcp4725.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int     fd;
    double  vref;   /* Reference voltage, e.g. 3.3 or 5.0 */
} mcp4725_t;

int  mcp4725_init(mcp4725_t *dev, const char *bus, uint8_t addr, double vref);
void mcp4725_close(mcp4725_t *dev);
int  mcp4725_write_raw(mcp4725_t *dev, uint16_t code, bool save_eeprom);
int  mcp4725_write_volts(mcp4725_t *dev, double volts, bool save_eeprom);
int  mcp4725_read_raw(mcp4725_t *dev, uint16_t *code);
```

```c
// mcp4725.c
#include "mcp4725.h"
#include "i2c_helper.h"
#include <unistd.h>
#include <math.h>
#include <stdio.h>

int mcp4725_init(mcp4725_t *dev, const char *bus, uint8_t addr, double vref) {
    dev->fd   = i2c_open(bus, addr);
    dev->vref = vref;
    return (dev->fd < 0) ? -1 : 0;
}

void mcp4725_close(mcp4725_t *dev) {
    i2c_close(dev->fd);
    dev->fd = -1;
}

int mcp4725_write_raw(mcp4725_t *dev, uint16_t code, bool save_eeprom) {
    if (code > 0x0FFF) code = 0x0FFF; /* Clamp to 12 bits */

    uint8_t buf[3];
    if (!save_eeprom) {
        /* Write DAC register only: command = 0b01, PD = 00 */
        buf[0] = 0x40;
        buf[1] = (uint8_t)(code >> 4);          /* D11..D4 */
        buf[2] = (uint8_t)((code & 0x0F) << 4); /* D3..D0 in upper nibble */
    } else {
        /* Write DAC + EEPROM: command = 0b11 */
        buf[0] = 0x60;
        buf[1] = (uint8_t)(code >> 4);
        buf[2] = (uint8_t)((code & 0x0F) << 4);
    }

    if (write(dev->fd, buf, 3) != 3) {
        perror("mcp4725_write_raw");
        return -1;
    }

    if (save_eeprom) {
        /* EEPROM write takes up to 25 ms; device NACKs during write */
        usleep(25000);
    }
    return 0;
}

int mcp4725_write_volts(mcp4725_t *dev, double volts, bool save_eeprom) {
    if (volts < 0.0)       volts = 0.0;
    if (volts > dev->vref) volts = dev->vref;
    uint16_t code = (uint16_t)round((volts / dev->vref) * 4095.0);
    return mcp4725_write_raw(dev, code, save_eeprom);
}

int mcp4725_read_raw(mcp4725_t *dev, uint16_t *code) {
    /* MCP4725 read returns 5 bytes: status + DAC (2 bytes) + EEPROM (2 bytes) */
    uint8_t buf[5];
    if (read(dev->fd, buf, 5) != 5) {
        perror("mcp4725_read_raw");
        return -1;
    }
    /* Bytes 1 and 2 are the current DAC output value (12-bit, upper-aligned) */
    *code = ((uint16_t)(buf[1]) << 4) | (buf[2] >> 4);
    return 0;
}
```

**MCP4725 usage — sine wave sweep:**

```c
// main_dac.c
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include "mcp4725.h"

#define STEPS       256
#define VREF        3.3
#define AMPLITUDE   1.5   /* Volts peak */
#define OFFSET      1.65  /* Centre at mid-rail */

int main(void) {
    mcp4725_t dac;
    if (mcp4725_init(&dac, "/dev/i2c-1", 0x60, VREF) < 0) {
        fprintf(stderr, "MCP4725 init failed\n");
        return 1;
    }

    printf("Generating %d-step sine wave...\n", STEPS);
    for (int i = 0; i < STEPS; i++) {
        double angle = 2.0 * M_PI * i / STEPS;
        double v     = OFFSET + AMPLITUDE * sin(angle);
        mcp4725_write_volts(&dac, v, false);
        usleep(1000); /* 1 ms per step → ~1 Hz sine */
    }

    /* Park output at mid-rail */
    mcp4725_write_volts(&dac, OFFSET, false);
    mcp4725_close(&dac);
    return 0;
}
```

---

### 7.4 C++ RAII Wrapper

```cpp
// I2cAdc.hpp
#pragma once
#include <string>
#include <stdexcept>
#include <cstdint>

extern "C" {
#include "ads1115.h"
}

class I2cAdc {
public:
    I2cAdc(const std::string &bus, uint8_t addr,
           ads1115_pga_t pga = ADS1115_PGA_2_048V,
           ads1115_dr_t  dr  = ADS1115_DR_128SPS)
    {
        if (ads1115_init(&dev_, bus.c_str(), addr, pga, dr) < 0)
            throw std::runtime_error("Failed to open ADS1115 on " + bus);
    }

    ~I2cAdc() { ads1115_close(&dev_); }

    /* Non-copyable */
    I2cAdc(const I2cAdc &) = delete;
    I2cAdc &operator=(const I2cAdc &) = delete;

    double readVolts(ads1115_mux_t mux) {
        double v = 0.0;
        if (ads1115_read_volts(&dev_, mux, &v) < 0)
            throw std::runtime_error("ADS1115 read failed");
        return v;
    }

    int16_t readRaw(ads1115_mux_t mux) {
        int16_t raw = 0;
        if (ads1115_read_raw(&dev_, mux, &raw) < 0)
            throw std::runtime_error("ADS1115 raw read failed");
        return raw;
    }

private:
    ads1115_t dev_;
};
```

```cpp
// main_cpp.cpp
#include <iostream>
#include <iomanip>
#include "I2cAdc.hpp"

int main() {
    try {
        I2cAdc adc("/dev/i2c-1", 0x48, ADS1115_PGA_4_096V, ADS1115_DR_128SPS);

        std::cout << std::fixed << std::setprecision(4);
        for (auto mux : {ADS1115_MUX_AIN0, ADS1115_MUX_AIN1,
                         ADS1115_MUX_AIN2, ADS1115_MUX_AIN3}) {
            std::cout << "Channel " << (mux >> 12 & 0x3)
                      << " = " << adc.readVolts(mux) << " V\n";
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

---

## 8. Rust Implementation 

The Rust examples use the `linux-embedded-hal` and `embedded-hal` ecosystem, which provides platform-agnostic traits for I2C. This makes the driver code fully portable across bare-metal targets (via BSP implementations) and Linux (via `linux-embedded-hal`).

**`Cargo.toml` dependencies:**

```toml
[dependencies]
embedded-hal        = "1.0"
linux-embedded-hal  = "0.4"
```

---

### 8.1 ADS1115 ADC Driver in Rust

```rust
// src/ads1115.rs
use embedded_hal::i2c::I2c;

pub const REG_CONVERSION: u8 = 0x00;
pub const REG_CONFIG:     u8 = 0x01;

/// Programmable Gain Amplifier setting
#[derive(Clone, Copy, Debug)]
#[repr(u16)]
pub enum Pga {
    V6_144 = 0x0000,
    V4_096 = 0x0200,
    V2_048 = 0x0400, // default
    V1_024 = 0x0600,
    V0_512 = 0x0800,
    V0_256 = 0x0A00,
}

impl Pga {
    /// Returns the full-scale range in volts
    pub fn fsr(self) -> f64 {
        match self {
            Pga::V6_144 => 6.144,
            Pga::V4_096 => 4.096,
            Pga::V2_048 => 2.048,
            Pga::V1_024 => 1.024,
            Pga::V0_512 => 0.512,
            Pga::V0_256 => 0.256,
        }
    }
}

/// Input multiplexer — single-ended channels
#[derive(Clone, Copy, Debug)]
#[repr(u16)]
pub enum Mux {
    Ain0 = 0x4000,
    Ain1 = 0x5000,
    Ain2 = 0x6000,
    Ain3 = 0x7000,
    Diff01 = 0x0000,
    Diff03 = 0x1000,
    Diff13 = 0x2000,
    Diff23 = 0x3000,
}

/// Data rate
#[derive(Clone, Copy, Debug)]
#[repr(u16)]
pub enum DataRate {
    Sps8   = 0x0000,
    Sps16  = 0x0020,
    Sps32  = 0x0040,
    Sps64  = 0x0060,
    Sps128 = 0x0080,
    Sps250 = 0x00A0,
    Sps475 = 0x00C0,
    Sps860 = 0x00E0,
}

impl DataRate {
    /// Approximate conversion time in microseconds
    pub fn conv_time_us(self) -> u64 {
        match self {
            DataRate::Sps8   => 125_000,
            DataRate::Sps16  =>  62_500,
            DataRate::Sps32  =>  31_250,
            DataRate::Sps64  =>  15_625,
            DataRate::Sps128 =>   7_813,
            DataRate::Sps250 =>   4_000,
            DataRate::Sps475 =>   2_106,
            DataRate::Sps860 =>   1_163,
        }
    }
}

pub struct Ads1115<I2C> {
    i2c:  I2C,
    addr: u8,
    pga:  Pga,
    dr:   DataRate,
}

#[derive(Debug)]
pub enum Ads1115Error<E> {
    I2c(E),
    Timeout,
}

impl<E> From<E> for Ads1115Error<E> {
    fn from(e: E) -> Self { Ads1115Error::I2c(e) }
}

impl<I2C: I2c> Ads1115<I2C> {
    pub fn new(i2c: I2C, addr: u8, pga: Pga, dr: DataRate) -> Self {
        Self { i2c, addr, pga, dr }
    }

    /// Perform a single-shot conversion and return the raw signed 16-bit result
    pub fn read_raw(&mut self, mux: Mux) -> Result<i16, Ads1115Error<I2C::Error>> {
        let config: u16 = 0x8000               // OS: start conversion
            | (mux as u16)
            | (self.pga as u16)
            | 0x0100                            // single-shot mode
            | (self.dr as u16)
            | 0x0003;                           // disable comparator

        // Write config register (pointer = 0x01)
        let bytes = [
            REG_CONFIG,
            (config >> 8) as u8,
            (config & 0xFF) as u8,
        ];
        self.i2c.write(self.addr, &bytes)?;

        // Poll OS bit until conversion is complete (max 200 tries × 1 ms)
        for _ in 0..200 {
            std::thread::sleep(std::time::Duration::from_millis(1));
            let mut cfg_buf = [0u8; 2];
            self.i2c.write_read(self.addr, &[REG_CONFIG], &mut cfg_buf)?;
            let cfg = u16::from_be_bytes(cfg_buf);
            if cfg & 0x8000 != 0 {
                // Conversion complete — read result
                let mut conv_buf = [0u8; 2];
                self.i2c.write_read(self.addr, &[REG_CONVERSION], &mut conv_buf)?;
                return Ok(i16::from_be_bytes(conv_buf));
            }
        }
        Err(Ads1115Error::Timeout)
    }

    /// Convert raw ADC value to volts
    pub fn raw_to_volts(&self, raw: i16) -> f64 {
        raw as f64 * self.pga.fsr() / 32768.0
    }

    /// Convenience: read a channel directly in volts
    pub fn read_volts(&mut self, mux: Mux) -> Result<f64, Ads1115Error<I2C::Error>> {
        let raw = self.read_raw(mux)?;
        Ok(self.raw_to_volts(raw))
    }

    /// Release the underlying I2C bus
    pub fn release(self) -> I2C { self.i2c }
}
```

---

### 8.2 MCP4725 DAC Driver in Rust

```rust
// src/mcp4725.rs
use embedded_hal::i2c::I2c;

pub struct Mcp4725<I2C> {
    i2c:  I2C,
    addr: u8,
    vref: f64,
}

#[derive(Debug)]
pub enum Mcp4725Error<E> {
    I2c(E),
    InvalidVoltage,
}

impl<I2C: I2c> Mcp4725<I2C> {
    pub fn new(i2c: I2C, addr: u8, vref: f64) -> Self {
        Self { i2c, addr, vref }
    }

    /// Write a raw 12-bit DAC code (0–4095).
    /// If `save_eeprom` is true, the value is also written to non-volatile memory.
    pub fn write_raw(
        &mut self,
        code: u16,
        save_eeprom: bool,
    ) -> Result<(), Mcp4725Error<I2C::Error>> {
        let code = code.min(0x0FFF);
        let cmd_byte: u8 = if save_eeprom { 0x60 } else { 0x40 };
        let buf = [
            cmd_byte,
            (code >> 4) as u8,            // D11..D4
            ((code & 0x0F) << 4) as u8,   // D3..D0, zero-padded
        ];
        self.i2c.write(self.addr, &buf)
            .map_err(Mcp4725Error::I2c)?;

        if save_eeprom {
            // EEPROM write cycle takes up to 25 ms
            std::thread::sleep(std::time::Duration::from_millis(25));
        }
        Ok(())
    }

    /// Set the output voltage. Clamps to [0, vref].
    pub fn write_volts(
        &mut self,
        volts: f64,
        save_eeprom: bool,
    ) -> Result<(), Mcp4725Error<I2C::Error>> {
        let v = volts.clamp(0.0, self.vref);
        let code = ((v / self.vref) * 4095.0).round() as u16;
        self.write_raw(code, save_eeprom)
    }

    /// Read back the current DAC register value (not EEPROM)
    pub fn read_raw(&mut self) -> Result<u16, Mcp4725Error<I2C::Error>> {
        let mut buf = [0u8; 5];
        self.i2c.read(self.addr, &mut buf)
            .map_err(Mcp4725Error::I2c)?;
        // Bytes 1 and 2 = current DAC output, upper 12 bits
        let code = ((buf[1] as u16) << 4) | ((buf[2] as u16) >> 4);
        Ok(code)
    }

    pub fn release(self) -> I2C { self.i2c }
}
```

---

### 8.3 Rust Main: ADC + DAC Together

```rust
// src/main.rs
mod ads1115;
mod mcp4725;

use ads1115::{Ads1115, DataRate, Mux, Pga};
use linux_embedded_hal::I2cdev;
use mcp4725::Mcp4725;
use std::error::Error;

fn main() -> Result<(), Box<dyn Error>> {
    // Open I2C bus (Linux)
    let i2c_adc = I2cdev::new("/dev/i2c-1")?;
    let i2c_dac = I2cdev::new("/dev/i2c-1")?;

    let mut adc = Ads1115::new(i2c_adc, 0x48, Pga::V4_096, DataRate::Sps128);
    let mut dac = Mcp4725::new(i2c_dac, 0x60, 3.3);

    // Read AIN0 and mirror it to the DAC output
    println!("Reading AIN0, mirroring to DAC...");
    for _ in 0..10 {
        match adc.read_volts(Mux::Ain0) {
            Ok(v) => {
                println!("AIN0 = {:.4} V  →  DAC set to {:.4} V", v, v);
                if let Err(e) = dac.write_volts(v, false) {
                    eprintln!("DAC write error: {:?}", e);
                }
            }
            Err(e) => eprintln!("ADC read error: {:?}", e),
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }

    // Sweep DAC from 0 V to VREF in 256 steps, read it back via ADC
    println!("\nSweep test (DAC → ADC loopback):");
    for step in 0u16..=255 {
        let target = (step as f64 / 255.0) * 3.3;
        dac.write_volts(target, false)?;
        std::thread::sleep(std::time::Duration::from_millis(10));

        let measured = adc.read_volts(Mux::Ain0)?;
        let error_mv = (measured - target) * 1000.0;
        println!(
            "Set {:.3} V  Measured {:.4} V  Error {:+.2} mV",
            target, measured, error_mv
        );
    }

    Ok(())
}
```

---

### 8.4 Rust: Continuous Mode with Interrupt (ALERT/RDY Pin)

For high-throughput applications, configure the ADS1115 in continuous conversion mode and use the ALERT/RDY pin to trigger reads via an OS interrupt rather than polling:

```rust
// Continuous mode config: MODE bit = 0
pub fn start_continuous(&mut self, mux: Mux) -> Result<(), I2C::Error> {
    let config: u16 = (mux as u16)
        | (self.pga as u16)
        | 0x0000  // continuous mode (MODE=0)
        | (self.dr as u16)
        | 0x0003; // disable comparator
    let bytes = [
        ads1115::REG_CONFIG,
        (config >> 8) as u8,
        (config & 0xFF) as u8,
    ];
    self.i2c.write(self.addr, &bytes)
}

// In continuous mode, just read conversion register at any time
pub fn read_continuous(&mut self) -> Result<i16, I2C::Error> {
    let mut buf = [0u8; 2];
    self.i2c.write_read(self.addr, &[ads1115::REG_CONVERSION], &mut buf)?;
    Ok(i16::from_be_bytes(buf))
}
```

On Linux, pair this with `epoll` on a GPIO sysfs/character device export for zero-latency interrupt-driven sampling.

---

## 9. Advanced Topics 

### 9.1 Multi-Device Bus Sharing

Multiple ADCs and DACs can coexist on the same two-wire bus as long as their addresses are distinct. Use address pins to configure up to four ADS1115 devices (addresses 0x48–0x4B) for up to 16 single-ended channels:

```
SDA ──┬─── ADS1115 (ADDR=GND, 0x48)  ← Channels 0–3
      ├─── ADS1115 (ADDR=VDD, 0x49)  ← Channels 4–7
      ├─── ADS1115 (ADDR=SDA, 0x4A)  ← Channels 8–11
      └─── ADS1115 (ADDR=SCL, 0x4B)  ← Channels 12–15
SCL ──┴─── (shared)
```

### 9.2 Noise Reduction Techniques

- **Decoupling capacitors** — Place 100 nF + 10 µF ceramic capacitors between VDD and GND close to each chip.
- **I2C pull-up resistors** — 2.2 kΩ to 4.7 kΩ to VDD. Lower values increase drive current and help at high speeds; higher values save power.
- **Averaging** — Software-average N consecutive readings to reduce random noise by factor √N.
- **Differential wiring** — Use twisted pairs and differential ADC input mode in noisy industrial environments.
- **Shielding** — Ground the cable shield at one end only to avoid ground loops.

### 9.3 Calibration

ADC/DAC devices have gain and offset errors from manufacturing tolerances. A two-point calibration eliminates both:

```c
// Two-point linear calibration
typedef struct { double gain; double offset; } CalibCoeffs;

CalibCoeffs calibrate(double raw_low, double known_low,
                      double raw_high, double known_high) {
    CalibCoeffs c;
    c.gain   = (known_high - known_low) / (raw_high - raw_low);
    c.offset = known_low - c.gain * raw_low;
    return c;
}

double apply_calibration(double raw, CalibCoeffs c) {
    return c.gain * raw + c.offset;
}
```

### 9.4 Handling I2C Clock Stretching

Some ADC chips (e.g. MCP3421 in one-shot mode) hold SCL low (clock stretching) while performing a conversion. Ensure the I2C master supports clock stretching. Linux `i2c-dev` supports this by default. On bare-metal, check the HAL documentation.

### 9.5 DAC Output Buffering

Most I2C DACs (including MCP4725) have a rail-to-rail output buffer and can source/sink a few milliamps directly. For driving loads below 1 kΩ or capacitive loads, add an op-amp buffer to prevent the DAC from affecting its own reference and to provide additional current capability.

### 9.6 Using DMA for Bulk Transfers

On embedded platforms with DMA-capable I2C controllers (STM32, RP2350, etc.), use DMA to offload bulk transfers to the hardware, freeing the CPU for other tasks:

```c
/* Pseudo-code for STM32 HAL DMA-based I2C read */
HAL_I2C_Mem_Read_DMA(&hi2c1,
    ADS1115_ADDR << 1,
    REG_CONVERSION,
    I2C_MEMADD_SIZE_8BIT,
    dma_rx_buffer,
    2);
/* Callback fires when transfer is complete */
```

---

## 10. Summary 

I2C ADCs and DACs are indispensable building blocks for embedded systems requiring analog sensing or signal generation without consuming precious GPIO or SPI channels.

**Key takeaways:**

- The **ADS1115** (16-bit, 4-channel, with PGA) is the go-to I2C ADC for precision measurements. Its config register must be written before each single-shot conversion, and the OS bit polled or the ALERT/RDY pin monitored for completion.
- The **MCP4725** (12-bit single-channel) is the standard I2C DAC, featuring optional EEPROM persistence and a straightforward 3-byte write protocol.
- Both **C/C++** and **Rust** implementations benefit from abstracting the raw I2C transactions behind a typed driver API with proper error handling.
- In **C**, the Linux `i2c-dev` interface via `open/ioctl/read/write` gives direct hardware access; in **C++**, RAII wrappers ensure resource safety.
- In **Rust**, the `embedded-hal` trait system decouples driver logic from the I2C implementation, enabling the same driver to run on Linux, STM32, Raspberry Pi RP2350, and other platforms with no code changes.
- Precision systems require attention to **decoupling, noise filtering, pull-up resistor selection, and two-point calibration**.
- For high-throughput applications, prefer **continuous mode + ALERT/RDY interrupts** over polling, and leverage **DMA** on hardware that supports it.

---

*Document covers I2C ADC/DAC control with ADS1115 and MCP4725 examples in C, C++, and Rust.*