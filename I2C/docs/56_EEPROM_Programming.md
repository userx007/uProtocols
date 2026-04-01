# 56. EEPROM Programming over I²C

**Fundamentals & Addressing** — physical interface, key timing parameters, memory organisation by capacity, and the 7-bit address format with the `1010` + A2/A1/A0 scheme.

**Page Writes** — the critical page-boundary wrap-around rule is explained in detail, with the arithmetic for calculating correctly split chunks across boundaries.

**Write Cycle Timing** — both strategies are covered: the simple fixed-delay approach and the more efficient ACK-polling technique (which typically saves 20–40% of wait time).

**C/C++ Implementation** — a complete Linux `i2c-dev` driver including: device open/close, ACK polling with timeout, single-byte write, page write, multi-page write with automatic boundary splitting, and combined-message sequential read via `I2C_RDWR ioctl`.

**Rust Implementation** — uses `embedded-hal 1.0` traits for portability, with the same full feature set plus a `no_std` variant using HAL delay primitives for bare-metal targets.

**Practical Pitfalls** — write protect pin management, power-on delay, pull-up resistor selection by bus speed, endurance/wear-levelling strategies, and write amplification from unnecessary byte writes.

**Summary Table** — a concise reference card covering every key concept.


> **Topic:** Page writes, polling techniques, and write cycle timing for I²C EEPROMs

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [I²C EEPROM Fundamentals](#2-i2c-eeprom-fundamentals)
3. [Device Addressing](#3-device-addressing)
4. [Byte Write Operations](#4-byte-write-operations)
5. [Page Write Operations](#5-page-write-operations)
6. [Write Cycle Timing and tWR](#6-write-cycle-timing-and-twr)
7. [Acknowledge Polling](#7-acknowledge-polling)
8. [Sequential / Random Read Operations](#8-sequential--random-read-operations)
9. [C / C++ Implementation](#9-c--c-implementation)
10. [Rust Implementation](#10-rust-implementation)
11. [Practical Considerations and Pitfalls](#11-practical-considerations-and-pitfalls)
12. [Summary](#12-summary)

---

## 1. Introduction

Electrically Erasable Programmable Read-Only Memory (EEPROM) ICs are among the most common non-volatile storage devices used in embedded systems. Examples include the ubiquitous **AT24Cxx** series (Atmel/Microchip), **M24xxx** series (STMicroelectronics), and **CAT24Cxx** (ON Semiconductor). They are accessed via the I²C (Inter-Integrated Circuit) two-wire bus, making them straightforward to integrate into virtually any microcontroller platform.

Unlike Flash memory, EEPROMs support **byte-level erase-before-write** internally, so from the programmer's perspective every write is a single operation. However, several subtle but critical characteristics—**page write limits**, **internal write cycle timing**, and **bus arbitration during programming**—must be understood to write reliable firmware.

---

## 2. I²C EEPROM Fundamentals

### 2.1 Physical Interface

| Signal | Direction | Description |
|--------|-----------|-------------|
| SCL    | Master → Slave | Serial Clock (100 kHz / 400 kHz / 1 MHz) |
| SDA    | Bidirectional  | Serial Data |
| A0–A2  | Hardware pins  | Device address selection |
| WP     | Hardware pin   | Write Protect (active HIGH) |

Both SCL and SDA require **pull-up resistors** to VCC (typically 4.7 kΩ at 100 kHz, 2.2 kΩ at 400 kHz).

### 2.2 Key EEPROM Parameters

| Parameter | Typical Value | Notes |
|-----------|--------------|-------|
| Write cycle time (tWR) | 5–10 ms | Varies by vendor/grade |
| Page size | 8–256 bytes | Depends on total capacity |
| Endurance | 1,000,000 cycles | Per byte location |
| Data retention | 100 years | At 25 °C |
| Max I²C speed | 400 kHz (FM) or 1 MHz (FM+) | Device dependent |

### 2.3 Memory Organisation

EEPROM capacity directly determines the **word address width**:

| Device  | Capacity | Address Width | Page Size |
|---------|----------|--------------|-----------|
| AT24C01 | 128 B    | 8-bit (1 byte)  | 8 B  |
| AT24C02 | 256 B    | 8-bit (1 byte)  | 8 B  |
| AT24C08 | 1 KB     | 10-bit (2 bytes) | 16 B |
| AT24C32 | 4 KB     | 12-bit (2 bytes) | 32 B |
| AT24C256| 32 KB    | 15-bit (2 bytes) | 64 B |
| AT24C512| 64 KB    | 16-bit (2 bytes) | 128 B |

---

## 3. Device Addressing

The 7-bit I²C address for AT24Cxx EEPROMs follows this format:

```
 Bit:  7    6    5    4    3    2    1    0
      [ 1 ][ 0 ][ 1 ][ 0 ][ A2][ A1][ A0][ R/W ]
```

- Bits [7:4] = `1010` — fixed device identifier
- Bits [3:1] = `A2 A1 A0` — hardware address pins
- Bit [0]    = `R/W` — `0` for write, `1` for read

**Example:** All address pins tied LOW, write operation:

```
7-bit address = 0b1010_000  (0x50)
Write frame   = 0xA0        (address shifted left + R/W=0)
Read frame    = 0xA1        (address shifted left + R/W=1)
```

> **Note for large EEPROMs (AT24C04/08/16):** The upper address bits overflow
> into the device address field because these devices use only one address byte.
> A2/A1 (or just A2 for AT24C16) become "page select" bits within the device
> address byte itself.

---

## 4. Byte Write Operations

A single-byte write transfers **one data byte** to one memory address. The
sequence is:

```
START → [Device Addr | W] → ACK → [Addr High] → ACK → [Addr Low] → ACK → [Data] → ACK → STOP
```

For devices with 8-bit addressing (≤ 256 B):

```
START → [0xA0] → ACK → [Word Addr] → ACK → [Data] → ACK → STOP
```

After the STOP condition the EEPROM begins its **internal write cycle** and will
NAK all I²C requests until it completes (typically 5–10 ms).

---

## 5. Page Write Operations

Page writes are the most important performance optimisation available. Instead of
issuing a STOP after each data byte, the master sends multiple bytes within a
single transaction. The EEPROM buffers them in its internal page latch and
programs the entire page in **one write cycle**.

```
START → [Device Addr | W] → ACK
      → [Addr High] → ACK → [Addr Low] → ACK
      → [Data0] → ACK → [Data1] → ACK → ... → [DataN] → ACK
      → STOP
```

### 5.1 Page Boundary Constraint — Critical Rule

The EEPROM page buffer wraps at the **page boundary**. If a write crosses a page
boundary, the address counter rolls over to the **beginning of the same page**,
silently overwriting earlier data.

```
Page size = 64 bytes, page boundary at 0x40, 0x80, 0xC0, ...

Write starting at 0x3E, 4 bytes:
  → 0x3E = Data[0]   ✓
  → 0x3F = Data[1]   ✓   ← last byte of page
  → 0x40 = would be Data[2], but wraps to 0x00 → Data[2] overwrites 0x00 ✗
  → 0x01 = Data[3]   ✗

Correct approach: split into two writes:
  Write 1: 0x3E, 2 bytes  (fills page)
  Write 2: 0x40, 2 bytes  (new page)
```

### 5.2 Calculating Page-Aligned Writes

For a write of `len` bytes starting at address `addr`:

```
bytes_in_first_page = page_size - (addr % page_size)
first_chunk         = min(len, bytes_in_first_page)
remaining           = len - first_chunk
full_pages          = remaining / page_size
last_chunk          = remaining % page_size
```

---

## 6. Write Cycle Timing and tWR

After a STOP condition the device enters its **internal programming cycle**. During this period:

- All I²C bus activity is **internally ignored**
- The device issues a **NAK** (NACK) to any START + address probe
- Typical duration: **5 ms** (fast-grade), **10 ms** (standard)

### 6.1 Fixed Delay Strategy

The simplest approach: wait the worst-case tWR after every page write.

```
Write page → STOP → delay(10 ms) → next Write
```

**Pros:** Simple, no additional bus traffic  
**Cons:** Always waits the full worst case, even if the device finishes in 4 ms

### 6.2 Acknowledge Polling Strategy

A smarter approach: repeatedly probe the device address after the STOP until it
responds with an **ACK**, signalling that the write cycle has completed.

```
Write page → STOP → poll until ACK → next Write
```

This can reduce average wait time by **20–40%** compared to fixed delays, especially at low temperatures where write cycles are shorter.

---

## 7. Acknowledge Polling

Acknowledge polling exploits the EEPROM's behaviour of NAKing during the write cycle.

### 7.1 Polling Sequence

```
loop:
  START
  Send [Device Addr | W]
  if ACK received  → STOP, proceed with next operation
  if NAK received  → STOP (or repeated START), try again
  [optional timeout guard]
```

### 7.2 Timing Diagram

```
SDA: ──┐ ┌─────────────────────────────────────────────┐ ┌─────
       │ │ STOP                                   START│ │ START
       └─┘ ─────────────── tWR elapsed ──────────────── └─┘

Master probes:  0xA0 NAK  0xA0 NAK  0xA0 NAK  0xA0 ACK ← done
                  │          │          │          │
Time →         t0+1ms    t0+2ms    t0+3ms    t0+4.5ms
```

### 7.3 Polling with Timeout

Always guard polling loops against EEPROM failure or bus lockup:

```
max_polls = 200          // at 100 kHz, each probe ≈ 90 µs → ~18 ms total
poll_count = 0
loop:
  probe device address
  if ACK → break
  poll_count++
  if poll_count > max_polls → return ERROR_TIMEOUT
```

---

## 8. Sequential / Random Read Operations

Reads do **not** require a write cycle. They return data immediately.

### 8.1 Random Read (Single Byte)

```
START → [0xA0] → ACK → [Addr High] → ACK → [Addr Low] → ACK
START (repeated) → [0xA1] → ACK → [Data] → NAK → STOP
```

The dummy write sets the internal address pointer, then a repeated START
switches to read mode.

### 8.2 Sequential Read (Multiple Bytes)

```
START → [0xA0] → ACK → [Addr High] → ACK → [Addr Low] → ACK
START → [0xA1] → ACK
      → [Data0] → ACK → [Data1] → ACK → ... → [DataN] → NAK → STOP
```

The master sends **ACK** after each byte to request the next one, and **NAK**
before the STOP to terminate the sequence. The address counter auto-increments
and **wraps at the end of the device** (not at page boundaries), so sequential
reads can span pages freely.

---

## 9. C / C++ Implementation

The following implementation targets a Linux I²C userspace driver (`/dev/i2c-N`)
but the logic applies equally to bare-metal HALs.

### 9.1 Header and Types

```c
/* eeprom.h */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define EEPROM_I2C_BASE_ADDR   0x50   /* A2=A1=A0=0 */
#define EEPROM_PAGE_SIZE       64     /* AT24C256: 64-byte pages */
#define EEPROM_TOTAL_SIZE      32768  /* 32 KB */
#define EEPROM_WRITE_CYCLE_MS  10     /* worst-case tWR */
#define EEPROM_POLL_MAX        300    /* acknowledge poll limit */

typedef struct {
    int      fd;           /* I2C file descriptor or HAL handle */
    uint8_t  dev_addr;     /* 7-bit I2C address */
    uint16_t page_size;
    uint32_t capacity;
} eeprom_t;

typedef enum {
    EEPROM_OK            =  0,
    EEPROM_ERR_BUS       = -1,
    EEPROM_ERR_TIMEOUT   = -2,
    EEPROM_ERR_BOUNDS    = -3,
} eeprom_err_t;
```

### 9.2 Low-Level I²C Helpers (Linux i2c-dev)

```c
/* eeprom_ll.c  —  Linux /dev/i2c-N backend */
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <errno.h>
#include <time.h>
#include "eeprom.h"

/* Open the I2C bus and initialise the EEPROM handle. */
eeprom_err_t eeprom_open(eeprom_t *dev, const char *bus,
                         uint8_t hw_addr_bits)
{
    dev->dev_addr  = EEPROM_I2C_BASE_ADDR | (hw_addr_bits & 0x07);
    dev->page_size = EEPROM_PAGE_SIZE;
    dev->capacity  = EEPROM_TOTAL_SIZE;

    dev->fd = open(bus, O_RDWR);
    if (dev->fd < 0) return EEPROM_ERR_BUS;

    if (ioctl(dev->fd, I2C_SLAVE, dev->dev_addr) < 0) {
        close(dev->fd);
        return EEPROM_ERR_BUS;
    }
    return EEPROM_OK;
}

void eeprom_close(eeprom_t *dev) { close(dev->fd); }

/* Millisecond sleep helper. */
static void msleep(unsigned int ms) {
    struct timespec ts = { .tv_sec = ms / 1000,
                           .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
```

### 9.3 Acknowledge Polling

```c
/*
 * eeprom_poll_ack — probe the device until it ACKs or we time out.
 *
 * Returns EEPROM_OK when the device is ready, EEPROM_ERR_TIMEOUT otherwise.
 */
eeprom_err_t eeprom_poll_ack(eeprom_t *dev)
{
    uint8_t dummy = 0;

    for (int i = 0; i < EEPROM_POLL_MAX; i++) {
        /*
         * A zero-length write is the standard ACK-poll:
         * the kernel i2c-dev layer issues START + device addr + STOP.
         * If the device ACKs (write() returns 0), it is ready.
         */
        if (write(dev->fd, &dummy, 0) == 0)
            return EEPROM_OK;

        /* Small delay avoids hammering the bus: ~100 µs between probes. */
        struct timespec t = { 0, 100000L };
        nanosleep(&t, NULL);
    }
    return EEPROM_ERR_TIMEOUT;
}
```

### 9.4 Single-Byte Write

```c
/*
 * eeprom_write_byte — write a single byte and wait for the write cycle.
 */
eeprom_err_t eeprom_write_byte(eeprom_t *dev, uint16_t addr, uint8_t data)
{
    if (addr >= dev->capacity) return EEPROM_ERR_BOUNDS;

    uint8_t buf[3] = {
        (uint8_t)(addr >> 8),   /* high address byte */
        (uint8_t)(addr & 0xFF), /* low  address byte */
        data
    };

    if (write(dev->fd, buf, sizeof(buf)) != sizeof(buf))
        return EEPROM_ERR_BUS;

    return eeprom_poll_ack(dev);   /* wait for internal write cycle */
}
```

### 9.5 Page Write (Core Routine)

```c
/*
 * eeprom_write_page — write up to one page of data.
 *
 * The caller guarantees that (addr + len) does not cross a page boundary.
 * len must be <= dev->page_size.
 */
static eeprom_err_t eeprom_write_page(eeprom_t *dev,
                                      uint16_t addr,
                                      const uint8_t *data,
                                      size_t len)
{
    /* Build: [addr_hi, addr_lo, data0, data1, ..., dataN] */
    uint8_t buf[2 + EEPROM_PAGE_SIZE];
    buf[0] = (uint8_t)(addr >> 8);
    buf[1] = (uint8_t)(addr & 0xFF);
    for (size_t i = 0; i < len; i++) buf[2 + i] = data[i];

    if (write(dev->fd, buf, 2 + len) != (ssize_t)(2 + len))
        return EEPROM_ERR_BUS;

    return eeprom_poll_ack(dev);   /* ACK polling after STOP */
}
```

### 9.6 Multi-Page Write with Boundary Splitting

```c
/*
 * eeprom_write — write an arbitrary buffer, handling page boundaries
 *               and write cycle waits automatically.
 */
eeprom_err_t eeprom_write(eeprom_t *dev, uint16_t addr,
                           const uint8_t *data, size_t len)
{
    if (addr + len > dev->capacity) return EEPROM_ERR_BOUNDS;

    size_t written = 0;

    while (written < len) {
        uint16_t cur_addr   = addr + written;
        size_t   page_off   = cur_addr % dev->page_size;
        size_t   space_left = dev->page_size - page_off;  /* bytes to page end */
        size_t   chunk      = (len - written < space_left)
                            ?  len - written
                            :  space_left;                /* min of remaining, space */

        eeprom_err_t err = eeprom_write_page(dev, cur_addr,
                                             data + written, chunk);
        if (err != EEPROM_OK) return err;

        written += chunk;
    }
    return EEPROM_OK;
}
```

### 9.7 Sequential Read

```c
/*
 * eeprom_read — read len bytes from addr into buf.
 *
 * Uses a combined I2C message: dummy write to set address, then read.
 * Reads do not require polling or delays.
 */
eeprom_err_t eeprom_read(eeprom_t *dev, uint16_t addr,
                          uint8_t *buf, size_t len)
{
    if (addr + len > dev->capacity) return EEPROM_ERR_BOUNDS;

    uint8_t addr_buf[2] = { (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF) };

    struct i2c_msg msgs[2] = {
        /* Message 0: write the memory address (no STOP between) */
        {
            .addr  = dev->dev_addr,
            .flags = 0,
            .len   = 2,
            .buf   = addr_buf,
        },
        /* Message 1: read data (Repeated START, then read) */
        {
            .addr  = dev->dev_addr,
            .flags = I2C_M_RD,
            .len   = (uint16_t)len,
            .buf   = buf,
        },
    };

    struct i2c_rdwr_ioctl_data xfer = { .msgs = msgs, .nmsgs = 2 };

    if (ioctl(dev->fd, I2C_RDWR, &xfer) < 0)
        return EEPROM_ERR_BUS;

    return EEPROM_OK;
}
```

### 9.8 Usage Example

```c
#include <stdio.h>
#include "eeprom.h"

int main(void)
{
    eeprom_t eeprom;

    if (eeprom_open(&eeprom, "/dev/i2c-1", 0 /* A2=A1=A0=0 */) != EEPROM_OK) {
        fprintf(stderr, "Failed to open EEPROM\n");
        return 1;
    }

    /* Write a string spanning two pages to demonstrate boundary handling */
    const uint8_t payload[] = "Hello, EEPROM! Page-write demo with boundary split.";
    uint16_t start_addr = 0x3C;   /* near a 64-byte page boundary (0x40) */

    eeprom_err_t err = eeprom_write(&eeprom, start_addr,
                                    payload, sizeof(payload));
    if (err != EEPROM_OK) {
        fprintf(stderr, "Write failed: %d\n", err);
        eeprom_close(&eeprom);
        return 1;
    }

    /* Read back and verify */
    uint8_t readback[sizeof(payload)];
    err = eeprom_read(&eeprom, start_addr, readback, sizeof(readback));
    if (err != EEPROM_OK) {
        fprintf(stderr, "Read failed: %d\n", err);
    } else {
        printf("Read back: %s\n", readback);
    }

    eeprom_close(&eeprom);
    return 0;
}
```

---

## 10. Rust Implementation

The Rust implementation uses the [`linux-embedded-hal`](https://crates.io/crates/linux-embedded-hal)
crate which implements the [`embedded-hal`](https://crates.io/crates/embedded-hal) I²C traits,
making the driver portable to any embedded target.

### 10.1 Cargo.toml

```toml
[package]
name    = "eeprom-i2c"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal     = "1.0"
linux-embedded-hal = "0.4"
thiserror        = "1.0"
```

### 10.2 Error Types

```rust
// src/error.rs
use thiserror::Error;

#[derive(Debug, Error)]
pub enum EepromError {
    #[error("I2C bus error: {0}")]
    Bus(String),

    #[error("ACK poll timed out after write cycle")]
    Timeout,

    #[error("Address 0x{addr:04X} + length {len} exceeds capacity {capacity}")]
    OutOfBounds { addr: u16, len: usize, capacity: usize },
}
```

### 10.3 EEPROM Driver Struct

```rust
// src/eeprom.rs
use embedded_hal::i2c::I2c;
use std::thread::sleep;
use std::time::Duration;
use crate::error::EepromError;

pub const PAGE_SIZE: usize  = 64;    // AT24C256 page size
pub const CAPACITY:  usize  = 32768; // 32 KB
pub const BASE_ADDR: u8     = 0x50;
pub const POLL_MAX:  u32    = 300;
pub const POLL_DELAY_US: u64 = 100;

pub struct Eeprom<I2C> {
    i2c:       I2C,
    dev_addr:  u8,
    page_size: usize,
    capacity:  usize,
}

impl<I2C: I2c> Eeprom<I2C> {
    /// Construct a new EEPROM driver.
    ///
    /// `hw_addr` is the state of the A2/A1/A0 pins (0..=7).
    pub fn new(i2c: I2C, hw_addr: u8) -> Self {
        Self {
            i2c,
            dev_addr:  BASE_ADDR | (hw_addr & 0x07),
            page_size: PAGE_SIZE,
            capacity:  CAPACITY,
        }
    }

    /// Release the underlying I2C bus.
    pub fn release(self) -> I2C { self.i2c }
```

### 10.4 Acknowledge Polling in Rust

```rust
    /// Poll the device with zero-length writes until it ACKs,
    /// indicating the internal write cycle has completed.
    fn poll_ack(&mut self) -> Result<(), EepromError> {
        for _ in 0..POLL_MAX {
            // A zero-byte write: START → addr → (expect ACK) → STOP
            if self.i2c.write(self.dev_addr, &[]).is_ok() {
                return Ok(());
            }
            sleep(Duration::from_micros(POLL_DELAY_US));
        }
        Err(EepromError::Timeout)
    }
```

### 10.5 Page Write

```rust
    /// Write a single page-aligned chunk (len ≤ page_size, no boundary crossing).
    fn write_page(&mut self, addr: u16, data: &[u8]) -> Result<(), EepromError> {
        debug_assert!(data.len() <= self.page_size, "chunk exceeds page size");

        // Construct: [addr_high, addr_low, data...]
        let mut buf = Vec::with_capacity(2 + data.len());
        buf.push((addr >> 8) as u8);
        buf.push((addr & 0xFF) as u8);
        buf.extend_from_slice(data);

        self.i2c
            .write(self.dev_addr, &buf)
            .map_err(|e| EepromError::Bus(format!("{e:?}")))?;

        self.poll_ack()
    }
```

### 10.6 Public Write API with Boundary Splitting

```rust
    /// Write `data` to EEPROM starting at `addr`.
    ///
    /// Automatically splits writes at page boundaries and waits
    /// for each write cycle via acknowledge polling.
    pub fn write(&mut self, addr: u16, data: &[u8]) -> Result<(), EepromError> {
        let end = addr as usize + data.len();
        if end > self.capacity {
            return Err(EepromError::OutOfBounds {
                addr,
                len: data.len(),
                capacity: self.capacity,
            });
        }

        let mut written = 0usize;

        while written < data.len() {
            let cur_addr  = addr as usize + written;
            let page_off  = cur_addr % self.page_size;
            let space     = self.page_size - page_off;          // bytes to page end
            let chunk_len = (data.len() - written).min(space);  // don't exceed space

            self.write_page(
                cur_addr as u16,
                &data[written..written + chunk_len],
            )?;

            written += chunk_len;
        }

        Ok(())
    }
```

### 10.7 Sequential Read

```rust
    /// Read `buf.len()` bytes from `addr`.
    ///
    /// Uses a write-then-read combined transfer (Repeated START).
    /// No write cycle wait is needed for reads.
    pub fn read(&mut self, addr: u16, buf: &mut [u8]) -> Result<(), EepromError> {
        if addr as usize + buf.len() > self.capacity {
            return Err(EepromError::OutOfBounds {
                addr,
                len: buf.len(),
                capacity: self.capacity,
            });
        }

        let addr_bytes = [(addr >> 8) as u8, (addr & 0xFF) as u8];

        // write_read issues: START → write addr_bytes → Repeated START → read into buf → STOP
        self.i2c
            .write_read(self.dev_addr, &addr_bytes, buf)
            .map_err(|e| EepromError::Bus(format!("{e:?}")))
    }
} // impl Eeprom
```

### 10.8 Rust Usage Example

```rust
// src/main.rs
mod eeprom;
mod error;

use linux_embedded_hal::I2cdev;
use eeprom::Eeprom;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open Linux I2C bus
    let i2c = I2cdev::new("/dev/i2c-1")?;
    let mut eeprom = Eeprom::new(i2c, 0 /* A2=A1=A0=0 */);

    // Write payload near a page boundary to exercise boundary splitting
    let payload = b"Rust EEPROM driver: page-write demo with ACK polling.";
    let start: u16 = 0x3D;  // 3 bytes before the 0x40 boundary

    println!("Writing {} bytes at address 0x{:04X}...", payload.len(), start);
    eeprom.write(start, payload)?;
    println!("Write complete.");

    // Read back
    let mut buf = vec![0u8; payload.len()];
    eeprom.read(start, &mut buf)?;

    println!("Read back: {}", std::str::from_utf8(&buf)?);

    assert_eq!(&buf, payload, "Data mismatch!");
    println!("Verification passed ✓");

    Ok(())
}
```

### 10.9 Fixed-Delay Fallback (no_std)

For bare-metal `no_std` environments where `std::thread::sleep` is unavailable,
replace the polling loop with a HAL delay:

```rust
use embedded_hal::delay::DelayNs;

pub struct EepromNoStd<I2C, D> {
    i2c:   I2C,
    delay: D,
    // ... same fields
}

impl<I2C: I2c, D: DelayNs> EepromNoStd<I2C, D> {
    fn wait_write_cycle(&mut self) {
        // Fixed 10 ms worst-case delay — simple but slightly wasteful
        self.delay.delay_ms(10);
    }

    fn poll_ack(&mut self) -> Result<(), EepromError> {
        for _ in 0..300u32 {
            if self.i2c.write(self.dev_addr, &[]).is_ok() {
                return Ok(());
            }
            self.delay.delay_us(100);
        }
        Err(EepromError::Timeout)
    }
}
```

---

## 11. Practical Considerations and Pitfalls

### 11.1 Write Protect Pin

Always drive `WP` LOW in firmware before any write, and HIGH afterwards if
security is required. A floating `WP` pin can cause unpredictable behaviour.

```c
gpio_write(WP_PIN, 0);          // disable write protect
eeprom_write(&dev, addr, data, len);
gpio_write(WP_PIN, 1);          // re-enable write protect
```

### 11.2 Power-On Delay

EEPROMs require a minimum time after VCC stabilises before accepting commands
(typically **1 ms**). Always add a startup delay in firmware init.

### 11.3 Bus Speed vs Pull-up Selection

| Speed | Pull-up |
|-------|---------|
| 100 kHz (Standard) | 4.7 kΩ |
| 400 kHz (Fast)     | 2.2 kΩ |
| 1 MHz  (Fast+)     | 1.0 kΩ |

Incorrect pull-ups cause marginal bus behaviour that is temperature- and
cable-length-sensitive — a common source of intermittent field failures.

### 11.4 Endurance Management

With 1,000,000 write cycles per location, **wear levelling** should be
considered for frequently updated data:

- Round-robin across multiple addresses for counters
- Use an 8-byte header with a generation counter to track the active slot
- Avoid writing the same byte if the value hasn't changed (read-before-write)

### 11.5 Write Amplification from Byte Writes

A single byte write consumes the same tWR as a full page write. Always batch
updates into page-aligned writes when modifying multiple nearby addresses.

### 11.6 Incomplete Page Fills

If you only need to update 3 bytes within a 64-byte page, you must either:
- Read the entire page, modify the 3 bytes in the buffer, write the whole page, **or**
- Write only the 3 bytes (the remaining bytes in the page latch are set from
  the EEPROM's last programmed state — no corruption occurs as long as you
  don't cross the page boundary).

---

## 12. Summary

| Topic | Key Takeaway |
|-------|-------------|
| **Device Addressing** | 7-bit address = `1010` + A2 A1 A0; large devices use address bits as page selectors |
| **Byte Write** | One byte per transaction; simple but slow — avoids page writes |
| **Page Write** | Up to N bytes per transaction; must not cross page boundaries or data wraps silently |
| **Boundary Splitting** | Compute `page_size - (addr % page_size)` for the first chunk; iterate for subsequent pages |
| **tWR (Write Cycle)** | 5–10 ms internal programming time; device NAKs all I²C traffic during this period |
| **Fixed Delay** | Simple: `delay(10 ms)` after every write — always waits worst case |
| **Acknowledge Polling** | Efficient: probe device address after STOP; exit loop on first ACK — saves 20–40 % wait time |
| **Sequential Read** | No write cycle needed; use `write_read` (Repeated START) to set address then stream bytes |
| **Endurance** | ~1,000,000 write cycles per byte; use wear levelling for frequently updated locations |
| **Rust / embedded-hal** | `I2c::write_read` for reads; `I2c::write` with address bytes prepended for writes; `no_std` compatible with HAL delay |
| **C / Linux i2c-dev** | `ioctl(I2C_RDWR)` for combined messages; zero-length `write()` for ACK polling |

I²C EEPROM programming is straightforward in principle but demands attention to
three details: **never cross a page boundary in a single write**, **always wait
for the write cycle to complete** (preferably via ACK polling), and **respect
the device's endurance budget** for long-lived products. Mastering these three
constraints yields reliable, efficient non-volatile storage on virtually any
embedded platform.

---

*Document generated for the I²C Embedded Systems Programming Series — Topic 56*