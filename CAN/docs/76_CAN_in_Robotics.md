# 76. CAN in Robotics

**Conceptual coverage:**
- Why CAN is preferred over EtherCAT, RS-485, and SPI/I²C in robotic systems
- Message ID allocation strategy with priority tiers (E-stop → SYNC → PDOs → SDOs)
- Timing budget analysis showing how a 1 kHz control loop fits within a 1 Mbit/s CAN bus
- SYNC/PDO isochronous mechanism (all drives latch simultaneously)
- Bus load calculation showing why CAN FD is needed beyond ~6 axes at 1 kHz
- Sensor fusion pipeline: IMU + F/T sensor timestamping and complementary filter
- CANopen NMT state machine and CiA 402 drive profile
- Emergency (EMCY) fault handling and heartbeat monitoring

**C/C++ examples:**
1. `socketcan_robot.c` — Raw SocketCAN frame I/O, receive filters, hardware timestamps, full 6-axis control loop
2. `canopen_nmt.cpp` — C++ NMT master managing node states, heartbeat monitoring, and emergency stop
3. `sensor_fusion.cpp` — IMU + force/torque data ingestion with a complementary orientation filter

**Rust examples:**
1. Async joint controller with `socketcan` + `tokio` — precise 1 kHz cycle timing, setpoint dispatch, feedback collection
2. CANopen Emergency frame parser and fault state machine with safety callbacks
3. Multi-axis linear trajectory interpolator dispatching to all joints via SYNC


