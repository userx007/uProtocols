# 32. CANopen FD (CiA 1301)

**CAN FD Frame Format** — ASCII diagrams comparing classical CAN vs. CAN FD bit fields, DLC-to-length mapping table, and padding behaviour for non-standard payload sizes.
**Bit-Rate Switching** — Timeline diagram of the nominal/data phase split, bit timing parameter tables with concrete 80 MHz clock examples, and a bus-length vs. data-rate constraint table.
**CANopen FD Encoding Differences** — Side-by-side 29-bit COB-ID field layout vs. classic 11-bit, PDU layout comparison for USDO vs. classic SDO.
**USDO** — Sequence diagrams comparing single-frame USDO vs. classic multi-frame SDO latency, CS byte bit field breakdown, and abort code extensions.
**LPDO** — Frame structure ASCII diagram, capacity comparison vs. classic PDO, and full object dictionary index listing for all new sub-indices.
**Migration Strategy** — Three strategies (island upgrade with gateway, full replacement, hybrid) with ASCII topology diagrams, a complete migration checklist, and a decision guide tree.
**Hardware & Driver Requirements** — Controller feature comparison table, transceiver selection guide, split termination wiring diagram, and Linux SocketCAN setup commands.
**C/C++ Examples** — Six complete, commented code files: type definitions, M_CAN bit timing setup, USDO client (upload + download with multi-frame support), LPDO mapping configuration, LPDO receive handler with bit-offset unpacking, and a full Linux SocketCAN node initialisation with NMT state machine diagram.


> **Standard:** CiA 1301 — CANopen FD Application Layer and Communication Profile  
> **Prerequisite Standards:** ISO 11898-1:2015 (CAN FD physical layer), CiA 301 (classic CANopen)

---

## Table of Contents

