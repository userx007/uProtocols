# 74. Diagnostic Data Flash

**Document Structure:**

1. **Introduction** — Role of diagnostic flash in OBD/UDS-compliant ECUs
2. **Key Concepts** — DTC, NVM types (EEPROM/Flash/FRAM), DIDs, Record Numbers
3. **NVM Architecture** — Logical partitions, storage strategies (fixed-slot, ring buffer, priority queue), write leveling and endurance
4. **Freeze Frame Data** — OBD-II PID table, UDS snapshot records, capture trigger sequence
5. **Environmental Data Records** — Comparison with freeze frames, time-windowed context, UDS access paths
6. **Extended Diagnostic Records** — Occurrence/aging/healing counters, calibration and lifecycle data
7. **UDS Services** — Full table of relevant service IDs and `0x19` sub-functions
8. **C/C++ Implementation** — Bitfield DTC status struct, freeze frame capture with NVM write, UDS response serialisation, extended data counters, aging, and clear functions
9. **Rust Implementation** — Trait-based `NvmBackend` abstraction, full store implementation, UDS dispatcher with pattern matching, unit tests with `MockNvm`
10. **Summary** — Architecture, protocol, and implementation pattern takeaways

## Managing Freeze Frame Data, Environmental Data, and Extended Diagnostic Records in Non-Volatile Memory

---

## Table of Contents

