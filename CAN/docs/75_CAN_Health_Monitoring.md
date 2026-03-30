# 75. CAN Health Monitoring

**What's inside:**

- **Error Architecture** — explains TEC/REC counters, the exact ISO 11898-1 increment/decrement rules, and the three-state fault-confinement machine (Error Active → Error Passive → Bus-Off).
- **Error Frame Types** — Bit, Stuff, CRC, Form, and ACK errors, with a table mapping each to its likely root cause.
- **Signal Quality Metrics** — derived metrics beyond raw counters: error rate, bus load, burst detection, and composite health score.
- **C/C++ implementation** — three layers: SocketCAN error frame reception and parsing, bare-metal STM32 register-level ISR + periodic poll, and a C++ `CanHealthMonitor` class with sliding-window trend analysis and diagnostic rule engine.
- **Rust implementation** — strongly typed error kinds, a `CanHealthMonitor` with `VecDeque`-backed windowing, SocketCAN integration via the `socketcan` crate, and a `no_std` ring-buffer variant for bare-metal targets.
- **Predictive Failure Detection** — time-to-passive extrapolation, burst pattern detection, repeated bus-off lockout, and a health score alerting threshold table.
- **Summary** — concise synthesis of concepts, implementation strategy, and language trade-offs.


> Tracking error counters, bus-off events, and signal quality metrics to predict and prevent failures.

---

## Table of Contents

