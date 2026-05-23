# 25. CANopen Master Architecture

**Architecture Overview** — ASCII block diagram showing how all subsystems relate to the CAN bus, Object Dictionary, and CAN driver HAL.
**SDO Manager** — State machine diagram, full C implementation with queuing, timeout, retry logic, and an asynchronous callback API.
**PDO Scheduler** — Transmission type reference table, C implementation with SYNC generation, synchronous and event-driven TPDO firing, RPDO timeout monitoring, and a timing diagram for the SYNC window.
**NMT Supervisor** — Per-slave NMT state machine (ASCII), C++ implementation with boot gating and all NMT commands.
**Heartbeat Monitor** — Timing diagram showing deadline detection with jitter tolerance, C++ implementation with lost/recover callbacks.
**Configuration Manager** — SDO-driven state machine (ASCII), C++ implementation that sequentially downloads EDS/DCF entries to a slave, including a concrete PDO mapping sequence example.
**RTOS Task Decomposition** — Priority-ordered ASCII task diagram, FreeRTOS skeleton with `vTaskDelayUntil`-based periodic tasks and ISR→queue→task CAN receive path.
**Re-entrant OD Access** — Concurrent access diagram, full C++ `ObjectDictionary` class with mutex-protected read/write and a zero-copy `acquire/release` path for PDO mapping, plus a deadlock-safe lock-ordering rule.
**Integration Example** — Shows all subsystems wired together in a realistic master init and boot sequence for a slave I/O node.
**Summary Table** — Quick-reference grid of subsystem, function, design pattern, and RTOS priority.# CANopen Master Architecture

---

## Table of Contents

