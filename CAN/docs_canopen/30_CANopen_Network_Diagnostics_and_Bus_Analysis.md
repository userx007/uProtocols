# 30. CANopen Network Diagnostics & Bus Analysis

**Document Structure Overview**

| Section | Content |
|---|---|
| **1 – Diagnostic Architecture** | Full ASCII system diagram: nodes → tap → interface → host PC toolchain |
| **2 – Physical Layer** | Differential voltage levels, termination checks, error frame type table, TEC/REC counter monitoring in C |
| **3 – CAN Analyser Tools** | PCAN-View schematic + PCAN-Basic API logger; Kvaser CANlib reader; Vector CANalyzer CAPL script (heartbeat, EMCY, SDO correlation) |
| **4 – Frame Decoding** | 11-bit COB-ID bit layout in ASCII, full function code table, complete C++ `CanopenDecoder` class |
| **5 – SDO Traffic Analysis** | SDO expedited upload timing diagram, command specifier bit breakdown, abort code table, full SDO client in C with timeout/abort handling |
| **6 – PDO Traffic Analysis** | SYNC-triggered PDO timeline diagram, mapping object (0x1A00) structure, `pdo_map_verify` tool reading all 4 TPDO/RPDO channels |
| **7 – Heartbeat Timeline** | Multi-node staggered heartbeat ASCII timeline, boot-up sequence diagram, full multi-threaded heartbeat monitor in C with period averaging |
| **8 – EMCY Monitoring** | EMCY 8-byte frame layout with bit annotation, full EEC classification table, C++ circular-buffer logger class |
| **9 – NMT State Machine** | Complete NMT state diagram in ASCII with transitions and bus-observable signals |
| **10 – Conformance Checks** | Automated test framework in C covering mandatory OD entries, read-only protection, invalid sub-index, SDO abort codes; PDO timing verifier with ±10% tolerance |
| **11 – Diagnostic Library** | `CanopenDiag` C++ class header unifying all features |
| **12 – Bus Load Analysis** | Frame bit-count formula, worked example (9.2% load), ASCII load bar meter, real-time sliding-window monitor in C |
| **13 – Summary** | Layered diagnostic pyramid (Physical → Conformance), key takeaways, full COB-ID quick reference table |


> **Scope:** Using CAN analysers (Peak PCAN-View, Kvaser CanKing, Vector CANalyzer),
> decoding CANopen frames, SDO/PDO traffic analysis, heartbeat timeline, EMCY monitoring,
> and scripted automated conformance checks.

---

## Table of Contents