1. [Introduction](#introduction)
2. [CAN Error Architecture](#can-error-architecture)
3. [Error Counters: TEC and REC](#error-counters-tec-and-rec)
4. [Node States: Error Active, Error Passive, Bus-Off](#node-states)
5. [Error Frame Types](#error-frame-types)
6. [Signal Quality Metrics](#signal-quality-metrics)
7. [Health Monitoring Strategy](#health-monitoring-strategy)
8. [Implementation in C/C++](#implementation-in-cc)
9. [Implementation in Rust](#implementation-in-rust)
10. [Predictive Failure Detection](#predictive-failure-detection)
11. [Summary](#summary)

---

## Introduction

CAN (Controller Area Network) Health Monitoring is the practice of continuously observing the internal diagnostic state of CAN controllers and the statistical properties of the bus to detect degradation, predict impending failures, and trigger recovery or alert mechanisms before a hard fault causes system downtime.

Unlike application-level error handling (e.g., missing heartbeats), CAN health monitoring operates at the physical and data-link layer. It interrogates registers inside the CAN controller hardware, tracks trends in error counters, intercepts error interrupt events, and correlates these observations into health scores and alert conditions.

Health monitoring is critical in:
- **Automotive ECUs** – where bus-off can disable safety-critical functions (ADAS, ABS, airbag)
- **Industrial automation** – where a degraded node causes process errors before hard failure
- **Medical devices** – where predictive alerts allow scheduled maintenance
- **Aerospace** – where deterministic communication integrity is mandatory

---

## CAN Error Architecture

The CAN protocol (ISO 11898) defines a fault-confinement mechanism built directly into the protocol layer. Every CAN node contains two hardware counters maintained automatically by the CAN controller silicon:

- **TEC** – Transmit Error Counter
- **REC** – Receive Error Counter

These counters drive a state machine that moves each node through three error states. The counters are incremented and decremented according to precise rules defined in the CAN specification.

```
                    ┌─────────────────────────────────────────┐
                    │          CAN Controller Hardware         │
                    │                                          │
                    │  ┌──────────┐      ┌──────────────────┐ │
                    │  │  TEC     │      │  Error State FSM │ │
                    │  │ (0..255) │─────▶│  Error Active    │ │
                    │  └──────────┘      │  Error Passive   │ │
                    │  ┌──────────┐      │  Bus-Off         │ │
                    │  │  REC     │─────▶│                  │ │
                    │  │ (0..127) │      └──────────────────┘ │
                    │  └──────────┘                           │
                    └─────────────────────────────────────────┘
```

### Counter Increment/Decrement Rules (ISO 11898-1)

| Event | TEC Change | REC Change |
|---|---|---|
| Successful transmission | −1 (min 0) | — |
| Successful reception | — | −1 (min 0) |
| Transmitter detects error | +8 | — |
| Receiver detects error | — | +8 |
| Transmitter detects dominant bit after error flag | +8 | — |
| Receiver dominant bit after error flag | +8 | — |
| Bus-off recovery (128 × 11 recessive bits) | Reset to 0 | Reset to 0 |

---

## Error Counters: TEC and REC

The TEC and REC values are the primary health indicators available at all times, without needing to wait for a hard fault event.

### Thresholds

```
TEC / REC value    State transition
────────────────────────────────────────────
0   – 127          Error Active (normal)
128 – 255          Error Passive (degraded)
> 255 (TEC only)   Bus-Off (disconnected)
```

### Interpretation

A slowly rising TEC suggests the node is struggling to transmit: possible causes include bus contention, a faulty transceiver, or a termination problem. A rising REC indicates the node is receiving malformed frames, pointing to another node with a bad clock or a wiring fault. Both rising simultaneously indicates a global bus problem (missing termination, severe EMI).

Monitoring the **rate of change** (delta per second) is more informative than the absolute value:

```
dTEC/dt > threshold  →  Transceiver or bus load problem
dREC/dt > threshold  →  Upstream node clock or signal integrity issue
```

---

## Node States

### Error Active (Normal Operation)

- TEC < 128 AND REC < 128
- Node transmits **Active Error Frames** (6 dominant bits) on detecting an error
- Normal bus participation, errors are visible to all other nodes

### Error Passive (Degraded)

- TEC ≥ 128 OR REC ≥ 128
- Node transmits **Passive Error Frames** (6 recessive bits), which are less disruptive
- Node must wait an additional **Suspend Transmission** period (8 recessive bits) before retransmitting
- The node is still on the bus but operating in a limited capacity

### Bus-Off (Disconnected)

- TEC > 255
- The node **disconnects from the bus entirely** — it can neither transmit nor receive
- Recovery requires the node to observe 128 consecutive sequences of 11 recessive bits (approximately 128 ms at 1 Mbit/s)
- Recovery can be automatic or require software intervention

```
                 TEC ≥ 128
Error Active ──────────────▶ Error Passive
     ▲               TEC < 128      │
     │◀──────────────────────────────│
     │                              │ TEC > 255
     │                              ▼
     │                          Bus-Off
     │    128 × 11 recessive bits   │
     └──────────────────────────────┘
```

---

## Error Frame Types

Beyond the counter values, monitoring which *kind* of error is occurring provides diagnostic specificity.

| Error Type | Cause | Detected By |
|---|---|---|
| **Bit Error** | Node reads back a different bit than it transmitted | Transmitter |
| **Stuff Error** | > 5 consecutive identical bits (bit stuffing violation) | Any node |
| **CRC Error** | Calculated CRC ≠ received CRC | Receiver |
| **Form Error** | Fixed-form field has wrong value (EOF, delimiter) | Any node |
| **Acknowledgment Error** | No dominant ACK bit received | Transmitter |

Many CAN controllers expose error type registers (e.g., SocketCAN's `can_berr_counter`, STM32's `CAN_ESR` register). These allow targeted diagnostics:

- **Frequent ACK errors** → no other node present, or all others in bus-off
- **Frequent CRC errors** → clock mismatch, EMI, signal integrity degradation
- **Frequent Stuff errors** → severe bit distortion or transceiver fault
- **Frequent Bit errors** → transceiver drive strength, short circuits

---

## Signal Quality Metrics

Beyond the built-in error counters, comprehensive health monitoring tracks derived metrics:

### 1. Error Rate (Errors Per Second)

```
error_rate = (TEC_delta + REC_delta) / sampling_interval
```

Alert if error rate exceeds a rolling baseline by a configurable multiplier.

### 2. Bus Load (%)

The fraction of time the bus is occupied. High bus load increases collision probability. Measured via hardware timestamping or DMA capture.

### 3. Error Burst Detection

A sudden spike in errors (rather than a steady rise) suggests transient EMI. Track error timestamps and detect bursts using a sliding window.

### 4. Successive Bus-Off Events

Count how many times a node has entered bus-off. Repeated bus-off cycles indicate an unresolved chronic fault.

### 5. Recovery Time

Measure the duration of each bus-off episode. A node that recovers and immediately re-enters bus-off has a hard fault, not a transient one.

### 6. Health Score (Composite)

A weighted composite metric normalizing all of the above to a 0–100 value. Below a threshold, raise a predictive maintenance alert.

---

## Health Monitoring Strategy

A practical health monitoring implementation uses a layered approach:

```
┌───────────────────────────────────────────────────────┐
│                  Application Layer                     │
│        Health score, alarms, logging, HMI             │
├───────────────────────────────────────────────────────┤
│                 Analysis Layer                         │
│    Rate-of-change, burst detection, trend analysis    │
├───────────────────────────────────────────────────────┤
│                 Collection Layer                       │
│   Periodic polling of TEC/REC, error interrupts,      │
│   bus-off ISR, timestamp capture                      │
├───────────────────────────────────────────────────────┤
│              CAN Controller Hardware                   │
│   TEC, REC, ESR, interrupt flags, error type regs    │
└───────────────────────────────────────────────────────┘
```

**Polling interval:** 10–100 ms is typical. Faster intervals catch transient spikes; slower intervals reduce CPU overhead. For critical systems, supplement polling with interrupt-driven capture.

**Interrupt sources to handle:**
- Error Passive entry/exit
- Bus-Off entry
- Error Warning (TEC or REC ≥ 96, a pre-passive warning on many controllers)

---

## Implementation in C/C++

The following examples target Linux SocketCAN (portable across embedded Linux platforms) and also show a bare-metal STM32-style register-access pattern.

### SocketCAN: Reading Error Counters

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <linux/can/netlink.h>
#include <libmnl/libmnl.h>   /* for netlink stats, optional */

/* Error counter structure matching kernel's can_berr_counter */
struct can_berr_counter {
    uint16_t txerr;  /* Transmit Error Counter */
    uint16_t rxerr;  /* Receive Error Counter */
};

/* Read TEC/REC from SocketCAN via ioctl */
int can_get_berr_counter(const char *ifname, struct can_berr_counter *bc) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) return -1;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    /* SIOCGCANSTATE / custom ioctl — use ip link show can0 or netlink */
    /* On many kernels, berr is exposed via netlink (ip -d link show can0) */
    /* For raw access in embedded, use hardware register directly */

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return -1;
    }

    close(fd);
    /* Real berr reading uses rtnetlink; simplified here */
    (void)bc;
    return 0;
}
```

### SocketCAN: Receiving Error Frames

SocketCAN delivers error conditions as special CAN frames with the `CAN_ERR_FLAG` set. This is the most portable way to monitor CAN health on Linux.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

/* ─── Health state ──────────────────────────────────────────────── */

typedef enum {
    CAN_HEALTH_OK       = 0,
    CAN_HEALTH_WARNING  = 1,   /* TEC or REC > 96  */
    CAN_HEALTH_PASSIVE  = 2,   /* TEC or REC >= 128 */
    CAN_HEALTH_BUS_OFF  = 3,
} can_health_state_t;

typedef struct {
    uint32_t bus_off_count;
    uint32_t error_passive_count;
    uint32_t bit_errors;
    uint32_t stuff_errors;
    uint32_t crc_errors;
    uint32_t form_errors;
    uint32_t ack_errors;
    uint32_t total_errors;
    uint8_t  last_tec;
    uint8_t  last_rec;
    can_health_state_t state;
    time_t   last_bus_off_ts;
    time_t   last_recovery_ts;
} can_health_t;

/* ─── Error frame parser ────────────────────────────────────────── */

static void parse_error_frame(const struct can_frame *frame,
                               can_health_t *health)
{
    canid_t err = frame->can_id & CAN_ERR_MASK;

    health->total_errors++;

    if (err & CAN_ERR_TX_TIMEOUT) {
        printf("[HEALTH] TX timeout (ACK error)\n");
        health->ack_errors++;
    }

    if (err & CAN_ERR_LOSTARB) {
        /* Lost arbitration — usually benign on a busy bus */
        printf("[HEALTH] Lost arbitration at bit %u\n",
               frame->data[0]);
    }

    if (err & CAN_ERR_CRTL) {
        uint8_t ctrl = frame->data[1];
        if (ctrl & CAN_ERR_CRTL_RX_PASSIVE) {
            printf("[HEALTH] RX Error Passive\n");
            health->state = CAN_HEALTH_PASSIVE;
            health->error_passive_count++;
        }
        if (ctrl & CAN_ERR_CRTL_TX_PASSIVE) {
            printf("[HEALTH] TX Error Passive\n");
            health->state = CAN_HEALTH_PASSIVE;
            health->error_passive_count++;
        }
        if (ctrl & CAN_ERR_CRTL_RX_WARNING) {
            printf("[HEALTH] RX Warning (REC > 96)\n");
            if (health->state < CAN_HEALTH_WARNING)
                health->state = CAN_HEALTH_WARNING;
        }
        if (ctrl & CAN_ERR_CRTL_TX_WARNING) {
            printf("[HEALTH] TX Warning (TEC > 96)\n");
            if (health->state < CAN_HEALTH_WARNING)
                health->state = CAN_HEALTH_WARNING;
        }
        if (ctrl & CAN_ERR_CRTL_ACTIVE) {
            printf("[HEALTH] Error Active (recovered from passive)\n");
            health->state = CAN_HEALTH_OK;
            health->last_recovery_ts = time(NULL);
        }
    }

    if (err & CAN_ERR_PROT) {
        uint8_t prot_type  = frame->data[2];
        uint8_t prot_loc   = frame->data[3];

        if (prot_type & CAN_ERR_PROT_BIT)   { health->bit_errors++;   printf("[HEALTH] Bit error\n"); }
        if (prot_type & CAN_ERR_PROT_FORM)  { health->form_errors++;  printf("[HEALTH] Form error\n"); }
        if (prot_type & CAN_ERR_PROT_STUFF) { health->stuff_errors++; printf("[HEALTH] Stuff error\n"); }
        if (prot_type & CAN_ERR_PROT_CRC)   { health->crc_errors++;   printf("[HEALTH] CRC error at location 0x%02X\n", prot_loc); }

        (void)prot_loc;
    }

    if (err & CAN_ERR_TRX) {
        printf("[HEALTH] Transceiver error: 0x%02X\n", frame->data[4]);
    }

    if (err & CAN_ERR_BUSOFF) {
        printf("[HEALTH] *** BUS-OFF ***\n");
        health->state          = CAN_HEALTH_BUS_OFF;
        health->bus_off_count++;
        health->last_bus_off_ts = time(NULL);
    }

    if (err & CAN_ERR_RESTARTED) {
        printf("[HEALTH] Controller restarted (bus-off recovery)\n");
        health->state             = CAN_HEALTH_OK;
        health->last_recovery_ts  = time(NULL);
    }
}

/* ─── Monitor loop ──────────────────────────────────────────────── */

int can_health_monitor(const char *ifname)
{
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return -1; }

    /* Enable reception of error frames */
    can_err_mask_t err_mask = CAN_ERR_MASK;   /* all error types */
    if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                   &err_mask, sizeof(err_mask)) < 0) {
        perror("setsockopt err_mask");
        close(fd); return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }

    can_health_t health = {0};
    health.state = CAN_HEALTH_OK;

    printf("[HEALTH] Monitoring %s ...\n", ifname);

    struct can_frame frame;
    while (1) {
        ssize_t nbytes = read(fd, &frame, sizeof(frame));
        if (nbytes < 0) { perror("read"); break; }

        if (frame.can_id & CAN_ERR_FLAG) {
            parse_error_frame(&frame, &health);

            /* Print summary */
            printf("[HEALTH] State=%-10s  TotalErr=%-6u  BusOff=%-4u"
                   "  Passive=%-4u  CRC=%-4u  Bit=%-4u  Stuff=%-4u\n",
                   health.state == CAN_HEALTH_OK      ? "OK"      :
                   health.state == CAN_HEALTH_WARNING  ? "WARNING" :
                   health.state == CAN_HEALTH_PASSIVE  ? "PASSIVE" : "BUS-OFF",
                   health.total_errors,
                   health.bus_off_count,
                   health.error_passive_count,
                   health.crc_errors,
                   health.bit_errors,
                   health.stuff_errors);
        }
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *iface = (argc > 1) ? argv[1] : "can0";
    return can_health_monitor(iface);
}
```

### Bare-Metal STM32: Register-Level Health Monitoring

On microcontrollers without an OS, health monitoring reads the CAN controller's Error Status Register (ESR) and sets up the Error interrupt.

```c
/* STM32 bxCAN register definitions (simplified) */
/* In practice, use ST's HAL or CMSIS headers     */

#define CAN_BASE        0x40006400UL
#define CAN_MCR         (*(volatile uint32_t *)(CAN_BASE + 0x000))
#define CAN_MSR         (*(volatile uint32_t *)(CAN_BASE + 0x004))
#define CAN_TSR         (*(volatile uint32_t *)(CAN_BASE + 0x008))
#define CAN_IER         (*(volatile uint32_t *)(CAN_BASE + 0x014))
#define CAN_ESR         (*(volatile uint32_t *)(CAN_BASE + 0x018))

/* ESR bit fields */
#define CAN_ESR_EWGF    (1U << 0)   /* Error Warning Flag   TEC|REC >= 96 */
#define CAN_ESR_EPVF    (1U << 1)   /* Error Passive Flag   TEC|REC >= 128*/
#define CAN_ESR_BOFF    (1U << 2)   /* Bus-Off Flag                       */
#define CAN_ESR_LEC_MSK (7U << 4)   /* Last Error Code [6:4]              */
#define CAN_ESR_TEC_MSK (0xFFU << 16) /* TEC [23:16]                      */
#define CAN_ESR_REC_MSK (0x7FU << 24) /* REC [30:24]                      */

/* Last Error Code values */
typedef enum {
    LEC_NONE   = 0,
    LEC_STUFF  = 1,
    LEC_FORM   = 2,
    LEC_ACK    = 3,
    LEC_BIT1   = 4,  /* Dominant bit expected, recessive seen */
    LEC_BIT0   = 5,  /* Recessive bit expected, dominant seen */
    LEC_CRC    = 6,
    LEC_NOERR  = 7,  /* No error since last read */
} can_lec_t;

/* IER bits for error interrupts */
#define CAN_IER_EWGIE   (1U << 8)   /* Error Warning Interrupt Enable  */
#define CAN_IER_EPVIE   (1U << 9)   /* Error Passive Interrupt Enable  */
#define CAN_IER_BOFIE   (1U << 10)  /* Bus-Off Interrupt Enable        */
#define CAN_IER_LECIE   (1U << 11)  /* Last Error Code Interrupt Enable*/
#define CAN_IER_ERRIE   (1U << 15)  /* Error Interrupt Enable (master) */

/* ─── Health counters ───────────────────────────────────────────── */

typedef struct {
    uint32_t bus_off_count;
    uint32_t passive_count;
    uint32_t warning_count;
    uint32_t lec_counts[8];    /* Indexed by LEC value */
    uint8_t  tec_max_seen;
    uint8_t  rec_max_seen;
    uint8_t  consecutive_busoff;
} stm32_can_health_t;

static volatile stm32_can_health_t g_health;

/* ─── Register read helpers ─────────────────────────────────────── */

static inline uint8_t can_get_tec(void) {
    return (uint8_t)((CAN_ESR & CAN_ESR_TEC_MSK) >> 16);
}

static inline uint8_t can_get_rec(void) {
    return (uint8_t)((CAN_ESR & CAN_ESR_REC_MSK) >> 24);
}

static inline can_lec_t can_get_lec(void) {
    return (can_lec_t)((CAN_ESR & CAN_ESR_LEC_MSK) >> 4);
}

/* ─── Interrupt enable setup ────────────────────────────────────── */

void can_health_init(void) {
    /* Enable all error interrupt sources */
    CAN_IER |= CAN_IER_EWGIE | CAN_IER_EPVIE |
               CAN_IER_BOFIE | CAN_IER_LECIE |
               CAN_IER_ERRIE;

    /* Enable CEC interrupt in NVIC (platform-specific) */
    /* NVIC_EnableIRQ(CAN1_SCE_IRQn); */
}

/* ─── Error ISR (CAN1_SCE_IRQHandler on STM32) ──────────────────── */

void CAN1_SCE_IRQHandler(void)
{
    uint32_t esr = CAN_ESR;
    uint8_t  tec = (uint8_t)((esr & CAN_ESR_TEC_MSK) >> 16);
    uint8_t  rec = (uint8_t)((esr & CAN_ESR_REC_MSK) >> 24);
    can_lec_t lec = (can_lec_t)((esr & CAN_ESR_LEC_MSK) >> 4);

    /* Track peak counters */
    if (tec > g_health.tec_max_seen) g_health.tec_max_seen = tec;
    if (rec > g_health.rec_max_seen) g_health.rec_max_seen = rec;

    /* Accumulate LEC statistics */
    if (lec != LEC_NOERR && lec != LEC_NONE) {
        g_health.lec_counts[lec]++;
    }

    if (esr & CAN_ESR_BOFF) {
        g_health.bus_off_count++;
        g_health.consecutive_busoff++;

        /* If repeated bus-off: do NOT auto-recover, alert application */
        if (g_health.consecutive_busoff >= 3) {
            /* Signal application — do not attempt further recovery */
            /* E.g., set a flag for the main task, post RTOS event */
        } else {
            /* Request automatic recovery: set ABOM (Auto Bus-Off Mgmt) */
            /* or manually: CAN_MCR |= CAN_MCR_INRQ then clear */
        }
    }

    if (esr & CAN_ESR_EPVF) {
        g_health.passive_count++;
    }

    if (esr & CAN_ESR_EWGF) {
        g_health.warning_count++;
    }

    /* Clear error interrupt flags (write 1 to clear in MSR) */
    /* Platform-specific; e.g.: */
    /* CAN_MSR |= CAN_MSR_ERRI; */
}

/* ─── Periodic health poll (call from 100 ms task) ──────────────── */

typedef struct {
    uint8_t  tec;
    uint8_t  rec;
    uint8_t  health_score;   /* 0 = critical, 100 = perfect */
    const char *state_str;
} can_health_report_t;

can_health_report_t can_health_poll(void)
{
    can_health_report_t report;
    uint32_t esr = CAN_ESR;

    report.tec = (uint8_t)((esr & CAN_ESR_TEC_MSK) >> 16);
    report.rec = (uint8_t)((esr & CAN_ESR_REC_MSK) >> 24);

    /* Determine state string */
    if (esr & CAN_ESR_BOFF) {
        report.state_str  = "BUS-OFF";
        report.health_score = 0;
    } else if (esr & CAN_ESR_EPVF) {
        report.state_str  = "ERROR-PASSIVE";
        report.health_score = 25;
    } else if (esr & CAN_ESR_EWGF) {
        report.state_str  = "WARNING";
        report.health_score = 60;
    } else {
        report.state_str  = "OK";
        /* Score degrades as TEC/REC approach 96 */
        uint8_t worst = (report.tec > report.rec) ? report.tec : report.rec;
        report.health_score = (uint8_t)(100U - (worst * 100U / 96U));
        if (report.health_score > 100) report.health_score = 100;
    }

    return report;
}
```

### C++: Health Monitor Class with Trend Analysis

```cpp
#include <cstdint>
#include <deque>
#include <numeric>
#include <functional>
#include <chrono>
#include <string>
#include <iostream>
#include <iomanip>

/* ─── Error sample ──────────────────────────────────────────────── */

struct CanErrorSample {
    std::chrono::steady_clock::time_point timestamp;
    uint8_t  tec;
    uint8_t  rec;
    uint32_t total_errors;
};

/* ─── Health monitor class ──────────────────────────────────────── */

class CanHealthMonitor {
public:
    enum class AlertLevel { OK, WARNING, PASSIVE, BUS_OFF, CRITICAL };

    struct HealthReport {
        AlertLevel level;
        uint8_t    tec;
        uint8_t    rec;
        float      tec_rate;      /* TEC increase per second */
        float      rec_rate;      /* REC increase per second */
        float      error_rate;    /* Errors per second */
        uint32_t   bus_off_count;
        float      health_score;  /* 0.0–1.0 */
        std::string diagnosis;
    };

    using AlertCallback = std::function<void(const HealthReport &)>;

    CanHealthMonitor(size_t window_size = 30,
                     AlertCallback cb   = nullptr)
        : window_size_(window_size)
        , alert_cb_(cb)
    {}

    /* Feed a new sample (call periodically, e.g. every 100 ms) */
    void update(uint8_t tec, uint8_t rec, uint32_t total_errors,
                AlertLevel level)
    {
        auto now = std::chrono::steady_clock::now();
        samples_.push_back({ now, tec, rec, total_errors });
        if (samples_.size() > window_size_)
            samples_.pop_front();

        if (level == AlertLevel::BUS_OFF) {
            bus_off_count_++;
            if (!in_bus_off_) {
                in_bus_off_ = true;
                bus_off_entry_time_ = now;
            }
        } else {
            if (in_bus_off_) {
                /* Just recovered */
                in_bus_off_ = false;
                auto duration = std::chrono::duration_cast<
                    std::chrono::milliseconds>(now - bus_off_entry_time_).count();
                std::cout << "[HEALTH] Bus-off recovery after " << duration << " ms\n";
            }
        }

        HealthReport report = build_report(level);

        if (report.level >= AlertLevel::WARNING && alert_cb_)
            alert_cb_(report);
    }

    HealthReport build_report(AlertLevel current_level) const {
        HealthReport r{};
        r.level         = current_level;
        r.bus_off_count = bus_off_count_;

        if (samples_.empty()) return r;

        const auto &latest = samples_.back();
        r.tec = latest.tec;
        r.rec = latest.rec;

        /* Rate of change over the window */
        if (samples_.size() >= 2) {
            const auto &oldest = samples_.front();
            float dt = std::chrono::duration<float>(
                latest.timestamp - oldest.timestamp).count();

            if (dt > 0.0f) {
                r.tec_rate   = (latest.tec - oldest.tec) / dt;
                r.rec_rate   = (latest.rec - oldest.rec) / dt;
                r.error_rate = static_cast<float>(
                    latest.total_errors - oldest.total_errors) / dt;
            }
        }

        /* Composite health score */
        float tec_factor  = 1.0f - (r.tec / 255.0f);
        float rec_factor  = 1.0f - (r.rec / 127.0f);
        float busoff_pen  = 1.0f - std::min(1.0f, bus_off_count_ * 0.25f);
        float rate_pen    = 1.0f - std::min(1.0f, r.error_rate / 100.0f);
        r.health_score = (tec_factor + rec_factor + busoff_pen + rate_pen) / 4.0f;

        /* Diagnosis */
        if (current_level == AlertLevel::BUS_OFF) {
            r.diagnosis = "BUS-OFF: Node disconnected. Check wiring and termination.";
        } else if (r.tec_rate > 5.0f && r.rec_rate < 1.0f) {
            r.diagnosis = "Rising TEC: Possible transceiver or short-circuit fault.";
        } else if (r.rec_rate > 5.0f && r.tec_rate < 1.0f) {
            r.diagnosis = "Rising REC: Upstream node clock or signal integrity issue.";
        } else if (r.tec_rate > 3.0f && r.rec_rate > 3.0f) {
            r.diagnosis = "Both counters rising: Bus termination or EMI problem.";
        } else if (bus_off_count_ >= 3) {
            r.diagnosis = "Repeated bus-off: Chronic hardware fault. Manual inspection required.";
            r.level = AlertLevel::CRITICAL;
        } else {
            r.diagnosis = "Nominal.";
        }

        return r;
    }

    void print_report(const HealthReport &r) const {
        static const char *level_str[] = {
            "OK", "WARNING", "PASSIVE", "BUS-OFF", "CRITICAL"
        };
        std::cout << std::fixed << std::setprecision(2)
                  << "[HEALTH] Level=" << level_str[static_cast<int>(r.level)]
                  << "  TEC=" << static_cast<int>(r.tec)
                  << "  REC=" << static_cast<int>(r.rec)
                  << "  TEC_rate=" << r.tec_rate << "/s"
                  << "  REC_rate=" << r.rec_rate << "/s"
                  << "  ErrRate=" << r.error_rate << "/s"
                  << "  BusOff=" << r.bus_off_count
                  << "  Score=" << (r.health_score * 100.0f) << "%"
                  << "\n  Diagnosis: " << r.diagnosis << "\n";
    }

private:
    std::deque<CanErrorSample> samples_;
    size_t      window_size_;
    AlertCallback alert_cb_;
    uint32_t    bus_off_count_  = 0;
    bool        in_bus_off_     = false;
    std::chrono::steady_clock::time_point bus_off_entry_time_;
};
```

---

## Implementation in Rust

Rust's ownership model and type system make it well suited for safe CAN health monitoring — especially on embedded targets (using `embedded-hal` traits) and on Linux (using the `socketcan` crate).

### Cargo.toml Dependencies

```toml
[dependencies]
# Linux SocketCAN interface
socketcan = "3"

# Embedded (no_std) — choose one:
# embedded-hal = "1"
# nb = "1"

# Async runtime (optional, for async monitoring)
tokio = { version = "1", features = ["full"], optional = true }
```

### Core Health State Types

```rust
use std::time::{Duration, Instant};

/// CAN error counter state (mirrors ISO 11898-1 fault confinement)
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum CanNodeState {
    ErrorActive,   // TEC < 128 && REC < 128  — normal
    ErrorPassive,  // TEC >= 128 || REC >= 128 — degraded
    BusOff,        // TEC > 255               — disconnected
}

impl CanNodeState {
    pub fn from_counters(tec: u8, rec: u8, bus_off: bool) -> Self {
        if bus_off {
            Self::BusOff
        } else if tec >= 128 || rec >= 128 {
            Self::ErrorPassive
        } else {
            Self::ErrorActive
        }
    }

    pub fn is_degraded(self) -> bool {
        self != CanNodeState::ErrorActive
    }
}

/// Classification of error types (from LEC or error frame)
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CanErrorKind {
    BitError,
    StuffError,
    CrcError,
    FormError,
    AckError,
    LostArbitration,
    TransceiverFault,
    BusOff,
    Unknown,
}

/// A single health measurement snapshot
#[derive(Debug, Clone)]
pub struct HealthSample {
    pub timestamp: Instant,
    pub tec: u8,
    pub rec: u8,
    pub state: CanNodeState,
    pub total_errors: u64,
}

/// Accumulated error counters by kind
#[derive(Debug, Default, Clone)]
pub struct ErrorCounts {
    pub bit_errors: u64,
    pub stuff_errors: u64,
    pub crc_errors: u64,
    pub form_errors: u64,
    pub ack_errors: u64,
    pub bus_off_events: u64,
    pub lost_arb_events: u64,
    pub transceiver_faults: u64,
}

impl ErrorCounts {
    pub fn total(&self) -> u64 {
        self.bit_errors
            + self.stuff_errors
            + self.crc_errors
            + self.form_errors
            + self.ack_errors
            + self.bus_off_events
    }

    pub fn record(&mut self, kind: CanErrorKind) {
        match kind {
            CanErrorKind::BitError       => self.bit_errors += 1,
            CanErrorKind::StuffError     => self.stuff_errors += 1,
            CanErrorKind::CrcError       => self.crc_errors += 1,
            CanErrorKind::FormError      => self.form_errors += 1,
            CanErrorKind::AckError       => self.ack_errors += 1,
            CanErrorKind::BusOff         => self.bus_off_events += 1,
            CanErrorKind::LostArbitration => self.lost_arb_events += 1,
            CanErrorKind::TransceiverFault => self.transceiver_faults += 1,
            CanErrorKind::Unknown        => {}
        }
    }
}
```

### Health Monitor with Trend Analysis

```rust
use std::collections::VecDeque;

/// Sliding-window health monitor
pub struct CanHealthMonitor {
    window: VecDeque<HealthSample>,
    window_size: usize,
    pub error_counts: ErrorCounts,
    pub bus_off_count: u32,
    pub consecutive_bus_off: u32,
    last_bus_off: Option<Instant>,
    last_recovery: Option<Instant>,
}

/// Computed health report derived from the monitoring window
#[derive(Debug, Clone)]
pub struct HealthReport {
    pub state: CanNodeState,
    pub tec: u8,
    pub rec: u8,
    /// Rate of TEC increase in counts/second
    pub tec_rate: f32,
    /// Rate of REC increase in counts/second  
    pub rec_rate: f32,
    /// Error events per second
    pub error_rate: f32,
    /// Composite score: 1.0 = perfect, 0.0 = critical
    pub health_score: f32,
    pub diagnosis: Diagnosis,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Diagnosis {
    Nominal,
    TransceiverFault,
    UpstreamClockIssue,
    BusTerminationOrEmi,
    RepeatedBusOff,
    BusOffActive,
}

impl std::fmt::Display for Diagnosis {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Diagnosis::Nominal            => write!(f, "Nominal"),
            Diagnosis::TransceiverFault   => write!(f, "Rising TEC: Possible transceiver fault"),
            Diagnosis::UpstreamClockIssue => write!(f, "Rising REC: Upstream clock or signal issue"),
            Diagnosis::BusTerminationOrEmi=> write!(f, "Both rising: Bus termination or EMI problem"),
            Diagnosis::RepeatedBusOff     => write!(f, "Repeated bus-off: Chronic hardware fault"),
            Diagnosis::BusOffActive       => write!(f, "BUS-OFF: Node disconnected"),
        }
    }
}

impl CanHealthMonitor {
    pub fn new(window_size: usize) -> Self {
        Self {
            window: VecDeque::with_capacity(window_size),
            window_size,
            error_counts: ErrorCounts::default(),
            bus_off_count: 0,
            consecutive_bus_off: 0,
            last_bus_off: None,
            last_recovery: None,
        }
    }

    /// Record a new error event (call from interrupt handler or reader task)
    pub fn record_error(&mut self, kind: CanErrorKind) {
        self.error_counts.record(kind);
        if kind == CanErrorKind::BusOff {
            self.bus_off_count += 1;
            self.consecutive_bus_off += 1;
            self.last_bus_off = Some(Instant::now());
        }
    }

    /// Record a successful recovery from bus-off
    pub fn record_recovery(&mut self) {
        self.consecutive_bus_off = 0;
        self.last_recovery = Some(Instant::now());
    }

    /// Push a new counter snapshot (call from polling task, e.g. every 100 ms)
    pub fn push_sample(&mut self, tec: u8, rec: u8, bus_off: bool) {
        let sample = HealthSample {
            timestamp: Instant::now(),
            tec,
            rec,
            state: CanNodeState::from_counters(tec, rec, bus_off),
            total_errors: self.error_counts.total(),
        };

        if self.window.len() >= self.window_size {
            self.window.pop_front();
        }
        self.window.push_back(sample);
    }

    /// Build a health report from the current window
    pub fn report(&self) -> Option<HealthReport> {
        let latest = self.window.back()?;
        let oldest = self.window.front()?;

        let dt = latest.timestamp.duration_since(oldest.timestamp).as_secs_f32();

        let (tec_rate, rec_rate, error_rate) = if self.window.len() >= 2 && dt > 0.0 {
            (
                (latest.tec as f32 - oldest.tec as f32) / dt,
                (latest.rec as f32 - oldest.rec as f32) / dt,
                (latest.total_errors as f32 - oldest.total_errors as f32) / dt,
            )
        } else {
            (0.0, 0.0, 0.0)
        };

        // Composite health score (0.0–1.0)
        let tec_factor  = 1.0 - (latest.tec as f32 / 255.0);
        let rec_factor  = 1.0 - (latest.rec as f32 / 127.0);
        let busoff_pen  = (1.0 - (self.bus_off_count as f32 * 0.25)).clamp(0.0, 1.0);
        let rate_pen    = (1.0 - (error_rate / 100.0)).clamp(0.0, 1.0);
        let health_score = (tec_factor + rec_factor + busoff_pen + rate_pen) / 4.0;

        // Diagnosis
        let diagnosis = match latest.state {
            CanNodeState::BusOff => Diagnosis::BusOffActive,
            _ if self.consecutive_bus_off >= 3 => Diagnosis::RepeatedBusOff,
            _ if tec_rate > 5.0 && rec_rate < 1.0 => Diagnosis::TransceiverFault,
            _ if rec_rate > 5.0 && tec_rate < 1.0 => Diagnosis::UpstreamClockIssue,
            _ if tec_rate > 3.0 && rec_rate > 3.0 => Diagnosis::BusTerminationOrEmi,
            _ => Diagnosis::Nominal,
        };

        Some(HealthReport {
            state: latest.state,
            tec: latest.tec,
            rec: latest.rec,
            tec_rate,
            rec_rate,
            error_rate,
            health_score,
            diagnosis,
        })
    }

    /// Returns true if the situation requires immediate operator attention
    pub fn is_critical(&self) -> bool {
        self.consecutive_bus_off >= 3
    }
}

impl std::fmt::Display for HealthReport {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "[HEALTH] State={:?}  TEC={}  REC={}  \
             TEC_rate={:.2}/s  REC_rate={:.2}/s  \
             ErrRate={:.2}/s  Score={:.0}%\n  Diagnosis: {}",
            self.state,
            self.tec,
            self.rec,
            self.tec_rate,
            self.rec_rate,
            self.error_rate,
            self.health_score * 100.0,
            self.diagnosis,
        )
    }
}
```

### SocketCAN Error Frame Parsing in Rust

```rust
use socketcan::{CanSocket, Socket, CanFrame, CanError};

/// Map SocketCAN error flags to our CanErrorKind
fn classify_error(err: &CanError) -> CanErrorKind {
    // socketcan crate exposes structured error info
    match err {
        CanError::TransmitTimeout          => CanErrorKind::AckError,
        CanError::LostArbitration(_)       => CanErrorKind::LostArbitration,
        CanError::ControllerProblem(_)     => CanErrorKind::TransceiverFault,
        CanError::ProtocolViolation(pv) => {
            use socketcan::ProtocolViolationError::*;
            match pv {
                BitError           => CanErrorKind::BitError,
                BitStuffingError   => CanErrorKind::StuffError,
                FrameFormatError   => CanErrorKind::FormError,
                CrcError           => CanErrorKind::CrcError,
                _                  => CanErrorKind::Unknown,
            }
        },
        CanError::BusOff               => CanErrorKind::BusOff,
        CanError::TransceiverError(_)  => CanErrorKind::TransceiverFault,
        _                              => CanErrorKind::Unknown,
    }
}

/// Monitoring task — runs in its own thread or async task
pub fn run_monitor(iface: &str) -> Result<(), Box<dyn std::error::Error>> {
    let sock = CanSocket::open(iface)?;
    sock.set_error_filter_accept_all()?;   /* enable error frame reception */

    let mut monitor = CanHealthMonitor::new(30);

    loop {
        match sock.read_frame()? {
            CanFrame::Error(err_frame) => {
                let kind = classify_error(err_frame.error());
                monitor.record_error(kind);

                if let Some(report) = monitor.report() {
                    println!("{}", report);

                    if monitor.is_critical() {
                        eprintln!("[ALERT] Critical CAN health condition! \
                                   Manual intervention required.");
                    }
                }
            }
            CanFrame::Remote(_) | CanFrame::Data(_) => {
                /* Normal traffic — optionally update bus load stats here */
            }
        }
    }
}
```

### Embedded Rust: `no_std` Health Monitor

```rust
#![no_std]

/// A no_std health monitor for bare-metal targets.
/// Uses a fixed-size ring buffer instead of VecDeque.

pub struct EmbeddedCanMonitor<const N: usize> {
    buf: [HealthSample; N],
    head: usize,
    count: usize,
    pub errors: ErrorCounts,
    pub bus_off_count: u32,
}

impl<const N: usize> EmbeddedCanMonitor<N> {
    pub const fn new() -> Self {
        // Safe: HealthSample is Copy and all-zero is valid
        Self {
            buf: [HealthSample {
                timestamp_ms: 0,
                tec: 0,
                rec: 0,
                state: CanNodeState::ErrorActive,
                total_errors: 0,
            }; N],
            head: 0,
            count: 0,
            errors: ErrorCounts {
                bit_errors: 0,
                stuff_errors: 0,
                crc_errors: 0,
                form_errors: 0,
                ack_errors: 0,
                bus_off_events: 0,
                lost_arb_events: 0,
                transceiver_faults: 0,
            },
            bus_off_count: 0,
        }
    }

    pub fn push(&mut self, tec: u8, rec: u8, bus_off: bool, timestamp_ms: u32) {
        let sample = HealthSample {
            timestamp_ms,
            tec,
            rec,
            state: CanNodeState::from_counters(tec, rec, bus_off),
            total_errors: self.errors.total(),
        };
        self.buf[self.head] = sample;
        self.head = (self.head + 1) % N;
        if self.count < N { self.count += 1; }
    }

    /// Health score 0–100; call from application/diagnostic task
    pub fn health_score(&self) -> u8 {
        if self.count == 0 { return 100; }
        let idx = (self.head + N - 1) % N;
        let latest = &self.buf[idx];

        if matches!(latest.state, CanNodeState::BusOff) { return 0; }

        let tec_factor = 100u32 - (latest.tec as u32 * 100 / 255);
        let rec_factor = 100u32 - (latest.rec as u32 * 100 / 127);
        let busoff_pen = 100u32.saturating_sub(self.bus_off_count as u32 * 25);

        ((tec_factor + rec_factor + busoff_pen) / 3) as u8
    }
}

// Minimal HealthSample for no_std (no Instant, use millisecond tick)
#[derive(Clone, Copy)]
pub struct HealthSample {
    pub timestamp_ms: u32,
    pub tec: u8,
    pub rec: u8,
    pub state: CanNodeState,
    pub total_errors: u64,
}
```

---

## Predictive Failure Detection

Health monitoring enables predictive maintenance rather than reactive repair. Key techniques:

### 1. Linear Trend Extrapolation

Given a rising TEC, predict when it will reach 128 (Error Passive threshold):

```c
/* C: Predict time-to-passive in seconds */
float predict_time_to_passive(float tec_current, float tec_rate_per_sec) {
    if (tec_rate_per_sec <= 0.0f) return -1.0f; /* not rising */
    return (128.0f - tec_current) / tec_rate_per_sec;
}
```

```rust
// Rust equivalent
fn predict_time_to_passive(tec: u8, tec_rate: f32) -> Option<f32> {
    if tec_rate <= 0.0 { return None; }
    Some((128.0 - tec as f32) / tec_rate)
}
```

### 2. Burst Pattern Detection

Transient EMI produces error bursts separated by quiet periods. Use a sliding window histogram to distinguish chronic faults (steady error rate) from transient interference (spiky pattern):

```c
/* C: Detect error burst (errors in a short window) */
#define BURST_WINDOW_MS   500
#define BURST_THRESHOLD   10

static uint32_t error_timestamps[64];
static int      ts_head = 0, ts_count = 0;

bool is_error_burst(uint32_t now_ms) {
    /* Record this error */
    error_timestamps[ts_head] = now_ms;
    ts_head = (ts_head + 1) % 64;
    if (ts_count < 64) ts_count++;

    /* Count errors in the last BURST_WINDOW_MS */
    int count = 0;
    for (int i = 0; i < ts_count; i++) {
        int idx = (ts_head - 1 - i + 64) % 64;
        if ((now_ms - error_timestamps[idx]) <= BURST_WINDOW_MS)
            count++;
        else
            break;
    }
    return count >= BURST_THRESHOLD;
}
```

### 3. Repeated Bus-Off Lockout

After N consecutive bus-off events without sustained recovery, lock the node and require manual reset:

```rust
const MAX_AUTO_RECOVERY: u32 = 3;

pub fn should_attempt_recovery(monitor: &CanHealthMonitor) -> bool {
    monitor.consecutive_bus_off < MAX_AUTO_RECOVERY
}
```

### 4. Health Score Alerting Thresholds

| Score | Action |
|---|---|
| 90–100% | No action |
| 70–89% | Log warning, increase monitoring frequency |
| 50–69% | Alert operator / maintenance system |
| 25–49% | Reduce bus load, switch to degraded mode |
| 0–24% | Isolate node, activate fallback |

---

## Summary

CAN Health Monitoring is a disciplined, multi-layer approach to observing the intrinsic fault-confinement state of CAN controllers and transforming raw hardware diagnostics into actionable system health intelligence.

**Core concepts:**
- The CAN standard defines two hardware error counters (TEC and REC) and three node states (Error Active, Error Passive, Bus-Off) that are maintained automatically in silicon.
- Health monitoring reads these counters periodically and captures error interrupt events to build a time-series picture of bus health.
- Error frames (on SocketCAN) or Error Status Registers (on bare-metal) classify errors by type: Bit, Stuff, CRC, Form, and ACK errors each point to a different root cause.

**Implementation strategy:**
- Use a sliding window of counter samples to compute error rates (errors/second) and counter rates-of-change (TEC/s, REC/s).
- Apply diagnostic rules to distinguish transceiver faults (rising TEC only), upstream clock issues (rising REC only), and bus integrity problems (both rising together).
- Track bus-off events cumulatively; repeated bus-off without recovery indicates a chronic hardware fault requiring human intervention rather than automatic recovery.
- Generate a composite health score (0–100%) to drive alerting thresholds and predictive maintenance workflows.

**Language considerations:**
- **C/C++** gives direct register access for bare-metal, SocketCAN error frame parsing for embedded Linux, and efficient object-oriented wrappers for trend analysis.
- **Rust** adds compile-time safety guarantees (no undefined behavior in counter arithmetic, pattern-exhaustive error classification) and supports both `std` (SocketCAN crate) and `no_std` (fixed-buffer ring on microcontrollers) targets.

Properly implemented CAN health monitoring shifts system maintenance from reactive (failures discovered only after a hard fault) to predictive (degradation detected seconds to minutes before failure), which is essential for safety-critical and high-availability systems.