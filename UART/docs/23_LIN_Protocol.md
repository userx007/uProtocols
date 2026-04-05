# 23. LIN Protocol — Local Interconnect Network

**Architecture & Theory**
- LIN's position relative to UART and CAN — why it exists and where it fits
- Full frame structure: Break → Sync → PID → Data → Checksum
- Bus topology, electrical layer, and the open-drain single-wire physical model
- Master/slave roles, schedule tables, Protected ID parity math, and both checksum types (classic vs. enhanced)

**C/C++ Code**
- `lin_master.h/.c` — full master driver with `lin_compute_checksum()`, `lin_make_pid()`, `lin_master_send_frame()`, `lin_master_request_frame()`, and a schedule-table runner
- `lin_slave.c` — interrupt-driven slave state machine handling break detection, sync, PID validation, publish/subscribe dispatch, and checksum verification
- Break field generation via hardware SBK bit (STM32) and the baud-rate trick fallback

**Rust Code**
- `LinMaster<Serial, Delay, BreakFn>` — generic over `embedded-hal` 1.x traits, with `send_frame()` and `request_frame()` returning `Result`
- `LinSlave<WriteFn>` — enum-state machine for ISR-driven slave operation, with task registration for publish/subscribe IDs
- A typed `LinError` enum for structured error propagation

**Additional Topics**
- LIN 2.x diagnostics transport layer over IDs 0x3C/0x3D
- Sleep/wake-up protocol
- Conformance classes (LL, NM, TP, API)
- Error table covering all common failure modes

## Table of Contents

