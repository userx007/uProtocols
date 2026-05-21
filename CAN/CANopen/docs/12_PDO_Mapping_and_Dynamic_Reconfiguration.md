# 12. PDO Mapping & Dynamic Reconfiguration

**Architecture & Encoding** — ASCII diagrams show how the Object Dictionary splits into Communication Parameter objects (`0x1800/0x1400`) and Mapping Parameter objects (`0x1A00/0x1600`), and how each 32-bit mapping entry encodes `Index:SubIndex:BitLength`.
**Static vs. Dynamic Mapping** — Static mapping (compile-time OD init in C) vs. runtime SDO-driven reconfiguration, with a motivating diagram showing payload waste without mapping control.
**The 5-Step Procedure** — A detailed ASCII flowchart of the mandatory sequence:
```
Disable PDO → Clear count → Write entries → Set count → Enable PDO
```
with a fully correct C implementation including safe error-path PDO re-enable via `goto`.

**Mixed-Type Packing** — Shows how to combine `UINT16`, `INT16`, `INT32` in one frame with a visual byte-layout diagram, plus a bit-level packing example and a `pdo_map_validate()` helper.
**Dummy Entries** — Table of standard dummy OD indices (`0x0001–0x0007`), a usage scenario showing forward-compatible frame layouts, and the code pattern to later replace a dummy with a real variable.
**Async State Machine** — A non-blocking C state machine safe for FreeRTOS tasks or bare-metal loops, tracking each step with an enum and a reusable SDO request context.
**C++ PdoMapper Class** — A C++17 fluent-builder class with `.map()`, `.pad_u16()`, `.is_valid()`, `.apply()`, and `.print_layout()`, covering both TPDO and RPDO remapping.
**Error Handling** — Table of common SDO abort codes (`0x06040041`, `0x06040042`, etc.) with causes and fixes, plus an ASCII checklist of the most common mistakes.

> **CANopen Application Layer | CiA 301 v4.2**
> Topics: Static vs. dynamic mapping · Mapping procedure · Mixed-type packing ·
> Dummy entries · Runtime remapping via SDO

---

## Table of Contents

