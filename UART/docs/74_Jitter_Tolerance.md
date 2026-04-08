# 74. UART Jitter Tolerance

**Theory** — explains what jitter is, its five distinct types (random, deterministic, periodic, data-dependent, total), and the five root causes in UART systems (oscillator quality, IRQ latency, power supply noise, PCB effects, temperature drift).

**Math** — derives the cumulative timing error formula, the 4% combined-error budget for a 10-bit frame, and a tolerance table across baud rates from 9600 to 3 Mbps.

**C/C++ code examples:**
- Jitter-tolerant software UART with 3× oversampling and majority voting
- Adaptive IIR sample-point corrector with auto-recalibration trigger
- STM32 HAL hardware UART error monitor (framing/noise/overrun flags)
- Host-side jitter budget calculator with pass/fail reporting

**Rust code examples:**
- Rolling `JitterStats` tracker with mean, peak, and RMS
- IIR-filtered `AdaptiveSamplePoint` corrector (no_std compatible)
- Generic `majority_vote_sample` and `receive_byte_majority` functions
- `JitterConfig::analyze()` budget calculator with unit tests
- `UartMonitor` with Normal/Warning/Critical alarm levels

**Summary** ties everything together with concise takeaways for hardware selection, software mitigation, and the limits of each approach at high baud rates.

## Handling Clock Jitter and Timing Variations

---

## Table of Contents

