# 15. EMCY — Emergency Object & Error Codes

**Structure covered:**
- **COB-ID addressing** — formula `0x080 + Node-ID`, the 32-bit Object 0x1014 layout including the enable/disable bit, and a full ASCII map of the 11-bit CAN identifier space
- **8-byte frame layout** — annotated byte-by-byte diagram, the "Error Reset" convention (EEC = 0x0000), and a decoded frame example
- **Error code classes** — full table from 0x00xx through 0xFF00, a hierarchical bit-field diagram, and the communication sub-codes (0x81xx) in detail
- **Object 0x1001** (Error Register) — bit-mask table with all 8 flags
- **Object 0x1003** (Pre-Defined Error Field) — sub-index table, 32-bit entry format, and a before/after FIFO-shift diagram
- **Object 0x1015** (Inhibit Time) — timing ASCII diagram showing blocked vs. transmitted frames
- **Objects 0x1014 / 0x1028** — consumer whitelist structure and a filtering flowchart
- **NMT state interaction** — ASCII state machine showing where EMCY is allowed/forbidden

**C/C++ examples include:**
1. Data structures (`EMCY_Frame_t`, error register flags, producer/consumer contexts)
2. Full producer implementation with inhibit time and history management
3. Practical usage — DC-link undervoltage with hysteresis
4. Consumer with COB-ID whitelist filtering and error class dispatch
5. SDO-based history readout (Object 0x1003)
6. Object Dictionary integration snippet
7. A C++ class wrapper with `std::function` callback


