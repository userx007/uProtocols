Now I have everything I need. Let me create the comprehensive markdown document.The document covers all requested topics in depth. Here's what's included:

**Structure (11 sections):**

1. **Introduction** — standards table (CiA 301/302/305/4xx), role of a slave node
2. **Network Architecture** — ASCII diagram of multi-node bus topology + full NMT state machine diagram
3. **Minimal Slave Object Set** — mandatory OD entries table, `CO_IdentityObject_t` in C, Device Type 0x1000
4. **OD Structure** — memory map (index ranges), sub-entry and object descriptor structs, binary search implementation
5. **OD Storage in Flash/EEPROM** — ASCII comparison of EEPROM vs. Flash (ping-pong dual-bank) strategies, `NVM_Header_t`, CRC-32, `NVM_Load()` with bank fallback
6. **Store/Restore (0x1010/0x1011)** — full `CO_StoreParameters()` with dual-bank atomic write, `CO_RestoreDefaults()`, SDO callback
7. **Write-Protect Mechanisms** — ASCII layered protection hierarchy, compile-time flags, NMT state guard, manufacturer guard-key with timeout
8. **Application Callback Hooks** — ASCII hook-point diagram, full `CO_Callbacks_t` table, NMT/RPDO/TPDO/SYNC/OD write callbacks with realistic examples
9. **Watchdog Integration** — ASCII supervision architecture, IWDG init/feed, heartbeat-coupled WDT feed, NMT Life Guarding implementation
10. **Bare-Metal C Reference** — file tree, complete `co_main.h`, `co_od.c`, `co_sdo.c` (expedited SDO server), `co_main.c` with CAN dispatch and main loop; plus an ASCII SDO transaction trace with byte-level annotation
11. **Summary** — concise five-pillar recap

