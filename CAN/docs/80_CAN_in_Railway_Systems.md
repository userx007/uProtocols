# 80. CAN in Railway Systems

**Structure (10 sections):**

1. **Introduction** — Why CAN suits the harsh, deterministic requirements of rail transport
2. **Railway Communication Architecture** — The 3-tier TCN hierarchy (WTB → MVB → CAN), with an ASCII architecture diagram
3. **Relevant Standards** — IEC 61375, CANopen (CiA 301/454), EN 50155, EN 50128/50129, J1939
4. **CAN in Train Control Networks** — Door control, traction/braking, HVAC, PIS roles
5. **Passenger Information Systems** — Architecture diagram, data flow table, display node behaviour
6. **Safety-Critical Communication** — CANopen Safety (EN 50325-5), SIL levels table, redundant bus patterns

**Code Examples (6 total):**

| # | Language | Topic |
|---|---|---|
| 1 | C | Door Control Unit — SocketCAN PDO transmission with door open/close cycle |
| 2 | C++ | VCU NMT Master — CANopen heartbeat monitoring, node fault detection |
| 3 | C | Safety Brake PDO — Consecutive number + CRC-16 safety layer (EN 50325-5) |
| 4 | Rust | PIS Server — CANopen PDO transmission of destination/stop data |
| 5 | Rust | PIS Display Node — PDO receiver with watchdog and sequence validation |
| 6 | Rust | Safety BCU — Type-safe state machine enforcing valid brake state transitions |

**Also included:** Fault handling tables, MISRA-C snippet, bit rate selection guide, HIL testing notes, and a comprehensive summary.

