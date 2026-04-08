# 73. UART Clock Recovery

**Theory** — Why asynchronous clocks drift, how accumulated bit-period error is calculated, and the role of the start-bit falling edge as the universal re-sync anchor.

**Three recovery techniques** — Fixed oversampling, DPLL, and baud-rate auto-detection — explained from principle through trade-offs.

**C/C++ code examples:**
- Software UART with 16× oversampling and majority voting (portable, MCU-agnostic)
- DPLL-based receiver with phase-accumulator correction
- Baud rate auto-detection by minimum pulse measurement
- STM32 HAL configuration (16×, 8×, and hardware Auto-Baud-Rate modes)

**Rust code examples:**
- `SoftUartRx<PIN, DELAY>` using `embedded-hal` traits — clean, `no_std` compatible
- `Dpll` struct with tick-by-tick phase correction and drift-in-ppm diagnostics
- `FrameCollector` for assembling DPLL output into validated 8N1 frames
- `BaudDetector` with unit tests for the matching logic

**Error handling table** and a **clock tolerance analysis** (theoretical ±5.26% maximum, practical ±2% recommended), followed by a concise summary.

## Extracting Timing Information from Asynchronous Data

---

## Table of Contents

1. [Introduction](#introduction)
2. [Theoretical Background](#theoretical-background)
3. [Clock Recovery Techniques](#clock-recovery-techniques)
4. [Oversampling Method](#oversampling-method)
5. [Phase-Locked Loop (PLL) Based Recovery](#phase-locked-loop-pll-based-recovery)
6. [Digital Phase-Locked Loop (DPLL)](#digital-phase-locked-loop-dpll)
7. [Baud Rate Detection](#baud-rate-detection)
8. [Implementation in C/C++](#implementation-in-cc)
9. [Implementation in Rust](#implementation-in-rust)
10. [Error Handling and Edge Cases](#error-handling-and-edge-cases)
11. [Summary](#summary)

---

## Introduction

In UART (Universal Asynchronous Receiver/Transmitter) communication, the "asynchronous" designation means that transmitter and receiver operate from **independent, unsynchronized clocks**. Unlike synchronous protocols (SPI, I²C), there is no shared clock signal. Instead, both sides must agree on a baud rate in advance, and the receiver must continuously recover timing information from the incoming data stream itself.

**Clock Recovery** is the process by which a UART receiver:

- Detects the start of a new data frame (start bit edge).
- Aligns its sampling point to the center of each incoming bit.
- Tracks and compensates for slow drift between transmitter and receiver clocks.
- Reconstructs the original bit sequence with high reliability.

Without proper clock recovery, even a tiny frequency mismatch between transmitter and receiver clocks accumulates over the length of a frame, potentially causing incorrect bit sampling and data corruption.

---

## Theoretical Background

### The Asynchronous Problem

Consider a UART frame at 9600 baud with 8 data bits, no parity, and 1 stop bit (8N1): the frame is 10 bits long (1 start + 8 data + 1 stop). One bit period is:

```
T_bit = 1 / 9600 ≈ 104.17 µs
T_frame = 10 × 104.17 µs ≈ 1.04 ms
```

If the receiver clock deviates by even **0.5%** from the transmitter's clock:

```
Accumulated error over 10 bits = 10 × 104.17 µs × 0.005 ≈ 5.2 µs
```

Since reliable sampling requires the sample point to be within ±50% of the bit center (±52 µs at 9600 baud), 0.5% is well within tolerance. However, at higher baud rates or with longer frames, this tolerance is tighter.

**Maximum allowable clock error** for standard UART is typically **±2–5%**, depending on the number of bits and oversampling ratio.

### The Start Bit Edge

The key to UART clock recovery is the **falling edge of the start bit**. The idle line is logic HIGH. Every new frame begins with the line going LOW (start bit). This edge:

- Signals the start of a new frame to the receiver.
- Provides a known reference point from which all subsequent bit-sample times are calculated.
- Allows the receiver to re-synchronize at the beginning of each frame (no inter-frame drift accumulates).

### Bit Center Sampling

To maximize noise immunity, the receiver samples each bit at or near its **center**. If we detect the start bit's falling edge at time `t₀`, the center of bit `n` (0-indexed, where bit 0 is the start bit) is:

```
t_sample(n) = t₀ + (n + 0.5) × T_bit
```

For an oversampling receiver with ratio `N` (e.g., `N = 16`), the timing is tracked in sub-bit units, and the center of a bit is at sample `N/2` within the bit period.

---

## Clock Recovery Techniques

There are three primary techniques used in UART clock recovery, ordered from simplest to most sophisticated:

### 1. Fixed Oversampling (Most Common in Microcontrollers)

The receiver samples the line at `N × baud_rate` (typically `N = 16`), detects the start bit edge, waits `N/2` samples to reach bit center, then samples every `N` samples thereafter. Simple and robust for short frames and modest clock accuracy.

### 2. Digital Phase-Locked Loop (DPLL)

A software or hardware feedback loop that continuously adjusts the sampling phase based on observed transitions. Better for long continuous streams or when clock accuracy is poor.

### 3. Baud Rate Auto-Detection

Measures the width of incoming pulses to determine the baud rate dynamically, before applying any of the above. Used in adaptive receivers.

---

## Oversampling Method

### Principle

The oversampling method is the standard approach in virtually all modern UART hardware (UARTs in STM32, AVR, PIC, ESP32, etc. all use 16× oversampling by default).

```
Idle:    ___________
         |
Start:   |___________
         ^           ^
         t₀          t₀ + T_bit

With 16× oversampling, T_bit = 16 samples

Start bit center = t₀ + 8 samples
Data bit 0 center = t₀ + 8 + 16 = 24 samples
Data bit 1 center = t₀ + 8 + 32 = 40 samples
...
```

### Majority Voting

To further improve noise immunity, many implementations use **3-of-3** or **2-of-3 majority vote** sampling: three consecutive samples are taken near the bit center, and the majority value is used:

```
Sample positions (16× oversampling):
Bit center at sample 8 (of 16)
Vote samples: 7, 8, 9

If 2 or 3 of these are HIGH → bit = HIGH
If 2 or 3 of these are LOW  → bit = LOW
```

---

## Phase-Locked Loop (PLL) Based Recovery

An analog or mixed-signal PLL continuously tracks the phase and frequency of transitions in the data stream. The loop has three components:

- **Phase Detector:** Compares the phase of incoming transitions to the local clock.
- **Loop Filter:** Low-pass filters the phase error to produce a smooth control signal.
- **Voltage-Controlled Oscillator (VCO):** Adjusts the local clock frequency to minimize phase error.

PLLs are used in high-speed UART variants (RS-485, LIN) and in implementations where the receiver must track slow crystal drift over long sessions.

---

## Digital Phase-Locked Loop (DPLL)

The DPLL is the software/firmware equivalent of an analog PLL, practical for microcontroller-based UART receivers.

### DPLL Concept

```
Incoming bit stream transitions → Phase Error Calculation
                                       ↓
                              Phase Accumulator Update
                                       ↓
                              Adjusted Sample Point
```

The phase accumulator increments at each oversampled clock tick. When a data transition is detected, the accumulator is nudged toward the expected transition phase (0 or `N` in an N× system), effectively steering the receiver clock toward the transmitter's clock.

---

## Baud Rate Detection

Before clock recovery can begin, the baud rate must be known. Auto-detection measures the duration of the shortest pulse in the incoming data stream, which corresponds to one bit period at the actual baud rate.

**Algorithm:**

1. Wait for a data transition.
2. Measure the time (in system clock ticks) until the next transition.
3. Record this as a candidate bit period.
4. Repeat and take the minimum observed value (shortest pulse = 1 bit period).
5. Match against standard baud rates: 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600.

---

## Implementation in C/C++

### 1. Basic 16× Oversampling Clock Recovery (Software UART)

This implements a complete software UART receiver with 16× oversampling clock recovery in C, suitable for microcontrollers without hardware UART or for bit-banging on GPIO.

```c
/**
 * uart_clock_recovery.c
 *
 * Software UART with 16x oversampling clock recovery.
 * Assumes:
 *   - uart_sample_pin() returns current RX pin state (0 or 1)
 *   - delay_us(n) delays for n microseconds
 *   - BAUD_RATE is defined (e.g., 9600)
 *   - CPU_FREQ_HZ is defined
 */

#include <stdint.h>
#include <stdbool.h>

#define BAUD_RATE       9600UL
#define OVERSAMPLE      16
#define SAMPLE_RATE     (BAUD_RATE * OVERSAMPLE)          // 153600 Hz
#define SAMPLE_US       (1000000UL / SAMPLE_RATE)         // ~6.5 µs per sample
#define HALF_BIT_TICKS  (OVERSAMPLE / 2)                  // 8 ticks to bit center
#define FRAME_BITS      10                                 // 1 start + 8 data + 1 stop

/* -------------------------------------------------------------------
 * Platform abstraction (implement these for your hardware)
 * ------------------------------------------------------------------- */
extern int  uart_sample_pin(void);  // Read RX pin: returns 0 or 1
extern void delay_us(uint32_t us);  // Busy-wait delay in microseconds

/* -------------------------------------------------------------------
 * Majority vote: sample 3 consecutive ticks, return dominant bit
 * ------------------------------------------------------------------- */
static int majority_vote(void)
{
    int s0, s1, s2;

    s0 = uart_sample_pin();
    delay_us(SAMPLE_US);
    s1 = uart_sample_pin();
    delay_us(SAMPLE_US);
    s2 = uart_sample_pin();

    /* Return 1 if 2 or 3 samples are high */
    return ((s0 + s1 + s2) >= 2) ? 1 : 0;
}

/* -------------------------------------------------------------------
 * Wait for falling edge of start bit (HIGH → LOW transition)
 * Returns false if no start bit detected within timeout_us.
 * ------------------------------------------------------------------- */
static bool wait_for_start_bit(uint32_t timeout_us)
{
    uint32_t elapsed = 0;

    /* Wait for line to be idle (HIGH) */
    while (!uart_sample_pin()) {
        delay_us(SAMPLE_US);
        elapsed += SAMPLE_US;
        if (elapsed > timeout_us) return false;
    }

    /* Wait for falling edge (start bit) */
    elapsed = 0;
    while (uart_sample_pin()) {
        delay_us(SAMPLE_US);
        elapsed += SAMPLE_US;
        if (elapsed > timeout_us) return false;
    }

    return true;   /* Falling edge detected — t₀ established here */
}

/* -------------------------------------------------------------------
 * Receive one UART byte using 16x oversampling clock recovery.
 *
 * Returns: received byte (0–255), or -1 on framing error.
 *
 * Timing diagram for 16x oversampling:
 *
 *   START  D0   D1   D2   D3   D4   D5   D6   D7   STOP
 *   |      |    |    |    |    |    |    |    |    |
 *   0     16   32   48   64   80   96  112  128  144  (ticks from edge)
 *   ^8    ^24  ^40  ...                               (sample points)
 * ------------------------------------------------------------------- */
int uart_receive_byte(void)
{
    uint8_t data = 0;
    int     bit;
    int     i;

    /* ---- Step 1: Detect start bit falling edge ---- */
    if (!wait_for_start_bit(100000 /* 100 ms timeout */)) {
        return -1;  /* Timeout — no activity */
    }

    /*
     * t₀ = NOW (falling edge of start bit)
     * Advance to center of start bit: wait HALF_BIT_TICKS - 1.5 ticks
     * (subtract 1.5 for the samples already taken in majority_vote)
     */
    delay_us(SAMPLE_US * (HALF_BIT_TICKS - 1));

    /* ---- Step 2: Verify start bit is still LOW at center ---- */
    bit = majority_vote();
    if (bit != 0) {
        return -1;  /* Framing error: start bit not LOW at center */
    }

    /* ---- Step 3: Sample 8 data bits ---- */
    for (i = 0; i < 8; i++) {
        /*
         * From current sample point, advance exactly one bit period.
         * We consumed 3 ticks in majority_vote(); wait OVERSAMPLE-3 more.
         */
        delay_us(SAMPLE_US * (OVERSAMPLE - 3));

        bit = majority_vote();  /* Sample at bit center with majority vote */

        /* LSB first in UART */
        data |= (uint8_t)(bit << i);
    }

    /* ---- Step 4: Sample stop bit ---- */
    delay_us(SAMPLE_US * (OVERSAMPLE - 3));
    bit = majority_vote();

    if (bit != 1) {
        return -1;  /* Framing error: stop bit not HIGH */
    }

    return (int)data;
}
```

---

### 2. DPLL-Based Clock Recovery

This implements a Digital Phase-Locked Loop for software UART clock recovery, suitable for longer data streams where accumulated drift must be corrected.

```c
/**
 * uart_dpll.c
 *
 * DPLL-based UART clock recovery.
 * The phase accumulator tracks sub-bit timing and is adjusted
 * whenever a data transition is observed, steering the receiver
 * clock toward the transmitter.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define OVERSAMPLE      16
#define PHASE_MAX       OVERSAMPLE
#define PHASE_CENTER    (OVERSAMPLE / 2)    /* Ideal sample point = 8 */
#define PHASE_STEP      2                   /* DPLL correction step size */

typedef struct {
    int32_t  phase_acc;      /* Phase accumulator (0 .. PHASE_MAX-1)  */
    uint8_t  prev_bit;       /* Previous bit value for edge detection  */
    uint32_t bit_count;      /* Total bits received (statistics)       */
    int32_t  total_error;    /* Accumulated phase error (statistics)   */
} dpll_state_t;

/* -------------------------------------------------------------------
 * Initialise the DPLL state
 * ------------------------------------------------------------------- */
void dpll_init(dpll_state_t *d)
{
    memset(d, 0, sizeof(*d));
    d->phase_acc = 0;
    d->prev_bit  = 1;  /* Idle line is HIGH */
}

/* -------------------------------------------------------------------
 * Feed one oversampled bit into the DPLL.
 *
 * Call this at OVERSAMPLE × baud_rate frequency.
 *
 * Returns:
 *   true  + sets *sample_out → this tick is the sample point
 *   false               → not a sample point this tick
 * ------------------------------------------------------------------- */
bool dpll_tick(dpll_state_t *d, uint8_t raw_bit, uint8_t *sample_out)
{
    bool is_sample_point = false;

    /* ---- Phase accumulator advance ---- */
    d->phase_acc++;
    if (d->phase_acc >= PHASE_MAX) {
        d->phase_acc -= PHASE_MAX;
    }

    /* ---- Edge detection: correct phase on transitions ---- */
    if (raw_bit != d->prev_bit) {
        /*
         * A transition should ideally occur at phase 0 (start of a bit).
         * Current phase error = d->phase_acc (how far past phase 0 we are).
         *
         * If phase_acc < PHASE_CENTER  → transition came early → slow down (subtract)
         * If phase_acc >= PHASE_CENTER → transition came late  → speed up  (add)
         *
         * Apply a fractional correction (PHASE_STEP / PHASE_MAX of one bit period).
         */
        int32_t error = d->phase_acc;
        d->total_error += error;

        if (error < PHASE_CENTER) {
            /* Transition early: phase accumulator is too fast, pull back */
            d->phase_acc -= PHASE_STEP;
            if (d->phase_acc < 0) d->phase_acc += PHASE_MAX;
        } else {
            /* Transition late: phase accumulator is too slow, push forward */
            d->phase_acc += PHASE_STEP;
            if (d->phase_acc >= PHASE_MAX) d->phase_acc -= PHASE_MAX;
        }

        d->prev_bit = raw_bit;
    }

    /* ---- Is this the sample point (center of bit)? ---- */
    if (d->phase_acc == PHASE_CENTER) {
        if (sample_out) *sample_out = raw_bit;
        is_sample_point = true;
        d->bit_count++;
    }

    return is_sample_point;
}

/* -------------------------------------------------------------------
 * Usage example: receive a byte with DPLL clock recovery
 * ------------------------------------------------------------------- */
int uart_dpll_receive_byte(dpll_state_t *dpll)
{
    extern int  uart_sample_pin(void);
    extern void delay_us(uint32_t us);

    /* Oversampling interval */
#define SAMPLE_INTERVAL_US  (1000000UL / (9600UL * OVERSAMPLE))

    uint8_t frame[10];        /* start + 8 data + stop */
    int     frame_idx  = 0;
    bool    in_frame   = false;
    uint8_t sampled_bit;
    uint8_t raw;

    /* Collect exactly 10 sampled bits (one UART frame) */
    while (frame_idx < 10) {
        raw = (uint8_t)uart_sample_pin();
        delay_us(SAMPLE_INTERVAL_US);

        if (dpll_tick(dpll, raw, &sampled_bit)) {
            if (!in_frame) {
                /* First sample: must be start bit (LOW) */
                if (sampled_bit == 0) {
                    in_frame = true;
                    frame[frame_idx++] = sampled_bit;
                }
                /* else: still idle, keep waiting */
            } else {
                frame[frame_idx++] = sampled_bit;
            }
        }
    }

    /* Validate framing */
    if (frame[0] != 0 || frame[9] != 1) {
        return -1;  /* Framing error */
    }

    /* Reconstruct byte (LSB first) */
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (uint8_t)(frame[1 + i] << i);
    }
    return byte;
}
```

---

### 3. Baud Rate Auto-Detection in C

```c
/**
 * uart_baud_detect.c
 *
 * Auto-detects UART baud rate by measuring the shortest pulse width
 * in the incoming data stream and matching to a standard baud rate.
 */

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

/* Standard baud rates to match against */
static const uint32_t STANDARD_BAUDS[] = {
    1200, 2400, 4800, 9600, 14400, 19200,
    28800, 38400, 57600, 115200, 230400, 460800, 921600
};
#define NUM_BAUDS (sizeof(STANDARD_BAUDS) / sizeof(STANDARD_BAUDS[0]))

/* -------------------------------------------------------------------
 * Platform hooks (implement for your hardware):
 *   get_time_us()     → monotonic microsecond counter
 *   uart_sample_pin() → current RX pin level (0 or 1)
 * ------------------------------------------------------------------- */
extern uint32_t get_time_us(void);
extern int      uart_sample_pin(void);

/* -------------------------------------------------------------------
 * Measure the width of the next pulse (either HIGH or LOW).
 * Returns pulse width in microseconds.
 * ------------------------------------------------------------------- */
static uint32_t measure_pulse_us(void)
{
    int start_level = uart_sample_pin();
    uint32_t t_start, t_end;

    /* Wait for the level to change (start of pulse) */
    while (uart_sample_pin() == start_level) { /* busy wait */ }

    t_start = get_time_us();

    /* Wait for the level to change again (end of pulse) */
    int current_level = !start_level;
    while (uart_sample_pin() == current_level) { /* busy wait */ }

    t_end = get_time_us();
    return (t_end - t_start);
}

/* -------------------------------------------------------------------
 * Find the closest standard baud rate to a measured bit period.
 *
 * @param bit_period_us  Measured bit period in microseconds
 * @return               Matched standard baud rate, or 0 if none
 * ------------------------------------------------------------------- */
static uint32_t match_baud_rate(uint32_t bit_period_us)
{
    uint32_t best_baud  = 0;
    uint32_t best_error = UINT32_MAX;

    for (size_t i = 0; i < NUM_BAUDS; i++) {
        /* Expected bit period for this baud rate */
        uint32_t expected_us = 1000000UL / STANDARD_BAUDS[i];

        /* Absolute error */
        uint32_t error = (bit_period_us > expected_us)
                         ? (bit_period_us - expected_us)
                         : (expected_us   - bit_period_us);

        /* Accept if error is within 5% of expected period */
        if (error < best_error && error < (expected_us / 20)) {
            best_error = error;
            best_baud  = STANDARD_BAUDS[i];
        }
    }
    return best_baud;
}

/* -------------------------------------------------------------------
 * Auto-detect baud rate.
 *
 * @param num_samples  Number of pulses to measure (more = more accurate)
 * @return             Detected baud rate, or 0 on failure
 * ------------------------------------------------------------------- */
uint32_t uart_detect_baud_rate(int num_samples)
{
    uint32_t min_pulse_us = UINT32_MAX;

    for (int i = 0; i < num_samples; i++) {
        uint32_t pulse = measure_pulse_us();

        /*
         * The shortest pulse corresponds to a single bit period
         * (all longer pulses are multiples of the bit period).
         */
        if (pulse < min_pulse_us && pulse > 0) {
            min_pulse_us = pulse;
        }
    }

    if (min_pulse_us == UINT32_MAX) return 0;

    return match_baud_rate(min_pulse_us);
}

/* -------------------------------------------------------------------
 * Example: Adaptive receiver initialization
 * ------------------------------------------------------------------- */
void uart_adaptive_init(void)
{
    uint32_t baud = uart_detect_baud_rate(20);  /* Sample 20 pulses */

    if (baud == 0) {
        /* Fall back to default */
        baud = 9600;
    }

    /* Configure hardware UART with detected baud rate */
    /* uart_hw_set_baud(baud); */
    (void)baud;
}
```

---

### 4. Hardware UART Clock Recovery Configuration (STM32 HAL, C)

This example shows how to configure the hardware UART peripheral on an STM32 microcontroller to use its built-in 16× oversampling clock recovery:

```c
/**
 * stm32_uart_clock_recovery.c
 *
 * STM32 HAL-based UART configuration with:
 *   - 16x oversampling (default, best noise immunity)
 *   -  8x oversampling (alternative, allows higher baud at lower CPU freq)
 *   - Auto baud rate detection using the hardware ABR unit
 */

#include "stm32f4xx_hal.h"

UART_HandleTypeDef huart2;

/* -------------------------------------------------------------------
 * Configure UART2 with 16x oversampling clock recovery (recommended)
 * ------------------------------------------------------------------- */
void uart_init_16x_oversampling(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 9600;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;

    /*
     * UART_OVERSAMPLING_16:
     *   - Samples the bit at positions 8, 9, 10 (of 16)
     *   - Uses 2-of-3 majority vote for noise rejection
     *   - Maximum baud rate = PCLK / 16
     *   - Tolerates up to ±3.75% clock error
     */
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    HAL_UART_Init(&huart2);
}

/* -------------------------------------------------------------------
 * Configure UART2 with 8x oversampling
 * Use when baud rate is too high for 16x at available PCLK.
 * ------------------------------------------------------------------- */
void uart_init_8x_oversampling(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;

    /*
     * UART_OVERSAMPLING_8:
     *   - Samples at positions 4, 5, 6 (of 8)
     *   - Maximum baud rate = PCLK / 8  (doubles achievable baud rate)
     *   - Less noise rejection, tighter clock tolerance (±1.875%)
     */
    huart2.Init.OverSampling = UART_OVERSAMPLING_8;

    /*
     * OneBitSampling: sample only at the exact center — disable majority vote.
     * Use only when the channel is very clean (short PCB traces, no RF noise).
     */
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;

    HAL_UART_Init(&huart2);
}

/* -------------------------------------------------------------------
 * Configure Auto Baud Rate Detection (STM32F0/F3/F7/H7 series)
 *
 * The hardware measures the duration of the first received character's
 * start bit and auto-configures the BRR register.
 * ------------------------------------------------------------------- */
void uart_init_auto_baud(void)
{
    UART_AdvFeatureInitTypeDef adv = {0};

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 9600;   /* Initial guess, will be overridden */
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    /*
     * UART_ADVFEATURE_AUTOBAUDRATE_ONSTARTBIT:
     *   Measures the start bit duration of the first received byte.
     *
     * UART_ADVFEATURE_AUTOBAUDRATE_ONFALLINGEDGE:
     *   Measures from the first falling edge to the next falling edge.
     */
    adv.AdvFeatureInit      = UART_ADVFEATURE_AUTOBAUDRATE_INIT;
    adv.AutoBaudRateEnable  = UART_ADVFEATURE_AUTOBAUDRATE_ENABLE;
    adv.AutoBaudRateMode    = UART_ADVFEATURE_AUTOBAUDRATE_ONSTARTBIT;
    huart2.AdvancedInit     = adv;

    HAL_UART_Init(&huart2);

    /* Wait for auto-baud lock */
    while (HAL_IS_BIT_CLR(huart2.Instance->ISR, USART_ISR_ABRF)) {
        /* Spin until Auto Baud Rate Flag is set */
    }
}
```

---

## Implementation in Rust

### 1. Software UART with 16× Oversampling Clock Recovery

```rust
//! uart_clock_recovery.rs
//!
//! Software UART receiver with 16× oversampling clock recovery in Rust.
//! Designed for `no_std` embedded targets (Cortex-M, RISC-V, etc.)
//! using embedded-hal traits for GPIO and time.

#![no_std]

use embedded_hal::digital::v2::InputPin;
use embedded_hal::blocking::delay::DelayUs;

/// Number of oversamples per bit period.
const OVERSAMPLE: u32 = 16;

/// Phase of bit center within an oversampled bit period.
const HALF_BIT_TICKS: u32 = OVERSAMPLE / 2; // 8

/// UART frame error type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UartError {
    /// Timeout waiting for start bit.
    Timeout,
    /// Start bit was not LOW at center.
    StartBitError,
    /// Stop bit was not HIGH.
    StopBitError,
    /// GPIO read error.
    PinError,
}

/// Software UART receiver with 16× oversampling clock recovery.
pub struct SoftUartRx<PIN, DELAY> {
    pin:        PIN,
    delay:      DELAY,
    sample_us:  u32,  // Microseconds per oversample tick
}

impl<PIN, DELAY, E> SoftUartRx<PIN, DELAY>
where
    PIN:   InputPin<Error = E>,
    DELAY: DelayUs<u32>,
{
    /// Create a new software UART receiver.
    ///
    /// # Arguments
    /// * `pin`      - GPIO input pin connected to UART RX.
    /// * `delay`    - Platform delay provider.
    /// * `baud`     - Target baud rate (e.g., 9600).
    pub fn new(pin: PIN, delay: DELAY, baud: u32) -> Self {
        let sample_us = 1_000_000 / (baud * OVERSAMPLE);
        SoftUartRx { pin, delay, sample_us }
    }

    /// Sample the pin once.
    fn sample(&mut self) -> Result<bool, UartError> {
        self.pin.is_high().map_err(|_| UartError::PinError)
    }

    /// 3-of-3 majority vote: take 3 consecutive samples, return dominant bit.
    fn majority_vote(&mut self) -> Result<bool, UartError> {
        let s0 = self.sample()? as u8;
        self.delay.delay_us(self.sample_us);
        let s1 = self.sample()? as u8;
        self.delay.delay_us(self.sample_us);
        let s2 = self.sample()? as u8;

        Ok((s0 + s1 + s2) >= 2)
    }

    /// Wait for a falling edge (start bit detection).
    ///
    /// Returns `Ok(())` when a falling edge is detected,
    /// or `Err(UartError::Timeout)` after `timeout_ticks` ticks.
    fn wait_for_start_bit(&mut self, timeout_ticks: u32) -> Result<(), UartError> {
        let mut ticks = 0u32;

        // Wait for idle HIGH
        loop {
            if self.sample()? {
                break;
            }
            self.delay.delay_us(self.sample_us);
            ticks += 1;
            if ticks >= timeout_ticks {
                return Err(UartError::Timeout);
            }
        }

        // Wait for falling edge (start bit LOW)
        ticks = 0;
        loop {
            if !self.sample()? {
                return Ok(()); // Falling edge found — t₀ established
            }
            self.delay.delay_us(self.sample_us);
            ticks += 1;
            if ticks >= timeout_ticks {
                return Err(UartError::Timeout);
            }
        }
    }

    /// Receive one UART byte (8N1 format).
    ///
    /// Uses 16× oversampling with majority-vote sampling for noise immunity.
    ///
    /// # Returns
    /// The received byte, or a `UartError` on framing failure or timeout.
    pub fn receive_byte(&mut self) -> Result<u8, UartError> {
        // Timeout = 100 ms expressed in sample ticks
        let timeout_ticks = 100_000 / self.sample_us;

        // --- Step 1: Detect start bit falling edge ---
        self.wait_for_start_bit(timeout_ticks)?;

        // t₀ = now.  Advance to center of start bit.
        // majority_vote() consumes 3 ticks; wait (HALF_BIT_TICKS - 1) before it.
        self.delay.delay_us(self.sample_us * (HALF_BIT_TICKS - 1));

        // --- Step 2: Verify start bit is LOW at center ---
        let start_center = self.majority_vote()?;
        if start_center {
            return Err(UartError::StartBitError);
        }

        // --- Step 3: Sample 8 data bits ---
        let mut byte: u8 = 0;
        for i in 0..8u8 {
            // Advance one full bit period (minus 3 ticks consumed by majority_vote)
            self.delay.delay_us(self.sample_us * (OVERSAMPLE - 3));

            let bit = self.majority_vote()?;
            if bit {
                byte |= 1 << i; // LSB first
            }
        }

        // --- Step 4: Sample stop bit ---
        self.delay.delay_us(self.sample_us * (OVERSAMPLE - 3));
        let stop = self.majority_vote()?;
        if !stop {
            return Err(UartError::StopBitError);
        }

        Ok(byte)
    }

    /// Receive `n` bytes into a buffer.
    pub fn receive_bytes(&mut self, buf: &mut [u8]) -> Result<(), UartError> {
        for slot in buf.iter_mut() {
            *slot = self.receive_byte()?;
        }
        Ok(())
    }
}
```

---

### 2. DPLL Clock Recovery in Rust

```rust
//! uart_dpll.rs
//!
//! Digital Phase-Locked Loop (DPLL) for UART clock recovery.
//! Tracks transmitter clock phase and steers the receiver sample
//! point to compensate for crystal drift.

/// DPLL phase correction step (tune for your clock accuracy).
const PHASE_STEP: i32 = 2;

/// UART DPLL clock recovery state.
#[derive(Debug, Clone)]
pub struct Dpll {
    /// Phase accumulator (0 .. OVERSAMPLE-1).
    /// Increments once per oversampled tick.
    phase_acc: i32,

    /// Previous raw bit value (for edge detection).
    prev_bit: u8,

    /// Total bits sampled (for statistics/diagnostics).
    pub bit_count: u64,

    /// Accumulated phase correction applied (diagnostics).
    pub total_correction: i64,

    /// Oversampling ratio.
    oversample: i32,
}

impl Dpll {
    /// Create a new DPLL for the given oversampling ratio (e.g., 16).
    pub fn new(oversample: u32) -> Self {
        Dpll {
            phase_acc:        0,
            prev_bit:         1, // Idle line = HIGH
            bit_count:        0,
            total_correction: 0,
            oversample:       oversample as i32,
        }
    }

    /// Phase at which we sample (center of bit).
    fn phase_center(&self) -> i32 {
        self.oversample / 2
    }

    /// Feed one oversampled raw bit into the DPLL.
    ///
    /// Call this at exactly `oversample × baud_rate` Hz.
    ///
    /// # Returns
    /// `Some(bit_value)` if this tick is the sampling point (bit center),
    /// `None` otherwise.
    pub fn tick(&mut self, raw_bit: u8) -> Option<u8> {
        // --- Advance phase accumulator ---
        self.phase_acc += 1;
        if self.phase_acc >= self.oversample {
            self.phase_acc -= self.oversample;
        }

        // --- Edge-triggered phase correction ---
        if raw_bit != self.prev_bit {
            /*
             * A transition ideally occurs at phase_acc == 0 (bit boundary).
             * Current phase_acc is the phase error (how far past boundary we are).
             *
             * phase_acc < phase_center → transition early → receiver is fast → slow down
             * phase_acc ≥ phase_center → transition late  → receiver is slow → speed up
             */
            let error = self.phase_acc;

            let correction = if error < self.phase_center() {
                // Receiver is ahead of transmitter: subtract
                -PHASE_STEP
            } else {
                // Receiver is behind transmitter: add
                PHASE_STEP
            };

            self.phase_acc += correction;
            // Keep phase_acc in [0, oversample)
            if self.phase_acc < 0 {
                self.phase_acc += self.oversample;
            } else if self.phase_acc >= self.oversample {
                self.phase_acc -= self.oversample;
            }

            self.total_correction += correction as i64;
            self.prev_bit = raw_bit;
        }

        // --- Emit sample at bit center ---
        if self.phase_acc == self.phase_center() {
            self.bit_count += 1;
            Some(raw_bit)
        } else {
            None
        }
    }

    /// Reset DPLL to initial state (call between frames if needed).
    pub fn reset(&mut self) {
        self.phase_acc = 0;
        self.prev_bit  = 1;
    }

    /// Estimated clock drift in parts-per-million (diagnostic).
    /// Positive = transmitter faster than receiver.
    pub fn estimated_drift_ppm(&self) -> i64 {
        if self.bit_count == 0 {
            return 0;
        }
        // Each PHASE_STEP correction = 1/oversample of a bit period
        // Over bit_count bits, total_correction / (oversample * bit_count)
        // in PPM: × 1_000_000
        (self.total_correction * 1_000_000)
            / (self.oversample as i64 * self.bit_count as i64)
    }
}

// ---------------------------------------------------------------------------
// UART frame receiver built on DPLL
// ---------------------------------------------------------------------------

/// UART framing errors.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FrameError {
    StartBit,
    StopBit,
    Incomplete,
}

/// Collect DPLL-sampled bits into a complete 8N1 UART frame.
pub struct FrameCollector {
    bits:   [u8; 10],  // [start, d0, d1, d2, d3, d4, d5, d6, d7, stop]
    count:  usize,
    armed:  bool,      // True once start bit detected
}

impl FrameCollector {
    pub fn new() -> Self {
        FrameCollector {
            bits:  [1; 10], // Default to HIGH (idle)
            count: 0,
            armed: false,
        }
    }

    /// Feed a sampled bit (output of `Dpll::tick`).
    ///
    /// # Returns
    /// `Some(Ok(byte))` → complete, valid frame.
    /// `Some(Err(e))`   → framing error.
    /// `None`           → frame not yet complete.
    pub fn push(&mut self, bit: u8) -> Option<Result<u8, FrameError>> {
        if !self.armed {
            // Wait for start bit (LOW)
            if bit == 0 {
                self.armed  = true;
                self.count  = 0;
                self.bits[self.count] = bit;
                self.count += 1;
            }
            return None;
        }

        self.bits[self.count] = bit;
        self.count += 1;

        if self.count == 10 {
            // Frame complete — validate and decode
            self.armed = false;
            self.count = 0;
            return Some(self.decode());
        }

        None
    }

    fn decode(&self) -> Result<u8, FrameError> {
        if self.bits[0] != 0 {
            return Err(FrameError::StartBit);
        }
        if self.bits[9] != 1 {
            return Err(FrameError::StopBit);
        }

        let mut byte: u8 = 0;
        for i in 0..8 {
            byte |= self.bits[1 + i] << i;
        }
        Ok(byte)
    }
}
```

---

### 3. Baud Rate Auto-Detection in Rust

```rust
//! uart_baud_detect.rs
//!
//! Auto-detects UART baud rate by finding the minimum pulse width
//! in the incoming data stream.

use embedded_hal::digital::v2::InputPin;
use embedded_hal::blocking::delay::DelayUs;

/// Standard UART baud rates to match against.
const STANDARD_BAUDS: &[u32] = &[
    1200, 2400, 4800, 9600, 14400, 19200,
    28800, 38400, 57600, 115200, 230400, 460800, 921600,
];

/// Maximum allowed clock error: 5% of the expected bit period.
const MAX_ERROR_PERCENT: u32 = 5;

/// Find the standard baud rate closest to a measured bit period.
///
/// # Arguments
/// * `bit_period_us` - Measured bit period in microseconds.
///
/// # Returns
/// Matched baud rate, or `None` if no match within tolerance.
pub fn match_baud_rate(bit_period_us: u32) -> Option<u32> {
    STANDARD_BAUDS
        .iter()
        .filter_map(|&baud| {
            let expected_us = 1_000_000 / baud;
            let error = bit_period_us.abs_diff(expected_us);
            let tolerance = expected_us / (100 / MAX_ERROR_PERCENT);

            if error <= tolerance {
                Some((error, baud))
            } else {
                None
            }
        })
        .min_by_key(|(err, _)| *err)
        .map(|(_, baud)| baud)
}

/// UART baud rate detector.
pub struct BaudDetector<PIN, DELAY> {
    pin:   PIN,
    delay: DELAY,
    /// Polling interval in microseconds (should be much shorter than one bit period).
    poll_us: u32,
}

impl<PIN, DELAY, E> BaudDetector<PIN, DELAY>
where
    PIN:   InputPin<Error = E>,
    DELAY: DelayUs<u32>,
{
    /// Create a new baud rate detector.
    ///
    /// # Arguments
    /// * `pin`     - GPIO pin connected to the UART RX line.
    /// * `delay`   - Delay provider.
    /// * `poll_us` - Polling interval (e.g., 1 µs for accurate measurements).
    pub fn new(pin: PIN, delay: DELAY, poll_us: u32) -> Self {
        BaudDetector { pin, delay, poll_us }
    }

    /// Measure the width of the next logic pulse.
    ///
    /// Returns the pulse width in microseconds.
    fn measure_pulse_us(&mut self) -> u32 {
        // Wait for transition
        let level_before = self.pin.is_high().unwrap_or(true);
        loop {
            if self.pin.is_high().unwrap_or(true) != level_before {
                break;
            }
            self.delay.delay_us(self.poll_us);
        }

        // Measure duration of current level
        let current_level = !level_before;
        let mut count: u32 = 0;
        loop {
            if self.pin.is_high().unwrap_or(true) != current_level {
                break;
            }
            self.delay.delay_us(self.poll_us);
            count += 1;
        }

        count * self.poll_us
    }

    /// Detect the baud rate by measuring `num_samples` pulses.
    ///
    /// The minimum pulse width corresponds to one bit period.
    ///
    /// # Returns
    /// Detected baud rate wrapped in `Some`, or `None` on failure.
    pub fn detect(&mut self, num_samples: usize) -> Option<u32> {
        let min_pulse = (0..num_samples)
            .map(|_| self.measure_pulse_us())
            .filter(|&p| p > 0)
            .min()?;

        match_baud_rate(min_pulse)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_match_baud_rate_exact() {
        // 9600 baud → 104.17 µs → rounds to 104
        assert_eq!(match_baud_rate(104), Some(9600));
    }

    #[test]
    fn test_match_baud_rate_within_tolerance() {
        // 5% tolerance: 9600 baud ±5.2 µs
        assert_eq!(match_baud_rate(100), Some(9600));
        assert_eq!(match_baud_rate(109), Some(9600));
    }

    #[test]
    fn test_match_baud_rate_115200() {
        // 115200 baud → 8.68 µs → rounds to 9
        assert_eq!(match_baud_rate(9), Some(115200));
    }

    #[test]
    fn test_match_baud_rate_no_match() {
        // 50 µs → 20000 baud → no standard match
        assert_eq!(match_baud_rate(50), None);
    }
}
```

---

## Error Handling and Edge Cases

### Common Clock Recovery Failures and Mitigations

| Problem | Cause | Mitigation |
|---|---|---|
| **Framing errors** | Clock drift > ±5% over one frame | Use crystal oscillator; verify baud rate accuracy |
| **Start bit noise** | Glitch detected as falling edge | Require LOW to persist for ≥ N/2 samples before arming |
| **Sampling at wrong phase** | Edge detection race condition | Lock out phase corrections mid-bit |
| **Missed start bit** | DMA/ISR latency | Use hardware UART or edge-triggered interrupt |
| **Drift accumulation** | Long frames or continuous transmission | Apply DPLL; re-sync on each start bit |
| **False baud rate match** | Short noise spike measured as minimum pulse | Require minimum count of consistent measurements |

### Clock Accuracy Requirements

For standard 8N1 UART framing, the theoretical maximum total clock error (transmitter + receiver combined) is:

```
Max error = 0.5 bit period / (total bits to last sample)
          = 0.5 / 9.5   (sampling at center of bit 9 = stop bit)
          ≈ 5.26%
```

In practice, to have a comfortable margin:
- **Hardware UART with 16× oversampling:** keep combined error below **±2%**
- **Software UART with DPLL:** effective tolerance improves to **±3–4%**

---

## Summary

UART Clock Recovery is the mechanism by which an asynchronous receiver extracts the bit timing of an incoming data stream from the data itself, without a shared clock signal.

**Key concepts:**

- **The start bit falling edge** is the universal synchronization event. Every UART frame begins here, and all subsequent bit-sample times are calculated relative to this edge.

- **Oversampling** (typically 16×) allows fine-grained alignment. The receiver samples the line at `N × baud_rate`, detects the start edge, waits `N/2` samples to reach bit center, then samples every `N` samples thereafter. Most hardware UART peripherals (STM32, AVR, PIC, ESP32) implement this directly in silicon.

- **Majority voting** (2-of-3 or 3-of-3 samples near bit center) significantly reduces sensitivity to line noise and signal integrity problems.

- **The DPLL** extends the basic approach for continuous or long streams. It nudges the phase accumulator toward the transmitter's clock phase whenever a data transition is observed, continuously compensating for slow crystal drift (typically 10–100 ppm between typical crystals). This reduces the accumulated error across a long burst.

- **Baud rate auto-detection** measures the shortest pulse duration in the incoming data stream and matches it to a known standard baud rate. This is useful for adaptive interfaces or when the baud rate is not known at design time.

- **Typical clock tolerance** for UART is ±2–5%. As long as the combined transmitter-receiver crystal error stays within this window, reliable communication is achievable without explicit synchronization signals.

For most embedded applications, hardware UART with 16× oversampling is the appropriate choice. Software UART with DPLL is relevant for microcontrollers with too few UART peripherals or for bit-banging on arbitrary GPIO pins. Baud rate auto-detection is useful in universal interfaces and diagnostic tools.

---

*Document: 73. Clock Recovery — UART Topics Series*