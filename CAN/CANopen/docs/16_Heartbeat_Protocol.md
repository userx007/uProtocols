# 16. CANopen Heartbeat Protocol

**Structure (12 sections):**

- **Overview & CAN Frame recap** – how the COB-ID is formed (`0x700 + Node-ID`) and what the single data byte means.
- **Heartbeat vs. Node Guarding** – an ASCII comparison table explaining why Heartbeat replaced the legacy polling approach.
- **Object Dictionary entries** – detailed layouts of **0x1017** (producer period) and **0x1016** (consumer table with the 32-bit entry format), including an SDO download example.
- **NMT State Machine** – full ASCII flow diagram covering Boot-Up → Pre-Operational → Operational → Stopped and all NMT transitions with command codes.
- **Monitoring multiple nodes** – ASCII timeline showing three nodes being watched simultaneously with individual countdown timers.
- **Timeout recovery strategy** – ASCII decision flowchart distinguishing safety-critical from non-critical nodes, NMT reset/retry logic, and escalation.
- **C/C++ examples** – five code sections: data structures, producer tick & Boot-Up send, consumer receive & timer reset, recovery handler with retry cap, and a full integration sketch wiring everything together.
- **Timing guidance** – recommended timeout multipliers and bus-load impact table.
- **Common pitfalls** – 7 frequent mistakes with their solutions.
- **Summary** – concise recap of the whole protocol.

---

## Table of Contents

