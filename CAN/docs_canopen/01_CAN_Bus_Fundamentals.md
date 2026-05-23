# 01 — CAN Bus Fundamentals

**Physical Layer & Signalling**
- CAN-H / CAN-L differential wiring with ASCII topology diagram
- Dominant/recessive voltage levels table, wired-AND explanation

**All Four Frame Types** — each with ASCII bit-field diagrams
- Data Frame (SFF 11-bit and EFF 29-bit layouts, field-by-field descriptions)
- Remote Frame (RTR)
- Error Frame (superposition example, all 5 error types)
- Overload Frame

**Protocol Mechanisms**
- Bit stuffing with before/after stream example and worst-case overhead calculation
- Arbitration walkthrough showing exactly where a losing node backs off
- CRC-15 polynomial, Hamming distance, and detection guarantees
- ACK timeline showing transmitter vs. receiver bus levels

**Bit Rate ↔ Bus Length** — reference table from 10 kbit/s to 1 Mbit/s with bit-time segment diagram

**Five C/C++ Code Examples**
1. SocketCAN (Linux) — send/receive data and RTR frames with ID filtering
2. STM32 bare-metal bxCAN — init, bit timing calculation, TX mailbox, filter bank, RX FIFO
3. Software CRC-15 — bit-by-bit and byte-by-byte implementations
4. Bit stuffing encoder/decoder with expected output shown
5. Error frame monitor (SocketCAN) — decodes TEC/REC counters and all error types

**Summary table** tying all concepts together with CANopen relevance notes.

