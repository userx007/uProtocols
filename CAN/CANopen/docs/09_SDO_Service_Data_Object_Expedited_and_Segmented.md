# CANopen Topic 09 — SDO: Service Data Object (Expedited & Segmented)

- **Intro & Fundamentals** — what SDO is, why it exists, and its CAN ID conventions
- **Client/Server Roles** — with an ASCII diagram showing the direction of download vs upload
- **Multiplexer** — Index + Sub-Index addressing with a C `od_lookup()` example
- **CAN Frame Layout** — byte-by-byte ASCII breakdown of the 8-byte SDO frame and command-specifier bit fields
- **Expedited Transfer** — ASCII ladder diagrams for both download and upload, command specifier table, and full C server-side handlers
- **Segmented Transfer** — ASCII ladder diagrams, toggle-bit diagram, segment cs bit layout, and C implementations for both initiate and segment handling (download and upload)
- **Abort Codes** — full table of the 0x05040000 series and neighbours, with a C lookup table
- **Timeout Handling** — ASCII state diagram and a periodic tick-based C timeout checker
- **Complete State Machine** — ASCII state diagram plus a unified `sdo_server_process()` dispatcher and init function
- **Worked Example** — byte-level trace of a 10-byte segmented upload ("MyDevice01")
- **Summary** — ASCII quick-reference table of all key concepts

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [SDO Fundamentals](#2-sdo-fundamentals)
3. [Client/Server Roles](#3-clientserver-roles)
4. [The Multiplexer: Index + Sub-Index](#4-the-multiplexer-index--sub-index)
5. [CAN Frame Layout for SDO](#5-can-frame-layout-for-sdo)
6. [Expedited Transfer](#6-expedited-transfer)
7. [Segmented Transfer](#7-segmented-transfer)
8. [SDO Abort Codes](#8-sdo-abort-codes-0x05040000-series)
9. [SDO Timeout Handling](#9-sdo-timeout-handling)
10. [Complete C Implementation: SDO Server State Machine](#10-complete-c-implementation-sdo-server-state-machine)
11. [Summary](#11-summary)

---

## 1. Introduction

**SDO (Service Data Object)** is the CANopen mechanism for *confirmed, point-to-point* access to any entry in a node's **Object Dictionary (OD)**. Unlike PDOs (Process Data Objects), which are unconfirmed and broadcast-style, an SDO exchange always involves a *request* followed by an *acknowledgement* — making it suitable for configuration, diagnostics, and parameterisation, rather than real-time process data.

SDO communication is the "postal system" of CANopen: reliable but not the fastest lane on the bus.

Key characteristics:

- Always uses exactly **two dedicated CAN IDs** per node pair (one for each direction).
- Accesses any OD entry by **Index (16-bit) + Sub-Index (8-bit)** — the "multiplexer".
- Supports two transfer modes: **Expedited** (≤ 4 bytes, single frame) and **Segmented** (> 4 bytes, multi-frame with handshake).
- **Confirmed**: every request frame gets a response frame (or an Abort).
- Governed by CiA 301, section 7.2.

---

## 2. SDO Fundamentals

### 2.1 Object Dictionary Access

Every CANopen node publishes an Object Dictionary — a structured table of all configurable and observable parameters. SDO is the universal key to read or write any cell of that table at any time, from any other node on the network.

```
   CANopen Network
   ┌─────────────────────────────────────────────────────┐
   │                                                     │
   │   [Manager / PLC]           [Device Node]           │
   │   SDO Client                SDO Server              │
   │        │                          │                 │
   │        │──── SDO Request ────────►│                 │
   │        │                          │  Object         │
   │        │                          │  Dictionary     │
   │        │◄─── SDO Response ────────│  [0x1000][0x00] │
   │        │                          │  [0x6040][0x00] │
   │        │                          │  [0x6041][0x00] │
   │                                                     │
   └─────────────────────────────────────────────────────┘
```

### 2.2 CAN Identifiers for SDO

By default (pre-configured communication parameters, CiA 301):

```
  SDO Request  (Client → Server):  COB-ID = 0x600 + Node-ID
  SDO Response (Server → Client):  COB-ID = 0x580 + Node-ID

  Example for Node-ID = 0x05:
    Client sends:   0x605
    Server replies: 0x585
```

Up to 128 SDO channels can be configured per node (via 0x1200–0x127F).

---

## 3. Client/Server Roles

| Role          | Description                                          | Typical Actor        |
|---------------|------------------------------------------------------|----------------------|
| **SDO Client**| Initiates the transfer; sends requests               | PLC, Master, Manager |
| **SDO Server**| Responds to requests; accesses its own OD            | Drive, Sensor, I/O   |

A node can be both client and server simultaneously (e.g., a gateway).

```
  ┌──────────────────────────────────────────────────────────────┐
  │                   SDO Transfer Roles                         │
  │                                                              │
  │   SDO CLIENT                        SDO SERVER               │
  │   ──────────                        ──────────               │
  │   • Initiates                       • Responds               │
  │   • Sends Download Request          • Reads/Writes OD        │
  │     (write to server OD)            • Returns Upload Data    │
  │   • Sends Upload Request            • Sends Abort on error   │
  │     (read from server OD)                                    │
  │                                                              │
  │   Download = Client  ──writes──►  Server OD (Client→Server)  │
  │   Upload   = Client  ◄──reads──   Server OD (Server→Client)  │
  │                                                              │
  └──────────────────────────────────────────────────────────────┘
```

> **Naming convention** (from the server's perspective):
> - **Download** = data flows *into* the server (client writes to server).
> - **Upload** = data flows *out of* the server (client reads from server).

---

## 4. The Multiplexer: Index + Sub-Index

Every SDO frame carries a **multiplexer** that pinpoints the exact OD entry being accessed.

```
  ┌────────────────────────────────────────────────────────────┐
  │               SDO Multiplexer (3 bytes)                    │
  │                                                            │
  │   Byte 1        Byte 2         Byte 3                      │
  │   ┌──────────┐  ┌──────────┐  ┌──────────┐                 │
  │   │Index Low │  │Index High│  │Sub-Index │                 │
  │   │(bits 0-7)│  │(bits 8-15│  │(0x00-FF) │                 │
  │   └──────────┘  └──────────┘  └──────────┘                 │
  │                                                            │
  │   Index:     16-bit OD main index (0x0000 – 0xFFFF)        │
  │   Sub-Index:  8-bit sub-entry     (0x00   – 0xFF  )        │
  │                                                            │
  │   Example: 0x6040 / 0x00  → Controlword, sub 0             │
  │            0x1018 / 0x01  → Vendor ID                      │
  │            0x6064 / 0x00  → Position Actual Value          │
  └────────────────────────────────────────────────────────────┘
```

In C this maps directly to a lookup function:

```c
/* Resolve an OD entry by index + sub-index */
ODEntry_t *od_lookup(uint16_t index, uint8_t subindex) {
    for (int i = 0; i < OD_SIZE; i++) {
        if (od_table[i].index == index &&
            od_table[i].subindex == subindex)
            return &od_table[i];
    }
    return NULL;  /* not found → SDO Abort 0x06020000 */
}
```

---

## 5. CAN Frame Layout for SDO

All SDO frames are exactly **8 bytes** (the full CAN data field). Unused bytes are set to zero.

```
  CAN Frame (8 bytes):
  ┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┐
  │ Byte 0 │ Byte 1 │ Byte 2 │ Byte 3 │ Byte 4 │ Byte 5 │ Byte 6 │ Byte 7 │
  ├────────┼────────┼────────┼────────┼────────┼────────┼────────┼────────┤
  │Command │ Index  │ Index  │Sub-Idx │      Data / Segment               │
  │ Spec.  │  Low   │  High  │        │                                   │
  └────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┘
     [0]      [1]      [2]      [3]      [4]      [5]      [6]      [7]

  Command Specifier (cs) — Byte 0 bit layout:
  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
  │  7  │  6  │  5  │  4  │  3  │  2  │  1  │  0  │
  ├─────┴─────┴─────┼─────┼─────┼─────┼─────┼─────┤
  │   ccs / scs     │  e  │  s  │  n1 │  n0 │  x  │
  │ (3 bits, cmd)   │exp'd│size │unused bytes (2b)│
  └─────────────────┴─────┴─────┴─────┴─────┴─────┘
  e = expedited flag (1 = expedited)
  s = size indicator (1 = size in 'n' field is valid)
  n = number of bytes NOT used in data field (0-3)
```

---

## 6. Expedited Transfer

Expedited transfer carries the entire data payload (1–4 bytes) in a **single request + single response**, making it the fastest and most common SDO mode.

### 6.1 Expedited Download (Client writes ≤ 4 bytes to Server)

```
  CLIENT (0x605)                                SERVER (0x585)
      │                                              │
      │  Byte: [0]    [1]  [2]  [3]  [4..7]          │
      │        ┌────┬────┬────┬────┬────────────────┐│
      │─────── │ cs │IdxL│IdxH│Sub │   Data (1-4B)  ││ ──────►
      │        └────┴────┴────┴────┴────────────────┘│
      │         0x23 (4B) 0x27(3B) 0x2B(2B) 0x2F(1B) │
      │                                              │
      │        ┌────┬────┬────┬────┬────────────────┐│
      │◄────── │ 60 │IdxL│IdxH│Sub │  00 00 00 00   ││ ───────
      │        └────┴────┴────┴────┴────────────────┘│
      │         (Download Response — confirmed OK)   │
      │                                              │
```

**Command specifier byte for expedited download:**

| Data Size | cs Byte | Meaning                        |
|-----------|---------|--------------------------------|
| 4 bytes   | `0x23`  | Expedited, 4 bytes, e=1, s=1, n=0 |
| 3 bytes   | `0x27`  | Expedited, 3 bytes, e=1, s=1, n=1 |
| 2 bytes   | `0x2B`  | Expedited, 2 bytes, e=1, s=1, n=2 |
| 1 byte    | `0x2F`  | Expedited, 1 byte,  e=1, s=1, n=3 |

Response is always `0x60`.

### 6.2 Expedited Upload (Client reads ≤ 4 bytes from Server)

```
  CLIENT (0x605)                                SERVER (0x585)
      │                                              │
      │        ┌────┬────┬────┬────┬────────────────┐│
      │─────── │ 40 │IdxL│IdxH│Sub │  00 00 00 00   ││ ──────►
      │        └────┴────┴────┴────┴────────────────┘│
      │         (Upload Request)                     │
      │                                              │
      │        ┌────┬────┬────┬────┬────────────────┐│
      │◄────── │ cs │IdxL│IdxH│Sub │   Data (1-4B)  ││ ───────
      │        └────┴────┴────┴────┴────────────────┘│
      │         0x43/0x47/0x4B/0x4F (4/3/2/1 bytes)  │
      │                                              │
```

### 6.3 C Implementation — Expedited Download & Upload

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* ── SDO Command Specifiers ─────────────────────────────── */
#define SDO_CS_EXPEDITED_DL_4B   0x23u   /* Download 4 bytes */
#define SDO_CS_EXPEDITED_DL_3B   0x27u   /* Download 3 bytes */
#define SDO_CS_EXPEDITED_DL_2B   0x2Bu   /* Download 2 bytes */
#define SDO_CS_EXPEDITED_DL_1B   0x2Fu   /* Download 1 byte  */
#define SDO_CS_EXPEDITED_DL_RSP  0x60u   /* Download response */

#define SDO_CS_EXPEDITED_UL_REQ  0x40u   /* Upload request   */
#define SDO_CS_EXPEDITED_UL_4B   0x43u   /* Upload response 4 bytes */
#define SDO_CS_EXPEDITED_UL_3B   0x47u
#define SDO_CS_EXPEDITED_UL_2B   0x4Bu
#define SDO_CS_EXPEDITED_UL_1B   0x4Fu

#define SDO_CS_ABORT             0x80u   /* Abort transfer   */

/* ── CAN frame wrapper ───────────────────────────────────── */
typedef struct {
    uint32_t cob_id;
    uint8_t  data[8];
    uint8_t  dlc;
} CANFrame_t;

/* ── Object Dictionary entry ─────────────────────────────── */
typedef struct {
    uint16_t index;
    uint8_t  subindex;
    uint8_t  attr;          /* bit0=readable, bit1=writable */
    uint8_t  size;          /* data size in bytes (1–4 for expedited) */
    uint8_t *data_ptr;
} ODEntry_t;

/* Forward declarations */
ODEntry_t *od_lookup(uint16_t index, uint8_t subindex);
void       can_send(const CANFrame_t *frame);

/* ── Helper: build SDO abort frame ──────────────────────── */
static void sdo_send_abort(uint32_t cob_id_response,
                           uint16_t index,
                           uint8_t  subindex,
                           uint32_t abort_code)
{
    CANFrame_t f = {0};
    f.cob_id   = cob_id_response;
    f.dlc      = 8;
    f.data[0]  = SDO_CS_ABORT;
    f.data[1]  = (uint8_t)(index & 0xFF);
    f.data[2]  = (uint8_t)(index >> 8);
    f.data[3]  = subindex;
    f.data[4]  = (uint8_t)(abort_code);
    f.data[5]  = (uint8_t)(abort_code >> 8);
    f.data[6]  = (uint8_t)(abort_code >> 16);
    f.data[7]  = (uint8_t)(abort_code >> 24);
    can_send(&f);
}

/* ── Expedited Download Handler (Server side) ───────────── */
void sdo_handle_expedited_download(const CANFrame_t *req,
                                   uint32_t response_cob_id)
{
    uint8_t  cs       = req->data[0];
    uint16_t index    = (uint16_t)(req->data[1] | (req->data[2] << 8));
    uint8_t  subindex = req->data[3];

    /* Number of bytes NOT used in data field */
    uint8_t n         = (cs >> 2) & 0x03u;
    uint8_t data_size = (uint8_t)(4u - n);

    ODEntry_t *entry = od_lookup(index, subindex);
    if (!entry) {
        /* Object does not exist */
        sdo_send_abort(response_cob_id, index, subindex, 0x06020000u);
        return;
    }
    if (!(entry->attr & 0x02u)) {
        /* Write not allowed */
        sdo_send_abort(response_cob_id, index, subindex, 0x06010002u);
        return;
    }
    if (data_size != entry->size) {
        /* Data type mismatch / length */
        sdo_send_abort(response_cob_id, index, subindex, 0x06070010u);
        return;
    }

    /* Write data to OD entry */
    memcpy(entry->data_ptr, &req->data[4], data_size);

    /* Send Download Response */
    CANFrame_t rsp = {0};
    rsp.cob_id  = response_cob_id;
    rsp.dlc     = 8;
    rsp.data[0] = SDO_CS_EXPEDITED_DL_RSP;
    rsp.data[1] = (uint8_t)(index & 0xFF);
    rsp.data[2] = (uint8_t)(index >> 8);
    rsp.data[3] = subindex;
    can_send(&rsp);
}

/* ── Expedited Upload Handler (Server side) ─────────────── */
void sdo_handle_expedited_upload(const CANFrame_t *req,
                                 uint32_t response_cob_id)
{
    uint16_t index    = (uint16_t)(req->data[1] | (req->data[2] << 8));
    uint8_t  subindex = req->data[3];

    ODEntry_t *entry = od_lookup(index, subindex);
    if (!entry) {
        sdo_send_abort(response_cob_id, index, subindex, 0x06020000u);
        return;
    }
    if (!(entry->attr & 0x01u)) {
        /* Read not allowed */
        sdo_send_abort(response_cob_id, index, subindex, 0x06010001u);
        return;
    }
    if (entry->size > 4u) {
        /* Too large for expedited — use segmented */
        sdo_send_abort(response_cob_id, index, subindex, 0x06070012u);
        return;
    }

    /* Build response cs from data size */
    static const uint8_t size_to_cs[5] = {
        0, SDO_CS_EXPEDITED_UL_1B, SDO_CS_EXPEDITED_UL_2B,
           SDO_CS_EXPEDITED_UL_3B, SDO_CS_EXPEDITED_UL_4B
    };

    CANFrame_t rsp = {0};
    rsp.cob_id  = response_cob_id;
    rsp.dlc     = 8;
    rsp.data[0] = size_to_cs[entry->size];
    rsp.data[1] = (uint8_t)(index & 0xFF);
    rsp.data[2] = (uint8_t)(index >> 8);
    rsp.data[3] = subindex;
    memcpy(&rsp.data[4], entry->data_ptr, entry->size);
    can_send(&rsp);
}
```

---

## 7. Segmented Transfer

When the data to be transferred exceeds 4 bytes, SDO switches to **segmented** mode. The full payload is split into chunks of up to 7 bytes, each transmitted in its own CAN frame. Both download and upload use a **toggle bit** to keep sender and receiver synchronised.

### 7.1 Segmented Download (Client writes > 4 bytes to Server)

```
  CLIENT                                           SERVER
    │                                                │
    │  ① Initiate Download Request (0x21 + size)    │
    │─────────────────────────────────────────────►  │
    │   [21][IdxL][IdxH][Sub][Size0][Size1][Size2][Size3]
    │                                                │
    │  ② Initiate Download Response (0x60)          │
    │◄─────────────────────────────────────────────  │
    │   [60][IdxL][IdxH][Sub][00][00][00][00]        │
    │                                                │
    │  ③ Download Segment (toggle=0, up to 7 bytes) │
    │─────────────────────────────────────────────►  │
    │   [cs][D0][D1][D2][D3][D4][D5][D6]             │
    │    cs = 0x00 (7B,not-last) or 0x01 (last)      │
    │                                                │
    │  ④ Segment Acknowledged (toggle echo)         │
    │◄─────────────────────────────────────────────  │
    │   [20][00][00][00][00][00][00][00] (toggle=0)  │
    │                                                │
    │  ⑤ Download Segment (toggle=1, next chunk)    │
    │─────────────────────────────────────────────►  │
    │   [cs | 0x10] ...                              │
    │                                                │
    │  ⑥ Segment Acknowledged (toggle=1 echo)       │
    │◄─────────────────────────────────────────────  │
    │   [30][00]...                                  │
    │                                                │
    │    ... continues until last segment ...        │
    │                                                │
```

**Segment command specifier layout:**

```
  Byte 0 of Download Segment:
  ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
  │  7  │  6  │  5  │  4  │  3  │  2  │  1  │  0  │
  ├─────┴─────┴─────┼─────┼─────┼─────┼─────┼─────┤
  │   000 (cmd=0)   │  t  │     n (3 bits)  │  c  │
  └─────────────────┴─────┴─────┴─────┴─────┴─────┘

  t = toggle bit (alternates 0,1,0,1,... per segment)
  n = number of bytes NOT used in segment (0–7)
  c = 1 if this is the LAST segment, 0 otherwise
```

### 7.2 Segmented Upload (Client reads > 4 bytes from Server)

```
  CLIENT                                           SERVER
    │                                                │
    │  ① Initiate Upload Request (0x40)             │
    │─────────────────────────────────────────────►  │
    │   [40][IdxL][IdxH][Sub][00][00][00][00]        │
    │                                                │
    │  ② Initiate Upload Response (0x41 + total size)│
    │◄─────────────────────────────────────────────  │
    │   [41][IdxL][IdxH][Sub][Size0..Size3]          │
    │                                                │
    │  ③ Upload Segment Request (toggle=0)          │
    │─────────────────────────────────────────────►  │
    │   [60][00][00][00][00][00][00][00]             │
    │    cs=0x60 (toggle=0) / 0x70 (toggle=1)        │
    │                                                │
    │  ④ Upload Segment Response (data chunk)       │
    │◄─────────────────────────────────────────────  │
    │   [cs][D0][D1][D2][D3][D4][D5][D6]             │
    │                                                │
    │    ... repeat ③④ until c=1 (last segment) ...│
    │                                                │
```

### 7.3 The Toggle Bit

The toggle bit is the reliability mechanism of segmented SDO. It alternates (0→1→0→1) with every segment and its acknowledgement. If a frame is duplicated or lost, the mismatch triggers an abort.

```
  Segment:    0     1     2     3     4     5
              │     │     │     │     │     │
  Toggle:   ──0─────1─────0─────1─────0─────1──►
              ▲           ▲           ▲
              │           │           │
              Toggle flip on each successfully ACK'd segment
```

### 7.4 C Implementation — Segmented SDO Server

```c
/* ── Segmented SDO Transfer State ───────────────────────── */
typedef enum {
    SDO_STATE_IDLE = 0,
    SDO_STATE_DOWNLOAD_INITIATE,
    SDO_STATE_DOWNLOAD_SEGMENT,
    SDO_STATE_UPLOAD_INITIATE,
    SDO_STATE_UPLOAD_SEGMENT,
    SDO_STATE_ABORT
} SDOState_t;

#define SDO_MAX_DATA  256u   /* Max OD object size for segmented transfer */

typedef struct {
    SDOState_t  state;
    uint16_t    index;
    uint8_t     subindex;
    uint8_t     toggle;          /* expected toggle bit (0 or 1) */
    uint32_t    total_size;      /* total bytes to transfer       */
    uint32_t    bytes_done;      /* bytes transferred so far      */
    uint8_t     buffer[SDO_MAX_DATA];
    uint32_t    response_cob_id;
    uint32_t    timeout_ms;      /* timestamp for timeout check   */
} SDOServer_t;

/* ── Command specifiers for segmented ───────────────────── */
#define SDO_CS_SEG_DL_INIT_REQ   0x21u  /* Initiate download, size indicated */
#define SDO_CS_SEG_DL_INIT_RSP   0x60u  /* Initiate download response        */
#define SDO_CS_SEG_UL_INIT_REQ   0x40u  /* Initiate upload request           */
#define SDO_CS_SEG_UL_INIT_RSP   0x41u  /* Initiate upload response          */

/* Download segment cs: t=toggle, n=unused bytes, c=last */
#define SDO_SEG_DL_CS(t,n,c)  (uint8_t)(((t)<<4)|((n)<<1)|(c))
/* Upload  segment request cs: 0x60 | (toggle<<4) */
#define SDO_SEG_UL_REQ_CS(t)  (uint8_t)(0x60u | ((t)<<4))
/* Upload  segment response cs: (toggle<<4)|(n<<1)|c */
#define SDO_SEG_UL_RSP_CS(t,n,c) (uint8_t)(((t)<<4)|((n)<<1)|(c))

static SDOServer_t sdo_srv;

/* Get system tick in ms — implement for your platform */
extern uint32_t sys_tick_ms(void);

/* ── Initiate Segmented Download ────────────────────────── */
static void sdo_initiate_seg_download(const CANFrame_t *req)
{
    uint16_t index    = (uint16_t)(req->data[1] | (req->data[2] << 8));
    uint8_t  subindex = req->data[3];
    uint32_t size     = (uint32_t)( req->data[4]
                      | ((uint32_t)req->data[5] << 8)
                      | ((uint32_t)req->data[6] << 16)
                      | ((uint32_t)req->data[7] << 24));

    ODEntry_t *entry = od_lookup(index, subindex);
    if (!entry) {
        sdo_send_abort(sdo_srv.response_cob_id, index, subindex, 0x06020000u);
        sdo_srv.state = SDO_STATE_IDLE;
        return;
    }
    if (!(entry->attr & 0x02u)) {
        sdo_send_abort(sdo_srv.response_cob_id, index, subindex, 0x06010002u);
        sdo_srv.state = SDO_STATE_IDLE;
        return;
    }
    if (size > SDO_MAX_DATA) {
        sdo_send_abort(sdo_srv.response_cob_id, index, subindex, 0x06070012u);
        sdo_srv.state = SDO_STATE_IDLE;
        return;
    }

    sdo_srv.index      = index;
    sdo_srv.subindex   = subindex;
    sdo_srv.total_size = size;
    sdo_srv.bytes_done = 0;
    sdo_srv.toggle     = 0;
    sdo_srv.state      = SDO_STATE_DOWNLOAD_SEGMENT;
    sdo_srv.timeout_ms = sys_tick_ms();

    /* Respond: Initiate Download Response */
    CANFrame_t rsp = {0};
    rsp.cob_id  = sdo_srv.response_cob_id;
    rsp.dlc     = 8;
    rsp.data[0] = SDO_CS_SEG_DL_INIT_RSP;
    rsp.data[1] = (uint8_t)(index & 0xFF);
    rsp.data[2] = (uint8_t)(index >> 8);
    rsp.data[3] = subindex;
    can_send(&rsp);
}

/* ── Handle Incoming Download Segment ───────────────────── */
static void sdo_handle_seg_download(const CANFrame_t *req)
{
    uint8_t cs     = req->data[0];
    uint8_t toggle = (cs >> 4) & 0x01u;
    uint8_t n      = (cs >> 1) & 0x07u;   /* unused bytes */
    uint8_t last   = cs & 0x01u;
    uint8_t count  = (uint8_t)(7u - n);

    if (toggle != sdo_srv.toggle) {
        /* Toggle mismatch → Abort */
        sdo_send_abort(sdo_srv.response_cob_id,
                       sdo_srv.index, sdo_srv.subindex, 0x05030000u);
        sdo_srv.state = SDO_STATE_IDLE;
        return;
    }

    if (sdo_srv.bytes_done + count > SDO_MAX_DATA) {
        sdo_send_abort(sdo_srv.response_cob_id,
                       sdo_srv.index, sdo_srv.subindex, 0x06070012u);
        sdo_srv.state = SDO_STATE_IDLE;
        return;
    }

    /* Copy segment data into buffer */
    memcpy(&sdo_srv.buffer[sdo_srv.bytes_done], &req->data[1], count);
    sdo_srv.bytes_done += count;
    sdo_srv.timeout_ms  = sys_tick_ms();

    /* Send segment acknowledge */
    CANFrame_t rsp = {0};
    rsp.cob_id  = sdo_srv.response_cob_id;
    rsp.dlc     = 8;
    rsp.data[0] = (uint8_t)(0x20u | (sdo_srv.toggle << 4));
    can_send(&rsp);

    /* Toggle for next segment */
    sdo_srv.toggle ^= 1u;

    if (last) {
        /* All segments received — write to OD */
        ODEntry_t *entry = od_lookup(sdo_srv.index, sdo_srv.subindex);
        if (entry) {
            memcpy(entry->data_ptr, sdo_srv.buffer, sdo_srv.bytes_done);
        }
        sdo_srv.state = SDO_STATE_IDLE;
    }
}

/* ── Initiate Segmented Upload ──────────────────────────── */
static void sdo_initiate_seg_upload(const CANFrame_t *req)
{
    uint16_t index    = (uint16_t)(req->data[1] | (req->data[2] << 8));
    uint8_t  subindex = req->data[3];

    ODEntry_t *entry = od_lookup(index, subindex);
    if (!entry) {
        sdo_send_abort(sdo_srv.response_cob_id, index, subindex, 0x06020000u);
        sdo_srv.state = SDO_STATE_IDLE;
        return;
    }
    if (!(entry->attr & 0x01u)) {
        sdo_send_abort(sdo_srv.response_cob_id, index, subindex, 0x06010001u);
        sdo_srv.state = SDO_STATE_IDLE;
        return;
    }

    sdo_srv.index      = index;
    sdo_srv.subindex   = subindex;
    sdo_srv.total_size = entry->size;
    sdo_srv.bytes_done = 0;
    sdo_srv.toggle     = 0;
    sdo_srv.state      = SDO_STATE_UPLOAD_SEGMENT;
    sdo_srv.timeout_ms = sys_tick_ms();

    /* Copy OD data into local buffer */
    memcpy(sdo_srv.buffer, entry->data_ptr, entry->size);

    /* Respond: Initiate Upload Response */
    CANFrame_t rsp = {0};
    rsp.cob_id  = sdo_srv.response_cob_id;
    rsp.dlc     = 8;
    rsp.data[0] = SDO_CS_SEG_UL_INIT_RSP;
    rsp.data[1] = (uint8_t)(index & 0xFF);
    rsp.data[2] = (uint8_t)(index >> 8);
    rsp.data[3] = subindex;
    rsp.data[4] = (uint8_t)(entry->size);
    rsp.data[5] = (uint8_t)(entry->size >> 8);
    rsp.data[6] = (uint8_t)(entry->size >> 16);
    rsp.data[7] = (uint8_t)(entry->size >> 24);
    can_send(&rsp);
}

/* ── Handle Upload Segment Request ─────────────────────── */
static void sdo_handle_seg_upload_req(const CANFrame_t *req)
{
    uint8_t toggle = (req->data[0] >> 4) & 0x01u;

    if (toggle != sdo_srv.toggle) {
        sdo_send_abort(sdo_srv.response_cob_id,
                       sdo_srv.index, sdo_srv.subindex, 0x05030000u);
        sdo_srv.state = SDO_STATE_IDLE;
        return;
    }

    uint32_t remaining = sdo_srv.total_size - sdo_srv.bytes_done;
    uint8_t  count     = (remaining > 7u) ? 7u : (uint8_t)remaining;
    uint8_t  last      = (count == remaining) ? 1u : 0u;
    uint8_t  n         = (uint8_t)(7u - count);

    CANFrame_t rsp = {0};
    rsp.cob_id  = sdo_srv.response_cob_id;
    rsp.dlc     = 8;
    rsp.data[0] = SDO_SEG_UL_RSP_CS(sdo_srv.toggle, n, last);
    memcpy(&rsp.data[1], &sdo_srv.buffer[sdo_srv.bytes_done], count);
    can_send(&rsp);

    sdo_srv.bytes_done += count;
    sdo_srv.toggle     ^= 1u;
    sdo_srv.timeout_ms  = sys_tick_ms();

    if (last) {
        sdo_srv.state = SDO_STATE_IDLE;
    }
}
```

---

## 8. SDO Abort Codes (0x05040000 Series)

When an error occurs, either side may send an **SDO Abort** frame (cs = 0x80) with a 32-bit abort code in bytes 4–7. The 0x05040000 series covers protocol-level errors.

```
  SDO Abort Frame:
  ┌────┬────┬────┬────┬────┬────┬────┬────┐
  │ 80 │IdxL│IdxH│Sub │ AC0│ AC1│ AC2│ AC3│
  └────┴────┴────┴────┴────┴────┴────┴────┘
                         └───── 32-bit Abort Code (little-endian) ─────┘
```

### 8.1 Common Abort Codes

| Abort Code   | Meaning                                              |
|--------------|------------------------------------------------------|
| `0x05030000` | Toggle bit not alternated                            |
| `0x05040000` | SDO protocol timed out                               |
| `0x05040001` | Client/server command specifier unknown              |
| `0x05040002` | Invalid block size                                   |
| `0x05040003` | Invalid sequence number                              |
| `0x05040004` | CRC error                                            |
| `0x05040005` | Out of memory                                        |
| `0x06010000` | Unsupported access to an object                      |
| `0x06010001` | Attempt to read a write-only object                  |
| `0x06010002` | Attempt to write a read-only object                  |
| `0x06020000` | Object does not exist in the object dictionary       |
| `0x06040041` | Object cannot be mapped to the PDO                   |
| `0x06070010` | Data type doesn't match — length doesn't match       |
| `0x06070012` | Data type doesn't match — length too high            |
| `0x06090011` | Sub-index does not exist                             |
| `0x06090030` | Value range of parameter exceeded                    |
| `0x08000000` | General error                                        |
| `0x08000020` | Data cannot be transferred or stored to the app      |
| `0x08000022` | Data cannot be stored — local control                |

### 8.2 C — Abort Code Lookup Table

```c
typedef struct {
    uint32_t    code;
    const char *description;
} SDOAbortEntry_t;

static const SDOAbortEntry_t sdo_abort_table[] = {
    { 0x05030000u, "Toggle bit not alternated"             },
    { 0x05040000u, "SDO protocol timed out"                },
    { 0x05040001u, "Unknown command specifier"             },
    { 0x06020000u, "Object does not exist"                 },
    { 0x06010001u, "Read from write-only object"           },
    { 0x06010002u, "Write to read-only object"             },
    { 0x06070010u, "Data length mismatch"                  },
    { 0x06070012u, "Data length too high"                  },
    { 0x06090011u, "Sub-index does not exist"              },
    { 0x06090030u, "Value out of range"                    },
    { 0x08000000u, "General error"                         },
    { 0x00000000u, NULL }   /* sentinel */
};

const char *sdo_abort_string(uint32_t code) {
    for (int i = 0; sdo_abort_table[i].description; i++) {
        if (sdo_abort_table[i].code == code)
            return sdo_abort_table[i].description;
    }
    return "Unknown abort code";
}
```

---

## 9. SDO Timeout Handling

CiA 301 specifies that if a response is not received within a configurable timeout period, the transfer must be aborted with code `0x05040000`.

The default SDO timeout is typically **1000 ms** but is configurable per implementation.

### 9.1 Timeout State Machine

```
  SDO Timeout State:

    ┌──────────────────────────────────────────────────────┐
    │                  SDO Timeout Check                   │
    │                                                      │
    │  Each segment exchange:                              │
    │  ┌─────────┐  Request sent   ┌──────────────────┐    │
    │  │         │────────────────►│   Awaiting ACK   │    │
    │  │  IDLE   │                 │                  │    │
    │  │         │◄────────────────│  Timer running   │    │
    │  └─────────┘  ACK received   └────────┬─────────┘    │
    │                                       │              │
    │                                  timeout?            │
    │                                       │              │
    │                              ┌────────▼────────┐     │
    │                              │  Send ABORT     │     │
    │                              │  0x05040000     │     │
    │                              │  → IDLE         │     │
    │                              └─────────────────┘     │
    └──────────────────────────────────────────────────────┘
```

### 9.2 C — Timeout Check (Call from periodic task)

```c
#define SDO_TIMEOUT_MS  1000u   /* CiA 301 default: 1 second */

/* Call this from a 1ms or 10ms periodic tick */
void sdo_server_check_timeout(void)
{
    if (sdo_srv.state == SDO_STATE_IDLE) return;

    uint32_t elapsed = sys_tick_ms() - sdo_srv.timeout_ms;

    if (elapsed > SDO_TIMEOUT_MS) {
        /* Timeout — abort the current transfer */
        sdo_send_abort(sdo_srv.response_cob_id,
                       sdo_srv.index,
                       sdo_srv.subindex,
                       0x05040000u);   /* SDO protocol timed out */
        sdo_srv.state = SDO_STATE_IDLE;
    }
}
```

---

## 10. Complete C Implementation: SDO Server State Machine

The following integrates all preceding elements into a single dispatching state machine suitable for an embedded SDO server. It is called from the CAN receive interrupt or task with every incoming frame on the node's SDO request COB-ID.

```
  SDO Server State Machine:

  ┌──────────────────────────────────────────────────────────────┐
  │                                                              │
  │                       IDLE                                   │
  │                      ┌─────┐                                 │
  │         ┌────────────┤     ├─────────────┐                   │
  │         │            └─────┘             │                   │
  │    cs=0x21/0x23..2F        cs=0x40       │                   │
  │    (Download Init)   (Upload Init)       │                   │
  │         │                   │            │                   │
  │         ▼                   ▼            │ cs=0x80 (Abort)   │
  │  ┌─────────────┐   ┌──────────────┐      │ received          │
  │  │ DL_SEGMENT  │   │ UL_SEGMENT   │      │ → reset to IDLE   │
  │  │  (wait seg) │   │ (wait ul req)│◄─────┘                   │
  │  └──────┬──────┘   └──────┬───────┘                          │
  │         │                 │                                  │
  │    seg  │                 │ ul req                           │
  │    rcvd │                 │ rcvd                             │
  │         │                 │                                  │
  │    last?│                 │ last?                            │
  │  ┌──────▼──────┐   ┌──────▼───────┐                          │
  │  │  Write OD   │   │  Send Seg    │                          │
  │  │  → IDLE     │   │  → IDLE (if  │                          │
  │  └─────────────┘   │    last)     │                          │
  │                    └──────────────┘                          │
  │                                                              │
  │  Any state: timeout → Abort 0x05040000 → IDLE                │
  └──────────────────────────────────────────────────────────────┘
```

```c
/* ── Main SDO Server Dispatcher ─────────────────────────── */
void sdo_server_process(const CANFrame_t *req)
{
    if (req->dlc < 8) return;  /* SDO frames are always 8 bytes */

    uint8_t cs = req->data[0];

    /* Handle abort from client at any time */
    if (cs == SDO_CS_ABORT) {
        sdo_srv.state = SDO_STATE_IDLE;
        return;
    }

    switch (sdo_srv.state) {

    case SDO_STATE_IDLE:
        if ((cs & 0xF0u) == 0x20u) {
            /* Expedited or Segmented Download Initiate */
            if (cs & 0x02u) {
                /* Expedited: e=1 */
                sdo_handle_expedited_download(req, sdo_srv.response_cob_id);
            } else {
                /* Segmented: e=0, s=1 */
                sdo_initiate_seg_download(req);
            }
        } else if (cs == SDO_CS_EXPEDITED_UL_REQ) {
            /* Check whether entry is expedited or must be segmented */
            uint16_t idx = (uint16_t)(req->data[1] | (req->data[2] << 8));
            uint8_t  sub = req->data[3];
            ODEntry_t *e = od_lookup(idx, sub);
            if (e && e->size <= 4u) {
                sdo_handle_expedited_upload(req, sdo_srv.response_cob_id);
            } else {
                sdo_initiate_seg_upload(req);
            }
        } else {
            /* Unknown request in IDLE */
            sdo_send_abort(sdo_srv.response_cob_id, 0, 0, 0x05040001u);
        }
        break;

    case SDO_STATE_DOWNLOAD_SEGMENT:
        if ((cs & 0xE0u) == 0x00u) {
            /* Download segment */
            sdo_handle_seg_download(req);
        } else {
            sdo_send_abort(sdo_srv.response_cob_id,
                           sdo_srv.index, sdo_srv.subindex, 0x05040001u);
            sdo_srv.state = SDO_STATE_IDLE;
        }
        break;

    case SDO_STATE_UPLOAD_SEGMENT:
        if ((cs & 0xE0u) == 0x60u) {
            /* Upload segment request */
            sdo_handle_seg_upload_req(req);
        } else {
            sdo_send_abort(sdo_srv.response_cob_id,
                           sdo_srv.index, sdo_srv.subindex, 0x05040001u);
            sdo_srv.state = SDO_STATE_IDLE;
        }
        break;

    default:
        sdo_srv.state = SDO_STATE_IDLE;
        break;
    }
}

/* ── SDO Server Initialisation ───────────────────────────── */
void sdo_server_init(uint8_t node_id)
{
    memset(&sdo_srv, 0, sizeof(sdo_srv));
    sdo_srv.state           = SDO_STATE_IDLE;
    sdo_srv.response_cob_id = 0x580u + node_id;
}
```

### 10.1 Example Usage

```c
/* Node-ID = 5; SDO requests arrive on CAN ID 0x605 */

void app_main(void) {
    sdo_server_init(5u);

    /* Register a 10ms periodic task for timeout checking */
    timer_register_callback(10, sdo_server_check_timeout);

    /* Main CAN receive loop */
    CANFrame_t frame;
    while (1) {
        if (can_receive(&frame)) {
            if (frame.cob_id == 0x605u) {
                sdo_server_process(&frame);
            }
        }
    }
}
```

### 10.2 Worked Example — Reading a 10-byte Device Name (Segmented Upload)

```
  Assume: Node-ID=5, OD[0x1008][0x00] = "MyDevice01" (10 bytes)

  CLIENT → 0x605:  [40][08][10][00][00][00][00][00]   Upload Init Request
  SERVER → 0x585:  [41][08][10][00][0A][00][00][00]   Response: 10 bytes total
                                          ↑
                                      0x0A = 10

  CLIENT → 0x605:  [60][00][00][00][00][00][00][00]   Segment Req (toggle=0)
  SERVER → 0x585:  [00][4D][79][44][65][76][69][63]   Seg 0: "MyDevic" (7 bytes)
                    ↑                                  toggle=0, n=0, last=0

  CLIENT → 0x605:  [70][00][00][00][00][00][00][00]   Segment Req (toggle=1)
  SERVER → 0x585:  [1B][65][30][31][00][00][00][00]   Seg 1: "e01" (3 bytes)
                    ↑                                  toggle=1, n=4, last=1
                    0x1B = 0001_1011 → t=1,n=101(4),c=1
```

---

## 11. Summary

```
  ┌──────────────────────────────────────────────────────────────┐
  │              CANopen SDO — Key Concepts at a Glance          │
  ├────────────────────────┬─────────────────────────────────────┤
  │ Concept                │ Detail                              │
  ├────────────────────────┼─────────────────────────────────────┤
  │ Purpose                │ Confirmed OD read/write             │
  │ Communication          │ Point-to-point, client↔server       │
  │ CAN IDs (default)      │ 0x600+NodeID (req) / 0x580+NodeID   │
  │ Frame size             │ Always 8 bytes                      │
  │ Multiplexer            │ 16-bit Index + 8-bit Sub-Index      │
  │ Expedited              │ 1–4 bytes, single frame each way    │
  │ Segmented              │ >4 bytes, multi-frame + toggle bit  │
  │ Toggle bit             │ Alternates per segment; detects loss│
  │ Abort                  │ cs=0x80 + 32-bit abort code         │
  │ Timeout code           │ 0x05040000                          │
  │ Timeout default        │ 1000 ms (CiA 301)                   │
  │ Download               │ Client writes → Server OD           │
  │ Upload                 │ Client reads ← Server OD            │
  └────────────────────────┴─────────────────────────────────────┘
```

SDO is the backbone of CANopen network configuration and diagnostics. Its two transfer modes — **expedited** (single-frame, 1–4 bytes) and **segmented** (multi-frame with toggle-bit handshake, any size) — cover the full range of OD data sizes encountered in practice.

The **multiplexer** (Index + Sub-Index) provides a uniform addressing scheme across all object types. **Abort codes** in the 0x05xxxxxx and 0x06xxxxxx ranges cleanly differentiate protocol-level failures from object-access failures. A robust **timeout mechanism** (send abort 0x05040000 if no response within 1 s) prevents hung transfers from stalling the bus.

Implementing an SDO server as a **state machine** (IDLE → DOWNLOAD_SEGMENT or UPLOAD_SEGMENT → IDLE) is the standard approach on embedded targets: it maps exactly to the CiA 301 state diagram, handles the toggle bit deterministically, and cleanly integrates periodic timeout checks without requiring an RTOS or blocking waits.

---

*Reference: CiA 301 — CANopen application layer and communication profile, version 4.2.0.*