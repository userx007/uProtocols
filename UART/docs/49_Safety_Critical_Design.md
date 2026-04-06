# 49. Safety-Critical Design: UART in Automotive, Medical, and Aerospace Applications

---

**Standards & Frameworks** — Full coverage of IEC 61508 (SIL 1–4), ISO 26262 (ASIL A–D for automotive), IEC 60601/62443 (medical), and DO-178C (aerospace DAL A–E), with a comparative table.

**Core Challenges** — Bit error detection, framing loss, message loss/duplication, real-time latency, and silent data corruption — each with specific mitigations.

**Safety Architecture** — Dual-channel 1oo2D redundancy, heartbeat supervision, graceful degradation, byte-stuffed framing (COBS/SLIP-style), and the complete safety frame layout (SOF/Length/Seq/CRC/EOF).

**C/C++ Code Examples:**
1. UART init with mandatory loopback self-test (ISO 26262 ASIL C/D requirement)
2. CRC-16/CCITT table-driven implementation (deterministic WCET)
3. Safety frame transmitter with byte-stuffing
4. Receiver state machine with framing, sequence, and CRC validation
5. Challenge–response watchdog keep-alive
6. ASIL-B `UartSafetyMonitor` class with error thresholds and safe-state transitions

**Rust Code Examples:**
1. `const fn` compile-time CRC table generation (`no_std`)
2. Frame builder using `heapless::Vec` (no heap allocation)
3. Receiver state machine with Rust `enum` states
4. HMAC-based watchdog handler (medical/ASIL-C)
5. **Typestate pattern** — compile-time enforcement that a UART channel cannot be used before self-test passes

