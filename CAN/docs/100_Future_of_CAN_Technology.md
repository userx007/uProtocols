# 100. Future of CAN Technology

1. **CAN XL Protocol Overview** — Frame format, bit fields table, side-by-side comparison of CAN Classic / FD / XL across speed, payload, VCID, and CRC.

2. **CAN XL in C/C++** — SocketCAN structs, a full TX sender with SDT dispatch, a RX receiver with SDT handler table, and a virtual CAN XL node simulator with a publish/subscribe bus.

3. **CAN XL in Rust** — Strongly-typed `CanXlFrame` builder with error handling, plus an async Tokio receiver using `AsyncFd` over raw SocketCAN syscalls.

4. **CAN ↔ Ethernet Gateway** — Architecture diagram of the zonal E/E backbone, a CAN FD → UDP forwarding gateway in C++ and Rust using packed headers.

5. **AUTOSAR / SOME/IP Bridge** — A PDU router in C that translates CAN signal byte ranges directly into SOME/IP multicast notifications with full header serialization.

6. **Ethernet Tunneling over CAN XL (SDT=0x01)** — Full encapsulate/decapsulate implementation in both C++ and Rust, parsing Ethernet MAC headers from the recovered payload.

7. **Software-Defined Vehicle Bus Manager** — Runtime bus profile switching (Classic / FD / XL / XL+ETH) in C++, and an OTA manifest-driven reconfiguration system in Rust.

8. **SecOC Security** — MAC authentication with freshness counters and anti-replay protection in both C and Rust, following AUTOSAR SecOC principles.

9. **UDS over CAN XL** — Diagnostics server showing how CAN XL's 2048-byte payload eliminates ISO-TP segmentation for large calibration/data blocks.

10. **Bus Health Monitor** — Sliding-window error rate and bus load anomaly detection in Rust.

11. **Migration Coexistence Layer** — A CAN FD ↔ CAN XL promotion/demotion bridge in C for incremental adoption.

12. **Summary + Quick Reference Table** — All key specs and use cases in one table.


> Exploring CAN XL adoption, integration with Ethernet backbones, and evolution in next-generation vehicles.

---

## Table of Contents

