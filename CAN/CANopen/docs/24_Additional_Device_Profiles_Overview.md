# 24. CiA 447 / CiA 454 — Additional Device Profiles Overview

**Structure (10 sections + summary):**

- **Profile landscape** — ASCII tree showing how all profiles branch from CiA 301
- **CiA 410 Inclinometers** — OD table, PDO defaults, ASCII node layout diagram
- **CiA 412 Medical Devices** — NMT state machine ASCII diagram with stricter transition rules
- **CiA 418/419 Battery & Charger** — bit-coded status field, ASCII charging system architecture
- **CiA 447 J1939 Gateway** — dual-bus topology diagram, mapping record layout, address claiming objects
- **CiA 454 Energy Management** — microgrid ASCII system diagram, mode enum table, scheduling objects

**Six C/C++ programming examples:**

1. CiA 410 — SDO read + TPDO callback for live inclination data
2. CiA 418 — BMS status polling with bit-field decoder
3. CiA 447 — Gateway PGN→OD mapping configuration via SDO
4. CiA 454 — C++ wrapper class for power set-points and energy counters
5. CiA 418 — Dynamic TPDO3 configuration for cell voltage arrays
6. EDS snippet — CiA 447 manufacturer extension in `.eds` format

**Summary** closes with an ASCII quick-reference table covering all six profiles at a glance.


## Table of Contents

