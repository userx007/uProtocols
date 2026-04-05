# 32. Protocol Analyzers — Using Logic Analyzers and Oscilloscopes for UART Debugging

**Content sections:**
- Signal fundamentals — frame structure, bit timing table, voltage level reference
- Logic Analyzers — how they work, popular hardware comparison table, PulseView decoder setup
- Oscilloscopes — setup procedure, trigger strategy, voltage thresholds, RS-232 safety warning
- Decision guide — which tool to use for which symptom

**C/C++ code examples:**
- STM32 HAL bare-metal: walking bits, ASCII test, `0x55` baud calibration, loopback test
- POSIX Linux sender: full-range `0x00–0xFF` pattern with hex-dump logging
- C++ RAII `SerialDebugger` class: transmit, receive, loopback, calibration
- C BER monitor: continuous loopback with running byte-error-rate reporting

**Rust code examples:**
- `serialport` crate: structured test-pattern sender with error handling
- Statistical loopback tester with BER reporting and per-round mismatch logging
- Embedded RP2040 (rp-hal) no\_std loopback with `0x55` calibration burst

**Advanced section:** A full software UART decoder (both C and Rust) that mirrors what a logic analyzer does internally — useful for writing custom protocol decoders.

**Summary:** Distills the key rules — when to use each tool, the `0x55` trick, sample-rate requirements, and the golden rules for reliable UART debugging.

---

## Table of Contents

