# 97. Command Queue Architecture

**Introduction** — why queued command systems outperform blocking SPI calls, with a comparison table and typical use cases (displays, sensors, flash, audio, motor drivers).

**Core Concepts** — the command descriptor anatomy, four queue variants (FIFO ring, priority heap, double-buffer, etc.), and the dispatcher state machine diagram.

**C/C++ Implementation** — four complete examples:
1. Lock-free ring buffer (bare-metal C, SPSC with memory barriers)
2. DMA-driven dispatcher state machine with retry logic and zero-gap chaining
3. C++ `std::priority_queue` wrapper with mutex for multi-producer environments
4. Write coalescing — merging adjacent same-device writes before DMA launch

**Rust Implementation** — five examples:
1. `SpiCommand` descriptor with `bitflags` and raw-pointer safety contracts
2. `no_std` lock-free ring buffer using `AtomicUsize` and `MaybeUninit`
3. Dispatcher state machine with ISR-safe atomic signalling
4. `heapless::mpmc` queue with embassy async task integration
5. `BinaryHeap`-based priority queue with unit tests

**Advanced Patterns** — double-buffered descriptor lists, CS_HOLD chaining for atomic multi-step sequences, and DMA watchdog/timeout handling.

**Summary** — design decision table, language trade-off analysis, and performance notes.

## Designing Queued Command Systems for Efficient SPI Device Control

