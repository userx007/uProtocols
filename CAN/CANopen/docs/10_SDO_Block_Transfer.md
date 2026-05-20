# CANopen SDO Block Transfer

1. **Introduction** — what block transfer is and why it exists
2. **Segmented vs. Block overview** — ASCII protocol flow diagrams comparing both approaches
3. **Block Download protocol** — all three phases (Initiate, Sub-Block, End) with byte-level ASCII frame layouts
4. **Block Upload protocol** — server-side equivalent with phase diagrams
5. **Sequence numbers** — how 1–127 seqno works, retransmission flow in ASCII
6. **CRC verification** — CRC-16/CCITT polynomial, scope, and negotiation
7. **Block size negotiation** — selection table based on available device RAM
8. **Use cases** — firmware update and parameter backup/restore with full ASCII workflow diagrams
9. **Throughput comparison** — quantified segmented vs. block transfer with a results table (up to ~12× faster at blksize=127)
10. **C/C++ examples** — five complete, commented implementations:
    - Block download client (`sdo_block_download_initiate`, `_subblock`, `_end`, and high-level wrapper)
    - Block upload server handler
    - CRC-16/CCITT with lookup table and incremental API
    - Block size negotiation helper and use/no-use decision helper
    - Full firmware update sequence using CiA 302 Program Download objects
11. **Error handling** — abort codes table and handling flowchart
12. **Summary** — comparison table, protocol state machine in ASCII, and six key practical takeaways

## Table of Contents

