# 90. Migration Strategies (CAN to CAN FD)

**Structure:**
- **Key Differences** — side-by-side comparison of CAN vs CAN FD (payload, bitrate, CRC, frame format)
- **Three Migration Phases** — Assessment → Hybrid (gateway-segmented) → Full CAN FD
- **Backward Compatibility Patterns** — classic mode, parallel message strategy, gateway segmentation
- **Gateway Implementation** — routing table design, FD→CAN payload truncation, CAN→FD forwarding
- **Error Handling** — common failure modes during migration and mitigations

**C/C++ examples include:**
1. Linux SocketCAN CAN FD socket setup
2. Sending a 64-byte CAN FD frame with BRS
3. Full gateway forward logic (both directions) with routing table
4. A C++ `MigrationAwareNode` class with runtime protocol selection and auto-downgrade
5. TEC/REC bus health monitoring

**Rust examples include:**
1. `CanFdFrame` and `CanMode` types with clean abstractions
2. A `GatewayRouter` with `HashMap`-based routing table
3. A `MigrationNode` with atomic error counting and auto-downgrade
4. CAN FD DLC encoding/padding utilities with unit tests

> Planning and executing network upgrades while maintaining backward compatibility and system availability.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Key Differences: CAN vs CAN FD](#key-differences-can-vs-can-fd)
3. [Migration Phases Overview](#migration-phases-overview)
4. [Phase 1 — Network Assessment](#phase-1--network-assessment)
5. [Phase 2 — Hybrid Operation (CAN + CAN FD)](#phase-2--hybrid-operation-can--can-fd)
6. [Phase 3 — Full CAN FD Migration](#phase-3--full-can-fd-migration)
7. [Backward Compatibility Patterns](#backward-compatibility-patterns)
8. [Gateway / Bridge Node Implementation](#gateway--bridge-node-implementation)
9. [Bitrate Switching and Timing Configuration](#bitrate-switching-and-timing-configuration)
10. [Error Handling During Migration](#error-handling-during-migration)
11. [Testing and Validation Strategies](#testing-and-validation-strategies)
12. [C/C++ Code Examples](#cc-code-examples)
13. [Rust Code Examples](#rust-code-examples)
14. [Summary](#summary)

---

## Introduction

Migrating from Classical CAN (ISO 11898-1) to CAN FD (CAN with Flexible Data-Rate, ISO 11898-1:2015) is a significant undertaking in embedded and automotive systems engineering. CAN FD extends the original CAN protocol by offering:

- **Higher payload capacity** — up to 64 bytes per frame (vs. 8 bytes in Classical CAN)
- **Higher data-phase bitrates** — up to 8 Mbit/s (vs. 1 Mbit/s max in Classical CAN)
- **Improved CRC** — stronger 17-bit and 21-bit CRC polynomials

However, CAN FD frames are **not backward compatible** with Classical CAN nodes. A CAN FD frame on a bus with any Classical CAN node will cause that node to generate an error frame. This makes migration planning critical to avoid network disruption.

The overarching goal of a migration strategy is to **upgrade the network incrementally**, maintaining bus integrity and system availability at every step.

---

## Key Differences: CAN vs CAN FD

| Property               | Classical CAN         | CAN FD                        |
|------------------------|-----------------------|-------------------------------|
| Max payload            | 8 bytes               | 64 bytes                      |
| Max bitrate (nominal)  | 1 Mbit/s              | 1 Mbit/s                      |
| Max bitrate (data)     | 1 Mbit/s              | Up to 8 Mbit/s                |
| Bitrate switching      | No                    | Yes (BRS bit)                 |
| CRC width              | 15-bit                | 17-bit (≤16 bytes), 21-bit    |
| Frame format bit (FDF) | 0 (dominant)          | 1 (recessive)                 |
| Backward compatible    | Yes (with CAN FD nodes in CAN mode) | No (FD frames break CAN nodes) |
| Error detection        | Good                  | Stronger (improved CRC)       |
| Stuff bit counting     | Fixed stuffing        | Counter-based dynamic stuffing |

### Frame Format Comparison

```
Classical CAN Data Frame:
 SOF | Arbitration (11 or 29 bits) | Control (6 bits) | Data (0-8 bytes) | CRC (15+1) | ACK | EOF

CAN FD Data Frame:
 SOF | Arbitration (11 or 29 bits) | Control (FDF, BRS, ESI bits) | Data (0-64 bytes) | CRC (17 or 21 bits) | ACK | EOF
                                                         ^--- Bitrate switches here (BRS)
```

---

## Migration Phases Overview

A robust migration follows three broad phases:

```
Phase 1: Assessment
  └─ Inventory nodes, traffic, timing, physical layer

Phase 2: Hybrid Network (CAN + CAN FD coexist via gateway)
  └─ Introduce CAN FD segments
  └─ Deploy gateway/bridge nodes
  └─ Validate backward compatibility

Phase 3: Full CAN FD
  └─ Replace all Classical CAN nodes
  └─ Remove gateways
  └─ Optimize bitrates and payload sizes
```

---

## Phase 1 — Network Assessment

Before any hardware or software changes, thoroughly audit the existing CAN network.

### Assessment Checklist

1. **Node inventory** — List all ECUs, their CAN controller types, and firmware versions
2. **Bus topology** — Document cable lengths, stub lengths, termination resistors
3. **Bitrate and timing** — Record current nominal bitrate (typically 250 kbit/s or 500 kbit/s in automotive)
4. **Message catalog** — Identify all CAN IDs, DLC values, cycle times, and producers/consumers
5. **Bus load** — Measure peak and average bus utilization
6. **Physical layer** — Check transceiver specs for CAN FD compatibility (rise/fall times matter at high data rates)

### Physical Layer Constraints

CAN FD at high data rates (2–8 Mbit/s) is extremely sensitive to:

- **Stub lengths** — must be minimized (< 30 cm at 5 Mbit/s)
- **Termination** — standard 120 Ω termination remains, but capacitive loads matter more
- **Transceiver speed** — must support the target data-phase bitrate

A network that works perfectly at CAN 500 kbit/s may fail at CAN FD 2 Mbit/s data phase without physical layer rework.

---

## Phase 2 — Hybrid Operation (CAN + CAN FD)

During the transition, the network will contain both Classical CAN and CAN FD nodes. The key strategy is **bus segmentation**: CAN FD nodes communicate on dedicated CAN FD segments, while a **gateway node** translates between segments.

### Hybrid Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     Vehicle Network                     │
│                                                         │
│  [CAN Segment A]          [Gateway]    [CAN FD Segment B]│
│  ECU-1 (CAN)  ────┐                ┌─── ECU-4 (CAN FD) │
│  ECU-2 (CAN)  ────┤── CAN Bus ─────┤─── ECU-5 (CAN FD) │
│  ECU-3 (CAN)  ────┘                └─── ECU-6 (CAN FD) │
│                    120Ω          120Ω                   │
└─────────────────────────────────────────────────────────┘
```

### CAN FD Nodes in "CAN Compatibility Mode"

Many CAN FD controllers support a **FD tolerance / CAN compatibility mode**, where the node:
- Only transmits Classical CAN frames
- Can optionally accept (or ignore) CAN FD frames on the bus

This allows a CAN FD-capable node to be introduced into an existing Classical CAN network before the bus is fully migrated.

---

## Phase 3 — Full CAN FD Migration

Once all nodes have been replaced or upgraded:

1. Enable CAN FD frame transmission on all nodes
2. Configure optimal data-phase bitrate (commonly 2 Mbit/s for automotive, up to 8 Mbit/s for short industrial buses)
3. Increase DLC to exploit the 64-byte payload where beneficial
4. Remove gateway nodes
5. Re-validate timing, bus load, and error rates

---

## Backward Compatibility Patterns

### Pattern 1 — CAN FD Node Transmits Only CAN Frames

The simplest migration approach: new CAN FD hardware runs in Classical CAN mode until all peers are upgraded.

```c
// Set controller to CAN 2.0 compatible mode — no FD frames transmitted
can_set_mode(controller, CAN_MODE_CLASSIC);
```

### Pattern 2 — Parallel Message Strategy

During migration, a node may publish the same data both as a Classical CAN frame (for legacy nodes) and a CAN FD frame (for new nodes), using different CAN IDs.

```
CAN ID 0x100  → Classical CAN, DLC=8, for legacy ECUs
CAN ID 0x200  → CAN FD, DLC=32, extended data for new ECUs
```

### Pattern 3 — Gateway-based Segmentation

A gateway node bridges two physical buses: one Classical CAN, one CAN FD. It receives frames on one side and retransmits (translated) on the other.

---

## Gateway / Bridge Node Implementation

The gateway is the most critical component during migration. It must:

- Receive CAN frames and forward as CAN FD (expanding payload if needed)
- Receive CAN FD frames and forward as CAN (truncating payload to 8 bytes)
- Maintain message routing tables
- Handle timing / latency budgets
- Not introduce priority inversions

### Routing Table Concept

```c
typedef struct {
    uint32_t src_id;         // Source CAN ID
    uint32_t dst_id;         // Destination CAN ID (may be same)
    uint8_t  src_bus;        // 0 = CAN bus, 1 = CAN FD bus
    uint8_t  dst_bus;
    uint8_t  max_dlc;        // Truncate to this DLC when forwarding to CAN side
    bool     fd_to_can;      // Requires payload truncation
} GatewayRoute;
```

---

## Bitrate Switching and Timing Configuration

CAN FD uses two bitrates:
- **Nominal bitrate (NBR)** — used for arbitration phase (same as Classical CAN)
- **Data bitrate (DBR)** — used for the data phase (after BRS bit, before CRC delimiter)

Both must be configured precisely. The controller requires separate timing segments for each phase.

### Timing Parameters

```
Nominal Phase (like Classical CAN):
  Tq = 1 / (prescaler × Fosc)
  NBR = 1 / (Tq × (1 + Tprop + Tphase1 + Tphase2))

Data Phase (CAN FD specific):
  DBR = 1 / (Tq_fd × (1 + Tprop_fd + Tphase1_fd + Tphase2_fd))
```

A common automotive configuration:
- Nominal: 500 kbit/s (backward compatible with existing CAN)
- Data: 2 Mbit/s

---

## Error Handling During Migration

Migration introduces unique error scenarios:

| Scenario | Cause | Mitigation |
|---|---|---|
| Error frames on CAN FD segments | Classical CAN node sees FD frame | Strict bus segmentation |
| Payload truncation loss | 64-byte FD payload reduced to 8 bytes at gateway | Design messages to put critical data in first 8 bytes |
| Timing violations | Data-phase bitrate too high for cable length | Reduce data bitrate or shorten bus |
| CRC mismatches | Mixing frame types | Ensure all nodes on a segment use same protocol |
| Passive error state storms | Misconfigured node during migration | Monitor error counters, isolate problematic nodes |

---

## Testing and Validation Strategies

### Recommended Test Sequence

1. **Bench test** — Test each new node individually for CAN FD compliance
2. **Segment test** — Test CAN FD segment in isolation before connecting gateway
3. **Gateway integration test** — Validate routing, latency, and payload truncation behavior
4. **Full network regression** — Replay recorded bus traffic through the migrated network
5. **Stress test** — Maximum bus load at target bitrates
6. **Error injection** — Inject error frames, disconnected nodes, and verify graceful degradation

### Key Metrics to Validate

- Bus error counters (TEC/REC) — should remain near zero in normal operation
- Message latency — especially for safety-critical frames
- Bus utilization — should be lower after migration (larger payloads = fewer frames)
- Gateway throughput — ensure no message drops under peak load

---

## C/C++ Code Examples

### Example 1 — CAN FD Controller Initialization with Dual Bitrate (Linux SocketCAN)

```c
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

/* CAN FD socket setup with dual bitrate */
int setup_canfd_socket(const char *ifname)
{
    struct sockaddr_can addr;
    struct ifreq ifr;
    int sock, enable_fd = 1;

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    /* Enable CAN FD frames on this socket */
    if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                   &enable_fd, sizeof(enable_fd)) < 0) {
        perror("setsockopt CAN_RAW_FD_FRAMES");
        close(sock);
        return -1;
    }

    /* Bind to the interface */
    strcpy(ifr.ifr_name, ifname);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    printf("[INFO] CAN FD socket opened on %s (fd=%d)\n", ifname, sock);
    return sock;
}
```

### Example 2 — Sending a CAN FD Frame with 64-byte Payload

```c
#include <linux/can/raw.h>

/* CAN FD DLC encoding — CAN FD supports non-linear DLC values for large frames */
static uint8_t canfd_len2dlc(uint8_t len)
{
    /* CAN FD DLC table: 0-8 = linear, 9-15 = 12,16,20,24,32,48,64 bytes */
    static const uint8_t dlc_table[] = {
        0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64
    };
    uint8_t i;
    for (i = 0; i < 16; i++) {
        if (dlc_table[i] >= len)
            return i;
    }
    return 15; /* 64 bytes max */
}

int send_canfd_frame(int sock, uint32_t can_id, const uint8_t *data, uint8_t len)
{
    struct canfd_frame frame;
    memset(&frame, 0, sizeof(frame));

    frame.can_id  = can_id;
    frame.len     = len;
    frame.flags   = CANFD_BRS;  /* Bitrate Switch — use data-phase bitrate */
    /* frame.flags |= CANFD_ESI; // Error State Indicator — set if node is error passive */

    if (len > CANFD_MAX_DLEN) {
        fprintf(stderr, "[ERROR] Payload exceeds 64 bytes\n");
        return -1;
    }
    memcpy(frame.data, data, len);

    ssize_t nbytes = write(sock, &frame, sizeof(struct canfd_frame));
    if (nbytes != sizeof(struct canfd_frame)) {
        perror("write canfd_frame");
        return -1;
    }
    printf("[TX] CAN FD frame: ID=0x%03X len=%d BRS=%s\n",
           can_id, len, (frame.flags & CANFD_BRS) ? "on" : "off");
    return 0;
}
```

### Example 3 — Gateway: Receiving and Forwarding Between CAN and CAN FD Buses

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define MAX_ROUTES  64
#define CAN_BUS     0
#define CANFD_BUS   1

typedef struct {
    uint32_t src_id;
    uint32_t dst_id;
    uint8_t  src_bus;
    uint8_t  dst_bus;
    uint8_t  truncate_dlc;  /* Truncate to this length when going CAN FD → CAN */
    bool     active;
} GatewayRoute;

static GatewayRoute routes[MAX_ROUTES];
static int          num_routes = 0;

void gateway_add_route(uint32_t src_id, uint32_t dst_id,
                       uint8_t src_bus, uint8_t dst_bus,
                       uint8_t truncate_dlc)
{
    if (num_routes >= MAX_ROUTES) return;
    routes[num_routes++] = (GatewayRoute){
        .src_id       = src_id,
        .dst_id       = dst_id,
        .src_bus      = src_bus,
        .dst_bus      = dst_bus,
        .truncate_dlc = truncate_dlc,
        .active       = true
    };
}

/*
 * Process a received CAN FD frame and forward it to the CAN side.
 * Payload is truncated to 8 bytes if the destination is a Classical CAN segment.
 */
int gateway_forward_fd_to_can(int canfd_sock, int can_sock,
                               const struct canfd_frame *fd_frame)
{
    /* Look up routing table */
    for (int i = 0; i < num_routes; i++) {
        GatewayRoute *r = &routes[i];
        if (!r->active)                 continue;
        if (r->src_bus != CANFD_BUS)    continue;
        if (r->src_id  != fd_frame->can_id) continue;

        struct can_frame classic_frame;
        memset(&classic_frame, 0, sizeof(classic_frame));

        classic_frame.can_id  = r->dst_id;
        classic_frame.can_dlc = (fd_frame->len > r->truncate_dlc)
                                    ? r->truncate_dlc
                                    : fd_frame->len;

        memcpy(classic_frame.data, fd_frame->data, classic_frame.can_dlc);

        if (fd_frame->len > 8) {
            printf("[GW] WARNING: FD frame ID=0x%03X len=%d truncated to %d bytes\n",
                   fd_frame->can_id, fd_frame->len, classic_frame.can_dlc);
        }

        ssize_t ret = write(can_sock, &classic_frame, sizeof(classic_frame));
        if (ret != sizeof(classic_frame)) {
            perror("[GW] write classic frame");
            return -1;
        }
        printf("[GW] Forwarded FD→CAN: 0x%03X → 0x%03X (%d bytes)\n",
               fd_frame->can_id, r->dst_id, classic_frame.can_dlc);
        return 0;
    }
    return 1; /* No route found — frame dropped */
}

/*
 * Process a received Classical CAN frame and forward it to the CAN FD side.
 * The payload can be extended (zeroed) to exploit FD's larger DLC if desired.
 */
int gateway_forward_can_to_fd(int can_sock, int canfd_sock,
                               const struct can_frame *classic_frame)
{
    for (int i = 0; i < num_routes; i++) {
        GatewayRoute *r = &routes[i];
        if (!r->active)                      continue;
        if (r->src_bus != CAN_BUS)           continue;
        if (r->src_id  != classic_frame->can_id) continue;

        struct canfd_frame fd_frame;
        memset(&fd_frame, 0, sizeof(fd_frame));

        fd_frame.can_id = r->dst_id;
        fd_frame.len    = classic_frame->can_dlc;  /* Keep original length */
        fd_frame.flags  = CANFD_BRS;

        memcpy(fd_frame.data, classic_frame->data, classic_frame->can_dlc);

        ssize_t ret = write(canfd_sock, &fd_frame, sizeof(fd_frame));
        if (ret != sizeof(fd_frame)) {
            perror("[GW] write FD frame");
            return -1;
        }
        printf("[GW] Forwarded CAN→FD: 0x%03X → 0x%03X (%d bytes)\n",
               classic_frame->can_id, r->dst_id, fd_frame.len);
        return 0;
    }
    return 1;
}
```

### Example 4 — Migration-Aware Node: Runtime Protocol Selection

```cpp
#include <cstdint>
#include <cstring>
#include <functional>

// C++ class representing a node that can operate in CAN or CAN FD mode
// and switch at runtime — useful during phased migration.

enum class CanMode {
    CLASSIC,   // Classical CAN — transmit only CAN 2.0 frames
    FD_COMPAT, // CAN FD capable, but sends only CAN frames for backward compat
    FD_FULL    // Full CAN FD — bitrate switching and >8 byte payloads enabled
};

class MigrationAwareNode {
public:
    using SendFn = std::function<int(uint32_t id, const uint8_t*, uint8_t, bool brs)>;

    explicit MigrationAwareNode(SendFn send_fn)
        : send_(std::move(send_fn))
        , mode_(CanMode::FD_COMPAT)
        , tx_error_count_(0)
    {}

    void set_mode(CanMode mode) {
        mode_ = mode;
        printf("[NODE] Mode changed to %s\n", mode_name());
    }

    CanMode get_mode() const { return mode_; }

    /*
     * Send data — behavior depends on current migration mode.
     * In CLASSIC mode: caps payload at 8 bytes.
     * In FD_COMPAT: could send CAN frame, even if FD hardware is available.
     * In FD_FULL: sends FD frame with BRS, full 64-byte payload.
     */
    int send(uint32_t can_id, const uint8_t *data, uint8_t len) {
        switch (mode_) {
            case CanMode::CLASSIC:
            case CanMode::FD_COMPAT: {
                uint8_t dlc = (len > 8) ? 8 : len;
                if (len > 8) {
                    printf("[NODE] WARN: Payload %d bytes truncated to 8 (CLASSIC mode)\n", len);
                }
                return send_(can_id, data, dlc, false /* no BRS */);
            }
            case CanMode::FD_FULL: {
                uint8_t dlc = (len > 64) ? 64 : len;
                return send_(can_id, data, dlc, true /* BRS enabled */);
            }
        }
        return -1;
    }

    /*
     * Observe bus error rate and auto-downgrade mode if FD causes errors.
     * Call periodically from your error monitoring task.
     */
    void on_error_frame_received() {
        tx_error_count_++;
        if (mode_ == CanMode::FD_FULL && tx_error_count_ > 10) {
            printf("[NODE] High error rate detected — downgrading to FD_COMPAT\n");
            set_mode(CanMode::FD_COMPAT);
            tx_error_count_ = 0;
        }
    }

    void on_successful_tx() {
        if (tx_error_count_ > 0) tx_error_count_--;
    }

private:
    const char *mode_name() const {
        switch (mode_) {
            case CanMode::CLASSIC:    return "CLASSIC";
            case CanMode::FD_COMPAT:  return "FD_COMPAT";
            case CanMode::FD_FULL:    return "FD_FULL";
        }
        return "UNKNOWN";
    }

    SendFn   send_;
    CanMode  mode_;
    uint32_t tx_error_count_;
};
```

### Example 5 — Monitoring Bus Health During Migration (C)

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* Simplified TEC/REC monitoring structure */
typedef struct {
    uint32_t tx_error_count;   /* Transmit Error Counter */
    uint32_t rx_error_count;   /* Receive Error Counter */
    uint32_t bus_off_events;
    uint32_t fd_format_errors; /* Errors specific to CAN FD parsing */
    bool     bus_off;
} CanBusHealth;

void bus_health_update(CanBusHealth *h,
                       uint8_t tec, uint8_t rec,
                       bool fd_format_err, bool bus_off)
{
    h->tx_error_count = tec;
    h->rx_error_count = rec;
    if (fd_format_err)  h->fd_format_errors++;
    if (bus_off && !h->bus_off) {
        h->bus_off_events++;
        printf("[HEALTH] Bus-off event #%u detected!\n", h->bus_off_events);
    }
    h->bus_off = bus_off;
}

void bus_health_report(const CanBusHealth *h)
{
    printf("=== CAN Bus Health Report ===\n");
    printf("  TEC             : %u\n", h->tx_error_count);
    printf("  REC             : %u\n", h->rx_error_count);
    printf("  Bus-off events  : %u\n", h->bus_off_events);
    printf("  FD format errors: %u\n", h->fd_format_errors);
    printf("  Bus-off now     : %s\n",  h->bus_off ? "YES" : "no");

    /* Migration readiness heuristic */
    if (h->fd_format_errors > 0) {
        printf("  [WARN] FD format errors suggest a non-FD node on the FD segment!\n");
    }
    if (h->tx_error_count > 127) {
        printf("  [WARN] TEC > 127: node entering error-passive state\n");
    }
    if (h->tx_error_count == 255) {
        printf("  [CRIT] TEC = 255: node will go bus-off\n");
    }
}
```

---

## Rust Code Examples

### Example 1 — CAN FD Frame Type Definitions and Mode Abstraction

```rust
use std::fmt;

/// Maximum data lengths for CAN and CAN FD frames
pub const CAN_MAX_DLC: usize = 8;
pub const CANFD_MAX_DLC: usize = 64;

/// Migration operating mode for a node
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CanMode {
    /// Only transmit Classical CAN frames (≤8 bytes, no BRS)
    Classic,
    /// Hardware is CAN FD capable, but transmits only CAN frames for compatibility
    FdCompat,
    /// Full CAN FD: bitrate switching enabled, payloads up to 64 bytes
    FdFull,
}

impl fmt::Display for CanMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CanMode::Classic   => write!(f, "CLASSIC"),
            CanMode::FdCompat  => write!(f, "FD_COMPAT"),
            CanMode::FdFull    => write!(f, "FD_FULL"),
        }
    }
}

/// A CAN or CAN FD frame
#[derive(Debug, Clone)]
pub struct CanFdFrame {
    pub can_id:  u32,
    pub data:    Vec<u8>,
    /// Bitrate Switch — if true, data phase uses the higher bitrate
    pub brs:     bool,
    /// Error State Indicator — if true, the transmitting node is error-passive
    pub esi:     bool,
}

impl CanFdFrame {
    pub fn new_classic(can_id: u32, data: Vec<u8>) -> Result<Self, String> {
        if data.len() > CAN_MAX_DLC {
            return Err(format!(
                "Classical CAN frame exceeds 8 bytes (got {})", data.len()
            ));
        }
        Ok(Self { can_id, data, brs: false, esi: false })
    }

    pub fn new_fd(can_id: u32, data: Vec<u8>, brs: bool) -> Result<Self, String> {
        if data.len() > CANFD_MAX_DLC {
            return Err(format!(
                "CAN FD frame exceeds 64 bytes (got {})", data.len()
            ));
        }
        Ok(Self { can_id, data, brs, esi: false })
    }

    pub fn is_fd(&self) -> bool {
        self.data.len() > CAN_MAX_DLC || self.brs
    }
}
```

### Example 2 — Gateway Routing Table in Rust

```rust
use std::collections::HashMap;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BusId {
    ClassicCan,
    CanFd,
}

#[derive(Debug, Clone)]
pub struct RouteEntry {
    pub dst_id:       u32,
    pub dst_bus:      BusId,
    /// When forwarding FD→CAN, truncate payload to this many bytes
    pub max_payload:  usize,
}

pub struct GatewayRouter {
    /// Key: (src_bus, src_can_id)  →  RouteEntry
    routes: HashMap<(BusId, u32), RouteEntry>,
    dropped_frames: u64,
    forwarded_frames: u64,
}

impl GatewayRouter {
    pub fn new() -> Self {
        Self {
            routes: HashMap::new(),
            dropped_frames: 0,
            forwarded_frames: 0,
        }
    }

    pub fn add_route(
        &mut self,
        src_bus: BusId,
        src_id:  u32,
        dst_bus: BusId,
        dst_id:  u32,
        max_payload: usize,
    ) {
        self.routes.insert(
            (src_bus, src_id),
            RouteEntry { dst_id, dst_bus, max_payload },
        );
        println!(
            "[GW] Route added: {:?}:0x{:03X} → {:?}:0x{:03X} (max {} bytes)",
            src_bus, src_id, dst_bus, dst_id, max_payload
        );
    }

    /// Forward a frame according to the routing table.
    /// Returns the translated outgoing frame, or None if no route exists.
    pub fn forward(
        &mut self,
        src_bus: BusId,
        frame:   &CanFdFrame,
    ) -> Option<(BusId, CanFdFrame)> {
        let key = (src_bus, frame.can_id);
        match self.routes.get(&key) {
            None => {
                self.dropped_frames += 1;
                eprintln!(
                    "[GW] No route for {:?}:0x{:03X} — frame dropped",
                    src_bus, frame.can_id
                );
                None
            }
            Some(route) => {
                let payload = match route.dst_bus {
                    BusId::ClassicCan => {
                        // Truncate to max 8 bytes for Classic CAN destination
                        let trunc = route.max_payload.min(CAN_MAX_DLC);
                        let len   = frame.data.len().min(trunc);
                        if frame.data.len() > CAN_MAX_DLC {
                            println!(
                                "[GW] WARN: Truncating FD payload {} → {} bytes for CAN",
                                frame.data.len(), len
                            );
                        }
                        frame.data[..len].to_vec()
                    }
                    BusId::CanFd => {
                        // Forward as-is (or could extend with zeroes)
                        frame.data.clone()
                    }
                };

                let out_frame = CanFdFrame {
                    can_id: route.dst_id,
                    data:   payload,
                    brs:    route.dst_bus == BusId::CanFd,
                    esi:    frame.esi,
                };

                self.forwarded_frames += 1;
                println!(
                    "[GW] Forwarded {:?}:0x{:03X} → {:?}:0x{:03X} ({} bytes)",
                    src_bus, frame.can_id,
                    route.dst_bus, route.dst_id,
                    out_frame.data.len()
                );
                Some((route.dst_bus, out_frame))
            }
        }
    }

    pub fn stats(&self) {
        println!(
            "[GW STATS] forwarded={} dropped={}",
            self.forwarded_frames, self.dropped_frames
        );
    }
}
```

### Example 3 — Migration-Aware Node with Error Monitoring

```rust
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

pub struct MigrationNode {
    mode:            CanMode,
    error_count:     Arc<AtomicU32>,
    downgrade_limit: u32,
}

impl MigrationNode {
    pub fn new(initial_mode: CanMode, downgrade_limit: u32) -> Self {
        Self {
            mode: initial_mode,
            error_count: Arc::new(AtomicU32::new(0)),
            downgrade_limit,
        }
    }

    pub fn set_mode(&mut self, mode: CanMode) {
        println!("[NODE] Switching mode: {} → {}", self.mode, mode);
        self.mode = mode;
        self.error_count.store(0, Ordering::Relaxed);
    }

    /// Build a frame appropriate for the current migration mode.
    pub fn build_frame(&self, can_id: u32, data: Vec<u8>) -> Result<CanFdFrame, String> {
        match self.mode {
            CanMode::Classic | CanMode::FdCompat => {
                let truncated = if data.len() > CAN_MAX_DLC {
                    println!(
                        "[NODE] Payload {} bytes capped to 8 in {} mode",
                        data.len(), self.mode
                    );
                    data[..CAN_MAX_DLC].to_vec()
                } else {
                    data
                };
                CanFdFrame::new_classic(can_id, truncated)
            }
            CanMode::FdFull => {
                CanFdFrame::new_fd(can_id, data, true /* BRS on */)
            }
        }
    }

    /// Called by the CAN driver when an error frame is detected on the bus.
    pub fn on_error_frame(&mut self) {
        let count = self.error_count.fetch_add(1, Ordering::Relaxed) + 1;
        println!("[NODE] Error frame #{} detected", count);

        // Auto-downgrade strategy: if FD_FULL is causing errors, fall back
        if self.mode == CanMode::FdFull && count >= self.downgrade_limit {
            println!(
                "[NODE] Error threshold ({}) reached — downgrading to FD_COMPAT",
                self.downgrade_limit
            );
            self.set_mode(CanMode::FdCompat);
        }
    }

    pub fn on_successful_tx(&self) {
        // Decay error counter on successful transmissions
        let prev = self.error_count.load(Ordering::Relaxed);
        if prev > 0 {
            self.error_count.store(prev - 1, Ordering::Relaxed);
        }
    }

    pub fn current_mode(&self) -> CanMode {
        self.mode
    }
}
```

### Example 4 — CAN FD DLC Encoding Utility

```rust
/// CAN FD supports non-linear DLC encoding for frames larger than 8 bytes.
/// DLC values 9–15 map to: 12, 16, 20, 24, 32, 48, 64 bytes.
pub fn len_to_dlc(len: usize) -> u8 {
    match len {
        0..=8   => len as u8,
        9..=12  => 9,
        13..=16 => 10,
        17..=20 => 11,
        21..=24 => 12,
        25..=32 => 13,
        33..=48 => 14,
        _       => 15, // 49–64 bytes
    }
}

pub fn dlc_to_len(dlc: u8) -> usize {
    match dlc {
        0..=8 => dlc as usize,
        9     => 12,
        10    => 16,
        11    => 20,
        12    => 24,
        13    => 32,
        14    => 48,
        15    => 64,
        _     => 0,
    }
}

/// Pad a payload vector to the next valid CAN FD frame length.
/// CAN FD frames must be padded to a valid DLC length; unused bytes are set to 0xCC.
pub fn pad_to_fd_dlc(data: &[u8]) -> Vec<u8> {
    let dlc     = len_to_dlc(data.len());
    let padded  = dlc_to_len(dlc);
    let mut out = data.to_vec();
    out.resize(padded, 0xCC); // 0xCC is a common padding byte
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_dlc_encoding() {
        assert_eq!(len_to_dlc(8),  8);
        assert_eq!(len_to_dlc(9),  9);
        assert_eq!(len_to_dlc(12), 9);
        assert_eq!(len_to_dlc(13), 10);
        assert_eq!(len_to_dlc(64), 15);
    }

    #[test]
    fn test_fd_padding() {
        let data = vec![0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09];
        let padded = pad_to_fd_dlc(&data);
        assert_eq!(padded.len(), 12);  // Next valid FD DLC after 9 is 12
        assert_eq!(&padded[9..], &[0xCC, 0xCC, 0xCC]);
    }

    #[test]
    fn test_frame_creation() {
        let frame = CanFdFrame::new_fd(0x123, vec![0u8; 32], true).unwrap();
        assert!(frame.is_fd());
        assert_eq!(frame.data.len(), 32);
        assert!(frame.brs);
    }
}
```

---

## Summary

Migrating from Classical CAN to CAN FD is not a flag-day event — it requires careful, phased execution to preserve network integrity and system availability throughout.

**Key takeaways:**

1. **CAN FD frames break Classical CAN nodes** — strict bus segmentation or CAN FD compatibility mode is mandatory during any mixed-protocol period.

2. **The gateway is the backbone of hybrid operation** — it must handle payload truncation (FD → CAN), routing, and timing without introducing message loss or priority inversions.

3. **Physical layer is often the bottleneck** — cable length, stub length, and transceiver rise times constrain the achievable data-phase bitrate more than the controller itself. Always validate the physical layer before increasing bitrates.

4. **Use CAN FD compatibility mode as a stepping stone** — hardware-upgrade your nodes first, run them in CAN-compatible mode, then enable FD transmission once the entire segment is ready.

5. **Design for truncation** — place the most critical signal bytes in the first 8 bytes of every message, so that if gateway truncation occurs, safety-critical information is preserved.

6. **Monitor error counters throughout** — sudden increases in TEC/REC or FD format errors are early indicators of a node misbehaving on the wrong segment.

7. **Automate the migration node logic** — as shown in the C++ `MigrationAwareNode` and Rust `MigrationNode` examples, nodes can self-detect error conditions and fall back to a safer protocol mode, enabling graceful degradation during rollout.

8. **Leverage the larger payload** — once fully migrated, consolidate related signals into single larger CAN FD frames to reduce bus load and improve latency predictability.

A well-executed migration yields a network with **significantly lower bus utilization**, **stronger error detection**, and **headroom for future data-intensive applications** (ADAS sensor fusion, over-the-air updates, high-resolution diagnostics) — all on the proven, deterministic CAN bus architecture.

---

*Reference: ISO 11898-1:2015 (CAN FD), Bosch CAN FD Specification 1.0, SAE J2284 (automotive CAN), Linux SocketCAN documentation.*