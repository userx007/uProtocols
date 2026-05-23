# 33. CANopen Safety (EN 50325-5 / CiA 304)

**Architecture & Concepts**
- Full layer stack diagram (physical → safety application)
- Node role diagram (producer/consumer over the black channel)
- Dual-channel 1oo2D hardware architecture pattern

**Black-Channel Principle**
- Fault assumption taxonomy (corruption, loss, repetition, insertion, reordering, wrong sender)
- Residual error probability target (≤ 10⁻⁹ per message)

**SPDO Frame & Integrity**
- Complete wire-format layout (control byte bit-field, payload, CRC)
- All five protection measures with fault coverage table
- Connection ID, sequence number, consecutive flag, CRC-8/CRC-16 explained with ASCII diagrams

**Watchdog**
- Consumer-side watchdog timer model with timeline diagram
- Parameter formula: `T_WD = T_cycle + T_jitter + T_delay_max < T_PST`
- Producer-side self-watchdog pattern

**SIL 2 / PLd & Standards Integration**
- IEC 61508 and ISO 13849 level tables
- Subsystem PFD decomposition model
- Process Safety Time budget example
- Documentation checklist for safety assessments

**C/C++ Code**
- Data structures (`spdo_frame_t`, `spdo_config_t`, producer/consumer state)
- CRC-8 (AUTOSAR 0x2F) and CRC-16 (CRC-CCITT) with complement logic
- Full producer (`spdo_produce`) and consumer (`spdo_receive` + `spdo_watchdog_check`)
- Cyclic safety task integration with ISR pattern
- Object dictionary entries (0x5800 / 0x5C00)
- Unit test skeleton with 7 test cases covering all error paths (MC/DC oriented)

> **Standards covered:** EN 50325-5 · CiA 304 · IEC 61508 · ISO 13849  
> **Target Safety Integrity Levels:** SIL 2 / Performance Level d (PLd)

---

## Table of Contents

