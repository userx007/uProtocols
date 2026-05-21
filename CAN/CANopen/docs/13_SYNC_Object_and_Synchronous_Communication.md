# 13. SYNC Object & Synchronous Communication

**Structure overview:**

| Section | Content |
|---|---|
| §1 Overview | Purpose of SYNC, CAN frame layout, why synchronous beats asynchronous |
| §2 SYNC COB-ID | OD 0x1005 bit-field breakdown, COB-ID change sequence |
| §3 SYNC Counter | OD 0x1019, counter waveform, sub-harmonic PDO scheduling |
| §4 Object Dictionary | All relevant OD entries for producer and consumer sides |
| §5 Window Length | OD 0x1007, timing diagram, bus-load sizing guidance |
| §6 SYNC Producer | Bare-metal ISR in C, consumer dispatch in C, C++ class with `std::atomic` |
| §7 PDO Timing | Two-phase data latch/commit model, TPDO/RPDO timing diagrams, double-buffer RPDO in C |
| §8 Multi-Axis Motion | Network topology, per-cycle bus frame sequence, interpolated C++ master, drive slave in C, 3-axis coordinated move timing |
| §9 Error Handling | SYNC watchdog timer in C, counter discontinuity detection, 4 common configuration pitfalls |
| §10 Summary | Quick-reference table, design checklist, relationship diagram to other CANopen objects |

All diagrams use ASCII graphics as requested, and code examples use `packed` structs, `volatile`, `std::atomic`, and ISR-safe patterns appropriate for embedded C/C++ targets.

> **Standard Reference:** CiA 301 — CANopen Application Layer and Communication Profile  
> **Applies to:** CiA 301 v4.0 (basic SYNC), CiA 301 v4.2+ (SYNC counter extension)

---

## Table of Contents

