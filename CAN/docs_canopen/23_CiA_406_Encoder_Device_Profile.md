# CiA 406 — CANopen Encoder Device Profile

## Table of Contents

1. [Introduction](#1-introduction)
2. [Object Dictionary Layout](#2-object-dictionary-layout)
3. [Position Value Object (0x6004)](#3-position-value-object-0x6004)
4. [Encoder Type — Singleturn vs. Multiturn (0x6001)](#4-encoder-type--singleturn-vs-multiturn-0x6001)
5. [Preset Function (0x6003)](#5-preset-function-0x6003)
6. [Scaling Function (0x6001, 0x6009, 0x600A)](#6-scaling-function)
7. [Working Area Limits (0x6005, 0x6006)](#7-working-area-limits-0x6005-0x6006)
8. [Alarm Objects (0x6007, 0x6008)](#8-alarm-objects-0x6007-0x6008)
9. [Master Polling vs. Event-Driven PDO Strategies](#9-master-polling-vs-event-driven-pdo-strategies)
10. [C/C++ Programming Examples](#10-cc-programming-examples)
11. [Summary](#11-summary)

---

## 1. Introduction

**CiA 406** is a CANopen device profile defined by CAN in Automation (CiA) specifically for
**rotary and linear encoders**. It standardises the object dictionary entries, data types,
and communication behaviour so that encoders from different vendors can be used
interchangeably on the same CANopen network without vendor-specific drivers.

Key characteristics covered by CiA 406:

- Absolute position measurement (singleturn and multiturn)
- Scaling of raw counts to engineering units (steps, degrees, mm, …)
- Preset (zero-point adjustment)
- Working area monitoring with configurable limits
- Alarm and status reporting
- Both polled (SDO/PDO on request) and event-driven (PDO on change or timer) data delivery

```
 +------------------+       CANopen Bus        +-------------------+
 |   PLC / Master   |<========================>|  CiA 406 Encoder  |
 |                  |  SDO config at startup   |                   |
 |  - Reads pos.    |  PDO position at runtime |  - Singleturn     |
 |  - Sets preset   |  Emergency on alarm      |  - Multiturn      |
 |  - Sets limits   |                          |  - Scaling        |
 +------------------+                          +-------------------+
```

---

## 2. Object Dictionary Layout

CiA 406 mandates a well-defined range inside the standard CANopen object dictionary
(index 0x6000–0x67FF for process data objects).

```
Index    Sub  Name                            Access  Type
-------  ---  ------------------------------  ------  --------
0x6000   00   Operating parameters            RW      UINT16
0x6001   00   Measuring steps per revolution  RW      UINT32
0x6002   00   Total measuring range           RW      UINT32
0x6003   00   Preset value                    RW      UINT32
0x6004   00   Position value                  RO      UINT32
0x6005   00   Limit switch speed CW           RW      UINT32
0x6006   00   Limit switch speed CCW          RW      UINT32
         --   (also used as position limits)
0x6007   00   Alarm control register          RW      UINT16
0x6008   00   Alarm status register           RO      UINT16
0x6009   00   Offset value (numerator)        RW      UINT32
0x600A   00   Scaling denominator             RW      UINT32
0x600B   00   Velocity value                  RO      INT32
0x6500   00   Operating status                RO      UINT16
0x6501   00   Single-turn resolution          RO      UINT32
0x6502   00   Number of distinguishable revs  RO      UINT32
```

> **Note:** Not all sub-indexes are mandatory. Mandatory vs. optional is specified in the
> CiA 406 standard document. The values above reflect the most common implementation.

---

## 3. Position Value Object (0x6004)

`0x6004` is the **primary output** of a CiA 406 encoder. It holds the current absolute
position as a 32-bit unsigned integer. The unit and resolution depend on the scaling
configuration (see Section 6).

### Raw vs. Scaled Position

```
  Physical shaft                Object 0x6004 (raw)       Object 0x6004 (scaled)
  ─────────────────             ────────────────────       ──────────────────────
  One full revolution  ──────>  0 … (steps/rev - 1)  -->  0 … user_units
                                e.g. 0 … 4095 (12-bit)    e.g. 0 … 359 (degrees)
```

The position value wraps (rolls over) at the total measuring range boundary for
singleturn encoders. Multiturn encoders extend the range across many revolutions.

### Reading via SDO (C example)

```c
#include <stdint.h>
#include "canopen_sdo.h"   /* vendor/stack-specific SDO API */

/**
 * Read current position value from encoder node.
 * @param node_id  CANopen node ID of the encoder (1..127)
 * @param position Output parameter for position value
 * @return 0 on success, negative error code on failure
 */
int encoder_read_position(uint8_t node_id, uint32_t *position)
{
    return sdo_read_u32(node_id,
                        0x6004,   /* index  */
                        0x00,     /* subindex */
                        position);
}
```

---

## 4. Encoder Type — Singleturn vs. Multiturn (0x6001)

### Singleturn Encoder

Measures position **within one revolution** only. The value resets to 0 after each full
turn. Typical resolution: 12–16 bit (4 096 to 65 536 steps per revolution).

```
  Shaft angle  →   0°    90°   180°   270°   360°(=0°)
  Raw value    →   0    1024   2048   3072      0
                   |─────────────────────────|
                         4096 steps / rev
                   (wraps here – no revolution counter)
```

### Multiturn Encoder

Adds a **revolution counter** on top of the singleturn measurement. Total position spans
many revolutions without wrap-around.

```
  Rev  0:  0 ──────────────────── 4095
  Rev  1:  4096 ─────────────────  8191
  Rev  2:  8192 ───────────────── 12287
   …
  Rev  N:  N*4096 ─── (N*4096 + 4095)
           |──────────────────────────|
                Total measuring range
           e.g. 4096 steps × 4096 revs = 16 777 216 counts
```

### Relevant Objects

| Object | Description                        | Singleturn | Multiturn  |
|--------|------------------------------------|------------|------------|
| 0x6001 | Measuring steps per revolution     | e.g. 4096  | e.g. 4096  |
| 0x6002 | Total measuring range              | = 0x6001   | 0x6001 × N |
| 0x6501 | Singleturn resolution (read-only)  | 4096       | 4096       |
| 0x6502 | Number of distinguishable revs     | 1          | e.g. 4096  |

### Reading Encoder Type Info (C++)

```cpp
#include <cstdint>
#include <iostream>
#include "canopen_sdo.hpp"

struct EncoderInfo {
    uint32_t steps_per_rev;       // 0x6501
    uint32_t num_revolutions;     // 0x6502
    bool     is_multiturn;
};

EncoderInfo encoder_get_type_info(uint8_t node_id)
{
    EncoderInfo info{};

    sdo_read_u32(node_id, 0x6501, 0x00, &info.steps_per_rev);
    sdo_read_u32(node_id, 0x6502, 0x00, &info.num_revolutions);
    info.is_multiturn = (info.num_revolutions > 1);

    std::cout << "Encoder @ node " << (int)node_id << ": "
              << info.steps_per_rev << " steps/rev, "
              << info.num_revolutions << " revolutions ("
              << (info.is_multiturn ? "multiturn" : "singleturn") << ")\n";
    return info;
}
```

---

## 5. Preset Function (0x6003)

The **preset** (also called *homing* or *zero-point offset*) allows the master to define
the logical zero of the encoder independently of the physical shaft position. After a
preset, `0x6004` returns the value written to `0x6003` at the moment of preset.

### How Preset Works

```
  Before preset:
  ─────────────
  Physical pos:  [ 1 537 counts ]
  Returned pos:  [ 1 537 counts ]

  Master writes 0x6003 = 0  (preset to zero):
  ──────────────────────────────────────────
  Internal offset = -1537 applied by encoder firmware

  After preset:
  ─────────────
  Physical pos:  [ 1 537 counts ]
  Returned pos:  [ 0 counts     ]   ← logical zero now here

  Shaft moves +100 counts:
  Physical pos:  [ 1 637 counts ]
  Returned pos:  [ 100 counts   ]   ← offset maintained
```

### Preset Sequence (SDO Write)

```c
/**
 * Set encoder position to desired_value at current shaft position.
 * The encoder internally adjusts its offset.
 *
 * @param node_id       CANopen node ID
 * @param desired_value The value that 0x6004 shall report at current pos
 * @return 0 on success
 */
int encoder_preset(uint8_t node_id, uint32_t desired_value)
{
    /* Some encoders require activating the preset via operating parameters.
     * Bit 0 of 0x6000: 1 = enable preset function */
    uint16_t op_params = 0x0001;
    int rc = sdo_write_u16(node_id, 0x6000, 0x00, op_params);
    if (rc != 0) return rc;

    /* Write the desired position value to preset object */
    rc = sdo_write_u32(node_id, 0x6003, 0x00, desired_value);
    if (rc != 0) return rc;

    /* Deactivate preset bit (edge-triggered on many encoders) */
    op_params = 0x0000;
    rc = sdo_write_u16(node_id, 0x6000, 0x00, op_params);

    return rc;
}
```

> **Important:** The exact activation mechanism (level-triggered vs. edge-triggered) is
> encoder-vendor dependent. Always consult the device EDS file and manual.

---

## 6. Scaling Function

By default, `0x6004` returns raw encoder counts. The **scaling function** maps raw
counts to user-defined units using a rational factor:

```
                      steps_per_revolution (0x6001)
  scaled_position  =  ──────────────────────────────  ×  raw_position
                         total_measuring_range (0x6002)

  Or with explicit numerator/denominator:

                      numerator   (0x6009)
  scaled_position  =  ─────────────────────  ×  raw_position
                      denominator (0x600A)
```

### Example: Scale to 0–360° for one revolution

```
  steps_per_rev = 4096  (singleturn, 12-bit)
  desired_range = 360   (degrees)

  Numerator   (0x6009) = 360
  Denominator (0x600A) = 4096

  At raw = 2048 (half revolution):
  scaled = (360 / 4096) × 2048 = 180.0 °
```

### Example: Scale multiturn to 0–10 000 mm linear (rack & pinion)

```
  steps_per_rev    = 4096
  num_revolutions  = 256     (total raw = 1 048 576 counts)
  travel_mm        = 10 000

  Numerator   (0x6009) = 10 000
  Denominator (0x600A) = 1 048 576

  At raw = 524 288 (midpoint):
  scaled = (10000 / 1048576) × 524288 = 5000 mm
```

### Configuring Scaling via SDO (C++)

```cpp
/**
 * Configure encoder scaling.
 * Scaled_pos = (numerator / denominator) * raw_pos
 *
 * Example: 4096-step encoder -> 360 degrees per revolution
 *   numerator   = 360
 *   denominator = 4096
 */
int encoder_configure_scaling(uint8_t  node_id,
                               uint32_t numerator,
                               uint32_t denominator)
{
    if (denominator == 0) return -EINVAL;

    /* Enable scaling function: bit 2 of operating parameters (0x6000) */
    uint16_t op_params = 0;
    int rc = sdo_read_u16(node_id, 0x6000, 0x00, &op_params);
    if (rc != 0) return rc;

    op_params |= (1u << 2);   /* set scaling enable bit */
    rc = sdo_write_u16(node_id, 0x6000, 0x00, op_params);
    if (rc != 0) return rc;

    /* Write numerator and denominator */
    rc = sdo_write_u32(node_id, 0x6009, 0x00, numerator);
    if (rc != 0) return rc;

    rc = sdo_write_u32(node_id, 0x600A, 0x00, denominator);
    return rc;
}
```

> After scaling is enabled, `0x6004` returns scaled values and limit/preset objects
> also operate in the scaled unit. The encoder handles the calculation internally.

---

## 7. Working Area Limits (0x6005, 0x6006)

CiA 406 defines **upper** and **lower working area limits**. When the position crosses a
limit, the encoder can set an alarm bit and/or transmit an Emergency message.

```
                       Working Area
    ┌──────────────────────────────────────────────────┐
    │                                                  │
◄───┼───────────[Lower Limit]──────────[Upper Limit]───┼───►
    │               0x6006               0x6005        │  position
    │                                                  │
    └──────────────────────────────────────────────────┘
         Out of range                   Out of range
         (alarm bit 1)                  (alarm bit 0)
```

### Limit Objects

| Object | Description             | Typical Use              |
|--------|-------------------------|--------------------------|
| 0x6005 | Upper working area limit | Max. travel position     |
| 0x6006 | Lower working area limit | Min. travel position (0) |

These objects hold position values **in the same unit as 0x6004** (raw or scaled,
depending on the scaling configuration).

### Setting Limits (C)

```c
/**
 * Configure position working area limits.
 * Both limits in the same unit as 0x6004 (raw counts or scaled units).
 *
 * @param node_id    Encoder node ID
 * @param lower_lim  Minimum allowed position (maps to 0x6006)
 * @param upper_lim  Maximum allowed position (maps to 0x6005)
 */
int encoder_set_limits(uint8_t  node_id,
                       uint32_t lower_lim,
                       uint32_t upper_lim)
{
    int rc;

    rc = sdo_write_u32(node_id, 0x6005, 0x00, upper_lim);
    if (rc != 0) return rc;

    rc = sdo_write_u32(node_id, 0x6006, 0x00, lower_lim);
    if (rc != 0) return rc;

    /* Enable limit alarms: bits 0 (upper) and 1 (lower) of alarm control 0x6007 */
    uint16_t alarm_ctrl = 0x0003;
    return sdo_write_u16(node_id, 0x6007, 0x00, alarm_ctrl);
}
```

---

## 8. Alarm Objects (0x6007, 0x6008)

### Alarm Control Register (0x6007)

This **writable** register enables/disables individual alarm sources:

```
  Bit 15 … 4   Reserved
  Bit 3        Speed alarm enable
  Bit 2        Position alarm enable (generic)
  Bit 1        Lower working area alarm enable
  Bit 0        Upper working area alarm enable
```

### Alarm Status Register (0x6008)

This **read-only** register reflects active alarm conditions:

```
  Bit 15 … 4   Reserved
  Bit 3        Speed alarm active
  Bit 2        Position alarm active
  Bit 1        Lower working area exceeded
  Bit 0        Upper working area exceeded
```

### Alarm Flow

```
  Position crosses upper limit
          │
          ▼
  Encoder sets bit 0 in 0x6008
          │
          ├──► (if PDO configured) transmits TPDO with status
          │
          └──► (if Emergency enabled) sends CANopen Emergency frame
                 Error Code: 0x8611 (position exceeded)
                 Error Register: 0x08 (manufacturer specific)
```

### Reading and Handling Alarms (C++)

```cpp
#include <cstdint>

constexpr uint16_t ALARM_UPPER_LIMIT   = (1u << 0);
constexpr uint16_t ALARM_LOWER_LIMIT   = (1u << 1);
constexpr uint16_t ALARM_POSITION      = (1u << 2);
constexpr uint16_t ALARM_SPEED         = (1u << 3);

/**
 * Read and decode encoder alarm status.
 * Returns bitmask of active alarms (0 = no alarm).
 */
uint16_t encoder_read_alarms(uint8_t node_id)
{
    uint16_t status = 0;
    sdo_read_u16(node_id, 0x6008, 0x00, &status);
    return status;
}

void encoder_handle_alarms(uint8_t node_id)
{
    uint16_t alarms = encoder_read_alarms(node_id);

    if (alarms == 0) return;

    if (alarms & ALARM_UPPER_LIMIT) {
        // Position exceeded upper working area limit
        trigger_safety_stop(node_id);
    }
    if (alarms & ALARM_LOWER_LIMIT) {
        // Position below lower working area limit
        trigger_safety_stop(node_id);
    }
    if (alarms & ALARM_SPEED) {
        // Encoder shaft rotating faster than configured maximum
        log_warning("Encoder %u: speed alarm\n", node_id);
    }
}

/* CANopen Emergency callback (registered with stack) */
void on_encoder_emergency(uint8_t  node_id,
                          uint16_t error_code,
                          uint8_t  error_register,
                          uint8_t  mfr_data[5])
{
    if (error_code == 0x8611) {
        // Position limit exceeded — read alarm register for details
        encoder_handle_alarms(node_id);
    }
}
```

---

## 9. Master Polling vs. Event-Driven PDO Strategies

CANopen offers two fundamentally different ways to get position data from an encoder.
Choosing the right strategy depends on the application's real-time requirements and
bus load budget.

### 9.1 Master Polling (SDO or RTR-PDO)

The master explicitly **requests** position data whenever it needs it. Two variants:

**a) SDO polling:** Master sends an SDO upload request for `0x6004`, encoder replies.

```
  Master                            Encoder (node 3)
    │                                    │
    │──── SDO Upload Req (0x6004) ──────►│
    │                                    │  (reads sensor, prepares reply)
    │◄─── SDO Upload Resp (position) ────│
    │                                    │
    │  [wait poll_interval ms]           │
    │                                    │
    │──── SDO Upload Req (0x6004) ──────►│
    │◄─── SDO Upload Resp (position) ────│
```

Overhead: 2 CAN frames per sample, plus protocol latency.
Suitable for: slow processes, diagnostics, configuration reads.

**b) RTR-triggered PDO:** Master sends a Remote Transmission Request on the PDO COB-ID.
The encoder responds with the PDO immediately.

```
  Master                            Encoder (node 3)
    │                                    │
    │──── CAN RTR (COB-ID 0x183) ───────►│
    │◄─── TPDO1 (position data) ─────────│
    │                                    │
    │  [wait poll_interval ms]           │
    │──── CAN RTR (COB-ID 0x183) ───────►│
    │◄─── TPDO1 (position data) ─────────│
```

Overhead: 2 CAN frames per sample, but PDO is faster than SDO (no protocol overhead).
Suitable for: semi-real-time polling at fixed intervals.

### 9.2 Event-Driven PDO (Asynchronous TPDO)

The encoder **autonomously transmits** a TPDO when the position changes by more than
a configurable delta (event timer or inhibit time). The master does not poll; it
simply processes incoming TPDOs.

```
  Master                            Encoder (node 3)
    │                                    │
    │  [position stable — no traffic]    │
    │                                    │
    │◄─── TPDO1 (position = 1024) ───────│  ← shaft moves
    │◄─── TPDO1 (position = 1100) ───────│  ← continues moving
    │◄─── TPDO1 (position = 1156) ───────│
    │                                    │
    │  [shaft stops — no more TPDOs]     │
    │                                    │
```

**PDO Transmission Types (Communication Parameter 0x1800 subindex 2):**

| Type | Value | Description                                     |
|------|-------|-------------------------------------------------|
| Synchronous cyclic   | 1–240 | Transmit every N SYNC messages          |
| Synchronous acyclic  | 0     | Transmit on change after SYNC           |
| RTR-only (sync)      | 252   | Only on RTR after SYNC                  |
| RTR-only (async)     | 253   | Only on RTR, anytime                    |
| Asynchronous         | 254   | Transmit on event (change/timer)        |
| Asynchronous         | 255   | Transmit on event (manufacturer-spec.)  |

### 9.3 TPDO Configuration for Event-Driven Position

```
  TPDO1 Communication Parameters (index 0x1800):
  ───────────────────────────────────────────────
  Sub 1:  COB-ID     = 0x183  (0x180 + node_id 3)
  Sub 2:  Tx Type    = 254    (asynchronous, event-driven)
  Sub 3:  Inhibit    = 10     (1 ms units → 10 ms minimum gap between PDOs)
  Sub 5:  Event Timer= 1000   (1 ms units → send at least every 1 s)

  TPDO1 Mapping Parameters (index 0x1A00):
  ─────────────────────────────────────────
  Sub 0:  Count      = 1      (one mapped object)
  Sub 1:  Mapping    = 0x60040020  → 0x6004, sub 0, 32 bits (position value)
```

### 9.4 Configuring TPDO via SDO (C++)

```cpp
/**
 * Configure TPDO1 for asynchronous (event-driven) position reporting.
 *
 * After this setup the encoder will autonomously transmit position
 * whenever the shaft moves (inhibit time guards bus load).
 *
 * @param node_id    Encoder CANopen node ID
 * @param inhibit_ms Minimum time between two PDOs in milliseconds
 * @param event_ms   Maximum interval (heartbeat-like); 0 = disable
 */
int encoder_configure_event_pdo(uint8_t  node_id,
                                 uint16_t inhibit_ms,
                                 uint16_t event_ms)
{
    int      rc;
    uint32_t cob_id;
    uint32_t mapping;

    /* Step 1: Disable TPDO1 while reconfiguring (set bit 31 of COB-ID) */
    cob_id = 0x80000180u | node_id;   /* valid=0, RTR=0, 11-bit */
    rc = sdo_write_u32(node_id, 0x1800, 0x01, cob_id);
    if (rc != 0) return rc;

    /* Step 2: Set transmission type = 254 (asynchronous, event-driven) */
    rc = sdo_write_u8(node_id, 0x1800, 0x02, 254);
    if (rc != 0) return rc;

    /* Step 3: Inhibit time in 100 µs units (inhibit_ms * 10) */
    rc = sdo_write_u16(node_id, 0x1800, 0x03, (uint16_t)(inhibit_ms * 10u));
    if (rc != 0) return rc;

    /* Step 4: Event timer in ms */
    rc = sdo_write_u16(node_id, 0x1800, 0x05, event_ms);
    if (rc != 0) return rc;

    /* Step 5: Set mapping: clear first */
    rc = sdo_write_u8(node_id, 0x1A00, 0x00, 0);
    if (rc != 0) return rc;

    /* Map 0x6004 sub 0 (32 bit) into TPDO1 byte 0..3 */
    mapping = (0x6004u << 16) | (0x00u << 8) | 0x20u;  /* index|sub|bits */
    rc = sdo_write_u32(node_id, 0x1A00, 0x01, mapping);
    if (rc != 0) return rc;

    /* Activate mapping */
    rc = sdo_write_u8(node_id, 0x1A00, 0x00, 1);
    if (rc != 0) return rc;

    /* Step 6: Re-enable TPDO1 (clear bit 31 of COB-ID) */
    cob_id = 0x00000180u | node_id;
    rc = sdo_write_u32(node_id, 0x1800, 0x01, cob_id);
    return rc;
}

/* ─────────────────────────────────────────────────────────────────
 * TPDO receive callback (registered with your CANopen stack)
 * ───────────────────────────────────────────────────────────────── */
void on_encoder_tpdo1(uint8_t node_id, const uint8_t *data, uint8_t dlc)
{
    if (dlc < 4) return;

    uint32_t position;
    /* Little-endian decoding as per CANopen */
    position  = (uint32_t)data[0];
    position |= (uint32_t)data[1] << 8;
    position |= (uint32_t)data[2] << 16;
    position |= (uint32_t)data[3] << 24;

    application_update_position(node_id, position);
}
```

### 9.5 Strategy Comparison

```
  Criterion           Master Polling (SDO)   RTR PDO          Event-Driven PDO
  ──────────────────  ─────────────────────  ───────────────  ─────────────────
  Bus frames/sample   2                      2                1 (only on change)
  Latency             High (SDO overhead)    Medium           Low (immediate)
  Bus load (moving)   Fixed                  Fixed            Higher (many msgs)
  Bus load (static)   Fixed                  Fixed            Near zero
  Master complexity   Low                    Low              Medium (callback)
  Missed events       Possible (poll gap)    Possible         None if inhibit ok
  Best for            Diagnostics, config    Semi-real-time   Motion control,
                                             at fixed rate    fast feedback loops
```

---

## 10. C/C++ Programming Examples

### 10.1 Complete Encoder Initialisation Sequence

```c
#include <stdint.h>
#include <stdbool.h>
#include "canopen_sdo.h"
#include "canopen_nmt.h"

typedef struct {
    uint8_t  node_id;
    uint32_t steps_per_rev;
    uint32_t num_revolutions;
    uint32_t total_range;
    bool     scaling_enabled;
    uint32_t scale_numerator;
    uint32_t scale_denominator;
    uint32_t lower_limit;
    uint32_t upper_limit;
} EncoderConfig;

/**
 * Full initialisation of a CiA 406 encoder.
 * Assumes encoder is in Pre-Operational NMT state.
 */
int encoder_init(const EncoderConfig *cfg)
{
    uint8_t  id  = cfg->node_id;
    uint16_t op  = 0x0000;   /* operating parameters accumulator */
    int      rc;

    /* ── 1. Read device identification ─────────────────────────── */
    uint32_t steps, revs;
    sdo_read_u32(id, 0x6501, 0x00, &steps);
    sdo_read_u32(id, 0x6502, 0x00, &revs);

    /* ── 2. Set measuring steps per revolution ──────────────────── */
    rc = sdo_write_u32(id, 0x6001, 0x00, cfg->steps_per_rev);
    if (rc) return rc;

    /* ── 3. Set total measuring range ───────────────────────────── */
    rc = sdo_write_u32(id, 0x6002, 0x00, cfg->total_range);
    if (rc) return rc;

    /* ── 4. Configure scaling if requested ──────────────────────── */
    if (cfg->scaling_enabled) {
        rc = sdo_write_u32(id, 0x6009, 0x00, cfg->scale_numerator);
        if (rc) return rc;
        rc = sdo_write_u32(id, 0x600A, 0x00, cfg->scale_denominator);
        if (rc) return rc;
        op |= (1u << 2);   /* bit 2: enable scaling */
    }

    /* ── 5. Set working area limits ─────────────────────────────── */
    rc = sdo_write_u32(id, 0x6005, 0x00, cfg->upper_limit);
    if (rc) return rc;
    rc = sdo_write_u32(id, 0x6006, 0x00, cfg->lower_limit);
    if (rc) return rc;

    /* ── 6. Enable limit alarms ─────────────────────────────────── */
    rc = sdo_write_u16(id, 0x6007, 0x00, 0x0003);
    if (rc) return rc;

    /* ── 7. Write operating parameters ──────────────────────────── */
    rc = sdo_write_u16(id, 0x6000, 0x00, op);
    if (rc) return rc;

    /* ── 8. Transition encoder to Operational ───────────────────── */
    nmt_send_command(id, NMT_CMD_START);

    return 0;
}
```

### 10.2 Cyclic Position Monitor with Alarm Detection

```cpp
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <thread>
#include "canopen_sdo.hpp"

class EncoderMonitor
{
public:
    EncoderMonitor(uint8_t node_id, uint32_t poll_ms)
        : node_id_(node_id), poll_ms_(poll_ms),
          last_position_(0), running_(false) {}

    void start()
    {
        running_ = true;
        while (running_) {
            poll_once();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(poll_ms_));
        }
    }

    void stop() { running_ = false; }

private:
    void poll_once()
    {
        uint32_t pos    = 0;
        uint16_t alarms = 0;

        /* Read position and alarm status in one pass */
        sdo_read_u32(node_id_, 0x6004, 0x00, &pos);
        sdo_read_u16(node_id_, 0x6008, 0x00, &alarms);

        /* Calculate velocity approximation */
        int32_t delta = static_cast<int32_t>(pos) -
                        static_cast<int32_t>(last_position_);
        last_position_ = pos;

        std::printf("[Node %u] pos=%-8u  delta=%+6d  alarms=0x%04X",
                    node_id_, pos, delta, alarms);

        if (alarms & 0x0001) std::printf("  [UPPER LIMIT!]");
        if (alarms & 0x0002) std::printf("  [LOWER LIMIT!]");
        if (alarms & 0x0008) std::printf("  [SPEED ALARM!]");

        std::printf("\n");
    }

    uint8_t  node_id_;
    uint32_t poll_ms_;
    uint32_t last_position_;
    bool     running_;
};

/* Usage:
 *   EncoderMonitor mon(3, 10);   // node 3, poll every 10 ms
 *   mon.start();
 */
```

### 10.3 PDO-Based Position Logging (Event-Driven)

```c
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "canopen_pdo.h"

#define MAX_ENCODER_NODES  8

static uint32_t g_position[MAX_ENCODER_NODES] = {0};
static uint32_t g_pdo_count[MAX_ENCODER_NODES] = {0};

/**
 * Registered as TPDO1 receive callback.
 * Called by CANopen stack on every incoming TPDO with
 * COB-ID range 0x181..0x187 (node IDs 1..7).
 */
void pdo_rx_callback(uint32_t cob_id,
                     const uint8_t *data,
                     uint8_t dlc)
{
    uint8_t node_id = (uint8_t)(cob_id & 0x7F);

    if (node_id == 0 || node_id >= MAX_ENCODER_NODES) return;
    if (dlc < 4) return;

    uint32_t pos = (uint32_t)data[0]
                 | (uint32_t)data[1] << 8
                 | (uint32_t)data[2] << 16
                 | (uint32_t)data[3] << 24;

    uint32_t prev = g_position[node_id];
    g_position[node_id] = pos;
    g_pdo_count[node_id]++;

    /* Log only on significant change (> 10 counts) */
    if ((pos > prev && pos - prev > 10) ||
        (pos < prev && prev - pos > 10))
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        printf("[%ld.%03ld] Node %u: pos=%u (was %u, delta=%+d)\n",
               ts.tv_sec, ts.tv_nsec / 1000000L,
               node_id, pos, prev,
               (int32_t)(pos - prev));
    }
}

void print_pdo_statistics(void)
{
    printf("\n=== PDO Statistics ===\n");
    for (int i = 1; i < MAX_ENCODER_NODES; i++) {
        if (g_pdo_count[i] > 0) {
            printf("  Node %d: %u PDOs received, last pos = %u\n",
                   i, g_pdo_count[i], g_position[i]);
        }
    }
}
```

---

## 11. Summary

CiA 406 provides a **complete, standardised framework** for integrating rotary and linear
encoders into CANopen networks. The key design principle is a clean separation between
the physical measurement domain and the application domain, with the encoder handling
all unit conversion and safety monitoring internally.

### Core Concepts at a Glance

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                     CiA 406 Encoder Profile                     │
  ├─────────────────┬───────────────────────────────────────────────┤
  │ Object          │ Purpose                                       │
  ├─────────────────┼───────────────────────────────────────────────┤
  │ 0x6004          │ Position value (primary output, RO)           │
  │ 0x6001 / 0x6002 │ Resolution & total range configuration        │
  │ 0x6003          │ Preset — define logical zero at any position  │
  │ 0x6009 / 0x600A │ Scaling numerator / denominator               │
  │ 0x6005 / 0x6006 │ Upper / lower working area limits             │
  │ 0x6007 / 0x6008 │ Alarm control & alarm status                  │
  │ 0x6501 / 0x6502 │ Read-only: hw resolution & revolution count   │
  └─────────────────┴───────────────────────────────────────────────┘
```

### Design Decisions

**Singleturn vs. Multiturn:** Choose multiturn when the application travel spans more
than one revolution and absolute position must be maintained across power cycles.
Multiturn encoders include a battery-backed or gearbox-based revolution counter.

**Preset:** Always perform a preset after mechanical installation to align the logical
zero with the application's reference point. Use edge-triggered preset (write enable bit,
write value, clear enable bit) for maximum compatibility.

**Scaling:** Enable scaling whenever the application works in engineering units. This
offloads the conversion from the master CPU and keeps position values human-readable.
All limit and alarm comparisons inside the encoder automatically use the scaled unit.

**Alarm strategy:** Use the alarm control register (0x6007) to enable only the alarms
relevant to your application to avoid false positives. Always register an Emergency
callback in addition to polling the alarm status register.

**PDO strategy:**

```
  Application type                Recommended strategy
  ──────────────────────────────  ────────────────────────────────────────
  Slow process (> 100 ms cycle)   SDO polling (simplest, lowest overhead)
  Fixed control loop (e.g. 10ms)  Synchronous TPDO (type 1, after SYNC)
  Motion control / servo          Asynchronous TPDO (type 254) + inhibit
  Diagnostics / commissioning     SDO on demand
  Multi-encoder systems           Mix: async PDO + event timer fallback
```

The combination of **scaling**, **preset**, and **event-driven PDOs** makes CiA 406
encoders nearly plug-and-play: configure once at startup via SDO, then receive
continuous position updates via TPDO with no master polling overhead at all.

---

*Document based on CiA 406 — CANopen Device Profile for Encoders (Edition 3.x).
All object indices are standard CiA 406; implementation details may vary by vendor —
always consult the device's EDS file and product manual.*