1. [Overview](#overview)
2. [LIN vs UART vs CAN](#lin-vs-uart-vs-can)
3. [LIN Frame Structure](#lin-frame-structure)
4. [LIN Bus Topology and Electrical Layer](#lin-bus-topology-and-electrical-layer)
5. [Master/Slave Architecture](#masterslave-architecture)
6. [Schedule Tables](#schedule-tables)
7. [LIN Identifier and Protected ID](#lin-identifier-and-protected-id)
8. [Checksum Types](#checksum-types)
9. [Programming in C/C++](#programming-in-cc)
   - [LIN Master Implementation](#lin-master-implementation-cc)
   - [LIN Slave Implementation](#lin-slave-implementation-cc)
   - [Bit-banged LIN on Bare Metal](#bit-banged-lin-on-bare-metal)
10. [Programming in Rust](#programming-in-rust)
    - [LIN Master in Rust](#lin-master-in-rust)
    - [LIN Slave in Rust](#lin-slave-in-rust)
11. [LIN Diagnostics (Transport Layer)](#lin-diagnostics-transport-layer)
12. [LIN Conformance Classes](#lin-conformance-classes)
13. [Common Use Cases and Examples](#common-use-cases-and-examples)
14. [Error Handling and Diagnostics](#error-handling-and-diagnostics)
15. [Summary](#summary)

---

## Overview

**LIN (Local Interconnect Network)** is a serial communications protocol designed for low-cost, low-speed communication in automotive and industrial embedded systems. Standardized as **ISO 17987** (and historically as SAE J2602), LIN was developed as a complement — not a replacement — to the CAN bus, targeting subsystems where CAN's full performance is unnecessary and cost must be minimized.

LIN is built directly on top of **UART** (Universal Asynchronous Receiver/Transmitter) physical principles, using:

- **Single-wire communication** at **1–20 kbps** (most commonly 9600 bps or 19200 bps)
- **One master, up to 16 slave nodes** on a single bus
- **No crystal oscillator required** in slave nodes (slaves can self-synchronize from the master's break field)
- **Simple, deterministic scheduling** driven entirely by the master

Typical LIN applications include:

- Window/sunroof motors
- Seat position controllers
- Mirror adjustment
- Climate control flaps
- Steering column switches
- Ambient lighting controllers
- Door handle/lock modules

---

## LIN vs UART vs CAN

| Feature | UART (raw) | LIN | CAN |
|---|---|---|---|
| Topology | Point-to-point | Single-wire bus | Differential 2-wire bus |
| Nodes | 2 | 1 master + up to 16 slaves | Up to 110+ |
| Speed | Up to ~5 Mbps | 1–20 kbps | Up to 1 Mbps (CAN FD: 8 Mbps) |
| Arbitration | None | None (master-only) | CSMA/CD bitwise |
| Clock sync | External crystal | Break + sync field (auto-baud) | External crystal required |
| Error detection | Parity (optional) | Checksum + parity | CRC, ACK, bit stuffing |
| Cost | Very low | Very low | Moderate |
| Standards | RS-232/422/485 | ISO 17987 / SAE J2602 | ISO 11898 |

LIN is essentially a **structured, framed, scheduled UART protocol** with a defined physical layer (single-wire, open-drain, pulled up to battery voltage ~12V through 1 kΩ).

---

## LIN Frame Structure

A LIN frame consists of:

```
| BREAK | SYNC | Protected ID (PID) | DATA[0..7] | CHECKSUM |
```

### Break Field

The **break field** marks the start of every LIN frame. It is a dominant (LOW) condition held for at least **13 bit times** (compared to the normal 1-bit start bit of UART). This is the only way a slave can detect a new frame without an external clock.

```
Normal UART bit time = 1/baud
Break = ≥ 13 × bit_time (dominant / LOW)
Break Delimiter = ≥ 1 × bit_time (recessive / HIGH)
```

### Sync Field

Fixed value **0x55** (binary: `01010101`). The alternating pattern allows each slave to precisely measure the master's current baud rate and calibrate its internal oscillator — this is the **auto-baud** mechanism enabling crystal-free slaves.

### Protected Identifier (PID)

6-bit frame identifier (0x00–0x3F) + 2 parity bits (P0, P1) packed into 1 byte. The 6-bit ID indicates:
- **What** this frame is about (e.g., "seat position", "window status")
- **How many data bytes** follow (determined by ID range: IDs 0x00–0x1F → 2 bytes; 0x20–0x2F → 4 bytes; 0x30–0x3F → 8 bytes in LIN 1.x; configurable in LIN 2.x)

### Data Field

1 to 8 bytes of payload. Byte order is **little-endian** (LSB first within signals).

### Checksum

1 byte calculated over the data (LIN 1.x "classic" checksum) or over PID + data (LIN 2.x "enhanced" checksum). The checksum is the **inverted modulo-256 sum** with carry.

---

## LIN Bus Topology and Electrical Layer

```
Battery +12V
     |
    1kΩ (Master pull-up)
     |
     +-----+--------+--------+--------+
           |        |        |        |
        [Master]  [Slave1] [Slave2] [SlaveN]
           |        |        |        |
          LIN transceiver (open-drain output)
           |
          GND
```

- **Recessive** (HIGH, ~12V): all nodes outputting high-impedance
- **Dominant** (LOW, ~0V): any node pulling the bus to ground (open-drain/collector wired-OR)
- **Master** has a 1 kΩ pull-up to battery; each **slave** typically has a 30 kΩ pull-up
- Common transceiver ICs: TJA1020, MC33661, TLE7259

---

## Master/Slave Architecture

### Master Node Responsibilities

1. Send the **break + sync + PID** header for each frame slot
2. Maintain the **schedule table** (timing of all frame transmissions)
3. Either publish (send) data in some frames or receive (subscribe) data in others
4. Optionally act as a **gateway** to CAN

### Slave Node Responsibilities

1. Detect the **break** field to wake from sleep
2. Synchronize baud rate on the **sync** byte (0x55)
3. Read the **PID** and decide whether to:
   - **Publish**: transmit data bytes + checksum onto the bus
   - **Subscribe**: receive data bytes + checksum from the bus
   - **Ignore**: take no action

Each slave has a **Slave Task** configured for each ID it cares about — whether to publish or subscribe, and which signals map to which bytes.

---

## Schedule Tables

The master cycles through a fixed **schedule table** — a list of (PID, period) pairs. The master fires each frame header at the appropriate time; slaves respond autonomously.

```
Example schedule (10 ms tick):
  t=0ms:   Send header for PID 0x01 (window position query)
  t=5ms:   Send header for PID 0x02 (motor command publish)
  t=10ms:  Send header for PID 0x03 (status query)
  t=15ms:  Send header for PID 0x10 (seat position)
  t=20ms:  → repeat
```

LIN 2.x also defines **event-triggered frames** and **sporadic frames** to allow some flexibility within the deterministic schedule.

---

## LIN Identifier and Protected ID

```
PID byte = [P1 | P0 | ID5 | ID4 | ID3 | ID2 | ID1 | ID0]

P0 = ID0 ^ ID1 ^ ID2 ^ ID4   (XOR)
P1 = ~(ID1 ^ ID3 ^ ID4 ^ ID5) (inverted XOR)
```

Example for ID = 0x12 (binary `010010`):
```
ID0=0 ID1=1 ID2=0 ID3=0 ID4=1 ID5=0
P0 = 0^1^0^1 = 0
P1 = ~(1^0^1^0) = ~0 = 1
PID = 1 0 010010 = 0x92
```

---

## Checksum Types

### Classic Checksum (LIN 1.x)

Computed over **data bytes only**:

```
sum = data[0] + data[1] + ... + data[n-1]  (with carry into sum)
checksum = (~sum) & 0xFF
```

### Enhanced Checksum (LIN 2.x)

Computed over **PID + data bytes**:

```
sum = PID + data[0] + data[1] + ... + data[n-1]  (with carry)
checksum = (~sum) & 0xFF
```

> **Note:** IDs 0x3C (60) and 0x3D (61) are reserved for diagnostics and always use the **classic** checksum even in LIN 2.x.

---

## Programming in C/C++

### LIN Master Implementation (C/C++)

This example targets a microcontroller (e.g., STM32, NXP S32K) with a hardware UART and a GPIO for the LIN break generation or a dedicated LIN controller.

```c
/*
 * lin_master.h — LIN 2.x Master driver (bare-metal C)
 * Assumes: HAL UART, 9600 baud, UART mapped to LIN transceiver
 */

#ifndef LIN_MASTER_H
#define LIN_MASTER_H

#include <stdint.h>
#include <stdbool.h>

#define LIN_SYNC_BYTE       0x55U
#define LIN_BREAK_BITS      13      /* min dominant bit times */
#define LIN_MAX_DATA_BYTES  8

/* Checksum variant */
typedef enum {
    LIN_CHECKSUM_CLASSIC  = 0,   /* LIN 1.x: over data only        */
    LIN_CHECKSUM_ENHANCED = 1    /* LIN 2.x: over PID + data        */
} lin_checksum_t;

/* Frame descriptor */
typedef struct {
    uint8_t         id;          /* 6-bit LIN ID (0x00–0x3F)        */
    uint8_t         data[LIN_MAX_DATA_BYTES];
    uint8_t         length;      /* Number of data bytes (1–8)       */
    lin_checksum_t  checksum_type;
    bool            is_publish;  /* true = master sends data         */
} lin_frame_t;

/* Platform callbacks (implement per MCU) */
typedef struct {
    void    (*send_break)(void);          /* Drive bus LOW ≥13 bit-times */
    void    (*uart_send)(uint8_t byte);   /* Blocking single-byte TX     */
    bool    (*uart_recv)(uint8_t *byte, uint32_t timeout_ms); /* RX      */
    void    (*delay_us)(uint32_t us);
} lin_driver_t;

#endif /* LIN_MASTER_H */
```

```c
/*
 * lin_master.c — LIN 2.x Master implementation
 */

#include "lin_master.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Checksum                                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Compute LIN checksum (classic or enhanced).
 *
 * Carries are folded back into the 8-bit sum ("inverted sum with carry").
 */
static uint8_t lin_compute_checksum(uint8_t pid,
                                    const uint8_t *data,
                                    uint8_t length,
                                    lin_checksum_t type)
{
    uint16_t sum = 0;

    if (type == LIN_CHECKSUM_ENHANCED) {
        sum += pid;          /* Include PID for LIN 2.x enhanced */
        if (sum > 0xFF) sum -= 0xFF;
    }

    for (uint8_t i = 0; i < length; i++) {
        sum += data[i];
        if (sum > 0xFF) sum -= 0xFF;
    }

    return (uint8_t)(~sum & 0xFF);
}

/* ------------------------------------------------------------------ */
/* Protected ID                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Calculate the Protected ID byte from a 6-bit LIN frame ID.
 *
 * P0 = ID0 ^ ID1 ^ ID2 ^ ID4
 * P1 = ~(ID1 ^ ID3 ^ ID4 ^ ID5)
 */
uint8_t lin_make_pid(uint8_t id)
{
    id &= 0x3F;  /* Mask to 6 bits */

    uint8_t p0 = ((id >> 0) ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 0x01;
    uint8_t p1 = (~((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5))) & 0x01;

    return (uint8_t)(id | (p0 << 6) | (p1 << 7));
}

/* ------------------------------------------------------------------ */
/* Frame transmission (Master → Bus)                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Send a complete LIN frame as master publisher.
 *
 * Sequence: BREAK → SYNC → PID → DATA[0..n-1] → CHECKSUM
 */
bool lin_master_send_frame(const lin_driver_t *drv,
                           const lin_frame_t  *frame)
{
    /* 1. Break field */
    drv->send_break();

    /* 2. Sync byte */
    drv->uart_send(LIN_SYNC_BYTE);

    /* 3. Protected ID */
    uint8_t pid = lin_make_pid(frame->id);
    drv->uart_send(pid);

    if (!frame->is_publish) {
        /* Header-only: slave(s) will respond */
        return true;
    }

    /* 4. Data bytes */
    for (uint8_t i = 0; i < frame->length; i++) {
        drv->uart_send(frame->data[i]);
    }

    /* 5. Checksum */
    uint8_t chk = lin_compute_checksum(pid, frame->data,
                                       frame->length,
                                       frame->checksum_type);
    drv->uart_send(chk);

    return true;
}

/* ------------------------------------------------------------------ */
/* Frame reception (Master reads slave response)                       */
/* ------------------------------------------------------------------ */

/**
 * @brief Send LIN header then receive slave's data + checksum.
 *
 * @param[out] frame   frame->data[] filled with received bytes
 * @return true if checksum valid, false on timeout or checksum error
 */
bool lin_master_request_frame(const lin_driver_t *drv,
                               lin_frame_t        *frame)
{
    /* Send header: break + sync + PID */
    drv->send_break();
    drv->uart_send(LIN_SYNC_BYTE);
    uint8_t pid = lin_make_pid(frame->id);
    drv->uart_send(pid);

    /* Receive data bytes */
    for (uint8_t i = 0; i < frame->length; i++) {
        if (!drv->uart_recv(&frame->data[i], 5 /* ms */)) {
            return false;   /* Timeout */
        }
    }

    /* Receive checksum */
    uint8_t rx_chk;
    if (!drv->uart_recv(&rx_chk, 5)) {
        return false;
    }

    /* Verify checksum */
    uint8_t calc_chk = lin_compute_checksum(pid, frame->data,
                                            frame->length,
                                            frame->checksum_type);
    return (rx_chk == calc_chk);
}

/* ------------------------------------------------------------------ */
/* Schedule table execution                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    lin_frame_t *frame;
    uint32_t     period_ms;
    uint32_t     _next_tick;
} lin_schedule_entry_t;

/**
 * @brief Run one iteration of the LIN schedule table.
 *
 * Call this function from a 1 ms periodic task or RTOS tick.
 *
 * @param entries   Array of schedule entries
 * @param count     Number of entries
 * @param now_ms    Current system tick in milliseconds
 * @param drv       Platform driver callbacks
 */
void lin_schedule_run(lin_schedule_entry_t *entries,
                      uint8_t               count,
                      uint32_t              now_ms,
                      const lin_driver_t   *drv)
{
    for (uint8_t i = 0; i < count; i++) {
        if (now_ms >= entries[i]._next_tick) {
            entries[i]._next_tick = now_ms + entries[i].period_ms;

            if (entries[i].frame->is_publish) {
                lin_master_send_frame(drv, entries[i].frame);
            } else {
                lin_master_request_frame(drv, entries[i].frame);
            }
        }
    }
}
```

---

### LIN Slave Implementation (C/C++)

```c
/*
 * lin_slave.c — LIN 2.x Slave node implementation
 *
 * Interrupt-driven RX, state machine approach.
 * Assumes: UART RX interrupt enabled, break detection via framing error
 *          or dedicated break interrupt.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define LIN_MAX_DATA_BYTES  8

/* Slave receive state machine */
typedef enum {
    LIN_STATE_IDLE = 0,
    LIN_STATE_SYNC,
    LIN_STATE_PID,
    LIN_STATE_DATA,
    LIN_STATE_CHECKSUM,
} lin_slave_state_t;

/* Slave response configuration (set at init for each ID) */
typedef struct {
    uint8_t  length;          /* Payload length in bytes             */
    bool     is_publisher;    /* true: slave sends data; false: RX   */
    uint8_t  tx_data[LIN_MAX_DATA_BYTES]; /* Data to publish          */
    void   (*on_receive)(uint8_t id,
                         const uint8_t *data,
                         uint8_t length); /* Callback for subscribed IDs */
} lin_slave_task_t;

/* Runtime state */
static struct {
    lin_slave_state_t   state;
    uint8_t             rx_buf[LIN_MAX_DATA_BYTES];
    uint8_t             rx_count;
    uint8_t             current_pid;
    uint8_t             expected_length;
    bool                is_publisher;
    lin_slave_task_t   *current_task;
} g_slave;

/* Table of slave tasks indexed by 6-bit ID */
static lin_slave_task_t *g_slave_tasks[64];  /* NULL = ignore */

/* Forward declarations for platform layer */
extern void     lin_platform_send_byte(uint8_t b);
extern uint8_t  lin_platform_checksum_type(uint8_t id); /* 0=classic,1=enhanced */

/* ------------------------------------------------------------------ */

static uint8_t compute_chk(uint8_t pid, const uint8_t *d,
                            uint8_t len, bool enhanced)
{
    uint16_t s = enhanced ? pid : 0;
    if (enhanced && s > 0xFF) s -= 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        s += d[i];
        if (s > 0xFF) s -= 0xFF;
    }
    return (uint8_t)(~s & 0xFF);
}

static bool verify_pid_parity(uint8_t pid)
{
    uint8_t id = pid & 0x3F;
    uint8_t p0_calc = ((id>>0)^(id>>1)^(id>>2)^(id>>4)) & 1;
    uint8_t p1_calc = (~((id>>1)^(id>>3)^(id>>4)^(id>>5))) & 1;
    uint8_t p0_rx   = (pid >> 6) & 1;
    uint8_t p1_rx   = (pid >> 7) & 1;
    return (p0_calc == p0_rx) && (p1_calc == p1_rx);
}

/**
 * @brief Called from UART break/framing-error ISR.
 *
 * A break field resets the state machine to expect SYNC next.
 */
void lin_slave_on_break(void)
{
    g_slave.state    = LIN_STATE_SYNC;
    g_slave.rx_count = 0;
}

/**
 * @brief Called from UART RX byte ISR for every received byte.
 */
void lin_slave_on_byte(uint8_t byte)
{
    switch (g_slave.state) {

    case LIN_STATE_SYNC:
        if (byte == 0x55) {
            /* Auto-baud calibration would happen here on RC oscillator */
            g_slave.state = LIN_STATE_PID;
        } else {
            g_slave.state = LIN_STATE_IDLE; /* Framing error */
        }
        break;

    case LIN_STATE_PID:
        if (!verify_pid_parity(byte)) {
            g_slave.state = LIN_STATE_IDLE;
            break;
        }
        g_slave.current_pid = byte;
        {
            uint8_t id = byte & 0x3F;
            lin_slave_task_t *task = g_slave_tasks[id];

            if (task == NULL) {
                g_slave.state = LIN_STATE_IDLE;  /* Not our frame */
                break;
            }

            g_slave.current_task     = task;
            g_slave.expected_length  = task->length;
            g_slave.is_publisher     = task->is_publisher;
            g_slave.rx_count         = 0;

            if (task->is_publisher) {
                /* Transmit response immediately after PID */
                for (uint8_t i = 0; i < task->length; i++) {
                    lin_platform_send_byte(task->tx_data[i]);
                }
                bool enhanced =
                    (lin_platform_checksum_type(id) == 1);
                uint8_t chk = compute_chk(byte, task->tx_data,
                                          task->length, enhanced);
                lin_platform_send_byte(chk);
                g_slave.state = LIN_STATE_IDLE;
            } else {
                g_slave.state = LIN_STATE_DATA;
            }
        }
        break;

    case LIN_STATE_DATA:
        g_slave.rx_buf[g_slave.rx_count++] = byte;
        if (g_slave.rx_count >= g_slave.expected_length) {
            g_slave.state = LIN_STATE_CHECKSUM;
        }
        break;

    case LIN_STATE_CHECKSUM: {
        uint8_t id       = g_slave.current_pid & 0x3F;
        bool    enhanced = (lin_platform_checksum_type(id) == 1);
        uint8_t expected = compute_chk(g_slave.current_pid,
                                       g_slave.rx_buf,
                                       g_slave.expected_length,
                                       enhanced);
        if (byte == expected && g_slave.current_task->on_receive) {
            g_slave.current_task->on_receive(
                id, g_slave.rx_buf, g_slave.expected_length);
        }
        g_slave.state = LIN_STATE_IDLE;
        break;
    }

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Application usage example                                           */
/* ------------------------------------------------------------------ */

static lin_slave_task_t task_window_cmd;
static lin_slave_task_t task_window_status;

static void on_window_command(uint8_t id,
                               const uint8_t *data, uint8_t len)
{
    (void)id; (void)len;
    uint8_t position_cmd = data[0];
    uint8_t speed        = data[1];
    /* Apply motor command: position_cmd, speed */
    (void)position_cmd; (void)speed;
}

void lin_slave_app_init(void)
{
    /* Frame 0x01: Master publishes motor commands, slave subscribes */
    task_window_cmd.length       = 2;
    task_window_cmd.is_publisher = false;
    task_window_cmd.on_receive   = on_window_command;
    g_slave_tasks[0x01]          = &task_window_cmd;

    /* Frame 0x02: Slave publishes position status */
    task_window_status.length       = 4;
    task_window_status.is_publisher = true;
    task_window_status.tx_data[0]   = 0x80; /* Position 128 */
    task_window_status.tx_data[1]   = 0x00; /* Flags        */
    task_window_status.tx_data[2]   = 0x01; /* State: moving */
    task_window_status.tx_data[3]   = 0x00; /* Reserved     */
    g_slave_tasks[0x02]             = &task_window_status;
}
```

---

### Bit-banged LIN on Bare Metal

When using a standard UART without break-generation hardware, the break field must be generated manually by briefly dropping the baud rate:

```c
/*
 * STM32 HAL example: generate LIN break by temporarily
 * reconfiguring UART to 1/13th of operating baud rate.
 *
 * At 9600 baud → break at 9600/13 ≈ 738 baud for one bit
 *             → that one "bit" appears as 13 bit-times at 9600 baud.
 */
static void lin_send_break_stm32(UART_HandleTypeDef *huart,
                                  uint32_t baud)
{
    /* Step 1: reconfigure to break baud (baud / 13) */
    huart->Init.BaudRate = baud / 13;
    HAL_UART_Init(huart);

    /* Step 2: send 0x00 — this produces a 13-bit dominant */
    uint8_t brk = 0x00;
    HAL_UART_Transmit(huart, &brk, 1, HAL_MAX_DELAY);

    /* Step 3: restore normal baud */
    huart->Init.BaudRate = baud;
    HAL_UART_Init(huart);
}
```

Alternatively, many MCU UARTs have a dedicated **LIN break** register bit (e.g., STM32 `USART_CR1_SBK`, NXP LPUART `CTRL[SBK]`):

```c
/* STM32 USART: send break using hardware SBK bit */
static void lin_send_break_hw(USART_TypeDef *USARTx)
{
    /* Set Send Break Request — cleared automatically after break TX */
    USARTx->RQR |= USART_RQR_SBKRQ;
    /* Wait until break transmitted */
    while (USARTx->RQR & USART_RQR_SBKRQ) {}
}
```

---

## Programming in Rust

Rust's `embedded-hal` and `nb` (non-blocking) ecosystem makes LIN driver development ergonomic and type-safe.

### LIN Master in Rust

```rust
// lin_master.rs — LIN 2.x Master driver for embedded Rust
// Requires: embedded-hal 1.x, embedded-hal-nb

use embedded_hal::delay::DelayNs;
use embedded_hal_nb::serial::{Read, Write};
use nb::block;

/// LIN checksum variant
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ChecksumType {
    Classic,   // LIN 1.x — over data bytes only
    Enhanced,  // LIN 2.x — over PID + data bytes
}

/// A LIN frame descriptor
#[derive(Debug, Clone)]
pub struct LinFrame {
    pub id: u8,                    // 6-bit LIN ID (0x00–0x3F)
    pub data: [u8; 8],
    pub length: usize,
    pub checksum_type: ChecksumType,
}

impl LinFrame {
    pub fn new(id: u8, length: usize, checksum_type: ChecksumType) -> Self {
        assert!(id <= 0x3F, "LIN ID must be 6-bit (0x00–0x3F)");
        assert!((1..=8).contains(&length), "Data length must be 1–8");
        Self {
            id,
            data: [0u8; 8],
            length,
            checksum_type,
        }
    }
}

/// Compute the Protected ID byte from a 6-bit LIN frame ID.
///
/// P0 = ID0 ^ ID1 ^ ID2 ^ ID4
/// P1 = ~(ID1 ^ ID3 ^ ID4 ^ ID5)
pub fn make_pid(id: u8) -> u8 {
    let id = id & 0x3F;
    let p0 = ((id >> 0) ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 0x01;
    let p1 = (!(( id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5))) & 0x01;
    id | (p0 << 6) | (p1 << 7)
}

/// Compute LIN checksum (with carry fold, then invert).
pub fn compute_checksum(pid: u8, data: &[u8], ctype: ChecksumType) -> u8 {
    let mut sum: u16 = if ctype == ChecksumType::Enhanced {
        let s = pid as u16;
        if s > 0xFF { s - 0xFF } else { s }
    } else {
        0
    };

    for &b in data {
        sum += b as u16;
        if sum > 0xFF { sum -= 0xFF; }
    }

    (!sum as u8) & 0xFF
}

/// LIN Master driver.
///
/// `Serial` must implement `embedded_hal_nb::serial::{Read, Write}<u8>`.
/// `Delay` must implement `embedded_hal::delay::DelayNs`.
/// `BreakFn` is a closure that drives the break field (platform-specific).
pub struct LinMaster<Serial, Delay, BreakFn>
where
    Serial: Read<u8> + Write<u8>,
    Delay: DelayNs,
    BreakFn: Fn(),
{
    serial: Serial,
    delay: Delay,
    send_break: BreakFn,
}

impl<Serial, Delay, BreakFn> LinMaster<Serial, Delay, BreakFn>
where
    Serial: Read<u8> + Write<u8>,
    Delay: DelayNs,
    BreakFn: Fn(),
{
    pub fn new(serial: Serial, delay: Delay, send_break: BreakFn) -> Self {
        Self { serial, delay, send_break }
    }

    /// Send a byte over UART (blocking).
    fn send_byte(&mut self, byte: u8) {
        block!(self.serial.write(byte)).ok();
    }

    /// Receive a byte with a simple spin-wait (non-production; use timeout).
    fn recv_byte(&mut self) -> Option<u8> {
        // In production, use a timeout loop or RTOS sleep
        for _ in 0..10_000u32 {
            if let Ok(b) = self.serial.read() {
                return Some(b);
            }
        }
        None
    }

    /// Transmit LIN header (break + sync + PID).
    fn send_header(&mut self, id: u8) -> u8 {
        (self.send_break)();
        self.send_byte(0x55);               // SYNC
        let pid = make_pid(id);
        self.send_byte(pid);
        pid
    }

    /// Send a complete LIN frame (master publishes data).
    pub fn send_frame(&mut self, frame: &LinFrame) -> bool {
        let pid = self.send_header(frame.id);

        for i in 0..frame.length {
            self.send_byte(frame.data[i]);
        }

        let chk = compute_checksum(
            pid,
            &frame.data[..frame.length],
            frame.checksum_type,
        );
        self.send_byte(chk);
        true
    }

    /// Request a LIN frame (slave publishes, master subscribes).
    ///
    /// Returns `Ok(())` on successful receipt and checksum validation.
    pub fn request_frame(&mut self, frame: &mut LinFrame) -> Result<(), &'static str> {
        let pid = self.send_header(frame.id);

        for i in 0..frame.length {
            frame.data[i] = self.recv_byte().ok_or("TIMEOUT")?;
        }

        let rx_chk = self.recv_byte().ok_or("TIMEOUT_CHK")?;
        let calc_chk = compute_checksum(
            pid,
            &frame.data[..frame.length],
            frame.checksum_type,
        );

        if rx_chk == calc_chk {
            Ok(())
        } else {
            Err("CHECKSUM_ERROR")
        }
    }
}

// -------------------------------------------------------------------------
// Example usage (e.g., in a main loop or RTOS task)
// -------------------------------------------------------------------------

// fn main_loop_example() {
//     // Assume `uart`, `delay`, and `send_break` are properly initialized
//     let mut master = LinMaster::new(uart, delay, || {
//         // Platform-specific: toggle GPIO or use UART SBK bit
//         send_break_hw();
//     });
//
//     // Publish: master sends seat motor command on ID 0x05
//     let mut cmd_frame = LinFrame::new(0x05, 2, ChecksumType::Enhanced);
//     cmd_frame.data[0] = 0x40;  // Target position
//     cmd_frame.data[1] = 0x03;  // Speed
//     master.send_frame(&cmd_frame);
//
//     // Subscribe: master requests window status from ID 0x06
//     let mut status_frame = LinFrame::new(0x06, 4, ChecksumType::Enhanced);
//     if master.request_frame(&mut status_frame).is_ok() {
//         let position = status_frame.data[0];
//         let flags    = status_frame.data[1];
//         // ... process response
//     }
// }
```

---

### LIN Slave in Rust

```rust
// lin_slave.rs — LIN 2.x Slave node, interrupt-safe state machine in Rust
//
// Designed for use with cortex-m RTIC or bare-metal with critical sections.

use core::sync::atomic::{AtomicU8, Ordering};

const LIN_MAX_DATA: usize = 8;

/// Slave receive state
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum SlaveState {
    Idle      = 0,
    WaitSync  = 1,
    WaitPid   = 2,
    RxData    = 3,
    RxChk     = 4,
}

/// Slave task: what does this node do for a given ID?
pub enum SlaveTask<'a> {
    /// Subscribe (receive): call callback when data arrives
    Subscribe {
        length: usize,
        on_recv: fn(id: u8, data: &[u8]),
    },
    /// Publish (transmit): send this data when ID is seen
    Publish {
        length: usize,
        data: &'a [u8; LIN_MAX_DATA],
        checksum_enhanced: bool,
    },
}

/// Interrupt-driven LIN slave context
pub struct LinSlave<'a, WriteFn>
where
    WriteFn: Fn(u8),
{
    state: SlaveState,
    rx_buf: [u8; LIN_MAX_DATA],
    rx_count: usize,
    current_pid: u8,
    current_len: usize,
    current_pub: bool,
    tasks: [Option<SlaveTask<'a>>; 64],  // Indexed by 6-bit ID
    write_byte: WriteFn,                 // Platform UART TX
}

impl<'a, WriteFn> LinSlave<'a, WriteFn>
where
    WriteFn: Fn(u8),
{
    pub fn new(write_byte: WriteFn) -> Self {
        // Cannot derive Default on arrays of non-Copy Option<SlaveTask>
        // Use unsafe to zero-initialize, then wrap in MaybeUninit if needed
        Self {
            state: SlaveState::Idle,
            rx_buf: [0u8; LIN_MAX_DATA],
            rx_count: 0,
            current_pid: 0,
            current_len: 0,
            current_pub: false,
            tasks: core::array::from_fn(|_| None),
            write_byte,
        }
    }

    /// Register a slave task for a given 6-bit LIN ID.
    pub fn register_task(&mut self, id: u8, task: SlaveTask<'a>) {
        assert!(id <= 0x3F);
        self.tasks[id as usize] = Some(task);
    }

    // --- Checksum (inline for ISR context) ---

    fn compute_chk(pid: u8, data: &[u8], enhanced: bool) -> u8 {
        let mut sum: u16 = if enhanced { pid as u16 } else { 0 };
        if sum > 0xFF { sum -= 0xFF; }
        for &b in data {
            sum += b as u16;
            if sum > 0xFF { sum -= 0xFF; }
        }
        (!(sum as u8)) & 0xFF
    }

    fn verify_pid(pid: u8) -> bool {
        let id = pid & 0x3F;
        let p0 = ((id>>0)^(id>>1)^(id>>2)^(id>>4)) & 1;
        let p1 = (!(( id>>1)^(id>>3)^(id>>4)^(id>>5))) & 1;
        (pid >> 6) & 1 == p0 && (pid >> 7) & 1 == p1
    }

    /// Called from the break detection ISR.
    pub fn on_break(&mut self) {
        self.state    = SlaveState::WaitSync;
        self.rx_count = 0;
    }

    /// Called from the UART RX ISR for each received byte.
    pub fn on_byte(&mut self, byte: u8) {
        match self.state {

            SlaveState::WaitSync => {
                if byte == 0x55 {
                    self.state = SlaveState::WaitPid;
                } else {
                    self.state = SlaveState::Idle;
                }
            }

            SlaveState::WaitPid => {
                if !Self::verify_pid(byte) {
                    self.state = SlaveState::Idle;
                    return;
                }
                self.current_pid = byte;
                let id = (byte & 0x3F) as usize;

                match &self.tasks[id] {
                    None => {
                        self.state = SlaveState::Idle;
                    }
                    Some(SlaveTask::Publish { length, data, checksum_enhanced }) => {
                        let len = *length;
                        let enh = *checksum_enhanced;
                        let data_copy = **data;
                        for i in 0..len {
                            (self.write_byte)(data_copy[i]);
                        }
                        let chk = Self::compute_chk(byte, &data_copy[..len], enh);
                        (self.write_byte)(chk);
                        self.state = SlaveState::Idle;
                    }
                    Some(SlaveTask::Subscribe { length, .. }) => {
                        self.current_len  = *length;
                        self.current_pub  = false;
                        self.rx_count     = 0;
                        self.state        = SlaveState::RxData;
                    }
                }
            }

            SlaveState::RxData => {
                self.rx_buf[self.rx_count] = byte;
                self.rx_count += 1;
                if self.rx_count >= self.current_len {
                    self.state = SlaveState::RxChk;
                }
            }

            SlaveState::RxChk => {
                let id  = (self.current_pid & 0x3F) as usize;
                // Enhanced for all IDs except 0x3C/0x3D (diagnostic)
                let enh = id != 0x3C && id != 0x3D;
                let expected = Self::compute_chk(
                    self.current_pid,
                    &self.rx_buf[..self.current_len],
                    enh,
                );
                if byte == expected {
                    if let Some(SlaveTask::Subscribe { on_recv, .. }) = &self.tasks[id] {
                        on_recv(id as u8, &self.rx_buf[..self.current_len]);
                    }
                }
                self.state = SlaveState::Idle;
            }

            SlaveState::Idle => {}
        }
    }
}

// -----------------------------------------------------------------------
// Application wiring example (pseudocode — fill in with your HAL)
// -----------------------------------------------------------------------

// static WINDOW_DATA: [u8; 8] = [0x80, 0x00, 0x01, 0x00, 0, 0, 0, 0];
//
// fn app_init(uart_tx: impl Fn(u8)) {
//     let mut slave = LinSlave::new(uart_tx);
//
//     // ID 0x01: Subscribe to motor commands
//     slave.register_task(0x01, SlaveTask::Subscribe {
//         length: 2,
//         on_recv: |id, data| {
//             // data[0] = position, data[1] = speed
//             apply_motor_command(data[0], data[1]);
//         },
//     });
//
//     // ID 0x02: Publish window status
//     slave.register_task(0x02, SlaveTask::Publish {
//         length: 4,
//         data: &WINDOW_DATA,
//         checksum_enhanced: true,
//     });
// }
```

---

## LIN Diagnostics (Transport Layer)

LIN 2.x defines a **transport layer** over frames with IDs **0x3C** (master request) and **0x3D** (slave response) for configuration and diagnostics, analogous to UDS over CAN (ISO 15765-2).

```
Frame 0x3C (master → slave, 8 bytes):
  Byte 0:    NAD  (Node Address — 1..127; 0x7F = broadcast)
  Byte 1:    PCI  (Protocol Control Info)
  Byte 2..7: Data (up to 6 bytes per single-frame, or segmented)

Frame 0x3D (slave → master, 8 bytes):
  Byte 0:    NAD  (responding slave's address)
  Byte 1:    PCI
  Byte 2..7: Data

PCI encoding:
  0x0N: Single Frame (N = length, 1..6)
  0x1N XX: First Frame of multi-frame (N+XX*256 = total length)
  0x2N:    Consecutive Frame (N = sequence counter)
  0x3N:    Last Frame
```

```c
/* Send a LIN single-frame diagnostic request (C example) */
void lin_send_diag_request(lin_driver_t *drv,
                            uint8_t       nad,
                            const uint8_t *payload,
                            uint8_t        length)
{
    lin_frame_t frame;
    frame.id             = 0x3C;
    frame.length         = 8;
    frame.checksum_type  = LIN_CHECKSUM_CLASSIC;  /* Always classic for 0x3C/0x3D */
    frame.is_publish     = true;

    frame.data[0] = nad;
    frame.data[1] = 0x00 | (length & 0x0F);  /* PCI: Single Frame */
    for (uint8_t i = 0; i < length && i < 6; i++) {
        frame.data[2 + i] = payload[i];
    }
    /* Pad remaining bytes */
    for (uint8_t i = length; i < 6; i++) {
        frame.data[2 + i] = 0xFF;
    }

    lin_master_send_frame(drv, &frame);
}
```

---

## LIN Conformance Classes

LIN 2.x defines node classes for compliance:

| Class | Description |
|---|---|
| **LL (Low-Level)** | Basic send/receive, no scheduling, no transport layer |
| **NM (Node Management)** | Go-to-sleep / wake-up handling |
| **TP (Transport Protocol)** | Full diagnostic transport layer (0x3C/0x3D) |
| **API** | Full LIN API as specified by LIN Consortium |

---

## Common Use Cases and Examples

### Window Regulator (Publish/Subscribe Pattern)

```c
/* Master schedule for window regulator subsystem */
lin_frame_t window_cmd    = { .id=0x10, .length=2,
                               .checksum_type=LIN_CHECKSUM_ENHANCED,
                               .is_publish=true };
lin_frame_t window_status = { .id=0x11, .length=4,
                               .checksum_type=LIN_CHECKSUM_ENHANCED,
                               .is_publish=false };

lin_schedule_entry_t schedule[] = {
    { &window_cmd,    10 },   /* Send motor command every 10 ms */
    { &window_status, 20 },   /* Request status every 20 ms     */
};

/* Update command data before schedule tick */
void set_window_position(uint8_t pos, uint8_t speed) {
    window_cmd.data[0] = pos;
    window_cmd.data[1] = speed;
}
```

### Sleep and Wake-Up

LIN defines a **go-to-sleep** command (ID 0x3C, NAD 0xFF, data 0x00 0xFF 0xFF ...) that puts all slaves into low-power mode. Any slave can initiate a **wake-up** pulse (a 250–5000 µs dominant pulse) to request the master to restart the schedule.

```c
void lin_send_sleep(lin_driver_t *drv) {
    lin_frame_t sleep_frame = {
        .id            = 0x3C,
        .length        = 8,
        .checksum_type = LIN_CHECKSUM_CLASSIC,
        .is_publish    = true
    };
    /* Go-to-sleep command: NAD=0xFF (all nodes), data[1..7]=0xFF */
    sleep_frame.data[0] = 0xFF;
    for (int i = 1; i < 8; i++) sleep_frame.data[i] = 0xFF;
    sleep_frame.data[1] = 0x00;  /* PCI: sleep request */

    lin_master_send_frame(drv, &sleep_frame);
}
```

---

## Error Handling and Diagnostics

| Error Type | Detection Method | Recovery |
|---|---|---|
| **Bit error** | TX loopback mismatch | Abort frame, log error |
| **Checksum error** | Slave detects mismatch | Discard frame, wait for next header |
| **Timeout / no response** | Master RX timeout after PID | Log missing response, continue schedule |
| **PID parity error** | Parity mismatch on PID byte | Slave ignores frame |
| **Sync error** | Sync byte ≠ 0x55 | Slave discards, re-arms on next break |
| **Break too short** | < 13 dominant bits | No break detected; frame missed |
| **Physical short** | Bus stuck LOW | All nodes silent; master detects idle timeout |

```rust
// Rust: error type for LIN master operations
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum LinError {
    Timeout,
    ChecksumMismatch { expected: u8, received: u8 },
    PidParityError,
    SyncError,
    BusOff,
}

// Usage in request_frame
fn request_frame_checked(master: &mut impl LinMasterTrait,
                          frame: &mut LinFrame) -> Result<(), LinError> {
    master.request_frame(frame).map_err(|e| match e {
        "TIMEOUT"        => LinError::Timeout,
        "TIMEOUT_CHK"    => LinError::Timeout,
        "CHECKSUM_ERROR" => LinError::ChecksumMismatch {
            expected: 0x00, // fill from context
            received: 0xFF,
        },
        _ => LinError::BusOff,
    })
}
```

---

## Summary

LIN (Local Interconnect Network) is a **single-wire, low-speed, master-slave serial protocol** built upon the same UART principles of asynchronous start-stop framing, but with a carefully defined structure for automotive and industrial subsystems.

**Key architectural points:**

- **Single master** controls the entire schedule; up to 16 slaves respond to frame headers
- **Break + sync + PID** header enables crystal-free slave nodes via auto-baud synchronization
- **Protected ID** encodes a 6-bit identifier with 2 parity bits, preventing ID corruption
- **Classic (LIN 1.x) and Enhanced (LIN 2.x) checksums** provide data integrity; diagnostic IDs 0x3C/0x3D always use classic
- **Schedule tables** make LIN fully deterministic — ideal for hard real-time body electronics

**Implementation guidance:**

- In **C/C++**, implement a break generation function (hardware SBK bit or baud-rate trick), followed by a header transmit function and a state-machine slave receive loop driven from the UART RX ISR
- In **Rust**, the `embedded-hal` trait abstractions allow type-safe, portable LIN drivers using closures for break generation and `nb::block!()` for synchronous UART operations; the slave state machine maps cleanly to a Rust enum with match arms
- Always handle **checksum validation** and **PID parity verification** before acting on received data
- For **diagnostics and node configuration**, implement the LIN transport layer over IDs 0x3C/0x3D (LIN 2.x) to support UDS-style service requests

LIN remains one of the most cost-effective solutions for sub-networks of simple actuators and sensors in automotive systems, where CAN's overhead is not justified. Its foundation on UART makes it highly approachable for embedded developers, with no special hardware required beyond a single-wire transceiver IC.