# 91. Redundant CAN Networks

**Architecture** — Three redundancy patterns are explained (hot standby, simultaneous dual-TX with health monitoring, and TMR voting), along with a comparison table of redundancy levels from cable-only up to full node duplication.

**Fault Detection** — A state machine governs channel transitions (Healthy → Warning → Faulted → recovery), driven by TEC/REC error counters, bus-off events, and RX message timeouts, with configurable hysteresis to prevent oscillation.

**C/C++ Implementation** — A complete `redundant_can.h` / `redundant_can.c` pair using SocketCAN, covering dual-TX, non-blocking receive with failover, periodic health updates, and a payload voting function for split-brain detection.

**Rust Implementation** — A type-safe manager split across `channel.rs` and `manager.rs`, using `Result<T, RCanError>` to force all failure paths to be handled, with a `recv_voted()` method that performs cross-channel consistency checking.

**Practical topics** — Sequence number deduplication, timestamp alignment, HiL fault injection test cases, AUTOSAR integration points, and ISO 26262 / IEC 61508 compliance considerations are all included.

## Designing Dual-CAN Architectures for Fault Tolerance in Safety-Critical Applications

---

## Table of Contents

1. [Introduction](#introduction)
2. [Fundamentals of CAN Redundancy](#fundamentals-of-can-redundancy)
3. [Redundancy Architectures](#redundancy-architectures)
4. [Fault Detection and Switchover Logic](#fault-detection-and-switchover-logic)
5. [Implementation in C/C++](#implementation-in-cc)
6. [Implementation in Rust](#implementation-in-rust)
7. [Synchronization and Message Consistency](#synchronization-and-message-consistency)
8. [Testing and Validation](#testing-and-validation)
9. [Standards and Compliance](#standards-and-compliance)
10. [Summary](#summary)

---

## Introduction

In safety-critical domains such as automotive, aerospace, industrial automation, and medical devices, communication reliability is not optional — it is a hard requirement. A single point of failure in a CAN (Controller Area Network) bus can lead to catastrophic outcomes: loss of braking control in a vehicle, misfire in a robotic arm, or incorrect dosing in infusion pumps.

**Redundant CAN networks** address this by deploying two (or more) parallel CAN buses carrying the same or complementary data, supervised by logic that can detect faults and seamlessly switch to, or blend data from, the healthy channel. The design goal is to ensure that no single physical or logical failure results in a loss of communication.

This document covers the architecture, fault detection strategies, switchover mechanisms, and practical implementation patterns — with complete code examples in C/C++ and Rust.

---

## Fundamentals of CAN Redundancy

### Why CAN Buses Fail

CAN failures fall into several categories:

| Failure Type | Example |
|---|---|
| Physical layer | Wire break, short to GND/VCC, connector corrosion |
| EMI/noise | High-voltage transients, radiated interference |
| Node fault | Bus-off state triggered by excessive errors |
| Termination fault | Missing or wrong termination resistor |
| Software fault | Babbling node continuously sending invalid frames |

### Redundancy Objectives

A redundant CAN design must achieve:

- **No single point of failure (SPOF):** The failure of any one cable, connector, transceiver, or controller must not interrupt communication.
- **Seamless failover:** Switchover time must be within the application's timing budget (often < 1 ms for automotive).
- **Data consistency:** Both channels must agree on message content to prevent split-brain scenarios.
- **Fault isolation:** A fault on one channel must not propagate to the other.

### Relevant Safety Standards

- **ISO 26262** (Automotive functional safety) — requires ASIL-D designs to have hardware redundancy for safety-relevant communication paths.
- **IEC 61508** — general functional safety standard demanding SIL 3/4 systems to consider dual-channel designs.
- **DO-178C / DO-254** — avionics software and hardware standards requiring redundant communication paths.
- **IEC 62280** — railway communication safety standard.

---

## Redundancy Architectures

### 1. Hot Standby (Active/Passive)

One CAN channel is **active** and carries all traffic. The second channel is **passive** — it monitors traffic but does not transmit. On fault detection, the passive channel takes over as the active channel.

```
  Node A                               Node B
  ┌──────────┐   CAN Bus 1 (Active)   ┌──────────┐
  │ CAN Ctrl1│━━━━━━━━━━━━━━━━━━━━━━━━│ CAN Ctrl1│
  │          │                        │          │
  │ CAN Ctrl2│────────────────────────│ CAN Ctrl2│
  └──────────┘   CAN Bus 2 (Standby)  └──────────┘
```

**Pros:** Simple logic, low bus load.  
**Cons:** Switchover latency; standby channel health is not continuously proven under load.

### 2. Hot Standby with Health Monitoring (Recommended for ASIL-C/D)

Both channels transmit simultaneously. Receivers accept data from whichever channel is healthy. This ensures both channels are continuously exercised and their health is always known.

```
  Node A                               Node B
  ┌──────────┐   CAN Bus 1 (Primary)  ┌──────────┐
  │ CAN Ctrl1│━━━━━━━━━━━━━━━━━━━━━━━━│ CAN Ctrl1│
  │          │   Both TX same data    │          │
  │ CAN Ctrl2│━━━━━━━━━━━━━━━━━━━━━━━━│ CAN Ctrl2│
  └──────────┘   CAN Bus 2 (Mirror)   └──────────┘
```

**Pros:** Instant failover (zero latency); continuous health proof; enables cross-channel voting.  
**Cons:** Higher bus load; requires message deduplication logic at receivers.

### 3. Voting / Arbitration (Triple Modular Redundancy, TMR)

Three independent CAN channels carry the same data. A majority voter selects the correct value when channels disagree.

```
  CAN Bus 1 ──┐
  CAN Bus 2 ──┤ Voter ──► Output
  CAN Bus 3 ──┘
```

Used in aerospace and nuclear applications. Overkill for most automotive ECUs but required for the highest safety integrity levels.

### 4. Physical Layer Redundancy vs. Controller Redundancy

| Level | What is Redundant | Typical Application |
|---|---|---|
| Cable only | Two wires (CAN_H/CAN_L pairs) per bus | Vibration-sensitive cabling |
| Transceiver | Two transceivers, one controller | PCB-level fault tolerance |
| Controller + Transceiver | Two complete CAN peripherals | ASIL-C/D ECUs |
| Full node | Entire ECU duplicated | Brake-by-wire, steer-by-wire |

---

## Fault Detection and Switchover Logic

### Key Metrics to Monitor

```
1. Error counters: TEC (Transmit Error Counter), REC (Receive Error Counter)
2. Bus-off state detection
3. Message timeout (no message received within expected period)
4. CRC error rate
5. Arbitration loss rate (possible babbling node)
6. Bit error detection
```

### Switchover State Machine

```
       ┌─────────────┐
       │   BOTH_OK   │◄──────────────────────────┐
       └──────┬──────┘                           │
              │ Channel 1 fault detected         │
              ▼                                  │
       ┌─────────────┐                           │
       │  CH2_ONLY   │ Channel 1 recovered &     │
       │  (Primary)  │──────────────────────────►│
       └──────┬──────┘ both stable for T_hold    │
              │ Channel 2 also faults            │
              ▼                                  │
       ┌─────────────┐                           │
       │   FAULT_    │                           │
       │   BOTH      │  Emergency / safe state   │
       └─────────────┘                           │
                                                 │
       ┌─────────────┐                           │
       │  CH1_ONLY   │ Channel 2 recovered &     │
       │  (Primary)  │──────────────────────────►│
       └─────────────┘ both stable for T_hold
```

---

## Implementation in C/C++

The following examples target an embedded system using the SocketCAN interface on Linux, which mirrors the API commonly used in automotive ECUs running AUTOSAR-like stacks. For bare-metal MCUs, the socket calls map directly to HAL register operations.

### Header: Redundant CAN Manager

```c
/* redundant_can.h */
#ifndef REDUNDANT_CAN_H
#define REDUNDANT_CAN_H

#include <stdint.h>
#include <stdbool.h>
#include <linux/can.h>

/* ------------------------------------------------------------------ */
/*  Configuration                                                     */
/* ------------------------------------------------------------------ */

#define RCAN_CHANNEL_COUNT       2
#define RCAN_TX_ERROR_THRESHOLD  96   /* Warn before bus-off at 256   */
#define RCAN_RX_ERROR_THRESHOLD  96
#define RCAN_FAULT_HOLD_MS       200  /* Hysteresis: channel must be  */
                                      /* fault-free this long to re-  */
                                      /* qualify as healthy           */
#define RCAN_MSG_TIMEOUT_MS      50   /* Max acceptable gap in rx     */

/* ------------------------------------------------------------------ */
/*  Channel health state                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    RCAN_STATE_HEALTHY   = 0,
    RCAN_STATE_WARNING   = 1,   /* Error counters elevated           */
    RCAN_STATE_FAULTED   = 2,   /* Bus-off or timeout                */
    RCAN_STATE_DISABLED  = 3    /* Manually disabled                 */
} rcan_channel_state_t;

/* ------------------------------------------------------------------ */
/*  Per-channel context                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    int                   fd;             /* SocketCAN file descriptor  */
    const char           *ifname;         /* e.g. "can0", "can1"        */
    rcan_channel_state_t  state;
    uint32_t              tx_errors;
    uint32_t              rx_errors;
    uint64_t              last_rx_ts_ms;  /* Timestamp of last rx frame */
    uint64_t              fault_since_ms; /* When fault was first seen  */
    uint64_t              healthy_since_ms;
    bool                  is_primary;
} rcan_channel_t;

/* ------------------------------------------------------------------ */
/*  Redundancy manager context                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    rcan_channel_t  ch[RCAN_CHANNEL_COUNT];
    int             active_rx_channel;  /* Index: 0 or 1              */
    bool            dual_tx_enabled;    /* Transmit on both channels  */
} rcan_manager_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

int  rcan_init(rcan_manager_t *mgr,
               const char *ch0_iface,
               const char *ch1_iface,
               bool dual_tx);

int  rcan_send(rcan_manager_t *mgr, const struct can_frame *frame);

int  rcan_recv(rcan_manager_t *mgr, struct can_frame *out_frame,
               int *out_channel);

void rcan_update_health(rcan_manager_t *mgr);

rcan_channel_state_t rcan_get_channel_state(const rcan_manager_t *mgr,
                                             int channel_idx);

void rcan_close(rcan_manager_t *mgr);

#endif /* REDUNDANT_CAN_H */
```

### Implementation: Redundant CAN Manager

```c
/* redundant_can.c */
#include "redundant_can.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                  */
/* ------------------------------------------------------------------ */

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

/**
 * Open a SocketCAN interface and configure it to receive error frames.
 */
static int open_can_socket(const char *ifname) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    /* Bind to interface */
    struct ifreq ifr;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(fd);
        return -1;
    }

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    /* Enable error frame reception */
    can_err_mask_t err_mask = CAN_ERR_TX_TIMEOUT |
                               CAN_ERR_LOSTARB    |
                               CAN_ERR_CRTL       |
                               CAN_ERR_PROT       |
                               CAN_ERR_BUSOFF;
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
               &err_mask, sizeof(err_mask));

    return fd;
}

/* ------------------------------------------------------------------ */
/*  Public: Initialise manager                                         */
/* ------------------------------------------------------------------ */

int rcan_init(rcan_manager_t *mgr,
              const char *ch0_iface,
              const char *ch1_iface,
              bool dual_tx)
{
    memset(mgr, 0, sizeof(*mgr));

    const char *ifaces[RCAN_CHANNEL_COUNT] = { ch0_iface, ch1_iface };

    for (int i = 0; i < RCAN_CHANNEL_COUNT; i++) {
        rcan_channel_t *ch = &mgr->ch[i];
        ch->ifname  = ifaces[i];
        ch->state   = RCAN_STATE_HEALTHY;
        ch->is_primary = (i == 0);

        ch->fd = open_can_socket(ifaces[i]);
        if (ch->fd < 0) {
            fprintf(stderr, "rcan: failed to open %s\n", ifaces[i]);
            /* Degrade: mark channel as faulted but continue */
            ch->state = RCAN_STATE_FAULTED;
        }

        ch->last_rx_ts_ms    = now_ms();
        ch->healthy_since_ms = now_ms();
    }

    mgr->active_rx_channel = 0;
    mgr->dual_tx_enabled   = dual_tx;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public: Transmit                                                   */
/* ------------------------------------------------------------------ */

/**
 * Send a CAN frame.
 *
 * If dual_tx is enabled, the frame is sent on both channels.
 * If a channel is faulted, it is skipped silently.
 * Returns 0 if at least one channel successfully sent the frame,
 * -1 if both channels failed.
 */
int rcan_send(rcan_manager_t *mgr, const struct can_frame *frame) {
    int sent = 0;

    for (int i = 0; i < RCAN_CHANNEL_COUNT; i++) {
        rcan_channel_t *ch = &mgr->ch[i];

        /* Skip faulted/disabled channels */
        if (ch->state == RCAN_STATE_FAULTED ||
            ch->state == RCAN_STATE_DISABLED ||
            ch->fd < 0) {
            continue;
        }

        /* In non-dual mode, only send on active channel */
        if (!mgr->dual_tx_enabled && i != mgr->active_rx_channel) {
            continue;
        }

        ssize_t nbytes = write(ch->fd, frame, sizeof(struct can_frame));
        if (nbytes < 0) {
            ch->tx_errors++;
            fprintf(stderr, "rcan: TX error on %s: %s\n",
                    ch->ifname, strerror(errno));
        } else {
            sent++;
        }
    }

    return (sent > 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/*  Public: Receive                                                    */
/* ------------------------------------------------------------------ */

/**
 * Non-blocking receive from the active channel.
 *
 * If the active channel has no data, falls back to the other channel.
 * Error frames are consumed internally to update health state.
 *
 * Returns number of bytes read, or -1 if no data available.
 */
int rcan_recv(rcan_manager_t *mgr, struct can_frame *out_frame,
              int *out_channel)
{
    /*
     * Poll active channel first, then failover channel.
     * A real implementation would use select()/epoll() across both fds.
     */
    int order[RCAN_CHANNEL_COUNT];
    order[0] = mgr->active_rx_channel;
    order[1] = 1 - mgr->active_rx_channel;  /* Works for 2-channel */

    for (int i = 0; i < RCAN_CHANNEL_COUNT; i++) {
        int idx = order[i];
        rcan_channel_t *ch = &mgr->ch[idx];

        if (ch->fd < 0 || ch->state == RCAN_STATE_FAULTED) continue;

        /* Non-blocking read */
        ssize_t nbytes = recv(ch->fd, out_frame,
                              sizeof(struct can_frame), MSG_DONTWAIT);
        if (nbytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            ch->rx_errors++;
            continue;
        }

        /* Check for error frame */
        if (out_frame->can_id & CAN_ERR_FLAG) {
            if (out_frame->can_id & CAN_ERR_BUSOFF) {
                fprintf(stderr, "rcan: BUS-OFF on %s\n", ch->ifname);
                ch->state         = RCAN_STATE_FAULTED;
                ch->fault_since_ms = now_ms();
            } else if (out_frame->can_id & CAN_ERR_CRTL) {
                ch->rx_errors++;
            }
            continue;  /* Do not deliver error frames to the application */
        }

        ch->last_rx_ts_ms = now_ms();
        if (out_channel) *out_channel = idx;
        return (int)nbytes;
    }

    return -1;
}

/* ------------------------------------------------------------------ */
/*  Public: Periodic health update                                     */
/* ------------------------------------------------------------------ */

/**
 * Call this from a periodic task (e.g. 10 ms tick).
 *
 * Evaluates error counters and message timeouts, updates channel state,
 * and selects the best channel for reception.
 */
void rcan_update_health(rcan_manager_t *mgr) {
    uint64_t now = now_ms();

    for (int i = 0; i < RCAN_CHANNEL_COUNT; i++) {
        rcan_channel_t *ch = &mgr->ch[i];
        if (ch->fd < 0) continue;

        /* --- Detect message timeout --- */
        uint64_t rx_age = now - ch->last_rx_ts_ms;
        if (rx_age > RCAN_MSG_TIMEOUT_MS &&
            ch->state == RCAN_STATE_HEALTHY) {
            fprintf(stderr, "rcan: RX timeout on %s (%llu ms)\n",
                    ch->ifname, (unsigned long long)rx_age);
            ch->state         = RCAN_STATE_FAULTED;
            ch->fault_since_ms = now;
        }

        /* --- Detect elevated error counters --- */
        if (ch->tx_errors > RCAN_TX_ERROR_THRESHOLD ||
            ch->rx_errors > RCAN_RX_ERROR_THRESHOLD) {
            if (ch->state == RCAN_STATE_HEALTHY) {
                ch->state         = RCAN_STATE_WARNING;
                ch->fault_since_ms = now;
            }
        }

        /* --- Recovery hysteresis --- */
        if (ch->state == RCAN_STATE_WARNING ||
            ch->state == RCAN_STATE_FAULTED) {
            bool counters_ok = (ch->tx_errors <= RCAN_TX_ERROR_THRESHOLD / 2 &&
                                 ch->rx_errors <= RCAN_RX_ERROR_THRESHOLD / 2);
            bool rx_fresh    = (rx_age < RCAN_MSG_TIMEOUT_MS / 2);

            if (counters_ok && rx_fresh) {
                uint64_t healthy_duration = now - ch->healthy_since_ms;
                if (healthy_duration >= RCAN_FAULT_HOLD_MS) {
                    fprintf(stderr, "rcan: Channel %d recovered\n", i);
                    ch->state     = RCAN_STATE_HEALTHY;
                    ch->tx_errors = 0;
                    ch->rx_errors = 0;
                }
            } else {
                /* Reset healthy timer if conditions not met */
                ch->healthy_since_ms = now;
            }
        }
    }

    /* --- Select best active RX channel --- */
    int primary = -1;
    for (int i = 0; i < RCAN_CHANNEL_COUNT; i++) {
        if (mgr->ch[i].state == RCAN_STATE_HEALTHY) {
            primary = i;
            break;
        }
    }
    if (primary >= 0) {
        if (mgr->active_rx_channel != primary) {
            fprintf(stderr, "rcan: Switching active RX to channel %d\n", primary);
        }
        mgr->active_rx_channel = primary;
    }
    /* If primary == -1, both channels are faulted — safe state is application-specific */
}

/* ------------------------------------------------------------------ */
/*  Public: Teardown                                                   */
/* ------------------------------------------------------------------ */

void rcan_close(rcan_manager_t *mgr) {
    for (int i = 0; i < RCAN_CHANNEL_COUNT; i++) {
        if (mgr->ch[i].fd >= 0) {
            close(mgr->ch[i].fd);
            mgr->ch[i].fd = -1;
        }
    }
}

rcan_channel_state_t rcan_get_channel_state(const rcan_manager_t *mgr,
                                              int channel_idx) {
    if (channel_idx < 0 || channel_idx >= RCAN_CHANNEL_COUNT)
        return RCAN_STATE_DISABLED;
    return mgr->ch[channel_idx].state;
}
```

### C++ Usage Example

```cpp
/* main_example.cpp */
#include "redundant_can.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <linux/can.h>

static rcan_manager_t g_rcan;

/* Periodic health-monitor thread (10 ms tick) */
void health_monitor_thread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        rcan_update_health(&g_rcan);

        for (int i = 0; i < RCAN_CHANNEL_COUNT; i++) {
            auto state = rcan_get_channel_state(&g_rcan, i);
            const char *labels[] = { "HEALTHY", "WARNING", "FAULTED", "DISABLED" };
            std::cout << "CH" << i << ": " << labels[state] << "\n";
        }
    }
}

int main() {
    /* Initialise with dual-TX: both channels transmit simultaneously */
    if (rcan_init(&g_rcan, "can0", "can1", /*dual_tx=*/true) < 0) {
        std::cerr << "Failed to initialise redundant CAN\n";
        return 1;
    }

    std::thread monitor(health_monitor_thread);
    monitor.detach();

    /* Application loop */
    can_frame tx_frame = {};
    tx_frame.can_id  = 0x100;
    tx_frame.can_dlc = 8;

    can_frame rx_frame = {};
    int rx_channel = -1;

    while (true) {
        /* Transmit sensor data on both channels */
        static uint8_t counter = 0;
        tx_frame.data[0] = counter++;
        if (rcan_send(&g_rcan, &tx_frame) < 0) {
            std::cerr << "All CAN channels faulted — entering safe state!\n";
            /* Application-specific safe state: e.g. apply brakes, alert operator */
        }

        /* Receive from the healthiest channel */
        if (rcan_recv(&g_rcan, &rx_frame, &rx_channel) > 0) {
            std::cout << "RX ID=0x" << std::hex << rx_frame.can_id
                      << " on CH" << rx_channel << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    rcan_close(&g_rcan);
    return 0;
}
```

### Dual-Channel Receive with Voting (C++)

For safety-critical data, compare values from both channels before trusting either:

```cpp
/* voting_receiver.cpp */
#include "redundant_can.h"
#include <cstring>
#include <cstdio>

/**
 * Perform a 2-of-2 consistency check on a received payload.
 *
 * Both channels must agree within `tolerance_bytes` for the message
 * to be considered valid.
 */
bool dual_channel_vote(const uint8_t *data_ch0, size_t len_ch0,
                        const uint8_t *data_ch1, size_t len_ch1,
                        uint8_t       *out_data)
{
    if (len_ch0 != len_ch1) {
        fprintf(stderr, "vote: DLC mismatch (%zu vs %zu)\n",
                len_ch0, len_ch1);
        return false;
    }

    if (memcmp(data_ch0, data_ch1, len_ch0) == 0) {
        /* Perfect agreement */
        memcpy(out_data, data_ch0, len_ch0);
        return true;
    }

    /*
     * Disagreement: log and reject.
     * In a TMR system, the majority value would be selected instead.
     * For safety-critical 2-channel systems, disagreement is itself a
     * fault that must be reported and handled.
     */
    fprintf(stderr, "vote: channel disagreement — data inconsistent\n");
    return false;
}

/**
 * Example usage: receive the same message from both channels within a
 * time window and vote on the result.
 */
void receive_with_voting(rcan_manager_t *mgr) {
    can_frame frames[RCAN_CHANNEL_COUNT] = {};
    bool      received[RCAN_CHANNEL_COUNT] = {};

    /* Collect frames from both channels within a short window */
    for (int attempt = 0; attempt < 100; attempt++) {
        can_frame tmp = {};
        int ch = -1;

        if (rcan_recv(mgr, &tmp, &ch) > 0) {
            if (ch >= 0 && ch < RCAN_CHANNEL_COUNT && !received[ch]) {
                frames[ch]   = tmp;
                received[ch] = true;
            }
        }

        if (received[0] && received[1]) break;
        /* Short spin delay — replace with select()/epoll() in production */
    }

    if (received[0] && received[1]) {
        uint8_t voted_data[8] = {};
        if (dual_channel_vote(frames[0].data, frames[0].can_dlc,
                               frames[1].data, frames[1].can_dlc,
                               voted_data)) {
            printf("Voted data accepted: 0x%02X 0x%02X ...\n",
                   voted_data[0], voted_data[1]);
        }
    } else if (received[0]) {
        printf("Only CH0 received — using with degraded confidence\n");
    } else if (received[1]) {
        printf("Only CH1 received — using with degraded confidence\n");
    } else {
        fprintf(stderr, "No data received on any channel — safe state!\n");
    }
}
```

---

## Implementation in Rust

Rust's ownership model and type system are excellent for encoding channel state transitions at compile time, preventing common bugs like accessing a faulted channel.

### Dependencies (`Cargo.toml`)

```toml
[package]
name    = "redundant_can"
version = "0.1.0"
edition = "2021"

[dependencies]
# socketcan provides safe SocketCAN bindings for Linux
socketcan = "3"
# For error handling
thiserror = "1"
# For atomic state flags
parking_lot = "0.12"
```

### Channel State and Error Types

```rust
// src/types.rs

use thiserror::Error;

/// Health state of a single CAN channel.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChannelState {
    Healthy,
    Warning,   // Error counters elevated
    Faulted,   // Bus-off or RX timeout
    Disabled,
}

impl std::fmt::Display for ChannelState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Healthy  => write!(f, "HEALTHY"),
            Self::Warning  => write!(f, "WARNING"),
            Self::Faulted  => write!(f, "FAULTED"),
            Self::Disabled => write!(f, "DISABLED"),
        }
    }
}

/// Errors produced by the redundant CAN manager.
#[derive(Debug, Error)]
pub enum RCanError {
    #[error("All CAN channels are faulted")]
    AllChannelsFaulted,

    #[error("Channel {0} is not available")]
    ChannelUnavailable(usize),

    #[error("Socket error on channel {channel}: {source}")]
    SocketError {
        channel: usize,
        #[source]
        source:  std::io::Error,
    },

    #[error("Channel disagreement: data mismatch between channels")]
    VoteDisagreement,

    #[error("No data available")]
    NoData,
}
```

### Per-Channel Context

```rust
// src/channel.rs

use std::time::{Duration, Instant};
use socketcan::{CanSocket, Socket, CanFrame};
use crate::types::{ChannelState, RCanError};

/// Thresholds
pub const TX_ERR_THRESHOLD: u32  = 96;
pub const RX_ERR_THRESHOLD: u32  = 96;
pub const FAULT_HOLD:        Duration = Duration::from_millis(200);
pub const MSG_TIMEOUT:       Duration = Duration::from_millis(50);

/// A single CAN channel with health tracking.
pub struct CanChannel {
    pub socket:        CanSocket,
    pub name:          String,
    pub state:         ChannelState,
    pub tx_errors:     u32,
    pub rx_errors:     u32,
    pub last_rx:       Instant,
    pub fault_since:   Option<Instant>,
    pub healthy_since: Instant,
}

impl CanChannel {
    /// Open a SocketCAN interface.
    pub fn open(ifname: &str) -> Result<Self, std::io::Error> {
        let socket = CanSocket::open(ifname)?;
        socket.set_nonblocking(true)?;

        Ok(Self {
            socket,
            name:          ifname.to_string(),
            state:         ChannelState::Healthy,
            tx_errors:     0,
            rx_errors:     0,
            last_rx:       Instant::now(),
            fault_since:   None,
            healthy_since: Instant::now(),
        })
    }

    /// Send a frame; increment error counter on failure.
    pub fn send(&mut self, frame: &CanFrame) -> Result<(), RCanError> {
        self.socket.write_frame(frame).map_err(|e| {
            self.tx_errors += 1;
            RCanError::SocketError { channel: 0, source: e }
        })
    }

    /// Non-blocking receive. Returns None if no frame is available.
    pub fn try_recv(&mut self) -> Option<CanFrame> {
        match self.socket.read_frame() {
            Ok(frame) => {
                self.last_rx = Instant::now();
                Some(frame)
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => None,
            Err(_) => {
                self.rx_errors += 1;
                None
            }
        }
    }

    /// Evaluate health and update state. Call from a periodic task.
    pub fn update_health(&mut self) {
        let now = Instant::now();

        // --- RX timeout ---
        if self.state == ChannelState::Healthy
            && now.duration_since(self.last_rx) > MSG_TIMEOUT
        {
            eprintln!("[{}] RX timeout — marking FAULTED", self.name);
            self.state       = ChannelState::Faulted;
            self.fault_since = Some(now);
        }

        // --- Elevated error counters ---
        if (self.tx_errors > TX_ERR_THRESHOLD
            || self.rx_errors > RX_ERR_THRESHOLD)
            && self.state == ChannelState::Healthy
        {
            self.state       = ChannelState::Warning;
            self.fault_since = Some(now);
        }

        // --- Recovery with hysteresis ---
        if matches!(self.state, ChannelState::Warning | ChannelState::Faulted) {
            let counters_ok = self.tx_errors <= TX_ERR_THRESHOLD / 2
                && self.rx_errors <= RX_ERR_THRESHOLD / 2;
            let rx_fresh    = now.duration_since(self.last_rx) < MSG_TIMEOUT / 2;

            if counters_ok && rx_fresh {
                if now.duration_since(self.healthy_since) >= FAULT_HOLD {
                    eprintln!("[{}] Recovered — marking HEALTHY", self.name);
                    self.state     = ChannelState::Healthy;
                    self.tx_errors = 0;
                    self.rx_errors = 0;
                    self.fault_since = None;
                }
            } else {
                // Reset hysteresis window
                self.healthy_since = now;
            }
        }
    }

    pub fn is_usable(&self) -> bool {
        matches!(self.state, ChannelState::Healthy | ChannelState::Warning)
    }
}
```

### Redundancy Manager

```rust
// src/manager.rs

use socketcan::CanFrame;
use crate::channel::CanChannel;
use crate::types::{ChannelState, RCanError};

/// Dual-CAN redundancy manager.
pub struct RedundantCanManager {
    channels:         Vec<CanChannel>,
    active_rx:        usize,   // Index of preferred RX channel
    dual_tx_enabled:  bool,
}

impl RedundantCanManager {
    /// Create a manager with two channels.
    ///
    /// # Arguments
    ///
    /// * `ch0` - Primary interface name (e.g. `"can0"`)
    /// * `ch1` - Secondary interface name (e.g. `"can1"`)
    /// * `dual_tx` - If true, frames are sent on both channels simultaneously
    pub fn new(ch0: &str, ch1: &str, dual_tx: bool) -> Result<Self, std::io::Error> {
        let channels = vec![
            CanChannel::open(ch0)?,
            CanChannel::open(ch1)?,
        ];
        Ok(Self {
            channels,
            active_rx:       0,
            dual_tx_enabled: dual_tx,
        })
    }

    /// Transmit a frame.
    ///
    /// In dual-TX mode, the frame is sent on every usable channel.
    /// In single-TX mode, only the active channel is used.
    /// Returns `Ok(())` if at least one channel succeeded.
    pub fn send(&mut self, frame: &CanFrame) -> Result<(), RCanError> {
        let mut sent = false;

        for (idx, ch) in self.channels.iter_mut().enumerate() {
            if !ch.is_usable() {
                continue;
            }
            if !self.dual_tx_enabled && idx != self.active_rx {
                continue;
            }

            if ch.send(frame).is_ok() {
                sent = true;
            }
        }

        if sent { Ok(()) } else { Err(RCanError::AllChannelsFaulted) }
    }

    /// Non-blocking receive.
    ///
    /// Tries the active channel first, then the other channel.
    /// Returns the frame and the channel index it arrived on.
    pub fn recv(&mut self) -> Result<(CanFrame, usize), RCanError> {
        let order: [usize; 2] = [self.active_rx, 1 - self.active_rx];

        for &idx in &order {
            let ch = &mut self.channels[idx];
            if !ch.is_usable() {
                continue;
            }
            if let Some(frame) = ch.try_recv() {
                return Ok((frame, idx));
            }
        }

        Err(RCanError::NoData)
    }

    /// Receive the same message from both channels and vote on consistency.
    ///
    /// Useful for safety-critical signals where split-brain must be detected.
    pub fn recv_voted(&mut self) -> Result<CanFrame, RCanError> {
        let mut frames: [Option<CanFrame>; 2] = [None, None];

        // Collect one frame from each channel
        for idx in 0..self.channels.len() {
            let ch = &mut self.channels[idx];
            if ch.is_usable() {
                frames[idx] = ch.try_recv();
            }
        }

        match (frames[0].take(), frames[1].take()) {
            (Some(f0), Some(f1)) => {
                // Both channels delivered — compare payloads
                if f0.data() == f1.data() && f0.id() == f1.id() {
                    Ok(f0)
                } else {
                    Err(RCanError::VoteDisagreement)
                }
            }
            (Some(f), None) => {
                eprintln!("rcan: only CH0 received — degraded confidence");
                Ok(f)
            }
            (None, Some(f)) => {
                eprintln!("rcan: only CH1 received — degraded confidence");
                Ok(f)
            }
            (None, None) => Err(RCanError::AllChannelsFaulted),
        }
    }

    /// Periodic health update. Call from a 10 ms timer task.
    pub fn update_health(&mut self) {
        for ch in &mut self.channels {
            ch.update_health();
        }

        // Re-select best active channel
        let best = self.channels.iter().enumerate()
            .find(|(_, ch)| ch.state == ChannelState::Healthy)
            .map(|(i, _)| i);

        if let Some(idx) = best {
            if idx != self.active_rx {
                eprintln!("rcan: active RX switched to channel {}", idx);
                self.active_rx = idx;
            }
        }
        // If best == None, all channels are degraded — application handles safe state
    }

    /// Query the state of a channel.
    pub fn channel_state(&self, idx: usize) -> Option<ChannelState> {
        self.channels.get(idx).map(|ch| ch.state)
    }
}
```

### Rust Usage Example

```rust
// src/main.rs

mod channel;
mod manager;
mod types;

use manager::RedundantCanManager;
use types::RCanError;
use socketcan::{CanFrame, StandardId};
use std::thread;
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialise with dual-TX on can0 and can1
    let mut rcan = RedundantCanManager::new("can0", "can1", true)?;

    // Spawn a health-monitor thread
    // (In production, this would share the manager via Arc<Mutex<>>)
    // Here we demonstrate inline periodic updates for clarity.

    let id = StandardId::new(0x100).expect("valid CAN ID");
    let mut counter: u8 = 0;

    loop {
        // --- Transmit ---
        let data = [counter, 0xAB, 0xCD, 0xEF, 0, 0, 0, 0];
        let frame = CanFrame::new(id.into(), &data).expect("valid frame");

        match rcan.send(&frame) {
            Ok(()) => {},
            Err(RCanError::AllChannelsFaulted) => {
                eprintln!("CRITICAL: All CAN channels faulted — safe state!");
                // Application-specific: apply mechanical brakes, alert operators, etc.
            }
            Err(e) => eprintln!("TX error: {}", e),
        }

        // --- Receive with voting ---
        match rcan.recv_voted() {
            Ok(frame) => {
                println!("Voted frame: ID=0x{:X}, data={:?}", frame.id(), frame.data());
            }
            Err(RCanError::VoteDisagreement) => {
                eprintln!("SAFETY: Channel disagreement detected — investigate");
            }
            Err(RCanError::AllChannelsFaulted) => {
                eprintln!("CRITICAL: No data on any channel");
            }
            Err(RCanError::NoData) => {} // Normal when no message pending
            Err(e) => eprintln!("RX error: {}", e),
        }

        // --- Health update (normally in a 10 ms periodic task) ---
        rcan.update_health();

        // Print channel states
        for i in 0..2 {
            if let Some(state) = rcan.channel_state(i) {
                println!("  CH{}: {}", i, state);
            }
        }

        counter = counter.wrapping_add(1);
        thread::sleep(Duration::from_millis(20));
    }
}
```

---

## Synchronization and Message Consistency

### Sequence Numbers

When both channels carry the same message, each frame should include a rolling sequence number so receivers can detect:

- Duplicates (same sequence from both channels — expected in dual-TX mode)
- Gaps (missed frames on one channel)
- Reordering (rare in CAN, but possible after a switchover)

```c
/* Add to the first byte of the CAN payload */
typedef struct {
    uint8_t  seq;          /* Rolling counter 0–255           */
    uint8_t  channel_id;   /* 0 = primary, 1 = secondary      */
    uint8_t  payload[6];   /* Application data                */
} redundant_can_msg_t;
```

### Timestamp Alignment

In dual-TX architectures, both channels transmit the same payload at the same logical time. Small jitter (< 1 bit time) is acceptable. Larger jitter can be caused by:

- Software scheduling delays
- Different clock domains for each CAN controller

Mitigate with hardware-triggered simultaneous transmission if the MCU supports it (e.g. STM32 FDCAN `TXBAR` register write to both peripherals in the same clock cycle).

### Deduplication Logic

```c
/* Receiver-side deduplication using sequence number */
typedef struct {
    uint8_t  last_seq;
    uint64_t last_ts_ms;
} dedup_state_t;

bool dedup_accept(dedup_state_t *state,
                  const redundant_can_msg_t *msg,
                  uint64_t now_ms)
{
    /* Accept if sequence advanced, or enough time has passed
     * to assume the sequence counter wrapped */
    if (msg->seq != state->last_seq ||
        (now_ms - state->last_ts_ms) > 500) {
        state->last_seq    = msg->seq;
        state->last_ts_ms  = now_ms;
        return true;   /* New unique message */
    }
    return false;  /* Duplicate — discard */
}
```

---

## Testing and Validation

### Fault Injection Checklist

| Test Case | Expected Behaviour |
|---|---|
| Disconnect CAN Bus 1 cable | Switchover to Bus 2 within < 1 ms; no message loss |
| Short CAN_H to GND on Bus 1 | Bus 1 enters bus-off; Bus 2 remains healthy |
| Introduce babbling node on Bus 1 | Bus 1 arbitration loss rate increases; system degrades to Bus 2 |
| Simultaneously fault both buses | System enters defined safe state; no silent data corruption |
| Restore Bus 1 after fault | Bus 1 re-qualifies after hysteresis period; dual-TX resumes |
| Inject mismatched payload on Bus 2 | Voting rejects message; fault logged |
| Power cycle one CAN transceiver | Channel briefly transitions Healthy → Faulted → Healthy |

### Hardware-in-the-Loop (HiL) Setup

```
┌─────────────────────────────────────────────────────┐
│  HiL Test PC                                        │
│  ┌──────────┐  CAN Bus 1  ┌───────────────────────┐ │
│  │ PEAK USB │━━━━━━━━━━━━━│  ECU Under Test       │ │
│  │ CAN If.  │             │  (Dual-CAN firmware)  │ │
│  │          │  CAN Bus 2  │                       │ │
│  │ PEAK USB │━━━━━━━━━━━━━│                       │ │
│  │ CAN If.  │             └───────────────────────┘ │
│  └──────────┘                                       │
│  Fault injection relay board between buses and ECU  │
└─────────────────────────────────────────────────────┘
```

Use tools such as **CANalyzer**, **BUSMASTER** (open source), or **python-can** to script fault injection and capture timing.

---

## Standards and Compliance

### ISO 26262 Considerations

For ASIL-C/D, the CAN communication path is typically classified as a **hardware element** and a **software element**. Dual-CAN satisfies several diagnostic requirements:

- **E2E Protection (End-to-End):** ISO 26262-1:2018 Part 6 recommends CRC, sequence counter, and data ID in every safety-relevant frame (typically AUTOSAR E2E Profile 2 or 4).
- **Independence:** Dual buses must be physically independent — shared connectors, shared PCB traces, or shared power rails violate independence.
- **Common-cause failure analysis (CCFA):** Document that a single environmental cause (vibration, temperature, EMI) cannot simultaneously disable both channels.

### AUTOSAR Integration

In AUTOSAR stacks, redundant CAN is typically realised through:

- **ComM (Communication Manager):** Manages channel active/standby state.
- **CanIf (CAN Interface):** Routes PDUs to one or both CAN controllers.
- **E2E Transformer:** Adds/checks E2E protection data at the PDU level.

---

## Summary

Redundant CAN networks are the backbone of fault-tolerant communication in safety-critical embedded systems. The core principles are:

**Architecture:** Use hot-standby dual-channel design where both channels transmit simultaneously. This ensures continuous health proof of the backup channel and enables zero-latency failover. Reserve passive standby for cost-constrained applications where instant failover is not required.

**Fault Detection:** Monitor TEC/REC error counters, bus-off state, and RX message timeouts in a periodic health task. Apply hysteresis before re-qualifying a recovered channel to avoid oscillation between states.

**Switchover:** Maintain an "active RX channel" pointer that the receiver always reads first. On fault detection, update this pointer. The transmitter should send on both channels at all times (dual-TX) so that switchover at the receiver side is instant.

**Data Consistency:** Protect against split-brain with payload voting. In 2-channel systems, a mismatch is itself a safety fault that must be logged and escalated. In TMR systems, the majority value is selected.

**Code Design:** In C, encode state transitions explicitly in a manager struct with periodic `update_health()` calls. In Rust, leverage the type system to make invalid channel access a compile-time error, and use `Result<T, E>` to force callers to handle all failure paths.

**Standards:** Apply AUTOSAR E2E protection on all safety-relevant PDUs. Ensure physical independence of the two buses and document common-cause failure exclusions as required by ISO 26262 / IEC 61508.

Redundant CAN is not a fire-and-forget feature — it must be continuously tested via fault injection, validated on HiL rigs, and reviewed against the relevant functional safety standards throughout the development lifecycle.

---

*Document: 91_Redundant_CAN_Networks.md — Part of the CAN Protocol Engineering Reference Series*