1. [Introduction and Motivation](#1-introduction-and-motivation)
2. [CAN FD Frame Format](#2-can-fd-frame-format)
3. [Bit-Rate Switching — Nominal vs. Data Phase](#3-bit-rate-switching--nominal-vs-data-phase)
4. [CANopen FD Frame Encoding Differences](#4-canopen-fd-frame-encoding-differences)
5. [USDO — Replacing Classic SDO](#5-usdo--replacing-classic-sdo)
6. [LPDO — Replacing Classic PDO](#6-lpdo--replacing-classic-pdo)
7. [Migration Strategy from Classic CANopen](#7-migration-strategy-from-classic-canopen)
8. [Hardware and Driver Requirements](#8-hardware-and-driver-requirements)
9. [C/C++ Programming Examples](#9-cc-programming-examples)
10. [Summary](#10-summary)

---

## 1. Introduction and Motivation

Classic CANopen (CiA 301) was standardised for classical CAN, which supports a maximum payload of **8 bytes** per frame and a maximum bit rate of **1 Mbit/s**. As industrial automation, robotics, and automotive subsystems demand higher throughput and larger data payloads, these limits become a bottleneck.

**CAN FD (Flexible Data-Rate)**, defined in ISO 11898-1:2015, overcomes both restrictions:

- Payload expanded from 8 bytes up to **64 bytes** per frame.
- Data-phase bit rate switchable up to **8 Mbit/s** (practical maximum: 5 Mbit/s on most hardware).

**CiA 1301** — *CANopen FD* — adapts the proven CANopen application layer to CAN FD, replacing and extending:

| Classic CANopen | CANopen FD Replacement |
|-----------------|----------------------|
| SDO (Service Data Object) | USDO (Universal SDO) |
| PDO (Process Data Object) | LPDO (Large PDO) |
| NMT, EMCY, SYNC, TIME | Retained (with extensions) |
| 8-byte data limit | Up to 64-byte payload |
| ≤ 1 Mbit/s | Nominal + up to 8 Mbit/s data phase |

CANopen FD is **backwards-compatible in concept**: the object dictionary, node-ID scheme, and NMT state machine are preserved, making migration tractable.

---

## 2. CAN FD Frame Format

### 2.1 Classical CAN vs. CAN FD Frame Comparison

```
CLASSICAL CAN FRAME (max 8 bytes payload)
===========================================

 SOF                                                          EOF
  |  Arbitration  Ctrl  Data (0-8 bytes)   CRC   ACK   IFS   |
  v  <----------> <---> <---------------> <----> <--> <----> v

Bit fields:
  +---+------------+---+--+-----------+-------+---+---+-----+
  | S | 11-bit ID  |RTR|r0| DLC (0-8) | DATA  |CRC|ACK| IFS |
  | O |  (or 29b)  |   |  |           |0-8 B  |   |   |     |
  | F |            |   |  |           |       |   |   |     |
  +---+------------+---+--+-----------+-------+---+---+-----+

  Bit rate: fixed, max 1 Mbit/s
  Payload : max 8 bytes


CAN FD FRAME (max 64 bytes payload, dual bit rate)
===================================================

 SOF                                                                   EOF
  |  Arbitration  Ctrl            Data Phase              CRC  ACK IFS |
  v  <----------> <--> <===============================> <===> <-> <-> v

Bit fields:
  +---+------------+---+---+---+---+----------+----------+------+---+--+
  | S | 11-bit ID  |RRS|IDE|FDF|BRS| ESI+DLC  |DATA      | CRC  |ACK|IF|
  | O |  (or 29b)  |   |   |   |   |(0-64 B)  |0-64 B    |      |   |S |
  | F |            |   |   |   |   |          |          |      |   |  |
  +---+------------+---+---+---+---+----------+----------+------+---+--+
                              ^   ^
                              |   |
                    FDF=1: CAN FD frame
                            BRS=1: switch to data bit-rate here
                                   <===data phase at high speed===>
                                   switch back to nominal after CRC

  New control bits:
    FDF  - FD Frame  (distinguishes CAN FD from classic CAN)
    BRS  - Bit Rate Switch
    ESI  - Error State Indicator
    RRS  - Remote Request Substitution (replaces RTR)

  DLC encoding (CAN FD):
    DLC  0- 8 -> 0- 8 bytes  (same as classic CAN)
    DLC  9    -> 12 bytes
    DLC 10    -> 16 bytes
    DLC 11    -> 20 bytes
    DLC 12    -> 24 bytes
    DLC 13    -> 32 bytes
    DLC 14    -> 48 bytes
    DLC 15    -> 64 bytes
```

### 2.2 DLC to Data Length Mapping

```
DLC Value  | 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
-----------+--------------------------------------------------
Data Bytes | 0  1  2  3  4  5  6  7  8 12 16 20 24 32 48 64
           |<---- same as classic CAN ---->|<-- CAN FD only -->
```

### 2.3 Unused Payload Padding

When the logical data length does not exactly match a valid CAN FD DLC, **padding bytes** (typically `0x00`) fill the remainder:

```
Example: 10 bytes of application data -> DLC = 9 -> allocate 12 bytes

  Byte:  0    1    2    3    4    5    6    7    8    9   10   11
       +----+----+----+----+----+----+----+----+----+----+----+----+
       |  Application Data (10 bytes)          |PAD |PAD |
       +----+----+----+----+----+----+----+----+----+----+----+----+
                                                  ^^^^  ^^^^
                                                 0x00  0x00  (padding)
```

---

## 3. Bit-Rate Switching — Nominal vs. Data Phase

### 3.1 The Two Phases

CAN FD splits each frame into two distinct timing phases:

```
NOMINAL PHASE (Arbitration)          DATA PHASE (Payload + CRC)
==============================        ============================
  - All nodes participate             - Transmitter controls
  - Slower bit rate required          - Higher bit rate allowed
  - Typical: 125 kbit/s –             - Typical: 1 – 8 Mbit/s
    1 Mbit/s                          - Enabled if BRS bit = 1

TIMELINE OF A CAN FD FRAME WITH BRS=1:

  Nominal speed               Data speed              Nominal speed
  <===================>  BRS  <==================>  CRC-Del  <=====>
                          |                             |
                   rate switches UP            rate switches DOWN
                   at sample point             at CRC Delimiter
```

### 3.2 Bit Timing Parameters

Each phase has its own set of timing registers:

```
NOMINAL PHASE TIMING:
  +---------+---------+---------+---------+
  |  SYNC   |  PROP   |  Phase1 |  Phase2 |
  |  SEG    |  SEG    |   SEG   |   SEG   |
  +---------+---------+---------+---------+
  ^                   ^         ^
  |                   |         |
  Sync point    Sample point   Next bit

DATA PHASE TIMING (separate register set):
  +---------+----+----+
  | SYNC SEG| P1 | P2 |
  +---------+----+----+
  (shorter segments because of higher speed)

Key parameters (typical values, 80 MHz clock):
                        Nominal       Data
  Prescaler (BRP)   :   4             1
  Sync Seg          :   1 TQ          1 TQ   (fixed)
  Prop + Phase1 Seg :   14 TQ         3 TQ
  Phase2 Seg        :   5 TQ          2 TQ
  SJW               :   4 TQ          2 TQ
  Resulting bit rate:   1 Mbit/s      8 Mbit/s
```

### 3.3 Transceiver Propagation Constraints

Higher data-phase rates impose stricter constraints on bus topology:

```
Maximum Bus Length vs. Data Phase Bit Rate:

  Bit Rate  | Max Bus Length | Max Node Count (typ.)
  ----------+----------------+----------------------
  1 Mbit/s  |   40 m         |  ~110
  2 Mbit/s  |   20 m         |   ~50
  4 Mbit/s  |    7 m         |   ~20
  5 Mbit/s  |    5 m         |   ~10
  8 Mbit/s  |    2 m         |    ~5

  Rule of thumb: nominal_length * data_rate ≤ 40 (m·Mbit/s)
```

---

## 4. CANopen FD Frame Encoding Differences

### 4.1 COB-ID Scheme — Extended to 29-bit

Classic CANopen uses **11-bit CAN IDs** (COB-IDs). CANopen FD extends this:

```
CLASSIC CANOPEN (11-bit COB-ID):
  Bits: 10       4  3          0
        +--------+--+----------+
        |Function|  | Node-ID  |
        | Code   |  | (0..127) |
        +--------+--+----------+
        <-- 7 bit-><-- 7 bit -->
        (function  (node, 0=broadcast)
          codes 0-15)

CANOPEN FD (29-bit Extended CAN ID):
  Bits: 28                18 17     14 13      7 6       0
        +------------------+--+-------+---------+---------+
        | Priority / Class |EF| Group |  Service| Node-ID |
        |   (11 bits)      |  | (4b)  |  (7b)   | (7b)    |
        +------------------+--+-------+---------+---------+

  EF = Extended Frame bit (1 for CAN FD extended addressing)
  Group: logical device group (0 = broadcast group)
  Service: replaces function code, wider range
  Node-ID: 1..127 (same as classic CANopen)
```

### 4.2 Protocol Data Units (PDU) Layout

CANopen FD reuses many classic PDU structures but extends them:

```
CLASSIC SDO INITIATE DOWNLOAD (8 bytes):
  Byte: 0     1     2     3     4     5     6     7
       +-----+-----+-----+-----+-----+-----+-----+-----+
       | CS  | Index LSB | Idx |Sub  | Data (4 bytes)  |
       |     |           | MSB |Idx  |                 |
       +-----+-----+-----+-----+-----+-----+-----+-----+

USDO REQUEST (CANopen FD, up to 64 bytes):
  Byte: 0     1     2     3     4     5  ...  N
       +-----+-----+-----+-----+-----+--------...------+
       | CS  |Priority   | Index     |Sub  | Data      |
       |     |+ Session  | (16-bit)  |Idx  | (variable)|
       +-----+-----+-----+-----+-----+--------...------+
         ^                                ^
         |                                |
      Command Specifier             Up to 56 bytes data
      includes new fields           in a single frame
      (session ID, more_follows)
```

### 4.3 Heartbeat and NMT — Unchanged in Structure

NMT, Heartbeat, SYNC, EMCY are retained from CiA 301. Their payloads fit within 8 bytes so no structural change is needed. However, their COB-IDs use the new 29-bit scheme in a pure CANopen FD network.

---

## 5. USDO — Replacing Classic SDO

### 5.1 Motivation

Classic SDO requires **multiple frames** for data > 4 bytes (segmented or block transfer). With CAN FD's 64-byte payload, a new mechanism — **USDO (Universal SDO)** — can transfer up to **56 bytes** in a single frame.

### 5.2 USDO Transfer Types

```
USDO TRANSFER OVERVIEW:
========================

  Case 1: Data ≤ 56 bytes  ->  SINGLE-FRAME TRANSFER
  Case 2: Data > 56 bytes  ->  MULTI-FRAME TRANSFER (segmented)

SINGLE-FRAME USDO DOWNLOAD (write, ≤ 56 bytes):
  Client                          Server
    |                               |
    |---[ USDO Request, 1 frame ]-->|   (data embedded, 1..56 bytes)
    |                               |
    |<--[ USDO Response, 1 frame ]--|
    |                               |
    Latency: 2 frames regardless of data size up to 56 bytes!

CLASSIC SDO DOWNLOAD (write, > 4 bytes, e.g. 32 bytes):
  Client                          Server
    |                               |
    |---[ Initiate Download Req ]-->|
    |<--[ Initiate Download Resp ]--|
    |---[ Download Segment 0 ]----->|
    |<--[ Segment Response 0 ]------|
    |---[ Download Segment 1 ]----->|
    |<--[ Segment Response 1 ]------|
    |  ... (n/7 round trips) ...    |
    Latency: 2 + 2*(ceil(n/7)) frames for n bytes
```

### 5.3 USDO Command Specifier (CS) Byte

```
USDO CS BYTE LAYOUT:
  Bit:  7    6    5    4    3    2    1    0
       +----+----+----+----+----+----+----+----+
       | CE | MF | SE |         SCS            |
       +----+----+----+----+----+----+----+----+

  CE  = Compact encoding (data length encoded in CS)
  MF  = More Follows    (1 = further segments pending)
  SE  = Session End     (final segment of multi-frame)
  SCS = Service Code Specifier:
          0x20 = Download Request      (write, client->server)
          0x60 = Download Response     (write ACK, server->client)
          0x40 = Upload Request        (read,  client->server)
          0x4F = Upload Response       (read,  server->client)
          0x80 = Abort Transfer

USDO SESSION ID: 8-bit counter per client, prevents mixing of
                 concurrent transfers from multiple clients.
```

### 5.4 USDO Abort Code — Extended

```
USDO Abort Codes (32-bit, same location as classic SDO):

  Classic SDO abort codes are RETAINED for compatibility.
  Additional CiA 1301 codes:

  0x0609 0030  Object access not allowed in current NMT state
  0x0609 0031  USDO session ID conflict
  0x0609 0032  Payload length exceeds object sub-index size
  0x0800 0024  USDO multi-frame timeout
```

---

## 6. LPDO — Replacing Classic PDO

### 6.1 Motivation

Classic PDO is limited to **8 bytes** of process data. With CANopen FD, a single **LPDO (Large PDO)** frame can carry up to **60 bytes** of mapped data, dramatically reducing the number of PDOs required for complex devices.

### 6.2 LPDO Frame Structure

```
CLASSIC PDO FRAME (8 bytes max):
  CAN Frame Payload:
  +----+----+----+----+----+----+----+----+
  | D0 | D1 | D2 | D3 | D4 | D5 | D6 | D7 |
  +----+----+----+----+----+----+----+----+
   Mapped variables packed MSB/LSB per object dictionary

LPDO FRAME (up to 60 bytes):
  CAN FD Payload (up to 64 bytes):
  Byte:  0       1       2  3  ...  63
        +-------+-------+--+--+----...----+
        | LPDO  | Flags |LN|  | Mapped    |
        | Header|       |  |  | Data      |
        +-------+-------+--+--+----...----+
         ^       ^       ^
         |       |       |
         |       |     Length nibble (if variable-length LPDO)
         |     SYNC counter / sequence info
         Header byte: encodes LPDO type (fixed/variable)

  Total mapped data: up to 60 bytes in ONE frame
```

### 6.3 LPDO Types

```
LPDO TYPES:
===========

  Type 1: STATIC LPDO
    - Fixed mapping defined at configuration time
    - No length field needed (length implicit from mapping)
    - Fastest processing, lowest overhead

  Type 2: DYNAMIC LPDO
    - Length field included in header
    - Mapping can change at runtime
    - Suitable for diagnostic/event data of variable size

LPDO vs. classic PDO capacity comparison:

  Classic PDO (8 bytes):    LPDO (60 bytes data):
  +---------+               +---------------------+
  |8 bytes  |               |                     |
  |process  |               |  60 bytes           |
  |data     |               |  process data       |
  +---------+               |                     |
                            |                     |
   1 PDO = 8 vars*          +---------------------+
                              1 LPDO = up to 60 vars*
  *assuming 1-byte variables
```

### 6.4 LPDO Communication Parameters (Object Dictionary)

```
CANOPEN FD OBJECT DICTIONARY EXTENSIONS FOR LPDO:

  Index 0x1800..0x19FF  LPDO Transmit Communication Parameters
    Sub 0x01: COB-ID (29-bit extended)
    Sub 0x02: Transmission type (same as PDO)
    Sub 0x03: Inhibit time
    Sub 0x04: Reserved
    Sub 0x05: Event timer
    Sub 0x06: SYNC start value
    Sub 0x07: Frame type (0=classic PDO compat, 1=LPDO)   <-- NEW
    Sub 0x08: Maximum data length                          <-- NEW

  Index 0x1A00..0x1BFF  LPDO Transmit Mapping Parameters
    Sub 0x00: Number of mapped objects (0..N, N up to ~60)
    Sub 0x01..N: Mapping entries (same 32-bit format as PDO)
      Bits 31..16: Object index
      Bits 15..8 : Sub-index
      Bits  7..0 : Length in bits
```

---

## 7. Migration Strategy from Classic CANopen

### 7.1 Coexistence Architecture

Classic CAN and CAN FD nodes **cannot coexist on the same physical bus segment** without a gateway. The standard migration paths are:

```
STRATEGY A: ISLAND UPGRADE (Recommended for large existing networks)
====================================================================

  Existing Classical CAN Segment        New CAN FD Segment
  ================================      =====================
                                              [FD Node 1]
  [CAN Node A]---+                           [FD Node 2]
  [CAN Node B]---+--[Bus]--[GATEWAY]--[FD Bus]--[FD Node 3]
  [CAN Node C]---+                           [FD Node 4]
  [CAN Node D]---+

  Gateway performs:
    SDO  <-> USDO  translation
    PDO  <-> LPDO  translation
    NMT state forwarding
    COB-ID / 29-bit ID remapping

STRATEGY B: FULL REPLACEMENT (Greenfield or major revision)
============================================================

  All nodes replaced with CAN FD capable hardware
  No gateway required
  Pure CiA 1301 network from day one

STRATEGY C: BACKWARD-COMPATIBLE FD NODES (Hybrid, CiA 1301 Annex)
===================================================================

  CAN FD nodes configured to operate with FDF=1, BRS=0:
    - CAN FD frame format used
    - Data phase bit rate NOT switched (same as nominal)
    - Classic CAN nodes CANNOT receive FD frames -> not truly mixed

  [!] True mixed classic/FD on one segment is NOT possible.
      CAN FD frames set the FDF bit which classic nodes interpret
      as a format error -> they generate error frames.
```

### 7.2 Object Dictionary Migration

```
OBJECT DICTIONARY CHANGES: CiA 301 -> CiA 1301

  RETAINED (unchanged):
    0x1000  Device Type
    0x1001  Error Register
    0x1003  Pre-defined Error Field
    0x1017  Producer Heartbeat Time
    0x1018  Identity Object
    0x6000+ Application Objects (user-defined)

  EXTENDED:
    0x1200..0x127F  SDO Server Parameters
                    -> Sub 0x05 added: USDO Session ID range

    0x1400..0x15FF  PDO Receive Comm. Parameters
                    -> Sub 0x07 added: frame type (PDO/LPDO)

  NEW (CiA 1301 only):
    0x1800..0x19FF  LPDO Transmit Comm. Parameters
    0x1A00..0x1BFF  LPDO Transmit Mapping
    0x1C00..0x1DFF  LPDO Receive Comm. Parameters
    0x1E00..0x1FFF  LPDO Receive Mapping

MIGRATION CHECKLIST:
  [ ] Update COB-IDs to 29-bit if using extended addressing
  [ ] Replace SDO server/client code with USDO handlers
  [ ] Replace PDO mapping with LPDO mapping (enlarge if needed)
  [ ] Update CAN controller init for FD bit timing
  [ ] Update transceiver to CAN FD capable type
  [ ] Verify bus topology meets FD propagation constraints
  [ ] Update EDS/DCF files to include new sub-indices
  [ ] Regression-test NMT state machine (unchanged logic)
```

---

## 8. Hardware and Driver Requirements

### 8.1 CAN FD Controller Requirements

```
CAN FD CONTROLLER FEATURE REQUIREMENTS:
=========================================

  Feature                        | Classic CAN | CAN FD Required
  -------------------------------+-------------+----------------
  ISO 11898-1:2015 compliance    | Optional    | Mandatory
  FD Frame support (FDF bit)     | No          | Yes
  Dual bit-rate timing registers | No          | Yes (nom+data)
  64-byte FIFO/mailbox support   | No          | Yes
  TDC (Transmitter Delay Comp.)  | No          | Recommended >= 2Mbit/s
  ISO CRC (21-bit for >16 bytes) | No          | Yes (automatic)
  Non-ISO CRC (legacy FD)        | No          | Optional (compat)

  Popular CAN FD controllers:
    - Bosch M_CAN IP (used in STM32H7, i.MX8, many SoCs)
    - Microchip MCP2518FD (SPI external controller)
    - NXP TJA1145 / TJA1153 (SBC with CAN FD)
    - Infineon TLE9255W
    - Kvaser Leaf Pro (USB, PC interface)
```

### 8.2 Transceiver Requirements

```
TRANSCEIVER SELECTION FOR CAN FD:
===================================

  Parameter            Classic CAN    CAN FD (2Mbit/s)  CAN FD (5Mbit/s)
  ---------------------+--------------+------------------+-----------------
  Loop delay symmetry  | < 250 ns     | < 120 ns         | < 50 ns
  Rise/fall time       | < 50 ns      | < 20 ns          | < 10 ns
  Part examples        | TJA1040      | TJA1042T/3       | TJA1044G
                       | PCA82C251    | MCP2561FD        | TCAN1042-Q1

  BUS TERMINATION for CAN FD:
  ============================
  Classic: 120 Ohm at each end (split: 60+60 Ohm with 4.7nF to GND)

  CAN FD (>2 Mbit/s) REQUIRES split termination:
    +--------+       60 Ohm       +-----+
    | Node A |---+---/\/\/---+----| End |
    |        |   |           |    | Term|
    +--------+   |   CAN_H   |    +-----+
                 |           |
    +--------+   | 4.7nF/GND |   +-----+
    | Node B |---+---/\/\/---+---| End |
    |        |       60 Ohm      | Term|
    +--------+       CAN_L       +-----+
                   (bus wire)
```

### 8.3 Linux SocketCAN for CAN FD

```
LINUX SOCKETCAN CAN FD SETUP:

  # Load CAN FD interface
  ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on
  ip link set up can0

  # Check interface capabilities
  ip -details link show can0

  # Send CAN FD frame (64 bytes) with cansend
  cansend can0 123##1DEADBEEF...  (## separates flags from data)
                  ^
                  FD flags byte: bit0=BRS, bit1=ESI
```

---

## 9. C/C++ Programming Examples

### 9.1 CAN FD Frame Structures

```c
/* canopen_fd.h - CANopen FD type definitions */

#ifndef CANOPEN_FD_H
#define CANOPEN_FD_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* CAN FD maximum payload */
#define CANFD_MAX_DLC       15
#define CANFD_MAX_DLEN      64

/* CAN FD frame flags */
#define CANFD_FLAG_BRS      0x01    /* Bit Rate Switch */
#define CANFD_FLAG_ESI      0x02    /* Error State Indicator */
#define CANFD_FLAG_FDF      0x04    /* FD Frame (set automatically) */

/* 29-bit extended CAN FD ID mask */
#define CANFD_EXT_ID_MASK   0x1FFFFFFF

/* CANopen FD COB-ID fields (29-bit) */
#define COFD_NODEID_MASK    0x0000007F   /* bits  6..0  */
#define COFD_SERVICE_MASK   0x00003F80   /* bits 13..7  */
#define COFD_GROUP_MASK     0x0003C000   /* bits 17..14 */
#define COFD_EF_BIT         0x00040000   /* bit  18     */
#define COFD_PRIORITY_MASK  0x1FF80000   /* bits 28..19 */

#define COFD_SERVICE_SHIFT  7
#define COFD_GROUP_SHIFT    14
#define COFD_PRIORITY_SHIFT 19

/* DLC to data length lookup table */
static const uint8_t canfd_dlc2len[16] = {
    0, 1, 2, 3, 4, 5, 6, 7,    /* DLC 0..7:  direct */
    8,                           /* DLC 8:     8 bytes */
   12,                           /* DLC 9:    12 bytes */
   16,                           /* DLC 10:   16 bytes */
   20,                           /* DLC 11:   20 bytes */
   24,                           /* DLC 12:   24 bytes */
   32,                           /* DLC 13:   32 bytes */
   48,                           /* DLC 14:   48 bytes */
   64                            /* DLC 15:   64 bytes */
};

/* Reverse: find smallest DLC that fits n bytes */
static inline uint8_t canfd_len2dlc(uint8_t len)
{
    if (len <= 8)  return len;
    if (len <= 12) return 9;
    if (len <= 16) return 10;
    if (len <= 20) return 11;
    if (len <= 24) return 12;
    if (len <= 32) return 13;
    if (len <= 48) return 14;
    return 15;
}

/* Generic CAN FD frame */
typedef struct {
    uint32_t id;            /* CAN ID (11-bit or 29-bit) */
    uint8_t  dlc;           /* Data Length Code (0..15)  */
    uint8_t  flags;         /* CANFD_FLAG_xxx            */
    uint8_t  data[CANFD_MAX_DLEN];
} canfd_frame_t;

/* CANopen FD COB-ID helper */
static inline uint32_t cofd_make_cobid(uint8_t priority, uint8_t group,
                                        uint8_t service, uint8_t nodeid)
{
    return ((uint32_t)priority << COFD_PRIORITY_SHIFT) |
           ((uint32_t)group    << COFD_GROUP_SHIFT)    |
           (1U                 << 18)                  | /* EF bit */
           ((uint32_t)service  << COFD_SERVICE_SHIFT)  |
           nodeid;
}

#endif /* CANOPEN_FD_H */
```

### 9.2 CAN FD Bit Timing Configuration

```c
/* canfd_bittiming.c - Bit timing setup for a Bosch M_CAN controller */

#include "canopen_fd.h"

/* M_CAN register offsets (example, vendor-specific base address) */
#define MCAN_BASE           0x40006400UL

#define MCAN_CCCR           (*(volatile uint32_t*)(MCAN_BASE + 0x18))
#define MCAN_NBTP           (*(volatile uint32_t*)(MCAN_BASE + 0x1C)) /* Nominal */
#define MCAN_DBTP           (*(volatile uint32_t*)(MCAN_BASE + 0x0C)) /* Data    */
#define MCAN_TDCR           (*(volatile uint32_t*)(MCAN_BASE + 0x48)) /* TDC     */

/* CCCR bits */
#define MCAN_CCCR_INIT      (1U << 0)
#define MCAN_CCCR_CCE       (1U << 1)   /* Configuration Change Enable */
#define MCAN_CCCR_FDOE      (1U << 8)   /* FD Operation Enable        */
#define MCAN_CCCR_BRSE      (1U << 9)   /* Bit Rate Switch Enable     */

typedef struct {
    uint32_t brp;       /* Baud Rate Prescaler    */
    uint32_t sjw;       /* Sync Jump Width        */
    uint32_t tseg1;     /* Time Segment 1 (prop+phase1) */
    uint32_t tseg2;     /* Time Segment 2 (phase2)      */
} can_bittiming_t;

/*
 * canfd_configure_bittiming()
 *
 * Example: fclk = 80 MHz
 *   Nominal: 1 Mbit/s,  BRP=4,  TSEG1=14, TSEG2=5, SJW=4
 *     -> Tq = 4/80MHz = 50 ns
 *     -> Bit time = (1+14+5)*50ns = 1000 ns = 1 Mbit/s  ✓
 *
 *   Data:    5 Mbit/s,  BRP=1,  TSEG1=11, TSEG2=4, SJW=2
 *     -> Tq = 1/80MHz = 12.5 ns
 *     -> Bit time = (1+11+4)*12.5ns = 200 ns = 5 Mbit/s  ✓
 */
int canfd_configure_bittiming(const can_bittiming_t *nom,
                               const can_bittiming_t *data_phase,
                               bool enable_brs)
{
    /* Step 1: Set INIT + CCE to enter configuration mode */
    MCAN_CCCR |= (MCAN_CCCR_INIT | MCAN_CCCR_CCE);

    /* Step 2: Enable CAN FD and optionally BRS */
    MCAN_CCCR |= MCAN_CCCR_FDOE;
    if (enable_brs)
        MCAN_CCCR |= MCAN_CCCR_BRSE;

    /* Step 3: Nominal bit timing (NBTP register)
     *   Bits 25..16 = NTSEG1, bits 31..25 = NBRP, etc. (simplified) */
    MCAN_NBTP = ((nom->brp   - 1) << 16) |
                ((nom->tseg1 - 1) << 8)  |
                ((nom->tseg2 - 1) << 0)  |
                ((nom->sjw   - 1) << 25);

    /* Step 4: Data phase bit timing (DBTP register) */
    MCAN_DBTP = ((data_phase->brp   - 1) << 16) |
                ((data_phase->tseg1 - 1) << 8)  |
                ((data_phase->tseg2 - 1) << 0)  |
                ((data_phase->sjw   - 1) << 25);

    /* Step 5: Transmitter Delay Compensation (needed at >= 2 Mbit/s)
     *   TDCO = propagation delay in Tq units (measure or use ~10) */
    MCAN_TDCR = (10U << 8) |    /* TDCO offset  */
                (1U  << 0);     /* TDCF filter  */

    /* Step 6: Clear INIT to start operation */
    MCAN_CCCR &= ~MCAN_CCCR_INIT;

    return 0;
}
```

### 9.3 USDO Client Implementation

```c
/* usdo_client.c - Universal SDO client for CANopen FD */

#include "canopen_fd.h"
#include <stdio.h>

/* USDO Command Specifier values */
#define USDO_CS_DOWNLOAD_REQ    0x20    /* Write request  (client->server) */
#define USDO_CS_DOWNLOAD_RESP   0x60    /* Write response (server->client) */
#define USDO_CS_UPLOAD_REQ      0x40    /* Read  request  (client->server) */
#define USDO_CS_UPLOAD_RESP     0x4F    /* Read  response (server->client) */
#define USDO_CS_ABORT           0x80    /* Abort transfer                  */

/* USDO CS flags */
#define USDO_CS_MF              0x40    /* More Follows                    */
#define USDO_CS_SE              0x20    /* Session End (last segment)      */
#define USDO_CS_CE              0x80    /* Compact Encoding                */

/* USDO frame offsets */
#define USDO_OFF_CS             0       /* Command Specifier               */
#define USDO_OFF_SESSION        1       /* Session ID (8-bit counter)      */
#define USDO_OFF_INDEX_L        2       /* Object index, LSB               */
#define USDO_OFF_INDEX_H        3       /* Object index, MSB               */
#define USDO_OFF_SUBINDEX       4       /* Sub-index                       */
#define USDO_OFF_DATA           5       /* Data start                      */

#define USDO_MAX_SINGLE_DATA    56      /* Max data in one USDO frame      */
#define USDO_ABORT_CODE_OFFSET  5       /* Abort code starts at byte 5     */

/* Abstract CAN send/receive interface (platform-specific) */
typedef int (*canfd_send_fn)(uint32_t id, const uint8_t *data,
                              uint8_t dlc, uint8_t flags);
typedef int (*canfd_recv_fn)(uint32_t *id, uint8_t *data,
                              uint8_t *dlc, uint32_t timeout_ms);

typedef struct {
    canfd_send_fn   send;
    canfd_recv_fn   recv;
    uint8_t         own_node_id;
    uint8_t         session_cnt;    /* Rolling session counter */
} usdo_client_t;

/*
 * usdo_download()
 *
 * Write 'len' bytes from 'src' into object [index:subindex] on 'server_node'.
 * Supports single-frame (len <= 56) and multi-frame (len > 56) transfers.
 *
 * Returns 0 on success, negative on error.
 */
int usdo_download(usdo_client_t *client,
                  uint8_t  server_node,
                  uint16_t index,
                  uint8_t  subindex,
                  const void *src,
                  uint32_t len,
                  uint32_t timeout_ms)
{
    uint8_t  frame[CANFD_MAX_DLEN];
    uint8_t  resp[CANFD_MAX_DLEN];
    uint32_t resp_id;
    uint8_t  resp_dlc;
    uint8_t  session = client->session_cnt++;

    /* COB-ID for USDO request to server (simplified 11-bit here) */
    uint32_t req_id  = 0x600 + server_node;  /* classic-compat COB-ID */
    uint32_t resp_cobid = 0x580 + server_node;

    const uint8_t *ptr = (const uint8_t *)src;
    uint32_t remaining = len;
    bool first_frame = true;

    while (remaining > 0) {
        uint32_t chunk = (remaining > USDO_MAX_SINGLE_DATA)
                         ? USDO_MAX_SINGLE_DATA : remaining;
        bool last_frame = (chunk == remaining);

        /* Build USDO download request frame */
        memset(frame, 0, sizeof(frame));

        frame[USDO_OFF_CS] = USDO_CS_DOWNLOAD_REQ
                           | (last_frame  ? USDO_CS_SE : 0)
                           | (first_frame ? 0           : USDO_CS_MF);
        frame[USDO_OFF_SESSION]  = session;
        frame[USDO_OFF_INDEX_L]  = (uint8_t)(index & 0xFF);
        frame[USDO_OFF_INDEX_H]  = (uint8_t)(index >> 8);
        frame[USDO_OFF_SUBINDEX] = subindex;

        memcpy(&frame[USDO_OFF_DATA], ptr, chunk);

        /* Choose DLC to fit header (5 bytes) + data */
        uint8_t dlc = canfd_len2dlc(5 + (uint8_t)chunk);
        /* Zero-pad to DLC boundary */
        uint8_t actual_len = canfd_dlc2len[dlc];
        memset(&frame[5 + chunk], 0x00, actual_len - 5 - chunk);

        /* Transmit with BRS enabled */
        if (client->send(req_id, frame, dlc, CANFD_FLAG_BRS) < 0)
            return -1;

        /* Wait for response */
        if (client->recv(&resp_id, resp, &resp_dlc, timeout_ms) < 0)
            return -2;  /* Timeout */

        if (resp_id != resp_cobid)
            return -3;  /* Wrong source */

        /* Check for abort */
        if ((resp[USDO_OFF_CS] & 0x9F) == USDO_CS_ABORT) {
            uint32_t abort_code;
            memcpy(&abort_code, &resp[USDO_ABORT_CODE_OFFSET], 4);
            fprintf(stderr, "USDO abort: 0x%08X\n", abort_code);
            return -4;
        }

        if ((resp[USDO_OFF_CS] & 0x9F) != USDO_CS_DOWNLOAD_RESP)
            return -5;  /* Unexpected response */

        ptr       += chunk;
        remaining -= chunk;
        first_frame = false;
    }

    return 0;  /* Success */
}

/*
 * usdo_upload()
 *
 * Read object [index:subindex] from 'server_node' into 'dst' buffer.
 * 'buf_len' must be large enough; *actual_len receives bytes read.
 *
 * Returns 0 on success, negative on error.
 */
int usdo_upload(usdo_client_t *client,
                uint8_t  server_node,
                uint16_t index,
                uint8_t  subindex,
                void    *dst,
                uint32_t buf_len,
                uint32_t *actual_len,
                uint32_t timeout_ms)
{
    uint8_t  frame[CANFD_MAX_DLEN];
    uint8_t  resp[CANFD_MAX_DLEN];
    uint32_t resp_id;
    uint8_t  resp_dlc;
    uint8_t  session = client->session_cnt++;
    uint8_t *out_ptr = (uint8_t *)dst;
    uint32_t total   = 0;

    uint32_t req_id      = 0x600 + server_node;
    uint32_t resp_cobid  = 0x580 + server_node;

    /* Build upload request */
    memset(frame, 0, sizeof(frame));
    frame[USDO_OFF_CS]       = USDO_CS_UPLOAD_REQ;
    frame[USDO_OFF_SESSION]  = session;
    frame[USDO_OFF_INDEX_L]  = (uint8_t)(index & 0xFF);
    frame[USDO_OFF_INDEX_H]  = (uint8_t)(index >> 8);
    frame[USDO_OFF_SUBINDEX] = subindex;

    if (client->send(req_id, frame, 5 /* DLC 5 */, CANFD_FLAG_BRS) < 0)
        return -1;

    /* Receive response frames (may be segmented) */
    do {
        if (client->recv(&resp_id, resp, &resp_dlc, timeout_ms) < 0)
            return -2;

        if (resp_id != resp_cobid)
            return -3;

        if ((resp[USDO_OFF_CS] & 0x9F) == USDO_CS_ABORT) {
            uint32_t ac;
            memcpy(&ac, &resp[USDO_ABORT_CODE_OFFSET], 4);
            fprintf(stderr, "USDO upload abort: 0x%08X\n", ac);
            return -4;
        }

        /* Extract data bytes from this frame */
        uint8_t frame_data_len = canfd_dlc2len[resp_dlc] - 5;
        if (total + frame_data_len > buf_len)
            return -6;  /* Buffer overflow */

        memcpy(out_ptr + total, &resp[USDO_OFF_DATA], frame_data_len);
        total += frame_data_len;

    } while (resp[USDO_OFF_CS] & USDO_CS_MF);  /* More follows? */

    if (actual_len) *actual_len = total;
    return 0;
}
```

### 9.4 LPDO Mapping Configuration

```c
/* lpdo_config.c - Configuring LPDO mappings via USDO */

#include "canopen_fd.h"
#include <string.h>

/*
 * LPDO Mapping Entry (32-bit, same encoding as classic PDO):
 *   Bits 31..16  Object Index
 *   Bits 15.. 8  Sub-Index
 *   Bits  7.. 0  Length in bits
 */
typedef struct {
    uint16_t index;
    uint8_t  subindex;
    uint8_t  length_bits;
} lpdo_map_entry_t;

/* Encode a mapping entry to 32-bit OD value */
static inline uint32_t lpdo_encode_mapping(const lpdo_map_entry_t *e)
{
    return ((uint32_t)e->index      << 16) |
           ((uint32_t)e->subindex   <<  8) |
           (e->length_bits);
}

/*
 * lpdo_configure_tx()
 *
 * Configure a transmit LPDO on a remote node using USDO writes.
 *
 * Parameters:
 *   client     - USDO client context
 *   node_id    - Target node ID
 *   lpdo_num   - LPDO number (0..N, maps to OD index 0x1800+n)
 *   cobid      - COB-ID for the LPDO
 *   trans_type - Transmission type (0xFF = event-driven)
 *   entries    - Array of mapping entries
 *   count      - Number of mapping entries
 */
int lpdo_configure_tx(usdo_client_t    *client,
                       uint8_t           node_id,
                       uint8_t           lpdo_num,
                       uint32_t          cobid,
                       uint8_t           trans_type,
                       const lpdo_map_entry_t *entries,
                       uint8_t           count)
{
    int ret;
    uint16_t comm_idx = 0x1800 + lpdo_num;  /* Communication params */
    uint16_t map_idx  = 0x1A00 + lpdo_num;  /* Mapping params       */
    uint32_t val32;
    uint8_t  val8;

    /* Step 1: Disable LPDO (set bit 31 of COB-ID) */
    val32 = cobid | 0x80000000UL;
    ret = usdo_download(client, node_id, comm_idx, 0x01,
                        &val32, 4, 1000);
    if (ret < 0) return ret;

    /* Step 2: Set number of mapped objects to 0 (disable mapping) */
    val8 = 0;
    ret = usdo_download(client, node_id, map_idx, 0x00, &val8, 1, 1000);
    if (ret < 0) return ret;

    /* Step 3: Set frame type to LPDO (sub-index 0x07, value 1) */
    val8 = 1;  /* 1 = LPDO frame type */
    ret = usdo_download(client, node_id, comm_idx, 0x07, &val8, 1, 1000);
    if (ret < 0) return ret;

    /* Step 4: Write mapping entries */
    for (uint8_t i = 0; i < count; i++) {
        val32 = lpdo_encode_mapping(&entries[i]);
        ret = usdo_download(client, node_id, map_idx, i + 1,
                            &val32, 4, 1000);
        if (ret < 0) return ret;
    }

    /* Step 5: Set number of mapped objects */
    val8 = count;
    ret = usdo_download(client, node_id, map_idx, 0x00, &val8, 1, 1000);
    if (ret < 0) return ret;

    /* Step 6: Set transmission type */
    ret = usdo_download(client, node_id, comm_idx, 0x02,
                        &trans_type, 1, 1000);
    if (ret < 0) return ret;

    /* Step 7: Enable LPDO (clear bit 31 of COB-ID) */
    val32 = cobid & ~0x80000000UL;
    ret = usdo_download(client, node_id, comm_idx, 0x01,
                        &val32, 4, 1000);
    return ret;
}

/* ---------------------------------------------------------------
 * EXAMPLE USAGE:
 *
 * Configure LPDO0 on node 3 to transmit:
 *   0x6041:00  Status Word          (16 bit)
 *   0x6064:00  Actual Position      (32 bit)
 *   0x606C:00  Actual Velocity      (32 bit)
 *   0x6077:00  Actual Torque        (16 bit)
 *   0x2100:01  Custom Sensor Data   (64 bit)
 *
 *   Total: 20 bytes -> fits easily in one LPDO frame
 * ---------------------------------------------------------------*/
void example_lpdo_setup(usdo_client_t *client)
{
    static const lpdo_map_entry_t drive_lpdo[] = {
        { 0x6041, 0x00, 16 },   /* Status Word         */
        { 0x6064, 0x00, 32 },   /* Actual Position     */
        { 0x606C, 0x00, 32 },   /* Actual Velocity     */
        { 0x6077, 0x00, 16 },   /* Actual Torque       */
        { 0x2100, 0x01, 64 },   /* Custom Sensor Data  */
    };

    lpdo_configure_tx(client,
                      3,                   /* node_id    */
                      0,                   /* lpdo_num   */
                      0x00000283UL,        /* COB-ID     */
                      0xFF,                /* event-driven */
                      drive_lpdo,
                      5);
}
```

### 9.5 LPDO Receive Handler (Server Side)

```c
/* lpdo_server.c - Receiving and unpacking LPDOs */

#include "canopen_fd.h"
#include <string.h>

/* Simplified LPDO receive context */
typedef struct {
    uint32_t  cobid;
    uint8_t   num_mappings;
    struct {
        uint16_t index;
        uint8_t  subindex;
        uint8_t  offset_bits;   /* Bit offset into LPDO payload */
        uint8_t  length_bits;
    } map[16];
    /* Object dictionary accessor (simplified) */
    void (*od_write)(uint16_t index, uint8_t sub, const uint8_t *data,
                     uint8_t len_bytes);
} lpdo_rx_t;

/*
 * lpdo_process_frame()
 *
 * Called when a CAN FD frame is received that matches a configured LPDO COB-ID.
 * Unpacks mapped variables from the frame payload into the object dictionary.
 */
void lpdo_process_frame(lpdo_rx_t *lpdo, const canfd_frame_t *frame)
{
    const uint8_t *payload = frame->data;
    uint8_t total_bytes = canfd_dlc2len[frame->dlc];

    for (uint8_t i = 0; i < lpdo->num_mappings; i++) {
        uint8_t byte_off  = lpdo->map[i].offset_bits / 8;
        uint8_t len_bytes = lpdo->map[i].length_bits  / 8;

        /* Boundary check */
        if (byte_off + len_bytes > total_bytes)
            break;  /* Truncated frame */

        lpdo->od_write(lpdo->map[i].index,
                       lpdo->map[i].subindex,
                       &payload[byte_off],
                       len_bytes);
    }
}

/*
 * lpdo_build_offsets()
 *
 * Pre-compute bit offsets for all mappings (called once after configuration).
 * Mappings are packed without gaps (standard CANopen PDO/LPDO behaviour).
 */
void lpdo_build_offsets(lpdo_rx_t *lpdo)
{
    uint8_t offset = 0;
    for (uint8_t i = 0; i < lpdo->num_mappings; i++) {
        lpdo->map[i].offset_bits = offset;
        offset += lpdo->map[i].length_bits;
    }
}
```

### 9.6 CANopen FD Node Initialisation (Linux SocketCAN)

```c
/* canfd_node_init.c - Full node initialisation using Linux SocketCAN */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "canopen_fd.h"

typedef struct {
    int      sock;
    uint8_t  node_id;
    uint8_t  nmt_state;     /* 0=init, 4=stopped, 5=operational, 127=pre-op */
} canfd_node_ctx_t;

/*
 * canfd_node_open()
 *
 * Opens a SocketCAN interface for CAN FD operation.
 */
int canfd_node_open(canfd_node_ctx_t *ctx, const char *ifname, uint8_t node_id)
{
    struct sockaddr_can addr;
    struct ifreq ifr;

    ctx->node_id  = node_id;
    ctx->nmt_state = 0;

    /* Open CAN raw socket */
    ctx->sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (ctx->sock < 0) {
        perror("socket");
        return -1;
    }

    /* Enable CAN FD frames */
    int canfd_on = 1;
    if (setsockopt(ctx->sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                   &canfd_on, sizeof(canfd_on)) < 0) {
        perror("setsockopt CAN_FD_FRAMES");
        close(ctx->sock);
        return -2;
    }

    /* Bind to interface */
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ioctl(ctx->sock, SIOCGIFINDEX, &ifr);

    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(ctx->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(ctx->sock);
        return -3;
    }

    return 0;
}

/*
 * canfd_send_frame()
 *
 * Transmit a CAN FD frame via SocketCAN.
 */
int canfd_send_frame(canfd_node_ctx_t *ctx, uint32_t id,
                     const uint8_t *data, uint8_t dlc, uint8_t flags)
{
    struct canfd_frame frame;
    memset(&frame, 0, sizeof(frame));

    frame.can_id  = id;
    frame.len     = canfd_dlc2len[dlc];
    frame.flags   = flags;          /* CANFD_BRS, CANFD_ESI */
    memcpy(frame.data, data, frame.len);

    ssize_t n = write(ctx->sock, &frame, sizeof(frame));
    return (n == sizeof(frame)) ? 0 : -1;
}

/*
 * canfd_send_heartbeat()
 *
 * Transmit a CANopen heartbeat message.
 * COB-ID = 0x700 + node_id  (classic compat, 11-bit)
 * Payload = 1 byte: NMT state
 */
int canfd_send_heartbeat(canfd_node_ctx_t *ctx)
{
    uint8_t payload[1] = { ctx->nmt_state };
    return canfd_send_frame(ctx, 0x700 + ctx->node_id,
                            payload, 1, 0 /* no BRS for small frames */);
}

/* ---------------------------------------------------------------
 * NMT State Machine
 *
 *                    [Power On]
 *                        |
 *                        v
 *                  +----------+
 *                  |Initialis-|
 *                  |  ation   |
 *                  +----+-----+
 *                       |  (auto)
 *                       v
 *                  +----------+
 *              +-->| Pre-Oper.|<--+
 *              |   | (0x7F)   |   |
 *              |   +----+-----+   |
 *              |        | Start   | Stop
 *              | Reset  v         |
 *              | Node +-----------+
 *              |      |Operational|
 *              |      | (0x05)    |
 *              |      +-----------+
 *              |
 *              |   +----------+
 *              +---| Stopped  |
 *                  | (0x04)   |
 *                  +----------+
 * ---------------------------------------------------------------*/
void canfd_nmt_set_state(canfd_node_ctx_t *ctx, uint8_t new_state)
{
    ctx->nmt_state = new_state;
    canfd_send_heartbeat(ctx);
}
```

---

## 10. Summary

CANopen FD (CiA 1301) is the natural evolution of classic CANopen into the CAN FD era. The key takeaways are:

### Core Technical Changes

| Aspect | Classic CANopen (CiA 301) | CANopen FD (CiA 1301) |
|--------|--------------------------|----------------------|
| Max payload | 8 bytes | 64 bytes |
| Max bit rate | 1 Mbit/s | 8 Mbit/s (data phase) |
| CAN ID | 11-bit | 11-bit or 29-bit |
| SDO mechanism | SDO (segmented) | USDO (single-frame up to 56 B) |
| PDO mechanism | PDO (≤ 8 bytes) | LPDO (≤ 60 bytes) |
| Multi-frame SDO | Required for > 4 bytes | Only for > 56 bytes |
| Bit timing | Single set | Dual (nominal + data) |
| Padding bytes | None needed | Required for non-standard DLC |
| NMT state machine | CiA 301 | Retained unchanged |
| Object dictionary | CiA 301 | Extended (new sub-indices) |

### Key Programming Points

**CAN FD bit timing** requires two independent register sets (nominal phase for arbitration, data phase for payload). Transmitter Delay Compensation (TDC) is essential at data rates ≥ 2 Mbit/s.

**USDO** eliminates the multi-frame overhead of classic SDO for transfers up to 56 bytes — the most common case in field device configuration. Multi-frame USDO handles larger data with a session-ID mechanism that supports concurrent transfers from multiple clients.

**LPDO** allows a single process-data frame to carry up to 60 bytes of mapped variables, reducing frame count on the bus by up to 7.5× compared to classic PDO (8-byte limit). Configuration follows the same pattern as classic PDO but with additional sub-indices (0x07 = frame type, 0x08 = max data length).

**Migration** from classic CANopen is best staged: replace nodes segment by segment behind a gateway that bridges PDO↔LPDO and SDO↔USDO. The NMT layer, heartbeat protocol, object dictionary structure, and EDS format are retained, minimising re-engineering effort.

**Hardware** must be qualified end-to-end: CAN FD capable controller (e.g. Bosch M_CAN), FD-rated transceiver (e.g. TJA1044G for 5 Mbit/s), impedance-controlled PCB traces, and split termination at ≥ 2 Mbit/s data rate.

### Migration Decision Guide

```
Is your bus < 40 m long AND all nodes replaceable?
    YES -> Full replacement with pure CiA 1301 network
    NO  -> Island upgrade with gateway(s)

Do your devices need > 8 bytes process data per update cycle?
    YES -> LPDO mapping is the primary driver for migration
    NO  -> Classic CANopen PDO may still suffice

Do you do frequent large SDO transfers (firmware, param blocks)?
    YES -> USDO single-frame transfers provide major latency reduction
    NO  -> SDO-to-USDO migration is lower priority

Is your bus data rate currently < 1 Mbit/s?
    YES -> Consider upgrading nominal rate first, add BRS later
    NO  -> BRS (data phase rate switch) delivers immediate bandwidth gain
```

---

*References:*
- *CiA 1301 — CANopen FD Application Layer and Communication Profile*
- *ISO 11898-1:2015 — Road vehicles: CAN FD physical layer*
- *CiA 301 v4.2 — CANopen Application Layer and Communication Profile*
- *Bosch M_CAN User Manual (IP core documentation)*
- *Linux SocketCAN Documentation — kernel.org*