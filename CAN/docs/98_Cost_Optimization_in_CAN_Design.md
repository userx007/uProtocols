# 98. Cost Optimization in CAN Design

**Content Structure:**
- **Component Selection** — MCU/transceiver trade-offs, integrated CAN controllers vs. external chips, partial networking transceivers, and harness cost considerations
- **Bandwidth Optimization** — bus load budgeting (≤65–70% target), signal packing strategies, adaptive rate control, and CAN-FD as a bus consolidation tool
- **System Partitioning** — topology decisions, safety domain isolation, gateway cost vs. bus count trade-offs
- **Message Scheduling** — priority assignment and WCRT (Worst-Case Response Time) analysis

**C/C++ Examples:**
1. **Signal packing** with rolling counter + XOR checksum integrity (zero-overhead for low-cost MCUs without CRC hardware)
2. **Bus load monitor** with adaptive transmission rate scaling
3. **WCRT analysis tool** — iterative fixed-point scheduler validation
4. **Partial networking wake-up frame generator** (ISO 11898-6 compatible)

**Rust Examples:**
1. **Zero-copy frame builder** — `#![no_std]` compatible, with strongly-typed signal encoding
2. **WCRT scheduler** — idiomatic Rust with `Option<u32>` for unschedulable detection
3. **Compile-time node configuration table** — `const` struct array eliminating runtime lookups and RAM overhead

**Summary table** maps each technique to its concrete cost reduction mechanism and typical impact.

# 98. Cost Optimization in CAN Design

