# 06 · COB-ID Scheme & Predefined Connection Set

**Structure overview:**

1. **11-bit ID Composition** — ASCII bit-field diagrams showing how the 4-bit Function Code and 7-bit Node-ID combine, with the exact formula `COB-ID = (FC << 7) | NID`

2. **Function Code Table** — all 16 FC slots mapped to their CANopen object type (TPDO, RPDO, SDO, EMCY, Heartbeat, etc.) with hex base addresses

3. **Predefined Connection Set** — full table of default COB-IDs for all Node-IDs, plus an ASCII visualisation of the entire 0x000–0x7FF ID space partitioned into bands

4. **COB-ID Conflict Detection** — ASCII diagrams of duplicate-NodeID and misconfig scenarios, explanation of the validity bit (bit 31), and a sorting-based conflict detector in C

5. **Dynamic vs. Static Assignment** — ASCII flow diagrams for both approaches, plus the full 5-step SDO reconfiguration procedure (disable → write → enable → verify)

6. **CAN Filter Configuration** — filter formula, mask semantics, three named strategies (exact, band, wildcard), and STM32 bxCAN register layout with a complete slave filter setup function

7. **C/C++ Code Examples** — `canopen_cobid.h`, `canopen_pcs.c`, `canopen_conflict.c`, `CanopenCobidManager` (C++17), and `canopen_filter.c` with STM32 HAL integration

8. **Summary table** with OD index references and links to the relevant CiA standards

