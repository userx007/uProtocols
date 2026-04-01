# 57. RTC (Real-Time Clock) Integration via I2C

**Structure:**
- I²C protocol recap (transaction format, speeds, pull-ups)
- Comparison table of 7 common RTC ICs with addresses and features
- Full DS3231 register map with BCD encoding explanation
- Step-by-step procedures for reading and writing time
- Alarm mask bit tables for Alarm 1 and Alarm 2

**C/C++ Examples:**
- Complete `rtc_ds3231.h` header with register definitions, bit masks, and structs
- Full `rtc_ds3231.c` implementation with burst read/write, alarm config, temperature, and OSF handling
- Linux application `main_c.c` with polling loop and alarm detection
- Arduino/C++ example using the `Wire` library (ESP32/AVR compatible)

**Rust Examples:**
- `Cargo.toml` with `embedded-hal 1.0` dependencies
- `src/rtc.rs` — a fully generic driver using the `I2c` trait, compatible with both Linux and bare-metal targets
- `src/main.rs` — hosted Linux application using `linux-embedded-hal`
- A `no_std` Embassy async example targeting STM32F4

**Summary** covers the 5 key areas: I²C communication, BCD encoding, alarm configuration, battery backup design, and IC selection guidance.

> **Topic:** Reading and setting time, alarm configuration, and battery backup considerations  
> **Interface:** I²C (Inter-Integrated Circuit)  
> **Languages covered:** C/C++ and Rust

---

## Table of Contents

