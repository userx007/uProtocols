# 04. CANopen Stack Architecture & Layer Model

- **CiA 301 Overview** — table of all major services (NMT, PDO, SDO, SYNC, EMCY, TIME, HB, LSS) with companion profile references.
- **OSI Layer Mapping** — full ASCII diagram showing where CAN (L1/L2) ends and CANopen begins (L3/L4/L7), with a note on the intentional gaps.
- **Four-Layer Architecture** — detailed ASCII stack diagram (Hardware → HAL → Stack → Application), including the RX dispatcher COB-ID range table and the predefined connection set.
- **Stack Initialisation Flow** — step-by-step ASCII flowchart from power-on through NMT `OPERATIONAL`, including the NMT state machine transitions.
- **Object Dictionary** — index range map, common CiA 301 objects table, and the C descriptor struct layout.
- **Stack Survey** — internal architecture ASCII diagrams for CANopenNode, lely-core, and CANfestival, plus a feature comparison table.
- **7 C/C++ Examples:**
  1. STM32 bxCAN HAL implementation
  2. CANopenNode v4 full initialisation
  3. OD read/write + SDO write callback with validation
  4. Runtime TPDO remapping (the 7-step sequence)
  5. SDO client — reading a remote node's serial number
  6. lely-core C++17 async coroutine (boot + SDO R/W + RPDO)
  7. CANfestival RPDO callback registration
- **Summary** — ASCII recap box with stack selection guide table.

---

> **Series:** CANopen Programming Guide
> **Standard:** CiA 301 v4.2
> **Audience:** Embedded / systems engineers working with C/C++

---

## Table of Contents