1. [Introduction & Motivation](#1-introduction--motivation)
2. [CANopen PDO Architecture Recap](#2-canopen-pdo-architecture-recap)
3. [The Object Dictionary Entries for PDO Mapping](#3-the-object-dictionary-entries-for-pdo-mapping)
4. [Static PDO Mapping](#4-static-pdo-mapping)
5. [Dynamic PDO Mapping](#5-dynamic-pdo-mapping)
6. [The Mandatory 5-Step Mapping Procedure](#6-the-mandatory-5-step-mapping-procedure)
7. [Mixed-Type Packing](#7-mixed-type-packing)
8. [Dummy Mapping Entries](#8-dummy-mapping-entries)
9. [Runtime Remapping via SDO in C](#9-runtime-remapping-via-sdo-in-c)
10. [Complete C++ Class: PDO Mapper](#10-complete-c-class-pdo-mapper)
11. [Error Handling & Abort Codes](#11-error-handling--abort-codes)
12. [Summary](#12-summary)

---

## 1. Introduction & Motivation

A **Process Data Object (PDO)** carries up to 8 bytes of real-time process data
across the CANbus with minimal overhead — typically a single CAN frame without
protocol headers.  The *mapping* defines which Object Dictionary (OD) variables
are packed into those 8 bytes, their order, and their bit-widths.

**Why does mapping matter?**

```
Without mapping control:        With optimal mapping:
+---------+---------+----+      +------+------+------+------+
| Var A   |padding  |... |      | VarA | VarB | VarC | VarD |
| 4 bytes | 4 bytes |    |      | 2B   | 2B   | 2B   | 2B   |
+---------+---------+----+      +------+------+------+------+
   Wastes 50% of payload           Uses all 8 bytes efficiently
```

Applications that start with a fixed sensor set and later expand — or that switch
between operating modes — need **dynamic remapping at runtime** without power-cycling
the node.

---

## 2. CANopen PDO Architecture Recap

Each PDO is described by two sets of OD entries:

```
  Object Dictionary (Node)
  ┌────────────────────────────────────────────────────────────┐
  │                                                            │
  │  Communication Parameter    Mapping Parameter              │
  │  ┌──────────────────────┐   ┌──────────────────────────┐   │
  │  │ 0x1400..0x15FF (RPDO)│   │ 0x1600..0x17FF (RPDO map)│   │
  │  │ 0x1800..0x19FF (TPDO)│   │ 0x1A00..0x1BFF (TPDO map)│   │
  │  │                      │   │                          │   │
  │  │ Sub 0: Highest sub   │   │ Sub 0: # mapped objects  │   │
  │  │ Sub 1: COB-ID        │   │ Sub 1: Mapping entry 1   │   │
  │  │ Sub 2: Tx type       │   │ Sub 2: Mapping entry 2   │   │
  │  │ Sub 3: Inhibit time  │   │  ...                     │   │
  │  │ Sub 4: Event timer   │   │ Sub N: Mapping entry N   │   │
  │  └──────────────────────┘   └──────────────────────────┘   │
  │                                                            │
  └────────────────────────────────────────────────────────────┘
```

Each **mapping entry** (32-bit value) encodes the mapped variable as:

```
  Bit 31      Bit 16   Bit 15    Bit 8   Bit 7    Bit 0
  ┌────────────────────┬──────────────────┬──────────────┐
  │    Index (16 bit)  │  Subindex (8 bit)│  Length(8bit)│
  │      0x6041        │       0x00       │     0x10     │
  └────────────────────┴──────────────────┴──────────────┘
                                                 └── bit-length (0x10 = 16 bits = 2 bytes)

  Encoded as:  (Index << 16) | (SubIndex << 8) | BitLength
  Example:     0x60410010   →  OD[0x6041][0x00], 16 bits
```

---

## 3. The Object Dictionary Entries for PDO Mapping

### 3.1 Communication Parameter Object (example: TPDO1 = 0x1800)

| Sub-index | Name          | Type   | Description                              |
|-----------|---------------|--------|------------------------------------------|
| 0x00      | Highest sub   | UINT8  | Number of supported sub-entries (= 0x05) |
| 0x01      | COB-ID        | UINT32 | CAN-ID + flags (bit31=valid, bit30=RTR)  |
| 0x02      | Trans. type   | UINT8  | 0=sync-acyclic, 1..240=every Nth SYNC, 254/255=event |
| 0x03      | Inhibit time  | UINT16 | Minimum interval in 100 µs steps         |
| 0x05      | Event timer   | UINT16 | Maximum interval in ms (0=off)           |

### 3.2 Mapping Parameter Object (example: TPDO1 map = 0x1A00)

| Sub-index | Name         | Type   | Description                              |
|-----------|--------------|--------|------------------------------------------|
| 0x00      | Map count    | UINT8  | Number of currently active mappings (0..8) |
| 0x01..08  | Map entry N  | UINT32 | Encoded mapping: index/subindex/length   |

**Critical constraint:** Mapped variables must be PDO-mappable (their OD entry
must have the PDO mapping access flag set). The total bit-length of all entries
must not exceed 64 bits (8 bytes).

---

## 4. Static PDO Mapping

Static mapping is configured **at firmware compile time** or via EDS (Electronic
Data Sheet). It cannot be changed while the node is running. This approach is used
when the application is fixed and performance or simplicity is paramount.

```c
/* ---------- static_pdo_map.c ----------
 * Compile-time PDO mapping for a simple motor node.
 * TPDO1 (0x1A00): Status word (2B) + Actual position (4B) + Actual velocity (2B)
 * --------------------------------------- */

#include <stdint.h>
#include "canopen_od.h"   /* Vendor OD access API */

/* Fixed mapping entries — embedded in OD at build time */
static const uint32_t tpdo1_map[3] = {
    0x60410010u,   /* 0x6041:00 StatusWord  — 16 bits */
    0x60640020u,   /* 0x6064:00 ActualPos   — 32 bits */
    0x606C0010u,   /* 0x606C:00 ActualVel   — 16 bits */
};

/* OD initialisation — called once at startup */
void od_init_static_pdo(void)
{
    /* Map count = 3 */
    od_write_u8(0x1A00u, 0x00u, 3u);

    od_write_u32(0x1A00u, 0x01u, tpdo1_map[0]);
    od_write_u32(0x1A00u, 0x02u, tpdo1_map[1]);
    od_write_u32(0x1A00u, 0x03u, tpdo1_map[2]);

    /* COB-ID: 0x180 + NodeID, PDO valid (bit31=0), no RTR */
    od_write_u32(0x1800u, 0x01u, 0x00000181u);  /* NodeID = 1 */

    /* Transmission type: event-driven (0xFF) */
    od_write_u8(0x1800u, 0x02u, 0xFFu);
}
```

The resulting CAN frame layout for TPDO1:

```
  CAN Frame  (COB-ID 0x181, DLC=8)
  ┌────────────────────────────────────────────────────────┐
  │ Byte 0 │ Byte 1 │ B2 │ B3 │ B4 │ B5 │ Byte 6│ Byte 7 │
  ├────────────────────────────────────────────────────────┤
  │   StatusWord (0x6041)   │  ActualPos (0x6064)  │ActVel │
  │       16 bits           │       32 bits        │16 bit │
  └────────────────────────────────────────────────────────┘
```

---

## 5. Dynamic PDO Mapping

Dynamic mapping allows reconfiguration **at runtime** via SDO writes, without
reprogramming the device.  The CANopen standard mandates a strict sequence (see
Section 6) that must be followed to avoid data inconsistency on the network.

**Typical use cases:**

- Switching between *position mode* and *torque mode* (different process variables)
- Reducing PDO payload during bandwidth-constrained multi-node bus loading
- Adding a newly installed sensor to an existing PDO after field upgrade
- Diagnostic mode that temporarily maps extra variables

```
  Normal Mode                    Diagnostic Mode
  ┌──────────────────────┐       ┌──────────────────────────────┐
  │ TPDO1 (0x181)        │       │ TPDO1 (0x181)                │
  │ [StatusWd][ActPos]   │  ───► │ [StatusWd][ActPos][TempSens] │
  │  2 bytes   4 bytes   │       │  2 bytes   4 bytes  2 bytes  │
  └──────────────────────┘       └──────────────────────────────┘
         6 bytes used                      8 bytes used
```

---

## 6. The Mandatory 5-Step Mapping Procedure

CiA 301 defines a **strict 5-step procedure** that must be executed in exact order.
Violating the order causes abort codes (typically `0x06040041`).

```
  ┌─────────────────────────────────────────────────────────────────────┐
  │                   5-Step PDO Mapping Procedure                      │
  │                                                                     │
  │  Step 1: DISABLE PDO                                                │
  │  ┌──────────────────────────────────────────────────────────────┐   │
  │  │ Write 0x1800:01  →  COB-ID | 0x80000000  (set invalid bit)   │   │
  │  └──────────────────────────────────────────────────────────────┘   │
  │                            │                                        │
  │                            ▼                                        │
  │  Step 2: CLEAR MAP COUNT                                            │
  │  ┌──────────────────────────────────────────────────────────────┐   │
  │  │ Write 0x1A00:00  →  0x00  (zero out number of mapped objs)   │   │
  │  └──────────────────────────────────────────────────────────────┘   │
  │                            │                                        │
  │                            ▼                                        │
  │  Step 3: WRITE MAPPING ENTRIES                                      │
  │  ┌──────────────────────────────────────────────────────────────┐   │
  │  │ Write 0x1A00:01  →  entry_1  (index:subidx:bitlen)           │   │
  │  │ Write 0x1A00:02  →  entry_2                                  │   │
  │  │    ...                                                       │   │
  │  │ Write 0x1A00:N   →  entry_N                                  │   │
  │  └──────────────────────────────────────────────────────────────┘   │
  │                            │                                        │
  │                            ▼                                        │
  │  Step 4: SET MAP COUNT                                              │
  │  ┌──────────────────────────────────────────────────────────────┐   │
  │  │ Write 0x1A00:00  →  N  (activate N mapped objects)           │   │
  │  └──────────────────────────────────────────────────────────────┘   │
  │                            │                                        │
  │                            ▼                                        │
  │  Step 5: ENABLE PDO                                                 │
  │  ┌──────────────────────────────────────────────────────────────┐   │
  │  │ Write 0x1800:01  →  COB-ID & ~0x80000000  (clear inv. bit)   │   │
  │  └──────────────────────────────────────────────────────────────┘   │
  └─────────────────────────────────────────────────────────────────────┘
```

### 6.1 C Implementation of the 5-Step Procedure (Master side via SDO)

```c
/* ---------- pdo_remap.c ----------
 * Master-side PDO dynamic remapping via expedited SDO.
 * Targets a remote node; uses a blocking SDO client API.
 * --------------------------------- */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "sdo_client.h"   /* sdo_write_u8(), sdo_write_u32(), etc. */
#include "pdo_remap.h"

/* Convenience macro: build a mapping entry */
#define PDO_MAP_ENTRY(idx, sub, bits)  \
    (((uint32_t)(idx) << 16u) | ((uint32_t)(sub) << 8u) | (uint32_t)(bits))

/* Maximum entries per PDO (CANopen limit: 8 bytes → max 64 bits) */
#define PDO_MAX_ENTRIES  8u

/* -----------------------------------------------------------------------
 * pdo_remap_tpdo
 *
 * Performs the complete 5-step procedure for one TPDO on a remote node.
 *
 * @param node_id       CANopen node-ID of the target device
 * @param pdo_num       PDO number: 1..4  (1 → 0x1800/0x1A00)
 * @param entries       Array of 32-bit mapping entries (PDO_MAP_ENTRY())
 * @param entry_count   Number of entries (1..8)
 *
 * @return  0   on success
 *         -1   on SDO abort / parameter error
 * ----------------------------------------------------------------------- */
int pdo_remap_tpdo(uint8_t  node_id,
                   uint8_t  pdo_num,
                   const uint32_t *entries,
                   uint8_t  entry_count)
{
    if (!entries || entry_count == 0u || entry_count > PDO_MAX_ENTRIES) {
        return -1;
    }
    if (pdo_num < 1u || pdo_num > 4u) {
        return -1;
    }

    /* OD base indices for comm. param and mapping param */
    const uint16_t comm_idx = (uint16_t)(0x1800u + pdo_num - 1u);
    const uint16_t map_idx  = (uint16_t)(0x1A00u + pdo_num - 1u);

    int rc;

    /* ── Step 1: Disable PDO (set invalid bit in COB-ID) ─────────────── */
    uint32_t cob_id = 0u;
    rc = sdo_read_u32(node_id, comm_idx, 0x01u, &cob_id);
    if (rc != 0) { return -1; }

    cob_id |= 0x80000000uL;                    /* set bit 31 = invalid */
    rc = sdo_write_u32(node_id, comm_idx, 0x01u, cob_id);
    if (rc != 0) { return -1; }

    /* ── Step 2: Clear map count ─────────────────────────────────────── */
    rc = sdo_write_u8(node_id, map_idx, 0x00u, 0x00u);
    if (rc != 0) { goto re_enable; }

    /* ── Step 3: Write mapping entries ───────────────────────────────── */
    for (uint8_t i = 0u; i < entry_count; ++i) {
        rc = sdo_write_u32(node_id, map_idx,
                           (uint8_t)(i + 1u),   /* sub-index 1..8 */
                           entries[i]);
        if (rc != 0) { goto re_enable; }
    }

    /* ── Step 4: Set map count ───────────────────────────────────────── */
    rc = sdo_write_u8(node_id, map_idx, 0x00u, entry_count);
    if (rc != 0) { goto re_enable; }

re_enable:
    /* ── Step 5: Re-enable PDO (clear invalid bit) ───────────────────── */
    cob_id &= ~0x80000000uL;
    (void)sdo_write_u32(node_id, comm_idx, 0x01u, cob_id);

    return rc;
}

/* -----------------------------------------------------------------------
 * Example usage
 * ----------------------------------------------------------------------- */
static void example_remap_to_diagnostic_mode(void)
{
    /* Node 1, TPDO1: StatusWord + ActualPosition + TemperatureSensor */
    const uint32_t diag_map[] = {
        PDO_MAP_ENTRY(0x6041u, 0x00u, 16u),   /* StatusWord   2 bytes */
        PDO_MAP_ENTRY(0x6064u, 0x00u, 32u),   /* ActualPos    4 bytes */
        PDO_MAP_ENTRY(0x2100u, 0x01u, 16u),   /* TempSensor   2 bytes */
    };

    int rc = pdo_remap_tpdo(1u, 1u, diag_map, 3u);
    if (rc != 0) {
        /* handle error — see abort codes in Section 11 */
    }
}
```

---

## 7. Mixed-Type Packing

PDO mapping allows mixing variables of different types and widths within one PDO,
as long as the total does not exceed 64 bits and all entries are byte-aligned (or
bit-aligned if the device supports bit mapping).

### 7.1 Byte-Aligned Packing Example

```
  TPDO2 — Mixed sensor data (8 bytes total)
  ┌───────────┬──────────┬────────────────────────┬────────┐
  │ B0  │ B1  │ B2 │ B3  │  B4  │  B5  │  B6  │ B7│        |
  ├───────────┼──────────┼────────────────────────┼────────┤
  │  Ctrl     │  Torque  │    Encoder (INT32)     │ Temp   │
  │  UINT16   │  INT16   │        INT32           │ INT16  │
  │  0x6040   │  0x6071  │       0x6063           │ 0x2100 │
  └───────────┴──────────┴────────────────────────┴────────┘
       2B          2B              4B                  — wait!

  2 + 2 + 4 = 8 bytes → Temp (2B) does NOT fit!
  Must reduce: drop Temp, or map a 1-byte value instead.
```

**Correct packing for the above scenario:**

```c
/* Mixed-type packing: 2B + 2B + 4B = 8 bytes exactly */
const uint32_t tpdo2_map[] = {
    PDO_MAP_ENTRY(0x6040u, 0x00u, 16u),  /* ControlWord  — 2 bytes */
    PDO_MAP_ENTRY(0x6071u, 0x00u, 16u),  /* TargetTorque — 2 bytes */
    PDO_MAP_ENTRY(0x6063u, 0x00u, 32u),  /* Enc.Position — 4 bytes */
};
/* Total: 16 + 16 + 32 = 64 bits = 8 bytes ✓ */
```

### 7.2 Bit-Level Packing (device-specific)

Some implementations support sub-byte granularity, typically for BOOLEAN or
bit-field objects:

```
  Bit mapping example (8 bytes = 64 bits):
  ┌─────────────────────────────────────────────────────────────────┐
  │ b63..b48 │ b47..b32 │ b31..b16 │ b15 │ b14 │ b13..b0  │ b7..b0  │
  │  INT16   │  INT16   │  UINT16  │BOOL │BOOL │ padding  │ UINT8   │
  │  0x3001  │  0x3002  │  0x6041  │f.12 │f.13 │ dummy    │ 0x3005  │
  └─────────────────────────────────────────────────────────────────┘
```

```c
/* Bit-level mapping entries */
const uint32_t tpdo3_bit_map[] = {
    PDO_MAP_ENTRY(0x3001u, 0x00u, 16u),  /* INT16    — 16 bits */
    PDO_MAP_ENTRY(0x3002u, 0x00u, 16u),  /* INT16    — 16 bits */
    PDO_MAP_ENTRY(0x6041u, 0x00u, 16u),  /* StatusWd — 16 bits */
    PDO_MAP_ENTRY(0x6041u, 0x00u,  1u),  /* Bit 0 of StatusWd — 1 bit  */
    PDO_MAP_ENTRY(0x6041u, 0x00u,  1u),  /* Bit 1 of StatusWd — 1 bit  */
    PDO_MAP_ENTRY(0x0000u, 0x00u, 14u),  /* Dummy gap — 14 bits padding */
    PDO_MAP_ENTRY(0x3005u, 0x00u,  8u),  /* UINT8    —  8 bits */
};
/* 16+16+16+1+1+14+8 = 72 bits → TOO LARGE! Must recalculate. */

/*
 * Practical rule: when mixing bit-width entries, sum all bit-lengths
 * before submitting. Use a helper:
 */
static bool pdo_map_validate(const uint32_t *map, uint8_t count)
{
    uint32_t total_bits = 0u;
    for (uint8_t i = 0u; i < count; ++i) {
        total_bits += (map[i] & 0xFFu);   /* lower byte = bit-length */
    }
    return (total_bits <= 64u);
}
```

---

## 8. Dummy Mapping Entries

A **dummy entry** (also called *dummy object* or *gap entry*) occupies space in
the PDO payload without mapping a real OD variable. This is used to:

- Pad/align data for word or dword boundaries
- Reserve bytes for future use without changing existing receivers
- Fill a gap left after removing a variable during remapping

Dummy objects are defined in OD sub-range `0x0001..0x001F` with a fixed type
but no backing data. Common ones:

```
  Index   │ Data type    │ Bit size   │ Purpose
  ────────┼──────────────┼────────────┼──────────────────────────
  0x0001  │ BOOLEAN      │  1 bit     │ single bit gap
  0x0002  │ INTEGER8     │  8 bits    │ 1 byte gap
  0x0003  │ INTEGER16    │ 16 bits    │ 2 byte gap
  0x0004  │ INTEGER32    │ 32 bits    │ 4 byte gap
  0x0005  │ UNSIGNED8    │  8 bits    │ 1 byte gap (unsigned)
  0x0006  │ UNSIGNED16   │ 16 bits    │ 2 byte gap (unsigned)
  0x0007  │ UNSIGNED32   │ 32 bits    │ 4 byte gap (unsigned)
```

Dummy mapping is enabled by setting bit 0 of the node's OD entry at 0x1000
(device type) — actually, each dummy object's entry must have the PDO mappable
flag and dummy support must be indicated. Check vendor EDS for supported dummies.

### 8.1 Dummy Mapping in C

```c
/* ---------- dummy_mapping.c ----------
 * Scenario: TPDO1 currently maps StatusWord (2B) + ActualPos (4B).
 * We want to add TargetVelocity later, but the receiver firmware
 * already expects a fixed 8-byte frame.
 * Solution: pad with 2 dummy bytes so frame size stays at 8 bytes.
 * ------------------------------------- */

/* Dummy object: 0x0006 = UNSIGNED16 dummy, 16 bits */
#define DUMMY_U16  PDO_MAP_ENTRY(0x0006u, 0x00u, 16u)
#define DUMMY_U8   PDO_MAP_ENTRY(0x0005u, 0x00u,  8u)

static void map_tpdo1_with_dummy(uint8_t node_id)
{
    /*
     * Layout:
     *  B0-B1: StatusWord  (0x6041, 16 bit)
     *  B2-B5: ActualPos   (0x6064, 32 bit)
     *  B6-B7: [DUMMY]     reserved / future TargetVelocity (16 bit)
     *
     *  ┌────────┬────────────────────┬─────────────┐
     *  │ B0  B1 │  B2   B3   B4   B5 │  B6     B7  │
     *  ├────────┼────────────────────┼─────────────┤
     *  │StatWrd │    Actual Pos      │  [dummy]    │
     *  └────────┴────────────────────┴─────────────┘
     */
    const uint32_t map[] = {
        PDO_MAP_ENTRY(0x6041u, 0x00u, 16u),  /* StatusWord */
        PDO_MAP_ENTRY(0x6064u, 0x00u, 32u),  /* ActualPos  */
        DUMMY_U16,                            /* 2-byte pad */
    };

    int rc = pdo_remap_tpdo(node_id, 1u, map, 3u);
    if (rc == 0) {
        /* Later, replace dummy with real variable without changing DLC */
    }
}

/* When TargetVelocity sensor becomes available, replace dummy: */
static void map_tpdo1_replace_dummy(uint8_t node_id)
{
    const uint32_t map[] = {
        PDO_MAP_ENTRY(0x6041u, 0x00u, 16u),  /* StatusWord     */
        PDO_MAP_ENTRY(0x6064u, 0x00u, 32u),  /* ActualPos      */
        PDO_MAP_ENTRY(0x606Cu, 0x00u, 16u),  /* ActualVelocity — replaces dummy */
    };

    (void)pdo_remap_tpdo(node_id, 1u, map, 3u);
}
```

---

## 9. Runtime Remapping via SDO in C

This section shows a complete, production-style flow of runtime remapping from a
CANopen master perspective.  The example includes a state-machine approach that is
safe to use in an event-loop or RTOS task context.

### 9.1 Non-Blocking SDO Remapping State Machine

```c
/* ---------- pdo_remap_async.c ----------
 * Asynchronous PDO remapper using a state machine.
 * Suitable for FreeRTOS tasks or bare-metal event loops.
 * --------------------------------------- */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "sdo_client_async.h"   /* sdo_request_t, sdo_submit(), sdo_is_done() */
#include "pdo_remap_async.h"

typedef enum {
    REMAP_IDLE = 0,
    REMAP_STEP1_DISABLE,
    REMAP_STEP2_CLEAR_COUNT,
    REMAP_STEP3_WRITE_ENTRIES,
    REMAP_STEP4_SET_COUNT,
    REMAP_STEP5_ENABLE,
    REMAP_DONE,
    REMAP_ERROR,
} remap_state_t;

typedef struct {
    remap_state_t  state;
    uint8_t        node_id;
    uint16_t       comm_idx;
    uint16_t       map_idx;
    uint32_t       saved_cob_id;
    const uint32_t *entries;
    uint8_t        entry_count;
    uint8_t        current_entry;   /* index into entries[] during step 3 */
    sdo_request_t  sdo_req;         /* outstanding SDO request */
} remap_ctx_t;

/* -----------------------------------------------------------------------
 * pdo_remap_start  — kick off the state machine
 * ----------------------------------------------------------------------- */
void pdo_remap_start(remap_ctx_t    *ctx,
                     uint8_t         node_id,
                     uint8_t         pdo_num,
                     const uint32_t *entries,
                     uint8_t         entry_count)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->node_id     = node_id;
    ctx->comm_idx    = (uint16_t)(0x1800u + pdo_num - 1u);
    ctx->map_idx     = (uint16_t)(0x1A00u + pdo_num - 1u);
    ctx->entries     = entries;
    ctx->entry_count = entry_count;
    ctx->state       = REMAP_STEP1_DISABLE;

    /* Initiate read of current COB-ID before disabling */
    sdo_read_u32_async(&ctx->sdo_req, node_id, ctx->comm_idx, 0x01u);
    sdo_submit(&ctx->sdo_req);
}

/* -----------------------------------------------------------------------
 * pdo_remap_process  — call from main loop / task, returns true when done
 * ----------------------------------------------------------------------- */
bool pdo_remap_process(remap_ctx_t *ctx)
{
    if (!sdo_is_done(&ctx->sdo_req)) {
        return false;   /* SDO still in flight, come back later */
    }

    if (sdo_has_error(&ctx->sdo_req)) {
        ctx->state = REMAP_ERROR;
        return true;
    }

    switch (ctx->state) {

    case REMAP_STEP1_DISABLE:
        ctx->saved_cob_id = sdo_get_u32_result(&ctx->sdo_req);
        /* Set invalid bit and write back */
        sdo_write_u32_async(&ctx->sdo_req, ctx->node_id, ctx->comm_idx, 0x01u,
                            ctx->saved_cob_id | 0x80000000uL);
        sdo_submit(&ctx->sdo_req);
        ctx->state = REMAP_STEP2_CLEAR_COUNT;
        break;

    case REMAP_STEP2_CLEAR_COUNT:
        sdo_write_u8_async(&ctx->sdo_req, ctx->node_id, ctx->map_idx, 0x00u, 0x00u);
        sdo_submit(&ctx->sdo_req);
        ctx->current_entry = 0u;
        ctx->state = REMAP_STEP3_WRITE_ENTRIES;
        break;

    case REMAP_STEP3_WRITE_ENTRIES:
        if (ctx->current_entry < ctx->entry_count) {
            sdo_write_u32_async(&ctx->sdo_req, ctx->node_id, ctx->map_idx,
                                (uint8_t)(ctx->current_entry + 1u),
                                ctx->entries[ctx->current_entry]);
            sdo_submit(&ctx->sdo_req);
            ctx->current_entry++;
            /* Stay in STEP3 until all entries written */
        } else {
            /* All entries written — advance to step 4 */
            sdo_write_u8_async(&ctx->sdo_req, ctx->node_id, ctx->map_idx,
                               0x00u, ctx->entry_count);
            sdo_submit(&ctx->sdo_req);
            ctx->state = REMAP_STEP4_SET_COUNT;
        }
        break;

    case REMAP_STEP4_SET_COUNT:
        /* Re-enable PDO */
        sdo_write_u32_async(&ctx->sdo_req, ctx->node_id, ctx->comm_idx, 0x01u,
                            ctx->saved_cob_id & ~0x80000000uL);
        sdo_submit(&ctx->sdo_req);
        ctx->state = REMAP_STEP5_ENABLE;
        break;

    case REMAP_STEP5_ENABLE:
        ctx->state = REMAP_DONE;
        return true;

    default:
        return true;   /* DONE or ERROR */
    }

    return false;
}

/* -----------------------------------------------------------------------
 * Usage in an RTOS task
 * ----------------------------------------------------------------------- */
static void remap_task(void *arg)
{
    (void)arg;

    static const uint32_t mode_a_map[] = {
        PDO_MAP_ENTRY(0x6041u, 0x00u, 16u),  /* StatusWord */
        PDO_MAP_ENTRY(0x6064u, 0x00u, 32u),  /* ActualPos  */
        PDO_MAP_ENTRY(0x606Cu, 0x00u, 16u),  /* ActualVel  */
    };

    static remap_ctx_t ctx;

    pdo_remap_start(&ctx, /*node=*/2u, /*pdo=*/1u, mode_a_map, 3u);

    while (!pdo_remap_process(&ctx)) {
        task_delay_ms(1u);           /* yield to other tasks */
    }

    if (ctx.state == REMAP_ERROR) {
        log_error("PDO remap failed, SDO abort=0x%08X",
                  sdo_get_abort_code(&ctx.sdo_req));
    }
}
```

### 9.2 RPDO Remapping (Receive Side)

RPDOs follow the identical 5-step procedure but use the RPDO OD ranges:

```c
/* RPDO mapping uses 0x1400..0x15FF (comm) and 0x1600..0x17FF (map) */

int pdo_remap_rpdo(uint8_t  node_id,
                   uint8_t  pdo_num,
                   const uint32_t *entries,
                   uint8_t  entry_count)
{
    if (pdo_num < 1u || pdo_num > 4u) { return -1; }

    /* Note: RPDO comm at 0x1400, map at 0x1600 */
    const uint16_t comm_idx = (uint16_t)(0x1400u + pdo_num - 1u);
    const uint16_t map_idx  = (uint16_t)(0x1600u + pdo_num - 1u);

    /* The procedure is IDENTICAL to TPDO — just different base indices */
    /* ... (same 5-step logic) ... */

    (void)comm_idx;
    (void)map_idx;
    return 0;
}
```

---

## 10. Complete C++ Class: PDO Mapper

```cpp
// ---------- PdoMapper.hpp ----------
// C++17 class encapsulating PDO mapping logic for a CANopen master.
// Provides a fluent builder API and blocking + async execution.
// -----------------------------------

#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

/// Represents a single PDO mapping entry.
struct PdoEntry {
    uint16_t index;
    uint8_t  subindex;
    uint8_t  bit_length;

    constexpr uint32_t encode() const noexcept {
        return (static_cast<uint32_t>(index)    << 16u)
             | (static_cast<uint32_t>(subindex) <<  8u)
             |  static_cast<uint32_t>(bit_length);
    }

    /// Creates a dummy entry for padding.
    static constexpr PdoEntry dummy_u8()  { return {0x0005u, 0x00u,  8u}; }
    static constexpr PdoEntry dummy_u16() { return {0x0006u, 0x00u, 16u}; }
    static constexpr PdoEntry dummy_u32() { return {0x0007u, 0x00u, 32u}; }
};

/// SDO write callback (blocking version).
using SdoWriteFn = std::function<int(uint8_t node, uint16_t idx,
                                     uint8_t  sub, uint32_t val,
                                     uint8_t  width)>;
using SdoReadFn  = std::function<int(uint8_t node, uint16_t idx,
                                     uint8_t  sub, uint32_t &val)>;

// ---------- PdoMapper.cpp ----------

class PdoMapper {
public:
    static constexpr uint8_t kMaxEntries = 8u;

    explicit PdoMapper(uint8_t node_id, SdoWriteFn write_fn, SdoReadFn read_fn)
        : node_id_(node_id),
          sdo_write_(std::move(write_fn)),
          sdo_read_(std::move(read_fn))
    {}

    // ── Fluent builder ──────────────────────────────────────────────────────

    PdoMapper& map(uint16_t index, uint8_t subindex, uint8_t bits) {
        if (entries_.size() < kMaxEntries && total_bits_ + bits <= 64u) {
            entries_.push_back({index, subindex, bits});
            total_bits_ += bits;
        }
        return *this;
    }

    PdoMapper& pad_u8()  { return map(0x0005u, 0x00u,  8u); }
    PdoMapper& pad_u16() { return map(0x0006u, 0x00u, 16u); }
    PdoMapper& pad_u32() { return map(0x0007u, 0x00u, 32u); }

    PdoMapper& clear_entries() {
        entries_.clear();
        total_bits_ = 0u;
        return *this;
    }

    // ── Validation ──────────────────────────────────────────────────────────

    [[nodiscard]] bool is_valid() const noexcept {
        return !entries_.empty()
            && entries_.size() <= kMaxEntries
            && total_bits_ <= 64u;
    }

    [[nodiscard]] uint32_t total_bits()  const noexcept { return total_bits_; }
    [[nodiscard]] uint32_t total_bytes() const noexcept { return (total_bits_ + 7u) / 8u; }

    // ── Apply mapping ───────────────────────────────────────────────────────

    /**
     * Execute the 5-step PDO remapping procedure (blocking).
     *
     * @param pdo_num  PDO index 1..4
     * @param is_tpdo  true = TPDO (0x18xx/0x1Axx), false = RPDO (0x14xx/0x16xx)
     * @return 0 on success, non-zero SDO abort code on failure
     */
    int apply(uint8_t pdo_num, bool is_tpdo = true) {
        if (!is_valid() || pdo_num < 1u || pdo_num > 4u) {
            return -1;
        }

        const uint16_t comm_base = is_tpdo ? 0x1800u : 0x1400u;
        const uint16_t map_base  = is_tpdo ? 0x1A00u : 0x1600u;

        const uint16_t comm_idx = static_cast<uint16_t>(comm_base + pdo_num - 1u);
        const uint16_t map_idx  = static_cast<uint16_t>(map_base  + pdo_num - 1u);

        uint32_t cob_id = 0u;
        int rc = sdo_read_(node_id_, comm_idx, 0x01u, cob_id);
        if (rc != 0) { return rc; }

        // Step 1: Disable
        rc = sdo_write_(node_id_, comm_idx, 0x01u, cob_id | 0x80000000uL, 4u);
        if (rc != 0) { sdo_write_(node_id_, comm_idx, 0x01u, cob_id, 4u); return rc; }

        // Step 2: Clear count
        rc = sdo_write_(node_id_, map_idx, 0x00u, 0x00u, 1u);
        if (rc != 0) { goto restore; }

        // Step 3: Write entries
        for (uint8_t i = 0u; i < static_cast<uint8_t>(entries_.size()); ++i) {
            rc = sdo_write_(node_id_, map_idx,
                            static_cast<uint8_t>(i + 1u),
                            entries_[i].encode(), 4u);
            if (rc != 0) { goto restore; }
        }

        // Step 4: Set count
        rc = sdo_write_(node_id_, map_idx, 0x00u,
                        static_cast<uint32_t>(entries_.size()), 1u);

    restore:
        // Step 5: Re-enable (always executed)
        (void)sdo_write_(node_id_, comm_idx, 0x01u,
                         cob_id & ~0x80000000uL, 4u);
        return rc;
    }

    // ── Diagnostic dump ─────────────────────────────────────────────────────

    void print_layout(bool use_hex = true) const {
        uint32_t bit_offset = 0u;
        for (const auto& e : entries_) {
            if (use_hex) {
                printf("  [%2u..%2u]  0x%04X:0x%02X  %u bits\n",
                       bit_offset, bit_offset + e.bit_length - 1u,
                       e.index, e.subindex, e.bit_length);
            }
            bit_offset += e.bit_length;
        }
        printf("  Total: %u bits (%u bytes)\n", total_bits_, total_bytes());
    }

private:
    uint8_t              node_id_;
    SdoWriteFn           sdo_write_;
    SdoReadFn            sdo_read_;
    std::vector<PdoEntry> entries_;
    uint32_t             total_bits_{0u};
};

// ── Usage example ────────────────────────────────────────────────────────────

/*
int main()
{
    // Assume sdo_write/sdo_read are connected to a real CANopen stack
    PdoMapper mapper(1u, sdo_write_blocking, sdo_read_blocking);

    // Build mapping for TPDO1: position mode
    mapper
        .map(0x6041u, 0x00u, 16u)   // StatusWord
        .map(0x6064u, 0x00u, 32u)   // ActualPosition
        .map(0x606Cu, 0x00u, 16u);  // ActualVelocity

    if (mapper.is_valid()) {
        int rc = mapper.apply(1u, true);   // TPDO1
    }

    // Switch to torque mode — clear and remap
    mapper.clear_entries()
        .map(0x6041u, 0x00u, 16u)   // StatusWord
        .map(0x6077u, 0x00u, 16u)   // ActualTorque
        .map(0x6064u, 0x00u, 32u);  // ActualPosition

    mapper.apply(1u, true);
    mapper.print_layout();

    return 0;
}
*/
```

---

## 11. Error Handling & Abort Codes

When an SDO write to a mapping parameter fails, the device returns an **SDO Abort
Code**. The most common ones during mapping operations:

```
  Abort Code   │ Meaning                              │ Likely Cause
  ─────────────┼──────────────────────────────────────┼──────────────────────────────
  0x06010000   │ Unsupported access to object          │ Object not PDO-mappable
  0x06010002   │ Attempt to write a read-only object   │ Writing map while PDO enabled
  0x06040041   │ PDO length exceeded                   │ Total bits > 64
  0x06040042   │ Parameter incompatible                │ Wrong step order violated
  0x06040043   │ Internal incompatibility              │ Firmware constraint
  0x06090011   │ Sub-index does not exist              │ Writing sub > supported max
  0x08000020   │ Data cannot be stored (general)       │ Node in wrong NMT state
  0x08000022   │ Data stored in NVM, reboot needed     │ Config requires save/reboot
```

```c
/* Translate common abort codes to human-readable strings */
const char *pdo_abort_str(uint32_t abort_code)
{
    switch (abort_code) {
    case 0x06010000uL: return "Object not PDO-mappable";
    case 0x06010002uL: return "PDO not disabled before mapping";
    case 0x06040041uL: return "PDO length exceeded (> 64 bits)";
    case 0x06040042uL: return "Mapping procedure order violated";
    case 0x06090011uL: return "Sub-index does not exist";
    case 0x08000020uL: return "Data cannot be stored to object";
    case 0x08000022uL: return "NMT state prohibits mapping change";
    default:           return "Unknown SDO abort";
    }
}
```

### 11.1 Common Mistakes and How to Avoid Them

```
  ┌─────────────────────────────────────────────────────────────────────┐
  │                     Common Mapping Mistakes                         │
  │                                                                     │
  │  - Writing map entries BEFORE clearing count → 0x06040042           │
  │    Fix: Always write 0 to sub-index 0 first (Step 2)                │
  │                                                                     │
  │  - Forgetting to disable PDO → 0x06010002                           │
  │    Fix: Set bit 31 of COB-ID before any mapping writes              │
  │                                                                     │
  │  - Exceeding 64 bits total → 0x06040041                             │
  │    Fix: Sum all bit_length fields before submitting                 │
  │                                                                     │
  │  - Mapping a non-mappable variable → 0x06010000                     │
  │    Fix: Check the OD / EDS — variable must have PDO flag set        │
  │                                                                     │
  │  - Not re-enabling PDO on error path                                │
  │    Fix: Use goto/RAII to always execute Step 5                      │
  │                                                                     │
  │  - Remapping while node is in NMT Stopped state → 0x08000022        │
  │    Fix: Ensure node is in NMT Pre-Operational or Operational        │
  └─────────────────────────────────────────────────────────────────────┘
```

---

## 12. Summary

PDO mapping is the mechanism by which CANopen transforms raw CAN frames into
structured, named process variables.  The key points are:

**Static vs. Dynamic mapping:**
Static mapping is set at compile time or via EDS and cannot change at runtime.
Dynamic mapping allows a CANopen master to reconfigure which OD variables are
packed into a PDO frame during operation, typically via expedited SDO writes.

**The 5-Step Procedure** (mandatory, in exact order):

```
  ┌─────────────────────────────────────────────────────────────────┐
  │  1. Disable PDO   →  write COB-ID | 0x80000000                  │
  │  2. Clear count   →  write map sub-index 0 = 0                  │
  │  3. Write entries →  write sub-index 1..N with encoded entries  │
  │  4. Set count     →  write map sub-index 0 = N                  │
  │  5. Enable PDO    →  write COB-ID & ~0x80000000                 │
  └─────────────────────────────────────────────────────────────────┘
```

**Mapping entry encoding:**

```
  Bits [31:16] = OD Index | Bits [15:8] = Sub-Index | Bits [7:0] = Bit-Length
  Example: 0x60410010 → OD[0x6041][0x00], 16 bits (StatusWord)
```

**Mixed-type packing** allows combining UINT8, INT16, INT32, BOOL and bit-fields
into one PDO, subject to the 64-bit total limit and device support for
sub-byte alignment.

**Dummy entries** (OD index 0x0001..0x001F) fill gaps without mapping real data,
enabling forward-compatible frame layouts and phased field upgrades.

**Error handling** requires always re-enabling the PDO (Step 5) even on failure,
and validating total bit-length before submission.  SDO abort code `0x06040042`
almost always indicates a violated step order.

---

*References: CiA 301 v4.2 §7.3.4 (PDO), §7.4 (SDO), CiA 402 (motion profile)*