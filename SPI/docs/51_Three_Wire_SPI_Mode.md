# 51. Three-Wire SPI Mode

**Physical & Protocol** — The three signals (SCK, CS, SIO), the two-phase transaction structure (master drives → turnaround → slave drives), and a timing diagram showing CS, SCK, and the bidirectional SIO transitions.

**Electrical** — Pull-up resistor sizing, bus contention mechanics, and how to safely handle the turnaround window (open-drain vs push-pull strategies, series resistors).

**C/C++ code** — Three layers:
1. A fully portable **bit-bang driver** with a HAL callback struct (abstracts GPIO from logic), covering `write`, `read`, and `write_then_read` with correct CPOL/CPHA handling.
2. A **sensor usage example** showing register reads and burst reads.
3. An **STM32 hardware SPI** example using `BIDIMODE`/`BIDIOE` register bits with correct `BSY`/`TXE` flag polling before direction switch.

**Rust code** — A trait-based design (`ThreeWireHal`) with a generic `ThreeWireSpi<H>` driver, a `SimulatedHal` for unit testing, and a full sensor driver example with `#[cfg(test)]` unit tests.

**Linux spidev** — Using `SPI_3WIRE` mode flag with a two-transfer `SPI_IOC_MESSAGE(2)` ioctl, which lets the kernel manage direction internally.

**Pitfalls** — Six named failure modes: bus contention timing, forgetting output re-assertion, missing pull-ups, STM32 early `BIDIOE` clear, multi-slave CS conflicts, and logic analyser capture issues.

## Half-Duplex SPI Using a Bidirectional Data Line

---

## Table of Contents

