# 80. SPI Impedance Matching

**Physics & Theory** — Transmission line modelling, the telegrapher's equations, characteristic impedance formulas for microstrip and stripline, and the critical-length rule-of-thumb table (1 MHz → 50 MHz SPI).

**Signal Integrity Problems** — Ringing/overshoot, crosstalk (capacitive and inductive), ground bounce, and EMI.

**Termination Strategies** — Five topologies with schematics: Series (source), Parallel (shunt), AC/capacitive, Thévenin (split), and Differential, each with values, advantages, and trade-offs.

**PCB Layout** — Controlled impedance trace widths, ground plane continuity, via parasitics, length matching formulas, and decoupling.

**C/C++ Code Examples:**
- HAL-agnostic drive strength / slew rate control
- BER loopback test with alternating 0xAA/0x55 stress pattern
- Adaptive drive strength sweep
- Template-based `AdaptiveSpi<HalDriver>` class with binary-search clock selection
- `constexpr` impedance calculation helpers (`microstrip_z0`, `critical_length_mm`, `reflection_coefficient`)

**Rust Code Examples:**
- `no_std` `SpiHardware` trait + `measure_ber()` with three pattern types
- `AdaptiveClock<H>` binary-search clock autotuner
- Full `ImpedanceReport` struct with unit tests for Z0, reflection coefficient, and critical length

