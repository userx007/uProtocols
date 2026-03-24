# 58. CANaerospace Standard

> **Lightweight protocol for avionics and aerospace applications with standardized message identifiers.**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Historical Background](#historical-background)
3. [Architecture Overview](#architecture-overview)
4. [Message Structure](#message-structure)
5. [Node Services](#node-services)
6. [Data Type Codes (DTC)](#data-type-codes-dtc)
7. [Standard Message Identifiers](#standard-message-identifiers)
8. [Programming in C/C++](#programming-in-cc)
9. [Programming in Rust](#programming-in-rust)
10. [Error Handling and Redundancy](#error-handling-and-redundancy)
11. [Certification and Safety Considerations](#certification-and-safety-considerations)
12. [Summary](#summary)

---

## Introduction

**CANaerospace** is a lightweight, open communication protocol designed specifically for avionics and aerospace applications. Developed by Stock Flight Systems (originally published as a public domain standard in 1998 by Michael Stock), it operates over the CAN bus (Controller Area Network) and defines a standardized framework for message identifiers, data types, and node services applicable to aircraft systems.

Unlike heavy industrial protocols such as CANopen or DeviceNet, CANaerospace was purpose-built for the stringent requirements of flight-critical applications: determinism, low latency, minimal overhead, and compatibility with airworthiness certification standards (DO-178C, DO-254, ARP4754A).

Key characteristics:
- Operates on standard CAN 2.0A (11-bit identifiers) and CAN 2.0B (29-bit identifiers)
- Defines over 1,000 standardized message IDs for common avionics parameters
- Supports both normal operation (NOD) and emergency operation (EED) data
- Provides node services for configuration, identification, and synchronization
- Designed for redundant dual-bus architectures common in aerospace

---

## Historical Background

The CAN bus was originally developed by Bosch in the early 1980s for automotive applications. Its inherent properties — multi-master capability, non-destructive bitwise arbitration, robust error detection, and fault confinement — made it attractive for aerospace use as well.

CANaerospace was formally introduced in the late 1990s as an answer to the lack of an aerospace-specific application layer for CAN. It has since been adopted in:
- General aviation (small aircraft, helicopters)
- Unmanned aerial vehicles (UAVs / drones)
- Flight simulators
- Space systems and launch vehicles
- Naval and military platforms

The standard is maintained and published by **Stock Flight Systems** and is freely available, making it attractive for cost-sensitive or open-architecture programs.

---

## Architecture Overview

### Physical Layer

CANaerospace runs on the standard ISO 11898 physical layer:
- Baud rates: typically **1 Mbit/s** for avionics (also 500 kbit/s, 250 kbit/s)
- Differential signaling on CAN_H and CAN_L
- Bus termination: 120 Ω at each end
- Maximum bus length depends on baud rate (40 m @ 1 Mbit/s)

### Dual-Redundant Bus Topology

Aerospace systems commonly employ two independent CAN buses (Bus A and Bus B) for fault tolerance:

```
  ┌──────────┐     Bus A     ┌──────────┐     Bus A     ┌──────────┐
  │  Node 1  │───────────────│  Node 2  │───────────────│  Node 3  │
  │          │     Bus B     │          │     Bus B     │          │
  │          │───────────────│          │───────────────│          │
  └──────────┘               └──────────┘               └──────────┘
```

Each node has two independent CAN controllers. Nodes transmit on both buses simultaneously and receive from both, selecting the valid frame when one bus fails.

### Node Types

| Node Type | Description |
|-----------|-------------|
| **Sensor Node** | Acquires physical data (e.g., airspeed, altitude) and broadcasts |
| **Actuator Node** | Receives commands and drives physical outputs |
| **Gateway Node** | Bridges CANaerospace to other buses (ARINC 429, MIL-STD-1553) |
| **Display Node** | Receives avionics parameters for cockpit presentation |
| **FCC Node** | Flight Control Computer node, integrates multiple data sources |

---

## Message Structure

CANaerospace uses the standard CAN 2.0A frame with an **11-bit identifier** and up to **8 bytes of payload**.

### Frame Layout

```
 ┌────────────────────┬─────┬──────────────────────────────────────────┐
 │  CAN ID (11 bits)  │ DLC │              DATA (0–8 bytes)            │
 │  Message ID        │     │  [Node ID][Data Type][Svc Code][Message  │
 │  (0x000–0x7FF)     │     │           Data (4 bytes max)]            │
 └────────────────────┴─────┴──────────────────────────────────────────┘
```

### CANaerospace Data Frame (Normal Operation)

| Byte | Field | Description |
|------|-------|-------------|
| 0 | **Node ID** | Originating node identifier (0–255; 0 = anonymous) |
| 1 | **Data Type Code (DTC)** | Type of data contained in bytes 4–7 |
| 2 | **Service Code** | Distinguishes normal data (0) from node services |
| 3 | **Message Code** | Rolling counter (0–255) for sequence detection |
| 4–7 | **Data** | Up to 4 bytes of payload, interpreted per DTC |

### Example: Airspeed Message

A node transmitting indicated airspeed (CAN ID `0x101`, defined as "Indicated Airspeed" in the standard):

```
CAN ID:  0x101
DLC:     8
Data:    [0x05] [0x09] [0x00] [0x2A] [43 BC 00 00]
          Node   DTC   Svc   MsgCnt  Value (float = 150.75 knots)
```

---

## Node Services

CANaerospace defines several **Node Services** — special messages used for node management and identification. They use specific CAN IDs in the range `0x700`–`0x7FF`.

### Service Codes

| Code | Service | Description |
|------|---------|-------------|
| 0 | **NOD** | Normal Operation Data (standard sensor/actuator data) |
| 1 | **NSS** | Node Synchronization Service — time synchronization |
| 2 | **DDS** | Data Download Service — firmware/config download |
| 3 | **SDS** | Simulation Data Service — inject test data |
| 4 | **TIS** | Transmission Interval Service — set broadcast rate |
| 5 | **FPS** | Flash Programming Service |
| 6 | **TSS** | Timestamp Service |
| 7 | **DUS** | Data Upload Service |
| 8–9 | **IDS** | Identification Service — node type, hardware/software version |
| 10 | **CAS** | CAN Aerospace Service — protocol version query |

### Identification Service (IDS) Example

When a master queries node ID `0x05` for identification:

```
Request  → CAN ID: 0x78F, Data: [0xFF][0x00][0x08][0x00][00 00 00 05]
Response ← CAN ID: 0x705, Data: [0x05][0x00][0x08][0x00][HW SW DD UD]
                                                         Hardware/SW versions
```

---

## Data Type Codes (DTC)

The **Data Type Code** in byte 1 of each message tells receivers how to interpret the 4-byte payload.

| DTC | Type | Size | Description |
|-----|------|------|-------------|
| 0 | NODATA | 0 | No data payload |
| 1 | ERROR | 4 | Error code (uint32) |
| 2 | FLOAT | 4 | IEEE 754 single precision |
| 3 | LONG | 4 | Signed 32-bit integer |
| 4 | ULONG | 4 | Unsigned 32-bit integer |
| 5 | BLONG | 4 | 32-bit bitfield |
| 6 | SHORT | 2 | Signed 16-bit integer |
| 7 | USHORT | 2 | Unsigned 16-bit integer |
| 8 | BSHORT | 2 | 16-bit bitfield |
| 9 | CHAR | 1 | Signed 8-bit integer |
| 10 | UCHAR | 1 | Unsigned 8-bit integer |
| 11 | BCHAR | 1 | 8-bit bitfield |
| 12 | SHORT2 | 4 | Two signed 16-bit values |
| 13 | USHORT2 | 4 | Two unsigned 16-bit values |
| 14 | BSHORT2 | 4 | Two 16-bit bitfields |
| 15 | CHAR4 | 4 | Four signed 8-bit values |
| 16 | UCHAR4 | 4 | Four unsigned 8-bit values |
| 17 | BCHAR4 | 4 | Four 8-bit bitfields |
| 18 | CHAR2 | 2 | Two signed 8-bit values |
| 19 | DOUBLEHIGH | 4 | High 32 bits of IEEE 754 double |
| 20 | DOUBLELOW | 4 | Low 32 bits of IEEE 754 double |
| 100–255 | USER DEFINED | — | Application-specific types |

---

## Standard Message Identifiers

CANaerospace partitions the 11-bit CAN identifier space (0x000–0x7FF) into functional ranges:

| ID Range | Usage |
|----------|-------|
| `0x000` | Emergency Event Data (EED) — highest priority |
| `0x001–0x07F` | Node Service High Priority |
| `0x080–0x0FF` | User-defined high priority |
| `0x100–0x17F` | Normal Operation Data — Air Data (airspeed, altitude, etc.) |
| `0x180–0x1FF` | Normal Operation Data — Attitude/Navigation |
| `0x200–0x27F` | Normal Operation Data — Engine/Fuel |
| `0x280–0x2FF` | Normal Operation Data — Aircraft Systems |
| `0x300–0x37F` | Normal Operation Data — Avionics/Electrical |
| `0x380–0x5FF` | User-defined normal data |
| `0x600–0x6FF` | Debug / test messages |
| `0x700–0x7FE` | Node Services (low priority) |
| `0x7FF` | Reserved |

### Selected Standard Message IDs

| CAN ID | Parameter | Unit | DTC |
|--------|-----------|------|-----|
| `0x101` | Indicated Airspeed (IAS) | knots | FLOAT |
| `0x102` | True Airspeed (TAS) | knots | FLOAT |
| `0x103` | Barometric Altitude | feet | FLOAT |
| `0x104` | Baro-corrected Altitude | feet | FLOAT |
| `0x106` | Vertical Speed | ft/min | FLOAT |
| `0x180` | Roll Angle | degrees | FLOAT |
| `0x181` | Pitch Angle | degrees | FLOAT |
| `0x182` | Magnetic Heading | degrees | FLOAT |
| `0x200` | Engine RPM #1 | RPM | FLOAT |
| `0x202` | Engine Oil Pressure #1 | psi | FLOAT |
| `0x204` | Engine Oil Temperature #1 | °C | FLOAT |
| `0x206` | Fuel Flow #1 | lb/hr | FLOAT |

---

## Programming in C/C++

### 1. Data Structures and Type Definitions

```c
/*
 * canaerospace.h
 * Core data structures for CANaerospace protocol implementation
 */

#ifndef CANAEROSPACE_H
#define CANAEROSPACE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── Data Type Codes (DTC) ─────────────────────────────────────────── */
typedef enum {
    CAN_AS_DTC_NODATA   = 0,
    CAN_AS_DTC_ERROR    = 1,
    CAN_AS_DTC_FLOAT    = 2,
    CAN_AS_DTC_LONG     = 3,
    CAN_AS_DTC_ULONG    = 4,
    CAN_AS_DTC_BLONG    = 5,
    CAN_AS_DTC_SHORT    = 6,
    CAN_AS_DTC_USHORT   = 7,
    CAN_AS_DTC_BSHORT   = 8,
    CAN_AS_DTC_CHAR     = 9,
    CAN_AS_DTC_UCHAR    = 10,
    CAN_AS_DTC_BCHAR    = 11,
    CAN_AS_DTC_SHORT2   = 12,
    CAN_AS_DTC_USHORT2  = 13,
    CAN_AS_DTC_CHAR4    = 14,
    CAN_AS_DTC_UCHAR4   = 15,
} CanAsDataType_t;

/* ── Service Codes ──────────────────────────────────────────────────── */
typedef enum {
    CAN_AS_SVC_NOD = 0,  /* Normal Operation Data */
    CAN_AS_SVC_NSS = 1,  /* Node Synchronization Service */
    CAN_AS_SVC_DDS = 2,  /* Data Download Service */
    CAN_AS_SVC_SDS = 3,  /* Simulation Data Service */
    CAN_AS_SVC_TIS = 4,  /* Transmission Interval Service */
    CAN_AS_SVC_IDS = 8,  /* Identification Service Request */
    CAN_AS_SVC_CAS = 10, /* CAN Aerospace Service */
} CanAsServiceCode_t;

/* ── Standard Message IDs (partial) ────────────────────────────────── */
#define CAN_AS_ID_IAS           0x101U  /* Indicated Airspeed [knots] */
#define CAN_AS_ID_TAS           0x102U  /* True Airspeed [knots] */
#define CAN_AS_ID_BARO_ALT      0x103U  /* Barometric Altitude [ft] */
#define CAN_AS_ID_CORR_ALT      0x104U  /* Baro-corrected Altitude [ft] */
#define CAN_AS_ID_VSPD          0x106U  /* Vertical Speed [ft/min] */
#define CAN_AS_ID_ROLL          0x180U  /* Roll Angle [deg] */
#define CAN_AS_ID_PITCH         0x181U  /* Pitch Angle [deg] */
#define CAN_AS_ID_HEADING       0x182U  /* Magnetic Heading [deg] */
#define CAN_AS_ID_ENG_RPM       0x200U  /* Engine RPM #1 */
#define CAN_AS_ID_OIL_PRESS     0x202U  /* Oil Pressure #1 [psi] */
#define CAN_AS_ID_OIL_TEMP      0x204U  /* Oil Temperature #1 [°C] */

/* ── Core Frame Structure ───────────────────────────────────────────── */
typedef struct {
    uint16_t message_id;     /* CAN 11-bit identifier */
    uint8_t  node_id;        /* Source node ID (byte 0) */
    uint8_t  data_type;      /* DTC (byte 1) */
    uint8_t  service_code;   /* Service code (byte 2) */
    uint8_t  message_code;   /* Rolling counter (byte 3) */
    uint8_t  payload[4];     /* Data bytes 4–7 */
    uint8_t  payload_len;    /* Actual payload length in bytes */
} CanAsFrame_t;

/* ── Payload union for type-safe access ────────────────────────────── */
typedef union {
    float    f;
    int32_t  l;
    uint32_t ul;
    uint32_t bl;
    int16_t  s[2];
    uint16_t us[2];
    int8_t   c[4];
    uint8_t  uc[4];
    uint8_t  raw[4];
} CanAsData_t;

#endif /* CANAEROSPACE_H */
```

---

### 2. Frame Encoding and Decoding

```c
/*
 * canaerospace.c
 * Frame packing/unpacking utilities
 */

#include "canaerospace.h"

/* Payload size lookup by DTC */
static const uint8_t DTC_SIZE[] = {
    0, 4, 4, 4, 4, 4,  /* NODATA, ERROR, FLOAT, LONG, ULONG, BLONG */
    2, 2, 2,            /* SHORT, USHORT, BSHORT */
    1, 1, 1,            /* CHAR, UCHAR, BCHAR */
    4, 4, 4, 4          /* SHORT2, USHORT2, CHAR4, UCHAR4 */
};

/**
 * @brief Pack a CANaerospace frame into an 8-byte CAN payload buffer.
 *
 * @param frame   Pointer to the populated CanAsFrame_t structure
 * @param buf     Output buffer (at least 8 bytes)
 * @param buf_len Output: number of bytes written
 * @return true on success, false on invalid parameters
 */
bool canas_pack(const CanAsFrame_t *frame, uint8_t *buf, uint8_t *buf_len)
{
    if (!frame || !buf || !buf_len) return false;
    if (frame->data_type >= (sizeof(DTC_SIZE) / sizeof(DTC_SIZE[0]))) return false;

    buf[0] = frame->node_id;
    buf[1] = frame->data_type;
    buf[2] = frame->service_code;
    buf[3] = frame->message_code;

    uint8_t plen = DTC_SIZE[frame->data_type];
    memcpy(&buf[4], frame->payload, plen);

    *buf_len = 4 + plen;
    return true;
}

/**
 * @brief Unpack an 8-byte CAN payload into a CanAsFrame_t structure.
 *
 * @param buf        Raw CAN data bytes (from hardware)
 * @param len        Number of received bytes (DLC)
 * @param message_id CAN arbitration ID
 * @param frame      Output frame structure
 * @return true on success
 */
bool canas_unpack(const uint8_t *buf, uint8_t len,
                  uint16_t message_id, CanAsFrame_t *frame)
{
    if (!buf || !frame || len < 4) return false;

    frame->message_id   = message_id;
    frame->node_id      = buf[0];
    frame->data_type    = buf[1];
    frame->service_code = buf[2];
    frame->message_code = buf[3];
    frame->payload_len  = (len > 4) ? (len - 4) : 0;

    memcpy(frame->payload, &buf[4], frame->payload_len);
    return true;
}

/**
 * @brief Extract a float value from a CANaerospace frame.
 *
 * @param frame   Source frame (must have DTC == FLOAT)
 * @param value   Output float value
 * @return true if DTC matches FLOAT and extraction succeeded
 */
bool canas_get_float(const CanAsFrame_t *frame, float *value)
{
    if (!frame || !value) return false;
    if (frame->data_type != CAN_AS_DTC_FLOAT) return false;

    /* Safe type-punning via memcpy (avoids strict aliasing UB) */
    memcpy(value, frame->payload, sizeof(float));
    return true;
}

/**
 * @brief Build a float NOD (Normal Operation Data) frame.
 */
bool canas_build_float_nod(CanAsFrame_t *frame, uint16_t msg_id,
                            uint8_t node_id, uint8_t msg_code, float value)
{
    if (!frame) return false;

    frame->message_id   = msg_id;
    frame->node_id      = node_id;
    frame->data_type    = CAN_AS_DTC_FLOAT;
    frame->service_code = CAN_AS_SVC_NOD;
    frame->message_code = msg_code;
    frame->payload_len  = 4;

    memcpy(frame->payload, &value, sizeof(float));
    return true;
}
```

---

### 3. Sensor Node — Airspeed Broadcast

```c
/*
 * airspeed_node.c
 * Example: ADC sensor node broadcasting Indicated Airspeed at 50 Hz
 */

#include "canaerospace.h"

/* Platform-specific CAN hardware abstraction (replace with your BSP) */
extern bool can_transmit(uint16_t id, const uint8_t *data, uint8_t len);
extern float adc_read_airspeed_knots(void);
extern uint32_t systick_ms(void);

#define NODE_ID_ADC     0x05U
#define BROADCAST_MS    20U     /* 50 Hz = 20 ms period */

void airspeed_node_run(void)
{
    uint8_t  msg_code  = 0;
    uint32_t last_tx   = 0;
    uint8_t  can_buf[8];
    uint8_t  can_len;
    CanAsFrame_t frame;

    for (;;) {
        uint32_t now = systick_ms();

        if ((now - last_tx) >= BROADCAST_MS) {
            last_tx = now;

            float ias = adc_read_airspeed_knots();

            canas_build_float_nod(&frame,
                                  CAN_AS_ID_IAS,
                                  NODE_ID_ADC,
                                  msg_code++,
                                  ias);

            if (canas_pack(&frame, can_buf, &can_len)) {
                can_transmit(CAN_AS_ID_IAS, can_buf, can_len);
            }
        }

        /* ... other tasks ... */
    }
}
```

---

### 4. Display Node — Receiving Multiple Parameters

```c
/*
 * display_node.c
 * Example: Cockpit display receiving airspeed, altitude, attitude
 */

#include "canaerospace.h"
#include <stdio.h>

typedef struct {
    float    ias_knots;
    float    baro_alt_ft;
    float    roll_deg;
    float    pitch_deg;
    float    heading_deg;
    uint8_t  last_msg_code[5];  /* sequence tracking per parameter */
    bool     valid[5];
} AviationData_t;

static AviationData_t g_data = {0};

/* Index mapping for sequence tracking */
typedef enum {
    IDX_IAS = 0,
    IDX_ALT,
    IDX_ROLL,
    IDX_PITCH,
    IDX_HDG,
    IDX_COUNT
} ParamIndex_t;

/**
 * @brief Detect dropped frames via message code discontinuity.
 */
static bool check_sequence(uint8_t *last, uint8_t current)
{
    bool gap = (uint8_t)(current - *last - 1) != 0;
    *last = current;
    return !gap;
}

/**
 * @brief Called from CAN RX ISR or task for every received frame.
 */
void display_on_receive(uint16_t can_id, const uint8_t *data, uint8_t dlc)
{
    CanAsFrame_t frame;
    float value;

    if (!canas_unpack(data, dlc, can_id, &frame)) return;
    if (frame.service_code != CAN_AS_SVC_NOD)    return; /* ignore services */
    if (!canas_get_float(&frame, &value))          return; /* expect float only */

    switch (can_id) {
    case CAN_AS_ID_IAS:
        g_data.valid[IDX_IAS]   = check_sequence(&g_data.last_msg_code[IDX_IAS],
                                                   frame.message_code);
        g_data.ias_knots        = value;
        break;

    case CAN_AS_ID_BARO_ALT:
        g_data.valid[IDX_ALT]   = check_sequence(&g_data.last_msg_code[IDX_ALT],
                                                   frame.message_code);
        g_data.baro_alt_ft      = value;
        break;

    case CAN_AS_ID_ROLL:
        g_data.valid[IDX_ROLL]  = check_sequence(&g_data.last_msg_code[IDX_ROLL],
                                                   frame.message_code);
        g_data.roll_deg         = value;
        break;

    case CAN_AS_ID_PITCH:
        g_data.valid[IDX_PITCH] = check_sequence(&g_data.last_msg_code[IDX_PITCH],
                                                   frame.message_code);
        g_data.pitch_deg        = value;
        break;

    case CAN_AS_ID_HEADING:
        g_data.valid[IDX_HDG]   = check_sequence(&g_data.last_msg_code[IDX_HDG],
                                                   frame.message_code);
        g_data.heading_deg      = value;
        break;

    default:
        break;
    }
}

void display_render(void)
{
    printf("=== PFD ===\n");
    printf("IAS    : %6.1f kt  [%s]\n",
           g_data.ias_knots,   g_data.valid[IDX_IAS]   ? "OK" : "FAIL");
    printf("ALT    : %6.0f ft  [%s]\n",
           g_data.baro_alt_ft, g_data.valid[IDX_ALT]   ? "OK" : "FAIL");
    printf("ROLL   : %+6.1f°   [%s]\n",
           g_data.roll_deg,    g_data.valid[IDX_ROLL]  ? "OK" : "FAIL");
    printf("PITCH  : %+6.1f°   [%s]\n",
           g_data.pitch_deg,   g_data.valid[IDX_PITCH] ? "OK" : "FAIL");
    printf("HDG    : %6.1f°    [%s]\n",
           g_data.heading_deg, g_data.valid[IDX_HDG]   ? "OK" : "FAIL");
}
```

---

### 5. Node Identification Service (C/C++)

```c
/*
 * node_service.c
 * Handle incoming Identification Service (IDS) requests and respond
 */

#include "canaerospace.h"

#define MY_NODE_ID          0x05U
#define MY_HW_REVISION      0x01U   /* Hardware revision 1 */
#define MY_SW_REVISION      0x03U   /* Software version 3 */
#define MY_DEVICE_TYPE      0x12U   /* Custom device type code */

#define CAN_ID_NODE_SVC_RX  0x78FU  /* Broadcast IDS request */
#define CAN_ID_NODE_SVC_TX  (0x700U + MY_NODE_ID)  /* 0x705 */

extern bool can_transmit(uint16_t id, const uint8_t *data, uint8_t len);

void handle_node_service(uint16_t can_id, const uint8_t *data, uint8_t dlc)
{
    CanAsFrame_t req;

    if (!canas_unpack(data, dlc, can_id, &req)) return;

    /* Check if this IDS request targets us or is broadcast (0xFF) */
    if (req.service_code != CAN_AS_SVC_IDS)   return;
    if (req.payload[3] != MY_NODE_ID &&
        req.payload[3] != 0xFF)                return;

    /* Build identification response */
    CanAsFrame_t rsp = {0};
    rsp.message_id   = CAN_ID_NODE_SVC_TX;
    rsp.node_id      = MY_NODE_ID;
    rsp.data_type    = CAN_AS_DTC_NODATA;
    rsp.service_code = CAN_AS_SVC_IDS;
    rsp.message_code = req.message_code;   /* echo back request code */
    rsp.payload[0]   = MY_HW_REVISION;
    rsp.payload[1]   = MY_SW_REVISION;
    rsp.payload[2]   = MY_DEVICE_TYPE;
    rsp.payload[3]   = 0x00;              /* reserved */
    rsp.payload_len  = 4;

    uint8_t buf[8];
    uint8_t len;

    if (canas_pack(&rsp, buf, &len)) {
        can_transmit(CAN_ID_NODE_SVC_TX, buf, len);
    }
}
```

---

### 6. Dual-Bus Redundancy Management (C++)

```cpp
/*
 * redundancy_manager.hpp / .cpp
 * Dual-bus CAN receive with automatic failover
 */

#include "canaerospace.h"
#include <cstdint>
#include <array>
#include <functional>

enum class BusId : uint8_t { BUS_A = 0, BUS_B = 1 };

struct BusStatus {
    bool     active        = true;
    uint32_t rx_count      = 0;
    uint32_t error_count   = 0;
    uint32_t last_rx_ms    = 0;
};

class RedundancyManager {
public:
    using FrameCallback = std::function<void(const CanAsFrame_t &)>;

    explicit RedundancyManager(FrameCallback cb) : cb_(std::move(cb)) {}

    /**
     * @brief Called from each bus RX handler.
     * Selects the frame from the primary (preferred) bus; falls back to
     * secondary if primary is silent for > BUS_TIMEOUT_MS.
     */
    void on_receive(BusId bus, uint16_t can_id,
                    const uint8_t *data, uint8_t dlc, uint32_t now_ms)
    {
        auto &st = status_[static_cast<uint8_t>(bus)];
        st.rx_count++;
        st.last_rx_ms = now_ms;

        CanAsFrame_t frame;
        if (!canas_unpack(data, dlc, can_id, &frame)) {
            st.error_count++;
            return;
        }

        /* Accept from preferred bus, or from fallback if preferred silent */
        BusId preferred = select_preferred(now_ms);
        if (bus == preferred) {
            cb_(frame);
        }
    }

    void on_bus_error(BusId bus) {
        status_[static_cast<uint8_t>(bus)].error_count++;
    }

    const BusStatus &bus_status(BusId bus) const {
        return status_[static_cast<uint8_t>(bus)];
    }

private:
    static constexpr uint32_t BUS_TIMEOUT_MS = 100U;

    BusId select_preferred(uint32_t now_ms)
    {
        auto &a = status_[0];
        bool a_alive = (now_ms - a.last_rx_ms) < BUS_TIMEOUT_MS;
        return a_alive ? BusId::BUS_A : BusId::BUS_B;
    }

    std::array<BusStatus, 2> status_{};
    FrameCallback             cb_;
};
```

---

## Programming in Rust

### 1. Core Types and Constants

```rust
// src/canaerospace/types.rs
// CANaerospace core type definitions

use core::mem;

/// Data Type Codes
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DataType {
    NoData   = 0,
    Error    = 1,
    Float    = 2,
    Long     = 3,
    Ulong    = 4,
    Blong    = 5,
    Short    = 6,
    Ushort   = 7,
    Bshort   = 8,
    Char     = 9,
    Uchar    = 10,
    Bchar    = 11,
    Short2   = 12,
    Ushort2  = 13,
    Char4    = 14,
    Uchar4   = 15,
}

impl DataType {
    /// Returns the number of payload bytes for this data type.
    pub fn payload_size(self) -> usize {
        match self {
            Self::NoData                        => 0,
            Self::Char | Self::Uchar | Self::Bchar => 1,
            Self::Short | Self::Ushort | Self::Bshort => 2,
            _                                   => 4,
        }
    }
}

impl TryFrom<u8> for DataType {
    type Error = CanAsError;
    fn try_from(v: u8) -> Result<Self, Self::Error> {
        match v {
            0  => Ok(Self::NoData),
            1  => Ok(Self::Error),
            2  => Ok(Self::Float),
            3  => Ok(Self::Long),
            4  => Ok(Self::Ulong),
            5  => Ok(Self::Blong),
            6  => Ok(Self::Short),
            7  => Ok(Self::Ushort),
            8  => Ok(Self::Bshort),
            9  => Ok(Self::Char),
            10 => Ok(Self::Uchar),
            11 => Ok(Self::Bchar),
            12 => Ok(Self::Short2),
            13 => Ok(Self::Ushort2),
            14 => Ok(Self::Char4),
            15 => Ok(Self::Uchar4),
            _  => Err(CanAsError::InvalidDataType(v)),
        }
    }
}

/// Service Codes
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ServiceCode {
    Nod = 0,
    Nss = 1,
    Dds = 2,
    Sds = 3,
    Tis = 4,
    Ids = 8,
    Cas = 10,
}

/// Well-known CANaerospace message IDs
pub mod msg_id {
    pub const IAS:      u16 = 0x101;
    pub const TAS:      u16 = 0x102;
    pub const BARO_ALT: u16 = 0x103;
    pub const CORR_ALT: u16 = 0x104;
    pub const VSPEED:   u16 = 0x106;
    pub const ROLL:     u16 = 0x180;
    pub const PITCH:    u16 = 0x181;
    pub const HEADING:  u16 = 0x182;
    pub const ENG_RPM:  u16 = 0x200;
    pub const OIL_PRESS:u16 = 0x202;
    pub const OIL_TEMP: u16 = 0x204;
}

/// Protocol errors
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CanAsError {
    BufferTooShort,
    InvalidDataType(u8),
    DataTypeMismatch { expected: DataType, got: DataType },
    InvalidMessageId(u16),
    SequenceGap { expected: u8, got: u8 },
}

/// Payload value, type-safe enum
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Payload {
    None,
    Float(f32),
    Long(i32),
    Ulong(u32),
    Short([i16; 2]),
    Ushort([u16; 2]),
    Char([i8; 4]),
    Uchar([u8; 4]),
    Raw([u8; 4]),
}
```

---

### 2. Frame Encoding and Decoding

```rust
// src/canaerospace/frame.rs

use super::types::*;

/// A parsed CANaerospace frame
#[derive(Debug, Clone)]
pub struct CanAsFrame {
    pub message_id:   u16,
    pub node_id:      u8,
    pub data_type:    DataType,
    pub service_code: u8,
    pub message_code: u8,
    pub payload:      Payload,
}

impl CanAsFrame {
    /// Parse a raw CAN frame (message_id + 8-byte buffer)
    pub fn from_raw(message_id: u16, buf: &[u8]) -> Result<Self, CanAsError> {
        if buf.len() < 4 {
            return Err(CanAsError::BufferTooShort);
        }

        let node_id      = buf[0];
        let data_type    = DataType::try_from(buf[1])?;
        let service_code = buf[2];
        let message_code = buf[3];

        let payload_bytes = if buf.len() >= 8 { &buf[4..8] }
                            else              { &buf[4..] };

        let payload = Self::decode_payload(data_type, payload_bytes)?;

        Ok(Self { message_id, node_id, data_type, service_code, message_code, payload })
    }

    /// Serialize frame to a buffer; returns number of bytes written
    pub fn to_raw(&self, buf: &mut [u8; 8]) -> Result<usize, CanAsError> {
        buf[0] = self.node_id;
        buf[1] = self.data_type as u8;
        buf[2] = self.service_code;
        buf[3] = self.message_code;

        let plen = self.encode_payload(&mut buf[4..])?;
        Ok(4 + plen)
    }

    /// Convenience: build a float NOD frame
    pub fn float_nod(
        message_id:   u16,
        node_id:      u8,
        message_code: u8,
        value:        f32,
    ) -> Self {
        Self {
            message_id,
            node_id,
            data_type:    DataType::Float,
            service_code: ServiceCode::Nod as u8,
            message_code,
            payload:      Payload::Float(value),
        }
    }

    /// Extract f32 value; returns error if data type is not Float
    pub fn as_float(&self) -> Result<f32, CanAsError> {
        match self.payload {
            Payload::Float(v) => Ok(v),
            _ => Err(CanAsError::DataTypeMismatch {
                expected: DataType::Float,
                got:      self.data_type,
            }),
        }
    }

    fn decode_payload(dt: DataType, bytes: &[u8]) -> Result<Payload, CanAsError> {
        let raw4 = |b: &[u8]| -> [u8; 4] {
            let mut a = [0u8; 4];
            let n = b.len().min(4);
            a[..n].copy_from_slice(&b[..n]);
            a
        };

        Ok(match dt {
            DataType::NoData  => Payload::None,
            DataType::Float   => Payload::Float(f32::from_be_bytes(raw4(bytes))),
            DataType::Long    => Payload::Long(i32::from_be_bytes(raw4(bytes))),
            DataType::Ulong | DataType::Blong
                              => Payload::Ulong(u32::from_be_bytes(raw4(bytes))),
            DataType::Short | DataType::Bshort => {
                let a = i16::from_be_bytes([bytes[0], bytes[1]]);
                let b = i16::from_be_bytes([bytes[2], bytes[3]]);
                Payload::Short([a, b])
            },
            DataType::Ushort  => {
                let a = u16::from_be_bytes([bytes[0], bytes[1]]);
                let b = u16::from_be_bytes([bytes[2], bytes[3]]);
                Payload::Ushort([a, b])
            },
            DataType::Uchar4 | DataType::Bchar => {
                Payload::Uchar(raw4(bytes))
            },
            DataType::Char4   => {
                let r = raw4(bytes);
                Payload::Char([r[0] as i8, r[1] as i8, r[2] as i8, r[3] as i8])
            },
            _                 => Payload::Raw(raw4(bytes)),
        })
    }

    fn encode_payload(&self, out: &mut [u8]) -> Result<usize, CanAsError> {
        match &self.payload {
            Payload::None              => Ok(0),
            Payload::Float(v)  => { out[..4].copy_from_slice(&v.to_be_bytes()); Ok(4) },
            Payload::Long(v)   => { out[..4].copy_from_slice(&v.to_be_bytes()); Ok(4) },
            Payload::Ulong(v)  => { out[..4].copy_from_slice(&v.to_be_bytes()); Ok(4) },
            Payload::Ushort(v) => {
                out[..2].copy_from_slice(&v[0].to_be_bytes());
                out[2..4].copy_from_slice(&v[1].to_be_bytes());
                Ok(4)
            },
            Payload::Short(v)  => {
                out[..2].copy_from_slice(&v[0].to_be_bytes());
                out[2..4].copy_from_slice(&v[1].to_be_bytes());
                Ok(4)
            },
            Payload::Uchar(v)  => { out[..4].copy_from_slice(v); Ok(4) },
            Payload::Char(v)   => {
                for (i, &c) in v.iter().enumerate() { out[i] = c as u8; }
                Ok(4)
            },
            Payload::Raw(v)    => { out[..4].copy_from_slice(v); Ok(4) },
        }
    }
}
```

---

### 3. Sensor Node — Airspeed Publisher

```rust
// src/nodes/airspeed_node.rs
// Periodic airspeed broadcast node at 50 Hz

use crate::canaerospace::{CanAsFrame, msg_id};
use crate::hal::{CanBus, Adc, SysTick};  // platform HAL traits

const NODE_ID: u8     = 0x05;
const PERIOD_MS: u32  = 20;  // 50 Hz

pub struct AirspeedNode<C: CanBus, A: Adc, T: SysTick> {
    can:       C,
    adc:       A,
    timer:     T,
    msg_code:  u8,
    last_tx:   u32,
}

impl<C: CanBus, A: Adc, T: SysTick> AirspeedNode<C, A, T> {
    pub fn new(can: C, adc: A, timer: T) -> Self {
        Self { can, adc, timer, msg_code: 0, last_tx: 0 }
    }

    pub fn run(&mut self) -> ! {
        loop {
            let now = self.timer.now_ms();

            if now.wrapping_sub(self.last_tx) >= PERIOD_MS {
                self.last_tx = now;
                self.broadcast_ias();
            }

            // Yield to other tasks / feed watchdog
        }
    }

    fn broadcast_ias(&mut self) {
        let ias_knots = self.adc.read_airspeed_knots();

        let frame = CanAsFrame::float_nod(
            msg_id::IAS,
            NODE_ID,
            self.msg_code,
            ias_knots,
        );

        self.msg_code = self.msg_code.wrapping_add(1);

        let mut buf = [0u8; 8];
        if let Ok(len) = frame.to_raw(&mut buf) {
            let _ = self.can.transmit(msg_id::IAS, &buf[..len]);
        }
    }
}
```

---

### 4. Display Node — Multi-Parameter Subscriber

```rust
// src/nodes/display_node.rs
// Receives airspeed, altitude, and attitude parameters

use crate::canaerospace::{CanAsFrame, CanAsError, msg_id, ServiceCode};

/// Tracks a single avionics parameter with freshness and sequence checking
#[derive(Debug, Default)]
pub struct Parameter {
    pub value:      f32,
    pub valid:      bool,
    pub last_code:  u8,
    pub gap_count:  u32,
}

impl Parameter {
    pub fn update(&mut self, frame: &CanAsFrame) -> Result<(), CanAsError> {
        let value = frame.as_float()?;

        // Detect sequence gaps (dropped frames)
        let expected = self.last_code.wrapping_add(1);
        if self.valid && frame.message_code != expected {
            self.gap_count += 1;
        }

        self.last_code = frame.message_code;
        self.value     = value;
        self.valid     = true;
        Ok(())
    }
}

/// Primary Flight Data
#[derive(Debug, Default)]
pub struct FlightData {
    pub ias_knots:   Parameter,
    pub baro_alt_ft: Parameter,
    pub roll_deg:    Parameter,
    pub pitch_deg:   Parameter,
    pub heading_deg: Parameter,
}

impl FlightData {
    /// Process an incoming raw CAN frame
    pub fn on_receive(&mut self, can_id: u16, raw: &[u8]) {
        let frame = match CanAsFrame::from_raw(can_id, raw) {
            Ok(f)  => f,
            Err(_) => return,
        };

        // Only handle Normal Operation Data
        if frame.service_code != ServiceCode::Nod as u8 {
            return;
        }

        let _ = match can_id {
            msg_id::IAS      => self.ias_knots.update(&frame),
            msg_id::BARO_ALT => self.baro_alt_ft.update(&frame),
            msg_id::ROLL     => self.roll_deg.update(&frame),
            msg_id::PITCH    => self.pitch_deg.update(&frame),
            msg_id::HEADING  => self.heading_deg.update(&frame),
            _                => return,
        };
    }

    pub fn render(&self) {
        fn fmt(p: &Parameter, unit: &str) -> String {
            if p.valid {
                format!("{:8.2} {} [OK, gaps={}]", p.value, unit, p.gap_count)
            } else {
                format!("  --- {} [NO DATA]", unit)
            }
        }

        println!("=== Primary Flight Display ===");
        println!("IAS    : {}", fmt(&self.ias_knots,   "kt"));
        println!("ALT    : {}", fmt(&self.baro_alt_ft, "ft"));
        println!("ROLL   : {}", fmt(&self.roll_deg,    "°"));
        println!("PITCH  : {}", fmt(&self.pitch_deg,   "°"));
        println!("HDG    : {}", fmt(&self.heading_deg, "°"));
    }
}
```

---

### 5. Dual-Bus Redundancy in Rust

```rust
// src/redundancy.rs
// Dual-bus receive with primary/fallback selection

use crate::canaerospace::CanAsFrame;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BusId { BusA, BusB }

#[derive(Debug, Default)]
pub struct BusStatus {
    pub rx_count:   u64,
    pub err_count:  u64,
    pub last_rx_ms: u32,
    pub alive:      bool,
}

pub struct RedundancyManager<F>
where
    F: FnMut(BusId, CanAsFrame),
{
    status:   [BusStatus; 2],
    timeout:  u32,
    callback: F,
}

impl<F: FnMut(BusId, CanAsFrame)> RedundancyManager<F> {
    pub fn new(timeout_ms: u32, callback: F) -> Self {
        Self {
            status:   [BusStatus::default(), BusStatus::default()],
            timeout:  timeout_ms,
            callback,
        }
    }

    /// Call on every received frame from either bus
    pub fn on_receive(
        &mut self,
        bus:    BusId,
        can_id: u16,
        data:   &[u8],
        now_ms: u32,
    ) {
        let idx = bus as usize;
        self.status[idx].rx_count   += 1;
        self.status[idx].last_rx_ms  = now_ms;
        self.status[idx].alive       = true;

        let frame = match CanAsFrame::from_raw(can_id, data) {
            Ok(f)  => f,
            Err(_) => { self.status[idx].err_count += 1; return; }
        };

        if bus == self.preferred_bus(now_ms) {
            (self.callback)(bus, frame);
        }
    }

    pub fn on_error(&mut self, bus: BusId) {
        self.status[bus as usize].err_count += 1;
    }

    fn preferred_bus(&self, now_ms: u32) -> BusId {
        let a_age = now_ms.wrapping_sub(self.status[0].last_rx_ms);
        if a_age < self.timeout { BusId::BusA } else { BusId::BusB }
    }

    pub fn bus_status(&self, bus: BusId) -> &BusStatus {
        &self.status[bus as usize]
    }
}
```

---

### 6. Unit Tests (Rust)

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use crate::canaerospace::{CanAsFrame, DataType, Payload, msg_id};

    #[test]
    fn test_float_roundtrip() {
        let orig = 150.75_f32;
        let frame = CanAsFrame::float_nod(msg_id::IAS, 0x05, 42, orig);

        let mut buf = [0u8; 8];
        let len = frame.to_raw(&mut buf).unwrap();

        let decoded = CanAsFrame::from_raw(msg_id::IAS, &buf[..len]).unwrap();
        let value   = decoded.as_float().unwrap();

        assert!((value - orig).abs() < 1e-5, "float mismatch: {value} vs {orig}");
        assert_eq!(decoded.node_id,      0x05);
        assert_eq!(decoded.message_code, 42);
        assert_eq!(decoded.data_type,    DataType::Float);
    }

    #[test]
    fn test_sequence_gap_detection() {
        let mut param = crate::nodes::display_node::Parameter::default();

        // First frame: no gap expected
        let f0 = CanAsFrame::float_nod(msg_id::IAS, 1, 0, 100.0);
        param.update(&f0).unwrap();
        assert_eq!(param.gap_count, 0);

        // Consecutive frame: ok
        let f1 = CanAsFrame::float_nod(msg_id::IAS, 1, 1, 101.0);
        param.update(&f1).unwrap();
        assert_eq!(param.gap_count, 0);

        // Skipped frame 2, jump to 3: gap
        let f3 = CanAsFrame::float_nod(msg_id::IAS, 1, 3, 103.0);
        param.update(&f3).unwrap();
        assert_eq!(param.gap_count, 1);
    }

    #[test]
    fn test_buffer_too_short() {
        let result = CanAsFrame::from_raw(0x101, &[0x05, 0x02]);
        assert!(result.is_err());
    }
}
```

---

## Error Handling and Redundancy

### Message Code (Rolling Counter)

Each CANaerospace data frame carries an 8-bit **message code** that increments by 1 on every transmission. Receivers use this to detect:

- **Lost frames** — a gap in the sequence (e.g., received 45, then 47, skipping 46)
- **Duplicate frames** — same code received twice (from bus echo or duplicate nodes)
- **Node restarts** — code resets to 0 unexpectedly

A common policy is to declare a parameter **INVALID** if more than N consecutive frames are missed within a given time window (e.g., 3 frames at 50 Hz = 60 ms timeout).

### Emergency Event Data (EED)

CAN ID `0x000` is reserved for **Emergency Event Data** — the highest-priority identifier on the bus (CAN arbitration guarantees ID 0 wins). Nodes detecting a safety-critical condition (e.g., engine fire, flight control failure) immediately transmit an EED frame, preempting all other traffic.

### Bus-Off Recovery

CAN nodes automatically enter **Bus-Off** state after 255 consecutive transmission errors. A CANaerospace node must:
1. Detect Bus-Off
2. Switch all outgoing traffic to the healthy bus
3. Attempt periodic recovery of the faulted bus
4. Log the fault for maintenance

---

## Certification and Safety Considerations

Aerospace software must comply with several standards when using CANaerospace:

| Standard | Scope |
|----------|-------|
| **DO-178C** | Software airworthiness (DAL A–E) |
| **DO-254** | Hardware airworthiness for complex electronics |
| **ARP4754A** | System development guidelines |
| **DO-160G** | Environmental qualification (EMI, temperature, vibration) |
| **RTCA DO-297** | Integrated Modular Avionics |

Key design considerations:
- **Determinism**: Fixed transmission schedules for all periodic messages
- **Latency**: 1 Mbit/s CAN supports ~8,000 frames/second; frame latency < 125 µs per frame
- **WCET analysis**: Worst-Case Execution Time must be bounded for all receive handlers
- **Partition isolation**: Failures in one CANaerospace node must not corrupt others (fault containment)
- **Data staleness**: Parameters must be invalidated if no refresh arrives within a defined timeout
- **Endianness**: CANaerospace mandates **big-endian** byte order for multi-byte values

---

## Summary

**CANaerospace** is a purpose-built, lightweight application-layer protocol for aviation and aerospace systems riding on the proven CAN bus physical layer. Its design priorities — minimal overhead, deterministic timing, standardized message identifiers, and support for redundant dual-bus architectures — make it an excellent fit for General Aviation avionics, UAV systems, and flight simulators where certified heavy protocols like ARINC 429 or MIL-STD-1553 are impractical.

The protocol defines a clean 8-byte frame structure with four header bytes (Node ID, Data Type Code, Service Code, Message Code) followed by up to four bytes of typed payload. The standardized message ID space covers the full range of common avionics parameters — airspeed, altitude, attitude, navigation, engine data — while leaving room for user-defined extensions.

**In C/C++**, implementation focuses on compact structs, safe type-punning via `memcpy`, and hardware abstraction layers that allow the same protocol logic to run on bare-metal embedded MCUs or POSIX-based simulation hosts. Dual-bus redundancy is cleanly managed by a bus-selection manager that monitors liveness of each bus and transparently falls back on failure.

**In Rust**, the strong type system and exhaustive pattern matching enable a safe, zero-cost abstraction over the wire format. The `Payload` enum eliminates undefined behavior from union access, and the `Result`-based API surfaces all protocol errors at compile-checked boundaries. Rust's ownership model naturally enforces single-owner access to the CAN hardware, while trait-based HALs facilitate embedded portability without `std`.

Together, these implementations demonstrate that CANaerospace — despite its simplicity — provides all the building blocks needed for robust, certifiable avionics communication: typed messages, sequence monitoring, node services, emergency signaling, and redundancy management.

---

*Document generated for the CAN Protocol Reference Series — Topic 58: CANaerospace Standard*