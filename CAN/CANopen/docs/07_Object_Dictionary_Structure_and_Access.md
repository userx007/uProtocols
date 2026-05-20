# 07 — CANopen Object Dictionary (OD): Structure & Access

**Structure**
- Full 16-bit index / 8-bit sub-index address space with all reserved ranges charted in ASCII
- VAR / ARRAY / RECORD object types with ASCII box diagrams showing sub-index layouts and a comparison table

**Data Types & Access**
- All CiA 301 primitive and composite types mapped to their C equivalents
- All five access attributes (RO/WO/RW/RWW/RWR) explained, including an ASCII NMT state machine diagram showing when RWW writes are permitted

**Profile Areas**
- Full ASCII area map (0x0000–0xFFFF) with annotated boundaries
- Communication Profile (0x1000–0x1FFF): key objects tabulated, PDO mapping entry bit-format dissected
- Manufacturer Area (0x2000–0x5FFF): usage pattern diagram and a real PID block example
- Device Profile (0x6000–0x9FFF): CiA 401 I/O and CiA 402 drive examples with Controlword bit map

**C/C++ Programming**
- `OD_Entry_t` descriptor struct with all fields
- Flat OD table definition with real-world entries
- `od_read()` / `od_write()` with full SDO abort code propagation
- Typed C++ template wrapper (`ObjectDictionary` class)
- Simplified SDO server integration showing how the OD wires into CAN frames
- Write callbacks, NVM store/restore (0x1010/0x1011), and PDO mapping validation

**Lookup Strategies**
- Linear, binary search, hash table, and two-level approaches — all with code and an O-notation comparison table

**Summary** with implementation checklist and EDS file snippet

---

