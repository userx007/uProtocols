# 05 — NMT State Machine & Network Management

## Table of Contents

1. [Overview](#1-overview)
2. [NMT State Machine](#2-nmt-state-machine)
   - 2.1 [States in Detail](#21-states-in-detail)
   - 2.2 [State Transition Table](#22-state-transition-table)
3. [NMT Protocol on the CAN Bus](#3-nmt-protocol-on-the-can-bus)
   - 3.1 [NMT Master Command Frame](#31-nmt-master-command-frame)
   - 3.2 [Boot-Up Message](#32-boot-up-message)
   - 3.3 [NMT Command Codes](#33-nmt-command-codes)
4. [NMT Master Commands — Deep Dive](#4-nmt-master-commands--deep-dive)
   - 4.1 [Start Node (Operational)](#41-start-node-operational)
   - 4.2 [Stop Node](#42-stop-node)
   - 4.3 [Pre-Operational](#43-pre-operational)
   - 4.4 [Reset Node](#44-reset-node)
   - 4.5 [Reset Communication](#45-reset-communication)
   - 4.6 [Broadcast vs Addressed](#46-broadcast-vs-addressed)
5. [Boot-Up Sequence](#5-boot-up-sequence)
6. [Services Available per State](#6-services-available-per-state)
7. [NMT Slave Implementation in C](#7-nmt-slave-implementation-in-c)
   - 7.1 [Data Structures](#71-data-structures)
   - 7.2 [Boot-Up Transmission](#72-boot-up-transmission)
   - 7.3 [NMT Command Processing](#73-nmt-command-processing)
   - 7.4 [State Machine Integration Loop](#74-state-machine-integration-loop)
   - 7.5 [Complete Minimal NMT Slave (C)](#75-complete-minimal-nmt-slave-c)
8. [NMT Master Implementation in C++](#8-nmt-master-implementation-in-c)
9. [Error Handling & Node Guarding / Heartbeat Interaction](#9-error-handling--node-guarding--heartbeat-interaction)
10. [Timing Diagram](#10-timing-diagram)
11. [Common Pitfalls](#11-common-pitfalls)
12. [Summary](#12-summary)

---

## 1. Overview

**NMT (Network Management)** is the supervisory layer of the CANopen protocol defined in
CiA 301. It controls the lifecycle of every device (node) on the bus through a strict state
machine. One node acts as the **NMT Master** — typically the PLC or embedded controller —
while all other nodes act as **NMT Slaves**.

Key responsibilities of NMT:

- Defining the operational state of each slave node
- Providing a standardised boot-up procedure
- Allowing the master to start, stop, or reset individual nodes or the entire network
- Enabling network-wide synchronisation before process data exchange begins

NMT messages use **CAN ID 0x000** for master→slave commands and
**CAN ID 0x700 + NodeID** for the slave boot-up and heartbeat responses.

---

## 2. NMT State Machine

Every CANopen node implements the following state machine from the moment power is applied.

### 2.1 States in Detail

```
                          Power-on / Hardware Reset
                                    |
                                    v
                        +-----------+-----------+
                        |                       |
                        |    INITIALISATION     |  <-- Internal only; no CAN traffic
                        |   (auto-entry state)  |      Object Dictionary loaded
                        |                       |      Communication objects reset
                        +-----------+-----------+
                                    |
                          Auto-transition on completion
                          Sends Boot-Up message (0x700+ID, data=0x00)
                                    |
                                    v
                        +-----------+-----------+
                        |                       |
                 +------>   PRE-OPERATIONAL     <------+
                 |      |                       |      |
                 |      |  SDO active           |      |
                 |      |  NMT active           |      |
                 |      |  SYNC/TIME active     |      |
                 |      |  PDO *inactive*       |      |
                 |      |  EMCY active          |      |
                 |      +-----------+-----------+      |
                 |                  |                  |
       [Stop Node]       [Start Node]        [Pre-Operational]
       [Reset Node]                |         (from Operational
       [Reset Comm]                v          or Stopped)
                 |      +-----------+-----------+
                 |      |                       |
                 +------+     OPERATIONAL       +-------+
                 |      |                       |       |
                 |      |  ALL services active  |       |
                 |      |  PDO active           |       |
                 |      |  SDO active           |       |
                 |      |  NMT active           |       |
                 |      +-----------+-----------+       |
                 |                  |                   |
                 |           [Stop Node]         [Reset Node]
                 |           [Reset Node]        [Reset Comm]
                 |           [Reset Comm]                |
                 |                  v                    |
                 |      +-----------+-----------+        |
                 |      |                       |        |
                 +------+       STOPPED         +--------+
                        |                       |
                        |  NMT active only      |
                        |  (heartbeat still     |
                        |   produced)           |
                        +-----------------------+
```

> **Note:** The INITIALISATION state is never visible externally — the first observable
> event is the **boot-up message** transmitted on exit from INITIALISATION.

### 2.2 State Transition Table

```
+--------------------+---------------------+----------------------------+------------------+
| Current State      | NMT Command         | Next State                 | Boot-Up sent?    |
+--------------------+---------------------+----------------------------+------------------+
| INITIALISATION     | (auto, internal)    | PRE-OPERATIONAL            | YES (0x00)       |
| PRE-OPERATIONAL    | Start Node (0x01)   | OPERATIONAL                | no               |
| PRE-OPERATIONAL    | Stop Node  (0x02)   | STOPPED                    | no               |
| PRE-OPERATIONAL    | Reset Node (0x81)   | INITIALISATION             | YES (on re-entry)|
| PRE-OPERATIONAL    | Reset Comm (0x82)   | INITIALISATION (comm only) | YES (on re-entry)|
| OPERATIONAL        | Stop Node  (0x02)   | STOPPED                    | no               |
| OPERATIONAL        | Pre-Op     (0x80)   | PRE-OPERATIONAL            | no               |
| OPERATIONAL        | Reset Node (0x81)   | INITIALISATION             | YES (on re-entry)|
| OPERATIONAL        | Reset Comm (0x82)   | INITIALISATION (comm only) | YES (on re-entry)|
| STOPPED            | Start Node (0x01)   | OPERATIONAL                | no               |
| STOPPED            | Pre-Op     (0x80)   | PRE-OPERATIONAL            | no               |
| STOPPED            | Reset Node (0x81)   | INITIALISATION             | YES (on re-entry)|
| STOPPED            | Reset Comm (0x82)   | INITIALISATION (comm only) | YES (on re-entry)|
+--------------------+---------------------+----------------------------+------------------+
```

---

## 3. NMT Protocol on the CAN Bus

### 3.1 NMT Master Command Frame

NMT master→slave commands always use **CAN-ID 0x000** with exactly **2 data bytes**:

```
 CAN Frame Layout
 +-----------+-----+----------------------------------------------------------+
 | Field     | Len | Description                                              |
 +-----------+-----+----------------------------------------------------------+
 | CAN ID    | 11b | Always 0x000  (highest priority on the bus)              |
 | DLC       |  4b | Always 2                                                 |
 | Byte 0    |  8b | cs  — Command Specifier (see table §3.3)                 |
 | Byte 1    |  8b | NodeID  0x00 = broadcast all nodes, 1..127 = addressed   |
 +-----------+-----+----------------------------------------------------------+

 Example — "Start Node 3":
 +------+---+------+--------+
 | 0x000| 2 | 0x01 |  0x03  |
 +------+---+------+--------+
   COB   DLC  cs    NodeID
```

### 3.2 Boot-Up Message

When a node completes initialisation and enters PRE-OPERATIONAL, it **must** transmit a
single CAN frame known as the **boot-up message**:

```
 Boot-Up Frame
 +--------------------+----------------+--------------------------------------------------+
 | Field              | Val            | Notes                                            |
 +--------------------+----------------+--------------------------------------------------+
 | CAN ID             | 0x700 + NodeID |  e.g. Node 5 → 0x705                             |
 | DLC                |  1             | One data byte                                    |
 | Byte 0             | 0x00           | Fixed — distinguishes boot-up from heartbeat     |
 +--------------------+----------------+--------------------------------------------------+

 Node 5 boot-up on CAN bus:
 ID=0x705  DLC=1  Data: 00
               ^                  ^
               |                  +-- State = 0x00 (Initialisation/boot-up)
               +-- 0x700 + 5
```

The heartbeat protocol uses the same COB-ID (0x700+NodeID) but with the **current NMT
state** encoded in byte 0 (0x04 = STOPPED, 0x05 = OPERATIONAL, 0x7F = PRE-OPERATIONAL).
A value of **0x00 uniquely identifies boot-up**.

### 3.3 NMT Command Codes

```
+--------+--------------------+---------------------------------------------------+
| cs     | Command            | Description                                       |
+--------+--------------------+---------------------------------------------------+
| 0x01   | Start Node         | PRE-OPERATIONAL / STOPPED  →  OPERATIONAL         |
| 0x02   | Stop Node          | PRE-OPERATIONAL / OPERATIONAL  →  STOPPED         |
| 0x80   | Enter Pre-Op       | OPERATIONAL / STOPPED  →  PRE-OPERATIONAL         |
| 0x81   | Reset Node         | Any state  →  INITIALISATION (full reset)         |
| 0x82   | Reset Communication| Any state  →  INITIALISATION (comm objects only)  |
+--------+--------------------+---------------------------------------------------+
```

---

## 4. NMT Master Commands — Deep Dive

### 4.1 Start Node (Operational)

Activates **PDO communication**. The node begins transmitting and accepting process data.
Should only be issued after the master has confirmed correct configuration via SDO.

```
 Master  ──[0x000, 01 00]──────────────────────────────> All Slaves
         or
 Master  ──[0x000, 01 05]──────────────────────────────> Node 5 only
                      ^^
                      cs=01, NodeID=05
```

### 4.2 Stop Node

Deactivates all communication except NMT and heartbeat. Useful for temporarily freezing
a sensor during re-configuration without a full reset.

### 4.3 Pre-Operational

Returns node to a configuration-safe state where SDOs are active but PDOs are silent.
Typically used during runtime re-configuration.

### 4.4 Reset Node

Triggers a **complete software reset**: the application and communication parameters are
re-loaded from non-volatile storage (Object Dictionary defaults / stored values in index
0x1010). The node re-runs full initialisation and broadcasts a new boot-up message.

### 4.5 Reset Communication

Resets only the **communication parameter objects** (indices 0x1000–0x1FFF).
Application objects (0x2000–0x9FFF) and manufacturer-specific areas are unaffected.
Faster than a full Reset Node.

### 4.6 Broadcast vs Addressed

```
 NodeID byte = 0x00  →  command applies to ALL nodes simultaneously
 NodeID byte = N     →  command applies only to node N (1..127)

 Example: Stop all nodes
 +------+---+------+--------+
 | 0x000| 2 | 0x02 |  0x00  |
 +------+---+------+--------+

 Example: Reset communication on node 7 only
 +------+---+------+--------+
 | 0x000| 2 | 0x82 |  0x07  |
 +------+---+------+--------+
```

---

## 5. Boot-Up Sequence

The following shows the typical network startup sequence coordinated by a master:

```
 Time
  |
  |  [All nodes power on simultaneously]
  |
  |  Node 1  ──(boot-up 0x701, data=0x00)──────────────────────────> Bus
  |  Node 2  ──(boot-up 0x702, data=0x00)──────────────────────────> Bus
  |  Node 5  ──(boot-up 0x705, data=0x00)──────────────────────────> Bus
  |
  |  [Master detects all expected nodes via boot-up messages]
  |
  |  Master  ──[SDO config writes to Node 1, 2, 5 if needed]──────> Each Node
  |
  |  Master  ──[0x000, 01 00]  (Start all nodes)───────────────────> Bus
  |
  |  [All nodes transition to OPERATIONAL]
  |  [PDOs begin flowing]
  |
  |  Node 1  ──(TPDO 0x181, process data)──────────────────────────> Bus
  |  Node 2  ──(TPDO 0x182, process data)──────────────────────────> Bus
  |
  v
```

If the master needs to reconfigure Node 2 at runtime:

```
  |  Master  ──[0x000, 80 02]  (Pre-Op Node 2)─────────────────────> Bus
  |  [Node 2 PDOs go silent; SDOs still active]
  |
  |  Master  ──[SDO write to Node 2]───────────────────────────────> Node 2
  |
  |  Master  ──[0x000, 01 02]  (Start Node 2)──────────────────────> Bus
  |  [Node 2 resumes OPERATIONAL]
  v
```

---

## 6. Services Available per State

```
+---------------------------+-----------------+------------------+------------------+
| Service / Object          | PRE-OPERATIONAL | OPERATIONAL      | STOPPED          |
+---------------------------+-----------------+------------------+------------------+
| NMT (receive commands)    |       YES       |       YES        |       YES        |
| SDO (server & client)     |       YES       |       YES        |       NO         |
| PDO (TPDO transmit)       |       NO        |       YES        |       NO         |
| PDO (RPDO receive)        |       NO        |       YES        |       NO         |
| SYNC (receive & produce)  |       YES       |       YES        |       NO         |
| TIME (receive)            |       YES       |       YES        |       NO         |
| EMCY (transmit)           |       YES       |       YES        |       NO         |
| Heartbeat (produce)       |       YES       |       YES        |       YES        |
| Node Guarding (respond)   |       YES       |       YES        |       YES        |
| LSS (Layer Setting Serv.) |       YES *     |       YES *      |       YES *      |
+---------------------------+-----------------+------------------+------------------+
  * LSS operates independently of the NMT state machine.
```

---

## 7. NMT Slave Implementation in C

### 7.1 Data Structures

```c
/* canopen_nmt.h — NMT Slave definitions */

#ifndef CANOPEN_NMT_H
#define CANOPEN_NMT_H

#include <stdint.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * NMT States (byte values used in heartbeat messages, CiA 301 §7.2.8)
 * ----------------------------------------------------------------------- */
typedef enum {
    NMT_STATE_INITIALISATION  = 0x00U,  /* internal; not sent in heartbeat */
    NMT_STATE_STOPPED         = 0x04U,
    NMT_STATE_OPERATIONAL     = 0x05U,
    NMT_STATE_PRE_OPERATIONAL = 0x7FU
} nmt_state_t;

/* -----------------------------------------------------------------------
 * NMT Command Specifiers (cs byte, CiA 301 §7.2.8.3)
 * ----------------------------------------------------------------------- */
typedef enum {
    NMT_CS_START_REMOTE_NODE    = 0x01U,
    NMT_CS_STOP_REMOTE_NODE     = 0x02U,
    NMT_CS_ENTER_PRE_OPERATIONAL= 0x80U,
    NMT_CS_RESET_NODE           = 0x81U,
    NMT_CS_RESET_COMMUNICATION  = 0x82U
} nmt_command_t;

/* -----------------------------------------------------------------------
 * CAN frame representation (platform-specific; adapt to your HAL)
 * ----------------------------------------------------------------------- */
typedef struct {
    uint32_t id;        /* 11-bit CAN-ID                    */
    uint8_t  dlc;       /* Data Length Code (0..8)          */
    uint8_t  data[8];
} can_frame_t;

/* -----------------------------------------------------------------------
 * NMT Slave context
 * ----------------------------------------------------------------------- */
typedef struct {
    uint8_t      node_id;       /* 1 .. 127                        */
    nmt_state_t  state;         /* current NMT state               */
    bool         boot_up_sent;  /* boot-up message transmitted?    */
} nmt_slave_t;

/* -----------------------------------------------------------------------
 * Callbacks — implement these in your application layer
 * ----------------------------------------------------------------------- */

/** Called when node enters OPERATIONAL (PDO comm should start). */
void app_on_operational(void);

/** Called when node enters PRE-OPERATIONAL (PDO comm should stop). */
void app_on_pre_operational(void);

/** Called when node enters STOPPED (SDO, PDO comm must stop). */
void app_on_stopped(void);

/** Called for Reset Node — re-load full Object Dictionary defaults. */
void app_on_reset_node(void);

/** Called for Reset Communication — re-load comm parameters only. */
void app_on_reset_communication(void);

/* -----------------------------------------------------------------------
 * HAL stub — implement for your CAN controller
 * ----------------------------------------------------------------------- */

/** Transmit a single CAN frame. Returns 0 on success, <0 on error. */
int  hal_can_transmit(const can_frame_t *frame);

/* -----------------------------------------------------------------------
 * NMT API
 * ----------------------------------------------------------------------- */
void nmt_slave_init(nmt_slave_t *nmt, uint8_t node_id);
void nmt_slave_process_frame(nmt_slave_t *nmt, const can_frame_t *frame);
int  nmt_slave_send_bootup(nmt_slave_t *nmt);
int  nmt_slave_send_heartbeat(nmt_slave_t *nmt);

#endif /* CANOPEN_NMT_H */
```

### 7.2 Boot-Up Transmission

```c
/* canopen_nmt.c — NMT Slave core */

#include "canopen_nmt.h"
#include <string.h>

/* -----------------------------------------------------------------------
 * nmt_slave_init
 *   Initialise the NMT slave context. Call once after CAN controller
 *   hardware has been configured.  The node immediately enters
 *   INITIALISATION (internally); call nmt_slave_send_bootup() after
 *   the Object Dictionary has been loaded to announce PRE-OPERATIONAL.
 * ----------------------------------------------------------------------- */
void nmt_slave_init(nmt_slave_t *nmt, uint8_t node_id)
{
    memset(nmt, 0, sizeof(*nmt));
    nmt->node_id      = node_id & 0x7FU; /* clamp to valid range 1..127 */
    nmt->state        = NMT_STATE_INITIALISATION;
    nmt->boot_up_sent = false;
}

/* -----------------------------------------------------------------------
 * nmt_slave_send_bootup
 *   Transmit the CANopen boot-up message:
 *     COB-ID  = 0x700 + NodeID
 *     DLC     = 1
 *     Data[0] = 0x00
 *   After transmission, the node state becomes PRE-OPERATIONAL.
 *   Returns 0 on success, <0 if the CAN layer reported an error.
 * ----------------------------------------------------------------------- */
int nmt_slave_send_bootup(nmt_slave_t *nmt)
{
    can_frame_t frame;

    frame.id     = 0x700U + (uint32_t)nmt->node_id;
    frame.dlc    = 1U;
    frame.data[0]= 0x00U;   /* boot-up indicator */

    int rc = hal_can_transmit(&frame);
    if (rc == 0) {
        nmt->state        = NMT_STATE_PRE_OPERATIONAL;
        nmt->boot_up_sent = true;
        app_on_pre_operational(); /* notify application layer */
    }
    return rc;
}

/* -----------------------------------------------------------------------
 * nmt_slave_send_heartbeat
 *   Transmit a heartbeat message with the current NMT state encoded.
 *   COB-ID  = 0x700 + NodeID
 *   DLC     = 1
 *   Data[0] = current state byte
 *   Returns 0 on success, <0 on error.
 * ----------------------------------------------------------------------- */
int nmt_slave_send_heartbeat(nmt_slave_t *nmt)
{
    can_frame_t frame;

    if (!nmt->boot_up_sent) {
        return -1; /* must send boot-up first */
    }

    frame.id     = 0x700U + (uint32_t)nmt->node_id;
    frame.dlc    = 1U;
    frame.data[0]= (uint8_t)nmt->state;

    return hal_can_transmit(&frame);
}
```

### 7.3 NMT Command Processing

```c
/* -----------------------------------------------------------------------
 * nmt_execute_command
 *   Internal: execute a validated NMT command.
 * ----------------------------------------------------------------------- */
static void nmt_execute_command(nmt_slave_t *nmt, nmt_command_t cmd)
{
    switch (cmd)
    {
    case NMT_CS_START_REMOTE_NODE:
        /*
         * Allowed from: PRE-OPERATIONAL, STOPPED
         * Forbidden from: OPERATIONAL (no-op per CiA 301)
         */
        if (nmt->state == NMT_STATE_PRE_OPERATIONAL ||
            nmt->state == NMT_STATE_STOPPED)
        {
            nmt->state = NMT_STATE_OPERATIONAL;
            app_on_operational();
        }
        break;

    case NMT_CS_STOP_REMOTE_NODE:
        /*
         * Allowed from: PRE-OPERATIONAL, OPERATIONAL
         */
        if (nmt->state == NMT_STATE_PRE_OPERATIONAL ||
            nmt->state == NMT_STATE_OPERATIONAL)
        {
            nmt->state = NMT_STATE_STOPPED;
            app_on_stopped();
        }
        break;

    case NMT_CS_ENTER_PRE_OPERATIONAL:
        /*
         * Allowed from: OPERATIONAL, STOPPED
         */
        if (nmt->state == NMT_STATE_OPERATIONAL ||
            nmt->state == NMT_STATE_STOPPED)
        {
            nmt->state = NMT_STATE_PRE_OPERATIONAL;
            app_on_pre_operational();
        }
        break;

    case NMT_CS_RESET_NODE:
        /*
         * Allowed from: any state
         * Full application + communication reset.
         */
        nmt->state        = NMT_STATE_INITIALISATION;
        nmt->boot_up_sent = false;
        app_on_reset_node();     /* re-load full OD, re-init application  */
        /* After app_on_reset_node() the application must call
         * nmt_slave_send_bootup() once hardware re-init is done.       */
        break;

    case NMT_CS_RESET_COMMUNICATION:
        /*
         * Allowed from: any state
         * Communication parameters only.
         */
        nmt->state        = NMT_STATE_INITIALISATION;
        nmt->boot_up_sent = false;
        app_on_reset_communication(); /* re-load indices 0x1000..0x1FFF  */
        /* Application must again call nmt_slave_send_bootup().         */
        break;

    default:
        /* Unknown command — silently ignore per CiA 301 */
        break;
    }
}

/* -----------------------------------------------------------------------
 * nmt_slave_process_frame
 *   Called from your CAN RX ISR or task for every received CAN frame.
 *   Filters for NMT master command frames (CAN-ID = 0x000, DLC = 2)
 *   and processes them if addressed to this node or broadcast.
 * ----------------------------------------------------------------------- */
void nmt_slave_process_frame(nmt_slave_t *nmt, const can_frame_t *frame)
{
    /* Only NMT master commands use COB-ID 0x000 with DLC = 2 */
    if (frame->id != 0x000U || frame->dlc != 2U) {
        return;
    }

    uint8_t cs      = frame->data[0];
    uint8_t node_id = frame->data[1];

    /*
     * Addressed to us?
     *   node_id == 0x00  → broadcast (applies to all slaves)
     *   node_id == our node_id → specifically addressed to us
     */
    if (node_id != 0x00U && node_id != nmt->node_id) {
        return; /* not for us */
    }

    nmt_execute_command(nmt, (nmt_command_t)cs);
}
```

### 7.4 State Machine Integration Loop

```c
/* -----------------------------------------------------------------------
 * Example: bare-metal main loop integration
 * ----------------------------------------------------------------------- */

#include "canopen_nmt.h"

/* Heartbeat period in milliseconds (configured via OD index 0x1017) */
#define HEARTBEAT_PERIOD_MS  100U

/* Application globals */
static nmt_slave_t g_nmt;
static uint32_t    g_heartbeat_timer = 0U;

/*
 * hal_get_tick_ms()  — returns a monotonic millisecond counter.
 * hal_can_receive()  — fills frame if a CAN frame is available, returns true.
 * These must be implemented for your platform (RTOS tick, SysTick, etc.).
 */
extern uint32_t hal_get_tick_ms(void);
extern bool     hal_can_receive(can_frame_t *frame);

void canopen_main(void)
{
    can_frame_t rx_frame;

    /* --- Step 1: Hardware and OD initialisation (application-specific) --- */
    hal_can_init();          /* configure CAN controller: 250 kbit/s etc.   */
    od_load_defaults();      /* fill Object Dictionary from ROM              */

    /* --- Step 2: NMT slave init + boot-up message ------------------------ */
    nmt_slave_init(&g_nmt, 5U);  /* this node is Node-ID 5 */
    nmt_slave_send_bootup(&g_nmt);
    /* Node is now in PRE-OPERATIONAL.  Master may send SDO config here.    */

    g_heartbeat_timer = hal_get_tick_ms();

    /* --- Step 3: Main event loop ----------------------------------------- */
    for (;;)
    {
        /* 3a. Process all incoming CAN frames */
        while (hal_can_receive(&rx_frame)) {
            nmt_slave_process_frame(&g_nmt, &rx_frame);
            /* Route other COB-IDs (SDO, SYNC, …) to their own handlers    */
            sdo_server_process_frame(&rx_frame);
            sync_handler_process_frame(&rx_frame);
        }

        /* 3b. Periodic heartbeat */
        uint32_t now = hal_get_tick_ms();
        if ((now - g_heartbeat_timer) >= HEARTBEAT_PERIOD_MS) {
            g_heartbeat_timer = now;
            nmt_slave_send_heartbeat(&g_nmt);
        }

        /* 3c. PDO transmission (only when OPERATIONAL) */
        if (g_nmt.state == NMT_STATE_OPERATIONAL) {
            pdo_cyclic_process();
        }
    }
}

/* -----------------------------------------------------------------------
 * Application callback implementations
 * ----------------------------------------------------------------------- */

void app_on_operational(void)
{
    /* Enable PDO timer, start sensor acquisition, etc. */
    pdo_enable_transmission(true);
}

void app_on_pre_operational(void)
{
    /* Disable PDO timers but keep SDO server running */
    pdo_enable_transmission(false);
}

void app_on_stopped(void)
{
    /* Disable PDOs and SDO server */
    pdo_enable_transmission(false);
    sdo_server_enable(false);
}

void app_on_reset_node(void)
{
    /* Full software reset — re-load everything */
    od_load_defaults();
    pdo_init();
    sdo_server_init();
    /* Caller (nmt_execute_command) expects us to call send_bootup again */
    nmt_slave_send_bootup(&g_nmt);
}

void app_on_reset_communication(void)
{
    /* Re-load communication parameters (OD 0x1000..0x1FFF) only */
    od_load_comm_defaults();
    pdo_init();
    sdo_server_init();
    nmt_slave_send_bootup(&g_nmt);
}
```

### 7.5 Complete Minimal NMT Slave (C)

The following is a self-contained, compilable minimal NMT slave for demonstration,
using a software loopback CAN HAL stub:

```c
/* minimal_nmt_slave.c
 * Compile: gcc -Wall -Wextra -o nmt_slave minimal_nmt_slave.c
 * Demonstrates boot-up, state transitions, and command processing.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---- Types ---- */
typedef enum {
    NMT_INIT     = 0x00,
    NMT_STOPPED  = 0x04,
    NMT_OP       = 0x05,
    NMT_PRE_OP   = 0x7F
} nmt_state_t;

typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} can_frame_t;

typedef struct {
    uint8_t     node_id;
    nmt_state_t state;
} nmt_slave_t;

/* ---- HAL stub (loopback) ---- */
static void hal_can_tx(const can_frame_t *f)
{
    printf("  TX  CAN-ID=0x%03X  DLC=%u  Data:", f->id, f->dlc);
    for (uint8_t i = 0; i < f->dlc; i++) printf(" %02X", f->data[i]);
    printf("\n");
}

static const char *state_name(nmt_state_t s) {
    switch (s) {
        case NMT_INIT:    return "INITIALISATION";
        case NMT_STOPPED: return "STOPPED";
        case NMT_OP:      return "OPERATIONAL";
        case NMT_PRE_OP:  return "PRE-OPERATIONAL";
        default:          return "UNKNOWN";
    }
}

/* ---- NMT implementation ---- */
static void nmt_set_state(nmt_slave_t *n, nmt_state_t next)
{
    printf("  STATE  %s  -->  %s\n", state_name(n->state), state_name(next));
    n->state = next;
}

static void nmt_send_bootup(nmt_slave_t *n)
{
    can_frame_t f;
    f.id     = 0x700U + n->node_id;
    f.dlc    = 1;
    f.data[0]= 0x00;
    hal_can_tx(&f);
    nmt_set_state(n, NMT_PRE_OP);
}

static void nmt_rx(nmt_slave_t *n, const can_frame_t *f)
{
    if (f->id != 0x000 || f->dlc != 2) return;

    uint8_t cs  = f->data[0];
    uint8_t nid = f->data[1];

    if (nid != 0x00 && nid != n->node_id) return;

    printf("  RX  NMT cmd=0x%02X  nodeID=0x%02X\n", cs, nid);

    switch (cs) {
    case 0x01:
        if (n->state == NMT_PRE_OP || n->state == NMT_STOPPED)
            nmt_set_state(n, NMT_OP);
        break;
    case 0x02:
        if (n->state == NMT_PRE_OP || n->state == NMT_OP)
            nmt_set_state(n, NMT_STOPPED);
        break;
    case 0x80:
        if (n->state == NMT_OP || n->state == NMT_STOPPED)
            nmt_set_state(n, NMT_PRE_OP);
        break;
    case 0x81:  /* fall-through */
    case 0x82:
        nmt_send_bootup(n);   /* re-initialise */
        break;
    default:
        break;
    }
}

/* ---- Demo ---- */
int main(void)
{
    nmt_slave_t slave = { .node_id = 5, .state = NMT_INIT };
    can_frame_t cmd;

    printf("=== CANopen NMT Slave Demo (Node-ID 5) ===\n\n");

    printf("[1] Boot-up\n");
    nmt_send_bootup(&slave);

    printf("\n[2] Master broadcasts Start Node\n");
    cmd = (can_frame_t){ .id=0, .dlc=2, .data={0x01, 0x00} };
    nmt_rx(&slave, &cmd);

    printf("\n[3] Master sends Pre-Op to Node 5\n");
    cmd = (can_frame_t){ .id=0, .dlc=2, .data={0x80, 0x05} };
    nmt_rx(&slave, &cmd);

    printf("\n[4] Master sends Stop Node to Node 5\n");
    cmd = (can_frame_t){ .id=0, .dlc=2, .data={0x02, 0x05} };
    nmt_rx(&slave, &cmd);

    printf("\n[5] Master sends Start Node broadcast\n");
    cmd = (can_frame_t){ .id=0, .dlc=2, .data={0x01, 0x00} };
    nmt_rx(&slave, &cmd);

    printf("\n[6] Master sends Reset Comm to Node 5\n");
    cmd = (can_frame_t){ .id=0, .dlc=2, .data={0x82, 0x05} };
    nmt_rx(&slave, &cmd);

    printf("\nFinal state: %s\n", state_name(slave.state));
    return 0;
}
```

**Expected output:**

```
=== CANopen NMT Slave Demo (Node-ID 5) ===

[1] Boot-up
  TX  CAN-ID=0x705  DLC=1  Data: 00
  STATE  INITIALISATION  -->  PRE-OPERATIONAL

[2] Master broadcasts Start Node
  RX  NMT cmd=0x01  nodeID=0x00
  STATE  PRE-OPERATIONAL  -->  OPERATIONAL

[3] Master sends Pre-Op to Node 5
  RX  NMT cmd=0x80  nodeID=0x05
  STATE  OPERATIONAL  -->  PRE-OPERATIONAL

[4] Master sends Stop Node to Node 5
  RX  NMT cmd=0x02  nodeID=0x05
  STATE  PRE-OPERATIONAL  -->  STOPPED

[5] Master sends Start Node broadcast
  RX  NMT cmd=0x01  nodeID=0x00
  STATE  STOPPED  -->  OPERATIONAL

[6] Master sends Reset Comm to Node 5
  RX  NMT cmd=0x82  nodeID=0x05
  TX  CAN-ID=0x705  DLC=1  Data: 00
  STATE  INITIALISATION  -->  PRE-OPERATIONAL

Final state: PRE-OPERATIONAL
```

---

## 8. NMT Master Implementation in C++

The following C++ class encapsulates the NMT master role, capable of commanding any
slave or broadcasting to all nodes:

```cpp
// NmtMaster.hpp — CANopen NMT Master
// C++17

#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <optional>

/* -----------------------------------------------------------------------
 * NMT state as reported by slaves in heartbeat messages
 * ----------------------------------------------------------------------- */
enum class NmtState : uint8_t {
    Initialisation = 0x00,
    Stopped        = 0x04,
    Operational    = 0x05,
    PreOperational = 0x7F,
    Unknown        = 0xFF
};

/* -----------------------------------------------------------------------
 * NMT command specifiers
 * ----------------------------------------------------------------------- */
enum class NmtCommand : uint8_t {
    StartNode         = 0x01,
    StopNode          = 0x02,
    EnterPreOp        = 0x80,
    ResetNode         = 0x81,
    ResetCommunication= 0x82
};

struct CanFrame {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
};

/* -----------------------------------------------------------------------
 * NmtMaster
 *   Manages NMT commands and tracks the last known state of each slave
 *   based on received heartbeat / boot-up messages.
 * ----------------------------------------------------------------------- */
class NmtMaster {
public:
    /* Transmit callback: supply your CAN HAL send function here */
    using TxFunc = std::function<int(const CanFrame&)>;

    explicit NmtMaster(TxFunc tx_fn) : tx_(std::move(tx_fn)) {}

    /* --- Command sending ------------------------------------------------ */

    /** Send NMT command to a specific node (node_id 1..127). */
    int sendCommand(NmtCommand cmd, uint8_t node_id)
    {
        return sendRaw(static_cast<uint8_t>(cmd), node_id);
    }

    /** Broadcast NMT command to all nodes (node_id = 0x00). */
    int broadcastCommand(NmtCommand cmd)
    {
        return sendRaw(static_cast<uint8_t>(cmd), 0x00U);
    }

    /* Convenience wrappers */
    int startAll()              { return broadcastCommand(NmtCommand::StartNode);  }
    int stopAll()               { return broadcastCommand(NmtCommand::StopNode);   }
    int preOpAll()              { return broadcastCommand(NmtCommand::EnterPreOp); }
    int resetAll()              { return broadcastCommand(NmtCommand::ResetNode);  }
    int resetCommAll()          { return broadcastCommand(NmtCommand::ResetCommunication); }

    int startNode(uint8_t id)   { return sendCommand(NmtCommand::StartNode,  id); }
    int stopNode(uint8_t id)    { return sendCommand(NmtCommand::StopNode,   id); }
    int preOpNode(uint8_t id)   { return sendCommand(NmtCommand::EnterPreOp, id); }
    int resetNode(uint8_t id)   { return sendCommand(NmtCommand::ResetNode,  id); }

    /* --- Frame reception ------------------------------------------------ */

    /**
     * Call this for every received CAN frame.
     * Processes boot-up and heartbeat messages (COB-ID = 0x700..0x77F).
     * Returns the node_id if a state change was detected, 0 otherwise.
     */
    uint8_t processFrame(const CanFrame& f)
    {
        /* Heartbeat / boot-up: 0x700 + NodeID, DLC = 1 */
        if (f.id >= 0x701U && f.id <= 0x77FU && f.dlc == 1U) {
            uint8_t  node_id   = static_cast<uint8_t>(f.id - 0x700U);
            NmtState new_state = static_cast<NmtState>(f.data[0]);

            auto& stored = node_states_[node_id];
            if (!stored.has_value() || *stored != new_state) {
                stored = new_state;
                if (on_state_change_) {
                    on_state_change_(node_id, new_state);
                }
                return node_id;
            }
        }
        return 0U;
    }

    /* --- Query ---------------------------------------------------------- */

    /** Returns the last known state of node_id, or NmtState::Unknown. */
    NmtState nodeState(uint8_t node_id) const
    {
        auto it = node_states_.find(node_id);
        return (it != node_states_.end() && it->second.has_value())
               ? *it->second
               : NmtState::Unknown;
    }

    bool isOperational(uint8_t node_id) const {
        return nodeState(node_id) == NmtState::Operational;
    }

    /* --- Callbacks ------------------------------------------------------ */

    using StateChangeCb = std::function<void(uint8_t node_id, NmtState state)>;
    void setStateChangeCallback(StateChangeCb cb) { on_state_change_ = std::move(cb); }

private:
    int sendRaw(uint8_t cs, uint8_t node_id)
    {
        CanFrame f{};
        f.id      = 0x000U;
        f.dlc     = 2U;
        f.data[0] = cs;
        f.data[1] = node_id;
        return tx_(f);
    }

    TxFunc       tx_;
    StateChangeCb on_state_change_;
    std::unordered_map<uint8_t, std::optional<NmtState>> node_states_;
};
```

**Usage example:**

```cpp
#include "NmtMaster.hpp"
#include <iostream>

int main()
{
    /* Provide your actual CAN transmit function here */
    NmtMaster master([](const CanFrame& f) -> int {
        std::printf("TX  ID=0x%03X  data=[%02X %02X]\n",
                    f.id, f.data[0], f.data[1]);
        return 0;
    });

    /* Register state-change callback */
    master.setStateChangeCallback([](uint8_t node_id, NmtState state) {
        const char *s = "?";
        switch (state) {
            case NmtState::PreOperational: s = "PRE-OP";      break;
            case NmtState::Operational:    s = "OPERATIONAL";  break;
            case NmtState::Stopped:        s = "STOPPED";      break;
            case NmtState::Initialisation: s = "BOOT-UP";      break;
            default: break;
        }
        std::printf("  Node %u  ->  %s\n", node_id, s);
    });

    /* Simulate receiving boot-up from Node 5 (ID=0x705, data=0x00) */
    CanFrame boot_up{ 0x705U, 1U, {0x00} };
    master.processFrame(boot_up);

    /* Start all nodes */
    master.startAll();

    /* Simulate heartbeat from Node 5 showing it is now OPERATIONAL */
    CanFrame hb_op{ 0x705U, 1U, {0x05} };
    master.processFrame(hb_op);

    std::printf("Node 5 operational: %s\n",
                master.isOperational(5) ? "YES" : "NO");

    /* Stop just Node 5 for reconfiguration */
    master.preOpNode(5);

    return 0;
}
```

---

## 9. Error Handling & Node Guarding / Heartbeat Interaction

```
 Heartbeat Producer / Consumer Monitoring
 =========================================

  Slave Node                         NMT Master / Monitor Node
  ----------                         --------------------------
  Every T_HB ms:                     Expects heartbeat within T_HB + guard_time:
  ┌──────────┐                        ┌──────────────────────────────┐
  │ Heartbeat│ ──0x705, data=0x05──>  │  Heartbeat consumer timer    │
  │  timer   │                        │  (re-armed on each message)  │
  └──────────┘                        └──────────────────────────────┘

  If no heartbeat arrives within window:
    → "Heartbeat Event" (emergency / error protocol)
    → Master may issue Reset Node to recover

  T_HB   : Heartbeat Producer Time  (OD index 0x1017, subindex 0, unit: ms)
  T_guard: Heartbeat Consumer Time  (OD index 0x1016, subindex N)

  CiA 301 recommends:  T_consumer > 1.5 × T_producer

 Node Guarding (legacy, pre-heartbeat)
 ======================================
  Master polls each slave by remote frame on 0x700+NodeID.
  Slave responds with current state + toggle bit.
  Deprecated — prefer heartbeat protocol.
```

NMT error reactions are often combined with the **EMCY** (Emergency) protocol:

```c
/* Example: raise emergency on NMT error */
void app_on_nmt_timeout(uint8_t failed_node_id)
{
    /* 0x8130 = Heartbeat by Consumer (CiA 301 error code) */
    emcy_send(0x8130U, 0x00U, failed_node_id);
    /* Optionally attempt recovery */
    nmt_master_reset_node(&g_master, failed_node_id);
}
```

---

## 10. Timing Diagram

```
 Multi-node startup timing (time flows downward)
 ================================================

  Bus          Node 1            Node 2            Node 5           Master
   |              |                 |                 |                |
   |  power-on    |  power-on       |  power-on       |                |
   |<-------------|<----------------|<----------------|                |
   |              |                 |                 |                |
   | 0x701 [0x00] |                 |                 |  boot-up N1    |
   |<-------------|                 |                 |--------------->|
   | 0x702 [0x00] |                 |                 |  boot-up N2    |
   |<-------------------------------|                 |--------------->|
   | 0x705 [0x00] |                 |                 |  boot-up N5    |
   |<------------------------------------------------ |--------------->|
   |              |                 |                 |                |
   |              |                 |<--SDO write (config N2)--------- |
   |              |                 |                 |                |
   | 0x000[01,00] |  Start_All      |                 |                |
   |<----------------------------------------------------------------- |
   |              | --> OPERATIONAL | --> OPERATIONAL | --> OPERATIONAL|
   |              |                 |                 |                |
   | 0x181 TPDO   |                 |                 |  PDO from N1   |
   |<-------------|                 |                 |                |
   | 0x182 TPDO   |                 |                 |  PDO from N2   |
   |<-------------------------------|                 |                |
   | 0x705 [0x05] heartbeat         |                 |  HB from N5    |
   |<------------------------------------------------ |--------------->|
   :              :                 :                 :                :
   |              |                 |                 |                |
   | 0x000[80,02] |  Pre-Op N2      |                 |                |
   |<------------------------------------------------------------------|
   |              |                 | --> PRE-OP      |                |
   |              |<--SDO reconfig--|                 |                |
   | 0x000[01,02] |  Start N2       |                 |                |
   |<------------------------------------------------------------------|
   |              |                 | --> OPERATIONAL |                |
   v              v                 v                 v                v
```

---

## 11. Common Pitfalls

```
+----+--------------------------------------+----------------------------------------+
| #  | Pitfall                              | Resolution                             |
+----+--------------------------------------+----------------------------------------+
| 1  | Missing boot-up message              | Always call nmt_send_bootup() after    |
|    |                                      | CAN init. Masters may time out waiting.|
+----+--------------------------------------+----------------------------------------+
| 2  | Sending PDOs before OPERATIONAL      | Gate all PDO transmit on               |
|    |                                      | state == NMT_STATE_OPERATIONAL.        |
+----+--------------------------------------+----------------------------------------+
| 3  | Accepting SDOs in STOPPED state      | SDO server must be disabled in         |
|    |                                      | STOPPED. Gate on state check.          |
+----+--------------------------------------+----------------------------------------+
| 4  | Reset Node without re-sending        | Reset must complete full INIT cycle    |
|    | boot-up                              | and re-transmit boot-up (data=0x00).   |
+----+--------------------------------------+----------------------------------------+
| 5  | Confusing boot-up with heartbeat     | Boot-up: data=0x00 ONLY on INIT exit.  |
|    | (both use 0x700+NodeID)              | Heartbeat: data = state byte (≠ 0x00). |
+----+--------------------------------------+----------------------------------------+
| 6  | Ignoring NMT broadcast (NodeID=0)    | Always check for NodeID==0 as well as  |
|    |                                      | your own node_id in the filter logic.  |
+----+--------------------------------------+----------------------------------------+
| 7  | Reset Comm re-loading app objects    | Reset Comm must only reset OD indices  |
|    |                                      | 0x1000..0x1FFF; leave 0x2000+ intact.  |
+----+--------------------------------------+----------------------------------------+
| 8  | Heartbeat period = 0 (disabled)      | If OD 0x1017 = 0, heartbeat is off.    |
|    | causing monitor timeout              | Set explicitly; don't rely on default. |
+----+--------------------------------------+----------------------------------------+
| 9  | NodeID 0 used as a device address    | NodeID 0 is reserved for broadcast.    |
|    |                                      | Valid slave IDs are 1..127 only.       |
+----+--------------------------------------+----------------------------------------+
| 10 | CAN-ID 0x000 not highest priority    | Ensure no higher-priority frame blocks |
|    |                                      | NMT commands. 0x000 is the lowest      |
|    |                                      | arbitration value = highest CAN prio.  |
+----+--------------------------------------+----------------------------------------+
```

---

## 12. Summary

The **CANopen NMT State Machine** defines the complete lifecycle of every node on the
network through four states: **Initialisation**, **Pre-Operational**, **Operational**, and
**Stopped**.

Key points to remember:

**Initialisation** is a private, internal state. It is never visible on the bus; the only
externally observable event is the **boot-up message** (COB-ID = 0x700 + NodeID, one byte
of value 0x00) sent when the node leaves this state and enters Pre-Operational. This
message is the network's announcement that the node is alive and ready to be configured.

**Pre-Operational** is the configuration state. SDOs are fully active, allowing the master
to read and write Object Dictionary entries. SYNC and TIME messages are processed, and the
EMCY object can transmit. However, PDOs are completely silent — process data exchange has
not yet begun.

**Operational** is the run state. All CANopen services are active simultaneously. PDOs
carry real-time process data at configured cycle times. This is the normal production state
for any running node.

**Stopped** is a parking state. Only NMT commands and heartbeat are still processed; no
SDO or PDO traffic occurs. Used to temporarily isolate a node without a full reset.

NMT master commands are sent on **CAN-ID 0x000** — the highest-priority arbitration ID on
the bus — using a two-byte frame: the command specifier (0x01, 0x02, 0x80, 0x81, 0x82)
followed by the target NodeID, where 0x00 means broadcast to all.

For implementation, the slave only needs to filter CAN-ID 0x000 with DLC=2, compare the
target NodeID against its own or 0x00, then execute the corresponding state transition and
invoke the appropriate application callback to start or stop PDOs, SDO services, and
hardware I/O accordingly.

The NMT layer is intentionally minimal and deterministic — it is the foundation on which
all higher-level CANopen services (SDO, PDO, SYNC, EMCY, LSS) depend.

---

*CiA 301 v4.2 · CANopen application layer and communication profile*
*References: CiA 302 (NMT master), CiA 305 (LSS), CiA 306 (Electronic Data Sheet)*# NMT State Machine & Network Management

