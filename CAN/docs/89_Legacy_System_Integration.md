# 89. Legacy System Integration

- **Core Concepts** — CAN generations table (2.0A through CAN XL), higher-layer protocols (J1939, CANopen, DeviceNet), and why legacy nodes can't simply be swapped out.
- **Architecture Patterns** — Four gateway topologies: transparent relay, protocol translation, router/multiplexer, and HIL simulation/emulation.
- **Protocol Translators** — How the three-stage decode→parse→re-encode pipeline works, with a signal mapping table example.

**C/C++ examples (4):**
1. **Basic Frame Relay** — Linux SocketCAN transparent bridge with 11-bit → 29-bit ID promotion
2. **J1939 EEC1 Translator** — Full PGN parser extracting RPM and torque, re-encoded to CAN FD
3. **CANopen SDO Proxy** — Translates modern diagnostic read requests into SDO expedited transfers on a legacy CANopen segment
4. **Mapping Table–Driven Router** — Trait-based/function-pointer routing table with a scaling transform for coolant temperature

**Rust examples (3):**
1. **Async Tokio Bridge** — `Arc<CANSocket>` passed into `spawn_blocking` for non-blocking relay
2. **J1939 Signal Translator** — Clean idiomatic Rust with `Option`-based error handling
3. **Trait-Based Router** — `HashMap<u32, Box<dyn FrameTransform>>` for extensible, type-safe routing

**Timing & Error Handling** sections cover latency budgeting, hardware timestamp preservation (`SO_TIMESTAMPING`), error propagation strategies, and a watchdog heartbeat pattern.