> **CANopen Application Layer — CiA 301**
> Document version 1.0 · Topic 06 of the CANopen series

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [The 11-bit CAN Identifier](#2-the-11-bit-can-identifier)
3. [COB-ID Composition: Function Code + Node-ID](#3-cob-id-composition-function-code--node-id)
4. [The Predefined Connection Set](#4-the-predefined-connection-set)
5. [COB-ID Conflict Detection](#5-cob-id-conflict-detection)
6. [Dynamic vs. Static COB-ID Assignment](#6-dynamic-vs-static-cob-id-assignment)
7. [CAN Filter Configuration for Multi-Node Networks](#7-can-filter-configuration-for-multi-node-networks)
8. [Complete Programming Examples in C/C++](#8-complete-programming-examples-in-cc)
9. [Summary](#9-summary)

---

## 1. Introduction

CANopen is a higher-layer protocol built on top of the CAN (Controller Area Network) bus. One of its most important architectural decisions is the mapping of **communication objects to CAN identifiers**. Every CAN frame has an 11-bit (standard frame format) or 29-bit (extended frame format) identifier. CANopen exclusively uses **11-bit identifiers** in its standard form (CiA 301), reducing the theoretical maximum to 2047 unique message IDs on a single bus segment.

The scheme that governs how these 2047 IDs are structured, allocated, and managed is called the **COB-ID Scheme** (Communication Object Identifier Scheme). Paired with it is the **Predefined Connection Set** — a lookup table that assigns default COB-IDs to standard CANopen communication objects at node startup without any configuration master intervention.

Understanding these two concepts is essential for:

- Building correct CANopen device firmware
- Diagnosing CAN bus ID collisions
- Configuring CAN hardware acceptance filters efficiently
- Implementing dynamic network configuration (LSS, SDO-based COB-ID reconfiguration)

---

## 2. The 11-bit CAN Identifier

CAN standard frames carry an 11-bit arbitration field. CANopen partitions these 11 bits into two logical subfields:

```
  Bit 10                                  Bit 0
  +--------+---------------------------------+
  |  4-bit |         7-bit                   |
  |  Func  |         Node-ID                 |
  |  Code  |         (1 .. 127)              |
  +--------+---------------------------------+
   [10:7]                [6:0]
```

The 11-bit space gives 2048 possible values (0x000 – 0x7FF). Value `0x000` is reserved for the NMT master command frame. This leaves 2047 usable COB-IDs across all nodes and object types.

```
  COB-ID range: 0x001 .. 0x7FF
  Reserved:     0x000  (NMT command)
  Reserved:     0x7FF  (unused / vendor-specific)
```

---

## 3. COB-ID Composition: Function Code + Node-ID

### 3.1 Bit Layout

```
  Bit: 10  9  8  7 | 6  5  4  3  2  1  0
       +-----------+-----------------------+
       |  Function |       Node-ID         |
       |   Code    |    (0x01 .. 0x7F)     |
       +-----------+-----------------------+
            FC              NID
```

The **Function Code** (FC) occupies bits 10..7 (the four most-significant bits of the 11-bit field). The **Node-ID** (NID) occupies bits 6..0 and is constrained to values 1–127; value 0 is the broadcast/master address.

### 3.2 COB-ID Calculation Formula

```
COB-ID = (Function_Code << 7) | Node_ID
```

### 3.3 Function Code Table

```
  +------+-------+-------------------------------+
  |  FC  |  Hex  |  Communication Object         |
  +------+-------+-------------------------------+
  | 0000 | 0x0xx | NMT (node management)         |
  | 0001 | 0x1xx | SYNC / EMCY (emergency)       |
  | 0010 | 0x2xx | TIME                          |
  | 0011 | 0x3xx | TPDO1  (Transmit PDO 1)       |
  | 0100 | 0x4xx | RPDO1  (Receive  PDO 1)       |
  | 0101 | 0x5xx | TPDO2  (Transmit PDO 2)       |
  | 0110 | 0x6xx | RPDO2  (Receive  PDO 2)       |
  | 0111 | 0x7xx | TPDO3  (Transmit PDO 3)       |
  | 1000 | 0x8xx | RPDO3  (Receive  PDO 3)       |
  | 1001 | 0x9xx | TPDO4  (Transmit PDO 4)       |
  | 1010 | 0xAxx | RPDO4  (Receive  PDO 4)       |
  | 1011 | 0xBxx | SDO Tx (Server → Client)      |
  | 1100 | 0xCxx | SDO Rx (Client → Server)      |
  | 1101 | 0xDxx | (reserved)                    |
  | 1110 | 0xExx | NMT Error Control (Heartbeat) |
  | 1111 | 0xFxx | (reserved / LSS)              |
  +------+-------+-------------------------------+
```

### 3.4 Worked Example: Node-ID = 5

```
  Object       FC    Node-ID   COB-ID
  -------      ----  -------   ------
  EMCY         0001  0000101   0x085   (0x80 + 5)
  TPDO1        0011  0000101   0x185   (0x180 + 5)
  RPDO1        0100  0000101   0x205   (0x200 + 5)
  TPDO2        0101  0000101   0x285   (0x280 + 5)
  RPDO2        0110  0000101   0x305   (0x300 + 5)
  TPDO3        0111  0000101   0x385   (0x380 + 5)
  RPDO3        1000  0000101   0x405   (0x400 + 5)
  TPDO4        1001  0000101   0x485   (0x480 + 5)
  RPDO4        1010  0000101   0x505   (0x500 + 5)
  SDO Tx       1011  0000101   0x585   (0x580 + 5)
  SDO Rx       1100  0000101   0x605   (0x600 + 5)
  Heartbeat    1110  0000101   0x705   (0x700 + 5)
```

---

## 4. The Predefined Connection Set

The **Predefined Connection Set** (PCS) is a table defined in CiA 301 that assigns a default COB-ID to each communication object for every legal Node-ID without any explicit configuration. A correctly powered-up CANopen node can immediately send and receive using these default identifiers.

### 4.1 Full Predefined Connection Set Table

```
  +-----+-----------+------------------+--------------------+------------------+
  | NID | EMCY      | TPDO1 / RPDO1    | TPDO2 / RPDO2      | SDO Tx / SDO Rx  |
  +-----+-----------+------------------+--------------------+------------------+
  |  1  | 0x081     | 0x181 / 0x201    | 0x281 / 0x301      | 0x581 / 0x601    |
  |  2  | 0x082     | 0x182 / 0x202    | 0x282 / 0x302      | 0x582 / 0x602    |
  |  3  | 0x083     | 0x183 / 0x203    | 0x283 / 0x303      | 0x583 / 0x603    |
  |  4  | 0x084     | 0x184 / 0x204    | 0x284 / 0x304      | 0x584 / 0x604    |
  |  5  | 0x085     | 0x185 / 0x205    | 0x285 / 0x305      | 0x585 / 0x605    |
  | ... | ...       | ...              | ...                | ...              |
  | 10  | 0x08A     | 0x18A / 0x20A    | 0x28A / 0x30A      | 0x58A / 0x60A    |
  | ... | ...       | ...              | ...                | ...              |
  | 32  | 0x0A0     | 0x1A0 / 0x220    | 0x2A0 / 0x320      | 0x5A0 / 0x620    |
  | ... | ...       | ...              | ...                | ...              |
  | 64  | 0x0C0     | 0x1C0 / 0x240    | 0x2C0 / 0x340      | 0x5C0 / 0x640    |
  | ... | ...       | ...              | ...                | ...              |
  | 127 | 0x0FF     | 0x1FF / 0x27F    | 0x2FF / 0x37F      | 0x5FF / 0x67F    |
  +-----+-----------+------------------+--------------------+------------------+

  Fixed (same for all nodes):
    NMT Command    : 0x000
    SYNC           : 0x080
    TIME           : 0x100
    LSS            : 0x7E4 / 0x7E5
```

### 4.2 Visualising the COB-ID Space

```
  0x000                                                              0x7FF
  |                                                                      |
  [NMT] [SYNC/EMCY-base] [TIME] [TPDO1s] [RPDO1s] [TPDO2s] [RPDO2s]...
  |      |               |      |         |         |         |
  0x000  0x080           0x100  0x180     0x200     0x280     0x300

  ... [TPDO3s] [RPDO3s] [TPDO4s] [RPDO4s] [SDO-Tx] [SDO-Rx] [..] [HB]
      |         |         |         |         |         |           |
      0x380     0x400     0x480     0x500     0x580     0x600       0x700

  Each band 0x180..0x67F spans 0x80 (128) values → exactly 127 node slots
  (Node-ID 0 is unused within PDO/SDO ranges)
```

### 4.3 Special Cases

- **SYNC** has COB-ID `0x080` by default. No Node-ID component.
- **TIME** has COB-ID `0x100` by default. No Node-ID component.
- **NMT** command uses `0x000`, NMT node control embeds the target Node-ID in the **data** bytes (not the COB-ID).
- **Heartbeat / Node Guarding** uses `0x700 + Node-ID`.

---

## 5. COB-ID Conflict Detection

### 5.1 Why Conflicts Occur

A COB-ID collision happens when two (or more) nodes transmit CAN frames with the same 11-bit identifier. On a CAN bus this causes **undefined arbitration** — the lower-priority transmitter detects a bit error and retransmits, causing bus load spikes. In the worst case, both nodes keep retransmitting, saturating the bus.

Common causes:

```
  Cause 1 — Duplicate Node-IDs
  ┌──────────┐         ┌──────────┐
  │  Node 5  │         │  Node 5  │   <-- Two nodes, same NID!
  │ TPDO1    │         │ TPDO1    │
  │ 0x185    │─────────│ 0x185    │   COLLISION on 0x185
  └──────────┘   BUS   └──────────┘

  Cause 2 — Reconfigured PDO overlaps SYNC
  ┌──────────┐
  │ Master   │  SYNC → 0x080
  └──────────┘
  ┌──────────┐
  │  Node 7  │  RPDO1 reconfigured to 0x080  <-- COLLISION
  └──────────┘

  Cause 3 — PDO base address pushed into SDO band
  ┌──────────┐
  │  Node 1  │  TPDO1 (default 0x181)   OK
  └──────────┘
  ┌──────────┐
  │  Node 1  │  TPDO1 moved to 0x581    COLLISION with own SDO-Tx
  └──────────┘
```

### 5.2 Detection Strategy

A CANopen configuration master should scan the bus during commissioning and verify that no two enabled COB-IDs are identical. The algorithm:

```
  1. Collect all active COB-IDs across all nodes
  2. Build a sorted list
  3. Scan for adjacent duplicates
  4. Report conflicts with node, object, and COB-ID value
```

### 5.3 COB-ID Validity Bit

Every configurable COB-ID in the Object Dictionary (e.g., 0x1400, 0x1800) has a **validity bit** — bit 31 of the 32-bit COB-ID value:

```
  Bit 31 = 1  →  COB-ID INVALID / disabled
  Bit 31 = 0  →  COB-ID VALID   / enabled

  Example (TPDO1 comm parameter 0x1800 sub-index 1):
  +--+-------------------------------+
  |31|30 .... 11 | 10 ............. 0|
  +--+-----------+-------------------+
  | V|  reserved | 11-bit COB-ID     |
  +--+-----------+-------------------+
```

A manager must first **set bit 31** to disable the object before changing its COB-ID, then **clear bit 31** to re-enable with the new ID. Changing a COB-ID while the object is active is a protocol error (abort code 0x06090030).

---

## 6. Dynamic vs. Static COB-ID Assignment

### 6.1 Static Assignment

In a static (fixed) configuration the developer assigns Node-IDs by hardware (DIP switches, jumpers, or one-time EEPROM write) and never reconfigures COB-IDs at runtime. All nodes operate with the Predefined Connection Set values. This is the simplest approach and is adequate for closed, fixed-topology machines.

```
  Network commissioning flow — Static:

  ┌──────────┐   Power-on    ┌─────────────────────────────┐
  │  Node X  │─────────────→ │ Load NID from hardware      │
  └──────────┘               │ Apply PCS defaults          │
                             │ Enter Pre-Operational state │
                             └─────────────────────────────┘
                             (No configuration master needed)
```

### 6.2 Dynamic Assignment

In a dynamic configuration a **Configuration Manager (CM)** or master communicates with nodes via SDO to read and rewrite COB-ID fields in the Object Dictionary. This is necessary when:

- Multiple identical devices share the bus (PDO COB-IDs would collide with PCS defaults)
- Flexible, software-defined network topologies are needed
- LSS (Layer Setting Services) is used to assign Node-IDs over the bus

```
  Network commissioning flow — Dynamic:

  ┌──────────┐  Power-on   ┌──────────────────────────────────────┐
  │  Node X  │───────────→ │ No valid NID in NVM → NID = 0xFF     │
  └──────────┘             │ Enter LSS Waiting state              │
                           └──────────────────────────────────────┘
                                         │ LSS protocol (identity)
                                         ▼
  ┌──────────┐  LSS-assign  ┌──────────────────────────────────────┐
  │  Master  │─────────────→│ Assign NID (e.g. 0x0C) via LSS       │
  └──────────┘              │ Node stores NID, reboots or resets   │
                            └──────────────────────────────────────┘
                                         │ SDO configuration phase
                                         ▼
                            ┌──────────────────────────────────────┐
                            │ Master writes TPDO / RPDO COB-IDs    │
                            │ via SDO (OD 0x1400..0x17FF,          │
                            │          OD 0x1800..0x1BFF)          │
                            │ Sets RTR, inhibit, event timer       │
                            └──────────────────────────────────────┘
                                         │ NMT Start
                                         ▼
                            ┌──────────────────────────────────────┐
                            │  Node enters Operational state       │
                            └──────────────────────────────────────┘
```

### 6.3 COB-ID Reconfiguration Procedure

The standard procedure for dynamically changing a TPDO COB-ID via SDO:

```
  Step 1: Read current COB-ID value
          SDO Read  → 0x1800 sub 1   (TPDO1 comm param, COB-ID)

  Step 2: Set bit 31 to disable the PDO
          SDO Write → 0x1800 sub 1 = (old_cobid | 0x80000000)

  Step 3: Write new COB-ID (bit 31 still set = still disabled)
          SDO Write → 0x1800 sub 1 = (new_cobid | 0x80000000)

  Step 4: Clear bit 31 to re-enable the PDO
          SDO Write → 0x1800 sub 1 = new_cobid

  Step 5: Verify by reading back
          SDO Read  → 0x1800 sub 1
```

---

## 7. CAN Filter Configuration for Multi-Node Networks

### 7.1 Why Filters Matter

Modern CAN controllers (e.g. SJA1000, MCP2515, STM32 bxCAN, ATSAM MCAN) have hardware **acceptance filters** that prevent the MCU from being interrupted for every CAN frame on the bus. In a busy 127-node network this is critical for CPU efficiency.

### 7.2 Filter Concepts

```
  CAN hardware filter operation:

  Bus frame arrives (11-bit ID)
          │
          ▼
  ┌───────────────────────────────────────────┐
  │  CAN Controller Hardware Filter Bank      │
  │                                           │
  │  (ID & MASK) == (FILTER & MASK) ?         │
  │                                           │
  │  YES ──→ Frame placed in RX FIFO          │
  │   NO ──→ Frame silently discarded         │
  └───────────────────────────────────────────┘

  Filter formula:
    Accept if: (received_id & mask) == (filter_id & mask)

  mask  bit = 1  → this bit MUST match the filter_id bit
  mask  bit = 0  → this bit is DON'T CARE
```

### 7.3 Filter Strategies for CANopen Nodes

#### Strategy A — Accept only own SDO Rx + NMT

A slave node typically needs to receive NMT commands (0x000) and SDO addressed to itself (0x600 + NodeID). It does not need to receive SDO addressed to other nodes.

```
  Accept: 0x000 (NMT)  and  0x600 + NID (SDO-Rx)

  Since these are not in the same "band", two filter banks are needed:
  Filter 1: id=0x000, mask=0x7FF  (exact match NMT)
  Filter 2: id=(0x600 | NID), mask=0x7FF  (exact match own SDO-Rx)
```

#### Strategy B — Accept all PDOs in a function-code band

A node that subscribes to RPDOs from multiple sources:

```
  Accept all RPDO1 frames (function code 0100 = 0x200):

  filter_id = 0x200
  mask      = 0x780   (bits 10..7 — only check function code)

  Check: (received_id & 0x780) == (0x200 & 0x780)
         → accepts 0x201, 0x202, ... 0x27F
```

#### Strategy C — Wildcard / accept-all (loopback/debug)

```
  filter_id = 0x000
  mask      = 0x000   (all bits don't-care)
  → Accept every frame on the bus
```

### 7.4 Bit-field Layout for Hardware Filter Registers

Most CAN controllers store the 11-bit ID left-justified in a 16-bit register:

```
  STM32 bxCAN filter register layout (16-bit mode):
  Bit: 15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
       |ID10|ID9|ID8|ID7|ID6|ID5|ID4|ID3|ID2|ID1|ID0|RTR|IDE| 0 | 0 | 0 |
       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
       ← MSB of 11-bit ID                       LSB →

  To filter for 0x185 (TPDO1 of Node 5):
    filter_id   = (0x185 << 5) = 0x3000 in register
    filter_mask = (0x7FF << 5) = 0xFFE0 in register
```

---

## 8. Complete Programming Examples in C/C++

### 8.1 COB-ID Utility Functions

```c
/* canopen_cobid.h — COB-ID utilities for CANopen */
#ifndef CANOPEN_COBID_H
#define CANOPEN_COBID_H

#include <stdint.h>
#include <stdbool.h>

/* Function codes (upper 4 bits of 11-bit COB-ID) */
typedef enum {
    FC_NMT          = 0x00,  /* 0x000 — fixed, no Node-ID */
    FC_SYNC_EMCY    = 0x01,  /* 0x080 (SYNC) / 0x08N (EMCY) */
    FC_TIME         = 0x02,  /* 0x100 — fixed, no Node-ID */
    FC_TPDO1        = 0x03,  /* 0x180 + Node-ID */
    FC_RPDO1        = 0x04,  /* 0x200 + Node-ID */
    FC_TPDO2        = 0x05,  /* 0x280 + Node-ID */
    FC_RPDO2        = 0x06,  /* 0x300 + Node-ID */
    FC_TPDO3        = 0x07,  /* 0x380 + Node-ID */
    FC_RPDO3        = 0x08,  /* 0x400 + Node-ID */
    FC_TPDO4        = 0x09,  /* 0x480 + Node-ID */
    FC_RPDO4        = 0x0A,  /* 0x500 + Node-ID */
    FC_SDO_TX       = 0x0B,  /* 0x580 + Node-ID */
    FC_SDO_RX       = 0x0C,  /* 0x600 + Node-ID */
    FC_HEARTBEAT    = 0x0E,  /* 0x700 + Node-ID */
} CanopenFunctionCode;

/* Validity bit — bit 31 of 32-bit COB-ID register */
#define COBID_VALID_BIT     (1UL << 31)
#define COBID_RTR_BIT       (1UL << 30)
#define COBID_FRAME29_BIT   (1UL << 29)
#define COBID_ID_MASK       (0x7FFUL)   /* lower 11 bits */

/* Compose an 11-bit COB-ID from function code and Node-ID */
static inline uint16_t canopen_make_cobid(CanopenFunctionCode fc, uint8_t node_id)
{
    return (uint16_t)(((uint16_t)fc << 7) | (node_id & 0x7F));
}

/* Extract function code from COB-ID */
static inline uint8_t canopen_fc_of(uint16_t cobid)
{
    return (uint8_t)((cobid >> 7) & 0x0F);
}

/* Extract Node-ID from COB-ID */
static inline uint8_t canopen_nodeid_of(uint16_t cobid)
{
    return (uint8_t)(cobid & 0x7F);
}

/* Check if a 32-bit OD COB-ID register marks the object as valid/enabled */
static inline bool canopen_cobid_is_valid(uint32_t od_cobid)
{
    return (od_cobid & COBID_VALID_BIT) == 0;
}

/* Get the 11-bit bus ID from a 32-bit OD COB-ID register */
static inline uint16_t canopen_cobid_get_id(uint32_t od_cobid)
{
    return (uint16_t)(od_cobid & COBID_ID_MASK);
}

/* Disable a COB-ID (set bit 31) before reconfiguring */
static inline uint32_t canopen_cobid_disable(uint32_t od_cobid)
{
    return od_cobid | COBID_VALID_BIT;
}

/* Enable a COB-ID (clear bit 31) */
static inline uint32_t canopen_cobid_enable(uint32_t od_cobid)
{
    return od_cobid & ~COBID_VALID_BIT;
}

#endif /* CANOPEN_COBID_H */
```

---

### 8.2 Predefined Connection Set Table

```c
/* canopen_pcs.h — Predefined Connection Set */
#ifndef CANOPEN_PCS_H
#define CANOPEN_PCS_H

#include <stdint.h>
#include "canopen_cobid.h"

/*
 * Predefined Connection Set:
 * Returns the default 11-bit COB-ID for a given function code and Node-ID.
 * Node-ID must be 1..127.
 * Returns 0xFFFF for invalid inputs.
 */
uint16_t canopen_pcs_get(CanopenFunctionCode fc, uint8_t node_id);

/* Print the complete PCS for a given Node-ID to stdout */
void canopen_pcs_print(uint8_t node_id);

#endif /* CANOPEN_PCS_H */
```

```c
/* canopen_pcs.c — Predefined Connection Set implementation */
#include "canopen_pcs.h"
#include <stdio.h>

/* Fixed COB-IDs (no Node-ID component) */
#define COBID_NMT_CMD   0x000U
#define COBID_SYNC      0x080U
#define COBID_TIME      0x100U
#define COBID_LSS_RX    0x7E4U
#define COBID_LSS_TX    0x7E5U

uint16_t canopen_pcs_get(CanopenFunctionCode fc, uint8_t node_id)
{
    if (node_id == 0 || node_id > 127) {
        return 0xFFFF; /* invalid Node-ID */
    }

    switch (fc) {
        case FC_NMT:        return COBID_NMT_CMD;            /* 0x000 */
        case FC_SYNC_EMCY:  return (uint16_t)(0x080 + node_id); /* EMCY */
        case FC_TIME:       return COBID_TIME;               /* 0x100 */
        case FC_TPDO1:      return canopen_make_cobid(FC_TPDO1,     node_id);
        case FC_RPDO1:      return canopen_make_cobid(FC_RPDO1,     node_id);
        case FC_TPDO2:      return canopen_make_cobid(FC_TPDO2,     node_id);
        case FC_RPDO2:      return canopen_make_cobid(FC_RPDO2,     node_id);
        case FC_TPDO3:      return canopen_make_cobid(FC_TPDO3,     node_id);
        case FC_RPDO3:      return canopen_make_cobid(FC_RPDO3,     node_id);
        case FC_TPDO4:      return canopen_make_cobid(FC_TPDO4,     node_id);
        case FC_RPDO4:      return canopen_make_cobid(FC_RPDO4,     node_id);
        case FC_SDO_TX:     return canopen_make_cobid(FC_SDO_TX,    node_id);
        case FC_SDO_RX:     return canopen_make_cobid(FC_SDO_RX,    node_id);
        case FC_HEARTBEAT:  return canopen_make_cobid(FC_HEARTBEAT, node_id);
        default:            return 0xFFFF;
    }
}

void canopen_pcs_print(uint8_t node_id)
{
    printf("Predefined Connection Set for Node-ID = %u (0x%02X)\n",
           node_id, node_id);
    printf("%-16s  %-6s\n", "Object", "COB-ID");
    printf("%-16s  %-6s\n", "----------------", "------");

    const struct { const char *name; CanopenFunctionCode fc; } entries[] = {
        { "EMCY",       FC_SYNC_EMCY  },
        { "TPDO1",      FC_TPDO1      },
        { "RPDO1",      FC_RPDO1      },
        { "TPDO2",      FC_TPDO2      },
        { "RPDO2",      FC_RPDO2      },
        { "TPDO3",      FC_TPDO3      },
        { "RPDO3",      FC_RPDO3      },
        { "TPDO4",      FC_TPDO4      },
        { "RPDO4",      FC_RPDO4      },
        { "SDO-Tx",     FC_SDO_TX     },
        { "SDO-Rx",     FC_SDO_RX     },
        { "Heartbeat",  FC_HEARTBEAT  },
    };

    for (size_t i = 0; i < sizeof(entries)/sizeof(entries[0]); i++) {
        uint16_t cob = canopen_pcs_get(entries[i].fc, node_id);
        printf("%-16s  0x%03X\n", entries[i].name, cob);
    }
    printf("%-16s  0x%03X  (fixed)\n", "SYNC",     COBID_SYNC);
    printf("%-16s  0x%03X  (fixed)\n", "TIME",     COBID_TIME);
    printf("%-16s  0x%03X  (fixed)\n", "NMT Cmd",  COBID_NMT_CMD);
}
```

---

### 8.3 COB-ID Conflict Detector

```c
/* canopen_conflict.h */
#ifndef CANOPEN_CONFLICT_H
#define CANOPEN_CONFLICT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_NODES       127
#define MAX_COBIDS_NODE  16   /* objects per node */

/* Record describing one active COB-ID on the network */
typedef struct {
    uint16_t cobid;
    uint8_t  node_id;
    char     name[24];
} CobidRecord;

/* Conflict result */
typedef struct {
    uint16_t cobid;
    uint8_t  node_a;
    uint8_t  node_b;
    char     name_a[24];
    char     name_b[24];
} CobidConflict;

/*
 * Populate a CobidRecord array with PCS defaults for all
 * nodes in node_ids[]. Returns the count of records added.
 */
size_t canopen_collect_pcs(
    const uint8_t  *node_ids,
    size_t          num_nodes,
    CobidRecord    *records,
    size_t          max_records);

/*
 * Scan records[] for duplicate COB-IDs.
 * Fills conflicts[] and returns the number of conflicts found.
 */
size_t canopen_find_conflicts(
    const CobidRecord *records,
    size_t             num_records,
    CobidConflict     *conflicts,
    size_t             max_conflicts);

/* Print a conflict report to stdout */
void canopen_print_conflicts(
    const CobidConflict *conflicts,
    size_t               num_conflicts);

#endif /* CANOPEN_CONFLICT_H */
```

```c
/* canopen_conflict.c */
#include "canopen_conflict.h"
#include "canopen_pcs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>  /* qsort */

static const struct {
    CanopenFunctionCode fc;
    const char         *name;
} pcs_objects[] = {
    { FC_SYNC_EMCY,  "EMCY"      },
    { FC_TPDO1,      "TPDO1"     },
    { FC_RPDO1,      "RPDO1"     },
    { FC_TPDO2,      "TPDO2"     },
    { FC_RPDO2,      "RPDO2"     },
    { FC_TPDO3,      "TPDO3"     },
    { FC_RPDO3,      "RPDO3"     },
    { FC_TPDO4,      "TPDO4"     },
    { FC_RPDO4,      "RPDO4"     },
    { FC_SDO_TX,     "SDO-Tx"    },
    { FC_SDO_RX,     "SDO-Rx"    },
    { FC_HEARTBEAT,  "Heartbeat" },
};
#define NUM_PCS_OBJECTS  (sizeof(pcs_objects) / sizeof(pcs_objects[0]))

size_t canopen_collect_pcs(
    const uint8_t *node_ids,
    size_t         num_nodes,
    CobidRecord   *records,
    size_t         max_records)
{
    size_t count = 0;

    for (size_t n = 0; n < num_nodes && count < max_records; n++) {
        uint8_t nid = node_ids[n];
        for (size_t o = 0; o < NUM_PCS_OBJECTS && count < max_records; o++) {
            records[count].cobid   = canopen_pcs_get(pcs_objects[o].fc, nid);
            records[count].node_id = nid;
            snprintf(records[count].name, sizeof(records[count].name),
                     "%s", pcs_objects[o].name);
            count++;
        }
    }
    return count;
}

/* Comparator for qsort by COB-ID */
static int cmp_cobid(const void *a, const void *b)
{
    const CobidRecord *ra = (const CobidRecord *)a;
    const CobidRecord *rb = (const CobidRecord *)b;
    return (int)ra->cobid - (int)rb->cobid;
}

size_t canopen_find_conflicts(
    const CobidRecord *records,
    size_t             num_records,
    CobidConflict     *conflicts,
    size_t             max_conflicts)
{
    if (num_records == 0) return 0;

    /* Sort a local copy by COB-ID */
    CobidRecord *sorted = (CobidRecord *)malloc(num_records * sizeof(*sorted));
    if (!sorted) return 0;
    memcpy(sorted, records, num_records * sizeof(*sorted));
    qsort(sorted, num_records, sizeof(*sorted), cmp_cobid);

    size_t nconf = 0;
    for (size_t i = 0; i + 1 < num_records && nconf < max_conflicts; i++) {
        if (sorted[i].cobid == sorted[i + 1].cobid) {
            conflicts[nconf].cobid  = sorted[i].cobid;
            conflicts[nconf].node_a = sorted[i].node_id;
            conflicts[nconf].node_b = sorted[i + 1].node_id;
            snprintf(conflicts[nconf].name_a,
                     sizeof(conflicts[nconf].name_a), "%s", sorted[i].name);
            snprintf(conflicts[nconf].name_b,
                     sizeof(conflicts[nconf].name_b), "%s", sorted[i + 1].name);
            nconf++;
        }
    }

    free(sorted);
    return nconf;
}

void canopen_print_conflicts(
    const CobidConflict *conflicts,
    size_t               num_conflicts)
{
    if (num_conflicts == 0) {
        printf("No COB-ID conflicts detected.\n");
        return;
    }
    printf("COB-ID CONFLICTS DETECTED (%zu):\n", num_conflicts);
    printf("%-8s  %-6s  %-20s  %-6s  %-20s\n",
           "COB-ID", "NodeA", "ObjectA", "NodeB", "ObjectB");
    printf("%-8s  %-6s  %-20s  %-6s  %-20s\n",
           "--------", "------", "--------------------",
           "------", "--------------------");
    for (size_t i = 0; i < num_conflicts; i++) {
        printf("0x%03X   N=%3u  %-20s  N=%3u  %-20s\n",
               conflicts[i].cobid,
               conflicts[i].node_a, conflicts[i].name_a,
               conflicts[i].node_b, conflicts[i].name_b);
    }
}
```

**Usage example:**

```c
/* main.c — conflict detection demo */
#include <stdio.h>
#include "canopen_conflict.h"
#include "canopen_pcs.h"

int main(void)
{
    /* Simulate a network with two nodes accidentally assigned the same Node-ID */
    uint8_t node_ids[] = { 1, 5, 5, 10 };  /* Node 5 duplicated! */
    size_t  num_nodes  = sizeof(node_ids) / sizeof(node_ids[0]);

    CobidRecord   records[512];
    CobidConflict conflicts[64];

    size_t nr = canopen_collect_pcs(node_ids, num_nodes,
                                    records, sizeof(records)/sizeof(records[0]));
    size_t nc = canopen_find_conflicts(records, nr,
                                       conflicts, sizeof(conflicts)/sizeof(conflicts[0]));
    canopen_print_conflicts(conflicts, nc);

    /* Also print PCS for a single node */
    printf("\n");
    canopen_pcs_print(5);
    return 0;
}
```

Expected output (excerpt):

```
COB-ID CONFLICTS DETECTED (12):
COB-ID    NodeA  ObjectA               NodeB  ObjectB
--------  ------  --------------------  ------  --------------------
0x085   N=  5  EMCY                  N=  5  EMCY
0x185   N=  5  TPDO1                 N=  5  TPDO1
0x205   N=  5  RPDO1                 N=  5  RPDO1
...
```

---

### 8.4 Dynamic COB-ID Reconfiguration via SDO

```cpp
// canopen_sdo_cobid.hpp — C++17, platform-independent SDO COB-ID management

#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

/* SDO abort codes relevant to COB-ID operations */
enum class SdoAbortCode : uint32_t {
    Success              = 0x00000000,
    InvalidValue         = 0x06090030,  /* COB-ID change while valid */
    ObjectNotExist       = 0x06020000,
    SubIndexNotExist     = 0x06090011,
    WriteProtected       = 0x06010002,
};

/* Platform callback type: SDO expedited read/write */
using SdoReadFn  = std::function<SdoAbortCode(uint8_t node, uint16_t idx,
                                               uint8_t sub, uint32_t &value)>;
using SdoWriteFn = std::function<SdoAbortCode(uint8_t node, uint16_t idx,
                                               uint8_t sub, uint32_t  value)>;

class CanopenCobidManager
{
public:
    CanopenCobidManager(SdoReadFn read_fn, SdoWriteFn write_fn)
        : sdo_read_(std::move(read_fn))
        , sdo_write_(std::move(write_fn))
    {}

    /*
     * Reconfigure a TPDO or RPDO COB-ID on a remote node.
     *
     * od_index: 0x1400..0x17FF (RPDO comm params) or 0x1800..0x1BFF (TPDO)
     * sub_index: 1 = COB-ID
     * new_cobid: 11-bit bus identifier for the PDO
     */
    void set_pdo_cobid(uint8_t node_id, uint16_t od_index, uint16_t new_cobid)
    {
        constexpr uint8_t  SUB_COBID    = 1;
        constexpr uint32_t VALID_BIT    = (1UL << 31);

        /* Step 1: Read current value */
        uint32_t current_val = 0;
        auto rc = sdo_read_(node_id, od_index, SUB_COBID, current_val);
        check_abort(rc, "read COB-ID");

        /* Step 2: Disable PDO by setting bit 31 */
        uint32_t disabled_val = current_val | VALID_BIT;
        rc = sdo_write_(node_id, od_index, SUB_COBID, disabled_val);
        check_abort(rc, "disable PDO");

        /* Step 3: Write new COB-ID (bit 31 still set) */
        uint32_t new_val = (disabled_val & ~0x7FFUL) | (new_cobid & 0x7FFU);
        rc = sdo_write_(node_id, od_index, SUB_COBID, new_val);
        check_abort(rc, "write new COB-ID");

        /* Step 4: Re-enable PDO by clearing bit 31 */
        new_val &= ~VALID_BIT;
        rc = sdo_write_(node_id, od_index, SUB_COBID, new_val);
        check_abort(rc, "re-enable PDO");

        /* Step 5: Readback verify */
        uint32_t verify = 0;
        rc = sdo_read_(node_id, od_index, SUB_COBID, verify);
        check_abort(rc, "verify readback");

        if ((verify & 0x7FFU) != (new_cobid & 0x7FFU)) {
            throw std::runtime_error(
                "COB-ID verification failed: expected 0x" +
                to_hex(new_cobid) + ", got 0x" + to_hex(verify & 0x7FFU));
        }
    }

    /*
     * Query the active COB-ID for a communication parameter.
     * Returns the 11-bit bus ID, or 0xFFFF if disabled/invalid.
     */
    uint16_t get_pdo_cobid(uint8_t node_id, uint16_t od_index)
    {
        constexpr uint8_t  SUB_COBID  = 1;
        constexpr uint32_t VALID_BIT  = (1UL << 31);

        uint32_t val = 0;
        auto rc = sdo_read_(node_id, od_index, SUB_COBID, val);
        check_abort(rc, "get COB-ID");

        if (val & VALID_BIT) {
            return 0xFFFF; /* PDO is disabled */
        }
        return static_cast<uint16_t>(val & 0x7FFU);
    }

private:
    SdoReadFn  sdo_read_;
    SdoWriteFn sdo_write_;

    static void check_abort(SdoAbortCode rc, const char *ctx)
    {
        if (rc != SdoAbortCode::Success) {
            throw std::runtime_error(std::string("SDO abort during ") + ctx +
                                     ": 0x" + to_hex(static_cast<uint32_t>(rc)));
        }
    }

    static std::string to_hex(uint32_t v)
    {
        char buf[12];
        snprintf(buf, sizeof(buf), "%08X", v);
        return std::string(buf);
    }
};
```

**Usage:**

```cpp
// Example: reconfigure TPDO1 of Node 3 from 0x183 to 0x383

auto manager = CanopenCobidManager(
    /* sdo_read  */ [](uint8_t n, uint16_t i, uint8_t s, uint32_t &v) {
        return platform_sdo_read(n, i, s, &v);
    },
    /* sdo_write */ [](uint8_t n, uint16_t i, uint8_t s, uint32_t v) {
        return platform_sdo_write(n, i, s, v);
    }
);

// TPDO1 communication parameter: OD index 0x1800
manager.set_pdo_cobid(3, 0x1800, 0x383);
```

---

### 8.5 CAN Hardware Filter Configuration

```c
/* canopen_filter.h — CAN acceptance filter helpers for CANopen */
#ifndef CANOPEN_FILTER_H
#define CANOPEN_FILTER_H

#include <stdint.h>
#include <stddef.h>

/* A logical CAN acceptance filter */
typedef struct {
    uint16_t id;    /* 11-bit COB-ID to match */
    uint16_t mask;  /* 1 = bit must match, 0 = don't care */
} CanFilter;

/*
 * Build a filter that accepts only a single exact COB-ID.
 * Use for: own SDO-Rx, own Heartbeat, NMT commands
 */
static inline CanFilter canfilter_exact(uint16_t cobid)
{
    return (CanFilter){ .id = cobid, .mask = 0x7FFU };
}

/*
 * Build a filter that accepts all COB-IDs with a given function code.
 * Useful for: accepting all RPDO1 (FC=0x04), all EMCY (FC=0x01), etc.
 */
static inline CanFilter canfilter_by_fc(uint8_t function_code)
{
    return (CanFilter){
        .id   = (uint16_t)((function_code & 0x0F) << 7),
        .mask = 0x780U   /* bits 10..7 only */
    };
}

/*
 * Build a wildcard filter (accept everything) — for debug / loopback.
 */
static inline CanFilter canfilter_accept_all(void)
{
    return (CanFilter){ .id = 0x000U, .mask = 0x000U };
}

/*
 * Compute the recommended filter bank settings for a CANopen slave node.
 * Fills filters[] array and returns the number of filters written.
 *
 * The slave needs:
 *   - NMT command (0x000)      — exact
 *   - own SDO-Rx (0x600+NID)   — exact
 *   - own RPDO1..4             — exact (or range if from multiple masters)
 *   - SYNC (0x080)             — exact (if node uses SYNC)
 *   - TIME (0x100)             — exact (if node uses TIME stamp)
 */
size_t canopen_slave_filters(
    uint8_t    node_id,
    bool       use_sync,
    bool       use_time,
    CanFilter *filters,
    size_t     max_filters);

/*
 * Apply a filter array to STM32 bxCAN filter banks (32-bit list mode).
 * filter_bank_start: first filter bank index to use (0..13)
 * Returns number of filter banks consumed.
 */
#ifdef STM32_BXCAN
#include "stm32f4xx_hal.h"
size_t canopen_apply_filters_stm32(
    CAN_HandleTypeDef *hcan,
    const CanFilter   *filters,
    size_t             num_filters,
    uint32_t           filter_bank_start);
#endif

#endif /* CANOPEN_FILTER_H */
```

```c
/* canopen_filter.c */
#include "canopen_filter.h"
#include "canopen_cobid.h"
#include <string.h>

size_t canopen_slave_filters(
    uint8_t    node_id,
    bool       use_sync,
    bool       use_time,
    CanFilter *filters,
    size_t     max_filters)
{
    size_t n = 0;

#define ADD_FILTER(f)  do { if (n < max_filters) filters[n++] = (f); } while(0)

    /* NMT command — always */
    ADD_FILTER(canfilter_exact(0x000U));

    /* Own SDO-Rx */
    ADD_FILTER(canfilter_exact(canopen_make_cobid(FC_SDO_RX, node_id)));

    /* Own PDOs (RPDO1..4) */
    ADD_FILTER(canfilter_exact(canopen_make_cobid(FC_RPDO1, node_id)));
    ADD_FILTER(canfilter_exact(canopen_make_cobid(FC_RPDO2, node_id)));
    ADD_FILTER(canfilter_exact(canopen_make_cobid(FC_RPDO3, node_id)));
    ADD_FILTER(canfilter_exact(canopen_make_cobid(FC_RPDO4, node_id)));

    /* SYNC — optional */
    if (use_sync) {
        ADD_FILTER(canfilter_exact(0x080U));
    }

    /* TIME — optional */
    if (use_time) {
        ADD_FILTER(canfilter_exact(0x100U));
    }

#undef ADD_FILTER
    return n;
}

#ifdef STM32_BXCAN
size_t canopen_apply_filters_stm32(
    CAN_HandleTypeDef *hcan,
    const CanFilter   *filters,
    size_t             num_filters,
    uint32_t           filter_bank_start)
{
    size_t banks_used = 0;
    CAN_FilterTypeDef cfg = {0};

    /*
     * STM32 bxCAN in 32-bit identifier list mode:
     * Each filter bank holds 2 independent 32-bit filters.
     * 11-bit ID is stored in bits [31:21] of the register.
     * bit 2 = IDE (0 = standard frame)
     * bit 1 = RTR
     * bit 0 = 0 (not used)
     *
     * Register value = (cobid << 21) | 0x00 (standard, data frame)
     */

    cfg.FilterMode      = CAN_FILTERMODE_IDLIST;
    cfg.FilterScale     = CAN_FILTERSCALE_32BIT;
    cfg.FilterActivation = CAN_FILTER_ENABLE;
    cfg.FilterFIFOAssignment = CAN_RX_FIFO0;

    for (size_t i = 0; i < num_filters; i += 2) {
        cfg.FilterBank = filter_bank_start + banks_used;

        /* First filter of pair */
        uint32_t id_high = (uint32_t)filters[i].id << 5;  /* shift to bits[15:5] */
        cfg.FilterIdHigh   = (uint16_t)(id_high >> 16);
        cfg.FilterIdLow    = (uint16_t)(id_high & 0xFFFF);

        /* Second filter of pair (or duplicate first if odd count) */
        size_t j = (i + 1 < num_filters) ? (i + 1) : i;
        uint32_t id_low  = (uint32_t)filters[j].id << 5;
        cfg.FilterMaskIdHigh = (uint16_t)(id_low >> 16);
        cfg.FilterMaskIdLow  = (uint16_t)(id_low & 0xFFFF);

        HAL_CAN_ConfigFilter(hcan, &cfg);
        banks_used++;
    }

    return banks_used;
}
#endif /* STM32_BXCAN */
```

**Filter setup for a slave node (Node-ID = 7):**

```c
/* main.c excerpt — configure CAN filters for CANopen slave at Node-ID 7 */

#include "canopen_filter.h"

void setup_can_filters(CAN_HandleTypeDef *hcan)
{
    CanFilter filters[16];
    size_t nf = canopen_slave_filters(
        7,          /* Node-ID    */
        true,       /* use SYNC   */
        false,      /* no TIME    */
        filters,
        sizeof(filters)/sizeof(filters[0])
    );

    /* Log filters for debug */
    for (size_t i = 0; i < nf; i++) {
        printf("Filter[%zu]: id=0x%03X mask=0x%03X\n",
               i, filters[i].id, filters[i].mask);
    }

    /* Apply to hardware (STM32 bxCAN, starting at filter bank 0) */
    canopen_apply_filters_stm32(hcan, filters, nf, 0);
}
```

Expected filter output:

```
Filter[0]: id=0x000 mask=0x7FF   <- NMT
Filter[1]: id=0x607 mask=0x7FF   <- SDO-Rx
Filter[2]: id=0x207 mask=0x7FF   <- RPDO1
Filter[3]: id=0x307 mask=0x7FF   <- RPDO2
Filter[4]: id=0x407 mask=0x7FF   <- RPDO3
Filter[5]: id=0x507 mask=0x7FF   <- RPDO4
Filter[6]: id=0x080 mask=0x7FF   <- SYNC
```

---

## 9. Summary

### Core Concepts at a Glance

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                   CANopen COB-ID Architecture                   │
  ├─────────────────────────────────────────────────────────────────┤
  │                                                                 │
  │  11-bit ID = [Function Code (4 bits)] + [Node-ID (7 bits)]      │
  │                                                                 │
  │  COB-ID = (FC << 7) | NID                                       │
  │                                                                 │
  │  Predefined Connection Set: deterministic defaults              │
  │    → No configuration master needed at power-on                 │
  │    → Any two devices with different Node-IDs: no conflict       │
  │                                                                 │
  │  COB-ID Validity Bit (bit 31 of 32-bit OD register):            │
  │    1 = disabled (safe to change COB-ID)                         │
  │    0 = enabled (active on bus)                                  │
  │                                                                 │
  │  Change procedure:  READ → DISABLE → WRITE → ENABLE → VERIFY    │
  │                                                                 │
  │  CAN Filter = (received_id & mask) == (filter_id & mask)        │
  │    mask=0x7FF → exact match                                     │
  │    mask=0x780 → function-code band (all nodes)                  │
  │    mask=0x000 → accept all                                      │
  └─────────────────────────────────────────────────────────────────┘
```

| Aspect | Key Rule |
|--------|----------|
| **COB-ID formula** | `(Function_Code << 7) \| Node_ID` |
| **Node-ID range** | 1..127 (0 = NMT broadcast, 255 = unconfigured) |
| **Fixed IDs** | NMT=0x000, SYNC=0x080, TIME=0x100 |
| **Conflict root cause** | Duplicate Node-IDs or manually misconfigured PDO COB-IDs |
| **Detection** | Sort all active COB-IDs, scan for adjacent duplicates |
| **Safe reconfiguration** | Always disable (set bit 31) before changing COB-ID |
| **Filter strategy** | Exact-match per object for slaves; band-match for monitors |
| **Static vs. dynamic** | Static = DIP/NVM NID, PCS defaults; Dynamic = LSS + SDO config |

### Object Dictionary References

```
  OD Index  Sub  Object
  --------  ---  ----------------------------------------
  0x1000     0   Device Type
  0x1017     0   Producer Heartbeat Time
  0x1400+n   1   RPDO (n+1) COB-ID (bit31=valid, bits10..0=ID)
  0x1400+n   2   RPDO (n+1) Transmission Type
  0x1800+n   1   TPDO (n+1) COB-ID
  0x1800+n   2   TPDO (n+1) Transmission Type
  0x1800+n   3   TPDO (n+1) Inhibit Time
  0x1800+n   5   TPDO (n+1) Event Timer
```

### Further Reading

- **CiA 301** — CANopen Application Layer and Communication Profile (the primary standard)
- **CiA 302** — Additional Application Layer Functions (LSS)
- **CiA 305** — Layer Setting Services and Protocol (LSS for dynamic Node-ID assignment)
- **ISO 11898-1** — CAN Data Link Layer and Physical Signalling

---

*End of Document 06 · COB-ID Scheme & Predefined Connection Set*