1. [Introduction](#introduction)
2. [I²C Protocol Fundamentals Recap](#i2c-protocol-fundamentals-recap)
3. [Common RTC ICs and Their I²C Addresses](#common-rtc-ics-and-their-i2c-addresses)
4. [RTC Register Architecture](#rtc-register-architecture)
5. [Reading Time from an RTC](#reading-time-from-an-rtc)
6. [Setting Time on an RTC](#setting-time-on-an-rtc)
7. [Alarm Configuration](#alarm-configuration)
8. [Battery Backup Considerations](#battery-backup-considerations)
9. [Error Handling and Reliability](#error-handling-and-reliability)
10. [Code Examples in C/C++](#code-examples-in-cc)
11. [Code Examples in Rust](#code-examples-in-rust)
12. [Summary](#summary)

---

## Introduction

A **Real-Time Clock (RTC)** is a dedicated integrated circuit that tracks the current time and date independently of the main processor. Unlike a software timer that resets on power loss, an RTC uses its own oscillator (typically a 32.768 kHz crystal) and a small backup battery to maintain accurate timekeeping even when the main system is powered off.

RTCs communicate over the **I²C bus**, making them extremely easy to integrate into any microcontroller or single-board-computer project. A typical RTC IC provides:

- Seconds, minutes, hours (12/24-hour mode)
- Day of week, day of month, month, year (with century bit)
- One or more programmable alarms
- A square-wave output pin
- Power-fail detection registers
- Optionally: temperature compensation, EEPROM storage, SRAM

### Why Use a Dedicated RTC?

| Feature | Software Timer | Hardware RTC |
|---|---|---|
| Accuracy | Poor (drift depends on MCU clock) | Excellent (±2 ppm with TCXO) |
| Power-loss retention | No | Yes (battery backup) |
| CPU overhead | High (interrupt-driven) | None |
| Alarm without CPU | No | Yes |
| Cost | Free | ~$0.50–$5 |

---

## I²C Protocol Fundamentals Recap

The I²C bus uses two lines:

- **SDA** — Serial Data line (bidirectional)
- **SCL** — Serial Clock line (driven by master)

Both lines are **open-drain** and require pull-up resistors (typically 4.7 kΩ for standard mode, 2.2 kΩ for fast mode). Typical bus speeds relevant to RTC use:

| Mode | Speed |
|---|---|
| Standard Mode | 100 kbit/s |
| Fast Mode | 400 kbit/s |
| Fast Mode Plus | 1 Mbit/s |

A typical I²C write transaction to an RTC:

```
START → [ADDR+W] → ACK → [REG] → ACK → [DATA] → ACK → ... → STOP
```

A typical I²C read transaction (register-addressed read):

```
START → [ADDR+W] → ACK → [REG] → ACK →
RESTART → [ADDR+R] → ACK → [DATA] → ACK → ... → NACK → STOP
```

---

## Common RTC ICs and Their I²C Addresses

| IC | Manufacturer | I²C Address | Features |
|---|---|---|---|
| **DS1307** | Maxim/Dallas | `0x68` | Basic RTC, 56-byte NVRAM, 1Hz–32kHz SQW |
| **DS3231** | Maxim/Dallas | `0x68` | High accuracy (±2ppm), temp sensor, TCXO |
| **DS3232** | Maxim/Dallas | `0x68` | DS3231 + 236-byte NVRAM |
| **PCF8523** | NXP | `0x68` | Low power, alarm, timer, offset register |
| **PCF8563** | NXP | `0x51` | Extremely low power, battery switchover |
| **MCP7940N** | Microchip | `0x6F` | Power-fail timestamp, 64-byte EEPROM |
| **RV-3028-C7** | Micro Crystal | `0x52` | Ultra-low power (40 nA), ±1 ppm/day |

> **Note:** Most RTC ICs use fixed I²C addresses. Some (like MCP7941x) allow address selection via pins.

---

## RTC Register Architecture

### DS3231 Register Map (representative example)

| Address | Bit 7 | Bits 6–4 | Bits 3–0 | Description |
|---|---|---|---|---|
| `0x00` | — | 10-Seconds | Seconds | Seconds (00–59) |
| `0x01` | — | 10-Minutes | Minutes | Minutes (00–59) |
| `0x02` | — | 10-Hours | Hours | Hours (1–12/00–23) |
| `0x03` | — | — | Day | Day of Week (1–7) |
| `0x04` | — | 10-Date | Date | Date (01–31) |
| `0x05` | Century | 10-Month | Month | Month (01–12) |
| `0x06` | — | 10-Year | Year | Year (00–99) |
| `0x07` | A1M1 | 10-Seconds | Seconds | Alarm 1 Seconds |
| `0x08` | A1M2 | 10-Minutes | Minutes | Alarm 1 Minutes |
| `0x09` | A1M3 | 12/24, AM/PM | Hour | Alarm 1 Hours |
| `0x0A` | A1M4 | DY/DT | Date/Day | Alarm 1 Day/Date |
| `0x0B` | A2M2 | 10-Minutes | Minutes | Alarm 2 Minutes |
| `0x0C` | A2M3 | 12/24, AM/PM | Hour | Alarm 2 Hours |
| `0x0D` | A2M4 | DY/DT | Date/Day | Alarm 2 Day/Date |
| `0x0E` | EOSC | BBSQW | CONV | Control Register |
| `0x0F` | OSF | — | A2F, A1F | Status Register |
| `0x11`–`0x12` | — | — | — | Temperature (MSB, LSB) |

### BCD Encoding

RTC chips store values in **Binary Coded Decimal (BCD)**, not binary. Each decimal digit occupies its own nibble:

```
Decimal 59 → BCD: 0101 1001 (0x59)
Decimal 23 → BCD: 0010 0011 (0x23)
```

Conversion macros are essential:

```c
#define BCD_TO_DEC(bcd)  ((((bcd) >> 4) * 10) + ((bcd) & 0x0F))
#define DEC_TO_BCD(dec)  ((((dec) / 10) << 4) | ((dec) % 10))
```

---

## Reading Time from an RTC

Reading time is a **burst read** starting from register `0x00`. The RTC internally latches all registers at the start of the I²C read, preventing a "seconds rollover" race condition mid-read.

### Procedure

1. Send START + device address + WRITE bit
2. Send register address `0x00`
3. Send REPEATED START + device address + READ bit
4. Clock out 7 bytes (seconds through year)
5. Convert each BCD byte to decimal
6. Send NACK + STOP

---

## Setting Time on an RTC

### Procedure

1. Prepare BCD-encoded values for all 7 time registers
2. Send START + device address + WRITE bit
3. Send register address `0x00`
4. Write 7 bytes sequentially
5. Send STOP

**Important:** When setting time on the DS3231, ensure the **OSF (Oscillator Stop Flag)** in the status register (`0x0F`, bit 7) is cleared after writing, to confirm the oscillator is running from the new time.

---

## Alarm Configuration

The DS3231 has two alarms:

- **Alarm 1**: Can match seconds, minutes, hours, and day/date
- **Alarm 2**: Can match minutes, hours, and day/date (no seconds)

### Alarm Mask Bits (DS3231)

Each alarm register has an **AxMy** mask bit. The alarm fires when the conditions defined by the mask match:

| A1M4 | A1M3 | A1M2 | A1M1 | Alarm 1 Condition |
|---|---|---|---|---|
| 1 | 1 | 1 | 1 | Once per second |
| 1 | 1 | 1 | 0 | When seconds match |
| 1 | 1 | 0 | 0 | When min, sec match |
| 1 | 0 | 0 | 0 | When hr, min, sec match |
| 0 | 0 | 0 | 0 | When date/day, hr, min, sec match |

### Enabling Alarm Interrupts

In the Control register (`0x0E`):
- Set **A1IE** (bit 0) or **A2IE** (bit 1) to enable the alarm interrupt
- Set **INTCN** (bit 2) = 1 to route alarm to the `INT#/SQW` pin (active LOW)

When alarm fires, the corresponding **A1F** or **A2F** flag in the status register is set. You **must** clear it in firmware before the next alarm can trigger.

### PCF8523 Alarm (Alternative IC)

The PCF8523 has one alarm per time unit (minute, hour, day, weekday), each independently maskable by bit 7 of its register.

---

## Battery Backup Considerations

### Why a Backup Battery?

When the main VCC is removed, the RTC switches to a small backup cell (typically a **CR2032** lithium coin cell, 3V, ~220 mAh) to maintain timekeeping. This is entirely transparent to the firmware — the I²C interface remains accessible from the backup supply on some chips.

### Battery Life Estimation

```
Battery Life (years) = Battery Capacity (mAh) / RTC Supply Current (µA) / 8760 hours/year
```

Example — DS3231 at 3V backup:
- Ibat = ~3 µA typical
- CR2032 = 220 mAh
- Life = 220,000 µA·h / 3 µA / 8760 h ≈ **8.4 years**

### The Oscillator Stop Flag (OSF)

The DS3231 sets the **OSF bit** (Status register, bit 7) when:
- Power is first applied
- VCC and VBAT both drop below threshold

This is your indicator that the clock **lost time** and must be re-set. Always check OSF at startup:

```c
if (rtc_read_register(0x0F) & 0x80) {
    // Time is invalid — set from NTP or user input
    rtc_set_time(&known_good_time);
    rtc_clear_osf();
}
```

### Battery Switchover Circuit (PCF8563 / PCF8523)

These NXP chips have a built-in **power-fail comparator and switch**. When VDD falls below a threshold, the chip automatically switches to VBAT. Firmware can read the `STOP` bit or power-fail timestamp register to detect this event.

### Design Guidelines

- Use a **Schottky diode** in series with VCC to prevent battery from powering the host system
- Keep RTC trace lengths short; use local 100 nF decoupling capacitors on VCC and VBAT pins
- A **supercapacitor** (0.1 F – 1 F) can replace the coin cell in applications with frequent power cycling
- Never leave VBAT unconnected — even if not using backup, tie to GND through a diode per the datasheet

---

## Error Handling and Reliability

- **Always check I²C ACK/NACK**: A NACK on address byte means the device is not present or is busy
- **Verify BCD range**: Corrupt reads can produce invalid BCD (e.g., seconds > 59). Validate before using
- **Handle bus lockup**: If SDA is stuck LOW, toggle SCL up to 9 times to release a partially completed transaction
- **Use CRC where available**: Some newer RTCs (RV-3028) include a CRC register for configuration validation
- **Debounce alarm interrupt pin**: In noisy environments, the INT# pin can glitch; use a software filter or hardware RC filter

---

## Code Examples in C/C++

### Platform Assumptions

The examples below target **Linux** (using `/dev/i2c-N`) but the logic is identical for bare-metal HAL calls (STM32 HAL, Arduino Wire, ESP-IDF). Platform-specific I²C primitives are abstracted into `i2c_write()` / `i2c_read_burst()`.

---

### Header: `rtc_ds3231.h`

```c
#ifndef RTC_DS3231_H
#define RTC_DS3231_H

#include <stdint.h>
#include <stdbool.h>

/* I²C address of DS3231 (fixed) */
#define DS3231_ADDR         0x68

/* Register map */
#define DS3231_REG_SECONDS  0x00
#define DS3231_REG_MINUTES  0x01
#define DS3231_REG_HOURS    0x02
#define DS3231_REG_DAY      0x03
#define DS3231_REG_DATE     0x04
#define DS3231_REG_MONTH    0x05
#define DS3231_REG_YEAR     0x06
#define DS3231_REG_ALM1_SEC 0x07
#define DS3231_REG_ALM1_MIN 0x08
#define DS3231_REG_ALM1_HR  0x09
#define DS3231_REG_ALM1_DAY 0x0A
#define DS3231_REG_ALM2_MIN 0x0B
#define DS3231_REG_ALM2_HR  0x0C
#define DS3231_REG_ALM2_DAY 0x0D
#define DS3231_REG_CONTROL  0x0E
#define DS3231_REG_STATUS   0x0F
#define DS3231_REG_TEMP_MSB 0x11
#define DS3231_REG_TEMP_LSB 0x12

/* Control register bits */
#define DS3231_CTRL_A1IE    (1 << 0)  /* Alarm 1 interrupt enable */
#define DS3231_CTRL_A2IE    (1 << 1)  /* Alarm 2 interrupt enable */
#define DS3231_CTRL_INTCN   (1 << 2)  /* Interrupt control */
#define DS3231_CTRL_RS1     (1 << 3)  /* Rate select 1 */
#define DS3231_CTRL_RS2     (1 << 4)  /* Rate select 2 */
#define DS3231_CTRL_CONV    (1 << 5)  /* Temperature conversion */
#define DS3231_CTRL_BBSQW   (1 << 6)  /* Battery-backed square wave */
#define DS3231_CTRL_EOSC    (1 << 7)  /* Enable oscillator (active LOW) */

/* Status register bits */
#define DS3231_STAT_A1F     (1 << 0)  /* Alarm 1 flag */
#define DS3231_STAT_A2F     (1 << 1)  /* Alarm 2 flag */
#define DS3231_STAT_BSY     (1 << 2)  /* Device busy (TCXO conversion) */
#define DS3231_STAT_EN32KHZ (1 << 3)  /* Enable 32kHz output */
#define DS3231_STAT_OSF     (1 << 7)  /* Oscillator stop flag */

/* Alarm 1 rate masks */
typedef enum {
    ALM1_EVERY_SECOND    = 0x0F, /* Once per second */
    ALM1_MATCH_SECONDS   = 0x0E, /* When seconds match */
    ALM1_MATCH_MIN_SEC   = 0x0C, /* When minutes and seconds match */
    ALM1_MATCH_HR_MIN_SEC = 0x08, /* When hours, minutes, and seconds match */
    ALM1_MATCH_DATE      = 0x00, /* When date, hours, minutes, seconds match */
    ALM1_MATCH_DAY       = 0x10, /* When day, hours, minutes, seconds match */
} ds3231_alm1_rate_t;

/* Time structure */
typedef struct {
    uint8_t seconds;   /* 0–59 */
    uint8_t minutes;   /* 0–59 */
    uint8_t hours;     /* 0–23 (24-hour mode) */
    uint8_t day;       /* 1–7  (day of week) */
    uint8_t date;      /* 1–31 */
    uint8_t month;     /* 1–12 */
    uint8_t year;      /* 0–99 (2000–2099) */
} rtc_time_t;

/* Alarm structure */
typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day_date;  /* Day of week or date */
    bool    use_day;   /* true = day-of-week, false = date */
    ds3231_alm1_rate_t rate;
} rtc_alarm_t;

/* Function prototypes */
int  rtc_init(int i2c_bus);
int  rtc_get_time(rtc_time_t *t);
int  rtc_set_time(const rtc_time_t *t);
bool rtc_oscillator_stopped(void);
int  rtc_clear_osf(void);
int  rtc_set_alarm1(const rtc_alarm_t *alarm);
int  rtc_clear_alarm1_flag(void);
bool rtc_alarm1_fired(void);
int  rtc_enable_alarm1_interrupt(bool enable);
float rtc_read_temperature(void);
void rtc_close(void);

#endif /* RTC_DS3231_H */
```

---

### Implementation: `rtc_ds3231.c`

```c
#include "rtc_ds3231.h"
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* BCD conversion macros */
#define BCD_TO_DEC(b)   (((b) >> 4) * 10 + ((b) & 0x0F))
#define DEC_TO_BCD(d)   ((((d) / 10) << 4) | ((d) % 10))

static int i2c_fd = -1;

/* ── Low-level I²C helpers ────────────────────────────────────────────── */

static int rtc_write_register(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    if (write(i2c_fd, buf, 2) != 2) {
        fprintf(stderr, "rtc: write reg 0x%02X failed: %s\n", reg, strerror(errno));
        return -1;
    }
    return 0;
}

static int rtc_read_register(uint8_t reg, uint8_t *out)
{
    if (write(i2c_fd, &reg, 1) != 1) return -1;
    if (read(i2c_fd, out, 1) != 1)  return -1;
    return 0;
}

static int rtc_burst_read(uint8_t start_reg, uint8_t *buf, size_t len)
{
    if (write(i2c_fd, &start_reg, 1) != 1) return -1;
    if ((size_t)read(i2c_fd, buf, len) != len) return -1;
    return 0;
}

/* ── Public API ────────────────────────────────────────────────────────── */

int rtc_init(int i2c_bus)
{
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", i2c_bus);

    i2c_fd = open(path, O_RDWR);
    if (i2c_fd < 0) {
        perror("rtc: open i2c device");
        return -1;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, DS3231_ADDR) < 0) {
        perror("rtc: set slave address");
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }

    /* Ensure oscillator is enabled (EOSC bit = 0 means enabled) */
    uint8_t ctrl;
    if (rtc_read_register(DS3231_REG_CONTROL, &ctrl) < 0) return -1;
    ctrl &= ~DS3231_CTRL_EOSC;                   /* Clear EOSC = enable osc */
    ctrl |=  DS3231_CTRL_INTCN;                  /* Route alarm to INT# pin */
    return rtc_write_register(DS3231_REG_CONTROL, ctrl);
}

void rtc_close(void)
{
    if (i2c_fd >= 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
}

/**
 * Read current time from DS3231.
 * Burst-reads 7 registers starting at 0x00.
 */
int rtc_get_time(rtc_time_t *t)
{
    uint8_t buf[7];

    if (rtc_burst_read(DS3231_REG_SECONDS, buf, 7) < 0) {
        fprintf(stderr, "rtc: burst read failed\n");
        return -1;
    }

    t->seconds = BCD_TO_DEC(buf[0] & 0x7F);     /* Mask bit 7 (unused) */
    t->minutes = BCD_TO_DEC(buf[1] & 0x7F);
    t->hours   = BCD_TO_DEC(buf[2] & 0x3F);     /* Assume 24-hour mode */
    t->day     = BCD_TO_DEC(buf[3] & 0x07);
    t->date    = BCD_TO_DEC(buf[4] & 0x3F);
    t->month   = BCD_TO_DEC(buf[5] & 0x1F);     /* Mask century bit */
    t->year    = BCD_TO_DEC(buf[6]);

    /* Sanity check */
    if (t->seconds > 59 || t->minutes > 59 || t->hours > 23 ||
        t->date < 1 || t->date > 31 || t->month < 1 || t->month > 12) {
        fprintf(stderr, "rtc: invalid time read (possible power-loss)\n");
        return -1;
    }

    return 0;
}

/**
 * Write time to DS3231.
 * Burst-writes 7 registers starting at 0x00.
 */
int rtc_set_time(const rtc_time_t *t)
{
    /* Validate inputs */
    if (t->seconds > 59 || t->minutes > 59 || t->hours > 23 ||
        t->date < 1 || t->date > 31 || t->month < 1 || t->month > 12 ||
        t->day < 1 || t->day > 7) {
        fprintf(stderr, "rtc: invalid time values\n");
        return -1;
    }

    /*
     * Buffer layout: [register][s][m][h][dow][date][month][year]
     * Total 8 bytes (1 register address + 7 data bytes)
     */
    uint8_t buf[8];
    buf[0] = DS3231_REG_SECONDS;
    buf[1] = DEC_TO_BCD(t->seconds);
    buf[2] = DEC_TO_BCD(t->minutes);
    buf[3] = DEC_TO_BCD(t->hours);   /* 24-hour mode: bit 6 = 0 */
    buf[4] = DEC_TO_BCD(t->day);
    buf[5] = DEC_TO_BCD(t->date);
    buf[6] = DEC_TO_BCD(t->month);
    buf[7] = DEC_TO_BCD(t->year);

    if (write(i2c_fd, buf, 8) != 8) {
        perror("rtc: set_time write");
        return -1;
    }

    /* Clear OSF to confirm oscillator started with new time */
    return rtc_clear_osf();
}

bool rtc_oscillator_stopped(void)
{
    uint8_t status;
    if (rtc_read_register(DS3231_REG_STATUS, &status) < 0) return true;
    return (status & DS3231_STAT_OSF) != 0;
}

int rtc_clear_osf(void)
{
    uint8_t status;
    if (rtc_read_register(DS3231_REG_STATUS, &status) < 0) return -1;
    status &= ~DS3231_STAT_OSF;
    return rtc_write_register(DS3231_REG_STATUS, status);
}

/**
 * Configure Alarm 1.
 *
 * The mask bits determine when the alarm fires. Each alarm register's
 * bit 7 (AxMy) participates in the mask:
 *   A1M4 A1M3 A1M2 A1M1
 *   All 1s  → once per second
 *   ...
 *   All 0s  → date/day + hh:mm:ss match
 */
int rtc_set_alarm1(const rtc_alarm_t *alarm)
{
    uint8_t rate  = alarm->rate;
    uint8_t a1m1  = (rate & 0x01) ? 0x80 : 0x00;  /* Seconds mask bit */
    uint8_t a1m2  = (rate & 0x02) ? 0x80 : 0x00;  /* Minutes mask bit */
    uint8_t a1m3  = (rate & 0x04) ? 0x80 : 0x00;  /* Hours mask bit   */
    uint8_t a1m4  = (rate & 0x08) ? 0x80 : 0x00;  /* Day/Date mask bit*/
    uint8_t dydt  = alarm->use_day ? 0x40 : 0x00;  /* DY/DT bit in reg 0x0A */

    uint8_t buf[5];
    buf[0] = DS3231_REG_ALM1_SEC;
    buf[1] = DEC_TO_BCD(alarm->seconds)  | a1m1;
    buf[2] = DEC_TO_BCD(alarm->minutes)  | a1m2;
    buf[3] = DEC_TO_BCD(alarm->hours)    | a1m3;
    buf[4] = DEC_TO_BCD(alarm->day_date) | a1m4 | dydt;

    if (write(i2c_fd, buf, 5) != 5) {
        perror("rtc: set_alarm1 write");
        return -1;
    }

    /* Clear any stale alarm flag */
    return rtc_clear_alarm1_flag();
}

int rtc_enable_alarm1_interrupt(bool enable)
{
    uint8_t ctrl;
    if (rtc_read_register(DS3231_REG_CONTROL, &ctrl) < 0) return -1;

    if (enable)
        ctrl |=  DS3231_CTRL_A1IE;
    else
        ctrl &= ~DS3231_CTRL_A1IE;

    return rtc_write_register(DS3231_REG_CONTROL, ctrl);
}

bool rtc_alarm1_fired(void)
{
    uint8_t status;
    if (rtc_read_register(DS3231_REG_STATUS, &status) < 0) return false;
    return (status & DS3231_STAT_A1F) != 0;
}

int rtc_clear_alarm1_flag(void)
{
    uint8_t status;
    if (rtc_read_register(DS3231_REG_STATUS, &status) < 0) return -1;
    status &= ~DS3231_STAT_A1F;
    return rtc_write_register(DS3231_REG_STATUS, status);
}

/**
 * Read on-chip temperature (DS3231 only).
 * Temperature = MSB + 0.25 * (LSB[7:6])
 * Result in degrees Celsius.
 */
float rtc_read_temperature(void)
{
    uint8_t msb, lsb;
    if (rtc_read_register(DS3231_REG_TEMP_MSB, &msb) < 0) return -999.0f;
    if (rtc_read_register(DS3231_REG_TEMP_LSB, &lsb) < 0) return -999.0f;

    int8_t temp_int = (int8_t)msb;               /* Signed integer part */
    float  temp_frac = ((lsb >> 6) & 0x03) * 0.25f;
    return (float)temp_int + temp_frac;
}
```

---

### Application Example: `main_c.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "rtc_ds3231.h"

static volatile bool running = true;

void sigint_handler(int sig)
{
    (void)sig;
    running = false;
}

int main(void)
{
    signal(SIGINT, sigint_handler);

    /* Open I²C bus 1 (/dev/i2c-1 on Raspberry Pi) */
    if (rtc_init(1) < 0) {
        fprintf(stderr, "Failed to initialize RTC\n");
        return EXIT_FAILURE;
    }

    /* Check if time was lost */
    if (rtc_oscillator_stopped()) {
        printf("WARNING: RTC oscillator was stopped — time is invalid!\n");
        printf("Setting time to 2025-01-15 09:30:00 (Wednesday)\n");

        rtc_time_t t = {
            .seconds = 0,
            .minutes = 30,
            .hours   = 9,
            .day     = 4,   /* Wednesday = 4 */
            .date    = 15,
            .month   = 1,
            .year    = 25,  /* 2025 */
        };

        if (rtc_set_time(&t) < 0) {
            fprintf(stderr, "Failed to set RTC time\n");
            rtc_close();
            return EXIT_FAILURE;
        }
    }

    /* Configure Alarm 1 to fire at HH:MM:SS = 09:31:00 */
    rtc_alarm_t alarm = {
        .seconds   = 0,
        .minutes   = 31,
        .hours     = 9,
        .day_date  = 0,
        .use_day   = false,
        .rate      = ALM1_MATCH_HR_MIN_SEC,
    };

    if (rtc_set_alarm1(&alarm) < 0 || rtc_enable_alarm1_interrupt(true) < 0) {
        fprintf(stderr, "Failed to configure alarm\n");
        rtc_close();
        return EXIT_FAILURE;
    }

    printf("Alarm 1 set for 09:31:00 — polling every second\n\n");

    /* Main polling loop */
    while (running) {
        rtc_time_t now;
        if (rtc_get_time(&now) == 0) {
            printf("\r20%02d-%02d-%02d %02d:%02d:%02d (DoW:%d)",
                   now.year, now.month, now.date,
                   now.hours, now.minutes, now.seconds, now.day);
            fflush(stdout);
        }

        if (rtc_alarm1_fired()) {
            printf("\n*** ALARM 1 FIRED ***\n");
            rtc_clear_alarm1_flag();
        }

        float temp = rtc_read_temperature();
        if (temp > -999.0f)
            printf("  Temp: %.2f°C", temp);

        sleep(1);
    }

    printf("\nShutting down.\n");
    rtc_enable_alarm1_interrupt(false);
    rtc_close();
    return EXIT_SUCCESS;
}
```

---

### Arduino/C++ Example (Wire library, DS3231)

```cpp
#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t DS3231_ADDR = 0x68;

struct DateTime {
    uint8_t second, minute, hour;
    uint8_t dayOfWeek, day, month;
    uint8_t year;   // 0 = 2000
};

inline uint8_t bcdToDec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
inline uint8_t decToBcd(uint8_t d) { return ((d / 10) << 4) | (d % 10);  }

bool rtcRead(DateTime &dt)
{
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(0x00);                         // Start at register 0
    if (Wire.endTransmission(false) != 0) {   // Repeated START
        Serial.println("RTC: I2C error");
        return false;
    }

    Wire.requestFrom(DS3231_ADDR, (uint8_t)7);
    if (Wire.available() < 7) return false;

    dt.second    = bcdToDec(Wire.read() & 0x7F);
    dt.minute    = bcdToDec(Wire.read() & 0x7F);
    dt.hour      = bcdToDec(Wire.read() & 0x3F); // 24-hour mode
    dt.dayOfWeek = bcdToDec(Wire.read() & 0x07);
    dt.day       = bcdToDec(Wire.read() & 0x3F);
    dt.month     = bcdToDec(Wire.read() & 0x1F);
    dt.year      = bcdToDec(Wire.read());
    return true;
}

bool rtcWrite(const DateTime &dt)
{
    Wire.beginTransmission(DS3231_ADDR);
    Wire.write(0x00);                         // Register pointer
    Wire.write(decToBcd(dt.second));
    Wire.write(decToBcd(dt.minute));
    Wire.write(decToBcd(dt.hour));            // 24-hour: MSBs = 0
    Wire.write(decToBcd(dt.dayOfWeek));
    Wire.write(decToBcd(dt.day));
    Wire.write(decToBcd(dt.month));
    Wire.write(decToBcd(dt.year));
    return Wire.endTransmission() == 0;
}

void setup()
{
    Serial.begin(115200);
    Wire.begin();                             // SDA=GPIO21, SCL=GPIO22 on ESP32

    // Set time: 2025-01-15 09:30:00 Wednesday
    DateTime setTime = { 0, 30, 9, 4, 15, 1, 25 };
    if (rtcWrite(setTime))
        Serial.println("RTC time set successfully");
    else
        Serial.println("RTC write failed!");
}

void loop()
{
    DateTime now;
    if (rtcRead(now)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "20%02d-%02d-%02d %02d:%02d:%02d",
                 now.year, now.month, now.day,
                 now.hour, now.minute, now.second);
        Serial.println(buf);
    }
    delay(1000);
}
```

---

## Code Examples in Rust

### Cargo.toml

```toml
[package]
name = "rtc-ds3231"
version = "0.1.0"
edition = "2021"

[dependencies]
linux-embedded-hal = "0.4"
embedded-hal = "1.0"
rppal = { version = "0.18", optional = true }  # Raspberry Pi HAL
anyhow = "1.0"
log = "0.4"
env_logger = "0.11"

[[bin]]
name = "rtc_demo"
path = "src/main.rs"
```

---

### `src/rtc.rs` — DS3231 Driver

```rust
//! DS3231 RTC driver over I²C (embedded-hal 1.0 compatible)

use embedded_hal::i2c::{I2c, SevenBitAddress};
use std::fmt;

/// DS3231 I²C address (fixed)
pub const DS3231_ADDR: SevenBitAddress = 0x68;

/// Register addresses
#[allow(dead_code)]
mod regs {
    pub const SECONDS:   u8 = 0x00;
    pub const MINUTES:   u8 = 0x01;
    pub const HOURS:     u8 = 0x02;
    pub const DAY:       u8 = 0x03;
    pub const DATE:      u8 = 0x04;
    pub const MONTH:     u8 = 0x05;
    pub const YEAR:      u8 = 0x06;
    pub const ALM1_SEC:  u8 = 0x07;
    pub const ALM1_MIN:  u8 = 0x08;
    pub const ALM1_HR:   u8 = 0x09;
    pub const ALM1_DAY:  u8 = 0x0A;
    pub const ALM2_MIN:  u8 = 0x0B;
    pub const ALM2_HR:   u8 = 0x0C;
    pub const ALM2_DAY:  u8 = 0x0D;
    pub const CONTROL:   u8 = 0x0E;
    pub const STATUS:    u8 = 0x0F;
    pub const TEMP_MSB:  u8 = 0x11;
    pub const TEMP_LSB:  u8 = 0x12;
}

/// Control register bit masks
pub mod ctrl {
    pub const A1IE:  u8 = 1 << 0;
    pub const A2IE:  u8 = 1 << 1;
    pub const INTCN: u8 = 1 << 2;
    pub const CONV:  u8 = 1 << 5;
    pub const EOSC:  u8 = 1 << 7;  // Active-low: 0 = oscillator ON
}

/// Status register bit masks
pub mod stat {
    pub const A1F:     u8 = 1 << 0;
    pub const A2F:     u8 = 1 << 1;
    pub const OSF:     u8 = 1 << 7;
}

/// BCD <→ decimal conversions
#[inline]
fn bcd_to_dec(bcd: u8) -> u8 { (bcd >> 4) * 10 + (bcd & 0x0F) }

#[inline]
fn dec_to_bcd(dec: u8) -> u8 { ((dec / 10) << 4) | (dec % 10) }

/// Date/time representation
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct DateTime {
    pub seconds:     u8,  // 0–59
    pub minutes:     u8,  // 0–59
    pub hours:       u8,  // 0–23
    pub day_of_week: u8,  // 1–7
    pub date:        u8,  // 1–31
    pub month:       u8,  // 1–12
    pub year:        u8,  // 0–99 (offset from 2000)
}

impl fmt::Display for DateTime {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "20{:02}-{:02}-{:02} {:02}:{:02}:{:02} (dow:{})",
            self.year, self.month, self.date,
            self.hours, self.minutes, self.seconds,
            self.day_of_week
        )
    }
}

/// Alarm 1 rate configuration
#[derive(Debug, Clone, Copy)]
pub enum Alarm1Rate {
    EverySecond,
    MatchSeconds,
    MatchMinuteSeconds,
    MatchHourMinuteSeconds,
    MatchDateHourMinuteSeconds,
    MatchDayHourMinuteSeconds,
}

/// Alarm 1 configuration
#[derive(Debug, Clone)]
pub struct Alarm1Config {
    pub seconds:  u8,
    pub minutes:  u8,
    pub hours:    u8,
    pub day_date: u8,
    pub rate:     Alarm1Rate,
}

/// Driver error type
#[derive(Debug)]
pub enum RtcError<E> {
    I2c(E),
    InvalidTime,
    OscillatorStopped,
}

impl<E: fmt::Debug> fmt::Display for RtcError<E> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RtcError::I2c(e)            => write!(f, "I2C error: {:?}", e),
            RtcError::InvalidTime       => write!(f, "Invalid time data from RTC"),
            RtcError::OscillatorStopped => write!(f, "RTC oscillator was stopped"),
        }
    }
}

impl<E: fmt::Debug> std::error::Error for RtcError<E> {}

/// DS3231 driver
pub struct Ds3231<I2C> {
    i2c: I2C,
}

impl<I2C, E> Ds3231<I2C>
where
    I2C: I2c<Error = E>,
    E: fmt::Debug,
{
    /// Create a new driver instance and initialise the control register.
    pub fn new(mut i2c: I2C) -> Result<Self, RtcError<E>> {
        // Read current control register
        let mut ctrl_val = [0u8; 1];
        i2c.write_read(DS3231_ADDR, &[regs::CONTROL], &mut ctrl_val)
            .map_err(RtcError::I2c)?;

        // Enable oscillator (clear EOSC), route alarm to INT# (set INTCN)
        let new_ctrl = (ctrl_val[0] & !ctrl::EOSC) | ctrl::INTCN;
        i2c.write(DS3231_ADDR, &[regs::CONTROL, new_ctrl])
            .map_err(RtcError::I2c)?;

        Ok(Self { i2c })
    }

    /// Read a single register.
    fn read_reg(&mut self, reg: u8) -> Result<u8, RtcError<E>> {
        let mut buf = [0u8; 1];
        self.i2c
            .write_read(DS3231_ADDR, &[reg], &mut buf)
            .map_err(RtcError::I2c)?;
        Ok(buf[0])
    }

    /// Write a single register.
    fn write_reg(&mut self, reg: u8, value: u8) -> Result<(), RtcError<E>> {
        self.i2c
            .write(DS3231_ADDR, &[reg, value])
            .map_err(RtcError::I2c)
    }

    /// Burst-read `N` registers starting at `start_reg`.
    fn burst_read<const N: usize>(&mut self, start_reg: u8) -> Result<[u8; N], RtcError<E>> {
        let mut buf = [0u8; N];
        self.i2c
            .write_read(DS3231_ADDR, &[start_reg], &mut buf)
            .map_err(RtcError::I2c)?;
        Ok(buf)
    }

    /// Returns `true` if the oscillator was stopped (time may be invalid).
    pub fn oscillator_stopped(&mut self) -> Result<bool, RtcError<E>> {
        let status = self.read_reg(regs::STATUS)?;
        Ok(status & stat::OSF != 0)
    }

    /// Clears the Oscillator Stop Flag in the status register.
    pub fn clear_osf(&mut self) -> Result<(), RtcError<E>> {
        let status = self.read_reg(regs::STATUS)?;
        self.write_reg(regs::STATUS, status & !stat::OSF)
    }

    /// Read the current date and time.
    ///
    /// Performs an atomic burst read of all 7 time registers. The DS3231
    /// latches them at the START of the I²C transaction, preventing rollover
    /// race conditions.
    pub fn get_time(&mut self) -> Result<DateTime, RtcError<E>> {
        let raw: [u8; 7] = self.burst_read(regs::SECONDS)?;

        let dt = DateTime {
            seconds:     bcd_to_dec(raw[0] & 0x7F),
            minutes:     bcd_to_dec(raw[1] & 0x7F),
            hours:       bcd_to_dec(raw[2] & 0x3F),  // 24-hour mode
            day_of_week: bcd_to_dec(raw[3] & 0x07),
            date:        bcd_to_dec(raw[4] & 0x3F),
            month:       bcd_to_dec(raw[5] & 0x1F),  // Mask century bit
            year:        bcd_to_dec(raw[6]),
        };

        // Validate decoded values
        if dt.seconds > 59 || dt.minutes > 59 || dt.hours > 23
            || dt.date < 1 || dt.date > 31
            || dt.month < 1 || dt.month > 12
        {
            return Err(RtcError::InvalidTime);
        }

        Ok(dt)
    }

    /// Set the date and time.
    ///
    /// Validates the input, writes all 7 registers in a single I²C
    /// transaction, then clears the OSF bit.
    pub fn set_time(&mut self, dt: &DateTime) -> Result<(), RtcError<E>> {
        if dt.seconds > 59 || dt.minutes > 59 || dt.hours > 23
            || dt.date < 1 || dt.date > 31
            || dt.month < 1 || dt.month > 12
            || dt.day_of_week < 1 || dt.day_of_week > 7
        {
            return Err(RtcError::InvalidTime);
        }

        let payload = [
            regs::SECONDS,
            dec_to_bcd(dt.seconds),
            dec_to_bcd(dt.minutes),
            dec_to_bcd(dt.hours),       // 24-hour: bits 7:6 = 00
            dec_to_bcd(dt.day_of_week),
            dec_to_bcd(dt.date),
            dec_to_bcd(dt.month),
            dec_to_bcd(dt.year),
        ];

        self.i2c
            .write(DS3231_ADDR, &payload)
            .map_err(RtcError::I2c)?;

        self.clear_osf()
    }

    /// Configure Alarm 1.
    pub fn set_alarm1(&mut self, config: &Alarm1Config) -> Result<(), RtcError<E>> {
        // Compute mask bits from rate
        let (a1m1, a1m2, a1m3, a1m4) = match config.rate {
            Alarm1Rate::EverySecond                => (0x80, 0x80, 0x80, 0x80),
            Alarm1Rate::MatchSeconds               => (0x00, 0x80, 0x80, 0x80),
            Alarm1Rate::MatchMinuteSeconds         => (0x00, 0x00, 0x80, 0x80),
            Alarm1Rate::MatchHourMinuteSeconds     => (0x00, 0x00, 0x00, 0x80),
            Alarm1Rate::MatchDateHourMinuteSeconds => (0x00, 0x00, 0x00, 0x00),
            Alarm1Rate::MatchDayHourMinuteSeconds  => (0x00, 0x00, 0x00, 0x40), // DY/DT=1
        };

        let payload = [
            regs::ALM1_SEC,
            dec_to_bcd(config.seconds)  | a1m1,
            dec_to_bcd(config.minutes)  | a1m2,
            dec_to_bcd(config.hours)    | a1m3,
            dec_to_bcd(config.day_date) | a1m4,
        ];

        self.i2c
            .write(DS3231_ADDR, &payload)
            .map_err(RtcError::I2c)?;

        // Clear stale alarm flag
        self.clear_alarm1_flag()
    }

    /// Enable or disable the Alarm 1 interrupt output on INT#/SQW pin.
    pub fn enable_alarm1_interrupt(&mut self, enable: bool) -> Result<(), RtcError<E>> {
        let ctrl = self.read_reg(regs::CONTROL)?;
        let new_ctrl = if enable {
            ctrl | ctrl::A1IE
        } else {
            ctrl & !ctrl::A1IE
        };
        self.write_reg(regs::CONTROL, new_ctrl)
    }

    /// Returns `true` if Alarm 1 has fired (A1F set in status register).
    pub fn alarm1_fired(&mut self) -> Result<bool, RtcError<E>> {
        Ok(self.read_reg(regs::STATUS)? & stat::A1F != 0)
    }

    /// Clear the Alarm 1 flag (must be done before the next alarm can fire).
    pub fn clear_alarm1_flag(&mut self) -> Result<(), RtcError<E>> {
        let status = self.read_reg(regs::STATUS)?;
        self.write_reg(regs::STATUS, status & !stat::A1F)
    }

    /// Read on-chip temperature in degrees Celsius (DS3231 only).
    ///
    /// Formula: Temp = MSB (signed) + (LSB[7:6] * 0.25)
    pub fn read_temperature(&mut self) -> Result<f32, RtcError<E>> {
        let msb = self.read_reg(regs::TEMP_MSB)? as i8;
        let lsb = self.read_reg(regs::TEMP_LSB)?;
        let frac = ((lsb >> 6) & 0x03) as f32 * 0.25;
        Ok(msb as f32 + frac)
    }

    /// Consume the driver and return the underlying I²C bus.
    pub fn destroy(self) -> I2C { self.i2c }
}
```

---

### `src/main.rs` — Application

```rust
mod rtc;

use rtc::{Alarm1Config, Alarm1Rate, DateTime, Ds3231};
use linux_embedded_hal::I2cdev;
use std::{thread, time::Duration};
use anyhow::{Context, Result};

fn main() -> Result<()> {
    env_logger::init();

    // Open Linux I²C bus 1
    let i2c = I2cdev::new("/dev/i2c-1")
        .context("Failed to open /dev/i2c-1")?;

    let mut rtc = Ds3231::new(i2c)
        .context("Failed to initialise DS3231")?;

    // ── Check oscillator stop flag ──────────────────────────────────────
    if rtc.oscillator_stopped().context("OSF check failed")? {
        eprintln!("WARNING: RTC oscillator was stopped — time is invalid!");
        eprintln!("Setting time to 2025-01-15 09:30:00");

        let initial_time = DateTime {
            seconds:     0,
            minutes:     30,
            hours:       9,
            day_of_week: 4,   // Wednesday
            date:        15,
            month:       1,
            year:        25,  // 2025
        };

        rtc.set_time(&initial_time)
            .context("Failed to set RTC time")?;
    }

    // ── Read and display current time ───────────────────────────────────
    let now = rtc.get_time().context("Failed to read time")?;
    println!("Current RTC time: {}", now);

    // ── Configure Alarm 1 to fire at 09:31:00 ──────────────────────────
    let alarm = Alarm1Config {
        seconds:  0,
        minutes:  31,
        hours:    9,
        day_date: 0,   // Not used for ALM1_MATCH_HR_MIN_SEC
        rate:     Alarm1Rate::MatchHourMinuteSeconds,
    };

    rtc.set_alarm1(&alarm)
        .context("Failed to set alarm 1")?;
    rtc.enable_alarm1_interrupt(true)
        .context("Failed to enable alarm 1 interrupt")?;

    println!("Alarm 1 armed for 09:31:00. Polling every second...\n");

    // ── Polling loop ────────────────────────────────────────────────────
    loop {
        match rtc.get_time() {
            Ok(dt) => {
                print!("\r{}", dt);

                if let Ok(temp) = rtc.read_temperature() {
                    print!("  Temp: {:.2}°C", temp);
                }

                // Flush stdout (no newline in the loop)
                use std::io::Write;
                let _ = std::io::stdout().flush();
            }
            Err(e) => eprintln!("\nFailed to read time: {}", e),
        }

        match rtc.alarm1_fired() {
            Ok(true) => {
                println!("\n*** ALARM 1 FIRED ***");
                rtc.clear_alarm1_flag()
                    .context("Failed to clear alarm flag")?;
            }
            Ok(false) => {}
            Err(e) => eprintln!("\nAlarm check error: {}", e),
        }

        thread::sleep(Duration::from_secs(1));
    }
}
```

---

### Rust: Embedded no_std Example (STM32 / Embassy)

```rust
//! no_std example using Embassy async I²C (STM32F4)
#![no_std]
#![no_main]

use embassy_stm32::{
    i2c::{Config as I2cConfig, I2c},
    time::Hertz,
};
use embassy_time::{Duration, Timer};
use defmt::*;
use {defmt_rtt as _, panic_probe as _};

const DS3231_ADDR: u8 = 0x68;

fn bcd_to_dec(b: u8) -> u8 { (b >> 4) * 10 + (b & 0x0F) }
fn dec_to_bcd(d: u8) -> u8 { ((d / 10) << 4) | (d % 10)  }

#[embassy_executor::main]
async fn main(_spawner: embassy_executor::Spawner) {
    let p = embassy_stm32::init(Default::default());

    // Initialise I²C1 at 100 kHz
    let mut i2c = I2c::new(
        p.I2C1,
        p.PB8,                // SCL
        p.PB9,                // SDA
        Irqs,
        p.DMA1_CH6,
        p.DMA1_CH0,
        Hertz(100_000),
        I2cConfig::default(),
    );

    // ── Set time ─────────────────────────────────────────────────────
    let set_payload: [u8; 8] = [
        0x00,                 // Start register
        dec_to_bcd(0),        // Seconds
        dec_to_bcd(30),       // Minutes
        dec_to_bcd(9),        // Hours (24h)
        dec_to_bcd(4),        // Day of week
        dec_to_bcd(15),       // Date
        dec_to_bcd(1),        // Month
        dec_to_bcd(25),       // Year (2025)
    ];

    if let Err(e) = i2c.write(DS3231_ADDR, &set_payload).await {
        error!("Failed to set RTC time: {:?}", e);
    } else {
        info!("RTC time set successfully");
    }

    // ── Polling loop ──────────────────────────────────────────────────
    loop {
        let mut buf = [0u8; 7];

        // Write register pointer
        if i2c.write(DS3231_ADDR, &[0x00]).await.is_err() {
            error!("I2C write failed");
            Timer::after(Duration::from_secs(1)).await;
            continue;
        }

        // Read 7 time registers
        if i2c.read(DS3231_ADDR, &mut buf).await.is_err() {
            error!("I2C read failed");
            Timer::after(Duration::from_secs(1)).await;
            continue;
        }

        let sec  = bcd_to_dec(buf[0] & 0x7F);
        let min  = bcd_to_dec(buf[1] & 0x7F);
        let hr   = bcd_to_dec(buf[2] & 0x3F);
        let day  = bcd_to_dec(buf[4] & 0x3F);
        let mon  = bcd_to_dec(buf[5] & 0x1F);
        let year = bcd_to_dec(buf[6]);

        info!("20{:02}-{:02}-{:02} {:02}:{:02}:{:02}", year, mon, day, hr, min, sec);

        Timer::after(Duration::from_secs(1)).await;
    }
}
```

---

## Summary

Real-Time Clock integration over I²C is a foundational embedded systems skill that brings accurate, persistent timekeeping to any project. Here are the key takeaways:

### I²C Communication
- RTCs use standard I²C at 100–400 kHz; pull-up resistors (2.2–4.7 kΩ) on SDA and SCL are mandatory
- Always use a **burst read** starting at register `0x00` to atomically capture all time registers — this prevents reading a "torn" time value across a seconds rollover
- A burst write of all 7 time registers in a single transaction ensures the RTC starts counting from a coherent state

### BCD Encoding
- All RTC registers store values in **Binary Coded Decimal (BCD)**, not binary
- Implement `BCD_TO_DEC` and `DEC_TO_BCD` helper macros/functions from the start — forgetting this is the most common source of bugs

### Alarm Configuration
- DS3231 Alarm 1 offers second-level granularity through a set of **mask bits** (A1M1–A1M4)
- Always **clear the alarm flag** in the status register after servicing an interrupt, or the alarm will not fire again
- Use the `INTCN` bit in the Control register to route alarms to the hardware `INT#` pin for true wake-from-sleep capability

### Battery Backup
- The **OSF (Oscillator Stop Flag)** is the definitive indicator that the RTC lost power and time is no longer valid — check it at every startup
- CR2032 cells typically last 5–10 years powering an RTC alone; calculate life using `Capacity / Ibat_backup`
- Add a Schottky diode between VCC and VBAT to prevent the backup battery from powering the host system
- Consider a supercapacitor for applications with frequent power cycling

### Code Architecture (C and Rust)
- In **C**, abstract hardware I²C into `read_register()` / `burst_read()` / `write_register()` helpers for portability across Linux, STM32 HAL, and Arduino
- In **Rust**, the `embedded-hal` trait abstraction (`I2c` trait) makes drivers compile for both hosted Linux targets and bare-metal MCUs with no code changes — only the HAL instantiation differs
- Always validate decoded BCD values before using them in application logic

### Choosing an RTC IC
- **DS1307**: Low cost, basic features, no temperature compensation — good for non-critical use
- **DS3231**: Best choice for most projects — high accuracy TCXO, onboard temperature sensor, two alarms
- **PCF8563**: Ideal for ultra-low-power IoT nodes — very low backup current (~250 nA)
- **RV-3028-C7**: Best-in-class accuracy (±1 ppm/day) and lowest power for demanding applications

---

*Document covers DS3231 in detail; principles apply equally to DS1307, PCF8523, PCF8563, MCP7940N, and RV-3028-C7 with datasheet-specific register differences.*