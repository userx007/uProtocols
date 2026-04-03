# 77. SPI Rise and Fall Time Measurement

**Theory & Concepts** — definitions of t_r and t_f (10%–90% convention), slew rate, the RC model (t_r ≈ 2.2 × R × C), transmission-line critical length, and why open-drain buses have asymmetric rise vs. fall times.

**Timing Budget** — three interlocking constraints (20%-transition rule, device datasheet setup/hold/access timings, transmission-line settling), how to combine them, and why a 20% safety margin is standard practice.

**C/C++ Examples:**
- `gpio_set_drive_strength()` — OSPEEDR register configuration on STM32 to control output impedance
- Timer input-capture ISR pair for 10%/90% threshold timestamping and rise time computation
- `calculate_max_spi_freq()` and `find_max_reliable_spi_freq()` — a binary-search BER loopback tester
- A standalone host-side timing budget calculator with formatted report output

**Rust Examples:**
- `SpiTimingParams::analyze()` — full budget computation as a pure function with a struct result
- `no_std` embedded-hal module with `DriveStrength` enum, `minimum_for_rise_time()`, `select_spi_prescaler()`, and `loopback_ber_test<SPI>`
- Iterator-chain statistical analysis (mean, min, max, σ, worst-case mean+3σ) from simulated timer captures, with unit tests

**Reference tables** for typical t_r values by scenario and a quick-reference formula table round out the document.

## Characterizing Signal Transitions and Their Impact on Maximum Speed

---

## Table of Contents