1. [Overview and Purpose](#1-overview-and-purpose)
2. [SYNC COB-ID (0x80)](#2-sync-cob-id-0x80)
3. [SYNC Counter (CiA 301 v4.2+)](#3-sync-counter-cia-301-v42)
4. [Object Dictionary Entries](#4-object-dictionary-entries)
5. [Synchronous Window Length](#5-synchronous-window-length)
6. [SYNC Producer Implementation](#6-sync-producer-implementation)
7. [Synchronised PDO Transmission Timing](#7-synchronised-pdo-transmission-timing)
8. [Multi-Axis Coordinated Motion with SYNC](#8-multi-axis-coordinated-motion-with-sync)
9. [Error Handling and Edge Cases](#9-error-handling-and-edge-cases)
10. [Summary](#10-summary)

---

## 1. Overview and Purpose

The **SYNC object** is the heartbeat of deterministic CANopen motion control. It is a
short CAN frame broadcast by a designated *SYNC producer* at a fixed, configurable period.
All *SYNC consumers* (drives, sensors, I/O modules) use it as a shared time reference to
latch input data and/or commit output data atomically — without any node needing an
independent, calibrated clock.

### Why Synchronous Communication?

Without SYNC, each PDO fires independently. Position setpoints for a three-axis gantry
arrive at slightly different times; the axes move asynchronously; the tool path deviates.
With SYNC all axes receive their setpoints and apply them at the same instant,
producing coordinated, deterministic motion.

```
   ASYNCHRONOUS (no SYNC)                  SYNCHRONOUS (with SYNC)
   ─────────────────────────               ─────────────────────────
   Axis A setpoint ──►[now]                        SYNC
   Axis B setpoint ──────────►[now+Δt]      ┌──────┴──────┐
   Axis C setpoint ──────────────►[now+2Δt] │             │
                                           A+B+C all apply setpoints
   Axes diverge from desired path          simultaneously → perfect path
```

### SYNC Message on the CAN Bus

```
   ┌────────────────────────────────────────────────────────────────┐
   │  CAN Frame                                                     │
   │  ┌──────────┬───┬────┬────────────────────────────────────┐    │
   │  │ COB-ID   │RTR│DLC │ Data Bytes                         │    │
   │  │ 0x080    │ 0 │  0 │ (empty — v4.0)                     │    │
   │  │ 0x080    │ 0 │  1 │ counter[7:0]  (v4.2+ when enabled) │    │
   │  └──────────┴───┴────┴────────────────────────────────────┘    │
   └────────────────────────────────────────────────────────────────┘
```

Key characteristics:

- Smallest possible CAN frame (0 data bytes, v4.0) — minimal bus load.
- Fixed high-priority COB-ID 0x080 (lower ID = higher CAN arbitration priority).
- Period typically 1 ms–10 ms for motion; 10 ms–100 ms for process I/O.
- A single SYNC producer per network (though redundant producer schemes exist in
  safety profiles).

---

## 2. SYNC COB-ID (0x80)

### COB-ID Register: Object 0x1005

The SYNC COB-ID is held in **Object 0x1005** (Communication Cycle Period).
The 32-bit value encodes both the CAN identifier and control flags:

```
   Bit 31       Bit 30       Bits 29–11   Bits 10–0
   ┌──────────┬───────────┬─────────────┬──────────────────┐
   │ Generate │ Frame     │  Reserved   │  CAN Identifier  │
   │ SYNC     │ Type      │  (= 0)      │  (default 0x080) │
   │ (1=yes)  │ (0=11-bit)│             │                  │
   └──────────┴───────────┴─────────────┴──────────────────┘
```

| Bit | Name              | Meaning                                            |
|-----|-------------------|----------------------------------------------------|
| 31  | `gen_sync`        | 1 = this node generates SYNC; 0 = consumer only    |
| 30  | `frame_type`      | 0 = 11-bit ID (standard); 1 = 29-bit (extended)    |
| 10–0| `can_id`          | CAN identifier — default **0x080**                 |

**Important:** Changing the COB-ID while the device is operational is **forbidden**.
The node must be in NMT Pre-Operational or Stopped state, the MSB (bit 31) must first
be cleared, then the new ID written, and finally bit 31 set again if this node is the
producer.

```c
/* Typical 0x1005 values */
#define SYNC_COBID_CONSUMER  0x00000080UL   /* consume SYNC, ID=0x080     */
#define SYNC_COBID_PRODUCER  0x40000080UL   /* generate SYNC, ID=0x080    */
#define SYNC_COBID_EXT_PROD  0xC0000080UL   /* generate, extended frame   */
```

### COB-ID Change Sequence

```
   NMT: Pre-Operational
          │
          ▼
   Write 0x1005 = 0x00000080  (clear bit 31 — disable generation)
          │
          ▼
   Write 0x1005 = 0x00000085  (new CAN ID = 0x085, still disabled)
          │
          ▼
   Write 0x1005 = 0x40000085  (set bit 31 — enable generation)
          │
          ▼
   NMT: Operational
```

---

## 3. SYNC Counter (CiA 301 v4.2+)

From CiA 301 version 4.2 onward the SYNC message may carry a **1-byte counter**
(DLC = 1). The counter increments from 1 to the *SYNC counter overflow* value, then
wraps back to 1.

### Object 0x1019 — SYNC Counter Overflow Value

| Value | Meaning                                                          |
|-------|------------------------------------------------------------------|
| 0     | Counter disabled; SYNC sent with DLC=0 (backward compatible)     |
| 1     | Counter disabled (reserved, do not use)                          |
| 2–240 | Counter cycles 1 → N → 1 → N → …                                 |

### Typical Counter Waveform (overflow = 4)

```
   SYNC#    1     2     3     4     5     6     7     8
            │     │     │     │     │     │     │     │
   counter  1     2     3     4     1     2     3     4
            │     │     │     │     │     │     │     │
            ▼     ▼     ▼     ▼     ▼     ▼     ▼     ▼
   ─────────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────
            │0x80 │0x80 │0x80 │0x80 │0x80 │0x80 │0x80 │0x80 │
            │  1  │  2  │  3  │  4  │  1  │  2  │  3  │  4  │
   ─────────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────
```

### Why Use a Counter?

The counter allows SYNC consumers to perform actions only on **specific SYNC cycles**,
enabling sub-harmonic scheduling:

```
   Overflow = 4, period = 1 ms

   Counter 1 → PDO group A transmits  (1 ms period)
   Counter 2 → PDO group B transmits  (1 ms period, different phase)
   Counter 4 → PDO group C transmits  (4 ms period, every 4th SYNC)

   ┌────┬────┬────┬────┬────┬────┬────┬────┐
   │  1 │  2 │  3 │  4 │  1 │  2 │  3 │  4 │   SYNC counter
   ├────┼────┼────┼────┼────┼────┼────┼────┤
   │ A  │ A  │ A  │ A  │ A  │ A  │ A  │ A  │   Every SYNC
   │ B  │    │    │    │ B  │    │    │    │   Every 4th (counter=1)
   │    │    │    │ C  │    │    │    │ C  │   Every 4th (counter=4)
   └────┴────┴────┴────┴────┴────┴────┴────┘
```

Transmission type **0xFC** (252) means "transmit on every SYNC where counter == SYNC
start value" — the PDO's own *SYNC start value* (sub-index in 0x1800/0x1A00) selects
which counter value triggers it.

---

## 4. Object Dictionary Entries

### 4.1 Master / Producer Side

| Index  | Name                        | Type   | Default     | Description                            |
|--------|-----------------------------|--------|-------------|----------------------------------------|
| 0x1005 | COB-ID SYNC message         | UINT32 | 0x00000080  | ID + producer flag                     |
| 0x1006 | Communication cycle period  | UINT32 | 0           | SYNC period in microseconds; 0=off     |
| 0x1019 | Synchronous counter overflow| UINT8  | 0           | Counter wrap value; 0=no counter       |

### 4.2 Consumer Side (per PDO)

| Index  | Sub | Name                        | Type   | Description                              |
|--------|-----|-----------------------------|--------|------------------------------------------|
| 0x1400 | 02  | TPDO1 transmission type     | UINT8  | 0x01–0xF0 = every N SYNCs                |
| 0x1800 | 02  | RPDO1 transmission type     | UINT8  | same encoding                            |
| 0x1800 | 06  | SYNC start value            | UINT8  | counter value that triggers first tx     |
| 0x1007 | 00  | Synchronous window length   | UINT32 | window in microseconds; 0=no window      |

### PDO Transmission Type Encoding

```
   Value    Meaning
   ─────    ──────────────────────────────────────────────────────────
   0x00     Acyclic synchronous — on SYNC, but only if flagged by app
   0x01     Cyclic — every 1 SYNC
   0x02     Cyclic — every 2 SYNCs
   ...
   0xF0     Cyclic — every 240 SYNCs
   0xFC     Synchronous, RTR only (with SYNC counter match)
   0xFD     Asynchronous, RTR only
   0xFE     Asynchronous, manufacturer-specific event
   0xFF     Asynchronous, device profile event
```

---

## 5. Synchronous Window Length

Object **0x1007** defines a time window (in microseconds) that opens **immediately
after** each SYNC message and within which all synchronous PDOs must be transmitted or
received. PDOs arriving outside this window shall be discarded by the receiver.

```
   Timeline (one SYNC period, e.g. 1000 µs)
   ──────────────────────────────────────────────────────────────────
   0 µs        50 µs     (window = 50 µs)          1000 µs
   │           │                                     │
   ▼           ▼                                     ▼
   SYNC ══════╗                                     SYNC
              ║  ← synchronous window open           │
   PDO_A ─────╫──►  OK (arrives at 10 µs)            │
   PDO_B ─────╫──────────────►  OK (40 µs)           │
              ║                                      │
   PDO_C ─────╫──────────────────────────────►  LATE! discarded
   ════════════╝ window closes at 50 µs
```

### Practical Guidance

- Window should be at least **bus_utilisation × SYNC_period** wide.
- A CAN frame at 1 Mbit/s takes ~130 µs; for 5 synchronous PDOs set window ≥ 700 µs.
- Window of 0 disables the check entirely (useful during commissioning).
- The window is a *consumer-side* guard only; the producer does not know about it.

---

## 6. SYNC Producer Implementation

### 6.1 Hardware Timer Approach (Bare-Metal C)

```c
/*
 * sync_producer.c
 * Bare-metal SYNC producer using a hardware timer ISR.
 * Target: any microcontroller with a CAN peripheral.
 * Standard: CiA 301 v4.2+  (counter enabled)
 */

#include <stdint.h>
#include <stdbool.h>
#include "can_driver.h"     /* platform CAN abstraction */
#include "timer_driver.h"   /* platform timer abstraction */

/* ── Object Dictionary mirror ─────────────────────────────────────────── */
#define OD_1005_SYNC_COBID    0x40000080UL  /* produce, 11-bit, id=0x080  */
#define OD_1006_CYCLE_PERIOD  1000UL        /* 1000 µs = 1 ms             */
#define OD_1019_CNT_OVERFLOW  10U           /* counter wraps 1..10        */

/* ── Internal state ────────────────────────────────────────────────────── */
static volatile uint8_t  sync_counter   = 0U;
static volatile uint32_t sync_tx_count  = 0UL;  /* statistics             */
static volatile bool     sync_enabled   = false;

/* Derived from 0x1005 */
static uint32_t sync_cob_id;
static bool     sync_produce;
static bool     sync_counter_active;

/* ── Initialise ────────────────────────────────────────────────────────── */
void sync_producer_init(void)
{
    uint32_t cobid   = OD_1005_SYNC_COBID;
    sync_produce     = (cobid & (1UL << 31)) != 0UL;
    sync_cob_id      = cobid & 0x1FFFFFFFUL;         /* mask flag bits     */
    sync_counter_active = (OD_1019_CNT_OVERFLOW >= 2U);
    sync_counter        = 0U;

    if (sync_produce && OD_1006_CYCLE_PERIOD > 0UL) {
        timer_set_period_us(OD_1006_CYCLE_PERIOD);
        timer_set_callback(sync_timer_isr);
        timer_start();
        sync_enabled = true;
    }
}

/* ── Timer ISR — called every SYNC period ──────────────────────────────── */
void sync_timer_isr(void)   /* __attribute__((interrupt)) on ARM Cortex-M  */
{
    can_frame_t frame;

    /* Advance counter */
    if (sync_counter_active) {
        sync_counter++;
        if (sync_counter > OD_1019_CNT_OVERFLOW) {
            sync_counter = 1U;
        }
    }

    /* Build CAN frame */
    frame.id  = sync_cob_id;       /* 0x080                                */
    frame.rtr = 0U;
    if (sync_counter_active) {
        frame.dlc     = 1U;
        frame.data[0] = sync_counter;
    } else {
        frame.dlc = 0U;             /* v4.0 compatible — no counter byte    */
    }

    /* Transmit — use highest-priority mailbox */
    can_transmit_high_prio(&frame);
    sync_tx_count++;
}

/* ── Runtime control ───────────────────────────────────────────────────── */
void sync_producer_stop(void)
{
    timer_stop();
    sync_enabled  = false;
    sync_counter  = 0U;
}

void sync_producer_set_period_us(uint32_t period_us)
{
    /* Only callable from Pre-Operational state */
    timer_stop();
    timer_set_period_us(period_us);
    timer_start();
}

uint8_t sync_get_counter(void)
{
    return sync_counter;   /* atomic read on 8-bit value */
}
```

### 6.2 SYNC Consumer — Reception and Dispatch (C)

```c
/*
 * sync_consumer.c
 * Receives SYNC and triggers synchronous PDO processing.
 */

#include <stdint.h>
#include "can_driver.h"
#include "pdo_manager.h"

#define SYNC_COB_ID           0x080U
#define SYNC_WINDOW_US        500U      /* Object 0x1007 value            */

static volatile uint8_t  last_sync_counter  = 0U;
static volatile uint32_t sync_rx_count      = 0UL;
static volatile uint64_t last_sync_time_us  = 0ULL;

/* ── Called from CAN RX ISR or task when a frame arrives ──────────────── */
void sync_consumer_on_rx(const can_frame_t *frame, uint64_t rx_time_us)
{
    uint8_t counter = 0U;

    /* Validate: must be SYNC COB-ID */
    if (frame->id != SYNC_COB_ID) {
        return;
    }
    /* Validate DLC: 0 (v4.0) or 1 (v4.2+) */
    if (frame->dlc > 1U) {
        /* Error: unexpected DLC — emit emergency? */
        emcy_send(0x8100U, 0x00U);  /* SYNC_ERROR */
        return;
    }

    if (frame->dlc == 1U) {
        counter = frame->data[0];
        /* Counter validation: must be 1..overflow_value */
        if (counter == 0U) {
            emcy_send(0x8100U, 0x01U);  /* counter = 0 is illegal */
            return;
        }
        /* Optional: detect missed SYNCs by checking continuity */
        uint8_t expected = (last_sync_counter % od_get_u8(0x1019)) + 1U;
        if (last_sync_counter != 0U && counter != expected) {
            /* Missed one or more SYNCs */
            fault_handler_sync_missed(expected, counter);
        }
        last_sync_counter = counter;
    }

    last_sync_time_us = rx_time_us;
    sync_rx_count++;

    /* Notify PDO manager — it decides which PDOs fire now */
    pdo_manager_on_sync(counter, rx_time_us);
}

/* ── PDO manager excerpt: synchronous PDO dispatch ───────────────────── */
void pdo_manager_on_sync(uint8_t counter, uint64_t sync_time_us)
{
    for (int i = 0; i < NUM_TPDOS; i++) {
        tpdo_t *pdo = &tpdo_table[i];

        /* Skip disabled PDOs */
        if (!pdo->enabled) continue;

        /* Synchronous transmission types 0x01–0xF0 */
        if (pdo->trans_type >= 0x01U && pdo->trans_type <= 0xF0U) {
            pdo->sync_count++;
            if (pdo->sync_count >= pdo->trans_type) {
                pdo->sync_count = 0;
                tpdo_transmit(pdo, sync_time_us, SYNC_WINDOW_US);
            }
        }
        /* Acyclic synchronous (0x00): transmit only if data changed */
        else if (pdo->trans_type == 0x00U && pdo->data_changed) {
            pdo->data_changed = false;
            tpdo_transmit(pdo, sync_time_us, SYNC_WINDOW_US);
        }
    }
    /* Process synchronous RPDOs — apply buffered setpoints NOW */
    rpdo_apply_synchronous_buffers(counter);
}
```

### 6.3 SYNC Producer — C++ Class

```cpp
/*
 * SyncProducer.hpp
 * Object-oriented SYNC producer with configurable period and counter.
 * CiA 301 v4.2+ compliant.
 */

#pragma once
#include <cstdint>
#include <functional>
#include <atomic>

class SyncProducer {
public:
    /* Callback signature: fired after each SYNC is sent */
    using SyncCallback = std::function<void(uint8_t counter)>;

    struct Config {
        uint32_t cobId          = 0x40000080UL; /* OD 0x1005               */
        uint32_t cyclePeriodUs  = 1000U;         /* OD 0x1006: 1 ms default */
        uint8_t  counterOverflow = 0U;           /* OD 0x1019: 0=disabled   */
    };

    explicit SyncProducer(const Config& cfg)
        : cfg_(cfg)
        , counter_(0U)
        , txCount_(0UL)
        , running_(false)
    {
        cobId_          = cfg.cobId & 0x1FFFFFFFUL;
        produce_        = (cfg.cobId & (1UL << 31)) != 0UL;
        counterActive_  = (cfg.counterOverflow >= 2U);
    }

    bool start()
    {
        if (!produce_ || cfg_.cyclePeriodUs == 0U) return false;
        running_.store(true);
        /* Platform: arm hardware timer, POSIX timer, RTOS task, etc. */
        return platform_timer_start(cfg_.cyclePeriodUs,
                                    [this]{ onTimerFired(); });
    }

    void stop()
    {
        running_.store(false);
        platform_timer_stop();
        counter_.store(0U);
    }

    /* Called by timer ISR/task */
    void onTimerFired()
    {
        if (!running_.load()) return;

        uint8_t cnt = 0U;
        if (counterActive_) {
            uint8_t c = counter_.load();
            c = (c >= cfg_.counterOverflow) ? 1U : c + 1U;
            counter_.store(c);
            cnt = c;
        }

        /* Build and send CAN frame */
        CanFrame frame{};
        frame.id  = cobId_;
        frame.rtr = false;
        frame.dlc = counterActive_ ? 1U : 0U;
        if (counterActive_) frame.data[0] = cnt;

        can_transmit_highprio(frame);
        txCount_.fetch_add(1U, std::memory_order_relaxed);

        if (callback_) callback_(cnt);
    }

    void setCallback(SyncCallback cb)    { callback_ = std::move(cb); }
    uint8_t  getCounter()   const        { return counter_.load(); }
    uint32_t getTxCount()   const        { return txCount_.load(); }
    bool     isRunning()    const        { return running_.load(); }

private:
    Config              cfg_;
    std::atomic<uint8_t>  counter_;
    std::atomic<uint32_t> txCount_;
    std::atomic<bool>     running_;
    uint32_t            cobId_;
    bool                produce_;
    bool                counterActive_;
    SyncCallback        callback_;
};
```

---

## 7. Synchronised PDO Transmission Timing

### 7.1 The Two-Phase Model

Synchronous PDOs follow a strict two-phase model to prevent data races:

```
   Phase 1: DATA LATCH  (application fills/reads PDO data into a shadow buffer)
   Phase 2: COMMIT      (on SYNC — shadow buffer swaps with live buffer)

   Application thread             SYNC ISR
   ──────────────────             ────────
   write shadow_buf[pos]          │
   write shadow_buf[vel]          │
   write shadow_buf[torque]       │
                                  SYNC arrives
                                  ┌────────────────────────┐
                                  │ swap(shadow, live)     │  atomic
                                  │ transmit live_buf PDO  │  window opens
                                  └────────────────────────┘
   read shadow_buf[pos]           ← next cycle already safe to write
```

### 7.2 Transmit PDO (TPDO) Timing

```
   TPDO, transmission type = 0x02 (every 2 SYNCs), window = 500 µs

   Time (ms):  0    1    2    3    4    5    6
               │    │    │    │    │    │    │
   SYNC:       ▼    ▼    ▼    ▼    ▼    ▼    ▼
   cnt:        1    2    1    2    1    2    1
               ├────┴────┼────┴────┼────┴────┤
   TPDO:            ▼         ▼         ▼       (fires every 2nd SYNC)
               │◄──►│           window open
               0   0.5 ms

   ───SYNC──[window open]───────────────────────[window closed]──SYNC───
       │         │                                      │           │
       0µs      PDO1  PDO2  PDO3                       500µs      1000µs
                 ▼     ▼     ▼
```

### 7.3 Receive PDO (RPDO) with Synchronous Latching

```c
/*
 * rpdo_sync.c
 * Demonstrates double-buffering for synchronous RPDO data.
 * The CAN RX fills 'rxbuf', SYNC swaps it into 'activebuf'.
 */

#include <stdint.h>
#include <string.h>
#include "can_driver.h"

#define RPDO1_COBID   0x200U   /* Node-ID 0: 0x200 + node_id            */
#define RPDO1_DLC     8U

typedef struct {
    int32_t  target_position;   /* bytes 0–3, unit: counts               */
    int16_t  target_velocity;   /* bytes 4–5, unit: counts/ms            */
    uint8_t  control_word;      /* byte  6                               */
    uint8_t  mode;              /* byte  7                               */
} __attribute__((packed)) rpdo1_data_t;

static rpdo1_data_t rxbuf;       /* filled by CAN RX callback            */
static rpdo1_data_t activebuf;   /* used by motion control task          */
static volatile bool rxbuf_fresh = false;

/* ── CAN RX callback (ISR context) ───────────────────────────────────── */
void on_can_rx(const can_frame_t *f)
{
    if (f->id == RPDO1_COBID && f->dlc == RPDO1_DLC) {
        memcpy(&rxbuf, f->data, sizeof(rpdo1_data_t));
        rxbuf_fresh = true;
        /* Do NOT apply data yet — wait for SYNC */
    }
}

/* ── SYNC callback (ISR context) ─────────────────────────────────────── */
void on_sync_received(uint8_t counter)
{
    /* Commit: swap shadow → active (only if new data arrived) */
    if (rxbuf_fresh) {
        rpdo1_data_t tmp = activebuf;   /* save previous for fallback     */
        activebuf  = rxbuf;
        rxbuf_fresh = false;
        (void)tmp;
    }
    /* Motion task will read activebuf this cycle */
    motion_task_trigger();
}

/* ── Motion control task ─────────────────────────────────────────────── */
void motion_task(void)
{
    /* Always reads a consistent, SYNC-aligned snapshot */
    int32_t pos_cmd = activebuf.target_position;
    int16_t vel_cmd = activebuf.target_velocity;
    apply_position_setpoint(pos_cmd, vel_cmd);
}
```

### 7.4 Timing Diagram: RPDO Double Buffer

```
   CAN Bus    RPDO arrives (any time within period)
              │
   Time:      │  0 µs        500 µs      1000 µs     1500 µs
   ───────────┼──────────────────────────────────────────────
   SYNC:      ▼              ▼                        ▼
   RPDO:        ──────►                ──────►
               (stored in rxbuf)       (stored in rxbuf)

   rxbuf_fresh:  ___/─────────\___/──────────\___
                    ^set by RX   ^cleared by SYNC

   activebuf:  [T0 data]       [T1 data]           [T2 data]
                             ^swapped on SYNC     ^swapped on SYNC

   Motion:              uses T0          uses T1            uses T2
                        │               │                   │
                        └──────1 period─┘                   │
```

---

## 8. Multi-Axis Coordinated Motion with SYNC

### 8.1 Architecture

```
   ┌─────────────────────────────────────────────────────────────────────┐
   │                    CANopen Network @ 1 Mbit/s                       │
   │                                                                     │
   │   ┌─────────────┐    CAN Bus (linear topology)                      │
   │   │  Motion     │                                                   │
   │   │  Controller │──────────────────────────────────────────────┐    │
   │   │  (Master)   │                                              │    │
   │   │  Node 1     │    ┌─────────┐  ┌─────────┐  ┌─────────┐     │    │
   │   │             │    │ Drive A │  │ Drive B │  │ Drive C │     │    │
   │   │ SYNC prod.  │    │ Node 2  │  │ Node 3  │  │ Node 4  │     │    │
   │   │ 0x1006=1ms  │    │ X-Axis  │  │ Y-Axis  │  │ Z-Axis  │     │    │
   │   └─────────────┘    └────┬────┘  └────┬────┘  └────┬────┘     │    │
   │                           │            │            │          │    │
   │   ─────────────────────────────────────────────────────────────│    │
   │                                 CAN Bus                        │    │
   │   ◄────────────────────────────────────────────────────────────┘    │
   └─────────────────────────────────────────────────────────────────────┘

   PDO mapping:
     RPDO1 (0x201): X setpoint  ← from master  (tx type 0x01)
     RPDO1 (0x301): Y setpoint  ← from master  (tx type 0x01)
     RPDO1 (0x401): Z setpoint  ← from master  (tx type 0x01)
     TPDO1 (0x181): X actual    → to master    (tx type 0x01)
     TPDO1 (0x281): Y actual    → to master    (tx type 0x01)
     TPDO1 (0x381): Z actual    → to master    (tx type 0x01)
```

### 8.2 Bus Frame Sequence Within One SYNC Cycle

```
   Time (µs):  0      10     30     50     70     90    110    500
               │      │      │      │      │      │      │      │
   Frames:    SYNC  RPDO-X RPDO-Y RPDO-Z TPDO-X TPDO-Y TPDO-Z  │
               │      │      │      │      │      │      │      │
   Priority:  80h   201h   301h   401h   181h   281h   381h     │
              HIGH                                          (next period)

   ═════╤═════╤══════╤══════╤══════╤══════╤══════╤══════╤══════════
   SYNC │0x201│0x301 │0x401 │0x181 │0x281 │0x381 │      │  (idle)
   ═════╧═════╧══════╧══════╧══════╧══════╧══════╧══════╧══════════
    0µs  12µs  24µs   36µs   48µs   60µs   72µs
   ├─────────────── sync window (500 µs) ───────────────┤
```

### 8.3 Master: Interpolated Position Controller (C++)

```cpp
/*
 * MultiAxisSyncMaster.cpp
 * Three-axis coordinated motion using synchronous PDOs and SYNC.
 * Demonstrates interpolation, PDO packing, and SYNC callback.
 */

#include <cstdint>
#include <cmath>
#include <array>

/* ── PDO data types ────────────────────────────────────────────────────── */
struct AxisSetpoint {
    int32_t  posTarget;    /* encoder counts          */
    int16_t  velFeedFwd;   /* counts / SYNC period    */
    uint16_t controlWord;
} __attribute__((packed));

struct AxisFeedback {
    int32_t  posActual;
    int16_t  velActual;
    uint16_t statusWord;
} __attribute__((packed));

/* ── Trajectory generator ──────────────────────────────────────────────── */
class LinearSegment {
public:
    LinearSegment(int32_t start, int32_t end, uint32_t syncSteps)
        : start_(start), delta_(end - start), steps_(syncSteps), step_(0U)
    {}

    int32_t nextPosition()
    {
        if (step_ >= steps_) return start_ + delta_;
        int32_t pos = start_ + (int32_t)(((int64_t)delta_ * step_) / steps_);
        ++step_;
        return pos;
    }

    int16_t velocity() const
    {
        return (steps_ > 0U) ? (int16_t)(delta_ / (int32_t)steps_) : 0;
    }

    bool done() const { return step_ >= steps_; }

private:
    int32_t  start_, delta_;
    uint32_t steps_, step_;
};

/* ── Multi-axis master ─────────────────────────────────────────────────── */
class MultiAxisMaster {
public:
    static constexpr size_t NUM_AXES = 3U;

    struct AxisConfig {
        uint32_t rpdoCobId;      /* e.g. 0x201, 0x301, 0x401 */
        uint32_t tpdoCobId;      /* e.g. 0x181, 0x281, 0x381 */
        uint32_t countsPerMm;    /* encoder resolution        */
    };

    MultiAxisMaster(std::array<AxisConfig, NUM_AXES> cfg)
        : cfg_(cfg), syncCount_(0UL)
    {}

    /* Called by SYNC producer callback each SYNC period */
    void onSync(uint8_t syncCounter)
    {
        syncCount_++;

        for (size_t ax = 0U; ax < NUM_AXES; ++ax) {
            if (traj_[ax]) {
                /* Compute interpolated setpoint */
                AxisSetpoint sp{};
                sp.posTarget   = traj_[ax]->nextPosition();
                sp.velFeedFwd  = traj_[ax]->velocity();
                sp.controlWord = 0x000FU;  /* enable operation            */

                /* Pack into RPDO and transmit within sync window */
                sendAxisSetpoint(cfg_[ax].rpdoCobId, sp);

                if (traj_[ax]->done()) {
                    traj_[ax].reset();
                }
            }
        }
    }

    /* Called by TPDO reception handler */
    void onAxisFeedback(uint32_t cobId, const AxisFeedback& fb)
    {
        for (size_t ax = 0U; ax < NUM_AXES; ++ax) {
            if (cfg_[ax].tpdoCobId == cobId) {
                feedback_[ax] = fb;
            }
        }
    }

    /* Command a linear move: distance in mm, time in ms (= SYNC periods) */
    void moveTo(size_t axis, double targetMm, uint32_t timeMsec)
    {
        if (axis >= NUM_AXES) return;
        int32_t startCounts = feedback_[axis].posActual;
        int32_t endCounts   = (int32_t)(targetMm * cfg_[axis].countsPerMm);
        traj_[axis] = std::make_unique<LinearSegment>(startCounts,
                                                       endCounts,
                                                       timeMsec);
    }

private:
    void sendAxisSetpoint(uint32_t cobId, const AxisSetpoint& sp)
    {
        CanFrame frame{};
        frame.id  = cobId;
        frame.dlc = sizeof(AxisSetpoint);
        memcpy(frame.data, &sp, sizeof(AxisSetpoint));
        can_transmit(&frame);
    }

    std::array<AxisConfig, NUM_AXES>                cfg_;
    std::array<AxisFeedback, NUM_AXES>              feedback_{};
    std::array<std::unique_ptr<LinearSegment>, NUM_AXES> traj_{};
    uint32_t                                        syncCount_;
};
```

### 8.4 Drive (Slave) — Synchronous Setpoint Application (C)

```c
/*
 * drive_sync_slave.c
 * Minimal drive-side synchronous setpoint handling.
 * Implements double-buffered RPDO + SYNC-triggered apply.
 */

#include <stdint.h>
#include <string.h>
#include "can_driver.h"
#include "motion_hal.h"

#define NODE_ID          2U
#define RPDO1_COBID      (0x200U + NODE_ID)  /* 0x202 */
#define TPDO1_COBID      (0x180U + NODE_ID)  /* 0x182 */

/* Setpoint double buffer */
typedef struct {
    int32_t  pos;
    int16_t  vel;
    uint16_t ctrl;
} __attribute__((packed)) setpoint_t;

static setpoint_t rx_shadow   = {0};    /* written by CAN ISR          */
static setpoint_t rx_active   = {0};    /* read by servo loop          */
static volatile bool shadow_valid = false;

/* Feedback, assembled for TPDO */
typedef struct {
    int32_t  pos_actual;
    int16_t  vel_actual;
    uint16_t status;
} __attribute__((packed)) feedback_t;

/* ── CAN RX (ISR) ─────────────────────────────────────────────────────── */
void drive_on_can_rx(const can_frame_t *f)
{
    if (f->id != RPDO1_COBID) return;
    if (f->dlc < sizeof(setpoint_t)) return;
    memcpy(&rx_shadow, f->data, sizeof(setpoint_t));
    shadow_valid = true;
    /* Do NOT use data yet — wait for SYNC commit */
}

/* ── SYNC callback (ISR or high-priority task) ────────────────────────── */
void drive_on_sync(uint8_t counter)
{
    (void)counter;  /* Not used in this example (trans_type = 0x01) */

    /* 1. Commit received setpoint */
    if (shadow_valid) {
        rx_active    = rx_shadow;
        shadow_valid = false;
    }

    /* 2. Apply setpoint to servo controller */
    motion_hal_set_position_target(rx_active.pos);
    motion_hal_set_velocity_ff(rx_active.vel);

    /* 3. Read back actual values */
    feedback_t fb;
    fb.pos_actual = motion_hal_get_position();
    fb.vel_actual = motion_hal_get_velocity();
    fb.status     = build_status_word();

    /* 4. Transmit TPDO within synchronous window */
    can_frame_t frame;
    frame.id  = TPDO1_COBID;
    frame.dlc = sizeof(feedback_t);
    memcpy(frame.data, &fb, sizeof(feedback_t));
    can_transmit_highprio(&frame);
}
```

### 8.5 Coordinated 3-Axis Move — Timing Overview

```
   Goal: move X+Y+Z simultaneously from (0,0,0) mm to (10,20,5) mm in 10 ms
   SYNC period = 1 ms  →  10 SYNC cycles

   Cycle:   0    1    2    3    4    5    6    7    8    9   10
   ─────────────────────────────────────────────────────────────
   X (mm):  0.0  1.0  2.0  3.0  4.0  5.0  6.0  7.0  8.0  9.0 10.0
   Y (mm):  0.0  2.0  4.0  6.0  8.0 10.0 12.0 14.0 16.0 18.0 20.0
   Z (mm):  0.0  0.5  1.0  1.5  2.0  2.5  3.0  3.5  4.0  4.5  5.0
   ─────────────────────────────────────────────────────────────
   All three axes complete motion at cycle 10.
   At every cycle the SYNC triggers simultaneous application.

   Bus load per cycle (1 Mbit/s, 8-byte PDO ~ 130 µs):
   SYNC(0B)+RPDO-X+RPDO-Y+RPDO-Z+TPDO-X+TPDO-Y+TPDO-Z
   = ~47µs + 6×130µs = ~827 µs of 1000 µs  → 83% bus load
   → reduce to 500 kbit/s or use PDO multiplexing above 4 axes.
```

---

## 9. Error Handling and Edge Cases

### 9.1 Lost SYNC Detection

SYNC consumers should run a watchdog timer. If no SYNC arrives within
`2 × cycle_period`, the node shall transition to a safe state.

```c
/*
 * sync_watchdog.c
 * Detects loss of SYNC and triggers safe state.
 */

#include <stdint.h>
#include "timer_driver.h"
#include "nmt_state_machine.h"

#define SYNC_PERIOD_US        1000U
#define SYNC_TIMEOUT_FACTOR   2U

static volatile uint32_t watchdog_counter_us = 0U;
static bool sync_lost = false;

/* Called by 100 µs tick timer */
void sync_watchdog_tick(void)
{
    watchdog_counter_us += 100U;
    if (watchdog_counter_us > (SYNC_PERIOD_US * SYNC_TIMEOUT_FACTOR)) {
        if (!sync_lost) {
            sync_lost = true;
            /* Emit EMCY: SYNC lost */
            emcy_send(0x8100U, 0x10U);
            /* Enter Pre-Operational or execute safety ramp-down */
            nmt_enter_pre_operational();
        }
    }
}

/* Called when SYNC is received */
void sync_watchdog_reset(void)
{
    watchdog_counter_us = 0U;
    sync_lost           = false;
}
```

### 9.2 SYNC Counter Discontinuity

```
   Expected sequence:   1  2  3  4  1  2  3  4
   Received sequence:   1  2  4  1  2  3  4     ← counter=3 missing!

   Action:
   ┌─────────────────────────────────────────────────────────┐
   │ 1. Log event (missed SYNC count++)                      │
   │ 2. If missed_count > threshold: EMCY 0x8100, subcode 2  │
   │ 3. Reset double-buffer to prevent stale data commit     │
   │ 4. Continue with next received counter value            │
   └─────────────────────────────────────────────────────────┘
```

### 9.3 Common Configuration Pitfalls

```
   PITFALL 1: Two SYNC producers on the same network
   ──────────────────────────────────────────────────
   Both send 0x080 → CAN arbitration collision → bus errors
   Fix: Ensure only ONE node has bit 31 of 0x1005 set.

   PITFALL 2: SYNC window too short
   ─────────────────────────────────
   Window = 100 µs, 8 PDOs at 1 Mbit/s ≈ 1040 µs total
   → PDOs 8+ all discarded as "late"
   Fix: window ≥ numPDOs × frame_time_us × 1.2 (20% margin)

   PITFALL 3: SYNC period not a multiple of application cycle
   ──────────────────────────────────────────────────────────
   App servo loop = 2 ms, SYNC = 1 ms
   → every other SYNC fires with stale data
   Fix: set SYNC period = servo loop period, or use trans_type=2.

   PITFALL 4: Changing 0x1006 while Operational
   ─────────────────────────────────────────────
   Timer reload races with ISR → jitter or double-fire
   Fix: Always change period in Pre-Operational state.
```

---

## 10. Summary

The **SYNC object** (COB-ID 0x080) is the synchronisation backbone of CANopen
motion and process control networks. Its design is deliberately minimal — zero or
one data bytes — to minimise CAN bus occupancy while acting as a global clock edge
for all nodes simultaneously.

### Key Points at a Glance

```
   ┌─────────────────────────────────────────────────────────────────────┐
   │                    SYNC Object — Quick Reference                    │
   ├──────────────────────────┬──────────────────────────────────────────┤
   │ COB-ID                   │ 0x080 (default, configurable via 0x1005) │
   │ DLC                      │ 0 (v4.0) or 1 (v4.2+ with counter)       │
   │ Counter range            │ 1 … overflow_value (OD 0x1019)           │
   │ Period register          │ OD 0x1006 (microseconds)                 │
   │ Window register          │ OD 0x1007 (microseconds)                 │
   │ PDO trans. types         │ 0x01–0xF0 cyclic, 0x00 acyclic sync      │
   │ Producer flag            │ Bit 31 of OD 0x1005                      │
   │ Typical period           │ 1 ms (motion), 10 ms (process I/O)       │
   │ Max nodes on 1 Mbit/s    │ ~4 axes at 1 ms with 8-byte PDOs         │
   └──────────────────────────┴──────────────────────────────────────────┘
```

### Design Checklist

```
   □  Only one node has OD 0x1005 bit 31 set (SYNC producer)
   □  OD 0x1006 set to desired period in µs (0 = disabled)
   □  OD 0x1019 set to desired counter overflow (0 = no counter)
   □  All synchronous PDO trans. types (0x1800/0x1400 sub 2) configured
   □  OD 0x1007 synchronous window ≥ numSyncPDOs × ~130 µs (at 1 Mbit/s)
   □  Double-buffering implemented in all RPDO/TPDO handlers
   □  SYNC watchdog implemented on all consumer nodes
   □  SYNC period changed only in NMT Pre-Operational state
   □  SYNC counter discontinuity detection implemented if counter enabled
   □  Bus load analysed: SYNC + all sync PDOs ≤ 70–80% of cycle period
```

### Relationship to Other CANopen Objects

```
   NMT Operational
        │
        ├──► SYNC Producer (0x1005, 0x1006, 0x1019)
        │         │
        │         │ 0x080 broadcast every 1 ms
        │         ▼
        ├──► SYNC Consumer
        │         │
        │    ┌────┴────────────────────┐
        │    │ Synchronous RPDO apply  │  ◄── setpoints latch
        │    │ Synchronous TPDO send   │  ──► feedback sent
        │    │ Interpolation step      │
        │    └─────────────────────────┘
        │
        ├──► PDO Manager (0x1400–0x1BFF)  ← trans. type controls firing
        ├──► Emergency (0x1014)           ← SYNC loss → 0x8100 EMCY
        └──► Heartbeat / Guarding        ← independent, not SYNC-tied
```

The SYNC mechanism, when correctly implemented with double-buffering,
watchdog supervision, and careful bus-load analysis, provides the
deterministic, jitter-free synchronisation that is essential for
multi-axis coordinated motion, high-precision data acquisition,
and any CANopen application where temporal coherence between
distributed nodes is required.

---

*Document conforms to CiA 301 v4.2 — CANopen Application Layer and Communication Profile.*  
*Code examples are illustrative; adapt interrupt/timer APIs to your target platform.*