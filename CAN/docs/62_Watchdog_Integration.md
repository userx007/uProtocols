# 62. Watchdog Integration
## Coordinating Hardware and Software Watchdogs with CAN Communication Health Monitoring

---

## Table of Contents

1. [Introduction](#introduction)
2. [Watchdog Fundamentals](#watchdog-fundamentals)
3. [CAN Communication Health Monitoring](#can-communication-health-monitoring)
4. [Hardware Watchdog Integration](#hardware-watchdog-integration)
5. [Software Watchdog Design](#software-watchdog-design)
6. [Layered Watchdog Architecture](#layered-watchdog-architecture)
7. [Implementation in C/C++](#implementation-in-cc)
8. [Implementation in Rust](#implementation-in-rust)
9. [Failure Modes and Recovery Strategies](#failure-modes-and-recovery-strategies)
10. [Summary](#summary)

---

## Introduction

In safety-critical embedded systems—automotive ECUs, industrial controllers, medical devices, and robotics—reliable communication is not merely a convenience but a hard requirement. The Controller Area Network (CAN) bus is the dominant protocol for such systems, yet no communication medium is immune to failure. Bus-off conditions, message timeouts, corrupted frames, and software deadlocks can silently degrade or halt a system.

Watchdog timers are the primary mechanism used to detect and recover from software and hardware faults. Integrating watchdogs with CAN health monitoring creates a layered safety net: the system continuously asserts its own liveness, verifies the integrity of the communication channel, and escalates recovery actions proportionally to the severity of the fault.

This document covers the concepts, architecture, and concrete code examples for building robust watchdog-integrated CAN systems in both C/C++ and Rust.

---

## Watchdog Fundamentals

### Hardware Watchdog Timer (HWT)

A hardware watchdog timer is a countdown timer implemented in silicon, independent of the CPU. If the software fails to periodically reset ("kick" or "service") the timer before it reaches zero, the watchdog triggers a hardware reset of the microcontroller. This ensures recovery even from complete CPU lockups, infinite loops, or stack corruption.

Key properties:
- **Operates independently** of the main CPU clock domain in many MCUs.
- **Cannot be disabled** in safety-critical configurations (window watchdog, locked configuration).
- **Trigger action**: typically a full MCU reset, but can also generate an NMI interrupt first.
- **Timeout window**: commonly 1ms–several seconds, configurable.

### Window Watchdog (WWDT)

An advanced variant that defines both a minimum and maximum service window. Servicing too early or too late both trigger a reset. This detects runaway-fast loops as well as slow/stuck ones.

### Software Watchdog

A software watchdog is a monitoring construct in application code that tracks the liveness and correctness of individual tasks or subsystems. Multiple software watchdogs can monitor separate threads/tasks, and they collectively "vote" to service the hardware watchdog. If any monitored task fails to check in, the hardware watchdog is deliberately starved, causing a system reset.

---

## CAN Communication Health Monitoring

CAN health monitoring goes beyond checking whether the bus is electrically active. A comprehensive monitor evaluates:

### 1. Bus State Monitoring

The CAN controller maintains an internal error state machine:
- **Error Active**: Normal operation (TEC < 128, REC < 128).
- **Error Passive**: Reduced priority on error frames (TEC or REC ≥ 128).
- **Bus-Off**: Transmitter has detected too many errors (TEC ≥ 256); controller disconnects from the bus.

### 2. Message Timeout Detection

Safety-critical messages (e.g., torque demands, brake commands) must arrive within a defined period. Absence of an expected message within that window constitutes a communication fault.

### 3. Error Frame Counting

CAN hardware counters track Transmit Error Counter (TEC) and Receive Error Counter (REC). Rising counters indicate wiring faults, node mismatches, or bus contention.

### 4. Message Content Validity

Even a timely message may carry invalid data. Plausibility checks (value range, rate-of-change, rolling counters, CRC fields in the application layer) detect corrupted or stale content.

### 5. Bus Load Monitoring

Excessive bus utilization (>80%) degrades real-time behavior and can indicate a misbehaving node flooding the bus.

---

## Hardware Watchdog Integration

The fundamental contract between the hardware watchdog and the CAN subsystem:

> **The hardware watchdog shall only be serviced when the CAN subsystem has demonstrated health within the last monitoring cycle.**

This means:
- The HWT service function is gated behind a health check.
- CAN health state is a mandatory input to the kick decision.
- Bus-off, timeout, or excessive error counts block the kick and allow the HWT to expire.

### Timing Relationships

```
HWT Timeout:         |<------- T_wdt ------->|  (e.g., 100ms)
CAN Health Check:    |<-- T_can -->|           (e.g., 20ms, must fit within T_wdt)
Task Scheduler:      |<-T_task->|              (e.g., 10ms main cycle)

Safe margin:  T_can < T_wdt / 2   (allows at least one missed cycle)
```

---

## Software Watchdog Design

A software watchdog manager maintains a registry of monitored entities (tasks, CAN channels, protocol handlers). Each entity must check in on time. The manager feeds the hardware watchdog only when all entities are healthy.

### Entity States

```
HEALTHY  --> MISSED (one timeout)  --> FAULTED (threshold exceeded)
FAULTED  --> triggers HWT starvation and/or recovery action
```

### Check-in Mechanism

Each monitored task calls a check-in function at the end of its execution. The watchdog manager verifies:
1. The check-in occurred within the allowed interval.
2. The task reported a valid health status (not just that it ran, but that it ran correctly).

---

## Layered Watchdog Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                        │
│   [CAN Tx Task]  [CAN Rx Task]  [Protocol Handler]         │
│       │check-in      │check-in       │check-in              │
└───────┼──────────────┼───────────────┼──────────────────────┘
        │              │               │
┌───────▼──────────────▼───────────────▼──────────────────────┐
│              Software Watchdog Manager                      │
│   - Tracks per-entity timeouts                              │
│   - Aggregates health votes                                 │
│   - Manages recovery escalation                             │
│   - Gates HWT kick                                          │
└─────────────────────────┬───────────────────────────────────┘
                          │ kick (only if all healthy)
┌─────────────────────────▼───────────────────────────────────┐
│              Hardware Watchdog Timer                        │
│   - Independent countdown                                   │
│   - Resets MCU if not kicked in time                        │
└─────────────────────────────────────────────────────────────┘
```

---

## Implementation in C/C++

### 1. CAN Health Monitor

```c
/* can_health.h */
#ifndef CAN_HEALTH_H
#define CAN_HEALTH_H

#include <stdint.h>
#include <stdbool.h>

/* CAN controller error state (mirrors hardware state machine) */
typedef enum {
    CAN_STATE_ERROR_ACTIVE  = 0,
    CAN_STATE_ERROR_PASSIVE = 1,
    CAN_STATE_BUS_OFF       = 2,
    CAN_STATE_UNKNOWN       = 3
} can_bus_state_t;

/* Per-message health record */
typedef struct {
    uint32_t can_id;
    uint32_t timeout_ms;          /* Maximum allowed gap between messages  */
    uint32_t last_received_tick;  /* Timestamp of last good reception      */
    uint32_t missed_count;        /* Consecutive missed deadlines           */
    uint32_t missed_threshold;    /* Faults after this many misses          */
    bool     is_faulted;
} can_message_monitor_t;

/* Overall CAN channel health snapshot */
typedef struct {
    can_bus_state_t bus_state;
    uint8_t  tec;                 /* Transmit Error Counter (0-255)         */
    uint8_t  rec;                 /* Receive Error Counter (0-255)          */
    uint32_t bus_off_count;       /* Total bus-off events since power-on    */
    uint32_t error_frame_count;   /* Error frames in last monitoring window */
    bool     any_message_faulted;
    bool     channel_healthy;
} can_health_status_t;

void     can_health_init(void);
void     can_health_tick(uint32_t now_ms);   /* Call every scheduler cycle  */
void     can_health_message_received(uint32_t can_id, uint32_t now_ms);
bool     can_health_get_status(can_health_status_t *out);
void     can_health_update_bus_state(can_bus_state_t state,
                                     uint8_t tec, uint8_t rec);

#endif /* CAN_HEALTH_H */
```

```c
/* can_health.c */
#include "can_health.h"
#include "hal_can.h"     /* MCU-specific CAN register access */
#include <string.h>

#define MAX_MONITORED_MESSAGES  16
#define ERROR_FRAME_WINDOW_MS   1000u
#define ERROR_FRAME_THRESHOLD   50u     /* Errors per second = bus problem  */

static can_message_monitor_t s_monitors[MAX_MONITORED_MESSAGES];
static uint8_t               s_monitor_count = 0;
static can_health_status_t   s_status;
static uint32_t              s_error_frame_window_start = 0;

/* Register a CAN message ID for timeout monitoring */
void can_health_register_message(uint32_t can_id,
                                 uint32_t timeout_ms,
                                 uint32_t missed_threshold)
{
    if (s_monitor_count >= MAX_MONITORED_MESSAGES) return;

    can_message_monitor_t *m = &s_monitors[s_monitor_count++];
    m->can_id           = can_id;
    m->timeout_ms       = timeout_ms;
    m->last_received_tick = 0;
    m->missed_count     = 0;
    m->missed_threshold = missed_threshold;
    m->is_faulted       = false;
}

void can_health_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.bus_state      = CAN_STATE_UNKNOWN;
    s_status.channel_healthy = false;
}

/* Called by CAN RX interrupt or RX task upon message reception */
void can_health_message_received(uint32_t can_id, uint32_t now_ms)
{
    for (uint8_t i = 0; i < s_monitor_count; ++i) {
        if (s_monitors[i].can_id == can_id) {
            s_monitors[i].last_received_tick = now_ms;
            s_monitors[i].missed_count       = 0;
            s_monitors[i].is_faulted         = false;
            break;
        }
    }
}

/* Call periodically (e.g., every 10ms) from main scheduler */
void can_health_tick(uint32_t now_ms)
{
    /* --- Check message timeouts --- */
    bool any_faulted = false;
    for (uint8_t i = 0; i < s_monitor_count; ++i) {
        can_message_monitor_t *m = &s_monitors[i];

        /* Skip messages not yet received at all (startup grace) */
        if (m->last_received_tick == 0) continue;

        uint32_t age = now_ms - m->last_received_tick;
        if (age > m->timeout_ms) {
            m->missed_count++;
            if (m->missed_count >= m->missed_threshold) {
                m->is_faulted = true;
            }
        }
        if (m->is_faulted) any_faulted = true;
    }
    s_status.any_message_faulted = any_faulted;

    /* --- Refresh bus state from hardware registers --- */
    s_status.tec = hal_can_get_tec();
    s_status.rec = hal_can_get_rec();

    can_bus_state_t hw_state = hal_can_get_bus_state();
    if (hw_state == CAN_STATE_BUS_OFF && s_status.bus_state != CAN_STATE_BUS_OFF) {
        s_status.bus_off_count++;
    }
    s_status.bus_state = hw_state;

    /* --- Error frame rate check --- */
    if ((now_ms - s_error_frame_window_start) >= ERROR_FRAME_WINDOW_MS) {
        s_status.error_frame_count  = hal_can_get_and_clear_error_count();
        s_error_frame_window_start  = now_ms;
    }

    /* --- Determine overall channel health --- */
    s_status.channel_healthy =
        (s_status.bus_state    == CAN_STATE_ERROR_ACTIVE) &&
        (!s_status.any_message_faulted)                   &&
        (s_status.error_frame_count < ERROR_FRAME_THRESHOLD) &&
        (s_status.tec < 96u)   /* Well below error-passive threshold */  &&
        (s_status.rec < 96u);
}

bool can_health_get_status(can_health_status_t *out)
{
    if (!out) return false;
    *out = s_status;
    return s_status.channel_healthy;
}
```

---

### 2. Software Watchdog Manager

```c
/* sw_watchdog.h */
#ifndef SW_WATCHDOG_H
#define SW_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

#define SW_WDT_MAX_ENTITIES  8

typedef enum {
    WDT_ENTITY_HEALTHY  = 0,
    WDT_ENTITY_MISSED   = 1,
    WDT_ENTITY_FAULTED  = 2
} wdt_entity_state_t;

typedef struct {
    const char         *name;
    uint32_t            deadline_ms;      /* Must check in within this period */
    uint32_t            last_checkin_ms;
    uint32_t            missed_count;
    uint32_t            fault_threshold;
    wdt_entity_state_t  state;
} wdt_entity_t;

/* Returns an entity handle (index) */
int8_t  sw_wdt_register(const char *name,
                         uint32_t    deadline_ms,
                         uint32_t    fault_threshold);

/* Entities call this to report they are alive and healthy */
void    sw_wdt_checkin(int8_t handle, uint32_t now_ms);

/* Run every scheduler tick; returns true if all entities healthy */
bool    sw_wdt_tick(uint32_t now_ms);

/* Query individual entity state */
wdt_entity_state_t sw_wdt_get_entity_state(int8_t handle);

#endif /* SW_WATCHDOG_H */
```

```c
/* sw_watchdog.c */
#include "sw_watchdog.h"
#include <string.h>

static wdt_entity_t s_entities[SW_WDT_MAX_ENTITIES];
static int8_t       s_entity_count = 0;

int8_t sw_wdt_register(const char *name,
                        uint32_t    deadline_ms,
                        uint32_t    fault_threshold)
{
    if (s_entity_count >= SW_WDT_MAX_ENTITIES) return -1;

    wdt_entity_t *e    = &s_entities[s_entity_count];
    e->name            = name;
    e->deadline_ms     = deadline_ms;
    e->last_checkin_ms = 0;
    e->missed_count    = 0;
    e->fault_threshold = fault_threshold;
    e->state           = WDT_ENTITY_HEALTHY;

    return s_entity_count++;
}

void sw_wdt_checkin(int8_t handle, uint32_t now_ms)
{
    if (handle < 0 || handle >= s_entity_count) return;
    wdt_entity_t *e    = &s_entities[handle];
    e->last_checkin_ms = now_ms;
    e->missed_count    = 0;
    e->state           = WDT_ENTITY_HEALTHY;
}

bool sw_wdt_tick(uint32_t now_ms)
{
    bool all_healthy = true;

    for (int8_t i = 0; i < s_entity_count; ++i) {
        wdt_entity_t *e = &s_entities[i];

        /* Grace period: not yet checked in at startup */
        if (e->last_checkin_ms == 0) continue;

        uint32_t age = now_ms - e->last_checkin_ms;
        if (age > e->deadline_ms) {
            e->missed_count++;
            if (e->missed_count >= e->fault_threshold) {
                e->state = WDT_ENTITY_FAULTED;
            } else {
                e->state = WDT_ENTITY_MISSED;
            }
        }

        if (e->state != WDT_ENTITY_HEALTHY) {
            all_healthy = false;
        }
    }

    return all_healthy;
}

wdt_entity_state_t sw_wdt_get_entity_state(int8_t handle)
{
    if (handle < 0 || handle >= s_entity_count) return WDT_ENTITY_FAULTED;
    return s_entities[handle].state;
}
```

---

### 3. Hardware Watchdog HAL and Integration

```c
/* hw_watchdog.h */
#ifndef HW_WATCHDOG_H
#define HW_WATCHDOG_H

#include <stdint.h>

void hw_wdt_init(uint32_t timeout_ms);
void hw_wdt_kick(void);   /* Resets the hardware countdown */

#endif

/* hw_watchdog.c  (example: STM32 IWDG) */
#include "hw_watchdog.h"
#include "stm32xx_hal.h"

static IWDG_HandleTypeDef s_iwdg;

void hw_wdt_init(uint32_t timeout_ms)
{
    /* LSI clock ~32kHz, prescaler /256 → ~125Hz tick → 8ms per count */
    s_iwdg.Instance       = IWDG;
    s_iwdg.Init.Prescaler = IWDG_PRESCALER_256;
    s_iwdg.Init.Reload    = (uint32_t)((timeout_ms * 125u) / 1000u);
    s_iwdg.Init.Window    = IWDG_WINDOW_DISABLE;  /* Basic mode, no window */

    HAL_IWDG_Init(&s_iwdg);
}

void hw_wdt_kick(void)
{
    HAL_IWDG_Refresh(&s_iwdg);
}
```

```c
/* main_watchdog_integration.c
 * Brings together CAN health, software watchdog, and hardware watchdog.
 */
#include "can_health.h"
#include "sw_watchdog.h"
#include "hw_watchdog.h"
#include "can_task.h"
#include "protocol_handler.h"

#define HWT_TIMEOUT_MS      100u   /* Hardware watchdog fires after 100ms  */
#define SCHEDULER_TICK_MS   10u    /* Main task runs every 10ms            */

static int8_t s_wdt_can_rx;
static int8_t s_wdt_can_tx;
static int8_t s_wdt_protocol;

void system_init(void)
{
    hw_wdt_init(HWT_TIMEOUT_MS);
    can_health_init();

    /* Register CAN messages to monitor (ID, timeout_ms, missed threshold) */
    can_health_register_message(0x100, 50,  3);   /* Safety-critical, 50ms period  */
    can_health_register_message(0x200, 100, 2);   /* Control setpoint, 100ms period */
    can_health_register_message(0x300, 500, 1);   /* Heartbeat from master node     */

    /* Register software watchdog entities */
    s_wdt_can_rx   = sw_wdt_register("CAN_RX",   30,  3);
    s_wdt_can_tx   = sw_wdt_register("CAN_TX",   30,  3);
    s_wdt_protocol = sw_wdt_register("PROTOCOL", 50,  2);
}

/* Called from main scheduler every SCHEDULER_TICK_MS */
void watchdog_supervisor_tick(uint32_t now_ms)
{
    /* 1. Update CAN health state */
    can_health_tick(now_ms);
    can_health_status_t can_status;
    bool can_ok = can_health_get_status(&can_status);

    /* 2. Evaluate software watchdog entities */
    bool sw_ok = sw_wdt_tick(now_ms);

    /* 3. Only kick HWT if both CAN channel and all tasks are healthy */
    if (can_ok && sw_ok) {
        hw_wdt_kick();
    }
    /* If either fails, HWT is starved and will reset the MCU
     * within HWT_TIMEOUT_MS - current_count_down_remaining */

    /* 4. Optional: log or trigger graduated recovery */
    if (!can_ok && can_status.bus_state == CAN_STATE_BUS_OFF) {
        can_initiate_bus_off_recovery();   /* Request bus-off recovery sequence */
    }
}

/* Tasks call sw_wdt_checkin() at end of each cycle */
void can_rx_task(uint32_t now_ms)
{
    /* ... receive and process CAN frames ... */
    can_health_message_received(0x100, now_ms);

    sw_wdt_checkin(s_wdt_can_rx, now_ms);   /* Report healthy completion */
}

void can_tx_task(uint32_t now_ms)
{
    /* ... build and transmit CAN frames ... */
    sw_wdt_checkin(s_wdt_can_tx, now_ms);
}

void protocol_handler_task(uint32_t now_ms)
{
    /* ... decode protocol, validate content ... */
    sw_wdt_checkin(s_wdt_protocol, now_ms);
}
```

---

### 4. Bus-Off Recovery in C

```c
/* can_recovery.c */
#include "can_recovery.h"
#include "hal_can.h"

#define BUS_OFF_RECOVERY_DELAY_MS   200u   /* Wait before attempting recovery */
#define MAX_RECOVERY_ATTEMPTS       3u

static uint8_t  s_recovery_attempts = 0;
static uint32_t s_recovery_start_ms = 0;
static bool     s_recovery_pending  = false;

void can_initiate_bus_off_recovery(void)
{
    if (!s_recovery_pending) {
        s_recovery_pending  = true;
        s_recovery_start_ms = hal_get_tick_ms();
    }
}

/* Call from scheduler; returns true when recovery is complete */
bool can_recovery_tick(uint32_t now_ms)
{
    if (!s_recovery_pending) return true;

    if ((now_ms - s_recovery_start_ms) < BUS_OFF_RECOVERY_DELAY_MS) {
        return false;   /* Still waiting */
    }

    if (s_recovery_attempts >= MAX_RECOVERY_ATTEMPTS) {
        /* Exhausted retries: enter safe state, let WDT reset the system */
        hal_can_disable();
        return false;
    }

    /* Attempt recovery: re-initialize CAN controller */
    hal_can_reset();
    hal_can_init();
    s_recovery_attempts++;
    s_recovery_pending = false;

    return true;
}
```

---

## Implementation in Rust

Rust's ownership model, type system, and `no_std` support make it well-suited for embedded watchdog integration. The examples below target a bare-metal embedded environment using `no_std`.

### 1. CAN Health Types and Monitor

```rust
// can_health.rs
#![allow(dead_code)]

use core::sync::atomic::{AtomicU32, Ordering};

/// Mirrors the CAN controller hardware error state machine.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BusState {
    ErrorActive,
    ErrorPassive,
    BusOff,
    Unknown,
}

/// Configuration for monitoring a single CAN message ID.
pub struct MessageMonitorConfig {
    pub can_id:           u32,
    pub timeout_ms:       u32,
    pub missed_threshold: u32,
}

/// Runtime state for a monitored message.
struct MessageMonitor {
    config:              MessageMonitorConfig,
    last_received_ms:    u32,
    missed_count:        u32,
    is_faulted:          bool,
}

/// Snapshot of overall CAN channel health.
#[derive(Debug, Clone, Copy)]
pub struct CanHealthStatus {
    pub bus_state:            BusState,
    pub tec:                  u8,
    pub rec:                  u8,
    pub bus_off_count:        u32,
    pub error_frame_count:    u32,
    pub any_message_faulted:  bool,
    pub channel_healthy:      bool,
}

impl CanHealthStatus {
    const fn default_unhealthy() -> Self {
        CanHealthStatus {
            bus_state:           BusState::Unknown,
            tec:                 0,
            rec:                 0,
            bus_off_count:       0,
            error_frame_count:   0,
            any_message_faulted: false,
            channel_healthy:     false,
        }
    }
}

const MAX_MONITORS: usize = 16;
const ERROR_FRAME_THRESHOLD: u32 = 50;
const TEC_REC_HEALTHY_MAX: u8 = 96;

pub struct CanHealthMonitor {
    monitors:                [Option<MessageMonitor>; MAX_MONITORS],
    monitor_count:           usize,
    status:                  CanHealthStatus,
    error_window_start_ms:   u32,
    error_frame_accumulator: u32,
}

impl CanHealthMonitor {
    pub const fn new() -> Self {
        // Option<T> is not Copy for non-Copy T, so we initialize manually
        CanHealthMonitor {
            monitors: [
                None, None, None, None, None, None, None, None,
                None, None, None, None, None, None, None, None,
            ],
            monitor_count:           0,
            status:                  CanHealthStatus::default_unhealthy(),
            error_window_start_ms:   0,
            error_frame_accumulator: 0,
        }
    }

    pub fn register_message(&mut self, config: MessageMonitorConfig) -> bool {
        if self.monitor_count >= MAX_MONITORS {
            return false;
        }
        self.monitors[self.monitor_count] = Some(MessageMonitor {
            config,
            last_received_ms: 0,
            missed_count:     0,
            is_faulted:       false,
        });
        self.monitor_count += 1;
        true
    }

    /// Called by CAN RX path when a message is received.
    pub fn on_message_received(&mut self, can_id: u32, now_ms: u32) {
        for slot in self.monitors[..self.monitor_count].iter_mut() {
            if let Some(m) = slot {
                if m.config.can_id == can_id {
                    m.last_received_ms = now_ms;
                    m.missed_count     = 0;
                    m.is_faulted       = false;
                    break;
                }
            }
        }
    }

    /// Update bus error counters from hardware.
    pub fn update_bus_state(&mut self, state: BusState, tec: u8, rec: u8) {
        if state == BusState::BusOff && self.status.bus_state != BusState::BusOff {
            self.status.bus_off_count += 1;
        }
        self.status.bus_state = state;
        self.status.tec       = tec;
        self.status.rec       = rec;
    }

    /// Increment error frame counter (call from CAN error interrupt).
    pub fn on_error_frame(&mut self) {
        self.error_frame_accumulator += 1;
    }

    /// Periodic health evaluation. Returns true if channel is healthy.
    pub fn tick(&mut self, now_ms: u32) -> bool {
        // Evaluate message timeouts
        let mut any_faulted = false;
        for slot in self.monitors[..self.monitor_count].iter_mut() {
            if let Some(m) = slot {
                if m.last_received_ms == 0 {
                    continue; // startup grace
                }
                let age = now_ms.wrapping_sub(m.last_received_ms);
                if age > m.config.timeout_ms {
                    m.missed_count += 1;
                    if m.missed_count >= m.config.missed_threshold {
                        m.is_faulted = true;
                    }
                }
                if m.is_faulted {
                    any_faulted = true;
                }
            }
        }
        self.status.any_message_faulted = any_faulted;

        // Rotate error frame window
        if now_ms.wrapping_sub(self.error_window_start_ms) >= 1000 {
            self.status.error_frame_count    = self.error_frame_accumulator;
            self.error_frame_accumulator     = 0;
            self.error_window_start_ms       = now_ms;
        }

        // Determine channel health
        self.status.channel_healthy =
            self.status.bus_state          == BusState::ErrorActive &&
            !self.status.any_message_faulted                         &&
            self.status.error_frame_count  <  ERROR_FRAME_THRESHOLD  &&
            self.status.tec                <  TEC_REC_HEALTHY_MAX    &&
            self.status.rec                <  TEC_REC_HEALTHY_MAX;

        self.status.channel_healthy
    }

    pub fn status(&self) -> &CanHealthStatus {
        &self.status
    }
}
```

---

### 2. Software Watchdog Manager in Rust

```rust
// sw_watchdog.rs

/// State of a monitored entity.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EntityState {
    Healthy,
    Missed,
    Faulted,
}

struct WdtEntity {
    name:            &'static str,
    deadline_ms:     u32,
    fault_threshold: u32,
    last_checkin_ms: u32,
    missed_count:    u32,
    state:           EntityState,
}

const MAX_ENTITIES: usize = 8;

pub struct SoftwareWatchdog {
    entities:     [Option<WdtEntity>; MAX_ENTITIES],
    entity_count: usize,
}

/// Opaque handle returned on registration.
#[derive(Clone, Copy, Debug)]
pub struct WdtHandle(u8);

impl SoftwareWatchdog {
    pub const fn new() -> Self {
        SoftwareWatchdog {
            entities: [
                None, None, None, None, None, None, None, None,
            ],
            entity_count: 0,
        }
    }

    /// Register a new entity. Returns None if capacity exceeded.
    pub fn register(
        &mut self,
        name:            &'static str,
        deadline_ms:     u32,
        fault_threshold: u32,
    ) -> Option<WdtHandle> {
        if self.entity_count >= MAX_ENTITIES {
            return None;
        }
        let idx = self.entity_count;
        self.entities[idx] = Some(WdtEntity {
            name,
            deadline_ms,
            fault_threshold,
            last_checkin_ms: 0,
            missed_count:    0,
            state:           EntityState::Healthy,
        });
        self.entity_count += 1;
        Some(WdtHandle(idx as u8))
    }

    /// Entity reports it completed its work cycle successfully.
    pub fn checkin(&mut self, handle: WdtHandle, now_ms: u32) {
        let idx = handle.0 as usize;
        if let Some(Some(e)) = self.entities.get_mut(idx) {
            e.last_checkin_ms = now_ms;
            e.missed_count    = 0;
            e.state           = EntityState::Healthy;
        }
    }

    /// Evaluate all entities. Returns true only when all are Healthy.
    pub fn tick(&mut self, now_ms: u32) -> bool {
        let mut all_healthy = true;

        for slot in self.entities[..self.entity_count].iter_mut() {
            if let Some(e) = slot {
                if e.last_checkin_ms == 0 {
                    continue; // startup grace
                }
                let age = now_ms.wrapping_sub(e.last_checkin_ms);
                if age > e.deadline_ms {
                    e.missed_count += 1;
                    e.state = if e.missed_count >= e.fault_threshold {
                        EntityState::Faulted
                    } else {
                        EntityState::Missed
                    };
                }
                if e.state != EntityState::Healthy {
                    all_healthy = false;
                }
            }
        }
        all_healthy
    }

    pub fn entity_state(&self, handle: WdtHandle) -> EntityState {
        let idx = handle.0 as usize;
        self.entities
            .get(idx)
            .and_then(|s| s.as_ref())
            .map(|e| e.state)
            .unwrap_or(EntityState::Faulted)
    }
}
```

---

### 3. Hardware Watchdog HAL Trait and Integration

```rust
// hw_watchdog.rs

/// Abstract trait for a hardware watchdog timer.
/// Implement this for your specific MCU peripheral.
pub trait HardwareWatchdog {
    /// Reload/reset the watchdog countdown.
    fn kick(&mut self);
}

/// Example implementation for an STM32-style IWDG
/// using embedded-hal register access patterns.
pub struct Stm32Iwdg {
    // Would hold a reference to the IWDG peripheral registers
    // Here represented as a phantom for illustration
    _private: (),
}

impl Stm32Iwdg {
    pub fn new(timeout_ms: u32) -> Self {
        // In real code: configure IWDG prescaler and reload registers
        let _ = timeout_ms;
        Stm32Iwdg { _private: () }
    }
}

impl HardwareWatchdog for Stm32Iwdg {
    fn kick(&mut self) {
        // Write reload key to IWDG_KR register: 0xAAAA
        // unsafe { (*stm32::IWDG::ptr()).kr.write(|w| w.key().bits(0xAAAA)) };
    }
}

// ─── Supervisor ──────────────────────────────────────────────────────────────

use crate::can_health::CanHealthMonitor;
use crate::sw_watchdog::{SoftwareWatchdog, WdtHandle};

pub struct WatchdogSupervisor<HWT: HardwareWatchdog> {
    hw_wdt:       HWT,
    can_monitor:  CanHealthMonitor,
    sw_wdt:       SoftwareWatchdog,
}

impl<HWT: HardwareWatchdog> WatchdogSupervisor<HWT> {
    pub fn new(hw_wdt: HWT) -> Self {
        WatchdogSupervisor {
            hw_wdt,
            can_monitor:  CanHealthMonitor::new(),
            sw_wdt:       SoftwareWatchdog::new(),
        }
    }

    pub fn can_monitor_mut(&mut self) -> &mut CanHealthMonitor {
        &mut self.can_monitor
    }

    pub fn sw_wdt_mut(&mut self) -> &mut SoftwareWatchdog {
        &mut self.sw_wdt
    }

    /// Call every scheduler tick (e.g., 10ms).
    /// Returns true if system is healthy and HWT was kicked.
    pub fn tick(&mut self, now_ms: u32) -> bool {
        let can_ok = self.can_monitor.tick(now_ms);
        let sw_ok  = self.sw_wdt.tick(now_ms);

        if can_ok && sw_ok {
            self.hw_wdt.kick();
            true
        } else {
            // HWT is deliberately starved; reset will occur within timeout
            false
        }
    }
}
```

---

### 4. Full Integration Example in Rust

```rust
// main.rs  (bare-metal, no_std)
#![no_std]
#![no_main]

mod can_health;
mod sw_watchdog;
mod hw_watchdog;

use can_health::MessageMonitorConfig;
use hw_watchdog::{HardwareWatchdog, Stm32Iwdg, WatchdogSupervisor};
use sw_watchdog::WdtHandle;

static mut SUPERVISOR: Option<WatchdogSupervisor<Stm32Iwdg>> = None;
static mut WDT_CAN_RX:   Option<WdtHandle> = None;
static mut WDT_CAN_TX:   Option<WdtHandle> = None;
static mut WDT_PROTOCOL: Option<WdtHandle> = None;

#[no_mangle]
pub extern "C" fn main() -> ! {
    let iwdg = Stm32Iwdg::new(100 /* ms */);
    let mut supervisor = WatchdogSupervisor::new(iwdg);

    // Register CAN messages for health monitoring
    supervisor.can_monitor_mut().register_message(MessageMonitorConfig {
        can_id: 0x100, timeout_ms: 50,  missed_threshold: 3,
    });
    supervisor.can_monitor_mut().register_message(MessageMonitorConfig {
        can_id: 0x200, timeout_ms: 100, missed_threshold: 2,
    });

    // Register software watchdog entities
    let h_rx  = supervisor.sw_wdt_mut()
        .register("CAN_RX",   30, 3).unwrap();
    let h_tx  = supervisor.sw_wdt_mut()
        .register("CAN_TX",   30, 3).unwrap();
    let h_prot = supervisor.sw_wdt_mut()
        .register("PROTOCOL", 50, 2).unwrap();

    // Store globally (single-threaded bare-metal context)
    unsafe {
        SUPERVISOR   = Some(supervisor);
        WDT_CAN_RX   = Some(h_rx);
        WDT_CAN_TX   = Some(h_tx);
        WDT_PROTOCOL = Some(h_prot);
    }

    let mut tick_ms: u32 = 0;

    loop {
        let now = tick_ms;

        // Run application tasks
        can_rx_task(now);
        can_tx_task(now);
        protocol_task(now);

        // Run watchdog supervisor (gates HWT kick)
        unsafe {
            if let Some(ref mut sup) = SUPERVISOR {
                sup.tick(now);
            }
        }

        // Simulate 10ms tick (replace with real timer wait)
        tick_ms = tick_ms.wrapping_add(10);
        busy_wait_10ms();
    }
}

fn can_rx_task(now_ms: u32) {
    // ... receive and process CAN frames ...
    unsafe {
        if let (Some(ref mut sup), Some(h)) = (SUPERVISOR.as_mut(), WDT_CAN_RX) {
            sup.can_monitor_mut().on_message_received(0x100, now_ms);
            sup.sw_wdt_mut().checkin(h, now_ms);
        }
    }
}

fn can_tx_task(now_ms: u32) {
    // ... transmit CAN frames ...
    unsafe {
        if let (Some(ref mut sup), Some(h)) = (SUPERVISOR.as_mut(), WDT_CAN_TX) {
            sup.sw_wdt_mut().checkin(h, now_ms);
        }
    }
}

fn protocol_task(now_ms: u32) {
    // ... decode protocol ...
    unsafe {
        if let (Some(ref mut sup), Some(h)) = (SUPERVISOR.as_mut(), WDT_PROTOCOL) {
            sup.sw_wdt_mut().checkin(h, now_ms);
        }
    }
}

fn busy_wait_10ms() {
    // Replace with SysTick or hardware timer delay
    for _ in 0..80_000u32 { core::hint::black_box(()); }
}
```

---

## Failure Modes and Recovery Strategies

| Failure Mode | Detection | Recovery Action |
|---|---|---|
| CAN Bus-Off | `bus_state == BusOff` | Delay + re-initialize CAN controller; count retries |
| Message Timeout | Age > `timeout_ms` × `missed_threshold` | Flag fault; escalate to safe state or reset |
| Rising TEC/REC | TEC or REC > 96 (approaching passive) | Log warning; check wiring; reduce bus load |
| Task Deadlock | SW WDT entity `FAULTED` | Block HWT kick → system reset |
| High Error Frame Rate | `error_frame_count > threshold/sec` | Log; initiate bus diagnostic routine |
| All Monitors Pass but Content Invalid | Application-layer CRC/plausibility fail | Report message as not received to health monitor |

### Recovery Escalation Ladder

```
Level 0:  Warning log only                  (TEC rising, occasional timeout)
Level 1:  Safe-state outputs                (message faulted, first bus-off)
Level 2:  CAN controller re-initialization  (bus-off, ≤ max retries)
Level 3:  ECU soft reset via WDT starvation (task faulted, recovery exhausted)
Level 4:  Manufacturing/diagnostic mode     (repeated resets detected via reset cause register)
```

---

## Summary

Watchdog integration in CAN systems is a multi-layered discipline that spans hardware, drivers, and application software:

**Hardware Watchdog Timer** provides the last line of defense—a MCU-level reset mechanism that operates independently of software. It must only be kicked when the entire system asserts its health.

**Software Watchdog Manager** provides per-task liveness monitoring. Every critical task must check in within its deadline. Failure of any single entity blocks the hardware watchdog kick, causing a controlled system reset.

**CAN Health Monitor** evaluates the communication channel comprehensively: bus state (Error Active / Error Passive / Bus-Off), per-message timeout tracking, error frame rate, and hardware error counters (TEC/REC). Channel health is a mandatory input to the kick decision.

**Integration Pattern**: The watchdog supervisor aggregates software entity states and CAN channel health every scheduler cycle. The hardware watchdog is kicked only on unanimous health confirmation. Any fault causes deliberate HWT starvation.

**C/C++** implementations are idiomatic for existing AUTOSAR or bare-metal codebases, using static allocation and function-pointer-free designs compatible with MISRA-C.

**Rust** implementations leverage the type system to prevent misuse—typed handles, trait-abstracted hardware, and ownership semantics ensure the watchdog kick path cannot be bypassed by accident.

Together, these layers implement a defense-in-depth strategy that satisfies the requirements of ISO 26262 ASIL-B and above for CAN communication monitoring and fault response.

---

*Document: CAN Topic 62 — Watchdog Integration*
*Covers: C/C++ and Rust, bare-metal embedded, safety-critical systems*