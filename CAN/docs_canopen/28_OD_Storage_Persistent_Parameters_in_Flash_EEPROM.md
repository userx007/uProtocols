Now I have everything I need. Let me create the comprehensive markdown document.The document covers all the key areas of **CANopen OD Storage** in one comprehensive file:

**Structure overview:**

1. **Protocol** — Objects 0x1010 (store) and 0x1011 (restore), their sub-indices for selective group saves, and the SDO read/write interaction sequence shown in ASCII

2. **Magic Signature `0x65766173`** — Byte-level breakdown of the "save" little-endian trick, with the matching "load" (`0x64616F6C`) for restore, including C constants and SDO handlers

3. **Restore Factory Defaults** — NVM invalidation flow, why reset is required, and the two-step "invalidate header → reset → load defaults" sequence

4. **EEPROM Strategy** — Simple linear writes for infrequent params, plus a full circular slot log with sequence numbers for frequently-written values (wear-levelling)

5. **Flash Ping-Pong & Ring Buffer** — Dual-bank atomic save (write inactive → validate → flip pointer), and a multi-page ring for small-but-frequent payloads, both with ASCII diagrams

6. **CRC-32 Validation** — Full table-driven CRC-32 implementation, dual-CRC (header + payload) for maximum robustness, and the boot-time validation flowchart

7. **Firmware Migration** — Version-stamped headers, the upgrade/downgrade decision tree, and a concrete V1→V2 migration example showing struct layout diffs and default-filling

8. **Complete module** — `od_storage.h` + `od_storage.c` + `main.c` boot sequence tying everything together

9. **Summary box** — Quick-reference ASCII table covering all key values, strategies, and common pitfalls# OD Storage — Persistent Parameters in Flash/EEPROM

# 28. OD Storage — Persistent Parameters in Flash/EEPROM

**CANopen Application Note | Embedded Systems Series**

---

## Table of Contents