> **Series:** CANopen Programming Guide  
> **Topic:** CAN Bus Fundamentals — frame structure, arbitration, bit stuffing, ACK,
> CRC, dominant/recessive levels, differential signalling, and bit-rate vs. bus-length
> relationships.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Physical Layer — Differential Signalling](#2-physical-layer--differential-signalling)
3. [Dominant and Recessive Bit Levels](#3-dominant-and-recessive-bit-levels)
4. [CAN Frame Types](#4-can-frame-types)
   - 4.1 [Data Frame](#41-data-frame)
   - 4.2 [Remote Frame (RTR)](#42-remote-frame-rtr)
   - 4.3 [Error Frame](#43-error-frame)
   - 4.4 [Overload Frame](#44-overload-frame)
5. [Bit Stuffing](#5-bit-stuffing)
6. [Arbitration (Non-Destructive CSMA/CA)](#6-arbitration-non-destructive-csmaca)
7. [Cyclic Redundancy Check (CRC)](#7-cyclic-redundancy-check-crc)
8. [Acknowledgement (ACK) Mechanism](#8-acknowledgement-ack-mechanism)
9. [Bit Rate vs. Bus Length](#9-bit-rate-vs-bus-length)
10. [C/C++ Programming Examples](#10-cc-programming-examples)
    - 10.1 [SocketCAN — Linux](#101-socketcan--linux)
    - 10.2 [Bare-Metal / Embedded (STM32-style HAL)](#102-bare-metal--embedded-stm32-style-hal)
    - 10.3 [CRC-15 Calculation in Software](#103-crc-15-calculation-in-software)
    - 10.4 [Bit Stuffing Encoder / Decoder](#104-bit-stuffing-encoder--decoder)
    - 10.5 [Error Counter Monitoring](#105-error-counter-monitoring)
11. [Summary](#11-summary)

---

## 1. Introduction

The **Controller Area Network (CAN)** is a serial communication protocol developed by
Robert Bosch GmbH in 1983 and standardised as ISO 11898. Originally designed for
automotive in-vehicle networks, it has become the dominant fieldbus in industrial
automation, robotics, medical equipment, and embedded systems worldwide.

CAN is the physical and data-link foundation on which **CANopen** (CiA 301) is built.
A solid understanding of CAN fundamentals is therefore mandatory before working with
CANopen application objects, SDOs, PDOs, or NMT state machines.

Key design goals of CAN:

- Multi-master broadcast bus — every node can transmit, every node receives every frame.
- Priority-based, non-destructive arbitration with no message collisions.
- Hardware-level error detection and automatic retransmission.
- Differential signalling for high noise immunity.
- Scalable bit rates from 10 kbit/s to 1 Mbit/s (Classic CAN) and up to 8 Mbit/s
  (CAN FD data phase).

---

## 2. Physical Layer — Differential Signalling

CAN uses a **two-wire differential bus**: CAN-H (CAN High) and CAN-L (CAN Low).
The receiver measures the *difference* between the two lines, which gives exceptional
rejection of common-mode noise — switching transients, motor interference, and power
supply ripple are seen equally on both wires and cancel out in the differential.

```
Supply voltage (typically 5 V or 3.3 V)
                                    ┌──────────────┐
Node A ─── CAN TX ──────────────────►  CAN         │
                                    │  Transceiver ├─── CAN-H ──────┐
Node A ─── CAN RX ◄─────────────────  (e.g.        ├─── CAN-L ──────┤
                                    │  TJA1050)    │                │
                                    └──────────────┘                │
                                                                    │  (bus)
                                    ┌──────────────┐                │
Node B ─── CAN TX ──────────────────►  CAN         │                │
                                    │  Transceiver ├─── CAN-H ──────┤
Node B ─── CAN RX ◄─────────────────  (TJA1050)    ├─── CAN-L ──────┘
                                    └──────────────┘

  Termination: 120 Ω at each end of the bus
  Bus topology: linear (daisy-chain), stubs kept as short as possible
```

**Differential voltage levels (ISO 11898-2, high-speed CAN):**

```
         CAN-H voltage        CAN-L voltage      VDIFF = CAN-H − CAN-L
         ─────────────        ─────────────      ──────────────────────
Dominant   3.5 V                1.5 V              +2.0 V  (logic 0)
Recessive  2.5 V                2.5 V               0.0 V  (logic 1)
```

The bus is **wired-AND**: any node can pull the bus into the dominant state, but no single
node can force it recessive against another node actively driving dominant.

---

## 3. Dominant and Recessive Bit Levels

The ISO 11898 bit encoding is *inverted* with respect to intuition:

| Logical Bit | Bus State  | VDIFF  | Who "wins" in arbitration |
|-------------|-----------|--------|--------------------------|
| **0**       | Dominant  | ~2 V   | Overwrites recessive     |
| **1**       | Recessive | ~0 V   | Can be overwritten       |

This is the key to non-destructive arbitration: the node transmitting the **lower
identifier (more dominant bits)** wins the bus without any collision or corruption.

```
Dominant  ─────┐     ┌─────┐         ┌──────   (bus)
               │     │     │         │
Recessive      └─────┘     └─────────┘

Node A Tx: 0   0   0   1   0   1   1   0   0
Node B Tx: 0   0   1   0   0   1   0   0   1
                   ^
                   │ Node B reads dominant (0) while transmitting recessive (1)
                   └─ Node B loses arbitration, backs off, becomes receiver
```

---

## 4. CAN Frame Types

### 4.1 Data Frame

The Data Frame carries 0–8 bytes of application data (Classic CAN). It exists in two
forms: **Standard Frame Format (SFF)** with an 11-bit identifier, and **Extended Frame
Format (EFF)** with a 29-bit identifier.

#### Standard Data Frame (SFF) — Bit-Level Layout

```
 ┌───┬────────────────────┬───┬───┬───┬─────────┬──────────────┬───────────────┬───┬──────┐
 │SOF│   Identifier       │RTR│IDE│r0 │   DLC   │     Data     │     CRC       │ACK│  EOF │
 │ 1 │      11 bits       │ 1 │ 1 │ 1 │  4 bits │  0–64 bits   │  15+1 bits    │2b │ 7 b  │
 └───┴────────────────────┴───┴───┴───┴─────────┴──────────────┴───────────────┴───┴──────┘
   │         │             │   │   │      │             │               │         │      │
   │         │             │   │   │      │             │               │         │      └─ 7 recessive bits
   │         │             │   │   │      │             │               │         └─ ACK slot + ACK delimiter
   │         │             │   │   │      │             │               └─ 15-bit CRC + recessive delimiter
   │         │             │   │   │      │             └─ 0 to 8 bytes (0–64 bits)
   │         │             │   │   │      └─ Data Length Code (0–8)
   │         │             │   │   └─ Reserved bit (dominant)
   │         │             │   └─ IDE = 0 for standard frame
   │         │             └─ RTR = 0 for data frame, 1 for remote frame
   │         └─ Message priority / identifier (11 bits, MSB first)
   └─ Start Of Frame — single dominant bit
```

#### Extended Data Frame (EFF) — Bit-Level Layout

```
 ┌───┬──────────┬─────┬───┬──────────────────┬───┬───┬─────┬──────┬─────┬───┬──────┐
 │SOF│ Base ID  │ SRR │IDE│   Extended ID    │RTR│r1 │ r0  │ DLC  │Data │CRC│ EOF  │
 │ 1 │ 11 bits  │  1  │ 1 │     18 bits      │ 1 │ 1 │  1  │  4 b │0–8B │16b│ 7 b  │
 └───┴──────────┴─────┴───┴──────────────────┴───┴───┴─────┴──────┴─────┴───┴──────┘
   Total identifier = 29 bits  (Base 11 + Extended 18)
   IDE = 1 for extended frame
   SRR = Substitute Remote Request (always recessive)
```

**Field descriptions:**

| Field | Bits | Description |
|-------|------|-------------|
| SOF | 1 | Start Of Frame — dominant bit that synchronises all nodes |
| Identifier | 11 or 29 | Message ID, doubles as priority (lower = higher priority) |
| RTR | 1 | Remote Transmission Request: 0 = data, 1 = remote |
| IDE | 1 | ID Extension: 0 = standard, 1 = extended |
| r0, r1 | 1 each | Reserved, transmitted dominant |
| DLC | 4 | Data Length Code: number of data bytes (0–8) |
| Data | 0–64 | Application payload, MSB first |
| CRC | 15+1 | 15-bit CRC sequence + recessive delimiter |
| ACK | 2 | ACK slot (recessive by Tx, pulled dominant by any receiver) + delimiter |
| EOF | 7 | End Of Frame — 7 recessive bits |

### 4.2 Remote Frame (RTR)

A Remote Frame requests another node to transmit data for a given identifier. It is
structurally identical to a Data Frame but with RTR = 1 and **no data field**.

```
 ┌───┬─────────────────────┬───┬───┬───┬────────┬───────────┬────┬──────┐
 │SOF│     Identifier      │RTR│IDE│r0 │  DLC   │    CRC    │ACK │ EOF  │
 │ 1 │       11 bits       │ 1 │ 1 │ 1 │ 4 bits │  15+1 b   │ 2b │  7b  │
 └───┴─────────────────────┴───┴───┴───┴────────┴───────────┴────┴──────┘
                             ^
                             RTR = 1  (recessive)
                             No data field present.
                             DLC indicates expected response length.
```

### 4.3 Error Frame

When a node detects a protocol error it immediately transmits an **Error Flag** to abort
the current frame and signal all other nodes. There are two kinds of error flag depending
on the node's error state:

```
  Active Error Flag   : 6 dominant bits   (violates bit stuffing → all nodes detect it)
  Passive Error Flag  : 6 recessive bits  (may go undetected by other nodes)

  ┌──────────────────────────────┬──────────────────────────────┬──────────┐
  │      Error Flag(s)           │   Error Delimiter            │ IFS      │
  │  6–12 dominant or recessive  │   8 recessive bits           │ 3 rec.   │
  └──────────────────────────────┴──────────────────────────────┴──────────┘

  Superposition example (two nodes detect error simultaneously):
  Node A:  D D D D D D _ _ _ _ _ _ _ _ _ _   (active, 6 dominant)
  Node B:  _ _ D D D D D D _ _ _ _ _ _ _ _   (joins 2 bits late)
  Bus:     D D D D D D D D _ _ _ _ _ _ _ _   (up to 12 dominant bits possible)
           └──────────────┘└─────────────┘
            Error flags     Error delimiter (8 recessive)
```

**Error types detected by CAN hardware:**

| Error Type | Detection Mechanism |
|------------|---------------------|
| Bit error | Node reads back a different level than it transmitted |
| Stuff error | Six consecutive same-polarity bits detected (stuffing violation) |
| CRC error | Calculated CRC does not match transmitted CRC |
| Form error | Fixed-form field (EOF, delimiter) has wrong bit level |
| ACK error | Transmitter sees no dominant ACK in the ACK slot |

### 4.4 Overload Frame

An Overload Frame is generated by a receiver that needs extra time before it can
process the next frame. It has the same structure as an Active Error Frame but is only
permitted during specific inter-frame windows.

```
  ┌────────────────────────┬────────────────────────┐
  │   Overload Flag        │   Overload Delimiter   │
  │   6 dominant bits      │   8 recessive bits     │
  └────────────────────────┴────────────────────────┘

  A maximum of two consecutive overload frames may be generated.
  Overload frames are rare in modern implementations.
```

---

## 5. Bit Stuffing

CAN uses **NRZ (Non-Return-to-Zero)** encoding. Long runs of identical bits could
prevent receiver synchronisation. Bit stuffing solves this:

> **Rule:** After five consecutive bits of the same polarity in the bit stream (SOF
> through CRC sequence), the transmitter inserts a **complementary stuff bit**. The
> receiver strips it automatically.

```
  Original data stream:
  1  1  1  1  1  0  0  0  0  0  1  0  1  1  1  1  1  0  1

  After bit stuffing (stuff bits marked with 'S'):
  1  1  1  1  1 [S:0] 0  0  0  0  0 [S:1] 1  0  1  1  1  1  1 [S:0] 0  1

  Legend:  [S:0] = inserted stuff bit, value 0 (complement of five 1s)
           [S:1] = inserted stuff bit, value 1 (complement of five 0s)

  Worst-case overhead: 1 stuff bit for every 5 data bits → +20% overhead.
  In a maximum 8-byte data frame:
    - Bits subject to stuffing: up to 80 bits (SOF through CRC)
    - Maximum additional stuff bits: 15
    - Maximum frame length before ACK: ~111 bits
```

Fields **not** subject to bit stuffing: CRC delimiter, ACK field, EOF, IFS.

---

## 6. Arbitration (Non-Destructive CSMA/CA)

CAN uses **Carrier Sense Multiple Access / Collision Avoidance** with a wired-AND bus.
Every transmitting node monitors the bus simultaneously. Arbitration proceeds
bit-by-bit over the identifier field.

```
  Node A wants to send ID = 0x123  →  0b 000 1001 0001 1  (binary, 11 bits)
  Node B wants to send ID = 0x120  →  0b 000 1001 0000 0  (binary, 11 bits)

  Bit position:  10  9  8  7  6  5  4  3  2  1  0
  Node A Tx:      0  0  0  1  0  0  1  0  0  0  1   (0x123)
  Node B Tx:      0  0  0  1  0  0  1  0  0  0  0   (0x120)
  Bus (wired-AND):0  0  0  1  0  0  1  0  0  0  0
                                                  ^
                                                  Bit 0: Node A transmitted 1 (recessive)
                                                         but reads 0 (dominant) from bus.
                                                         Node A detects it LOST arbitration.
                                                         Node A becomes receiver immediately.
                                                         Node B continues uninterrupted.

  Rules:
  ┌────────────────────────────────────────────────────────┐
  │  Lower numeric ID  →  Higher priority  →  Wins bus     │
  │  Loser backs off   →  retries after bus goes idle      │
  │  No message is destroyed — winner's frame is intact    │
  └────────────────────────────────────────────────────────┘
```

**Practical implications for CANopen:** CANopen reserves low identifiers for
time-critical messages (SYNC = 0x080, EMCY = 0x081+, TPDO1 = 0x181+), ensuring they
always win over less critical SDO or heartbeat traffic.

---

## 7. Cyclic Redundancy Check (CRC)

CAN uses a **15-bit CRC** with the generator polynomial:

```
  G(x) = x^15 + x^14 + x^10 + x^8 + x^7 + x^4 + x^3 + x^0

  Binary:  1 1000 0110 1001 1001  (16 bits including implicit leading 1)
  Hex:     0xC599

  The CRC is calculated over: SOF + Identifier + Control + Data fields
  (all bits including stuff bits, before stuffing is stripped).
```

The 15-bit CRC can reliably detect:
- All single and double bit errors.
- All odd numbers of bit errors.
- All burst errors of length ≤ 15 bits.
- Most burst errors of length 16–21 bits.

Hamming distance = 6 for frames up to 127 bits — this means up to 5 random bit errors
are guaranteed to be detected in any single frame.

---

## 8. Acknowledgement (ACK) Mechanism

CAN ACK is **frame-level**, not node-level. Any receiver that successfully validates
CRC pulls the ACK slot dominant. The transmitter does not know *which* node (or how
many) acknowledged.

```
  CRC Field              ACK Field            EOF
  ┌──────────────────┬───────┬──────────┬─────────┬─────────────┐
  │  CRC Sequence    │CRC Del│ ACK Slot │ ACK Del │    EOF      │
  │    15 bits       │   1   │    1     │    1    │   7 rec.    │
  └──────────────────┴───────┴──────────┴─────────┴─────────────┘
                      rec.    rec.→dom.  recessive  recessive

  Timeline during ACK slot:
  ─────────────────────────────────────────────────────────────

  Transmitter sends:  recessive (1) ──────────────────────►
                                              │
  Receiver(s) read CRC OK, pull:  dominant (0)────────────►
                                              │
  Bus state:          dominant  ◄─────────────┘  (wired-AND wins)

  Transmitter reads back dominant → ACK received → frame accepted.
  If bus stays recessive → ACK Error → transmitter increments TEC,
                            schedules retransmission.
```

---

## 9. Bit Rate vs. Bus Length

The **propagation delay** of the electrical signal through the cable and transceivers
is the limiting factor. For arbitration to work correctly, every node must sample the
bit at the same logical time. This requires the complete signal round-trip (transmitter
→ far end → back) to fit within a single bit time.

```
  Rule of thumb:  bus_length_max ≈ (1 / bit_rate) × 50 m/µs × 0.5

  More precisely (ISO 11898-1):
  t_prop ≤ 0.4 × t_bit   (40% of the bit time for round-trip propagation)

  ┌─────────────┬─────────────────────┬──────────────────────────────┐
  │  Bit Rate   │  Bit Time           │  Max Bus Length (typ.)       │
  ├─────────────┼─────────────────────┼──────────────────────────────┤
  │  1 Mbit/s   │  1 µs               │  ~40 m                       │
  │  500 kbit/s │  2 µs               │  ~100 m                      │
  │  250 kbit/s │  4 µs               │  ~250 m                      │
  │  125 kbit/s │  8 µs               │  ~500 m                      │
  │   50 kbit/s │  20 µs              │  ~1000 m                     │
  │   10 kbit/s │  100 µs             │  ~5000 m                     │
  └─────────────┴─────────────────────┴──────────────────────────────┘

  Bit time segments (CiA 601 recommended for 1 Mbit/s, 80% sample point):
  ┌──────────────────────────────────────────────────────────────────┐
  │ SYNC_SEG │ PROP_SEG │ PHASE_SEG1 │ PHASE_SEG2 │  ← total = tBit  │
  │   1 TQ   │  1–8 TQ  │   1–8 TQ   │   1–8 TQ   │                  │
  └──────────────────────────────────────────────────────────────────┘
        ^         ^              ^           ^
        │         │              │           └─ After sample point, adjustable by SJW
        │         │              └─ Before sample point
        │         └─ Propagation compensation
        └─ Hard synchronisation edge

  TQ = Time Quantum = period of CAN clock prescaler output
  Sample point is between PHASE_SEG1 and PHASE_SEG2.
  SJW (Synchronisation Jump Width) adjusts for clock drift between nodes.
```

**Practical rule for CANopen systems:** The CiA 301 standard mandates all nodes on a
network use identical bit timing. The network designer must select a bit rate
consistent with the physical bus length and then configure every node identically using
the NMT or LSS (Layer Setting Services, CiA 305) protocol.

---

## 10. C/C++ Programming Examples

### 10.1 SocketCAN — Linux

SocketCAN is the standard Linux kernel CAN interface. It exposes CAN hardware as
network sockets, enabling standard POSIX socket calls.

```c
/*
 * socketcan_basic.c
 * Demonstrates sending and receiving standard CAN frames on Linux
 * using the SocketCAN interface (socket(AF_CAN, SOCK_RAW, CAN_RAW)).
 *
 * Build:  gcc -o socketcan_basic socketcan_basic.c
 * Run:    ./socketcan_basic vcan0
 * Setup:  sudo modprobe vcan
 *         sudo ip link add dev vcan0 type vcan
 *         sudo ip link set vcan0 up
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

/* ── Helper: print a CAN frame ───────────────────────────────────────── */
static void print_frame(const struct can_frame *f)
{
    printf("[%03X] len=%u  data:", f->can_id & CAN_EFF_MASK, f->len);
    for (int i = 0; i < f->len; i++)
        printf(" %02X", f->data[i]);
    printf("\n");
}

/* ── Build and send a Data Frame ─────────────────────────────────────── */
static int send_data_frame(int sock, uint32_t can_id,
                            const uint8_t *data, uint8_t dlc)
{
    struct can_frame frame = {0};

    /* Set extended frame flag if ID > 11-bit range */
    frame.can_id  = (can_id > 0x7FFu) ? (can_id | CAN_EFF_FLAG) : can_id;
    frame.len     = dlc;                     /* 0–8 bytes               */
    memcpy(frame.data, data, dlc);

    ssize_t nbytes = write(sock, &frame, sizeof(frame));
    if (nbytes != sizeof(frame)) {
        perror("write");
        return -1;
    }
    printf("Sent:  ");
    print_frame(&frame);
    return 0;
}

/* ── Send a Remote Frame (RTR) ───────────────────────────────────────── */
static int send_remote_frame(int sock, uint32_t can_id, uint8_t dlc)
{
    struct can_frame frame = {0};
    frame.can_id = can_id | CAN_RTR_FLAG;
    frame.len    = dlc;                      /* Expected response length */

    ssize_t nbytes = write(sock, &frame, sizeof(frame));
    if (nbytes != sizeof(frame)) { perror("write RTR"); return -1; }
    printf("Sent RTR for ID 0x%03X, expected DLC=%u\n", can_id, dlc);
    return 0;
}

/* ── Receive one frame with optional CAN ID filter ───────────────────── */
static int receive_frame(int sock, struct can_frame *out_frame)
{
    ssize_t nbytes = read(sock, out_frame, sizeof(*out_frame));
    if (nbytes < 0) { perror("read"); return -1; }
    if (nbytes < (ssize_t)sizeof(*out_frame)) {
        fprintf(stderr, "Incomplete frame\n");
        return -1;
    }
    printf("Recv:  ");
    print_frame(out_frame);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>  e.g. vcan0\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* 1. Create raw CAN socket */
    int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return EXIT_FAILURE; }

    /* 2. Resolve interface index */
    struct ifreq ifr;
    strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX"); close(sock); return EXIT_FAILURE;
    }

    /* 3. Apply a receive filter — accept only COB-IDs 0x180..0x1FF (TPDO1) */
    struct can_filter rfilter[] = {
        { .can_id   = 0x180,
          .can_mask = CAN_SFF_MASK & ~0x07Fu }  /* match 0x180–0x1FF */
    };
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER,
               &rfilter, sizeof(rfilter));

    /* 4. Bind to the interface */
    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock); return EXIT_FAILURE;
    }

    /* 5. Send an 8-byte data frame with ID 0x181 (TPDO1 from node 1) */
    uint8_t payload[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    send_data_frame(sock, 0x181, payload, 8);

    /* 6. Send a Remote Frame requesting 2 bytes from ID 0x182 */
    send_remote_frame(sock, 0x182, 2);

    /* 7. Receive one frame */
    struct can_frame rx;
    receive_frame(sock, &rx);

    close(sock);
    return EXIT_SUCCESS;
}
```

---

### 10.2 Bare-Metal / Embedded (STM32-style HAL)

This example shows the register-level concepts for configuring a bxCAN peripheral
(used on STM32F1/F2/F4) and transmitting/receiving frames. The HAL abstractions
directly mirror the hardware registers described in the STM32 reference manual.

```c
/*
 * stm32_can_basics.c  (pseudo-HAL, illustrative — adapt to your specific MCU)
 *
 * Demonstrates:
 *   - CAN peripheral initialisation and bit timing for 500 kbit/s @ 36 MHz APB1
 *   - Transmitting a standard data frame
 *   - Configuring a filter bank (16-bit ID list mode)
 *   - Receiving a frame from FIFO 0
 */

#include <stdint.h>
#include <string.h>

/* ── Minimal register definitions (replace with your CMSIS header) ─── */
typedef struct {
    volatile uint32_t MCR, MSR, TSR;
    volatile uint32_t RF0R, RF1R;
    volatile uint32_t IER, ESR, BTR;
    uint32_t          RESERVED[88];
    struct { volatile uint32_t TIR, TDTR, TDLR, TDHR; } sTxMailBox[3];
    struct { volatile uint32_t RIR, RDTR, RDLR, RDHR; } sFIFOMailBox[2];
    uint32_t          RESERVED2[12];
    volatile uint32_t FMR, FM1R, RESERVED3, FS1R, RESERVED4;
    volatile uint32_t FFA1R, RESERVED5, FA1R;
    uint32_t          RESERVED6[8];
    struct { volatile uint32_t FR1, FR2; } sFilterRegister[28];
} CAN_TypeDef;

#define CAN1  ((CAN_TypeDef *)0x40006400UL)

/* CAN MCR bits */
#define CAN_MCR_INRQ   (1u << 0)   /* Init request         */
#define CAN_MCR_SLEEP  (1u << 1)   /* Sleep mode request   */
#define CAN_MCR_ABOM   (1u << 6)   /* Auto bus-off manage  */
#define CAN_MCR_AWUM   (1u << 5)   /* Auto wakeup          */
#define CAN_MCR_TXFP   (1u << 2)   /* TX FIFO priority     */

/* CAN MSR bits */
#define CAN_MSR_INAK   (1u << 0)   /* Init acknowledge     */
#define CAN_MSR_SLAK   (1u << 1)   /* Sleep acknowledge    */

/* BTR register bit fields */
#define CAN_BTR_SJW(n) (((n) & 0x3u) << 24)
#define CAN_BTR_TS2(n) (((n) & 0x7u) << 20)
#define CAN_BTR_TS1(n) (((n) & 0xFu) << 16)
#define CAN_BTR_BRP(n) (((n) & 0x3FFu) << 0)

/* TX Mailbox TIR bits */
#define CAN_TIxR_TXRQ  (1u << 0)   /* Transmit request     */
#define CAN_TIxR_RTR   (1u << 1)   /* Remote frame         */
#define CAN_TIxR_IDE   (1u << 2)   /* Extended ID flag     */
#define CAN_TIxR_STID_SHIFT  21    /* Standard ID position */

/* Filter mode/scale */
#define CAN_FMR_FINIT  (1u << 0)   /* Filter init mode     */

/* ── CAN frame structure ─────────────────────────────────────────────── */
typedef struct {
    uint32_t id;       /* 11-bit or 29-bit CAN ID */
    uint8_t  ide;      /* 0 = standard, 1 = extended */
    uint8_t  rtr;      /* 0 = data frame, 1 = remote frame */
    uint8_t  dlc;      /* 0–8 bytes */
    uint8_t  data[8];
} CAN_Frame_t;

/* ── Simple busy-wait helper ─────────────────────────────────────────── */
static void spin_until(volatile uint32_t *reg, uint32_t mask, uint32_t val,
                        uint32_t timeout_ms)
{
    /* In real code replace with a proper timeout using SysTick */
    (void)timeout_ms;
    while ((*reg & mask) != val) { __asm__("nop"); }
}

/*
 * can_init()
 * Configure CAN1 for 500 kbit/s assuming APB1 = 36 MHz.
 *
 *   tq    = APB1 / (BRP+1) = 36 MHz / 4 = 9 MHz  → tq = 111 ns
 *   tBit  = tq × (1 + TS1+1 + TS2+1)
 *         = 111 ns × (1 + 13 + 4) = 111 ns × 18 = 2 µs = 500 kbit/s
 *   Sample point = (1 + 13) / 18 = 77.8%
 */
void can_init(void)
{
    /* 1. Request initialisation mode */
    CAN1->MCR &= ~CAN_MCR_SLEEP;
    CAN1->MCR |=  CAN_MCR_INRQ;
    spin_until(&CAN1->MSR, CAN_MSR_INAK, CAN_MSR_INAK, 10);

    /* 2. Configure bit timing */
    CAN1->BTR = CAN_BTR_SJW(0)   /* SJW = 1 tq                    */
              | CAN_BTR_TS2(3)   /* PHASE_SEG2 = 4 tq             */
              | CAN_BTR_TS1(12)  /* PHASE_SEG1 = 13 tq            */
              | CAN_BTR_BRP(3);  /* BRP = 3 → prescaler divides by 4 */

    /* 3. Enable auto bus-off recovery */
    CAN1->MCR |= CAN_MCR_ABOM;

    /* 4. Configure filter bank 0 — accept 0x600..0x67F (SDO Rx window) */
    CAN1->FMR |=  CAN_FMR_FINIT;       /* Enter filter init mode       */
    CAN1->FA1R &= ~(1u << 0);          /* Deactivate filter 0          */
    CAN1->FS1R &= ~(1u << 0);          /* 16-bit scale                 */
    CAN1->FM1R |=  (1u << 0);          /* ID list mode                 */

    /* Filter register: two 16-bit IDs packed into FR1                  */
    /* Each 16-bit entry: [15:5] = ID[10:0], [4] = RTR, [3] = IDE       */
    CAN1->sFilterRegister[0].FR1 =
        ((0x600u << 5) | 0x0000u) |    /* ID 0x600, data, standard      */
        ((0x601u << 5) | 0x0000u) << 16; /* ID 0x601, data, standard    */
    CAN1->FFA1R &= ~(1u << 0);          /* Route to FIFO 0              */
    CAN1->FA1R  |=  (1u << 0);          /* Activate filter 0            */
    CAN1->FMR   &= ~CAN_FMR_FINIT;      /* Leave filter init mode       */

    /* 5. Leave initialisation mode */
    CAN1->MCR &= ~CAN_MCR_INRQ;
    spin_until(&CAN1->MSR, CAN_MSR_INAK, 0, 10);
}

/*
 * can_transmit()
 * Send a CAN frame using the first free TX mailbox.
 * Returns 0 on success, -1 if no mailbox is free.
 */
int can_transmit(const CAN_Frame_t *f)
{
    /* Find a free TX mailbox (TSR bits 26,27,28 = mailbox 0,1,2 empty) */
    uint8_t mbox = 0xFF;
    if (CAN1->TSR & (1u << 26)) mbox = 0;
    else if (CAN1->TSR & (1u << 27)) mbox = 1;
    else if (CAN1->TSR & (1u << 28)) mbox = 2;
    if (mbox == 0xFF) return -1;   /* All mailboxes busy */

    /* Write Identifier */
    uint32_t tir = 0;
    if (f->ide) {
        tir = (f->id << 3) | CAN_TIxR_IDE;       /* Extended: ID in [31:3] */
    } else {
        tir = (f->id << CAN_TIxR_STID_SHIFT);     /* Standard: ID in [31:21] */
    }
    if (f->rtr) tir |= CAN_TIxR_RTR;
    CAN1->sTxMailBox[mbox].TIR = tir;

    /* Write DLC */
    CAN1->sTxMailBox[mbox].TDTR = f->dlc & 0x0Fu;

    /* Write data (two 32-bit registers, little-endian) */
    CAN1->sTxMailBox[mbox].TDLR =
        ((uint32_t)f->data[3] << 24) | ((uint32_t)f->data[2] << 16) |
        ((uint32_t)f->data[1] <<  8) |  (uint32_t)f->data[0];
    CAN1->sTxMailBox[mbox].TDHR =
        ((uint32_t)f->data[7] << 24) | ((uint32_t)f->data[6] << 16) |
        ((uint32_t)f->data[5] <<  8) |  (uint32_t)f->data[4];

    /* Request transmission */
    CAN1->sTxMailBox[mbox].TIR |= CAN_TIxR_TXRQ;
    return 0;
}

/*
 * can_receive()
 * Read one frame from FIFO 0 (if pending).
 * Returns 1 if a frame was read, 0 if FIFO is empty.
 */
int can_receive(CAN_Frame_t *out)
{
    if ((CAN1->RF0R & 0x03u) == 0)
        return 0;   /* FIFO 0 empty */

    uint32_t rir  = CAN1->sFIFOMailBox[0].RIR;
    uint32_t rdtr = CAN1->sFIFOMailBox[0].RDTR;
    uint32_t rdlr = CAN1->sFIFOMailBox[0].RDLR;
    uint32_t rdhr = CAN1->sFIFOMailBox[0].RDHR;

    out->ide = (rir & CAN_TIxR_IDE) ? 1u : 0u;
    out->rtr = (rir & CAN_TIxR_RTR) ? 1u : 0u;
    out->id  = out->ide ? (rir >> 3) : (rir >> CAN_TIxR_STID_SHIFT);
    out->dlc = rdtr & 0x0Fu;

    out->data[0] = (rdlr >>  0) & 0xFFu;
    out->data[1] = (rdlr >>  8) & 0xFFu;
    out->data[2] = (rdlr >> 16) & 0xFFu;
    out->data[3] = (rdlr >> 24) & 0xFFu;
    out->data[4] = (rdhr >>  0) & 0xFFu;
    out->data[5] = (rdhr >>  8) & 0xFFu;
    out->data[6] = (rdhr >> 16) & 0xFFu;
    out->data[7] = (rdhr >> 24) & 0xFFu;

    /* Release the mailbox */
    CAN1->RF0R |= (1u << 5);   /* Set RFOM0 */
    return 1;
}
```

---

### 10.3 CRC-15 Calculation in Software

Understanding (and testing) the CAN CRC in software is valuable for simulation,
testing tools, and protocol analysers.

```c
/*
 * can_crc15.c
 * Software implementation of the CAN 15-bit CRC.
 * Polynomial: 0xC599  (x^15+x^14+x^10+x^8+x^7+x^4+x^3+1)
 *
 * Note: In real hardware the CRC engine processes the raw bit stream
 * including stuff bits. This implementation operates on the pre-stuff
 * logical bit stream for clarity.
 */

#include <stdint.h>
#include <stdio.h>

#define CAN_CRC15_POLY  0x4599u   /* Bit-reflected form of 0xC599 (MSB first) */

/*
 * can_crc15_bit()
 * Update a running 15-bit CRC with one bit (0 or 1).
 */
static inline uint16_t can_crc15_bit(uint16_t crc, uint8_t bit)
{
    uint16_t crc_next = (crc << 1) | (bit & 1u);
    if (crc_next & 0x8000u)            /* Bit 15 was set before shift */
        crc_next ^= CAN_CRC15_POLY;
    return crc_next & 0x7FFFu;
}

/*
 * can_crc15_byte()
 * Update a running 15-bit CRC with one byte, MSB first.
 */
static inline uint16_t can_crc15_byte(uint16_t crc, uint8_t byte)
{
    for (int i = 7; i >= 0; i--)
        crc = can_crc15_bit(crc, (byte >> i) & 1u);
    return crc;
}

/*
 * can_crc15_frame()
 * Calculate the CRC for a complete standard data frame's protected field:
 *   SOF (1 bit) + ID (11) + RTR (1) + IDE (1) + r0 (1) + DLC (4) + Data (0..64)
 *
 * Parameters:
 *   id    : 11-bit CAN identifier
 *   rtr   : 0 = data frame, 1 = remote frame
 *   dlc   : data length code (0–8)
 *   data  : pointer to payload bytes
 *
 * Returns: 15-bit CRC value
 */
uint16_t can_crc15_frame(uint16_t id, uint8_t rtr, uint8_t dlc,
                          const uint8_t *data)
{
    uint16_t crc = 0;

    /* SOF — 1 dominant bit */
    crc = can_crc15_bit(crc, 0);

    /* Identifier — 11 bits, MSB first */
    for (int i = 10; i >= 0; i--)
        crc = can_crc15_bit(crc, (id >> i) & 1u);

    /* RTR bit */
    crc = can_crc15_bit(crc, rtr & 1u);

    /* IDE = 0 (standard frame) */
    crc = can_crc15_bit(crc, 0);

    /* r0 = 0 (reserved, dominant) */
    crc = can_crc15_bit(crc, 0);

    /* DLC — 4 bits, MSB first */
    for (int i = 3; i >= 0; i--)
        crc = can_crc15_bit(crc, (dlc >> i) & 1u);

    /* Data bytes */
    for (int b = 0; b < dlc; b++)
        crc = can_crc15_byte(crc, data[b]);

    return crc;
}

/* ── Demo ────────────────────────────────────────────────────────────── */
int main(void)
{
    /* Example: ID=0x123, DLC=4, data={0xDE,0xAD,0xBE,0xEF} */
    uint8_t  payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint16_t crc = can_crc15_frame(0x123, 0, 4, payload);
    printf("CAN CRC-15 = 0x%04X\n", crc);
    /* Expected output will vary; use a hardware CAN analyser to verify */
    return 0;
}
```

---

### 10.4 Bit Stuffing Encoder / Decoder

```c
/*
 * bit_stuffing.c
 * Software demonstration of CAN NRZ bit stuffing and de-stuffing.
 * Operates on a packed bit array represented as uint8_t[].
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_BITS  256

typedef struct {
    uint8_t  bits[MAX_BITS];  /* One bit per element (0 or 1) */
    uint32_t count;
} BitStream;

/* Push one bit onto a stream */
static void bs_push(BitStream *s, uint8_t bit)
{
    if (s->count < MAX_BITS)
        s->bits[s->count++] = bit & 1u;
}

/*
 * stuff_bits()
 * Apply CAN bit stuffing: after 5 consecutive same-polarity bits,
 * insert a complementary stuff bit.
 *
 * Input:  src  — logical bit stream (SOF through CRC, no stuff bits)
 * Output: dst  — stuffed bit stream
 */
void stuff_bits(const BitStream *src, BitStream *dst)
{
    dst->count = 0;
    int run   = 1;            /* current run length */
    uint8_t last = src->bits[0];

    bs_push(dst, last);

    for (uint32_t i = 1; i < src->count; i++) {
        uint8_t bit = src->bits[i];

        if (bit == last) {
            run++;
            if (run == 5) {
                bs_push(dst, bit);    /* the 5th bit          */
                bs_push(dst, bit ^ 1); /* insert stuff bit    */
                run  = 1;
                last = bit ^ 1;
                continue;
            }
        } else {
            run  = 1;
            last = bit;
        }
        bs_push(dst, bit);
    }
}

/*
 * destuff_bits()
 * Remove CAN stuff bits from a received bit stream.
 * Returns 0 on success, -1 if a stuff error is detected.
 */
int destuff_bits(const BitStream *src, BitStream *dst)
{
    dst->count = 0;
    int     run  = 1;
    uint8_t last = src->bits[0];

    bs_push(dst, last);

    for (uint32_t i = 1; i < src->count; i++) {
        uint8_t bit = src->bits[i];

        if (bit == last) {
            run++;
            if (run == 5) {
                /* Next bit MUST be the complement (stuff bit) */
                i++;
                if (i >= src->count) return -1;   /* truncated stream */
                if (src->bits[i] == bit) {
                    /* Six consecutive identical bits = stuff error */
                    fprintf(stderr, "Stuff error at bit %u\n", i);
                    return -1;
                }
                /* Discard the stuff bit, reset run */
                run  = 1;
                last = src->bits[i];
                continue;
            }
        } else {
            run  = 1;
            last = bit;
        }
        bs_push(dst, bit);
    }
    return 0;
}

/* ── Demo ────────────────────────────────────────────────────────────── */
int main(void)
{
    /* Construct a stream with a run of 5 ones */
    BitStream orig = { .count = 10,
                       .bits  = {1,0,1,1,1,1,1,0,1,0} };
    BitStream stuffed   = {0};
    BitStream recovered = {0};

    stuff_bits(&orig, &stuffed);
    destuff_bits(&stuffed, &recovered);

    printf("Original  (%2u bits): ", orig.count);
    for (uint32_t i = 0; i < orig.count; i++) printf("%u", orig.bits[i]);

    printf("\nStuffed   (%2u bits): ", stuffed.count);
    for (uint32_t i = 0; i < stuffed.count; i++) printf("%u", stuffed.bits[i]);

    printf("\nRecovered (%2u bits): ", recovered.count);
    for (uint32_t i = 0; i < recovered.count; i++) printf("%u", recovered.bits[i]);
    printf("\n");

    return 0;
}
```

**Expected output:**
```
Original  (10 bits): 1011111010
Stuffed   (11 bits): 10111110010
                              ^
                              Inserted stuff bit (0) after five 1s
Recovered (10 bits): 1011111010
```

---

### 10.5 Error Counter Monitoring

CAN nodes use two hardware error counters to manage error confinement.

```
  Error states and thresholds:
  ┌────────────────────────────────────────────────────────────────────┐
  │  TEC / REC = 0                                                     │
  │        │                                                           │
  │        ▼                                                           │
  │  ┌──────────────┐   TEC > 127  ┌──────────────┐                   │
  │  │    Error     │──────────────►│    Error     │                   │
  │  │    Active    │              │    Passive   │                   │
  │  │ (6 dom. flag)│◄─────────────│ (6 rec. flag)│                   │
  │  └──────────────┘   TEC ≤ 127  └──────────────┘                   │
  │                                       │ TEC > 255                 │
  │                                       ▼                           │
  │                               ┌──────────────┐                   │
  │                               │   Bus Off    │ (disconnected)     │
  │                               │  TEC > 255   │                   │
  │                               └──────────────┘                   │
  │                                       │ 128 × 11 rec. bits       │
  │                                       ▼                           │
  │                               ┌──────────────┐                   │
  │                               │    Error     │                   │
  │                               │    Active    │ (auto-recovery)   │
  │                               └──────────────┘                   │
  └────────────────────────────────────────────────────────────────────┘

  Counter rules (abbreviated):
  +8  to TEC  for each transmitted error
  +1  to REC  for each received error
  −1  from TEC/REC after each successful transmission/reception
```

```c
/*
 * can_error_monitor.c  (SocketCAN version)
 * Subscribe to CAN error frames on Linux to monitor bus health.
 * Error frames are delivered as special CAN frames with CAN_ERR_FLAG set.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

/* Decode and print an error frame */
static void decode_error_frame(const struct can_frame *ef)
{
    uint32_t err = ef->can_id & CAN_ERR_MASK;
    printf("[ERROR] 0x%08X ", err);

    if (err & CAN_ERR_TX_TIMEOUT)   printf("TX_TIMEOUT ");
    if (err & CAN_ERR_LOSTARB)      printf("LOST_ARBITRATION(bit=%u) ",
                                           ef->data[0]);
    if (err & CAN_ERR_CRTL) {
        printf("CONTROLLER(");
        if (ef->data[1] & CAN_ERR_CRTL_RX_OVERFLOW) printf("RX_OVF ");
        if (ef->data[1] & CAN_ERR_CRTL_TX_OVERFLOW) printf("TX_OVF ");
        if (ef->data[1] & CAN_ERR_CRTL_RX_PASSIVE)  printf("RX_PASSIVE ");
        if (ef->data[1] & CAN_ERR_CRTL_TX_PASSIVE)  printf("TX_PASSIVE ");
        if (ef->data[1] & CAN_ERR_CRTL_RX_WARNING)  printf("RX_WARN ");
        if (ef->data[1] & CAN_ERR_CRTL_TX_WARNING)  printf("TX_WARN ");
        printf(") ");
    }
    if (err & CAN_ERR_PROT) {
        printf("PROTOCOL(");
        if (ef->data[2] & CAN_ERR_PROT_BIT)   printf("BIT ");
        if (ef->data[2] & CAN_ERR_PROT_FORM)  printf("FORM ");
        if (ef->data[2] & CAN_ERR_PROT_STUFF) printf("STUFF ");
        if (ef->data[2] & CAN_ERR_PROT_CRC)   printf("CRC ");
        printf(") ");
    }
    if (err & CAN_ERR_ACK)   printf("NO_ACK ");
    if (err & CAN_ERR_BUSOFF) printf("BUS_OFF ");

    /* Error counters reported in data[6] (TEC) and data[7] (REC) */
    printf(" TEC=%u REC=%u", ef->data[6], ef->data[7]);
    printf("\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return EXIT_FAILURE; }

    /* Enable reception of error frames */
    can_err_mask_t err_mask = CAN_ERR_MASK;   /* subscribe to all errors */
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
               &err_mask, sizeof(err_mask));

    struct ifreq ifr;
    strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    printf("Monitoring error frames on %s. Press Ctrl+C to stop.\n", argv[1]);
    struct can_frame frame;
    while (1) {
        ssize_t n = read(sock, &frame, sizeof(frame));
        if (n < 0) { perror("read"); break; }

        if (frame.can_id & CAN_ERR_FLAG)
            decode_error_frame(&frame);
    }

    close(sock);
    return EXIT_SUCCESS;
}
```

---

## 11. Summary

The table below consolidates the key CAN bus fundamentals covered in this document.

| Topic | Key Points |
|-------|-----------|
| **Physical layer** | Two-wire differential bus (CAN-H / CAN-L), ISO 11898-2 high-speed transceiver, 120 Ω termination at each end, linear topology |
| **Bit levels** | Dominant = logic 0 ≈ +2 V differential; Recessive = logic 1 ≈ 0 V differential; bus is wired-AND |
| **Frame types** | Data (0–8 bytes payload), Remote (no payload, RTR=1), Error (6-bit flag + 8-bit delimiter), Overload (rare, receiver delay) |
| **Identifier** | 11-bit (SFF) or 29-bit (EFF); lower number = higher bus priority |
| **Bit stuffing** | After 5 consecutive same-polarity bits, transmitter inserts a complement bit; receiver removes it; ensures synchronisation edges |
| **Arbitration** | Non-destructive CSMA/CA; nodes monitor the bus bit-by-bit; first node to transmit a recessive bit while the bus is dominant backs off; winner continues uninterrupted |
| **CRC** | 15-bit CRC with polynomial x¹⁵+x¹⁴+x¹⁰+x⁸+x⁷+x⁴+x³+1 (0xC599); covers SOF through data field; Hamming distance 6 for ≤127-bit frames |
| **ACK** | Any receiver with correct CRC pulls ACK slot dominant; transmitter expects dominant ACK or raises an ACK error and retransmits |
| **Bit rate vs. length** | Inverse relationship: higher bit rate → shorter maximum bus length; 1 Mbit/s ≈ 40 m; 125 kbit/s ≈ 500 m; governed by round-trip propagation fitting within one bit time |
| **Error confinement** | TEC and REC counters; Error Active (TEC/REC ≤ 127), Error Passive (TEC > 127), Bus Off (TEC > 255); auto-recovery after 128 × 11 recessive bits |
| **CANopen relevance** | CANopen uses 11-bit identifiers structured as Function Code (4 bits) + Node-ID (7 bits); priority ordering mirrors CAN arbitration so SYNC/EMCY always beat SDO/heartbeat traffic |

> **Next topic:** [02 — CANopen Architecture and Object Dictionary](02_CANopen_Architecture.md)

---

*Document revision: 1.0 — based on ISO 11898-1:2015, ISO 11898-2:2016, and CiA 301 v4.2.0*