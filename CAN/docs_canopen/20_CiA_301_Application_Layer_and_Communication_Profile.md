# CiA 301 — CANopen Application Layer & Communication Profile

> **Standard:** CiA 301 v4.2.0  
> **Scope:** Application layer and communication profile for CANopen devices  
> **Physical Layer:** ISO 11898-1/2 (CAN bus)

---

## Table of Contents

1. [Overview](#overview)
2. [CANopen Network Architecture](#canopen-network-architecture)
3. [Object Dictionary](#object-dictionary)
4. [Mandatory and Optional Objects](#mandatory-and-optional-objects)
5. [Device Identity Object (0x1018)](#device-identity-object-0x1018)
6. [Communication Objects (COB-IDs)](#communication-objects-cob-ids)
7. [Process Data Objects (PDO)](#process-data-objects-pdo)
8. [Service Data Objects (SDO)](#service-data-objects-sdo)
9. [Network Management (NMT)](#network-management-nmt)
10. [Error Behaviour Object (0x1029)](#error-behaviour-object-0x1029)
11. [Emergency Object (EMCY)](#emergency-object-emcy)
12. [Node Guarding vs Heartbeat](#node-guarding-vs-heartbeat)
13. [Guard/Heartbeat Interplay](#guardheartbeat-interplay)
14. [Sync Object](#sync-object)
15. [Time Stamp Object](#time-stamp-object)
16. [Full Conformance Checklist](#full-conformance-checklist)
17. [C/C++ Programming Examples](#cc-programming-examples)
18. [Summary](#summary)

---

## Overview

CiA 301 defines the **CANopen Application Layer and Communication Profile**. It specifies how CANopen devices communicate over a CAN bus network, defining the object dictionary structure, communication services, and network management. Every conformant CANopen device — whether a motor drive, sensor, I/O module, or PLC — must implement the mandatory subset defined in this standard.

CANopen separates concerns cleanly:

- **Application layer**: What data is exchanged (Object Dictionary, PDO, SDO)
- **Communication profile**: How it is exchanged (NMT, EMCY, SYNC, TIME, Heartbeat/Guard)
- **Device profile**: Device-type-specific behaviour (CiA 402, CiA 404, etc.)

```
+---------------------------------------------------------------+
|                     USER APPLICATION                          |
+---------------------------------------------------------------+
|           CANopen Application Layer (CiA 301)                 |
|  +----------+  +---------+  +--------+  +--------+            |
|  |   PDO    |  |   SDO   |  |  NMT   |  |  EMCY  |            |
|  +----------+  +---------+  +--------+  +--------+            |
|  +----------+  +---------+  +--------+  +--------+            |
|  |   SYNC   |  |  TIME   |  | Guard  |  | HBeat  |            |
|  +----------+  +---------+  +--------+  +--------+            |
+---------------------------------------------------------------+
|              Object Dictionary (OD) 0x0000–0xFFFF             |
+---------------------------------------------------------------+
|              CAN Data Link Layer (ISO 11898-1)                |
+---------------------------------------------------------------+
|              CAN Physical Layer (ISO 11898-2)                 |
+---------------------------------------------------------------+
```

---

## CANopen Network Architecture

A CANopen network consists of one **NMT Master** and up to **127 NMT Slaves** (Node-IDs 1–127). Node-ID 0 is reserved for broadcast.

```
                    CAN BUS (up to 1 Mbit/s)
    _______________________________________________
   |          |          |          |             |
+------+  +------+  +------+  +------+       +------+
| NMT  |  | Node |  | Node |  | Node |  ...  | Node |
|Master|  | ID=1 |  | ID=2 |  | ID=3 |       | ID=N |
| (PLC)|  |(Drv) |  |(I/O) |  |(Sen) |       |      |
+------+  +------+  +------+  +------+       +------+
   |
   |  Controls NMT state machine of all nodes
   |  Reads/writes OD entries via SDO
   |  Receives PDO process data
   +-- Optionally monitors heartbeat / guard
```

**Bit Timing (standard rates):**

| Baud Rate | Max Bus Length|
|-----------|---------------|
| 1 Mbit/s  | 25 m          |
| 500 kbit/s| 100 m         |
| 250 kbit/s| 250 m         |
| 125 kbit/s| 500 m         |
| 50 kbit/s | 1000 m        |
| 20 kbit/s | 2500 m        |
| 10 kbit/s | 5000 m        |

---

## Object Dictionary

The **Object Dictionary (OD)** is the heart of every CANopen device. It is an ordered table of all data objects accessible over the network. Each entry is identified by a 16-bit **Index** and an 8-bit **Sub-Index**.

```
Index (hex)       Area
-----------       ----
0x0000            Reserved
0x0001–0x001F     Static data types
0x0020–0x003F     Complex data types
0x0040–0x005F     Manufacturer-specific complex types
0x0060–0x007F     Device profile static types
0x0080–0x009F     Device profile complex types
0x00A0–0x0FFF     Reserved
0x1000–0x1FFF     Communication Profile Area   <-- CiA 301
0x2000–0x5FFF     Manufacturer-Specific Area
0x6000–0x9FFF     Standardised Device Profile Area
0xA000–0xFFFF     Reserved / future use
```

### Object Types

| Type Code | Name          | Description                                      |
|-----------|---------------|--------------------------------------------------|
| 0x05      | DEFTYPE       | Data type definition                             |
| 0x06      | DEFSTRUCT     | Structure definition                             |
| 0x07      | VAR           | Single variable                                  |
| 0x08      | ARRAY         | Array of uniform elements (Sub-Index 0 = count)  |
| 0x09      | RECORD        | Record of mixed-type fields                      |

### Access Types

| Access | Meaning                           |
|--------|-----------------------------------|
| ro     | Read-only                         |
| wo     | Write-only                        |
| rw     | Read-write                        |
| rwr    | Read-write on receive             |
| rww    | Read-write on write               |
| const  | Constant value, read-only         |

---

## Mandatory and Optional Objects

CiA 301 divides objects into **mandatory** (M) and **optional** (O) categories. A device claiming CiA 301 conformance must implement all mandatory objects.

### Mandatory Communication Objects

| Index  | Sub | Name                          | Type    | Access | M/O |
|--------|-----|-------------------------------|---------|--------|-----|
| 0x1000 | 0   | Device Type                   | UINT32  | ro     | **M** |
| 0x1001 | 0   | Error Register                | UINT8   | ro     | **M** |
| 0x1018 | 0–4 | Identity Object               | RECORD  | ro     | **M** |

### Mandatory (if PDO supported)

| Index  | Sub | Name                          | Type    | M/O |
|--------|-----|-------------------------------|---------|-----|
| 0x1400–0x141F | 0–2 | Receive PDO Communication Params | RECORD | **M** (if RPDO) |
| 0x1600–0x161F | 0–N | Receive PDO Mapping Params    | RECORD  | **M** (if RPDO) |
| 0x1800–0x181F | 0–5 | Transmit PDO Communication Params | RECORD | **M** (if TPDO) |
| 0x1A00–0x1A1F | 0–N | Transmit PDO Mapping Params   | RECORD  | **M** (if TPDO) |

### Optional but Common Communication Objects

| Index  | Name                              | M/O |
|--------|-----------------------------------|-----|
| 0x1002 | Manufacturer Status Register      | O   |
| 0x1003 | Pre-defined Error Field           | O   |
| 0x1005 | COB-ID SYNC Message               | O   |
| 0x1006 | Communication Cycle Period        | O   |
| 0x1007 | Synchronous Window Length         | O   |
| 0x1008 | Manufacturer Device Name          | O   |
| 0x1009 | Manufacturer Hardware Version     | O   |
| 0x100A | Manufacturer Software Version     | O   |
| 0x100C | Guard Time                        | O   |
| 0x100D | Life Time Factor                  | O   |
| 0x1010 | Store Parameters                  | O   |
| 0x1011 | Restore Default Parameters        | O   |
| 0x1012 | COB-ID TIME Message               | O   |
| 0x1013 | High Resolution Timestamp         | O   |
| 0x1014 | COB-ID EMCY                       | O   |
| 0x1015 | Inhibit Time EMCY                 | O   |
| 0x1016 | Consumer Heartbeat Time           | O   |
| 0x1017 | Producer Heartbeat Time           | O   |
| 0x1019 | Synchronous Counter Overflow Val  | O   |
| 0x1020 | Verify Configuration              | O   |
| 0x1021 | Store EDS                         | O   |
| 0x1022 | Store Format                      | O   |
| 0x1023–0x1026 | OS Command / Prompt / Debug| O   |
| 0x1027 | Module List                       | O   |
| 0x1028 | Emergency Consumer Object         | O   |
| 0x1029 | Error Behaviour Object            | O   |
| 0x102A | NMT Inhibit Time                  | O   |

### Device Type Object (0x1000)

The 32-bit device type encodes device profile number and additional information:

```
  Bit 31..16          Bit 15..0
+------------------+------------------+
| Additional Info  | Device Profile   |
| (Mfr-specific)   | Number (CiA 4xx) |
+------------------+------------------+

Example: 0x00000402 = CiA 402 (Drives & Motion)
         0x00000401 = CiA 401 (Generic I/O)
         0x00000404 = CiA 404 (Measuring Devices)
```

---

## Device Identity Object (0x1018)

The **Identity Object** at index `0x1018` is **mandatory** for all CANopen devices. It provides a standardised way for a master to identify every node on the bus without any prior knowledge.

### Structure

| Sub-Index | Name              | Type   | Description                                        |
|-----------|-------------------|--------|----------------------------------------------------|
| 0x00      | Highest Sub-Index | UINT8  | Number of sub-entries (2, 3, or 4)                 |
| 0x01      | Vendor ID         | UINT32 | CiA-assigned manufacturer ID (mandatory)           |
| 0x02      | Product Code      | UINT32 | Manufacturer's product code (mandatory)            |
| 0x03      | Revision Number   | UINT32 | Revision (optional, Sub-0 >= 3 to include)         |
| 0x04      | Serial Number     | UINT32 | Unique device serial (optional, Sub-0 >= 4)        |

### Revision Number Encoding

The revision number uses a split encoding to allow minor revisions without breaking compatibility checks:

```
  Bit 31..16               Bit 15..0
+------------------------+------------------------+
|   Major Revision       |   Minor Revision       |
|   (interface change)   |   (compatible update)  |
+------------------------+------------------------+

Example: 0x00020003 = Major 2, Minor 3
```

A master can match a node using Vendor ID + Product Code + (optionally) Major Revision while ignoring the minor revision, enabling firmware update compatibility checks.

### Identity Object in EDS (Electronic Data Sheet)

```ini
[1018]
ParameterName=Identity Object
ObjectType=0x09
SubNumber=0x05

[1018sub0]
ParameterName=Highest Sub-Index Supported
ObjectType=0x07
DataType=0x0005
AccessType=ro
DefaultValue=0x04
PDOMapping=0

[1018sub1]
ParameterName=Vendor-ID
ObjectType=0x07
DataType=0x0007
AccessType=ro
DefaultValue=0x00000123
PDOMapping=0

[1018sub2]
ParameterName=Product Code
ObjectType=0x07
DataType=0x0007
AccessType=ro
DefaultValue=0x00000456
PDOMapping=0

[1018sub3]
ParameterName=Revision Number
ObjectType=0x07
DataType=0x0007
AccessType=ro
DefaultValue=0x00010000
PDOMapping=0

[1018sub4]
ParameterName=Serial Number
ObjectType=0x07
DataType=0x0007
AccessType=ro
DefaultValue=0x00000001
PDOMapping=0
```

---

## Communication Objects (COB-IDs)

CANopen uses the CAN 11-bit identifier as a **COB-ID** (Communication Object Identifier). The default assignment follows a **Pre-defined Connection Set** based on Node-ID.

```
COB-ID = Function Code (4 bits) + Node-ID (7 bits)

  Bit 10  9  8  7  6  5  4  3  2  1  0
        +--+--+--+--+--+--+--+--+--+--+
        | FC  FC  FC  FC| NID NID NID NID NID NID NID |
        +--+--+--+--+--+--+--+--+--+--+

Function Codes:
  0000  = NMT               (0x000, no Node-ID)
  0001  = SYNC              (0x080, no Node-ID)
  0001  = EMCY              (0x080 + Node-ID)
  0010  = TIME              (0x100, no Node-ID)
  0011  = TPDO1             (0x180 + Node-ID)
  0100  = RPDO1             (0x200 + Node-ID)
  0101  = TPDO2             (0x280 + Node-ID)
  0110  = RPDO2             (0x300 + Node-ID)
  0111  = TPDO3             (0x380 + Node-ID)
  1000  = RPDO3             (0x400 + Node-ID)
  1001  = TPDO4             (0x480 + Node-ID)
  1010  = RPDO4             (0x500 + Node-ID)
  1011  = SDO (Tx, server)  (0x580 + Node-ID)
  1100  = SDO (Rx, client)  (0x600 + Node-ID)
  1110  = NMT Error Control (0x700 + Node-ID)
```

### Default COB-ID Table for Node-ID = 5

| Object  | COB-ID (hex) | Direction (from node) |
|---------|--------------|-----------------------|
| NMT     | 0x000        | Receive               |
| SYNC    | 0x080        | Receive               |
| EMCY    | 0x085        | Transmit              |
| TPDO1   | 0x185        | Transmit              |
| RPDO1   | 0x205        | Receive               |
| TPDO2   | 0x285        | Transmit              |
| RPDO2   | 0x305        | Receive               |
| TPDO3   | 0x385        | Transmit              |
| RPDO3   | 0x405        | Receive               |
| TPDO4   | 0x485        | Transmit              |
| RPDO4   | 0x505        | Receive               |
| SDO Tx  | 0x585        | Transmit              |
| SDO Rx  | 0x605        | Receive               |
| Heartbeat | 0x705      | Transmit              |

---

## Process Data Objects (PDO)

PDOs provide **real-time, low-overhead** data exchange. A PDO carries up to 8 bytes of process data in a single CAN frame with **no protocol overhead** — the COB-ID alone identifies the content (via the mapping).

### PDO Communication Parameters (0x1400–0x141F for RPDO, 0x1800–0x181F for TPDO)

| Sub | Name              | Description                                      |
|-----|-------------------|--------------------------------------------------|
| 0   | Highest Sub-Index | Number of entries                                |
| 1   | COB-ID            | Bit 31=disable, Bit 30=RTR, Bits 10–0=CAN ID     |
| 2   | Transmission Type | 0–240=synchronous, 254=event-driven async, 255=event |
| 3   | Inhibit Time      | (TPDO only) Min interval in 100 µs steps         |
| 4   | Reserved          |                                                  |
| 5   | Event Timer       | (TPDO only) Periodic transmit in ms              |
| 6   | SYNC Start Value  | (TPDO only) First SYNC counter to start with     |

### PDO Transmission Types

```
Transmission Type   Behaviour
-----------------   ---------
0                   Synchronous, acyclic (on SYNC after change)
1                   Synchronous, every 1 SYNC period
2                   Synchronous, every 2 SYNC periods
...
240                 Synchronous, every 240 SYNC periods
241–253             Reserved
254                 Asynchronous, manufacturer-specific event
255                 Asynchronous, device profile or application event
```

### PDO Mapping Parameters (0x1600–0x161F for RPDO, 0x1A00–0x1A1F for TPDO)

Each sub-index (1–N) holds a 32-bit mapping entry:

```
Bits 31..16   Bits 15..8   Bits 7..0
+------------+------------+----------+
|   Index    |  Sub-Index |  Length  |
|  (OD addr) |  (OD sub)  | (bits)   |
+------------+------------+----------+

Example: 0x60410008 maps OD[0x6041][0x00], 8 bits (Status Word low byte)
         0x60410010 maps OD[0x6041][0x00], 16 bits (Status Word full)
```

### PDO Communication Flow

```
Master/Controller                    Node (e.g. Motor Drive)
      |                                        |
      |-- SYNC (0x080) ----------------------->|
      |                                        | [updates TPDO data]
      |<-- TPDO1 (0x181+NID, 8 bytes) ---------|
      |                                        |
      |-- RPDO1 (0x201+NID, 8 bytes) --------->|
      |                                        | [applies setpoint data]
      |-- SYNC (0x080) ----------------------->|
      |<-- TPDO1 (0x181+NID, 8 bytes) ---------|
      |                                        |
```

---

## Service Data Objects (SDO)

SDOs provide **confirmed, reliable** access to any entry in a node's Object Dictionary. Unlike PDOs, SDOs use a request/response protocol and can transfer payloads larger than 8 bytes via segmented or block transfer.

### SDO COB-IDs

- Client → Server (write/read request): **0x600 + Node-ID**
- Server → Client (response): **0x580 + Node-ID**

### SDO Expedited Transfer (≤ 4 bytes)

```
  Byte:  0       1       2       3       4       5       6       7
        +-------+-------+-------+-------+-------+-------+-------+-------+
 Req:   |  cs   |  Index (lo)   |  Index (hi)   | Sub-  |     Data      |
        | (0x40)|               |               | Index |  (up to 4B)   |
        +-------+-------+-------+-------+-------+-------+-------+-------+

        +-------+-------+-------+-------+-------+-------+-------+-------+
 Resp:  |  cs   |  Index (lo)   |  Index (hi)   | Sub-  |     Data      |
        | (0x43)|               |               | Index |  (4 bytes)    |
        +-------+-------+-------+-------+-------+-------+-------+-------+

cs = command specifier
  0x40 = Upload (read) request
  0x43 = Upload response, 4 bytes
  0x47 = Upload response, 3 bytes
  0x4B = Upload response, 2 bytes
  0x4F = Upload response, 1 byte
  0x23 = Download (write) request, 4 bytes
  0x27 = Download request, 3 bytes
  0x2B = Download request, 2 bytes
  0x2F = Download request, 1 byte
  0x60 = Download response (acknowledge)
  0x80 = Abort
```

### SDO Abort Codes (common)

| Code       | Meaning                                    |
|------------|--------------------------------------------|
| 0x05030000 | Toggle bit not alternated                  |
| 0x05040000 | SDO protocol timed out                     |
| 0x05040001 | Client/server command specifier unknown    |
| 0x06010000 | Unsupported access to object               |
| 0x06010001 | Read of write-only object                  |
| 0x06010002 | Write of read-only object                  |
| 0x06020000 | Object does not exist in OD                |
| 0x06040041 | Object cannot be mapped to PDO             |
| 0x06040042 | PDO length exceeded                        |
| 0x06070010 | Data type mismatch                         |
| 0x06090011 | Sub-Index does not exist                   |
| 0x06090030 | Value out of range                         |
| 0x08000000 | General error                              |
| 0x08000020 | Data cannot be transferred or stored       |
| 0x08000022 | Data cannot be stored — local control      |
| 0x08000023 | Data cannot be stored — device state       |

### SDO Segmented Transfer

Used for data > 4 bytes (strings, large arrays):

```
Client                                  Server
  |                                        |
  |-- Initiate Download (0x21, size=N) --->|
  |<-- Initiate Download Response ---------|
  |-- Download Segment (toggle=0, data) -->|
  |<-- Segment Response (toggle=0) --------|
  |-- Download Segment (toggle=1, data) -->|
  |<-- Segment Response (toggle=1) --------|
  |   ... (toggle alternates) ...          |
  |-- Download Segment (last, c=1) ------->|
  |<-- Segment Response -------------------|
  |                                        |
```

### SDO Block Transfer

Optimised for large bulk transfers (firmware, EDS files). Reduces overhead by sending multiple 7-byte segments per block without individual acknowledgement:

```
Client                                  Server
  |                                        |
  |-- Block Download Initiate ------------>|
  |<-- Block Download Initiate Response ---|
  |-- Block Segment 1 (seqno=1) ---------->|
  |-- Block Segment 2 (seqno=2) ---------->|
  |   ...                                  |
  |-- Block Segment N (seqno=N, last) ---->|
  |<-- Block Download Sub-Block Response --|  (ackseq, blksize)
  |-- Block Download End ----------------->|
  |<-- Block Download End Response --------|
  |                                        |
```

---

## Network Management (NMT)

NMT controls the **state machine** of every CANopen node. The NMT master sends single-frame commands using COB-ID **0x000**.

### NMT State Machine

```
                          +----------------+
         Power-on / Reset |                |
      +-------------------> INITIALISATION |
      |                   |                |  
      |                   +----+-----------+
      |                        |
      |                   Auto-transition after boot-up
      |                        |
      |                   +----v------+
      |     +-------------+  PRE-     |+<-----------+
      |     |  NMT cmd    |OPERATIONAL|  NMT cmd    |
      |     | (Start)     +----+------+ (Pre-op)    |
      |     |                  |                    |
      |  +--v--------+      NMT cmd              +--+------+
      |  |           |      (Stop)               |         |
      |  |OPERATIONAL|<------------------------> | STOPPED |
      |  |           |      NMT cmd (Start)      |         |
      |  +-----------+                           +---------+
      |       |                                      |
      |  NMT Reset Node / Reset Communication        |
      +----------------------------------------------+
```

### NMT Command Frame

```
COB-ID: 0x000 (broadcast) or 0x000 (always broadcast)
DLC: 2 bytes

  Byte 0: Command Specifier
  Byte 1: Node-ID (0 = all nodes)

Command Specifiers:
  0x01 = Start Remote Node      -> Operational
  0x02 = Stop Remote Node       -> Stopped
  0x80 = Enter Pre-Operational  -> Pre-Operational
  0x81 = Reset Node             -> Initialisation (full reset)
  0x82 = Reset Communication    -> Initialisation (comm reset only)
```

### Boot-Up Message

After initialisation, every node sends a single boot-up frame to announce its readiness:

```
COB-ID: 0x700 + Node-ID
DLC: 1
Data: 0x00
```

### NMT State Capabilities

| State           | SDO | PDO | SYNC | EMCY | Heartbeat |
|-----------------|-----|-----|------|------|-----------|
| Initialisation  | No  | No  | No   | No   | No        |
| Pre-Operational | Yes | No  | Yes  | Yes  | Yes       |
| Operational     | Yes | Yes | Yes  | Yes  | Yes       |
| Stopped         | No  | No  | No   | No   | Yes       |

---

## Error Behaviour Object (0x1029)

The **Error Behaviour Object** at index `0x1029` defines how a CANopen node reacts when it detects a communication error (e.g., loss of a PDO, SYNC timeout, or guarding failure). This is an **optional** object that allows fine-grained control over what NMT state the node enters upon various error conditions.

### Structure

| Sub-Index | Name                          | Description                           |
|-----------|-------------------------------|---------------------------------------|
| 0x00      | Highest Sub-Index             | Number of entries (typically 1 or 2)  |
| 0x01      | Communication Error           | Behaviour on generic comm error       |
| 0x02      | Device Profile Specific Error | Behaviour on device-profile error     |

### Behaviour Values

| Value | Meaning                                      |
|-------|----------------------------------------------|
| 0x00  | Pre-Operational (default recommended)        |
| 0x01  | No state change (ignore the error state)     |
| 0x02  | Stopped                                      |

### Default Behaviour without 0x1029

If the object is absent, the default behaviour upon a communication error is to transition to **Pre-Operational**, which effectively freezes PDO exchange while keeping SDO configuration access alive.

```
Error detected (e.g., NMT guard timeout)
           |
           v
  +------------------+
  | Check OD[0x1029] |
  | Sub-Index 0x01   |
  +--------+---------+
           |
     +-----+-------+
     |             |
  value=0x00    value=0x01    value=0x02
     |             |              |
     v             v              v
Pre-Operational  No change     Stopped
(SDO OK,         (stay in      (all comm
 PDO frozen)      Operational)  frozen)
```

---

## Emergency Object (EMCY)

The **Emergency Object** reports serious device errors. It uses COB-ID `0x080 + Node-ID` and carries an 8-byte payload.

### EMCY Frame Structure

```
COB-ID: 0x080 + Node-ID
DLC: 8

  Byte 0–1: Emergency Error Code (UINT16, little-endian)
  Byte 2:   Error Register (same as OD[0x1001][0x00])
  Byte 3–7: Manufacturer-Specific Error Field

Error Code categories:
  0x0000  Error Reset / No Error
  0x1xxx  Generic error
  0x2xxx  Current error
  0x3xxx  Voltage error
  0x4xxx  Temperature error
  0x5xxx  Device hardware error
  0x6xxx  Device software error
  0x7xxx  Additional modules error
  0x8xxx  Monitoring error
  0x9xxx  External error
  0xFxxx  Additional function / Device specific
```

### Error Register (0x1001)

The 8-bit error register is always present and updated before sending EMCY:

```
Bit 7: Manufacturer-specific
Bit 6: Reserved
Bit 5: Device profile specific
Bit 4: Communication error (overrun, error state)
Bit 3: Temperature
Bit 2: Voltage
Bit 1: Current
Bit 0: Generic error
```

### Pre-defined Error Field (0x1003)

Optional FIFO of the last N emergency error codes. Sub-Index 0 holds the current count; writing 0x00 clears the list.

---

## Node Guarding vs Heartbeat

CANopen provides two mechanisms for monitoring node liveness. They are **mutually exclusive** — only one should be active at a time on a given node.

### Node Guarding (Legacy, CiA 301 v3.x)

Node guarding uses a **polling model**: the master sends a Remote Frame (RTR) to each node, and the node responds with its current NMT state and a toggle bit.

```
Master                              Node (ID=5)
  |                                    |
  |-- RTR frame (0x705, DLC=1) ------->|
  |<-- Guard Response (0x705, 1B) -----|  (toggle | NMT state)
  |                                    |
  |   [Guard Time = 100ms]             |
  |   [Life Time Factor = 3]           |
  |   [Life Time = 300ms]              |
  |                                    |
  |-- RTR frame (0x705, DLC=1) ------->|
  |<-- Guard Response (0x705, 1B) -----|
  |                                    |
  |   (no response for 300ms)          |
  |   GUARD ERROR -> OD[0x1029]        |
  |                                    |
```

**Guard Response Byte:**

```
Bit 7: Toggle bit (alternates 0/1 each response)
Bits 6..0: NMT State
  0x00 = Boot-Up
  0x04 = Stopped
  0x05 = Operational
  0x7F = Pre-Operational
```

**Configuration:**

| Object  | Description                                     |
|---------|-------------------------------------------------|
| 0x100C  | Guard Time (ms) — how often master polls        |
| 0x100D  | Life Time Factor — node waits Factor × Time     |

### Heartbeat (CiA 301 v4.x, Recommended)

Heartbeat uses a **push model**: each node autonomously broadcasts its state at a configured interval. No RTR frames required. More reliable and simpler.

```
Node (ID=5)                          Master / Consumer
  |                                        |
  |-- Heartbeat (0x705, 1B=0x05) --------->|  (Operational)
  |                                        |  [starts consumer timer]
  |   [1000ms passes]                      |
  |-- Heartbeat (0x705, 1B=0x05) --------->|  [resets consumer timer]
  |                                        |
  |   [1000ms passes]                      |
  |-- Heartbeat (0x705, 1B=0x05) --------->|  [resets consumer timer]
  |                                        |
  |   [NO heartbeat for >1100ms]           |
  |                                        |  HEARTBEAT EVENT
  |                                        |  -> application callback
```

**Heartbeat Configuration:**

| Object  | Sub | Description                                          |
|---------|-----|------------------------------------------------------|
| 0x1017  | 0   | Producer Heartbeat Time (ms), 0 = disabled           |
| 0x1016  | 0   | Highest Sub-Index (number of consumers monitored)    |
| 0x1016  | 1–N | Consumer Heartbeat Time: bits 31..16 = Node-ID, bits 15..0 = timeout (ms) |

**Consumer Entry Encoding:**

```
  Bit 31..24   Bit 23..16   Bit 15..0
+------------+------------+-----------+
|  Reserved  |  Node-ID   |  Time(ms) |
+------------+------------+-----------+

Example: 0x00050064 = monitor Node-ID 5, timeout 100 ms
```

---

## Guard/Heartbeat Interplay

A major architectural decision in every CANopen system is whether to use **Node Guarding** or **Heartbeat**. CiA 301 v4.x deprecates node guarding in favour of heartbeat but must remain backward compatible.

### Simultaneous Activation — What Happens?

CiA 301 explicitly states:

> *"The node guarding protocol and the heartbeat protocol shall not be active at the same time on the same node."*

Activation rules:

```
Guard Time (0x100C) = 0  AND  Life Time Factor (0x100D) = 0
  -> Node Guarding DISABLED

Producer Heartbeat Time (0x1017) = 0
  -> Heartbeat Producer DISABLED

Consumer Heartbeat Time(s) (0x1016) all zero
  -> Heartbeat Consumer DISABLED
```

If a master writes a non-zero value to `0x100C` (Guard Time) while `0x1017` (Heartbeat) is also non-zero, the node should prioritise heartbeat and reject or ignore the guard time configuration — implementation-specific.

### Comparison Table

| Feature              | Node Guarding              | Heartbeat               |
|----------------------|----------------------------|-------------------------|
| Protocol direction   | Master polls (RTR)         | Node pushes             |
| Toggle bit check     | Yes                        | No                      |
| CAN bus load         | Higher (RTR + response)    | Lower (response only)   |
| Master dependency    | Master must be running     | Nodes self-supervise    |
| Peer monitoring      | No (master only)           | Yes (any node consumes) |
| RTR support needed   | Yes                        | No                      |
| CiA status           | Deprecated (legacy)        | Recommended (current)   |
| Configuration        | 0x100C, 0x100D             | 0x1016, 0x1017          |

### Interplay State Diagram

```
                 Power-On
                     |
                     v
             +---------------+
             | Both Disabled |
             |  (default)    |
             +-------+-------+
                     |
         +-----------+-----------+
         |                       |
   Write 0x100C != 0       Write 0x1017 != 0
   (Guard Time)            (HBeat Time)
         |                       |
         v                       v
  +-------------+         +-------------+
  |    Node     |         |  Heartbeat  |
  |  Guarding   |         |   Active    |
  |   Active    |         |             |
  +------+------+         +------+------+
         |                       |
   Write 0x1017 != 0      Write 0x100C != 0
   (Heartbeat set)        (Guard set while HB on)
         |                       |
         v                       v
  +-----------------------------------+
  | CONFLICT: Implementation-specific |
  | (recommended: Heartbeat wins)     |
  +-----------------------------------+
```

### Master-Side Monitoring Strategy (Recommended)

```
For each node in network:
  1. During configuration (Pre-Operational):
     a. Write 0x1017 on node to set producer interval (e.g., 1000 ms)
     b. Write 0x1016[N] on master node with: (node_id << 16) | timeout
        where timeout = 1.5 × producer_interval (e.g., 1500 ms)
  2. Ensure 0x100C = 0 and 0x100D = 0 on all nodes
  3. Start node (NMT Start Remote Node)
  4. Application monitors 0x1016 consumer timeout events
```

---

## Sync Object

The SYNC message provides a network-wide clock for synchronising PDO exchanges. It has no data (DLC = 0) or optionally carries a 1-byte counter (v4.x).

```
COB-ID: 0x080 (default, configured via 0x1005)
DLC: 0 (or 1 with counter)

Communication Cycle Period (0x1006): interval in µs
Sync Window Length (0x1007): PDO must be sent/received within this window after SYNC

     t=0         t=T         t=2T        t=3T
      |           |           |           |
      v           v           v           v
 +--SYNC--+  +--SYNC--+  +--SYNC--+  +--SYNC--+
 |        |  |        |  |        |  |        |
 | window |  | window |  | window |  | window |
 +--------+  +--------+  +--------+  +--------+
   PDO Tx      PDO Tx      PDO Tx      PDO Tx
```

---

## Time Stamp Object

The TIME message broadcasts a 48-bit absolute timestamp to synchronise device clocks.

```
COB-ID: 0x100 (default, configured via 0x1012)
DLC: 6

  Bytes 0–3: Milliseconds after midnight (UINT32, little-endian)
  Bytes 4–5: Days since 1 January 1984 (UINT16, little-endian)
```

---

## Full Conformance Checklist

A device claiming **CiA 301 conformance** must satisfy all mandatory items in this checklist. Optional items are noted and should be documented in the EDS.

### Mandatory Requirements

#### Object Dictionary
- [ ] OD[0x1000] Device Type implemented (ro, UINT32)
- [ ] OD[0x1001] Error Register implemented (ro, UINT8)
- [ ] OD[0x1018] Identity Object implemented (ro, RECORD, Sub-Index 0–2 minimum, Vendor-ID and Product Code valid)
- [ ] All OD objects have correct data types per CiA 301 specification
- [ ] Read-only objects reject write attempts with SDO abort 0x06010002
- [ ] Write-only objects reject read attempts with SDO abort 0x06010001
- [ ] Non-existent objects return SDO abort 0x06020000

#### SDO
- [ ] SDO server on COB-ID 0x580+NID (Tx) and 0x600+NID (Rx)
- [ ] Expedited download (write) supported for ≤ 4-byte objects
- [ ] Expedited upload (read) supported for ≤ 4-byte objects
- [ ] SDO abort response implemented for all error cases
- [ ] Segmented transfer supported for objects > 4 bytes (if such objects exist)

#### NMT
- [ ] Initialisation state entered at power-on
- [ ] Boot-up frame transmitted (COB-ID 0x700+NID, data=0x00)
- [ ] Pre-Operational state entered automatically after boot-up
- [ ] NMT Start Remote Node (0x01) transitions to Operational
- [ ] NMT Stop Remote Node (0x02) transitions to Stopped
- [ ] NMT Enter Pre-Operational (0x80) transitions to Pre-Operational
- [ ] NMT Reset Node (0x81) performs full reset
- [ ] NMT Reset Communication (0x82) resets communication parameters
- [ ] NMT commands addressed to Node-ID=0 (broadcast) are processed
- [ ] PDOs active only in Operational state
- [ ] SDOs accessible in Pre-Operational and Operational states

#### Error Handling
- [ ] Error Register (0x1001) updated before sending EMCY
- [ ] EMCY frame transmitted on error occurrence (COB-ID 0x080+NID)
- [ ] EMCY "error cleared" frame (error code 0x0000) sent on error resolution

### Optional but Strongly Recommended

- [ ] OD[0x1008] Manufacturer Device Name (ASCII string)
- [ ] OD[0x1009] Manufacturer Hardware Version (ASCII string)
- [ ] OD[0x100A] Manufacturer Software Version (ASCII string)
- [ ] OD[0x1003] Pre-defined Error Field (FIFO of last EMCY codes)
- [ ] OD[0x1014] COB-ID EMCY (allows reconfiguration)
- [ ] OD[0x1017] Producer Heartbeat Time (heartbeat monitoring)
- [ ] OD[0x1016] Consumer Heartbeat Time (peer monitoring)
- [ ] OD[0x1029] Error Behaviour Object (configurable error response)
- [ ] OD[0x1010] Store Parameters (non-volatile storage)
- [ ] OD[0x1011] Restore Default Parameters
- [ ] EDS file provided and valid
- [ ] EMCY inhibit time (0x1015) implemented

### PDO Conformance (if PDO supported)

- [ ] At least one TPDO or RPDO implemented
- [ ] PDO communication parameters at 0x1400–0x141F / 0x1800–0x181F
- [ ] PDO mapping parameters at 0x1600–0x161F / 0x1A00–0x1A1F
- [ ] PDO mapping only to mappable objects (PDOMapping=1 in EDS)
- [ ] PDO disabled during mapping reconfiguration
- [ ] Total mapped bits ≤ 64 bits (8 bytes) per PDO
- [ ] Transmission type 254 and/or 255 (async event) supported

### Node Guarding / Heartbeat Conformance

- [ ] Only one of Node Guarding or Heartbeat active at a time
- [ ] Boot-up message sent on entering Pre-Operational
- [ ] If heartbeat: 0x1017 producer implemented
- [ ] If heartbeat: 0x1016 consumer(s) implemented
- [ ] Heartbeat event triggers application callback / error handling

---

## C/C++ Programming Examples

### 1. Object Dictionary Structure Definition

```c
/* canopen_od.h — Minimal Object Dictionary framework */

#include <stdint.h>
#include <stddef.h>

/* CiA 301 Object types */
#define OD_OBJECT_VAR       0x07
#define OD_OBJECT_ARRAY     0x08
#define OD_OBJECT_RECORD    0x09

/* CiA 301 Data types */
#define OD_TYPE_BOOLEAN     0x0001
#define OD_TYPE_INT8        0x0002
#define OD_TYPE_INT16       0x0003
#define OD_TYPE_INT32       0x0004
#define OD_TYPE_UINT8       0x0005
#define OD_TYPE_UINT16      0x0006
#define OD_TYPE_UINT32      0x0007
#define OD_TYPE_REAL32      0x0008
#define OD_TYPE_VISIBLE_STR 0x0009
#define OD_TYPE_INT64       0x0015
#define OD_TYPE_UINT64      0x001B

/* Access types */
#define OD_ACCESS_RO    0x01
#define OD_ACCESS_WO    0x02
#define OD_ACCESS_RW    0x03
#define OD_ACCESS_CONST 0x04

/* PDO mapping flag */
#define OD_MAPPABLE     0x01
#define OD_NOT_MAPPABLE 0x00

typedef struct {
    uint8_t  sub_index;
    uint8_t  data_type;
    uint8_t  access_type;
    uint8_t  pdo_mappable;
    void    *data_ptr;
    uint32_t data_size;   /* bytes */
} OD_Entry_t;

typedef struct {
    uint16_t      index;
    uint8_t       object_type;
    uint8_t       num_subs;
    OD_Entry_t   *subs;
} OD_Object_t;
```

### 2. Device Identity Object (0x1018) Implementation

```c
/* identity.c — Device Identity Object 0x1018 */

#include "canopen_od.h"

/* Identity data — typically stored in flash */
static const uint8_t  identity_highest_sub = 0x04;
static const uint32_t identity_vendor_id   = 0x00000123UL; /* CiA-assigned */
static const uint32_t identity_product_code= 0x00010001UL;
static const uint32_t identity_revision    = 0x00020003UL; /* Major 2, Minor 3 */
static const uint32_t identity_serial      = 0x00000042UL;

static OD_Entry_t identity_subs[] = {
    /* Sub 0: Highest sub-index */
    { 0x00, OD_TYPE_UINT8,  OD_ACCESS_RO, OD_NOT_MAPPABLE,
      (void*)&identity_highest_sub, sizeof(identity_highest_sub) },
    /* Sub 1: Vendor ID */
    { 0x01, OD_TYPE_UINT32, OD_ACCESS_RO, OD_NOT_MAPPABLE,
      (void*)&identity_vendor_id,   sizeof(identity_vendor_id) },
    /* Sub 2: Product Code */
    { 0x02, OD_TYPE_UINT32, OD_ACCESS_RO, OD_NOT_MAPPABLE,
      (void*)&identity_product_code, sizeof(identity_product_code) },
    /* Sub 3: Revision Number */
    { 0x03, OD_TYPE_UINT32, OD_ACCESS_RO, OD_NOT_MAPPABLE,
      (void*)&identity_revision,    sizeof(identity_revision) },
    /* Sub 4: Serial Number */
    { 0x04, OD_TYPE_UINT32, OD_ACCESS_RO, OD_NOT_MAPPABLE,
      (void*)&identity_serial,      sizeof(identity_serial) },
};

OD_Object_t od_identity = {
    .index       = 0x1018,
    .object_type = OD_OBJECT_RECORD,
    .num_subs    = 5,
    .subs        = identity_subs,
};

/* Helper: extract major/minor revision */
uint16_t identity_get_major_revision(void) {
    return (uint16_t)(identity_revision >> 16);
}

uint16_t identity_get_minor_revision(void) {
    return (uint16_t)(identity_revision & 0xFFFF);
}
```

### 3. NMT State Machine

```c
/* nmt.h / nmt.c — NMT state machine */

typedef enum {
    NMT_STATE_INITIALISATION  = 0x00,
    NMT_STATE_STOPPED         = 0x04,
    NMT_STATE_OPERATIONAL     = 0x05,
    NMT_STATE_PRE_OPERATIONAL = 0x7F,
} NMT_State_t;

typedef enum {
    NMT_CMD_START_REMOTE_NODE     = 0x01,
    NMT_CMD_STOP_REMOTE_NODE      = 0x02,
    NMT_CMD_ENTER_PREOPERATIONAL  = 0x80,
    NMT_CMD_RESET_NODE            = 0x81,
    NMT_CMD_RESET_COMMUNICATION   = 0x82,
} NMT_Command_t;

typedef struct {
    NMT_State_t  state;
    uint8_t      node_id;
    void (*on_state_change)(NMT_State_t old_state, NMT_State_t new_state);
} NMT_t;

/* --- nmt.c --- */
#include "nmt.h"
#include "can_driver.h"   /* platform CAN driver */

static NMT_t g_nmt;

static void nmt_send_bootup(uint8_t node_id) {
    CAN_Frame_t frame = {
        .cob_id = 0x700U + node_id,
        .dlc    = 1,
        .data   = { 0x00 }
    };
    can_send(&frame);
}

void nmt_init(uint8_t node_id, void (*cb)(NMT_State_t, NMT_State_t)) {
    g_nmt.node_id        = node_id;
    g_nmt.state          = NMT_STATE_INITIALISATION;
    g_nmt.on_state_change = cb;
}

void nmt_enter_preoperational(void) {
    NMT_State_t old = g_nmt.state;
    g_nmt.state = NMT_STATE_PRE_OPERATIONAL;
    nmt_send_bootup(g_nmt.node_id);
    if (g_nmt.on_state_change) {
        g_nmt.on_state_change(old, g_nmt.state);
    }
}

void nmt_process_command(uint8_t cmd, uint8_t target_node_id) {
    /* Accept broadcast (0) or own node-id */
    if (target_node_id != 0 && target_node_id != g_nmt.node_id) {
        return;
    }

    NMT_State_t old = g_nmt.state;
    NMT_State_t new_state = old;

    switch ((NMT_Command_t)cmd) {
        case NMT_CMD_START_REMOTE_NODE:
            if (old == NMT_STATE_PRE_OPERATIONAL || old == NMT_STATE_STOPPED)
                new_state = NMT_STATE_OPERATIONAL;
            break;

        case NMT_CMD_STOP_REMOTE_NODE:
            if (old == NMT_STATE_PRE_OPERATIONAL || old == NMT_STATE_OPERATIONAL)
                new_state = NMT_STATE_STOPPED;
            break;

        case NMT_CMD_ENTER_PREOPERATIONAL:
            if (old == NMT_STATE_OPERATIONAL || old == NMT_STATE_STOPPED)
                new_state = NMT_STATE_PRE_OPERATIONAL;
            break;

        case NMT_CMD_RESET_NODE:
        case NMT_CMD_RESET_COMMUNICATION:
            /* Trigger reset — application-specific */
            new_state = NMT_STATE_INITIALISATION;
            break;

        default:
            return;
    }

    if (new_state != old) {
        g_nmt.state = new_state;
        if (g_nmt.on_state_change) {
            g_nmt.on_state_change(old, new_state);
        }
    }
}

NMT_State_t nmt_get_state(void) {
    return g_nmt.state;
}
```

### 4. SDO Server (Expedited Transfer)

```c
/* sdo_server.c — Expedited SDO server */

#include <string.h>
#include "canopen_od.h"
#include "can_driver.h"

#define SDO_CS_DOWNLOAD_4B  0x23
#define SDO_CS_DOWNLOAD_3B  0x27
#define SDO_CS_DOWNLOAD_2B  0x2B
#define SDO_CS_DOWNLOAD_1B  0x2F
#define SDO_CS_UPLOAD_REQ   0x40
#define SDO_CS_UPLOAD_4B    0x43
#define SDO_CS_UPLOAD_3B    0x47
#define SDO_CS_UPLOAD_2B    0x4B
#define SDO_CS_UPLOAD_1B    0x4F
#define SDO_CS_DOWNLOAD_RSP 0x60
#define SDO_CS_ABORT        0x80

/* SDO Abort codes */
#define SDO_ABORT_OBJECT_NOT_EXIST  0x06020000UL
#define SDO_ABORT_SUB_NOT_EXIST     0x06090011UL
#define SDO_ABORT_READ_ONLY         0x06010002UL
#define SDO_ABORT_WRITE_ONLY        0x06010001UL
#define SDO_ABORT_GENERAL_ERROR     0x08000000UL

static void sdo_send_abort(uint8_t node_id, uint16_t index,
                           uint8_t sub, uint32_t abort_code) {
    CAN_Frame_t frame;
    frame.cob_id   = 0x580U + node_id;
    frame.dlc      = 8;
    frame.data[0]  = SDO_CS_ABORT;
    frame.data[1]  = (uint8_t)(index & 0xFF);
    frame.data[2]  = (uint8_t)(index >> 8);
    frame.data[3]  = sub;
    frame.data[4]  = (uint8_t)(abort_code);
    frame.data[5]  = (uint8_t)(abort_code >> 8);
    frame.data[6]  = (uint8_t)(abort_code >> 16);
    frame.data[7]  = (uint8_t)(abort_code >> 24);
    can_send(&frame);
}

static OD_Entry_t* od_find(uint16_t index, uint8_t sub) {
    /* Implementation: search global OD table */
    extern OD_Object_t od_table[];
    extern size_t      od_table_size;

    for (size_t i = 0; i < od_table_size; i++) {
        if (od_table[i].index == index) {
            for (uint8_t s = 0; s < od_table[i].num_subs; s++) {
                if (od_table[i].subs[s].sub_index == sub)
                    return &od_table[i].subs[s];
            }
            return NULL; /* object exists, sub does not */
        }
    }
    return NULL;
}

void sdo_server_process(uint8_t node_id, const CAN_Frame_t *frame) {
    if (frame->cob_id != (0x600U + node_id)) return;
    if (frame->dlc < 8) return;

    uint8_t  cs    = frame->data[0];
    uint16_t index = (uint16_t)(frame->data[1]) | ((uint16_t)(frame->data[2]) << 8);
    uint8_t  sub   = frame->data[3];

    OD_Entry_t *entry = od_find(index, sub);

    /* --- UPLOAD (read) --- */
    if (cs == SDO_CS_UPLOAD_REQ) {
        if (!entry) {
            sdo_send_abort(node_id, index, sub, SDO_ABORT_OBJECT_NOT_EXIST);
            return;
        }
        if (entry->access_type == OD_ACCESS_WO) {
            sdo_send_abort(node_id, index, sub, SDO_ABORT_WRITE_ONLY);
            return;
        }

        CAN_Frame_t rsp;
        rsp.cob_id = 0x580U + node_id;
        rsp.dlc    = 8;
        memset(rsp.data, 0, 8);
        rsp.data[1] = (uint8_t)(index);
        rsp.data[2] = (uint8_t)(index >> 8);
        rsp.data[3] = sub;

        switch (entry->data_size) {
            case 1: rsp.data[0] = SDO_CS_UPLOAD_1B; break;
            case 2: rsp.data[0] = SDO_CS_UPLOAD_2B; break;
            case 3: rsp.data[0] = SDO_CS_UPLOAD_3B; break;
            default:
            case 4: rsp.data[0] = SDO_CS_UPLOAD_4B; break;
        }
        memcpy(&rsp.data[4], entry->data_ptr,
               entry->data_size < 4 ? entry->data_size : 4);
        can_send(&rsp);
        return;
    }

    /* --- DOWNLOAD (write) --- */
    if ((cs & 0xF0) == 0x20) {  /* Download request family */
        if (!entry) {
            sdo_send_abort(node_id, index, sub, SDO_ABORT_OBJECT_NOT_EXIST);
            return;
        }
        if (entry->access_type == OD_ACCESS_RO ||
            entry->access_type == OD_ACCESS_CONST) {
            sdo_send_abort(node_id, index, sub, SDO_ABORT_READ_ONLY);
            return;
        }

        uint8_t bytes = (uint8_t)(4 - ((cs >> 2) & 0x03));
        if (bytes == 0) bytes = 4;

        memcpy(entry->data_ptr, &frame->data[4],
               bytes < entry->data_size ? bytes : entry->data_size);

        CAN_Frame_t rsp;
        rsp.cob_id = 0x580U + node_id;
        rsp.dlc    = 8;
        memset(rsp.data, 0, 8);
        rsp.data[0] = SDO_CS_DOWNLOAD_RSP;
        rsp.data[1] = (uint8_t)(index);
        rsp.data[2] = (uint8_t)(index >> 8);
        rsp.data[3] = sub;
        can_send(&rsp);
        return;
    }

    /* Unknown CS */
    sdo_send_abort(node_id, index, sub, SDO_ABORT_GENERAL_ERROR);
}
```

### 5. Heartbeat Producer and Consumer

```c
/* heartbeat.c — Producer and Consumer */

#include <stdint.h>
#include <stdbool.h>
#include "can_driver.h"
#include "nmt.h"
#include "timer.h"   /* platform timer */

/* ---- Producer ---- */
static uint32_t hb_producer_time_ms = 0;  /* OD[0x1017] */
static uint32_t hb_producer_timer   = 0;

void heartbeat_producer_set_time(uint32_t time_ms) {
    hb_producer_time_ms = time_ms;
    hb_producer_timer   = 0;
}

void heartbeat_producer_tick(uint8_t node_id, uint32_t elapsed_ms) {
    if (hb_producer_time_ms == 0) return;

    hb_producer_timer += elapsed_ms;
    if (hb_producer_timer >= hb_producer_time_ms) {
        hb_producer_timer -= hb_producer_time_ms;

        CAN_Frame_t frame;
        frame.cob_id  = 0x700U + node_id;
        frame.dlc     = 1;
        frame.data[0] = (uint8_t)nmt_get_state();
        can_send(&frame);
    }
}

/* ---- Consumer ---- */
#define MAX_HB_CONSUMERS 8

typedef struct {
    uint8_t  node_id;        /* Node to monitor */
    uint32_t timeout_ms;     /* OD[0x1016][N] timeout */
    uint32_t elapsed_ms;     /* Time since last heartbeat */
    bool     active;
    bool     error;          /* Timeout detected */
    void (*on_event)(uint8_t node_id, bool timeout);
} HB_Consumer_t;

static HB_Consumer_t hb_consumers[MAX_HB_CONSUMERS];
static uint8_t       hb_consumer_count = 0;

void heartbeat_consumer_add(uint8_t node_id, uint32_t timeout_ms,
                             void (*on_event)(uint8_t, bool)) {
    if (hb_consumer_count >= MAX_HB_CONSUMERS) return;

    HB_Consumer_t *c = &hb_consumers[hb_consumer_count++];
    c->node_id    = node_id;
    c->timeout_ms = timeout_ms;
    c->elapsed_ms = 0;
    c->active     = true;
    c->error      = false;
    c->on_event   = on_event;
}

/* Call when a heartbeat frame is received */
void heartbeat_consumer_receive(uint8_t sender_node_id) {
    for (uint8_t i = 0; i < hb_consumer_count; i++) {
        HB_Consumer_t *c = &hb_consumers[i];
        if (c->active && c->node_id == sender_node_id) {
            bool was_error = c->error;
            c->elapsed_ms = 0;
            c->error      = false;
            /* Notify recovery */
            if (was_error && c->on_event) {
                c->on_event(sender_node_id, false);
            }
            break;
        }
    }
}

/* Call periodically with elapsed time */
void heartbeat_consumer_tick(uint32_t elapsed_ms) {
    for (uint8_t i = 0; i < hb_consumer_count; i++) {
        HB_Consumer_t *c = &hb_consumers[i];
        if (!c->active || c->error) continue;

        c->elapsed_ms += elapsed_ms;
        if (c->elapsed_ms >= c->timeout_ms) {
            c->error = true;
            if (c->on_event) {
                c->on_event(c->node_id, true);  /* timeout = true */
            }
        }
    }
}

/* Example usage */
static void on_heartbeat_event(uint8_t node_id, bool timeout) {
    if (timeout) {
        /* Node lost — trigger application safety response */
        /* Check OD[0x1029] for configured behaviour */
        printf("Node %d heartbeat lost!\n", node_id);
        /* ... apply error behaviour ... */
    } else {
        printf("Node %d recovered\n", node_id);
    }
}

void example_setup_heartbeat_monitoring(void) {
    /* Monitor Node 5: producer sends every 1000 ms,
       we timeout after 1500 ms (1.5× factor) */
    heartbeat_consumer_add(5, 1500, on_heartbeat_event);
    /* Monitor Node 10: producer sends every 500 ms */
    heartbeat_consumer_add(10, 750, on_heartbeat_event);
}
```

### 6. Error Behaviour Object (0x1029) Implementation

```c
/* error_behaviour.c — OD[0x1029] */

#include "canopen_od.h"
#include "nmt.h"
#include "emcy.h"

/* Error behaviour values */
#define EB_ENTER_PREOP    0x00
#define EB_NO_CHANGE      0x01
#define EB_ENTER_STOPPED  0x02

static uint8_t error_behaviour_highest = 0x01;
static uint8_t error_behaviour_comm    = EB_ENTER_PREOP;  /* default */

static OD_Entry_t error_behaviour_subs[] = {
    { 0x00, OD_TYPE_UINT8, OD_ACCESS_RO, OD_NOT_MAPPABLE,
      &error_behaviour_highest, sizeof(error_behaviour_highest) },
    { 0x01, OD_TYPE_UINT8, OD_ACCESS_RW, OD_NOT_MAPPABLE,
      &error_behaviour_comm,    sizeof(error_behaviour_comm) },
};

OD_Object_t od_error_behaviour = {
    .index       = 0x1029,
    .object_type = OD_OBJECT_ARRAY,
    .num_subs    = 2,
    .subs        = error_behaviour_subs,
};

/* Apply error behaviour — call on communication error detection */
void error_behaviour_apply_comm_error(void) {
    switch (error_behaviour_comm) {
        case EB_ENTER_PREOP:
            /* Transition to Pre-Operational */
            nmt_process_command(NMT_CMD_ENTER_PREOPERATIONAL, 0);
            break;

        case EB_NO_CHANGE:
            /* Stay in current state — log only */
            break;

        case EB_ENTER_STOPPED:
            nmt_process_command(NMT_CMD_STOP_REMOTE_NODE, 0);
            break;

        default:
            /* Unknown — default to Pre-Operational */
            nmt_process_command(NMT_CMD_ENTER_PREOPERATIONAL, 0);
            break;
    }
}
```

### 7. EMCY Transmit

```c
/* emcy.c — Emergency Object transmit */

#include <string.h>
#include "can_driver.h"
#include "nmt.h"

#define EMCY_ERROR_RESET    0x0000
#define EMCY_GENERIC_ERR    0x1000
#define EMCY_CURRENT_ERR    0x2000
#define EMCY_VOLTAGE_ERR    0x3000
#define EMCY_TEMP_ERR       0x4000
#define EMCY_HW_ERR         0x5000
#define EMCY_SW_ERR         0x6000
#define EMCY_COMM_ERR       0x8100  /* CAN overrun */
#define EMCY_HEARTBEAT_ERR  0x8130

static uint8_t error_register = 0x00;  /* OD[0x1001] */

void emcy_send(uint8_t node_id, uint16_t error_code,
               uint8_t err_reg,
               const uint8_t mfr_data[5]) {
    CAN_Frame_t frame;
    frame.cob_id  = 0x080U + node_id;
    frame.dlc     = 8;
    frame.data[0] = (uint8_t)(error_code & 0xFF);
    frame.data[1] = (uint8_t)(error_code >> 8);
    frame.data[2] = err_reg;

    if (mfr_data) {
        memcpy(&frame.data[3], mfr_data, 5);
    } else {
        memset(&frame.data[3], 0, 5);
    }

    can_send(&frame);
}

/* Report a communication error */
void emcy_report_comm_error(uint8_t node_id) {
    error_register |= (1 << 4);  /* Bit 4: communication error */
    error_register |= (1 << 0);  /* Bit 0: generic error */
    emcy_send(node_id, EMCY_COMM_ERR, error_register, NULL);
}

/* Clear a communication error */
void emcy_clear_comm_error(uint8_t node_id) {
    error_register &= ~(1 << 4);
    if (error_register == 0) {
        /* All errors cleared — send error reset */
        emcy_send(node_id, EMCY_ERROR_RESET, 0x00, NULL);
    }
}

uint8_t emcy_get_error_register(void) {
    return error_register;
}
```

### 8. PDO Configuration and Transmission

```c
/* pdo.c — TPDO transmit with mapping */

#include <string.h>
#include "can_driver.h"
#include "nmt.h"

#define TPDO1_DEFAULT_COBID  0x180  /* + Node-ID */
#define PDO_TRANS_ASYNC_255  0xFF

typedef struct {
    uint32_t mapping[8];   /* OD mapping entries */
    uint8_t  num_mapped;
    uint32_t cob_id;
    uint8_t  trans_type;
    uint16_t inhibit_time; /* 100us units */
    uint16_t event_timer;  /* ms */
} TPDO_t;

/* Example: map Status Word (0x6041, 16-bit) + Actual Position (0x6064, 32-bit) */
static TPDO_t tpdo1 = {
    .mapping    = {
        0x60410010,  /* OD[0x6041][0x00], 16 bits */
        0x60640020,  /* OD[0x6064][0x00], 32 bits */
    },
    .num_mapped = 2,
    .cob_id     = 0x000,   /* 0 = use default: 0x180 + Node-ID */
    .trans_type = PDO_TRANS_ASYNC_255,
    .event_timer = 10,     /* 10 ms periodic */
};

/* Application data (normally from OD) */
static uint16_t status_word   = 0x0237;
static int32_t  actual_position = 12345;

void tpdo1_transmit(uint8_t node_id) {
    if (nmt_get_state() != NMT_STATE_OPERATIONAL) return;

    CAN_Frame_t frame;
    frame.cob_id = (tpdo1.cob_id ? tpdo1.cob_id : (TPDO1_DEFAULT_COBID + node_id));
    frame.dlc    = 6;  /* 2 bytes + 4 bytes */

    /* Pack data little-endian */
    memcpy(&frame.data[0], &status_word,      2);
    memcpy(&frame.data[2], &actual_position,  4);

    can_send(&frame);
}
```

### 9. Master: SDO Write to Configure Heartbeat

```c
/* master_config.c — Configure node heartbeat via SDO */

#include "sdo_client.h"

/* Write a 32-bit value to a node's OD via SDO */
int sdo_write_u32(uint8_t node_id, uint16_t index, uint8_t sub, uint32_t value) {
    uint8_t req[8];
    req[0] = 0x23;                          /* Download 4 bytes */
    req[1] = (uint8_t)(index);
    req[2] = (uint8_t)(index >> 8);
    req[3] = sub;
    req[4] = (uint8_t)(value);
    req[5] = (uint8_t)(value >> 8);
    req[6] = (uint8_t)(value >> 16);
    req[7] = (uint8_t)(value >> 24);

    return sdo_client_transfer(node_id, req, NULL, 5000 /* timeout ms */);
}

void configure_node_heartbeat(uint8_t node_id,
                               uint16_t producer_ms,
                               uint16_t consumer_ms) {
    /* 1. Set the node's heartbeat producer time */
    if (sdo_write_u32(node_id, 0x1017, 0x00, producer_ms) != 0) {
        printf("Failed to set heartbeat producer on node %d\n", node_id);
        return;
    }

    /* 2. Configure master's consumer entry for this node
     *    Consumer entry: bits 31..16 = node_id, bits 15..0 = timeout_ms */
    uint32_t consumer_entry = ((uint32_t)node_id << 16) | consumer_ms;

    /* Write to master's own OD (sub-index 1 for first consumer) */
    /* In practice, done via local OD write, not SDO */
    (void)consumer_entry;  /* ... configure locally ... */

    printf("Node %d: HB producer=%ums, consumer timeout=%ums\n",
           node_id, producer_ms, consumer_ms);
}

/* Example: configure all nodes at startup */
void master_configure_all_nodes(void) {
    /* Node 1: 1000ms heartbeat, 1500ms consumer timeout */
    configure_node_heartbeat(1, 1000, 1500);

    /* Node 2: 500ms heartbeat, 750ms consumer timeout */
    configure_node_heartbeat(2, 500, 750);

    /* Ensure node guarding is disabled on all nodes */
    sdo_write_u32(1, 0x100C, 0x00, 0);  /* Guard Time = 0 */
    sdo_write_u32(1, 0x100D, 0x00, 0);  /* Life Time Factor = 0 */
    sdo_write_u32(2, 0x100C, 0x00, 0);
    sdo_write_u32(2, 0x100D, 0x00, 0);
}
```

### 10. Full Node Initialisation Sequence

```c
/* node_init.c — Complete CiA 301 node startup */

#include "canopen_od.h"
#include "nmt.h"
#include "sdo_server.h"
#include "heartbeat.h"
#include "emcy.h"
#include "pdo.h"

#define MY_NODE_ID  5

static void on_nmt_state_change(NMT_State_t old_state, NMT_State_t new_state) {
    if (new_state == NMT_STATE_OPERATIONAL) {
        /* Enable PDO processing */
        pdo_enable();
    } else {
        pdo_disable();
    }
}

void canopen_node_init(void) {
    /* 1. Initialise CAN driver */
    can_init(500000);  /* 500 kbit/s */

    /* 2. Initialise NMT state machine */
    nmt_init(MY_NODE_ID, on_nmt_state_change);

    /* 3. Configure heartbeat producer: 1000 ms */
    heartbeat_producer_set_time(1000);

    /* 4. Configure heartbeat consumers (if monitoring other nodes) */
    example_setup_heartbeat_monitoring();

    /* 5. Initialise SDO server */
    sdo_server_init(MY_NODE_ID);

    /* 6. Initialise PDOs */
    pdo_init(MY_NODE_ID);

    /* 7. Transition to Pre-Operational (sends boot-up frame) */
    nmt_enter_preoperational();

    /* Node now waits for:
     *   a) NMT Start Remote Node command from master, or
     *   b) SDO configuration by master, then Start command
     */
}

void canopen_node_process(void) {
    /* Called from main loop or timer interrupt */
    CAN_Frame_t frame;

    while (can_receive(&frame) == CAN_OK) {
        uint16_t cob_id = frame.cob_id;

        if (cob_id == 0x000) {
            /* NMT command */
            nmt_process_command(frame.data[0], frame.data[1]);
        }
        else if (cob_id == 0x600 + MY_NODE_ID) {
            /* SDO request */
            sdo_server_process(MY_NODE_ID, &frame);
        }
        else if ((cob_id & 0xFF0) == 0x700) {
            /* Heartbeat from another node */
            uint8_t sender = (uint8_t)(cob_id & 0x7F);
            if (frame.data[0] != 0x00) {  /* not a boot-up */
                heartbeat_consumer_receive(sender);
            }
        }
        else if (cob_id == 0x200 + MY_NODE_ID ||
                 cob_id == 0x300 + MY_NODE_ID) {
            /* RPDO */
            pdo_receive(&frame);
        }
    }

    /* Periodic tasks */
    uint32_t elapsed = timer_get_elapsed_ms();
    heartbeat_producer_tick(MY_NODE_ID, elapsed);
    heartbeat_consumer_tick(elapsed);
    pdo_event_tick(elapsed);
}
```

---

## Summary

CiA 301 is the foundational standard of the CANopen ecosystem. It defines a complete, layered communication architecture on top of the CAN bus that enables interoperability between devices from different manufacturers.

### Key Takeaways

**Object Dictionary** is the universal interface: every piece of data accessible over the network lives at a defined `Index:Sub-Index` location, typed and access-controlled. Mastering the OD structure — especially the communication profile area `0x1000–0x1FFF` — is the prerequisite for everything else in CANopen.

**The Identity Object (0x1018)** is the fingerprint of every CANopen device. It is the only mandatory record object in CiA 301 and enables automated network commissioning and firmware compatibility checks through its Vendor ID, Product Code, and split Major/Minor Revision Number.

**PDOs** deliver real-time process data with minimal overhead — up to 8 bytes per CAN frame with no protocol header. Their flexible mapping mechanism allows any OD variable to be bundled into a PDO and transmitted synchronously (driven by SYNC) or asynchronously (event-driven).

**SDOs** are the reliable configuration channel. The expedited transfer covers the vast majority of single-variable reads/writes in 2 CAN frames; segmented and block transfers handle larger payloads such as EDS files and firmware images. SDO abort codes provide precise diagnostics.

**NMT** imposes a well-defined lifecycle — Initialisation → Pre-Operational → Operational — that prevents a misconfigured device from disturbing the network. The mandatory boot-up message guarantees the master knows when each node is ready.

**Heartbeat** (preferred) and **Node Guarding** (legacy) monitor liveness. The critical rule is that they must not be active simultaneously. Heartbeat's push model is more robust, requires no RTR support, and enables peer-to-peer monitoring beyond the master.

**Error Behaviour (0x1029)** and the **Error Register (0x1001)** together give system integrators control over how a node responds to faults — from safe state (Pre-Operational) to graceful degradation (no state change) to full shutdown (Stopped) — while the **EMCY object** broadcasts the error details immediately to all interested parties.

**Conformance** rests on a small mandatory core — Device Type, Error Register, Identity Object, SDO server, and NMT state machine — augmented by optional features that are declared in the EDS. The EDS/DCF mechanism is the essential companion to CiA 301 for automated network configuration and tool support.

```
+-----------------------------------------------------------+
|                CiA 301 Essentials at a Glance             |
+---------------+-------------------------------------------+
| MANDATORY     | 0x1000 Device Type                        |
|               | 0x1001 Error Register                     |
|               | 0x1018 Identity Object                    |
|               | SDO Server (0x580/0x600)                  |
|               | NMT State Machine + Boot-Up               |
+---------------+-------------------------------------------+
| RECOMMENDED   | 0x1017/0x1016 Heartbeat                   |
|               | 0x1014/0x1001 EMCY                        |
|               | 0x1029 Error Behaviour                    |
|               | 0x1003 Error History                      |
|               | EDS file                                  |
+---------------+-------------------------------------------+
| REAL-TIME     | PDO (0x1400–0x1A1F)                       |
| PROCESS DATA  | SYNC (0x1005/0x1006)                      |
+---------------+-------------------------------------------+
| CONFIGURATION | SDO (expedited / segmented / block)       |
|               | 0x1010 Store / 0x1011 Restore             |
+---------------+-------------------------------------------+
```

Understanding CiA 301 thoroughly makes every higher-level CANopen standard (CiA 402 for drives, CiA 401 for I/O, CiA 404 for measurement) immediately accessible, as they all build directly on this communication foundation.

---

*Document generated for CiA 301 v4.2.0 | Examples target embedded C99/C++11 on bare-metal or RTOS platforms*# CiA 301 — Application Layer & Communication Profile

