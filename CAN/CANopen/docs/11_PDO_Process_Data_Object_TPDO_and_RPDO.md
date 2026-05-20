# 11. PDO — Process Data Object (TPDO & RPDO)

## Table of Contents

1. [Introduction](#1-introduction)
2. [Producer/Consumer Model](#2-producerconsumer-model)
3. [PDO Types: TPDO and RPDO](#3-pdo-types-tpdo-and-rpdo)
4. [Object Dictionary Entries](#4-object-dictionary-entries)
   - 4.1 [PDO Communication Parameters](#41-pdo-communication-parameters)
   - 4.2 [PDO Mapping Parameters](#42-pdo-mapping-parameters)
5. [COB-ID and Node Addressing](#5-cob-id-and-node-addressing)
6. [Transmission Types](#6-transmission-types)
   - 6.1 [Type 0 — Acyclic Synchronous](#61-type-0--acyclic-synchronous)
   - 6.2 [Types 1–240 — Cyclic Synchronous](#62-types-1240--cyclic-synchronous)
   - 6.3 [Type 254 — Event-Driven (Manufacturer-Specific)](#63-type-254--event-driven-manufacturer-specific)
   - 6.4 [Type 255 — Event-Driven (Profile/Application-Specific)](#64-type-255--event-drivenprofileapplication-specific)
7. [Inhibit Time](#7-inhibit-time)
8. [Event Timer](#8-event-timer)
9. [RPDO Timeout Monitoring](#9-rpdo-timeout-monitoring)
10. [PDO Mapping in Detail](#10-pdo-mapping-in-detail)
11. [Dynamic PDO Configuration](#11-dynamic-pdo-configuration)
12. [Programming Examples in C/C++](#12-programming-examples-in-cc)
    - 12.1 [Object Dictionary Structures](#121-object-dictionary-structures)
    - 12.2 [Configuring a TPDO via SDO](#122-configuring-a-tpdo-via-sdo)
    - 12.3 [Configuring an RPDO via SDO](#123-configuring-an-rpdo-via-sdo)
    - 12.4 [Transmitting a TPDO](#124-transmitting-a-tpdo)
    - 12.5 [Receiving and Processing an RPDO](#125-receiving-and-processing-an-rpdo)
    - 12.6 [Inhibit Time and Event Timer Handling](#126-inhibit-time-and-event-timer-handling)
    - 12.7 [RPDO Timeout Watchdog](#127-rpdo-timeout-watchdog)
    - 12.8 [Full Node Example (CANopen Slave)](#128-full-node-example-canopen-slave)
13. [Summary](#13-summary)

---

## 1. Introduction

Process Data Objects (PDOs) are the primary mechanism for real-time data exchange in a CANopen
network. Unlike Service Data Objects (SDOs), which use a confirmed, handshake-based protocol
suitable for configuration and parameter access, PDOs are unconfirmed single-frame CAN messages
designed for high-throughput, low-latency transmission of process variables such as sensor
readings, actuator setpoints, digital I/O states, and control signals.

A PDO carries up to **8 bytes of application data** in a standard CAN frame (no protocol overhead
beyond the CAN header itself). This makes PDOs extremely efficient — one CAN frame delivers
up to eight process variables packed consecutively.

Key characteristics of PDOs:

- No protocol overhead: pure application data in the CAN data field
- Up to 8 bytes per PDO (64 bits)
- No acknowledgement from the receiver (unconfirmed service)
- Flexible transmission triggering: synchronous, event-driven, or timer-based
- Content defined by **PDO Mapping** in the Object Dictionary
- Behaviour defined by **PDO Communication Parameters** in the Object Dictionary

---

## 2. Producer/Consumer Model

CANopen PDOs follow a **producer/consumer** communication model. One node produces (transmits)
a PDO; one or more nodes consume (receive) it. Because CAN is a broadcast bus, multiple nodes
can receive the same PDO frame simultaneously.

```
                      CAN Bus
  +-----------+  ----------------------  +-----------+
  |  Producer |---->  [ PDO Frame ]  --->| Consumer  |
  |  (TPDO)   |                          |  (RPDO)   |
  +-----------+  ----------------------  +-----------+
                              |
                              +-----------> +-----------+
                                            | Consumer  |
                                            |  (RPDO)   |
                                            +-----------+

  Producer: one sender, identified by COB-ID
  Consumer: one or many receivers, all see the same frame
```

The producer decides **when** to send (based on transmission type). Each consumer independently
decides whether to process a received frame based on its configured COB-ID filter.

A node may simultaneously be a producer for some PDOs and a consumer for others, enabling
bidirectional process data exchange.

---

## 3. PDO Types: TPDO and RPDO

From the perspective of a single node:

```
  +------------------------------------------------------+
  |                   CANopen Node                       |
  |                                                      |
  |  Application                                         |
  |      |                                               |
  |      |  write process data                           |
  |      v                                               |
  |  +---------+   Pack 8 bytes    +---------+           |
  |  |  TPDO   | ----------------> | CAN TX  |----> Bus  |
  |  | (sender)|                   | Driver  |           |
  |  +---------+                   +---------+           |
  |                                                      |
  |  +---------+   Unpack bytes    +---------+           |
  |  |  RPDO   | <---------------- | CAN RX  |<---- Bus  |
  |  |(receiver)|                  | Driver  |           |
  |  +---------+                   +---------+           |
  |      |                                               |
  |      |  deliver process data                         |
  |      v                                               |
  |  Application                                         |
  +------------------------------------------------------+

  TPDO = Transmit PDO  (this node sends)
  RPDO = Receive PDO   (this node receives)
```

Each node supports up to **512 TPDOs** and **512 RPDOs** according to the CANopen standard
(DS301), though in practice most devices implement 4 TPDOs (indices 0x1800–0x1803) and
4 RPDOs (indices 0x1400–0x1403).

---

## 4. Object Dictionary Entries

Every PDO is fully described by two Object Dictionary (OD) entries: the **Communication
Parameters** (how/when to send) and the **Mapping Parameters** (what to send).

### 4.1 PDO Communication Parameters

#### RPDO Communication Parameters: Index 0x1400 – 0x15FF

| Sub-index | Name             | Type   | Description                                      |
|-----------|------------------|--------|--------------------------------------------------|
| 0x00      | Highest sub-idx  | UINT8  | Number of entries (usually 2 or 3)               |
| 0x01      | COB-ID           | UINT32 | CAN identifier + validity/RTR flags              |
| 0x02      | Transmission Type| UINT8  | 0, 1–240, 254, 255                               |
| 0x03      | Inhibit Time     | UINT16 | Min time between two PDOs in 100 µs units        |
| 0x05      | Event Timer      | UINT16 | Watchdog timeout in 1 ms units (0 = disabled)    |
| 0x06      | SYNC Start Value | UINT8  | SYNC counter value to start reception (optional) |

#### TPDO Communication Parameters: Index 0x1800 – 0x19FF

| Sub-index | Name             | Type   | Description                                      |
|-----------|------------------|--------|--------------------------------------------------|
| 0x00      | Highest sub-idx  | UINT8  | Number of entries (usually 2, 3, or 5)           |
| 0x01      | COB-ID           | UINT32 | CAN identifier + validity/RTR flags              |
| 0x02      | Transmission Type| UINT8  | 0, 1–240, 254, 255                               |
| 0x03      | Inhibit Time     | UINT16 | Min time between two PDOs in 100 µs units        |
| 0x04      | Reserved         | UINT8  | —                                                |
| 0x05      | Event Timer      | UINT16 | Periodic send interval in 1 ms units (0=off)     |
| 0x06      | SYNC Start Value | UINT8  | SYNC counter for first transmission (optional)   |

#### COB-ID Bit Layout (Sub-index 0x01)

```
  Bit 31      Bit 30     Bits 29–11    Bits 10–0
  +--------+----------+---------------+-----------+
  | Valid  | RTR-Flag | reserved (0)  |  CAN-ID   |
  | 0=yes  | 0=RTR ok |               | (11 bits) |
  | 1=off  | 1=no RTR |               |           |
  +--------+----------+---------------+-----------+

  Bit 31 = 0 : PDO is active (valid)
  Bit 31 = 1 : PDO is inactive (not used)
  Bit 30 = 0 : RTR frame allowed to trigger TPDO
  Bit 30 = 1 : RTR frame not allowed
```

### 4.2 PDO Mapping Parameters

#### RPDO Mapping: Index 0x1600 – 0x17FF
#### TPDO Mapping: Index 0x1A00 – 0x1BFF

| Sub-index | Name              | Type   | Description                              |
|-----------|-------------------|--------|------------------------------------------|
| 0x00      | Number of entries | UINT8  | Number of mapped objects (0–8 or 0 for dummy) |
| 0x01–0x08 | Mapping entry N   | UINT32 | Object reference: Index(16)+Sub(8)+Len(8) |

Each mapping entry is a 32-bit value encoding the mapped OD object:

```
  Bits 31–16    Bits 15–8     Bits 7–0
  +------------+-------------+----------+
  |   Index    |  Sub-index  |  Length  |
  | (16 bits)  |  (8 bits)   | (8 bits) |
  |  0x0000–   |  0x00–0xFF  | in bits  |
  |  0xFFFF    |             | (e.g.16) |
  +------------+-------------+----------+

  Example: 0x60410010
    Index     = 0x6041  (Statusword)
    Sub-index = 0x00
    Length    = 0x10 = 16 bits
```

The total bit count of all mapped objects must not exceed 64 bits (8 bytes).

---

## 5. COB-ID and Node Addressing

The default COB-IDs for PDOs are derived from the **predefined connection set** (DS301):

```
  PDO          Default COB-ID Formula      Node 1    Node 5    Node 20
  ---------    --------------------------  --------  --------  --------
  TPDO1        0x180 + NodeID              0x181     0x185     0x194
  RPDO1        0x200 + NodeID              0x201     0x205     0x214
  TPDO2        0x280 + NodeID              0x281     0x285     0x294
  RPDO2        0x300 + NodeID              0x301     0x305     0x314
  TPDO3        0x380 + NodeID              0x381     0x385     0x394
  RPDO3        0x400 + NodeID              0x401     0x405     0x414
  TPDO4        0x480 + NodeID              0x481     0x485     0x494
  RPDO4        0x500 + NodeID              0x501     0x505     0x514
```

The COB-ID can be freely reassigned during configuration (while the PDO is disabled,
i.e. bit 31 of sub-index 0x01 set to 1) to any valid 11-bit CAN identifier not already
in use on the bus.

---

## 6. Transmission Types

The transmission type (TT) at sub-index 0x02 of the communication parameters controls
**when** a PDO is transmitted (TPDO) or what event triggers processing (RPDO).

```
  Transmission Type Value   Meaning
  -------------------------+--------------------------------------------------
  0                        | Acyclic synchronous (send on SYNC if data changed)
  1 – 240                  | Cyclic synchronous (send every N-th SYNC message)
  241 – 251                | Reserved
  252                      | Synchronous RTR only (RPDO: on SYNC after RTR)
  253                      | Asynchronous RTR only (send on RTR request)
  254                      | Asynchronous, manufacturer-specific event
  255                      | Asynchronous, device profile or application event
```

### 6.1 Type 0 — Acyclic Synchronous

The TPDO is transmitted on the next SYNC message, but **only** if the application data has
changed since the last transmission. This reduces bus load when process values are stable.

```
  SYNC   SYNC   SYNC   SYNC   SYNC   SYNC
    |      |      |      |      |      |
    v      v      v      v      v      v
----+------+------+------+------+------+--------> time
         [PDO]                [PDO]
         ^ data               ^ data
         changed              changed again
```

### 6.2 Types 1–240 — Cyclic Synchronous

The TPDO is transmitted every N-th SYNC message unconditionally. For N=1, the PDO is sent
on every SYNC; for N=3, every third SYNC.

```
  N = 3 (send every 3rd SYNC)

  SYNC1  SYNC2  SYNC3  SYNC4  SYNC5  SYNC6
    |      |      |      |      |      |
    v      v      v      v      v      v
----+------+------+------+------+------+--------> time
                 [PDO]                [PDO]
```

### 6.3 Type 254 — Event-Driven (Manufacturer-Specific)

The TPDO is sent when a manufacturer-defined event occurs (e.g. a digital input changes state,
an internal flag is set). The event definition is device-specific.

### 6.4 Type 255 — Event-Driven (Profile/Application-Specific)

The TPDO is sent when a device-profile-defined event occurs (e.g. a value exceeds a threshold
as defined by the device profile, such as CiA 402 for drives). This is the most common mode
for sensors and drives sending real-time process data continuously.

```
  Type 254/255 — Asynchronous transmission:

  Event  Event        Event  Event
    |      |            |      |
    v      v            v      v
----+------+------------+------+----> time
   [PDO]  [PDO]        [PDO]  [PDO]
   (may be rate-limited by inhibit time)
```

---

## 7. Inhibit Time

The **inhibit time** (sub-index 0x03 of the TPDO communication parameters) defines the
minimum interval between two consecutive transmissions of the same PDO. It prevents
bus flooding when process data changes rapidly.

- Unit: multiples of **100 µs**
- Value 0: no inhibit (send as fast as events occur)
- Value 100: minimum 10 ms between transmissions

```
  Inhibit time = 50 (= 5 ms)

  Event  Event  Event  Event  Event
    |      |      |      |      |
    v      v      v      v      v
----+--+---+--+---+------+--+---+--> time
   [P]    [P]                [P]
         ^--- blocked        ^--- blocked
         too soon            too soon

  [P] = PDO transmitted
  Only events separated by >= 5 ms produce a new frame
```

---

## 8. Event Timer

The **event timer** (sub-index 0x05) serves different purposes for TPDO and RPDO:

**TPDO Event Timer**: Causes the TPDO to be transmitted periodically even if no change event
has occurred. Acts as a heartbeat for the PDO.

- Unit: **1 ms**
- Value 0: disabled (event timer inactive)
- Useful to guarantee a maximum age of data on the consumer side

```
  Event Timer = 100 ms (TPDO, type 255)

  Event                    Timer     Timer     Timer
    |                       |         |         |
    v                       v         v         v
----+---------------------------+-------+-------+---> time
   [P]                     [P]       [P]       [P]
    ^--- triggered by        ^--- triggered by event timer
         application event        (data unchanged but refreshed)
```

**RPDO Event Timer**: Acts as a **watchdog** (see Section 9).

---

## 9. RPDO Timeout Monitoring

For RPDOs, the event timer (sub-index 0x05) defines a **reception watchdog timeout**. If the
RPDO is not received within the configured interval, the node can detect a communication
failure and react accordingly (e.g. set outputs to a safe state, generate an emergency message).

```
  RPDO Timeout Monitoring (event timer = 200 ms)

  RPDO  RPDO  RPDO     ??? No RPDO received!
    |     |     |
    v     v     v                     TIMEOUT!
----+-----+-----+--------------------+-----------> time
    |<--->|<--->|<--- 200 ms ------->|
    timer  timer  timer reset        timer fires
    reset  reset                     -> ERROR ACTION
```

Upon timeout expiry, a well-implemented node should:

1. Raise an emergency message (EMCY) with error code 0x8130 (PDO not processed)
2. Set affected outputs to a defined safe or default state
3. Optionally transition to a pre-operational or stopped state

---

## 10. PDO Mapping in Detail

PDO mapping defines the content of the PDO data field by referencing OD objects. Multiple
objects can be packed into a single PDO frame.

```
  TPDO1 Mapping (example: drive status + actual position + actual velocity)

  OD Object          Index  Sub  Bits  Mapping Entry
  -----------------  -----  ---  ----  ------------------
  Statusword         0x6041  00   16   0x60410010
  Actual Position    0x6064  00   32   0x60640020
  Actual Velocity    0x606C  00   16   0x606C0010
                                  --
                             Total 64 bits = 8 bytes (exactly one CAN frame)

  CAN Frame Data Field (8 bytes):
  +--------+--------+--------+--------+--------+--------+--------+--------+
  | SW_lo  | SW_hi  | Pos_0  | Pos_1  | Pos_2  | Pos_3  | Vel_lo | Vel_hi |
  | 0x6041 sub0     | 0x6064 sub0 (32-bit LE)  | 0x606C sub0              |
  +--------+--------+--------+--------+--------+--------+--------+--------+
  Byte 0   Byte 1   Byte 2   Byte 3   Byte 4   Byte 5   Byte 6   Byte 7

  All values in little-endian byte order per CANopen specification.
```

**Dummy mapping** (sub-index 0x00 = 0) disables all mapped objects. This is required when
dynamically reconfiguring the PDO mapping:

1. Set number of mapped objects (sub 0x00) to 0
2. Write new mapping entries to sub-indices 0x01, 0x02, ...
3. Set number of mapped objects back to the new count

---

## 11. Dynamic PDO Configuration

PDO parameters can be modified at runtime via SDO writes, following a strict sequence to
avoid inconsistent states.

**To reconfigure a TPDO:**

```
  Step 1: Disable PDO
          Write 0x8000_0000 (bit 31 = 1) to TPDO Comm Param sub 0x01

  Step 2: Disable mapping
          Write 0x00 to TPDO Map Param sub 0x00

  Step 3: Write new mapping entries
          Write mapping entry to sub 0x01, 0x02, ...

  Step 4: Enable mapping
          Write number of mapped objects to sub 0x00

  Step 5: Write new communication parameters
          (COB-ID, transmission type, inhibit time, event timer)

  Step 6: Enable PDO
          Write new COB-ID with bit 31 = 0 to sub 0x01
```

---

## 12. Programming Examples in C/C++

### 12.1 Object Dictionary Structures

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* PDO Communication Parameter record (for one PDO) */
typedef struct {
    uint8_t  highest_sub_index;    /* 0x00: always 5 for TPDO, 3 for RPDO basic */
    uint32_t cob_id;               /* 0x01: COB-ID with validity and RTR bits    */
    uint8_t  transmission_type;    /* 0x02: 0, 1-240, 254, 255                   */
    uint16_t inhibit_time;         /* 0x03: in 100 µs units                      */
    uint8_t  reserved;             /* 0x04: reserved                             */
    uint16_t event_timer;          /* 0x05: in 1 ms units                        */
    uint8_t  sync_start_value;     /* 0x06: optional SYNC counter start          */
} PDO_CommParam_t;

/* PDO Mapping Parameter record (for one PDO) */
typedef struct {
    uint8_t  num_mapped;           /* 0x00: number of valid mapping entries      */
    uint32_t mapping[8];           /* 0x01-0x08: encoded object references       */
} PDO_MapParam_t;

/* Encode a mapping entry from index, sub-index and bit-length */
static inline uint32_t pdo_mapping_entry(uint16_t index,
                                          uint8_t  sub,
                                          uint8_t  len_bits)
{
    return ((uint32_t)index << 16) | ((uint32_t)sub << 8) | (uint32_t)len_bits;
}

/* COB-ID helpers */
#define PDO_COBID_VALID(id)    ((id) & ~0x80000000UL)  /* bit 31 = 0: active */
#define PDO_COBID_INVALID(id)  ((id) |  0x80000000UL)  /* bit 31 = 1: off    */
#define PDO_COBID_NO_RTR(id)   ((id) |  0x40000000UL)  /* bit 30 = 1: no RTR */

/* Default COB-IDs for the predefined connection set */
#define TPDO1_DEFAULT_COBID(node)  (0x180U + (node))
#define RPDO1_DEFAULT_COBID(node)  (0x200U + (node))
#define TPDO2_DEFAULT_COBID(node)  (0x280U + (node))
#define RPDO2_DEFAULT_COBID(node)  (0x300U + (node))
#define TPDO3_DEFAULT_COBID(node)  (0x380U + (node))
#define RPDO3_DEFAULT_COBID(node)  (0x400U + (node))
#define TPDO4_DEFAULT_COBID(node)  (0x480U + (node))
#define RPDO4_DEFAULT_COBID(node)  (0x500U + (node))

/* Transmission type constants */
#define PDO_TRANS_ACYCLIC_SYNC      0    /* type 0  */
#define PDO_TRANS_CYCLIC_SYNC(n)    (n)  /* type 1–240 */
#define PDO_TRANS_ASYNC_MANUF       254  /* type 254 */
#define PDO_TRANS_ASYNC_EVENT       255  /* type 255 */
```

### 12.2 Configuring a TPDO via SDO

The following example shows how a CANopen master configures TPDO1 of a remote slave node
using SDO expedited write transfers. The SDO layer is abstracted via `sdo_write_u32()` etc.

```c
#include <stdint.h>
#include <stdbool.h>

/* Platform SDO write abstraction — returns 0 on success, negative on error */
extern int sdo_write_u8 (uint8_t node_id, uint16_t idx, uint8_t sub, uint8_t  val);
extern int sdo_write_u16(uint8_t node_id, uint16_t idx, uint8_t sub, uint16_t val);
extern int sdo_write_u32(uint8_t node_id, uint16_t idx, uint8_t sub, uint32_t val);

/* CiA 402 drive object indices used in the mapping example */
#define OD_STATUSWORD       0x6041U
#define OD_ACTUAL_POSITION  0x6064U
#define OD_ACTUAL_VELOCITY  0x606CU

/**
 * configure_tpdo1_drive_status()
 *
 * Configures TPDO1 of a CiA 402 servo drive (node_id) to transmit:
 *   - Statusword        (0x6041:00, 16 bit)
 *   - Actual Position   (0x6064:00, 32 bit)
 *   - Actual Velocity   (0x606C:00, 16 bit)
 * Total: 64 bits = 8 bytes
 *
 * Transmission: event-driven (type 255), event timer 10 ms,
 *               inhibit time 2 ms (= 20 * 100 µs).
 *
 * Returns 0 on success, negative error code on failure.
 */
int configure_tpdo1_drive_status(uint8_t node_id)
{
    int rc;
    uint32_t cob_id;

    /* ------------------------------------------------------------------ */
    /* Step 1: Disable TPDO1 by setting bit 31 of COB-ID                  */
    /* ------------------------------------------------------------------ */
    cob_id = PDO_COBID_INVALID(TPDO1_DEFAULT_COBID(node_id));
    rc = sdo_write_u32(node_id, 0x1800, 0x01, cob_id);
    if (rc != 0) return rc;

    /* ------------------------------------------------------------------ */
    /* Step 2: Disable mapping (set count to 0)                            */
    /* ------------------------------------------------------------------ */
    rc = sdo_write_u8(node_id, 0x1A00, 0x00, 0);
    if (rc != 0) return rc;

    /* ------------------------------------------------------------------ */
    /* Step 3: Write mapping entries                                        */
    /* ------------------------------------------------------------------ */
    /* Entry 1: Statusword (0x6041:00, 16 bits) */
    rc = sdo_write_u32(node_id, 0x1A00, 0x01,
                       pdo_mapping_entry(OD_STATUSWORD, 0x00, 16));
    if (rc != 0) return rc;

    /* Entry 2: Actual Position (0x6064:00, 32 bits) */
    rc = sdo_write_u32(node_id, 0x1A00, 0x02,
                       pdo_mapping_entry(OD_ACTUAL_POSITION, 0x00, 32));
    if (rc != 0) return rc;

    /* Entry 3: Actual Velocity (0x606C:00, 16 bits) */
    rc = sdo_write_u32(node_id, 0x1A00, 0x03,
                       pdo_mapping_entry(OD_ACTUAL_VELOCITY, 0x00, 16));
    if (rc != 0) return rc;

    /* ------------------------------------------------------------------ */
    /* Step 4: Enable mapping — set count to 3                             */
    /* ------------------------------------------------------------------ */
    rc = sdo_write_u8(node_id, 0x1A00, 0x00, 3);
    if (rc != 0) return rc;

    /* ------------------------------------------------------------------ */
    /* Step 5: Set communication parameters                                */
    /* ------------------------------------------------------------------ */

    /* Transmission type: event-driven (255) */
    rc = sdo_write_u8(node_id, 0x1800, 0x02, PDO_TRANS_ASYNC_EVENT);
    if (rc != 0) return rc;

    /* Inhibit time: 20 * 100 µs = 2 ms */
    rc = sdo_write_u16(node_id, 0x1800, 0x03, 20);
    if (rc != 0) return rc;

    /* Event timer: 10 ms (periodic refresh even without data change) */
    rc = sdo_write_u16(node_id, 0x1800, 0x05, 10);
    if (rc != 0) return rc;

    /* ------------------------------------------------------------------ */
    /* Step 6: Re-enable TPDO (clear bit 31)                              */
    /* ------------------------------------------------------------------ */
    cob_id = PDO_COBID_VALID(TPDO1_DEFAULT_COBID(node_id));
    rc = sdo_write_u32(node_id, 0x1800, 0x01, cob_id);
    if (rc != 0) return rc;

    return 0;
}
```

### 12.3 Configuring an RPDO via SDO

```c
#define OD_CONTROLWORD      0x6040U
#define OD_TARGET_VELOCITY  0x60FFU
#define OD_TARGET_TORQUE    0x6071U

/**
 * configure_rpdo1_drive_control()
 *
 * Configures RPDO1 of a CiA 402 drive to receive:
 *   - Controlword       (0x6040:00, 16 bit)
 *   - Target Velocity   (0x60FF:00, 32 bit)
 *   - Target Torque     (0x6071:00, 16 bit)
 * Total: 64 bits = 8 bytes
 *
 * Transmission: event-driven (type 255), RPDO watchdog 50 ms.
 */
int configure_rpdo1_drive_control(uint8_t node_id)
{
    int rc;
    uint32_t cob_id;

    /* Step 1: Disable RPDO */
    cob_id = PDO_COBID_INVALID(RPDO1_DEFAULT_COBID(node_id));
    rc = sdo_write_u32(node_id, 0x1400, 0x01, cob_id);
    if (rc != 0) return rc;

    /* Step 2: Disable mapping */
    rc = sdo_write_u8(node_id, 0x1600, 0x00, 0);
    if (rc != 0) return rc;

    /* Step 3: Write mapping entries */
    rc = sdo_write_u32(node_id, 0x1600, 0x01,
                       pdo_mapping_entry(OD_CONTROLWORD, 0x00, 16));
    if (rc != 0) return rc;

    rc = sdo_write_u32(node_id, 0x1600, 0x02,
                       pdo_mapping_entry(OD_TARGET_VELOCITY, 0x00, 32));
    if (rc != 0) return rc;

    rc = sdo_write_u32(node_id, 0x1600, 0x03,
                       pdo_mapping_entry(OD_TARGET_TORQUE, 0x00, 16));
    if (rc != 0) return rc;

    /* Step 4: Enable mapping */
    rc = sdo_write_u8(node_id, 0x1600, 0x00, 3);
    if (rc != 0) return rc;

    /* Step 5: Transmission type and RPDO watchdog */
    rc = sdo_write_u8(node_id, 0x1400, 0x02, PDO_TRANS_ASYNC_EVENT);
    if (rc != 0) return rc;

    /* RPDO event timer = watchdog: 50 ms */
    rc = sdo_write_u16(node_id, 0x1400, 0x05, 50);
    if (rc != 0) return rc;

    /* Step 6: Re-enable RPDO */
    cob_id = PDO_COBID_VALID(RPDO1_DEFAULT_COBID(node_id));
    rc = sdo_write_u32(node_id, 0x1400, 0x01, cob_id);
    if (rc != 0) return rc;

    return 0;
}
```

### 12.4 Transmitting a TPDO

This example shows a minimal TPDO transmit implementation inside a CANopen slave node.
It demonstrates both event-triggered and SYNC-triggered transmission.

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* Platform CAN frame structure */
typedef struct {
    uint32_t cob_id;     /* 11-bit CAN identifier             */
    uint8_t  dlc;        /* data length code (0–8)            */
    uint8_t  data[8];    /* payload                           */
} CAN_Frame_t;

/* Platform CAN send function */
extern int can_send(const CAN_Frame_t *frame);

/* Platform time in milliseconds */
extern uint32_t get_time_ms(void);

/* ---------------------------------------------------------------- */
/* Application process data (the actual values being communicated)  */
/* ---------------------------------------------------------------- */
typedef struct {
    uint16_t statusword;        /* 0x6041:00 */
    int32_t  actual_position;   /* 0x6064:00 */
    int16_t  actual_velocity;   /* 0x606C:00 */
} DriveStatus_t;

/* ---------------------------------------------------------------- */
/* TPDO1 runtime state                                              */
/* ---------------------------------------------------------------- */
typedef struct {
    uint32_t    cob_id;
    uint8_t     trans_type;
    uint16_t    inhibit_time_100us; /* in 100 µs units */
    uint16_t    event_timer_ms;
    uint32_t    last_tx_ms;         /* timestamp of last transmission */
    uint32_t    event_timer_ms_ts;  /* timestamp of last timer reset  */
    uint8_t     sync_counter;       /* counts received SYNC frames    */
    bool        data_changed;       /* flag set by application        */
    bool        valid;
} TPDO_State_t;

static TPDO_State_t tpdo1 = {
    .cob_id              = 0x181,   /* node ID = 1 */
    .trans_type          = 255,
    .inhibit_time_100us  = 20,      /* 2 ms */
    .event_timer_ms      = 10,      /* 10 ms */
    .last_tx_ms          = 0,
    .event_timer_ms_ts   = 0,
    .sync_counter        = 0,
    .data_changed        = false,
    .valid               = true
};

static DriveStatus_t drive_status;

/**
 * pack_tpdo1() — Pack application data into 8-byte CAN frame payload.
 * All values are little-endian as required by CANopen.
 */
static void pack_tpdo1(uint8_t *out)
{
    /* Statusword: 2 bytes LE */
    out[0] = (uint8_t)(drive_status.statusword & 0xFF);
    out[1] = (uint8_t)(drive_status.statusword >> 8);

    /* Actual Position: 4 bytes LE */
    out[2] = (uint8_t)(drive_status.actual_position & 0xFF);
    out[3] = (uint8_t)((drive_status.actual_position >> 8)  & 0xFF);
    out[4] = (uint8_t)((drive_status.actual_position >> 16) & 0xFF);
    out[5] = (uint8_t)((drive_status.actual_position >> 24) & 0xFF);

    /* Actual Velocity: 2 bytes LE */
    out[6] = (uint8_t)(drive_status.actual_velocity & 0xFF);
    out[7] = (uint8_t)(drive_status.actual_velocity >> 8);
}

/**
 * tpdo1_try_send() — Attempt to transmit TPDO1 if allowed by inhibit time.
 * Returns true if the frame was sent.
 */
static bool tpdo1_try_send(void)
{
    uint32_t now_ms = get_time_ms();
    /* Convert inhibit time from 100 µs to ms (rounded up) */
    uint32_t inhibit_ms = ((uint32_t)tpdo1.inhibit_time_100us + 9) / 10;

    if ((now_ms - tpdo1.last_tx_ms) < inhibit_ms) {
        return false;  /* inhibit time not yet elapsed */
    }

    CAN_Frame_t frame;
    frame.cob_id = tpdo1.cob_id;
    frame.dlc    = 8;
    pack_tpdo1(frame.data);

    if (can_send(&frame) == 0) {
        tpdo1.last_tx_ms       = now_ms;
        tpdo1.event_timer_ms_ts = now_ms;  /* reset event timer */
        tpdo1.data_changed     = false;
        return true;
    }
    return false;
}

/**
 * tpdo1_on_event() — Called by the application when process data changes.
 * For transmission types 254 and 255.
 */
void tpdo1_on_event(void)
{
    if (!tpdo1.valid) return;
    if (tpdo1.trans_type != PDO_TRANS_ASYNC_EVENT &&
        tpdo1.trans_type != PDO_TRANS_ASYNC_MANUF) return;

    tpdo1.data_changed = true;
    tpdo1_try_send();
}

/**
 * tpdo1_on_sync() — Called when a SYNC frame is received.
 * For transmission types 0 and 1–240.
 */
void tpdo1_on_sync(void)
{
    if (!tpdo1.valid) return;

    if (tpdo1.trans_type == PDO_TRANS_ACYCLIC_SYNC) {
        /* Send only if data changed since last transmission */
        if (tpdo1.data_changed) {
            tpdo1_try_send();
        }
    } else if (tpdo1.trans_type >= 1 && tpdo1.trans_type <= 240) {
        /* Send every N-th SYNC */
        tpdo1.sync_counter++;
        if (tpdo1.sync_counter >= tpdo1.trans_type) {
            tpdo1.sync_counter = 0;
            tpdo1_try_send();
        }
    }
}

/**
 * tpdo1_periodic() — Call from the application main loop or timer ISR.
 * Handles event timer (periodic PDO refresh for types 254/255).
 */
void tpdo1_periodic(void)
{
    if (!tpdo1.valid) return;
    if (tpdo1.event_timer_ms == 0) return;
    if (tpdo1.trans_type != PDO_TRANS_ASYNC_EVENT &&
        tpdo1.trans_type != PDO_TRANS_ASYNC_MANUF) return;

    uint32_t now_ms = get_time_ms();
    if ((now_ms - tpdo1.event_timer_ms_ts) >= tpdo1.event_timer_ms) {
        tpdo1_try_send();
    }
}

/**
 * app_update_drive_status() — Example: application updates process data.
 * Call this whenever the drive hardware reports new values.
 */
void app_update_drive_status(uint16_t sw, int32_t pos, int16_t vel)
{
    drive_status.statusword      = sw;
    drive_status.actual_position = pos;
    drive_status.actual_velocity = vel;
    tpdo1_on_event();  /* trigger PDO transmission */
}
```

### 12.5 Receiving and Processing an RPDO

```c
/* ---------------------------------------------------------------- */
/* RPDO1 receive: Controlword + Target Velocity + Target Torque     */
/* ---------------------------------------------------------------- */

typedef struct {
    uint16_t controlword;       /* 0x6040:00 */
    int32_t  target_velocity;   /* 0x60FF:00 */
    int16_t  target_torque;     /* 0x6071:00 */
} DriveControl_t;

static DriveControl_t drive_control;
static bool rpdo1_received = false;

/**
 * rpdo1_unpack() — Extract process data from raw CAN frame data.
 * All values are little-endian (CANopen standard).
 */
static void rpdo1_unpack(const uint8_t *data)
{
    drive_control.controlword = (uint16_t)data[0]
                              | ((uint16_t)data[1] << 8);

    drive_control.target_velocity = (int32_t)data[2]
                                  | ((int32_t)data[3] << 8)
                                  | ((int32_t)data[4] << 16)
                                  | ((int32_t)data[5] << 24);

    drive_control.target_torque = (int16_t)data[6]
                                | ((int16_t)data[7] << 8);
}

/**
 * can_rx_dispatch() — Called from CAN receive ISR or receive task.
 * Routes incoming CAN frames to the appropriate PDO handler.
 */
void can_rx_dispatch(const CAN_Frame_t *frame)
{
    /* Check for RPDO1 COB-ID (node ID = 1 → 0x201) */
    if (frame->cob_id == RPDO1_DEFAULT_COBID(1) && frame->dlc == 8) {
        rpdo1_unpack(frame->data);
        rpdo1_received = true;
        rpdo1_watchdog_reset();   /* reset the reception watchdog */
        app_apply_drive_control(&drive_control);
    }
}

/* Application callback — executes new setpoints */
void app_apply_drive_control(const DriveControl_t *ctrl)
{
    /* Process controlword bits, update velocity/torque setpoints, etc. */
}
```

### 12.6 Inhibit Time and Event Timer Handling

A more precise inhibit time implementation using a hardware or OS timer:

```cpp
#include <chrono>

class PDOInhibitGuard {
public:
    explicit PDOInhibitGuard(uint32_t inhibit_100us)
        : inhibit_us_(inhibit_100us * 100U),
          last_tx_(std::chrono::steady_clock::now() -
                   std::chrono::microseconds(inhibit_us_))
    {}

    /**
     * try_allow() — Returns true if the inhibit time has elapsed
     * since the last transmission. Call this before sending.
     */
    bool try_allow() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                now - last_tx_).count();
        return elapsed_us >= (long long)inhibit_us_;
    }

    /** Mark a successful transmission to reset the inhibit timer. */
    void mark_sent() {
        last_tx_ = std::chrono::steady_clock::now();
    }

private:
    uint32_t inhibit_us_;
    std::chrono::steady_clock::time_point last_tx_;
};

class PDOEventTimer {
public:
    explicit PDOEventTimer(uint32_t period_ms)
        : period_ms_(period_ms),
          last_event_(std::chrono::steady_clock::now())
    {}

    /** Returns true if the event timer has expired. */
    bool is_expired() const {
        if (period_ms_ == 0) return false;
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_event_).count();
        return elapsed_ms >= (long long)period_ms_;
    }

    /** Reset the event timer after a PDO was transmitted. */
    void reset() {
        last_event_ = std::chrono::steady_clock::now();
    }

private:
    uint32_t period_ms_;
    std::chrono::steady_clock::time_point last_event_;
};

/* Usage in a TPDO transmit function: */
static PDOInhibitGuard  tpdo_inhibit(20);   /* 20 * 100 µs = 2 ms  */
static PDOEventTimer    tpdo_timer(10);     /* 10 ms periodic       */

void tpdo_service(void) {
    bool should_send = tpdo_timer.is_expired() || data_has_changed();

    if (should_send && tpdo_inhibit.try_allow()) {
        if (transmit_tpdo1() == 0) {
            tpdo_inhibit.mark_sent();
            tpdo_timer.reset();
            clear_data_changed();
        }
    }
}
```

### 12.7 RPDO Timeout Watchdog

```c
#include <stdint.h>
#include <stdbool.h>

/* Platform */
extern uint32_t get_time_ms(void);
extern void     send_emergency(uint16_t error_code, uint8_t error_reg,
                               const uint8_t *vendor_data);
extern void     set_output_safe_state(void);

typedef struct {
    uint16_t timeout_ms;        /* configured watchdog period           */
    uint32_t last_rx_ms;        /* time of last received RPDO           */
    bool     timeout_active;    /* true = watchdog is armed             */
    bool     timed_out;         /* true = timeout was detected          */
} RPDO_Watchdog_t;

static RPDO_Watchdog_t rpdo1_wdog = {
    .timeout_ms     = 50,       /* 50 ms watchdog */
    .last_rx_ms     = 0,
    .timeout_active = false,
    .timed_out      = false
};

/** Call this when the RPDO is successfully received. */
void rpdo1_watchdog_reset(void)
{
    rpdo1_wdog.last_rx_ms    = get_time_ms();
    rpdo1_wdog.timeout_active = true;
    rpdo1_wdog.timed_out      = false;
}

/** Call this periodically (e.g. every 1 ms) to check for timeout. */
void rpdo1_watchdog_check(void)
{
    if (!rpdo1_wdog.timeout_active) return;
    if (rpdo1_wdog.timed_out)       return;
    if (rpdo1_wdog.timeout_ms == 0) return;

    uint32_t now  = get_time_ms();
    uint32_t diff = now - rpdo1_wdog.last_rx_ms;

    if (diff >= rpdo1_wdog.timeout_ms) {
        rpdo1_wdog.timed_out = true;

        /*
         * React to the communication failure:
         * 1. Switch outputs to safe state (e.g. zero velocity/torque)
         * 2. Send EMCY: PDO not processed (0x8130)
         */
        set_output_safe_state();

        uint8_t vendor_data[5] = {0x00, 0x00, 0x00, 0x00, 0x01};
        send_emergency(0x8130,  /* PDO not processed */
                       0x10,    /* communication error */
                       vendor_data);
    }
}
```

### 12.8 Full Node Example (CANopen Slave)

A skeleton showing the main loop structure of a complete CANopen slave with PDO handling:

```cpp
#include <cstdint>
#include <cstring>

/* Simplified CANopen slave loop */
class CanOpenSlave {
public:
    CanOpenSlave(uint8_t node_id)
        : node_id_(node_id),
          tpdo_inhibit_(20),   /* 2 ms  */
          tpdo_timer_(10),     /* 10 ms */
          sync_count_(0),
          op_state_(PRE_OPERATIONAL)
    {}

    /* Called from CAN RX interrupt or task */
    void on_can_receive(const CAN_Frame_t& frame) {
        switch (frame.cob_id) {
        case 0x080:
            /* SYNC received */
            on_sync();
            break;
        default:
            if (frame.cob_id == rpdo1_cob_id()) {
                on_rpdo1(frame.data, frame.dlc);
            }
            break;
        }
    }

    /* Call from 1 ms system tick */
    void on_tick_1ms() {
        rpdo1_watchdog_check();
        tpdo1_service();
    }

    /* Call when application data changes */
    void notify_data_changed() {
        data_changed_ = true;
        tpdo1_service();
    }

private:
    enum State { PRE_OPERATIONAL, OPERATIONAL, STOPPED };

    uint8_t  node_id_;
    State    op_state_;
    uint8_t  sync_count_;
    bool     data_changed_ = false;

    PDOInhibitGuard tpdo_inhibit_;
    PDOEventTimer   tpdo_timer_;

    uint32_t rpdo1_cob_id() const {
        return RPDO1_DEFAULT_COBID(node_id_);
    }

    void on_sync() {
        if (op_state_ != OPERATIONAL) return;
        sync_count_++;
        /* Handle synchronous TPDOs here if configured */
    }

    void on_rpdo1(const uint8_t* data, uint8_t dlc) {
        if (op_state_ != OPERATIONAL || dlc < 8) return;
        rpdo1_unpack(data);
        rpdo1_watchdog_reset();
        app_apply_drive_control(&drive_control);
    }

    void tpdo1_service() {
        if (op_state_ != OPERATIONAL) return;

        bool should_send = (data_changed_ && tpdo_timer_.is_expired() == false)
                        || tpdo_timer_.is_expired();

        if (should_send && tpdo_inhibit_.try_allow()) {
            CAN_Frame_t frame;
            frame.cob_id = TPDO1_DEFAULT_COBID(node_id_);
            frame.dlc    = 8;
            pack_tpdo1(frame.data);
            if (can_send(&frame) == 0) {
                tpdo_inhibit_.mark_sent();
                tpdo_timer_.reset();
                data_changed_ = false;
            }
        }
    }

    /* Defined elsewhere in the application */
    void rpdo1_watchdog_reset();
    void rpdo1_watchdog_check();
};
```

---

## 13. Summary

Process Data Objects (PDOs) are the backbone of real-time communication in CANopen networks.
They provide a lean, zero-overhead mechanism for exchanging process variables between nodes
using standard CAN frames. The following table summarises the essential concepts covered
in this chapter.

| Topic | Key Points |
|-------|------------|
| **Model** | Producer/consumer; one sender, one or many receivers; broadcast on CAN bus |
| **TPDO** | Transmit PDO: node sends its own process data to the bus |
| **RPDO** | Receive PDO: node listens for specific COB-ID and extracts data |
| **Comm. Param.** | 0x1400–0x15FF (RPDO) / 0x1800–0x19FF (TPDO); COB-ID, trans. type, timers |
| **Map. Param.** | 0x1600–0x17FF (RPDO) / 0x1A00–0x1BFF (TPDO); up to 8 OD objects, ≤64 bits |
| **Mapping Entry** | 32-bit: Index(16) + Sub(8) + BitLength(8); e.g. 0x60410010 = Statusword |
| **COB-ID** | 11-bit CAN ID; bit 31 = disable, bit 30 = no RTR; default = base + NodeID |
| **Trans. Type 0** | Acyclic sync: send on SYNC only if data changed |
| **Trans. 1–240** | Cyclic sync: send every N-th SYNC unconditionally |
| **Trans. 254/255** | Asynchronous event-driven; most common for real-time process data |
| **Inhibit Time** | Minimum gap between two PDO frames; unit = 100 µs; prevents bus flooding |
| **Event Timer** | TPDO: periodic refresh even without change; RPDO: reception watchdog |
| **RPDO Timeout** | If RPDO not received within event timer period → safe state + EMCY 0x8130 |
| **Dyn. Config.** | Disable PDO → disable mapping → write new mapping → enable → enable PDO |
| **Byte Order** | All PDO data is little-endian (LSB first) per CANopen specification |

### Configuration Sequence Quick Reference

```
  TPDO/RPDO Reconfiguration Sequence
  ====================================

  1. Write COB-ID with bit 31 = 1        (disable PDO)
  2. Write 0 to mapping sub 0x00         (disable mapping)
  3. Write mapping entries sub 0x01...   (define content)
  4. Write count to mapping sub 0x00     (enable mapping)
  5. Write trans. type, inhibit, timer   (set behaviour)
  6. Write COB-ID with bit 31 = 0        (enable PDO)

  Never write mapping entries while the PDO is active!
```

### Transmission Type Decision Guide

```
  Is process data tied to motion controller SYNC?
      |
      +--YES--> Is data always valid every SYNC?
      |             |
      |             +--YES--> Type 1–240 (Cyclic Synchronous)
      |             |         (choose N for your cycle ratio)
      |             |
      |             +--NO---> Type 0 (Acyclic Synchronous)
      |                       (send on SYNC only if changed)
      |
      +--NO---> Is there a device-profile defined event?
                    |
                    +--YES--> Type 255 (Profile Event)
                    |
                    +--NO---> Type 254 (Manufacturer Event)
                              (define your own trigger)
```

PDOs, combined with the SYNC object for coordinated motion and the NMT state machine for
lifecycle management, form the foundation of deterministic real-time control in CANopen systems
ranging from simple I/O modules to complex multi-axis servo drives.