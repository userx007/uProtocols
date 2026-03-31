# 83. Time-Sensitive Networking (TSN)

**Architecture & Standards** — A detailed breakdown of all seven relevant IEEE 802.1 TSN standards (AS, Qbv, Qav, Qci, CB, Qbu, Qch) and a layered ASCII architecture diagram showing how CAN segments connect through a bridge node into a TSN switch backbone.

**CAN–TSN Frame Encapsulation** — Three encapsulation strategies are compared (CiA 603/UDP, AUTOSAR SoAd, Raw L2), with the full packed wire-format struct for the custom EtherType approach including a gPTP timestamp field.

**C/C++ Code Examples:**
- `can_receiver.c` — SocketCAN socket with `SO_TIMESTAMPING` / `recvmsg` to capture hardware PHC timestamps
- `tsn_encap.c` — Builds a complete 802.1Q-tagged raw Ethernet frame from a CAN frame + timestamp
- `tsn_configure.c` — Programmatically invokes `tc taprio` to set up a 4-slot TAS Gate Control List over a 1 ms cycle
- `gptp_clock.c` — Reads the PHC via `PTP_SYS_OFFSET_PRECISE` ioctl with CLOCK_TAI fallback
- `can_tsn_bridge.c` — Full bridge loop with `SO_TXTIME` / `SCM_TXTIME` scheduled transmission

**Rust Code Examples:**
- `can_receiver.rs` — Raw `libc` FFI SocketCAN receiver with cmsg timestamp extraction
- `tsn_encap.rs` — Idiomatic Rust frame builder with CAN ID → PCP priority mapping
- `gptp_clock.rs` — PHC reader using `PTP_SYS_OFFSET_PRECISE` ioctl
- `tsn_tx.rs` — `AF_PACKET` socket with `SO_TXTIME`/`SCM_TXTIME` for scheduled delivery
- `main.rs` — Full async Tokio bridge with `spawn_blocking` for the CAN RX loop

**Error Handling** — CAN bus-off recovery, `MSG_ERRQUEUE` polling for missed TXTIME deadlines, and FRER/HSR redundancy configuration.