1. [Introduction](#introduction)
2. [CAN XL — The Next Generation](#can-xl--the-next-generation)
   - [Protocol Overview](#protocol-overview)
   - [Frame Format](#can-xl-frame-format)
   - [Key Differences: CAN Classic vs. CAN FD vs. CAN XL](#key-differences)
3. [CAN XL Programming in C/C++](#can-xl-programming-in-cc)
   - [Socket CAN with CAN XL on Linux](#socket-can-with-can-xl-on-linux)
   - [Sending a CAN XL Frame](#sending-a-can-xl-frame)
   - [Receiving a CAN XL Frame](#receiving-a-can-xl-frame)
   - [CAN XL Node Simulation](#can-xl-node-simulation)
4. [CAN XL Programming in Rust](#can-xl-programming-in-rust)
   - [CAN XL Frame Construction in Rust](#can-xl-frame-construction-in-rust)
   - [Async CAN XL Receiver in Rust](#async-can-xl-receiver-in-rust)
5. [CAN and Automotive Ethernet Integration](#can-and-automotive-ethernet-integration)
   - [The Multi-Bus Architecture](#the-multi-bus-architecture)
   - [CAN-to-Ethernet Gateway in C/C++](#can-to-ethernet-gateway-in-cc)
   - [CAN-to-Ethernet Gateway in Rust](#can-to-ethernet-gateway-in-rust)
6. [AUTOSAR and Service-Oriented Communication](#autosar-and-service-oriented-communication)
   - [SOME/IP-over-Ethernet Bridge](#someip-over-ethernet-bridge)
   - [PDU Router Abstraction in C](#pdu-router-abstraction-in-c)
7. [CAN XL as an Ethernet Backbone Tunnel](#can-xl-as-an-ethernet-backbone-tunnel)
   - [Tunneling Ethernet Frames over CAN XL in C++](#tunneling-ethernet-frames-over-can-xl-in-c)
   - [Tunnel Implementation in Rust](#tunnel-implementation-in-rust)
8. [Software-Defined Vehicle (SDV) Implications](#software-defined-vehicle-sdv-implications)
   - [Dynamic Bus Configuration in C++](#dynamic-bus-configuration-in-c)
   - [OTA-aware Bus Manager in Rust](#ota-aware-bus-manager-in-rust)
9. [Security in Future CAN Networks](#security-in-future-can-networks)
   - [MAC Authentication for CAN XL in C](#mac-authentication-for-can-xl-in-c)
   - [SecOC-inspired Authenticator in Rust](#secoc-inspired-authenticator-in-rust)
10. [Diagnostics and Monitoring in Next-Gen CAN](#diagnostics-and-monitoring-in-next-gen-can)
    - [UDS over CAN XL in C++](#uds-over-can-xl-in-c)
    - [Bus Health Monitor in Rust](#bus-health-monitor-in-rust)
11. [Migration Strategy: From CAN FD to CAN XL](#migration-strategy)
    - [Coexistence Layer in C](#coexistence-layer-in-c)
12. [Summary](#summary)
13. [Quick Reference Table](#quick-reference-table)

---

## Introduction

The **Controller Area Network (CAN)** bus, standardized in ISO 11898, has been the dominant in-vehicle network protocol for over three decades. CAN Classic (up to 1 Mbit/s) and CAN FD (Flexible Data-Rate, up to 8 Mbit/s data phase) have served the automotive industry well — but the emergence of **Advanced Driver Assistance Systems (ADAS)**, **Over-The-Air (OTA) updates**, **Vehicle-to-Everything (V2X)**, **autonomous driving**, and **Software-Defined Vehicles (SDV)** demands drastically higher bandwidth, lower latency, and tighter integration with IP-based networks.

Three major trends are reshaping the future of CAN:

1. **CAN XL** — A third-generation CAN variant with up to 20 Mbit/s data rate and up to 2048 bytes of payload per frame, bridging the gap between CAN FD and Automotive Ethernet.
2. **Ethernet Backbone Integration** — Modern E/E (Electrical/Electronic) architectures use a high-speed Ethernet spine (100BASE-T1, 1000BASE-T1) with CAN networks connected via domain controllers and central gateways.
3. **Software-Defined Vehicle Evolution** — Centralized compute platforms replacing distributed ECUs, with CAN evolving to support dynamic configuration, service-oriented communication, and cloud connectivity.

---

## CAN XL — The Next Generation

### Protocol Overview

**CAN XL** is defined in **ISO 11898-1:2024** and was developed by the CAN in Automation (CiA) consortium. It is a superset of CAN FD and introduces:

- **Data rate:** Up to **20 Mbit/s** in the data phase (using PWM or NRZ coding with SIC — Signal Improvement Capability transceivers).
- **Payload size:** Up to **2048 bytes** per frame (vs. 64 bytes for CAN FD).
- **Addressing:** 11-bit CAN ID + optional **Virtual CAN Network ID (VCID)** — 8-bit field enabling multiple virtual channels on a single physical bus.
- **Service Data Unit type (SDT):** 8-bit field to indicate the payload type (raw data, Ethernet frame, AUTOSAR PDU, etc.).
- **Acceptance field (AF):** 32-bit field for extended addressing, replacing the DLC for XL frames.
- **CRC:** Enhanced 32-bit CRC for large payloads.
- **Backward compatibility:** CAN XL transceivers can coexist on the same network with CAN FD and CAN Classic nodes.

### CAN XL Frame Format

```
+----------+-------+-----+-----+------+---------+----+-------+
| SOF(1b)  | ARB   | RRS | BRS | VCID |   SDT   | AF | DATA  |
|          | 11-b  | 1b  | 1b  | 8-b  |   8-b   |32-b| 0-2048|
+----------+-------+-----+-----+------+---------+----+-------+
       [Arbitration Phase: up to 1 Mbit/s]  [Data Phase: up to 20 Mbit/s]
```

Key bit fields:

| Field | Size | Description |
|-------|------|-------------|
| SOF | 1 bit | Start of Frame |
| CAN ID | 11 bits | Arbitration identifier |
| RRS | 1 bit | Remote Request Substitution |
| BRS | 1 bit | Bit Rate Switch (switches to data phase speed) |
| VCID | 8 bits | Virtual CAN Network ID |
| SDT | 8 bits | Service Data Unit type |
| AF | 32 bits | Acceptance Field (addressing / length) |
| DATA | 0–2048 bytes | Payload |
| CRC | 32 bits | Cyclic Redundancy Check |
| EOF | 11 bits | End of Frame |

### Key Differences

| Feature | CAN Classic | CAN FD | CAN XL |
|---------|------------|--------|--------|
| Standard | ISO 11898-1 (2003) | ISO 11898-1 (2015) | ISO 11898-1 (2024) |
| Max Data Rate | 1 Mbit/s | 8 Mbit/s | 20 Mbit/s |
| Max Payload | 8 bytes | 64 bytes | 2048 bytes |
| VCID | No | No | Yes (8-bit) |
| SDT Field | No | No | Yes (8-bit) |
| CRC | 15-bit | 17/21-bit | 32-bit |
| Ethernet Tunneling | No | No | Yes (SDT=0x01) |
| Typical Use Case | Body, chassis | Powertrain, safety | Zonal, SDV, Ethernet bridge |

---

## CAN XL Programming in C/C++

### Socket CAN with CAN XL on Linux

Linux kernel 6.2+ includes Socket CAN support for CAN XL via the `CANXL_*` family of structures in `<linux/can.h>`.

```c
// can_xl_defs.h — CAN XL type definitions (subset of linux/can.h)
#ifndef CAN_XL_DEFS_H
#define CAN_XL_DEFS_H

#include <linux/can.h>
#include <stdint.h>

// CAN XL frame struct (as of Linux 6.2+)
#define CANXL_MAX_DLEN   2048
#define CANXL_XLF        0x80000000U  // XL frame flag in can_id
#define CANXL_SEC        0x40000000U  // Simple Extended Content
#define CANXL_VCID_SHIFT 16

struct canxl_frame {
    canid_t can_id;      /* 11-bit CAN ID + XLF flag */
    uint8_t flags;
    uint8_t sdt;         /* Service Data Unit type */
    uint16_t len;        /* Payload length (0–2048) */
    uint32_t af;         /* Acceptance Field */
    uint8_t data[CANXL_MAX_DLEN];
};

#endif /* CAN_XL_DEFS_H */
```

### Sending a CAN XL Frame

```c
// tx_canxl.c — Transmit a CAN XL frame via SocketCAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "can_xl_defs.h"

#define CANXL_IFACE "canxl0"

int send_canxl_frame(int sock, uint32_t can_id, uint8_t vcid,
                     uint8_t sdt, uint32_t af,
                     const uint8_t *payload, uint16_t len)
{
    struct canxl_frame frame;
    memset(&frame, 0, sizeof(frame));

    if (len > CANXL_MAX_DLEN) {
        fprintf(stderr, "Payload too large: %u bytes\n", len);
        return -1;
    }

    /* Set XL flag + 11-bit CAN ID + VCID encoded in upper bits */
    frame.can_id = CANXL_XLF | (can_id & CAN_SFF_MASK)
                   | ((uint32_t)vcid << CANXL_VCID_SHIFT);
    frame.sdt    = sdt;
    frame.len    = len;
    frame.af     = af;
    memcpy(frame.data, payload, len);

    ssize_t ret = write(sock, &frame, sizeof(struct canxl_frame));
    if (ret != sizeof(struct canxl_frame)) {
        perror("write CAN XL frame");
        return -1;
    }

    printf("[TX] CAN XL  ID=0x%03X  VCID=%02X  SDT=%02X  "
           "AF=0x%08X  LEN=%u\n",
           can_id, vcid, sdt, af, len);
    return 0;
}

int main(void)
{
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return EXIT_FAILURE; }

    /* Enable CAN XL on the socket */
    int enable_canxl = 1;
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
               &enable_canxl, sizeof(enable_canxl));

    struct ifreq ifr;
    strncpy(ifr.ifr_name, CANXL_IFACE, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    /* Construct a 512-byte dummy payload */
    uint8_t payload[512];
    for (int i = 0; i < 512; i++) payload[i] = (uint8_t)(i & 0xFF);

    /*
     * SDT=0x00: raw CAN XL payload
     * SDT=0x01: tunneled Ethernet frame
     * SDT=0x02: AUTOSAR PDU (SOME/IP etc.)
     */
    send_canxl_frame(sock,
                     /*can_id=*/ 0x123,
                     /*vcid=*/   0x01,
                     /*sdt=*/    0x00,
                     /*af=*/     0xDEADBEEF,
                     payload, sizeof(payload));

    close(sock);
    return EXIT_SUCCESS;
}
```

### Receiving a CAN XL Frame

```c
// rx_canxl.c — Receive and dispatch CAN XL frames
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "can_xl_defs.h"

/* SDT dispatch table */
typedef void (*sdt_handler_t)(const struct canxl_frame *frame);

static void handle_raw_payload(const struct canxl_frame *f) {
    printf("[SDT=0x00] Raw payload, %u bytes\n", f->len);
}

static void handle_eth_tunnel(const struct canxl_frame *f) {
    printf("[SDT=0x01] Tunneled Ethernet frame, %u bytes\n", f->len);
    /* Forward to virtual Ethernet interface, e.g. veth0 */
}

static void handle_autosar_pdu(const struct canxl_frame *f) {
    printf("[SDT=0x02] AUTOSAR PDU, %u bytes\n", f->len);
}

static sdt_handler_t sdt_table[256] = {
    [0x00] = handle_raw_payload,
    [0x01] = handle_eth_tunnel,
    [0x02] = handle_autosar_pdu,
};

int main(void)
{
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return EXIT_FAILURE; }

    int enable_canxl = 1;
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
               &enable_canxl, sizeof(enable_canxl));

    struct ifreq ifr;
    strncpy(ifr.ifr_name, "canxl0", IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    printf("Listening for CAN XL frames on canxl0...\n");

    while (1) {
        struct canxl_frame frame;
        ssize_t nbytes = read(sock, &frame, sizeof(frame));
        if (nbytes < 0) { perror("read"); break; }

        /* Ignore non-XL frames */
        if (!(frame.can_id & CANXL_XLF)) continue;

        uint32_t can_id = frame.can_id & CAN_SFF_MASK;
        uint8_t  vcid   = (uint8_t)((frame.can_id >> CANXL_VCID_SHIFT) & 0xFF);

        printf("[RX] ID=0x%03X  VCID=%02X  SDT=%02X  "
               "AF=0x%08X  LEN=%u\n",
               can_id, vcid, frame.sdt, frame.af, frame.len);

        /* Dispatch by SDT */
        if (sdt_table[frame.sdt]) {
            sdt_table[frame.sdt](&frame);
        } else {
            printf("  [WARN] No handler for SDT=0x%02X\n", frame.sdt);
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}
```

### CAN XL Node Simulation

```cpp
// canxl_node.cpp — A simulated CAN XL ECU node
#include <iostream>
#include <vector>
#include <functional>
#include <cstring>
#include <thread>
#include <chrono>
#include "can_xl_defs.h"  // as defined above

struct CanXlMessage {
    uint32_t  can_id;
    uint8_t   vcid;
    uint8_t   sdt;
    uint32_t  af;
    std::vector<uint8_t> data;
};

class CanXlNode {
public:
    using RxCallback = std::function<void(const CanXlMessage&)>;

    explicit CanXlNode(std::string name, uint8_t vcid)
        : name_(std::move(name)), vcid_(vcid) {}

    void on_receive(uint8_t sdt, RxCallback cb) {
        callbacks_[sdt] = std::move(cb);
    }

    void inject_frame(const CanXlMessage& msg) {
        if (msg.vcid != vcid_ && msg.vcid != 0xFF) return; // VCID filter
        auto it = callbacks_.find(msg.sdt);
        if (it != callbacks_.end()) it->second(msg);
    }

    void transmit(uint32_t can_id, uint8_t sdt, uint32_t af,
                  std::vector<uint8_t> data,
                  std::function<void(CanXlMessage)> bus_send)
    {
        CanXlMessage msg{ can_id, vcid_, sdt, af, std::move(data) };
        std::cout << "[" << name_ << "] TX  ID=0x"
                  << std::hex << can_id
                  << "  SDT=0x" << (int)sdt
                  << "  LEN=" << std::dec << msg.data.size() << "\n";
        bus_send(std::move(msg));
    }

private:
    std::string name_;
    uint8_t     vcid_;
    std::unordered_map<uint8_t, RxCallback> callbacks_;
};

// ---- Minimal virtual CAN XL bus ----
class VirtualCanXlBus {
public:
    void connect(CanXlNode* node) { nodes_.push_back(node); }

    void send(CanXlMessage msg) {
        for (auto* n : nodes_) n->inject_frame(msg);
    }

private:
    std::vector<CanXlNode*> nodes_;
};

int main() {
    VirtualCanXlBus bus;

    CanXlNode sensor("SensorECU",   0x01);
    CanXlNode gateway("ZonalGW",    0xFF); // VCID 0xFF = broadcast

    bus.connect(&sensor);
    bus.connect(&gateway);

    // Gateway listens for SDT=0x00 raw data
    gateway.on_receive(0x00, [](const CanXlMessage& m) {
        std::cout << "[ZonalGW] RX raw data, "
                  << m.data.size() << " bytes from VCID="
                  << (int)m.vcid << "\n";
    });

    auto bus_send = [&](CanXlMessage m){ bus.send(std::move(m)); };

    // Simulate periodic sensor transmission
    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> payload(256, static_cast<uint8_t>(i));
        sensor.transmit(0x200, 0x00, 0x00000001, payload, bus_send);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
```

---

## CAN XL Programming in Rust

### CAN XL Frame Construction in Rust

```toml
# Cargo.toml
[package]
name    = "canxl_demo"
version = "0.1.0"
edition = "2021"

[dependencies]
tokio  = { version = "1", features = ["full"] }
libc   = "0.2"
bytes  = "1"
```

```rust
// src/canxl_frame.rs — CAN XL frame definition and builder
use std::fmt;

pub const CANXL_MAX_DLEN: usize = 2048;
pub const CANXL_XLF_FLAG: u32   = 0x80000000;
pub const CANXL_VCID_SHIFT: u32 = 16;

#[derive(Debug, Clone)]
pub struct CanXlFrame {
    /// 11-bit CAN ID
    pub can_id: u16,
    /// Virtual CAN Network ID (0x00–0xFF)
    pub vcid: u8,
    /// Service Data Unit type
    pub sdt: u8,
    /// Acceptance Field
    pub af: u32,
    /// Payload (0–2048 bytes)
    pub data: Vec<u8>,
}

impl CanXlFrame {
    /// Construct a new CAN XL frame with validation.
    pub fn new(
        can_id: u16,
        vcid: u8,
        sdt: u8,
        af: u32,
        data: Vec<u8>,
    ) -> Result<Self, CanXlError> {
        if can_id > 0x7FF {
            return Err(CanXlError::InvalidId(can_id));
        }
        if data.len() > CANXL_MAX_DLEN {
            return Err(CanXlError::PayloadTooLarge(data.len()));
        }
        Ok(Self { can_id, vcid, sdt, af, data })
    }

    /// Serialize into raw kernel `canxl_frame` bytes (little-endian).
    pub fn to_raw_bytes(&self) -> [u8; 8 + CANXL_MAX_DLEN] {
        let mut buf = [0u8; 8 + CANXL_MAX_DLEN];

        let raw_id: u32 = CANXL_XLF_FLAG
            | ((self.vcid as u32) << CANXL_VCID_SHIFT)
            | (self.can_id as u32);

        buf[0..4].copy_from_slice(&raw_id.to_le_bytes());
        buf[4] = 0; // flags
        buf[5] = self.sdt;
        buf[6..8].copy_from_slice(&(self.data.len() as u16).to_le_bytes());
        // af would follow at byte 8 in a real kernel struct
        buf[8..12].copy_from_slice(&self.af.to_le_bytes());
        buf[12..12 + self.data.len()].copy_from_slice(&self.data);
        buf
    }
}

#[derive(Debug)]
pub enum CanXlError {
    InvalidId(u16),
    PayloadTooLarge(usize),
    IoError(std::io::Error),
}

impl fmt::Display for CanXlError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidId(id)         => write!(f, "Invalid 11-bit CAN ID: 0x{id:03X}"),
            Self::PayloadTooLarge(len)  => write!(f, "Payload too large: {len} bytes"),
            Self::IoError(e)            => write!(f, "I/O error: {e}"),
        }
    }
}

impl From<std::io::Error> for CanXlError {
    fn from(e: std::io::Error) -> Self { Self::IoError(e) }
}
```

### Async CAN XL Receiver in Rust

```rust
// src/canxl_socket.rs — Async CAN XL SocketCAN wrapper using tokio
use tokio::io::unix::AsyncFd;
use std::os::unix::io::RawFd;
use libc::{
    socket, bind, AF_CAN, PF_CAN, SOCK_RAW,
    sockaddr, sockaddr_can, ifreq,
    SIOCGIFINDEX, ioctl, setsockopt,
    SOL_CAN_RAW,
};

pub const CAN_RAW: libc::c_int = 1;
pub const CAN_RAW_XL_FRAMES: libc::c_int = 7; // Linux 6.2+

#[repr(C)]
pub struct RawCanXlFrame {
    pub can_id:  u32,
    pub flags:   u8,
    pub sdt:     u8,
    pub len:     u16,
    pub af:      u32,
    pub data:    [u8; 2048],
}

pub struct CanXlSocket {
    fd: AsyncFd<RawFd>,
}

impl CanXlSocket {
    pub fn bind(iface: &str) -> std::io::Result<Self> {
        let sock_fd = unsafe {
            socket(PF_CAN, SOCK_RAW, CAN_RAW)
        };
        if sock_fd < 0 {
            return Err(std::io::Error::last_os_error());
        }

        // Enable CAN XL reception
        let enable: libc::c_int = 1;
        unsafe {
            setsockopt(
                sock_fd,
                SOL_CAN_RAW,
                CAN_RAW_XL_FRAMES,
                &enable as *const _ as *const libc::c_void,
                std::mem::size_of_val(&enable) as u32,
            );
        }

        // Resolve interface index
        let mut ifr: ifreq = unsafe { std::mem::zeroed() };
        let name_bytes = iface.as_bytes();
        unsafe {
            std::ptr::copy_nonoverlapping(
                name_bytes.as_ptr() as *const libc::c_char,
                ifr.ifr_name.as_mut_ptr(),
                name_bytes.len().min(libc::IFNAMSIZ - 1),
            );
            ioctl(sock_fd, SIOCGIFINDEX, &mut ifr as *mut _);
        }

        let addr: sockaddr_can = unsafe {
            let mut a: sockaddr_can = std::mem::zeroed();
            a.can_family  = AF_CAN as u16;
            a.can_ifindex = ifr.ifr_ifru.ifru_ifindex;
            a
        };

        let rc = unsafe {
            bind(
                sock_fd,
                &addr as *const sockaddr_can as *const sockaddr,
                std::mem::size_of::<sockaddr_can>() as u32,
            )
        };
        if rc < 0 {
            return Err(std::io::Error::last_os_error());
        }

        Ok(Self {
            fd: AsyncFd::new(sock_fd)?,
        })
    }

    pub async fn recv(&self) -> std::io::Result<RawCanXlFrame> {
        loop {
            let mut guard = self.fd.readable().await?;
            let mut frame: RawCanXlFrame = unsafe { std::mem::zeroed() };

            match guard.try_io(|fd| {
                let n = unsafe {
                    libc::read(
                        *fd.get_ref(),
                        &mut frame as *mut _ as *mut libc::c_void,
                        std::mem::size_of::<RawCanXlFrame>(),
                    )
                };
                if n < 0 {
                    Err(std::io::Error::last_os_error())
                } else {
                    Ok(frame)
                }
            }) {
                Ok(Ok(f))  => return Ok(f),
                Ok(Err(e)) => return Err(e),
                Err(_would_block) => continue,
            }
        }
    }
}

#[tokio::main]
async fn main() -> std::io::Result<()> {
    let sock = CanXlSocket::bind("canxl0")?;
    println!("Listening for CAN XL frames on canxl0...");

    loop {
        let frame = sock.recv().await?;
        let is_xl   = frame.can_id & 0x80000000 != 0;
        let can_id  = frame.can_id & 0x7FF;
        let vcid    = ((frame.can_id >> 16) & 0xFF) as u8;

        if !is_xl { continue; }

        println!(
            "[RX] ID=0x{can_id:03X}  VCID={vcid:02X}  SDT={:02X}  \
             AF=0x{:08X}  LEN={}",
            frame.sdt, frame.af, frame.len
        );
    }
}
```

---

## CAN and Automotive Ethernet Integration

### The Multi-Bus Architecture

Modern vehicles use a **zonal E/E architecture** where:

- A central **High-Performance Computer (HPC)** or **Vehicle Computer** connects to the Ethernet backbone (1000BASE-T1 or 10GBASE-T1).
- **Zonal controllers** bridge CAN/LIN/MOST networks to the Ethernet backbone.
- CAN (Classic / FD / XL) remains essential for **safety-critical, time-deterministic** control functions (braking, steering, powertrain).

```
┌─────────────────────────────────────────────────────┐
│               ETHERNET BACKBONE (1000BASE-T1)        │
│  ┌─────────┐    ┌────────────┐    ┌──────────────┐  │
│  │  HPC /  │    │  Zonal GW  │    │  Zonal GW    │  │
│  │  ADAS   │────│  Front     │    │  Rear        │  │
│  └─────────┘    └─────┬──────┘    └──────┬───────┘  │
└────────────────────────┼──────────────────┼──────────┘
                         │ CAN XL / FD       │ CAN FD
               ┌─────────┴──┐         ┌─────┴──────┐
               │ Radar ECU  │         │ Rear Lights│
               │ Camera ECU │         │ Trunk ECU  │
               └────────────┘         └────────────┘
```

### CAN-to-Ethernet Gateway in C/C++

```cpp
// gw_can_eth.cpp — CAN FD to UDP/IP gateway (simplified)
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define ETH_GW_IP    "192.168.1.100"
#define ETH_GW_PORT  5000
#define CAN_IFACE    "can0"

// CAN-to-UDP packet header
#pragma pack(push, 1)
struct CanEthPacket {
    uint32_t timestamp_ms;
    uint32_t can_id;
    uint8_t  dlc;
    uint8_t  data[64]; // CAN FD max
};
#pragma pack(pop)

class CanToEthGateway {
public:
    CanToEthGateway() {
        // Setup CAN socket
        can_sock_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        struct ifreq ifr;
        strncpy(ifr.ifr_name, CAN_IFACE, IFNAMSIZ);
        ioctl(can_sock_, SIOCGIFINDEX, &ifr);

        struct sockaddr_can addr{};
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        bind(can_sock_, (struct sockaddr*)&addr, sizeof(addr));

        // Enable CAN FD
        int enable_fd = 1;
        setsockopt(can_sock_, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                   &enable_fd, sizeof(enable_fd));

        // Setup UDP socket
        udp_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        memset(&eth_addr_, 0, sizeof(eth_addr_));
        eth_addr_.sin_family      = AF_INET;
        eth_addr_.sin_port        = htons(ETH_GW_PORT);
        inet_pton(AF_INET, ETH_GW_IP, &eth_addr_.sin_addr);
    }

    ~CanToEthGateway() {
        close(can_sock_);
        close(udp_sock_);
    }

    void run() {
        running_ = true;
        printf("[GW] CAN→ETH gateway started: %s → %s:%d\n",
               CAN_IFACE, ETH_GW_IP, ETH_GW_PORT);

        while (running_) {
            struct canfd_frame frame;
            ssize_t n = read(can_sock_, &frame, sizeof(frame));
            if (n <= 0) break;

            forward_to_eth(frame);
        }
    }

    void stop() { running_ = false; }

private:
    void forward_to_eth(const struct canfd_frame& cf) {
        CanEthPacket pkt{};
        pkt.timestamp_ms = get_timestamp_ms();
        pkt.can_id       = cf.can_id;
        pkt.dlc          = cf.len;
        memcpy(pkt.data, cf.data, cf.len);

        sendto(udp_sock_, &pkt, sizeof(pkt), 0,
               (struct sockaddr*)&eth_addr_, sizeof(eth_addr_));

        printf("[GW] FWD  ID=0x%03X  LEN=%u  → %s:%d\n",
               cf.can_id & CAN_SFF_MASK, cf.len, ETH_GW_IP, ETH_GW_PORT);
    }

    static uint32_t get_timestamp_ms() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }

    int can_sock_, udp_sock_;
    struct sockaddr_in eth_addr_;
    std::atomic<bool> running_{false};
};

int main() {
    CanToEthGateway gw;
    gw.run();
    return 0;
}
```

### CAN-to-Ethernet Gateway in Rust

```rust
// src/can_eth_gw.rs — CAN FD to UDP gateway in Rust
use std::net::UdpSocket;
use std::time::{SystemTime, UNIX_EPOCH};

const CAN_IFACE:   &str = "can0";
const ETH_GW_ADDR: &str = "192.168.1.100:5000";
const CANFD_MTU:   usize = 72; // canfd_frame size

#[repr(C, packed)]
struct CanEthPacket {
    timestamp_ms: u32,
    can_id:       u32,
    dlc:          u8,
    data:         [u8; 64],
}

fn timestamp_ms() -> u32 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .subsec_millis()
}

fn parse_canfd_frame(buf: &[u8]) -> Option<(u32, u8, &[u8])> {
    if buf.len() < 8 { return None; }
    let can_id = u32::from_le_bytes(buf[0..4].try_into().ok()?);
    let dlc    = buf[4];
    let data   = &buf[8..8 + dlc as usize];
    Some((can_id, dlc, data))
}

fn run_gateway() -> std::io::Result<()> {
    use std::os::unix::io::FromRawFd;

    // Open CAN socket via raw syscall
    let sock_fd = unsafe {
        libc::socket(libc::PF_CAN, libc::SOCK_RAW, 1 /*CAN_RAW*/)
    };
    if sock_fd < 0 {
        return Err(std::io::Error::last_os_error());
    }

    // Bind to can0 interface
    let mut ifr: libc::ifreq = unsafe { std::mem::zeroed() };
    let iface_bytes = CAN_IFACE.as_bytes();
    unsafe {
        std::ptr::copy_nonoverlapping(
            iface_bytes.as_ptr() as *const libc::c_char,
            ifr.ifr_name.as_mut_ptr(),
            iface_bytes.len(),
        );
        libc::ioctl(sock_fd, libc::SIOCGIFINDEX, &mut ifr);
    }

    let mut addr: libc::sockaddr_can = unsafe { std::mem::zeroed() };
    addr.can_family  = libc::AF_CAN as u16;
    addr.can_ifindex = unsafe { ifr.ifr_ifru.ifru_ifindex };

    let rc = unsafe {
        libc::bind(
            sock_fd,
            &addr as *const _ as *const libc::sockaddr,
            std::mem::size_of::<libc::sockaddr_can>() as u32,
        )
    };
    if rc < 0 {
        return Err(std::io::Error::last_os_error());
    }

    // Enable CAN FD
    let enable: libc::c_int = 1;
    unsafe {
        libc::setsockopt(
            sock_fd,
            libc::SOL_CAN_RAW,
            5, /* CAN_RAW_FD_FRAMES */
            &enable as *const _ as *const libc::c_void,
            std::mem::size_of_val(&enable) as u32,
        );
    }

    let udp = UdpSocket::bind("0.0.0.0:0")?;
    println!("[GW] CAN→ETH gateway: {CAN_IFACE} → {ETH_GW_ADDR}");

    let mut buf = [0u8; CANFD_MTU];
    loop {
        let n = unsafe {
            libc::read(
                sock_fd,
                buf.as_mut_ptr() as *mut libc::c_void,
                buf.len(),
            )
        };
        if n < 0 { return Err(std::io::Error::last_os_error()); }

        if let Some((can_id, dlc, data)) = parse_canfd_frame(&buf[..n as usize]) {
            let mut pkt = CanEthPacket {
                timestamp_ms: timestamp_ms(),
                can_id,
                dlc,
                data: [0u8; 64],
            };
            pkt.data[..data.len()].copy_from_slice(data);

            let raw = unsafe {
                std::slice::from_raw_parts(
                    &pkt as *const CanEthPacket as *const u8,
                    std::mem::size_of::<CanEthPacket>(),
                )
            };
            udp.send_to(raw, ETH_GW_ADDR)?;

            println!("[GW] FWD  ID=0x{:03X}  DLC={dlc}", can_id & 0x7FF);
        }
    }
}

fn main() {
    if let Err(e) = run_gateway() {
        eprintln!("Gateway error: {e}");
    }
}
```

---

## AUTOSAR and Service-Oriented Communication

### SOME/IP-over-Ethernet Bridge

In next-generation vehicles, **SOME/IP** (Scalable service-Oriented MiddlewarE over IP) carries service-oriented communication over Ethernet. CAN signals are translated into SOME/IP service events by a gateway. CAN XL's SDT field (SDT=0x02) can natively carry AUTOSAR PDUs.

### PDU Router Abstraction in C

```c
// pdu_router.c — Simplified AUTOSAR-style PDU router
// Translates CAN signals to SOME/IP events and vice versa
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

#define SOMEIP_HEADER_LEN    16
#define MAX_PDU_LEN          1400

/* SOME/IP header fields (big-endian on wire) */
typedef struct {
    uint16_t service_id;
    uint16_t method_id;
    uint32_t length;
    uint16_t client_id;
    uint16_t session_id;
    uint8_t  protocol_version;
    uint8_t  iface_version;
    uint8_t  msg_type;    /* 0x00=REQUEST, 0x02=NOTIFICATION */
    uint8_t  return_code;
} SomeIpHeader;

/* CAN signal → SOME/IP notification mapping */
typedef struct {
    uint32_t can_id;
    uint16_t someip_service_id;
    uint16_t someip_method_id;
    uint8_t  byte_offset;  /* Position of signal in CAN frame */
    uint8_t  byte_len;
} CanToSomeIpMap;

static const CanToSomeIpMap route_table[] = {
    /* CAN ID    SrvID   MethodID  Off  Len */
    { 0x100,    0x0101, 0x8001,   0,   4 },  /* Engine speed → SOME/IP event */
    { 0x200,    0x0102, 0x8002,   0,   2 },  /* Brake pressure → SOME/IP event */
    { 0x300,    0x0103, 0x8003,   2,   2 },  /* Steering angle → SOME/IP event */
};

static int udp_sock = -1;
static struct sockaddr_in someip_addr;

static void init_someip_transport(const char* ip, uint16_t port) {
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&someip_addr, 0, sizeof(someip_addr));
    someip_addr.sin_family = AF_INET;
    someip_addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &someip_addr.sin_addr);
}

static void send_someip_notification(const CanToSomeIpMap* route,
                                     const uint8_t* signal_data,
                                     uint8_t signal_len)
{
    uint8_t buf[SOMEIP_HEADER_LEN + MAX_PDU_LEN] = {0};
    SomeIpHeader *hdr = (SomeIpHeader*)buf;

    hdr->service_id       = htons(route->someip_service_id);
    hdr->method_id        = htons(route->someip_method_id);
    hdr->length           = htonl(8 + signal_len); /* After first 8 bytes */
    hdr->client_id        = htons(0x0000);
    hdr->session_id       = htons(0x0001);
    hdr->protocol_version = 0x01;
    hdr->iface_version    = 0x01;
    hdr->msg_type         = 0x02; /* NOTIFICATION */
    hdr->return_code      = 0x00; /* E_OK */

    memcpy(buf + SOMEIP_HEADER_LEN, signal_data, signal_len);

    sendto(udp_sock, buf, SOMEIP_HEADER_LEN + signal_len, 0,
           (struct sockaddr*)&someip_addr, sizeof(someip_addr));

    printf("[PDU_RTR] CAN 0x%03X → SOME/IP Srv=0x%04X Mth=0x%04X len=%u\n",
           route->can_id, route->someip_service_id,
           route->someip_method_id, signal_len);
}

void pdu_router_process_can_frame(uint32_t can_id,
                                   const uint8_t *data,
                                   uint8_t dlc)
{
    for (size_t i = 0;
         i < sizeof(route_table) / sizeof(route_table[0]); i++)
    {
        const CanToSomeIpMap *r = &route_table[i];
        if (r->can_id != can_id) continue;
        if (r->byte_offset + r->byte_len > dlc) continue;

        send_someip_notification(r,
                                  data + r->byte_offset,
                                  r->byte_len);
        break;
    }
}

int main(void) {
    init_someip_transport("239.127.3.1", 30490); /* SOME/IP multicast */

    /* Simulate incoming CAN frames */
    uint8_t engine_speed[] = { 0x1F, 0x40, 0x00, 0x00 }; /* ~8000 RPM */
    pdu_router_process_can_frame(0x100, engine_speed, sizeof(engine_speed));

    uint8_t brake[] = { 0x05, 0xA0 }; /* brake pressure */
    pdu_router_process_can_frame(0x200, brake, sizeof(brake));

    return 0;
}
```

---

## CAN XL as an Ethernet Backbone Tunnel

One of the most powerful features of CAN XL is its ability to **tunnel full Ethernet frames** using SDT=0x01, enabling Ethernet-based protocols (SOME/IP, DoIP, AVB) to traverse a CAN XL segment without a dedicated Ethernet wire.

### Tunneling Ethernet Frames over CAN XL in C++

```cpp
// eth_over_canxl.cpp — Ethernet frame tunneling over CAN XL
#include <cstring>
#include <cstdio>
#include <vector>
#include <cstdint>

// SDT value for Ethernet tunneling per CiA 610-3
static constexpr uint8_t SDT_ETH_TUNNEL = 0x01;
static constexpr uint16_t MAX_ETH_FRAME  = 1518;  // Max Ethernet frame
static constexpr uint16_t CANXL_MAX_DATA = 2048;

struct EthernetHeader {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
};

struct CanXlTunnelFrame {
    uint32_t can_id;
    uint8_t  vcid;
    uint8_t  sdt;      // Must be SDT_ETH_TUNNEL = 0x01
    uint16_t data_len;
    uint32_t af;       // Carries Ethernet frame sequence number
    std::vector<uint8_t> data;  // Raw Ethernet frame bytes
};

class EthCanXlTunnel {
public:
    /**
     * Encapsulate a raw Ethernet frame into a CAN XL tunnel frame.
     * Per CiA 610-3, the Ethernet frame (incl. header, excl. FCS)
     * is placed directly in the CAN XL payload.
     */
    static CanXlTunnelFrame encapsulate(const uint8_t* eth_frame,
                                         uint16_t eth_len,
                                         uint8_t  vcid = 0x00)
    {
        if (eth_len > CANXL_MAX_DATA) {
            // Segmentation required — simplified: truncate for demo
            eth_len = CANXL_MAX_DATA;
        }

        CanXlTunnelFrame tunnel{};
        tunnel.can_id   = 0x001;              // Reserved CAN ID for ETH tunnel
        tunnel.vcid     = vcid;
        tunnel.sdt      = SDT_ETH_TUNNEL;
        tunnel.af       = seq_num_++;          // Sequence counter in AF
        tunnel.data.assign(eth_frame, eth_frame + eth_len);
        tunnel.data_len = eth_len;

        return tunnel;
    }

    /**
     * Decapsulate a CAN XL tunnel frame back to Ethernet.
     */
    static std::vector<uint8_t> decapsulate(const CanXlTunnelFrame& tunnel)
    {
        if (tunnel.sdt != SDT_ETH_TUNNEL) {
            printf("[TUNNEL] Error: SDT=0x%02X is not ETH tunnel\n",
                   tunnel.sdt);
            return {};
        }
        printf("[TUNNEL] RX Ethernet frame, seq=%u, %zu bytes\n",
               tunnel.af, tunnel.data.size());

        const auto& d = tunnel.data;
        if (d.size() >= sizeof(EthernetHeader)) {
            const auto* eh = reinterpret_cast<const EthernetHeader*>(d.data());
            printf("  DST=%02X:%02X:%02X:%02X:%02X:%02X  "
                   "SRC=%02X:%02X:%02X:%02X:%02X:%02X  "
                   "Type=0x%04X\n",
                   eh->dst_mac[0], eh->dst_mac[1], eh->dst_mac[2],
                   eh->dst_mac[3], eh->dst_mac[4], eh->dst_mac[5],
                   eh->src_mac[0], eh->src_mac[1], eh->src_mac[2],
                   eh->src_mac[3], eh->src_mac[4], eh->src_mac[5],
                   ntohs(eh->ethertype));
        }
        return tunnel.data;
    }

private:
    static inline uint32_t seq_num_ = 0;
};

int main() {
    // Fake Ethernet frame: DST=FF:FF:FF:FF:FF:FF (broadcast), Type=0x0800 (IPv4)
    uint8_t eth_frame[60] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,  // dst MAC
        0x00,0x1A,0x2B,0x3C,0x4D,0x5E, // src MAC
        0x08,0x00,                      // EtherType: IPv4
        // IPv4 payload (dummy)
        0x45,0x00,0x00,0x2E, 0x00,0x01, 0x00,0x00,
        0x40,0x11,0x00,0x00, 0xC0,0xA8,0x01,0x01,
        0xC0,0xA8,0x01,0x64, 0x13,0x88,0x13,0x88,
    };

    // Encapsulate and print
    auto tunnel = EthCanXlTunnel::encapsulate(eth_frame, sizeof(eth_frame), 0x01);
    printf("[TUNNEL] TX CAN XL tunnel  VCID=%02X  SDT=%02X  "
           "AF(seq)=%u  DataLen=%u\n",
           tunnel.vcid, tunnel.sdt, tunnel.af, tunnel.data_len);

    // Decapsulate on the other side
    auto recovered = EthCanXlTunnel::decapsulate(tunnel);
    printf("[TUNNEL] Recovered %zu bytes of Ethernet payload\n",
           recovered.size());

    return 0;
}
```

### Tunnel Implementation in Rust

```rust
// src/eth_canxl_tunnel.rs — Ethernet tunneling over CAN XL in Rust
use std::sync::atomic::{AtomicU32, Ordering};
use std::fmt;

const SDT_ETH_TUNNEL: u8  = 0x01;
const CANXL_MAX_DATA: usize = 2048;

static SEQ_COUNTER: AtomicU32 = AtomicU32::new(0);

#[derive(Debug, Clone)]
pub struct CanXlTunnelFrame {
    pub can_id:  u16,
    pub vcid:    u8,
    pub sdt:     u8,
    pub af:      u32,  // Used as sequence counter
    pub payload: Vec<u8>,
}

#[derive(Debug)]
pub enum TunnelError {
    FrameTooLarge(usize),
    WrongSdt(u8),
    InvalidEthernetFrame,
}

impl fmt::Display for TunnelError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::FrameTooLarge(n) => write!(f, "Frame too large: {n} bytes"),
            Self::WrongSdt(s)      => write!(f, "Expected SDT=0x01, got 0x{s:02X}"),
            Self::InvalidEthernetFrame => write!(f, "Invalid Ethernet frame"),
        }
    }
}

/// Encapsulate an Ethernet frame into a CAN XL tunnel frame (SDT=0x01).
pub fn encapsulate(
    eth_frame: &[u8],
    vcid: u8,
    can_id: u16,
) -> Result<CanXlTunnelFrame, TunnelError> {
    if eth_frame.len() > CANXL_MAX_DATA {
        return Err(TunnelError::FrameTooLarge(eth_frame.len()));
    }

    let seq = SEQ_COUNTER.fetch_add(1, Ordering::Relaxed);

    Ok(CanXlTunnelFrame {
        can_id,
        vcid,
        sdt: SDT_ETH_TUNNEL,
        af: seq,
        payload: eth_frame.to_vec(),
    })
}

/// Decapsulate a CAN XL tunnel frame back to an Ethernet frame.
pub fn decapsulate(frame: &CanXlTunnelFrame) -> Result<Vec<u8>, TunnelError> {
    if frame.sdt != SDT_ETH_TUNNEL {
        return Err(TunnelError::WrongSdt(frame.sdt));
    }
    if frame.payload.len() < 14 {
        return Err(TunnelError::InvalidEthernetFrame);
    }

    let dst = &frame.payload[0..6];
    let src = &frame.payload[6..12];
    let eth_type = u16::from_be_bytes([frame.payload[12], frame.payload[13]]);

    println!(
        "[TUNNEL] RX seq={} | DST={} | SRC={} | EtherType=0x{eth_type:04X} | len={}",
        frame.af,
        format_mac(dst),
        format_mac(src),
        frame.payload.len(),
    );

    Ok(frame.payload.clone())
}

fn format_mac(mac: &[u8]) -> String {
    mac.iter()
        .map(|b| format!("{b:02X}"))
        .collect::<Vec<_>>()
        .join(":")
}

fn main() {
    // Construct a fake ARP Ethernet frame (EtherType 0x0806)
    let mut eth_frame = vec![
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Broadcast dst
        0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E, // Source MAC
        0x08, 0x06,                           // EtherType: ARP
    ];
    eth_frame.extend_from_slice(&[0u8; 46]); // Minimum payload

    match encapsulate(&eth_frame, 0x01, 0x001) {
        Ok(tunnel) => {
            println!(
                "[TUNNEL] TX | VCID={:02X} | SDT={:02X} | seq={} | len={}",
                tunnel.vcid, tunnel.sdt, tunnel.af, tunnel.payload.len()
            );
            match decapsulate(&tunnel) {
                Ok(recovered) => println!(
                    "[TUNNEL] Recovered {} bytes", recovered.len()
                ),
                Err(e) => eprintln!("[TUNNEL] Decap error: {e}"),
            }
        }
        Err(e) => eprintln!("[TUNNEL] Encap error: {e}"),
    }
}
```

---

## Software-Defined Vehicle (SDV) Implications

The **Software-Defined Vehicle** paradigm decouples vehicle functionality from hardware. Functions previously implemented in dedicated ECUs become software services running on shared compute platforms. CAN bus must evolve to support:

- **Dynamic bus configuration** — Bit rate, VCID assignment, and filter rules are configured at runtime via OTA.
- **Service discovery** — Nodes announce capabilities and subscribe to signals dynamically.
- **Virtualization** — Multiple virtual CAN networks share a single physical bus via CAN XL VCIDs.

### Dynamic Bus Configuration in C++

```cpp
// sdv_bus_manager.cpp — Runtime CAN bus reconfiguration
#include <iostream>
#include <map>
#include <string>
#include <functional>
#include <cstdint>

enum class BusProfile {
    LEGACY_CAN_CLASSIC,
    CAN_FD_STANDARD,
    CAN_XL_HIGH_SPEED,
    CAN_XL_ETH_TUNNEL,
};

struct BusConfig {
    BusProfile profile;
    uint32_t   arb_bitrate;   // bps, e.g. 500000
    uint32_t   data_bitrate;  // bps, e.g. 5000000 (CAN FD/XL only)
    uint8_t    vcid;          // Virtual network ID (CAN XL only)
    bool       eth_tunnel;    // Enable Ethernet tunneling (SDT=0x01)
};

class SdvBusManager {
public:
    using ReconfigCallback = std::function<void(const std::string&, const BusConfig&)>;

    SdvBusManager() {
        // Default profiles
        profiles_["legacy"]   = { BusProfile::LEGACY_CAN_CLASSIC, 500'000,       0,        0x00, false };
        profiles_["canfd"]    = { BusProfile::CAN_FD_STANDARD,   500'000, 2'000'000,   0x00, false };
        profiles_["canxl_hs"] = { BusProfile::CAN_XL_HIGH_SPEED, 500'000, 10'000'000,  0x01, false };
        profiles_["canxl_et"] = { BusProfile::CAN_XL_ETH_TUNNEL, 500'000, 20'000'000,  0x02, true  };
    }

    void register_reconfig_callback(ReconfigCallback cb) {
        reconfig_cb_ = std::move(cb);
    }

    bool apply_profile(const std::string& iface, const std::string& profile_name) {
        auto it = profiles_.find(profile_name);
        if (it == profiles_.end()) {
            std::cerr << "[SDV-BM] Unknown profile: " << profile_name << "\n";
            return false;
        }

        const BusConfig& cfg = it->second;
        std::cout << "[SDV-BM] Applying profile '" << profile_name
                  << "' on " << iface << "\n"
                  << "  Arb bitrate : " << cfg.arb_bitrate  << " bps\n"
                  << "  Data bitrate: " << cfg.data_bitrate << " bps\n"
                  << "  VCID        : 0x" << std::hex << (int)cfg.vcid << std::dec << "\n"
                  << "  ETH tunnel  : " << (cfg.eth_tunnel ? "yes" : "no") << "\n";

        // In production: call ip(8) or netlink to reconfigure the interface
        apply_to_kernel(iface, cfg);

        active_configs_[iface] = cfg;
        if (reconfig_cb_) reconfig_cb_(iface, cfg);
        return true;
    }

    const BusConfig* get_active_config(const std::string& iface) const {
        auto it = active_configs_.find(iface);
        return (it != active_configs_.end()) ? &it->second : nullptr;
    }

private:
    void apply_to_kernel(const std::string& iface, const BusConfig& cfg) {
        // Production: use netlink AF_CAN or ip-link to set bitrate
        char cmd[256];
        if (cfg.profile == BusProfile::LEGACY_CAN_CLASSIC) {
            snprintf(cmd, sizeof(cmd),
                     "ip link set %s type can bitrate %u",
                     iface.c_str(), cfg.arb_bitrate);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "ip link set %s type can bitrate %u dbitrate %u fd on",
                     iface.c_str(), cfg.arb_bitrate, cfg.data_bitrate);
        }
        printf("[SDV-BM] CMD: %s\n", cmd);
        // system(cmd);  // Uncomment in production
    }

    std::map<std::string, BusConfig>   profiles_;
    std::map<std::string, BusConfig>   active_configs_;
    ReconfigCallback reconfig_cb_;
};

int main() {
    SdvBusManager mgr;

    mgr.register_reconfig_callback([](const std::string& iface,
                                       const BusConfig& cfg) {
        std::cout << "[CB] Bus '" << iface
                  << "' reconfigured to profile "
                  << (int)cfg.profile << "\n";
    });

    mgr.apply_profile("can0", "canfd");
    mgr.apply_profile("canxl0", "canxl_et");

    return 0;
}
```

### OTA-aware Bus Manager in Rust

```rust
// src/sdv_ota_bus_manager.rs — OTA-triggered CAN bus reconfiguration
use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq)]
pub enum BusProfile {
    CanClassic,
    CanFd,
    CanXlHighSpeed,
    CanXlEthTunnel,
}

#[derive(Debug, Clone)]
pub struct BusConfig {
    pub profile:      BusProfile,
    pub arb_bitrate:  u32,
    pub data_bitrate: u32,
    pub vcid:         u8,
    pub eth_tunnel:   bool,
}

#[derive(Debug)]
pub struct OtaBusManager {
    profiles: HashMap<String, BusConfig>,
    active:   HashMap<String, BusConfig>,
}

impl OtaBusManager {
    pub fn new() -> Self {
        let mut profiles = HashMap::new();

        profiles.insert("legacy".into(), BusConfig {
            profile: BusProfile::CanClassic,
            arb_bitrate: 500_000, data_bitrate: 0, vcid: 0x00, eth_tunnel: false,
        });
        profiles.insert("canfd".into(), BusConfig {
            profile: BusProfile::CanFd,
            arb_bitrate: 500_000, data_bitrate: 2_000_000, vcid: 0x00, eth_tunnel: false,
        });
        profiles.insert("canxl_hs".into(), BusConfig {
            profile: BusProfile::CanXlHighSpeed,
            arb_bitrate: 500_000, data_bitrate: 10_000_000, vcid: 0x01, eth_tunnel: false,
        });
        profiles.insert("canxl_et".into(), BusConfig {
            profile: BusProfile::CanXlEthTunnel,
            arb_bitrate: 500_000, data_bitrate: 20_000_000, vcid: 0x02, eth_tunnel: true,
        });

        Self { profiles, active: HashMap::new() }
    }

    /// Apply an OTA-delivered profile manifest to a bus interface.
    pub fn apply_ota_manifest(&mut self, manifest: &OtaManifest) -> Result<(), String> {
        for (iface, profile_name) in &manifest.bus_profiles {
            let cfg = self.profiles.get(profile_name)
                .ok_or_else(|| format!("Unknown profile: {profile_name}"))?
                .clone();

            println!(
                "[OTA-BM] Applying '{profile_name}' on {iface}: \
                 arb={}bps data={}bps vcid=0x{:02X} eth_tunnel={}",
                cfg.arb_bitrate, cfg.data_bitrate, cfg.vcid, cfg.eth_tunnel
            );

            self.apply_to_kernel(iface, &cfg);
            self.active.insert(iface.clone(), cfg);
        }
        Ok(())
    }

    fn apply_to_kernel(&self, iface: &str, cfg: &BusConfig) {
        let cmd = if cfg.profile == BusProfile::CanClassic {
            format!("ip link set {iface} type can bitrate {}", cfg.arb_bitrate)
        } else {
            format!(
                "ip link set {iface} type can bitrate {} dbitrate {} fd on",
                cfg.arb_bitrate, cfg.data_bitrate
            )
        };
        println!("[OTA-BM] CMD: {cmd}");
        // std::process::Command::new("sh").arg("-c").arg(&cmd).status().ok();
    }

    pub fn get_active(&self, iface: &str) -> Option<&BusConfig> {
        self.active.get(iface)
    }
}

/// Simulates an OTA manifest delivered from the cloud
pub struct OtaManifest {
    pub version:      String,
    pub bus_profiles: HashMap<String, String>,
}

fn main() {
    let mut mgr = OtaBusManager::new();

    let manifest = OtaManifest {
        version: "2.3.1".into(),
        bus_profiles: [
            ("can0".into(),   "canfd".into()),
            ("canxl0".into(), "canxl_et".into()),
        ].into(),
    };

    match mgr.apply_ota_manifest(&manifest) {
        Ok(_)  => println!("[OTA] Manifest v{} applied successfully", manifest.version),
        Err(e) => eprintln!("[OTA] Error: {e}"),
    }

    if let Some(cfg) = mgr.get_active("canxl0") {
        println!("[OTA] canxl0 active profile: {:?}", cfg.profile);
    }
}
```

---

## Security in Future CAN Networks

CAN XL's larger payload enables proper **message authentication codes (MACs)** and **SecOC (Secure Onboard Communication)** as defined in AUTOSAR, overcoming CAN FD's tight 64-byte limit. Security in next-gen CAN involves:

- **SecOC** — Appending a truncated MAC (typically 24–48 bits) and a freshness counter to each message.
- **Symmetric key management** — Pre-shared keys per message, rotated via OTA.
- **Anomaly detection** — Monitoring bus traffic patterns for intrusion detection.

### MAC Authentication for CAN XL in C

```c
// secoc_canxl.c — SecOC-style MAC authentication for CAN XL payloads
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* --- Minimal CMAC-AES128 stub (replace with OpenSSL/mbedTLS in production) --- */
static void hmac_sha256_stub(const uint8_t *key, uint8_t key_len,
                              const uint8_t *data, uint16_t data_len,
                              uint8_t *out_mac, uint8_t mac_len)
{
    /* In production: use mbedtls_md_hmac() or EVP_MAC from OpenSSL */
    /* Here: trivial XOR fold for demonstration only */
    uint8_t full[32] = {0};
    for (int i = 0; i < data_len; i++)
        full[i % 32] ^= data[i] ^ key[i % key_len];
    memcpy(out_mac, full, mac_len);
}

#define SECOC_MAC_LEN       8   /* 64-bit truncated MAC */
#define SECOC_FRESHNESS_LEN 4   /* 32-bit freshness counter */
#define SECOC_OVERHEAD      (SECOC_MAC_LEN + SECOC_FRESHNESS_LEN)

typedef struct {
    uint8_t  key[16];
    uint32_t tx_counter;
    uint32_t rx_counter_min; /* Replay window lower bound */
} SecOcContext;

/**
 * Authenticate and append MAC + freshness to a CAN XL payload.
 * The output buffer must be at least (payload_len + SECOC_OVERHEAD) bytes.
 */
int secoc_protect(SecOcContext *ctx,
                   const uint8_t *payload, uint16_t payload_len,
                   uint8_t *out_buf, uint16_t *out_len)
{
    /* Copy plaintext payload */
    memcpy(out_buf, payload, payload_len);

    /* Append freshness counter (big-endian) */
    uint32_t ctr = ctx->tx_counter++;
    out_buf[payload_len + 0] = (ctr >> 24) & 0xFF;
    out_buf[payload_len + 1] = (ctr >> 16) & 0xFF;
    out_buf[payload_len + 2] = (ctr >>  8) & 0xFF;
    out_buf[payload_len + 3] = (ctr >>  0) & 0xFF;

    /* Compute MAC over (payload || freshness) */
    hmac_sha256_stub(ctx->key, sizeof(ctx->key),
                     out_buf, payload_len + SECOC_FRESHNESS_LEN,
                     out_buf + payload_len + SECOC_FRESHNESS_LEN,
                     SECOC_MAC_LEN);

    *out_len = payload_len + SECOC_OVERHEAD;
    printf("[SecOC] TX protected: payload=%u overhead=%u total=%u ctr=%u\n",
           payload_len, SECOC_OVERHEAD, *out_len, ctr);
    return 0;
}

/**
 * Verify and strip MAC from a received CAN XL payload.
 */
int secoc_verify(SecOcContext *ctx,
                  const uint8_t *buf, uint16_t buf_len,
                  uint8_t *out_payload, uint16_t *out_payload_len)
{
    if (buf_len < SECOC_OVERHEAD) {
        fprintf(stderr, "[SecOC] Buffer too short\n");
        return -1;
    }

    uint16_t payload_len = buf_len - SECOC_OVERHEAD;

    /* Extract freshness counter */
    const uint8_t *fc_ptr = buf + payload_len;
    uint32_t received_ctr = ((uint32_t)fc_ptr[0] << 24)
                          | ((uint32_t)fc_ptr[1] << 16)
                          | ((uint32_t)fc_ptr[2] <<  8)
                          | ((uint32_t)fc_ptr[3]);

    /* Anti-replay check */
    if (received_ctr < ctx->rx_counter_min) {
        fprintf(stderr, "[SecOC] Replay attack! ctr=%u min=%u\n",
                received_ctr, ctx->rx_counter_min);
        return -2;
    }

    /* Recompute MAC */
    uint8_t expected_mac[SECOC_MAC_LEN];
    hmac_sha256_stub(ctx->key, sizeof(ctx->key),
                     buf, payload_len + SECOC_FRESHNESS_LEN,
                     expected_mac, SECOC_MAC_LEN);

    if (memcmp(expected_mac, buf + payload_len + SECOC_FRESHNESS_LEN,
               SECOC_MAC_LEN) != 0)
    {
        fprintf(stderr, "[SecOC] MAC verification FAILED!\n");
        return -3;
    }

    ctx->rx_counter_min = received_ctr + 1;
    memcpy(out_payload, buf, payload_len);
    *out_payload_len = payload_len;

    printf("[SecOC] RX verified OK: payload=%u ctr=%u\n",
           payload_len, received_ctr);
    return 0;
}

int main(void) {
    SecOcContext tx_ctx = { .key = {0x01,0x23,0x45,0x67,0x89,0xAB,
                                    0xCD,0xEF,0xFE,0xDC,0xBA,0x98,
                                    0x76,0x54,0x32,0x10},
                            .tx_counter = 100,
                            .rx_counter_min = 0 };
    SecOcContext rx_ctx;
    memcpy(&rx_ctx, &tx_ctx, sizeof(tx_ctx));
    rx_ctx.tx_counter = 0;
    rx_ctx.rx_counter_min = 99;

    /* Payload: engine control message */
    uint8_t msg[8] = { 0x01, 0x00, 0x1F, 0x40, 0x00, 0x00, 0x00, 0x00 };

    uint8_t  protected_buf[64];
    uint16_t protected_len;
    secoc_protect(&tx_ctx, msg, sizeof(msg), protected_buf, &protected_len);

    uint8_t  plain[64];
    uint16_t plain_len;
    secoc_verify(&rx_ctx, protected_buf, protected_len, plain, &plain_len);

    return 0;
}
```

### SecOC-inspired Authenticator in Rust

```rust
// src/secoc.rs — SecOC message authenticator in Rust
use std::sync::atomic::{AtomicU32, Ordering};

const MAC_LEN: usize       = 8;  // 64-bit truncated MAC
const FRESHNESS_LEN: usize = 4;  // 32-bit counter
const OVERHEAD: usize      = MAC_LEN + FRESHNESS_LEN;

pub struct SecOcContext {
    key: [u8; 16],
    tx_counter: AtomicU32,
    rx_min:     u32,
}

impl SecOcContext {
    pub fn new(key: [u8; 16], initial_counter: u32) -> Self {
        Self {
            key,
            tx_counter: AtomicU32::new(initial_counter),
            rx_min: initial_counter.saturating_sub(1),
        }
    }

    fn compute_mac(key: &[u8; 16], data: &[u8]) -> [u8; MAC_LEN] {
        // Stub: XOR-fold for demo. Use ring/hmac in production.
        let mut mac = [0u8; MAC_LEN];
        for (i, b) in data.iter().enumerate() {
            mac[i % MAC_LEN] ^= b ^ key[i % 16];
        }
        mac
    }

    /// Protect a payload: returns authenticated buffer (payload + freshness + MAC).
    pub fn protect(&self, payload: &[u8]) -> Vec<u8> {
        let counter = self.tx_counter.fetch_add(1, Ordering::SeqCst);
        let mut buf = Vec::with_capacity(payload.len() + OVERHEAD);

        buf.extend_from_slice(payload);
        buf.extend_from_slice(&counter.to_be_bytes());

        // Compute MAC over (payload || freshness)
        let mac = Self::compute_mac(&self.key, &buf);
        buf.extend_from_slice(&mac);

        println!(
            "[SecOC] TX protected: payload={} overhead={OVERHEAD} total={} ctr={counter}",
            payload.len(), buf.len()
        );
        buf
    }

    /// Verify and strip authentication from a received buffer.
    pub fn verify(&mut self, buf: &[u8]) -> Result<Vec<u8>, SecOcError> {
        if buf.len() < OVERHEAD {
            return Err(SecOcError::BufferTooShort(buf.len()));
        }

        let payload_len = buf.len() - OVERHEAD;
        let payload     = &buf[..payload_len];
        let freshness   = &buf[payload_len..payload_len + FRESHNESS_LEN];
        let recv_mac    = &buf[payload_len + FRESHNESS_LEN..];

        let counter = u32::from_be_bytes(freshness.try_into().unwrap());

        if counter < self.rx_min {
            return Err(SecOcError::ReplayAttack { received: counter, minimum: self.rx_min });
        }

        // Recompute MAC over (payload || freshness)
        let auth_data: Vec<u8> = [payload, freshness].concat();
        let expected_mac = Self::compute_mac(&self.key, &auth_data);

        if recv_mac != expected_mac {
            return Err(SecOcError::MacMismatch);
        }

        self.rx_min = counter + 1;
        println!("[SecOC] RX verified OK: payload={payload_len} ctr={counter}");
        Ok(payload.to_vec())
    }
}

#[derive(Debug)]
pub enum SecOcError {
    BufferTooShort(usize),
    ReplayAttack { received: u32, minimum: u32 },
    MacMismatch,
}

fn main() {
    let key = [
        0x01,0x23,0x45,0x67, 0x89,0xAB,0xCD,0xEF,
        0xFE,0xDC,0xBA,0x98, 0x76,0x54,0x32,0x10,
    ];

    let tx_ctx = SecOcContext::new(key, 100);
    let mut rx_ctx = SecOcContext::new(key, 100);
    rx_ctx.rx_min = 99;

    let payload = [0x01u8, 0x00, 0x1F, 0x40, 0x00, 0x00, 0x00, 0x00];
    let protected = tx_ctx.protect(&payload);

    match rx_ctx.verify(&protected) {
        Ok(plain) => println!("[SecOC] Recovered {} bytes: {:?}", plain.len(), &plain[..4]),
        Err(e)    => eprintln!("[SecOC] Error: {e:?}"),
    }
}
```

---

## Diagnostics and Monitoring in Next-Gen CAN

### UDS over CAN XL in C++

```cpp
// uds_canxl.cpp — UDS (ISO 14229) over CAN XL with extended payloads
// CAN XL allows UDS response payloads > 64 bytes without multi-frame ISO-TP
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

#define UDS_SID_READ_DATA_BY_ID    0x22
#define UDS_SID_WRITE_DATA_BY_ID   0x2E
#define UDS_SID_ERASE_MEMORY       0x31
#define UDS_SID_TRANSFER_DATA      0x36
#define UDS_SID_NEGATIVE_RESPONSE  0x7F

#define UDS_NRC_SUB_NOT_SUPPORTED  0x12
#define UDS_NRC_REQUEST_SEQERR     0x24
#define UDS_NRC_UPLOAD_DOWNLOAD_NA 0x70

struct UdsRequest {
    uint8_t  sid;
    uint16_t did;          // Data Identifier
    std::vector<uint8_t> data;
};

struct UdsResponse {
    uint8_t  sid;          // Positive = 0x40 + request SID; Negative = 0x7F
    uint16_t did;
    uint8_t  nrc;          // Negative Response Code (if negative)
    std::vector<uint8_t> data;
};

class UdsServerOverCanXl {
public:
    using ReadHandler  = std::function<std::vector<uint8_t>(uint16_t did)>;
    using WriteHandler = std::function<bool(uint16_t did, const std::vector<uint8_t>&)>;

    void on_read (ReadHandler  h) { read_handler_  = std::move(h); }
    void on_write(WriteHandler h) { write_handler_ = std::move(h); }

    UdsResponse handle(const UdsRequest& req) {
        switch (req.sid) {
        case UDS_SID_READ_DATA_BY_ID:
            return handle_read(req);
        case UDS_SID_WRITE_DATA_BY_ID:
            return handle_write(req);
        default:
            return negative_response(req.sid, UDS_NRC_SUB_NOT_SUPPORTED);
        }
    }

private:
    UdsResponse handle_read(const UdsRequest& req) {
        if (!read_handler_)
            return negative_response(req.sid, UDS_NRC_UPLOAD_DOWNLOAD_NA);

        auto data = read_handler_(req.did);
        printf("[UDS] ReadDataById DID=0x%04X → %zu bytes\n",
               req.did, data.size());
        return { (uint8_t)(0x40 + req.sid), req.did, 0x00, data };
    }

    UdsResponse handle_write(const UdsRequest& req) {
        if (!write_handler_)
            return negative_response(req.sid, UDS_NRC_UPLOAD_DOWNLOAD_NA);

        bool ok = write_handler_(req.did, req.data);
        printf("[UDS] WriteDataById DID=0x%04X  %zu bytes → %s\n",
               req.did, req.data.size(), ok ? "OK" : "FAIL");

        if (!ok) return negative_response(req.sid, UDS_NRC_REQUEST_SEQERR);
        return { (uint8_t)(0x40 + req.sid), req.did, 0x00, {} };
    }

    static UdsResponse negative_response(uint8_t sid, uint8_t nrc) {
        printf("[UDS] NegativeResponse SID=0x%02X NRC=0x%02X\n", sid, nrc);
        return { UDS_SID_NEGATIVE_RESPONSE, 0x0000, nrc, {} };
    }

    ReadHandler  read_handler_;
    WriteHandler write_handler_;
};

int main() {
    UdsServerOverCanXl uds;

    // Register a 512-byte ECU calibration record (possible in CAN XL, impossible in CAN FD)
    uds.on_read([](uint16_t did) -> std::vector<uint8_t> {
        if (did == 0xF190) {  // VIN
            return { 'W','B','A','1','2','3','4','5','6','7','8','9','0','1','2','3','4' };
        }
        if (did == 0xD001) {  // Large calibration block (512 bytes) — requires CAN XL
            return std::vector<uint8_t>(512, 0xCC);
        }
        return {};
    });

    uds.on_write([](uint16_t did, const std::vector<uint8_t>& data) {
        printf("[UDS] Writing %zu bytes to DID=0x%04X\n", data.size(), did);
        return true;
    });

    UdsRequest req_vin  { UDS_SID_READ_DATA_BY_ID, 0xF190, {} };
    UdsRequest req_cal  { UDS_SID_READ_DATA_BY_ID, 0xD001, {} };
    UdsRequest req_write{ UDS_SID_WRITE_DATA_BY_ID,0xD001,
                          std::vector<uint8_t>(128, 0xAB) };

    auto r1 = uds.handle(req_vin);
    printf("  VIN response: %zu bytes, SID=0x%02X\n", r1.data.size(), r1.sid);

    auto r2 = uds.handle(req_cal);
    printf("  Cal response: %zu bytes\n", r2.data.size());

    auto r3 = uds.handle(req_write);
    printf("  Write response SID=0x%02X\n", r3.sid);

    return 0;
}
```

### Bus Health Monitor in Rust

```rust
// src/bus_health_monitor.rs — CAN bus anomaly detection
use std::collections::VecDeque;
use std::time::{Duration, Instant};

const WINDOW_SIZE:      usize    = 100;
const ERROR_THRESHOLD:  f32      = 0.05;   // 5% error rate triggers alert
const LOAD_THRESHOLD:   f32      = 0.85;   // 85% bus load triggers alert
const MAX_BITRATE_BPS:  u64      = 20_000_000; // CAN XL max

#[derive(Debug, Default, Clone)]
pub struct FrameStats {
    pub total_frames:   u64,
    pub error_frames:   u64,
    pub total_bits:     u64,
    pub window_start:   Option<Instant>,
}

#[derive(Debug)]
pub enum HealthAlert {
    HighErrorRate { rate: f32 },
    HighBusLoad   { load_percent: f32 },
    BusOff,
    ErrorPassive,
}

pub struct BusHealthMonitor {
    stats:          FrameStats,
    error_window:   VecDeque<bool>,    // true = error frame
    bit_rate_window: VecDeque<(Instant, u64)>, // (time, bits)
}

impl BusHealthMonitor {
    pub fn new() -> Self {
        Self {
            stats:          FrameStats::default(),
            error_window:   VecDeque::with_capacity(WINDOW_SIZE),
            bit_rate_window: VecDeque::new(),
        }
    }

    /// Record a received frame.
    pub fn record_frame(&mut self, is_error: bool, bits: u64) -> Vec<HealthAlert> {
        self.stats.total_frames += 1;
        if is_error { self.stats.error_frames += 1; }
        self.stats.total_bits += bits;

        // Sliding window error tracking
        if self.error_window.len() >= WINDOW_SIZE {
            self.error_window.pop_front();
        }
        self.error_window.push_back(is_error);

        // Bit rate tracking
        let now = Instant::now();
        self.bit_rate_window.push_back((now, bits));
        // Keep only last 100ms window
        let cutoff = now - Duration::from_millis(100);
        while self.bit_rate_window.front()
            .map(|(t, _)| *t < cutoff).unwrap_or(false)
        {
            self.bit_rate_window.pop_front();
        }

        self.evaluate_health()
    }

    fn evaluate_health(&self) -> Vec<HealthAlert> {
        let mut alerts = Vec::new();

        // Error rate check
        let errors: usize = self.error_window.iter().filter(|&&e| e).count();
        let err_rate = errors as f32 / self.error_window.len().max(1) as f32;
        if err_rate > ERROR_THRESHOLD {
            alerts.push(HealthAlert::HighErrorRate { rate: err_rate });
        }

        // Bus load check (bits in last 100ms vs. max capacity)
        let window_bits: u64 = self.bit_rate_window.iter().map(|(_, b)| b).sum();
        let max_bits_100ms = MAX_BITRATE_BPS / 10; // 100ms window
        let load = window_bits as f32 / max_bits_100ms as f32;
        if load > LOAD_THRESHOLD {
            alerts.push(HealthAlert::HighBusLoad { load_percent: load * 100.0 });
        }

        alerts
    }

    pub fn report(&self) {
        let err_rate = self.stats.error_frames as f32
            / self.stats.total_frames.max(1) as f32 * 100.0;

        println!(
            "[BUS HEALTH] Frames={} Errors={} ErrRate={:.2}% TotalBits={}",
            self.stats.total_frames,
            self.stats.error_frames,
            err_rate,
            self.stats.total_bits,
        );
    }
}

fn main() {
    let mut monitor = BusHealthMonitor::new();

    // Simulate normal traffic
    for i in 0..90 {
        let alerts = monitor.record_frame(false, 200); // 200-bit frames
        if !alerts.is_empty() {
            println!("[Frame {i}] Alerts: {alerts:?}");
        }
    }

    // Inject errors
    for i in 0..20 {
        let alerts = monitor.record_frame(true, 0);
        if !alerts.is_empty() {
            println!("[Error {i}] ALERT: {alerts:?}");
        }
    }

    monitor.report();
}
```

---

## Migration Strategy

### From CAN FD to CAN XL

The migration from CAN FD to CAN XL is designed to be gradual and backward-compatible:

1. **Phase 1 — Parallel operation:** CAN XL transceivers support CAN FD and CAN Classic frames. No network topology change required.
2. **Phase 2 — VCID adoption:** Introduce virtual channels for new services while keeping legacy CAN IDs intact.
3. **Phase 3 — Extended payloads:** Migrate large data transfers (calibration, OTA segments, diagnostics) to CAN XL frames with 512–2048 byte payloads.
4. **Phase 4 — Ethernet tunneling:** Enable SDT=0x01 tunneling to bridge Ethernet services over CAN XL segments, removing the need for dedicated Ethernet wiring in low-cost zones.

### Coexistence Layer in C

```c
// coexistence.c — Transparent CAN FD <-> CAN XL bridge
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MAX_CANFD_PAYLOAD  64
#define MAX_CANXL_PAYLOAD  2048

typedef enum { FRAME_CLASSIC, FRAME_FD, FRAME_XL } CanFrameType;

typedef struct {
    CanFrameType type;
    uint32_t can_id;
    uint8_t  vcid;
    uint8_t  sdt;
    uint32_t af;
    uint16_t len;
    uint8_t  data[MAX_CANXL_PAYLOAD];
} UnifiedCanFrame;

/**
 * Promote a CAN FD frame to CAN XL for passage through a CAN XL segment.
 * The original CAN FD data is embedded with SDT=0x00 (raw).
 */
static UnifiedCanFrame promote_fd_to_xl(const UnifiedCanFrame *fd_frame,
                                         uint8_t vcid)
{
    UnifiedCanFrame xl = *fd_frame;
    xl.type = FRAME_XL;
    xl.vcid = vcid;
    xl.sdt  = 0x00;  /* Raw CAN FD payload */
    /* AF carries original DLC to allow demoting at the other end */
    xl.af   = (uint32_t)fd_frame->len;

    printf("[BRIDGE] Promote CAN FD → CAN XL: ID=0x%03X DLC=%u VCID=%02X\n",
           fd_frame->can_id, fd_frame->len, vcid);
    return xl;
}

/**
 * Demote a CAN XL frame back to CAN FD after crossing the XL segment.
 */
static UnifiedCanFrame demote_xl_to_fd(const UnifiedCanFrame *xl_frame)
{
    if (xl_frame->sdt != 0x00) {
        printf("[BRIDGE] Cannot demote SDT=0x%02X to CAN FD\n", xl_frame->sdt);
        return *xl_frame; /* Pass through unchanged */
    }

    UnifiedCanFrame fd = *xl_frame;
    fd.type = FRAME_FD;
    fd.len  = (uint16_t)(xl_frame->af & 0xFF); /* Recover original DLC */

    if (fd.len > MAX_CANFD_PAYLOAD) {
        printf("[BRIDGE] Warning: payload %u > CAN FD max, truncating\n", fd.len);
        fd.len = MAX_CANFD_PAYLOAD;
    }

    printf("[BRIDGE] Demote CAN XL → CAN FD: ID=0x%03X DLC=%u\n",
           fd.can_id, fd.len);
    return fd;
}

int main(void) {
    /* Simulate CAN FD frame arriving from legacy segment */
    UnifiedCanFrame fd_in = {
        .type   = FRAME_FD,
        .can_id = 0x1A0,
        .vcid   = 0x00,
        .sdt    = 0x00,
        .af     = 0,
        .len    = 32,
        .data   = { [0 ... 31] = 0xAB }
    };

    /* Promote to XL for backbone traversal */
    UnifiedCanFrame xl = promote_fd_to_xl(&fd_in, 0x03);

    /* Demote back to FD at destination zone gateway */
    UnifiedCanFrame fd_out = demote_xl_to_fd(&xl);

    printf("[BRIDGE] Final: type=%d ID=0x%03X DLC=%u\n",
           fd_out.type, fd_out.can_id, fd_out.len);
    return 0;
}
```

---

## Summary

The future of CAN technology is defined by three converging forces: **greater bandwidth**, **seamless IP integration**, and **software-defined adaptability**.

**CAN XL** — the most significant revision to the CAN standard since CAN FD — addresses the bandwidth and payload limitations of previous generations. With up to 20 Mbit/s and 2048-byte payloads, it fills the gap between CAN FD and Automotive Ethernet, enabling use cases like direct Ethernet frame tunneling (SDT=0x01), large diagnostic transfers, and AUTOSAR PDU routing without ISO-TP segmentation.

**Ethernet backbone integration** turns CAN from an isolated bus into a participant in a converged vehicle network. Zonal gateway architectures use CAN XL as the domain-level network, bridging seamlessly into 1000BASE-T1 Ethernet backbones via PDU routers and SOME/IP translators. This enables service-oriented communication patterns across both CAN and IP domains.

**Software-defined vehicles** demand that the bus itself become configurable at runtime. VCID-based virtual channels allow multiple logical networks to share one physical CAN XL bus. OTA-triggered bus profile changes, SecOC message authentication, and integrated health monitoring complete the picture of a modern, secure, adaptive vehicle network.

From an engineering perspective, the programming model for CAN XL builds directly on SocketCAN (Linux 6.2+) with extensions for the `canxl_frame` structure, VCID handling, and SDT dispatch. In Rust, async-first patterns (Tokio + `AsyncFd`) combine well with zero-cost abstraction to produce safe, high-performance gateway and monitor implementations. In C/C++, the existing SocketCAN API provides a natural upgrade path from CAN FD without architectural disruption.

The migration from legacy CAN to CAN XL is incremental: coexistence of all three frame types (Classic, FD, XL) on the same transceiver means vehicles can adopt CAN XL features gradually, per domain, per OTA release — exactly as the software-defined vehicle paradigm demands.

---

## Quick Reference Table

| Aspect | CAN Classic | CAN FD | CAN XL | Automotive Ethernet |
|--------|------------|--------|--------|---------------------|
| Max speed | 1 Mbit/s | 8 Mbit/s | 20 Mbit/s | 1000 Mbit/s |
| Max payload | 8 bytes | 64 bytes | 2048 bytes | 1500 bytes (Ethernet MTU) |
| Virtual channels | No | No | Yes (VCID) | Yes (VLAN) |
| ETH tunneling | No | No | Yes (SDT=0x01) | Native |
| SecOC support | Limited | Limited | Native | Via TLS/DTLS |
| OTA reconfiguration | No | No | Yes | Yes |
| Linux kernel support | 3.6+ | 4.8+ | 6.2+ | All |
| Rust SocketCAN crate | `socketcan` | `socketcan` | Raw libc | `tokio`/`async-std` |
| ISO standard | 11898-1 (2003) | 11898-1 (2015) | 11898-1 (2024) | IEEE 802.3 |
| Typical use case | Body, HVAC | Powertrain | Zonal GW, SDV | ADAS, Infotainment |

---

*Document: 100_Future_of_CAN_Technology.md | Series: CAN Bus Programming Reference*