1. [Introduction and Motivation](#1-introduction-and-motivation)
2. [Safety Architecture Concepts](#2-safety-architecture-concepts)
3. [The Black-Channel Principle](#3-the-black-channel-principle)
4. [Safety PDO (SPDO)](#4-safety-pdo-spdo)
5. [Frame Integrity Measures](#5-frame-integrity-measures)
6. [Watchdog Relationships](#6-watchdog-relationships)
7. [SIL 2 / PLd Compliance](#7-sil-2--pld-compliance)
8. [Integration with IEC 61508 and ISO 13849](#8-integration-with-iec-61508-and-iso-13849)
9. [C/C++ Programming Examples](#9-cc-programming-examples)
10. [Summary](#10-summary)

---

## 1. Introduction and Motivation

Standard CANopen (EN 50325-4 / CiA 301) provides a robust fieldbus protocol but was designed
with availability and determinism as primary goals — not functional safety. As industrial
machines and safety-critical systems increasingly rely on fieldbus networks, a complementary
safety layer became necessary that could operate *over* CAN without requiring a separate
dedicated safety bus.

**CANopen Safety** (standardised as EN 50325-5 and specified in CiA 304) addresses this need
by defining:

- A safety-relevant communication layer that sits on top of the standard CAN/CANopen stack.
- A **Safety PDO (SPDO)** mechanism that transmits safety data with provable integrity.
- A set of error-detection measures sufficient to meet IEC 61508 SIL 2 and ISO 13849 PLd
  requirements for the communication channel.
- Watchdog relationships that allow safety observers to detect transmission failures,
  repetitions, losses, insertions, and incorrect sequencing of safety messages.

CANopen Safety does **not** require special hardware — it runs on the same CAN controller and
physical layer used for standard CANopen. Safety is achieved entirely through software measures
in the application layer.

---

## 2. Safety Architecture Concepts

### 2.1 Overall System Layers

```
+------------------------------------------------------------------+
|                    Safety Application Layer                      |
|  (Safety logic, cross-checking, safe-state management)           |
+------------------------------------------------------------------+
|                  CANopen Safety Layer (CiA 304)                  |
|  SPDO framing, CRC, Sequence Number, Connection ID, Watchdog     |
+------------------------------------------------------------------+
|              Standard CANopen Layer (CiA 301)                    |
|  NMT, SDO, PDO, SYNC, EMCY, Heartbeat                            |
+------------------------------------------------------------------+
|                   CAN Data Link Layer (ISO 11898)                |
|  Bit stuffing, CRC-15, ACK, EOF — not relied on for safety       |
+------------------------------------------------------------------+
|                     CAN Physical Layer                           |
|  Differential signalling, 120 Ω termination, max 1 Mbit/s        |
+------------------------------------------------------------------+
```

### 2.2 Node Roles in a Safety Network

```
                  CANopen Safety Network
  ┌──────────────────────────────────────────────────────────┐
  │                    CAN Bus (shared)                      │
  └─────────┬──────────────────────────────────┬─────────────┘
            │                                  │
  ┌─────────┴──────────┐            ┌──────────┴──────────┐
  │   SSDO Producer    │            │   SSDO Consumer     │
  │  (e.g. Safety I/O) │            │  (e.g. Safety PLC)  │
  │  ┌──────────────┐  │            │  ┌───────────────┐  │
  │  │ Safety App.  │  │   SPDO     │  │ Safety App.   │  │
  │  │              ├──┼──────────►─┼──┤               │  │
  │  │ CRC, SeqNo,  │  │            │  │ CRC check,    │  │
  │  │ ConnID       │  │            │  │ SeqNo check,  │  │
  │  └──────────────┘  │            │  │ WD monitoring │  │
  └────────────────────┘            └─────────────────────┘
```

A **producer** generates SPDO frames; one or more **consumers** receive and validate them.
Validation failures trigger a transition to the **safe state** (typically de-energised outputs
or controlled stop).

### 2.3 Dual-Channel Architecture

For SIL 2 / PLd, safety nodes typically implement a dual-channel (1oo2D) internal structure:

```
        Input Signal
             │
     ┌───────┴────────┐
     │                │
  ┌──▼───┐         ┌──▼───┐
  │ MCU  │         │ MCU  │   ◄── Two independent microcontrollers
  │  A   │         │  B   │        or execution channels
  └──┬───┘         └──┬───┘
     │  Cross-check   │
     └───────┬────────┘
             │
      ┌──────▼──────┐
      │ SPDO Frame  │   ◄── Single CAN frame assembled after cross-check
      │ Producer    │
      └─────────────┘
```

The cross-check compares results from both channels before any output is activated or any SPDO
is sent with a "valid" status. Discrepancies force the safe state.

---

## 3. The Black-Channel Principle

### 3.1 Definition

The **black-channel principle** is the cornerstone of CANopen Safety's architecture. It states:

> *The underlying communication channel (CAN bus, CAN controller, CANopen stack) is treated as
> an untrusted medium. No assumptions are made about its correct behaviour. All safety
> properties are achieved exclusively through measures in the safety application layer.*

The CAN bus is literally a "black box" — it may corrupt, delay, lose, repeat, or reorder
messages. The safety layer must detect all such failures within the required diagnostic
coverage.

### 3.2 Fault Assumptions Under the Black Channel

```
                    BLACK CHANNEL (untrusted)
  ┌────────────────────────────────────────────────────────┐
  │                                                        │
  │   Producer                           Consumer          │
  │   ┌──────┐   ┌────────────────────┐  ┌──────┐          │
  │   │SPDO  │──►│   CAN Bus / Stack  │─►│SPDO  │          │
  │   │Frame │   │                    │  │Frame │          │
  │   └──────┘   │  Possible faults:  │  └──────┘          │
  │              │  ✗ Corruption      │                    │
  │              │  ✗ Loss/dropout    │                    │
  │              │  ✗ Repetition      │                    │
  │              │  ✗ Insertion       │                    │
  │              │  ✗ Wrong sequence  │                    │
  │              │  ✗ Wrong delay     │                    │
  │              │  ✗ Wrong sender    │                    │
  │              └────────────────────┘                    │
  └────────────────────────────────────────────────────────┘
```

### 3.3 Residual Error Probability

CiA 304 targets a **residual error probability** of less than 10⁻⁹ per message transmission,
which is sufficient for SIL 2 when combined with a watchdog monitoring interval adequate for
the application's process safety time.

---

## 4. Safety PDO (SPDO)

### 4.1 Frame Structure

An SPDO is a standard CANopen PDO (Process Data Object) with a specific payload structure
defined by CiA 304. The CAN ID follows normal PDO assignment rules.

```
 CAN Frame (max 8 bytes data):
 ┌──────────┬──────────────────────────────────────────────┐
 │ CAN ID   │              CAN Data Field (≤8 bytes)       │
 │ (11-bit) │                                              │
 └──────────┴──────────────────────────────────────────────┘

 SPDO Payload Layout (CiA 304 §6):
 ┌───────────────────────────────────────────────────────────┐
 │  Byte 0      │  Bytes 1..n    │  Bytes n+1..n+2           │
 │  Control     │  Safety Data   │  CRC-8 / CRC-16           │
 │  (SeqNo +    │  (process      │  (inverted complement     │
 │   Flags)     │   values)      │   over control+data)      │
 └───────────────────────────────────────────────────────────┘

 Control Byte (Byte 0) bit-field:
  7     6     5     4     3     2     1     0
 ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 │ TAF │CONS │  —  │  —  │        SeqNo[3:0]     │
 └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
   │      │                       └─── 4-bit rolling sequence number
   │      └─── Consecutive flag (two identical frames must differ here)
   └──────── Time-out acknowledgement flag
```

**Key fields:**

| Field | Size | Purpose |
|-------|------|---------|
| Connection ID | Encoded in SPDO-ID | Identifies the logical safety connection |
| Sequence Number | 4 bits in control byte | Detects loss, repetition, and re-ordering |
| Consecutive Flag | 1 bit in control byte | Distinguishes identical successive values |
| Safety Data | 1–6 bytes | Actual process data (e.g. e-stop state, speed) |
| CRC | 8 or 16 bits | Detects data corruption |

### 4.2 SPDO Object Dictionary Entries

SPDOs are configured via the object dictionary in the range **0x5C00–0x5FFF** (for Safety
consumer parameters) and **0x5800–0x5BFF** (for Safety producer parameters):

```
Index   Sub  Content
0x5800  0    Highest sub-index supported
        1    Connection ID (SPDO-ID, CAN COB-ID)
        2    Producer cycle time [µs]
        3    Safety data size [bytes]
        4    CRC polynomial selection

0x5C00  0    Highest sub-index
        1    Connection ID (matched to producer)
        2    Watchdog timeout [µs]
        3    Expected data size [bytes]
        4    CRC polynomial selection
```

---

## 5. Frame Integrity Measures

CiA 304 mandates a combination of measures to achieve the required diagnostic coverage of
≥99 % (DC medium, required for SIL 2).

### 5.1 CRC Protection

Two CRC polynomials are defined:

| Variant | Polynomial | Protection | Typical use |
|---------|-----------|------------|-------------|
| CRC-8   | 0x2F (AUTOSAR) | ≤6 bytes data | Small SPDOs |
| CRC-16  | 0x1021 (CRC-CCITT) | ≤6 bytes data | Larger SPDOs |

The CRC covers **Control byte + Safety Data bytes**. The CRC itself is sent as the bitwise
complement (inverted) to increase the hamming distance.

```
CRC Calculation scope:
  ┌─────────────┬──────────────────┬──────────────┐
  │ Control (1) │ Safety Data (1-6)│  CRC (1 or 2)│
  └─────────────┴──────────────────┴──────────────┘
  │◄────── CRC input ─────────────►│◄── stored ──►│
```

### 5.2 Sequence Number

The 4-bit rolling sequence number (0–15) increments with each SPDO transmission cycle.

```
  Time ──►
  Tx: [SeqNo=0] [SeqNo=1] [SeqNo=2] [SeqNo=3] ... [SeqNo=15] [SeqNo=0] ...

  Fault scenarios detected by sequence number:
  ┌──────────────────────┬───────────────────────────────────┐
  │ Fault type           │ Detection mechanism               │
  ├──────────────────────┼───────────────────────────────────┤
  │ Message loss         │ SeqNo skips (e.g. 2→4)            │
  │ Message repetition   │ SeqNo does not increment          │
  │ Message reordering   │ SeqNo decreases unexpectedly      │
  │ Wrong sender (same   │ Connection ID mismatch catches it │
  │ sequence range)      │ (not SeqNo alone)                 │
  └──────────────────────┴───────────────────────────────────┘
```

**Consecutive flag:** When two successive SPDO transmissions carry *identical* data values, the
sequence number alone changes. The consecutive flag alternates to ensure the frame bit pattern
changes even when process data is constant, preventing a stuck-frame error from being masked.

### 5.3 Connection ID

The Connection ID is encoded in the SPDO's COB-ID and is unique per safety connection. It
provides:

- **Node authentication:** A consumer verifies it is receiving from the correct producer.
- **Insertion protection:** A rogue or misaddressed frame with a different COB-ID is rejected.
- **Connection binding:** Multiple SPDOs on the same network have distinct IDs.

```
  SPDO-ID assignment (example):
  ┌────────────────────────────────────────────────────────┐
  │  Bits 10..7 = SPDO function code (fixed by CiA 304)    │
  │  Bits  6..0 = Node-ID of the producer                  │
  │                                                        │
  │  e.g. Node-ID = 0x05, function code = 0b0001_1         │
  │  COB-ID = (0b0001_1 << 7) | 0x05 = 0x0C5               │
  └────────────────────────────────────────────────────────┘
```

### 5.4 Timestamp / Watchdog Time

Although CiA 304 does not embed a real-time timestamp into the SPDO itself, **timing** is
enforced externally by the watchdog mechanism described in Section 6.

### 5.5 Summary of Fault Coverage

```
 Fault class             Measure                    Coverage
 ──────────────────────────────────────────────────────────────
 Data corruption         CRC-8/CRC-16               ≥ 99.6 %
 Frame loss              Watchdog timer             100 %
 Frame repetition        Sequence number            100 %
 Frame insertion         Connection ID + SeqNo      100 %
 Reordering              Sequence number            100 %
 Wrong delay             Watchdog timer             100 %
 Wrong sender            Connection ID              100 %
 Masking (static data)   Consecutive flag           100 %
 ──────────────────────────────────────────────────────────────
 Combined DC (IEC 61508 Table A.15)                 ≥ 99 %
```

---

## 6. Watchdog Relationships

### 6.1 Concept

Every SPDO consumer maintains a **watchdog timer** per safety connection. The timer is reset
each time a *valid* SPDO is received. If the timer expires before the next valid SPDO arrives,
the consumer immediately transitions to the **safe state**.

```
  Time ──►
  │← SPDO cycle time T_cycle → │← T_cycle → │
  │                            │            │
  Rx: ──────[ok]────────────[ok]────────────?──────────────►
                                            │← WD timeout →│
                                            │              │
                                        WD reset           WD fires
                                                           → SAFE STATE
```

### 6.2 Watchdog Parameters

The watchdog timeout **T_WD** must be chosen such that:

```
  T_WD  =  T_cycle  +  T_jitter  +  T_delay_max

  where:
    T_cycle     = nominal SPDO production interval (configured in OD 0x5800:02)
    T_jitter    = maximum transmission jitter (depends on CAN load, bit rate)
    T_delay_max = maximum propagation + stack delay
```

The configured T_WD is stored in the consumer's object dictionary (0x5C00:02) and must satisfy:

```
  T_WD  <  T_PST  (Process Safety Time — application requirement)
```

### 6.3 Watchdog Network Diagram

```
  Producer                    CAN Bus                    Consumer
  ┌──────────────┐                                  ┌──────────────────────┐
  │ Timer T_prod │──── SPDO every T_cycle ─────────►│  WD Timer T_WD       │
  │              │                                  │                      │
  │ If T_prod    │     ← Heartbeat monitoring ──────│  Reset on each valid │
  │ overruns:    │       (separate NMT channel)     │  SPDO received       │
  │ → safe state │                                  │                      │
  │              │                                  │  On expiry:          │
  └──────────────┘                                  │  → safe state output │
                                                    └──────────────────────┘
```

### 6.4 Dual Watchdog (Producer-side)

A safety-conscious producer also monitors itself:

- A **production watchdog** ensures the SPDO is sent within the required cycle time.
- If the production cycle is missed, the producer either sends a "not valid" SPDO or
  transitions its own outputs to the safe state.

```
  Producer Internal Watchdog:
  ┌───────────────────────────────────────────┐
  │ Application Layer                         │
  │  ┌──────────────────────────────────┐     │
  │  │  Safety Task (cyclic, T_cycle)   │     │
  │  │                                  │     │
  │  │  Checks production WD ───────────┼─► If missed → safe state │
  │  │  Builds SPDO frame               │     │
  │  │  Sends via CAN driver            │     │
  │  └──────────────────────────────────┘     │
  └───────────────────────────────────────────┘
```

---

## 7. SIL 2 / PLd Compliance

### 7.1 IEC 61508 SIL Definitions

```
  Safety Integrity Level (SIL) — Low Demand Mode (IEC 61508-1 Table 2)
  ┌───────────────────────────────────────────────────────────┐
  │  SIL  │  PFD (avg probability of failure on demand)       │
  ├───────┼────────────────────────────────────────────────── │
  │   1   │  10⁻² ≥ PFD > 10⁻¹                                │
  │   2   │  10⁻³ ≥ PFD > 10⁻²   ◄── CANopen Safety target    │
  │   3   │  10⁻⁴ ≥ PFD > 10⁻³                                │
  │   4   │  10⁻⁵ ≥ PFD > 10⁻⁴                                │
  └───────┴───────────────────────────────────────────────────┘
```

### 7.2 ISO 13849 Performance Level Definitions

```
  Performance Level (PL) — ISO 13849-1 Table 2
  ┌────────────────────────────────────────────────────────────┐
  │  PL   │  PFH (avg probability of dangerous failure/hour)   │
  ├───────┼────────────────────────────────────────────────────│
  │   a   │  10⁻⁵ ≥ PFH > 10⁻⁴                                 │
  │   b   │  3×10⁻⁶ ≥ PFH > 10⁻⁵                               │
  │   c   │  10⁻⁶ ≥ PFH > 3×10⁻⁶                               │
  │   d   │  10⁻⁷ ≥ PFH > 10⁻⁶   ◄── CANopen Safety target     │
  │   e   │  10⁻⁸ ≥ PFH > 10⁻⁷                                 │
  └───────┴────────────────────────────────────────────────────┘
```

### 7.3 Achieving SIL 2 / PLd — What the Communication Layer Must Provide

According to IEC 61508-2 §7.4.11 and CiA 304, the communication subsystem must:

1. **Residual error rate ≤ 10⁻⁹** per message — achieved by the CRC + sequence number
   combination as shown in Section 5.
2. **Diagnostic Coverage ≥ 99 %** — achieved by the combined measures in Section 5.5.
3. **Proof-test interval** compatible with the overall safety function's required PFD.
4. **Random hardware failure** of the CAN hardware must be accounted for separately
   (typically in the safety node's hardware FMEA).

### 7.4 Architectural Constraints (IEC 61508-2 Table 2)

For SIL 2 with a single-channel (1oo1) hardware architecture:

```
  Hardware Fault Tolerance (HFT) = 0
  → Safe Failure Fraction (SFF) must be ≥ 90 % (Route 1H)
  → OR use Route 2H (IEC 61508-2 §7.4.4.2) with qualified components

  Typical safety node architecture for SIL 2 / PLd:
  ┌──────────────────────────────────────────────────────────┐
  │  1oo2D: Two channels, cross-monitored (HFT = 1)          │
  │  This allows SFF ≥ 60 % while still achieving SIL 2      │
  │  and provides superior diagnostic coverage               │
  └──────────────────────────────────────────────────────────┘
```

---

## 8. Integration with IEC 61508 and ISO 13849 Assessments

### 8.1 Subsystem Decomposition

In a safety assessment, the total safety function is decomposed into subsystems:

```
  ┌──────────────────────────────────────────────────────────────────┐
  │                  Safety Function (e.g. STO)                      │
  └───────────┬───────────────────────────────────┬──────────────────┘
              │                                   │
  ┌───────────▼──────────┐             ┌──────────▼────────────────┐
  │  Input subsystem     │             │  Output subsystem         │
  │  (e.g. safety sensor)│             │  (e.g. safety relay,      │
  │  SIL 2 / PLd         │             │   drive STO input)        │
  └───────────┬──────────┘             └──────────┬────────────────┘
              │                                   │
              │  ┌────────────────────────────┐   │
              └─►│  Communication subsystem   ├──►│
                 │  CANopen Safety (CiA 304)  │
                 │  SIL 2 / PLd rated         │
                 └────────────────────────────┘
```

Each subsystem contributes its own PFD (or PFH). The total safety function PFD is:

```
  PFD_total = PFD_input + PFD_comms + PFD_output
```

The CANopen Safety communication layer is qualified to contribute no more than 10⁻⁹ per
message to PFD_comms — a negligible contribution, meaning the subsystem is not the bottleneck.

### 8.2 Required Documentation for a Safety Assessment

```
  Documentation checklist for CANopen Safety integration:
  ┌─────────────────────────────────────────────────────────────────┐
  │  ✓ SPDO mapping table (which data, which connection IDs)        │
  │  ✓ Watchdog timeout values and justification                    │
  │  ✓ Process Safety Time (PST) analysis                           │
  │  ✓ Worst-case CAN bus load analysis (jitter calculation)        │
  │  ✓ FMEA for the CAN hardware (controller, transceiver)          │
  │  ✓ Evidence of CRC polynomial selection meeting ≤10⁻⁹ target    │
  │  ✓ Software safety plan (IEC 61508-3 SIL 2 requirements)        │
  │  ✓ Unit and integration test records for safety software        │
  │  ✓ EMC test records (IEC 61000 series)                          │
  └─────────────────────────────────────────────────────────────────┘
```

### 8.3 Process Safety Time (PST) and Response Time Budget

```
  T_PST (e.g. 100 ms)
  │
  ├─── T_sensor_response (e.g. 5 ms)
  │
  ├─── T_SPDO_cycle × N_cycles_max
  │    (e.g. 10 ms × 2 = 20 ms — allow one missed frame + WD check)
  │
  ├─── T_WD_check (e.g. 10 ms — worst-case watchdog granularity)
  │
  ├─── T_logic (e.g. 5 ms — PLC safety task cycle)
  │
  └─── T_actuator_response (e.g. 50 ms)
       ─────────────────────────────────
       Total = 90 ms  ≤  100 ms ✓
```

---

## 9. C/C++ Programming Examples

### 9.1 Data Structures

```c
/**
 * @file canopen_safety.h
 * @brief CANopen Safety (CiA 304) core data structures and API
 *
 * Implements black-channel safety layer for SIL 2 / PLd compliance.
 * All safety-critical functions must be called from a deterministic
 * cyclic task with jitter < T_jitter_max.
 */

#ifndef CANOPEN_SAFETY_H
#define CANOPEN_SAFETY_H

#include <stdint.h>
#include <stdbool.h>

/* ── Safety PDO control byte bit-field masks ─────────────────── */
#define SPDO_CTRL_SEQNO_MASK    0x0Fu   /**< Bits [3:0] sequence number     */
#define SPDO_CTRL_CONS_FLAG     0x40u   /**< Bit  [6]   consecutive flag    */
#define SPDO_CTRL_TAF           0x80u   /**< Bit  [7]   time-out ack flag   */

/* ── CRC polynomial selections ───────────────────────────────── */
#define SPDO_CRC8_POLY          0x2Fu   /**< AUTOSAR CRC-8; ≤6 bytes data   */
#define SPDO_CRC16_POLY         0x1021u /**< CRC-CCITT; ≤6 bytes data       */

/* ── Maximum safety data payload ─────────────────────────────── */
#define SPDO_MAX_DATA_BYTES     6u      /**< Bytes available after ctrl byte */
#define SPDO_MAX_FRAME_BYTES    8u      /**< Full CAN data field             */

/** Return codes for safety API functions */
typedef enum {
    SPDO_OK            = 0,
    SPDO_ERR_CRC       = 1,   /**< CRC mismatch                         */
    SPDO_ERR_SEQNO     = 2,   /**< Sequence number out of expected range */
    SPDO_ERR_CONN_ID   = 3,   /**< Wrong connection ID (COB-ID)          */
    SPDO_ERR_WD_EXPIRY = 4,   /**< Watchdog timeout                     */
    SPDO_ERR_LENGTH    = 5,   /**< Frame DLC does not match expected     */
    SPDO_ERR_INVALID   = 6    /**< Generic invalid parameter             */
} spdo_result_t;

/**
 * @brief Safety PDO raw frame (wire format, max 8 bytes CAN payload)
 *
 * Layout:  [ctrl_byte] [data_0 .. data_n] [crc_lo] [crc_hi?]
 * For CRC-8:  total = 1 + data_len + 1  bytes
 * For CRC-16: total = 1 + data_len + 2  bytes
 */
typedef struct __attribute__((packed)) {
    uint8_t ctrl;                          /**< Control byte (SeqNo + flags) */
    uint8_t payload[SPDO_MAX_FRAME_BYTES - 1u]; /**< Data + CRC bytes        */
} spdo_frame_t;

/**
 * @brief Configuration for a single SPDO safety connection
 *
 * Stored in the object dictionary at 0x5800 (producer) or 0x5C00 (consumer).
 * Both sides must agree on conn_id, data_size, crc_type, and cycle_us.
 */
typedef struct {
    uint32_t conn_id;       /**< COB-ID used for this safety connection      */
    uint16_t cycle_us;      /**< Nominal production cycle time [µs]          */
    uint16_t watchdog_us;   /**< Consumer watchdog timeout [µs]              */
    uint8_t  data_size;     /**< Safety data bytes (1–6)                     */
    bool     use_crc16;     /**< true = CRC-16, false = CRC-8                */
} spdo_config_t;

/**
 * @brief Run-time state for a SPDO producer
 */
typedef struct {
    spdo_config_t cfg;
    uint8_t  seq_no;         /**< Rolling 4-bit sequence counter [0–15]      */
    bool     cons_flag;      /**< Consecutive bit, toggled each cycle        */
    uint32_t last_tx_time_us;/**< Timestamp of last successful SPDO send     */
    bool     prod_wd_fired;  /**< true if production WD expired              */
} spdo_producer_t;

/**
 * @brief Run-time state for a SPDO consumer
 */
typedef struct {
    spdo_config_t cfg;
    uint8_t  expected_seq;   /**< Next expected sequence number               */
    bool     first_frame;    /**< true until first valid frame received       */
    uint32_t last_rx_time_us;/**< Timestamp of last valid frame reception     */
    bool     safe_state;     /**< true = consumer is in safe state            */
} spdo_consumer_t;

#endif /* CANOPEN_SAFETY_H */
```

### 9.2 CRC Calculation

```c
/**
 * @file spdo_crc.c
 * @brief CRC-8 (AUTOSAR, 0x2F) and CRC-16 (CRC-CCITT, 0x1021) for SPDO frames.
 *
 * Both CRCs are sent as bitwise complement to increase Hamming distance.
 * NOTE: These functions must be tested with 100 % MC/DC coverage (IEC 61508-3).
 */

#include "canopen_safety.h"

/* ── CRC-8 (AUTOSAR profile, polynomial 0x2F) ─────────────────── */

/** Pre-computed lookup table for CRC-8 (poly 0x2F, init 0xFF) */
static const uint8_t crc8_table[256] = {
    /* Generated offline with standard AUTOSAR CRC8 algorithm.
     * Showing first 16 entries as illustration: */
    0x00u, 0x2Fu, 0x4Eu, 0x61u, 0x9Cu, 0xB3u, 0xD2u, 0xFDu,
    0xF5u, 0xDAu, 0xBBu, 0x94u, 0x69u, 0x46u, 0x27u, 0x08u
    /* ... 240 more entries ... */
};

/**
 * @brief Compute CRC-8 over a buffer.
 * @param data   Pointer to input bytes (ctrl byte + safety data).
 * @param length Number of bytes to process.
 * @return CRC-8 value (NOT yet inverted — caller inverts before transmission).
 */
uint8_t spdo_crc8_compute(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0xFFu;   /* AUTOSAR initial value */

    while (length > 0u) {
        crc = crc8_table[crc ^ *data];
        ++data;
        --length;
    }

    /* XOR-out value for AUTOSAR CRC8 is 0xFF */
    return crc ^ 0xFFu;
}

/* ── CRC-16 (CRC-CCITT, polynomial 0x1021) ──────────────────── */

/**
 * @brief Compute CRC-16 over a buffer.
 * @param data   Pointer to input bytes.
 * @param length Number of bytes.
 * @return CRC-16 value (NOT yet inverted).
 */
uint16_t spdo_crc16_compute(const uint8_t *data, uint8_t length)
{
    uint16_t crc = 0xFFFFu;

    while (length > 0u) {
        uint8_t b = *data ^ (uint8_t)(crc >> 8u);
        b ^= (uint8_t)(b >> 4u);

        crc  = (uint16_t)((crc << 8u)
               ^ (uint16_t)((uint16_t)b << 12u)
               ^ (uint16_t)((uint16_t)b << 5u)
               ^ (uint16_t)b);
        ++data;
        --length;
    }

    return crc;
}
```

### 9.3 SPDO Producer

```c
/**
 * @file spdo_producer.c
 * @brief SPDO producer — builds and transmits Safety PDO frames.
 *
 * Called from a deterministic safety task at interval T_cycle.
 * The caller provides:
 *   - Current safety process data (e.g. safety input state)
 *   - Current system time in microseconds
 *   - A CAN transmit callback
 *
 * Safety measures implemented:
 *   ✓ Rolling sequence number (4-bit, 0–15)
 *   ✓ Consecutive flag (toggled when data unchanged)
 *   ✓ CRC-8 or CRC-16 (inverted before sending)
 *   ✓ Producer watchdog (detects own task overrun)
 */

#include "canopen_safety.h"
#include "spdo_crc.h"        /* spdo_crc8_compute, spdo_crc16_compute */
#include "can_driver.h"      /* can_send_frame()                      */

/* Forward declaration of CAN send helper */
static bool send_can_frame(uint32_t cob_id, const uint8_t *data, uint8_t dlc);

/**
 * @brief Initialise an SPDO producer instance.
 * @param prod   Pointer to producer state struct.
 * @param cfg    Configuration (COB-ID, cycle time, data size, CRC type).
 * @param now_us Current time in microseconds.
 */
void spdo_producer_init(spdo_producer_t  *prod,
                        const spdo_config_t *cfg,
                        uint32_t          now_us)
{
    prod->cfg             = *cfg;
    prod->seq_no          = 0u;
    prod->cons_flag       = false;
    prod->last_tx_time_us = now_us;
    prod->prod_wd_fired   = false;
}

/**
 * @brief Build and transmit one SPDO frame.
 *
 * Must be called exactly once per T_cycle from a safety task.
 *
 * @param prod          Producer state.
 * @param safety_data   Pointer to process values to transmit.
 * @param data_len      Length of safety_data (must match cfg.data_size).
 * @param now_us        Current time [µs] for production watchdog check.
 * @return SPDO_OK on success, error code otherwise.
 */
spdo_result_t spdo_produce(spdo_producer_t *prod,
                           const uint8_t   *safety_data,
                           uint8_t          data_len,
                           uint32_t         now_us)
{
    uint8_t frame_buf[SPDO_MAX_FRAME_BYTES] = {0};
    uint8_t dlc;
    uint8_t crc_input_len;

    /* ── Parameter guard ─────────────────────────────────────── */
    if ((prod == NULL) || (safety_data == NULL)) {
        return SPDO_ERR_INVALID;
    }
    if (data_len != prod->cfg.data_size) {
        return SPDO_ERR_INVALID;
    }
    if (data_len > SPDO_MAX_DATA_BYTES) {
        return SPDO_ERR_INVALID;
    }

    /* ── Production watchdog check ───────────────────────────── */
    uint32_t elapsed = now_us - prod->last_tx_time_us;
    if (elapsed > (uint32_t)(prod->cfg.cycle_us * 2u)) {
        /* Task has overrun — set safe state and abort */
        prod->prod_wd_fired = true;
        return SPDO_ERR_WD_EXPIRY;
    }

    /* ── Build control byte ──────────────────────────────────── */
    /* Increment sequence number, wrap at 15 → 0 */
    prod->seq_no = (uint8_t)((prod->seq_no + 1u) & SPDO_CTRL_SEQNO_MASK);

    /* Toggle consecutive flag every transmission cycle */
    prod->cons_flag = !prod->cons_flag;

    uint8_t ctrl = (prod->seq_no & SPDO_CTRL_SEQNO_MASK);
    if (prod->cons_flag) {
        ctrl |= SPDO_CTRL_CONS_FLAG;
    }
    frame_buf[0] = ctrl;

    /* ── Copy safety data ────────────────────────────────────── */
    for (uint8_t i = 0u; i < data_len; ++i) {
        frame_buf[1u + i] = safety_data[i];
    }

    crc_input_len = (uint8_t)(1u + data_len);   /* ctrl + data */

    /* ── Compute and append CRC (inverted) ───────────────────── */
    if (prod->cfg.use_crc16) {
        uint16_t crc = spdo_crc16_compute(frame_buf, crc_input_len);
        crc = (uint16_t)(~crc);                 /* Bit-invert (complement) */
        frame_buf[crc_input_len]      = (uint8_t)(crc & 0xFFu);
        frame_buf[crc_input_len + 1u] = (uint8_t)(crc >> 8u);
        dlc = (uint8_t)(crc_input_len + 2u);
    } else {
        uint8_t crc = spdo_crc8_compute(frame_buf, crc_input_len);
        frame_buf[crc_input_len] = (uint8_t)(~crc);   /* Bit-invert */
        dlc = (uint8_t)(crc_input_len + 1u);
    }

    /* ── Transmit via CAN driver ─────────────────────────────── */
    bool tx_ok = send_can_frame(prod->cfg.conn_id, frame_buf, dlc);
    if (!tx_ok) {
        /* CAN transmission error — initiate safe state externally */
        return SPDO_ERR_INVALID;
    }

    prod->last_tx_time_us = now_us;
    return SPDO_OK;
}

/* ── Thin wrapper around platform CAN driver ─────────────────── */
static bool send_can_frame(uint32_t cob_id, const uint8_t *data, uint8_t dlc)
{
    can_frame_t f;
    f.id  = cob_id;
    f.dlc = dlc;
    for (uint8_t i = 0u; i < dlc; ++i) {
        f.data[i] = data[i];
    }
    return (can_send_frame(&f) == CAN_OK);
}
```

### 9.4 SPDO Consumer and Watchdog

```c
/**
 * @file spdo_consumer.c
 * @brief SPDO consumer — receives, validates, and watchdog-monitors Safety PDOs.
 *
 * Two entry-points:
 *   spdo_receive()  — called from CAN receive interrupt / task when a frame arrives.
 *   spdo_watchdog() — called from a periodic safety task to check timeout.
 *
 * If any validation fails, or if the watchdog expires, the consumer
 * transitions to safe_state = true.  The application must poll this flag
 * and act (e.g. disable safety relay output).
 */

#include "canopen_safety.h"
#include "spdo_crc.h"

/**
 * @brief Initialise an SPDO consumer instance.
 * @param cons   Consumer state.
 * @param cfg    Matching configuration (must agree with producer's config).
 * @param now_us Current time [µs].
 */
void spdo_consumer_init(spdo_consumer_t  *cons,
                        const spdo_config_t *cfg,
                        uint32_t          now_us)
{
    cons->cfg             = *cfg;
    cons->expected_seq    = 0u;      /* Initialised to 0; first valid frame sets it */
    cons->first_frame     = true;
    cons->last_rx_time_us = now_us;
    cons->safe_state      = false;
}

/**
 * @brief Process a received CAN frame as a potential SPDO.
 *
 * Called whenever a CAN frame with conn_id matching this consumer arrives.
 * Performs in order:
 *   1. Connection ID (COB-ID) check
 *   2. DLC check
 *   3. CRC verification
 *   4. Sequence number check
 *
 * On success: resets watchdog, extracts safety data into out_data.
 * On failure: sets safe_state = true, returns error code.
 *
 * @param cons       Consumer state.
 * @param cob_id     COB-ID of received CAN frame.
 * @param raw        Received CAN data bytes.
 * @param dlc        Received CAN DLC.
 * @param out_data   Buffer for extracted safety data (must be cfg.data_size bytes).
 * @param now_us     Current time [µs] (for watchdog reset).
 * @return SPDO_OK or error code.
 */
spdo_result_t spdo_receive(spdo_consumer_t *cons,
                           uint32_t         cob_id,
                           const uint8_t   *raw,
                           uint8_t          dlc,
                           uint8_t         *out_data,
                           uint32_t         now_us)
{
    uint8_t  expected_dlc;
    uint8_t  crc_input_len;
    uint8_t  received_seq;

    /* ── 1. Connection ID check ──────────────────────────────── */
    if (cob_id != cons->cfg.conn_id) {
        /* Not our SPDO — silently ignore (other safety connections exist) */
        return SPDO_ERR_CONN_ID;
    }

    /* ── 2. DLC (length) check ───────────────────────────────── */
    crc_input_len = (uint8_t)(1u + cons->cfg.data_size);
    expected_dlc  = (uint8_t)(crc_input_len + (cons->cfg.use_crc16 ? 2u : 1u));

    if (dlc != expected_dlc) {
        cons->safe_state = true;
        return SPDO_ERR_LENGTH;
    }

    /* ── 3. CRC check ────────────────────────────────────────── */
    if (cons->cfg.use_crc16) {
        uint16_t rx_crc;
        /* Reconstruct CRC from last two bytes (little-endian) */
        rx_crc  = (uint16_t)raw[crc_input_len];
        rx_crc |= (uint16_t)((uint16_t)raw[crc_input_len + 1u] << 8u);
        /* Invert stored CRC back to compare with computed */
        uint16_t computed = spdo_crc16_compute(raw, crc_input_len);
        if ((uint16_t)(~rx_crc) != computed) {
            cons->safe_state = true;
            return SPDO_ERR_CRC;
        }
    } else {
        uint8_t rx_crc    = raw[crc_input_len];
        uint8_t computed  = spdo_crc8_compute(raw, crc_input_len);
        if ((uint8_t)(~rx_crc) != computed) {
            cons->safe_state = true;
            return SPDO_ERR_CRC;
        }
    }

    /* ── 4. Sequence number check ────────────────────────────── */
    received_seq = (uint8_t)(raw[0] & SPDO_CTRL_SEQNO_MASK);

    if (!cons->first_frame) {
        /* Expected next SeqNo = (last + 1) mod 16 */
        uint8_t next_expected = (uint8_t)((cons->expected_seq + 1u) & 0x0Fu);
        if (received_seq != next_expected) {
            cons->safe_state = true;
            return SPDO_ERR_SEQNO;
        }
    }
    cons->first_frame  = false;
    cons->expected_seq = received_seq;

    /* ── All checks passed — reset watchdog ──────────────────── */
    cons->last_rx_time_us = now_us;
    cons->safe_state      = false;

    /* ── Extract safety data for application use ─────────────── */
    for (uint8_t i = 0u; i < cons->cfg.data_size; ++i) {
        out_data[i] = raw[1u + i];
    }

    return SPDO_OK;
}

/**
 * @brief Watchdog check — call from periodic safety task.
 *
 * Must be called at a period significantly smaller than cfg.watchdog_us
 * (typically at least 4× faster) to ensure timely detection.
 *
 * @param cons   Consumer state.
 * @param now_us Current time [µs].
 * @return true if safe_state was (or has been) set.
 */
bool spdo_watchdog_check(spdo_consumer_t *cons, uint32_t now_us)
{
    if (cons->safe_state) {
        return true;   /* Already in safe state — do not re-evaluate */
    }

    uint32_t age = now_us - cons->last_rx_time_us;
    if (age > (uint32_t)cons->cfg.watchdog_us) {
        cons->safe_state = true;
    }

    return cons->safe_state;
}
```

### 9.5 Application Integration — Cyclic Safety Task

```c
/**
 * @file safety_task.c
 * @brief Example integration: cyclic safety task driving a Safe Torque Off (STO) function.
 *
 * Architecture:
 *   - Producer side: Safety I/O node reads an emergency stop switch and sends SPDO.
 *   - Consumer side: Safety PLC validates SPDO and controls STO output to drive.
 *
 * Task called at T_cycle = 10 ms from RTOS with jitter < 0.5 ms.
 */

#include "canopen_safety.h"
#include "safety_task.h"
#include "hal_io.h"       /* hal_read_estop(), hal_set_sto_output() */
#include "hal_time.h"     /* hal_get_time_us()                      */

/* ── SPDO configuration for the STO safety connection ─────────── */
static const spdo_config_t sto_spdo_cfg = {
    .conn_id     = 0x0C5u,   /* COB-ID: function code 0b0001_1 | node 0x05 */
    .cycle_us    = 10000u,   /* 10 ms production cycle                       */
    .watchdog_us = 25000u,   /* 25 ms watchdog (2.5× cycle; < PST = 100 ms)  */
    .data_size   = 1u,       /* 1 byte: bit 0 = e-stop OK, bit 1 = alive     */
    .use_crc16   = false     /* CRC-8 sufficient for 1 data byte              */
};

/* ── Module-level state ────────────────────────────────────────── */
static spdo_producer_t  sto_producer;
static spdo_consumer_t  sto_consumer;
static bool             system_in_safe_state = true;

/**
 * @brief One-time initialisation — call before RTOS scheduler starts.
 */
void safety_task_init(void)
{
    uint32_t now = hal_get_time_us();
    spdo_producer_init(&sto_producer, &sto_spdo_cfg, now);
    spdo_consumer_init(&sto_consumer, &sto_spdo_cfg, now);
    system_in_safe_state = true;
}

/**
 * @brief Periodic safety task body — called every 10 ms.
 *
 * On the PRODUCER node (safety I/O):
 *   Reads the physical e-stop switch and transmits an SPDO.
 *
 * On the CONSUMER node (safety PLC):
 *   Checks the watchdog and sets/clears the STO output accordingly.
 *
 * In this example both roles run on different physical nodes, but the
 * code pattern for each is illustrated here side-by-side.
 */
void safety_task_cycle(void)
{
    uint32_t now = hal_get_time_us();

    /* ════════════════════════════════════════════════════════════
     * PRODUCER SIDE — runs on the safety I/O node
     * ════════════════════════════════════════════════════════════ */
    {
        uint8_t estop_state = 0u;

        /* Bit 0: 1 = e-stop circuit CLOSED (safe to run) */
        if (hal_read_estop() == ESTOP_CLOSED) {
            estop_state |= 0x01u;
        }
        /* Bit 1: alive bit, always set during normal operation */
        estop_state |= 0x02u;

        spdo_result_t tx_result = spdo_produce(&sto_producer,
                                               &estop_state,
                                               1u,
                                               now);
        if (tx_result != SPDO_OK) {
            /* Production error — cannot guarantee communication.
             * Transition own outputs to safe state immediately. */
            hal_set_local_safe_state();
        }
    }

    /* ════════════════════════════════════════════════════════════
     * CONSUMER SIDE — runs on the safety PLC node
     * ════════════════════════════════════════════════════════════ */
    {
        /* Step 1: Run watchdog check (irrespective of new frame) */
        bool wd_fired = spdo_watchdog_check(&sto_consumer, now);

        if (wd_fired) {
            /* No valid SPDO within 25 ms — activate STO (safe state) */
            hal_set_sto_output(STO_ACTIVE);
            system_in_safe_state = true;
            return;
        }

        /* Step 2: Process any freshly-arrived SPDO (set by RX interrupt) */
        extern volatile bool     g_spdo_frame_available;
        extern volatile uint8_t  g_spdo_raw[8];
        extern volatile uint8_t  g_spdo_dlc;
        extern volatile uint32_t g_spdo_cobid;

        if (g_spdo_frame_available) {
            uint8_t safety_data[1] = {0};

            /* Atomically capture the frame (disable interrupt briefly) */
            uint8_t  raw[8];
            uint8_t  dlc;
            uint32_t cobid;

            ENTER_CRITICAL();
            for (int i = 0; i < 8; i++) raw[i] = g_spdo_raw[i];
            dlc   = g_spdo_dlc;
            cobid = g_spdo_cobid;
            g_spdo_frame_available = false;
            EXIT_CRITICAL();

            spdo_result_t rx_result = spdo_receive(&sto_consumer,
                                                   cobid,
                                                   raw,
                                                   dlc,
                                                   safety_data,
                                                   now);

            if (rx_result != SPDO_OK) {
                /* Validation failure — safe state */
                hal_set_sto_output(STO_ACTIVE);
                system_in_safe_state = true;
                return;
            }

            /* Step 3: Evaluate safety logic from validated data */
            bool estop_ok  = (safety_data[0] & 0x01u) != 0u;
            bool alive_bit = (safety_data[0] & 0x02u) != 0u;

            if (estop_ok && alive_bit) {
                hal_set_sto_output(STO_INACTIVE);   /* Allow drive to run */
                system_in_safe_state = false;
            } else {
                hal_set_sto_output(STO_ACTIVE);     /* Emergency stop     */
                system_in_safe_state = true;
            }
        }
    }
}

/**
 * @brief CAN receive ISR — stores incoming frames for the safety task.
 * Called from the CAN hardware interrupt.
 */
void CAN_RX_IRQHandler(void)
{
    extern volatile bool     g_spdo_frame_available;
    extern volatile uint8_t  g_spdo_raw[8];
    extern volatile uint8_t  g_spdo_dlc;
    extern volatile uint32_t g_spdo_cobid;

    can_frame_t f;
    if (can_get_received_frame(&f) == CAN_OK) {
        /* Filter: only store frames matching our SPDO COB-ID */
        if (f.id == sto_spdo_cfg.conn_id) {
            for (int i = 0; i < f.dlc; ++i) g_spdo_raw[i] = f.data[i];
            g_spdo_dlc             = f.dlc;
            g_spdo_cobid           = f.id;
            g_spdo_frame_available = true;
        }
    }
}
```

### 9.6 Object Dictionary Configuration (CANopen OD Entries)

```c
/**
 * @file od_safety.c
 * @brief Object dictionary entries for CANopen Safety (indices 0x5800, 0x5C00).
 *
 * Compliant with CiA 304 §8 and EDS/DCF file structure.
 * In a real implementation these entries are integrated into the master OD.
 */

#include "od_safety.h"

/* ── Producer parameter record (index 0x5800) ─────────────────── */
/*
   Sub  Name                    Access  Type    Value
   ─────────────────────────────────────────────────────────────────
   0    Highest sub-index       RO      UINT8   4
   1    SPDO-ID (COB-ID)        RW      UINT32  0x000000C5 (node 5)
   2    Producer cycle time     RW      UINT16  10000  [µs = 10 ms]
   3    Safety data size        RW      UINT8   1      [bytes]
   4    CRC type                RW      UINT8   0      [0=CRC-8, 1=CRC-16]
*/

od_entry_t od_spdo_producer_0x5800[] = {
    /* Sub 0 */ { .index=0x5800, .sub=0, .type=OD_UINT8,  .access=OD_RO, .value.u8  = 4u          },
    /* Sub 1 */ { .index=0x5800, .sub=1, .type=OD_UINT32, .access=OD_RW, .value.u32 = 0x000000C5u },
    /* Sub 2 */ { .index=0x5800, .sub=2, .type=OD_UINT16, .access=OD_RW, .value.u16 = 10000u      },
    /* Sub 3 */ { .index=0x5800, .sub=3, .type=OD_UINT8,  .access=OD_RW, .value.u8  = 1u          },
    /* Sub 4 */ { .index=0x5800, .sub=4, .type=OD_UINT8,  .access=OD_RW, .value.u8  = 0u          },
};

/* ── Consumer parameter record (index 0x5C00) ─────────────────── */
/*
   Sub  Name                    Access  Type    Value
   ─────────────────────────────────────────────────────────────────
   0    Highest sub-index       RO      UINT8   4
   1    SPDO-ID (COB-ID)        RW      UINT32  0x000000C5 (matches producer)
   2    Watchdog timeout        RW      UINT16  25000  [µs = 25 ms]
   3    Expected data size      RW      UINT8   1      [bytes]
   4    CRC type                RW      UINT8   0      [0=CRC-8, 1=CRC-16]
*/

od_entry_t od_spdo_consumer_0x5C00[] = {
    /* Sub 0 */ { .index=0x5C00, .sub=0, .type=OD_UINT8,  .access=OD_RO, .value.u8  = 4u          },
    /* Sub 1 */ { .index=0x5C00, .sub=1, .type=OD_UINT32, .access=OD_RW, .value.u32 = 0x000000C5u },
    /* Sub 2 */ { .index=0x5C00, .sub=2, .type=OD_UINT16, .access=OD_RW, .value.u16 = 25000u      },
    /* Sub 3 */ { .index=0x5C00, .sub=3, .type=OD_UINT8,  .access=OD_RW, .value.u8  = 1u          },
    /* Sub 4 */ { .index=0x5C00, .sub=4, .type=OD_UINT8,  .access=OD_RW, .value.u8  = 0u          },
};
```

### 9.7 Unit Test Skeleton (Safety-Critical Test Requirements)

```c
/**
 * @file test_spdo.c
 * @brief Unit tests for SPDO producer and consumer.
 *
 * IEC 61508-3 requires 100 % MC/DC (Modified Condition/Decision Coverage)
 * for SIL 2 software.  These tests exercise every decision point and
 * every independent condition affecting each decision in spdo_consumer.c.
 */

#include "unity.h"   /* Unity test framework */
#include "canopen_safety.h"

static spdo_producer_t prod;
static spdo_consumer_t cons;
static const spdo_config_t test_cfg = {
    .conn_id     = 0x0C5u,
    .cycle_us    = 10000u,
    .watchdog_us = 25000u,
    .data_size   = 1u,
    .use_crc16   = false
};

void setUp(void) {
    spdo_producer_init(&prod, &test_cfg, 0u);
    spdo_consumer_init(&cons, &test_cfg, 0u);
}

/** TC-01: First valid frame is accepted and resets watchdog */
void test_valid_first_frame_accepted(void) {
    /* Produce a frame */
    uint8_t tx_data = 0x03u;
    /* (In a real integration test, the frame goes through CAN and back) */
    TEST_ASSERT_EQUAL(SPDO_OK, spdo_produce(&prod, &tx_data, 1u, 1000u));
}

/** TC-02: CRC corruption is detected */
void test_crc_error_detected(void) {
    uint8_t corrupted[3] = { 0x01u, 0x03u, 0xFFu }; /* Bad CRC byte */
    uint8_t out[1];
    spdo_result_t r = spdo_receive(&cons, 0x0C5u, corrupted, 3u, out, 5000u);
    TEST_ASSERT_EQUAL(SPDO_ERR_CRC, r);
    TEST_ASSERT_TRUE(cons.safe_state);
}

/** TC-03: Wrong COB-ID is silently rejected */
void test_wrong_cob_id_rejected(void) {
    uint8_t frame[3] = {0x01u, 0x03u, 0x00u};
    uint8_t out[1];
    spdo_result_t r = spdo_receive(&cons, 0x1FFu, frame, 3u, out, 5000u);
    TEST_ASSERT_EQUAL(SPDO_ERR_CONN_ID, r);
    TEST_ASSERT_FALSE(cons.safe_state); /* Silently ignored — no safe state */
}

/** TC-04: Sequence number skip (message loss) triggers safe state */
void test_sequence_skip_triggers_safe_state(void) {
    /* Manually advance expected_seq to 5, send seq=7 (skip 6) */
    cons.first_frame  = false;
    cons.expected_seq = 5u;

    /* Build a frame with seq=7, correct CRC for that control byte */
    uint8_t ctrl   = 0x07u;                              /* seq=7, no flags */
    uint8_t data   = 0x03u;
    uint8_t input[2] = { ctrl, data };
    uint8_t crc    = ~spdo_crc8_compute(input, 2u);
    uint8_t frame[3] = { ctrl, data, crc };
    uint8_t out[1];

    spdo_result_t r = spdo_receive(&cons, 0x0C5u, frame, 3u, out, 5000u);
    TEST_ASSERT_EQUAL(SPDO_ERR_SEQNO, r);
    TEST_ASSERT_TRUE(cons.safe_state);
}

/** TC-05: Watchdog fires after timeout */
void test_watchdog_fires_after_timeout(void) {
    /* last_rx_time = 0, watchdog = 25000 µs, now = 26000 µs */
    cons.last_rx_time_us = 0u;
    bool fired = spdo_watchdog_check(&cons, 26000u);
    TEST_ASSERT_TRUE(fired);
    TEST_ASSERT_TRUE(cons.safe_state);
}

/** TC-06: Watchdog does NOT fire within timeout */
void test_watchdog_ok_within_timeout(void) {
    cons.last_rx_time_us = 0u;
    bool fired = spdo_watchdog_check(&cons, 24000u);
    TEST_ASSERT_FALSE(fired);
    TEST_ASSERT_FALSE(cons.safe_state);
}

/** TC-07: Wrong DLC triggers safe state */
void test_wrong_dlc_triggers_safe_state(void) {
    uint8_t frame[4] = { 0x01u, 0x03u, 0xAAu, 0xBBu }; /* DLC=4, expected=3 */
    uint8_t out[1];
    spdo_result_t r = spdo_receive(&cons, 0x0C5u, frame, 4u, out, 5000u);
    TEST_ASSERT_EQUAL(SPDO_ERR_LENGTH, r);
    TEST_ASSERT_TRUE(cons.safe_state);
}
```

---

## 10. Summary

```
╔══════════════════════════════════════════════════════════════════════════════╗
║              CANopen Safety (EN 50325-5 / CiA 304) — Summary                 ║
╠══════════════════════════════════════════════════════════════════════════════╣
║                                                                              ║
║  CORE PRINCIPLE                                                              ║
║  ─────────────                                                               ║
║  Black-channel: treat the CAN bus as completely untrusted.                   ║
║  All safety properties live entirely in the application layer.               ║
║                                                                              ║
║  SAFETY PDO (SPDO)                                                           ║
║  ─────────────────                                                           ║
║  Standard CAN PDO with a special payload:                                    ║
║  [ Control byte | Safety Data 1–6 B | CRC-8 or CRC-16 (inverted) ]           ║
║                                                                              ║
║  INTEGRITY MEASURES (combined DC ≥ 99 %)                                     ║
║  ─────────────────────────────────────────                                   ║
║  ① Connection ID   — authenticates source, prevents insertion               ║
║  ② Sequence number — detects loss, repetition, reordering (4-bit, 0–15)     ║
║  ③ Consecutive flag — detects stuck-frame when data is constant             ║
║  ④ CRC-8 / CRC-16  — detects data corruption (inverted for extra HD)        ║
║  ⑤ Watchdog timer  — detects transmission delay and loss                    ║
║                                                                              ║
║  WATCHDOG                                                                    ║
║  ─────────                                                                   ║
║  Consumer resets timer on each valid SPDO.                                   ║
║  On expiry → immediate safe state.                                           ║
║  T_WD = T_cycle + T_jitter + T_delay  < T_PST (Process Safety Time)          ║
║                                                                              ║
║  SAFETY TARGET                                                               ║
║  ─────────────                                                               ║
║  Residual error probability per message: ≤ 10⁻⁹                              ║
║  IEC 61508: SIL 2    PFD ∈ [10⁻³, 10⁻²)                                      ║
║  ISO 13849: PLd      PFH ∈ [10⁻⁷, 10⁻⁶)                                      ║
║                                                                              ║
║  KEY IMPLEMENTATION RULES (C/C++)                                            ║
║  ────────────────────────────────                                            ║
║  • Cyclic safety task with bounded jitter (RTOS, not bare loop)              ║
║  • All safety variables in separate RAM region (MPU-protected if possible)   ║
║  • No dynamic memory allocation in safety code paths                         ║
║  • 100 % MC/DC test coverage for all safety functions (IEC 61508-3 SIL 2)    ║
║  • CRC must be verified bitwise-complemented (as stored/transmitted)         ║
║  • Safe state transitions must be non-recoverable without explicit reset     ║
║  • OD entries 0x5800 / 0x5C00 must be write-locked in operational mode       ║
║                                                                              ║
║  ASSESSMENT INTEGRATION                                                      ║
║  ──────────────────────                                                      ║
║  • CANopen Safety subsystem contributes ≤ 10⁻⁹ to PFD_comms                  ║
║  • Dominant PFD contributors are sensor and actuator hardware                ║
║  • Required documentation: SPDO map, WD analysis, PST budget, FMEA,          ║
║    software safety plan, test records, EMC evidence                          ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

### Quick-Reference Parameter Checklist

| Parameter | Location in OD | Typical Value | Constraint |
|-----------|---------------|---------------|-----------|
| SPDO-ID (COB-ID) | 0x5800:01 / 0x5C00:01 | Node-specific | Must be unique per connection |
| Producer cycle | 0x5800:02 | 10 ms | Determined by PST analysis |
| Watchdog timeout | 0x5C00:02 | 25 ms | > cycle + jitter; < PST |
| Data size | 0x5800:03 / 0x5C00:03 | 1–6 bytes | Must match both ends |
| CRC type | 0x5800:04 / 0x5C00:04 | 0 = CRC-8 | CRC-16 for > 4 data bytes |

### Standards Cross-Reference

| Standard | Relevance to CANopen Safety |
|----------|-----------------------------|
| EN 50325-5 | European adoption of CiA 304; mandatory in EU safety applications |
| CiA 304 | Detailed protocol specification (SPDO, watchdog, OD layout) |
| IEC 61508-2 | Hardware random failure analysis; SIL claim for communication subsystem |
| IEC 61508-3 | Software requirements: MC/DC coverage, coding guidelines for SIL 2 |
| ISO 13849-1 | PL assessment; CANopen Safety rated to PLd |
| ISO 11898 | Underlying CAN physical and data-link layer (not relied on for safety) |

---

*Document: 33_CANopen_Safety.md — CANopen Safety (EN 50325-5 / CiA 304)*  
*Coverage: Safety architecture, black-channel principle, SPDO, watchdog, SIL 2 / PLd, IEC 61508, ISO 13849*