1. [Overview](#1-overview)
2. [CAN Frame Basics Recap](#2-can-frame-basics-recap)
3. [Heartbeat vs. Node Guarding](#3-heartbeat-vs-node-guarding)
4. [Object Dictionary Entries](#4-object-dictionary-entries)
   - 4.1 [Producer Heartbeat Time – 0x1017](#41-producer-heartbeat-time--0x1017)
   - 4.2 [Consumer Heartbeat Table – 0x1016](#42-consumer-heartbeat-table--0x1016)
5. [Heartbeat Message Format](#5-heartbeat-message-format)
6. [Node States and the Heartbeat State Machine](#6-node-states-and-the-heartbeat-state-machine)
   - 6.1 [NMT State Codes](#61-nmt-state-codes)
   - 6.2 [State Machine Diagram](#62-state-machine-diagram)
   - 6.3 [Boot-Up Message](#63-boot-up-message)
7. [Monitoring Multiple Nodes](#7-monitoring-multiple-nodes)
8. [Timeout Recovery Strategy](#8-timeout-recovery-strategy)
9. [C/C++ Implementation Examples](#9-cc-implementation-examples)
   - 9.1 [Data Structures](#91-data-structures)
   - 9.2 [Producer: Sending Heartbeat](#92-producer-sending-heartbeat)
   - 9.3 [Consumer: Receiving and Monitoring Heartbeats](#93-consumer-receiving-and-monitoring-heartbeats)
   - 9.4 [Timeout Detection and Recovery](#94-timeout-detection-and-recovery)
   - 9.5 [Full Integration Example (C++)](#95-full-integration-example-c)
10. [Timing Considerations](#10-timing-considerations)
11. [Common Pitfalls](#11-common-pitfalls)
12. [Summary](#12-summary)

---

## 1. Overview

The **Heartbeat Protocol** is the primary network-level node monitoring mechanism in CANopen
(CiA 301). Each node on the bus periodically broadcasts a single-byte CAN message – the
*heartbeat* – that announces its current NMT (Network Management) state. Any other node
configured as a **consumer** can watch these messages and detect when a producer has gone
silent, allowing the system to react to a lost node before a higher-level application error
occurs.

Key responsibilities of the Heartbeat Protocol:

- Confirm that a node is alive and in the expected operational state.
- Detect communication loss within a bounded time window.
- Distinguish between different runtime states (Pre-Operational, Operational, Stopped).
- Signal the initial boot-up event after a reset.

---

## 2. CAN Frame Basics Recap

```
 CAN Frame (Standard 11-bit identifier)
 ┌──────────────┬───┬─────┬──────────────────────────┬─────┐
 │  COB-ID      │RTR│ DLC │        Data Bytes        │ CRC │
 │  (11 bits)   │   │(4b) │  0 – 8 bytes             │     │
 └──────────────┴───┴─────┴──────────────────────────┴─────┘

 Heartbeat COB-ID = 0x700 + Node-ID
 Example for Node 5:  COB-ID = 0x705
 DLC = 1 (one data byte)
 Data[0] = NMT state code
```

---

## 3. Heartbeat vs. Node Guarding

CANopen historically offered two monitoring mechanisms. Heartbeat is the modern approach
and the only one recommended for new designs:

```
 ┌─────────────────────┬──────────────────────────────┬──────────────────────────────┐
 │ Feature             │ Node Guarding (legacy)       │ Heartbeat (current)          │
 ├─────────────────────┼──────────────────────────────┼──────────────────────────────┤
 │ Direction           │ Master polls, node responds  │ Node broadcasts autonomously │
 │ CAN traffic         │ RTR + response = 2 frames    │ 1 frame per interval         │
 │ Master dependency   │ Requires NMT master polling  │ Any node can consume         │
 │ Toggle bit          │ Yes – must alternate         │ No                           │
 │ Boot-up signal      │ No                           │ Yes (state byte = 0x00)      │
 │ OD entries          │ 0x100C / 0x100D              │ 0x1016 / 0x1017              │
 │ Recommended         │ No (deprecated)              │ Yes                          │
 └─────────────────────┴──────────────────────────────┴──────────────────────────────┘
```

---

## 4. Object Dictionary Entries

### 4.1 Producer Heartbeat Time – 0x1017

This entry is written on the **producing node** to configure how often it sends its
heartbeat CAN frame.

```
 Object 0x1017 – Producer Heartbeat Time
 ┌─────────────────────────────────────────────────────────────────┐
 │  Index   │ 0x1017                                               │
 │  Sub-idx │ 0x00                                                 │
 │  Type    │ UNSIGNED16                                           │
 │  Unit    │ milliseconds                                         │
 │  Access  │ Read/Write                                           │
 │  Default │ 0x0000  (heartbeat disabled)                         │
 ├──────────────────────────────────────────────────────────────── │
 │  Value   │ Effect                                               │
 │  0x0000  │ Heartbeat producer disabled                          │
 │  0x0064  │ Send heartbeat every 100 ms                          │
 │  0x03E8  │ Send heartbeat every 1000 ms (1 s)                   │
 └─────────────────────────────────────────────────────────────────┘
```

Writing this value via SDO from a master:

```
 SDO Download – Write 0x1017 sub0 = 500 ms (0x01F4)
 ──────────────────────────────────────────────────
 Request  (master → node):
   COB-ID  = 0x600 + Node-ID
   Byte[0] = 0x2B  (initiate download, 2 bytes, expedited)
   Byte[1] = 0x17  (index low)
   Byte[2] = 0x10  (index high)
   Byte[3] = 0x00  (sub-index)
   Byte[4] = 0xF4  (data low)
   Byte[5] = 0x01  (data high)
   Byte[6] = 0x00
   Byte[7] = 0x00

 Response (node → master):
   COB-ID  = 0x580 + Node-ID
   Byte[0] = 0x60  (success)
   Bytes[1..7] = echo of index/sub
```

### 4.2 Consumer Heartbeat Table – 0x1016

This entry is configured on any node that wants to **monitor** other nodes' heartbeats.
It is an array where each sub-index describes one monitored node.

```
 Object 0x1016 – Consumer Heartbeat Time
 ┌────────────┬──────────┬──────────────────────────────────────────────────────────┐
 │ Sub-index  │ Type     │ Description                                              │
 ├────────────┼──────────┼──────────────────────────────────────────────────────────┤
 │ 0x00       │ UINT8    │ Number of entries (max supported consumers)              │
 │ 0x01..N    │ UINT32   │ Bits 31..24: reserved (0)                                │
 │            │          │ Bits 23..16: Node-ID of the producer to watch (1..127)   │
 │            │          │ Bits 15..0 : Consumer heartbeat timeout [ms]             │
 └────────────┴──────────┴──────────────────────────────────────────────────────────┘
```

Example layout for monitoring three nodes:

```
 0x1016 sub0 = 0x03  (3 entries)

 sub1 = 0x00_02_01_F4   →  Node-ID 2,  timeout 500 ms
        ─────────────
        Byte3 Byte2 Byte1:Byte0
              0x02  0x01F4 = 500

 sub2 = 0x00_05_03_E8   →  Node-ID 5,  timeout 1000 ms

 sub3 = 0x00_0A_07_D0   →  Node-ID 10, timeout 2000 ms
```

---

## 5. Heartbeat Message Format

```
 Heartbeat CAN Frame
 ┌────────────────────────────────────────────────────────────────────┐
 │  COB-ID  =  0x700 + Node-ID  (fixed, not configurable)             │
 │  DLC     =  1                                                      │
 │  Data[0] =  NMT State Code (see table in section 6.1)              │
 └────────────────────────────────────────────────────────────────────┘

 Example  – Node 7 is Operational:
   COB-ID  = 0x707
   DLC     = 1
   Data[0] = 0x05

 Example  – Node 7 sends Boot-Up:
   COB-ID  = 0x707
   DLC     = 1
   Data[0] = 0x00
```

---

## 6. Node States and the Heartbeat State Machine

### 6.1 NMT State Codes

```
 ┌──────────────────────┬───────────┬───────────────────────────────────────────────┐
 │ State                │ Code      │ Description                                   │
 ├──────────────────────┼───────────┼───────────────────────────────────────────────┤
 │ Boot-Up              │ 0x00      │ One-time message after power-on / reset       │
 │ Stopped              │ 0x04      │ Only NMT and heartbeat CAN traffic allowed    │
 │ Operational          │ 0x05      │ Full CANopen communication active             │
 │ Pre-Operational      │ 0x7F      │ SDO active; PDO communication disabled        │
 └──────────────────────┴───────────┴───────────────────────────────────────────────┘
```

### 6.2 State Machine Diagram

```
                          Power-On / Reset
                                │
                                ▼
                      ┌─────────────────┐
                      │  Initialisation │  (internal, no heartbeat yet)
                      │  (not visible)  │
                      └────────┬────────┘
                               │  Send Boot-Up message (0x00)
                               │
                               ▼
                    ┌──────────────────────┐
                    │    PRE-OPERATIONAL   │◄───────────────────────────────┐
                    │      (0x7F)          │                                │
                    └──────────────────────┘                                │
                      │               ▲                                     │
          NMT Start   │               │  NMT Enter Pre-Op                   │
          Remote Node │               │  (0x80)                             │
                      ▼               │                                     │
                    ┌──────────────────────┐                                │
                    │    OPERATIONAL       │                                │
                    │      (0x05)          │                                │
                    └──────────────────────┘                                │
                      │               ▲                                     │
          NMT Stop    │               │  NMT Start Remote Node              │
          Remote Node │               │  (from Stopped)                     │
          (0x02)      ▼               │                                     │
                    ┌──────────────────────┐                                │
                    │      STOPPED         │────────────────────────────────┘
                    │      (0x04)          │   NMT Enter Pre-Op (0x80)
                    └──────────────────────┘
                               │
                               │  NMT Reset Node (0x81) / Reset Comm (0x82)
                               ▼
                      ┌─────────────────┐
                      │  Initialisation │  (restart cycle)
                      └─────────────────┘

 NMT Command Codes:
   0x01 = Start Remote Node      → Operational
   0x02 = Stop Remote Node       → Stopped
   0x80 = Enter Pre-Operational  → Pre-Operational
   0x81 = Reset Node             → Initialisation (full)
   0x82 = Reset Communication    → Initialisation (comm layer only)
```

### 6.3 Boot-Up Message

The boot-up message is a special heartbeat with state byte `0x00`. It is transmitted
**once** when a node finishes its initialisation phase and enters Pre-Operational. A
consumer receiving `0x00` knows that the sender has just (re-)started and may need
re-configuration via SDO before being commanded to Operational state.

```
 Boot-Up detection on the bus:

 t=0    Node 3 power-on
        ...initialising...
 t=45ms  ──[0x703, DLC=1, Data=0x00]──►  (Boot-Up, entering Pre-Op)
 t=545ms ──[0x703, DLC=1, Data=0x7F]──►  (Heartbeat: Pre-Operational)
 t=1045ms──[0x703, DLC=1, Data=0x7F]──►  (Heartbeat: Pre-Operational)
        ...master sends SDO config...
        ...master sends NMT Start (0x01)...
 t=1600ms──[0x703, DLC=1, Data=0x05]──►  (Heartbeat: Operational)
```

---

## 7. Monitoring Multiple Nodes

A consumer node maintains a timer for every entry in its 0x1016 table. Each time a
heartbeat arrives from the expected producer, the corresponding timer is reset. If the
timer expires without a matching message, a **heartbeat event** (timeout) is triggered.

```
 Consumer monitoring three producers (Node 2, 5, 10):

 Time ──────────────────────────────────────────────────────────────────────────►
 ms     0    100  200  300  400  500  600  700  800  900  1000 1100 1200
        │    │    │    │    │    │    │    │    │    │    │    │    │

 Node2  ●────────────────────●────────────────────●────────────────────●  (500ms)
 Node5  ●──────────────────────────────────────────────────────────────● (1000ms)
 Node10 ●                                                          !!TIMEOUT!!
        ●= heartbeat received / reset timer            !! = timer expired

 Consumer timer state after each event:
 ┌────────┬────────────────────────────────────────────────────────────┐
 │ Node   │ Timer countdown (resets on each received heartbeat)        │
 ├────────┼────────────────────────────────────────────────────────────┤
 │  2     │ [500]──countdown──[0]●reset[500]──countdown──[0]●reset...  │
 │  5     │ [1000]───────────────────────────countdown───────[0]●reset │
 │  10    │ [2000]──────────────────────────────────────────────[0]!!  │
 └────────┴────────────────────────────────────────────────────────────┘
```

---

## 8. Timeout Recovery Strategy

When a heartbeat consumer detects a timeout, the application must decide on a recovery
action. The appropriate strategy depends on the system's safety requirements:

```
 Timeout Detected for Node N
           │
           ▼
   ┌───────────────────┐
   │  Log / notify     │   Record the event with timestamp and node ID
   │  application      │
   └────────┬──────────┘
            │
            ▼
   ┌───────────────────┐
   │  Classify node    │   Is it safety-critical?
   │  criticality      │   Is it a sensor, actuator, master, or info-only node?
   └────────┬──────────┘
            │
     ┌──────┴──────────────────────────┐
     │                                 │
     ▼                                 ▼
 ┌──────────────┐               ┌──────────────────┐
 │  Safety-     │               │  Non-critical    │
 │  Critical    │               │  node            │
 └──────┬───────┘               └──────┬───────────┘
        │                              │
        ▼                              ▼
 ┌──────────────┐               ┌──────────────────┐
 │  Enter safe  │               │  Retry: send     │
 │  state /     │               │  NMT Reset Node  │
 │  emergency   │               │  (0x81) to N     │
 │  stop        │               └──────┬───────────┘
 └──────────────┘                      │
                                       ▼
                                ┌──────────────────┐
                                │  Wait for        │
                                │  Boot-Up (0x00)  │
                                │  from node N     │
                                └──────┬───────────┘
                                       │
                          ┌────────────┴───────────────┐
                          │                            │
                          ▼                            ▼
                   ┌──────────────┐           ┌──────────────┐
                   │  Boot-Up     │           │  No Boot-Up  │
                   │  received    │           │  within      │
                   │              │           │  deadline    │
                   └──────┬───────┘           └──────┬───────┘
                          │                          │
                          ▼                          ▼
                   ┌──────────────┐           ┌──────────────┐
                   │  Re-config   │           │  Mark node   │
                   │  via SDO     │           │  as FAILED;  │
                   │  then NMT    │           │  escalate    │
                   │  Start (0x01)│           │  alarm       │
                   └──────────────┘           └──────────────┘
```

---

## 9. C/C++ Implementation Examples

### 9.1 Data Structures

```cpp
// canopen_heartbeat.h
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>

// Maximum number of nodes we can monitor simultaneously
static constexpr uint8_t  HEARTBEAT_MAX_CONSUMERS = 16;

// NMT state codes carried in the heartbeat data byte
enum class NmtState : uint8_t {
    BootUp         = 0x00,
    Stopped        = 0x04,
    Operational    = 0x05,
    PreOperational = 0x7F,
    Unknown        = 0xFF   // local placeholder – never transmitted
};

// One entry in the consumer table (mirrors Object 0x1016 sub-entry)
struct HeartbeatConsumerEntry {
    uint8_t  nodeId;            // Producer Node-ID to watch (1..127), 0 = unused
    uint32_t timeoutMs;         // Configured timeout in milliseconds
    uint32_t timerRemaining;    // Countdown, decremented by tick function
    NmtState lastState;         // Last observed NMT state
    bool     timedOut;          // Set when timerRemaining reaches 0
    bool     bootUpPending;     // Set after receiving BootUp, cleared after re-config
};

// Heartbeat manager state
struct HeartbeatManager {
    // Producer side
    uint8_t  localNodeId;
    uint32_t producerPeriodMs;  // Object 0x1017
    uint32_t producerTimer;     // Counts down to next transmission
    NmtState localState;

    // Consumer side (Object 0x1016 table)
    HeartbeatConsumerEntry consumers[HEARTBEAT_MAX_CONSUMERS];
    uint8_t  consumerCount;

    // Callback invoked on timeout or state change
    std::function<void(uint8_t nodeId, NmtState state, bool timeout)> eventCallback;
};
```

### 9.2 Producer: Sending Heartbeat

```cpp
// canopen_heartbeat_producer.cpp
#include "canopen_heartbeat.h"
#include "can_driver.h"       // platform-specific CAN send

// COB-ID base for heartbeat
static constexpr uint16_t HEARTBEAT_COB_BASE = 0x700U;

/**
 * @brief  Transmit one heartbeat CAN frame.
 *
 * Called internally whenever the producer timer expires.
 */
static void heartbeat_send(const HeartbeatManager& mgr)
{
    CanFrame frame;
    frame.id   = static_cast<uint32_t>(HEARTBEAT_COB_BASE + mgr.localNodeId);
    frame.dlc  = 1U;
    frame.data[0] = static_cast<uint8_t>(mgr.localState);
    frame.flags   = 0U;   // standard frame, no RTR

    can_transmit(&frame);
}

/**
 * @brief  Called once during node initialisation to send the Boot-Up message
 *         and configure the producer timer.
 *
 * @param  mgr         Heartbeat manager instance.
 * @param  nodeId      This node's Node-ID (1..127).
 * @param  periodMs    Heartbeat period from Object 0x1017 (0 = disabled).
 */
void heartbeat_producer_init(HeartbeatManager& mgr, uint8_t nodeId, uint32_t periodMs)
{
    mgr.localNodeId      = nodeId;
    mgr.producerPeriodMs = periodMs;
    mgr.producerTimer    = periodMs;
    mgr.localState       = NmtState::PreOperational;

    // Send the one-time Boot-Up message (state byte = 0x00)
    NmtState saved   = mgr.localState;
    mgr.localState   = NmtState::BootUp;
    heartbeat_send(mgr);
    mgr.localState   = saved;   // restore to Pre-Operational for ongoing heartbeats
}

/**
 * @brief  Call this every 1 ms (or adjust delta accordingly) to drive the
 *         producer timer.
 *
 * @param  mgr      Heartbeat manager instance.
 * @param  deltaMs  Milliseconds elapsed since last call.
 */
void heartbeat_producer_tick(HeartbeatManager& mgr, uint32_t deltaMs)
{
    if (mgr.producerPeriodMs == 0U) {
        return;  // producer disabled
    }

    if (mgr.producerTimer > deltaMs) {
        mgr.producerTimer -= deltaMs;
    } else {
        mgr.producerTimer = mgr.producerPeriodMs;   // reload
        heartbeat_send(mgr);
    }
}

/**
 * @brief  Update the local NMT state (called by NMT state machine).
 */
void heartbeat_set_local_state(HeartbeatManager& mgr, NmtState newState)
{
    mgr.localState = newState;
}
```

### 9.3 Consumer: Receiving and Monitoring Heartbeats

```cpp
// canopen_heartbeat_consumer.cpp
#include "canopen_heartbeat.h"

/**
 * @brief  Add a node to the consumer monitoring table.
 *
 * Mirrors writing a value into Object 0x1016 sub1..subN.
 * The 32-bit value format:  bits[23:16] = Node-ID, bits[15:0] = timeout_ms
 *
 * @param  mgr        Heartbeat manager instance.
 * @param  od_value   Raw 32-bit value from Object 0x1016 sub-entry.
 * @return true if entry was added, false if table is full.
 */
bool heartbeat_consumer_add(HeartbeatManager& mgr, uint32_t od_value)
{
    if (mgr.consumerCount >= HEARTBEAT_MAX_CONSUMERS) {
        return false;
    }

    uint8_t  nodeId    = static_cast<uint8_t>((od_value >> 16U) & 0x7FU);
    uint32_t timeoutMs = static_cast<uint32_t>(od_value & 0xFFFFU);

    if (nodeId == 0U || timeoutMs == 0U) {
        return false;  // disabled entry
    }

    HeartbeatConsumerEntry& e = mgr.consumers[mgr.consumerCount++];
    e.nodeId         = nodeId;
    e.timeoutMs      = timeoutMs;
    e.timerRemaining = timeoutMs;
    e.lastState      = NmtState::Unknown;
    e.timedOut       = false;
    e.bootUpPending  = false;

    return true;
}

/**
 * @brief  Called when a CAN frame with COB-ID in range 0x701..0x77F is received.
 *
 * Parses the heartbeat, resets the consumer timer for the sending node, and
 * fires the event callback on state changes or boot-up detection.
 *
 * @param  mgr      Heartbeat manager instance.
 * @param  cobId    COB-ID of the received frame.
 * @param  data     Pointer to data bytes (at least 1 byte).
 * @param  dlc      Data length code (must be 1 for a valid heartbeat).
 */
void heartbeat_consumer_on_receive(HeartbeatManager& mgr,
                                   uint32_t cobId,
                                   const uint8_t* data,
                                   uint8_t dlc)
{
    if (dlc < 1U) {
        return;  // malformed frame
    }

    // Extract sender Node-ID from COB-ID
    uint8_t senderNodeId = static_cast<uint8_t>(cobId - 0x700U);
    if (senderNodeId == 0U || senderNodeId > 127U) {
        return;
    }

    NmtState receivedState = static_cast<NmtState>(data[0]);

    // Find the matching consumer entry
    for (uint8_t i = 0U; i < mgr.consumerCount; ++i) {
        HeartbeatConsumerEntry& e = mgr.consumers[i];
        if (e.nodeId != senderNodeId) {
            continue;
        }

        // Reset the timeout timer
        e.timerRemaining = e.timeoutMs;
        e.timedOut       = false;

        // Detect boot-up event
        if (receivedState == NmtState::BootUp) {
            e.bootUpPending = true;
            e.lastState     = NmtState::PreOperational;  // node enters Pre-Op after boot
            if (mgr.eventCallback) {
                mgr.eventCallback(senderNodeId, NmtState::BootUp, false);
            }
            return;
        }

        // Notify on state change
        if (receivedState != e.lastState) {
            e.lastState = receivedState;
            if (mgr.eventCallback) {
                mgr.eventCallback(senderNodeId, receivedState, false);
            }
        }

        return;
    }
    // Frame from a node we are not monitoring – silently ignore
}

/**
 * @brief  Periodic tick for the consumer side. Decrements all timers and
 *         fires timeout callbacks when a timer reaches zero.
 *
 * @param  mgr      Heartbeat manager instance.
 * @param  deltaMs  Milliseconds elapsed since last call.
 */
void heartbeat_consumer_tick(HeartbeatManager& mgr, uint32_t deltaMs)
{
    for (uint8_t i = 0U; i < mgr.consumerCount; ++i) {
        HeartbeatConsumerEntry& e = mgr.consumers[i];

        if (e.timedOut) {
            continue;  // already flagged; waiting for recovery
        }

        if (e.timerRemaining > deltaMs) {
            e.timerRemaining -= deltaMs;
        } else {
            e.timerRemaining = 0U;
            e.timedOut       = true;
            if (mgr.eventCallback) {
                mgr.eventCallback(e.nodeId, e.lastState, /*timeout=*/true);
            }
        }
    }
}
```

### 9.4 Timeout Detection and Recovery

```cpp
// canopen_recovery.cpp
#include "canopen_heartbeat.h"
#include "canopen_nmt.h"     // nmt_send_command()
#include "app_safety.h"      // app_enter_safe_state()
#include <cstdio>

// Application-defined criticality table
struct NodeConfig {
    uint8_t nodeId;
    bool    isSafetyCritical;
    uint32_t bootUpWaitMs;  // how long to wait for boot-up after reset
};

static const NodeConfig NODE_TABLE[] = {
    { 2,  true,  3000U },   // Safety-critical drive controller
    { 5,  false, 2000U },   // Non-critical sensor
    { 10, false, 2000U },   // Non-critical HMI
};
static const uint8_t NODE_TABLE_COUNT =
    static_cast<uint8_t>(sizeof(NODE_TABLE) / sizeof(NODE_TABLE[0]));

// Per-recovery state
struct RecoveryState {
    uint8_t  nodeId;
    bool     resetSent;
    uint32_t bootUpDeadline;    // absolute ms timestamp
    uint8_t  retryCount;
};
static RecoveryState s_recovery[HEARTBEAT_MAX_CONSUMERS];

static uint32_t get_ms_now(void);  // implement with RTOS tick or HAL_GetTick()

/**
 * @brief  Heartbeat event handler – wired into mgr.eventCallback.
 */
void on_heartbeat_event(uint8_t nodeId, NmtState state, bool timeout)
{
    if (!timeout) {
        // State change or boot-up notification
        if (state == NmtState::BootUp) {
            printf("[HB] Node %u rebooted – will re-configure\n", nodeId);
            // Application-level re-configuration via SDO should be triggered here
        } else {
            printf("[HB] Node %u state changed -> 0x%02X\n", nodeId,
                   static_cast<uint8_t>(state));
        }
        // Clear any recovery state for this node
        for (auto& r : s_recovery) {
            if (r.nodeId == nodeId) {
                r.resetSent  = false;
                r.retryCount = 0U;
                break;
            }
        }
        return;
    }

    // --- Timeout path ---
    printf("[HB] TIMEOUT: node %u (last state 0x%02X)\n",
           nodeId, static_cast<uint8_t>(state));

    // Find node configuration
    const NodeConfig* cfg = nullptr;
    for (uint8_t i = 0U; i < NODE_TABLE_COUNT; ++i) {
        if (NODE_TABLE[i].nodeId == nodeId) {
            cfg = &NODE_TABLE[i];
            break;
        }
    }

    if (cfg == nullptr || cfg->isSafetyCritical) {
        // Unknown node or safety-critical: enter safe state immediately
        printf("[HB] Safety-critical timeout – entering safe state!\n");
        app_enter_safe_state();
        return;
    }

    // Non-critical: attempt reset recovery
    for (auto& r : s_recovery) {
        if (r.nodeId == nodeId || r.nodeId == 0U) {
            if (r.nodeId == 0U) {
                r.nodeId     = nodeId;
                r.retryCount = 0U;
            }
            if (r.retryCount >= 3U) {
                printf("[HB] Node %u: max retries exceeded – marking FAILED\n", nodeId);
                // Escalate to application alarm system
                return;
            }

            printf("[HB] Node %u: sending NMT Reset (attempt %u)\n",
                   nodeId, r.retryCount + 1U);
            nmt_send_command(0x81U, nodeId);  // NMT Reset Node
            r.resetSent      = true;
            r.bootUpDeadline = get_ms_now() + cfg->bootUpWaitMs;
            r.retryCount++;
            break;
        }
    }
}

/**
 * @brief  Call periodically to check boot-up deadlines after recovery resets.
 */
void recovery_tick(HeartbeatManager& mgr)
{
    uint32_t now = get_ms_now();

    for (auto& r : s_recovery) {
        if (!r.resetSent || r.nodeId == 0U) {
            continue;
        }
        if (now >= r.bootUpDeadline) {
            printf("[HB] Node %u did not respond to reset within deadline\n", r.nodeId);
            r.resetSent = false;
            // Trigger on_heartbeat_event again as a fresh timeout to retry or escalate
            on_heartbeat_event(r.nodeId, NmtState::Unknown, /*timeout=*/true);
        }
    }
}
```

### 9.5 Full Integration Example (C++)

```cpp
// main_canopen.cpp  –  Minimal integration sketch
#include "canopen_heartbeat.h"
#include "canopen_heartbeat_producer.cpp"    // inline for brevity
#include "canopen_heartbeat_consumer.cpp"
#include "canopen_recovery.cpp"
#include "can_driver.h"
#include "rtos.h"

static HeartbeatManager g_hbMgr{};

// -----------------------------------------------------------------------
// Initialisation  (called once at startup)
// -----------------------------------------------------------------------
void canopen_init(void)
{
    // Wire up the event callback
    g_hbMgr.eventCallback = on_heartbeat_event;

    // Producer: this node (ID=1) sends heartbeat every 500 ms (Object 0x1017)
    heartbeat_producer_init(g_hbMgr, /*nodeId=*/1U, /*periodMs=*/500U);

    // Consumer table (Object 0x1016): monitor three nodes
    //   Format: (Node-ID << 16) | timeout_ms
    heartbeat_consumer_add(g_hbMgr, (2U  << 16U) | 1500U);  // Node 2, 1500 ms timeout
    heartbeat_consumer_add(g_hbMgr, (5U  << 16U) | 3000U);  // Node 5, 3000 ms timeout
    heartbeat_consumer_add(g_hbMgr, (10U << 16U) | 4000U);  // Node 10, 4000 ms timeout
}

// -----------------------------------------------------------------------
// CAN receive ISR or task (called for every received frame)
// -----------------------------------------------------------------------
void canopen_on_frame_received(uint32_t cobId, const uint8_t* data, uint8_t dlc)
{
    // Route heartbeat frames (COB-ID 0x701..0x77F) to heartbeat consumer
    if (cobId >= 0x701U && cobId <= 0x77FU) {
        heartbeat_consumer_on_receive(g_hbMgr, cobId, data, dlc);
    }
    // ... other CANopen message routing (SDO, PDO, EMCY, NMT) ...
}

// -----------------------------------------------------------------------
// 1 ms periodic timer task
// -----------------------------------------------------------------------
void canopen_1ms_tick(void)
{
    constexpr uint32_t DELTA_MS = 1U;
    heartbeat_producer_tick(g_hbMgr, DELTA_MS);
    heartbeat_consumer_tick(g_hbMgr, DELTA_MS);
    recovery_tick(g_hbMgr);
}
```

---

## 10. Timing Considerations

```
 Recommended timeout margin:
 ─────────────────────────────────────────────────────────────────────
  timeout_ms  ≥  producer_period_ms × 1.5   (minimum for low jitter)
  timeout_ms  ≥  producer_period_ms × 2.0   (recommended for typical systems)
  timeout_ms  ≥  producer_period_ms × 3.0   (high-jitter or loaded networks)
 ─────────────────────────────────────────────────────────────────────

 Example:  producer_period = 500 ms  →  timeout ≥ 1000 ms (recommended 1500 ms)

 Jitter sources:
  - RTOS scheduling latency (typically 0.1..10 ms)
  - CAN bus arbitration and retransmission
  - Interrupt latency on receiving node
  - Clock drift between nodes (usually < 0.1%)

 Bus load impact on 1 Mbit/s CAN:
 ┌───────────────────┬────────────────┬────────────────────────────────┐
 │ Number of nodes   │ HB period (ms) │ Additional bus load            │
 ├───────────────────┼────────────────┼────────────────────────────────┤
 │ 10                │ 100            │ ~0.9% (10 frames × 111 µs)     │
 │ 50                │ 100            │ ~4.4%                          │
 │ 127               │ 100            │ ~11%  (approaching guideline)  │
 │ 127               │ 500            │ ~2.2% (comfortable)            │
 └───────────────────┴────────────────┴────────────────────────────────┘
 (Each heartbeat frame: 1 data byte + CAN overhead ≈ 111 µs on 1 Mbit/s)
```

---

## 11. Common Pitfalls

```
 ┌────┬─────────────────────────────┬────────────────────────────────────────────────┐
 │ #  │ Pitfall                     │ Solution                                       │
 ├────┼─────────────────────────────┼────────────────────────────────────────────────┤
 │ 1  │ Timeout = producer period   │ Always use a multiple (≥1.5×) of producer      │
 │    │ (false timeouts)            │ period for the consumer timeout                │
 ├────┼─────────────────────────────┼────────────────────────────────────────────────┤
 │ 2  │ Missing Boot-Up handler     │ Always handle state = 0x00; node needs SDO     │
 │    │                             │ re-configuration before returning to Oper.     │
 ├────┼─────────────────────────────┼────────────────────────────────────────────────┤
 │ 3  │ Consumer not started until  │ Start consumer timer only after first          │
 │    │ first message received      │ heartbeat is received, not at power-on, to     │
 │    │                             │ avoid false alarms during network startup      │
 ├────┼─────────────────────────────┼────────────────────────────────────────────────┤
 │ 4  │ Same COB-ID from two nodes  │ Ensure each node has a unique Node-ID;         │
 │    │ (Node-ID conflict)          │ use LSS protocol to assign IDs                 │
 ├────┼─────────────────────────────┼────────────────────────────────────────────────┤
 │ 5  │ Sending heartbeat in        │ Heartbeat must be sent in ALL states including │
 │    │ Operational only            │ Pre-Operational and Stopped                    │
 ├────┼─────────────────────────────┼────────────────────────────────────────────────┤
 │ 6  │ Aggressive reset loops      │ Cap retry count; escalate to application       │
 │    │ on non-responding node      │ alarm after N retries                          │
 ├────┼─────────────────────────────┼────────────────────────────────────────────────┤
 │ 7  │ Tick function called        │ Pass actual elapsed time (deltaMs) rather than │
 │    │ with variable interval      │ assuming fixed 1 ms; use hardware timer        │
 └────┴─────────────────────────────┴────────────────────────────────────────────────┘
```

---

## 12. Summary

The **CANopen Heartbeat Protocol** is a lightweight but essential mechanism for network
health monitoring on a CANopen bus.

**How it works in brief:**

Each node configured as a *producer* periodically broadcasts a single-byte CAN frame at
COB-ID `0x700 + Node-ID`. The data byte encodes the node's current NMT state:
`0x00` (Boot-Up), `0x04` (Stopped), `0x05` (Operational), or `0x7F` (Pre-Operational).
The producer period is set via Object **0x1017** (Producer Heartbeat Time, in ms).

Any node configured as a *consumer* maintains a watchdog timer for each producer it
watches, as configured in Object **0x1016** (Consumer Heartbeat Table). Each table entry
encodes both the producer's Node-ID and the allowed timeout window. When a heartbeat
arrives, the corresponding timer is reset. If no heartbeat arrives before the timer
expires, a timeout event is raised.

**The heartbeat state machine** mirrors the NMT state machine. On power-on or reset, a
node sends a one-time Boot-Up message (`0x00`), then transitions to Pre-Operational
(`0x7F`). From there the NMT master can move it to Operational (`0x05`) or Stopped
(`0x04`), and back, with every state visible to all consumers on the bus.

**Recovery strategy** should be tiered: safety-critical node timeouts trigger an
immediate safe state, while non-critical nodes may be reset via an NMT Reset Node
command (code `0x81`) and re-configured via SDO once their Boot-Up message is received.
Retry counts must be bounded to avoid infinite reset loops.

**Key implementation points:**

- Consumer timeout should be at least **1.5× the producer period**; 2× is recommended.
- Start consumer timers only after the **first heartbeat is received** to avoid false
  alarms during network bring-up.
- Always handle the Boot-Up message (`0x00`) as a trigger for **SDO re-configuration**.
- Heartbeat must be transmitted in **all NMT states** – not just Operational.
- Prefer Heartbeat over the deprecated Node Guarding mechanism in all new designs.

---

*Reference: CiA 301 – CANopen application layer and communication profile, Version 4.2.0*