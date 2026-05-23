# 18. LSS — Layer Setting Services

**Structure of the document:**

1. **Overview** — purpose, CAN COB-IDs (0x7E5/0x7E4), 8-byte frame layout
2. **Master/Slave Roles** — ASCII diagram showing the bus topology
3. **LSS Address** — the 128-bit structure (Vendor-ID / Product / Revision / Serial from OD 0x1018), with a C struct
4. **Protocol Framing** — byte-level CAN frame layout in ASCII + full command specifier table
5. **Global vs. Selective Addressing** — ASCII sequence diagrams for both modes
6. **LSS State Machine** — ASCII state diagram (WAITING ↔ CONFIGURATION)
7. **Node-ID Assignment** — sequence diagram + valid range rules
8. **Baud Rate Setting** — two-step configure/activate with the standard baud table
9. **Store Configuration** — the "store" magic byte mechanism and error codes
10. **Fastscan Algorithm** — principle, command details, full ASCII sequence diagram, and bit-search walkthrough
11. **C/C++ Implementation** — complete slave handler, master API, full commissioning workflow example, and baud table helper
12. **Summary** — ASCII reference table + commissioning workflow checklist


## Table of Contents

1. [Overview](#1-overview)
2. [LSS Roles: Master and Slave](#2-lss-roles-master-and-slave)
3. [LSS Address Structure](#3-lss-address-structure)
4. [LSS Protocol Framing (CAN Messages)](#4-lss-protocol-framing-can-messages)
5. [Addressing Modes: Global vs. Selective](#5-addressing-modes-global-vs-selective)
6. [LSS State Machine](#6-lss-state-machine)
7. [Node-ID Assignment](#7-node-id-assignment)
8. [Baud Rate Setting](#8-baud-rate-setting)
9. [Store Configuration Command](#9-store-configuration-command)
10. [Fastscan Algorithm](#10-fastscan-algorithm)
11. [C/C++ Implementation](#11-cc-implementation)
12. [Summary](#12-summary)

---

## 1. Overview

**LSS (Layer Setting Services)** is defined in **CiA 305** and is part of the CANopen higher-layer protocol suite. It provides a mechanism for commissioning CAN nodes **before** they have a valid Node-ID (NID) assigned. Without LSS, every new device would need pre-configuration or a physical DIP-switch to set its Node-ID and baud rate.

LSS solves three critical bootstrapping problems:

- **Node-ID assignment** — nodes arrive from the factory with NID = 0xFF (invalid/unconfigured).
- **Baud rate setting** — the network baud rate must match; LSS allows remote configuration.
- **Identification** — each device carries a globally unique 128-bit address, enabling collision-free commissioning.

LSS uses **two dedicated CAN identifiers**:

```
Master → Slave :  COB-ID 0x7E5  (LSS request)
Slave  → Master:  COB-ID 0x7E4  (LSS response)
```

Both frames always carry exactly **8 bytes**. The first byte is the **LssCS** (Command Specifier), followed by up to 7 bytes of payload.

---

## 2. LSS Roles: Master and Slave

```
  +-----------------+          CAN Bus         +------------------+
  |   LSS Master    |<------------------------>|   LSS Slave #1   |
  | (Configurator)  |   COB-ID 0x7E5 (req)     | NID = 0xFF (new) |
  |                 |   COB-ID 0x7E4 (rsp)     +------------------+
  |  e.g. NMT Mstr  |                          +------------------+
  |  or dedicated   |<------------------------>|   LSS Slave #2   |
  |  commissioning  |                          | NID = 0xFF (new) |
  |  tool           |                          +------------------+
  +-----------------+                          +------------------+
                                               |   LSS Slave #3   |
                                               | NID = 3 (known)  |
                                               +------------------+
```

**LSS Master:**
- Initiates all LSS transactions.
- Sends switch-state, addressing, and configuration commands.
- Typically integrated into the NMT master or a standalone commissioning tool.
- Only **one** LSS master is permitted on the network at a time.

**LSS Slave:**
- Every CANopen device implementing LSS is an LSS slave.
- Responds only when selected or when a global broadcast applies.
- Maintains an internal LSS state machine.
- Stores NID and baud rate in non-volatile memory upon LSS "Store" command.

---

## 3. LSS Address Structure

Each LSS slave is uniquely identified by a **128-bit LSS address** composed of four 32-bit fields from its **CANopen Identity Object (0x1018)**:

```
 Bit 127              96  95              64  63              32  31               0
 +----------------------+------------------+------------------+-------------------+
 |     Vendor-ID        |   Product Code   | Revision Number  |   Serial Number   |
 |  (0x1018 sub 0x01)   | (0x1018 sub 02)  | (0x1018 sub 03)  | (0x1018 sub 04)   |
 +----------------------+------------------+------------------+-------------------+
       32 bits                32 bits             32 bits             32 bits
```

**Field descriptions:**

| Field           | Sub-index | Description                                              |
|-----------------|-----------|----------------------------------------------------------|
| Vendor-ID       | 0x01      | Assigned by CAN in Automation (CiA); unique per vendor.  |
| Product Code    | 0x02      | Vendor-defined product type identifier.                  |
| Revision Number | 0x03      | Major revision (high 16 bits) + minor revision (low 16). |
| Serial Number   | 0x04      | Unique per device unit (e.g. manufacturing serial).      |

The combination of all four fields is **guaranteed globally unique** (analogous to a MAC address). Even two identical products from the same vendor will differ in serial number.

**Example LSS address in C:**

```c
typedef struct {
    uint32_t vendor_id;       /* 0x1018:01 */
    uint32_t product_code;    /* 0x1018:02 */
    uint32_t revision_number; /* 0x1018:03 */
    uint32_t serial_number;   /* 0x1018:04 */
} lss_address_t;

/* Example: Vendor 0x00000123, Product 0x00000042,
            Revision 0x00010002, Serial 0xDEADBEEF */
static const lss_address_t my_lss_addr = {
    .vendor_id       = 0x00000123,
    .product_code    = 0x00000042,
    .revision_number = 0x00010002,
    .serial_number   = 0xDEADBEEF
};
```

---

## 4. LSS Protocol Framing (CAN Messages)

All LSS messages are 8-byte CAN data frames:

```
  Byte:  [0]       [1]       [2]       [3]       [4]       [5]       [6]       [7]
         +--------+---------+---------+---------+---------+---------+---------+--------+
  Field: | LssCS  |  Data0  |  Data1  |  Data2  |  Data3  |  Data4  |  Data5  | Data6  |
         +--------+---------+---------+---------+---------+---------+---------+--------+
              |
              +--- Command Specifier (1 byte) identifies the LSS service
```

**Important Command Specifiers (LssCS values):**

| LssCS (hex) | Service                                      | Direction        |
|-------------|----------------------------------------------|------------------|
| 0x04        | Switch State Global                          | Master → Slaves  |
| 0x40        | Switch State Selective (Vendor-ID)           | Master → Slaves  |
| 0x41        | Switch State Selective (Product Code)        | Master → Slaves  |
| 0x42        | Switch State Selective (Revision)            | Master → Slaves  |
| 0x43        | Switch State Selective (Serial)              | Master → Slaves  |
| 0x44        | Switch State Selective Response              | Slave  → Master  |
| 0x11        | Configure Node-ID                            | Master → Slave   |
| 0x11 (rsp)  | Configure Node-ID Response                   | Slave  → Master  |
| 0x13        | Configure Bit Timing Parameters              | Master → Slave   |
| 0x13 (rsp)  | Configure Bit Timing Response                | Slave  → Master  |
| 0x15        | Activate Bit Timing Parameters               | Master → Slaves  |
| 0x17        | Store Configuration                          | Master → Slave   |
| 0x17 (rsp)  | Store Configuration Response                 | Slave  → Master  |
| 0x4C        | LSS Identify Slave (Fastscan)                | Master → Slaves  |
| 0x4F        | LSS Identify Slave Response                  | Slave  → Master  |
| 0x4D        | LSS Identify Non-Configured Slaves           | Master → Slaves  |
| 0x50        | LSS Identify Non-Configured Response         | Slave  → Master  |
| 0x51        | Fastscan                                     | Master → Slaves  |
| 0x4E        | Fastscan Response                            | Slave  → Master  |

---

## 5. Addressing Modes: Global vs. Selective

LSS provides two ways to address slaves: **global** (broadcast) and **selective** (unicast via LSS address).

### 5.1 Global Addressing

A single command targets **all LSS slaves simultaneously**. Used primarily for switching all slaves to a new LSS state.

```
Master                                    All Slaves
  |                                           |
  |--- Switch State Global (LssCS=0x04) ----->|  mode=0x01 (waiting)
  |                                           |  ALL slaves enter LSS WAITING
  |                                           |
  |--- Switch State Global (LssCS=0x04) ----->|  mode=0x02 (config)
  |                                           |  ALL slaves enter LSS CONFIGURATION
  |                                           |
```

**No response** is sent by slaves for global commands (avoids bus flooding).

### 5.2 Selective Addressing

Four sequential CAN frames transmit the four 32-bit address fields. Only the slave whose LSS address **exactly matches all four fields** enters the CONFIGURATION state and responds.

```
Master                                    Slave (matching)    Slave (non-matching)
  |                                           |                     |
  |-- SwitchSelective VendorID (CS=0x40) ---->|                     |
  |-- SwitchSelective ProductCode (CS=0x41)-> |                     |
  |-- SwitchSelective Revision (CS=0x42) ---->|                     |
  |-- SwitchSelective Serial (CS=0x43) ------>|                     |
  |                                           |                     |
  |<-- SelectiveResponse (CS=0x44) -----------|    (no response)    |
  |                                           |                     |
  |   [Only matching slave is now in          |                     |
  |    CONFIGURATION state]                   |                     |
```

A slave collects all four frames sequentially. Only after all four are received and matched does it respond and enter CONFIGURATION mode.

---

## 6. LSS State Machine

Each LSS slave implements the following state machine:

```
                        Power-On / Reset
                              |
                              v
              +-------------------------------+
              |          WAITING              |<---------+
              |  (LSS_STATE_WAITING)          |          |
              |  - NID may be 0xFF (uncfg)    |          |
              |  - Participates in Fastscan   |          |
              +-------------------------------+          |
                   |              ^                      |
    Global/Sel.    |              | Global (mode=0x00)   | Global
    (mode=0x02)    |              | or Identify OK fails | (mode=0x00)
                   v              |                      |
              +-------------------------------+          |
              |       CONFIGURATION           |          |
              |  (LSS_STATE_CONFIG)           |----------+
              |  - Accepts NID, baud rate     |  Error or
              |  - Accepts Store cmd          |  Identify fails
              |  - One slave at a time        |
              +-------------------------------+
```

**State transitions:**

| From          | Trigger                                 | To            |
|---------------|-----------------------------------------|---------------|
| WAITING       | Switch State Global (mode=CONFIGURATION)| CONFIGURATION |
| WAITING       | Switch State Selective (address match)  | CONFIGURATION |
| CONFIGURATION | Switch State Global (mode=WAITING)      | WAITING       |
| CONFIGURATION | Switch State Global (mode=WAITING)      | WAITING       |
| Any           | Reset Node / Power cycle                | WAITING       |

---

## 7. Node-ID Assignment

Once a slave is in CONFIGURATION state, the master assigns a Node-ID:

```
Master                              Slave (in CONFIGURATION)
  |                                       |
  |-- Configure Node-ID (CS=0x11) ------->|
  |   [Byte1 = new_node_id]               |
  |   [Bytes 2-7 = 0x00]                  |
  |                                       |
  |<-- Configure Node-ID Response --------|
  |   [Byte1 = error_code]                |
  |   [Byte2 = error_spec (if error)]     |
  |   error_code: 0x00 = success          |
  |               0x01 = out of range     |
  |               0xFF = impl-specific    |
  |                                       |
  |-- Store Configuration (CS=0x17) ----->|  (optional, makes NID permanent)
  |<-- Store Response --------------------|
```

**Valid Node-ID range:** 1–127 (0x01–0x7F).
- 0x00 = reserved (no NID configured yet, node stays unconfigured).
- 0xFF = invalid / factory default.

After a Node-ID is configured but before `Store Configuration` is sent, the new NID is held in **volatile memory** only. A power cycle would revert to the old (or unconfigured) NID.

---

## 8. Baud Rate Setting

LSS allows remote baud rate configuration using a two-step process: **configure** then **activate**.

### Step 1: Configure Bit Timing

```
Master                              Slave (in CONFIGURATION)
  |                                       |
  |-- Configure Bit Timing (CS=0x13) ---->|
  |   [Byte1 = table_selector]            |
  |   [Byte2 = table_index]               |
  |                                       |
  |<-- Bit Timing Response (CS=0x13) -----|
  |   [Byte1 = error_code]                |
```

**Standard Bit Timing Table (table_selector = 0):**

| Index | Baud Rate |
|-------|-----------|
| 0     | 1 Mbit/s  |
| 1     | 800 kbit/s|
| 2     | 500 kbit/s|
| 3     | 250 kbit/s|
| 4     | 125 kbit/s|
| 5     | 100 kbit/s|
| 6     | 50 kbit/s |
| 7     | 20 kbit/s |
| 8     | 10 kbit/s |
| 9     | Auto      |

### Step 2: Activate Bit Timing (Global Broadcast)

```
Master                              All Slaves
  |                                       |
  |-- Activate Bit Timing (CS=0x15) ----->|
  |   [Byte1..2 = switch_delay_ms LSB]    |
  |   [Byte3..4 = switch_delay_ms MSB]    |
  |                                       |
  |   [Master waits switch_delay_ms]      |
  |                                       |
  |   [Master switches own baud rate]     |
  |                                       |
  |   [Both sides now at new baud rate]   |
```

The `switch_delay` (in milliseconds) ensures all nodes on the bus have time to complete any ongoing transmissions before the baud rate changes. Typical values: 100–500 ms.

> **Important:** `Activate Bit Timing` is a **global** command. All slaves receiving it will switch, regardless of their current LSS state. Store the new baud rate with the `Store Configuration` command to make it persistent.

---

## 9. Store Configuration Command

The Store Configuration command commits the current NID and baud rate to **non-volatile memory** (e.g. EEPROM, Flash):

```
Master                              Slave (in CONFIGURATION)
  |                                       |
  |-- Store Configuration (CS=0x17) ----->|
  |   [Byte1 = 0x73 ('s')]                |
  |   [Byte2 = 0x74 ('t')]                |
  |   [Byte3 = 0x6F ('o')]                |
  |   [Byte4 = 0x72 ('r')]                |
  |   [Byte5 = 0x65 ('e')]  ("store")     |
  |   [Bytes 6-7 = 0x00]                  |
  |                                       |
  |<-- Store Response (CS=0x17) ----------|
  |   [Byte1 = error_code]                |
  |   error_code: 0x00 = success          |
  |               0x01 = not supported    |
  |               0x02 = access error     |
```

The ASCII signature bytes `"store"` (0x73, 0x74, 0x6F, 0x72, 0x65) act as a safeguard against accidental writes. After a successful Store command, the slave retains the new NID and baud rate across power cycles.

---

## 10. Fastscan Algorithm

**Fastscan** (defined in CiA 305, revision ≥ 3.0) is an efficient binary-search algorithm for automatically discovering and assigning Node-IDs to **multiple unconfigured slaves** without knowing their LSS addresses in advance.

### 10.1 Principle

Fastscan divides the 128-bit LSS address space using a **binary search** approach, working through each of the four 32-bit fields sequentially.

For each field, the master queries with a value and a bit-mask. Slaves whose address field matches the masked bits respond. The master narrows the search by incrementally fixing more bits until a single slave is isolated.

### 10.2 Fastscan Commands

```
LssCS = 0x4D  : Identify Non-Configured Slaves
  -> Response 0x50 from any unconfigured slave confirms unconfirmed nodes exist

LssCS = 0x51  : Fastscan
  Byte[1..4]  : IDNumber  (32-bit value to match)
  Byte[5]     : BitChecked (number of bits checked, 0–31)
  Byte[6]     : LSSSub    (which field: 0=VendorID, 1=Product, 2=Revision, 3=Serial)
  Byte[7]     : LSSNext   (which field to check next if match found)

LssCS = 0x4E  : Fastscan Response (slave responds if it matches)
```

### 10.3 Fastscan Sequence Diagram

```
Master                                  Unconfigured Slaves (A, B, C)
  |                                        |          |          |
  |--- Identify Non-Configured (0x4D) ---->|          |          |
  |<-- Response (0x50) --------------------|<---------|<---------|
  |   (at least one unconfigured slave     |          |          |
  |    exists, possibly multiple respond)  |          |          |
  |                                        |          |          |
  |--- Fastscan(VendorID=0,bits=0,         |          |          |
  |            sub=0,next=0) (0x51) ------>|          |          |
  |<-- Fastscan Response (0x4E) -----------|<---------|<---------| all respond
  |   (all 3 slaves match, bits=0          |          |          |
  |    means "match any value")            |          |          |
  |                                        |          |          |
  |   [Binary search narrows VendorID...]  |          |          |
  |   [Eventually VendorID fully matched]  |          |          |
  |                                        |          |          |
  |--- Fastscan(ProductCode=X, sub=1) ---->|          |          |
  |<-- Response --------------------------->          |          |
  |                                        |          |          |
  |   [Continues through Revision,         |          |          |
  |    Serial until single slave found]    |          |          |
  |                                        |          |          |
  |   [Slave A uniquely identified]        |          |          |
  |--- Configure Node-ID (0x11) NID=3 ---->|          |          |
  |--- Store Configuration (0x17) -------->|          |          |
  |--- Switch State Global WAITING ------->|          |          |
  |                                        |          |          |
  |   [Repeat entire process for B, C...]  |          |          |
```

### 10.4 Fastscan Bit-Search Detail

```
Searching 32-bit Vendor-ID field using BitChecked:

Step 1: BitChecked=0  -> match ANY value (all slaves respond)
         IDNumber=0x00000000, mask covers 0 bits (all pass)

Step 2: BitChecked=1  -> test MSB
         IDNumber=0x80000000 -> only slaves with bit31=1 respond
         If response: upper half confirmed. Else: lower half.

Step 3: BitChecked=2  -> test next bit
         IDNumber=0xC0000000 or 0x40000000 (based on step 2 result)

...continue until BitChecked=32 (all 32 bits confirmed)...

At BitChecked=32: Vendor-ID fully determined.
Move to next field (LSSSub++), reset BitChecked=0.
```

This requires at most **32 iterations per field × 4 fields = 128 CAN frames** per slave in the worst case.

---

## 11. C/C++ Implementation

### 11.1 Data Structures

```c
/* lss.h */
#ifndef LSS_H
#define LSS_H

#include <stdint.h>
#include <stdbool.h>

/* LSS COB-IDs */
#define LSS_TX_COBID   0x7E5U  /* Master -> Slave  */
#define LSS_RX_COBID   0x7E4U  /* Slave  -> Master */

/* LSS Command Specifiers */
#define LSS_CS_SWITCH_GLOBAL         0x04U
#define LSS_CS_SWITCH_SEL_VENDOR     0x40U
#define LSS_CS_SWITCH_SEL_PRODUCT    0x41U
#define LSS_CS_SWITCH_SEL_REVISION   0x42U
#define LSS_CS_SWITCH_SEL_SERIAL     0x43U
#define LSS_CS_SWITCH_SEL_RESPONSE   0x44U
#define LSS_CS_CONF_NODE_ID          0x11U
#define LSS_CS_CONF_BAUD_RATE        0x13U
#define LSS_CS_ACTIVATE_BAUD_RATE    0x15U
#define LSS_CS_STORE_CONFIG          0x17U
#define LSS_CS_IDENT_SLAVE           0x4CU
#define LSS_CS_IDENT_RESPONSE        0x4FU
#define LSS_CS_IDENT_NON_CONF        0x4DU
#define LSS_CS_IDENT_NON_CONF_RSP    0x50U
#define LSS_CS_FASTSCAN              0x51U
#define LSS_CS_FASTSCAN_RESPONSE     0x4EU

/* LSS Modes for Switch State Global */
#define LSS_MODE_WAITING             0x00U
#define LSS_MODE_CONFIGURATION       0x01U

/* LSS Slave states */
typedef enum {
    LSS_STATE_WAITING       = 0,
    LSS_STATE_CONFIGURATION = 1
} lss_state_t;

/* 128-bit LSS address (from Object 0x1018) */
typedef struct {
    uint32_t vendor_id;
    uint32_t product_code;
    uint32_t revision_number;
    uint32_t serial_number;
} lss_address_t;

/* Generic 8-byte CAN frame for LSS */
typedef struct {
    uint32_t cob_id;
    uint8_t  data[8];
    uint8_t  dlc;
} can_frame_t;

/* Error codes in LSS responses */
#define LSS_ERR_OK            0x00U
#define LSS_ERR_OUT_OF_RANGE  0x01U
#define LSS_ERR_NOT_SUPPORTED 0x01U
#define LSS_ERR_ACCESS_ERROR  0x02U

#endif /* LSS_H */
```

### 11.2 LSS Slave Implementation

```c
/* lss_slave.c */
#include "lss.h"
#include "can_driver.h"   /* platform-specific CAN send/receive */
#include "nvm.h"          /* non-volatile storage: nvm_write(), nvm_read() */
#include <string.h>

/* ------------------------------------------------------------------ */
/* Device identity — stored in ROM / read from Object Dictionary       */
/* ------------------------------------------------------------------ */
static const lss_address_t g_lss_addr = {
    .vendor_id       = 0x00000123U,
    .product_code    = 0x00000042U,
    .revision_number = 0x00010002U,
    .serial_number   = 0xDEADBEEFU
};

/* Current runtime state */
static lss_state_t   g_lss_state   = LSS_STATE_WAITING;
static uint8_t       g_node_id     = 0xFFU; /* 0xFF = unconfigured */
static uint8_t       g_baud_index  = 0xFFU; /* 0xFF = not set via LSS */

/* Selective addressing: track partial match progress */
static uint8_t  g_sel_step = 0;  /* 0–3: which field we're waiting for */

/* ------------------------------------------------------------------ */
/* Helper: send an 8-byte LSS response frame                          */
/* ------------------------------------------------------------------ */
static void lss_send_response(uint8_t cs,
                               uint8_t b1, uint8_t b2, uint8_t b3,
                               uint8_t b4, uint8_t b5, uint8_t b6,
                               uint8_t b7)
{
    can_frame_t f;
    f.cob_id  = LSS_RX_COBID;
    f.dlc     = 8;
    f.data[0] = cs;
    f.data[1] = b1; f.data[2] = b2; f.data[3] = b3; f.data[4] = b4;
    f.data[5] = b5; f.data[6] = b6; f.data[7] = b7;
    can_send(&f);
}

/* ------------------------------------------------------------------ */
/* Handle incoming LSS frame (called from CAN RX ISR or task)         */
/* ------------------------------------------------------------------ */
void lss_slave_process(const can_frame_t *f)
{
    if (f->cob_id != LSS_TX_COBID || f->dlc != 8)
        return;

    uint8_t cs = f->data[0];

    switch (cs) {

    /* ---- Switch State Global ---------------------------------------- */
    case LSS_CS_SWITCH_GLOBAL:
        if (f->data[1] == LSS_MODE_CONFIGURATION) {
            g_lss_state = LSS_STATE_CONFIGURATION;
            g_sel_step  = 0;
        } else {
            g_lss_state = LSS_STATE_WAITING;
        }
        break;

    /* ---- Switch State Selective (4 sequential frames) --------------- */
    case LSS_CS_SWITCH_SEL_VENDOR:
        if (g_lss_state != LSS_STATE_WAITING) break;
        {
            uint32_t v;
            memcpy(&v, &f->data[1], 4); /* little-endian per CiA 305 */
            if (v == g_lss_addr.vendor_id)
                g_sel_step = 1;
            else
                g_sel_step = 0;
        }
        break;

    case LSS_CS_SWITCH_SEL_PRODUCT:
        if (g_sel_step != 1) break;
        {
            uint32_t v;
            memcpy(&v, &f->data[1], 4);
            if (v == g_lss_addr.product_code)
                g_sel_step = 2;
            else
                g_sel_step = 0;
        }
        break;

    case LSS_CS_SWITCH_SEL_REVISION:
        if (g_sel_step != 2) break;
        {
            uint32_t v;
            memcpy(&v, &f->data[1], 4);
            if (v == g_lss_addr.revision_number)
                g_sel_step = 3;
            else
                g_sel_step = 0;
        }
        break;

    case LSS_CS_SWITCH_SEL_SERIAL:
        if (g_sel_step != 3) break;
        {
            uint32_t v;
            memcpy(&v, &f->data[1], 4);
            if (v == g_lss_addr.serial_number) {
                g_lss_state = LSS_STATE_CONFIGURATION;
                g_sel_step  = 0;
                /* Send selective response */
                lss_send_response(LSS_CS_SWITCH_SEL_RESPONSE,
                                  0,0,0,0,0,0,0);
            } else {
                g_sel_step = 0;
            }
        }
        break;

    /* ---- Configure Node-ID ------------------------------------------ */
    case LSS_CS_CONF_NODE_ID:
        if (g_lss_state != LSS_STATE_CONFIGURATION) break;
        {
            uint8_t nid = f->data[1];
            if (nid < 1 || nid > 127) {
                lss_send_response(LSS_CS_CONF_NODE_ID,
                                  LSS_ERR_OUT_OF_RANGE, 0,0,0,0,0,0);
            } else {
                g_node_id = nid;
                lss_send_response(LSS_CS_CONF_NODE_ID,
                                  LSS_ERR_OK, 0,0,0,0,0,0);
            }
        }
        break;

    /* ---- Configure Bit Timing --------------------------------------- */
    case LSS_CS_CONF_BAUD_RATE:
        if (g_lss_state != LSS_STATE_CONFIGURATION) break;
        {
            uint8_t tbl_sel = f->data[1];
            uint8_t tbl_idx = f->data[2];
            if (tbl_sel == 0 && tbl_idx <= 9) {
                g_baud_index = tbl_idx; /* staged, not applied yet */
                lss_send_response(LSS_CS_CONF_BAUD_RATE,
                                  LSS_ERR_OK, 0,0,0,0,0,0);
            } else {
                lss_send_response(LSS_CS_CONF_BAUD_RATE,
                                  LSS_ERR_NOT_SUPPORTED, 0,0,0,0,0,0);
            }
        }
        break;

    /* ---- Activate Bit Timing (global, no response) ------------------ */
    case LSS_CS_ACTIVATE_BAUD_RATE:
        if (g_baud_index <= 9) {
            uint16_t delay_ms;
            memcpy(&delay_ms, &f->data[1], 2);
            /* Platform: wait delay_ms, then switch baud rate */
            can_schedule_baud_switch(g_baud_index, delay_ms);
        }
        break;

    /* ---- Store Configuration ---------------------------------------- */
    case LSS_CS_STORE_CONFIG:
        if (g_lss_state != LSS_STATE_CONFIGURATION) break;
        {
            /* Verify magic signature: "store" = 0x73,0x74,0x6F,0x72,0x65 */
            static const uint8_t magic[5] = {0x73,0x74,0x6F,0x72,0x65};
            if (memcmp(&f->data[1], magic, 5) == 0) {
                /* Write NID and baud rate to NVM */
                nvm_write(NVM_KEY_NODE_ID,    &g_node_id,    1);
                nvm_write(NVM_KEY_BAUD_INDEX, &g_baud_index, 1);
                lss_send_response(LSS_CS_STORE_CONFIG,
                                  LSS_ERR_OK, 0,0,0,0,0,0);
            } else {
                lss_send_response(LSS_CS_STORE_CONFIG,
                                  LSS_ERR_ACCESS_ERROR, 0,0,0,0,0,0);
            }
        }
        break;

    /* ---- Identify Non-Configured Slaves ----------------------------- */
    case LSS_CS_IDENT_NON_CONF:
        if (g_node_id == 0xFFU) {
            lss_send_response(LSS_CS_IDENT_NON_CONF_RSP,
                              0,0,0,0,0,0,0);
        }
        break;

    /* ---- Fastscan --------------------------------------------------- */
    case LSS_CS_FASTSCAN:
        {
            uint32_t id_number;
            uint8_t  bit_checked = f->data[5];
            uint8_t  lss_sub     = f->data[6];
            uint8_t  lss_next    = f->data[7];

            memcpy(&id_number, &f->data[1], 4);

            /* Only unconfigured slaves participate */
            if (g_node_id != 0xFFU) break;

            /* Get our address field for lss_sub */
            const uint32_t *fields[4] = {
                &g_lss_addr.vendor_id,
                &g_lss_addr.product_code,
                &g_lss_addr.revision_number,
                &g_lss_addr.serial_number
            };
            if (lss_sub > 3) break;
            uint32_t my_field = *fields[lss_sub];

            /* Compare: only bits [bit_checked-1 .. 31] are compared */
            /* bit_checked=0 means "match all" */
            uint32_t mask = (bit_checked == 0) ? 0 :
                            (0xFFFFFFFFU << (32 - bit_checked));
            if ((my_field & mask) == (id_number & mask)) {
                if (bit_checked == 32 && lss_next == 0) {
                    /* Fully identified: enter CONFIGURATION state */
                    g_lss_state = LSS_STATE_CONFIGURATION;
                }
                lss_send_response(LSS_CS_FASTSCAN_RESPONSE,
                                  0,0,0,0,0,0,0);
            }
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Initialization: restore NID and baud rate from NVM                 */
/* ------------------------------------------------------------------ */
void lss_slave_init(void)
{
    nvm_read(NVM_KEY_NODE_ID,    &g_node_id,    1);
    nvm_read(NVM_KEY_BAUD_INDEX, &g_baud_index, 1);
    g_lss_state = LSS_STATE_WAITING;
    g_sel_step  = 0;
}
```

### 11.3 LSS Master Implementation

```c
/* lss_master.c */
#include "lss.h"
#include "can_driver.h"
#include <string.h>

#define LSS_TIMEOUT_MS   100   /* Response timeout in milliseconds */

/* ------------------------------------------------------------------ */
/* Helper: send an LSS request frame                                  */
/* ------------------------------------------------------------------ */
static void lss_send(uint8_t cs,
                     uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4,
                     uint8_t b5, uint8_t b6, uint8_t b7)
{
    can_frame_t f;
    f.cob_id  = LSS_TX_COBID;
    f.dlc     = 8;
    f.data[0] = cs;
    f.data[1] = b1; f.data[2] = b2; f.data[3] = b3; f.data[4] = b4;
    f.data[5] = b5; f.data[6] = b6; f.data[7] = b7;
    can_send(&f);
}

/* ------------------------------------------------------------------ */
/* Helper: wait for a specific LSS response CS                        */
/* Returns true on success, false on timeout                          */
/* ------------------------------------------------------------------ */
static bool lss_wait_response(uint8_t expected_cs, can_frame_t *out,
                               uint32_t timeout_ms)
{
    uint32_t deadline = timer_now_ms() + timeout_ms;
    while (timer_now_ms() < deadline) {
        can_frame_t f;
        if (can_receive_filtered(LSS_RX_COBID, &f)) {
            if (f.data[0] == expected_cs) {
                if (out) *out = f;
                return true;
            }
        }
    }
    return false; /* timeout */
}

/* ------------------------------------------------------------------ */
/* Switch all slaves to CONFIGURATION mode (global)                   */
/* ------------------------------------------------------------------ */
void lss_master_switch_global_config(void)
{
    lss_send(LSS_CS_SWITCH_GLOBAL, LSS_MODE_CONFIGURATION, 0,0,0,0,0,0);
}

/* ------------------------------------------------------------------ */
/* Switch all slaves back to WAITING mode (global)                    */
/* ------------------------------------------------------------------ */
void lss_master_switch_global_wait(void)
{
    lss_send(LSS_CS_SWITCH_GLOBAL, LSS_MODE_WAITING, 0,0,0,0,0,0);
}

/* ------------------------------------------------------------------ */
/* Selective address: put one slave into CONFIGURATION state          */
/* Returns true if slave responded                                    */
/* ------------------------------------------------------------------ */
bool lss_master_select_slave(const lss_address_t *addr)
{
    uint8_t buf[4];

    memcpy(buf, &addr->vendor_id, 4);
    lss_send(LSS_CS_SWITCH_SEL_VENDOR,
             buf[0],buf[1],buf[2],buf[3], 0,0,0);

    memcpy(buf, &addr->product_code, 4);
    lss_send(LSS_CS_SWITCH_SEL_PRODUCT,
             buf[0],buf[1],buf[2],buf[3], 0,0,0);

    memcpy(buf, &addr->revision_number, 4);
    lss_send(LSS_CS_SWITCH_SEL_REVISION,
             buf[0],buf[1],buf[2],buf[3], 0,0,0);

    memcpy(buf, &addr->serial_number, 4);
    lss_send(LSS_CS_SWITCH_SEL_SERIAL,
             buf[0],buf[1],buf[2],buf[3], 0,0,0);

    return lss_wait_response(LSS_CS_SWITCH_SEL_RESPONSE, NULL,
                              LSS_TIMEOUT_MS);
}

/* ------------------------------------------------------------------ */
/* Configure Node-ID for the currently selected slave                 */
/* ------------------------------------------------------------------ */
bool lss_master_set_node_id(uint8_t node_id, uint8_t *err_out)
{
    can_frame_t rsp;
    lss_send(LSS_CS_CONF_NODE_ID, node_id, 0,0,0,0,0,0);
    if (!lss_wait_response(LSS_CS_CONF_NODE_ID, &rsp, LSS_TIMEOUT_MS))
        return false;
    if (err_out) *err_out = rsp.data[1];
    return (rsp.data[1] == LSS_ERR_OK);
}

/* ------------------------------------------------------------------ */
/* Configure baud rate for the currently selected slave               */
/* ------------------------------------------------------------------ */
bool lss_master_set_baud_rate(uint8_t baud_index, uint8_t *err_out)
{
    can_frame_t rsp;
    lss_send(LSS_CS_CONF_BAUD_RATE, 0, baud_index, 0,0,0,0,0);
    if (!lss_wait_response(LSS_CS_CONF_BAUD_RATE, &rsp, LSS_TIMEOUT_MS))
        return false;
    if (err_out) *err_out = rsp.data[1];
    return (rsp.data[1] == LSS_ERR_OK);
}

/* ------------------------------------------------------------------ */
/* Activate new baud rate on all slaves (global, no ACK)              */
/* switch_delay_ms: time for slaves to prepare                        */
/* ------------------------------------------------------------------ */
void lss_master_activate_baud_rate(uint16_t switch_delay_ms)
{
    uint8_t lo = (uint8_t)(switch_delay_ms & 0xFF);
    uint8_t hi = (uint8_t)(switch_delay_ms >> 8);
    lss_send(LSS_CS_ACTIVATE_BAUD_RATE, lo, hi, 0,0,0,0,0);
    /* Master must also switch its own baud rate after the delay */
    timer_delay_ms(switch_delay_ms);
    can_set_baud_rate_index(/* new baud from context */ 0);
}

/* ------------------------------------------------------------------ */
/* Store configuration (NID + baud rate) to NVM in selected slave     */
/* ------------------------------------------------------------------ */
bool lss_master_store_config(uint8_t *err_out)
{
    can_frame_t rsp;
    /* Signature: "store" = 0x73 0x74 0x6F 0x72 0x65 */
    lss_send(LSS_CS_STORE_CONFIG, 0x73, 0x74, 0x6F, 0x72, 0x65, 0, 0);
    if (!lss_wait_response(LSS_CS_STORE_CONFIG, &rsp, LSS_TIMEOUT_MS))
        return false;
    if (err_out) *err_out = rsp.data[1];
    return (rsp.data[1] == LSS_ERR_OK);
}

/* ------------------------------------------------------------------ */
/* Check if any unconfigured slave exists on the bus                  */
/* ------------------------------------------------------------------ */
bool lss_master_has_unconfigured(void)
{
    lss_send(LSS_CS_IDENT_NON_CONF, 0,0,0,0,0,0,0);
    return lss_wait_response(LSS_CS_IDENT_NON_CONF_RSP, NULL,
                              LSS_TIMEOUT_MS);
}

/* ------------------------------------------------------------------ */
/* Fastscan: discover one unconfigured slave and isolate it           */
/* On success, the slave is in CONFIGURATION state and addr is filled */
/* Returns true if a slave was found and isolated                     */
/* ------------------------------------------------------------------ */
bool lss_master_fastscan_isolate(lss_address_t *found_addr)
{
    uint32_t fields[4] = {0, 0, 0, 0};
    can_frame_t rsp;
    uint8_t buf[4];

    for (uint8_t lss_sub = 0; lss_sub < 4; lss_sub++) {
        uint32_t id_number = 0;

        for (uint8_t bit_checked = 0; bit_checked <= 32; bit_checked++) {

            uint8_t lss_next = (bit_checked == 32) ?
                               ((lss_sub == 3) ? 0 : lss_sub + 1) :
                               lss_sub;

            memcpy(buf, &id_number, 4);
            lss_send(LSS_CS_FASTSCAN,
                     buf[0], buf[1], buf[2], buf[3],
                     bit_checked, lss_sub, lss_next);

            bool got_rsp = lss_wait_response(LSS_CS_FASTSCAN_RESPONSE,
                                             &rsp, LSS_TIMEOUT_MS);

            if (!got_rsp) {
                if (bit_checked == 0) return false; /* no slave */
                /* Flip the last tested bit: this half is empty */
                id_number ^= (1U << (31 - (bit_checked - 1)));
                /* Re-send for this bit_checked to confirm other half */
                memcpy(buf, &id_number, 4);
                lss_send(LSS_CS_FASTSCAN,
                         buf[0], buf[1], buf[2], buf[3],
                         bit_checked, lss_sub, lss_next);
                if (!lss_wait_response(LSS_CS_FASTSCAN_RESPONSE, &rsp,
                                       LSS_TIMEOUT_MS))
                    return false; /* neither half responds */
            }

            if (bit_checked < 32) {
                /* Extend by one bit: try setting the next bit to 1 */
                id_number |= (1U << (31 - bit_checked));
            }
        }
        fields[lss_sub] = id_number;
    }

    /* Slave is now in CONFIGURATION state */
    found_addr->vendor_id       = fields[0];
    found_addr->product_code    = fields[1];
    found_addr->revision_number = fields[2];
    found_addr->serial_number   = fields[3];
    return true;
}
```

### 11.4 Complete Commissioning Workflow Example

```c
/* commission.c — Full network commissioning using LSS Fastscan */
#include "lss.h"
#include "lss_master.h"
#include <stdio.h>

/* Node-ID assignment table: maps LSS address -> desired Node-ID      */
typedef struct {
    lss_address_t addr;
    uint8_t       desired_nid;
} nid_assignment_t;

static const nid_assignment_t g_assignments[] = {
    {{ 0x123, 0x42, 0x10002, 0xDEADBEEF }, 3 },
    {{ 0x123, 0x42, 0x10002, 0xCAFEBABE }, 4 },
    {{ 0x123, 0x43, 0x10001, 0x12345678 }, 5 },
};

void commission_network(void)
{
    uint8_t err;
    int nodes_found = 0;

    printf("LSS Commissioning started...\n");

    /*
     * Strategy A: Use Fastscan for fully unknown nodes
     */
    while (lss_master_has_unconfigured()) {

        lss_address_t found;
        if (!lss_master_fastscan_isolate(&found)) {
            printf("Fastscan failed to isolate a slave.\n");
            break;
        }

        printf("Found slave: VID=%08X PC=%08X REV=%08X SN=%08X\n",
               found.vendor_id, found.product_code,
               found.revision_number, found.serial_number);

        /* Look up desired NID */
        uint8_t nid = 0xFF;
        for (size_t i = 0;
             i < sizeof(g_assignments)/sizeof(g_assignments[0]); i++) {
            const lss_address_t *a = &g_assignments[i].addr;
            if (a->vendor_id       == found.vendor_id       &&
                a->product_code    == found.product_code    &&
                a->revision_number == found.revision_number &&
                a->serial_number   == found.serial_number) {
                nid = g_assignments[i].desired_nid;
                break;
            }
        }

        if (nid == 0xFF) {
            printf("  Unknown slave — skipping.\n");
            lss_master_switch_global_wait();
            continue;
        }

        /* Assign NID */
        if (!lss_master_set_node_id(nid, &err)) {
            printf("  Set NID failed (err=%02X)\n", err);
        } else {
            printf("  Assigned NID=%d\n", nid);
        }

        /* Store permanently */
        if (!lss_master_store_config(&err)) {
            printf("  Store config failed (err=%02X)\n", err);
        } else {
            printf("  Configuration stored.\n");
        }

        /* Return slave to WAITING */
        lss_master_switch_global_wait();
        nodes_found++;
    }

    /*
     * Strategy B: Selective addressing for known devices
     */
    static const lss_address_t known = {
        0x00000456U, 0x00000010U, 0x00020001U, 0xABCD1234U
    };

    if (lss_master_select_slave(&known)) {
        lss_master_set_node_id(10, &err);
        lss_master_store_config(&err);
        lss_master_switch_global_wait();
        printf("Known slave commissioned as NID=10\n");
    }

    printf("Commissioning complete. %d nodes auto-assigned.\n",
           nodes_found);
}
```

### 11.5 LSS Baud Rate Table Helper

```c
/* Baud rate index -> actual bps lookup */
static const uint32_t lss_baud_table[] = {
    1000000UL,  /* index 0: 1 Mbit/s   */
     800000UL,  /* index 1: 800 kbit/s */
     500000UL,  /* index 2: 500 kbit/s */
     250000UL,  /* index 3: 250 kbit/s */
     125000UL,  /* index 4: 125 kbit/s */
     100000UL,  /* index 5: 100 kbit/s */
      50000UL,  /* index 6:  50 kbit/s */
      20000UL,  /* index 7:  20 kbit/s */
      10000UL,  /* index 8:  10 kbit/s */
          0UL,  /* index 9: auto-detect */
};

uint32_t lss_baud_to_bps(uint8_t index)
{
    if (index >= 9) return 0; /* auto or invalid */
    return lss_baud_table[index];
}
```

---

## 12. Summary

LSS (Layer Setting Services, CiA 305) solves the fundamental bootstrapping problem in CANopen networks: how to assign Node-IDs and baud rates to devices that arrive unconfigured from the factory.

**Core concepts at a glance:**

```
+-------------------+------------------------------------------------+
| Concept           | Key Details                                    |
+-------------------+------------------------------------------------+
| LSS Address       | 128-bit: Vendor-ID + Product + Revision +      |
|                   | Serial (from OD 0x1018). Globally unique.      |
+-------------------+------------------------------------------------+
| CAN Identifiers   | Request:  COB-ID 0x7E5 (Master→Slave)          |
|                   | Response: COB-ID 0x7E4 (Slave→Master)          |
|                   | Always 8 bytes, byte[0] = Command Specifier    |
+-------------------+------------------------------------------------+
| Slave States      | WAITING → CONFIGURATION (on select/global)     |
|                   | CONFIGURATION → WAITING (on global or done)    |
+-------------------+------------------------------------------------+
| Global Addressing | Targets ALL slaves simultaneously.             |
|                   | No response. Used to broadcast mode changes    |
|                   | and baud rate activation.                      |
+-------------------+------------------------------------------------+
| Selective Addr.   | 4 sequential frames with address fields.       |
|                   | Only matching slave responds + enters CONFIG.  |
+-------------------+------------------------------------------------+
| Node-ID Setting   | CS=0x11, valid range 1–127.                    |
|                   | Volatile until Store Configuration is called.  |
+-------------------+------------------------------------------------+
| Baud Rate Setting | CS=0x13 (configure, 2-step):                   |
|                   | 1. Set baud index (staged, not yet active).    |
|                   | 2. Activate (CS=0x15, global broadcast) with   |
|                   |    switch_delay_ms for safe transition.        |
+-------------------+------------------------------------------------+
| Store Config      | CS=0x17 + magic "store" signature bytes.       |
|                   | Writes NID + baud rate to NVM (EEPROM/Flash).  |
+-------------------+------------------------------------------------+
| Fastscan          | CS=0x51. Binary search over 128-bit address.   |
|                   | Discovers and isolates unknown slaves without  |
|                   | prior knowledge. ≤128 CAN frames per slave.    |
|                   | Key params: IDNumber, BitChecked, LSSSub,      |
|                   | LSSNext.                                       |
+-------------------+------------------------------------------------+
```

**Typical commissioning workflow:**

```
1. Power-on: all new nodes have NID=0xFF, LSS state = WAITING
2. Master: lss_master_has_unconfigured()
        -> slaves respond with CS=0x50 if unconfigured
3. Master: lss_master_fastscan_isolate(&found_addr)
        -> one slave enters CONFIGURATION state
4. Master: lss_master_set_node_id(desired_nid, &err)
5. Master: lss_master_store_config(&err)
        -> NID saved to NVM
6. Master: lss_master_switch_global_wait()
        -> slave returns to WAITING with new NID
7. Repeat steps 2–6 until no unconfigured slaves remain
8. Optional: lss_master_set_baud_rate() + lss_master_activate_baud_rate()
        -> all nodes switch to new baud rate
```

**Key design considerations:**

- Only **one LSS master** must operate on the network at a time.
- `Activate Bit Timing` is **global** — it affects every slave simultaneously, so the master must also switch its baud rate after the configured delay.
- The `Store Configuration` "store" magic bytes are a deliberate safeguard against accidental NVM writes.
- Fastscan is strictly more efficient than polling known addresses sequentially; for large networks with many unknown devices it is the recommended approach.
- Nodes retain their last configured NID and baud rate in NVM. Re-commissioning requires either a selective addressing sequence or a global switch to CONFIGURATION (which would expose all nodes simultaneously — use with care).

---

*Reference: CiA 305 — CANopen Layer Setting Services (LSS) and Protocols, version 3.0.0 and later.*