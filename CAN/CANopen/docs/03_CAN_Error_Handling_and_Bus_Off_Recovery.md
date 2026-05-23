# 03. CAN Error Handling & Bus-Off Recovery

> **CANopen / CAN Protocol Series — Chapter 3**
> Topics: TEC/REC counters · Error state machine · Error frame types ·
> Automatic retransmission · Bus-off recovery · Hardware driver integration

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [CAN Error Counters: TEC and REC](#2-can-error-counters-tec-and-rec)
3. [Error State Machine](#3-error-state-machine)
4. [CAN Error Frame Types](#4-can-error-frame-types)
5. [Automatic Retransmission](#5-automatic-retransmission)
6. [Bus-Off Recovery Sequences](#6-bus-off-recovery-sequences)
7. [Hardware Driver Integration](#7-hardware-driver-integration)
8. [CANopen-Level Error Handling](#8-canopen-level-error-handling)
9. [Complete Example: Error Monitor Daemon](#9-complete-example-error-monitor-daemon)
10. [Summary](#10-summary)

---

## 1. Introduction

The CAN bus (ISO 11898) incorporates one of the most sophisticated fault-confinement
mechanisms found in any serial field bus. Unlike RS-485 or Modbus, where a broken node
can flood the bus and freeze all other participants, CAN nodes automatically demote
themselves through a graduated hierarchy of error states — and, in the worst case, remove
themselves from the bus entirely (bus-off) without requiring host-CPU intervention.

CANopen builds on this foundation by adding application-level error signalling via the
**Emergency (EMCY) object** (COB-ID `0x80 + NodeID`) and the **Error Register**
(Object Dictionary index `0x1001`). Understanding both layers is essential for writing
robust industrial firmware.

### Why This Matters

```
  Normal operation                  One faulty node
  ────────────────                  ──────────────────────────────────────
  Node A ──┐                        Node A ──┐
  Node B ──┼── CAN bus (ok)         Node B ──┼── CAN bus  ←── Node C (broken)
  Node C ──┘                        Node D ──┘         keeps transmitting
                                                        garbage frames

  Without fault confinement: ALL nodes freeze, bus 100% utilised by error frames
  With CAN fault confinement: Node C enters bus-off, others continue normally
```

---

## 2. CAN Error Counters: TEC and REC

Every CAN controller maintains two 8-bit (logically 9-bit) hardware counters:

| Counter | Full Name            | Increments on                      | Decrements on                       |
|---------|----------------------|------------------------------------|-------------------------------------|
| TEC     | Transmit Error Counter | Transmission error detected       | Successful frame transmission       |
| REC     | Receive Error Counter  | Reception error detected          | Successful frame reception          |

### 2.1 Counter Adjustment Rules (ISO 11898-1)

The standard defines precise increment/decrement steps:

```
  TEC adjustments
  ───────────────
  +8   Transmit error (bit error, stuff error during TX, etc.)
  +8   Transmission of an error flag
  -1   Successful frame transmission (ACK received)

  REC adjustments
  ───────────────
  +1   Receive error (most error types)
  +8   Dominant bit detected during error flag (severe)
  +8   Dominant bit after overload flag
  -1   Successful frame reception (down to minimum 0)
  (REC cannot go below 0, but does not cause bus-off even at 127+)
```

### 2.2 Reading Counters from Hardware

Most CAN controllers expose TEC/REC as memory-mapped registers.

```c
/* ─────────────────────────────────────────────────────────────────
 * Example: Reading TEC/REC from an NXP SJA1000-compatible controller
 * ───────────────────────────────────────────────────────────────── */
#include <stdint.h>

/* SJA1000 register map (memory-mapped base address) */
#define CAN_BASE        0x40006000UL
#define REG_TXERR       (*(volatile uint8_t *)(CAN_BASE + 0x1CU))  /* TEC */
#define REG_RXERR       (*(volatile uint8_t *)(CAN_BASE + 0x1DU))  /* REC */
#define REG_STATUS      (*(volatile uint8_t *)(CAN_BASE + 0x02U))
#define REG_ECC         (*(volatile uint8_t *)(CAN_BASE + 0x0CU))  /* Error code capture */

/* Status register bits */
#define STATUS_ES       (1U << 6)   /* Error Status: 1 = error-passive or bus-off */
#define STATUS_BS       (1U << 7)   /* Bus Status:   1 = bus-off */

typedef struct {
    uint8_t  tec;           /* Transmit Error Counter          */
    uint8_t  rec;           /* Receive  Error Counter          */
    uint8_t  status;        /* Controller status register      */
    uint8_t  ecc;           /* Error Code Capture register     */
    uint8_t  error_passive; /* 1 when node is error-passive    */
    uint8_t  bus_off;       /* 1 when node is bus-off          */
} CAN_ErrorState_t;

/**
 * @brief  Snapshot current error state from hardware registers.
 * @param  out  Pointer to CAN_ErrorState_t to fill.
 */
void CAN_ReadErrorState(CAN_ErrorState_t *out)
{
    out->tec    = REG_TXERR;
    out->rec    = REG_RXERR;
    out->status = REG_STATUS;
    out->ecc    = REG_ECC;

    out->bus_off       = (out->status & STATUS_BS) ? 1U : 0U;
    out->error_passive = (!out->bus_off &&
                         (out->status & STATUS_ES)) ? 1U : 0U;
}
```

### 2.3 SocketCAN (Linux) Example

On Linux with SocketCAN, use the `CAN_RAW` socket and `SIOCGIFDATA` ioctl, or read
`/proc/net/can/stats` and `/sys/class/net/can0/statistics/`:

```c
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/netlink.h>   /* struct can_berr_counter */
#include <stdio.h>
#include <string.h>

/**
 * @brief  Read TEC/REC via SocketCAN netlink interface.
 * @param  ifname  Interface name, e.g. "can0"
 */
void socketcan_print_error_counters(const char *ifname)
{
    int fd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    /* Use SIOCGCANSTATE / driver-specific ioctl depending on kernel version.
     * The portable approach is to enable error frames and parse them.        */
    can_err_mask_t err_mask = CAN_ERR_MASK; /* subscribe to all error frames */
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

    /* Read error frame */
    struct can_frame frame;
    ssize_t n = read(fd, &frame, sizeof(frame));
    if (n == sizeof(frame) && (frame.can_id & CAN_ERR_FLAG)) {
        /* frame.data[6] = TX error counter (if supported by driver) */
        /* frame.data[7] = RX error counter (if supported by driver) */
        printf("TEC=%u  REC=%u\n", frame.data[6], frame.data[7]);
    }
    close(fd);
}
```

---

## 3. Error State Machine

The CAN standard defines three error states driven entirely by TEC and REC thresholds.

### 3.1 State Diagram

```
  ┌──────────────────────────────────────────────────────────────────────┐
  │                    CAN ERROR STATE MACHINE                           │
  └──────────────────────────────────────────────────────────────────────┘

  Power-On
     │
     ▼
  ╔══════════════════╗
  ║                  ║  TEC <= 127 AND REC <= 127
  ║   ERROR-ACTIVE   ║◄──────────────────────────────────────────────────┐
  ║                  ║                                                   │
  ║  Sends ACTIVE    ║  TEC > 127 OR REC > 127                           │
  ║  error flags     ║──────────────────────────────────────┐            │
  ║  (6 dominant     ║                                      │            │
  ║   bits)          ║                                      ▼            │
  ╚══════════════════╝                          ╔══════════════════╗     │
                                                ║                  ║     │
                                                ║  ERROR-PASSIVE   ║     │
                                                ║                  ║     │
                                                ║  Sends PASSIVE   ║     │
                                                ║  error flags     ║─────┘
                                                ║  (6 recessive    ║  TEC < 128 AND REC < 128
                                                ║   bits)          ║  (after successful frames)
                                                ║                  ║
                                                ╚══════════════════╝
                                                         │
                                                         │  TEC > 255
                                                         │  (TEC overflows 8-bit range)
                                                         ▼
                                                ╔══════════════════╗
                                                ║                  ║
                                                ║    BUS-OFF       ║
                                                ║                  ║
                                                ║  NOT transmit,   ║
                                                ║  NOT receive     ║
                                                ║                  ║
                                                ║  Recovery:       ║
                                                ║  128 × 11        ║
                                                ║  recessive bits  ║
                                                ╚══════════════════╝
                                                         │
                                                         │  128 occurrences of
                                                         │  11 consecutive
                                                         │  recessive bits seen
                                                         ▼
                                                ┌────────────────────┐
                                                │  Re-enters         │
                                                │  ERROR-ACTIVE      │
                                                │  TEC = 0, REC = 0  │
                                                └────────────────────┘

  Threshold Summary
  ──────────────────────────────────────────────────
  TEC or REC > 127  →  ERROR-ACTIVE  → ERROR-PASSIVE
  TEC          > 255  →  ERROR-PASSIVE → BUS-OFF
  128 × 11 recessive bits seen  →  BUS-OFF → ERROR-ACTIVE
```

### 3.2 State Thresholds at a Glance

```
  Counter value  ──────────────────────────────────────────────────────►
  0             128                          256 (TEC only)
  │              │                             │
  ├──────────────┼─────────────────────────────┤
  │ ERROR-ACTIVE │     ERROR-PASSIVE           │ → BUS-OFF (TEC only)
  └──────────────┴─────────────────────────────┘
  (REC never causes bus-off, only error-passive)
```

### 3.3 C Implementation: Error State Evaluation

```c
/* ─────────────────────────────────────────────────────────────────
 * CAN Error State type and evaluation
 * ───────────────────────────────────────────────────────────────── */
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CAN_STATE_ERROR_ACTIVE  = 0,
    CAN_STATE_ERROR_PASSIVE = 1,
    CAN_STATE_BUS_OFF       = 2
} CAN_State_t;

/**
 * @brief  Determine logical CAN state from TEC and REC values.
 *
 *         Note: hardware may set bus-off before TEC reaches 256 due to
 *         internal saturation; always OR this with a hardware status check.
 *
 * @param  tec  Transmit Error Counter (0–255, wraps to bus-off)
 * @param  rec  Receive  Error Counter (0–127+, never bus-off)
 * @param  hw_busoff  Hardware bus-off flag (from status register)
 * @return CAN state enum
 */
CAN_State_t CAN_EvaluateState(uint8_t tec, uint8_t rec, bool hw_busoff)
{
    if (hw_busoff) {
        return CAN_STATE_BUS_OFF;
    }
    /* ISO 11898: error-passive when TEC > 127 OR REC > 127 */
    if (tec > 127U || rec > 127U) {
        return CAN_STATE_ERROR_PASSIVE;
    }
    return CAN_STATE_ERROR_ACTIVE;
}

/**
 * @brief  Log a state transition (hook for application / CANopen EMCY).
 */
void CAN_OnStateChange(CAN_State_t old_state, CAN_State_t new_state)
{
    static const char *names[] = {
        "ERROR-ACTIVE",
        "ERROR-PASSIVE",
        "BUS-OFF"
    };
    printf("[CAN] State: %s --> %s\n",
           names[old_state], names[new_state]);

    /* Example: trigger CANopen EMCY object on bus-off */
    if (new_state == CAN_STATE_BUS_OFF) {
        /* CO_errorReport(CO, CO_EM_BUS_OFF_RECOVERED, CO_EMC_COMMUNICATION, 0); */
    }
}
```

---

## 4. CAN Error Frame Types

When a CAN node detects an error, it immediately transmits an **error flag**, aborting
the current frame and forcing all other nodes to detect the error as well. Six error
conditions are defined:

### 4.1 Error Type Overview

| # | Error Type        | Detected By   | Cause                                              |
|---|-------------------|---------------|----------------------------------------------------|
| 1 | Bit Error         | Transmitter   | Transmitted bit ≠ monitored bus level              |
| 2 | Stuff Error       | Receiver      | 6+ consecutive equal bits (violates bit stuffing)  |
| 3 | CRC Error         | Receiver      | Computed CRC ≠ received CRC                        |
| 4 | Form Error        | Receiver      | Fixed-format field (EOF, IFS) has wrong bit value  |
| 5 | Acknowledgement Error | Transmitter | No dominant ACK bit detected                    |
| 6 | Overload Error    | Receiver      | Receiver needs more time (deprecated in CAN FD)    |

### 4.2 Error Frame Structure

```
  CAN Frame with Error (active node detecting bit error)
  ══════════════════════════════════════════════════════

  Normal frame in progress:
  ┌─────┬─────┬──────┬──────────────┬─────┬─────┬─────┐
  │ SOF │ ARB │ CTRL │     DATA     │ CRC │ ACK │ EOF │
  └─────┴─────┴──────┴──────────────┴─────┴─────┴─────┘
                              ▲
                              │ Error detected here
                              │
  ┌─────┬─────┬──────┬────────┴───────────────────────────────────────┐
  │ SOF │ ARB │ CTRL │ DATA (partial)                                 │
  └─────┴─────┴──────┴────────────────────────────────────────────────┘
                                  │
                                  ▼ Node transmits ERROR FLAG immediately
  ┌──────────────────┬──────────────────┬─────────────────────────────┐
  │  Error Flag      │  Error Delimiter │  Interframe Space           │
  │  6 dominant bits │ 8 recessive bits │  3 recessive bits minimum   │
  │  (active) OR     │                  │                             │
  │  6 recessive     │                  │                             │
  │  bits (passive)  │                  │                             │
  └──────────────────┴──────────────────┴─────────────────────────────┘

  Active Error Flag:   ┌─┬─┬─┬─┬─┬─┐        (forces all nodes to see error)
  bus level:        ───┘0 0 0 0 0 0└──────────────────────────
                        dominant = LOW

  Passive Error Flag:  ┌─┬─┬─┬─┬─┬─┐    (recessive — may be invisible)
  bus level:        ───┘1 1 1 1 1 1└──────────────────────────
                        recessive = HIGH
```

### 4.3 Parsing Error Frames in SocketCAN

```c
/* ─────────────────────────────────────────────────────────────────
 * SocketCAN error frame decoder
 * ───────────────────────────────────────────────────────────────── */
#include <linux/can/error.h>
#include <stdio.h>

/**
 * @brief  Decode and print a SocketCAN error frame.
 * @param  f  Pointer to a received CAN frame with CAN_ERR_FLAG set.
 */
void CAN_DecodeErrorFrame(const struct can_frame *f)
{
    if (!(f->can_id & CAN_ERR_FLAG)) return; /* not an error frame */

    canid_t id = f->can_id & CAN_ERR_MASK;

    if (id & CAN_ERR_TX_TIMEOUT)   puts("  [ERR] TX timeout");
    if (id & CAN_ERR_LOSTARB)
        printf("  [ERR] Lost arbitration at bit %u\n", f->data[0]);
    if (id & CAN_ERR_CRTL) {
        uint8_t ctrl = f->data[1];
        if (ctrl & CAN_ERR_CRTL_RX_OVERFLOW) puts("  [ERR] RX buffer overflow");
        if (ctrl & CAN_ERR_CRTL_TX_OVERFLOW) puts("  [ERR] TX buffer overflow");
        if (ctrl & CAN_ERR_CRTL_RX_PASSIVE)  puts("  [ERR] RX error-passive");
        if (ctrl & CAN_ERR_CRTL_TX_PASSIVE)  puts("  [ERR] TX error-passive");
        if (ctrl & CAN_ERR_CRTL_RX_WARNING)  puts("  [ERR] RX warning (>96)");
        if (ctrl & CAN_ERR_CRTL_TX_WARNING)  puts("  [ERR] TX warning (>96)");
    }
    if (id & CAN_ERR_PROT) {
        uint8_t prot  = f->data[2];
        uint8_t loc   = f->data[3];
        if (prot & CAN_ERR_PROT_BIT)    puts("  [ERR] Bit error");
        if (prot & CAN_ERR_PROT_FORM)   puts("  [ERR] Form error");
        if (prot & CAN_ERR_PROT_STUFF)  puts("  [ERR] Stuff error");
        if (prot & CAN_ERR_PROT_CRC)    puts("  [ERR] CRC error");
        if (prot & CAN_ERR_PROT_ACK)    puts("  [ERR] ACK error");
        printf("  [ERR] Error location code: 0x%02X\n", loc);
    }
    if (id & CAN_ERR_BUSOFF)     puts("  [ERR] *** BUS-OFF ***");
    if (id & CAN_ERR_RESTARTED)  puts("  [INFO] Controller restarted");

    printf("  TEC=%u  REC=%u\n", f->data[6], f->data[7]);
}
```

---

## 5. Automatic Retransmission

### 5.1 How Retransmission Works

When a frame transmission fails (e.g., ACK error, bit error), the CAN controller
**automatically re-queues and retransmits** the frame without CPU intervention:

```
  Transmitter timeline
  ─────────────────────────────────────────────────────────────────────
  Attempt 1:
  ┌──────────────────────────┐
  │  Frame ID=0x200, D=0xAB  │ ──► ERROR (no ACK)
  └──────────────────────────┘
                               ▲ TEC += 8
                               │ Error flag transmitted

  Attempt 2 (automatic, hardware):
  ┌──────────────────────────┐
  │  Frame ID=0x200, D=0xAB  │ ──► SUCCESS (ACK received)
  └──────────────────────────┘
                               ▼ TEC -= 1

  Programmer sees: one "send" API call, result: success or final timeout.
```

### 5.2 Single-Shot Mode (No Retransmission)

For time-critical or network-management frames, disable auto-retransmission:

```c
/* ─────────────────────────────────────────────────────────────────
 * Single-shot transmit — no automatic retransmission
 * ───────────────────────────────────────────────────────────────── */

/* Linux SocketCAN: enable single-shot with CAN_RAW_LOOPBACK off
   and CAN_RAW_RECV_OWN_MSGS or just set the flag on the socket: */
int enable_single_shot(int sock)
{
    /* CAN_RAW_LOOPBACK=0 alone doesn't disable retransmit.
       Use the ip link set can0 restart-ms 0  +  bitrate option
       OR use the lower-level approach: */

    /* For IXXAT/PEAK hardware drivers: set the "single-shot" flag
       in the device-specific TX request structure (driver-dependent). */

    /* For SJA1000: set bit 3 (STB) in Command Register to
       abort transmission after first failed attempt — see datasheet. */
    return 0;
}

/* SocketCAN kernel ≥ 4.6: use CAN_RAW_RECV_OWN_MSGS + SO_SNDTIMEO
   to implement a software-level single-shot with timeout: */
#include <sys/socket.h>
#include <time.h>

int socketcan_send_single_shot(int sock,
                               const struct can_frame *frame,
                               unsigned timeout_ms)
{
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000U,
        .tv_usec = (timeout_ms % 1000U) * 1000U
    };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    ssize_t n = write(sock, frame, sizeof(*frame));
    return (n == sizeof(*frame)) ? 0 : -1;
}
```

### 5.3 Retransmission Interaction with TEC

```
  Repeated retransmission scenario (e.g., RX-side pull-up missing → no ACK)

  Attempt │ TEC before │ TEC after │ State
  ────────┼────────────┼───────────┼────────────────────
     1    │     0      │     8     │ Error-Active
     2    │     8      │    16     │ Error-Active
    ...   │   ...      │   ...     │ ...
    16    │   120      │   128     │ Error-Active → Error-Passive
    17    │   128      │   136     │ Error-Passive (now sends passive flags)
    ...   │   ...      │   ...     │ ...
    32    │   248      │   256+    │ Error-Passive → BUS-OFF
           (hardware clamps at 255, trips bus-off threshold)
```

---

## 6. Bus-Off Recovery Sequences

### 6.1 Recovery Protocol (ISO 11898)

Bus-off recovery is **mandatory** and defined by the standard:

```
  BUS-OFF RECOVERY SEQUENCE
  ══════════════════════════════════════════════════════════════════════

  Node enters bus-off (TEC > 255):
  ┌────────────────────────────────────────────────────────────────┐
  │  CAN controller disables TX and RX completely                  │
  │  (node is electrically present but logically silent)           │
  └────────────────────────────────────────────────────────────────┘
                           │
                           │ Host CPU receives bus-off interrupt
                           ▼
  ┌────────────────────────────────────────────────────────────────┐
  │  Application decides: initiate recovery? (yes/no/delay?)       │
  │  CANopen: NMT state machine may stop node first                │
  └────────────────────────────────────────────────────────────────┘
                           │
                           │ Recovery initiated (write to control register)
                           ▼
  ┌────────────────────────────────────────────────────────────────┐
  │  Hardware monitors bus for 128 consecutive occurrences of      │
  │  11 recessive bits (= one idle interframe gap equivalent)      │
  │                                                                │
  │  Count:  1   2   3  ...  126  127  128                         │
  │         [11r][11r][11r]  [11r][11r][11r] → DONE                │
  │                                                                │
  │  Duration at 250 kbit/s:  128 × 11 × 4µs ≈ 5.6 ms minimum      │
  │  Duration at 125 kbit/s:  128 × 11 × 8µs ≈ 11.3 ms minimum     │
  └────────────────────────────────────────────────────────────────┘
                           │
                           │ Recovery complete
                           ▼
  ┌────────────────────────────────────────────────────────────────┐
  │  Controller re-enters ERROR-ACTIVE state                       │
  │  TEC = 0,  REC = 0                                             │
  │  Normal TX and RX resume                                       │
  └────────────────────────────────────────────────────────────────┘
```

### 6.2 Automatic vs. Manual Recovery

```
  Strategy A: Automatic (hardware-initiated)
  ──────────────────────────────────────────
  Some controllers auto-recover after 128×11 recessive bits.
  Risk: if bus fault is permanent, node will enter bus-off loop:
  bus-off → recover → bus-off → recover → ...  (floods bus with error flags)

  Strategy B: Manual with delay (recommended)
  ───────────────────────────────────────────
  1. Bus-off interrupt → disable auto-recovery
  2. Wait T_delay (application-defined, e.g. 1–5 s)
  3. Check: has bus fault cleared? (optional: monitor bus via separate logic)
  4. Initiate recovery
  5. If bus-off again within T_window → increase T_delay (exponential backoff)
```

### 6.3 C Implementation: Bus-Off Recovery with Backoff

```c
/* ─────────────────────────────────────────────────────────────────
 * Bus-off recovery manager with exponential backoff
 * ───────────────────────────────────────────────────────────────── */
#include <stdint.h>
#include <stdbool.h>

#define BUSOFF_DELAY_INITIAL_MS   100U    /* 100 ms first attempt   */
#define BUSOFF_DELAY_MAX_MS      5000U    /* 5 s cap                */
#define BUSOFF_DELAY_MULTIPLIER     2U    /* double each failure     */

typedef struct {
    uint32_t  retry_count;          /* total bus-off events      */
    uint32_t  current_delay_ms;     /* current backoff delay     */
    uint32_t  recovery_start_tick;  /* tick when recovery began  */
    bool      recovering;           /* recovery in progress?     */
} BusOff_Manager_t;

static BusOff_Manager_t g_busoff = {
    .current_delay_ms = BUSOFF_DELAY_INITIAL_MS
};

/* Called by HAL from bus-off interrupt (ISR context or deferred task) */
void CAN_BusOff_ISR(void)
{
    /* 1. Disable further automatic retransmission */
    /* REG_MODE |= MODE_RESET; */  /* SJA1000: enter reset mode */

    g_busoff.retry_count++;
    g_busoff.recovering = false;

    /* Signal main task to handle recovery (do not block in ISR) */
    /* RTOS: xSemaphoreGiveFromISR(g_busoff_sem, &xHigherPriorityTaskWoken); */
}

/* Called by main task / CANopen NMT handler */
void CAN_BusOff_RecoveryTask(uint32_t current_tick_ms)
{
    static uint32_t next_attempt_tick = 0U;

    if (g_busoff.retry_count == 0U) return; /* no bus-off pending */

    if (!g_busoff.recovering) {
        /* Schedule next recovery attempt */
        next_attempt_tick = current_tick_ms + g_busoff.current_delay_ms;
        g_busoff.recovering = true;

        printf("[BUS-OFF] Recovery in %u ms (attempt #%u)\n",
               g_busoff.current_delay_ms, g_busoff.retry_count);
    }

    if (current_tick_ms >= next_attempt_tick) {
        /* Initiate hardware recovery (write to CAN control register) */
        /* REG_MODE &= ~MODE_RESET; */  /* SJA1000: leave reset mode */
        /* SocketCAN: write "1" to /sys/class/net/can0/can_restart */

        printf("[BUS-OFF] Recovery initiated\n");
        g_busoff.recovering = false;

        /* Update backoff — will be reset to initial on clean operation */
        if (g_busoff.current_delay_ms < BUSOFF_DELAY_MAX_MS) {
            g_busoff.current_delay_ms *= BUSOFF_DELAY_MULTIPLIER;
            if (g_busoff.current_delay_ms > BUSOFF_DELAY_MAX_MS)
                g_busoff.current_delay_ms = BUSOFF_DELAY_MAX_MS;
        }
    }
}

/* Call this when frames transmit successfully for N seconds */
void CAN_BusOff_ResetBackoff(void)
{
    g_busoff.retry_count      = 0U;
    g_busoff.current_delay_ms = BUSOFF_DELAY_INITIAL_MS;
    g_busoff.recovering       = false;
}
```

### 6.4 SocketCAN: Bus-Off Recovery via sysfs

```c
/* ─────────────────────────────────────────────────────────────────
 * SocketCAN bus-off recovery via sysfs restart
 * ───────────────────────────────────────────────────────────────── */
#include <stdio.h>
#include <unistd.h>

/**
 * @brief  Trigger SocketCAN controller restart after bus-off.
 *
 *  Equivalent shell command:
 *    ip link set can0 type can restart
 *  or:
 *    echo 1 > /sys/class/net/can0/can_restart
 */
int socketcan_restart(const char *ifname)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/can_restart", ifname);

    FILE *f = fopen(path, "w");
    if (!f) { perror("fopen can_restart"); return -1; }

    fprintf(f, "1\n");
    fclose(f);

    printf("[SocketCAN] Restart triggered for %s\n", ifname);
    return 0;
}

/*
 * Configure automatic restart with a delay (kernel handles the 128×11 bits):
 *
 *   ip link set can0 type can restart-ms 500
 *
 * This instructs the kernel driver to automatically restart after 500 ms.
 * Disable with restart-ms 0 for manual control.
 */
```

---

## 7. Hardware Driver Integration

### 7.1 Driver Architecture

```
  Application / CANopen Stack
  ──────────────────────────────────────────────────────────────
         │                          ▲
         │  CAN_Send()              │  CAN_OnReceive()
         │  CAN_SetBitrate()        │  CAN_OnError()
         ▼                          │
  ┌──────────────────────────────────────────────────────────┐
  │                   CAN HAL / Driver Layer                 │
  │                                                          │
  │  ┌────────────┐  ┌─────────────┐  ┌────────────────────┐ │
  │  │ TX Queue   │  │  Error ISR  │  │  RX FIFO / DMA     │ │
  │  │ (ring buf) │  │  Handler    │  │  (interrupt-driven)│ │
  │  └────────────┘  └─────────────┘  └────────────────────┘ │
  └──────────────────────────────────────────────────────────┘
         │                          ▲
         │  Register writes         │  Register reads / IRQ
         ▼                          │
  ┌──────────────────────────────────────────────────────────┐
  │              CAN Controller Hardware                     │
  │  (SJA1000 / STM32 bxCAN / MCP2515 / TCAN4550 / etc.)     │
  └──────────────────────────────────────────────────────────┘
         │                          │
         └──────────┬───────────────┘
                    │  CAN bus (physical, 120 Ω terminated)
```

### 7.2 Interrupt Handler Template (Bare Metal)

```c
/* ─────────────────────────────────────────────────────────────────
 * Generic CAN interrupt service routine template
 * Adapt register names and bit masks to your specific controller.
 * ───────────────────────────────────────────────────────────────── */
#include <stdint.h>
#include <stdbool.h>

/* ── Callback signatures (implement in application / CANopen stack) ── */
extern void APP_CAN_OnReceive(uint32_t id, const uint8_t *data, uint8_t len);
extern void APP_CAN_OnBusOff(void);
extern void APP_CAN_OnErrorPassive(uint8_t tec, uint8_t rec);
extern void APP_CAN_OnErrorWarning(uint8_t tec, uint8_t rec);

/* ── Simulated register definitions (replace with your MCU's CMSIS defs) ── */
#define CAN_ISR     (*(volatile uint32_t *)0x40006004U)  /* interrupt status */
#define CAN_IER     (*(volatile uint32_t *)0x40006008U)  /* interrupt enable  */
#define CAN_ESR     (*(volatile uint32_t *)0x40006018U)  /* error status      */
#define CAN_MCR     (*(volatile uint32_t *)0x40006000U)  /* master control    */

/* ISR flag bits */
#define CAN_ISR_FMPIE0  (1U << 1)   /* FIFO 0 message pending */
#define CAN_ISR_ERRIE   (1U << 15)  /* Error interrupt        */
#define CAN_ISR_BOFFI   (1U << 10)  /* Bus-off interrupt      */
#define CAN_ISR_EPVIE   (1U << 9)   /* Error-passive interrupt*/
#define CAN_ISR_EWGIE   (1U << 8)   /* Error-warning interrupt*/

/* ESR bits */
#define CAN_ESR_BOFF    (1U << 2)   /* Bus-off flag           */
#define CAN_ESR_EPVF    (1U << 1)   /* Error-passive flag     */
#define CAN_ESR_EWGF    (1U << 0)   /* Error-warning flag     */
#define CAN_ESR_TEC_Pos 24U         /* TEC field position     */
#define CAN_ESR_REC_Pos 16U         /* REC field position     */

/**
 * @brief  CAN peripheral interrupt service routine.
 *         Maps to the MCU vector table entry (e.g. CAN1_SCE_IRQHandler on STM32).
 */
void CAN_IRQHandler(void)
{
    uint32_t isr = CAN_ISR;
    uint32_t esr = CAN_ESR;

    /* ── RX: message received ─────────────────────────────────── */
    if (isr & CAN_ISR_FMPIE0) {
        /* Read from RX FIFO registers (controller-specific) */
        uint32_t rx_id   = 0;  /* CAN_RI0R >> 21 (std) or >> 3 (ext) */
        uint8_t  rx_data[8] = {0};
        uint8_t  rx_dlc  = 0;  /* CAN_RDT0R & 0x0F */

        APP_CAN_OnReceive(rx_id, rx_data, rx_dlc);

        /* Release FIFO output mailbox */
        /* CAN_RF0R |= CAN_RF0R_RFOM0; */
    }

    /* ── Error conditions ─────────────────────────────────────── */
    if (isr & CAN_ISR_ERRIE) {
        uint8_t tec = (uint8_t)((esr >> CAN_ESR_TEC_Pos) & 0xFFU);
        uint8_t rec = (uint8_t)((esr >> CAN_ESR_REC_Pos) & 0xFFU);

        if (esr & CAN_ESR_BOFF) {
            /* Bus-off: notify application, do NOT recover from ISR */
            CAN_ISR |= CAN_ISR_BOFFI;  /* clear flag (RC_W1) */
            APP_CAN_OnBusOff();
        }
        else if (esr & CAN_ESR_EPVF) {
            CAN_ISR |= CAN_ISR_EPVIE;
            APP_CAN_OnErrorPassive(tec, rec);
        }
        else if (esr & CAN_ESR_EWGF) {
            CAN_ISR |= CAN_ISR_EWGIE;
            APP_CAN_OnErrorWarning(tec, rec);
        }

        /* Clear general error interrupt flag */
        CAN_ISR |= CAN_ISR_ERRIE;
    }
}
```

### 7.3 STM32 HAL Integration Example

```c
/* ─────────────────────────────────────────────────────────────────
 * STM32 HAL weak-function overrides for CAN error callbacks
 * ───────────────────────────────────────────────────────────────── */
#include "stm32f4xx_hal.h"

extern CAN_HandleTypeDef hcan1;

/**
 * @brief  Called by HAL when an error is detected on the CAN bus.
 *         Override the weak HAL default.
 */
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
    uint32_t err = HAL_CAN_GetError(hcan);

    if (err & HAL_CAN_ERROR_BOF) {
        /* Bus-off: schedule recovery in main loop */
        g_busoff.retry_count++;
    }
    if (err & HAL_CAN_ERROR_EPV) {
        /* Error-passive: log, send CANopen EMCY */
    }
    if (err & HAL_CAN_ERROR_EWG) {
        /* Error-warning (TEC or REC > 96): early warning */
    }

    /* Clear error flags */
    __HAL_CAN_CLEAR_FLAG(hcan, CAN_FLAG_ERRI);
}

/**
 * @brief  Initiate HAL bus-off recovery.
 */
void CAN_RecoverBusOff_HAL(void)
{
    HAL_CAN_Stop(&hcan1);
    HAL_Delay(g_busoff.current_delay_ms);
    HAL_CAN_Start(&hcan1);
    HAL_CAN_ActivateNotification(&hcan1,
        CAN_IT_RX_FIFO0_MSG_PENDING |
        CAN_IT_ERROR                |
        CAN_IT_BUSOFF               |
        CAN_IT_ERROR_PASSIVE        |
        CAN_IT_ERROR_WARNING);
}
```

---

## 8. CANopen-Level Error Handling

CANopen adds two important abstractions on top of raw CAN error handling.

### 8.1 Error Register (Object 0x1001)

```
  Object 0x1001 — Error Register (UNSIGNED8, mandatory)

  Bit │ Meaning
  ────┼───────────────────────────────────
   0  │ Generic error
   1  │ Current error
   2  │ Voltage error
   3  │ Temperature error
   4  │ Communication error  ← set on bus-off / error-passive
   5  │ Device profile specific
   6  │ Reserved (= 0)
   7  │ Manufacturer specific
```

### 8.2 Emergency Object (EMCY, COB-ID 0x80 + NodeID)

```
  EMCY frame layout (8 bytes)
  ┌────────┬────────┬──────────────────────────────────────────┐
  │ Byte 0 │ Byte 1 │ Byte 2  │ Byte 3  │ Bytes 4–7            │
  │  Error Code LSB │ Error   │ Err Reg │ Manufacturer Data    │
  │         Error Code MSB    │ 0x1001  │ (device specific)    │
  └────────┴────────┴──────────────────────────────────────────┘

  Standard Error Codes for communication errors:
  ┌────────────┬──────────────────────────────────────────────┐
  │ 0x8140     │ CAN Bus Warning (TEC/REC > 96)               │
  │ 0x8120     │ CAN Bus Off                                  │
  │ 0x8130     │ Life Guard / Heartbeat error                 │
  │ 0x8200     │ Protocol Error (PDO not processed)           │
  │ 0x0000     │ Error Reset (no error)                       │
  └────────────┴──────────────────────────────────────────────┘
```

### 8.3 Sending EMCY with CANopenNode

```c
/* ─────────────────────────────────────────────────────────────────
 * CANopenNode: report bus-off as an emergency object
 * (CANopenNode v2.x API)
 * ───────────────────────────────────────────────────────────────── */
#include "CANopen.h"
#include "CO_Emergency.h"

/* CO_EM_errorCodes defined in CO_Emergency.h */
#define CO_EMC_BUS_OFF_RECOVERED   0x8120U  /* CAN Bus Off */
#define CO_EMC_COMMUNICATION       0x04U    /* Error register bit 4 */

/**
 * @brief  Report bus-off via CANopen Emergency object.
 * @param  CO  Pointer to CANopen object (from CO_new / CO_init).
 */
void CANopen_ReportBusOff(CO_t *CO)
{
    /* Set generic + communication error bits in error register 0x1001 */
    CO_error(CO->em,
             true,                      /* set error (not clear) */
             CO_EM_CAN_BUS_OFF,         /* error bit in error status bits array */
             CO_EMC_BUS_OFF_RECOVERED,  /* emergency error code (2 bytes)       */
             0U);                       /* info: additional manufacturer data   */
}

/**
 * @brief  Clear bus-off emergency after successful recovery.
 */
void CANopen_ClearBusOff(CO_t *CO)
{
    CO_error(CO->em,
             false,                     /* clear error */
             CO_EM_CAN_BUS_OFF,
             CO_EMC_BUS_OFF_RECOVERED,
             0U);
}

/**
 * @brief  Example: bus-off callback integrated into CANopenNode main loop.
 *         Called periodically in the application's 1 ms task.
 */
void CANopen_BusOffMonitor(CO_t *CO, uint32_t tick_ms)
{
    static CAN_State_t last_state = CAN_STATE_ERROR_ACTIVE;
    CAN_ErrorState_t   hw;

    CAN_ReadErrorState(&hw);

    CAN_State_t now = CAN_EvaluateState(hw.tec, hw.rec,
                                        hw.bus_off);
    if (now != last_state) {
        CAN_OnStateChange(last_state, now);

        if (now == CAN_STATE_BUS_OFF) {
            CANopen_ReportBusOff(CO);
            CAN_BusOff_ISR();              /* trigger recovery manager */
        } else if (last_state == CAN_STATE_BUS_OFF) {
            CANopen_ClearBusOff(CO);
            CAN_BusOff_ResetBackoff();
        }

        last_state = now;
    }

    CAN_BusOff_RecoveryTask(tick_ms);
}
```

---

## 9. Complete Example: Error Monitor Daemon

The following example ties all concepts together into a self-contained Linux daemon
that monitors a SocketCAN interface, tracks error state transitions, performs
bus-off recovery with backoff, and logs all events.

```c
/* ═══════════════════════════════════════════════════════════════════
 * can_error_monitor.c
 *
 * Usage:  gcc -o can_error_monitor can_error_monitor.c
 *         ./can_error_monitor can0
 *
 * Requires SocketCAN (Linux ≥ 3.6) with a supported CAN adapter.
 * ═══════════════════════════════════════════════════════════════════ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

/* ── Configuration ────────────────────────────────────────────────── */
#define BACKOFF_INIT_MS   200U
#define BACKOFF_MAX_MS   10000U
#define BACKOFF_MULT        2U
#define RECOVERY_ATTEMPTS  10U    /* give up after this many retries */

/* ── State ────────────────────────────────────────────────────────── */
typedef enum { S_ACTIVE, S_PASSIVE, S_BUSOFF } State_t;

static struct {
    State_t  state;
    uint32_t busoff_count;
    uint32_t backoff_ms;
    bool     awaiting_recovery;
} g = {
    .state      = S_ACTIVE,
    .backoff_ms = BACKOFF_INIT_MS
};

/* ── Helpers ──────────────────────────────────────────────────────── */
static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

static const char *state_name(State_t s)
{
    switch (s) {
        case S_ACTIVE:  return "ERROR-ACTIVE";
        case S_PASSIVE: return "ERROR-PASSIVE";
        case S_BUSOFF:  return "BUS-OFF";
        default:        return "UNKNOWN";
    }
}

static void log_event(const char *fmt, ...)
{
    char ts[32];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    printf("[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static int trigger_restart(const char *ifname)
{
    char path[80];
    snprintf(path, sizeof(path), "/sys/class/net/%s/can_restart", ifname);
    int fd = open(path, O_WRONLY);
    if (fd < 0) { perror("open can_restart"); return -1; }
    write(fd, "1\n", 2);
    close(fd);
    return 0;
}

/* ── Error frame handler ──────────────────────────────────────────── */
static void handle_error_frame(const struct can_frame *f,
                               const char *ifname)
{
    canid_t id = f->can_id & CAN_ERR_MASK;
    uint8_t tec = f->data[6];
    uint8_t rec = f->data[7];

    /* Determine new state from counters + bus-off flag */
    State_t new_state;
    if (id & CAN_ERR_BUSOFF)
        new_state = S_BUSOFF;
    else if (tec > 127 || rec > 127)
        new_state = S_PASSIVE;
    else
        new_state = S_ACTIVE;

    /* Log state transitions */
    if (new_state != g.state) {
        log_event("STATE: %s --> %s  (TEC=%u REC=%u)",
                  state_name(g.state), state_name(new_state), tec, rec);
        g.state = new_state;
    }

    /* Detailed error breakdown */
    if (id & CAN_ERR_CRTL) {
        uint8_t c = f->data[1];
        if (c & CAN_ERR_CRTL_TX_WARNING) log_event("WARN: TX error warning (TEC>96)");
        if (c & CAN_ERR_CRTL_RX_WARNING) log_event("WARN: RX error warning (REC>96)");
        if (c & CAN_ERR_CRTL_TX_PASSIVE) log_event("WARN: TX error-passive (TEC>127)");
        if (c & CAN_ERR_CRTL_RX_PASSIVE) log_event("WARN: RX error-passive (REC>127)");
    }
    if (id & CAN_ERR_PROT) {
        uint8_t p = f->data[2];
        if (p & CAN_ERR_PROT_BIT)   log_event("ERR: Bit error");
        if (p & CAN_ERR_PROT_STUFF) log_event("ERR: Stuff error");
        if (p & CAN_ERR_PROT_CRC)   log_event("ERR: CRC error");
        if (p & CAN_ERR_PROT_FORM)  log_event("ERR: Form error");
        if (p & CAN_ERR_PROT_ACK)   log_event("ERR: ACK error (no receiver?)");
    }

    /* Bus-off recovery */
    if (new_state == S_BUSOFF && !g.awaiting_recovery) {
        g.busoff_count++;
        if (g.busoff_count > RECOVERY_ATTEMPTS) {
            log_event("FATAL: %u bus-off events — giving up.", g.busoff_count);
            exit(EXIT_FAILURE);
        }
        log_event("BUS-OFF #%u: recovery in %u ms", g.busoff_count, g.backoff_ms);
        g.awaiting_recovery = true;

        usleep((useconds_t)g.backoff_ms * 1000U); /* simple sleep (use timer in RTOS) */

        if (trigger_restart(ifname) == 0)
            log_event("BUS-OFF: restart triggered");

        /* Update backoff */
        g.backoff_ms = (g.backoff_ms * BACKOFF_MULT < BACKOFF_MAX_MS)
                       ? g.backoff_ms * BACKOFF_MULT
                       : BACKOFF_MAX_MS;
        g.awaiting_recovery = false;
    }

    /* Reset backoff on sustained healthy operation (external logic needed for full impl) */
    if (new_state == S_ACTIVE && g.busoff_count > 0) {
        /* Could reset after N consecutive good seconds */
        /* g.backoff_ms = BACKOFF_INIT_MS; g.busoff_count = 0; */
    }
}

/* ── Main ─────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <can-interface>\n", argv[0]);
        return 1;
    }
    const char *ifname = argv[1];

    int sock = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return 1; }

    /* Subscribe to all error frames */
    can_err_mask_t err_mask = CAN_ERR_MASK;
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
               &err_mask, sizeof(err_mask));

    struct ifreq ifr;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    log_event("CAN Error Monitor started on %s", ifname);
    log_event("Initial state: %s", state_name(g.state));

    /* Main receive loop */
    for (;;) {
        struct can_frame frame;
        ssize_t n = read(sock, &frame, sizeof(frame));
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read"); break;
        }
        if ((size_t)n < sizeof(frame)) continue;

        if (frame.can_id & CAN_ERR_FLAG)
            handle_error_frame(&frame, ifname);
        /* else: handle normal data frames */
    }

    close(sock);
    return 0;
}
```

---

## 10. Summary

### Key Takeaways

**TEC and REC Counters** are the foundation of CAN fault confinement. TEC increments by
+8 on transmit errors and decrements by 1 on success; REC follows similar rules for the
receive path. The hardware manages these counters autonomously.

**Three Error States** form a graduated response: error-active nodes participate fully;
error-passive nodes transmit recessive (invisible) error flags; bus-off nodes are
completely silent. Only TEC drives the bus-off transition (REC cannot).

**Error Frame Types** — bit, stuff, CRC, form, ACK, and overload errors — each have
specific detection rules and counter impacts. ACK errors are particularly common during
bring-up (single node on bus, no receiver to assert ACK).

**Automatic Retransmission** is handled in hardware; the application simply issues a
send request and polls for completion. Disable it (single-shot mode) for
time-critical NMT or SYNC frames where a delayed retransmit would be worse than
a lost frame.

**Bus-Off Recovery** requires 128 consecutive sequences of 11 recessive bits (~5–11 ms
at typical baud rates). Always implement exponential backoff to prevent a node with a
permanent fault from hammering the bus with recovery attempts.

**CANopen EMCY Object** (COB-ID `0x80 + NodeID`) translates hardware-level error states
into application-visible events. Error code `0x8120` signals bus-off; `0x0000` signals
recovery. The Error Register (OD `0x1001`) provides a persistent flag that SDO masters
can poll.

### Error State Reference Card

```
  ┌────────────────┬───────────────┬───────────────┬──────────────────────────┐
  │  State         │  TEC          │  REC          │  CAN Activity            │
  ├────────────────┼───────────────┼───────────────┼──────────────────────────┤
  │ Error-Active   │  0 – 127      │  0 – 127      │  Full TX + RX            │
  │                │               │               │  Active error flags      │
  ├────────────────┼───────────────┼───────────────┼──────────────────────────┤
  │ Error-Passive  │  128 – 255    │  128+         │  Full TX + RX            │
  │                │               │               │  Passive (recessive)     │
  │                │               │               │  error flags only        │
  ├────────────────┼───────────────┼───────────────┼──────────────────────────┤
  │ Bus-Off        │  > 255        │  N/A          │  NO TX, NO RX            │
  │                │  (overflow)   │               │  128×11 recessive bits   │
  │                │               │               │  needed to recover       │
  └────────────────┴───────────────┴───────────────┴──────────────────────────┘

  Warning threshold: TEC or REC > 96  (many controllers generate an interrupt)
```

### Recommended Integration Checklist

- [ ] Enable bus-off, error-passive, and error-warning interrupts in CAN controller
- [ ] Implement ISR → deferred task handoff (never recover from ISR context)
- [ ] Use exponential backoff for bus-off recovery (initial 100–500 ms, cap at 5–10 s)
- [ ] Map hardware events to CANopen EMCY codes (0x8140, 0x8120)
- [ ] Set Error Register bit 4 (communication error) on bus-off and error-passive
- [ ] Clear Emergency with code 0x0000 after successful recovery
- [ ] Log TEC/REC with timestamps for post-mortem analysis
- [ ] Test with a CAN analyzer: inject dominant glitches, disconnect ACK, verify recovery

---

*End of Chapter 03 — CAN Error Handling & Bus-Off Recovery*

*Next: [04. CANopen NMT State Machine & Node Management](04_CANopen_NMT_State_Machine.md)*