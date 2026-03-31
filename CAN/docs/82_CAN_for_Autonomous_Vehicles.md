# 82. CAN for Autonomous Vehicles

**Architecture & Concepts**
- Full heterogeneous AV network diagram (CAN / CAN FD / Automotive Ethernet / LIN) with a domain-oriented topology
- Role breakdown: CAN for actuators, CAN FD for ADAS sensors, Ethernet for cameras and LiDAR

**Sensor Fusion**
- Radar object list encoding over CAN FD (binary frame layout)
- 2D constant-velocity Kalman filter (predict / update) merging radar (full-state) and camera (position-only) measurements
- Nearest-neighbour track association with configurable gating distance

**Safety (ISO 26262)**
- AUTOSAR E2E Profile 2: CRC-8 (SAE J1850) + rolling counter on every protected PDU
- Timeout monitoring, degraded mode, and safe-state fallback logic

**Code Examples**
| Example | Language | What it shows |
|---|---|---|
| `adas_radar_receiver.c` | C | SocketCAN CAN FD receive loop, radar frame parsing, AEB command TX |
| `adas_fusion.hpp/.cpp` | C++ | `FusionECU` class with Kalman filter, sensor merging, CAN FD encode |
| `radar_parser.rs` | Rust | `socketcan` CAN FD frame parsing, track table, AEB evaluation |
| `fusion_ecu.rs` | Rust | E2E Profile 2 checker, Kalman state, full fusion pipeline |
| `can_eth_gateway.c` | C | CAN FD → SOME/IP/UDP bridge to Automotive Ethernet HPC |

**Integration**
- Gateway ECU design (PDU routing, AUTOSAR COM/PduR)
- gPTP time synchronization — from Ethernet master down to CAN ECUs via AUTOSAR TimeSync frames

