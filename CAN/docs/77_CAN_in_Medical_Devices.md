# 77. CAN in Medical Devices


**Regulatory Framework** — How IEC 62304's three safety classes (A/B/C) map onto CAN software components, and how ISO 13485 design controls, supplier management, and configuration management apply throughout the lifecycle.

**Architecture & Safety Design** — Layered CAN software architecture, message ID prioritization by clinical urgency, CANopen usage in medical contexts, and the application-level safety frame pattern (sequence counter + CRC-16 on top of hardware CRC).

**C/C++ Examples (4 listings):**
- MISRA-compliant CAN peripheral initialization with timeout guards and fault reporting
- Safety-wrapped frame transmitter with sequence counter and application CRC
- C++ heartbeat monitor with safe-state escalation for silent nodes
- Bus-off error handler with bounded recovery attempts before permanent fault

**Rust Examples (4 listings):**
- Type-safe CAN frame builder using `Option`/`Result` — no panics, no UB
- Safety-wrapped transmitter with `saturating_add` arithmetic
- `no_std`-compatible heartbeat monitor using `heapless::Vec`
- Bus-off state machine with embedded unit tests traceable to IEC 62304 Class C test requirements

**Traceability & Testing** — Bidirectional traceability matrix, SOUP handling, unit/integration/system test strategies, and MISRA vs. Rust static analysis approaches.


## Applying IEC 62304 Software Lifecycle and ISO 13485 Quality Management to Medical CAN Systems

---

## Table of Contents