> **Bridging older CAN implementations with modern systems using protocol translators and adapters.**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Architecture Patterns](#architecture-patterns)
4. [Protocol Translators](#protocol-translators)
5. [Gateway / Adapter Design](#gateway--adapter-design)
6. [C/C++ Implementation Examples](#cc-implementation-examples)
7. [Rust Implementation Examples](#rust-implementation-examples)
8. [Timing & Synchronization Considerations](#timing--synchronization-considerations)
9. [Error Handling & Fault Tolerance](#error-handling--fault-tolerance)
10. [Summary](#summary)

---

## Introduction

Controller Area Network (CAN) was introduced by Bosch in 1986 and has since become one of the most pervasive embedded communication protocols in automotive, industrial, medical, and aerospace systems. Its longevity means that many active production environments still run on early CAN 2.0A/2.0B implementations, legacy proprietary higher-layer protocols (J1939, CANopen 3.x, DeviceNet), and obsolete baud rates — all of which must coexist with modern CAN FD networks, Ethernet-based backbones (DoIP, SOME/IP), and cloud-connected edge gateways.

**Legacy System Integration** addresses the strategies, hardware choices, and software patterns used to bridge these worlds without replacing the older nodes, which is often cost-prohibitive or operationally impossible.

---

## Core Concepts

### CAN Generations

| Generation | Standard | Max Payload | Max Bitrate | Typical Use |
|---|---|---|---|---|
| Classic CAN 2.0A | ISO 11898-1 (1993) | 8 bytes | 1 Mbit/s | Early automotive ECUs |
| Classic CAN 2.0B | ISO 11898-1 (1995) | 8 bytes | 1 Mbit/s | Industrial, trucks |
| CAN FD | ISO 11898-1 (2015) | 64 bytes | 8 Mbit/s (data) | Modern automotive |
| CAN XL | ISO 11898-1 (2022) | 2048 bytes | 10 Mbit/s | ADAS, zonal E/E |

### Higher-Layer Protocols Commonly Found in Legacy Systems

- **J1939** — SAE standard for heavy-duty vehicles; uses 29-bit extended IDs, PGN-based addressing, multi-packet transport (BAM / CMDT).
- **CANopen** — CiA 301; object dictionary, SDO/PDO, NMT state machine.
- **DeviceNet** — Allen-Bradley industrial I/O over CAN; uses connection-based model.
- **NMEA 2000** — Marine instrumentation; J1939-derived.
- **Proprietary OEM protocols** — Many vehicle manufacturers defined custom message sets in the 1990s–2000s that are still active in service tools and test rigs.

### Why Legacy Nodes Cannot Simply Be Upgraded

- Safety-certified firmware that cannot be reflashed without re-certification (ISO 26262, IEC 61508).
- Physical connectors and wiring harnesses sealed in production vehicles.
- Intellectual property — source code no longer available.
- Cost: thousands of deployed units with negligible per-unit upgrade ROI.

---

## Architecture Patterns

### 1. Transparent Gateway (1:1 Relay)

Forwards every message from one bus to another with minimal transformation. Used when the target system can interpret legacy message formats but operates at a different bitrate or physical layer.

```
Legacy CAN 2.0B (500 kbit/s)  ──►  [Gateway]  ──►  CAN FD (2 Mbit/s nominal)
```

### 2. Protocol Translation Gateway

Performs semantic transformation — unpacks a J1939 PGN and re-publishes the data as a SOME/IP service or MQTT topic.

```
J1939 Bus  ──►  [Translator]  ──►  Ethernet / SOME/IP
                    │
                    └──►  Diagnostic UDS (ISO 14229)
```

### 3. Router / Multiplexer

Routes messages based on ID ranges between multiple legacy buses and one modern backbone. Common in multi-ECU test benches.

```
Bus A (CANopen)  ─┐
Bus B (J1939)    ─┤──►  [Router]  ──►  CAN FD Backbone
Bus C (OEM)      ─┘
```

### 4. Simulation / Emulation Layer

A software node that impersonates a legacy ECU on a new bus, translating queries from modern diagnostic tools into legacy bus traffic. Used heavily in HIL (Hardware-in-the-Loop) test environments.

---

## Protocol Translators

A protocol translator sits between two bus segments and performs three operations:

1. **Frame reception** — decode the physical/data-link layer of the source bus.
2. **Semantic parsing** — interpret the message according to the source higher-layer protocol.
3. **Re-encoding** — construct a valid frame for the destination protocol.

### Message Mapping Table

The translator is driven by a static or dynamically loaded mapping table:

| Source Protocol | Source ID / PGN | Signal | Destination Protocol | Dest ID | Scaling |
|---|---|---|---|---|---|
| J1939 | PGN 0xF004 (EEC1) | Engine RPM | CAN FD | 0x300 | ×0.125 rpm/bit |
| CANopen | COB-ID 0x181 | Actual velocity | SOME/IP | Service 0x44 | ×0.01 rpm |
| OEM legacy | 0x4B0 | Coolant temp | MQTT | `engine/coolant` | offset −40 °C |

---

## Gateway / Adapter Design

### Key Hardware Choices

- **Dual-channel CAN controller** (e.g., MCP2517FD, SJA1000 + TJA1050): one channel per bus segment.
- **CAN-to-CAN FD transparent bridge** chips (NXP TJA1153, Microchip ATA6560).
- **Embedded MCU with two independent CAN peripherals** (STM32G0B1, S32K3xx, Renesas RH850).
- **Industrial gateway modules** (Kvaser Memorator, Peak PCAN-Router Pro FD).

### Software Stack Layers

```
┌─────────────────────────────────────────────────────┐
│          Application Layer (Mapping Engine)         │
├─────────────────────────────────────────────────────┤
│   Higher-Layer Protocol Parsers (J1939 / CANopen)   │
├──────────────────┬──────────────────────────────────┤
│  CAN 2.0 Driver  │       CAN FD Driver              │
├──────────────────┴──────────────────────────────────┤
│          Hardware Abstraction Layer (HAL)           │
└─────────────────────────────────────────────────────┘
```

---

## C/C++ Implementation Examples

### 1. Basic CAN Frame Relay (Linux SocketCAN)

This example opens two SocketCAN interfaces and relays every frame from the legacy bus (`can0`) to the modern bus (`can1`), optionally transforming the frame header for CAN FD.

```c
/* legacy_relay.c
 * Compile: gcc -o legacy_relay legacy_relay.c
 * Run:     ./legacy_relay can0 can1
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

static int open_can_socket(const char *ifname) {
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }
    return sock;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <legacy_if> <modern_if>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int src_sock = open_can_socket(argv[1]);   /* e.g. can0 — legacy bus  */
    int dst_sock = open_can_socket(argv[2]);   /* e.g. can1 — modern bus  */

    struct can_frame frame;
    printf("Relaying %s -> %s\n", argv[1], argv[2]);

    while (1) {
        ssize_t nbytes = read(src_sock, &frame, sizeof(frame));
        if (nbytes < (ssize_t)sizeof(struct can_frame)) continue;

        /* Optional: map 11-bit legacy ID to 29-bit extended ID */
        if (!(frame.can_id & CAN_EFF_FLAG)) {
            frame.can_id = (frame.can_id | 0x18000000UL) | CAN_EFF_FLAG;
        }

        if (write(dst_sock, &frame, sizeof(frame)) < 0) {
            perror("write");
        }
    }
    close(src_sock);
    close(dst_sock);
    return EXIT_SUCCESS;
}
```

---

### 2. J1939 PGN Parser and Re-Encoder (C++)

This example decodes Engine Electronic Control 1 (EEC1, PGN 0xF004) from a legacy J1939 bus and re-encodes the extracted signals into a compact CAN FD frame for a modern backbone.

```cpp
// j1939_to_canfd_translator.cpp
// Compile: g++ -std=c++17 -o translator j1939_to_canfd_translator.cpp

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <array>
#include <optional>

// ─── J1939 Frame Representation ───────────────────────────────────────────────

struct J1939Frame {
    uint32_t pgn;         // 18-bit PGN extracted from 29-bit CAN ID
    uint8_t  src_addr;    // Source address (bits 7–0 of CAN ID)
    uint8_t  dst_addr;    // Destination address (PDU1 only)
    uint8_t  priority;    // Bits 28–26
    uint8_t  data[8];
    uint8_t  dlc;
};

// ─── CAN FD Frame Representation ──────────────────────────────────────────────

struct CanFdFrame {
    uint32_t can_id;
    uint8_t  data[64];
    uint8_t  dlc;         // CAN FD DLC (0–15, maps to 0–64 bytes per ISO 11898)
};

// ─── J1939 EEC1 (PGN 0xF004) Signal Extraction ────────────────────────────────

struct EEC1Signals {
    float    engine_torque_mode;     // byte 0, bits 3–0
    float    actual_engine_torque;   // byte 2, 1 %/bit, -125% offset
    float    engine_rpm;             // bytes 3–4, 0.125 rpm/bit
    uint8_t  src_address_override;   // byte 5
};

static std::optional<EEC1Signals> parse_eec1(const J1939Frame &f) {
    if (f.pgn != 0xF004U || f.dlc < 8) return std::nullopt;

    EEC1Signals s{};
    s.engine_torque_mode    = static_cast<float>(f.data[0] & 0x0FU);
    s.actual_engine_torque  = static_cast<float>(static_cast<int8_t>(f.data[2])) * 1.0f - 125.0f;
    s.engine_rpm            = static_cast<float>(
                                  (static_cast<uint16_t>(f.data[4]) << 8) | f.data[3]
                              ) * 0.125f;
    s.src_address_override  = f.data[5];
    return s;
}

// ─── Re-encode into CAN FD Frame ──────────────────────────────────────────────

static CanFdFrame encode_to_canfd(const EEC1Signals &s, uint32_t base_id) {
    CanFdFrame out{};
    out.can_id = base_id | 0x80000004U;  // EFF + custom base_id

    // Pack engine RPM as uint16 (resolution 0.125, stored as raw counts)
    uint16_t rpm_raw = static_cast<uint16_t>(s.engine_rpm / 0.125f);
    out.data[0] = static_cast<uint8_t>(rpm_raw & 0xFF);
    out.data[1] = static_cast<uint8_t>(rpm_raw >> 8);

    // Pack torque as int8 with +125 offset
    out.data[2] = static_cast<uint8_t>(static_cast<int8_t>(s.actual_engine_torque + 125.0f));

    // Pack torque mode in nibble
    out.data[3] = static_cast<uint8_t>(s.engine_torque_mode) & 0x0FU;

    out.dlc = 4;
    return out;
}

// ─── Translation Entry Point ──────────────────────────────────────────────────

void translate_j1939_to_canfd(const J1939Frame &legacy, CanFdFrame &out_frame) {
    auto signals = parse_eec1(legacy);
    if (!signals) {
        printf("[WARN] Unsupported PGN 0x%05X — dropping frame.\n", legacy.pgn);
        return;
    }

    printf("[EEC1] RPM=%.1f  Torque=%.1f%%  Mode=%.0f\n",
           signals->engine_rpm,
           signals->actual_engine_torque,
           signals->engine_torque_mode);

    out_frame = encode_to_canfd(*signals, 0x300);
}

// ─── Demo ─────────────────────────────────────────────────────────────────────

int main() {
    // Simulate a received J1939 EEC1 frame
    J1939Frame legacy{};
    legacy.pgn      = 0xF004;
    legacy.src_addr = 0x00;   // Engine ECU
    legacy.priority = 3;
    legacy.dlc      = 8;
    // Engine speed = 2500 rpm → raw = 2500/0.125 = 20000 = 0x4E20
    legacy.data[3] = 0x20;   // low byte
    legacy.data[4] = 0x4E;   // high byte
    // Torque = 50% → stored as 50 + 125 = 175 = 0xAF
    legacy.data[2] = 0xAF;
    legacy.data[0] = 0x03;   // torque mode = 3

    CanFdFrame modern{};
    translate_j1939_to_canfd(legacy, modern);

    printf("[CAN FD] ID=0x%08X  DLC=%d  Data:", modern.can_id, modern.dlc);
    for (int i = 0; i < modern.dlc; ++i)
        printf(" %02X", modern.data[i]);
    printf("\n");

    return 0;
}
```

---

### 3. CANopen SDO Proxy (C)

Legacy CANopen nodes (CiA 301) use SDO (Service Data Object) for configuration. This proxy accepts modern diagnostic requests on one bus and issues SDO expedited transfers on the legacy CANopen segment.

```c
/* canopen_sdo_proxy.c
 * Translates modern read requests (custom frame 0x7F0) into
 * CANopen SDO upload requests on the legacy bus, then returns
 * the response to the requester.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define SDO_TX_BASE   0x600U   /* Client → Server (node_id + 0x600) */
#define SDO_RX_BASE   0x580U   /* Server → Client (node_id + 0x580) */

#define SDO_CMD_UPLOAD_REQ   0x40U
#define SDO_CMD_UPLOAD_RESP  0x40U   /* response ccs field checked separately */

typedef struct {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
} CanFrame;

/* Build a CANopen SDO upload request frame */
static CanFrame build_sdo_upload_req(uint8_t node_id,
                                     uint16_t index,
                                     uint8_t  subindex) {
    CanFrame f = {0};
    f.id     = SDO_TX_BASE + node_id;
    f.dlc    = 8;
    f.data[0] = SDO_CMD_UPLOAD_REQ;            /* initiate upload request */
    f.data[1] = (uint8_t)(index & 0xFFU);      /* index low byte  */
    f.data[2] = (uint8_t)(index >> 8);         /* index high byte */
    f.data[3] = subindex;
    /* bytes 4–7 reserved, set to 0 */
    return f;
}

/* Parse an SDO upload response (expedited, 4 bytes of object data) */
static bool parse_sdo_upload_resp(const CanFrame *resp,
                                  uint8_t node_id,
                                  uint16_t index,
                                  uint8_t subindex,
                                  uint32_t *value_out) {
    if (resp->id != (SDO_RX_BASE + node_id)) return false;
    if (resp->dlc < 8)                        return false;

    /* Check: response index/subindex must match request */
    uint16_t resp_index = (uint16_t)resp->data[1] | ((uint16_t)resp->data[2] << 8);
    if (resp_index != index || resp->data[3] != subindex) return false;

    /* ccs bits [7:5]: 010 = initiate upload response */
    if ((resp->data[0] >> 5) != 2U) return false;

    /* e=1 expedited, s=1 size indicated: extract byte count from bits [3:2] */
    bool expedited  = (resp->data[0] >> 1) & 1U;
    bool size_ind   = (resp->data[0])      & 1U;
    if (!expedited || !size_ind) return false;

    uint8_t n = (resp->data[0] >> 2) & 3U;   /* n = (4 - data_bytes) */
    uint8_t bytes = 4U - n;

    *value_out = 0;
    for (uint8_t i = 0; i < bytes; ++i)
        *value_out |= ((uint32_t)resp->data[4 + i]) << (8U * i);

    return true;
}

/* ─── Simulated bus send / receive stubs ──────────────────────────────────── */

static void legacy_bus_send(const CanFrame *f) {
    printf("[LEGACY TX] ID=0x%03X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
           f->id,
           f->data[0], f->data[1], f->data[2], f->data[3],
           f->data[4], f->data[5], f->data[6], f->data[7]);
}

static CanFrame legacy_bus_receive_sdo_resp(uint8_t node_id,
                                            uint16_t index, uint8_t sub) {
    /* In production: block on socket read with timeout */
    /* Here we fabricate a valid expedited response: value = 0x00001388 (5000) */
    CanFrame r = {0};
    r.id     = SDO_RX_BASE + node_id;
    r.dlc    = 8;
    r.data[0] = 0x43;       /* 0b0100_0011: ccs=2, e=1, s=1, n=0 (4 bytes) */
    r.data[1] = (uint8_t)(index & 0xFF);
    r.data[2] = (uint8_t)(index >> 8);
    r.data[3] = sub;
    r.data[4] = 0x88; r.data[5] = 0x13; /* 5000 = 0x1388 little-endian */
    return r;
}

/* ─── Proxy entry point ───────────────────────────────────────────────────── */

uint32_t sdo_proxy_read(uint8_t node_id, uint16_t index, uint8_t subindex) {
    CanFrame req  = build_sdo_upload_req(node_id, index, subindex);
    legacy_bus_send(&req);

    CanFrame resp = legacy_bus_receive_sdo_resp(node_id, index, subindex);

    uint32_t value = 0;
    if (parse_sdo_upload_resp(&resp, node_id, index, subindex, &value)) {
        printf("[PROXY] Node 0x%02X  Obj 0x%04X:%d  =  %u (0x%08X)\n",
               node_id, index, subindex, value, value);
    } else {
        printf("[PROXY] SDO read failed for node 0x%02X\n", node_id);
    }
    return value;
}

int main(void) {
    /* Read object 0x6064 sub0 (Position Actual Value) from node 2 */
    sdo_proxy_read(0x02, 0x6064, 0x00);
    return 0;
}
```

---

### 4. Mapping Table–Driven Router (C++)

A flexible router that uses a compile-time-initialised routing table to map legacy CAN IDs to their modern equivalents with signal scaling.

```cpp
// can_router.cpp  —  Mapping table–driven ID translator
// Compile: g++ -std=c++17 -o router can_router.cpp

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
#include <optional>

struct CanFrame {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
};

// ─── Route Entry ──────────────────────────────────────────────────────────────

struct RouteEntry {
    uint32_t src_id;       // Legacy CAN ID to match
    uint32_t dst_id;       // Target CAN ID on modern bus

    /* Optional transform: receives src frame, writes dst frame.
     * Return false to drop the frame. */
    std::function<bool(const CanFrame &src, CanFrame &dst)> transform;
};

// ─── Identity transform ───────────────────────────────────────────────────────

static bool identity_transform(const CanFrame &src, CanFrame &dst) {
    dst = src;
    return true;
}

// ─── Example: legacy coolant temp (raw, offset=−40) → modern (Celsius × 10) ──

static bool coolant_temp_transform(const CanFrame &src, CanFrame &dst) {
    dst.id  = 0x3B0;
    dst.dlc = 2;
    int16_t celsius_x10 = static_cast<int16_t>((src.data[0] - 40) * 10);
    dst.data[0] = static_cast<uint8_t>(celsius_x10 & 0xFF);
    dst.data[1] = static_cast<uint8_t>((celsius_x10 >> 8) & 0xFF);
    return true;
}

// ─── Router ───────────────────────────────────────────────────────────────────

class CanRouter {
public:
    void add_route(RouteEntry entry) {
        routes_.push_back(std::move(entry));
    }

    /* Returns translated frame if a route was found, nullopt if dropped/unknown */
    std::optional<CanFrame> route(const CanFrame &src) const {
        for (const auto &r : routes_) {
            if (r.src_id != src.id) continue;

            CanFrame dst{};
            dst.id  = r.dst_id;
            dst.dlc = src.dlc;
            memcpy(dst.data, src.data, src.dlc);

            if (r.transform && !r.transform(src, dst)) return std::nullopt;
            return dst;
        }
        return std::nullopt;  // no route — drop
    }

private:
    std::vector<RouteEntry> routes_;
};

// ─── Demo ─────────────────────────────────────────────────────────────────────

int main() {
    CanRouter router;

    // Transparent relay: legacy 0x100 → modern 0x200
    router.add_route({ 0x100, 0x200, identity_transform });

    // Semantic translation: legacy coolant temp
    router.add_route({ 0x4B0, 0x3B0, coolant_temp_transform });

    // Test frame 1: transparent
    CanFrame f1{ 0x100, {0x01,0x02,0x03,0x04,0,0,0,0}, 4 };
    auto r1 = router.route(f1);
    if (r1) printf("Routed 0x%03X → 0x%03X  DLC=%d\n", f1.id, r1->id, r1->dlc);

    // Test frame 2: coolant temp, raw byte = 105 → 65 °C
    CanFrame f2{ 0x4B0, {105,0,0,0,0,0,0,0}, 1 };
    auto r2 = router.route(f2);
    if (r2) {
        int16_t celsius_x10 = static_cast<int16_t>(r2->data[0] | (r2->data[1] << 8));
        printf("Coolant: 0x%03X → 0x%03X  %.1f °C\n",
               f2.id, r2->id, celsius_x10 / 10.0f);
    }

    return 0;
}
```

---

## Rust Implementation Examples

### 1. Async CAN Bridge with Tokio

This example uses the `socketcan` crate and Tokio to build an async bridge between a legacy CAN interface and a modern CAN FD interface, handling message fan-out efficiently.

```toml
# Cargo.toml
[dependencies]
tokio       = { version = "1", features = ["full"] }
socketcan   = "3"
```

```rust
// src/main.rs — Async CAN-to-CAN bridge
use socketcan::{CANSocket, CANFrame, CANFilter};
use std::sync::Arc;
use tokio::task;

/// Open a SocketCAN interface by name.
fn open_can(iface: &str) -> Arc<CANSocket> {
    let sock = CANSocket::open(iface)
        .unwrap_or_else(|e| panic!("Cannot open {iface}: {e}"));
    sock.set_nonblocking(true).expect("set_nonblocking");
    Arc::new(sock)
}

#[tokio::main]
async fn main() {
    let legacy_sock = open_can("can0");   // Legacy CAN 2.0B bus
    let modern_sock = open_can("can1");   // Modern CAN FD bus

    // Clone Arc handles for the spawned task
    let src  = Arc::clone(&legacy_sock);
    let dst  = Arc::clone(&modern_sock);

    let bridge_task = task::spawn_blocking(move || {
        println!("Bridge started: can0 → can1");
        loop {
            match src.read_frame() {
                Ok(frame) => {
                    // Re-map 11-bit SID to 29-bit EID with a prefix
                    let new_id = if frame.id() & 0x8000_0000 == 0 {
                        // Standard frame: promote to extended
                        (frame.id() | 0x1800_0000) | 0x8000_0000
                    } else {
                        frame.id()
                    };

                    let out_frame = CANFrame::new(
                        new_id,
                        frame.data(),
                        frame.is_remote_frame(),
                        frame.is_error_frame(),
                    )
                    .expect("build frame");

                    if let Err(e) = dst.write_frame(&out_frame) {
                        eprintln!("Write error: {e}");
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                    std::thread::sleep(std::time::Duration::from_micros(100));
                }
                Err(e) => eprintln!("Read error: {e}"),
            }
        }
    });

    bridge_task.await.expect("bridge task panicked");
}
```

---

### 2. J1939 Signal Translator in Rust

A clean, idiomatic Rust translator that decodes J1939 EEC1 and re-encodes it as a compact modern frame.

```rust
// src/j1939_translator.rs

/// Raw J1939 29-bit CAN frame
#[derive(Debug, Clone)]
pub struct J1939Frame {
    pub can_id: u32,   // full 29-bit CAN ID
    pub data:   [u8; 8],
    pub dlc:    u8,
}

/// Parsed EEC1 (PGN 0xF004) signals
#[derive(Debug)]
pub struct Eec1Signals {
    pub engine_rpm:            f32,   // rpm
    pub actual_engine_torque:  f32,   // %
    pub torque_mode:           u8,
}

/// Modern compact frame for backbone bus
#[derive(Debug)]
pub struct ModernFrame {
    pub can_id: u32,
    pub data:   Vec<u8>,
}

impl J1939Frame {
    /// Extract 18-bit PGN from a J1939 29-bit CAN ID
    pub fn pgn(&self) -> u32 {
        // For PDU2 (PS >= 0xF0): PGN = DP | PF | PS
        let pf = (self.can_id >> 16) & 0xFF;
        let ps = (self.can_id >> 8)  & 0xFF;
        let dp = (self.can_id >> 24) & 0x01;
        if pf >= 0xF0 {
            (dp << 17) | (pf << 8) | ps
        } else {
            (dp << 17) | (pf << 8)
        }
    }

    pub fn source_address(&self) -> u8 {
        (self.can_id & 0xFF) as u8
    }
}

/// Decode EEC1 from a J1939 frame.
pub fn decode_eec1(frame: &J1939Frame) -> Option<Eec1Signals> {
    if frame.pgn() != 0xF004 || frame.dlc < 8 {
        return None;
    }
    let d = &frame.data;

    let rpm_raw   = u16::from_le_bytes([d[3], d[4]]);
    let rpm       = rpm_raw as f32 * 0.125;

    let torque_raw = d[2] as i8;
    let torque     = torque_raw as f32 - 125.0;

    let mode = d[0] & 0x0F;

    Some(Eec1Signals {
        engine_rpm:           rpm,
        actual_engine_torque: torque,
        torque_mode:          mode,
    })
}

/// Encode EEC1 signals into a compact modern CAN frame.
pub fn encode_modern(signals: &Eec1Signals, base_id: u32) -> ModernFrame {
    let rpm_raw = (signals.engine_rpm / 0.125) as u16;
    let torq_raw = (signals.actual_engine_torque + 125.0).clamp(0.0, 255.0) as u8;

    ModernFrame {
        can_id: base_id,
        data:   vec![
            (rpm_raw & 0xFF) as u8,    // RPM low
            (rpm_raw >> 8) as u8,      // RPM high
            torq_raw,                  // Torque
            signals.torque_mode,       // Mode nibble
        ],
    }
}

// ─── Demo ─────────────────────────────────────────────────────────────────────

fn main() {
    // Simulate received J1939 EEC1 frame
    // RPM = 2500 → raw = 20000 = 0x4E20 (LE: 0x20, 0x4E)
    // Torque = 50% → stored 50+125=175=0xAF
    let legacy = J1939Frame {
        can_id: 0x0CF00400,   // Priority=3, PGN=0xF004, SA=0x00
        data:   [0x03, 0xFF, 0xAF, 0x20, 0x4E, 0xFF, 0xFF, 0xFF],
        dlc:    8,
    };

    println!("PGN:    0x{:05X}", legacy.pgn());
    println!("SrcAddr: 0x{:02X}", legacy.source_address());

    match decode_eec1(&legacy) {
        Some(sig) => {
            println!("Decoded EEC1:");
            println!("  RPM:    {:.1}", sig.engine_rpm);
            println!("  Torque: {:.1}%", sig.actual_engine_torque);
            println!("  Mode:   {}", sig.torque_mode);

            let modern = encode_modern(&sig, 0x8000_0300);
            println!("Modern frame  ID=0x{:08X}  Data={:02X?}",
                     modern.can_id, modern.data);
        }
        None => eprintln!("Decode failed"),
    }
}
```

---

### 3. Routing Table in Rust with Trait-based Transforms

```rust
// src/router.rs — Trait-based routing table

use std::collections::HashMap;

pub struct CanFrame {
    pub id:   u32,
    pub data: [u8; 8],
    pub dlc:  u8,
}

/// A transform maps one frame to another (or drops it by returning None).
pub trait FrameTransform: Send + Sync {
    fn transform(&self, src: &CanFrame) -> Option<CanFrame>;
}

// ─── Identity Transform ───────────────────────────────────────────────────────

pub struct IdentityTransform {
    pub dst_id: u32,
}

impl FrameTransform for IdentityTransform {
    fn transform(&self, src: &CanFrame) -> Option<CanFrame> {
        Some(CanFrame { id: self.dst_id, ..*src })
    }
}

// ─── Scaling Transform ────────────────────────────────────────────────────────

/// Reads a single byte from src, applies linear scaling (y = mx + b × 10),
/// and writes the result as a little-endian i16 into dst.
pub struct ScaleByteTransform {
    pub dst_id:    u32,
    pub src_byte:  usize,
    pub scale:     f32,    // multiplier
    pub offset:    f32,    // additive offset
}

impl FrameTransform for ScaleByteTransform {
    fn transform(&self, src: &CanFrame) -> Option<CanFrame> {
        if self.src_byte >= src.dlc as usize { return None; }

        let raw   = src.data[self.src_byte] as f32;
        let value = (raw * self.scale + self.offset) as i16;
        let bytes = value.to_le_bytes();

        let mut dst = CanFrame { id: self.dst_id, data: [0; 8], dlc: 2 };
        dst.data[0] = bytes[0];
        dst.data[1] = bytes[1];
        Some(dst)
    }
}

// ─── Router ───────────────────────────────────────────────────────────────────

pub struct CanRouter {
    routes: HashMap<u32, Box<dyn FrameTransform>>,
}

impl CanRouter {
    pub fn new() -> Self {
        Self { routes: HashMap::new() }
    }

    pub fn add_route(&mut self, src_id: u32, transform: Box<dyn FrameTransform>) {
        self.routes.insert(src_id, transform);
    }

    pub fn route(&self, frame: &CanFrame) -> Option<CanFrame> {
        self.routes.get(&frame.id)?.transform(frame)
    }
}

// ─── Demo ─────────────────────────────────────────────────────────────────────

fn main() {
    let mut router = CanRouter::new();

    // Transparent relay: legacy 0x100 → modern 0x200
    router.add_route(0x100, Box::new(IdentityTransform { dst_id: 0x200 }));

    // Coolant temperature: raw byte (offset −40 °C) → Celsius × 10 as i16
    router.add_route(0x4B0, Box::new(ScaleByteTransform {
        dst_id:   0x3B0,
        src_byte: 0,
        scale:    10.0,
        offset:   -400.0,   // (raw - 40) * 10  ⟹  raw*10 - 400
    }));

    // Test 1: transparent relay
    let f1 = CanFrame { id: 0x100, data: [0xDE, 0xAD, 0, 0, 0, 0, 0, 0], dlc: 2 };
    if let Some(out) = router.route(&f1) {
        println!("Relay: 0x{:03X} → 0x{:03X}  data={:02X?}", f1.id, out.id, &out.data[..out.dlc as usize]);
    }

    // Test 2: coolant temp, raw=105 → (105−40)×10 = 650 → 0x028A LE
    let f2 = CanFrame { id: 0x4B0, data: [105, 0, 0, 0, 0, 0, 0, 0], dlc: 1 };
    if let Some(out) = router.route(&f2) {
        let celsius_x10 = i16::from_le_bytes([out.data[0], out.data[1]]);
        println!("Coolant: 0x{:03X} → 0x{:03X}  {:.1} °C",
                 f2.id, out.id, celsius_x10 as f32 / 10.0);
    }
}
```

---

## Timing & Synchronization Considerations

### Latency Budget

A gateway introduces forwarding latency. Typical breakdown:

```
Legacy bus frame arrival        : T₀
Driver interrupt / DMA          : +5–20 µs
Protocol parsing                : +1–5 µs
Signal scaling / remapping      : +1–3 µs
Destination frame construction  : +1–2 µs
Tx scheduling & bus arbitration : +5–50 µs (bus-dependent)
                                 ─────────────────────────
Total gateway latency           : ~13–80 µs per frame
```

For safety-critical signals (e.g., ABS torque demand), this latency must be accounted for in the system timing analysis and may require hardware-accelerated forwarding or FPGA-based bridges.

### Timestamp Preservation

When logging or forwarding frames that carry implicit timing (e.g., periodic heartbeats), the gateway should tag each frame with a hardware timestamp from the receiving interface so that the downstream system can detect missed cycles:

```c
/* Attach hardware timestamp to a forwarded frame (Linux SO_TIMESTAMPING) */
struct msghdr  msg  = {0};
struct iovec   iov  = { .iov_base = &frame, .iov_len = sizeof(frame) };
char           ctrl[CMSG_SPACE(sizeof(struct scm_timestamping))];
msg.msg_iov        = &iov;
msg.msg_iovlen     = 1;
msg.msg_control    = ctrl;
msg.msg_controllen = sizeof(ctrl);
recvmsg(sock, &msg, 0);

for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
    if (cm->cmsg_type == SCM_TIMESTAMPING) {
        struct scm_timestamping *ts = (void *)CMSG_DATA(cm);
        printf("HW timestamp: %ld.%09ld\n",
               ts->ts[2].tv_sec, ts->ts[2].tv_nsec);
    }
}
```

---

## Error Handling & Fault Tolerance

### Bus Error Propagation

A critical design decision is whether to propagate bus errors across the bridge:

| Strategy | When to Use |
|---|---|
| **Drop & log** | Non-critical data; logging to file or cloud |
| **Forward error frame** | Diagnostic gateways where the upstream tool must know about bus health |
| **Substitute last-known-good value** | Safety-relevant signals with defined substitution values per AUTOSAR |
| **Trigger DTC** | ISO 14229 (UDS) compliant systems |

### Watchdog Pattern

A gateway that stops forwarding due to a software fault should actively report its own absence:

```c
/* Send a gateway-alive heartbeat on the modern bus every 10 ms */
static void send_heartbeat(int dst_sock, uint32_t gw_id) {
    struct can_frame hb = {
        .can_id  = gw_id,
        .can_dlc = 1,
        .data    = { 0xAA },   /* alive token */
    };
    write(dst_sock, &hb, sizeof(hb));
}
```

The modern ECU sets a reception timeout on `gw_id`; if absent for >30 ms, it flags a `GW_COMMUNICATION_LOST` DTC and enters a safe state.

---

## Summary

Legacy CAN system integration is a multi-layered engineering challenge that spans hardware, real-time firmware, and application-level protocol semantics:

**The core problem** is that CAN networks deployed decades ago — running classic 2.0A/2.0B at low bitrates, using J1939, CANopen, or proprietary higher-layer protocols — must coexist with modern CAN FD, Ethernet, and cloud-connected architectures, yet cannot be replaced because of certification constraints, cost, or inaccessibility.

**The primary tools** are protocol translation gateways and adapters that operate at three levels: transparent frame relay (bitrate and ID bridging), structural translation (PDU format conversion between protocol layers), and semantic translation (signal extraction, unit scaling, and re-encoding).

**In C/C++**, Linux SocketCAN provides a mature, low-overhead API for raw frame relay; the gateway logic is naturally expressed as a mapping table of `(src_id → transform_fn → dst_id)` entries, with protocol-specific parsers (J1939 PGN decoder, CANopen SDO proxy) sitting above the frame layer.

**In Rust**, the same architecture is expressed cleanly using traits (`FrameTransform`), `HashMap`-based routing tables, and `Arc`-wrapped sockets passed into `tokio::task::spawn_blocking` for non-blocking async bridge operation — yielding memory-safe, zero-cost-abstraction code that prevents the class of buffer-overflow bugs common in C implementations.

**Key engineering disciplines** to maintain are: latency budgeting (gateway forwarding must not break the real-time assumptions of safety-critical consumers), hardware timestamp preservation (to detect missed cycles in periodic legacy traffic), and explicit error propagation strategy (drop, substitute, forward, or trigger DTC) aligned with the functional safety requirements of the system.

---

*Document: 89_Legacy_System_Integration.md — Part of the CAN Bus Programming Reference Series*