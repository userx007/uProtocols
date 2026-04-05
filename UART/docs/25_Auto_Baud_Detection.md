# 25. Auto-Baud Detection

**Concept & Theory**
- How T_bit measurement works from signal transitions
- Four detection methods: start-bit timing, BREAK character, 0x55 sync byte, and software pulse timing
- A hardware support table across STM32, NXP, ESP32, AVR, LIN, and RP2040

**C/C++ Examples (3 implementations)**
1. **STM32 HAL hardware auto-baud** — using `UART_ADVFEATURE_AUTOBAUDRATE_ENABLE` and polling the `ABRF` flag
2. **Generic software pulse timing** — GPIO edge detection with a high-resolution timer, minimum pulse filtering, and standard-baud snapping
3. **Validated auto-baud loop** — a full probe-and-ACK protocol (0x55 → 0xAA) with retry logic

**Rust Examples (3 implementations)**
1. **`embedded-hal` portable auto-baud** — generic over any `InputPin` + timer closure, `no_std` compatible
2. **`stm32f4xx-hal` UART reconfiguration** — applying the detected baud rate to the HAL serial driver
3. **Host-side `serialport` baud scanner** — scanning all standard baud rates and validating with an ACK exchange

**Supporting Sections**
- Tolerance analysis table with error sources and mitigations
- Common pitfalls table (BREAK misdetection, glitches, IRQ blocking, etc.)
- A concise summary of all key rules


> **Automatically detecting communication speed from incoming data**

---

## Table of Contents

