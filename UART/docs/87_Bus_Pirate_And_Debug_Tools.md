# 87. Bus Pirate and Debug Tools — UART Development

**Hardware & Setup** — Bus Pirate feature table, pinout diagrams, voltage-level considerations, and the TX/RX cross-over rule.

**Interactive Use** — Transparent bridge, live hex monitor, sending raw bytes and strings from the Bus Pirate terminal.

**C/C++ Programming** — A reusable `BusPirate` library (`bp_serial.c/h`) that opens the virtual COM port, enters binary mode, configures UART mode, and performs bulk transfers. Includes a baud-rate auto-scanner that scores printable ASCII output.

**Rust Programming** — A full `BusPirate` struct using the `serialport` crate with typed `Parity`/`StopBits` enums, proper `anyhow` error propagation, and a matching baud-rate scanner.

**Logic Analysis** — `sigrok-cli` capture commands, PulseView decoder settings, the 8× oversampling rule, and a `libsigrokdecode` C embedding example.

**UART Tap / Sniffer** — Dual-port man-in-the-middle implementations in both C++17 and Rust with timestamped hex dumps.

**Loopback Tests** — Hardware wiring and self-test code (C and Rust) that validates every byte value 0x00–0xFF.

**Automated Test Harness** — Keyword-driven stimulus/response test runners in C++17 and Rust, suitable for embedding in CI pipelines.