> Train control networks, passenger information systems, and safety-critical rail communication.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Railway Communication Architecture](#railway-communication-architecture)
3. [Relevant Standards and Protocols](#relevant-standards-and-protocols)
4. [CAN Bus in Train Control Networks (TCN)](#can-bus-in-train-control-networks-tcn)
5. [Passenger Information Systems (PIS)](#passenger-information-systems-pis)
6. [Safety-Critical Communication](#safety-critical-communication)
7. [C/C++ Programming Examples](#cc-programming-examples)
8. [Rust Programming Examples](#rust-programming-examples)
9. [Fault Handling and Redundancy](#fault-handling-and-redundancy)
10. [Testing and Certification Considerations](#testing-and-certification-considerations)
11. [Summary](#summary)

---

## Introduction

CAN (Controller Area Network) bus technology has found wide adoption in railway systems, providing a robust, deterministic, and cost-effective communication backbone for a broad range of subsystems — from safety-critical train control to passenger-facing information displays. Originally developed for automotive use by Bosch in the 1980s, CAN's inherent properties of multi-master arbitration, built-in error detection, and fault confinement made it an ideal candidate for the harsh electromagnetic environments and stringent reliability requirements of rail transport.

Modern rolling stock (trains, trams, metro cars) typically features multiple layers of communication networks. CAN occupies an important middle tier: fast enough for real-time control loops, yet simple and inexpensive enough to be embedded in hundreds of nodes distributed throughout a train consist. Where older railways relied on hardwired relay logic or proprietary serial buses, contemporary designs leverage standardised CAN-based protocols to interconnect door controllers, HVAC units, braking systems, traction inverters, and passenger display units under a unified communication framework.

---

## Railway Communication Architecture

A typical modern train employs a hierarchical network structure:

```
┌──────────────────────────────────────────────────────────────────┐
│                     Train Level (Consist)                        │
│          WTB – Wire Train Bus (IEC 61375-1, MVB or Ethernet)     │
└────────────────────────┬─────────────────────────────────────────┘
                         │ Gateway / Vehicle Control Unit (VCU)
┌────────────────────────▼─────────────────────────────────────────┐
│                     Vehicle Level                                │
│          MVB – Multifunction Vehicle Bus (IEC 61375-2)           │
│                  or CAN (CANopen, J1939, proprietary)            │
└──────┬──────────────────┬──────────────────┬─────────────────────┘
       │                  │                  │
  ┌────▼────┐        ┌────▼────┐        ┌────▼────┐
  │ Traction│        │  Doors  │        │  HVAC   │
  │Subsystem│        │Subsystem│        │Subsystem│
  │  (CAN)  │        │  (CAN)  │        │  (CAN)  │
  └─────────┘        └─────────┘        └─────────┘
```

**Key network roles:**

- **WTB (Wire Train Bus):** Spans the full train consist, handles train-level control (coupling/uncoupling logic, consist-wide diagnostics).
- **MVB (Multifunction Vehicle Bus):** Within a single vehicle, connects major subsystems. Often bridges to CAN for lower-level device networks.
- **CAN Bus:** Used at the device/subsystem level — connecting sensors, actuators, display units, and controllers within a subsystem or vehicle zone.

CAN is typically found in:

- Door control units (DCU)
- HVAC and climate control
- Brake control units (BCU) — sometimes via CANopen Safety
- Passenger Information Systems (PIS) display nodes
- Battery management and auxiliary power units
- Pantograph and coupler controllers

---

## Relevant Standards and Protocols

### IEC 61375 – Train Communication Network (TCN)

The IEC 61375 family is the principal international standard for train communication networks:

| Part | Scope |
|------|-------|
| IEC 61375-1 | TCN General Architecture |
| IEC 61375-2-1 | MVB (Multifunction Vehicle Bus) |
| IEC 61375-3-3 | CANopen Consist Network (CCN) |
| IEC 61375-3-4 | Ethernet Consist Network (ECN) |

**IEC 61375-3-3** specifically defines the use of CANopen as a consist-level network in railways, standardising device profiles, process data objects (PDOs), and service data objects (SDOs) in the rail context.

### CANopen (CiA 301 / EN 50325-4)

CANopen is the dominant higher-layer protocol used on CAN in railways. It defines:

- **NMT (Network Management):** Node start, stop, reset, and heartbeat monitoring.
- **PDO (Process Data Object):** High-speed, low-latency cyclic or event-driven process data (e.g., door status, speed).
- **SDO (Service Data Object):** Configuration and parameterisation of nodes.
- **EMCY (Emergency Object):** Fault reporting.
- **SYNC:** Network-wide synchronisation.

Relevant CANopen device profiles for rail include:

- **CiA 417:** CANopen lift/door systems (adapted for train doors).
- **CiA 454:** CANopen for public transport.

### EN 50155 – Electronic Equipment in Rolling Stock

EN 50155 defines environmental and operational requirements (vibration, temperature, EMC, power supply transients) that CAN hardware must meet for railway certification.

### EN 50128 / EN 50129 – Railway Software and System Safety

For safety-critical CAN applications (braking, door interlock), software must comply with EN 50128 (software for railway control and protection systems) and hardware/systems with EN 50129. These standards mandate rigorous development processes, safety integrity levels (SIL 1–4), and formal verification.

### J1939

In some regional or freight rail applications (particularly in North America), SAE J1939 — originally for heavy commercial vehicles — is used on CAN for locomotive engine control units, generators, and auxiliary systems.

---

## CAN Bus in Train Control Networks (TCN)

### Door Control Systems

Door systems represent one of the most prevalent CAN applications in railways. Each door leaf or door module contains a Door Control Unit (DCU) connected to a CAN segment. The DCU manages:

- Motor drive commands (open/close)
- Position sensor feedback (fully open, fully closed, obstruction)
- Lock/unlock actuation
- Emergency release detection
- Door status reporting to the Vehicle Control Unit (VCU)

A typical door CAN segment operates at **250 kbit/s or 500 kbit/s**, connecting 2–16 DCUs per vehicle side.

### Traction and Braking Subsystems

CAN links between the Brake Control Unit (BCU) and the Traction Control Unit (TCU) carry:

- Brake demand (deceleration set point in m/s²)
- Wheel slip/slide detection signals
- Blending commands (combining regenerative and friction braking)
- Fault status

Because braking is SIL 2–3 in most railway systems, the CAN implementation here typically uses **CANopen Safety (EN 50325-5)** or a proprietary safety layer with CRC, sequence numbering, and timeout watchdogs added above raw CAN frames.

### HVAC and Climate Control

HVAC controllers form a relatively straightforward CAN subsystem: temperature set points, fan speed, compressor demand, and filter status are exchanged between the zone controllers and the vehicle HVAC master. CANopen PDOs are well-suited here — cyclic transmission at 100–500 ms intervals.

### Passenger Information Systems (PIS)

PIS networks distribute dynamic route and stop information, multilingual announcements, and real-time data to passenger-facing displays throughout the train. CAN (often CANopen or a proprietary overlay) connects:

- Central PIS Server (route schedule, GPS position)
- Interior LED/LCD display heads (destination, next stop, connections)
- Audio announcement units
- External destination displays (cab-end and side roller/LED boards)

---

## Passenger Information Systems (PIS)

### Architecture

```
┌─────────────────────────────────┐
│     PIS Central Server          │
│  (Route DB, GPS, GPRS/LTE link) │
└───────────────┬─────────────────┘
                │ CAN (CANopen or proprietary)
     ┌──────────┼──────────┐
     │          │          │
┌────▼──┐  ┌────▼──┐  ┌────▼──────────┐
│Interior│  │Interior│  │  External    │
│Display │  │Display │  │  Destination │
│(Saloon)│  │(Saloon)│  │  Display     │
└────────┘  └────────┘  └──────────────┘
```

### Data Flows

| Message Type | Direction | Typical Period |
|---|---|---|
| Next stop announcement | Server → Displays | Event-driven |
| Route/destination text | Server → Displays | On change |
| GPS position | Server → all nodes | 1–5 s |
| Fault status | Display → Server | Event-driven |
| Node heartbeat | Each node → broadcast | 1 s |

### Display Node Behaviour

Each display node:
1. Boots and performs CANopen NMT initialisation.
2. Subscribes to PDOs carrying destination text and stop data.
3. Renders received Unicode strings on its display hardware.
4. Sends heartbeat and EMCY objects on fault.

---

## Safety-Critical Communication

### CANopen Safety (EN 50325-5 / IEC 61784-3-3)

CANopen Safety adds a safety layer on top of standard CAN frames. Each safety-relevant PDO is transmitted as a **Safety PDO (SPDO)** carrying:

- A **Consecutive Number (CN):** Detects lost or duplicated messages.
- A **CRC:** Detects data corruption.
- A **Time Expectation:** Enables watchdog-based timeout detection.

The safety layer is transparent to the underlying CAN hardware and can be implemented in software, making it compatible with standard CAN controllers.

### Safety Integrity Levels in Rail

| Application | Typical SIL | Protocol Approach |
|---|---|---|
| Passenger door interlock | SIL 2 | CANopen Safety or dual-channel CAN |
| Emergency braking demand | SIL 2–3 | CANopen Safety + hardware redundancy |
| HVAC / PIS | SIL 0–1 | Standard CANopen |
| Pantograph control | SIL 1–2 | CANopen Safety |

### Redundancy Patterns

For SIL 2+ applications, redundant CAN buses are common:

```
  ┌──────────────────────────────────┐
  │         Safety Node              │
  │   ┌──────────┐  ┌──────────┐     │
  │   │ CAN Ctrl │  │ CAN Ctrl │     │
  │   │  Chan A  │  │  Chan B  │     │
  └───┴────┬─────┴──┴────┬─────┴────┘
           │              │
    CAN Bus A          CAN Bus B
    (primary)          (redundant)
```

Both channels transmit identical frames; the receiver validates both and signals a fault if they diverge.

---

## C/C++ Programming Examples

### Example 1: CAN Frame Transmission – Door Status (SocketCAN / Linux)

This example demonstrates sending a door status PDO from a Door Control Unit using the Linux SocketCAN interface, which is commonly used in development and test environments for railway CAN nodes.

```c
/*
 * door_control_can.c
 * Railway Door Control Unit – CAN Status Frame Transmission
 * Protocol: CANopen PDO (CiA 301)
 * Target: Linux / SocketCAN (test/development)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <stdint.h>
#include <time.h>

/* CANopen COB-ID definitions for door subsystem */
#define NODE_ID_DCU_1           0x01    /* Door Control Unit node 1 */
#define COBID_TPDO1_BASE        0x180   /* Transmit PDO1 base COB-ID */
#define COBID_TPDO1_DCU1        (COBID_TPDO1_BASE + NODE_ID_DCU_1)

/* Door status bit masks (PDO1 byte 0) */
#define DOOR_STATUS_CLOSED      (1 << 0)
#define DOOR_STATUS_OPEN        (1 << 1)
#define DOOR_STATUS_LOCKED      (1 << 2)
#define DOOR_STATUS_OBSTRUCTION (1 << 3)
#define DOOR_STATUS_FAULT       (1 << 4)
#define DOOR_STATUS_EMERG_OPEN  (1 << 5)

/* Door position in mm (0 = fully closed, 1000 = fully open) */
typedef struct {
    uint8_t  status_flags;      /* Byte 0: status bits */
    uint8_t  fault_code;        /* Byte 1: fault code (0 = no fault) */
    uint16_t position_mm;       /* Bytes 2-3: position in mm (little-endian) */
    uint16_t motor_current_ma;  /* Bytes 4-5: motor current in mA */
    uint8_t  temperature_c;     /* Byte 6: DCU board temperature in °C */
    uint8_t  reserved;          /* Byte 7: reserved */
} __attribute__((packed)) DoorStatusPDO;

/* Open a SocketCAN socket and bind to interface */
int can_open(const char *ifname) {
    int sock;
    struct sockaddr_can addr;
    struct ifreq ifr;

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket(PF_CAN)");
        return -1;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(sock);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    return sock;
}

/* Transmit a door status PDO */
int send_door_status_pdo(int sock, const DoorStatusPDO *status) {
    struct can_frame frame;

    memset(&frame, 0, sizeof(frame));
    frame.can_id  = COBID_TPDO1_DCU1;   /* Standard 11-bit CAN ID */
    frame.can_dlc = sizeof(DoorStatusPDO);

    /* Copy PDO payload into CAN data field */
    memcpy(frame.data, status, sizeof(DoorStatusPDO));

    ssize_t nbytes = write(sock, &frame, sizeof(frame));
    if (nbytes != sizeof(frame)) {
        perror("write CAN frame");
        return -1;
    }
    return 0;
}

/* Simulate a door opening cycle and transmit status updates */
int main(void) {
    const char *can_iface = "can0";
    int sock = can_open(can_iface);
    if (sock < 0) {
        fprintf(stderr, "Failed to open CAN interface %s\n", can_iface);
        return EXIT_FAILURE;
    }

    printf("Door Control Unit started on %s, node ID 0x%02X\n",
           can_iface, NODE_ID_DCU_1);

    DoorStatusPDO pdo = {
        .status_flags     = DOOR_STATUS_CLOSED | DOOR_STATUS_LOCKED,
        .fault_code       = 0x00,
        .position_mm      = 0,
        .motor_current_ma = 0,
        .temperature_c    = 25,
        .reserved         = 0,
    };

    /* --- Simulate door open command received from VCU --- */

    /* Step 1: Unlock */
    pdo.status_flags = DOOR_STATUS_CLOSED;          /* Remove LOCKED bit */
    pdo.motor_current_ma = 200;
    send_door_status_pdo(sock, &pdo);
    printf("[DCU] Door unlocked, transmitting PDO\n");
    usleep(200000);

    /* Step 2: Opening */
    for (uint16_t pos = 0; pos <= 1000; pos += 100) {
        pdo.status_flags     = 0;                   /* Transitioning */
        pdo.position_mm      = pos;
        pdo.motor_current_ma = 800 + (pos / 10);    /* Rising current during travel */
        send_door_status_pdo(sock, &pdo);
        printf("[DCU] Door position: %d mm\n", pos);
        usleep(50000);
    }

    /* Step 3: Fully open */
    pdo.status_flags     = DOOR_STATUS_OPEN;
    pdo.position_mm      = 1000;
    pdo.motor_current_ma = 100;                     /* Hold current only */
    send_door_status_pdo(sock, &pdo);
    printf("[DCU] Door fully open\n");

    close(sock);
    return EXIT_SUCCESS;
}
```

---

### Example 2: CANopen NMT Master & Heartbeat Monitor (C++)

This example shows a Vehicle Control Unit (VCU) acting as a CANopen NMT master, commanding door nodes and monitoring their heartbeats to detect node failures.

```cpp
/*
 * vcu_nmt_master.cpp
 * Vehicle Control Unit – CANopen NMT Master with Heartbeat Monitoring
 * Monitors door control units and commands network state transitions.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <array>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

/* CANopen COB-IDs */
constexpr uint32_t COBID_NMT_COMMAND    = 0x000;   /* NMT command (broadcast) */
constexpr uint32_t COBID_HEARTBEAT_BASE = 0x700;   /* Heartbeat: 0x700 + NodeID */
constexpr uint32_t COBID_EMCY_BASE      = 0x080;   /* Emergency: 0x080 + NodeID */

/* CANopen NMT command specifiers */
enum NMTCommand : uint8_t {
    NMT_START_REMOTE_NODE   = 0x01,
    NMT_STOP_REMOTE_NODE    = 0x02,
    NMT_ENTER_PRE_OP        = 0x80,
    NMT_RESET_NODE          = 0x81,
    NMT_RESET_COMM          = 0x82,
};

/* CANopen node states (from heartbeat byte) */
enum NodeState : uint8_t {
    STATE_BOOT_UP       = 0x00,
    STATE_STOPPED       = 0x04,
    STATE_OPERATIONAL   = 0x05,
    STATE_PRE_OP        = 0x7F,
};

/* Maximum allowed heartbeat interval: 2× expected period (1 s) */
constexpr double HEARTBEAT_TIMEOUT_S = 2.0;

constexpr int MAX_DOOR_NODES = 8;

struct NodeMonitor {
    uint8_t     node_id;
    NodeState   last_state;
    struct timespec last_heartbeat;
    bool        alive;
};

static std::array<NodeMonitor, MAX_DOOR_NODES> door_nodes;
static int g_sock = -1;

/* Get monotonic timestamp */
static struct timespec now_ts() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

/* Elapsed seconds between two timespecs */
static double elapsed_s(const struct timespec &from, const struct timespec &to) {
    return (double)(to.tv_sec - from.tv_sec) +
           (double)(to.tv_nsec - from.tv_nsec) / 1e9;
}

/* Send an NMT command to a specific node (node_id=0 = all nodes) */
static int send_nmt_command(NMTCommand cmd, uint8_t node_id) {
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = COBID_NMT_COMMAND;
    frame.can_dlc = 2;
    frame.data[0] = static_cast<uint8_t>(cmd);
    frame.data[1] = node_id;

    if (write(g_sock, &frame, sizeof(frame)) != sizeof(frame)) {
        perror("NMT write");
        return -1;
    }
    return 0;
}

/* Process a received heartbeat message */
static void process_heartbeat(uint32_t cob_id, const uint8_t *data) {
    uint8_t   node_id = static_cast<uint8_t>(cob_id - COBID_HEARTBEAT_BASE);
    NodeState state   = static_cast<NodeState>(data[0]);

    for (auto &node : door_nodes) {
        if (node.node_id == node_id) {
            node.last_state    = state;
            node.last_heartbeat = now_ts();
            bool was_dead = !node.alive;
            node.alive = true;
            if (was_dead) {
                printf("[VCU] Node 0x%02X recovered, state=%02X\n", node_id, state);
            }
            return;
        }
    }
}

/* Process an emergency (EMCY) object */
static void process_emcy(uint32_t cob_id, const uint8_t *data) {
    uint8_t  node_id     = static_cast<uint8_t>(cob_id - COBID_EMCY_BASE);
    uint16_t error_code  = static_cast<uint16_t>(data[0] | (data[1] << 8));
    uint8_t  error_reg   = data[2];

    printf("[VCU] EMCY from node 0x%02X: ErrorCode=0x%04X, ErrorReg=0x%02X\n",
           node_id, error_code, error_reg);

    /* In a real system: log to black box recorder, raise VCU fault flag */
}

/* Check all nodes for heartbeat timeouts */
static void check_heartbeat_timeouts() {
    struct timespec now = now_ts();
    for (auto &node : door_nodes) {
        if (!node.alive) continue;
        double age = elapsed_s(node.last_heartbeat, now);
        if (age > HEARTBEAT_TIMEOUT_S) {
            printf("[VCU] FAULT: Node 0x%02X heartbeat timeout (%.1f s)\n",
                   node.node_id, age);
            node.alive = false;
            /* Safety action: command emergency stop if door node lost */
        }
    }
}

int main() {
    /* Initialise monitored door node IDs */
    for (int i = 0; i < MAX_DOOR_NODES; ++i) {
        door_nodes[i] = {
            .node_id        = static_cast<uint8_t>(0x10 + i),
            .last_state     = STATE_BOOT_UP,
            .last_heartbeat = now_ts(),
            .alive          = false,
        };
    }

    /* Open SocketCAN */
    g_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    strncpy(ifr.ifr_name, "can0", IFNAMSIZ - 1);
    ioctl(g_sock, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr{};
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(g_sock, (struct sockaddr *)&addr, sizeof(addr));

    /* Start all nodes */
    printf("[VCU] Commanding all nodes to OPERATIONAL\n");
    send_nmt_command(NMT_START_REMOTE_NODE, 0x00);  /* node_id=0: all nodes */

    /* Main receive loop */
    struct can_frame frame;
    time_t last_check = time(nullptr);

    while (true) {
        /* Non-blocking read using select would be used in production */
        ssize_t nbytes = read(g_sock, &frame, sizeof(frame));
        if (nbytes == sizeof(frame)) {
            uint32_t cob_id = frame.can_id & CAN_SFF_MASK;

            if (cob_id >= COBID_HEARTBEAT_BASE && cob_id <= 0x77F) {
                process_heartbeat(cob_id, frame.data);
            } else if (cob_id >= COBID_EMCY_BASE && cob_id <= 0x0FF) {
                process_emcy(cob_id, frame.data);
            }
        }

        /* Periodic timeout check */
        if (time(nullptr) - last_check >= 1) {
            check_heartbeat_timeouts();
            last_check = time(nullptr);
        }
    }

    close(g_sock);
    return 0;
}
```

---

### Example 3: CANopen Safety PDO with Sequence Number and CRC (C)

A simplified demonstration of the safety layer added to a braking demand PDO, implementing consecutive numbering and CRC to satisfy EN 50325-5 requirements.

```c
/*
 * safety_brake_pdo.c
 * Simplified CANopen Safety PDO – Brake Demand Transmission
 * Demonstrates consecutive number (CN) and CRC safety wrapper.
 * Reference: EN 50325-5 / IEC 61784-3-3
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* Safety PDO structure (8 bytes for a brake demand) */
typedef struct {
    uint8_t  consecutive_number;    /* Byte 0: CN (0-255, wraps) */
    int16_t  deceleration_demand;   /* Bytes 1-2: demanded decel in cm/s² */
    uint8_t  control_flags;         /* Byte 3: brake flags */
    uint8_t  watchdog_time_ms;      /* Byte 4: sender watchdog time */
    uint8_t  reserved[1];           /* Byte 5: reserved */
    uint16_t crc16;                 /* Bytes 6-7: CRC over bytes 0-5 */
} __attribute__((packed)) SafetyBrakePDO;

/* CRC-16/CAN-FD (CRC-16/IBM-3740) polynomial 0x1021 */
static uint16_t crc16_compute(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

/* Control flag bits */
#define BRAKE_FLAG_EMERGENCY     (1 << 0)
#define BRAKE_FLAG_SERVICE       (1 << 1)
#define BRAKE_FLAG_HOLD          (1 << 2)
#define BRAKE_FLAG_VALID         (1 << 7)  /* Must be set for valid demand */

static uint8_t g_consecutive_number = 0;

/* Build a safety brake PDO */
void build_safety_brake_pdo(SafetyBrakePDO *pdo,
                             int16_t decel_cm_s2,
                             uint8_t flags,
                             uint8_t watchdog_ms) {
    pdo->consecutive_number  = g_consecutive_number++;
    pdo->deceleration_demand = decel_cm_s2;
    pdo->control_flags       = flags | BRAKE_FLAG_VALID;
    pdo->watchdog_time_ms    = watchdog_ms;
    pdo->reserved[0]         = 0x00;

    /* CRC covers all bytes except the CRC field itself */
    pdo->crc16 = crc16_compute((const uint8_t *)pdo,
                                sizeof(SafetyBrakePDO) - sizeof(uint16_t));
}

/* Validate a received safety brake PDO */
typedef enum {
    SAFETY_PDO_OK,
    SAFETY_PDO_CRC_ERROR,
    SAFETY_PDO_SEQ_ERROR,
    SAFETY_PDO_INVALID_FLAG,
} SafetyPDOResult;

SafetyPDOResult validate_safety_brake_pdo(const SafetyBrakePDO *pdo,
                                           uint8_t expected_cn) {
    /* 1. Validate flag */
    if (!(pdo->control_flags & BRAKE_FLAG_VALID)) {
        return SAFETY_PDO_INVALID_FLAG;
    }

    /* 2. Check CRC */
    uint16_t computed = crc16_compute((const uint8_t *)pdo,
                                       sizeof(SafetyBrakePDO) - sizeof(uint16_t));
    if (computed != pdo->crc16) {
        fprintf(stderr, "[BCU] CRC error: computed=0x%04X received=0x%04X\n",
                computed, pdo->crc16);
        return SAFETY_PDO_CRC_ERROR;
    }

    /* 3. Check consecutive number (allows wrap-around) */
    if (pdo->consecutive_number != expected_cn) {
        fprintf(stderr, "[BCU] Sequence error: expected=%u got=%u\n",
                expected_cn, pdo->consecutive_number);
        return SAFETY_PDO_SEQ_ERROR;
    }

    return SAFETY_PDO_OK;
}

int main(void) {
    SafetyBrakePDO pdo;
    uint8_t expected_cn = 0;

    printf("=== Safety Brake PDO Demonstration ===\n\n");

    /* Simulate sending a service brake demand: 120 cm/s² deceleration */
    build_safety_brake_pdo(&pdo,
                            -120,            /* Negative = deceleration */
                            BRAKE_FLAG_SERVICE,
                            100);            /* 100 ms watchdog */

    printf("Transmitted PDO:\n");
    printf("  CN:           %u\n",     pdo.consecutive_number);
    printf("  Deceleration: %d cm/s²\n", pdo.deceleration_demand);
    printf("  Flags:        0x%02X\n", pdo.control_flags);
    printf("  Watchdog:     %u ms\n",  pdo.watchdog_time_ms);
    printf("  CRC16:        0x%04X\n\n", pdo.crc16);

    /* Simulate receiving and validating */
    SafetyPDOResult result = validate_safety_brake_pdo(&pdo, expected_cn);
    if (result == SAFETY_PDO_OK) {
        printf("Validation: PASSED\n");
        printf("Brake demand accepted: %d cm/s²\n", pdo.deceleration_demand);
        expected_cn++;
    } else {
        printf("Validation: FAILED (code %d) — safety reaction triggered\n", result);
    }

    /* Demonstrate CRC error detection */
    printf("\n--- Injecting CAN bit error ---\n");
    pdo.deceleration_demand = -999;          /* Data corrupted in transit */
    result = validate_safety_brake_pdo(&pdo, expected_cn);
    printf("Corrupted PDO validation: %s\n",
           result == SAFETY_PDO_CRC_ERROR ? "CRC ERROR DETECTED (correct)" : "ERROR NOT DETECTED");

    return 0;
}
```

---

## Rust Programming Examples

### Example 4: CAN Frame Transmission – Passenger Information Display (Rust)

Using the `socketcan` crate to transmit PIS (Passenger Information System) display updates from a central PIS server node.

```toml
# Cargo.toml
[package]
name = "pis_can_server"
version = "0.1.0"
edition = "2021"

[dependencies]
socketcan = "3.3"
byteorder = "1.5"
```

```rust
//! pis_can_server/src/main.rs
//!
//! Passenger Information System – CAN PIS Server
//! Transmits destination and next-stop PDOs to display nodes.
//! Protocol: CANopen PDO (CiA 301 / CiA 454)

use socketcan::{CanFrame, CanSocket, Socket, StandardId};
use std::time::Duration;
use std::thread;

/// CANopen COB-ID constants for PIS subsystem
const NODE_ID_PIS_SERVER:  u16 = 0x20;
const COBID_TPDO1_PIS:     u16 = 0x180 + NODE_ID_PIS_SERVER;  // 0x1A0
const COBID_TPDO2_PIS:     u16 = 0x280 + NODE_ID_PIS_SERVER;  // 0x2A0
const COBID_HEARTBEAT_PIS: u16 = 0x700 + NODE_ID_PIS_SERVER;  // 0x720

/// Node state for CANopen heartbeat
#[repr(u8)]
enum NodeState {
    Operational = 0x05,
}

/// PIS Control PDO 1 – Destination and line number (8 bytes)
///
/// | Byte | Content                        |
/// |------|--------------------------------|
/// | 0-1  | Line number (BCD, e.g. 0x0042 = Line 42) |
/// | 2-5  | Destination text index (UTF-8 code point, LE) |
/// | 6    | Screen zone flags              |
/// | 7    | Sequence number                |
#[derive(Debug, Clone, Copy)]
struct PisControlPdo1 {
    line_number:      u16,
    destination_idx:  u32,
    zone_flags:       u8,
    sequence:         u8,
}

impl PisControlPdo1 {
    fn to_bytes(self) -> [u8; 8] {
        let mut b = [0u8; 8];
        b[0..2].copy_from_slice(&self.line_number.to_le_bytes());
        b[2..6].copy_from_slice(&self.destination_idx.to_le_bytes());
        b[6] = self.zone_flags;
        b[7] = self.sequence;
        b
    }
}

/// PIS Control PDO 2 – Next stop text (8 bytes, packed ASCII/UTF-8 short name)
///
/// | Byte | Content                        |
/// |------|--------------------------------|
/// | 0-6  | Next stop short name (7 chars, null-padded) |
/// | 7    | Stop index in route (0-127)    |
#[derive(Debug, Clone)]
struct PisControlPdo2 {
    stop_name:  [u8; 7],   // Short station name, max 7 ASCII chars
    stop_index: u8,
}

impl PisControlPdo2 {
    fn new(name: &str, index: u8) -> Self {
        let mut stop_name = [0u8; 7];
        for (i, b) in name.bytes().take(7).enumerate() {
            stop_name[i] = b;
        }
        PisControlPdo2 { stop_name, stop_index: index }
    }

    fn to_bytes(&self) -> [u8; 8] {
        let mut b = [0u8; 8];
        b[0..7].copy_from_slice(&self.stop_name);
        b[7] = self.stop_index;
        b
    }
}

/// Send a CAN frame with the given COB-ID and 8-byte payload
fn send_pdo(sock: &CanSocket, cob_id: u16, data: &[u8; 8]) -> socketcan::Result<()> {
    let id = StandardId::new(cob_id).expect("Invalid CAN ID");
    let frame = CanFrame::new(id, data).expect("Frame construction failed");
    sock.write_frame(&frame)?;
    Ok(())
}

/// Send a CANopen heartbeat indicating this node is OPERATIONAL
fn send_heartbeat(sock: &CanSocket) -> socketcan::Result<()> {
    let id = StandardId::new(COBID_HEARTBEAT_PIS).expect("Invalid heartbeat ID");
    let data = [NodeState::Operational as u8, 0, 0, 0, 0, 0, 0, 0];
    let frame = CanFrame::new(id, &data[..1]).expect("Heartbeat frame failed");
    sock.write_frame(&frame)?;
    Ok(())
}

fn main() -> socketcan::Result<()> {
    let sock = CanSocket::open("can0")?;
    println!("[PIS Server] Started on can0, node 0x{:02X}", NODE_ID_PIS_SERVER);

    // Simulated route data
    let route = vec![
        ("Frankfurt Hbf",  "FrankfurtHbf",  0u8),
        ("Frankfurt Süd",  "FrankfSüd",    1u8),
        ("Darmstadt Hbf",  "DarmstdtHbf",  2u8),
        ("Bensheim",       "Bensheim",      3u8),
        ("Mannheim Hbf",   "MannheimHbf",  4u8),
    ];

    let mut sequence: u8 = 0;
    let line_number: u16 = 0x0042;  // Line RE42

    for (i, (dest_full, dest_short, stop_idx)) in route.iter().enumerate() {
        println!("[PIS Server] Approaching stop {}: {}", stop_idx, dest_full);

        // PDO1: Destination and line number update
        let pdo1 = PisControlPdo1 {
            line_number,
            destination_idx: *stop_idx as u32,
            zone_flags: 0b0000_0011,  // Zones A and B active
            sequence,
        };
        send_pdo(&sock, COBID_TPDO1_PIS, &pdo1.to_bytes())?;
        println!("[PIS Server] Sent PDO1 – Line {:04X}, dest_idx={}", line_number, stop_idx);

        // PDO2: Next stop name (look ahead one stop)
        let next = route.get(i + 1).unwrap_or(&route[i]);
        let pdo2 = PisControlPdo2::new(next.1, next.2);
        send_pdo(&sock, COBID_TPDO2_PIS, &pdo2.to_bytes())?;
        println!("[PIS Server] Sent PDO2 – Next stop: {}", next.0);

        sequence = sequence.wrapping_add(1);

        // Send heartbeat every cycle
        send_heartbeat(&sock)?;
        println!("[PIS Server] Heartbeat sent\n");

        // In a real system this would be event-driven or on SYNC
        thread::sleep(Duration::from_secs(5));
    }

    println!("[PIS Server] End of line reached");
    Ok(())
}
```

---

### Example 5: CAN Receiver – PIS Display Node with Watchdog (Rust)

A display node that receives PIS PDOs, validates them, and triggers a watchdog fault if the server goes silent.

```toml
# Cargo.toml additions
[dependencies]
socketcan = "3.3"
```

```rust
//! pis_display_node/src/main.rs
//!
//! PIS Display Node – Receives destination PDOs from PIS Server.
//! Implements a watchdog to detect server silence (SIL 1 requirement).

use socketcan::{CanFrame, CanSocket, EmbeddedFrame, Frame, Socket, StandardId};
use std::collections::HashMap;
use std::time::{Duration, Instant};

const NODE_ID_PIS_SERVER:   u16 = 0x20;
const COBID_TPDO1_PIS:      u16 = 0x180 + NODE_ID_PIS_SERVER;
const COBID_TPDO2_PIS:      u16 = 0x280 + NODE_ID_PIS_SERVER;
const COBID_HEARTBEAT_PIS:  u16 = 0x700 + NODE_ID_PIS_SERVER;

/// Watchdog: if no heartbeat within this period, trigger fault
const HEARTBEAT_TIMEOUT: Duration = Duration::from_secs(2);

/// Display fault flags (would trigger fallback display mode in production)
#[derive(Debug)]
enum DisplayFault {
    HeartbeatTimeout,
    UnexpectedSequence { expected: u8, received: u8 },
    InvalidFrame { cob_id: u16 },
}

/// Simple route destination lookup (in production: full Unicode string table from NVM)
fn lookup_destination(idx: u32) -> &'static str {
    match idx {
        0 => "Frankfurt Hbf",
        1 => "Frankfurt Süd",
        2 => "Darmstadt Hbf",
        3 => "Bensheim",
        4 => "Mannheim Hbf",
        _ => "Unknown",
    }
}

/// Process PDO1 – Destination and line update
fn handle_pdo1(data: &[u8], expected_seq: &mut u8) -> Result<(), DisplayFault> {
    if data.len() < 8 {
        return Err(DisplayFault::InvalidFrame { cob_id: COBID_TPDO1_PIS });
    }

    let line_number    = u16::from_le_bytes([data[0], data[1]]);
    let dest_idx       = u32::from_le_bytes([data[2], data[3], data[4], data[5]]);
    let _zone_flags    = data[6];
    let seq            = data[7];

    if seq != *expected_seq {
        let fault = DisplayFault::UnexpectedSequence {
            expected: *expected_seq,
            received: seq,
        };
        *expected_seq = seq.wrapping_add(1); // Re-sync
        return Err(fault);
    }
    *expected_seq = seq.wrapping_add(1);

    let destination = lookup_destination(dest_idx);
    println!("[DISPLAY] Line {:04X} → Destination: {}", line_number, destination);

    Ok(())
}

/// Process PDO2 – Next stop name
fn handle_pdo2(data: &[u8]) {
    if data.len() < 8 { return; }
    // Extract null-terminated ASCII stop name from bytes 0–6
    let name_bytes: Vec<u8> = data[0..7].iter()
        .copied()
        .take_while(|&b| b != 0)
        .collect();
    let next_stop = String::from_utf8_lossy(&name_bytes);
    let stop_index = data[7];
    println!("[DISPLAY] Next stop ({:02}): {}", stop_index, next_stop);
}

fn main() -> socketcan::Result<()> {
    let sock = CanSocket::open("can0")?;
    // Set receive timeout so we can periodically check the watchdog
    sock.set_read_timeout(Duration::from_millis(500))?;

    println!("[Display Node] Waiting for PIS data on can0...");

    let mut last_heartbeat = Instant::now();
    let mut expected_seq: u8 = 0;
    let mut faults: Vec<DisplayFault> = Vec::new();

    loop {
        // Check watchdog before blocking read
        if last_heartbeat.elapsed() > HEARTBEAT_TIMEOUT {
            let fault = DisplayFault::HeartbeatTimeout;
            eprintln!("[DISPLAY] FAULT: {:?} — activating fallback display mode", fault);
            faults.push(fault);
            // In production: switch display to cached "No service data" screen
            last_heartbeat = Instant::now(); // Reset to avoid flood of fault messages
        }

        match sock.read_frame() {
            Ok(frame) => {
                let id = match frame.id() {
                    socketcan::Id::Standard(sid) => sid.as_raw(),
                    socketcan::Id::Extended(eid) => {
                        eprintln!("[DISPLAY] Unexpected extended frame, ignoring");
                        continue;
                    }
                };
                let data = frame.data();

                match id {
                    x if x == COBID_TPDO1_PIS => {
                        if let Err(e) = handle_pdo1(data, &mut expected_seq) {
                            eprintln!("[DISPLAY] PDO1 error: {:?}", e);
                            faults.push(e);
                        }
                    }
                    x if x == COBID_TPDO2_PIS => {
                        handle_pdo2(data);
                    }
                    x if x == COBID_HEARTBEAT_PIS => {
                        last_heartbeat = Instant::now();
                        let state = data.first().copied().unwrap_or(0);
                        println!("[DISPLAY] Heartbeat from PIS server, state=0x{:02X}", state);
                    }
                    other => {
                        // Ignore frames not addressed to PIS subsystem
                        let _ = other;
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock
                   || e.kind() == std::io::ErrorKind::TimedOut =>
            {
                // Timeout: loop back to watchdog check
            }
            Err(e) => {
                eprintln!("[DISPLAY] CAN read error: {}", e);
            }
        }
    }
}
```

---

### Example 6: Safety PDO Validation in Rust with Type-Safe State Machine

A type-safe Rust implementation of the braking controller that uses Rust's type system to enforce safety state transitions.

```rust
//! safety_brake_controller/src/main.rs
//!
//! Safety-critical Brake Control Unit (BCU) – CANopen Safety PDO Receiver
//! Uses Rust type system to enforce valid state transitions.
//! Demonstrates how Rust ownership and enums complement SIL 2 design.

use std::time::{Duration, Instant};

/// Brake system states – invalid transitions are unrepresentable
#[derive(Debug, Clone, PartialEq)]
enum BrakeState {
    /// Normal operation – awaiting commands
    Idle,
    /// Service brake applied with given deceleration in cm/s²
    ServiceBrake { deceleration_cm_s2: i16 },
    /// Emergency brake – maximum deceleration, cannot return to Idle directly
    EmergencyBrake,
    /// Fault state – requires reset procedure
    Fault { code: u8, description: &'static str },
}

/// Safety PDO received from TCU (Traction Control Unit)
#[derive(Debug, Clone, Copy)]
struct SafetyBrakePdo {
    consecutive_number:   u8,
    deceleration_demand:  i16,
    control_flags:        u8,
    watchdog_time_ms:     u8,
    crc16:                u16,
}

/// PDO flag bits
const FLAG_EMERGENCY: u8 = 1 << 0;
const FLAG_SERVICE:   u8 = 1 << 1;
const FLAG_VALID:     u8 = 1 << 7;

/// CRC-16/IBM-3740 (polynomial 0x1021, init 0xFFFF)
fn crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ 0x1021
            } else {
                crc << 1
            };
        }
    }
    crc
}

/// Deserialise and validate a Safety Brake PDO from raw bytes
fn parse_safety_pdo(raw: &[u8; 8], expected_cn: u8)
    -> Result<SafetyBrakePdo, &'static str>
{
    // CRC covers bytes 0–5 (payload), CRC is in bytes 6–7
    let computed_crc = crc16(&raw[0..6]);
    let received_crc = u16::from_le_bytes([raw[6], raw[7]]);

    if computed_crc != received_crc {
        return Err("CRC mismatch – frame corrupted");
    }

    let pdo = SafetyBrakePdo {
        consecutive_number:  raw[0],
        deceleration_demand: i16::from_le_bytes([raw[1], raw[2]]),
        control_flags:       raw[3],
        watchdog_time_ms:    raw[4],
        crc16:               received_crc,
    };

    if pdo.control_flags & FLAG_VALID == 0 {
        return Err("VALID flag not set – PDO rejected");
    }

    if pdo.consecutive_number != expected_cn {
        return Err("Consecutive number error – message lost or duplicated");
    }

    Ok(pdo)
}

/// Apply a validated PDO to the brake state machine
fn apply_brake_pdo(state: &BrakeState, pdo: &SafetyBrakePdo)
    -> Result<BrakeState, &'static str>
{
    match state {
        BrakeState::Fault { .. } => {
            // Cannot accept new brake commands in fault state
            Err("BCU in fault state – manual reset required")
        }
        BrakeState::EmergencyBrake => {
            // Once in emergency brake, only a safe stop can clear it
            if pdo.control_flags & FLAG_EMERGENCY != 0 {
                Ok(BrakeState::EmergencyBrake)  // Maintain
            } else {
                Err("Cannot release emergency brake via normal PDO")
            }
        }
        BrakeState::Idle | BrakeState::ServiceBrake { .. } => {
            if pdo.control_flags & FLAG_EMERGENCY != 0 {
                println!("[BCU] Emergency brake commanded!");
                Ok(BrakeState::EmergencyBrake)
            } else if pdo.control_flags & FLAG_SERVICE != 0 {
                Ok(BrakeState::ServiceBrake {
                    deceleration_cm_s2: pdo.deceleration_demand,
                })
            } else {
                Ok(BrakeState::Idle)
            }
        }
    }
}

fn main() {
    let mut bcu_state = BrakeState::Idle;
    let mut expected_cn: u8 = 0;

    println!("=== BCU Safety Brake PDO Demonstration ===\n");

    // --- Scenario 1: Valid service brake demand ---
    // Construct a raw Safety PDO (normally received over CAN)
    let mut raw: [u8; 8] = [0u8; 8];
    raw[0] = expected_cn;                          // CN
    raw[1..3].copy_from_slice(&(-120i16).to_le_bytes()); // -120 cm/s²
    raw[3] = FLAG_SERVICE | FLAG_VALID;
    raw[4] = 100;                                  // 100 ms watchdog
    raw[5] = 0;
    let crc = crc16(&raw[0..6]);
    raw[6..8].copy_from_slice(&crc.to_le_bytes());

    match parse_safety_pdo(&raw, expected_cn) {
        Ok(pdo) => {
            expected_cn = expected_cn.wrapping_add(1);
            match apply_brake_pdo(&bcu_state, &pdo) {
                Ok(new_state) => {
                    println!("[BCU] State: {:?} → {:?}", bcu_state, new_state);
                    bcu_state = new_state;
                }
                Err(e) => eprintln!("[BCU] State transition error: {}", e),
            }
        }
        Err(e) => eprintln!("[BCU] PDO validation failed: {}", e),
    }

    // --- Scenario 2: Emergency brake command ---
    raw[0] = expected_cn;
    raw[1..3].copy_from_slice(&(-300i16).to_le_bytes());
    raw[3] = FLAG_EMERGENCY | FLAG_VALID;
    raw[4] = 50;
    let crc = crc16(&raw[0..6]);
    raw[6..8].copy_from_slice(&crc.to_le_bytes());

    match parse_safety_pdo(&raw, expected_cn) {
        Ok(pdo) => {
            expected_cn = expected_cn.wrapping_add(1);
            match apply_brake_pdo(&bcu_state, &pdo) {
                Ok(new_state) => {
                    println!("[BCU] State: {:?} → {:?}", bcu_state, new_state);
                    bcu_state = new_state;
                }
                Err(e) => eprintln!("[BCU] State transition error: {}", e),
            }
        }
        Err(e) => eprintln!("[BCU] PDO validation failed: {}", e),
    }

    // --- Scenario 3: CRC error injection ---
    println!("\n[BCU] Injecting CRC error...");
    raw[0] = expected_cn;
    raw[3] = FLAG_SERVICE | FLAG_VALID;
    let crc = crc16(&raw[0..6]);
    raw[6..8].copy_from_slice(&crc.to_le_bytes());
    raw[2] ^= 0xFF;  // Corrupt one byte after CRC computation

    match parse_safety_pdo(&raw, expected_cn) {
        Ok(_) => println!("[BCU] ERROR: Corrupted PDO accepted — safety failure!"),
        Err(e) => println!("[BCU] Correctly rejected corrupted PDO: {}", e),
    }

    println!("\nFinal BCU state: {:?}", bcu_state);
}
```

---

## Fault Handling and Redundancy

### Fault Categories in Railway CAN Systems

| Fault Type | Detection Mechanism | Response |
|---|---|---|
| Node offline | Heartbeat timeout | NMT reset command; raise VCU alarm |
| Frame corruption | CAN hardware CRC; software CRC (safety PDO) | Discard frame; increment error counter |
| Bus-off condition | CAN controller error counter overflow | Re-initialise controller; switch to redundant bus |
| Consecutive number gap | Safety PDO sequence check | Inhibit actuator; demand safe state |
| Short circuit / wire break | CAN differential signal monitoring | Fault isolation; redundant path activation |

### Error Counter Management (C)

```c
/* Simplified CAN error counter management for railway BCU */
#include <stdint.h>
#include <stdbool.h>

#define CAN_ERROR_THRESHOLD_WARN   5
#define CAN_ERROR_THRESHOLD_FAULT  10

typedef struct {
    uint32_t  tx_error_count;
    uint32_t  rx_error_count;
    uint32_t  bus_off_count;
    bool      redundant_bus_active;
} CanBusStatus;

static CanBusStatus bus_a = {0};
static CanBusStatus bus_b = {0};

void handle_can_error_interrupt(CanBusStatus *bus, bool is_bus_off) {
    if (is_bus_off) {
        bus->bus_off_count++;
        if (!bus->redundant_bus_active) {
            /* Switch to redundant bus */
            bus->redundant_bus_active = true;
            /* Trigger CAN controller re-initialisation on next cycle */
        }
    } else {
        bus->rx_error_count++;
    }

    if (bus->rx_error_count >= CAN_ERROR_THRESHOLD_FAULT) {
        /* Notify VCU safety monitor */
    }
}
```

---

## Testing and Certification Considerations

### Hardware-in-the-Loop (HIL) Testing

Railway CAN systems undergo extensive HIL testing before homologation. A typical HIL bench for a door subsystem includes:

- Real Door Control Unit (DCU) hardware
- CAN bus analyser (e.g., Vector CANalyzer, PEAK PCAN)
- Load simulator (motor emulation)
- Fault injection hardware (bus shorting, bit error injection)
- VCU simulator running on RTOS

### Required Test Evidence for SIL 2 (EN 50128)

- Formal requirements traceability matrix (RTM)
- Unit test coverage ≥ MC/DC (Modified Condition/Decision Coverage)
- Static analysis (MISRA-C compliance)
- Fault injection test results demonstrating correct safety reactions
- Timing analysis: worst-case message latency on loaded CAN bus

### MISRA-C Considerations for Railway CAN Code

```c
/* MISRA-C:2012 compliant CAN ID construction */
/* Rule 10.1: Essential type checks on CAN ID */
/* Rule 14.4: Boolean conditions in if statements */

#include <stdint.h>
#include <stdbool.h>

#define CAN_SFF_MASK  ((uint32_t)0x000007FFU)

/* MISRA-compliant: explicit cast and mask */
static bool is_valid_can_id(uint32_t id) {
    return ((id & ~CAN_SFF_MASK) == 0U);   /* Rule 10.1: unsigned comparison */
}

static uint32_t make_canopen_cobid(uint16_t function_code, uint8_t node_id) {
    /* Rule 12.2: No undefined shift behaviour */
    return (((uint32_t)function_code << 7U) | (uint32_t)node_id) & CAN_SFF_MASK;
}
```

### Bit Rate Selection

| Application | Typical Bit Rate | Max Bus Length |
|---|---|---|
| Safety-critical control (braking, doors) | 250 kbit/s | ~250 m |
| PIS display network | 125 kbit/s | ~500 m |
| Short intra-cabinet segments | 500 kbit/s – 1 Mbit/s | ~40–100 m |

Railway vehicles can span 20–200 m, and trains can exceed 300 m; bit rate selection carefully balances propagation delay against required message cycle times.

---

## Summary

CAN bus occupies a well-established and strategically important role in modern railway systems. Its strengths — multi-master arbitration, robust error detection, deterministic priority scheduling, and cost-effective implementation — align naturally with the requirements of rail subsystems ranging from safety-critical braking and door control to passenger-facing information displays.

**Key architectural points:**

- CAN is deployed at the device/subsystem tier of the Train Communication Network (TCN) hierarchy, below the IEC 61375 MVB or WTB that spans whole vehicles and consists.
- **CANopen** (CiA 301, IEC 61375-3-3) is the dominant higher-layer protocol, providing standardised NMT, PDO, SDO, and heartbeat mechanisms used across traction, braking, HVAC, door, and PIS subsystems.
- **Safety-critical applications** (SIL 2–3) require an additional safety layer — most commonly **CANopen Safety (EN 50325-5)** — adding consecutive numbering, CRC, and watchdog mechanisms above standard CAN frames.
- The **EN 50155, EN 50128, and EN 50129** standards govern the environmental robustness, software development process, and system safety requirements that all railway CAN implementations must satisfy.

**Programming considerations:**

- In **C/C++**, SocketCAN on Linux provides a portable development and test interface; production embedded implementations use BSP-specific CAN drivers with MISRA-C compliance, interrupt-driven error handling, and hardware watchdogs.
- In **Rust**, the `socketcan` crate enables expressive, memory-safe CAN programming. Rust's ownership model and algebraic types are a natural fit for safety-critical state machines, reducing the risk of use-after-free, integer overflow, and invalid state transitions that are particularly dangerous in railway contexts.
- Both languages require careful attention to **message timing** (ensuring PDOs meet cycle time budgets even under peak bus load), **error counter management** (bus-off recovery, redundant bus switchover), and **sequence/CRC validation** for safety-relevant data.

The ongoing evolution of railways toward **CAN FD** and **Ethernet (IEC 61375-3-4)** will progressively extend or replace classical CAN in bandwidth-intensive applications such as CCTV, high-resolution PIS, and TCMS diagnostics — but classical CAN and CANopen will remain deeply embedded in cost-sensitive and legacy-compatible subsystems for the foreseeable future.

---

*Document: 80 – CAN in Railway Systems | Series: CAN Bus Technology Reference*