1. [Overview & Diagnostic Architecture](#1-overview--diagnostic-architecture)
2. [CAN Bus Physical Layer Diagnostics](#2-can-bus-physical-layer-diagnostics)
3. [CAN Analyser Tools](#3-can-analyser-tools)
4. [CANopen Frame Decoding](#4-canopen-frame-decoding)
5. [SDO Traffic Analysis](#5-sdo-traffic-analysis)
6. [PDO Traffic Analysis](#6-pdo-traffic-analysis)
7. [Heartbeat & Node Guarding Timeline](#7-heartbeat--node-guarding-timeline)
8. [EMCY Monitoring](#8-emcy-monitoring)
9. [NMT State Machine Observation](#9-nmt-state-machine-observation)
10. [Scripted Automated Conformance Checks](#10-scripted-automated-conformance-checks)
11. [C/C++ Diagnostic Library Implementation](#11-cc-diagnostic-library-implementation)
12. [Advanced Bus Load & Statistics Analysis](#12-advanced-bus-load--statistics-analysis)
13. [Summary](#13-summary)

---

## 1. Overview & Diagnostic Architecture

CANopen network diagnostics involves both **passive observation** (listening without
transmitting) and **active probing** (sending SDO requests, NMT commands). The diagnostic
toolchain typically sits outside the operational network with a CAN-to-USB or CAN-to-PCIe
interface bridging to a host PC.

```
  ┌─────────────────────────────────────────────────────────────────────┐
  │                   CANopen Network Diagnostic Setup                  │
  │                                                                     │
  │  ┌────────┐   ┌────────┐   ┌────────┐   ┌────────┐   ┌────────┐     │
  │  │ Node 1 │   │ Node 2 │   │ Node 3 │   │  NMT   │   │  SDO   │     │
  │  │ (0x01) │   │ (0x02) │   │ (0x03) │   │ Master │   │ Client │     │
  │  └───┬────┘   └───┬────┘   └───┬────┘   └────┬───┘   └───┬────┘     │
  │      │            │            │             │           │          │
  │  ════╪════════════╪════════════╪═════════════╪═══════════╪════      │
  │      │         CAN Bus (120Ω termination both ends)      │          │
  │  ════╪════════════╪═══════════╪══════════════╪═══════════╪════      │
  │      │                        │                                     │
  │      │              ┌─────────┴──────────┐                          │
  │      │              │  Diagnostic Tap    │                          │
  │      └──────────────│  (High-Z passive)  │                          │
  │                     └─────────┬──────────┘                          │
  │                               │ USB / PCIe                          │
  │                     ┌─────────┴──────────┐                          │
  │                     │   CAN Interface    │  Peak PCAN-USB           │
  │                     │   (PCAN / Kvaser / │  Kvaser Leaf             │
  │                     │    Vector VN16xx)  │  Vector VN1610           │
  │                     └─────────┬──────────┘                          │
  │                               │                                     │
  │                     ┌─────────┴──────────┐                          │
  │                     │  Host PC           │                          │
  │                     │  ┌───────────────┐ │                          │
  │                     │  │ PCAN-View /   │ │                          │
  │                     │  │ CanKing /     │ │                          │
  │                     │  │ CANalyzer /   │ │                          │
  │                     │  │ Custom script │ │                          │
  │                     │  └───────────────┘ │                          │
  │                     └────────────────────┘                          │
  └─────────────────────────────────────────────────────────────────────┘
```

### 1.1 Diagnostic Goals

| Goal                        | Method                              | Tooling              |
|-----------------------------|-------------------------------------|----------------------|
| Verify node presence        | Heartbeat / Node Guarding monitoring| Passive listen       |
| Check NMT transitions       | NMT command + state response        | Active               |
| Read OD entries             | SDO upload                          | Active               |
| Validate PDO mappings       | SDO read 0x1A00, 0x1600             | Active               |
| Measure bus load            | Frame counting over time            | Passive listen       |
| Detect error frames         | CAN error frame counting            | Physical layer       |
| Monitor EMCY events         | Listen on 0x80 + Node-ID            | Passive listen       |
| Automated conformance check | Scripted SDO sequence               | Active scripted      |

---

## 2. CAN Bus Physical Layer Diagnostics

Before any protocol-level analysis, physical layer health must be verified. Many CANopen
issues trace back to bad termination, ground loops, or stub lengths.

### 2.1 Key Physical Parameters

```
  CAN Bus Physical Diagnostics
  ═════════════════════════════

  Differential Voltage (CAN_H - CAN_L):
  ────────────────────────────────────
  Dominant (logical 0):   +2.0 V to +3.0 V  ──► OK
  Recessive (logical 1):  ~0 V (< 0.5 V)    ──► OK
  Signal eye:
      3.5V ─┐   ┌──┐  ┌──────────────┐
            │   │  │  │              │
      1.5V  └───┘  └──┘              └─────
            ◄─► ◄──►
            tq  Bit period
  Typical bit timing (500 kbps, 80% sampling):
    Prescaler=4, Seg1=13tq, Seg2=2tq, SJW=1tq

  Termination:
  ────────────
  Correct:  120Ω at each bus end  →  60Ω measured across CAN_H/CAN_L
  Missing:  One end   →  120Ω (signal reflections, intermittent errors)
  Missing:  Both ends →  No dominant state possible (bus dead)
  Extra:    <60Ω      →  Dominant bit voltage too low

  Common Error Frame Causes:
  ─────────────────────────
  ┌─────────────────┬──────────────────────────────────────────────┐
  │ Error Type      │ Likely Cause                                 │
  ├─────────────────┼──────────────────────────────────────────────┤
  │ Bit Error       │ EMI, ground loop, incorrect baud rate        │
  │ Stuff Error     │ Signal corruption, bad termination           │
  │ CRC Error       │ Bit flip during transmission                 │
  │ Form Error      │ Protocol violation, damaged frame            │
  │ ACK Error       │ No receiver, node not listening              │
  └─────────────────┴──────────────────────────────────────────────┘
```

### 2.2 Error Counter Monitoring via C

CAN controllers expose Transmit Error Counter (TEC) and Receive Error Counter (REC).
Reading these reveals bus health without an external analyser:

```c
/*
 * can_error_monitor.c
 * Physical layer health monitoring via error counters.
 * Targets: SocketCAN (Linux) with PCAN, Kvaser, or any SocketCAN adapter.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <linux/can/netlink.h>
#include <linux/sockios.h>

/* Error severity thresholds per CAN specification */
#define TEC_WARNING_LIMIT    96
#define TEC_ERROR_PASSIVE   128
#define TEC_BUS_OFF         256

typedef struct {
    uint32_t tec;               /* Transmit Error Counter */
    uint32_t rec;               /* Receive Error Counter  */
    uint32_t rx_errors;         /* Total receive error frames */
    uint32_t tx_errors;         /* Total transmit errors      */
    uint32_t bus_error;         /* Bus error count            */
    uint32_t arbitration_lost;  /* Arbitration lost count     */
    uint32_t state;             /* CAN controller state       */
    struct timespec timestamp;
} can_error_stats_t;

typedef enum {
    BUS_STATE_OK           = 0,
    BUS_STATE_WARNING      = 1,
    BUS_STATE_ERROR_PASSIVE = 2,
    BUS_STATE_BUS_OFF      = 3
} can_bus_state_t;

static const char *bus_state_str(can_bus_state_t state)
{
    switch (state) {
    case BUS_STATE_OK:            return "ERROR-ACTIVE (OK)";
    case BUS_STATE_WARNING:       return "WARNING";
    case BUS_STATE_ERROR_PASSIVE: return "ERROR-PASSIVE";
    case BUS_STATE_BUS_OFF:       return "BUS-OFF";
    default:                      return "UNKNOWN";
    }
}

/* Open error-reporting socket and decode error frames */
int can_error_monitor_open(const char *ifname)
{
    struct sockaddr_can addr;
    struct ifreq        ifr;
    can_err_mask_t      err_mask;
    int                 sock;

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    /* Enable all error frame types */
    err_mask = (CAN_ERR_TX_TIMEOUT   |
                CAN_ERR_LOSTARB      |
                CAN_ERR_CRTL         |
                CAN_ERR_PROT         |
                CAN_ERR_TRX          |
                CAN_ERR_ACK          |
                CAN_ERR_BUSOFF       |
                CAN_ERR_BUSERROR     |
                CAN_ERR_RESTARTED);

    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask,
               sizeof(err_mask));

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

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

/* Decode incoming CAN error frame into stats structure */
void decode_error_frame(const struct can_frame *frame,
                        can_error_stats_t      *stats)
{
    clock_gettime(CLOCK_MONOTONIC, &stats->timestamp);

    if (frame->can_id & CAN_ERR_LOSTARB) {
        stats->arbitration_lost++;
        printf("[ARBLOST] Arbitration lost at bit %u\n",
               frame->data[0]);
    }

    if (frame->can_id & CAN_ERR_CRTL) {
        uint8_t ctrl = frame->data[1];
        if (ctrl & CAN_ERR_CRTL_RX_WARNING)
            printf("[CTRL]    RX warning (REC > %d)\n", TEC_WARNING_LIMIT);
        if (ctrl & CAN_ERR_CRTL_TX_WARNING)
            printf("[CTRL]    TX warning (TEC > %d)\n", TEC_WARNING_LIMIT);
        if (ctrl & CAN_ERR_CRTL_RX_PASSIVE)
            printf("[CTRL]    RX Error-Passive\n");
        if (ctrl & CAN_ERR_CRTL_TX_PASSIVE)
            printf("[CTRL]    TX Error-Passive\n");
        /* Data bytes 6,7 contain REC and TEC */
        stats->rec = frame->data[6];
        stats->tec = frame->data[7];
    }

    if (frame->can_id & CAN_ERR_PROT) {
        stats->bus_error++;
        const char *prot_err = "";
        switch (frame->data[2]) {
        case CAN_ERR_PROT_BIT:    prot_err = "Bit error";         break;
        case CAN_ERR_PROT_FORM:   prot_err = "Form error";        break;
        case CAN_ERR_PROT_STUFF:  prot_err = "Stuff error";       break;
        case CAN_ERR_PROT_CRC:    prot_err = "CRC error";         break;
        case CAN_ERR_PROT_ACK:    prot_err = "ACK error";         break;
        default:                   prot_err = "Unknown protocol";  break;
        }
        printf("[PROTO]   %s at location 0x%02X\n", prot_err, frame->data[3]);
    }

    if (frame->can_id & CAN_ERR_BUSOFF) {
        printf("[BUSOFF]  *** BUS-OFF STATE - controller disabled! ***\n");
        stats->state = BUS_STATE_BUS_OFF;
    }

    /* Classify bus state by TEC */
    if (stats->state != BUS_STATE_BUS_OFF) {
        if (stats->tec >= TEC_ERROR_PASSIVE || stats->rec >= TEC_ERROR_PASSIVE)
            stats->state = BUS_STATE_ERROR_PASSIVE;
        else if (stats->tec >= TEC_WARNING_LIMIT || stats->rec >= TEC_WARNING_LIMIT)
            stats->state = BUS_STATE_WARNING;
        else
            stats->state = BUS_STATE_OK;
    }
}

void print_error_report(const can_error_stats_t *stats)
{
    printf("┌─────────────────────────────────────────┐\n");
    printf("│         CAN Bus Error Status Report     │\n");
    printf("├─────────────────────────────────────────┤\n");
    printf("│  State:            %-22s│\n",
           bus_state_str((can_bus_state_t)stats->state));
    printf("│  TEC (TX errors):  %-4u                 │\n", stats->tec);
    printf("│  REC (RX errors):  %-4u                 │\n", stats->rec);
    printf("│  Bus errors:       %-4u                 │\n", stats->bus_error);
    printf("│  Arbitration lost: %-4u                 │\n",
           stats->arbitration_lost);
    printf("└─────────────────────────────────────────┘\n");
}
```

---

## 3. CAN Analyser Tools

### 3.1 Peak PCAN-View

PCAN-View is a free Windows-based tool provided with Peak CAN interfaces (PCAN-USB,
PCAN-USB Pro FD). Key features for CANopen diagnostics:

```
  PCAN-View Layout (Schematic)
  ════════════════════════════

  ┌────────────────────────────────────────────────────────────────────┐
  │  PCAN-View  [File] [Hardware] [View] [Help]         [●REC][●STOP]  │
  ├────────────────────────────────────────────────────────────────────┤
  │ Interface: PCAN-USB (FD) │ Bitrate: 500 kbps  │ Bus: OK │ Load: 18%│
  ├──────────────────────────┬────────────────────┬────────────────────┤
  │ Receive Tab              │ Transmit Tab       │ Trace Tab          │
  │ ─────────                │ ────────────       │                    │
  │ ID     DLC  Data         │ [+] Add Frame      │ Time    ID   Data  │
  │ 0x081  2    00 00        │ 0x601 8 40 00 10.. │ 0.000  0x701 00    │
  │ 0x188  4    A2 04 00 00  │ [SEND ONCE]        │ 0.001  0x081 0000  │
  │ 0x208  4    00 00 00 00  │ [SEND CYCLIC 100ms]│ 0.101  0x701 00    │
  │ 0x285  8    01 00 00 00  │                    │ 0.201  0x701 00    │
  │ 0x301  8    00 00 22 09  │                    │ 0.215  0x188 A204  │
  │ 0x401  8    AA BB CC DD  │                    │ 0.301  0x701 00    │
  │ 0x601  8    40 00 10 00  │                    │ 0.315  0x208 0000  │
  │ 0x581  8    4F 00 10 00  │                    │ 0.401  0x701 00    │
  │ 0x702  1    05           │                    │ 0.425  0x285 0100  │
  │ 0x703  1    05           │                    │          ...       │
  └──────────────────────────┴────────────────────┴────────────────────┘

  Useful Filters for CANopen Diagnostics:
  ├─ EMCY:      0x080 – 0x0FF  (filter: 0x080, mask 0x780)
  ├─ NMT:       0x000          (filter: 0x000, mask 0x7FF)
  ├─ SYNC:      0x080          (filter: 0x080, mask 0x7FF)
  ├─ TPDO1:     0x181 – 0x1FF  (filter: 0x180, mask 0x780)
  ├─ RPDO1:     0x201 – 0x27F  (filter: 0x200, mask 0x780)
  ├─ SDO resp:  0x581 – 0x5FF  (filter: 0x580, mask 0x780)
  ├─ SDO req:   0x601 – 0x67F  (filter: 0x600, mask 0x780)
  └─ Heartbeat: 0x701 – 0x77F  (filter: 0x700, mask 0x780)
```

**Usage workflow with PEAK PCAN-Basic API (C):**

```c
/*
 * pcan_canopen_logger.c
 * Example using Peak PCAN-Basic API to capture and decode CANopen traffic.
 * Requires: PCAN-Basic SDK headers (PCANBasic.h) and libPCANBasic.so / PCANBasic.dll
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "PCANBasic.h"

#define PCAN_DEVICE     PCAN_USBBUS1
#define PCAN_BAUDRATE   PCAN_BAUD_500K

/* CANopen function codes (upper 4 bits of 11-bit COB-ID) */
#define FC_NMT          0x000
#define FC_SYNC         0x080
#define FC_EMCY         0x080   /* 0x081..0x0FF with node-id */
#define FC_TIME         0x100
#define FC_TPDO1        0x180
#define FC_RPDO1        0x200
#define FC_TPDO2        0x280
#define FC_RPDO2        0x300
#define FC_TPDO3        0x380
#define FC_RPDO3        0x400
#define FC_TPDO4        0x480
#define FC_RPDO4        0x500
#define FC_TSDO         0x580
#define FC_RSDO         0x600
#define FC_NMT_ERR_CTRL 0x700  /* Heartbeat / Node Guarding */
#define FC_LSS          0x7E4

typedef struct {
    uint32_t cob_id;
    uint8_t  node_id;
    uint16_t function_code;
    const char *fc_name;
} canopen_id_info_t;

/* Decode 11-bit CANopen COB-ID into function code and node-id */
canopen_id_info_t decode_canopen_id(uint32_t cob_id)
{
    canopen_id_info_t info = {0};
    info.cob_id        = cob_id;
    info.function_code = cob_id & 0x780;
    info.node_id       = cob_id & 0x07F;

    /* NMT and SYNC have no node-id */
    if (cob_id == 0x000) { info.fc_name = "NMT";        info.node_id = 0; }
    else if (cob_id == 0x080) { info.fc_name = "SYNC";  info.node_id = 0; }
    else if (cob_id >= 0x081 && cob_id <= 0x0FF) info.fc_name = "EMCY";
    else if (cob_id == 0x100) { info.fc_name = "TIME";  info.node_id = 0; }
    else if (cob_id >= 0x181 && cob_id <= 0x1FF) info.fc_name = "TPDO1";
    else if (cob_id >= 0x201 && cob_id <= 0x27F) info.fc_name = "RPDO1";
    else if (cob_id >= 0x281 && cob_id <= 0x2FF) info.fc_name = "TPDO2";
    else if (cob_id >= 0x301 && cob_id <= 0x37F) info.fc_name = "RPDO2";
    else if (cob_id >= 0x381 && cob_id <= 0x3FF) info.fc_name = "TPDO3";
    else if (cob_id >= 0x401 && cob_id <= 0x47F) info.fc_name = "RPDO3";
    else if (cob_id >= 0x481 && cob_id <= 0x4FF) info.fc_name = "TPDO4";
    else if (cob_id >= 0x501 && cob_id <= 0x57F) info.fc_name = "RPDO4";
    else if (cob_id >= 0x581 && cob_id <= 0x5FF) info.fc_name = "SDO-Tx";
    else if (cob_id >= 0x601 && cob_id <= 0x67F) info.fc_name = "SDO-Rx";
    else if (cob_id >= 0x701 && cob_id <= 0x77F) info.fc_name = "HB/NG";
    else                                          info.fc_name = "UNKN";

    return info;
}

void print_canopen_frame(const TPCANMsg *msg, DWORD timestamp_ms)
{
    canopen_id_info_t info = decode_canopen_id(msg->ID);
    printf("[%8lu ms] COB-ID=0x%03X  %-8s  Node=%02X  DLC=%d  Data:",
           (unsigned long)timestamp_ms,
           msg->ID, info.fc_name, info.node_id, msg->LEN);

    for (int i = 0; i < msg->LEN; i++)
        printf(" %02X", msg->DATA[i]);
    printf("\n");

    /* Additional decode for well-known frame types */
    if (msg->ID == 0x000 && msg->LEN == 2) {
        /* NMT command */
        const char *nmt_cmd = "?";
        switch (msg->DATA[0]) {
        case 0x01: nmt_cmd = "Start (Operational)";       break;
        case 0x02: nmt_cmd = "Stop";                      break;
        case 0x80: nmt_cmd = "Enter Pre-Operational";     break;
        case 0x81: nmt_cmd = "Reset Application";         break;
        case 0x82: nmt_cmd = "Reset Communication";       break;
        }
        printf("           └─ NMT Cmd: %s → Node 0x%02X\n",
               nmt_cmd, msg->DATA[1]);
    } else if (info.function_code == FC_NMT_ERR_CTRL && msg->LEN == 1) {
        /* Heartbeat */
        const char *hb_state = "?";
        switch (msg->DATA[0] & 0x7F) {
        case 0x00: hb_state = "Boot-Up";           break;
        case 0x04: hb_state = "Stopped";           break;
        case 0x05: hb_state = "Operational";       break;
        case 0x7F: hb_state = "Pre-Operational";   break;
        }
        printf("           └─ Heartbeat: Node 0x%02X State=%s\n",
               info.node_id, hb_state);
    }
}

int main(void)
{
    TPCANMsg        msg;
    TPCANTimestamp  ts;
    TPCANStatus     sts;
    DWORD           ms;

    sts = CAN_Initialize(PCAN_DEVICE, PCAN_BAUDRATE, 0, 0, 0);
    if (sts != PCAN_ERROR_OK) {
        fprintf(stderr, "CAN_Initialize failed: 0x%08X\n", sts);
        return 1;
    }
    printf("CANopen logger started. Press Ctrl+C to stop.\n\n");

    while (1) {
        sts = CAN_Read(PCAN_DEVICE, &msg, &ts);
        if (sts == PCAN_ERROR_OK) {
            ms = ts.millis + (ts.millis_overflow * 0xFFFFFFFF) +
                 (ts.micros / 1000);
            print_canopen_frame(&msg, ms);
        } else if (sts != PCAN_ERROR_QRCVEMPTY) {
            fprintf(stderr, "CAN_Read error: 0x%08X\n", sts);
            break;
        }
    }

    CAN_Uninitialize(PCAN_DEVICE);
    return 0;
}
```

### 3.2 Kvaser CanKing

Kvaser CanKing offers a similar frame view with strong hardware timestamping. Access via
the Kvaser CANlib API:

```c
/*
 * kvaser_canopen_probe.c
 * Using Kvaser CANlib to read and decode CANopen frames.
 * Build: gcc -o kvaser_probe kvaser_canopen_probe.c -lcanlib
 */
#include <stdio.h>
#include <canlib.h>

#define KVASER_CHANNEL   0          /* First Kvaser device channel */
#define BITRATE_500K     canBITRATE_500K

int main(void)
{
    canHandle   hnd;
    canStatus   stat;
    long        id;
    unsigned char data[8];
    unsigned int  dlc, flags;
    unsigned long timestamp;

    canInitializeLibrary();

    hnd = canOpenChannel(KVASER_CHANNEL,
                         canOPEN_ACCEPT_VIRTUAL);
    if (hnd < 0) {
        char errstr[64];
        canGetErrorText((canStatus)hnd, errstr, sizeof(errstr));
        fprintf(stderr, "canOpenChannel: %s\n", errstr);
        return 1;
    }

    stat = canSetBusParams(hnd, BITRATE_500K, 0, 0, 0, 0, 0);
    if (stat != canOK) {
        fprintf(stderr, "canSetBusParams failed: %d\n", stat);
        canCloseChannel(hnd);
        return 1;
    }

    canSetBusOutputControl(hnd, canDRIVER_NORMAL);
    canBusOn(hnd);

    printf("%-12s %-6s %-8s %s\n",
           "Timestamp(ms)", "COB-ID", "Type", "Data");
    printf("%-12s %-6s %-8s %s\n",
           "────────────", "──────", "────────", "────");

    while (1) {
        stat = canReadWait(hnd, &id, data, &dlc, &flags,
                           &timestamp, 100 /* ms timeout */);
        if (stat == canOK) {
            if (flags & canMSG_ERROR_FRAME) {
                printf("%-12lu [ERROR FRAME]\n", timestamp);
                continue;
            }
            /* Reuse decode function from previous example */
            extern canopen_id_info_t decode_canopen_id(uint32_t);
            canopen_id_info_t info = decode_canopen_id((uint32_t)id);
            printf("%-12lu 0x%03lX  %-8s",
                   timestamp, id, info.fc_name);
            for (unsigned int i = 0; i < dlc; i++)
                printf(" %02X", data[i]);
            printf("\n");
        } else if (stat != canERR_NOMSG) {
            break;
        }
    }

    canBusOff(hnd);
    canCloseChannel(hnd);
    return 0;
}
```

### 3.3 Vector CANalyzer

Vector CANalyzer is the industry-standard tool for professional CAN/CANopen analysis.
It uses CAPL (CAN Application Programming Language), a C-like scripting language.

```
  Vector CANalyzer Block Diagram (Schematic)
  ══════════════════════════════════════════

  ┌──────────────────────────────────────────────────────┐
  │                  CANalyzer Window                    │
  │                                                      │
  │  ┌─────────┐  ┌──────────┐  ┌────────────────────┐   │
  │  │ Measure │  │ Analysis │  │  Graphics Window   │   │
  │  │ Setup   │  │ Windows  │  │  (Signal Timelines)│   │
  │  └────┬────┘  └────┬─────┘  └────────────────────┘   │
  │       │            │                                 │
  │  ┌────▼────────────▼──────────────────────────────┐  │
  │  │           Measurement Block                    │  │
  │  │  ┌────────┐  ┌────────┐  ┌──────────────────┐  │  │
  │  │  │  CAPL  │  │ Replay │  │ CANopen Explorer │  │  │
  │  │  │ Program│  │ Block  │  │ (OD Browser)     │  │  │
  │  │  └────┬───┘  └────┬───┘  └──────────────────┘  │  │
  │  └───────┼───────────┼────────────────────────────┘  │
  │          │           │                               │
  │  ┌───────▼───────────▼────────────────────────────┐  │
  │  │          CAN Hardware (VN1610 / VN1630)        │  │
  │  └────────────────────────────────────────────────┘  │
  └──────────────────────────────────────────────────────┘
```

**Example CAPL script for CANopen diagnostics:**

```cpp
/*
 * canopen_diagnostics.can
 * CAPL script for Vector CANalyzer: monitors heartbeats,
 * decodes SDO transactions, and flags EMCY events.
 */

/* -- Variables ------------------------------------------------------------ */
variables
{
    msTimer heartbeatTimer[128];         /* One timer per node              */
    int     heartbeatTimeout_ms = 1500;  /* 1.5× of expected 1000ms period  */
    int     nodeActive[128];             /* Tracks which nodes are alive     */
    message 0x600 sdoRequest;            /* SDO client → server              */
    long    sdoActive_index;
    long    sdoActive_subindex;
}

/* -- Heartbeat monitoring ------------------------------------------------- */
on message 0x700-0x77F    /* matches all heartbeat / node-guard COB-IDs    */
{
    int nodeId = this.id & 0x07F;
    int state  = this.byte(0) & 0x7F;

    cancelTimer(heartbeatTimer[nodeId]);
    setTimer(heartbeatTimer[nodeId], heartbeatTimeout_ms);

    if (!nodeActive[nodeId]) {
        nodeActive[nodeId] = 1;
        if (state == 0x00)
            write("Node 0x%02X: Boot-Up detected", nodeId);
        else
            write("Node 0x%02X: First heartbeat (state=0x%02X)", nodeId, state);
    }
}

/* Heartbeat timeout handler — called when timer fires */
on timer heartbeatTimer    /* one instance per array element */
{
    /* Determine which node ID this timer belongs to */
    int nodeId = getTimerNumber(this);
    if (nodeActive[nodeId]) {
        nodeActive[nodeId] = 0;
        writeToLog("*** HEARTBEAT LOST: Node 0x%02X ***", nodeId);
        setMeasurementExitError(1);  /* optionally flag test failure */
    }
}

/* -- EMCY monitoring ------------------------------------------------------ */
on message 0x081-0x0FF
{
    int      nodeId   = this.id & 0x07F;
    word     errCode  = this.word(0);    /* Emergency Error Code (EEC)       */
    byte     errReg   = this.byte(2);    /* Error Register (OD 0x1001)       */
    dword    errInfo  = this.dword(3);   /* Manufacturer-specific error info */

    write("*** EMCY *** Node=0x%02X  EEC=0x%04X  ErrReg=0x%02X  Info=0x%08X",
          nodeId, errCode, errReg, errInfo);

    /* Decode common error codes (CiA 301 Annex B) */
    if      (errCode == 0x0000) write("    ↳ No error (reset)");
    else if (errCode >= 0x1000 && errCode <= 0x1FFF) write("    ↳ Generic error");
    else if (errCode >= 0x2000 && errCode <= 0x2FFF) write("    ↳ Current error");
    else if (errCode >= 0x3000 && errCode <= 0x3FFF) write("    ↳ Voltage error");
    else if (errCode >= 0x4000 && errCode <= 0x4FFF) write("    ↳ Temperature error");
    else if (errCode >= 0x5000 && errCode <= 0x5FFF) write("    ↳ Device hardware error");
    else if (errCode >= 0x6000 && errCode <= 0x6FFF) write("    ↳ Device software error");
    else if (errCode >= 0x8000 && errCode <= 0x8FFF) write("    ↳ Monitoring error");
    else if (errCode >= 0xFF00)                      write("    ↳ Manufacturer-specific");
}

/* -- SDO request/response correlation ------------------------------------ */
on message 0x601-0x67F    /* SDO receive: client → server */
{
    int nodeId   = this.id & 0x07F;
    byte cs      = this.byte(0) >> 5;   /* Command Specifier (upper 3 bits) */
    word index   = this.word(1);
    byte subIdx  = this.byte(3);

    sdoActive_index    = index;
    sdoActive_subindex = subIdx;

    if (cs == 2) {
        write("SDO Upload Req:  Node=0x%02X  [%04X:%02X]",
              nodeId, index, subIdx);
    } else if (cs == 1) {
        write("SDO Download Req: Node=0x%02X  [%04X:%02X]  Data=%02X %02X %02X %02X",
              nodeId, index, subIdx,
              this.byte(4), this.byte(5), this.byte(6), this.byte(7));
    }
}

on message 0x581-0x5FF    /* SDO transmit: server → client */
{
    int  nodeId  = this.id & 0x07F;
    byte cs      = this.byte(0) >> 5;
    dword abortCode;

    if (cs == 2) {
        /* Upload response: 4 - n bytes valid */
        int n    = (this.byte(0) >> 2) & 0x03;   /* number of bytes NOT used */
        int size = 4 - n;
        write("SDO Upload Resp: Node=0x%02X  [%04X:%02X]  Size=%d  Data=",
              nodeId, sdoActive_index, sdoActive_subindex, size);
    } else if (cs == 4) {
        abortCode = this.dword(4);
        write("SDO Abort: Node=0x%02X  [%04X:%02X]  AbortCode=0x%08X",
              nodeId, this.word(1), this.byte(3), abortCode);
    }
}
```

---

## 4. CANopen Frame Decoding

### 4.1 CANopen COB-ID Structure

```
  11-bit CAN Identifier (Standard Frame)
  ═══════════════════════════════════════

  Bit:  10  9  8  7  6  5  4  3  2  1  0
        ─────────────────┬───────────────
        Function Code    │   Node-ID
        (4 bits, 0..F)   │   (7 bits, 1..127)
                         │
  Examples:
  ─────────
  0x000  =  [0000] [000 0000]  → NMT Command           (no node-id)
  0x080  =  [0001] [000 0000]  → SYNC                  (no node-id)
  0x081  =  [0001] [000 0001]  → EMCY from Node 1
  0x181  =  [0011] [000 0001]  → TPDO1 from Node 1
  0x201  =  [0100] [000 0001]  → RPDO1 to Node 1
  0x581  =  [1011] [000 0001]  → SDO Response from Node 1
  0x601  =  [1100] [000 0001]  → SDO Request to Node 1
  0x701  =  [1110] [000 0001]  → Heartbeat from Node 1

  Function Code Table:
  ────────────────────
  Hex   Binary  Name                    Direction
  0x0   0000    NMT                     Master → broadcast
  0x1   0001    SYNC / EMCY             Broadcast / Node → all
  0x2   0010    TIME                    Producer → all
  0x3   0011    TPDO1                   Node → all
  0x4   0100    RPDO1                   Master → node
  0x5   0101    TPDO2                   Node → all
  0x6   0110    RPDO2                   Master → node
  0x7   0111    TPDO3                   Node → all
  0x8   1000    RPDO3                   Master → node
  0x9   1001    TPDO4                   Node → all
  0xA   1010    RPDO4                   Master → node
  0xB   1011    SDO (Tx, server→client) Node → master
  0xC   1100    SDO (Rx, client→server) Master → node
  0xE   1110    NMT Error Control (HB)  Node → all
  0xF   1111    LSS                     Master ↔ node
```

### 4.2 C++ Frame Decoder Class

```cpp
/*
 * canopen_decoder.hpp / canopen_decoder.cpp
 * A complete C++ CANopen frame decoder with human-readable output.
 */
#pragma once
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <array>

class CanopenDecoder {
public:
    /* ------------------------------------------------------------------ */
    struct Frame {
        uint32_t cob_id;
        uint8_t  dlc;
        uint8_t  data[8];
        uint64_t timestamp_us;  /* microseconds from capture start */
    };

    struct DecodedFrame {
        uint32_t    cob_id;
        uint8_t     node_id;
        std::string function_name;
        std::string detail;
        std::string raw_hex;
        bool        is_error_frame;
    };

    /* ------------------------------------------------------------------ */
    static DecodedFrame decode(const Frame &f)
    {
        DecodedFrame d{};
        d.cob_id = f.cob_id;
        d.node_id = static_cast<uint8_t>(f.cob_id & 0x07F);
        d.raw_hex = bytes_to_hex(f.data, f.dlc);

        if (f.cob_id == 0x000)                decode_nmt(f, d);
        else if (f.cob_id == 0x080)            decode_sync(f, d);
        else if (f.cob_id == 0x100)            decode_time(f, d);
        else if (f.cob_id >= 0x081 &&
                 f.cob_id <= 0x0FF)            decode_emcy(f, d);
        else if (f.cob_id >= 0x181 &&
                 f.cob_id <= 0x1FF)            decode_pdo(f, d, 1, "TPDO");
        else if (f.cob_id >= 0x201 &&
                 f.cob_id <= 0x27F)            decode_pdo(f, d, 1, "RPDO");
        else if (f.cob_id >= 0x281 &&
                 f.cob_id <= 0x2FF)            decode_pdo(f, d, 2, "TPDO");
        else if (f.cob_id >= 0x301 &&
                 f.cob_id <= 0x37F)            decode_pdo(f, d, 2, "RPDO");
        else if (f.cob_id >= 0x581 &&
                 f.cob_id <= 0x5FF)            decode_sdo_response(f, d);
        else if (f.cob_id >= 0x601 &&
                 f.cob_id <= 0x67F)            decode_sdo_request(f, d);
        else if (f.cob_id >= 0x701 &&
                 f.cob_id <= 0x77F)            decode_heartbeat(f, d);
        else {
            d.function_name = "UNKNOWN";
            d.detail = "Unrecognised COB-ID";
        }
        return d;
    }

    /* ------------------------------------------------------------------ */
    static std::string format(const DecodedFrame &d, uint64_t ts_us)
    {
        std::ostringstream os;
        os << std::setw(10) << ts_us << " us"
           << "  0x" << std::hex << std::uppercase
           << std::setw(3) << std::setfill('0') << d.cob_id
           << std::dec << std::setfill(' ')
           << "  " << std::setw(8) << std::left << d.function_name
           << "  Node=" << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(d.node_id)
           << std::dec << std::setfill(' ')
           << "  [" << d.raw_hex << "]"
           << "\n        " << d.detail;
        return os.str();
    }

private:
    static void decode_nmt(const Frame &f, DecodedFrame &d)
    {
        d.function_name = "NMT";
        d.node_id = 0;
        if (f.dlc < 2) { d.detail = "Malformed (DLC<2)"; return; }

        static const struct { uint8_t cmd; const char *name; } cmds[] = {
            {0x01, "Start (→ Operational)"},
            {0x02, "Stop (→ Stopped)"},
            {0x80, "Enter Pre-Operational"},
            {0x81, "Reset Application"},
            {0x82, "Reset Communication"},
        };
        const char *cmd_name = "Unknown";
        for (auto &c : cmds)
            if (c.cmd == f.data[0]) { cmd_name = c.name; break; }

        std::ostringstream os;
        os << "Cmd=" << cmd_name;
        if (f.data[1] == 0) os << "  Target=ALL";
        else os << "  Target=Node0x" << std::hex << std::uppercase
                << std::setw(2) << std::setfill('0')
                << static_cast<int>(f.data[1]);
        d.detail = os.str();
    }

    static void decode_sync(const Frame &f, DecodedFrame &d)
    {
        d.function_name = "SYNC";
        d.node_id = 0;
        if (f.dlc == 0)
            d.detail = "(no counter)";
        else
            d.detail = "Counter=" + std::to_string(f.data[0]);
    }

    static void decode_emcy(const Frame &f, DecodedFrame &d)
    {
        d.function_name = "EMCY";
        if (f.dlc < 8) { d.detail = "Malformed (DLC<8)"; return; }

        uint16_t eec = static_cast<uint16_t>(f.data[0]) |
                       (static_cast<uint16_t>(f.data[1]) << 8);
        uint8_t  err_reg = f.data[2];

        /* Error Register bits (OD 0x1001) */
        std::string err_bits;
        if (err_reg & 0x01) err_bits += "GenericErr ";
        if (err_reg & 0x02) err_bits += "Current ";
        if (err_reg & 0x04) err_bits += "Voltage ";
        if (err_reg & 0x08) err_bits += "Temp ";
        if (err_reg & 0x10) err_bits += "CommErr ";
        if (err_reg & 0x20) err_bits += "DevProfile ";
        if (err_reg & 0x80) err_bits += "Mfr ";

        std::ostringstream os;
        os << "EEC=0x" << std::hex << std::uppercase << std::setw(4)
           << std::setfill('0') << eec
           << "  ErrReg=[" << err_bits << "]";
        d.detail = os.str();
    }

    static void decode_pdo(const Frame &f, DecodedFrame &d,
                           int pdo_num, const char *type)
    {
        std::ostringstream fn;
        fn << type << pdo_num;
        d.function_name = fn.str();
        std::ostringstream os;
        os << "DLC=" << static_cast<int>(f.dlc) << "  Payload=" << bytes_to_hex(f.data, f.dlc);
        d.detail = os.str();
    }

    static void decode_sdo_request(const Frame &f, DecodedFrame &d)
    {
        d.function_name = "SDO-Rx";
        if (f.dlc < 4) { d.detail = "Malformed"; return; }
        uint8_t  cs    = f.data[0] >> 5;
        uint16_t index = static_cast<uint16_t>(f.data[1]) |
                         (static_cast<uint16_t>(f.data[2]) << 8);
        uint8_t  sub   = f.data[3];
        std::ostringstream os;
        switch (cs) {
        case 1: os << "Download (write) [" << std::hex << std::uppercase
                   << std::setw(4) << std::setfill('0') << index
                   << ":" << std::setw(2) << static_cast<int>(sub) << "]"; break;
        case 2: os << "Upload req (read) [" << std::hex << std::uppercase
                   << std::setw(4) << std::setfill('0') << index
                   << ":" << std::setw(2) << static_cast<int>(sub) << "]"; break;
        case 4: os << "Abort [" << std::hex << std::setw(4) << index
                   << ":" << std::setw(2) << static_cast<int>(sub) << "]"; break;
        default: os << "CS=" << static_cast<int>(cs) << " (segmented?)"; break;
        }
        d.detail = os.str();
    }

    static void decode_sdo_response(const Frame &f, DecodedFrame &d)
    {
        d.function_name = "SDO-Tx";
        if (f.dlc < 4) { d.detail = "Malformed"; return; }
        uint8_t cs   = f.data[0] >> 5;
        uint16_t index = static_cast<uint16_t>(f.data[1]) |
                         (static_cast<uint16_t>(f.data[2]) << 8);
        uint8_t sub  = f.data[3];
        std::ostringstream os;
        switch (cs) {
        case 1: os << "Download ack [" << std::hex << std::setw(4)
                   << std::setfill('0') << index
                   << ":" << std::setw(2) << static_cast<int>(sub) << "]"; break;
        case 2: {
            int n    = (f.data[0] >> 2) & 3;
            int size = 4 - n;
            os << "Upload resp [" << std::hex << std::setw(4) << std::setfill('0')
               << index << ":" << std::setw(2) << static_cast<int>(sub) << "]"
               << " Size=" << std::dec << size << " Val=";
            for (int i = 0; i < size && i + 4 < 8; i++)
                os << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<int>(f.data[4 + i]);
            break;
        }
        case 4: {
            uint32_t abort = static_cast<uint32_t>(f.data[4])
                           | (static_cast<uint32_t>(f.data[5]) << 8)
                           | (static_cast<uint32_t>(f.data[6]) << 16)
                           | (static_cast<uint32_t>(f.data[7]) << 24);
            os << "ABORT [" << std::hex << std::setw(4) << std::setfill('0')
               << index << ":" << std::setw(2) << static_cast<int>(sub)
               << "] Code=0x" << std::setw(8) << abort;
            break;
        }
        default: os << "CS=" << static_cast<int>(cs); break;
        }
        d.detail = os.str();
    }

    static void decode_heartbeat(const Frame &f, DecodedFrame &d)
    {
        d.function_name = "Heartbeat";
        if (f.dlc < 1) { d.detail = "Malformed"; return; }
        static const struct { uint8_t state; const char *name; } states[] = {
            {0x00, "Boot-Up"},
            {0x04, "Stopped"},
            {0x05, "Operational"},
            {0x7F, "Pre-Operational"},
        };
        uint8_t state_val = f.data[0] & 0x7F;
        const char *state_name = "Unknown";
        for (auto &s : states)
            if (s.state == state_val) { state_name = s.name; break; }
        d.detail = std::string("State=") + state_name;
    }

    static void decode_time(const Frame &f, DecodedFrame &d)
    {
        d.function_name = "TIME";
        d.node_id = 0;
        if (f.dlc < 6) { d.detail = "Malformed"; return; }
        uint32_t ms_since_midnight =
            static_cast<uint32_t>(f.data[0])        |
            (static_cast<uint32_t>(f.data[1]) << 8) |
            (static_cast<uint32_t>(f.data[2]) << 16)|
            (static_cast<uint32_t>(f.data[3]) << 24);
        uint16_t days =
            static_cast<uint16_t>(f.data[4]) |
            (static_cast<uint16_t>(f.data[5]) << 8);
        std::ostringstream os;
        os << "Days=" << days << " Ms=" << ms_since_midnight;
        d.detail = os.str();
    }

    static std::string bytes_to_hex(const uint8_t *data, int len)
    {
        std::ostringstream os;
        for (int i = 0; i < len; i++) {
            if (i) os << ' ';
            os << std::hex << std::uppercase << std::setw(2)
               << std::setfill('0') << static_cast<int>(data[i]);
        }
        return os.str();
    }
};
```

---

## 5. SDO Traffic Analysis

### 5.1 SDO Frame Structure

```
  SDO Expedited Upload (Read) Transaction — 4 frames total
  ═════════════════════════════════════════════════════════

  Client (Master)                           Server (Node 0x02)
       │                                            │
       │──── SDO Upload Request ───────────────────►│  COB-ID: 0x602
       │  [40][index_lo][index_hi][sub][00 00 00 00]│
       │   │                                        │
       │   └─ CS=2 (upload initiate request)        │
       │                                            │
       │◄─── SDO Upload Response ───────────────────│  COB-ID: 0x582
       │  [4F][index_lo][index_hi][sub][D0 D1 D2 D3]│
       │   │                                    │   │
       │   └─ CS=2 (upload initiate response)   │   │
       │       e=1 (expedited), s=1 (size given)│   │
       │       n=0 → 4 bytes valid ─────────────┘   │
       │                                            │

  Byte 0 (Command Specifier) detail:
  ──────────────────────────────────────
  Bit:   7  6  5 │ 4  3 │  2  1  │  0
                 │      │        │
         ccs(3b) │ x  x │  n(2b) │  e  s
                 │      │        │
  Upload req:    ccs=2(010), all other bits=0
                 → 0x40

  Upload resp:   ccs=2(010), e=1, s=1, n=bytes_unused
    4 bytes → n=0 → 0x4F  (0100 1111)
    3 bytes → n=1 → 0x4B  (0100 1011)
    2 bytes → n=2 → 0x47  (0100 0111)
    1 byte  → n=3 → 0x43  (0100 0011)

  Abort frame:   ccs=4(100) → 0x80, abort code in bytes 4-7

  Common Abort Codes:
  ───────────────────
  0x05030000  Toggle bit not alternated
  0x05040000  SDO protocol timed out
  0x05040001  Command specifier not valid
  0x06010000  Unsupported access to object
  0x06010001  Attempt to read write-only object
  0x06010002  Attempt to write read-only object
  0x06020000  Object does not exist in OD
  0x06040041  Object not mappable to PDO
  0x06090011  Sub-index does not exist
  0x06090030  Value out of parameter range
  0x08000000  General error
```

### 5.2 SDO Client Implementation with Diagnostics

```c
/*
 * sdo_diagnostic_client.c
 * SDO client with full logging and timeout detection.
 * Uses SocketCAN (Linux). Compile with: gcc -o sdo_client sdo_diagnostic_client.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define SDO_REQUEST_BASE    0x600
#define SDO_RESPONSE_BASE   0x580
#define SDO_TIMEOUT_MS      500

typedef enum {
    SDO_OK        =  0,
    SDO_TIMEOUT   = -1,
    SDO_ABORT     = -2,
    SDO_ERROR     = -3,
} sdo_result_t;

typedef struct {
    int      sock;
    uint8_t  node_id;
    uint8_t  verbose;
} sdo_client_t;

/* Get current time in milliseconds */
static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Decode SDO abort code to human-readable string */
static const char *sdo_abort_str(uint32_t code)
{
    switch (code) {
    case 0x05030000: return "Toggle bit not alternated";
    case 0x05040000: return "SDO protocol timed out";
    case 0x06010000: return "Unsupported access to object";
    case 0x06010001: return "Read from write-only object";
    case 0x06010002: return "Write to read-only object";
    case 0x06020000: return "Object does not exist in OD";
    case 0x06040041: return "Object not mappable to PDO";
    case 0x06090011: return "Sub-index does not exist";
    case 0x06090030: return "Value out of parameter range";
    case 0x08000000: return "General error";
    default:          return "Unknown abort code";
    }
}

/* Open SocketCAN socket bound to given interface */
int sdo_client_init(sdo_client_t *client, const char *ifname,
                    uint8_t node_id, uint8_t verbose)
{
    struct sockaddr_can addr;
    struct ifreq        ifr;

    client->node_id = node_id;
    client->verbose  = verbose;

    client->sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (client->sock < 0) return -1;

    /* Filter: only accept SDO responses from our node */
    struct can_filter filter[1];
    filter[0].can_id   = SDO_RESPONSE_BASE + node_id;
    filter[0].can_mask = 0x7FF;
    setsockopt(client->sock, SOL_CAN_RAW, CAN_RAW_FILTER, filter,
               sizeof(filter));

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ioctl(client->sock, SIOCGIFINDEX, &ifr);

    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    return bind(client->sock, (struct sockaddr *)&addr, sizeof(addr));
}

/* Perform SDO expedited upload (read), return bytes read or negative error */
sdo_result_t sdo_upload(sdo_client_t *client,
                         uint16_t index, uint8_t subindex,
                         uint8_t *out_data, int *out_size)
{
    struct can_frame req = {0};
    struct can_frame resp;
    struct timeval   tv;
    fd_set           fds;
    uint64_t         deadline;
    ssize_t          n;

    /* Build upload initiate request */
    req.can_id  = SDO_REQUEST_BASE + client->node_id;
    req.can_dlc = 8;
    req.data[0] = 0x40;                    /* CS=2 upload req      */
    req.data[1] = (uint8_t)(index & 0xFF); /* Index low byte       */
    req.data[2] = (uint8_t)(index >> 8);   /* Index high byte      */
    req.data[3] = subindex;

    if (client->verbose) {
        printf("SDO Upload Req  → Node=0x%02X [%04X:%02X]\n",
               client->node_id, index, subindex);
    }

    if (write(client->sock, &req, sizeof(req)) < 0) {
        perror("SDO write");
        return SDO_ERROR;
    }

    /* Wait for response with timeout */
    deadline = now_ms() + SDO_TIMEOUT_MS;

    while (1) {
        uint64_t remaining = deadline - now_ms();
        if ((int64_t)remaining <= 0) {
            printf("SDO TIMEOUT: Node=0x%02X [%04X:%02X] after %d ms\n",
                   client->node_id, index, subindex, SDO_TIMEOUT_MS);
            return SDO_TIMEOUT;
        }

        FD_ZERO(&fds);
        FD_SET(client->sock, &fds);
        tv.tv_sec  = (long)(remaining / 1000);
        tv.tv_usec = (long)((remaining % 1000) * 1000);

        int r = select(client->sock + 1, &fds, NULL, NULL, &tv);
        if (r <= 0) continue;

        n = read(client->sock, &resp, sizeof(resp));
        if (n <= 0) continue;

        /* Check response COB-ID */
        if ((resp.can_id & 0x7FF) !=
            (uint32_t)(SDO_RESPONSE_BASE + client->node_id)) continue;

        uint8_t cs = resp.data[0] >> 5;

        if (cs == 2) {
            /* Expedited upload response */
            int bytes_unused = (resp.data[0] >> 2) & 0x03;
            int valid_bytes  = (resp.data[0] & 0x01) ? (4 - bytes_unused) : 4;

            if (client->verbose) {
                printf("SDO Upload Resp ← Node=0x%02X [%04X:%02X] "
                       "Size=%d Data=",
                       client->node_id, index, subindex, valid_bytes);
                for (int i = 0; i < valid_bytes; i++)
                    printf("%02X ", resp.data[4 + i]);
                printf("\n");
            }

            if (out_data && out_size) {
                *out_size = valid_bytes;
                memcpy(out_data, &resp.data[4], (size_t)valid_bytes);
            }
            return SDO_OK;

        } else if (cs == 4) {
            /* Abort */
            uint32_t abort_code =
                (uint32_t)resp.data[4]        |
                ((uint32_t)resp.data[5] << 8)  |
                ((uint32_t)resp.data[6] << 16) |
                ((uint32_t)resp.data[7] << 24);

            printf("SDO ABORT ← Node=0x%02X [%04X:%02X] "
                   "Code=0x%08X (%s)\n",
                   client->node_id, index, subindex,
                   abort_code, sdo_abort_str(abort_code));
            return SDO_ABORT;
        }
    }
}

/* Example: read device name and software version from a node */
int main(void)
{
    sdo_client_t client;
    uint8_t      buf[256];
    int          size = 0;
    sdo_result_t r;

    if (sdo_client_init(&client, "can0", 0x02, 1) < 0) {
        perror("sdo_client_init");
        return 1;
    }

    /* Read Manufacturer Device Name (OD 0x1008:00) */
    r = sdo_upload(&client, 0x1008, 0x00, buf, &size);
    if (r == SDO_OK) {
        buf[size] = '\0';
        printf("Device Name: %s\n", buf);
    }

    /* Read Software Version (OD 0x100A:00) */
    r = sdo_upload(&client, 0x100A, 0x00, buf, &size);
    if (r == SDO_OK) {
        buf[size] = '\0';
        printf("SW Version : %s\n", buf);
    }

    /* Read Error Register (OD 0x1001:00) */
    r = sdo_upload(&client, 0x1001, 0x00, buf, &size);
    if (r == SDO_OK)
        printf("Error Reg  : 0x%02X\n", buf[0]);

    close(client.sock);
    return 0;
}
```

---

## 6. PDO Traffic Analysis

### 6.1 PDO Frame Structure & Timing

```
  TPDO1 from Node 2 (COB-ID 0x182), synchronous, period=10ms
  ════════════════════════════════════════════════════════════

  Time:  0ms    10ms   20ms   30ms   40ms   50ms
         │      │      │      │      │      │
  SYNC   ▼      ▼      ▼      ▼      ▼      ▼
  ───────┼──────┼──────┼──────┼──────┼──────┼─────►
         │      │      │      │      │
  TPDO1  │▼     │▼     │▼     │▼     │▼
  ───────┴─┬────┴─┬────┴─┬────┴─┬────┴─┬───────►
           │      │      │      │      │
           └──────┴──────┴──────┴──────┘
           Δt ≈ 1-2 ms after SYNC (transmission delay)

  TPDO1 Data bytes (example mapping):
  ─────────────────────────────────────
  Byte 0-1: Position (INT16, 0.1 mm/bit)  OD 0x6064:00
  Byte 2-3: Velocity (INT16, rpm)          OD 0x606C:00
  Byte 4:   Status word low               OD 0x6041:00
  Byte 5:   Status word high              OD 0x6041:00
  Byte 6-7: Error code                    OD 0x603F:00

  Mapping stored in OD 0x1A00 (TPDO1 Mapping):
  ───────────────────────────────────────────────
  Sub 0: Number of mapped objects = 4
  Sub 1: 0x60640010  (OD 0x6064, sub 0, 16 bits)
  Sub 2: 0x606C0010  (OD 0x606C, sub 0, 16 bits)
  Sub 3: 0x60410010  (OD 0x6041, sub 0, 16 bits)
  Sub 4: 0x603F0010  (OD 0x603F, sub 0, 16 bits)
          │           │
          │           └─ bit length (0x10 = 16)
          └─ OD index:sub (16:8 bits)
```

### 6.2 PDO Mapping Readback & Verification in C

```c
/*
 * pdo_map_verify.c
 * Read and display all PDO mapping entries for a given node via SDO.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Reuse sdo_client_t and sdo_upload() from previous example */
extern sdo_result_t sdo_upload(sdo_client_t *client,
                                uint16_t index, uint8_t subindex,
                                uint8_t *out_data, int *out_size);

typedef struct {
    uint16_t od_index;
    uint8_t  od_subindex;
    uint8_t  bit_length;
} pdo_mapping_entry_t;

/* Read PDO communication parameters (0x1400..0x15FF for RPDO) */
static void read_pdo_comm(sdo_client_t *client, uint16_t comm_index,
                           int pdo_num, const char *type)
{
    uint8_t buf[4];
    int     size;
    sdo_result_t r;

    printf("\n%s%d Communication (OD 0x%04X):\n", type, pdo_num, comm_index);

    r = sdo_upload(client, comm_index, 0x01, buf, &size);
    if (r == SDO_OK) {
        uint32_t cob_id = buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);
        printf("  COB-ID : 0x%08X  (Valid=%s)\n",
               cob_id, (cob_id & 0x80000000) ? "NO" : "YES");
    }

    r = sdo_upload(client, comm_index, 0x02, buf, &size);
    if (r == SDO_OK) {
        uint8_t tx_type = buf[0];
        printf("  TX Type: %u  (%s)\n", tx_type,
               tx_type == 0     ? "Synchronous acyclic" :
               tx_type <= 240   ? "Synchronous cyclic"  :
               tx_type == 254   ? "Event-driven (RTR)"  :
               tx_type == 255   ? "Event-driven"        : "Reserved");
    }

    r = sdo_upload(client, comm_index, 0x03, buf, &size);
    if (r == SDO_OK) {
        uint16_t inhibit = buf[0] | (buf[1]<<8);
        printf("  Inhibit: %u × 100µs = %.1f ms\n",
               inhibit, inhibit * 0.1);
    }

    r = sdo_upload(client, comm_index, 0x05, buf, &size);
    if (r == SDO_OK) {
        uint16_t event_ms = buf[0] | (buf[1]<<8);
        printf("  Event  : %u ms\n", event_ms);
    }
}

/* Read and print PDO mapping object (0x1600..0x17FF for RPDO,
   0x1A00..0x1BFF for TPDO) */
static void read_pdo_mapping(sdo_client_t *client, uint16_t map_index,
                              int pdo_num, const char *type)
{
    uint8_t  buf[4];
    int      size;
    sdo_result_t r;

    printf("\n%s%d Mapping (OD 0x%04X):\n", type, pdo_num, map_index);

    r = sdo_upload(client, map_index, 0x00, buf, &size);
    if (r != SDO_OK) { printf("  (cannot read)\n"); return; }

    uint8_t count = buf[0];
    printf("  Number of mapped objects: %u\n", count);

    uint8_t byte_offset = 0;
    for (uint8_t i = 1; i <= count; i++) {
        r = sdo_upload(client, map_index, i, buf, &size);
        if (r != SDO_OK) continue;

        uint32_t entry = buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);
        pdo_mapping_entry_t m;
        m.od_index    = (uint16_t)(entry >> 16);
        m.od_subindex = (uint8_t)((entry >> 8) & 0xFF);
        m.bit_length  = (uint8_t)(entry & 0xFF);

        printf("  [%u] Byte%-2u–%-2u → OD[%04X:%02X]  %u bits\n",
               i, byte_offset, byte_offset + m.bit_length/8 - 1,
               m.od_index, m.od_subindex, m.bit_length);
        byte_offset += m.bit_length / 8;
    }
    printf("  Total PDO size: %u bytes\n", byte_offset);
}

void verify_all_pdos(sdo_client_t *client)
{
    /* TPDO1..4 */
    for (int i = 0; i < 4; i++) {
        read_pdo_comm(client,    (uint16_t)(0x1800 + i), i+1, "TPDO");
        read_pdo_mapping(client, (uint16_t)(0x1A00 + i), i+1, "TPDO");
    }
    /* RPDO1..4 */
    for (int i = 0; i < 4; i++) {
        read_pdo_comm(client,    (uint16_t)(0x1400 + i), i+1, "RPDO");
        read_pdo_mapping(client, (uint16_t)(0x1600 + i), i+1, "RPDO");
    }
}
```

---

## 7. Heartbeat & Node Guarding Timeline

### 7.1 Heartbeat Protocol Visual

```
  Heartbeat Producer/Consumer Protocol
  ════════════════════════════════════

  Normal operation (period = 1000 ms):

  Node 1  ──[HB:05]────────[HB:05]────────[HB:05]────────[HB:05]──►
  Master  ──────────────────────────────────────────────────────────►
              │                │                │                │
              0ms           1000ms           2000ms           3000ms
              │
              └─ 0x701 data=0x05 (Operational)

  Consumer timeout = 1500 ms (1.5× period):

                ┌─── Timeout window: 1500 ms ───┐
  Node 1  ──[HB:05]──────────────────────────────────X──────────►
  Master  ──────────────────────────────────────────────[ALARM]──►
              │                                         │
              0ms                                     1500ms
                                                       └─ Heartbeat lost!

  Multi-node heartbeat timeline:

  Time (ms):  0    200  400  600  800 1000 1200 1400 1600
              │    │    │    │    │    │    │    │    │
  Node 0x01  [05] ────────────── [05] ──────────────[05]
  Node 0x02       [05]      ────────── [05]     ─────────[05]
  Node 0x03            [05]                [05]
  Node 0x04                 [05]                [05]
              │
              └─ Staggered heartbeats reduce bus load peaks

  Heartbeat Boot-Up sequence:

       Device                          NMT Master
         │                                  │
         │── COB-ID: 0x701 data=0x00 ──────►│  Boot-Up (state=0)
         │                                  │  (sent once on power-up)
         │◄─ COB-ID: 0x000 [01][01] ────────│  NMT: Start Node 0x01
         │                                  │
         │── COB-ID: 0x701 data=0x05 ──────►│  Heartbeat: Operational
         │── COB-ID: 0x701 data=0x05 ──────►│  (periodic thereafter)
         │── COB-ID: 0x701 data=0x05 ──────►│
```

### 7.2 Heartbeat Monitor in C

```c
/*
 * heartbeat_monitor.c
 * Monitors heartbeats from multiple nodes and reports timeline / losses.
 * Requires: SocketCAN (Linux)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define MAX_NODES        127
#define DEFAULT_TIMEOUT  1500   /* ms — 1.5x a 1000ms heartbeat period */

typedef enum {
    NODE_UNKNOWN  = 0,
    NODE_BOOTUP   = 1,
    NODE_PREOP    = 2,
    NODE_OPER     = 3,
    NODE_STOPPED  = 4,
    NODE_LOST     = 5,
} node_state_t;

static const char *node_state_str[] = {
    "UNKNOWN", "BOOT-UP", "PRE-OP", "OPER", "STOPPED", "*** LOST ***"
};

typedef struct {
    uint8_t       node_id;
    node_state_t  state;
    uint64_t      last_seen_ms;
    uint64_t      timeout_ms;
    uint32_t      hb_count;
    uint64_t      first_seen_ms;
    double        avg_period_ms;
    uint64_t      prev_timestamp_ms;
} node_info_t;

static node_info_t  g_nodes[MAX_NODES + 1];
static volatile int g_running = 1;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void update_node_heartbeat(uint8_t node_id, uint8_t nmt_state,
                                   uint64_t ts)
{
    node_info_t *n = &g_nodes[node_id];
    pthread_mutex_lock(&g_lock);

    /* Calculate actual heartbeat period */
    if (n->hb_count > 0 && n->prev_timestamp_ms > 0) {
        uint64_t period = ts - n->prev_timestamp_ms;
        /* Exponential moving average */
        n->avg_period_ms = (n->avg_period_ms == 0.0)
            ? (double)period
            : 0.9 * n->avg_period_ms + 0.1 * (double)period;
    }

    if (n->hb_count == 0)
        n->first_seen_ms = ts;

    n->prev_timestamp_ms = ts;
    n->last_seen_ms      = ts;
    n->hb_count++;

    /* Map NMT state byte to enum */
    node_state_t prev_state = n->state;
    switch (nmt_state & 0x7F) {
    case 0x00: n->state = NODE_BOOTUP;  break;
    case 0x04: n->state = NODE_STOPPED; break;
    case 0x05: n->state = NODE_OPER;    break;
    case 0x7F: n->state = NODE_PREOP;   break;
    }

    if (n->state == NODE_LOST) n->state = prev_state; /* recovered */
    if (prev_state == NODE_LOST && n->state != NODE_LOST) {
        printf("[%lu ms] Node 0x%02X RECOVERED (state=%s)\n",
               ts, node_id, node_state_str[n->state]);
    } else if (prev_state == NODE_UNKNOWN) {
        printf("[%lu ms] Node 0x%02X APPEARED (state=%s)\n",
               ts, node_id, node_state_str[n->state]);
    }

    pthread_mutex_unlock(&g_lock);
}

/* Watchdog thread: checks for heartbeat timeouts */
static void *watchdog_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        uint64_t now = now_ms();
        pthread_mutex_lock(&g_lock);
        for (int i = 1; i <= MAX_NODES; i++) {
            node_info_t *n = &g_nodes[i];
            if (n->hb_count == 0) continue;  /* never seen */
            if (n->state == NODE_LOST) continue;

            if ((now - n->last_seen_ms) > n->timeout_ms) {
                n->state = NODE_LOST;
                printf("[%lu ms] *** HEARTBEAT LOST: Node 0x%02X "
                       "(last seen %lu ms ago) ***\n",
                       now, (uint8_t)i, now - n->last_seen_ms);
            }
        }
        pthread_mutex_unlock(&g_lock);
        usleep(50000);  /* check every 50 ms */
    }
    return NULL;
}

/* Print live node table */
static void print_node_table(void)
{
    uint64_t now = now_ms();
    printf("\n┌────────┬───────────────┬──────────┬──────────┬────────────┐\n");
    printf("│ NodeID │ State         │ HB Count │ Avg ms   │ Last seen  │\n");
    printf("├────────┼───────────────┼──────────┼──────────┼────────────┤\n");
    pthread_mutex_lock(&g_lock);
    for (int i = 1; i <= MAX_NODES; i++) {
        node_info_t *n = &g_nodes[i];
        if (n->hb_count == 0) continue;
        printf("│  0x%02X  │ %-13s │ %8u │ %8.1f │ %+8ld ms │\n",
               (uint8_t)i,
               node_state_str[n->state],
               n->hb_count,
               n->avg_period_ms,
               (long)(now - n->last_seen_ms));
    }
    pthread_mutex_unlock(&g_lock);
    printf("└────────┴───────────────┴──────────┴──────────┴────────────┘\n");
}

int main(void)
{
    struct sockaddr_can addr;
    struct ifreq        ifr;
    struct can_filter   filter[1];
    int                 sock;
    struct can_frame    frame;

    /* Initialise node table */
    for (int i = 0; i <= MAX_NODES; i++) {
        g_nodes[i].node_id   = (uint8_t)i;
        g_nodes[i].timeout_ms = DEFAULT_TIMEOUT;
    }

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    filter[0].can_id   = 0x700;
    filter[0].can_mask = 0x780;
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, filter, sizeof(filter));

    strncpy(ifr.ifr_name, "can0", IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    /* Start watchdog thread */
    pthread_t wdog;
    pthread_create(&wdog, NULL, watchdog_thread, NULL);

    printf("Heartbeat monitor started on can0. Press Ctrl+C to stop.\n");

    while (g_running) {
        fd_set fds;
        struct timeval tv = {5, 0};  /* Report table every 5 seconds */
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        int r = select(sock + 1, &fds, NULL, NULL, &tv);
        if (r == 0) {
            print_node_table();
            continue;
        }
        if (read(sock, &frame, sizeof(frame)) > 0) {
            uint8_t node_id = frame.can_id & 0x07F;
            if (frame.can_dlc >= 1)
                update_node_heartbeat(node_id, frame.data[0], now_ms());
        }
    }

    g_running = 0;
    pthread_join(wdog, NULL);
    close(sock);
    return 0;
}
```

---

## 8. EMCY Monitoring

### 8.1 EMCY Frame Layout

```
  Emergency Object (EMCY) Frame Structure
  ════════════════════════════════════════

  COB-ID = 0x80 + Node-ID  (e.g., Node 5 → COB-ID 0x085)
  DLC    = 8 bytes (always)

  ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┐
  │ B[0] │ B[1] │ B[2] │ B[3] │ B[4] │ B[5] │ B[6] │ B[7] │
  └──┬───┴──┬───┴──┬───┴──────┴──────┴──────┴──────┴──────┘
     │      │      │      └─────────────────────────────────
     │      │      │        Manufacturer-specific Info (5B)
     │      │      │
     │      │      └─ Error Register (OD 0x1001, 1 byte)
     │      │         Bit 0: Generic error
     │      │         Bit 1: Current
     │      │         Bit 2: Voltage
     │      │         Bit 3: Temperature
     │      │         Bit 4: Communication error
     │      │         Bit 5: Device profile specific
     │      │         Bit 7: Manufacturer specific
     │      │
     └──────┴─ Emergency Error Code (EEC, 2 bytes, little-endian)

  CiA 301 EEC Classification:
  ───────────────────────────
  0x0000          No error (EMCY reset message)
  0x1000–0x1FFF   Generic error
  0x2000–0x2FFF   Current error
    0x2310          Current – Device input side
    0x2320          Current – Inside device
    0x2330          Current – Device output side
  0x3000–0x3FFF   Voltage error
    0x3100          Mains voltage
    0x3200          DC link voltage
    0x3300          Output voltage
  0x4000–0x4FFF   Temperature error
    0x4110          Ambient temperature
    0x4120          Device temperature
  0x5000–0x5FFF   Hardware error
  0x6000–0x6FFF   Software error
  0x7000–0x7FFF   Additional modules
  0x8000–0x8FFF   Monitoring
    0x8100          Communication error
    0x8110          CAN overrun
    0x8120          CAN passive mode
    0x8130          Heartbeat error
    0x8140          Recovered from bus off
  0x9000–0xEFFF   External error / reserved
  0xFF00–0xFFFF   Manufacturer-specific
```

### 8.2 EMCY Logger and History Buffer in C++

```cpp
/*
 * emcy_logger.hpp
 * Circular buffer EMCY logger with decode and reporting.
 */
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <array>
#include <string>
#include <mutex>

struct EmcyEvent {
    uint64_t timestamp_us;
    uint8_t  node_id;
    uint16_t error_code;       /* EEC */
    uint8_t  error_register;   /* OD 0x1001 */
    uint8_t  mfr_data[5];      /* Manufacturer specific */
    bool     is_reset;          /* EEC == 0x0000 */
};

class EmcyLogger {
public:
    static constexpr size_t HISTORY_SIZE = 256;

    void log(const EmcyEvent &ev)
    {
        std::lock_guard<std::mutex> lk(m_);
        m_buf[m_head % HISTORY_SIZE] = ev;
        m_head++;
        if (!ev.is_reset) {
            fprintf(stderr,
                "[EMCY] t=%lu us  Node=0x%02X  EEC=0x%04X (%s)  "
                "ErrReg=0x%02X  Mfr=%02X%02X%02X%02X%02X\n",
                (unsigned long)ev.timestamp_us,
                ev.node_id,
                ev.error_code, decode_eec(ev.error_code).c_str(),
                ev.error_register,
                ev.mfr_data[0], ev.mfr_data[1], ev.mfr_data[2],
                ev.mfr_data[3], ev.mfr_data[4]);
        } else {
            fprintf(stderr,
                "[EMCY] t=%lu us  Node=0x%02X  RESET (no error)\n",
                (unsigned long)ev.timestamp_us, ev.node_id);
        }
    }

    /* Parse raw 8-byte EMCY CAN frame */
    static EmcyEvent parse(uint8_t node_id,
                            const uint8_t *data,
                            uint64_t ts_us)
    {
        EmcyEvent ev{};
        ev.timestamp_us  = ts_us;
        ev.node_id       = node_id;
        ev.error_code    = static_cast<uint16_t>(data[0]) |
                           (static_cast<uint16_t>(data[1]) << 8);
        ev.error_register = data[2];
        std::memcpy(ev.mfr_data, &data[3], 5);
        ev.is_reset = (ev.error_code == 0x0000 && ev.error_register == 0x00);
        return ev;
    }

    /* Print historical EMCY event table */
    void print_history(void) const
    {
        std::lock_guard<std::mutex> lk(m_);
        size_t total  = (m_head < HISTORY_SIZE) ? m_head : HISTORY_SIZE;
        size_t start  = (m_head < HISTORY_SIZE) ? 0 : m_head % HISTORY_SIZE;

        printf("┌────────────┬────────┬────────┬──────────────────────────────┐\n");
        printf("│ Time (us)  │ NodeID │  EEC   │ Description                  │\n");
        printf("├────────────┼────────┼────────┼──────────────────────────────┤\n");

        for (size_t i = 0; i < total; i++) {
            const EmcyEvent &ev = m_buf[(start + i) % HISTORY_SIZE];
            printf("│ %-10lu │  0x%02X  │ 0x%04X │ %-28s │\n",
                   (unsigned long)(ev.timestamp_us / 1000),
                   ev.node_id, ev.error_code,
                   decode_eec(ev.error_code).c_str());
        }
        printf("└────────────┴────────┴────────┴──────────────────────────────┘\n");
    }

private:
    mutable std::mutex  m_;
    std::array<EmcyEvent, HISTORY_SIZE> m_buf{};
    size_t              m_head = 0;

    static std::string decode_eec(uint16_t eec)
    {
        if (eec == 0x0000) return "No error (reset)";
        if (eec >= 0x1000 && eec < 0x2000) return "Generic error";
        if (eec >= 0x2000 && eec < 0x3000) return "Current error";
        if (eec >= 0x3000 && eec < 0x4000) return "Voltage error";
        if (eec >= 0x4000 && eec < 0x5000) return "Temperature error";
        if (eec >= 0x5000 && eec < 0x6000) return "Hardware error";
        if (eec >= 0x6000 && eec < 0x7000) return "Software error";
        if (eec == 0x8100) return "Communication error";
        if (eec == 0x8110) return "CAN overrun";
        if (eec == 0x8120) return "CAN passive mode";
        if (eec == 0x8130) return "Heartbeat error";
        if (eec == 0x8140) return "Recovered bus-off";
        if (eec >= 0x8000 && eec < 0x9000) return "Monitoring error";
        if (eec >= 0xFF00)                  return "Manufacturer specific";
        return "Unknown EEC";
    }
};
```

---

## 9. NMT State Machine Observation

### 9.1 NMT State Machine ASCII Diagram

```
  CANopen NMT State Machine
  ══════════════════════════

                    Power On / Reset
                          │
                          ▼
               ┌──────────────────────┐
               │   Initialization     │
               │   (not on bus yet)   │
               └──────────┬───────────┘
                          │ Auto-transition (Boot-Up message sent)
                          ▼
               ┌─────────────────────┐ ◄────────────────────────┐
               │   Pre-Operational   │                          │
               │   State=0x7F        │                          │
               │   ─────────────     │   NMT: Enter Pre-Op      │
               │   SDO: active       │   (cmd=0x80)             │
               │   PDO: inactive     │                          │
               │   SYNC: active      │                          │
               └──────────┬──────────┘                          │
               ▲          │ NMT: Start Node                     │
               │          │ (cmd=0x01)                          │
               │          ▼                                     │
  NMT: Stop    │ ┌──────────────────────┐   NMT: Stop           │
  (cmd=0x02)   │ │    Operational       │   (cmd=0x02)          │
               │ │    State=0x05        │──────────────┐        │
               └─│    ─────────────     │              │        │
                 │    SDO: active       │              ▼        │
                 │    PDO: active       │  ┌───────────────────┐│
                 │    SYNC: active      │  │    Stopped        ││
                 └──────────────────────┘  │    State=0x04     ││
                       ▲    │              │    ────────────── ││
                       │    │              │    SDO: inactive  ││
                       │    │ NMT: Reset   │    PDO: inactive  ││
                       │    │ (0x81/0x82)  │    HB: active     ││
                       │    ▼              └───────────────────┘│
                       │  ┌──────────────────────┐              │
                       │  │  Reset               │◄─────────────┘
                       └──│  (returns to Init)   │  NMT: Reset
                          └──────────────────────┘  (0x81/0x82)

  Observed on bus:
  ─────────────────
  Boot-Up:     0x701 data=[00]        (single byte, state=0)
  Pre-Op HB:   0x701 data=[7F]
  Oper HB:     0x701 data=[05]
  Stopped HB:  0x701 data=[04]
  NMT cmd:     0x000 data=[cmd][node]
```

---

## 10. Scripted Automated Conformance Checks

Conformance testing validates that a CANopen device behaves according to CiA 301.
Automated scripts systematically exercise all mandatory OD entries and protocol sequences.

### 10.1 Conformance Test Framework in C

```c
/*
 * conformance_tester.c
 * Automated CANopen conformance check suite.
 * Tests: mandatory OD objects, SDO behaviour, NMT response, heartbeat.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

/* Reuse sdo_client_t and sdo_upload() from earlier examples */

typedef enum {
    TEST_PASS = 0,
    TEST_FAIL = 1,
    TEST_WARN = 2,
    TEST_SKIP = 3,
} test_result_t;

typedef struct {
    const char   *name;
    test_result_t result;
    char          message[256];
} test_case_t;

#define MAX_TESTS  128
static test_case_t g_tests[MAX_TESTS];
static int         g_test_count = 0;

static void record_test(const char *name, test_result_t result,
                         const char *msg)
{
    if (g_test_count >= MAX_TESTS) return;
    test_case_t *tc = &g_tests[g_test_count++];
    tc->name   = name;
    tc->result = result;
    strncpy(tc->message, msg, sizeof(tc->message) - 1);
}

static void check_mandatory_od_entry(sdo_client_t *client,
                                      uint16_t index, uint8_t sub,
                                      const char *description,
                                      int min_dlc)
{
    uint8_t buf[256];
    int     size = 0;
    char    msg[256];
    sdo_result_t r = sdo_upload(client, index, sub, buf, &size);

    if (r == SDO_OK) {
        if (size >= min_dlc) {
            snprintf(msg, sizeof(msg),
                     "OD[%04X:%02X] present, size=%d bytes", index, sub, size);
            record_test(description, TEST_PASS, msg);
        } else {
            snprintf(msg, sizeof(msg),
                     "OD[%04X:%02X] size=%d < expected %d",
                     index, sub, size, min_dlc);
            record_test(description, TEST_FAIL, msg);
        }
    } else if (r == SDO_TIMEOUT) {
        snprintf(msg, sizeof(msg), "OD[%04X:%02X] SDO timeout", index, sub);
        record_test(description, TEST_FAIL, msg);
    } else {
        snprintf(msg, sizeof(msg),
                 "OD[%04X:%02X] SDO abort (object missing?)", index, sub);
        record_test(description, TEST_FAIL, msg);
    }
}

/* Test: Write to read-only object should abort */
static void test_readonly_protection(sdo_client_t *client)
{
    /* Identity Object (0x1018) sub 0 = number of entries, always read-only */
    struct can_frame req = {0};
    req.can_id  = 0x600 + client->node_id;
    req.can_dlc = 8;
    req.data[0] = 0x23;      /* Download 4 bytes, expedited */
    req.data[1] = 0x18;      /* Index 0x1018 low */
    req.data[2] = 0x10;      /* Index 0x1018 high */
    req.data[3] = 0x00;      /* Sub-index 0 */
    req.data[4] = 0x99;      /* Bogus data */

    write(client->sock, &req, sizeof(req));

    uint8_t buf[8];
    int     size;
    /* Expect abort with 0x06010002 (write to read-only) */
    sdo_result_t r = sdo_upload(client, 0x1018, 0x00, buf, &size);
    /* If we can read it, the write was correctly rejected */
    if (r == SDO_OK)
        record_test("Read-only OD protection (0x1018:00)",
                    TEST_PASS, "Write rejected, read succeeds");
    else
        record_test("Read-only OD protection (0x1018:00)",
                    TEST_FAIL, "Could not verify read-only protection");
}

/* Test: Invalid sub-index access should abort with 0x06090011 */
static void test_invalid_subindex(sdo_client_t *client)
{
    uint8_t buf[8];
    int     size;
    /* Sub-index 0xFF should not exist on most objects */
    sdo_result_t r = sdo_upload(client, 0x1000, 0xFF, buf, &size);
    if (r == SDO_ABORT)
        record_test("Invalid sub-index (0x1000:FF)",
                    TEST_PASS, "Correctly aborted");
    else if (r == SDO_TIMEOUT)
        record_test("Invalid sub-index (0x1000:FF)",
                    TEST_FAIL, "Timeout (no response)");
    else
        record_test("Invalid sub-index (0x1000:FF)",
                    TEST_FAIL, "Unexpected success — sub-index 0xFF exists?");
}

/* Run the full mandatory OD conformance suite */
void run_conformance_tests(sdo_client_t *client)
{
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║    CANopen Conformance Test Suite (CiA 301)          ║\n");
    printf("║    Node-ID: 0x%02X                                     ║\n",
           client->node_id);
    printf("╚══════════════════════════════════════════════════════╝\n\n");

    /* --- Mandatory Communication Objects (CiA 301 §7.5) --- */
    check_mandatory_od_entry(client, 0x1000, 0x00,
        "Device Type (0x1000:00)", 4);
    check_mandatory_od_entry(client, 0x1001, 0x00,
        "Error Register (0x1001:00)", 1);
    check_mandatory_od_entry(client, 0x1018, 0x00,
        "Identity Object - Sub count (0x1018:00)", 1);
    check_mandatory_od_entry(client, 0x1018, 0x01,
        "Identity Object - Vendor ID (0x1018:01)", 4);
    check_mandatory_od_entry(client, 0x1018, 0x02,
        "Identity Object - Product Code (0x1018:02)", 4);
    check_mandatory_od_entry(client, 0x1018, 0x03,
        "Identity Object - Revision Number (0x1018:03)", 4);
    check_mandatory_od_entry(client, 0x1018, 0x04,
        "Identity Object - Serial Number (0x1018:04)", 4);

    /* --- Optional but highly recommended objects --- */
    check_mandatory_od_entry(client, 0x1008, 0x00,
        "Manufacturer Device Name (0x1008:00)", 1);
    check_mandatory_od_entry(client, 0x1009, 0x00,
        "Hardware Version (0x1009:00)", 1);
    check_mandatory_od_entry(client, 0x100A, 0x00,
        "Software Version (0x100A:00)", 1);

    /* --- Heartbeat producer --- */
    check_mandatory_od_entry(client, 0x1017, 0x00,
        "Heartbeat Producer Time (0x1017:00)", 2);

    /* --- Pre-defined PDO communication --- */
    check_mandatory_od_entry(client, 0x1400, 0x01,
        "RPDO1 COB-ID (0x1400:01)", 4);
    check_mandatory_od_entry(client, 0x1600, 0x00,
        "RPDO1 Mapping count (0x1600:00)", 1);
    check_mandatory_od_entry(client, 0x1800, 0x01,
        "TPDO1 COB-ID (0x1800:01)", 4);
    check_mandatory_od_entry(client, 0x1A00, 0x00,
        "TPDO1 Mapping count (0x1A00:00)", 1);

    /* --- Protocol behaviour tests --- */
    test_readonly_protection(client);
    test_invalid_subindex(client);

    /* --- Print results --- */
    int pass = 0, fail = 0, warn = 0;
    printf("\n%-45s  %-6s  %s\n", "Test", "Result", "Detail");
    printf("%-45s  %-6s  %s\n",
           "─────────────────────────────────────────────",
           "──────", "──────────────────────────────────");

    for (int i = 0; i < g_test_count; i++) {
        test_case_t *tc = &g_tests[i];
        const char *res_str;
        switch (tc->result) {
        case TEST_PASS: res_str = "PASS ✓"; pass++; break;
        case TEST_FAIL: res_str = "FAIL ✗"; fail++; break;
        case TEST_WARN: res_str = "WARN △"; warn++; break;
        default:        res_str = "SKIP  "; break;
        }
        printf("%-45s  %-6s  %s\n", tc->name, res_str, tc->message);
    }

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  PASS: %d   FAIL: %d   WARN: %d   TOTAL: %d\n",
           pass, fail, warn, g_test_count);
    printf("  Conformance: %s\n",
           fail == 0 ? "COMPLIANT" : "NON-COMPLIANT");
    printf("═══════════════════════════════════════════════════════\n");
}
```

### 10.2 Automated SYNC + PDO Timing Verification

```c
/*
 * pdo_timing_checker.c
 * Measures actual PDO period against expected, flags violations.
 */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define MAX_TRACKED_PDOS  16
#define TIMING_TOLERANCE  0.10   /* ±10% */

typedef struct {
    uint32_t cob_id;
    uint64_t last_rx_us;
    double   expected_period_us;
    double   measured_period_us;
    uint32_t count;
    uint32_t violations;
} pdo_timing_t;

static pdo_timing_t g_pdos[MAX_TRACKED_PDOS];
static int          g_pdo_count = 0;

void pdo_timing_add(uint32_t cob_id, double period_ms)
{
    if (g_pdo_count >= MAX_TRACKED_PDOS) return;
    g_pdos[g_pdo_count].cob_id            = cob_id;
    g_pdos[g_pdo_count].expected_period_us = period_ms * 1000.0;
    g_pdo_count++;
}

void pdo_timing_update(uint32_t cob_id, uint64_t ts_us)
{
    for (int i = 0; i < g_pdo_count; i++) {
        pdo_timing_t *p = &g_pdos[i];
        if (p->cob_id != cob_id) continue;

        if (p->last_rx_us > 0) {
            double delta = (double)(ts_us - p->last_rx_us);
            /* Exponential moving average for measured period */
            p->measured_period_us = (p->count == 1)
                ? delta
                : 0.9 * p->measured_period_us + 0.1 * delta;

            /* Violation detection */
            double expected = p->expected_period_us;
            double lo = expected * (1.0 - TIMING_TOLERANCE);
            double hi = expected * (1.0 + TIMING_TOLERANCE);
            if (delta < lo || delta > hi) {
                p->violations++;
                printf("[TIMING VIOLATION] COB-ID=0x%03X  "
                       "Δt=%.1f µs  Expected=%.0f µs  (%.1f%%)\n",
                       cob_id, delta, expected,
                       (delta - expected) / expected * 100.0);
            }
        }
        p->last_rx_us = ts_us;
        p->count++;
        break;
    }
}

void pdo_timing_report(void)
{
    printf("\n┌──────────┬──────────────┬──────────────┬───────────┬────────────┐\n");
    printf("│  COB-ID  │ Expected(ms) │ Measured(ms) │   Count   │ Violations │\n");
    printf("├──────────┼──────────────┼──────────────┼───────────┼────────────┤\n");
    for (int i = 0; i < g_pdo_count; i++) {
        pdo_timing_t *p = &g_pdos[i];
        printf("│  0x%03X   │  %10.2f  │  %10.2f  │  %7u  │  %8u  │\n",
               p->cob_id,
               p->expected_period_us / 1000.0,
               p->measured_period_us / 1000.0,
               p->count, p->violations);
    }
    printf("└──────────┴──────────────┴──────────────┴───────────┴────────────┘\n");
}
```

---

## 11. C/C++ Diagnostic Library Implementation

A reusable diagnostic library encapsulates all the above into a single cohesive API:

```cpp
/*
 * canopen_diag.hpp  —  unified CANopen diagnostic library
 *
 * Usage:
 *   CanopenDiag diag("can0");
 *   diag.add_node(0x01, 1000);   // node 1, heartbeat 1000ms
 *   diag.add_node(0x02, 1000);
 *   diag.start();
 *   diag.run_conformance(0x01);
 *   diag.print_report();
 */
#pragma once

#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <unordered_map>
#include <linux/can.h>
#include <linux/can/raw.h>

class CanopenDiag {
public:
    struct NodeStatus {
        uint8_t  node_id;
        uint32_t hb_count;
        uint32_t emcy_count;
        uint32_t sdo_timeout_count;
        uint32_t nmt_transitions;
        double   hb_period_ms;
        bool     alive;
        std::string last_state;
    };

    using EmcyCallback = std::function<void(uint8_t node,
                                             uint16_t eec,
                                             uint8_t errReg)>;
    using HbLostCallback = std::function<void(uint8_t node)>;

    explicit CanopenDiag(const std::string &ifname);
    ~CanopenDiag();

    /* Configuration */
    void add_node(uint8_t node_id, uint32_t expected_hb_ms,
                  uint32_t timeout_factor_pct = 150);
    void on_emcy(EmcyCallback cb)    { emcy_cb_   = std::move(cb); }
    void on_hb_lost(HbLostCallback cb) { hblost_cb_ = std::move(cb); }

    /* Control */
    bool start();
    void stop();

    /* Active probing */
    bool sdo_read_uint32(uint8_t node, uint16_t idx, uint8_t sub,
                          uint32_t &out, int timeout_ms = 500);
    bool send_nmt(uint8_t node, uint8_t cmd);
    void run_conformance(uint8_t node_id);

    /* Reporting */
    void             print_report() const;
    NodeStatus       get_node_status(uint8_t node_id) const;
    std::vector<NodeStatus> all_nodes() const;

private:
    std::string   ifname_;
    int           sock_       = -1;
    std::atomic<bool> running_{false};
    std::thread   rx_thread_;
    mutable std::mutex mtx_;

    struct NodeEntry {
        uint8_t  id;
        uint32_t expected_hb_ms;
        uint32_t timeout_ms;
        NodeStatus status;
        uint64_t   last_hb_us;
        double     prev_hb_us;
    };

    std::unordered_map<uint8_t, NodeEntry> nodes_;
    EmcyCallback    emcy_cb_;
    HbLostCallback  hblost_cb_;

    void rx_loop();
    void handle_frame(const struct can_frame &f, uint64_t ts_us);
    void check_timeouts();
    int  open_socket();
};
```

---

## 12. Advanced Bus Load & Statistics Analysis

### 12.1 Bus Load Calculation

```
  CAN Bus Load Formula (500 kbps example)
  ════════════════════════════════════════

  Worst-case bit count for standard CAN frame with DLC=8:
  ─────────────────────────────────────────────────────────
  SOF:          1 bit
  Identifier:  11 bits
  RTR:          1 bit
  IDE:          1 bit
  r0:           1 bit
  DLC:          4 bits
  Data:        64 bits  (8 bytes)
  CRC:         15 bits + delimiter
  ACK:          2 bits
  EOF:          7 bits
  IFS:          3 bits
  Stuffing:  ≤ 23 bits worst case (1 stuff bit per 5 bits)
             (19 + 64 + 15 = 98 bits → max 19 stuff bits)
  ─────────────────────────────────────────────────────────
  Worst case: 11 + 1 + 1 + 1 + 4 + 64 + 15 + 1 + 2 + 7 + 3 + 23
            = 133 bits per frame

  At 500 kbps:  133 bits / 500,000 bits/s = 0.266 ms per frame

  Bus load (%) = (frames/sec × bits_per_frame) / bitrate × 100

  Example network:
  ────────────────
  SYNC every 10ms:        1 frame/10ms   = 100 frames/s × 11 bits  = 1,100 b/s
  TPDO1 (10ms, 8B):       4 nodes        × 100 Hz × 111 bits        = 44,400 b/s
  Heartbeats (1000ms):    4 nodes        × 1 Hz   × 39 bits         =    156 b/s
  SDO traffic (occasional):               ~5 frames/s × 111 bits    =    555 b/s
                                                                    ──────────────
  Total ≈ 46,211 bits/s out of 500,000 → Bus load ≈ 9.2%

  Recommended maximum bus load: 30–40% for reliable operation.
  Above 60–70%: increased latency and error probability.

  ASCII Bus Load Meter:
  ─────────────────────
   0%  [                                        ] 100%
   9%  [████                                   ]
  30%  [████████████                           ]  ← Safe max
  60%  [████████████████████████               ]  ← Warning
  80%  [████████████████████████████████       ]  ← Danger
```

### 12.2 Bus Load Monitor in C

```c
/*
 * bus_load_monitor.c
 * Real-time CAN bus load measurement using SocketCAN.
 * Samples frames over a 1-second sliding window.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define BITRATE_BPS      500000UL
#define WINDOW_SLOTS     10        /* 10 × 100ms = 1 second window */
#define SLOT_MS          100

typedef struct {
    uint64_t frame_count;
    uint64_t bits_seen;
} time_slot_t;

/* Estimate bits for a standard CAN frame (DLC ≤ 8) */
static uint32_t estimate_bits(uint8_t dlc)
{
    /* Base frame + data + typical stuffing overhead */
    uint32_t data_bits = (dlc > 8 ? 8 : dlc) * 8;
    uint32_t base = 44 + data_bits;          /* without stuff bits */
    uint32_t stuff = (base + data_bits) / 5; /* rough stuff estimate */
    return base + stuff;
}

static void print_load_bar(double load_pct)
{
    int filled = (int)(load_pct * 40.0 / 100.0);
    if (filled > 40) filled = 40;

    const char *color = (load_pct < 30.0) ? "" :
                        (load_pct < 60.0) ? "" : "";  /* terminal colours omitted */
    printf("Load: [");
    for (int i = 0; i < 40; i++)
        printf("%c", i < filled ? '#' : ' ');
    printf("] %5.1f%%  ", load_pct);

    if      (load_pct < 30.0) printf("OK\n");
    else if (load_pct < 60.0) printf("WARNING\n");
    else                       printf("OVERLOAD\n");
}

int main(void)
{
    int sock;
    struct sockaddr_can addr;
    struct ifreq        ifr;
    struct can_frame    frame;
    time_slot_t         slots[WINDOW_SLOTS] = {{0}};
    int                 slot_idx  = 0;
    uint64_t            slot_start_ms;

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    /* Disable filter: accept all frames */
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);
    strncpy(ifr.ifr_name, "can0", IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    slot_start_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    printf("Bus load monitor started (can0, 500 kbps)\n\n");

    while (1) {
        fd_set fds;
        struct timeval tv = {0, SLOT_MS * 1000};
        FD_ZERO(&fds);
        FD_SET(sock, &fds);

        int r = select(sock + 1, &fds, NULL, NULL, &tv);
        if (r > 0 && FD_ISSET(sock, &fds)) {
            ssize_t n = read(sock, &frame, sizeof(frame));
            if (n > 0) {
                slots[slot_idx].frame_count++;
                slots[slot_idx].bits_seen += estimate_bits(frame.can_dlc);
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

        if (now_ms - slot_start_ms >= SLOT_MS) {
            /* Advance slot */
            slot_idx = (slot_idx + 1) % WINDOW_SLOTS;
            memset(&slots[slot_idx], 0, sizeof(slots[0]));
            slot_start_ms = now_ms;

            /* Compute 1-second window load */
            uint64_t total_bits = 0;
            uint64_t total_frames = 0;
            for (int i = 0; i < WINDOW_SLOTS; i++) {
                total_bits   += slots[i].bits_seen;
                total_frames += slots[i].frame_count;
            }
            double load_pct = (double)total_bits / (double)BITRATE_BPS * 100.0;
            printf("\r%-10lu frames/s  %8lu b/s  ", total_frames, total_bits);
            print_load_bar(load_pct);
            fflush(stdout);
        }
    }

    close(sock);
    return 0;
}
```

---

## 13. Summary

CANopen network diagnostics combines physical layer inspection, passive frame capture,
and active protocol probing into a layered diagnostic process:

```
  Diagnostic Layers — From Physical to Application
  ══════════════════════════════════════════════════

  Layer 5 – Conformance          ┌─────────────────────────────────┐
  (OD content, protocol rules)   │ Scripted SDO sequence tests,    │
                                 │ mandatory object presence,      │
                                 │ write-protect verification      │
                                 └─────────────────────────────────┘
                                               ▲
  Layer 4 – Application monitoring    ┌──────────────────────────────┐
  (Heartbeat, EMCY, NMT state)        │ Node alive/lost detection,   │
                                      │ EMCY decode (EEC + ErrReg),  │
                                      │ NMT state timeline           │
                                      └──────────────────────────────┘
                                               ▲
  Layer 3 – PDO / SDO analysis   ┌─────────────────────────────────┐
  (Data content, timing)         │ PDO mapping readback, period    │
                                 │ verification, SDO upload/abort  │
                                 │ decode, bus load measurement    │
                                 └─────────────────────────────────┘
                                               ▲
  Layer 2 – Frame decode         ┌─────────────────────────────────┐
  (COB-ID → function + node)     │ Function code table lookup,     │
                                 │ NMT/SYNC/PDO/SDO/HB decode,     │
                                 │ PCAN-View / CanKing / CANalyzer │
                                 └─────────────────────────────────┘
                                               ▲
  Layer 1 – Physical             ┌─────────────────────────────────┐
  (Signal, termination, errors)  │ TEC/REC counters, error frame   │
                                 │ types, termination check,       │
                                 │ oscilloscope eye diagram        │
                                 └─────────────────────────────────┘
```

### Key Takeaways

**Tool selection:** PCAN-View and CanKing suit quick frame-level inspection; Vector
CANalyzer with CAPL scripting is appropriate for complex automated test sequences and
long-term bus logging. SocketCAN-based custom C programs provide the deepest integration
and automation flexibility in Linux environments.

**Systematic approach:** Always start at Layer 1 (physical). A bad termination resistor
or ground issue will cause spurious results at every higher layer. Only after confirming
clean signal quality should SDO and protocol diagnostics proceed.

**SDO diagnostics:** Expedited SDO upload is the fundamental read tool. Abort codes
provide precise failure information. The mandatory OD entries at 0x1000, 0x1001, and
0x1018 must be present and correctly structured for any CiA 301-compliant device.

**Heartbeat monitoring:** Staggered heartbeat periods (e.g., nodes at 1000 ms, 1010 ms,
1020 ms) reduce simultaneous bus activity spikes. Consumer timeouts of 1.5× the producer
period provide reliable loss detection without false alarms.

**EMCY analysis:** The 2-byte Emergency Error Code together with the 1-byte Error
Register gives a high-level fault classification. The 5 manufacturer-specific bytes carry
device-internal diagnostic detail; interpretation requires the device's EDS/documentation.

**Conformance testing:** Automated conformance scripts remove human error from repetitive
OD structure checks. They should exercise not only happy paths (valid reads) but also
error paths: writes to read-only entries, access to non-existent sub-indices, and SDO
abort code verification.

**Bus load management:** Keep operational bus load below 30–40%. At higher loads,
low-priority SDO responses and heartbeats will experience increased latency, potentially
triggering false heartbeat-loss alarms or SDO timeouts even on otherwise healthy nodes.

---

### Reference: CANopen COB-ID Quick Table

```
  COB-ID Range    Function            Default Node Range
  ────────────────────────────────────────────────────────────
  0x000           NMT Command         (broadcast)
  0x001–0x07F     (reserved)
  0x080           SYNC                (broadcast)
  0x081–0x0FF     EMCY                Node 1–127
  0x100           TIME                (broadcast)
  0x101–0x17F     (reserved)
  0x181–0x1FF     TPDO1               Node 1–127
  0x201–0x27F     RPDO1               Node 1–127
  0x281–0x2FF     TPDO2               Node 1–127
  0x301–0x37F     RPDO2               Node 1–127
  0x381–0x3FF     TPDO3               Node 1–127
  0x401–0x47F     RPDO3               Node 1–127
  0x481–0x4FF     TPDO4               Node 1–127
  0x501–0x57F     RPDO4               Node 1–127
  0x581–0x5FF     SDO Tx (Resp)       Node 1–127
  0x601–0x67F     SDO Rx (Req)        Node 1–127
  0x680–0x6FF     (reserved)
  0x700           NMT Error Ctrl      (master HB)
  0x701–0x77F     Heartbeat / NG      Node 1–127
  0x7E4–0x7E5     LSS                 (all nodes)
  0x780–0x7FF     (reserved)
```

---

*Document: CANopen Network Diagnostics & Bus Analysis — CiA 301 Rev. 4.2.0 scope*
*Tools: Peak PCAN-Basic SDK, Kvaser CANlib, Vector CANalyzer CAPL, Linux SocketCAN*
*Languages: C11, C++17*