1. [Overview](#overview)
2. [Physical Layer and Signal Description](#physical-layer-and-signal-description)
3. [Protocol Mechanics](#protocol-mechanics)
4. [Electrical Considerations](#electrical-considerations)
5. [Timing and Direction Switching](#timing-and-direction-switching)
6. [Comparison: Standard SPI vs Three-Wire SPI](#comparison-standard-spi-vs-three-wire-spi)
7. [Common Use Cases and Devices](#common-use-cases-and-devices)
8. [C/C++ Implementation](#cc-implementation)
9. [Rust Implementation](#rust-implementation)
10. [Linux Kernel / spidev Interface](#linux-kernel--spidev-interface)
11. [Pitfalls and Gotchas](#pitfalls-and-gotchas)
12. [Summary](#summary)

---

## Overview

**Three-wire SPI** (also called **3-wire SPI**, **half-duplex SPI**, or **SPI with bidirectional data line**) is a variant of the standard four-wire SPI bus that merges the two unidirectional data lines — MOSI (Master Out Slave In) and MISO (Master In Slave Out) — into a single, shared, bidirectional line commonly called **SIO** (Serial I/O), **SDIO**, or **SDA** (depending on the vendor).

The trade-off is straightforward:

| Property            | Standard SPI (4-wire) | Three-Wire SPI (3-wire) |
|---------------------|-----------------------|-------------------------|
| Data lines          | 2 (MOSI + MISO)       | 1 (SIO)                 |
| Total signal wires  | 4 (SCK, CS, MOSI, MISO) | 3 (SCK, CS, SIO)      |
| Duplex mode         | Full-duplex           | Half-duplex             |
| Simultaneous TX/RX  | Yes                   | No                      |
| Pin count savings   | —                     | 1 pin per slave         |

The elimination of one wire comes at the cost of **simultaneity**: the master and slave cannot transmit at the same time. Instead, the direction of the shared data line must be explicitly managed in software (or hardware) between transmit and receive phases.

---

## Physical Layer and Signal Description

### Signals

```
Master                          Slave
  ┌─────────────────────────────────────┐
  │  SCK  ──────────────────────────►  │  Clock
  │  CS   ──────────────────────────►  │  Chip Select (active low)
  │  SIO  ◄────────────────────────►   │  Bidirectional Data
  └─────────────────────────────────────┘
```

- **SCK (Serial Clock):** Always driven by the master. Polarity and phase follow the configured SPI mode (CPOL/CPHA), identical to standard SPI.
- **CS / NSS (Chip Select):** Active-low signal driven by the master to select the target slave.
- **SIO (Serial I/O):** The shared data line. During the **command/write phase**, the master drives this line. During the **read/response phase**, the master tristates (releases) the line and the slave drives it.

### Line States

```
SIO Line States:
  ┌──────────┬──────────────────────────────────────────────────┐
  │  State   │  Description                                     │
  ├──────────┼──────────────────────────────────────────────────┤
  │  Driven  │  Master or Slave actively outputting 0 or 1      │
  │  Hi-Z    │  Driver disabled (tristated), line floats        │
  │  Pull-up │  Weak pull-up resistor holds line high when idle │
  └──────────┴──────────────────────────────────────────────────┘
```

A **pull-up resistor** (typically 4.7 kΩ–10 kΩ) is usually placed on the SIO line to ensure a defined idle state when neither master nor slave is driving it.

---

## Protocol Mechanics

### Transaction Structure

A typical three-wire SPI read transaction (e.g., reading a register from a sensor) follows this pattern:

```
Phase 1: Master Drives (Write / Command Phase)
  CS asserted LOW
  Master drives SIO → sends command/address bytes
  Slave tristates SIO (or ignores)

[Turnaround / Direction Switch]
  Master tristates SIO
  Slave takes control of SIO

Phase 2: Slave Drives (Read / Response Phase)
  Slave drives SIO → sends data bytes
  Master samples SIO on each clock edge
  CS deasserted HIGH (end of transaction)
```

### Timing Diagram

```
CS    ‾‾‾‾‾‾|___________________________________|‾‾‾‾‾‾
            ←── Write Phase ──→ ←── Read Phase ──→

SCK         _‾_‾_‾_‾_‾_‾_‾_‾_ _‾_‾_‾_‾_‾_‾_‾_‾_

SIO(W)      [CMD/ADDR bits   ] [Hi-Z / Released  ]
SIO(R)      [Hi-Z            ] [DATA bits        ]

Direction   MASTER DRIVES      SLAVE DRIVES
```

The critical moment is the **turnaround**: exactly when the master releases SIO (tristates its output) and the slave starts driving. Many devices specify a **turnaround clock count** — a number of dummy clock pulses during which neither side drives — to prevent bus contention.

### Write-Only Transaction

When the master only writes (no response expected):

```
CS  ‾‾|_________________________|‾‾
SCK    _‾_‾_‾_‾_‾_‾_‾_‾_‾_‾_‾_
SIO    [DATA bits, master drives ]
```

This is identical to standard SPI MOSI, with MISO simply ignored (or absent).

---

## Electrical Considerations

### Bus Contention Risk

The most dangerous failure mode in three-wire SPI is **bus contention**: both master and slave driving the shared line simultaneously. This causes:

- High current flow through output drivers
- Undefined logic level on SIO
- Potential damage to GPIO driver transistors

Prevention strategies:

1. **Strict protocol sequencing:** Ensure the master tristates before the slave begins driving. Many devices automatically tristate their output until commanded otherwise.
2. **Turnaround clocks:** Insert one or more dummy clock cycles where both sides are high-impedance.
3. **Open-drain outputs:** Some implementations use open-drain drivers with a pull-up, so contention causes only logic level ambiguity, not hardware damage.
4. **Current-limiting resistors:** Small series resistors (33–100 Ω) on the SIO line can absorb contention current during brief overlap.

### Pull-Up Resistor Sizing

```
         VCC
          │
         [R]   ← Pull-up (4.7 kΩ typical)
          │
SIO ──────┤────── to master GPIO and slave SIO pin
          │
```

- **Too weak (high R):** Slow rise time, signal integrity issues at high clock rates.
- **Too strong (low R):** Excessive current when SIO is driven low; increased power consumption.
- **Rule of thumb:** Choose R such that rise time ≤ 10% of SCK period.

---

## Timing and Direction Switching

### GPIO-Bit-Banged Direction Control

When bit-banging three-wire SPI, direction switching is explicit:

```
Write mode:  GPIO configured as OUTPUT, driven by master
Read mode:   GPIO configured as INPUT (high-impedance)
```

The sequence must guarantee the master's output driver is **disabled before** the slave begins driving. The order matters:

```
❌ Wrong (causes contention):
   Slave starts driving → Master disables output

✅ Correct:
   Master disables output → Slave starts driving (after turnaround clocks)
```

### Hardware SPI Controller Direction Control

Many microcontroller SPI peripherals support a dedicated **bidirectional mode** register bit:

- **STM32:** `SPI_CR1_BIDIMODE` (bidirectional mode) + `SPI_CR1_BIDIOE` (output enable)
- **NXP LPC:** `SPI_CFG_LOOP` and related control registers
- **RP2040:** PIO state machines can implement custom three-wire protocols

When `BIDIOE = 1` → SIO is an output (master transmits).  
When `BIDIOE = 0` → SIO is an input (master receives, slave transmits).

---

## Comparison: Standard SPI vs Three-Wire SPI

```
Standard 4-Wire SPI:
  Master ──MOSI──► Slave
  Master ◄──MISO── Slave
  Master ──SCK───► Slave
  Master ──CS────► Slave
  (Full duplex — simultaneous TX and RX possible)

Three-Wire SPI:
  Master ◄──SIO──► Slave
  Master ──SCK───► Slave
  Master ──CS────► Slave
  (Half duplex — TX and RX alternate in time)
```

| Feature                     | 4-Wire SPI              | 3-Wire SPI                    |
|-----------------------------|-------------------------|-------------------------------|
| Wire count                  | 4                       | 3                             |
| Duplex                      | Full                    | Half                          |
| Simultaneous TX+RX          | Yes                     | No                            |
| Throughput                  | Higher (concurrent)     | Lower (sequential)            |
| Complexity                  | Simple                  | Moderate (direction switching)|
| Risk of bus contention      | None                    | Requires careful management   |
| Typical use                 | General purpose         | Space-constrained, low-pin    |
| Multi-slave support         | Straightforward         | Each slave needs its own CS   |

---

## Common Use Cases and Devices

Three-wire SPI is popular in:

- **Display controllers:** Nokia 5110, SSD1306 (some variants), Adafruit displays
- **IMU/sensors:** Bosch BMI160, BMA400, Invensense ICM-42688 (3-wire mode selectable)
- **RF/wireless modules:** nRF24L01 (optional 3-wire), various Bluetooth modules
- **ADC/DAC:** Some Microchip MCP devices in space-constrained designs
- **Wearables and IoT:** Any design where PCB routing or connector pin count is constrained

---

## C/C++ Implementation

### 1. Bit-Bang Three-Wire SPI (Platform-Agnostic)

This implementation abstracts hardware through function pointers, making it portable across any microcontroller.

```c
/**
 * three_wire_spi.h
 * Portable bit-bang three-wire SPI driver.
 */
#ifndef THREE_WIRE_SPI_H
#define THREE_WIRE_SPI_H

#include <stdint.h>
#include <stdbool.h>

/* Direction constants */
typedef enum {
    SIO_DIR_OUTPUT = 0,   /* Master drives SIO */
    SIO_DIR_INPUT  = 1    /* Slave drives SIO  */
} SioDirection;

/* SPI mode (CPOL/CPHA) */
typedef enum {
    SPI_MODE_0 = 0,  /* CPOL=0, CPHA=0 */
    SPI_MODE_1 = 1,  /* CPOL=0, CPHA=1 */
    SPI_MODE_2 = 2,  /* CPOL=1, CPHA=0 */
    SPI_MODE_3 = 3   /* CPOL=1, CPHA=1 */
} SpiMode;

/**
 * Platform HAL callbacks.
 * The user supplies these to decouple the driver from hardware.
 */
typedef struct {
    void (*set_sck)(bool level);
    void (*set_cs)(bool level);          /* active low */
    void (*set_sio)(bool level);
    bool (*get_sio)(void);
    void (*set_sio_dir)(SioDirection);   /* switch GPIO direction */
    void (*delay_half_cycle)(void);      /* half SCK period delay */
} ThreeWireSpiHal;

typedef struct {
    ThreeWireSpiHal hal;
    SpiMode         mode;
    uint8_t         turnaround_clocks;   /* dummy clocks between TX→RX */
} ThreeWireSpi;

/* API */
void    tw_spi_init(ThreeWireSpi *spi, ThreeWireSpiHal hal,
                    SpiMode mode, uint8_t turnaround_clocks);
void    tw_spi_write_byte(ThreeWireSpi *spi, uint8_t byte);
uint8_t tw_spi_read_byte(ThreeWireSpi *spi);
void    tw_spi_write(ThreeWireSpi *spi, const uint8_t *buf, size_t len);
void    tw_spi_read(ThreeWireSpi *spi, uint8_t *buf, size_t len);
void    tw_spi_write_then_read(ThreeWireSpi *spi,
                               const uint8_t *tx_buf, size_t tx_len,
                               uint8_t *rx_buf,       size_t rx_len);

#endif /* THREE_WIRE_SPI_H */
```

```c
/**
 * three_wire_spi.c
 * Bit-bang implementation of three-wire (half-duplex) SPI.
 */
#include "three_wire_spi.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static inline bool cpol(const ThreeWireSpi *s) { return (s->mode & 0x02) != 0; }
static inline bool cpha(const ThreeWireSpi *s) { return (s->mode & 0x01) != 0; }

/** Produce one SCK clock pulse. */
static void clock_pulse(ThreeWireSpi *s) {
    s->hal.delay_half_cycle();
    s->hal.set_sck(!cpol(s));   /* active edge */
    s->hal.delay_half_cycle();
    s->hal.set_sck( cpol(s));   /* return to idle */
}

/** Emit `n` turnaround dummy clocks with SIO in high-impedance. */
static void turnaround(ThreeWireSpi *s) {
    /* Master releases SIO FIRST to avoid bus contention */
    s->hal.set_sio_dir(SIO_DIR_INPUT);

    for (uint8_t i = 0; i < s->turnaround_clocks; i++) {
        clock_pulse(s);
    }
    /* Slave is now expected to drive SIO from this point */
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void tw_spi_init(ThreeWireSpi *spi, ThreeWireSpiHal hal,
                 SpiMode mode, uint8_t turnaround_clocks)
{
    spi->hal               = hal;
    spi->mode              = mode;
    spi->turnaround_clocks = turnaround_clocks;

    /* Set bus to idle state */
    spi->hal.set_sck(cpol(spi));
    spi->hal.set_cs(true);                   /* deselect (active low) */
    spi->hal.set_sio_dir(SIO_DIR_OUTPUT);
    spi->hal.set_sio(true);                  /* idle high */
}

/**
 * Transmit one byte MSB-first on SIO (master drives).
 * CS must already be asserted by the caller.
 */
void tw_spi_write_byte(ThreeWireSpi *spi, uint8_t byte)
{
    spi->hal.set_sio_dir(SIO_DIR_OUTPUT);

    for (int bit = 7; bit >= 0; bit--) {
        bool b = (byte >> bit) & 0x01;

        if (!cpha(spi)) {
            /* CPHA=0: data set before rising edge */
            spi->hal.set_sio(b);
            spi->hal.delay_half_cycle();
            spi->hal.set_sck(!cpol(spi));    /* active edge: sample here */
            spi->hal.delay_half_cycle();
            spi->hal.set_sck( cpol(spi));    /* return to idle */
        } else {
            /* CPHA=1: data set after first clock edge */
            spi->hal.set_sck(!cpol(spi));
            spi->hal.set_sio(b);
            spi->hal.delay_half_cycle();
            spi->hal.set_sck( cpol(spi));    /* active edge: sample here */
            spi->hal.delay_half_cycle();
        }
    }
}

/**
 * Receive one byte MSB-first from SIO (slave drives).
 * CS must already be asserted. turnaround() must have been called.
 */
uint8_t tw_spi_read_byte(ThreeWireSpi *spi)
{
    uint8_t byte = 0;

    /* Ensure master is in input mode (should be from turnaround) */
    spi->hal.set_sio_dir(SIO_DIR_INPUT);

    for (int bit = 7; bit >= 0; bit--) {
        if (!cpha(spi)) {
            spi->hal.delay_half_cycle();
            spi->hal.set_sck(!cpol(spi));    /* active edge */
            byte |= (spi->hal.get_sio() ? 1u : 0u) << bit;
            spi->hal.delay_half_cycle();
            spi->hal.set_sck( cpol(spi));
        } else {
            spi->hal.set_sck(!cpol(spi));
            spi->hal.delay_half_cycle();
            spi->hal.set_sck( cpol(spi));    /* active edge */
            byte |= (spi->hal.get_sio() ? 1u : 0u) << bit;
            spi->hal.delay_half_cycle();
        }
    }

    return byte;
}

/** Write a buffer MSB-first (master drives entire time). */
void tw_spi_write(ThreeWireSpi *spi, const uint8_t *buf, size_t len)
{
    spi->hal.set_cs(false);   /* assert CS */

    for (size_t i = 0; i < len; i++) {
        tw_spi_write_byte(spi, buf[i]);
    }

    spi->hal.set_cs(true);    /* deassert CS */
    spi->hal.set_sio_dir(SIO_DIR_OUTPUT);
    spi->hal.set_sio(true);
}

/** Read a buffer (slave drives after turnaround). */
void tw_spi_read(ThreeWireSpi *spi, uint8_t *buf, size_t len)
{
    spi->hal.set_cs(false);
    turnaround(spi);           /* master releases, slave takes over */

    for (size_t i = 0; i < len; i++) {
        buf[i] = tw_spi_read_byte(spi);
    }

    spi->hal.set_cs(true);
    spi->hal.set_sio_dir(SIO_DIR_OUTPUT);
    spi->hal.set_sio(true);
}

/**
 * Write command/address bytes, then read response bytes.
 * This is the most common three-wire SPI transaction pattern.
 *
 * Example: send 1-byte register address, receive N bytes of data.
 */
void tw_spi_write_then_read(ThreeWireSpi *spi,
                             const uint8_t *tx_buf, size_t tx_len,
                             uint8_t       *rx_buf, size_t rx_len)
{
    spi->hal.set_cs(false);

    /* Phase 1: Master transmits command/address */
    for (size_t i = 0; i < tx_len; i++) {
        tw_spi_write_byte(spi, tx_buf[i]);
    }

    /*
     * Phase 2: Direction switch.
     * Master tristates SIO, emits turnaround clocks.
     * Slave takes control of SIO afterwards.
     */
    turnaround(spi);

    /* Phase 3: Slave drives response data */
    for (size_t i = 0; i < rx_len; i++) {
        rx_buf[i] = tw_spi_read_byte(spi);
    }

    spi->hal.set_cs(true);
    spi->hal.set_sio_dir(SIO_DIR_OUTPUT);
    spi->hal.set_sio(true);
}
```

### 2. Usage Example: Reading an Imaginary Sensor

```c
/**
 * example_sensor.c
 * Demonstrates reading a register from a three-wire SPI sensor.
 * Platform-specific HAL functions are stubs — replace with real GPIO calls.
 */
#include "three_wire_spi.h"
#include <stdio.h>

/* ---- Platform HAL stubs (replace with real MCU GPIO calls) ---- */

static void hal_set_sck(bool level)         { /* GPIO_Write(SCK_PIN, level) */ }
static void hal_set_cs(bool level)          { /* GPIO_Write(CS_PIN,  level) */ }
static void hal_set_sio(bool level)         { /* GPIO_Write(SIO_PIN, level) */ }
static bool hal_get_sio(void)               { return false; /* GPIO_Read(SIO_PIN) */ }
static void hal_set_sio_dir(SioDirection d) {
    /* e.g. STM32:
       if (d == SIO_DIR_OUTPUT)
           GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
       else
           GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
       HAL_GPIO_Init(SIO_GPIO_Port, &GPIO_InitStruct);
    */
}
static void hal_delay_half_cycle(void) {
    /* Delay for half SCK period.
       At 1 MHz SCK: half period = 500 ns.
       Use hardware timer or __NOP() loops. */
    for (volatile int i = 0; i < 10; i++) { __asm__("nop"); }
}

/* ---- Sensor register addresses ---- */
#define REG_WHO_AM_I    0x0F   /* Read: device ID */
#define REG_TEMP_MSB    0x26   /* Read: temperature high byte */
#define REG_TEMP_LSB    0x27   /* Read: temperature low byte  */
#define READ_FLAG       0x80   /* Bit 7 set = read operation  */

int main(void)
{
    ThreeWireSpiHal hal = {
        .set_sck        = hal_set_sck,
        .set_cs         = hal_set_cs,
        .set_sio        = hal_set_sio,
        .get_sio        = hal_get_sio,
        .set_sio_dir    = hal_set_sio_dir,
        .delay_half_cycle = hal_delay_half_cycle,
    };

    ThreeWireSpi spi;
    tw_spi_init(&spi, hal, SPI_MODE_0, /*turnaround_clocks=*/1);

    /* --- Read WHO_AM_I register --- */
    uint8_t cmd    = REG_WHO_AM_I | READ_FLAG;
    uint8_t who_am_i = 0;
    tw_spi_write_then_read(&spi, &cmd, 1, &who_am_i, 1);
    printf("WHO_AM_I = 0x%02X\n", who_am_i);

    /* --- Read 2-byte temperature --- */
    uint8_t temp_cmd[2] = { REG_TEMP_MSB | READ_FLAG, REG_TEMP_LSB | READ_FLAG };
    uint8_t temp_data[2] = {0};
    /*
     * Some devices auto-increment the register address during a burst read.
     * Send only the first register address in the command, then read N bytes.
     */
    uint8_t burst_cmd = REG_TEMP_MSB | READ_FLAG;
    tw_spi_write_then_read(&spi, &burst_cmd, 1, temp_data, 2);

    int16_t temperature_raw = (int16_t)((temp_data[0] << 8) | temp_data[1]);
    float   temperature_c   = temperature_raw / 16.0f;
    printf("Temperature: %.2f °C\n", temperature_c);

    return 0;
}
```

### 3. STM32 Hardware SPI in Bidirectional Mode (C)

```c
/**
 * stm32_3wire_spi.c
 * Uses STM32 HAL with BIDIMODE enabled (hardware 3-wire SPI).
 *
 * CubeMX configuration:
 *   Mode:              Half-Duplex Master
 *   Data Size:         8 Bits
 *   CPOL:              Low
 *   CPHA:              1 Edge
 *   NSS:               Software
 */
#include "stm32f4xx_hal.h"

extern SPI_HandleTypeDef hspi1;

#define SENSOR_CS_GPIO   GPIOA
#define SENSOR_CS_PIN    GPIO_PIN_4

#define READ_BIT         0x80

static void cs_assert(void)   { HAL_GPIO_WritePin(SENSOR_CS_GPIO, SENSOR_CS_PIN, GPIO_PIN_RESET); }
static void cs_deassert(void) { HAL_GPIO_WritePin(SENSOR_CS_GPIO, SENSOR_CS_PIN, GPIO_PIN_SET);   }

/**
 * Three-wire SPI: write command bytes, then switch to receive and
 * read response bytes using hardware BIDIMODE.
 */
HAL_StatusTypeDef spi3w_write_then_read(
    SPI_HandleTypeDef *hspi,
    const uint8_t *tx_buf, uint16_t tx_len,
    uint8_t       *rx_buf, uint16_t rx_len)
{
    HAL_StatusTypeDef status;

    cs_assert();

    /*
     * Transmit phase: BIDIOE=1 → SIO is output.
     * HAL_SPI_Transmit uses BIDIMODE if CR1_BIDIMODE is set.
     */
    status = HAL_SPI_Transmit(hspi, (uint8_t *)tx_buf, tx_len, HAL_MAX_DELAY);
    if (status != HAL_OK) goto done;

    /*
     * Direction switch: clear BIDIOE → SIO becomes input (high-Z).
     * The slave will begin driving SIO after this point.
     *
     * IMPORTANT: Must wait for TXE and BSY flags to clear before
     *            switching direction to avoid cutting off the last bit.
     */
    while (__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_BSY)) { /* wait */ }
    CLEAR_BIT(hspi->Instance->CR1, SPI_CR1_BIDIOE);

    /*
     * Receive phase: BIDIOE=0 → SIO is input.
     * HAL_SPI_Receive works in bidirectional input mode.
     */
    status = HAL_SPI_Receive(hspi, rx_buf, rx_len, HAL_MAX_DELAY);

done:
    cs_deassert();

    /* Restore BIDIOE=1 for next transmit operation */
    SET_BIT(hspi->Instance->CR1, SPI_CR1_BIDIOE);

    return status;
}

/* ---- Example: read sensor register ---- */
void sensor_read_register_example(void)
{
    uint8_t cmd     = 0x0F | READ_BIT;   /* register address + read flag */
    uint8_t value   = 0;

    spi3w_write_then_read(&hspi1, &cmd, 1, &value, 1);
    /* value now contains the register data */
}
```

---

## Rust Implementation

### 1. Trait Definitions and Bit-Bang Driver

```rust
//! three_wire_spi.rs
//!
//! Portable bit-bang three-wire (half-duplex) SPI driver for Rust.
//! Uses embedded-hal-style traits for platform abstraction.

use core::convert::Infallible;

/// SPI mode: CPOL and CPHA combination.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum SpiMode {
    Mode0, // CPOL=0, CPHA=0
    Mode1, // CPOL=0, CPHA=1
    Mode2, // CPOL=1, CPHA=0
    Mode3, // CPOL=1, CPHA=1
}

impl SpiMode {
    pub fn cpol(&self) -> bool {
        matches!(self, SpiMode::Mode2 | SpiMode::Mode3)
    }
    pub fn cpha(&self) -> bool {
        matches!(self, SpiMode::Mode1 | SpiMode::Mode3)
    }
}

/// Direction of the shared SIO line.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum SioDirection {
    Output, // Master drives
    Input,  // Slave drives
}

/// Platform HAL trait: the user implements this for their target hardware.
pub trait ThreeWireHal {
    type Error;

    fn set_sck(&mut self, level: bool) -> Result<(), Self::Error>;
    fn set_cs(&mut self, level: bool) -> Result<(), Self::Error>;
    fn set_sio(&mut self, level: bool) -> Result<(), Self::Error>;
    fn get_sio(&mut self) -> Result<bool, Self::Error>;
    fn set_sio_direction(&mut self, dir: SioDirection) -> Result<(), Self::Error>;
    fn delay_half_cycle(&mut self) -> Result<(), Self::Error>;
}

/// Three-wire SPI driver.
pub struct ThreeWireSpi<H: ThreeWireHal> {
    hal: H,
    mode: SpiMode,
    turnaround_clocks: u8,
}

impl<H: ThreeWireHal> ThreeWireSpi<H> {
    /// Create and initialise a new three-wire SPI driver.
    pub fn new(
        mut hal: H,
        mode: SpiMode,
        turnaround_clocks: u8,
    ) -> Result<Self, H::Error> {
        // Set bus to idle state
        hal.set_sck(mode.cpol())?;
        hal.set_cs(true)?;                      // deassert (active low)
        hal.set_sio_direction(SioDirection::Output)?;
        hal.set_sio(true)?;                     // idle high

        Ok(Self { hal, mode, turnaround_clocks })
    }

    // ----------------------------------------------------------------
    // Internal helpers
    // ----------------------------------------------------------------

    fn clock_pulse(&mut self) -> Result<(), H::Error> {
        self.hal.delay_half_cycle()?;
        self.hal.set_sck(!self.mode.cpol())?;   // active edge
        self.hal.delay_half_cycle()?;
        self.hal.set_sck(self.mode.cpol())?;    // return to idle
        Ok(())
    }

    /// Release SIO and emit dummy turnaround clocks.
    /// Master tristates BEFORE slave begins driving — no bus contention.
    fn turnaround(&mut self) -> Result<(), H::Error> {
        self.hal.set_sio_direction(SioDirection::Input)?;
        for _ in 0..self.turnaround_clocks {
            self.clock_pulse()?;
        }
        Ok(())
    }

    /// Transmit one byte MSB-first (master drives SIO).
    fn write_byte(&mut self, byte: u8) -> Result<(), H::Error> {
        self.hal.set_sio_direction(SioDirection::Output)?;

        for bit in (0..8).rev() {
            let b = (byte >> bit) & 0x01 != 0;

            if !self.mode.cpha() {
                // CPHA=0: data valid before active clock edge
                self.hal.set_sio(b)?;
                self.hal.delay_half_cycle()?;
                self.hal.set_sck(!self.mode.cpol())?;   // active edge
                self.hal.delay_half_cycle()?;
                self.hal.set_sck(self.mode.cpol())?;
            } else {
                // CPHA=1: data valid after first clock edge
                self.hal.set_sck(!self.mode.cpol())?;
                self.hal.set_sio(b)?;
                self.hal.delay_half_cycle()?;
                self.hal.set_sck(self.mode.cpol())?;    // active edge
                self.hal.delay_half_cycle()?;
            }
        }
        Ok(())
    }

    /// Receive one byte MSB-first (slave drives SIO, master samples).
    fn read_byte(&mut self) -> Result<u8, H::Error> {
        let mut byte = 0u8;
        self.hal.set_sio_direction(SioDirection::Input)?;

        for bit in (0..8u8).rev() {
            let sample = if !self.mode.cpha() {
                self.hal.delay_half_cycle()?;
                self.hal.set_sck(!self.mode.cpol())?;   // active edge
                let s = self.hal.get_sio()?;
                self.hal.delay_half_cycle()?;
                self.hal.set_sck(self.mode.cpol())?;
                s
            } else {
                self.hal.set_sck(!self.mode.cpol())?;
                self.hal.delay_half_cycle()?;
                self.hal.set_sck(self.mode.cpol())?;    // active edge
                let s = self.hal.get_sio()?;
                self.hal.delay_half_cycle()?;
                s
            };

            if sample {
                byte |= 1 << bit;
            }
        }
        Ok(byte)
    }

    // ----------------------------------------------------------------
    // Public API
    // ----------------------------------------------------------------

    /// Write a buffer (master drives SIO throughout).
    pub fn write(&mut self, buf: &[u8]) -> Result<(), H::Error> {
        self.hal.set_cs(false)?;

        for &byte in buf {
            self.write_byte(byte)?;
        }

        self.hal.set_cs(true)?;
        self.hal.set_sio_direction(SioDirection::Output)?;
        self.hal.set_sio(true)?;
        Ok(())
    }

    /// Read a buffer (slave drives SIO after turnaround).
    pub fn read(&mut self, buf: &mut [u8]) -> Result<(), H::Error> {
        self.hal.set_cs(false)?;
        self.turnaround()?;

        for byte in buf.iter_mut() {
            *byte = self.read_byte()?;
        }

        self.hal.set_cs(true)?;
        self.hal.set_sio_direction(SioDirection::Output)?;
        self.hal.set_sio(true)?;
        Ok(())
    }

    /// Write command/address bytes, then read response bytes.
    /// This is the canonical three-wire SPI pattern.
    pub fn write_then_read(
        &mut self,
        tx_buf: &[u8],
        rx_buf: &mut [u8],
    ) -> Result<(), H::Error> {
        self.hal.set_cs(false)?;

        // Phase 1: transmit command / address
        for &byte in tx_buf {
            self.write_byte(byte)?;
        }

        // Phase 2: direction switch (master releases, slave takes over)
        self.turnaround()?;

        // Phase 3: receive response from slave
        for byte in rx_buf.iter_mut() {
            *byte = self.read_byte()?;
        }

        self.hal.set_cs(true)?;
        self.hal.set_sio_direction(SioDirection::Output)?;
        self.hal.set_sio(true)?;
        Ok(())
    }
}
```

### 2. Platform HAL Implementation (Simulated)

```rust
//! simulated_hal.rs
//!
//! A simulated HAL implementation for testing and development.
//! Replace with real GPIO drivers for embedded targets.

use crate::{SioDirection, ThreeWireHal};

/// Simulated GPIO pin state tracked in memory.
pub struct SimulatedHal {
    pub sck:     bool,
    pub cs:      bool,
    pub sio_out: bool,
    pub sio_in:  bool,   // value that "slave" would return
    pub sio_dir: SioDirection,
    pub tx_log:  Vec<u8>,
    pub rx_src:  Vec<u8>,
    rx_index:    usize,
}

impl SimulatedHal {
    pub fn new(slave_response: Vec<u8>) -> Self {
        Self {
            sck:     false,
            cs:      true,
            sio_out: true,
            sio_in:  true,
            sio_dir: SioDirection::Output,
            tx_log:  Vec::new(),
            rx_src:  slave_response,
            rx_index: 0,
        }
    }
}

impl ThreeWireHal for SimulatedHal {
    type Error = core::convert::Infallible;

    fn set_sck(&mut self, level: bool) -> Result<(), Self::Error> {
        self.sck = level;
        Ok(())
    }

    fn set_cs(&mut self, level: bool) -> Result<(), Self::Error> {
        self.cs = level;
        Ok(())
    }

    fn set_sio(&mut self, level: bool) -> Result<(), Self::Error> {
        self.sio_out = level;
        Ok(())
    }

    fn get_sio(&mut self) -> Result<bool, Self::Error> {
        // Return bits from simulated slave response byte stream
        // (simplified: just returns true/false based on rx_src stream)
        Ok(self.sio_in)
    }

    fn set_sio_direction(&mut self, dir: SioDirection) -> Result<(), Self::Error> {
        self.sio_dir = dir;
        Ok(())
    }

    fn delay_half_cycle(&mut self) -> Result<(), Self::Error> {
        // No real delay in simulation
        Ok(())
    }
}
```

### 3. Sensor Driver Example (Rust)

```rust
//! sensor_driver.rs
//!
//! Example three-wire SPI sensor driver using ThreeWireSpi.

use crate::{SpiMode, ThreeWireHal, ThreeWireSpi};

/// Register map for an imaginary IMU sensor.
#[repr(u8)]
pub enum Register {
    WhoAmI   = 0x0F,
    TempMsb  = 0x26,
    TempLsb  = 0x27,
    AccelXH  = 0x28,
    AccelXL  = 0x29,
    CtrlReg1 = 0x20,
}

const READ_FLAG: u8 = 0x80;

pub struct ImuSensor<H: ThreeWireHal> {
    spi: ThreeWireSpi<H>,
}

impl<H: ThreeWireHal> ImuSensor<H> {
    pub fn new(hal: H) -> Result<Self, H::Error> {
        let spi = ThreeWireSpi::new(hal, SpiMode::Mode0, /*turnaround_clocks=*/1)?;
        Ok(Self { spi })
    }

    /// Read a single register.
    pub fn read_register(&mut self, reg: Register) -> Result<u8, H::Error> {
        let cmd = [reg as u8 | READ_FLAG];
        let mut val = [0u8; 1];
        self.spi.write_then_read(&cmd, &mut val)?;
        Ok(val[0])
    }

    /// Write a single register.
    pub fn write_register(&mut self, reg: Register, value: u8) -> Result<(), H::Error> {
        let buf = [reg as u8 & !READ_FLAG, value];  // clear read flag for write
        self.spi.write(&buf)
    }

    /// Read WHO_AM_I and verify the sensor is reachable.
    pub fn verify_identity(&mut self, expected_id: u8) -> Result<bool, H::Error> {
        let id = self.read_register(Register::WhoAmI)?;
        Ok(id == expected_id)
    }

    /// Read raw temperature as a 16-bit signed integer.
    pub fn read_temperature_raw(&mut self) -> Result<i16, H::Error> {
        // Burst read: send first register address, slave auto-increments
        let cmd = [Register::TempMsb as u8 | READ_FLAG];
        let mut data = [0u8; 2];
        self.spi.write_then_read(&cmd, &mut data)?;
        Ok(i16::from_be_bytes(data))
    }

    /// Read temperature in degrees Celsius.
    pub fn read_temperature_celsius(&mut self) -> Result<f32, H::Error> {
        let raw = self.read_temperature_raw()?;
        Ok(raw as f32 / 16.0)
    }

    /// Read X-axis accelerometer data.
    pub fn read_accel_x(&mut self) -> Result<i16, H::Error> {
        let cmd = [Register::AccelXH as u8 | READ_FLAG];
        let mut data = [0u8; 2];
        self.spi.write_then_read(&cmd, &mut data)?;
        Ok(i16::from_be_bytes(data))
    }
}

// ---- Unit test ----
#[cfg(test)]
mod tests {
    use super::*;
    use crate::simulated_hal::SimulatedHal;

    #[test]
    fn test_read_who_am_i() {
        // Simulate slave returning 0x6A (e.g. LSM6DS3 WHO_AM_I)
        let hal = SimulatedHal::new(vec![0x6A]);
        let mut sensor = ImuSensor::new(hal).unwrap();
        assert!(sensor.verify_identity(0x6A).unwrap());
    }

    #[test]
    fn test_temperature_conversion() {
        // Raw value 0x0010 = 16 → 16 / 16.0 = 1.0 °C
        let hal = SimulatedHal::new(vec![0x00, 0x10]);
        let mut sensor = ImuSensor::new(hal).unwrap();
        let temp = sensor.read_temperature_celsius().unwrap();
        assert!((temp - 1.0f32).abs() < f32::EPSILON);
    }
}
```

---

## Linux Kernel / spidev Interface

On Linux (e.g., Raspberry Pi, BeagleBone), three-wire SPI is exposed through the `spidev` interface with the `SPI_3WIRE` flag.

```c
/**
 * linux_3wire_spi.c
 * Three-wire SPI via Linux spidev kernel driver.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define SPI_DEVICE    "/dev/spidev0.0"
#define SPI_SPEED_HZ  1000000   /* 1 MHz */

static int spi_fd = -1;

int spi3w_init(void)
{
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) { perror("open"); return -1; }

    /* Enable three-wire (half-duplex) mode */
    uint32_t mode = SPI_MODE_0 | SPI_3WIRE;
    if (ioctl(spi_fd, SPI_IOC_WR_MODE32, &mode) < 0) {
        perror("SPI_IOC_WR_MODE32"); return -1;
    }

    uint8_t bits = 8;
    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        perror("SPI_IOC_WR_BITS_PER_WORD"); return -1;
    }

    uint32_t speed = SPI_SPEED_HZ;
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI_IOC_WR_MAX_SPEED_HZ"); return -1;
    }

    return 0;
}

/**
 * Three-wire SPI transfer: write tx_buf, then read into rx_buf.
 *
 * In three-wire mode (SPI_3WIRE), the kernel driver handles direction
 * switching. A transfer with only tx_buf set is a write; with only
 * rx_buf set (tx_buf = NULL) is a read.
 * Two separate ioctls are used: one write, one read.
 */
int spi3w_write_then_read(
    const uint8_t *tx_buf, uint32_t tx_len,
    uint8_t       *rx_buf, uint32_t rx_len)
{
    struct spi_ioc_transfer xfer[2];
    memset(xfer, 0, sizeof(xfer));

    /* Transfer 0: write phase */
    xfer[0].tx_buf        = (unsigned long)tx_buf;
    xfer[0].rx_buf        = 0;          /* no receive on this phase */
    xfer[0].len           = tx_len;
    xfer[0].speed_hz      = SPI_SPEED_HZ;
    xfer[0].bits_per_word = 8;
    xfer[0].cs_change     = 0;         /* keep CS asserted between transfers */

    /* Transfer 1: read phase */
    xfer[1].tx_buf        = 0;          /* no transmit on this phase */
    xfer[1].rx_buf        = (unsigned long)rx_buf;
    xfer[1].len           = rx_len;
    xfer[1].speed_hz      = SPI_SPEED_HZ;
    xfer[1].bits_per_word = 8;
    xfer[1].cs_change     = 0;

    /*
     * SPI_IOC_MESSAGE(2) sends both transfers atomically.
     * The kernel driver handles SIO direction switching internally
     * because SPI_3WIRE was set in the mode flags.
     */
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(2), xfer) < 0) {
        perror("SPI_IOC_MESSAGE"); return -1;
    }
    return 0;
}

int main(void)
{
    if (spi3w_init() != 0) return 1;

    uint8_t cmd  = 0x8F;   /* Read WHO_AM_I (example) */
    uint8_t data = 0;

    if (spi3w_write_then_read(&cmd, 1, &data, 1) == 0) {
        printf("WHO_AM_I = 0x%02X\n", data);
    }

    close(spi_fd);
    return 0;
}
```

---

## Pitfalls and Gotchas

### 1. Bus Contention During Direction Switch
The most common and destructive error. Always ensure the master tristates **before** the slave begins driving. Add turnaround clocks when in doubt. Never assume the slave instantly drives — check its datasheet for turnaround timing requirements.

### 2. Forgetting to Re-Assert Output Direction
After a read transaction, the master's SIO GPIO is still configured as input. If the next operation is a write and you forget to switch back to output, you will write garbage (the GPIO input state, not your data).

### 3. Pull-Up Resistor Absent or Wrong Value
Without a pull-up, the SIO line floats during the turnaround period, potentially causing spurious clock edges to sample noise. Always add a pull-up; size it for the clock speed.

### 4. Hardware SPI BIDIOE Cleared Too Early (STM32)
On STM32, clearing `BIDIOE` before the last transmitted bit completes will corrupt the final bit. Always poll `BSY` and `TXE` flags to completion before switching direction.

### 5. Multi-Slave Contention
In a multi-slave three-wire configuration, only one slave's CS should be asserted at a time. If two slaves share SIO and both are selected, both may attempt to drive — even if one is being written to and the other is idle, its output may not be fully tristated.

### 6. Logic Analyser Capture Issues
Standard logic analysers decoding SPI expect separate MOSI/MISO channels. For three-wire SPI, configure the analyser to treat the single SIO line as MOSI during the write phase and MISO during the read phase, often requiring manual annotation.

---

## Summary

Three-wire SPI is a half-duplex variant of the standard four-wire SPI bus that replaces the dedicated MOSI and MISO lines with a single bidirectional SIO line, reducing the wire count from four to three. This trade-off sacrifices simultaneous transmit/receive capability in exchange for a simpler physical interface.

**Key technical points:**

- **Direction is time-multiplexed:** the master drives SIO during command/address phases and tristates it during data read phases, when the slave takes control.
- **Turnaround clocks** are critical — they provide a safe transition window during which neither master nor slave drives SIO, preventing destructive bus contention.
- **Pull-up resistors** on SIO are essential for maintaining a defined state during the high-impedance turnaround period.
- **Hardware support** (e.g., STM32's `BIDIMODE`) simplifies implementation but requires careful register sequencing around the direction switch, particularly ensuring the TX FIFO and shift register are empty before switching.
- **Software (bit-bang) implementations** give precise control over the direction switch but must account for GPIO reconfiguration latency.
- **Linux spidev** exposes three-wire mode cleanly via the `SPI_3WIRE` flag, with the kernel managing direction internally across two-transfer `ioctl` messages.

Three-wire SPI is well-suited for space-constrained designs, wearable electronics, and any scenario where saving one data pin per peripheral is valuable — particularly common in IMUs, display controllers, and low-pin-count sensor modules.

---

*Document: `51_Three_Wire_SPI_Mode.md` | Topic: Embedded Protocols Series*