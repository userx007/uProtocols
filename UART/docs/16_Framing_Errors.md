# 16. UART Framing Errors

**Structure (10 sections + summary):**

- **UART Frame anatomy** — ASCII timing diagram showing start, data, parity, and stop bits with the exact sampling point where framing errors are detected.

- **Root causes** — baud rate mismatch with quantified drift calculations, signal integrity issues, protocol mismatch, break signals, and hot-plug glitches.

- **Hardware detection** — a cross-platform register reference table (STM32, AVR, NXP, Nordic, Linux tty, POSIX termios).

**C/C++ examples:**
- Bare-metal STM32 ISR with status-before-data-register read ordering (the critical pitfall)
- POSIX `termios` with `PARMRK` and a full 3-state parser for the `0xFF 0x00 <byte>` escape sequence
- Re-synchronisation logic with a consecutive-error counter
- A C++17 RAII `UartPort` class with a pluggable `FramingPolicy` (Discard / Substitute / Throw / Callback)

**Rust examples:**
- `serialport` crate desktop reader with typed `UartError` enum and `FramingStats`
- `no_std` bare-metal `UartRxQueue` using `heapless` for ISR-safe event queuing
- A `ResyncController` state machine (Synced → Recovering → WaitingIdle)

**Supporting material:** auto-baud detection sketch, CRC-16/CCITT guard, interrupt-driven full error path (STM32), a diagnostic BER/burst-length logger in Rust, and platform notes for Linux, Windows, RS-485, and FreeRTOS.

