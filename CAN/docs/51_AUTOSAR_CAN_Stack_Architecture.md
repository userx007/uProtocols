# 51. AUTOSAR CAN Stack Architecture

> Understanding the layered AUTOSAR communication stack: CAN driver, CAN interface, PDU router, and COM module.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [AUTOSAR Architecture Overview](#2-autosar-architecture-overview)
3. [Layer 1 — CAN Driver (CanDrv)](#3-layer-1--can-driver-candrv)
4. [Layer 2 — CAN Interface (CanIf)](#4-layer-2--can-interface-canif)
5. [Layer 3 — PDU Router (PduR)](#5-layer-3--pdu-router-pdur)
6. [Layer 4 — COM Module](#6-layer-4--com-module)
7. [Inter-Layer Data Flow](#7-inter-layer-data-flow)
8. [C/C++ Implementation Examples](#8-cc-implementation-examples)
9. [Rust Implementation Examples](#9-rust-implementation-examples)
10. [Configuration and ARXML](#10-configuration-and-arxml)
11. [Error Handling Across Layers](#11-error-handling-across-layers)
12. [Summary](#12-summary)

---

## 1. Introduction

AUTOSAR (AUTomotive Open System ARchitecture) defines a standardized software architecture for automotive ECUs (Electronic Control Units). Within this architecture, the **communication stack** for CAN (Controller Area Network) is organized as a strict vertical hierarchy of software layers, each with a precisely defined interface, responsibility, and API.

The AUTOSAR CAN stack is located in the **Basic Software (BSW)** layer of the AUTOSAR architecture, below the **RTE (Runtime Environment)** and above the physical CAN hardware. The stack's primary purpose is to completely decouple application-level software components (SWCs) from hardware details, enabling portability, reusability, and supplier independence.

### Key Design Principles

**Layered abstraction** is the foundation: each layer knows only its immediate neighbor above and below. The CAN Driver knows about registers; the CAN Interface knows about hardware objects; the PDU Router knows about routing tables; the COM module knows about signals. No layer skips another.

**Standardized interfaces** ensure that any AUTOSAR-compliant module from any supplier can replace another at the same layer without changing the layers above or below — this is the essence of AUTOSAR's supplier independence guarantee.

**Static configuration** is done at compile-time via ARXML (AUTOSAR XML) tooling (e.g., Vector DaVinci, EB Tresos). The runtime behavior is determined entirely by generated C code and configuration tables.

---

## 2. AUTOSAR Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│              Application Layer (SWCs)                       │
├─────────────────────────────────────────────────────────────┤
│              RTE (Runtime Environment)                      │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────┐   │
│  │           COM Module (Signal Layer)                  │   │
│  │  Signal packing, signal filtering, IPDU composition  │   │
│  └─────────────────────┬────────────────────────────────┘   │
│                        │ I-PDU (Interaction Layer PDU)      │
│  ┌─────────────────────▼────────────────────────────────┐   │
│  │           PDU Router (PduR)                          │   │
│  │  Routing table, gateway, 1:N fan-out, multiplexing   │   │
│  └──────────┬────────────────────────┬──────────────────┘   │
│             │ N-PDU                  │ N-PDU                │
│  ┌──────────▼──────────┐  ┌─────────▼───────────────────┐   │
│  │  CAN Interface      │  │  LIN / FlexRay / Ethernet   │   │
│  │  (CanIf)            │  │  Interface (other buses)    │   │
│  └──────────┬──────────┘  └─────────────────────────────┘   │
│             │ CAN L-PDU                                     │
│  ┌──────────▼──────────┐                                    │
│  │  CAN Driver (CanDrv)│                                    │
│  │  HW mailbox mgmt    │                                    │
│  └──────────┬──────────┘                                    │
├─────────────┼───────────────────────────────────────────────┤
│             │              Microcontroller Hardware         │
│  ┌──────────▼──────────┐                                    │
│  │  CAN Controller     │  (MCP2515, SJA1000, TC397 CAN, ..) │
│  └─────────────────────┘                                    │
└─────────────────────────────────────────────────────────────┘
```

### PDU Types at Each Layer Boundary

| Boundary | PDU Name | Contents |
|---|---|---|
| COM ↔ PduR | I-PDU | Assembled signal bytes, up to 8/64 bytes |
| PduR ↔ CanIf | N-PDU | Routing metadata + data buffer pointer |
| CanIf ↔ CanDrv | L-PDU | CAN ID + DLC + raw data bytes |
| CanDrv ↔ HW | Frame | Physical CAN frame bits |

A **PDU (Protocol Data Unit)** in AUTOSAR is a structure with a length, a data pointer, and in some layers additional metadata (CAN ID, hardware object handle).

---

## 3. Layer 1 — CAN Driver (CanDrv)

### Responsibility

The CAN Driver is the **lowest software layer**, sitting directly on the CAN controller hardware. It is microcontroller and silicon-specific — one of the few non-portable components in the stack. Its job is to:

- Initialize CAN controller registers (baud rate, mode, filters).
- Manage hardware **message objects** (mailboxes or FIFOs).
- Transmit L-PDUs by writing to hardware mailboxes.
- Receive L-PDUs via interrupt or polling and pass them up to CanIf.
- Handle bus-off and error passive states.

### Key AUTOSAR APIs (AUTOSAR 4.x Can.h)

```
Can_Init()             – Initialize the CAN controller
Can_SetControllerMode()– SLEEP / STOPPED / STARTED
Can_Write()            – Request transmission of an L-PDU
Can_MainFunction_Write()– Polling mode TX confirmation
Can_MainFunction_Read() – Polling mode RX processing
CanIf_TxConfirmation() – Callback: TX complete (CanIf provides this)
CanIf_RxIndication()   – Callback: Frame received (CanIf provides this)
```

The driver does **not** decide what to transmit or how to interpret received data — it only manages the hardware.

---

## 4. Layer 2 — CAN Interface (CanIf)

### Responsibility

The CAN Interface provides a **hardware-independent** abstraction above the driver. It:

- Manages **PDU handles** (abstract IDs mapping to CAN IDs and mailboxes).
- Implements a **transmit buffer** (software queue) for pending frames.
- Provides mode control (wake-up, sleep, active).
- Notifies the PDU Router (via callbacks) of received frames.
- Handles **basic software filtering** of received CAN IDs.

CanIf is the last layer that knows the concept of a "CAN frame". Above it, everything is just bytes + a PDU ID.

### Key AUTOSAR APIs (CanIf.h)

```
CanIf_Init()               – Initialize CanIf
CanIf_Transmit()           – Request TX of an N-PDU
CanIf_SetPduMode()         – Online / Offline / Tx-Offline
CanIf_GetTxConfirmationState() – Query pending TX
PduR_CanIfRxIndication()   – Callback up to PduR (PduR provides)
PduR_CanIfTxConfirmation() – Callback up to PduR (PduR provides)
```

---

## 5. Layer 3 — PDU Router (PduR)

### Responsibility

The PDU Router is the **central routing hub** of the AUTOSAR communication stack. It decouples transport protocol modules (TP) and interface modules (CanIf, LinIf, EthIf) from upper-layer modules (COM, DCM, NM). Its primary capabilities are:

- **Routing**: Forward received N-PDUs from CanIf to COM based on a compile-time routing table.
- **Gateway**: Route a PDU received on CAN directly back out to another CAN channel or a different bus (LIN, FlexRay) without COM involvement.
- **Fan-out (1:N)**: Deliver one received PDU to multiple upper modules simultaneously.
- **TP segmentation**: Buffer and forward multi-frame transport protocol messages (ISO 15765).

The PDU Router has **no knowledge of signal content** — it operates purely on PDU IDs and byte buffers.

### Key AUTOSAR APIs (PduR.h)

```
PduR_Init()                 – Initialize routing tables
PduR_CanIfRxIndication()    – Called by CanIf on frame reception
PduR_CanIfTxConfirmation()  – Called by CanIf after TX
PduR_ComTransmit()          – Called by COM to request TX
PduR_DCMTransmit()          – Called by DCM (diagnostics) to request TX
```

---

## 6. Layer 4 — COM Module

### Responsibility

The COM (Communication) module is the highest layer in the BSW communication stack, operating at the **signal level**. It:

- **Packs signals** from application SWCs into I-PDU byte arrays (handles endianness, bit positions, data types).
- **Unpacks signals** from received I-PDUs and stores them in signal buffers.
- Implements **signal filtering** (never, always, masked-new-differs-old, etc.) to control when PDUs are actually sent.
- Manages **transmission modes**: periodic (cyclic), event-driven, or mixed.
- Implements **deadline monitoring** for received PDUs (timeout detection).
- Provides the **RTE interface** so application SWCs read/write signals as typed values without knowing about CAN.

### Key AUTOSAR APIs (Com.h)

```
Com_Init()               – Initialize COM module
Com_SendSignal()         – Application writes a signal value
Com_ReceiveSignal()      – Application reads a signal value
Com_SendIPdu()           – Force immediate transmission of an I-PDU
Com_TriggerIPDUSend()    – Trigger event-driven PDU sending
Com_MainFunctionTx()     – Cyclic TX processing (call every task cycle)
Com_MainFunctionRx()     – Cyclic RX deadline monitoring
```

---

## 7. Inter-Layer Data Flow

### Transmit Path (Application → CAN Bus)

```
Application SWC
  │  Com_SendSignal(signalId, &value)
  ▼
COM Module
  │  Signal bit-packing into I-PDU buffer
  │  Apply filter (if filter passes → trigger)
  │  PduR_ComTransmit(pduId, &PduInfo)
  ▼
PDU Router
  │  Lookup routing table → destination = CanIf
  │  CanIf_Transmit(txPduId, &PduInfo)
  ▼
CAN Interface
  │  Lookup CAN ID, hardware object for txPduId
  │  Can_Write(hwObjHandle, &canPdu)
  ▼
CAN Driver
  │  Write data to mailbox registers
  │  Start CAN frame transmission
  ▼
CAN Controller Hardware → CAN Bus
```

### Receive Path (CAN Bus → Application)

```
CAN Bus → CAN Controller Hardware
  ▼
CAN Driver ISR/polling
  │  Read frame from mailbox
  │  CanIf_RxIndication(mailboxHandle, &canPdu)
  ▼
CAN Interface
  │  Software ID filter check
  │  Map CAN ID → RX PDU handle
  │  PduR_CanIfRxIndication(rxPduId, &PduInfo)
  ▼
PDU Router
  │  Routing table lookup → destination = COM
  │  Com_RxIndication(comPduId, &PduInfo)
  ▼
COM Module
  │  Unpack signals from PDU bytes (endianness, bit extract)
  │  Store in signal shadow buffer
  │  Reset deadline monitoring timer
  ▼
Application SWC
  Com_ReceiveSignal(signalId, &value)
```

---

## 8. C/C++ Implementation Examples

### 8.1 CAN Driver — Hardware Mailbox Initialization and Transmission

```c
/*
 * CanDrv.h — Minimal AUTOSAR-style CAN Driver interface
 * Targets a generic 32-bit MCU with memory-mapped CAN registers
 */

#ifndef CAN_DRV_H
#define CAN_DRV_H

#include <stdint.h>
#include <stdbool.h>

/* AUTOSAR standard return type */
typedef uint8_t  Std_ReturnType;
#define E_OK        ((Std_ReturnType)0x00U)
#define E_NOT_OK    ((Std_ReturnType)0x01U)

/* CAN controller operating modes */
typedef enum {
    CAN_CS_UNINIT   = 0x00,
    CAN_CS_STARTED  = 0x01,
    CAN_CS_STOPPED  = 0x02,
    CAN_CS_SLEEP    = 0x03
} Can_ControllerStateType;

/* An L-PDU as handed to Can_Write() */
typedef struct {
    uint32_t id;          /* CAN ID (11-bit or 29-bit) */
    uint8_t  dlc;         /* Data Length Code 0..8 (or 0..64 for CAN-FD) */
    uint8_t  sdu[8];      /* Payload bytes */
    bool     isExtended;  /* True = 29-bit ID */
} Can_PduType;

/* Hardware object (mailbox) handle — index into HW mailbox array */
typedef uint8_t Can_HwHandleType;

/* CAN controller register layout (memory-mapped, MCU-specific) */
typedef volatile struct {
    uint32_t MCR;         /* Master Control Register */
    uint32_t MSR;         /* Master Status Register  */
    uint32_t TSR;         /* Transmit Status Register */
    uint32_t RF0R;        /* Receive FIFO 0 Register  */
    uint32_t IER;         /* Interrupt Enable Register */
    uint32_t BTR;         /* Bit Timing Register       */
    uint32_t reserved[88];
    struct {
        uint32_t TIR;     /* TX Mailbox Identifier Register */
        uint32_t TDTR;    /* TX Data Length + Timestamp     */
        uint32_t TDLR;    /* TX Data Low Register (bytes 0-3) */
        uint32_t TDHR;    /* TX Data High Register (bytes 4-7) */
    } TxMailbox[3];
    struct {
        uint32_t RIR;     /* RX FIFO Identifier Register */
        uint32_t RDTR;    /* RX Data Length Register     */
        uint32_t RDLR;    /* RX Data Low                 */
        uint32_t RDHR;    /* RX Data High                */
    } FIFOMailBox[2];
} CAN_TypeDef;

/* Base address from linker/MCU header — replace with your MCU's actual address */
#define CAN1_BASE   0x40006400UL
#define CAN1        ((CAN_TypeDef *)CAN1_BASE)

/* Bit definitions (STM32-style as example) */
#define CAN_MCR_INRQ    (1U << 0)   /* Initialization Request */
#define CAN_MCR_SLEEP   (1U << 1)   /* Sleep Mode Request     */
#define CAN_MSR_INAK    (1U << 0)   /* Initialization Acknowledge */
#define CAN_TSR_TME0    (1U << 26)  /* Transmit Mailbox 0 Empty */
#define CAN_TIR_TXRQ    (1U << 0)   /* Transmit Mailbox Request */
#define CAN_TIR_IDE     (1U << 2)   /* Identifier Extension (extended) */
#define CAN_TIR_RTR     (1U << 1)   /* Remote Transmission Request */

/* Public API */
void             Can_Init(void);
Std_ReturnType   Can_SetControllerMode(Can_ControllerStateType mode);
Std_ReturnType   Can_Write(Can_HwHandleType hth, const Can_PduType *pduInfo);
void             Can_MainFunction_Write(void);
void             Can_MainFunction_Read(void);

#endif /* CAN_DRV_H */
```

```c
/*
 * CanDrv.c — CAN Driver implementation
 */

#include "CanDrv.h"
#include "CanIf_Cbk.h"   /* CanIf_RxIndication, CanIf_TxConfirmation */
#include <string.h>

/* Simple spin-wait with iteration limit — replace with hardware timer in production */
static bool wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t expected,
                      uint32_t timeout_iterations) {
    while (timeout_iterations--) {
        if ((*reg & mask) == expected) return true;
    }
    return false;
}

void Can_Init(void) {
    /* Step 1: Request initialization mode (set INRQ, clear SLEEP) */
    CAN1->MCR |=  CAN_MCR_INRQ;
    CAN1->MCR &= ~CAN_MCR_SLEEP;

    /* Wait for hardware to acknowledge init mode */
    if (!wait_flag(&CAN1->MSR, CAN_MSR_INAK, CAN_MSR_INAK, 10000U)) {
        /* In production: call Det_ReportError() — AUTOSAR DET module */
        return;
    }

    /*
     * Step 2: Configure bit timing register for 500 kBit/s on 48 MHz APB1 clock.
     * BTR[9:0]  = BRP  (Baud Rate Prescaler - 1) = 5  → Tq = 2*(5+1)/48MHz = 250ns
     * BTR[19:16]= TS1  (Time Segment 1 - 1)      = 12 → 13 Tq
     * BTR[22:20]= TS2  (Time Segment 2 - 1)      = 1  → 2 Tq
     * Bit time = 1 + 13 + 2 = 16 Tq → 16 × 250ns = 4µs = 250kBit/s (adjust as needed)
     */
    CAN1->BTR = (0U << 24) |   /* SJW = 1 Tq */
                (1U << 20) |   /* TS2 = 2 Tq */
                (12U << 16) |  /* TS1 = 13 Tq */
                (5U << 0);     /* BRP: prescaler */

    /* Step 3: Leave initialization mode */
    CAN1->MCR &= ~CAN_MCR_INRQ;
    if (!wait_flag(&CAN1->MSR, CAN_MSR_INAK, 0U, 10000U)) {
        return; /* Hardware did not leave init mode */
    }

    /* Step 4: Enable RX FIFO 0 message pending interrupt */
    CAN1->IER |= (1U << 1); /* FMPIE0 */
}

Std_ReturnType Can_SetControllerMode(Can_ControllerStateType mode) {
    switch (mode) {
        case CAN_CS_STARTED:
            CAN1->MCR &= ~CAN_MCR_INRQ;
            CAN1->MCR &= ~CAN_MCR_SLEEP;
            break;
        case CAN_CS_STOPPED:
            CAN1->MCR |=  CAN_MCR_INRQ;
            break;
        case CAN_CS_SLEEP:
            CAN1->MCR |=  CAN_MCR_SLEEP;
            break;
        default:
            return E_NOT_OK;
    }
    return E_OK;
}

Std_ReturnType Can_Write(Can_HwHandleType hth, const Can_PduType *pduInfo) {
    /* hth selects TX mailbox 0, 1, or 2.
     * In a real driver this maps via a HW-object configuration table.
     * Here we use hth directly as mailbox index for brevity. */
    if (hth > 2U || pduInfo == NULL || pduInfo->dlc > 8U) {
        return E_NOT_OK;
    }

    /* Check mailbox is empty (TME bit) */
    uint32_t tme_mask = CAN_TSR_TME0 << hth;
    if (!(CAN1->TSR & tme_mask)) {
        return E_NOT_OK; /* Mailbox busy — CanIf will queue and retry */
    }

    /* Clear the mailbox transmit request first */
    CAN1->TxMailbox[hth].TIR = 0U;

    /* Load CAN ID */
    if (pduInfo->isExtended) {
        /* 29-bit extended ID goes in TIR[31:3], IDE=1 */
        CAN1->TxMailbox[hth].TIR = (pduInfo->id << 3U) | CAN_TIR_IDE;
    } else {
        /* 11-bit standard ID goes in TIR[31:21] */
        CAN1->TxMailbox[hth].TIR = (pduInfo->id << 21U);
    }

    /* Load DLC (lower 4 bits of TDTR) */
    CAN1->TxMailbox[hth].TDTR = pduInfo->dlc & 0x0FU;

    /* Load data bytes — low register = bytes 0..3, high = bytes 4..7 */
    uint32_t data_low = 0U, data_high = 0U;
    for (uint8_t i = 0; i < pduInfo->dlc && i < 4U; i++) {
        data_low |= ((uint32_t)pduInfo->sdu[i] << (i * 8U));
    }
    for (uint8_t i = 4U; i < pduInfo->dlc && i < 8U; i++) {
        data_high |= ((uint32_t)pduInfo->sdu[i] << ((i - 4U) * 8U));
    }
    CAN1->TxMailbox[hth].TDLR = data_low;
    CAN1->TxMailbox[hth].TDHR = data_high;

    /* Trigger transmission by setting TXRQ bit */
    CAN1->TxMailbox[hth].TIR |= CAN_TIR_TXRQ;

    return E_OK;
}

/* Called periodically in polling mode — check TX completion */
void Can_MainFunction_Write(void) {
    for (uint8_t mb = 0; mb < 3U; mb++) {
        uint32_t tme_mask = CAN_TSR_TME0 << mb;
        /* If mailbox just became empty, a TX completed */
        if (CAN1->TSR & tme_mask) {
            /* Notify CanIf (it will notify PduR, which will notify COM) */
            CanIf_TxConfirmation((Can_HwHandleType)mb);
        }
    }
}

/* Called periodically — poll RX FIFO 0 */
void Can_MainFunction_Read(void) {
    while (CAN1->RF0R & 0x03U) { /* FMP0: messages pending */
        Can_PduType rxPdu;
        uint32_t rir  = CAN1->FIFOMailBox[0].RIR;
        uint32_t rdtr = CAN1->FIFOMailBox[0].RDTR;
        uint32_t rdlr = CAN1->FIFOMailBox[0].RDLR;
        uint32_t rdhr = CAN1->FIFOMailBox[0].RDHR;

        rxPdu.isExtended = (rir & CAN_TIR_IDE) ? true : false;
        rxPdu.id  = rxPdu.isExtended ? ((rir >> 3U) & 0x1FFFFFFFU)
                                     : ((rir >> 21U) & 0x7FFU);
        rxPdu.dlc = (uint8_t)(rdtr & 0x0FU);

        /* Unpack data bytes */
        for (uint8_t i = 0; i < rxPdu.dlc && i < 4U; i++) {
            rxPdu.sdu[i] = (uint8_t)(rdlr >> (i * 8U));
        }
        for (uint8_t i = 4U; i < rxPdu.dlc && i < 8U; i++) {
            rxPdu.sdu[i] = (uint8_t)(rdhr >> ((i - 4U) * 8U));
        }

        /* Release FIFO slot BEFORE calling CanIf (re-entrancy safety) */
        CAN1->RF0R |= (1U << 5U); /* RFOM0: Release FIFO 0 Output Mailbox */

        /* Forward to CAN Interface layer */
        CanIf_RxIndication(0U /* mailbox handle */, &rxPdu);
    }
}
```

---

### 8.2 CAN Interface — PDU Handle Mapping and TX Queuing

```c
/*
 * CanIf.h — CAN Interface module
 */

#ifndef CAN_IF_H
#define CAN_IF_H

#include "CanDrv.h"

/* PDU info type used at CanIf and above */
typedef struct {
    uint8_t  *SduDataPtr; /* Pointer to payload buffer */
    uint16_t  SduLength;  /* Number of valid bytes     */
} PduInfoType;

typedef uint16_t PduIdType;  /* Routing handle */

/* Configuration entry: maps a TX PDU handle to a CAN ID + mailbox */
typedef struct {
    PduIdType        txPduId;
    uint32_t         canId;
    bool             isExtended;
    Can_HwHandleType hwHandle;   /* Which TX mailbox to use */
    uint8_t          dlc;
} CanIf_TxPduConfigType;

/* Configuration entry: maps a received CAN ID to an RX PDU handle */
typedef struct {
    uint32_t  canId;
    bool      isExtended;
    PduIdType rxPduId;           /* Handle passed up to PduR */
} CanIf_RxPduConfigType;

/* Software TX buffer entry */
typedef struct {
    bool         pending;
    Can_PduType  frame;
    PduIdType    pduId;
} CanIf_TxBufferEntryType;

#define CANIF_TX_PDU_COUNT  16U
#define CANIF_RX_PDU_COUNT  32U
#define CANIF_TX_QUEUE_SIZE 8U

/* Public API */
void           CanIf_Init(void);
Std_ReturnType CanIf_Transmit(PduIdType txPduId, const PduInfoType *pduInfoPtr);
void           CanIf_RxIndication(Can_HwHandleType mailboxHandle, const Can_PduType *canPduPtr);
void           CanIf_TxConfirmation(Can_HwHandleType mailboxHandle);
void           CanIf_MainFunction(void);

#endif /* CAN_IF_H */
```

```c
/*
 * CanIf.c — CAN Interface implementation
 */

#include "CanIf.h"
#include "PduR_CanIf.h"  /* PduR_CanIfRxIndication, PduR_CanIfTxConfirmation */
#include <string.h>

/* -----------------------------------------------------------------------
 * Static configuration tables (normally generated by tooling from ARXML)
 * ----------------------------------------------------------------------- */

static const CanIf_TxPduConfigType CanIf_TxPduConfig[CANIF_TX_PDU_COUNT] = {
    /* pduId,  canId,      isExtended, hwHandle, dlc */
    { 0x00U,  0x100U,     false,      0U,       8U },  /* EngineSpeed PDU     */
    { 0x01U,  0x200U,     false,      1U,       4U },  /* ThrottlePosition PDU */
    { 0x02U,  0x18FF1122, true,       2U,       8U },  /* Diagnostic request   */
    /* ... more PDUs ... */
};

static const CanIf_RxPduConfigType CanIf_RxPduConfig[CANIF_RX_PDU_COUNT] = {
    /* canId,      isExtended, rxPduId */
    { 0x300U,     false,      0x10U },  /* VehicleSpeed RX */
    { 0x400U,     false,      0x11U },  /* BrakePressure RX */
    { 0x18FEDF00, true,       0x20U },  /* J1939 ETC2     */
    /* ... more RX PDUs ... */
};

/* Software TX queue */
static CanIf_TxBufferEntryType CanIf_TxQueue[CANIF_TX_QUEUE_SIZE];
static uint8_t CanIf_TxQueueHead = 0U;
static uint8_t CanIf_TxQueueTail = 0U;

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

static const CanIf_TxPduConfigType *CanIf_FindTxConfig(PduIdType pduId) {
    for (uint16_t i = 0; i < CANIF_TX_PDU_COUNT; i++) {
        if (CanIf_TxPduConfig[i].txPduId == pduId) {
            return &CanIf_TxPduConfig[i];
        }
    }
    return NULL;
}

static const CanIf_RxPduConfigType *CanIf_FindRxConfig(uint32_t canId, bool isExtended) {
    for (uint16_t i = 0; i < CANIF_RX_PDU_COUNT; i++) {
        if (CanIf_RxPduConfig[i].canId == canId &&
            CanIf_RxPduConfig[i].isExtended == isExtended) {
            return &CanIf_RxPduConfig[i];
        }
    }
    return NULL;
}

static bool CanIf_TxQueuePush(PduIdType pduId, const Can_PduType *frame) {
    uint8_t next = (CanIf_TxQueueTail + 1U) % CANIF_TX_QUEUE_SIZE;
    if (next == CanIf_TxQueueHead) return false; /* Queue full */
    CanIf_TxQueue[CanIf_TxQueueTail].pending = true;
    CanIf_TxQueue[CanIf_TxQueueTail].pduId   = pduId;
    CanIf_TxQueue[CanIf_TxQueueTail].frame   = *frame;
    CanIf_TxQueueTail = next;
    return true;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void CanIf_Init(void) {
    memset(CanIf_TxQueue, 0, sizeof(CanIf_TxQueue));
    CanIf_TxQueueHead = 0U;
    CanIf_TxQueueTail = 0U;
}

Std_ReturnType CanIf_Transmit(PduIdType txPduId, const PduInfoType *pduInfoPtr) {
    if (pduInfoPtr == NULL || pduInfoPtr->SduDataPtr == NULL) return E_NOT_OK;

    const CanIf_TxPduConfigType *cfg = CanIf_FindTxConfig(txPduId);
    if (cfg == NULL) return E_NOT_OK;

    Can_PduType canPdu;
    canPdu.id         = cfg->canId;
    canPdu.isExtended = cfg->isExtended;
    canPdu.dlc        = (uint8_t)(pduInfoPtr->SduLength > cfg->dlc
                                  ? cfg->dlc : pduInfoPtr->SduLength);
    memcpy(canPdu.sdu, pduInfoPtr->SduDataPtr, canPdu.dlc);

    /* Try direct transmission first */
    Std_ReturnType result = Can_Write(cfg->hwHandle, &canPdu);
    if (result != E_OK) {
        /* Mailbox busy — push to software queue for retry */
        if (!CanIf_TxQueuePush(txPduId, &canPdu)) {
            return E_NOT_OK; /* Queue also full — drop and report */
        }
    }
    return E_OK;
}

/* Called by CAN Driver when a frame arrives */
void CanIf_RxIndication(Can_HwHandleType mailboxHandle,
                        const Can_PduType *canPduPtr) {
    (void)mailboxHandle; /* Not needed for ID-based lookup in this example */

    /* Software CAN ID filter */
    const CanIf_RxPduConfigType *cfg =
        CanIf_FindRxConfig(canPduPtr->id, canPduPtr->isExtended);

    if (cfg == NULL) {
        return; /* Frame filtered out — no matching PDU configured */
    }

    /* Build PduInfoType and notify PDU Router */
    PduInfoType pduInfo;
    /* Safe cast: canPduPtr->sdu is valid for the duration of this call */
    pduInfo.SduDataPtr = (uint8_t *)canPduPtr->sdu;
    pduInfo.SduLength  = canPduPtr->dlc;

    PduR_CanIfRxIndication(cfg->rxPduId, &pduInfo);
}

/* Called by CAN Driver when a TX mailbox completes */
void CanIf_TxConfirmation(Can_HwHandleType mailboxHandle) {
    /* Find which PDU was in this mailbox and notify PduR */
    /* In production this tracks pending TX per mailbox handle */
    (void)mailboxHandle;
    PduR_CanIfTxConfirmation(0U /* resolved pduId */);
}

/* Periodic function: drain TX queue */
void CanIf_MainFunction(void) {
    while (CanIf_TxQueueHead != CanIf_TxQueueTail) {
        CanIf_TxBufferEntryType *entry = &CanIf_TxQueue[CanIf_TxQueueHead];
        const CanIf_TxPduConfigType *cfg = CanIf_FindTxConfig(entry->pduId);
        if (cfg == NULL) {
            CanIf_TxQueueHead = (CanIf_TxQueueHead + 1U) % CANIF_TX_QUEUE_SIZE;
            continue;
        }
        if (Can_Write(cfg->hwHandle, &entry->frame) == E_OK) {
            CanIf_TxQueueHead = (CanIf_TxQueueHead + 1U) % CANIF_TX_QUEUE_SIZE;
        } else {
            break; /* Still busy, try next cycle */
        }
    }
}
```

---

### 8.3 PDU Router — Routing Table and Forwarding

```c
/*
 * PduR.h — PDU Router module
 */

#ifndef PDU_R_H
#define PDU_R_H

#include "CanIf.h"   /* PduIdType, PduInfoType */

/* Routing destination module identifiers */
typedef enum {
    PDUR_DEST_COM    = 0x01U,
    PDUR_DEST_CANIF  = 0x02U,
    PDUR_DEST_DCM    = 0x03U,  /* Diagnostic communication manager */
    PDUR_DEST_NM     = 0x04U,  /* Network management               */
    PDUR_DEST_GATEWAY= 0x05U,  /* Gateway to another bus           */
} PduR_DestModuleType;

/* One entry in the RX routing table */
typedef struct {
    PduIdType            srcPduId;    /* PDU ID as handed by CanIf */
    PduIdType            destPduId;   /* PDU ID as passed to destination */
    PduR_DestModuleType  destModule;
} PduR_RxRoutingTableEntryType;

/* One entry in the TX routing table */
typedef struct {
    PduIdType            srcPduId;    /* PDU ID used by COM/DCM to call PduR */
    PduIdType            destPduId;   /* PDU ID used when calling CanIf */
    PduR_DestModuleType  srcModule;
} PduR_TxRoutingTableEntryType;

#define PDUR_RX_ROUTE_COUNT 32U
#define PDUR_TX_ROUTE_COUNT 16U

/* Public API */
void           PduR_Init(void);
/* Called by CanIf on reception */
void           PduR_CanIfRxIndication(PduIdType rxPduId, const PduInfoType *pduInfoPtr);
void           PduR_CanIfTxConfirmation(PduIdType txPduId);
/* Called by COM/DCM to transmit */
Std_ReturnType PduR_ComTransmit(PduIdType id, const PduInfoType *info);
Std_ReturnType PduR_DcmTransmit(PduIdType id, const PduInfoType *info);

#endif /* PDU_R_H */
```

```c
/*
 * PduR.c — PDU Router implementation
 */

#include "PduR.h"
#include "Com.h"    /* Com_RxIndication */
#include "Dcm.h"    /* Dcm_RxIndication */
#include "CanIf.h"  /* CanIf_Transmit   */
#include <string.h>

/* -----------------------------------------------------------------------
 * Static routing tables — generated from ARXML in a real project
 * ----------------------------------------------------------------------- */

static const PduR_RxRoutingTableEntryType PduR_RxRoutingTable[PDUR_RX_ROUTE_COUNT] = {
    /*  srcPduId  destPduId  destModule           Description         */
    {   0x10U,   0x50U,    PDUR_DEST_COM    },  /* VehicleSpeed → COM */
    {   0x11U,   0x51U,    PDUR_DEST_COM    },  /* BrakePressure → COM */
    {   0x20U,   0x60U,    PDUR_DEST_DCM    },  /* DiagRequest → DCM  */
    {   0x10U,   0x90U,    PDUR_DEST_GATEWAY},  /* VehicleSpeed gateway to LIN */
    /* Note: 0x10U appears twice → fan-out (1:N routing)             */
};

static const PduR_TxRoutingTableEntryType PduR_TxRoutingTable[PDUR_TX_ROUTE_COUNT] = {
    /*  srcPduId  destPduId  srcModule      Description                  */
    {   0x70U,   0x00U,    PDUR_DEST_COM  },  /* COM EngineSpeed → CanIf */
    {   0x71U,   0x01U,    PDUR_DEST_COM  },  /* COM Throttle → CanIf    */
    {   0x80U,   0x02U,    PDUR_DEST_DCM  },  /* DCM DiagResp → CanIf    */
};

/* -----------------------------------------------------------------------
 * Routing dispatch helpers
 * ----------------------------------------------------------------------- */

static void PduR_RouteRxToDestination(PduR_DestModuleType dest,
                                       PduIdType destPduId,
                                       const PduInfoType *pduInfo) {
    switch (dest) {
        case PDUR_DEST_COM:
            Com_RxIndication(destPduId, pduInfo);
            break;
        case PDUR_DEST_DCM:
            Dcm_RxIndication(destPduId, pduInfo);
            break;
        case PDUR_DEST_GATEWAY:
            /* For gateway: call CanIf_Transmit on the other channel.
             * In a real gateway ECU this selects the correct bus interface. */
            CanIf_Transmit(destPduId, pduInfo);
            break;
        default:
            break; /* Unknown destination — no action */
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void PduR_Init(void) {
    /* Nothing to initialize in a purely table-driven router */
}

/* Called by CanIf when a CAN frame is received */
void PduR_CanIfRxIndication(PduIdType rxPduId, const PduInfoType *pduInfoPtr) {
    /* Walk the entire routing table: multiple destinations possible (1:N) */
    for (uint16_t i = 0; i < PDUR_RX_ROUTE_COUNT; i++) {
        if (PduR_RxRoutingTable[i].srcPduId == rxPduId) {
            PduR_RouteRxToDestination(
                PduR_RxRoutingTable[i].destModule,
                PduR_RxRoutingTable[i].destPduId,
                pduInfoPtr
            );
            /* Do NOT break — continue to find more destinations (fan-out) */
        }
    }
}

void PduR_CanIfTxConfirmation(PduIdType txPduId) {
    /* Find which upper module owns this TX PDU and notify it */
    for (uint16_t i = 0; i < PDUR_TX_ROUTE_COUNT; i++) {
        if (PduR_TxRoutingTable[i].destPduId == txPduId) {
            /* COM and DCM have TxConfirmation callbacks for timing purposes */
            if (PduR_TxRoutingTable[i].srcModule == PDUR_DEST_COM) {
                Com_TxConfirmation(PduR_TxRoutingTable[i].srcPduId);
            }
            break;
        }
    }
}

Std_ReturnType PduR_ComTransmit(PduIdType id, const PduInfoType *info) {
    for (uint16_t i = 0; i < PDUR_TX_ROUTE_COUNT; i++) {
        if (PduR_TxRoutingTable[i].srcPduId == id &&
            PduR_TxRoutingTable[i].srcModule == PDUR_DEST_COM) {
            return CanIf_Transmit(PduR_TxRoutingTable[i].destPduId, info);
        }
    }
    return E_NOT_OK; /* No routing entry found */
}

Std_ReturnType PduR_DcmTransmit(PduIdType id, const PduInfoType *info) {
    for (uint16_t i = 0; i < PDUR_TX_ROUTE_COUNT; i++) {
        if (PduR_TxRoutingTable[i].srcPduId == id &&
            PduR_TxRoutingTable[i].srcModule == PDUR_DEST_DCM) {
            return CanIf_Transmit(PduR_TxRoutingTable[i].destPduId, info);
        }
    }
    return E_NOT_OK;
}
```

---

### 8.4 COM Module — Signal Packing, Filtering, and Cyclic Transmission

```c
/*
 * Com.h — COM module
 */

#ifndef COM_H
#define COM_H

#include "PduR.h"

/* Signal data type variants */
typedef enum {
    COM_UINT8 = 0, COM_UINT16, COM_UINT32, COM_SINT8, COM_SINT16, COM_SINT32,
    COM_FLOAT32
} Com_SignalType;

/* Signal filter modes */
typedef enum {
    COM_FILTER_ALWAYS         = 0,  /* Always transmit on new value      */
    COM_FILTER_NEVER          = 1,  /* Never trigger transmission        */
    COM_FILTER_MASKED_NEW_DIFFERS_OLD = 2 /* Trigger only if bits differ */
} Com_FilterAlgorithmType;

/* Signal configuration (generated from System Description) */
typedef struct {
    uint16_t                signalId;
    PduIdType               pduId;         /* Which I-PDU this signal lives in */
    uint8_t                 byteOffset;    /* Byte position within I-PDU        */
    uint8_t                 bitOffset;     /* Bit offset within the byte        */
    uint8_t                 bitLength;     /* Number of bits                    */
    bool                    isBigEndian;   /* Motorola (true) or Intel (false)  */
    Com_SignalType          type;
    Com_FilterAlgorithmType filter;
    uint32_t                filterMask;    /* Mask for MASKED_NEW_DIFFERS_OLD   */
} Com_SignalConfigType;

/* I-PDU configuration */
typedef struct {
    PduIdType   pduId;
    uint8_t     length;           /* Byte length of this I-PDU (max 8 classic CAN) */
    uint32_t    cyclePeriod_ms;   /* 0 = event-triggered only                      */
    uint32_t    timerValue_ms;    /* Current cycle countdown                        */
    bool        txPending;        /* Set when a signal triggers this PDU            */
    uint8_t     buffer[8];        /* I-PDU byte buffer                              */
    uint32_t    shadowValues[8];  /* Last transmitted signal values (for filter)    */
} Com_IPduType;

#define COM_SIGNAL_COUNT 32U
#define COM_IPDU_COUNT   8U

/* Public API */
void           Com_Init(void);
Std_ReturnType Com_SendSignal(uint16_t signalId, const void *signalDataPtr);
Std_ReturnType Com_ReceiveSignal(uint16_t signalId, void *signalDataPtr);
void           Com_RxIndication(PduIdType pduId, const PduInfoType *pduInfoPtr);
void           Com_TxConfirmation(PduIdType pduId);
void           Com_MainFunctionTx(void);  /* Call every OS task cycle */
void           Com_MainFunctionRx(void);  /* Call every OS task cycle */

#endif /* COM_H */
```

```c
/*
 * Com.c — COM module: signal packing/unpacking and cyclic TX
 */

#include "Com.h"
#include "PduR.h"
#include <string.h>

/* -----------------------------------------------------------------------
 * Signal and PDU tables (generated from ARXML/System Description)
 * ----------------------------------------------------------------------- */

static const Com_SignalConfigType Com_SignalConfig[COM_SIGNAL_COUNT] = {
    /*  id    pdu  byte  bit  len  big     type          filter              mask */
    {  0x01, 0x50,  0,   0,  16,  false, COM_UINT16, COM_FILTER_ALWAYS,    0xFFFF },
    {  0x02, 0x50,  2,   0,  8,   false, COM_UINT8,  COM_FILTER_ALWAYS,    0x00FF },
    {  0x03, 0x51,  0,   0,  8,   false, COM_UINT8,  COM_FILTER_MASKED_NEW_DIFFERS_OLD, 0xF0 },
    /* ... */
};

static Com_IPduType Com_IPdus[COM_IPDU_COUNT] = {
    /*  pduId  len  period(ms)  timer  pending  buffer  shadow */
    { 0x70U,   8,  10,          10,    false,  {0}, {0} }, /* EngineSpeed: 100Hz */
    { 0x71U,   4,  20,          20,    false,  {0}, {0} }, /* Throttle:    50Hz  */
    /* ... */
};

/* -----------------------------------------------------------------------
 * Signal bit-packing helpers
 * ----------------------------------------------------------------------- */

/* Pack an integer value into an I-PDU buffer at specified bit position.
 * Supports Intel (little-endian) byte order only in this example.
 * Production code must also handle Motorola (big-endian) signals. */
static void Com_PackSignal(uint8_t *pduBuf, uint8_t byteOffset, uint8_t bitOffset,
                            uint8_t bitLength, uint32_t value) {
    uint32_t mask = (bitLength == 32) ? 0xFFFFFFFFU : ((1U << bitLength) - 1U);
    value &= mask;

    /* Walk through bits and write them into the byte buffer */
    for (uint8_t bit = 0; bit < bitLength; bit++) {
        uint8_t totalBit = byteOffset * 8U + bitOffset + bit;
        uint8_t byteIdx  = totalBit / 8U;
        uint8_t bitIdx   = totalBit % 8U;

        if (value & (1U << bit)) {
            pduBuf[byteIdx] |=  (uint8_t)(1U << bitIdx);
        } else {
            pduBuf[byteIdx] &= ~(uint8_t)(1U << bitIdx);
        }
    }
}

/* Extract an integer value from an I-PDU buffer */
static uint32_t Com_UnpackSignal(const uint8_t *pduBuf, uint8_t byteOffset,
                                  uint8_t bitOffset, uint8_t bitLength) {
    uint32_t result = 0U;
    for (uint8_t bit = 0; bit < bitLength; bit++) {
        uint8_t totalBit = byteOffset * 8U + bitOffset + bit;
        uint8_t byteIdx  = totalBit / 8U;
        uint8_t bitIdx   = totalBit % 8U;
        if (pduBuf[byteIdx] & (1U << bitIdx)) {
            result |= (1U << bit);
        }
    }
    return result;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void Com_Init(void) {
    memset(Com_IPdus, 0, sizeof(Com_IPdus));
    /* Restore configured cycle timers */
    for (uint8_t i = 0; i < COM_IPDU_COUNT; i++) {
        Com_IPdus[i].timerValue_ms = Com_IPdus[i].cyclePeriod_ms;
    }
}

/* Called by application SWC to update a signal value */
Std_ReturnType Com_SendSignal(uint16_t signalId, const void *signalDataPtr) {
    if (signalDataPtr == NULL) return E_NOT_OK;

    /* Find signal configuration */
    const Com_SignalConfigType *sig = NULL;
    for (uint16_t i = 0; i < COM_SIGNAL_COUNT; i++) {
        if (Com_SignalConfig[i].signalId == signalId) {
            sig = &Com_SignalConfig[i];
            break;
        }
    }
    if (sig == NULL) return E_NOT_OK;

    /* Find the I-PDU that owns this signal */
    Com_IPduType *pdu = NULL;
    for (uint8_t i = 0; i < COM_IPDU_COUNT; i++) {
        if (Com_IPdus[i].pduId == sig->pduId) {
            pdu = &Com_IPdus[i];
            break;
        }
    }
    if (pdu == NULL) return E_NOT_OK;

    /* Convert signal value to 32-bit for packing */
    uint32_t rawValue = 0;
    switch (sig->type) {
        case COM_UINT8:  rawValue = *(const uint8_t  *)signalDataPtr; break;
        case COM_UINT16: rawValue = *(const uint16_t *)signalDataPtr; break;
        case COM_UINT32: rawValue = *(const uint32_t *)signalDataPtr; break;
        case COM_SINT8:  rawValue = (uint32_t)(int32_t)(*(const int8_t *)signalDataPtr); break;
        default:         rawValue = *(const uint32_t *)signalDataPtr; break;
    }

    /* Apply transmission filter */
    bool trigger = false;
    switch (sig->filter) {
        case COM_FILTER_ALWAYS:
            trigger = true;
            break;
        case COM_FILTER_NEVER:
            trigger = false;
            break;
        case COM_FILTER_MASKED_NEW_DIFFERS_OLD: {
            uint32_t oldVal = pdu->shadowValues[sig->byteOffset];
            trigger = ((rawValue & sig->filterMask) != (oldVal & sig->filterMask));
            break;
        }
        default:
            trigger = true;
            break;
    }

    /* Pack signal bits into I-PDU buffer */
    Com_PackSignal(pdu->buffer, sig->byteOffset, sig->bitOffset,
                   sig->bitLength, rawValue);

    /* Update shadow value for filter */
    pdu->shadowValues[sig->byteOffset] = rawValue;

    /* Event-driven transmission trigger */
    if (trigger && pdu->cyclePeriod_ms == 0U) {
        pdu->txPending = true;
    }

    return E_OK;
}

/* Called by application SWC to read a received signal */
Std_ReturnType Com_ReceiveSignal(uint16_t signalId, void *signalDataPtr) {
    if (signalDataPtr == NULL) return E_NOT_OK;

    const Com_SignalConfigType *sig = NULL;
    for (uint16_t i = 0; i < COM_SIGNAL_COUNT; i++) {
        if (Com_SignalConfig[i].signalId == signalId) { sig = &Com_SignalConfig[i]; break; }
    }
    if (sig == NULL) return E_NOT_OK;

    Com_IPduType *pdu = NULL;
    for (uint8_t i = 0; i < COM_IPDU_COUNT; i++) {
        if (Com_IPdus[i].pduId == sig->pduId) { pdu = &Com_IPdus[i]; break; }
    }
    if (pdu == NULL) return E_NOT_OK;

    uint32_t rawValue = Com_UnpackSignal(pdu->buffer, sig->byteOffset,
                                          sig->bitOffset, sig->bitLength);

    /* Write to caller's buffer */
    switch (sig->type) {
        case COM_UINT8:  *(uint8_t  *)signalDataPtr = (uint8_t)rawValue;  break;
        case COM_UINT16: *(uint16_t *)signalDataPtr = (uint16_t)rawValue; break;
        case COM_UINT32: *(uint32_t *)signalDataPtr = rawValue;            break;
        default:         *(uint32_t *)signalDataPtr = rawValue;            break;
    }
    return E_OK;
}

/* Called by PduR when a received CAN frame arrives at COM */
void Com_RxIndication(PduIdType pduId, const PduInfoType *pduInfoPtr) {
    for (uint8_t i = 0; i < COM_IPDU_COUNT; i++) {
        if (Com_IPdus[i].pduId == pduId) {
            uint8_t len = (pduInfoPtr->SduLength < 8U)
                          ? (uint8_t)pduInfoPtr->SduLength : 8U;
            memcpy(Com_IPdus[i].buffer, pduInfoPtr->SduDataPtr, len);
            return;
        }
    }
}

void Com_TxConfirmation(PduIdType pduId) {
    /* Can be used to reset TxError counter, update TxTimeoutMonitor, etc. */
    (void)pduId;
}

/* Cyclic TX main function — call every 1ms from OS task */
void Com_MainFunctionTx(void) {
    for (uint8_t i = 0; i < COM_IPDU_COUNT; i++) {
        Com_IPduType *pdu = &Com_IPdus[i];

        bool doSend = false;

        /* Cyclic PDU: decrement timer, send when it expires */
        if (pdu->cyclePeriod_ms > 0U) {
            if (pdu->timerValue_ms > 0U) {
                pdu->timerValue_ms--;
            }
            if (pdu->timerValue_ms == 0U) {
                pdu->timerValue_ms = pdu->cyclePeriod_ms;
                doSend = true;
            }
        }

        /* Event-triggered PDU: send if flagged */
        if (pdu->txPending) {
            pdu->txPending = false;
            doSend = true;
        }

        if (doSend) {
            PduInfoType info;
            info.SduDataPtr = pdu->buffer;
            info.SduLength  = pdu->length;
            PduR_ComTransmit(pdu->pduId, &info);
        }
    }
}

/* Cyclic RX main function — implements deadline monitoring */
void Com_MainFunctionRx(void) {
    /* In a full implementation: decrement per-PDU RX timeout counters.
     * If counter reaches 0, call the RxTimeoutNotification callback.
     * This notifies the application that a sender has gone silent. */
}
```

---

## 9. Rust Implementation Examples

Rust is increasingly used in safety-critical automotive contexts. The following examples mirror the C architecture using Rust's type system to enforce the layer separation at compile time.

### 9.1 CAN Driver — Hardware Abstraction with `unsafe` Register Access

```rust
// can_driver.rs — CAN Driver layer (hardware-facing)
// Uses volatile_register crate pattern; no_std for embedded targets.

#![allow(dead_code)]
use core::ptr::{read_volatile, write_volatile};

// -----------------------------------------------------------------------
// Hardware register definitions (memory-mapped)
// -----------------------------------------------------------------------

const CAN1_BASE: usize = 0x4000_6400;

/// Raw hardware register block — only accessed via read/write volatile
struct CanRegisters {
    mcr:  u32,   // +0x00 Master Control Register
    msr:  u32,   // +0x04 Master Status Register
    tsr:  u32,   // +0x08 Transmit Status Register
    _rf0r: u32,  // +0x0C RX FIFO 0
    ier:  u32,   // +0x10 Interrupt Enable
    _esr: u32,   // +0x14 Error Status
    btr:  u32,   // +0x18 Bit Timing
}

const CAN_MCR_INRQ:  u32 = 1 << 0;
const CAN_MCR_SLEEP: u32 = 1 << 1;
const CAN_MSR_INAK:  u32 = 1 << 0;
const CAN_TSR_TME0:  u32 = 1 << 26;
const CAN_TIR_TXRQ:  u32 = 1 << 0;
const CAN_TIR_IDE:   u32 = 1 << 2;

// -----------------------------------------------------------------------
// Public types — correspond to AUTOSAR Can.h types
// -----------------------------------------------------------------------

#[derive(Debug, Clone, Copy)]
pub enum CanControllerState { Uninit, Started, Stopped, Sleep }

#[derive(Debug, Clone, Copy)]
pub struct CanPdu {
    pub id:          u32,
    pub dlc:         u8,
    pub data:        [u8; 8],
    pub is_extended: bool,
}

#[derive(Debug)]
pub enum CanError {
    InvalidMailbox,
    MailboxBusy,
    InvalidDlc,
    HwTimeout,
}

pub type HwHandle = u8;

// -----------------------------------------------------------------------
// CAN Driver implementation
// -----------------------------------------------------------------------

pub struct CanDriver {
    base: usize,
}

impl CanDriver {
    /// Create a driver instance bound to a hardware base address.
    /// Safety: caller must ensure `base` is valid for the target MCU.
    pub const unsafe fn new(base: usize) -> Self {
        CanDriver { base }
    }

    fn reg_read(&self, offset: usize) -> u32 {
        unsafe { read_volatile((self.base + offset) as *const u32) }
    }

    fn reg_write(&self, offset: usize, val: u32) {
        unsafe { write_volatile((self.base + offset) as *mut u32, val) }
    }

    fn reg_modify(&self, offset: usize, clear: u32, set: u32) {
        let v = self.reg_read(offset);
        self.reg_write(offset, (v & !clear) | set);
    }

    fn wait_bit(&self, offset: usize, mask: u32, expected: u32) -> bool {
        for _ in 0..10_000u32 {
            if (self.reg_read(offset) & mask) == expected {
                return true;
            }
        }
        false
    }

    /// Initialize CAN controller for 500 kBit/s @ 48 MHz APB1
    pub fn init(&self) -> Result<(), CanError> {
        const MCR_OFF: usize = 0x00;
        const MSR_OFF: usize = 0x04;
        const BTR_OFF: usize = 0x18;
        const IER_OFF: usize = 0x10;

        // Request init mode
        self.reg_modify(MCR_OFF, CAN_MCR_SLEEP, CAN_MCR_INRQ);

        if !self.wait_bit(MSR_OFF, CAN_MSR_INAK, CAN_MSR_INAK) {
            return Err(CanError::HwTimeout);
        }

        // Configure bit timing (500 kBit/s, 48 MHz)
        self.reg_write(BTR_OFF,
            (0 << 24) |   // SJW
            (1 << 20) |   // TS2 - 1
            (12 << 16) |  // TS1 - 1
            (5 << 0));    // BRP - 1

        // Enable FIFO0 message pending interrupt
        self.reg_modify(IER_OFF, 0, 1 << 1);

        // Leave init mode
        self.reg_modify(MCR_OFF, CAN_MCR_INRQ, 0);
        if !self.wait_bit(MSR_OFF, CAN_MSR_INAK, 0) {
            return Err(CanError::HwTimeout);
        }

        Ok(())
    }

    /// Transmit an L-PDU via the specified hardware mailbox (0, 1, or 2)
    pub fn write(&self, hth: HwHandle, pdu: &CanPdu) -> Result<(), CanError> {
        if hth > 2 || pdu.dlc > 8 {
            return Err(CanError::InvalidMailbox);
        }

        const TSR_OFF: usize = 0x08;
        let tme_mask = CAN_TSR_TME0 << hth;
        if (self.reg_read(TSR_OFF) & tme_mask) == 0 {
            return Err(CanError::MailboxBusy);
        }

        // TX mailbox register offsets (each mailbox is 16 bytes apart)
        let mb_base: usize = 0x180 + (hth as usize) * 16;
        let tir_off  = mb_base;
        let tdtr_off = mb_base + 4;
        let tdlr_off = mb_base + 8;
        let tdhr_off = mb_base + 12;

        // Clear TIR
        self.reg_write(tir_off, 0);

        // Set CAN ID
        let tir = if pdu.is_extended {
            (pdu.id << 3) | CAN_TIR_IDE
        } else {
            pdu.id << 21
        };
        self.reg_write(tir_off, tir);

        // DLC
        self.reg_write(tdtr_off, pdu.dlc as u32);

        // Data registers
        let mut low: u32  = 0;
        let mut high: u32 = 0;
        for i in 0..pdu.dlc.min(4) as usize {
            low  |= (pdu.data[i] as u32) << (i * 8);
        }
        for i in 4..pdu.dlc.min(8) as usize {
            high |= (pdu.data[i] as u32) << ((i - 4) * 8);
        }
        self.reg_write(tdlr_off, low);
        self.reg_write(tdhr_off, high);

        // Trigger TX
        self.reg_modify(tir_off, 0, CAN_TIR_TXRQ);

        Ok(())
    }

    /// Poll RX FIFO 0 and return a received frame, if any
    pub fn poll_rx(&self) -> Option<CanPdu> {
        const RF0R_OFF: usize = 0x0C;
        const FIFO0_BASE: usize = 0x1B0;

        if (self.reg_read(RF0R_OFF) & 0x03) == 0 {
            return None; // No messages pending
        }

        let rir  = self.reg_read(FIFO0_BASE);
        let rdtr = self.reg_read(FIFO0_BASE + 4);
        let rdlr = self.reg_read(FIFO0_BASE + 8);
        let rdhr = self.reg_read(FIFO0_BASE + 12);

        let is_extended = (rir & CAN_TIR_IDE) != 0;
        let id = if is_extended { (rir >> 3) & 0x1FFF_FFFF }
                 else           { (rir >> 21) & 0x7FF };
        let dlc = (rdtr & 0x0F) as u8;

        let mut data = [0u8; 8];
        for i in 0..dlc.min(4) as usize {
            data[i] = (rdlr >> (i * 8)) as u8;
        }
        for i in 4..dlc.min(8) as usize {
            data[i] = (rdhr >> ((i - 4) * 8)) as u8;
        }

        // Release FIFO slot (RFOM0 bit)
        self.reg_modify(RF0R_OFF, 0, 1 << 5);

        Some(CanPdu { id, dlc, data, is_extended })
    }
}
```

---

### 9.2 CAN Interface — Type-Safe PDU Handle Mapping

```rust
// can_if.rs — CAN Interface layer
// Uses newtypes to prevent mixing TX/RX PDU handles at compile time.

use crate::can_driver::{CanDriver, CanError, CanPdu, HwHandle};

// -----------------------------------------------------------------------
// Newtype wrappers — compile-time separation of TX and RX PDU IDs
// -----------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TxPduId(pub u16);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RxPduId(pub u16);

#[derive(Debug, Clone)]
pub struct PduInfo<'a> {
    pub data:   &'a [u8],
    pub length: u16,
}

// -----------------------------------------------------------------------
// Configuration tables
// -----------------------------------------------------------------------

#[derive(Clone, Copy)]
pub struct TxPduConfig {
    pub pdu_id:      TxPduId,
    pub can_id:      u32,
    pub is_extended: bool,
    pub hw_handle:   HwHandle,
    pub dlc:         u8,
}

#[derive(Clone, Copy)]
pub struct RxPduConfig {
    pub can_id:      u32,
    pub is_extended: bool,
    pub rx_pdu_id:   RxPduId,
}

// -----------------------------------------------------------------------
// TX software queue
// -----------------------------------------------------------------------

const TX_QUEUE_SIZE: usize = 8;

struct TxQueueEntry {
    active: bool,
    pdu_id: TxPduId,
    frame:  CanPdu,
}

impl Default for TxQueueEntry {
    fn default() -> Self {
        TxQueueEntry {
            active: false,
            pdu_id: TxPduId(0),
            frame: CanPdu { id: 0, dlc: 0, data: [0u8; 8], is_extended: false },
        }
    }
}

// -----------------------------------------------------------------------
// CAN Interface
// -----------------------------------------------------------------------

pub struct CanInterface<'drv, 'cfg, RxCb> {
    driver:    &'drv CanDriver,
    tx_config: &'cfg [TxPduConfig],
    rx_config: &'cfg [RxPduConfig],
    tx_queue:  [TxQueueEntry; TX_QUEUE_SIZE],
    q_head:    usize,
    q_tail:    usize,
    rx_callback: RxCb,  // Closure/fn called on reception → replaces PduR callback
}

impl<'drv, 'cfg, RxCb> CanInterface<'drv, 'cfg, RxCb>
where
    RxCb: FnMut(RxPduId, &[u8]),
{
    pub fn new(
        driver:      &'drv CanDriver,
        tx_config:   &'cfg [TxPduConfig],
        rx_config:   &'cfg [RxPduConfig],
        rx_callback: RxCb,
    ) -> Self {
        CanInterface {
            driver, tx_config, rx_config,
            tx_queue: core::array::from_fn(|_| TxQueueEntry::default()),
            q_head: 0, q_tail: 0,
            rx_callback,
        }
    }

    /// Transmit a PDU — called by PduR layer
    pub fn transmit(&mut self, tx_pdu_id: TxPduId, info: &PduInfo) -> Result<(), CanError> {
        let cfg = self.tx_config.iter()
            .find(|c| c.pdu_id == tx_pdu_id)
            .ok_or(CanError::InvalidMailbox)?;

        let dlc = info.length.min(cfg.dlc as u16) as u8;
        let mut data = [0u8; 8];
        data[..dlc as usize].copy_from_slice(&info.data[..dlc as usize]);

        let frame = CanPdu { id: cfg.can_id, dlc, data, is_extended: cfg.is_extended };

        match self.driver.write(cfg.hw_handle, &frame) {
            Ok(()) => Ok(()),
            Err(CanError::MailboxBusy) => {
                self.enqueue(tx_pdu_id, frame)
                    .ok_or(CanError::MailboxBusy)
            },
            Err(e) => Err(e),
        }
    }

    /// Poll hardware for new frames and drive RX callback
    pub fn main_function_rx(&mut self) {
        while let Some(frame) = self.driver.poll_rx() {
            // Software ID filter
            if let Some(cfg) = self.rx_config.iter().find(|c| {
                c.can_id == frame.id && c.is_extended == frame.is_extended
            }) {
                (self.rx_callback)(cfg.rx_pdu_id, &frame.data[..frame.dlc as usize]);
            }
        }
    }

    /// Drain the TX software queue
    pub fn main_function_tx(&mut self) {
        while self.q_head != self.q_tail {
            let entry = &self.tx_queue[self.q_head];
            if !entry.active { break; }
            let cfg = match self.tx_config.iter().find(|c| c.pdu_id == entry.pdu_id) {
                Some(c) => c,
                None => { self.advance_head(); continue; }
            };
            match self.driver.write(cfg.hw_handle, &entry.frame) {
                Ok(()) => self.advance_head(),
                Err(_) => break, // Still busy
            }
        }
    }

    fn enqueue(&mut self, pdu_id: TxPduId, frame: CanPdu) -> Option<()> {
        let next = (self.q_tail + 1) % TX_QUEUE_SIZE;
        if next == self.q_head { return None; } // Full
        self.tx_queue[self.q_tail] = TxQueueEntry { active: true, pdu_id, frame };
        self.q_tail = next;
        Some(())
    }

    fn advance_head(&mut self) {
        self.tx_queue[self.q_head].active = false;
        self.q_head = (self.q_head + 1) % TX_QUEUE_SIZE;
    }
}
```

---

### 9.3 PDU Router — Enum-Driven Routing with Zero Allocation

```rust
// pdu_router.rs — PDU Router: static routing table with enum dispatch

use crate::can_if::{TxPduId, RxPduId, PduInfo};

// -----------------------------------------------------------------------
// Routing destination — Rust enum replaces C integer codes
// -----------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum RoutingDest {
    Com  { dest_pdu: u16 },
    Dcm  { dest_pdu: u16 },
    CanIf{ dest_pdu: u16 },   // Gateway: route back to a CAN channel
}

#[derive(Clone, Copy)]
pub struct RxRoute {
    pub src_pdu_id: RxPduId,
    pub destination: RoutingDest,
}

#[derive(Clone, Copy)]
pub struct TxRoute {
    pub com_pdu_id:  u16,      // PDU ID used by COM when calling PduR
    pub canif_pdu_id: TxPduId, // PDU ID passed down to CanIf
}

// -----------------------------------------------------------------------
// PDU Router — generic over COM and DCM callbacks
// -----------------------------------------------------------------------

pub struct PduRouter<'cfg, ComRx, DcmRx, CanIfTx>
where
    ComRx:   FnMut(u16, &[u8]),
    DcmRx:   FnMut(u16, &[u8]),
    CanIfTx: FnMut(TxPduId, &PduInfo) -> Result<(), ()>,
{
    rx_routes:  &'cfg [RxRoute],
    tx_routes:  &'cfg [TxRoute],
    com_rx_cb:  ComRx,
    dcm_rx_cb:  DcmRx,
    canif_tx:   CanIfTx,
}

impl<'cfg, ComRx, DcmRx, CanIfTx> PduRouter<'cfg, ComRx, DcmRx, CanIfTx>
where
    ComRx:   FnMut(u16, &[u8]),
    DcmRx:   FnMut(u16, &[u8]),
    CanIfTx: FnMut(TxPduId, &PduInfo) -> Result<(), ()>,
{
    pub fn new(
        rx_routes: &'cfg [RxRoute],
        tx_routes: &'cfg [TxRoute],
        com_rx_cb: ComRx,
        dcm_rx_cb: DcmRx,
        canif_tx:  CanIfTx,
    ) -> Self {
        PduRouter { rx_routes, tx_routes, com_rx_cb, dcm_rx_cb, canif_tx }
    }

    /// Called by CanIf when a frame has been received and filtered
    pub fn canif_rx_indication(&mut self, rx_pdu_id: RxPduId, data: &[u8]) {
        // Fan-out: iterate ALL routes for 1:N delivery
        for route in self.rx_routes {
            if route.src_pdu_id != rx_pdu_id { continue; }

            match route.destination {
                RoutingDest::Com   { dest_pdu } => (self.com_rx_cb)(dest_pdu, data),
                RoutingDest::Dcm   { dest_pdu } => (self.dcm_rx_cb)(dest_pdu, data),
                RoutingDest::CanIf { dest_pdu } => {
                    // Gateway: forward to another CAN channel
                    let info = PduInfo { data, length: data.len() as u16 };
                    let _ = (self.canif_tx)(TxPduId(dest_pdu), &info);
                }
            }
        }
    }

    /// Called by COM to request transmission of an I-PDU
    pub fn com_transmit(&mut self, com_pdu_id: u16, data: &[u8]) -> Result<(), ()> {
        for route in self.tx_routes {
            if route.com_pdu_id == com_pdu_id {
                let info = PduInfo { data, length: data.len() as u16 };
                return (self.canif_tx)(route.canif_pdu_id, &info);
            }
        }
        Err(()) // No routing entry
    }
}
```

---

### 9.4 COM Module — Signal Packing with Const Generics

```rust
// com_module.rs — COM module: signal packing and cyclic TX

use crate::pdu_router::PduRouter;

// -----------------------------------------------------------------------
// Signal and PDU types
// -----------------------------------------------------------------------

#[derive(Clone, Copy, Debug)]
pub enum SignalType { U8, U16, U32, I8, I16, I32 }

#[derive(Clone, Copy)]
pub struct SignalConfig {
    pub signal_id:   u16,
    pub pdu_id:      u16,
    pub byte_offset: u8,
    pub bit_offset:  u8,
    pub bit_length:  u8,
    pub sig_type:    SignalType,
    pub cyclic:      bool,  // If false: event-triggered only
}

pub struct IPduState {
    pub pdu_id:       u16,
    pub dlc:          u8,
    pub period_ms:    u32,
    pub timer_ms:     u32,
    pub tx_pending:   bool,
    pub buffer:       [u8; 8],
}

// -----------------------------------------------------------------------
// COM module
// -----------------------------------------------------------------------

pub struct ComModule<'cfg, const N_SIG: usize, const N_PDU: usize> {
    signals:  &'cfg [SignalConfig; N_SIG],
    pdus:     [IPduState; N_PDU],
}

impl<'cfg, const N_SIG: usize, const N_PDU: usize> ComModule<'cfg, N_SIG, N_PDU> {

    pub fn new(
        signals: &'cfg [SignalConfig; N_SIG],
        pdus: [IPduState; N_PDU],
    ) -> Self {
        ComModule { signals, pdus }
    }

    // -----------------------------------------------------------------------
    // Signal bit packing (Intel / little-endian byte order)
    // -----------------------------------------------------------------------

    fn pack_signal(buf: &mut [u8; 8], byte_off: u8, bit_off: u8,
                   bit_len: u8, value: u32) {
        let mask: u32 = if bit_len == 32 { 0xFFFF_FFFF } else { (1 << bit_len) - 1 };
        let value = value & mask;
        for bit in 0..bit_len {
            let total = byte_off as u16 * 8 + bit_off as u16 + bit as u16;
            let byte_idx = (total / 8) as usize;
            let bit_idx  = (total % 8) as u8;
            if byte_idx < 8 {
                if value & (1 << bit) != 0 {
                    buf[byte_idx] |=  1 << bit_idx;
                } else {
                    buf[byte_idx] &= !(1 << bit_idx);
                }
            }
        }
    }

    fn unpack_signal(buf: &[u8; 8], byte_off: u8, bit_off: u8, bit_len: u8) -> u32 {
        let mut result = 0u32;
        for bit in 0..bit_len {
            let total = byte_off as u16 * 8 + bit_off as u16 + bit as u16;
            let byte_idx = (total / 8) as usize;
            let bit_idx  = (total % 8) as u8;
            if byte_idx < 8 && (buf[byte_idx] & (1 << bit_idx)) != 0 {
                result |= 1 << bit;
            }
        }
        result
    }

    // -----------------------------------------------------------------------
    // Application API
    // -----------------------------------------------------------------------

    /// Write a signal — called by application SWC (replaces Com_SendSignal)
    pub fn send_signal(&mut self, signal_id: u16, value: u32) -> Result<(), &'static str> {
        let sig = self.signals.iter()
            .find(|s| s.signal_id == signal_id)
            .ok_or("Signal not found")?;

        let pdu = self.pdus.iter_mut()
            .find(|p| p.pdu_id == sig.pdu_id)
            .ok_or("PDU not found")?;

        Self::pack_signal(&mut pdu.buffer, sig.byte_offset, sig.bit_offset,
                          sig.bit_length, value);

        if !sig.cyclic {
            pdu.tx_pending = true; // Event-triggered: schedule immediate TX
        }
        Ok(())
    }

    /// Read a signal — called by application SWC (replaces Com_ReceiveSignal)
    pub fn receive_signal(&self, signal_id: u16) -> Result<u32, &'static str> {
        let sig = self.signals.iter()
            .find(|s| s.signal_id == signal_id)
            .ok_or("Signal not found")?;

        let pdu = self.pdus.iter()
            .find(|p| p.pdu_id == sig.pdu_id)
            .ok_or("PDU not found")?;

        Ok(Self::unpack_signal(&pdu.buffer, sig.byte_offset, sig.bit_offset,
                                sig.bit_length))
    }

    /// Called by PduR on RX (stores received bytes in the PDU buffer)
    pub fn rx_indication(&mut self, pdu_id: u16, data: &[u8]) {
        if let Some(pdu) = self.pdus.iter_mut().find(|p| p.pdu_id == pdu_id) {
            let len = data.len().min(pdu.dlc as usize);
            pdu.buffer[..len].copy_from_slice(&data[..len]);
        }
    }

    /// Cyclic TX processing — call every 1 ms from OS periodic task
    pub fn main_function_tx<ComRx, DcmRx, CanIfTx>(
        &mut self,
        router: &mut PduRouter<ComRx, DcmRx, CanIfTx>,
    ) where
        ComRx:   FnMut(u16, &[u8]),
        DcmRx:   FnMut(u16, &[u8]),
        CanIfTx: FnMut(crate::can_if::TxPduId, &crate::can_if::PduInfo) -> Result<(), ()>,
    {
        for pdu in self.pdus.iter_mut() {
            let mut do_send = false;

            // Cyclic timer
            if pdu.period_ms > 0 {
                pdu.timer_ms = pdu.timer_ms.saturating_sub(1);
                if pdu.timer_ms == 0 {
                    pdu.timer_ms = pdu.period_ms;
                    do_send = true;
                }
            }

            // Event-triggered
            if pdu.tx_pending {
                pdu.tx_pending = false;
                do_send = true;
            }

            if do_send {
                let data = &pdu.buffer[..pdu.dlc as usize];
                let _ = router.com_transmit(pdu.pdu_id, data);
            }
        }
    }
}
```

---

### 9.5 Integration — Wiring the Stack Together

```rust
// main.rs — Stack integration example

mod can_driver;
mod can_if;
mod pdu_router;
mod com_module;

use can_driver::CanDriver;
use can_if::{CanInterface, TxPduConfig, RxPduConfig, TxPduId, RxPduId};
use pdu_router::{PduRouter, RxRoute, TxRoute, RoutingDest};
use com_module::{ComModule, SignalConfig, SignalType, IPduState};

fn main() {
    // ---- 1. CAN Driver (hardware layer) ----
    let driver = unsafe { CanDriver::new(0x4000_6400) };
    driver.init().expect("CAN init failed");

    // ---- 2. CAN Interface configuration ----
    let tx_config = [
        TxPduConfig { pdu_id: TxPduId(0x00), can_id: 0x100, is_extended: false,
                      hw_handle: 0, dlc: 8 },
        TxPduConfig { pdu_id: TxPduId(0x01), can_id: 0x200, is_extended: false,
                      hw_handle: 1, dlc: 4 },
    ];
    let rx_config = [
        RxPduConfig { can_id: 0x300, is_extended: false, rx_pdu_id: RxPduId(0x10) },
        RxPduConfig { can_id: 0x400, is_extended: false, rx_pdu_id: RxPduId(0x11) },
    ];

    // ---- 3. PDU Router routing tables ----
    let rx_routes = [
        RxRoute { src_pdu_id: RxPduId(0x10), destination: RoutingDest::Com { dest_pdu: 0x50 } },
        RxRoute { src_pdu_id: RxPduId(0x10), destination: RoutingDest::CanIf { dest_pdu: 0x90 } }, // Gateway
        RxRoute { src_pdu_id: RxPduId(0x11), destination: RoutingDest::Com { dest_pdu: 0x51 } },
    ];
    let tx_routes = [
        TxRoute { com_pdu_id: 0x70, canif_pdu_id: TxPduId(0x00) },
        TxRoute { com_pdu_id: 0x71, canif_pdu_id: TxPduId(0x01) },
    ];

    // ---- 4. COM signal configuration ----
    let signals = [
        SignalConfig { signal_id: 0x01, pdu_id: 0x70, byte_offset: 0, bit_offset: 0,
                       bit_length: 16, sig_type: SignalType::U16, cyclic: true },
        SignalConfig { signal_id: 0x02, pdu_id: 0x70, byte_offset: 2, bit_offset: 0,
                       bit_length: 8,  sig_type: SignalType::U8,  cyclic: true },
    ];
    let pdus = [
        IPduState { pdu_id: 0x70, dlc: 8, period_ms: 10, timer_ms: 10,
                    tx_pending: false, buffer: [0u8; 8] },
        IPduState { pdu_id: 0x71, dlc: 4, period_ms: 20, timer_ms: 20,
                    tx_pending: false, buffer: [0u8; 8] },
    ];

    let mut com = ComModule::new(&signals, pdus);

    // ---- 5. Build the CAN Interface (captures driver) ----
    // We'll use a local signal buffer to share receive state with COM
    let mut rx_buffer: [(u16, [u8; 8]); 4] = [(0, [0u8; 8]); 4];

    let mut can_if = CanInterface::new(
        &driver, &tx_config, &rx_config,
        |rx_pdu_id, data| {
            // In a real system: call PduR_CanIfRxIndication
            // Here we store for later processing to avoid borrowing issues
            let _ = (rx_pdu_id, data);
        },
    );

    // ---- 6. Build PDU Router ----
    let mut router = PduRouter::new(
        &rx_routes, &tx_routes,
        /* COM RX */ |pdu_id, data| {
            let _ = (pdu_id, data); // -> com.rx_indication(pdu_id, data)
        },
        /* DCM RX */ |pdu_id, data| { let _ = (pdu_id, data); },
        /* CanIf TX */ |tx_pdu_id, info| {
            let _ = (tx_pdu_id, info); // -> can_if.transmit(...)
            Ok(())
        },
    );

    // ---- 7. Application writes a signal ----
    let engine_rpm: u32 = 2500; // Simulated: 2500 RPM
    com.send_signal(0x01, engine_rpm).expect("Signal send failed");

    // ---- 8. OS periodic task simulation (1 ms tick) ----
    loop {
        can_if.main_function_rx();    // CanDrv poll → CanIf filter → PduR notify
        can_if.main_function_tx();    // Drain TX software queue
        com.main_function_tx(&mut router);  // Cyclic I-PDU dispatch
        // In an RTOS: OsTask_WaitEvent(1ms);
        break; // Exit in this example
    }
}
```

---

## 10. Configuration and ARXML

In a real AUTOSAR project, all configuration tables seen in the examples above are **generated** — never hand-written. The toolchain flow is:

```
System Designer (OEM)
  │
  │  System Description (.arxml)
  │  - ECU extract, COM matrix, network topology
  ▼
ECU Configurator (Tier-1 / supplier tool: DaVinci, EB Tresos)
  │
  │  Per-module configuration containers:
  │  - CanDriverConfiguration
  │  - CanIfInitConfig / CanIfTxPduConfig / CanIfRxPduConfig
  │  - PduRRoutingPath / PduRRoutingPathGroup
  │  - ComConfig / ComSignal / ComIPdu
  ▼
Code Generator
  │
  │  Generated C files:
  │  - CanIf_Cfg.h / CanIf_Cfg.c
  │  - PduR_Cfg.h  / PduR_Cfg.c
  │  - Com_Cfg.h   / Com_Cfg.c
  ▼
Compiler + Linker → ECU Binary
```

A fragment of what a `ComSignal` looks like in ARXML:

```xml
<COM-SIGNAL>
  <SHORT-NAME>EngineSpeed</SHORT-NAME>
  <DATA-TYPE-REF DEST="ImplementationDataType">/DataTypes/uint16</DATA-TYPE-REF>
  <NETWORK-REPRESENTATION-PROPS>
    <SW-DATA-DEF-PROPS-VARIANTS>
      <SW-DATA-DEF-PROPS-CONDITIONAL>
        <BASE-TYPE-REF DEST="SwBaseType">/AUTOSAR/Platform/uint16</BASE-TYPE-REF>
      </SW-DATA-DEF-PROPS-CONDITIONAL>
    </SW-DATA-DEF-PROPS-VARIANTS>
  </NETWORK-REPRESENTATION-PROPS>
  <START-POSITION>0</START-POSITION>   <!-- bit start position in I-PDU -->
  <LENGTH>16</LENGTH>                  <!-- bit length -->
  <BIT-POSITION>0</BIT-POSITION>
  <BYTE-ORDER>OPAQUE</BYTE-ORDER>      <!-- Intel = OPAQUE, Motorola = MOST-SIGNIFICANT-BYTE-FIRST -->
</COM-SIGNAL>
```

---

## 11. Error Handling Across Layers

| Layer | Error Condition | AUTOSAR Response |
|---|---|---|
| CanDrv | HW mailbox full | Return `CAN_BUSY` → CanIf queues |
| CanDrv | Bus-off | `CanIf_ControllerBusOff()` callback |
| CanIf | TX queue full | Return `E_NOT_OK` to PduR |
| CanIf | Unknown CAN ID | Frame silently discarded (filtered) |
| PduR | No routing entry | Return `E_NOT_OK` to caller |
| COM | TX deadline missed | `Com_CbkTxErr()` notification |
| COM | RX timeout | `Com_CbkRxTOut()` notification |

In an AUTOSAR system, errors that should be visible during development are reported via the **DET (Default Error Tracer)** module using `Det_ReportError(MODULE_ID, INSTANCE_ID, API_ID, ERROR_ID)`. Production builds typically have DET disabled to save flash/RAM.

---

## 12. Summary

The AUTOSAR CAN stack implements a clean four-layer separation of concerns:

The **CAN Driver** (CanDrv) is the only hardware-dependent component. It manages physical registers, mailboxes, baud rate, and interrupt/polling RX. It knows nothing about PDU IDs or signal semantics — only raw CAN frames.

The **CAN Interface** (CanIf) provides hardware independence to all layers above it. It maintains an abstract PDU handle space, performs software-level ID filtering, manages a TX software queue for mailbox contention, and translates between CAN frame terminology and AUTOSAR PDU terminology.

The **PDU Router** (PduR) is the communication hub. It routes received PDUs to one or more upper-layer modules (COM, DCM, NM) using a compile-time routing table, enables transparent gateway behaviour between buses, and supports 1:N fan-out. It has no signal knowledge — it operates purely on byte buffers and PDU IDs.

The **COM Module** is the signal layer. It packs and unpacks typed application signals into/from I-PDU byte arrays, handles endianness, manages cyclic and event-driven transmission timing, applies signal filters to suppress redundant transmissions, and provides deadline monitoring for received PDUs.

The key architectural benefits are **supplier independence** (any module can be replaced at its layer boundary), **portability** (CanDrv is the only non-portable part), and **separation of concerns** (an application engineer works only with signals; a network engineer works only with ARXML; hardware bringup only touches CanDrv). The entire stack is statically configured at compile-time from ARXML, resulting in zero dynamic memory allocation at runtime — a critical property for safety-critical embedded automotive systems.

---

*Document: 51 — AUTOSAR CAN Stack Architecture | CAN Bus Mastery Series*