## Transmission Line Effects and Termination for High-Speed SPI

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [When Do Transmission Line Effects Matter?](#2-when-do-transmission-line-effects-matter)
3. [Transmission Line Theory for SPI](#3-transmission-line-theory-for-spi)
4. [Signal Integrity Problems](#4-signal-integrity-problems)
5. [Termination Strategies](#5-termination-strategies)
6. [PCB Layout Considerations](#6-pcb-layout-considerations)
7. [Measurement and Diagnostics in Software](#7-measurement-and-diagnostics-in-software)
8. [Impedance Matching in C/C++](#8-impedance-matching-in-cc)
9. [Impedance Matching in Rust](#9-impedance-matching-in-rust)
10. [Advanced Topics](#10-advanced-topics)
11. [Summary](#11-summary)

---

## 1. Introduction

At low SPI clock frequencies (below a few MHz), signal integrity is rarely a concern — wires and PCB traces behave as simple conductors. As clock speeds climb into the tens and hundreds of MHz range however, the physical length of interconnects becomes comparable to the wavelength of the signals being transmitted, and the interconnect itself must be treated as a **transmission line**.

Impedance mismatches along a transmission line cause **signal reflections**. These reflections can corrupt data, cause false clock edges, increase radiated emissions, and prevent reliable communication entirely. Proper impedance matching is therefore a prerequisite for robust high-speed SPI design.

This document covers:

- The physics of transmission line effects as they apply to SPI buses
- The rule-of-thumb thresholds at which impedance matching becomes necessary
- Common termination topologies and their trade-offs
- PCB layout best practices
- Software-side diagnostic and configuration techniques in both C/C++ and Rust

---

## 2. When Do Transmission Line Effects Matter?

A PCB trace must be treated as a transmission line when the **signal rise/fall time is comparable to or shorter than twice the propagation delay** of the trace.

### The Critical Length Rule

```
L_critical = T_rise / (2 × T_pd)
```

Where:
- `L_critical` — maximum trace length before transmission line effects are significant
- `T_rise` — signal rise time (10%–90%) in seconds
- `T_pd` — propagation delay per unit length (~170 ps/inch for FR4, ~60 ps/cm)

### Example Calculation

| SPI Clock | Typical Rise Time | Critical Length (FR4) |
|-----------|------------------|-----------------------|
| 1 MHz     | ~10 ns           | ~30 cm (12 inches)    |
| 10 MHz    | ~2 ns            | ~6 cm (2.4 inches)    |
| 50 MHz    | ~1 ns            | ~3 cm (1.2 inches)    |
| 100 MHz   | ~500 ps          | ~1.5 cm (0.6 inches)  |

> **Rule of thumb:** For SPI signals faster than ~20 MHz on traces longer than 5 cm, always consider impedance matching.

### Characteristic Impedance of PCB Traces

For a **microstrip** (trace on outer layer over a ground plane):

```
Z0 ≈ (87 / √(εr + 1.41)) × ln(5.98h / (0.8w + t))
```

For a **stripline** (trace buried between two ground planes):

```
Z0 ≈ (60 / √εr) × ln(4b / (0.67π × (0.8w + t)))
```

Where `εr` is the substrate dielectric constant (~4.2–4.5 for FR4), `h` is the height above the ground plane, `w` is the trace width, `t` is the trace thickness, and `b` is the distance between ground planes.

**Standard target impedance:** 50 Ω for single-ended SPI signals.

---

## 3. Transmission Line Theory for SPI

### The Telegrapher's Equations

A transmission line is modeled as distributed inductance (L) and capacitance (C) per unit length:

```
Characteristic Impedance:  Z0 = √(L/C)
Propagation velocity:       v  = 1 / √(LC)
Reflection coefficient:     Γ  = (ZL - Z0) / (ZL + Z0)
```

Where `ZL` is the load impedance at the end of the line.

### Reflection Coefficient Interpretation

| Load condition     | ZL        | Γ      | Effect                          |
|--------------------|-----------|--------|---------------------------------|
| Matched            | Z0 = 50Ω  | 0      | No reflection — ideal           |
| Open circuit       | ∞         | +1     | Full positive reflection         |
| Short circuit      | 0         | −1     | Full negative reflection         |
| High-Z CMOS input  | ~10 kΩ    | ≈ +1   | Near-total reflection (problem!) |

Most CMOS SPI devices present a very high input impedance (~10 kΩ or more). Without termination, nearly all incident signal energy reflects back toward the driver, causing ringing and overshoot.

### Time-Domain Reflection (TDR) Analogy

```
         Driver                    Load (high-Z)
           │                           │
     ──────┼────────────────────────── ┼──────
     50Ω   │        50Ω trace          │  ~∞
           │<──── propagation delay ──>│
           │                           │
    Forward wave ──────────────────>   │
                          Reflected ──>│ (Γ ≈ +1)
           │<──────────── wave         │
```

The reflected wave travels back to the driver, where it encounters the driver's output impedance. If the driver impedance is also mismatched, a second reflection occurs, creating **multiple bounces** that appear as ringing on an oscilloscope.

---

## 4. Signal Integrity Problems

### 4.1 Ringing and Overshoot

Underdamped reflections create oscillation around the logic threshold. This can:
- Cause multiple triggering of the SPI clock
- Create metastability in receiving flip-flops
- Exceed absolute maximum voltage ratings of ICs

### 4.2 Crosstalk

Adjacent SPI traces (MOSI, MISO, SCK, CS) couple electromagnetically. At high speeds, a transition on one trace induces a noise spike on its neighbor. Two mechanisms apply:

- **Capacitive (electric) crosstalk** — proportional to `dV/dt`
- **Inductive (magnetic) crosstalk** — proportional to `dI/dt`

### 4.3 Ground Bounce

Simultaneous switching of multiple SPI lines causes transient currents through shared inductance in the power/ground planes, raising or lowering the effective ground reference for all pins momentarily.

### 4.4 EMI and Radiated Emissions

Reflections and ringing increase the high-frequency spectral content of SPI signals, worsening radiated emissions and potentially causing regulatory compliance failures.

---

## 5. Termination Strategies

### 5.1 Series Termination (Source Termination)

The most common method for point-to-point SPI links.

```
Driver ──[Rs]──────────────────── Receiver (high-Z)
         │
        Rs = Z0 − Rdriver_output
```

**How it works:** The series resistor Rs combines with the driver's output impedance to match Z0. The signal travels to the load at half-amplitude, reflects with Γ = +1 (doubling to full amplitude), and the return wave is absorbed by the now-matched source.

**Values:** Typically 22 Ω–68 Ω in series (fine-tuned based on driver output impedance, usually 10–25 Ω).

**Advantages:**
- No DC power dissipation
- Simple — one resistor per signal
- Works well for single loads

**Disadvantages:**
- Signal arrives at full amplitude only after one round-trip delay
- Not suitable for multiple loads at different positions (mid-trace taps see corrupted signal)

```
           Rs
MOSI ──[33Ω]──────────────── SLAVE MOSI_IN
MISO ──────────────[33Ω]──── SLAVE MISO_OUT  (series near source)
SCK  ──[33Ω]──────────────── SLAVE SCK_IN
CS   ──[33Ω]──────────────── SLAVE CS_IN
```

### 5.2 Parallel (Shunt) Termination

Places a resistor from the signal line to VCC or GND at the far end.

```
Driver ────────────────────[Rt]──── GND
                                │
                           Receiver
```

**Value:** `Rt = Z0 = 50 Ω` (for shunt to GND)

**Advantages:**
- Good for one-to-many (broadcast) topologies
- Correct signal level seen immediately at all loads

**Disadvantages:**
- Significant DC current draw: `I = VCC / Rt` (e.g., 3.3V / 50Ω = 66 mA per line — impractical!)
- May overdrive the driver's sink capability

### 5.3 AC (Capacitive) Termination

Adds a capacitor in series with the termination resistor.

```
Driver ─────────────────── Receiver
                       │
                      [Rt]
                       │
                      [Ct]
                       │
                      GND
```

**Values:** `Rt = Z0`, `Ct` chosen so `τ = Rt × Ct ≫ T_rise`

**Advantages:**
- No DC power dissipation
- Eliminates steady-state resistive loading

**Disadvantages:**
- Effective only for signals with a stable DC component
- Component selection requires careful calculation

### 5.4 Thévenin (Split) Termination

Two resistors forming a voltage divider to a mid-supply voltage.

```
             VCC
              │
             [Rpu]  ← pull-up
              │
Driver ───────┼─────────────── Receiver
              │
             [Rpd]  ← pull-down
              │
             GND
```

**Values:** `Rpu = Rpd = 2 × Z0 = 100 Ω` (parallel combination = 50 Ω)

**Advantages:**
- Provides a defined idle state
- Moderate power dissipation

**Disadvantages:**
- Power consumed even when idle: `P = VCC² / (Rpu + Rpd)`
- Requires matched resistors for clean termination

### 5.5 Differential Termination (for SPI variants)

Some high-speed SPI variants use differential signaling (e.g., LVDS-based). A differential termination resistor is placed across the pair at the receiver:

```
DIFF+ ─────────────[Rt = 100Ω]───── DIFF−
                        │
                    Receiver
```

---

## 6. PCB Layout Considerations

### 6.1 Controlled Impedance Traces

For 50 Ω microstrip on FR4 (εr ≈ 4.3, h = 0.1 mm):

```
Trace width ≈ 0.18 mm  (for h = 0.1 mm, t = 35 µm)
Trace width ≈ 0.25 mm  (for h = 0.15 mm, t = 35 µm)
```

Most PCB manufacturers offer 50 Ω controlled impedance stackups. Always request an impedance control note on your fabrication drawings.

### 6.2 Ground Plane Continuity

- Never route SPI traces over ground plane gaps or splits
- A gap in the return path forces current to detour, creating a loop antenna
- Keep the reference plane continuous beneath all high-speed SPI traces

### 6.3 Via Placement

Each via introduces approximately 0.5–1 nH of inductance and 0.1–0.3 pF of capacitance. At high speeds this creates an impedance discontinuity. Minimize layer changes for high-speed SPI signals.

### 6.4 Length Matching

For synchronous SPI, the clock and data traces should be length-matched to within a fraction of the unit interval:

```
ΔL_max ≈ T_setup / T_pd
```

For 100 MHz SPI (T_bit = 10 ns, assuming T_setup = 1 ns):

```
ΔL_max = 1 ns / 170 ps/inch ≈ 6 mm
```

### 6.5 Decoupling Capacitors

Place 100 nF + 10 µF decoupling capacitors as close as possible to every VCC pin of SPI devices. Use short, low-inductance paths to the ground plane.

---

## 7. Measurement and Diagnostics in Software

Before diving into termination code, software can assist in diagnosing signal integrity issues by monitoring error rates, adjusting drive strengths, and tuning timing margins.

### 7.1 Drive Strength Configuration

Many microcontrollers allow configuring the GPIO drive strength. Reducing drive strength slows rise times, which can reduce overshoot without external resistors:

```c
// STM32 example: configure GPIO drive strength
// Slower slew rate reduces high-frequency content
GPIO_InitTypeDef GPIO_InitStruct = {0};
GPIO_InitStruct.Pin   = GPIO_PIN_5;           // SCK
GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
GPIO_InitStruct.Pull  = GPIO_NOPULL;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;  // LOW vs MEDIUM vs HIGH vs VERY_HIGH
HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

### 7.2 Loopback BER Testing

A software loopback test quantifies signal integrity degradation:

```c
// Connect MOSI to MISO externally, run a pattern, count bit errors
uint32_t spi_loopback_ber_test(SPI_HandleTypeDef *hspi,
                               uint32_t num_bytes) {
    uint8_t tx_buf[256], rx_buf[256];
    uint32_t errors = 0;

    // Fill with alternating 0xAA/0x55 — worst-case for ringing
    for (int i = 0; i < 256; i++)
        tx_buf[i] = (i % 2 == 0) ? 0xAA : 0x55;

    HAL_SPI_TransmitReceive(hspi, tx_buf, rx_buf, 256, HAL_MAX_DELAY);

    for (int i = 0; i < 256; i++)
        if (tx_buf[i] != rx_buf[i])
            errors += __builtin_popcount(tx_buf[i] ^ rx_buf[i]);

    return errors;  // Bit error count
}
```

---

## 8. Impedance Matching in C/C++

The following examples demonstrate software-side configuration, diagnostics, and adaptive techniques relevant to SPI impedance matching at the embedded systems level.

### 8.1 Drive Strength and Slew Rate Control (HAL-Agnostic C)

```c
/**
 * @file spi_signal_integrity.c
 * @brief SPI drive strength and slew rate management for impedance-matched designs.
 *
 * Physical termination resistors handle the hardware side. Software controls
 * drive strength to optimize waveform quality in conjunction with termination.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── Platform register abstraction ─────────────────────────────────────── */
/* Replace with real MMIO access for your target MCU.                       */
typedef volatile uint32_t vu32;

#define GPIO_BASE     0x40020000UL
#define OSPEEDR_OFF   0x08UL   /* Output speed register offset              */
#define OTYPER_OFF    0x04UL   /* Output type register offset               */

typedef enum {
    DRIVE_LOW       = 0,  /* ~2 MHz,  lowest EMI, longest rise time         */
    DRIVE_MEDIUM    = 1,  /* ~25 MHz                                        */
    DRIVE_HIGH      = 2,  /* ~50 MHz                                        */
    DRIVE_VERY_HIGH = 3   /* ~100 MHz, highest EMI, shortest rise time      */
} gpio_drive_t;

/**
 * @brief Set GPIO output drive strength (slew rate) for SPI pins.
 * @param gpio_base   Base address of the GPIO port.
 * @param pin_mask    Bitmask of pins to configure (bit N = pin N).
 * @param drive       Desired drive strength.
 */
void spi_set_drive_strength(uint32_t gpio_base,
                            uint16_t pin_mask,
                            gpio_drive_t drive) {
    vu32 *ospeedr = (vu32 *)(gpio_base + OSPEEDR_OFF);
    uint32_t reg = *ospeedr;

    for (int pin = 0; pin < 16; pin++) {
        if (pin_mask & (1u << pin)) {
            reg &= ~(3u << (pin * 2));           /* Clear existing bits    */
            reg |=  ((uint32_t)drive << (pin * 2)); /* Set new drive level */
        }
    }
    *ospeedr = reg;
}

/* ── Bit-Error-Rate (BER) measurement ──────────────────────────────────── */

typedef struct {
    uint32_t total_bits;
    uint32_t error_bits;
    float    ber;           /* Bit error rate (0.0 = perfect)              */
    bool     pass;          /* true if BER < threshold                     */
} ber_result_t;

/**
 * @brief Transmit a pseudo-random test pattern and count bit errors.
 *
 * Requires MOSI externally looped back to MISO on the PCB or test jig.
 * The alternating pattern 0xAA/0x55 exercises both logic levels on every
 * bit position and is maximally stressful for transmission line ringing.
 *
 * @param spi_transmit_receive  Platform SPI full-duplex function pointer.
 * @param ctx                   Driver context passed to the function.
 * @param num_bytes             Number of bytes to transfer.
 * @param ber_threshold         Maximum acceptable BER (e.g., 1e-6f).
 * @return                      BER measurement result.
 */
ber_result_t spi_measure_ber(
    int (*spi_transmit_receive)(void *ctx,
                                const uint8_t *tx,
                                uint8_t *rx,
                                size_t len),
    void    *ctx,
    size_t   num_bytes,
    float    ber_threshold)
{
    static uint8_t tx_buf[1024];
    static uint8_t rx_buf[1024];

    if (num_bytes > sizeof(tx_buf))
        num_bytes = sizeof(tx_buf);

    /* Build alternating stress pattern                                     */
    for (size_t i = 0; i < num_bytes; i++)
        tx_buf[i] = (i & 1) ? 0x55u : 0xAAu;

    memset(rx_buf, 0, num_bytes);
    spi_transmit_receive(ctx, tx_buf, rx_buf, num_bytes);

    /* Count bit errors using XOR + popcount                                */
    uint32_t error_bits = 0;
    for (size_t i = 0; i < num_bytes; i++) {
        uint8_t diff = tx_buf[i] ^ rx_buf[i];
        /* Brian Kernighan bit count                                        */
        while (diff) { error_bits++; diff &= (diff - 1); }
    }

    ber_result_t result = {
        .total_bits = (uint32_t)(num_bytes * 8),
        .error_bits = error_bits,
        .ber        = (float)error_bits / (float)(num_bytes * 8),
        .pass       = ((float)error_bits / (float)(num_bytes * 8)) < ber_threshold
    };
    return result;
}

/* ── Adaptive drive strength sweep ─────────────────────────────────────── */

/**
 * @brief Sweep drive strengths and select the lowest setting with BER=0.
 *
 * This avoids unnecessary over-driving (which worsens EMI and reflections)
 * while guaranteeing reliable data transfer.
 *
 * @param gpio_base     GPIO port base address.
 * @param spi_pins      Pin mask for SCK, MOSI (CS is often non-critical).
 * @param measure_fn    BER measurement function.
 * @param ctx           SPI driver context.
 * @return              Optimal gpio_drive_t, or DRIVE_VERY_HIGH if all fail.
 */
gpio_drive_t spi_autotune_drive(
    uint32_t     gpio_base,
    uint16_t     spi_pins,
    ber_result_t (*measure_fn)(void *ctx),
    void         *ctx)
{
    static const gpio_drive_t levels[] = {
        DRIVE_LOW, DRIVE_MEDIUM, DRIVE_HIGH, DRIVE_VERY_HIGH
    };

    for (int i = 0; i < 4; i++) {
        spi_set_drive_strength(gpio_base, spi_pins, levels[i]);

        /* Allow transient to settle before measuring                      */
        for (volatile int d = 0; d < 1000; d++) (void)d;

        ber_result_t result = measure_fn(ctx);
        if (result.pass)
            return levels[i];
    }
    return DRIVE_VERY_HIGH;
}

/* ── Termination verification via eye diagram metrics ──────────────────── */

/**
 * @brief Simplified eye diagram quality metric using sampled voltages.
 *
 * A real implementation would use an ADC or oscilloscope capture.
 * This skeleton shows the structure for automated margin testing.
 *
 * @param samples       Array of ADC samples taken at bit-centre.
 * @param n             Number of samples.
 * @param vdd_mv        Supply voltage in millivolts.
 * @return              Eye opening ratio (0.0 to 1.0).
 */
float spi_eye_opening(const uint16_t *samples,
                      size_t          n,
                      uint16_t        vdd_mv)
{
    uint16_t v_min_hi = UINT16_MAX;  /* Lowest '1' sample seen             */
    uint16_t v_max_lo = 0;           /* Highest '0' sample seen            */

    for (size_t i = 0; i < n; i++) {
        uint16_t threshold = vdd_mv / 2;
        if (samples[i] > threshold && samples[i] < v_min_hi)
            v_min_hi = samples[i];
        if (samples[i] <= threshold && samples[i] > v_max_lo)
            v_max_lo = samples[i];
    }

    if (v_min_hi <= v_max_lo) return 0.0f;  /* Closed eye — failure        */

    return (float)(v_min_hi - v_max_lo) / (float)vdd_mv;
}
```

### 8.2 Multi-Speed Adaptive SPI Controller (C++)

```cpp
/**
 * @file AdaptiveSpi.hpp
 * @brief Adaptive SPI controller that adjusts clock and drive based on SI metrics.
 *
 * High-speed SPI on long traces may require the host to back off from the
 * maximum clock frequency if the physical channel cannot support it. This
 * class implements a binary-search algorithm over SPI clock prescalers,
 * selecting the fastest setting that achieves a target BER.
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <array>
#include <optional>

/// Platform-specific SPI configuration — fill in for your MCU.
struct SpiConfig {
    uint32_t base_clock_hz;   ///< Peripheral input clock (e.g., 72 MHz)
    uint8_t  prescaler;       ///< Clock divider exponent: f_sck = base / 2^prescaler
    uint8_t  drive_strength;  ///< 0=low … 3=very high
    uint8_t  cpol;            ///< Clock polarity
    uint8_t  cpha;            ///< Clock phase
};

/// Summary of one BER measurement sweep.
struct BerSweep {
    uint32_t sck_hz;
    uint32_t error_bits;
    uint32_t total_bits;
    float    ber;
    bool     pass;
};

/**
 * @brief Adaptive SPI manager.
 * @tparam HalDriver  Type exposing:
 *                    - void configure(const SpiConfig&)
 *                    - int  transfer(const uint8_t* tx, uint8_t* rx, size_t len)
 */
template<typename HalDriver>
class AdaptiveSpi {
public:
    static constexpr size_t   PATTERN_LEN     = 512;
    static constexpr float    TARGET_BER      = 0.0f;   ///< Zero errors required
    static constexpr uint8_t  MIN_PRESCALER   = 1;      ///< f/2
    static constexpr uint8_t  MAX_PRESCALER   = 7;      ///< f/128

    explicit AdaptiveSpi(HalDriver &hal, SpiConfig initial_cfg)
        : hal_(hal), cfg_(initial_cfg)
    {
        /* Prepare alternating stress pattern once */
        for (size_t i = 0; i < PATTERN_LEN; i++)
            pattern_[i] = static_cast<uint8_t>((i & 1u) ? 0x55u : 0xAAu);
    }

    /**
     * @brief Run a binary search over prescaler values.
     *
     * Starts at the fastest setting (MIN_PRESCALER) and falls back until
     * a BER=0 setting is found. Records results for all tested settings.
     *
     * @return The selected SpiConfig, or std::nullopt on total failure.
     */
    std::optional<SpiConfig> autotune() {
        sweep_results_.fill({});
        sweep_count_ = 0;

        uint8_t lo = MIN_PRESCALER;
        uint8_t hi = MAX_PRESCALER;

        SpiConfig best = cfg_;
        best.prescaler = MAX_PRESCALER;   /* Fallback: slowest */
        bool found = false;

        while (lo <= hi) {
            uint8_t mid = lo + (hi - lo) / 2;
            SpiConfig test_cfg = cfg_;
            test_cfg.prescaler = mid;

            hal_.configure(test_cfg);

            BerSweep result = measure_ber(test_cfg);
            record_result(result);

            if (result.pass) {
                best  = test_cfg;
                found = true;
                hi    = mid - 1;   /* Try faster */
            } else {
                lo = mid + 1;      /* Too fast — slow down */
            }
        }

        if (found) {
            cfg_ = best;
            hal_.configure(cfg_);
            return cfg_;
        }
        return std::nullopt;  /* All prescalers failed — hardware problem */
    }

    /// Return the most recently selected configuration.
    const SpiConfig &current_config() const { return cfg_; }

    /// Access sweep results for logging or diagnostics.
    const std::array<BerSweep, 8> &sweep_results() const {
        return sweep_results_;
    }

    /// Perform a single full-duplex transfer with the current configuration.
    int transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
        return hal_.transfer(tx, rx, len);
    }

private:
    HalDriver  &hal_;
    SpiConfig   cfg_;

    std::array<uint8_t,   PATTERN_LEN> pattern_{};
    std::array<uint8_t,   PATTERN_LEN> rx_buf_{};
    std::array<BerSweep, 8>            sweep_results_{};
    size_t                             sweep_count_ = 0;

    BerSweep measure_ber(const SpiConfig &cfg) {
        std::memset(rx_buf_.data(), 0, PATTERN_LEN);
        hal_.transfer(pattern_.data(), rx_buf_.data(), PATTERN_LEN);

        uint32_t errors = 0;
        for (size_t i = 0; i < PATTERN_LEN; i++) {
            uint8_t diff = pattern_[i] ^ rx_buf_[i];
            while (diff) { errors++; diff &= diff - 1u; }
        }

        const uint32_t total = PATTERN_LEN * 8;
        return BerSweep{
            .sck_hz     = cfg.base_clock_hz >> cfg.prescaler,
            .error_bits = errors,
            .total_bits = total,
            .ber        = static_cast<float>(errors) / static_cast<float>(total),
            .pass       = (errors == 0)
        };
    }

    void record_result(const BerSweep &r) {
        if (sweep_count_ < sweep_results_.size())
            sweep_results_[sweep_count_++] = r;
    }
};
```

### 8.3 Termination Resistor Calculation Helper (C++)

```cpp
/**
 * @file termination_calc.hpp
 * @brief Compile-time and runtime impedance matching calculations.
 */

#pragma once
#include <cmath>
#include <cstdint>

namespace spi_si {

/// PCB substrate parameters.
struct Substrate {
    float er;      ///< Dielectric constant (FR4 ≈ 4.3)
    float h_mm;    ///< Height above ground plane [mm]
    float t_mm;    ///< Trace thickness [mm]
};

/// Microstrip characteristic impedance (IPC-2141A approximation).
/// @param w_mm  Trace width [mm]
constexpr float microstrip_z0(float w_mm,
                               const Substrate &sub) {
    return (87.0f / std::sqrt(sub.er + 1.41f)) *
           std::log(5.98f * sub.h_mm / (0.8f * w_mm + sub.t_mm));
}

/// Required series termination resistor.
/// @param z0         Line impedance [Ω]
/// @param r_driver   Driver output impedance [Ω]
constexpr float series_term_resistor(float z0, float r_driver) {
    float rs = z0 - r_driver;
    return rs > 0.0f ? rs : 0.0f;
}

/// Propagation delay on microstrip [ps/mm].
constexpr float propagation_delay_ps_per_mm(const Substrate &sub) {
    return 3.33564f * std::sqrt(0.475f * sub.er + 0.67f);
}

/// Critical trace length [mm] above which transmission line effects apply.
/// @param rise_time_ps  Signal 10%–90% rise time [ps]
constexpr float critical_length_mm(float rise_time_ps,
                                    const Substrate &sub) {
    float tpd = propagation_delay_ps_per_mm(sub);
    return rise_time_ps / (2.0f * tpd);
}

/// Reflection coefficient for a given load impedance.
constexpr float reflection_coefficient(float z_load, float z0) {
    return (z_load - z0) / (z_load + z0);
}

/* ── Example usage ────────────────────────────────────────────────────── */
/*
    constexpr Substrate fr4 = { .er = 4.3f, .h_mm = 0.1f, .t_mm = 0.035f };

    constexpr float z0  = microstrip_z0(0.18f, fr4);   // ≈ 50 Ω
    constexpr float rs  = series_term_resistor(z0, 15.0f); // driver Zout = 15Ω → Rs ≈ 35Ω
    constexpr float tpd = propagation_delay_ps_per_mm(fr4); // ≈ 7.07 ps/mm
    constexpr float lcr = critical_length_mm(1000.0f, fr4); // 1 ns rise → ≈ 71 mm critical
*/

} // namespace spi_si
```

---

## 9. Impedance Matching in Rust

### 9.1 SPI Signal Integrity Configuration (Rust + embedded-hal)

```rust
//! spi_signal_integrity.rs
//!
//! SPI impedance matching support: drive strength configuration,
//! BER measurement, and adaptive clock selection using embedded-hal traits.

#![no_std]

use core::marker::PhantomData;

/// Drive strength levels — maps to hardware register values.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum DriveStrength {
    Low      = 0, // ~2 MHz, minimal EMI
    Medium   = 1, // ~25 MHz
    High     = 2, // ~50 MHz
    VeryHigh = 3, // ~100 MHz, maximum slew rate
}

/// BER measurement result from a loopback test.
#[derive(Debug, Clone, Copy)]
pub struct BerResult {
    pub total_bits: u32,
    pub error_bits: u32,
    /// Bit error rate as a fraction [0.0, 1.0].
    pub ber: f32,
    pub pass: bool,
}

impl BerResult {
    fn compute(total: u32, errors: u32, threshold: f32) -> Self {
        let ber = if total == 0 { 0.0 } else { errors as f32 / total as f32 };
        BerResult {
            total_bits: total,
            error_bits: errors,
            ber,
            pass: ber <= threshold,
        }
    }
}

/// SPI clock prescaler configuration.
#[derive(Debug, Clone, Copy)]
pub struct ClockConfig {
    /// Base peripheral clock [Hz].
    pub base_hz: u32,
    /// Divider exponent: f_sck = base_hz / 2^prescaler_exp.
    pub prescaler_exp: u8,
}

impl ClockConfig {
    pub fn sck_hz(&self) -> u32 {
        self.base_hz >> self.prescaler_exp
    }
}

/// Trait for platform-specific SPI hardware control.
pub trait SpiHardware {
    type Error: core::fmt::Debug;

    /// Set GPIO drive strength for SPI pins.
    fn set_drive_strength(&mut self, strength: DriveStrength) -> Result<(), Self::Error>;

    /// Set SPI clock prescaler.
    fn set_clock(&mut self, cfg: ClockConfig) -> Result<(), Self::Error>;

    /// Full-duplex transfer. Returns number of bytes transferred.
    fn transfer(&mut self, tx: &[u8], rx: &mut [u8]) -> Result<usize, Self::Error>;

    /// Short busy-wait for settling after configuration changes.
    fn delay_us(&mut self, us: u32);
}

/// Pattern type for BER stress testing.
#[derive(Debug, Clone, Copy)]
pub enum TestPattern {
    /// Alternating 0xAA / 0x55 — maximum transitions, worst-case for ringing.
    Alternating,
    /// Walking ones — tests individual bit paths.
    WalkingOne,
    /// Pseudo-random (PRBS-7 approximation using XOR feedback).
    PseudoRandom { seed: u8 },
}

/// Measure BER using a loopback on the SPI bus.
///
/// # Arguments
/// * `hw`        - Mutable reference to the SPI hardware abstraction.
/// * `buf`       - Working buffer (tx) and receive buffer must be the same length.
/// * `rx`        - Receive buffer — must equal `buf` length.
/// * `pattern`   - Test pattern to transmit.
/// * `threshold` - Maximum acceptable BER (use `0.0` for zero-error requirement).
pub fn measure_ber<H: SpiHardware>(
    hw:        &mut H,
    buf:       &mut [u8],
    rx:        &mut [u8],
    pattern:   TestPattern,
    threshold: f32,
) -> Result<BerResult, H::Error> {
    assert_eq!(buf.len(), rx.len(), "tx and rx buffers must be equal length");

    // Fill transmit buffer with chosen pattern.
    match pattern {
        TestPattern::Alternating => {
            for (i, b) in buf.iter_mut().enumerate() {
                *b = if i & 1 == 0 { 0xAAu8 } else { 0x55u8 };
            }
        }
        TestPattern::WalkingOne => {
            for (i, b) in buf.iter_mut().enumerate() {
                *b = 1u8 << (i % 8);
            }
        }
        TestPattern::PseudoRandom { seed } => {
            let mut lfsr = seed;
            for b in buf.iter_mut() {
                let bit = ((lfsr >> 6) ^ (lfsr >> 5)) & 1;
                lfsr = (lfsr << 1) | bit;
                *b = lfsr;
            }
        }
    }

    rx.fill(0);
    hw.transfer(buf, rx)?;

    // Count bit errors using XOR and Hamming weight.
    let error_bits: u32 = buf.iter().zip(rx.iter()).map(|(&t, &r)| {
        let mut diff = t ^ r;
        let mut count = 0u32;
        while diff != 0 {
            count += 1;
            diff &= diff.wrapping_sub(1);
        }
        count
    }).sum();

    let total = (buf.len() as u32).saturating_mul(8);
    Ok(BerResult::compute(total, error_bits, threshold))
}

/// Adaptive SPI clock selector.
///
/// Performs a binary search over prescaler exponents to find the fastest
/// clock configuration that achieves BER=0 in a loopback test.
pub struct AdaptiveClock<H: SpiHardware> {
    hw:          H,
    base_hz:     u32,
    min_exp:     u8,   // Fastest (smallest divisor)
    max_exp:     u8,   // Slowest (largest divisor)
    current_exp: u8,
}

impl<H: SpiHardware> AdaptiveClock<H> {
    /// Create a new `AdaptiveClock` manager.
    ///
    /// # Arguments
    /// * `hw`       - Hardware abstraction.
    /// * `base_hz`  - Peripheral input clock frequency.
    /// * `min_exp`  - Minimum prescaler exponent (fastest clock, e.g., 1 → base/2).
    /// * `max_exp`  - Maximum prescaler exponent (slowest clock, e.g., 7 → base/128).
    pub fn new(hw: H, base_hz: u32, min_exp: u8, max_exp: u8) -> Self {
        assert!(min_exp <= max_exp);
        AdaptiveClock {
            hw,
            base_hz,
            min_exp,
            max_exp,
            current_exp: max_exp,
        }
    }

    /// Run binary search and configure the optimal clock.
    ///
    /// Returns the selected `ClockConfig`, or `None` if even the slowest
    /// setting produces bit errors (suggesting a hardware fault).
    pub fn autotune(
        &mut self,
        tx_buf:    &mut [u8],
        rx_buf:    &mut [u8],
        pattern:   TestPattern,
    ) -> Result<Option<ClockConfig>, H::Error> {
        let mut lo  = self.min_exp;
        let mut hi  = self.max_exp;
        let mut best: Option<u8> = None;

        while lo <= hi {
            let mid = lo + (hi - lo) / 2;
            let cfg = ClockConfig { base_hz: self.base_hz, prescaler_exp: mid };

            self.hw.set_clock(cfg)?;
            self.hw.delay_us(10);

            let result = measure_ber(&mut self.hw, tx_buf, rx_buf, pattern, 0.0)?;

            if result.pass {
                best = Some(mid);
                if mid == self.min_exp { break; }
                hi = mid - 1;   // Try faster
            } else {
                if mid == self.max_exp { break; }
                lo = mid + 1;   // Slow down
            }
        }

        if let Some(exp) = best {
            self.current_exp = exp;
            let final_cfg = ClockConfig { base_hz: self.base_hz, prescaler_exp: exp };
            self.hw.set_clock(final_cfg)?;
            Ok(Some(final_cfg))
        } else {
            Ok(None)
        }
    }

    /// Current SCK frequency in Hz.
    pub fn current_sck_hz(&self) -> u32 {
        self.base_hz >> self.current_exp
    }

    /// Borrow the underlying hardware for normal transfers.
    pub fn hardware(&mut self) -> &mut H {
        &mut self.hw
    }
}
```

### 9.2 Impedance Calculation Library (Rust, `no_std`)

```rust
//! impedance_calc.rs
//!
//! Compile-time and runtime PCB impedance matching calculations.
//! All functions are `const`-compatible where the target toolchain supports it.

/// PCB substrate parameters.
#[derive(Debug, Clone, Copy)]
pub struct Substrate {
    /// Relative dielectric constant (FR4 ≈ 4.3).
    pub er: f32,
    /// Trace height above ground plane [mm].
    pub h_mm: f32,
    /// Copper trace thickness [mm] (1 oz ≈ 0.035 mm).
    pub t_mm: f32,
}

/// Common substrate presets.
impl Substrate {
    /// Standard FR4, 2-layer, 1 oz copper, 0.1 mm dielectric.
    pub const FR4_STANDARD: Self = Substrate { er: 4.3, h_mm: 0.10, t_mm: 0.035 };
    /// High-speed FR4 variant with tighter dielectric.
    pub const FR4_HIGH_SPEED: Self = Substrate { er: 3.8, h_mm: 0.08, t_mm: 0.035 };
    /// Rogers RO4003C — low-loss laminate for very high-speed designs.
    pub const ROGERS_RO4003C: Self = Substrate { er: 3.55, h_mm: 0.10, t_mm: 0.035 };
}

/// IPC-2141A microstrip characteristic impedance [Ω].
///
/// Valid for `w/h` ratios of 0.1 to 3.0.
pub fn microstrip_z0(w_mm: f32, sub: &Substrate) -> f32 {
    (87.0 / libm::sqrtf(sub.er + 1.41)) *
        libm::logf(5.98 * sub.h_mm / (0.8 * w_mm + sub.t_mm))
}

/// Propagation delay on a microstrip [ps/mm].
pub fn propagation_delay_ps_per_mm(sub: &Substrate) -> f32 {
    3.335_64 * libm::sqrtf(0.475 * sub.er + 0.67)
}

/// Critical trace length [mm] above which the line must be impedance-matched.
///
/// Based on the rule: `L_crit = T_rise / (2 × T_pd)`.
///
/// # Arguments
/// * `rise_time_ps` - Signal 10%–90% rise time [picoseconds].
pub fn critical_length_mm(rise_time_ps: f32, sub: &Substrate) -> f32 {
    rise_time_ps / (2.0 * propagation_delay_ps_per_mm(sub))
}

/// Reflection coefficient for a given load impedance.
pub fn reflection_coefficient(z_load: f32, z0: f32) -> f32 {
    (z_load - z0) / (z_load + z0)
}

/// Required series termination resistor.
///
/// # Arguments
/// * `z0`       - Line characteristic impedance [Ω].
/// * `r_driver` - Driver output impedance [Ω] (typically 10–25 Ω for CMOS).
pub fn series_termination(z0: f32, r_driver: f32) -> f32 {
    (z0 - r_driver).max(0.0)
}

/// Thévenin parallel termination resistors (Rpu = Rpd = 2 × Z0).
pub fn thevenin_termination(z0: f32) -> (f32, f32) {
    (2.0 * z0, 2.0 * z0)
}

/// AC termination time constant [ns] recommendation.
///
/// Returns the minimum RC time constant such that the capacitor appears as
/// a low impedance for the signal frequency.
///
/// # Arguments
/// * `z0`       - Line impedance [Ω].
/// * `f_max_hz` - Maximum signal frequency component (typically 5× the bit rate).
pub fn ac_termination_tau_ns(z0: f32, f_max_hz: f32) -> f32 {
    // τ >> 1/(2πf) → use 10× margin
    1.0e9 / (2.0 * core::f32::consts::PI * f_max_hz) * 10.0 / z0
}

/// Full impedance report for a SPI trace.
#[derive(Debug)]
pub struct ImpedanceReport {
    pub z0_ohm:               f32,
    pub propagation_ps_per_mm: f32,
    pub critical_length_mm:   f32,
    pub series_resistor_ohm:  f32,
    pub reflection_coeff:     f32,  // with high-Z CMOS load (~10 kΩ)
    pub needs_termination:    bool,
}

impl ImpedanceReport {
    /// Generate a full report for a trace.
    ///
    /// # Arguments
    /// * `trace_w_mm`    - Actual trace width [mm].
    /// * `trace_len_mm`  - Actual trace length [mm].
    /// * `rise_time_ps`  - Expected signal rise time [ps].
    /// * `r_driver_ohm`  - Driver output impedance [Ω].
    /// * `sub`           - PCB substrate parameters.
    pub fn new(
        trace_w_mm:   f32,
        trace_len_mm: f32,
        rise_time_ps: f32,
        r_driver_ohm: f32,
        sub:          &Substrate,
    ) -> Self {
        let z0   = microstrip_z0(trace_w_mm, sub);
        let tpd  = propagation_delay_ps_per_mm(sub);
        let lcrit = critical_length_mm(rise_time_ps, sub);

        ImpedanceReport {
            z0_ohm:                z0,
            propagation_ps_per_mm: tpd,
            critical_length_mm:   lcrit,
            series_resistor_ohm:  series_termination(z0, r_driver_ohm),
            reflection_coeff:     reflection_coefficient(10_000.0, z0),
            needs_termination:    trace_len_mm > lcrit,
        }
    }
}

// ── Unit tests ──────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fr4_standard_z0_approx_50_ohm() {
        // 0.18 mm wide trace on standard FR4 should yield ≈ 50 Ω.
        let z0 = microstrip_z0(0.18, &Substrate::FR4_STANDARD);
        assert!((z0 - 50.0).abs() < 5.0, "Z0 = {} Ω, expected ~50 Ω", z0);
    }

    #[test]
    fn high_z_load_reflects_almost_all() {
        let gamma = reflection_coefficient(10_000.0, 50.0);
        assert!(gamma > 0.99, "Γ = {}, expected > 0.99 for high-Z CMOS load", gamma);
    }

    #[test]
    fn critical_length_100mhz_spi() {
        // 100 MHz SPI: rise time ≈ 1 ns = 1000 ps
        let lcrit = critical_length_mm(1000.0, &Substrate::FR4_STANDARD);
        // Expected: roughly 70–80 mm
        assert!(lcrit > 50.0 && lcrit < 100.0,
            "lcrit = {} mm for 1 ns rise on FR4", lcrit);
    }

    #[test]
    fn termination_report_flags_long_trace() {
        let report = ImpedanceReport::new(
            0.18,    // trace width [mm]
            120.0,   // trace length [mm] — intentionally long
            500.0,   // 500 ps rise time (fast driver)
            15.0,    // 15 Ω driver output impedance
            &Substrate::FR4_STANDARD,
        );
        assert!(report.needs_termination,
            "120 mm trace with 500 ps rise should require termination");
        assert!(report.series_resistor_ohm > 0.0);
    }
}
```

---

## 10. Advanced Topics

### 10.1 Fly-by Topology for Multi-Slave SPI

When driving multiple SPI slaves, a **fly-by** (daisy-chain) topology is preferable to a star topology at high speeds:

```
Master ──Rs──┬─────────────────────────┬──── (end termination if needed)
             │                         │
         Slave 1 (CS1)            Slave 2 (CS2)
```

Each slave tap is close to the main trace rather than at the end of a long stub. Stubs cause additional impedance discontinuities and should be kept under 10 mm at 100 MHz.

### 10.2 Frequency-Domain Analysis

The −3 dB bandwidth of a series RC termination network:

```
f_−3dB = 1 / (2π × Rs × Cload)
```

Where `Cload` is the input capacitance of the receiver (typically 5–20 pF). For reliable data at `f_bit`:

```
f_−3dB ≥ 5 × f_bit
```

This sets an upper limit on how large Rs can be before it rolls off the signal too severely.

### 10.3 IBIS Models

For accurate pre-layout signal integrity simulation, use IBIS (Input/Output Buffer Information Specification) models supplied by IC vendors. These models characterize driver output impedance vs. current, input capacitance, and package parasitics — critical for choosing correct termination values without physical prototypes.

### 10.4 Differential SPI (QSPI, OctoSPI)

Modern high-density flash interfaces (Quad SPI, Octo SPI) transmit 4 or 8 data bits simultaneously. Each data line must be individually impedance-matched, and **skew between lanes must be minimized** — typically to within 10–25 ps. This requires precise length matching during PCB layout.

---

## 11. Summary

| Aspect | Key Takeaway |
|--------|-------------|
| **When to terminate** | Any SPI trace longer than `T_rise / (2 × T_pd)`; typically > 5 cm at ≥ 20 MHz |
| **Target impedance** | 50 Ω for single-ended SPI; 100 Ω differential for LVDS variants |
| **Best topology (point-to-point)** | Series termination (22–68 Ω near driver) — no DC power, simple |
| **Multi-drop SPI** | Fly-by routing; minimize stub lengths; consider parallel AC termination |
| **PCB layout** | Controlled impedance traces, continuous ground plane, minimize vias |
| **Drive strength** | Configure the minimum drive strength that yields BER = 0 |
| **Software diagnostics** | BER loopback test, adaptive clock sweep, drive strength autotuning |
| **Calculation tools** | Use impedance calculator libraries (C++ constexpr / Rust `no_std`) during design |

Proper impedance matching for high-speed SPI is a multidisciplinary effort spanning PCB stackup design, component selection, layout discipline, and software-level tuning. The physical termination network forms the foundation, and software diagnostic routines provide the visibility needed to verify and adaptively optimize the design under real operating conditions.

---

*Document: SPI Impedance Matching — Topic 80 of the SPI Programming Series*