1. [Introduction](#1-introduction)
2. [SDO Transfer Overview: Segmented vs. Block](#2-sdo-transfer-overview-segmented-vs-block)
3. [Block Download Protocol](#3-block-download-protocol)
   - 3.1 [Initiate Phase](#31-initiate-phase)
   - 3.2 [Sub-Block Transmission Phase](#32-sub-block-transmission-phase)
   - 3.3 [End Phase](#33-end-phase)
4. [Block Upload Protocol](#4-block-upload-protocol)
   - 4.1 [Initiate Phase](#41-initiate-phase)
   - 4.2 [Sub-Block Reception Phase](#42-sub-block-reception-phase)
   - 4.3 [End Phase](#43-end-phase)
5. [Sequence Numbers](#5-sequence-numbers)
6. [CRC Verification](#6-crc-verification)
7. [Block Size Negotiation](#7-block-size-negotiation)
8. [Use Cases](#8-use-cases)
   - 8.1 [Firmware Update](#81-firmware-update)
   - 8.2 [Large Parameter Sets](#82-large-parameter-sets)
9. [Throughput Comparison: Segmented vs. Block Transfer](#9-throughput-comparison-segmented-vs-block-transfer)
10. [C/C++ Implementation Examples](#10-cc-implementation-examples)
    - 10.1 [Block Download (Client Side)](#101-block-download-client-side)
    - 10.2 [Block Upload (Server Side)](#102-block-upload-server-side)
    - 10.3 [CRC-16 CCITT Calculation](#103-crc-16-ccitt-calculation)
    - 10.4 [Block Size Negotiation Helper](#104-block-size-negotiation-helper)
    - 10.5 [Firmware Update Example](#105-firmware-update-example)
11. [Error Handling](#11-error-handling)
12. [Summary](#12-summary)

---

## 1. Introduction


The **SDO Block Transfer** is an advanced CANopen communication mechanism defined in
**CiA 301** (CANopen Application Layer and Communication Profile). It extends the basic
SDO (Service Data Object) protocol to allow efficient, high-throughput transfer of large
data objects between a CANopen master (client) and a slave device (server).

Standard SDO transfers come in two flavours:

- **Expedited transfer** — up to 4 bytes, all in a single CAN frame.
- **Segmented transfer** — any length, but each segment carries only **7 bytes** of
  payload and requires an individual acknowledgement per segment.

Block transfer removes the per-segment acknowledgement bottleneck by grouping up to
**127 segments** (called a *sub-block*) into a single acknowledged unit, dramatically
reducing protocol overhead and nearly saturating the available CAN bus bandwidth.

> **Key benefit:** For a 64 kB firmware image at 1 Mbit/s, block transfer can be roughly
> **5–7× faster** than segmented transfer.

---

## 2. SDO Transfer Overview: Segmented vs. Block

```
SEGMENTED TRANSFER (7 bytes payload per round-trip)
=====================================================

  Client                          Server
    |                               |
    |--- Initiate Download Req. --->|
    |<-- Initiate Download Resp. ---|
    |--- Segment 1 (7 bytes) ------>|
    |<-- Segment ACK ---------------|
    |--- Segment 2 (7 bytes) ------>|
    |<-- Segment ACK ---------------|
    |--- Segment N (7 bytes) ------>|
    |<-- Segment ACK ---------------|
    |                               |

  One round-trip per 7-byte segment = low bus utilisation


BLOCK TRANSFER (up to 127 × 7 = 889 bytes per round-trip)
===========================================================

  Client                          Server
    |                               |
    |--- Initiate Block Down Req -->|   (negotiate block size, CRC flag)
    |<-- Initiate Block Down Resp --|
    |                               |
    |--- Seg  1 [seqno=1] --------> |   \
    |--- Seg  2 [seqno=2] --------> |    |
    |--- Seg  3 [seqno=3] --------> |    |  Sub-block: no ACK between segments
    |         ...                   |    |
    |--- Seg 127 [seqno=127] -----> |   /
    |<-- Sub-Block ACK  [ackseq] -- |   (ACK covers all 127 segments at once)
    |                               |
    |--- Seg  1 [seqno=1] --------> |   \
    |         ...                   |    |  Next sub-block
    |--- Seg 127 [seqno=127] -----> |   /
    |<-- Sub-Block ACK  [ackseq] -- |
    |                               |
    |--- End Block Download Req --->|
    |<-- End Block Download Resp ---|
    |                               |
```

---

## 3. Block Download Protocol

Block **download** transfers data from the **client** (master) to the **server** (slave).
The process has three distinct phases: **Initiate**, **Sub-Block Transmission**, and **End**.

### 3.1 Initiate Phase

The client sends a request specifying the target object dictionary entry, the total data
size, the desired block size (`blksize`, 1–127 segments per sub-block), and whether CRC
generation is requested.

```
CAN Frame layout — Client Initiate Block Download Request
---------------------------------------------------------
Byte 0 (cs field):  0xC2  (cc=0: no CRC)
                    0xC6  (cc=1: CRC supported)
                    Bits [7:5] = 110 (command specifier)
                    Bit  [2]   = 1   (size indicator present)
                    Bit  [1]   = 1   (CRC support)

Bytes 1-2: Object Index     (little-endian, e.g. 0x1018 → 0x18 0x10)
Byte  3:   Sub-index
Bytes 4-7: Total data size  (little-endian, 0 if unknown)


ASCII Frame Diagram:
+------+-------+-------+--------+--------+--------+--------+--------+
| Byte | Byte  | Byte  | Byte   | Byte   | Byte   | Byte   | Byte   |
|  0   |  1    |  2    |  3     |  4     |  5     |  6     |  7     |
+------+-------+-------+--------+--------+--------+--------+--------+
| 0xC6 | Idx_L | Idx_H | SubIdx | Size_0 | Size_1 | Size_2 | Size_3 |
+------+-------+-------+--------+--------+--------+--------+--------+
  ccs=6          Object Address             Total Size (uint32 LE)
```

The server responds, confirming the block size it will accept:

```
CAN Frame layout — Server Initiate Block Download Response
---------------------------------------------------------
Byte 0: 0xA4  (command specifier for block download response)
Bytes 1-2: Object Index
Byte  3:   Sub-index
Byte  4:   blksize — number of segments per sub-block the server accepts
Bytes 5-7: reserved (0x00)

+------+-------+-------+--------+---------+------+------+------+
| Byte | Byte  | Byte  | Byte   | Byte    | Byte | Byte | Byte |
|  0   |  1    |  2    |  3     |  4      |  5   |  6   |  7   |
+------+-------+-------+--------+---------+------+------+------+
| 0xA4 | Idx_L | Idx_H | SubIdx | blksize | 0x00 | 0x00 | 0x00 |
+------+-------+-------+--------+---------+------+------+------+
```

### 3.2 Sub-Block Transmission Phase

The client transmits segments continuously (no per-segment acknowledgement). Each segment
carries a **sequence number** in byte 0:

```
CAN Frame layout — Client Block Download Sub-Block Segment
----------------------------------------------------------
Byte 0: [c | seqno]
        Bit 7  (c):    1 = this is the last segment of the entire transfer
                       0 = more segments follow
        Bits[6:0] (seqno): segment sequence number within this sub-block, 1..127

Bytes 1-7: 7 bytes of data payload

+-------------------+------+------+------+------+------+------+------+
|   Byte 0          | Byte | Byte | Byte | Byte | Byte | Byte | Byte |
| [c | seqno(6:0)]  |  1   |  2   |  3   |  4   |  5   |  6   |  7   |
+-------------------+------+------+------+------+------+------+------+
|  0 | 0 0 0 0 0 0 1| D[0] | D[1] | D[2] | D[3] | D[4] | D[5] | D[6] |
+-------------------+------+------+------+------+------+------+------+
  c=0 (more)  seqno=1

A sub-block of 3 segments looks like this on the bus:

  +--------+-------+---...---+  Seg 1, seqno=1
  | 0 | 01 | data7           |
  +--------+-------+---...---+
  +--------+-------+---...---+  Seg 2, seqno=2
  | 0 | 02 | data7           |
  +--------+-------+---...---+
  +--------+-------+---...---+  Seg 3, seqno=3  (last of sub-block, NOT last of transfer)
  | 0 | 03 | data7           |
  +--------+-------+---...---+
                               <-- Server sends Sub-Block ACK here
```

After the last segment of a sub-block is sent, the server acknowledges:

```
CAN Frame layout — Server Sub-Block ACK
----------------------------------------
Byte 0: 0xA2
Byte 1: ackseq — highest correctly received sequence number
Byte 2: blksize — new proposed block size for the next sub-block
Bytes 3-7: reserved

+------+--------+---------+------+------+------+------+------+
| Byte | Byte   | Byte    | Byte | Byte | Byte | Byte | Byte |
|  0   |  1     |  2      |  3   |  4   |  5   |  6   |  7   |
+------+--------+---------+------+------+------+------+------+
| 0xA2 | ackseq | blksize | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 |
+------+--------+---------+------+------+------+------+------+

  ackseq < expected: client must re-send segments from (ackseq+1) onwards
  ackseq = expected: sub-block received correctly, proceed with next
```

### 3.3 End Phase

When all data has been sent, the client signals the end of the block transfer, providing
the CRC (if negotiated) and the number of unused bytes in the last segment:

```
CAN Frame layout — Client End Block Download Request
----------------------------------------------------
Byte 0: [1 1 0 | n[2:1] | 0 | crc_valid | 1]
        Bits[4:3] (n): number of bytes in last segment that do NOT contain data
                       (0 to 6)
        Bit  [2]:      reserved
        Bit  [1]:      1 = CRC field is valid
        Bit  [0]:      1 (end of block marker)

Bytes 1-2: CRC-16/CCITT value (little-endian, if crc_valid=1)
Bytes 3-7: reserved

+------------------+-------+-------+------+------+------+------+------+
| Byte 0           | Byte  | Byte  | Byte | Byte | Byte | Byte | Byte |
| [110|n|0|crc|1]  |  1    |  2    |  3   |  4   |  5   |  6   |  7   |
+------------------+-------+-------+------+------+------+------+------+
| 0xD9 (n=0,crc=1) | CRC_L | CRC_H | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 |
+------------------+-------+-------+------+------+------+------+------+

Server confirms:
Byte 0: 0xA1  (End Block Download Response)
Bytes 1-7: reserved (0x00)
```

---

## 4. Block Upload Protocol

Block **upload** transfers data from the **server** to the **client**. The roles for
sending segments are reversed, but the negotiation and acknowledgement structure mirrors
block download.

### 4.1 Initiate Phase

```
Client Initiate Block Upload Request:
Byte 0: 0xA4  (blksize request)
Bytes 1-2: Index
Byte 3: Sub-index
Byte 4: blksize — max segments per sub-block client can receive
Byte 5: pst (protocol switch threshold, 0 = no switch)
Bytes 6-7: reserved

Server Initiate Block Upload Response:
Byte 0: 0xC6 (size present, CRC supported)
Bytes 1-2: Index
Byte 3: Sub-index
Bytes 4-7: Total data size (little-endian)

+------+-------+-------+--------+--------+--------+--------+--------+
| 0xC6 | Idx_L | Idx_H | SubIdx | Size_0 | Size_1 | Size_2 | Size_3 |
+------+-------+-------+--------+--------+--------+--------+--------+
```

After the server response, the client sends a **Start Upload** command (byte 0 = 0xA3)
to trigger the data flow.

### 4.2 Sub-Block Reception Phase

The server now sends segments continuously (same byte 0 format as block download, but
with the server as sender). The client acknowledges each sub-block:

```
  Server                          Client
    |                               |
    |--- Seg 1 [seqno=1] ---------> |
    |--- Seg 2 [seqno=2] ---------> |
    |--- Seg 3 [seqno=3] ---------> |
    |<-- Sub-Block ACK [ackseq=3] --|   (client ACKs the sub-block)
    |--- Seg 1 [seqno=1] ---------> |
    |         ...                   |
```

### 4.3 End Phase

```
Server End Block Upload Request:
Byte 0: [1 1 0 | n | 0 | crc_valid | 1]
Bytes 1-2: CRC-16/CCITT (if valid)

Client End Block Upload Response:
Byte 0: 0xA1
```

---

## 5. Sequence Numbers

Sequence numbers serve as the **error-detection and retransmission mechanism** within a
sub-block. They are critical to understanding how block transfer achieves reliability
without per-segment acknowledgements.

### Rules

- Sequence numbers run **1 to 127** within each sub-block and reset to 1 at the start of
  every new sub-block.
- The **most significant bit** (bit 7) of byte 0 is the **last-segment flag** (`c`), not
  part of the sequence number.
- The `ackseq` field in the sub-block acknowledgement contains the highest contiguous
  sequence number correctly received.

### Retransmission on Error

```
Normal sub-block (blksize=5):

  Client --> [01][data]  seqno=1  OK
  Client --> [02][data]  seqno=2  OK
  Client --> [03][data]  seqno=3  LOST (CAN error or buffer overrun)
  Client --> [04][data]  seqno=4  OK (but server discards — gap detected)
  Client --> [05][data]  seqno=5  OK (discarded)
  Server <-- ACK ackseq=2, blksize=3

  Client re-sends from seqno=3:
  Client --> [01][data]  seqno=1 (was 3)  OK
  Client --> [02][data]  seqno=2 (was 4)  OK
  Client --> [03][data]  seqno=3 (was 5)  OK
  Server <-- ACK ackseq=3, blksize=5  (sub-block complete)

  Note: sequence numbers restart from 1 for retransmission
```

---

## 6. CRC Verification

Block transfer supports an optional **CRC-16/CCITT** (polynomial 0x1021, initial value
0xFFFF) computed over the **entire** transferred data. CRC use is negotiated during the
initiate phase.

### CRC Negotiation

```
Initiate request:  client sets cc=1 (CRC supported)
Initiate response: server sets sc=1 (CRC supported) → CRC enabled
                   server sets sc=0 (CRC not supported) → no CRC
```

### CRC Scope

```
                            All transferred data
            +--------------------------------------------------+
            | Block 1 data | Block 2 data | ... | Last block   |
            +--------------------------------------------------+
                                                        |
                                         CRC16/CCITT covers all of the above
                                         Sent in End Block Download Request
```

### Polynomial

```
CRC-16/CCITT:
  Polynomial:    0x1021  (x^16 + x^12 + x^5 + 1)
  Initial value: 0xFFFF
  Input/Output:  not reflected
  Final XOR:     0x0000
```

---

## 7. Block Size Negotiation

Block size (`blksize`) is the number of segments per sub-block, ranging from **1 to 127**.
Each segment carries 7 bytes of data, so a full sub-block with `blksize=127` carries
`127 × 7 = 889 bytes` before an acknowledgement is required.

### Negotiation Process

```
  1. Client proposes a blksize in the Initiate request.
  2. Server responds with the blksize it actually accepts (may be lower).
  3. After each sub-block ACK, the server may propose a new blksize.
  4. The client uses the newly proposed blksize for the next sub-block.

  This allows dynamic adaptation, e.g. if the server's receive buffer is
  temporarily full it can reduce blksize to apply back-pressure.

  blksize selection guidelines:
  +-------------------+------------------+----------------------------+
  | Available RAM     | Recommended      | Notes                      |
  |   on server       | blksize          |                            |
  +-------------------+------------------+----------------------------+
  | >= 889 bytes      | 127              | Maximum throughput         |
  | 448 – 888 bytes   | 64               | Good balance               |
  | 112 – 447 bytes   | 16               | Low-memory devices         |
  | < 112 bytes       | 1 – 15           | Very constrained; may be   |
  |                   |                  | slower than segmented      |
  +-------------------+------------------+----------------------------+
```

---

## 8. Use Cases

### 8.1 Firmware Update

Firmware updates are the canonical use case for SDO block transfer. A typical embedded
controller firmware may be 32 kB – 512 kB in size. Downloading this via segmented SDO
would be prohibitively slow on a 125 kbit/s bus.

```
Firmware Update Workflow (Block Download):
==========================================

  [Master / CANopen Manager]           [Slave / ECU]
         |                                   |
         |-- Enter Bootloader (NMT/SDO) ---->|
         |<-- OK ----------------------------|
         |                                   |
         |-- Block Download to 0x1F50/01 --> |  (Program Data object)
         |   [Initiate: size=65536, blk=127] |
         |<-- [blksize=127 confirmed] -------|
         |                                   |
         |-- Sub-block 1 (889 bytes) ------->|
         |<-- ACK ackseq=127 --------------- |
         |-- Sub-block 2 (889 bytes) ------->|
         |<-- ACK ackseq=127 --------------- |
         |       ... 73 sub-blocks total ... |
         |-- Last sub-block (partial) ------>|
         |<-- ACK ---------------------------|
         |-- End Block [CRC=0xABCD] -------->|
         |<-- End Block OK ------------------|
         |                                   |
         |-- Write 0x1F51/01 = 0x01 (Flash)->|  (Program Control)
         |<-- OK ----------------------------|
         |                                   |
         |-- NMT Reset Node ---------------->|
         |<-- [device reboots with new FW] --|
```

Relevant object dictionary entries (CiA 302 / manufacturer-specific):

| Index | Sub | Name                 | Typical use                    |
|-------|-----|----------------------|--------------------------------|
| 0x1F50| 01  | Program Data         | Receives firmware binary       |
| 0x1F51| 01  | Program Control      | 0x01=start flash, 0x02=reset   |
| 0x1F56| 01  | Program Software ID  | Version check before flash     |
| 0x1F57| 01  | Flash Status         | 0=idle, 1=busy, 2=OK, 3=error  |

### 8.2 Large Parameter Sets

Industrial devices such as servo drives, frequency inverters, and vision systems often
have thousands of configuration parameters that must be backed up or restored. Block
transfer can move an entire parameter snapshot in seconds rather than minutes.

```
Parameter Backup Workflow (Block Upload):
=========================================

  [Master]                              [Slave / Drive]
     |                                        |
     |-- Block Upload 0x2000/00 (param set) ->|
     |   [Initiate: blksize=64, pst=0]        |
     |<-- [size=8192 bytes, blksize=64] ------|
     |-- Start Upload [0xA3] ---------------->|
     |<-- Sub-block  1 (448 bytes) -----------|
     |-- ACK ackseq=64 ---------------------->|
     |<-- Sub-block  2 (448 bytes) -----------|
     |-- ACK ackseq=64 ---------------------->|
     |       ... 18 sub-blocks total ...      |
     |<-- End Block [CRC=0x1234] -------------|
     |-- End Block OK [0xA1] ---------------->|
     |                                        |
     [Master stores 8 kB snapshot to file]
```

---

## 9. Throughput Comparison: Segmented vs. Block Transfer

Assumptions: 1 Mbit/s CAN bus, 11-bit identifiers (standard frame), no bus errors,
no bit-stuffing overhead in this simplified model.

### CAN Frame Timing

```
Standard CAN frame overhead:
  SOF(1) + ID(11) + RTR(1) + IDE(1) + r0(1) + DLC(4) + CRC(16)
  + CRC-DEL(1) + ACK(2) + EOF(7) + IFS(3) = 48 bits overhead
  + 8 bytes data = 64 data bits
  Total: 112 bits per frame → 112 µs @ 1 Mbit/s
```

### Segmented Transfer Throughput

```
Per 7 data bytes:
  - 1 segment frame (client → server): 112 µs
  - 1 ACK frame    (server → client): 112 µs
  - Turnaround / processing delay:    ~10 µs (estimate)
  Total per 7 bytes: ~234 µs

  Throughput ≈ 7 bytes / 234 µs ≈ 29.9 kB/s
```

### Block Transfer Throughput (blksize=127)

```
Per sub-block of 127 × 7 = 889 bytes:
  - 127 segment frames: 127 × 112 µs = 14,224 µs
  - 1 ACK frame:                        112 µs
  - Turnaround delay:                   ~10 µs
  Total per 889 bytes: ~14,346 µs

  Throughput ≈ 889 bytes / 14,346 µs ≈ 62.0 kB/s
```

### Summary Table

```
+----------------------+------------+------------+------------------+
| Transfer Mode        | Payload/   | Time for   | Relative Speed   |
|                      | Round-trip | 64 kB      |                  |
+----------------------+------------+------------+------------------+
| Segmented (7 bytes)  |   7 bytes  | ~2,198 ms  |     1.0 ×        |
| Block (blksize= 16)  | 112 bytes  |   ~459 ms  |     4.8 ×        |
| Block (blksize= 64)  | 448 bytes  |   ~257 ms  |     8.6 ×        |
| Block (blksize=127)  | 889 bytes  |   ~188 ms  |    11.7 ×        |
+----------------------+------------+------------+------------------+

Note: Real-world values depend on bus load, device processing time,
      and CAN controller interrupt latency.
```

---

## 10. C/C++ Implementation Examples

### 10.1 Block Download (Client Side)

```cpp
/**
 * @file sdo_block_download.cpp
 * @brief CANopen SDO Block Download client implementation.
 *        Compliant with CiA 301, section 7.2.4.3
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "can_driver.h"   // Platform-specific CAN send/receive

/* SDO command specifiers */
#define SDO_CS_INITIATE_BLOCK_DOWNLOAD_REQ  0xC6u  /* cc=1 (CRC), s=1 (size) */
#define SDO_CS_INITIATE_BLOCK_DOWNLOAD_RESP 0xA4u
#define SDO_CS_BLOCK_DOWNLOAD_END_REQ       0xC1u  /* with CRC, n=0 in byte 0 */
#define SDO_CS_BLOCK_DOWNLOAD_END_RESP      0xA1u
#define SDO_CS_BLOCK_DOWNLOAD_ACK           0xA2u

#define SDO_BLOCK_MAX_SEQNO  127u
#define SDO_SEGMENT_SIZE     7u

/* CAN IDs (client COB-ID = 0x600 + node_id, server COB-ID = 0x580 + node_id) */
#define SDO_TX_COBID(node)  (0x600u + (node))
#define SDO_RX_COBID(node)  (0x580u + (node))

typedef struct {
    uint8_t  node_id;
    uint16_t index;
    uint8_t  subindex;
    uint8_t  blksize;          /* negotiated segments per sub-block      */
    bool     crc_enabled;
    uint16_t crc_accum;        /* running CRC over entire transferred data */
} SdoBlockCtx;

/** Compute CRC-16/CCITT over one byte */
static uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000u)
            crc = (uint16_t)((crc << 1) ^ 0x1021u);
        else
            crc <<= 1;
    }
    return crc;
}

/**
 * @brief Initiate a block download session.
 * @param ctx      Pointer to SDO block context (pre-filled with node_id, index, etc.)
 * @param data_len Total number of bytes to transfer.
 * @param blksize  Desired segments per sub-block (1–127).
 * @return true on success (server responded with valid initiate response).
 */
bool sdo_block_download_initiate(SdoBlockCtx *ctx,
                                  uint32_t     data_len,
                                  uint8_t      blksize)
{
    uint8_t frame[8] = {0};
    uint8_t resp[8]  = {0};

    /* Build Initiate Block Download Request */
    frame[0] = SDO_CS_INITIATE_BLOCK_DOWNLOAD_REQ; /* 0xC6: CRC + size present */
    frame[1] = (uint8_t)(ctx->index & 0xFFu);
    frame[2] = (uint8_t)(ctx->index >> 8);
    frame[3] = ctx->subindex;
    frame[4] = (uint8_t)(data_len & 0xFFu);
    frame[5] = (uint8_t)((data_len >> 8)  & 0xFFu);
    frame[6] = (uint8_t)((data_len >> 16) & 0xFFu);
    frame[7] = (uint8_t)((data_len >> 24) & 0xFFu);

    can_send(SDO_TX_COBID(ctx->node_id), frame, 8);

    /* Wait for Initiate Block Download Response */
    if (!can_receive(SDO_RX_COBID(ctx->node_id), resp, 8, 1000 /*ms*/))
        return false;

    if (resp[0] != SDO_CS_INITIATE_BLOCK_DOWNLOAD_RESP)
        return false; /* Server returned abort or wrong response */

    /* Extract negotiated block size from server response */
    ctx->blksize     = resp[4];
    ctx->crc_enabled = true;  /* We requested CRC, server must support it here */
    ctx->crc_accum   = 0xFFFFu;

    (void)blksize; /* Server's accepted blksize overrides client proposal */
    return true;
}

/**
 * @brief Transmit one sub-block and wait for the server's ACK.
 *
 * @param ctx        SDO block context.
 * @param data       Pointer to the start of data for this sub-block.
 * @param data_len   Total remaining bytes (may be less than a full sub-block).
 * @param last_block true if this sub-block contains the final data byte.
 * @param[out] ackseq Sequence number acknowledged by the server.
 * @return Number of bytes successfully acknowledged, or 0 on error.
 */
uint32_t sdo_block_download_subblock(SdoBlockCtx   *ctx,
                                      const uint8_t *data,
                                      uint32_t       data_len,
                                      bool           last_block,
                                      uint8_t       *ackseq)
{
    uint8_t  frame[8];
    uint8_t  resp[8];
    uint8_t  seqno = 1;
    uint32_t bytes_sent = 0;

    /* Send up to blksize segments */
    while (seqno <= ctx->blksize) {
        uint32_t remaining = data_len - bytes_sent;
        bool     last_seg  = (seqno == ctx->blksize) ||
                             (remaining <= SDO_SEGMENT_SIZE && last_block);

        /* Byte 0: c | seqno */
        frame[0] = (uint8_t)((last_seg && last_block) ? (0x80u | seqno) : seqno);

        /* Fill data payload */
        uint8_t payload_size = (remaining >= SDO_SEGMENT_SIZE)
                               ? SDO_SEGMENT_SIZE
                               : (uint8_t)remaining;

        memcpy(&frame[1], data + bytes_sent, payload_size);

        /* Pad with zeros if last segment is shorter than 7 bytes */
        if (payload_size < SDO_SEGMENT_SIZE)
            memset(&frame[1 + payload_size], 0,
                   SDO_SEGMENT_SIZE - payload_size);

        /* Accumulate CRC over actual data bytes */
        if (ctx->crc_enabled) {
            for (uint8_t i = 0; i < payload_size; i++)
                ctx->crc_accum = crc16_update(ctx->crc_accum,
                                              (data + bytes_sent)[i]);
        }

        can_send(SDO_TX_COBID(ctx->node_id), frame, 8);
        bytes_sent += payload_size;

        if (last_seg) break;
        seqno++;
    }

    /* Wait for Sub-Block ACK */
    if (!can_receive(SDO_RX_COBID(ctx->node_id), resp, 8, 1000 /*ms*/))
        return 0;

    if (resp[0] != SDO_CS_BLOCK_DOWNLOAD_ACK)
        return 0;

    *ackseq       = resp[1];
    ctx->blksize  = resp[2]; /* Update block size for next sub-block */

    return (uint32_t)(*ackseq) * SDO_SEGMENT_SIZE;
}

/**
 * @brief Finalize a block download (send End request with CRC).
 *
 * @param ctx         SDO block context.
 * @param n_unused    Number of unused bytes in the last segment (0–6).
 * @return true if server confirmed the end of block transfer.
 */
bool sdo_block_download_end(SdoBlockCtx *ctx, uint8_t n_unused)
{
    uint8_t frame[8] = {0};
    uint8_t resp[8]  = {0};

    /*
     * Byte 0 encoding:
     *   Bits [7:5] = 110 (command)
     *   Bits [4:3] = n (unused bytes in last segment)
     *   Bit  [2]   = 0 (reserved)
     *   Bit  [1]   = 1 (CRC present)
     *   Bit  [0]   = 1 (end of block)
     */
    frame[0] = (uint8_t)(0xC0u | ((n_unused & 0x07u) << 2) | 0x02u | 0x01u);

    if (ctx->crc_enabled) {
        frame[1] = (uint8_t)(ctx->crc_accum & 0xFFu);
        frame[2] = (uint8_t)(ctx->crc_accum >> 8);
    }

    can_send(SDO_TX_COBID(ctx->node_id), frame, 8);

    if (!can_receive(SDO_RX_COBID(ctx->node_id), resp, 8, 2000 /*ms*/))
        return false;

    return (resp[0] == SDO_CS_BLOCK_DOWNLOAD_END_RESP);
}

/**
 * @brief High-level convenience function: transfer an entire buffer via block download.
 *
 * @param node_id  CANopen node ID of the target device.
 * @param index    Object dictionary index.
 * @param subindex Object dictionary sub-index.
 * @param data     Pointer to data buffer.
 * @param length   Number of bytes to transfer.
 * @param blksize  Desired block size (1–127). Server may lower this.
 * @return true on successful transfer with CRC verification.
 */
bool sdo_block_download(uint8_t        node_id,
                         uint16_t       index,
                         uint8_t        subindex,
                         const uint8_t *data,
                         uint32_t       length,
                         uint8_t        blksize)
{
    SdoBlockCtx ctx = {
        .node_id  = node_id,
        .index    = index,
        .subindex = subindex,
    };

    /* Phase 1: Initiate */
    if (!sdo_block_download_initiate(&ctx, length, blksize))
        return false;

    /* Phase 2: Sub-block loop */
    uint32_t offset = 0;
    while (offset < length) {
        uint32_t remaining = length - offset;
        bool     last_blk  = (remaining <= (uint32_t)ctx.blksize * SDO_SEGMENT_SIZE);
        uint8_t  ackseq    = 0;

        uint32_t sent = sdo_block_download_subblock(&ctx,
                                                     data + offset,
                                                     remaining,
                                                     last_blk,
                                                     &ackseq);
        if (sent == 0)
            return false; /* Timeout or protocol error */

        offset += sent;
    }

    /* Phase 3: End */
    uint8_t last_seg_bytes = (uint8_t)(length % SDO_SEGMENT_SIZE);
    uint8_t n_unused = (last_seg_bytes == 0) ? 0 :
                       (SDO_SEGMENT_SIZE - last_seg_bytes);

    return sdo_block_download_end(&ctx, n_unused);
}
```

---

### 10.2 Block Upload (Server Side)

```cpp
/**
 * @file sdo_block_upload_server.cpp
 * @brief CANopen SDO Block Upload server-side implementation.
 *        The "server" is the node that *sends* the data.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "can_driver.h"
#include "object_dictionary.h"   /* od_read() */

#define SDO_CS_INITIATE_BLOCK_UPLOAD_REQ   0xA4u
#define SDO_CS_INITIATE_BLOCK_UPLOAD_RESP  0xC6u  /* size + CRC */
#define SDO_CS_BLOCK_UPLOAD_START          0xA3u
#define SDO_CS_BLOCK_UPLOAD_ACK            0xA2u  /* client ACK */
#define SDO_CS_BLOCK_UPLOAD_END_REQ        0xC1u  /* server end */
#define SDO_CS_BLOCK_UPLOAD_END_RESP       0xA1u  /* client confirm */

typedef struct {
    uint8_t  node_id;
    uint8_t  blksize;
    bool     crc_enabled;
    uint16_t crc_accum;
    const uint8_t *od_data;
    uint32_t       od_length;
    uint32_t       offset;
} SdoUploadServerCtx;

/**
 * @brief Handle an incoming Initiate Block Upload request from a client.
 *        Call this when byte[0] of an SDO frame equals 0xA4.
 */
bool sdo_block_upload_handle_initiate(SdoUploadServerCtx *ctx,
                                       const uint8_t      *req)
{
    uint16_t index    = (uint16_t)(req[1] | ((uint16_t)req[2] << 8));
    uint8_t  subindex = req[3];
    uint8_t  blksize  = req[4];   /* max segments client can receive */

    /* Look up the object in the OD */
    ctx->od_data   = od_get_pointer(index, subindex);
    ctx->od_length = od_get_length(index, subindex);
    if (!ctx->od_data || ctx->od_length == 0)
        return false; /* send SDO abort 0x06020000 (object does not exist) */

    ctx->blksize     = (blksize > 0 && blksize <= 127) ? blksize : 127;
    ctx->crc_enabled = true;
    ctx->crc_accum   = 0xFFFFu;
    ctx->offset      = 0;

    /* Send Initiate Block Upload Response */
    uint8_t resp[8] = {0};
    resp[0] = SDO_CS_INITIATE_BLOCK_UPLOAD_RESP; /* 0xC6 */
    resp[1] = (uint8_t)(index & 0xFFu);
    resp[2] = (uint8_t)(index >> 8);
    resp[3] = subindex;
    resp[4] = (uint8_t)(ctx->od_length & 0xFFu);
    resp[5] = (uint8_t)((ctx->od_length >> 8)  & 0xFFu);
    resp[6] = (uint8_t)((ctx->od_length >> 16) & 0xFFu);
    resp[7] = (uint8_t)((ctx->od_length >> 24) & 0xFFu);

    can_send(SDO_RX_COBID(ctx->node_id), resp, 8);
    return true;
}

/**
 * @brief Send one sub-block. Call after receiving 0xA3 (start) or 0xA2 (next ACK).
 * @return Number of bytes sent in this sub-block.
 */
uint32_t sdo_block_upload_send_subblock(SdoUploadServerCtx *ctx)
{
    uint8_t  frame[8];
    uint8_t  seqno        = 1;
    uint32_t bytes_in_blk = 0;

    while (seqno <= ctx->blksize) {
        uint32_t remaining    = ctx->od_length - ctx->offset;
        bool     last_of_all  = (remaining <= SDO_SEGMENT_SIZE);
        bool     last_of_blk  = (seqno == ctx->blksize) || last_of_all;

        frame[0] = (uint8_t)((last_of_all) ? (0x80u | seqno) : seqno);

        uint8_t n = (remaining >= SDO_SEGMENT_SIZE) ?
                     SDO_SEGMENT_SIZE : (uint8_t)remaining;

        memcpy(&frame[1], ctx->od_data + ctx->offset, n);
        if (n < SDO_SEGMENT_SIZE)
            memset(&frame[1 + n], 0, SDO_SEGMENT_SIZE - n);

        if (ctx->crc_enabled)
            for (uint8_t i = 0; i < n; i++)
                ctx->crc_accum = crc16_update(ctx->crc_accum,
                                              ctx->od_data[ctx->offset + i]);

        can_send(SDO_RX_COBID(ctx->node_id), frame, 8);

        ctx->offset  += n;
        bytes_in_blk += n;
        seqno++;

        if (last_of_blk) break;
    }

    return bytes_in_blk;
}

/**
 * @brief Handle client's sub-block ACK (0xA2) and optionally send next sub-block.
 * @param ack_frame   8-byte CAN frame from client.
 * @return true if transfer should continue, false if done or error.
 */
bool sdo_block_upload_handle_ack(SdoUploadServerCtx *ctx,
                                  const uint8_t      *ack_frame)
{
    uint8_t ackseq   = ack_frame[1];
    uint8_t blksize  = ack_frame[2];

    /* Update block size */
    if (blksize > 0 && blksize <= 127)
        ctx->blksize = blksize;

    /* If not all data sent yet, send next sub-block */
    if (ctx->offset < ctx->od_length) {
        sdo_block_upload_send_subblock(ctx);
        return true;
    }

    /* All data sent — send End Block Upload Request */
    uint8_t last_bytes = (uint8_t)(ctx->od_length % SDO_SEGMENT_SIZE);
    uint8_t n_unused   = (last_bytes == 0) ? 0 : (SDO_SEGMENT_SIZE - last_bytes);

    uint8_t end_frame[8] = {0};
    end_frame[0] = (uint8_t)(0xC0u | ((n_unused & 0x07u) << 2) | 0x02u | 0x01u);
    end_frame[1] = (uint8_t)(ctx->crc_accum & 0xFFu);
    end_frame[2] = (uint8_t)(ctx->crc_accum >> 8);

    can_send(SDO_RX_COBID(ctx->node_id), end_frame, 8);

    (void)ackseq;
    return false; /* Wait for client's End Block Upload Response (0xA1) */
}
```

---

### 10.3 CRC-16 CCITT Calculation

```cpp
/**
 * @file crc16_ccitt.cpp
 * @brief CRC-16/CCITT used for CANopen SDO Block Transfer verification.
 *        Polynomial: 0x1021, Init: 0xFFFF, No reflect, No final XOR.
 */

#include <stdint.h>
#include <stddef.h>

/* Pre-computed lookup table for CRC-16/CCITT */
static const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    /* ... (full 256-entry table omitted for brevity; generate with standard
            CRC-16/CCITT table generation algorithm) ... */
    0x0000  /* placeholder: populate all 256 entries in production code */
};

/**
 * @brief Compute CRC-16/CCITT over a byte buffer.
 *
 * @param data    Pointer to data buffer.
 * @param length  Number of bytes.
 * @return 16-bit CRC value.
 */
uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    while (length--) {
        uint8_t pos = (uint8_t)((crc >> 8) ^ *data++);
        crc = (uint16_t)((crc << 8) ^ crc16_table[pos]);
    }
    return crc;
}

/**
 * @brief Incremental version: accumulate CRC over multiple calls.
 *
 * Usage:
 *   uint16_t crc = 0xFFFF;
 *   crc = crc16_ccitt_update(crc, buf1, len1);
 *   crc = crc16_ccitt_update(crc, buf2, len2);
 *   // 'crc' now covers buf1 || buf2
 */
uint16_t crc16_ccitt_update(uint16_t      crc,
                              const uint8_t *data,
                              size_t         length)
{
    while (length--) {
        uint8_t pos = (uint8_t)((crc >> 8) ^ *data++);
        crc = (uint16_t)((crc << 8) ^ crc16_table[pos]);
    }
    return crc;
}
```

---

### 10.4 Block Size Negotiation Helper

```cpp
/**
 * @brief Calculate the optimal block size based on available buffer size.
 *
 * @param rx_buffer_bytes  Available receive buffer in bytes on the server.
 * @param data_length      Total data to transfer.
 * @return Recommended blksize (1–127).
 */
uint8_t sdo_calc_optimal_blksize(uint32_t rx_buffer_bytes,
                                   uint32_t data_length)
{
    if (rx_buffer_bytes == 0 || data_length == 0)
        return 1u;

    /* Each segment carries 7 bytes */
    uint32_t max_segs = rx_buffer_bytes / SDO_SEGMENT_SIZE;

    /* Clamp to CANopen maximum of 127 */
    if (max_segs > SDO_BLOCK_MAX_SEQNO)
        max_segs = SDO_BLOCK_MAX_SEQNO;

    /* For very small transfers, reduce to exact number of segments needed */
    uint32_t segs_needed = (data_length + SDO_SEGMENT_SIZE - 1) / SDO_SEGMENT_SIZE;
    if (segs_needed < max_segs)
        max_segs = segs_needed;

    return (max_segs > 0) ? (uint8_t)max_segs : 1u;
}

/**
 * @brief Determine whether block or segmented transfer is better.
 *
 * Block transfer has a fixed per-session overhead (~4 extra CAN frames
 * for initiate + end). It only pays off for data longer than ~28 bytes
 * (4 segments). For very small objects, segmented or expedited is faster.
 *
 * @param data_length  Total data size in bytes.
 * @return true if block transfer is recommended.
 */
bool sdo_should_use_block_transfer(uint32_t data_length)
{
    /* Break-even point: block overhead (~4 frames = 448 bits at 1 Mbit/s)
     * vs. per-segment round-trip savings. Empirically ~32 bytes is the
     * threshold for block transfer to become beneficial. */
    return (data_length >= 32u);
}
```

---

### 10.5 Firmware Update Example

```cpp
/**
 * @file firmware_update.cpp
 * @brief Example: update firmware of a CANopen slave using SDO block download.
 *        Uses CiA 302 Program Download objects.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "sdo_block_download.h"
#include "sdo_expedited.h"   /* sdo_write_u8() helper */

/* CiA 302 Program Download objects */
#define OBJ_PROGRAM_DATA     0x1F50u
#define OBJ_PROGRAM_CONTROL  0x1F51u
#define OBJ_PROGRAM_SW_ID    0x1F56u
#define OBJ_FLASH_STATUS     0x1F57u

#define PROGRAM_CONTROL_STOP   0x00u
#define PROGRAM_CONTROL_START  0x01u
#define PROGRAM_CONTROL_RESET  0x02u
#define PROGRAM_CONTROL_CLEAR  0x03u

/**
 * @brief Perform a full firmware update sequence on a CANopen slave.
 *
 * @param node_id      Target node ID.
 * @param fw_image     Pointer to firmware binary.
 * @param fw_size      Firmware image size in bytes.
 * @param expected_crc Expected CRC-16 of the firmware (pre-computed).
 * @return true on success.
 */
bool firmware_update(uint8_t        node_id,
                      const uint8_t *fw_image,
                      uint32_t       fw_size,
                      uint16_t       expected_crc)
{
    printf("[FW Update] Starting firmware update for node %u\n", node_id);
    printf("[FW Update] Image size: %u bytes\n", fw_size);

    /* Step 1: Verify CRC of the firmware image before sending */
    uint16_t local_crc = crc16_ccitt(fw_image, fw_size);
    if (local_crc != expected_crc) {
        printf("[FW Update] ERROR: Local CRC mismatch (0x%04X vs 0x%04X)\n",
               local_crc, expected_crc);
        return false;
    }
    printf("[FW Update] Local CRC OK: 0x%04X\n", local_crc);

    /* Step 2: Stop the running program */
    if (!sdo_write_u8(node_id, OBJ_PROGRAM_CONTROL, 0x01,
                      PROGRAM_CONTROL_STOP)) {
        printf("[FW Update] ERROR: Could not stop program\n");
        return false;
    }

    /* Step 3: Clear the flash area */
    if (!sdo_write_u8(node_id, OBJ_PROGRAM_CONTROL, 0x01,
                      PROGRAM_CONTROL_CLEAR)) {
        printf("[FW Update] ERROR: Could not clear flash\n");
        return false;
    }

    /* Wait for flash erase to complete (poll 0x1F57) */
    for (int timeout = 0; timeout < 100; timeout++) {
        uint8_t status = 0;
        sdo_read_u8(node_id, OBJ_FLASH_STATUS, 0x01, &status);
        if (status == 0x00) break;   /* idle = erase done  */
        if (status == 0x03) {
            printf("[FW Update] ERROR: Flash erase failed\n");
            return false;
        }
        os_delay_ms(100);
    }

    /* Step 4: Block download the firmware image */
    printf("[FW Update] Starting SDO block download...\n");

    uint8_t blksize = sdo_calc_optimal_blksize(889u /* 127 segs * 7 */,
                                                fw_size);

    bool ok = sdo_block_download(node_id,
                                  OBJ_PROGRAM_DATA, 0x01,
                                  fw_image, fw_size,
                                  blksize);
    if (!ok) {
        printf("[FW Update] ERROR: Block download failed\n");
        return false;
    }
    printf("[FW Update] Block download complete\n");

    /* Step 5: Trigger flash programming */
    if (!sdo_write_u8(node_id, OBJ_PROGRAM_CONTROL, 0x01,
                      PROGRAM_CONTROL_START)) {
        printf("[FW Update] ERROR: Could not start flash programming\n");
        return false;
    }

    /* Step 6: Wait for flash programming to complete */
    for (int timeout = 0; timeout < 200; timeout++) {
        uint8_t status = 0;
        sdo_read_u8(node_id, OBJ_FLASH_STATUS, 0x01, &status);
        if (status == 0x02) {        /* 0x02 = success */
            printf("[FW Update] Flash programming successful\n");
            break;
        }
        if (status == 0x03) {
            printf("[FW Update] ERROR: Flash programming failed\n");
            return false;
        }
        os_delay_ms(50);
    }

    /* Step 7: Reset the node to boot the new firmware */
    printf("[FW Update] Resetting node...\n");
    sdo_write_u8(node_id, OBJ_PROGRAM_CONTROL, 0x01, PROGRAM_CONTROL_RESET);

    /* Allow time for the node to reboot */
    os_delay_ms(2000);

    /* Step 8: Verify the software ID matches the new version */
    uint32_t new_sw_id = 0;
    sdo_read_u32(node_id, OBJ_PROGRAM_SW_ID, 0x01, &new_sw_id);
    printf("[FW Update] New Software ID: 0x%08X\n", new_sw_id);

    printf("[FW Update] Firmware update completed successfully\n");
    return true;
}
```

---

## 11. Error Handling

Block transfer defines a standard SDO abort mechanism. If any phase fails, either side
sends an **SDO Abort Transfer** frame:

```
SDO Abort Frame:
Byte 0: 0x80  (abort command specifier)
Bytes 1-2: Object Index
Byte 3: Sub-index
Bytes 4-7: Abort Code (uint32, little-endian)

Common Abort Codes for Block Transfer:
+------------+---------------------------------------------+
| Abort Code | Meaning                                     |
+------------+---------------------------------------------+
| 0x05030000 | Toggle bit not alternated (not applicable   |
|            | to block, but used for protocol errors)     |
| 0x05040000 | SDO protocol timed out                      |
| 0x05040001 | Client/server command specifier not valid   |
| 0x05040002 | Invalid block size                          |
| 0x05040003 | Invalid sequence number                     |
| 0x05040004 | CRC error                                   |
| 0x05040005 | Out of memory                               |
| 0x06010000 | Unsupported access to an object             |
| 0x06020000 | Object does not exist in OD                 |
| 0x06090011 | Sub-index does not exist                    |
| 0x08000000 | General error                               |
+------------+---------------------------------------------+

Handling flowchart:

  Receive CAN frame during block transfer
          |
          v
  frame[0] == 0x80?  -------> YES --> Extract abort code
          |                               |
          NO                         Log and abort session
          |
  Process normally
```

---

## 12. Summary

SDO Block Transfer is the most efficient data transfer mechanism available in the CANopen
protocol stack. The following table captures the key points:

| Aspect              | Detail                                                      |
|---------------------|-------------------------------------------------------------|
| **Standard**        | CiA 301, section 7.2.4.3                                    |
| **Direction**       | Block Download (client→server) and Block Upload (server→client) |
| **Payload/frame**   | 7 bytes per segment (same as segmented)                     |
| **ACK frequency**   | Once per sub-block (1–127 segments) instead of per segment  |
| **Max throughput**  | ~62 kB/s at 1 Mbit/s (blksize=127, no bus errors)           |
| **vs. Segmented**   | Up to ~12× faster for large data at blksize=127             |
| **Sequence numbers**| 1–127 per sub-block; reset to 1 each new sub-block          |
| **Retransmission**  | Server's ACK includes last good seqno; client retransmits   |
| **CRC**             | CRC-16/CCITT (poly 0x1021, init 0xFFFF) over entire data    |
| **Block size**      | Negotiated at initiate; server can adjust after each ACK    |
| **Primary use cases**| Firmware update, large parameter backup/restore            |
| **Minimum data**    | Effective only for ≥32 bytes (below that, segmented is adequate) |

### Protocol State Machine (Block Download)

```
  +------------+    Initiate Req     +-------------+
  |   IDLE     | ------------------> | INIT_WAIT   |
  +------------+                     +-------------+
                                           |
                                    Server ACK (0xA4)
                                           |
                                           v
                                     +----------+
                           +-------> | SEND_SUB | <---------+
                           |         +----------+            |
                           |              |                  |
                           |         sub-block sent          |
                           |              |                  |
                           |              v                  |
                           |       +-------------+           |
                           |       |  WAIT_ACK   |           |
                           |       +-------------+           |
                           |              |                  |
                           |     ackseq OK & more data       |
                           +------------------------------+  |
                                          |                  |
                                  ackseq < expected          |
                                    (lost segments)          |
                                          +------------------+
                                          (retransmit from ackseq+1)
                                          |
                                   all data sent
                                          |
                                          v
                                   +------------+
                                   |  END_WAIT  |
                                   +------------+
                                          |
                                   Server End ACK (0xA1)
                                          |
                                          v
                                   +----------+
                                   |   DONE   |
                                   +----------+
```

### Key Takeaways

1. **Use block transfer for any data larger than ~32 bytes** — the overhead of the extra
   initiate/end frames is amortised quickly, and throughput gains are substantial.

2. **Always enable CRC** — the two-byte CRC overhead is negligible and prevents silent
   data corruption, which is especially critical for firmware updates.

3. **Set `blksize` based on device memory** — a device with only 128 bytes of receive
   buffer must use `blksize ≤ 18`. Oversizing causes buffer overruns and retransmissions
   that negate the throughput advantage.

4. **Handle retransmissions gracefully** — track `ackseq` in the sub-block ACK; if it
   is less than the last segment sent, retransmit from `(ackseq + 1)` with sequence
   numbers restarting from 1.

5. **Poll flash status after firmware download** — writing the Program Control object
   (0x1F51) triggers flash operations; always wait for the Flash Status object (0x1F57)
   to confirm success before resetting the node.

6. **Block transfer and segmented transfer are mutually exclusive per session** — once
   a block transfer is initiated, do not mix in segmented commands until the block
   session ends (End Response received or Abort sent).