1. [Introduction](#introduction)
2. [Key Concepts](#key-concepts)
3. [Non-Volatile Memory Architecture](#non-volatile-memory-architecture)
4. [Freeze Frame Data](#freeze-frame-data)
5. [Environmental Data Records](#environmental-data-records)
6. [Extended Diagnostic Records](#extended-diagnostic-records)
7. [UDS Services for Diagnostic Data Flash](#uds-services-for-diagnostic-data-flash)
8. [C/C++ Implementation](#cc-implementation)
9. [Rust Implementation](#rust-implementation)
10. [Summary](#summary)

---

## Introduction

Diagnostic Data Flash refers to the mechanisms by which automotive Electronic Control Units (ECUs) capture, store, and manage diagnostic-relevant data in non-volatile memory (NVM). This data survives power cycles and is essential for post-event analysis, fault diagnosis, and regulatory compliance (e.g., OBD-II/EOBD mandates).

The three primary categories of data managed in diagnostic flash are:

- **Freeze Frame Data** — a snapshot of sensor and system parameters at the moment a Diagnostic Trouble Code (DTC) was set.
- **Environmental Data Records** — broader contextual measurements (temperature, load, speed, voltage, etc.) recorded alongside or independently of DTCs.
- **Extended Diagnostic Records** — manufacturer-specific or standardized data blocks providing deeper insight into ECU state, calibration data, event counters, and lifecycle information.

These records are stored and retrieved over the CAN bus using standardized diagnostic protocols, primarily **ISO 14229 (UDS)** and **ISO 15031 (OBD-II)**.

---

## Key Concepts

### Diagnostic Trouble Code (DTC)
A numeric identifier representing a detected fault condition. DTC storage typically triggers the capture of associated freeze frame and environmental data.

### Non-Volatile Memory (NVM)
Persistent storage that retains data across power cycles. In automotive ECUs this is typically implemented with:
- **EEPROM** — byte-level erasable, for small/frequent updates
- **Flash memory** — sector-erase-based, for larger blocks
- **Fram (Ferroelectric RAM)** — fast, byte-level, high-endurance, increasingly common

### Data Identifier (DID)
A 16-bit identifier used in UDS to address specific data records. For example:
- `0xF180`–`0xF18F` — vehicle identification data
- `0xF190`–`0xF19F` — ECU identification data
- `0x0100`–`0x7FFF` — manufacturer-defined DIDs

### Record Number
A 1-byte index used within ReadDTCInformation sub-functions to reference specific stored records (e.g., `0x01` = first freeze frame).

### Snapshot / Freeze Frame
The term "freeze frame" comes from OBD-II (SAE J1979), while UDS uses the term "snapshot data record." They are functionally equivalent: a set of PID/DID values captured at fault onset.

---

## Non-Volatile Memory Architecture

ECU NVM for diagnostics is typically partitioned into several logical regions:

```
+----------------------------+
|  DTC Status Byte Array     |  (per-DTC: confirmed, pending, aged, etc.)
+----------------------------+
|  Freeze Frame Store        |  (indexed by DTC or record number)
+----------------------------+
|  Environmental Data Store  |  (time-stamped ring buffer or slot-based)
+----------------------------+
|  Extended Data Records     |  (counters, calibration, lifecycle data)
+----------------------------+
|  Event Memory Metadata     |  (timestamps, odometer, ignition cycles)
+----------------------------+
```

### Storage Strategies

| Strategy       | Description                                          | Use Case                        |
|----------------|------------------------------------------------------|---------------------------------|
| Fixed-slot     | Pre-allocated slots per DTC                         | Simple, low-memory ECUs         |
| Ring buffer    | Circular buffer, oldest overwritten                 | High-frequency event logging    |
| Priority queue | Higher-priority DTCs displace lower-priority ones   | Safety-critical applications    |
| FIFO           | First-in-first-out with configurable depth          | General purpose                 |

### NVM Write Strategy

Flash memory has limited write cycles (~10,000–100,000 endurance cycles). Key strategies include:

- **Write leveling** — distribute writes across sectors
- **Deferred writing** — buffer writes in RAM and flush periodically or on shutdown
- **Verify after write** — read-back verification for safety-critical data
- **Redundant storage** — dual-write for critical data (DTC status, odometer)

---

## Freeze Frame Data

### Definition

A freeze frame is a fixed-size record capturing vehicle operating parameters at the exact moment a DTC transitions to the *confirmed* or *pending* state. It allows technicians to reproduce fault conditions.

### Standard OBD-II Freeze Frame PIDs (SAE J1979)

| PID    | Description                      | Bytes |
|--------|----------------------------------|-------|
| `0x02` | DTC that caused freeze frame     | 2     |
| `0x04` | Calculated engine load           | 1     |
| `0x05` | Engine coolant temperature       | 1     |
| `0x0B` | Intake manifold absolute pressure| 1     |
| `0x0C` | Engine RPM                       | 2     |
| `0x0D` | Vehicle speed                    | 1     |
| `0x0F` | Intake air temperature           | 1     |
| `0x11` | Throttle position                | 1     |

### UDS Freeze Frame (Snapshot Data Record)

In UDS, snapshot data is accessed via `ReadDTCInformation` (Service `0x19`), sub-function `0x04` (reportDTCSnapshotRecordByDTCNumber). The data is structured as a sequence of DID–value pairs.

### Freeze Frame Capture Logic

When a DTC is set:
1. The fault detection algorithm confirms the fault condition.
2. The DTC status byte is updated (confirmed bit set).
3. Current sensor values for all configured PIDs/DIDs are read.
4. The snapshot record is assembled and written to NVM.
5. If a freeze frame already exists for this DTC, behavior depends on configuration (overwrite / keep oldest / keep most recent).

---

## Environmental Data Records

### Definition

Environmental Data Records (EDR) provide broader context beyond a single fault event. They may include:

- Ambient temperature, battery voltage, ignition timing
- Time-stamped history of a parameter (e.g., coolant temperature over the last 30 seconds)
- Aggregated statistics (min/max/average over an ignition cycle)
- Occurrence counters (how many times a threshold was exceeded)

### Difference from Freeze Frames

| Aspect             | Freeze Frame              | Environmental Data Record     |
|--------------------|---------------------------|-------------------------------|
| Trigger            | DTC setting event         | Configurable (DTC, periodic, threshold) |
| Time scope         | Instantaneous snapshot    | May span a window of time     |
| Association        | Tied to specific DTC      | May be standalone or DTC-linked |
| Access method      | `0x19/0x04`               | `0x19/0x06` or `0x22`         |

### UDS Access

Environmental data is accessed using:
- `ReadDTCInformation` (0x19), sub-function `0x06` — `reportDTCExtDataRecordByDTCNumber`
- `ReadDataByIdentifier` (0x22) — for standalone environmental records addressed by DID

---

## Extended Diagnostic Records

### Definition

Extended Diagnostic Records (XDR) are manufacturer-defined or standardized data blocks that go beyond simple freeze frames. They include:

- **Fault occurrence counters** — how many times a DTC has been detected
- **Aging counters** — for DTC qualification and dequalification
- **Healing counters** — tracking consecutive passed cycles
- **Calibration data** — sensor offsets, learned corrections
- **Lifecycle data** — total operating hours, battery replacement events
- **Security-related data** — authentication attempt logs

### UDS Extended Data Access

Extended data records are accessed via:
- `ReadDTCInformation` (0x19), sub-function `0x06` — `reportDTCExtDataRecordByDTCNumber`
- Sub-function `0x10` — `reportDTCExtDataRecordByRecordNumber`

Each record is identified by an **Extended Data Record Number** (1 byte, `0x01`–`0xEF` for user-defined, `0xFE`/`0xFF` reserved).

---

## UDS Services for Diagnostic Data Flash

### Service Overview

| Service ID | Name                          | Relevance                                  |
|------------|-------------------------------|--------------------------------------------|
| `0x14`     | ClearDiagnosticInformation    | Erase DTCs and associated NVM records      |
| `0x19`     | ReadDTCInformation            | Read DTC status, freeze frames, ext. data  |
| `0x22`     | ReadDataByIdentifier          | Read any DID including environmental data  |
| `0x2E`     | WriteDataByIdentifier         | Write configuration/calibration DIDs       |
| `0x34`     | RequestDownload               | Flash programming initiation               |
| `0x36`     | TransferData                  | Flash data transfer                        |
| `0x37`     | RequestTransferExit           | Flash programming completion               |

### ReadDTCInformation Sub-functions (0x19)

| Sub-function | Name                                        |
|--------------|---------------------------------------------|
| `0x01`       | reportNumberOfDTCByStatusMask               |
| `0x02`       | reportDTCByStatusMask                       |
| `0x03`       | reportDTCSnapshotIdentification             |
| `0x04`       | reportDTCSnapshotRecordByDTCNumber          |
| `0x05`       | reportDTCSnapshotRecordByRecordNumber       |
| `0x06`       | reportDTCExtDataRecordByDTCNumber           |
| `0x0A`       | reportSupportedDTC                          |

### ClearDiagnosticInformation (0x14)

Clears all DTCs and their associated NVM data (freeze frames, extended data). The request carries a 3-byte DTC group identifier:
- `0xFFFFFF` — all DTCs
- `0x000000`–`0xFEFFFF` — specific group

---

## C/C++ Implementation

### Data Structures

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* DTC Status Byte (ISO 14229-1, Table D.1)                           */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t testFailed              : 1;  /* Bit 0 */
    uint8_t testFailedThisOpCycle   : 1;  /* Bit 1 */
    uint8_t pendingDTC              : 1;  /* Bit 2 */
    uint8_t confirmedDTC            : 1;  /* Bit 3 */
    uint8_t testNotCompletedSinceLast : 1; /* Bit 4 */
    uint8_t testFailedSinceLast     : 1;  /* Bit 5 */
    uint8_t testNotCompletedThisOp  : 1;  /* Bit 6 */
    uint8_t warningIndicatorReq     : 1;  /* Bit 7 */
} DtcStatusByte_t;

/* ------------------------------------------------------------------ */
/* Freeze Frame Record                                                 */
/* ------------------------------------------------------------------ */
#define FREEZE_FRAME_DID_COUNT   8u
#define FREEZE_FRAME_DATA_MAX  128u

typedef struct {
    uint32_t dtcNumber;           /* 3-byte DTC + severity nibble     */
    uint8_t  recordNumber;        /* 0x01..0xFE                       */
    uint32_t timestamp;           /* ms since ECU start or epoch      */
    uint32_t odometer;            /* km * 10                          */
    uint16_t ignitionCycles;      /* ignition cycle counter           */

    /* DID-value pairs captured at fault set                          */
    struct {
        uint16_t did;
        uint8_t  length;
        uint8_t  data[8];
    } records[FREEZE_FRAME_DID_COUNT];

    uint8_t  recordCount;
    uint8_t  crc8;                /* Integrity check over the record  */
} FreezeFrameRecord_t;

/* ------------------------------------------------------------------ */
/* Extended Data Record                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t dtcNumber;
    uint8_t  recordNumber;        /* 0x01..0xEF                       */

    uint16_t occurrenceCounter;   /* Times DTC has been confirmed     */
    uint16_t agingCounter;        /* Cycles since last confirmation   */
    uint16_t healingCounter;      /* Consecutive passed cycles        */
    uint8_t  priorityValue;       /* 0 = highest                      */

    uint8_t  extData[16];         /* Application-specific payload     */
    uint8_t  extDataLen;
} ExtendedDataRecord_t;

/* ------------------------------------------------------------------ */
/* NVM Diagnostic Store — manages slots for multiple DTCs             */
/* ------------------------------------------------------------------ */
#define DTC_STORE_MAX_ENTRIES    16u

typedef struct {
    FreezeFrameRecord_t  freezeFrames[DTC_STORE_MAX_ENTRIES];
    ExtendedDataRecord_t extRecords[DTC_STORE_MAX_ENTRIES];
    uint8_t              usedSlots;
    uint32_t             storeCrc32;  /* CRC over entire store        */
} DiagnosticNvmStore_t;
```

### Freeze Frame Capture

```c
#include <string.h>

/* Application-provided functions (platform-specific)                 */
extern uint32_t App_GetTimestamp(void);
extern uint32_t App_GetOdometer(void);
extern uint16_t App_GetIgnitionCycles(void);
extern bool     App_ReadDid(uint16_t did, uint8_t *buf, uint8_t *len);
extern uint8_t  Crc8_Calculate(const uint8_t *data, uint16_t len);
extern bool     Nvm_Write(uint32_t addr, const void *data, uint16_t len);

/* DIDs to capture in every freeze frame                              */
static const uint16_t g_freezeFrameDids[] = {
    0x0C30,   /* Engine RPM                  */
    0x0C31,   /* Vehicle Speed               */
    0x0C32,   /* Engine Load                 */
    0x0C33,   /* Coolant Temperature         */
    0x0C34,   /* Battery Voltage             */
    0x0C35,   /* Intake Air Temperature      */
    0xF40A,   /* Operating Mode              */
    0xF40B,   /* Gear Position               */
};

/**
 * @brief Capture freeze frame data and write to NVM slot.
 *
 * @param store     Pointer to the NVM diagnostic store (RAM mirror)
 * @param dtcNumber 24-bit DTC number
 * @param recordNum Freeze frame record number (1..254)
 * @return true on success, false on NVM write failure or store full
 */
bool DiagFlash_CaptureFreezeFrame(DiagnosticNvmStore_t *store,
                                   uint32_t dtcNumber,
                                   uint8_t  recordNum)
{
    if (store->usedSlots >= DTC_STORE_MAX_ENTRIES) {
        return false;   /* Store full — policy: reject or evict       */
    }

    FreezeFrameRecord_t *ff = &store->freezeFrames[store->usedSlots];
    memset(ff, 0, sizeof(FreezeFrameRecord_t));

    ff->dtcNumber      = dtcNumber;
    ff->recordNumber   = recordNum;
    ff->timestamp      = App_GetTimestamp();
    ff->odometer       = App_GetOdometer();
    ff->ignitionCycles = App_GetIgnitionCycles();
    ff->recordCount    = 0u;

    /* Capture each configured DID                                    */
    uint8_t numDids = (uint8_t)(sizeof(g_freezeFrameDids) /
                                sizeof(g_freezeFrameDids[0]));

    for (uint8_t i = 0u; i < numDids && i < FREEZE_FRAME_DID_COUNT; i++) {
        uint8_t buf[8];
        uint8_t len = 0u;

        if (App_ReadDid(g_freezeFrameDids[i], buf, &len) && len > 0u) {
            ff->records[ff->recordCount].did    = g_freezeFrameDids[i];
            ff->records[ff->recordCount].length = len;
            memcpy(ff->records[ff->recordCount].data, buf,
                   len < 8u ? len : 8u);
            ff->recordCount++;
        }
    }

    /* Calculate integrity CRC                                        */
    ff->crc8 = Crc8_Calculate((const uint8_t *)ff,
                               (uint16_t)(sizeof(FreezeFrameRecord_t) - 1u));

    store->usedSlots++;

    /* Persist to NVM                                                 */
    return Nvm_Write(/* NVM_FREEZE_FRAME_BASE_ADDR + slot offset */
                     0x00100000UL + (store->usedSlots - 1u) *
                         sizeof(FreezeFrameRecord_t),
                     ff,
                     sizeof(FreezeFrameRecord_t));
}
```

### Reading and Serialising a Freeze Frame for UDS Response

```c
/**
 * @brief Build a UDS 0x19/0x04 response payload for a given DTC.
 *
 * Response format (per ISO 14229-1):
 *   [3 bytes DTC] [1 byte status] [1 byte record#]
 *   [DID count] { [2 bytes DID] [1 byte len] [n bytes data] }...
 *
 * @param store       NVM store (RAM mirror)
 * @param dtcNumber   DTC to look up
 * @param recordNum   Record number requested (0xFF = all records)
 * @param respBuf     Output buffer
 * @param respLen     Output: number of bytes written
 * @param dtcStatus   Current DTC status byte
 * @return true if DTC found, false otherwise
 */
bool DiagFlash_ReadFreezeFrame(const DiagnosticNvmStore_t *store,
                                uint32_t dtcNumber,
                                uint8_t  recordNum,
                                uint8_t *respBuf,
                                uint16_t *respLen,
                                uint8_t  dtcStatus)
{
    *respLen = 0u;

    for (uint8_t i = 0u; i < store->usedSlots; i++) {
        const FreezeFrameRecord_t *ff = &store->freezeFrames[i];

        if (ff->dtcNumber != dtcNumber) continue;
        if (recordNum != 0xFFu && ff->recordNumber != recordNum) continue;

        /* Validate CRC                                               */
        uint8_t expected = Crc8_Calculate((const uint8_t *)ff,
                               (uint16_t)(sizeof(FreezeFrameRecord_t) - 1u));
        if (expected != ff->crc8) continue;    /* Corrupted — skip   */

        /* Encode DTC (3 bytes, big-endian)                           */
        respBuf[(*respLen)++] = (uint8_t)((ff->dtcNumber >> 16u) & 0xFFu);
        respBuf[(*respLen)++] = (uint8_t)((ff->dtcNumber >>  8u) & 0xFFu);
        respBuf[(*respLen)++] = (uint8_t)( ff->dtcNumber         & 0xFFu);
        respBuf[(*respLen)++] = dtcStatus;
        respBuf[(*respLen)++] = ff->recordNumber;

        /* DID-value pairs                                            */
        for (uint8_t j = 0u; j < ff->recordCount; j++) {
            respBuf[(*respLen)++] =
                (uint8_t)((ff->records[j].did >> 8u) & 0xFFu);
            respBuf[(*respLen)++] =
                (uint8_t)( ff->records[j].did        & 0xFFu);
            respBuf[(*respLen)++] = ff->records[j].length;
            memcpy(&respBuf[*respLen],
                   ff->records[j].data,
                   ff->records[j].length);
            *respLen += ff->records[j].length;
        }
    }

    return (*respLen > 0u);
}
```

### Clearing Diagnostic Data

```c
/**
 * @brief Clear all NVM diagnostic records for a given DTC group.
 *
 * @param store         NVM store (RAM mirror)
 * @param dtcGroupMask  0xFFFFFF = all DTCs; specific value = one DTC
 * @return true on success
 */
bool DiagFlash_ClearDiagnosticInfo(DiagnosticNvmStore_t *store,
                                    uint32_t dtcGroupMask)
{
    if (dtcGroupMask == 0xFFFFFFUL) {
        /* Clear everything                                           */
        memset(store->freezeFrames, 0,
               sizeof(store->freezeFrames));
        memset(store->extRecords, 0,
               sizeof(store->extRecords));
        store->usedSlots = 0u;
    } else {
        /* Remove only records matching the specific DTC              */
        uint8_t writeIdx = 0u;
        for (uint8_t i = 0u; i < store->usedSlots; i++) {
            if (store->freezeFrames[i].dtcNumber != dtcGroupMask) {
                store->freezeFrames[writeIdx] =
                    store->freezeFrames[i];
                store->extRecords[writeIdx] =
                    store->extRecords[i];
                writeIdx++;
            }
        }
        store->usedSlots = writeIdx;
    }

    /* Flush RAM mirror to NVM                                        */
    return Nvm_Write(0x00100000UL, store, sizeof(DiagnosticNvmStore_t));
}
```

### Extended Data Record Management

```c
/**
 * @brief Update occurrence and aging counters for a DTC.
 *        Called each time a DTC transitions to confirmed.
 */
void DiagFlash_UpdateExtendedData(DiagnosticNvmStore_t *store,
                                   uint32_t dtcNumber)
{
    ExtendedDataRecord_t *rec = NULL;

    /* Find existing record or allocate new slot                      */
    for (uint8_t i = 0u; i < store->usedSlots; i++) {
        if (store->extRecords[i].dtcNumber == dtcNumber) {
            rec = &store->extRecords[i];
            break;
        }
    }

    if (rec == NULL && store->usedSlots < DTC_STORE_MAX_ENTRIES) {
        rec = &store->extRecords[store->usedSlots];
        memset(rec, 0, sizeof(ExtendedDataRecord_t));
        rec->dtcNumber    = dtcNumber;
        rec->recordNumber = 0x01u;
    }

    if (rec == NULL) return;   /* Store full                          */

    if (rec->occurrenceCounter < 0xFFFFu) rec->occurrenceCounter++;
    rec->agingCounter    = 0u;   /* Reset: just confirmed again       */
    rec->healingCounter  = 0u;
}

/**
 * @brief Increment aging counter on each drive cycle without fault.
 *        When agingCounter reaches threshold, DTC is aged out.
 */
void DiagFlash_AgeAllDtcs(DiagnosticNvmStore_t *store,
                            uint16_t agingThreshold)
{
    for (uint8_t i = 0u; i < store->usedSlots; i++) {
        ExtendedDataRecord_t *rec = &store->extRecords[i];
        if (rec->agingCounter < agingThreshold) {
            rec->agingCounter++;
        }
    }
}
```

---

## Rust Implementation

### Data Structures

```rust
use std::time::{SystemTime, UNIX_EPOCH};

// ------------------------------------------------------------------ //
// DTC Status Byte                                                     //
// ------------------------------------------------------------------ //
#[derive(Debug, Clone, Copy, Default)]
pub struct DtcStatusByte {
    pub test_failed:                    bool,
    pub test_failed_this_op_cycle:      bool,
    pub pending_dtc:                    bool,
    pub confirmed_dtc:                  bool,
    pub test_not_completed_since_last:  bool,
    pub test_failed_since_last:         bool,
    pub test_not_completed_this_op:     bool,
    pub warning_indicator_requested:    bool,
}

impl DtcStatusByte {
    pub fn to_byte(&self) -> u8 {
        (self.test_failed                   as u8)
        | ((self.test_failed_this_op_cycle  as u8) << 1)
        | ((self.pending_dtc                as u8) << 2)
        | ((self.confirmed_dtc              as u8) << 3)
        | ((self.test_not_completed_since_last as u8) << 4)
        | ((self.test_failed_since_last     as u8) << 5)
        | ((self.test_not_completed_this_op as u8) << 6)
        | ((self.warning_indicator_requested as u8) << 7)
    }

    pub fn from_byte(byte: u8) -> Self {
        DtcStatusByte {
            test_failed:                    (byte & 0x01) != 0,
            test_failed_this_op_cycle:      (byte & 0x02) != 0,
            pending_dtc:                    (byte & 0x04) != 0,
            confirmed_dtc:                  (byte & 0x08) != 0,
            test_not_completed_since_last:  (byte & 0x10) != 0,
            test_failed_since_last:         (byte & 0x20) != 0,
            test_not_completed_this_op:     (byte & 0x40) != 0,
            warning_indicator_requested:    (byte & 0x80) != 0,
        }
    }
}

// ------------------------------------------------------------------ //
// DID Record — one captured data identifier value pair               //
// ------------------------------------------------------------------ //
#[derive(Debug, Clone)]
pub struct DidRecord {
    pub did:    u16,
    pub data:   Vec<u8>,
}

// ------------------------------------------------------------------ //
// Freeze Frame Record                                                 //
// ------------------------------------------------------------------ //
#[derive(Debug, Clone)]
pub struct FreezeFrameRecord {
    pub dtc_number:      u32,    // 24-bit DTC
    pub record_number:   u8,
    pub timestamp_ms:    u64,
    pub odometer_dm:     u32,    // decimetres
    pub ignition_cycles: u16,
    pub did_records:     Vec<DidRecord>,
}

impl FreezeFrameRecord {
    pub fn new(dtc_number: u32, record_number: u8) -> Self {
        let ts = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_millis() as u64)
            .unwrap_or(0);

        FreezeFrameRecord {
            dtc_number,
            record_number,
            timestamp_ms: ts,
            odometer_dm: 0,
            ignition_cycles: 0,
            did_records: Vec::new(),
        }
    }

    /// Serialise to UDS 0x19/0x04 response format
    pub fn to_uds_response(&self, dtc_status: u8) -> Vec<u8> {
        let mut buf = Vec::new();

        // 3-byte DTC (big-endian)
        buf.push(((self.dtc_number >> 16) & 0xFF) as u8);
        buf.push(((self.dtc_number >>  8) & 0xFF) as u8);
        buf.push((self.dtc_number         & 0xFF) as u8);
        buf.push(dtc_status);
        buf.push(self.record_number);

        for rec in &self.did_records {
            buf.push(((rec.did >> 8) & 0xFF) as u8);
            buf.push((rec.did        & 0xFF) as u8);
            buf.push(rec.data.len() as u8);
            buf.extend_from_slice(&rec.data);
        }

        buf
    }
}

// ------------------------------------------------------------------ //
// Extended Data Record                                                //
// ------------------------------------------------------------------ //
#[derive(Debug, Clone, Default)]
pub struct ExtendedDataRecord {
    pub dtc_number:         u32,
    pub record_number:      u8,
    pub occurrence_counter: u16,
    pub aging_counter:      u16,
    pub healing_counter:    u16,
    pub priority:           u8,
    pub ext_data:           Vec<u8>,
}

// ------------------------------------------------------------------ //
// Diagnostic NVM Store                                                //
// ------------------------------------------------------------------ //
#[derive(Debug, Default)]
pub struct DiagnosticNvmStore {
    pub freeze_frames:  Vec<FreezeFrameRecord>,
    pub ext_records:    Vec<ExtendedDataRecord>,
    pub max_slots:      usize,
}
```

### NVM Store Implementation

```rust
use std::io::{self, Write};

/// Trait abstracting NVM persistence — implement for EEPROM, Flash, file, etc.
pub trait NvmBackend {
    fn write(&mut self, offset: u32, data: &[u8]) -> Result<(), NvmError>;
    fn read(&self,  offset: u32, buf: &mut [u8]) -> Result<(), NvmError>;
    fn erase_sector(&mut self, sector: u32)      -> Result<(), NvmError>;
}

#[derive(Debug)]
pub enum NvmError {
    WriteFailure,
    ReadFailure,
    AddressOutOfRange,
    StoreFull,
    RecordNotFound,
    Corrupt,
}

impl DiagnosticNvmStore {
    pub fn new(max_slots: usize) -> Self {
        DiagnosticNvmStore {
            freeze_frames: Vec::with_capacity(max_slots),
            ext_records:   Vec::with_capacity(max_slots),
            max_slots,
        }
    }

    // -------------------------------------------------------------- //
    // Capture a freeze frame                                          //
    // -------------------------------------------------------------- //
    pub fn capture_freeze_frame<B: NvmBackend>(
        &mut self,
        backend:     &mut B,
        dtc_number:  u32,
        record_num:  u8,
        dids:        Vec<DidRecord>,
        odometer:    u32,
        ign_cycles:  u16,
    ) -> Result<(), NvmError> {
        if self.freeze_frames.len() >= self.max_slots {
            return Err(NvmError::StoreFull);
        }

        let mut ff = FreezeFrameRecord::new(dtc_number, record_num);
        ff.odometer_dm   = odometer;
        ff.ignition_cycles = ign_cycles;
        ff.did_records   = dids;

        // Serialise and write to NVM
        let serialised = self.serialise_freeze_frame(&ff);
        let offset = (self.freeze_frames.len() as u32) * 256; // 256 bytes/slot
        backend.write(offset, &serialised)?;

        self.freeze_frames.push(ff);
        Ok(())
    }

    fn serialise_freeze_frame(&self, ff: &FreezeFrameRecord) -> Vec<u8> {
        let mut buf = Vec::new();
        buf.extend_from_slice(&ff.dtc_number.to_be_bytes()[1..]);  // 3 bytes
        buf.push(ff.record_number);
        buf.extend_from_slice(&ff.timestamp_ms.to_be_bytes());
        buf.extend_from_slice(&ff.odometer_dm.to_be_bytes());
        buf.extend_from_slice(&ff.ignition_cycles.to_be_bytes());
        buf.push(ff.did_records.len() as u8);
        for rec in &ff.did_records {
            buf.extend_from_slice(&rec.did.to_be_bytes());
            buf.push(rec.data.len() as u8);
            buf.extend_from_slice(&rec.data);
        }
        buf
    }

    // -------------------------------------------------------------- //
    // Read freeze frame by DTC and record number                      //
    // -------------------------------------------------------------- //
    pub fn read_freeze_frame(
        &self,
        dtc_number:  u32,
        record_num:  u8,
        dtc_status:  u8,
    ) -> Result<Vec<u8>, NvmError> {
        let results: Vec<Vec<u8>> = self.freeze_frames
            .iter()
            .filter(|ff| {
                ff.dtc_number == dtc_number
                    && (record_num == 0xFF || ff.record_number == record_num)
            })
            .map(|ff| ff.to_uds_response(dtc_status))
            .collect();

        if results.is_empty() {
            Err(NvmError::RecordNotFound)
        } else {
            Ok(results.into_iter().flatten().collect())
        }
    }

    // -------------------------------------------------------------- //
    // Update extended data counters on DTC confirmation               //
    // -------------------------------------------------------------- //
    pub fn update_extended_data(&mut self, dtc_number: u32) {
        if let Some(rec) = self.ext_records
                               .iter_mut()
                               .find(|r| r.dtc_number == dtc_number)
        {
            rec.occurrence_counter = rec.occurrence_counter.saturating_add(1);
            rec.aging_counter   = 0;
            rec.healing_counter = 0;
        } else if self.ext_records.len() < self.max_slots {
            self.ext_records.push(ExtendedDataRecord {
                dtc_number,
                record_number:      0x01,
                occurrence_counter: 1,
                aging_counter:      0,
                healing_counter:    0,
                priority:           0xFF,
                ext_data:           Vec::new(),
            });
        }
    }

    // -------------------------------------------------------------- //
    // Age all active DTCs (call once per drive cycle)                 //
    // -------------------------------------------------------------- //
    pub fn age_all_dtcs(&mut self, aging_threshold: u16) {
        for rec in &mut self.ext_records {
            if rec.aging_counter < aging_threshold {
                rec.aging_counter += 1;
            }
        }
    }

    // -------------------------------------------------------------- //
    // Clear diagnostic information                                    //
    // -------------------------------------------------------------- //
    pub fn clear_diagnostic_info<B: NvmBackend>(
        &mut self,
        backend:       &mut B,
        dtc_group:     u32,
    ) -> Result<(), NvmError> {
        if dtc_group == 0xFFFFFF {
            self.freeze_frames.clear();
            self.ext_records.clear();
        } else {
            self.freeze_frames.retain(|ff| ff.dtc_number != dtc_group);
            self.ext_records.retain(|r|  r.dtc_number   != dtc_group);
        }

        // Re-serialise entire store to NVM (simple strategy)
        for (i, ff) in self.freeze_frames.iter().enumerate() {
            let data = self.serialise_freeze_frame(ff);
            backend.write((i as u32) * 256, &data)?;
        }

        Ok(())
    }
}
```

### UDS Request Dispatcher

```rust
/// Process an incoming UDS ReadDTCInformation (0x19) request.
pub fn handle_read_dtc_information(
    store:      &DiagnosticNvmStore,
    request:    &[u8],
    dtc_status: &DtcStatusByte,
) -> Result<Vec<u8>, u8> /* u8 = NRC */ {
    if request.len() < 2 {
        return Err(0x13); // NRC: incorrectMessageLengthOrInvalidFormat
    }

    let sub_fn = request[1] & 0x7F;  // Mask out suppressPositiveResponse bit

    match sub_fn {
        // 0x02 — reportDTCByStatusMask
        0x02 => {
            if request.len() < 3 {
                return Err(0x13);
            }
            let status_mask = request[2];
            let mut resp = vec![0x59, 0x02, status_mask];

            for ff in &store.freeze_frames {
                if dtc_status.to_byte() & status_mask != 0 {
                    resp.push(((ff.dtc_number >> 16) & 0xFF) as u8);
                    resp.push(((ff.dtc_number >>  8) & 0xFF) as u8);
                    resp.push((ff.dtc_number         & 0xFF) as u8);
                    resp.push(dtc_status.to_byte());
                }
            }
            Ok(resp)
        },

        // 0x04 — reportDTCSnapshotRecordByDTCNumber
        0x04 => {
            if request.len() < 6 {
                return Err(0x13);
            }
            let dtc = ((request[2] as u32) << 16)
                     | ((request[3] as u32) <<  8)
                     |  (request[4] as u32);
            let rec_num = request[5];

            match store.read_freeze_frame(dtc, rec_num, dtc_status.to_byte()) {
                Ok(payload) => {
                    let mut resp = vec![0x59, 0x04];
                    resp.extend_from_slice(&payload);
                    Ok(resp)
                },
                Err(_) => Err(0x31), // NRC: requestOutOfRange
            }
        },

        // 0x06 — reportDTCExtDataRecordByDTCNumber
        0x06 => {
            if request.len() < 6 {
                return Err(0x13);
            }
            let dtc = ((request[2] as u32) << 16)
                     | ((request[3] as u32) <<  8)
                     |  (request[4] as u32);
            let rec_num = request[5];

            if let Some(rec) = store.ext_records
                                    .iter()
                                    .find(|r| r.dtc_number == dtc
                                              && (rec_num == 0xFF
                                                  || r.record_number == rec_num))
            {
                let mut resp = vec![0x59, 0x06];
                resp.push(((dtc >> 16) & 0xFF) as u8);
                resp.push(((dtc >>  8) & 0xFF) as u8);
                resp.push((dtc         & 0xFF) as u8);
                resp.push(dtc_status.to_byte());
                resp.push(rec.record_number);
                resp.extend_from_slice(&rec.occurrence_counter.to_be_bytes());
                resp.extend_from_slice(&rec.aging_counter.to_be_bytes());
                resp.extend_from_slice(&rec.healing_counter.to_be_bytes());
                resp.extend_from_slice(&rec.ext_data);
                Ok(resp)
            } else {
                Err(0x31)
            }
        },

        _ => Err(0x12), // NRC: subFunctionNotSupported
    }
}
```

### Unit Test (Rust)

```rust
#[cfg(test)]
mod tests {
    use super::*;

    struct MockNvm { data: Vec<u8> }
    impl MockNvm {
        fn new() -> Self { MockNvm { data: vec![0u8; 65536] } }
    }
    impl NvmBackend for MockNvm {
        fn write(&mut self, offset: u32, data: &[u8]) -> Result<(), NvmError> {
            let off = offset as usize;
            self.data[off..off + data.len()].copy_from_slice(data);
            Ok(())
        }
        fn read(&self, offset: u32, buf: &mut [u8]) -> Result<(), NvmError> {
            let off = offset as usize;
            buf.copy_from_slice(&self.data[off..off + buf.len()]);
            Ok(())
        }
        fn erase_sector(&mut self, _sector: u32) -> Result<(), NvmError> { Ok(()) }
    }

    #[test]
    fn test_freeze_frame_capture_and_read() {
        let mut nvm   = MockNvm::new();
        let mut store = DiagnosticNvmStore::new(16);

        let dids = vec![
            DidRecord { did: 0x0C30, data: vec![0x0F, 0xA0] }, // 4000 RPM
            DidRecord { did: 0x0C31, data: vec![0x64] },         // 100 km/h
        ];

        store.capture_freeze_frame(
            &mut nvm,
            0x00B100,  // DTC P0001 example
            0x01,
            dids,
            0x0003_E800, // odometer
            42,
        ).expect("capture failed");

        let status = DtcStatusByte {
            confirmed_dtc: true,
            ..Default::default()
        };

        let payload = store.read_freeze_frame(0x00B100, 0x01, status.to_byte())
            .expect("read failed");

        assert!(!payload.is_empty());
        assert_eq!(payload[0], 0x00); // DTC high byte
        assert_eq!(payload[1], 0xB1); // DTC mid byte
        assert_eq!(payload[2], 0x00); // DTC low byte
        assert_eq!(payload[3], 0x08); // confirmed DTC status
    }

    #[test]
    fn test_extended_data_counters() {
        let mut store = DiagnosticNvmStore::new(16);

        store.update_extended_data(0x00B100);
        store.update_extended_data(0x00B100);
        store.update_extended_data(0x00B100);

        let rec = store.ext_records.iter()
            .find(|r| r.dtc_number == 0x00B100)
            .expect("record not found");

        assert_eq!(rec.occurrence_counter, 3);
        assert_eq!(rec.aging_counter, 0);
    }

    #[test]
    fn test_clear_all_dtcs() {
        let mut nvm   = MockNvm::new();
        let mut store = DiagnosticNvmStore::new(16);

        store.capture_freeze_frame(
            &mut nvm, 0x00B100, 0x01, vec![], 0, 0,
        ).unwrap();
        store.capture_freeze_frame(
            &mut nvm, 0x00C200, 0x01, vec![], 0, 0,
        ).unwrap();

        store.clear_diagnostic_info(&mut nvm, 0xFFFFFF).unwrap();

        assert!(store.freeze_frames.is_empty());
    }
}
```

---

## Summary

Diagnostic Data Flash is a foundational layer of automotive embedded software, enabling post-fault analysis, workshop diagnostics, and regulatory OBD compliance. The key points are:

**Storage Architecture**
Non-volatile memory for diagnostics is logically partitioned into three main areas: freeze frame slots (instantaneous DTC-triggered snapshots), environmental data records (contextual measurements, potentially time-windowed), and extended diagnostic records (counters, aging data, calibration state). Physical NVM (EEPROM, Flash, FRAM) requires careful management including write leveling, deferred writes, and redundancy for critical data.

**Freeze Frames**
Captured atomically when a DTC transitions to confirmed state, freeze frames store a configurable set of DID-value pairs alongside metadata (timestamp, odometer, ignition cycle count). They are accessed via UDS service `0x19`, sub-function `0x04`, and are the primary tool for reproducing fault conditions during workshop diagnosis.

**Environmental Data Records**
Broader than freeze frames, these records can span time windows, contain aggregated statistics, or be triggered independently of DTCs. They are accessed via UDS `0x19/0x06` or `0x22` and provide context beyond the single moment of fault onset.

**Extended Diagnostic Records**
Manufacturer-defined counters and metadata (occurrence count, aging counter, healing counter) govern DTC lifecycle management. Occurrence counters track how often a fault has recurred; aging counters determine when a DTC should be erased after repeated passed cycles; healing counters qualify DTC removal.

**UDS Protocol**
Services `0x14` (clear), `0x19` (read DTC information), `0x22` (read by DID), `0x2E` (write by DID), and the flash programming services (`0x34/0x36/0x37`) together form the complete diagnostic flash interface. Proper NRC handling, session gating (e.g., extended diagnostic session required for clear), and security access for write operations are essential for production implementations.

**Implementation Considerations**
Both the C/C++ and Rust implementations above illustrate a common pattern: a RAM mirror of the NVM store for fast access, with explicit persistence calls to a backend abstraction. In Rust, the trait-based `NvmBackend` allows clean separation of storage logic from hardware specifics and enables unit testing with mock implementations. In C/C++, similar abstraction is achieved via function pointers or platform HAL layers.

---

*Compliant with ISO 14229-1 (UDS), ISO 15031 (OBD-II), and SAE J1979.*