1. [Introduction](#introduction)
2. [Responsibilities of a Master Node](#responsibilities-of-a-master-node)
3. [Overall Master Architecture](#overall-master-architecture)
4. [SDO Manager](#sdo-manager)
5. [PDO Scheduler](#pdo-scheduler)
6. [NMT Supervisor](#nmt-supervisor)
7. [Heartbeat Monitor](#heartbeat-monitor)
8. [Configuration Manager Pattern](#configuration-manager-pattern)
9. [Task/Thread Decomposition on RTOS](#taskthread-decomposition-on-rtos)
10. [Re-entrant Object Dictionary Access](#re-entrant-object-dictionary-access)
11. [Putting It All Together — Integration Example](#putting-it-all-together--integration-example)
12. [Summary](#summary)

---

## Introduction

In a CANopen network, nodes are classified as either **masters** or **slaves**. The master node is the
central coordinator that bootstraps the network, manages communication, monitors node health, and
configures each slave's behaviour. Unlike a simple slave node that merely responds to requests, the
master proactively drives the system through a rich set of management subsystems.

This document covers every major subsystem of a production-quality CANopen master, with C/C++
implementation examples suitable for deployment on a real-time operating system (RTOS) such as
FreeRTOS, RTEMS, or VxWorks.

---

## Responsibilities of a Master Node

A CANopen master node carries the following core responsibilities:

**Network Lifecycle Management**
- Issue NMT commands (Operational, Pre-Operational, Stopped, Reset) to slave nodes
- Execute the network boot-up sequence as defined by CiA 302

**Service Data Object (SDO) Management**
- Read and write Object Dictionary (OD) entries on remote slave nodes
- Queue, serialise, and time-out SDO transactions

**Process Data Object (PDO) Scheduling**
- Organise and trigger synchronised (SYNC-based) and event-driven PDO transmission
- Map received PDOs from slaves into the master's internal data model

**Node Health Supervision**
- Send and receive heartbeat messages (CiA 301 §7.2.8)
- Detect missing heartbeats and initiate recovery procedures

**System Configuration**
- Download slave configuration via SDO at startup (EDS/DCF-based)
- Manage firmware updates and node commissioning

**Error Handling and Diagnostics**
- Monitor emergency objects (EMCY) from slaves
- Log and react to bus-off conditions and communication errors

---

## Overall Master Architecture

```
+---------------------------------------------------------------+
|                     CANopen Master Node                       |
|                                                               |
|  +-------------------+      +----------------------------+    |
|  |   Application /   |      |   Configuration Manager    |    |
|  |   User Logic      |<---->|   (EDS/DCF Loader)         |    |
|  +--------+----------+      +----------------------------+    |
|           |                            |                      |
|           v                            v                      |
|  +--------+-------------------------------------------+       |
|  |              Master Core / Dispatcher              |       |
|  |  (event loop or RTOS task co-ordinating all below) |       |
|  +----+----------+----------+-----------+-------------+       |
|       |          |          |           |                     |
|       v          v          v           v                     |
|  +--------+ +--------+ +--------+ +---------+                 |
|  |  SDO   | |  PDO   | |  NMT   | | Heart-  |                 |
|  | Manager| |Schedul.| | Super- | |  beat   |                 |
|  |        | |        | | visor  | | Monitor |                 |
|  +---+----+ +---+----+ +---+----+ +----+----+                 |
|      |          |          |           |                      |
|      +----------+----------+-----------+                      |
|                            |                                  |
|               +------------+-------------+                    |
|               |    Object Dictionary     |                    |
|               |  (re-entrant, mutex-     |                    |
|               |   protected access)      |                    |
|               +------------+-------------+                    |
|                            |                                  |
|               +------------+-------------+                    |
|               |      CAN Driver / HAL    |                    |
|               +------------+-------------+                    |
|                            |                                  |
+----------------------------+---------------------------------+
                             |
            =================|========================================
                        CAN Bus (ISO 11898)
            ==========================================================
                   |              |              |
             +-----+-----+  +-----+-----+  +-----+-----+
             | Slave  1  |  | Slave  2  |  | Slave  N  |
             +-----------+  +-----------+  +-----------+
```

---

## SDO Manager

### Concept

SDO (Service Data Object) transfers allow peer-to-peer read/write access to any entry in a remote
node's Object Dictionary. SDOs are confirmed, meaning every request receives a response (or a
timeout). On a master, many subsystems may need to perform SDO transfers concurrently (configuration
manager, diagnostic tool, application layer). The SDO Manager serialises these requests through a
queue, tracks active transfers, and handles timeouts and retries.

### SDO Transfer State Machine

```
            +----------+
            |   IDLE   |<-----------------------+
            +----+-----+                        |
                 |  request enqueued            |
                 v                              |
            +----+--------+    timeout/abort    |
            |  INITIATE   +--------------------+|
            | (send req)  |                    ||
            +----+--------+                    ||
                 |  response received          ||
                 v                             ||
            +----+--------+                    ||
            | SEGMENTED?  +--No-->  COMPLETE --+|
            +----+--------+                    |
                 | Yes                         |
                 v                             |
            +----+--------+  all segments rx   |
            |  SEGMENT    +-------------------+|
            |  TRANSFER   |                    |
            +----+--------+                    |
                 | error / abort               |
                 +------>  ERROR  -------------+
```

### C Implementation

```c
/* sdo_manager.h */
#ifndef SDO_MANAGER_H
#define SDO_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#define SDO_QUEUE_DEPTH     16
#define SDO_TIMEOUT_MS      200
#define SDO_MAX_RETRIES     3

typedef enum {
    SDO_STATE_IDLE,
    SDO_STATE_WAITING_RESPONSE,
    SDO_STATE_SEGMENTED,
    SDO_STATE_DONE,
    SDO_STATE_ERROR
} SDOState;

typedef void (*SDOCallback)(uint8_t node_id, uint16_t index, uint8_t subindex,
                            int error_code, void *user_data);

typedef struct {
    uint8_t     node_id;
    uint16_t    index;
    uint8_t     subindex;
    bool        is_write;
    uint8_t     data[4];        /* expedited data (<=4 bytes) */
    uint8_t     size;
    SDOCallback callback;
    void        *user_data;
    uint8_t     retries;
} SDORequest;

typedef struct {
    SDORequest  queue[SDO_QUEUE_DEPTH];
    uint8_t     head;
    uint8_t     tail;
    uint8_t     count;
    SDOState    state;
    SDORequest  active;
    uint32_t    timeout_tick;   /* set when request is sent */
    /* RTOS mutex handle (opaque pointer for portability) */
    void        *mutex;
} SDOManager;

/* Public API */
int  sdo_manager_init(SDOManager *mgr);
int  sdo_read(SDOManager *mgr, uint8_t node_id,
              uint16_t index, uint8_t subindex,
              SDOCallback cb, void *user_data);
int  sdo_write(SDOManager *mgr, uint8_t node_id,
               uint16_t index, uint8_t subindex,
               const uint8_t *data, uint8_t len,
               SDOCallback cb, void *user_data);
void sdo_manager_process(SDOManager *mgr, uint32_t now_ms);
void sdo_manager_on_rx(SDOManager *mgr, uint8_t node_id,
                       const uint8_t *payload, uint8_t dlc);

#endif /* SDO_MANAGER_H */
```

```c
/* sdo_manager.c */
#include "sdo_manager.h"
#include "can_driver.h"
#include "rtos_mutex.h"
#include <string.h>

/* CAN-IDs for SDO: Tx to node = 0x600 + node_id,
                    Rx from node = 0x580 + node_id  */
#define SDO_TX_BASE  0x600u
#define SDO_RX_BASE  0x580u

/* SDO command specifiers */
#define SDO_CS_WRITE_4B  0x23u   /* initiate download, 4 bytes */
#define SDO_CS_WRITE_3B  0x27u
#define SDO_CS_WRITE_2B  0x2Bu
#define SDO_CS_WRITE_1B  0x2Fu
#define SDO_CS_READ      0x40u   /* initiate upload */
#define SDO_CS_WRITE_OK  0x60u   /* download response */
#define SDO_CS_READ_OK   0x43u   /* upload response, 4 bytes expedited */
#define SDO_CS_ABORT     0x80u

static int enqueue(SDOManager *mgr, const SDORequest *req)
{
    if (mgr->count >= SDO_QUEUE_DEPTH) return -1;
    mgr->queue[mgr->tail] = *req;
    mgr->tail = (mgr->tail + 1u) % SDO_QUEUE_DEPTH;
    mgr->count++;
    return 0;
}

static void send_active(SDOManager *mgr, uint32_t now_ms)
{
    SDORequest *r = &mgr->active;
    uint8_t frame[8] = {0};
    uint32_t can_id = SDO_TX_BASE + r->node_id;

    if (r->is_write) {
        /* Choose command specifier based on data size */
        switch (r->size) {
        case 1: frame[0] = SDO_CS_WRITE_1B; break;
        case 2: frame[0] = SDO_CS_WRITE_2B; break;
        case 3: frame[0] = SDO_CS_WRITE_3B; break;
        default:frame[0] = SDO_CS_WRITE_4B; break;
        }
        memcpy(&frame[4], r->data, r->size);
    } else {
        frame[0] = SDO_CS_READ;
    }
    frame[1] = (uint8_t)(r->index & 0xFFu);
    frame[2] = (uint8_t)(r->index >> 8u);
    frame[3] = r->subindex;

    can_send(can_id, frame, 8);
    mgr->state        = SDO_STATE_WAITING_RESPONSE;
    mgr->timeout_tick = now_ms + SDO_TIMEOUT_MS;
}

int sdo_manager_init(SDOManager *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->mutex = rtos_mutex_create();
    return (mgr->mutex != NULL) ? 0 : -1;
}

int sdo_read(SDOManager *mgr, uint8_t node_id,
             uint16_t index, uint8_t subindex,
             SDOCallback cb, void *user_data)
{
    SDORequest req = {
        .node_id  = node_id,
        .index    = index,
        .subindex = subindex,
        .is_write = false,
        .callback = cb,
        .user_data = user_data,
        .retries  = 0
    };
    rtos_mutex_lock(mgr->mutex);
    int rc = enqueue(mgr, &req);
    rtos_mutex_unlock(mgr->mutex);
    return rc;
}

int sdo_write(SDOManager *mgr, uint8_t node_id,
              uint16_t index, uint8_t subindex,
              const uint8_t *data, uint8_t len,
              SDOCallback cb, void *user_data)
{
    SDORequest req = {
        .node_id  = node_id,
        .index    = index,
        .subindex = subindex,
        .is_write = true,
        .size     = (len > 4u) ? 4u : len,
        .callback = cb,
        .user_data = user_data,
        .retries  = 0
    };
    memcpy(req.data, data, req.size);
    rtos_mutex_lock(mgr->mutex);
    int rc = enqueue(mgr, &req);
    rtos_mutex_unlock(mgr->mutex);
    return rc;
}

/* Call this periodically from the master task */
void sdo_manager_process(SDOManager *mgr, uint32_t now_ms)
{
    rtos_mutex_lock(mgr->mutex);

    /* Start next request if idle */
    if (mgr->state == SDO_STATE_IDLE && mgr->count > 0u) {
        mgr->active = mgr->queue[mgr->head];
        mgr->head   = (mgr->head + 1u) % SDO_QUEUE_DEPTH;
        mgr->count--;
        send_active(mgr, now_ms);
    }

    /* Check for timeout */
    if (mgr->state == SDO_STATE_WAITING_RESPONSE &&
        now_ms >= mgr->timeout_tick) {
        if (mgr->active.retries < SDO_MAX_RETRIES) {
            mgr->active.retries++;
            send_active(mgr, now_ms);
        } else {
            /* Report error to caller */
            if (mgr->active.callback) {
                mgr->active.callback(mgr->active.node_id,
                                     mgr->active.index,
                                     mgr->active.subindex,
                                     -1 /* timeout */,
                                     mgr->active.user_data);
            }
            mgr->state = SDO_STATE_IDLE;
        }
    }

    rtos_mutex_unlock(mgr->mutex);
}

void sdo_manager_on_rx(SDOManager *mgr, uint8_t node_id,
                       const uint8_t *payload, uint8_t dlc)
{
    (void)dlc;
    rtos_mutex_lock(mgr->mutex);

    if (mgr->state != SDO_STATE_WAITING_RESPONSE ||
        mgr->active.node_id != node_id) {
        rtos_mutex_unlock(mgr->mutex);
        return;
    }

    uint8_t cs = payload[0] & 0xE0u;  /* upper 3 bits = command specifier */

    if (payload[0] == SDO_CS_ABORT) {
        uint32_t abort_code;
        memcpy(&abort_code, &payload[4], 4);
        if (mgr->active.callback)
            mgr->active.callback(node_id, mgr->active.index,
                                 mgr->active.subindex,
                                 (int)abort_code,
                                 mgr->active.user_data);
        mgr->state = SDO_STATE_IDLE;
    } else if (cs == 0x60u /* write ack */ || (payload[0] & 0x02u)) {
        /* Expedited upload or successful download */
        memcpy(mgr->active.data, &payload[4], 4);
        if (mgr->active.callback)
            mgr->active.callback(node_id, mgr->active.index,
                                 mgr->active.subindex,
                                 0 /* success */,
                                 mgr->active.user_data);
        mgr->state = SDO_STATE_IDLE;
    }

    rtos_mutex_unlock(mgr->mutex);
}
```

---

## PDO Scheduler

### Concept

PDOs (Process Data Objects) carry real-time process data with no protocol overhead beyond a CAN
frame. The master's PDO scheduler manages:

- **SYNC message** generation (object 0x1005): triggers synchronous PDOs across all nodes simultaneously
- **TPDO monitoring**: receiving PDOs from slave nodes and mapping data into the master's process image
- **RPDO scheduling**: sending master's own PDOs to slaves at the correct transmission type and event timing

### PDO Transmission Types

```
Transmission Type |  Trigger condition
------------------+--------------------------------------------------
  0               |  Acyclic synchronous  (SYNC + new data)
  1               |  Every SYNC
  2-240           |  Every N-th SYNC
  241-251         |  Reserved
  252             |  Synchronous, on remote request
  253             |  Event-driven (manufacturer-specific)
  254             |  Event-driven (device-profile specific)
  255             |  Event-driven, with inhibit time
------------------+--------------------------------------------------
```

### PDO Scheduler Data Structures

```c
/* pdo_scheduler.h */
#ifndef PDO_SCHEDULER_H
#define PDO_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_RPDOS   16
#define MAX_TPDOS   16
#define PDO_MAX_LEN  8

typedef struct {
    uint32_t can_id;
    uint8_t  tx_type;           /* transmission type 0-255 */
    uint16_t inhibit_time_100us;
    uint16_t event_timer_ms;
    uint8_t  data[PDO_MAX_LEN];
    uint8_t  dlc;
    uint32_t last_sent_tick;
    uint8_t  sync_counter;      /* for types 2-240 */
    bool     data_changed;
} TPDO;

typedef struct {
    uint32_t can_id;
    uint8_t  data[PDO_MAX_LEN];
    uint8_t  dlc;
    uint32_t last_rx_tick;
    uint32_t timeout_ms;        /* 0 = no monitoring */
    bool     fresh;             /* set on RX, cleared by application */
} RPDO;

typedef struct {
    TPDO     tpdos[MAX_TPDOS];
    uint8_t  tpdo_count;
    RPDO     rpdos[MAX_RPDOS];
    uint8_t  rpdo_count;
    uint32_t sync_period_ms;
    uint32_t next_sync_tick;
    uint8_t  sync_counter;      /* global, wraps at 240 */
    void     *mutex;
} PDOScheduler;

int  pdo_scheduler_init(PDOScheduler *s, uint32_t sync_period_ms);
void pdo_scheduler_process(PDOScheduler *s, uint32_t now_ms);
void pdo_scheduler_on_rx(PDOScheduler *s, uint32_t can_id,
                         const uint8_t *data, uint8_t dlc,
                         uint32_t now_ms);
int  pdo_register_tpdo(PDOScheduler *s, uint32_t can_id,
                       uint8_t tx_type, uint16_t event_timer_ms);
int  pdo_register_rpdo(PDOScheduler *s, uint32_t can_id,
                       uint32_t timeout_ms);

#endif /* PDO_SCHEDULER_H */
```

```c
/* pdo_scheduler.c */
#include "pdo_scheduler.h"
#include "can_driver.h"
#include "rtos_mutex.h"
#include <string.h>

#define SYNC_COB_ID   0x80u

int pdo_scheduler_init(PDOScheduler *s, uint32_t sync_period_ms)
{
    memset(s, 0, sizeof(*s));
    s->sync_period_ms = sync_period_ms;
    s->mutex = rtos_mutex_create();
    return (s->mutex != NULL) ? 0 : -1;
}

/* Send SYNC and process synchronous TPDOs */
static void do_sync(PDOScheduler *s, uint32_t now_ms)
{
    uint8_t sync_frame[1] = { s->sync_counter };
    can_send(SYNC_COB_ID, sync_frame, 1);

    s->sync_counter = (s->sync_counter >= 240u) ? 1u : s->sync_counter + 1u;

    /* Fire synchronous TPDOs */
    for (uint8_t i = 0; i < s->tpdo_count; i++) {
        TPDO *t = &s->tpdos[i];
        bool fire = false;

        if (t->tx_type == 0) {
            fire = t->data_changed;
        } else if (t->tx_type >= 1u && t->tx_type <= 240u) {
            fire = ((s->sync_counter % t->tx_type) == 0u);
        }

        if (fire) {
            can_send(t->can_id, t->data, t->dlc);
            t->last_sent_tick = now_ms;
            t->data_changed   = false;
        }
    }

    s->next_sync_tick = now_ms + s->sync_period_ms;
}

void pdo_scheduler_process(PDOScheduler *s, uint32_t now_ms)
{
    rtos_mutex_lock(s->mutex);

    /* SYNC generation */
    if (now_ms >= s->next_sync_tick) {
        do_sync(s, now_ms);
    }

    /* Event-driven TPDOs (tx_type 254/255) */
    for (uint8_t i = 0; i < s->tpdo_count; i++) {
        TPDO *t = &s->tpdos[i];
        if (t->tx_type < 254u) continue;
        if (!t->data_changed)  continue;

        uint32_t inhibit_ms = (uint32_t)t->inhibit_time_100us / 10u;
        if ((now_ms - t->last_sent_tick) >= inhibit_ms) {
            can_send(t->can_id, t->data, t->dlc);
            t->last_sent_tick = now_ms;
            t->data_changed   = false;
        }
    }

    /* Check RPDO timeouts */
    for (uint8_t i = 0; i < s->rpdo_count; i++) {
        RPDO *r = &s->rpdos[i];
        if (r->timeout_ms == 0u) continue;
        if ((now_ms - r->last_rx_tick) > r->timeout_ms) {
            /* RPDO timeout: log / raise error */
            /* application hook: rpdo_timeout_callback(r->can_id); */
        }
    }

    rtos_mutex_unlock(s->mutex);
}

void pdo_scheduler_on_rx(PDOScheduler *s, uint32_t can_id,
                         const uint8_t *data, uint8_t dlc,
                         uint32_t now_ms)
{
    rtos_mutex_lock(s->mutex);
    for (uint8_t i = 0; i < s->rpdo_count; i++) {
        RPDO *r = &s->rpdos[i];
        if (r->can_id == can_id) {
            memcpy(r->data, data, dlc);
            r->dlc         = dlc;
            r->last_rx_tick = now_ms;
            r->fresh       = true;
            break;
        }
    }
    rtos_mutex_unlock(s->mutex);
}
```

---

## NMT Supervisor

### Concept

The NMT (Network Management) supervisor controls the operating state of every slave node. The master
initiates state transitions by broadcasting NMT command frames (CAN-ID 0x000). It also enforces the
network boot-up sequence and can restrict which nodes may enter the Operational state.

### NMT State Machine (per slave)

```
                  Power-on / Reset
                        |
                        v
               +--------+----------+
               | INITIALISATION    |
               | (Boot-up msg sent)|
               +--------+----------+
                        |  Boot-up frame received (0x700 + node_id, data=0x00)
                        v
        +---------------+------------------+
        |         PRE-OPERATIONAL          |<-----------+
        |  (SDO allowed, PDOs inhibited)   |            |
        +---------------+------------------+            |
                        |  NMT cmd: Start Node          |
                        v                               |
        +---------------+------------------+            |
        |           OPERATIONAL            |            |
        |  (SDO + PDO active)              +--Stop Node-+
        +---------------+------------------+            |
                        |                               |
                NMT cmd: Stop Node                      |
                        v                               |
        +---------------+------------------+            |
        |            STOPPED               +--Enter Pre-Op
        |  (only NMT rx, heartbeat tx)     +------------+
        +----------------------------------+
```

### C++ NMT Supervisor Implementation

```cpp
// nmt_supervisor.hpp
#pragma once
#include <cstdint>
#include <array>
#include <functional>

enum class NMTState : uint8_t {
    Unknown        = 0xFF,
    Initialisation = 0x00,
    Stopped        = 0x04,
    Operational    = 0x05,
    PreOperational = 0x7F
};

struct SlaveNMTEntry {
    uint8_t  node_id;
    NMTState state      = NMTState::Unknown;
    bool     mandatory  = false;  /* if true, master waits before going Operational */
    bool     boot_done  = false;
};

class NMTSupervisor {
public:
    using ErrorCallback = std::function<void(uint8_t node_id, NMTState expected,
                                             NMTState actual)>;

    explicit NMTSupervisor(ErrorCallback on_error)
        : on_error_(std::move(on_error)) {}

    void register_slave(uint8_t node_id, bool mandatory = true)
    {
        if (count_ < MAX_NODES) {
            nodes_[count_++] = {node_id, NMTState::Unknown, mandatory, false};
        }
    }

    /* Called when a boot-up or heartbeat frame arrives */
    void on_heartbeat(uint8_t node_id, NMTState reported_state)
    {
        for (auto &n : nodes_) {
            if (n.node_id != node_id) continue;
            if (!n.boot_done && reported_state == NMTState::PreOperational) {
                n.boot_done = true;
            }
            n.state = reported_state;
            break;
        }
    }

    /* Send NMT command to a node (or all nodes if node_id == 0) */
    void send_command(uint8_t node_id, uint8_t cs) const
    {
        uint8_t frame[2] = { cs, node_id };
        can_send(0x000u, frame, 2);
    }

    void start_node(uint8_t node_id)     { send_command(node_id, 0x01); }
    void stop_node(uint8_t node_id)      { send_command(node_id, 0x02); }
    void enter_pre_op(uint8_t node_id)   { send_command(node_id, 0x80); }
    void reset_node(uint8_t node_id)     { send_command(node_id, 0x81); }
    void reset_comms(uint8_t node_id)    { send_command(node_id, 0x82); }

    /* Returns true when all mandatory slaves are booted and pre-operational */
    bool all_mandatory_ready() const
    {
        for (uint8_t i = 0; i < count_; i++) {
            const auto &n = nodes_[i];
            if (n.mandatory && !n.boot_done) return false;
        }
        return true;
    }

    NMTState state_of(uint8_t node_id) const
    {
        for (uint8_t i = 0; i < count_; i++)
            if (nodes_[i].node_id == node_id)
                return nodes_[i].state;
        return NMTState::Unknown;
    }

private:
    static constexpr uint8_t MAX_NODES = 127;
    std::array<SlaveNMTEntry, MAX_NODES> nodes_{};
    uint8_t count_ = 0;
    ErrorCallback on_error_;
};
```

---

## Heartbeat Monitor

### Concept

The heartbeat protocol (CANopen object 0x1017 for producer, 0x1016 for consumers) allows nodes to
announce their health at a configured interval. If the master does not receive a heartbeat within
the expected window, it considers the slave failed and triggers recovery.

### Timing Diagram

```
Slave heartbeat period = T

  Slave TX:  |---HB---|---HB---|---HB---|   (missing)  ---HB---|
             |        |        |        |                       |
Master sees: HB       HB       HB       |<-- guard time -->|   HB

             <----T---> <--T--> <--T--> <--------fail--------->
                                             MISSED!
                                          on_heartbeat_lost()
                                          called here
```

### C++ Heartbeat Monitor

```cpp
// heartbeat_monitor.hpp
#pragma once
#include <cstdint>
#include <array>
#include <functional>

struct HeartbeatEntry {
    uint8_t  node_id       = 0;
    uint16_t period_ms     = 0;
    uint32_t deadline_ms   = 0;
    bool     active        = false;
    bool     lost          = false;
};

class HeartbeatMonitor {
public:
    using LostCB     = std::function<void(uint8_t node_id)>;
    using RecoverCB  = std::function<void(uint8_t node_id)>;

    HeartbeatMonitor(LostCB on_lost, RecoverCB on_recover)
        : on_lost_(std::move(on_lost))
        , on_recover_(std::move(on_recover)) {}

    /* Call once at startup for each slave to be monitored */
    void register_consumer(uint8_t node_id, uint16_t period_ms)
    {
        if (count_ < MAX_HB) {
            entries_[count_++] = {node_id, period_ms, 0u, true, false};
        }
    }

    /* Call from CAN receive path when a 0x700+node_id frame arrives */
    void on_heartbeat_rx(uint8_t node_id, uint32_t now_ms)
    {
        for (auto &e : entries_) {
            if (!e.active || e.node_id != node_id) continue;
            /* Extend deadline by 150% of period to allow jitter */
            e.deadline_ms = now_ms + e.period_ms + (e.period_ms / 2u);
            if (e.lost) {
                e.lost = false;
                on_recover_(node_id);
            }
            break;
        }
    }

    /* Call periodically from the master task */
    void process(uint32_t now_ms)
    {
        for (auto &e : entries_) {
            if (!e.active || e.lost) continue;
            if (e.deadline_ms != 0u && now_ms > e.deadline_ms) {
                e.lost = true;
                on_lost_(e.node_id);
            }
        }
    }

    bool is_lost(uint8_t node_id) const
    {
        for (uint8_t i = 0; i < count_; i++)
            if (entries_[i].node_id == node_id)
                return entries_[i].lost;
        return false;
    }

private:
    static constexpr uint8_t MAX_HB = 127;
    std::array<HeartbeatEntry, MAX_HB> entries_{};
    uint8_t count_ = 0;
    LostCB     on_lost_;
    RecoverCB  on_recover_;
};
```

---

## Configuration Manager Pattern

### Concept

The configuration manager is responsible for downloading a known-good configuration to each slave
at network startup, typically from an EDS (Electronic Data Sheet) or DCF (Device Configuration
File). It uses SDO writes to set PDO mappings, communication parameters, heartbeat periods, and
application-specific settings. The pattern is driven as a state machine to handle the sequential,
asynchronous nature of SDO transfers.

### Configuration State Machine

```
  +----------+     all entries      +-----------+
  |  IDLE    +------------------+-->|  DONE     |
  +----------+                  |   +-----------+
       |                        |
  node booted                   | last entry written
       |                        |
       v                        |
  +-----------+    callback     |
  | CONFIGURE +--OK----------->>+
  |  (write   |
  |  next SDO)|<--retry---------+
  +-----------+                 |
       |                        |
      ERR?                   timeout
       |                        |
       v                        |
  +-----------+                 |
  |   FAULT   +-----------------+
  +-----------+
```

### C++ Configuration Manager

```cpp
// config_manager.hpp
#pragma once
#include <cstdint>
#include <vector>
#include "sdo_manager.h"

struct ODEntry {
    uint16_t index;
    uint8_t  subindex;
    uint8_t  data[4];
    uint8_t  size;
};

enum class ConfigState {
    Idle, Configuring, Done, Fault
};

class ConfigManager {
public:
    ConfigManager(SDOManager *sdo_mgr)
        : sdo_(sdo_mgr) {}

    void load_configuration(uint8_t node_id,
                            const std::vector<ODEntry> &entries)
    {
        node_id_ = node_id;
        entries_ = entries;
        cursor_  = 0;
        state_   = ConfigState::Configuring;
        write_next();
    }

    ConfigState state() const { return state_; }

    /* SDO callback — drives the state machine forward */
    static void sdo_callback(uint8_t node_id, uint16_t index,
                             uint8_t subindex, int error_code,
                             void *user_data)
    {
        auto *self = static_cast<ConfigManager *>(user_data);
        if (error_code != 0) {
            /* Log: index/subindex write failed */
            self->state_ = ConfigState::Fault;
            return;
        }
        self->cursor_++;
        if (self->cursor_ >= self->entries_.size()) {
            self->state_ = ConfigState::Done;
        } else {
            self->write_next();
        }
    }

private:
    void write_next()
    {
        const ODEntry &e = entries_[cursor_];
        sdo_write(sdo_, node_id_,
                  e.index, e.subindex,
                  e.data, e.size,
                  sdo_callback, this);
    }

    SDOManager          *sdo_;
    uint8_t              node_id_ = 0;
    std::vector<ODEntry> entries_;
    size_t               cursor_  = 0;
    ConfigState          state_   = ConfigState::Idle;
};
```

### Typical Configuration Sequence for One Slave

```
Master                                             Slave (node 3)
  |                                                    |
  |-- SDO Write 0x1400.01 (RPDO1 COB-ID) ------------>|
  |<-- SDO Ack --------------------------------------- |
  |-- SDO Write 0x1400.02 (TX type = 1) ------------->|
  |<-- SDO Ack --------------------------------------- |
  |-- SDO Write 0x1600.00 (num mapped = 0) ---------->|
  |<-- SDO Ack --------------------------------------- |
  |-- SDO Write 0x1600.01 (mapping obj 1) ----------->|
  |<-- SDO Ack --------------------------------------- |
  |-- SDO Write 0x1600.00 (num mapped = 1) ---------->|
  |<-- SDO Ack --------------------------------------- |
  |-- SDO Write 0x1017    (heartbeat period 100ms) -->|
  |<-- SDO Ack --------------------------------------- |
  |-- NMT Start Node (0x01, 0x03) ------------------->|
  |                                                    |-- enters Operational
```

---

## Task/Thread Decomposition on RTOS

### Motivation

A monolithic polling loop cannot meet the competing timing requirements of CAN receive (latency-
critical), SYNC generation (jitter-critical), heartbeat monitoring (periodic), and SDO management
(blocking-tolerant). An RTOS task model separates these concerns with appropriate priorities.

### Recommended Task Architecture

```
Priority (high to low)
   |
   |  +---------------------------+
   |  | CAN RX ISR / Task         |  Priority: HIGHEST (real-time)
   |  | - DMA/interrupt driven    |  Period:   Event-driven
   |  | - Demux: PDO/SDO/HB/NMT   |  Stack:    512 bytes
   |  | - Post to queues          |
   |  +---------------------------+
   |
   |  +---------------------------+
   |  | SYNC + PDO Task           |  Priority: HIGH
   |  | - Generates SYNC frame    |  Period:   sync_period_ms (e.g. 10ms)
   |  | - Fires synchronous TPDOs |  Stack:    1 KB
   |  | - Checks event PDOs       |
   |  +---------------------------+
   |
   |  +---------------------------+
   |  | Heartbeat Monitor Task    |  Priority: MEDIUM-HIGH
   |  | - Checks HB deadlines     |  Period:   10 ms
   |  | - Calls recovery hooks    |  Stack:    512 bytes
   |  +---------------------------+
   |
   |  +---------------------------+
   |  | SDO Manager Task          |  Priority: MEDIUM
   |  | - Drives SDO state m/c    |  Period:   5 ms
   |  | - Sends queued requests   |  Stack:    1 KB
   |  | - Handles timeouts/retry  |
   |  +---------------------------+
   |
   |  +---------------------------+
   |  | NMT Supervisor Task       |  Priority: MEDIUM-LOW
   |  | - Monitors NMT states     |  Period:   20 ms
   |  | - Drives boot sequence    |  Stack:    1 KB
   v  +---------------------------+

   |  +---------------------------+
      | Config Manager Task       |  Priority: LOW
      | - Writes EDS config       |  Period:   On-demand (event-driven)
      | - Calls SDO Manager       |  Stack:    2 KB
      +---------------------------+

      +---------------------------+
      | Application Task          |  Priority: LOW
      | - Business logic          |  Period:   Application-defined
      | - Reads/writes process    |  Stack:    Application-defined
      |   image via OD            |
      +---------------------------+
```

### FreeRTOS Task Skeleton

```c
/* master_tasks.c — FreeRTOS task setup */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "sdo_manager.h"
#include "pdo_scheduler.h"
#include "heartbeat_monitor.h"
#include "nmt_supervisor.h"

#define SYNC_PERIOD_MS   10u
#define HB_CHECK_MS      10u
#define SDO_TICK_MS       5u
#define NMT_CHECK_MS     20u

/* Shared subsystem instances — access protected by mutexes within each module */
static SDOManager    g_sdo;
static PDOScheduler  g_pdo;

static QueueHandle_t g_can_rx_queue;

/* ---- CAN RX Task (highest priority) ---- */
static void task_can_rx(void *arg)
{
    (void)arg;
    CANFrame frame;
    for (;;) {
        /* Block until a frame arrives from the CAN driver ISR */
        if (xQueueReceive(g_can_rx_queue, &frame, portMAX_DELAY) == pdTRUE) {
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

            /* Demultiplex by COB-ID range */
            if ((frame.id & 0x780u) == 0x580u) {
                /* SDO response */
                sdo_manager_on_rx(&g_sdo,
                                  (uint8_t)(frame.id & 0x7Fu),
                                  frame.data, frame.dlc);
            } else if ((frame.id & 0x780u) == 0x700u) {
                /* Heartbeat / boot-up */
                /* heartbeat_monitor_on_rx(frame.id & 0x7F, frame.data[0], now); */
            } else {
                /* PDO */
                pdo_scheduler_on_rx(&g_pdo, frame.id,
                                    frame.data, frame.dlc, now);
            }
        }
    }
}

/* ---- SYNC + PDO Task ---- */
static void task_pdo_sync(void *arg)
{
    (void)arg;
    TickType_t xLastWake = xTaskGetTickCount();
    for (;;) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        pdo_scheduler_process(&g_pdo, now);
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SYNC_PERIOD_MS));
    }
}

/* ---- SDO Manager Task ---- */
static void task_sdo(void *arg)
{
    (void)arg;
    TickType_t xLastWake = xTaskGetTickCount();
    for (;;) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        sdo_manager_process(&g_sdo, now);
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SDO_TICK_MS));
    }
}

void master_tasks_create(void)
{
    sdo_manager_init(&g_sdo);
    pdo_scheduler_init(&g_pdo, SYNC_PERIOD_MS);

    g_can_rx_queue = xQueueCreate(32, sizeof(CANFrame));

    xTaskCreate(task_can_rx,   "CAN_RX",  512,  NULL, 5, NULL);
    xTaskCreate(task_pdo_sync, "PDO_SYNC",1024, NULL, 4, NULL);
    xTaskCreate(task_sdo,      "SDO_MGR", 1024, NULL, 3, NULL);
    /* Add NMT, HB, Config, Application tasks similarly */
}
```

---

## Re-entrant Object Dictionary Access

### Problem

The Object Dictionary (OD) is the central data store shared across all subsystems and tasks. Without
synchronisation, concurrent reads and writes lead to torn values, especially for multi-byte objects.

### Access Patterns

```
Task A (SDO write)     Task B (Application read)     Task C (PDO map)
      |                         |                          |
      +--lock(od_mutex)         |                          |
      |  write OD[0x6001.0]     +--lock(od_mutex)--WAIT   |
      |                         |    (blocked)             +--lock --WAIT
      +--unlock(od_mutex)       |                          |
                                +--read OD[0x6001.0]  OK  |
                                +--unlock                  |
                                                           +--lock OK
                                                           +--map data
                                                           +--unlock
```

### C++ Re-entrant OD Implementation

```cpp
// object_dictionary.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>

/* RTOS-portable mutex abstraction */
extern "C" {
    void *rtos_mutex_create(void);
    void  rtos_mutex_lock(void *);
    void  rtos_mutex_unlock(void *);
}

enum class ODAttr : uint8_t {
    RO = 0x01,
    WO = 0x02,
    RW = 0x03
};

struct ODVar {
    uint16_t index;
    uint8_t  subindex;
    uint8_t  size;       /* in bytes, 1-8 */
    ODAttr   attr;
    uint8_t  data[8];
};

class ObjectDictionary {
public:
    static constexpr uint16_t MAX_ENTRIES = 256;

    ObjectDictionary()
    {
        mutex_ = rtos_mutex_create();
        count_ = 0;
    }

    /* Register a variable — call before starting tasks */
    bool register_var(uint16_t index, uint8_t subindex,
                      uint8_t size, ODAttr attr,
                      const void *init_val = nullptr)
    {
        if (count_ >= MAX_ENTRIES) return false;
        ODVar &v   = vars_[count_++];
        v.index    = index;
        v.subindex = subindex;
        v.size     = size;
        v.attr     = attr;
        if (init_val)
            std::memcpy(v.data, init_val, size);
        else
            std::memset(v.data, 0, size);
        return true;
    }

    /* Thread-safe read — copies out up to *len bytes, updates *len */
    bool read(uint16_t index, uint8_t subindex,
              void *out, uint8_t *len) const
    {
        rtos_mutex_lock(mutex_);
        bool ok = false;
        for (uint16_t i = 0; i < count_; i++) {
            const ODVar &v = vars_[i];
            if (v.index != index || v.subindex != subindex) continue;
            if ((uint8_t)v.attr & (uint8_t)ODAttr::RO ||
                (uint8_t)v.attr & (uint8_t)ODAttr::RW) {
                uint8_t copy = (*len < v.size) ? *len : v.size;
                std::memcpy(out, v.data, copy);
                *len = copy;
                ok   = true;
            }
            break;
        }
        rtos_mutex_unlock(mutex_);
        return ok;
    }

    /* Thread-safe write */
    bool write(uint16_t index, uint8_t subindex,
               const void *in, uint8_t len)
    {
        rtos_mutex_lock(mutex_);
        bool ok = false;
        for (uint16_t i = 0; i < count_; i++) {
            ODVar &v = vars_[i];
            if (v.index != index || v.subindex != subindex) continue;
            if ((uint8_t)v.attr & (uint8_t)ODAttr::WO ||
                (uint8_t)v.attr & (uint8_t)ODAttr::RW) {
                uint8_t copy = (len < v.size) ? len : v.size;
                std::memcpy(v.data, in, copy);
                ok = true;
            }
            break;
        }
        rtos_mutex_unlock(mutex_);
        return ok;
    }

    /* Zero-copy access for PDO mapping (returns pointer under lock)  *
     * Caller MUST call release() after use — NOT suitable for slow   *
     * paths (use read/write instead).                                 */
    const uint8_t *acquire(uint16_t index, uint8_t subindex,
                            uint8_t *size_out)
    {
        rtos_mutex_lock(mutex_);
        for (uint16_t i = 0; i < count_; i++) {
            if (vars_[i].index == index &&
                vars_[i].subindex == subindex) {
                *size_out = vars_[i].size;
                return vars_[i].data;
            }
        }
        rtos_mutex_unlock(mutex_);
        return nullptr;   /* not found: mutex already released */
    }

    void release() { rtos_mutex_unlock(mutex_); }

private:
    mutable void *mutex_;
    ODVar   vars_[MAX_ENTRIES];
    uint16_t count_;
};
```

### Deadlock-Safe Usage Rule

```
Rule: Always acquire locks in the same order across all tasks.
      Never hold the OD mutex while calling into CAN driver or SDO manager.

   CORRECT:
     lock(od_mutex)
     copy data locally
     unlock(od_mutex)
     sdo_write(...)     <-- called without od_mutex held

   WRONG (potential deadlock):
     lock(od_mutex)
     sdo_write(...)     <-- SDO task may also try to lock od_mutex
```

---

## Putting It All Together — Integration Example

This example shows how all subsystems are wired together in a master's main initialisation and
boot-up sequence.

```cpp
// master_main.cpp
#include "object_dictionary.hpp"
#include "sdo_manager.h"
#include "pdo_scheduler.h"
#include "nmt_supervisor.hpp"
#include "heartbeat_monitor.hpp"
#include "config_manager.hpp"
#include <vector>

/* Global OD */
static ObjectDictionary g_od;

/* Slave configuration: node 3, an I/O module */
static const std::vector<ODEntry> slave3_config = {
    /* Stop slave PDO comms before remapping */
    { 0x1400, 0x01, {0x83, 0x02, 0x00, 0x00}, 4 }, /* RPDO1 COB-ID (disable) */
    { 0x1600, 0x00, {0x00, 0x00, 0x00, 0x00}, 1 }, /* clear mapping count */
    { 0x1600, 0x01, {0x08, 0x00, 0x01, 0x60}, 4 }, /* map 0x6001.00, 8 bits */
    { 0x1600, 0x00, {0x01, 0x00, 0x00, 0x00}, 1 }, /* set mapping count = 1 */
    { 0x1400, 0x01, {0x03, 0x02, 0x00, 0x00}, 4 }, /* RPDO1 COB-ID (enable) */
    { 0x1400, 0x02, {0x01, 0x00, 0x00, 0x00}, 1 }, /* TX type = SYNC */
    { 0x1017, 0x00, {0x64, 0x00, 0x00, 0x00}, 2 }, /* Heartbeat 100 ms */
};

static SDOManager    g_sdo;
static PDOScheduler  g_pdo;
static ConfigManager *g_config;

static NMTSupervisor *g_nmt;
static HeartbeatMonitor *g_hb;

static void on_hb_lost(uint8_t node_id) {
    /* e.g. enter safe state, log fault */
    g_nmt->reset_comms(node_id);
}

static void on_hb_recover(uint8_t node_id) {
    /* re-configure and restart */
    g_config->load_configuration(node_id, slave3_config);
}

void master_init(void)
{
    /* 1. Initialise OD with master's own objects */
    uint32_t sync_cycle = 10000; /* 10 000 µs = 10 ms */
    g_od.register_var(0x1005, 0x00, 4, ODAttr::RW, &sync_cycle);

    /* 2. Initialise communication subsystems */
    sdo_manager_init(&g_sdo);
    pdo_scheduler_init(&g_pdo, 10 /* ms */);

    /* 3. Wire up NMT supervisor */
    g_nmt = new NMTSupervisor([](uint8_t id, NMTState exp, NMTState act) {
        /* handle unexpected state change */
        (void)id; (void)exp; (void)act;
    });
    g_nmt->register_slave(3, /*mandatory=*/true);

    /* 4. Wire up heartbeat monitor */
    g_hb = new HeartbeatMonitor(on_hb_lost, on_hb_recover);
    g_hb->register_consumer(3, /*period_ms=*/100);

    /* 5. Configuration manager */
    g_config = new ConfigManager(&g_sdo);

    /* 6. Register PDOs */
    pdo_register_rpdo(&g_pdo, 0x0203, /*timeout_ms=*/50);  /* TPDO from node 3 */
    pdo_register_tpdo(&g_pdo, 0x0003, /*tx_type=*/1, 0);   /* RPDO to node 3, every SYNC */

    /* 7. Put all slaves in pre-op and configure */
    g_nmt->enter_pre_op(0); /* broadcast: all nodes to pre-op */

    /* Boot sequence runs in NMT task; when boot-up received,
       config manager is triggered, then Start Node is issued. */
}

/* Called by NMT task when slave boot-up frame received */
void on_slave_bootup(uint8_t node_id)
{
    g_nmt->on_heartbeat(node_id, NMTState::PreOperational);

    if (node_id == 3) {
        g_config->load_configuration(3, slave3_config);
        /* After config completes (ConfigState::Done), send Start Node */
    }
}

/* Called periodically by application to check config done and start network */
void master_poll(uint32_t now_ms)
{
    g_hb->process(now_ms);

    if (g_config->state() == ConfigState::Done) {
        if (g_nmt->all_mandatory_ready()) {
            g_nmt->start_node(0); /* start all nodes */
        }
    }
}
```

---

## Summary

The table below consolidates the key design decisions for each subsystem of a CANopen master.

```
+---------------------+------------------+---------------------------+------------------+
| Subsystem           | Core Function    | Key Design Pattern        | RTOS Priority    |
+---------------------+------------------+---------------------------+------------------+
| SDO Manager         | Remote OD r/w    | Queue + state machine     | Medium           |
|                     |                  | with timeout & retry      |                  |
+---------------------+------------------+---------------------------+------------------+
| PDO Scheduler       | Real-time data   | SYNC generator +          | High             |
|                     | exchange         | transmission-type engine  |                  |
+---------------------+------------------+---------------------------+------------------+
| NMT Supervisor      | Node lifecycle   | Per-slave state machine;  | Medium-low       |
|                     | control          | boot sequence gating      |                  |
+---------------------+------------------+---------------------------+------------------+
| Heartbeat Monitor   | Node health      | Deadline timer with       | Medium-high      |
|                     | detection        | jitter tolerance          |                  |
+---------------------+------------------+---------------------------+------------------+
| Configuration Mgr   | Slave EDS/DCF    | SDO-driven state machine  | Low (startup)    |
|                     | download         | with sequential writes    |                  |
+---------------------+------------------+---------------------------+------------------+
| Object Dictionary   | Shared data      | Mutex-protected per-entry | Shared resource  |
|                     | store            | read/write API            | (not a task)     |
+---------------------+------------------+---------------------------+------------------+
| CAN RX Task         | Frame demux      | ISR → queue → task        | Highest          |
|                     |                  | (zero-copy where possible)|                  |
+---------------------+------------------+---------------------------+------------------+
```

### Key Architectural Principles

**Separation of concerns** — Each subsystem owns exactly one responsibility. The SDO manager does
not know about heartbeats; the heartbeat monitor does not send NMT commands directly.

**Asynchronous, callback-driven** — SDO requests and configuration steps complete asynchronously
via callbacks, preventing any task from blocking the CAN receive path.

**Mutex discipline** — Every shared data structure is protected by its own mutex. Lock ordering is
strictly defined (OD mutex is always acquired last) to prevent deadlock.

**Fail-safe by design** — Missed heartbeats, SDO timeouts, and NMT state anomalies all route to
well-defined recovery procedures rather than silent failure.

**RTOS task granularity** — Tasks are split by timing criticality, not by functional module. The
CAN receive ISR/task is always the highest priority to avoid frame loss; configuration activity runs
at the lowest priority as it is startup-only and tolerant of delay.

**Portability** — CAN driver, mutex, and timer interfaces are wrapped in thin HAL layers so the
master stack is portable across FreeRTOS, RTEMS, POSIX, and bare-metal polling environments.

---

*Reference Standards: CiA 301 v4.2.0 (CANopen application layer), CiA 302 (CANopen manager and
slave boot-up), CiA 306 (EDS specification), ISO 11898-1 (CAN data link layer).*