1. [Introduction](#introduction)
2. [UART Signal Fundamentals for Debugging](#uart-signal-fundamentals-for-debugging)
3. [Logic Analyzers](#logic-analyzers)
4. [Oscilloscopes](#oscilloscopes)
5. [Logic Analyzer vs. Oscilloscope: When to Use Which](#logic-analyzer-vs-oscilloscope-when-to-use-which)
6. [Generating Test Signals in C/C++](#generating-test-signals-in-cc)
7. [Generating Test Signals in Rust](#generating-test-signals-in-rust)
8. [Automated Loopback and Self-Test](#automated-loopback-and-self-test)
9. [Capturing and Decoding UART with Software Tools](#capturing-and-decoding-uart-with-software-tools)
10. [Common UART Faults and How Analyzers Reveal Them](#common-uart-faults-and-how-analyzers-reveal-them)
11. [Advanced: Writing a Software UART Decoder](#advanced-writing-a-software-uart-decoder)
12. [Summary](#summary)

---

## Introduction

Protocol analyzers are indispensable tools for embedded systems engineers working with UART. When a device refuses to communicate, or data arrives corrupted, a protocol analyzer lets you *see* the raw electrical signal and decode it back into bytes and frames — making the invisible visible.

Two primary classes of instrument are used:

- **Logic Analyzers** — capture digital (high/low) signal transitions and decode them into protocol frames (bytes, parity, stop bits). Best for protocol-level debugging.
- **Oscilloscopes** — capture the continuous analog waveform, revealing signal quality, noise, ringing, voltage levels, and timing. Best for signal-integrity and physical-layer debugging.

Modern mixed-signal oscilloscopes (MSOs) combine both capabilities.

---

## UART Signal Fundamentals for Debugging

Before attaching an analyzer, it helps to know what a healthy UART frame looks like on the wire.

### Frame Structure

```
IDLE  START  D0  D1  D2  D3  D4  D5  D6  D7  PARITY  STOP
 ─────┐     ┌───┬───┬───┬───┬───┬───┬───┬───┬────────┐────
      │     │   │   │   │   │   │   │   │   │        │
      └─────┘   └───┘   └───┘   └───┘   └───┘        └────
```

Key facts for your analyzer:

| Parameter       | Typical values                              |
|-----------------|---------------------------------------------|
| Idle state      | Logic HIGH (mark)                           |
| Start bit       | Logic LOW (one bit period)                  |
| Data bits       | LSB first (unless configured otherwise)     |
| Parity bit      | Optional — None / Even / Odd                |
| Stop bits       | 1, 1.5, or 2                                |
| Bit period      | `1 / baud_rate`  (e.g., 104 µs at 9600)    |
| Voltage levels  | TTL: 0/5 V — LVCMOS: 0/3.3 V — RS-232: ±12V |

### Bit Timing Reference

| Baud Rate | Bit Period | 8-bit frame (no parity, 1 stop) |
|-----------|------------|---------------------------------|
| 9 600     | 104.2 µs   | 1.042 ms                        |
| 115 200   | 8.68 µs    | 86.8 µs                         |
| 1 000 000 | 1.00 µs    | 10.0 µs                         |
| 4 000 000 | 250 ns     | 2.50 µs                         |

---

## Logic Analyzers

### How They Work

A logic analyzer continuously samples each probe channel at a fixed clock rate (typically 24 MHz–500 MHz for modern USB analyzers), comparing the voltage against a threshold. It records the stream of 0s and 1s and then post-processes them with protocol decoders.

### Popular Hardware

| Device                      | Channels | Max Sample Rate | Protocol Decode | Notes                   |
|-----------------------------|----------|-----------------|-----------------|-------------------------|
| Saleae Logic 8 / Pro        | 8–16     | 100 MHz / 500 MHz | Built-in (UART, SPI, I²C, ...) | Industry standard |
| Sigrok / fx2lafw (e.g., DSLogic) | 16–32 | 100–400 MHz | PulseView software | Open-source ecosystem |
| STM32 / Raspberry Pi Pico  | GPIO-limited | MCU-speed | DIY via sigrok | Zero-cost option        |
| Analog Discovery 2/3        | 16 digital | 100 MHz        | Waveforms software | Also has oscilloscope   |

### Configuring the UART Decoder (Sigrok / PulseView Example)

Steps in PulseView:

1. Set sample rate ≥ 8× the baud rate (e.g., ≥ 921.6 kHz for 115 200 baud).
2. Add the **UART** protocol decoder.
3. Set **TX** and/or **RX** channels.
4. Configure: baud rate, data bits (8), parity (None/Even/Odd), stop bits (1), bit order (LSB first).
5. Press **Run** and send data.

The decoder overlays each decoded byte directly onto the waveform.

---

## Oscilloscopes

### Why Use a Scope for UART?

A logic analyzer sees *logical* levels; an oscilloscope reveals the *analog* reality:

- **Under/overshoot** on signal edges
- **Ringing** caused by impedance mismatches
- **Ground bounce** from shared power rails
- **Slow rise times** due to capacitive loading or long cables
- **Noise margins** — how close the signal comes to the decision threshold
- **Baud rate accuracy** — measure the actual bit period vs. the nominal value

### Oscilloscope Setup for UART

1. **Probe compensation** — always compensate probes before measuring.
2. **Trigger** — trigger on the falling edge of the start bit (falling edge, suitable threshold).
3. **Time/div** — set to show 2–4 bit periods per division (e.g., 10 µs/div for 115 200 baud).
4. **Decode** — most modern scopes have a UART/serial decode option in their protocol analysis menu.
5. **Measure** — use cursor measurements to verify the bit period against `1/baud_rate`.

### Voltage Thresholds

```
TTL (5 V)       VIH min = 2.0 V,   VIL max = 0.8 V
LVCMOS (3.3 V)  VIH min = 0.7×VCC, VIL max = 0.3×VCC
RS-232          MARK: -3 to -15 V, SPACE: +3 to +15 V
```

> **Caution:** Never connect an RS-232 port directly to an MCU GPIO or a logic analyzer input without a level shifter (e.g., MAX232, SP3232). RS-232 voltages will destroy 3.3 V / 5 V inputs.

---

## Logic Analyzer vs. Oscilloscope: When to Use Which

| Symptom                                  | Best Tool         |
|------------------------------------------|-------------------|
| Garbled / framing errors                 | Logic Analyzer    |
| Wrong baud rate (framing errors)         | Oscilloscope      |
| Data arrives but some bytes wrong        | Logic Analyzer    |
| Intermittent corruption on long cables   | Oscilloscope      |
| Signal not reaching HIGH or LOW reliably | Oscilloscope      |
| Parity errors                            | Logic Analyzer    |
| Noise / ringing on edges                 | Oscilloscope      |
| Protocol-level timing (inter-byte gaps)  | Logic Analyzer    |
| Mixed 3.3 V / 5 V level mismatch         | Oscilloscope      |

---

## Generating Test Signals in C/C++

To use a protocol analyzer effectively, you need a known, controlled signal to observe. The following examples show how to generate predictable UART output for analyzer capture on common embedded platforms.

### 1. Bare-Metal UART Init and Loopback Test (ARM Cortex-M / STM32, HAL)

```c
/* uart_debug_test.c
 * Target: STM32 with HAL (easily adapted to other MCUs)
 * Purpose: Send a known pattern for logic analyzer / oscilloscope capture
 */

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

UART_HandleTypeDef huart2;

/* ------------------------------------------------------------------ */
/* UART Initialisation                                                  */
/* ------------------------------------------------------------------ */
static void UART2_Init(uint32_t baud_rate)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = baud_rate;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK) {
        /* Initialization error — check clock config and GPIO alternate functions */
        Error_Handler();
    }
}

/* ------------------------------------------------------------------ */
/* Test Pattern 1: Walking-bit pattern                                  */
/* Useful for verifying bit-order on the analyzer                       */
/* ------------------------------------------------------------------ */
void uart_send_walking_bits(void)
{
    uint8_t pattern[] = {
        0x01, 0x02, 0x04, 0x08,   /* individual bits 0-3   */
        0x10, 0x20, 0x40, 0x80,   /* individual bits 4-7   */
        0xAA, 0x55,               /* alternating bits      */
        0xFF, 0x00                /* all-ones, all-zeros   */
    };

    HAL_UART_Transmit(&huart2, pattern, sizeof(pattern), HAL_MAX_DELAY);
}

/* ------------------------------------------------------------------ */
/* Test Pattern 2: ASCII string                                         */
/* Easy to decode visually in PulseView / Logic                         */
/* ------------------------------------------------------------------ */
void uart_send_ascii_test(void)
{
    const char *msg = "UART_TEST_PATTERN_0123456789\r\n";
    HAL_UART_Transmit(&huart2,
                      (uint8_t *)msg,
                      strlen(msg),
                      HAL_MAX_DELAY);
}

/* ------------------------------------------------------------------ */
/* Test Pattern 3: Baud-rate stress test                                */
/* Send a continuous stream to measure actual baud rate on the scope    */
/* 0x55 = 01010101b — toggles the line every half-bit: ideal for timing */
/* ------------------------------------------------------------------ */
void uart_send_baud_stress(uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        uint8_t byte = 0x55;
        HAL_UART_Transmit(&huart2, &byte, 1, HAL_MAX_DELAY);
    }
}

/* ------------------------------------------------------------------ */
/* Test Pattern 4: Loopback echo (connect TX to RX externally)          */
/* Transmit, receive back, compare — reports pass/fail via another UART */
/* ------------------------------------------------------------------ */
HAL_StatusTypeDef uart_loopback_test(void)
{
    uint8_t tx_buf[16];
    uint8_t rx_buf[16];
    HAL_StatusTypeDef status;

    /* Fill TX buffer with known values */
    for (int i = 0; i < 16; i++) tx_buf[i] = (uint8_t)(i * 17);

    /* Transmit */
    status = HAL_UART_Transmit(&huart2, tx_buf, sizeof(tx_buf), 100);
    if (status != HAL_OK) return status;

    /* Receive */
    status = HAL_UART_Receive(&huart2, rx_buf, sizeof(rx_buf), 200);
    if (status != HAL_OK) return status;

    /* Compare */
    if (memcmp(tx_buf, rx_buf, sizeof(tx_buf)) != 0)
        return HAL_ERROR;

    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/* Main entry point                                                     */
/* ------------------------------------------------------------------ */
int main(void)
{
    HAL_Init();
    SystemClock_Config();   /* configure PLL/HSE as appropriate */

    /* Test at multiple baud rates to expose clock-accuracy issues */
    uint32_t baud_rates[] = { 9600, 115200, 460800, 921600 };

    for (int b = 0; b < 4; b++) {
        UART2_Init(baud_rates[b]);

        uart_send_walking_bits();
        HAL_Delay(10);

        uart_send_ascii_test();
        HAL_Delay(10);

        uart_send_baud_stress(64);
        HAL_Delay(100);
    }

    while (1) {
        /* Continuous loopback mode for long-term analyzer capture */
        uart_send_ascii_test();
        HAL_Delay(50);
    }
}
```

### 2. Linux / POSIX: Serial Port Debug Sender

```c
/* posix_uart_debug.c
 * Compile: gcc -o posix_uart_debug posix_uart_debug.c
 * Run: ./posix_uart_debug /dev/ttyUSB0 115200
 *
 * Opens a serial port and sends patterns that a logic analyzer
 * attached to the physical TX pin can capture.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Utility: map integer baud rate to POSIX B-constant                   */
/* ------------------------------------------------------------------ */
static speed_t baud_to_speed(int baud)
{
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default:
            fprintf(stderr, "Unsupported baud rate %d\n", baud);
            exit(EXIT_FAILURE);
    }
}

/* ------------------------------------------------------------------ */
/* Open and configure a serial port                                     */
/* ------------------------------------------------------------------ */
static int open_serial(const char *device, int baud)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) < 0) { perror("tcgetattr"); close(fd); return -1; }

    speed_t speed = baud_to_speed(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    /* Raw mode: 8N1, no flow control */
    cfmakeraw(&tty);
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);  /* no parity, 1 stop, no HW flow */
    tty.c_cflag |= CS8 | CREAD | CLOCAL;           /* 8 data bits                   */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;  /* 100 ms read timeout */

    if (tcsetattr(fd, TCSANOW, &tty) < 0) { perror("tcsetattr"); close(fd); return -1; }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* ------------------------------------------------------------------ */
/* Send a buffer and print hex dump of what was sent                    */
/* ------------------------------------------------------------------ */
static void send_and_dump(int fd, const uint8_t *data, size_t len, const char *label)
{
    printf("[TX] %s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\n");

    ssize_t written = write(fd, data, len);
    if (written != (ssize_t)len) {
        fprintf(stderr, "Short write: wrote %zd of %zu bytes\n", written, len);
    }
    tcdrain(fd);  /* wait for hardware TX FIFO to drain */
}

/* ------------------------------------------------------------------ */
/* Main: run a battery of analyzer-friendly test patterns               */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device> <baud_rate>\n", argv[0]);
        fprintf(stderr, "Example: %s /dev/ttyUSB0 115200\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *device = argv[1];
    int baud = atoi(argv[2]);

    int fd = open_serial(device, baud);
    if (fd < 0) return EXIT_FAILURE;

    printf("Opened %s at %d baud. Sending test patterns...\n\n", device, baud);

    /* Pattern 1: 0x55 — alternating bits, ideal for baud-rate measurement on scope */
    uint8_t alt_bits[32];
    memset(alt_bits, 0x55, sizeof(alt_bits));
    send_and_dump(fd, alt_bits, sizeof(alt_bits), "Alternating bits (0x55)");
    usleep(50000);

    /* Pattern 2: Walking bits */
    uint8_t walking[] = { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80 };
    send_and_dump(fd, walking, sizeof(walking), "Walking bits");
    usleep(50000);

    /* Pattern 3: Known ASCII string */
    const char *ascii = "Hello, Logic Analyzer! ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789\r\n";
    send_and_dump(fd, (const uint8_t *)ascii, strlen(ascii), "ASCII string");
    usleep(100000);

    /* Pattern 4: All byte values 0x00–0xFF */
    uint8_t full_range[256];
    for (int i = 0; i < 256; i++) full_range[i] = (uint8_t)i;
    send_and_dump(fd, full_range, sizeof(full_range), "Full byte range 0x00-0xFF");
    usleep(100000);

    printf("\nTest patterns sent. Attach your analyzer and re-run to capture.\n");

    close(fd);
    return EXIT_SUCCESS;
}
```

### 3. C++ RAII Serial Port Wrapper with Diagnostics

```cpp
// SerialDebugger.hpp
// C++17 RAII wrapper around a POSIX serial port.
// Adds per-byte TX/RX logging suitable for correlating with analyzer captures.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

class SerialDebugger {
public:
    // ----------------------------------------------------------------
    // Constructor: open and configure the port
    // ----------------------------------------------------------------
    SerialDebugger(const std::string& device, int baud, bool verbose = true)
        : device_(device), verbose_(verbose)
    {
        fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) throw std::runtime_error("Cannot open " + device);

        struct termios tty{};
        if (::tcgetattr(fd_, &tty) < 0) throw std::runtime_error("tcgetattr failed");

        speed_t spd = toSpeed(baud);
        ::cfsetispeed(&tty, spd);
        ::cfsetospeed(&tty, spd);
        ::cfmakeraw(&tty);
        tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
        tty.c_cflag |= CS8 | CREAD | CLOCAL;
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 10;  // 1 s timeout

        if (::tcsetattr(fd_, TCSANOW, &tty) < 0) throw std::runtime_error("tcsetattr failed");
        ::tcflush(fd_, TCIOFLUSH);

        log("Opened " + device + " @ " + std::to_string(baud) + " baud");
    }

    ~SerialDebugger() { if (fd_ >= 0) ::close(fd_); }

    // Non-copyable, movable
    SerialDebugger(const SerialDebugger&) = delete;
    SerialDebugger& operator=(const SerialDebugger&) = delete;

    // ----------------------------------------------------------------
    // Transmit: write bytes and log them
    // ----------------------------------------------------------------
    void transmit(const std::vector<uint8_t>& data)
    {
        if (verbose_) logHex("TX", data);
        ssize_t n = ::write(fd_, data.data(), data.size());
        ::tcdrain(fd_);
        if (n != static_cast<ssize_t>(data.size()))
            throw std::runtime_error("Short write");
    }

    void transmit(const std::string& s)
    {
        transmit(std::vector<uint8_t>(s.begin(), s.end()));
    }

    // ----------------------------------------------------------------
    // Receive: read up to `max_bytes` within `timeout_ms`
    // ----------------------------------------------------------------
    std::vector<uint8_t> receive(size_t max_bytes, int timeout_ms = 1000)
    {
        using Clock = std::chrono::steady_clock;
        auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

        std::vector<uint8_t> buf;
        buf.reserve(max_bytes);

        while (buf.size() < max_bytes && Clock::now() < deadline) {
            uint8_t byte;
            ssize_t n = ::read(fd_, &byte, 1);
            if (n == 1) buf.push_back(byte);
        }

        if (verbose_) logHex("RX", buf);
        return buf;
    }

    // ----------------------------------------------------------------
    // Loopback test: TX, then RX, then compare
    // ----------------------------------------------------------------
    bool loopbackTest(const std::vector<uint8_t>& pattern, int timeout_ms = 500)
    {
        ::tcflush(fd_, TCIOFLUSH);
        transmit(pattern);
        auto echo = receive(pattern.size(), timeout_ms);
        bool ok = (echo == pattern);
        log(std::string("Loopback test: ") + (ok ? "PASS" : "FAIL"));
        return ok;
    }

    // ----------------------------------------------------------------
    // Send the classic 0x55 baud-rate calibration burst
    // ----------------------------------------------------------------
    void sendBaudCalibration(size_t count = 64)
    {
        transmit(std::vector<uint8_t>(count, 0x55));
    }

private:
    int         fd_      = -1;
    std::string device_;
    bool        verbose_;

    static speed_t toSpeed(int baud)
    {
        switch (baud) {
            case 9600:   return B9600;
            case 115200: return B115200;
            case 460800: return B460800;
            case 921600: return B921600;
            default: throw std::invalid_argument("Unsupported baud: " + std::to_string(baud));
        }
    }

    void log(const std::string& msg)
    {
        if (verbose_) std::cout << "[SerialDebugger] " << msg << "\n";
    }

    void logHex(const std::string& dir, const std::vector<uint8_t>& data)
    {
        std::ostringstream oss;
        oss << "[" << dir << " " << data.size() << "B] ";
        for (uint8_t b : data)
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)b << ' ';
        log(oss.str());
    }
};

// ----------------------------------------------------------------
// Usage example (main.cpp):
// ----------------------------------------------------------------
/*
#include "SerialDebugger.hpp"

int main() {
    try {
        SerialDebugger dbg("/dev/ttyUSB0", 115200, true);

        // 1. Baud-rate calibration burst — measure with oscilloscope
        dbg.sendBaudCalibration(64);

        // 2. ASCII string — easy to read in logic analyzer decoder
        dbg.transmit("UART Protocol Analyzer Test\r\n");

        // 3. Loopback test (requires TX→RX loopback wire)
        std::vector<uint8_t> pattern;
        for (int i = 0; i < 32; i++) pattern.push_back(static_cast<uint8_t>(i * 8));
        bool ok = dbg.loopbackTest(pattern);

        return ok ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}
*/
```

---

## Generating Test Signals in Rust

### 1. Basic UART Debug Sender (serialport crate)

```rust
// Cargo.toml:
// [dependencies]
// serialport = "4"
// anyhow = "1"
// clap = { version = "4", features = ["derive"] }

use anyhow::{Context, Result};
use serialport::{DataBits, FlowControl, Parity, SerialPort, StopBits};
use std::io::Write;
use std::time::Duration;

/// Send a hex dump to stdout for correlation with analyzer captures.
fn log_hex(direction: &str, data: &[u8]) {
    print!("[{}  {}B] ", direction, data.len());
    for byte in data {
        print!("{:02X} ", byte);
    }
    println!();
}

/// Open and configure a serial port for UART debugging.
fn open_port(device: &str, baud: u32) -> Result<Box<dyn SerialPort>> {
    serialport::new(device, baud)
        .data_bits(DataBits::Eight)
        .parity(Parity::None)
        .stop_bits(StopBits::One)
        .flow_control(FlowControl::None)
        .timeout(Duration::from_millis(500))
        .open()
        .with_context(|| format!("Failed to open {device} at {baud} baud"))
}

/// Send a pattern and log it.
fn send(port: &mut Box<dyn SerialPort>, data: &[u8], label: &str) -> Result<()> {
    println!("Sending: {}", label);
    log_hex("TX", data);
    port.write_all(data)?;
    port.flush()?;
    Ok(())
}

fn main() -> Result<()> {
    // Parse device and baud from CLI args (simple version)
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        eprintln!("Usage: {} <device> <baud>", args[0]);
        eprintln!("       {} /dev/ttyUSB0 115200", args[0]);
        std::process::exit(1);
    }
    let device = &args[1];
    let baud: u32 = args[2].parse().context("Invalid baud rate")?;

    let mut port = open_port(device, baud)?;
    println!("Opened {device} @ {baud} baud\n");

    // --- Pattern 1: Alternating bits 0x55 (ideal for oscilloscope timing) ---
    let alt_bits = vec![0x55u8; 32];
    send(&mut port, &alt_bits, "Alternating bits (0x55 × 32)")?;
    std::thread::sleep(Duration::from_millis(50));

    // --- Pattern 2: Walking bits ---
    let walking: Vec<u8> = (0..8).map(|i| 1u8 << i).collect();
    send(&mut port, &walking, "Walking bits (bit 0→7)")?;
    std::thread::sleep(Duration::from_millis(50));

    // --- Pattern 3: ASCII string ---
    let ascii = b"Hello from Rust! UART Analyzer Test 0123456789\r\n";
    send(&mut port, ascii, "ASCII string")?;
    std::thread::sleep(Duration::from_millis(100));

    // --- Pattern 4: Complete byte range 0x00..=0xFF ---
    let full_range: Vec<u8> = (0u8..=255).collect();
    send(&mut port, &full_range, "Full byte range 0x00–0xFF")?;
    std::thread::sleep(Duration::from_millis(100));

    println!("\nAll patterns sent. Inspect your analyzer capture.");
    Ok(())
}
```

### 2. Rust UART Loopback Test with Statistics

```rust
// uart_loopback.rs
// Performs a statistical loopback test: transmit N rounds of random data,
// compare with received echo, report byte error rate (BER).
//
// Useful for long-term reliability testing while the logic analyzer is
// capturing for intermittent-fault analysis.

use anyhow::Result;
use serialport::SerialPort;
use std::io::{Read, Write};
use std::time::{Duration, Instant};

pub struct LoopbackStats {
    pub rounds:        usize,
    pub bytes_sent:    usize,
    pub bytes_correct: usize,
    pub bytes_error:   usize,
    pub duration:      Duration,
}

impl LoopbackStats {
    pub fn ber(&self) -> f64 {
        if self.bytes_sent == 0 { return 0.0; }
        self.bytes_error as f64 / self.bytes_sent as f64
    }

    pub fn print_report(&self) {
        println!("=== Loopback Test Report ===");
        println!("  Rounds       : {}", self.rounds);
        println!("  Bytes sent   : {}", self.bytes_sent);
        println!("  Bytes correct: {}", self.bytes_correct);
        println!("  Bytes error  : {}", self.bytes_error);
        println!("  BER          : {:.2e}", self.ber());
        println!("  Duration     : {:.2}s", self.duration.as_secs_f64());
        println!("  Throughput   : {:.0} B/s",
            self.bytes_sent as f64 / self.duration.as_secs_f64());
    }
}

/// Run `rounds` loopback iterations of `payload_size` bytes each.
/// Requires TX and RX pins physically connected (loopback cable or jumper).
pub fn run_loopback_test(
    port:         &mut Box<dyn SerialPort>,
    rounds:       usize,
    payload_size: usize,
) -> Result<LoopbackStats> {
    let mut stats = LoopbackStats {
        rounds,
        bytes_sent:    0,
        bytes_correct: 0,
        bytes_error:   0,
        duration:      Duration::ZERO,
    };

    // Flush any stale data before starting
    // (give hardware FIFO time to drain)
    std::thread::sleep(Duration::from_millis(20));

    let start = Instant::now();

    for round in 0..rounds {
        // Generate a deterministic but non-trivial test vector:
        // each byte = (round * payload_size + i) mod 256
        let tx: Vec<u8> = (0..payload_size)
            .map(|i| ((round * payload_size + i) & 0xFF) as u8)
            .collect();

        // Transmit
        port.write_all(&tx)?;
        port.flush()?;
        stats.bytes_sent += payload_size;

        // Receive with timeout
        let mut rx = vec![0u8; payload_size];
        let mut received = 0;
        let deadline = Instant::now() + Duration::from_millis(500);

        while received < payload_size && Instant::now() < deadline {
            match port.read(&mut rx[received..]) {
                Ok(n)  => received += n,
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => break,
                Err(e) => return Err(e.into()),
            }
        }

        // Compare byte-by-byte and accumulate error statistics
        for i in 0..received.min(payload_size) {
            if tx[i] == rx[i] {
                stats.bytes_correct += 1;
            } else {
                stats.bytes_error += 1;
                // Verbose: print the first mismatch in a round for analyzer correlation
                if stats.bytes_error <= 5 {
                    eprintln!(
                        "  Mismatch round={round} byte={i}: TX={:02X} RX={:02X}",
                        tx[i], rx[i]
                    );
                }
            }
        }

        // Count unread bytes as errors
        if received < payload_size {
            stats.bytes_error += payload_size - received;
        }
    }

    stats.duration = start.elapsed();
    Ok(stats)
}

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        eprintln!("Usage: {} <device> <baud> [rounds] [payload_bytes]", args[0]);
        std::process::exit(1);
    }

    let device  = &args[1];
    let baud: u32 = args[2].parse()?;
    let rounds: usize = args.get(3).and_then(|s| s.parse().ok()).unwrap_or(100);
    let payload: usize = args.get(4).and_then(|s| s.parse().ok()).unwrap_or(64);

    let mut port = serialport::new(device.as_str(), baud)
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .timeout(Duration::from_millis(200))
        .open()?;

    println!("UART Loopback Test: {device} @ {baud} baud, {rounds} rounds × {payload}B");

    let stats = run_loopback_test(&mut port, rounds, payload)?;
    stats.print_report();

    // Fail if BER exceeds 0.1%
    if stats.ber() > 0.001 {
        std::process::exit(1);
    }
    Ok(())
}
```

### 3. Embedded Rust: Loopback on RP2040 (rp-hal)

```rust
// Embedded Rust loopback test for Raspberry Pi Pico (rp2040)
// Transmits patterns on UART0 TX (GPIO0) — attach logic analyzer to this pin.
// Receives on UART0 RX (GPIO1) — connect TX→RX with a jumper for loopback.
//
// Cargo.toml dependencies:
//   rp-pico = "0.9"
//   rp2040-hal = "0.10"
//   embedded-hal = "0.2"
//   panic-halt = "0.2"
//   cortex-m-rt = "0.7"

#![no_std]
#![no_main]

use panic_halt as _;

use rp_pico::entry;
use rp_pico::hal::{
    self,
    clocks::init_clocks_and_plls,
    pac,
    uart::{DataBits, StopBits, UartConfig, UartPeripheral},
    watchdog::Watchdog,
    Sio,
};

use embedded_hal::serial::{Read, Write};
use fugit::RateExtU32;

#[entry]
fn main() -> ! {
    let mut pac = pac::Peripherals::take().unwrap();
    let mut watchdog = Watchdog::new(pac.WATCHDOG);
    let sio = Sio::new(pac.SIO);

    // Configure system clock to 125 MHz
    let clocks = init_clocks_and_plls(
        rp_pico::XOSC_CRYSTAL_FREQ,
        pac.XOSC,
        pac.CLOCKS,
        pac.PLL_SYS,
        pac.PLL_USB,
        &mut pac.RESETS,
        &mut watchdog,
    )
    .ok()
    .unwrap();

    // Configure GPIO pins
    let pins = rp_pico::Pins::new(
        pac.IO_BANK0,
        pac.PADS_BANK0,
        sio.gpio_bank0,
        &mut pac.RESETS,
    );

    // UART0: TX=GPIO0, RX=GPIO1 — attach logic analyzer probe to GPIO0
    let uart_pins = (
        pins.gpio0.into_mode::<hal::gpio::FunctionUart>(),
        pins.gpio1.into_mode::<hal::gpio::FunctionUart>(),
    );

    let uart = UartPeripheral::new(pac.UART0, uart_pins, &mut pac.RESETS)
        .enable(
            UartConfig::new(115_200u32.Hz(), DataBits::Eight, None, StopBits::One),
            clocks.peripheral_clock.freq(),
        )
        .unwrap();

    // Pattern 1: 0x55 alternating bits — measure bit period on oscilloscope
    for _ in 0..32 {
        nb::block!(uart.write(0x55)).ok();
    }

    // Small delay via busy-loop (no systick configured here)
    cortex_m::asm::delay(1_000_000);

    // Pattern 2: Walking bits
    let walking: [u8; 8] = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80];
    for &byte in &walking {
        nb::block!(uart.write(byte)).ok();
    }

    cortex_m::asm::delay(1_000_000);

    // Pattern 3: Loopback — TX pin wired to RX pin externally
    let test_bytes: [u8; 8] = [0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE];
    let mut pass = true;

    for &byte in &test_bytes {
        nb::block!(uart.write(byte)).ok();
        if let Ok(received) = nb::block!(uart.read()) {
            if received != byte {
                pass = false;
            }
        } else {
            pass = false;
        }
    }

    // Indicate result: repeated 0xFF = PASS, repeated 0x00 = FAIL
    let indicator = if pass { 0xFF } else { 0x00 };
    loop {
        nb::block!(uart.write(indicator)).ok();
        cortex_m::asm::delay(12_000_000);  // ~100 ms at 125 MHz
    }
}
```

---

## Automated Loopback and Self-Test

### C: Continuous BER Monitor for Long-Term Capture

```c
/* ber_monitor.c
 * Continuously send/receive on a loopback port and print running BER.
 * Attach a logic analyzer to capture intermittent errors over hours.
 * Compile: gcc -o ber_monitor ber_monitor.c
 * Run:     ./ber_monitor /dev/ttyUSB0 115200
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>

static int     g_fd;
static uint64_t g_bytes_ok = 0;
static uint64_t g_bytes_err = 0;

static int open_port(const char *dev, int baud)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return -1; }

    struct termios t;
    tcgetattr(fd, &t);
    cfmakeraw(&t);
    speed_t s = (baud == 115200) ? B115200 : B9600;
    cfsetispeed(&t, s);
    cfsetospeed(&t, s);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 5;
    tcsetattr(fd, TCSANOW, &t);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static void print_ber_report(void)
{
    uint64_t total = g_bytes_ok + g_bytes_err;
    double ber = (total > 0) ? (double)g_bytes_err / total : 0.0;
    printf("\r  Total: %8llu  OK: %8llu  ERR: %6llu  BER: %.2e   ",
           (unsigned long long)total,
           (unsigned long long)g_bytes_ok,
           (unsigned long long)g_bytes_err,
           ber);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "Usage: %s <dev> <baud>\n", argv[0]); return 1; }

    g_fd = open_port(argv[1], atoi(argv[2]));
    if (g_fd < 0) return 1;

    printf("BER Monitor on %s @ %s baud. Ctrl+C to stop.\n", argv[1], argv[2]);

    /* Linear congruential sequence for pseudo-random test data */
    uint8_t state = 0xA5;

    while (1) {
        /* TX one byte */
        uint8_t tx = state;
        write(g_fd, &tx, 1);

        /* RX one byte (loopback) */
        uint8_t rx = 0;
        int n = read(g_fd, &rx, 1);

        if (n == 1) {
            if (rx == tx) g_bytes_ok++;
            else          g_bytes_err++;
        } else {
            g_bytes_err++;
        }

        /* Advance LCG */
        state = (uint8_t)(state * 53 + 113);

        /* Print report every 256 bytes */
        if ((g_bytes_ok + g_bytes_err) % 256 == 0)
            print_ber_report();
    }
}
```

---

## Capturing and Decoding UART with Software Tools

### Sigrok / PulseView CLI Example

```bash
# Capture 1 second of data at 1 MHz from a fx2lafw-based analyzer
# and run the UART decoder to extract bytes.

sigrok-cli \
  --driver fx2lafw \
  --config samplerate=1m \
  --time 1000 \
  --channels D0 \
  --protocol-decoder uart:tx=D0:baudrate=115200 \
  --output-format ascii

# Export to CSV for post-processing
sigrok-cli \
  --driver fx2lafw \
  --config samplerate=4m \
  --time 500 \
  --channels D0,D1 \
  --protocol-decoder uart:tx=D0:rx=D1:baudrate=115200 \
  --output-format csv \
  > capture_$(date +%Y%m%d_%H%M%S).csv
```

### Processing a Sigrok CSV Capture in Python

```python
#!/usr/bin/env python3
"""
parse_uart_capture.py
Read a sigrok CSV export and display a formatted byte stream
with timestamps, useful for correlating with oscilloscope traces.
"""

import csv
import sys
from datetime import timedelta

def parse_capture(path: str) -> None:
    with open(path, newline='') as f:
        reader = csv.reader(f)
        for row in reader:
            if not row or row[0].startswith(';'):
                continue  # skip comments
            # sigrok UART CSV columns: timestamp_s, decoded_value, type
            try:
                ts   = float(row[0])
                val  = row[1].strip()
                kind = row[2].strip() if len(row) > 2 else ''
            except (ValueError, IndexError):
                continue

            if kind == 'data':
                byte_val = int(val, 16) if val.startswith('0x') else int(val)
                char = chr(byte_val) if 0x20 <= byte_val < 0x7F else '.'
                print(f"  t={ts*1000:.3f}ms  0x{byte_val:02X}  '{char}'")
            elif kind == 'framing-error':
                print(f"  t={ts*1000:.3f}ms  *** FRAMING ERROR ***")
            elif kind == 'parity-error':
                print(f"  t={ts*1000:.3f}ms  *** PARITY ERROR ***")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <capture.csv>")
        sys.exit(1)
    parse_capture(sys.argv[1])
```

---

## Common UART Faults and How Analyzers Reveal Them

### Fault 1: Baud Rate Mismatch

**Symptom:** Every byte received is wrong; logic analyzer shows framing errors on every frame.

**Logic Analyzer Signature:** Start bit detected, but data bits sampled at wrong positions — decoded bytes are garbage.

**Oscilloscope Signature:** Measure the actual bit period. If `measured_period × baud_rate ≠ 1.0`, the clocks are mismatched.

**Fix:** Verify MCU clock configuration. Check PLL settings and UART divider register value.

```c
/* Diagnostic: print the actual measured baud rate given a known bit period */
void diagnose_baud_rate(uint32_t measured_bit_period_ns, uint32_t configured_baud)
{
    uint32_t actual_baud = 1000000000UL / measured_bit_period_ns;
    float error_pct = 100.0f * (float)(actual_baud - configured_baud) / configured_baud;
    printf("Configured: %lu baud\n", (unsigned long)configured_baud);
    printf("Actual:     %lu baud\n", (unsigned long)actual_baud);
    printf("Error:      %.2f%%\n",   error_pct);
    if (error_pct > 2.0f || error_pct < -2.0f)
        printf("WARNING: Error > 2%% — likely to cause framing errors!\n");
}
```

### Fault 2: Framing Error (Wrong Stop Bit)

**Symptom:** UART receiver sets FE (Framing Error) flag; occasional garbage bytes.

**Logic Analyzer Signature:** Stop bit sampled as LOW instead of HIGH.

**Cause:** Usually a baud rate mismatch of ~5–10%, or wrong number of stop bits.

### Fault 3: Parity Error

**Symptom:** Receiver sets PE (Parity Error) flag; data is correct but parity bit is wrong.

**Logic Analyzer Signature:** PulseView UART decoder annotates frames with "parity error".

**Cause:** Transmitter and receiver configured with different parity (None vs. Even, etc.).

```c
/* Verify parity configuration matches between TX and RX */
typedef enum { PARITY_NONE = 0, PARITY_EVEN, PARITY_ODD } parity_t;

static uint8_t compute_even_parity(uint8_t data)
{
    uint8_t p = 0;
    while (data) { p ^= (data & 1); data >>= 1; }
    return p;  /* 0 = even number of 1-bits */
}

void parity_self_check(uint8_t byte, parity_t parity)
{
    uint8_t p = compute_even_parity(byte);
    printf("Byte 0x%02X: even_parity_bit=%u", byte, p);
    if (parity == PARITY_ODD) printf("  (ODD: bit should be %u)", 1 - p);
    printf("\n");
}
```

### Fault 4: Signal Integrity — Slow Rise Time

**Symptom:** Intermittent errors that increase with cable length or baud rate.

**Oscilloscope Signature:** Edges look like RC charging curves rather than sharp transitions. The signal may not fully reach VIH before being sampled.

**Cause:** Excessive bus capacitance (long cables, parasitic capacitance) combined with high source impedance.

**Fix:** Add a stronger pull-up/pull-down, reduce cable length, or use RS-485 differential signaling for distances > 1 m.

### Fault 5: Ground Offset / Floating Ground

**Symptom:** All bytes garbled; logic analyzer sees what appears to be inverted data.

**Oscilloscope Signature:** Signal mid-point is offset from 0 V; may even appear inverted.

**Cause:** Devices powered from separate supplies with no common ground connection.

**Fix:** Connect GND of all devices together. Use only a single ground wire — not signal earth.

---

## Advanced: Writing a Software UART Decoder

Understanding how decoders work helps you interpret their output (and write custom decoders for proprietary protocols).

### C: Minimal UART Decoder for Captured Bit Stream

```c
/* soft_uart_decoder.c
 * Decodes a bit stream (array of 0s and 1s sampled at `sample_rate` Hz)
 * into UART bytes at `baud_rate`.
 *
 * This mirrors what a logic analyzer's protocol decoder does internally.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    uint8_t  byte;
    bool     framing_error;
    bool     parity_error;
    uint32_t bit_position;   /* sample index where start bit was detected */
} decoded_frame_t;

/* ------------------------------------------------------------------ */
/* Decode a bit stream into UART frames                                 */
/* bits[]      : sampled bit stream (0 or 1)                           */
/* num_samples : length of bits[]                                       */
/* sample_rate : samples per second                                     */
/* baud_rate   : UART baud rate                                         */
/* data_bits   : 7 or 8                                                 */
/* parity      : 0=none, 1=odd, 2=even                                  */
/* stop_bits   : 1 or 2                                                 */
/* frames_out  : output array (caller allocates)                        */
/* max_frames  : capacity of frames_out                                 */
/* returns     : number of decoded frames                               */
/* ------------------------------------------------------------------ */
int decode_uart(
    const uint8_t *bits,
    uint32_t       num_samples,
    uint32_t       sample_rate,
    uint32_t       baud_rate,
    int            data_bits,
    int            parity,
    int            stop_bits,
    decoded_frame_t *frames_out,
    int            max_frames)
{
    /* samples per bit — use rounding for better accuracy */
    uint32_t spb = (sample_rate + baud_rate / 2) / baud_rate;
    /* sample in the middle of each bit period */
    uint32_t half_spb = spb / 2;

    int  frame_count = 0;
    uint32_t i = 0;

    while (i < num_samples && frame_count < max_frames) {
        /* Wait for idle → start bit transition (HIGH→LOW) */
        if (i > 0 && bits[i-1] == 1 && bits[i] == 0) {
            decoded_frame_t f = {0};
            f.bit_position = i;

            /* Skip to middle of start bit and verify it is LOW */
            uint32_t pos = i + half_spb;
            if (pos >= num_samples || bits[pos] != 0) { i++; continue; }

            /* Decode data bits */
            uint8_t value = 0;
            for (int b = 0; b < data_bits; b++) {
                pos += spb;
                if (pos >= num_samples) goto done;
                if (bits[pos]) value |= (1 << b);
            }
            f.byte = value;

            /* Check parity bit (if enabled) */
            if (parity != 0) {
                pos += spb;
                if (pos >= num_samples) goto done;
                uint8_t rx_parity = bits[pos];
                uint8_t calc_parity = 0;
                uint8_t tmp = value;
                while (tmp) { calc_parity ^= (tmp & 1); tmp >>= 1; }
                if (parity == 2) { /* even */ f.parity_error = (rx_parity != calc_parity); }
                else             { /* odd  */ f.parity_error = (rx_parity == calc_parity); }
            }

            /* Check stop bit */
            pos += spb;
            if (pos >= num_samples) goto done;
            f.framing_error = (bits[pos] != 1);

            frames_out[frame_count++] = f;

            /* Advance past this frame */
            i = pos + half_spb;
            continue;
        }
        i++;
    }

done:
    return frame_count;
}

/* ------------------------------------------------------------------ */
/* Example: decode a synthetic bit stream                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    /* Synthesise: "AB" at 9600 baud, 1 MHz sample rate, 8N1 */
    uint32_t sample_rate = 1000000;
    uint32_t baud_rate   = 9600;
    uint32_t spb = sample_rate / baud_rate;   /* ~104 samples per bit */
    uint32_t total_bits_per_frame = 10;       /* 1 start + 8 data + 1 stop */
    uint32_t num_samples = spb * total_bits_per_frame * 3 + spb * 5; /* "AB" + guard */

    uint8_t *bits = calloc(num_samples, 1);
    if (!bits) return 1;

    /* Idle HIGH */
    for (uint32_t k = 0; k < num_samples; k++) bits[k] = 1;

    /* Encode byte `ch` starting at sample offset `off` */
    #define ENCODE(off, ch) do { \
        uint32_t _o = (off); uint8_t _c = (ch); \
        for (uint32_t _k = 0; _k < spb; _k++) bits[_o + _k] = 0; /* start */ \
        for (int _b = 0; _b < 8; _b++) \
            for (uint32_t _k = 0; _k < spb; _k++) \
                bits[_o + spb*(1+_b) + _k] = (_c >> _b) & 1; \
        for (uint32_t _k = 0; _k < spb; _k++) bits[_o + spb*9 + _k] = 1; /* stop */ \
    } while(0)

    ENCODE(spb * 2,  'A');   /* 'A' = 0x41 */
    ENCODE(spb * 13, 'B');   /* 'B' = 0x42 */

    decoded_frame_t frames[16];
    int n = decode_uart(bits, num_samples, sample_rate, baud_rate,
                        8, 0, 1, frames, 16);

    printf("Decoded %d frame(s):\n", n);
    for (int j = 0; j < n; j++) {
        printf("  [%u] 0x%02X '%c'  framing=%d parity=%d\n",
               frames[j].bit_position,
               frames[j].byte,
               (frames[j].byte >= 0x20 && frames[j].byte < 0x7F) ? frames[j].byte : '.',
               frames[j].framing_error,
               frames[j].parity_error);
    }

    free(bits);
    return 0;
}
```

### Rust: Software UART Decoder

```rust
// soft_uart_decoder.rs — mirrors the C version above in idiomatic Rust

#[derive(Debug)]
pub struct DecodedFrame {
    pub byte:          u8,
    pub framing_error: bool,
    pub parity_error:  bool,
    pub bit_position:  usize,  // sample index of the start bit
}

/// Decode a binary bit-stream into UART frames.
///
/// # Arguments
/// * `bits`        — sampled bit stream (0 = LOW, 1 = HIGH)
/// * `sample_rate` — samples per second
/// * `baud_rate`   — UART baud rate
/// * `data_bits`   — 7 or 8
/// * `parity`      — 0 = none, 1 = odd, 2 = even
pub fn decode_uart(
    bits:        &[u8],
    sample_rate: u32,
    baud_rate:   u32,
    data_bits:   u8,
    parity:      u8,
) -> Vec<DecodedFrame> {
    let spb      = (sample_rate / baud_rate) as usize;
    let half_spb = spb / 2;
    let mut frames = Vec::new();
    let mut i = 1usize;

    while i < bits.len() {
        // Detect falling edge (idle → start bit)
        if bits[i - 1] == 1 && bits[i] == 0 {
            let start_pos = i;

            // Sample middle of start bit
            let mut pos = i + half_spb;
            if pos >= bits.len() || bits[pos] != 0 { i += 1; continue; }

            // Decode data bits (LSB first)
            let mut value: u8 = 0;
            let mut valid = true;
            for b in 0..data_bits {
                pos += spb;
                if pos >= bits.len() { valid = false; break; }
                if bits[pos] != 0 { value |= 1 << b; }
            }
            if !valid { break; }

            // Parity check
            let parity_error = if parity != 0 {
                pos += spb;
                if pos >= bits.len() { break; }
                let rx_parity = bits[pos];
                let calc_parity = value.count_ones() as u8 & 1;
                match parity {
                    2 => rx_parity != calc_parity,       // even
                    _ => rx_parity == calc_parity,       // odd
                }
            } else {
                false
            };

            // Stop bit check
            pos += spb;
            if pos >= bits.len() { break; }
            let framing_error = bits[pos] != 1;

            frames.push(DecodedFrame { byte: value, framing_error, parity_error, bit_position: start_pos });

            i = pos + half_spb;
        } else {
            i += 1;
        }
    }

    frames
}

fn main() {
    // Synthesise the bit stream for 'A' (0x41) at 9600 baud, 1 MHz sample rate
    let sample_rate: u32 = 1_000_000;
    let baud_rate:   u32 = 9_600;
    let spb = (sample_rate / baud_rate) as usize;

    let total = spb * 30;
    let mut bits = vec![1u8; total];

    // Helper: encode one 8N1 byte at offset `off`
    let encode = |bits: &mut Vec<u8>, off: usize, ch: u8| {
        for k in 0..spb { bits[off + k] = 0; }                      // start bit
        for b in 0..8u8 {
            let level = (ch >> b) & 1;
            for k in 0..spb { bits[off + spb * (1 + b as usize) + k] = level; }
        }
        for k in 0..spb { bits[off + spb * 9 + k] = 1; }            // stop bit
    };

    encode(&mut bits, spb * 2,  b'A');
    encode(&mut bits, spb * 13, b'B');

    let frames = decode_uart(&bits, sample_rate, baud_rate, 8, 0);

    println!("Decoded {} frame(s):", frames.len());
    for f in &frames {
        let ch = if f.byte.is_ascii_graphic() { f.byte as char } else { '.' };
        println!("  [{}] 0x{:02X} '{}'  framing={} parity={}",
                 f.bit_position, f.byte, ch, f.framing_error, f.parity_error);
    }
}
```

---

## Summary

Protocol analyzers — logic analyzers and oscilloscopes — are the definitive tools for diagnosing UART problems at every layer:

**Logic analyzers** work at the digital/protocol level. They capture the bit stream, run a UART decoder, and report the exact byte value of every frame along with any framing or parity errors. They are the right tool for: baud rate mismatches (detected as framing errors), wrong parity configuration, bit-order issues (LSB vs. MSB first), inter-byte timing gaps, and protocol-level sequencing bugs.

**Oscilloscopes** work at the analog/physical level. They reveal the actual voltage waveform, showing rise/fall times, overshoot, ringing, and true voltage levels. They are the right tool for: signal integrity issues on long cables, voltage level mismatches (3.3 V vs. 5 V), ground offset problems, noise, and verifying that the baud rate is set accurately by measuring the true bit period.

**Key debugging workflow:**

1. Start with the oscilloscope to verify signal integrity — correct voltage levels, clean edges, no excessive noise.
2. Move to the logic analyzer to decode frames — confirm bytes, detect framing/parity errors.
3. Use the `0x55` pattern (`01010101b`) as a first test: it maximally exercises every edge transition and makes bit-period measurement trivial.
4. Follow with the full byte range `0x00–0xFF` to exercise all data combinations.
5. For long-term or intermittent faults, run an automated loopback BER test while the analyzer captures continuously — correlate the timestamp of the first BER failure with the analyzer trace.

**The golden rules of UART debugging:**

- Baud rates must match within ~2% or framing errors will occur.
- Ground must be shared between all communicating devices.
- RS-232 levels (+/-12 V) must never be connected directly to 3.3 V logic.
- The `0x55` byte is your best friend on an oscilloscope.
- Logic analyzer sample rate must be at least 8× the baud rate for reliable decoding.
- When in doubt, start with the slowest baud rate (9600) and increase once the physical layer is verified.