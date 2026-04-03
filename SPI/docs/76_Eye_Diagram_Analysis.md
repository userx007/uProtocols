# 76. Eye Diagram Analysis

**Theory & Fundamentals**
- How eye diagrams are constructed by overlaying UI-aligned waveform segments
- All key parameters: eye height, eye width, jitter (TJ/RJ/DJ), Q-factor, crossing percentage, and BER estimation
- Signal integrity challenges specific to high-speed SPI: reflections, ISI, skin effect, crosstalk

**C/C++ Implementation**
- `EyeDiagram` accumulator struct with a 2D voltage×time hit map
- Sub-sample precision rising edge detection with linear interpolation
- Full metrics extraction (eye height, width, jitter pk-pk/RMS, BER)
- ASCII art renderer for embedded targets with no display
- Rectangular mask testing with violation counting
- Complete `main.c` with a simulated 50 MHz SPI MOSI waveform including realistic ISI and jitter

**Rust Implementation**
- Idiomatic struct/`impl` architecture across multiple modules
- Iterator-chain based edge detection and accumulation
- `EyeMask` struct with named standard presets (e.g. `spi_50mhz_standard()`)
- `erfc` approximation for BER estimation without external dependencies

**Advanced Topics**
- Bathtub curve (BER vs. sample point sweep)
- RJ/DJ separation using kurtosis and the Dual-Dirac model
- Gardner timing error detector for clockless/DPLL clock recovery

**Summary table** with practical design guidelines for maintaining signal integrity on SPI traces.

## Measuring Signal Integrity Using Eye Diagrams for High-Speed SPI

---

## Table of Contents

