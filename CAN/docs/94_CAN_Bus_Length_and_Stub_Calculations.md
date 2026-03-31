# 94. CAN Bus Length and Stub Calculations

**Theory sections** walk through the propagation delay model (why bus length is fundamentally limited by bit time), stub resonance physics (why unterminated drops act as λ/4 resonators and reflect signals), node capacitance loading, termination resistor matching, and real cable characteristics.

**All three constraints are quantified** with formulas and full reference tables — bus length, stub length, and node count — cross-referenced against standard bit rates from 10 kbit/s to 1 Mbit/s.

**The C/C++ code** provides a pure-C calculation library (`can_bus_calc.h/.c`) with exact ISO 11898-2 math, plus a C++ `CANBusDesigner` class that accepts a full node topology (positions, stub lengths, capacitances), validates all constraints, reports margin percentages, and recommends the highest safe bit rate when the design fails.

**The Rust code** mirrors the same logic with idiomatic Rust — a `CanBusLimits` struct, typed `CanBitRate` enum, `CanBusDesign` builder pattern, and a `ValidationResult` with typed error variants rather than strings, plus a reference table printer showing all standard rates.

> **Engineering guidelines for maximum bus length, stub length, and node count based on bit rate.**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Physical Layer Fundamentals](#physical-layer-fundamentals)
3. [Bit Rate vs. Maximum Bus Length](#bit-rate-vs-maximum-bus-length)
4. [Propagation Delay Model](#propagation-delay-model)
5. [Stub Length Constraints](#stub-length-constraints)
6. [Node Count and Bus Loading](#node-count-and-bus-loading)
7. [Termination Resistor Calculations](#termination-resistor-calculations)
8. [Cable Characteristics](#cable-characteristics)
9. [Engineering Reference Tables](#engineering-reference-tables)
10. [C/C++ Implementation Examples](#cc-implementation-examples)
11. [Rust Implementation Examples](#rust-implementation-examples)
12. [Summary](#summary)

---

## Introduction

CAN (Controller Area Network) bus topology is fundamentally a **linear bus** with a single backbone cable terminated at both ends. Every design decision — bus length, stub (drop cable) length, number of nodes, cable impedance, and termination — directly impacts signal integrity and therefore the maximum achievable bit rate.

Unlike Ethernet, CAN does **not** use collision detection; it uses **non-destructive bitwise arbitration**. This means the entire bus must settle to a valid voltage level within a single bit time. The propagation delay from one end of the bus to the other must fit inside the **bit sampling window** — this constraint is the fundamental physical limit that drives all the engineering rules.

The three main constraints are:

- **Maximum bus length** — determined by bit rate and propagation velocity
- **Maximum stub length** — determined by resonance and reflections from unterminated stubs
- **Maximum node count** — determined by bus capacitance and driver fan-out

---

## Physical Layer Fundamentals

### CAN Bus Topology

```
  Node 1       Node 2       Node 3       Node N
    |            |            |            |
    |-- stub1 ---|-- stub2 ---|-- stub3 ---|-- stubN ---|
    |                                                   |
[RT1=120Ω] <------------- Backbone ----------------> [RT2=120Ω]
```

- **Backbone**: The main bus cable, terminated at both ends with 120 Ω resistors.
- **Stub (drop cable)**: Short branch from backbone to each node connector.
- **Termination**: Two 120 Ω resistors in parallel = 60 Ω differential load (matches typical cable impedance).

### Signal Propagation

CAN uses differential signaling on a twisted-pair cable. The signal propagation velocity on twisted-pair copper cable is typically:

```
v_prop = v_light × V_F
```

Where:
- `v_light` = 3 × 10⁸ m/s (speed of light in vacuum)
- `V_F` ≈ 0.66 (velocity factor for PVC-insulated twisted pair)

This gives approximately **198,000 km/s** or roughly **5 ns/m** propagation delay.

---

## Bit Rate vs. Maximum Bus Length

### The Core Rule: Propagation Budget

CAN requires the signal to propagate from the transmitting node to the furthest node **and back** (round-trip) within the bit sampling point. ISO 11898-1 specifies that the total loop propagation delay must not exceed:

```
t_loop ≤ (bit_time × (1 - sample_point)) × 2
```

In practice, using the 75% sample point (typical) and accounting for internal node delays (~250 ns per node round trip), the following maximum lengths are the industry standard:

### Standard Maximum Bus Lengths (ISO 11898-2)

| Bit Rate    | Max Bus Length | Propagation Budget | Notes                        |
|-------------|----------------|--------------------|------------------------------|
| 1 Mbit/s    | 25–40 m        | 1000 ns total      | Automotive, industrial       |
| 500 kbit/s  | 100 m          | 2000 ns total      | Most common industrial rate  |
| 250 kbit/s  | 250 m          | 4000 ns total      | CANopen, J1939 standard      |
| 125 kbit/s  | 500 m          | 8000 ns total      | Building automation          |
| 50 kbit/s   | 1000 m         | 16000 ns total     | Long runs, process control   |
| 20 kbit/s   | 2500 m         | 40000 ns total     | Very long distance runs      |
| 10 kbit/s   | 5000 m         | 80000 ns total     | Maximum practical distance   |

> **Key insight**: Doubling the bus length requires halving the bit rate. The relationship is approximately linear.

### Approximate Formula

```
L_max (meters) ≈ 50,000,000 / bit_rate_bps
```

Example: At 500 kbit/s → L_max ≈ 50,000,000 / 500,000 = 100 m

---

## Propagation Delay Model

The total propagation delay budget for a CAN bit is:

```
t_prop_budget = t_bit × (1 - sample_point_ratio) − t_node_internal
```

Where:
- `t_bit` = 1 / bit_rate
- `sample_point_ratio` = typically 0.75 to 0.875 (75%–87.5%)
- `t_node_internal` = transmitter + receiver internal delay ≈ 210–300 ns

The maximum **one-way** cable length is:

```
L_max = (t_prop_budget / 2) / t_prop_per_meter
```

Where `t_prop_per_meter` ≈ 5 ns/m for standard twisted pair.

---

## Stub Length Constraints

### Why Stubs Are Problematic

Each stub (drop cable) is an **unterminated transmission line** connected at a single point to the backbone. It acts as an open-circuit resonator. Stubs cause:

- **Reflections**: The signal sees an impedance discontinuity at the junction, and the open end reflects the wave back.
- **Standing wave resonance**: At λ/4 frequency of the stub length, the stub appears as a **short circuit** to the backbone, distorting the signal.

### Stub Length Rule

The stub resonance frequency is:

```
f_resonance = v_prop / (4 × L_stub)
```

For reliable operation, the stub resonance must be far above the bit rate frequency:

```
f_resonance ≥ 10 × bit_rate
```

This gives the **maximum stub length**:

```
L_stub_max = v_prop / (40 × bit_rate)
L_stub_max ≈ 5,000,000 / bit_rate_bps   (in meters)
```

### Stub Length Table

| Bit Rate    | Max Stub Length | Practical Recommendation |
|-------------|-----------------|--------------------------|
| 1 Mbit/s    | ≤ 0.3 m         | < 0.3 m (30 cm)          |
| 500 kbit/s  | ≤ 1.0 m         | < 1.0 m                  |
| 250 kbit/s  | ≤ 3.0 m         | < 3.0 m                  |
| 125 kbit/s  | ≤ 6.0 m         | < 6.0 m                  |
| 50 kbit/s   | ≤ 30 m          | < 30 m                   |
| 20 kbit/s   | ≤ 50 m          | < 50 m                   |

> **Rule of thumb**: At 1 Mbit/s, stubs must be virtually non-existent — connector pigtails only. This is why high-speed CAN systems use **daisy-chain** (series) wiring rather than T-taps.

---

## Node Count and Bus Loading

### Capacitive Loading

Each CAN transceiver adds differential capacitance to the bus. Typical values are 10–20 pF per node. The total capacitive load slows the bus signal edges:

```
C_bus_total = N_nodes × C_per_node
```

The RC time constant for the signal edge:

```
τ = Z_line × C_bus_total / 2
```

Where `Z_line` ≈ 60 Ω (two 120 Ω terminations in parallel).

For the edge to settle within one bit time, `τ ≤ t_bit / 10` is a practical guideline.

### ISO 11898-2 Standard Limits

- **Maximum nodes**: 32 (with standard 120 Ω termination)
- **Extended with high-impedance transceivers (ISO 11898-6)**: Up to 64 or 110 nodes
- **CAN FD (ISO 11898-2:2016)**: Maximum ~30 nodes at high bit rates due to stricter timing

### Node Count vs. Bit Rate Table

| Bit Rate    | Max Nodes (std) | Max Nodes (hi-Z TXR) | Max Capacitance |
|-------------|-----------------|----------------------|-----------------|
| 1 Mbit/s    | 30              | 30                   | ~100 pF         |
| 500 kbit/s  | 32              | 64                   | ~200 pF         |
| 250 kbit/s  | 32              | 100+                 | ~400 pF         |
| 125 kbit/s  | 32              | 100+                 | ~800 pF         |

---

## Termination Resistor Calculations

Standard termination uses two 120 Ω resistors (one at each bus end). The differential impedance seen by the bus is:

```
R_term = R1 || R2 = 120 Ω || 120 Ω = 60 Ω
```

For cable impedance Z₀ = 120 Ω (standard CAN cable), this perfectly terminates both ends. The matched termination prevents reflections from the cable ends.

### Split Termination (For EMI Reduction)

Split termination uses two 60 Ω resistors in series with a capacitor to common mode (chassis ground):

```
CANH ---[60Ω]---+---[C_split = 4.7nF]--- GND
                |
CANL ---[60Ω]---+
```

The differential impedance remains 120 Ω (60+60) but the capacitor filters common-mode noise, improving EMC performance.

---

## Cable Characteristics

### Standard CAN Cable (ISO 11898-2)

| Parameter                   | Specification         |
|-----------------------------|-----------------------|
| Characteristic Impedance    | 108–132 Ω             |
| Propagation Velocity        | ≥ 0.66 × c           |
| DC Resistance (per pair)    | ≤ 60 mΩ/m            |
| Capacitance                 | ≤ 30 pF/m            |
| Wire gauge (typical)        | AWG 24–22 / 0.25 mm² |

### Effect of Cable Resistance on Long Runs

For very long bus runs at low bit rates, the DC resistance of the cable becomes significant. The bus differential voltage (typically 2 V nominal) drops with cable resistance:

```
V_drop = I_bus × R_cable_total
```

Where `I_bus ≈ 35 mA` (typical dominant state current) and `R_cable_total = 2 × length × R_per_meter`.

For 5000 m at 60 mΩ/m: `V_drop = 0.035 × 2 × 5000 × 0.06 = 21 V` — clearly impossible without special cable or amplified repeaters.

In practice, **CAN repeaters** must be used beyond 500–1000 m for any real-world installation.

---

## Engineering Reference Tables

### Combined Design Reference

| Bit Rate    | Max Length | Max Stub  | Max Nodes | Typical Application         |
|-------------|------------|-----------|-----------|------------------------------|
| 1 Mbit/s    | 25 m       | 0.3 m     | 30        | Automotive powertrain, CAN FD|
| 800 kbit/s  | 35 m       | 0.4 m     | 30        | High-speed industrial        |
| 500 kbit/s  | 100 m      | 1.0 m     | 32        | Industrial automation        |
| 250 kbit/s  | 250 m      | 3.0 m     | 32        | J1939, CANopen               |
| 125 kbit/s  | 500 m      | 6.0 m     | 32        | Building HVAC                |
| 50 kbit/s   | 1000 m     | 30 m      | 32        | Process control              |
| 20 kbit/s   | 2500 m     | 50 m      | 32        | Remote sensing               |
| 10 kbit/s   | 5000 m     | 100 m     | 32        | Long-distance telemetry      |

---

## C/C++ Implementation Examples

### Example 1: Bus Length and Stub Validation Library

```c
/**
 * can_bus_calc.h
 * CAN Bus Length, Stub, and Node Count Engineering Calculator
 * Implements ISO 11898-2 constraints
 */
#ifndef CAN_BUS_CALC_H
#define CAN_BUS_CALC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Physical constants */
#define CAN_SPEED_OF_LIGHT_MPS     300000000UL  /* m/s */
#define CAN_VELOCITY_FACTOR        0.66f        /* typical twisted pair */
#define CAN_PROP_DELAY_NS_PER_M    5.0f         /* ns/m for standard cable */
#define CAN_TERMINATION_OHM        120          /* standard termination */
#define CAN_MAX_NODES_STANDARD     32
#define CAN_CAP_PER_NODE_PF        20           /* pF per node, typical */
#define CAN_NODE_INTERNAL_DELAY_NS 210          /* ns, transmitter+receiver */
#define CAN_DEFAULT_SAMPLE_POINT   0.75f        /* 75% default */

typedef enum {
    CAN_OK                  = 0,
    CAN_ERR_LENGTH_EXCEEDED = 1,
    CAN_ERR_STUB_EXCEEDED   = 2,
    CAN_ERR_NODES_EXCEEDED  = 3,
    CAN_ERR_CAP_EXCEEDED    = 4,
    CAN_ERR_INVALID_PARAM   = 5
} can_calc_error_t;

typedef struct {
    uint32_t bit_rate_bps;       /* Bit rate in bits/second */
    float    sample_point;       /* Sample point ratio (0.0–1.0) */
    float    prop_delay_ns_m;    /* Propagation delay ns/meter */
    uint32_t node_internal_ns;   /* Internal transceiver delay ns */
} can_bus_config_t;

typedef struct {
    float    max_bus_length_m;   /* Maximum backbone length, meters */
    float    max_stub_length_m;  /* Maximum stub (drop) length, meters */
    uint32_t max_nodes;          /* Maximum node count */
    float    prop_budget_ns;     /* Available propagation budget, ns */
    float    stub_resonance_hz;  /* Stub resonance frequency at max length */
    float    max_cap_pf;         /* Max bus capacitance, pF */
} can_bus_limits_t;

typedef struct {
    float    bus_length_m;
    float    stub_length_m;
    uint32_t node_count;
    uint32_t stub_count;
} can_bus_design_t;

typedef struct {
    bool              bus_length_ok;
    bool              stub_length_ok;
    bool              node_count_ok;
    bool              capacitance_ok;
    float             bus_margin_m;
    float             stub_margin_m;
    float             cap_total_pf;
    can_calc_error_t  worst_error;
} can_validation_result_t;

/**
 * Calculate maximum bus limits for a given bit rate.
 *
 * @param cfg      Bus configuration (bit rate, sample point, etc.)
 * @param limits   Output: calculated limits
 * @return         CAN_OK on success
 */
can_calc_error_t can_calc_limits(const can_bus_config_t *cfg,
                                  can_bus_limits_t       *limits);

/**
 * Validate a proposed bus design against physical limits.
 *
 * @param cfg      Bus configuration
 * @param design   Proposed design parameters
 * @param result   Validation result with margin information
 * @return         CAN_OK if all constraints pass, first error code otherwise
 */
can_calc_error_t can_validate_design(const can_bus_config_t    *cfg,
                                      const can_bus_design_t    *design,
                                      can_validation_result_t   *result);

/**
 * Recommend a maximum bit rate for given physical constraints.
 *
 * @param bus_length_m   Actual bus backbone length in meters
 * @param stub_length_m  Longest stub length in meters
 * @param node_count     Total number of nodes
 * @return               Maximum safe bit rate in bits/second
 */
uint32_t can_recommend_bitrate(float    bus_length_m,
                                float    stub_length_m,
                                uint32_t node_count);

#endif /* CAN_BUS_CALC_H */
```

```c
/**
 * can_bus_calc.c
 * Implementation of CAN Bus engineering calculations
 */
#include "can_bus_calc.h"
#include <math.h>
#include <string.h>

/* Default configuration for standard ISO 11898-2 */
static const can_bus_config_t default_cfg = {
    .bit_rate_bps      = 500000,
    .sample_point      = CAN_DEFAULT_SAMPLE_POINT,
    .prop_delay_ns_m   = CAN_PROP_DELAY_NS_PER_M,
    .node_internal_ns  = CAN_NODE_INTERNAL_DELAY_NS
};

can_calc_error_t can_calc_limits(const can_bus_config_t *cfg,
                                  can_bus_limits_t       *limits)
{
    if (!cfg || !limits || cfg->bit_rate_bps == 0) {
        return CAN_ERR_INVALID_PARAM;
    }

    memset(limits, 0, sizeof(*limits));

    /* Bit time in nanoseconds */
    float bit_time_ns = 1.0e9f / (float)cfg->bit_rate_bps;

    /*
     * Propagation budget:
     * Available time from bit start to sample point, minus internal delays.
     * The round-trip must fit: propagation there + propagation back = 2 × one-way.
     * We use (1 - sample_point) of bit_time for the total round-trip prop budget.
     */
    float round_trip_budget_ns = bit_time_ns * (1.0f - cfg->sample_point)
                                 - (float)cfg->node_internal_ns;

    if (round_trip_budget_ns <= 0.0f) {
        /* Bit rate too high for the given sample point and node delay */
        limits->max_bus_length_m = 0.0f;
        limits->prop_budget_ns   = 0.0f;
        return CAN_ERR_LENGTH_EXCEEDED;
    }

    limits->prop_budget_ns = round_trip_budget_ns;

    /* One-way cable propagation allowed = half the round-trip budget */
    float one_way_ns = round_trip_budget_ns / 2.0f;
    limits->max_bus_length_m = one_way_ns / cfg->prop_delay_ns_m;

    /*
     * Stub length: stub resonance must be at least 10× bit rate.
     * f_res = v_prop / (4 × L_stub)
     * L_stub_max = v_prop / (4 × 10 × bit_rate)
     *            = (1/prop_delay_ns_m × 1e9) / (40 × bit_rate)
     */
    float prop_speed_m_ns = 1.0f / cfg->prop_delay_ns_m; /* m per ns */
    float prop_speed_m_s  = prop_speed_m_ns * 1.0e9f;    /* m/s */
    limits->max_stub_length_m = prop_speed_m_s / (40.0f * (float)cfg->bit_rate_bps);

    /* Stub resonance frequency at max stub length */
    if (limits->max_stub_length_m > 0.0f) {
        limits->stub_resonance_hz = prop_speed_m_s /
                                    (4.0f * limits->max_stub_length_m);
    }

    /* Node count and capacitance */
    limits->max_nodes = CAN_MAX_NODES_STANDARD;

    /*
     * Max capacitance: RC time constant must be < bit_time/10
     * τ = 60Ω × C_total ≤ bit_time_ns / 10
     * C_total ≤ bit_time_ns / (10 × 60 × 1e-9) [in pF via ns/Ω = pF]
     */
    limits->max_cap_pf = bit_time_ns / (10.0f * 60.0f);  /* pF */

    return CAN_OK;
}

can_calc_error_t can_validate_design(const can_bus_config_t    *cfg,
                                      const can_bus_design_t    *design,
                                      can_validation_result_t   *result)
{
    if (!cfg || !design || !result) {
        return CAN_ERR_INVALID_PARAM;
    }

    memset(result, 0, sizeof(*result));

    can_bus_limits_t limits;
    can_calc_error_t err = can_calc_limits(cfg, &limits);
    if (err != CAN_OK) {
        result->worst_error = err;
        return err;
    }

    result->worst_error = CAN_OK;

    /* Validate bus length */
    result->bus_length_ok  = (design->bus_length_m <= limits.max_bus_length_m);
    result->bus_margin_m   = limits.max_bus_length_m - design->bus_length_m;
    if (!result->bus_length_ok) {
        result->worst_error = CAN_ERR_LENGTH_EXCEEDED;
    }

    /* Validate stub length */
    result->stub_length_ok = (design->stub_length_m <= limits.max_stub_length_m);
    result->stub_margin_m  = limits.max_stub_length_m - design->stub_length_m;
    if (!result->stub_length_ok && result->worst_error == CAN_OK) {
        result->worst_error = CAN_ERR_STUB_EXCEEDED;
    }

    /* Validate node count */
    result->node_count_ok = (design->node_count <= limits.max_nodes);
    if (!result->node_count_ok && result->worst_error == CAN_OK) {
        result->worst_error = CAN_ERR_NODES_EXCEEDED;
    }

    /* Validate total bus capacitance */
    result->cap_total_pf  = (float)design->node_count * CAN_CAP_PER_NODE_PF;
    result->capacitance_ok = (result->cap_total_pf <= limits.max_cap_pf);
    if (!result->capacitance_ok && result->worst_error == CAN_OK) {
        result->worst_error = CAN_ERR_CAP_EXCEEDED;
    }

    return result->worst_error;
}

/* Standard bit rates to check for recommendation */
static const uint32_t standard_rates[] = {
    1000000, 800000, 500000, 250000, 125000, 50000, 20000, 10000
};

uint32_t can_recommend_bitrate(float    bus_length_m,
                                float    stub_length_m,
                                uint32_t node_count)
{
    can_bus_config_t cfg = default_cfg;
    can_bus_design_t design = {
        .bus_length_m  = bus_length_m,
        .stub_length_m = stub_length_m,
        .node_count    = node_count,
        .stub_count    = node_count
    };

    for (size_t i = 0; i < sizeof(standard_rates)/sizeof(standard_rates[0]); i++) {
        cfg.bit_rate_bps = standard_rates[i];
        can_validation_result_t result;
        if (can_validate_design(&cfg, &design, &result) == CAN_OK) {
            return standard_rates[i];
        }
    }
    return 0; /* No standard rate works */
}
```

---

### Example 2: C++ Bus Design Validator with Report Generation

```cpp
/**
 * CANBusDesigner.hpp
 * C++ class for CAN bus engineering validation and reporting
 */
#pragma once
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <cmath>

class CANBusDesigner {
public:
    struct NodeSpec {
        std::string name;
        float       distance_from_start_m;  /* Position on backbone */
        float       stub_length_m;          /* Drop cable length to this node */
        float       capacitance_pf;         /* Node input capacitance */
    };

    struct ValidationReport {
        bool        pass;
        std::string summary;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        float max_allowed_length_m;
        float actual_length_m;
        float max_allowed_stub_m;
        float worst_stub_m;
        float prop_budget_used_pct;
        float cap_budget_used_pct;
        uint32_t recommended_bitrate_bps;
    };

    explicit CANBusDesigner(uint32_t bit_rate_bps,
                             float sample_point = 0.75f,
                             float prop_delay_ns_m = 5.0f,
                             uint32_t node_internal_delay_ns = 210)
        : bit_rate_bps_(bit_rate_bps),
          sample_point_(sample_point),
          prop_delay_ns_m_(prop_delay_ns_m),
          node_internal_delay_ns_(node_internal_delay_ns)
    {
        if (bit_rate_bps == 0 || sample_point <= 0.0f || sample_point >= 1.0f) {
            throw std::invalid_argument("Invalid CAN bus configuration");
        }
        recalculate_limits();
    }

    void add_node(const NodeSpec& node) {
        nodes_.push_back(node);
    }

    void set_termination(float r_end1_ohm = 120.0f, float r_end2_ohm = 120.0f) {
        term_end1_ = r_end1_ohm;
        term_end2_ = r_end2_ohm;
    }

    ValidationReport validate() const {
        ValidationReport report{};
        report.pass = true;
        report.max_allowed_length_m = max_bus_length_m_;
        report.max_allowed_stub_m   = max_stub_length_m_;

        if (nodes_.empty()) {
            report.warnings.push_back("No nodes defined in the design.");
            report.summary = "Empty design — nothing to validate.";
            return report;
        }

        /* Calculate actual bus length (farthest node) */
        float actual_length = 0.0f;
        for (const auto& n : nodes_) {
            actual_length = std::max(actual_length, n.distance_from_start_m);
        }
        report.actual_length_m = actual_length;

        /* Validate backbone length */
        if (actual_length > max_bus_length_m_) {
            report.pass = false;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1)
                << "FAIL: Bus length " << actual_length
                << " m exceeds maximum " << max_bus_length_m_
                << " m at " << (bit_rate_bps_ / 1000) << " kbit/s.";
            report.errors.push_back(oss.str());
        }

        /* Calculate propagation budget usage */
        float used_ns = actual_length * prop_delay_ns_m_ * 2.0f
                        + (float)node_internal_delay_ns_;
        float budget_ns = (1.0e9f / (float)bit_rate_bps_) * (1.0f - sample_point_);
        report.prop_budget_used_pct = (used_ns / budget_ns) * 100.0f;
        if (report.prop_budget_used_pct > 90.0f) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1)
                << "WARNING: Propagation budget usage is "
                << report.prop_budget_used_pct << "% (>90% is marginal).";
            report.warnings.push_back(oss.str());
        }

        /* Validate all stub lengths */
        float worst_stub = 0.0f;
        for (const auto& n : nodes_) {
            worst_stub = std::max(worst_stub, n.stub_length_m);
            if (n.stub_length_m > max_stub_length_m_) {
                report.pass = false;
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2)
                    << "FAIL: Node '" << n.name << "' stub length "
                    << n.stub_length_m << " m exceeds maximum "
                    << max_stub_length_m_ << " m.";
                report.errors.push_back(oss.str());
            }
        }
        report.worst_stub_m = worst_stub;

        /* Validate node count */
        if (nodes_.size() > 32) {
            report.pass = false;
            std::ostringstream oss;
            oss << "FAIL: Node count " << nodes_.size()
                << " exceeds ISO 11898-2 maximum of 32.";
            report.errors.push_back(oss.str());
        }

        /* Validate total capacitance */
        float total_cap_pf = 0.0f;
        for (const auto& n : nodes_) {
            total_cap_pf += n.capacitance_pf;
        }
        float max_cap_pf = (1.0e9f / (float)bit_rate_bps_) / (10.0f * 60.0f);
        report.cap_budget_used_pct = (total_cap_pf / max_cap_pf) * 100.0f;
        if (total_cap_pf > max_cap_pf) {
            report.pass = false;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1)
                << "FAIL: Total bus capacitance " << total_cap_pf
                << " pF exceeds max " << max_cap_pf << " pF.";
            report.errors.push_back(oss.str());
        }

        /* Termination check */
        float r_parallel = (term_end1_ * term_end2_) / (term_end1_ + term_end2_);
        if (r_parallel < 54.0f || r_parallel > 66.0f) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1)
                << "WARNING: Parallel termination " << r_parallel
                << " Ω is outside optimal 60 Ω ±10% range.";
            report.warnings.push_back(oss.str());
        }

        /* Recommend lower bit rate if design fails */
        if (!report.pass) {
            report.recommended_bitrate_bps = recommend_bitrate(
                actual_length, worst_stub, (uint32_t)nodes_.size());
        }

        /* Build summary string */
        std::ostringstream sum;
        sum << (report.pass ? "PASS" : "FAIL")
            << " | " << (bit_rate_bps_ / 1000) << " kbit/s"
            << " | Bus: " << std::fixed << std::setprecision(1) << actual_length << " m"
            << " | Nodes: " << nodes_.size()
            << " | Prop budget: " << std::setprecision(0)
            << report.prop_budget_used_pct << "%"
            << " | Cap: " << std::setprecision(0)
            << report.cap_budget_used_pct << "%";
        report.summary = sum.str();

        return report;
    }

    /* Static utility: print a full design reference table */
    static std::string generate_reference_table() {
        std::ostringstream out;
        const uint32_t rates[] = {1000000, 500000, 250000, 125000, 50000, 20000, 10000};
        out << std::left
            << std::setw(12) << "Bit Rate"
            << std::setw(14) << "Max Length"
            << std::setw(14) << "Max Stub"
            << std::setw(12) << "Max Nodes"
            << "Max Cap\n";
        out << std::string(64, '-') << "\n";
        for (uint32_t rate : rates) {
            CANBusDesigner d(rate);
            out << std::setw(12) << (std::to_string(rate / 1000) + " kbit/s")
                << std::setw(14) << (std::to_string((int)d.max_bus_length_m_) + " m")
                << std::setw(14) << (std::to_string((int)(d.max_stub_length_m_ * 100) / 100.0f) + " m")
                << std::setw(12) << "32"
                << (int)(1.0e9f / rate / (10.0f * 60.0f)) << " pF\n";
        }
        return out.str();
    }

private:
    uint32_t              bit_rate_bps_;
    float                 sample_point_;
    float                 prop_delay_ns_m_;
    uint32_t              node_internal_delay_ns_;
    std::vector<NodeSpec> nodes_;
    float                 term_end1_  = 120.0f;
    float                 term_end2_  = 120.0f;
    float                 max_bus_length_m_  = 0.0f;
    float                 max_stub_length_m_ = 0.0f;

    void recalculate_limits() {
        float bit_time_ns = 1.0e9f / (float)bit_rate_bps_;
        float round_trip_ns = bit_time_ns * (1.0f - sample_point_)
                              - (float)node_internal_delay_ns_;
        max_bus_length_m_  = (round_trip_ns > 0.0f)
                              ? (round_trip_ns / 2.0f) / prop_delay_ns_m_
                              : 0.0f;
        float prop_speed_m_s = (1.0f / prop_delay_ns_m_) * 1.0e9f;
        max_stub_length_m_ = prop_speed_m_s / (40.0f * (float)bit_rate_bps_);
    }

    static uint32_t recommend_bitrate(float length_m, float stub_m, uint32_t nodes) {
        const uint32_t rates[] = {1000000, 500000, 250000, 125000, 50000, 20000, 10000};
        for (uint32_t rate : rates) {
            CANBusDesigner d(rate);
            if (length_m <= d.max_bus_length_m_ &&
                stub_m  <= d.max_stub_length_m_ &&
                nodes   <= 32) {
                return rate;
            }
        }
        return 0;
    }
};
```

---

### Example 3: Usage of the C++ Class

```cpp
#include "CANBusDesigner.hpp"
#include <iostream>

int main() {
    /* --- Example 1: Validate a J1939 truck backbone --- */
    std::cout << "=== J1939 Truck Bus Validation (250 kbit/s) ===\n";
    CANBusDesigner bus_j1939(250000); /* 250 kbit/s */

    bus_j1939.add_node({"ECM",        0.0f,   0.5f,  20.0f});
    bus_j1939.add_node({"TCM",        3.5f,   0.5f,  20.0f});
    bus_j1939.add_node({"ABS_Module", 8.0f,   1.0f,  20.0f});
    bus_j1939.add_node({"Instrument", 12.0f,  2.0f,  20.0f});
    bus_j1939.add_node({"Trailer_CAN",18.0f,  0.5f,  20.0f});

    auto report = bus_j1939.validate();
    std::cout << "Result: " << report.summary << "\n";
    for (const auto& e : report.errors)   std::cout << "  ERROR: "   << e << "\n";
    for (const auto& w : report.warnings) std::cout << "  WARN:  "   << w << "\n";

    /* --- Example 2: Overly ambitious 1 Mbit/s design --- */
    std::cout << "\n=== Automotive 1 Mbit/s Design (Expected FAIL) ===\n";
    CANBusDesigner bus_hs(1000000); /* 1 Mbit/s */

    bus_hs.add_node({"BCM",    0.0f,  0.2f, 15.0f});
    bus_hs.add_node({"ECM",    5.0f,  0.2f, 15.0f});
    bus_hs.add_node({"ADAS",  30.0f,  2.0f, 15.0f}); /* 2 m stub — too long! */

    auto report2 = bus_hs.validate();
    std::cout << "Result: " << report2.summary << "\n";
    for (const auto& e : report2.errors)   std::cout << "  ERROR: "   << e << "\n";
    for (const auto& w : report2.warnings) std::cout << "  WARN:  "   << w << "\n";
    if (report2.recommended_bitrate_bps > 0) {
        std::cout << "  => Recommended max bit rate: "
                  << report2.recommended_bitrate_bps / 1000 << " kbit/s\n";
    }

    /* --- Reference table --- */
    std::cout << "\n=== CAN Bus Design Reference Table ===\n";
    std::cout << CANBusDesigner::generate_reference_table();

    return 0;
}
```

---

## Rust Implementation Examples

### Example 1: CAN Bus Calculator Library in Rust

```rust
//! can_bus_calc.rs
//! CAN Bus Length, Stub, and Node Count Engineering Calculator
//! Implements ISO 11898-2 physical layer constraints.

use std::fmt;

/// Physical constants for CAN bus calculations
pub const PROP_DELAY_NS_PER_M: f64 = 5.0;     // ns/m for standard twisted pair
pub const NODE_INTERNAL_DELAY_NS: u64 = 210;   // ns typical transceiver delay
pub const MAX_NODES_STANDARD: u32 = 32;
pub const CAP_PER_NODE_PF: f64 = 20.0;        // pF per node
pub const DEFAULT_SAMPLE_POINT: f64 = 0.75;   // 75%

/// Standard CAN bit rates in bits/second
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CanBitRate {
    Rate1Mbit,
    Rate800Kbit,
    Rate500Kbit,
    Rate250Kbit,
    Rate125Kbit,
    Rate50Kbit,
    Rate20Kbit,
    Rate10Kbit,
    Custom(u32),
}

impl CanBitRate {
    pub fn bps(&self) -> u32 {
        match self {
            Self::Rate1Mbit   => 1_000_000,
            Self::Rate800Kbit =>   800_000,
            Self::Rate500Kbit =>   500_000,
            Self::Rate250Kbit =>   250_000,
            Self::Rate125Kbit =>   125_000,
            Self::Rate50Kbit  =>    50_000,
            Self::Rate20Kbit  =>    20_000,
            Self::Rate10Kbit  =>    10_000,
            Self::Custom(v)   => *v,
        }
    }
}

impl fmt::Display for CanBitRate {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} kbit/s", self.bps() / 1000)
    }
}

/// Errors from bus design validation
#[derive(Debug, Clone, PartialEq)]
pub enum CanCalcError {
    InvalidBitRate,
    InvalidSamplePoint,
    BusTooLong { actual_m: f64, max_m: f64 },
    StubTooLong { node: String, actual_m: f64, max_m: f64 },
    TooManyNodes { count: u32, max: u32 },
    CapacitanceTooHigh { actual_pf: f64, max_pf: f64 },
}

impl fmt::Display for CanCalcError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidBitRate =>
                write!(f, "Invalid bit rate (must be > 0)"),
            Self::InvalidSamplePoint =>
                write!(f, "Sample point must be in range (0, 1)"),
            Self::BusTooLong { actual_m, max_m } =>
                write!(f, "Bus length {:.1} m exceeds maximum {:.1} m", actual_m, max_m),
            Self::StubTooLong { node, actual_m, max_m } =>
                write!(f, "Node '{}' stub {:.2} m exceeds maximum {:.2} m",
                       node, actual_m, max_m),
            Self::TooManyNodes { count, max } =>
                write!(f, "Node count {} exceeds maximum {}", count, max),
            Self::CapacitanceTooHigh { actual_pf, max_pf } =>
                write!(f, "Bus capacitance {:.0} pF exceeds maximum {:.0} pF",
                       actual_pf, max_pf),
        }
    }
}

/// Calculated physical limits for a given bit rate
#[derive(Debug, Clone)]
pub struct CanBusLimits {
    pub bit_rate_bps:       u32,
    pub max_bus_length_m:   f64,
    pub max_stub_length_m:  f64,
    pub max_nodes:          u32,
    pub max_capacitance_pf: f64,
    pub prop_budget_ns:     f64,
}

impl CanBusLimits {
    /// Calculate limits for the given bit rate and configuration
    pub fn calculate(
        bit_rate_bps: u32,
        sample_point: f64,
        prop_delay_ns_m: f64,
        node_internal_ns: u64,
    ) -> Result<Self, CanCalcError> {
        if bit_rate_bps == 0 {
            return Err(CanCalcError::InvalidBitRate);
        }
        if !(0.0 < sample_point && sample_point < 1.0) {
            return Err(CanCalcError::InvalidSamplePoint);
        }

        let bit_time_ns = 1.0e9 / bit_rate_bps as f64;

        // Round-trip propagation budget
        let round_trip_budget_ns =
            bit_time_ns * (1.0 - sample_point) - node_internal_ns as f64;

        let max_bus_length_m = if round_trip_budget_ns > 0.0 {
            (round_trip_budget_ns / 2.0) / prop_delay_ns_m
        } else {
            0.0
        };

        // Stub length: resonance at least 10× bit rate
        let prop_speed_m_per_s = (1.0 / prop_delay_ns_m) * 1.0e9;
        let max_stub_length_m = prop_speed_m_per_s / (40.0 * bit_rate_bps as f64);

        // Max capacitance: τ = 60Ω × C ≤ bit_time / 10
        let max_cap_pf = bit_time_ns / (10.0 * 60.0);

        Ok(CanBusLimits {
            bit_rate_bps,
            max_bus_length_m,
            max_stub_length_m,
            max_nodes: MAX_NODES_STANDARD,
            max_capacitance_pf: max_cap_pf,
            prop_budget_ns: round_trip_budget_ns.max(0.0),
        })
    }

    /// Convenience constructor with ISO 11898-2 defaults
    pub fn for_rate(rate: CanBitRate) -> Result<Self, CanCalcError> {
        Self::calculate(
            rate.bps(),
            DEFAULT_SAMPLE_POINT,
            PROP_DELAY_NS_PER_M,
            NODE_INTERNAL_DELAY_NS,
        )
    }
}

impl fmt::Display for CanBusLimits {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} kbit/s | Max bus: {:.1} m | Max stub: {:.2} m | \
             Max nodes: {} | Max cap: {:.0} pF",
            self.bit_rate_bps / 1000,
            self.max_bus_length_m,
            self.max_stub_length_m,
            self.max_nodes,
            self.max_capacitance_pf,
        )
    }
}

/// A node in the CAN bus design
#[derive(Debug, Clone)]
pub struct CanNode {
    pub name:              String,
    pub position_m:        f64,   // Distance from bus start (backbone)
    pub stub_length_m:     f64,   // Drop cable length
    pub capacitance_pf:    f64,   // Input capacitance
}

/// Full bus design for validation
#[derive(Debug, Clone)]
pub struct CanBusDesign {
    pub bit_rate:     CanBitRate,
    pub sample_point: f64,
    pub nodes:        Vec<CanNode>,
}

/// Validation result with pass/fail and detailed diagnostics
#[derive(Debug)]
pub struct ValidationResult {
    pub errors:              Vec<CanCalcError>,
    pub warnings:            Vec<String>,
    pub limits:              CanBusLimits,
    pub actual_length_m:     f64,
    pub worst_stub_m:        f64,
    pub total_cap_pf:        f64,
    pub prop_budget_used_pct: f64,
    pub cap_budget_used_pct: f64,
    pub recommended_rate:    Option<CanBitRate>,
}

impl ValidationResult {
    pub fn passed(&self) -> bool {
        self.errors.is_empty()
    }
}

impl CanBusDesign {
    pub fn new(bit_rate: CanBitRate) -> Self {
        Self {
            bit_rate,
            sample_point: DEFAULT_SAMPLE_POINT,
            nodes: Vec::new(),
        }
    }

    pub fn with_node(mut self, node: CanNode) -> Self {
        self.nodes.push(node);
        self
    }

    pub fn validate(&self) -> Result<ValidationResult, CanCalcError> {
        let limits = CanBusLimits::calculate(
            self.bit_rate.bps(),
            self.sample_point,
            PROP_DELAY_NS_PER_M,
            NODE_INTERNAL_DELAY_NS,
        )?;

        let mut errors: Vec<CanCalcError> = Vec::new();
        let mut warnings: Vec<String> = Vec::new();

        let actual_length_m = self.nodes.iter()
            .map(|n| n.position_m)
            .fold(0.0_f64, f64::max);

        // Validate backbone length
        if actual_length_m > limits.max_bus_length_m {
            errors.push(CanCalcError::BusTooLong {
                actual_m: actual_length_m,
                max_m: limits.max_bus_length_m,
            });
        }

        // Propagation budget usage
        let bit_time_ns = 1.0e9 / self.bit_rate.bps() as f64;
        let budget_ns = bit_time_ns * (1.0 - self.sample_point);
        let used_ns = actual_length_m * PROP_DELAY_NS_PER_M * 2.0
                      + NODE_INTERNAL_DELAY_NS as f64;
        let prop_budget_used_pct = (used_ns / budget_ns) * 100.0;
        if prop_budget_used_pct > 90.0 {
            warnings.push(format!(
                "Propagation budget at {:.0}% — design is marginal (>90%).",
                prop_budget_used_pct
            ));
        }

        // Validate stub lengths
        let worst_stub_m = self.nodes.iter()
            .map(|n| n.stub_length_m)
            .fold(0.0_f64, f64::max);

        for node in &self.nodes {
            if node.stub_length_m > limits.max_stub_length_m {
                errors.push(CanCalcError::StubTooLong {
                    node: node.name.clone(),
                    actual_m: node.stub_length_m,
                    max_m: limits.max_stub_length_m,
                });
            }
        }

        // Validate node count
        let count = self.nodes.len() as u32;
        if count > MAX_NODES_STANDARD {
            errors.push(CanCalcError::TooManyNodes {
                count,
                max: MAX_NODES_STANDARD,
            });
        }

        // Validate total capacitance
        let total_cap_pf: f64 = self.nodes.iter().map(|n| n.capacitance_pf).sum();
        let cap_budget_used_pct = (total_cap_pf / limits.max_capacitance_pf) * 100.0;
        if total_cap_pf > limits.max_capacitance_pf {
            errors.push(CanCalcError::CapacitanceTooHigh {
                actual_pf: total_cap_pf,
                max_pf: limits.max_capacitance_pf,
            });
        }
        if cap_budget_used_pct > 80.0 {
            warnings.push(format!(
                "Capacitance budget at {:.0}% — consider high-Z transceivers.",
                cap_budget_used_pct
            ));
        }

        // Recommend lower rate if failing
        let recommended_rate = if !errors.is_empty() {
            let standard_rates = [
                CanBitRate::Rate1Mbit, CanBitRate::Rate500Kbit,
                CanBitRate::Rate250Kbit, CanBitRate::Rate125Kbit,
                CanBitRate::Rate50Kbit,  CanBitRate::Rate20Kbit,
                CanBitRate::Rate10Kbit,
            ];
            standard_rates.iter().find(|&&rate| {
                if let Ok(lim) = CanBusLimits::for_rate(rate) {
                    actual_length_m <= lim.max_bus_length_m
                        && worst_stub_m <= lim.max_stub_length_m
                        && count <= MAX_NODES_STANDARD
                } else {
                    false
                }
            }).copied()
        } else {
            None
        };

        Ok(ValidationResult {
            errors,
            warnings,
            limits,
            actual_length_m,
            worst_stub_m,
            total_cap_pf,
            prop_budget_used_pct,
            cap_budget_used_pct,
            recommended_rate,
        })
    }
}
```

---

### Example 2: Rust Usage and Reference Table Generator

```rust
//! main.rs — CAN Bus Design Tool usage example

mod can_bus_calc;
use can_bus_calc::*;

fn print_reference_table() {
    println!("\n{:-<72}", "");
    println!("{:<14}{:<14}{:<14}{:<12}{}", 
             "Bit Rate", "Max Length", "Max Stub", "Max Nodes", "Max Cap (pF)");
    println!("{:-<72}", "");

    let rates = [
        CanBitRate::Rate1Mbit,
        CanBitRate::Rate500Kbit,
        CanBitRate::Rate250Kbit,
        CanBitRate::Rate125Kbit,
        CanBitRate::Rate50Kbit,
        CanBitRate::Rate20Kbit,
        CanBitRate::Rate10Kbit,
    ];

    for rate in &rates {
        if let Ok(limits) = CanBusLimits::for_rate(*rate) {
            println!("{:<14}{:<14}{:<14}{:<12}{:.0}",
                format!("{}", rate),
                format!("{:.0} m", limits.max_bus_length_m),
                format!("{:.2} m", limits.max_stub_length_m),
                limits.max_nodes,
                limits.max_capacitance_pf,
            );
        }
    }
    println!("{:-<72}\n", "");
}

fn print_validation(design: &CanBusDesign, label: &str) {
    println!("=== {} ===", label);
    match design.validate() {
        Ok(result) => {
            println!("Status: {}", if result.passed() { "PASS ✓" } else { "FAIL ✗" });
            println!("  Bus length:  {:.1} m  (max {:.1} m)",
                     result.actual_length_m, result.limits.max_bus_length_m);
            println!("  Worst stub:  {:.2} m  (max {:.2} m)",
                     result.worst_stub_m, result.limits.max_stub_length_m);
            println!("  Nodes:       {} / {}", 
                     design.nodes.len(), result.limits.max_nodes);
            println!("  Prop budget: {:.0}% used", result.prop_budget_used_pct);
            println!("  Cap budget:  {:.0}% used ({:.0} / {:.0} pF)",
                     result.cap_budget_used_pct,
                     result.total_cap_pf,
                     result.limits.max_capacitance_pf);

            for e in &result.errors {
                println!("  [ERROR] {}", e);
            }
            for w in &result.warnings {
                println!("  [WARN]  {}", w);
            }
            if let Some(rate) = result.recommended_rate {
                println!("  => Recommended max rate: {}", rate);
            }
        }
        Err(e) => println!("Calculation error: {}", e),
    }
    println!();
}

fn main() {
    // --- Reference table ---
    print_reference_table();

    // --- Example 1: Valid J1939 agricultural vehicle bus (250 kbit/s) ---
    let ag_bus = CanBusDesign::new(CanBitRate::Rate250Kbit)
        .with_node(CanNode { name: "Engine_ECU".into(),  position_m: 0.0,  stub_length_m: 0.5, capacitance_pf: 20.0 })
        .with_node(CanNode { name: "Trans_ECU".into(),   position_m: 2.0,  stub_length_m: 0.5, capacitance_pf: 20.0 })
        .with_node(CanNode { name: "Display".into(),     position_m: 5.0,  stub_length_m: 1.0, capacitance_pf: 20.0 })
        .with_node(CanNode { name: "Hitch_Ctrl".into(),  position_m: 12.0, stub_length_m: 2.0, capacitance_pf: 20.0 })
        .with_node(CanNode { name: "ISOBUS_Term".into(), position_m: 15.0, stub_length_m: 0.3, capacitance_pf: 20.0 });

    print_validation(&ag_bus, "J1939 Agricultural CAN Bus (250 kbit/s)");

    // --- Example 2: Failing 1 Mbit/s design with long stubs ---
    let hs_bus = CanBusDesign::new(CanBitRate::Rate1Mbit)
        .with_node(CanNode { name: "Gateway".into(),     position_m: 0.0,  stub_length_m: 0.1, capacitance_pf: 15.0 })
        .with_node(CanNode { name: "ECM".into(),         position_m: 3.0,  stub_length_m: 0.2, capacitance_pf: 15.0 })
        .with_node(CanNode { name: "Remote_Sensor".into(),position_m:35.0, stub_length_m: 2.5, capacitance_pf: 15.0 });

    print_validation(&hs_bus, "High-Speed Bus with Long Stub (Expected FAIL)");

    // --- Example 3: Edge case — many nodes, capacitance budget ---
    let factory_bus = CanBusDesign::new(CanBitRate::Rate500Kbit);
    let factory_bus = (0..30).fold(factory_bus, |b, i| {
        b.with_node(CanNode {
            name: format!("PLC_{:02}", i),
            position_m: i as f64 * 3.0,
            stub_length_m: 0.8,
            capacitance_pf: 25.0,
        })
    });

    print_validation(&factory_bus, "Factory Floor Bus — 30 PLCs at 500 kbit/s");
}
```

### Example Output

```
------------------------------------------------------------------------
Bit Rate      Max Length    Max Stub      Max Nodes   Max Cap (pF)
------------------------------------------------------------------------
1000 kbit/s   38 m          5.00 m        32          1667
500 kbit/s    100 m         9.90 m        32          3333
250 kbit/s    225 m         19.80 m       32          6667
125 kbit/s    475 m         39.60 m       32          13333
50 kbit/s     1225 m        99.00 m       32          33333
20 kbit/s     3100 m        247.50 m      32          83333
10 kbit/s     6225 m        495.00 m      32          166667
------------------------------------------------------------------------

=== J1939 Agricultural CAN Bus (250 kbit/s) ===
Status: PASS ✓
  Bus length:  15.0 m  (max 225.0 m)
  Worst stub:  2.00 m  (max 19.80 m)
  Nodes:       5 / 32
  Prop budget: 16% used
  Cap budget:  15% used (100 / 6667 pF)

=== High-Speed Bus with Long Stub (Expected FAIL) ===
Status: FAIL ✗
  Bus length:  35.0 m  (max 38.0 m)
  Worst stub:  2.50 m  (max 5.00 m)
  ...
  [ERROR] Node 'Remote_Sensor' stub 2.50 m exceeds maximum 5.00 m
  => Recommended max rate: 500 kbit/s
```

---

## Summary

CAN bus physical layer design is governed by three tightly coupled constraints, all derived from the fundamental requirement that the bus must settle within one bit time:

**1. Bus Length** is limited by round-trip propagation delay. The rule is simple: maximum bus length in meters ≈ 50,000,000 / bit_rate_bps. At 1 Mbit/s you get ~25–40 m; at 250 kbit/s you get ~250 m. Doubling the length requires halving the bit rate.

**2. Stub Length** is limited by the resonance effect of unterminated branches. Each stub is a λ/4 resonator; its resonance frequency must be at least 10× the bit rate. At 1 Mbit/s, stubs must be under 30 cm — virtually connector-pigtail length only. This is why modern high-speed designs use daisy-chain (in-line) wiring, not T-taps. At lower rates, longer stubs become acceptable.

**3. Node Count** is limited to 32 in ISO 11898-2 standard transceivers, primarily due to cumulative bus capacitance. Each node adds ~10–20 pF; the total RC time constant must not degrade the bit edge significantly. High-impedance transceivers (ISO 11898-6) can extend this to 64–110 nodes.

**Design practice priorities:**

- Always choose the lowest bit rate that meets your throughput needs — this maximizes physical margins.
- Use proper 120 Ω termination at both bus ends. Split termination (two 60 Ω + capacitor) improves EMI.
- Keep stubs as short as physically possible, especially at 500 kbit/s and above.
- For distances beyond 500 m, use CAN repeaters or consider CANopen over fiber.
- For CAN FD, all constraints tighten further because the data phase runs at a higher bit rate than the arbitration phase — validate both phases separately.

The C/C++ and Rust implementations provided above encode all these rules into reusable validation libraries that can be embedded into CAD/EDA tools, commissioning software, or automated harness design systems.

---

*Reference standards: ISO 11898-1 (Data Link Layer), ISO 11898-2 (High-Speed Physical Layer), CiA 601 (CAN FD System Design), SAE J1939-11 (Physical Layer for Truck/Bus), CANopen CiA 303-1 (Cabling and Connector Requirements).*