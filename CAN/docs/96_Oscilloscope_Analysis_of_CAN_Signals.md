# 96. Oscilloscope Analysis of CAN Signals

## Interpreting Eye Diagrams, Rise Times, and Voltage Levels to Diagnose Physical Layer Problems

---

## Table of Contents

1. [Introduction](#introduction)
2. [CAN Physical Layer Fundamentals](#can-physical-layer-fundamentals)
3. [Oscilloscope Setup for CAN Measurements](#oscilloscope-setup-for-can-measurements)
4. [Voltage Level Analysis](#voltage-level-analysis)
5. [Rise and Fall Time Analysis](#rise-and-fall-time-analysis)
6. [Eye Diagram Analysis](#eye-diagram-analysis)
7. [Common Physical Layer Problems and Diagnosis](#common-physical-layer-problems-and-diagnosis)
8. [C/C++ Code Examples](#cc-code-examples)
9. [Rust Code Examples](#rust-code-examples)
10. [Summary](#summary)

---

## Introduction

The Controller Area Network (CAN) bus is widely deployed in automotive, industrial, and embedded systems. While software-level debugging tools like CAN analyzers are indispensable, many elusive problems are rooted in the **physical layer** — the actual electrical signals on the wire. An oscilloscope is the primary tool for diagnosing these issues.

This document covers the use of an oscilloscope to interpret:

- **Voltage levels** (dominant/recessive states, differential voltage)
- **Rise and fall times** (signal integrity, bit rate limits)
- **Eye diagrams** (aggregate signal quality, timing margins, noise margins)

Understanding these measurements allows engineers to detect problems such as missing or incorrect termination, bus loading, ground shifts, electromagnetic interference (EMI), and more.

---

## CAN Physical Layer Fundamentals

### Differential Signaling

CAN uses a two-wire differential bus: **CANH** and **CANL**.

| State      | CANH Voltage | CANL Voltage | Differential (CANH - CANL) |
|------------|-------------|-------------|------------------------------|
| Recessive  | ~2.5 V      | ~2.5 V      | ~0 V                         |
| Dominant   | ~3.5 V      | ~1.5 V      | ~2.0 V                       |

The ISO 11898-2 standard defines:

- **Dominant threshold**: Differential voltage > 0.9 V
- **Recessive threshold**: Differential voltage < 0.5 V
- **Undefined region**: 0.5 V to 0.9 V (should be avoided)

### Bus Termination

CAN requires 120 Ohm termination resistors at each end of the bus. The resulting parallel combination is 60 Ohm. Incorrect termination is one of the most frequent physical layer issues.

### Bit Timing

CAN bit timing is divided into segments:

```
|<--- Nominal Bit Time (NBT) --->|
| SYNC_SEG | PROP_SEG | PHASE_SEG1 | PHASE_SEG2 |
     1 Tq     1-8 Tq     1-8 Tq       1-8 Tq
```

At 500 kbit/s, NBT = 2 us. At 1 Mbit/s, NBT = 1 us.

---

## Oscilloscope Setup for CAN Measurements

### Recommended Settings

| Parameter            | Recommendation                          |
|----------------------|-----------------------------------------|
| Bandwidth            | >= 20 MHz (>= 100 MHz for CAN FD)       |
| Sample Rate          | >= 5x the bit rate                      |
| Time/Division        | 1-2 us/div (Classic CAN 500 kbit/s)     |
| Voltage/Division     | 1 V/div per channel                     |
| Trigger Mode         | Single or normal, trigger on CANH edge  |
| Probe                | Passive 10:1 probe (< 10 pF input cap)  |

### Probe Connection Points

```
          +---------------------------+
          |         CAN Node          |
          |  +----+     +----------+  |
          |  |MCU |-----|CAN Trans |  |
          |  +----+     +----------+  |
          +----------+------+---------+
                     |CANH  |CANL
                     |      |
  Probe 1 (+) -------+      |
  Probe 2 (+) --------------+
  Both probes GND --- Bus GND
```

**For differential measurement:** Use the math channel (CH1 - CH2) to display the true differential signal.

---

## Voltage Level Analysis

### What to Look For

A healthy CAN bus at 500 kbit/s should display:

- **CANH dominant**: 3.3 V to 4.0 V
- **CANL dominant**: 1.0 V to 1.8 V
- **Both recessive**: 2.3 V to 2.7 V (common mode ~2.5 V)
- **Differential dominant**: >= 1.5 V (typically ~2.0 V)
- **Differential recessive**: <= 0.1 V (typically ~0 V)

### Voltage Level Diagnostic Table

| Observation                      | Likely Cause                                   |
|----------------------------------|------------------------------------------------|
| Dominant voltage too low         | Missing or short-circuit termination           |
| Dominant voltage too high        | Bus driver overdrive, supply voltage issue     |
| Recessive not returning to 2.5 V | Missing termination (only one 120 Ohm present) |
| CANH/CANL asymmetric amplitude   | Unequal impedance, damaged transceiver         |
| Common mode offset               | Ground potential difference between nodes      |
| Negative differential voltage   | CANH/CANL wires swapped                         |

---

## Rise and Fall Time Analysis

### Definition

Rise time is measured from **10%** to **90%** of the final amplitude. Fall time is measured from **90%** to **10%**.

For a CAN signal at 1 Mbit/s (NBT = 1 us), the rise/fall time should be roughly **10-20% of the bit time**, approximately **100-200 ns**.

### ISO 11898-2 Limits

| Bit Rate   | Max Rise/Fall Time |
|------------|--------------------|
| 125 kbit/s | 600 ns             |
| 250 kbit/s | 300 ns             |
| 500 kbit/s | 150 ns             |
| 1 Mbit/s   | 75 ns              |

### Causes of Slow Rise Times

- Excessive bus capacitance (too many nodes, long cables)
- Incorrect termination (too high impedance)
- Damaged or weak CAN transceiver
- Over-filtering on the bus lines

### Causes of Fast Rise Times (Overshoot/Ringing)

- Insufficient termination (too low impedance or missing)
- Inductance in wiring harness
- Long stub connections
- Split termination capacitor missing

---

## Eye Diagram Analysis

### What Is an Eye Diagram?

An eye diagram is created by overlaying many successive bit periods on the oscilloscope screen, synchronized to the bit clock. The resulting "eye opening" reveals:

- **Eye height**: Voltage margin (noise immunity)
- **Eye width**: Timing margin (jitter tolerance)
- **Crossing points**: Where transitions occur (should be at 50% amplitude)
- **Eye closure**: Degraded signal quality

```
Voltage
  |
  |  +----------------------------+
  |  |                            |   <- Maximum amplitude
  |  |   +====================+   |
  |  |   ||                  ||   |   <- Eye height
  |  |---||------------------||---|   <- Decision threshold
  |  |   ||                  ||   |
  |  |   +====================+   |
  |  |                            |   <- Minimum amplitude
  |  +----------------------------+
  +-------------------------------------> Time
         ^                  ^
     Eye open           Eye close
     (transition)       (transition)
         |<--- Eye width --->|
```

### Eye Diagram Parameters

| Parameter          | Description                                | Healthy Value (500 kbit/s) |
|--------------------|--------------------------------------------|----------------------------|
| Eye Height         | Vertical opening at decision point         | > 1.0 V                    |
| Eye Width          | Horizontal opening at decision threshold   | > 60% of bit period        |
| Jitter (RMS)       | Variation in crossing times                | < 5% of bit period         |
| Jitter (Peak-Peak) | Worst-case crossing variation              | < 15% of bit period        |
| Rise Time          | 10%-90% transition                         | < 150 ns                   |
| Crossing %         | Amplitude at crossing point (% of height)  | 40%-60%                    |

### Interpreting Eye Closure

```
Good Eye:                 Closed Eye (Jitter):      Closed Eye (Noise):

  ----------------        ------------------        ------------------
       +===+                    ++++                  +====+  +====+
  -----+   +-----          -----++++-----         ----+    +--+    +--
       +===+                    ++++                  +====+  +====+
  ----------------        ------------------        ------------------

  Wide, clear eye         Narrow eye (jitter)       Blurry eye (noise)
```

---

## Common Physical Layer Problems and Diagnosis

### 1. Missing Termination

**Oscilloscope Observation:**
- Recessive voltage does not settle at 2.5 V; it may float or slowly charge
- Rise/fall times are slow
- Ringing on transitions

**Diagnosis:** Measure resistance between CANH and CANL with bus powered off. Should be ~60 Ohm. If > 120 Ohm, one terminator is missing.

---

### 2. Double Termination on One End

**Oscilloscope Observation:**
- Dominant voltage amplitude lower than expected
- Recessive recovers quickly (fast fall time)
- Eye height is reduced

**Diagnosis:** Bus shows ~40 Ohm instead of 60 Ohm. Check PCB layout and connector pinout.

---

### 3. Ground Offset / Common Mode Interference

**Oscilloscope Observation:**
- Both CANH and CANL shift together (common mode shift)
- Differential signal looks correct but absolute voltages are displaced
- Intermittent errors at different nodes

**Diagnosis:** Measure voltage between each node's GND and a reference. Common-mode voltage should stay within -2 V to +7 V of the CAN transceiver supply.

---

### 4. Stub Length Too Long

**Oscilloscope Observation:**
- Ringing on dominant-to-recessive transitions
- Secondary reflections visible 10-50 ns after main edge
- Eye diagram shows multiple crossing points

**Diagnosis:** Stub length should obey: `L_stub < (rise_time x propagation_speed) / 2`. For 150 ns rise time and 0.6c propagation: `L_stub < 13.5 m`.

---

### 5. CAN FD Phase Errors

In CAN FD, the data phase can run at 2-8 Mbit/s. The oscilloscope must capture both phases:

- **Arbitration phase**: Standard CAN timing (e.g., 500 kbit/s)
- **Data phase**: High-speed section (e.g., 2 Mbit/s)

Look for the **BRS (Bit Rate Switch)** bit, followed by narrower bits in the data phase.

---

## C/C++ Code Examples

### 1. Logging CAN Bus Error Counters (Linux SocketCAN)

This example reads the transmit error counter (TEC) and receive error counter (REC) from a SocketCAN interface, which can correlate with physical layer problems seen on the oscilloscope.

```c
// can_error_monitor.c
// Compile: gcc -o can_error_monitor can_error_monitor.c
// Run:     ./can_error_monitor vcan0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

#define CAN_ERR_MASK  (CAN_ERR_TX_TIMEOUT | CAN_ERR_LOSTARB | \
                       CAN_ERR_CRTL | CAN_ERR_PROT | CAN_ERR_TRX | \
                       CAN_ERR_ACK | CAN_ERR_BUSOFF | CAN_ERR_BUSERROR)

void decode_error_frame(const struct can_frame *frame) {
    printf("[ERROR FRAME] can_id=0x%08X\n", frame->can_id);

    if (frame->can_id & CAN_ERR_BUSOFF)
        printf("  -> Bus-off condition detected\n");

    if (frame->can_id & CAN_ERR_BUSERROR)
        printf("  -> Bus error (check oscilloscope for signal integrity)\n");

    if (frame->can_id & CAN_ERR_CRTL) {
        uint8_t ctrl = frame->data[1];
        if (ctrl & CAN_ERR_CRTL_RX_WARNING)
            printf("  -> RX warning (REC >= 96)\n");
        if (ctrl & CAN_ERR_CRTL_TX_WARNING)
            printf("  -> TX warning (TEC >= 96)\n");
        if (ctrl & CAN_ERR_CRTL_RX_PASSIVE)
            printf("  -> RX passive (REC >= 128) -- check termination\n");
        if (ctrl & CAN_ERR_CRTL_TX_PASSIVE)
            printf("  -> TX passive (TEC >= 128) -- check dominant voltage\n");
    }

    if (frame->can_id & CAN_ERR_PROT) {
        uint8_t prot = frame->data[2];
        uint8_t loc  = frame->data[3];
        printf("  -> Protocol error: type=0x%02X location=0x%02X\n", prot, loc);
        if (prot & CAN_ERR_PROT_BIT)
            printf("     Bit error   -- oscilloscope: check dominant level\n");
        if (prot & CAN_ERR_PROT_STUFF)
            printf("     Stuff error -- oscilloscope: check eye diagram jitter\n");
        if (prot & CAN_ERR_PROT_FORM)
            printf("     Form error  -- oscilloscope: check bit timing\n");
        if (prot & CAN_ERR_PROT_ACK)
            printf("     ACK error   -- oscilloscope: check recessive level\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }

    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return 1; }

    // Enable reception of error frames
    can_err_mask_t err_mask = CAN_ERR_MASK;
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
               &err_mask, sizeof(err_mask));

    struct ifreq ifr;
    strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    printf("Monitoring CAN errors on %s (correlate with oscilloscope)...\n", argv[1]);

    struct can_frame frame;
    while (1) {
        ssize_t nbytes = read(sock, &frame, sizeof(frame));
        if (nbytes < 0) { perror("read"); break; }

        if (frame.can_id & CAN_ERR_FLAG) {
            decode_error_frame(&frame);
        }
    }

    close(sock);
    return 0;
}
```

---

### 2. Bit Timing Calculator (C++)

This utility calculates CAN bit timing parameters given a clock frequency and desired bit rate. The resulting values directly correspond to what you observe on the oscilloscope.

```cpp
// can_bit_timing.cpp
// Compile: g++ -std=c++17 -o can_bit_timing can_bit_timing.cpp
// Run:     ./can_bit_timing 80000000 500000

#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <algorithm>

struct BitTimingConfig {
    uint32_t clock_hz;       // CAN clock frequency
    uint32_t bitrate;        // Target bit rate
    uint32_t brp;            // Baud Rate Prescaler
    uint32_t tseg1;          // Time Segment 1 (PROP + PHASE_SEG1)
    uint32_t tseg2;          // Time Segment 2 (PHASE_SEG2)
    uint32_t sjw;            // Synchronization Jump Width
    double   actual_bitrate; // Achieved bit rate
    double   sample_point;   // Sample point as % of NBT
    double   nbt_ns;         // Nominal bit time in nanoseconds
};

// ISO 11898-1 constraints
static const uint32_t BRP_MIN = 1,  BRP_MAX = 64;
static const uint32_t T1_MIN  = 1,  T1_MAX  = 16;
static const uint32_t T2_MIN  = 1,  T2_MAX  = 8;
static const uint32_t SJW_MAX = 4;

std::vector<BitTimingConfig> calculate_bit_timing(uint32_t clock_hz,
                                                   uint32_t target_bps,
                                                   double   target_sp = 0.875) {
    std::vector<BitTimingConfig> results;
    double best_error = 1e9;

    for (uint32_t brp = BRP_MIN; brp <= BRP_MAX; ++brp) {
        double tq_ns    = (1e9 * brp) / clock_hz;
        double nbt_ns   = 1e9 / target_bps;
        double total_tq = nbt_ns / tq_ns;

        if (total_tq < 4 || total_tq > 25) continue;
        uint32_t ntq = (uint32_t)std::round(total_tq);
        if (ntq < 4 || ntq > 25) continue;

        for (uint32_t tseg2 = T2_MIN; tseg2 <= T2_MAX; ++tseg2) {
            uint32_t tseg1 = ntq - 1 - tseg2; // -1 for SYNC_SEG
            if (tseg1 < T1_MIN || tseg1 > T1_MAX) continue;

            double actual_bps = clock_hz / (double)(brp * (1 + tseg1 + tseg2));
            double sp         = (1.0 + tseg1) / (1.0 + tseg1 + tseg2);
            double sp_err     = std::fabs(sp - target_sp);
            double bps_err    = std::fabs(actual_bps - target_bps) / target_bps;

            if (bps_err > 0.01) continue; // Accept <= 1% error

            double combined_err = bps_err + sp_err;
            if (combined_err < best_error + 0.001) {
                best_error = combined_err;
                uint32_t sjw = std::min(tseg2, SJW_MAX);
                results.push_back({clock_hz, target_bps, brp, tseg1, tseg2,
                                   sjw, actual_bps, sp * 100.0,
                                   (double)ntq * tq_ns});
            }
        }
    }

    std::sort(results.begin(), results.end(), [&](const auto &a, const auto &b) {
        double ea = std::fabs(a.actual_bitrate - target_bps);
        double eb = std::fabs(b.actual_bitrate - target_bps);
        if (std::fabs(ea - eb) > 1.0) return ea < eb;
        return std::fabs(a.sample_point - target_sp * 100) <
               std::fabs(b.sample_point - target_sp * 100);
    });

    if (results.size() > 5) results.resize(5);
    return results;
}

void print_oscilloscope_hints(const BitTimingConfig &cfg) {
    double tq_ns = cfg.nbt_ns / (1 + cfg.tseg1 + cfg.tseg2);
    double sp_ns = cfg.nbt_ns * cfg.sample_point / 100.0;

    std::cout << "\n  Oscilloscope Measurement Targets:\n";
    std::cout << "  -------------------------------------------------\n";
    std::cout << "  Nominal Bit Time:   " << std::fixed << std::setprecision(1)
              << cfg.nbt_ns << " ns\n";
    std::cout << "  Time Quantum (Tq):  " << tq_ns << " ns\n";
    std::cout << "  Sample Point:       " << std::setprecision(1)
              << cfg.sample_point << "% of NBT (" << sp_ns << " ns from SOF)\n";
    std::cout << "  Max Rise/Fall Time: " << (tq_ns * 1.5) << " ns\n";
    std::cout << "  CANH dominant:      3.3 V - 4.0 V\n";
    std::cout << "  CANL dominant:      1.0 V - 1.8 V\n";
    std::cout << "  Differential:       >= 1.5 V (dominant)\n";
    std::cout << "  Eye height target:  >= 1.0 V at sample point\n";
}

int main(int argc, char *argv[]) {
    uint32_t clock_hz   = (argc > 1) ? std::stoul(argv[1]) : 80'000'000;
    uint32_t target_bps = (argc > 2) ? std::stoul(argv[2]) : 500'000;

    std::cout << "CAN Bit Timing Calculator\n";
    std::cout << "Clock:      " << clock_hz / 1e6 << " MHz\n";
    std::cout << "Target:     " << target_bps / 1000 << " kbit/s\n\n";

    auto configs = calculate_bit_timing(clock_hz, target_bps);
    if (configs.empty()) {
        std::cerr << "No valid configuration found.\n";
        return 1;
    }

    int idx = 1;
    for (const auto &cfg : configs) {
        std::cout << "Config #" << idx++ << ":\n";
        std::cout << "  BRP=" << cfg.brp
                  << "  TSEG1=" << cfg.tseg1
                  << "  TSEG2=" << cfg.tseg2
                  << "  SJW=" << cfg.sjw << "\n";
        std::cout << "  Actual bitrate: "
                  << std::fixed << std::setprecision(0) << cfg.actual_bitrate
                  << " bit/s (" << std::setprecision(3)
                  << (cfg.actual_bitrate / target_bps - 1.0) * 100.0 << "% error)\n";
        std::cout << "  Sample point:   "
                  << std::fixed << std::setprecision(1) << cfg.sample_point << "%\n";
        print_oscilloscope_hints(cfg);
        std::cout << "\n";
    }

    return 0;
}
```

---

### 3. Eye Diagram Quality Estimator from Sampled Data (C++)

This example processes digitized oscilloscope data (CSV export) to estimate eye diagram parameters programmatically.

```cpp
// eye_diagram_analyzer.cpp
// Compile: g++ -std=c++17 -o eye_analyzer eye_diagram_analyzer.cpp
// Input:   CSV file with columns: time_ns,voltage_v
//          (exported from oscilloscope at >= 5x oversampling)

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

struct Sample { double time_ns; double voltage_v; };

struct EyeMetrics {
    double eye_height_v;
    double eye_width_ns;
    double crossing_pct;
    double jitter_rms_ns;
    double jitter_pk_pk_ns;
    double noise_rms_v;
    bool   is_open;
};

std::vector<Sample> load_csv(const std::string &filename) {
    std::vector<Sample> data;
    std::ifstream file(filename);
    std::string line;
    std::getline(file, line); // skip header
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        Sample s;
        char comma;
        if (ss >> s.time_ns >> comma >> s.voltage_v)
            data.push_back(s);
    }
    return data;
}

// Detect zero-crossings at the decision threshold
std::vector<double> find_crossings(const std::vector<Sample> &data,
                                    double threshold) {
    std::vector<double> crossings;
    for (size_t i = 1; i < data.size(); ++i) {
        bool prev_above = data[i-1].voltage_v > threshold;
        bool curr_above = data[i  ].voltage_v > threshold;
        if (prev_above != curr_above) {
            double dv = data[i].voltage_v - data[i-1].voltage_v;
            double dt = data[i].time_ns   - data[i-1].time_ns;
            double tc = data[i-1].time_ns +
                        (threshold - data[i-1].voltage_v) * dt / dv;
            crossings.push_back(tc);
        }
    }
    return crossings;
}

EyeMetrics analyze_eye(const std::vector<Sample> &data, double bit_period_ns) {
    EyeMetrics metrics = {};

    if (data.empty()) return metrics;

    double v_max = (*std::max_element(data.begin(), data.end(),
                    [](auto &a, auto &b){ return a.voltage_v < b.voltage_v; })).voltage_v;
    double v_min = (*std::min_element(data.begin(), data.end(),
                    [](auto &a, auto &b){ return a.voltage_v < b.voltage_v; })).voltage_v;
    double v_amp = v_max - v_min;
    double threshold = v_min + v_amp / 2.0;

    auto crossings = find_crossings(data, threshold);
    if (crossings.size() < 4) {
        std::cerr << "Insufficient crossings found\n";
        return metrics;
    }

    std::vector<double> deviations;
    double expected = bit_period_ns / 2.0;
    for (size_t i = 1; i < crossings.size(); ++i) {
        double iv = crossings[i] - crossings[i-1];
        double n  = std::round(iv / expected);
        if (n > 0) deviations.push_back(iv - n * expected);
    }

    if (!deviations.empty()) {
        double sum = 0, sum_sq = 0;
        double pk_min = *std::min_element(deviations.begin(), deviations.end());
        double pk_max = *std::max_element(deviations.begin(), deviations.end());
        for (double d : deviations) { sum += d; sum_sq += d * d; }
        double mean = sum / deviations.size();
        double rms  = std::sqrt(sum_sq / deviations.size() - mean * mean);
        metrics.jitter_rms_ns  = std::fabs(rms);
        metrics.jitter_pk_pk_ns = pk_max - pk_min;
    }

    // Noise estimation on flat (non-transition) regions
    std::vector<double> flat_voltages;
    for (const auto &s : data) {
        bool near = false;
        for (double c : crossings)
            if (std::fabs(s.time_ns - c) < bit_period_ns * 0.1) { near = true; break; }
        if (!near) flat_voltages.push_back(s.voltage_v);
    }

    if (!flat_voltages.empty()) {
        double mean = std::accumulate(flat_voltages.begin(),
                                      flat_voltages.end(), 0.0) / flat_voltages.size();
        double sq_sum = 0;
        for (double v : flat_voltages) sq_sum += (v - mean) * (v - mean);
        metrics.noise_rms_v = std::sqrt(sq_sum / flat_voltages.size());
    }

    metrics.eye_height_v = v_amp - 6.0 * metrics.noise_rms_v;
    metrics.eye_width_ns = bit_period_ns - metrics.jitter_pk_pk_ns;
    metrics.crossing_pct = 50.0;
    metrics.is_open = (metrics.eye_height_v > 0.2 * v_amp) &&
                      (metrics.eye_width_ns  > 0.2 * bit_period_ns);

    return metrics;
}

void print_diagnosis(const EyeMetrics &m, double bit_period_ns) {
    std::cout << "\n=== Eye Diagram Analysis Results ===\n";
    std::cout << "Eye Height:       " << std::fixed << std::setprecision(3)
              << m.eye_height_v << " V"
              << (m.eye_height_v < 1.0 ? "  WARN: check noise/termination" : "  OK") << "\n";
    std::cout << "Eye Width:        " << std::setprecision(1) << m.eye_width_ns << " ns"
              << " (" << (m.eye_width_ns / bit_period_ns * 100.0) << "% of bit period)"
              << (m.eye_width_ns < 0.6 * bit_period_ns ? "  WARN: check jitter" : "  OK") << "\n";
    std::cout << "Jitter RMS:       " << std::setprecision(2) << m.jitter_rms_ns << " ns ("
              << (m.jitter_rms_ns / bit_period_ns * 100.0) << "% of bit period)"
              << (m.jitter_rms_ns > 0.05 * bit_period_ns ? "  WARN" : "  OK") << "\n";
    std::cout << "Jitter Pk-Pk:     " << m.jitter_pk_pk_ns << " ns\n";
    std::cout << "Noise RMS:        " << std::setprecision(4) << m.noise_rms_v << " V\n";
    std::cout << "Eye Open:         " << (m.is_open ? "YES - OK" : "NO - SIGNAL DEGRADED") << "\n";
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <samples.csv> <bit_period_ns>\n"
                  << "  Example: " << argv[0] << " scope_export.csv 2000\n";
        return 1;
    }

    auto data = load_csv(argv[1]);
    double bit_period_ns = std::stod(argv[2]);

    if (data.empty()) { std::cerr << "Failed to load data\n"; return 1; }
    std::cout << "Loaded " << data.size() << " samples\n";

    auto metrics = analyze_eye(data, bit_period_ns);
    print_diagnosis(metrics, bit_period_ns);

    return 0;
}
```

---

## Rust Code Examples

### 1. CAN Error Counter Monitor (Rust, Linux SocketCAN)

```rust
// can_error_monitor/src/main.rs
// Cargo.toml dependencies:
//   socketcan = "3"
//   anyhow = "1"
// Run: cargo run -- vcan0

use std::env;
use socketcan::{CanSocket, Socket, EmbeddedFrame};

// CAN error flag bits (from linux/can/error.h)
const CAN_ERR_FLAG:      u32 = 0x20000000;
const CAN_ERR_BUSOFF:    u32 = 0x00000040;
const CAN_ERR_BUSERROR:  u32 = 0x00000080;
const CAN_ERR_CRTL:      u32 = 0x00000004;
const CAN_ERR_PROT:      u32 = 0x00000008;

const CRTL_RX_WARNING: u8 = 0x04;
const CRTL_TX_WARNING: u8 = 0x08;
const CRTL_RX_PASSIVE: u8 = 0x10;
const CRTL_TX_PASSIVE: u8 = 0x20;

const PROT_BIT:   u8 = 0x01;
const PROT_STUFF: u8 = 0x02;
const PROT_FORM:  u8 = 0x04;
const PROT_ACK:   u8 = 0x20;

fn decode_error_frame(raw_id: u32, data: &[u8]) {
    println!("[ERROR FRAME] can_id=0x{:08X}", raw_id);

    if raw_id & CAN_ERR_BUSOFF != 0 {
        println!("  -> Bus-off: node exceeded TEC 255");
        println!("     Oscilloscope: check for persistent dominant state");
    }
    if raw_id & CAN_ERR_BUSERROR != 0 {
        println!("  -> Bus error: check differential voltage levels");
        println!("     Oscilloscope: verify CANH dominant >= 3.3V, CANL dominant <= 1.8V");
    }
    if raw_id & CAN_ERR_CRTL != 0 && data.len() > 1 {
        let ctrl = data[1];
        if ctrl & CRTL_RX_WARNING != 0 { println!("  -> RX warning (REC >= 96)"); }
        if ctrl & CRTL_TX_WARNING != 0 { println!("  -> TX warning (TEC >= 96)"); }
        if ctrl & CRTL_RX_PASSIVE != 0 {
            println!("  -> RX passive (REC >= 128)");
            println!("     Oscilloscope: inspect recessive level, should return to ~2.5V");
        }
        if ctrl & CRTL_TX_PASSIVE != 0 {
            println!("  -> TX passive (TEC >= 128)");
            println!("     Oscilloscope: measure rise time on dominant transitions");
        }
    }
    if raw_id & CAN_ERR_PROT != 0 && data.len() > 3 {
        let prot = data[2];
        let loc  = data[3];
        println!("  -> Protocol error type=0x{:02X} loc=0x{:02X}", prot, loc);
        if prot & PROT_BIT   != 0 { println!("     Bit error   - check dominant voltage amplitude"); }
        if prot & PROT_STUFF != 0 { println!("     Stuff error - check eye diagram jitter"); }
        if prot & PROT_FORM  != 0 { println!("     Form error  - check bit timing configuration"); }
        if prot & PROT_ACK   != 0 { println!("     ACK error   - verify recessive level < 0.5V diff"); }
    }
}

fn main() -> anyhow::Result<()> {
    let iface = env::args().nth(1).unwrap_or_else(|| "vcan0".to_string());
    println!("Monitoring CAN errors on {} (correlate with oscilloscope)...", iface);

    let sock = CanSocket::open(&iface)?;
    loop {
        match sock.read_frame() {
            Ok(frame) => {
                let raw_id = frame.raw_id();
                if raw_id & CAN_ERR_FLAG != 0 {
                    decode_error_frame(raw_id, frame.data());
                }
            }
            Err(e) => { eprintln!("Read error: {}", e); break; }
        }
    }
    Ok(())
}
```

---

### 2. Bit Timing Calculator (Rust)

```rust
// can_bit_timing/src/main.rs
// Run: cargo run -- 80000000 500000

use std::env;

#[derive(Debug, Clone)]
struct BitTimingConfig {
    brp:          u32,
    tseg1:        u32,
    tseg2:        u32,
    sjw:          u32,
    actual_bps:   f64,
    sample_point: f64,
    nbt_ns:       f64,
}

fn calculate_bit_timing(clock_hz: u32, target_bps: u32, target_sp: f64)
    -> Vec<BitTimingConfig>
{
    let mut results = Vec::new();

    for brp in 1u32..=64 {
        let tq_ns     = 1e9 * brp as f64 / clock_hz as f64;
        let total_tq  = 1e9 / (target_bps as f64 * tq_ns);
        if !(4.0..=25.0).contains(&total_tq) { continue; }
        let ntq = total_tq.round() as u32;
        if !(4..=25).contains(&ntq) { continue; }

        for tseg2 in 1u32..=8 {
            if ntq < 1 + tseg2 + 1 { continue; }
            let tseg1 = ntq - 1 - tseg2;
            if !(1..=16).contains(&tseg1) { continue; }

            let actual_bps = clock_hz as f64 / (brp * (1 + tseg1 + tseg2)) as f64;
            let bps_err    = (actual_bps - target_bps as f64).abs() / target_bps as f64;
            if bps_err > 0.01 { continue; }

            let sp  = (1 + tseg1) as f64 / (1 + tseg1 + tseg2) as f64;
            let sjw = tseg2.min(4);

            results.push(BitTimingConfig {
                brp, tseg1, tseg2, sjw,
                actual_bps,
                sample_point: sp,
                nbt_ns: ntq as f64 * tq_ns,
            });
        }
    }

    results.sort_by(|a, b| {
        let ea = (a.sample_point - target_sp).abs();
        let eb = (b.sample_point - target_sp).abs();
        ea.partial_cmp(&eb).unwrap()
    });
    results.truncate(5);
    results
}

fn print_oscilloscope_hints(cfg: &BitTimingConfig) {
    let tq_ns = cfg.nbt_ns / (1 + cfg.tseg1 + cfg.tseg2) as f64;
    let sp_ns = cfg.nbt_ns * cfg.sample_point;

    println!("  Oscilloscope Measurement Targets:");
    println!("  -----------------------------------------------");
    println!("  Nominal Bit Time:    {:.1} ns", cfg.nbt_ns);
    println!("  Time Quantum (Tq):   {:.2} ns", tq_ns);
    println!("  Sample Point:        {:.1}% -> {:.1} ns from bit start",
             cfg.sample_point * 100.0, sp_ns);
    println!("  Max Rise/Fall Time:  {:.1} ns", tq_ns * 1.5);
    println!("  CANH dominant:       3.3 V - 4.0 V");
    println!("  CANL dominant:       1.0 V - 1.8 V");
    println!("  Differential:        >= 1.5 V dominant, <= 0.1 V recessive");
    println!("  Eye height target:   >= 1.0 V at sample point");
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let clock_hz   = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(80_000_000u32);
    let target_bps = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(500_000u32);

    println!("CAN Bit Timing Calculator (Rust)");
    println!("Clock:  {} MHz", clock_hz / 1_000_000);
    println!("Target: {} kbit/s\n", target_bps / 1_000);

    let configs = calculate_bit_timing(clock_hz, target_bps, 0.875);
    if configs.is_empty() { eprintln!("No valid configuration found."); return; }

    for (i, cfg) in configs.iter().enumerate() {
        let err_pct = (cfg.actual_bps / target_bps as f64 - 1.0) * 100.0;
        println!("Config #{}:", i + 1);
        println!("  BRP={} TSEG1={} TSEG2={} SJW={}",
                 cfg.brp, cfg.tseg1, cfg.tseg2, cfg.sjw);
        println!("  Actual bitrate: {:.0} bit/s ({:+.3}% error)",
                 cfg.actual_bps, err_pct);
        println!("  Sample point:   {:.1}%", cfg.sample_point * 100.0);
        print_oscilloscope_hints(cfg);
        println!();
    }
}
```

---

### 3. Oscilloscope CSV Data Analyzer — Rise Time & Eye Metrics (Rust)

```rust
// scope_analyzer/src/main.rs
// Run: cargo run -- samples.csv 2000.0

use std::{env, fs, io::{self, BufRead}};

#[derive(Clone)]
struct Sample { time_ns: f64, voltage_v: f64 }

#[derive(Debug, Default)]
struct EyeMetrics {
    v_max:          f64,
    v_min:          f64,
    rise_time_ns:   f64,
    fall_time_ns:   f64,
    jitter_rms_ns:  f64,
    jitter_pkpk_ns: f64,
    eye_height_v:   f64,
    eye_width_ns:   f64,
    noise_rms_v:    f64,
    is_open:        bool,
}

fn load_csv(path: &str) -> io::Result<Vec<Sample>> {
    let file   = fs::File::open(path)?;
    let reader = io::BufReader::new(file);
    let mut samples = Vec::new();
    for (i, line) in reader.lines().enumerate() {
        let line = line?;
        if i == 0 { continue; }
        let mut parts = line.splitn(2, ',');
        let t = parts.next().and_then(|s| s.trim().parse::<f64>().ok());
        let v = parts.next().and_then(|s| s.trim().parse::<f64>().ok());
        if let (Some(t), Some(v)) = (t, v) {
            samples.push(Sample { time_ns: t, voltage_v: v });
        }
    }
    Ok(samples)
}

fn find_crossings(data: &[Sample], threshold: f64) -> Vec<f64> {
    let mut crossings = Vec::new();
    for i in 1..data.len() {
        let above_prev = data[i-1].voltage_v > threshold;
        let above_curr = data[i  ].voltage_v > threshold;
        if above_prev != above_curr {
            let dv = data[i].voltage_v - data[i-1].voltage_v;
            let dt = data[i].time_ns   - data[i-1].time_ns;
            crossings.push(data[i-1].time_ns +
                           (threshold - data[i-1].voltage_v) * dt / dv);
        }
    }
    crossings
}

fn measure_rise_fall(data: &[Sample]) -> (f64, f64) {
    let v_max = data.iter().map(|s| s.voltage_v).fold(f64::NEG_INFINITY, f64::max);
    let v_min = data.iter().map(|s| s.voltage_v).fold(f64::INFINITY,     f64::min);
    let v_lo  = v_min + (v_max - v_min) * 0.10;
    let v_hi  = v_min + (v_max - v_min) * 0.90;

    let mut rises = Vec::new();
    let mut falls = Vec::new();
    let mut t10r: Option<f64> = None;
    let mut t90f: Option<f64> = None;

    for i in 1..data.len() {
        let prev = &data[i-1];
        let curr = &data[i];
        let interp = |thr: f64| -> f64 {
            let dv = curr.voltage_v - prev.voltage_v;
            let dt = curr.time_ns   - prev.time_ns;
            prev.time_ns + (thr - prev.voltage_v) * dt / dv
        };
        if curr.voltage_v > prev.voltage_v {
            if prev.voltage_v <= v_lo && curr.voltage_v >= v_lo { t10r = Some(interp(v_lo)); }
            if let Some(t10) = t10r {
                if prev.voltage_v <= v_hi && curr.voltage_v >= v_hi {
                    rises.push(interp(v_hi) - t10);
                    t10r = None;
                }
            }
        } else {
            if prev.voltage_v >= v_hi && curr.voltage_v <= v_hi { t90f = Some(interp(v_hi)); }
            if let Some(t90) = t90f {
                if prev.voltage_v >= v_lo && curr.voltage_v <= v_lo {
                    falls.push(t90 - interp(v_lo));
                    t90f = None;
                }
            }
        }
    }

    let avg = |v: &[f64]| if v.is_empty() { 0.0 } else { v.iter().sum::<f64>() / v.len() as f64 };
    (avg(&rises), avg(&falls))
}

fn analyze(data: &[Sample], bit_period_ns: f64) -> EyeMetrics {
    let mut m = EyeMetrics::default();
    m.v_max = data.iter().map(|s| s.voltage_v).fold(f64::NEG_INFINITY, f64::max);
    m.v_min = data.iter().map(|s| s.voltage_v).fold(f64::INFINITY,     f64::min);
    let v_amp = m.v_max - m.v_min;
    let threshold = m.v_min + v_amp * 0.5;

    let (rt, ft) = measure_rise_fall(data);
    m.rise_time_ns = rt;
    m.fall_time_ns = ft;

    let crossings = find_crossings(data, threshold);
    if crossings.len() >= 4 {
        let half = bit_period_ns / 2.0;
        let devs: Vec<f64> = (1..crossings.len())
            .filter_map(|i| {
                let iv = crossings[i] - crossings[i-1];
                let n  = (iv / half).round();
                if n >= 1.0 { Some(iv - n * half) } else { None }
            })
            .collect();

        if !devs.is_empty() {
            let mean = devs.iter().sum::<f64>() / devs.len() as f64;
            let var  = devs.iter().map(|d| (d - mean).powi(2)).sum::<f64>() / devs.len() as f64;
            m.jitter_rms_ns  = var.sqrt();
            let pk_min = devs.iter().cloned().fold(f64::INFINITY,     f64::min);
            let pk_max = devs.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
            m.jitter_pkpk_ns = pk_max - pk_min;
        }
    }

    let flat: Vec<f64> = data.iter()
        .filter(|s| crossings.iter().all(|&c| (s.time_ns - c).abs() > bit_period_ns * 0.1))
        .map(|s| s.voltage_v)
        .collect();

    if !flat.is_empty() {
        let mean = flat.iter().sum::<f64>() / flat.len() as f64;
        let var  = flat.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / flat.len() as f64;
        m.noise_rms_v = var.sqrt();
    }

    m.eye_height_v = v_amp - 6.0 * m.noise_rms_v;
    m.eye_width_ns = bit_period_ns - m.jitter_pkpk_ns;
    m.is_open = m.eye_height_v > 0.2 * v_amp && m.eye_width_ns > 0.2 * bit_period_ns;
    m
}

fn diagnose(m: &EyeMetrics, bit_period_ns: f64) {
    println!("\n=== Oscilloscope Signal Analysis ===");
    println!("Amplitude:       {:.3} V  (min={:.3}, max={:.3})",
             m.v_max - m.v_min, m.v_min, m.v_max);
    println!("Rise Time:       {:.1} ns  {}", m.rise_time_ns,
             if m.rise_time_ns > bit_period_ns * 0.15 { "WARN: slow -- check termination/capacitance" } else { "OK" });
    println!("Fall Time:       {:.1} ns  {}", m.fall_time_ns,
             if m.fall_time_ns > bit_period_ns * 0.15 { "WARN: slow" } else { "OK" });
    println!("Eye Height:      {:.3} V  {}", m.eye_height_v,
             if m.eye_height_v < 1.0 { "WARN: check noise/voltage levels" } else { "OK" });
    println!("Eye Width:       {:.1} ns ({:.1}% of bit period)",
             m.eye_width_ns, m.eye_width_ns / bit_period_ns * 100.0);
    println!("Jitter RMS:      {:.2} ns ({:.1}%)",
             m.jitter_rms_ns, m.jitter_rms_ns / bit_period_ns * 100.0);
    println!("Jitter Pk-Pk:    {:.2} ns", m.jitter_pkpk_ns);
    println!("Noise RMS:       {:.4} V", m.noise_rms_v);
    println!("Eye Open:        {}", if m.is_open { "YES - OK" } else { "NO - DEGRADED SIGNAL" });

    println!("\n--- Diagnostic Hints ---");
    if m.rise_time_ns > bit_period_ns * 0.15 {
        println!("- Slow rise: Measure ~60 Ohm CANH-CANL (power off). Check stub length/capacitance.");
    }
    if m.noise_rms_v > 0.05 {
        println!("- High noise: Check EMI coupling. Verify differential signaling and ground loops.");
    }
    if m.jitter_pkpk_ns > bit_period_ns * 0.15 {
        println!("- High jitter: Verify clock stability. Check for reflections/impedance mismatch.");
    }
    if !m.is_open {
        println!("- Eye closed: Immediate inspection needed.");
        println!("  Check order: termination -> voltage levels -> rise times -> jitter");
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 3 {
        eprintln!("Usage: {} <samples.csv> <bit_period_ns>", args[0]);
        return;
    }
    let bit_period_ns = args[2].parse::<f64>().expect("Invalid bit period");

    match load_csv(&args[1]) {
        Ok(data) => {
            println!("Loaded {} samples from {}", data.len(), args[1]);
            let m = analyze(&data, bit_period_ns);
            diagnose(&m, bit_period_ns);
        }
        Err(e) => eprintln!("Error: {}", e),
    }
}
```

---

## Summary

Oscilloscope analysis of CAN signals is an essential discipline for diagnosing physical layer faults that are invisible to software-level tools. The key measurements and their significance are:

**Voltage Levels** confirm that the CAN transceiver is producing correct dominant (CANH ~3.5 V, CANL ~1.5 V) and recessive (~2.5 V common mode) states. Deviations indicate wiring faults, transceiver damage, or ground potential differences between nodes.

**Rise and Fall Times** — measured from 10% to 90% of amplitude — must satisfy the ISO 11898-2 limits for the configured bit rate (e.g., <= 150 ns at 500 kbit/s). Slow transitions point to excessive capacitance or missing termination; excessively fast transitions with ringing indicate impedance mismatch or missing split-termination capacitors.

**Eye Diagrams** aggregate hundreds of bit periods to reveal the combined effect of noise, jitter, and waveform distortion. A healthy eye has a wide, tall opening with a clear, symmetric crossing point near 50%. A closed eye signals that bit errors will occur under real operating conditions. Key eye diagram parameters are height (noise margin), width (timing margin), and the jitter distribution at crossing points.

**Termination** is the most common root cause of physical layer failures. A healthy bus measures ~60 Ohm between CANH and CANL with power off. A single 120 Ohm terminator, double termination on one end, or missing termination produce characteristic and identifiable oscilloscope signatures.

The **C/C++ and Rust code examples** complement oscilloscope measurements by monitoring kernel-reported error counters (which map directly to physical events such as bit errors, stuff errors, and bus-off), computing the bit timing parameters that determine what the oscilloscope should show, and processing exported scope data to automate eye diagram quality assessment. This combination of hardware measurement and software analysis provides a complete diagnostic picture — from the raw electrical signal on the wire through to network-level reliability.

---

*References: ISO 11898-2:2016, CiA 601 (CAN physical layer), Bosch CAN Specification 2.0*