1. [Introduction](#introduction)
2. [Fundamentals of Eye Diagrams](#fundamentals-of-eye-diagrams)
3. [Key Eye Diagram Parameters](#key-eye-diagram-parameters)
4. [Signal Integrity Challenges in High-Speed SPI](#signal-integrity-challenges-in-high-speed-spi)
5. [Eye Diagram Measurement Setup](#eye-diagram-measurement-setup)
6. [Implementation in C/C++](#implementation-in-cc)
7. [Implementation in Rust](#implementation-in-rust)
8. [Automated Pass/Fail Mask Testing](#automated-passfail-mask-testing)
9. [Advanced Topics](#advanced-topics)
10. [Summary](#summary)

---

## Introduction

As SPI (Serial Peripheral Interface) clock speeds push into the tens and hundreds of megahertz range — found in modern Flash memories, ADCs, DACs, IMUs, and display controllers — the digital "perfect square wave" assumption breaks down. Parasitic inductances, trace impedance mismatches, dielectric losses, and crosstalk degrade signal edges to the point where receivers struggle to distinguish logic-high from logic-low.

**Eye diagram analysis** is the industry-standard technique for visualising and quantifying this degradation. By overlaying thousands of consecutive bit-period waveforms — synchronized to the bit clock — a characteristic eye-shaped opening (or closure) emerges. The shape of that eye tells engineers everything about timing margins, noise tolerance, jitter, and the likelihood of a bit error.

This document covers the theory, measurement methodology, and complete software implementations for capturing, constructing, and analysing eye diagrams in SPI contexts, both in **C/C++** and **Rust**.

---

## Fundamentals of Eye Diagrams

### How an Eye Diagram Is Constructed

An eye diagram is a form of **persistence display** or **overlay plot**:

1. A continuous waveform is sampled at a rate much higher than the SPI clock.
2. The waveform is divided into fixed-length segments, each exactly one or two bit-periods (Unit Intervals, UI) long.
3. All segments are time-aligned to a common trigger point (usually a rising edge of SCLK).
4. All segments are superimposed (overlaid) on a single time axis.
5. The resulting pattern shows every possible 0→0, 0→1, 1→0, 1→1 transition simultaneously.

```
Voltage
  ^
  |  ___   ___       ___
  | |   | |   |_____| | |
  | |   |_|           | |
  |_________________________> Time (one UI →)
       |← UI →|

After overlay of many segments:

  ^
1 |  ████████████████████
  | ██  EYE OPENING  ██
0 |  ████████████████████
  +------------------------> Time
     0%     50%    100% UI
```

The central open region — the **eye opening** — represents the safe sampling zone where a receiver can correctly identify the bit value.

### Unit Interval (UI)

The Unit Interval is the nominal duration of one bit:

```
UI = 1 / f_SCLK
```

For a 50 MHz SPI clock: UI = 20 ns.  
For a 100 MHz SPI clock: UI = 10 ns.

All timing measurements in an eye diagram are normalised to UI (0% to 100%, or –0.5 UI to +0.5 UI centred on the optimal sample point).

---

## Key Eye Diagram Parameters

### Eye Height

The **vertical opening** of the eye at the optimal sampling instant:

```
Eye Height = V_eye_top - V_eye_bottom
```

Represents noise margin. A larger eye height means the receiver can tolerate more noise before making a bit error.

### Eye Width

The **horizontal opening** of the eye at the decision threshold voltage:

```
Eye Width = t_eye_close_right - t_eye_close_left   (in UI or time)
```

Represents timing margin. A wider eye means jitter and clock uncertainty have less impact.

### Jitter

Deviation of signal transitions from their ideal positions:

| Jitter Type        | Description                                             |
|--------------------|----------------------------------------------------------|
| **Total Jitter (TJ)**  | Peak-to-peak variation of all crossing times         |
| **Random Jitter (RJ)** | Unbounded, Gaussian-distributed; from thermal noise  |
| **Deterministic Jitter (DJ)** | Bounded, repeatable; from ISI, crosstalk, EMI |
| **Data-Dependent Jitter (DDJ)** | Subset of DJ caused by Inter-Symbol Interference |

### Eye Crossing Percentage

The voltage level at which the eye "crosses" (transitions intersect), typically near 50% of the logic swing. Deviation from 50% indicates duty-cycle distortion.

### Q-Factor / BER Estimation

The Q-factor links eye height to estimated Bit Error Rate (BER):

```
Q = Eye_Height / (2 * σ_noise)
BER ≈ 0.5 * erfc(Q / √2)
```

### Rise/Fall Time

Measured from 20% to 80% of signal amplitude on the waveform edges visible in the eye. Directly related to bandwidth limitations in the signal path.

---

## Signal Integrity Challenges in High-Speed SPI

### Trace Impedance and Reflections

PCB traces are transmission lines. Impedance mismatches at connectors, vias, and IC pads cause reflections:

```
Γ = (Z_L - Z_0) / (Z_L + Z_0)

Where:
  Z_0 = characteristic impedance of trace (typically 50 Ω)
  Z_L = load impedance at termination point
  Γ   = reflection coefficient (-1 to +1)
```

Reflections appear in the eye diagram as secondary crossings and additional jitter.

### Skin Effect and Dielectric Loss

At high frequencies, current flows only on the surface of conductors (skin effect), increasing resistance. Dielectric loss in FR4 increases with frequency. Both effects cause frequency-dependent attenuation, rounding signal edges and closing the eye vertically.

### Crosstalk

Adjacent SPI signals (MOSI, MISO, CS, SCLK routed in parallel) capacitively and inductively couple. Near-End Crosstalk (NEXT) and Far-End Crosstalk (FEXT) appear as noise bursts correlated with switching activity of neighbouring lines.

### Inter-Symbol Interference (ISI)

The low-pass filtering effect of the PCB channel causes each symbol to partially overlap into adjacent symbols. A long sequence of 1s followed by a 0 looks different from an isolated 0 transition — this is ISI, and it directly causes eye closure.

---

## Eye Diagram Measurement Setup

### Hardware Requirements

```
SPI Device Under Test (DUT)
        |
        | SPI signals (SCLK, MOSI, MISO, CS)
        |
  ┌─────▼──────────────────────────────────┐
  │  High-bandwidth oscilloscope (≥ 4x     │
  │  signal bandwidth, e.g. ≥ 400 MHz for  │
  │  100 MHz SPI) with:                    │
  │  • High sample rate (≥ 1 GS/s)         │
  │  • Deep memory (≥ 10M samples)         │
  │  • Mask testing capability             │
  └─────┬──────────────────────────────────┘
        |
  ┌─────▼──────────────────────────────────┐
  │  PC / Embedded Host                    │
  │  Eye diagram construction software     │
  └────────────────────────────────────────┘
```

### Sampling Considerations

- **Oversampling ratio**: Sample the signal at ≥ 8× the clock frequency for adequate edge resolution.
- **Acquisition depth**: Capture ≥ 10,000 UI for statistically meaningful jitter estimation.
- **Trigger**: Trigger on the recovered clock or SCLK rising edge for clean alignment.
- **Probing**: Use low-capacitance probes (≤ 1 pF) or differential probes to avoid loading the DUT.

### Software-Based Eye Diagram Generation

When a real oscilloscope is not available, eye diagrams can be generated from:
- Logic analyser captures (digital, lower quality)
- Simulation data (SPICE, HyperLynx, etc.)
- Captured ADC data streams from custom hardware

---

## Implementation in C/C++

### Data Structures

```c
// eye_diagram.h
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define EYE_WIDTH_BINS   200   // Time bins across one UI
#define EYE_HEIGHT_BINS  256   // Voltage bins (maps to 0–3.3V or normalised)
#define MAX_SAMPLES      1000000

typedef struct {
    float voltage;
    double timestamp_ns;
} Sample;

typedef struct {
    uint32_t hits[EYE_HEIGHT_BINS][EYE_WIDTH_BINS]; // [voltage_bin][time_bin]
    double   ui_ns;          // Unit Interval in nanoseconds
    float    v_min;          // Minimum voltage (maps to bin 0)
    float    v_max;          // Maximum voltage (maps to bin EYE_HEIGHT_BINS-1)
    uint32_t total_uis;      // Number of UIs overlaid
} EyeDiagram;

typedef struct {
    float eye_height;        // Volts
    float eye_width_ui;      // Fraction of UI (0.0 – 1.0)
    float crossing_pct;      // Percent (ideally 50%)
    float rise_time_ns;      // 20% to 80%
    float fall_time_ns;      // 80% to 20%
    float jitter_pp_ui;      // Peak-to-peak jitter in UI
    float jitter_rms_ui;     // RMS jitter in UI
    double ber_estimate;     // Estimated Bit Error Rate
} EyeMetrics;
```

### Eye Diagram Construction

```c
// eye_diagram.c
#include "eye_diagram.h"
#include <string.h>
#include <stdio.h>

/**
 * Initialise an eye diagram accumulator.
 * @param eye      Pointer to EyeDiagram structure to initialise
 * @param ui_ns    Unit Interval duration in nanoseconds (1 / f_sclk * 1e9)
 * @param v_min    Minimum expected voltage (e.g. -0.5V)
 * @param v_max    Maximum expected voltage (e.g. 3.8V)
 */
void eye_init(EyeDiagram *eye, double ui_ns, float v_min, float v_max) {
    memset(eye, 0, sizeof(EyeDiagram));
    eye->ui_ns    = ui_ns;
    eye->v_min    = v_min;
    eye->v_max    = v_max;
    eye->total_uis = 0;
}

/**
 * Accumulate waveform data into the eye diagram.
 * Samples must be uniformly spaced in time.
 *
 * @param eye          Eye diagram accumulator
 * @param samples      Array of voltage samples
 * @param n_samples    Number of samples
 * @param sample_period_ns  Time between consecutive samples (ns)
 * @param trigger_offsets_ns  Array of clock edge timestamps (trigger points)
 * @param n_triggers   Number of trigger points
 */
void eye_accumulate(EyeDiagram     *eye,
                    const float    *samples,
                    size_t          n_samples,
                    double          sample_period_ns,
                    const double   *trigger_offsets_ns,
                    size_t          n_triggers) {

    double v_range = eye->v_max - eye->v_min;

    for (size_t t = 0; t < n_triggers - 1; t++) {
        double t_start = trigger_offsets_ns[t];
        double t_end   = trigger_offsets_ns[t] + eye->ui_ns;

        // Iterate over samples within this UI window
        for (size_t s = 0; s < n_samples; s++) {
            double t_sample = s * sample_period_ns;
            if (t_sample < t_start || t_sample >= t_end)
                continue;

            // Normalise time within UI to [0, EYE_WIDTH_BINS)
            double t_norm = (t_sample - t_start) / eye->ui_ns;
            int time_bin = (int)(t_norm * EYE_WIDTH_BINS);
            if (time_bin < 0 || time_bin >= EYE_WIDTH_BINS) continue;

            // Normalise voltage to [0, EYE_HEIGHT_BINS)
            float v = samples[s];
            int volt_bin = (int)((v - eye->v_min) / v_range * EYE_HEIGHT_BINS);
            if (volt_bin < 0)                   volt_bin = 0;
            if (volt_bin >= EYE_HEIGHT_BINS)    volt_bin = EYE_HEIGHT_BINS - 1;

            eye->hits[volt_bin][time_bin]++;
        }

        eye->total_uis++;
    }
}

/**
 * Find trigger (clock edge) positions from a SCLK waveform.
 * Returns the number of rising edges found.
 */
size_t find_rising_edges(const float  *sclk_samples,
                         size_t        n_samples,
                         double        sample_period_ns,
                         float         threshold,
                         double       *out_times_ns,
                         size_t        max_edges) {
    size_t count = 0;
    for (size_t i = 1; i < n_samples && count < max_edges; i++) {
        if (sclk_samples[i - 1] < threshold && sclk_samples[i] >= threshold) {
            // Linear interpolation for sub-sample precision
            double frac = (threshold - sclk_samples[i-1])
                        / (sclk_samples[i] - sclk_samples[i-1]);
            out_times_ns[count++] = (i - 1 + frac) * sample_period_ns;
        }
    }
    return count;
}
```

### Metrics Extraction

```c
/**
 * Extract eye diagram quality metrics from the accumulated eye.
 *
 * Strategy:
 *  - Eye height: at 50% UI time bin, find lowest "top" and highest "bottom"
 *    occupied voltage, measured at BER contour threshold.
 *  - Eye width: at mid-voltage, find leftmost and rightmost time where
 *    signal is absent (the crossing region).
 *  - Jitter: from the distribution of transition times.
 */
EyeMetrics eye_analyse(const EyeDiagram *eye) {
    EyeMetrics m = {0};

    uint32_t threshold_hits = eye->total_uis / 1000; // 0.1% density threshold

    // --- Eye Height at optimal sample point (50% UI) ---
    int center_time_bin = EYE_WIDTH_BINS / 2;
    int top_bin    = -1;
    int bottom_bin = -1;

    // Find the "open" vertical region at the centre of the eye
    // Scan from top down to find the lower boundary of the '1' region
    for (int v = EYE_HEIGHT_BINS - 1; v >= 0; v--) {
        if (eye->hits[v][center_time_bin] > threshold_hits) {
            top_bin = v;
            break;
        }
    }
    // Scan from bottom up to find the upper boundary of the '0' region
    for (int v = 0; v < EYE_HEIGHT_BINS; v++) {
        if (eye->hits[v][center_time_bin] > threshold_hits) {
            bottom_bin = v;
            break;
        }
    }

    if (top_bin > 0 && bottom_bin >= 0 && top_bin > bottom_bin) {
        float v_range = eye->v_max - eye->v_min;
        float top_v    = eye->v_min + ((float)top_bin    / EYE_HEIGHT_BINS) * v_range;
        float bottom_v = eye->v_min + ((float)bottom_bin / EYE_HEIGHT_BINS) * v_range;
        // Eye height = gap between top of '0' band and bottom of '1' band
        // Find the actual gap (where hit count is zero between the two bands)
        int gap_top = -1, gap_bot = -1;
        for (int v = bottom_bin; v <= top_bin; v++) {
            if (eye->hits[v][center_time_bin] == 0) {
                if (gap_bot < 0) gap_bot = v;
                gap_top = v;
            }
        }
        if (gap_top >= 0 && gap_bot >= 0) {
            float gap_top_v = eye->v_min + ((float)gap_top / EYE_HEIGHT_BINS) * v_range;
            float gap_bot_v = eye->v_min + ((float)gap_bot / EYE_HEIGHT_BINS) * v_range;
            m.eye_height = gap_top_v - gap_bot_v;
        }
    }

    // --- Eye Width at decision threshold voltage (mid-rail) ---
    int mid_volt_bin = EYE_HEIGHT_BINS / 2;
    int left_open = -1, right_open = -1;

    for (int tb = 0; tb < EYE_WIDTH_BINS; tb++) {
        if (eye->hits[mid_volt_bin][tb] == 0) {
            if (left_open < 0) left_open = tb;
            right_open = tb;
        }
    }
    if (left_open >= 0 && right_open > left_open) {
        m.eye_width_ui = (float)(right_open - left_open) / EYE_WIDTH_BINS;
    }

    // --- Jitter: analyse transition density in crossing region ---
    // Count transition hits in a ±10% UI window around the theoretical crossing
    double crossing_times[10000];
    size_t n_crossings = 0;
    int cross_volt_bin = mid_volt_bin;

    for (int tb = 0; tb < EYE_WIDTH_BINS; tb++) {
        uint32_t h = eye->hits[cross_volt_bin][tb];
        if (h > 0) {
            // Weight contribution proportional to hit count
            for (uint32_t k = 0; k < h && n_crossings < 10000; k++) {
                crossing_times[n_crossings++] = (double)tb / EYE_WIDTH_BINS;
            }
        }
    }

    if (n_crossings > 1) {
        double mean = 0.0;
        double t_min = crossing_times[0], t_max = crossing_times[0];
        for (size_t i = 0; i < n_crossings; i++) {
            mean  += crossing_times[i];
            if (crossing_times[i] < t_min) t_min = crossing_times[i];
            if (crossing_times[i] > t_max) t_max = crossing_times[i];
        }
        mean /= n_crossings;

        double variance = 0.0;
        for (size_t i = 0; i < n_crossings; i++) {
            double d = crossing_times[i] - mean;
            variance += d * d;
        }
        variance /= n_crossings;

        m.jitter_pp_ui  = (float)(t_max - t_min);
        m.jitter_rms_ui = (float)sqrt(variance);

        // Crossing percentage: where in UI do most crossings fall?
        m.crossing_pct = (float)(mean * 100.0);
    }

    // --- BER Estimate using Q-factor ---
    // Q = eye_height / (2 * sigma_noise)
    // Approximate sigma_noise from the vertical scatter at centre
    float sigma_estimate = 0.02f; // fallback: 20 mV typical
    if (m.eye_height > 0.0f) {
        // Use the width of the '1' and '0' bands as proxy for noise sigma
        // (more rigorous: fit Gaussian to voltage histogram slices)
        float v_range = eye->v_max - eye->v_min;
        sigma_estimate = (v_range / EYE_HEIGHT_BINS) * 3.0f; // ~3-bin sigma
        float Q = m.eye_height / (2.0f * sigma_estimate);
        // BER ≈ 0.5 * erfc(Q / sqrt(2))
        m.ber_estimate = 0.5 * erfc((double)Q / sqrt(2.0));
    }

    return m;
}

void eye_print_metrics(const EyeMetrics *m) {
    printf("=== Eye Diagram Metrics ===\n");
    printf("  Eye Height      : %.3f V\n",   m->eye_height);
    printf("  Eye Width       : %.1f%% UI\n", m->eye_width_ui * 100.0f);
    printf("  Crossing Point  : %.1f%%\n",   m->crossing_pct);
    printf("  Jitter (pk-pk)  : %.3f UI  (%.3f UI RMS)\n",
           m->jitter_pp_ui, m->jitter_rms_ui);
    printf("  Estimated BER   : %.2e\n",     m->ber_estimate);
}
```

### ASCII Eye Diagram Renderer

```c
/**
 * Render the eye diagram as ASCII art to stdout.
 * Useful for embedded targets without display capability.
 */
void eye_render_ascii(const EyeDiagram *eye,
                      int display_width,
                      int display_height) {
    // Find max hit count for normalisation
    uint32_t max_hits = 0;
    for (int v = 0; v < EYE_HEIGHT_BINS; v++)
        for (int t = 0; t < EYE_WIDTH_BINS; t++)
            if (eye->hits[v][t] > max_hits) max_hits = eye->hits[v][t];

    if (max_hits == 0) { printf("[empty eye]\n"); return; }

    const char density_chars[] = " .:-=+*#@";
    int n_chars = (int)(sizeof(density_chars) - 1);

    printf("\n+");
    for (int t = 0; t < display_width; t++) printf("-");
    printf("+\n");

    for (int dv = display_height - 1; dv >= 0; dv--) {
        int v_start = (dv * EYE_HEIGHT_BINS) / display_height;
        int v_end   = ((dv + 1) * EYE_HEIGHT_BINS) / display_height;
        printf("|");
        for (int dt = 0; dt < display_width; dt++) {
            int t_start = (dt * EYE_WIDTH_BINS) / display_width;
            int t_end   = ((dt + 1) * EYE_WIDTH_BINS) / display_width;

            uint64_t sum = 0;
            int count = 0;
            for (int v = v_start; v < v_end; v++)
                for (int t = t_start; t < t_end; t++) {
                    sum += eye->hits[v][t];
                    count++;
                }

            float density = (count > 0) ? (float)sum / count / max_hits : 0.0f;
            int char_idx = (int)(density * (n_chars - 1));
            printf("%c", density_chars[char_idx]);
        }
        printf("|\n");
    }

    printf("+");
    for (int t = 0; t < display_width; t++) printf("-");
    printf("+\n");
    printf(" 0%%");
    for (int t = 3; t < display_width - 4; t++) printf(" ");
    printf("50%%");
    for (int t = 0; t < display_width/2 - 6; t++) printf(" ");
    printf("100%% UI\n\n");
}
```

### Complete Usage Example

```c
// main.c — Full eye diagram analysis pipeline for SPI MOSI
#include "eye_diagram.h"
#include <stdio.h>
#include <stdlib.h>

// Simulate a 50 MHz SPI MOSI waveform with ISI and jitter
// In production, replace with real ADC captures from oscilloscope
static void generate_test_waveform(float  *mosi_out,
                                   float  *sclk_out,
                                   size_t  n,
                                   double  sample_period_ns) {
    double ui_ns    = 20.0;   // 50 MHz clock
    double v_high   = 3.3f;
    double v_low    = 0.0f;
    double rise_ns  = 2.0;    // 2 ns rise time (realistic for 50 MHz SPI on FR4)

    // Pseudo-random bit stream (PRBS-7 style)
    uint8_t shift = 0x55;
    double jitter_amp_ns = 0.3; // 300 ps pp jitter

    for (size_t i = 0; i < n; i++) {
        double t = i * sample_period_ns;
        uint64_t ui_idx = (uint64_t)(t / ui_ns);

        // Advance PRBS
        if (ui_idx > 0 && ((uint64_t)((t - sample_period_ns) / ui_ns)) < ui_idx) {
            shift = ((shift << 1) | (((shift >> 6) ^ (shift >> 5)) & 1)) & 0x7F;
        }
        int bit = (shift >> 6) & 1;

        // Add deterministic jitter proportional to previous bit (DDJ/ISI)
        int prev_bit = (shift >> 5) & 1;
        double jitter = (bit != prev_bit) ? (jitter_amp_ns * (((double)rand()/RAND_MAX) - 0.5)) : 0.0;

        double t_in_ui = fmod(t + jitter, ui_ns);
        float v_target = bit ? v_high : v_low;

        // Simple RC-filter edge model
        if (t_in_ui < rise_ns) {
            int prev = ((shift >> 5) & 1);
            float v_from = prev ? v_high : v_low;
            mosi_out[i] = v_from + (v_target - v_from) * (t_in_ui / rise_ns);
        } else {
            mosi_out[i] = v_target;
        }
        // Add noise
        mosi_out[i] += 0.015f * ((float)rand()/RAND_MAX - 0.5f);

        // SCLK (clean)
        sclk_out[i] = (fmod(t, ui_ns) < (ui_ns / 2)) ? 3.3f : 0.0f;
    }
}

int main(void) {
    const size_t N_SAMPLES       = 500000;
    const double SAMPLE_PERIOD   = 0.5;   // 2 GS/s  → 0.5 ns per sample
    const double UI_NS           = 20.0;  // 50 MHz SPI

    float *mosi = malloc(N_SAMPLES * sizeof(float));
    float *sclk = malloc(N_SAMPLES * sizeof(float));
    double *edges = malloc(N_SAMPLES * sizeof(double));

    if (!mosi || !sclk || !edges) { fprintf(stderr, "OOM\n"); return 1; }

    srand(42);
    generate_test_waveform(mosi, sclk, N_SAMPLES, SAMPLE_PERIOD);

    // 1. Find SCLK rising edges (trigger points)
    size_t n_edges = find_rising_edges(sclk, N_SAMPLES, SAMPLE_PERIOD,
                                       1.65f,   // 50% of 3.3V threshold
                                       edges, N_SAMPLES);
    printf("Found %zu rising edges (%.0f UI)\n", n_edges, (double)n_edges);

    // 2. Build eye diagram
    EyeDiagram eye;
    eye_init(&eye, UI_NS, -0.3f, 3.6f);
    eye_accumulate(&eye, mosi, N_SAMPLES, SAMPLE_PERIOD, edges, n_edges);

    // 3. Display and analyse
    printf("\nSPI MOSI Eye Diagram (50 MHz, 2 GS/s capture):\n");
    eye_render_ascii(&eye, 80, 24);

    EyeMetrics m = eye_analyse(&eye);
    eye_print_metrics(&m);

    // 4. Pass/fail check
    const float MIN_EYE_HEIGHT = 0.4f;   // 400 mV minimum
    const float MIN_EYE_WIDTH  = 0.35f;  // 35% UI minimum
    const float MAX_JITTER_PP  = 0.25f;  // 25% UI maximum

    printf("\nPass/Fail:\n");
    printf("  Eye Height  : %s (%.3fV, min %.3fV)\n",
           m.eye_height  >= MIN_EYE_HEIGHT ? "PASS" : "FAIL",
           m.eye_height, MIN_EYE_HEIGHT);
    printf("  Eye Width   : %s (%.1f%% UI, min %.0f%% UI)\n",
           m.eye_width_ui >= MIN_EYE_WIDTH  ? "PASS" : "FAIL",
           m.eye_width_ui * 100, MIN_EYE_WIDTH * 100);
    printf("  Jitter pp   : %s (%.3f UI, max %.3f UI)\n",
           m.jitter_pp_ui <= MAX_JITTER_PP  ? "PASS" : "FAIL",
           m.jitter_pp_ui, MAX_JITTER_PP);

    free(mosi); free(sclk); free(edges);
    return 0;
}
```

---

## Implementation in Rust

### Project Structure

```toml
# Cargo.toml
[package]
name    = "spi_eye_diagram"
version = "0.1.0"
edition = "2021"

[dependencies]
# No external dependencies required for core functionality
# Optional: use 'plotters' crate for PNG/SVG output
```

### Data Structures and Types

```rust
// src/eye_diagram.rs

/// Number of time bins across one Unit Interval
pub const EYE_WIDTH_BINS:  usize = 200;
/// Number of voltage bins
pub const EYE_HEIGHT_BINS: usize = 256;

/// A single digitised sample from the oscilloscope or ADC
#[derive(Debug, Clone, Copy)]
pub struct Sample {
    pub voltage:      f32,
    pub timestamp_ns: f64,
}

/// Accumulated eye diagram hit-map
pub struct EyeDiagram {
    /// hits[voltage_bin][time_bin]
    pub hits:       Box<[[u32; EYE_WIDTH_BINS]; EYE_HEIGHT_BINS]>,
    pub ui_ns:      f64,
    pub v_min:      f32,
    pub v_max:      f32,
    pub total_uis:  usize,
}

/// Extracted quality metrics
#[derive(Debug, Default)]
pub struct EyeMetrics {
    pub eye_height_v:    f32,
    pub eye_width_ui:    f32,   // 0.0 – 1.0 (fraction of UI)
    pub crossing_pct:    f32,   // percentage (ideal: 50%)
    pub jitter_pp_ui:    f32,   // peak-to-peak jitter in UI
    pub jitter_rms_ui:   f32,   // RMS jitter in UI
    pub ber_estimate:    f64,
}

impl EyeDiagram {
    /// Create a new eye diagram accumulator.
    pub fn new(ui_ns: f64, v_min: f32, v_max: f32) -> Self {
        Self {
            hits:      Box::new([[0u32; EYE_WIDTH_BINS]; EYE_HEIGHT_BINS]),
            ui_ns,
            v_min,
            v_max,
            total_uis: 0,
        }
    }
}
```

### Clock Edge Detection

```rust
// src/edge_detect.rs

/// Find rising-edge positions with sub-sample interpolation.
///
/// Returns a Vec of edge timestamps in nanoseconds.
pub fn find_rising_edges(
    samples:           &[f32],
    sample_period_ns:  f64,
    threshold:         f32,
) -> Vec<f64> {
    samples
        .windows(2)
        .enumerate()
        .filter_map(|(i, w)| {
            if w[0] < threshold && w[1] >= threshold {
                // Linear interpolation for sub-sample precision
                let frac = (threshold - w[0]) as f64 / (w[1] - w[0]) as f64;
                Some((i as f64 + frac) * sample_period_ns)
            } else {
                None
            }
        })
        .collect()
}

/// Estimate Unit Interval from measured edge timestamps.
/// Uses median inter-edge spacing for robustness against missed edges.
pub fn estimate_ui_ns(edges: &[f64]) -> Option<f64> {
    if edges.len() < 2 { return None; }

    let mut spacings: Vec<f64> = edges.windows(2)
        .map(|w| w[1] - w[0])
        .collect();

    spacings.sort_by(|a, b| a.partial_cmp(b).unwrap());

    // Median
    let mid = spacings.len() / 2;
    Some(if spacings.len() % 2 == 0 {
        (spacings[mid - 1] + spacings[mid]) / 2.0
    } else {
        spacings[mid]
    })
}
```

### Eye Accumulation

```rust
// src/accumulate.rs
use crate::eye_diagram::{EyeDiagram, EYE_HEIGHT_BINS, EYE_WIDTH_BINS};

impl EyeDiagram {
    /// Overlay waveform segments onto the eye diagram accumulator.
    ///
    /// # Arguments
    /// * `samples`           - Voltage samples (uniformly timed)
    /// * `sample_period_ns`  - Time between samples in nanoseconds
    /// * `trigger_times_ns`  - Clock edge timestamps (rising edges of SCLK)
    pub fn accumulate(
        &mut self,
        samples:          &[f32],
        sample_period_ns: f64,
        trigger_times_ns: &[f64],
    ) {
        let v_range = (self.v_max - self.v_min) as f64;
        let n_samples = samples.len();

        for &t_start in trigger_times_ns {
            let t_end = t_start + self.ui_ns;

            // Index range covering this UI window
            let i_start = ((t_start / sample_period_ns) as usize).min(n_samples);
            let i_end   = ((t_end   / sample_period_ns) as usize + 1).min(n_samples);

            for i in i_start..i_end {
                let t_sample = i as f64 * sample_period_ns;
                if t_sample < t_start || t_sample >= t_end { continue; }

                // Normalise time → [0, EYE_WIDTH_BINS)
                let t_norm   = (t_sample - t_start) / self.ui_ns;
                let time_bin = (t_norm * EYE_WIDTH_BINS as f64) as usize;
                let time_bin = time_bin.min(EYE_WIDTH_BINS - 1);

                // Normalise voltage → [0, EYE_HEIGHT_BINS)
                let v = samples[i];
                let v_norm   = ((v - self.v_min) as f64 / v_range)
                               .clamp(0.0, 1.0 - f64::EPSILON);
                let volt_bin = (v_norm * EYE_HEIGHT_BINS as f64) as usize;

                self.hits[volt_bin][time_bin] =
                    self.hits[volt_bin][time_bin].saturating_add(1);
            }

            self.total_uis += 1;
        }
    }
}
```

### Metrics Analysis

```rust
// src/analyse.rs
use crate::eye_diagram::{EyeDiagram, EyeMetrics, EYE_HEIGHT_BINS, EYE_WIDTH_BINS};

impl EyeDiagram {
    /// Extract eye quality metrics from the accumulated hit map.
    pub fn analyse(&self) -> EyeMetrics {
        let mut m = EyeMetrics::default();
        if self.total_uis == 0 { return m; }

        let threshold = (self.total_uis as u32) / 1000; // 0.1% density
        let v_range   = self.v_max - self.v_min;
        let center_tb = EYE_WIDTH_BINS / 2;

        // ---- Eye Height ----
        // Find the vertical gap at 50% UI where no signal transitions land
        let mut gap_bottom = None::<usize>;
        let mut gap_top    = None::<usize>;
        let mut in_gap     = false;

        for vb in 0..EYE_HEIGHT_BINS {
            let h = self.hits[vb][center_tb];
            if h <= threshold && !in_gap {
                in_gap = true;
                gap_bottom = Some(vb);
            } else if h > threshold && in_gap {
                gap_top = Some(vb.saturating_sub(1));
                in_gap  = false;
            }
        }
        if in_gap { gap_top = Some(EYE_HEIGHT_BINS - 1); }

        if let (Some(gb), Some(gt)) = (gap_bottom, gap_top) {
            if gt > gb {
                let top_v = self.v_min + (gt as f32 / EYE_HEIGHT_BINS as f32) * v_range;
                let bot_v = self.v_min + (gb as f32 / EYE_HEIGHT_BINS as f32) * v_range;
                m.eye_height_v = top_v - bot_v;
            }
        }

        // ---- Eye Width ----
        // At the mid-voltage bin, find the horizontal span without signal hits
        let mid_vb = EYE_HEIGHT_BINS / 2;
        let open_bins: Vec<usize> = (0..EYE_WIDTH_BINS)
            .filter(|&tb| self.hits[mid_vb][tb] == 0)
            .collect();

        if open_bins.len() >= 2 {
            let left  = *open_bins.first().unwrap();
            let right = *open_bins.last().unwrap();
            m.eye_width_ui = (right - left) as f32 / EYE_WIDTH_BINS as f32;
        }

        // ---- Jitter (from transition density at crossing voltage) ----
        let cross_vb = mid_vb;
        let crossing_weights: Vec<(f64, u32)> = (0..EYE_WIDTH_BINS)
            .filter_map(|tb| {
                let h = self.hits[cross_vb][tb];
                if h > 0 {
                    Some((tb as f64 / EYE_WIDTH_BINS as f64, h))
                } else {
                    None
                }
            })
            .collect();

        if crossing_weights.len() > 1 {
            let total_w: u64 = crossing_weights.iter().map(|&(_, w)| w as u64).sum();
            let mean: f64 = crossing_weights.iter()
                .map(|&(t, w)| t * w as f64)
                .sum::<f64>() / total_w as f64;

            let variance: f64 = crossing_weights.iter()
                .map(|&(t, w)| (t - mean).powi(2) * w as f64)
                .sum::<f64>() / total_w as f64;

            let t_min = crossing_weights.first().map(|&(t, _)| t).unwrap_or(0.0);
            let t_max = crossing_weights.last().map(|(t, _)| *t).unwrap_or(0.0);

            m.jitter_pp_ui  = (t_max - t_min) as f32;
            m.jitter_rms_ui = variance.sqrt() as f32;
            m.crossing_pct  = (mean * 100.0) as f32;
        }

        // ---- BER Estimate ----
        if m.eye_height_v > 0.0 {
            let sigma = v_range * 0.01; // 1% of swing as noise floor estimate
            let q = m.eye_height_v / (2.0 * sigma);
            m.ber_estimate = 0.5 * erfc_approx(q as f64 / 2.0_f64.sqrt());
        }

        m
    }
}

/// Complementary error function approximation (Abramowitz & Stegun 7.1.26)
fn erfc_approx(x: f64) -> f64 {
    if x < 0.0 { return 2.0 - erfc_approx(-x); }
    let t = 1.0 / (1.0 + 0.3275911 * x);
    let poly = t * (0.254829592
               + t * (-0.284496736
               + t * (1.421413741
               + t * (-1.453152027
               + t * 1.061405429))));
    poly * (-x * x).exp()
}
```

### ASCII Renderer and Pass/Fail

```rust
// src/render.rs
use crate::eye_diagram::{EyeDiagram, EYE_HEIGHT_BINS, EYE_WIDTH_BINS};

impl EyeDiagram {
    /// Render the eye diagram as ASCII art.
    pub fn render_ascii(&self, width: usize, height: usize) {
        const DENSITY: &[u8] = b" .:-=+*#@";
        let n_chars = DENSITY.len();

        let max_hits = self.hits.iter()
            .flat_map(|row| row.iter())
            .copied()
            .max()
            .unwrap_or(1)
            .max(1) as f32;

        println!("\n+{}+", "-".repeat(width));
        for dv in (0..height).rev() {
            let v_start = (dv * EYE_HEIGHT_BINS) / height;
            let v_end   = ((dv + 1) * EYE_HEIGHT_BINS) / height;
            print!("|");
            for dt in 0..width {
                let t_start = (dt * EYE_WIDTH_BINS) / width;
                let t_end   = ((dt + 1) * EYE_WIDTH_BINS) / width;

                let sum: u64 = (v_start..v_end.min(EYE_HEIGHT_BINS))
                    .flat_map(|v| (t_start..t_end.min(EYE_WIDTH_BINS))
                        .map(move |t| self.hits[v][t] as u64))
                    .sum();
                let count = ((v_end - v_start) * (t_end - t_start)).max(1) as f32;
                let density = (sum as f32 / count) / max_hits;
                let idx = ((density * (n_chars - 1) as f32) as usize).min(n_chars - 1);
                print!("{}", DENSITY[idx] as char);
            }
            println!("|");
        }
        println!("+{}+", "-".repeat(width));
        println!(" 0%{:>width$}100% UI\n", "50%", width = width / 2);
    }
}

/// Pass/fail mask check against engineering thresholds.
pub struct EyeMask {
    pub min_eye_height_v:  f32,
    pub min_eye_width_ui:  f32,
    pub max_jitter_pp_ui:  f32,
    pub max_ber:           f64,
}

impl EyeMask {
    /// Standard mask for 50 MHz SPI over FR4 ≤ 10 cm trace.
    pub fn spi_50mhz_standard() -> Self {
        Self {
            min_eye_height_v: 0.4,
            min_eye_width_ui: 0.35,
            max_jitter_pp_ui: 0.25,
            max_ber:          1e-9,
        }
    }

    /// Check metrics and return (pass, failure_reasons)
    pub fn check(&self, m: &crate::eye_diagram::EyeMetrics)
        -> (bool, Vec<String>)
    {
        let mut failures = Vec::new();

        if m.eye_height_v < self.min_eye_height_v {
            failures.push(format!(
                "Eye height {:.3}V < minimum {:.3}V",
                m.eye_height_v, self.min_eye_height_v
            ));
        }
        if m.eye_width_ui < self.min_eye_width_ui {
            failures.push(format!(
                "Eye width {:.1}% UI < minimum {:.0}% UI",
                m.eye_width_ui * 100.0, self.min_eye_width_ui * 100.0
            ));
        }
        if m.jitter_pp_ui > self.max_jitter_pp_ui {
            failures.push(format!(
                "Jitter {:.3} UI > maximum {:.3} UI",
                m.jitter_pp_ui, self.max_jitter_pp_ui
            ));
        }
        if m.ber_estimate > self.max_ber {
            failures.push(format!(
                "BER estimate {:.2e} > maximum {:.2e}",
                m.ber_estimate, self.max_ber
            ));
        }

        (failures.is_empty(), failures)
    }
}
```

### Main Entry Point

```rust
// src/main.rs
mod eye_diagram;
mod edge_detect;
mod accumulate;
mod analyse;
mod render;

use eye_diagram::EyeDiagram;
use edge_detect::{find_rising_edges, estimate_ui_ns};
use render::EyeMask;

fn generate_test_waveform(
    n_samples: usize,
    sample_period_ns: f64,
) -> (Vec<f32>, Vec<f32>) {
    let ui_ns   = 20.0_f64;   // 50 MHz
    let v_high  = 3.3_f32;
    let rise_ns = 2.0_f64;

    let mut mosi = vec![0.0f32; n_samples];
    let mut sclk = vec![0.0f32; n_samples];
    let mut lfsr: u8 = 0x55;

    let mut prev_bit = 0u8;
    let mut current_bit = 0u8;
    let mut prev_ui_idx = u64::MAX;

    for i in 0..n_samples {
        let t = i as f64 * sample_period_ns;
        let ui_idx = (t / ui_ns) as u64;

        if ui_idx != prev_ui_idx {
            prev_bit = current_bit;
            lfsr = ((lfsr << 1) | (((lfsr >> 6) ^ (lfsr >> 5)) & 1)) & 0x7F;
            current_bit = (lfsr >> 6) & 1;
            prev_ui_idx = ui_idx;
        }

        let t_in_ui = t % ui_ns;
        let jitter  = if current_bit != prev_bit { 0.3 * (i as f64 * 0.127).sin() } else { 0.0 };
        let t_eff   = t_in_ui + jitter;

        mosi[i] = if current_bit == 1 {
            if t_eff < rise_ns {
                let v_from = if prev_bit == 1 { v_high } else { 0.0 };
                v_from + (v_high - v_from) * (t_eff / rise_ns) as f32
            } else {
                v_high
            }
        } else {
            if t_eff < rise_ns {
                let v_from = if prev_bit == 1 { v_high } else { 0.0 };
                v_from * (1.0 - t_eff / rise_ns) as f32
            } else {
                0.0
            }
        };
        // Add noise
        mosi[i] += 0.015 * ((i as f32 * 0.397).sin());

        // Clean SCLK
        sclk[i] = if (t % ui_ns) < (ui_ns / 2.0) { v_high } else { 0.0 };
    }

    (mosi, sclk)
}

fn main() {
    const N_SAMPLES:       usize = 500_000;
    const SAMPLE_PERIOD:   f64   = 0.5;    // 2 GS/s
    const THRESHOLD_V:     f32   = 1.65;   // 50% of 3.3V

    println!("SPI Eye Diagram Analysis — 50 MHz MOSI");
    println!("Capture: {} samples @ {:.0} GS/s", N_SAMPLES, 1.0 / SAMPLE_PERIOD);

    // 1. Generate (or load) waveform
    let (mosi, sclk) = generate_test_waveform(N_SAMPLES, SAMPLE_PERIOD);

    // 2. Detect SCLK rising edges
    let edges = find_rising_edges(&sclk, SAMPLE_PERIOD, THRESHOLD_V);
    let ui_ns = estimate_ui_ns(&edges).unwrap_or(20.0);
    println!("Detected {} rising edges, UI = {:.2} ns ({:.1} MHz)",
             edges.len(), ui_ns, 1000.0 / ui_ns);

    // 3. Build eye diagram
    let mut eye = EyeDiagram::new(ui_ns, -0.3, 3.6);
    eye.accumulate(&mosi, SAMPLE_PERIOD, &edges);
    println!("Accumulated {} UIs into eye diagram", eye.total_uis);

    // 4. Render ASCII eye
    eye.render_ascii(80, 24);

    // 5. Extract metrics
    let metrics = eye.analyse();
    println!("=== Eye Diagram Metrics ===");
    println!("  Eye Height   : {:.3} V",       metrics.eye_height_v);
    println!("  Eye Width    : {:.1}% UI",      metrics.eye_width_ui * 100.0);
    println!("  Crossing     : {:.1}%",         metrics.crossing_pct);
    println!("  Jitter pp    : {:.3} UI  ({:.3} UI RMS)",
             metrics.jitter_pp_ui, metrics.jitter_rms_ui);
    println!("  Estimated BER: {:.2e}",         metrics.ber_estimate);

    // 6. Pass/fail mask
    let mask = EyeMask::spi_50mhz_standard();
    let (pass, failures) = mask.check(&metrics);

    println!("\n=== Mask Test: {} ===",
             if pass { "PASS ✓" } else { "FAIL ✗" });
    for f in &failures {
        println!("  ✗ {}", f);
    }
}
```

---

## Automated Pass/Fail Mask Testing

Eye mask testing defines a **forbidden zone** — a rectangular or hexagonal polygon in the eye diagram space — that the waveform must not enter. If any sample hits the mask, the signal fails.

```
Voltage
  ^
3.3V ──────────────────────────────────
      ████  UPPER MASK REGION  ████
 2.2V ──────┬────────────────┬──────────
            |  EYE OPENING   |
 1.1V ──────┴────────────────┴──────────
      ████  LOWER MASK REGION  ████
  0V ──────────────────────────────────>
      0%  20%     50%     80% 100% UI

Upper mask: V > 2.2V AND 20% UI < t < 80% UI → FAIL
Lower mask: V < 1.1V AND 20% UI < t < 80% UI → FAIL
```

### Hexagonal Mask Definition (C)

```c
typedef struct {
    float v_upper_inner;   // Upper inner boundary at centre of eye
    float v_lower_inner;   // Lower inner boundary at centre of eye
    float t_left_ui;       // Left time boundary (fraction of UI)
    float t_right_ui;      // Right time boundary
} RectMask;

/**
 * Test eye diagram against a rectangular mask.
 * Returns number of mask violations (0 = pass).
 */
uint32_t eye_mask_test(const EyeDiagram *eye, const RectMask *mask) {
    uint32_t violations = 0;
    float v_range = eye->v_max - eye->v_min;

    int t_left  = (int)(mask->t_left_ui  * EYE_WIDTH_BINS);
    int t_right = (int)(mask->t_right_ui * EYE_WIDTH_BINS);

    int v_upper = (int)((mask->v_upper_inner - eye->v_min) / v_range * EYE_HEIGHT_BINS);
    int v_lower = (int)((mask->v_lower_inner - eye->v_min) / v_range * EYE_HEIGHT_BINS);

    for (int tb = t_left; tb <= t_right && tb < EYE_WIDTH_BINS; tb++) {
        for (int vb = v_lower; vb <= v_upper && vb < EYE_HEIGHT_BINS; vb++) {
            if (eye->hits[vb][tb] > 0) violations++;
        }
    }
    return violations;
}
```

---

## Advanced Topics

### Bathtub Curve — BER vs. Sampling Time

A **bathtub plot** sweeps the sampling point across the eye width and measures the estimated BER at each point. The flat bottom of the bathtub is the safe operating zone.

```c
// Generate bathtub curve data
void eye_bathtub_curve(const EyeDiagram *eye,
                       float *ber_out,     // output: BER at each time bin
                       int    n_points) {
    float v_range = eye->v_max - eye->v_min;
    int mid_vb    = EYE_HEIGHT_BINS / 2;

    for (int tp = 0; tp < n_points; tp++) {
        int tb = (tp * EYE_WIDTH_BINS) / n_points;
        uint32_t hits_at_threshold = 0;
        uint32_t total_in_col = 0;

        // Count hits near the decision threshold
        for (int vb = mid_vb - 5; vb <= mid_vb + 5; vb++) {
            if (vb >= 0 && vb < EYE_HEIGHT_BINS) {
                hits_at_threshold += eye->hits[vb][tb];
            }
        }
        for (int vb = 0; vb < EYE_HEIGHT_BINS; vb++)
            total_in_col += eye->hits[vb][tb];

        ber_out[tp] = (total_in_col > 0)
                    ? (float)hits_at_threshold / total_in_col
                    : 0.0f;
    }
}
```

### Jitter Separation: RJ vs. DJ

Random Jitter (RJ) follows a Gaussian distribution and is theoretically unbounded. Deterministic Jitter (DJ) is bounded and repeatable. They combine as:

```
TJ(BER) = DJ + 2 * Q(BER) * σ_RJ
```

Where Q(BER) is the Q-factor for a target BER. Separating them requires:
- **Dual-Dirac model**: Fit two Gaussians to the crossing time histogram
- **Spectral analysis**: DJ components appear as discrete spectral lines; RJ is broadband noise

```rust
// Rust: Fit dual-Dirac model to crossing histogram
fn separate_rj_dj(crossing_times: &[f64]) -> (f64, f64) {
    if crossing_times.len() < 100 {
        return (0.0, 0.0);
    }

    let n = crossing_times.len() as f64;
    let mean = crossing_times.iter().sum::<f64>() / n;
    let variance = crossing_times.iter()
        .map(|&t| (t - mean).powi(2))
        .sum::<f64>() / n;

    // Kurtosis-based RJ/DJ decomposition (simplified)
    let std_dev = variance.sqrt();
    let kurtosis = crossing_times.iter()
        .map(|&t| ((t - mean) / std_dev).powi(4))
        .sum::<f64>() / n;

    // Excess kurtosis > 0 indicates bounded (DJ) component
    let dj_fraction = ((kurtosis - 3.0) / kurtosis).max(0.0).min(1.0);
    let rj_sigma = std_dev * (1.0 - dj_fraction).sqrt();
    let dj_pp    = std_dev * dj_fraction.sqrt() * 6.0; // 6-sigma approximation

    (rj_sigma, dj_pp)
}
```

### Clock Recovery for Software-Based Analysis

When no external clock reference is available, use a **Digital Phase-Locked Loop (DPLL)** or a **Mueller-Müller clock recovery algorithm** to recover the bit clock from the data stream itself:

```c
// Simplified Gardner timing error detector for DPLL-based clock recovery
// Used when no SCLK is available (e.g. analysing isolated MOSI capture)
float gardner_ted(float y_prev, float y_curr, float y_mid) {
    // Gardner TED: e(k) = (y[k] - y[k-1]) * y[k - 0.5]
    return (y_curr - y_prev) * y_mid;
}
```

---

## Summary

Eye diagram analysis is an essential technique for validating signal integrity in high-speed SPI designs. The following table summarises the key concepts:

| Aspect | Key Point |
|---|---|
| **What it shows** | Overlaid waveform segments reveal noise margin, timing margin, and jitter simultaneously |
| **Eye Height** | Vertical opening = noise immunity; target >400 mV for 3.3V SPI |
| **Eye Width** | Horizontal opening = timing margin; target >35% UI at 50 MHz |
| **Jitter** | Deviation of transitions from ideal; Random (RJ) + Deterministic (DJ) = Total (TJ) |
| **BER Estimation** | Q-factor links eye height to bit error rate; Q > 7 → BER < 10⁻¹² |
| **ISI** | Inter-Symbol Interference from PCB channel losses closes the eye vertically and horizontally |
| **Mask Testing** | Define a forbidden zone polygon; any waveform sample inside = fail |
| **SPI-specific concerns** | SCLK, MOSI, MISO routing, stub lengths, series termination, and trace impedance all affect the eye |
| **Practical thresholds** | 50 MHz SPI on FR4 ≤10 cm: Eye height >400 mV, Width >35% UI, Jitter pk-pk <25% UI |
| **C/C++ approach** | Direct memory-mapped hit arrays with sub-sample interpolation; efficient for embedded capture |
| **Rust approach** | Ownership model prevents data races in multi-threaded accumulation; iterator chains for clarity |

### Design Guidelines for SPI Signal Integrity

To maintain an open eye at high SPI clock rates:

- **Terminate traces**: Use series resistors (22–33 Ω) at the driver output or parallel termination at the receiver to suppress reflections.
- **Control impedance**: Route SPI lines on 50 Ω controlled-impedance traces; avoid stubs and vias in the signal path.
- **Minimise trace length**: Every 10 cm of FR4 trace adds ~1 ns of delay and significant attenuation; keep SPI traces short.
- **Avoid parallel routing**: Separate SCLK from MOSI/MISO by ≥ 3× trace width to reduce crosstalk.
- **Match lengths**: For multi-slave SPI buses, match SCLK and data line lengths to prevent skew-induced jitter.
- **Decouple power planes**: Add 100 nF + 10 µF decoupling per IC power pin; switching transients couple into the signal via PCB parasitics.

Eye diagram analysis, whether performed with a real oscilloscope or through software reconstruction of captured ADC data, provides the quantitative confidence that a high-speed SPI link will operate reliably at production volumes and across temperature and supply voltage variations.

---

*Document: 76_Eye_Diagram_Analysis.md | SPI Signal Integrity Series*