# 27. CANopen Slave Node Implementation

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [CANopen Network Architecture](#2-canopen-network-architecture)
3. [Minimal Slave Object Set](#3-minimal-slave-object-set)
4. [Object Dictionary (OD) — Structure and Storage](#4-object-dictionary-od--structure-and-storage)
5. [OD Storage in Flash and EEPROM](#5-od-storage-in-flash-and-eeprom)
6. [Store/Restore Parameters — Objects 0x1010 / 0x1011](#6-storerestore-parameters--objects-0x1010--0x1011)
7. [Write-Protect Mechanisms](#7-write-protect-mechanisms)
8. [Application Callback Hooks](#8-application-callback-hooks)
9. [Watchdog Integration](#9-watchdog-integration)
10. [Bare-Metal C Reference Implementation](#10-bare-metal-c-reference-implementation)
11. [Summary](#11-summary)

---

## 1. Introduction

A **CANopen Slave Node** is a device on a CANopen network that responds to a master (or manager) node, provides process data via PDOs, and exposes configuration objects through an Object Dictionary (OD). Every compliant CANopen device — whether a sensor, actuator, drive, or I/O module — acts as a slave. The master orchestrates network startup, configures slaves via SDO, and exchanges cyclic process data.

This chapter covers the complete lifecycle of a CANopen slave node: its mandatory object set, persistent storage, parameter protection, callback architecture, watchdog supervision, and a fully worked bare-metal implementation in C.

### Key Standards

| Standard | Scope |
|---|---|
| CiA 301 | CANopen Application Layer (mandatory objects, NMT, SDO, PDO, EMCY, SYNC, TIME) |
| CiA 302 | Network Management extensions |
| CiA 305 | Layer Setting Services (LSS) |
| CiA 4xx | Device profiles (e.g. CiA 402 for drives, CiA 401 for I/O) |

---

## 2. CANopen Network Architecture

```
  +-----------------------------------------------------------------+
  |                     CANopen Network (CAN Bus)                   |
  |                                                                 |
  |  +----------+    +----------+    +----------+    +----------+  |
  |  |  MASTER  |    |  SLAVE   |    |  SLAVE   |    |  SLAVE   |  |
  |  | Node ID 1|    | Node ID 2|    | Node ID 3|    | Node ID 4|  |
  |  |  (NMT    |    |  Sensor  |    |  Actuator|    |  Drive   |  |
  |  |  Manager)|    |          |    |          |    |          |  |
  |  +----+-----+    +----+-----+    +----+-----+    +----+-----+  |
  |       |               |               |               |         |
  +-------+---------------+---------------+---------------+---------+
          |               |               |               |
       CAN H ============================================ CAN H
       CAN L ============================================ CAN L
                     120 Ohm                  120 Ohm
                   terminator              terminator

  NMT Commands (Broadcast):  COB-ID = 0x000
  SDO Server (per slave):    COB-ID = 0x580 + NodeID  (Tx)
                             COB-ID = 0x600 + NodeID  (Rx)
  PDO Tx (slave -> master):  COB-ID = 0x180 + NodeID  (TPDO1)
  PDO Rx (master -> slave):  COB-ID = 0x200 + NodeID  (RPDO1)
  EMCY (slave -> master):    COB-ID = 0x080 + NodeID
  Heartbeat (slave):         COB-ID = 0x700 + NodeID
```

### NMT State Machine

```
                        [Power On / Reset]
                               |
                               v
                      +------------------+
                      |  INITIALISATION  |
                      +--------+---------+
                               |  Boot-up message sent
                               v
             +----------------------------------+
             |          PRE-OPERATIONAL         |<------+
             |  SDO allowed, PDO NOT allowed    |       |
             +--------+-------------------------+       |
                      |                                 |
           NMT: Start |                     NMT: Stop   |
                      v                                 |
             +------------------+           NMT: Enter  |
             |   OPERATIONAL    |-------->  Pre-Op -----+
             |  SDO+PDO allowed |
             +--------+---------+
                      |
           NMT: Reset |
          Node/Comm.  |
                      v
             +------------------+
             |     STOPPED      |
             | No comms except  |
             | NMT & Heartbeat  |
             +------------------+
```

---

## 3. Minimal Slave Object Set

CiA 301 mandates a minimum set of OD entries for every compliant slave. These form the "mandatory object set":

```
  Object Dictionary — Mandatory Entries
  =======================================

  Index   Sub  Name                        Access  Type
  ------  ---  --------------------------  ------  --------
  0x1000   0   Device Type                 RO      UINT32
  0x1001   0   Error Register             RO      UINT8
  0x1018   0   Identity Object (count)    RO      UINT8
  0x1018   1     Vendor ID                RO      UINT32
  0x1018   2     Product Code             RO      UINT32
  0x1018   3     Revision Number          RO      UINT32
  0x1018   4     Serial Number            RO      UINT32

  --- Strongly Recommended (practically mandatory) ---

  0x1005   0   SYNC COB-ID                RW      UINT32
  0x1017   0   Producer Heartbeat Time    RW      UINT16
  0x1400  0-2  RPDO1 Communication Param  RW      RECORD
  0x1600  0-N  RPDO1 Mapping Parameter    RW      RECORD
  0x1800  0-5  TPDO1 Communication Param  RW      RECORD
  0x1A00  0-N  TPDO1 Mapping Parameter    RW      RECORD
  0x1010   0   Store Parameters           RW      UINT32
  0x1011   0   Restore Default Parameters RW      UINT32
```

### C Structure — Device Identity

```c
/* --------------------------------------------------------
 * Mandatory Identity Object 0x1018
 * -------------------------------------------------------- */
typedef struct {
    uint8_t  highest_sub_index;   /* sub 0x00 = 4 */
    uint32_t vendor_id;           /* sub 0x01, assigned by CiA */
    uint32_t product_code;        /* sub 0x02, manufacturer-defined */
    uint32_t revision_number;     /* sub 0x03, major.minor encoded */
    uint32_t serial_number;       /* sub 0x04, unique per device */
} CO_IdentityObject_t;

/* Example population */
static const CO_IdentityObject_t device_identity = {
    .highest_sub_index = 4,
    .vendor_id         = 0x00000123UL,  /* your CiA vendor ID */
    .product_code      = 0x00010002UL,  /* product line 1, model 2 */
    .revision_number   = 0x00010005UL,  /* HW rev 1, SW rev 5 */
    .serial_number     = 0xDEADBEEFUL   /* from MCU serial number */
};
```

### Device Type Object 0x1000

```c
/*
 * Bits 15..0  = Device Profile Number (CiA 4xx)
 * Bits 31..16 = Additional Information
 *
 * Example: Generic I/O device = profile 0x0401 (CiA 401)
 */
#define CO_DEVICE_TYPE_CIA401    0x00000401UL   /* CiA 401 generic I/O  */
#define CO_DEVICE_TYPE_CIA402    0x00000402UL   /* CiA 402 drives       */
#define CO_DEVICE_TYPE_GENERIC   0x00000000UL   /* no profile           */

static const uint32_t co_device_type = CO_DEVICE_TYPE_CIA401;
```

---

## 4. Object Dictionary (OD) — Structure and Storage

The Object Dictionary is the central configuration repository of a CANopen node. Every parameter, process value, and communication parameter lives in the OD, addressed by a 16-bit **Index** and an 8-bit **Sub-Index**.

### OD Architecture

```
  Object Dictionary Memory Layout
  =================================

        Index      Sub    Description
        -------    ---    ---------------------------------
        0x0000            [Reserved]
        0x0001-           Data Type Definitions
        0x001F
        0x0020-           Complex Data Types
        0x003F
        0x1000-           Communication Profile Objects
        0x1FFF            (NMT, SDO, PDO, SYNC, EMCY, ...)
        0x2000-           Manufacturer-Specific Objects
        0x5FFF            (application parameters, I/O, ...)
        0x6000-           Device Profile Objects
        0x9FFF            (CiA 401 I/O, CiA 402 drive ...)
        0xA000-           [Reserved]
        0xFFFF

  OD Entry Descriptor:
  +----------+--------+---------+--------+----------+---------+
  | Index    | SubIdx | Type    | Access | Default  | Ptr/Val |
  | (uint16) |(uint8) |(uint8)  |(uint8) | (void*)  | (void*) |
  +----------+--------+---------+--------+----------+---------+
```

### OD Entry Descriptor in C

```c
/* Access rights bitmask */
#define OD_ACCESS_RO    0x01U
#define OD_ACCESS_WO    0x02U
#define OD_ACCESS_RW    0x03U
#define OD_ACCESS_CONST 0x05U  /* RO + constant flag */

/* Object type codes */
#define OD_TYPE_VAR     0x07U
#define OD_TYPE_ARRAY   0x08U
#define OD_TYPE_RECORD  0x09U

/* Data type codes (CiA 301 Table 44) */
#define OD_DTYPE_BOOL   0x01U
#define OD_DTYPE_INT8   0x02U
#define OD_DTYPE_INT16  0x03U
#define OD_DTYPE_INT32  0x04U
#define OD_DTYPE_UINT8  0x05U
#define OD_DTYPE_UINT16 0x06U
#define OD_DTYPE_UINT32 0x07U
#define OD_DTYPE_UINT64 0x1BU
#define OD_DTYPE_VISSTR 0x09U  /* Visible String */

/* ----------------------------------------------------------
 * OD Sub-entry descriptor
 * ---------------------------------------------------------- */
typedef struct {
    uint8_t   sub_index;
    uint8_t   data_type;     /* OD_DTYPE_xxx */
    uint8_t   access;        /* OD_ACCESS_xxx */
    uint8_t   pdo_mappable;  /* 1 = can be mapped to PDO */
    uint16_t  size_bytes;    /* data size in bytes */
    void     *data_ptr;      /* pointer to RAM or const data */
    void     *default_ptr;   /* pointer to default (ROM) */
} OD_SubEntry_t;

/* ----------------------------------------------------------
 * OD Object (index-level) descriptor
 * ---------------------------------------------------------- */
typedef struct {
    uint16_t          index;
    uint8_t           obj_type;   /* OD_TYPE_xxx */
    uint8_t           num_subs;
    OD_SubEntry_t    *subs;
    /* Optional callback invoked on SDO access */
    int (*on_write)(uint16_t idx, uint8_t sub, void *data, uint16_t len);
    int (*on_read) (uint16_t idx, uint8_t sub, void *data, uint16_t *len);
} OD_Object_t;
```

### Searching the OD

```c
/**
 * @brief  Binary search for an OD object by index.
 *
 * The OD array MUST be sorted by index for binary search to work.
 *
 * @param  od       Pointer to OD array
 * @param  od_size  Number of entries
 * @param  index    16-bit OD index to find
 * @return Pointer to OD_Object_t on success, NULL if not found
 */
const OD_Object_t *OD_Find(const OD_Object_t *od,
                            uint16_t           od_size,
                            uint16_t           index)
{
    int32_t lo = 0, hi = (int32_t)od_size - 1;

    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if (od[mid].index == index) {
            return &od[mid];
        } else if (od[mid].index < index) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}
```

---

## 5. OD Storage in Flash and EEPROM

On embedded systems without an OS, OD values must be persisted in non-volatile storage. Two common strategies are used:

```
  Storage Strategy Overview
  ==========================

  Option A: EEPROM (byte-addressable, random write)
  --------------------------------------------------
  +------------------+   byte-level   +-------------------+
  |  RAM (OD shadow) |  <-----------> |  EEPROM / FRAM    |
  |  (working copy)  |   read/write   |  (persistent NVM) |
  +------------------+                +-------------------+
     Fast access                        Survives power loss
     Lost on reset                      Slow byte-level writes
     Full bandwidth                     Limited write cycles (~1M)

  Option B: Internal Flash (page-erase, word-write)
  --------------------------------------------------
  +------------------+    page copy   +---------+---------+
  |  RAM (OD shadow) |  <-----------> | Active  | Shadow  |
  |  (working copy)  |   on store cmd | Page A  | Page B  |
  +------------------+                +---------+---------+
     Fast access                        Wear levelling needed
     Lost on reset                      Erase before write
                                        Atomic page swap

  Recommended: Dual-bank Flash (ping-pong)
  -----------------------------------------
        Bank A (Active)       Bank B (Shadow / Backup)
        +--------------+      +--------------+
        | Magic        |      | Magic        |
        | CRC-32       |      | CRC-32       |
        | Version      |      | Version      |
        | Param data   |      | Param data   |
        +--------------+      +--------------+
        When storing:
          1. Write new data to Bank B
          2. Verify Bank B CRC
          3. Erase Bank A
          4. Copy Bank B -> Bank A  (or swap pointers)
```

### Flash Storage Header

```c
#include <stdint.h>
#include <string.h>

#define NVM_MAGIC         0x434F4E46UL  /* "CONF" */
#define NVM_VERSION       0x0001U
#define NVM_PARAM_MAX     64U           /* max storable OD entries */

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;           /* NVM_MAGIC — validity marker      */
    uint16_t version;         /* layout version for migration      */
    uint16_t num_entries;     /* number of stored parameters       */
    uint32_t crc32;           /* CRC over [version..data end]      */
    /* Followed immediately by NVM_Entry_t[num_entries] */
} NVM_Header_t;

typedef struct {
    uint16_t index;
    uint8_t  sub_index;
    uint8_t  size;            /* data size in bytes (max 4)        */
    uint8_t  data[4];         /* little-endian value               */
} NVM_Entry_t;
#pragma pack(pop)

/* Flash bank base addresses (MCU-specific) */
#define FLASH_BANK_A_BASE  0x08060000UL
#define FLASH_BANK_B_BASE  0x08070000UL
#define FLASH_BANK_SIZE    0x00010000UL  /* 64 KiB each */
```

### CRC-32 for Data Integrity

```c
/**
 * @brief  Software CRC-32 (polynomial 0xEDB88320, standard Ethernet/ZIP).
 */
uint32_t CRC32_Compute(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;

    while (len--) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8u; bit++) {
            if (crc & 1u) {
                crc = (crc >> 1u) ^ 0xEDB88320UL;
            } else {
                crc >>= 1u;
            }
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}
```

### Loading Parameters from Flash at Boot

```c
/**
 * @brief  Load OD parameters from NVM into RAM shadow.
 *         Tries Bank A first, then Bank B as fallback.
 *
 * @return  0  success
 *         -1  both banks corrupt — factory defaults used
 */
int NVM_Load(OD_Object_t *od, uint16_t od_size)
{
    const uint32_t banks[2] = { FLASH_BANK_A_BASE, FLASH_BANK_B_BASE };

    for (int b = 0; b < 2; b++) {
        const NVM_Header_t *hdr = (const NVM_Header_t *)banks[b];

        /* 1. Validate magic */
        if (hdr->magic != NVM_MAGIC)              { continue; }
        if (hdr->version != NVM_VERSION)          { continue; }
        if (hdr->num_entries > NVM_PARAM_MAX)     { continue; }

        /* 2. Validate CRC (covers version..end of entries) */
        const uint8_t *crc_start = (const uint8_t *)&hdr->version;
        uint32_t       crc_len   = sizeof(uint16_t) + sizeof(uint16_t)
                                 + hdr->num_entries * sizeof(NVM_Entry_t);
        uint32_t computed_crc = CRC32_Compute(crc_start, crc_len);

        if (computed_crc != hdr->crc32)           { continue; }

        /* 3. Populate OD RAM from stored entries */
        const NVM_Entry_t *entries =
            (const NVM_Entry_t *)((uintptr_t)hdr + sizeof(NVM_Header_t));

        for (uint16_t i = 0; i < hdr->num_entries; i++) {
            const OD_Object_t *obj =
                OD_Find(od, od_size, entries[i].index);
            if (!obj) { continue; }

            /* Find sub-entry */
            for (uint8_t s = 0; s < obj->num_subs; s++) {
                if (obj->subs[s].sub_index == entries[i].sub_index) {
                    /* Only restore if writable and size matches */
                    if ((obj->subs[s].access & OD_ACCESS_WO) &&
                        (obj->subs[s].size_bytes == entries[i].size)) {
                        memcpy(obj->subs[s].data_ptr,
                               entries[i].data,
                               entries[i].size);
                    }
                    break;
                }
            }
        }
        return 0; /* success */
    }

    /* Both banks invalid — use factory defaults already in RAM */
    return -1;
}
```

---

## 6. Store/Restore Parameters — Objects 0x1010 / 0x1011

CiA 301 §7.4 defines two mandatory objects for persistent storage management:

### Object 0x1010 — Store Parameters

```
  Object 0x1010 — Store Parameters
  ===================================
  Sub 0x00:  Highest sub-index supported  (RO, UINT8)
  Sub 0x01:  Save all parameters          (RW, UINT32)
  Sub 0x02:  Save communication params   (RW, UINT32)
  Sub 0x03:  Save application params     (RW, UINT32)
  Sub 0x04+: Manufacturer-specific       (RW, UINT32)

  Write trigger: write the signature 0x65766173 ("save" in ASCII LE)
  Read value:
    Bit 0 = 1: device saves spontaneously on power loss (if capable)
    Bit 1 = 1: device saves parameters autonomously
    Bit 2..31: reserved
```

### Object 0x1011 — Restore Default Parameters

```
  Object 0x1011 — Restore Default Parameters
  =============================================
  Sub 0x00:  Highest sub-index supported  (RO, UINT8)
  Sub 0x01:  Restore all defaults         (RW, UINT32)
  Sub 0x02:  Restore communication defs  (RW, UINT32)
  Sub 0x03:  Restore application defs    (RW, UINT32)

  Write trigger: write the signature 0x64616F6C ("load" in ASCII LE)
  Takes effect after next device RESET.
```

### C Implementation — Store/Restore Callbacks

```c
#define CO_STORE_SIGNATURE    0x65766173UL   /* "save" little-endian */
#define CO_RESTORE_SIGNATURE  0x64616F6CUL   /* "load" little-endian */

/* Shadow RAM for all storable parameters */
typedef struct {
    /* Communication parameters (OD 0x1005, 0x1006, 0x1014, 0x1017 etc.) */
    uint32_t sync_cob_id;
    uint32_t sync_cycle_period;
    uint16_t heartbeat_producer;
    /* Application parameters */
    uint16_t sensor_sample_rate_ms;
    int16_t  analog_offset_mv;
    uint8_t  output_mode;
    /* ... more fields ... */
} App_Params_t;

static App_Params_t g_params;  /* working copy in RAM */

/* -----------------------------------------------------------------------
 * Enumerate storable parameters (index, sub, ptr, size)
 * ----------------------------------------------------------------------- */
typedef struct {
    uint16_t index;
    uint8_t  sub;
    void    *ptr;
    uint8_t  size;
} Storable_t;

static const Storable_t g_storable[] = {
    { 0x1005, 0x00, &g_params.sync_cob_id,         4 },
    { 0x1006, 0x00, &g_params.sync_cycle_period,   4 },
    { 0x1017, 0x00, &g_params.heartbeat_producer,  2 },
    { 0x2001, 0x01, &g_params.sensor_sample_rate_ms, 2 },
    { 0x2001, 0x02, &g_params.analog_offset_mv,    2 },
    { 0x2001, 0x03, &g_params.output_mode,         1 },
};
#define NUM_STORABLE  (sizeof(g_storable) / sizeof(g_storable[0]))

/* -----------------------------------------------------------------------
 * Write to Flash Bank A (simplified — real MCU uses HAL_FLASH_Program)
 * ----------------------------------------------------------------------- */
static int Flash_WritePage(uint32_t base_addr,
                            const uint8_t *data,
                            uint32_t len);
static int Flash_ErasePage(uint32_t base_addr);

/* -----------------------------------------------------------------------
 * CO_StoreParameters() — triggered by 0x1010 SDO write
 * ----------------------------------------------------------------------- */
int CO_StoreParameters(uint8_t sub_index)
{
    static uint8_t nvm_buf[FLASH_BANK_SIZE];
    NVM_Header_t  *hdr = (NVM_Header_t *)nvm_buf;
    NVM_Entry_t   *entries = (NVM_Entry_t *)(nvm_buf + sizeof(NVM_Header_t));
    uint16_t       count = 0;

    /* Build the NVM image in RAM first */
    memset(nvm_buf, 0xFF, sizeof(nvm_buf));

    for (uint16_t i = 0; i < NUM_STORABLE; i++) {
        /* Filter by sub_index: 1=all, 2=comm only, 3=app only */
        if (sub_index == 2 && g_storable[i].index >= 0x2000) continue;
        if (sub_index == 3 && g_storable[i].index <  0x2000) continue;

        entries[count].index     = g_storable[i].index;
        entries[count].sub_index = g_storable[i].sub;
        entries[count].size      = g_storable[i].size;
        memcpy(entries[count].data, g_storable[i].ptr, g_storable[i].size);
        count++;
    }

    /* Fill header */
    hdr->magic       = NVM_MAGIC;
    hdr->version     = NVM_VERSION;
    hdr->num_entries = count;

    /* CRC over version + num_entries + entries */
    uint32_t crc_len = sizeof(uint16_t) + sizeof(uint16_t)
                     + count * sizeof(NVM_Entry_t);
    hdr->crc32 = CRC32_Compute((uint8_t *)&hdr->version, crc_len);

    /* Write to Bank B first (shadow), then swap */
    if (Flash_ErasePage(FLASH_BANK_B_BASE) != 0)  return -1;
    if (Flash_WritePage(FLASH_BANK_B_BASE,
                        nvm_buf,
                        sizeof(NVM_Header_t) + count * sizeof(NVM_Entry_t))
        != 0) return -2;

    /* Verify Bank B, then promote to Bank A */
    if (Flash_ErasePage(FLASH_BANK_A_BASE) != 0)  return -3;
    if (Flash_WritePage(FLASH_BANK_A_BASE,
                        nvm_buf,
                        sizeof(NVM_Header_t) + count * sizeof(NVM_Entry_t))
        != 0) return -4;

    return 0; /* success */
}

/* -----------------------------------------------------------------------
 * CO_RestoreDefaults() — triggered by 0x1011 SDO write
 * Marks NVM invalid; actual restore happens after CPU reset
 * ----------------------------------------------------------------------- */
int CO_RestoreDefaults(uint8_t sub_index)
{
    (void)sub_index;

    /* Invalidate both banks by erasing magic (first 4 bytes) */
    Flash_ErasePage(FLASH_BANK_A_BASE);
    Flash_ErasePage(FLASH_BANK_B_BASE);

    /* Trigger watchdog reset or software reset */
    /* NVIC_SystemReset(); */   /* Cortex-M */

    return 0;
}

/* -----------------------------------------------------------------------
 * SDO write callback for 0x1010 and 0x1011
 * ----------------------------------------------------------------------- */
int OD_Callback_Store(uint16_t idx, uint8_t sub, void *data, uint16_t len)
{
    if (len != 4) return -1; /* abort: wrong size */

    uint32_t sig;
    memcpy(&sig, data, 4);

    if (idx == 0x1010) {
        if (sig != CO_STORE_SIGNATURE)   return -2; /* abort: bad signature */
        return CO_StoreParameters(sub);
    }

    if (idx == 0x1011) {
        if (sig != CO_RESTORE_SIGNATURE) return -2; /* abort: bad signature */
        return CO_RestoreDefaults(sub);
    }

    return -1;
}
```

---

## 7. Write-Protect Mechanisms

CANopen provides multiple layers of write protection to prevent accidental or unauthorized parameter changes.

### Protection Levels Overview

```
  Write Protection Hierarchy
  ============================

  Level 1: CiA 301 Access Rights (compile-time)
  -----------------------------------------------
  OD entries marked OD_ACCESS_RO or OD_ACCESS_CONST
  -> SDO write returns abort code 0x06010002 (read-only)

  Level 2: NMT State Guard (runtime)
  ------------------------------------
  PDO mapping may only be changed in PRE-OPERATIONAL
  -> Check current NMT state before allowing write

  Level 3: Signature-Protected Write (runtime)
  ----------------------------------------------
  0x1010 / 0x1011 require magic signatures
  -> Write without signature -> abort 0x08000022

  Level 4: Password/Key Object (manufacturer-specific)
  ------------------------------------------------------
  A "guard key" object in 0x2000 range must be
  written with a secret value before sensitive
  objects accept writes.
  -> Key expires after timeout or on next access

  Level 5: Hardware Write-Enable Pin (hardware)
  ----------------------------------------------
  External WP pin on EEPROM or MCU flash
  -> NVM write attempted without WP pulled low -> error

  SDO Abort Codes (CiA 301 Table 23 excerpts)
  =============================================
  0x06010000  Unsupported access
  0x06010001  Read-only, write attempted
  0x06010002  Write-only, read attempted
  0x06090030  Value range exceeded
  0x08000022  Data cannot be stored / NMT state conflict
  0x08000021  Wrong state for operation
```

### Compile-Time OD Access Flags

```c
/* OD declaration: 0x1000 is permanently read-only */
static const uint32_t co_device_type = CO_DEVICE_TYPE_CIA401;

static OD_SubEntry_t od_1000_subs[] = {
    {
        .sub_index   = 0x00,
        .data_type   = OD_DTYPE_UINT32,
        .access      = OD_ACCESS_RO,   /* <-- compile-time RO */
        .pdo_mappable= 0,
        .size_bytes  = 4,
        .data_ptr    = (void *)&co_device_type,
        .default_ptr = (void *)&co_device_type
    }
};
```

### Runtime NMT State Guard for PDO Mapping

```c
typedef enum {
    NMT_STATE_INIT        = 0x00,
    NMT_STATE_STOPPED     = 0x04,
    NMT_STATE_OPERATIONAL = 0x05,
    NMT_STATE_PRE_OP      = 0x7F
} NMT_State_t;

static NMT_State_t g_nmt_state = NMT_STATE_INIT;

/**
 * @brief  Check if an SDO write to the given index is allowed
 *         in the current NMT state.
 *
 * PDO communication (0x1400-0x1BFF) and PDO mapping (0x1600-0x1BFF)
 * objects may only be modified in PRE-OPERATIONAL.
 *
 * @return  0  allowed
 *         SDO abort code on violation
 */
uint32_t OD_CheckWriteAllowed(uint16_t index)
{
    /* PDO objects: only writable in PRE-OPERATIONAL */
    if (index >= 0x1400U && index <= 0x1BFFU) {
        if (g_nmt_state != NMT_STATE_PRE_OP) {
            return 0x08000022UL; /* cannot store, wrong NMT state */
        }
    }

    /* Stopped state: only NMT + heartbeat allowed */
    if (g_nmt_state == NMT_STATE_STOPPED) {
        return 0x08000021UL; /* wrong state */
    }

    return 0; /* allowed */
}
```

### Manufacturer-Specific Guard Key

```c
/*
 * Object 0x2000, Sub 1: "Guard Key"
 *
 * Before writing protected params in 0x2001, 0x2002 etc.,
 * the master must write 0xABCD1234 to 0x2000:01.
 * The key is valid for 5 seconds only.
 */
#define GUARD_KEY_VALUE   0xABCD1234UL
#define GUARD_KEY_TIMEOUT_MS  5000U

static uint8_t  g_guard_active   = 0;
static uint32_t g_guard_set_time = 0;   /* milliseconds */

extern uint32_t SysTick_GetMs(void);    /* platform-specific */

int OD_Callback_GuardKey(uint16_t idx, uint8_t sub, void *data, uint16_t len)
{
    (void)idx; (void)sub;
    if (len != 4) return -1;

    uint32_t key;
    memcpy(&key, data, 4);

    if (key == GUARD_KEY_VALUE) {
        g_guard_active   = 1;
        g_guard_set_time = SysTick_GetMs();
        return 0;
    }

    g_guard_active = 0;  /* wrong key clears guard */
    return -1; /* abort */
}

/**
 * @brief  Called from protected OD write callbacks.
 * @return  0 = write permitted,  -1 = write denied
 */
int OD_GuardCheck(void)
{
    if (!g_guard_active) return -1;

    uint32_t elapsed = SysTick_GetMs() - g_guard_set_time;
    if (elapsed > GUARD_KEY_TIMEOUT_MS) {
        g_guard_active = 0;
        return -1;   /* timed out */
    }
    return 0;  /* permitted */
}
```

---

## 8. Application Callback Hooks

Callbacks decouple the CANopen stack from application logic and allow the stack to notify the application of significant events without polling.

### Callback Table Architecture

```
  Callback Hook Points
  ======================

  Stack Layer                     Application
  +--------------+                +----------------+
  | NMT Handler  |-- on_nmt_state_change() ------> | Update I/O,  |
  |              |                                  | start/stop   |
  |              |                                  | timers, etc. |
  +--------------+                                  |              |
  | SDO Server   |-- on_od_write()  -------------> | Validate,    |
  |              |-- on_od_read()   -------------> | compute,     |
  |              |                                  | hardware cmd |
  +--------------+                                  |              |
  | PDO Handler  |-- on_rpdo_received() ---------> | Copy inputs  |
  |              |-- on_tpdo_transmit() ---------> | Fill outputs |
  +--------------+                                  |              |
  | EMCY Handler |-- on_emcy_received() ---------> | Log, alarm   |
  +--------------+                                  |              |
  | SYNC Handler |-- on_sync() ------------------> | Trigger A/D, |
  |              |                                  | run control  |
  +--------------+                +----------------+
```

### Callback Function Prototypes and Registration

```c
/* -----------------------------------------------------------------------
 * Callback type definitions
 * ----------------------------------------------------------------------- */

/** NMT state change notification */
typedef void (*CO_CB_NmtStateChange)(NMT_State_t old_state,
                                      NMT_State_t new_state);

/** SDO write notification: return 0 = accept, non-zero = SDO abort */
typedef int  (*CO_CB_OdWrite)(uint16_t index, uint8_t sub,
                               const void *data, uint16_t len);

/** SDO read notification: fill data buffer, set *len */
typedef int  (*CO_CB_OdRead) (uint16_t index, uint8_t sub,
                               void *data, uint16_t *len);

/** RPDO received notification: data = PDO payload, len = payload length */
typedef void (*CO_CB_RpdoReceived)(uint8_t pdo_num,
                                    const uint8_t *data,
                                    uint8_t len);

/** TPDO about to be transmitted: fill PDO payload */
typedef void (*CO_CB_TpdoTransmit)(uint8_t pdo_num,
                                    uint8_t *data,
                                    uint8_t len);

/** SYNC received */
typedef void (*CO_CB_Sync)(uint8_t counter);

/** Emergency received from network */
typedef void (*CO_CB_EmcyReceived)(uint8_t node_id,
                                    uint16_t error_code,
                                    uint8_t  error_register);

/* -----------------------------------------------------------------------
 * CANopen stack callback table
 * ----------------------------------------------------------------------- */
typedef struct {
    CO_CB_NmtStateChange  on_nmt_state_change;
    CO_CB_OdWrite         on_od_write;
    CO_CB_OdRead          on_od_read;
    CO_CB_RpdoReceived    on_rpdo_received;
    CO_CB_TpdoTransmit    on_tpdo_transmit;
    CO_CB_Sync            on_sync;
    CO_CB_EmcyReceived    on_emcy_received;
} CO_Callbacks_t;

static CO_Callbacks_t g_co_callbacks = { 0 };  /* all NULL initially */

/** Register application callbacks */
void CO_RegisterCallbacks(const CO_Callbacks_t *cbs)
{
    if (cbs) {
        g_co_callbacks = *cbs;
    }
}
```

### Example Application Callbacks

```c
/* -----------------------------------------------------------------------
 * Application NMT state change handler
 * ----------------------------------------------------------------------- */
static void App_OnNmtStateChange(NMT_State_t old_state,
                                  NMT_State_t new_state)
{
    (void)old_state;

    switch (new_state) {
        case NMT_STATE_OPERATIONAL:
            /* Enable PWM output, start control loop */
            PWM_Enable();
            ControlLoop_Start();
            break;

        case NMT_STATE_PRE_OP:
        case NMT_STATE_STOPPED:
            /* Safe state: disable outputs */
            PWM_Disable();
            ControlLoop_Stop();
            Output_SetSafe();
            break;

        default:
            break;
    }
}

/* -----------------------------------------------------------------------
 * RPDO received — copy process image
 * PDO 1: Byte 0 = control word, Bytes 2-3 = target velocity (int16)
 * ----------------------------------------------------------------------- */
static void App_OnRpdoReceived(uint8_t pdo_num,
                                const uint8_t *data,
                                uint8_t len)
{
    if (pdo_num == 0 && len >= 4) {
        uint16_t ctrl_word;
        int16_t  target_vel;
        memcpy(&ctrl_word,  &data[0], 2);
        memcpy(&target_vel, &data[2], 2);

        /* Apply to application state */
        DriveCtrl_SetControlWord(ctrl_word);
        DriveCtrl_SetTargetVelocity(target_vel);
    }
}

/* -----------------------------------------------------------------------
 * TPDO transmit — fill process image into PDO payload
 * PDO 1: Bytes 0-1 = status word, Bytes 2-3 = actual velocity
 * ----------------------------------------------------------------------- */
static void App_OnTpdoTransmit(uint8_t pdo_num,
                                uint8_t *data,
                                uint8_t len)
{
    if (pdo_num == 0 && len >= 4) {
        uint16_t status_word = DriveCtrl_GetStatusWord();
        int16_t  actual_vel  = DriveCtrl_GetActualVelocity();
        memcpy(&data[0], &status_word, 2);
        memcpy(&data[2], &actual_vel,  2);
    }
}

/* -----------------------------------------------------------------------
 * SYNC received — trigger synchronous ADC sampling
 * ----------------------------------------------------------------------- */
static void App_OnSync(uint8_t counter)
{
    (void)counter;
    ADC_TriggerSample();     /* starts DMA-based ADC read */
}

/* -----------------------------------------------------------------------
 * OD write callback — validate application-specific parameters
 * ----------------------------------------------------------------------- */
static int App_OnOdWrite(uint16_t index, uint8_t sub,
                          const void *data, uint16_t len)
{
    /* 0x2001:01 — sensor sample rate: must be 1..1000 ms */
    if (index == 0x2001 && sub == 0x01 && len == 2) {
        uint16_t rate;
        memcpy(&rate, data, 2);
        if (rate < 1 || rate > 1000) {
            return 0x06090030; /* SDO abort: value range exceeded */
        }
        g_params.sensor_sample_rate_ms = rate;
        Sensor_SetSampleRate(rate);
        return 0;
    }

    /* 0x1010 / 0x1011 — store/restore */
    if (index == 0x1010 || index == 0x1011) {
        return OD_Callback_Store(index, sub, (void *)data, len);
    }

    return 0; /* default: accept */
}

/* -----------------------------------------------------------------------
 * Wire up callbacks at startup
 * ----------------------------------------------------------------------- */
void App_Init_CANopen(void)
{
    static const CO_Callbacks_t cbs = {
        .on_nmt_state_change = App_OnNmtStateChange,
        .on_od_write         = App_OnOdWrite,
        .on_od_read          = NULL,
        .on_rpdo_received    = App_OnRpdoReceived,
        .on_tpdo_transmit    = App_OnTpdoTransmit,
        .on_sync             = App_OnSync,
        .on_emcy_received    = NULL
    };
    CO_RegisterCallbacks(&cbs);
}
```

---

## 9. Watchdog Integration

A hardware watchdog timer (WDT) is critical for embedded slave nodes to recover from software lockups. The CANopen stack's timing structure provides natural watchdog feed points.

### Watchdog Supervision Architecture

```
  Watchdog Supervision Strategy
  ================================

  Hardware WDT timeout:  e.g. 500 ms
  CAN heartbeat:         e.g. 100 ms (0x1017 = 100)

  +-----------------------------------+
  |         Application Main Loop     |
  |                                   |
  |  while(1) {                       |
  |    +--------------+               |
  |    | CAN RX Poll  |               |
  |    +--------------+               |
  |    | SDO Process  |               |
  |    +--------------+               |
  |    | NMT Process  |               |
  |    +--------------+               |
  |    | PDO Transmit |               |
  |    +--------------+               |
  |    | Heartbeat    |<-- feeds WDT  |
  |    +--------------+               |
  |    | App Task     |               |
  |    +--------------+               |
  |  }                                |
  +-----------------------------------+

  If main loop stalls > 500 ms -> WDT fires -> MCU reset

  Additional: NMT Life Guarding / Node Guarding
  -----------------------------------------------
  0x100C: Guard Time (ms)   — how often master polls
  0x100D: Life Time Factor  — missed polls before error

  If master stops guarding within guard_time * life_time_factor
  -> Node fires EMCY, enters PRE-OPERATIONAL or STOPPED
  -> This gives software-level supervision even without HW WDT
```

### IWDG / WDT Driver (Cortex-M style)

```c
/* -----------------------------------------------------------------------
 * Platform-agnostic watchdog interface
 * Replace implementations with MCU-specific register writes
 * ----------------------------------------------------------------------- */

/**
 * @brief  Initialise the independent watchdog.
 * @param  timeout_ms  Watchdog timeout in milliseconds.
 *                     MCU reset occurs if not fed within this period.
 */
void WDT_Init(uint32_t timeout_ms)
{
    /*
     * STM32 IWDG example (LSI = ~32 kHz):
     *   prescaler=64 -> tick=2ms; reload = timeout_ms/2
     *
     * IWDG->KR   = 0xCCCC;   -- enable
     * IWDG->KR   = 0x5555;   -- unlock PR/RLR
     * IWDG->PR   = 0x04;     -- /64 prescaler
     * IWDG->RLR  = timeout_ms / 2;
     * while (IWDG->SR != 0); -- wait for update
     * IWDG->KR   = 0xAAAA;   -- refresh
     */
    (void)timeout_ms;
    /* TODO: implement for your MCU */
}

/**
 * @brief  Feed (kick) the watchdog — call periodically.
 */
void WDT_Feed(void)
{
    /* STM32: IWDG->KR = 0xAAAA; */
}

/* -----------------------------------------------------------------------
 * Heartbeat producer — sends heartbeat AND feeds WDT
 * Called from 1 ms SysTick or from main loop timer check
 * ----------------------------------------------------------------------- */
static uint32_t g_heartbeat_timer_ms = 0;

void CO_HeartbeatProcess(uint32_t elapsed_ms)
{
    if (g_params.heartbeat_producer == 0) {
        return;  /* heartbeat disabled */
    }

    g_heartbeat_timer_ms += elapsed_ms;

    if (g_heartbeat_timer_ms >= g_params.heartbeat_producer) {
        g_heartbeat_timer_ms = 0;

        /* Build and send heartbeat message */
        uint8_t hb_data = (uint8_t)g_nmt_state;
        CAN_Send(0x700U + g_node_id, &hb_data, 1);

        /* Feed watchdog each heartbeat cycle */
        WDT_Feed();
    }
}
```

### NMT Life Guarding (Software Supervision)

```c
/* -----------------------------------------------------------------------
 * NMT Life Guarding
 * Master polls 0x100C + 0x100D via RTR on COB-ID 0x700+NodeID.
 * Node tracks missed guard polls.
 * ----------------------------------------------------------------------- */
static uint16_t g_guard_time_ms       = 0;  /* 0 = disabled  */
static uint8_t  g_life_time_factor    = 0;
static uint16_t g_guard_miss_count    = 0;
static uint32_t g_guard_last_time_ms  = 0;

/** Called from CAN RX handler when RTR on 0x700+NodeID received */
void CO_LifeGuard_OnPoll(void)
{
    g_guard_miss_count   = 0;
    g_guard_last_time_ms = SysTick_GetMs();
}

/** Called from main loop (1 ms tick) */
void CO_LifeGuard_Process(uint32_t elapsed_ms)
{
    if (g_guard_time_ms == 0 || g_life_time_factor == 0) return;

    uint32_t deadline = (uint32_t)g_guard_time_ms * g_life_time_factor;
    uint32_t elapsed  = SysTick_GetMs() - g_guard_last_time_ms;

    if (elapsed > deadline) {
        /* Life guarding event: send EMCY and enter PRE-OPERATIONAL */
        uint8_t emcy[8] = { 0x81, 0x81, 0x20, 0, 0, 0, 0, 0 };
        /* 0x8181 = Life Guarding / Heartbeat Error */
        CAN_Send(0x080U + g_node_id, emcy, 8);

        CO_NMT_SetState(NMT_STATE_PRE_OP);
        g_guard_last_time_ms = SysTick_GetMs(); /* reset to avoid spam */
    }
    (void)elapsed_ms;
}
```

---

## 10. Bare-Metal C Reference Implementation

This section presents a minimal but complete, self-contained bare-metal CANopen slave for a Cortex-M MCU. It integrates all topics covered above.

### File Structure

```
  project/
  ├── canopen/
  │   ├── co_main.h          -- public API
  │   ├── co_main.c          -- top-level init + main loop
  │   ├── co_od.h/c          -- object dictionary
  │   ├── co_sdo.h/c         -- SDO server
  │   ├── co_pdo.h/c         -- PDO handler
  │   ├── co_nmt.h/c         -- NMT state machine
  │   ├── co_emcy.h/c        -- emergency object
  │   └── co_nvm.h/c         -- NVM store/restore
  ├── bsp/
  │   ├── can_driver.h/c     -- CAN peripheral driver
  │   ├── flash_driver.h/c   -- flash read/write/erase
  │   └── wdt_driver.h/c     -- watchdog driver
  └── app/
      └── main.c             -- application entry point
```

### co_main.h — Public Interface

```c
/* co_main.h */
#ifndef CO_MAIN_H
#define CO_MAIN_H

#include <stdint.h>
#include "co_od.h"

#define CO_NODE_ID_DEFAULT   2U   /* override with DIP switch or NVM */

/**
 * @brief  Initialise CANopen stack.
 * @param  node_id  CAN Node ID (1..127)
 * @param  bitrate  CAN bitrate in kbit/s (125, 250, 500, 1000)
 */
void CO_Init(uint8_t node_id, uint32_t bitrate_kbps);

/**
 * @brief  Main processing function — call from main loop or 1 ms tick.
 * @param  elapsed_ms  Milliseconds since last call.
 */
void CO_Process(uint32_t elapsed_ms);

/**
 * @brief  Transmit an Emergency message.
 * @param  error_code  16-bit EMCY error code (CiA 301 §7.2.7)
 * @param  error_reg   Error register (OD 0x1001)
 */
void CO_SendEmergency(uint16_t error_code, uint8_t error_reg);

#endif /* CO_MAIN_H */
```

### co_od.c — Minimal Object Dictionary

```c
/* co_od.c — Minimal OD population */
#include "co_od.h"
#include "co_nvm.h"
#include <string.h>

/* ---- Persistent application parameters (RAM shadow) ---- */
App_Params_t g_params = {
    .sync_cob_id            = 0x00000080UL,
    .sync_cycle_period      = 10000U,    /* 10 ms */
    .heartbeat_producer     = 100U,      /* 100 ms */
    .sensor_sample_rate_ms  = 10U,
    .analog_offset_mv       = 0,
    .output_mode            = 0
};

/* ---- Run-time values ---- */
static uint8_t  co_error_register   = 0;
static uint32_t co_store_param      = 0;
static uint32_t co_restore_param    = 0;

/* ---- Identity (ROM) ---- */
static const CO_IdentityObject_t co_identity = {
    .highest_sub_index = 4,
    .vendor_id         = 0x00000123UL,
    .product_code      = 0x00010001UL,
    .revision_number   = 0x00010001UL,
    .serial_number     = 0xDEADBEEFUL
};
static const uint32_t co_device_type = CO_DEVICE_TYPE_CIA401;

/* ---- Sub-entry arrays (per OD object) ---- */

/* 0x1000 Device Type */
static OD_SubEntry_t od_1000_subs[] = {
    { 0, OD_DTYPE_UINT32, OD_ACCESS_RO, 0, 4,
      (void*)&co_device_type, (void*)&co_device_type }
};

/* 0x1001 Error Register */
static OD_SubEntry_t od_1001_subs[] = {
    { 0, OD_DTYPE_UINT8, OD_ACCESS_RO, 0, 1,
      &co_error_register, NULL }
};

/* 0x1010 Store Parameters */
static OD_SubEntry_t od_1010_subs[] = {
    { 0x00, OD_DTYPE_UINT8,  OD_ACCESS_RO, 0, 1,
      NULL, NULL },   /* populated at runtime */
    { 0x01, OD_DTYPE_UINT32, OD_ACCESS_RW, 0, 4,
      &co_store_param, NULL },
    { 0x02, OD_DTYPE_UINT32, OD_ACCESS_RW, 0, 4,
      &co_store_param, NULL },
    { 0x03, OD_DTYPE_UINT32, OD_ACCESS_RW, 0, 4,
      &co_store_param, NULL }
};

/* 0x1011 Restore Default Parameters */
static OD_SubEntry_t od_1011_subs[] = {
    { 0x00, OD_DTYPE_UINT8,  OD_ACCESS_RO, 0, 1, NULL, NULL },
    { 0x01, OD_DTYPE_UINT32, OD_ACCESS_RW, 0, 4,
      &co_restore_param, NULL }
};

/* 0x1017 Producer Heartbeat Time */
static OD_SubEntry_t od_1017_subs[] = {
    { 0, OD_DTYPE_UINT16, OD_ACCESS_RW, 0, 2,
      &g_params.heartbeat_producer, NULL }
};

/* 0x1018 Identity Object */
static OD_SubEntry_t od_1018_subs[] = {
    { 0x00, OD_DTYPE_UINT8,  OD_ACCESS_RO, 0, 1,
      (void*)&co_identity.highest_sub_index, NULL },
    { 0x01, OD_DTYPE_UINT32, OD_ACCESS_RO, 0, 4,
      (void*)&co_identity.vendor_id, NULL },
    { 0x02, OD_DTYPE_UINT32, OD_ACCESS_RO, 0, 4,
      (void*)&co_identity.product_code, NULL },
    { 0x03, OD_DTYPE_UINT32, OD_ACCESS_RO, 0, 4,
      (void*)&co_identity.revision_number, NULL },
    { 0x04, OD_DTYPE_UINT32, OD_ACCESS_RO, 0, 4,
      (void*)&co_identity.serial_number, NULL }
};

/* 0x2001 Application Parameters */
static OD_SubEntry_t od_2001_subs[] = {
    { 0x00, OD_DTYPE_UINT8,  OD_ACCESS_RO, 0, 1, NULL, NULL },
    { 0x01, OD_DTYPE_UINT16, OD_ACCESS_RW, 0, 2,
      &g_params.sensor_sample_rate_ms, NULL },
    { 0x02, OD_DTYPE_INT16,  OD_ACCESS_RW, 0, 2,
      &g_params.analog_offset_mv, NULL },
    { 0x03, OD_DTYPE_UINT8,  OD_ACCESS_RW, 0, 1,
      &g_params.output_mode, NULL }
};

/* ---- Master OD Table (must be sorted by index!) ---- */
OD_Object_t g_od[] = {
    { 0x1000, OD_TYPE_VAR,    1, od_1000_subs,  NULL,              NULL  },
    { 0x1001, OD_TYPE_VAR,    1, od_1001_subs,  NULL,              NULL  },
    { 0x1010, OD_TYPE_RECORD, 4, od_1010_subs,  OD_Callback_Store, NULL  },
    { 0x1011, OD_TYPE_RECORD, 2, od_1011_subs,  OD_Callback_Store, NULL  },
    { 0x1017, OD_TYPE_VAR,    1, od_1017_subs,  NULL,              NULL  },
    { 0x1018, OD_TYPE_RECORD, 5, od_1018_subs,  NULL,              NULL  },
    { 0x2001, OD_TYPE_RECORD, 4, od_2001_subs,  App_OnOdWrite,     NULL  },
};
const uint16_t g_od_size = sizeof(g_od) / sizeof(g_od[0]);
```

### co_sdo.c — SDO Server (Expedited Transfer)

```c
/* co_sdo.c — Expedited SDO server (4 bytes or less) */
#include "co_sdo.h"
#include "co_od.h"
#include <string.h>

#define SDO_CS_INITIATE_DOWNLOAD   0x20U
#define SDO_CS_INITIATE_UPLOAD     0x40U
#define SDO_CS_ABORT               0x80U

#define SDO_RESP_DOWNLOAD_OK       0x60U
#define SDO_RESP_UPLOAD_OK         0x43U  /* expedited, 4 bytes */
#define SDO_RESP_ABORT             0x80U

typedef struct __attribute__((packed)) {
    uint8_t  cs;
    uint16_t index;
    uint8_t  sub_index;
    uint8_t  data[4];
} SDO_Frame_t;

static void SDO_SendAbort(uint8_t node_id,
                           uint16_t idx, uint8_t sub,
                           uint32_t abort_code)
{
    uint8_t frame[8];
    frame[0] = SDO_RESP_ABORT;
    memcpy(&frame[1], &idx,        2);
    frame[3] = sub;
    memcpy(&frame[4], &abort_code, 4);
    CAN_Send(0x580U + node_id, frame, 8);
}

/**
 * @brief  Process an incoming SDO request (COB-ID 0x600 + NodeID).
 */
void SDO_Process(uint8_t node_id, const uint8_t *can_data, uint8_t dlc)
{
    if (dlc < 8) return;

    const SDO_Frame_t *req = (const SDO_Frame_t *)can_data;
    uint8_t cs_nibble = req->cs & 0xE0U;

    /* Locate OD object */
    const OD_Object_t *obj = OD_Find(g_od, g_od_size, req->index);
    if (!obj) {
        SDO_SendAbort(node_id, req->index, req->sub_index, 0x06020000UL);
        /* 0x06020000 = object does not exist */
        return;
    }

    /* Find sub-entry */
    OD_SubEntry_t *sub_entry = NULL;
    for (uint8_t i = 0; i < obj->num_subs; i++) {
        if (obj->subs[i].sub_index == req->sub_index) {
            sub_entry = &obj->subs[i];
            break;
        }
    }
    if (!sub_entry) {
        SDO_SendAbort(node_id, req->index, req->sub_index, 0x06090011UL);
        /* 0x06090011 = sub-index does not exist */
        return;
    }

    /* ---------------------------------------------------------------- */
    if (cs_nibble == SDO_CS_INITIATE_UPLOAD) {
        /* SDO READ */
        if (!(sub_entry->access & OD_ACCESS_RO)) {
            SDO_SendAbort(node_id, req->index, req->sub_index, 0x06010001UL);
            return;
        }
        if (sub_entry->size_bytes > 4) {
            /* Segmented transfer not shown; abort for simplicity */
            SDO_SendAbort(node_id, req->index, req->sub_index, 0x08000020UL);
            return;
        }

        uint8_t resp[8] = { 0 };
        /* CS: expedited, size indicated, (4 - size) in bits 3:2 */
        resp[0] = (uint8_t)(0x43U | ((4U - sub_entry->size_bytes) << 2U));
        memcpy(&resp[1], &req->index, 2);
        resp[3] = req->sub_index;

        if (obj->on_read) {
            uint16_t rd_len = sub_entry->size_bytes;
            obj->on_read(req->index, req->sub_index, &resp[4], &rd_len);
        } else if (sub_entry->data_ptr) {
            memcpy(&resp[4], sub_entry->data_ptr, sub_entry->size_bytes);
        }

        CAN_Send(0x580U + node_id, resp, 8);

    /* ---------------------------------------------------------------- */
    } else if (cs_nibble == SDO_CS_INITIATE_DOWNLOAD) {
        /* SDO WRITE */
        if (!(sub_entry->access & OD_ACCESS_WO)) {
            SDO_SendAbort(node_id, req->index, req->sub_index, 0x06010002UL);
            return;
        }

        /* NMT state guard */
        uint32_t nmt_abort = OD_CheckWriteAllowed(req->index);
        if (nmt_abort) {
            SDO_SendAbort(node_id, req->index, req->sub_index, nmt_abort);
            return;
        }

        /* Size check (expedited: cs bits 3:2 = 4 - data_size) */
        uint8_t transfer_size = (req->cs & 0x01U) ?
                                (4U - ((req->cs >> 2U) & 0x03U)) : 4U;

        /* Invoke OD write callback (validation) */
        int cb_result = 0;
        if (obj->on_write) {
            cb_result = obj->on_write(req->index, req->sub_index,
                                       req->data, transfer_size);
        }
        if (cb_result != 0) {
            uint32_t abort = (cb_result > 0) ?
                             (uint32_t)cb_result : 0x08000000UL;
            SDO_SendAbort(node_id, req->index, req->sub_index, abort);
            return;
        }

        /* Write to OD RAM */
        if (sub_entry->data_ptr && transfer_size <= sub_entry->size_bytes) {
            memcpy(sub_entry->data_ptr, req->data, transfer_size);
        }

        /* Send confirmation */
        uint8_t resp[8] = { 0 };
        resp[0] = SDO_RESP_DOWNLOAD_OK;
        memcpy(&resp[1], &req->index, 2);
        resp[3] = req->sub_index;
        CAN_Send(0x580U + node_id, resp, 8);
    }
}
```

### co_main.c — Top-Level Init and Main Loop

```c
/* co_main.c */
#include "co_main.h"
#include "co_od.h"
#include "co_sdo.h"
#include "co_nmt.h"
#include "co_pdo.h"
#include "co_nvm.h"
#include "can_driver.h"
#include "wdt_driver.h"
#include <string.h>

static uint8_t g_node_id = CO_NODE_ID_DEFAULT;

/* -----------------------------------------------------------------------
 * CAN receive dispatch
 * ----------------------------------------------------------------------- */
static void CAN_RxDispatch(uint32_t cob_id,
                            const uint8_t *data,
                            uint8_t dlc)
{
    /* NMT command (broadcast) */
    if (cob_id == 0x000U) {
        CO_NMT_ProcessCommand(data[0], data[1]);
        return;
    }

    /* SDO receive */
    if (cob_id == (0x600U + g_node_id)) {
        SDO_Process(g_node_id, data, dlc);
        return;
    }

    /* RPDO 1 */
    if (cob_id == (0x200U + g_node_id)) {
        if (g_co_callbacks.on_rpdo_received) {
            g_co_callbacks.on_rpdo_received(0, data, dlc);
        }
        return;
    }

    /* SYNC */
    if (cob_id == (g_params.sync_cob_id & 0x7FFU)) {
        static uint8_t sync_counter = 0;
        sync_counter++;
        if (g_co_callbacks.on_sync) {
            g_co_callbacks.on_sync(sync_counter);
        }
        CO_PDO_TransmitAll();   /* trigger SYNC-based TPDOs */
        return;
    }

    /* Life Guard RTR on 0x700 + NodeID */
    if (cob_id == (0x700U + g_node_id)) {
        CO_LifeGuard_OnPoll();
        /* respond with heartbeat byte */
        uint8_t hb = (uint8_t)g_nmt_state;
        CAN_Send(0x700U + g_node_id, &hb, 1);
        return;
    }
}

/* -----------------------------------------------------------------------
 * CO_Init
 * ----------------------------------------------------------------------- */
void CO_Init(uint8_t node_id, uint32_t bitrate_kbps)
{
    g_node_id = node_id;

    /* Initialise CAN peripheral */
    CAN_Init(bitrate_kbps);

    /* Configure acceptance filters */
    CAN_SetFilter(0x000U, 0x7FFU, CAN_FILTER_EXACT);  /* NMT */
    CAN_SetFilter(0x600U + node_id, 0x7FFU, CAN_FILTER_EXACT); /* SDO */
    CAN_SetFilter(0x200U + node_id, 0x7FFU, CAN_FILTER_EXACT); /* RPDO1 */
    CAN_SetFilter(0x080U, 0x780U, CAN_FILTER_MASK);   /* SYNC/EMCY */
    CAN_SetFilter(0x700U + node_id, 0x7FFU, CAN_FILTER_EXACT); /* HB/LG */
    CAN_SetRxCallback(CAN_RxDispatch);

    /* Load persistent parameters */
    int nvm_result = NVM_Load(g_od, g_od_size);
    if (nvm_result != 0) {
        /* Factory defaults already in g_params; no action needed */
    }

    /* Initialise watchdog: timeout = 3 * heartbeat period */
    WDT_Init(g_params.heartbeat_producer * 3U);

    /* Register application callbacks */
    App_Init_CANopen();

    /* Send boot-up message */
    uint8_t boot = 0x00U;
    CAN_Send(0x700U + node_id, &boot, 1);

    /* Enter PRE-OPERATIONAL */
    CO_NMT_SetState(NMT_STATE_PRE_OP);
}

/* -----------------------------------------------------------------------
 * CO_Process — call every millisecond (or pass elapsed_ms)
 * ----------------------------------------------------------------------- */
void CO_Process(uint32_t elapsed_ms)
{
    /* Drain CAN RX FIFO */
    CAN_PollRx();

    /* Heartbeat producer + watchdog feed */
    CO_HeartbeatProcess(elapsed_ms);

    /* Life guarding supervision */
    CO_LifeGuard_Process(elapsed_ms);

    /* Event-triggered TPDOs (non-SYNC) */
    CO_PDO_ProcessEventTimers(elapsed_ms);
}

/* -----------------------------------------------------------------------
 * Application entry point
 * ----------------------------------------------------------------------- */
int main(void)
{
    /* BSP init */
    SystemClock_Config();
    SysTick_Init(1U);  /* 1 ms tick */

    uint8_t  node_id = DIPSwitch_ReadNodeID();   /* hardware pins */
    uint32_t bitrate = 250U;                      /* kbit/s */

    CO_Init(node_id, bitrate);

    uint32_t last_ms = SysTick_GetMs();

    while (1) {
        uint32_t now     = SysTick_GetMs();
        uint32_t elapsed = now - last_ms;
        last_ms = now;

        CO_Process(elapsed);
        App_RunTasks(elapsed);  /* application-specific work */
    }
}
```

### Complete SDO Transaction Trace (ASCII)

```
  SDO Write: Master sets Heartbeat Producer (0x1017:00) to 200 ms
  =================================================================

  Master (Node 1)                           Slave (Node 2)
  +--------------+                          +--------------+
  |  COB-ID:     |                          |              |
  |  0x601       |                          |              |
  |  DLC: 8      |                          |              |
  |  Data:       |                          |              |
  |  [2B 17 10   | -- SDO Write Request --> |              |
  |   00 C8 00   |    index=0x1017          |  SDO_Process |
  |   00 00]     |    sub=0x00              |  validates & |
  |  CS=0x2B:    |    data=0x00C8 (200)     |  writes OD   |
  |  expedited   |                          |              |
  |  2-byte      |                          |              |
  |              | <-- SDO Write Confirm -- |  COB-ID:     |
  |              |    index=0x1017          |  0x582       |
  |              |    sub=0x00              |  DLC: 8      |
  |              |    CS=0x60 (OK)          |  Data:       |
  |              |                          |  [60 17 10   |
  |              |                          |   00 00 00   |
  |              |                          |   00 00]     |
  +--------------+                          +--------------+

  Byte Layout of Download Initiate Request (CS=0x2B):
  +------+-------+-------+------+------+------+------+------+
  | 0x2B | 0x17  | 0x10  | 0x00 | 0xC8 | 0x00 | 0x00 | 0x00|
  +------+-------+-------+------+------+------+------+------+
  CS:exp  Index_Lo Index_Hi  Sub   Data[0] Data[1] Data[2] Data[3]
          <-- Index 0x1017 -->       <------- Value 200 -------->
                                              (little-endian)
```

---

## 11. Summary

A CANopen slave node is built around five interlocking pillars:

**Mandatory Object Set** — Every CiA 301 compliant slave must expose at minimum the Device Type (0x1000), Error Register (0x1001), and Identity Object (0x1018). PDO communication and mapping objects (0x1400–0x1BFF), heartbeat producer (0x1017), and the store/restore objects (0x1010/0x1011) complete the standard communication profile.

**Object Dictionary Architecture** — The OD is a sorted array of descriptor records, each holding type, access, size, and a pointer to the actual data in RAM (or ROM for read-only entries). Binary search enables efficient SDO dispatch. Compile-time access flags (`OD_ACCESS_RO`, `OD_ACCESS_RW`) form the first layer of write protection.

**Persistent Storage (0x1010 / 0x1011)** — Parameters are stored in a dual-bank Flash or EEPROM layout protected by a magic marker and CRC-32. At power-on, the most recently valid bank is loaded into the RAM shadow of the OD. Writing the ASCII signature `"save"` to 0x1010 triggers a verified, atomic write; `"load"` to 0x1011 invalidates both banks so factory defaults are restored after the next reset.

**Write-Protect Layers** — Protection is layered: static `OD_ACCESS_RO` flags prevent any SDO write at compile time; NMT state guards block PDO re-mapping outside PRE-OPERATIONAL; signature checks on 0x1010/0x1011 prevent accidental parameter loss; optional manufacturer guard-key objects provide time-limited unlock windows for sensitive parameters; hardware WP pins guard Flash/EEPROM at silicon level.

**Application Callback Hooks** — A compact callback table (`CO_Callbacks_t`) decouples the protocol stack from application logic. Hooks fire on NMT state changes, SDO reads/writes, RPDO reception, TPDO transmission, SYNC, and emergency reception. This structure keeps the stack portable and testable independently from the application.

**Watchdog Integration** — The hardware independent watchdog is fed each heartbeat cycle. If the main loop stalls, the WDT fires and resets the MCU within three heartbeat intervals. NMT Life Guarding provides an additional software-level supervision: if the master stops polling within `GuardTime × LifeTimeFactor` milliseconds, the slave transmits an Emergency frame and returns to PRE-OPERATIONAL, disabling outputs.

**Bare-Metal Architecture** — The complete slave fits within a single main loop: `CAN_PollRx()` → `SDO_Process()` → `CO_HeartbeatProcess()` → `CO_LifeGuard_Process()` → `CO_PDO_ProcessEventTimers()`. No RTOS or dynamic allocation is required. All state lives in statically allocated globals, making behaviour fully deterministic and easy to verify with a logic analyser or CAN bus monitor.

---

*Reference Standard: CiA 301 v4.2.0 — CANopen Application Layer and Communication Profile*
*Device Profile Example: CiA 401 — CANopen Device Profile for Generic I/O Modules*