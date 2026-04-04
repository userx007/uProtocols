# 10. Signal Integrity in UART Communications

**Theory & Hardware** — Voltage level noise margins and rise-time rules, the RC cable-length/baud-rate relationship (with worked examples), transmission line effects (reflections, Γ coefficient), all four termination strategies (series, parallel, AC, Thevenin bias), noise sources table, and mitigation techniques (shielding, twisted pair, ferrite beads, decoupling caps, ESD protection), plus RS-485 differential signalling principles.

**C/C++ Code** — A `UartErrorStats` struct with ISR-friendly counters, a `uart_si_update_stats()` function driven by the hardware status register, a `SignalQuality` assessor using integer arithmetic (no floats), a `uart_si_reduce_baud()` adaptive fallback, a full C++ `UartSignalManager` class with callback hooks, and an RS-485 half-duplex direction-control driver with pre/post-TX guard times.

**Rust Code** — Atomic `UartErrorCounters` (ISR-safe via `core::sync::atomic`), `SignalQuality` enum with `no_std` integer rate calculation, `AdaptiveBaud` manager, a status register bit-field parser, an `embedded-hal`-generic `Rs485<TX,DE>` driver, and a complete `no_std` `main.rs` integration example with a USART ISR stub.

**Reference** — A hardware design checklist covering physical layer, noise immunity, and software requirements.


> Managing cable length, termination, and noise immunity in UART communications

---

## Table of Contents