1. [Introduction](#introduction)
2. [How Auto-Baud Detection Works](#how-auto-baud-detection-works)
3. [Detection Methods](#detection-methods)
   - [Start-Bit Measurement](#1-start-bit-measurement)
   - [Break Character Detection](#2-break-character-detection)
   - [0x55 / 0xAA Sync Byte](#3-0x55--0xaa-sync-byte)
   - [Software Pulse Timing](#4-software-pulse-timing)
4. [Hardware Support](#hardware-support)
5. [Programming in C/C++](#programming-in-cc)
   - [STM32 Hardware Auto-Baud (HAL)](#51-stm32-hardware-auto-baud-hal)
   - [Generic Software Auto-Baud via Pulse Timing](#52-generic-software-auto-baud-via-pulse-timing)
   - [Auto-Baud with Validation Loop](#53-auto-baud-with-validation-loop)
6. [Programming in Rust](#programming-in-rust)
   - [Rust Software Auto-Baud via GPIO Timing](#61-rust-software-auto-baud-via-gpio-timing)
   - [Rust Auto-Baud on STM32 using stm32f4xx-hal](#62-rust-auto-baud-on-stm32-using-stm32f4xx-hal)
   - [Rust Cross-Platform Serial Port Auto-Baud Scan](#63-rust-cross-platform-serial-port-auto-baud-scan)
7. [Baud Rate Tolerance and Validation](#baud-rate-tolerance-and-validation)
8. [Common Pitfalls](#common-pitfalls)
9. [Summary](#summary)

---

## Introduction

In many embedded and communication systems, the baud rate (bits per second) of an incoming UART signal is not known in advance. This occurs in scenarios such as:

- **Field-programmable devices** where the host may send at any supported speed.
- **Legacy equipment integration** where documentation is unavailable.
- **Multi-speed bootloaders** (e.g., STM32 system bootloader, LIN bus).
- **Universal USB-to-Serial adapters** that must accommodate any connected device.

**Auto-baud detection** is the process by which a receiver automatically measures and locks onto the baud rate of the transmitter without prior configuration. Once the baud rate is determined, the UART peripheral (or software) reconfigures itself to match, enabling reliable communication.

---

## How Auto-Baud Detection Works

UART frames data as a series of bits with a fixed bit period `T_bit = 1 / baud_rate`. The receiver's job is to measure how long individual bit pulses last.

```
Idle (HIGH)
     ___________
    |           |   Start   D0   D1   D2  ...  D7  Stop
    |           |_____|_____|____|____|          |_____|
    
    <-- T_bit -->
```

The fundamental insight: **if you can accurately measure the duration of one or more bits, you can compute the baud rate.**

```
baud_rate = 1 / T_bit
```

---

## Detection Methods

### 1. Start-Bit Measurement

Measure the falling edge (idle → start bit) and determine the bit width from that single transition. This is the simplest hardware method.

**Limitation:** A single bit measurement is noise-sensitive.

### 2. Break Character Detection

A BREAK is a prolonged LOW (at least 10–11 bit periods). Some protocols (LIN, DMX512) guarantee a BREAK at session start. The receiver measures the LOW duration and divides by the known number of bit periods to compute `T_bit`.

```
BREAK = 13 bit periods (LIN)
T_bit = T_BREAK / 13
```

### 3. 0x55 / 0xAA Sync Byte

The byte `0x55` (binary `01010101`) produces a perfect alternating bit pattern — every bit transition is one bit wide. By measuring the total pulse widths across this byte, an average `T_bit` can be computed with high accuracy.

```
0x55 framed: [START=0][1][0][1][0][1][0][1][0][STOP=1]
              transitions occur every T_bit
```

The byte `0xAA` (`10101010`) provides the same alternating pattern with inverted polarity.

### 4. Software Pulse Timing

A software GPIO interrupt approach times every transition edge (rising and falling) and accumulates the shortest pulse width as the best estimate of `T_bit`. This is the most portable method.

---

## Hardware Support

Several microcontroller families provide **hardware-assisted auto-baud** in their UART peripherals:

| Platform        | Feature                           | Notes                                    |
|-----------------|-----------------------------------|------------------------------------------|
| STM32 (USART)   | ABR (Auto Baud Rate) bit in CR2   | Modes: falling edge, 0x55, 0x7F, 0xF7  |
| NXP LPC         | UART auto-baud with ACR register  | Timeout configurable                     |
| ESP32           | No direct HW auto-baud            | Use software timing                      |
| AVR (ATmega)    | No HW auto-baud                   | Pure software required                   |
| LIN controllers | Hardware BREAK detection          | T_bit derived from BREAK length          |
| RP2040 (PIO)    | PIO state machine timing          | Highly flexible, software-defined        |

---

## Programming in C/C++

### 5.1 STM32 Hardware Auto-Baud (HAL)

STM32 USARTs support auto-baud rate detection via the `ABRMOD` bits in `CR2` and the `ABREN` bit. After enabling, the hardware measures the first received character and auto-configures `BRR`.

```c
#include "stm32f4xx_hal.h"

UART_HandleTypeDef huart2;

/**
 * @brief Initialize USART2 with hardware auto-baud detection enabled.
 *        The peripheral will detect the baud rate from the first received byte (0x55 mode).
 */
void UART_AutoBaud_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 9600;       // Initial value; overwritten by ABR
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_AUTOBAUDRATE_INIT;
    huart2.AdvancedInit.AutoBaudRateEnable = UART_ADVFEATURE_AUTOBAUDRATE_ENABLE;

    // ABRMODE_0x55: expects 0x55 as the first character to measure bit width
    huart2.AdvancedInit.AutoBaudRateMode = UART_ADVFEATURE_AUTOBAUDRATE_ONSTARTBIT;

    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief Wait for auto-baud detection to complete and return detected baud rate.
 * @return Detected baud rate in bits per second, or 0 on timeout.
 */
uint32_t UART_WaitForAutoBaud(uint32_t timeout_ms)
{
    uint32_t tick_start = HAL_GetTick();

    // Poll ABRF (Auto Baud Rate Flag) in ISR register
    while (!(USART2->ISR & USART_ISR_ABRF)) {
        if ((HAL_GetTick() - tick_start) > timeout_ms) {
            return 0; // Timeout
        }
    }

    // Check for error flag ABRE
    if (USART2->ISR & USART_ISR_ABRE) {
        // Auto-baud error: clear flag and retry
        USART2->ICR = USART_ICR_ABRECF;
        return 0;
    }

    // Compute actual baud rate from BRR register
    // BaudRate = PCLK / BRR  (for OVER8=0)
    uint32_t pclk = HAL_RCC_GetPCLK1Freq();
    uint32_t brr  = USART2->BRR;
    uint32_t detected_baud = pclk / brr;

    return detected_baud;
}

/**
 * @brief Example usage: detect baud and echo back a confirmation.
 */
void AutoBaud_Example(void)
{
    UART_AutoBaud_Init();

    uint32_t baud = UART_WaitForAutoBaud(5000);
    if (baud > 0) {
        // Re-initialize with the detected baud rate for TX as well
        huart2.Init.BaudRate = baud;
        huart2.Init.Mode     = UART_MODE_TX_RX;
        HAL_UART_Init(&huart2);

        char msg[48];
        snprintf(msg, sizeof(msg), "Auto-baud OK: %lu bps\r\n", baud);
        HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 1000);
    }
}
```

---

### 5.2 Generic Software Auto-Baud via Pulse Timing

For platforms without hardware support, measure the shortest pulse duration across multiple transitions using a high-resolution timer.

```c
#include <stdint.h>
#include <stdbool.h>

// Platform-specific: returns current time in microseconds
extern uint32_t timer_get_us(void);
// Platform-specific: read RX pin level (1 = HIGH, 0 = LOW)
extern int  gpio_rx_read(void);

#define ABD_MAX_EDGES       32    // Number of edges to sample
#define ABD_TIMEOUT_US   50000    // 50 ms overall timeout
#define ABD_MIN_PULSE_US     5    // Reject pulses shorter than this (glitch filter)

/**
 * @brief Measure shortest pulse on the RX line to estimate T_bit.
 *
 * Waits for up to ABD_MAX_EDGES transitions and records the duration
 * of each pulse. The minimum valid pulse duration is T_bit.
 *
 * @param[out] baud_rate  Detected baud rate (bps)
 * @return true on success, false on timeout or invalid measurement
 */
bool autobaud_detect(uint32_t *baud_rate)
{
    uint32_t min_pulse_us = UINT32_MAX;
    uint32_t t_start, t_edge, duration;
    int      prev_level, cur_level;
    int      edge_count = 0;

    // Wait for line to go idle (HIGH)
    uint32_t deadline = timer_get_us() + ABD_TIMEOUT_US;
    while (!gpio_rx_read()) {
        if (timer_get_us() > deadline) return false;
    }

    prev_level = 1;
    t_edge     = timer_get_us();
    deadline   = t_edge + ABD_TIMEOUT_US;

    while (edge_count < ABD_MAX_EDGES) {
        if (timer_get_us() > deadline) break;

        cur_level = gpio_rx_read();

        if (cur_level != prev_level) {
            uint32_t now = timer_get_us();
            duration = now - t_edge;
            t_edge   = now;

            if (duration >= ABD_MIN_PULSE_US) {
                if (duration < min_pulse_us) {
                    min_pulse_us = duration;
                }
            }
            prev_level = cur_level;
            edge_count++;
        }
    }

    if (edge_count < 4 || min_pulse_us == UINT32_MAX) {
        return false; // Not enough data
    }

    // T_bit = min_pulse_us; baud = 1_000_000 / T_bit
    *baud_rate = 1000000UL / min_pulse_us;
    return true;
}

/**
 * @brief Snap a raw measured baud rate to the nearest standard baud rate.
 *
 * @param raw   Raw measured baud rate (bps)
 * @return Nearest standard baud rate, or raw if no match within 3%
 */
uint32_t snap_to_standard_baud(uint32_t raw)
{
    static const uint32_t standard_bauds[] = {
        300, 600, 1200, 2400, 4800, 9600,
        14400, 19200, 38400, 57600, 115200,
        230400, 460800, 921600, 0
    };

    uint32_t best      = raw;
    uint32_t best_diff = UINT32_MAX;

    for (int i = 0; standard_bauds[i] != 0; i++) {
        uint32_t diff = (raw > standard_bauds[i])
                        ? (raw - standard_bauds[i])
                        : (standard_bauds[i] - raw);
        if (diff < best_diff) {
            best_diff = diff;
            best      = standard_bauds[i];
        }
    }

    // Only snap if within 3% tolerance
    if (best_diff * 100 / raw <= 3) {
        return best;
    }
    return raw;
}

/**
 * @brief Full auto-baud sequence: measure, snap, and apply to UART.
 */
void autobaud_run(void)
{
    uint32_t raw_baud = 0;

    if (!autobaud_detect(&raw_baud)) {
        // Handle error: no signal or timeout
        return;
    }

    uint32_t baud = snap_to_standard_baud(raw_baud);

    // Apply detected baud to UART peripheral (platform-specific)
    uart_set_baud(baud);
}
```

---

### 5.3 Auto-Baud with Validation Loop

A robust implementation attempts detection, then validates by receiving a known sync sequence.

```c
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define SYNC_BYTE       0x55
#define MAX_ATTEMPTS    5

extern bool  autobaud_detect(uint32_t *baud_out);
extern void  uart_set_baud(uint32_t baud);
extern int   uart_recv_byte_timeout(uint8_t *byte, uint32_t timeout_ms);
extern void  uart_send_byte(uint8_t byte);
extern uint32_t snap_to_standard_baud(uint32_t raw);

/**
 * @brief Attempt auto-baud with ACK-based validation.
 *
 * Protocol:
 *   1. Measure baud from incoming pulses.
 *   2. Reconfigure UART.
 *   3. Wait for 0x55 sync byte.
 *   4. Echo back 0xAA as acknowledgment.
 *   5. If 0x55 received cleanly → success.
 *
 * @return Detected and validated baud rate, or 0 on failure.
 */
uint32_t autobaud_with_validation(void)
{
    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        uint32_t raw_baud = 0;

        if (!autobaud_detect(&raw_baud)) {
            continue;
        }

        uint32_t baud = snap_to_standard_baud(raw_baud);
        uart_set_baud(baud);

        // Wait for sync byte 0x55
        uint8_t rx_byte = 0;
        if (uart_recv_byte_timeout(&rx_byte, 100) == 0 && rx_byte == SYNC_BYTE) {
            // Send ACK
            uart_send_byte(0xAA);
            return baud; // Validated
        }
    }

    return 0; // Failed after all attempts
}
```

---

## Programming in Rust

### 6.1 Rust Software Auto-Baud via GPIO Timing

Rust's ownership model and `embedded-hal` traits make it natural to implement timing-based auto-baud in a portable, no_std environment.

```rust
#![no_std]

use embedded_hal::digital::v2::InputPin;
use fugit::MicrosDurationU32;

/// Maximum number of edges to sample.
const MAX_EDGES: usize = 32;
/// Minimum valid pulse width in microseconds (glitch filter).
const MIN_PULSE_US: u32 = 5;

/// Standard baud rates for snapping.
const STANDARD_BAUDS: &[u32] = &[
    300, 600, 1200, 2400, 4800, 9600,
    14400, 19200, 38400, 57600, 115200,
    230400, 460800, 921600,
];

/// Error type for auto-baud detection.
#[derive(Debug)]
pub enum AutoBaudError {
    Timeout,
    InsufficientEdges,
    InvalidMeasurement,
}

/// Snap a raw measured baud rate to the nearest standard baud rate within 3% tolerance.
pub fn snap_to_standard_baud(raw: u32) -> u32 {
    STANDARD_BAUDS
        .iter()
        .copied()
        .min_by_key(|&s| raw.abs_diff(s))
        .filter(|&s| {
            let diff = raw.abs_diff(s);
            diff * 100 / raw <= 3
        })
        .unwrap_or(raw)
}

/// Detect the baud rate by measuring the minimum pulse duration on the RX pin.
///
/// # Arguments
/// * `rx_pin`    - An `InputPin` connected to the UART RX line.
/// * `timer_us`  - Closure returning the current time in microseconds.
/// * `timeout_us`- Overall timeout in microseconds.
///
/// # Returns
/// Detected baud rate (possibly snapped to standard) or an error.
pub fn autobaud_detect<P, F>(
    rx_pin: &P,
    timer_us: &F,
    timeout_us: u32,
) -> Result<u32, AutoBaudError>
where
    P: InputPin,
    F: Fn() -> u32,
{
    // Wait for idle (HIGH)
    let deadline = timer_us() + timeout_us;
    loop {
        if rx_pin.is_high().unwrap_or(false) {
            break;
        }
        if timer_us() >= deadline {
            return Err(AutoBaudError::Timeout);
        }
    }

    let mut min_pulse_us: u32 = u32::MAX;
    let mut edge_count:   usize = 0;
    let mut prev_level:   bool = true; // idle = HIGH
    let mut t_edge:       u32 = timer_us();
    let     deadline:     u32 = t_edge + timeout_us;

    while edge_count < MAX_EDGES {
        if timer_us() >= deadline {
            break;
        }

        let cur_level = rx_pin.is_high().unwrap_or(false);

        if cur_level != prev_level {
            let now      = timer_us();
            let duration = now.saturating_sub(t_edge);
            t_edge       = now;

            if duration >= MIN_PULSE_US && duration < min_pulse_us {
                min_pulse_us = duration;
            }

            prev_level = cur_level;
            edge_count += 1;
        }
    }

    if edge_count < 4 {
        return Err(AutoBaudError::InsufficientEdges);
    }
    if min_pulse_us == u32::MAX {
        return Err(AutoBaudError::InvalidMeasurement);
    }

    // T_bit = min_pulse_us → baud = 1_000_000 / T_bit
    let raw_baud = 1_000_000_u32 / min_pulse_us;
    Ok(snap_to_standard_baud(raw_baud))
}
```

---

### 6.2 Rust Auto-Baud on STM32 using stm32f4xx-hal

This example uses the `stm32f4xx-hal` crate to configure the UART peripheral after a software-detected baud rate.

```rust
// Cargo.toml dependencies:
// [dependencies]
// stm32f4xx-hal = { version = "0.21", features = ["stm32f411"] }
// embedded-hal = "0.2"
// cortex-m = "0.7"

#![no_std]
#![no_main]

use cortex_m_rt::entry;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{Config, Serial},
    timer::Timer,
};
use core::fmt::Write;

#[entry]
fn main() -> ! {
    let dp  = pac::Peripherals::take().unwrap();
    let cp  = cortex_m::Peripherals::take().unwrap();

    let rcc    = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

    let gpioa = dp.GPIOA.split();
    let tx_pin = gpioa.pa2.into_alternate();
    let rx_pin = gpioa.pa3.into_alternate();

    // --- Step 1: Software pulse timing on PA3 as GPIO input ---
    // (Simplified: in practice, use the autobaud_detect function above)
    // Here we assume 9600 baud was detected as an example.
    let detected_baud: u32 = 9600; // Replace with autobaud_detect() result

    // --- Step 2: Configure UART with detected baud rate ---
    let serial = Serial::new(
        dp.USART2,
        (tx_pin, rx_pin),
        Config::default()
            .baudrate(detected_baud.bps())
            .wordlength_8()
            .parity_none()
            .stopbits(stm32f4xx_hal::serial::StopBits::STOP1),
        &clocks,
    )
    .unwrap();

    let (mut tx, mut rx) = serial.split();

    // --- Step 3: Announce detected baud rate ---
    writeln!(tx, "Auto-baud detected: {} bps\r", detected_baud).ok();

    // --- Step 4: Echo loop ---
    loop {
        if let Ok(byte) = rx.read() {
            tx.write(byte).ok(); // Echo back
        }
    }
}
```

---

### 6.3 Rust Cross-Platform Serial Port Auto-Baud Scan

For host-side Rust applications (Linux/Windows/macOS), use the `serialport` crate to scan standard baud rates and find the one that produces valid responses.

```rust
// Cargo.toml:
// [dependencies]
// serialport = "4"

use serialport::{SerialPort, SerialPortSettings};
use std::time::Duration;
use std::io::{Read, Write};

const STANDARD_BAUDS: &[u32] = &[
    1200, 2400, 4800, 9600, 19200, 38400,
    57600, 115200, 230400, 460800, 921600,
];

/// Sync byte sent to probe the device.
const SYNC_BYTE: u8 = 0x55;
/// Expected response indicating the device understood the probe.
const ACK_BYTE:  u8 = 0xAA;

/// Try each standard baud rate in turn; return the first one that produces an ACK.
///
/// # Arguments
/// * `port_name` - e.g., "/dev/ttyUSB0" or "COM3"
///
/// # Returns
/// The detected baud rate, or an error.
pub fn scan_for_baud_rate(port_name: &str) -> Result<u32, String> {
    for &baud in STANDARD_BAUDS {
        let port = serialport::new(port_name, baud)
            .timeout(Duration::from_millis(100))
            .data_bits(serialport::DataBits::Eight)
            .parity(serialport::Parity::None)
            .stop_bits(serialport::StopBits::One)
            .open();

        let mut port = match port {
            Ok(p) => p,
            Err(e) => {
                eprintln!("Could not open {}: {}", port_name, e);
                continue;
            }
        };

        // Flush any stale data
        port.clear(serialport::ClearBuffer::All).ok();

        // Send sync probe
        if port.write_all(&[SYNC_BYTE]).is_err() {
            continue;
        }

        // Read response
        let mut response = [0u8; 1];
        match port.read_exact(&mut response) {
            Ok(_) if response[0] == ACK_BYTE => {
                println!("Auto-baud success: {} bps on {}", baud, port_name);
                return Ok(baud);
            }
            _ => {
                // Wrong baud or no response — try next
                continue;
            }
        }
    }

    Err(format!("Auto-baud detection failed on {}", port_name))
}

fn main() {
    match scan_for_baud_rate("/dev/ttyUSB0") {
        Ok(baud) => println!("Detected baud rate: {}", baud),
        Err(e)   => eprintln!("Error: {}", e),
    }
}
```

---

## Baud Rate Tolerance and Validation

Even after detecting a baud rate, measurement error is inevitable. Standard UART tolerates up to **±2–3%** baud rate mismatch before framing errors occur.

### Error Sources

| Source                          | Typical Impact      |
|---------------------------------|---------------------|
| Timer resolution (1 µs @ 115200)| ~11.5% error → critical |
| Interrupt latency jitter        | 1–3% at high baud   |
| Crystal oscillator tolerance    | 0.005–0.02%         |
| Oversampling (16×)              | Reduces noise significantly |

### Mitigation Strategies

**1. Average multiple measurements**
```c
uint32_t sum = 0;
int valid = 0;
for (int i = 0; i < 8; i++) {
    uint32_t b;
    if (measure_pulse(&b)) {
        sum += b;
        valid++;
    }
}
uint32_t avg_t_bit = (valid > 0) ? (sum / valid) : 0;
```

**2. Use a higher-resolution timer**
- At 115200 baud, `T_bit ≈ 8.68 µs`. A 1 µs timer gives only ~8 counts — coarse.
- Use a timer with 100 ns or better resolution for accurate high-speed detection.

**3. Always snap to standard baud rates**
```rust
// Snap within 3% tolerance (see snap_to_standard_baud above)
let snapped = snap_to_standard_baud(raw_measured);
```

**4. Validate with a known exchange**
- After setting the detected baud, send a known byte and expect a specific reply.
- Retry if the reply is incorrect or garbled.

---

## Common Pitfalls

| Pitfall                              | Description                                                                         | Solution                                                        |
|--------------------------------------|-------------------------------------------------------------------------------------|-----------------------------------------------------------------|
| **Measuring a BREAK as a bit**       | A LIN BREAK (13 bits of LOW) looks like a very slow baud rate                      | Detect and discard pulses longer than ~10× expected T_bit      |
| **Glitch pulses from noise**         | Short spikes on an unconnected RX line cause incorrect short-pulse measurements     | Enforce a minimum pulse width filter (e.g., 5 µs)             |
| **Single-bit measurement error**     | One bit measured in isolation can be off by 1–2 timer counts                        | Average across many pulses; prefer multi-bit patterns           |
| **Missing idle period**              | Starting measurement mid-frame corrupts T_bit                                       | Always wait for the line to go idle before starting            |
| **Not re-enabling TX after ABR**     | STM32 ABR mode operates only in RX; TX must be reconfigured separately             | Reinitialize UART fully after detecting baud                   |
| **Tight timing loops blocking IRQs** | Spin-wait loops for edge detection prevent ISR service                               | Use timer capture/compare interrupts or DMA where possible     |
| **Non-standard baud rates**          | Devices using 31250 baud (MIDI) or 76800 baud (custom) won't snap correctly        | Expand the standard baud table for the target application      |

---

## Summary

Auto-baud detection enables a UART receiver to determine the transmitter's baud rate without prior configuration, making it essential in field-programmable devices, bootloaders, and universal interfaces.

**Core concept:** The bit period `T_bit = 1 / baud_rate` can be measured by timing signal transitions on the RX line. The shortest observed pulse approximates one bit period.

**Detection approaches** range from simple falling-edge measurement and BREAK-length analysis to high-accuracy alternating sync-byte patterns (0x55/0xAA). Many modern MCUs (STM32, NXP LPC) provide **hardware-assisted** auto-baud directly in the UART peripheral, simplifying implementation.

**In C/C++**, hardware HAL APIs (e.g., STM32 HAL's `UART_ADVFEATURE_AUTOBAUDRATE_ENABLE`) or software GPIO pulse-timing loops with a high-resolution timer provide flexible detection, followed by snapping the raw measurement to the nearest standard baud rate.

**In Rust**, the `embedded-hal` trait ecosystem enables portable, type-safe implementations that work across MCU families. The `stm32f4xx-hal` crate enables reconfiguring the UART serial port with a detected baud rate, while the `serialport` crate allows host-side scanning for desktop applications.

**Key rules for robust operation:**
- Filter out glitch pulses shorter than a minimum threshold.
- Average multiple measurements to reduce timer quantization error.
- Always snap raw measurements to the nearest standard baud rate within 3%.
- Validate the result with a known-byte exchange before trusting the detection.
- Prefer hardware-assisted detection when available to minimize CPU load and latency.

With these techniques, a UART interface can reliably and autonomously adapt to any transmitter baud rate within its operational range.

---

*Document: 25_Auto_Baud_Detection.md — Part of the UART Programming Reference Series*