**Debug Patterns** — Troubleshooting tables for no output, framing errors, Bus Pirate reset tricks, AUX-pin timing markers, and oscilloscope baud measurement.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Bus Pirate Overview](#bus-pirate-overview)
3. [Hardware Setup and Wiring](#hardware-setup-and-wiring)
4. [Bus Pirate UART Mode — Interactive Use](#bus-pirate-uart-mode--interactive-use)
5. [Scripting Bus Pirate from a Host — C/C++](#scripting-bus-pirate-from-a-host--cc)
6. [Scripting Bus Pirate from a Host — Rust](#scripting-bus-pirate-from-a-host--rust)
7. [Logic Analyzer Integration (Sigrok / PulseView)](#logic-analyzer-integration-sigrok--pulseview)
8. [Decoding UART with libsigrokdecode — C](#decoding-uart-with-libsigrokdecode--c)
9. [UART Sniffer / Tap in C/C++](#uart-sniffer--tap-in-cc)
10. [UART Sniffer / Tap in Rust](#uart-sniffer--tap-in-rust)
11. [Loopback and Self-Test Utilities](#loopback-and-self-test-utilities)
12. [Automated UART Test Harness — C/C++](#automated-uart-test-harness--cc)
13. [Automated UART Test Harness — Rust](#automated-uart-test-harness--rust)
14. [Common Debug Scenarios and Patterns](#common-debug-scenarios-and-patterns)
15. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) is one of the oldest and most ubiquitous serial
communication protocols in embedded systems. Despite its simplicity — two wires, no clock — UART
bugs are surprisingly common: wrong baud rates, incorrect parity, framing errors, level mismatches,
and broken flow control all appear in real designs.

Debug tools bridge the gap between code and physics. The **Bus Pirate** is an open-source,
multi-protocol hardware tool that lets a PC interact directly with UART, SPI, I²C, 1-Wire, and
several other buses. Paired with logic analyzers, software decoders, and custom host scripts, it
provides a complete development and debug ecosystem that complements — and often replaces — expensive
proprietary analysers.

This chapter covers:

- Using Bus Pirate interactively and programmatically for UART work.
- Scripting the Bus Pirate from host applications in **C/C++** and **Rust**.
- Capturing and decoding UART traffic with open-source logic analyzer software.
- Building loopback tests, sniffers, and automated test harnesses.

---

## Bus Pirate Overview

The Bus Pirate (versions 3.x, 4, and 5) presents itself to the host PC as a **USB-CDC virtual
serial port**. Once connected you interact through a simple ASCII terminal menu, or via a
well-documented **binary bit-bang / binary protocol mode** that allows programmatic control.

### Key UART-related features

| Feature | Detail |
|---|---|
| Baud rates | 300 bps – 115,200 bps (v3/4); up to 2 Mbps on v5 |
| Voltage levels | 0–5 V (on-board pull-ups), 3.3 V I/O, VPULLUP pin |
| Data bits | 8 (standard) |
| Parity | None / Even / Odd |
| Stop bits | 1 or 2 |
| Flow control | None / RTS-CTS (hardware) |
| Auxiliary pins | AUX, CLK, MOSI, MISO — usable as GPIO or for bit-bang |
| Power supply | 3.3 V & 5 V switchable on-board supply (max ~150 mA) |

### Firmware command set (terminal mode)

```
HiZ> m           # mode selection menu
1. HiZ
2. 1-WIRE
3. UART
4. I2C
5. SPI
...
(3) >            # select UART
Set serial port speed: (1)300 (2)1200 (3)2400 (4)4800 (5)9600
                       (6)19200 (7)38400 (8)57600 (9)115200
                       (10)BRG raw value
(9) >            # 115200
Data bits and parity: (1)8, NONE *default* (2)8, EVEN (3)8, ODD (4)9, NONE
(1) >
Stop bits: (1)1 *default* (2)2
(1) >
Receive polarity: (1)Idle 1 *default* (2)Idle 0
(1) >
Select output type: (1)Open drain (H=Hi-Z, L=GND) (2)Normal (H=3.3V, L=GND)
(2) >
UART mode set
UART> (0) macro menu
UART> (1) transparent UART bridge
UART> (2) live monitor
```

---

## Hardware Setup and Wiring

### Signal levels and voltage translation

The Bus Pirate operates at **3.3 V** logic internally. When connecting to a 5 V UART target you
**must** use a level shifter (e.g., TXS0108E or simple resistor divider on the RX line). When
connecting to a 1.8 V target, use a bidirectional level translator.

```
Bus Pirate v3/4 pinout (relevant pins)
┌─────────────────────────────┐
│  MOSI  ──►  target TX       │  ← BP transmit (MOSI in UART mode)
│  MISO  ◄──  target RX       │  ← BP receive  (MISO in UART mode)
│  GND   ───  target GND      │
│  +3.3V ───  target VCC      │  (if target is 3.3V, max 150 mA)
│  VPULLUP ── external VCC    │  (sets pull-up voltage)
│  AUX   ───  optional CTS/RTS│
└─────────────────────────────┘

Minimal UART connection (no flow control):
  Host PC  ←USB→  Bus Pirate  MOSI──►TX  MISO◄──RX  GND─GND  Target Device
```

### Cross-over rule

UART is **DTE↔DCE** wired (TX→RX, RX→TX). When the Bus Pirate acts as a **terminal / DTE**, its
**MOSI** drives the **target's RX** and its **MISO** listens on the **target's TX**.

---

## Bus Pirate UART Mode — Interactive Use

### Transparent bridge (macro 1)

The fastest way to interact with a UART target — the Bus Pirate relays bytes transparently between
the USB CDC port and the target device.

```
UART> (1)
UART bridge. Space to exit.
# Everything typed now goes directly to the target device.
# All target output appears in the terminal.
```

### Live monitor (macro 2)

Receive-only hex dump — useful for capturing output from a device without transmitting:

```
UART> (2)
Raw UART input. Space to exit.
0x48 0x65 0x6C 0x6C 0x6F 0x0D 0x0A    # "Hello\r\n"
```

### Sending raw bytes / strings in UART mode

```
UART> [0x55 0xAA 0x00 0xFF]    # send four bytes
UART> "Hello, world!\r\n"      # send ASCII string
UART> {0x55 0xAA}              # alternative syntax (same as [...])
```

### Reading with timeout

```
UART> r:10                     # read 10 bytes (blocks until received or timeout)
UART> r                        # read one byte
```

---

## Scripting Bus Pirate from a Host — C/C++

The Bus Pirate exposes a **binary bit-bang mode** (send `0x00` up to 20 times) and a
**binary UART mode** (enter with `0x03` after bit-bang mode is active). This allows full
programmatic control from any language that can open a serial port.

### Opening the Bus Pirate serial port (POSIX)

```c
// bp_serial.h
#ifndef BP_SERIAL_H
#define BP_SERIAL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int fd;          /* file descriptor           */
    char dev[64];    /* e.g. /dev/ttyUSB0         */
} BusPirate;

int  bp_open(BusPirate *bp, const char *dev);
void bp_close(BusPirate *bp);
int  bp_write(BusPirate *bp, const uint8_t *buf, size_t len);
int  bp_read_byte(BusPirate *bp, uint8_t *out, int timeout_ms);
int  bp_enter_binary_mode(BusPirate *bp);
int  bp_enter_uart_mode(BusPirate *bp, uint32_t baud, uint8_t parity, uint8_t stop);

#endif /* BP_SERIAL_H */
```

```c
// bp_serial.c — Bus Pirate host library (C99, POSIX)
#include "bp_serial.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>

/* ------------------------------------------------------------------ */
/*  Low-level serial helpers                                           */
/* ------------------------------------------------------------------ */

int bp_open(BusPirate *bp, const char *dev)
{
    strncpy(bp->dev, dev, sizeof(bp->dev) - 1);
    bp->fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (bp->fd < 0) { perror("open"); return -1; }

    struct termios tio;
    tcgetattr(bp->fd, &tio);
    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 1;   /* 100 ms read timeout */
    tcsetattr(bp->fd, TCSANOW, &tio);
    tcflush(bp->fd, TCIOFLUSH);
    return 0;
}

void bp_close(BusPirate *bp) { close(bp->fd); bp->fd = -1; }

int bp_write(BusPirate *bp, const uint8_t *buf, size_t len)
{
    return (int)write(bp->fd, buf, len);
}

int bp_read_byte(BusPirate *bp, uint8_t *out, int timeout_ms)
{
    fd_set rd;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    FD_ZERO(&rd); FD_SET(bp->fd, &rd);
    int r = select(bp->fd + 1, &rd, NULL, NULL, &tv);
    if (r <= 0) return r;               /* 0 = timeout, -1 = error   */
    return (int)read(bp->fd, out, 1);
}

/* ------------------------------------------------------------------ */
/*  Binary mode entry                                                   */
/* ------------------------------------------------------------------ */

int bp_enter_binary_mode(BusPirate *bp)
{
    /* Send 0x00 up to 20 times; Bus Pirate replies "BBIO1"           */
    uint8_t zero = 0x00;
    char    resp[6] = {0};

    for (int attempt = 0; attempt < 20; ++attempt) {
        bp_write(bp, &zero, 1);
        usleep(5000);
        uint8_t b;
        int n = 0;
        while (bp_read_byte(bp, &b, 50) == 1 && n < 5)
            resp[n++] = (char)b;
        if (strncmp(resp, "BBIO1", 5) == 0) return 0;
    }
    fprintf(stderr, "bp_enter_binary_mode: no BBIO1 response\n");
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Binary UART mode                                                    */
/* ------------------------------------------------------------------ */

/* Baud rate config byte: 0x60 | index
 *   index: 0=300 1=1200 2=2400 3=4800 4=9600
 *          5=19200 6=38400 7=57600 8=115200  */
static int baud_to_index(uint32_t baud)
{
    static const uint32_t table[] =
        {300,1200,2400,4800,9600,19200,38400,57600,115200};
    for (int i = 0; i < 9; i++)
        if (table[i] == baud) return i;
    return -1;
}

int bp_enter_uart_mode(BusPirate *bp, uint32_t baud,
                       uint8_t parity /* 0=none 1=even 2=odd */,
                       uint8_t stop   /* 1 or 2 */)
{
    /* Enter binary UART mode: send 0x03, expect "ART1"               */
    uint8_t cmd = 0x03;
    bp_write(bp, &cmd, 1);
    char resp[5] = {0};
    uint8_t b; int n = 0;
    while (bp_read_byte(bp, &b, 200) == 1 && n < 4) resp[n++] = (char)b;
    if (strncmp(resp, "ART1", 4) != 0) {
        fprintf(stderr, "bp_enter_uart_mode: no ART1\n"); return -1;
    }

    /* Configure baud: 0x60 | index                                   */
    int idx = baud_to_index(baud);
    if (idx < 0) { fprintf(stderr, "unsupported baud %u\n", baud); return -1; }
    cmd = (uint8_t)(0x60 | idx);
    bp_write(bp, &cmd, 1);
    bp_read_byte(bp, &b, 100);    /* 0x01 = OK */

    /* Configure frame: 0x80 | (output<<4) | (parity<<2) | (data_bits<<1) | stop_bits
     * For simplicity: 8N1 = 0x80|0x00, 8E1 = 0x80|0x04, 8O1 = 0x80|0x08           */
    uint8_t par_bits = (parity & 0x03) << 2;
    uint8_t stp_bits = (stop == 2) ? 0x01 : 0x00;
    cmd = (uint8_t)(0x80 | 0x10 | par_bits | stp_bits); /* 0x10 = normal output */
    bp_write(bp, &cmd, 1);
    bp_read_byte(bp, &b, 100);    /* 0x01 = OK */

    return 0;
}
```

### Sending and receiving UART data via Bus Pirate binary mode

```c
// bp_uart_demo.c
#include "bp_serial.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Binary UART bulk transfer: 0x10 | (len-1) followed by bytes        */
static int bp_uart_send(BusPirate *bp, const uint8_t *data, uint8_t len)
{
    if (len == 0 || len > 16) return -1;
    uint8_t hdr = (uint8_t)(0x10 | (len - 1));
    bp_write(bp, &hdr, 1);
    bp_write(bp, data, len);

    /* Each byte ACK'd with 0x01; collect them                        */
    for (int i = 0; i < len; ++i) {
        uint8_t ack;
        if (bp_read_byte(bp, &ack, 200) != 1 || ack != 0x01) {
            fprintf(stderr, "UART send NAK at byte %d\n", i);
            return -1;
        }
    }
    return 0;
}

int main(void)
{
    BusPirate bp;
    if (bp_open(&bp, "/dev/ttyUSB0") != 0) return 1;

    puts("Entering binary mode...");
    if (bp_enter_binary_mode(&bp) != 0) goto fail;

    puts("Entering UART mode (115200, 8N1)...");
    if (bp_enter_uart_mode(&bp, 115200, 0, 1) != 0) goto fail;

    /* Send "AT\r\n" to a modem / ESP8266 / serial device             */
    const uint8_t cmd[] = "AT\r\n";
    printf("Sending: %s", (char *)cmd);
    bp_uart_send(&bp, cmd, (uint8_t)(sizeof(cmd) - 1));

    /* Read response for 2 seconds                                    */
    puts("Response:");
    uint8_t b;
    for (int i = 0; i < 2000; ++i) {
        if (bp_read_byte(&bp, &b, 1) == 1)
            putchar((char)b);
    }
    putchar('\n');

fail:
    bp_close(&bp);
    return 0;
}
```

### UART protocol scanner (baud-rate detection)

```c
// bp_baud_scan.c — tries common baud rates and looks for printable ASCII
#include "bp_serial.h"
#include <stdio.h>
#include <ctype.h>

static const uint32_t baud_list[] =
    {300,1200,2400,4800,9600,19200,38400,57600,115200};

static int score_printable(const uint8_t *buf, int len)
{
    int score = 0;
    for (int i = 0; i < len; i++)
        if (isprint(buf[i]) || buf[i] == '\r' || buf[i] == '\n')
            score++;
    return score;
}

int main(void)
{
    BusPirate bp;
    if (bp_open(&bp, "/dev/ttyUSB0") != 0) return 1;

    for (int i = 0; i < 9; ++i) {
        uint32_t baud = baud_list[i];
        printf("Testing %6u baud ... ", baud);
        fflush(stdout);

        if (bp_enter_binary_mode(&bp) != 0) { puts("binary mode fail"); break; }
        if (bp_enter_uart_mode(&bp, baud, 0, 1) != 0) { puts("uart mode fail"); continue; }

        uint8_t buf[64]; int n = 0, total = 0;
        uint8_t b;
        while (bp_read_byte(&bp, &b, 10) == 1 && n < 64)
            buf[n++] = b;
        total = score_printable(buf, n);

        printf("received %d bytes, printable score %d", n, total);
        if (total > n / 2) printf("  <-- LIKELY MATCH");
        putchar('\n');
    }

    bp_close(&bp);
    return 0;
}
```

---

## Scripting Bus Pirate from a Host — Rust

Rust's `serialport` crate provides cross-platform serial I/O. The Bus Pirate binary protocol maps
cleanly to Rust's type system and error handling.

### Cargo.toml dependencies

```toml
[dependencies]
serialport  = "4"
thiserror   = "1"
anyhow      = "1"
```

### Bus Pirate driver in Rust

```rust
// src/bus_pirate.rs
use anyhow::{bail, Context, Result};
use serialport::{SerialPort, TTYPort};
use std::io::{Read, Write};
use std::time::Duration;

pub struct BusPirate {
    port: Box<dyn SerialPort>,
}

impl BusPirate {
    /// Open the virtual COM port at 115 200 baud (always used for control)
    pub fn open(path: &str) -> Result<Self> {
        let port = serialport::new(path, 115_200)
            .timeout(Duration::from_millis(200))
            .open()
            .with_context(|| format!("Failed to open {path}"))?;
        Ok(Self { port })
    }

    // ----------------------------------------------------------------
    // Raw I/O
    // ----------------------------------------------------------------

    pub fn write_bytes(&mut self, data: &[u8]) -> Result<()> {
        self.port.write_all(data).context("write_bytes")
    }

    /// Read exactly `n` bytes; returns fewer if timeout fires
    pub fn read_bytes(&mut self, n: usize) -> Vec<u8> {
        let mut buf = vec![0u8; n];
        let mut total = 0;
        while total < n {
            match self.port.read(&mut buf[total..]) {
                Ok(0) | Err(_) => break,
                Ok(k) => total += k,
            }
        }
        buf.truncate(total);
        buf
    }

    pub fn read_byte(&mut self) -> Option<u8> {
        let v = self.read_bytes(1);
        v.first().copied()
    }

    // ----------------------------------------------------------------
    // Binary bit-bang mode entry
    // ----------------------------------------------------------------

    pub fn enter_binary_mode(&mut self) -> Result<()> {
        for _ in 0..20 {
            self.write_bytes(&[0x00])?;
            std::thread::sleep(Duration::from_millis(5));
            let resp = self.read_bytes(5);
            if resp.starts_with(b"BBIO1") {
                return Ok(());
            }
        }
        bail!("Did not receive BBIO1; is Bus Pirate connected and idle?")
    }

    // ----------------------------------------------------------------
    // Binary UART mode entry
    // ----------------------------------------------------------------

    pub fn enter_uart_mode(&mut self, baud: u32, parity: Parity, stop: StopBits) -> Result<()> {
        self.write_bytes(&[0x03])?;
        let resp = self.read_bytes(4);
        if &resp != b"ART1" {
            bail!("Expected ART1, got {:?}", resp);
        }

        // Configure baud rate
        let baud_idx = Self::baud_index(baud)
            .with_context(|| format!("Unsupported baud rate {baud}"))?;
        self.write_bytes(&[0x60 | baud_idx])?;
        let ack = self.read_byte().unwrap_or(0);
        if ack != 0x01 { bail!("Baud config NAK (got 0x{ack:02X})"); }

        // Configure frame format
        let par: u8 = match parity {
            Parity::None => 0b00,
            Parity::Even => 0b01,
            Parity::Odd  => 0b10,
        };
        let stp: u8 = match stop {
            StopBits::One => 0,
            StopBits::Two => 1,
        };
        let frame_cfg = 0x80_u8 | 0x10 | (par << 2) | stp;
        self.write_bytes(&[frame_cfg])?;
        let ack = self.read_byte().unwrap_or(0);
        if ack != 0x01 { bail!("Frame config NAK (got 0x{ack:02X})"); }

        Ok(())
    }

    // ----------------------------------------------------------------
    // UART bulk send (binary mode)
    // ----------------------------------------------------------------

    /// Send 1–16 bytes via Bus Pirate binary UART bulk write
    pub fn uart_send(&mut self, data: &[u8]) -> Result<()> {
        if data.is_empty() || data.len() > 16 {
            bail!("uart_send: length must be 1..=16, got {}", data.len());
        }
        let hdr = 0x10_u8 | (data.len() as u8 - 1);
        self.write_bytes(&[hdr])?;
        self.write_bytes(data)?;
        for i in 0..data.len() {
            let ack = self.read_byte().unwrap_or(0);
            if ack != 0x01 {
                bail!("uart_send: NAK at byte index {i} (got 0x{ack:02X})");
            }
        }
        Ok(())
    }

    // ----------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------

    fn baud_index(baud: u32) -> Option<u8> {
        let table = [300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];
        table.iter().position(|&b| b == baud).map(|i| i as u8)
    }
}

// ----------------------------------------------------------------
// Configuration types
// ----------------------------------------------------------------

#[derive(Debug, Clone, Copy)]
pub enum Parity  { None, Even, Odd }

#[derive(Debug, Clone, Copy)]
pub enum StopBits { One, Two }
```

### UART probe application in Rust

```rust
// src/main.rs
mod bus_pirate;
use bus_pirate::{BusPirate, Parity, StopBits};
use anyhow::Result;
use std::time::{Duration, Instant};

fn main() -> Result<()> {
    let mut bp = BusPirate::open("/dev/ttyUSB0")?;

    println!("Entering binary mode...");
    bp.enter_binary_mode()?;

    println!("Entering UART mode (115200, 8N1)...");
    bp.enter_uart_mode(115_200, Parity::None, StopBits::One)?;

    // Send an AT command to test a serial device
    let cmd = b"AT\r\n";
    println!("TX: {}", String::from_utf8_lossy(cmd));
    bp.uart_send(cmd)?;

    // Collect response for 1 second
    let deadline = Instant::now() + Duration::from_secs(1);
    let mut response = Vec::new();
    while Instant::now() < deadline {
        let chunk = bp.read_bytes(64);
        response.extend_from_slice(&chunk);
        if chunk.is_empty() {
            std::thread::sleep(Duration::from_millis(10));
        }
    }

    println!("RX: {}", String::from_utf8_lossy(&response));
    Ok(())
}
```

### Baud-rate scanner in Rust

```rust
// src/baud_scanner.rs
use crate::bus_pirate::{BusPirate, Parity, StopBits};
use anyhow::Result;
use std::time::Duration;

static BAUD_RATES: &[u32] = &[300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];

fn printable_score(data: &[u8]) -> usize {
    data.iter()
        .filter(|&&b| b.is_ascii_graphic() || b == b'\r' || b == b'\n' || b == b' ')
        .count()
}

pub fn scan_baud_rates(dev: &str) -> Result<()> {
    for &baud in BAUD_RATES {
        print!("Testing {:>7} baud ... ", baud);

        let mut bp = BusPirate::open(dev)?;
        bp.enter_binary_mode()?;
        bp.enter_uart_mode(baud, Parity::None, StopBits::One)?;

        std::thread::sleep(Duration::from_millis(200));
        let data = bp.read_bytes(128);
        let score = printable_score(&data);

        print!("received {} bytes, printable {score}", data.len());
        if !data.is_empty() && score * 2 > data.len() {
            print!("  <-- LIKELY MATCH");
        }
        println!();
    }
    Ok(())
}
```

---

## Logic Analyzer Integration (Sigrok / PulseView)

[Sigrok](https://sigrok.org/) is an open-source signal analysis framework. **PulseView** is its
GUI front-end. Together they support hundreds of logic analyzers and oscilloscopes, and include a
comprehensive UART protocol decoder.

### Capturing UART with sigrok-cli

```bash
# List connected devices
sigrok-cli --scan

# Capture UART on D0 (RX), sample at 1 MHz, 10 000 samples, decode on the fly
sigrok-cli \
  --driver fx2lafw \
  --config samplerate=1M \
  --samples 100000 \
  --channels D0 \
  --protocol-decoder uart:rx=D0:baudrate=115200 \
  --protocol-decoder-annotations uart:rx \
  --output-format ascii

# Save a compressed .sr session file for later review in PulseView
sigrok-cli \
  --driver fx2lafw \
  --config samplerate=4M \
  --samples 4000000 \
  --output-file capture.sr \
  --output-format srzip
```

### PulseView UART decoder settings

| Decoder option | Typical value |
|---|---|
| Data channel | D0 (RX) or D1 (TX) |
| Sample rate | ≥ 8× baud rate |
| Baud rate | 9600 / 115200 / custom |
| Data bits | 8 |
| Parity | none / even / odd |
| Stop bits | 1 |
| Bit order | LSB first |
| Idle level | High (standard) or Low (inverted) |

**Rule of thumb:** sample at least **8×** the baud rate. For 115 200 baud, use ≥ 1 MSPS. For
reliable decoding, 16× or 32× is preferred (1.85 MSPS → use 2 MSPS or 4 MSPS).

---

## Decoding UART with libsigrokdecode — C

`libsigrokdecode` can be embedded in your own test tools to decode captured logic-level waveforms
without running PulseView.

```c
// sigrok_uart_decode.c — minimal libsigrokdecode UART example (C)
#include <libsigrokdecode/libsigrokdecode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void annotation_cb(struct srd_proto_data *pdata, void *cb_data)
{
    (void)cb_data;
    struct srd_proto_data_annotation *ann =
        (struct srd_proto_data_annotation *)pdata->data;

    /* ann->ann_class: 0=data, 1=start bits, 2=parity, 3=stop, 4=warnings */
    if (pdata->pdo->id == 0 /* 'rx' output */) {
        const char *text = ann->ann_text[0];
        printf("[%lu–%lu] %s\n",
               (unsigned long)pdata->start_sample,
               (unsigned long)pdata->end_sample,
               text ? text : "(null)");
    }
}

int main(void)
{
    srd_init(NULL);

    /* Load all protocol decoders from default path */
    srd_decoder_load_all();

    /* Create a decode session */
    struct srd_session *sess;
    srd_session_new(&sess);

    /* Instantiate UART decoder */
    struct srd_decoder_inst *inst;
    srd_inst_new(sess, "uart", NULL, &inst);

    /* Set decoder options */
    GHashTable *opts = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free,
        (GDestroyNotify)g_variant_unref);

    g_hash_table_insert(opts, g_strdup("rx"),
        g_variant_new_uint32(0));           /* channel 0 = RX          */
    g_hash_table_insert(opts, g_strdup("baudrate"),
        g_variant_new_uint64(115200));
    g_hash_table_insert(opts, g_strdup("num_data_bits"),
        g_variant_new_uint32(8));

    srd_inst_option_set(inst, opts);
    g_hash_table_unref(opts);

    /* Connect annotation output to our callback */
    srd_pd_output_callback_add(sess, SRD_OUTPUT_ANN,
                               annotation_cb, NULL);

    uint64_t samplerate = 2000000; /* 2 MSPS */
    srd_session_metadata_set(sess, SRD_CONF_SAMPLERATE,
                             g_variant_new_uint64(samplerate));
    srd_session_start(sess);

    /* Feed captured samples — here we use a synthetic buffer.
       In practice read this from a .sr file or logic analyser SDK.   */
    /* Each byte in 'samples' represents 8 channels (D0–D7).          */
    uint8_t *samples = NULL;     /* replace with real capture data    */
    size_t   num_samples = 0;
    /* ... load samples ... */

    if (samples && num_samples > 0)
        srd_session_send(sess, 0, num_samples - 1, samples, num_samples, 1);

    srd_session_terminate_reset(sess);
    srd_session_destroy(sess);
    srd_exit();

    free(samples);
    return 0;
}
```

---

## UART Sniffer / Tap in C/C++

A **UART tap** sits between two communicating devices, forwards all bytes, and logs them. This
requires three UART ports (two for the monitored link, one for the host debug console) or a dual-
channel USB-to-UART adapter.

```cpp
// uart_tap.cpp — Linux dual-port UART tap (C++17)
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

static int open_serial(const char *dev, speed_t speed)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror(dev); return -1; }
    struct termios tio{};
    cfmakeraw(&tio);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cc[VMIN] = 0; tio.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tio);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static std::string timestamp()
{
    using namespace std::chrono;
    auto now = system_clock::now().time_since_epoch();
    auto ms  = duration_cast<milliseconds>(now).count();
    char buf[32];
    snprintf(buf, sizeof(buf), "[%lld.%03lld]",
             (long long)(ms / 1000), (long long)(ms % 1000));
    return buf;
}

static void hex_dump(const char *direction, const uint8_t *buf, int n)
{
    std::ostringstream oss;
    oss << timestamp() << " " << direction << " ";
    for (int i = 0; i < n; i++)
        oss << std::hex << std::setw(2) << std::setfill('0')
            << (int)buf[i] << " ";
    oss << "  |";
    for (int i = 0; i < n; i++)
        oss << (char)(isprint(buf[i]) ? buf[i] : '.');
    oss << "|";
    std::cout << oss.str() << "\n";
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <dev_a> <dev_b> [baud]\n", argv[0]);
        return 1;
    }

    speed_t speed = B115200;
    if (argc >= 4 && std::stoi(argv[3]) == 9600) speed = B9600;

    int fda = open_serial(argv[1], speed);
    int fdb = open_serial(argv[2], speed);
    if (fda < 0 || fdb < 0) return 1;

    std::cout << "UART tap active: " << argv[1] << " <-> " << argv[2] << "\n";

    uint8_t buf[256];
    while (true) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(fda, &rd);
        FD_SET(fdb, &rd);
        int maxfd = std::max(fda, fdb) + 1;
        struct timeval tv{0, 10000};  /* 10 ms */

        if (select(maxfd, &rd, nullptr, nullptr, &tv) <= 0) continue;

        if (FD_ISSET(fda, &rd)) {
            int n = (int)read(fda, buf, sizeof(buf));
            if (n > 0) {
                hex_dump("A->B", buf, n);
                write(fdb, buf, (size_t)n);   /* forward to B         */
            }
        }
        if (FD_ISSET(fdb, &rd)) {
            int n = (int)read(fdb, buf, sizeof(buf));
            if (n > 0) {
                hex_dump("B->A", buf, n);
                write(fda, buf, (size_t)n);   /* forward to A         */
            }
        }
    }
}
```

---

## UART Sniffer / Tap in Rust

```rust
// src/uart_tap.rs
use anyhow::Result;
use serialport::SerialPort;
use std::io::{Read, Write};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

fn timestamp_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
}

fn hex_dump(direction: &str, data: &[u8]) {
    let ts = timestamp_ms();
    let hex: String = data.iter().map(|b| format!("{b:02X} ")).collect();
    let ascii: String = data
        .iter()
        .map(|&b| if b.is_ascii_graphic() || b == b' ' { b as char } else { '.' })
        .collect();
    println!("[{}.{:03}] {direction} {hex} |{ascii}|",
             ts / 1000, ts % 1000);
}

pub fn run_tap(dev_a: &str, dev_b: &str, baud: u32) -> Result<()> {
    let port_a: Arc<Mutex<Box<dyn SerialPort>>> = Arc::new(Mutex::new(
        serialport::new(dev_a, baud)
            .timeout(Duration::from_millis(10))
            .open()?,
    ));
    let port_b: Arc<Mutex<Box<dyn SerialPort>>> = Arc::new(Mutex::new(
        serialport::new(dev_b, baud)
            .timeout(Duration::from_millis(10))
            .open()?,
    ));

    let pa_clone = Arc::clone(&port_a);
    let pb_clone = Arc::clone(&port_b);

    // Thread A→B
    let t_ab = thread::spawn(move || {
        let mut buf = vec![0u8; 256];
        loop {
            let n = { pa_clone.lock().unwrap().read(&mut buf).unwrap_or(0) };
            if n > 0 {
                hex_dump("A→B", &buf[..n]);
                pb_clone.lock().unwrap().write_all(&buf[..n]).ok();
            }
        }
    });

    // Thread B→A
    let t_ba = thread::spawn(move || {
        let mut buf = vec![0u8; 256];
        loop {
            let n = { port_b.lock().unwrap().read(&mut buf).unwrap_or(0) };
            if n > 0 {
                hex_dump("B→A", &buf[..n]);
                port_a.lock().unwrap().write_all(&buf[..n]).ok();
            }
        }
    });

    t_ab.join().ok();
    t_ba.join().ok();
    Ok(())
}
```

---

## Loopback and Self-Test Utilities

A **loopback** connects TX to RX on the same port. Any byte written should come back immediately.
This validates the driver, the UART peripheral, and the physical connection.

### Hardware loopback

```
  Pin TX ─┐
           ├── physically jumpered (or via Bus Pirate MOSI→MISO)
  Pin RX ─┘
```

### Software loopback test — C

```c
// loopback_test.c — POSIX UART loopback self-test
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>

static int open_uart(const char *dev, speed_t speed)
{
    int fd = open(dev, O_RDWR | O_NOCTTY);
    struct termios tio;
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 10;   /* 1 s timeout */
    tcsetattr(fd, TCSANOW, &tio);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int main(void)
{
    int fd = open_uart("/dev/ttyUSB0", B115200);
    int pass = 0, fail = 0;

    for (int round = 0; round < 256; ++round) {
        uint8_t tx = (uint8_t)round;
        uint8_t rx = 0;

        write(fd, &tx, 1);
        int n = (int)read(fd, &rx, 1);

        if (n == 1 && rx == tx) {
            pass++;
        } else {
            fprintf(stderr, "FAIL: sent 0x%02X, got 0x%02X (n=%d)\n", tx, rx, n);
            fail++;
        }
    }

    printf("Loopback test: %d PASS, %d FAIL\n", pass, fail);
    close(fd);
    return fail == 0 ? 0 : 1;
}
```

### Software loopback test — Rust

```rust
// src/loopback_test.rs
use anyhow::{Context, Result};
use serialport::SerialPort;
use std::io::{Read, Write};
use std::time::Duration;

pub fn run_loopback_test(dev: &str, baud: u32) -> Result<()> {
    let mut port = serialport::new(dev, baud)
        .timeout(Duration::from_millis(500))
        .open()
        .with_context(|| format!("Cannot open {dev}"))?;

    let (mut pass, mut fail) = (0usize, 0usize);

    for byte in 0u8..=255 {
        port.write_all(&[byte])?;
        let mut buf = [0u8; 1];
        match port.read_exact(&mut buf) {
            Ok(()) if buf[0] == byte => pass += 1,
            Ok(()) => {
                eprintln!("FAIL: sent 0x{byte:02X}, echoed 0x{:02X}", buf[0]);
                fail += 1;
            }
            Err(e) => {
                eprintln!("FAIL: sent 0x{byte:02X}, read error: {e}");
                fail += 1;
            }
        }
    }

    println!("Loopback test: {pass} PASS, {fail} FAIL");
    if fail > 0 { anyhow::bail!("{fail} loopback failures"); }
    Ok(())
}
```

---

## Automated UART Test Harness — C/C++

A full test harness sends stimulus, receives response, and validates against expected patterns.
This pattern is widely used in CI pipelines for embedded firmware.

```cpp
// uart_test_harness.cpp — C++17 keyword-driven UART test runner
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

// ----------------------------------------------------------------
// Serial helpers
// ----------------------------------------------------------------

static int open_uart(const char *dev, speed_t spd)
{
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) { perror(dev); return -1; }
    struct termios tio{};
    cfmakeraw(&tio);
    cfsetispeed(&tio, spd);
    cfsetospeed(&tio, spd);
    tio.c_cc[VMIN] = 0; tio.c_cc[VTIME] = 5; /* 500 ms */
    tcsetattr(fd, TCSANOW, &tio);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static std::string read_until(int fd, const char *sentinel, int timeout_ms)
{
    std::string buf;
    auto deadline = [] { return (long long)time(nullptr) * 1000; };
    long long end = deadline() + timeout_ms;

    while (deadline() < end) {
        fd_set rd; FD_ZERO(&rd); FD_SET(fd, &rd);
        struct timeval tv{0, 20000};
        if (select(fd+1, &rd, nullptr, nullptr, &tv) <= 0) continue;
        char c;
        if (read(fd, &c, 1) == 1) {
            buf += c;
            if (buf.find(sentinel) != std::string::npos) break;
        }
    }
    return buf;
}

// ----------------------------------------------------------------
// Test case infrastructure
// ----------------------------------------------------------------

struct TestCase {
    std::string name;
    std::string stimulus;
    std::string expected;       /* substring match */
    int         timeout_ms{500};
};

struct TestResult { std::string name; bool passed; std::string actual; };

static TestResult run_test(int fd, const TestCase &tc)
{
    tcflush(fd, TCIOFLUSH);
    write(fd, tc.stimulus.data(), tc.stimulus.size());
    std::string resp = read_until(fd, tc.expected.c_str(), tc.timeout_ms);
    bool ok = resp.find(tc.expected) != std::string::npos;
    return { tc.name, ok, resp };
}

// ----------------------------------------------------------------
// Test suite definition
// ----------------------------------------------------------------

static const std::vector<TestCase> g_tests = {
    { "Ping",        "ping\r\n",  "pong",        500  },
    { "Version",     "version\r\n", "v1.",        500  },
    { "Overflow",    std::string(4096, 'A') + "\r\n", "ERR", 1000 },
    { "Empty cmd",   "\r\n",       ">",           200  },
    { "Unknown cmd", "????\r\n",   "unknown",     500  },
};

int main(int argc, char *argv[])
{
    const char *dev = argc > 1 ? argv[1] : "/dev/ttyUSB0";
    int fd = open_uart(dev, B115200);
    if (fd < 0) return 1;

    int pass = 0, fail = 0;
    for (const auto &tc : g_tests) {
        TestResult r = run_test(fd, tc);
        printf("[%s] %s\n", r.passed ? "PASS" : "FAIL", r.name.c_str());
        if (!r.passed)
            printf("  Expected: '%s'\n  Got:      '%s'\n",
                   tc.expected.c_str(), r.actual.c_str());
        r.passed ? pass++ : fail++;
    }

    printf("\n%d / %d tests passed.\n", pass, pass + fail);
    close(fd);
    return fail ? 1 : 0;
}
```

---

## Automated UART Test Harness — Rust

```rust
// src/test_harness.rs
use anyhow::{Context, Result};
use serialport::SerialPort;
use std::io::{Read, Write};
use std::time::{Duration, Instant};

// ----------------------------------------------------------------
// Test case definition
// ----------------------------------------------------------------

pub struct TestCase<'a> {
    pub name:       &'a str,
    pub stimulus:   &'a [u8],
    pub expected:   &'a str,    /* substring that must appear in response */
    pub timeout_ms: u64,
}

pub struct TestResult {
    pub name:   String,
    pub passed: bool,
    pub actual: String,
}

// ----------------------------------------------------------------
// Serial read-until-sentinel with timeout
// ----------------------------------------------------------------

fn read_until(port: &mut dyn SerialPort, sentinel: &str, timeout: Duration) -> String {
    let deadline = Instant::now() + timeout;
    let mut buf   = String::new();
    let mut tmp   = [0u8; 64];

    while Instant::now() < deadline {
        match port.read(&mut tmp) {
            Ok(0) | Err(_) => {}
            Ok(n) => {
                buf.push_str(&String::from_utf8_lossy(&tmp[..n]));
                if buf.contains(sentinel) { break; }
            }
        }
    }
    buf
}

// ----------------------------------------------------------------
// Run a single test case
// ----------------------------------------------------------------

pub fn run_test(port: &mut dyn SerialPort, tc: &TestCase<'_>) -> TestResult {
    // Flush
    port.clear(serialport::ClearBuffer::All).ok();

    // Send stimulus
    port.write_all(tc.stimulus).ok();

    // Collect response
    let actual = read_until(port, tc.expected, Duration::from_millis(tc.timeout_ms));
    let passed = actual.contains(tc.expected);

    TestResult {
        name:   tc.name.to_string(),
        passed,
        actual,
    }
}

// ----------------------------------------------------------------
// Test suite runner
// ----------------------------------------------------------------

pub fn run_suite(dev: &str, baud: u32) -> Result<()> {
    let mut port = serialport::new(dev, baud)
        .timeout(Duration::from_millis(50))
        .open()
        .with_context(|| format!("Cannot open {dev}"))?;

    let tests = vec![
        TestCase { name: "Ping",        stimulus: b"ping\r\n",    expected: "pong",    timeout_ms: 500 },
        TestCase { name: "Version",     stimulus: b"version\r\n", expected: "v1.",     timeout_ms: 500 },
        TestCase { name: "Empty",       stimulus: b"\r\n",        expected: ">",       timeout_ms: 200 },
        TestCase { name: "Unknown cmd", stimulus: b"????\r\n",    expected: "unknown", timeout_ms: 500 },
    ];

    let (mut pass, mut fail) = (0usize, 0usize);

    for tc in &tests {
        let result = run_test(port.as_mut(), tc);
        let status = if result.passed { "PASS" } else { "FAIL" };
        println!("[{status}] {}", result.name);
        if !result.passed {
            println!("  Expected substring: {:?}", tc.expected);
            println!("  Got: {:?}", result.actual);
            fail += 1;
        } else {
            pass += 1;
        }
    }

    println!("\n{pass}/{} tests passed.", pass + fail);
    if fail > 0 { anyhow::bail!("{fail} test(s) failed"); }
    Ok(())
}
```

---

## Common Debug Scenarios and Patterns

### 1. No output from the target device

**Checklist:**

- TX and RX are not swapped (remember: TX of one device goes to RX of the other).
- Baud rate matches on both sides. Use the baud scanner above if unknown.
- Voltage levels are compatible. Bus Pirate is 3.3 V; a 5 V device may not trigger the 3.3 V threshold.
- Device is actually powered and running the expected firmware.
- UART peripheral is initialised before the first byte is transmitted (check boot delay).

### 2. Garbled data (framing errors)

**Possible causes and fixes:**

| Symptom | Cause | Fix |
|---|---|---|
| Every byte offset by 1 bit | Baud rate mismatch | Recalculate BRG; use oscilloscope on idle-to-start transition |
| Occasional corruption | Clock source tolerance | Use a crystal, not an internal RC oscillator, or enable auto-baud |
| All bytes 0xFF or 0x00 | Polarity inverted | Toggle idle-level setting (UART vs inverted UART) |
| Consistent bit-pattern error | Wrong stop bits | Change 1↔2 stop bits |
| Burst errors on long frames | Flow control ignored | Enable RTS/CTS or add delay between chunks |

### 3. Bus Pirate binary mode not responding

```c
/* If enter_binary_mode() keeps failing, try a hard reset first:
   Send 0x0F (binary mode reset) before the 0x00 sequence            */
static void bp_reset(BusPirate *bp)
{
    uint8_t reset_cmd = 0x0F;
    bp_write(bp, &reset_cmd, 1);
    usleep(100000);              /* 100 ms */
    tcflush(bp->fd, TCIOFLUSH);
}
```

### 4. Measuring UART timing with Bus Pirate AUX pin

```c
/* Toggle AUX pin to mark events on a logic analyser channel         */
static void bp_aux_high(BusPirate *bp)
{
    uint8_t cmd = 0b00010010;   /* config pins: AUX=output high      */
    bp_write(bp, &cmd, 1);
}
static void bp_aux_low(BusPirate *bp)
{
    uint8_t cmd = 0b00010000;   /* AUX=output low                    */
    bp_write(bp, &cmd, 1);
}
```

Trigger `bp_aux_high()` before sending a UART burst and `bp_aux_low()` after. The pulse appears as
a marker on your logic analyser, letting you precisely measure the latency between software event
and first UART bit.

### 5. Checking actual baud rate with an oscilloscope

Measure the width of the **start bit** (the first low-going pulse after the idle high). The
bit-period T = 1 / baud. For 115 200 baud, T ≈ 8.68 µs.

```
Idle ─────────┐ S  b0  b1  b2  b3  b4  b5  b6  b7  P ┌─── Idle
              └──┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
                 │←─── 8 data bits + optional parity ───→│
                 │← T →│  (measure this: T = 1 / actual_baud)
```

---

## Summary

| Tool / Technique | Best for |
|---|---|
| Bus Pirate interactive (terminal) | Quick manual exploration, transparent bridge, live hex monitor |
| Bus Pirate binary mode (C/Rust) | Programmatic stimulus / response, automated CI testing |
| Baud-rate scanner | Identifying unknown serial devices with no datasheet |
| UART tap / sniffer (C++/Rust) | Non-invasive capture of device-to-device communication |
| Loopback test (C/Rust) | Validating UART hardware, drivers, and cabling |
| sigrok-cli / PulseView | Capturing and visually decoding logic-level waveforms |
| libsigrokdecode in C | Embedding UART decoding into custom test tools |
| Automated test harness (C++/Rust) | Regression testing of embedded firmware over UART |

**Key takeaways:**

- The Bus Pirate is a versatile and inexpensive tool for UART exploration. Its binary mode enables
  full scriptability from any language capable of opening a serial port.
- Sampling a UART signal at **≥ 8× the baud rate** is the minimum for reliable logic-analyser
  decoding; 16× or higher is recommended for margin.
- The most common UART bugs are **TX/RX swap**, **baud-rate mismatch**, and **voltage-level
  mismatch**. A systematic hardware checklist eliminates the vast majority of issues before
  reaching for an analyser.
- Wrapping UART communication in an **automated test harness** pays dividends in catching
  regressions early, especially when combined with a Bus Pirate or virtual serial pair in CI.
- Rust's ownership model and `anyhow`/`thiserror` error handling make UART tooling robust with
  minimal boilerplate; C/C++ remains the lingua franca for embedded-adjacent host tools where
  portability and minimal dependencies matter.

---

*End of Chapter 87 — Bus Pirate and Debug Tools*