1. [Introduction](#introduction)
2. [Definitions and Theory](#definitions-and-theory)
3. [Why Rise/Fall Times Matter for SPI](#why-risefall-times-matter-for-spi)
4. [Factors Influencing Rise and Fall Time](#factors-influencing-rise-and-fall-time)
5. [Measurement Methodology](#measurement-methodology)
6. [Timing Budget and Maximum Speed Calculation](#timing-budget-and-maximum-speed-calculation)
7. [C/C++ Implementation](#cc-implementation)
8. [Rust Implementation](#rust-implementation)
9. [Practical Examples and Diagnostics](#practical-examples-and-diagnostics)
10. [Summary](#summary)

---

## Introduction

In any digital communication bus, signals do not switch instantaneously between logic levels. Every edge — whether rising (low-to-high) or falling (high-to-low) — takes a finite amount of time to traverse the voltage range that separates a logic `0` from a logic `1`. These transition intervals are called **rise time** (t_r) and **fall time** (t_f).

For SPI (Serial Peripheral Interface), which is a synchronous, full-duplex bus commonly running at speeds from a few hundred kHz up to 100 MHz or more on modern microcontrollers, understanding and quantifying rise and fall times is critical. Excessive transition times relative to the clock period can cause:

- Setup and hold time violations at the receiver
- Signal integrity failures (ringing, overshoot)
- Incorrect sampling of data bits
- An effective ceiling on the maximum achievable clock frequency

This document explains the physics, measurement methods, software techniques for capturing and analyzing transitions, and how to calculate the maximum safe SPI clock frequency given measured rise/fall times.

---

## Definitions and Theory

### Rise Time (t_r)

Rise time is defined as the time it takes for a signal to transition from **10% to 90%** of its final high voltage level (V_OH).

```
                 ___________
                /
               /   ← rise time
              /
_____________/

 ^           ^
10% V_OH   90% V_OH
```

**Formula:**

```
t_r = t(90%) − t(10%)
```

### Fall Time (t_f)

Fall time is the mirror image: the time for a signal to fall from **90% to 10%** of its high voltage level.

```
_____________\
              \   ← fall time
               \
                \___________

^               ^
90% V_OH       10% V_OH
```

### Propagation Delay (t_pd)

Often paired with rise/fall, this is the delay from when a driving signal crosses 50% to when the driven signal crosses 50%. It is distinct from rise/fall time but contributes to the total timing budget.

### Slew Rate

The slew rate is a related concept — it is the rate of voltage change per unit time (V/ns or V/µs). It is the inverse relationship to rise/fall time:

```
Slew Rate = ΔV / Δt  (V/ns)
```

A high slew rate means fast transitions (small t_r, t_f). Driver ICs specify a maximum slew rate; signal lines have a maximum slew rate they can support given their load capacitance.

---

## Why Rise/Fall Times Matter for SPI

SPI is built around a clock signal (SCLK) and data lines (MOSI, MISO, CS). The master generates SCLK and data must be stable within a setup window before and after each active clock edge.

### The Timing Relationship

For a SPI transaction to be reliable, every bit period must accommodate:

```
T_bit = T_setup + T_hold + T_propagation + T_transition
```

Where `T_transition` is dominated by the rise/fall time of the relevant signals. If rise/fall times consume too large a fraction of the bit period, there is insufficient margin for valid data sampling.

### Nyquist-Like Constraint

A common rule of thumb in digital design is that the rise/fall time should be **no more than 20–30%** of the clock period for reliable operation:

```
t_r (or t_f) ≤ 0.2 × T_clock

=> f_max ≤ 1 / (5 × t_r)
```

For example, if t_r = 10 ns:

```
f_max ≤ 1 / (5 × 10 ns) = 20 MHz
```

### Reflections and Impedance

On PCB traces longer than a critical length, signals behave as transmission lines. If the line is not properly terminated, fast rise times cause reflections that distort the waveform — the received signal may ring above or below the logic thresholds, causing multiple false transitions at the receiver.

Critical length rule:
```
L_critical = (t_r / 2) × v_propagation

where v_propagation ≈ 15 cm/ns (on typical FR4 PCB)
```

For t_r = 2 ns: L_critical = 15 cm. Traces shorter than this are generally safe without termination.

---

## Factors Influencing Rise and Fall Time

### 1. Driver Output Impedance

The output stage of the driving device has a source resistance R_out. Combined with load capacitance C_L:

```
t_r ≈ 2.2 × R_out × C_L   (for RC circuit, 10%–90% definition)
```

### 2. Load Capacitance

Every wire, pin, via, and receiver input adds capacitance. The aggregate C_L directly multiplies with driver impedance to set the rise time.

Typical values:
- PCB trace: 1–3 pF/cm
- IC input pin: 2–10 pF
- Oscilloscope probe: 10–15 pF (significant — use 10:1 probes)

### 3. Pull-up / Pull-down Resistors

For open-drain signals (common in SPI variants with shared buses or level translation), the pull-up resistor R_PU dominates the rise time:

```
t_r ≈ 2.2 × R_PU × C_L
```

This is why strong pull-ups (lower resistance) are needed for high-speed buses, and why I2C (which is always open-drain) is inherently slower than push-pull SPI.

### 4. Trace Length and Inductance

Long traces add series inductance L. With capacitance, this creates an LC circuit that can resonate, causing overshoot and ringing on fast edges.

### 5. GPIO Drive Strength Configuration

Most modern microcontrollers allow the drive strength of GPIO outputs to be configured in software (e.g., 2 mA, 4 mA, 8 mA, 12 mA). Higher drive strength reduces rise/fall time but increases EMI.

---

## Measurement Methodology

### Hardware Measurement (Oscilloscope)

The canonical method uses an oscilloscope with sufficient bandwidth (bandwidth ≥ 5× the highest frequency component of interest):

1. Connect a 10:1 passive probe to the SPI line under test
2. Trigger on the rising (or falling) edge
3. Use the oscilloscope's built-in rise/fall time measurement, or manually measure between 10% and 90% cursor positions
4. Measure at the receiver end, not the driver end — this captures the effects of the transmission path

**Important:** The probe itself loads the circuit (typically 10–15 pF). For accurate measurements of fast signals (< 5 ns rise time), use a low-capacitance active probe or a matched SMA connection.

### Software Measurement

While software cannot measure nanosecond-scale transitions directly (it lacks the timing resolution), it can:

1. **Configure and test drive strength settings** to indirectly control rise/fall times
2. **Measure round-trip latency** as a proxy for signal integrity at the system level
3. **Perform loopback bit-error-rate testing** at increasing clock frequencies to find the practical maximum
4. **Read hardware timer captures** from a high-resolution timer peripheral that captures edge timestamps on GPIO inputs

The following code examples demonstrate these software-level approaches.

---

## Timing Budget and Maximum Speed Calculation

Given measured (or specified) rise/fall times, the maximum SPI clock frequency is determined by the tightest constraint:

### Constraint 1: Transition Time Fraction

```
f_max_1 = 1 / (5 × max(t_r, t_f))
```

### Constraint 2: Device Timing Specs

From the SPI slave datasheet, the minimum clock period is:

```
T_min = t_su(data) + t_h(data) + t_pd(SCLK→MISO) + t_r(MISO)
```

Where:
- `t_su` = setup time of data before clock edge
- `t_h` = hold time of data after clock edge
- `t_pd` = propagation delay from SCLK to MISO response
- `t_r` = rise time of MISO line

### Constraint 3: Transmission Line (if applicable)

If the trace is longer than the critical length and unterminated, ringing can persist for 2–4× the rise time. The clock period must exceed this settling time:

```
f_max_3 = 1 / (4 × t_r × (1 + reflection_coefficient))
```

The final maximum clock frequency is the minimum of all constraints:

```
f_max = min(f_max_1, 1/T_min, f_max_3)
```

---

## C/C++ Implementation

The following C/C++ examples target a bare-metal embedded environment (e.g., ARM Cortex-M, STM32/NRF52 family) using CMSIS or a vendor HAL. They illustrate:

1. Configuring GPIO drive strength to control slew rate
2. Measuring edge timestamps using a high-resolution timer capture
3. Computing rise/fall time from captured timestamps
4. Binary-searching for the maximum reliable SPI clock frequency via loopback

### Example 1: GPIO Drive Strength Configuration (STM32 HAL / CMSIS)

```c
/**
 * spi_signal_config.h
 * Configure GPIO drive strength and SPI clock to manage rise/fall times.
 */

#ifndef SPI_SIGNAL_CONFIG_H
#define SPI_SIGNAL_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/** GPIO drive strength levels (maps to hardware register values) */
typedef enum {
    GPIO_DRIVE_2MA  = 0,  /**< Slowest: longest rise/fall time, lowest EMI */
    GPIO_DRIVE_4MA  = 1,
    GPIO_DRIVE_8MA  = 2,
    GPIO_DRIVE_12MA = 3   /**< Fastest: shortest rise/fall time, highest EMI */
} gpio_drive_strength_t;

/**
 * @brief  Set the output drive strength of a SPI GPIO pin.
 *
 * This directly controls the source impedance of the output stage, which
 * in turn sets the RC time constant with the line capacitance:
 *
 *   t_r ≈ 2.2 × R_out × C_L
 *
 * Reducing R_out (increasing drive strength) reduces t_r proportionally.
 *
 * @param  port   GPIO port base address (e.g., GPIOA)
 * @param  pin    Pin number (0–15)
 * @param  drive  Drive strength setting
 */
void gpio_set_drive_strength(GPIO_TypeDef *port, uint8_t pin,
                             gpio_drive_strength_t drive);

/**
 * @brief  Configure all SPI bus pins to a uniform drive strength.
 *
 * Call this before spi_init(). Applies the same drive strength to
 * SCLK, MOSI, and CS — not MISO, which is an input.
 *
 * @param  drive  Target drive strength
 */
void spi_configure_drive_strength(gpio_drive_strength_t drive);

#endif /* SPI_SIGNAL_CONFIG_H */
```

```c
/**
 * spi_signal_config.c
 */

#include "spi_signal_config.h"
#include "stm32f4xx.h"   /* Adjust for your MCU family */

/* STM32 OSPEEDR register encodes drive strength as 2-bit fields */
#define OSPEEDR_MASK   0x3U
#define OSPEEDR_SHIFT(pin)  ((pin) * 2U)

/* Map logical drive levels to STM32 OSPEEDR values */
static const uint32_t drive_to_ospeedr[] = {
    [GPIO_DRIVE_2MA]  = 0x0,  /* Low speed     (~2 MHz output toggle) */
    [GPIO_DRIVE_4MA]  = 0x1,  /* Medium speed  (~25 MHz)              */
    [GPIO_DRIVE_8MA]  = 0x2,  /* High speed    (~50 MHz)              */
    [GPIO_DRIVE_12MA] = 0x3   /* Very high speed (~100 MHz)           */
};

void gpio_set_drive_strength(GPIO_TypeDef *port, uint8_t pin,
                             gpio_drive_strength_t drive)
{
    uint32_t shift = OSPEEDR_SHIFT(pin);
    uint32_t reg   = port->OSPEEDR;

    /* Clear existing 2-bit field */
    reg &= ~(OSPEEDR_MASK << shift);
    /* Set new value */
    reg |= (drive_to_ospeedr[drive] << shift);

    port->OSPEEDR = reg;
}

/* Pin definitions for a typical SPI1 on STM32F4 */
#define SPI1_SCLK_PORT   GPIOA
#define SPI1_SCLK_PIN    5U
#define SPI1_MOSI_PORT   GPIOA
#define SPI1_MOSI_PIN    7U
#define SPI1_CS_PORT     GPIOA
#define SPI1_CS_PIN      4U

void spi_configure_drive_strength(gpio_drive_strength_t drive)
{
    gpio_set_drive_strength(SPI1_SCLK_PORT, SPI1_SCLK_PIN, drive);
    gpio_set_drive_strength(SPI1_MOSI_PORT, SPI1_MOSI_PIN, drive);
    gpio_set_drive_strength(SPI1_CS_PORT,   SPI1_CS_PIN,   drive);
    /* MISO is an input — drive strength does not apply */
}
```

---

### Example 2: Rise/Fall Time Measurement via Timer Input Capture

This example uses a hardware timer in input-capture mode to timestamp edges on the MISO line, allowing software to compute the time between the 10% and 90% crossing points. A dual-threshold comparator (or the MCU's internal analog comparator) generates separate interrupts at each threshold.

```c
/**
 * rise_fall_measure.c
 *
 * Measures rise and fall times on a SPI line using dual-threshold
 * input capture. Two comparator outputs (one set to V_10 = 0.1×VDD,
 * one to V_90 = 0.9×VDD) each trigger a timer capture channel.
 *
 * Timer resolution required:
 *   For t_r ~ 5 ns, need ≥ 1 ns timer resolution.
 *   At 168 MHz (STM32F4), TIM2 with prescaler=0 gives ~5.95 ns/tick.
 *   For sub-ns accuracy, use an external logic analyzer or oscilloscope.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Data types                                                           */
/* ------------------------------------------------------------------ */

/** Raw capture timestamps from the two-threshold comparator setup */
typedef struct {
    uint32_t ts_10pct;   /**< Timer count when signal crossed 10% threshold */
    uint32_t ts_90pct;   /**< Timer count when signal crossed 90% threshold */
    bool     valid;      /**< Both captures occurred in same transition     */
} edge_capture_t;

/** Computed timing result */
typedef struct {
    float rise_time_ns;  /**< 10%→90% rise time in nanoseconds  */
    float fall_time_ns;  /**< 90%→10% fall time in nanoseconds  */
    uint32_t sample_count;
} rise_fall_result_t;

/* ------------------------------------------------------------------ */
/* Hardware-specific constants                                          */
/* ------------------------------------------------------------------ */

#define TIMER_FREQ_HZ     168000000UL   /**< 168 MHz timer clock         */
#define TIMER_TICK_NS     (1e9f / TIMER_FREQ_HZ)  /**< ~5.95 ns per tick */

/* Capture buffers filled by ISR */
#define MAX_CAPTURES 256
static volatile edge_capture_t rise_captures[MAX_CAPTURES];
static volatile edge_capture_t fall_captures[MAX_CAPTURES];
static volatile uint32_t rise_idx = 0;
static volatile uint32_t fall_idx = 0;

/* ------------------------------------------------------------------ */
/* ISR handlers (must be registered in vector table)                   */
/* ------------------------------------------------------------------ */

/**
 * @brief  Capture Channel 1 ISR — fires at V_10 (10% threshold).
 *
 * On a rising edge, this is the START of the transition.
 * On a falling edge, this is the END of the transition.
 */
void TIM2_CC1_IRQHandler(void)
{
    static uint32_t last_ts10 = 0;
    uint32_t ts = TIM2->CCR1;   /* Hardware latches count on edge */

    /* Store for pairing with the 90% capture */
    last_ts10 = ts;
    (void)last_ts10; /* suppress unused warning; used by CC2 handler logic */

    /* Clear interrupt flag */
    TIM2->SR &= ~TIM_SR_CC1IF;
}

/**
 * @brief  Capture Channel 2 ISR — fires at V_90 (90% threshold).
 *
 * On a rising edge, this is the END of the transition (pair with CC1).
 */
void TIM2_CC2_IRQHandler(void)
{
    extern uint32_t g_last_ts10;  /* shared with CC1 handler */
    uint32_t ts90 = TIM2->CCR2;

    if (rise_idx < MAX_CAPTURES) {
        rise_captures[rise_idx].ts_10pct = g_last_ts10;
        rise_captures[rise_idx].ts_90pct = ts90;
        rise_captures[rise_idx].valid    = true;
        rise_idx++;
    }

    TIM2->SR &= ~TIM_SR_CC2IF;
}

/* ------------------------------------------------------------------ */
/* Analysis                                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief  Compute mean rise time from captured edge data.
 *
 * Filters out obviously invalid samples (e.g., timer wraparound
 * within a single short transition period).
 *
 * @param  captures  Array of edge capture records
 * @param  count     Number of valid captures
 * @param  result    Output: computed rise time statistics
 */
void compute_rise_time(const volatile edge_capture_t *captures,
                       uint32_t count,
                       rise_fall_result_t *result)
{
    if (count == 0) {
        result->rise_time_ns   = 0.0f;
        result->sample_count   = 0;
        return;
    }

    float sum = 0.0f;
    uint32_t valid = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (!captures[i].valid) continue;

        /* Handle 32-bit timer wraparound */
        uint32_t delta;
        if (captures[i].ts_90pct >= captures[i].ts_10pct) {
            delta = captures[i].ts_90pct - captures[i].ts_10pct;
        } else {
            delta = (0xFFFFFFFFUL - captures[i].ts_10pct)
                    + captures[i].ts_90pct + 1UL;
        }

        /* Sanity check: reject if delta > 1 ms (likely a noise spike) */
        float t_ns = (float)delta * TIMER_TICK_NS;
        if (t_ns > 1e6f) continue;

        sum += t_ns;
        valid++;
    }

    result->rise_time_ns = (valid > 0) ? (sum / (float)valid) : 0.0f;
    result->sample_count = valid;
}

/* ------------------------------------------------------------------ */
/* Maximum frequency calculation                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief  Calculate the maximum safe SPI clock frequency.
 *
 * Applies the rule: t_r (or t_f) must be ≤ 20% of the clock period.
 *
 *   f_max = 1 / (5 × max(t_r, t_f))
 *
 * Also applies datasheet timing constraints if provided.
 *
 * @param  t_rise_ns     Measured rise time (ns)
 * @param  t_fall_ns     Measured fall time (ns)
 * @param  t_setup_ns    Device setup time spec from datasheet (ns)
 * @param  t_hold_ns     Device hold time spec from datasheet (ns)
 * @param  t_prop_ns     Max propagation delay SCLK→data (ns)
 * @return Maximum recommended SPI clock frequency in Hz
 */
uint32_t calculate_max_spi_freq(float t_rise_ns, float t_fall_ns,
                                float t_setup_ns, float t_hold_ns,
                                float t_prop_ns)
{
    /* Constraint 1: Transition time ≤ 20% of period */
    float t_max_transition = (t_rise_ns > t_fall_ns) ? t_rise_ns : t_fall_ns;
    float f_max_transition = 1.0f / (5.0f * t_max_transition * 1e-9f);

    /* Constraint 2: Device timing spec
     *   T_min = t_setup + t_hold + t_prop + t_rise
     */
    float t_min_period_ns = t_setup_ns + t_hold_ns + t_prop_ns + t_rise_ns;
    float f_max_device    = 1.0f / (t_min_period_ns * 1e-9f);

    /* Return the more restrictive of the two */
    float f_max = (f_max_transition < f_max_device)
                  ? f_max_transition : f_max_device;

    /* Apply a 20% safety margin */
    f_max *= 0.80f;

    return (uint32_t)f_max;
}

/* ------------------------------------------------------------------ */
/* Loopback BER test to validate a given clock frequency               */
/* ------------------------------------------------------------------ */

/**
 * @brief  Transmit a known pattern via SPI loopback and count bit errors.
 *
 * MOSI is physically connected to MISO on the test board. Any bit error
 * is a direct consequence of signal integrity failure — the received bit
 * does not match the transmitted bit. This gives a practical pass/fail
 * criterion for a given SPI clock frequency.
 *
 * @param  spi          SPI peripheral handle
 * @param  clock_hz     Clock frequency to test
 * @param  iterations   Number of 8-bit transfers to perform
 * @return Bit error count (0 = pass)
 */
uint32_t spi_loopback_ber_test(SPI_HandleTypeDef *spi,
                               uint32_t clock_hz,
                               uint32_t iterations)
{
    /* Reconfigure SPI baud rate */
    /* (Implementation depends on HAL; shown conceptually) */
    spi->Init.BaudRatePrescaler = spi_freq_to_prescaler(clock_hz);
    HAL_SPI_Init(spi);

    /* Test pattern: alternating 0xAA/0x55 stresses all bit positions */
    static const uint8_t patterns[] = { 0xAA, 0x55, 0xFF, 0x00,
                                        0xA5, 0x5A, 0x0F, 0xF0 };
    uint32_t num_patterns = sizeof(patterns) / sizeof(patterns[0]);
    uint32_t error_count  = 0;

    for (uint32_t i = 0; i < iterations; i++) {
        uint8_t tx = patterns[i % num_patterns];
        uint8_t rx = 0;

        HAL_SPI_TransmitReceive(spi, &tx, &rx, 1, HAL_MAX_DELAY);

        if (rx != tx) {
            /* Count individual bit errors */
            uint8_t diff = rx ^ tx;
            while (diff) {
                error_count += (diff & 1U);
                diff >>= 1;
            }
        }
    }

    return error_count;
}

/**
 * @brief  Binary search for the maximum reliable SPI clock frequency.
 *
 * Starts with a known-good low frequency and a candidate high frequency,
 * then narrows down to find the highest frequency with zero bit errors.
 *
 * @param  spi         SPI peripheral handle
 * @param  f_low_hz    Lower bound (known good), e.g. 1 MHz
 * @param  f_high_hz   Upper bound to test, e.g. 50 MHz
 * @param  iterations  Transfers per test (more = higher confidence)
 * @return Highest frequency (Hz) with zero BER
 */
uint32_t find_max_reliable_spi_freq(SPI_HandleTypeDef *spi,
                                    uint32_t f_low_hz,
                                    uint32_t f_high_hz,
                                    uint32_t iterations)
{
    uint32_t best = f_low_hz;

    for (int step = 0; step < 16; step++) {   /* 16 iterations → ~0.001% precision */
        uint32_t mid = f_low_hz + (f_high_hz - f_low_hz) / 2;

        uint32_t errors = spi_loopback_ber_test(spi, mid, iterations);

        if (errors == 0) {
            best     = mid;
            f_low_hz = mid;    /* Mid was good: search higher half */
        } else {
            f_high_hz = mid;   /* Mid failed: search lower half    */
        }

        /* Termination: range collapsed to < 1% */
        if ((f_high_hz - f_low_hz) < (best / 100)) break;
    }

    return best;
}

/* ------------------------------------------------------------------ */
/* Main demonstration                                                   */
/* ------------------------------------------------------------------ */

void rise_fall_demo(void)
{
    /* --- Hypothetical measured values (would come from ISR captures) --- */
    float t_rise_ns = 8.5f;    /* Measured from loopback capture */
    float t_fall_ns = 6.2f;    /* Typically faster (active pull-down) */

    /* --- Device datasheet specs (example: SPI flash memory) --- */
    float t_setup_ns = 5.0f;
    float t_hold_ns  = 3.0f;
    float t_prop_ns  = 7.0f;

    uint32_t f_max = calculate_max_spi_freq(t_rise_ns, t_fall_ns,
                                            t_setup_ns, t_hold_ns,
                                            t_prop_ns);

    printf("Rise time:       %.1f ns\n", t_rise_ns);
    printf("Fall time:       %.1f ns\n", t_fall_ns);
    printf("Max SPI freq:    %lu Hz (%.1f MHz)\n",
           f_max, (float)f_max / 1e6f);

    /* --- Drive strength tuning --- */
    /* Start conservative, increase until BER appears */
    const gpio_drive_strength_t strengths[] = {
        GPIO_DRIVE_2MA, GPIO_DRIVE_4MA,
        GPIO_DRIVE_8MA, GPIO_DRIVE_12MA
    };
    const char *strength_names[] = { "2 mA", "4 mA", "8 mA", "12 mA" };

    for (int i = 0; i < 4; i++) {
        spi_configure_drive_strength(strengths[i]);
        /* In a real system: re-measure rise time here with the timer capture */
        printf("Drive strength %s configured\n", strength_names[i]);
    }
}
```

---

### Example 3: Timing Budget Calculator (Pure C, Host/Desktop)

```c
/**
 * spi_timing_budget.c
 *
 * Standalone utility that accepts measured rise/fall times and device
 * timing specs, and produces a complete SPI timing budget report.
 *
 * Compile: gcc -o spi_timing spi_timing_budget.c -lm
 * Usage:   ./spi_timing
 */

#include <stdio.h>
#include <math.h>
#include <float.h>

typedef struct {
    const char *name;
    float t_rise_ns;
    float t_fall_ns;
    float t_setup_ns;     /* data setup before clock edge */
    float t_hold_ns;      /* data hold after clock edge   */
    float t_access_ns;    /* clock-to-output delay (slave) */
    float c_load_pf;      /* estimated load capacitance   */
    float r_driver_ohm;   /* driver output impedance      */
} spi_scenario_t;

static void print_budget(const spi_scenario_t *s)
{
    printf("\n========================================\n");
    printf("  SPI Timing Budget: %s\n", s->name);
    printf("========================================\n");

    /* Derived rise time from RC model (cross-check) */
    float t_rc_ns = 2.2f * s->r_driver_ohm * s->c_load_pf * 1e-3f;
    /* (R in Ω × C in pF × 1e-3 gives ns: 2.2×50×100×1e-3 = 11 ns) */

    printf("\n--- Signal Characteristics ---\n");
    printf("  Measured rise time:       %6.1f ns\n", s->t_rise_ns);
    printf("  Measured fall time:       %6.1f ns\n", s->t_fall_ns);
    printf("  RC-model rise time est.:  %6.1f ns  (R=%.0fΩ, C=%.0fpF)\n",
           t_rc_ns, s->r_driver_ohm, s->c_load_pf);
    printf("  Critical trace length:    %6.1f cm  (v=15 cm/ns)\n",
           (s->t_rise_ns / 2.0f) * 15.0f);

    float t_transition = fmaxf(s->t_rise_ns, s->t_fall_ns);

    /* Constraint 1: 20% transition budget */
    float f1 = 1.0f / (5.0f * t_transition * 1e-9f) / 1e6f;  /* MHz */

    /* Constraint 2: device timing  */
    float t_period_min = s->t_access_ns + s->t_rise_ns
                       + s->t_setup_ns + s->t_hold_ns;
    float f2 = 1.0f / (t_period_min * 1e-9f) / 1e6f;  /* MHz */

    float f_max = fminf(f1, f2) * 0.8f;   /* 20% margin */

    printf("\n--- Timing Constraints ---\n");
    printf("  Transition ≤ 20%% period:  f_max = %6.1f MHz\n", f1);
    printf("  Device timing budget:     f_max = %6.1f MHz\n", f2);
    printf("\n  *** Recommended max SPI clock: %.1f MHz ***\n", f_max);
    printf("  (includes 20%% safety margin)\n");

    /* Slew rate */
    float vdd = 3.3f;
    float dv  = vdd * 0.8f;  /* 10% to 90% = 80% of VDD */
    float slew_vns = dv / s->t_rise_ns;
    printf("\n--- Slew Rate ---\n");
    printf("  Rising edge slew rate:    %6.3f V/ns\n", slew_vns);
    printf("  Falling edge slew rate:   %6.3f V/ns\n",
           dv / s->t_fall_ns);

    printf("========================================\n");
}

int main(void)
{
    /* Scenario A: Short trace, strong driver (e.g., on-board SPI flash) */
    spi_scenario_t on_board = {
        .name          = "On-Board SPI Flash (10 cm trace)",
        .t_rise_ns     = 4.0f,
        .t_fall_ns     = 3.0f,
        .t_setup_ns    = 5.0f,
        .t_hold_ns     = 2.0f,
        .t_access_ns   = 6.0f,
        .c_load_pf     = 30.0f,
        .r_driver_ohm  = 25.0f,
    };

    /* Scenario B: Long cable, open-drain with pull-up resistor */
    spi_scenario_t long_cable = {
        .name          = "Long Cable (30 cm) with 4.7kΩ Pull-up",
        .t_rise_ns     = 110.0f,   /* RC: 2.2 × 4700 × 100e-12 = 1.034 µs */
        .t_fall_ns     = 15.0f,    /* Active pull-down is faster */
        .t_setup_ns    = 5.0f,
        .t_hold_ns     = 2.0f,
        .t_access_ns   = 6.0f,
        .c_load_pf     = 100.0f,
        .r_driver_ohm  = 4700.0f,
    };

    print_budget(&on_board);
    print_budget(&long_cable);

    return 0;
}
```

**Sample output:**
```
========================================
  SPI Timing Budget: On-Board SPI Flash (10 cm trace)
========================================

--- Signal Characteristics ---
  Measured rise time:          4.0 ns
  Measured fall time:          3.0 ns
  RC-model rise time est.:     1.7 ns  (R=25Ω, C=30pF)
  Critical trace length:      30.0 cm  (v=15 cm/ns)

--- Timing Constraints ---
  Transition ≤ 20% period:  f_max =  50.0 MHz
  Device timing budget:     f_max =  58.8 MHz

  *** Recommended max SPI clock: 40.0 MHz ***
  (includes 20% safety margin)

--- Slew Rate ---
  Rising edge slew rate:     0.660 V/ns
  Falling edge slew rate:    0.880 V/ns
========================================

========================================
  SPI Timing Budget: Long Cable (30 cm) with 4.7kΩ Pull-up
========================================
...
  *** Recommended max SPI clock: 1.5 MHz ***
```

---

## Rust Implementation

The Rust examples target both an `embedded-hal`-compatible bare-metal environment and a standard desktop environment for the timing budget calculations.

### Example 1: Timing Budget Calculator (Desktop / `no_std`-compatible)

```rust
//! spi_timing_budget.rs
//!
//! SPI timing budget calculator.
//! Computes maximum safe clock frequency from measured rise/fall times
//! and device datasheet parameters.
//!
//! Compile: rustc spi_timing_budget.rs -o spi_timing
//! Or with Cargo: add to main.rs and run with `cargo run`

/// All measured and specified timing parameters for one SPI scenario.
#[derive(Debug, Clone)]
pub struct SpiTimingParams {
    /// Descriptive name for this scenario
    pub name: &'static str,
    /// Measured 10%→90% rise time on the relevant signal line (ns)
    pub t_rise_ns: f64,
    /// Measured 90%→10% fall time (ns)
    pub t_fall_ns: f64,
    /// Data setup time before active clock edge, from device datasheet (ns)
    pub t_setup_ns: f64,
    /// Data hold time after active clock edge, from device datasheet (ns)
    pub t_hold_ns: f64,
    /// Clock-to-output delay of the SPI slave (ns) — how long after the
    /// clock edge does the slave begin driving MISO?
    pub t_access_ns: f64,
    /// Estimated load capacitance on the line (pF)
    pub c_load_pf: f64,
    /// Driver output impedance (Ω) — used for RC model cross-check
    pub r_driver_ohm: f64,
    /// Supply voltage (V) — for slew rate calculation
    pub vdd_v: f64,
}

/// Computed results of a timing budget analysis.
#[derive(Debug, Clone)]
pub struct TimingBudgetResult {
    /// Maximum frequency from the 20%-transition-time rule (Hz)
    pub f_max_transition_hz: f64,
    /// Maximum frequency from device timing constraints (Hz)
    pub f_max_device_hz: f64,
    /// Final recommended maximum frequency with safety margin (Hz)
    pub f_max_recommended_hz: f64,
    /// RC-model estimated rise time (ns) — cross-check vs measured
    pub t_rise_rc_model_ns: f64,
    /// Critical PCB trace length above which termination is needed (cm)
    pub critical_trace_length_cm: f64,
    /// Rising edge slew rate (V/ns)
    pub slew_rate_rise_v_per_ns: f64,
    /// Falling edge slew rate (V/ns)
    pub slew_rate_fall_v_per_ns: f64,
}

impl SpiTimingParams {
    /// Perform the full timing budget analysis.
    ///
    /// Returns a [`TimingBudgetResult`] containing all derived quantities
    /// and the recommended maximum SPI clock frequency.
    pub fn analyze(&self) -> TimingBudgetResult {
        // RC model cross-check: t_r ≈ 2.2 × R × C
        // R in Ω, C in pF → result in ps; divide by 1000 for ns
        let t_rise_rc_model_ns =
            2.2 * self.r_driver_ohm * self.c_load_pf * 1e-3;

        // Critical trace length: L = (t_r / 2) × v_prop  (v_prop ≈ 15 cm/ns)
        let critical_trace_length_cm = (self.t_rise_ns / 2.0) * 15.0;

        // ── Constraint 1: transition time must be ≤ 20% of clock period ──
        // T_clk ≥ 5 × t_transition  →  f ≤ 1/(5×t_transition)
        let t_worst_transition = self.t_rise_ns.max(self.t_fall_ns);
        let f_max_transition_hz = 1.0 / (5.0 * t_worst_transition * 1e-9);

        // ── Constraint 2: device timing budget ──
        // Minimum period = t_access + t_rise + t_setup + t_hold
        let t_min_period_ns =
            self.t_access_ns + self.t_rise_ns + self.t_setup_ns + self.t_hold_ns;
        let f_max_device_hz = 1.0 / (t_min_period_ns * 1e-9);

        // Apply 20% safety margin to the more restrictive constraint
        let f_max_recommended_hz =
            f_max_transition_hz.min(f_max_device_hz) * 0.80;

        // Slew rates: ΔV = 80% of VDD (10%→90%), Δt = rise/fall time
        let delta_v = 0.80 * self.vdd_v;
        let slew_rate_rise_v_per_ns = delta_v / self.t_rise_ns;
        let slew_rate_fall_v_per_ns = delta_v / self.t_fall_ns;

        TimingBudgetResult {
            f_max_transition_hz,
            f_max_device_hz,
            f_max_recommended_hz,
            t_rise_rc_model_ns,
            critical_trace_length_cm,
            slew_rate_rise_v_per_ns,
            slew_rate_fall_v_per_ns,
        }
    }

    /// Print a formatted timing budget report.
    pub fn print_report(&self) {
        let r = self.analyze();

        println!("\n{}", "=".repeat(50));
        println!("  SPI Timing Budget: {}", self.name);
        println!("{}", "=".repeat(50));

        println!("\n── Signal Characteristics ──");
        println!("  Measured rise time        : {:>8.1} ns", self.t_rise_ns);
        println!("  Measured fall time        : {:>8.1} ns", self.t_fall_ns);
        println!(
            "  RC-model rise time (est.) : {:>8.1} ns  (R={:.0}Ω, C={:.0}pF)",
            r.t_rise_rc_model_ns, self.r_driver_ohm, self.c_load_pf
        );
        println!(
            "  Critical trace length     : {:>8.1} cm  (v_prop=15 cm/ns)",
            r.critical_trace_length_cm
        );

        println!("\n── Timing Constraints ──");
        println!(
            "  20%%-transition rule  f_max : {:>8.1} MHz",
            r.f_max_transition_hz / 1e6
        );
        println!(
            "  Device timing budget f_max : {:>8.1} MHz",
            r.f_max_device_hz / 1e6
        );
        println!(
            "\n  *** Recommended max SPI clock: {:.1} MHz ***",
            r.f_max_recommended_hz / 1e6
        );
        println!("  (includes 20% safety margin)");

        println!("\n── Slew Rates ──");
        println!(
            "  Rise: {:>6.3} V/ns   Fall: {:>6.3} V/ns",
            r.slew_rate_rise_v_per_ns, r.slew_rate_fall_v_per_ns
        );
        println!("{}", "=".repeat(50));
    }
}

fn main() {
    let on_board = SpiTimingParams {
        name: "On-Board SPI Flash (short trace)",
        t_rise_ns: 4.0,
        t_fall_ns: 3.0,
        t_setup_ns: 5.0,
        t_hold_ns: 2.0,
        t_access_ns: 6.0,
        c_load_pf: 30.0,
        r_driver_ohm: 25.0,
        vdd_v: 3.3,
    };

    let long_cable = SpiTimingParams {
        name: "Long Cable with 4.7 kΩ Pull-up",
        t_rise_ns: 1034.0,   // 2.2 × 4700 × 100pF = 1.034 µs
        t_fall_ns: 15.0,
        t_setup_ns: 5.0,
        t_hold_ns: 2.0,
        t_access_ns: 6.0,
        c_load_pf: 100.0,
        r_driver_ohm: 4700.0,
        vdd_v: 3.3,
    };

    on_board.print_report();
    long_cable.print_report();
}
```

---

### Example 2: Embedded `no_std` Driver with Drive Strength Control (`embedded-hal`)

```rust
//! spi_drive_strength.rs
//!
//! Embedded-hal driver demonstrating GPIO drive strength configuration
//! and SPI clock frequency selection based on rise/fall time constraints.
//!
//! This is a `no_std` compatible module suitable for bare-metal targets.
//!
//! [dependencies]
//! embedded-hal = "1.0"

#![no_std]

use embedded_hal::spi::SpiBus;

/// Drive strength levels for SPI GPIO outputs.
///
/// Higher drive strength → lower output impedance → faster rise/fall time.
/// Select the lowest strength that satisfies your frequency requirement
/// to minimise EMI radiation.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
pub enum DriveStrength {
    /// ~25–50 Ω output impedance (slowest; suits ≤ 5 MHz)
    Low    = 0,
    /// ~15–25 Ω (suits ≤ 20 MHz)
    Medium = 1,
    /// ~8–15 Ω (suits ≤ 50 MHz)
    High   = 2,
    /// ~4–8 Ω (suits ≤ 100 MHz; highest EMI)
    Max    = 3,
}

impl DriveStrength {
    /// Estimate rise time in nanoseconds given load capacitance.
    ///
    /// Uses the lumped RC model: t_r ≈ 2.2 × R_out × C_load.
    ///
    /// # Parameters
    /// - `c_load_pf`: Line capacitance in picofarads
    ///
    /// # Returns
    /// Estimated rise time in nanoseconds.
    pub fn estimated_rise_time_ns(&self, c_load_pf: f32) -> f32 {
        let r_out_ohm: f32 = match self {
            DriveStrength::Low    => 40.0,
            DriveStrength::Medium => 20.0,
            DriveStrength::High   => 12.0,
            DriveStrength::Max    =>  6.0,
        };
        // t_r = 2.2 × R × C; R in Ω, C in pF → result in ps; ÷1000 → ns
        2.2 * r_out_ohm * c_load_pf * 1e-3
    }

    /// Recommend the minimum drive strength that achieves a given
    /// maximum rise time at the specified load capacitance.
    ///
    /// # Parameters
    /// - `target_rise_ns`: Maximum acceptable rise time (ns)
    /// - `c_load_pf`: Estimated line capacitance (pF)
    ///
    /// # Returns
    /// The weakest (lowest EMI) drive setting that meets the target.
    pub fn minimum_for_rise_time(
        target_rise_ns: f32,
        c_load_pf: f32,
    ) -> Option<DriveStrength> {
        for strength in [
            DriveStrength::Low,
            DriveStrength::Medium,
            DriveStrength::High,
            DriveStrength::Max,
        ] {
            if strength.estimated_rise_time_ns(c_load_pf) <= target_rise_ns {
                return Some(strength);
            }
        }
        None   // Even Max drive cannot meet the target — reduce C_load
    }
}

/// SPI frequency selection based on rise/fall time constraints.
///
/// Returns the largest SPI clock period (in nanoseconds) from the
/// available prescaler values that satisfies the rise time rule.
///
/// # Parameters
/// - `base_clock_hz`: Peripheral bus clock (Hz), e.g. 84_000_000
/// - `t_rise_ns`: Measured or estimated rise time (ns)
/// - `t_fall_ns`: Measured or estimated fall time (ns)
/// - `t_setup_ns`: Device data setup time (ns)
/// - `t_hold_ns`: Device data hold time (ns)
/// - `t_access_ns`: Slave clock-to-data delay (ns)
///
/// # Returns
/// Best prescaler value as a power of 2 (2, 4, 8, … 256), and the
/// resulting SPI clock frequency in Hz.
pub fn select_spi_prescaler(
    base_clock_hz: u32,
    t_rise_ns: f32,
    t_fall_ns: f32,
    t_setup_ns: f32,
    t_hold_ns: f32,
    t_access_ns: f32,
) -> (u32, u32) {
    // Worst-case transition time
    let t_trans = t_rise_ns.max(t_fall_ns);

    // Constraint 1: 20% rule
    let f_max_1_hz = 1.0 / (5.0 * t_trans * 1e-9);

    // Constraint 2: device timing
    let t_min_ns = t_access_ns + t_rise_ns + t_setup_ns + t_hold_ns;
    let f_max_2_hz = 1.0 / (t_min_ns * 1e-9);

    // Apply 20% margin
    let f_max_hz = (f_max_1_hz.min(f_max_2_hz) * 0.80) as u32;

    // Find the largest prescaler (= smallest frequency) that does NOT
    // exceed f_max_hz.  Prescalers available: 2, 4, 8, 16, 32, 64, 128, 256.
    let mut best_prescaler = 256u32;
    let mut best_freq_hz   = base_clock_hz / 256;

    for exp in 1u32..=8 {
        let prescaler = 1u32 << exp;   // 2, 4, 8, …, 256
        let freq_hz   = base_clock_hz / prescaler;
        if freq_hz <= f_max_hz && freq_hz > best_freq_hz {
            best_prescaler = prescaler;
            best_freq_hz   = freq_hz;
        }
    }

    (best_prescaler, best_freq_hz)
}

/// Loopback bit-error-rate test using embedded-hal SpiBus trait.
///
/// Transmits a pseudo-random pattern and compares received bytes.
/// Since MOSI is physically wired to MISO in the loopback configuration,
/// any mismatch indicates a signal integrity problem at the current
/// clock frequency.
///
/// # Type Parameters
/// - `SPI`: Any type implementing `embedded_hal::spi::SpiBus<u8>`
///
/// # Parameters
/// - `spi`: Mutable reference to an initialised SPI bus
/// - `iterations`: Number of bytes to transfer per test run
///
/// # Returns
/// Number of bit errors detected (0 = pass)
pub fn loopback_ber_test<SPI>(spi: &mut SPI, iterations: usize) -> u32
where
    SPI: SpiBus<u8>,
{
    // LFSR-based test pattern: exercises all 256 byte values
    let mut lfsr: u8 = 0xACu8;
    let mut error_bits: u32 = 0;

    let mut tx_buf = [0u8; 1];
    let mut rx_buf = [0u8; 1];

    for _ in 0..iterations {
        // Advance LFSR (polynomial x^8 + x^6 + x^5 + x^4 + 1)
        let feedback = ((lfsr >> 7) ^ (lfsr >> 5) ^ (lfsr >> 4) ^ (lfsr >> 3)) & 1;
        lfsr = (lfsr << 1) | feedback;

        tx_buf[0] = lfsr;
        let _ = spi.transfer(&mut rx_buf, &tx_buf);

        // XOR to find differing bits; popcount to count them
        let diff = rx_buf[0] ^ tx_buf[0];
        error_bits += diff.count_ones();
    }

    error_bits
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_drive_strength_ordering() {
        // Higher drive strength should yield shorter rise times
        let c = 50.0; // 50 pF
        assert!(
            DriveStrength::Low.estimated_rise_time_ns(c)
                > DriveStrength::Max.estimated_rise_time_ns(c),
            "Low drive must be slower than Max drive"
        );
    }

    #[test]
    fn test_minimum_drive_strength_selection() {
        // For 50 pF load, target 10 ns rise: should recommend High or Max
        let result = DriveStrength::minimum_for_rise_time(10.0, 50.0);
        assert!(result.is_some());
        assert!(result.unwrap() >= DriveStrength::High);
    }

    #[test]
    fn test_prescaler_selection_respects_constraints() {
        // 84 MHz bus, 10 ns rise time — should not suggest > 20 MHz
        let (_, freq_hz) =
            select_spi_prescaler(84_000_000, 10.0, 8.0, 5.0, 2.0, 7.0);
        assert!(
            freq_hz <= 20_000_000,
            "Selected frequency {} Hz exceeds constraint",
            freq_hz
        );
    }

    #[test]
    fn test_prescaler_worst_case_slow_rise() {
        // 1 µs rise time (open-drain with 4.7 kΩ) — must select very low freq
        let (_, freq_hz) =
            select_spi_prescaler(84_000_000, 1000.0, 20.0, 5.0, 2.0, 7.0);
        // f_max = 1/(5 × 1000ns) = 200 kHz, so 84 MHz / 256 = 328 kHz is still
        // too fast — prescaler should saturate at 256
        assert!(freq_hz <= 400_000,
            "Slow-rise scenario should yield a very low clock frequency");
    }
}
```

---

### Example 3: Rise/Fall Time Statistics with Iterator Chains (Desktop Rust)

```rust
//! rise_fall_stats.rs
//!
//! Demonstrates idiomatic Rust: computing statistical summaries of
//! rise/fall time measurements using iterator combinators.
//!
//! In a real system, `raw_timestamps` would come from a timer peripheral.

/// A pair of timestamps (in timer ticks) marking the 10% and 90%
/// threshold crossings on a single edge.
#[derive(Debug, Clone, Copy)]
pub struct EdgeCapture {
    /// Timer count at 10% threshold crossing
    pub tick_10pct: u64,
    /// Timer count at 90% threshold crossing
    pub tick_90pct: u64,
}

/// Statistical summary of a set of rise (or fall) time measurements.
#[derive(Debug)]
pub struct EdgeStats {
    pub mean_ns: f64,
    pub min_ns: f64,
    pub max_ns: f64,
    pub std_dev_ns: f64,
    pub sample_count: usize,
}

/// Convert a slice of [`EdgeCapture`] records to nanoseconds.
///
/// # Parameters
/// - `captures`: Slice of raw capture records
/// - `timer_freq_hz`: Timer clock frequency in Hz
///
/// # Returns
/// `Vec<f64>` of transition times in nanoseconds, one per capture.
pub fn captures_to_ns(captures: &[EdgeCapture], timer_freq_hz: u64) -> Vec<f64> {
    let tick_period_ns = 1e9 / timer_freq_hz as f64;
    captures
        .iter()
        .map(|c| {
            let delta = c.tick_90pct.wrapping_sub(c.tick_10pct);
            delta as f64 * tick_period_ns
        })
        // Filter out obviously bad measurements (e.g. > 10 µs for a signal
        // that should have a rise time in the tens of nanoseconds range)
        .filter(|&t| t > 0.0 && t < 10_000.0)
        .collect()
}

/// Compute statistical properties of a set of timing measurements.
pub fn compute_stats(times_ns: &[f64]) -> Option<EdgeStats> {
    let n = times_ns.len();
    if n == 0 {
        return None;
    }

    let mean = times_ns.iter().copied().sum::<f64>() / n as f64;
    let min  = times_ns.iter().cloned().fold(f64::INFINITY, f64::min);
    let max  = times_ns.iter().cloned().fold(f64::NEG_INFINITY, f64::max);

    let variance = times_ns
        .iter()
        .map(|&t| (t - mean).powi(2))
        .sum::<f64>()
        / n as f64;

    Some(EdgeStats {
        mean_ns: mean,
        min_ns: min,
        max_ns: max,
        std_dev_ns: variance.sqrt(),
        sample_count: n,
    })
}

fn main() {
    // Simulated captures from a 168 MHz timer peripheral
    // In production, these would be filled by timer ISRs.
    let timer_freq_hz: u64 = 168_000_000;
    let tick_period_ns = 1e9 / timer_freq_hz as f64; // ≈ 5.95 ns/tick

    // Simulate ~8 ns mean rise time with jitter
    let simulated_captures: Vec<EdgeCapture> = (0..200)
        .map(|i| {
            // Simple deterministic jitter: ±1 tick around 1345 ticks (≈ 8 ns)
            let jitter: i64 = (i % 3) as i64 - 1;
            EdgeCapture {
                tick_10pct: 1_000_000 + i * 500,
                tick_90pct: 1_000_000 + i * 500 + 1345 + jitter as u64,
            }
        })
        .collect();

    let times_ns = captures_to_ns(&simulated_captures, timer_freq_hz);

    if let Some(stats) = compute_stats(&times_ns) {
        println!("Rise Time Statistics ({} samples):", stats.sample_count);
        println!("  Mean    : {:.2} ns", stats.mean_ns);
        println!("  Min     : {:.2} ns", stats.min_ns);
        println!("  Max     : {:.2} ns", stats.max_ns);
        println!("  Std Dev : {:.2} ns", stats.std_dev_ns);

        // Worst-case (mean + 3σ) for timing budget
        let worst_case = stats.mean_ns + 3.0 * stats.std_dev_ns;
        println!("  Worst-case (mean+3σ): {:.2} ns", worst_case);

        let f_max_mhz = 1.0 / (5.0 * worst_case * 1e-9) / 1e6;
        println!("\n  Max SPI clock (20%% rule, worst-case): {:.1} MHz", f_max_mhz * 0.8);
    }

    println!("\nTimer resolution: {:.2} ns/tick at {} MHz",
             tick_period_ns,
             timer_freq_hz / 1_000_000);
}
```

---

## Practical Examples and Diagnostics

### Typical Rise Time Values

| Scenario | t_rise | Notes |
|---|---|---|
| MCU GPIO, short trace, 12 mA drive | 2–5 ns | On-board SPI flash; ≤ 100 MHz feasible |
| MCU GPIO, 30 cm PCB trace, 4 mA drive | 10–20 ns | ≤ 20 MHz recommended |
| Open-drain, 4.7 kΩ pull-up, 100 pF line | ~1 µs | ≤ 200 kHz recommended |
| Level-shifter (TXS0104) | 5–15 ns | Check enable capacitance |
| Long coaxial cable, 50 Ω terminated | 3–8 ns | Good for high-speed; termination critical |

### Diagnosis Flowchart

```
Observe SPI communication errors or CRC failures?
            │
            ▼
   Reduce clock frequency by 50%
            │
         Errors stop? ──YES──► Rise/Fall time is the bottleneck
            │                   → measure t_r, apply timing budget
            NO
            │
            ▼
   Check power supply noise and decoupling capacitors
            │
         Errors stop? ──YES──► Ground/power integrity issue (separate topic)
            │
            NO
            │
            ▼
   Check for CS glitches or multi-slave bus contention
```

### Rule of Thumb Reference

| Rule | Formula | Purpose |
|---|---|---|
| 20% transition budget | f ≤ 1/(5×t_r) | Maximum clock without SI analysis |
| RC rise time | t_r ≈ 2.2 × R × C | Estimate before measuring |
| Critical trace length | L = (t_r/2) × 15 cm/ns | When to add termination |
| Slew rate from rise time | SR = 0.8×VDD / t_r (V/ns) | EMC pre-compliance estimate |
| Safety margin | f_actual = 0.8 × f_max | Standard engineering practice |

---

## Summary

**Rise time (t_r)** and **fall time (t_f)** describe how quickly SPI bus signals transition between logic levels, measured between the 10% and 90% voltage points. These times are not cosmetic — they are fundamental constraints on how fast SPI can reliably operate.

**Key physical relationships:**
- Rise time is set by the RC product of driver output impedance and line capacitance: t_r ≈ 2.2 × R_out × C_L.
- Open-drain pull-up resistors dominate rise time on open-drain buses; fall time is typically faster because active pull-down is used.
- PCB traces longer than L_critical = (t_r/2) × 15 cm/ns behave as transmission lines and require termination to suppress reflections.

**The key design rule** is that rise/fall times must consume no more than 20% of the SPI clock period:

```
f_max ≤ 1 / (5 × max(t_r, t_f))
```

In practice, the tightest constraint wins among: the 20% transition rule, device datasheet setup/hold/access timings, and transmission-line settling time. A 20% safety margin should be applied to the computed maximum.

**Software's role** in rise/fall time management includes:
1. Configuring GPIO drive strength (slew rate control) to trade off rise time against EMI.
2. Using hardware timer input-capture peripherals to measure edge timestamps and compute t_r statistically from ISR data.
3. Running loopback BER tests to empirically determine the maximum reliable clock frequency, using binary search to find the operating boundary.
4. Computing and validating the full timing budget before committing to a SPI clock prescaler.

The C/C++ and Rust examples in this document cover all four of these roles: GPIO slew configuration, timer-capture edge measurement, BER-based max-frequency search, and deterministic timing budget computation — providing a complete software toolkit for characterising and managing SPI signal transition times.

---

*Document: 77_Rise_And_Fall_Time_Measurement.md — SPI Signal Integrity Series*