# SAE J2284 — CAN for Diagnostics

## Table of Contents

1. [Introduction](#introduction)
2. [Historical Context and Standards Relationship](#historical-context-and-standards-relationship)
3. [Protocol Architecture](#protocol-architecture)
4. [Physical Layer Specifications](#physical-layer-specifications)
5. [Data Link Layer and Frame Structure](#data-link-layer-and-frame-structure)
6. [Network Layer — ISO 15765-2 Transport Protocol](#network-layer--iso-15765-2-transport-protocol)
7. [Application Layer — UDS (ISO 14229)](#application-layer--uds-iso-14229)
8. [Diagnostic Message Addressing](#diagnostic-message-addressing)
9. [Session Management](#session-management)
10. [Security Access](#security-access)
11. [Data Transfer and Memory Services](#data-transfer-and-memory-services)
12. [Programming in C/C++](#programming-in-cc)
13. [Programming in Rust](#programming-in-rust)
14. [Tooling and Testing](#tooling-and-testing)
15. [Summary](#summary)

---

## Introduction

**SAE J2284** defines the requirements for a high-speed Controller Area Network (CAN) bus used for diagnostic communication in passenger cars and light trucks. It specifies the physical medium, electrical characteristics, timing, and network topology for a dedicated diagnostic CAN bus operating at **500 kbit/s**. It builds directly upon ISO 11898 (the base CAN standard) and is tightly coupled with **ISO 15765** (Diagnostic Communication over CAN — DoCAN), which provides the transport and network layer services.

Where ISO 15765 defines *how* multi-frame diagnostic messages are transported over CAN, SAE J2284 defines *which physical wire, connector, and bitrate* shall be used, and how the OBD-II (On-Board Diagnostics II) diagnostic port integrates with the vehicle's CAN network. Together, they form the backbone of modern automotive diagnostics used by OEM dealer tools, aftermarket scan tools, and regulatory emissions testing equipment.

Key characteristics:

| Property | Value |
|---|---|
| Bitrate | 500 kbit/s (mandatory for J2284-3 and later) |
| Physical connector | SAE J1962 OBD-II (pins 6 and 14 for CAN H/L) |
| Standard CAN ID length | 11-bit (CAN 2.0A), extended 29-bit also supported |
| Termination | 120 Ω at each network end |
| Transport layer | ISO 15765-2 |
| Application layer | ISO 14229 (UDS) |
| OBD services | SAE J1979 / ISO 15031 |

---

## Historical Context and Standards Relationship

The evolution of automotive diagnostics over CAN:

```
SAE J1979  (OBD-II Service Modes 01–0A)          ← Emissions / regulatory diagnostics
     |
ISO 15031  (OBD equivalent, global)               ← ISO counterpart to J1979
     |
ISO 15765-2 (Transport Protocol / Network Layer)  ← Segmentation & flow control
     |
ISO 15765-4 (CAN for OBD — physical/data link)   ← Physical layer for OBD
     |
SAE J2284   (CAN for Diagnostics)                 ← Full diagnostic bus specification
     |
ISO 14229   (UDS — Unified Diagnostic Services)   ← Extended / OEM diagnostics
     |
ISO 27145   (WWH-OBD — World Wide Harmonized OBD) ← Modern harmonized standard
```

SAE J2284 versions:

- **J2284/1** (1997) — 125 kbit/s diagnostic CAN
- **J2284/2** (1997) — 250 kbit/s
- **J2284/3** (2006) — 500 kbit/s, the current dominant version
- **J2284/4** and beyond — address CAN FD and evolving requirements

From model year 2008 onward, US vehicles are required to support ISO 15765-4 (CAN at 500 kbit/s) for OBD-II diagnostics, making J2284-3 the de facto standard.

---

## Protocol Architecture

The full diagnostic stack in layered form (OSI model mapping):

```
┌─────────────────────────────────────────────────┐
│  Layer 7 — Application                          │
│  ISO 14229 UDS / SAE J1979 OBD-II Services      │
│  Service IDs: 0x10, 0x11, 0x22, 0x27, 0x2E...  │
├─────────────────────────────────────────────────┤
│  Layer 5/6 — Session / Presentation             │
│  UDS Session Control (0x10)                     │
│  Security Access (0x27)                         │
├─────────────────────────────────────────────────┤
│  Layer 4 — Transport                            │
│  ISO 15765-2 (TP layer)                         │
│  Single Frame (SF), First Frame (FF),           │
│  Consecutive Frame (CF), Flow Control (FC)      │
├─────────────────────────────────────────────────┤
│  Layer 3 — Network                              │
│  ISO 15765-2 Network Address (N_AI)             │
│  11-bit: functional 0x7DF / physical 0x7E0-7E7  │
│  29-bit: extended addressing                    │
├─────────────────────────────────────────────────┤
│  Layer 2 — Data Link                            │
│  ISO 11898-1 CAN 2.0A/B framing                 │
│  11-bit or 29-bit arbitration IDs               │
├─────────────────────────────────────────────────┤
│  Layer 1 — Physical                             │
│  SAE J2284 / ISO 11898-2                        │
│  500 kbit/s, differential pair, 120 Ω           │
│  OBD-II J1962 connector pins 6 (CAN-H), 14 (CAN-L) │
└─────────────────────────────────────────────────┘
```

---

## Physical Layer Specifications

SAE J2284-3 mandates:

- **Bus topology:** Linear (trunk + stubs); stubs ≤ 1 m
- **Bus length:** Up to 40 m for the trunk
- **Differential voltage:** Recessive ~2.5 V on both lines; Dominant: CAN-H ~3.5 V, CAN-L ~1.5 V (differential 2 V)
- **Common mode range:** ±2 V
- **Termination:** 120 Ω at each end (split termination with capacitor also allowed)
- **Connector:** SAE J1962 OBD-II, Type A (12 V systems)
  - Pin 6: CAN High
  - Pin 14: CAN Low
  - Pin 16: Battery positive (B+)
  - Pin 4/5: Chassis/Signal Ground

---

## Data Link Layer and Frame Structure

Standard CAN 2.0A frames used for diagnostics (11-bit IDs):

```
 ┌──┬────────────┬───┬───┬────────────────────────┬───────┬─┐
 │SOF│  11-bit ID │RTR│IDE│ DLC (4 bits, max 8 bytes)│  Data │CRC│
 └──┴────────────┴───┴───┴────────────────────────┴───────┴─┘

Standard diagnostic CAN IDs (11-bit):
  0x7DF  — Functional/broadcast request (tester to all ECUs)
  0x7E0  — Physical request to ECU 1 (e.g., Engine ECU)
  0x7E8  — Physical response from ECU 1
  0x7E1  — Physical request to ECU 2 (e.g., Transmission)
  0x7E9  — Physical response from ECU 2
  ...
  0x7E7  — Physical request to ECU 8
  0x7EF  — Physical response from ECU 8
```

The response ID is always request ID + 0x8 for the standard 0x7Ex range.

---

## Network Layer — ISO 15765-2 Transport Protocol

ISO 15765-2 provides segmentation and reassembly, allowing diagnostic messages longer than 7 bytes to be transferred over CAN.

### Frame Types

#### Single Frame (SF) — messages ≤ 7 bytes

```
Byte 0:  [0][0][0][0] [LEN: 4 bits]   — PCI byte, upper nibble = 0 (SF), lower nibble = length
Bytes 1–7: Data (up to 7 bytes)
```

#### First Frame (FF) — start of multi-frame message

```
Byte 0:  [0][0][0][1] [LEN_HI: 4 bits]  — PCI, upper nibble = 1 (FF)
Byte 1:  [LEN_LO: 8 bits]               — total message length (12 bits combined)
Bytes 2–7: First 6 bytes of data
```

#### Consecutive Frame (CF) — subsequent frames

```
Byte 0:  [0][0][1][0] [SN: 4 bits]  — PCI, upper nibble = 2 (CF), lower nibble = sequence number (1..F, wraps)
Bytes 1–7: Up to 7 bytes of data
```

#### Flow Control (FC) — sent by receiver after FF

```
Byte 0:  [0][0][1][1] [FS: 4 bits]  — PCI, upper nibble = 3 (FC); FS: 0=ContinueToSend, 1=Wait, 2=Overflow
Byte 1:  BlockSize (BS)             — number of CF before next FC (0 = no limit)
Byte 2:  STmin                      — minimum separation time between CF frames
```

`STmin` encoding:
- 0x00–0x7F: 0–127 ms
- 0xF1–0xF9: 100–900 µs

---

## Application Layer — UDS (ISO 14229)

UDS defines diagnostic services identified by a **Service Identifier (SID)** byte. Common services:

| SID (hex) | Service Name | Description |
|---|---|---|
| 0x10 | DiagnosticSessionControl | Switch session (Default, Extended, Programming) |
| 0x11 | ECUReset | Hard/Soft/KeyOffOnReset |
| 0x14 | ClearDiagnosticInformation | Clear DTCs |
| 0x19 | ReadDTCInformation | Read Diagnostic Trouble Codes |
| 0x22 | ReadDataByIdentifier | Read a DID value |
| 0x23 | ReadMemoryByAddress | Read raw memory |
| 0x27 | SecurityAccess | Seed/Key authentication |
| 0x28 | CommunicationControl | Enable/Disable TX/RX |
| 0x2E | WriteDataByIdentifier | Write a DID value |
| 0x31 | RoutineControl | Start/Stop/Request routine |
| 0x34 | RequestDownload | Initiate flash download |
| 0x35 | RequestUpload | Initiate data upload |
| 0x36 | TransferData | Transfer data block |
| 0x37 | RequestTransferExit | Finalize transfer |
| 0x3E | TesterPresent | Keep session alive |
| 0x7F | NegativeResponse | Error response |

Positive response SID = request SID + 0x40 (e.g., 0x22 → 0x62).

### Negative Response Codes (NRC)

| NRC (hex) | Meaning |
|---|---|
| 0x10 | generalReject |
| 0x11 | serviceNotSupported |
| 0x12 | subFunctionNotSupported |
| 0x13 | incorrectMessageLengthOrInvalidFormat |
| 0x22 | conditionsNotCorrect |
| 0x24 | requestSequenceError |
| 0x25 | noResponseFromSubnetComponent |
| 0x31 | requestOutOfRange |
| 0x33 | securityAccessDenied |
| 0x35 | invalidKey |
| 0x36 | exceededNumberOfAttempts |
| 0x37 | requiredTimeDelayNotExpired |
| 0x78 | requestCorrectlyReceivedResponsePending |

---

## Diagnostic Message Addressing

### Functional Addressing (broadcast)

Used to address all ECUs simultaneously (e.g., for OBD-II Mode 01 requests):

```
CAN ID: 0x7DF
Data:   [0x02] [0x01] [0x00] [0xCC] [0xCC] [0xCC] [0xCC] [0xCC]
         ^SF    ^SID   ^PID   ^^^padding
```

### Physical Addressing (point-to-point)

Used for UDS services targeting a specific ECU:

```
Request:  CAN ID 0x7E0, Data: [0x02][0x10][0x03]...   (DiagnosticSessionControl → ExtendedDiagnosticSession)
Response: CAN ID 0x7E8, Data: [0x02][0x50][0x03]...   (positive response)
```

---

## Session Management

```
Default Session (0x01)      — Always active on power-up. Limited services.
Extended Diagnostic (0x03)  — Unlocks additional services (WriteDataByIdentifier, etc.)
Programming Session (0x02)  — Required for ECU reprogramming (flash write).

Transitions:
  0x7E0  → [0x02][0x10][0x03]   : Enter Extended Session
  0x7E8  ← [0x02][0x50][0x03]   : Positive response
  ...periodic 0x3E TesterPresent to stay in session...
  Timeout (typically 5 s) → ECU returns to Default Session
```

---

## Security Access

Two-step seed/key exchange:

```
Step 1 — Request seed:
  Tester → ECU:  [0x02][0x27][0x01]          (requestSeed, accessLevel 0x01)
  ECU → Tester:  [0x06][0x67][0x01][S1][S2][S3][S4]  (4-byte seed)

Step 2 — Send key:
  Tester → ECU:  [0x06][0x27][0x02][K1][K2][K3][K4]  (sendKey)
  ECU → Tester:  [0x02][0x67][0x02]           (positive response, access granted)
```

The key is derived from the seed using an algorithm known to the tester (OEM-specific, often XOR/AES/custom).

---

## Programming in C/C++

### Platform-Agnostic CAN Abstraction

```c
// can_diag.h — CAN diagnostic abstraction layer

#ifndef CAN_DIAG_H
#define CAN_DIAG_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define CAN_MAX_DLC          8
#define ISO15765_MAX_PAYLOAD 4095   /* 12-bit length field in FF */

/* ISO 15765-2 PCI frame types */
#define ISO15765_SF   0x00
#define ISO15765_FF   0x10
#define ISO15765_CF   0x20
#define ISO15765_FC   0x30

/* Flow Control Status */
#define FC_CTS        0x00   /* Continue To Send */
#define FC_WAIT       0x01
#define FC_OVERFLOW   0x02

/* Standard OBD/UDS CAN IDs */
#define CAN_ID_TESTER_BROADCAST   0x7DFU
#define CAN_ID_TESTER_REQUEST     0x7E0U   /* to ECU 1 */
#define CAN_ID_ECU_RESPONSE       0x7E8U   /* from ECU 1 */

/* UDS Service IDs */
#define UDS_SID_DIAGNOSTIC_SESSION_CONTROL   0x10U
#define UDS_SID_ECU_RESET                    0x11U
#define UDS_SID_SECURITY_ACCESS              0x27U
#define UDS_SID_TESTER_PRESENT               0x3EU
#define UDS_SID_READ_DATA_BY_IDENTIFIER      0x22U
#define UDS_SID_WRITE_DATA_BY_IDENTIFIER     0x2EU
#define UDS_SID_CLEAR_DTC                    0x14U
#define UDS_SID_READ_DTC                     0x19U
#define UDS_SID_NEGATIVE_RESPONSE            0x7FU

#define UDS_POSITIVE_RESPONSE_OFFSET         0x40U

typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[CAN_MAX_DLC];
} can_frame_t;

/* Platform HAL callbacks — implement for your hardware */
typedef int (*can_send_fn)(const can_frame_t *frame, void *ctx);
typedef int (*can_recv_fn)(can_frame_t *frame, uint32_t timeout_ms, void *ctx);

typedef struct {
    can_send_fn send;
    can_recv_fn recv;
    void       *ctx;
} can_hal_t;

/* ISO 15765-2 transport session */
typedef struct {
    can_hal_t   hal;
    uint32_t    tx_id;
    uint32_t    rx_id;
    uint8_t     rx_buf[ISO15765_MAX_PAYLOAD];
    uint16_t    rx_len;
    uint8_t     rx_sn;       /* expected consecutive frame sequence number */
    uint8_t     block_size;  /* BS from FC */
    uint8_t     stmin_ms;    /* STmin (ms resolution, simplified) */
} iso15765_t;

#endif /* CAN_DIAG_H */
```

### ISO 15765-2 Transport Layer Implementation

```c
// iso15765.c — Transport Protocol implementation

#include "can_diag.h"
#include <string.h>

/* Platform delay stub — replace with your RTOS/HAL delay */
extern void platform_delay_ms(uint32_t ms);

/* Send a Flow Control frame */
static int send_flow_control(iso15765_t *tp, uint8_t fs, uint8_t bs, uint8_t stmin)
{
    can_frame_t fc = {0};
    fc.id     = tp->tx_id;
    fc.dlc    = 3;
    fc.data[0] = ISO15765_FC | (fs & 0x0F);
    fc.data[1] = bs;
    fc.data[2] = stmin;
    /* Pad unused bytes per ISO 15765-2 */
    memset(&fc.data[3], 0xCC, 5);
    fc.dlc = 8;   /* Some ECUs require DLC=8 */
    return tp->hal.send(&fc, tp->hal.ctx);
}

/* Transmit a diagnostic payload using ISO 15765-2 */
int iso15765_send(iso15765_t *tp, const uint8_t *payload, uint16_t len)
{
    can_frame_t frame = {0};
    frame.id = tp->tx_id;

    if (len <= 7) {
        /* Single Frame */
        frame.dlc     = (uint8_t)(len + 1);
        frame.data[0] = ISO15765_SF | (uint8_t)len;
        memcpy(&frame.data[1], payload, len);
        /* Pad to 8 bytes */
        if (frame.dlc < 8) {
            memset(&frame.data[frame.dlc], 0xCC, 8 - frame.dlc);
            frame.dlc = 8;
        }
        return tp->hal.send(&frame, tp->hal.ctx);
    }

    /* Multi-frame: First Frame */
    frame.dlc     = 8;
    frame.data[0] = ISO15765_FF | (uint8_t)((len >> 8) & 0x0F);
    frame.data[1] = (uint8_t)(len & 0xFF);
    memcpy(&frame.data[2], payload, 6);
    if (tp->hal.send(&frame, tp->hal.ctx) < 0)
        return -1;

    /* Wait for Flow Control from receiver */
    can_frame_t fc_frame;
    if (tp->hal.recv(&fc_frame, 1000, tp->hal.ctx) < 0)
        return -2;   /* FC timeout */
    if ((fc_frame.data[0] & 0xF0) != ISO15765_FC)
        return -3;
    uint8_t fs    = fc_frame.data[0] & 0x0F;
    uint8_t bs    = fc_frame.data[1];
    uint8_t stmin = fc_frame.data[2];
    (void)fs;  /* Handle FC_WAIT / FC_OVERFLOW in production code */

    /* Consecutive Frames */
    uint16_t offset = 6;
    uint8_t  sn     = 1;
    uint8_t  block  = 0;

    while (offset < len) {
        uint16_t chunk = (uint16_t)(len - offset);
        if (chunk > 7) chunk = 7;

        frame.data[0] = ISO15765_CF | (sn & 0x0F);
        memcpy(&frame.data[1], payload + offset, chunk);
        if (chunk < 7)
            memset(&frame.data[1 + chunk], 0xCC, 7 - chunk);
        frame.dlc = 8;

        if (tp->hal.send(&frame, tp->hal.ctx) < 0)
            return -4;

        offset += chunk;
        sn = (sn + 1) & 0x0F;   /* Wrap at 0xF → 0x0 */
        block++;

        /* Wait for next FC if block size reached */
        if (bs != 0 && block >= bs) {
            if (tp->hal.recv(&fc_frame, 1000, tp->hal.ctx) < 0)
                return -5;
            bs    = fc_frame.data[1];
            stmin = fc_frame.data[2];
            block = 0;
        }

        /* STmin delay */
        if (stmin > 0 && stmin <= 0x7F)
            platform_delay_ms(stmin);
        else if (stmin >= 0xF1 && stmin <= 0xF9)
            platform_delay_ms(1);   /* sub-ms granularity: treat as 1 ms */
    }
    return 0;
}

/* Receive a complete diagnostic message (handles segmentation) */
int iso15765_recv(iso15765_t *tp, uint32_t timeout_ms)
{
    can_frame_t frame;
    if (tp->hal.recv(&frame, timeout_ms, tp->hal.ctx) < 0)
        return -1;
    if (frame.id != tp->rx_id)
        return -2;

    uint8_t pci_type = frame.data[0] & 0xF0;

    if (pci_type == ISO15765_SF) {
        /* Single Frame */
        tp->rx_len = frame.data[0] & 0x0F;
        if (tp->rx_len == 0 || tp->rx_len > 7) return -3;
        memcpy(tp->rx_buf, &frame.data[1], tp->rx_len);
        return (int)tp->rx_len;
    }

    if (pci_type == ISO15765_FF) {
        /* First Frame of multi-frame message */
        tp->rx_len = (uint16_t)(((frame.data[0] & 0x0F) << 8) | frame.data[1]);
        if (tp->rx_len > ISO15765_MAX_PAYLOAD) return -4;
        memcpy(tp->rx_buf, &frame.data[2], 6);
        uint16_t received = 6;
        tp->rx_sn = 1;

        /* Send Flow Control: CTS, no block limit, STmin=0 */
        send_flow_control(tp, FC_CTS, 0, 0);

        /* Receive Consecutive Frames */
        while (received < tp->rx_len) {
            if (tp->hal.recv(&frame, 200, tp->hal.ctx) < 0)
                return -5;
            if ((frame.data[0] & 0xF0) != ISO15765_CF) return -6;
            if ((frame.data[0] & 0x0F) != tp->rx_sn)  return -7;

            uint16_t remaining = tp->rx_len - received;
            uint16_t chunk     = remaining > 7 ? 7 : remaining;
            memcpy(tp->rx_buf + received, &frame.data[1], chunk);
            received  += chunk;
            tp->rx_sn  = (tp->rx_sn + 1) & 0x0F;
        }
        return (int)tp->rx_len;
    }

    return -8;   /* Unexpected frame type */
}
```

### UDS Service Layer

```c
// uds.c — Unified Diagnostic Services layer

#include "can_diag.h"
#include <string.h>

#define UDS_SESSION_DEFAULT     0x01
#define UDS_SESSION_PROGRAMMING 0x02
#define UDS_SESSION_EXTENDED    0x03

typedef struct {
    iso15765_t *tp;
    uint8_t     req_buf[ISO15765_MAX_PAYLOAD];
    uint8_t     rsp_buf[ISO15765_MAX_PAYLOAD];
    uint16_t    rsp_len;
} uds_t;

/* Generic request/response helper */
static int uds_transact(uds_t *uds, const uint8_t *req, uint16_t req_len,
                        uint32_t timeout_ms)
{
    if (iso15765_send(uds->tp, req, req_len) < 0)
        return -1;

    /* Handle 0x78 ResponsePending — ECU may send several before final */
    for (int retry = 0; retry < 10; retry++) {
        int rlen = iso15765_recv(uds->tp, timeout_ms);
        if (rlen < 0) return -2;
        uds->rsp_len = (uint16_t)rlen;
        memcpy(uds->rsp_buf, uds->tp->rx_buf, rlen);

        /* Check for 0x78 NRC — response pending */
        if (rlen >= 3 &&
            uds->rsp_buf[0] == UDS_SID_NEGATIVE_RESPONSE &&
            uds->rsp_buf[2] == 0x78) {
            continue;   /* Wait and retry */
        }
        break;
    }

    /* Validate response */
    if (uds->rsp_len < 1) return -3;

    /* Negative response check */
    if (uds->rsp_buf[0] == UDS_SID_NEGATIVE_RESPONSE) {
        return -(int)uds->rsp_buf[2];   /* Return NRC as negative error */
    }

    return (int)uds->rsp_len;
}

/* DiagnosticSessionControl (0x10) */
int uds_session_control(uds_t *uds, uint8_t session_type)
{
    uint8_t req[2] = { UDS_SID_DIAGNOSTIC_SESSION_CONTROL, session_type };
    int ret = uds_transact(uds, req, 2, 500);
    if (ret < 0) return ret;
    /* Positive response: [0x50][session_type][P2][P2*] */
    if (uds->rsp_buf[0] != (UDS_SID_DIAGNOSTIC_SESSION_CONTROL + UDS_POSITIVE_RESPONSE_OFFSET))
        return -1;
    return 0;
}

/* TesterPresent (0x3E) — keepalive */
int uds_tester_present(uds_t *uds)
{
    uint8_t req[2] = { UDS_SID_TESTER_PRESENT, 0x00 };  /* suppressPosRspMsgIndicationBit=0 */
    return uds_transact(uds, req, 2, 200);
}

/* ReadDataByIdentifier (0x22) */
int uds_read_did(uds_t *uds, uint16_t did, uint8_t *out, uint16_t *out_len)
{
    uint8_t req[3] = {
        UDS_SID_READ_DATA_BY_IDENTIFIER,
        (uint8_t)(did >> 8),
        (uint8_t)(did & 0xFF)
    };
    int ret = uds_transact(uds, req, 3, 500);
    if (ret < 0) return ret;

    /* Response: [0x62][DID_HI][DID_LO][data...] */
    if (uds->rsp_buf[0] != (UDS_SID_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_OFFSET))
        return -1;
    if (uds->rsp_len < 3) return -2;

    *out_len = uds->rsp_len - 3;
    memcpy(out, &uds->rsp_buf[3], *out_len);
    return 0;
}

/* SecurityAccess (0x27) — seed/key exchange */
int uds_security_access(uds_t *uds, uint8_t access_level,
                        uint32_t (*derive_key_fn)(uint32_t seed))
{
    /* Step 1: Request Seed */
    uint8_t req[2] = { UDS_SID_SECURITY_ACCESS, access_level };
    int ret = uds_transact(uds, req, 2, 500);
    if (ret < 0) return ret;

    if (uds->rsp_len < 6) return -1;   /* Need [0x67][lvl][s0][s1][s2][s3] */
    uint32_t seed = ((uint32_t)uds->rsp_buf[2] << 24) |
                    ((uint32_t)uds->rsp_buf[3] << 16) |
                    ((uint32_t)uds->rsp_buf[4] <<  8) |
                    ((uint32_t)uds->rsp_buf[5]);

    /* ECU already unlocked if seed == 0 */
    if (seed == 0) return 0;

    /* Step 2: Send Key */
    uint32_t key = derive_key_fn(seed);
    uint8_t key_req[6] = {
        UDS_SID_SECURITY_ACCESS,
        (uint8_t)(access_level + 1),   /* send key subfunction = request seed + 1 */
        (uint8_t)(key >> 24), (uint8_t)(key >> 16),
        (uint8_t)(key >>  8), (uint8_t)(key)
    };
    return uds_transact(uds, key_req, 6, 500);
}

/* WriteDataByIdentifier (0x2E) */
int uds_write_did(uds_t *uds, uint16_t did, const uint8_t *data, uint16_t data_len)
{
    uint8_t req[3 + data_len];
    req[0] = UDS_SID_WRITE_DATA_BY_IDENTIFIER;
    req[1] = (uint8_t)(did >> 8);
    req[2] = (uint8_t)(did & 0xFF);
    memcpy(&req[3], data, data_len);
    return uds_transact(uds, req, (uint16_t)(3 + data_len), 1000);
}

/* ClearDiagnosticInformation (0x14) — clear all DTCs */
int uds_clear_dtc(uds_t *uds)
{
    /* Group of DTC 0xFFFFFF = all DTCs */
    uint8_t req[4] = { UDS_SID_CLEAR_DTC, 0xFF, 0xFF, 0xFF };
    return uds_transact(uds, req, 4, 2000);
}
```

### Example: Full Diagnostic Session in C

```c
// example_diagnostic_session.c

#include "can_diag.h"
#include <stdio.h>

/* OEM-specific key derivation (illustrative XOR example) */
static uint32_t my_derive_key(uint32_t seed)
{
    return seed ^ 0xDEADBEEFUL;   /* Replace with actual OEM algorithm */
}

/* Linux SocketCAN HAL implementation */
#include <linux/can.h>
#include <sys/socket.h>
#include <net/if.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

typedef struct { int sock; } socketcan_ctx_t;

static int socketcan_send(const can_frame_t *f, void *ctx)
{
    socketcan_ctx_t *sc = (socketcan_ctx_t *)ctx;
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = f->id;
    frame.can_dlc = f->dlc;
    memcpy(frame.data, f->data, f->dlc);
    return (int)write(sc->sock, &frame, sizeof(frame));
}

static int socketcan_recv(can_frame_t *f, uint32_t timeout_ms, void *ctx)
{
    socketcan_ctx_t *sc = (socketcan_ctx_t *)ctx;
    struct timeval tv = { .tv_sec = timeout_ms / 1000,
                          .tv_usec = (timeout_ms % 1000) * 1000 };
    setsockopt(sc->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct can_frame frame;
    ssize_t n = read(sc->sock, &frame, sizeof(frame));
    if (n <= 0) return -1;

    f->id  = frame.can_id & CAN_EFF_MASK;
    f->dlc = frame.can_dlc;
    memcpy(f->data, frame.data, frame.can_dlc);
    return 0;
}

int main(void)
{
    /* Set up SocketCAN */
    socketcan_ctx_t sc_ctx;
    sc_ctx.sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    strcpy(ifr.ifr_name, "can0");
    ioctl(sc_ctx.sock, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex
    };
    bind(sc_ctx.sock, (struct sockaddr *)&addr, sizeof(addr));

    /* Wire up HAL */
    can_hal_t hal = {
        .send = socketcan_send,
        .recv = socketcan_recv,
        .ctx  = &sc_ctx
    };

    iso15765_t tp = {
        .hal   = hal,
        .tx_id = CAN_ID_TESTER_REQUEST,
        .rx_id = CAN_ID_ECU_RESPONSE
    };

    uds_t uds = { .tp = &tp };

    /* 1. Enter Extended Diagnostic Session */
    if (uds_session_control(&uds, UDS_SESSION_EXTENDED) < 0) {
        fprintf(stderr, "Failed to enter extended session\n");
        return 1;
    }
    printf("Entered Extended Diagnostic Session\n");

    /* 2. Security Access — Level 1 */
    if (uds_security_access(&uds, 0x01, my_derive_key) < 0) {
        fprintf(stderr, "Security access denied\n");
        return 1;
    }
    printf("Security access granted\n");

    /* 3. Read VIN (DID 0xF190) */
    uint8_t  vin[20];
    uint16_t vin_len = 0;
    if (uds_read_did(&uds, 0xF190, vin, &vin_len) == 0) {
        printf("VIN (%u bytes): %.*s\n", vin_len, vin_len, vin);
    }

    /* 4. Read ECU Software Version (DID 0xF189) */
    uint8_t  sw_ver[16];
    uint16_t sw_len = 0;
    if (uds_read_did(&uds, 0xF189, sw_ver, &sw_len) == 0) {
        printf("SW Version (%u bytes):", sw_len);
        for (int i = 0; i < sw_len; i++) printf(" %02X", sw_ver[i]);
        printf("\n");
    }

    /* 5. Clear all DTCs */
    if (uds_clear_dtc(&uds) == 0)
        printf("DTCs cleared\n");

    /* 6. Return to default session */
    uds_session_control(&uds, UDS_SESSION_DEFAULT);

    close(sc_ctx.sock);
    return 0;
}
```

---

## Programming in Rust

### Cargo.toml

```toml
[package]
name    = "can-diagnostics"
version = "0.1.0"
edition = "2021"

[dependencies]
socketcan  = "3"          # SocketCAN bindings for Linux
thiserror  = "1"          # Ergonomic error types
tokio      = { version = "1", features = ["full"] }  # async runtime (optional)
```

### Error Types and Core Structures

```rust
// src/error.rs

use thiserror::Error;

#[derive(Debug, Error)]
pub enum DiagError {
    #[error("CAN send/receive IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("ISO 15765 segmentation error: {0}")]
    Transport(String),

    #[error("UDS negative response code 0x{0:02X}")]
    NegativeResponse(u8),

    #[error("Timeout waiting for response")]
    Timeout,

    #[error("Invalid frame: {0}")]
    InvalidFrame(String),
}

pub type DiagResult<T> = Result<T, DiagError>;
```

```rust
// src/types.rs

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PciType {
    SingleFrame    = 0x00,
    FirstFrame     = 0x10,
    ConsecutiveFrame = 0x20,
    FlowControl    = 0x30,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FlowControlStatus {
    ContinueToSend = 0,
    Wait           = 1,
    Overflow       = 2,
}

/// UDS Session Types
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum DiagSession {
    Default     = 0x01,
    Programming = 0x02,
    Extended    = 0x03,
}

/// UDS Service Identifiers
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum UdsSid {
    DiagnosticSessionControl = 0x10,
    EcuReset                 = 0x11,
    ClearDtc                 = 0x14,
    ReadDtc                  = 0x19,
    ReadDataById             = 0x22,
    SecurityAccess           = 0x27,
    WriteDataById            = 0x2E,
    RoutineControl           = 0x31,
    RequestDownload          = 0x34,
    TransferData             = 0x36,
    RequestTransferExit      = 0x37,
    TesterPresent            = 0x3E,
    NegativeResponse         = 0x7F,
}

impl UdsSid {
    pub fn positive_response(self) -> u8 {
        self as u8 + 0x40
    }
}

pub const CAN_ID_BROADCAST:   u32 = 0x7DF;
pub const CAN_ID_TESTER_REQ:  u32 = 0x7E0;
pub const CAN_ID_ECU_RESP:    u32 = 0x7E8;
pub const ISO15765_MAX_PAYLOAD: usize = 4095;
```

### ISO 15765-2 Transport Layer in Rust

```rust
// src/iso15765.rs

use crate::{error::*, types::*};
use socketcan::{CanFrame, CanSocket, Socket};
use std::time::{Duration, Instant};

pub struct Iso15765 {
    socket:  CanSocket,
    tx_id:   u32,
    rx_id:   u32,
}

impl Iso15765 {
    pub fn new(iface: &str, tx_id: u32, rx_id: u32) -> DiagResult<Self> {
        let socket = CanSocket::open(iface)?;
        socket.set_read_timeout(Duration::from_millis(500))?;
        Ok(Self { socket, tx_id, rx_id })
    }

    fn send_raw(&self, data: &[u8]) -> DiagResult<()> {
        assert!(data.len() <= 8, "CAN frame data exceeds 8 bytes");
        let mut padded = [0xCCu8; 8];
        padded[..data.len()].copy_from_slice(data);
        let frame = CanFrame::new(
            socketcan::StandardId::new(self.tx_id as u16)
                .ok_or_else(|| DiagError::InvalidFrame("Invalid CAN ID".into()))?,
            &padded,
        ).map_err(|e| DiagError::Transport(e.to_string()))?;
        self.socket.write_frame(&frame)?;
        Ok(())
    }

    fn recv_raw(&self, timeout: Duration) -> DiagResult<[u8; 8]> {
        self.socket.set_read_timeout(timeout)?;
        let deadline = Instant::now() + timeout;
        loop {
            match self.socket.read_frame() {
                Ok(frame) => {
                    if frame.raw_id() == self.rx_id {
                        let data = frame.data();
                        let mut buf = [0u8; 8];
                        buf[..data.len()].copy_from_slice(data);
                        return Ok(buf);
                    }
                }
                Err(_) if Instant::now() >= deadline => return Err(DiagError::Timeout),
                Err(_) => {}
            }
        }
    }

    fn send_flow_control(&self, status: FlowControlStatus, bs: u8, stmin: u8) -> DiagResult<()> {
        let fc = [
            0x30 | (status as u8),
            bs,
            stmin,
        ];
        self.send_raw(&fc)
    }

    /// Send a payload using ISO 15765-2 segmentation
    pub fn send(&self, payload: &[u8]) -> DiagResult<()> {
        let len = payload.len();

        if len <= 7 {
            // Single Frame
            let mut frame = vec![0x00u8 | len as u8];
            frame.extend_from_slice(payload);
            return self.send_raw(&frame);
        }

        // First Frame
        let ff_byte0 = 0x10 | ((len >> 8) as u8 & 0x0F);
        let ff_byte1 = (len & 0xFF) as u8;
        let mut ff = vec![ff_byte0, ff_byte1];
        ff.extend_from_slice(&payload[..6]);
        self.send_raw(&ff)?;

        // Wait for Flow Control
        let fc = self.recv_raw(Duration::from_millis(1000))?;
        if fc[0] & 0xF0 != 0x30 {
            return Err(DiagError::Transport("Expected Flow Control frame".into()));
        }
        let _fs    = fc[0] & 0x0F;
        let bs     = fc[1];
        let stmin  = fc[2];

        // Consecutive Frames
        let mut sn    = 1u8;
        let mut offset = 6usize;
        let mut block  = 0u8;

        while offset < len {
            let chunk_end = (offset + 7).min(len);
            let chunk = &payload[offset..chunk_end];
            let mut cf = vec![0x20 | (sn & 0x0F)];
            cf.extend_from_slice(chunk);
            self.send_raw(&cf)?;

            offset += chunk.len();
            sn = sn.wrapping_add(1) & 0x0F;
            block = block.wrapping_add(1);

            if bs != 0 && block >= bs {
                let next_fc = self.recv_raw(Duration::from_millis(1000))?;
                let _ = next_fc; // process next FC in production
                block = 0;
            }

            // STmin delay
            if stmin > 0 && stmin <= 0x7F {
                std::thread::sleep(Duration::from_millis(stmin as u64));
            } else if (0xF1..=0xF9).contains(&stmin) {
                std::thread::sleep(Duration::from_micros(
                    (stmin - 0xF0) as u64 * 100
                ));
            }
        }
        Ok(())
    }

    /// Receive a complete ISO 15765-2 message
    pub fn recv(&self, timeout: Duration) -> DiagResult<Vec<u8>> {
        let frame = self.recv_raw(timeout)?;
        let pci = frame[0] & 0xF0;

        match pci {
            0x00 => {
                // Single Frame
                let len = (frame[0] & 0x0F) as usize;
                if len == 0 || len > 7 {
                    return Err(DiagError::InvalidFrame("Invalid SF length".into()));
                }
                Ok(frame[1..=len].to_vec())
            }
            0x10 => {
                // First Frame
                let total = (((frame[0] & 0x0F) as usize) << 8) | frame[1] as usize;
                let mut buf = Vec::with_capacity(total);
                buf.extend_from_slice(&frame[2..8]);

                // Send Flow Control
                self.send_flow_control(FlowControlStatus::ContinueToSend, 0, 0)?;

                let mut expected_sn = 1u8;
                while buf.len() < total {
                    let cf = self.recv_raw(Duration::from_millis(200))?;
                    if cf[0] & 0xF0 != 0x20 {
                        return Err(DiagError::Transport("Expected Consecutive Frame".into()));
                    }
                    if cf[0] & 0x0F != expected_sn {
                        return Err(DiagError::Transport(
                            format!("Sequence error: expected {}, got {}", expected_sn, cf[0] & 0x0F)
                        ));
                    }
                    let remaining = total - buf.len();
                    let take = remaining.min(7);
                    buf.extend_from_slice(&cf[1..=take]);
                    expected_sn = expected_sn.wrapping_add(1) & 0x0F;
                }
                Ok(buf)
            }
            _ => Err(DiagError::InvalidFrame(format!("Unexpected PCI type 0x{:02X}", pci))),
        }
    }
}
```

### UDS Client in Rust

```rust
// src/uds.rs

use crate::{error::*, iso15765::Iso15765, types::*};
use std::time::Duration;

pub struct UdsClient {
    tp: Iso15765,
}

impl UdsClient {
    pub fn new(tp: Iso15765) -> Self {
        Self { tp }
    }

    /// Generic request/response with 0x78 (ResponsePending) handling
    fn transact(&self, request: &[u8], timeout: Duration) -> DiagResult<Vec<u8>> {
        self.tp.send(request)?;

        for _ in 0..10 {
            let response = self.tp.recv(timeout)?;
            if response.is_empty() {
                return Err(DiagError::InvalidFrame("Empty response".into()));
            }

            // Handle 0x78 NRC (response pending)
            if response.len() >= 3
                && response[0] == UdsSid::NegativeResponse as u8
                && response[2] == 0x78
            {
                continue; // ECU is still processing — retry
            }

            // Negative response
            if response[0] == UdsSid::NegativeResponse as u8 {
                return Err(DiagError::NegativeResponse(
                    *response.get(2).unwrap_or(&0xFF)
                ));
            }

            return Ok(response);
        }
        Err(DiagError::Timeout)
    }

    /// DiagnosticSessionControl (0x10)
    pub fn session_control(&self, session: DiagSession) -> DiagResult<()> {
        let req = [UdsSid::DiagnosticSessionControl as u8, session as u8];
        let rsp = self.transact(&req, Duration::from_millis(500))?;
        let expected_sid = UdsSid::DiagnosticSessionControl.positive_response();
        if rsp[0] != expected_sid {
            return Err(DiagError::InvalidFrame("Unexpected session control response".into()));
        }
        Ok(())
    }

    /// TesterPresent (0x3E) — keepalive
    pub fn tester_present(&self) -> DiagResult<()> {
        let req = [UdsSid::TesterPresent as u8, 0x00];
        self.transact(&req, Duration::from_millis(200))?;
        Ok(())
    }

    /// ReadDataByIdentifier (0x22)
    pub fn read_did(&self, did: u16) -> DiagResult<Vec<u8>> {
        let req = [
            UdsSid::ReadDataById as u8,
            (did >> 8) as u8,
            (did & 0xFF) as u8,
        ];
        let rsp = self.transact(&req, Duration::from_millis(500))?;
        let expected = UdsSid::ReadDataById.positive_response();
        if rsp.len() < 3 || rsp[0] != expected {
            return Err(DiagError::InvalidFrame("Invalid ReadDID response".into()));
        }
        Ok(rsp[3..].to_vec())
    }

    /// WriteDataByIdentifier (0x2E)
    pub fn write_did(&self, did: u16, data: &[u8]) -> DiagResult<()> {
        let mut req = vec![
            UdsSid::WriteDataById as u8,
            (did >> 8) as u8,
            (did & 0xFF) as u8,
        ];
        req.extend_from_slice(data);
        let rsp = self.transact(&req, Duration::from_millis(1000))?;
        let expected = UdsSid::WriteDataById.positive_response();
        if rsp[0] != expected {
            return Err(DiagError::InvalidFrame("WriteDID failed".into()));
        }
        Ok(())
    }

    /// SecurityAccess (0x27) — seed/key
    pub fn security_access<F>(&self, level: u8, derive_key: F) -> DiagResult<()>
    where
        F: Fn(u32) -> u32,
    {
        // Request seed
        let req = [UdsSid::SecurityAccess as u8, level];
        let rsp = self.transact(&req, Duration::from_millis(500))?;

        let expected = UdsSid::SecurityAccess.positive_response();
        if rsp.len() < 6 || rsp[0] != expected {
            return Err(DiagError::InvalidFrame("SecurityAccess seed response invalid".into()));
        }

        let seed = u32::from_be_bytes([rsp[2], rsp[3], rsp[4], rsp[5]]);
        if seed == 0 {
            return Ok(()); // Already unlocked
        }

        let key = derive_key(seed);
        let key_bytes = key.to_be_bytes();
        let key_req = [
            UdsSid::SecurityAccess as u8,
            level + 1,
            key_bytes[0], key_bytes[1], key_bytes[2], key_bytes[3],
        ];
        let rsp2 = self.transact(&key_req, Duration::from_millis(500))?;
        if rsp2[0] != expected {
            return Err(DiagError::NegativeResponse(0x35)); // invalidKey
        }
        Ok(())
    }

    /// ClearDiagnosticInformation (0x14) — clear all DTCs
    pub fn clear_dtc(&self) -> DiagResult<()> {
        let req = [UdsSid::ClearDtc as u8, 0xFF, 0xFF, 0xFF];
        self.transact(&req, Duration::from_millis(2000))?;
        Ok(())
    }

    /// ECUReset (0x11)
    pub fn ecu_reset(&self, reset_type: u8) -> DiagResult<()> {
        let req = [UdsSid::EcuReset as u8, reset_type];
        self.transact(&req, Duration::from_millis(500))?;
        Ok(())
    }
}
```

### Main Application in Rust

```rust
// src/main.rs

mod error;
mod iso15765;
mod types;
mod uds;

use error::DiagResult;
use iso15765::Iso15765;
use types::*;
use uds::UdsClient;
use std::time::Duration;

/// Example OEM key derivation (illustrative only)
fn derive_key(seed: u32) -> u32 {
    seed ^ 0xDEAD_BEEF
}

fn main() -> DiagResult<()> {
    // Set up transport layer on can0
    let tp = Iso15765::new("can0", CAN_ID_TESTER_REQ, CAN_ID_ECU_RESP)?;
    let client = UdsClient::new(tp);

    // 1. Enter Extended Diagnostic Session
    client.session_control(DiagSession::Extended)?;
    println!("Entered Extended Diagnostic Session");

    // 2. Security Access Level 1
    client.security_access(0x01, derive_key)?;
    println!("Security access granted");

    // 3. Read VIN (DID 0xF190)
    match client.read_did(0xF190) {
        Ok(data) => {
            if let Ok(vin) = std::str::from_utf8(&data) {
                println!("VIN: {}", vin);
            } else {
                println!("VIN (hex): {:02X?}", data);
            }
        }
        Err(e) => eprintln!("Failed to read VIN: {}", e),
    }

    // 4. Read Software Version (DID 0xF189)
    match client.read_did(0xF189) {
        Ok(data) => println!("SW Version: {:02X?}", data),
        Err(e)   => eprintln!("Failed to read SW version: {}", e),
    }

    // 5. Send periodic TesterPresent to keep session alive
    // (in a real application this would run on a timer thread)
    client.tester_present()?;
    println!("TesterPresent sent");

    // 6. Clear DTCs
    client.clear_dtc()?;
    println!("DTCs cleared");

    // 7. Return to Default Session
    client.session_control(DiagSession::Default)?;
    println!("Returned to Default Session");

    Ok(())
}
```

### Async Rust with Tokio (TesterPresent keepalive)

```rust
// src/keepalive.rs — Async TesterPresent keepalive task

use tokio::time::{interval, Duration};

/// Spawns a background task that sends TesterPresent every `period`
/// to prevent the ECU from timing out the diagnostic session.
pub fn spawn_keepalive(
    client: std::sync::Arc<uds::UdsClient>,
    period: Duration,
) -> tokio::task::JoinHandle<()> {
    tokio::spawn(async move {
        let mut ticker = interval(period);
        loop {
            ticker.tick().await;
            if let Err(e) = client.tester_present() {
                eprintln!("TesterPresent failed: {}", e);
                break;
            }
        }
    })
}
```

---

## Tooling and Testing

### Linux SocketCAN — Useful Commands

```bash
# Bring up virtual CAN interface (for testing without hardware)
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Bring up real CAN hardware at 500 kbit/s
sudo ip link set can0 type can bitrate 500000
sudo ip link set up can0

# Monitor all CAN traffic
candump can0

# Send a single frame (DiagnosticSessionControl → Extended)
cansend can0 7E0#0210030000000000

# Filter by CAN ID range
candump can0,7E0:7EF~8

# Log to file and replay
candump -l can0 > diag_session.log
canplayer -I diag_session.log

# Decode with Python-can
pip install python-can
python3 -c "
import can
bus = can.interface.Bus(channel='vcan0', bustype='socketcan')
msg = can.Message(arbitration_id=0x7E0, data=[0x02,0x10,0x03,0xCC,0xCC,0xCC,0xCC,0xCC], is_extended_id=False)
bus.send(msg)
"
```

### Python UDS Diagnostic Script (Quick Testing)

```python
# quick_diag.py — Python UDS tester using python-udsoncan

import udsoncan
from udsoncan.connections import PythonIsoTpConnection
from udsoncan.client import Client
import isotp

# Set up ISO-TP socket over SocketCAN
conn = PythonIsoTpConnection(
    isotp.socket(interface='can0',
                 address=isotp.Address(isotp.AddressingMode.Normal_11bits,
                                       txid=0x7E0, rxid=0x7E8))
)
config = udsoncan.configs.default_client_config.copy()
config['exception_on_negative_response'] = False

with Client(conn, config=config) as client:
    # Enter extended session
    response = client.change_session(udsoncan.services.DiagnosticSessionControl.Session.extendedDiagnosticSession)
    print("Session:", response)

    # Read VIN
    response = client.read_data_by_identifier([0xF190])
    print("VIN:", response.service_data.values[0xF190])
```

---

## Summary

SAE J2284 defines the physical CAN bus specification for automotive diagnostics, mandating 500 kbit/s operation over the SAE J1962 OBD-II connector. It works in concert with **ISO 15765-2** (the transport layer providing segmentation and flow control for messages up to 4095 bytes) and **ISO 14229 UDS** (the application layer defining all diagnostic services).

Key takeaways:

**Protocol Stack.** The diagnostic stack is cleanly layered: J2284 defines the wire; ISO 15765-2 handles transport segmentation (Single Frame, First Frame, Consecutive Frame, Flow Control); UDS provides the service vocabulary (session control, security access, read/write DID, DTC management, ECU reprogramming).

**Addressing.** The 11-bit CAN ID range 0x7E0–0x7E7 is reserved for physical tester-to-ECU requests, with responses at 0x7E8–0x7EF. The broadcast ID 0x7DF is used for functional (OBD-II) requests to all ECUs.

**Session and Security Model.** Diagnostic sessions gate which services are available. Extended and Programming sessions require SecurityAccess (seed/key exchange) to prevent unauthorized ECU manipulation. The session must be kept alive by periodic TesterPresent messages (typically every 2–3 seconds).

**C/C++ Implementation.** A typical embedded implementation requires a thin HAL abstraction for CAN send/receive, an ISO 15765-2 segmentation engine, and a UDS service dispatcher. The code is deterministic and suitable for both tester-side (PC/Linux) and ECU-side (embedded RTOS) implementations.

**Rust Implementation.** Rust's ownership model and strong typing align well with diagnostic protocol implementation. Error propagation via `Result` and `thiserror` makes error handling explicit; the `socketcan` crate provides zero-copy CAN frame I/O on Linux. Async Rust (Tokio) is well-suited for concurrent keepalive timers alongside diagnostic operations.

**Tooling.** The Linux SocketCAN ecosystem (`candump`, `cansend`, `canplayer`, `python-udsoncan`, `python-isotp`) provides a complete software testbench, enabling full diagnostic session development and validation without physical hardware using virtual CAN (`vcan0`).

SAE J2284 together with ISO 15765 and ISO 14229 forms the foundation of all modern OEM and aftermarket automotive diagnostic tools, ECU calibration systems, and regulatory emissions test equipment worldwide.