> Using CAN for real-time motor control, sensor fusion, and coordinated multi-axis robot communication.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why CAN for Robotics?](#why-can-for-robotics)
3. [CAN Bus Fundamentals in a Robotics Context](#can-bus-fundamentals-in-a-robotics-context)
4. [Real-Time Motor Control](#real-time-motor-control)
5. [Sensor Fusion over CAN](#sensor-fusion-over-can)
6. [Multi-Axis Coordinated Motion](#multi-axis-coordinated-motion)
7. [CAN FD for High-Bandwidth Robotics](#can-fd-for-high-bandwidth-robotics)
8. [Error Handling and Fault Tolerance](#error-handling-and-fault-tolerance)
9. [Code Examples: C/C++](#code-examples-cc)
10. [Code Examples: Rust](#code-examples-rust)
11. [Summary](#summary)

---

## Introduction

The Controller Area Network (CAN) bus, originally designed for automotive applications in the 1980s by Bosch, has become one of the most widely adopted communication protocols in robotics engineering. Its robustness, real-time determinism, multi-master capability, and excellent noise immunity make it ideally suited for the demanding requirements of robotic systems — from industrial manipulators and collaborative robots (cobots) to mobile platforms, exoskeletons, and autonomous vehicles.

In modern robotic systems, dozens of nodes — motor controllers (servo drives), encoders, IMUs, force/torque sensors, safety controllers, and HMI panels — must exchange data reliably at rates sufficient to close control loops at hundreds to thousands of Hz. CAN provides the electrical and protocol foundation to achieve this in a cost-effective, deterministic manner.

---

## Why CAN for Robotics?

| Requirement | CAN Solution |
|---|---|
| Real-time guarantees | Priority-based arbitration ensures high-priority messages win bus access |
| Noise immunity | Differential signaling (CAN_H / CAN_L) rejects common-mode interference |
| Multi-drop wiring | Single twisted pair connects all nodes — reduces cable harness complexity |
| Fault containment | Nodes autonomously enter Bus-Off state rather than corrupting the bus |
| Standardized upper layers | CANopen, EtherCAT-over-CAN, ROS 2 / micro-ROS support |
| Low latency | Up to 1 Mbit/s (classic CAN) or 8 Mbit/s (CAN FD) |
| Proven ecosystem | Hundreds of servo drives, IMUs, and sensors ship with native CAN interfaces |

### Comparison with Alternatives

- **EtherCAT / Ethernet**: Higher bandwidth but more complex, less noise-immune in harsh environments.
- **RS-485 / Modbus**: Simpler but master-slave only, no built-in arbitration or error frames.
- **SPI / I²C**: Board-level only, not suitable for distributed robotic joints.
- **CAN FD**: Superset of CAN — same physical layer, larger payloads (up to 64 bytes) and higher data-phase bit rates.

---

## CAN Bus Fundamentals in a Robotics Context

### Frame Types Used in Robotics

```
Standard Data Frame (11-bit ID):
┌──────┬────┬─────────────┬─────┬──────────────────┬─────┬──────┐
│ SOF  │ ID │  Control    │ DLC │   Data (0–8 B)   │ CRC │ ACK  │
│  1b  │11b │    6b       │ 4b  │    0–64 bits     │ 16b │  2b  │
└──────┴────┴─────────────┴─────┴──────────────────┴─────┴──────┘
```

### Message ID Allocation Strategy (Robotics)

A well-designed robotic CAN network assigns message IDs based on **priority** (lower ID = higher priority) and **function**:

```
ID Range    Priority    Function
0x001–0x07F High        Safety stop, E-stop, fault broadcast
0x080–0x0FF High        Sync pulse, NMT (CANopen)
0x100–0x17F Medium-High PDO (Process Data Objects) — setpoints
0x180–0x4FF Medium      PDO feedback — positions, velocities, torques
0x500–0x57F Low         SDO (Service Data Objects) — configuration
0x580–0x5FF Low         SDO responses
0x600–0x67F Low         Heartbeat / node guarding
0x700–0x7FF Lowest      Diagnostic / logging
```

### Timing Budget for a Closed Control Loop

```
Task                         Worst-case time
────────────────────────────────────────────
Setpoint generation          50–200 µs
CAN frame serialisation      ~5 µs
Bus arbitration + tx (1M)    ~130 µs (8-byte frame)
Node processing              50–300 µs
Feedback frame tx            ~130 µs
Host reception + decode      ~10 µs
────────────────────────────────────────────
Total round-trip (typ.)      ~0.5–1.0 ms  →  1 kHz loop feasible
```

---

## Real-Time Motor Control

### Architecture

A typical joint control architecture over CAN:

```
┌──────────────────────────────────────────────────────┐
│                  Robot Controller (PC / SoC)          │
│  ┌──────────────┐   ┌──────────────┐                 │
│  │ Trajectory   │→  │ CAN Master   │                 │
│  │ Planner      │   │ (SocketCAN / │                 │
│  └──────────────┘   │  CANopen)    │                 │
│                     └──────┬───────┘                 │
└────────────────────────────│────────────────────────┘
                             │ CAN Bus (twisted pair, 120Ω termination)
         ┌───────────────────┼───────────────┐
         │                   │               │
   ┌─────┴─────┐       ┌─────┴─────┐   ┌────┴──────┐
   │ Joint 1   │       │ Joint 2   │   │ Joint 3   │
   │ Servo     │       │ Servo     │   │ Servo     │
   │ Drive     │       │ Drive     │   │ Drive     │
   │ (CANopen) │       │ (CANopen) │   │ (CANopen) │
   └─────┬─────┘       └─────┬─────┘   └────┬──────┘
         │ Encoder            │               │ Force/Torque
         │ Feedback           │               │ Sensor
```

### CANopen CiA 402 Profile (Standard for Drives)

Most industrial servo drives implement the **CiA 402** drive profile over CANopen:

- **Modes**: Cyclic Synchronous Position (CSP), Cyclic Synchronous Velocity (CSV), Cyclic Synchronous Torque (CST), Profile Position, Homing.
- **PDO Mapping**: Process Data Objects carry setpoints and feedback each sync cycle.
- **SYNC Object**: Master broadcasts a sync frame (ID 0x080) that triggers all drives to latch setpoints and send feedback simultaneously — enabling isochronous operation.

### Synchronisation Mechanism

```
Time →
  0ms    1ms    2ms    3ms
  │      │      │      │
  ▼      ▼      ▼      ▼
SYNC   SYNC   SYNC   SYNC    (Master broadcasts 0x80, period = 1ms)
  │      │      │      │
  ├─SP1──┤      │      │     Setpoint PDOs from master to drives
  ├─SP2──┤      │      │
  ├─SP3──┤      │      │
  │      │      │      │
  ├──FB1─┤      │      │     Feedback PDOs from drives to master
  ├──FB2─┤      │      │
  ├──FB3─┤      │      │
```

---

## Sensor Fusion over CAN

Robotic systems integrate multiple sensor types on the CAN bus to build a consistent world model:

### Common Sensors with CAN Interfaces

| Sensor | Typical CAN Message Rate | Data |
|---|---|---|
| IMU (9-DOF) | 500 Hz – 1 kHz | Acceleration, gyro, magnetometer, quaternion |
| Encoder (absolute) | On-demand / 1 kHz | Position, velocity |
| Force/Torque sensor | 500 Hz – 1 kHz | 6-axis wrench (Fx, Fy, Fz, Tx, Ty, Tz) |
| LiDAR (compact) | 100 Hz | Distance scan data |
| Temperature sensor | 10–100 Hz | Motor / ambient temperature |
| Current sensor | 1–10 kHz | Phase currents (for FOC) |

### Data Fusion Pipeline

```
CAN Bus
  │
  ├── IMU frames (ID 0x201)       ──┐
  ├── Encoder frames (ID 0x181)   ──┤──→ Timestamp buffer ──→ EKF / UKF ──→ Robot State
  ├── F/T sensor (ID 0x251)       ──┤     (per joint)         Estimator      (pose, vel,
  └── Current sensors (ID 0x301)  ──┘                                         wrench)
```

### Timestamping Strategy

Because CAN does not carry a hardware timestamp in the frame itself, robotics systems use one of these approaches:

1. **Receive-side timestamping**: The host CAN controller or SocketCAN applies a kernel timestamp when the frame is received. Accurate to ~10–50 µs.
2. **Embedded timestamp in payload**: The sensor node encodes its local timestamp (µs resolution) in the first 4 bytes of the payload.
3. **SYNC-based alignment**: All nodes latch measurements on the SYNC pulse; the host knows all feedback corresponds to the same instant.

---

## Multi-Axis Coordinated Motion

### Synchronised Multi-Axis Control

Coordinated motion (e.g., Cartesian path following on a 6-DOF arm) requires all joints to receive their setpoints and execute them at the same instant:

```
Interpolation cycle (e.g., 1 ms):

1. Controller computes IK → [θ₁, θ₂, θ₃, θ₄, θ₅, θ₆]
2. Master queues setpoint PDOs for all 6 joints
3. Master sends SYNC frame (0x080)
4. All drives latch new setpoints simultaneously
5. Drives execute motion for 1 ms
6. Drives send feedback PDOs
7. Master collects feedback, checks errors, loops
```

### Bus Load Calculation

For a 6-DOF arm running at 1 kHz with CAN at 1 Mbit/s:

```
Per frame bit count (8-byte data, 11-bit ID):
  1 + 11 + 1 + 1 + 1 + 4 + 64 + 15 + 1 + 1 + 1 + 7 = ~108 bits (stuffing +~20%)
  Effective: ~130 bits per frame ≈ 130 µs at 1 Mbit/s

Messages per cycle (1 ms):
  1 SYNC + 6 setpoint PDOs + 6 feedback PDOs = 13 frames
  13 × 130 µs = 1.69 ms  ← exceeds 1 ms cycle!

Resolution: Use CAN FD (5 Mbit/s data phase):
  8-byte CAN FD frame ≈ 70 µs at 5 Mbit/s
  13 × 70 µs = 0.91 ms  ← fits within 1 ms cycle ✓
```

### Priority Assignment (Rate Monotonic)

Assign lower CAN IDs (higher priority) to higher-frequency messages:

```
Message              Rate     CAN ID (priority)
──────────────────────────────────────────────
E-stop               Event    0x001  (highest)
SYNC pulse           1 kHz    0x080
Joint setpoints      1 kHz    0x201–0x206
Joint feedback       1 kHz    0x181–0x186
F/T sensor           500 Hz   0x251
IMU                  500 Hz   0x301
Temperature          50 Hz    0x701–0x706
Configuration SDOs   On-demand 0x601–0x606  (lowest)
```

---

## CAN FD for High-Bandwidth Robotics

CAN Flexible Data-rate (CAN FD, ISO 11898-1:2015) extends classic CAN:

- **Payload**: Up to 64 bytes (vs. 8 bytes in classic CAN).
- **Data phase bit rate**: Up to 8 Mbit/s (arbitration phase still max 1 Mbit/s for compatibility).
- **Backward compatible** at the physical layer — same connectors, terminators.

### When to Choose CAN FD in Robotics

- High-DOF systems (>6 joints) where bus load exceeds 80% with classic CAN.
- Dense sensor fusion requiring >8 bytes per message (e.g., full quaternion + angular velocity + acceleration in one frame).
- Firmware update over CAN (FOTA) requiring high throughput.
- EtherCAT-over-CAN-FD bridges.

---

## Error Handling and Fault Tolerance

### CAN Error Counters

Each CAN node maintains:
- **TEC** (Transmit Error Counter)
- **REC** (Receive Error Counter)

States: `Error Active` → `Error Passive` (TEC/REC ≥ 128) → `Bus Off` (TEC ≥ 256).

### Robotics-Specific Fault Handling

```
Fault Event          Detection                  Action
─────────────────────────────────────────────────────────────────
Node heartbeat lost  Timeout on 0x700+nodeID    Safe torque off (STO)
Position error       |cmd − actual| > threshold  Fault reaction (stop)
Overcurrent          Drive internal             Node sends EMCY frame
Bus-Off node         Host detects missing FB    Controlled shutdown
Watchdog timeout     Hardware watchdog          MCU reset, re-init CAN
```

### Emergency (EMCY) Object (CANopen)

When a drive detects a fault, it broadcasts an Emergency frame:

```
CAN ID: 0x080 + Node-ID
Data:   [Error Code (2B)] [Error Register (1B)] [Manufacturer-specific (5B)]

Example: Joint 3 (Node-ID=3) overcurrent:
  ID: 0x083
  Data: 0x2310 0x04 0x00 0x00 0x00 0x00 0x00
         ↑ Overcurrent  ↑ Current device = fault
```

---

## Code Examples: C/C++

### 1. SocketCAN Setup and Basic Frame I/O (Linux)

```c
/**
 * socketcan_robot.c
 * SocketCAN interface for robotic joint control on Linux.
 * Compile: gcc -o socketcan_robot socketcan_robot.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <errno.h>

#define CAN_IFACE       "can0"
#define SYNC_ID         0x080
#define BASE_SETPOINT   0x200   /* 0x201–0x206 for joints 1–6 */
#define BASE_FEEDBACK   0x180   /* 0x181–0x186 for joints 1–6 */
#define NUM_JOINTS      6

typedef struct {
    int32_t position_counts;    /* Encoder counts */
    int16_t velocity_rpm;       /* RPM × 10 */
    int16_t torque_mnm;         /* mNm */
} __attribute__((packed)) JointFeedback;

typedef struct {
    int32_t target_position;    /* Encoder counts */
    int16_t max_velocity;       /* RPM × 10 */
    int16_t feed_forward_torque;/* mNm */
} __attribute__((packed)) JointSetpoint;

int open_can_socket(const char *iface) {
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) { perror("socket"); return -1; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(s, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_index,
    };

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(s); return -1;
    }

    /* Enable hardware receive timestamps */
    int enable = 1;
    setsockopt(s, SOL_SOCKET, SO_TIMESTAMP, &enable, sizeof(enable));

    /* Set receive filter: only accept feedback PDOs and EMCY */
    struct can_filter rfilter[2] = {
        { .can_id = BASE_FEEDBACK, .can_mask = 0x780 }, /* 0x180–0x1FF */
        { .can_id = 0x080,         .can_mask = 0x780 }, /* EMCY 0x080–0x0FF */
    };
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, rfilter, sizeof(rfilter));

    return s;
}

/**
 * Send a SYNC frame — all drives latch setpoints simultaneously.
 */
int send_sync(int sock) {
    struct can_frame frame = {
        .can_id  = SYNC_ID,
        .can_dlc = 0,  /* SYNC has no data bytes */
    };
    return write(sock, &frame, sizeof(frame));
}

/**
 * Send a position setpoint to one joint (CiA 402 CSP mode).
 * joint_id: 1–6
 */
int send_setpoint(int sock, uint8_t joint_id, const JointSetpoint *sp) {
    struct can_frame frame;
    frame.can_id  = BASE_SETPOINT + joint_id;
    frame.can_dlc = sizeof(JointSetpoint);
    memcpy(frame.data, sp, sizeof(JointSetpoint));
    return write(sock, &frame, sizeof(frame));
}

/**
 * Receive a feedback PDO.
 * Returns the joint_id (1–6) or -1 on error.
 */
int recv_feedback(int sock, JointFeedback *fb_out, uint8_t *joint_id_out,
                  struct timeval *tv_out) {
    struct can_frame frame;
    struct msghdr   msg   = {0};
    struct iovec    iov   = { .iov_base = &frame, .iov_len = sizeof(frame) };
    char            ctlbuf[64];

    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = ctlbuf;
    msg.msg_controllen = sizeof(ctlbuf);

    ssize_t nbytes = recvmsg(sock, &msg, 0);
    if (nbytes < 0) return -1;

    /* Extract hardware timestamp */
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET &&
            cmsg->cmsg_type  == SO_TIMESTAMP) {
            memcpy(tv_out, CMSG_DATA(cmsg), sizeof(*tv_out));
        }
    }

    uint8_t jid = (uint8_t)(frame.can_id - BASE_FEEDBACK);
    if (jid < 1 || jid > NUM_JOINTS) return -1;

    *joint_id_out = jid;
    memcpy(fb_out, frame.data, sizeof(JointFeedback));
    return (int)jid;
}

/* ── Main control loop ──────────────────────────────────────── */
int main(void) {
    int sock = open_can_socket(CAN_IFACE);
    if (sock < 0) return EXIT_FAILURE;

    printf("CAN socket open on %s\n", CAN_IFACE);

    JointSetpoint setpoints[NUM_JOINTS + 1] = {0}; /* index 1–6 */
    JointFeedback feedback[NUM_JOINTS + 1]  = {0};

    /* Simple demo: hold all joints at position 0, send 1000 cycles */
    for (int cycle = 0; cycle < 1000; cycle++) {

        /* 1. Update setpoints (from trajectory planner — placeholder) */
        for (int j = 1; j <= NUM_JOINTS; j++) {
            setpoints[j].target_position     = 0;
            setpoints[j].max_velocity        = 1000; /* 100 RPM */
            setpoints[j].feed_forward_torque = 0;
        }

        /* 2. Send setpoints to all joints */
        for (int j = 1; j <= NUM_JOINTS; j++) {
            if (send_setpoint(sock, (uint8_t)j, &setpoints[j]) < 0) {
                fprintf(stderr, "Failed to send setpoint for joint %d\n", j);
            }
        }

        /* 3. Broadcast SYNC — drives latch setpoints */
        send_sync(sock);

        /* 4. Collect feedback (with 2 ms timeout) */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        struct timeval timeout = { .tv_sec = 0, .tv_usec = 2000 };

        int ready = select(sock + 1, &rfds, NULL, NULL, &timeout);
        if (ready > 0) {
            uint8_t        jid;
            JointFeedback  fb;
            struct timeval ts;
            int r = recv_feedback(sock, &fb, &jid, &ts);
            if (r > 0) {
                feedback[jid] = fb;
                if (cycle % 100 == 0) {
                    printf("[cycle %4d] Joint %d: pos=%d counts, vel=%d RPM*10\n",
                           cycle, jid, fb.position_counts, fb.velocity_rpm);
                }
            }
        }

        /* 5. Sleep for remainder of 1 ms cycle */
        usleep(1000);
    }

    close(sock);
    return EXIT_SUCCESS;
}
```

---

### 2. CANopen NMT State Machine and Heartbeat (C++)

```cpp
/**
 * canopen_nmt.cpp
 * CANopen NMT master: bring nodes to Operational state,
 * monitor heartbeats, and handle Bus-Off recovery.
 */

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

extern "C" {
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
}

namespace canopen {

enum class NmtCommand : uint8_t {
    Start         = 0x01,
    Stop          = 0x02,
    EnterPreOp    = 0x80,
    ResetNode     = 0x81,
    ResetComm     = 0x82,
};

enum class NodeState : uint8_t {
    BootUp       = 0x00,
    Stopped      = 0x04,
    Operational  = 0x05,
    PreOp        = 0x7F,
    Unknown      = 0xFF,
};

constexpr uint32_t NMT_ID         = 0x000;
constexpr uint32_t HEARTBEAT_BASE = 0x700;
constexpr int      NUM_JOINTS     = 6;

struct NodeInfo {
    NodeState               state{NodeState::Unknown};
    std::chrono::steady_clock::time_point last_heartbeat{};
    uint32_t                missed_heartbeats{0};
};

class CanOpenMaster {
public:
    explicit CanOpenMaster(const std::string &iface) {
        sock_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (sock_ < 0) throw std::runtime_error("socket() failed");

        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
        ioctl(sock_, SIOCGIFINDEX, &ifr);

        sockaddr_can addr{};
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifr.ifr_index;
        bind(sock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

        for (int j = 1; j <= NUM_JOINTS; j++) {
            nodes_[j] = NodeInfo{};
        }
    }

    ~CanOpenMaster() { if (sock_ >= 0) close(sock_); }

    /** Send NMT command to one node (node_id=0 → broadcast) */
    void send_nmt(uint8_t node_id, NmtCommand cmd) {
        can_frame frame{};
        frame.can_id  = NMT_ID;
        frame.can_dlc = 2;
        frame.data[0] = static_cast<uint8_t>(cmd);
        frame.data[1] = node_id;
        write(sock_, &frame, sizeof(frame));
    }

    /** Bring all joints to Operational state */
    void start_all_nodes() {
        std::cout << "[NMT] Reset all nodes...\n";
        send_nmt(0, NmtCommand::ResetNode);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::cout << "[NMT] Enter Pre-Operational...\n";
        send_nmt(0, NmtCommand::EnterPreOp);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        /* Configure PDOs and other SDO parameters here */
        configure_pdos();

        std::cout << "[NMT] Start all nodes (Operational)...\n";
        send_nmt(0, NmtCommand::Start);
    }

    /**
     * Monitor heartbeat frames.
     * Returns false if any node has missed more than max_misses heartbeats.
     */
    bool check_heartbeats(uint32_t timeout_ms = 200, uint32_t max_misses = 3) {
        auto now = std::chrono::steady_clock::now();
        bool all_ok = true;

        /* Non-blocking read of all pending heartbeat frames */
        can_frame frame;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock_, &rfds);
        timeval tv{0, 0};

        while (select(sock_ + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            ssize_t n = read(sock_, &frame, sizeof(frame));
            if (n < 0) break;

            uint32_t id = frame.can_id & CAN_SFF_MASK;
            if (id >= HEARTBEAT_BASE && id <= HEARTBEAT_BASE + 127) {
                uint8_t node_id = static_cast<uint8_t>(id - HEARTBEAT_BASE);
                if (nodes_.count(node_id)) {
                    nodes_[node_id].state =
                        static_cast<NodeState>(frame.data[0] & 0x7F);
                    nodes_[node_id].last_heartbeat = now;
                    nodes_[node_id].missed_heartbeats = 0;
                }
            }

            FD_ZERO(&rfds);
            FD_SET(sock_, &rfds);
        }

        /* Check for stale heartbeats */
        for (auto &[id, node] : nodes_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - node.last_heartbeat).count();

            if (elapsed > static_cast<long>(timeout_ms)) {
                node.missed_heartbeats++;
                if (node.missed_heartbeats >= max_misses) {
                    std::cerr << "[FAULT] Joint " << id
                              << " heartbeat lost (" << node.missed_heartbeats
                              << " misses)!\n";
                    all_ok = false;
                }
            }

            if (node.state != NodeState::Operational && node.missed_heartbeats == 0) {
                std::cerr << "[WARN] Joint " << id << " not in Operational state!\n";
            }
        }
        return all_ok;
    }

    void print_node_states() const {
        for (const auto &[id, node] : nodes_) {
            const char *state_str = "?";
            switch (node.state) {
                case NodeState::Operational: state_str = "Operational"; break;
                case NodeState::PreOp:       state_str = "Pre-Op";      break;
                case NodeState::Stopped:     state_str = "Stopped";     break;
                case NodeState::BootUp:      state_str = "Boot-Up";     break;
                default: break;
            }
            std::cout << "  Joint " << static_cast<int>(id)
                      << ": " << state_str
                      << " (misses=" << node.missed_heartbeats << ")\n";
        }
    }

private:
    void configure_pdos() {
        /* In a real system: send SDO writes to configure PDO mappings.
         * Example: set heartbeat producer time to 100 ms on each node.
         * SDO write to object 0x1017 sub 0x00 (Producer Heartbeat Time).
         * This is left as a placeholder for brevity. */
        std::cout << "[SDO] Configuring PDOs and heartbeat period...\n";
    }

    int sock_{-1};
    std::unordered_map<uint8_t, NodeInfo> nodes_;
};

} // namespace canopen

int main() {
    try {
        canopen::CanOpenMaster master("can0");
        master.start_all_nodes();

        std::cout << "Entering monitoring loop. Press Ctrl+C to exit.\n";
        for (int i = 0; i < 100; i++) {
            bool ok = master.check_heartbeats(200, 3);
            if (!ok) {
                std::cerr << "[SAFETY] Heartbeat failure — issuing E-stop!\n";
                master.send_nmt(0, canopen::NmtCommand::Stop);
                break;
            }
            if (i % 10 == 0) {
                std::cout << "\n[Status at cycle " << i << "]\n";
                master.print_node_states();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
```

---

### 3. Sensor Fusion: IMU + F/T Sensor Data Collection (C++)

```cpp
/**
 * sensor_fusion.cpp
 * Collects IMU and Force/Torque sensor data from CAN and
 * feeds a simple complementary filter for orientation estimation.
 */

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

/* CAN IDs */
constexpr uint32_t IMU_ACCEL_ID  = 0x201;  /* Ax, Ay, Az (int16 ×100 m/s²) */
constexpr uint32_t IMU_GYRO_ID   = 0x202;  /* Gx, Gy, Gz (int16 ×100 rad/s) */
constexpr uint32_t FT_FORCES_ID  = 0x251;  /* Fx, Fy, Fz (int16 ×100 N)    */
constexpr uint32_t FT_TORQUES_ID = 0x252;  /* Tx, Ty, Tz (int16 ×100 Nm)   */

struct Vec3 { float x, y, z; };
struct Wrench { Vec3 force; Vec3 torque; };

#pragma pack(push, 1)
struct ImuAccelFrame  { int16_t ax, ay, az, temperature; };
struct ImuGyroFrame   { int16_t gx, gy, gz, pad; };
struct FtForcesFrame  { int16_t fx, fy, fz, status; };
struct FtTorquesFrame { int16_t tx, ty, tz, status; };
#pragma pack(pop)

/** Complementary filter state for roll/pitch from IMU */
struct OrientationFilter {
    float roll{0.f}, pitch{0.f};
    static constexpr float ALPHA = 0.98f; /* gyro weight */

    void update(const Vec3 &accel, const Vec3 &gyro, float dt) {
        /* Accelerometer-derived angles */
        float acc_roll  = std::atan2(accel.y, accel.z);
        float acc_pitch = std::atan2(-accel.x,
                                     std::sqrt(accel.y*accel.y + accel.z*accel.z));

        /* Complementary filter */
        roll  = ALPHA * (roll  + gyro.x * dt) + (1.f - ALPHA) * acc_roll;
        pitch = ALPHA * (pitch + gyro.y * dt) + (1.f - ALPHA) * acc_pitch;
    }
};

class SensorFusionNode {
public:
    /**
     * Process one incoming CAN frame and update internal state.
     * In production this would be called from a CAN receive callback.
     */
    void process_frame(uint32_t can_id, const uint8_t *data, uint64_t timestamp_us) {
        dt_ = (last_timestamp_us_ > 0)
              ? (timestamp_us - last_timestamp_us_) * 1e-6f
              : 0.001f;
        last_timestamp_us_ = timestamp_us;

        if (can_id == IMU_ACCEL_ID) {
            ImuAccelFrame f;
            std::memcpy(&f, data, sizeof(f));
            imu_accel_ = { f.ax / 100.f, f.ay / 100.f, f.az / 100.f };
            accel_valid_ = true;
        }
        else if (can_id == IMU_GYRO_ID) {
            ImuGyroFrame f;
            std::memcpy(&f, data, sizeof(f));
            imu_gyro_ = { f.gx / 100.f, f.gy / 100.f, f.gz / 100.f };
            if (accel_valid_) {
                orientation_.update(imu_accel_, imu_gyro_, dt_);
            }
        }
        else if (can_id == FT_FORCES_ID) {
            FtForcesFrame f;
            std::memcpy(&f, data, sizeof(f));
            wrench_.force = { f.fx / 100.f, f.fy / 100.f, f.fz / 100.f };
        }
        else if (can_id == FT_TORQUES_ID) {
            FtTorquesFrame f;
            std::memcpy(&f, data, sizeof(f));
            wrench_.torque = { f.tx / 100.f, f.ty / 100.f, f.tz / 100.f };
        }
    }

    void print_state() const {
        printf("Roll: %6.2f°  Pitch: %6.2f°\n",
               orientation_.roll  * 180.f / M_PI,
               orientation_.pitch * 180.f / M_PI);
        printf("Force:  Fx=%.2f Fy=%.2f Fz=%.2f N\n",
               wrench_.force.x, wrench_.force.y, wrench_.force.z);
        printf("Torque: Tx=%.3f Ty=%.3f Tz=%.3f Nm\n",
               wrench_.torque.x, wrench_.torque.y, wrench_.torque.z);
    }

private:
    Vec3               imu_accel_{};
    Vec3               imu_gyro_{};
    Wrench             wrench_{};
    OrientationFilter  orientation_{};
    bool               accel_valid_{false};
    float              dt_{0.001f};
    uint64_t           last_timestamp_us_{0};
};

/* ── Demo: simulate received frames ────────────────────────── */
int main() {
    SensorFusionNode node;

    /* Simulate gravity vector pointing down (Z-axis), small tilt */
    uint8_t accel_data[8] = {0};
    int16_t ax =    50, ay =  100, az = 980; /* ~1g on Z, slight tilt */
    std::memcpy(accel_data + 0, &ax, 2);
    std::memcpy(accel_data + 2, &ay, 2);
    std::memcpy(accel_data + 4, &az, 2);

    uint8_t gyro_data[8] = {0};
    int16_t gx = 5, gy = -3, gz = 0; /* 0.05 rad/s rotation */
    std::memcpy(gyro_data + 0, &gx, 2);
    std::memcpy(gyro_data + 2, &gy, 2);
    std::memcpy(gyro_data + 4, &gz, 2);

    uint8_t force_data[8] = {0};
    int16_t fz = 1500; /* 15 N in Z (gravity on 1.5kg payload) */
    std::memcpy(force_data + 4, &fz, 2);

    for (int i = 0; i < 5; i++) {
        uint64_t ts = (uint64_t)i * 2000; /* 2 ms steps */
        node.process_frame(IMU_ACCEL_ID,  accel_data, ts);
        node.process_frame(IMU_GYRO_ID,   gyro_data,  ts);
        node.process_frame(FT_FORCES_ID,  force_data, ts);
        printf("--- Step %d ---\n", i);
        node.print_state();
    }
    return 0;
}
```

---

## Code Examples: Rust

### 1. Async CAN Frame I/O with `socketcan` crate

```rust
//! robotics_can/src/main.rs
//!
//! Async CANopen-style robot joint controller using socketcan + tokio.
//!
//! [dependencies]
//! tokio        = { version = "1", features = ["full"] }
//! socketcan    = "3"
//! byteorder    = "1"

use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use socketcan::{CanFrame, CanSocket, EmbeddedFrame, Frame, Socket};
use std::io::Cursor;
use std::time::{Duration, Instant};
use tokio::time::sleep;

const CAN_IFACE: &str = "can0";
const SYNC_ID: u32 = 0x080;
const BASE_SETPOINT: u32 = 0x200; // 0x201–0x206 for joints 1–6
const BASE_FEEDBACK: u32 = 0x180; // 0x181–0x186 for joints 1–6
const NUM_JOINTS: u8 = 6;

/// Setpoint PDO: target position, max velocity, feed-forward torque
#[derive(Debug, Clone, Copy, Default)]
struct JointSetpoint {
    target_position: i32,  // encoder counts
    max_velocity: i16,      // RPM × 10
    ff_torque: i16,         // mNm
}

impl JointSetpoint {
    fn to_bytes(&self) -> [u8; 8] {
        let mut buf = [0u8; 8];
        let mut cur = std::io::Cursor::new(&mut buf[..]);
        cur.write_i32::<LittleEndian>(self.target_position).unwrap();
        cur.write_i16::<LittleEndian>(self.max_velocity).unwrap();
        cur.write_i16::<LittleEndian>(self.ff_torque).unwrap();
        buf
    }
}

/// Feedback PDO: actual position, velocity, torque
#[derive(Debug, Clone, Copy, Default)]
struct JointFeedback {
    position: i32,   // encoder counts
    velocity: i16,   // RPM × 10
    torque: i16,     // mNm
}

impl JointFeedback {
    fn from_bytes(data: &[u8]) -> Option<Self> {
        if data.len() < 8 {
            return None;
        }
        let mut cur = Cursor::new(data);
        Some(Self {
            position: cur.read_i32::<LittleEndian>().ok()?,
            velocity: cur.read_i16::<LittleEndian>().ok()?,
            torque:   cur.read_i16::<LittleEndian>().ok()?,
        })
    }
}

/// Send a SYNC frame (DLC=0, ID=0x080)
fn send_sync(sock: &CanSocket) -> std::io::Result<()> {
    let frame = CanFrame::new(
        socketcan::StandardId::new(SYNC_ID as u16).unwrap(),
        &[],
    )
    .unwrap();
    sock.write_frame(&frame)
}

/// Send a setpoint PDO to one joint
fn send_setpoint(sock: &CanSocket, joint_id: u8, sp: &JointSetpoint) -> std::io::Result<()> {
    let id = BASE_SETPOINT + joint_id as u32;
    let frame = CanFrame::new(
        socketcan::StandardId::new(id as u16).unwrap(),
        &sp.to_bytes(),
    )
    .unwrap();
    sock.write_frame(&frame)
}

/// Parse a received CAN frame as a feedback PDO.
/// Returns (joint_id, feedback) or None if not a feedback frame.
fn parse_feedback(frame: &CanFrame) -> Option<(u8, JointFeedback)> {
    let raw_id = frame.raw_id();
    if raw_id < BASE_FEEDBACK + 1 || raw_id > BASE_FEEDBACK + NUM_JOINTS as u32 {
        return None;
    }
    let joint_id = (raw_id - BASE_FEEDBACK) as u8;
    JointFeedback::from_bytes(frame.data()).map(|fb| (joint_id, fb))
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    // Open the CAN socket (non-blocking for async use)
    let sock = CanSocket::open(CAN_IFACE)?;
    sock.set_nonblocking(true)?;

    let mut feedback = [JointFeedback::default(); 7]; // index 1–6
    let mut setpoints = [JointSetpoint::default(); 7];

    println!("Robot CAN controller running on {CAN_IFACE}...");

    let cycle_time = Duration::from_millis(1);
    let mut next_cycle = Instant::now();
    let mut cycle: u32 = 0;

    loop {
        // 1. Update setpoints (trajectory planner placeholder)
        for j in 1..=NUM_JOINTS as usize {
            setpoints[j] = JointSetpoint {
                target_position: (cycle as i32 * 10) % 100_000, // ramp up
                max_velocity: 500,
                ff_torque: 0,
            };
        }

        // 2. Transmit setpoints to all joints
        for j in 1..=NUM_JOINTS {
            if let Err(e) = send_setpoint(&sock, j, &setpoints[j as usize]) {
                eprintln!("TX error joint {j}: {e}");
            }
        }

        // 3. Broadcast SYNC — drives latch setpoints simultaneously
        send_sync(&sock)?;

        // 4. Drain the receive buffer for feedback frames
        let deadline = Instant::now() + Duration::from_micros(800);
        while Instant::now() < deadline {
            match sock.read_frame() {
                Ok(frame) => {
                    if let Some((jid, fb)) = parse_feedback(&frame) {
                        feedback[jid as usize] = fb;
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e) => eprintln!("RX error: {e}"),
            }
        }

        // 5. Log every 250 cycles (~250 ms)
        if cycle % 250 == 0 {
            println!("=== Cycle {cycle} ===");
            for j in 1..=NUM_JOINTS as usize {
                println!(
                    "  Joint {j}: pos={} counts, vel={} RPM×10, torque={} mNm",
                    feedback[j].position, feedback[j].velocity, feedback[j].torque
                );
            }
        }

        cycle += 1;

        // 6. Precise cycle timing
        next_cycle += cycle_time;
        let now = Instant::now();
        if next_cycle > now {
            sleep(next_cycle - now).await;
        }
    }
}
```

---

### 2. CANopen Emergency Frame Handler and Fault Management (Rust)

```rust
//! emcy_handler.rs
//! Parses CANopen Emergency frames and implements a fault state machine.

use std::collections::HashMap;
use std::time::{Duration, Instant};

const HEARTBEAT_BASE: u32 = 0x700;
const EMCY_BASE: u32      = 0x080;

/// CANopen Emergency error codes (CiA 301)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
pub enum EmcyErrorCode {
    NoError          = 0x0000,
    GenericError     = 0x1000,
    CurrentOverload  = 0x2310,
    VoltageUnder     = 0x3210,
    TempMotorOver    = 0x4310,
    CommError        = 0x8100,
    DeviceSpecific   = 0xFF00,
    Unknown          = 0xFFFF,
}

impl EmcyErrorCode {
    fn from_u16(v: u16) -> Self {
        match v {
            0x0000 => Self::NoError,
            0x1000 => Self::GenericError,
            0x2310 => Self::CurrentOverload,
            0x3210 => Self::VoltageUnder,
            0x4310 => Self::TempMotorOver,
            0x8100 => Self::CommError,
            v if v & 0xFF00 == 0xFF00 => Self::DeviceSpecific,
            _ => Self::Unknown,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NodeState {
    Unknown,
    BootUp,
    PreOperational,
    Operational,
    Stopped,
    Faulted,
}

#[derive(Debug)]
pub struct NodeInfo {
    pub id:                 u8,
    pub state:              NodeState,
    pub last_heartbeat:     Instant,
    pub active_faults:      Vec<EmcyErrorCode>,
    pub missed_heartbeats:  u32,
}

impl NodeInfo {
    pub fn new(id: u8) -> Self {
        Self {
            id,
            state: NodeState::Unknown,
            last_heartbeat: Instant::now(),
            active_faults: Vec::new(),
            missed_heartbeats: 0,
        }
    }

    pub fn is_healthy(&self) -> bool {
        self.state == NodeState::Operational && self.active_faults.is_empty()
    }
}

/// Robot fault manager: processes CAN frames, tracks node health,
/// and triggers safety actions on faults.
pub struct FaultManager {
    nodes:           HashMap<u8, NodeInfo>,
    heartbeat_to:    Duration,
    safety_callback: Box<dyn Fn(u8, &str) + Send>,
}

impl FaultManager {
    pub fn new<F>(node_ids: &[u8], heartbeat_timeout: Duration, safety_cb: F) -> Self
    where
        F: Fn(u8, &str) + Send + 'static,
    {
        let nodes = node_ids
            .iter()
            .map(|&id| (id, NodeInfo::new(id)))
            .collect();

        Self {
            nodes,
            heartbeat_to: heartbeat_timeout,
            safety_callback: Box::new(safety_cb),
        }
    }

    /// Process one incoming CAN frame (raw ID + data bytes).
    pub fn process_frame(&mut self, can_id: u32, data: &[u8]) {
        // Heartbeat frames: 0x700 + node_id
        if can_id > HEARTBEAT_BASE && can_id <= HEARTBEAT_BASE + 127 {
            let node_id = (can_id - HEARTBEAT_BASE) as u8;
            if let Some(node) = self.nodes.get_mut(&node_id) {
                node.last_heartbeat    = Instant::now();
                node.missed_heartbeats = 0;
                if let Some(&raw) = data.first() {
                    node.state = match raw & 0x7F {
                        0x00 => NodeState::BootUp,
                        0x04 => NodeState::Stopped,
                        0x05 => NodeState::Operational,
                        0x7F => NodeState::PreOperational,
                        _    => NodeState::Unknown,
                    };
                }
            }
        }

        // Emergency frames: 0x080 + node_id
        if can_id > EMCY_BASE && can_id <= EMCY_BASE + 127 && data.len() >= 3 {
            let node_id = (can_id - EMCY_BASE) as u8;
            let error_code = u16::from_le_bytes([data[0], data[1]]);
            let error_register = data[2];
            let emcy = EmcyErrorCode::from_u16(error_code);

            println!(
                "[EMCY] Node {node_id}: {:?} (reg=0x{error_register:02X})",
                emcy
            );

            if let Some(node) = self.nodes.get_mut(&node_id) {
                if emcy == EmcyErrorCode::NoError {
                    node.active_faults.clear();
                    node.state = NodeState::Operational;
                } else {
                    node.active_faults.push(emcy);
                    node.state = NodeState::Faulted;
                    let msg = format!("{emcy:?}");
                    (self.safety_callback)(node_id, &msg);
                }
            }
        }
    }

    /// Periodic check — call every heartbeat period.
    /// Returns list of nodes that have timed out.
    pub fn check_heartbeats(&mut self) -> Vec<u8> {
        let now = Instant::now();
        let mut timed_out = Vec::new();

        for node in self.nodes.values_mut() {
            if now.duration_since(node.last_heartbeat) > self.heartbeat_to {
                node.missed_heartbeats += 1;
                if node.missed_heartbeats >= 3 {
                    node.state = NodeState::Faulted;
                    timed_out.push(node.id);
                    let msg = format!(
                        "Heartbeat lost ({} misses)", node.missed_heartbeats
                    );
                    (node.id, msg); // would call callback in full impl
                }
            }
        }
        timed_out
    }

    pub fn print_status(&self) {
        println!("── Node Status ───────────────────────────────");
        for node in self.nodes.values() {
            println!(
                "  Node {:2}: {:?}  faults={:?}  missed={}",
                node.id, node.state, node.active_faults, node.missed_heartbeats
            );
        }
    }

    pub fn all_operational(&self) -> bool {
        self.nodes.values().all(|n| n.is_healthy())
    }
}

fn main() {
    let node_ids: Vec<u8> = (1..=6).collect();

    let mut manager = FaultManager::new(
        &node_ids,
        Duration::from_millis(200),
        |node_id, reason| {
            eprintln!("🚨 SAFETY: Joint {node_id} fault: {reason} → Safe Torque Off!");
            // In production: send NMT Stop + engage hardware brakes
        },
    );

    // Simulate normal heartbeats for all joints
    for j in 1u8..=6 {
        let hb_id = HEARTBEAT_BASE + j as u32;
        manager.process_frame(hb_id, &[0x05]); // 0x05 = Operational
    }

    println!("After normal heartbeats:");
    manager.print_status();

    // Simulate an overcurrent EMCY on joint 3
    let emcy_id = EMCY_BASE + 3u32;
    let emcy_data = [0x10, 0x23, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00];
    manager.process_frame(emcy_id, &emcy_data);

    println!("\nAfter Joint 3 EMCY (overcurrent):");
    manager.print_status();

    println!("\nAll operational: {}", manager.all_operational());
}
```

---

### 3. Multi-Axis Trajectory Interpolation and CAN Dispatch (Rust)

```rust
//! trajectory.rs
//! Linear trajectory interpolation for a 6-DOF arm,
//! dispatched to drives via CAN at 1 kHz.

use std::time::{Duration, Instant};

const NUM_JOINTS: usize = 6;

/// A waypoint in joint space (encoder counts per joint)
#[derive(Clone, Copy, Debug)]
pub struct Waypoint {
    pub positions: [i32; NUM_JOINTS], // encoder counts
    pub time_s:    f64,               // time from trajectory start
}

/// Linear (trapezoidal in production) trajectory interpolator
pub struct TrajectoryInterpolator {
    waypoints: Vec<Waypoint>,
    start:     Instant,
}

impl TrajectoryInterpolator {
    pub fn new(waypoints: Vec<Waypoint>) -> Self {
        assert!(waypoints.len() >= 2, "Need at least start + end waypoint");
        Self { waypoints, start: Instant::now() }
    }

    /// Interpolate joint positions at the current time.
    /// Returns None when the trajectory is complete.
    pub fn sample(&self) -> Option<[i32; NUM_JOINTS]> {
        let t = self.start.elapsed().as_secs_f64();
        let last = self.waypoints.last().unwrap();

        if t >= last.time_s {
            return None; // trajectory complete
        }

        // Find surrounding waypoints
        let idx = self.waypoints.partition_point(|w| w.time_s <= t);
        let idx = idx.clamp(1, self.waypoints.len() - 1);
        let w0 = &self.waypoints[idx - 1];
        let w1 = &self.waypoints[idx];

        // Linear interpolation factor
        let span = w1.time_s - w0.time_s;
        let alpha = if span > 1e-9 { (t - w0.time_s) / span } else { 1.0 };

        let mut out = [0i32; NUM_JOINTS];
        for j in 0..NUM_JOINTS {
            let p0 = w0.positions[j] as f64;
            let p1 = w1.positions[j] as f64;
            out[j] = (p0 + alpha * (p1 - p0)).round() as i32;
        }
        Some(out)
    }
}

/// Simulated CAN transmit — replace with real socketcan write in production
fn can_send_setpoint(joint_id: u8, position: i32, _velocity: i16) {
    // In production:
    //   let id = 0x200 + joint_id as u32;
    //   let frame = CanFrame::new(id, &encode_setpoint(position, velocity));
    //   socket.write_frame(&frame);
    let _ = (joint_id, position); // suppress unused warnings
}

fn can_send_sync() {
    // socket.write_frame(&sync_frame());
}

#[tokio::main]
async fn main() {
    // Define a simple 3-waypoint trajectory (e.g., pick and place)
    let waypoints = vec![
        Waypoint { positions: [0; NUM_JOINTS],                               time_s: 0.0 },
        Waypoint { positions: [10000, 8000, -5000, 3000, -2000, 1000],       time_s: 2.0 },
        Waypoint { positions: [20000, 15000, -10000, 6000, -4000, 2000],     time_s: 4.0 },
    ];

    let traj = TrajectoryInterpolator::new(waypoints);
    let cycle = Duration::from_millis(1);
    let mut next = Instant::now();
    let mut cycles: u64 = 0;

    println!("Starting trajectory execution (4.0 s, 1 kHz)...");

    loop {
        match traj.sample() {
            None => {
                println!("Trajectory complete after {cycles} cycles.");
                break;
            }
            Some(positions) => {
                // Send setpoints to all joints
                for (j, &pos) in positions.iter().enumerate() {
                    can_send_setpoint((j + 1) as u8, pos, 500);
                }
                // SYNC pulse — drives execute simultaneously
                can_send_sync();

                if cycles % 500 == 0 {
                    println!("[{:.1}s] Joints: {:?}", cycles as f64 * 0.001, positions);
                }
            }
        }

        cycles += 1;
        next += cycle;
        let now = Instant::now();
        if next > now {
            tokio::time::sleep_until(tokio::time::Instant::from_std(next)).await;
        }
    }
}
```

---

## Summary

CAN bus occupies a central role in modern robotics, providing the combination of **determinism**, **noise immunity**, **multi-master arbitration**, and **mature tooling** that robotic systems demand. The key technical themes covered in this document are:

**Real-Time Motor Control** — The CAN SYNC/PDO mechanism of CANopen CiA 402 enables isochronous, sub-millisecond closed-loop control of multiple servo drives. The SYNC object (ID 0x080) broadcasts a timing pulse that causes all connected drives to latch new setpoints simultaneously, making coordinated multi-axis motion possible without per-axis jitter.

**Sensor Fusion** — IMUs, force/torque sensors, absolute encoders, and temperature monitors are common CAN nodes in robotic systems. Each node publishes structured PDOs at rates from 10 Hz to 1 kHz. On the host, receive-side timestamping (SocketCAN `SO_TIMESTAMP`) or embedded payload timestamps allow data from multiple sensors to be aligned in time for state estimation filters (Kalman, complementary).

**Multi-Axis Coordination** — Careful message ID assignment (lower ID = higher bus priority) and bus load analysis are essential. Classic CAN at 1 Mbit/s supports approximately 6–8 axes at 1 kHz before bus saturation. CAN FD, with its higher data-phase bit rate and 64-byte payload, extends this to 12+ axes or allows richer per-frame payloads (e.g., full 6-DOF wrench in one frame).

**Fault Tolerance** — CANopen's Emergency (EMCY) object, heartbeat/node-guarding protocol, and the CAN hardware's autonomous Bus-Off mechanism form a layered safety architecture. Robust implementations monitor heartbeat timeouts, parse EMCY codes, and trigger Safe Torque Off (STO) or controlled stops within one or two bus cycles of a detected fault.

**Practical Implementation** — Linux SocketCAN provides a production-ready, zero-cost abstraction for CAN in C/C++ and Rust. The `socketcan` Rust crate, combined with `tokio` for async I/O, enables high-level, ergonomic robot controllers with the same real-time performance as C code. Careful use of non-blocking I/O, hardware timestamps, and precise cycle timing (CLOCK_MONOTONIC) are prerequisites for 1 kHz loop closure.

As robotics continues to evolve toward higher DOF counts, compliant control, and human collaboration, CAN FD and higher-layer protocols (micro-ROS over CAN, CiA 402 with Safety over CAN) are extending the classic CAN ecosystem to meet these demands — making CAN expertise a durable and valuable skill in robotics engineering.

---

*Document: 76 — CAN in Robotics | Series: Controller Area Network Programming Guide*