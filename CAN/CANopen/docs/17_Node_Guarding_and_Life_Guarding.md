# 17. CANopen Node Guarding & Life Guarding (Legacy)

> **Scope:** Guard time, life-time factor, RTR-based polling, toggle bit,
> remote frame limitations on CAN FD networks, and migration path to the
> heartbeat protocol.

---

## Table of Contents

1. [Overview and Historical Context](#1-overview-and-historical-context)
2. [Core Concepts and Terminology](#2-core-concepts-and-terminology)
3. [Protocol Mechanics](#3-protocol-mechanics)
   - 3.1 [Guard Time and Life-Time Factor](#31-guard-time-and-life-time-factor)
   - 3.2 [RTR-Based Polling](#32-rtr-based-polling)
   - 3.3 [The Toggle Bit](#33-the-toggle-bit)
   - 3.4 [Node State Encoding](#34-node-state-encoding)
4. [Object Dictionary Entries](#4-object-dictionary-entries)
5. [Timing Diagrams (ASCII)](#5-timing-diagrams-ascii)
6. [Error Conditions](#6-error-conditions)
7. [CAN FD Limitations](#7-can-fd-limitations)
8. [C/C++ Implementation](#8-cc-implementation)
   - 8.1 [Data Structures](#81-data-structures)
   - 8.2 [NMT Master: Sending RTR and Evaluating Response](#82-nmt-master-sending-rtr-and-evaluating-response)
   - 8.3 [NMT Slave: Responding to Guard RTR](#83-nmt-slave-responding-to-guard-rtr)
   - 8.4 [Life Guarding on the Slave Side](#84-life-guarding-on-the-slave-side)
   - 8.5 [Full Guard State-Machine Example](#85-full-guard-state-machine-example)
9. [Migration Path to Heartbeat Protocol](#9-migration-path-to-heartbeat-protocol)
10. [Summary](#10-summary)

---

## 1. Overview and Historical Context

Node Guarding and Life Guarding are **legacy NMT monitoring mechanisms** defined
in the CANopen standard (CiA 301). They were the original method for a master
to supervise the health of remote nodes and for a slave to detect the loss of
its master, long before the simpler **Heartbeat protocol** was introduced.

```
  Timeline of NMT Monitoring in CANopen
  ───────────────────────────────────────────────────────────────────────
  CiA 301 v1.x  (early 1990s)   Node Guarding + Life Guarding introduced
  CiA 301 v3.0  (1996)          Heartbeat protocol added as alternative
  CiA 301 v4.x  (2000s)        Heartbeat recommended; Node Guarding legacy
  CiA 301 v4.2+ (current)       Node Guarding retained for compatibility
  ───────────────────────────────────────────────────────────────────────
```

Despite being legacy, many deployed systems — especially in older industrial
machinery, medical devices, and automotive sub-systems — still rely on this
mechanism. Understanding it is essential for maintenance, migration, and
interoperability work.

---

## 2. Core Concepts and Terminology

| Term | Description |
|---|---|
| **Node Guard Message** | A CAN Remote Frame (RTR) sent by the master to a specific slave node, requesting the slave to respond with its current NMT state |
| **Guard Time (`0x100C`)** | Period (ms) at which the master polls a slave with RTR frames. Zero disables guarding |
| **Life-Time Factor (`0x100D`)** | Multiplier applied to Guard Time on the **slave side** to compute the maximum tolerable silence interval from the master |
| **Life Time** | `Guard Time × Life-Time Factor` — if the slave sees no RTR within this window, a **Life Guard Event** is triggered |
| **Toggle Bit** | Bit 7 of the guard response byte, alternating with each successful exchange to detect missed frames |
| **Node Guard Event** | Master-side error: a slave failed to respond within guard time |
| **Life Guard Event** | Slave-side error: the slave received no RTR from master within life time |
| **NMT State** | Lower 7 bits of the guard response byte encoding the node's current NMT state |

---

## 3. Protocol Mechanics

### 3.1 Guard Time and Life-Time Factor

The Guard Time and Life-Time Factor are stored in the slave's **Object Dictionary**
at indices `0x100C` and `0x100D` respectively. The master reads (or pre-configures)
these values during network startup.

```
  Guard Time (0x100C) = 200 ms    <-- master polls slave every 200 ms
  Life-Time Factor (0x100D) = 3   <-- slave's life time = 200 × 3 = 600 ms

  Master polling interval:         |<---200ms--->|<---200ms--->|<---200ms--->|
                                   RTR           RTR           RTR

  Slave life-time window:          |<-------------- 600 ms ------------------>|
                                   If no RTR arrives in 600 ms => Life Guard Event
```

Both parameters must be **non-zero** to activate the guarding relationship.
Setting either to zero disables that side of the supervision:

- `Guard Time = 0` → Master does NOT poll this slave (no Node Guarding)
- `Life-Time Factor = 0` → Slave does NOT supervise the master (no Life Guarding)


### 3.2 RTR-Based Polling

Node Guarding uses **CAN Remote Frames (RTR)**. The master sends a zero-data-byte
frame with the RTR bit set and the COB-ID of the target slave's guard object:

```
  COB-ID (Node Guard) = 0x700 + Node-ID

  Examples:
    Node 1  -->  COB-ID 0x701
    Node 5  -->  COB-ID 0x705
    Node 127 --> COB-ID 0x77F
```

The slave's CAN hardware (or software filter) recognises this RTR and
automatically responds with a **1-byte data frame** at the same COB-ID.

```
  Master CAN Bus                         Slave Node 5
  ─────────────────────────────────────────────────────────────
       RTR frame (COB-ID=0x705, DLC=1)
  ───────────────────────────────────>
                                         Recognises own guard COB-ID
                                         Prepares response byte
       Data frame (COB-ID=0x705, DLC=1)
  <───────────────────────────────────
  ─────────────────────────────────────────────────────────────
```

> **Important:** The RTR mechanism relies on the CAN 2.0A/B specification's
> ability to send frames with the RTR bit set. This is **not supported in CAN FD**
> (see Section 7).


### 3.3 The Toggle Bit

Bit 7 of the single-byte guard response is the **Toggle Bit**. It flips (0→1→0→…)
with each successful guard exchange. This allows the master to detect:

- **Missed responses** (toggle does not change when expected)
- **Duplicate/replayed responses** (toggle value does not match expected)

```
  Response byte layout (8 bits):

  Bit:   7       6   5   4   3   2   1   0
        ┌───┬───────────────────────────────┐
        │ T │        NMT State (7 bits)     │
        └───┴───────────────────────────────┘
          │
          └── Toggle Bit: alternates 0, 1, 0, 1, ...
              First response after boot: T = 0

  NMT State values (lower 7 bits):
    0x00 = Initialising  (transitional, rarely seen in guard response)
    0x04 = Stopped
    0x05 = Operational
    0x7F = Pre-Operational
```

**Toggle Bit Sequence Example (Node in Operational state = 0x05):**

```
  Exchange #   Master Expects T=   Slave Sends Byte   Decoded
  ─────────────────────────────────────────────────────────────
      1              0             0x05  (T=0, S=0x05)   OK
      2              1             0x85  (T=1, S=0x05)   OK
      3              0             0x05  (T=0, S=0x05)   OK
      4              1             0x85  (T=1, S=0x05)   OK
      ...
  ─────────────────────────────────────────────────────────────
  If exchange #3 yields 0x85 instead of 0x05 → Toggle Error!
```


### 3.4 Node State Encoding

The lower 7 bits encode the NMT state of the slave at the time of the response:

```
  ┌─────────────────────────────────────────────────────┐
  │             NMT State Machine                        │
  │                                                     │
  │   Power-On / Reset                                  │
  │         │                                           │
  │         ▼                                           │
  │   ┌─────────────┐                                   │
  │   │Initialising │  State = 0x00                    │
  │   │  (0x00)     │                                   │
  │   └──────┬──────┘                                   │
  │          │ Boot-up message sent                     │
  │          ▼                                          │
  │   ┌─────────────┐   NMT cmd 128  ┌──────────────┐  │
  │   │Pre-Operat.  │◄───────────────│  Operational │  │
  │   │  (0x7F)     │────────────────►   (0x05)     │  │
  │   └──────┬──────┘   NMT cmd 1    └──────┬───────┘  │
  │          │ NMT cmd 2              NMT cmd 2│        │
  │          ▼                               ▼         │
  │   ┌─────────────┐                 ┌──────────────┐  │
  │   │  Stopped    │◄────────────────│  Stopped     │  │
  │   │  (0x04)     │  NMT cmd 2      │  (0x04)      │  │
  │   └─────────────┘                 └──────────────┘  │
  └─────────────────────────────────────────────────────┘

  NMT Commands:  1 = Start,  2 = Stop,  128 = Enter Pre-Operational
                 129 = Reset Node,  130 = Reset Communication
```

---

## 4. Object Dictionary Entries

These Object Dictionary (OD) entries are mandatory for a slave supporting
Node/Life Guarding:

```
  Index    Sub  Name                    Type     Access  Default
  ───────────────────────────────────────────────────────────────
  0x100C   0x00 Guard Time              UINT16   RW      0 (disabled)
  0x100D   0x00 Life Time Factor        UINT8    RW      0 (disabled)
  0x100E   0x00 COB-ID Guard Message    UINT32   R       0x700 + NodeID
  ───────────────────────────────────────────────────────────────

  0x100C  Guard Time:
          Value in milliseconds.
          0 = feature disabled.
          Written by master via SDO before starting network.

  0x100D  Life Time Factor:
          Multiplied with 0x100C to get slave's supervison timeout.
          0 = life guarding disabled (slave ignores missing RTRs).

  0x100E  COB-ID Guard Message (read-only):
          Always 0x700 + NodeID. Master derives this automatically.
          Some implementations expose it explicitly for diagnostic tools.
```

**SDO Write Example (master configuring Guard Time = 500 ms on Node 5):**

```
  SDO Download (write) to Node 5:
    Index:    0x100C
    Sub-idx:  0x00
    Data:     0x01F4  (500 decimal, little-endian: F4 01)

  CAN Frame:
    COB-ID: 0x605          (0x600 + Node-ID 5)
    Data:   60 0C 10 00 F4 01 00 00
            ^^                        Command: Initiate Download, 2 bytes
               ^^^^^^^^               Index 0x100C (little-endian)
                        ^^            Sub-index 0x00
                           ^^^^^^^^^  Value 0x01F4 = 500
```

---

## 5. Timing Diagrams (ASCII)

### 5.1 Normal Node Guarding Operation (Master Side)

```
  Master Guard Timer (Guard Time = 200 ms, Node 5)
  ────────────────────────────────────────────────────────────────────────

  Time (ms):  0    200   400   600   800   1000  1200
              │     │     │     │     │     │     │
  RTR sent:   ├─────┼─────┼─────┼─────┼─────┼─────┤
              ▼     ▼     ▼     ▼     ▼     ▼     ▼
             RTR   RTR   RTR   RTR   RTR   RTR   RTR

  Response:   │     │     │     │     ×     │     │
              ▼     ▼     ▼     ▼           ▼     ▼
             0x05  0x85  0x05  0x85        0x05  0x85
              T=0   T=1   T=0   T=1   !!   T=0   T=1
                                      ↑
                              Node Guard EVENT at 800 ms
                              (no response within guard time)
  ────────────────────────────────────────────────────────────────────────
```

### 5.2 Life Guarding on the Slave Side

```
  Slave (Guard Time=200ms, Life-Time Factor=3, Life Time=600ms)
  ────────────────────────────────────────────────────────────────────────

  RTRs from
  Master:     RTR   RTR   RTR    [Master crashes here]
  Time(ms):   0    200   400   600   800   1000
              │     │     │     │     │     │
              ▼     ▼     ▼     │     │     │
             resp  resp  resp   │     │     │
                                │     │     │
  Life Timer: ├────────────600ms────────────┤
                                            ▲
                                   LIFE GUARD EVENT fired!
                                   Slave enters Error state
                                   (calls Life Guard Error callback)
  ────────────────────────────────────────────────────────────────────────
```

### 5.3 Toggle Bit Error Detection

```
  Normal sequence:
  ─────────────────────────────────────────────────────────────────
  Master:   RTR ──────────> Node 5
  Node 5:          <─────── 0x05 (T=0)   ← expected T=0 ✓

  Master:   RTR ──────────> Node 5
  Node 5:          <─────── 0x85 (T=1)   ← expected T=1 ✓

  Master:   RTR ──────────> Node 5
  Node 5:          <─────── 0x05 (T=0)   ← expected T=0 ✓
  ─────────────────────────────────────────────────────────────────

  Toggle error (frame duplicated / node rebooted mid-sequence):
  ─────────────────────────────────────────────────────────────────
  Master:   RTR ──────────> Node 5
  Node 5:          <─────── 0x85 (T=1)   ← expected T=0 ✗
                                                 │
                                         NODE GUARD EVENT
                                         "Toggle Error"
  ─────────────────────────────────────────────────────────────────
```

### 5.4 Multi-Node Polling (Master Staggering)

```
  Master polling 3 nodes with Guard Time = 300 ms (staggered start)
  ────────────────────────────────────────────────────────────────────────

  Time(ms):  0   100  200  300  400  500  600  700  800  900
             │    │    │    │    │    │    │    │    │    │
  Node 1:    ├RTR─┤         ├RTR─┤         ├RTR─┤
             │resp│         │resp│         │resp│
             │    │         │    │         │    │
  Node 2:         ├RTR─┤         ├RTR─┤         ├RTR─┤
                  │resp│         │resp│         │resp│
                  │    │         │    │         │    │
  Node 3:              ├RTR─┤         ├RTR─┤         ├RTR─┤
                       │resp│         │resp│         │resp│

  NOTE: Staggering avoids simultaneous RTR bursts that could collide
        or overload the bus at the guard time rollover point.
  ────────────────────────────────────────────────────────────────────────
```

---

## 6. Error Conditions

### 6.1 Node Guard Event (Master Side)

Triggered when the master sends an RTR but receives no valid response within
the guard time window (or receives a response with the wrong toggle bit).

```
  Possible causes:
  ┌────────────────────────────────────────────────────────────┐
  │  1. Node powered off or crashed                            │
  │  2. CAN bus disconnected / broken cable                    │
  │  3. Node in Bus-Off state (CAN error passive/bus-off)      │
  │  4. Toggle bit mismatch (node rebooted, frame lost)        │
  │  5. Node's CAN controller overwhelmed (high bus load)      │
  └────────────────────────────────────────────────────────────┘

  Standard reaction (application-defined):
    - Log the event with node-id and timestamp
    - Optionally retry N times before declaring node lost
    - Trigger safety reaction (e-stop, alarm, degraded mode)
    - Attempt NMT Reset Communication on the node
```

### 6.2 Life Guard Event (Slave Side)

Triggered when the slave's life-time timer expires before a new RTR arrives.

```
  Standard reactions (per CiA 301):
    - The slave shall issue an EMCY (Emergency) message:
        Error Code:  0x8130  (Life Guard or Heartbeat Error)
        Error Reg:   Bit 3 (Communication Error)
    - Application callback to handle safety-critical response
    - Slave may transition to Pre-Operational or Stopped state
      depending on application configuration
```

### 6.3 Error Register Bit (Object 0x1001)

```
  Object 0x1001 - Error Register (UINT8, read-only)

  Bit  Meaning
  ─────────────────────────────────────
   0   Generic Error
   1   Current Error
   2   Voltage Error
   3   Temperature Error
   4   Communication Error  ← set on Life Guard Event
   5   Device Profile Error
   6   Reserved
   7   Manufacturer-specific
```

---

## 7. CAN FD Limitations

### 7.1 Why RTR Frames Are Incompatible with CAN FD

CAN FD (ISO 11898-1:2015) **does not support Remote Frames (RTR)**. The RTR bit
was repurposed in the CAN FD frame format as the **BRS (Bit Rate Switch)** bit.
Sending an RTR on a CAN FD network is either impossible (hardware rejects it)
or causes bus errors.

```
  CAN 2.0 Frame:                     CAN FD Frame:
  ┌────────────────────────────┐      ┌─────────────────────────────────┐
  │ SOF │ Arbitration │ Control │      │ SOF │ Arbitration │ Control     │
  │  1  │  11/29 bits  │  6bits │      │  1  │  11/29 bits  │  7/8 bits  │
  │     │              │  ┌───┐ │      │     │              │  ┌───┐     │
  │     │              │  │RTR│ │      │     │              │  │BRS│ ←── │
  │     │              │  └───┘ │      │     │              │  └───┘     │
  │     │          Bit 4 = RTR  │      │     │         Bit 4 = RRS (res) │
  └────────────────────────────┘      │     │         Bit 6 = BRS       │
                                      └─────────────────────────────────┘

  On CAN FD buses: RTR bit position is reserved/repurposed → RTR UNSUPPORTED
```

### 7.2 Migration Impact

If a network is upgraded from CAN 2.0 to CAN FD:

```
  CAN 2.0 Network (Node Guarding works):
  ────────────────────────────────────────────────────────────────
  Master ──[RTR COB-ID=0x705]──> Node5 ──[0x85 data]──> Master
  ────────────────────────────────────────────────────────────────

  CAN FD Network (Node Guarding FAILS):
  ────────────────────────────────────────────────────────────────
  Master ──[RTR attempt]──> ✗ CAN FD controller rejects RTR
                              No polling possible
                              No node supervision!
  ────────────────────────────────────────────────────────────────

  Solution: Migrate to Heartbeat Protocol (push-based, no RTR needed)
```

### 7.3 Mixed Networks

On networks that physically mix CAN 2.0 and CAN FD nodes (via CAN FD tolerant
transceivers or gateways), RTR frames from a CAN 2.0 master may still reach
CAN 2.0 slaves. However, this is an inherently fragile architecture and is
**strongly discouraged** for new designs.

---

## 8. C/C++ Implementation

### 8.1 Data Structures

```c
/* canopen_node_guard.h */
#ifndef CANOPEN_NODE_GUARD_H
#define CANOPEN_NODE_GUARD_H

#include <stdint.h>
#include <stdbool.h>

/* NMT State values (lower 7 bits of guard response) */
typedef enum {
    NMT_STATE_INITIALISING   = 0x00,
    NMT_STATE_STOPPED        = 0x04,
    NMT_STATE_OPERATIONAL    = 0x05,
    NMT_STATE_PRE_OPERATIONAL = 0x7F
} NmtState_t;

/* Guard status for a single remote node (master perspective) */
typedef struct {
    uint8_t     node_id;           /* 1..127                              */
    uint16_t    guard_time_ms;     /* Guard Time from OD 0x100C           */
    uint8_t     life_time_factor;  /* Life-Time Factor from OD 0x100D     */
    bool        toggle_expected;   /* Next expected toggle bit value       */
    NmtState_t  last_state;        /* Last received NMT state              */
    uint32_t    last_response_ms;  /* Timestamp of last valid response     */
    uint32_t    missed_responses;  /* Consecutive missed RTR responses     */
    bool        active;            /* Is this slot in use?                 */
    bool        guard_error;       /* Current guard error condition        */
} NodeGuardEntry_t;

/* Slave-side life guard state */
typedef struct {
    uint16_t    guard_time_ms;     /* Configured by master via SDO 0x100C */
    uint8_t     life_time_factor;  /* Configured by master via SDO 0x100D */
    uint32_t    life_timer_ms;     /* Countdown timer (software)           */
    bool        toggle_bit;        /* Current toggle bit to send           */
    NmtState_t  nmt_state;         /* Own NMT state                        */
    bool        life_guard_active; /* True if life guarding is configured  */
    bool        life_guard_error;  /* True if life guard event fired       */
} LifeGuardState_t;

#define MAX_GUARDED_NODES   127
#define NODE_GUARD_COB_BASE 0x700U

#endif /* CANOPEN_NODE_GUARD_H */
```

---

### 8.2 NMT Master: Sending RTR and Evaluating Response

```c
/* master_node_guard.c
 * NMT Master side: RTR transmission and response evaluation.
 *
 * Assumes the following platform-specific functions are available:
 *   can_send_rtr(uint32_t cob_id, uint8_t dlc)
 *   get_time_ms()  --> returns monotonic ms timestamp
 *   on_node_guard_event(uint8_t node_id, const char* reason)
 */

#include "canopen_node_guard.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Module state                                                        */
/* ------------------------------------------------------------------ */
static NodeGuardEntry_t s_guard_table[MAX_GUARDED_NODES];
static uint8_t          s_guard_count = 0;

/* ------------------------------------------------------------------ */
/*  Register a node for guarding                                        */
/* ------------------------------------------------------------------ */
bool ng_master_register_node(uint8_t  node_id,
                              uint16_t guard_time_ms,
                              uint8_t  life_time_factor)
{
    if (node_id < 1 || node_id > 127) return false;
    if (s_guard_count >= MAX_GUARDED_NODES) return false;

    NodeGuardEntry_t *e = &s_guard_table[s_guard_count++];
    memset(e, 0, sizeof(*e));

    e->node_id          = node_id;
    e->guard_time_ms    = guard_time_ms;
    e->life_time_factor = life_time_factor;
    e->toggle_expected  = false;   /* First response toggle bit = 0 */
    e->last_state       = NMT_STATE_INITIALISING;
    e->active           = true;

    return true;
}

/* ------------------------------------------------------------------ */
/*  Periodic guard poll – call from a timer ISR or RTOS task           */
/* ------------------------------------------------------------------ */
void ng_master_periodic_poll(void)
{
    uint32_t now = get_time_ms();

    for (int i = 0; i < s_guard_count; i++) {
        NodeGuardEntry_t *e = &s_guard_table[i];
        if (!e->active) continue;

        uint32_t elapsed = now - e->last_response_ms;

        /* Check for overdue response first */
        if (elapsed > e->guard_time_ms && e->last_response_ms != 0) {
            e->missed_responses++;
            if (!e->guard_error) {
                e->guard_error = true;
                on_node_guard_event(e->node_id, "No response within guard time");
            }
        }

        /* Send RTR if guard time has elapsed since last send */
        if (elapsed >= e->guard_time_ms) {
            uint32_t cob_id = NODE_GUARD_COB_BASE + e->node_id;
            can_send_rtr(cob_id, 1U);  /* DLC = 1 (expects 1 byte back) */
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Process an incoming guard response frame                            */
/* ------------------------------------------------------------------ */
void ng_master_on_guard_response(uint32_t cob_id,
                                  const uint8_t *data,
                                  uint8_t dlc)
{
    if (dlc != 1) return;                    /* Guard response is always 1 byte */
    if (cob_id < 0x701 || cob_id > 0x77F) return;

    uint8_t node_id = (uint8_t)(cob_id - NODE_GUARD_COB_BASE);
    uint8_t response_byte = data[0];

    /* Find the node in our guard table */
    NodeGuardEntry_t *e = NULL;
    for (int i = 0; i < s_guard_count; i++) {
        if (s_guard_table[i].node_id == node_id && s_guard_table[i].active) {
            e = &s_guard_table[i];
            break;
        }
    }
    if (!e) return;  /* Unknown node */

    /* Extract toggle bit (bit 7) and NMT state (bits 6..0) */
    bool       toggle = (response_byte & 0x80U) != 0;
    NmtState_t state  = (NmtState_t)(response_byte & 0x7FU);

    /* Validate toggle bit */
    if (toggle != e->toggle_expected) {
        on_node_guard_event(node_id, "Toggle bit mismatch");
        /* Do NOT update toggle_expected – wait for re-sync or node reset */
        return;
    }

    /* Response is valid */
    e->toggle_expected  = !e->toggle_expected;  /* Flip for next exchange */
    e->last_state       = state;
    e->last_response_ms = get_time_ms();
    e->missed_responses = 0;
    e->guard_error      = false;
}

/* ------------------------------------------------------------------ */
/*  Query current node state (for application use)                      */
/* ------------------------------------------------------------------ */
NmtState_t ng_master_get_node_state(uint8_t node_id)
{
    for (int i = 0; i < s_guard_count; i++) {
        if (s_guard_table[i].node_id == node_id)
            return s_guard_table[i].last_state;
    }
    return NMT_STATE_INITIALISING;  /* Unknown node */
}
```

---

### 8.3 NMT Slave: Responding to Guard RTR

On the slave side, the CAN hardware or driver must be configured to
automatically respond to RTR frames at the guard COB-ID, **or** the
application must intercept the RTR and send the response manually.

```c
/* slave_node_guard.c
 * NMT Slave side: handle RTR and prepare guard response byte.
 */

#include "canopen_node_guard.h"

static LifeGuardState_t s_lg;
static uint8_t          s_own_node_id = 0;

/* ------------------------------------------------------------------ */
/*  Initialise slave guard state                                        */
/* ------------------------------------------------------------------ */
void ng_slave_init(uint8_t node_id, NmtState_t initial_nmt_state)
{
    s_own_node_id   = node_id;
    s_lg.nmt_state  = initial_nmt_state;
    s_lg.toggle_bit = false;    /* First response has toggle bit = 0    */
    s_lg.guard_time_ms    = 0;  /* 0 = disabled until master configures */
    s_lg.life_time_factor = 0;
    s_lg.life_timer_ms    = 0;
    s_lg.life_guard_active = false;
    s_lg.life_guard_error  = false;
}

/* ------------------------------------------------------------------ */
/*  Called by SDO handler when master writes 0x100C or 0x100D          */
/* ------------------------------------------------------------------ */
void ng_slave_set_guard_time(uint16_t guard_time_ms)
{
    s_lg.guard_time_ms = guard_time_ms;
    /* Recalculate life timer */
    s_lg.life_guard_active =
        (s_lg.guard_time_ms > 0 && s_lg.life_time_factor > 0);
    s_lg.life_timer_ms =
        (uint32_t)s_lg.guard_time_ms * s_lg.life_time_factor;
}

void ng_slave_set_life_time_factor(uint8_t factor)
{
    s_lg.life_time_factor = factor;
    s_lg.life_guard_active =
        (s_lg.guard_time_ms > 0 && s_lg.life_time_factor > 0);
    s_lg.life_timer_ms =
        (uint32_t)s_lg.guard_time_ms * s_lg.life_time_factor;
}

/* ------------------------------------------------------------------ */
/*  Update own NMT state (called by NMT state machine)                  */
/* ------------------------------------------------------------------ */
void ng_slave_set_nmt_state(NmtState_t state)
{
    s_lg.nmt_state = state;
}

/* ------------------------------------------------------------------ */
/*  Build the guard response byte                                        */
/* ------------------------------------------------------------------ */
static uint8_t build_guard_response_byte(void)
{
    uint8_t byte = (uint8_t)(s_lg.nmt_state & 0x7FU);
    if (s_lg.toggle_bit) {
        byte |= 0x80U;  /* Set toggle bit */
    }
    s_lg.toggle_bit = !s_lg.toggle_bit;  /* Flip for next time */
    return byte;
}

/* ------------------------------------------------------------------ */
/*  Called when an RTR is received at our guard COB-ID                  */
/* ------------------------------------------------------------------ */
void ng_slave_on_guard_rtr_received(void)
{
    uint8_t  response  = build_guard_response_byte();
    uint32_t cob_id    = NODE_GUARD_COB_BASE + s_own_node_id;

    /* Transmit the guard response */
    can_send_data(cob_id, &response, 1U);

    /* Reset life guard timer on every valid RTR */
    if (s_lg.life_guard_active) {
        s_lg.life_timer_ms =
            (uint32_t)s_lg.guard_time_ms * s_lg.life_time_factor;
        s_lg.life_guard_error = false;
    }
}
```

---

### 8.4 Life Guarding on the Slave Side

The slave decrements a software timer each millisecond (or equivalent) and
fires a callback if it expires before the next RTR arrives.

```c
/* life_guard_timer.c
 * Decrement life guard timer and fire event on expiry.
 * Call ng_slave_tick_1ms() from a 1 ms hardware timer ISR.
 */

#include "canopen_node_guard.h"

/* Declared in slave_node_guard.c */
extern LifeGuardState_t s_lg;

/* Forward declaration of application callback */
void on_life_guard_event(void);

/* EMCY error code for Life Guard / Heartbeat error */
#define EMCY_ERR_LIFE_GUARD  0x8130U

/* ------------------------------------------------------------------ */
/*  1 ms tick – decrement life timer                                    */
/* ------------------------------------------------------------------ */
void ng_slave_tick_1ms(void)
{
    if (!s_lg.life_guard_active) return;
    if (s_lg.life_guard_error)   return;  /* Already in error state */

    if (s_lg.life_timer_ms > 0) {
        s_lg.life_timer_ms--;
    }

    if (s_lg.life_timer_ms == 0) {
        /* Life guard event! */
        s_lg.life_guard_error = true;

        /* Send Emergency message */
        send_emcy(EMCY_ERR_LIFE_GUARD,
                  0x10U /* Communication error bit in error register */);

        /* Notify application */
        on_life_guard_event();
    }
}

/* ------------------------------------------------------------------ */
/*  Application callback example                                        */
/* ------------------------------------------------------------------ */
void on_life_guard_event(void)
{
    /* Example: transition to Pre-Operational and assert safe outputs */
    ng_slave_set_nmt_state(NMT_STATE_PRE_OPERATIONAL);
    set_all_outputs_safe();         /* Application-specific safety action */
    log_error("Life Guard Event: master silent for %u ms",
              (uint32_t)s_lg.guard_time_ms * s_lg.life_time_factor);
}
```

---

### 8.5 Full Guard State-Machine Example

A more realistic C++ implementation using a state machine pattern:

```cpp
// NodeGuardMaster.hpp  –  C++17

#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <chrono>

enum class GuardNodeState {
    IDLE,           // Not yet started
    POLLING,        // Normal operation
    MISSED,         // One or more responses missed
    ERROR,          // Guard error declared
    INACTIVE        // Guard disabled (guard_time = 0)
};

struct GuardNodeInfo {
    uint8_t         node_id;
    uint16_t        guard_time_ms;
    uint8_t         life_time_factor;
    bool            toggle_expected    = false;
    uint8_t         nmt_state          = 0x00;
    GuardNodeState  state              = GuardNodeState::IDLE;
    uint32_t        consecutive_misses = 0;
    uint32_t        max_misses         = 3;  // Retries before ERROR
    std::chrono::steady_clock::time_point last_poll_time;
    std::chrono::steady_clock::time_point last_response_time;
};

class NodeGuardMaster {
public:
    using GuardErrorCb = std::function<void(uint8_t node_id,
                                            GuardNodeState state,
                                            const std::string& reason)>;

    explicit NodeGuardMaster(GuardErrorCb error_cb)
        : m_error_cb(std::move(error_cb)) {}

    /* Register a remote node for guarding */
    void register_node(uint8_t node_id,
                       uint16_t guard_time_ms,
                       uint8_t life_time_factor,
                       uint32_t max_misses = 3)
    {
        GuardNodeInfo info{};
        info.node_id          = node_id;
        info.guard_time_ms    = guard_time_ms;
        info.life_time_factor = life_time_factor;
        info.max_misses       = max_misses;
        info.state            = (guard_time_ms > 0)
                                    ? GuardNodeState::IDLE
                                    : GuardNodeState::INACTIVE;
        m_nodes[node_id] = info;
    }

    /* Called periodically from main loop or timer task */
    void poll(std::function<void(uint32_t cob_id)> send_rtr_fn)
    {
        auto now = std::chrono::steady_clock::now();

        for (auto& [id, node] : m_nodes) {
            if (node.state == GuardNodeState::INACTIVE) continue;

            auto elapsed_ms = std::chrono::duration_cast<
                std::chrono::milliseconds>(now - node.last_poll_time).count();

            if (elapsed_ms >= node.guard_time_ms) {
                /* Check if the previous RTR got a response */
                if (node.state == GuardNodeState::POLLING) {
                    auto since_resp = std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                            now - node.last_response_time).count();

                    if (since_resp > node.guard_time_ms) {
                        node.consecutive_misses++;
                        if (node.consecutive_misses >= node.max_misses) {
                            transition_error(node, "Max consecutive misses");
                        } else {
                            node.state = GuardNodeState::MISSED;
                        }
                    }
                }

                /* Send RTR for next cycle */
                uint32_t cob_id = 0x700U + node.node_id;
                send_rtr_fn(cob_id);
                node.last_poll_time = now;
                node.state = GuardNodeState::POLLING;
            }
        }
    }

    /* Called when a guard response frame arrives */
    void on_response(uint32_t cob_id, uint8_t response_byte)
    {
        uint8_t node_id = static_cast<uint8_t>(cob_id - 0x700U);
        auto it = m_nodes.find(node_id);
        if (it == m_nodes.end()) return;

        GuardNodeInfo& node = it->second;
        bool  toggle = (response_byte & 0x80U) != 0;
        uint8_t nmt  = (response_byte & 0x7FU);

        if (toggle != node.toggle_expected) {
            transition_error(node, "Toggle bit mismatch");
            return;
        }

        /* Valid response */
        node.toggle_expected    = !node.toggle_expected;
        node.nmt_state          = nmt;
        node.consecutive_misses = 0;
        node.last_response_time = std::chrono::steady_clock::now();
        node.state              = GuardNodeState::POLLING;
    }

private:
    void transition_error(GuardNodeInfo& node, const std::string& reason)
    {
        node.state = GuardNodeState::ERROR;
        if (m_error_cb) {
            m_error_cb(node.node_id, node.state, reason);
        }
    }

    std::unordered_map<uint8_t, GuardNodeInfo> m_nodes;
    GuardErrorCb m_error_cb;
};
```

**Usage example:**

```cpp
// main.cpp  –  Example usage of NodeGuardMaster

#include "NodeGuardMaster.hpp"
#include <iostream>

int main()
{
    /* Create master with error callback */
    NodeGuardMaster master([](uint8_t node_id,
                               GuardNodeState state,
                               const std::string& reason) {
        std::cerr << "[GUARD ERROR] Node " << (int)node_id
                  << " | State: " << (int)state
                  << " | Reason: " << reason << "\n";
        /* Application-specific: trigger e-stop, alarm, etc. */
    });

    /* Register nodes */
    master.register_node(1,   200, 3, 3);  /* Node 1: 200ms guard, 600ms life */
    master.register_node(5,   500, 2, 2);  /* Node 5: 500ms guard, 1000ms life */
    master.register_node(10, 1000, 3, 1);  /* Node 10: 1s guard, 3s life      */

    /* CAN send RTR (platform-specific) */
    auto send_rtr = [](uint32_t cob_id) {
        /* platform_can_send_rtr(cob_id, 1); */
        std::cout << "RTR -> COB-ID 0x" << std::hex << cob_id << "\n";
    };

    /* Simulated main loop */
    while (running) {
        master.poll(send_rtr);

        /* CAN receive callback feeds this: */
        /* master.on_response(0x705, received_byte); */

        sleep_ms(1);
    }
}
```

---

## 9. Migration Path to Heartbeat Protocol

### 9.1 Why Migrate?

```
  Node Guarding / Life Guarding         Heartbeat Protocol
  ────────────────────────────────────────────────────────────────────
  RTR-based (pull model)                Push-based (node sends autonomously)
  Requires master to poll               No master polling needed
  Toggle bit complexity                 No toggle bit
  Broken on CAN FD                      Works on CAN FD
  Bus load ∝ N nodes                    Bus load ∝ N nodes (same COB-IDs)
  Master must know all node-IDs early   Consumer subscribes by COB-ID
  Life guarding for slave↔master        Consumer timeout = simple timer
  Defined in CiA 301 as legacy          Defined in CiA 301 as recommended
  ────────────────────────────────────────────────────────────────────
```

### 9.2 Heartbeat Object Dictionary Entries

```
  Index    Sub  Name                    Type     Default
  ─────────────────────────────────────────────────────────
  0x1017   0x00 Producer Heartbeat Time UINT16   0 (disabled)
                (slave: period in ms to send HB)

  0x1016   0x00 Number of HB consumers UINT8    n
  0x1016   0x01 Consumer HB Time[1]    UINT32   0
  0x1016   0x02 Consumer HB Time[2]    UINT32   0
           ...
  0x1016   0x7F Consumer HB Time[127]  UINT32   0

  Consumer HB Time format (UINT32):
    Bits 31..24: reserved
    Bits 23..16: Monitored Node-ID (1..127)
    Bits 15..0 : Heartbeat Consumer Time (ms, 0=disabled)
```

### 9.3 Heartbeat COB-ID

The heartbeat uses the same COB-ID range as node guarding:

```
  COB-ID = 0x700 + Node-ID   (same as Node Guard!)

  Heartbeat frame: 1 data byte, NMT state (no toggle bit)
    Byte 0: NMT State (0x00, 0x04, 0x05, or 0x7F)
    No RTR, no toggle bit, sent autonomously by the node.
```

### 9.4 Step-by-Step Migration

```
  Step 1: Add heartbeat producer to each slave
  ──────────────────────────────────────────────────────────────
  SDO Write to slave:
    Index 0x1017, Sub 0x00  ← set heartbeat period (e.g. 200 ms)
  Slave now sends 0x700+NodeID every 200 ms automatically.

  Step 2: Configure master as heartbeat consumer
  ──────────────────────────────────────────────────────────────
  SDO Write to master (or configure in EDS):
    Index 0x1016, Sub 0x01  ← 0x00050384
                               ^^          Node-ID = 5
                                 ^^^^      Timeout = 0x0384 = 900 ms
                                           (4.5 × heartbeat period)

  Step 3: Disable node guarding on both sides
  ──────────────────────────────────────────────────────────────
  SDO Write to slave:
    Index 0x100C, Sub 0x00  ← set to 0 (disable guard time)
    Index 0x100D, Sub 0x00  ← set to 0 (disable life-time factor)
  Master: stop sending RTR frames for this node.

  Step 4: Validate
  ──────────────────────────────────────────────────────────────
  - Confirm heartbeat frames arriving at master
  - Simulate node crash → verify EMCY 0x8130 fired by consumer
  - Run on CAN FD bus → verify no RTR issues
```

### 9.5 Heartbeat Consumer in C

```c
/* heartbeat_consumer.c
 * Minimal heartbeat consumer on the master/monitor side.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_HB_PRODUCERS  127

typedef struct {
    uint8_t  node_id;
    uint16_t timeout_ms;       /* From OD 0x1016 */
    uint16_t remaining_ms;     /* Countdown timer */
    uint8_t  last_nmt_state;
    bool     active;
    bool     error;
} HbConsumerEntry_t;

static HbConsumerEntry_t s_consumers[MAX_HB_PRODUCERS];
static uint8_t           s_consumer_count = 0;

void hb_consumer_register(uint8_t node_id, uint16_t timeout_ms)
{
    HbConsumerEntry_t *e = &s_consumers[s_consumer_count++];
    memset(e, 0, sizeof(*e));
    e->node_id    = node_id;
    e->timeout_ms = timeout_ms;
    e->remaining_ms = timeout_ms;
    e->active     = true;
}

/* Called every 1 ms */
void hb_consumer_tick_1ms(void)
{
    for (int i = 0; i < s_consumer_count; i++) {
        HbConsumerEntry_t *e = &s_consumers[i];
        if (!e->active || e->error) continue;

        if (e->remaining_ms > 0) {
            e->remaining_ms--;
        }
        if (e->remaining_ms == 0) {
            e->error = true;
            send_emcy(0x8130U, 0x10U);  /* Same error code as life guard */
            on_heartbeat_timeout(e->node_id);
        }
    }
}

/* Called when a heartbeat frame arrives (COB-ID 0x700..0x77F) */
void hb_consumer_on_heartbeat(uint32_t cob_id, uint8_t nmt_state)
{
    uint8_t node_id = (uint8_t)(cob_id - 0x700U);
    for (int i = 0; i < s_consumer_count; i++) {
        HbConsumerEntry_t *e = &s_consumers[i];
        if (e->node_id == node_id && e->active) {
            e->remaining_ms   = e->timeout_ms;  /* Reset timer */
            e->last_nmt_state = nmt_state;
            e->error          = false;
            return;
        }
    }
}
```

### 9.6 Node Guarding vs Heartbeat Comparison (ASCII)

```
  NODE GUARDING (RTR-based):
  ──────────────────────────────────────────────────────────────────
  Master                    Bus                      Slave Node 5
    │                        │                           │
    │──[RTR 0x705]──────────►│──────────────────────────►│
    │                        │◄──[0x85 data]─────────────│
    │                        │                           │
    │──[RTR 0x705]──────────►│──────────────────────────►│
    │                        │◄──[0x05 data]─────────────│
  ──────────────────────────────────────────────────────────────────
  Bus traffic: 2 frames per guard interval per node
  Master drives the timing; slave is passive

  HEARTBEAT (push-based):
  ──────────────────────────────────────────────────────────────────
  Master/Consumer           Bus                      Slave Node 5
    │                        │                           │
    │◄──────────────────────[0x05 @ 0x705]───────────────│
    │                        │                           │
    │◄──────────────────────[0x05 @ 0x705]───────────────│
    │                        │                           │
  ──────────────────────────────────────────────────────────────────
  Bus traffic: 1 frame per heartbeat interval per node
  Slave drives the timing; master is passive consumer
  Works on CAN FD (no RTR required)
```

---

## 10. Summary

Node Guarding and Life Guarding represent the **original CANopen NMT monitoring
mechanism**, built on CAN 2.0's Remote Frame (RTR) capability. While functional
and still widely deployed, they carry a number of inherent limitations that
make the heartbeat protocol preferable for all new designs.

**Key points to remember:**

```
  ┌─────────────────────────────────────────────────────────────────────┐
  │  MECHANISM       │  Guard Time (0x100C) × Life-Time Factor (0x100D) │
  ├─────────────────────────────────────────────────────────────────────┤
  │  MASTER ROLE     │  Sends RTR at Guard Time interval; checks toggle  │
  │                  │  bit and NMT state in 1-byte response             │
  ├─────────────────────────────────────────────────────────────────────┤
  │  SLAVE ROLE      │  Responds to RTR with state+toggle byte;         │
  │                  │  fires Life Guard Event if RTR absent > life time │
  ├─────────────────────────────────────────────────────────────────────┤
  │  TOGGLE BIT      │  Bit 7 of response; alternates each exchange;    │
  │                  │  detects missed/replayed frames                   │
  ├─────────────────────────────────────────────────────────────────────┤
  │  COB-ID          │  0x700 + Node-ID (same range as heartbeat)        │
  ├─────────────────────────────────────────────────────────────────────┤
  │  ERROR (Master)  │  Node Guard Event → no/wrong response received   │
  │  ERROR (Slave)   │  Life Guard Event → EMCY 0x8130 sent             │
  ├─────────────────────────────────────────────────────────────────────┤
  │  CAN FD          │  NOT SUPPORTED – RTR frames undefined in CAN FD  │
  ├─────────────────────────────────────────────────────────────────────┤
  │  MIGRATION       │  Replace with Heartbeat (OD 0x1016/0x1017):     │
  │                  │  1) Set 0x1017 on slave, 2) Set 0x1016 on master │
  │                  │  3) Zero-out 0x100C and 0x100D to disable guard  │
  └─────────────────────────────────────────────────────────────────────┘
```

**When to keep Node Guarding:**
- Maintaining existing CAN 2.0 networks where all nodes already implement it
- Constrained legacy devices that do not support heartbeat
- Interoperability with equipment that exclusively uses this mechanism

**When to migrate to Heartbeat:**
- Any new CAN FD deployment
- Networks with more than ~20 nodes (RTR traffic overhead becomes significant)
- Safety-critical systems requiring deterministic, push-based supervision
- Simplifying master implementation (no RTR scheduling, no toggle tracking)

---

*References: CiA 301 v4.2.0 – CANopen Application Layer and Communication Profile;
ISO 11898-1:2015 – Road vehicles – Controller area network (CAN).*# Node Guarding & Life Guarding (Legacy)

> _TODO: add content_