1. [Introduction](#introduction)
2. [What Is Clock Jitter?](#what-is-clock-jitter)
3. [Sources of Jitter in UART Systems](#sources-of-jitter-in-uart-systems)
4. [Jitter Tolerance Fundamentals](#jitter-tolerance-fundamentals)
5. [Measurement and Quantification](#measurement-and-quantification)
6. [Mitigation Strategies](#mitigation-strategies)
7. [C/C++ Implementation](#cc-implementation)
8. [Rust Implementation](#rust-implementation)
9. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) communication is inherently sensitive to timing accuracy because it lacks a shared clock signal between transmitter and receiver. Both ends must independently maintain their own baud-rate clocks and agree on timing closely enough to sample each bit correctly. **Jitter** — the deviation of a clock edge from its ideal position — is one of the most significant sources of communication errors in UART systems and must be carefully understood, measured, and mitigated.

This document covers the theory of jitter in UART contexts, practical measurement approaches, and concrete implementation techniques in C/C++ and Rust to detect and compensate for timing variations.

---

## What Is Clock Jitter?

**Clock jitter** is the short-term variation in the period of a periodic signal. In a UART context, jitter manifests as:

- Variation in the time between successive bit boundaries
- Deviation of a sample point from the center of a bit period
- Cumulative drift across multiple bits within a single frame

Jitter is distinct from **frequency offset** (a fixed deviation of baud rate from nominal) and **frequency drift** (a slow, temperature/aging-related change). Jitter is random or pseudo-random in nature, occurring cycle-to-cycle.

### Types of Jitter

| Type | Description | UART Impact |
|------|-------------|-------------|
| **Random Jitter (RJ)** | Gaussian-distributed, caused by thermal noise | Contributes to long-term bit errors |
| **Deterministic Jitter (DJ)** | Bounded, caused by EMI, crosstalk, power supply noise | Creates repeatable error patterns |
| **Periodic Jitter (PJ)** | Sinusoidal variation, often caused by switching power supplies | Predictable but hard to filter at unknown frequencies |
| **Data-Dependent Jitter (DDJ)** | Varies with bit pattern (ISI) | Especially problematic in high-speed UART |
| **Total Jitter (TJ)** | RJ + DJ combined | The practical specification limit |

---

## Sources of Jitter in UART Systems

### 1. Oscillator Quality

The most fundamental source of jitter is the clock source used to generate the baud rate. Crystal oscillators exhibit lower jitter than RC oscillators. Typical phase noise figures:

- **Crystal oscillator (XTAL):** ±20–50 ppm
- **RC oscillator (internal MCU):** ±1–3% (10,000–30,000 ppm)
- **PLL-derived clock:** ±100–500 ppm, plus PLL jitter

For UART at 115200 baud, a single bit period is approximately 8.68 µs. A 1% frequency error means the receiver samples about 87 ns off-center per bit — cumulative over a 10-bit frame, this reaches 870 ns, which is 10% of a bit period and approaches the tolerance limit.

### 2. Interrupt and ISR Latency Jitter

Software-driven UART implementations that rely on timer interrupts experience jitter from:

- Variable interrupt service latency (other IRQs masking the UART IRQ)
- Instruction pipeline stalls and cache misses
- Context-switch overhead in RTOS environments

### 3. Power Supply Noise

Switching regulators induce voltage ripple on VDD, which modulates the switching threshold of digital logic, effectively shifting edge timing. At 100 kHz switching frequency with a UART at 115200 baud, the modulation period can cause periodic jitter of several nanoseconds.

### 4. PCB and Signal Integrity Effects

- Transmission-line reflections on long UART traces
- Capacitive loading from multiple receivers on a bus
- Ground plane discontinuities causing return current jitter
- Crosstalk from adjacent high-speed signals

### 5. Temperature and Aging

Both oscillator frequency and transistor switching thresholds vary with temperature, introducing slow drift that accumulates over time. This is usually addressed through calibration rather than jitter-tolerance design.

---

## Jitter Tolerance Fundamentals

### The UART Sampling Window

A UART receiver must sample each incoming bit at its center. With a 16× oversampled receiver (common in hardware UARTs), the sampling window is:

```
Ideal sample point = start_of_bit + (bit_period / 2)
Acceptable range  = ±(bit_period / 2) * (1 - guard_band)
```

The **guard band** accounts for inter-symbol interference and receiver logic propagation delay. A typical guard band of 10–20% means the receiver can tolerate up to 30–40% total timing error (±15–20% per edge).

### Cumulative Timing Error in a UART Frame

UART frames are 10 bits minimum (1 start + 8 data + 1 stop). The receiver re-synchronizes on the **falling edge of the start bit** and then counts bit periods independently. The maximum cumulative error at the stop bit is:

```
Total error = (baud_rate_error_TX + baud_rate_error_RX) × N_bits × bit_period
```

For reliable operation:

```
(error_TX + error_RX) < (0.5 - guard) / N_bits
```

For N=10 bits and 10% guard:

```
Combined error budget = (0.5 - 0.10) / 10 = 4% per bit period
```

This means the sum of TX and RX frequency errors must remain below 4%. With matched crystals at ±50 ppm each, the combined error is 100 ppm — well within budget. With RC oscillators at ±3% each, the combined error of 6% **exceeds** the budget, making calibration or oversampling essential.

### Oversampling and Majority Voting

Hardware UARTs typically oversample at 16× or 8× the baud rate. The receiver takes three or more samples near the center of a bit period and uses majority voting to reject glitches:

```
Bit value = majority(sample[6], sample[7], sample[8])  // for 16× oversampling
```

This provides immunity to single-sample noise events and effectively reduces the impact of small jitter deviations.

---

## Measurement and Quantification

### Jitter Measurement Approaches

**Time-domain measurement:** Use a digital oscilloscope with jitter analysis to measure the histogram of edge timing deviations. The standard deviation gives RMS jitter; the peak-to-peak value gives the deterministic jitter bounds.

**Eye diagram:** Plot all bit periods superimposed. The eye opening width indicates the jitter tolerance margin. A closed eye indicates jitter that exceeds the bit period tolerance.

**Phase noise measurement:** Use a spectrum analyzer to measure phase noise sidebands around the carrier. Convert to time-domain jitter using:

```
RMS_jitter = (1 / (2π × f_carrier)) × 10^(phase_noise_dBc/20)
```

### Practical Tolerance Limits

| Baud Rate | Bit Period | Max Total Error (4%) | Max Jitter (RMS, 3σ) |
|-----------|-----------|----------------------|----------------------|
| 9600 | 104.2 µs | 4.17 µs | 1.39 µs |
| 115200 | 8.68 µs | 347 ns | 116 ns |
| 921600 | 1.09 µs | 43 ns | 14 ns |
| 3000000 | 333 ns | 13 ns | 4.4 ns |

At high baud rates, even PCB trace mismatch introduces jitter that consumes a significant portion of this budget.

---

## Mitigation Strategies

### Hardware Strategies

1. **Use crystal oscillators** in preference to RC oscillators wherever baud-rate accuracy is critical.
2. **PLL filtering:** Choose PLL loop bandwidth to attenuate reference jitter above the baud rate.
3. **Ferrite beads and decoupling capacitors** on VDD near oscillator circuits reduce power-supply-induced jitter.
4. **Controlled impedance traces** and proper termination eliminate reflection-induced DDJ.
5. **Isolated ground planes** under the UART transceiver prevent return-current crosstalk.

### Software Strategies

1. **Baud rate calibration:** Measure actual oscillator frequency at startup (or periodically) using a reference signal (GPS PPS, network time, RTC) and correct the baud rate divider.
2. **Adaptive sampling:** Adjust the sample point dynamically based on measured edge positions (used in bit-bang UART implementations).
3. **Frame error detection:** Monitor the stop bit; a framing error indicates accumulated jitter or baud rate mismatch. Use this as feedback for auto-baud detection.
4. **Error counters and backoff:** Track UART error rates and trigger recalibration or baud-rate renegotiation when thresholds are exceeded.
5. **Oversampling in software:** For bit-bang UART, sample each bit 3–5 times and use majority vote to reduce jitter sensitivity.

---

## C/C++ Implementation

### 1. Jitter-Tolerant Software UART with Majority Voting

This implementation targets ARM Cortex-M microcontrollers. It uses a high-frequency timer (oversampled at 3× per bit) and majority voting to reject jitter.

```c
/**
 * uart_jitter.c
 * Software UART with 3x oversampling and majority voting for jitter tolerance.
 * Target: ARM Cortex-M (e.g., STM32)
 */

#include <stdint.h>
#include <stdbool.h>
#include "systick.h"   // Platform timer abstraction

#define BAUD_RATE        9600UL
#define CPU_FREQ         72000000UL
#define OVERSAMPLE       3          // Samples per bit
#define BIT_TICKS        (CPU_FREQ / BAUD_RATE)
#define HALF_BIT_TICKS   (BIT_TICKS / 2)
#define SAMPLE_TICKS     (BIT_TICKS / OVERSAMPLE)

/* Jitter statistics (rolling window) */
typedef struct {
    int32_t  samples[64];
    uint32_t head;
    int32_t  mean;
    int32_t  variance;
    int32_t  peak_pos;
    int32_t  peak_neg;
} JitterStats;

static JitterStats g_jitter = {0};

/**
 * Update jitter statistics with a new edge timing deviation (in timer ticks).
 */
void jitter_update(JitterStats *js, int32_t deviation)
{
    int32_t oldest = js->samples[js->head];
    js->samples[js->head] = deviation;
    js->head = (js->head + 1) & 63;

    /* Incremental mean update */
    js->mean += (deviation - oldest) / 64;

    if (deviation > js->peak_pos) js->peak_pos = deviation;
    if (deviation < js->peak_neg) js->peak_neg = deviation;
}

/**
 * Read one UART byte using 3x oversampling and majority voting.
 * @param rx_pin  Function pointer returning current state of RX pin (0 or 1)
 * @param byte_out Output byte
 * @return true on success, false on framing error
 */
bool uart_read_byte_jitter_tolerant(uint8_t (*rx_pin)(void), uint8_t *byte_out)
{
    uint8_t byte = 0;
    uint32_t t_start;

    /* Wait for start bit falling edge */
    while (rx_pin() != 0);

    /* Record start time and wait to center of first data bit */
    t_start = systick_now();

    /* Confirm start bit is valid (not a glitch): sample at 1/3 and 2/3 */
    systick_delay_ticks(BIT_TICKS / 3);
    if (rx_pin() != 0) return false;   /* Glitch — abort */

    systick_delay_ticks(BIT_TICKS / 3);
    if (rx_pin() != 0) return false;   /* Glitch — abort */

    /* Move to center of bit 0 (LSB) */
    systick_delay_ticks(BIT_TICKS / 6 + BIT_TICKS / 2);

    for (int bit = 0; bit < 8; bit++) {
        uint8_t s[3];

        /* Three samples around the center of the bit */
        uint32_t t_center = t_start + HALF_BIT_TICKS + (bit + 1) * BIT_TICKS;

        systick_delay_until(t_center - SAMPLE_TICKS);
        s[0] = rx_pin();

        systick_delay_until(t_center);
        s[1] = rx_pin();

        systick_delay_until(t_center + SAMPLE_TICKS);
        s[2] = rx_pin();

        /* Majority vote */
        uint8_t bit_val = (s[0] + s[1] + s[2]) >= 2 ? 1 : 0;
        byte |= (bit_val << bit);

        /* Update jitter stats based on first/last edge detected */
        /* (simplified: in production, use edge-capture timer) */
    }

    /* Sample stop bit */
    uint32_t t_stop = t_start + HALF_BIT_TICKS + 9 * BIT_TICKS;
    systick_delay_until(t_stop);

    if (rx_pin() == 0) {
        /* Framing error — stop bit not high */
        return false;
    }

    *byte_out = byte;
    return true;
}
```

---

### 2. Adaptive Sample-Point Correction

When the receiver detects timing drift (e.g., via measured edge positions), it can dynamically adjust the sample point.

```c
/**
 * uart_adaptive.c
 * Adaptive sample-point correction using measured edge positions.
 */

#include <stdint.h>
#include <stdlib.h>

#define MAX_BIT_ERROR_PPM   5000   /* Trigger recal if error exceeds 0.5% */
#define ALPHA               8      /* IIR filter coefficient (1/2^ALPHA) */

typedef struct {
    int32_t  offset_ticks;     /* Current sample offset from nominal center */
    uint32_t baud_ticks;       /* Ticks per bit at current correction */
    uint32_t error_count;      /* Framing errors since last calibration */
    uint32_t byte_count;       /* Total bytes received */
    int32_t  iir_accumulator;  /* IIR filter state for edge timing */
} AdaptiveUART;

static AdaptiveUART g_adapt = {
    .offset_ticks    = 0,
    .baud_ticks      = BIT_TICKS,
    .error_count     = 0,
    .byte_count      = 0,
    .iir_accumulator = 0,
};

/**
 * Update the IIR filter with a new edge timing measurement.
 * @param measured_edge  Timer value at which the start-bit falling edge was detected
 * @param expected_edge  Timer value at which it was expected
 */
void adaptive_update_edge(AdaptiveUART *ua, int32_t measured_edge, int32_t expected_edge)
{
    int32_t error = measured_edge - expected_edge;

    /* IIR low-pass: accumulator += (error - accumulator) >> ALPHA */
    ua->iir_accumulator += (error - ua->iir_accumulator) >> ALPHA;

    /* Apply filtered offset to next sample point */
    ua->offset_ticks = ua->iir_accumulator / 2;

    /* Clamp to ±25% of a bit period to prevent runaway */
    int32_t max_offset = (int32_t)(ua->baud_ticks / 4);
    if (ua->offset_ticks >  max_offset) ua->offset_ticks =  max_offset;
    if (ua->offset_ticks < -max_offset) ua->offset_ticks = -max_offset;
}

/**
 * Report a framing error and decide whether to recalibrate.
 * Returns true if baud rate should be re-negotiated.
 */
bool adaptive_report_error(AdaptiveUART *ua)
{
    ua->error_count++;
    ua->byte_count++;

    /* If error rate exceeds 1%, trigger recalibration */
    if (ua->byte_count > 100) {
        uint32_t error_ppm = (ua->error_count * 1000000UL) / ua->byte_count;
        if (error_ppm > MAX_BIT_ERROR_PPM) {
            ua->error_count = 0;
            ua->byte_count  = 0;
            ua->iir_accumulator = 0;
            ua->offset_ticks    = 0;
            return true;   /* Signal caller to recalibrate / retry auto-baud */
        }
    }
    return false;
}

/**
 * Auto-baud: measure the width of the start bit to estimate baud rate.
 * Assumes the first received character is 'U' (0x55) or 0xFF with known pattern.
 */
uint32_t autobaud_measure(uint8_t (*rx_pin)(void), uint32_t timer_freq_hz)
{
    /* Wait for falling edge (start bit) */
    while (rx_pin() != 0);
    uint32_t t0 = systick_now();

    /* Wait for rising edge (end of start bit / first '1' bit) */
    while (rx_pin() == 0);
    uint32_t t1 = systick_now();

    uint32_t start_bit_ticks = t1 - t0;
    uint32_t measured_baud   = timer_freq_hz / start_bit_ticks;

    return measured_baud;
}
```

---

### 3. Hardware UART Jitter Monitoring (STM32 HAL)

This example monitors the hardware UART error flags and accumulates statistics to detect jitter-induced degradation.

```c
/**
 * uart_monitor.c
 * Hardware UART error monitoring for jitter detection on STM32.
 */

#include "stm32f4xx_hal.h"
#include <string.h>

#define MONITOR_WINDOW_SIZE  1000   /* Bytes per monitoring window */
#define JITTER_ALARM_THRESH  10     /* Framing errors per window to raise alarm */

typedef struct {
    uint32_t framing_errors;
    uint32_t noise_errors;
    uint32_t overrun_errors;
    uint32_t parity_errors;
    uint32_t total_bytes;
    bool     jitter_alarm;
} UARTMonitor;

static UARTMonitor g_monitor = {0};

/**
 * Call this from the UART interrupt handler or polling loop.
 * Reads and clears error flags, updates monitor stats.
 */
void uart_monitor_update(UART_HandleTypeDef *huart)
{
    uint32_t sr = huart->Instance->SR;   /* Status register */

    if (sr & USART_SR_FE) {              /* Framing error */
        g_monitor.framing_errors++;
        __HAL_UART_CLEAR_FEFLAG(huart);
    }
    if (sr & USART_SR_NE) {              /* Noise error — often jitter-induced */
        g_monitor.noise_errors++;
        __HAL_UART_CLEAR_NEFLAG(huart);
    }
    if (sr & USART_SR_ORE) {             /* Overrun */
        g_monitor.overrun_errors++;
        __HAL_UART_CLEAR_OREFLAG(huart);
    }
    if (sr & USART_SR_PE) {              /* Parity error */
        g_monitor.parity_errors++;
        __HAL_UART_CLEAR_PEFLAG(huart);
    }

    g_monitor.total_bytes++;

    /* Evaluate alarm condition every window */
    if (g_monitor.total_bytes >= MONITOR_WINDOW_SIZE) {
        uint32_t jitter_errors = g_monitor.framing_errors + g_monitor.noise_errors;
        g_monitor.jitter_alarm = (jitter_errors >= JITTER_ALARM_THRESH);

        /* Reset window */
        memset(&g_monitor, 0, sizeof(g_monitor));
    }
}

/**
 * Check if jitter alarm is active and return current statistics.
 */
bool uart_monitor_check_alarm(uint32_t *framing_out, uint32_t *noise_out)
{
    if (framing_out) *framing_out = g_monitor.framing_errors;
    if (noise_out)   *noise_out   = g_monitor.noise_errors;
    return g_monitor.jitter_alarm;
}

/**
 * Adjust UART oversampling mode to improve jitter tolerance at lower baud rates.
 * STM32 supports OVER8 (8x) and OVER16 (16x) modes.
 * OVER16 gives better jitter tolerance; OVER8 allows higher max baud rate.
 */
void uart_set_oversampling(UART_HandleTypeDef *huart, bool use_over8)
{
    HAL_UART_DeInit(huart);

    if (use_over8) {
        huart->Init.OverSampling = UART_OVERSAMPLING_8;
    } else {
        huart->Init.OverSampling = UART_OVERSAMPLING_16;   /* Better jitter tolerance */
    }

    HAL_UART_Init(huart);
}
```

---

### 4. Jitter Budget Calculator (Host/Test Utility)

```c
/**
 * jitter_budget.c
 * Utility to calculate the jitter tolerance budget for a given UART configuration.
 * Suitable for host-side test tooling or a resource-rich embedded target.
 */

#include <stdio.h>
#include <math.h>

typedef struct {
    uint32_t baud_rate;
    double   tx_freq_error_ppm;   /* Transmitter oscillator error */
    double   rx_freq_error_ppm;   /* Receiver oscillator error */
    uint32_t frame_bits;          /* Total bits in frame (e.g., 10 = 1+8+1) */
    double   guard_band_fraction; /* Guard band as fraction of bit period (e.g., 0.1) */
} JitterBudget;

typedef struct {
    double bit_period_us;
    double max_total_error_us;
    double max_per_bit_error_ppm;
    double actual_combined_ppm;
    double margin_us;
    double margin_fraction;
    bool   compliant;
} JitterAnalysis;

JitterAnalysis jitter_analyze(const JitterBudget *cfg)
{
    JitterAnalysis r = {0};

    r.bit_period_us = 1e6 / cfg->baud_rate;

    /* Maximum allowable timing error at the last bit */
    double usable_window = 0.5 - cfg->guard_band_fraction;
    r.max_total_error_us = r.bit_period_us * usable_window;

    /* Per-bit combined frequency error budget */
    r.max_per_bit_error_ppm = (usable_window / cfg->frame_bits) * 1e6;

    /* Actual combined error */
    r.actual_combined_ppm = cfg->tx_freq_error_ppm + cfg->rx_freq_error_ppm;

    /* Margin */
    double actual_error_us = r.bit_period_us * r.actual_combined_ppm * 1e-6
                             * cfg->frame_bits;
    r.margin_us       = r.max_total_error_us - actual_error_us;
    r.margin_fraction = r.margin_us / r.bit_period_us;
    r.compliant       = (r.margin_us > 0.0);

    return r;
}

void jitter_print_report(const JitterBudget *cfg, const JitterAnalysis *r)
{
    printf("=== UART Jitter Budget Report ===\n");
    printf("Baud rate         : %u bps\n", cfg->baud_rate);
    printf("Bit period        : %.2f us\n", r->bit_period_us);
    printf("Frame bits        : %u\n", cfg->frame_bits);
    printf("TX error          : %.1f ppm\n", cfg->tx_freq_error_ppm);
    printf("RX error          : %.1f ppm\n", cfg->rx_freq_error_ppm);
    printf("Combined error    : %.1f ppm\n", r->actual_combined_ppm);
    printf("Max allowed error : %.2f us (%.0f ppm/bit)\n",
           r->max_total_error_us, r->max_per_bit_error_ppm);
    printf("Margin            : %.2f us (%.1f%% of bit period)\n",
           r->margin_us, r->margin_fraction * 100.0);
    printf("Status            : %s\n", r->compliant ? "COMPLIANT" : "*** VIOLATION ***");
    printf("\n");
}

int main(void)
{
    /* Scenario 1: Crystal-based system at 115200 baud */
    JitterBudget xtal = {
        .baud_rate          = 115200,
        .tx_freq_error_ppm  = 50.0,
        .rx_freq_error_ppm  = 50.0,
        .frame_bits         = 10,
        .guard_band_fraction= 0.10,
    };

    /* Scenario 2: RC oscillator system at 9600 baud */
    JitterBudget rc = {
        .baud_rate          = 9600,
        .tx_freq_error_ppm  = 30000.0,   /* 3% */
        .rx_freq_error_ppm  = 30000.0,   /* 3% */
        .frame_bits         = 10,
        .guard_band_fraction= 0.10,
    };

    JitterAnalysis a1 = jitter_analyze(&xtal);
    JitterAnalysis a2 = jitter_analyze(&rc);

    jitter_print_report(&xtal, &a1);
    jitter_print_report(&rc, &a2);

    return 0;
}
```

**Expected output:**
```
=== UART Jitter Budget Report ===
Baud rate         : 115200 bps
Bit period        : 8.68 us
Frame bits        : 10
TX error          : 50.0 ppm
RX error          : 50.0 ppm
Combined error    : 100.0 ppm
Max allowed error : 3.47 us (40000 ppm/bit)
Margin            : 3.47 us (40.0% of bit period)
Status            : COMPLIANT

=== UART Jitter Budget Report ===
Baud rate         : 9600 bps
Bit period        : 104.17 us
Frame bits        : 10
TX error          : 30000.0 ppm
RX error          : 30000.0 ppm
Combined error    : 60000.0 ppm
Max allowed error : 41.67 us (40000 ppm/bit)
Margin            : -20.83 us (-20.0% of bit period)
Status            : *** VIOLATION ***
```

---

## Rust Implementation

### 1. Jitter Statistics Tracker

```rust
// uart_jitter_stats.rs
// Rolling statistics for UART timing jitter analysis.

use core::sync::atomic::{AtomicI32, Ordering};

const WINDOW: usize = 64;

pub struct JitterStats {
    samples: [i32; WINDOW],
    head: usize,
    pub mean_ticks: i32,
    pub peak_positive: i32,
    pub peak_negative: i32,
    pub rms_ns: f32,         // Only available when std / float is available
    sample_count: usize,
}

impl JitterStats {
    pub const fn new() -> Self {
        Self {
            samples: [0i32; WINDOW],
            head: 0,
            mean_ticks: 0,
            peak_positive: 0,
            peak_negative: i32::MAX,
            rms_ns: 0.0,
            sample_count: 0,
        }
    }

    /// Record a new timing deviation in timer ticks.
    /// Positive = later than expected; negative = earlier than expected.
    pub fn record(&mut self, deviation_ticks: i32) {
        let oldest = self.samples[self.head];
        self.samples[self.head] = deviation_ticks;
        self.head = (self.head + 1) % WINDOW;
        self.sample_count += 1;

        // Incremental mean (avoids full recalculation each update)
        self.mean_ticks += (deviation_ticks - oldest) / WINDOW as i32;

        if deviation_ticks > self.peak_positive {
            self.peak_positive = deviation_ticks;
        }
        if deviation_ticks < self.peak_negative {
            self.peak_negative = deviation_ticks;
        }

        // RMS calculation over the window (requires float)
        if self.sample_count >= WINDOW {
            let sum_sq: i64 = self.samples.iter().map(|&s| (s as i64) * (s as i64)).sum();
            self.rms_ns = ((sum_sq / WINDOW as i64) as f32).sqrt();
        }
    }

    /// Returns peak-to-peak jitter in ticks.
    pub fn peak_to_peak(&self) -> i32 {
        self.peak_positive - self.peak_negative
    }

    /// Returns true if peak-to-peak jitter exceeds the given threshold.
    pub fn exceeds_threshold(&self, max_ticks: i32) -> bool {
        self.peak_to_peak() > max_ticks
    }

    pub fn reset(&mut self) {
        *self = Self::new();
    }
}
```

---

### 2. Adaptive IIR Sample-Point Corrector

```rust
// uart_adaptive.rs
// IIR-filtered adaptive sample-point correction for software UART.

/// IIR filter alpha as right-shift amount (equivalent to 1/2^ALPHA weight).
const IIR_ALPHA: u32 = 4;   // Effectively 1/16 weight per new sample

/// Clamp to ±25% of bit period for safety.
const MAX_OFFSET_FRACTION: i32 = 4;

pub struct AdaptiveSamplePoint {
    /// Nominal ticks per bit at the configured baud rate.
    pub nominal_bit_ticks: i32,
    /// Current correction offset (add to nominal sample point).
    pub offset_ticks: i32,
    /// IIR accumulator (scaled by 2^IIR_ALPHA for precision).
    iir_acc: i32,
    /// Rolling error counter for alarm detection.
    pub framing_errors: u32,
    pub total_frames: u32,
}

impl AdaptiveSamplePoint {
    pub fn new(nominal_bit_ticks: u32) -> Self {
        Self {
            nominal_bit_ticks: nominal_bit_ticks as i32,
            offset_ticks: 0,
            iir_acc: 0,
            framing_errors: 0,
            total_frames: 0,
        }
    }

    /// Feed a measured start-bit edge deviation (measured - expected, in ticks).
    /// Call this on each successfully received frame.
    pub fn update(&mut self, edge_deviation_ticks: i32) {
        // IIR low-pass filter
        let error_scaled = edge_deviation_ticks << IIR_ALPHA;
        self.iir_acc += (error_scaled - self.iir_acc) >> IIR_ALPHA;

        // Convert accumulator back to ticks (divide out the scale)
        let raw_offset = self.iir_acc >> IIR_ALPHA;

        // Clamp to ±25% of bit period
        let max_off = self.nominal_bit_ticks / MAX_OFFSET_FRACTION;
        self.offset_ticks = raw_offset.clamp(-max_off, max_off);

        self.total_frames += 1;
    }

    /// Report a framing error; returns true if the error rate triggers recalibration.
    pub fn report_framing_error(&mut self) -> bool {
        self.framing_errors += 1;
        self.total_frames += 1;

        // Evaluate after at least 100 frames
        if self.total_frames >= 100 {
            let error_ppm =
                (self.framing_errors as u64 * 1_000_000) / self.total_frames as u64;

            if error_ppm > 5_000 {   // 0.5% error rate threshold
                self.reset();
                return true;          // Caller should trigger auto-baud or recal
            }
        }
        false
    }

    /// Effective sample point = nominal_center + offset_ticks.
    pub fn sample_point(&self, bit_index: usize) -> i32 {
        let nominal = (bit_index as i32 + 1) * self.nominal_bit_ticks
            + self.nominal_bit_ticks / 2;
        nominal + self.offset_ticks
    }

    pub fn reset(&mut self) {
        self.offset_ticks = 0;
        self.iir_acc = 0;
        self.framing_errors = 0;
        self.total_frames = 0;
    }
}
```

---

### 3. Majority-Vote Bit Sampler

```rust
// uart_majority_vote.rs
// Three-sample majority voter for jitter-tolerant bit reception.

/// Sample a bit three times around the expected center and return the majority value.
/// `sample_fn`: closure returning the current RX pin state (true = high).
/// `center_tick`: nominal timer value for the bit center.
/// `spread_ticks`: spacing between the three samples (e.g., bit_period / 6).
/// `delay_until`: closure that busy-waits until the given timer value.
pub fn majority_vote_sample<F, D>(
    sample_fn: &F,
    delay_until: &D,
    center_tick: u32,
    spread_ticks: u32,
) -> bool
where
    F: Fn() -> bool,
    D: Fn(u32),
{
    delay_until(center_tick - spread_ticks);
    let s0 = sample_fn();

    delay_until(center_tick);
    let s1 = sample_fn();

    delay_until(center_tick + spread_ticks);
    let s2 = sample_fn();

    // Majority vote: at least 2 of 3 must agree
    (s0 as u8 + s1 as u8 + s2 as u8) >= 2
}

/// Receive one UART byte using majority voting for each bit.
/// Returns Ok(byte) on success, Err(FramingError) if the stop bit is invalid.
pub fn receive_byte_majority<F, D>(
    sample_fn: &F,
    delay_until: &D,
    wait_for_edge: &impl Fn(),  // blocks until start bit falling edge
    start_tick: impl Fn() -> u32,
    bit_ticks: u32,
) -> Result<u8, UartError>
where
    F: Fn() -> bool,
    D: Fn(u32),
{
    wait_for_edge();
    let t0 = start_tick();
    let spread = bit_ticks / 6;   // ±1/6 of bit period per sample

    // Validate start bit at mid-point
    let mid_start = t0 + bit_ticks / 2;
    delay_until(mid_start);
    if sample_fn() {
        return Err(UartError::GlitchOnStartBit);
    }

    let mut byte: u8 = 0;

    for bit in 0..8u32 {
        // Center of data bit (bits start after the start bit)
        let center = t0 + bit_ticks + bit * bit_ticks + bit_ticks / 2;
        let val = majority_vote_sample(sample_fn, delay_until, center, spread);
        if val {
            byte |= 1 << bit;
        }
    }

    // Check stop bit
    let stop_center = t0 + 9 * bit_ticks + bit_ticks / 2;
    delay_until(stop_center);
    if !sample_fn() {
        return Err(UartError::FramingError);
    }

    Ok(byte)
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum UartError {
    FramingError,
    GlitchOnStartBit,
    ParityError,
    BufferOverflow,
}
```

---

### 4. Jitter Budget Analyzer (no_std compatible)

```rust
// jitter_budget.rs
// Compile-time and runtime jitter budget analysis for UART configurations.
// no_std compatible (uses libm for sqrt if needed).

#[derive(Debug, Clone)]
pub struct JitterConfig {
    pub baud_rate: u32,
    pub tx_error_ppm: u32,
    pub rx_error_ppm: u32,
    pub frame_bits: u32,
    /// Guard band as parts-per-thousand (e.g., 100 = 10%)
    pub guard_band_ppk: u32,
}

#[derive(Debug)]
pub struct JitterBudgetResult {
    pub bit_period_ns: u32,
    pub max_error_ns: u32,
    pub actual_error_ns: u32,
    pub margin_ns: i32,
    pub compliant: bool,
}

impl JitterConfig {
    pub fn analyze(&self) -> JitterBudgetResult {
        // Bit period in nanoseconds
        let bit_period_ns: u64 = 1_000_000_000u64 / self.baud_rate as u64;

        // Usable eye opening (in ppk = parts per thousand)
        let usable_ppk = 500u64 - self.guard_band_ppk as u64;

        // Maximum total error in nanoseconds
        let max_error_ns = (bit_period_ns * usable_ppk) / 1000;

        // Actual combined error: (TX_ppm + RX_ppm) * frame_bits * bit_period
        let combined_ppm = self.tx_error_ppm as u64 + self.rx_error_ppm as u64;
        let actual_error_ns =
            (bit_period_ns * combined_ppm * self.frame_bits as u64) / 1_000_000;

        let margin_ns = max_error_ns as i64 - actual_error_ns as i64;

        JitterBudgetResult {
            bit_period_ns: bit_period_ns as u32,
            max_error_ns: max_error_ns as u32,
            actual_error_ns: actual_error_ns as u32,
            margin_ns: margin_ns as i32,
            compliant: margin_ns >= 0,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crystal_system_is_compliant() {
        let cfg = JitterConfig {
            baud_rate: 115_200,
            tx_error_ppm: 50,
            rx_error_ppm: 50,
            frame_bits: 10,
            guard_band_ppk: 100,  // 10%
        };
        let r = cfg.analyze();
        assert!(r.compliant, "Crystal UART should be compliant: {:?}", r);
        assert!(r.margin_ns > 0);
    }

    #[test]
    fn rc_oscillator_violates_budget() {
        let cfg = JitterConfig {
            baud_rate: 9_600,
            tx_error_ppm: 30_000,   // 3%
            rx_error_ppm: 30_000,   // 3%
            frame_bits: 10,
            guard_band_ppk: 100,
        };
        let r = cfg.analyze();
        assert!(!r.compliant, "Uncompensated RC UART should violate budget: {:?}", r);
    }

    #[test]
    fn rc_calibrated_may_comply() {
        let cfg = JitterConfig {
            baud_rate: 9_600,
            tx_error_ppm: 1_000,   // After calibration: 0.1%
            rx_error_ppm: 1_000,
            frame_bits: 10,
            guard_band_ppk: 100,
        };
        let r = cfg.analyze();
        assert!(r.compliant, "Calibrated RC UART should comply: {:?}", r);
    }
}
```

---

### 5. UART Error Rate Monitor in Rust (embedded-hal compatible)

```rust
// uart_monitor.rs
// Tracks UART error rates to detect jitter-induced degradation.
// Compatible with embedded-hal UART traits.

use core::fmt;

const WINDOW: u32 = 1000;

#[derive(Debug, Default, Clone, Copy)]
pub struct ErrorWindow {
    pub framing:  u32,
    pub noise:    u32,
    pub overrun:  u32,
    pub parity:   u32,
    pub total:    u32,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum JitterAlarmLevel {
    /// Error rate below warning threshold.
    Normal,
    /// Error rate elevated; consider investigating clock quality.
    Warning,
    /// Error rate critical; recalibration or baud-rate change required.
    Critical,
}

pub struct UartMonitor {
    current: ErrorWindow,
    last_window: ErrorWindow,
    warn_ppm: u32,     // Warning threshold in parts-per-million
    crit_ppm: u32,     // Critical threshold in parts-per-million
}

impl UartMonitor {
    pub fn new(warn_ppm: u32, crit_ppm: u32) -> Self {
        Self {
            current: ErrorWindow::default(),
            last_window: ErrorWindow::default(),
            warn_ppm,
            crit_ppm,
        }
    }

    /// Call on each received byte. Returns alarm level when the window closes.
    pub fn record_byte(&mut self, err: Option<UartRxError>) -> Option<JitterAlarmLevel> {
        self.current.total += 1;

        if let Some(e) = err {
            match e {
                UartRxError::Framing  => self.current.framing  += 1,
                UartRxError::Noise    => self.current.noise    += 1,
                UartRxError::Overrun  => self.current.overrun  += 1,
                UartRxError::Parity   => self.current.parity   += 1,
            }
        }

        if self.current.total >= WINDOW {
            let level = self.evaluate();
            self.last_window = self.current;
            self.current = ErrorWindow::default();
            Some(level)
        } else {
            None
        }
    }

    fn evaluate(&self) -> JitterAlarmLevel {
        let jitter_errors = self.current.framing + self.current.noise;
        let ppm = (jitter_errors as u64 * 1_000_000) / self.current.total as u64;

        if ppm >= self.crit_ppm as u64 {
            JitterAlarmLevel::Critical
        } else if ppm >= self.warn_ppm as u64 {
            JitterAlarmLevel::Warning
        } else {
            JitterAlarmLevel::Normal
        }
    }

    pub fn last_window(&self) -> &ErrorWindow {
        &self.last_window
    }
}

#[derive(Debug, Clone, Copy)]
pub enum UartRxError {
    Framing,
    Noise,
    Overrun,
    Parity,
}

impl fmt::Display for JitterAlarmLevel {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            JitterAlarmLevel::Normal   => write!(f, "NORMAL"),
            JitterAlarmLevel::Warning  => write!(f, "WARNING"),
            JitterAlarmLevel::Critical => write!(f, "CRITICAL"),
        }
    }
}
```

---

## Summary

**UART jitter tolerance** is the system's ability to maintain reliable communication in the presence of clock-edge timing variations. Because UART is asynchronous, both ends run independent clocks, and any deviation accumulates across an entire frame. The key points are:

**Fundamentals:**
- Jitter arises from oscillator quality, interrupt latency, power supply noise, and PCB signal integrity effects.
- The cumulative timing error across a 10-bit frame must remain below ~40% of a bit period (with a 10% guard band), meaning the combined TX+RX frequency error must stay below ~4%.
- Crystal oscillators (±50 ppm) provide enormous margin; uncompensated RC oscillators (±3%) routinely violate the budget without calibration.

**Mitigation hardware:** Use crystal or PLL-derived clocks, proper PCB layout, controlled impedance traces, and decoupling capacitors near the oscillator.

**Mitigation software:**
- Implement **3× oversampling with majority voting** to reject single-sample glitches.
- Use **IIR-filtered adaptive sample-point correction** to track slow oscillator drift.
- Deploy **error-rate monitoring** (framing and noise error counters) to detect jitter-induced degradation and trigger recalibration automatically.
- Use **auto-baud detection** (measuring the start-bit width) to measure and compensate for frequency offset at startup.

**At high baud rates** (>1 Mbps), jitter tolerances shrink into the nanosecond range, requiring hardware UART peripherals with 16× oversampling, low-jitter clock sources, and careful board-level signal integrity design. Software mitigation alone is insufficient.

The Rust and C/C++ examples provided implement all major mitigation strategies: jitter statistics tracking, adaptive IIR correction, majority-vote sampling, budget analysis, and hardware error monitoring — forming a complete toolkit for robust UART communication under real-world jitter conditions.

---

*Document: 74. Jitter Tolerance — Handling clock jitter and timing variations in UART systems.*