# 29. CANopen Error Handling Strategy

---

## Table of Contents

1. [Introduction](#introduction)
2. [Error Register – Object 0x1001](#error-register-object-0x1001)
3. [Pre-defined Error Field – Object 0x1003](#pre-defined-error-field-object-0x1003)
4. [Emergency Object (EMCY) – Generation Policy](#emergency-object-emcy--generation-policy)
5. [Communication Error Counters](#communication-error-counters)
6. [Designing a Recoverable Fault Architecture](#designing-a-recoverable-fault-architecture)
7. [Complete Integration Example](#complete-integration-example)
8. [Summary](#summary)

---

## Introduction

CANopen defines a layered, structured approach to error handling that goes well beyond
simple status flags. Every CANopen device is required to maintain an **Error Register**
(Object Dictionary entry 0x1001), may store a history of errors in the **Pre-defined
Error Field** (0x1003), and must be able to generate **Emergency (EMCY) messages** to
broadcast fault conditions onto the network.

This document covers each mechanism in depth, shows how they interlock, and provides
idiomatic C/C++ implementation patterns.

```
   ┌─────────────────────────────────────────────────────────────┐
   │                   CANopen Error Architecture                 │
   │                                                             │
   │  Application Layer                                          │
   │  ┌───────────────────────────────────────────────────────┐  │
   │  │  Error Source Detection (HW / SW / Comm)              │  │
   │  └──────────────────────┬────────────────────────────────┘  │
   │                         │                                   │
   │                         ▼                                   │
   │  ┌──────────────────────────────────────────────────────┐   │
   │  │  Error Register 0x1001  (bit flags, 1 byte)          │   │
   │  └──────────┬───────────────────────────────────────────┘   │
   │             │                                               │
   │             ├──────────────────────────────────────────┐    │
   │             │                                          │    │
   │             ▼                                          ▼    │
   │  ┌─────────────────────┐          ┌───────────────────────┐ │
   │  │ Pre-def Error Field │          │  EMCY Object          │ │
   │  │ 0x1003  (FIFO, 8+)  │          │  (COB-ID 0x80+NodeID) │ │
   │  └─────────────────────┘          └───────────────────────┘ │
   │                                                             │
   │  NMT State Machine reacts to persistent errors              │
   └─────────────────────────────────────────────────────────────┘
```

---

## Error Register – Object 0x1001

### Overview

The **Error Register** (0x1001) is a mandatory, single-byte object in every CANopen
device's Object Dictionary. It is a **bitmask** that reflects the current error state of
the device. Because it is part of the Object Dictionary it can be read at any time via
SDO, and it is included in every EMCY message payload.

### Bit Definitions (CiA 301)

```
  Bit 7   Bit 6   Bit 5   Bit 4   Bit 3   Bit 2   Bit 1   Bit 0
  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
  │Mfr  │     │Dev  │Com  │Temp │Volt │Curr │Gen  │
  │Spec │Rsv  │Prof │Err  │Err  │Err  │Err  │Err  │
  └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
     7     6     5     4     3     2     1     0

  Bit 0: Generic error           (mandatory – set when any error is active)
  Bit 1: Current error           (overcurrent, short circuit)
  Bit 2: Voltage error           (over/under voltage, mains)
  Bit 3: Temperature error       (overtemperature, thermal warning)
  Bit 4: Communication error     (CAN overrun, guarding, SYNC loss)
  Bit 5: Device profile specific (motor stall, encoder fault, …)
  Bit 6: Reserved                (shall be 0)
  Bit 7: Manufacturer specific   (vendor-defined error class)
```

### Rules

- **Bit 0 (Generic Error) must be set** whenever any other bit is set, and must be
  cleared only when all errors have been resolved.
- Bits are **OR-accumulative**: if two independent errors of the same class are active,
  the bit stays set until both are resolved.
- The register is **live** – it reflects the real-time state of active errors, not a
  history.

### C Implementation

```c
/* error_register.h */
#ifndef ERROR_REGISTER_H
#define ERROR_REGISTER_H

#include <stdint.h>
#include <stdbool.h>

/* Bit masks for Object 0x1001 */
#define ERR_REG_GENERIC       (1u << 0)   /* Generic error        */
#define ERR_REG_CURRENT       (1u << 1)   /* Current error        */
#define ERR_REG_VOLTAGE       (1u << 2)   /* Voltage error        */
#define ERR_REG_TEMPERATURE   (1u << 3)   /* Temperature error    */
#define ERR_REG_COMMUNICATION (1u << 4)   /* Communication error  */
#define ERR_REG_DEV_PROFILE   (1u << 5)   /* Device profile spec. */
#define ERR_REG_MANUFACTURER  (1u << 7)   /* Manufacturer spec.   */

/* Internal reference counts per error class (allows multiple callers) */
typedef struct {
    uint8_t  reg_value;                   /* live 0x1001 value    */
    uint8_t  ref_count[8];               /* refcount per bit      */
} ErrorRegister;

void     error_register_init  (ErrorRegister *er);
void     error_register_set   (ErrorRegister *er, uint8_t mask);
void     error_register_clear (ErrorRegister *er, uint8_t mask);
uint8_t  error_register_get   (const ErrorRegister *er);
bool     error_register_any   (const ErrorRegister *er);

#endif /* ERROR_REGISTER_H */
```

```c
/* error_register.c */
#include "error_register.h"
#include <string.h>

void error_register_init(ErrorRegister *er)
{
    memset(er, 0, sizeof(*er));
}

void error_register_set(ErrorRegister *er, uint8_t mask)
{
    for (int bit = 0; bit < 8; bit++) {
        if (mask & (1u << bit)) {
            if (er->ref_count[bit] < 255u)
                er->ref_count[bit]++;
            er->reg_value |= (1u << bit);
        }
    }
    /* Enforce: Generic Error (bit 0) set whenever any other bit is set */
    if (er->reg_value & ~ERR_REG_GENERIC) {
        er->reg_value   |= ERR_REG_GENERIC;
        er->ref_count[0] = 1;  /* synthetic count */
    }
}

void error_register_clear(ErrorRegister *er, uint8_t mask)
{
    for (int bit = 0; bit < 8; bit++) {
        if (mask & (1u << bit)) {
            if (er->ref_count[bit] > 0)
                er->ref_count[bit]--;
            if (er->ref_count[bit] == 0)
                er->reg_value &= ~(1u << bit);
        }
    }
    /* Auto-clear Generic Error when no other bits remain */
    if ((er->reg_value & ~ERR_REG_GENERIC) == 0) {
        er->reg_value    = 0;
        er->ref_count[0] = 0;
    }
}

uint8_t error_register_get(const ErrorRegister *er)
{
    return er->reg_value;
}

bool error_register_any(const ErrorRegister *er)
{
    return er->reg_value != 0;
}
```

### C++ Wrapper

```cpp
// ErrorRegister.hpp
#pragma once
#include <cstdint>
#include <atomic>

class ErrorRegister {
public:
    static constexpr uint8_t GENERIC       = (1u << 0);
    static constexpr uint8_t CURRENT       = (1u << 1);
    static constexpr uint8_t VOLTAGE       = (1u << 2);
    static constexpr uint8_t TEMPERATURE   = (1u << 3);
    static constexpr uint8_t COMMUNICATION = (1u << 4);
    static constexpr uint8_t DEV_PROFILE   = (1u << 5);
    static constexpr uint8_t MANUFACTURER  = (1u << 7);

    void    set  (uint8_t mask) noexcept;
    void    clear(uint8_t mask) noexcept;
    uint8_t get  () const noexcept { return value_.load(std::memory_order_acquire); }
    bool    any  () const noexcept { return get() != 0; }

private:
    std::atomic<uint8_t> value_{0};
    uint8_t              ref_[8]{};

    void update() noexcept;
};
```

```cpp
// ErrorRegister.cpp
#include "ErrorRegister.hpp"

void ErrorRegister::set(uint8_t mask) noexcept {
    for (int b = 0; b < 8; ++b)
        if (mask & (1u << b))
            if (ref_[b] < 255u) ++ref_[b];
    update();
}

void ErrorRegister::clear(uint8_t mask) noexcept {
    for (int b = 0; b < 8; ++b)
        if ((mask & (1u << b)) && ref_[b] > 0)
            --ref_[b];
    update();
}

void ErrorRegister::update() noexcept {
    uint8_t v = 0;
    for (int b = 1; b < 8; ++b)   /* skip bit 0 – derived */
        if (ref_[b]) v |= (1u << b);
    if (v) v |= GENERIC;           /* auto-set Generic Error */
    ref_[0] = (v ? 1u : 0u);
    value_.store(v, std::memory_order_release);
}
```

---

## Pre-defined Error Field – Object 0x1003

### Overview

Object **0x1003** is an optional but widely supported **error history array** (FIFO
stack). When implemented, it stores the last N errors that occurred, providing
post-mortem diagnostics accessible via SDO from a master or engineering tool.

### Structure

```
  Object 0x1003 – Pre-defined Error Field
  ┌───────────────────────────────────────────────────────────┐
  │ Sub-index 0x00  │  UINT8  │  Number of errors stored (0–N)│
  │ Sub-index 0x01  │  UINT32 │  Most recent error            │
  │ Sub-index 0x02  │  UINT32 │  Second most recent error     │
  │  ...            │  ...    │  ...                          │
  │ Sub-index 0xFE  │  UINT32 │  Oldest error (up to 254)     │
  └───────────────────────────────────────────────────────────┘

  Each 32-bit entry format:
  ┌──────────────────────────┬───────────────────────────────┐
  │  Bits 31..16             │  Bits 15..0                   │
  │  Manufacturer Specific   │  Standard Error Code (EMCY)   │
  └──────────────────────────┴───────────────────────────────┘
```

### FIFO Behavior

- **New error → pushed to sub-index 0x01**: existing entries shift down (oldest at
  highest sub-index).
- **Write 0x00 to sub-index 0x00** clears the entire history.
- Sub-index 0x00 gives the **current fill level**; reading sub-index > fill level
  returns 0x00000000.

```
  Push new error 0xABCD1234 (max depth = 4):

  Before:                    After:
  [0x00] = 2                 [0x00] = 3
  [0x01] = 0xBEEF0001        [0x01] = 0xABCD1234  ← new
  [0x02] = 0xDEAD0002        [0x02] = 0xBEEF0001
  [0x03] = 0x00000000        [0x03] = 0xDEAD0002
  [0x04] = 0x00000000        [0x04] = 0x00000000  (oldest, not yet full)
```

### Standard Error Codes (EMCY, CiA 301 Table 6)

```
  0x0000  Error reset / No error
  0x1000  Generic error
  0x2000  Current – generic
  0x2100  Current, device input side
  0x2200  Current, device internal
  0x2300  Current, device output side
  0x3000  Voltage – generic
  0x3100  Mains voltage
  0x3200  Voltage inside the device
  0x3300  Output voltage
  0x4000  Temperature – generic
  0x4100  Ambient temperature
  0x4200  Device temperature
  0x5000  Device hardware
  0x6000  Device software
  0x7000  Additional modules
  0x8000  Monitoring
  0x8100  Communication
  0x8110  CAN overrun
  0x8120  CAN in error passive mode
  0x8130  Life guard error / heartbeat error
  0x8140  CAN bus off recovered
  0x8200  Protocol error
  0x9000  External error
  0xF000  Additional functions
  0xFF00  Device specific
```

### C Implementation

```c
/* error_history.h */
#ifndef ERROR_HISTORY_H
#define ERROR_HISTORY_H

#include <stdint.h>
#include <stdbool.h>

#define ERR_HISTORY_MAX_DEPTH  8u   /* configurable, up to 254 */

typedef struct {
    uint32_t entries[ERR_HISTORY_MAX_DEPTH]; /* sub-index 1..N   */
    uint8_t  count;                          /* sub-index 0x00    */
} ErrorHistory;

void     error_history_init  (ErrorHistory *eh);
void     error_history_push  (ErrorHistory *eh, uint16_t emcy_code,
                               uint16_t mfr_specific);
void     error_history_clear (ErrorHistory *eh);
uint32_t error_history_read  (const ErrorHistory *eh, uint8_t sub_index);
uint8_t  error_history_count (const ErrorHistory *eh);

#endif /* ERROR_HISTORY_H */
```

```c
/* error_history.c */
#include "error_history.h"
#include <string.h>

void error_history_init(ErrorHistory *eh)
{
    memset(eh, 0, sizeof(*eh));
}

/* Push newest entry – older entries shift toward higher indices */
void error_history_push(ErrorHistory *eh,
                        uint16_t      emcy_code,
                        uint16_t      mfr_specific)
{
    uint8_t depth = ERR_HISTORY_MAX_DEPTH;

    /* Shift existing entries down by one */
    for (uint8_t i = (depth - 1u); i > 0u; i--)
        eh->entries[i] = eh->entries[i - 1u];

    /* Insert at front (sub-index 0x01 == array index 0) */
    eh->entries[0] = ((uint32_t)mfr_specific << 16u) |
                     (uint32_t)emcy_code;

    if (eh->count < depth)
        eh->count++;
}

/* Write 0x00 to sub-index 0x00 clears history (CiA 301 §7.5.2.7) */
void error_history_clear(ErrorHistory *eh)
{
    memset(eh->entries, 0, sizeof(eh->entries));
    eh->count = 0;
}

/* SDO read handler: sub_index 0x00 → count, 0x01..N → entry */
uint32_t error_history_read(const ErrorHistory *eh, uint8_t sub_index)
{
    if (sub_index == 0x00u)
        return eh->count;

    uint8_t idx = sub_index - 1u;
    if (idx >= ERR_HISTORY_MAX_DEPTH || idx >= eh->count)
        return 0x00000000u;

    return eh->entries[idx];
}

uint8_t error_history_count(const ErrorHistory *eh)
{
    return eh->count;
}
```

---

## Emergency Object (EMCY) – Generation Policy

### Overview

An **Emergency (EMCY) message** is a high-priority CAN frame broadcast by a device
whenever a new error condition is detected or an existing error is resolved. It is
event-driven and does not require polling.

### Frame Layout

```
  CAN Frame for EMCY:
  ┌─────────────────────────────────────────────────────────────┐
  │  COB-ID: 0x080 + Node-ID  (default; configurable via 0x1014)│
  │  DLC: 8 bytes                                               │
  ├──────────┬──────────┬──────────────────────────────────────┤
  │ Byte 0-1 │ Byte 2   │ Bytes 3..7                           │
  │ EMCY     │ Error    │ Manufacturer-specific                │
  │ Error    │ Register │ Error Information                    │
  │ Code     │ (0x1001) │ (5 bytes, device-defined)            │
  │ (UINT16) │ (UINT8)  │                                      │
  └──────────┴──────────┴──────────────────────────────────────┘

  Example – overvoltage on Node 5:
  COB-ID  = 0x085
  Bytes   = [00 32] [04] [00 00 00 00 00]
             ^^^^^   ^
             0x3200  Bit2=Voltage
             (Voltage inside device)
```

### Generation Policy (CiA 301 §7.2.7)

The standard mandates:

1. **On error occurrence**: send EMCY with the appropriate error code; update 0x1001
   and push to 0x1003.
2. **On error resolution**: send EMCY with code **0x0000** ("Error reset – no error").
   The 0x1001 value at the time of the reset message **must already reflect** the
   resolved state.
3. **Inhibit time** (0x1015): a minimum interval between consecutive EMCY messages to
   prevent CAN bus flooding during oscillating faults. Value in units of 100 µs.
4. **No duplicate storms**: if the same error fires multiple times while still active,
   do **not** re-transmit the EMCY (unless it was cleared in between).

```
  Error state machine per error source:

  ┌────────────┐   error detected    ┌────────────┐
  │            ├────────────────────►│            │
  │  INACTIVE  │                     │   ACTIVE   │
  │            │◄────────────────────┤            │
  └────────────┘   error cleared     └────────────┘
       │                                  │
       │ (no EMCY, no 0x1003 push)        │ send EMCY on ENTRY
       │                                  │ push 0x1003 on ENTRY
       │                                  │ send EMCY(0x0000) on EXIT
       ▼                                  ▼
```

### C Implementation

```c
/* emcy.h */
#ifndef EMCY_H
#define EMCY_H

#include <stdint.h>
#include <stdbool.h>
#include "error_register.h"
#include "error_history.h"

/* Manufacturer-specific error info: 5 bytes */
typedef struct {
    uint8_t data[5];
} EmcyMfrInfo;

/* Callback to transmit the 8-byte EMCY CAN frame */
typedef void (*EmcyTxFn)(uint32_t cob_id,
                          const uint8_t *payload,
                          void          *user_data);

typedef struct {
    uint8_t        node_id;
    uint16_t       inhibit_100us;       /* from Object 0x1015    */
    uint32_t       inhibit_timer_us;    /* runtime counter        */

    ErrorRegister *err_reg;
    ErrorHistory  *err_hist;

    EmcyTxFn       tx_fn;
    void          *tx_user;

    /* Track which error codes are currently active (bitmap of slots) */
    uint16_t       active_codes[16];    /* up to 16 simultaneous  */
    uint8_t        active_count;
} EmcyProducer;

void emcy_init    (EmcyProducer *ep,
                   uint8_t       node_id,
                   uint16_t      inhibit_100us,
                   ErrorRegister *er,
                   ErrorHistory  *eh,
                   EmcyTxFn      tx_fn,
                   void         *tx_user);

void emcy_error_occurred(EmcyProducer *ep,
                         uint16_t      emcy_code,
                         uint8_t       err_reg_mask,
                         const EmcyMfrInfo *mfr);

void emcy_error_resolved(EmcyProducer *ep,
                         uint16_t      emcy_code,
                         uint8_t       err_reg_mask);

void emcy_tick_us(EmcyProducer *ep, uint32_t elapsed_us);

#endif /* EMCY_H */
```

```c
/* emcy.c */
#include "emcy.h"
#include <string.h>

static bool is_code_active(EmcyProducer *ep, uint16_t code)
{
    for (uint8_t i = 0; i < ep->active_count; i++)
        if (ep->active_codes[i] == code)
            return true;
    return false;
}

static void add_active_code(EmcyProducer *ep, uint16_t code)
{
    if (ep->active_count < 16u)
        ep->active_codes[ep->active_count++] = code;
}

static void remove_active_code(EmcyProducer *ep, uint16_t code)
{
    for (uint8_t i = 0; i < ep->active_count; i++) {
        if (ep->active_codes[i] == code) {
            ep->active_codes[i] =
                ep->active_codes[--ep->active_count];
            return;
        }
    }
}

static void transmit_emcy(EmcyProducer      *ep,
                           uint16_t           code,
                           const EmcyMfrInfo *mfr)
{
    uint8_t payload[8];
    payload[0] = (uint8_t)(code & 0xFFu);
    payload[1] = (uint8_t)(code >> 8u);
    payload[2] = error_register_get(ep->err_reg);
    if (mfr)
        memcpy(&payload[3], mfr->data, 5);
    else
        memset(&payload[3], 0, 5);

    uint32_t cob_id = 0x080u + ep->node_id;
    ep->tx_fn(cob_id, payload, ep->tx_user);

    /* Reset inhibit timer */
    ep->inhibit_timer_us = (uint32_t)ep->inhibit_100us * 100u;
}

void emcy_init(EmcyProducer *ep,
               uint8_t       node_id,
               uint16_t      inhibit_100us,
               ErrorRegister *er,
               ErrorHistory  *eh,
               EmcyTxFn      tx_fn,
               void         *tx_user)
{
    memset(ep, 0, sizeof(*ep));
    ep->node_id        = node_id;
    ep->inhibit_100us  = inhibit_100us;
    ep->err_reg        = er;
    ep->err_hist       = eh;
    ep->tx_fn          = tx_fn;
    ep->tx_user        = tx_user;
}

void emcy_error_occurred(EmcyProducer      *ep,
                          uint16_t           emcy_code,
                          uint8_t            err_reg_mask,
                          const EmcyMfrInfo *mfr)
{
    if (is_code_active(ep, emcy_code))
        return;   /* duplicate – suppress per policy */

    add_active_code(ep, emcy_code);

    /* 1. Update error register */
    error_register_set(ep->err_reg, err_reg_mask);

    /* 2. Push to history */
    error_history_push(ep->err_hist, emcy_code, 0x0000u);

    /* 3. Transmit EMCY (inhibit respected by caller via tick) */
    transmit_emcy(ep, emcy_code, mfr);
}

void emcy_error_resolved(EmcyProducer *ep,
                          uint16_t      emcy_code,
                          uint8_t       err_reg_mask)
{
    if (!is_code_active(ep, emcy_code))
        return;   /* already inactive */

    remove_active_code(ep, emcy_code);

    /* 1. Clear error register bits */
    error_register_clear(ep->err_reg, err_reg_mask);

    /* 2. Transmit reset EMCY (0x0000) */
    transmit_emcy(ep, 0x0000u, NULL);
}

/* Call periodically to advance inhibit timer */
void emcy_tick_us(EmcyProducer *ep, uint32_t elapsed_us)
{
    if (ep->inhibit_timer_us > elapsed_us)
        ep->inhibit_timer_us -= elapsed_us;
    else
        ep->inhibit_timer_us = 0;
}
```

---

## Communication Error Counters

### Overview

The CAN controller hardware maintains two key error counters defined by ISO 11898:

```
  ┌──────────────────────────────────────────────────────────────┐
  │                 CAN Error State Machine                       │
  │                                                              │
  │    TEC / REC < 128             TEC / REC >= 128              │
  │   ┌─────────────┐             ┌──────────────┐               │
  │   │ ERROR       │────────────►│ ERROR        │               │
  │   │ ACTIVE      │             │ PASSIVE      │               │
  │   │             │◄────────────│              │               │
  │   └─────────────┘             └──────┬───────┘               │
  │         │                           │  TEC >= 256            │
  │         │                           ▼                        │
  │         │                    ┌──────────────┐                │
  │         │                    │  BUS-OFF     │                │
  │         │                    │  (TEC reset  │                │
  │         └────────────────────│   after 128  │                │
  │                              │   recessive  │                │
  │                              │   sequences) │                │
  │                              └──────────────┘                │
  │                                                              │
  │  TEC = Transmit Error Counter   REC = Receive Error Counter  │
  └──────────────────────────────────────────────────────────────┘
```

### CANopen-Level Monitoring

CANopen adds application-level counters on top of the hardware counters:

| Counter / Object | Description |
|---|---|
| 0x1003 push rate | Frequency of new errors entering history |
| Guarding / HB miss | NMT guard events, heartbeat consumer timeouts |
| SYNC overrun (0x1005) | SYNC received while previous not yet processed |
| RX PDO timeout (0x1400+) | Event-timer driven PDO reception watchdog |

### C Implementation – CAN Error Monitor

```c
/* can_error_monitor.h */
#ifndef CAN_ERROR_MONITOR_H
#define CAN_ERROR_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CAN_STATE_ACTIVE  = 0,
    CAN_STATE_WARNING = 1,   /* TEC or REC >= 96 (vendor warning) */
    CAN_STATE_PASSIVE = 2,
    CAN_STATE_BUS_OFF = 3
} CanState;

typedef struct {
    uint16_t  tec;              /* Transmit Error Counter          */
    uint16_t  rec;              /* Receive Error Counter           */
    CanState  state;
    CanState  prev_state;

    uint32_t  passive_entries;  /* transition count into passive   */
    uint32_t  busoff_entries;   /* transition count into bus-off   */
    uint32_t  busoff_recoveries;

    /* Thresholds (configurable) */
    uint16_t  warning_threshold;  /* default 96 */
} CanErrorMonitor;

void     can_monitor_init   (CanErrorMonitor *m);
void     can_monitor_update (CanErrorMonitor *m,
                              uint16_t tec, uint16_t rec);
CanState can_monitor_state  (const CanErrorMonitor *m);
bool     can_monitor_changed(const CanErrorMonitor *m);

#endif /* CAN_ERROR_MONITOR_H */
```

```c
/* can_error_monitor.c */
#include "can_error_monitor.h"
#include <string.h>

void can_monitor_init(CanErrorMonitor *m)
{
    memset(m, 0, sizeof(*m));
    m->warning_threshold = 96u;
    m->state      = CAN_STATE_ACTIVE;
    m->prev_state = CAN_STATE_ACTIVE;
}

void can_monitor_update(CanErrorMonitor *m,
                         uint16_t         tec,
                         uint16_t         rec)
{
    m->prev_state = m->state;
    m->tec        = tec;
    m->rec        = rec;

    CanState new_state;
    if (tec >= 256u) {
        new_state = CAN_STATE_BUS_OFF;
    } else if (tec >= 128u || rec >= 128u) {
        new_state = CAN_STATE_PASSIVE;
    } else if (tec >= m->warning_threshold ||
               rec >= m->warning_threshold) {
        new_state = CAN_STATE_WARNING;
    } else {
        new_state = CAN_STATE_ACTIVE;
    }

    if (new_state != m->state) {
        if (new_state == CAN_STATE_PASSIVE) m->passive_entries++;
        if (new_state == CAN_STATE_BUS_OFF) m->busoff_entries++;
        if (m->state  == CAN_STATE_BUS_OFF &&
            new_state != CAN_STATE_BUS_OFF)  m->busoff_recoveries++;
        m->state = new_state;
    }
}

CanState can_monitor_state(const CanErrorMonitor *m)
{
    return m->state;
}

bool can_monitor_changed(const CanErrorMonitor *m)
{
    return m->state != m->prev_state;
}
```

### Mapping CAN States to EMCY Codes

```c
/* Map CAN controller state changes to EMCY events */
void handle_can_state_change(CanErrorMonitor *m,
                              EmcyProducer    *ep)
{
    static CanState last = CAN_STATE_ACTIVE;
    CanState now = can_monitor_state(m);

    if (now == last) return;

    switch (now) {
    case CAN_STATE_PASSIVE:
        emcy_error_occurred(ep, 0x8120u, /* CAN in error passive */
                            ERR_REG_COMMUNICATION, NULL);
        break;
    case CAN_STATE_BUS_OFF:
        emcy_error_occurred(ep, 0x8140u, /* CAN bus off */
                            ERR_REG_COMMUNICATION, NULL);
        break;
    case CAN_STATE_ACTIVE:
        if (last == CAN_STATE_PASSIVE)
            emcy_error_resolved(ep, 0x8120u, ERR_REG_COMMUNICATION);
        if (last == CAN_STATE_BUS_OFF)
            emcy_error_resolved(ep, 0x8140u, ERR_REG_COMMUNICATION);
        break;
    default:
        break;
    }
    last = now;
}
```

---

## Designing a Recoverable Fault Architecture

### Fault Classification

A robust CANopen device classifies faults by severity so that recovery strategies
can be applied automatically where safe, and require operator confirmation where not.

```
  Fault Severity Tiers:
  ┌────────────────────────────────────────────────────────────────┐
  │  TIER 0 – INFORMATIONAL                                        │
  │  No output change. Log, update 0x1001/0x1003.                  │
  │  Example: Temperature warning at 80°C                          │
  ├────────────────────────────────────────────────────────────────┤
  │  TIER 1 – DEGRADED OPERATION                                   │
  │  Reduce capability, stay in Operational. Attempt auto-recover. │
  │  Example: CAN warning threshold reached (TEC/REC ≥ 96)         │
  ├────────────────────────────────────────────────────────────────┤
  │  TIER 2 – SAFE STATE                                           │
  │  Transition to NMT Pre-Operational or device-safe state.       │
  │  Require software clear. Example: CAN bus-off                  │
  ├────────────────────────────────────────────────────────────────┤
  │  TIER 3 – FATAL                                                │
  │  Immediate hardware shutdown. Require power-cycle or explicit  │
  │  hardware reset + operator confirmation.                       │
  │  Example: Overcurrent hardware latch, watchdog timeout         │
  └────────────────────────────────────────────────────────────────┘
```

### Recovery State Machine

```
  ┌──────────────────────────────────────────────────────────────┐
  │                 Recoverable Fault State Machine               │
  │                                                              │
  │  ┌──────────┐  fault T2        ┌─────────────────┐          │
  │  │          ├─────────────────►│                 │          │
  │  │ NOMINAL  │                  │  FAULT_ACTIVE   │          │
  │  │          │◄─────────────────┤                 │          │
  │  └──────────┘  auto-clear      └────────┬────────┘          │
  │                (T0/T1 only)             │                    │
  │                                         │ recovery timer     │
  │                                         ▼                    │
  │                              ┌─────────────────┐             │
  │                              │   RECOVERING    │             │
  │                              │  (wait + test)  │             │
  │                              └────────┬────────┘             │
  │                                       │                      │
  │                    ┌──────────────────┼──────────────────┐   │
  │                    │                 │                   │   │
  │                    ▼                 ▼                   ▼   │
  │           ┌──────────────┐  ┌─────────────┐  ┌─────────────┐│
  │           │   CLEARED    │  │   RETRY     │  │   LATCHED   ││
  │           │ (back to     │  │  (attempt   │  │  (Tier 3 –  ││
  │           │  Nominal)    │  │   resume)   │  │  HW reset)  ││
  │           └──────────────┘  └─────────────┘  └─────────────┘│
  └──────────────────────────────────────────────────────────────┘
```

### C Implementation – Fault Manager

```c
/* fault_manager.h */
#ifndef FAULT_MANAGER_H
#define FAULT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "emcy.h"

typedef enum {
    FAULT_TIER_INFO    = 0,
    FAULT_TIER_DEGRADE = 1,
    FAULT_TIER_SAFE    = 2,
    FAULT_TIER_FATAL   = 3
} FaultTier;

typedef enum {
    FAULT_STATE_INACTIVE  = 0,
    FAULT_STATE_ACTIVE    = 1,
    FAULT_STATE_RECOVERING= 2,
    FAULT_STATE_LATCHED   = 3
} FaultState;

typedef struct {
    uint16_t   emcy_code;
    uint8_t    err_reg_mask;
    FaultTier  tier;
    uint32_t   recovery_delay_ms;  /* 0 = immediate / manual only   */
    uint8_t    max_retries;        /* 0 = infinite (for T0/T1)      */
} FaultDef;

typedef struct {
    const FaultDef *def;
    FaultState      state;
    uint8_t         retry_count;
    uint32_t        timer_ms;
    bool            condition_active;   /* current HW/SW condition  */
} FaultInstance;

#define MAX_FAULTS 16

typedef struct {
    FaultInstance faults[MAX_FAULTS];
    uint8_t       count;
    EmcyProducer *emcy;
} FaultManager;

void fault_manager_init      (FaultManager *fm, EmcyProducer *ep);
int  fault_manager_register  (FaultManager *fm, const FaultDef *def);
void fault_manager_signal    (FaultManager *fm, uint8_t id, bool active);
void fault_manager_tick_ms   (FaultManager *fm, uint32_t dt_ms);
void fault_manager_ack_fatal (FaultManager *fm, uint8_t id);

#endif /* FAULT_MANAGER_H */
```

```c
/* fault_manager.c */
#include "fault_manager.h"
#include <string.h>

void fault_manager_init(FaultManager *fm, EmcyProducer *ep)
{
    memset(fm, 0, sizeof(*fm));
    fm->emcy = ep;
}

int fault_manager_register(FaultManager *fm, const FaultDef *def)
{
    if (fm->count >= MAX_FAULTS) return -1;
    int id = fm->count++;
    fm->faults[id].def   = def;
    fm->faults[id].state = FAULT_STATE_INACTIVE;
    return id;
}

void fault_manager_signal(FaultManager *fm, uint8_t id, bool active)
{
    if (id >= fm->count) return;
    fm->faults[id].condition_active = active;
}

static void activate_fault(FaultManager *fm, FaultInstance *fi)
{
    fi->state    = FAULT_STATE_ACTIVE;
    fi->timer_ms = 0;
    emcy_error_occurred(fm->emcy,
                        fi->def->emcy_code,
                        fi->def->err_reg_mask,
                        NULL);
}

static void clear_fault(FaultManager *fm, FaultInstance *fi)
{
    fi->state       = FAULT_STATE_INACTIVE;
    fi->retry_count = 0;
    fi->timer_ms    = 0;
    emcy_error_resolved(fm->emcy,
                        fi->def->emcy_code,
                        fi->def->err_reg_mask);
}

void fault_manager_tick_ms(FaultManager *fm, uint32_t dt_ms)
{
    for (uint8_t i = 0; i < fm->count; i++) {
        FaultInstance *fi = &fm->faults[i];
        const FaultDef *fd = fi->def;

        switch (fi->state) {

        case FAULT_STATE_INACTIVE:
            if (fi->condition_active)
                activate_fault(fm, fi);
            break;

        case FAULT_STATE_ACTIVE:
            if (!fi->condition_active &&
                fd->tier <= FAULT_TIER_DEGRADE) {
                /* Auto-clear for info/degrade tier */
                clear_fault(fm, fi);
            } else if (!fi->condition_active &&
                       fd->tier == FAULT_TIER_SAFE &&
                       fd->recovery_delay_ms > 0) {
                fi->state    = FAULT_STATE_RECOVERING;
                fi->timer_ms = fd->recovery_delay_ms;
            }
            /* FATAL stays active until ack */
            break;

        case FAULT_STATE_RECOVERING:
            fi->timer_ms = (fi->timer_ms > dt_ms) ?
                            fi->timer_ms - dt_ms : 0;
            if (fi->timer_ms == 0) {
                if (fi->condition_active) {
                    /* Fault re-appeared during recovery */
                    fi->retry_count++;
                    if (fd->max_retries > 0 &&
                        fi->retry_count >= fd->max_retries) {
                        fi->state = FAULT_STATE_LATCHED;
                    } else {
                        fi->state    = FAULT_STATE_ACTIVE;
                        fi->timer_ms = 0;
                    }
                } else {
                    clear_fault(fm, fi);
                }
            }
            break;

        case FAULT_STATE_LATCHED:
            /* Stays latched until explicit ack */
            break;
        }
    }
}

void fault_manager_ack_fatal(FaultManager *fm, uint8_t id)
{
    if (id >= fm->count) return;
    FaultInstance *fi = &fm->faults[id];
    if (fi->state == FAULT_STATE_LATCHED && !fi->condition_active)
        clear_fault(fm, fi);
}
```

### Integration with NMT

The CANopen NMT state machine reacts to persistent errors by transitioning out of
the Operational state. The fault manager feeds into this via the error register:

```c
/* nmt_guard.c – example integration */
void nmt_process_errors(FaultManager   *fm,
                         ErrorRegister  *er,
                         NmtController  *nmt)
{
    /* Any SAFE or FATAL tier fault → move to Pre-Operational */
    for (uint8_t i = 0; i < fm->count; i++) {
        const FaultInstance *fi = &fm->faults[i];
        if ((fi->def->tier >= FAULT_TIER_SAFE) &&
            (fi->state == FAULT_STATE_ACTIVE ||
             fi->state == FAULT_STATE_LATCHED)) {
            nmt_request_state(nmt, NMT_STATE_PREOPERATIONAL);
            return;
        }
    }

    /* Generic: if error register has communication error → Preop */
    if (error_register_get(er) & ERR_REG_COMMUNICATION) {
        nmt_request_state(nmt, NMT_STATE_PREOPERATIONAL);
    }
}
```

---

## Complete Integration Example

The following example ties all subsystems together in a minimal device main loop:

```c
/* main_device.c – complete CANopen error handling integration */

#include "error_register.h"
#include "error_history.h"
#include "emcy.h"
#include "can_error_monitor.h"
#include "fault_manager.h"

/* ----------------------------------------------------------------
   Hardware / BSP stubs
   ---------------------------------------------------------------- */
extern uint16_t  bsp_can_tec(void);
extern uint16_t  bsp_can_rec(void);
extern uint8_t   bsp_node_id(void);
extern bool      bsp_overcurrent(void);
extern bool      bsp_overvoltage(void);
extern uint32_t  bsp_tick_ms(void);

void bsp_can_transmit(uint32_t cob_id, const uint8_t *data, void *user)
{
    (void)user;
    /* Platform-specific CAN TX */
}

/* ----------------------------------------------------------------
   Fault definitions
   ---------------------------------------------------------------- */
static const FaultDef FAULT_OVERCURRENT = {
    .emcy_code        = 0x2100u,            /* Current, input side */
    .err_reg_mask     = ERR_REG_CURRENT,
    .tier             = FAULT_TIER_SAFE,
    .recovery_delay_ms= 500u,
    .max_retries      = 3u
};

static const FaultDef FAULT_OVERVOLTAGE = {
    .emcy_code        = 0x3200u,            /* Voltage inside device */
    .err_reg_mask     = ERR_REG_VOLTAGE,
    .tier             = FAULT_TIER_SAFE,
    .recovery_delay_ms= 1000u,
    .max_retries      = 5u
};

/* ----------------------------------------------------------------
   Global state
   ---------------------------------------------------------------- */
static ErrorRegister   g_err_reg;
static ErrorHistory    g_err_hist;
static EmcyProducer    g_emcy;
static CanErrorMonitor g_can_mon;
static FaultManager    g_fault_mgr;

static int FAULT_ID_OC;  /* overcurrent fault ID */
static int FAULT_ID_OV;  /* overvoltage fault ID */

/* ----------------------------------------------------------------
   Initialization
   ---------------------------------------------------------------- */
void device_init(void)
{
    error_register_init(&g_err_reg);
    error_history_init(&g_err_hist);

    emcy_init(&g_emcy,
              bsp_node_id(),
              10u,               /* inhibit = 1 ms (10 × 100 µs)  */
              &g_err_reg,
              &g_err_hist,
              bsp_can_transmit,
              NULL);

    can_monitor_init(&g_can_mon);

    fault_manager_init(&g_fault_mgr, &g_emcy);
    FAULT_ID_OC = fault_manager_register(&g_fault_mgr, &FAULT_OVERCURRENT);
    FAULT_ID_OV = fault_manager_register(&g_fault_mgr, &FAULT_OVERVOLTAGE);
}

/* ----------------------------------------------------------------
   Periodic task – call every 1 ms
   ---------------------------------------------------------------- */
static uint32_t last_tick = 0;

void device_task_1ms(void)
{
    uint32_t now = bsp_tick_ms();
    uint32_t dt  = now - last_tick;
    last_tick    = now;

    /* 1. Update CAN bus error state */
    can_monitor_update(&g_can_mon, bsp_can_tec(), bsp_can_rec());
    if (can_monitor_changed(&g_can_mon))
        handle_can_state_change(&g_can_mon, &g_emcy);

    /* 2. Signal application faults */
    fault_manager_signal(&g_fault_mgr, FAULT_ID_OC, bsp_overcurrent());
    fault_manager_signal(&g_fault_mgr, FAULT_ID_OV, bsp_overvoltage());

    /* 3. Run fault state machines */
    fault_manager_tick_ms(&g_fault_mgr, dt);

    /* 4. Advance EMCY inhibit timer */
    emcy_tick_us(&g_emcy, dt * 1000u);
}
```

---

## Summary

CANopen Error Handling is built on four interlocking mechanisms that, together, provide
both real-time fault reporting and long-term diagnostic traceability:

```
  ┌──────────────────────────────────────────────────────────────────┐
  │                  CANopen Error Handling – Summary                 │
  │                                                                  │
  │  Object 0x1001 – Error Register                                  │
  │  • 8-bit live bitmask of active error classes                    │
  │  • Bit 0 (Generic) must mirror the OR of all other bits          │
  │  • Included in every EMCY frame (byte 2)                         │
  │                                                                  │
  │  Object 0x1003 – Pre-defined Error Field                         │
  │  • FIFO array (max 254 entries) of 32-bit error records          │
  │  • Sub-index 0 = current count; sub-index 1 = most recent        │
  │  • Each entry = 16-bit EMCY code + 16-bit mfr-specific           │
  │  • Cleared by writing 0 to sub-index 0                           │
  │                                                                  │
  │  EMCY Object (COB-ID 0x080 + Node-ID)                            │
  │  • 8-byte frame: EMCY code | Error Register | Mfr (5 bytes)      │
  │  • Sent on error occurrence AND on error recovery (code 0x0000)  │
  │  • Suppressed for duplicate active errors                        │
  │  • Throttled by inhibit time (Object 0x1015, unit 100 µs)        │
  │                                                                  │
  │  Communication Error Counters                                    │
  │  • ISO 11898 TEC/REC → Active / Warning / Passive / Bus-Off      │
  │  • Bus-Off maps to EMCY 0x8140; passive maps to 0x8120           │
  │                                                                  │
  │  Recoverable Fault Architecture                                  │
  │  • Tier 0/1: auto-clear; Tier 2: recovery delay + retry limit    │
  │  • Tier 3: hardware latch, requires explicit acknowledgment      │
  │  • Persistent Tier 2/3 faults trigger NMT → Pre-Operational      │
  └──────────────────────────────────────────────────────────────────┘
```

### Key Design Principles

**Consistency**: The error register, error history, and EMCY message must always
agree. Update the register before transmitting the EMCY frame to ensure any SDO
read of 0x1001 during or after the EMCY sees the correct state.

**No duplicate storms**: Track which error codes are currently active and suppress
re-transmission of the same code while it remains unresolved.

**Inhibit time**: Always honour Object 0x1015 to prevent a rapidly oscillating
fault from monopolising the CAN bus bandwidth.

**Tiered recovery**: Not every fault should go straight to a safe shutdown.
Classifying faults by severity allows the device to degrade gracefully, attempt
automatic recovery for transient faults, and escalate only genuinely serious
conditions to the operator.

**Audit trail**: The pre-defined error field (0x1003) provides valuable post-mortem
data. In field-deployed equipment it is often the first diagnostic step when
investigating intermittent issues.

---

*Reference: CiA 301 v4.2 – CANopen Application Layer and Communication Profile,
ISO 11898-1 – Road vehicles – Controller area network.*