> **CANopen Application Layer | CiA 301 / EN 50325-4**

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [EMCY COB-ID and Addressing](#2-emcy-cob-id-and-addressing)
3. [8-Byte Frame Layout](#3-8-byte-frame-layout)
4. [Standard Error Code Classes](#4-standard-error-code-classes)
5. [Error Register — Object 0x1001](#5-error-register--object-0x1001)
6. [Pre-Defined Error Field — Object 0x1003](#6-pre-defined-error-field--object-0x1003)
7. [Inhibit Time — Object 0x1015](#7-inhibit-time--object-0x1015)
8. [EMCY Consumer Object — Object 0x1028](#8-emcy-consumer-object--object-0x1028)
9. [EMCY Consumer Filtering](#9-emcy-consumer-filtering)
10. [State Machine and EMCY Behaviour](#10-state-machine-and-emcy-behaviour)
11. [C/C++ Programming Examples](#11-cc-programming-examples)
12. [Summary](#12-summary)

---

## 1. Introduction

The **Emergency (EMCY) object** is a CANopen communication mechanism that allows a device
(node) to broadcast fault or error conditions onto the network immediately when they occur.
It is a **producer–consumer** model: each node produces at most one EMCY stream
(identified by its Node-ID), and any number of other nodes may consume it.

EMCY messages are **event-driven** (not polled), making them the fastest path for error
notification in a CANopen system. Typical use cases include:

- Overcurrent / overvoltage detection in a motor drive
- Encoder or sensor failure
- Internal memory errors
- Communication stack faults
- Application-level watchdog timeouts

```
  +-----------+        CAN bus         +-----------+   +-----------+
  |  Node 1   |  EMCY (COB-ID 0x181)   |  Node 2   |   |  Master   |
  | (Producer)|----------------------->| (Consumer)|   | (Consumer)|
  |           |                        |           |   |           |
  | generates |                        | filters / |   | logs /    |
  | on error  |                        | reacts    |   | alarams   |
  +-----------+                        +-----------+   +-----------+
```

Key properties:

| Property              | Value / Range                        |
|-----------------------|--------------------------------------|
| CAN frame type        | Data frame (no RTR)                  |
| DLC (data length)     | Always 8 bytes                       |
| Default COB-ID base   | 0x080 + Node-ID                      |
| Node-ID range         | 1 … 127 (0x01 … 0x7F)                |
| COB-ID range          | 0x081 … 0x0FF                        |
| Transmission type     | Event-driven (asynchronous)          |
| Direction             | Node → Network (broadcast)           |

---

## 2. EMCY COB-ID and Addressing

### 2.1 Default COB-ID Formula

```
EMCY COB-ID = 0x080 + Node-ID
```

```
  Node-ID  |  EMCY COB-ID
  ---------+--------------
   0x01    |   0x081
   0x02    |   0x082
   0x0A    |   0x08A
   0x7F    |   0x0FF
```

### 2.2 EMCY COB-ID Object — Object 0x1014

The EMCY COB-ID is stored and configurable via **Object Dictionary entry 0x1014**,
a 32-bit value.

```
  Bit 31          Bit 30        Bits 29..11   Bits 10..0
  +---------------+-------------+-------------+------------+
  |  Valid (inv.) | RTR allowed |   reserved  |  COB-ID    |
  |  0=enabled    |  (reserved) |   (0)       |  (11-bit)  |
  +---------------+-------------+-------------+------------+
```

Bit 31 = 1 means the EMCY object is **disabled** (producer will not transmit).
Bit 31 = 0 means **enabled** (default).

The lower 11 bits hold the actual CAN identifier. The default reset value is:

```
  0x00000080 | Node-ID   (e.g. Node 1 → 0x00000081)
```

### 2.3 ASCII Diagram — COB-ID Allocation on CAN Bus

```
  CAN Identifier space (11-bit, 0x000 … 0x7FF)
  |
  0x000  NMT (Master → Nodes)
  0x080  SYNC
  0x081─0x0FF  ◄── EMCY objects  (Node-IDs 1..127)
  |              ╔═══════╗  ╔═══════╗  ╔═══════╗
  |              ║ 0x081 ║  ║ 0x082 ║  ║ 0x0FF ║
  |              ║ Node1 ║  ║ Node2 ║  ║Node127║
  |              ╚═══════╝  ╚═══════╝  ╚═══════╝
  0x100─0x17F  TPDO1
  0x180─0x1FF  TPDO2
  ...
  0x580─0x5FF  SDO (Tx)
  0x600─0x67F  SDO (Rx)
  0x700─0x77F  NMT Heartbeat / Node guarding
  0x7FF  (reserved)
```

---

## 3. 8-Byte Frame Layout

Every EMCY message is exactly **8 bytes** (DLC = 8), regardless of how many bytes
contain meaningful data.

```
  Byte:  0       1       2       3       4       5       6       7
         +-------+-------+-------+-------+-------+-------+-------+-------+
         | EEC_L | EEC_H | ER    | VD[0] | VD[1] | VD[2] | VD[3] | VD[4] |
         +-------+-------+-------+-------+-------+-------+-------+-------+
              \_____/       |      \____________________________________/
               Emergency   Error            Vendor-Specific
               Error Code  Register         Data (5 bytes)
               (16-bit LE)  (8-bit)
```

| Bytes | Name                     | Size     | Description                                                 |
|-------|--------------------------|----------|-------------------------------------------------------------|
| 0–1   | Emergency Error Code     | 16-bit LE| Standardised error code (see Section 4)                     |
| 2     | Error Register           | 8-bit    | Copy of Object 0x1001 at time of error (see Section 5)      |
| 3–7   | Manufacturer-Specific    | 5 bytes  | Free use: vendor data, sub-codes, timestamps, diagnostics   |

### 3.1 "Error Reset / No Error" Message

When an error condition clears, the device sends a special EMCY with:

```
  Byte 0–1  =  0x0000   (Emergency Error Code = "No Error")
  Byte 2    =  updated Error Register (ideally 0x00 if all cleared)
  Byte 3–7  =  0x00 0x00 0x00 0x00 0x00
```

This tells consumers the previous error is resolved.

### 3.2 Frame Layout — Visual

```
  ┌─────────────────────────────────────────────────────────────────┐
  │  CAN Frame  COB-ID=0x082  (Node 2)  DLC=8                       │
  ├────────┬────────┬────────┬──────────────────────────────────────┤
  │  0x42  │  0x43  │  0x04  │  0x00  0x00  0x00  0x00  0x00        │
  ├────────┴────────┼────────┼──────────────────────────────────────┤
  │  EEC = 0x4342   │ ER=0x04│  Vendor Data (unused here)           │
  │  "DC Link       │ Comm.  │                                      │
  │   Undervoltage" │ Error  │                                      │
  └─────────────────┴────────┴──────────────────────────────────────┘
```

---

## 4. Standard Error Code Classes

Emergency Error Codes (EEC) are 16-bit values grouped into classes by the **upper byte**
(most-significant byte, since the value is little-endian on the wire the high byte is byte[1]).

### 4.1 Error Code Map

```
  Upper byte    Class                         Examples
  ───────────  ────────────────────────────── ──────────────────────────────────
  0x00xx        No Error / Reset              0x0000 = No Error
  0x10xx        Generic Error                 0x1000 = Generic Error
  0x20xx        Current Error                 0x2310 = Output overcurrent
  0x30xx        Voltage Error                 0x3210 = Mains overvoltage
                                              0x3220 = Mains undervoltage
                                              0x3310 = Output overvoltage
  0x40xx        Temperature Error             0x4210 = Drive/amplifier temp high
                                              0x4310 = Device temp high
  0x50xx        Device Hardware               0x5112 = Supply voltage low
                                              0x5530 = Data record invalid
  0x60xx        Device Software               0x6100 = Internal software error
                                              0x6320 = Parameter error
  0x70xx        Additional Modules            0x7300 = Sensor error
                                              0x7305 = Encoder error
  0x80xx        Monitoring                    0x8110 = CAN RX overrun
                                              0x8120 = CAN TX overrun
                                              0x8130 = Heartbeat / guard error
                                              0x8140 = Bus off
                                              0x8150 = ID collision
  0x90xx        External Error                0x9000 = External error
  0xF0xx        Additional Functions          0xF000 = Generic function error
  0xFF00        Device-Specific (Vendor)      Manufacturer-defined
```

### 4.2 Hierarchical Error Code Structure

```
  16-bit Emergency Error Code
  ┌──────────────────────┬──────────────────────┐
  │   High Byte [15..8]  │   Low Byte  [7..0]   │
  │   Error Class        │   Error Sub-Code     │
  └──────────────────────┴──────────────────────┘
         │                        │
         ▼                        ▼
  0x20 = Current          0x10 = Sustained
  0x30 = Voltage          0x20 = Peak
  0x40 = Temperature      0x00 = Unspecified
  0x80 = Communication    (device-specific detail)
```

### 4.3 Communication Error Codes (0x81xx) Detail

```
  Code    Meaning
  ──────  ──────────────────────────────────────────────
  0x8110  CAN receive FIFO overflow
  0x8120  CAN transmit FIFO overflow / error passive
  0x8130  Life guard / heartbeat timeout (node guarding)
  0x8140  CAN bus off (too many errors, node silent)
  0x8150  CAN-ID collision detected
  0x8210  Protocol error (PDO length mismatch)
  0x8220  Protocol error (PDO length exceeded)
  0x8230  DAM-MPDO not processed
  0x8240  Unexpected SYNC data length
  0x8250  RPDO timeout
```

---

## 5. Error Register — Object 0x1001

Object **0x1001** (Error Register) is an 8-bit read-only value that summarises the
**active error categories** as a bitmask. It is mandatory in every CANopen device.

```
  Bit   Meaning
  ───   ──────────────────────────────────────────────
   0    Generic Error          (always set on any error)
   1    Current                (current-related fault)
   2    Voltage                (voltage-related fault)
   3    Temperature            (thermal fault)
   4    Communication Error    (CAN / NMT fault)
   5    Device Profile Specific
   6    Reserved (0)
   7    Manufacturer Specific
```

Byte 2 of every EMCY frame **must** contain a snapshot of Object 0x1001 at the moment
the emergency is triggered.

```
  Error Register = 0x05  →  binary 0000 0101
                                          ││
                                          │└─ Bit 0: Generic error set
                                          └── Bit 2: Voltage error set
```

---

## 6. Pre-Defined Error Field — Object 0x1003

Object **0x1003** (Pre-Defined Error Field) is a mandatory object that stores a
**history of the last N EMCY error codes** in a FIFO ring. Sub-index 0 holds the
count of logged entries; sub-indices 1..N hold the codes (newest first).

### 6.1 Object 0x1003 Structure

```
  Sub-Index  Access  Type    Description
  ─────────  ──────  ──────  ──────────────────────────────────────────
  0x00       RW      UINT8   Number of actual errors stored (0..N)
                             Writing 0 clears the history
  0x01       RO      UINT32  Most recent error (newest)
  0x02       RO      UINT32  Second most recent error
  ...
  0x0N       RO      UINT32  Oldest error
```

### 6.2 32-Bit Entry Format in 0x1003

```
  Bits 31..16       Bits 15..0
  ┌─────────────────┬─────────────────────────────────────────────┐
  │ Manufacturer-   │       Emergency Error Code (EEC)            │
  │ Specific        │       (same as bytes 0–1 of EMCY frame)     │
  └─────────────────┴─────────────────────────────────────────────┘
```

### 6.3 FIFO Shift on New Error

```
  Before new error arrives:
    0x1003[0] = 3
    0x1003[1] = 0x00008130   ← newest
    0x1003[2] = 0x00004210
    0x1003[3] = 0x00008140   ← oldest

  After error 0x00002310 (output overcurrent) occurs:
    0x1003[0] = 4
    0x1003[1] = 0x00002310   ← newest (inserted at front)
    0x1003[2] = 0x00008130
    0x1003[3] = 0x00004210
    0x1003[4] = 0x00008140   ← oldest (may be dropped if N=3)
```

---

## 7. Inhibit Time — Object 0x1015

To prevent a malfunctioning node from flooding the CAN bus with EMCY messages,
**Object 0x1015** defines the **EMCY Inhibit Time**.

```
  Object 0x1015 — EMCY Inhibit Time
  ──────────────────────────────────
  Type   : UINT16
  Unit   : 100 µs increments
  Default: 0 (no inhibit, disabled)
  Range  : 0x0000 … 0xFFFF  (0 … 6553.5 ms)
```

### 7.1 How Inhibit Time Works

```
  Time ─────────────────────────────────────────────────────►
        │        │           │         │
        E1       E2          E3        E4   ← error events
        │        │           │         │
        │<──────>│           │         │
        │Inhibit │           │         │
        │Time T  │           │         │
        │        │           │         │
  TX:   EMCY1   (blocked)   EMCY3    EMCY4
                 △
                 │ E2 occurs inside inhibit window → NOT transmitted
                   (the error is still logged in 0x1003)
```

Setting an inhibit time of, e.g., 500 (= 50 ms) means no more than one EMCY per
50 ms can be transmitted from that node.

---

## 8. EMCY Consumer Object — Object 0x1028

Nodes that **consume** (listen to) EMCY messages use **Object 0x1028** to configure
which COB-IDs they monitor. This is optional — if absent, the node listens to all EMCY
messages on the bus.

```
  Object 0x1028 — Emergency Consumer Object
  ──────────────────────────────────────────
  Sub-Index 0 : UINT8  — number of EMCY producers monitored (1..127)
  Sub-Index 1 : UINT32 — COB-ID of first monitored EMCY producer
  Sub-Index 2 : UINT32 — COB-ID of second monitored EMCY producer
  ...
  Sub-Index N : UINT32 — COB-ID of Nth monitored EMCY producer
```

Each 32-bit entry has the same layout as Object 0x1014 (bit 31 = valid/invalid flag,
lower 11 bits = COB-ID).

---

## 9. EMCY Consumer Filtering

A consumer node can filter incoming EMCY messages in several ways:

```
  Incoming CAN frame
        │
        ▼
  ┌─────────────────────────┐
  │  COB-ID match?          │   ← compare against 0x1028 entries
  │  Is valid bit = 0 ?     │
  └────────┬────────────────┘
           │ Yes
           ▼
  ┌─────────────────────────┐
  │  Error Code != 0x0000 ? │   ← active error vs. reset message
  └────────┬────────────────┘
           │ Yes (active error)
           ▼
  ┌─────────────────────────┐
  │  Dispatch to            │
  │  application callback   │
  │  (error handling logic) │
  └─────────────────────────┘
```

Filtering strategies used in practice:

| Filter Type         | Description                                                       |
|---------------------|-------------------------------------------------------------------|
| COB-ID whitelist    | Only process EMCY from nodes listed in Object 0x1028              |
| Error code mask     | Ignore codes below a severity threshold                           |
| Error class filter  | E.g. only react to communication errors (0x8xxx)                  |
| Duplicate suppress  | Do not act on repeated identical EMCY within a time window        |
| Reset detection     | Detect 0x0000 code to clear alarm state                           |

---

## 10. State Machine and EMCY Behaviour

EMCY messages may only be produced when the device is in **Pre-Operational** or
**Operational** NMT state. A device in **Stopped** state must not transmit EMCY.

```
  NMT State Machine (relevant states)
  ─────────────────────────────────────────────────────────
                        Power-on / Reset
                              │
                              ▼
                    ┌──────────────────┐
                    │  Initialisation  │
                    └────────┬─────────┘
                             │ Boot-up complete
                             ▼
                    ┌──────────────────┐
              ┌────►│ Pre-Operational  │◄────┐
              │     │  EMCY: ALLOWED   │     │
              │     └────────┬─────────┘     │
              │              │ NMT Start     │
              │              ▼               │ NMT Pre-Op
              │     ┌──────────────────┐     │
              │     │   Operational    │─────┘
              │     │  EMCY: ALLOWED   │
              │     └────────┬─────────┘
              │              │ NMT Stop
              │              ▼
              │     ┌──────────────────┐
              └─────│    Stopped       │
                    │  EMCY: FORBIDDEN │
                    └──────────────────┘
```

**Rule:** When a device transitions from Stopped to Pre-Operational (e.g. after an NMT
Reset), if an error condition persists, it **must** re-send the EMCY message on entering
Pre-Operational so consumers are notified.

---

## 11. C/C++ Programming Examples

### 11.1 Data Structures

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* ---------------------------------------------------------------
 * EMCY frame as received/transmitted on CAN bus (8 bytes)
 * --------------------------------------------------------------- */
typedef struct {
    uint16_t error_code;        /* Emergency Error Code (EEC), little-endian */
    uint8_t  error_register;    /* Snapshot of Object 0x1001                 */
    uint8_t  vendor_data[5];    /* Manufacturer-specific diagnostic bytes    */
} __attribute__((packed)) EMCY_Frame_t;

/* ---------------------------------------------------------------
 * Object 0x1001 — Error Register (bitmask)
 * --------------------------------------------------------------- */
#define ERROR_REG_GENERIC       (1u << 0)
#define ERROR_REG_CURRENT       (1u << 1)
#define ERROR_REG_VOLTAGE       (1u << 2)
#define ERROR_REG_TEMPERATURE   (1u << 3)
#define ERROR_REG_COMMUNICATION (1u << 4)
#define ERROR_REG_DEVICE_PROF   (1u << 5)
#define ERROR_REG_MANUFACTURER  (1u << 7)

/* ---------------------------------------------------------------
 * Standard Emergency Error Codes
 * --------------------------------------------------------------- */
#define EMCY_NO_ERROR               0x0000u
#define EMCY_GENERIC_ERROR          0x1000u
#define EMCY_OVERCURRENT_OUTPUT     0x2310u
#define EMCY_OVERVOLTAGE_MAINS      0x3210u
#define EMCY_UNDERVOLTAGE_MAINS     0x3220u
#define EMCY_OVERVOLTAGE_OUTPUT     0x3310u
#define EMCY_TEMP_DRIVE_HIGH        0x4210u
#define EMCY_TEMP_DEVICE_HIGH       0x4310u
#define EMCY_CAN_RX_OVERRUN         0x8110u
#define EMCY_CAN_TX_OVERRUN         0x8120u
#define EMCY_HEARTBEAT_TIMEOUT      0x8130u
#define EMCY_BUS_OFF                0x8140u

/* ---------------------------------------------------------------
 * Pre-Defined Error Field — Object 0x1003 (ring buffer)
 * --------------------------------------------------------------- */
#define EMCY_HISTORY_SIZE  8u

typedef struct {
    uint8_t  count;                         /* Sub-index 0: number of entries    */
    uint32_t entries[EMCY_HISTORY_SIZE];    /* Sub-index 1..N: EEC + mfr data   */
} ErrorHistory_t;

/* ---------------------------------------------------------------
 * EMCY Producer context
 * --------------------------------------------------------------- */
typedef struct {
    uint32_t      cob_id;           /* Object 0x1014: COB-ID (bit31=disabled)   */
    uint8_t       error_register;   /* Object 0x1001: active error bitmask      */
    ErrorHistory_t history;         /* Object 0x1003: error history             */
    uint16_t      inhibit_time_100us; /* Object 0x1015: inhibit time (x100µs)  */
    uint32_t      last_emcy_tick;   /* Tick counter for inhibit time tracking   */
    bool          active_error;     /* True if at least one error bit is set    */
} EMCY_Producer_t;
```

---

### 11.2 EMCY Producer — Sending Emergencies

```c
/* ---------------------------------------------------------------
 * Platform-specific CAN send (provide your own implementation)
 * --------------------------------------------------------------- */
extern bool CAN_Send(uint32_t cob_id, const uint8_t *data, uint8_t dlc);
extern uint32_t SYS_GetTickMs(void);   /* millisecond tick counter */

/* ---------------------------------------------------------------
 * Initialise an EMCY producer for a given node
 * --------------------------------------------------------------- */
void EMCY_ProducerInit(EMCY_Producer_t *ctx, uint8_t node_id,
                       uint16_t inhibit_time_100us)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->cob_id          = 0x080u | (uint32_t)node_id;  /* bit31=0 → enabled */
    ctx->inhibit_time_100us = inhibit_time_100us;
}

/* ---------------------------------------------------------------
 * Internal: push error code into history ring (Object 0x1003)
 * --------------------------------------------------------------- */
static void emcy_push_history(ErrorHistory_t *hist,
                               uint16_t eec,
                               uint16_t mfr_data)
{
    /* Shift all entries down by one position (newest at index 1) */
    uint8_t n = (hist->count < EMCY_HISTORY_SIZE)
                    ? hist->count
                    : (EMCY_HISTORY_SIZE - 1u);
    for (uint8_t i = n; i > 0u; i--) {
        hist->entries[i] = hist->entries[i - 1u];
    }
    /* Compose 32-bit entry: upper 16 bits = mfr, lower 16 bits = EEC */
    hist->entries[0] = ((uint32_t)mfr_data << 16u) | eec;

    if (hist->count < EMCY_HISTORY_SIZE) {
        hist->count++;
    }
}

/* ---------------------------------------------------------------
 * Check inhibit time — returns true if we are allowed to transmit
 * --------------------------------------------------------------- */
static bool emcy_inhibit_ok(EMCY_Producer_t *ctx)
{
    if (ctx->inhibit_time_100us == 0u) {
        return true;    /* No inhibit configured */
    }
    uint32_t elapsed_ms   = SYS_GetTickMs() - ctx->last_emcy_tick;
    uint32_t inhibit_ms   = (uint32_t)ctx->inhibit_time_100us / 10u; /* 100µs → ms */
    return (elapsed_ms >= inhibit_ms);
}

/* ---------------------------------------------------------------
 * Send an EMCY message (error occurred)
 *
 *  eec           : 16-bit Emergency Error Code
 *  err_reg_bits  : bits to SET in Error Register (OR'd in)
 *  vendor_data   : pointer to 5 vendor bytes (NULL → zeros)
 * --------------------------------------------------------------- */
bool EMCY_SendError(EMCY_Producer_t *ctx,
                    uint16_t         eec,
                    uint8_t          err_reg_bits,
                    const uint8_t   *vendor_data)
{
    /* Bit 31 of COB-ID set → EMCY producer disabled */
    if (ctx->cob_id & 0x80000000u) {
        return false;
    }
    /* Respect inhibit time */
    if (!emcy_inhibit_ok(ctx)) {
        /* Still update history and error register, just don't transmit */
        ctx->error_register |= err_reg_bits | ERROR_REG_GENERIC;
        emcy_push_history(&ctx->history, eec, 0x0000u);
        return false;
    }

    /* Update Error Register */
    ctx->error_register |= err_reg_bits | ERROR_REG_GENERIC;
    ctx->active_error    = true;

    /* Update history ring */
    emcy_push_history(&ctx->history, eec, 0x0000u);

    /* Build 8-byte EMCY frame */
    uint8_t frame[8] = {0};
    frame[0] = (uint8_t)(eec & 0x00FFu);           /* EEC low byte  */
    frame[1] = (uint8_t)((eec >> 8u) & 0x00FFu);   /* EEC high byte */
    frame[2] = ctx->error_register;                 /* Error Register */
    if (vendor_data != NULL) {
        memcpy(&frame[3], vendor_data, 5u);
    }

    /* Transmit on CAN bus */
    bool ok = CAN_Send(ctx->cob_id & 0x7FFu, frame, 8u);
    if (ok) {
        ctx->last_emcy_tick = SYS_GetTickMs();
    }
    return ok;
}

/* ---------------------------------------------------------------
 * Send EMCY reset (error cleared)
 *
 *  err_reg_bits_to_clear : bits to CLEAR in Error Register
 * --------------------------------------------------------------- */
bool EMCY_SendReset(EMCY_Producer_t *ctx, uint8_t err_reg_bits_to_clear)
{
    if (ctx->cob_id & 0x80000000u) {
        return false;
    }

    /* Clear the specified bits from Error Register */
    ctx->error_register &= ~err_reg_bits_to_clear;

    /* If all error bits gone, clear the generic bit too */
    if ((ctx->error_register & ~ERROR_REG_GENERIC) == 0u) {
        ctx->error_register = 0x00u;
        ctx->active_error   = false;
    }

    /* Build reset frame: EEC = 0x0000 */
    uint8_t frame[8] = {0};
    frame[0] = 0x00u;
    frame[1] = 0x00u;
    frame[2] = ctx->error_register;    /* Updated (possibly cleared) register */

    return CAN_Send(ctx->cob_id & 0x7FFu, frame, 8u);
}
```

---

### 11.3 Practical Producer Usage

```c
/* Example: Motor drive detects DC-link undervoltage then recovers */

static EMCY_Producer_t g_emcy;

void App_Init(void)
{
    /* Node-ID 5, inhibit time = 500 × 100µs = 50 ms */
    EMCY_ProducerInit(&g_emcy, 5u, 500u);
}

void App_MonitorDCLink(float dc_voltage_V)
{
    static bool undervoltage_active = false;

    if (dc_voltage_V < 18.0f && !undervoltage_active) {
        /* --- Error condition detected --- */
        undervoltage_active = true;

        /* Vendor data: encode measured voltage × 100 in bytes 0-1 */
        uint8_t vdata[5] = {0};
        uint16_t v100 = (uint16_t)(dc_voltage_V * 100.0f);
        vdata[0] = (uint8_t)(v100 & 0xFFu);
        vdata[1] = (uint8_t)((v100 >> 8u) & 0xFFu);

        EMCY_SendError(&g_emcy,
                       EMCY_UNDERVOLTAGE_MAINS,   /* 0x3220 */
                       ERROR_REG_VOLTAGE,          /* bit 2  */
                       vdata);

    } else if (dc_voltage_V >= 20.0f && undervoltage_active) {
        /* --- Error condition cleared (hysteresis) --- */
        undervoltage_active = false;

        EMCY_SendReset(&g_emcy, ERROR_REG_VOLTAGE);
    }
}
```

---

### 11.4 EMCY Consumer — Receiving and Filtering

```c
/* ---------------------------------------------------------------
 * EMCY Consumer context
 * --------------------------------------------------------------- */
#define MAX_MONITORED_NODES  16u

typedef struct {
    uint32_t cob_id_filter[MAX_MONITORED_NODES]; /* Object 0x1028 entries */
    uint8_t  filter_count;
} EMCY_Consumer_t;

/* Application-provided callback */
typedef void (*EMCY_Callback_t)(uint8_t  node_id,
                                 uint16_t error_code,
                                 uint8_t  error_register,
                                 const uint8_t *vendor_data);

/* ---------------------------------------------------------------
 * Initialise consumer, add a node to the whitelist
 * --------------------------------------------------------------- */
void EMCY_ConsumerInit(EMCY_Consumer_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

bool EMCY_ConsumerAddNode(EMCY_Consumer_t *ctx, uint8_t node_id)
{
    if (ctx->filter_count >= MAX_MONITORED_NODES) return false;
    /* Bit 31 = 0 (valid / enabled) */
    ctx->cob_id_filter[ctx->filter_count++] = 0x080u | (uint32_t)node_id;
    return true;
}

/* ---------------------------------------------------------------
 * Process an incoming CAN frame — call from CAN RX interrupt / task
 * --------------------------------------------------------------- */
void EMCY_ConsumerProcess(EMCY_Consumer_t    *ctx,
                           uint32_t            can_id,
                           const uint8_t      *data,
                           uint8_t             dlc,
                           EMCY_Callback_t     callback)
{
    if (dlc < 8u) return;   /* EMCY must always be 8 bytes */

    /* Check COB-ID is in the EMCY range */
    if (can_id < 0x081u || can_id > 0x0FFu) return;

    /* COB-ID whitelist check (if consumer has entries configured) */
    if (ctx->filter_count > 0u) {
        bool found = false;
        for (uint8_t i = 0u; i < ctx->filter_count; i++) {
            uint32_t entry   = ctx->cob_id_filter[i];
            bool     enabled = !(entry & 0x80000000u);
            uint32_t flt_id  = entry & 0x7FFu;
            if (enabled && (flt_id == can_id)) {
                found = true;
                break;
            }
        }
        if (!found) return;     /* Filtered out */
    }

    /* Decode the EMCY frame */
    uint16_t eec  = (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
    uint8_t  ereg = data[2];
    uint8_t  node_id = (uint8_t)(can_id & 0x7Fu);

    /* Invoke application callback */
    if (callback != NULL) {
        callback(node_id, eec, ereg, &data[3]);
    }
}

/* ---------------------------------------------------------------
 * Example application callback
 * --------------------------------------------------------------- */
void App_EMCY_Callback(uint8_t  node_id,
                       uint16_t error_code,
                       uint8_t  error_register,
                       const uint8_t *vendor_data)
{
    if (error_code == EMCY_NO_ERROR) {
        /* Error reset from node */
        printf("[EMCY] Node %02X: Error cleared (ER=0x%02X)\n",
               node_id, error_register);
        return;
    }

    /* Classify by error code upper byte */
    uint8_t error_class = (uint8_t)((error_code >> 8u) & 0xFFu);

    switch (error_class) {
        case 0x20: printf("[EMCY] Node %02X: CURRENT error  0x%04X\n", node_id, error_code); break;
        case 0x30: printf("[EMCY] Node %02X: VOLTAGE error  0x%04X\n", node_id, error_code); break;
        case 0x40: printf("[EMCY] Node %02X: THERMAL error  0x%04X\n", node_id, error_code); break;
        case 0x81: printf("[EMCY] Node %02X: CAN BUS error  0x%04X\n", node_id, error_code); break;
        default:   printf("[EMCY] Node %02X: Generic error  0x%04X\n", node_id, error_code); break;
    }

    (void)vendor_data;  /* Log / store as needed */
}
```

---

### 11.5 SDO Access to Object 0x1003 (Error History)

Reading the error history via SDO upload (simplified):

```c
/* ---------------------------------------------------------------
 * Dump EMCY error history by SDO-reading Object 0x1003
 * (assumes SDO_Upload returns a uint32_t value from OD)
 * --------------------------------------------------------------- */
extern bool SDO_Upload_U8 (uint8_t node, uint16_t idx, uint8_t sub, uint8_t  *out);
extern bool SDO_Upload_U32(uint8_t node, uint16_t idx, uint8_t sub, uint32_t *out);

void EMCY_DumpHistory(uint8_t node_id)
{
    uint8_t count = 0u;
    if (!SDO_Upload_U8(node_id, 0x1003u, 0x00u, &count)) {
        printf("SDO read 0x1003:00 failed\n");
        return;
    }

    printf("Error history for node %02X (%u entries):\n", node_id, count);

    for (uint8_t i = 1u; i <= count; i++) {
        uint32_t entry = 0u;
        if (SDO_Upload_U32(node_id, 0x1003u, i, &entry)) {
            uint16_t eec      = (uint16_t)(entry & 0xFFFFu);
            uint16_t mfr_data = (uint16_t)((entry >> 16u) & 0xFFFFu);
            printf("  [%02u] EEC=0x%04X  MfgData=0x%04X\n", i, eec, mfr_data);
        }
    }

    /* Optionally clear history by writing 0 to sub-index 0 */
    /* SDO_Download_U8(node_id, 0x1003u, 0x00u, 0u); */
}
```

---

### 11.6 Object Dictionary Entries in C (CANopen Stack Integration)

```c
/* ---------------------------------------------------------------
 * Object Dictionary constant entries (ROM) for a CANopen device
 * These are typically declared in a generated OD table
 * --------------------------------------------------------------- */

/* Object 0x1001 — Error Register (RO, 1 byte) */
static uint8_t OD_ErrorRegister = 0x00u;

/* Object 0x1003 — Pre-Defined Error Field */
static uint32_t OD_ErrorHistory[EMCY_HISTORY_SIZE + 1u];
/*  [0] = count (cast to uint32 for uniform array; use lower byte only) */

/* Object 0x1014 — EMCY COB-ID */
static uint32_t OD_EMCY_COB_ID = 0x00000081u;   /* Node 1, enabled */

/* Object 0x1015 — EMCY Inhibit Time (unit: 100 µs) */
static uint16_t OD_EMCY_InhibitTime = 500u;     /* 50 ms inhibit */

/* Object 0x1028 — Emergency Consumer Object */
static uint32_t OD_EMCYConsumer[4] = {
    0x00000082u,   /* Monitor node 2 */
    0x00000083u,   /* Monitor node 3 */
    0x80000084u,   /* Node 4 disabled (bit31 set) */
    0x00000085u,   /* Monitor node 5 */
};

/* ---------------------------------------------------------------
 * Wire the objects together during device init
 * --------------------------------------------------------------- */
void Device_Init(uint8_t my_node_id)
{
    OD_EMCY_COB_ID  = 0x080u | (uint32_t)my_node_id;
    OD_ErrorRegister = 0x00u;
    memset(OD_ErrorHistory, 0, sizeof(OD_ErrorHistory));

    EMCY_ProducerInit(&g_emcy, my_node_id, OD_EMCY_InhibitTime);
}
```

---

### 11.7 C++ Class Wrapper

```cpp
#include <cstdint>
#include <cstring>
#include <functional>

// -----------------------------------------------------------------
// C++ RAII wrapper around the C EMCY producer
// -----------------------------------------------------------------
class EmcyProducer {
public:
    using VendorData = std::array<uint8_t, 5>;

    explicit EmcyProducer(uint8_t nodeId, uint16_t inhibit100us = 0u)
    {
        EMCY_ProducerInit(&ctx_, nodeId, inhibit100us);
    }

    bool sendError(uint16_t         errorCode,
                   uint8_t          errorRegBits,
                   const VendorData &vdata = VendorData{})
    {
        return EMCY_SendError(&ctx_, errorCode, errorRegBits, vdata.data());
    }

    bool sendReset(uint8_t errorRegBitsToClear)
    {
        return EMCY_SendReset(&ctx_, errorRegBitsToClear);
    }

    uint8_t errorRegister() const { return ctx_.error_register; }
    bool    hasActiveError() const { return ctx_.active_error;   }

    void clearHistory()
    {
        memset(&ctx_.history, 0, sizeof(ctx_.history));
    }

private:
    EMCY_Producer_t ctx_{};
};

// -----------------------------------------------------------------
// C++ consumer with std::function callback
// -----------------------------------------------------------------
class EmcyConsumer {
public:
    using Callback = std::function<void(uint8_t nodeId,
                                        uint16_t errorCode,
                                        uint8_t  errorReg,
                                        const uint8_t *vendorData)>;

    EmcyConsumer() { EMCY_ConsumerInit(&ctx_); }

    void addNode(uint8_t nodeId) { EMCY_ConsumerAddNode(&ctx_, nodeId); }

    void setCallback(Callback cb) { callback_ = std::move(cb); }

    void processFrame(uint32_t canId, const uint8_t *data, uint8_t dlc)
    {
        EMCY_ConsumerProcess(&ctx_, canId, data, dlc,
            [](uint8_t nid, uint16_t ec, uint8_t er, const uint8_t *vd)
            {
                /* Static trampoline — capture is not possible in C callback;
                   store instance pointer elsewhere for real use-cases */
                (void)nid; (void)ec; (void)er; (void)vd;
            });
        /* For the C++ version, decode inline: */
        if (dlc >= 8u && canId >= 0x081u && canId <= 0x0FFu) {
            if (callback_) {
                uint16_t eec    = (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
                uint8_t  ereg   = data[2];
                uint8_t  nodeId = static_cast<uint8_t>(canId & 0x7Fu);
                callback_(nodeId, eec, ereg, &data[3]);
            }
        }
    }

private:
    EMCY_Consumer_t ctx_{};
    Callback        callback_;
};

// -----------------------------------------------------------------
// Usage example in main application
// -----------------------------------------------------------------
int main()
{
    EmcyProducer drive(5u, 500u);   /* Node 5, 50ms inhibit */
    EmcyConsumer master;

    master.addNode(5u);
    master.setCallback([](uint8_t nid, uint16_t ec, uint8_t er,
                          const uint8_t *) {
        if (ec == 0x0000u) {
            printf("Node %d: error cleared\n", nid);
        } else {
            printf("Node %d: ERROR 0x%04X  ER=0x%02X\n", nid, ec, er);
        }
    });

    /* Simulate overvoltage on drive */
    EmcyProducer::VendorData vd{};
    vd[0] = 0xA8u;  /* 0x00A8 = 168 → scaled voltage */
    drive.sendError(EMCY_OVERVOLTAGE_OUTPUT, ERROR_REG_VOLTAGE, vd);

    /* Simulate reception on master side */
    uint8_t raw_frame[8] = {0x10, 0x33, 0x04, 0xA8, 0x00, 0x00, 0x00, 0x00};
    master.processFrame(0x085u, raw_frame, 8u);

    return 0;
}
```

---

## 12. Summary

```
  ┌─────────────────────────────────────────────────────────────────────┐
  │                    EMCY — Quick Reference                           │
  ├─────────────────────────┬───────────────────────────────────────────┤
  │  COB-ID                 │  0x080 + Node-ID  (Object 0x1014)         │
  │  Frame size             │  Always 8 bytes (DLC = 8)                 │
  │  Byte 0-1               │  Emergency Error Code (16-bit LE)         │
  │  Byte 2                 │  Error Register (snapshot of 0x1001)      │
  │  Byte 3-7               │  Vendor-specific data (5 bytes)           │
  │  Error Reset            │  EEC = 0x0000 signals error cleared       │
  ├─────────────────────────┼───────────────────────────────────────────┤
  │  Error Register 0x1001  │  8-bit bitmask, bit0=generic always set   │
  │  Error History  0x1003  │  FIFO of last N×32-bit error codes        │
  │  Inhibit Time   0x1015  │  Min gap between EMCY TX (unit: 100µs)    │
  │  Consumer OD    0x1028  │  Whitelist of COB-IDs to monitor          │
  ├─────────────────────────┼───────────────────────────────────────────┤
  │  Error Classes          │  0x1xxx Generic  0x2xxx Current           │
  │  (upper byte of EEC)    │  0x3xxx Voltage  0x4xxx Temperature       │
  │                         │  0x5xxx Hardware 0x6xxx Software          │
  │                         │  0x7xxx Modules  0x8xxx Communication     │
  │                         │  0x9xxx External 0xFFxx Vendor-specific   │
  ├─────────────────────────┼───────────────────────────────────────────┤
  │  NMT state              │  Allowed in Pre-Operational & Operational │
  │                         │  Forbidden in Stopped state               │
  ├─────────────────────────┼───────────────────────────────────────────┤
  │  Producer rule          │  One EMCY producer per node               │
  │  Consumer rule          │  Any number of consumers per network      │
  │  Inhibit rule           │  No more than 1 EMCY per inhibit window   │
  │  History clear          │  SDO write 0 to 0x1003:00                 │
  └─────────────────────────┴───────────────────────────────────────────┘
```

The EMCY object gives CANopen systems a **fast, standardised, node-identified** mechanism
for broadcasting faults. Its key design points are:

- **Producer–consumer model** with a single producer per node and unlimited consumers.
- **Fixed 8-byte frame** carrying a 16-bit Error Code, an 8-bit Error Register copy, and
  5 bytes of vendor diagnostics.
- **Structured error codes** grouped by class (voltage, current, temperature, communication,
  etc.) enabling generic handling alongside device-specific detail.
- **Object 0x1003** provides persistent, SDO-readable error history for diagnostics and
  commissioning tools.
- **Inhibit time** (Object 0x1015) protects bus bandwidth during error storms.
- **Consumer filtering** via Object 0x1028 allows nodes to subscribe only to relevant
  producers, reducing unnecessary processing.
- **Interaction with NMT**: EMCY is forbidden in the Stopped state and must be
  re-transmitted after a state transition if the error persists.

Together these features make EMCY the primary real-time alarm channel in a CANopen network,
suitable for everything from safety-critical shutdown signalling to routine maintenance
diagnostics.

---

*Reference: CiA 301 — CANopen Application Layer and Communication Profile, Version 4.2.0*