> Balancing component selection, bandwidth requirements, and system partitioning for cost-effective designs.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Component Selection Strategies](#component-selection-strategies)
3. [Bandwidth Optimization](#bandwidth-optimization)
4. [System Partitioning](#system-partitioning)
5. [Message Scheduling and Priority Tuning](#message-scheduling-and-priority-tuning)
6. [Code Examples in C/C++](#code-examples-in-cc)
7. [Code Examples in Rust](#code-examples-in-rust)
8. [Summary](#summary)

---

## Introduction

Controller Area Network (CAN) has been the backbone of automotive and industrial embedded systems for decades. As systems grow in complexity—adding more ECUs, sensors, and actuators—cost pressures demand careful architectural decisions at every layer: hardware selection, bus topology, message design, and software implementation.

Cost optimization in CAN design is not simply about choosing the cheapest components. It is about achieving the required functionality with the minimum total system cost while maintaining reliability, timing guarantees, and maintainability. This involves trade-offs across several dimensions:

- **Hardware BOM cost** — transceivers, microcontrollers, connectors, wiring harness
- **Bandwidth efficiency** — minimizing wasted bus capacity without sacrificing latency
- **System partitioning** — deciding which ECUs share a bus and which are isolated
- **Software complexity** — reducing development and validation effort
- **Scalability** — designing for future expansion without expensive redesigns

The sections below examine each dimension, with practical code examples in C/C++ and Rust.

---

## Component Selection Strategies

### Microcontroller (MCU) Selection

The MCU is typically the largest single-component cost. Key considerations:

| Factor | Impact on Cost |
|---|---|
| Number of integrated CAN controllers | Eliminates external CAN controllers (~$0.50–$2.00 savings per node) |
| CAN-FD vs. Classic CAN support | CAN-FD MCUs cost more but reduce node count by increasing throughput |
| Flash/RAM sizing | Oversizing wastes money; undersizing forces expensive redesigns |
| Operating temperature range | Industrial/automotive grades cost 2–5× more than commercial |
| Package type | QFP/QFN vs. BGA affects PCB cost and assembly yield |

**Best practices:**

- Use MCUs with **integrated CAN controllers** (e.g., NXP S32K, STM32 series, Infineon XMC, Microchip SAME5x). External CAN controllers (MCP2515) are only justified on very low-cost MCUs.
- Match flash/RAM to application: profile your stack, OS, and application code, then add 20–30% headroom.
- For high-volume production (>10,000 units), negotiate pricing tiers; the difference between 5k and 50k pricing can exceed 40%.

### Transceiver Selection

CAN transceivers are commodity components, but selection still matters:

- **Standard CAN transceivers** (TJA1050, MCP2551, SN65HVD230): $0.20–$0.60, suitable for most designs.
- **Automotive-grade transceivers** with partial networking (TJA1145, NCV7356): more expensive ($1.50–$3.00) but enable selective wake-up, drastically reducing quiescent current and eliminating the need for separate wake-up circuits.
- **CAN-FD transceivers** (TJA1044, TCAN1044): marginal cost premium (~$0.10–$0.30) over Classic CAN, with significant bandwidth gains.

**Partial Networking (ISO 11898-6)** allows transceivers to wake a specific ECU on receipt of a defined CAN frame, rather than waking the entire network. This reduces system-level power management hardware costs.

### Wiring Harness Costs

The wiring harness is often the most overlooked cost in CAN system design, yet it can represent 5–15% of total vehicle electrical system cost.

- **Shorter stub lengths** reduce reflections and allow operation at higher bit rates, enabling fewer nodes per bus.
- **Ring or star topologies** cost more in wire but may reduce the number of required buses.
- **Reduce node count** by consolidating functions into fewer ECUs — eliminating a $5 ECU may save $15 in harness.

---

## Bandwidth Optimization

### CAN Bus Utilization Budget

A CAN bus at 500 kbit/s with Classic CAN frames has an approximate maximum throughput of ~7,000 frames/second (for 8-byte frames including overhead). In practice, designers target **≤ 60–70% bus load** to allow for error recovery, retransmissions, and future growth.

Exceeding this leads to:
- Increased worst-case latency for high-priority messages
- Cascading retransmission storms under error conditions
- Difficulty in meeting deadline requirements (AUTOSAR ComStack timing)

### Message Packing

Packing multiple signals into fewer frames is one of the highest-leverage optimizations:

**Inefficient design:**
```
Frame 0x100: Engine Speed       (2 bytes payload, 6 bytes wasted)
Frame 0x101: Throttle Position  (1 byte payload, 7 bytes wasted)
Frame 0x102: Coolant Temp       (1 byte payload, 7 bytes wasted)
```
Bus consumption: 3 frames × ~130-bit overhead = ~390 bits per cycle

**Optimized design:**
```
Frame 0x100: Engine Speed | Throttle Pos | Coolant Temp (4 bytes payload, 4 bytes spare)
```
Bus consumption: 1 frame × ~130 bits = ~130 bits per cycle → **67% reduction**

### Transmission Rate Tuning

Not all signals require the same transmission rate. A well-designed system uses **event-triggered** messages for fast-changing signals and **time-triggered** messages with low rates for slow-changing data.

| Signal Type | Example | Recommended Rate |
|---|---|---|
| Safety-critical control | ABS active, airbag trigger | Event-triggered + 10 ms periodic |
| Powertrain control | Engine torque demand | 10–20 ms |
| Driver information | Vehicle speed | 20–100 ms |
| Environmental | Ambient temperature | 1000–5000 ms |
| Diagnostics | DTC status | On-request only |

### CAN-FD as a Cost Reducer

Migrating from Classic CAN (up to 1 Mbit/s, 8 bytes) to CAN-FD (up to 8 Mbit/s, 64 bytes) can reduce the number of required buses in a system:

- A single CAN-FD bus at 2 Mbit/s data phase provides roughly **10× the payload throughput** of a Classic CAN bus at 500 kbit/s.
- Consolidating two Classic CAN buses into one CAN-FD bus saves: wiring, one gateway ECU, and software integration effort.

---

## System Partitioning

### Network Topology Decision

The key question is: **Which ECUs share a CAN bus, and which are isolated?**

**Principles for partitioning:**

1. **Safety isolation**: Safety-relevant buses (airbag, braking) must not share bus load with body/comfort systems. Fault containment is a regulatory requirement, not just a design choice.

2. **Bandwidth grouping**: ECUs that exchange data frequently should share a bus. Routing through a gateway adds latency (~1–5 ms) and cost.

3. **Physical proximity**: ECUs near each other can share shorter stubs. Separating physically distant nodes onto different buses reduces harness length.

4. **Wake-up domains**: ECUs that wake together should share a bus, enabling partial networking at the bus level.

**Typical automotive partitioning:**

```
Powertrain Bus (500 kbit/s):      Engine ECU, Transmission ECU, Hybrid/EV ECU
Chassis Bus (500 kbit/s):         ABS ECU, ESC ECU, EPS ECU
Body Bus (125 kbit/s):            BCM, Lighting, Seat Control, HVAC
Infotainment Bus (CAN-FD):        Head Unit, Instrument Cluster, ADAS
Diagnostics Bus (shared):         OBD-II connector (via gateway)
```

A central **gateway ECU** routes messages between buses and often adds filtering, rate limiting, and security. This is a cost center but is almost always cheaper than running a single flat bus with all ECUs.

### Gateway Cost vs. Bus Count

Adding a gateway adds hardware cost ($5–$30 per gateway ECU) but can be justified when:
- It replaces multiple point-to-point wires
- It enables separation of safety and non-safety domains
- It allows independent development and testing of bus segments
- It enables partial networking (wake-up control per segment)

---

## Message Scheduling and Priority Tuning

CAN uses a bitwise arbitration mechanism where lower CAN IDs have higher priority. A well-designed ID assignment directly affects latency and bus efficiency.

### Priority Assignment Process

1. List all messages with their **deadline** (maximum acceptable latency from trigger to reception).
2. Assign IDs so that tighter deadline = lower ID (higher priority).
3. Verify using **Worst-Case Response Time (WCRT)** analysis.

**WCRT formula for a message m:**

```
R_m = C_m + sum over all higher-priority messages j: ceil(R_m / T_j) * C_j
```

Where:
- `R_m` = worst-case response time of message m
- `C_m` = transmission time of message m
- `T_j` = period of higher-priority message j
- `C_j` = transmission time of higher-priority message j

This is iterated until convergence.

### Bandwidth Reservation for Diagnostics

Diagnostic traffic (UDS, OBD-II) should be assigned **low priority** IDs and should be rate-limited. Without rate limiting, a diagnostic tester can saturate the bus and violate timing guarantees of operational messages.

---

## Code Examples in C/C++

### 1. Message Packing — Efficient Signal Encoding

```c
/**
 * cost_optimized_can.c
 *
 * Demonstrates efficient signal packing to minimize frame count.
 * All signals for a sensor cluster packed into a single 8-byte frame.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* CAN frame structure (platform-independent) */
typedef struct {
    uint32_t id;        /* CAN ID (11-bit standard or 29-bit extended) */
    uint8_t  dlc;       /* Data Length Code (0–8 for Classic CAN) */
    uint8_t  data[8];   /* Payload */
} can_frame_t;

/* --------------------------------------------------------------------------
 * Signal definitions for Frame 0x200 — Powertrain Sensor Data
 *
 * Byte 0–1: Engine Speed (uint16, 0.25 RPM/LSB, range 0–16383.75 RPM)
 * Byte 2:   Throttle Position (uint8, 0.4%/LSB, range 0–102%)
 * Byte 3:   Coolant Temperature (uint8, 1°C/LSB, offset -40, range -40..215°C)
 * Byte 4:   Battery Voltage (uint8, 0.1V/LSB, offset 0, range 0–25.5V)
 * Byte 5:   Flags (bit 0=MIL, bit 1=CruiseActive, bit 2=ACRequest, bits 3-7 reserved)
 * Bytes 6–7: Checksum + Rolling Counter (cheap integrity, avoids CRC hardware cost)
 * --------------------------------------------------------------------------
 */

#define FRAME_POWERTRAIN_ID   0x200U

typedef struct {
    uint16_t engine_speed_raw;     /* 0.25 RPM/LSB */
    uint8_t  throttle_raw;         /* 0.4 %/LSB    */
    uint8_t  coolant_temp_raw;     /* 1°C/LSB, -40 offset */
    uint8_t  battery_voltage_raw;  /* 0.1 V/LSB   */
    uint8_t  flags;
} powertrain_signals_t;

/**
 * pack_powertrain_frame
 *
 * Packs all powertrain signals into a single CAN frame.
 * Rolling counter increments each call for detection of missed frames.
 *
 * @param signals  Input signal values (physical → raw must be done by caller)
 * @param frame    Output CAN frame
 * @param counter  Rolling counter 0–15 (caller increments)
 */
void pack_powertrain_frame(const powertrain_signals_t *signals,
                            can_frame_t *frame,
                            uint8_t counter)
{
    frame->id  = FRAME_POWERTRAIN_ID;
    frame->dlc = 8;

    frame->data[0] = (uint8_t)(signals->engine_speed_raw >> 8);
    frame->data[1] = (uint8_t)(signals->engine_speed_raw & 0xFF);
    frame->data[2] = signals->throttle_raw;
    frame->data[3] = signals->coolant_temp_raw;
    frame->data[4] = signals->battery_voltage_raw;
    frame->data[5] = signals->flags & 0x07U;  /* mask reserved bits */

    /* Rolling counter in low nibble of byte 6 */
    frame->data[6] = counter & 0x0FU;

    /* Simple XOR checksum over bytes 0–6 for low-cost integrity */
    uint8_t chk = 0;
    for (int i = 0; i < 7; i++) chk ^= frame->data[i];
    frame->data[7] = chk;
}

/**
 * unpack_powertrain_frame
 *
 * Unpacks and validates a received powertrain frame.
 * Returns false if checksum fails or counter is stale.
 *
 * @param frame     Received CAN frame
 * @param signals   Output decoded signals
 * @param last_ctr  Pointer to last-seen counter value (updated on success)
 * @return          true if frame is valid and fresh
 */
bool unpack_powertrain_frame(const can_frame_t *frame,
                              powertrain_signals_t *signals,
                              uint8_t *last_ctr)
{
    if (frame->id != FRAME_POWERTRAIN_ID || frame->dlc != 8)
        return false;

    /* Verify checksum */
    uint8_t chk = 0;
    for (int i = 0; i < 7; i++) chk ^= frame->data[i];
    if (chk != frame->data[7]) return false;

    /* Check rolling counter advances (allows wrap-around) */
    uint8_t recv_ctr = frame->data[6] & 0x0FU;
    uint8_t expected = (*last_ctr + 1) & 0x0FU;
    if (recv_ctr != expected) return false;  /* missed frame or replay */
    *last_ctr = recv_ctr;

    signals->engine_speed_raw    = ((uint16_t)frame->data[0] << 8) | frame->data[1];
    signals->throttle_raw        = frame->data[2];
    signals->coolant_temp_raw    = frame->data[3];
    signals->battery_voltage_raw = frame->data[4];
    signals->flags               = frame->data[5];

    return true;
}

/* Physical value helpers (inline, zero-overhead) */
static inline float engine_speed_rpm(uint16_t raw)  { return raw * 0.25f; }
static inline float throttle_pct(uint8_t raw)        { return raw * 0.4f; }
static inline float coolant_temp_c(uint8_t raw)      { return (float)raw - 40.0f; }
static inline float battery_voltage_v(uint8_t raw)   { return raw * 0.1f; }
```

---

### 2. Bus Load Monitoring and Rate Adaptation

```c
/**
 * can_bus_load_monitor.c
 *
 * Tracks bus utilization and adapts transmission rates for non-critical
 * messages to prevent bus saturation — a key cost-optimization technique
 * (avoids needing a second bus or higher-speed physical layer upgrade).
 */

#include <stdint.h>
#include <stdbool.h>

#define CAN_BITRATE_BPS         500000U    /* 500 kbit/s */
#define MONITOR_WINDOW_MS       100U       /* 100 ms sliding window */
#define MAX_BUS_LOAD_PERCENT    65U        /* Target maximum load */

/* Cost in bits for an 8-byte standard CAN frame (worst-case with bit stuffing) */
#define BITS_PER_FRAME_8BYTE    134U

typedef struct {
    uint32_t frames_in_window;
    uint32_t window_start_ms;
    uint8_t  load_percent;       /* 0–100 */
    bool     overload_detected;
} can_bus_monitor_t;

/**
 * Update bus load estimate.
 * Call this every time a frame is transmitted or received.
 *
 * @param mon        Monitor state
 * @param now_ms     Current system tick in milliseconds
 */
void bus_monitor_frame_event(can_bus_monitor_t *mon, uint32_t now_ms)
{
    uint32_t elapsed = now_ms - mon->window_start_ms;

    if (elapsed >= MONITOR_WINDOW_MS) {
        /* Calculate load for the completed window */
        uint32_t total_bits = mon->frames_in_window * BITS_PER_FRAME_8BYTE;
        uint32_t capacity   = (CAN_BITRATE_BPS / 1000U) * MONITOR_WINDOW_MS;
        mon->load_percent   = (uint8_t)((total_bits * 100U) / capacity);
        mon->overload_detected = (mon->load_percent > MAX_BUS_LOAD_PERCENT);

        /* Reset window */
        mon->frames_in_window = 0;
        mon->window_start_ms  = now_ms;
    }

    mon->frames_in_window++;
}

/**
 * Adaptive transmission period selector.
 *
 * Returns the transmission period in ms for a given message, scaled
 * based on current bus load. Non-critical messages are sent less
 * frequently under high load, freeing bandwidth for priority traffic.
 *
 * @param mon           Current bus monitor state
 * @param base_period   Nominal transmission period in ms
 * @param is_critical   If true, period is never increased
 * @return              Adjusted period in ms
 */
uint32_t adaptive_tx_period(const can_bus_monitor_t *mon,
                             uint32_t base_period,
                             bool is_critical)
{
    if (is_critical || !mon->overload_detected)
        return base_period;

    /* Slow down by 2× at 65–80% load, 4× above 80% */
    if (mon->load_percent > 80U)
        return base_period * 4U;
    else
        return base_period * 2U;
}
```

---

### 3. Worst-Case Response Time (WCRT) Analysis Tool

```cpp
/**
 * wcrt_analysis.cpp
 *
 * Iterative WCRT analysis for CAN message scheduling.
 * Use this at design time to verify that all messages meet their deadlines
 * before committing to a bus speed / topology — avoiding costly redesigns.
 *
 * Reference: Davis et al., "Controller Area Network (CAN) schedulability
 * analysis: Refuted, revisited and revised." (Real-Time Systems, 2007)
 */

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <string>

struct CanMessage {
    std::string name;
    uint32_t    period_us;    /* Transmission period in microseconds */
    uint32_t    tx_time_us;   /* Frame transmission time (bits/bitrate * 1e6) */
    uint32_t    deadline_us;  /* Maximum allowed response time */
    uint8_t     priority;     /* Lower = higher priority (= lower CAN ID) */
};

/**
 * Calculate worst-case response time for message m.
 * All messages in the set with higher priority (lower ID) are considered.
 *
 * @param messages  Full set of messages on the bus
 * @param idx       Index of the message to analyze
 * @return          WCRT in microseconds, or UINT32_MAX if not schedulable
 */
uint32_t calc_wcrt(const std::vector<CanMessage>& messages, size_t idx)
{
    const CanMessage& m = messages[idx];

    /* Collect higher-priority messages */
    std::vector<const CanMessage*> hp;
    for (size_t i = 0; i < messages.size(); i++) {
        if (i != idx && messages[i].priority < m.priority)
            hp.push_back(&messages[i]);
    }

    /* Iterative fixed-point computation */
    uint32_t R = m.tx_time_us;
    uint32_t R_prev = 0;
    const uint32_t MAX_ITER = 1000;
    uint32_t iter = 0;

    while (R != R_prev && iter < MAX_ITER) {
        R_prev = R;
        uint32_t interference = 0;
        for (const auto* j : hp) {
            uint32_t preemptions = (uint32_t)std::ceil((double)R / j->period_us);
            interference += preemptions * j->tx_time_us;
        }
        R = m.tx_time_us + interference;
        iter++;
    }

    if (iter >= MAX_ITER) return UINT32_MAX;  /* Not schedulable */
    return R;
}

/**
 * Run full WCRT analysis and print a cost optimization report.
 * Identifies messages that could tolerate a lower-priority ID (freeing
 * up low IDs for future additions without bus speed upgrades).
 */
void run_wcrt_analysis(std::vector<CanMessage>& messages)
{
    /* Sort by priority (ascending = highest priority first) */
    std::sort(messages.begin(), messages.end(),
              [](const CanMessage& a, const CanMessage& b) {
                  return a.priority < b.priority;
              });

    std::cout << "=== CAN Bus WCRT Analysis ===\n\n";
    std::cout << std::left;
    printf("%-20s %8s %8s %8s %8s %10s\n",
           "Message", "Period", "TxTime", "Deadline", "WCRT", "Margin");
    printf("%-20s %8s %8s %8s %8s %10s\n",
           "", "(us)", "(us)", "(us)", "(us)", "");
    std::cout << std::string(70, '-') << '\n';

    bool all_schedulable = true;
    for (size_t i = 0; i < messages.size(); i++) {
        uint32_t wcrt = calc_wcrt(messages, i);
        const CanMessage& m = messages[i];

        bool meets_deadline = (wcrt <= m.deadline_us);
        if (!meets_deadline) all_schedulable = false;

        int32_t margin = (int32_t)m.deadline_us - (int32_t)wcrt;
        printf("%-20s %8u %8u %8u %8u %+10d  %s\n",
               m.name.c_str(), m.period_us, m.tx_time_us,
               m.deadline_us, wcrt, margin,
               meets_deadline ? "OK" : "*** DEADLINE MISS ***");
    }

    std::cout << '\n';
    if (all_schedulable)
        std::cout << "All messages schedulable. Bus design is valid.\n";
    else
        std::cout << "SCHEDULING FAILURE: increase bitrate, reduce frame count,\n"
                  << "or partition onto multiple buses.\n";
}

/* Example usage */
int main()
{
    /* Frame tx time at 500 kbit/s for 8-byte frame: 134 bits / 500000 * 1e6 = 268 us */
    const uint32_t TX_8BYTE_500K = 268U;

    std::vector<CanMessage> bus_messages = {
        { "ABS_Control",      10000,  TX_8BYTE_500K,  10000, 1 },
        { "Engine_Torque",    10000,  TX_8BYTE_500K,  15000, 2 },
        { "Vehicle_Speed",    20000,  TX_8BYTE_500K,  25000, 3 },
        { "Throttle_Pos",     10000,  TX_8BYTE_500K,  20000, 4 },
        { "HVAC_Request",    100000,  TX_8BYTE_500K, 200000, 5 },
        { "Ambient_Temp",   1000000,  TX_8BYTE_500K, 2000000, 6 },
    };

    run_wcrt_analysis(bus_messages);
    return 0;
}
```

---

### 4. Partial Networking Wake-Up Frame Generator

```c
/**
 * partial_networking.c
 *
 * Generates ISO 11898-6 compliant partial networking (PN) wake-up frames.
 * PN reduces quiescent current by keeping most ECUs in sleep mode,
 * waking only the ECU(s) needed for a specific function.
 * This avoids the hardware cost of dedicated wake-up circuits on each node.
 */

#include <stdint.h>
#include <string.h>

/* TJA1145 / NCV7356 compliant PN wake-up frame */
typedef struct {
    uint32_t can_id;          /* The ID the sleeping ECU is configured to respond to */
    uint8_t  dlc;             /* Must match ECU's configured DLC filter */
    uint8_t  data[8];         /* Must match ECU's configured data mask/pattern */
    bool     is_extended;     /* Extended (29-bit) vs standard (11-bit) ID */
} pn_wakeup_frame_t;

/* Wake-up target definitions — configures which ECU wakes for which function */
typedef struct {
    const char        *ecu_name;
    pn_wakeup_frame_t  wake_frame;
    uint32_t           wake_timeout_ms;  /* ECU must be operational within this time */
} ecu_pn_config_t;

static const ecu_pn_config_t ECU_PN_TABLE[] = {
    {
        .ecu_name = "Body Control Module",
        .wake_frame = {
            .can_id = 0x101,
            .dlc    = 1,
            .data   = { 0x01 },  /* Bit 0 = BCM wake request */
            .is_extended = false
        },
        .wake_timeout_ms = 50
    },
    {
        .ecu_name = "HVAC Controller",
        .wake_frame = {
            .can_id = 0x101,
            .dlc    = 1,
            .data   = { 0x02 },  /* Bit 1 = HVAC wake request */
            .is_extended = false
        },
        .wake_timeout_ms = 80
    },
    {
        .ecu_name = "Seat Memory ECU",
        .wake_frame = {
            .can_id = 0x101,
            .dlc    = 1,
            .data   = { 0x04 },  /* Bit 2 = Seat wake request */
            .is_extended = false
        },
        .wake_timeout_ms = 120
    },
};

#define ECU_PN_TABLE_SIZE (sizeof(ECU_PN_TABLE) / sizeof(ECU_PN_TABLE[0]))

/**
 * build_combined_wake_frame
 *
 * Combines wake requests for multiple ECUs into a single CAN frame,
 * reducing the number of frames needed and decreasing bus activity
 * during network wake-up.
 *
 * @param ecu_indices    Array of ECU indices from ECU_PN_TABLE to wake
 * @param count          Number of ECUs to wake
 * @param out_frame      Output frame (all targets must share ID and DLC)
 * @return               true if combined frame was built successfully
 */
bool build_combined_wake_frame(const uint8_t *ecu_indices,
                                uint8_t count,
                                pn_wakeup_frame_t *out_frame)
{
    if (count == 0 || count > ECU_PN_TABLE_SIZE) return false;

    /* Validate all ECUs share the same ID and DLC */
    const pn_wakeup_frame_t *ref = &ECU_PN_TABLE[ecu_indices[0]].wake_frame;
    memcpy(out_frame, ref, sizeof(pn_wakeup_frame_t));

    for (uint8_t i = 1; i < count; i++) {
        const pn_wakeup_frame_t *wf = &ECU_PN_TABLE[ecu_indices[i]].wake_frame;
        if (wf->can_id != ref->can_id || wf->dlc != ref->dlc) return false;

        /* OR the data bytes to combine wake bits */
        for (uint8_t b = 0; b < ref->dlc; b++)
            out_frame->data[b] |= wf->data[b];
    }

    return true;
}
```

---

## Code Examples in Rust

### 1. Efficient CAN Frame Builder with Zero-Copy Signal Packing

```rust
//! can_frame_builder.rs
//!
//! Zero-copy, allocation-free CAN frame packing in Rust.
//! Uses bit manipulation on fixed-size arrays — ideal for bare-metal or RTOS targets.
//! No std dependency: suitable for `#![no_std]` embedded environments.

#![allow(dead_code)]

/// Standard 8-byte CAN frame (Classic CAN)
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct CanFrame {
    pub id: u32,
    pub dlc: u8,
    pub data: [u8; 8],
}

impl CanFrame {
    pub const fn new(id: u32, dlc: u8) -> Self {
        Self { id, dlc, data: [0u8; 8] }
    }

    /// Pack an unsigned value into an arbitrary bit region of the payload.
    /// Supports signals that span byte boundaries (e.g., a 12-bit signal
    /// starting at bit 4).
    ///
    /// - `start_bit`: LSB position in Motorola (big-endian) bit order
    /// - `bit_len`:   Number of bits for this signal (1–32)
    /// - `value`:     Raw (pre-scaled) signal value
    pub fn set_signal_be(&mut self, start_bit: u8, bit_len: u8, value: u32) {
        debug_assert!(bit_len <= 32, "signal too wide");
        debug_assert!((start_bit as u16 + bit_len as u16) <= 64, "signal out of frame");

        let mask = if bit_len == 32 { u32::MAX } else { (1u32 << bit_len) - 1 };
        let value = value & mask;

        for i in 0..bit_len {
            let bit_val = (value >> i) & 1;
            let bit_pos = start_bit + i;
            let byte_idx = (bit_pos / 8) as usize;
            let bit_in_byte = bit_pos % 8;

            if bit_val == 1 {
                self.data[byte_idx] |= 1u8 << bit_in_byte;
            } else {
                self.data[byte_idx] &= !(1u8 << bit_in_byte);
            }
        }
    }

    /// Extract an unsigned signal value from the payload (little-endian bit order).
    pub fn get_signal_le(&self, start_bit: u8, bit_len: u8) -> u32 {
        debug_assert!(bit_len <= 32);
        let mut value = 0u32;
        for i in 0..bit_len {
            let bit_pos = start_bit + i;
            let byte_idx = (bit_pos / 8) as usize;
            let bit_in_byte = bit_pos % 8;
            let bit_val = ((self.data[byte_idx] >> bit_in_byte) & 1) as u32;
            value |= bit_val << i;
        }
        value
    }
}

/// Strongly-typed powertrain signal pack — eliminates raw byte manipulation
/// at call sites and ensures consistent encoding across all producers.
pub struct PowertrainFrame(CanFrame);

impl PowertrainFrame {
    pub const CAN_ID: u32 = 0x200;

    pub fn new() -> Self {
        Self(CanFrame::new(Self::CAN_ID, 8))
    }

    /// Engine speed: 0.25 RPM/LSB, 16-bit, bits 0–15
    pub fn set_engine_speed(&mut self, rpm: f32) {
        let raw = (rpm / 0.25) as u32;
        self.0.set_signal_be(0, 16, raw.min(0xFFFF));
    }

    /// Throttle position: 0.4%/LSB, 8-bit, bits 16–23
    pub fn set_throttle(&mut self, pct: f32) {
        let raw = (pct / 0.4) as u32;
        self.0.set_signal_be(16, 8, raw.min(0xFF));
    }

    /// Coolant temperature: 1°C/LSB, -40°C offset, 8-bit, bits 24–31
    pub fn set_coolant_temp(&mut self, deg_c: f32) {
        let raw = ((deg_c + 40.0) as u32).min(0xFF);
        self.0.set_signal_be(24, 8, raw);
    }

    /// MIL flag: bit 40
    pub fn set_mil(&mut self, active: bool) {
        self.0.set_signal_be(40, 1, active as u32);
    }

    /// Attach rolling counter and XOR checksum (bytes 6–7)
    pub fn finalize(&mut self, counter: u8) {
        self.0.data[6] = counter & 0x0F;
        let chk = self.0.data[..7].iter().fold(0u8, |acc, &b| acc ^ b);
        self.0.data[7] = chk;
    }

    pub fn frame(&self) -> &CanFrame { &self.0 }
}

/// Stateless bus load estimator — no allocation, no floating point
/// (suitable for MCUs without FPU).
pub struct BusLoadMonitor {
    frame_count: u32,
    window_start_ms: u32,
    pub load_percent: u8,
    pub overloaded: bool,
}

impl BusLoadMonitor {
    const WINDOW_MS: u32 = 100;
    /// Bits per 8-byte CAN frame at worst-case bit stuffing
    const BITS_PER_FRAME: u32 = 134;
    /// 500 kbit/s → 50,000 bits per 100 ms window
    const CAPACITY_BITS: u32 = 50_000;
    const LOAD_LIMIT: u8 = 65;

    pub const fn new(now_ms: u32) -> Self {
        Self {
            frame_count: 0,
            window_start_ms: now_ms,
            load_percent: 0,
            overloaded: false,
        }
    }

    pub fn record_frame(&mut self, now_ms: u32) {
        let elapsed = now_ms.wrapping_sub(self.window_start_ms);
        if elapsed >= Self::WINDOW_MS {
            let bits_used = self.frame_count * Self::BITS_PER_FRAME;
            // Integer percentage, avoids division by zero
            self.load_percent = ((bits_used * 100) / Self::CAPACITY_BITS).min(100) as u8;
            self.overloaded = self.load_percent > Self::LOAD_LIMIT;
            self.frame_count = 0;
            self.window_start_ms = now_ms;
        }
        self.frame_count += 1;
    }

    /// Returns recommended TX period multiplier for non-critical messages
    pub fn load_multiplier(&self) -> u32 {
        if !self.overloaded { 1 }
        else if self.load_percent > 80 { 4 }
        else { 2 }
    }
}
```

---

### 2. WCRT Scheduler in Rust

```rust
//! wcrt_scheduler.rs
//!
//! Compile-time and runtime WCRT analysis for CAN message sets.
//! Helps verify bus designs during development — catch deadline
//! misses before hardware is ordered.

use std::fmt;

#[derive(Debug, Clone)]
pub struct CanMessage {
    pub name: String,
    pub period_us: u32,
    pub tx_time_us: u32,
    pub deadline_us: u32,
    pub priority: u8,   // lower = higher priority
}

#[derive(Debug)]
pub struct WcrtResult {
    pub message_name: String,
    pub wcrt_us: Option<u32>,  // None if not schedulable
    pub deadline_us: u32,
    pub meets_deadline: bool,
    pub margin_us: i64,
}

impl fmt::Display for WcrtResult {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self.wcrt_us {
            Some(wcrt) => write!(
                f,
                "{:<20} WCRT={:>8}us  Deadline={:>8}us  Margin={:>+8}  {}",
                self.message_name, wcrt, self.deadline_us, self.margin_us,
                if self.meets_deadline { "OK" } else { "*** MISS ***" }
            ),
            None => write!(f, "{:<20} NOT SCHEDULABLE", self.message_name),
        }
    }
}

/// Calculate WCRT for a single message given its set of higher-priority peers.
fn calc_wcrt(msg: &CanMessage, higher_priority: &[&CanMessage]) -> Option<u32> {
    let mut r = msg.tx_time_us;
    for _ in 0..1000 {
        let interference: u32 = higher_priority
            .iter()
            .map(|j| {
                let preemptions = (r as f64 / j.period_us as f64).ceil() as u32;
                preemptions * j.tx_time_us
            })
            .sum();

        let r_new = msg.tx_time_us + interference;
        if r_new == r {
            return Some(r);
        }
        r = r_new;

        // Bail out early if response time exceeds deadline
        if r > msg.deadline_us * 10 {
            return None;
        }
    }
    None  // Did not converge
}

pub fn analyze_bus(messages: &[CanMessage]) -> Vec<WcrtResult> {
    // Sort by priority: highest priority (lowest value) first
    let mut sorted: Vec<&CanMessage> = messages.iter().collect();
    sorted.sort_by_key(|m| m.priority);

    sorted
        .iter()
        .enumerate()
        .map(|(idx, msg)| {
            let hp: Vec<&CanMessage> = sorted[..idx].to_vec();
            let wcrt_us = calc_wcrt(msg, &hp);
            let meets_deadline = wcrt_us.map_or(false, |w| w <= msg.deadline_us);
            let margin_us = wcrt_us.map_or(i64::MIN, |w| {
                msg.deadline_us as i64 - w as i64
            });
            WcrtResult {
                message_name: msg.name.clone(),
                wcrt_us,
                deadline_us: msg.deadline_us,
                meets_deadline,
                margin_us,
            }
        })
        .collect()
}

fn main() {
    // Frame tx time: 134 bits / 500,000 bit/s = 268 µs
    const TX_8B_500K: u32 = 268;

    let messages = vec![
        CanMessage { name: "ABS_Control".into(),   period_us: 10_000,   tx_time_us: TX_8B_500K, deadline_us: 10_000,    priority: 1 },
        CanMessage { name: "Engine_Torque".into(), period_us: 10_000,   tx_time_us: TX_8B_500K, deadline_us: 15_000,    priority: 2 },
        CanMessage { name: "Vehicle_Speed".into(), period_us: 20_000,   tx_time_us: TX_8B_500K, deadline_us: 25_000,    priority: 3 },
        CanMessage { name: "Throttle_Pos".into(),  period_us: 10_000,   tx_time_us: TX_8B_500K, deadline_us: 20_000,    priority: 4 },
        CanMessage { name: "HVAC_Request".into(),  period_us: 100_000,  tx_time_us: TX_8B_500K, deadline_us: 200_000,   priority: 5 },
        CanMessage { name: "Ambient_Temp".into(),  period_us: 1_000_000,tx_time_us: TX_8B_500K, deadline_us: 2_000_000, priority: 6 },
    ];

    println!("=== CAN Bus WCRT Analysis (Rust) ===\n");
    let results = analyze_bus(&messages);

    let all_ok = results.iter().all(|r| r.meets_deadline);
    for r in &results {
        println!("{}", r);
    }

    println!("\n{}", if all_ok {
        "✓ All messages schedulable. Bus design validated."
    } else {
        "✗ Scheduling failure — increase bitrate, reduce traffic, or partition the bus."
    });
}
```

---

### 3. Static Node Configuration Table (const / no-alloc)

```rust
//! node_config.rs
//!
//! Compile-time CAN node configuration table — zero runtime cost.
//! Encodes the full system topology in a single const struct.
//! Eliminates runtime configuration lookups, reducing MCU RAM usage.

/// Maximum number of nodes representable at compile time.
const MAX_NODES: usize = 16;
const MAX_BUSES: usize = 4;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum BusRole {
    Powertrain,
    Chassis,
    Body,
    Diagnostic,
}

#[derive(Debug, Clone, Copy)]
pub struct CanNodeConfig {
    pub node_id: u8,
    pub bus: BusRole,
    pub bitrate_kbps: u32,
    pub rx_filter_id: u32,
    pub rx_filter_mask: u32,
    pub tx_base_id: u32,
    pub supports_partial_networking: bool,
    pub pn_wake_id: u32,
    pub pn_wake_data_byte: u8,
}

/// Full system node table — evaluated at compile time.
/// Adding a node here automatically propagates to all subsystems that
/// iterate this table, preventing costly integration mistakes.
pub const SYSTEM_NODE_TABLE: [CanNodeConfig; 5] = [
    CanNodeConfig {
        node_id: 0x01,
        bus: BusRole::Powertrain,
        bitrate_kbps: 500,
        rx_filter_id: 0x100,
        rx_filter_mask: 0x7F0,  /* Accept 0x100–0x10F */
        tx_base_id: 0x200,
        supports_partial_networking: false,
        pn_wake_id: 0,
        pn_wake_data_byte: 0,
    },
    CanNodeConfig {
        node_id: 0x02,
        bus: BusRole::Chassis,
        bitrate_kbps: 500,
        rx_filter_id: 0x300,
        rx_filter_mask: 0x7F0,
        tx_base_id: 0x310,
        supports_partial_networking: false,
        pn_wake_id: 0,
        pn_wake_data_byte: 0,
    },
    CanNodeConfig {
        node_id: 0x10,
        bus: BusRole::Body,
        bitrate_kbps: 125,
        rx_filter_id: 0x101,
        rx_filter_mask: 0x7FF,
        tx_base_id: 0x500,
        supports_partial_networking: true,
        pn_wake_id: 0x101,
        pn_wake_data_byte: 0x01,  /* BCM wake bit */
    },
    CanNodeConfig {
        node_id: 0x11,
        bus: BusRole::Body,
        bitrate_kbps: 125,
        rx_filter_id: 0x101,
        rx_filter_mask: 0x7FF,
        tx_base_id: 0x510,
        supports_partial_networking: true,
        pn_wake_id: 0x101,
        pn_wake_data_byte: 0x02,  /* HVAC wake bit */
    },
    CanNodeConfig {
        node_id: 0xFF,
        bus: BusRole::Diagnostic,
        bitrate_kbps: 500,
        rx_filter_id: 0x7DF,   /* OBD-II functional address */
        rx_filter_mask: 0x7FF,
        tx_base_id: 0x7E8,
        supports_partial_networking: false,
        pn_wake_id: 0,
        pn_wake_data_byte: 0,
    },
];

/// Count nodes on a given bus — zero-overhead at runtime (const fn in Rust 1.46+)
pub const fn count_nodes_on_bus(bus: BusRole) -> usize {
    let mut count = 0;
    let mut i = 0;
    while i < SYSTEM_NODE_TABLE.len() {
        // Note: PartialEq on enums requires manual compare in const context
        let same = matches!(
            (SYSTEM_NODE_TABLE[i].bus, bus),
            (BusRole::Powertrain, BusRole::Powertrain)
            | (BusRole::Chassis,    BusRole::Chassis)
            | (BusRole::Body,       BusRole::Body)
            | (BusRole::Diagnostic, BusRole::Diagnostic)
        );
        if same { count += 1; }
        i += 1;
    }
    count
}
```

---

## Summary

Cost optimization in CAN design is a multi-layered discipline that spans hardware selection, network architecture, message design, and software implementation. The following table captures the key techniques and their impact:

| Technique | Cost Reduction Mechanism | Typical Impact |
|---|---|---|
| Integrated CAN controllers in MCU | Eliminates external CAN controller chip | $0.50–$2.00 per node |
| Signal packing (multiple signals per frame) | Reduces frame count → lower bus load → avoids bus upgrade | 30–70% fewer frames |
| Adaptive transmission rates | Prevents bus saturation without hardware changes | Avoids 2nd bus addition |
| Partial networking (PN transceivers) | Eliminates dedicated wake-up circuits | $1–$5 per node |
| WCRT analysis before hardware commit | Catches scheduling failures early | Avoids costly ECU respin |
| System partitioning (safety isolation) | Reduces gateway cost and wire length | 5–15% harness cost reduction |
| Const node configuration tables | Eliminates runtime config RAM and lookup overhead | Smaller MCU, lower BOM |
| CAN-FD bus consolidation | Two Classic CAN buses → one CAN-FD bus | Saves gateway ECU + wiring |
| Priority-based ID assignment | Ensures critical messages meet deadlines without extra bandwidth | Avoids bus speed upgrade |

**Key design principles to carry forward:**

1. **Design for bandwidth headroom**: Never exceed 65–70% bus load. Reserve capacity for error recovery and future signals.
2. **Pack signals early**: Resist the temptation to add individual frames per signal. Grouping signals by functional domain and update rate is the highest-ROI optimization.
3. **Use WCRT analysis as a gateway check**: Every bus design change should be validated with scheduling analysis before hardware is ordered.
4. **Partition by safety domain, not just bandwidth**: Regulatory requirements (ISO 26262) mandate separation of safety-critical and non-safety buses. Design this in from day one — retrofitting safety isolation is expensive.
5. **Leverage partial networking**: In systems with many ECUs that are not always active, PN transceivers pay for themselves quickly in reduced quiescent current and simplified power management hardware.
6. **Prefer const/static configuration**: Rust's `const` evaluation and C's `static const` structures allow the compiler to validate configuration at build time, catching integration errors before silicon is programmed.

A well-optimized CAN system is one where every frame carries meaningful data, every ECU wakes only when needed, every bus operates well within its bandwidth budget, and all timing requirements are mathematically verified — all without paying for more hardware than the application demands.

---

*Document generated for CAN Design Series — Topic 98 of 100.*