> Integrating CAN with Ethernet and sensor fusion for ADAS and self-driving systems.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Autonomous Vehicle Network Architecture](#2-autonomous-vehicle-network-architecture)
3. [CAN's Role in ADAS and Self-Driving Systems](#3-cans-role-in-adas-and-self-driving-systems)
4. [CAN and Automotive Ethernet Integration](#4-can-and-automotive-ethernet-integration)
5. [Sensor Fusion over CAN](#5-sensor-fusion-over-can)
6. [ADAS-Specific CAN Message Design](#6-adas-specific-can-message-design)
7. [Safety and Fault Tolerance in Autonomous CAN Networks](#7-safety-and-fault-tolerance-in-autonomous-can-networks)
8. [Programming CAN for ADAS in C/C++](#8-programming-can-for-adas-in-cc)
9. [Programming CAN for ADAS in Rust](#9-programming-can-for-adas-in-rust)
10. [Gateway: Bridging CAN and Automotive Ethernet](#10-gateway-bridging-can-and-automotive-ethernet)
11. [Time Synchronization: gPTP over Ethernet with CAN](#11-time-synchronization-gptp-over-ethernet-with-can)
12. [Summary](#12-summary)

---

## 1. Introduction

Modern autonomous vehicles (AVs) and Advanced Driver-Assistance Systems (ADAS) are among the most complex networked embedded systems ever built. They combine dozens of sensors — cameras, LiDAR, radar, ultrasonic — with real-time actuator control, high-speed data pipelines, and safety-critical decision-making, all inside a moving vehicle operating in unpredictable environments.

**Controller Area Network (CAN)** was designed in the 1980s for in-vehicle communication between ECUs (Electronic Control Units) at modest data rates. In the era of autonomous driving, CAN does not disappear — it evolves into a critical lower layer of a **heterogeneous network stack** that includes:

- **CAN FD** (Flexible Data-rate) for higher-bandwidth actuator and sensor buses
- **Automotive Ethernet** (100BASE-T1, 1000BASE-T1) for massive sensor data streams
- **LIN** for low-speed peripherals
- **FlexRay** (legacy) for deterministic chassis control
- **SOME/IP** and **DDS** as middleware layers over Ethernet

CAN remains essential because:

- It is **proven, deterministic, and fault-tolerant** — exactly what safety standards (ISO 26262) demand
- Actuator ECUs (braking, steering, throttle) run on CAN/CAN FD due to simplicity and robustness
- Legacy integration: existing automotive supply chain, tools, and infrastructure are CAN-based
- Low-latency bus arbitration is ideal for safety-critical, low-data-volume control messages

This document explores how CAN integrates into the AV architecture, how it works alongside Ethernet-based sensor pipelines, and how to program these integrations with practical C/C++ and Rust examples.

---

## 2. Autonomous Vehicle Network Architecture

A typical autonomous vehicle uses a **domain-oriented** or **zonal** E/E (Electrical/Electronic) architecture:

```
┌─────────────────────────────────────────────────────────────────┐
│                  AUTONOMOUS VEHICLE NETWORK                      │
│                                                                   │
│  ┌───────────────────────────────────────────────────────────┐   │
│  │              CENTRAL COMPUTE DOMAIN (HPC)                  │   │
│  │   AI Inference │ Sensor Fusion │ Path Planning │ V2X       │   │
│  │   ┌──────────────────────────────────────────────────┐    │   │
│  │   │         Automotive Ethernet Backbone              │    │   │
│  │   │    1000BASE-T1 / 100BASE-T1  (SOME/IP, DDS)      │    │   │
│  │   └──────┬──────────┬──────────┬──────────┬───────────┘    │   │
│  └──────────│──────────│──────────│──────────│────────────────┘   │
│             │          │          │          │                     │
│    ┌────────▼───┐  ┌───▼──────┐  │     ┌────▼──────────┐         │
│    │ PERCEPTION │  │ CHASSIS  │  │     │  BODY/COMFORT │         │
│    │  DOMAIN    │  │ DOMAIN   │  │     │  DOMAIN       │         │
│    │            │  │          │  │     │               │         │
│    │ CAN FD Bus │  │ CAN Bus  │  │     │ CAN Bus / LIN │         │
│    │  5 Mbit/s  │  │ 1 Mbit/s │  │     │               │         │
│    │            │  │          │  │     │               │         │
│    │ ┌────────┐ │  │ ┌──────┐ │  │     │ ┌───────────┐ │         │
│    │ │ Radar  │ │  │ │Brake │ │  │     │ │ Lights    │ │         │
│    │ │ ECU    │ │  │ │ ECU  │ │  │     │ │ HVAC      │ │         │
│    │ ├────────┤ │  │ ├──────┤ │  │     │ │ Doors     │ │         │
│    │ │Ultras. │ │  │ │Steer.│ │  │     └─┴───────────┴─┘         │
│    │ │ ECU    │ │  │ │ ECU  │ │  │                               │
│    │ ├────────┤ │  │ ├──────┤ │  │                               │
│    │ │ IMU    │ │  │ │Throt.│ │  │                               │
│    │ │ ECU    │ │  │ │ ECU  │ │  │                               │
│    │ └────────┘ │  │ └──────┘ │  │                               │
│    └────────────┘  └──────────┘  │                               │
│                                  │                               │
│    ┌─────────────────────────────▼──────────────────────────┐    │
│    │              HIGH-BANDWIDTH SENSOR DOMAIN              │    │
│    │  Camera ECUs │ LiDAR ECU │ GPS/GNSS                    │    │
│    │  Automotive Ethernet (10GBASE-T1 / AVB / TSN)          │    │
│    └────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

### Network Roles Summary

| Network         | Typical Bandwidth | Primary Use in AV                              |
|-----------------|-------------------|------------------------------------------------|
| CAN 2.0         | 1 Mbit/s          | Chassis control, body electronics, legacy ECUs |
| CAN FD          | 5–8 Mbit/s        | ADAS sensors (radar, IMU), perception domain   |
| LIN             | 20 kbit/s         | Simple peripherals (window switches, mirrors)  |
| 100BASE-T1      | 100 Mbit/s        | Cameras, domain controllers, V2X               |
| 1000BASE-T1     | 1 Gbit/s          | LiDAR, multi-camera fusion, HPC backbone       |
| 10GBASE-T1      | 10 Gbit/s         | Raw LiDAR point clouds, high-res cameras       |

---

## 3. CAN's Role in ADAS and Self-Driving Systems

### 3.1 Vehicle Dynamics and Actuator Control

The most **safety-critical** messages in any AV are actuator commands: steering angle, brake pressure, throttle position. These travel over **CAN** because:

- Sub-1 ms latency is achievable and deterministic
- CAN's bus arbitration guarantees that highest-priority messages (e.g., emergency brake) always win
- ISO 26262 ASIL-D certification pathways are well-established for CAN ECUs
- ECU hardware is mature, inexpensive, and robust

Example ADAS actuator CAN message layout (SAE J1939 / proprietary hybrid):

```
CAN ID: 0x200  (Longitudinal Control Command)
DLC: 8
Bytes:
  [0-1]: Target acceleration (int16, 0.01 m/s² per LSB, signed)
  [2-3]: Target brake pressure (uint16, 0.1 bar per LSB)
  [4]:   Control mode flags
           bit0: ACC active
           bit1: AEB active
           bit2: Driver override detected
  [5]:   Checksum (XOR bytes 0-4)
  [6-7]: Counter (rolling 0-255, prevents replay attacks)

CAN ID: 0x201  (Lateral Control Command)
DLC: 8
Bytes:
  [0-1]: Steering angle request (int16, 0.1° per LSB, signed)
  [2-3]: Steering rate limit (uint16, 0.1°/s per LSB)
  [4]:   Lane keeping mode
  [5]:   Checksum
  [6-7]: Counter
```

### 3.2 ADAS Sensor Data over CAN FD

Low-bandwidth sensor data that does NOT need Ethernet goes over CAN FD:

- **Radar**: Object list (distance, velocity, angle, RCS) — typically 10–20 objects per cycle at 20 Hz fits comfortably in CAN FD frames
- **Ultrasonic**: 12 sensors, distance readings, object flags — well within CAN bandwidth
- **IMU**: 6-DOF (3-axis accelerometer + 3-axis gyroscope) at 100 Hz — fits in CAN FD
- **Wheel speed sensors**: 4 channels at 100 Hz — standard CAN
- **GNSS fix**: Position, velocity, heading, accuracy — CAN FD

High-bandwidth sensors (cameras, LiDAR) stream raw data over Automotive Ethernet and only send **processed results** (object lists, segmentation maps) over CAN/CAN FD to actuator ECUs.

### 3.3 Diagnostic and OTA Update Channels

CAN also serves as the backbone for:

- **UDS (ISO 14229)** diagnostics — ECU health, fault codes, calibration
- **OTA (Over-The-Air) updates** via DoIP (Diagnostics over IP) — bridged to CAN through a gateway
- **Event logging** — DTC (Diagnostic Trouble Code) generation and storage

---

## 4. CAN and Automotive Ethernet Integration

### 4.1 The Gateway ECU

The **central gateway** (CGW) or **domain controller** is the key integration point. It bridges CAN/CAN FD buses to the Automotive Ethernet backbone and performs:

- **Protocol translation**: Convert SOME/IP service data to CAN frames and vice versa
- **Filtering and routing**: Only relevant data crosses domain boundaries
- **Time correlation**: Align CAN timestamps with IEEE 802.1AS (gPTP) Ethernet time
- **Security**: Firewall between domains, message authentication

```
┌──────────────────────────────────────────────────────┐
│                  GATEWAY ECU                          │
│                                                       │
│  ┌─────────────┐         ┌──────────────────────┐    │
│  │  CAN Stack  │◄───────►│  Ethernet Stack       │    │
│  │  (CAN FD)   │  Route  │  (SOME/IP / DDS)     │    │
│  │             │  Table  │                      │    │
│  │  Rx Filter  │         │  Service Discovery   │    │
│  │  Tx Sched.  │         │  AVB/TSN Shaping     │    │
│  └──────┬──────┘         └──────────┬───────────┘    │
│         │                           │                 │
│  ┌──────▼───────────────────────────▼───────────┐    │
│  │           ROUTING / TRANSLATION CORE          │    │
│  │  Signal extraction │ PDU mapping │ Timestamp  │    │
│  └───────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────┘
```

### 4.2 PDU (Protocol Data Unit) Mapping

AUTOSAR (AUTomotive Open System ARchitecture) defines how signals are packed into PDUs and routed:

- **I-PDU**: Interaction-Layer PDU — a collection of signals packed per DBC/ARXML
- **N-PDU**: Network-Layer PDU — the CAN frame or Ethernet payload carrying the I-PDU
- **COM module**: Handles signal packing, filtering, timeout monitoring
- **PduR (PDU Router)**: Routes PDUs between COM, CANTP, SomeIpTp based on routing tables

### 4.3 SOME/IP over Ethernet for Sensor Services

High-level AV services (camera streams, LiDAR point cloud services, GNSS services) are exposed as **SOME/IP services** over Automotive Ethernet. Only derived/fused results flow back onto CAN:

```
Camera Service (SOME/IP, 1000BASE-T1)
    │
    │  Object detection results (class, bounding box, confidence)
    ▼
Fusion ECU  ──────────►  CAN FD Frame  ──────────►  Brake ECU
              Object list                 Collision
              (processed)                warning command
```

---

## 5. Sensor Fusion over CAN

### 5.1 Fusion Architecture

Sensor fusion in AVs happens at multiple levels:

**Level 1 – Raw data fusion** (Ethernet domain): Camera + LiDAR point clouds are fused in perception HPC. This is purely Ethernet-based.

**Level 2 – Object list fusion** (CAN FD): Radar object lists + ultrasonic proximity + camera bounding boxes are fused into a unified object model. Results go onto CAN FD.

**Level 3 – Decision fusion** (CAN): Fused object model + path planning output = actuator commands on CAN.

### 5.2 Radar Object List over CAN FD

A typical automotive radar (e.g., Bosch SRR3, Continental ARS540) outputs an object list over CAN FD:

```
Frame 1: Object Header
  CAN ID: 0x300
  Payload (64 bytes, CAN FD):
    [0]:   Number of valid objects (0–48)
    [1]:   Cycle counter
    [2]:   Sensor status flags
    [3-7]: Timestamp (uint40, microseconds, synchronized to gPTP)

Frames 2..N: Object Data (one frame per object)
  CAN ID: 0x301
  Payload (32 bytes, CAN FD):
    [0]:   Object ID (0–255)
    [1]:   Object class (car/truck/pedestrian/cyclist/unknown)
    [2-3]: Longitudinal distance (int16, 0.01 m, signed)
    [4-5]: Lateral distance (int16, 0.01 m, signed)
    [6-7]: Longitudinal velocity (int16, 0.01 m/s, signed)
    [8-9]: Lateral velocity (int16, 0.01 m/s, signed)
    [10]:  RCS (Radar Cross Section, dBm²)
    [11]:  Probability of existence (0–100%)
    [12]:  Measurement quality flag
    [13-15]: Reserved
```

### 5.3 Kalman Filter Fusion Concept

Each CAN FD radar frame feeds a **tracking filter** that maintains object tracks across frames. In a simplified 2D constant-velocity model:

```
State vector: x = [px, py, vx, vy]'
Measurement:  z = [px_meas, py_meas, vx_meas, vy_meas]'

Predict:  x_pred = F * x_prev     (F = state transition matrix)
          P_pred = F * P * F' + Q (Q = process noise)

Update:   K = P_pred * H' * inv(H * P_pred * H' + R)
          x = x_pred + K * (z - H * x_pred)
          P = (I - K * H) * P_pred
```

This runs at the fusion ECU on every radar CAN FD cycle (typically 20 ms / 50 Hz).

---

## 6. ADAS-Specific CAN Message Design

### 6.1 SAE J2735 and AUTOSAR Compliance

ADAS CAN databases (`.dbc` files) for autonomous vehicles typically encode:

- **ADAS object lists**: Fused track data per detected object
- **Ego vehicle state**: Speed, yaw rate, steering angle, acceleration
- **Environment model**: Lane markings, road curvature, traffic signs
- **System state**: ADAS mode, driver monitoring status, takeover requests

### 6.2 Example DBC Signal Definitions

```dbc
VERSION ""

BO_ 512 ADAS_EgoVehicle: 8 FCU
 SG_ EgoSpeed : 0|16@1+ (0.01,0) [0|327.67] "km/h" Vector__XXX
 SG_ EgoYawRate : 16|16@1- (0.01,0) [-327.68|327.67] "deg/s" Vector__XXX
 SG_ EgoSteerAngle : 32|16@1- (0.1,0) [-3276.8|3276.7] "deg" Vector__XXX
 SG_ ADASMode : 48|4@1+ (1,0) [0|15] "" Vector__XXX
 SG_ DriverOverride : 52|1@1+ (1,0) [0|1] "" Vector__XXX
 SG_ Checksum : 56|8@1+ (1,0) [0|255] "" Vector__XXX

BO_ 513 ADAS_TargetObject: 8 FCU
 SG_ ObjID : 0|8@1+ (1,0) [0|255] "" Vector__XXX
 SG_ ObjClass : 8|4@1+ (1,0) [0|15] "" Vector__XXX
 SG_ ObjDistLong : 12|12@1- (0.05,0) [-102.4|102.35] "m" Vector__XXX
 SG_ ObjDistLat : 24|12@1- (0.05,0) [-102.4|102.35] "m" Vector__XXX
 SG_ ObjVelLong : 36|12@1- (0.05,0) [-102.4|102.35] "m/s" Vector__XXX
 SG_ ObjTTC : 48|8@1+ (0.1,0) [0|25.5] "s" Vector__XXX

BO_ 768 AEB_Command: 8 FCU
 SG_ AEB_Active : 0|1@1+ (1,0) [0|1] "" BrakeECU
 SG_ AEB_DecelRequest : 1|12@1+ (0.01,0) [0|40.95] "m/s2" BrakeECU
 SG_ ACC_Active : 13|1@1+ (1,0) [0|1] "" BrakeECU
 SG_ ACC_SpeedTarget : 14|12@1+ (0.1,0) [0|409.5] "km/h" BrakeECU
```

---

## 7. Safety and Fault Tolerance in Autonomous CAN Networks

### 7.1 ISO 26262 Requirements

Autonomous driving systems are classified **ASIL-B to ASIL-D** under ISO 26262. For CAN in these systems:

- **Message authentication**: E2E (End-to-End) protection profiles (AUTOSAR E2E Profile 2, Profile 5)
- **Alive counters**: Rolling counters detect lost or duplicated frames
- **CRC checks**: In-message CRC (not the CAN hardware CRC) detect data corruption
- **Timeout monitoring**: Missing messages trigger fallback/safe state
- **Redundant buses**: Dual CAN buses for critical actuator commands

### 7.2 AUTOSAR E2E Profile 2 (for CAN)

E2E Profile 2 adds an 8-bit CRC and an 8-bit counter to each protected PDU:

```
 Byte 0:    Counter (0-255, increments each transmission cycle)
 Byte 1:    CRC (CRC-8/SAE-J1850 over Data ID + Bytes 2..N-1)
 Bytes 2..N: Actual payload data
```

The receiver checks:
1. Counter incremented by exactly 1 (no lost/duplicated frames)
2. CRC matches — data integrity confirmed
3. If either check fails → E2E error reported to safety monitor

### 7.3 Safe State and Fallback

When the autonomous system detects a CAN communication fault:

```
Normal Operation
      │
      ▼
CAN Timeout / E2E Error
      │
      ├──► Increment error counter
      │
      ├──► Error counter > threshold?
      │         YES → Enter Degraded Mode (limit ADAS features)
      │         NO  → Continue with last valid data
      │
      └──► Error counter >> threshold?
                YES → Safe State: Driver takeover request
                      Emergency hazard lights ON
                      Controlled deceleration to stop
```

---

## 8. Programming CAN for ADAS in C/C++

### 8.1 Linux SocketCAN: ADAS Sensor Frame Reception

This example shows reading radar object frames from a CAN FD bus and building a local object track table.

```c
/* adas_radar_receiver.c
 * Receives radar object list over CAN FD (SocketCAN, Linux)
 * and maintains a simple object track table.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

/* ─── CAN IDs ─────────────────────────────────────────────── */
#define CAN_ID_RADAR_HEADER   0x300U
#define CAN_ID_RADAR_OBJECT   0x301U
#define CAN_ID_AEB_COMMAND    0x300U  /* TX: AEB command to brake ECU */

#define MAX_RADAR_OBJECTS     48
#define OBJECT_TIMEOUT_MS     200     /* remove stale tracks after 200 ms */
#define AEB_TTC_THRESHOLD_S   2.0f    /* trigger AEB if TTC < 2 seconds */

/* ─── Data Types ──────────────────────────────────────────── */
typedef struct {
    uint8_t  id;
    uint8_t  obj_class;      /* 0=unknown,1=car,2=truck,3=pedestrian,4=cyclist */
    float    dist_long;      /* m, positive = forward */
    float    dist_lat;       /* m, positive = left */
    float    vel_long;       /* m/s, positive = approaching */
    float    vel_lat;        /* m/s */
    uint8_t  prob_exist;     /* 0-100 % */
    uint64_t timestamp_us;   /* microseconds, gPTP-synchronized */
    uint64_t last_seen_ms;   /* local monotonic clock, for timeout */
    bool     valid;
} RadarObject;

typedef struct {
    uint8_t     num_objects;
    uint8_t     cycle_counter;
    uint64_t    timestamp_us;
    RadarObject objects[MAX_RADAR_OBJECTS];
} RadarObjectList;

/* ─── Utilities ───────────────────────────────────────────── */
static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static float decode_int16_scaled(const uint8_t *buf, size_t offset, float scale)
{
    int16_t raw;
    memcpy(&raw, buf + offset, sizeof(raw));
    return (float)raw * scale;
}

/* ─── E2E Profile 2 CRC-8 (SAE J1850) ────────────────────── */
static const uint8_t CRC8_TABLE[256] = {
    0x00,0x1D,0x3A,0x27,0x74,0x69,0x4E,0x53,
    0xE8,0xF5,0xD2,0xCF,0x9C,0x81,0xA6,0xBB,
    /* ... full 256-entry table omitted for brevity ... */
    /* In production: generate with polynomial 0x1D (SAE J1850) */
};

static uint8_t crc8_j1850(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++)
        crc = CRC8_TABLE[crc ^ data[i]];
    return crc ^ 0xFF;
}

/* ─── Radar Frame Parsing ─────────────────────────────────── */
static void parse_radar_header(const struct canfd_frame *frame,
                               RadarObjectList *list)
{
    if (frame->len < 8) return;
    const uint8_t *d = frame->data;
    list->num_objects  = d[0];
    list->cycle_counter = d[1];
    /* Bytes 3-7: 40-bit timestamp in microseconds */
    list->timestamp_us = ((uint64_t)d[3])
                       | ((uint64_t)d[4] << 8)
                       | ((uint64_t)d[5] << 16)
                       | ((uint64_t)d[6] << 24)
                       | ((uint64_t)d[7] << 32);
}

static bool parse_radar_object(const struct canfd_frame *frame,
                               RadarObjectList *list)
{
    if (frame->len < 14) return false;
    const uint8_t *d = frame->data;

    uint8_t id = d[0];
    if (id >= MAX_RADAR_OBJECTS) return false;

    RadarObject *obj = &list->objects[id];
    obj->id          = id;
    obj->obj_class   = d[1];
    obj->dist_long   = decode_int16_scaled(d, 2, 0.01f);   /* 0.01 m/LSB */
    obj->dist_lat    = decode_int16_scaled(d, 4, 0.01f);
    obj->vel_long    = decode_int16_scaled(d, 6, 0.01f);   /* 0.01 m·s⁻¹/LSB */
    obj->vel_lat     = decode_int16_scaled(d, 8, 0.01f);
    obj->prob_exist  = d[11];
    obj->timestamp_us = list->timestamp_us;
    obj->last_seen_ms = monotonic_ms();
    obj->valid       = true;

    return true;
}

/* ─── Object Track Management ─────────────────────────────── */
static void invalidate_stale_objects(RadarObjectList *list)
{
    uint64_t now = monotonic_ms();
    for (int i = 0; i < MAX_RADAR_OBJECTS; i++) {
        RadarObject *obj = &list->objects[i];
        if (obj->valid && (now - obj->last_seen_ms) > OBJECT_TIMEOUT_MS) {
            obj->valid = false;
            printf("[TRACKER] Object %d timed out\n", i);
        }
    }
}

/* ─── Time-To-Collision Calculation ──────────────────────────*/
static float compute_ttc(const RadarObject *obj)
{
    /* TTC = -dist_long / rel_velocity (for approaching objects) */
    if (obj->vel_long >= 0.0f || obj->dist_long <= 0.0f)
        return 999.0f;  /* not a threat */
    return -obj->dist_long / obj->vel_long;
}

/* ─── AEB Decision and CAN TX ─────────────────────────────── */
static void evaluate_aeb_and_send(int sock, const RadarObjectList *list)
{
    float     min_ttc = 999.0f;
    float     req_decel = 0.0f;
    bool      aeb_active = false;

    for (int i = 0; i < MAX_RADAR_OBJECTS; i++) {
        const RadarObject *obj = &list->objects[i];
        if (!obj->valid) continue;
        float ttc = compute_ttc(obj);
        if (ttc < min_ttc) {
            min_ttc = ttc;
        }
    }

    if (min_ttc < AEB_TTC_THRESHOLD_S) {
        aeb_active = true;
        /* Proportional decel: max 9.81 m/s² at TTC=0, 0 at TTC=threshold */
        req_decel = 9.81f * (1.0f - min_ttc / AEB_TTC_THRESHOLD_S);
        printf("[AEB] ACTIVE: TTC=%.2f s, Decel=%.2f m/s²\n",
               min_ttc, req_decel);
    }

    /* Encode AEB command CAN frame */
    struct canfd_frame tx;
    memset(&tx, 0, sizeof(tx));
    tx.can_id  = CAN_ID_AEB_COMMAND | CAN_EFF_FLAG;
    tx.flags   = CANFD_BRS;
    tx.len     = 8;

    /* Bit 0: AEB active flag */
    tx.data[0] = aeb_active ? 0x01 : 0x00;

    /* Bits 1-12: Decel request (uint12, 0.01 m/s²/LSB) */
    uint16_t decel_raw = (uint16_t)(req_decel / 0.01f);
    decel_raw &= 0x0FFF;
    tx.data[0] |= (uint8_t)((decel_raw & 0x07) << 1);
    tx.data[1]  = (uint8_t)(decel_raw >> 3);

    /* Simple XOR checksum over bytes 0-6 */
    tx.data[7] = 0;
    for (int i = 0; i < 7; i++) tx.data[7] ^= tx.data[i];

    if (write(sock, &tx, sizeof(struct canfd_frame)) < 0)
        perror("CAN TX AEB command failed");
}

/* ─── Main Loop ───────────────────────────────────────────── */
int main(void)
{
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return 1; }

    /* Enable CAN FD frames */
    int enable_canfd = 1;
    if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                   &enable_canfd, sizeof(enable_canfd)) < 0) {
        perror("setsockopt CAN FD"); return 1;
    }

    /* Bind to can0 */
    struct ifreq ifr;
    strncpy(ifr.ifr_name, "can0", IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl"); return 1; }

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    /* Set receive filter for radar frames only */
    struct can_filter filters[] = {
        { .can_id = CAN_ID_RADAR_HEADER, .can_mask = CAN_SFF_MASK },
        { .can_id = CAN_ID_RADAR_OBJECT, .can_mask = CAN_SFF_MASK },
    };
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER,
               filters, sizeof(filters));

    RadarObjectList track_list;
    memset(&track_list, 0, sizeof(track_list));

    printf("[ADAS] Radar receiver started on can0\n");

    while (1) {
        struct canfd_frame frame;
        ssize_t nbytes = read(sock, &frame, sizeof(frame));
        if (nbytes < 0) {
            if (errno == EINTR) continue;
            perror("read"); break;
        }

        uint32_t id = frame.can_id & CAN_EFF_MASK;

        if (id == CAN_ID_RADAR_HEADER) {
            parse_radar_header(&frame, &track_list);
            invalidate_stale_objects(&track_list);
        } else if (id == CAN_ID_RADAR_OBJECT) {
            if (parse_radar_object(&frame, &track_list)) {
                /* After parsing each object, evaluate AEB */
                evaluate_aeb_and_send(sock, &track_list);
            }
        }
    }

    close(sock);
    return 0;
}
```

---

### 8.2 C++: Sensor Fusion ECU — ADAS Object Fusion Manager

This C++ example shows an object fusion class that merges radar tracks with camera detections arriving via a gateway-translated CAN FD message:

```cpp
// adas_fusion.hpp
// ADAS Sensor Fusion: merge radar (CAN FD) + camera detections (gateway-routed)

#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <optional>
#include <chrono>
#include <vector>
#include <iostream>
#include <format>

/* ─── Constants ──────────────────────────────────────────── */
static constexpr size_t  MAX_OBJECTS    = 48;
static constexpr float   ASSOC_GATE_M   = 3.0f;   /* Mahalanobis gate [m]  */
static constexpr int64_t TRACK_TTL_MS   = 300;     /* track lifetime [ms]   */

/* ─── Object Class Enum ───────────────────────────────────── */
enum class ObjectClass : uint8_t {
    Unknown     = 0,
    Car         = 1,
    Truck       = 2,
    Pedestrian  = 3,
    Cyclist     = 4,
};

/* ─── Sensor Source Bitmask ───────────────────────────────── */
enum class SensorSource : uint8_t {
    None   = 0x00,
    Radar  = 0x01,
    Camera = 0x02,
    Fused  = Radar | Camera,
};

/* ─── 2D Kalman State: [px, py, vx, vy] ─────────────────── */
struct KalmanState {
    float px = 0.0f, py = 0.0f;
    float vx = 0.0f, vy = 0.0f;

    /* 4×4 covariance matrix (row-major, symmetric) */
    std::array<float, 16> P = {
        4.0f, 0,    0,    0,
        0,    4.0f, 0,    0,
        0,    0,    2.0f, 0,
        0,    0,    0,    2.0f
    };
};

/* ─── Fused Object Track ──────────────────────────────────── */
struct FusedObject {
    uint8_t      id         = 0;
    ObjectClass  cls        = ObjectClass::Unknown;
    SensorSource sources    = SensorSource::None;
    KalmanState  state;
    float        prob_exist = 0.0f;
    int64_t      last_update_ms = 0;
    bool         valid       = false;

    float dist_long() const { return state.px; }
    float dist_lat()  const { return state.py; }
    float vel_long()  const { return state.vx; }
    float vel_lat()   const { return state.vy; }

    float ttc() const {
        if (state.vx >= 0.0f || state.px <= 0.0f) return 999.0f;
        return -state.px / state.vx;
    }
};

/* ─── Measurement types ───────────────────────────────────── */
struct RadarMeasurement {
    float dist_long, dist_lat;
    float vel_long,  vel_lat;
    float prob_exist;
    ObjectClass cls;
    int64_t timestamp_ms;
};

struct CameraMeasurement {
    float dist_long, dist_lat;
    ObjectClass cls;
    float confidence;
    int64_t timestamp_ms;
};

/* ─── Fusion ECU ──────────────────────────────────────────── */
class FusionECU {
public:
    FusionECU() {
        for (auto& obj : objects_) obj.valid = false;
    }

    /* ── Process a radar measurement (from CAN FD frame) ──── */
    void process_radar(const RadarMeasurement& meas)
    {
        size_t idx = find_or_create_track(meas.dist_long, meas.dist_lat);
        FusedObject& obj = objects_[idx];

        /* Kalman update with radar measurement [px, py, vx, vy] */
        kalman_update(obj.state,
                      meas.dist_long, meas.dist_lat,
                      meas.vel_long,  meas.vel_lat,
                      /* R diag */ 0.25f, 0.25f, 0.09f, 0.09f);

        obj.sources    = static_cast<SensorSource>(
                            static_cast<uint8_t>(obj.sources) |
                            static_cast<uint8_t>(SensorSource::Radar));
        obj.prob_exist = std::max(obj.prob_exist, meas.prob_exist);
        if (obj.cls == ObjectClass::Unknown) obj.cls = meas.cls;
        obj.last_update_ms = meas.timestamp_ms;
        obj.valid = true;
    }

    /* ── Process a camera detection (gateway-routed, CAN FD) ─ */
    void process_camera(const CameraMeasurement& meas)
    {
        size_t idx = find_or_create_track(meas.dist_long, meas.dist_lat);
        FusedObject& obj = objects_[idx];

        /* Camera has no velocity measurement — position-only update */
        kalman_update_position(obj.state,
                               meas.dist_long, meas.dist_lat,
                               /* R diag */ 1.0f, 1.0f);

        obj.sources    = static_cast<SensorSource>(
                            static_cast<uint8_t>(obj.sources) |
                            static_cast<uint8_t>(SensorSource::Camera));
        /* Camera class is often more reliable than radar for pedestrians */
        if (meas.cls == ObjectClass::Pedestrian ||
            meas.cls == ObjectClass::Cyclist)
            obj.cls = meas.cls;
        obj.last_update_ms = meas.timestamp_ms;
        obj.valid = true;
    }

    /* ── Propagate all tracks to current time ─────────────── */
    void predict(float dt_s)
    {
        for (auto& obj : objects_) {
            if (!obj.valid) continue;
            /* Constant velocity prediction */
            obj.state.px += obj.state.vx * dt_s;
            obj.state.py += obj.state.vy * dt_s;
            /* Grow covariance: Q process noise */
            constexpr float q = 0.5f;
            obj.state.P[0]  += q * dt_s * dt_s;
            obj.state.P[5]  += q * dt_s * dt_s;
            obj.state.P[10] += q;
            obj.state.P[15] += q;
        }
    }

    /* ── Remove expired tracks ────────────────────────────── */
    void prune_stale(int64_t now_ms)
    {
        for (auto& obj : objects_) {
            if (obj.valid && (now_ms - obj.last_update_ms) > TRACK_TTL_MS) {
                obj.valid = false;
                std::cout << std::format("[FUSION] Track {} expired\n", obj.id);
            }
        }
    }

    /* ── Get most critical object (lowest TTC) ────────────── */
    std::optional<FusedObject> most_critical_object() const
    {
        float best_ttc = 999.0f;
        std::optional<FusedObject> best;
        for (const auto& obj : objects_) {
            if (!obj.valid) continue;
            if (float t = obj.ttc(); t < best_ttc) {
                best_ttc = t;
                best = obj;
            }
        }
        return best;
    }

    /* ── Encode fused object list to CAN FD payload ──────── */
    std::vector<std::array<uint8_t, 16>> encode_object_list() const
    {
        std::vector<std::array<uint8_t, 16>> frames;
        for (const auto& obj : objects_) {
            if (!obj.valid) continue;
            std::array<uint8_t, 16> frame{};
            frame[0] = obj.id;
            frame[1] = static_cast<uint8_t>(obj.cls);

            auto encode_int16 = [&](float val, float scale, size_t off) {
                int16_t raw = static_cast<int16_t>(val / scale);
                memcpy(&frame[off], &raw, 2);
            };
            encode_int16(obj.dist_long(), 0.01f, 2);
            encode_int16(obj.dist_lat(),  0.01f, 4);
            encode_int16(obj.vel_long(),  0.01f, 6);
            encode_int16(obj.vel_lat(),   0.01f, 8);
            frame[10] = static_cast<uint8_t>(obj.prob_exist * 100.0f);
            frame[11] = static_cast<uint8_t>(obj.sources);

            /* TTC in 0.1s/LSB, clamped to uint8 */
            float ttc_val = std::clamp(obj.ttc(), 0.0f, 25.5f);
            frame[12] = static_cast<uint8_t>(ttc_val / 0.1f);

            frames.push_back(frame);
        }
        return frames;
    }

private:
    std::array<FusedObject, MAX_OBJECTS> objects_;
    uint8_t next_id_ = 0;

    /* ── Simple nearest-neighbor association gate ─────────── */
    size_t find_or_create_track(float px, float py)
    {
        float  best_dist = ASSOC_GATE_M;
        size_t best_idx  = MAX_OBJECTS;

        for (size_t i = 0; i < MAX_OBJECTS; i++) {
            if (!objects_[i].valid) continue;
            float dx = objects_[i].state.px - px;
            float dy = objects_[i].state.py - py;
            float d  = std::sqrt(dx*dx + dy*dy);
            if (d < best_dist) {
                best_dist = d;
                best_idx  = i;
            }
        }

        if (best_idx != MAX_OBJECTS) return best_idx;

        /* Create new track in first free slot */
        for (size_t i = 0; i < MAX_OBJECTS; i++) {
            if (!objects_[i].valid) {
                objects_[i] = FusedObject{};
                objects_[i].id    = next_id_++;
                objects_[i].state.px = px;
                objects_[i].state.py = py;
                return i;
            }
        }
        /* All slots full: overwrite oldest (simplified) */
        return 0;
    }

    /* ── Kalman update (full 4D: position + velocity) ──────── */
    void kalman_update(KalmanState& s,
                       float px_m, float py_m, float vx_m, float vy_m,
                       float r_px, float r_py, float r_vx, float r_vy)
    {
        /* Innovation */
        float y[4] = { px_m - s.px, py_m - s.py,
                       vx_m - s.vx, vy_m - s.vy };

        /* S = H*P*H' + R  (H=I for full-state measurement) */
        float S[4] = { s.P[0]  + r_px,
                       s.P[5]  + r_py,
                       s.P[10] + r_vx,
                       s.P[15] + r_vy };

        /* Kalman gain K = P*H' * S^-1  (diagonal case) */
        float K[4] = { s.P[0]  / S[0],
                       s.P[5]  / S[1],
                       s.P[10] / S[2],
                       s.P[15] / S[3] };

        /* State update */
        s.px += K[0] * y[0];
        s.py += K[1] * y[1];
        s.vx += K[2] * y[2];
        s.vy += K[3] * y[3];

        /* Covariance update  P = (I - K*H) * P */
        s.P[0]  *= (1.0f - K[0]);
        s.P[5]  *= (1.0f - K[1]);
        s.P[10] *= (1.0f - K[2]);
        s.P[15] *= (1.0f - K[3]);
    }

    /* ── Kalman update (position only: camera) ─────────────── */
    void kalman_update_position(KalmanState& s,
                                float px_m, float py_m,
                                float r_px, float r_py)
    {
        float yx = px_m - s.px;
        float yy = py_m - s.py;
        float Sx = s.P[0]  + r_px;
        float Sy = s.P[5]  + r_py;
        float Kx = s.P[0]  / Sx;
        float Ky = s.P[5]  / Sy;
        s.px += Kx * yx;
        s.py += Ky * yy;
        s.P[0]  *= (1.0f - Kx);
        s.P[5]  *= (1.0f - Ky);
        /* Also correct velocity via cross-covariance */
        float Kvx = s.P[8]  / Sx;
        float Kvy = s.P[13] / Sy;
        s.vx += Kvx * yx;
        s.vy += Kvy * yy;
    }
};

/* ─── Usage Example ──────────────────────────────────────── */
int main()
{
    FusionECU fusion;

    /* Simulate incoming radar measurement at t=0 */
    RadarMeasurement rm1{
        .dist_long   = 25.0f,  /* 25 m ahead */
        .dist_lat    = -0.5f,  /* slightly right */
        .vel_long    = -8.0f,  /* approaching at 8 m/s */
        .vel_lat     = 0.0f,
        .prob_exist  = 0.90f,
        .cls         = ObjectClass::Car,
        .timestamp_ms = 1000,
    };
    fusion.process_radar(rm1);

    /* Simulate camera detection of same object */
    CameraMeasurement cm1{
        .dist_long    = 24.8f,
        .dist_lat     = -0.6f,
        .cls          = ObjectClass::Car,
        .confidence   = 0.92f,
        .timestamp_ms = 1005,
    };
    fusion.process_camera(cm1);

    /* Predict 50 ms forward */
    fusion.predict(0.050f);

    /* Evaluate criticality */
    if (auto critical = fusion.most_critical_object()) {
        std::cout << std::format(
            "[FUSION] Critical object: class={} dist={:.1f}m vel={:.1f}m/s TTC={:.2f}s\n",
            static_cast<int>(critical->cls),
            critical->dist_long(),
            critical->vel_long(),
            critical->ttc()
        );

        if (critical->ttc() < 2.0f)
            std::cout << "[AEB] EMERGENCY BRAKE REQUESTED\n";
    }

    /* Get CAN FD payload for transmission to actuator domain */
    auto frames = fusion.encode_object_list();
    std::cout << std::format("[TX] {} object frames ready for CAN FD\n",
                             frames.size());
    return 0;
}
```

---

## 9. Programming CAN for ADAS in Rust

### 9.1 Rust: Radar CAN FD Frame Parser

```rust
// radar_parser.rs
// Parses CAN FD radar object frames (socketcan-rs crate)
// Crates: socketcan = "3", embedded-can = "0.4"

use std::time::{Duration, Instant};
use socketcan::{CanFdFrame, CanFdSocket, Socket, SocketOptions};

/* ─── Constants ─────────────────────────────────────────── */
const CAN_ID_RADAR_HEADER: u32 = 0x300;
const CAN_ID_RADAR_OBJECT: u32 = 0x301;
const MAX_OBJECTS: usize       = 48;
const OBJECT_TTL: Duration     = Duration::from_millis(200);
const AEB_TTC_THRESHOLD: f32   = 2.0; // seconds

/* ─── Object Class ───────────────────────────────────────── */
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum ObjectClass {
    #[default]
    Unknown    = 0,
    Car        = 1,
    Truck      = 2,
    Pedestrian = 3,
    Cyclist    = 4,
}

impl From<u8> for ObjectClass {
    fn from(v: u8) -> Self {
        match v {
            1 => Self::Car,
            2 => Self::Truck,
            3 => Self::Pedestrian,
            4 => Self::Cyclist,
            _ => Self::Unknown,
        }
    }
}

/* ─── Radar Object ───────────────────────────────────────── */
#[derive(Debug, Clone)]
pub struct RadarObject {
    pub id:           u8,
    pub class:        ObjectClass,
    pub dist_long:    f32,    // metres, + = ahead
    pub dist_lat:     f32,    // metres, + = left
    pub vel_long:     f32,    // m/s, negative = approaching
    pub vel_lat:      f32,    // m/s
    pub prob_exist:   u8,     // 0-100 %
    pub timestamp_us: u64,
    pub last_seen:    Instant,
}

impl RadarObject {
    /// Time-To-Collision in seconds (999 = not a threat)
    pub fn ttc(&self) -> f32 {
        if self.vel_long >= 0.0 || self.dist_long <= 0.0 {
            return 999.0;
        }
        -self.dist_long / self.vel_long
    }

    pub fn is_stale(&self) -> bool {
        self.last_seen.elapsed() > OBJECT_TTL
    }
}

/* ─── Radar Header ───────────────────────────────────────── */
#[derive(Debug, Default)]
pub struct RadarHeader {
    pub num_objects:   u8,
    pub cycle_counter: u8,
    pub timestamp_us:  u64,
}

/* ─── Parse Header Frame ─────────────────────────────────── */
fn parse_radar_header(data: &[u8]) -> Option<RadarHeader> {
    if data.len() < 8 { return None; }
    let ts_us = u64::from_le_bytes([
        data[3], data[4], data[5], data[6], data[7], 0, 0, 0
    ]);
    Some(RadarHeader {
        num_objects:   data[0],
        cycle_counter: data[1],
        timestamp_us:  ts_us,
    })
}

/* ─── Parse Object Frame ─────────────────────────────────── */
fn parse_radar_object(data: &[u8], ts_us: u64) -> Option<RadarObject> {
    if data.len() < 14 { return None; }

    let decode_i16 = |off: usize, scale: f32| -> f32 {
        let raw = i16::from_le_bytes([data[off], data[off + 1]]);
        raw as f32 * scale
    };

    Some(RadarObject {
        id:           data[0],
        class:        ObjectClass::from(data[1]),
        dist_long:    decode_i16(2, 0.01),
        dist_lat:     decode_i16(4, 0.01),
        vel_long:     decode_i16(6, 0.01),
        vel_lat:      decode_i16(8, 0.01),
        prob_exist:   data[11],
        timestamp_us: ts_us,
        last_seen:    Instant::now(),
    })
}

/* ─── AEB Command Encoder ────────────────────────────────── */
fn encode_aeb_command(active: bool, decel_m_s2: f32) -> [u8; 8] {
    let mut frame = [0u8; 8];
    let decel_raw = ((decel_m_s2 / 0.01) as u16).min(0x0FFF);

    frame[0] = if active { 0x01 } else { 0x00 };
    frame[0] |= ((decel_raw & 0x07) as u8) << 1;
    frame[1]  = (decel_raw >> 3) as u8;

    // XOR checksum over bytes 0-6
    frame[7] = frame[..7].iter().fold(0u8, |acc, &b| acc ^ b);
    frame
}

/* ─── Object Track Table ─────────────────────────────────── */
pub struct TrackTable {
    objects: Box<[Option<RadarObject>; MAX_OBJECTS]>,
}

impl TrackTable {
    pub fn new() -> Self {
        Self {
            objects: Box::new(std::array::from_fn(|_| None)),
        }
    }

    pub fn update(&mut self, obj: RadarObject) {
        let idx = obj.id as usize;
        if idx < MAX_OBJECTS {
            self.objects[idx] = Some(obj);
        }
    }

    pub fn prune_stale(&mut self) {
        for slot in self.objects.iter_mut() {
            if let Some(obj) = slot {
                if obj.is_stale() {
                    println!("[TRACKER] Object {} timed out", obj.id);
                    *slot = None;
                }
            }
        }
    }

    pub fn most_critical(&self) -> Option<&RadarObject> {
        self.objects
            .iter()
            .flatten()
            .min_by(|a, b| a.ttc().partial_cmp(&b.ttc()).unwrap())
    }

    pub fn valid_count(&self) -> usize {
        self.objects.iter().filter(|s| s.is_some()).count()
    }
}

/* ─── Main Receive Loop ───────────────────────────────────── */
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let sock = CanFdSocket::open("can0")?;
    sock.set_read_timeout(Some(Duration::from_millis(100)))?;

    let mut tracks = TrackTable::new();
    let mut current_ts_us: u64 = 0;

    println!("[ADAS] Radar receiver started on can0");

    loop {
        match sock.read_frame() {
            Ok(frame) => {
                let can_id = frame.raw_id() & 0x1FFF_FFFF;
                let data   = frame.data();

                if can_id == CAN_ID_RADAR_HEADER {
                    if let Some(hdr) = parse_radar_header(data) {
                        current_ts_us = hdr.timestamp_us;
                        tracks.prune_stale();
                        println!("[HEADER] cycle={} objects={} ts={}µs",
                                 hdr.cycle_counter, hdr.num_objects, hdr.timestamp_us);
                    }
                } else if can_id == CAN_ID_RADAR_OBJECT {
                    if let Some(obj) = parse_radar_object(data, current_ts_us) {
                        let ttc = obj.ttc();
                        println!("[OBJ {}] cls={:?} dist_long={:.1}m vel={:.1}m/s TTC={:.2}s",
                                 obj.id, obj.class, obj.dist_long, obj.vel_long, ttc);
                        tracks.update(obj);
                    }
                }

                /* AEB evaluation after every object update */
                if let Some(critical) = tracks.most_critical() {
                    let ttc = critical.ttc();
                    if ttc < AEB_TTC_THRESHOLD {
                        let decel = 9.81 * (1.0 - ttc / AEB_TTC_THRESHOLD);
                        let cmd = encode_aeb_command(true, decel);
                        println!("[AEB] ACTIVE TTC={:.2}s decel={:.2}m/s² frame={cmd:?}",
                                 ttc, decel);
                        // In production: sock.write_frame(aeb_can_fd_frame)
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                /* Timeout — normal, continue */
            }
            Err(e) => return Err(e.into()),
        }
    }
}
```

---

### 9.2 Rust: ADAS Fusion ECU with E2E Safety Checks

```rust
// fusion_ecu.rs
// Sensor fusion ECU: Kalman-filter-based track management with
// AUTOSAR E2E Profile 2 integrity checking for incoming CAN FD frames.

use std::time::Instant;

/* ─── E2E Profile 2 CRC-8 (SAE J1850 poly=0x1D) ────────── */
fn crc8_j1850(data: &[u8]) -> u8 {
    const POLY: u8 = 0x1D;
    let mut crc: u8 = 0xFF;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    crc ^ 0xFF
}

/* ─── E2E check result ───────────────────────────────────── */
#[derive(Debug, PartialEq)]
pub enum E2EStatus {
    Ok,
    CrcError,
    CounterError { expected: u8, got: u8 },
    DataIdError,
}

/// Verify AUTOSAR E2E Profile 2 header on a CAN FD payload.
/// Layout: [counter: u8][crc: u8][payload...]
pub fn e2e_check(data: &[u8], data_id: u16, expected_counter: u8) -> E2EStatus {
    if data.len() < 2 {
        return E2EStatus::DataIdError;
    }
    let counter = data[0];
    let received_crc = data[1];

    // Counter must increment by 1 (wrapping)
    if counter != expected_counter.wrapping_add(1) {
        return E2EStatus::CounterError {
            expected: expected_counter.wrapping_add(1),
            got: counter,
        };
    }

    // CRC covers: data_id_low, data_id_high, counter, payload[2..]
    let mut crc_input = Vec::with_capacity(data.len() + 2);
    crc_input.push((data_id & 0xFF) as u8);
    crc_input.push((data_id >> 8)   as u8);
    crc_input.push(counter);
    crc_input.extend_from_slice(&data[2..]);
    let computed_crc = crc8_j1850(&crc_input);

    if computed_crc != received_crc {
        return E2EStatus::CrcError;
    }

    E2EStatus::Ok
}

/* ─── 2-D Kalman State ───────────────────────────────────── */
#[derive(Debug, Clone, Default)]
pub struct KalmanState {
    pub px: f32, pub py: f32,
    pub vx: f32, pub vy: f32,
    /// Diagonal variances [σ²_px, σ²_py, σ²_vx, σ²_vy]
    pub cov: [f32; 4],
}

impl KalmanState {
    pub fn new(px: f32, py: f32) -> Self {
        Self {
            px, py,
            vx: 0.0, vy: 0.0,
            cov: [4.0, 4.0, 2.0, 2.0], // initial uncertainty
        }
    }

    /// Constant-velocity prediction over dt seconds.
    pub fn predict(&mut self, dt: f32) {
        self.px += self.vx * dt;
        self.py += self.vy * dt;
        // Grow uncertainty with process noise
        const Q: f32 = 0.5;
        self.cov[0] += Q * dt * dt;
        self.cov[1] += Q * dt * dt;
        self.cov[2] += Q;
        self.cov[3] += Q;
    }

    /// Update with full state measurement [px, py, vx, vy] + noise R.
    pub fn update_full(&mut self,
                       meas: [f32; 4],
                       r: [f32; 4])
    {
        for i in 0..4 {
            let k = self.cov[i] / (self.cov[i] + r[i]);
            let state = [&mut self.px, &mut self.py,
                         &mut self.vx, &mut self.vy][i];
            *state += k * (meas[i] - *state);
            self.cov[i] *= 1.0 - k;
        }
    }

    /// Update with position-only measurement [px, py].
    pub fn update_position(&mut self, px: f32, py: f32, r: [f32; 2]) {
        let kx = self.cov[0] / (self.cov[0] + r[0]);
        let ky = self.cov[1] / (self.cov[1] + r[1]);
        let inno_x = px - self.px;
        let inno_y = py - self.py;
        self.px += kx * inno_x;
        self.py += ky * inno_y;
        self.vx += (self.cov[0] / (self.cov[0] + r[0])) * inno_x * 0.1;
        self.vy += (self.cov[1] / (self.cov[1] + r[1])) * inno_y * 0.1;
        self.cov[0] *= 1.0 - kx;
        self.cov[1] *= 1.0 - ky;
    }

    pub fn ttc(&self) -> f32 {
        if self.vx >= 0.0 || self.px <= 0.0 { return 999.0; }
        -self.px / self.vx
    }
}

/* ─── Track ──────────────────────────────────────────────── */
#[derive(Debug, Clone)]
pub struct Track {
    pub id:          u8,
    pub state:       KalmanState,
    pub last_update: Instant,
    pub sources:     u8,    // bitmask: 0x01=radar, 0x02=camera
    pub obj_class:   u8,
}

impl Track {
    pub fn is_stale(&self) -> bool {
        self.last_update.elapsed().as_millis() > 300
    }
}

/* ─── Fusion ECU ─────────────────────────────────────────── */
pub struct FusionEcu {
    tracks: Vec<Track>,
    next_id: u8,
    e2e_counters: std::collections::HashMap<u16, u8>, // data_id → last counter
}

impl FusionEcu {
    pub fn new() -> Self {
        Self {
            tracks: Vec::with_capacity(48),
            next_id: 0,
            e2e_counters: Default::default(),
        }
    }

    /// Process an E2E-protected CAN FD frame from a radar ECU.
    /// Returns Err if E2E check fails (message dropped).
    pub fn process_radar_frame(
        &mut self,
        data_id: u16,
        payload: &[u8],
    ) -> Result<(), E2EStatus> {
        let last_ctr = *self.e2e_counters.get(&data_id).unwrap_or(&0xFF);
        let status = e2e_check(payload, data_id, last_ctr);
        if status != E2EStatus::Ok {
            eprintln!("[E2E] Frame rejected: {status:?}");
            return Err(status);
        }
        // Update counter after successful check
        self.e2e_counters.insert(data_id, payload[0]);

        // Decode actual radar object from payload[2..]
        let p = &payload[2..];
        if p.len() < 12 { return Err(E2EStatus::DataIdError); }

        let dist_long = i16::from_le_bytes([p[0], p[1]]) as f32 * 0.01;
        let dist_lat  = i16::from_le_bytes([p[2], p[3]]) as f32 * 0.01;
        let vel_long  = i16::from_le_bytes([p[4], p[5]]) as f32 * 0.01;
        let vel_lat   = i16::from_le_bytes([p[6], p[7]]) as f32 * 0.01;
        let obj_class = p[8];

        let idx = self.associate_or_create(dist_long, dist_lat, 3.0);
        let track = &mut self.tracks[idx];
        track.state.update_full(
            [dist_long, dist_lat, vel_long, vel_lat],
            [0.04, 0.04, 0.01, 0.01],  // R noise (radar is accurate in velocity)
        );
        track.sources |= 0x01;
        track.obj_class = obj_class;
        track.last_update = Instant::now();

        println!(
            "[FUSION] Radar track {} updated: ({:.1},{:.1})m vel={:.1}m/s TTC={:.2}s",
            track.id,
            track.state.px,
            track.state.py,
            track.state.vx,
            track.state.ttc()
        );

        Ok(())
    }

    /// Process a camera-derived detection frame (no velocity).
    pub fn process_camera_frame(&mut self, dist_long: f32, dist_lat: f32, cls: u8) {
        let idx = self.associate_or_create(dist_long, dist_lat, 3.0);
        let track = &mut self.tracks[idx];
        track.state.update_position(dist_long, dist_lat, [0.25, 0.25]);
        track.sources |= 0x02;
        if cls == 3 || cls == 4 { track.obj_class = cls; } // pedestrian/cyclist priority
        track.last_update = Instant::now();
    }

    /// Predict all tracks forward by dt seconds.
    pub fn predict_all(&mut self, dt: f32) {
        for t in &mut self.tracks {
            t.state.predict(dt);
        }
    }

    /// Remove stale tracks.
    pub fn prune(&mut self) {
        let before = self.tracks.len();
        self.tracks.retain(|t| !t.is_stale());
        let pruned = before - self.tracks.len();
        if pruned > 0 {
            println!("[FUSION] Pruned {pruned} stale tracks");
        }
    }

    /// Return the most critical track (lowest TTC).
    pub fn most_critical(&self) -> Option<&Track> {
        self.tracks
            .iter()
            .min_by(|a, b| {
                a.state.ttc().partial_cmp(&b.state.ttc()).unwrap()
            })
    }

    /// Encode object list as CAN FD payload bytes (16 bytes/object).
    pub fn encode_output(&self) -> Vec<u8> {
        let mut out = Vec::new();
        for t in &self.tracks {
            let mut frame = [0u8; 16];
            frame[0] = t.id;
            frame[1] = t.obj_class;
            frame[2..4].copy_from_slice(
                &((t.state.px / 0.01) as i16).to_le_bytes());
            frame[4..6].copy_from_slice(
                &((t.state.py / 0.01) as i16).to_le_bytes());
            frame[6..8].copy_from_slice(
                &((t.state.vx / 0.01) as i16).to_le_bytes());
            frame[8..10].copy_from_slice(
                &((t.state.vy / 0.01) as i16).to_le_bytes());
            let ttc_raw = (t.state.ttc().min(25.5) / 0.1) as u8;
            frame[10] = ttc_raw;
            frame[11] = t.sources;
            out.extend_from_slice(&frame);
        }
        out
    }

    /* ── Private: nearest-neighbour association ─────────── */
    fn associate_or_create(&mut self, px: f32, py: f32, gate: f32) -> usize {
        if let Some((idx, _)) = self.tracks.iter().enumerate().min_by(|(_, a), (_, b)| {
            let da = ((a.state.px - px).powi(2) + (a.state.py - py).powi(2)).sqrt();
            let db = ((b.state.px - px).powi(2) + (b.state.py - py).powi(2)).sqrt();
            da.partial_cmp(&db).unwrap()
        }) {
            let d = ((self.tracks[idx].state.px - px).powi(2)
                   + (self.tracks[idx].state.py - py).powi(2))
                   .sqrt();
            if d < gate {
                return idx;
            }
        }
        // New track
        let id = self.next_id;
        self.next_id = self.next_id.wrapping_add(1);
        self.tracks.push(Track {
            id,
            state: KalmanState::new(px, py),
            last_update: Instant::now(),
            sources: 0,
            obj_class: 0,
        });
        self.tracks.len() - 1
    }
}

/* ─── Demo Main ───────────────────────────────────────────── */
fn main() {
    let mut ecu = FusionEcu::new();

    // Simulate an E2E-protected radar CAN FD frame
    // Layout: [counter][crc][dist_long_i16][dist_lat_i16][vx_i16][vy_i16][class][...]
    let data_id: u16 = 0x0301;
    let counter: u8  = 0x01;  // first frame after init (last=0xFF → expect 0x00 ... let's use 0x00)

    // Build raw payload (no E2E header yet)
    let dist_long_raw: i16 = (30.0f32 / 0.01) as i16; // 30 m
    let dist_lat_raw:  i16 = 0;
    let vel_long_raw:  i16 = (-12.0f32 / 0.01) as i16; // -12 m/s (approaching fast)
    let vel_lat_raw:   i16 = 0;
    let obj_class:      u8 = 1; // car

    let mut inner = Vec::new();
    inner.extend_from_slice(&dist_long_raw.to_le_bytes());
    inner.extend_from_slice(&dist_lat_raw.to_le_bytes());
    inner.extend_from_slice(&vel_long_raw.to_le_bytes());
    inner.extend_from_slice(&vel_lat_raw.to_le_bytes());
    inner.push(obj_class);
    inner.extend_from_slice(&[0u8; 3]); // padding

    // E2E: compute CRC over [data_id_lo, data_id_hi, counter, inner...]
    let mut crc_input = vec![
        (data_id & 0xFF) as u8,
        (data_id >> 8) as u8,
        counter,
    ];
    crc_input.extend_from_slice(&inner);
    let crc = crc8_j1850(&crc_input);

    let mut frame = vec![counter, crc];
    frame.extend_from_slice(&inner);

    // Process through fusion ECU
    match ecu.process_radar_frame(data_id, &frame) {
        Ok(()) => println!("[OK] Frame accepted by E2E"),
        Err(e) => println!("[ERR] Frame rejected: {e:?}"),
    }

    ecu.predict_all(0.050); // 50 ms prediction step

    if let Some(critical) = ecu.most_critical() {
        let ttc = critical.state.ttc();
        println!("[CRITICAL] id={} dist={:.1}m vel={:.1}m/s TTC={:.2}s",
                 critical.id, critical.state.px, critical.state.vx, ttc);
        if ttc < 2.0 {
            let decel = 9.81 * (1.0 - ttc / 2.0);
            println!("[AEB] Decel request = {decel:.2} m/s²");
        }
    }

    let output = ecu.encode_output();
    println!("[OUTPUT] {} bytes ready for CAN FD actuator bus", output.len());
}
```

---

## 10. Gateway: Bridging CAN and Automotive Ethernet

### 10.1 C: CAN-to-Ethernet PDU Routing

This simplified gateway translates a fused object CAN FD frame into a SOME/IP-style UDP payload for delivery to the HPC over Automotive Ethernet:

```c
/* can_eth_gateway.c
 * Routes fused object list from CAN FD to Ethernet (UDP/SOME/IP stub)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* SOME/IP header (simplified, 16 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t service_id;      /* 0x0042 = Fusion Object Service */
    uint16_t method_id;       /* 0x0001 = ObjectListNotification */
    uint32_t length;          /* payload length (bytes after header) */
    uint16_t client_id;
    uint16_t session_id;
    uint8_t  protocol_version;
    uint8_t  interface_version;
    uint8_t  msg_type;        /* 0x02 = NOTIFICATION */
    uint8_t  return_code;
} SomeIpHeader;

/* Fused object (16 bytes, matches encode_output format from fusion ECU) */
typedef struct __attribute__((packed)) {
    uint8_t  id;
    uint8_t  obj_class;
    int16_t  dist_long;   /* 0.01 m/LSB */
    int16_t  dist_lat;    /* 0.01 m/LSB */
    int16_t  vel_long;    /* 0.01 m/s/LSB */
    int16_t  vel_lat;     /* 0.01 m/s/LSB */
    uint8_t  ttc;         /* 0.1 s/LSB */
    uint8_t  sources;
    uint8_t  reserved[4];
} FusedObjectFrame;

#define MAX_OBJECTS_PER_PDU  20
#define ETH_HPC_IP           "192.168.10.1"
#define ETH_HPC_PORT         30490   /* SOME/IP default port */

static int udp_sock = -1;
static struct sockaddr_in hpc_addr;
static uint16_t session_id = 0;

static void init_udp_socket(void)
{
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&hpc_addr, 0, sizeof(hpc_addr));
    hpc_addr.sin_family      = AF_INET;
    hpc_addr.sin_port        = htons(ETH_HPC_PORT);
    inet_pton(AF_INET, ETH_HPC_IP, &hpc_addr.sin_addr);
}

static void forward_to_ethernet(const FusedObjectFrame *objects, size_t count)
{
    /* Build SOME/IP packet */
    uint8_t buf[sizeof(SomeIpHeader) + sizeof(FusedObjectFrame) * MAX_OBJECTS_PER_PDU];
    size_t  payload_len = sizeof(FusedObjectFrame) * count;

    SomeIpHeader hdr = {
        .service_id        = htons(0x0042),
        .method_id         = htons(0x0001),
        .length            = htonl((uint32_t)(payload_len + 8)),
        .client_id         = htons(0x0000),
        .session_id        = htons(++session_id),
        .protocol_version  = 0x01,
        .interface_version = 0x01,
        .msg_type          = 0x02, /* NOTIFICATION */
        .return_code       = 0x00,
    };

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), objects, payload_len);

    sendto(udp_sock, buf, sizeof(hdr) + payload_len, 0,
           (struct sockaddr *)&hpc_addr, sizeof(hpc_addr));

    printf("[GW] Forwarded %zu objects to HPC via SOME/IP (session=%u)\n",
           count, session_id);
}

int main(void)
{
    /* Open CAN FD socket */
    int can_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    int enable_canfd = 1;
    setsockopt(can_sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
               &enable_canfd, sizeof(enable_canfd));

    struct ifreq ifr;
    strncpy(ifr.ifr_name, "can1", IFNAMSIZ - 1); /* fusion domain bus */
    ioctl(can_sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    bind(can_sock, (struct sockaddr *)&addr, sizeof(addr));

    init_udp_socket();

    FusedObjectFrame object_buf[MAX_OBJECTS_PER_PDU];
    size_t obj_count = 0;

    printf("[GW] CAN→Ethernet gateway started\n");

    struct canfd_frame frame;
    while (1) {
        ssize_t nb = read(can_sock, &frame, sizeof(frame));
        if (nb <= 0) continue;

        uint32_t can_id = frame.can_id & CAN_EFF_MASK;

        /* 0x310 = fused object frame from fusion ECU */
        if (can_id == 0x310 && frame.len >= sizeof(FusedObjectFrame)) {
            if (obj_count < MAX_OBJECTS_PER_PDU) {
                memcpy(&object_buf[obj_count++], frame.data,
                       sizeof(FusedObjectFrame));
            }
        }
        /* 0x311 = end-of-list marker: flush to Ethernet */
        else if (can_id == 0x311 && obj_count > 0) {
            forward_to_ethernet(object_buf, obj_count);
            obj_count = 0;
        }
    }

    close(can_sock);
    close(udp_sock);
    return 0;
}
```

---

## 11. Time Synchronization: gPTP over Ethernet with CAN

Sensor fusion requires that all sensor timestamps be in a **common time domain**. IEEE 802.1AS (gPTP) provides sub-microsecond synchronization across Ethernet nodes. CAN ECUs synchronize to the gPTP master via:

1. **Hardware timestamping**: CAN controller latches the gPTP-synchronized hardware clock at the SOF (Start of Frame) bit of each received frame
2. **Timestamp field in frames**: Sensors include their gPTP-synchronized timestamp in the CAN FD payload (as shown in the radar header above)
3. **AUTOSAR TimeSync**: Dedicated CAN message carries the current gPTP time to ECUs without Ethernet access

```
Ethernet Domain (gPTP Master: HPC)
  │
  │  IEEE 802.1AS Sync (every 125 ms)
  ▼
Gateway ECU
  │
  │  AUTOSAR TimeSync CAN Frame (every 10 ms)
  │  CAN ID: 0x7FF
  │  [seconds: uint32][nanoseconds: uint32]
  ▼
All CAN ECUs (radar, IMU, brake, steer)
  │
  │  Timestamps in sensor frames now anchored to gPTP epoch
  ▼
Fusion ECU: all measurements share a common time reference
```

---

## 12. Summary

| Aspect                    | Key Points                                                                                                |
|---------------------------|-----------------------------------------------------------------------------------------------------------|
| **CAN's role in AVs**     | Safety-critical actuator control (brake, steer, throttle); low-bandwidth sensor data (radar, IMU, ultrasonic); diagnostics and OTA bridge |
| **CAN FD advantage**      | 5–8 Mbit/s enables radar object lists and IMU data at high update rates without moving to Ethernet        |
| **Network architecture**  | Heterogeneous: CAN FD for control/ADAS sensors, Automotive Ethernet (100BASE-T1/1000BASE-T1) for cameras, LiDAR, backbone |
| **Gateway ECU**           | Translates CAN ↔ Ethernet; performs PDU mapping, routing, security, and time correlation                  |
| **Sensor fusion**         | Level 2 fusion (radar + camera object lists) runs on CAN FD; raw data fusion runs on Ethernet at HPC     |
| **Kalman filtering**      | Constant-velocity tracker merges radar (full-state) and camera (position-only) measurements per cycle    |
| **Safety — ISO 26262**    | AUTOSAR E2E Profile 2 (CRC-8 + rolling counter) on all safety-critical CAN PDUs; timeout monitoring; safe-state fallback |
| **Time sync**             | gPTP over Ethernet → AUTOSAR TimeSync over CAN ensures all sensor timestamps share a common epoch         |
| **C/C++ programming**     | SocketCAN (Linux) provides direct CAN FD frame access; AUTOSAR COM/CANTP libraries handle E2E, signal packing, and transport |
| **Rust programming**      | `socketcan` crate provides type-safe CAN FD I/O; E2E checking and Kalman filtering are naturally expressed with Rust's ownership model and zero-cost abstractions |
| **AEB integration**       | TTC computed from fused tracks; AEB command encoded as CAN FD frame and sent to brake ECU with safety checksum |

### Key Takeaways

- **CAN is not obsolete in autonomous vehicles** — it is the backbone of the safety-critical control layer, complemented by Automotive Ethernet for high-bandwidth sensor data.
- **CAN FD bridges the gap** between classical CAN and Ethernet, handling ADAS sensor data like radar object lists at rates impractical for CAN 2.0.
- **Domain/zonal architecture** cleanly separates perception (Ethernet), fusion (CAN FD), and actuation (CAN) domains, with gateways providing secure, time-correlated translation.
- **Safety is non-negotiable**: every CAN message in an ADAS context must carry E2E protection (CRC + counter), and the system must define explicit degraded-mode and safe-state behaviors for all communication faults.
- **Sensor fusion over CAN** is feasible and well-established for low-bandwidth processed results (object lists), while raw data stays on Ethernet — this division is both practical and architecturally sound.

---

*References: ISO 11898 (CAN), ISO 26262 (Functional Safety), AUTOSAR Classic Platform Specifications, IEEE 802.1AS (gPTP), SAE J2735, SOME/IP Protocol Specification (AUTOSAR), Bosch CAN FD Specification 1.0*