1. [Introduction](#introduction)
2. [CiA 301 Standard Overview](#cia-301-standard-overview)
3. [OSI Layer Mapping](#osi-layer-mapping)
4. [The Four-Layer Software Architecture](#the-four-layer-software-architecture)
   - [CAN Hardware / Physical Layer](#1-can-hardware--physical-layer)
   - [CAN Driver / HAL](#2-can-driver--hal)
   - [CANopen Stack](#3-canopen-stack)
   - [Application Layer](#4-application-layer)
5. [Stack Initialisation Flow](#stack-initialisation-flow)
6. [Object Dictionary — The Central Data Store](#object-dictionary--the-central-data-store)
7. [Survey of Popular Open-Source Stacks](#survey-of-popular-open-source-stacks)
   - [CANopenNode](#canopennode)
   - [lely-core](#lely-core)
   - [CANfestival](#canfestival)
8. [Porting a Stack to a New Target](#porting-a-stack-to-a-new-target)
9. [Programming Examples in C/C++](#programming-examples-in-cc)
10. [Summary](#summary)

---

## Introduction

CANopen is a higher-layer protocol (HLP) built on top of the Controller Area Network (CAN) bus, standardised by CAN in Automation (CiA) under the **CiA 301** specification. It defines a complete communication model—object dictionary, network management, process data, service data, and synchronisation—that turns a raw CAN bus into a deterministic, self-describing fieldbus suitable for industrial machinery, medical devices, elevators, maritime systems, and autonomous mobile robots.

Understanding the *architecture* of a CANopen stack—how software layers interact, who owns what responsibility, and where the application programmer hooks in—is the prerequisite for productive CANopen development and for porting existing open-source stacks to constrained MCU targets.

---

## CiA 301 Standard Overview

CiA 301 ("CANopen Application Layer and Communication Profile") is the core specification. Key provisions include:

| Topic | CiA 301 Provision |
|---|---|
| Network Management (NMT) | Master/slave state machine (Initialising → Pre-Operational → Operational → Stopped) |
| Object Dictionary (OD) | 16-bit index / 8-bit sub-index address space, 0x0000–0xFFFF |
| PDO (Process Data Object) | Real-time, producer/consumer, up to 8 bytes per CAN frame |
| SDO (Service Data Object) | Client/server configuration/access, segmented and block transfer |
| SYNC Object | Network-wide synchronisation broadcast |
| EMCY Object | Emergency messages with error register |
| TIME Object | Absolute timestamp distribution |
| Heartbeat / Node Guarding | Node health monitoring |
| LSS (Layer Setting Services) | Dynamic node-ID / bit-rate assignment (CiA 305) |

Companion profiles (CiA 402 for drives, CiA 406 for encoders, CiA 418 for battery chargers, etc.) sit *on top of* CiA 301 and extend the object dictionary with device-specific objects.

---

## OSI Layer Mapping

The classic OSI 7-layer model maps onto CAN + CANopen as shown below.

```
  OSI Model                  CAN / CANopen Mapping
  ┌─────────────────────┐    ┌──────────────────────────────────────────────┐
  │Layer 7 –Application │◄── │  CiA 301 Application Layer                   │
  │                     │    │  (NMT, PDO, SDO, SYNC, EMCY, TIME, HB)       │
  ├─────────────────────┤    ├──────────────────────────────────────────────┤
  │Layer 6 –Presentation│    │  Object Dictionary (index/sub-index encoding)│
  ├─────────────────────┤    ├──────────────────────────────────────────────┤
  │  Layer 5 – Session  │    │  (not explicitly defined; absorbed into L7)  │
  ├─────────────────────┤    ├──────────────────────────────────────────────┤
  │  Layer 4 – Transport│    │  SDO block/segmented transfer (fragmentation)│
  ├─────────────────────┤    ├──────────────────────────────────────────────┤
  │  Layer 3 – Network  │    │  CANopen COB-ID (11-bit CAN identifier)      │
  │                     │    │  Node-ID assignment (1–127)                  │
  ├─────────────────────┤    ├──────────────────────────────────────────────┤
  │  Layer 2 – Data Link│◄── │  ISO 11898-1  CAN Data Link Layer            │
  │                     │    │  (frame format, arbitration, ACK, CRC)       │
  ├─────────────────────┤    ├──────────────────────────────────────────────┤
  │  Layer 1 – Physical │◄── │  ISO 11898-2  CAN Physical Layer             │
  │                     │    │  (differential signalling, 120 Ω termination)│
  └─────────────────────┘    └──────────────────────────────────────────────┘

  NOTE: CAN skips Layers 3–5 by design; CANopen partially fills those gaps.
```

**Key architectural decisions enforced by this mapping:**

- CAN arbitration (L2) guarantees collision-free, priority-ordered transmission without a bus master — every node participates.
- CANopen's 11-bit COB-ID encodes *both* a function code (4 bits) *and* a node-ID (7 bits), acting as a lightweight network address (L3).
- PDO communication is connectionless (UDP-like); SDO communication is connection-oriented (TCP-like handshake).

---

## The Four-Layer Software Architecture

Real-world CANopen software is always structured as four cooperating layers. The strict separation of concerns is what enables portability across MCUs, RTOSes, and bus controllers.

```
  ╔══════════════════════════════════════════════════════════════════════╗
  ║                        APPLICATION LAYER                             ║
  ║  User process logic, device profile objects, event handling          ║
  ║  e.g. motor control loop, sensor fusion, HMI state machine           ║
  ╠══════════════════════════════════════════════════════════════════════╣
  ║                        CANopen STACK                                 ║
  ║  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐    ║
  ║  │  NMT     │ │  PDO     │ │  SDO     │ │  SYNC    │ │  EMCY    │    ║
  ║  │ manager  │ │ engine   │ │ server/  │ │ producer/│ │ handler  │    ║
  ║  │          │ │          │ │ client   │ │ consumer │ │          │    ║
  ║  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘    ║
  ║  ┌───────────────────────────────────────────────────────────────┐   ║
  ║  │               Object Dictionary (OD) Engine                   │   ║
  ║  │  Index/sub-index lookup, type checking, callback dispatch     │   ║
  ║  └───────────────────────────────────────────────────────────────┘   ║
  ╠══════════════════════════════════════════════════════════════════════╣
  ║                    CAN DRIVER / HAL                                  ║
  ║  canOpen_send()  canOpen_recv()  canOpen_setBitrate()                ║
  ║  Portable API — same signature on every target                       ║
  ╠══════════════════════════════════════════════════════════════════════╣
  ║               CAN HARDWARE / PERIPHERAL                              ║
  ║  STM32 bxCAN / FDCAN   MCP2515 SPI   PEAK PCAN   SocketCAN (Linux)   ║
  ╚══════════════════════════════════════════════════════════════════════╝

  Arrows:   Application ──► OD ──► Stack Services ──► HAL ──► Hardware
            Hardware    ──► HAL callbacks ──► Stack RX dispatcher ──► App
```

### 1. CAN Hardware / Physical Layer

The bottom layer is the silicon: an on-chip CAN peripheral (STM32 bxCAN, NXP FlexCAN, Renesas RSCAN) or an external SPI-connected controller (Microchip MCP2515 / MCP2518FD). Its job is solely to:

- Accept/reject frames based on hardware acceptance filters.
- Signal Tx complete / Rx available via interrupt or polled register.
- Report bus-off, error-passive, and error-warning states.

Nothing above this layer is CAN-controller-specific.

### 2. CAN Driver / HAL

The Hardware Abstraction Layer is the **portability seam**. Every CANopen stack defines a small set of functions that must be implemented for a new target. Typical HAL interface:

```c
/* Canonical CANopen HAL interface (target-specific implementation) */

typedef struct {
    uint32_t id;        /* 11-bit COB-ID (standard frame)          */
    uint8_t  dlc;       /* Data Length Code, 0–8                    */
    uint8_t  data[8];   /* Payload                                  */
    uint8_t  rtr;       /* Remote Transmission Request flag         */
} CAN_Frame_t;

/* Blocking or interrupt-driven transmit */
int  CAN_HAL_send(const CAN_Frame_t *frame);

/* Poll or ISR-driven receive; returns 1 if frame available */
int  CAN_HAL_recv(CAN_Frame_t *frame);

/* Set bit-rate (called during LSS or boot) */
int  CAN_HAL_setBitrate(uint32_t bitrate_bps);

/* Enable/disable the CAN controller */
int  CAN_HAL_init(uint8_t node_id, uint32_t bitrate_bps);
void CAN_HAL_deinit(void);

/* Bus status inquiry */
int  CAN_HAL_getBusState(void);   /* returns: OK / WARN / PASSIVE / BUSOFF */
```

Keeping this interface minimal (≈ 6 functions) means porting costs are predictable and bounded.

### 3. CANopen Stack

The stack is the protocol engine. It consumes raw CAN frames from the HAL and dispatches them to the appropriate sub-module:

```
  Incoming CAN frame
         │
         ▼
  ┌─────────────────────────────────────────────┐
  │           RX Dispatcher                     │
  │  COB-ID range lookup:                       │
  │   0x000       → NMT command                 │
  │   0x080       → SYNC / EMCY (node 0)        │
  │   0x081–0x0FF → EMCY (nodes 1–127)          │
  │   0x100       → TIME                        │
  │   0x180–0x1FF → TPDO1  (nodes 1–127)        │
  │   0x200–0x27F → RPDO1                       │
  │   0x280–0x2FF → TPDO2                       │
  │   ...                                       │
  │   0x580–0x5FF → SDO response (server→client)│
  │   0x600–0x67F → SDO request  (client→server)│
  │   0x700–0x77F → Heartbeat / Node Guard      │
  └────────────┬────────────────────────────────┘
               │
    ┌──────────▼──────────┐
    │  Route to sub-module│
    └──────────┬──────────┘
               │
    ┌──────────┴──────────┐
    │                     │
  NMT    PDO    SDO    SYNC    EMCY    HB/NG
```

**COB-ID predefined connection set (CiA 301, §7.3.3):**

```
  Function Code  COB-ID base   Usage
  ─────────────  ───────────   ──────────────────────────────
  0000           0x000         NMT (broadcast, no node-ID)
  0001           0x080         SYNC
  0010           0x100         TIME
  0001+node      0x080+n       EMCY  (n = node-ID 1–127)
  0011+node      0x180+n       TPDO1
  0100+node      0x200+n       RPDO1
  0101+node      0x280+n       TPDO2
  0110+node      0x300+n       RPDO2
  0111+node      0x380+n       TPDO3
  1000+node      0x400+n       RPDO3
  1001+node      0x480+n       TPDO4
  1010+node      0x500+n       RPDO4
  1011+node      0x580+n       SDO (server→client response)
  1100+node      0x600+n       SDO (client→server request)
  1110+node      0x700+n       Heartbeat
```

### 4. Application Layer

The application layer is the user's code. It interacts with the CANopen stack exclusively through the **Object Dictionary API** and a handful of event callbacks. It should have zero knowledge of CAN frame encoding.

```c
/* Application only calls high-level stack APIs */

/* Read a value from the local OD */
uint32_t speed = OD_get_u32(OD_INDEX_SPEED, 0x00);

/* Write a value and trigger a TPDO if mapped */
OD_set_u32(OD_INDEX_SPEED, 0x00, new_speed, /*notify=*/true);

/* Initiate an SDO read from a remote node */
SDO_readRemote(node_id, 0x6040, 0x00, callback_fn, timeout_ms);

/* Send an emergency */
EMCY_send(EMCY_CODE_VOLTAGE_HIGH, 0x00, extra_info);
```

---

## Stack Initialisation Flow

The sequence below covers everything from power-on to the transition into Operational state, which is the normal state where PDOs are exchanged.

```
  Power-on / Reset
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  1. Board / MCU Init                                   │
  │     - Clock tree, GPIO, NVIC                           │
  │     - CAN peripheral registers, TX/RX FIFO config      │
  └────────────────────────────────────────────────────────┘
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  2. CAN HAL Init                                       │
  │     CAN_HAL_init(node_id, CAN_500KBPS)                 │
  │     - Set bit-timing registers                         │
  │     - Enable Rx interrupt / DMA                        │
  └────────────────────────────────────────────────────────┘
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  3. Object Dictionary Init                             │
  │     OD_init()                                          │
  │     - Load default values (ROM / flash)                │
  │     - Load non-volatile values (EEPROM / flash page)   │
  │     - Register SDO write callbacks                     │
  └────────────────────────────────────────────────────────┘
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  4. CANopen Stack Init                                 │
  │     CANopen_init(node_id, &od, &hal_ops)               │
  │     - NMT state machine → INITIALISING                 │
  │     - PDO mapping tables parsed from OD 0x1400–0x1BFF  │
  │     - SDO server enabled (COB-ID 0x600+node_id)        │
  │     - Heartbeat producer timer configured              │
  └────────────────────────────────────────────────────────┘
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  5. Boot-up message transmitted                        │
  │     TX: COB-ID 0x700+node_id, data = 0x00              │
  │     NMT state → PRE-OPERATIONAL                        │
  └────────────────────────────────────────────────────────┘
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  6. (Optional) LSS / Node-ID assignment                │
  │     Only if node_id not yet assigned (node_id = 0xFF)  │
  └────────────────────────────────────────────────────────┘
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  7. NMT Master sends "Start Remote Node"               │
  │     RX: COB-ID 0x000, data = [0x01, 0x00 or node_id]   │
  │     NMT state → OPERATIONAL                            │
  │     PDO communication enabled                          │
  └────────────────────────────────────────────────────────┘
       │
       ▼
  ┌────────────────────────────────────────────────────────┐
  │  8. Main loop / RTOS task                              │
  │     CANopen_process(elapsed_ms)   ← call periodically  │
  │     App_process()                                      │
  └────────────────────────────────────────────────────────┘

  NMT State Transitions:
  ┌────────────────┐   Boot-up    ┌──────────────────┐
  │ INITIALISING   │ ──────────►  │ PRE-OPERATIONAL  │
  └────────────────┘              └────────┬─────────┘
                                           │ NMT Start
                                           ▼
                                 ┌──────────────────┐
                            ┌─── │   OPERATIONAL    │ ───┐
                            │    └──────────────────┘    │ NMT Stop
                            │ NMT Reset                  ▼
                            │                    ┌──────────────────┐
                            └──────────────────► │    STOPPED       │
                                                 └──────────────────┘
```

---

## Object Dictionary — The Central Data Store

The OD is the cornerstone of CANopen. Every piece of configuration, process data mapping, and device state is an OD entry addressed by `(index, sub-index)`.

```
  Index Range       Category
  ───────────────   ───────────────────────────────────────────────────
  0x0001–0x001F     Data types (static definitions, not instantiated)
  0x1000–0x1FFF     Communication profile objects (CiA 301)
  0x2000–0x5FFF     Manufacturer-specific objects
  0x6000–0x9FFF     Standardised device profile objects (CiA 4xx)
  0xA000–0xBFFF     Standardised network variable objects
  0xC000–0xFFFF     Reserved

  Common CiA 301 Communication Objects:
  ─────────────────────────────────────
  0x1000  Device type
  0x1001  Error register
  0x1017  Heartbeat producer time [ms]
  0x1018  Identity object (vendor, product, revision, serial)
  0x1200  SDO server parameter
  0x1400  RPDO1 communication parameter  (COB-ID, transmission type)
  0x1600  RPDO1 mapping parameter        (what OD entries are mapped)
  0x1800  TPDO1 communication parameter
  0x1A00  TPDO1 mapping parameter
  ... up to RPDO4 (0x1403/0x1603) and TPDO4 (0x1803/0x1A03)

  OD Entry (Index=0x1017, Sub=0x00):
  ┌──────────┬──────────┬─────────┬────────────┬──────────────────────┐
  │  Index   │ Sub-idx  │  Type   │  Access    │  Value               │
  │  0x1017  │  0x00    │  U16    │  RW        │  1000  (1 s HB)      │
  └──────────┴──────────┴─────────┴────────────┴──────────────────────┘
```

In C, the OD is typically a static array of descriptor structs:

```c
/* Object Dictionary entry descriptor */
typedef struct {
    uint16_t    index;
    uint8_t     sub_index;
    uint8_t     type;           /* CO_OBJ_TYPE_U8, _U16, _U32, _DOMAIN … */
    uint16_t    attr;           /* CO_OBJ_ATTR_RW, _RD, _TPDO_MAP … */
    void       *data;           /* pointer to the actual variable */
    uint32_t    size;           /* byte size of *data */
    OD_cb_t     write_cb;       /* optional callback on SDO write */
} OD_Entry_t;
```

---

## Survey of Popular Open-Source Stacks

### CANopenNode

**Repository:** https://github.com/CANopenNode/CANopenNode
**Language:** C99
**Licence:** Apache 2.0

CANopenNode is the most widely adopted open-source CANopen stack for deeply embedded targets. Its architecture is explicitly layered:

```
  CANopenNode Internal Architecture
  ═══════════════════════════════════════════════════════════════
  CANopen.h               ← top-level init / process API
  ┌───────────┬───────────┬──────────┬───────────┬────────────┐
  │  CO_NMT   │  CO_PDO   │  CO_SDO  │  CO_SYNC  │  CO_EMCY   │
  └─────┬─────┴─────┬─────┴────┬─────┴─────┬─────┴─────┬──────┘
        │           │          │           │           │
  ┌─────▼───────────▼──────────▼───────────▼───────────▼──────┐
  │               CO_OD  (Object Dictionary engine)           │
  └───────────────────────────┬───────────────────────────────┘
                              │
  ┌───────────────────────────▼────────────────────────────────┐
  │               CANopen_target.c  (HAL implementation)       │
  │  CO_CANmodule_init / CO_CANsend / CO_CANrxBufferInit …     │
  └───────────────────────────┬────────────────────────────────┘
                              │
                       CAN Hardware
```

Key design points:

- The OD is generated from an **EDS file** (Electronic Data Sheet) using the `CANopenEditor` tool, producing `OD.c` / `OD.h`.
- All state (CO_t struct) is allocated by the application — no hidden globals, enabling multiple instances on the same CPU.
- Processing is split between a **time-critical interrupt context** (`CO_CANinterrupt()`) and a **main-loop context** (`CO_process()`).

### lely-core

**Repository:** https://github.com/lely-industries/lely-core
**Language:** C11 + C++17 wrappers
**Licence:** Apache 2.0

lely-core is a mature, POSIX-oriented stack suited for Linux-based gateways, PLCs, and AUTOSAR-inspired architectures.

```
  lely-core Architecture (simplified)
  ═══════════════════════════════════════════════════════════════════
  ┌───────────────────────────────────────────────────────────────┐
  │  C++17 Coroutine / Fiber API  (co::Node, co::Master, …)       │
  │  Asynchronous SDO, LSS, NMT management                        │
  ├───────────────────────────────────────────────────────────────┤
  │  C library: libco  (NMT, PDO, SDO, SYNC, EMCY, LSS, TIME)     │
  ├───────────────────────────────────────────────────────────────┤
  │  libcan  (SocketCAN back-end, virtual CAN, PEAK)              │
  ├───────────────────────────────────────────────────────────────┤
  │  libevent-style I/O loop (liblely-io)                         │
  └───────────────────────────────────────────────────────────────┘
```

Notable features:

- Full **CANopen master** with SDO manager, LSS master, and NMT master.
- DCF (Device Configuration File) support for automated network bring-up.
- C++17 awaitable wrappers eliminate callback spaghetti.
- **Not** suitable for bare-metal MCUs with < 64 KB RAM.

### CANfestival

**Repository:** https://github.com/nucleron/canfestival-3 (community mirror)
**Language:** C89/C99
**Licence:** LGPL 2.1+

CANfestival is one of the earliest open-source CANopen stacks and remains popular in legacy embedded Linux and RTOS (FreeRTOS, RTAI) projects.

```
  CANfestival Architecture
  ═══════════════════════════════════════════════════════════════
  ┌───────────────────────────────────────────────────────────┐
  │  objdict.c / objdict.h  (OD generated by ObjectDict GUI)  │
  ├───────────────────────────────────────────────────────────┤
  │  canfestival/  : nmt.c  pdo.c  sdo.c  sync.c  emcy.c      │
  ├───────────────────────────────────────────────────────────┤
  │  drivers/  (target-specific): canfestival_[target].c      │
  │  canSend()  setTimer()  getElapsedTime()                  │
  └───────────────────────────────────────────────────────────┘
```

Strengths: battle-tested, small footprint, straightforward OD code generation.
Weaknesses: LGPL licence, no SDO block transfer, no LSS master, aging codebase.

**Comparison table:**

```
  Feature               CANopenNode     lely-core       CANfestival
  ──────────────────    ───────────     ──────────      ───────────
  Licence               Apache 2.0      Apache 2.0      LGPL 2.1
  Language              C99             C11/C++17       C89
  Bare-metal MCU        Excellent       No (POSIX)      Good
  Linux / RTOS          Good            Excellent       Good
  NMT Master            Yes             Yes             Partial
  SDO Block Transfer    Yes             Yes             No
  LSS Support           Yes             Yes             No
  C++ Async API         No              Yes             No
  EDS/DCF tooling       Yes             Yes             Yes (GUI)
  Active maintenance    Yes (2024)      Yes (2024)      Sporadic
  Typical flash usage   ~20–40 KB       ~200+ KB        ~15–25 KB
```

---

## Porting a Stack to a New Target

Porting reduces to implementing the HAL. For CANopenNode the interface lives in `CO_driver.h`:

```
  Porting Checklist
  ─────────────────────────────────────────────────────────────────
  Step  File to create           What to implement
  ────  ─────────────────────    ─────────────────────────────────
   1    CO_driver_target.h       Define CO_CANrxMsg_t, CO_CANtx_t
   2    CO_driver.c              CO_CANmodule_init()
                                 CO_CANmodule_disable()
                                 CO_CANrxBufferInit()
                                 CO_CANsend()
                                 CO_CANclearPendingSync()
                                 CO_CANverifyErrors()
   3    main.c                   Call CO_CANinterrupt() from CAN ISR
                                 Call CO_process() from main loop
   4    OD.c / OD.h              Generated by CANopenEditor from EDS
  ─────────────────────────────────────────────────────────────────
```

---

## Programming Examples in C/C++

### Example 1 — Minimal HAL for STM32 bxCAN (C99)

```c
/* file: CO_driver_stm32.c
 * Target: STM32F4xx with bxCAN peripheral
 * HAL: STM32 HAL (CubeMX generated)
 */

#include "CANopen.h"
#include "stm32f4xx_hal.h"

extern CAN_HandleTypeDef hcan1;   /* generated by CubeMX */

/* ── CO_CANmodule_init ─────────────────────────────────────────── */
CO_ReturnError_t CO_CANmodule_init(
    CO_CANmodule_t   *CANmodule,
    void             *CANptr,          /* &hcan1 */
    CO_CANrx_t        rxArray[],
    uint16_t          rxSize,
    CO_CANtx_t        txArray[],
    uint16_t          txSize,
    uint16_t          CANbitRate)
{
    CANmodule->CANptr         = CANptr;
    CANmodule->rxArray        = rxArray;
    CANmodule->rxSize         = rxSize;
    CANmodule->txArray        = txArray;
    CANmodule->txSize         = txSize;
    CANmodule->CANnormal      = false;
    CANmodule->useCANrxFilters = false;  /* software filtering */
    CANmodule->bufferInhibitFlag  = false;
    CANmodule->firstCANtxMessage  = true;
    CANmodule->CANtxCount         = 0;
    CANmodule->errOld             = 0;

    /* Bit-rate is configured via CubeMX / prescaler, not at runtime here */
    (void)CANbitRate;

    /* Start CAN peripheral */
    if (HAL_CAN_Start((CAN_HandleTypeDef *)CANptr) != HAL_OK) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }
    /* Enable RX FIFO0 message pending interrupt */
    HAL_CAN_ActivateNotification((CAN_HandleTypeDef *)CANptr,
                                 CAN_IT_RX_FIFO0_MSG_PENDING);
    return CO_ERROR_NO;
}

/* ── CO_CANsend ────────────────────────────────────────────────── */
CO_ReturnError_t CO_CANsend(CO_CANmodule_t *CANmodule,
                             CO_CANtx_t     *buffer)
{
    CAN_HandleTypeDef *hcan = (CAN_HandleTypeDef *)CANmodule->CANptr;
    CAN_TxHeaderTypeDef hdr = {
        .StdId  = buffer->ident,
        .IDE    = CAN_ID_STD,
        .RTR    = CAN_RTR_DATA,
        .DLC    = buffer->DLC,
        .TransmitGlobalTime = DISABLE
    };
    uint32_t txMailbox;

    CO_LOCK_CAN_SEND(CANmodule);
    if (HAL_CAN_AddTxMessage(hcan, &hdr, buffer->data, &txMailbox) != HAL_OK) {
        /* All mailboxes full — queue for retry in CO_CANverifyErrors */
        CANmodule->CANtxCount++;
        CO_UNLOCK_CAN_SEND(CANmodule);
        return CO_ERROR_TX_BUSY;
    }
    CO_UNLOCK_CAN_SEND(CANmodule);
    return CO_ERROR_NO;
}

/* ── CAN RX ISR (called from HAL_CAN_RxFifo0MsgPendingCallback) ── */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CO_CANrxMsg_t      rcvMsg;
    CAN_RxHeaderTypeDef rxHdr;

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0,
                             &rxHdr, rcvMsg.data) != HAL_OK) {
        return;
    }
    rcvMsg.ident = (uint16_t)rxHdr.StdId;
    rcvMsg.DLC   = (uint8_t)rxHdr.DLC;

    /* Dispatch to CANopenNode interrupt handler */
    CO_CANinterrupt(CO->CANmodule, &rcvMsg);
}
```

---

### Example 2 — Stack Initialisation (C99, CANopenNode v4)

```c
/* file: main.c — CANopenNode v4 initialisation sequence */

#include "CANopen.h"
#include "OD.h"           /* generated by CANopenEditor  */

#define NODE_ID     0x01u
#define CAN_BITRATE 500u  /* kbps */

/* Static allocation — no heap required */
static CO_t          CO_obj;
static CO_CANrx_t    rxBuf[CO_RXCAN_NO_MSGS];
static CO_CANtx_t    txBuf[CO_TXCAN_NO_MSGS];

CO_t *CO = &CO_obj;   /* global pointer used by CANopenNode internals */

int main(void)
{
    CO_ReturnError_t err;
    CO_NMT_reset_cmd_t reset_cmd = CO_RESET_NOT;
    uint32_t heap_mem_used = 0;

    /* 1. Board init */
    BSP_SystemClockConfig();
    BSP_CAN_PinConfig();

    /* 2. CANopen first init */
    err = CO_CANinit(CO, &hcan1, CAN_BITRATE);
    if (err != CO_ERROR_NO) { Error_Handler(); }

    err = CO_CANopenInit(CO,
                         NULL,          /* CO_config_t: use defaults */
                         &OD,           /* generated Object Dictionary */
                         OD_STATUS_BITS,
                         NODE_ID,
                         CAN_BITRATE,
                         rxBuf, CO_RXCAN_NO_MSGS,
                         txBuf, CO_TXCAN_NO_MSGS,
                         &heap_mem_used);
    if (err != CO_ERROR_NO) { Error_Handler(); }

    /* 3. Configure first/error boot-up */
    CO->NMT->operatingStatePrev = CO_NMT_PRE_OPERATIONAL;

    /* 4. Enable CAN */
    CO_CANsetNormalMode(CO->CANmodule);

    /* 5. Main loop */
    while (1) {
        uint32_t now_ms = HAL_GetTick();
        static uint32_t prev_ms = 0;
        uint32_t elapsed_ms = now_ms - prev_ms;
        prev_ms = now_ms;

        /* Process stack (NMT, HB, PDO timers, SDO timeout) */
        reset_cmd = CO_process(CO, false, elapsed_ms, NULL);

        if (reset_cmd == CO_RESET_COMM) {
            /* Reinitialise communication objects only */
            CO_CANopenInitPDO(CO, CO->em, &OD, NODE_ID, &err);
            CO_CANsetNormalMode(CO->CANmodule);
        } else if (reset_cmd == CO_RESET_APP) {
            NVIC_SystemReset();  /* full device reset */
        }

        /* Application process */
        App_process(elapsed_ms);
    }
}
```

---

### Example 3 — Object Dictionary Access and SDO Write Callback (C99)

```c
/* Demonstrating OD read/write and an SDO write callback */

#include "CANopen.h"
#include "OD.h"

/* ── Read a manufacturer-specific U32 (index 0x2001) ─────────────── */
uint32_t App_readTargetSpeed(void)
{
    uint32_t speed = 0;
    OD_entry_t *entry = OD_find(&OD, 0x2001u);
    if (entry == NULL) { return 0; }

    OD_IO_t io;
    OD_getSub(entry, 0x00, &io, false);
    OD_size_t bytes_read = 0;
    io.read(&io.stream, &speed, sizeof(speed), &bytes_read);
    return speed;
}

/* ── Write a value and flag a TPDO as changed ─────────────────────── */
void App_setActualSpeed(uint32_t rpm)
{
    OD_entry_t *entry = OD_find(&OD, 0x2002u);
    if (entry == NULL) { return; }

    OD_IO_t io;
    OD_getSub(entry, 0x00, &io, false);
    OD_size_t written = 0;
    io.write(&io.stream, &rpm, sizeof(rpm), &written);

    /* Mark TPDO1 as containing new data (triggers event-driven send) */
    OD_requestTPDO(CO->TPDOcomm[0], CO->TPDOparam[0]);
}

/* ── SDO write callback — invoked when remote node writes 0x2001 ──── */
static ODR_t onTargetSpeedWrite(OD_stream_t *stream,
                                const void  *buf,
                                OD_size_t    count,
                                OD_size_t   *countWritten)
{
    /* Let the stack do the actual write first */
    ODR_t ret = OD_writeOriginal(stream, buf, count, countWritten);
    if (ret != ODR_OK) { return ret; }

    /* Post-write hook: validate range */
    uint32_t new_speed;
    memcpy(&new_speed, buf, sizeof(new_speed));
    if (new_speed > 5000u) {
        /* Reject with SDO abort code 0x06090031 (value too high) */
        return ODR_MAX_LESS_THAN_VALUE;
    }
    Motor_setTargetRPM(new_speed);
    return ODR_OK;
}

/* Register callback during OD init */
void OD_initExtensions(void)
{
    OD_entry_t *e = OD_find(&OD, 0x2001u);
    OD_extension_t *ext = /* statically allocated */ &speed_ext;
    ext->write = onTargetSpeedWrite;
    OD_extensionIO_init(e, ext);
}
```

---

### Example 4 — PDO Configuration at Runtime (C99)

PDOs can be remapped at runtime by writing to the OD communication/mapping parameter objects via SDO, or directly in code:

```c
/*
 * Reconfigure TPDO1 to transmit:
 *   sub 1 → OD 0x6041 sub 0 (statusword, U16)  — CiA 402
 *   sub 2 → OD 0x6064 sub 0 (position actual,  I32)
 *   Total: 2 + 4 = 6 bytes
 *
 * Must be done in PRE-OPERATIONAL state.
 */
void App_configurTPDO1(void)
{
    OD_entry_t *comm = OD_find(&OD, 0x1800u);  /* TPDO1 comm param */
    OD_entry_t *map  = OD_find(&OD, 0x1A00u);  /* TPDO1 mapping    */

    /* Step 1: Disable TPDO by setting MSB of COB-ID */
    uint32_t cob_id_disabled = (0x180u + NODE_ID) | 0x80000000u;
    OD_set_u32(comm, 0x01, cob_id_disabled, false);

    /* Step 2: Set number of mapped objects to 0 (mandatory before remapping) */
    OD_set_u8(map, 0x00, 0, false);

    /* Step 3: Write mapping entries
     * Encoding: bits 31-16 = index, bits 15-8 = sub, bits 7-0 = bit-length */
    OD_set_u32(map, 0x01, 0x60410010u, false);  /* 0x6041:00, 16 bits */
    OD_set_u32(map, 0x02, 0x60640020u, false);  /* 0x6064:00, 32 bits */

    /* Step 4: Restore number of mapped objects */
    OD_set_u8(map, 0x00, 2, false);

    /* Step 5: Re-enable TPDO */
    uint32_t cob_id_enabled = 0x180u + NODE_ID;
    OD_set_u32(comm, 0x01, cob_id_enabled, false);

    /* Step 6: Set transmission type (0xFE = event-driven, 0xFF = synchronous) */
    OD_set_u8(comm, 0x02, 0xFEu, false);

    /* Step 7: Set event timer 10 ms (0x0A) if desired */
    OD_set_u16(comm, 0x05, 10u, false);
}
```

---

### Example 5 — SDO Client: Reading a Remote Node's Object (C99)

```c
/*
 * Read OD 0x1018:04 (serial number) from remote node 0x05 via SDO.
 * Uses CANopenNode v4 SDO client (blocking helper for demonstration).
 */
#include "CO_SDOclient.h"

static volatile bool  sdo_done  = false;
static volatile ODR_t sdo_result = ODR_OK;

static void sdo_transfer_cb(void *arg, ODR_t result)
{
    (void)arg;
    sdo_result = result;
    sdo_done   = true;
}

uint32_t App_getRemoteSerialNumber(uint8_t remote_node_id)
{
    uint32_t serial = 0;
    CO_SDOclient_t *sdoc = CO->SDOclient;  /* pre-allocated in CO init */

    /* Point SDO client at the remote node */
    CO_SDOclient_setup(sdoc,
                       CO_CAN_ID_SDO_CLI + remote_node_id,  /* 0x600+n */
                       CO_CAN_ID_SDO_SRV + remote_node_id,  /* 0x580+n */
                       remote_node_id);

    /* Initiate expedited upload (read) */
    sdo_done = false;
    CO_SDOclientUploadInitiate(sdoc, 0x1018u, 0x04u,
                               &serial, sizeof(serial),
                               1000u,       /* timeout ms */
                               false,       /* not block transfer */
                               sdo_transfer_cb, NULL);

    /* Spin-wait (in real code, handle in process loop) */
    uint32_t start = HAL_GetTick();
    while (!sdo_done && (HAL_GetTick() - start < 1100u)) {
        CO_SDOclientUploadInProgress(sdoc, 1u, NULL, NULL);
    }

    if (sdo_result != ODR_OK) {
        Log_error("SDO read failed: %d\n", sdo_result);
        return 0;
    }
    return serial;
}
```

---

### Example 6 — lely-core C++ Async SDO (C++17)

```cpp
// Asynchronous SDO read using lely-core coroutines.
// Demonstrates how the C++17 wrapper eliminates callback nesting.

#include <lely/coapp/master.hpp>
#include <lely/coapp/fiber_driver.hpp>
#include <lely/ev/loop.hpp>
#include <iostream>

using namespace lely;

class MyMaster : public canopen::FiberMaster {
public:
    using FiberMaster::FiberMaster;

protected:
    void OnStart() override {
        // This coroutine runs on a fiber — can co_await async ops
        Boot(5, std::chrono::seconds(5));   // boot slave node 5, 5s timeout

        // Asynchronous SDO read: index=0x1018, sub=0x04
        uint32_t serial = Wait(AsyncRead<uint32_t>(5, 0x1018, 0x04));
        std::cout << "Node 5 serial number: 0x"
                  << std::hex << serial << "\n";

        // Asynchronous SDO write: set heartbeat producer time to 500 ms
        Wait(AsyncWrite<uint16_t>(5, 0x1017, 0x00, 500u));

        // Start PDO exchange
        Start(5);
    }

    void OnRpdoWrite(uint8_t id, uint16_t idx, uint8_t subidx) override {
        if (id == 5 && idx == 0x6041 && subidx == 0) {
            uint16_t statusword = rpdo_mapped<uint16_t>(5, idx, subidx);
            std::cout << "Node 5 statusword: 0x"
                      << std::hex << statusword << "\n";
        }
    }
};

int main()
{
    ev::Loop loop;
    io::CanController ctrl("can0");          // SocketCAN interface
    io::CanChannel   chan(loop.get_poll(), loop.get_exec());
    chan.open(ctrl);

    MyMaster master(loop.get_timer(), chan,
                    "master.dcf",            // DCF with network config
                    "master.bin");           // compiled OD
    loop.run();
    return 0;
}
```

---

### Example 7 — CANfestival: Registering a PDO Callback (C99)

```c
/*
 * CANfestival: callback invoked each time RPDO1 is received.
 * The OD variable is updated automatically by the stack;
 * this callback notifies the application.
 */
#include "canfestival.h"
#include "objdict.h"    /* generated by ObjectDict GUI */

/* Called by CANfestival stack after each RPDO write */
void OnRPDO1Receive(CO_Data *d, UNS8 nodeId)
{
    (void)d;
    (void)nodeId;

    /* OD variable directly accessible by symbol from objdict.h */
    UNS16 controlword = ControlWord;    /* OD 0x6040:00 — mapped in RPDO1 */
    UNS32 target_pos  = TargetPosition; /* OD 0x607A:00 — mapped in RPDO1 */

    Motor_applyCommand(controlword, target_pos);
}

/* Registration during init */
void App_initCANfestival(void)
{
    /* Set node-ID and bit-rate in the OD */
    setNodeId(&ObjDict_Data, 0x03u);

    /* Register the RPDO callback */
    RegisterSetODentryCallBack(&ObjDict_Data,
                               0x6040u, 0x00u,
                               &OnRPDO1Receive);
    /* Start the node */
    setState(&ObjDict_Data, Initialisation);
}
```

---

## Summary

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                   CHAPTER 4 — KEY TAKEAWAYS                     │
  ├─────────────────────────────────────────────────────────────────┤
  │                                                                 │
  │  CiA 301  defines the complete CANopen application layer:       │
  │  NMT, OD, PDO, SDO, SYNC, EMCY, TIME, and heartbeat.            │
  │                                                                 │
  │  OSI Mapping                                                    │
  │  L1+L2 → CAN hardware + ISO 11898                               │
  │  L3     → COB-ID (function code + node-ID)                      │
  │  L4     → SDO block/segmented transfer                          │
  │  L7     → CANopen services                                      │
  │                                                                 │
  │  Four software layers (bottom → top):                           │
  │   Hardware  →  HAL (≈6 functions)  →  Stack  →  Application     │
  │  The HAL is the only target-specific code.                      │
  │                                                                 │
  │  Object Dictionary is the central data store.                   │
  │  Every config value, process datum, and device state lives      │
  │  at a unique (index, sub-index) address.                        │
  │  The application interacts only via OD APIs.                    │
  │                                                                 │
  │  Initialisation sequence:                                       │
  │   Board init → HAL init → OD init → Stack init →                │
  │   Boot-up TX → Pre-Op → (NMT Start) → Operational               │
  │                                                                 │
  │  Open-source stack selection guide:                             │
  │   ┌──────────────────┬────────────────────────────────────┐     │
  │   │ Target           │ Recommended stack                  │     │
  │   ├──────────────────┼────────────────────────────────────┤     │
  │   │ Bare-metal MCU   │ CANopenNode (Apache 2.0, C99)      │     │
  │   │ Linux gateway    │ lely-core   (Apache 2.0, C++17)    │     │
  │   │ Legacy RTOS      │ CANfestival (LGPL, C89)            │     │
  │   └──────────────────┴────────────────────────────────────┘     │
  │                                                                 │
  │  Porting cost = implementing HAL (~6 functions).                │
  │  OD code is generated from EDS/DCF files by tooling.            │
  │                                                                 │
  └─────────────────────────────────────────────────────────────────┘
```

### Further Reading

- **CiA 301 v4.2** — "CANopen Application Layer and Communication Profile" (CiA membership required)
- **CiA 305** — "CANopen Layer Setting Services (LSS) and Protocols"
- **CiA 402** — "CANopen device profile for drives and motion control"
- **CANopenNode documentation:** https://github.com/CANopenNode/CANopenNode/wiki
- **lely-core documentation:** https://opensource.lely.com/canopen/docs/
- *"The CANopen Book"* — Wolfgang Pfeiffer, VDE Verlag, ISBN 978-3800730759

---

*Document: 04_CANopen_Stack_Architecture_and_Layer_Model.md — CANopen Programming Guide Series*# CANopen Stack Architecture & Layer Model

