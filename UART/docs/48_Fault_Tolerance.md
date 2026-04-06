# 48. UART Fault Tolerance

- **Sources of faults** — electrical (EMI, ground offset, ESD), protocol-level (framing/overrun/parity errors), and system-level (deadlock, baud drift, desynchronization)
- **Hardware detection** — status register flags, parity modes, RTS/CTS flow control
- **Software strategies** — CRC-16 CCITT, message framing (SOH/LEN/CMD/DATA/CRC/EOT), sequence numbers, ACK/NACK with retry
- **Graceful degradation** — a 5-level degradation ladder (Full → Reduced Rate → Essential Only → Fallback Channel → Safe State) with a full state machine diagram
- **Watchdog & timeouts** — a 4-tier timeout hierarchy (byte → message → link → system watchdog)
- **Recovery** — UART peripheral reset sequence, baud rate auto-negotiation, BREAK-as-reset
- **Redundancy** — dual UART hot-standby, cross-bus data validation, heartbeat frames
- **Diagnostics** — error counter structs and fault record snapshots for NVRAM logging

**Code examples:**
- **C** — complete bare-metal UART context with ISR-driven RX state machine, CRC, retry, and fault manager
- **C++** — RAII wrapper with lambda callbacks, `std::optional`, and compile-time safety
- **Rust** — full `no_std` implementation with `Result`/`Option` error propagation, unit tests, plus an async version using `embassy-time` with exponential back-off

## Graceful Degradation and System Resilience Strategies

---

## Table of Contents