> **Series:** CANopen Programming Reference  
> **Topic:** Object Dictionary — Index/Sub-index Space, Object Types, Data Types,
> Access Attributes, Profile Areas, and OD Lookup Strategies  
> **Standard references:** CiA 301 (CANopen Application Layer), CiA 302, CiA 4xx device profiles

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [The Index / Sub-index Addressing Space](#2-the-index--sub-index-addressing-space)
3. [Object Types: VAR, ARRAY, RECORD](#3-object-types-var-array-record)
4. [CANopen Data Types](#4-canopen-data-types)
5. [Access Attributes: RO, WO, RW, RWW, RWR](#5-access-attributes-ro-wo-rw-rww-rwr)
6. [OD Area Map](#6-od-area-map)
   - 6.1 Communication Profile Area (0x1000–0x1FFF)
   - 6.2 Manufacturer-Specific Area (0x2000–0x5FFF)
   - 6.3 Device Profile Area (0x6000–0x9FFF)
7. [OD in C/C++ — Data Structures & Implementation](#7-od-in-cc--data-structures--implementation)
8. [OD Access Functions: Read & Write](#8-od-access-functions-read--write)
9. [OD Lookup Strategies](#9-od-lookup-strategies)
10. [Advanced Topics](#10-advanced-topics)
11. [Summary](#11-summary)

---

## 1. Introduction

The **Object Dictionary (OD)** is the central data repository of every CANopen device.
It is a structured, indexed table that describes all data objects a node exposes to the
network — configuration parameters, process data, communication settings, device identity,
and application variables.

Every CANopen node — whether a sensor, motor controller, I/O module, or master —
**must** implement an Object Dictionary.  Network management (NMT), process data
exchange (PDO), and service data access (SDO) all ultimately reference entries in the OD.

```
  +-------------------------------------------------+
  |              CANopen Node                       |
  |                                                 |
  |  +-------------------------------------------+  |
  |  |          Object Dictionary (OD)           |  |
  |  |                                           |  |
  |  |  Index  Sub  Name              Value      |  |
  |  |  0x1000  0   Device Type       0x00020192 |  |
  |  |  0x1001  0   Error Register    0x00       |  |
  |  |  0x1018  1   Vendor-ID         0xDEADBEEF |  |
  |  |  0x2001  0   Motor Speed Ref   1500 rpm   |  |
  |  |  0x6000  1   Digital Inputs    0b10110001 |  |
  |  |    ...   ...  ...               ...       |  |
  |  +-------------------------------------------+  |
  |        ^                    ^                   |
  |        |  SDO Read/Write    |  Application      |
  |        |  (via CAN bus)     |  internal access  |
  +-------------------------------------------------+
```

The OD acts as the **single source of truth** for both the network (accessed remotely
via SDO) and the application firmware (accessed locally via API calls).

---

## 2. The Index / Sub-index Addressing Space

### 2.1 Address Structure

Each OD entry is addressed by a **16-bit index** and an **8-bit sub-index**:

```
  Bit 15                              Bit 0
  +--------+--------+--------+--------+
  |  Index (16 bit): 0x0000 – 0xFFFF  |
  +--------+--------+--------+--------+

  Byte 7                        Byte 0
  +--------+
  | Sub-index (8 bit): 0x00 – 0xFF    |
  +--------+
```

This gives a theoretical address space of:

```
  65 536 indices  x  256 sub-indices  =  16 777 216 addressable entries
```

In practice, the usable space is partitioned by the CiA 301 standard (see Section 6).

### 2.2 Index Ranges at a Glance

```
  0x0000             Reserved (not used)
  |
  0x0001 – 0x001F    Static data types  (e.g. BOOLEAN=0x0001, INTEGER8=0x0002 …)
  |
  0x0020 – 0x003F    Complex data types (e.g. PDO mapping record)
  |
  0x0040 – 0x005F    Manufacturer-defined complex types
  |
  0x0060 – 0x007F    Device profile-defined static types
  |
  0x0080 – 0x009F    Device profile-defined complex types
  |
  0x00A0 – 0x0FFF    Reserved
  |
  0x1000 – 0x1FFF    Communication Profile Area  (CiA 301)
  |
  0x2000 – 0x5FFF    Manufacturer-Specific Area
  |
  0x6000 – 0x9FFF    Standardised Device Profile Area  (CiA 4xx)
  |
  0xA000 – 0xBFFF    Network variable area
  |
  0xC000 – 0xFFFF    Reserved / system objects
```

### 2.3 Sub-index Conventions

| Sub-index | Meaning |
|-----------|---------|
| `0x00` | For VAR: the variable itself. For ARRAY/RECORD: number of sub-entries (highest populated sub-index) |
| `0x01 – 0xFE` | Actual data entries (array elements or record fields) |
| `0xFF` | Reserved |

---

## 3. Object Types: VAR, ARRAY, RECORD

CiA 301 defines several object types. The three you will encounter in virtually every
implementation are **VAR**, **ARRAY**, and **RECORD**.

### 3.1 VAR — Single Variable

A VAR occupies a single index and has only sub-index 0x00 (which holds the value
directly).

```
  Index 0x1000  (Device Type — VAR, UNSIGNED32)
  +------------------------------------------+
  |  Sub 0x00 |  UNSIGNED32  |  0x00020192   |
  +------------------------------------------+
```

VAR is the simplest and most common object type.

### 3.2 ARRAY — Homogeneous Sequence

An ARRAY occupies one index, with sub-index 0x00 holding the count of elements,
and sub-indices 0x01…0xN holding the actual array elements — all of the **same** data
type.

```
  Index 0x1017  (Producer Heartbeat Time — illustrative ARRAY example)

  Index 0x1003  (Pre-defined Error Field — ARRAY of UNSIGNED32)
  +---------------------------------------------------+
  |  Sub 0x00 |  UNSIGNED8   |  0x05  (5 entries)     |
  |  Sub 0x01 |  UNSIGNED32  |  0x00050000 (error 1)  |
  |  Sub 0x02 |  UNSIGNED32  |  0x00060000 (error 2)  |
  |  Sub 0x03 |  UNSIGNED32  |  0x00000000            |
  |  Sub 0x04 |  UNSIGNED32  |  0x00000000            |
  |  Sub 0x05 |  UNSIGNED32  |  0x00000000            |
  +---------------------------------------------------+
```

### 3.3 RECORD — Heterogeneous Structure

A RECORD occupies one index with sub-index 0x00 holding the count of fields.
Sub-indices 0x01…0xN hold fields that **may have different data types** (like a C struct).

```
  Index 0x1018  (Identity Object — RECORD)
  +--------------------------------------------------+
  |  Sub 0x00 |  UNSIGNED8   |  0x04  (4 fields)     |
  |  Sub 0x01 |  UNSIGNED32  |  Vendor ID            |
  |  Sub 0x02 |  UNSIGNED32  |  Product Code         |
  |  Sub 0x03 |  UNSIGNED32  |  Revision Number      |
  |  Sub 0x04 |  UNSIGNED32  |  Serial Number        |
  +--------------------------------------------------+
```

### 3.4 Comparison Summary

```
  +----------------+------------+-------------------+-----------------------------+
  | Object Type    | Sub-index  | Element Types     | Typical Use                 |
  +----------------+------------+-------------------+-----------------------------+
  | VAR            | 0x00 only  | One type          | Single parameter/value      |
  | ARRAY          | 0x00..0xN  | All identical     | Error logs, mapped PDOs     |
  | RECORD         | 0x00..0xN  | Mixed types       | Identity, comm parameters   |
  +----------------+------------+-------------------+-----------------------------+
```

---

## 4. CANopen Data Types

CiA 301 defines standard data type codes used to describe each OD entry.

### 4.1 Basic / Primitive Types

| Type Code | Name | Size | C Equivalent |
|-----------|------|------|--------------|
| `0x0001` | `BOOLEAN` | 1 bit / 1 byte | `uint8_t` (0 or 1) |
| `0x0002` | `INTEGER8` | 8 bit | `int8_t` |
| `0x0003` | `INTEGER16` | 16 bit | `int16_t` |
| `0x0004` | `INTEGER32` | 32 bit | `int32_t` |
| `0x0005` | `UNSIGNED8` | 8 bit | `uint8_t` |
| `0x0006` | `UNSIGNED16` | 16 bit | `uint16_t` |
| `0x0007` | `UNSIGNED32` | 32 bit | `uint32_t` |
| `0x0008` | `REAL32` | 32 bit IEEE 754 | `float` |
| `0x0009` | `VISIBLE_STRING` | N bytes | `char[]` |
| `0x000A` | `OCTET_STRING` | N bytes | `uint8_t[]` |
| `0x000B` | `UNICODE_STRING` | N×2 bytes | `uint16_t[]` |
| `0x000F` | `DOMAIN` | variable | `void *` |
| `0x0010` | `INTEGER24` | 24 bit | — |
| `0x0011` | `REAL64` | 64 bit IEEE 754 | `double` |
| `0x0015` | `INTEGER64` | 64 bit | `int64_t` |
| `0x001B` | `UNSIGNED64` | 64 bit | `uint64_t` |

### 4.2 Complex / Composite Types

| Type Code | Name | Description |
|-----------|------|-------------|
| `0x0020` | `PDO_COMM_PARAM` | PDO communication parameter record |
| `0x0021` | `PDO_MAPPING` | PDO mapping record |
| `0x0022` | `SDO_PARAMETER` | SDO parameter record |
| `0x0023` | `IDENTITY` | Device identity (vendor, product, rev, serial) |

### 4.3 Data Encoding Note

CANopen uses **little-endian** byte order for all multi-byte types on the wire
(SDO/PDO transfers). Your OD implementation must account for endianness when
reading or writing from CAN frames.

```
  UNSIGNED32 value 0x12345678 on the wire (little-endian):
  Byte 0   Byte 1   Byte 2   Byte 3
  0x78     0x56     0x34     0x12
```

---

## 5. Access Attributes: RO, WO, RW, RWW, RWR

Every OD entry carries an **access attribute** that defines who can read or write it
and under what circumstances.

### 5.1 Attribute Definitions

| Attribute | Name | Read | Write | Typical Objects |
|-----------|------|------|-------|-----------------|
| `RO` | Read Only | Yes | No | Device type, serial number, error register |
| `WO` | Write Only | No | Yes | Command objects, passwords |
| `RW` | Read / Write | Yes | Yes | Configuration, setpoints |
| `RWW` | Read/Write on Write | Yes | In NMT Pre-Op or Stopped only | PDO mapping, PDO comm params |
| `RWR` | Read/Write on Read | Yes | Yes | Not yet fully standardised |

### 5.2 NMT State vs. Write Permission

Some objects with `RWW` can only be modified when the node is in specific NMT states:

```
  NMT State Machine:

         Power-On / Reset
               |
               v
         +----------+
         | INIT     |
         +----+-----+
              |  (auto-transition)
              v
         +-----------+
    +--->| PRE-OP    |<-----+
    |    +-----------+      |
    |         |             |
    |    Start Remote Node  |
    |         v             |
    |    +-----------+      |
    |    | OPERATIONAL|     |
    |    +-----------+      |
    |         |             |
    |    Enter Pre-Op       |
    +----+----+             |
         |                  |
    Stop Remote Node        |
         v                  |
    +-----------+           |
    | STOPPED   +-----------+
    +-----------+   Start / Pre-Op

  PDO Mapping (RWW): writable in PRE-OP and STOPPED only
  Process Data:      exchanged in OPERATIONAL only
```

### 5.3 Checking Access in Code

```c
typedef enum {
    OD_ACCESS_RO  = 0x01,
    OD_ACCESS_WO  = 0x02,
    OD_ACCESS_RW  = 0x03,
    OD_ACCESS_RWW = 0x04,   /* write allowed only in PRE-OP / STOPPED */
    OD_ACCESS_RWR = 0x05
} OD_Access_t;

/* Check whether a write is permitted given current NMT state */
bool od_write_permitted(OD_Access_t access, NMT_State_t nmtState)
{
    switch (access) {
    case OD_ACCESS_RO:
        return false;
    case OD_ACCESS_WO:
    case OD_ACCESS_RW:
        return true;
    case OD_ACCESS_RWW:
        return (nmtState == NMT_PRE_OPERATIONAL ||
                nmtState == NMT_STOPPED);
    default:
        return false;
    }
}
```

---

## 6. OD Area Map

### Overview Diagram

```
  0x0000  +================================+
          |  Reserved / Data Type Defs     |
  0x0FFF  +================================+
          |                                |
  0x1000  +================================+  <--- Communication Profile
          |  CiA 301 Communication Profile |       (fixed by standard)
          |  Device type, Identity, Error  |
          |  Register, PDO config, SDO,    |
          |  Heartbeat, Sync, Time, NMT    |
  0x1FFF  +================================+
          |                                |
  0x2000  +================================+  <--- Manufacturer-Specific
          |  Vendor-defined objects        |       (freely usable)
          |  Application parameters        |
          |  Proprietary control objects   |
          |  Tuning parameters, raw I/O    |
  0x5FFF  +================================+
          |                                |
  0x6000  +================================+  <--- Device Profile
          |  CiA 4xx Device Profile Area   |       (standardised per type)
          |  Digital/Analog I/O            |
          |  Drive parameters (CiA 402)    |
          |  Encoder objects (CiA 406)     |
  0x9FFF  +================================+
          |                                |
  0xA000  +================================+  <--- Network Variables / Reserved
          |  (reserved / system)           |
  0xFFFF  +================================+
```

---

### 6.1 Communication Profile Area (0x1000–0x1FFF)

This range is fully specified by CiA 301. Every compliant device must implement the
mandatory objects and may implement the optional ones.

#### Key Communication Profile Objects

```
  Index    Sub   Name                         Type        Access  M/O
  -----------------------------------------------------------------------
  0x1000   0x00  Device Type                  UNSIGNED32  RO      M
  0x1001   0x00  Error Register               UNSIGNED8   RO      M
  0x1002   0x00  Manufacturer Status Register UNSIGNED32  RO      O
  0x1003   0x00  Number of Errors             UNSIGNED8   RW      O
  0x1003   0x01  Pre-defined Error Field[1]   UNSIGNED32  RO      O
  ...
  0x1005   0x00  COB-ID SYNC Message          UNSIGNED32  RW      M
  0x1006   0x00  Communication Cycle Period   UNSIGNED32  RW      O
  0x1007   0x00  Synchronous Window Length    UNSIGNED32  RW      O
  0x1008   0x00  Manufacturer Device Name     VIS_STRING  RO      O
  0x1009   0x00  Manufacturer HW Version      VIS_STRING  RO      O
  0x100A   0x00  Manufacturer SW Version      VIS_STRING  RO      O
  0x100C   0x00  Guard Time (ms)              UNSIGNED16  RW      O
  0x100D   0x00  Life Time Factor             UNSIGNED8   RW      O
  0x1010   0x01  Store All Parameters         UNSIGNED32  RW      O
  0x1011   0x01  Restore All Parameters       UNSIGNED32  RW      O
  0x1014   0x00  COB-ID EMCY                  UNSIGNED32  RW      M
  0x1017   0x00  Producer Heartbeat Time (ms) UNSIGNED16  RW      M
  0x1018   0x01  Identity — Vendor ID         UNSIGNED32  RO      M
  0x1018   0x02  Identity — Product Code      UNSIGNED32  RO      O
  0x1018   0x03  Identity — Revision Number   UNSIGNED32  RO      O
  0x1018   0x04  Identity — Serial Number     UNSIGNED32  RO      O
  0x1020   0x01  Verify Config — Date         UNSIGNED32  RW      O
  0x1020   0x02  Verify Config — Time         UNSIGNED32  RW      O
  0x1400   0x01  RPDO1 COB-ID                 UNSIGNED32  RWW     O
  0x1400   0x02  RPDO1 Transmission Type      UNSIGNED8   RWW     O
  0x1600   0x00  RPDO1 Mapping — #entries     UNSIGNED8   RWW     O
  0x1600   0x01  RPDO1 Mapping — Entry 1      UNSIGNED32  RWW     O
  ...
  0x1800   0x01  TPDO1 COB-ID                 UNSIGNED32  RWW     O
  0x1800   0x02  TPDO1 Transmission Type      UNSIGNED8   RWW     O
  0x1A00   0x00  TPDO1 Mapping — #entries     UNSIGNED8   RWW     O
  0x1A00   0x01  TPDO1 Mapping — Entry 1      UNSIGNED32  RWW     O
  ...
```

M = Mandatory, O = Optional

#### Device Type Object (0x1000)

```
  Bits 31-16: Additional Information (device profile number extension)
  Bits 15- 0: Device Profile Number

  Example: 0x00020192
    0x0002 = Reserved/generic
    0x0192 = 402 decimal = CiA 402 (Drives and Motion Control)
```

#### PDO Mapping Entry Format (0x1600 / 0x1A00)

Each 32-bit mapping entry encodes the index, sub-index, and bit length of the
mapped object:

```
  Bits 31-16: Index     (0x6000..0x9FFF typically)
  Bits 15- 8: Sub-index
  Bits  7- 0: Length in bits

  Example: 0x60400108
    Index     = 0x6040   (Controlword — CiA 402)
    Sub-index = 0x01
    Length    = 0x08 bits  (UNSIGNED8... wait, Controlword is UNSIGNED16 = 16 bits)

  Correct example: 0x60400110
    Index     = 0x6040
    Sub-index = 0x01
    Length    = 0x10 = 16 bits  (UNSIGNED16)
```

---

### 6.2 Manufacturer-Specific Area (0x2000–0x5FFF)

This area is entirely free for the device vendor to populate. There are no CiA
constraints on object types, data types, or layout here. Common uses include:

```
  0x2000  +-------------------------------+
          |  Application setpoints        |
          |  e.g. 0x2001: Motor speed ref |
          |       0x2002: Current limit   |
  0x2FFF  +-------------------------------+
          |  Tuning / calibration data    |
          |  e.g. 0x3000: PID Kp          |
          |       0x3001: PID Ki          |
  0x3FFF  +-------------------------------+
          |  Diagnostic / status objects  |
          |  e.g. 0x4000: Internal temp   |
          |       0x4001: Runtime counter |
  0x4FFF  +-------------------------------+
          |  Raw hardware / I/O access    |
          |  e.g. 0x5000: GPIO direct reg |
  0x5FFF  +-------------------------------+
```

#### Example: Manufacturer Parameter Block

```
  Index 0x3000  (PID Gain Block — RECORD, manufacturer-specific)
  +--------------------------------------------------------+
  |  Sub 0x00 |  UNSIGNED8  |  0x04  (4 fields)            |
  |  Sub 0x01 |  REAL32     |  2.50  Kp (proportional)     |
  |  Sub 0x02 |  REAL32     |  0.15  Ki (integral)         |
  |  Sub 0x03 |  REAL32     |  0.02  Kd (derivative)       |
  |  Sub 0x04 |  UNSIGNED16 |  1000  Update rate (Hz)      |
  +--------------------------------------------------------+
```

**Recommendation:** Even though the manufacturer area is freeform, document your
objects in an Electronic Data Sheet (EDS) file so integrators can use generic
CANopen tools to configure your device.

---

### 6.3 Device Profile Area (0x6000–0x9FFF)

This area is defined by the applicable CiA device profile. The most common profiles are:

```
  CiA 401  —  I/O Modules (digital/analogue in+out)
  CiA 402  —  Drives and Motion Controllers
  CiA 406  —  Encoders
  CiA 410  —  Inclinometers
  CiA 418  —  Battery Modules
  CiA 419  —  Battery Chargers
```

#### CiA 401 I/O Module Example

```
  0x6000  Digital Inputs  8-bit bank 1
  0x6001  Digital Inputs  8-bit bank 2
  ...
  0x6200  Digital Outputs 8-bit bank 1
  0x6401  Analogue Inputs 16-bit channel 1..N
  0x6411  Analogue Outputs 16-bit channel 1..N
```

#### CiA 402 Drive Profile Example (partial)

```
  Index    Sub   Name                        Access
  ----------------------------------------------------------
  0x6040   0x00  Controlword                 RW
  0x6041   0x00  Statusword                  RO
  0x6060   0x00  Modes of Operation          RW
  0x6061   0x00  Modes of Operation Display  RO
  0x6064   0x00  Position Actual Value       RO
  0x606C   0x00  Velocity Actual Value       RO
  0x6071   0x00  Target Torque               RW
  0x607A   0x00  Target Position             RW
  0x60FF   0x00  Target Velocity             RW
```

CiA 402 Controlword bit map:

```
  Bit  0 : Switch On
  Bit  1 : Enable Voltage
  Bit  2 : Quick Stop (0 = active)
  Bit  3 : Enable Operation
  Bit  4 : Operation Mode Specific
  Bit  5 : Operation Mode Specific
  Bit  6 : Operation Mode Specific
  Bit  7 : Fault Reset
  Bit  8 : Halt
  Bit  9 : Operation Mode Specific
  Bit 10 : Reserved
  ...
```

---

## 7. OD in C/C++ — Data Structures & Implementation

### 7.1 OD Entry Descriptor

The most portable approach is a table of descriptors, each describing one
index/sub-index entry:

```c
/* canopen_od.h */
#ifndef CANOPEN_OD_H
#define CANOPEN_OD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Object types */
typedef enum {
    OD_OBJTYPE_VAR    = 0x07,
    OD_OBJTYPE_ARRAY  = 0x08,
    OD_OBJTYPE_RECORD = 0x09
} OD_ObjectType_t;

/* Data types (CiA 301 codes) */
typedef enum {
    OD_DTYPE_BOOLEAN        = 0x0001,
    OD_DTYPE_INTEGER8       = 0x0002,
    OD_DTYPE_INTEGER16      = 0x0003,
    OD_DTYPE_INTEGER32      = 0x0004,
    OD_DTYPE_UNSIGNED8      = 0x0005,
    OD_DTYPE_UNSIGNED16     = 0x0006,
    OD_DTYPE_UNSIGNED32     = 0x0007,
    OD_DTYPE_REAL32         = 0x0008,
    OD_DTYPE_VIS_STRING     = 0x0009,
    OD_DTYPE_OCTET_STRING   = 0x000A,
    OD_DTYPE_UNSIGNED64     = 0x001B
} OD_DataType_t;

/* Access attributes */
typedef enum {
    OD_ACCESS_RO  = 0x01,
    OD_ACCESS_WO  = 0x02,
    OD_ACCESS_RW  = 0x03,
    OD_ACCESS_RWW = 0x04,
    OD_ACCESS_RWR = 0x05
} OD_Access_t;

/* NMT states */
typedef enum {
    NMT_INIT           = 0x00,
    NMT_STOPPED        = 0x04,
    NMT_OPERATIONAL    = 0x05,
    NMT_PRE_OPERATIONAL = 0x7F
} NMT_State_t;

/* Single OD sub-entry descriptor */
typedef struct {
    uint16_t        index;
    uint8_t         subIndex;
    OD_ObjectType_t objectType;
    OD_DataType_t   dataType;
    OD_Access_t     access;
    void           *pData;          /* pointer to the actual data storage */
    uint16_t        maxDataLen;     /* maximum data length in bytes        */
    uint16_t        dataLen;        /* actual current data length in bytes */
} OD_Entry_t;

/* OD table (array of entries, terminated by index=0xFFFF) */
typedef struct {
    const OD_Entry_t *entries;
    uint16_t          count;
} OD_t;

#endif /* CANOPEN_OD_H */
```

### 7.2 Defining OD Data Storage

The OD entries point to actual variables in RAM (or ROM for constants):

```c
/* od_data.c — storage for OD variable values */
#include "canopen_od.h"
#include <string.h>

/* --- Communication Profile Objects --- */
static uint32_t od_deviceType         = 0x00020192UL; /* CiA 402 drive */
static uint8_t  od_errorRegister      = 0x00;
static uint32_t od_cobIdSync          = 0x80000080UL; /* COB-ID 0x80, disabled */
static uint16_t od_heartbeatTime      = 1000;         /* 1000 ms */

/* Identity Object fields */
static uint32_t od_vendorId           = 0xDEADBEEFUL;
static uint32_t od_productCode        = 0x00000001UL;
static uint32_t od_revisionNumber     = 0x00010000UL; /* major=1, minor=0 */
static uint32_t od_serialNumber       = 0x00000042UL;

/* Manufacturer-specific */
static float    od_pidKp              = 2.50f;
static float    od_pidKi              = 0.15f;
static float    od_pidKd              = 0.02f;
static uint16_t od_pidUpdateRate      = 1000;

/* Device profile — CiA 402 */
static uint16_t od_controlword        = 0x0000;
static uint16_t od_statusword         = 0x0000;
static int8_t   od_modesOfOperation   = 0;
static int8_t   od_modesOfOpDisplay   = 0;
static int32_t  od_positionActual     = 0;
static int32_t  od_targetPosition     = 0;
static int32_t  od_targetVelocity     = 0;

/* Manufacturer device name (VIS_STRING) */
static char     od_deviceName[32]     = "MyDrive-X1";
static char     od_hwVersion[8]       = "1.0";
static char     od_swVersion[8]       = "2.3.1";
```

### 7.3 OD Table Definition

```c
/* od_table.c — the OD itself */
#include "canopen_od.h"
#include "od_data.h"

static const OD_Entry_t od_entries[] = {
    /*  index    sub   objtype             dtype                access        pData                   maxLen  */

    /* 0x1000 Device Type */
    { 0x1000, 0x00, OD_OBJTYPE_VAR, OD_DTYPE_UNSIGNED32, OD_ACCESS_RO, &od_deviceType,       4, 4 },

    /* 0x1001 Error Register */
    { 0x1001, 0x00, OD_OBJTYPE_VAR, OD_DTYPE_UNSIGNED8,  OD_ACCESS_RO, &od_errorRegister,    1, 1 },

    /* 0x1008 Manufacturer Device Name */
    { 0x1008, 0x00, OD_OBJTYPE_VAR, OD_DTYPE_VIS_STRING, OD_ACCESS_RO, od_deviceName, sizeof(od_deviceName), 10 },

    /* 0x1017 Producer Heartbeat Time */
    { 0x1017, 0x00, OD_OBJTYPE_VAR, OD_DTYPE_UNSIGNED16, OD_ACCESS_RW, &od_heartbeatTime,    2, 2 },

    /* 0x1018 Identity — sub 0 (count) */
    { 0x1018, 0x00, OD_OBJTYPE_RECORD, OD_DTYPE_UNSIGNED8,  OD_ACCESS_RO, (void*)(uintptr_t)4, 1, 1 },
    { 0x1018, 0x01, OD_OBJTYPE_RECORD, OD_DTYPE_UNSIGNED32, OD_ACCESS_RO, &od_vendorId,       4, 4 },
    { 0x1018, 0x02, OD_OBJTYPE_RECORD, OD_DTYPE_UNSIGNED32, OD_ACCESS_RO, &od_productCode,    4, 4 },
    { 0x1018, 0x03, OD_OBJTYPE_RECORD, OD_DTYPE_UNSIGNED32, OD_ACCESS_RO, &od_revisionNumber, 4, 4 },
    { 0x1018, 0x04, OD_OBJTYPE_RECORD, OD_DTYPE_UNSIGNED32, OD_ACCESS_RO, &od_serialNumber,   4, 4 },

    /* 0x3000 PID Gains (manufacturer-specific RECORD) */
    { 0x3000, 0x00, OD_OBJTYPE_RECORD, OD_DTYPE_UNSIGNED8,  OD_ACCESS_RO, (void*)(uintptr_t)4, 1, 1 },
    { 0x3000, 0x01, OD_OBJTYPE_RECORD, OD_DTYPE_REAL32,     OD_ACCESS_RW, &od_pidKp,           4, 4 },
    { 0x3000, 0x02, OD_OBJTYPE_RECORD, OD_DTYPE_REAL32,     OD_ACCESS_RW, &od_pidKi,           4, 4 },
    { 0x3000, 0x03, OD_OBJTYPE_RECORD, OD_DTYPE_REAL32,     OD_ACCESS_RW, &od_pidKd,           4, 4 },
    { 0x3000, 0x04, OD_OBJTYPE_RECORD, OD_DTYPE_UNSIGNED16, OD_ACCESS_RW, &od_pidUpdateRate,   2, 2 },

    /* 0x6040 Controlword (CiA 402) */
    { 0x6040, 0x00, OD_OBJTYPE_VAR, OD_DTYPE_UNSIGNED16, OD_ACCESS_RW, &od_controlword,     2, 2 },

    /* 0x6041 Statusword (CiA 402) */
    { 0x6041, 0x00, OD_OBJTYPE_VAR, OD_DTYPE_UNSIGNED16, OD_ACCESS_RO, &od_statusword,      2, 2 },

    /* 0x6064 Position Actual Value */
    { 0x6064, 0x00, OD_OBJTYPE_VAR, OD_DTYPE_INTEGER32,  OD_ACCESS_RO, &od_positionActual,  4, 4 },

    /* 0x607A Target Position */
    { 0x607A, 0x00, OD_OBJTYPE_VAR, OD_DTYPE_INTEGER32,  OD_ACCESS_RW, &od_targetPosition,  4, 4 },

    /* 0x60FF Target Velocity */
    { 0x60FF, 0x00, OD_OBJTYPE_VAR, OD_DTYPE_INTEGER32,  OD_ACCESS_RW, &od_targetVelocity,  4, 4 },

    /* Terminator */
    { 0xFFFF, 0xFF, OD_OBJTYPE_VAR, OD_DTYPE_UNSIGNED8,  OD_ACCESS_RO, NULL, 0, 0 }
};

const OD_t g_od = {
    .entries = od_entries,
    .count   = (sizeof(od_entries) / sizeof(od_entries[0])) - 1 /* exclude terminator */
};
```

---

## 8. OD Access Functions: Read & Write

### 8.1 Error Codes

```c
/* od_error.h */
typedef enum {
    OD_OK                  = 0x00000000,
    OD_ERR_NOT_FOUND       = 0x06020000, /* Object does not exist in OD */
    OD_ERR_NO_SUB          = 0x06090011, /* Sub-index does not exist     */
    OD_ERR_READ_ONLY       = 0x06010002, /* Write to read-only object    */
    OD_ERR_WRITE_ONLY      = 0x06010001, /* Read from write-only object  */
    OD_ERR_BAD_LENGTH      = 0x06070010, /* Data type length mismatch    */
    OD_ERR_VALUE_HIGH      = 0x06090031, /* Value exceeds maximum        */
    OD_ERR_VALUE_LOW       = 0x06090032, /* Value below minimum          */
    OD_ERR_HARDWARE        = 0x06060000, /* Access failed due to hardware */
    OD_ERR_NMT_STATE       = 0x08000022  /* Data cannot be written in current NMT state */
} OD_Error_t;
```

### 8.2 OD Lookup and Read

```c
/* od_access.c */
#include "canopen_od.h"
#include "od_error.h"
#include <string.h>

extern const OD_t g_od;

/*
 * od_find - locate an entry in the OD
 * Returns pointer to matching entry, or NULL if not found.
 */
const OD_Entry_t *od_find(uint16_t index, uint8_t subIndex)
{
    const OD_Entry_t *e = g_od.entries;
    const OD_Entry_t *end = e + g_od.count;

    for (; e < end; ++e) {
        if (e->index == index && e->subIndex == subIndex) {
            return e;
        }
    }
    return NULL;
}

/*
 * od_read - read data from an OD entry into caller's buffer
 *
 * @index      16-bit OD index
 * @subIndex   8-bit sub-index
 * @pDst       destination buffer
 * @bufLen     size of destination buffer in bytes
 * @pActualLen out: number of bytes actually written to pDst
 * @nmtState   current NMT state (for RWR enforcement)
 *
 * Returns OD_OK on success, otherwise an OD_Error_t SDO abort code.
 */
OD_Error_t od_read(uint16_t index, uint8_t subIndex,
                   void *pDst, uint16_t bufLen, uint16_t *pActualLen,
                   NMT_State_t nmtState)
{
    (void)nmtState; /* RWR enforcement omitted for brevity */

    const OD_Entry_t *e = od_find(index, subIndex);
    if (!e) {
        /* Check if the index itself exists (for better error reporting) */
        const OD_Entry_t *base = od_find(index, 0x00);
        return base ? OD_ERR_NO_SUB : OD_ERR_NOT_FOUND;
    }

    if (e->access == OD_ACCESS_WO) {
        return OD_ERR_WRITE_ONLY;
    }

    if (bufLen < e->dataLen) {
        return OD_ERR_BAD_LENGTH;
    }

    memcpy(pDst, e->pData, e->dataLen);
    if (pActualLen) {
        *pActualLen = e->dataLen;
    }
    return OD_OK;
}

/*
 * od_write - write data from caller's buffer into an OD entry
 */
OD_Error_t od_write(uint16_t index, uint8_t subIndex,
                    const void *pSrc, uint16_t srcLen,
                    NMT_State_t nmtState)
{
    const OD_Entry_t *e = od_find(index, subIndex);
    if (!e) {
        const OD_Entry_t *base = od_find(index, 0x00);
        return base ? OD_ERR_NO_SUB : OD_ERR_NOT_FOUND;
    }

    if (e->access == OD_ACCESS_RO) {
        return OD_ERR_READ_ONLY;
    }

    if (e->access == OD_ACCESS_RWW) {
        if (nmtState != NMT_PRE_OPERATIONAL && nmtState != NMT_STOPPED) {
            return OD_ERR_NMT_STATE;
        }
    }

    /* Accept either exact match or shorter (zero-padded) write */
    if (srcLen > e->maxDataLen) {
        return OD_ERR_BAD_LENGTH;
    }

    memcpy(e->pData, pSrc, srcLen);
    /* If srcLen < maxDataLen, zero-fill the rest */
    if (srcLen < e->maxDataLen) {
        memset((uint8_t *)e->pData + srcLen, 0, e->maxDataLen - srcLen);
    }

    /* Update actual data length for variable-length objects */
    /* Note: In a real implementation, dataLen in the entry struct
     * would need to be mutable; here we cast away const for demo. */
    ((OD_Entry_t *)(uintptr_t)e)->dataLen = srcLen;

    return OD_OK;
}
```

### 8.3 Typed Convenience Wrappers (C++)

```cpp
/* od_helpers.hpp */
#pragma once
#include "canopen_od.h"
#include "od_access.h"
#include "od_error.h"
#include <cstdint>
#include <optional>
#include <cstring>

class ObjectDictionary {
public:
    explicit ObjectDictionary(NMT_State_t &nmtStateRef)
        : m_nmtState(nmtStateRef) {}

    /* Read a typed value. Returns empty optional on error. */
    template<typename T>
    std::optional<T> read(uint16_t index, uint8_t subIndex) const {
        T value{};
        uint16_t actualLen = 0;
        OD_Error_t err = od_read(index, subIndex,
                                  &value, sizeof(T), &actualLen,
                                  m_nmtState);
        if (err != OD_OK || actualLen != sizeof(T)) {
            return std::nullopt;
        }
        return value;
    }

    /* Write a typed value. Returns true on success. */
    template<typename T>
    bool write(uint16_t index, uint8_t subIndex, const T &value) {
        OD_Error_t err = od_write(index, subIndex,
                                   &value, sizeof(T),
                                   m_nmtState);
        return (err == OD_OK);
    }

    /* Read a string */
    std::optional<std::string> readString(uint16_t index, uint8_t subIndex) const {
        char buf[256] = {};
        uint16_t len  = 0;
        OD_Error_t err = od_read(index, subIndex,
                                  buf, sizeof(buf) - 1, &len,
                                  m_nmtState);
        if (err != OD_OK) return std::nullopt;
        return std::string(buf, len);
    }

private:
    NMT_State_t &m_nmtState;
};

/* Usage example: */
/*
    NMT_State_t nmtState = NMT_PRE_OPERATIONAL;
    ObjectDictionary od(nmtState);

    // Read heartbeat time
    if (auto hb = od.read<uint16_t>(0x1017, 0x00)) {
        printf("Heartbeat: %u ms\n", *hb);
    }

    // Write Kp gain
    od.write<float>(0x3000, 0x01, 3.14f);

    // Read device name
    if (auto name = od.readString(0x1008, 0x00)) {
        printf("Device: %s\n", name->c_str());
    }
*/
```

### 8.4 SDO Server Integration

When an SDO Download request (write from master to node) arrives on the CAN bus,
the SDO server calls `od_write` after decoding the CAN frame:

```c
/* sdo_server.c — simplified SDO server handler */
#include "can_driver.h"
#include "canopen_od.h"
#include "od_access.h"
#include "od_error.h"

#define SDO_SERVER_COBID_RX  0x600   /* COB-ID base for SDO rx: 0x600 + NodeID */
#define SDO_SERVER_COBID_TX  0x580   /* COB-ID base for SDO tx: 0x580 + NodeID */

/* SDO command specifiers (simplified) */
#define SDO_CS_DOWNLOAD_4  0x23  /* Download 4 bytes, expedited */
#define SDO_CS_DOWNLOAD_3  0x27  /* Download 3 bytes, expedited */
#define SDO_CS_DOWNLOAD_2  0x2B  /* Download 2 bytes, expedited */
#define SDO_CS_DOWNLOAD_1  0x2F  /* Download 1 byte,  expedited */
#define SDO_CS_UPLOAD_REQ  0x40  /* Upload request */
#define SDO_CS_UPLOAD_4    0x43  /* Upload response 4 bytes     */
#define SDO_CS_UPLOAD_1    0x4F  /* Upload response 1 byte      */
#define SDO_CS_ABORT       0x80  /* Abort transfer              */

void sdo_server_process_frame(const CAN_Frame_t *frame,
                               uint8_t nodeId,
                               NMT_State_t nmtState)
{
    if (frame->cobId != (SDO_SERVER_COBID_RX + nodeId)) return;
    if (frame->dlc < 8) return;

    uint8_t  cs       = frame->data[0];
    uint16_t index    = (uint16_t)frame->data[1] | ((uint16_t)frame->data[2] << 8);
    uint8_t  subIndex = frame->data[3];
    OD_Error_t  result;
    CAN_Frame_t response = { .cobId = SDO_SERVER_COBID_TX + nodeId, .dlc = 8 };

    if (cs == SDO_CS_UPLOAD_REQ) {
        /* SDO Upload (master reads OD entry) */
        uint8_t  buf[4] = {0};
        uint16_t actualLen = 0;

        result = od_read(index, subIndex, buf, sizeof(buf), &actualLen, nmtState);
        if (result == OD_OK) {
            uint8_t responseCs = 0x43 | ((4 - actualLen) << 2); /* encode length */
            response.data[0] = responseCs;
            response.data[1] = (uint8_t)(index & 0xFF);
            response.data[2] = (uint8_t)(index >> 8);
            response.data[3] = subIndex;
            response.data[4] = buf[0];
            response.data[5] = buf[1];
            response.data[6] = buf[2];
            response.data[7] = buf[3];
        } else {
            /* Send abort */
            response.data[0] = SDO_CS_ABORT;
            response.data[1] = (uint8_t)(index & 0xFF);
            response.data[2] = (uint8_t)(index >> 8);
            response.data[3] = subIndex;
            uint32_t abort_code = (uint32_t)result;
            memcpy(&response.data[4], &abort_code, 4);
        }
        can_transmit(&response);

    } else if ((cs & 0xE0) == 0x20) {
        /* SDO Download (master writes OD entry) */
        uint8_t  byteCount = 4 - ((cs >> 2) & 0x03); /* expedited size */
        result = od_write(index, subIndex, &frame->data[4], byteCount, nmtState);

        if (result == OD_OK) {
            response.data[0] = 0x60; /* download response */
            response.data[1] = (uint8_t)(index & 0xFF);
            response.data[2] = (uint8_t)(index >> 8);
            response.data[3] = subIndex;
            memset(&response.data[4], 0, 4);
        } else {
            response.data[0] = SDO_CS_ABORT;
            response.data[1] = (uint8_t)(index & 0xFF);
            response.data[2] = (uint8_t)(index >> 8);
            response.data[3] = subIndex;
            uint32_t abort_code = (uint32_t)result;
            memcpy(&response.data[4], &abort_code, 4);
        }
        can_transmit(&response);
    }
}
```

---

## 9. OD Lookup Strategies

The OD lookup strategy has a direct impact on real-time performance, especially
on embedded targets with many OD entries.

### 9.1 Linear Search

The simplest approach — iterate through every entry until the index+sub-index match.

```c
const OD_Entry_t *od_find_linear(uint16_t index, uint8_t subIndex)
{
    for (uint16_t i = 0; i < g_od.count; ++i) {
        if (g_od.entries[i].index    == index &&
            g_od.entries[i].subIndex == subIndex) {
            return &g_od.entries[i];
        }
    }
    return NULL;
}
```

```
  Performance:  O(N) worst case
  Memory:       No extra memory needed
  Best for:     Small ODs (<30 entries), startup/config access only
```

### 9.2 Binary Search (Sorted Table)

If the OD entry table is sorted by (index, subIndex), a binary search gives O(log N):

```c
#include <stdlib.h>

static int od_compare(const void *a, const void *b)
{
    const OD_Entry_t *ea = (const OD_Entry_t *)a;
    const OD_Entry_t *eb = (const OD_Entry_t *)b;
    if (ea->index != eb->index)
        return (int)ea->index - (int)eb->index;
    return (int)ea->subIndex - (int)eb->subIndex;
}

const OD_Entry_t *od_find_binary(uint16_t index, uint8_t subIndex)
{
    OD_Entry_t key = { .index = index, .subIndex = subIndex };
    return (const OD_Entry_t *)bsearch(&key, g_od.entries, g_od.count,
                                        sizeof(OD_Entry_t), od_compare);
}
```

```
  Performance:  O(log N)
  Memory:       No extra memory needed
  Requirement:  Table MUST be sorted by (index, subIndex)
  Best for:     Medium ODs (30-500 entries), SDO access
```

### 9.3 Hash Table Lookup

For large ODs or high-frequency SDO access, a hash table gives near O(1) lookup:

```c
/* Simple open-addressing hash table for OD entries */
#define OD_HASH_SIZE  256   /* must be power of 2 */
#define OD_HASH_MASK  (OD_HASH_SIZE - 1)

static const OD_Entry_t *od_hash[OD_HASH_SIZE];

/* Pack (index, sub-index) into a 24-bit key */
static inline uint32_t od_key(uint16_t index, uint8_t sub) {
    return ((uint32_t)index << 8) | sub;
}

static inline uint8_t od_hash_fn(uint32_t key) {
    /* Knuth multiplicative hash */
    return (uint8_t)((key * 2654435761UL) & OD_HASH_MASK);
}

void od_hash_build(void)
{
    memset(od_hash, 0, sizeof(od_hash));
    for (uint16_t i = 0; i < g_od.count; ++i) {
        uint32_t key   = od_key(g_od.entries[i].index, g_od.entries[i].subIndex);
        uint8_t  slot  = od_hash_fn(key);
        /* Linear probing for collision resolution */
        while (od_hash[slot] != NULL) {
            slot = (slot + 1) & OD_HASH_MASK;
        }
        od_hash[slot] = &g_od.entries[i];
    }
}

const OD_Entry_t *od_find_hash(uint16_t index, uint8_t subIndex)
{
    uint32_t key  = od_key(index, subIndex);
    uint8_t  slot = od_hash_fn(key);

    while (od_hash[slot] != NULL) {
        if (od_hash[slot]->index    == index &&
            od_hash[slot]->subIndex == subIndex) {
            return od_hash[slot];
        }
        slot = (slot + 1) & OD_HASH_MASK;
    }
    return NULL;
}
```

```
  Performance:  O(1) average, O(N) worst (collision)
  Memory:       OD_HASH_SIZE * sizeof(pointer) extra
  Requirement:  Hash table must be built at startup (od_hash_build)
  Best for:     Large ODs (>200 entries), PDO callback lookup
```

### 9.4 Two-Level Lookup (Index + Sub-index)

A practical middle ground: a sorted array of index "buckets", each pointing to a
sub-array of sub-index entries:

```
  Level 1 — Index lookup (binary search, fast):
  +--------+--------+--------+--------+
  | 0x1000 | 0x1018 | 0x3000 | 0x6040 |  <-- sorted index array
  +---+----+---+----+---+----+---+----+
      |        |        |        |
      v        v        v        v
  Level 2 — Sub-index lookup (linear, tiny sub-arrays):
  [0x00]   [0x00..04] [0x00..04] [0x00]
```

This approach is used by CANopenNode and similar open-source stacks.

### 9.5 Lookup Strategy Comparison

```
  +-----------------+-----------+-----------+----------+---------------+
  | Strategy        | Time      | Memory    | Code     | Best for      |
  |                 | Complexity| Overhead  | Complexity               |
  +-----------------+-----------+-----------+----------+---------------+
  | Linear Search   | O(N)      | None      | Low      | <30 entries   |
  | Binary Search   | O(log N)  | None      | Low      | 30-500 entries|
  | Hash Table      | O(1) avg  | High      | Medium   | >200 entries  |
  | Two-Level       | O(log M)+ | Low-Med   | Medium   | Any size      |
  |                 | O(K)      |           |          | (production)  |
  +-----------------+-----------+-----------+----------+---------------+
  N = total entries, M = unique indices, K = sub-entries per index
```

---

## 10. Advanced Topics

### 10.1 OD Callbacks (Pre/Post-Write Hooks)

Many CANopen stacks allow attaching callbacks to OD entries to react to changes
(e.g. applying a new setpoint immediately when written via SDO):

```c
/* Extend OD_Entry_t with callback function pointers */
typedef OD_Error_t (*OD_WriteCallback_t)(uint16_t index, uint8_t subIndex,
                                          const void *pNewValue, uint16_t len);
typedef OD_Error_t (*OD_ReadCallback_t)(uint16_t index, uint8_t subIndex,
                                         void *pValue, uint16_t *pLen);

typedef struct {
    /* ... all previous fields ... */
    OD_WriteCallback_t onWrite;   /* NULL = no callback */
    OD_ReadCallback_t  onRead;    /* NULL = no callback (use stored value) */
} OD_Entry_Extended_t;

/* Example callback: apply new heartbeat time immediately */
static OD_Error_t on_heartbeat_write(uint16_t index, uint8_t subIndex,
                                      const void *pVal, uint16_t len)
{
    (void)index; (void)subIndex;
    if (len != 2) return OD_ERR_BAD_LENGTH;
    uint16_t newTime;
    memcpy(&newTime, pVal, 2);
    heartbeat_timer_restart(newTime);   /* apply immediately */
    return OD_OK;
}
```

### 10.2 Non-Volatile Storage (0x1010 / 0x1011)

Index 0x1010 (Store Parameters) and 0x1011 (Restore Default Parameters) control
persistence. Writing the magic word `"save"` (0x65766173) to 0x1010 sub 0x01
triggers a save to non-volatile memory:

```c
#define OD_STORE_MAGIC    0x65766173UL  /* ASCII "save" little-endian */
#define OD_RESTORE_MAGIC  0x64616F6CUL  /* ASCII "load" */

OD_Error_t on_store_params_write(uint16_t index, uint8_t sub,
                                   const void *pVal, uint16_t len)
{
    uint32_t magic;
    if (len != 4) return OD_ERR_BAD_LENGTH;
    memcpy(&magic, pVal, 4);
    if (magic == OD_STORE_MAGIC) {
        return nvm_save_od() ? OD_OK : OD_ERR_HARDWARE;
    }
    return OD_ERR_VALUE_HIGH;  /* wrong magic */
}
```

### 10.3 PDO Mapping Validation

When a master writes a new PDO mapping at 0x1600/0x1A00, validate that:

1. The mapped object exists in the OD.
2. The total bit count does not exceed 64 bits (8 bytes, one CAN frame).
3. All mapped objects have the correct access direction.

```c
OD_Error_t validate_tpdo_mapping(uint8_t pdoNum)
{
    uint16_t mapIndex = 0x1A00 + pdoNum;
    uint8_t  count    = 0;
    uint32_t totalBits = 0;

    od_read(mapIndex, 0x00, &count, 1, NULL, NMT_PRE_OPERATIONAL);

    for (uint8_t i = 1; i <= count; ++i) {
        uint32_t mapping = 0;
        OD_Error_t err = od_read(mapIndex, i, &mapping, 4, NULL, NMT_PRE_OPERATIONAL);
        if (err != OD_OK) return err;

        uint16_t objIndex  = (uint16_t)(mapping >> 16);
        uint8_t  objSub    = (uint8_t)((mapping >> 8) & 0xFF);
        uint8_t  bitLen    = (uint8_t)(mapping & 0xFF);

        /* Check the mapped object exists */
        const OD_Entry_t *mapped = od_find(objIndex, objSub);
        if (!mapped) return OD_ERR_NOT_FOUND;

        /* Check readable (TPDO sends data = needs to read OD) */
        if (mapped->access == OD_ACCESS_WO) return OD_ERR_READ_ONLY;

        totalBits += bitLen;
    }

    if (totalBits > 64) return OD_ERR_BAD_LENGTH;
    return OD_OK;
}
```

### 10.4 Electronic Data Sheet (EDS)

An EDS file is an INI-style text file that describes the OD structure of a CANopen
device. It allows generic tools (such as CANopen Magic, PEAK-System PCAN-Explorer,
or Kvaser CanKing) to automatically discover and configure any compliant node.

```
  ; EDS snippet for identity object
  [1018]
  ParameterName=Identity Object
  ObjectType=0x9           ; RECORD
  SubNumber=0x5

  [1018sub0]
  ParameterName=Highest Sub-Index Supported
  ObjectType=0x7           ; VAR
  DataType=0x0005          ; UNSIGNED8
  AccessType=ro
  DefaultValue=0x4
  PDOMapping=0

  [1018sub1]
  ParameterName=Vendor-ID
  ObjectType=0x7
  DataType=0x0007          ; UNSIGNED32
  AccessType=ro
  DefaultValue=0xDEADBEEF
  PDOMapping=0
```

---

## 11. Summary

The Object Dictionary is the architectural backbone of every CANopen device.
Understanding its structure is prerequisite for any serious CANopen development.

### Key Points

**Addressing Space**
The OD uses a 16-bit index (0x0000–0xFFFF) combined with an 8-bit sub-index
(0x00–0xFF), giving over 16 million addressable locations. In practice, the usable
space is partitioned into communication profile, manufacturer-specific, and device
profile areas.

**Object Types**
VAR holds a single scalar or string value at sub-index 0. ARRAY holds an ordered
sequence of identically-typed values with the count at sub-index 0. RECORD holds
a heterogeneous struct-like collection, also with count at sub-index 0. The
sub-index 0 count convention is universal — always check it before iterating.

**Data Types**
CiA 301 defines numeric types (INTEGER8 through INTEGER64, UNSIGNED8 through
UNSIGNED64, REAL32, REAL64), string types (VISIBLE_STRING, OCTET_STRING,
UNICODE_STRING), and complex composite types (PDO mapping, Identity record).
All multi-byte values on the wire are little-endian.

**Access Attributes**
RO (read-only) and RW (read-write) are the most common. RWW objects — including
PDO mapping and communication parameters — can only be written when the node is
in PRE-OPERATIONAL or STOPPED NMT state; this is a hard protocol requirement that
your OD write path must enforce.

**Profile Areas**

```
  +---------------------+-----------------------------------+
  | Area                | Index Range    | Defined by       |
  +---------------------+-----------------------------------+
  | Communication       | 0x1000–0x1FFF  | CiA 301 (fixed)  |
  | Manufacturer-Spec.  | 0x2000–0x5FFF  | Vendor (free)    |
  | Device Profile      | 0x6000–0x9FFF  | CiA 4xx          |
  +---------------------+-----------------------------------+
```

**Lookup Strategies**
Use linear search for small ODs, binary search on a sorted table for medium ones,
and a hash table or two-level lookup for large/high-frequency access patterns.
The choice significantly affects SDO response latency on constrained MCUs.

**Implementation Checklist**

```
  [ ] OD entry table defined with index, sub-index, type, access, data pointer
  [ ] od_find() with an appropriate lookup strategy
  [ ] od_read() enforces WO restriction
  [ ] od_write() enforces RO restriction
  [ ] od_write() enforces NMT state for RWW objects
  [ ] SDO server calls od_read/od_write and returns correct abort codes
  [ ] PDO mapping objects (0x1600/0x1A00) validated before use
  [ ] 0x1010/0x1011 store/restore callbacks connected to NVM layer
  [ ] EDS file generated and maintained alongside firmware
```

---

*End of document — CANopen Object Dictionary Structure & Access*

---

> **Related Topics:**
> `06_SDO_Protocol.md` | `08_PDO_Configuration.md` | `09_NMT_Protocol.md` | `10_EDS_DCF_Files.md`