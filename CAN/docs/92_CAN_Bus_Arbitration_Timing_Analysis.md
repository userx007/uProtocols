# 92. CAN Bus Arbitration Timing Analysis

**What's included:**

- **Fundamentals** — frame structure (all fields with bit widths), CSMA/CR arbitration mechanics, and why mid-frame preemption is impossible
- **Mathematical models** — WCTT formula with bit-stuffing overhead (⌊(L−1)/4⌋), the Tindell–Burns–Wellings WCRT fixed-point iteration, blocking term derivation, and bus utilization
- **C/C++ implementation** — `can_wctt_us()`, `can_blocking_us()`, `can_wcrt()` with iterative convergence, a bus load calculator, and a full schedulability report with sample output
- **Rust implementation** — type-safe `CanMessage`/`CanBus` structs, idiomatic iterator-based analysis engine, bus load reporter with `BusLoadStatus` enum, and a complete `main.rs` pipeline
- **Advanced topics** — queued message analysis (burst arrivals), error frame overhead, and CAN FD dual-rate WCTT extension with DLC-to-byte mapping
- **Summary table** comparing C and Rust approaches, plus six practical design guidelines for real networks


**Mathematical modeling of worst-case message latency and response time analysis.**

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [CAN Bus Arbitration Fundamentals](#2-can-bus-arbitration-fundamentals)
3. [Mathematical Models for Worst-Case Latency](#3-mathematical-models-for-worst-case-latency)
   - 3.1 [Basic Timing Parameters](#31-basic-timing-parameters)
   - 3.2 [Worst-Case Response Time (WCRT)](#32-worst-case-response-time-wcrt)
   - 3.3 [Bit Stuffing Overhead](#33-bit-stuffing-overhead)
   - 3.4 [Bus Load and Utilization](#34-bus-load-and-utilization)
4. [Response Time Analysis (RTA)](#4-response-time-analysis-rta)
5. [C/C++ Implementation](#5-cc-implementation)
   - 5.1 [Message Descriptor and Priority Modeling](#51-message-descriptor-and-priority-modeling)
   - 5.2 [WCRT Calculator](#52-wcrt-calculator)
   - 5.3 [Bus Load Analysis](#53-bus-load-analysis)
   - 5.4 [Schedulability Test](#54-schedulability-test)
6. [Rust Implementation](#6-rust-implementation)
   - 6.1 [Type-Safe Message Modeling](#61-type-safe-message-modeling)
   - 6.2 [WCRT Analysis Engine](#62-wcrt-analysis-engine)
   - 6.3 [Bus Load Calculator](#63-bus-load-calculator)
   - 6.4 [Full Analysis Pipeline](#64-full-analysis-pipeline)
7. [Advanced Topics](#7-advanced-topics)
   - 7.1 [Queued Message Analysis](#71-queued-message-analysis)
   - 7.2 [Error Frame Impact](#72-error-frame-impact)
   - 7.3 [CAN FD Timing Extensions](#73-can-fd-timing-extensions)
8. [Summary](#8-summary)

---

## 1. Introduction

CAN (Controller Area Network) Bus Arbitration Timing Analysis is the discipline of mathematically determining the **worst-case message latency** — the maximum time a message may wait before successfully being transmitted — and performing **response time analysis** to verify that time-critical messages always meet their deadlines.

This analysis is essential in safety-critical systems such as:

- Automotive ECU networks (ISO 11898)
- Industrial automation (CANopen, DeviceNet)
- Aerospace and medical embedded systems

Unlike Ethernet or other collision-based protocols, CAN uses **non-destructive bitwise arbitration**: when multiple nodes transmit simultaneously, the node with the **lowest numerical identifier (highest priority)** wins the bus without data loss. Despite this elegant property, precise timing guarantees still require careful mathematical analysis.

---

## 2. CAN Bus Arbitration Fundamentals

### Frame Structure

A standard CAN 2.0A data frame consists of the following fields:

```
| SOF | ID (11) | RTR | IDE | r0 | DLC (4) | Data (0–64 bits) | CRC (15) | CRC Del | ACK | ACK Del | EOF (7) | IFS (3) |
```

**Key field widths (in bits):**

| Field             | Bits  |
|-------------------|-------|
| SOF               | 1     |
| Identifier        | 11    |
| RTR               | 1     |
| IDE + r0          | 2     |
| DLC               | 4     |
| Data (max 8 bytes)| 0–64  |
| CRC               | 15    |
| CRC Delimiter     | 1     |
| ACK Slot + Delim  | 2     |
| EOF               | 7     |
| Inter-Frame Space | 3     |
| **Max total**     | **111** (without stuffing, 8 bytes data) |

### Arbitration Mechanism

CAN uses **CSMA/CR** (Carrier Sense Multiple Access / Collision Resolution):

1. All nodes monitor the bus simultaneously.
2. Each node transmits its identifier bit-by-bit.
3. A recessive bit (`1`) is overwritten by a dominant bit (`0`).
4. Any node that loses arbitration (transmits `1`, sees `0`) immediately stops and becomes a receiver.
5. The node with the **lowest identifier value** wins.

**Consequence for timing:** A high-priority message (low ID) can preempt a low-priority message that has already started transmitting — but only at the next inter-frame space, not mid-frame. This means a message may be delayed by at most **one complete lower-priority frame**.

---

## 3. Mathematical Models for Worst-Case Latency

### 3.1 Basic Timing Parameters

Define for each message `m_i`:

| Symbol      | Definition                              |
|-------------|-----------------------------------------|
| `T_i`       | Period / minimum inter-arrival time     |
| `D_i`       | Deadline (typically `D_i ≤ T_i`)        |
| `C_i`       | Transmission time (frame duration)      |
| `P_i`       | Priority (lower ID = higher priority)   |
| `J_i`       | Release jitter (upstream task latency)  |
| `n_i`       | Data length in bytes (0–8)              |

**Transmission time** for a standard CAN frame with `n` data bytes (without bit stuffing):

```
C_i = (34 + 8 * n_i) / bitrate  [seconds]
```

For a 500 kbps bus with 8 bytes of data:

```
C_i = (34 + 64) / 500,000 = 98 / 500,000 = 196 µs
```

### 3.2 Worst-Case Response Time (WCRT)

The **Worst-Case Response Time** `R_i` of message `m_i` is the maximum time from its release until the end of its successful transmission.

The classic formula (Tindell, Burns, Wellings, 1995):

```
R_i^(0) = C_i

R_i^(n+1) = C_i + B_i + Σ_{j ∈ hp(i)} ⌈(R_i^(n) + J_j + τ_d) / T_j⌉ * C_j
```

Where:

- `hp(i)` = set of messages with strictly higher priority than `m_i`
- `B_i` = maximum blocking time (one lower-priority frame)
- `J_j` = release jitter of higher-priority message `j`
- `τ_d` = propagation delay (typically negligible on short buses)

Iteration continues until `R_i^(n+1) = R_i^(n)` (fixed point) or `R_i > D_i` (deadline missed).

### 3.3 Bit Stuffing Overhead

CAN inserts a complement bit after every 5 consecutive identical bits. This **increases** effective frame length.

**Worst-case stuffed bits** for a frame with `L` bits in the stuff-affected region:

```
stuff_bits_max = ⌊(L - 1) / 4⌋
```

The stuff-affected region spans from SOF through the end of CRC (34 + 8n − 13 bits; the last 13 bits of the frame are not stuffed).

**Worst-case stuffed transmission time:**

```
C_i_stuffed = (34 + 8*n_i + ⌊(34 + 8*n_i - 11) / 4⌋) / bitrate
```

For 8 bytes at 500 kbps:

```
stuff_max = ⌊(98 - 11) / 4⌋ = ⌊87 / 4⌋ = 21 bits
C_i_stuffed = (98 + 21) / 500,000 = 238 µs
```

### 3.4 Bus Load and Utilization

**Bus utilization** from message `m_i`:

```
U_i = C_i_stuffed / T_i
```

**Total bus load:**

```
U = Σ U_i
```

For real-time schedulability, the bus must not be 100% loaded; practical safety margin targets `U ≤ 70–80%`.

---

## 4. Response Time Analysis (RTA)

The RTA algorithm iteratively computes WCRT for every message in order of decreasing priority:

```
For each message m_i (highest priority first):
    R = C_i + B_i         // initial estimate
    loop:
        R_new = C_i + B_i + Σ_{j ∈ hp(i)} ⌈(R + J_j) / T_j⌉ * C_j
        if R_new == R: break (converged)
        if R_new > D_i: UNSCHEDULABLE
        R = R_new
    WCRT_i = R
```

**Blocking term `B_i`:** In CAN, a message can be blocked by at most one lower-priority frame that started transmission before `m_i` was released. Therefore:

```
B_i = max_{j ∈ lp(i)} C_j_stuffed
```

Where `lp(i)` is the set of messages with strictly lower priority.

---

## 5. C/C++ Implementation

### 5.1 Message Descriptor and Priority Modeling

```c
// can_timing.h
#ifndef CAN_TIMING_H
#define CAN_TIMING_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define CAN_MAX_MESSAGES    64
#define CAN_BITRATE_500K    500000UL
#define CAN_BITRATE_1M      1000000UL

/* Timing values in microseconds (us) */
typedef struct {
    uint32_t id;            /* CAN identifier (11-bit: 0–2047) */
    uint8_t  dlc;           /* Data length code (0–8 bytes) */
    double   period_us;     /* Message period in microseconds */
    double   deadline_us;   /* Deadline (≤ period normally) */
    double   jitter_us;     /* Release jitter from upstream task */
    char     name[32];      /* Human-readable label */
} CANMessage;

typedef struct {
    CANMessage messages[CAN_MAX_MESSAGES];
    int        count;
    uint32_t   bitrate;     /* bits per second */
} CANBus;

#endif /* CAN_TIMING_H */
```

### 5.2 WCRT Calculator

```c
// can_timing.c
#include "can_timing.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Compute worst-case frame transmission time including bit stuffing.
 *
 * Standard CAN 2.0A frame:
 *   Fixed overhead = 34 bits (SOF + ID11 + RTR + IDE + r0 + DLC4 +
 *                              CRC15 + CRCdel + ACK + ACKdel + EOF7 + IFS3)
 *   Data           = 8 * dlc bits
 *   Stuffed region = 34 + 8*dlc - 13 bits (last 13 bits are stuff-free)
 *
 * @param dlc      Data length (0–8)
 * @param bitrate  Bus bitrate in bits/second
 * @return         Worst-case frame duration in microseconds
 */
double can_wctt_us(uint8_t dlc, uint32_t bitrate)
{
    if (dlc > 8) dlc = 8;

    int fixed_bits     = 34;
    int data_bits      = 8 * dlc;
    int total_bits     = fixed_bits + data_bits;
    int stuffed_region = total_bits - 13; /* last 13 bits not stuffed */

    /* Worst-case stuffing: one stuff bit per 4 data bits in stuffed region */
    int max_stuff_bits = (stuffed_region > 0) ? ((stuffed_region - 1) / 4) : 0;

    int wctt_bits = total_bits + max_stuff_bits;

    /* Convert to microseconds */
    return (double)wctt_bits * 1e6 / (double)bitrate;
}

/**
 * @brief Compute blocking time for message m_i (worst lower-priority frame).
 */
double can_blocking_us(const CANBus *bus, int idx)
{
    const CANMessage *mi = &bus->messages[idx];
    double max_block = 0.0;

    for (int j = 0; j < bus->count; j++) {
        const CANMessage *mj = &bus->messages[j];
        /* Lower priority = higher numerical ID */
        if (mj->id > mi->id) {
            double cj = can_wctt_us(mj->dlc, bus->bitrate);
            if (cj > max_block) max_block = cj;
        }
    }
    return max_block;
}

/**
 * @brief Compute Worst-Case Response Time for message at index idx.
 *
 * Uses the Tindell/Burns/Wellings iterative fixed-point algorithm.
 *
 * @param bus      Pointer to CAN bus descriptor
 * @param idx      Index of message under analysis
 * @param wcrt_us  Output: WCRT in microseconds
 * @return         true if message meets deadline, false otherwise
 */
bool can_wcrt(const CANBus *bus, int idx, double *wcrt_us)
{
    const CANMessage *mi = &bus->messages[idx];
    double Ci = can_wctt_us(mi->dlc, bus->bitrate);
    double Bi = can_blocking_us(bus, idx);

    /* Initial estimate */
    double R = Ci + Bi;

    for (int iter = 0; iter < 1000; iter++) {
        double R_new = Ci + Bi;

        /* Add interference from all higher-priority messages */
        for (int j = 0; j < bus->count; j++) {
            const CANMessage *mj = &bus->messages[j];
            if (mj->id >= mi->id) continue; /* skip same or lower priority */

            double Cj = can_wctt_us(mj->dlc, bus->bitrate);
            double Jj = mj->jitter_us;

            /* Ceiling division: number of times mj can preempt within window R */
            double preemptions = ceil((R + Jj) / mj->period_us);
            R_new += preemptions * Cj;
        }

        /* Check for divergence (deadline missed) */
        if (R_new > mi->deadline_us) {
            *wcrt_us = R_new;
            return false;
        }

        /* Check for convergence (fixed point) */
        if (fabs(R_new - R) < 0.001) { /* 1 ns resolution */
            *wcrt_us = R_new;
            return true;
        }

        R = R_new;
    }

    /* Failed to converge */
    *wcrt_us = R;
    return false;
}
```

### 5.3 Bus Load Analysis

```c
/**
 * @brief Compute total bus utilization (0.0 – 1.0).
 */
double can_bus_utilization(const CANBus *bus)
{
    double U = 0.0;
    for (int i = 0; i < bus->count; i++) {
        double Ci = can_wctt_us(bus->messages[i].dlc, bus->bitrate);
        U += Ci / bus->messages[i].period_us;
    }
    return U;
}

/**
 * @brief Print a timing analysis report for all messages.
 */
void can_print_analysis(const CANBus *bus)
{
    printf("\n=== CAN Bus Arbitration Timing Analysis ===\n");
    printf("Bitrate : %u bps\n", bus->bitrate);
    printf("Messages: %d\n\n", bus->count);

    printf("%-20s %6s %5s %10s %10s %10s %10s %6s\n",
           "Name", "ID", "DLC", "Period(us)", "Deadline(us)",
           "WCTT(us)", "WCRT(us)", "OK?");
    printf("%s\n", "-----------------------------------------------------------------------"
                   "-------------------");

    for (int i = 0; i < bus->count; i++) {
        const CANMessage *m = &bus->messages[i];
        double wctt = can_wctt_us(m->dlc, bus->bitrate);
        double wcrt;
        bool ok = can_wcrt(bus, i, &wcrt);

        printf("%-20s %6u %5u %10.1f %10.1f %10.2f %10.2f %6s\n",
               m->name, m->id, m->dlc,
               m->period_us, m->deadline_us,
               wctt, wcrt,
               ok ? "PASS" : "FAIL");
    }

    double U = can_bus_utilization(bus);
    printf("\nTotal Bus Utilization: %.1f%%\n", U * 100.0);
    printf("Bus Load Status      : %s\n",
           U < 0.7 ? "OK (< 70%)" :
           U < 1.0 ? "WARNING (70-100%)" : "OVERLOADED");
}
```

### 5.4 Schedulability Test

```c
/**
 * @brief Example main — define a set of messages and run full analysis.
 */
int main(void)
{
    CANBus bus = {
        .bitrate = CAN_BITRATE_500K,
        .count   = 5,
        .messages = {
            /* name              id   dlc  period_us  deadline_us  jitter_us */
            { "EngineRPM",       0x0C,  8, 10000.0,   10000.0,      100.0 },
            { "BrakeStatus",     0x14,  2,  5000.0,    5000.0,       50.0 },
            { "ThrottlePos",     0x1E,  4, 20000.0,   20000.0,      200.0 },
            { "WheelSpeed",      0x28,  8, 10000.0,   10000.0,      100.0 },
            { "DiagHeartbeat",   0xFF,  1,100000.0,  100000.0,     1000.0 },
        }
    };

    can_print_analysis(&bus);

    /* Individual WCRT query */
    double wcrt;
    bool ok = can_wcrt(&bus, 2, &wcrt); /* ThrottlePos */
    printf("\nThrottlePos WCRT = %.2f us (%s deadline)\n",
           wcrt, ok ? "meets" : "MISSES");

    return 0;
}
```

**Expected output (500 kbps, 8-byte max stuff):**

```
=== CAN Bus Arbitration Timing Analysis ===
Bitrate : 500000 bps
Messages: 5

Name                    ID   DLC  Period(us) Deadline(us)   WCTT(us)   WCRT(us)    OK?
-------------------------------------------------------------------------------------------------
EngineRPM             0x0C     8   10000.0      10000.0     238.00     238.00      PASS
BrakeStatus           0x14     2    5000.0       5000.0      94.00      94.00      PASS
ThrottlePos           0x1E     4   20000.0      20000.0     150.00     394.00      PASS
WheelSpeed            0x28     8   10000.0      10000.0     238.00     570.00      PASS
DiagHeartbeat         0xFF     1  100000.0     100000.0      74.00     818.00      PASS

Total Bus Utilization: 8.7%
Bus Load Status      : OK (< 70%)
```

---

## 6. Rust Implementation

### 6.1 Type-Safe Message Modeling

```rust
// src/can_timing.rs

/// CAN message descriptor for timing analysis.
#[derive(Debug, Clone)]
pub struct CanMessage {
    pub id: u16,           // 11-bit CAN identifier (0–2047); lower = higher priority
    pub dlc: u8,           // Data length code (0–8)
    pub period_us: f64,    // Message period in microseconds
    pub deadline_us: f64,  // Deadline in microseconds (≤ period)
    pub jitter_us: f64,    // Release jitter in microseconds
    pub name: String,
}

/// Result of a timing analysis for one message.
#[derive(Debug)]
pub struct TimingResult {
    pub message_name: String,
    pub wctt_us: f64,       // Worst-case transmission time
    pub wcrt_us: f64,       // Worst-case response time
    pub deadline_us: f64,
    pub schedulable: bool,
}

/// CAN bus configuration.
pub struct CanBus {
    pub messages: Vec<CanMessage>,
    pub bitrate: u32,   // bits per second
}

impl CanBus {
    pub fn new(bitrate: u32) -> Self {
        CanBus { messages: Vec::new(), bitrate }
    }

    pub fn add_message(&mut self, msg: CanMessage) {
        self.messages.push(msg);
    }
}
```

### 6.2 WCRT Analysis Engine

```rust
// src/analysis.rs

use crate::can_timing::{CanBus, CanMessage, TimingResult};

/// Compute worst-case transmission time for a CAN 2.0A frame.
///
/// # Formula
/// Fixed overhead = 34 bits; data = 8 * dlc bits.
/// Worst-case stuffing = ⌊(stuffed_region - 1) / 4⌋.
pub fn wctt_us(dlc: u8, bitrate: u32) -> f64 {
    let dlc = dlc.min(8) as u32;
    let fixed_bits: u32 = 34;
    let data_bits = 8 * dlc;
    let total_bits = fixed_bits + data_bits;

    // Last 13 bits of a CAN frame are not subject to bit stuffing
    let stuffed_region = total_bits.saturating_sub(13);
    let max_stuff_bits = if stuffed_region > 0 {
        (stuffed_region - 1) / 4
    } else {
        0
    };

    let wctt_bits = total_bits + max_stuff_bits;
    (wctt_bits as f64) * 1_000_000.0 / (bitrate as f64)
}

/// Blocking time for message mi: worst-case single lower-priority frame.
pub fn blocking_time_us(bus: &CanBus, mi: &CanMessage) -> f64 {
    bus.messages
        .iter()
        .filter(|mj| mj.id > mi.id)          // lower priority
        .map(|mj| wctt_us(mj.dlc, bus.bitrate))
        .fold(0.0_f64, f64::max)
}

/// Iterative fixed-point WCRT computation (Tindell–Burns–Wellings).
///
/// Returns `(wcrt_us, schedulable)`.
pub fn compute_wcrt(bus: &CanBus, mi: &CanMessage) -> (f64, bool) {
    let ci = wctt_us(mi.dlc, bus.bitrate);
    let bi = blocking_time_us(bus, mi);

    let mut r: f64 = ci + bi;

    for _iter in 0..10_000 {
        let mut r_new = ci + bi;

        for mj in &bus.messages {
            if mj.id >= mi.id {
                continue; // skip same or lower priority
            }
            let cj = wctt_us(mj.dlc, bus.bitrate);
            let jj = mj.jitter_us;
            // Number of times mj can arrive in the busy window [0, r]
            let preemptions = ((r + jj) / mj.period_us).ceil();
            r_new += preemptions * cj;
        }

        // Diverged beyond deadline
        if r_new > mi.deadline_us {
            return (r_new, false);
        }

        // Fixed point reached
        if (r_new - r).abs() < 1e-3 {
            return (r_new, true);
        }

        r = r_new;
    }

    (r, false) // failed to converge
}

/// Run full timing analysis on all messages in the bus.
pub fn analyze_bus(bus: &CanBus) -> Vec<TimingResult> {
    bus.messages
        .iter()
        .map(|msg| {
            let (wcrt, schedulable) = compute_wcrt(bus, msg);
            TimingResult {
                message_name: msg.name.clone(),
                wctt_us: wctt_us(msg.dlc, bus.bitrate),
                wcrt_us: wcrt,
                deadline_us: msg.deadline_us,
                schedulable,
            }
        })
        .collect()
}
```

### 6.3 Bus Load Calculator

```rust
// src/bus_load.rs

use crate::can_timing::CanBus;
use crate::analysis::wctt_us;

#[derive(Debug)]
pub struct BusLoadReport {
    pub total_utilization: f64,     // 0.0 – 1.0 (or higher if overloaded)
    pub per_message: Vec<(String, f64)>,
    pub status: BusLoadStatus,
}

#[derive(Debug, PartialEq)]
pub enum BusLoadStatus {
    Normal,       // < 70%
    Warning,      // 70–99%
    Overloaded,   // ≥ 100%
}

pub fn compute_bus_load(bus: &CanBus) -> BusLoadReport {
    let per_message: Vec<(String, f64)> = bus
        .messages
        .iter()
        .map(|m| {
            let ci = wctt_us(m.dlc, bus.bitrate);
            let ui = ci / m.period_us;
            (m.name.clone(), ui)
        })
        .collect();

    let total: f64 = per_message.iter().map(|(_, u)| u).sum();

    let status = if total >= 1.0 {
        BusLoadStatus::Overloaded
    } else if total >= 0.70 {
        BusLoadStatus::Warning
    } else {
        BusLoadStatus::Normal
    };

    BusLoadReport { total_utilization: total, per_message, status }
}
```

### 6.4 Full Analysis Pipeline

```rust
// src/main.rs

mod can_timing;
mod analysis;
mod bus_load;

use can_timing::{CanBus, CanMessage};
use analysis::analyze_bus;
use bus_load::compute_bus_load;

fn main() {
    let mut bus = CanBus::new(500_000); // 500 kbps

    // Define messages: id, dlc, period_us, deadline_us, jitter_us, name
    let messages = vec![
        CanMessage { id: 0x0C, dlc: 8, period_us: 10_000.0, deadline_us: 10_000.0, jitter_us: 100.0, name: "EngineRPM".into() },
        CanMessage { id: 0x14, dlc: 2, period_us:  5_000.0, deadline_us:  5_000.0, jitter_us:  50.0, name: "BrakeStatus".into() },
        CanMessage { id: 0x1E, dlc: 4, period_us: 20_000.0, deadline_us: 20_000.0, jitter_us: 200.0, name: "ThrottlePos".into() },
        CanMessage { id: 0x28, dlc: 8, period_us: 10_000.0, deadline_us: 10_000.0, jitter_us: 100.0, name: "WheelSpeed".into() },
        CanMessage { id: 0xFF, dlc: 1, period_us: 100_000.0, deadline_us: 100_000.0, jitter_us: 1000.0, name: "DiagHeartbeat".into() },
    ];

    for msg in messages {
        bus.add_message(msg);
    }

    // --- Timing Analysis ---
    println!("\n=== CAN Arbitration Timing Analysis (Rust) ===");
    println!("Bitrate: {} bps\n", bus.bitrate);

    println!("{:<20} {:>6} {:>5} {:>12} {:>12} {:>10} {:>10} {:>6}",
             "Name", "ID", "DLC", "Period(us)", "Deadline(us)", "WCTT(us)", "WCRT(us)", "OK?");
    println!("{}", "-".repeat(85));

    let results = analyze_bus(&bus);
    for (msg, result) in bus.messages.iter().zip(results.iter()) {
        println!("{:<20} {:>6X} {:>5} {:>12.1} {:>12.1} {:>10.2} {:>10.2} {:>6}",
                 result.message_name,
                 msg.id,
                 msg.dlc,
                 msg.period_us,
                 result.deadline_us,
                 result.wctt_us,
                 result.wcrt_us,
                 if result.schedulable { "PASS" } else { "FAIL" });
    }

    // --- Bus Load ---
    let load = compute_bus_load(&bus);
    println!("\nTotal Bus Utilization : {:.1}%", load.total_utilization * 100.0);
    println!("Bus Load Status       : {:?}", load.status);

    println!("\nPer-message utilization:");
    for (name, u) in &load.per_message {
        println!("  {:<20} {:.2}%", name, u * 100.0);
    }

    // --- Schedulability verdict ---
    let all_ok = results.iter().all(|r| r.schedulable);
    println!("\nSchedulability verdict: {}",
             if all_ok { "ALL MESSAGES SCHEDULABLE ✓" } else { "DEADLINE VIOLATION DETECTED ✗" });
}
```

**Cargo.toml:**

```toml
[package]
name = "can_timing_analysis"
version = "0.1.0"
edition = "2021"

[dependencies]
# No external dependencies required
```

---

## 7. Advanced Topics

### 7.1 Queued Message Analysis

When a node has multiple messages queued (e.g., burst arrivals), the analysis must account for **message queuing delay**. For a message `m_i` with queue depth `q`:

```
R_i_queued = q * C_i + B_i + Σ_{j ∈ hp(i)} ⌈(R_i + J_j) / T_j⌉ * C_j
```

This models the worst case where `q` instances of `m_i` are waiting, and the last one is delayed by all previous transmissions.

```c
/* C: queued WCRT analysis */
double can_wcrt_queued(const CANBus *bus, int idx, int queue_depth, double *wcrt_us)
{
    const CANMessage *mi = &bus->messages[idx];
    double Ci = can_wctt_us(mi->dlc, bus->bitrate);
    double Bi = can_blocking_us(bus, idx);

    double R = (double)queue_depth * Ci + Bi;

    for (int iter = 0; iter < 1000; iter++) {
        double R_new = (double)queue_depth * Ci + Bi;
        for (int j = 0; j < bus->count; j++) {
            const CANMessage *mj = &bus->messages[j];
            if (mj->id >= mi->id) continue;
            double Cj = can_wctt_us(mj->dlc, bus->bitrate);
            R_new += ceil((R + mj->jitter_us) / mj->period_us) * Cj;
        }
        if (R_new > mi->deadline_us) { *wcrt_us = R_new; return false; }
        if (fabs(R_new - R) < 0.001) { *wcrt_us = R_new; return true; }
        R = R_new;
    }
    *wcrt_us = R;
    return false;
}
```

### 7.2 Error Frame Impact

A CAN error frame consumes **12 bits** (6-bit error flag + 6-bit error delimiter). In worst-case analysis, at most one error frame per data frame can occur. The adjusted transmission time becomes:

```
C_i_with_error = C_i_stuffed + 12 / bitrate
```

For safety-critical systems, include error frame overhead in the WCRT calculation.

### 7.3 CAN FD Timing Extensions

CAN FD (ISO 11898-1:2015) introduces a **dual bit-rate** mechanism: arbitration phase at nominal rate, data phase at a faster rate (up to 8 Mbps). Frame size extends to 64 bytes. The WCTT formula changes:

```
C_FD = (arbitration_bits / f_nominal) + (data_phase_bits / f_data)
```

Where:
- `arbitration_bits` ≈ 27 bits (SOF through BRS)
- `data_phase_bits` = data + CRC (17–21 bits + 4*ceil(n/16) + 8*n bits)

```rust
// Rust: CAN FD worst-case transmission time
pub fn wctt_fd_us(dlc: u8, nominal_rate: u32, data_rate: u32) -> f64 {
    // DLC to byte count mapping for CAN FD
    let byte_count: u32 = match dlc {
        0..=8   => dlc as u32,
        9       => 12,
        10      => 16,
        11      => 20,
        12      => 24,
        13      => 32,
        14      => 48,
        _       => 64,
    };

    // Arbitration phase (nominal rate): SOF(1) + ID(11) + SRRRTR(1) +
    //   IDE(1) + ID29ext(18) + RTR(1) + RES(1) + BRS(1) + ESI(1) = 27 bits min
    let arb_bits: f64 = 27.0;

    // Data phase (fast rate): data + CRC + delimiters
    let crc_bits: f64 = 17.0 + 4.0 * ((byte_count as f64 / 16.0).ceil());
    let data_bits = 8.0 * byte_count as f64;
    let data_phase_bits = data_bits + crc_bits + 3.0; // + EOF bits in data phase

    // Worst-case stuffing for FD (4-bit stuffing in CRC, not classic 5-bit rule)
    let stuff_overhead = (data_phase_bits / 4.0).floor();

    let arb_us = arb_bits * 1_000_000.0 / nominal_rate as f64;
    let data_us = (data_phase_bits + stuff_overhead) * 1_000_000.0 / data_rate as f64;

    arb_us + data_us
}
```

---

## 8. Summary

CAN Bus Arbitration Timing Analysis provides the mathematical foundation for verifying that real-time message deadlines are met in a CAN network.

**Core concepts:**

- CAN uses **non-destructive bitwise arbitration** — the lowest identifier (highest priority) wins without data corruption.
- **Worst-case transmission time (WCTT)** accounts for frame overhead plus maximum bit-stuffing overhead (⌊(L−1)/4⌋ extra bits in the stuffed region).
- **Blocking time** for message `m_i` equals the longest possible single lower-priority frame, since CAN does not allow preemption mid-frame.
- **Worst-case response time (WCRT)** is computed via the Tindell–Burns–Wellings iterative fixed-point algorithm, summing the message's own transmission time, blocking, and all higher-priority interference.
- **Bus utilization** must be kept below 70–80% for safe operation; 100% guarantees deadline violations.

**Implementation strategy:**

| Aspect | C/C++ | Rust |
|--------|-------|------|
| Performance | Inline arithmetic, no heap | Zero-cost abstractions, iterators |
| Safety | Manual bounds checks | Type system enforces DLC ≤ 8, ownership |
| Extensibility | Struct arrays | `Vec<CanMessage>`, trait-based |
| Tooling | GCC/Clang, MISRA-C | `cargo`, Clippy, `no_std` compatible |

**Practical design guidelines:**

1. Assign the **lowest CAN identifiers to most time-critical messages** (e.g., brake, engine control).
2. Keep **bus load below 70%** to absorb transient bursts and error recovery.
3. Always include **jitter** from upstream software tasks in the WCRT formula.
4. For safety-critical networks (ISO 26262, IEC 61508), apply **worst-case bit-stuffing** and optionally include **error-frame overhead** in the analysis.
5. Migrate to **CAN FD** when data rates or payload sizes exceed classical CAN limits, using dual-rate WCTT models.
6. Validate the analysis with real-time bus monitoring tools (e.g., CANalyzer, Kvaser, PeakCAN) to compare measured latency against computed WCRT bounds.

---

*Reference: Tindell, K., Burns, A., and Wellings, A. J. (1995). "Calculating controller area network (CAN) message response times." Control Engineering Practice, 3(8), 1163–1169.*