### Bridging CAN to IEEE 802.1 TSN Networks for Deterministic Real-Time Communication

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [TSN Standards Overview](#2-tsn-standards-overview)
3. [Why Bridge CAN to TSN?](#3-why-bridge-can-to-tsn)
4. [Architecture: CAN–TSN Bridge](#4-architecture-cantSN-bridge)
5. [Key TSN Mechanisms Relevant to CAN Bridging](#5-key-tsn-mechanisms-relevant-to-can-bridging)
   - 5.1 [IEEE 802.1Qbv — Time-Aware Shaper (TAS)](#51-ieee-8021qbv--time-aware-shaper-tas)
   - 5.2 [IEEE 802.1Qav — Credit-Based Shaper (CBS)](#52-ieee-8021qav--credit-based-shaper-cbs)
   - 5.3 [IEEE 802.1AS — gPTP Time Synchronization](#53-ieee-8021as--gptp-time-synchronization)
   - 5.4 [IEEE 802.1Qci — Per-Stream Filtering and Policing (PSFP)](#54-ieee-8021qci--per-stream-filtering-and-policing-psfp)
   - 5.5 [IEEE 802.1CB — Frame Replication and Elimination (FRER)](#55-ieee-8021cb--frame-replication-and-elimination-frer)
6. [CAN Frame Encapsulation in TSN](#6-can-frame-encapsulation-in-tsn)
7. [Time Synchronization: Aligning CAN and TSN Clocks](#7-time-synchronization-aligning-can-and-tsn-clocks)
8. [Programming the CAN–TSN Bridge in C/C++](#8-programming-the-cantSN-bridge-in-cc)
   - 8.1 [Reading CAN Frames via SocketCAN](#81-reading-can-frames-via-socketcan)
   - 8.2 [Encapsulating CAN into Ethernet/TSN Frames](#82-encapsulating-can-into-ethernettsn-frames)
   - 8.3 [Configuring TSN Queues via Linux TC (Traffic Control)](#83-configuring-tsn-queues-via-linux-tc-traffic-control)
   - 8.4 [gPTP Clock Synchronization Integration](#84-gptp-clock-synchronization-integration)
   - 8.5 [Full CAN-to-TSN Bridge Loop](#85-full-can-to-tsn-bridge-loop)
9. [Programming the CAN–TSN Bridge in Rust](#9-programming-the-cantSN-bridge-in-rust)
   - 9.1 [CAN Frame Reception with socketcan-rs](#91-can-frame-reception-with-socketcan-rs)
   - 9.2 [TSN Frame Encapsulation and Transmission](#92-tsn-frame-encapsulation-and-transmission)
   - 9.3 [gPTP Clock Synchronization in Rust](#93-gptp-clock-synchronization-in-rust)
   - 9.4 [Scheduled Transmission (TAS) via ethtool/netlink in Rust](#94-scheduled-transmission-tas-via-ethtoolnetlink-in-rust)
   - 9.5 [Full Async CAN–TSN Bridge in Rust (Tokio)](#95-full-async-cantSN-bridge-in-rust-tokio)
10. [Error Handling and Fault Tolerance](#10-error-handling-and-fault-tolerance)
11. [Real-World Use Cases](#11-real-world-use-cases)
12. [Summary](#12-summary)
13. [References](#13-references)

---

## 1. Introduction

**Time-Sensitive Networking (TSN)** is a suite of IEEE 802.1 standards that extends standard Ethernet with deterministic, bounded-latency capabilities. TSN addresses the fundamental limitation of classical Ethernet — its non-deterministic "best-effort" delivery — and transforms it into a real-time transport medium suitable for automotive, industrial, aerospace, and professional audio/video applications.

**Controller Area Network (CAN)** has been the dominant in-vehicle and industrial fieldbus for over three decades. It offers robust, priority-based arbitration and excellent noise immunity, but is constrained by limited bandwidth (up to 5 Mbit/s for CAN FD) and a fundamentally serial, shared-medium architecture that does not scale well with the rapidly growing data demands of modern systems.

Bridging CAN to TSN does not replace CAN — it **extends CAN's reach** into high-bandwidth, time-synchronized Ethernet backbone networks while preserving the real-time guarantees that CAN-dependent systems depend on. This is increasingly critical as vehicles migrate to **zonal E/E architectures** and Industry 4.0 factories adopt converged Ethernet-based control networks.

---

## 2. TSN Standards Overview

| Standard | Name | Purpose |
|---|---|---|
| **IEEE 802.1AS** | Generalized Precision Time Protocol (gPTP) | Sub-microsecond clock synchronization across the network |
| **IEEE 802.1Qbv** | Time-Aware Shaper (TAS) | Scheduled gate control for deterministic transmission windows |
| **IEEE 802.1Qav** | Credit-Based Shaper (CBS) | Bandwidth reservation for audio/video streams (AVB) |
| **IEEE 802.1Qci** | Per-Stream Filtering and Policing (PSFP) | Ingress metering, filtering, and policing per stream |
| **IEEE 802.1Qcc** | Stream Reservation Protocol (SRP) Enhancements | Centralized/distributed configuration of TSN streams |
| **IEEE 802.1CB** | Frame Replication and Elimination for Reliability (FRER) | Redundant frame paths for seamless failover |
| **IEEE 802.1Qbu** | Frame Preemption | Allows high-priority frames to interrupt lower-priority transmission |
| **IEEE 802.1Qch** | Cyclic Queuing and Forwarding (CQF) | Leaky-bucket forwarding for bounded end-to-end latency |

For CAN–TSN bridging, the most critical standards are **802.1AS** (clock sync), **802.1Qbv** (scheduled transmission), and **802.1Qci** (stream policing).

---

## 3. Why Bridge CAN to TSN?

### Limitations of Standalone CAN
- **Bandwidth ceiling**: Classical CAN ≤ 1 Mbit/s, CAN FD ≤ 5 Mbit/s — insufficient for sensor fusion, HD maps, ADAS camera streams
- **Segment isolation**: CAN buses are localized; spanning across vehicle zones requires gateways with latency and complexity
- **No time synchronization**: CAN does not inherently provide a global network time base
- **No traffic shaping**: All frames compete on the shared bus using arbitration; there is no concept of scheduled windows

### What TSN Adds
- **Deterministic latency**: Bounded end-to-end delivery times in the microsecond to millisecond range
- **High bandwidth**: Gigabit and multi-gigabit Ethernet transport
- **Global time base**: IEEE 802.1AS provides a synchronized clock across all bridge nodes
- **Convergence**: Time-critical CAN control traffic can coexist with bulk data (diagnostics, logging, OTA updates) on the same physical Ethernet infrastructure

### Target Architectures
- **Automotive zonal architecture**: Domain controllers aggregate multiple CAN/CAN FD segments and bridge to a central TSN backbone
- **Industrial automation**: IEC/IEEE 60802 (TSN for Industrial Automation) requires CAN-connected PLCs to integrate into TSN-based PROFINET/EtherNet/IP networks
- **Aerospace**: ARINC 664/AFDX networks transitioning toward TSN for next-generation avionics

---

## 4. Architecture: CAN–TSN Bridge

```
  CAN Segment A               TSN Ethernet Backbone
  ┌──────────┐                ┌──────────────────────────────────┐
  │ ECU 1    │──┐             │                                  │
  │ ECU 2    │──┤  ┌────────────────────────┐   ┌────────────┐  │
  │ Sensor 1 │──┘  │  CAN–TSN BRIDGE NODE   │   │  TSN Switch│  │
  └──────────┘     │                        │   │            │  │
                   │  ┌─────────┐           │   │  IEEE      │  │
  CAN Segment B    │  │CanSocket│  ┌──────┐ │──▶│  802.1Qbv  │  │
  ┌──────────┐     │  │ (vcan0/ │  │ TSN  │ │   │  802.1AS   │  │
  │ ECU 3    │──┐  │  │  can0)  │─▶│ Enc/ │ │   │  802.1Qci  │  │
  │ ECU 4    │──┤──▶  │         │  │ Sched│ │   └────────────┘  │
  │ Actuator │──┘  │  └─────────┘  └──────┘ │                   │
  └──────────┘     │                        │   ┌────────────┐  │
                   │  gPTP Clock Slave       │   │ Remote     │  │
                   │  (sync to TSN master)   │──▶│ Controller │  │
                   └────────────────────────┘   └────────────┘  │
                                                └──────────────────┘
```

### Bridge Node Responsibilities
1. **Receive** CAN frames from one or more CAN/CAN FD bus segments
2. **Tag** each frame with a reception timestamp (referenced to gPTP global time)
3. **Encapsulate** the CAN frame into an Ethernet payload (e.g., using AUTOSAR PDU or a custom/standardized format like CiA 603)
4. **Classify** the stream and assign a VLAN + priority (PCP) according to the TSN QoS policy
5. **Schedule** transmission during the appropriate TSN gate-open window (TAS)
6. **De-encapsulate** on the receiving end and inject back into the target CAN segment with timing compensation

---

## 5. Key TSN Mechanisms Relevant to CAN Bridging

### 5.1 IEEE 802.1Qbv — Time-Aware Shaper (TAS)

TAS divides time into repeating **hyperperiods** called cycles. Each cycle contains multiple **gate control list (GCL)** entries that open or close transmission gates for up to 8 traffic queues. CAN traffic mapped to a high-priority queue receives a dedicated **guard band** window during which only that queue transmits — eliminating interference from lower-priority traffic.

```
Cycle (e.g., 1 ms)
├─── Gate Open: Queue 7 (CAN safety-critical)  [50 µs]
├─── Gate Open: Queue 5 (CAN control)           [100 µs]
├─── Gate Open: Queue 3 (CAN diagnostics)       [200 µs]
└─── Gate Open: Queue 0 (Best-effort data)      [650 µs]
```

**Guard Band**: A time slot before a high-priority window during which no new lower-priority frame may begin transmission, ensuring the window is not invaded by a frame that started just before the gate switch.

### 5.2 IEEE 802.1Qav — Credit-Based Shaper (CBS)

CBS uses a leaky-bucket mechanism to pace streams. Each queue accumulates **credits** at a defined rate (idleSlope) when idle and consumes credits when transmitting. A frame is held until credits ≥ 0. This prevents burst overload while guaranteeing a sustained throughput allocation — well-suited for periodic CAN traffic encapsulated into TSN streams.

### 5.3 IEEE 802.1AS — gPTP Time Synchronization

gPTP is a profile of IEEE 1588 (PTP) optimized for bridged Ethernet networks. All bridge nodes synchronize to a **grandmaster clock** with typically < 1 µs accuracy across the network. For CAN–TSN bridging, this global clock is essential to:
- Timestamp CAN frame reception accurately
- Schedule TAS windows consistently across bridges
- Enable end-to-end latency calculation and validation

### 5.4 IEEE 802.1Qci — Per-Stream Filtering and Policing (PSFP)

PSFP gates individual streams at ingress: each stream has a **stream filter** (matching on VLAN + destination MAC) associated with a **stream gate** (a TAS-like per-stream gate) and a **flow meter** (rate limiter). This prevents a misbehaving CAN bridge from flooding the TSN network with unexpected traffic.

### 5.5 IEEE 802.1CB — Frame Replication and Elimination (FRER)

FRER duplicates safety-critical frames across two independent paths and eliminates duplicates at the receiver. For CAN frames carrying safety-critical signals (e.g., brake-by-wire commands), FRER provides seamless redundancy with < 1 ms recovery — equivalent to CAN's own error detection but across the TSN domain.

---

## 6. CAN Frame Encapsulation in TSN

There is no single universal standard for encapsulating CAN frames in Ethernet, but the following approaches are used in practice:

### 6.1 CiA 603 (CAN in Automation — CAN over Ethernet)
CiA 603 defines a UDP-based transport (port 17000) for CAN frames with a defined header:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
├─────────────────────────────────────────────────────────────────┤
│  Version  │  Type   │         Channel ID                        │
├─────────────────────────────────────────────────────────────────┤
│                    Sequence Number                              │
├─────────────────────────────────────────────────────────────────┤
│                    Timestamp (nanoseconds)                      │
├─────────────────────────────────────────────────────────────────┤
│  CAN ID (11/29-bit)  │ DLC │ Flags │ Data[0..7]               │
└─────────────────────────────────────────────────────────────────┘
```

### 6.2 AUTOSAR PDU over Ethernet (PduR / SoAd)
AUTOSAR's **Socket Adaptor (SoAd)** and **PDU Router (PduR)** provide a standardized way to transport PDUs (which may carry CAN frames) over UDP/TCP. The PDU header includes a PDU ID and length field.

### 6.3 Raw Layer-2 (Custom EtherType)
For lowest latency, a custom EtherType (e.g., `0x88B5` — IEEE experimental) with a minimal header is used. This avoids UDP/IP overhead, reducing per-frame overhead from ~42 bytes to ~18 bytes.

```c
/* Custom L2 CAN-over-Ethernet frame layout */
struct can_tsn_frame {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint8_t  vlan_tag[4];      /* 802.1Q: PCP, DEI, VID */
    uint16_t ethertype;        /* 0x88B5 or custom */
    uint32_t can_id;           /* 29-bit extended or 11-bit base */
    uint8_t  dlc;
    uint8_t  flags;            /* RTR, EFF, ERR bits */
    uint8_t  padding[2];
    uint64_t timestamp_ns;     /* gPTP timestamp at CAN RX */
    uint8_t  data[8];
} __attribute__((packed));
```

---

## 7. Time Synchronization: Aligning CAN and TSN Clocks

The CAN–TSN bridge must correlate two time domains:

| Domain | Clock Source | Typical Resolution |
|---|---|---|
| CAN hardware timestamp | CAN controller internal timer | 1–10 µs |
| TSN/gPTP | IEEE 802.1AS grandmaster via PHC (PTP Hardware Clock) | < 100 ns |

On Linux, the PHC (PTP Hardware Clock) is accessible via `/dev/ptp0`. The bridge reads the PHC at CAN frame reception using `clock_gettime(CLOCK_TAI)` after synchronizing the system clock to the PHC via `phc2sys`, or directly via `ioctl(PTP_CLOCK_GETCAPS)`.

```
CAN Frame RX
     │
     ▼
Read PHC via clock_gettime(CLOCK_REALTIME) ──▶ gPTP-aligned timestamp
     │
     ▼
Embed timestamp in TSN frame payload
     │
     ▼
Schedule TSN transmission in next gate-open window (TAS)
```

---

## 8. Programming the CAN–TSN Bridge in C/C++

### 8.1 Reading CAN Frames via SocketCAN

```c
/*
 * can_receiver.c
 * Receive CAN frames with hardware timestamps via SocketCAN.
 * Requires Linux kernel with SocketCAN support (CONFIG_CAN_RAW).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/sockios.h>       /* SIOCGSTAMPNS */
#include <linux/net_tstamp.h>    /* SO_TIMESTAMPING */
#include <time.h>

#define CAN_IFACE "can0"

/* Enable hardware RX timestamping on the CAN socket */
static int enable_hw_timestamp(int sock)
{
    int flags = SOF_TIMESTAMPING_RX_HARDWARE |
                SOF_TIMESTAMPING_RAW_HARDWARE |
                SOF_TIMESTAMPING_SOFTWARE;

    if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING,
                   &flags, sizeof(flags)) < 0) {
        perror("SO_TIMESTAMPING");
        return -1;
    }
    return 0;
}

/* Extract hardware timestamp from ancillary data (cmsg) */
static int extract_hw_timestamp(struct msghdr *msg, struct timespec *ts_hw)
{
    struct cmsghdr *cmsg;
    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET &&
            cmsg->cmsg_type  == SO_TIMESTAMPING) {
            /*
             * Three timespec values are returned:
             *   [0] = software timestamp
             *   [1] = (deprecated)
             *   [2] = hardware timestamp (raw)
             */
            struct timespec *stamps = (struct timespec *)CMSG_DATA(cmsg);
            if (stamps[2].tv_sec != 0 || stamps[2].tv_nsec != 0) {
                *ts_hw = stamps[2];   /* hardware (PHC) timestamp */
                return 0;
            }
            /* fallback to software timestamp */
            *ts_hw = stamps[0];
            return 0;
        }
    }
    return -1;  /* no timestamp found */
}

int open_can_socket(const char *iface)
{
    struct sockaddr_can addr;
    struct ifreq ifr;
    int sock;

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket(PF_CAN)");
        return -1;
    }

    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
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

    /* Enable all CAN frame types including error frames */
    can_err_mask_t err_mask = CAN_ERR_MASK;
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

    if (enable_hw_timestamp(sock) < 0) {
        fprintf(stderr, "Warning: HW timestamp not available, using SW\n");
    }

    return sock;
}

/* Receive one CAN frame with its hardware timestamp */
int receive_can_frame(int sock, struct can_frame *frame, struct timespec *ts_hw)
{
    uint8_t ctrl_buf[256];
    struct iovec iov = { .iov_base = frame, .iov_len = sizeof(*frame) };
    struct msghdr msg = {
        .msg_iov        = &iov,
        .msg_iovlen     = 1,
        .msg_control    = ctrl_buf,
        .msg_controllen = sizeof(ctrl_buf),
    };

    ssize_t nbytes = recvmsg(sock, &msg, 0);
    if (nbytes < 0) {
        perror("recvmsg");
        return -1;
    }
    if (nbytes < (ssize_t)sizeof(*frame)) {
        fprintf(stderr, "Incomplete CAN frame\n");
        return -1;
    }

    if (extract_hw_timestamp(&msg, ts_hw) < 0) {
        /* Fallback: use CLOCK_TAI as approximation */
        clock_gettime(CLOCK_TAI, ts_hw);
    }

    return 0;
}

int main(void)
{
    int can_sock = open_can_socket(CAN_IFACE);
    if (can_sock < 0) return EXIT_FAILURE;

    printf("Listening on %s ...\n", CAN_IFACE);

    for (;;) {
        struct can_frame frame;
        struct timespec  ts_hw;

        if (receive_can_frame(can_sock, &frame, &ts_hw) < 0) continue;

        /* Filter error frames */
        if (frame.can_id & CAN_ERR_FLAG) {
            fprintf(stderr, "CAN error frame: 0x%08X\n", frame.can_id);
            continue;
        }

        uint32_t can_id = frame.can_id & CAN_EFF_MASK;
        int      is_ext = !!(frame.can_id & CAN_EFF_FLAG);

        printf("[%ld.%09ld] CAN ID=0x%0*X DLC=%u Data:",
               ts_hw.tv_sec, ts_hw.tv_nsec,
               is_ext ? 8 : 3, can_id,
               frame.can_dlc);

        for (int i = 0; i < frame.can_dlc; i++)
            printf(" %02X", frame.data[i]);
        printf("\n");
    }

    close(can_sock);
    return EXIT_SUCCESS;
}
```

---

### 8.2 Encapsulating CAN into Ethernet/TSN Frames

```c
/*
 * tsn_encap.h / tsn_encap.c
 * Encapsulate a CAN frame into a raw Ethernet frame for TSN transport.
 * Uses a custom EtherType 0x88B5 with an 802.1Q VLAN tag.
 */
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/can.h>
#include <time.h>

/* ---- Wire format ---- */
#define CAN_TSN_ETHERTYPE    0x88B5U
#define CAN_TSN_VLAN_ID      100          /* VLAN 100 for CAN traffic   */
#define CAN_TSN_PCP_CRITICAL 6            /* PCP 6 = safety-critical    */
#define CAN_TSN_PCP_CONTROL  4            /* PCP 4 = control data       */

#pragma pack(push, 1)
typedef struct {
    /* Ethernet header */
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    /* 802.1Q VLAN tag (4 bytes) */
    uint16_t tpid;               /* 0x8100 */
    uint16_t tci;                /* PCP(3) | DEI(1) | VID(12) */
    /* EtherType */
    uint16_t ethertype;          /* CAN_TSN_ETHERTYPE */
    /* CAN–TSN payload */
    uint32_t can_id;             /* raw can_id including EFF/RTR/ERR flags */
    uint8_t  dlc;
    uint8_t  bus_channel;        /* source CAN channel (0..N) */
    uint8_t  reserved[2];
    uint64_t timestamp_ns;       /* gPTP-referenced RX timestamp */
    uint8_t  data[8];
} can_tsn_eth_frame_t;
#pragma pack(pop)

#define CAN_TSN_FRAME_LEN sizeof(can_tsn_eth_frame_t)

/*
 * Build a raw Ethernet frame carrying a CAN frame.
 *
 * @param out         Output buffer (must be >= CAN_TSN_FRAME_LEN bytes)
 * @param src_mac     Bridge source MAC address (6 bytes)
 * @param dst_mac     Destination MAC (multicast or unicast, 6 bytes)
 * @param pcp         Priority Code Point (0..7); use CAN_TSN_PCP_* constants
 * @param channel     CAN bus channel index
 * @param cf          Received CAN frame
 * @param ts          gPTP-aligned reception timestamp
 * @return            Frame length in bytes
 */
size_t can_tsn_build_frame(uint8_t *out,
                           const uint8_t *src_mac,
                           const uint8_t *dst_mac,
                           uint8_t pcp,
                           uint8_t channel,
                           const struct can_frame *cf,
                           const struct timespec *ts)
{
    can_tsn_eth_frame_t *f = (can_tsn_eth_frame_t *)out;

    memcpy(f->dst_mac, dst_mac, 6);
    memcpy(f->src_mac, src_mac, 6);

    f->tpid      = htons(0x8100);
    /* TCI: PCP(3 bits) | DEI(1 bit)=0 | VID(12 bits) */
    f->tci       = htons(((uint16_t)(pcp & 0x7) << 13) |
                          (uint16_t)(CAN_TSN_VLAN_ID & 0xFFF));
    f->ethertype = htons(CAN_TSN_ETHERTYPE);

    f->can_id       = htonl(cf->can_id);
    f->dlc          = cf->can_dlc;
    f->bus_channel  = channel;
    f->reserved[0]  = 0;
    f->reserved[1]  = 0;

    /* Convert timespec to nanoseconds since epoch */
    uint64_t ts_ns = (uint64_t)ts->tv_sec * 1000000000ULL +
                     (uint64_t)ts->tv_nsec;
    /* Store in network byte order */
    f->timestamp_ns = htobe64(ts_ns);

    memcpy(f->data, cf->data, cf->can_dlc);
    /* Zero-pad remaining data bytes */
    if (cf->can_dlc < 8)
        memset(f->data + cf->can_dlc, 0, 8 - cf->can_dlc);

    return CAN_TSN_FRAME_LEN;
}
```

---

### 8.3 Configuring TSN Queues via Linux TC (Traffic Control)

The following C code invokes `tc` commands programmatically to configure a **Time-Aware Shaper (TAS)** on the TSN-capable Ethernet interface:

```c
/*
 * tsn_configure.c
 * Configure IEEE 802.1Qbv TAS via Linux Traffic Control (tc qdisc).
 * Requires: iproute2 >= 5.9, kernel >= 5.14, TSN-capable NIC
 *           (e.g., Intel i210, NXP ENETC, TI CPSW).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TSN_IFACE      "eth0"
#define CYCLE_NS       1000000ULL   /* 1 ms cycle */
#define NUM_QUEUES     8

/*
 * Gate Control List entry: (gate_mask, interval_ns)
 * gate_mask bits: bit N = 1 means queue N is open.
 */
typedef struct {
    uint32_t gate_mask;     /* which queues are open */
    uint64_t interval_ns;   /* duration of this slot */
} gcl_entry_t;

static const gcl_entry_t gcl[] = {
    /* Slot 0: Only Q7 (safety-critical CAN) open — 50 µs guard + TX */
    { .gate_mask = 0x80, .interval_ns =  50000 },
    /* Slot 1: Q5 (CAN control data) + Q7 open — 150 µs */
    { .gate_mask = 0xA0, .interval_ns = 150000 },
    /* Slot 2: Q3 (CAN diagnostics) open — 200 µs */
    { .gate_mask = 0x08, .interval_ns = 200000 },
    /* Slot 3: All best-effort queues — remaining 600 µs */
    { .gate_mask = 0x07, .interval_ns = 600000 },
};
#define GCL_LEN  (sizeof(gcl) / sizeof(gcl[0]))

/*
 * Build and execute the 'tc qdisc' command for taprio (TAS).
 * In a production system, use netlink (libnl / libmnl) instead of
 * system() for efficiency and safety.
 */
int configure_tas(const char *iface, uint64_t base_time_ns)
{
    /* Remove any existing qdisc */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "tc qdisc del dev %s root 2>/dev/null; "
             "tc qdisc add dev %s root handle 100: taprio "
             "num_tc %d "
             "map 0 1 2 3 4 5 6 7 "  /* TC i → queue i (identity) */
             "queues 1@0 1@1 1@2 1@3 1@4 1@5 1@6 1@7 "
             "base-time %llu "
             "clockid CLOCK_TAI ",
             iface, iface, NUM_QUEUES,
             (unsigned long long)base_time_ns);

    /* Append GCL entries */
    char sched[2048] = "";
    for (size_t i = 0; i < GCL_LEN; i++) {
        char entry[128];
        snprintf(entry, sizeof(entry),
                 "sched-entry S 0x%02X %llu ",
                 gcl[i].gate_mask,
                 (unsigned long long)gcl[i].interval_ns);
        strncat(sched, entry, sizeof(sched) - strlen(sched) - 1);
    }
    strncat(cmd, sched, sizeof(cmd) - strlen(cmd) - 1);

    /* Use 'txtime-assist' mode when NIC lacks hardware TAS support */
    /* strncat(cmd, "flags 0x1 ", sizeof(cmd) - strlen(cmd) - 1); */

    printf("Executing: %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "tc qdisc configuration failed (ret=%d)\n", ret);
        return -1;
    }
    return 0;
}

/*
 * Map a CAN frame's priority to a TSN traffic queue and PCP.
 * Priority mapping follows AUTOSAR/CANopen conventions:
 *   CAN ID < 0x100   → safety-critical (Q7, PCP=6)
 *   CAN ID < 0x400   → control (Q5, PCP=4)
 *   CAN ID < 0x700   → diagnostics (Q3, PCP=2)
 *   else             → best-effort (Q1, PCP=0)
 */
typedef struct {
    int     queue;
    uint8_t pcp;
} tsn_priority_t;

tsn_priority_t map_can_priority(uint32_t can_id)
{
    can_id &= 0x1FFFFFFF;  /* strip flags */
    if (can_id < 0x100)   return (tsn_priority_t){ 7, 6 };
    if (can_id < 0x400)   return (tsn_priority_t){ 5, 4 };
    if (can_id < 0x700)   return (tsn_priority_t){ 3, 2 };
    return                        (tsn_priority_t){ 1, 0 };
}
```

---

### 8.4 gPTP Clock Synchronization Integration

```c
/*
 * gptp_clock.c
 * Read and use the gPTP-synchronized PHC (PTP Hardware Clock).
 * Assumes ptp4l + phc2sys are running and have disciplined CLOCK_TAI.
 */
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/ptp_clock.h>

#define PHC_DEVICE "/dev/ptp0"

/*
 * Get current gPTP time directly from the PHC.
 * Returns nanoseconds since TAI epoch.
 */
uint64_t phc_get_time_ns(int phc_fd)
{
    struct ptp_clock_time ptc;
    if (ioctl(phc_fd, PTP_CLOCK_GETCAPS, NULL) < 0) {
        /* Try CLOCK_TAI as fallback (if phc2sys is disciplining it) */
        struct timespec ts;
        clock_gettime(CLOCK_TAI, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    }

    /*
     * Use PTP_SYS_OFFSET_PRECISE for cross-timestamp (PHC ↔ system clock)
     * to get a precise PHC sample without an ioctl round-trip.
     */
    struct ptp_sys_offset_precise offset;
    if (ioctl(phc_fd, PTP_SYS_OFFSET_PRECISE, &offset) == 0) {
        return (uint64_t)offset.device.sec  * 1000000000ULL +
                         offset.device.nsec;
    }

    /* Fallback: simple get-time */
    struct ptp_sys_offset sys_off = { .n_samples = 1 };
    ioctl(phc_fd, PTP_SYS_OFFSET, &sys_off);
    ptc = sys_off.ts[1];   /* middle sample = PHC time */
    return (uint64_t)ptc.sec * 1000000000ULL + ptc.nsec;
}

/*
 * Compute the next TAS cycle start time aligned to the gPTP epoch.
 * This ensures all bridges use the same cycle reference.
 */
uint64_t next_cycle_start_ns(int phc_fd, uint64_t cycle_ns)
{
    uint64_t now_ns = phc_get_time_ns(phc_fd);
    /* Align to the next multiple of cycle_ns */
    uint64_t remainder = now_ns % cycle_ns;
    return now_ns + (cycle_ns - remainder) + cycle_ns;  /* +1 extra cycle margin */
}

int open_phc(const char *dev)
{
    int fd = open(dev, O_RDONLY);
    if (fd < 0) perror("open PHC");
    return fd;
}
```

---

### 8.5 Full CAN-to-TSN Bridge Loop

```c
/*
 * can_tsn_bridge.c
 * Complete CAN→TSN bridge: receives CAN frames, timestamps them with
 * gPTP clock, encapsulates into raw Ethernet, and transmits via
 * a SO_TXTIME-enabled socket for TAS-scheduled delivery.
 *
 * Compile:
 *   gcc -O2 -Wall -o can_tsn_bridge can_tsn_bridge.c -lrt
 *
 * Prerequisites:
 *   - SocketCAN (can0) up and running
 *   - TSN NIC (eth0) with taprio qdisc configured
 *   - ptp4l + phc2sys synchronizing CLOCK_TAI to gPTP grandmaster
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/net_tstamp.h>
#include <linux/errqueue.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>

/* --- Configuration --- */
#define CAN_IFACE        "can0"
#define TSN_IFACE        "eth0"
#define CAN_CHANNEL      0
#define CYCLE_NS         1000000ULL      /* 1 ms TAS cycle           */
#define TX_OFFSET_NS     (CYCLE_NS / 4)  /* TX in 2nd quarter-cycle  */

static const uint8_t DST_MAC[6] = { 0x01, 0x80, 0xC2, 0x00, 0x01, 0x00 }; /* TSN multicast */
static uint8_t g_src_mac[6];

/* --- Raw Ethernet socket for TSN TX with SO_TXTIME --- */
static int open_tsn_tx_socket(const char *iface, uint8_t *src_mac_out)
{
    struct ifreq ifr;
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) { perror("AF_PACKET socket"); return -1; }

    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    int ifindex = ifr.ifr_ifindex;

    /* Get MAC address */
    ioctl(sock, SIOCGIFHWADDR, &ifr);
    memcpy(src_mac_out, ifr.ifr_hwaddr.sa_data, 6);

    struct sockaddr_ll sll = {
        .sll_family   = AF_PACKET,
        .sll_ifindex  = ifindex,
        .sll_protocol = htons(ETH_P_ALL),
    };
    if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("bind TSN"); close(sock); return -1;
    }

    /* Enable SO_TXTIME for LaunchTime / ETF qdisc scheduling */
    struct sock_txtime txtime_opt = {
        .clockid  = CLOCK_TAI,
        .flags    = 0,
    };
    if (setsockopt(sock, SOL_SOCKET, SO_TXTIME,
                   &txtime_opt, sizeof(txtime_opt)) < 0) {
        fprintf(stderr, "SO_TXTIME not supported — using best-effort TX\n");
    }

    return sock;
}

/*
 * Transmit with SO_TXTIME: schedule the frame to be sent at `txtime_ns`
 * (TAI nanoseconds). The ETF (Earliest TxTime First) qdisc will release
 * the frame to the hardware at the precise scheduled time.
 */
static int tsn_send_at(int sock, const uint8_t *buf, size_t len,
                       uint64_t txtime_ns)
{
    struct iovec iov = { .iov_base = (void *)buf, .iov_len = len };

    /* Control message buffer for CMSG_SPACE(sizeof(uint64_t)) */
    union {
        char buf[CMSG_SPACE(sizeof(uint64_t))];
        struct cmsghdr align;
    } ctrl;
    memset(&ctrl, 0, sizeof(ctrl));

    struct msghdr msg = {
        .msg_iov        = &iov,
        .msg_iovlen     = 1,
        .msg_control    = ctrl.buf,
        .msg_controllen = sizeof(ctrl.buf),
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type  = SCM_TXTIME;
    cmsg->cmsg_len   = CMSG_LEN(sizeof(uint64_t));
    *((uint64_t *)CMSG_DATA(cmsg)) = txtime_ns;

    ssize_t ret = sendmsg(sock, &msg, 0);
    if (ret < 0) {
        perror("sendmsg (TSN)");
        return -1;
    }
    return 0;
}

/* --- Bridge thread --- */
typedef struct {
    int can_sock;
    int tsn_sock;
} bridge_args_t;

static void *bridge_thread(void *arg)
{
    bridge_args_t *a = (bridge_args_t *)arg;
    struct can_frame  cf;
    struct timespec   ts_hw;
    uint8_t           eth_buf[256];

    while (1) {
        /* 1. Receive CAN frame with gPTP-aligned hardware timestamp */
        if (receive_can_frame(a->can_sock, &cf, &ts_hw) < 0) continue;
        if (cf.can_id & CAN_ERR_FLAG) continue;

        uint64_t rx_ns = (uint64_t)ts_hw.tv_sec * 1000000000ULL + ts_hw.tv_nsec;

        /* 2. Determine TSN priority */
        tsn_priority_t prio = map_can_priority(cf.can_id);

        /* 3. Build Ethernet frame */
        size_t frame_len = can_tsn_build_frame(eth_buf,
                                               g_src_mac,
                                               DST_MAC,
                                               prio.pcp,
                                               CAN_CHANNEL,
                                               &cf,
                                               &ts_hw);

        /* 4. Schedule transmission at next gate-open window.
         *    For Q7 (safety-critical): send in the very next TAS window
         *    starting at the next cycle boundary + TAS slot offset.
         */
        uint64_t cycle_start = (rx_ns / CYCLE_NS + 1) * CYCLE_NS;
        uint64_t txtime_ns   = cycle_start + TX_OFFSET_NS;

        /* 5. Transmit with SO_TXTIME */
        if (tsn_send_at(a->tsn_sock, eth_buf, frame_len, txtime_ns) < 0) {
            fprintf(stderr, "TX failed for CAN ID 0x%X\n",
                    cf.can_id & CAN_EFF_MASK);
        } else {
            printf("Forwarded CAN 0x%X → TSN (txtime=%llu ns)\n",
                   cf.can_id & CAN_EFF_MASK,
                   (unsigned long long)txtime_ns);
        }
    }
    return NULL;
}

int main(void)
{
    /* 1. Open CAN socket */
    int can_sock = open_can_socket(CAN_IFACE);
    if (can_sock < 0) return EXIT_FAILURE;

    /* 2. Open TSN TX socket */
    int tsn_sock = open_tsn_tx_socket(TSN_IFACE, g_src_mac);
    if (tsn_sock < 0) { close(can_sock); return EXIT_FAILURE; }

    /* 3. Configure TAS on TSN interface */
    int phc_fd = open_phc(PHC_DEVICE);
    uint64_t base_time = (phc_fd >= 0)
        ? next_cycle_start_ns(phc_fd, CYCLE_NS)
        : 0;

    configure_tas(TSN_IFACE, base_time);
    if (phc_fd >= 0) close(phc_fd);

    /* 4. Start bridge thread */
    bridge_args_t args = { .can_sock = can_sock, .tsn_sock = tsn_sock };
    pthread_t tid;
    pthread_create(&tid, NULL, bridge_thread, &args);

    printf("CAN→TSN bridge running. Press Ctrl+C to stop.\n");
    pthread_join(tid, NULL);

    close(can_sock);
    close(tsn_sock);
    return EXIT_SUCCESS;
}
```

---

## 9. Programming the CAN–TSN Bridge in Rust

### 9.1 CAN Frame Reception with socketcan-rs

```toml
# Cargo.toml dependencies
[dependencies]
socketcan       = "3.3"
tokio           = { version = "1", features = ["full"] }
tokio-socketcan = "3.0"
nix             = { version = "0.29", features = ["time", "socket", "ioctl"] }
libc            = "0.2"
anyhow          = "1"
tracing         = "0.1"
tracing-subscriber = "0.3"
```

```rust
// src/can_receiver.rs
//! CAN frame reception with hardware timestamps via SocketCAN.

use anyhow::{Context, Result};
use libc::{
    self, AF_CAN, SOCK_RAW, CAN_RAW, SOL_SOCKET, SO_TIMESTAMPING,
    SOF_TIMESTAMPING_RX_HARDWARE, SOF_TIMESTAMPING_RAW_HARDWARE,
    SOF_TIMESTAMPING_SOFTWARE,
    sockaddr_can, ifreq, SIOCGIFINDEX,
};
use std::mem::{size_of, zeroed};
use std::os::unix::io::RawFd;
use std::time::Duration;

/// A received CAN frame with its gPTP-aligned hardware timestamp.
#[derive(Debug, Clone)]
pub struct TimestampedCanFrame {
    pub can_id:    u32,
    pub dlc:       u8,
    pub data:      [u8; 8],
    pub is_ext:    bool,
    pub is_rtr:    bool,
    pub timestamp: std::time::SystemTime,
    pub timestamp_ns: u64,
}

/// Raw CAN frame layout matching Linux `struct can_frame`
#[repr(C)]
struct CanFrame {
    can_id:  u32,
    can_dlc: u8,
    pad:     u8,
    res0:    u8,
    res1:    u8,
    data:    [u8; 8],
}

pub struct CanReceiver {
    fd: RawFd,
}

impl CanReceiver {
    pub fn new(iface: &str) -> Result<Self> {
        // SAFETY: standard socket system calls with validated parameters
        let fd = unsafe {
            let s = libc::socket(AF_CAN, SOCK_RAW, CAN_RAW);
            if s < 0 {
                return Err(anyhow::anyhow!("socket(AF_CAN) failed: {}",
                    std::io::Error::last_os_error()));
            }

            // Resolve interface index
            let mut ifr: ifreq = zeroed();
            let iface_bytes = iface.as_bytes();
            let copy_len = iface_bytes.len().min(libc::IFNAMSIZ - 1);
            std::ptr::copy_nonoverlapping(
                iface_bytes.as_ptr() as *const libc::c_char,
                ifr.ifr_name.as_mut_ptr(),
                copy_len,
            );

            if libc::ioctl(s, SIOCGIFINDEX as libc::c_ulong, &mut ifr) < 0 {
                libc::close(s);
                return Err(anyhow::anyhow!("SIOCGIFINDEX failed: {}",
                    std::io::Error::last_os_error()));
            }

            let mut addr: sockaddr_can = zeroed();
            addr.can_family = AF_CAN as u16;
            // SAFETY: union access for ifr_ifindex
            addr.can_ifindex = ifr.ifr_ifru.ifru_ifindex;

            if libc::bind(s,
                &addr as *const _ as *const libc::sockaddr,
                size_of::<sockaddr_can>() as u32) < 0
            {
                libc::close(s);
                return Err(anyhow::anyhow!("bind(AF_CAN) failed: {}",
                    std::io::Error::last_os_error()));
            }

            // Enable hardware+software timestamping
            let flags: libc::c_int =
                SOF_TIMESTAMPING_RX_HARDWARE  as libc::c_int |
                SOF_TIMESTAMPING_RAW_HARDWARE as libc::c_int |
                SOF_TIMESTAMPING_SOFTWARE     as libc::c_int;

            libc::setsockopt(
                s, SOL_SOCKET, SO_TIMESTAMPING,
                &flags as *const _ as *const libc::c_void,
                size_of::<libc::c_int>() as u32,
            );

            s
        };

        Ok(Self { fd })
    }

    /// Blocking receive of a single CAN frame with hardware timestamp.
    pub fn recv_frame(&self) -> Result<TimestampedCanFrame> {
        let mut frame = CanFrame {
            can_id: 0, can_dlc: 0, pad: 0, res0: 0, res1: 0,
            data: [0u8; 8],
        };

        // Control message buffer for ancillary data (timestamps)
        let mut ctrl_buf = [0u8; 256];

        let mut iov = libc::iovec {
            iov_base: &mut frame as *mut _ as *mut libc::c_void,
            iov_len:  size_of::<CanFrame>(),
        };

        let mut msg: libc::msghdr = unsafe { zeroed() };
        msg.msg_iov        = &mut iov;
        msg.msg_iovlen     = 1;
        msg.msg_control    = ctrl_buf.as_mut_ptr() as *mut libc::c_void;
        msg.msg_controllen = ctrl_buf.len();

        let nbytes = unsafe { libc::recvmsg(self.fd, &mut msg, 0) };
        if nbytes < 0 {
            return Err(anyhow::anyhow!("recvmsg failed: {}",
                std::io::Error::last_os_error()));
        }

        let timestamp_ns = Self::extract_timestamp(&ctrl_buf, msg.msg_controllen);

        const CAN_EFF_FLAG: u32 = 0x80000000;
        const CAN_RTR_FLAG: u32 = 0x40000000;
        const CAN_EFF_MASK: u32 = 0x1FFFFFFF;
        const CAN_SFF_MASK: u32 = 0x000007FF;

        let is_ext = (frame.can_id & CAN_EFF_FLAG) != 0;
        let is_rtr = (frame.can_id & CAN_RTR_FLAG) != 0;
        let can_id = if is_ext {
            frame.can_id & CAN_EFF_MASK
        } else {
            frame.can_id & CAN_SFF_MASK
        };

        Ok(TimestampedCanFrame {
            can_id,
            dlc: frame.can_dlc,
            data: frame.data,
            is_ext,
            is_rtr,
            timestamp: std::time::UNIX_EPOCH
                + Duration::from_nanos(timestamp_ns),
            timestamp_ns,
        })
    }

    /// Extract hardware timestamp from ancillary control message.
    fn extract_timestamp(ctrl_buf: &[u8], controllen: usize) -> u64 {
        // Walk the cmsg chain to find SO_TIMESTAMPING data
        // Layout: [software_ts, (deprecated), hardware_ts] — three timespec64
        let ptr = ctrl_buf.as_ptr() as *const libc::cmsghdr;
        let mut cmsg = ptr;
        let mut remaining = controllen as isize;

        while remaining > 0 {
            let hdr = unsafe { &*cmsg };
            if hdr.cmsg_level == SOL_SOCKET as i32
                && hdr.cmsg_type == SO_TIMESTAMPING as i32
            {
                // Data starts after the cmsghdr
                let data_ptr = unsafe {
                    (cmsg as *const u8).add(
                        ((size_of::<libc::cmsghdr>() + 7) & !7) // CMSG_DATA alignment
                    )
                } as *const libc::timespec;

                // timestamps[2] is the hardware (PHC) timestamp
                let hw_ts = unsafe { &*data_ptr.add(2) };
                if hw_ts.tv_sec > 0 || hw_ts.tv_nsec > 0 {
                    return (hw_ts.tv_sec as u64) * 1_000_000_000
                         + (hw_ts.tv_nsec as u64);
                }
                // fallback to software timestamp
                let sw_ts = unsafe { &*data_ptr };
                return (sw_ts.tv_sec as u64) * 1_000_000_000
                     + (sw_ts.tv_nsec as u64);
            }

            let next_len = ((hdr.cmsg_len + 7) & !7) as isize;
            remaining -= next_len;
            cmsg = unsafe { (cmsg as *const u8).offset(next_len) as *const _ };
        }

        // Final fallback: CLOCK_TAI approximation
        let mut ts = libc::timespec { tv_sec: 0, tv_nsec: 0 };
        unsafe { libc::clock_gettime(libc::CLOCK_TAI, &mut ts) };
        (ts.tv_sec as u64) * 1_000_000_000 + (ts.tv_nsec as u64)
    }
}

impl Drop for CanReceiver {
    fn drop(&mut self) {
        unsafe { libc::close(self.fd) };
    }
}
```

---

### 9.2 TSN Frame Encapsulation and Transmission

```rust
// src/tsn_encap.rs
//! Encapsulate CAN frames into raw Ethernet frames for TSN transport.

use crate::can_receiver::TimestampedCanFrame;

/// Ethernet + 802.1Q + CAN-TSN payload wire format
pub struct CanTsnFrame {
    pub bytes: Vec<u8>,
}

pub const CAN_TSN_ETHERTYPE: u16 = 0x88B5;
pub const TSN_VLAN_ID:       u16 = 100;

/// TSN Priority Code Point values
#[derive(Debug, Clone, Copy)]
pub struct TsnPriority {
    pub queue: u8,
    pub pcp:   u8,
}

impl TsnPriority {
    /// Map CAN ID to TSN priority (queue + PCP).
    pub fn from_can_id(can_id: u32) -> Self {
        match can_id {
            0x000..=0x0FF => TsnPriority { queue: 7, pcp: 6 }, // safety-critical
            0x100..=0x3FF => TsnPriority { queue: 5, pcp: 4 }, // control data
            0x400..=0x6FF => TsnPriority { queue: 3, pcp: 2 }, // diagnostics
            _             => TsnPriority { queue: 1, pcp: 0 }, // best-effort
        }
    }
}

impl CanTsnFrame {
    /// Build a raw Ethernet frame carrying a CAN frame.
    pub fn build(
        src_mac:  &[u8; 6],
        dst_mac:  &[u8; 6],
        channel:  u8,
        frame:    &TimestampedCanFrame,
    ) -> Self {
        let prio = TsnPriority::from_can_id(frame.can_id);
        let mut buf = Vec::with_capacity(64);

        // Ethernet header — destination MAC
        buf.extend_from_slice(dst_mac);
        // Source MAC
        buf.extend_from_slice(src_mac);

        // 802.1Q VLAN tag (TPID = 0x8100)
        buf.extend_from_slice(&0x8100u16.to_be_bytes());
        // TCI: PCP (3 bits) | DEI (1 bit) | VID (12 bits)
        let tci: u16 = ((prio.pcp as u16 & 0x7) << 13) | (TSN_VLAN_ID & 0xFFF);
        buf.extend_from_slice(&tci.to_be_bytes());

        // EtherType
        buf.extend_from_slice(&CAN_TSN_ETHERTYPE.to_be_bytes());

        // CAN ID (with EFF/RTR flags packed back)
        let raw_id = frame.can_id
            | if frame.is_ext { 0x80000000 } else { 0 }
            | if frame.is_rtr { 0x40000000 } else { 0 };
        buf.extend_from_slice(&raw_id.to_be_bytes());

        // DLC, channel, reserved
        buf.push(frame.dlc);
        buf.push(channel);
        buf.extend_from_slice(&[0u8, 0u8]);

        // gPTP-aligned timestamp (nanoseconds, big-endian)
        buf.extend_from_slice(&frame.timestamp_ns.to_be_bytes());

        // CAN data payload (always 8 bytes, zero-padded)
        let mut padded = [0u8; 8];
        let n = (frame.dlc as usize).min(8);
        padded[..n].copy_from_slice(&frame.data[..n]);
        buf.extend_from_slice(&padded);

        // Ethernet minimum frame size is 64 bytes; pad if needed
        while buf.len() < 60 {
            buf.push(0u8);
        }

        CanTsnFrame { bytes: buf }
    }
}
```

---

### 9.3 gPTP Clock Synchronization in Rust

```rust
// src/gptp_clock.rs
//! Interface with the PTP Hardware Clock (PHC) for gPTP time.

use anyhow::{Context, Result};
use std::fs::File;
use std::os::unix::io::AsRawFd;

/// PTP_SYS_OFFSET_PRECISE ioctl number and data structure (Linux)
const PTP_SYS_OFFSET_PRECISE: libc::c_ulong = 0xc0403d09;

#[repr(C)]
struct PtpClockTimePrecise {
    device:    PtpClockTime,
    sys_realtime: PtpClockTime,
    sys_monoraw:  PtpClockTime,
    reserved: [u32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct PtpClockTime {
    sec:  i64,
    nsec: u32,
    reserved: u32,
}

pub struct GptpClock {
    phc_fd: Option<File>,
}

impl GptpClock {
    pub fn new(phc_dev: &str) -> Self {
        let phc_fd = std::fs::OpenOptions::new()
            .read(true)
            .open(phc_dev)
            .ok();
        if phc_fd.is_none() {
            tracing::warn!("PHC device {} not accessible, using CLOCK_TAI", phc_dev);
        }
        GptpClock { phc_fd }
    }

    /// Get current gPTP time in nanoseconds (TAI).
    pub fn now_ns(&self) -> u64 {
        if let Some(ref file) = self.phc_fd {
            let mut precise: PtpClockTimePrecise = unsafe { std::mem::zeroed() };
            let ret = unsafe {
                libc::ioctl(file.as_raw_fd(),
                            PTP_SYS_OFFSET_PRECISE,
                            &mut precise as *mut _)
            };
            if ret == 0 {
                return (precise.device.sec as u64) * 1_000_000_000
                     + (precise.device.nsec as u64);
            }
        }
        // Fallback: CLOCK_TAI (valid if phc2sys is running)
        let mut ts = libc::timespec { tv_sec: 0, tv_nsec: 0 };
        unsafe { libc::clock_gettime(libc::CLOCK_TAI, &mut ts) };
        (ts.tv_sec as u64) * 1_000_000_000 + (ts.tv_nsec as u64)
    }

    /// Compute the start time of the next TAS cycle after now.
    pub fn next_cycle_start(&self, cycle_ns: u64) -> u64 {
        let now = self.now_ns();
        let rem = now % cycle_ns;
        now + (cycle_ns - rem) + cycle_ns  // +1 cycle for transmission margin
    }
}
```

---

### 9.4 Scheduled Transmission (TAS) via ethtool/netlink in Rust

```rust
// src/tsn_tx.rs
//! Raw Ethernet transmission with SO_TXTIME for TSN scheduled delivery.

use anyhow::{Context, Result};
use libc::{self, AF_PACKET, SOCK_RAW, SOL_SOCKET, ETH_P_ALL};
use std::mem::{size_of, zeroed};
use std::os::unix::io::RawFd;

/// SO_TXTIME socket option structure
#[repr(C)]
struct SockTxtime {
    clockid: libc::clockid_t,
    flags:   u32,
}

pub struct TsnTxSocket {
    fd:      RawFd,
    ifindex: libc::c_int,
}

impl TsnTxSocket {
    pub fn new(iface: &str) -> Result<(Self, [u8; 6])> {
        let sock = unsafe {
            libc::socket(AF_PACKET, SOCK_RAW, (ETH_P_ALL as u16).to_be() as i32)
        };
        if sock < 0 {
            anyhow::bail!("AF_PACKET socket: {}", std::io::Error::last_os_error());
        }

        let mut ifr: libc::ifreq = unsafe { zeroed() };
        let iface_bytes = iface.as_bytes();
        unsafe {
            std::ptr::copy_nonoverlapping(
                iface_bytes.as_ptr() as *const libc::c_char,
                ifr.ifr_name.as_mut_ptr(),
                iface_bytes.len().min(libc::IFNAMSIZ - 1),
            );
            libc::ioctl(sock, libc::SIOCGIFINDEX as libc::c_ulong, &mut ifr);
        }
        let ifindex = unsafe { ifr.ifr_ifru.ifru_ifindex };

        // Get MAC address
        unsafe { libc::ioctl(sock, libc::SIOCGIFHWADDR as libc::c_ulong, &mut ifr) };
        let mut mac = [0u8; 6];
        mac.copy_from_slice(unsafe { &ifr.ifr_ifru.ifru_hwaddr.sa_data[..6] }
            .iter().map(|&b| b as u8).collect::<Vec<_>>().as_slice());

        // Bind to interface
        let sll = libc::sockaddr_ll {
            sll_family:   AF_PACKET as u16,
            sll_protocol: (ETH_P_ALL as u16).to_be(),
            sll_ifindex:  ifindex,
            sll_hatype:   0,
            sll_pkttype:  0,
            sll_halen:    0,
            sll_addr:     [0; 8],
        };
        unsafe {
            libc::bind(sock,
                &sll as *const _ as *const libc::sockaddr,
                size_of::<libc::sockaddr_ll>() as u32);
        }

        // Enable SO_TXTIME with CLOCK_TAI
        let txtime_opt = SockTxtime {
            clockid: libc::CLOCK_TAI,
            flags:   0,
        };
        let so_txtime: libc::c_int = 61; // SO_TXTIME = 61 on Linux
        unsafe {
            libc::setsockopt(
                sock, SOL_SOCKET, so_txtime,
                &txtime_opt as *const _ as *const libc::c_void,
                size_of::<SockTxtime>() as u32,
            );
        }

        Ok((TsnTxSocket { fd: sock, ifindex }, mac))
    }

    /// Transmit frame at a specific gPTP time (TAI nanoseconds).
    pub fn send_at(&self, frame: &[u8], txtime_ns: u64) -> Result<()> {
        // Ancillary data buffer for SCM_TXTIME
        const SCM_TXTIME: libc::c_int = 61;
        let mut ctrl = [0u8; unsafe { libc::CMSG_SPACE(8) } as usize];

        let mut iov = libc::iovec {
            iov_base: frame.as_ptr() as *mut libc::c_void,
            iov_len:  frame.len(),
        };

        let mut msg: libc::msghdr = unsafe { zeroed() };
        msg.msg_iov        = &mut iov;
        msg.msg_iovlen     = 1;
        msg.msg_control    = ctrl.as_mut_ptr() as *mut libc::c_void;
        msg.msg_controllen = ctrl.len();

        // Fill in cmsg for SCM_TXTIME
        unsafe {
            let cmsg = libc::CMSG_FIRSTHDR(&msg);
            (*cmsg).cmsg_level = SOL_SOCKET;
            (*cmsg).cmsg_type  = SCM_TXTIME;
            (*cmsg).cmsg_len   = libc::CMSG_LEN(8) as usize;
            let data = libc::CMSG_DATA(cmsg) as *mut u64;
            *data = txtime_ns;
        }

        let ret = unsafe { libc::sendmsg(self.fd, &msg, 0) };
        if ret < 0 {
            anyhow::bail!("sendmsg(SO_TXTIME): {}", std::io::Error::last_os_error());
        }
        Ok(())
    }
}

impl Drop for TsnTxSocket {
    fn drop(&mut self) {
        unsafe { libc::close(self.fd) };
    }
}
```

---

### 9.5 Full Async CAN–TSN Bridge in Rust (Tokio)

```rust
// src/main.rs
//! Async CAN→TSN bridge using Tokio for concurrent frame processing.
//! Multiple CAN channels can be bridged concurrently with separate tasks.

mod can_receiver;
mod tsn_encap;
mod tsn_tx;
mod gptp_clock;

use anyhow::Result;
use std::sync::Arc;
use tokio::task;
use tracing::{info, warn, error};

use can_receiver::CanReceiver;
use tsn_encap::CanTsnFrame;
use tsn_tx::TsnTxSocket;
use gptp_clock::GptpClock;

const CAN_IFACE:   &str = "can0";
const TSN_IFACE:   &str = "eth0";
const PHC_DEVICE:  &str = "/dev/ptp0";
const CYCLE_NS:    u64  = 1_000_000;   // 1 ms TAS cycle
const TX_OFFSET:   u64  = 250_000;     // 250 µs into cycle

/// DST multicast MAC for TSN CAN traffic (IEEE 802.1Q multicast)
const DST_MAC: [u8; 6] = [0x01, 0x80, 0xC2, 0x00, 0x01, 0x00];

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_max_level(tracing::Level::INFO)
        .init();

    info!("Starting CAN→TSN bridge: {} → {}", CAN_IFACE, TSN_IFACE);

    // Open TSN TX socket (shared across tasks)
    let (tsn_sock, src_mac) = TsnTxSocket::new(TSN_IFACE)
        .expect("Failed to open TSN TX socket");
    let tsn_sock = Arc::new(tsn_sock);

    // Open gPTP clock
    let clock = Arc::new(GptpClock::new(PHC_DEVICE));

    info!("Source MAC: {:02X?}", src_mac);

    // Spawn bridge task for CAN channel 0
    let tsn_sock_c = Arc::clone(&tsn_sock);
    let clock_c    = Arc::clone(&clock);

    // Run the blocking CAN receiver in a dedicated thread pool
    task::spawn_blocking(move || {
        bridge_loop(CAN_IFACE, 0, src_mac, &tsn_sock_c, &clock_c)
    }).await??;

    Ok(())
}

fn bridge_loop(
    can_iface: &str,
    channel:   u8,
    src_mac:   [u8; 6],
    tsn_sock:  &TsnTxSocket,
    clock:     &GptpClock,
) -> Result<()> {
    let receiver = CanReceiver::new(can_iface)
        .with_context(|| format!("Opening CAN interface {}", can_iface))?;

    info!("Bridge active: {} (ch={}) → TSN", can_iface, channel);

    loop {
        // 1. Blocking receive with hardware timestamp
        let frame = match receiver.recv_frame() {
            Ok(f)  => f,
            Err(e) => {
                error!("CAN recv error: {}", e);
                continue;
            }
        };

        // 2. Skip error frames
        if frame.can_id & 0x20000000 != 0 {
            warn!("CAN error frame received, skipping");
            continue;
        }

        tracing::debug!(
            can_id = frame.can_id,
            dlc    = frame.dlc,
            ts_ns  = frame.timestamp_ns,
            "RX CAN frame"
        );

        // 3. Encapsulate into TSN Ethernet frame
        let eth_frame = CanTsnFrame::build(&src_mac, &DST_MAC, channel, &frame);

        // 4. Calculate scheduled TX time (next cycle boundary + offset)
        let cycle_start = clock.next_cycle_start(CYCLE_NS);
        let txtime_ns   = cycle_start + TX_OFFSET;

        // 5. Transmit with SO_TXTIME
        if let Err(e) = tsn_sock.send_at(&eth_frame.bytes, txtime_ns) {
            error!("TSN TX failed for CAN ID 0x{:X}: {}", frame.can_id, e);
        } else {
            info!(
                can_id   = frame.can_id,
                txtime   = txtime_ns,
                dlc      = frame.dlc,
                "Forwarded CAN→TSN"
            );
        }
    }
}
```

---

## 10. Error Handling and Fault Tolerance

### 10.1 CAN Bus Errors

```c
/* Detect and handle CAN bus-off condition */
void handle_can_error(const struct can_frame *frame)
{
    if (!(frame->can_id & CAN_ERR_FLAG)) return;

    if (frame->can_id & CAN_ERR_BUSOFF) {
        fprintf(stderr, "CAN bus-off detected — initiating recovery\n");
        /* Recovery: take interface down, back up */
        system("ip link set can0 down && ip link set can0 up");
    }
    if (frame->can_id & CAN_ERR_CRTL) {
        fprintf(stderr, "CAN controller error: RX/TX overflow\n");
    }
    if (frame->can_id & CAN_ERR_PROT) {
        fprintf(stderr, "CAN protocol violation: %02X\n", frame->data[2]);
    }
}
```

### 10.2 TSN Transmission Errors

When `SO_TXTIME` frames miss their scheduled window, the kernel reports an error via the socket error queue. The bridge must poll this queue to detect and handle late frames:

```c
/* Poll SO_TXTIME error queue for missed deadlines */
void check_tx_errors(int tsn_sock)
{
    uint8_t ctrl[256];
    uint8_t data[64];
    struct iovec iov = { data, sizeof(data) };
    struct msghdr msg = {
        .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = ctrl, .msg_controllen = sizeof(ctrl)
    };

    while (recvmsg(tsn_sock, &msg, MSG_ERRQUEUE | MSG_DONTWAIT) > 0) {
        struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
        if (cm && cm->cmsg_type == SO_TIMESTAMPING) {
            struct sock_extended_err *serr =
                (struct sock_extended_err *)CMSG_DATA(cm);
            if (serr->ee_origin == SO_EE_ORIGIN_TXTIME_MISSED) {
                fprintf(stderr,
                    "TXTIME MISSED: scheduled=%llu, actual delta=%u ns\n",
                    (unsigned long long)serr->ee_data,
                    serr->ee_info);
            }
        }
    }
}
```

### 10.3 FRER Redundancy (IEEE 802.1CB)

For safety-critical CAN signals (e.g., brake-by-wire, steer-by-wire), configure FRER to send duplicate frames on two independent TSN paths. Linux supports FRER via the `hsr` driver combined with `ip link add type hsr`:

```bash
# Create HSR (High-availability Seamless Redundancy) interface
# which implements FRER-like duplicate elimination
ip link add name hsr0 type hsr slave1 eth0 slave2 eth1 supervision 45 version 1
ip link set hsr0 up
```

---

## 11. Real-World Use Cases

### 11.1 Automotive Zonal Architecture (AUTOSAR Adaptive)
Modern vehicles use a **zonal ECU** topology where multiple CAN segments from body, chassis, and powertrain are aggregated into a central high-speed TSN backbone. The bridge node runs AUTOSAR Adaptive Platform services (`ara::com`) that publish CAN signal data as service-oriented interfaces over TSN.

### 11.2 Industrial Automation (IEC/IEEE 60802 TSN-IA)
Factory control systems bridge **CANopen** nodes (motors, sensors, I/O modules) into an IEC/IEEE 60802 TSN network running PROFINET or EtherNet/IP. The TAS schedule is coordinated by a centralized **CUC (Centralized User Configuration)** and **CNC (Centralized Network Configuration)** per IEEE 802.1Qcc.

### 11.3 Mobile Robotics
Autonomous mobile robots use CAN FD for motor controllers and low-level actuators, bridged via TSN to a central compute unit running sensor fusion and SLAM. TSN's bounded latency ensures motor commands arrive within the servo control cycle time (typically 1–4 ms).

### 11.4 Aerospace (DO-178C / ARINC 664)
Next-generation avionics platforms replace ARINC 429 serial buses with CAN-connected LRUs (Line Replaceable Units) bridged to an AFDX-TSN hybrid backbone, reducing wire weight while maintaining DO-254 hardware determinism requirements.

---

## 12. Summary

| Aspect | Key Point |
|---|---|
| **Purpose** | Bridge CAN's deterministic bus to high-bandwidth TSN Ethernet without sacrificing real-time guarantees |
| **Core standards** | IEEE 802.1AS (clock sync), 802.1Qbv (TAS), 802.1Qci (PSFP), 802.1CB (redundancy) |
| **Clock alignment** | Both domains must share a gPTP-synchronized time base; PHC provides < 1 µs accuracy |
| **Priority mapping** | CAN ID ranges map to TSN PCP values and TAS queue assignments |
| **Encapsulation** | Raw L2 (custom EtherType) for lowest latency; UDP/CiA 603 for interoperability |
| **Linux tools** | SocketCAN, `tc taprio`, `ptp4l`, `phc2sys`, `SO_TXTIME` / ETF qdisc |
| **C/C++ approach** | `recvmsg` with `SO_TIMESTAMPING`, raw `AF_PACKET` socket, `sendmsg` with `SCM_TXTIME` |
| **Rust approach** | `libc` FFI for socket syscalls, `tokio::task::spawn_blocking` for CAN RX, `Arc<TsnTxSocket>` for shared TX |
| **Fault tolerance** | CAN error frame detection + bus-off recovery; `MSG_ERRQUEUE` for missed TXTIME detection; FRER for redundancy |
| **Applications** | Automotive zonal E/E, industrial IEC/IEEE 60802, mobile robotics, next-gen avionics |

### Critical Design Rules
1. **Always synchronize the TAS base-time** across all bridges to the same gPTP grandmaster before activating TAS — unsynchronized TAS schedules will collide.
2. **Dimension guard bands** to be at least as large as the maximum CAN-TSN frame size at line rate (typically 1–4 µs at 1 Gbit/s) to prevent window violations.
3. **Use hardware timestamping** whenever the NIC supports it — software timestamps introduce jitter of 10–100 µs that defeats the purpose of TSN latency guarantees.
4. **Monitor the error queue** (`MSG_ERRQUEUE`) continuously in production; missed TXTIME events indicate schedule misconfiguration or clock drift.
5. **Preserve CAN timestamps** in the TSN payload — downstream nodes may need the original CAN bus reception time for data fusion and signal validity checks.

---

## 13. References

- IEEE Std 802.1AS-2020 — *Timing and Synchronization for Time-Sensitive Applications*
- IEEE Std 802.1Qbv-2015 — *Enhancements for Scheduled Traffic*
- IEEE Std 802.1Qci-2017 — *Per-Stream Filtering and Policing*
- IEEE Std 802.1CB-2017 — *Frame Replication and Elimination for Reliability*
- IEC/IEEE 60802 — *TSN Profile for Industrial Automation*
- CiA 603 — *CAN over Ethernet (CoE)*
- Linux Kernel Documentation: `Documentation/networking/can.rst`
- Linux Kernel Documentation: `Documentation/networking/timestamping.rst`
- AUTOSAR — *Specification of Socket Adaptor (SoAd)*
- Open Source: `linuxptp` project (`ptp4l`, `phc2sys`) — https://linuxptp.sourceforge.net
- Open Source: `iproute2` `tc taprio` — https://man7.org/linux/man-pages/man8/tc-taprio.8.html
- Rust crate: `socketcan` — https://docs.rs/socketcan
- Rust crate: `tokio-socketcan` — https://docs.rs/tokio-socketcan