1. [Introduction](#introduction)
2. [Regulatory Framework Overview](#regulatory-framework-overview)
3. [IEC 62304 Software Lifecycle for CAN Systems](#iec-62304-software-lifecycle-for-can-systems)
4. [ISO 13485 Quality Management Applied to CAN](#iso-13485-quality-management-applied-to-can)
5. [CAN in Medical Device Architectures](#can-in-medical-device-architectures)
6. [Safety Classification and Risk Management](#safety-classification-and-risk-management)
7. [CAN Protocol Considerations for Medical Use](#can-protocol-considerations-for-medical-use)
8. [Programming Examples in C/C++](#programming-examples-in-cc)
9. [Programming Examples in Rust](#programming-examples-in-rust)
10. [Traceability and Documentation](#traceability-and-documentation)
11. [Testing and Verification](#testing-and-verification)
12. [Summary](#summary)

---

## Introduction

The Controller Area Network (CAN) bus, originally developed for automotive applications, has found significant adoption in medical devices due to its robustness, deterministic behavior, multi-master capability, and strong error-detection mechanisms. Medical devices such as patient monitoring systems, infusion pumps, surgical robots, imaging equipment, and radiation therapy machines increasingly rely on CAN to interconnect safety-critical subsystems.

However, deploying CAN in medical devices is not merely a technical engineering exercise. It demands adherence to a comprehensive regulatory and quality framework that governs how medical software is designed, implemented, verified, and maintained throughout its lifecycle. The two most relevant international standards are:

- **IEC 62304:2006+AMD1:2015** — Medical device software: Software life cycle processes
- **ISO 13485:2016** — Medical devices: Quality management systems — Requirements for regulatory purposes

Together, these standards impose structured processes on every phase of CAN software development, from architecture through deployment and post-market surveillance. Non-compliance is not merely a quality concern — it is a legal and regulatory barrier to market entry in the EU (MDR 2017/745), the US (FDA 21 CFR Part 820), and most global markets.

---

## Regulatory Framework Overview

### IEC 62304

IEC 62304 defines a software lifecycle framework specifically for medical device software. It classifies software into three safety classes based on the severity of harm that a software failure could cause:

| Class | Severity of Injury | Process Requirements |
|-------|--------------------|----------------------|
| A     | No injury or damage to health | Basic lifecycle, limited documentation |
| B     | Non-serious injury | Adds detailed architecture, unit testing |
| C     | Death or serious injury | Full lifecycle, comprehensive testing, traceability |

For CAN systems in medical devices, the software that controls, monitors, or communicates safety-critical data over CAN is typically classified **Class B or Class C**. For example, a CAN driver managing drug dosage commands in an infusion pump is Class C; a CAN module displaying non-critical status messages may be Class B.

### ISO 13485

ISO 13485 is a quality management system (QMS) standard tailored to the medical device industry. It goes beyond ISO 9001 by adding requirements specific to regulatory compliance, risk management integration, and design controls. For CAN development, ISO 13485 mandates:

- Documented design and development planning
- Design input/output controls
- Design verification and validation
- Design transfer controls
- Change control processes
- Supplier controls (including CAN silicon and IP vendors)
- Post-market surveillance feedback loops

### ISO 14971

Although not the focus of this chapter, ISO 14971 (Risk Management for Medical Devices) is inseparable from IEC 62304 and ISO 13485. CAN software risk items must be identified, analyzed, evaluated, mitigated, and tracked in a risk management file that spans the product's entire lifecycle.

---

## IEC 62304 Software Lifecycle for CAN Systems

### Planning Phase

Before a single line of CAN driver code is written, IEC 62304 requires a **Software Development Plan (SDP)**. For a CAN-based subsystem, this plan must address:

- Identification of all CAN software items (drivers, protocol stacks, application logic)
- Software safety classification for each item
- Tools and compilers used (with qualification evidence if needed)
- Configuration management strategy for CAN firmware
- Integration with the system-level risk management process

### Software Requirements

Requirements for CAN software must be documented in a **Software Requirements Specification (SRS)**. These are directly traceable to system requirements and risk control measures. Typical CAN-related software requirements include:

- The CAN driver shall detect and report bus-off states within 10 ms
- The system shall retransmit any lost safety-critical CAN frame up to 3 times before triggering a safe state
- CAN message identifiers shall be allocated such that high-priority safety messages have arbitration priority over status messages
- The software shall verify the integrity of each received CAN frame using application-level CRC in addition to the hardware CRC

### Software Architecture

IEC 62304 requires a documented **Software Architecture Design** that decomposes the system into software items. For a CAN-based medical device, a typical layered architecture is:

```
┌─────────────────────────────────────────┐
│         Application Layer               │
│  (Clinical logic, safety state machine) │
├─────────────────────────────────────────┤
│         Protocol Layer                  │
│  (CANopen / J1939 / proprietary stack)  │
├─────────────────────────────────────────┤
│         CAN Abstraction Layer           │
│  (Frame Tx/Rx, ID filtering, timing)    │
├─────────────────────────────────────────┤
│         CAN Driver Layer (HAL)          │
│  (Hardware register access, ISR)        │
├─────────────────────────────────────────┤
│         CAN Hardware / ASIC             │
└─────────────────────────────────────────┘
```

Each layer is a separately documented and tested software item, enabling class-appropriate verification at each boundary.

### Detailed Design

For Class C software, a **Software Detailed Design** document must specify:

- CAN message object allocation in hardware mailboxes
- Interrupt service routine (ISR) design and re-entrancy considerations
- Mutex and critical section usage for shared CAN buffers
- Timeout and watchdog behaviors for bus silence detection
- Error handling state machine (active, passive, bus-off, recovery)

### Implementation

Coding standards must be defined and applied. For safety-critical CAN software in C/C++, MISRA C:2012 (or MISRA C++:2008) is the dominant coding guideline. For Rust, the memory-safety guarantees of the language satisfy many MISRA goals by design, making it an increasingly attractive choice for medical CAN software.

---

## ISO 13485 Quality Management Applied to CAN

### Design Controls

ISO 13485 Clause 7.3 mandates formal **design controls** for every medical device. For CAN-based subsystems, this means:

- **Design Inputs:** Derived from clinical, regulatory, and risk requirements (e.g., "the CAN network must detect a single-point failure within 5 ms")
- **Design Outputs:** Architecture documents, detailed designs, code, test procedures, and manufacturing procedures
- **Design Reviews:** Formal reviews at each phase gate, with documented sign-off by QA, Systems Engineering, and Clinical/Regulatory representatives
- **Design Verification:** Confirming design outputs meet design inputs (e.g., test that CAN bus-off detection latency is ≤ 5 ms)
- **Design Validation:** Confirming the final device meets user needs under simulated or actual use conditions

### Supplier Controls

ISO 13485 Clause 7.4 requires evaluation and control of suppliers. For CAN systems, this applies to:

- CAN controller silicon vendors (e.g., NXP, Microchip, Renesas)
- Third-party CAN IP stacks or drivers
- COTS CAN transceivers and isolation components
- Contract software development firms contributing CAN code

Each supplier must be evaluated, approved, and subject to ongoing monitoring. Critical suppliers (those whose failure could directly cause patient harm) require additional controls such as source code escrow, reference design testing, and incoming inspection.

### Configuration Management

All CAN-related software artifacts must be under formal configuration management (CM). ISO 13485 requires traceability of all design outputs to their controlled baseline. For CAN firmware, this means:

- Version-controlled repositories (Git with tag-based releases)
- Build reproducibility (deterministic builds tied to specific compiler versions)
- Change control — any modification to CAN driver logic requires a formal change request, impact assessment, re-verification, and QA approval

---

## CAN in Medical Device Architectures

### Typical Topologies

Medical CAN networks commonly use one of three topologies:

**Linear Bus (most common):** All nodes on a single terminated bus. Simple, reliable, but a single wiring fault can affect all nodes.

**Star with Active Hub:** Nodes connect to a central hub that provides isolation. More fault-tolerant but adds latency and cost.

**Redundant Dual-CAN:** Two parallel CAN buses. Safety-critical nodes connect to both. Provides single-fault tolerance at the network level, common in Class C systems.

### Node Examples

A typical surgical robot using CAN might have the following nodes:

| Node | CAN Role | Safety Class |
|------|----------|--------------|
| Main Safety Controller | Master + Monitor | C |
| Joint Motor Controller × 7 | Slaves | C |
| Force/Torque Sensor Interface | Slave | C |
| Surgeon Console | Slave | B |
| System Status Display | Slave | A |
| Endoscope Camera Controller | Slave | B |

### Message Prioritization

CAN's non-destructive bitwise arbitration means that lower numerical CAN IDs win arbitration and are transmitted first. Medical device message ID allocation must reflect clinical priority:

```
0x001–0x0FF  Emergency/Safety (STOP, FAULT, WATCHDOG)
0x100–0x1FF  Real-time Control (motor commands, sensor data)
0x200–0x3FF  Monitoring (vital signs, device state)
0x400–0x5FF  Configuration and Diagnostics
0x600–0x7FF  Non-critical Status and Logging
```

---

## Safety Classification and Risk Management

### FMEA Integration with CAN Design

A CAN-specific Failure Mode and Effects Analysis (FMEA) must identify failure modes at the CAN bus, controller, and message levels:

| Failure Mode | Effect | Severity | Mitigation |
|---|---|---|---|
| CAN bus short circuit | All nodes lose communication | Critical | Redundant bus, galvanic isolation |
| Node fails to transmit heartbeat | Controller appears offline | Critical | Watchdog timeout → safe state |
| Message corruption (undetected) | Wrong command executed | Catastrophic | App-level CRC + sequence counter |
| Bus-off lockout | Node permanently silenced | Serious | Automatic bus-off recovery with limit |
| ID collision (misconfiguration) | Priority inversion | Serious | Factory-programmed unique node IDs |
| Babbling idiot node | Bus saturation | Serious | Bus guardian or transmit inhibit |

### Safe State Design

Every Class C CAN node must define and implement a **safe state** — a condition it enters when it detects a failure it cannot recover from. Safe state behaviors for CAN nodes include:

- Ceasing all control outputs (motor drives off, valves closed)
- Transmitting a final emergency frame before going bus-off (best effort)
- Enabling hardware watchdog to force a controlled reset
- Illuminating fault indicators and generating an auditable event log entry

---

## CAN Protocol Considerations for Medical Use

### CANopen in Medical Devices

CANopen (CiA 301) is the most widely used higher-layer protocol for medical CAN systems. It provides:

- **NMT (Network Management):** Node state machine (Initialization → Pre-Operational → Operational → Stopped)
- **PDO (Process Data Objects):** Low-latency cyclic or event-driven data exchange for real-time control
- **SDO (Service Data Objects):** Client-server configuration and parameter access
- **EMCY (Emergency Objects):** Standardized error reporting with 2-byte error code
- **Heartbeat/Guarding:** Node alive monitoring

Medical implementations must supplement CANopen with application-level safety extensions because CANopen itself is not certified for functional safety without a safety layer such as **CANopen Safety (EN 50325-5)** or a proprietary redundancy protocol.

### Application-Level Safety Protocol

A common pattern is to add a safety wrapper to each critical CAN message:

```
┌──────────────────────────────────────────────┐
│ CAN ID (11/29-bit) │ DLC │ DATA              │
│                    │     │ [SEQ][CRC][PAYLOAD]│
└──────────────────────────────────────────────┘
```

- **SEQ:** 8-bit rolling sequence counter (detects lost or duplicated frames)
- **CRC:** 16-bit CRC over SEQ + PAYLOAD (detects corruption; hardware CRC alone covers only the CAN frame, not application-layer errors)
- **PAYLOAD:** Actual data (motor command, sensor reading, etc.)

### Timing and Determinism

Medical CAN networks require bounded worst-case latency. Key parameters to verify:

- **Bus utilization:** Should remain below 60–70% to ensure priority messages win arbitration within one frame period
- **Bit rate:** 250 kbit/s to 1 Mbit/s typical; higher rates require shorter bus lengths
- **Propagation delay:** Must be accounted for in bit timing calculations to ensure all nodes sample bits consistently
- **Worst-case response time:** Must be formally calculated using CAN schedulability analysis (e.g., Tindell's method) and documented as part of design verification

---

## Programming Examples in C/C++

### 1. MISRA-Compliant CAN Driver Initialization (Bare-Metal C)

This example demonstrates IEC 62304-aligned initialization of a CAN peripheral with explicit error handling, defensive coding, and full state reporting — suitable for a Class C software item.

```c
/**
 * @file    can_driver.c
 * @brief   Medical-grade CAN driver initialization
 *          Conforms to: MISRA C:2012, IEC 62304 Class C
 *          Traceability: SRS-CAN-001, SRS-CAN-002, SRS-CAN-003
 */

#include <stdint.h>
#include <stdbool.h>
#include "can_driver.h"
#include "can_hw_reg.h"   /* Hardware register definitions */
#include "fault_manager.h"
#include "watchdog.h"

/* Module version — must match Software Configuration Item baseline */
#define CAN_DRIVER_VERSION_MAJOR  (2U)
#define CAN_DRIVER_VERSION_MINOR  (4U)

/* Timing parameters for 500 kbit/s @ 80 MHz APB clock
 * Verified against: DESIGN-CAN-007, Test Report TR-CAN-012
 * BRP=7, TSEG1=13, TSEG2=2, SJW=1 => 500 kbit/s, 87.5% sample point
 */
#define CAN_BRP    (7U)
#define CAN_TSEG1  (13U)
#define CAN_TSEG2  (2U)
#define CAN_SJW    (1U)

/* Safety: Bus-off recovery maximum attempts before declaring permanent fault
 * Traceability: RISK-CAN-004 mitigation control
 */
#define CAN_MAX_BUSOFF_RECOVERY_ATTEMPTS  (3U)

/* Internal state — not exposed to application layer */
typedef struct {
    CanState_t     state;
    uint8_t        busoff_count;
    uint32_t       tx_error_count;
    uint32_t       rx_error_count;
    bool           initialized;
} CanDriverContext_t;

static CanDriverContext_t s_ctx = {
    .state        = CAN_STATE_UNINIT,
    .busoff_count = 0U,
    .initialized  = false
};

/**
 * @brief  Initialize the CAN peripheral.
 *
 * @pre    System clock must be configured.
 * @pre    Fault manager must be initialized.
 *
 * @param  config  Pointer to caller-supplied configuration. Must not be NULL.
 * @return CAN_OK on success, or a CAN_ERR_* code on failure.
 *
 * Requirement: SRS-CAN-001 — The CAN driver shall initialize within 50 ms.
 */
CanResult_t CAN_Init(const CanConfig_t * const config)
{
    CanResult_t result = CAN_ERR_GENERAL;

    /* MISRA C:2012 Rule 14.5: Guard against NULL parameter */
    if (config == NULL)
    {
        (void)FaultManager_ReportFault(FAULT_ID_CAN_NULL_CONFIG, 0U);
        return CAN_ERR_INVALID_PARAM;
    }

    /* Prevent re-initialization without explicit de-init */
    if (s_ctx.initialized)
    {
        return CAN_ERR_ALREADY_INITIALIZED;
    }

    /* Enter initialization mode — controller stops bus activity */
    CAN_HW_REG->MCR |= CAN_MCR_INRQ;
    uint32_t timeout = CAN_INIT_TIMEOUT_TICKS;
    while (((CAN_HW_REG->MSR & CAN_MSR_INAK) == 0U) && (timeout > 0U))
    {
        timeout--;
        Watchdog_Kick(); /* Keep watchdog alive during polling loop */
    }
    if (timeout == 0U)
    {
        (void)FaultManager_ReportFault(FAULT_ID_CAN_INIT_TIMEOUT, 0U);
        return CAN_ERR_TIMEOUT;
    }

    /* Exit sleep mode */
    CAN_HW_REG->MCR &= ~CAN_MCR_SLEEP;

    /* Configure bit timing — values from verified design document */
    CAN_HW_REG->BTR = ((uint32_t)(CAN_SJW   - 1U) << 24U)
                    | ((uint32_t)(CAN_TSEG2 - 1U) << 20U)
                    | ((uint32_t)(CAN_TSEG1 - 1U) << 16U)
                    | ((uint32_t)(CAN_BRP   - 1U));

    /* Enable automatic bus-off recovery, TX FIFO mode, error interrupts */
    CAN_HW_REG->MCR |= (CAN_MCR_ABOM | CAN_MCR_TXFP);
    CAN_HW_REG->IER  = (CAN_IER_ERRIE | CAN_IER_BOFIE | CAN_IER_FMPIE0);

    /* Exit initialization mode */
    CAN_HW_REG->MCR &= ~CAN_MCR_INRQ;
    timeout = CAN_INIT_TIMEOUT_TICKS;
    while (((CAN_HW_REG->MSR & CAN_MSR_INAK) != 0U) && (timeout > 0U))
    {
        timeout--;
        Watchdog_Kick();
    }
    if (timeout == 0U)
    {
        (void)FaultManager_ReportFault(FAULT_ID_CAN_INIT_TIMEOUT, 1U);
        return CAN_ERR_TIMEOUT;
    }

    /* Update internal state */
    s_ctx.state       = CAN_STATE_ACTIVE;
    s_ctx.initialized = true;
    result            = CAN_OK;

    return result;
}
```

### 2. Safety-Wrapped CAN Message Transmission (C)

Demonstrates application-level CRC and sequence counter as required for Class C safety integrity:

```c
/**
 * @file    can_safe_tx.c
 * @brief   Safety wrapper for CAN transmission
 *          Traceability: SRS-CAN-010 (sequence counter),
 *                        SRS-CAN-011 (application CRC),
 *                        RISK-CAN-008 (undetected corruption mitigation)
 */

#include <string.h>
#include <stdint.h>
#include "can_safe_tx.h"
#include "crc16.h"

/* Safety frame layout:
 *  Byte 0:     Sequence counter (0–255, wraps)
 *  Bytes 1–2:  CRC-16/CCITT over [SEQ][PAYLOAD]
 *  Bytes 3–7:  Payload (up to 5 bytes for standard frame)
 */
#define SAFE_FRAME_SEQ_OFFSET   (0U)
#define SAFE_FRAME_CRC_OFFSET   (1U)
#define SAFE_FRAME_DATA_OFFSET  (3U)
#define SAFE_FRAME_MAX_PAYLOAD  (5U)
#define SAFE_FRAME_OVERHEAD     (3U)  /* SEQ + CRC16 */
#define CAN_MAX_DLC             (8U)

static uint8_t s_sequence_counter = 0U;

/**
 * @brief Transmit a safety-wrapped CAN frame.
 *
 * @param msg_id   CAN message identifier (11-bit standard)
 * @param payload  Pointer to payload data
 * @param length   Payload length (0–5 bytes)
 * @return         CAN_OK or error code
 *
 * Requirement: SRS-CAN-010, SRS-CAN-011
 */
CanResult_t CanSafe_Transmit(uint16_t msg_id,
                             const uint8_t * const payload,
                             uint8_t length)
{
    uint8_t frame_data[CAN_MAX_DLC];
    uint8_t crc_input[1U + SAFE_FRAME_MAX_PAYLOAD];
    uint16_t crc;

    if ((payload == NULL) || (length > SAFE_FRAME_MAX_PAYLOAD))
    {
        return CAN_ERR_INVALID_PARAM;
    }

    /* Build CRC input: [sequence][payload] */
    crc_input[0U] = s_sequence_counter;
    (void)memcpy(&crc_input[1U], payload, (size_t)length);
    crc = CRC16_Compute(crc_input, (uint16_t)(1U + length));

    /* Assemble frame */
    frame_data[SAFE_FRAME_SEQ_OFFSET]      = s_sequence_counter;
    frame_data[SAFE_FRAME_CRC_OFFSET]      = (uint8_t)(crc >> 8U);
    frame_data[SAFE_FRAME_CRC_OFFSET + 1U] = (uint8_t)(crc & 0xFFU);
    (void)memcpy(&frame_data[SAFE_FRAME_DATA_OFFSET], payload, (size_t)length);

    /* Increment counter — wraps naturally at 0xFF → 0x00 */
    s_sequence_counter++;

    return CAN_Transmit(msg_id,
                        frame_data,
                        (uint8_t)(length + SAFE_FRAME_OVERHEAD));
}
```

### 3. Heartbeat Monitor with Safe-State Transition (C++)

Demonstrates the node monitoring pattern required by IEC 62304 for Class C systems:

```cpp
/**
 * @file    heartbeat_monitor.cpp
 * @brief   CAN node heartbeat monitor with safe-state escalation
 *          Traceability: SRS-CAN-020 through SRS-CAN-025
 *          Risk Control: RISK-CAN-015 (silent node detection)
 */

#include "heartbeat_monitor.hpp"
#include "system_time.hpp"
#include "safe_state_manager.hpp"
#include "event_log.hpp"
#include <cstdint>
#include <array>

namespace medical_can {

/**
 * @brief Monitors CAN heartbeat messages from a set of registered nodes.
 *
 * Each node must transmit a heartbeat within its configured period.
 * A single missed heartbeat triggers a warning.
 * Consecutive missed heartbeats beyond the threshold trigger safe-state entry.
 */
class HeartbeatMonitor
{
public:
    static constexpr std::uint8_t MAX_NODES              = 16U;
    static constexpr std::uint8_t MISS_THRESHOLD_WARNING = 1U;
    static constexpr std::uint8_t MISS_THRESHOLD_FAULT   = 3U;

    struct NodeConfig
    {
        std::uint8_t  node_id;
        std::uint32_t period_ms;          /**< Expected heartbeat period */
        std::uint32_t tolerance_ms;       /**< Allowed jitter/latency */
        bool          safety_critical;    /**< If true, fault triggers safe state */
    };

private:
    struct NodeState
    {
        NodeConfig   config;
        std::uint32_t last_rx_time_ms;
        std::uint8_t  miss_count;
        bool          registered;
        bool          in_fault;
    };

    std::array<NodeState, MAX_NODES> m_nodes{};
    std::uint8_t m_node_count{0U};

public:
    /**
     * @brief Register a CAN node for heartbeat monitoring.
     * @return true on success, false if max nodes exceeded.
     */
    bool RegisterNode(const NodeConfig& config)
    {
        if (m_node_count >= MAX_NODES)
        {
            EventLog::Write(EVENT_CAN_MONITOR_OVERFLOW, config.node_id);
            return false;
        }
        NodeState& ns         = m_nodes[m_node_count];
        ns.config             = config;
        ns.last_rx_time_ms    = SystemTime::GetMs();
        ns.miss_count         = 0U;
        ns.registered         = true;
        ns.in_fault           = false;
        ++m_node_count;
        return true;
    }

    /**
     * @brief Called by the CAN receive ISR (or task) when a heartbeat arrives.
     * @param node_id  Node ID from the heartbeat message
     */
    void OnHeartbeatReceived(std::uint8_t node_id)
    {
        for (std::uint8_t i = 0U; i < m_node_count; ++i)
        {
            if (m_nodes[i].config.node_id == node_id)
            {
                m_nodes[i].last_rx_time_ms = SystemTime::GetMs();
                m_nodes[i].miss_count      = 0U;
                if (m_nodes[i].in_fault)
                {
                    m_nodes[i].in_fault = false;
                    EventLog::Write(EVENT_CAN_NODE_RECOVERED, node_id);
                }
                return;
            }
        }
        /* Node not registered — log unexpected heartbeat */
        EventLog::Write(EVENT_CAN_UNKNOWN_NODE, node_id);
    }

    /**
     * @brief Periodic monitoring task — call from a 1 ms or 10 ms scheduler tick.
     *
     * Requirement: SRS-CAN-022 — Monitor shall detect node silence within
     *              (period + tolerance + one monitor tick) milliseconds.
     */
    void Tick()
    {
        const std::uint32_t now_ms = SystemTime::GetMs();

        for (std::uint8_t i = 0U; i < m_node_count; ++i)
        {
            NodeState& ns        = m_nodes[i];
            const std::uint32_t deadline = ns.last_rx_time_ms
                                         + ns.config.period_ms
                                         + ns.config.tolerance_ms;

            if (now_ms > deadline)
            {
                ns.miss_count++;
                ns.last_rx_time_ms = now_ms; /* Restart window to avoid re-trigger */

                if (ns.miss_count >= MISS_THRESHOLD_FAULT)
                {
                    if (!ns.in_fault)
                    {
                        ns.in_fault = true;
                        EventLog::Write(EVENT_CAN_NODE_FAULT, ns.config.node_id);

                        if (ns.config.safety_critical)
                        {
                            /* Escalate to system safe state — no return from this call
                             * in Class C implementations; the safe state manager
                             * handles shutdown sequencing. */
                            SafeStateManager::Enter(SAFE_STATE_REASON_CAN_NODE_LOST,
                                                    ns.config.node_id);
                        }
                    }
                }
                else if (ns.miss_count >= MISS_THRESHOLD_WARNING)
                {
                    EventLog::Write(EVENT_CAN_NODE_LATE, ns.config.node_id);
                }
                else
                {
                    /* Within acceptable miss count — no action */
                }
            }
        }
    }
};

} /* namespace medical_can */
```

### 4. Bus-Off Error Handler with Recovery Limit (C)

```c
/**
 * @file    can_error_handler.c
 * @brief   CAN bus-off error handler with bounded recovery attempts
 *          Traceability: RISK-CAN-004, SRS-CAN-030
 */

#include "can_error_handler.h"
#include "fault_manager.h"
#include "event_log.h"
#include "safe_state_manager.h"

/* Maximum automatic recovery attempts before escalating to permanent fault.
 * Value chosen based on FMEA analysis — see RISK-CAN-004. */
#define MAX_BUSOFF_RECOVERY_ATTEMPTS  (3U)

static uint8_t s_busoff_attempt_count = 0U;

/**
 * @brief  CAN error interrupt handler.
 *         Called from the CAN peripheral error interrupt (e.g., USB_HP_CAN_TX_IRQn).
 *
 * Requirement: SRS-CAN-030 — Detect bus-off within 10 ms of occurrence.
 * Requirement: SRS-CAN-031 — Limit recovery attempts to MAX_BUSOFF_RECOVERY_ATTEMPTS.
 */
void CAN_ErrorIRQHandler(void)
{
    const uint32_t esr = CAN_HW_REG->ESR;

    /* Bus-off condition: TEC exceeded 255 */
    if ((esr & CAN_ESR_BOFF) != 0U)
    {
        s_busoff_attempt_count++;

        EventLog_Write(EVENT_CAN_BUS_OFF,
                       (uint32_t)s_busoff_attempt_count);

        if (s_busoff_attempt_count >= MAX_BUSOFF_RECOVERY_ATTEMPTS)
        {
            /* Exceeded recovery limit — this node is experiencing a
             * persistent fault (likely wiring or transceiver failure).
             * Escalate to safe state and report permanent fault. */
            FaultManager_ReportFault(FAULT_ID_CAN_BUSOFF_PERMANENT,
                                     (uint32_t)s_busoff_attempt_count);
            SafeStateManager_Enter(SAFE_STATE_REASON_CAN_BUSOFF_PERMANENT);
            /* No further CAN recovery attempted */
        }
        else
        {
            /* Automatic bus-off recovery is enabled in MCR (ABOM bit).
             * The hardware will wait 128 × 11 recessive bits then attempt
             * to rejoin the bus. Log and continue monitoring. */
            FaultManager_ReportRecoverableFault(FAULT_ID_CAN_BUS_OFF,
                                                (uint32_t)s_busoff_attempt_count);
        }
    }

    /* Error passive condition: TEC or REC exceeded 127 */
    if ((esr & CAN_ESR_EPVF) != 0U)
    {
        EventLog_Write(EVENT_CAN_ERROR_PASSIVE,
                       (esr & CAN_ESR_TEC_Msk) >> CAN_ESR_TEC_Pos);
    }

    /* Clear error interrupt flag */
    CAN_HW_REG->MSR |= CAN_MSR_ERRI;
}
```

---

## Programming Examples in Rust

Rust's ownership model, type safety, and absence of undefined behavior make it well-suited for safety-critical CAN software. The Rust embedded ecosystem (via the `embedded-hal` and `socketcan` crates) supports both bare-metal and Linux-based CAN implementations.

### 1. Type-Safe CAN Frame Builder (Rust, bare-metal via embedded-hal)

```rust
//! can_frame.rs
//! Medical-grade CAN frame abstraction with compile-time DLC enforcement.
//!
//! Traceability: SRS-CAN-010, SRS-CAN-011
//! Conforms to: IEC 62304 Class C software item

use core::convert::TryFrom;

/// Maximum CAN data length code for standard/FD frame (classical CAN: 0–8)
pub const CAN_MAX_DLC: usize = 8;

/// A valid CAN message identifier (11-bit standard ID)
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct CanStdId(u16);

impl CanStdId {
    /// Construct a standard CAN ID. Returns None if id >= 0x800.
    ///
    /// Using Option<T> ensures the caller handles invalid IDs at compile time
    /// rather than relying on runtime assertions (MISRA-equivalent approach in Rust).
    pub const fn new(id: u16) -> Option<Self> {
        if id < 0x800 {
            Some(Self(id))
        } else {
            None
        }
    }

    pub const fn raw(self) -> u16 {
        self.0
    }
}

/// A validated CAN data frame with immutable data slice.
///
/// Guarantees:
///  - DLC is always in range [0, 8]
///  - Data buffer is stack-allocated (no heap allocation in bare-metal context)
#[derive(Debug, Clone)]
pub struct CanFrame {
    id:   CanStdId,
    dlc:  usize,
    data: [u8; CAN_MAX_DLC],
}

impl CanFrame {
    /// Construct a CAN frame. Returns Err if data.len() > 8.
    pub fn new(id: CanStdId, data: &[u8]) -> Result<Self, CanError> {
        if data.len() > CAN_MAX_DLC {
            return Err(CanError::InvalidDlc(data.len()));
        }
        let mut buf = [0u8; CAN_MAX_DLC];
        buf[..data.len()].copy_from_slice(data);
        Ok(Self { id, dlc: data.len(), data: buf })
    }

    pub fn id(&self)   -> CanStdId { self.id }
    pub fn dlc(&self)  -> usize    { self.dlc }
    pub fn data(&self) -> &[u8]    { &self.data[..self.dlc] }
}

/// CAN driver error type — exhaustive enum prevents unhandled cases.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CanError {
    InvalidDlc(usize),
    InvalidId(u16),
    TxBufferFull,
    BusOff,
    ErrorPassive,
    Timeout,
    NotInitialized,
}
```

### 2. Safety-Wrapped Transmitter with Sequence Counter (Rust)

```rust
//! can_safe_tx.rs
//! Application-level safety wrapper for CAN transmission.
//!
//! Adds sequence counter and CRC-16/CCITT to each outgoing frame.
//! Traceability: SRS-CAN-010, SRS-CAN-011, RISK-CAN-008

use crate::can_frame::{CanFrame, CanStdId, CanError};
use crate::crc16::crc16_ccitt;

/// Maximum application payload (DLC 8 minus 3 bytes overhead: SEQ + CRC16)
pub const SAFE_MAX_PAYLOAD: usize = 5;

/// Safety frame overhead in bytes: 1 (SEQ) + 2 (CRC16)
const OVERHEAD: usize = 3;

/// CAN safety transmitter — owns a rolling sequence counter.
pub struct SafeCanTx<T: CanTransmit> {
    inner:   T,
    seq_ctr: u8,
}

/// Trait abstraction over the hardware CAN driver — enables unit testing
/// without physical hardware (satisfies IEC 62304 unit test requirement).
pub trait CanTransmit {
    fn transmit(&mut self, frame: &CanFrame) -> Result<(), CanError>;
}

impl<T: CanTransmit> SafeCanTx<T> {
    pub fn new(inner: T) -> Self {
        Self { inner, seq_ctr: 0 }
    }

    /// Transmit payload wrapped in safety envelope.
    ///
    /// Frame layout:
    ///   Byte 0:     Sequence counter
    ///   Bytes 1–2:  CRC-16/CCITT over [seq, payload...]
    ///   Bytes 3–N:  Payload
    ///
    /// Returns Err if payload exceeds SAFE_MAX_PAYLOAD.
    pub fn send(
        &mut self,
        id:      CanStdId,
        payload: &[u8],
    ) -> Result<(), CanError> {
        if payload.len() > SAFE_MAX_PAYLOAD {
            // Returning a typed error — no panic, no undefined behavior
            return Err(CanError::InvalidDlc(payload.len() + OVERHEAD));
        }

        let seq = self.seq_ctr;
        // Wrapping add is explicit in Rust — no silent overflow
        self.seq_ctr = self.seq_ctr.wrapping_add(1);

        // Build CRC input: [seq_byte, payload...]
        let mut crc_input = [0u8; 1 + SAFE_MAX_PAYLOAD];
        crc_input[0] = seq;
        crc_input[1..=payload.len()].copy_from_slice(payload);
        let crc = crc16_ccitt(&crc_input[..=payload.len()]);

        // Assemble full frame data
        let mut data = [0u8; 8];
        data[0] = seq;
        data[1] = (crc >> 8) as u8;
        data[2] = (crc & 0xFF) as u8;
        data[3..3 + payload.len()].copy_from_slice(payload);
        let dlc = payload.len() + OVERHEAD;

        let frame = CanFrame::new(id, &data[..dlc])?;
        self.inner.transmit(&frame)
    }
}
```

### 3. Heartbeat Monitor (Rust, no_std compatible)

```rust
//! heartbeat_monitor.rs
//! CAN node heartbeat monitor for bare-metal medical devices.
//!
//! Traceability: SRS-CAN-020 – SRS-CAN-025, RISK-CAN-015
//! no_std compatible — uses heapless::Vec for fixed-capacity node list.

use heapless::Vec;

/// Maximum number of monitored nodes (compile-time constant — no dynamic allocation)
const MAX_NODES: usize = 16;

/// Missed heartbeat count before a warning is raised
const WARN_THRESHOLD: u8 = 1;

/// Missed heartbeat count before a safety fault is declared
const FAULT_THRESHOLD: u8 = 3;

/// Per-node configuration provided at registration time.
#[derive(Debug, Clone)]
pub struct NodeConfig {
    pub node_id:         u8,
    pub period_ms:       u32,
    pub tolerance_ms:    u32,
    pub safety_critical: bool,
}

/// Internal per-node monitoring state.
#[derive(Debug)]
struct NodeState {
    config:           NodeConfig,
    last_rx_ms:       u32,
    miss_count:       u8,
    in_fault:         bool,
}

/// Outcome of a monitoring tick for a given node.
#[derive(Debug, PartialEq, Eq)]
pub enum HeartbeatEvent {
    Ok,
    LateWarning { node_id: u8, miss_count: u8 },
    /// Node is silent beyond the fault threshold.
    /// If safety_critical, caller must enter safe state.
    Fault { node_id: u8, safety_critical: bool },
    /// Node previously in fault has recovered.
    Recovered { node_id: u8 },
}

pub struct HeartbeatMonitor {
    nodes: Vec<NodeState, MAX_NODES>,
}

impl HeartbeatMonitor {
    pub fn new() -> Self {
        Self { nodes: Vec::new() }
    }

    /// Register a node. Returns Err(()) if the monitor is full.
    pub fn register(&mut self, config: NodeConfig, now_ms: u32) -> Result<(), ()> {
        self.nodes.push(NodeState {
            last_rx_ms: now_ms,
            miss_count: 0,
            in_fault: false,
            config,
        }).map_err(|_| ())
    }

    /// Call when a heartbeat frame is received for a given node.
    pub fn on_heartbeat(&mut self, node_id: u8, now_ms: u32) -> HeartbeatEvent {
        for ns in self.nodes.iter_mut() {
            if ns.config.node_id == node_id {
                ns.last_rx_ms = now_ms;
                ns.miss_count = 0;
                if ns.in_fault {
                    ns.in_fault = false;
                    return HeartbeatEvent::Recovered { node_id };
                }
                return HeartbeatEvent::Ok;
            }
        }
        // Unknown node — not registered; caller may log this
        HeartbeatEvent::Ok
    }

    /// Periodic monitoring tick. Returns a Vec of events for this tick.
    ///
    /// Requirement: SRS-CAN-022 — detect silence within (period + tolerance + tick).
    pub fn tick(&mut self, now_ms: u32) -> Vec<HeartbeatEvent, MAX_NODES> {
        let mut events: Vec<HeartbeatEvent, MAX_NODES> = Vec::new();

        for ns in self.nodes.iter_mut() {
            let deadline = ns.last_rx_ms
                .saturating_add(ns.config.period_ms)
                .saturating_add(ns.config.tolerance_ms);

            if now_ms > deadline {
                // Saturating add prevents any possibility of overflow-based
                // incorrect deadline calculation (safety-critical arithmetic)
                ns.miss_count = ns.miss_count.saturating_add(1);
                ns.last_rx_ms = now_ms; // Restart window

                if ns.miss_count >= FAULT_THRESHOLD && !ns.in_fault {
                    ns.in_fault = true;
                    let _ = events.push(HeartbeatEvent::Fault {
                        node_id:         ns.config.node_id,
                        safety_critical: ns.config.safety_critical,
                    });
                } else if ns.miss_count >= WARN_THRESHOLD {
                    let _ = events.push(HeartbeatEvent::LateWarning {
                        node_id:    ns.config.node_id,
                        miss_count: ns.miss_count,
                    });
                }
            }
        }
        events
    }
}
```

### 4. CAN Bus-Off Recovery State Machine (Rust)

```rust
//! busoff_handler.rs
//! Bounded bus-off recovery state machine for medical CAN nodes.
//!
//! Traceability: RISK-CAN-004, SRS-CAN-030, SRS-CAN-031

/// Maximum automatic recovery attempts — after this, declare permanent fault.
const MAX_RECOVERY_ATTEMPTS: u8 = 3;

/// Bus-off handler state
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum BusOffState {
    Normal,
    Recovering { attempt: u8 },
    PermanentFault,
}

/// Action the caller must take after processing a bus-off event.
#[derive(Debug, PartialEq, Eq)]
pub enum BusOffAction {
    /// Allow hardware ABOM to recover; log the attempt.
    AllowRecovery { attempt: u8 },
    /// Recovery limit exceeded; enter device safe state immediately.
    EnterSafeState,
    /// No action needed.
    None,
}

pub struct BusOffHandler {
    state: BusOffState,
}

impl BusOffHandler {
    pub fn new() -> Self {
        Self { state: BusOffState::Normal }
    }

    /// Call when a bus-off interrupt is detected.
    pub fn on_bus_off(&mut self) -> BusOffAction {
        self.state = match self.state {
            BusOffState::Normal => BusOffState::Recovering { attempt: 1 },
            BusOffState::Recovering { attempt } => {
                BusOffState::Recovering { attempt: attempt.saturating_add(1) }
            },
            BusOffState::PermanentFault => BusOffState::PermanentFault,
        };

        match self.state {
            BusOffState::Recovering { attempt } if attempt >= MAX_RECOVERY_ATTEMPTS => {
                self.state = BusOffState::PermanentFault;
                BusOffAction::EnterSafeState
            },
            BusOffState::Recovering { attempt } => {
                BusOffAction::AllowRecovery { attempt }
            },
            BusOffState::PermanentFault => BusOffAction::EnterSafeState,
            BusOffState::Normal => BusOffAction::None,
        }
    }

    /// Call when the node successfully rejoins the bus after bus-off recovery.
    pub fn on_recovery_success(&mut self) {
        // Reset count only on confirmed recovery to prevent gaming the counter
        self.state = BusOffState::Normal;
    }

    pub fn state(&self) -> BusOffState {
        self.state
    }
}

#[cfg(test)]
mod tests {
    //! Unit tests satisfy IEC 62304 Class C unit testing requirement.
    //! These tests must be traceable in the Software Verification Plan.
    //! Test ID: UT-CAN-BUSOFF-001 through UT-CAN-BUSOFF-004

    use super::*;

    #[test]
    fn test_first_busoff_triggers_recovery() {
        // UT-CAN-BUSOFF-001
        let mut handler = BusOffHandler::new();
        let action = handler.on_bus_off();
        assert_eq!(action, BusOffAction::AllowRecovery { attempt: 1 });
    }

    #[test]
    fn test_third_busoff_triggers_safe_state() {
        // UT-CAN-BUSOFF-002
        let mut handler = BusOffHandler::new();
        handler.on_bus_off();
        handler.on_bus_off();
        let action = handler.on_bus_off();
        assert_eq!(action, BusOffAction::EnterSafeState);
        assert_eq!(handler.state(), BusOffState::PermanentFault);
    }

    #[test]
    fn test_recovery_resets_counter() {
        // UT-CAN-BUSOFF-003
        let mut handler = BusOffHandler::new();
        handler.on_bus_off();
        handler.on_recovery_success();
        let action = handler.on_bus_off();
        // After reset, a new bus-off starts from attempt 1
        assert_eq!(action, BusOffAction::AllowRecovery { attempt: 1 });
    }

    #[test]
    fn test_permanent_fault_is_sticky() {
        // UT-CAN-BUSOFF-004
        let mut handler = BusOffHandler::new();
        handler.on_bus_off();
        handler.on_bus_off();
        handler.on_bus_off();
        // Additional bus-off events after permanent fault still return EnterSafeState
        let action = handler.on_bus_off();
        assert_eq!(action, BusOffAction::EnterSafeState);
    }
}
```

---

## Traceability and Documentation

### Bidirectional Traceability Matrix

IEC 62304 Class C mandates bidirectional traceability from user needs through system requirements, software requirements, design, code, and tests. For CAN software:

```
User Need (UN-003)
  └─ System Requirement (SYS-CAN-012): Detect node failure within 500 ms
       └─ Software Requirement (SRS-CAN-022): Monitor heartbeat; fault on 3 misses
            └─ Architecture Item (ARCH-CAN-003): HeartbeatMonitor software unit
                 └─ Detailed Design (DD-CAN-022): NodeState struct, tick() algorithm
                      └─ Code: heartbeat_monitor.rs / heartbeat_monitor.cpp
                           └─ Unit Test (UT-CAN-HB-001 through 005)
                                └─ Integration Test (IT-CAN-003)
                                     └─ System Test (ST-003)
                                          └─ Risk Control Verification (RCV-CAN-015)
```

Every artifact in this chain is version-controlled and change-controlled. If any element changes, the impact assessment must propagate upward and downward through the chain.

### Software of Unknown Provenance (SOUP)

Any third-party CAN library or COTS stack used in the device is classified as SOUP under IEC 62304. Required documentation for each SOUP item includes:

- Identification: name, version, publisher
- Functional and performance requirements placed on the SOUP
- Known anomaly list (bugs acknowledged and assessed)
- Regression test strategy if the SOUP is updated

---

## Testing and Verification

### Unit Testing

Each CAN software unit (driver, safety wrapper, heartbeat monitor, error handler) must have unit tests that are traceable to software requirements. The Rust examples above include embedded unit tests (`#[cfg(test)]` blocks) that run on the host via `cargo test`, providing fast verification without target hardware.

For C/C++, testing frameworks such as Unity, CppUTest, or GoogleTest are used with hardware abstraction stubs that replace physical CAN register access.

### Integration Testing

Integration tests verify that software units interact correctly across CAN bus boundaries. Typical test scenarios:

- Nominal message exchange between simulated nodes
- Single frame loss injection — verify sequence counter detection
- Node silence simulation — verify heartbeat monitor fault escalation
- Bus-off injection via test equipment — verify recovery state machine
- Babbling node simulation — verify non-affected nodes continue operation

### System-Level Testing (Verification and Validation)

System tests, conducted with the complete device, must verify all software requirements under worst-case conditions. For CAN, this includes testing at maximum bus load (≥ 70% utilization) to confirm safety message latency remains within specified bounds.

### Static Analysis and MISRA Compliance

For C/C++ code, static analysis tools (e.g., PC-lint Plus, Polyspace, Coverity, PRQA QA-C) must be run against the codebase with MISRA C:2012 rule sets. All deviations must be formally documented and justified. For Rust, the compiler itself eliminates many MISRA-equivalent concerns, but `clippy` with medical-device-appropriate lints and `cargo deny` for dependency auditing serve analogous roles.

---

## Summary

Deploying CAN in medical devices is a multidisciplinary endeavor that integrates bus engineering with formal regulatory compliance. The key principles of this chapter are:

**Regulatory grounding:** Every CAN software component must be classified under IEC 62304 (Class A, B, or C) based on its potential to cause patient harm. Class C components — those whose failure could lead to death or serious injury — demand the most rigorous lifecycle processes: full requirements traceability, formal architecture and detailed design documentation, comprehensive unit and integration testing, static analysis, and change control.

**Quality management as backbone:** ISO 13485 does not merely add paperwork — it creates the systematic controls (design inputs, design verification, supplier management, CM) that ensure the CAN system remains predictably safe across the product's entire commercial life, including after design changes and software updates.

**CAN-specific safety engineering:** Beyond the standards, effective medical CAN design requires explicit engineering choices: message ID allocation by clinical priority, application-level CRC and sequence counters beyond the hardware CRC, heartbeat-based node monitoring with safe-state escalation, and bounded bus-off recovery with a hard limit on automatic retries before a permanent fault is declared.

**Language and tooling:** C with MISRA C:2012 remains the dominant language for safety-critical embedded CAN software, supported by a mature ecosystem of certified compilers and static analysis tools. Rust is an increasingly viable alternative, particularly for Class B and C software, because its ownership model eliminates entire classes of memory safety defects at compile time — reducing the static analysis burden and producing inherently more auditable code.

**Traceability is non-negotiable:** The audit trail from user need to unit test is not optional bureaucracy. It is the mechanism by which a regulator (FDA, notified body) gains confidence that every safety requirement is implemented and verified. Gaps in traceability are one of the most common causes of regulatory submission rejection.

Building a CAN-based medical device that is safe, effective, and commercially viable requires treating the regulatory framework not as an obstacle, but as an engineering discipline in its own right — one that, when applied rigorously, produces systems that earn and maintain clinical trust.

---

*Document prepared in accordance with IEC 62304:2006+AMD1:2015 documentation requirements.*
*Revision: 1.0 | Classification: Technical Reference | Safety Class: Informational*