1. [Introduction](#introduction)
2. [What Is a CANopen Device Profile?](#what-is-a-canopen-device-profile)
3. [Profile Landscape Overview](#profile-landscape-overview)
4. [CiA 410 — Inclinometers](#cia-410--inclinometers)
5. [CiA 412 — Medical Devices](#cia-412--medical-devices)
6. [CiA 418 / CiA 419 — Battery Systems and Chargers](#cia-418--cia-419--battery-systems-and-chargers)
7. [CiA 447 — CANopen / J1939 Gateway](#cia-447--canopen--j1939-gateway)
8. [CiA 454 — Energy Management Systems](#cia-454--energy-management-systems)
9. [Choosing the Right Profile](#choosing-the-right-profile)
10. [Extending Profiles with Manufacturer-Specific Objects](#extending-profiles-with-manufacturer-specific-objects)
11. [Programming Examples in C/C++](#programming-examples-in-cc)
12. [Summary](#summary)

---

## Introduction

The CANopen application layer (CiA 301) and device profile framework (CiA 302) form the foundation
on which dozens of standardised **device profiles** are built. Each profile defines a set of mandatory
and optional object dictionary entries, PDO mappings, and state-machine extensions that make
devices from different manufacturers interoperable without custom drivers.

This chapter surveys six profiles that are especially relevant in industrial automation, mobile
machinery, medical technology, and energy management:

| Profile | Short name | Domain |
|---------|-----------|--------|
| CiA 410 | Inclinometers | Mobile machinery, levelling |
| CiA 412 | Medical devices | Clinical equipment |
| CiA 418 | Battery systems | Traction / stationary storage |
| CiA 419 | Battery chargers | Charging infrastructure |
| CiA 447 | J1939 gateway | Cross-network bridging |
| CiA 454 | Energy management | Smart grids, microgrids |

Understanding these profiles lets you select the right baseline for a new device, extend it legally
within the CANopen rules, and avoid reinventing the wheel.

---

## What Is a CANopen Device Profile?

A **device profile** is a CiA specification that inherits from CiA 301 and adds:

```
CiA 301 (Application Layer)
        │
        ├── Object Dictionary (OD)
        │     ├── Communication objects  0x1000 – 0x1FFF
        │     ├── Manufacturer objects   0x2000 – 0x5FFF
        │     └── Profile objects        0x6000 – 0x9FFF
        │
        └── Device Profile (e.g. CiA 410)
              ├── Mandatory profile objects
              ├── Optional profile objects
              └── Profile-specific PDO defaults
```

Profile objects live in the range **0x6000 – 0x9FFF**. Manufacturer-specific extensions use
**0x2000 – 0x5FFF** and must not conflict with the chosen profile's defined indices.

The Electronic Data Sheet (EDS) file describes a device's OD in a machine-readable format
recognised by configuration tools such as CANopen Magic, PEAK PCAN-Explorer, and others.

---

## Profile Landscape Overview

```
                    ┌──────────────────────────────────────┐
                    │        CANopen Ecosystem             │
                    │                                      │
        ┌───────────┤   CiA 301  (Application Layer)       │
        │           │   CiA 302  (Additional Functions)    │
        │           └──────────────────────────────────────┘
        │
        ├─── CiA 40x  ── Sensors & Measurement
        │       ├── CiA 404   Measuring Devices / Closed-Loop Controllers
        │       └── CiA 410   Inclinometers ◄─── this chapter
        │
        ├─── CiA 41x  ── Medical
        │       └── CiA 412   Medical Devices ◄─── this chapter
        │
        ├─── CiA 41x  ── Energy Storage
        │       ├── CiA 418   Battery Systems ◄─── this chapter
        │       └── CiA 419   Battery Chargers ◄─── this chapter
        │
        ├─── CiA 44x  ── Gateways
        │       └── CiA 447   J1939 Gateway ◄─── this chapter
        │
        └─── CiA 45x  ── Energy Management
                └── CiA 454   Energy Management ◄─── this chapter
```

Every profile must be implemented on top of a CiA 301-compliant stack. A node may implement
**multiple profiles** simultaneously if none of their required OD indices clash.

---

## CiA 410 — Inclinometers

### Overview

CiA 410 standardises tilt and inclination sensors used in mobile machinery (cranes, agricultural
equipment, construction vehicles, forklifts). It defines measurement axes, units (degrees × 10⁻²),
scaling, and alarm conditions.

### Key Object Dictionary Entries

| Index | Sub | Name | Type | Description |
|-------|-----|------|------|-------------|
| 0x6000 | 0x01 | Inclination X-axis | INT32 | Tilt angle × 100, unit 0.01° |
| 0x6000 | 0x02 | Inclination Y-axis | INT32 | Tilt angle × 100 |
| 0x6001 | 0x00 | Operating status | UINT8 | Bit-coded sensor status |
| 0x6002 | 0x01 | Warning threshold X | INT32 | Alarm trigger level |
| 0x6010 | 0x01 | Sensor range X | INT32 | Max measurable angle |

### PDO Defaults

```
TPDO1  (COB-ID 0x180 + Node-ID)
  Mapping:  0x6000:01 (4 bytes, Inclination X)
            0x6000:02 (4 bytes, Inclination Y)
  Transmission: Event-driven or cyclic (configurable)
```

### Typical Node Layout

```
  ┌──────────────────────────────────┐
  │   Inclinometer Node (CiA 410)    │
  │                                  │
  │   MEMS Sensor                    │
  │       │                          │
  │   Signal Conditioning            │
  │       │                          │
  │   OD 0x6000:01  Incl-X  ─────────┼──► TPDO1
  │   OD 0x6000:02  Incl-Y  ─────────┼──► TPDO1
  │   OD 0x6001:00  Status  ─────────┼──► TPDO2
  │   OD 0x6002:01  WarnThr  ◄───────┼─── RPDO1 (from master)
  └──────────────────────────────────┘
```

---

## CiA 412 — Medical Devices

### Overview

CiA 412 is the CANopen profile for medical and laboratory equipment. It extends the base
application layer with:

- **Safety-relevant object types** (mandatory cyclic heartbeat, guarding)
- **Device identification** objects (serial number, manufacturing date)
- **Operational mode management** (standby, operational, service)
- Strict requirements on NMT state transitions to avoid hazardous operating states

Because medical devices are subject to IEC 62443 / IEC 60601, CiA 412 mandates that the
NMT Pre-Operational state is entered before any critical measurement begins, and the master
must explicitly command Operational.

### Key Considerations

```
  NMT State Machine (CiA 412 stricter requirements)
  ─────────────────────────────────────────────────

  Power-On
     │
     ▼
  Initialisation ──► Boot-Up message (mandatory within 1 s)
     │
     ▼
  Pre-Operational   ◄── Default landing state
     │  ▲
     │  │  SDO configuration of safety parameters
     ▼  │
  Operational       ◄── Only when master confirms readiness
     │
     ▼
  Stopped           ◄── All PDOs inhibited; SDO still active
```

### Mandatory Objects for CiA 412

| Index | Name | Requirement |
|-------|------|-------------|
| 0x1000 | Device type | Mandatory (bit 16–31 = 0x0194 for medical) |
| 0x1008 | Device name | Mandatory |
| 0x1018 | Identity object | Mandatory, all four sub-indices |
| 0x6000 | Device status | Mandatory |
| 0x6001 | Operating mode | Mandatory |

---

## CiA 418 / CiA 419 — Battery Systems and Chargers

### CiA 418 — Battery Systems

CiA 418 covers traction batteries, stationary storage, and UPS systems. It defines objects for:

- State of charge (SoC), state of health (SoH)
- Cell voltages and temperatures
- Protection relay status
- Balancing control

```
  Battery Management System (BMS) — CiA 418 Object Map
  ──────────────────────────────────────────────────────

  0x6000  Battery Status (UINT16, bit-coded)
          Bit 0: Ready
          Bit 1: Charging
          Bit 2: Fault
          Bit 3: Balancing active
          Bit 4: Over-temperature
          Bit 5: Under-voltage

  0x6001  State of Charge      [0 – 10000] = [0.00 – 100.00 %]
  0x6002  State of Health      [0 – 10000]
  0x6003  Pack Voltage         [mV]
  0x6004  Pack Current         [mA, signed]
  0x6010  Cell Voltage Array   sub-index = cell number
  0x6020  Cell Temperature Array
```

### CiA 419 — Battery Chargers

CiA 419 is the complementary profile for the charging side. A charger node implements:

```
  ┌─────────────────────────────────────────────────────┐
  │          Charging System Architecture               │
  │                                                     │
  │   AC Grid                                           │
  │     │                                               │
  │     ▼                                               │
  │  ┌──────────────┐   RPDO (set-points)               │
  │  │  CiA 419     │◄──────────────────────── Master   │
  │  │  Charger     │                                   │
  │  │  Node        │──────────────────────── Master    │
  │  │              │   TPDO (status / measurements)    │
  │  └──────┬───────┘                                   │
  │         │  DC output                                │
  │         ▼                                           │
  │  ┌──────────────┐                                   │
  │  │  CiA 418     │   Battery pack                    │
  │  │  BMS Node    │                                   │
  │  └──────────────┘                                   │
  └─────────────────────────────────────────────────────┘
```

Key CiA 419 objects:

| Index | Name | Description |
|-------|------|-------------|
| 0x6000 | Charger status | Bit-coded operational status |
| 0x6001 | Charging mode | CC / CV / Trickle / Off |
| 0x6010 | Target voltage | mV set-point |
| 0x6011 | Current limit | mA maximum charge current |
| 0x6020 | Output voltage | Measured output voltage |
| 0x6021 | Output current | Measured output current |

---

## CiA 447 — CANopen / J1939 Gateway

### Background

**J1939** (SAE J1939) is the dominant CAN-based protocol in heavy-duty vehicles and off-highway
machinery. It uses 29-bit extended CAN identifiers, Parameter Group Numbers (PGNs), and Source
Addresses (SA) — all concepts foreign to a CANopen network that relies on 11-bit identifiers
and Node-IDs.

**CiA 447** defines a standardised gateway device that bridges the two worlds, translating
objects in both directions without custom firmware on either side.

### Network Topology

```
  J1939 Network (29-bit CAN IDs)         CANopen Network (11-bit CAN IDs)
  ────────────────────────────────        ─────────────────────────────────

  Engine ECU  ──┐                         ┌── CANopen Master
  Trans ECU   ──┤  J1939 Bus              │
  ABS ECU     ──┤══════════════╗          ├── CiA 410 Inclinometer
  Dashboard   ──┘              ║          │
                         ┌─────╨──────┐   ├── CiA 418 BMS
                         │  CiA 447   │   │
                         │  J1939 /   │═══╧══ CANopen Bus
                         │  CANopen   │
                         │  Gateway   │
                         └────────────┘

  PGN 0xFEF1 → OD 0x6100:01 (Engine Speed)
  OD 0x6200:01 → PGN 0xFF00 (Remote Torque Demand)
```

### Mapping Model

CiA 447 uses a table of **mapping entries** stored in the gateway's OD:

```
  Gateway OD (CiA 447)
  ─────────────────────────────────────────────────────

  0x5F00  J1939-to-CANopen mapping table
    :01   Number of mappings
    :02   Mapping 1: PGN | SP | CANopen-Index | Sub | Scale
    :03   Mapping 2: ...

  0x5F01  CANopen-to-J1939 mapping table
    :01   Number of mappings
    :02   ...

  0x5F10  J1939 source address filter
  0x5F11  J1939 priority for outgoing PGNs
```

A single mapping entry encodes:

```
  Bytes 1–3  : PGN (24-bit Parameter Group Number)
  Byte  4    : SPN start bit within the PGN data
  Byte  5    : SPN bit length
  Bytes 6–7  : CANopen Object Index
  Byte  8    : CANopen Sub-index
  Bytes 9–12 : Scale factor (IEEE 754 float or fixed-point)
  Bytes 13–16: Offset
```

### NMT and J1939 Address Claiming

The gateway runs standard CANopen NMT on the CANopen side. On the J1939 side it participates in
the address claiming procedure autonomously. The J1939 name and preferred address are stored in:

```
  0x5F20  J1939 name (8 bytes)
  0x5F21  Preferred source address
  0x5F22  Address claim result (read-only, assigned address)
```

---

## CiA 454 — Energy Management Systems

### Overview

CiA 454 targets stationary and mobile energy management: microgrids, renewable energy
installations, building energy management, and smart charging hubs. It extends CiA 301 with
objects for:

- Power set-points (active and reactive power)
- Energy metering (Wh, VAh, VArh counters)
- Tariff and scheduling management
- Grid connection state (on-grid / island / transitioning)

### System Architecture

```
  ┌──────────────────────────────────────────────────────────┐
  │              Energy Management System (CiA 454)          │
  │                                                          │
  │   ┌──────────┐   ┌──────────┐   ┌──────────────────┐     │
  │   │ PV Array │   │ Battery  │   │  Grid Connection │     │
  │   │ Inverter │   │ Storage  │   │  Point (GCP)     │     │
  │   │ CiA 454  │   │ CiA 418  │   │  CiA 454         │     │
  │   └────┬─────┘   └────┬─────┘   └────────┬─────────┘     │
  │        │              │                  │               │
  │        └──────────────┴──────────────────┘               │
  │                       │                                  │
  │               CANopen Bus (CiA 301)                      │
  │                       │                                  │
  │               ┌───────┴───────┐                          │
  │               │  EMS Master   │                          │
  │               │  CiA 454      │                          │
  │               └───────────────┘                          │
  └──────────────────────────────────────────────────────────┘
```

### Key Object Dictionary Entries (CiA 454)

| Index | Sub | Name | Type | Unit |
|-------|-----|------|------|------|
| 0x6000 | 0x00 | Device mode | UINT8 | Enum |
| 0x6001 | 0x00 | Grid status | UINT16 | Bit-coded |
| 0x6010 | 0x01 | Active power set-point | INT32 | W |
| 0x6010 | 0x02 | Reactive power set-point | INT32 | VAr |
| 0x6020 | 0x01 | Active power measured | INT32 | W |
| 0x6020 | 0x02 | Reactive power measured | INT32 | VAr |
| 0x6030 | 0x01 | Energy produced (Wh) | UINT32 | Wh |
| 0x6030 | 0x02 | Energy consumed (Wh) | UINT32 | Wh |
| 0x6040 | 0x01 | Tariff zone active | UINT8 | Enum |
| 0x6050 | 0x01 | Schedule entry count | UINT8 | |
| 0x6051 | 0x01–n | Schedule table | RECORD | |

### Device Mode Enumeration (0x6000:00)

```
  0x00  Standby       – no power exchange, monitoring only
  0x01  Grid-Follow   – inverter follows grid frequency/voltage
  0x02  Grid-Form     – inverter forms island grid
  0x03  Curtailed     – active power limited by tariff/schedule
  0x04  Fault         – protective shutdown active
  0x05  Calibration   – factory / maintenance mode
```

---

## Choosing the Right Profile

Use the decision tree below when starting a new CANopen device design:

```
  Start: What is the primary function of my device?
  ────────────────────────────────────────────────────────────────

  Measures angle / tilt?
       └──► CiA 410 (Inclinometer)

  Medical / clinical equipment?
       └──► CiA 412 (Medical Devices)

  Stores electrochemical energy (battery, supercap)?
       └──► CiA 418 (Battery System)

  Charges a battery from AC or DC?
       └──► CiA 419 (Battery Charger)
            └── Often combined with CiA 418 on the battery side

  Bridges a J1939 vehicle bus to CANopen?
       └──► CiA 447 (J1939 Gateway)

  Manages energy flows across multiple sources/loads?
       └──► CiA 454 (Energy Management)
            └── May co-exist with CiA 418 on storage nodes

  None of the above?
       └──► Check: CiA 401 (I/O), CiA 402 (Drives), CiA 404 (Measurement),
                   CiA 406 (Encoders), CiA 408 (Hydraulics)
            └── Still nothing? → Manufacturer-specific profile using
                0x2000–0x5FFF range only
```

### Multi-Profile Nodes

A single physical device may implement **two profiles** if their OD index ranges do not overlap
and both are declared in **0x1000** (Device Type). The convention is:

```
  0x1000  Device Type
    Bit 0–15:  Profile number of primary profile
    Bit 16–31: Additional profile indicator (profile-dependent)
```

Example: a solar inverter with integrated battery might declare CiA 454 as primary and
CiA 418 as secondary, using non-overlapping index sub-ranges within 0x6000–0x9FFF.

---

## Extending Profiles with Manufacturer-Specific Objects

All profiles allow extension in **0x2000 – 0x5FFF** (manufacturer-specific area). The rules:

```
  ┌─────────────────────────────────────────────────────────┐
  │  Object Dictionary Layout for Extended Profile Device   │
  │                                                         │
  │  0x1000 – 0x1FFF  Communication Profile (CiA 301)       │
  │                   (NEVER modify standard definitions)   │
  │                                                         │
  │  0x2000 – 0x5FFF  Manufacturer-Specific                 │
  │     0x2000–0x2FFF   Calibration data                    │
  │     0x3000–0x3FFF   Diagnostics / logging               │
  │     0x4000–0x4FFF   Configuration extensions            │
  │     0x5000–0x5FFF   (CiA 447 gateway tables use 0x5Fxx) │
  │                                                         │
  │  0x6000 – 0x9FFF  Device Profile Objects                │
  │     Defined by chosen profile — do not invent new       │
  │     objects here if profile already covers the need     │
  └─────────────────────────────────────────────────────────┘
```

**Rules for extensions:**

1. Never redefine a profile-mandatory index — add sub-indices only where the profile allows it.
2. Document all extensions in the EDS file with `[Comments]` and manufacturer info sections.
3. Use the `AccessType=rw/ro/wo` field correctly; tool-configured parameters should be `rw`.
4. Test that your EDS is accepted by at least one third-party configuration tool.

---

## Programming Examples in C/C++

The examples below use a portable, stack-agnostic style that maps onto popular open-source
CANopen stacks (CANopenNode, lely-core, CanFestival). Object dictionary access is abstracted
through functions that each stack provides in a slightly different form — adapt as needed.

---

### Example 1 — Reading Inclination Data (CiA 410)

```c
/*
 * cia410_reader.c
 *
 * Master reads inclination X/Y from a CiA 410 node via SDO,
 * then subscribes to TPDO1 for continuous updates.
 *
 * OD indices used:
 *   0x6000:01  Inclination X-axis  (INT32, unit: 0.01 degree)
 *   0x6000:02  Inclination Y-axis  (INT32)
 */

#include <stdint.h>
#include <stdio.h>
#include "canopen_master.h"   /* stack-specific header */

#define NODE_ID_INCLINOMETER   0x05
#define OD_INCLINATION         0x6000u
#define SUB_AXIS_X             0x01u
#define SUB_AXIS_Y             0x02u

/* PDO receive callback — called by stack when TPDO1 arrives */
void on_tpdo1_received(uint8_t node_id, const uint8_t *data, uint8_t len)
{
    if (node_id != NODE_ID_INCLINOMETER || len < 8)
        return;

    /* TPDO1 maps 0x6000:01 (bytes 0-3) and 0x6000:02 (bytes 4-7) */
    int32_t incl_x = (int32_t)( (uint32_t)data[0]
                               | ((uint32_t)data[1] << 8)
                               | ((uint32_t)data[2] << 16)
                               | ((uint32_t)data[3] << 24) );

    int32_t incl_y = (int32_t)( (uint32_t)data[4]
                               | ((uint32_t)data[5] << 8)
                               | ((uint32_t)data[6] << 16)
                               | ((uint32_t)data[7] << 24) );

    /* Scale: raw value / 100 = degrees */
    printf("Inclination X: %+.2f deg  Y: %+.2f deg\n",
           incl_x / 100.0, incl_y / 100.0);
}

/* One-shot SDO read at startup to verify communication */
int read_inclination_sdo(uint8_t node_id, uint8_t sub, int32_t *out)
{
    uint8_t  buf[4];
    uint32_t size = 4;
    int rc = co_sdo_read(node_id, OD_INCLINATION, sub,
                         buf, &size, 1000 /*ms timeout*/);
    if (rc != 0) {
        fprintf(stderr, "SDO read failed: node=%02X idx=%04X:%02X err=%d\n",
                node_id, OD_INCLINATION, sub, rc);
        return rc;
    }
    *out = (int32_t)( (uint32_t)buf[0] | ((uint32_t)buf[1] << 8)
                    | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24) );
    return 0;
}

int main(void)
{
    co_master_init();
    co_register_tpdo_callback(1 /*TPDO number*/, on_tpdo1_received);

    /* NMT: start the inclinometer */
    co_nmt_send(CO_NMT_START, NODE_ID_INCLINOMETER);

    /* Initial SDO read */
    int32_t x, y;
    if (read_inclination_sdo(NODE_ID_INCLINOMETER, SUB_AXIS_X, &x) == 0 &&
        read_inclination_sdo(NODE_ID_INCLINOMETER, SUB_AXIS_Y, &y) == 0)
    {
        printf("Initial: X=%+.2f  Y=%+.2f\n", x/100.0, y/100.0);
    }

    co_master_run(); /* event loop — calls on_tpdo1_received on each PDO */
    return 0;
}
```

---

### Example 2 — CiA 418 Battery Status Monitoring

```c
/*
 * cia418_bms_monitor.c
 *
 * Master polls a CiA 418 BMS node for State of Charge,
 * Pack Voltage, and Pack Current.
 * Demonstrates expedited SDO read with error handling.
 */

#include <stdint.h>
#include <stdio.h>
#include "canopen_master.h"

#define NODE_BMS          0x0A
#define OD_BATT_STATUS    0x6000u
#define OD_SOC            0x6001u
#define OD_SOH            0x6002u
#define OD_PACK_VOLTAGE   0x6003u
#define OD_PACK_CURRENT   0x6004u

/* Bit definitions for 0x6000 battery status */
#define BATT_READY        (1u << 0)
#define BATT_CHARGING     (1u << 1)
#define BATT_FAULT        (1u << 2)
#define BATT_BALANCING    (1u << 3)
#define BATT_OVER_TEMP    (1u << 4)
#define BATT_UNDER_VOLT   (1u << 5)

static const char *decode_status(uint16_t status)
{
    static char buf[128];
    int pos = 0;
    if (status & BATT_READY)     pos += snprintf(buf+pos, 128-pos, "READY ");
    if (status & BATT_CHARGING)  pos += snprintf(buf+pos, 128-pos, "CHARGING ");
    if (status & BATT_FAULT)     pos += snprintf(buf+pos, 128-pos, "FAULT ");
    if (status & BATT_BALANCING) pos += snprintf(buf+pos, 128-pos, "BALANCING ");
    if (status & BATT_OVER_TEMP) pos += snprintf(buf+pos, 128-pos, "OVER-TEMP ");
    if (status & BATT_UNDER_VOLT)pos += snprintf(buf+pos, 128-pos, "UNDER-VOLT ");
    if (pos == 0) snprintf(buf, 128, "IDLE");
    return buf;
}

/* Read a 16-bit unsigned value via SDO */
static int sdo_read_u16(uint8_t node, uint16_t idx, uint8_t sub, uint16_t *out)
{
    uint8_t buf[2]; uint32_t sz = 2;
    int rc = co_sdo_read(node, idx, sub, buf, &sz, 1000);
    if (rc == 0) *out = (uint16_t)(buf[0] | (buf[1] << 8));
    return rc;
}

/* Read a 32-bit signed value via SDO */
static int sdo_read_i32(uint8_t node, uint16_t idx, uint8_t sub, int32_t *out)
{
    uint8_t buf[4]; uint32_t sz = 4;
    int rc = co_sdo_read(node, idx, sub, buf, &sz, 1000);
    if (rc == 0)
        *out = (int32_t)((uint32_t)buf[0] | ((uint32_t)buf[1]<<8)
                        |((uint32_t)buf[2]<<16)|((uint32_t)buf[3]<<24));
    return rc;
}

void print_bms_status(void)
{
    uint16_t status, soc, soh;
    int32_t  voltage, current;

    sdo_read_u16(NODE_BMS, OD_BATT_STATUS,  0x00, &status);
    sdo_read_u16(NODE_BMS, OD_SOC,          0x00, &soc);
    sdo_read_u16(NODE_BMS, OD_SOH,          0x00, &soh);
    sdo_read_i32(NODE_BMS, OD_PACK_VOLTAGE, 0x00, &voltage);
    sdo_read_i32(NODE_BMS, OD_PACK_CURRENT, 0x00, &current);

    printf("─────────────────────────────────\n");
    printf(" BMS Status : %s\n", decode_status(status));
    printf(" SoC        : %.1f %%\n", soc / 100.0);
    printf(" SoH        : %.1f %%\n", soh / 100.0);
    printf(" Pack V     : %.3f V\n",  voltage / 1000.0);
    printf(" Pack I     : %+.3f A\n", current / 1000.0);
    printf("─────────────────────────────────\n");
}
```

---

### Example 3 — CiA 447 Gateway: Configuring a PGN Mapping via SDO

```c
/*
 * cia447_gateway_config.c
 *
 * Configure the CiA 447 gateway to forward J1939
 * Engine Speed (PGN 0xF004, SPN 190) to
 * CANopen OD index 0x6100:01 (INT16, rpm).
 *
 * Mapping record layout (16 bytes):
 *   Bytes 0-2  : PGN (24-bit, little-endian)
 *   Byte  3    : SPN start bit in J1939 data field
 *   Byte  4    : SPN bit length
 *   Bytes 5-6  : CANopen object index
 *   Byte  7    : CANopen sub-index
 *   Bytes 8-11 : Scale factor (float32, J1939 → CANopen unit)
 *   Bytes 12-15: Offset (float32)
 */

#include <stdint.h>
#include <string.h>
#include <math.h>
#include "canopen_master.h"

#define NODE_GW            0x01u
#define OD_J2CO_MAP_TABLE  0x5F00u  /* J1939-to-CANopen mapping table */
#define SUB_MAP_COUNT      0x01u
#define SUB_MAP_FIRST      0x02u    /* first mapping entry */

/* Pack IEEE 754 float into 4 bytes, little-endian */
static void pack_float_le(float f, uint8_t *out)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    out[0] = (uint8_t)(bits);
    out[1] = (uint8_t)(bits >>  8);
    out[2] = (uint8_t)(bits >> 16);
    out[3] = (uint8_t)(bits >> 24);
}

int configure_engine_speed_mapping(void)
{
    /*
     * J1939 Engine Speed (SPN 190):
     *   PGN       : 0x00F004
     *   Start bit : 24  (byte 3, bit 0)
     *   Bit length: 16
     *   Resolution: 0.125 rpm/bit
     *
     * CANopen target:
     *   Index     : 0x6100
     *   Sub-index : 0x01
     *   Type      : UINT16, unit: 1 rpm
     *   Scale     : 0.125  (J1939 raw × 0.125 = rpm)
     */

    uint8_t mapping[16];
    memset(mapping, 0, sizeof(mapping));

    /* PGN 0x00F004 — 3 bytes, little-endian */
    mapping[0] = 0x04; mapping[1] = 0xF0; mapping[2] = 0x00;
    /* SPN start bit and length */
    mapping[3] = 24;   /* start bit */
    mapping[4] = 16;   /* bit length */
    /* CANopen target index 0x6100, sub 0x01 */
    mapping[5] = 0x00; mapping[6] = 0x61;
    mapping[7] = 0x01;
    /* Scale = 0.125 */
    pack_float_le(0.125f, &mapping[8]);
    /* Offset = 0.0 */
    pack_float_le(0.0f,   &mapping[12]);

    /* Write mapping count = 1 */
    uint8_t count = 1;
    int rc = co_sdo_write(NODE_GW, OD_J2CO_MAP_TABLE, SUB_MAP_COUNT,
                          &count, 1, 2000);
    if (rc != 0) { fprintf(stderr, "Map count write failed: %d\n", rc); return rc; }

    /* Write mapping entry */
    rc = co_sdo_write(NODE_GW, OD_J2CO_MAP_TABLE, SUB_MAP_FIRST,
                      mapping, sizeof(mapping), 2000);
    if (rc != 0) { fprintf(stderr, "Map entry write failed: %d\n", rc); return rc; }

    printf("Gateway mapping configured: J1939 PGN 0xF004 -> OD 0x6100:01\n");
    return 0;
}
```

---

### Example 4 — CiA 454 Energy Management: Set Active Power Set-Point

```cpp
/*
 * cia454_power_control.cpp
 *
 * C++ class wrapping CANopen SDO access to a CiA 454
 * energy management node. Sets active/reactive power
 * set-points and reads measured energy counters.
 */

#include <cstdint>
#include <stdexcept>
#include "canopen_master.hpp"   /* C++ stack wrapper */

class Cia454EmsNode {
public:
    static constexpr uint16_t OD_DEVICE_MODE      = 0x6000u;
    static constexpr uint16_t OD_GRID_STATUS       = 0x6001u;
    static constexpr uint16_t OD_POWER_SETPOINT    = 0x6010u;
    static constexpr uint16_t OD_POWER_MEASURED    = 0x6020u;
    static constexpr uint16_t OD_ENERGY_COUNTERS   = 0x6030u;

    /* Device mode values */
    enum class Mode : uint8_t {
        Standby    = 0x00,
        GridFollow = 0x01,
        GridForm   = 0x02,
        Curtailed  = 0x03,
        Fault      = 0x04,
        Calibration= 0x05
    };

    explicit Cia454EmsNode(uint8_t nodeId, CanOpenMaster &master)
        : nodeId_(nodeId), master_(master) {}

    /* Set active power [W] and reactive power [VAr] */
    void setPowerSetpoint(int32_t active_W, int32_t reactive_VAr)
    {
        master_.sdoWrite<int32_t>(nodeId_, OD_POWER_SETPOINT, 0x01, active_W);
        master_.sdoWrite<int32_t>(nodeId_, OD_POWER_SETPOINT, 0x02, reactive_VAr);
    }

    /* Read measured active power [W] */
    int32_t measuredActivePower()
    {
        return master_.sdoRead<int32_t>(nodeId_, OD_POWER_MEASURED, 0x01);
    }

    /* Read total energy produced [Wh] */
    uint32_t energyProduced_Wh()
    {
        return master_.sdoRead<uint32_t>(nodeId_, OD_ENERGY_COUNTERS, 0x01);
    }

    /* Read total energy consumed [Wh] */
    uint32_t energyConsumed_Wh()
    {
        return master_.sdoRead<uint32_t>(nodeId_, OD_ENERGY_COUNTERS, 0x02);
    }

    /* Change device operating mode */
    void setMode(Mode m)
    {
        master_.sdoWrite<uint8_t>(nodeId_, OD_DEVICE_MODE, 0x00,
                                  static_cast<uint8_t>(m));
    }

    /* Read grid connection status bits */
    uint16_t gridStatus()
    {
        return master_.sdoRead<uint16_t>(nodeId_, OD_GRID_STATUS, 0x00);
    }

    void printStatus()
    {
        int32_t  p   = measuredActivePower();
        uint32_t ep  = energyProduced_Wh();
        uint32_t ec  = energyConsumed_Wh();
        uint16_t gs  = gridStatus();

        printf("EMS Node 0x%02X:\n", nodeId_);
        printf("  Active power    : %+d W\n", p);
        printf("  Energy produced : %u Wh\n", ep);
        printf("  Energy consumed : %u Wh\n", ec);
        printf("  Grid status     : 0x%04X\n", gs);
    }

private:
    uint8_t       nodeId_;
    CanOpenMaster &master_;
};

/* Usage example */
int main()
{
    CanOpenMaster master;
    master.init();

    Cia454EmsNode ems(0x20, master);

    /* Switch to grid-follow mode */
    ems.setMode(Cia454EmsNode::Mode::GridFollow);

    /* Demand 5 kW export, 0 VAr */
    ems.setPowerSetpoint(5000, 0);

    /* Poll loop */
    for (;;) {
        ems.printStatus();
        platform_sleep_ms(1000);
    }
}
```

---

### Example 5 — CiA 418 TPDO Configuration for Cell Voltages

```c
/*
 * cia418_cell_voltage_pdo.c
 *
 * Configure a BMS TPDO to transmit four cell voltages
 * at 100 ms interval using PDO mapping via SDO.
 *
 * CiA 418 cell voltage: OD 0x6010, sub-index = cell number
 * Each cell value is UINT16 (mV).
 *
 * TPDO3 (0x180 + 2 + Node-ID):
 *   4 × UINT16 = 8 bytes = full CAN frame
 */

#include <stdint.h>
#include "canopen_master.h"

#define NODE_BMS         0x0Au
#define TPDO3_COMM_IDX   0x1802u   /* TPDO3 communication parameters */
#define TPDO3_MAP_IDX    0x1A02u   /* TPDO3 mapping parameters */
#define OD_CELL_VOLTAGE  0x6010u

/* Encode a PDO mapping entry:
 *   bits 31-16: object index
 *   bits 15-8 : sub-index
 *   bits 7-0  : bit length
 */
#define PDO_MAPPING(idx, sub, bits) \
    (((uint32_t)(idx) << 16) | ((uint32_t)(sub) << 8) | (bits))

int configure_cell_voltage_pdo(uint8_t node)
{
    int rc;

    /* Step 1: Disable TPDO3 by setting bit 31 of COB-ID */
    uint32_t cob_id_disabled = 0x80000000ul | (0x300ul + node);
    rc = co_sdo_write_u32(node, TPDO3_COMM_IDX, 0x01, cob_id_disabled, 2000);
    if (rc) return rc;

    /* Step 2: Clear existing mappings (set count = 0) */
    rc = co_sdo_write_u8(node, TPDO3_MAP_IDX, 0x00, 0, 2000);
    if (rc) return rc;

    /* Step 3: Write four mapping entries (cells 1–4, each 16 bits) */
    uint32_t maps[4] = {
        PDO_MAPPING(OD_CELL_VOLTAGE, 0x01, 16),
        PDO_MAPPING(OD_CELL_VOLTAGE, 0x02, 16),
        PDO_MAPPING(OD_CELL_VOLTAGE, 0x03, 16),
        PDO_MAPPING(OD_CELL_VOLTAGE, 0x04, 16),
    };
    for (int i = 0; i < 4; i++) {
        rc = co_sdo_write_u32(node, TPDO3_MAP_IDX, (uint8_t)(i+1), maps[i], 2000);
        if (rc) return rc;
    }

    /* Step 4: Set mapping count = 4 */
    rc = co_sdo_write_u8(node, TPDO3_MAP_IDX, 0x00, 4, 2000);
    if (rc) return rc;

    /* Step 5: Set transmission type = 0xFE (event-driven) */
    rc = co_sdo_write_u8(node, TPDO3_COMM_IDX, 0x02, 0xFE, 2000);
    if (rc) return rc;

    /* Step 6: Set inhibit time = 100 ms (unit: 100 µs → 1000) */
    rc = co_sdo_write_u16(node, TPDO3_COMM_IDX, 0x03, 1000, 2000);
    if (rc) return rc;

    /* Step 7: Re-enable TPDO3 */
    uint32_t cob_id_enabled = 0x300ul + node;
    rc = co_sdo_write_u32(node, TPDO3_COMM_IDX, 0x01, cob_id_enabled, 2000);
    if (rc) return rc;

    printf("TPDO3 configured: cells 1-4 at 100ms inhibit, node 0x%02X\n", node);
    return 0;
}
```

---

### Example 6 — EDS Snippet: CiA 447 Gateway Manufacturer Extension

```ini
; Electronic Data Sheet excerpt for a CiA 447 gateway node
; Shows profile declaration and manufacturer-specific gateway
; configuration objects in the 0x5F00 area.

[FileInfo]
FileName=J1939_Gateway_v2.eds
FileVersion=3
FileRevision=1
EDSVersion=4.0
Description=CiA 447 J1939/CANopen Gateway

[DeviceInfo]
VendorName=Example GmbH
ProductName=CANgate-500
OrderCode=CG500-001
BaudRate_250=1
BaudRate_500=1
SimpleBootUpMaster=0
SimpleBootUpSlave=1
Granularity=0
DynamicChannelsSupported=0
GroupMessaging=0
NrOfRXPDO=4
NrOfTXPDO=4
LSS_Supported=1

[DummyUsage]
Dummy0001=0

[Comments]
Lines=2
Line1=Implements CiA 447 J1939/CANopen gateway profile
Line2=Manufacturer extensions at 0x2000-0x3FFF

[DeviceComissioning]
NodeID=0x01
BaudRate=500

[Objects]
SupportedObjects=6
1=0x1000
2=0x1001
3=0x1018
4=0x5F00
5=0x5F01
6=0x5F20

[0x5F00]
ParameterName=J1939-to-CANopen mapping table
ObjectType=0x08
SubNumber=9

[0x5F00sub0]
ParameterName=Largest sub-index supported
ObjectType=0x07
DataType=0x0005
AccessType=ro
DefaultValue=0x08

[0x5F00sub1]
ParameterName=Number of active mappings
ObjectType=0x07
DataType=0x0005
AccessType=rw
DefaultValue=0

[0x5F00sub2]
ParameterName=Mapping entry 1
ObjectType=0x07
DataType=0x000F
AccessType=rw
DefaultValue=0x0000000000000000
```

---

## Summary

This chapter surveyed six specialised CANopen device profiles, each targeting a distinct
application domain but all built on the same CiA 301 foundation.

```
  Profile Quick-Reference
  ──────────────────────────────────────────────────────────────────────

  CiA 410  Inclinometers
           • Profile OD: 0x6000 (axis values), 0x6001 (status)
           • Unit: 0.01°, signed INT32
           • Key feature: configurable alarm thresholds

  CiA 412  Medical Devices
           • Stricter NMT transition rules
           • Mandatory heartbeat, mandatory identity object
           • Pre-operational is the safe default state

  CiA 418  Battery Systems
           • SoC / SoH / cell-level monitoring
           • Protection relay status in 0x6000 bit-field
           • Cell arrays at 0x6010 / 0x6020

  CiA 419  Battery Chargers
           • Paired with CiA 418 on battery side
           • Charging mode, V/I set-points via RPDO
           • Measured output via TPDO

  CiA 447  J1939 / CANopen Gateway
           • Mapping table at 0x5F00 / 0x5F01
           • 29-bit ↔ 11-bit CAN ID translation
           • J1939 address claiming managed autonomously
           • PGN-to-OD mapping: scale + offset per entry

  CiA 454  Energy Management
           • Power set-points (W / VAr)
           • Energy counters (Wh, VArh)
           • Grid mode management (grid-follow / grid-form)
           • Tariff and scheduling tables
```

**Key architectural principles:**

- All profiles share the same NMT, SDO, PDO, and EMCY mechanisms from CiA 301.
- Manufacturer-specific extensions belong exclusively in **0x2000 – 0x5FFF**.
- A node may combine profiles as long as their OD index ranges do not conflict.
- Always declare the implemented profile(s) correctly in **0x1000 (Device Type)**.
- PDO mapping should be pre-configured via EDS and only changed at runtime when truly
  necessary, to keep the system deterministic and easy to diagnose.
- The EDS file is the contract between device and configuration tool — keep it up to date.

By selecting the most appropriate standardised profile and confining extensions to the
manufacturer-specific area, new devices become interoperable with off-the-shelf masters,
configuration tools, and monitoring systems — without any custom driver development on the
system integrator's side.

---

*Document reference: Chapter 24 of the CANopen Programming Guide series.*
*Profiles described: CiA 410 (Ed. 3), CiA 412 (Ed. 2), CiA 418 (Ed. 3),*
*CiA 419 (Ed. 2), CiA 447 (Ed. 2), CiA 454 (Ed. 1).*