1. [Introduction](#introduction)
2. [CANopen Standard: Object 0x1010 and 0x1011](#canopen-standard)
3. [Store Command Signature (0x65766173)](#store-command-signature)
4. [Restore Factory Defaults](#restore-factory-defaults)
5. [Storage Architecture Overview](#storage-architecture)
6. [EEPROM Storage Strategy](#eeprom-storage-strategy)
7. [Flash Storage and Wear-Levelling](#flash-storage-and-wear-levelling)
8. [CRC Validation of Stored Data](#crc-validation)
9. [Migration Between Firmware Versions](#migration-between-firmware-versions)
10. [Complete Implementation Examples](#complete-implementation-examples)
11. [Summary](#summary)

---

## 1. Introduction <a name="introduction"></a>

In CANopen networks, many parameters — PID gains, node addresses, communication parameters,
application-specific setpoints — must survive power cycles. The CANopen standard defines a
structured mechanism for persisting Object Dictionary (OD) entries to non-volatile memory (NVM),
whether that is byte-addressable EEPROM or page-erased Flash.

This document covers:

- The CANopen store/restore protocol (objects 0x1010 / 0x1011)
- The magic signature `0x65766173` ("save" in little-endian ASCII)
- EEPROM vs. Flash persistence strategies
- Wear-levelling for Flash-backed OD storage
- CRC validation to detect corruption
- Firmware migration — handling layout changes across versions

---

## 2. CANopen Standard: Object 0x1010 and 0x1011 <a name="canopen-standard"></a>

### Object 0x1010 — Store Parameters

| Sub-index | Name                  | Description                          |
|-----------|-----------------------|--------------------------------------|
| 0x00      | Highest sub-index     | Number of sub-objects                |
| 0x01      | Save all parameters   | Saves all storable OD parameters     |
| 0x02      | Save comm. parameters | Saves communication-related params   |
| 0x03      | Save app. parameters  | Saves application-specific params    |
| 0x04+     | Save manufacturer     | Device-specific storage groups       |

### Object 0x1011 — Restore Default Parameters

| Sub-index | Name                   | Description                         |
|-----------|------------------------|-------------------------------------|
| 0x00      | Highest sub-index      | Number of sub-objects               |
| 0x01      | Restore all parameters | Restores all parameters to defaults |
| 0x02      | Restore comm. params   | Restores communication parameters   |
| 0x03      | Restore app. params    | Restores application parameters     |

**Key rule:** A write to these objects only takes effect if the correct magic value is written.
Reads always return the device capability (bit 0 = 1 means "save on command supported").

```
Object 0x1010 Write Sequence:
==============================

  Master                             Device
    |                                   |
    |--- SDO Write 0x1010:01 ---------> |
    |    Value = 0x65766173             |
    |    ("save" in ASCII bytes)        |
    |                                   |
    |                          [Checks magic value]
    |                          [Triggers NVM write]
    |                          [Returns SDO ack]
    |                                   |
    |<-- SDO Ack ---------------------- |
    |                                   |
```

---

## 3. Store Command Signature (0x65766173) <a name="store-command-signature"></a>

The value `0x65766173` is the ASCII string `"save"` stored in little-endian byte order:

```
Magic Value Breakdown:
======================

  Hex:   0x65  0x76  0x61  0x73
  ASCII:  'e'   'v'   'a'   's'

  As 32-bit little-endian integer: 0x65766173
                                        ^
                                   "save" reversed

  Memory layout (little-endian CPU):
  Address+0: 0x73  's'
  Address+1: 0x61  'a'
  Address+2: 0x76  'v'
  Address+3: 0x65  'e'
```

This intentional design prevents accidental writes. A random SDO write or bus glitch is
extremely unlikely to produce exactly `0x65766173`.

### C Implementation: Checking the Signature

```c
#include <stdint.h>
#include <stdbool.h>

#define OD_STORE_SIGNATURE    0x65766173UL   /* "save" little-endian */
#define OD_RESTORE_SIGNATURE  0x64616F6CUL   /* "load" little-endian */

/**
 * @brief  Handler for SDO write to Object 0x1010 (Store Parameters)
 * @param  subindex   Sub-index written by master (0x01 = all, 0x02 = comm, etc.)
 * @param  value      32-bit value written by master
 * @return true if storage was triggered, false if signature invalid
 */
bool OD_StoreParameters_Write(uint8_t subindex, uint32_t value)
{
    if (value != OD_STORE_SIGNATURE) {
        /* Wrong magic — ignore, return SDO abort code 0x08000000 */
        return false;
    }

    switch (subindex) {
        case 0x01:
            NVM_SaveAllParameters();
            break;
        case 0x02:
            NVM_SaveCommParameters();
            break;
        case 0x03:
            NVM_SaveAppParameters();
            break;
        default:
            return false;  /* Sub-index not supported */
    }
    return true;
}

/**
 * @brief  Handler for SDO read from Object 0x1010
 * @param  subindex   Sub-index read by master
 * @return Capability word (bit 0 = save-on-cmd, bit 1 = auto-save)
 */
uint32_t OD_StoreParameters_Read(uint8_t subindex)
{
    (void)subindex;
    return 0x00000001UL;  /* Bit 0: "save on command" supported */
}
```

---

## 4. Restore Factory Defaults <a name="restore-factory-defaults"></a>

The restore magic is `0x64616F6C` — ASCII `"load"` in little-endian. Writing this to
Object 0x1011 instructs the device to invalidate the stored NVM image. On the next
power cycle (or after a reset), the firmware loads its compiled-in default values.

```
Restore Sequence:
=================

  Master                             Device
    |                                   |
    |--- SDO Write 0x1011:01 ---------> |
    |    Value = 0x64616F6C ("load")    |
    |                                   |
    |                       [Invalidates NVM header]
    |                       [Does NOT immediately    ]
    |                       [overwrite flash/eeprom  ]
    |                                   |
    |<-- SDO Ack ---------------------- |
    |                                   |
    |--- NMT Reset Node --------------> |
    |                                   |
    |                       [Boot: NVM invalid]
    |                       [Load ROM defaults]
    |                                   |
```

### C Implementation: Restore Handler

```c
#define OD_RESTORE_SIGNATURE  0x64616F6CUL  /* "load" little-endian */

/* A small flag in a dedicated NVM location indicates validity */
#define NVM_VALID_MARKER    0xA55AA55AUL
#define NVM_INVALID_MARKER  0xDEADBEEFUL

bool OD_RestoreDefaults_Write(uint8_t subindex, uint32_t value)
{
    if (value != OD_RESTORE_SIGNATURE) {
        return false;
    }

    switch (subindex) {
        case 0x01:
            /* Invalidate the NVM header — defaults load on next boot */
            NVM_InvalidateHeader(NVM_GROUP_ALL);
            break;
        case 0x02:
            NVM_InvalidateHeader(NVM_GROUP_COMM);
            break;
        case 0x03:
            NVM_InvalidateHeader(NVM_GROUP_APP);
            break;
        default:
            return false;
    }
    return true;
}

void NVM_InvalidateHeader(uint8_t group)
{
    NvmHeader_t hdr;
    NVM_ReadHeader(group, &hdr);
    hdr.valid_marker = NVM_INVALID_MARKER;
    NVM_WriteHeader(group, &hdr);
}

/**
 * @brief  Called at boot. Loads NVM if valid, otherwise loads ROM defaults.
 */
void OD_LoadParameters(void)
{
    NvmHeader_t hdr;
    NVM_ReadHeader(NVM_GROUP_ALL, &hdr);

    if (hdr.valid_marker == NVM_VALID_MARKER &&
        NVM_ValidateCRC(NVM_GROUP_ALL))
    {
        OD_DeserializeFromNVM();
    }
    else
    {
        OD_LoadROMDefaults();
    }
}
```

---

## 5. Storage Architecture Overview <a name="storage-architecture"></a>

```
NVM Layout — Two Storage Groups:
=================================

  NVM Address Space
  +--------------------------------------------------+
  |  HEADER BLOCK (fixed location)                   |
  |  +--------------------------------------------+  |
  |  | Magic:   0xA55AA55A  (valid marker)        |  |
  |  | Version: 0x0003      (firmware layout ver) |  |
  |  | Group:   0x01        (which group)         |  |
  |  | Length:  0x01A0      (bytes of payload)    |  |
  |  | CRC32:   0xDEAD1234  (payload CRC)         |  |
  |  +--------------------------------------------+  |
  |                                                  |
  |  COMM PARAMETERS (0x1000-0x1FFF range objects)   |
  |  +--------------------------------------------+  |
  |  | NodeID   | Baudrate | Heartbeat | ...       |  |
  |  +--------------------------------------------+  |
  |                                                  |
  |  APP PARAMETERS (0x2000+ manufacturer objects)   |
  |  +--------------------------------------------+  |
  |  | Setpoint | Gain_P | Gain_I | Gain_D | ...  |  |
  |  +--------------------------------------------+  |
  +--------------------------------------------------+

  Two separate save groups allow:
  - Restoring only comm params without touching app params
  - Updating app firmware without losing network config
```

---

## 6. EEPROM Storage Strategy <a name="eeprom-storage-strategy"></a>

EEPROM is byte-addressable and self-erasing on write. It tolerates roughly **100,000 to
1,000,000 write cycles** per byte location. For frequently-changing parameters (e.g. position
setpoints updated every second), a simple linear write will exhaust EEPROM within days.

### Simple EEPROM Layout (for infrequently-written params)

```c
#include <stdint.h>
#include <string.h>

/* Example: I2C EEPROM (AT24C256 = 32KB) */
#define EEPROM_BASE_ADDR     0x0000
#define EEPROM_HEADER_SIZE   16
#define EEPROM_MAX_PAYLOAD   512

typedef struct __attribute__((packed)) {
    uint32_t valid_marker;   /* 0xA55AA55A when valid      */
    uint16_t version;        /* Firmware layout version    */
    uint16_t length;         /* Payload byte count         */
    uint32_t crc32;          /* CRC32 of payload           */
    uint32_t reserved;
} NvmHeader_t;

typedef struct __attribute__((packed)) {
    uint8_t  node_id;
    uint16_t heartbeat_ms;
    uint32_t baudrate;
    /* ... more OD entries ... */
    int32_t  setpoint;
    float    gain_p;
    float    gain_i;
    float    gain_d;
} NvmPayload_t;

/**
 * @brief  Save OD parameters to EEPROM (simple, non-wear-levelled)
 */
bool NVM_SaveToEEPROM(void)
{
    NvmPayload_t payload;
    NvmHeader_t  hdr;

    /* Collect current OD values into payload struct */
    payload.node_id      = OD_GetNodeID();
    payload.heartbeat_ms = OD_GetHeartbeat();
    payload.baudrate     = OD_GetBaudrate();
    payload.setpoint     = OD_GetSetpoint();
    payload.gain_p       = OD_GetGainP();
    payload.gain_i       = OD_GetGainI();
    payload.gain_d       = OD_GetGainD();

    /* Fill header */
    hdr.valid_marker = NVM_VALID_MARKER;
    hdr.version      = FIRMWARE_LAYOUT_VERSION;
    hdr.length       = sizeof(NvmPayload_t);
    hdr.crc32        = CRC32_Calculate((uint8_t*)&payload, sizeof(payload));
    hdr.reserved     = 0;

    /* Write payload first, then header (atomic-ish: header last) */
    if (!EEPROM_Write(EEPROM_BASE_ADDR + EEPROM_HEADER_SIZE,
                      (uint8_t*)&payload, sizeof(payload))) {
        return false;
    }
    if (!EEPROM_Write(EEPROM_BASE_ADDR,
                      (uint8_t*)&hdr, sizeof(hdr))) {
        return false;
    }
    return true;
}
```

### EEPROM Wear-Levelling for Frequently-Written Values

For values written often, use a **circular log** approach. Keep N slots and rotate:

```
EEPROM Circular Log — Wear-Levelling:
======================================

  Slot 0    Slot 1    Slot 2    Slot 3    Slot 4   ...  Slot N-1
  +------+  +------+  +------+  +------+  +------+      +------+
  |seq=1 |  |seq=2 |  |seq=3 |  |EMPTY |  |EMPTY |      |EMPTY |
  |data..|  |data..|  |data..|  |      |  |      |      |      |
  |crc.  |  |crc.  |  |crc.  |  |      |  |      |      |crc.  |
  +------+  +------+  +------+  +------+  +------+      +------+
                          ^
                     Current (highest valid seq)

  On next write: Slot 3 gets seq=4, becomes current.
  Spreads writes across N*100k cycles instead of 100k.
```

```c
#define EEPROM_SLOT_COUNT   64
#define EEPROM_SLOT_SIZE    32  /* bytes per slot (header + small payload) */

typedef struct __attribute__((packed)) {
    uint32_t seq_num;    /* Monotonically increasing sequence */
    uint16_t length;
    uint16_t crc16;
    uint8_t  data[24];   /* Payload (padded to slot size)     */
} EepromSlot_t;

/**
 * @brief  Find the most recent valid slot (highest sequence number)
 * @return Slot index, or -1 if no valid slot found
 */
int EEPROM_FindCurrentSlot(void)
{
    uint32_t best_seq  = 0;
    int      best_slot = -1;
    EepromSlot_t slot;

    for (int i = 0; i < EEPROM_SLOT_COUNT; i++) {
        uint32_t addr = i * EEPROM_SLOT_SIZE;
        EEPROM_Read(addr, (uint8_t*)&slot, sizeof(slot));

        uint16_t calc_crc = CRC16_Calculate(slot.data, slot.length);
        if (calc_crc == slot.crc16 && slot.seq_num > best_seq) {
            best_seq  = slot.seq_num;
            best_slot = i;
        }
    }
    return best_slot;
}

/**
 * @brief  Write to the next slot in the circular log
 */
bool EEPROM_WriteNextSlot(const uint8_t *data, uint16_t len)
{
    int cur = EEPROM_FindCurrentSlot();
    int next = (cur < 0) ? 0 : ((cur + 1) % EEPROM_SLOT_COUNT);

    EepromSlot_t slot;
    memset(&slot, 0xFF, sizeof(slot));

    /* Read current to get sequence number */
    if (cur >= 0) {
        EepromSlot_t cur_slot;
        EEPROM_Read(cur * EEPROM_SLOT_SIZE, (uint8_t*)&cur_slot, sizeof(cur_slot));
        slot.seq_num = cur_slot.seq_num + 1;
    } else {
        slot.seq_num = 1;
    }

    slot.length = len;
    memcpy(slot.data, data, len);
    slot.crc16 = CRC16_Calculate(slot.data, slot.length);

    return EEPROM_Write(next * EEPROM_SLOT_SIZE, (uint8_t*)&slot, sizeof(slot));
}
```

---

## 7. Flash Storage and Wear-Levelling <a name="flash-storage-and-wear-levelling"></a>

Internal MCU Flash is not byte-writable. It must be **erased in pages** (typically 512 B to
64 KB) before writing. Each page tolerates roughly **10,000 to 100,000 erase cycles**.
Writing directly to one page on every parameter save will wear it out rapidly.

```
Flash Page Structure:
======================

  Flash (internal MCU)

  Page 0          Page 1          Page 2          Page 3
  (4 KB each)     (4 KB each)     (4 KB each)     (4 KB each)
  +------------+  +------------+  +------------+  +------------+
  | ERASED     |  | seq=1      |  | seq=2      |  | seq=3  <-- |
  | 0xFFFFFFFF |  | payload... |  | payload... |  | payload... |
  |            |  | CRC OK     |  | CRC OK     |  | CRC OK     |
  +------------+  +------------+  +------------+  +------------+

  Active slot = Page 3 (highest valid sequence)

  On next save:
    - All pages full? Erase Page 0, write seq=4 there.
    - Otherwise write to next erased page.
```

### Flash Wear-Levelling with Two Banks

For OD storage, a **dual-bank (ping-pong)** approach is the simplest viable strategy:

```
Ping-Pong Flash Strategy:
==========================

  Bank A (Pages 0-3)         Bank B (Pages 4-7)
  +-------------------+      +-------------------+
  |  Version: 3       |      |  ERASED / INVALID  |
  |  Seq:     100     |      |                    |
  |  CRC:  0xABCD     |      |                    |
  |  [OD data...]     |      |                    |
  +-------------------+      +-------------------+
           ^
      Active Bank

  Save Triggered:
    1. Write new data to Bank B
    2. Validate Bank B CRC
    3. If OK: mark Bank A invalid (or erase)
    4. Bank B becomes active

  This way: Bank A or Bank B always holds valid data.
  If power loss occurs mid-write, Bank A is still intact.
```

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define FLASH_PAGE_SIZE       4096U   /* 4 KB pages (STM32 example) */
#define FLASH_BANK_A_ADDR     0x08060000UL
#define FLASH_BANK_B_ADDR     0x08061000UL
#define FLASH_BANK_SIZE       FLASH_PAGE_SIZE

typedef struct __attribute__((packed)) {
    uint32_t valid_marker;
    uint32_t sequence;
    uint16_t version;
    uint16_t length;
    uint32_t crc32;
} FlashHeader_t;

#define FLASH_PAYLOAD_OFFSET  sizeof(FlashHeader_t)
#define FLASH_MAX_PAYLOAD     (FLASH_PAGE_SIZE - sizeof(FlashHeader_t))

static uint32_t s_active_bank = FLASH_BANK_A_ADDR;

/**
 * @brief  Determine which bank holds the most recent valid data
 */
void Flash_SelectActiveBank(void)
{
    FlashHeader_t hdrA, hdrB;
    memcpy(&hdrA, (void*)FLASH_BANK_A_ADDR, sizeof(hdrA));
    memcpy(&hdrB, (void*)FLASH_BANK_B_ADDR, sizeof(hdrB));

    bool validA = (hdrA.valid_marker == NVM_VALID_MARKER);
    bool validB = (hdrB.valid_marker == NVM_VALID_MARKER);

    /* Validate CRC */
    if (validA) {
        uint8_t *payloadA = (uint8_t*)(FLASH_BANK_A_ADDR + FLASH_PAYLOAD_OFFSET);
        validA = (CRC32_Calculate(payloadA, hdrA.length) == hdrA.crc32);
    }
    if (validB) {
        uint8_t *payloadB = (uint8_t*)(FLASH_BANK_B_ADDR + FLASH_PAYLOAD_OFFSET);
        validB = (CRC32_Calculate(payloadB, hdrB.length) == hdrB.crc32);
    }

    if (validA && validB) {
        /* Both valid — pick higher sequence */
        s_active_bank = (hdrA.sequence >= hdrB.sequence)
                        ? FLASH_BANK_A_ADDR
                        : FLASH_BANK_B_ADDR;
    } else if (validA) {
        s_active_bank = FLASH_BANK_A_ADDR;
    } else if (validB) {
        s_active_bank = FLASH_BANK_B_ADDR;
    } else {
        /* Neither valid — will load ROM defaults */
        s_active_bank = 0;
    }
}

/**
 * @brief  Save OD payload to the inactive bank, then flip active pointer
 */
bool Flash_SaveOD(const uint8_t *payload, uint16_t len)
{
    /* Select inactive bank */
    uint32_t write_bank = (s_active_bank == FLASH_BANK_A_ADDR)
                          ? FLASH_BANK_B_ADDR
                          : FLASH_BANK_A_ADDR;

    /* Build header */
    FlashHeader_t hdr;
    FlashHeader_t active_hdr;
    if (s_active_bank != 0) {
        memcpy(&active_hdr, (void*)s_active_bank, sizeof(active_hdr));
        hdr.sequence = active_hdr.sequence + 1;
    } else {
        hdr.sequence = 1;
    }
    hdr.valid_marker = NVM_VALID_MARKER;
    hdr.version      = FIRMWARE_LAYOUT_VERSION;
    hdr.length       = len;
    hdr.crc32        = CRC32_Calculate(payload, len);

    /* Erase inactive bank */
    if (!Flash_ErasePage(write_bank)) {
        return false;
    }

    /* Write payload first, then header */
    if (!Flash_Write(write_bank + FLASH_PAYLOAD_OFFSET, payload, len)) {
        return false;
    }
    if (!Flash_Write(write_bank, (uint8_t*)&hdr, sizeof(hdr))) {
        return false;
    }

    /* Verify written data */
    uint8_t *written = (uint8_t*)(write_bank + FLASH_PAYLOAD_OFFSET);
    if (CRC32_Calculate(written, len) != hdr.crc32) {
        return false;  /* Write verify failed */
    }

    /* Invalidate old bank and flip pointer */
    Flash_InvalidateHeader(s_active_bank);
    s_active_bank = write_bank;

    return true;
}
```

### Multi-Page Wear-Levelling (for small payloads, high write frequency)

When payload is small but writes are frequent, use multiple pages as a circular ring:

```
Ring Buffer of Flash Pages:
============================

  Page 0    Page 1    Page 2    Page 3    Page 4    Page 5
  +------+  +------+  +------+  +------+  +------+  +------+
  |seq=1 |  |seq=2 |  |seq=3 |  |seq=4 |  |BLANK |  |BLANK |
  |      |  |      |  |      |  |  <-- |  |      |  |      |
  +------+  +------+  +------+  +------+  +------+  +------+
                                    ^
                               Current write

  When all pages are written:
    Erase Page 0 (oldest), write seq=7 there.
    Rotate pointer to Page 1 as oldest.

  Write distribution across N pages:
    Total writes before wear = N * 10,000 erase cycles
    For N=8 pages: 80,000 OD saves supported
```

---

## 8. CRC Validation of Stored Data <a name="crc-validation"></a>

Every stored block must carry a checksum. CANopen implementations typically use CRC-32
(IEEE 802.3 polynomial) or CRC-16-CCITT. The checksum is computed over the raw payload
bytes and stored in the header.

```
CRC Validation Flow:
====================

  Boot
    |
    v
  Read Header from NVM
    |
    +--[valid_marker != 0xA55AA55A?]--> Load ROM Defaults --> Done
    |
    v
  Read Payload
    |
    v
  Calculate CRC32(payload, length)
    |
    +--[calc_crc != stored_crc?]------> Log Error
    |                                   Load ROM Defaults --> Done
    v
  CRC OK
    |
    v
  Deserialize Payload into OD
    |
    v
  Done — OD loaded from NVM
```

### CRC-32 Implementation

```c
#include <stdint.h>
#include <stddef.h>

/* CRC-32 (IEEE 802.3) polynomial */
#define CRC32_POLYNOMIAL  0xEDB88320UL

static uint32_t s_crc32_table[256];
static bool     s_crc32_table_ready = false;

/**
 * @brief  Build the CRC-32 lookup table (call once at startup)
 */
void CRC32_InitTable(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
        s_crc32_table[i] = crc;
    }
    s_crc32_table_ready = true;
}

/**
 * @brief  Calculate CRC-32 over a data buffer
 * @param  data   Pointer to data
 * @param  len    Number of bytes
 * @return 32-bit CRC value
 */
uint32_t CRC32_Calculate(const uint8_t *data, size_t len)
{
    if (!s_crc32_table_ready) {
        CRC32_InitTable();
    }

    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        crc = (crc >> 8) ^ s_crc32_table[(crc ^ byte) & 0xFFU];
    }
    return crc ^ 0xFFFFFFFFUL;
}

/**
 * @brief  Validate NVM block integrity
 * @return true if data is valid
 */
bool NVM_ValidateCRC(const NvmHeader_t *hdr, const uint8_t *payload)
{
    if (hdr->valid_marker != NVM_VALID_MARKER) {
        return false;
    }
    if (hdr->length == 0 || hdr->length > FLASH_MAX_PAYLOAD) {
        return false;
    }
    uint32_t calc = CRC32_Calculate(payload, hdr->length);
    return (calc == hdr->crc32);
}
```

### Dual-CRC Strategy (Header + Payload)

For extra robustness, protect both header and payload independently:

```c
typedef struct __attribute__((packed)) {
    uint32_t valid_marker;
    uint32_t sequence;
    uint16_t version;
    uint16_t length;
    uint32_t payload_crc32;   /* CRC of payload bytes only         */
    uint16_t header_crc16;    /* CRC of all fields above this one  */
} NvmHeaderV2_t;

bool NVM_ValidateV2(const NvmHeaderV2_t *hdr, const uint8_t *payload)
{
    /* Check header integrity first */
    uint16_t hdr_crc = CRC16_Calculate((uint8_t*)hdr,
                                        offsetof(NvmHeaderV2_t, header_crc16));
    if (hdr_crc != hdr->header_crc16) {
        return false;  /* Header itself is corrupt */
    }

    /* Then check payload */
    if (hdr->valid_marker != NVM_VALID_MARKER) {
        return false;
    }
    uint32_t pay_crc = CRC32_Calculate(payload, hdr->length);
    return (pay_crc == hdr->payload_crc32);
}
```

---

## 9. Migration Between Firmware Versions <a name="migration-between-firmware-versions"></a>

When firmware is updated, the OD layout often changes: new objects are added, sub-indices
are removed, data types change. A naive load of old NVM data into a new firmware's OD will
silently corrupt parameters.

### Version Tagging Strategy

Every NVM block stores a **layout version** number. On load, the firmware compares the
stored version against its own compiled-in `FIRMWARE_LAYOUT_VERSION`.

```
Migration Decision Tree:
=========================

  Boot — Read NVM Header
             |
             v
    stored_version == CURRENT_VERSION?
             |
    YES      |      NO
     |       |       |
     v               v
  Load          stored_version > CURRENT_VERSION?
  directly           |
  (normal)     YES   |   NO
                |         |
                v         v
           REJECT:    Migration Needed
           newer fw   (upgrade path)
           data in
           old MCU
```

```c
#define FIRMWARE_LAYOUT_VERSION  3U   /* Increment on any OD layout change */

typedef enum {
    MIGRATION_OK         = 0,
    MIGRATION_UPGRADED   = 1,
    MIGRATION_DOWNGRADE  = 2,
    MIGRATION_CORRUPT    = 3,
    MIGRATION_NO_DATA    = 4
} MigrationResult_t;

/**
 * @brief  Migrate NVM data from an older layout version to current
 * @param  old_version  Version found in NVM header
 * @param  raw_data     Pointer to raw NVM payload
 * @param  len          Length of raw_data
 * @return Migration result code
 */
MigrationResult_t NVM_Migrate(uint16_t old_version,
                               const uint8_t *raw_data,
                               uint16_t len)
{
    if (old_version == FIRMWARE_LAYOUT_VERSION) {
        /* No migration needed */
        return MIGRATION_OK;
    }

    if (old_version > FIRMWARE_LAYOUT_VERSION) {
        /* NVM was written by a newer firmware — dangerous to load */
        OD_LoadROMDefaults();
        return MIGRATION_DOWNGRADE;
    }

    /* Apply upgrade steps in sequence */
    if (old_version < 2) {
        NVM_MigrateV1toV2(raw_data, len);
    }
    if (old_version < 3) {
        NVM_MigrateV2toV3(raw_data, len);
    }
    /* Add future steps here: if (old_version < 4) ... */

    return MIGRATION_UPGRADED;
}
```

### Migration Step Example: V1 → V2

In version 2, suppose `gain_d` (float, 4 bytes) was added after `gain_i`.
Old V1 payload had no `gain_d` field.

```c
typedef struct __attribute__((packed)) {
    uint8_t  node_id;
    uint16_t heartbeat_ms;
    uint32_t baudrate;
    int32_t  setpoint;
    float    gain_p;
    float    gain_i;
    /* V1 ends here — no gain_d */
} NvmPayloadV1_t;

typedef struct __attribute__((packed)) {
    uint8_t  node_id;
    uint16_t heartbeat_ms;
    uint32_t baudrate;
    int32_t  setpoint;
    float    gain_p;
    float    gain_i;
    float    gain_d;       /* NEW in V2 */
} NvmPayloadV2_t;

void NVM_MigrateV1toV2(const uint8_t *v1_data, uint16_t v1_len)
{
    if (v1_len < sizeof(NvmPayloadV1_t)) {
        OD_LoadROMDefaults();
        return;
    }

    const NvmPayloadV1_t *v1 = (const NvmPayloadV1_t*)v1_data;

    /* Copy known fields */
    OD_SetNodeID(v1->node_id);
    OD_SetHeartbeat(v1->heartbeat_ms);
    OD_SetBaudrate(v1->baudrate);
    OD_SetSetpoint(v1->setpoint);
    OD_SetGainP(v1->gain_p);
    OD_SetGainI(v1->gain_i);

    /* New field — apply safe default */
    OD_SetGainD(0.0f);   /* ROM default for new parameter */
}
```

### Migration Layout Visualised

```
NVM V1 Payload Layout:
========================

  Offset  Field           Type    Bytes
  ------  -----           ----    -----
  0x00    node_id         u8       1
  0x01    heartbeat_ms    u16      2
  0x03    baudrate        u32      4
  0x07    setpoint        i32      4
  0x0B    gain_p          f32      4
  0x0F    gain_i          f32      4
                                 ---
                          Total: 19 bytes

NVM V2 Payload Layout:
========================

  Offset  Field           Type    Bytes
  ------  -----           ----    -----
  0x00    node_id         u8       1
  0x01    heartbeat_ms    u16      2
  0x03    baudrate        u32      4
  0x07    setpoint        i32      4
  0x0B    gain_p          f32      4
  0x0F    gain_i          f32      4
  0x13    gain_d          f32      4   <-- ADDED
                                 ---
                          Total: 23 bytes

  Migration: copy 0x00..0x12 directly, set 0x13..0x16 = 0.0f (default)
```

---

## 10. Complete Implementation Examples <a name="complete-implementation-examples"></a>

### 10.1 Full OD Storage Module Header

```c
/* od_storage.h */
#ifndef OD_STORAGE_H
#define OD_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

/* Magic values per CANopen standard */
#define OD_STORE_SIGNATURE      0x65766173UL  /* "save" */
#define OD_RESTORE_SIGNATURE    0x64616F6CUL  /* "load" */
#define NVM_VALID_MARKER        0xA55AA55AUL
#define NVM_INVALID_MARKER      0x00000000UL

/* Layout version — increment when OD structure changes */
#define FIRMWARE_LAYOUT_VERSION  3U

/* Storage group identifiers */
typedef enum {
    NVM_GROUP_ALL  = 0x01,
    NVM_GROUP_COMM = 0x02,
    NVM_GROUP_APP  = 0x03
} NvmGroup_t;

/* Public API */
void            OD_Storage_Init(void);
bool            OD_Storage_Save(NvmGroup_t group);
bool            OD_Storage_Load(NvmGroup_t group);
void            OD_Storage_InvalidateGroup(NvmGroup_t group);
uint32_t        OD_Storage_GetCapability(void);

/* SDO write handlers (called by CANopen stack) */
bool            OD_1010_WriteHandler(uint8_t subindex, uint32_t value);
bool            OD_1011_WriteHandler(uint8_t subindex, uint32_t value);
uint32_t        OD_1010_ReadHandler(uint8_t subindex);
uint32_t        OD_1011_ReadHandler(uint8_t subindex);

#endif /* OD_STORAGE_H */
```

### 10.2 Full OD Storage Module Implementation

```c
/* od_storage.c */
#include "od_storage.h"
#include "nvm_driver.h"   /* Platform-specific Flash/EEPROM driver */
#include "od.h"           /* Object Dictionary access functions     */
#include "crc32.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  NVM Header / Payload types                                         */
/* ------------------------------------------------------------------ */

typedef struct __attribute__((packed)) {
    uint32_t valid_marker;
    uint32_t sequence;
    uint16_t version;
    uint16_t length;
    uint32_t crc32;
} NvmHeader_t;

typedef struct __attribute__((packed)) {
    /* Communication parameters (Object 0x1000-0x1FFF) */
    uint8_t  node_id;
    uint16_t heartbeat_producer_ms;
    uint32_t sync_cycle_us;
    /* Application parameters (Object 0x2000+) */
    int32_t  position_setpoint;
    float    ctrl_gain_p;
    float    ctrl_gain_i;
    float    ctrl_gain_d;
    uint16_t ctrl_deadband;
    uint8_t  ctrl_mode;
} NvmPayload_t;

/* ------------------------------------------------------------------ */
/*  Module state                                                       */
/* ------------------------------------------------------------------ */
static bool s_initialized = false;

/* ------------------------------------------------------------------ */
/*  Serialization helpers                                              */
/* ------------------------------------------------------------------ */

static void OD_Serialize(NvmPayload_t *dst)
{
    dst->node_id                = OD_GetU8(0x2000, 0x00);
    dst->heartbeat_producer_ms  = OD_GetU16(0x1017, 0x00);
    dst->sync_cycle_us          = OD_GetU32(0x1006, 0x00);
    dst->position_setpoint      = OD_GetI32(0x2010, 0x01);
    dst->ctrl_gain_p            = OD_GetFloat(0x2011, 0x01);
    dst->ctrl_gain_i            = OD_GetFloat(0x2011, 0x02);
    dst->ctrl_gain_d            = OD_GetFloat(0x2011, 0x03);
    dst->ctrl_deadband          = OD_GetU16(0x2012, 0x01);
    dst->ctrl_mode              = OD_GetU8(0x2013, 0x00);
}

static void OD_Deserialize(const NvmPayload_t *src)
{
    OD_SetU8  (0x2000, 0x00, src->node_id);
    OD_SetU16 (0x1017, 0x00, src->heartbeat_producer_ms);
    OD_SetU32 (0x1006, 0x00, src->sync_cycle_us);
    OD_SetI32 (0x2010, 0x01, src->position_setpoint);
    OD_SetFloat(0x2011, 0x01, src->ctrl_gain_p);
    OD_SetFloat(0x2011, 0x02, src->ctrl_gain_i);
    OD_SetFloat(0x2011, 0x03, src->ctrl_gain_d);
    OD_SetU16 (0x2012, 0x01, src->ctrl_deadband);
    OD_SetU8  (0x2013, 0x00, src->ctrl_mode);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void OD_Storage_Init(void)
{
    NVM_Init();
    CRC32_InitTable();
    s_initialized = true;
}

bool OD_Storage_Save(NvmGroup_t group)
{
    if (!s_initialized) return false;

    NvmPayload_t payload;
    NvmHeader_t  hdr;

    OD_Serialize(&payload);

    hdr.valid_marker = NVM_VALID_MARKER;
    hdr.version      = FIRMWARE_LAYOUT_VERSION;
    hdr.length       = sizeof(NvmPayload_t);
    hdr.crc32        = CRC32_Calculate((uint8_t*)&payload, sizeof(payload));
    hdr.sequence     = NVM_GetNextSequence(group);

    return NVM_WritePingPong(group, &hdr, (uint8_t*)&payload);
}

bool OD_Storage_Load(NvmGroup_t group)
{
    if (!s_initialized) return false;

    NvmHeader_t  hdr;
    NvmPayload_t payload;

    if (!NVM_ReadPingPong(group, &hdr, (uint8_t*)&payload)) {
        /* No valid data — use defaults */
        OD_LoadROMDefaults();
        return false;
    }

    /* Validate */
    uint32_t crc = CRC32_Calculate((uint8_t*)&payload, hdr.length);
    if (crc != hdr.crc32) {
        OD_LoadROMDefaults();
        return false;
    }

    /* Migrate if version mismatch */
    if (hdr.version != FIRMWARE_LAYOUT_VERSION) {
        NVM_Migrate(hdr.version, (uint8_t*)&payload, hdr.length);
        return true;
    }

    OD_Deserialize(&payload);
    return true;
}

void OD_Storage_InvalidateGroup(NvmGroup_t group)
{
    NVM_InvalidateHeader(group);
}

/* ------------------------------------------------------------------ */
/*  SDO Handlers (register with CANopen stack)                        */
/* ------------------------------------------------------------------ */

bool OD_1010_WriteHandler(uint8_t subindex, uint32_t value)
{
    if (value != OD_STORE_SIGNATURE) {
        return false;  /* Stack returns SDO abort 0x08000000 */
    }
    switch (subindex) {
        case 0x01: return OD_Storage_Save(NVM_GROUP_ALL);
        case 0x02: return OD_Storage_Save(NVM_GROUP_COMM);
        case 0x03: return OD_Storage_Save(NVM_GROUP_APP);
        default:   return false;
    }
}

bool OD_1011_WriteHandler(uint8_t subindex, uint32_t value)
{
    if (value != OD_RESTORE_SIGNATURE) {
        return false;
    }
    switch (subindex) {
        case 0x01: OD_Storage_InvalidateGroup(NVM_GROUP_ALL);  return true;
        case 0x02: OD_Storage_InvalidateGroup(NVM_GROUP_COMM); return true;
        case 0x03: OD_Storage_InvalidateGroup(NVM_GROUP_APP);  return true;
        default:   return false;
    }
}

uint32_t OD_1010_ReadHandler(uint8_t subindex)
{
    (void)subindex;
    return 0x00000001UL;   /* Bit 0: "save on command" supported */
}

uint32_t OD_1011_ReadHandler(uint8_t subindex)
{
    (void)subindex;
    return 0x00000001UL;   /* Bit 0: "restore on command" supported */
}
```

### 10.3 Boot Sequence with NVM Loading

```c
/* main.c — relevant boot sequence */
#include "od_storage.h"
#include "canopen.h"

int main(void)
{
    /* 1. Hardware init */
    BSP_Init();
    WDT_Enable(5000U);  /* 5 second watchdog */

    /* 2. Init OD Storage (Flash/EEPROM driver + CRC tables) */
    OD_Storage_Init();

    /* 3. Load OD from NVM (or ROM defaults on first boot / after restore) */
    OD_Storage_Load(NVM_GROUP_ALL);

    /* 4. Start CANopen stack with loaded node ID / baud rate */
    CANopen_Init(OD_GetU8(0x2000, 0x00),  /* Node ID from OD  */
                 OD_GetU32(0x1006, 0x00)); /* Baud from OD     */

    /* 5. Register SDO write handlers */
    CANopen_RegisterSDOWriteHandler(0x1010, OD_1010_WriteHandler);
    CANopen_RegisterSDOReadHandler (0x1010, OD_1010_ReadHandler);
    CANopen_RegisterSDOWriteHandler(0x1011, OD_1011_WriteHandler);
    CANopen_RegisterSDOReadHandler (0x1011, OD_1011_ReadHandler);

    /* 6. Main loop */
    while (1) {
        WDT_Reset();
        CANopen_Process();
        Application_Process();
    }
}
```

---

## 11. Summary <a name="summary"></a>

```
CANopen OD Storage — Key Points at a Glance:
=============================================

  PROTOCOL
  --------
  Object 0x1010 (Store)   : Write 0x65766173 ("save") to trigger NVM write
  Object 0x1011 (Restore) : Write 0x64616F6C ("load") to invalidate NVM
  Both objects read back   : capability word (bit 0 = save-on-cmd supported)
  Reset required after 0x1011 for defaults to take effect

  MAGIC VALUES
  ------------
  0x65766173  =  'e','v','a','s'  =  "save" (little-endian)
  0x64616F6C  =  'd','a','o','l'  =  "load" (little-endian)
  0xA55AA55A  =  valid NVM marker (in header)

  NVM HEADER FIELDS (minimum)
  ---------------------------
  valid_marker  : 0xA55AA55A when valid
  version       : firmware layout version number
  length        : payload byte count
  crc32         : CRC-32 over payload bytes
  sequence      : monotonic counter for ping-pong / ring selection

  EEPROM STRATEGY
  ---------------
  Simple params  : single location, write-in-place, header last
  Frequent params: circular log of N slots, rotate on each write
  ~100k-1M write cycles per byte location

  FLASH STRATEGY
  --------------
  Ping-pong banks: write to inactive bank, validate CRC, flip pointer
  Ring of pages  : N pages, write to next erased page, erase oldest when full
  ~10k-100k erase cycles per page

  CRC VALIDATION
  --------------
  Always compute CRC over raw payload bytes
  Store CRC in header, verify on every load
  Optionally add separate header CRC for dual-level protection
  On CRC failure: log error, load ROM defaults, continue safely

  MIGRATION STRATEGY
  ------------------
  Store layout version in every NVM header
  On load: compare stored_version vs FIRMWARE_LAYOUT_VERSION
  If stored < current  : run upgrade chain (V1→V2→V3→...)
  If stored > current  : reject (downgrade risk), load defaults
  Each step copies known fields, applies safe defaults to new fields
  Always test migration paths in CI with golden NVM images

  COMMON PITFALLS
  ---------------
  [!] Writing header before payload: power loss leaves stale header
      -> Always write payload first, then header
  [!] No wear-levelling on Flash: single page exhausts in days
      -> Use ping-pong or ring buffer
  [!] No version field: firmware update silently misinterprets data
      -> Always version-stamp every NVM block
  [!] Ignoring migration: OD fields shift offset after layout change
      -> Explicit migration step for every layout version increment
  [!] Assuming NVM writes are atomic on Flash: they are NOT
      -> Dual-bank ensures one bank is always consistent
```

---

*End of Document — CANopen OD Storage, Persistent Parameters in Flash/EEPROM*