1. [Introduction](#introduction)
2. [Sources of UART Faults](#sources-of-uart-faults)
3. [Hardware-Level Fault Detection](#hardware-level-fault-detection)
4. [Software Error Detection Strategies](#software-error-detection-strategies)
5. [Graceful Degradation Patterns](#graceful-degradation-patterns)
6. [Watchdog and Timeout Management](#watchdog-and-timeout-management)
7. [Automatic Recovery and Reconnection](#automatic-recovery-and-reconnection)
8. [Redundancy and Fallback Communication](#redundancy-and-fallback-communication)
9. [Logging and Diagnostics](#logging-and-diagnostics)
10. [Implementation in C/C++](#implementation-in-cc)
11. [Implementation in Rust](#implementation-in-rust)
12. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) communication, while simple and widely deployed,
is inherently fragile in real-world environments. Unlike SPI or I²C which include clock signals,
UART relies on both sides independently maintaining the same baud rate, making it susceptible to
bit errors, framing failures, and data loss under electrical noise or timing drift.

**Fault tolerance** in UART systems refers to the design of hardware and software layers that:

- **Detect** errors quickly and unambiguously
- **Isolate** faulty conditions without crashing the system
- **Recover** automatically or with minimal operator intervention
- **Degrade gracefully** when full functionality cannot be restored

Fault tolerance is critical in embedded systems operating in industrial, automotive, medical,
and aerospace environments where a UART failure must not bring down an entire subsystem.

---

## Sources of UART Faults

Understanding what can go wrong is the foundation of fault-tolerant design.

### Electrical Faults

| Fault Type | Description | Symptom |
|---|---|---|
| Line noise | EMI coupling onto Tx/Rx lines | Bit errors, framing errors |
| Ground offset | Different GND potentials between devices | Persistent byte corruption |
| Signal reflection | Impedance mismatch on long cables | Data corruption at high baud rates |
| ESD damage | Electrostatic discharge on UART pins | Permanent hardware failure |
| Voltage level mismatch | 5V device driving 3.3V UART | Possible latch-up or corruption |

### Protocol-Level Faults

| Fault Type | Description | UART Flag |
|---|---|---|
| Framing error | Stop bit not detected at expected time | `FE` (Framing Error) |
| Overrun error | New byte arrived before previous byte was read | `OE` (Overrun Error) |
| Parity error | Parity bit mismatch | `PE` (Parity Error) |
| Break condition | Line held low for >1 frame | `BI` (Break Indicator) |
| Buffer overflow | Software RX buffer full | Application-level |

### System-Level Faults

- **Baud rate drift**: Clock source frequency variation between sender and receiver
- **Deadlock**: Both sides waiting for response while neither is transmitting
- **Partial messages**: Power loss or reset during transmission
- **Message loss**: Interrupts disabled for too long (overrun conditions)
- **Protocol desynchronization**: Loss of message framing in a higher-level protocol

---

## Hardware-Level Fault Detection

Modern UART peripherals expose status registers with error flags. Reading and acting on these
flags is the first line of defense.

### UART Status Register Flags (Generic)

```
Bit 7: Parity Error  (PE)
Bit 6: Framing Error (FE)
Bit 5: Overrun Error (OE)
Bit 4: Break Detect  (BD)
Bit 3: TX Complete   (TC)
Bit 2: RX Not Empty  (RXNE)
Bit 1: TX Empty      (TXE)
Bit 0: Line Idle     (IDLE)
```

### Parity Checking

Parity adds a single bit after each data byte to allow single-bit error detection:

- **Even parity**: Total 1-bits (data + parity) is even
- **Odd parity**: Total 1-bits is odd
- **Mark/Space parity**: Fixed 1 or 0 — used for addressing in multidrop systems

Parity alone cannot detect 2-bit (or any even-count) errors. For stronger guarantees,
application-level CRC is required.

### Hardware Flow Control (RTS/CTS)

Hardware flow control prevents overrun by signaling readiness:

```
Device A                    Device B
  RTS ──────────────────────► CTS   (A tells B it's ready to receive)
  CTS ◄────────────────────── RTS   (B tells A it's ready to receive)
  TX  ──────────────────────► RX
  RX  ◄────────────────────── TX
```

If the receiver asserts CTS low (not ready), the transmitter pauses. This is a hardware-
enforced backpressure mechanism — a form of fault prevention rather than fault recovery.

---

## Software Error Detection Strategies

### 1. Checksum and CRC

Every message should include a checksum so the receiver can verify integrity.

**Simple 8-bit additive checksum:**

```
Checksum = (sum of all data bytes) & 0xFF
```

**CRC-16 (CCITT)** — far stronger, detects all single/double-bit errors and burst errors
up to 16 bits:

```
Generator polynomial: x^16 + x^12 + x^5 + 1  (0x1021)
```

### 2. Message Framing

A well-designed protocol wraps data in identifiable delimiters:

```
| SOH (0x01) | LEN (1B) | CMD (1B) | DATA (N bytes) | CRC16 (2B) | EOT (0x04) |
```

This allows the receiver to:
- Detect where messages start and end
- Detect length mismatches
- Discard partial/corrupted messages
- Resynchronize after a data stream disruption

### 3. Sequence Numbers

Including a monotonically increasing sequence number per message allows detection of:
- Duplicate messages (retransmit received twice)
- Out-of-order delivery
- Message gaps (missed packets)

### 4. Acknowledgement (ACK/NACK) Protocol

A request-response scheme with timeout:

```
Sender:   [MSG seq=5] ──────────────────────► Receiver
Receiver:                                      [ACK seq=5] ──► Sender
Sender:   (timeout if no ACK within T ms)
          [MSG seq=5] ──────────────────────► Receiver  (retransmit)
```

After N retransmits without ACK, declare link failure and enter degraded mode.

---

## Graceful Degradation Patterns

Graceful degradation means the system continues operating at reduced capacity rather than
failing catastrophically.

### Degradation Levels

```
Level 0: FULL OPERATION
         All UART links healthy, full data rate, all features active.

Level 1: REDUCED RATE
         Baud rate lowered to improve noise immunity.
         Non-critical telemetry suspended.

Level 2: ESSENTIAL ONLY
         Only safety-critical messages transmitted.
         Heartbeat / watchdog messages maintained.
         Diagnostics logged locally.

Level 3: FALLBACK CHANNEL
         Primary UART failed; switch to redundant UART or alternate bus (I²C, CAN).
         Alert operator.

Level 4: SAFE STATE
         All UART communication failed.
         System enters safe, known-good hardware state.
         Actuators de-energized; await manual recovery.
```

### State Machine for Fault Tolerance

```
         ┌─────────────────────────────────────────┐
         │              NORMAL                      │
         │   All errors within threshold            │
         └───────────────┬─────────────────────────┘
                         │ Error rate > threshold OR
                         │ timeout expires
                         ▼
         ┌─────────────────────────────────────────┐
         │           DEGRADED                       │
         │   Reduced baud rate, essential msgs only │◄──┐
         └───────────────┬─────────────────────────┘   │
                         │ Recovery attempt fails        │ Recovery
                         │ N times                       │ succeeds
                         ▼                               │
         ┌─────────────────────────────────────────┐   │
         │           RECOVERY                       │───┘
         │   Reset UART peripheral                  │
         │   Clear buffers                          │
         │   Re-negotiate baud rate                 │
         └───────────────┬─────────────────────────┘
                         │ Recovery fails after M attempts
                         ▼
         ┌─────────────────────────────────────────┐
         │           SAFE STATE                     │
         │   Disable actuators                      │
         │   Alert operator                         │
         │   Log fault record                       │
         └─────────────────────────────────────────┘
```

---

## Watchdog and Timeout Management

### Communication Watchdog

A communication watchdog timer resets if a valid message is received within a deadline.
If it expires, the system assumes the remote device is silent or crashed.

```
Timer restarted on every valid received message
         │
         ▼
[============================] T_max
         │
         ▼
Timer expires → declare remote device DEAD → enter degraded mode
```

### Timeout Hierarchy

Well-designed systems use multiple timeout tiers:

| Tier | Timeout | Action |
|---|---|---|
| Byte timeout | ~10 ms | Flush partial frame; restart framing |
| Message timeout | ~100 ms | Retransmit last message |
| Link timeout | ~1 s | Declare link failed; attempt recovery |
| System watchdog | ~5 s | Hardware reset of subsystem |

### Inter-Character Timeout (ICT)

When receiving a multi-byte message, if a gap between bytes exceeds a threshold (e.g., 3.5
character times in Modbus), the message is considered complete or corrupted and the
receiver should flush its buffer and restart.

---

## Automatic Recovery and Reconnection

### UART Peripheral Reset Sequence

When software detects a stuck or erroneous UART state, it should perform a controlled reset:

1. **Disable** UART peripheral (stop all activity)
2. **Flush** TX and RX hardware FIFOs
3. **Clear** all error flags in the status register
4. **Wait** for line to return to idle (all 1s / MARK state)
5. **Re-enable** UART with same or adjusted configuration
6. **Send** a synchronization/reset message to the remote device
7. **Wait** for acknowledgement before resuming normal operation

### Baud Rate Auto-Negotiation

Some systems implement baud rate fallback: if communication fails at 115200, try 57600,
then 9600, until a connection is established.

```
Try 115200 → Timeout → Try 57600 → Timeout → Try 9600 → ACK received → Use 9600
```

### Line Break as Reset Signal

Sending a BREAK condition (line forced low for >1 frame period) is a universal "reset
framing" signal. Both sides can agree: upon receiving BREAK, discard all buffered data
and restart the protocol state machine.

---

## Redundancy and Fallback Communication

### Dual UART Links

Critical systems duplicate the UART path:

```
Primary MCU ──[UART0]──► Remote Device
Primary MCU ──[UART1]──► Remote Device   (standby, monitored for integrity)

On UART0 failure: switch to UART1 with zero data loss (if hot-standby)
```

### Cross-Checking with Alternate Bus

Where available, critical data can be cross-checked across buses:

```
UART:    temperature = 85°C
CAN:     temperature = 84°C   → Within tolerance → trust UART
CAN:     temperature = 120°C  → Outlier → declare UART fault
```

### Heartbeat Messages

Periodic heartbeat frames verify link liveness without requiring application-level data:

```
[HB | seq=42 | timestamp | CRC]  → sent every 100ms
```

Absence of heartbeat triggers link-down detection faster than waiting for a real message
timeout.

---

## Logging and Diagnostics

### Error Counters

Maintain persistent (non-volatile) error counters for post-mortem analysis:

```c
typedef struct {
    uint32_t framing_errors;
    uint32_t overrun_errors;
    uint32_t parity_errors;
    uint32_t crc_failures;
    uint32_t timeouts;
    uint32_t resets;
    uint32_t total_bytes_rx;
    uint32_t total_bytes_tx;
} uart_diagnostics_t;
```

### Fault Records

When a significant fault occurs, capture a snapshot:

```c
typedef struct {
    uint32_t timestamp_ms;
    uint8_t  fault_code;
    uint8_t  uart_status_reg;
    uint8_t  last_bytes[8];    // Last N bytes received before fault
    uint8_t  system_state;
} fault_record_t;
```

Store in a circular buffer in NVRAM or Flash. Review after field incidents.

---

## Implementation in C/C++

### Complete Fault-Tolerant UART Manager (C)

```c
/* uart_fault_tolerant.c
 * Fault-tolerant UART communication layer for embedded systems.
 * Demonstrates: error detection, timeout management, recovery, graceful degradation.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ─── Configuration ────────────────────────────────────────────────── */
#define UART_RX_BUF_SIZE        256
#define UART_TX_BUF_SIZE        256
#define MAX_MSG_LEN             128
#define MAX_RETRIES             3
#define BYTE_TIMEOUT_MS         10
#define MSG_TIMEOUT_MS          200
#define LINK_TIMEOUT_MS         2000
#define ERROR_RATE_WINDOW       100    /* messages in sliding window */
#define ERROR_RATE_THRESHOLD    10     /* max errors per window */

/* ─── Frame delimiters ─────────────────────────────────────────────── */
#define SOH     0x01
#define EOT     0x04

/* ─── Link state machine ───────────────────────────────────────────── */
typedef enum {
    LINK_NORMAL   = 0,
    LINK_DEGRADED = 1,
    LINK_RECOVERY = 2,
    LINK_FAILED   = 3
} link_state_t;

/* ─── Receive state machine ────────────────────────────────────────── */
typedef enum {
    RX_WAIT_SOH = 0,
    RX_LENGTH,
    RX_CMD,
    RX_DATA,
    RX_CRC_HI,
    RX_CRC_LO,
    RX_WAIT_EOT
} rx_state_t;

/* ─── Diagnostics ──────────────────────────────────────────────────── */
typedef struct {
    uint32_t framing_errors;
    uint32_t overrun_errors;
    uint32_t parity_errors;
    uint32_t crc_failures;
    uint32_t timeouts;
    uint32_t resets;
    uint32_t msgs_rx_ok;
    uint32_t msgs_tx_ok;
    uint32_t msgs_retried;
} uart_diag_t;

/* ─── Message frame ────────────────────────────────────────────────── */
typedef struct {
    uint8_t  cmd;
    uint8_t  len;
    uint8_t  data[MAX_MSG_LEN];
    uint16_t crc;
    uint8_t  seq;
} uart_msg_t;

/* ─── UART context ─────────────────────────────────────────────────── */
typedef struct {
    /* Hardware abstraction (platform-supplied callbacks) */
    bool     (*hw_init)(uint32_t baud);
    void     (*hw_deinit)(void);
    bool     (*hw_tx_byte)(uint8_t b);
    int      (*hw_rx_byte)(void);          /* returns -1 if empty */
    uint8_t  (*hw_read_status)(void);      /* reads UART status reg */
    void     (*hw_clear_errors)(void);
    uint32_t (*get_tick_ms)(void);

    /* Receive state machine */
    rx_state_t  rx_state;
    uart_msg_t  rx_msg;
    uint8_t     rx_data_idx;
    uint32_t    last_byte_tick;

    /* Transmit */
    uart_msg_t  tx_msg;
    uint8_t     tx_seq;

    /* Link management */
    link_state_t link_state;
    uint32_t     last_valid_rx_tick;
    uint32_t     recovery_attempts;
    uint8_t      retry_count;

    /* Error rate tracking (sliding window) */
    uint32_t     window_msgs;
    uint32_t     window_errors;

    /* Diagnostics */
    uart_diag_t  diag;

    /* Baud rate list for auto-negotiation */
    uint32_t     baud_rates[4];
    uint8_t      baud_idx;
} uart_ctx_t;

/* ─── CRC-16 CCITT ─────────────────────────────────────────────────── */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ─── Status register error flags (platform-specific bit masks) ─────── */
#define STATUS_FRAMING  (1u << 0)
#define STATUS_OVERRUN  (1u << 1)
#define STATUS_PARITY   (1u << 2)
#define STATUS_BREAK    (1u << 3)

/* ─── Check and count hardware errors ─────────────────────────────── */
static bool check_hw_errors(uart_ctx_t *ctx)
{
    uint8_t status = ctx->hw_read_status();
    bool had_error = false;

    if (status & STATUS_FRAMING) {
        ctx->diag.framing_errors++;
        had_error = true;
    }
    if (status & STATUS_OVERRUN) {
        ctx->diag.overrun_errors++;
        had_error = true;
    }
    if (status & STATUS_PARITY) {
        ctx->diag.parity_errors++;
        had_error = true;
    }

    if (had_error) {
        ctx->hw_clear_errors();
        ctx->window_errors++;
    }
    return had_error;
}

/* ─── Reset receive state machine ──────────────────────────────────── */
static void rx_reset(uart_ctx_t *ctx)
{
    ctx->rx_state    = RX_WAIT_SOH;
    ctx->rx_data_idx = 0;
    memset(&ctx->rx_msg, 0, sizeof(ctx->rx_msg));
}

/* ─── Evaluate error rate; update link state ───────────────────────── */
static void update_link_state(uart_ctx_t *ctx)
{
    ctx->window_msgs++;

    /* Slide window */
    if (ctx->window_msgs >= ERROR_RATE_WINDOW) {
        bool too_many_errors = ctx->window_errors >= ERROR_RATE_THRESHOLD;
        ctx->window_msgs   = 0;
        ctx->window_errors = 0;

        if (too_many_errors && ctx->link_state == LINK_NORMAL) {
            ctx->link_state = LINK_DEGRADED;
            /* Optionally: drop baud rate here */
        }
    }

    /* Link timeout check */
    uint32_t now = ctx->get_tick_ms();
    if ((now - ctx->last_valid_rx_tick) > LINK_TIMEOUT_MS &&
         ctx->link_state == LINK_NORMAL)
    {
        ctx->diag.timeouts++;
        ctx->link_state = LINK_RECOVERY;
    }
}

/* ─── UART peripheral recovery ─────────────────────────────────────── */
static bool attempt_recovery(uart_ctx_t *ctx)
{
    ctx->diag.resets++;
    ctx->recovery_attempts++;

    ctx->hw_deinit();

    /* Small delay to let line settle */
    /* (platform-supplied delay_ms omitted for brevity) */

    /* Try next baud rate if multiple configured */
    ctx->baud_idx = (ctx->baud_idx + 1) % 4;
    if (!ctx->baud_rates[ctx->baud_idx]) ctx->baud_idx = 0;

    bool ok = ctx->hw_init(ctx->baud_rates[ctx->baud_idx]);
    if (ok) {
        rx_reset(ctx);
        ctx->last_valid_rx_tick = ctx->get_tick_ms();
    }
    return ok;
}

/* ─── Transmit one message ─────────────────────────────────────────── */
static bool uart_send(uart_ctx_t *ctx, uint8_t cmd, const uint8_t *data, uint8_t len)
{
    if (len > MAX_MSG_LEN) return false;

    /* Build frame: SOH | LEN | SEQ | CMD | DATA | CRC_HI | CRC_LO | EOT */
    uint8_t frame[MAX_MSG_LEN + 8];
    uint8_t fi = 0;

    frame[fi++] = SOH;
    frame[fi++] = len;
    frame[fi++] = ctx->tx_seq++;
    frame[fi++] = cmd;
    memcpy(&frame[fi], data, len);
    fi += len;

    uint16_t crc = crc16_ccitt(frame + 1, fi - 1); /* CRC over LEN..DATA */
    frame[fi++] = (uint8_t)(crc >> 8);
    frame[fi++] = (uint8_t)(crc & 0xFF);
    frame[fi++] = EOT;

    for (uint8_t i = 0; i < fi; i++) {
        if (!ctx->hw_tx_byte(frame[i])) return false;
    }

    ctx->diag.msgs_tx_ok++;
    return true;
}

/* ─── Send with retry ──────────────────────────────────────────────── */
bool uart_send_reliable(uart_ctx_t *ctx, uint8_t cmd,
                        const uint8_t *data, uint8_t len)
{
    for (ctx->retry_count = 0;
         ctx->retry_count <= MAX_RETRIES;
         ctx->retry_count++)
    {
        if (!uart_send(ctx, cmd, data, len)) continue;

        /* Wait for ACK (simplified — real implementation polls rx) */
        uint32_t deadline = ctx->get_tick_ms() + MSG_TIMEOUT_MS;
        while (ctx->get_tick_ms() < deadline) {
            /* uart_rx_poll(ctx) would be called here */
            /* If ACK received: */
            /*   ctx->diag.msgs_tx_ok++;  return true; */
        }

        ctx->diag.msgs_retried++;
    }

    /* All retries exhausted */
    ctx->link_state = LINK_RECOVERY;
    return false;
}

/* ─── Receive byte processor (call from ISR or polling loop) ────────── */
void uart_rx_poll(uart_ctx_t *ctx)
{
    /* Check hardware error flags first */
    if (check_hw_errors(ctx)) {
        rx_reset(ctx);
        return;
    }

    int b = ctx->hw_rx_byte();
    if (b < 0) {
        /* No byte available — check for byte-level timeout */
        if (ctx->rx_state != RX_WAIT_SOH) {
            uint32_t now = ctx->get_tick_ms();
            if ((now - ctx->last_byte_tick) > BYTE_TIMEOUT_MS) {
                ctx->diag.timeouts++;
                ctx->window_errors++;
                rx_reset(ctx);
            }
        }
        return;
    }

    ctx->last_byte_tick = ctx->get_tick_ms();

    switch (ctx->rx_state) {
    case RX_WAIT_SOH:
        if ((uint8_t)b == SOH) ctx->rx_state = RX_LENGTH;
        /* else: discard — not in sync */
        break;

    case RX_LENGTH:
        if (b > MAX_MSG_LEN) {
            ctx->window_errors++;
            rx_reset(ctx);
        } else {
            ctx->rx_msg.len = (uint8_t)b;
            ctx->rx_state   = RX_CMD;
        }
        break;

    case RX_CMD:
        ctx->rx_msg.cmd = (uint8_t)b;
        ctx->rx_data_idx = 0;
        ctx->rx_state    = (ctx->rx_msg.len > 0) ? RX_DATA : RX_CRC_HI;
        break;

    case RX_DATA:
        ctx->rx_msg.data[ctx->rx_data_idx++] = (uint8_t)b;
        if (ctx->rx_data_idx >= ctx->rx_msg.len)
            ctx->rx_state = RX_CRC_HI;
        break;

    case RX_CRC_HI:
        ctx->rx_msg.crc = (uint16_t)b << 8;
        ctx->rx_state   = RX_CRC_LO;
        break;

    case RX_CRC_LO:
        ctx->rx_msg.crc |= (uint8_t)b;
        ctx->rx_state    = RX_WAIT_EOT;
        break;

    case RX_WAIT_EOT:
        if ((uint8_t)b == EOT) {
            /* Validate CRC */
            /* (Re-build header for CRC calculation) */
            uint8_t hdr[2] = { ctx->rx_msg.len, ctx->rx_msg.cmd };
            uint16_t crc = crc16_ccitt(hdr, 2);
            crc = crc16_ccitt(ctx->rx_msg.data, ctx->rx_msg.len);
            /* (Simplified — real code would include all CRC'd fields) */

            if (crc == ctx->rx_msg.crc) {
                ctx->diag.msgs_rx_ok++;
                ctx->last_valid_rx_tick = ctx->get_tick_ms();
                /* Deliver message to application layer */
                /* on_message_received(&ctx->rx_msg); */
            } else {
                ctx->diag.crc_failures++;
                ctx->window_errors++;
            }
        } else {
            ctx->window_errors++;  /* EOT missing — framing lost */
        }
        rx_reset(ctx);
        update_link_state(ctx);
        break;
    }
}

/* ─── Main fault management task (call periodically) ───────────────── */
void uart_fault_manager(uart_ctx_t *ctx)
{
    switch (ctx->link_state) {

    case LINK_NORMAL:
        /* Nothing to do — healthy */
        break;

    case LINK_DEGRADED:
        /* Reduce data rate, send essential msgs only */
        /* Application layer should check ctx->link_state */
        update_link_state(ctx);
        break;

    case LINK_RECOVERY:
        if (ctx->recovery_attempts < 5) {
            if (attempt_recovery(ctx)) {
                ctx->link_state = LINK_DEGRADED; /* Re-enter cautiously */
                ctx->recovery_attempts = 0;
            }
        } else {
            ctx->link_state = LINK_FAILED;
        }
        break;

    case LINK_FAILED:
        /* Enter safe state — application must handle this */
        /* Signal via callback: on_link_failed(); */
        break;
    }
}

/* ─── Initialise context ────────────────────────────────────────────── */
void uart_ctx_init(uart_ctx_t *ctx, uint32_t primary_baud)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->baud_rates[0] = primary_baud;
    ctx->baud_rates[1] = 57600;
    ctx->baud_rates[2] = 9600;
    ctx->baud_rates[3] = 0;     /* sentinel */
    ctx->link_state    = LINK_NORMAL;
    rx_reset(ctx);
    ctx->hw_init(primary_baud);
    ctx->last_valid_rx_tick = ctx->get_tick_ms();
}
```

---

### C++ RAII Wrapper with Exception-Safe Recovery

```cpp
// uart_resilient.hpp
// C++ fault-tolerant UART manager using RAII and state machine.

#pragma once
#include <cstdint>
#include <functional>
#include <array>
#include <chrono>
#include <optional>

class ResilientUart {
public:
    enum class LinkState { Normal, Degraded, Recovery, Failed };

    struct Config {
        uint32_t primary_baud   = 115200;
        uint32_t msg_timeout_ms = 200;
        uint32_t link_timeout_ms = 2000;
        uint8_t  max_retries    = 3;
        bool     use_parity     = true;
        bool     use_hw_flow    = false;
    };

    struct Stats {
        uint32_t rx_ok         = 0;
        uint32_t tx_ok         = 0;
        uint32_t crc_failures  = 0;
        uint32_t timeouts      = 0;
        uint32_t resets        = 0;
        uint32_t retransmits   = 0;
        LinkState current_state = LinkState::Normal;
    };

    /* Platform callbacks */
    using TxFn     = std::function<bool(const uint8_t*, size_t)>;
    using RxFn     = std::function<int()>;            // returns -1 if empty
    using InitFn   = std::function<bool(uint32_t)>;
    using DeinitFn = std::function<void()>;
    using TickFn   = std::function<uint32_t()>;
    using MsgCb    = std::function<void(uint8_t cmd, const uint8_t*, size_t)>;

    ResilientUart(Config cfg, TxFn tx, RxFn rx, InitFn init,
                  DeinitFn deinit, TickFn tick, MsgCb on_msg)
        : cfg_(cfg), tx_(tx), rx_(rx), init_(init),
          deinit_(deinit), tick_(tick), on_msg_(on_msg)
    {
        init_(cfg_.primary_baud);
        last_rx_tick_ = tick_();
    }

    ~ResilientUart() { deinit_(); }

    /* Non-copyable */
    ResilientUart(const ResilientUart&) = delete;
    ResilientUart& operator=(const ResilientUart&) = delete;

    /* ── Send with automatic retry ─────────────────────────────── */
    bool send(uint8_t cmd, const uint8_t* data, size_t len) {
        for (uint8_t attempt = 0; attempt <= cfg_.max_retries; ++attempt) {
            if (attempt > 0) stats_.retransmits++;

            auto frame = build_frame(cmd, data, len);
            if (!tx_(frame.data(), frame.size())) continue;

            /* Wait for ACK */
            uint32_t deadline = tick_() + cfg_.msg_timeout_ms;
            while (tick_() < deadline) {
                poll_rx();
                if (ack_received_) {
                    ack_received_ = false;
                    stats_.tx_ok++;
                    return true;
                }
            }
        }

        transition(LinkState::Recovery);
        return false;
    }

    /* ── Poll receive — call frequently from main loop or ISR ──── */
    void poll_rx() {
        int b = rx_();
        if (b < 0) {
            handle_rx_timeout();
            return;
        }

        last_byte_tick_ = tick_();
        process_rx_byte(static_cast<uint8_t>(b));
    }

    /* ── Periodic maintenance — call from task / timer ──────────── */
    void tick() {
        check_link_timeout();
        run_recovery_if_needed();
    }

    const Stats& stats() const { return stats_; }
    LinkState state() const { return state_; }

    /* ── Register optional state-change callback ─────────────────── */
    void on_state_change(std::function<void(LinkState)> cb) {
        state_cb_ = cb;
    }

private:
    /* ── CRC-16 CCITT ────────────────────────────────────────────── */
    static uint16_t crc16(const uint8_t* d, size_t n) {
        uint16_t crc = 0xFFFF;
        while (n--) {
            crc ^= static_cast<uint16_t>(*d++) << 8;
            for (int i = 0; i < 8; ++i)
                crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
        return crc;
    }

    /* ── Frame builder ───────────────────────────────────────────── */
    std::array<uint8_t, 136> build_frame(uint8_t cmd,
                                          const uint8_t* data, size_t len) {
        std::array<uint8_t, 136> f{};
        size_t i = 0;
        f[i++] = 0x01;                           // SOH
        f[i++] = static_cast<uint8_t>(len);
        f[i++] = tx_seq_++;
        f[i++] = cmd;
        for (size_t j = 0; j < len; ++j) f[i++] = data[j];
        uint16_t crc = crc16(f.data() + 1, i - 1);
        f[i++] = static_cast<uint8_t>(crc >> 8);
        f[i++] = static_cast<uint8_t>(crc & 0xFF);
        f[i++] = 0x04;                           // EOT
        return f;
    }

    /* ── RX state machine ────────────────────────────────────────── */
    enum class RxState {
        WaitSoh, Length, Seq, Cmd, Data, CrcHi, CrcLo, WaitEot
    };

    void process_rx_byte(uint8_t b) {
        switch (rx_state_) {
        case RxState::WaitSoh:
            if (b == 0x01) rx_state_ = RxState::Length;
            break;
        case RxState::Length:
            if (b > 128) { reset_rx(); break; }
            rx_len_ = b; rx_state_ = RxState::Seq;
            break;
        case RxState::Seq:
            rx_seq_  = b; rx_state_ = RxState::Cmd;
            break;
        case RxState::Cmd:
            rx_cmd_  = b; rx_idx_   = 0;
            rx_state_ = rx_len_ > 0 ? RxState::Data : RxState::CrcHi;
            break;
        case RxState::Data:
            rx_buf_[rx_idx_++] = b;
            if (rx_idx_ >= rx_len_) rx_state_ = RxState::CrcHi;
            break;
        case RxState::CrcHi:
            rx_crc_  = static_cast<uint16_t>(b) << 8;
            rx_state_ = RxState::CrcLo;
            break;
        case RxState::CrcLo:
            rx_crc_ |= b;
            rx_state_ = RxState::WaitEot;
            break;
        case RxState::WaitEot:
            if (b == 0x04) validate_and_deliver();
            else           stats_.crc_failures++;
            reset_rx();
            break;
        }
    }

    void validate_and_deliver() {
        /* Compute expected CRC over received payload */
        uint16_t computed = crc16(rx_buf_.data(), rx_len_);
        if (computed == rx_crc_) {
            stats_.rx_ok++;
            last_rx_tick_ = tick_();
            if (state_ != LinkState::Normal)
                transition(LinkState::Normal);
            on_msg_(rx_cmd_, rx_buf_.data(), rx_len_);
            ack_received_ = (rx_cmd_ == 0x06); /* ACK command */
        } else {
            stats_.crc_failures++;
            error_count_++;
        }
    }

    void reset_rx() {
        rx_state_ = RxState::WaitSoh;
        rx_idx_   = 0;
    }

    void handle_rx_timeout() {
        if (rx_state_ != RxState::WaitSoh) {
            if ((tick_() - last_byte_tick_) > 10u) {
                stats_.timeouts++;
                error_count_++;
                reset_rx();
            }
        }
    }

    void check_link_timeout() {
        if ((tick_() - last_rx_tick_) > cfg_.link_timeout_ms &&
             state_ == LinkState::Normal)
        {
            stats_.timeouts++;
            transition(LinkState::Recovery);
        }
    }

    void run_recovery_if_needed() {
        if (state_ != LinkState::Recovery) return;

        if (recovery_attempts_ < 5) {
            deinit_();
            bool ok = init_(cfg_.primary_baud);
            stats_.resets++;
            recovery_attempts_++;

            if (ok) {
                last_rx_tick_ = tick_();
                reset_rx();
                transition(LinkState::Degraded);
                recovery_attempts_ = 0;
            }
        } else {
            transition(LinkState::Failed);
        }
    }

    void transition(LinkState next) {
        if (state_ == next) return;
        state_       = next;
        stats_.current_state = next;
        if (state_cb_) state_cb_(next);
    }

    /* ── Members ─────────────────────────────────────────────────── */
    Config    cfg_;
    TxFn      tx_;   RxFn rx_;
    InitFn    init_; DeinitFn deinit_;
    TickFn    tick_;
    MsgCb     on_msg_;
    std::function<void(LinkState)> state_cb_;

    LinkState state_            = LinkState::Normal;
    Stats     stats_;

    RxState   rx_state_         = RxState::WaitSoh;
    std::array<uint8_t, 128> rx_buf_{};
    uint8_t   rx_len_           = 0;
    uint8_t   rx_seq_           = 0;
    uint8_t   rx_cmd_           = 0;
    uint8_t   rx_idx_           = 0;
    uint16_t  rx_crc_           = 0;

    uint8_t   tx_seq_           = 0;
    bool      ack_received_     = false;

    uint32_t  last_rx_tick_     = 0;
    uint32_t  last_byte_tick_   = 0;
    uint32_t  recovery_attempts_ = 0;
    uint32_t  error_count_      = 0;
};
```

---

## Implementation in Rust

Rust's type system, ownership model, and `Result`/`Option` types are a natural fit for
fault-tolerant protocol implementation — errors cannot be silently ignored.

### Core Protocol Types

```rust
// uart_fault_tolerant/src/lib.rs
//! Fault-tolerant UART communication layer.
//!
//! Features:
//! - CRC-16 CCITT frame validation
//! - Receive state machine with byte timeouts
//! - Link state machine (Normal → Degraded → Recovery → Failed)
//! - Automatic retry with configurable limits
//! - Diagnostic counters

use core::fmt;

// ─── Error types ────────────────────────────────────────────────────

#[derive(Debug, Clone, PartialEq)]
pub enum UartError {
    CrcMismatch { expected: u16, got: u16 },
    FramingError,
    OverrunError,
    ParityError,
    ByteTimeout,
    MessageTimeout,
    LinkTimeout,
    MaxRetriesExceeded,
    BufferFull,
    InvalidLength(usize),
    HardwareError(&'static str),
}

impl fmt::Display for UartError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::CrcMismatch { expected, got } =>
                write!(f, "CRC mismatch: expected 0x{expected:04X}, got 0x{got:04X}"),
            Self::FramingError        => write!(f, "UART framing error"),
            Self::OverrunError        => write!(f, "UART overrun error"),
            Self::ParityError         => write!(f, "UART parity error"),
            Self::ByteTimeout         => write!(f, "Byte timeout"),
            Self::MessageTimeout      => write!(f, "Message timeout"),
            Self::LinkTimeout         => write!(f, "Link timeout"),
            Self::MaxRetriesExceeded  => write!(f, "Max retries exceeded"),
            Self::BufferFull          => write!(f, "Buffer full"),
            Self::InvalidLength(n)    => write!(f, "Invalid message length: {n}"),
            Self::HardwareError(msg)  => write!(f, "Hardware error: {msg}"),
        }
    }
}

// ─── Link state ──────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LinkState {
    Normal,
    Degraded,
    Recovery,
    Failed,
}

// ─── Receive state machine ────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum RxState {
    WaitSoh,
    Length,
    Seq,
    Cmd,
    Data,
    CrcHi,
    CrcLo,
    WaitEot,
}

// ─── Message ─────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct Message {
    pub cmd:  u8,
    pub seq:  u8,
    pub data: heapless::Vec<u8, 128>,
}

// ─── Diagnostics ─────────────────────────────────────────────────────

#[derive(Debug, Default, Clone)]
pub struct Diagnostics {
    pub framing_errors: u32,
    pub overrun_errors: u32,
    pub parity_errors:  u32,
    pub crc_failures:   u32,
    pub timeouts:       u32,
    pub resets:         u32,
    pub msgs_rx_ok:     u32,
    pub msgs_tx_ok:     u32,
    pub retransmits:    u32,
}

// ─── CRC-16 CCITT ─────────────────────────────────────────────────────

fn crc16_ccitt(data: &[u8]) -> u16 {
    data.iter().fold(0xFFFFu16, |crc, &byte| {
        let crc = crc ^ ((byte as u16) << 8);
        (0..8).fold(crc, |crc, _| {
            if crc & 0x8000 != 0 {
                (crc << 1) ^ 0x1021
            } else {
                crc << 1
            }
        })
    })
}

// ─── Configuration ────────────────────────────────────────────────────

pub struct Config {
    pub primary_baud:    u32,
    pub byte_timeout_ms: u32,
    pub msg_timeout_ms:  u32,
    pub link_timeout_ms: u32,
    pub max_retries:     u8,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            primary_baud:    115_200,
            byte_timeout_ms: 10,
            msg_timeout_ms:  200,
            link_timeout_ms: 2_000,
            max_retries:     3,
        }
    }
}

// ─── Platform abstraction (hardware callbacks) ────────────────────────

pub trait UartHw {
    fn init(&mut self, baud: u32) -> Result<(), UartError>;
    fn deinit(&mut self);
    fn tx_byte(&mut self, b: u8) -> Result<(), UartError>;
    fn rx_byte(&mut self) -> Option<u8>;
    fn read_status(&self) -> u8;     // bit0=framing, bit1=overrun, bit2=parity
    fn clear_errors(&mut self);
    fn tick_ms(&self) -> u32;
}

// ─── Receiver ─────────────────────────────────────────────────────────

struct Receiver {
    state:      RxState,
    len:        u8,
    seq:        u8,
    cmd:        u8,
    buf:        heapless::Vec<u8, 128>,
    crc:        u16,
    last_byte:  u32,
}

impl Default for Receiver {
    fn default() -> Self {
        Self {
            state:     RxState::WaitSoh,
            len:       0, seq: 0, cmd: 0,
            buf:       heapless::Vec::new(),
            crc:       0,
            last_byte: 0,
        }
    }
}

impl Receiver {
    fn reset(&mut self) {
        self.state = RxState::WaitSoh;
        self.buf.clear();
    }

    /// Feed one byte into the state machine.
    /// Returns `Ok(Some(Message))` when a complete valid frame is decoded.
    fn feed(&mut self, b: u8, now_ms: u32) -> Result<Option<Message>, UartError> {
        self.last_byte = now_ms;

        match self.state {
            RxState::WaitSoh => {
                if b == 0x01 { self.state = RxState::Length; }
            }
            RxState::Length => {
                if b as usize > 128 {
                    self.reset();
                    return Err(UartError::InvalidLength(b as usize));
                }
                self.len   = b;
                self.state = RxState::Seq;
            }
            RxState::Seq => {
                self.seq   = b;
                self.state = RxState::Cmd;
            }
            RxState::Cmd => {
                self.cmd   = b;
                self.buf.clear();
                self.state = if self.len > 0 { RxState::Data } else { RxState::CrcHi };
            }
            RxState::Data => {
                self.buf.push(b).map_err(|_| UartError::BufferFull)?;
                if self.buf.len() >= self.len as usize {
                    self.state = RxState::CrcHi;
                }
            }
            RxState::CrcHi => {
                self.crc   = (b as u16) << 8;
                self.state = RxState::CrcLo;
            }
            RxState::CrcLo => {
                self.crc  |= b as u16;
                self.state = RxState::WaitEot;
            }
            RxState::WaitEot => {
                self.state = RxState::WaitSoh;
                if b != 0x04 {
                    self.reset();
                    return Err(UartError::FramingError);
                }
                // Validate CRC over payload only (simplified)
                let computed = crc16_ccitt(&self.buf);
                if computed != self.crc {
                    let err = UartError::CrcMismatch {
                        expected: self.crc,
                        got: computed,
                    };
                    self.reset();
                    return Err(err);
                }
                let msg = Message {
                    cmd:  self.cmd,
                    seq:  self.seq,
                    data: self.buf.clone(),
                };
                self.reset();
                return Ok(Some(msg));
            }
        }
        Ok(None)
    }

    fn check_byte_timeout(&mut self, now_ms: u32, timeout_ms: u32)
        -> Result<(), UartError>
    {
        if self.state != RxState::WaitSoh
            && (now_ms - self.last_byte) > timeout_ms
        {
            self.reset();
            return Err(UartError::ByteTimeout);
        }
        Ok(())
    }
}

// ─── Fault-tolerant UART manager ──────────────────────────────────────

pub struct FaultTolerantUart<H: UartHw> {
    hw:                 H,
    cfg:                Config,
    receiver:           Receiver,
    tx_seq:             u8,
    link_state:         LinkState,
    last_valid_rx:      u32,
    recovery_attempts:  u32,
    diag:               Diagnostics,
}

impl<H: UartHw> FaultTolerantUart<H> {
    pub fn new(mut hw: H, cfg: Config) -> Result<Self, UartError> {
        let baud = cfg.primary_baud;
        hw.init(baud)?;
        let now = hw.tick_ms();
        Ok(Self {
            hw,
            cfg,
            receiver:          Receiver::default(),
            tx_seq:            0,
            link_state:        LinkState::Normal,
            last_valid_rx:     now,
            recovery_attempts: 0,
            diag:              Diagnostics::default(),
        })
    }

    // ── Transmit helpers ──────────────────────────────────────────

    fn build_frame(&self, cmd: u8, data: &[u8]) -> heapless::Vec<u8, 136> {
        let mut frame: heapless::Vec<u8, 136> = heapless::Vec::new();
        let _ = frame.push(0x01);                    // SOH
        let _ = frame.push(data.len() as u8);
        let _ = frame.push(self.tx_seq);
        let _ = frame.push(cmd);
        frame.extend_from_slice(data).ok();
        let crc = crc16_ccitt(data);
        let _ = frame.push((crc >> 8) as u8);
        let _ = frame.push((crc & 0xFF) as u8);
        let _ = frame.push(0x04);                    // EOT
        frame
    }

    fn transmit_frame(&mut self, frame: &[u8]) -> Result<(), UartError> {
        for &b in frame {
            self.hw.tx_byte(b)?;
        }
        Ok(())
    }

    /// Send message with automatic retry on timeout.
    pub fn send(&mut self, cmd: u8, data: &[u8]) -> Result<(), UartError> {
        for attempt in 0..=self.cfg.max_retries {
            if attempt > 0 {
                self.diag.retransmits += 1;
            }

            let frame = self.build_frame(cmd, data);
            if let Err(e) = self.transmit_frame(&frame) {
                log_error(&e);
                continue;
            }

            // Wait for ACK within timeout
            let deadline = self.hw.tick_ms() + self.cfg.msg_timeout_ms;
            loop {
                if self.hw.tick_ms() >= deadline { break; }
                match self.poll() {
                    Ok(Some(msg)) if msg.cmd == 0x06 => { // ACK
                        self.tx_seq = self.tx_seq.wrapping_add(1);
                        self.diag.msgs_tx_ok += 1;
                        return Ok(());
                    }
                    Ok(Some(msg)) => {
                        // Other message received — queue for application
                        let _ = msg; // real impl: push to rx queue
                    }
                    Ok(None) => {}
                    Err(e)   => log_error(&e),
                }
            }
        }

        self.transition(LinkState::Recovery);
        Err(UartError::MaxRetriesExceeded)
    }

    // ── Receive ───────────────────────────────────────────────────

    /// Poll for incoming bytes. Call frequently from main loop or timer.
    pub fn poll(&mut self) -> Result<Option<Message>, UartError> {
        // Check hardware error flags
        self.check_hw_errors()?;

        let now = self.hw.tick_ms();

        // Check byte timeout
        if let Err(e) = self.receiver.check_byte_timeout(now, self.cfg.byte_timeout_ms) {
            self.diag.timeouts += 1;
            return Err(e);
        }

        // Process available byte
        if let Some(b) = self.hw.rx_byte() {
            match self.receiver.feed(b, now) {
                Ok(Some(msg)) => {
                    self.diag.msgs_rx_ok += 1;
                    self.last_valid_rx = now;
                    if self.link_state != LinkState::Normal {
                        self.transition(LinkState::Normal);
                    }
                    return Ok(Some(msg));
                }
                Ok(None)   => {}
                Err(e) => {
                    self.diag.crc_failures += 1;
                    return Err(e);
                }
            }
        }

        Ok(None)
    }

    fn check_hw_errors(&mut self) -> Result<(), UartError> {
        let status = self.hw.read_status();
        if status == 0 { return Ok(()); }

        if status & 0x01 != 0 { self.diag.framing_errors += 1; }
        if status & 0x02 != 0 { self.diag.overrun_errors += 1; }
        if status & 0x04 != 0 { self.diag.parity_errors  += 1; }

        self.hw.clear_errors();
        self.receiver.reset();

        Err(UartError::FramingError)
    }

    // ── Periodic fault management ─────────────────────────────────

    /// Call from a periodic task (e.g., 100 ms timer).
    pub fn maintain(&mut self) {
        let now = self.hw.tick_ms();
        self.check_link_timeout(now);
        self.run_recovery();
    }

    fn check_link_timeout(&mut self, now: u32) {
        if self.link_state == LinkState::Normal
            && (now - self.last_valid_rx) > self.cfg.link_timeout_ms
        {
            self.diag.timeouts += 1;
            self.transition(LinkState::Recovery);
        }
    }

    fn run_recovery(&mut self) {
        if self.link_state != LinkState::Recovery { return; }

        if self.recovery_attempts >= 5 {
            self.transition(LinkState::Failed);
            return;
        }

        self.hw.deinit();
        self.diag.resets += 1;
        self.recovery_attempts += 1;

        match self.hw.init(self.cfg.primary_baud) {
            Ok(()) => {
                self.receiver.reset();
                self.last_valid_rx = self.hw.tick_ms();
                self.recovery_attempts = 0;
                self.transition(LinkState::Degraded);
            }
            Err(_) => {} // will retry next maintain() call
        }
    }

    fn transition(&mut self, next: LinkState) {
        if self.link_state != next {
            // Real impl: log transition, call callback
            self.link_state = next;
        }
    }

    // ── Accessors ─────────────────────────────────────────────────

    pub fn link_state(&self) -> LinkState { self.link_state }
    pub fn diagnostics(&self) -> &Diagnostics { &self.diag }
    pub fn is_operational(&self) -> bool {
        matches!(self.link_state, LinkState::Normal | LinkState::Degraded)
    }
}

fn log_error(e: &UartError) {
    // In a real system: write to ring-buffer log in NVRAM
    let _ = e;
}

// ─── Unit tests ────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc16_known_value() {
        // CRC-16 CCITT of b"123456789" = 0x29B1
        let data = b"123456789";
        assert_eq!(crc16_ccitt(data), 0x29B1);
    }

    #[test]
    fn receiver_valid_frame() {
        let mut rx = Receiver::default();
        // Frame: SOH | len=1 | seq=0 | cmd=0x10 | data=0xAB | CRC_HI | CRC_LO | EOT
        let data = [0xABu8];
        let crc  = crc16_ccitt(&data);
        let frame = [
            0x01u8,            // SOH
            0x01,              // len
            0x00,              // seq
            0x10,              // cmd
            0xAB,              // data
            (crc >> 8) as u8,  // CRC hi
            (crc & 0xFF) as u8,// CRC lo
            0x04,              // EOT
        ];

        let mut result = None;
        for &b in &frame {
            match rx.feed(b, 0) {
                Ok(Some(msg)) => result = Some(msg),
                Ok(None) => {}
                Err(e) => panic!("Unexpected error: {e}"),
            }
        }

        let msg = result.expect("No message decoded");
        assert_eq!(msg.cmd, 0x10);
        assert_eq!(msg.data.as_slice(), &[0xAB]);
    }

    #[test]
    fn receiver_rejects_bad_crc() {
        let mut rx = Receiver::default();
        let frame = [0x01u8, 0x01, 0x00, 0x10, 0xAB, 0xFF, 0xFF, 0x04];
        let mut got_error = false;
        for &b in &frame {
            if let Err(UartError::CrcMismatch { .. }) = rx.feed(b, 0) {
                got_error = true;
            }
        }
        assert!(got_error, "Expected CRC mismatch error");
    }

    #[test]
    fn receiver_byte_timeout() {
        let mut rx = Receiver::default();
        // Start receiving a frame but stop partway
        let _ = rx.feed(0x01, 0);   // SOH at t=0
        let _ = rx.feed(0x02, 0);   // LEN
        // Simulate timeout: last_byte=0, now=100, timeout=10
        let result = rx.check_byte_timeout(100, 10);
        assert!(matches!(result, Err(UartError::ByteTimeout)));
    }
}
```

---

### Rust: Async Fault-Tolerant UART (embassy / tokio)

```rust
// async_uart.rs
// Async fault-tolerant UART using embassy-executor (or tokio for hosted targets).
// Demonstrates timeout, retry, and degradation with async/await.

use core::time::Duration;
use embassy_time::{Timer, Instant};

pub struct AsyncResilientUart<W, R>
where
    W: embedded_io_async::Write,
    R: embedded_io_async::Read,
{
    writer:         W,
    reader:         R,
    msg_timeout:    Duration,
    link_timeout:   Duration,
    max_retries:    u8,
    link_state:     LinkState,
    last_rx:        Instant,
}

impl<W, R> AsyncResilientUart<W, R>
where
    W: embedded_io_async::Write,
    R: embedded_io_async::Read,
{
    pub fn new(writer: W, reader: R) -> Self {
        Self {
            writer,
            reader,
            msg_timeout:  Duration::from_millis(200),
            link_timeout: Duration::from_millis(2000),
            max_retries:  3,
            link_state:   LinkState::Normal,
            last_rx:      Instant::now(),
        }
    }

    /// Send a frame and wait for ACK — with timeout.
    pub async fn send_reliable(&mut self, data: &[u8]) -> Result<(), UartError> {
        for attempt in 0..=self.max_retries {
            if attempt > 0 {
                // Exponential back-off
                Timer::after(Duration::from_millis(10 * (1 << attempt) as u64)).await;
            }

            // Transmit
            self.writer.write_all(data).await
                .map_err(|_| UartError::HardwareError("TX failed"))?;

            // Await ACK with timeout
            match embassy_time::with_timeout(
                self.msg_timeout,
                self.receive_ack()
            ).await {
                Ok(Ok(())) => return Ok(()),
                Ok(Err(e)) => {
                    // Protocol error — retry
                    log_error(&e);
                }
                Err(_timeout) => {
                    // Timed out — retry
                }
            }
        }

        self.link_state = LinkState::Recovery;
        Err(UartError::MaxRetriesExceeded)
    }

    async fn receive_ack(&mut self) -> Result<(), UartError> {
        let mut buf = [0u8; 8];
        self.reader.read_exact(&mut buf).await
            .map_err(|_| UartError::HardwareError("RX failed"))?;

        // Check for ACK frame (0x06 command byte)
        if buf[3] == 0x06 {
            self.last_rx = Instant::now();
            Ok(())
        } else {
            Err(UartError::FramingError)
        }
    }

    /// Background task: monitors link health.
    /// Run as a separate embassy task.
    pub async fn link_monitor_task(&mut self) {
        loop {
            Timer::after(Duration::from_millis(500)).await;

            if self.last_rx.elapsed() > self.link_timeout {
                self.link_state = LinkState::Recovery;
                // Trigger recovery logic
            }
        }
    }

    pub fn link_state(&self) -> LinkState { self.link_state }
}
```

---

## Summary

UART fault tolerance is an essential discipline for any embedded system that must operate
reliably in real-world conditions. The key strategies covered in this document are:

**Detection:** Hardware error flags (framing, overrun, parity) must be read and acted upon
immediately. Application-level CRC-16 provides a second layer of integrity verification for
every message, catching multi-bit errors that parity cannot detect.

**Framing and Synchronisation:** A well-designed message framing protocol (SOH / LEN / CMD /
DATA / CRC / EOT) allows the receiver to detect partial messages, resynchronise after data
corruption, and cleanly discard invalid frames without crashing the state machine.

**Timeout Hierarchy:** Byte-level timeouts flush partial frames; message-level timeouts trigger
retransmission; link-level timeouts initiate recovery. This layered approach ensures the
system responds proportionally to the severity of the fault.

**State Machine Approach:** A four-state link model (Normal → Degraded → Recovery → Failed)
provides structured escalation. On each state transition the system takes a proportionate
action — reducing baud rate, suspending non-critical traffic, resetting the peripheral, or
entering a hardware safe state.

**Graceful Degradation:** Rather than crashing or hanging on link failure, the system continues
operating at reduced capacity: essential safety messages are preserved, actuators remain
controllable, and the operator is alerted. Full recovery is attempted automatically before
manual intervention is required.

**Diagnostics:** Persistent error counters and fault records provide invaluable insight for
field debugging and preventive maintenance. They turn silent failures into traceable events.

**Language Considerations:** C provides direct hardware access and predictable ISR-driven
operation, critical for bare-metal MCUs. Rust's type system enforces exhaustive error handling
— `Result<T, E>` makes it impossible to silently discard errors — while `async/await` on
embassy enables clean timeout management without blocking the scheduler.

Together, these techniques enable UART communication to be used safely and reliably in
industrial, automotive, and safety-critical embedded systems.

---

*Part of the UART Embedded Systems Reference Series — Topic 48 of 64*