---

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Queue Data Structures](#queue-data-structures)
4. [C/C++ Implementation](#cc-implementation)
5. [Rust Implementation](#rust-implementation)
6. [Advanced Patterns](#advanced-patterns)
7. [Summary](#summary)

---

## Introduction

SPI (Serial Peripheral Interface) communication is inherently sequential and blocking at the bus level: only one transaction can occur at a time. In naive implementations, each SPI operation forces the CPU to block while bytes are clocked out — a pattern that wastes cycles, introduces unpredictable latency, and makes real-time scheduling difficult.

A **Command Queue Architecture** decouples *command creation* from *command execution*. Instead of calling a send function and waiting, the application enqueues a command descriptor and continues. A dedicated dispatcher — driven by DMA completion interrupts, a scheduler tick, or a state machine — drains the queue and executes transfers on behalf of all callers.

### Why It Matters

| Without Queue | With Command Queue |
|---|---|
| Caller blocks per transfer | Caller enqueues and returns immediately |
| Hard to batch small transfers | Batching and coalescing is natural |
| Priority handling is ad-hoc | Priority field per command |
| DMA underutilized | DMA pipelines back-to-back |
| Error retry logic scattered | Centralized retry/callback |

### Typical Use Cases

- **Display controllers** (e.g. ST7789, ILI9341): pixel pushes queued during rendering, flushed in one DMA burst
- **Sensor hubs**: multiple sensors share one SPI bus; queue arbitrates access
- **Flash/EEPROM**: write-then-read sequences queued atomically
- **Audio DAC/ADC**: streaming sample blocks with deadline requirements
- **Motor drivers**: high-frequency torque updates mixed with low-frequency config writes

---

## Core Concepts

### Command Descriptor

Every queue entry is a self-contained *command descriptor* that carries all information needed to execute the transfer independently:

```
┌──────────────────────────────────────┐
│  Command Descriptor                  │
│  ─────────────────                   │
│  device_id   : which CS line         │
│  tx_buf      : pointer to send data  │
│  rx_buf      : pointer for recv data │
│  length      : byte count            │
│  callback    : completion function   │
│  user_data   : caller context        │
│  priority    : 0 (high) … N (low)    │
│  flags       : CS_HOLD, NO_DMA, …    │
│  retries     : remaining retries     │
└──────────────────────────────────────┘
```

### Queue Variants

**FIFO Queue** — simplest, first-in-first-out, no priority.

**Priority Queue (min-heap)** — higher-priority commands jump the queue. Used when latency guarantees differ per device.

**Ring Buffer** — fixed-size circular buffer with head/tail pointers. Zero dynamic allocation, ideal for embedded targets.

**Double-Buffered Queue** — two alternating buffers: one fills while the other drains. Eliminates head-of-line blocking.

### Dispatcher States

```
         ┌──────────┐    command available    ┌──────────────┐
         │   IDLE   │ ──────────────────────► │  DEQUEUE &   │
         │          │                         │  SETUP DMA   │
         └──────────┘                         └──────┬───────┘
               ▲                                     │ DMA started
               │  queue empty                        ▼
         ┌─────┴──────┐    DMA done ISR       ┌──────────────┐
         │  CALLBACK  │ ◄──────────────────── │  TRANSFERRING│
         │  & RETIRE  │                       │              │
         └────────────┘                       └──────────────┘
```

---

## Queue Data Structures

### Ring Buffer (Lock-Free, Single Producer / Single Consumer)

```c
#define SPI_QUEUE_DEPTH  32   /* must be power of 2 */
#define SPI_QUEUE_MASK   (SPI_QUEUE_DEPTH - 1)

typedef void (*spi_callback_t)(void *user_data, int status);

typedef struct {
    uint8_t        device_id;
    const uint8_t *tx_buf;
    uint8_t       *rx_buf;
    uint16_t       length;
    spi_callback_t callback;
    void          *user_data;
    uint8_t        priority;
    uint8_t        retries;
    uint16_t       flags;
} spi_command_t;

typedef struct {
    spi_command_t  buf[SPI_QUEUE_DEPTH];
    volatile uint32_t head;   /* written by producer */
    volatile uint32_t tail;   /* written by consumer */
} spi_ring_queue_t;
```

---

## C/C++ Implementation

### 1. Ring Buffer Queue — Bare Metal C

```c
/* spi_queue.h */
#ifndef SPI_QUEUE_H
#define SPI_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#define SPI_QUEUE_DEPTH  32
#define SPI_QUEUE_MASK   (SPI_QUEUE_DEPTH - 1)

/* Flags */
#define SPI_FLAG_CS_HOLD   (1u << 0)  /* keep CS asserted after transfer */
#define SPI_FLAG_NO_DMA    (1u << 1)  /* force polled transfer */
#define SPI_FLAG_WRITE_ONLY (1u << 2) /* ignore rx_buf */

typedef void (*spi_callback_t)(void *user_data, int status);

typedef struct {
    uint8_t        device_id;
    const uint8_t *tx_buf;
    uint8_t       *rx_buf;
    uint16_t       length;
    spi_callback_t callback;
    void          *user_data;
    uint8_t        priority;
    uint8_t        retries_remaining;
    uint16_t       flags;
} spi_command_t;

typedef struct {
    spi_command_t    entries[SPI_QUEUE_DEPTH];
    volatile uint32_t head;
    volatile uint32_t tail;
} spi_ring_queue_t;

void spi_queue_init(spi_ring_queue_t *q);
bool spi_queue_push(spi_ring_queue_t *q, const spi_command_t *cmd);
bool spi_queue_pop (spi_ring_queue_t *q, spi_command_t *cmd);
bool spi_queue_empty(const spi_ring_queue_t *q);
uint32_t spi_queue_count(const spi_ring_queue_t *q);

#endif /* SPI_QUEUE_H */
```

```c
/* spi_queue.c */
#include "spi_queue.h"
#include <string.h>

/* Memory barrier macro — replace with your CPU's intrinsic */
#ifdef __ARM_ARCH
  #define DMB()  __asm__ volatile ("dmb" ::: "memory")
#else
  #define DMB()  __asm__ volatile ("" ::: "memory")
#endif

void spi_queue_init(spi_ring_queue_t *q)
{
    memset(q, 0, sizeof(*q));
}

/*
 * Lock-free push: safe for a single producer calling from thread/task context.
 * If the queue is full, the command is dropped and false is returned.
 */
bool spi_queue_push(spi_ring_queue_t *q, const spi_command_t *cmd)
{
    uint32_t next = (q->head + 1u) & SPI_QUEUE_MASK;
    if (next == q->tail) {
        return false;   /* queue full */
    }
    q->entries[q->head & SPI_QUEUE_MASK] = *cmd;
    DMB();
    q->head = next;
    return true;
}

/*
 * Lock-free pop: safe for a single consumer (ISR or dispatcher task).
 */
bool spi_queue_pop(spi_ring_queue_t *q, spi_command_t *cmd)
{
    if (q->tail == q->head) {
        return false;   /* queue empty */
    }
    *cmd = q->entries[q->tail & SPI_QUEUE_MASK];
    DMB();
    q->tail = (q->tail + 1u) & SPI_QUEUE_MASK;
    return true;
}

bool spi_queue_empty(const spi_ring_queue_t *q)
{
    return q->tail == q->head;
}

uint32_t spi_queue_count(const spi_ring_queue_t *q)
{
    return (q->head - q->tail) & SPI_QUEUE_MASK;
}
```

---

### 2. Dispatcher — DMA-Driven State Machine

```c
/* spi_dispatcher.h */
#ifndef SPI_DISPATCHER_H
#define SPI_DISPATCHER_H

#include "spi_queue.h"

typedef enum {
    DISP_IDLE,
    DISP_TRANSFERRING,
    DISP_CALLBACK,
    DISP_RETRY_DELAY,
} disp_state_t;

typedef struct {
    spi_ring_queue_t  *queue;
    spi_command_t      active;        /* command currently on the bus */
    disp_state_t       state;
    uint32_t           error_count;
} spi_dispatcher_t;

void spi_dispatcher_init(spi_dispatcher_t *d, spi_ring_queue_t *q);
void spi_dispatcher_poll(spi_dispatcher_t *d);   /* call from task or SysTick */
void spi_dispatcher_dma_done_isr(spi_dispatcher_t *d, int status); /* call from DMA ISR */

#endif
```

```c
/* spi_dispatcher.c */
#include "spi_dispatcher.h"
#include "hal_spi.h"   /* your platform HAL */
#include <string.h>

#define MAX_RETRIES  3

void spi_dispatcher_init(spi_dispatcher_t *d, spi_ring_queue_t *q)
{
    memset(d, 0, sizeof(*d));
    d->queue = q;
    d->state = DISP_IDLE;
}

static void start_transfer(spi_dispatcher_t *d)
{
    spi_command_t *c = &d->active;
    hal_spi_cs_assert(c->device_id);

    if (c->flags & SPI_FLAG_NO_DMA) {
        /* Polled fallback — blocks until complete */
        int status = hal_spi_transfer_polled(c->tx_buf, c->rx_buf, c->length);
        hal_spi_cs_deassert(c->device_id);
        spi_dispatcher_dma_done_isr(d, status);
    } else {
        d->state = DISP_TRANSFERRING;
        hal_spi_transfer_dma(c->tx_buf, c->rx_buf, c->length);
        /* DMA ISR will call spi_dispatcher_dma_done_isr() when done */
    }
}

/*
 * Poll from cooperative task or SysTick.
 * Kicks off next transfer when dispatcher is idle.
 */
void spi_dispatcher_poll(spi_dispatcher_t *d)
{
    if (d->state != DISP_IDLE) return;

    if (spi_queue_pop(d->queue, &d->active)) {
        start_transfer(d);
    }
}

/*
 * Called from DMA completion ISR.
 * status == 0: success; status < 0: error code.
 */
void spi_dispatcher_dma_done_isr(spi_dispatcher_t *d, int status)
{
    spi_command_t *c = &d->active;

    if (!(c->flags & SPI_FLAG_CS_HOLD)) {
        hal_spi_cs_deassert(c->device_id);
    }

    if (status != 0 && c->retries_remaining > 0) {
        /* Retry: re-queue the command with decremented retry count */
        c->retries_remaining--;
        d->error_count++;
        d->state = DISP_IDLE;
        spi_queue_push(d->queue, c);  /* re-enqueue at tail */
        return;
    }

    /* Fire completion callback from ISR context */
    if (c->callback) {
        c->callback(c->user_data, status);
    }

    d->state = DISP_IDLE;

    /* Immediately chain next command if available (zero-gap DMA chaining) */
    if (!spi_queue_empty(d->queue)) {
        spi_queue_pop(d->queue, &d->active);
        start_transfer(d);
    }
}
```

---

### 3. C++ Priority Queue with std::priority_queue

```cpp
// spi_priority_queue.hpp
#pragma once
#include <cstdint>
#include <functional>
#include <queue>
#include <mutex>

struct SpiCommand {
    uint8_t                     device_id;
    const uint8_t              *tx_buf;
    uint8_t                    *rx_buf;
    uint16_t                    length;
    std::function<void(int)>    callback;
    uint8_t                     priority;   /* lower value = higher priority */
    uint8_t                     retries;
    uint16_t                    flags;

    /* Comparator: lower priority value wins */
    bool operator>(const SpiCommand &rhs) const {
        return priority > rhs.priority;
    }
};

class SpiCommandQueue {
public:
    void enqueue(SpiCommand cmd) {
        std::lock_guard<std::mutex> lk(mutex_);
        pq_.push(std::move(cmd));
    }

    bool dequeue(SpiCommand &out) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (pq_.empty()) return false;
        out = pq_.top();
        pq_.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return pq_.empty();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return pq_.size();
    }

private:
    mutable std::mutex mutex_;
    std::priority_queue<SpiCommand,
                        std::vector<SpiCommand>,
                        std::greater<SpiCommand>> pq_;
};
```

```cpp
// Example usage
#include "spi_priority_queue.hpp"
#include <cstdio>

SpiCommandQueue g_queue;

static uint8_t tx_display[] = {0x2C, 0xFF, 0x00, 0xFF};
static uint8_t tx_sensor[]  = {0x0B};
static uint8_t rx_sensor[6] = {};

void on_display_done(int status) {
    printf("[display] transfer done, status=%d\n", status);
}

void on_sensor_done(int status) {
    printf("[sensor] accel data ready, status=%d\n", status);
}

int main() {
    /* Low-priority display pixel burst */
    g_queue.enqueue(SpiCommand{
        .device_id = 0,
        .tx_buf    = tx_display,
        .rx_buf    = nullptr,
        .length    = sizeof(tx_display),
        .callback  = on_display_done,
        .priority  = 10,
        .retries   = 0,
        .flags     = 0,
    });

    /* High-priority sensor read (interrupts display updates) */
    g_queue.enqueue(SpiCommand{
        .device_id = 1,
        .tx_buf    = tx_sensor,
        .rx_buf    = rx_sensor,
        .length    = sizeof(tx_sensor),
        .callback  = on_sensor_done,
        .priority  = 1,
        .retries   = 2,
        .flags     = 0,
    });

    /* Dispatcher loop */
    SpiCommand cmd;
    while (g_queue.dequeue(cmd)) {
        printf("Executing cmd: device=%u len=%u prio=%u\n",
               cmd.device_id, cmd.length, cmd.priority);
        /* ... hand off to HAL / DMA ... */
        if (cmd.callback) cmd.callback(0);
    }
    return 0;
}
```

---

### 4. Coalescing — Merging Adjacent Write Commands

A common optimization for display drivers: merge multiple small writes to the same device into a single DMA burst.

```c
/*
 * spi_coalesce: if the next queued command is a contiguous write to the
 * same device, extend the active command's length.
 * Precondition: active and next both have SPI_FLAG_WRITE_ONLY set.
 * Returns number of bytes added (0 if no coalescing possible).
 */
uint16_t spi_coalesce(spi_dispatcher_t *d)
{
    spi_command_t peek;
    uint16_t added = 0;

    while (!spi_queue_empty(d->queue)) {
        /* Peek without consuming */
        if (!spi_queue_peek(d->queue, &peek)) break;

        bool same_device  = (peek.device_id == d->active.device_id);
        bool both_write   = (peek.flags & SPI_FLAG_WRITE_ONLY) &&
                            (d->active.flags & SPI_FLAG_WRITE_ONLY);
        bool contiguous   = (peek.tx_buf ==
                             d->active.tx_buf + d->active.length);

        if (!same_device || !both_write || !contiguous) break;

        spi_queue_pop(d->queue, &peek);  /* consume */
        d->active.length += peek.length;
        added += peek.length;
    }
    return added;
}
```

---

## Rust Implementation

### 1. Queue Types and Command Descriptor

```rust
// spi_queue/mod.rs

use core::sync::atomic::{AtomicU32, Ordering};

pub type SpiCallback = fn(user_data: *mut (), status: i32);

bitflags::bitflags! {
    #[derive(Clone, Copy, Debug, Default)]
    pub struct SpiFlags: u16 {
        const CS_HOLD    = 0b0000_0001;
        const NO_DMA     = 0b0000_0010;
        const WRITE_ONLY = 0b0000_0100;
    }
}

#[derive(Clone, Debug)]
pub struct SpiCommand {
    pub device_id:  u8,
    pub tx_buf:     *const u8,
    pub tx_len:     usize,
    pub rx_buf:     *mut u8,
    pub rx_len:     usize,
    pub callback:   Option<SpiCallback>,
    pub user_data:  *mut (),
    pub priority:   u8,       // 0 = highest
    pub retries:    u8,
    pub flags:      SpiFlags,
}

// SAFETY: SpiCommand contains raw pointers; callers must ensure
// buffer lifetimes exceed the command's time in the queue.
unsafe impl Send for SpiCommand {}
unsafe impl Sync for SpiCommand {}
```

---

### 2. Lock-Free Ring Buffer (no_std compatible)

```rust
// spi_queue/ring.rs

use core::cell::UnsafeCell;
use core::mem::MaybeUninit;
use core::sync::atomic::{AtomicUsize, Ordering};
use super::SpiCommand;

const QUEUE_DEPTH: usize = 32; // must be power of 2

pub struct SpiRingQueue {
    buf:  [UnsafeCell<MaybeUninit<SpiCommand>>; QUEUE_DEPTH],
    head: AtomicUsize,  // producer index
    tail: AtomicUsize,  // consumer index
}

unsafe impl Sync for SpiRingQueue {}
unsafe impl Send for SpiRingQueue {}

impl SpiRingQueue {
    pub const fn new() -> Self {
        // SAFETY: MaybeUninit array is safe to zero-init
        Self {
            buf:  unsafe { MaybeUninit::zeroed().assume_init() },
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    /// Push from producer context (task / thread).
    /// Returns `Err(cmd)` if full.
    pub fn push(&self, cmd: SpiCommand) -> Result<(), SpiCommand> {
        let head = self.head.load(Ordering::Relaxed);
        let next = (head + 1) & (QUEUE_DEPTH - 1);

        if next == self.tail.load(Ordering::Acquire) {
            return Err(cmd); // full
        }

        // SAFETY: head slot is exclusively owned by producer until head advances
        unsafe {
            (*self.buf[head].get()).write(cmd);
        }
        self.head.store(next, Ordering::Release);
        Ok(())
    }

    /// Pop from consumer context (ISR / dispatcher).
    pub fn pop(&self) -> Option<SpiCommand> {
        let tail = self.tail.load(Ordering::Relaxed);
        if tail == self.head.load(Ordering::Acquire) {
            return None; // empty
        }

        // SAFETY: tail slot is ready — producer released it above
        let cmd = unsafe { (*self.buf[tail].get()).assume_init_read() };
        self.tail.store((tail + 1) & (QUEUE_DEPTH - 1), Ordering::Release);
        Some(cmd)
    }

    pub fn is_empty(&self) -> bool {
        self.tail.load(Ordering::Acquire) == self.head.load(Ordering::Acquire)
    }

    pub fn len(&self) -> usize {
        let h = self.head.load(Ordering::Acquire);
        let t = self.tail.load(Ordering::Acquire);
        (h.wrapping_sub(t)) & (QUEUE_DEPTH - 1)
    }
}
```

---

### 3. Dispatcher State Machine

```rust
// spi_dispatcher.rs

use core::sync::atomic::{AtomicBool, Ordering};
use super::spi_queue::ring::SpiRingQueue;
use super::spi_queue::{SpiCommand, SpiFlags};

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum DispatcherState {
    Idle,
    Transferring,
    RetryPending,
}

pub struct SpiDispatcher {
    queue:        &'static SpiRingQueue,
    active:       Option<SpiCommand>,
    state:        DispatcherState,
    error_count:  u32,
    dma_done:     AtomicBool,  // set by ISR
    dma_status:   i32,
}

impl SpiDispatcher {
    pub fn new(queue: &'static SpiRingQueue) -> Self {
        Self {
            queue,
            active:      None,
            state:       DispatcherState::Idle,
            error_count: 0,
            dma_done:    AtomicBool::new(false),
            dma_status:  0,
        }
    }

    /// Call from task loop or SysTick to advance state machine.
    pub fn poll(&mut self) {
        match self.state {
            DispatcherState::Idle => self.try_start_next(),

            DispatcherState::Transferring => {
                if self.dma_done.swap(false, Ordering::Acquire) {
                    let status = self.dma_status;
                    self.handle_completion(status);
                }
            }

            DispatcherState::RetryPending => {
                // In a real system, wait for a backoff timer here
                if let Some(cmd) = self.active.take() {
                    self.state = DispatcherState::Idle;
                    let _ = self.queue.push(cmd); // re-queue
                }
            }
        }
    }

    /// Called from DMA IRQ handler (must be fast, no allocation).
    /// # Safety
    /// Must only be called from the DMA completion interrupt.
    pub unsafe fn dma_irq_handler(&self, status: i32) {
        // SAFETY: only this ISR writes dma_status before setting dma_done
        let ptr = &self.dma_status as *const i32 as *mut i32;
        ptr.write_volatile(status);
        self.dma_done.store(true, Ordering::Release);
    }

    // ── Private ────────────────────────────────────────────────

    fn try_start_next(&mut self) {
        if let Some(cmd) = self.queue.pop() {
            self.active = Some(cmd);
            self.start_dma();
        }
    }

    fn start_dma(&mut self) {
        if let Some(cmd) = &self.active {
            hal_cs_assert(cmd.device_id);
            if cmd.flags.contains(SpiFlags::NO_DMA) {
                let status = hal_transfer_polled(cmd.tx_buf, cmd.tx_len,
                                                 cmd.rx_buf, cmd.rx_len);
                hal_cs_deassert(cmd.device_id);
                self.handle_completion(status);
            } else {
                self.state = DispatcherState::Transferring;
                hal_transfer_dma(cmd.tx_buf, cmd.tx_len,
                                 cmd.rx_buf, cmd.rx_len);
            }
        }
    }

    fn handle_completion(&mut self, status: i32) {
        if let Some(mut cmd) = self.active.take() {
            if !cmd.flags.contains(SpiFlags::CS_HOLD) {
                hal_cs_deassert(cmd.device_id);
            }

            if status != 0 && cmd.retries > 0 {
                cmd.retries -= 1;
                self.error_count += 1;
                self.active = Some(cmd);
                self.state = DispatcherState::RetryPending;
                return;
            }

            // Fire callback
            if let Some(cb) = cmd.callback {
                cb(cmd.user_data, status);
            }

            self.state = DispatcherState::Idle;

            // Chain immediately to next command
            self.try_start_next();
        }
    }
}

// ── Stub HAL (replace with real platform HAL) ─────────────────
fn hal_cs_assert(_dev: u8) {}
fn hal_cs_deassert(_dev: u8) {}
fn hal_transfer_polled(_tx: *const u8, _tl: usize,
                       _rx: *mut u8,   _rl: usize) -> i32 { 0 }
fn hal_transfer_dma   (_tx: *const u8, _tl: usize,
                       _rx: *mut u8,   _rl: usize) {}
```

---

### 4. Safe High-Level API with `heapless` (no_std)

Using the `heapless` crate for embedded targets:

```rust
// spi_safe_queue.rs — safe wrapper using heapless::mpmc::Q32
#![no_std]

use heapless::mpmc::Q32;
use crate::SpiCommand;

pub struct SafeSpiQueue {
    inner: Q32<SpiCommand>,
}

impl SafeSpiQueue {
    pub const fn new() -> Self {
        Self { inner: Q32::new() }
    }

    /// Non-blocking enqueue. Returns `Err` if full.
    pub fn enqueue(&self, cmd: SpiCommand) -> Result<(), SpiCommand> {
        self.inner.enqueue(cmd).map_err(|e| e)
    }

    /// Non-blocking dequeue.
    pub fn dequeue(&self) -> Option<SpiCommand> {
        self.inner.dequeue()
    }
}
```

```rust
// RTOS integration example using embassy-executor
#[embassy_executor::task]
async fn spi_dispatcher_task(
    queue: &'static SafeSpiQueue,
    mut spi: Spi<'static, SPI1, DMA1_CH3, DMA1_CH2>,
) {
    loop {
        if let Some(cmd) = queue.dequeue() {
            let result = spi.transfer(
                unsafe { core::slice::from_raw_parts_mut(cmd.rx_buf, cmd.rx_len) },
                unsafe { core::slice::from_raw_parts(cmd.tx_buf, cmd.tx_len) },
            ).await;

            let status = result.map(|_| 0i32).unwrap_or(-1);
            if let Some(cb) = cmd.callback {
                cb(cmd.user_data, status);
            }
        } else {
            // Yield to avoid busy-waiting
            embassy_futures::yield_now().await;
        }
    }
}
```

---

### 5. Rust Priority Queue using BinaryHeap

```rust
// spi_priority_queue.rs — std environment or alloc feature

use std::collections::BinaryHeap;
use std::cmp::Ordering;
use std::sync::{Arc, Mutex};
use crate::SpiCommand;

/// Newtype that reverses ordering so lower priority value = higher urgency
#[derive(Eq, PartialEq)]
struct PriorityCmd(SpiCommand);

impl PartialOrd for PriorityCmd {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for PriorityCmd {
    fn cmp(&self, other: &Self) -> Ordering {
        // Reverse: smaller priority number wins (max-heap becomes min-heap)
        other.0.priority.cmp(&self.0.priority)
    }
}

impl PartialEq for SpiCommand {
    fn eq(&self, other: &Self) -> bool {
        self.priority == other.priority
    }
}
impl Eq for SpiCommand {}

pub struct SpiPriorityQueue {
    heap: Arc<Mutex<BinaryHeap<PriorityCmd>>>,
}

impl SpiPriorityQueue {
    pub fn new() -> Self {
        Self { heap: Arc::new(Mutex::new(BinaryHeap::new())) }
    }

    pub fn enqueue(&self, cmd: SpiCommand) {
        self.heap.lock().unwrap().push(PriorityCmd(cmd));
    }

    pub fn dequeue(&self) -> Option<SpiCommand> {
        self.heap.lock().unwrap().pop().map(|w| w.0)
    }

    pub fn len(&self) -> usize {
        self.heap.lock().unwrap().len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::SpiFlags;

    fn make_cmd(prio: u8) -> SpiCommand {
        SpiCommand {
            device_id: 0,
            tx_buf:    core::ptr::null(),
            tx_len:    0,
            rx_buf:    core::ptr::null_mut(),
            rx_len:    0,
            callback:  None,
            user_data: core::ptr::null_mut(),
            priority:  prio,
            retries:   0,
            flags:     SpiFlags::empty(),
        }
    }

    #[test]
    fn highest_priority_dequeued_first() {
        let q = SpiPriorityQueue::new();
        q.enqueue(make_cmd(5));
        q.enqueue(make_cmd(1));
        q.enqueue(make_cmd(3));

        assert_eq!(q.dequeue().unwrap().priority, 1); // lowest value = highest priority
        assert_eq!(q.dequeue().unwrap().priority, 3);
        assert_eq!(q.dequeue().unwrap().priority, 5);
    }
}
```

---

## Advanced Patterns

### Double-Buffered Descriptor List

For display controllers that send large frame chunks, a double-buffer approach eliminates DMA idle gaps:

```
Buffer A: [cmd0][cmd1][cmd2]  ←── filling (CPU)
Buffer B: [cmd3][cmd4]        ←── draining (DMA)

On DMA done:
  swap A ↔ B
  start DMA on new B immediately
  CPU begins filling new A
```

```c
/* Minimal double-buffer descriptor example */
#define CHUNK_DEPTH 16

typedef struct {
    spi_command_t cmds[CHUNK_DEPTH];
    uint8_t       count;
} spi_chunk_t;

typedef struct {
    spi_chunk_t   chunks[2];
    volatile uint8_t fill_idx;  /* CPU fills this one */
    volatile uint8_t drain_idx; /* DMA drains this one */
} spi_double_buf_t;

void spi_double_buf_swap(spi_double_buf_t *db) {
    uint8_t tmp   = db->fill_idx;
    db->fill_idx  = db->drain_idx;
    db->drain_idx = tmp;
    db->chunks[db->fill_idx].count = 0; /* reset fill buffer */
}
```

---

### Sequence Commands (Atomic Multi-Transfer)

Some devices require multi-step transactions (e.g., write command byte, assert CS, write data) without releasing CS between steps. Chain commands using `SPI_FLAG_CS_HOLD`:

```c
static uint8_t cmd_byte  = 0x02;    /* write command */
static uint8_t addr[3]   = {0x00, 0x10, 0x00};
static uint8_t data[256];           /* payload */

/* Enqueue as a sequence — CS stays asserted between steps */
spi_command_t seq[3] = {
    { .device_id=0, .tx_buf=&cmd_byte, .length=1,
      .flags=SPI_FLAG_WRITE_ONLY | SPI_FLAG_CS_HOLD },
    { .device_id=0, .tx_buf=addr,      .length=3,
      .flags=SPI_FLAG_WRITE_ONLY | SPI_FLAG_CS_HOLD },
    { .device_id=0, .tx_buf=data,      .length=256,
      .flags=SPI_FLAG_WRITE_ONLY,          /* last: release CS */
      .callback=on_flash_write_done },
};

for (int i = 0; i < 3; i++) {
    spi_queue_push(&g_queue, &seq[i]);
}
```

---

### Timeout and Watchdog

```c
/* Add to dispatcher: detect DMA hang */
#define DMA_TIMEOUT_MS  50

typedef struct {
    uint32_t transfer_start_ms;
} spi_watchdog_t;

void spi_watchdog_kick(spi_watchdog_t *w) {
    w->transfer_start_ms = hal_get_tick_ms();
}

bool spi_watchdog_expired(const spi_watchdog_t *w) {
    return (hal_get_tick_ms() - w->transfer_start_ms) > DMA_TIMEOUT_MS;
}

/* In dispatcher poll loop */
if (d->state == DISP_TRANSFERRING && spi_watchdog_expired(&d->wdog)) {
    hal_spi_abort_dma();
    spi_dispatcher_dma_done_isr(d, -ETIMEDOUT);
}
```

---

## Summary

### Architecture Overview

A Command Queue Architecture for SPI replaces synchronous blocking calls with an asynchronous descriptor pipeline. Commands are enqueued with all transfer metadata (buffers, length, device ID, callback, priority, retry count, flags) and executed by a dispatcher that drives the DMA engine without CPU intervention.

### Key Design Decisions

| Decision | Recommendation |
|---|---|
| Queue type | Ring buffer for fixed-priority FIFO; binary heap for priority ordering |
| Memory model | Static/no_std ring buffer for bare-metal; std BinaryHeap or heapless for RTOS |
| Concurrency | Lock-free SPSC ring for ISR/task split; mutex-guarded heap for multi-producer |
| CS management | Per-command `CS_HOLD` flag enables atomic multi-step sequences |
| Error handling | Per-command retry counter; centralized error count in dispatcher |
| DMA chaining | Re-arm DMA immediately on completion ISR for zero-gap throughput |
| Coalescing | Merge contiguous writes to same device before DMA start |
| Watchdog | Timeout per transfer; abort + retry on DMA hang |

### Language Trade-offs

**C/C++** gives full control over memory layout and ISR integration. The ring buffer is straightforward to implement with memory barriers. C++ adds the priority queue and `std::function` callbacks for richer composition, at the cost of heap use.

**Rust** enforces ownership discipline at compile time: the `Send`/`Sync` bounds on `SpiCommand` make buffer lifetime hazards explicit. The `heapless` crate provides zero-allocation queues for `no_std` targets. The `embassy` async executor turns the dispatcher into an `async fn`, automatically yielding on empty queue without a polling loop.

### Performance Notes

- A 32-entry ring buffer with DMA chaining can sustain near-100% SPI bus utilization on a Cortex-M4 at 20 MHz clock with zero CPU involvement per byte.
- Priority queue overhead (heap push/pop) is O(log N) — negligible for N ≤ 64 typical in embedded systems.
- Coalescing display writes can reduce DMA setup overhead by 10× for high-framerate applications.

---

*End of Document — Topic 97: Command Queue Architecture*