1. [Introduction](#introduction)
2. [Fundamentals of Signal Integrity](#fundamentals-of-signal-integrity)
3. [Cable Length and Baud Rate Relationship](#cable-length-and-baud-rate-relationship)
4. [Transmission Line Effects](#transmission-line-effects)
5. [Termination Strategies](#termination-strategies)
6. [Noise Sources and Immunity](#noise-sources-and-immunity)
7. [Differential Signaling (RS-422 / RS-485)](#differential-signaling-rs-422--rs-485)
8. [Programming: Diagnostics and Adaptive Strategies in C/C++](#programming-diagnostics-and-adaptive-strategies-in-cc)
9. [Programming: Signal Integrity Monitoring in Rust](#programming-signal-integrity-monitoring-in-rust)
10. [Hardware Design Checklist](#hardware-design-checklist)
11. [Summary](#summary)

---

## Introduction

Signal integrity refers to the quality and reliability of electrical signals as they travel through conductors, connectors, and cables. In UART communications, degraded signal integrity manifests as framing errors, parity errors, data corruption, and complete communication failures.

Unlike high-speed differential protocols (SPI, I²C at higher speeds, USB), standard TTL UART is a **single-ended, asynchronous** interface — making it particularly vulnerable to:

- Capacitive loading from long cables
- Electromagnetic interference (EMI)
- Ground potential differences
- Impedance mismatches causing reflections

Understanding and mitigating these effects is critical for any embedded system that communicates over more than a few centimetres of wire.

---

## Fundamentals of Signal Integrity

### Voltage Levels

UART signal integrity begins with ensuring valid voltage levels at both transmitter and receiver:

| Logic Family | Logic LOW (V) | Logic HIGH (V) | Input Threshold LOW | Input Threshold HIGH |
|---|---|---|---|---|
| TTL (5V)     | 0 – 0.8       | 2.4 – 5.0      | < 0.8 V             | > 2.0 V              |
| CMOS (3.3V)  | 0 – 0.9       | 2.4 – 3.3      | < 0.8 V             | > 2.0 V              |
| CMOS (1.8V)  | 0 – 0.54      | 1.26 – 1.8     | < 0.63 V            | > 1.17 V             |
| RS-232       | +3 to +15 V (SPACE/0) | -3 to -15 V (MARK/1) | — | — |

**Noise margin** is the difference between the transmitter's guaranteed output level and the receiver's threshold. A larger noise margin means the signal can tolerate more degradation.

```
Noise Margin (HIGH) = V_OH(min) − V_IH(min)
Noise Margin (LOW)  = V_IL(max) − V_OL(max)
```

For a 3.3 V CMOS system: NM_HIGH ≈ 2.4 − 2.0 = 0.4 V, NM_LOW ≈ 0.8 − 0.0 = 0.8 V

### Rise and Fall Times

Signal rise/fall times must be fast enough to achieve a clean logic transition within the bit period, but not so fast that they cause severe reflections. The rule of thumb:

```
t_rise < 0.3 × T_bit
```

For 115200 baud: T_bit = 8.68 µs → t_rise < 2.6 µs

---

## Cable Length and Baud Rate Relationship

The maximum usable cable length depends on:

1. **Baud rate** — higher baud rates have shorter bit periods, leaving less time for signal settling.
2. **Cable capacitance** — typically 50–100 pF/m for standard cables.
3. **Driver output impedance** — forms an RC low-pass filter with cable capacitance.

### RC Time Constant Rule

The cable acts as a lumped capacitance `C_cable` in series with the driver's output impedance `R_out`:

```
τ = R_out × C_cable

For reliable operation: τ < 0.1 × T_bit
```

**Example: Maximum cable length at 9600 baud with TTL driver**

```
T_bit = 1 / 9600 ≈ 104 µs
τ_max = 0.1 × 104 µs = 10.4 µs
R_out (typical TTL) ≈ 50 Ω
C_cable = τ_max / R_out = 10.4 µs / 50 Ω = 208 nF

At 100 pF/m: L_max = 208 nF / 100 pF/m = 2080 m
```

### Practical Limits by Standard

| Standard | Max Cable Length | Max Data Rate | Notes |
|---|---|---|---|
| TTL UART (3.3/5V) | ~1–3 m | ~1 Mbps | Highly environment-dependent |
| RS-232            | ~15 m (spec) | 20 kbps (spec), 1 Mbps (practical) | Spec is conservative |
| RS-422            | 1200 m        | 10 Mbps | Differential, point-to-point |
| RS-485            | 1200 m        | 10 Mbps | Differential, multi-drop |

> **Rule of thumb for RS-232/TTL:** Keep cable length below `(baud_rate × 10⁻⁸)⁻¹` metres. At 115200 baud → ~0.87 m practical TTL limit in noisy environments.

---

## Transmission Line Effects

When cable length exceeds approximately 1/10th of the signal wavelength (for fast edges), the cable must be treated as a **transmission line**, not just a wire.

### Signal Wavelength

```
λ = v_p / f_knee
f_knee ≈ 0.35 / t_rise
v_p ≈ 2×10⁸ m/s (typical PCB/cable with εr ≈ 2.25)
```

For t_rise = 10 ns: f_knee = 35 MHz, λ ≈ 5.7 m → treat cables > 0.57 m as transmission lines.

### Reflections

Reflections occur when the signal encounters an impedance discontinuity. The **reflection coefficient** Γ is:

```
Γ = (Z_L − Z_0) / (Z_L + Z_0)
```

Where `Z_0` is characteristic impedance (~100–120 Ω for twisted pair) and `Z_L` is the load impedance.

- `Z_L >> Z_0` (open circuit): Γ → +1 (positive reflection, overshoot)
- `Z_L << Z_0` (short circuit): Γ → -1 (negative reflection, undershoot)
- `Z_L = Z_0` (matched): Γ = 0 (no reflection)

Reflections on UART lines cause **ringing** — oscillations near transitions that can be falsely interpreted as multiple edges by the receiver.

---

## Termination Strategies

### 1. Series Termination (Source Termination)

A resistor `R_s` is placed in series at the **transmitter output**:

```
R_s = Z_0 − R_driver_output
```

For Z_0 = 100 Ω and R_driver = 10 Ω → R_s = 90 Ω (use 100 Ω standard value)

```
TX ──[R_s 100Ω]──────────── RX
                   cable
```

- **Pros:** Simple, low power, no DC loading.
- **Cons:** Signal at transmitter end is initially half amplitude; reflected wave restores it at the source after one round trip.
- **Best for:** Point-to-point connections, TTL UART.

### 2. Parallel Termination (Load Termination)

A resistor `R_T` is placed at the **receiver end** to ground (or a voltage divider):

```
TX ──────────────────[R_T 120Ω]── GND
          cable             RX
```

```
R_T = Z_0 (typically 120 Ω for RS-485 twisted pair)
```

- **Pros:** Eliminates reflections at the receive end; clean waveform at receiver.
- **Cons:** Continuous DC current draw; may overload the driver.
- **Best for:** RS-485, RS-422 multi-drop buses.

### 3. AC Termination (RC Termination)

Combines a resistor and capacitor to terminate only at high frequencies:

```
TX ──────────────────[R_T]──[C_T]── GND
```

Typical: R_T = 120 Ω, C_T = 100 pF

- **Pros:** No DC loading; effective for high-frequency reflections.
- **Cons:** More complex; less effective at low frequencies.

### 4. Thevenin Termination (RS-485 Bias)

Two resistors form a voltage divider to bias the bus to a defined state (preventing indeterminate levels when all drivers are off):

```
VCC ──[R1]──┬── RX
            │
TX ─────────┤
            │
      [R2]──┴── GND
```

Typical: R1 = R2 = 560 Ω, creating ~VCC/2 ≈ 1.65 V bias for a 3.3 V system.

---

## Noise Sources and Immunity

### Common Noise Sources

| Source | Coupling Mechanism | Typical Frequency |
|---|---|---|
| Power supply ripple | Conducted, capacitive | 50/100 Hz, switching freq |
| Motor drives / PWM | Conducted, radiated | 1–100 kHz |
| RF transmitters | Radiated | MHz–GHz |
| Adjacent digital signals | Crosstalk (capacitive/inductive) | Signal frequency |
| Ground loops | Common-mode, conducted | 50/60 Hz and harmonics |

### Mitigation Techniques

#### Shielded Cable

Use cable with a foil or braid shield. Connect shield to **ground at one end only** to avoid ground loop currents. For UART:

```
Shield ──────────── connected to GND at RECEIVER side only
```

#### Twisted Pair

Twisting the TX/GND or TX/RX pair cancels magnetically induced noise (noise induces equal and opposite voltages in adjacent twists). Effective for differential signals; helpful for single-ended with a dedicated return conductor.

#### Common-Mode Chokes / Ferrite Beads

Place ferrite beads in series on signal lines near connectors to suppress high-frequency common-mode currents:

```
TX ──[FB]──────── to cable
GND──[FB]──────── to cable
```

Use ferrite beads with impedance > 100 Ω at target noise frequency.

#### Decoupling Capacitors

Place 100 nF ceramic capacitors from VCC to GND at every UART transceiver IC:

```
VCC ──┬──── IC VCC pin
      │
    [100nF]
      │
GND ──┴──── IC GND pin
```

#### ESD Protection

TVS diodes or ESD protection arrays protect signal lines from electrostatic discharge events (especially on external connectors):

```
TX ──┬──── to connector
     │
   [TVS]   (e.g., PRTR5V0U2X, USBLC6-2)
     │
GND ─┘
```

---

## Differential Signaling (RS-422 / RS-485)

The most effective solution for long-distance or noisy environments is to convert TTL UART to a differential standard.

### How Differential Signaling Works

Instead of measuring voltage relative to a shared ground, the receiver measures the **voltage difference** between two wires (A and B):

```
Logic 1: A − B > +200 mV
Logic 0: A − B < −200 mV
Indeterminate: |A − B| < 200 mV
```

Noise couples equally to both wires (common-mode noise), so it cancels at the differential receiver — providing **common-mode rejection** of up to ±25 V (RS-485).

### RS-485 Transceiver Wiring

```
MCU TX ──► [DE/RE Control]
            │
MCU TX ──► [DI pin]  ┌── RS-485 IC ──┐
MCU RX ◄── [RO pin]  │  (e.g. MAX485) ├── A wire ──► twisted pair ──► A
                     └───────────────┘── B wire ──► twisted pair ──► B
```

### Half-Duplex Direction Control

RS-485 is typically half-duplex. The transmit-enable (`DE`) and receive-enable (`/RE`) pins must be driven correctly:

```c
// Transmit mode
GPIO_SetPin(DE_RE_PIN, HIGH);  // Enable driver, disable receiver
UART_Transmit(data, len);
// Wait for transmission complete
GPIO_SetPin(DE_RE_PIN, LOW);   // Disable driver, enable receiver
```

---

## Programming: Diagnostics and Adaptive Strategies in C/C++

The following examples demonstrate how to detect and respond to signal integrity issues programmatically, as well as how to configure hardware features that improve noise immunity.

### 8.1 Error Counter and Diagnostics Structure

```c
// uart_signal_integrity.h
#ifndef UART_SIGNAL_INTEGRITY_H
#define UART_SIGNAL_INTEGRITY_H

#include <stdint.h>
#include <stdbool.h>

// ── Error statistics ──────────────────────────────────────────────────────────
typedef struct {
    uint32_t framing_errors;    // Stop-bit not detected (level mismatch)
    uint32_t parity_errors;     // Parity check failed (bit-flip noise)
    uint32_t overrun_errors;    // RX FIFO overflow (processing too slow)
    uint32_t noise_flags;       // Sampler detected noise on RX line (STM32 NF bit)
    uint32_t total_bytes_rx;
    uint32_t total_bytes_tx;
    uint32_t crc_failures;      // Application-level CRC/checksum failures
} UartErrorStats;

// ── Signal quality assessment ─────────────────────────────────────────────────
typedef enum {
    SIGNAL_QUALITY_GOOD    = 0,  // Error rate < 0.01%
    SIGNAL_QUALITY_FAIR    = 1,  // Error rate 0.01% – 0.1%
    SIGNAL_QUALITY_POOR    = 2,  // Error rate 0.1% – 1%
    SIGNAL_QUALITY_FAILING = 3,  // Error rate > 1%
} SignalQuality;

// ── Configuration ─────────────────────────────────────────────────────────────
typedef struct {
    uint32_t baud_rate;
    uint8_t  data_bits;         // 7, 8, or 9
    uint8_t  stop_bits;         // 1 or 2
    bool     parity_enabled;
    bool     hardware_flow_ctrl;
    uint32_t sample_rate_override; // 0 = auto (16x); 8 for noisy lines (8x on STM32)
} UartConfig;

// ── Public API ────────────────────────────────────────────────────────────────
void          uart_si_init(UartConfig *cfg);
void          uart_si_reset_stats(void);
void          uart_si_update_stats(uint32_t status_register);
SignalQuality uart_si_assess_quality(void);
void          uart_si_log_report(void);
bool          uart_si_reduce_baud(void);  // Step down baud rate on degradation

extern UartErrorStats g_uart_stats;
extern UartConfig     g_uart_cfg;

#endif // UART_SIGNAL_INTEGRITY_H
```

### 8.2 Error Statistics Implementation

```c
// uart_signal_integrity.c
#include "uart_signal_integrity.h"
#include <stdio.h>

// ── STM32 USARTx SR / ISR register bit definitions ───────────────────────────
#define UART_SR_PE   (1U << 0)   // Parity Error
#define UART_SR_FE   (1U << 1)   // Framing Error
#define UART_SR_NF   (1U << 2)   // Noise Flag (STM32 specific)
#define UART_SR_ORE  (1U << 3)   // Overrun Error
#define UART_SR_RXNE (1U << 5)   // RX buffer not empty

UartErrorStats g_uart_stats = {0};
UartConfig     g_uart_cfg   = {0};

// Ordered baud rate fallback table (fastest to slowest)
static const uint32_t baud_fallback[] = {
    921600, 460800, 230400, 115200, 57600, 38400, 19200, 9600, 4800, 2400, 0
};

// ─────────────────────────────────────────────────────────────────────────────
void uart_si_init(UartConfig *cfg) {
    if (!cfg) return;
    g_uart_cfg = *cfg;
    uart_si_reset_stats();

    // Platform-specific UART initialisation would go here.
    // Example shown for STM32 HAL:
    //   huart1.Init.BaudRate    = cfg->baud_rate;
    //   huart1.Init.WordLength  = UART_WORDLENGTH_8B;
    //   huart1.Init.StopBits    = cfg->stop_bits == 2 ? UART_STOPBITS_2
    //                                                 : UART_STOPBITS_1;
    //   huart1.Init.Parity      = cfg->parity_enabled ? UART_PARITY_EVEN
    //                                                 : UART_PARITY_NONE;
    //   huart1.Init.OverSampling = cfg->sample_rate_override == 8
    //                              ? UART_OVERSAMPLING_8
    //                              : UART_OVERSAMPLING_16;
    //   HAL_UART_Init(&huart1);
}

// ─────────────────────────────────────────────────────────────────────────────
void uart_si_reset_stats(void) {
    g_uart_stats = (UartErrorStats){0};
}

// ─────────────────────────────────────────────────────────────────────────────
// Call this from the UART ISR or polling loop, passing the status register value.
void uart_si_update_stats(uint32_t sr) {
    if (sr & UART_SR_PE)  g_uart_stats.parity_errors++;
    if (sr & UART_SR_FE)  g_uart_stats.framing_errors++;
    if (sr & UART_SR_NF)  g_uart_stats.noise_flags++;
    if (sr & UART_SR_ORE) g_uart_stats.overrun_errors++;
    if (sr & UART_SR_RXNE) g_uart_stats.total_bytes_rx++;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute error rate and return a quality bucket.
SignalQuality uart_si_assess_quality(void) {
    if (g_uart_stats.total_bytes_rx == 0) return SIGNAL_QUALITY_GOOD;

    uint32_t total_errors = g_uart_stats.framing_errors
                          + g_uart_stats.parity_errors
                          + g_uart_stats.noise_flags;

    // Multiply by 10000 to get rate per 10000 bytes (avoids float)
    uint32_t error_rate_1e4 = (total_errors * 10000U)
                              / g_uart_stats.total_bytes_rx;

    if (error_rate_1e4 < 1)   return SIGNAL_QUALITY_GOOD;    // < 0.01%
    if (error_rate_1e4 < 10)  return SIGNAL_QUALITY_FAIR;    // < 0.1%
    if (error_rate_1e4 < 100) return SIGNAL_QUALITY_POOR;    // < 1%
    return SIGNAL_QUALITY_FAILING;
}

// ─────────────────────────────────────────────────────────────────────────────
void uart_si_log_report(void) {
    const char *quality_str[] = { "GOOD", "FAIR", "POOR", "FAILING" };
    SignalQuality q = uart_si_assess_quality();

    printf("\n=== UART Signal Integrity Report ===\n");
    printf("  Baud Rate      : %lu\n",   (unsigned long)g_uart_cfg.baud_rate);
    printf("  Bytes RX       : %lu\n",   (unsigned long)g_uart_stats.total_bytes_rx);
    printf("  Framing Errors : %lu\n",   (unsigned long)g_uart_stats.framing_errors);
    printf("  Parity Errors  : %lu\n",   (unsigned long)g_uart_stats.parity_errors);
    printf("  Noise Flags    : %lu\n",   (unsigned long)g_uart_stats.noise_flags);
    printf("  Overrun Errors : %lu\n",   (unsigned long)g_uart_stats.overrun_errors);
    printf("  CRC Failures   : %lu\n",   (unsigned long)g_uart_stats.crc_failures);
    printf("  Signal Quality : %s\n",    quality_str[q]);
    printf("=====================================\n\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Adaptive baud rate: step down to the next lower rate in the fallback table.
// Returns true if a lower baud was found and applied; false if already at minimum.
bool uart_si_reduce_baud(void) {
    for (int i = 0; baud_fallback[i] != 0; i++) {
        if (baud_fallback[i] == g_uart_cfg.baud_rate && baud_fallback[i + 1] != 0) {
            g_uart_cfg.baud_rate = baud_fallback[i + 1];
            uart_si_init(&g_uart_cfg);   // Re-init at new baud
            uart_si_reset_stats();
            printf("[UART-SI] Baud stepped down to %lu\n",
                   (unsigned long)g_uart_cfg.baud_rate);
            return true;
        }
    }
    printf("[UART-SI] Already at minimum baud rate.\n");
    return false;
}
```

### 8.3 Adaptive Signal Integrity Manager (C++)

```cpp
// UartSignalManager.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <array>
#include <string_view>

// ── Callback types ────────────────────────────────────────────────────────────
using BaudChangeCb = std::function<void(uint32_t new_baud)>;
using AlertCb      = std::function<void(std::string_view message)>;

// ─────────────────────────────────────────────────────────────────────────────
class UartSignalManager {
public:
    static constexpr std::array<uint32_t, 10> kBaudTable = {
        921600, 460800, 230400, 115200, 57600, 38400, 19200, 9600, 4800, 2400
    };

    // Thresholds per 1000 frames
    static constexpr uint32_t kFairThreshold    = 1;   // 0.1%
    static constexpr uint32_t kPoorThreshold    = 10;  // 1%
    static constexpr uint32_t kFailingThreshold = 50;  // 5%

    // ─────────────────────────────────────────────────────────────────────────
    explicit UartSignalManager(uint32_t initial_baud,
                               BaudChangeCb on_baud_change = nullptr,
                               AlertCb      on_alert       = nullptr)
        : baud_idx_(baudIndex(initial_baud))
        , on_baud_change_(std::move(on_baud_change))
        , on_alert_(std::move(on_alert))
    {}

    // ── Called per received byte/frame ────────────────────────────────────────
    void onByteReceived()  { ++frames_total_;   }
    void onFramingError()  { ++err_framing_;    }
    void onParityError()   { ++err_parity_;     }
    void onNoiseDetected() { ++err_noise_;      }
    void onOverrun()       { ++err_overrun_;    }
    void onCrcFailure()    { ++err_crc_;        }

    // ── Periodic evaluation (call every N seconds or N frames) ───────────────
    void evaluate() {
        if (frames_total_ < 100) return;   // Not enough data yet

        uint32_t total_err = err_framing_ + err_parity_ + err_noise_;
        uint32_t rate_per_1k = (total_err * 1000U) / frames_total_;

        if (rate_per_1k >= kFailingThreshold) {
            alert("Signal FAILING — reducing baud rate");
            reduceBaud();
            reset();
        } else if (rate_per_1k >= kPoorThreshold) {
            alert("Signal POOR — monitoring closely");
        } else if (rate_per_1k >= kFairThreshold) {
            alert("Signal FAIR — minor noise detected");
        }
        // Quality GOOD — no action needed
    }

    uint32_t currentBaud() const { return kBaudTable[baud_idx_]; }

    void reset() {
        frames_total_ = err_framing_ = err_parity_ = 0;
        err_noise_    = err_overrun_ = err_crc_    = 0;
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    static std::size_t baudIndex(uint32_t baud) {
        for (std::size_t i = 0; i < kBaudTable.size(); ++i)
            if (kBaudTable[i] == baud) return i;
        return 3; // default to 115200
    }

    void reduceBaud() {
        if (baud_idx_ + 1 < kBaudTable.size()) {
            ++baud_idx_;
            if (on_baud_change_) on_baud_change_(kBaudTable[baud_idx_]);
        }
    }

    void alert(std::string_view msg) {
        if (on_alert_) on_alert_(msg);
    }

    // ─────────────────────────────────────────────────────────────────────────
    std::size_t  baud_idx_      = 3;
    uint32_t     frames_total_  = 0;
    uint32_t     err_framing_   = 0;
    uint32_t     err_parity_    = 0;
    uint32_t     err_noise_     = 0;
    uint32_t     err_overrun_   = 0;
    uint32_t     err_crc_       = 0;

    BaudChangeCb on_baud_change_;
    AlertCb      on_alert_;
};
```

### 8.4 Usage Example (C++)

```cpp
// main.cpp  — Demonstrates UartSignalManager in an embedded loop
#include "UartSignalManager.hpp"
#include <cstdio>

// Simulated UART HAL (replace with real BSP calls)
namespace uart_hal {
    uint32_t read_status_register() { return 0x00; }  // Real: read USART1->SR
    uint8_t  read_data_register()   { return 0xAA; }  // Real: read USART1->DR
    void     set_baud(uint32_t b)   { printf("[HAL] Baud set to %lu\n", (unsigned long)b); }
}

int main() {
    UartSignalManager mgr(
        115200,
        // Baud change callback — re-configure peripheral
        [](uint32_t new_baud) { uart_hal::set_baud(new_baud); },
        // Alert callback — log to console or LED indicator
        [](std::string_view msg) { printf("[ALERT] %s\n", msg.data()); }
    );

    uint32_t frame_count = 0;

    while (true) {
        uint32_t sr = uart_hal::read_status_register();

        // Feed errors into manager
        if (sr & 0x02) mgr.onFramingError();
        if (sr & 0x01) mgr.onParityError();
        if (sr & 0x04) mgr.onNoiseDetected();
        if (sr & 0x08) mgr.onOverrun();

        // Simulate data
        (void)uart_hal::read_data_register();
        mgr.onByteReceived();

        // Evaluate every 500 frames
        if (++frame_count % 500 == 0) {
            mgr.evaluate();
        }

        // break; // In real application: event-driven or RTOS task yield
        break; // Demo: exit loop
    }

    return 0;
}
```

### 8.5 RS-485 Direction Control with Turnaround Guard Time (C)

```c
// rs485.c — Half-duplex RS-485 with signal integrity guard times
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ── Platform abstraction (fill in for your MCU) ───────────────────────────────
static inline void gpio_de_re_set(bool transmit) {
    // HIGH = transmit mode (DE=1, /RE=1)
    // LOW  = receive mode  (DE=0, /RE=0)
    // Example: HAL_GPIO_WritePin(DE_RE_GPIO_Port, DE_RE_Pin, transmit ? GPIO_PIN_SET : GPIO_PIN_RESET);
    (void)transmit;
}

static inline void uart_transmit_bytes(const uint8_t *buf, size_t len) {
    // Example: HAL_UART_Transmit(&huart1, (uint8_t*)buf, len, HAL_MAX_DELAY);
    (void)buf; (void)len;
}

static inline bool uart_tx_complete(void) {
    // Poll transmit-complete flag
    // Example: return __HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC);
    return true;
}

static inline void delay_us(uint32_t us) {
    // Busy-wait or hardware timer delay
    volatile uint32_t count = us * 72; // Approx for 72 MHz CPU
    while (count--);
}

// ── Guard time constants ───────────────────────────────────────────────────────
// Minimum time after asserting DE before first bit is driven onto the bus.
// RS-485 spec: typically 1–2 bit periods at the operating baud rate.
#define RS485_GUARD_US_PRE_TX   20   // 20 µs  (~2 bit periods at 115200)
#define RS485_GUARD_US_POST_TX  20   // 20 µs  allow last bit to settle before releasing bus

// ── Public transmit function ──────────────────────────────────────────────────
void rs485_transmit(const uint8_t *data, size_t len) {
    // 1. Assert transmit mode
    gpio_de_re_set(true);

    // 2. Guard time: allow driver to stabilise before first bit
    delay_us(RS485_GUARD_US_PRE_TX);

    // 3. Send data
    uart_transmit_bytes(data, len);

    // 4. Wait for all bits to leave the shift register (TC flag)
    while (!uart_tx_complete());

    // 5. Post-transmission guard: let last stop bit propagate down cable
    //    before releasing the bus to receive mode
    delay_us(RS485_GUARD_US_POST_TX);

    // 6. Release bus → receive mode
    gpio_de_re_set(false);
}
```

---

## Programming: Signal Integrity Monitoring in Rust

### 9.1 Error Statistics and Quality Assessment

```rust
// uart_signal_integrity.rs
// no_std compatible — uses core only

use core::sync::atomic::{AtomicU32, Ordering};

// ── Atomic error counters (safe for ISR + main context) ──────────────────────
pub struct UartErrorCounters {
    pub framing:  AtomicU32,
    pub parity:   AtomicU32,
    pub noise:    AtomicU32,
    pub overrun:  AtomicU32,
    pub crc_fail: AtomicU32,
    pub rx_total: AtomicU32,
}

impl UartErrorCounters {
    pub const fn new() -> Self {
        Self {
            framing:  AtomicU32::new(0),
            parity:   AtomicU32::new(0),
            noise:    AtomicU32::new(0),
            overrun:  AtomicU32::new(0),
            crc_fail: AtomicU32::new(0),
            rx_total: AtomicU32::new(0),
        }
    }

    pub fn reset(&self) {
        self.framing.store(0,  Ordering::Relaxed);
        self.parity.store(0,   Ordering::Relaxed);
        self.noise.store(0,    Ordering::Relaxed);
        self.overrun.store(0,  Ordering::Relaxed);
        self.crc_fail.store(0, Ordering::Relaxed);
        self.rx_total.store(0, Ordering::Relaxed);
    }

    pub fn total_errors(&self) -> u32 {
        self.framing.load(Ordering::Relaxed)
            + self.parity.load(Ordering::Relaxed)
            + self.noise.load(Ordering::Relaxed)
    }
}

// Global static counters — accessible from ISR
pub static UART_ERRORS: UartErrorCounters = UartErrorCounters::new();

// ── Signal quality ─────────────────────────────────────────────────────────
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SignalQuality {
    Good,    // error rate < 0.01%
    Fair,    // error rate 0.01–0.1%
    Poor,    // error rate 0.1–1%
    Failing, // error rate > 1%
}

impl SignalQuality {
    pub fn assess(counters: &UartErrorCounters) -> Self {
        let rx = counters.rx_total.load(Ordering::Relaxed);
        if rx == 0 {
            return Self::Good;
        }
        // Rate per 10_000 frames (avoids floating point)
        let rate = (counters.total_errors() * 10_000) / rx;
        match rate {
            0          => Self::Good,
            1..=9      => Self::Fair,
            10..=99    => Self::Poor,
            _          => Self::Failing,
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Good    => "GOOD",
            Self::Fair    => "FAIR",
            Self::Poor    => "POOR",
            Self::Failing => "FAILING",
        }
    }
}
```

### 9.2 Adaptive Baud Rate Manager in Rust

```rust
// adaptive_baud.rs

/// Standard baud rates, ordered fastest to slowest.
const BAUD_TABLE: &[u32] = &[921_600, 460_800, 230_400, 115_200,
                              57_600,  38_400,  19_200,   9_600,
                               4_800,   2_400,   1_200];

pub struct AdaptiveBaud {
    idx:        usize,
    eval_every: u32,   // Evaluate quality every N bytes received
    pending:    u32,   // Bytes received since last evaluation
}

impl AdaptiveBaud {
    pub fn new(initial_baud: u32, eval_every: u32) -> Self {
        let idx = BAUD_TABLE
            .iter()
            .position(|&b| b == initial_baud)
            .unwrap_or(3); // Default to 115200 index

        Self { idx, eval_every, pending: 0 }
    }

    pub fn current_baud(&self) -> u32 {
        BAUD_TABLE[self.idx]
    }

    /// Call each time a byte is received.
    /// Returns Some(new_baud) if a baud rate change is recommended.
    pub fn tick(&mut self, counters: &crate::UartErrorCounters) -> Option<u32> {
        counters.rx_total.fetch_add(1, core::sync::atomic::Ordering::Relaxed);
        self.pending += 1;

        if self.pending < self.eval_every {
            return None;
        }

        self.pending = 0;
        let quality = crate::SignalQuality::assess(counters);

        if quality == crate::SignalQuality::Failing {
            if self.idx + 1 < BAUD_TABLE.len() {
                self.idx += 1;
                counters.reset();
                return Some(BAUD_TABLE[self.idx]);
            }
        }

        None
    }
}
```

### 9.3 UART Status Register Parser (Rust)

```rust
// status_register.rs — Parse hardware UART status bits

/// Bit positions in a typical ARM Cortex-M USART Status Register.
/// Adjust for your specific MCU/HAL.
pub struct UsartStatusBits(pub u32);

impl UsartStatusBits {
    pub fn parity_error(self)   -> bool { self.0 & (1 << 0) != 0 }
    pub fn framing_error(self)  -> bool { self.0 & (1 << 1) != 0 }
    pub fn noise_flag(self)     -> bool { self.0 & (1 << 2) != 0 }
    pub fn overrun_error(self)  -> bool { self.0 & (1 << 3) != 0 }
    pub fn rx_not_empty(self)   -> bool { self.0 & (1 << 5) != 0 }
    pub fn tx_complete(self)    -> bool { self.0 & (1 << 6) != 0 }
    pub fn tx_empty(self)       -> bool { self.0 & (1 << 7) != 0 }
}

/// Update global counters from raw status register value.
pub fn process_status(sr: u32) {
    let bits = UsartStatusBits(sr);
    if bits.framing_error() {
        crate::UART_ERRORS.framing.fetch_add(1, core::sync::atomic::Ordering::Relaxed);
    }
    if bits.parity_error() {
        crate::UART_ERRORS.parity.fetch_add(1, core::sync::atomic::Ordering::Relaxed);
    }
    if bits.noise_flag() {
        crate::UART_ERRORS.noise.fetch_add(1, core::sync::atomic::Ordering::Relaxed);
    }
    if bits.overrun_error() {
        crate::UART_ERRORS.overrun.fetch_add(1, core::sync::atomic::Ordering::Relaxed);
    }
}
```

### 9.4 RS-485 Direction Control in Rust (embedded-hal)

```rust
// rs485_control.rs
// Uses embedded-hal traits for MCU-agnostic GPIO and UART control.

use embedded_hal::digital::OutputPin;
use embedded_hal::serial::Write;
use nb::block;

/// Half-duplex RS-485 driver with configurable guard times.
pub struct Rs485<TX, DE>
where
    TX: Write<u8>,
    DE: OutputPin,
{
    uart:           TX,
    de_re:          DE,
    guard_ticks_pre:  u32,  // Platform-specific: cycles or timer ticks
    guard_ticks_post: u32,
}

impl<TX, DE> Rs485<TX, DE>
where
    TX: Write<u8>,
    DE: OutputPin,
    DE::Error: core::fmt::Debug,
{
    pub fn new(uart: TX, mut de_re: DE, guard_pre: u32, guard_post: u32) -> Self {
        de_re.set_low().unwrap(); // Start in receive mode
        Self {
            uart,
            de_re,
            guard_ticks_pre:  guard_pre,
            guard_ticks_post: guard_post,
        }
    }

    fn busy_delay(ticks: u32) {
        // Replace with a proper timer abstraction in production code
        for _ in 0..ticks {
            core::hint::spin_loop();
        }
    }

    /// Transmit a byte slice onto the RS-485 bus.
    /// Automatically manages DE/RE pin and guard times.
    pub fn write(&mut self, data: &[u8]) -> Result<(), TX::Error> {
        // 1. Assert driver enable (transmit mode)
        self.de_re.set_high().unwrap();

        // 2. Pre-transmission guard time
        Self::busy_delay(self.guard_ticks_pre);

        // 3. Transmit each byte; block until the UART accepts it
        for &byte in data {
            block!(self.uart.write(byte))?;
        }

        // 4. Flush — wait until the shift register is empty
        block!(self.uart.flush())?;

        // 5. Post-transmission guard time
        Self::busy_delay(self.guard_ticks_post);

        // 6. Release bus — receive mode
        self.de_re.set_low().unwrap();

        Ok(())
    }
}
```

### 9.5 Complete Integration Example (Rust)

```rust
// main.rs — no_std example showing integration of all SI components

#![no_std]
#![no_main]

mod uart_signal_integrity; // counters + quality
mod adaptive_baud;
mod status_register;

use uart_signal_integrity::{UART_ERRORS, SignalQuality};
use adaptive_baud::AdaptiveBaud;
use status_register::process_status;

// Simulated hardware reads (replace with real register reads in production)
fn read_uart_sr() -> u32 { 0x0000 }
fn reconfigure_uart(_baud: u32) { /* platform-specific */ }

#[cortex_m_rt::entry]
fn main() -> ! {
    let mut baud_mgr = AdaptiveBaud::new(115_200, 500);

    loop {
        // Parse and count hardware errors
        let sr = read_uart_sr();
        process_status(sr);

        // Trigger quality evaluation every 500 bytes;
        // reconfigure UART if baud reduction is needed
        if let Some(new_baud) = baud_mgr.tick(&UART_ERRORS) {
            reconfigure_uart(new_baud);
        }

        // Optional: periodic quality logging (requires defmt or similar)
        // let quality = SignalQuality::assess(&UART_ERRORS);
        // defmt::info!("Signal quality: {}", quality.as_str());

        cortex_m::asm::wfi(); // Wait for interrupt
    }
}

// UART ISR — update error counters from interrupt context
#[cortex_m_rt::interrupt]
fn USART1() {
    let sr = read_uart_sr();
    process_status(sr);
    UART_ERRORS.rx_total.fetch_add(1, core::sync::atomic::Ordering::Relaxed);
}
```

---

## Hardware Design Checklist

Use this checklist when designing or debugging a UART communication link:

### Physical Layer

- [ ] Cable length appropriate for baud rate (see table above)
- [ ] Characteristic impedance of cable documented (typically 100–120 Ω for twisted pair)
- [ ] Series termination resistors placed at transmitter output
- [ ] Parallel termination resistors (RS-485 only) at far end
- [ ] Thevenin bias resistors on RS-485 bus to define idle state
- [ ] Cable shielded and shield grounded at one end only
- [ ] Twisted pair used for differential signals and their returns

### Noise Immunity

- [ ] Ferrite beads on signal lines at connectors
- [ ] ESD protection devices (TVS diodes) on all external-facing UART pins
- [ ] 100 nF decoupling capacitor on each transceiver IC supply pin
- [ ] Ground plane unbroken under UART signal traces on PCB
- [ ] Signal traces routed away from high-current (motor, power) traces
- [ ] Connector backshell electrically bonded to cable shield

### Software

- [ ] Error counters (framing, parity, noise, overrun) logged and monitored
- [ ] CRC or checksum applied at application layer
- [ ] Adaptive baud rate reduction implemented for degraded links
- [ ] RS-485 guard times programmed around DE/RE transitions
- [ ] Receiver timeout configured to flush partial frames

---

## Summary

Signal integrity is the foundation of reliable UART communication. The key principles and practices covered in this document are:

**Physical constraints** drive the achievable baud rate and distance. Cable capacitance and driver impedance form an RC low-pass filter; when the bit period approaches the RC time constant, signal quality degrades sharply. Standard TTL UART is limited to short runs (< 3 m at 115200 baud in noisy environments), while RS-232, RS-422, and RS-485 extend range to 15 m, 1200 m, and 1200 m respectively.

**Transmission line effects** (reflections, ringing) appear when cable length exceeds 1/10th of the signal wavelength and must be controlled with **termination resistors** — series termination at the source for TTL point-to-point, parallel termination at the load for RS-485 buses, with Thevenin bias to define the idle bus state.

**Noise immunity** is achieved through a combination of: differential signalling (RS-485 provides ±25 V common-mode rejection), shielded and twisted-pair cable, ferrite beads and decoupling capacitors at transceiver ICs, and ESD protection on external connectors.

**Software complements hardware** by monitoring UART status register error bits (framing, parity, noise flag, overrun), computing error rates, and triggering adaptive measures such as baud rate reduction when signal quality degrades. In C/C++ this can be achieved with atomic counters updated from ISR context and evaluated in the main loop; in Rust, the `core::sync::atomic` types provide ISR-safe counters in `no_std` environments.

**RS-485 half-duplex direction control** requires careful management of the DE/RE pin with pre- and post-transmission guard times to prevent bus contention and allow the cable's signal to settle before releasing the driver.

Together, these hardware and software practices ensure that a UART communication link remains reliable across varying cable lengths, environmental noise levels, and operating conditions.

---

*Document: UART Signal Integrity — Part of the UART Programming Guide series*