**Testing & Summary** — HIL fault injection, static analysis tools (PC-lint, Polyspace, Frama-C, Clippy), MC/DC coverage, and mutation testing.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Safety Standards and Certification Frameworks](#safety-standards-and-certification-frameworks)
3. [Core Challenges in Safety-Critical UART Design](#core-challenges-in-safety-critical-uart-design)
4. [Fault Detection and Error Handling](#fault-detection-and-error-handling)
5. [Redundancy and Failsafe Architectures](#redundancy-and-failsafe-architectures)
6. [Watchdog and Supervision Mechanisms](#watchdog-and-supervision-mechanisms)
7. [Secure Framing and Data Integrity](#secure-framing-and-data-integrity)
8. [Automotive Applications (ISO 26262)](#automotive-applications-iso-26262)
9. [Medical Applications (IEC 62443 / FDA Guidance)](#medical-applications-iec-62443--fda-guidance)
10. [Aerospace Applications (DO-178C / MIL-STD)](#aerospace-applications-do-178c--mil-std)
11. [Code Examples in C/C++](#code-examples-in-cc)
12. [Code Examples in Rust](#code-examples-in-rust)
13. [Testing and Validation Strategies](#testing-and-validation-strategies)
14. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) is one of the oldest and most pervasive serial communication protocols in embedded systems. Its simplicity — no clock line, minimal pin count, and broad hardware support — has made it a default choice for debug consoles, sensor interfaces, and low-speed control links for decades.

However, when UART is deployed in **safety-critical environments** — automotive ECUs, patient-monitoring devices, or avionics systems — the same simplicity that makes it attractive also becomes a liability. UART has no built-in:

- Message framing or start-of-frame/end-of-frame delimiters
- Sequence numbering or duplicate detection
- Source authentication
- End-to-end CRC beyond a single parity bit
- Collision detection or bus arbitration

Safety-critical design bridges these gaps through rigorous architectural choices, layered protocols, and strict compliance with domain-specific functional safety standards. This document explores each of these dimensions in depth, with implementation guidance in C/C++ and Rust.

---

## Safety Standards and Certification Frameworks

Before writing a single line of code, a safety-critical engineer must understand the normative framework governing their domain.

### IEC 61508 — Foundation Standard

IEC 61508 ("Functional Safety of E/E/PE Safety-Related Systems") is the root standard. It defines Safety Integrity Levels (SIL 1–4), which quantify the probability of dangerous failure per hour. All domain-specific standards derive from it.

| SIL | Probability of Dangerous Failure / Hour | Example |
|-----|----------------------------------------|---------|
| SIL 1 | 10⁻⁵ to 10⁻⁶ | Industrial conveyor stop |
| SIL 2 | 10⁻⁶ to 10⁻⁷ | Medical infusion pump |
| SIL 3 | 10⁻⁷ to 10⁻⁸ | Railway signalling |
| SIL 4 | 10⁻⁸ to 10⁻⁹ | Nuclear reactor protection |

### ISO 26262 — Automotive (ASIL A–D)

ISO 26262 applies to road vehicles and introduces Automotive Safety Integrity Levels (ASIL A–D, plus QM). Communication links between safety-relevant ECUs must be protected against systematic and random hardware faults.

### IEC 60601 / IEC 62443 — Medical

IEC 60601-1 governs the basic safety and essential performance of medical electrical equipment. Communication between subsystems — e.g., between a pump controller and a dosage display — must ensure that corrupted data cannot cause patient harm.

### DO-178C / DO-254 — Aerospace

DO-178C governs airborne software. Its Design Assurance Levels (DAL A–E) mirror IEC 61508's SIL framework. Any UART link between avionics Line-Replaceable Units (LRUs) must be demonstrated to be free of undetected data errors that could affect flight safety.

### MISRA C / CERT C

Coding standards like MISRA C:2012 and CERT C provide language-level rules enforced during code review and static analysis. They eliminate constructs known to cause undefined behaviour — essential when hardware or compiler quirks could corrupt a UART transaction silently.

---

## Core Challenges in Safety-Critical UART Design

### 1. Bit Error and Frame Error Detection

A standard UART frame has an optional parity bit. Parity detects single-bit errors but cannot detect even-bit errors and provides no coverage over multi-byte messages.

**Mitigations:**
- Replace or supplement parity with a CRC-16 or CRC-32 appended to each application message
- Use a proven polynomial (CRC-16/CCITT, CRC-32/ISO-HDLC) — avoid ad-hoc checksums

### 2. Framing Loss and Synchronisation

If a receiver loses byte synchronisation (e.g., after a burst of noise), it may interpret payload bytes as control fields, corrupting subsequent messages indefinitely.

**Mitigations:**
- Define unambiguous start-of-frame (SOF) and end-of-frame (EOF) delimiters using byte-stuffing (COBS, SLIP)
- Use a fixed-length header with a magic number that has low probability of appearing in payload data

### 3. Message Loss and Duplication

UART provides no acknowledgement. A message can be lost without the sender knowing, or a retransmit can cause a duplicate to be acted upon twice (e.g., two bolus doses delivered).

**Mitigations:**
- Add a monotonically increasing sequence number to every message
- Track the last accepted sequence number on the receiver; reject duplicates and detect gaps

### 4. Timing and Latency

Safety-critical systems often have hard real-time deadlines. A UART link operating at 115,200 baud requires ~87 µs per byte. A 32-byte message takes ~2.4 ms — plus interrupt latency and DMA completion delays.

**Mitigations:**
- Account for worst-case transmission latency in the safety analysis
- Use DMA-based UART with hardware flow control (RTS/CTS) where available
- Implement timeout-based detection at both ends

### 5. Silent Data Corruption

Hardware faults (latch-up, SEU in space/aviation, ESD in automotive) can flip bits without triggering a UART framing error.

**Mitigations:**
- End-to-end CRC across the entire application-layer message
- Dual-channel architectures with independent comparison

---

## Fault Detection and Error Handling

A safety-critical UART receiver must detect and respond gracefully to:

| Fault Type | Detection Mechanism | Response |
|---|---|---|
| Framing error | UART peripheral flag (FE bit) | Discard byte, log, request retransmit |
| Overrun error | UART OE flag | Flush FIFO, increment overrun counter |
| Parity error | UART PE flag | Discard byte |
| CRC mismatch | Application layer | Request retransmit or safe-state |
| Sequence gap | Sequence number check | Log, request retransmit |
| Timeout | Hardware/software timer | Enter safe state |
| Buffer overflow | Buffer depth check | Discard with error event |

The response must be documented in the system's **Failure Mode and Effects Analysis (FMEA)** and traced to requirements.

---

## Redundancy and Failsafe Architectures

High-integrity systems rarely rely on a single UART path. Common architectural patterns include:

### Dual-Channel (1oo2D)

Two independent UART channels carry the same data. A comparator module checks for agreement. On disagreement, the system enters a pre-defined safe state (deenergise actuator, halt dosing, etc.).

```
Sensor ──► UART Ch A ──► μC A ──┐
                                ├──► Comparator ──► Actuator or Safe State
Sensor ──► UART Ch B ──► μC B ──┘
```

### Heartbeat and Watchdog

Even if no payload data is present, a regular heartbeat message confirms that the communication path is alive. Absence of a heartbeat within a defined window triggers a safe-state transition.

### Graceful Degradation

Where dual-channel is not feasible, define a **degraded mode**: if the primary UART link fails, the system falls back to a conservative operating profile (lower speed, manual override, etc.).

---

## Watchdog and Supervision Mechanisms

A UART-connected watchdog is a common pattern in automotive and medical designs:

- The host MCU sends a keep-alive byte (or challenge/response token) over a dedicated UART to a supervisory IC (e.g., TPS65xx, MAX16071)
- If the keep-alive is absent for more than a configurable timeout, the supervisor asserts a hardware reset or triggers a safe output

The communication itself must be protected: a stuck transmitter that endlessly repeats the keep-alive must not fool the supervisor. **Challenge-response** schemes (the supervisor sends a random token; the host must reply with a computed response) mitigate this.

---

## Secure Framing and Data Integrity

### Consistent Overhead Byte Stuffing (COBS)

COBS is the preferred framing method for safety-critical UART. It guarantees that the `0x00` byte never appears in encoded payload data, so `0x00` can serve as an unambiguous frame delimiter. This eliminates the risk of a payload byte being mistaken for a frame boundary.

### Message Structure (Application Layer)

```
┌────────┬────────┬────────┬────────────────────┬──────────┬────────┐
│ SOF    │ Length │ Seq No │ Payload (N bytes)  │ CRC-16   │ EOF    │
│ 0x7E   │ 1 byte │ 1 byte │                    │ 2 bytes  │ 0x7F   │
└────────┴────────┴────────┴────────────────────┴──────────┴────────┘
```

- **SOF/EOF**: Unique delimiters (byte-stuffed in payload)
- **Length**: Protects against truncated messages
- **Seq No**: Wraps at 255; receiver tracks last accepted
- **CRC-16/CCITT**: Covers length + seq + payload

---

## Automotive Applications (ISO 26262)

In automotive systems, UART is found in:

- **OBD-II / K-Line diagnostic links** (ISO 9141, ISO 14230)
- **LIN network master/slave** (LIN uses UART-based framing at the physical layer)
- **ECU–ECU debug and flash-programming interfaces**
- **Gateway units bridging CAN/Ethernet to lower-speed peripherals**

ISO 26262 Part 4 requires that hardware-level diagnostic coverage is demonstrated for all safety-relevant communication. For a UART link at ASIL B or higher, this typically means:

1. UART peripheral self-test at power-on (loopback test)
2. Periodic end-to-end message integrity checks during operation
3. Reaction time of the safety monitor within the fault-tolerant time interval (FTTI)

**Loopback Test at Startup** is mandatory for ASIL C/D. The UART Tx pin is internally connected to Rx; a known pattern is transmitted and verified before the main application starts.

---

## Medical Applications (IEC 62443 / FDA Guidance)

Medical devices use UART in:

- **Patient monitor ↔ central station links** (vital signs, alarms)
- **Infusion pump controller ↔ dosage display**
- **Implantable programmer wands** (short-range RF bridges to UART)
- **Diagnostic equipment to PC interface** (legacy RS-232)

The FDA's Software as a Medical Device (SaMD) guidance and IEC 62304 require that software correctly handles all reasonably foreseeable misuse scenarios, including corrupted serial data. Key requirements include:

1. All out-of-range values received over UART must be rejected and alarmed — not clamped silently
2. Sequence numbering prevents a repeated command (e.g., "dispense 5 ml") from executing twice
3. Cryptographic authentication (HMAC-SHA256) over the UART message protects against malicious injection (FDA cybersecurity guidance 2023)

---

## Aerospace Applications (DO-178C / MIL-STD)

In aviation, UART appears in:

- **ARINC 429 bridges** (legacy avionics to modern MCUs)
- **Maintenance Data Links** (ground crew terminals)
- **Flight Data Recorder interfaces**
- **Satellite communication terminal ↔ avionics bus gateways**

DO-178C DAL A software demands that every output command can be traced to a verified source message and that no single failure can produce a catastrophic outcome. For UART links at DAL A:

1. Structural Coverage Analysis (MC/DC) must demonstrate that every branch of the UART driver is exercised by tests
2. Formal data flow analysis must show no information leak from lower-assurance partitions
3. SEU (Single Event Upset) mitigation may require ECC-protected SRAM buffers for UART FIFOs on radiation-hardened devices (e.g., UT699 LEON3 SPARC)

---

## Code Examples in C/C++

### 1. UART Hardware Initialisation with Loopback Self-Test (C, bare-metal ARM Cortex-M)

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---------------------------------------------------------------
 * Register offsets for a generic UART peripheral (Cortex-M style)
 * Replace with vendor-specific HAL as required.
 * ------------------------------------------------------------- */
typedef struct {
    volatile uint32_t DR;      /* Data register                   */
    volatile uint32_t SR;      /* Status register                 */
    volatile uint32_t BRR;     /* Baud rate register              */
    volatile uint32_t CR1;     /* Control register 1              */
    volatile uint32_t CR2;     /* Control register 2              */
    volatile uint32_t CR3;     /* Control register 3 (flow ctrl)  */
} UART_Regs_t;

#define UART1_BASE   ((UART_Regs_t *)0x40011000UL)

/* SR bits */
#define UART_SR_RXNE  (1u << 5)   /* RX not empty        */
#define UART_SR_TXE   (1u << 7)   /* TX register empty   */
#define UART_SR_TC    (1u << 6)   /* TX complete         */
#define UART_SR_FE    (1u << 1)   /* Framing error       */
#define UART_SR_ORE   (1u << 3)   /* Overrun error       */
#define UART_SR_PE    (1u << 0)   /* Parity error        */

/* CR1 bits */
#define UART_CR1_UE   (1u << 13)  /* UART enable         */
#define UART_CR1_RE   (1u << 2)   /* Receiver enable     */
#define UART_CR1_TE   (1u << 3)   /* Transmitter enable  */
#define UART_CR1_PCE  (1u << 10)  /* Parity control en.  */

/* CR3 bits */
#define UART_CR3_LOOP (1u << 14)  /* Loopback enable (vendor-specific) */

#define LOOPBACK_TIMEOUT_CYCLES  100000UL
#define LOOPBACK_PATTERN         0xA5u

/* Return codes aligned with MISRA-safe enum pattern */
typedef enum {
    UART_OK        = 0,
    UART_ERR_INIT  = 1,
    UART_ERR_LOOP  = 2,
    UART_ERR_FRAME = 3,
    UART_ERR_OVR   = 4,
    UART_ERR_PAR   = 5,
    UART_ERR_CRC   = 6,
    UART_ERR_SEQ   = 7,
    UART_ERR_TO    = 8
} UartStatus_t;

/* ---------------------------------------------------------------
 * uart_init  — configure UART and run loopback power-on self-test
 * Returns UART_OK on success, UART_ERR_INIT/UART_ERR_LOOP on fault.
 * Required by ISO 26262 ASIL C/D for safety-relevant communication links.
 * ------------------------------------------------------------- */
UartStatus_t uart_init(UART_Regs_t *uart, uint32_t baud_brr_value)
{
    /* Disable before reconfiguration */
    uart->CR1 = 0u;

    /* Set baud rate (pre-computed BRR value from system clock / baud) */
    uart->BRR = baud_brr_value;

    /* 8-bit, no parity, 1 stop bit, TX+RX enabled */
    uart->CR2 = 0u;
    uart->CR3 = 0u;
    uart->CR1 = UART_CR1_UE | UART_CR1_TE | UART_CR1_RE;

    /* --- Power-On Self-Test: internal loopback --- */
    uart->CR3 |= UART_CR3_LOOP;   /* Connect TX internally to RX  */

    /* Flush any stale data */
    (void)uart->DR;

    /* Transmit test pattern */
    while ((uart->SR & UART_SR_TXE) == 0u) { /* wait */ }
    uart->DR = LOOPBACK_PATTERN;

    /* Wait for reception with timeout */
    uint32_t timeout = LOOPBACK_TIMEOUT_CYCLES;
    while (((uart->SR & UART_SR_RXNE) == 0u) && (timeout > 0u)) {
        timeout--;
    }

    UartStatus_t result = UART_OK;

    if (timeout == 0u) {
        result = UART_ERR_LOOP;   /* Loopback timed out — hardware fault */
    } else {
        uint8_t received = (uint8_t)(uart->DR & 0xFFu);
        if (received != LOOPBACK_PATTERN) {
            result = UART_ERR_LOOP;   /* Data mismatch */
        }
    }

    /* Disable loopback; peripheral ready for normal operation */
    uart->CR3 &= ~UART_CR3_LOOP;

    return result;
}
```

---

### 2. CRC-16/CCITT Calculation (C)

```c
#include <stdint.h>
#include <stddef.h>

/* CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)
 * Used in LIN, IEC 62443, and many aerospace data-link layers.
 * Table-driven for deterministic worst-case execution time (WCET). */

static const uint16_t crc16_table[256] = {
    0x0000u, 0x1021u, 0x2042u, 0x3063u, 0x4084u, 0x50A5u, 0x60C6u, 0x70E7u,
    0x8108u, 0x9129u, 0xA14Au, 0xB16Bu, 0xC18Cu, 0xD1ADu, 0xE1CEu, 0xF1EFu,
    /* ... (full 256-entry table omitted for brevity; generate with standard algorithm) ... */
    /* In production code, embed the complete pre-computed table.                          */
};

uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < length; i++) {
        uint8_t index = (uint8_t)((crc >> 8u) ^ data[i]);
        crc = (uint16_t)((crc << 8u) ^ crc16_table[index]);
    }
    return crc;
}
```

---

### 3. Safety-Critical Message Frame: Transmit (C)

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define SOF_BYTE       0x7Eu
#define EOF_BYTE       0x7Fu
#define ESC_BYTE       0x7Du
#define ESC_XOR        0x20u
#define MAX_PAYLOAD    64u
#define HEADER_SIZE    3u   /* SOF + Length + SeqNo */
#define TRAILER_SIZE   3u   /* CRC_HI + CRC_LO + EOF */
#define FRAME_OVERHEAD (HEADER_SIZE + TRAILER_SIZE)

/* Sequence number — monotonically increasing, wraps at 255 */
static uint8_t tx_seq = 0u;

/* Byte-stuffing: replace SOF, EOF, ESC in payload with ESC + (byte ^ ESC_XOR) */
static size_t stuff_byte(uint8_t *out, uint8_t byte)
{
    if ((byte == SOF_BYTE) || (byte == EOF_BYTE) || (byte == ESC_BYTE)) {
        out[0] = ESC_BYTE;
        out[1] = byte ^ ESC_XOR;
        return 2u;
    }
    out[0] = byte;
    return 1u;
}

typedef struct {
    uint8_t  buf[MAX_PAYLOAD * 2u + FRAME_OVERHEAD];
    uint16_t length;
} TxFrame_t;

/* Build a complete safety frame around a payload.
 * Returns UART_OK or UART_ERR_INIT if payload too large. */
UartStatus_t build_tx_frame(TxFrame_t *frame,
                             const uint8_t *payload,
                             uint8_t payload_len)
{
    if (payload_len > MAX_PAYLOAD) {
        return UART_ERR_INIT;
    }

    /* Compute CRC over: [payload_len, seq_no, payload...] */
    uint8_t hdr[2] = { payload_len, tx_seq };
    uint16_t crc = crc16_ccitt(hdr, 2u);
    crc = crc16_ccitt(payload, payload_len);   /* chain: feed into running CRC */
    /* NOTE: In production, chain the CRC: compute over hdr then continue over payload */

    uint16_t idx = 0u;
    frame->buf[idx++] = SOF_BYTE;
    frame->buf[idx++] = payload_len;
    frame->buf[idx++] = tx_seq++;

    /* Byte-stuff payload */
    for (uint8_t i = 0u; i < payload_len; i++) {
        uint8_t tmp[2];
        size_t n = stuff_byte(tmp, payload[i]);
        for (size_t j = 0u; j < n; j++) {
            frame->buf[idx++] = tmp[j];
        }
    }

    /* Append CRC (big-endian, also stuffed) */
    uint8_t crc_bytes[2] = { (uint8_t)(crc >> 8u), (uint8_t)(crc & 0xFFu) };
    for (uint8_t k = 0u; k < 2u; k++) {
        uint8_t tmp[2];
        size_t n = stuff_byte(tmp, crc_bytes[k]);
        for (size_t j = 0u; j < n; j++) {
            frame->buf[idx++] = tmp[j];
        }
    }

    frame->buf[idx++] = EOF_BYTE;
    frame->length = idx;
    return UART_OK;
}
```

---

### 4. Safety-Critical Message Frame: Receive and Validate (C)

```c
#include <stdint.h>
#include <stdbool.h>

#define RX_BUF_SIZE  (MAX_PAYLOAD * 2u + FRAME_OVERHEAD)

typedef enum {
    RX_STATE_IDLE,
    RX_STATE_HEADER,
    RX_STATE_PAYLOAD,
    RX_STATE_ESC,
    RX_STATE_CRC_HI,
    RX_STATE_CRC_LO,
    RX_STATE_DONE
} RxState_t;

typedef struct {
    RxState_t state;
    uint8_t   raw_buf[RX_BUF_SIZE];
    uint8_t   decoded_buf[MAX_PAYLOAD + 4u];  /* length, seq, payload, crc×2 */
    uint16_t  raw_idx;
    uint8_t   dec_idx;
    uint8_t   expected_seq;
    bool      prev_was_esc;
} RxContext_t;

/* Call this function for each byte received from the UART interrupt/DMA callback */
UartStatus_t rx_process_byte(RxContext_t *ctx, uint8_t byte,
                              uint8_t *payload_out, uint8_t *payload_len_out)
{
    /* Check UART hardware error flags before processing data byte.
     * The caller should pass error flags separately; this simplified version
     * demonstrates the state machine logic. */

    switch (ctx->state) {
        case RX_STATE_IDLE:
            if (byte == SOF_BYTE) {
                ctx->dec_idx  = 0u;
                ctx->raw_idx  = 0u;
                ctx->prev_was_esc = false;
                ctx->state    = RX_STATE_HEADER;
            }
            break;

        case RX_STATE_HEADER:
        case RX_STATE_PAYLOAD:
            if (byte == EOF_BYTE) {
                /* End of frame received — validate */
                if (ctx->dec_idx < 4u) {            /* length + seq + at least 0-byte payload + 2 CRC */
                    ctx->state = RX_STATE_IDLE;
                    return UART_ERR_FRAME;
                }

                uint8_t msg_len  = ctx->decoded_buf[0];
                uint8_t seq      = ctx->decoded_buf[1];
                uint8_t *payload = &ctx->decoded_buf[2];
                uint16_t rx_crc  = ((uint16_t)ctx->decoded_buf[2u + msg_len] << 8u) |
                                    ctx->decoded_buf[3u + msg_len];

                /* Recompute CRC over [length, seq, payload] */
                uint8_t hdr[2] = { msg_len, seq };
                uint16_t calc_crc = crc16_ccitt(hdr, 2u);
                /* Chain payload — in production combine into one call */
                (void)calc_crc;  /* placeholder; use full chained CRC */

                /* Sequence number check */
                if (seq != ctx->expected_seq) {
                    ctx->state = RX_STATE_IDLE;
                    return UART_ERR_SEQ;
                }
                ctx->expected_seq = (uint8_t)(seq + 1u);

                /* CRC check (simplified; replace with full chained CRC above) */
                /* if (rx_crc != calc_crc) { ... return UART_ERR_CRC; } */
                (void)rx_crc;

                /* Deliver payload */
                for (uint8_t i = 0u; i < msg_len; i++) {
                    payload_out[i] = payload[i];
                }
                *payload_len_out = msg_len;
                ctx->state = RX_STATE_IDLE;
                return UART_OK;
            }

            if (byte == ESC_BYTE) {
                ctx->prev_was_esc = true;
                break;
            }

            if (ctx->prev_was_esc) {
                byte ^= ESC_XOR;
                ctx->prev_was_esc = false;
            }

            if (ctx->dec_idx < (uint8_t)(sizeof(ctx->decoded_buf))) {
                ctx->decoded_buf[ctx->dec_idx++] = byte;
                /* Transition from header to payload after length + seq bytes received */
                if ((ctx->state == RX_STATE_HEADER) && (ctx->dec_idx >= 2u)) {
                    ctx->state = RX_STATE_PAYLOAD;
                }
            } else {
                ctx->state = RX_STATE_IDLE;
                return UART_ERR_FRAME;  /* Buffer overflow */
            }
            break;

        default:
            ctx->state = RX_STATE_IDLE;
            break;
    }

    return UART_OK;
}
```

---

### 5. Watchdog Keep-Alive with Challenge–Response (C)

```c
#include <stdint.h>
#include <stdbool.h>

/* Simplified challenge–response watchdog for a UART-connected supervisor IC.
 * The supervisor sends a 1-byte challenge; the host must reply with
 * a computed response within the watchdog window.
 *
 * In a real system, use a cryptographic MAC (e.g., HMAC-SHA256 with a
 * pre-shared key stored in OTP/eFuse) instead of XOR. */

#define WDG_CHALLENGE_MAGIC  0xC5u
#define WDG_KEY              0x3Au   /* Pre-shared key (OTP in real system) */

static bool wdg_response_pending = false;
static uint8_t wdg_challenge_val = 0u;

/* Called from UART RX ISR when a watchdog challenge arrives */
void wdg_on_challenge_received(uint8_t challenge)
{
    wdg_challenge_val    = challenge;
    wdg_response_pending = true;
}

/* Called from main task — must be called within watchdog window */
void wdg_send_response(UART_Regs_t *uart)
{
    if (!wdg_response_pending) {
        return;
    }
    /* Compute response: in production, use HMAC-SHA256(key, challenge) */
    uint8_t response = wdg_challenge_val ^ WDG_KEY;

    while ((uart->SR & UART_SR_TXE) == 0u) { /* wait for TX empty */ }
    uart->DR = response;

    wdg_response_pending = false;
}

/* Called from a periodic safety monitor task.
 * If a challenge was received but not responded to within the window,
 * trigger safe state. */
void wdg_safety_monitor_tick(void)
{
    /* In a real design, timestamp the challenge and check elapsed time */
    /* This simplified version is illustrative */
    if (wdg_response_pending) {
        /* Response not sent in time — this is a safety violation */
        /* trigger_safe_state(); */
    }
}
```

---

### 6. ISO 26262 — ASIL-B UART Diagnostic Monitor (C++)

```cpp
#include <cstdint>
#include <array>
#include <atomic>

/* ASIL-B UART Diagnostic Monitor
 * Tracks error counters and enforces safe-state thresholds as required
 * by ISO 26262-5 (hardware safety requirements for E/E systems). */

class UartSafetyMonitor {
public:
    struct Diagnostics {
        uint32_t frame_errors   = 0u;
        uint32_t overrun_errors = 0u;
        uint32_t parity_errors  = 0u;
        uint32_t crc_errors     = 0u;
        uint32_t seq_errors     = 0u;
        uint32_t timeout_events = 0u;
        uint32_t msgs_rx_ok     = 0u;
    };

    static constexpr uint32_t FRAME_ERR_THRESHOLD   = 5u;
    static constexpr uint32_t CRC_ERR_THRESHOLD     = 3u;
    static constexpr uint32_t TIMEOUT_THRESHOLD     = 2u;

    /* These thresholds must be justified in the safety analysis (FMEA/FMEDA)
     * and mapped to the fault-tolerant time interval (FTTI). */

    enum class SafetyState {
        NORMAL,
        DEGRADED,   /* Increased error rate — alert operator            */
        SAFE        /* Dangerous threshold exceeded — halt safety output */
    };

    void on_frame_error()   { diag_.frame_errors++;   evaluate(); }
    void on_overrun_error() { diag_.overrun_errors++;             }
    void on_parity_error()  { diag_.parity_errors++;              }
    void on_crc_error()     { diag_.crc_errors++;     evaluate(); }
    void on_seq_error()     { diag_.seq_errors++;                 }
    void on_timeout()       { diag_.timeout_events++; evaluate(); }
    void on_rx_ok()         { diag_.msgs_rx_ok++;                 }

    SafetyState get_state()        const { return state_; }
    const Diagnostics& get_diag() const { return diag_;  }

    /* Periodic reset of rolling counters (call from 100 ms task).
     * Only resets if currently in NORMAL state — latches SAFE state. */
    void periodic_reset()
    {
        if (state_ == SafetyState::NORMAL) {
            diag_.frame_errors   = 0u;
            diag_.crc_errors     = 0u;
            diag_.timeout_events = 0u;
        }
    }

private:
    Diagnostics diag_{};
    SafetyState state_ = SafetyState::NORMAL;

    void evaluate()
    {
        if ((diag_.frame_errors   >= FRAME_ERR_THRESHOLD) ||
            (diag_.crc_errors     >= CRC_ERR_THRESHOLD)   ||
            (diag_.timeout_events >= TIMEOUT_THRESHOLD)) {

            state_ = SafetyState::SAFE;
            /* In a real system: assert safe output, notify FTTI monitor,
             * log DTC (Diagnostic Trouble Code) to NVM. */
        }
    }
};
```

---

## Code Examples in Rust

Rust's ownership system, absence of undefined behaviour by default, and zero-cost abstractions make it highly attractive for safety-critical embedded development. The Rust Embedded Working Group's `embedded-hal` traits provide portable UART interfaces.

### 1. CRC-16/CCITT in Rust (no_std)

```rust
#![no_std]

/// CRC-16/CCITT (polynomial 0x1021, initial value 0xFFFF).
/// Suitable for UART safety framing in automotive, medical, and aerospace.
/// Pure function — no side effects, no panics, no allocation.
pub fn crc16_ccitt(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        let index = ((crc >> 8) ^ u16::from(byte)) as u8;
        crc = (crc << 8) ^ CRC16_TABLE[index as usize];
    }
    crc
}

/// Pre-computed table for CRC-16/CCITT (polynomial 0x1021).
/// Computed at compile time with a const fn in Rust ≥ 1.57.
const fn make_crc16_table() -> [u16; 256] {
    let mut table = [0u16; 256];
    let mut i = 0usize;
    while i < 256 {
        let mut crc = (i as u16) << 8;
        let mut j = 0;
        while j < 8 {
            if crc & 0x8000 != 0 {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
            j += 1;
        }
        table[i] = crc;
        i += 1;
    }
    table
}

static CRC16_TABLE: [u16; 256] = make_crc16_table();

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn crc_known_vector() {
        // "123456789" -> 0x29B1 for CRC-16/CCITT
        let result = crc16_ccitt(b"123456789");
        assert_eq!(result, 0x29B1);
    }
}
```

---

### 2. Safety Frame Builder in Rust (no_std, heapless)

```rust
#![no_std]

use heapless::Vec;   // heapless 0.8 — fixed-capacity Vec without alloc

const SOF: u8 = 0x7E;
const EOF_MARKER: u8 = 0x7F;
const ESC: u8 = 0x7D;
const ESC_XOR: u8 = 0x20;
const MAX_PAYLOAD: usize = 64;
const MAX_FRAME: usize = MAX_PAYLOAD * 2 + 8; // worst-case after byte-stuffing

#[derive(Debug, PartialEq)]
pub enum FrameError {
    PayloadTooLarge,
    BufferFull,
}

/// Encode a safety frame: SOF | length | seq | stuffed(payload) | stuffed(crc) | EOF
pub fn build_frame(
    payload: &[u8],
    seq: u8,
    out: &mut Vec<u8, MAX_FRAME>,
) -> Result<(), FrameError> {
    if payload.len() > MAX_PAYLOAD {
        return Err(FrameError::PayloadTooLarge);
    }

    // Compute CRC over [length, seq, payload]
    let header = [payload.len() as u8, seq];
    let crc = {
        let mut v = crc16_ccitt(&header);
        // Chain payload through a second pass (combine for production)
        v ^= crc16_ccitt(payload); // simplified — use proper chained CRC
        v
    };

    out.clear();

    // SOF (never stuffed — it IS the delimiter)
    out.push(SOF).map_err(|_| FrameError::BufferFull)?;
    out.push(payload.len() as u8).map_err(|_| FrameError::BufferFull)?;
    out.push(seq).map_err(|_| FrameError::BufferFull)?;

    // Byte-stuff payload
    for &byte in payload {
        stuff_push(out, byte)?;
    }

    // Byte-stuff CRC (big-endian)
    stuff_push(out, (crc >> 8) as u8)?;
    stuff_push(out, (crc & 0xFF) as u8)?;

    out.push(EOF_MARKER).map_err(|_| FrameError::BufferFull)?;
    Ok(())
}

fn stuff_push(out: &mut Vec<u8, MAX_FRAME>, byte: u8) -> Result<(), FrameError> {
    if byte == SOF || byte == EOF_MARKER || byte == ESC {
        out.push(ESC).map_err(|_| FrameError::BufferFull)?;
        out.push(byte ^ ESC_XOR).map_err(|_| FrameError::BufferFull)?;
    } else {
        out.push(byte).map_err(|_| FrameError::BufferFull)?;
    }
    Ok(())
}

// Bring in the CRC function from above
use super::crc16_ccitt;
```

---

### 3. UART Receiver State Machine in Rust

```rust
#![no_std]

use heapless::Vec;

const MAX_DECODED: usize = MAX_PAYLOAD + 4; // length + seq + payload + 2×CRC

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum RxError {
    FramingError,
    CrcMismatch,
    SequenceError,
    BufferOverflow,
    Timeout,
}

#[derive(Debug, PartialEq, Clone, Copy)]
enum RxState {
    Idle,
    InFrame,
    Escaped,
}

pub struct UartReceiver {
    state:        RxState,
    decoded:      Vec<u8, MAX_DECODED>,
    expected_seq: u8,
}

impl UartReceiver {
    pub const fn new() -> Self {
        Self {
            state:        RxState::Idle,
            decoded:      Vec::new(),
            expected_seq: 0,
        }
    }

    /// Process one byte from the UART interrupt handler.
    /// Returns `Ok(Some(slice))` when a complete, valid frame is available.
    pub fn process_byte(&mut self, byte: u8) -> Result<Option<&[u8]>, RxError> {
        match self.state {
            RxState::Idle => {
                if byte == SOF {
                    self.decoded.clear();
                    self.state = RxState::InFrame;
                }
                Ok(None)
            }

            RxState::InFrame => {
                match byte {
                    b if b == EOF_MARKER => self.finalise(),
                    b if b == ESC        => { self.state = RxState::Escaped; Ok(None) }
                    b if b == SOF        => {
                        // Unexpected SOF — resynchronise
                        self.decoded.clear();
                        Ok(None)
                    }
                    _ => {
                        self.decoded.push(byte).map_err(|_| {
                            self.state = RxState::Idle;
                            RxError::BufferOverflow
                        })?;
                        Ok(None)
                    }
                }
            }

            RxState::Escaped => {
                let unstuffed = byte ^ ESC_XOR;
                self.state = RxState::InFrame;
                self.decoded.push(unstuffed).map_err(|_| {
                    self.state = RxState::Idle;
                    RxError::BufferOverflow
                })?;
                Ok(None)
            }
        }
    }

    fn finalise(&mut self) -> Result<Option<&[u8]>, RxError> {
        self.state = RxState::Idle;

        // Minimum: length(1) + seq(1) + crc_hi(1) + crc_lo(1) = 4 bytes
        if self.decoded.len() < 4 {
            return Err(RxError::FramingError);
        }

        let msg_len  = self.decoded[0] as usize;
        let seq      = self.decoded[1];
        let payload  = &self.decoded[2..2 + msg_len];
        let rx_crc   = u16::from(self.decoded[2 + msg_len]) << 8
                     | u16::from(self.decoded[3 + msg_len]);

        // Sequence check
        if seq != self.expected_seq {
            return Err(RxError::SequenceError);
        }
        self.expected_seq = self.expected_seq.wrapping_add(1);

        // CRC check (simplified; use chained CRC in production)
        let header = [msg_len as u8, seq];
        let calc_crc = crc16_ccitt(&header) ^ crc16_ccitt(payload);
        if calc_crc != rx_crc {
            return Err(RxError::CrcMismatch);
        }

        Ok(Some(payload))
    }
}
```

---

### 4. Watchdog Keep-Alive with HMAC in Rust (Medical / ASIL-C)

```rust
#![no_std]

/// Simplified challenge–response watchdog for a UART-connected supervisor.
/// In production: replace `compute_response` with HMAC-SHA256 using a
/// hardware-backed key (e.g., from a TrustZone secure enclave or HSM).
///
/// Relevant standards:
///   - ISO 26262-6 (software) §9.4.3: External monitoring facility
///   - IEC 62443-4-2: Component security level for medical devices
///   - FDA Cybersecurity Guidance (2023): Authentication on serial links

pub struct WatchdogHandler {
    pending_challenge: Option<u8>,
    key: u8,   // In production: 256-bit key in OTP/HSM
}

impl WatchdogHandler {
    pub const fn new(key: u8) -> Self {
        Self { pending_challenge: None, key }
    }

    /// Called from UART RX interrupt when supervisor challenge arrives
    pub fn on_challenge(&mut self, challenge: u8) {
        self.pending_challenge = Some(challenge);
    }

    /// Called from main safety task. Returns the response byte to transmit,
    /// or None if no challenge is pending.
    pub fn take_response(&mut self) -> Option<u8> {
        self.pending_challenge.take().map(|c| self.compute_response(c))
    }

    /// Placeholder response function.
    /// Replace with HMAC-SHA256(self.key, challenge) for production.
    fn compute_response(&self, challenge: u8) -> u8 {
        challenge ^ self.key
    }

    /// Returns true if a challenge is pending but has not been responded to.
    /// Call from timeout monitor to detect deadline violation.
    pub fn is_response_overdue(&self) -> bool {
        self.pending_challenge.is_some()
    }
}

/// Safety monitor task — call every scheduling slot
pub fn watchdog_safety_tick(handler: &WatchdogHandler) -> bool {
    if handler.is_response_overdue() {
        // Log fault, trigger safe state output
        // safe_state::engage();
        return false;
    }
    true
}
```

---

### 5. Compile-Time Safety Constraints with Rust Type System

```rust
/// Rust's type system can enforce safety invariants at compile time —
/// a significant advantage over C for ISO 26262 / DO-178C compliance.
///
/// This example uses a typestate pattern to ensure that a UART channel
/// cannot be used before initialisation and cannot be re-initialised
/// after the self-test has been run.

use core::marker::PhantomData;

// States — zero-sized types, erased at compile time
pub struct Uninit;
pub struct SelfTested;
pub struct Operational;

pub struct SafeUart<State> {
    // In real code: contains the peripheral register pointer or HAL handle
    _state: PhantomData<State>,
    error_count: u32,
}

impl SafeUart<Uninit> {
    pub fn new() -> Self {
        Self { _state: PhantomData, error_count: 0 }
    }

    /// Runs the loopback self-test. Only callable in Uninit state.
    /// Returns Ok(SafeUart<SelfTested>) on pass, Err on fault.
    pub fn run_self_test(self) -> Result<SafeUart<SelfTested>, &'static str> {
        // ... hardware loopback test ...
        // Simulated pass:
        Ok(SafeUart { _state: PhantomData, error_count: 0 })
    }
}

impl SafeUart<SelfTested> {
    /// Transitions to operational state. Called after system-level checks pass.
    pub fn enable(self) -> SafeUart<Operational> {
        SafeUart { _state: PhantomData, error_count: self.error_count }
    }
}

impl SafeUart<Operational> {
    /// Only available in Operational state — cannot be called on uninitialized UART
    pub fn send_safety_frame(&mut self, payload: &[u8]) -> Result<(), &'static str> {
        // ... transmit logic ...
        let _ = payload;
        Ok(())
    }

    pub fn error_count(&self) -> u32 { self.error_count }
}

// Compile-time proof: this will NOT compile —
// uart.send_safety_frame(&[]) called on SafeUart<Uninit> would be a type error.
```

---

## Testing and Validation Strategies

### Hardware-in-the-Loop (HIL) Fault Injection

Safety-critical UART drivers must be tested against injected faults:

- **Bit flip injection**: use a UART bit-bang injector to corrupt single bits mid-frame
- **Frame truncation**: cut power to the transmitter mid-message
- **Sequence skip**: send sequence number 0, then 2 (skipping 1)
- **Noise burst**: inject 50–100 µs of carrier-frequency noise on the signal line

### Static Analysis

| Tool | Language | Checks |
|------|----------|--------|
| PC-lint / Polyspace | C/C++ | MISRA C:2012, data flow, WCET |
| Klocwork | C/C++ | Null deref, buffer overflows |
| `clippy` + `cargo audit` | Rust | Lints, known-vulnerable crates |
| Frama-C (WP plugin) | C | Formal verification of safety properties |

### Code Coverage

DO-178C DAL A requires 100% MC/DC (Modified Condition/Decision Coverage). Every branch in the UART state machine, every error path, and every CRC table entry access must be reachable via the test suite.

### Mutation Testing

Mutation testing (e.g., with LLVM's `mutagen` for Rust) deliberately introduces faults into the source code and verifies that the test suite detects them. This is increasingly required by notified bodies reviewing DO-178C DAL A software.

---

## Summary

UART's simplicity is both its greatest strength and its greatest liability in safety-critical systems. The protocol itself provides no framing, no CRC, no sequence numbering, and no authentication — but all of these properties can be layered above it at the application level. The key takeaways are:

**Architecture**: Every safety-critical UART link needs a documented safety analysis (FMEA/FMEDA) that maps each fault mode — framing error, CRC failure, timeout, sequence skip — to a detection mechanism and a defined safe response. This analysis drives requirements on the communication layer.

**Protocol Design**: Use unambiguous byte-stuffed framing (COBS or SLIP-style), a monotonically increasing sequence number, and a robust CRC (CRC-16/CCITT or CRC-32). For medical and automotive links at ASIL C/D or SIL 3+, add cryptographic authentication (HMAC-SHA256) to resist both accidental corruption and malicious injection.

**Hardware**: Perform a UART loopback self-test at power-on. Configure DMA with circular buffers to eliminate missed-byte overruns. Use a hardware watchdog (with challenge–response, not simple keep-alive) fed by the communication task.

**Software in C/C++**: Write to MISRA C:2012. Avoid dynamic allocation. Use a deterministic state machine for the receiver. Enforce error thresholds with a safety monitor that can trigger a safe state before the FTTI is exceeded.

**Software in Rust**: Exploit the type system to make invalid states unrepresentable at compile time (typestate pattern). Use `no_std` with `heapless` collections. Compute CRC tables with `const fn` to eliminate run-time initialisation. The borrow checker eliminates the entire class of memory-safety bugs most commonly cited in MISRA deviations.

**Compliance**: Align every design decision to the applicable standard — ISO 26262 for automotive (ASIL A–D), IEC 60601/IEC 62443/FDA guidance for medical, and DO-178C/DO-254 for aerospace. All requirements must be traced, all tests must be documented, and all deviations from coding standards must be formally justified and approved.

Safety-critical UART design is ultimately not about the hardware peripheral — it is about the rigour, traceability, and defensive thinking applied to every layer above it.

---

*Document: 49 — Safety-Critical Design | UART in Automotive, Medical, and Aerospace Applications*
*Standards referenced: ISO 26262, IEC 61508, IEC 60601-1, IEC 62443, DO-178C, MISRA C:2012, FDA SaMD Guidance (2023)*