## Detecting and Recovering from Invalid Stop Bit Conditions

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [UART Frame Structure Recap](#2-uart-frame-structure-recap)
3. [What Is a Framing Error?](#3-what-is-a-framing-error)
4. [Root Causes](#4-root-causes)
5. [Hardware Detection Mechanisms](#5-hardware-detection-mechanisms)
6. [Software Detection and Recovery — C/C++](#6-software-detection-and-recovery--cc)
7. [Software Detection and Recovery — Rust](#7-software-detection-and-recovery--rust)
8. [Recovery Strategies](#8-recovery-strategies)
9. [Interrupt-Driven Handling](#9-interrupt-driven-handling)
10. [Logging and Diagnostics](#10-logging-and-diagnostics)
11. [Platform-Specific Notes](#11-platform-specific-notes)
12. [Summary](#12-summary)

---

## 1. Introduction

UART (Universal Asynchronous Receiver/Transmitter) is one of the oldest and most widely used serial
communication protocols in embedded systems, desktop computing, and telecommunications. Its
simplicity — no shared clock, no bus arbitration — also makes it sensitive to timing and
signal-integrity problems. Among the error conditions a UART receiver can report, the **framing
error** is the most fundamental: it tells you that the receiver found the line in an unexpected
state exactly where it expected to see a stop bit.

Understanding framing errors is essential for writing robust firmware and drivers, because an
unhandled framing error can corrupt a receive buffer, deadlock a state machine, or silently
drop bytes in a high-throughput data stream.

---

## 2. UART Frame Structure Recap

A standard UART frame has the following structure (transmitted LSB-first):

```
  Idle  Start  D0  D1  D2  D3  D4  D5  D6  D7  [Parity]  Stop(s)  Idle
 ──────┐      ┌───┬───┬───┬───┬───┬───┬───┬───┬─────────┐         ┌─────
  HIGH │  LOW │ b │ b │ b │ b │ b │ b │ b │ b │    P    │  HIGH   │ HIGH
       └──────┘   └───┴───┴───┴───┴───┴───┴───┘         └─────────┘
       ← start →←────────── data bits ──────────→← opt →← 1/1.5/2 stop →
```

Key timing constraints:

| Parameter | Typical value | Role |
|-----------|---------------|------|
| Start bit | 1 bit, always LOW | Signals frame start |
| Data bits | 5–9 bits | Payload |
| Parity bit | 0 or 1 bit | Optional error check |
| Stop bit(s) | 1, 1.5, or 2 bits, always HIGH | Marks frame end; provides idle time |

The receiver samples each bit at the **midpoint** of the expected bit period. After all data (and
optional parity) bits are sampled, it samples the stop bit position. The line **must** be HIGH
at that moment. If it is LOW, a framing error is flagged.

---

## 3. What Is a Framing Error?

A **framing error** occurs when the UART receiver samples a **logical 0 (LOW)** at the position
where it expects a **logical 1 (HIGH)** stop bit.

### 3.1 Normal vs. Erroneous Frame

**Normal frame (no error):**
```
Start  D0…D7   Stop
  0   xxxxxxxx   1   ← stop bit sampled HIGH → OK
```

**Framing error:**
```
Start  D0…D7   Stop
  0   xxxxxxxx   0   ← stop bit sampled LOW → FRAMING ERROR
```

### 3.2 Framing Error vs. Other UART Errors

| Error type | Cause | Detection mechanism |
|------------|-------|---------------------|
| **Framing Error (FE)** | Stop bit is LOW | Hardware samples stop bit position |
| Parity Error (PE) | Data bits flipped | Parity bit mismatch |
| Overrun Error (OE) | New byte arrived before old one read | Receive buffer full |
| Break Condition | Line held LOW for ≥ 1 full frame | Special case of framing error |

A break condition is architecturally a special, extended framing error: the line stays LOW
for at least one full character time, meaning every bit — including stop — is sampled LOW.
Many UARTs report break and framing error simultaneously.

---

## 4. Root Causes

### 4.1 Baud Rate Mismatch

This is by far the most common cause. If transmitter and receiver disagree on baud rate by more
than roughly ±3–5%, the accumulated timing error over 10–11 bits will push the stop-bit sample
point into the wrong half of the bit period.

```
Transmitter: 9600 baud  (bit period = 104.17 µs)
Receiver:    9615 baud  (bit period = 104.01 µs)

After 10 bits: drift = 10 × (104.17 − 104.01) = 1.6 µs
  → well within tolerance (~52 µs margin), no error.

Transmitter: 9600 baud  (bit period = 104.17 µs)
Receiver:    10000 baud (bit period = 100.00 µs)

After 10 bits: drift = 10 × 4.17 µs = 41.7 µs
  → sample lands in wrong half → framing error!
```

### 4.2 Signal Integrity Problems

- Excessive cable capacitance rounding bit edges
- Ground loops injecting noise
- Impedance mismatch causing reflections on long lines
- Slow rise/fall times from weak drivers

### 4.3 Glitches and Line Noise

A single noise spike can pull the line LOW during a stop bit, even if all timing is correct.

### 4.4 Protocol Mismatch

- Transmitter uses 2 stop bits; receiver expects 1 (the second stop bit arrives as a new start bit)
- Different data-bit widths (e.g., transmitter sends 9-bit frames; receiver reads 8-bit)
- Parity configuration mismatch distorting the perceived frame boundary

### 4.5 Break Signal

An intentional RS-232/RS-485 break: the transmitting side drives the line LOW for a full frame
or more. Receivers use this as an out-of-band attention signal (e.g., LIN bus, DMX512 lighting).

### 4.6 Hot-Plug / Asynchronous Connection

Connecting a cable while data is already flowing causes the receiver to join mid-frame, almost
guaranteeing a framing error on the first byte seen.

---

## 5. Hardware Detection Mechanisms

Modern UART peripherals expose a **Framing Error** flag in a status register. The exact register
name varies by microcontroller family:

| Platform | Status register | Framing error bit |
|----------|----------------|-------------------|
| STM32 (USART) | `USARTx->ISR` | `USART_ISR_FE` (bit 1) |
| AVR (ATmega) | `UCSRnA` | `FEn` (bit 4) |
| NXP LPC / Kinetis | `UARTx_S1` | `FE` (bit 1) |
| Nordic nRF52 | `UARTE0->ERRORSRC` | bit 2 |
| Linux tty driver | `struct tty_port` | `TTY_FRAME` flag |
| POSIX termios | N/A (reported via read) | `\0` + parity-error byte pair |

**Important:** On most hardware, the framing error flag is latched per-byte and must be read
**before** reading the receive data register. Reading the data register clears the flag on many
architectures (e.g., AVR). Always read the status register first.

---

## 6. Software Detection and Recovery — C/C++

### 6.1 Bare-Metal STM32 (HAL-free)

```c
/*
 * uart_framing.c
 * Bare-metal STM32 UART framing error detection and recovery.
 * Target: STM32F4xx, USART2 at 9600 8N1
 */

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx.h"   /* CMSIS header */

/* ------------------------------------------------------------------ */
/* Statistics counters                                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t framing_errors;
    uint32_t parity_errors;
    uint32_t overrun_errors;
    uint32_t bytes_received;
} UartStats;

static volatile UartStats g_stats = {0};

/* ------------------------------------------------------------------ */
/* Initialise USART2: PA2 TX, PA3 RX, 9600 8N1, no parity             */
/* ------------------------------------------------------------------ */
void uart2_init(void)
{
    /* Enable clocks */
    RCC->AHB1ENR  |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR  |= RCC_APB1ENR_USART2EN;

    /* PA2 = TX, PA3 = RX → alternate function 7 */
    GPIOA->MODER  |=  (2u << 4) | (2u << 6);   /* AF mode */
    GPIOA->AFR[0] |=  (7u << 8) | (7u << 12);  /* AF7 = USART2 */

    /* Configure USART2: 9600 baud @ 42 MHz APB1 */
    USART2->BRR = 0x1117;   /* 42000000 / 9600 ≈ 4375 = 0x1117 */
    USART2->CR1 = USART_CR1_RE    /* Receiver enable  */
                | USART_CR1_TE    /* Transmitter enable */
                | USART_CR1_UE;   /* USART enable      */

    /* Enable error interrupts (framing, overrun, noise) */
    USART2->CR3 |= USART_CR3_EIE;

    /* Enable RXNE interrupt so we process every received byte */
    USART2->CR1 |= USART_CR1_RXNEIE;

    NVIC_SetPriority(USART2_IRQn, 5);
    NVIC_EnableIRQ(USART2_IRQn);
}

/* ------------------------------------------------------------------ */
/* Read a byte, checking for framing error.                             */
/* Returns true on success; false on framing error (byte discarded).   */
/* ------------------------------------------------------------------ */
bool uart2_read_byte(uint8_t *out)
{
    /* Step 1: snapshot status register BEFORE reading DR */
    uint32_t sr = USART2->SR;

    /* Step 2: always read DR to clear flags (even on error) */
    uint8_t data = (uint8_t)(USART2->DR & 0xFF);

    /* Step 3: check for framing error */
    if (sr & USART_SR_FE) {
        g_stats.framing_errors++;
        /* Optionally: if FE and data == 0x00 → probable BREAK condition */
        if (data == 0x00) {
            /* handle break signal */
        }
        return false;   /* discard byte */
    }

    if (sr & USART_SR_PE) { g_stats.parity_errors++;  return false; }
    if (sr & USART_SR_ORE) { g_stats.overrun_errors++; return false; }

    g_stats.bytes_received++;
    *out = data;
    return true;
}

/* ------------------------------------------------------------------ */
/* ISR — called on RXNE (byte ready) and on error conditions           */
/* ------------------------------------------------------------------ */
void USART2_IRQHandler(void)
{
    uint8_t byte;
    if (uart2_read_byte(&byte)) {
        /* Push byte into application ring buffer */
        ringbuf_push(byte);
    }
}
```

### 6.2 POSIX Linux / macOS (termios)

On a POSIX system, framing errors surfaced through a tty are signalled via a two-byte escape
sequence injected into the byte stream when `PARMRK` is set:

```
0xFF 0x00 <bad_byte>   ← framing or parity error on <bad_byte>
0xFF 0xFF              ← literal 0xFF data byte (escaped)
0x00 0x00              ← break condition (NUL after NUL)
```

```c
/*
 * posix_uart_framing.c
 * POSIX framing-error detection via PARMRK.
 * Compile: gcc -o posix_uart posix_uart_framing.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#define DEVICE  "/dev/ttyUSB0"
#define BAUD    B9600

/* ------------------------------------------------------------------ */
/* Open and configure the serial port                                   */
/* ------------------------------------------------------------------ */
int uart_open(const char *dev, speed_t baud)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return -1; }

    struct termios tty;
    memset(&tty, 0, sizeof tty);
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    tty.c_cflag  = CS8 | CLOCAL | CREAD;   /* 8 data bits, no modem ctrl */
    tty.c_iflag  = INPCK                   /* enable parity checking      */
                 | PARMRK;                 /* mark framing/parity errors  */
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;                  /* 1 s timeout                 */

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr"); close(fd); return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* ------------------------------------------------------------------ */
/* Framing-error-aware read loop                                        */
/* ------------------------------------------------------------------ */
typedef enum {
    STATE_NORMAL,
    STATE_FF_SEEN,      /* received 0xFF — could be escape prefix */
    STATE_FF_00_SEEN,   /* received 0xFF 0x00 — error prefix complete */
} ParseState;

typedef struct {
    uint32_t framing_errors;
    uint32_t break_conditions;
    uint32_t literal_ff;
    uint32_t good_bytes;
} Stats;

void uart_read_loop(int fd)
{
    uint8_t buf[256];
    ParseState state = STATE_NORMAL;
    Stats stats = {0};

    while (true) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EAGAIN) continue;   /* no data yet */
            perror("read"); break;
        }
        if (n == 0) continue;

        for (ssize_t i = 0; i < n; i++) {
            uint8_t b = buf[i];

            switch (state) {
            case STATE_NORMAL:
                if (b == 0xFF) {
                    state = STATE_FF_SEEN;
                } else {
                    /* Normal data byte */
                    stats.good_bytes++;
                    printf("DATA: 0x%02X\n", b);
                }
                break;

            case STATE_FF_SEEN:
                if (b == 0xFF) {
                    /* Escaped literal 0xFF */
                    stats.literal_ff++;
                    printf("DATA: 0xFF (literal)\n");
                    state = STATE_NORMAL;
                } else if (b == 0x00) {
                    /* 0xFF 0x00 — error marker prefix, next byte is bad */
                    state = STATE_FF_00_SEEN;
                } else {
                    /* Unexpected sequence — treat both as data */
                    stats.good_bytes += 2;
                    state = STATE_NORMAL;
                }
                break;

            case STATE_FF_00_SEEN:
                if (b == 0x00) {
                    /* 0xFF 0x00 0x00 → BREAK condition */
                    stats.break_conditions++;
                    fprintf(stderr, "BREAK condition detected!\n");
                } else {
                    /* 0xFF 0x00 <b> → framing or parity error on byte <b> */
                    stats.framing_errors++;
                    fprintf(stderr,
                        "FRAMING ERROR: bad byte = 0x%02X "
                        "(total errors: %u)\n",
                        b, stats.framing_errors);
                }
                state = STATE_NORMAL;
                break;
            }
        }
    }

    printf("\n--- Statistics ---\n");
    printf("Good bytes:       %u\n", stats.good_bytes);
    printf("Framing errors:   %u\n", stats.framing_errors);
    printf("Break conditions: %u\n", stats.break_conditions);
    printf("Literal 0xFF:     %u\n", stats.literal_ff);
}

int main(void)
{
    int fd = uart_open(DEVICE, BAUD);
    if (fd < 0) return EXIT_FAILURE;
    uart_read_loop(fd);
    close(fd);
    return EXIT_SUCCESS;
}
```

### 6.3 Re-synchronisation After Framing Errors

When framing errors are persistent (suggesting sustained baud-rate mismatch or a break), a
re-synchronisation strategy is needed:

```c
/*
 * uart_resync.c
 * Re-synchronisation strategy: flush + wait for idle + restart.
 */

#include <stdint.h>
#include <stdbool.h>

#define MAX_FE_BEFORE_RESYNC  5
#define RESYNC_TIMEOUT_MS    100

static uint32_t s_consecutive_fe = 0;

/* Call this from your byte-received callback */
void on_byte_received(bool framing_error, uint8_t byte)
{
    if (framing_error) {
        s_consecutive_fe++;

        if (s_consecutive_fe >= MAX_FE_BEFORE_RESYNC) {
            uart_resync();
        }
        return;
    }

    s_consecutive_fe = 0;   /* reset counter on good byte */
    process_byte(byte);
}

/* Flush the receive FIFO and discard bytes for RESYNC_TIMEOUT_MS */
void uart_resync(void)
{
    /* 1. Disable receiver to stop accumulating bad bytes */
    USART2->CR1 &= ~USART_CR1_RE;

    /* 2. Clear error flags by reading SR then DR */
    (void)USART2->SR;
    (void)USART2->DR;

    /* 3. Wait for line to return to idle (HIGH) */
    delay_ms(RESYNC_TIMEOUT_MS);

    /* 4. Re-enable receiver */
    USART2->CR1 |= USART_CR1_RE;

    /* 5. Reset application-level protocol state machine */
    protocol_reset();

    s_consecutive_fe = 0;
}
```

### 6.4 C++ RAII Wrapper with Error Policy

```cpp
/*
 * UartPort.hpp
 * C++17 RAII wrapper for a POSIX serial port with framing-error policy.
 */

#pragma once

#include <string>
#include <functional>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

enum class FramingPolicy {
    Discard,    /* Drop erroneous byte silently        */
    Substitute, /* Replace erroneous byte with placeholder */
    Throw,      /* Throw an exception                  */
    Callback,   /* Call user-provided error handler    */
};

class UartPort {
public:
    using ErrorCallback = std::function<void(uint8_t bad_byte)>;

    explicit UartPort(const std::string& device,
                      speed_t baud = B115200,
                      FramingPolicy policy = FramingPolicy::Discard,
                      uint8_t substitute  = '?',
                      ErrorCallback cb    = nullptr)
        : policy_(policy), substitute_(substitute), cb_(cb)
    {
        fd_ = open(device.c_str(), O_RDWR | O_NOCTTY);
        if (fd_ < 0) throw std::runtime_error("Cannot open " + device);
        configure(baud);
    }

    ~UartPort() { if (fd_ >= 0) close(fd_); }

    /* Non-copyable, movable */
    UartPort(const UartPort&)            = delete;
    UartPort& operator=(const UartPort&) = delete;
    UartPort(UartPort&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }

    /* Read one byte, applying framing-error policy.
     * Returns nullopt if the byte was discarded.                    */
    std::optional<uint8_t> read_byte()
    {
        /* With PARMRK, a framing error arrives as {0xFF, 0x00, byte} */
        uint8_t triple[3];
        ssize_t n = ::read(fd_, triple, 1);
        if (n <= 0) return std::nullopt;

        if (triple[0] == 0xFF) {
            /* Need two more bytes to determine type */
            if (::read(fd_, triple + 1, 2) != 2) return std::nullopt;

            if (triple[1] == 0xFF) {
                /* Literal 0xFF */
                return 0xFF;
            }
            if (triple[1] == 0x00) {
                framing_error_count_++;
                return handle_framing_error(triple[2]);
            }
        }
        return triple[0];
    }

    uint32_t framing_error_count() const noexcept { return framing_error_count_; }

private:
    int            fd_                  = -1;
    FramingPolicy  policy_;
    uint8_t        substitute_;
    ErrorCallback  cb_;
    uint32_t       framing_error_count_ = 0;

    std::optional<uint8_t> handle_framing_error(uint8_t bad_byte)
    {
        switch (policy_) {
        case FramingPolicy::Discard:
            return std::nullopt;
        case FramingPolicy::Substitute:
            return substitute_;
        case FramingPolicy::Throw:
            throw std::runtime_error("UART framing error");
        case FramingPolicy::Callback:
            if (cb_) cb_(bad_byte);
            return std::nullopt;
        }
        return std::nullopt;
    }

    void configure(speed_t baud)
    {
        struct termios tty{};
        cfsetispeed(&tty, baud);
        cfsetospeed(&tty, baud);
        tty.c_cflag = CS8 | CLOCAL | CREAD;
        tty.c_iflag = INPCK | PARMRK;
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 10;
        tcsetattr(fd_, TCSANOW, &tty);
    }
};
```

---

## 7. Software Detection and Recovery — Rust

### 7.1 `serialport` Crate (Desktop / Linux / macOS / Windows)

```toml
# Cargo.toml
[dependencies]
serialport = "4"
thiserror  = "1"
log        = "0.4"
env_logger = "0.11"
```

```rust
// src/uart_framing.rs
//! UART framing error detection using the `serialport` crate.
//! The crate surfaces POSIX PARMRK sequences on Unix; on Windows it
//! maps to COMSTAT error flags.

use std::io::{self, Read};
use std::time::Duration;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum UartError {
    #[error("Framing error on byte 0x{byte:02X}")]
    FramingError { byte: u8 },

    #[error("Break condition detected")]
    BreakCondition,

    #[error("IO error: {0}")]
    Io(#[from] io::Error),

    #[error("Serial port error: {0}")]
    Serial(#[from] serialport::Error),
}

/// Statistics collected by the reader.
#[derive(Debug, Default)]
pub struct FramingStats {
    pub good_bytes:       u64,
    pub framing_errors:   u64,
    pub break_conditions: u64,
}

/// State machine that interprets the PARMRK escape sequences.
#[derive(Debug, Default, PartialEq)]
enum ParseState {
    #[default]
    Normal,
    FfSeen,
    FfZeroSeen,
}

/// A UART reader that detects and handles framing errors.
pub struct FramingAwareReader {
    port:  Box<dyn serialport::SerialPort>,
    state: ParseState,
    pub stats: FramingStats,
}

impl FramingAwareReader {
    /// Open `device` at the given baud rate.
    pub fn open(device: &str, baud: u32) -> Result<Self, UartError> {
        let port = serialport::new(device, baud)
            .data_bits(serialport::DataBits::Eight)
            .stop_bits(serialport::StopBits::One)
            .parity(serialport::Parity::None)
            .timeout(Duration::from_millis(1000))
            .open()?;

        Ok(Self {
            port,
            state: ParseState::default(),
            stats: FramingStats::default(),
        })
    }

    /// Read the next valid data byte.
    ///
    /// Returns:
    /// - `Ok(Some(byte))` — valid data byte
    /// - `Ok(None)`       — erroneous byte was discarded (caller may retry)
    /// - `Err(_)`         — break condition or I/O error
    pub fn read_byte(&mut self) -> Result<Option<u8>, UartError> {
        let mut raw = [0u8; 1];
        self.port.read_exact(&mut raw)?;
        let b = raw[0];

        match self.state {
            ParseState::Normal => {
                if b == 0xFF {
                    self.state = ParseState::FfSeen;
                    Ok(None)   // need more bytes to decide
                } else {
                    self.stats.good_bytes += 1;
                    Ok(Some(b))
                }
            }

            ParseState::FfSeen => {
                if b == 0xFF {
                    // Escaped literal 0xFF
                    self.state = ParseState::Normal;
                    self.stats.good_bytes += 1;
                    Ok(Some(0xFF))
                } else if b == 0x00 {
                    self.state = ParseState::FfZeroSeen;
                    Ok(None)
                } else {
                    // Unexpected — treat both bytes as data (best-effort)
                    self.state = ParseState::Normal;
                    self.stats.good_bytes += 1;
                    Ok(Some(b))
                }
            }

            ParseState::FfZeroSeen => {
                self.state = ParseState::Normal;
                if b == 0x00 {
                    // BREAK condition
                    self.stats.break_conditions += 1;
                    Err(UartError::BreakCondition)
                } else {
                    // Framing (or parity) error on byte `b`
                    self.stats.framing_errors += 1;
                    log::warn!(
                        "Framing error: bad byte=0x{:02X}, total errors={}",
                        b, self.stats.framing_errors
                    );
                    Err(UartError::FramingError { byte: b })
                }
            }
        }
    }

    /// Convenience: read up to `max_bytes` valid bytes, skipping errors.
    pub fn read_good_bytes(&mut self, max_bytes: usize) -> Vec<u8> {
        let mut result = Vec::with_capacity(max_bytes);
        while result.len() < max_bytes {
            match self.read_byte() {
                Ok(Some(b)) => result.push(b),
                Ok(None)    => { /* mid-escape, continue */ }
                Err(UartError::FramingError { .. }) => {
                    // Discard and continue
                }
                Err(UartError::BreakCondition) => {
                    log::info!("Break: resetting protocol state");
                    break;
                }
                Err(e) => {
                    log::error!("Fatal: {e}");
                    break;
                }
            }
        }
        result
    }
}
```

### 7.2 Bare-Metal Rust with `embedded-hal`

```toml
# Cargo.toml (no_std embedded target, e.g. STM32F4)
[dependencies]
embedded-hal = "1.0"
nb           = "1.0"
heapless     = "0.8"
```

```rust
// src/embedded_uart.rs
//! Bare-metal framing-error handler for embedded-hal UART.
//! Works with any HAL crate that implements embedded_hal::serial traits.

#![no_std]

use core::fmt;
use heapless::spsc::Queue;

/// Ring buffer capacity (must be power of two for heapless)
const BUF_CAP: usize = 64;

/// All possible receive outcomes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RxEvent {
    /// A valid byte was received.
    Byte(u8),
    /// Framing error — stop bit was LOW.
    FramingError,
    /// Break condition (line held LOW ≥ 1 frame).
    Break,
    /// Parity error.
    ParityError,
    /// Overrun — byte lost because buffer was full.
    Overrun,
}

/// Aggregated error counters.
#[derive(Debug, Default)]
pub struct ErrorCounters {
    pub framing: u32,
    pub parity:  u32,
    pub overrun: u32,
    pub breaks:  u32,
}

/// A framing-error-aware UART receive queue for bare-metal use.
///
/// The queue stores `RxEvent` values so the application can distinguish
/// data bytes from error markers without losing their relative order.
pub struct UartRxQueue {
    queue:    Queue<RxEvent, BUF_CAP>,
    pub errs: ErrorCounters,
}

impl UartRxQueue {
    pub const fn new() -> Self {
        Self {
            queue: Queue::new(),
            errs:  ErrorCounters {
                framing: 0, parity: 0, overrun: 0, breaks: 0,
            },
        }
    }

    /// Call from the UART ISR.
    ///
    /// `status` is the hardware status register snapshot taken **before**
    /// reading the data register. `data` is the received byte.
    ///
    /// Bit masks (adjust for your MCU):
    ///   - `FE_MASK`  — Framing Error flag
    ///   - `PE_MASK`  — Parity Error flag
    ///   - `ORE_MASK` — Overrun Error flag
    ///   - `BREAK`    — Break Detection (or check FE + data == 0)
    pub fn push_from_isr(&mut self, status: u32, data: u8) {
        const FE_MASK:  u32 = 1 << 1;  // STM32 USART_SR_FE
        const PE_MASK:  u32 = 1 << 0;  // STM32 USART_SR_PE
        const ORE_MASK: u32 = 1 << 3;  // STM32 USART_SR_ORE

        let event = if status & FE_MASK != 0 && data == 0x00 {
            self.errs.breaks += 1;
            RxEvent::Break
        } else if status & FE_MASK != 0 {
            self.errs.framing += 1;
            RxEvent::FramingError
        } else if status & PE_MASK != 0 {
            self.errs.parity += 1;
            RxEvent::ParityError
        } else if status & ORE_MASK != 0 {
            self.errs.overrun += 1;
            RxEvent::Overrun
        } else {
            RxEvent::Byte(data)
        };

        // Best-effort enqueue; if queue is full, the event is dropped
        let _ = self.queue.enqueue(event);
    }

    /// Dequeue the next event (call from main loop / task).
    pub fn pop(&mut self) -> Option<RxEvent> {
        self.queue.dequeue()
    }

    /// Drain all framing errors from the queue, returning only good bytes.
    pub fn drain_good<F: FnMut(u8)>(&mut self, mut f: F) {
        while let Some(event) = self.pop() {
            if let RxEvent::Byte(b) = event {
                f(b);
            }
        }
    }
}
```

### 7.3 Re-synchronisation State Machine in Rust

```rust
// src/resync.rs
//! Protocol-level re-synchronisation after sustained framing errors.

use core::time::Duration;

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum SyncState {
    Synced,
    Recovering { consecutive_errors: u8 },
    WaitingIdle,
}

pub struct ResyncController {
    pub state:              SyncState,
    max_errors_before_resync: u8,
    idle_wait:              Duration,
}

impl ResyncController {
    pub fn new(max_errors: u8, idle_wait: Duration) -> Self {
        Self {
            state: SyncState::Synced,
            max_errors_before_resync: max_errors,
            idle_wait,
        }
    }

    /// Feed each RxEvent into the controller.
    /// Returns `true` if the application should process the byte,
    /// `false` if it should be discarded.
    pub fn feed(&mut self, event: super::RxEvent) -> bool {
        use super::RxEvent::*;
        use SyncState::*;

        match (&self.state, event) {
            // ── Happy path ────────────────────────────────────────────
            (Synced, Byte(_)) => true,

            // ── First framing error while synced ─────────────────────
            (Synced, FramingError | ParityError) => {
                self.state = Recovering { consecutive_errors: 1 };
                false
            }

            // ── Accumulating errors ───────────────────────────────────
            (Recovering { consecutive_errors }, FramingError | ParityError)
                if *consecutive_errors + 1 >= self.max_errors_before_resync =>
            {
                log::warn!("Too many framing errors — entering WaitingIdle");
                self.state = WaitingIdle;
                self.initiate_resync();
                false
            }

            (Recovering { consecutive_errors }, FramingError | ParityError) => {
                self.state = Recovering {
                    consecutive_errors: consecutive_errors + 1,
                };
                false
            }

            // ── Good byte clears the error count ─────────────────────
            (Recovering { .. }, Byte(_)) => {
                self.state = Synced;
                true
            }

            // ── Waiting for idle: discard everything ─────────────────
            (WaitingIdle, Byte(_)) => {
                // Still receiving → not idle yet, keep waiting
                false
            }

            // ── Break while waiting → treat as resync signal ──────────
            (_, Break) => {
                log::info!("BREAK received — resynchronising now");
                self.state = Synced;
                false
            }

            _ => false,
        }
    }

    fn initiate_resync(&self) {
        // Platform-specific: flush FIFO, wait idle_wait, re-enable RX.
        // On bare-metal you'd poke registers; on POSIX you'd tcflush().
        log::info!("Resync: flushing RX and waiting {:?}", self.idle_wait);
    }
}
```

---

## 8. Recovery Strategies

The appropriate recovery strategy depends on the application's error tolerance and the cause
of the framing errors.

### 8.1 Decision Matrix

| Cause | Short-term recovery | Long-term fix |
|-------|---------------------|---------------|
| Baud rate mismatch | Auto-baud detection | Fix configuration |
| Noise / glitch | Discard byte, continue | Shield cable, reduce baud rate |
| Break signal (intentional) | Reset protocol SM | Design break handling |
| Hot-plug glitch | Flush + wait idle | Debounce connect event |
| Sustained errors (≥ N) | Flush + resync timeout | Investigate hardware |

### 8.2 Auto-Baud Detection (Concept)

When the baud rate is unknown, measure the width of the start bit:

```c
/*
 * Auto-baud: measure the start-bit duration using a timer capture.
 * The start bit is always exactly 1 bit period wide.
 */
void auto_baud_from_start_bit(uint32_t captured_timer_ticks,
                               uint32_t timer_freq_hz)
{
    /* bit_period_us = captured_ticks / (timer_freq / 1e6) */
    uint32_t bit_period_us = (captured_timer_ticks * 1000000UL)
                             / timer_freq_hz;
    uint32_t detected_baud = 1000000UL / bit_period_us;

    /* Round to nearest standard baud rate */
    const uint32_t std_bauds[] =
        {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400};
    uint32_t best = std_bauds[0];
    uint32_t min_diff = UINT32_MAX;

    for (size_t i = 0; i < sizeof std_bauds / sizeof *std_bauds; i++) {
        uint32_t diff = (detected_baud > std_bauds[i])
                      ? (detected_baud - std_bauds[i])
                      : (std_bauds[i] - detected_baud);
        if (diff < min_diff) { min_diff = diff; best = std_bauds[i]; }
    }

    uart_set_baud(best);
}
```

### 8.3 Protocol-Level CRC Guard

Even without framing errors, wrapping payloads in a CRC gives an independent corruption check:

```c
#include <stdint.h>
#include <stddef.h>

uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

bool frame_is_valid(const uint8_t *frame, size_t payload_len)
{
    /* Last 2 bytes are CRC16 big-endian */
    uint16_t received = ((uint16_t)frame[payload_len] << 8)
                      |  (uint16_t)frame[payload_len + 1];
    uint16_t computed = crc16_ccitt(frame, payload_len);
    return received == computed;
}
```

---

## 9. Interrupt-Driven Handling

Full interrupt-driven handler for STM32 showing all error paths:

```c
/* stm32_uart_isr.c — Interrupt-driven UART with full error handling */

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

#define RX_BUF_SIZE 256

static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0, rx_tail = 0;
static volatile uint32_t fe_count = 0, pe_count = 0, ore_count = 0;

static inline bool rxbuf_push(uint8_t b)
{
    uint16_t next = (rx_head + 1) % RX_BUF_SIZE;
    if (next == rx_tail) return false;   /* buffer full */
    rx_buf[rx_head] = b;
    rx_head = next;
    return true;
}

void USART2_IRQHandler(void)
{
    /*
     * CRITICAL: read SR before DR on STM32F4.
     * Reading DR without reading SR first can miss error flags.
     */
    uint32_t sr   = USART2->SR;
    uint8_t  data = (uint8_t)(USART2->DR & 0xFF);   /* clears RXNE + errors */

    if (sr & USART_SR_FE) {
        fe_count++;
        /*
         * Do NOT push data on framing error.
         * On a break, data will be 0x00 — useful for LIN / DMX protocols.
         */
        if (data == 0x00) {
            /* signal break to application */
        }
        return;
    }

    if (sr & USART_SR_PE)  { pe_count++;  return; }
    if (sr & USART_SR_ORE) { ore_count++;          } /* overrun: byte lost but we push current */

    if (sr & USART_SR_RXNE) {
        rxbuf_push(data);
    }
}
```

---

## 10. Logging and Diagnostics

Good diagnostics turn a mysterious hardware problem into a solvable one:

```rust
// src/diagnostics.rs
use std::time::{Duration, Instant};

pub struct FramingDiagnostics {
    window_start:      Instant,
    window_duration:   Duration,
    errors_in_window:  u32,
    total_errors:      u64,
    total_bytes:       u64,
    max_burst:         u32,   // largest run of consecutive errors seen
    current_burst:     u32,
}

impl FramingDiagnostics {
    pub fn new(window: Duration) -> Self {
        Self {
            window_start:     Instant::now(),
            window_duration:  window,
            errors_in_window: 0,
            total_errors:     0,
            total_bytes:      0,
            max_burst:        0,
            current_burst:    0,
        }
    }

    pub fn record_byte(&mut self, is_error: bool) {
        self.total_bytes += 1;

        if is_error {
            self.total_errors     += 1;
            self.errors_in_window += 1;
            self.current_burst    += 1;
            if self.current_burst > self.max_burst {
                self.max_burst = self.current_burst;
            }
        } else {
            self.current_burst = 0;
        }

        /* Flush window every `window_duration` */
        if self.window_start.elapsed() >= self.window_duration {
            let ber = if self.total_bytes > 0 {
                self.total_errors as f64 / self.total_bytes as f64
            } else { 0.0 };

            log::info!(
                "UART diagnostics | window errors: {} | total BER: {:.2e} | \
                 max burst: {} | total bytes: {}",
                self.errors_in_window,
                ber,
                self.max_burst,
                self.total_bytes,
            );

            self.errors_in_window = 0;
            self.window_start     = Instant::now();
        }
    }
}
```

---

## 11. Platform-Specific Notes

### Linux / POSIX

- Set `PARMRK` in `termios.c_iflag` to get the `0xFF 0x00 <byte>` escape sequence.
- Without `PARMRK`, framing errors are silently discarded by the tty layer.
- `IGNPAR` will suppress all parity and framing errors — useful for raw streams where you handle
  errors at a higher layer, but be aware you lose the error signal entirely.
- Use `tcflush(fd, TCIFLUSH)` to discard buffered input during resync.

### Windows (Win32)

- Set `fErrorChar = TRUE` and choose a non-data value for `ErrorChar` in `DCB`.
- Alternatively, use `ClearCommError()` with a `COMSTAT` to inspect the `fFraming` field
  after any `ReadFile()` returns a short count.
- `PurgeComm(hPort, PURGE_RXCLEAR)` is the Win32 equivalent of `tcflush`.

### FreeRTOS / RTOS Environments

- Push `RxEvent` structs (data byte or error marker) onto a FreeRTOS queue from the ISR using
  `xQueueSendFromISR()`.
- A dedicated UART task dequeues and processes events without blocking the ISR.
- Use a timer task to implement the resync idle timeout without busy-waiting.

### RS-485 Half-Duplex

- During the transmit phase, disable the receiver to avoid echoing your own transmitted bytes,
  which would otherwise appear as framing errors.
- Enable the receiver only after asserting DE/RE low (transmit-enable de-asserted).

---

## 12. Summary

A **UART framing error** occurs when the receiver samples a **LOW (0)** level at the expected
stop-bit position, indicating that the received bit stream does not match the configured frame
format. It is the foundational error condition in asynchronous serial communication and the
starting point for diagnosing most UART problems.

**Key points to remember:**

- **Always read the status register before the data register.** On many UART peripherals, reading
  the data register clears all associated error flags simultaneously.

- **Baud rate mismatch is the primary cause.** Errors exceeding ±3–5% accumulated over a full
  frame will shift the stop-bit sample into the LOW half of the bit period.

- **A data byte of 0x00 accompanying a framing error usually indicates a break condition.**
  Many protocols (LIN, DMX512, MIDI) use break as an out-of-band signal.

- **Recovery should be layered:** discard the bad byte immediately at the hardware level; apply
  a re-synchronisation strategy (flush + idle wait) after N consecutive errors; use CRC or
  checksums at the protocol level for independent corruption detection.

- **On POSIX systems,** enable `PARMRK` in `termios` to make framing errors visible to
  user-space as the `{0xFF, 0x00, byte}` escape sequence; without it, errors are silently dropped
  by the kernel tty layer.

- **In Rust,** model error events as enum variants (`RxEvent::FramingError`, `RxEvent::Break`)
  and propagate them through typed queues; the compiler's exhaustive match enforcement prevents
  silent error suppression.

- **Collect statistics.** Bit Error Rate, burst length, and error frequency distinguish a
  transient glitch from a systematic configuration problem or a failing cable.

---

*Document: `16_Framing_Errors.md` — Part of the UART Programming Reference Series*