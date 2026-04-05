# 15. Circular Buffers — Lock-Free Ring Buffers for UART Data Queuing

**Theory** — how head/tail indices work, the three indexing schemes (modulo, bitmask, free-running), and why unsigned wrapping arithmetic is the cleanest approach.

**Lock-free design** — the two atomic guarantees needed for safe ISR ↔ task communication without disabling interrupts: hardware-atomic index accesses + `release`/`acquire` memory ordering.

**C/C++ examples (5 variants):**
- Basic SPSC ring buffer with `<stdatomic.h>`
- ISR-safe UART RX buffer (no `__disable_irq()` needed)
- TX buffer with DMA kick-off and staging
- Power-of-two `static_assert` enforcement
- MPMC variant using CAS loops for RTOS environments

**Rust examples (3 variants):**
- `no_std` SPSC ring buffer using `core::sync::atomic`
- Full bare-metal UART driver with RX/TX ISR integration
- Production-ready `heapless::spsc::Queue` usage (type-system enforced SPSC)

**Practical guidance** on buffer sizing by baud rate, overflow strategies (drop newest/oldest, hardware flow control, XON/XOFF), and a pitfall table covering the most common bugs found in real firmware.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Theory of Operation](#2-theory-of-operation)
3. [Memory Layout & Index Arithmetic](#3-memory-layout--index-arithmetic)
4. [Lock-Free Design Principles](#4-lock-free-design-principles)
5. [Implementation in C/C++](#5-implementation-in-cc)
   - 5.1 [Basic Ring Buffer (Single-Producer / Single-Consumer)](#51-basic-ring-buffer-single-producer--single-consumer)
   - 5.2 [ISR-Safe UART RX Buffer](#52-isr-safe-uart-rx-buffer)
   - 5.3 [ISR-Safe UART TX Buffer with DMA Kick-Off](#53-isr-safe-uart-tx-buffer-with-dma-kick-off)
   - 5.4 [Power-of-Two Optimisation](#54-power-of-two-optimisation)
   - 5.5 [Multi-Producer / Multi-Consumer with C11 Atomics](#55-multi-producer--multi-consumer-with-c11-atomics)
6. [Implementation in Rust](#6-implementation-in-rust)
   - 6.1 [Safe SPSC Ring Buffer using `core::sync::atomic`](#61-safe-spsc-ring-buffer-using-coresyncatomic)
   - 6.2 [UART Driver Integration (bare-metal / `no_std`)](#62-uart-driver-integration-bare-metal--no_std)
   - 6.3 [Using `heapless::spsc::Queue`](#63-using-heaplessspscqueue)
7. [Capacity, Overflow and Flow Control](#7-capacity-overflow-and-flow-control)
8. [Common Pitfalls](#8-common-pitfalls)
9. [Summary](#9-summary)

---

## 1. Introduction

A **circular buffer** (also called a *ring buffer* or *cyclic queue*) is a fixed-size, first-in / first-out (FIFO) data structure that treats its underlying storage array as if its two ends are joined together. When the write pointer reaches the end of the storage it wraps back to index 0, reusing memory that has already been consumed.

In UART drivers this data structure appears in **two distinct roles**:

| Role | Buffer | Producer | Consumer |
|------|--------|----------|----------|
| Receive path | RX ring buffer | UART RX ISR / DMA TC interrupt | Application task |
| Transmit path | TX ring buffer | Application task | UART TX ISR / DMA request |

Without a ring buffer the ISR must either drop incoming bytes while the CPU is busy, or the CPU must poll the UART register continuously — both unacceptable in production firmware. A ring buffer **decouples the timing** of the hardware event from the timing of software processing.

---

## 2. Theory of Operation

```
Storage array (size N = 8)
Index:   0    1    2    3    4    5    6    7
       +----+----+----+----+----+----+----+----+
Data:  | D0 | D1 | D2 |    |    |    |    | D7 |
       +----+----+----+----+----+----+----+----+
              ↑                            ↑
            head (read)                  tail (write)
```

Two indices track the buffer state:

- **`head`** (read pointer) — index of the next byte to be consumed.
- **`tail`** (write pointer) — index where the next byte will be written.

| Condition         | Meaning          |
|-------------------|------------------|
| `head == tail`    | Buffer **empty** |
| `(tail+1) % N == head` | Buffer **full** |

The buffer can hold at most **N − 1** elements when using the classic "waste one slot" method, or exactly **N** elements when using a separate count or mirrored-index scheme.

---

## 3. Memory Layout & Index Arithmetic

### 3.1 Modulo Indexing (General)

```
next_index = (current_index + 1) % BUFFER_SIZE
```

This works for any buffer size but `%` on non-power-of-two sizes costs a division instruction on most MCUs.

### 3.2 Mask Indexing (Power-of-Two — preferred)

When `BUFFER_SIZE` is a power of two (8, 16, 32, 64 …):

```
next_index = (current_index + 1) & (BUFFER_SIZE - 1)
```

A single `AND` replaces the division, which is typically one clock cycle on ARM Cortex-M.

### 3.3 Free-Running (Ever-Incrementing) Index Scheme

Use unsigned integers that are allowed to wrap at their natural type limit (e.g. `uint16_t` wraps at 65535 → 0). The *occupancy* is always:

```
used  = tail - head          // unsigned subtraction wraps correctly
free  = BUFFER_SIZE - used
empty = (tail == head)
full  = (used == BUFFER_SIZE)
```

This eliminates the "waste one slot" problem and makes overflow detection trivial. **This is the recommended approach for lock-free SPSC buffers.**

---

## 4. Lock-Free Design Principles

A **lock-free** SPSC (Single-Producer / Single-Consumer) ring buffer can be built without mutexes or disabling interrupts by exploiting two guarantees:

1. **Atomic reads and writes of index variables** — On all modern 32-bit MCUs a naturally-aligned 32-bit (or smaller) variable read/write is atomic at the hardware level. C11/C++11 `_Atomic` / `std::atomic` makes this portable and prevents compiler reordering.

2. **Happens-before ordering** — The producer writes data *before* updating `tail`; the consumer reads `tail` *before* reading data. Appropriate memory barriers (`memory_order_release` on the write side, `memory_order_acquire` on the read side) enforce this ordering across compiler and CPU pipelines.

```
Producer                        Consumer
────────────────────────────    ────────────────────────────
buf[tail & mask] = byte;        local_tail = load(tail, acquire);
store(tail+1, tail, release);   byte = buf[head & mask];
                                store(head+1, head, release);
```

For a **single-core MCU** (Cortex-M0/M3/M4 without SMP) a simpler `volatile` + compiler barrier or `__DMB()` is often sufficient, but using `<stdatomic.h>` is cleaner and portable.

---

## 5. Implementation in C/C++

### 5.1 Basic Ring Buffer (Single-Producer / Single-Consumer)

```c
/* ring_buffer.h — portable SPSC lock-free ring buffer */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

#define RB_SIZE  256u          /* Must be a power of two */
#define RB_MASK  (RB_SIZE - 1u)

typedef struct {
    uint8_t          buf[RB_SIZE];
    atomic_uint_fast16_t head;   /* consumer updates */
    atomic_uint_fast16_t tail;   /* producer updates */
} RingBuf;

/* Returns true if byte was enqueued (producer side). */
static inline bool rb_push(RingBuf *rb, uint8_t byte)
{
    uint_fast16_t t = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint_fast16_t h = atomic_load_explicit(&rb->head, memory_order_acquire);

    if ((uint_fast16_t)(t - h) >= RB_SIZE)
        return false;                    /* full */

    rb->buf[t & RB_MASK] = byte;
    atomic_store_explicit(&rb->tail, t + 1u, memory_order_release);
    return true;
}

/* Returns true if a byte was dequeued (consumer side). */
static inline bool rb_pop(RingBuf *rb, uint8_t *byte)
{
    uint_fast16_t h = atomic_load_explicit(&rb->head, memory_order_relaxed);
    uint_fast16_t t = atomic_load_explicit(&rb->tail, memory_order_acquire);

    if (h == t)
        return false;                    /* empty */

    *byte = rb->buf[h & RB_MASK];
    atomic_store_explicit(&rb->head, h + 1u, memory_order_release);
    return true;
}

static inline bool     rb_empty(const RingBuf *rb) {
    return atomic_load_explicit(&rb->head, memory_order_acquire)
        == atomic_load_explicit(&rb->tail, memory_order_acquire);
}
static inline uint16_t rb_count(const RingBuf *rb) {
    return (uint16_t)(
        atomic_load_explicit(&rb->tail, memory_order_acquire) -
        atomic_load_explicit(&rb->head, memory_order_acquire));
}
```

---

### 5.2 ISR-Safe UART RX Buffer

```c
/* uart_rx.c — STM32-style example (HAL_UART_RxCpltCallback) */
#include "ring_buffer.h"

static RingBuf uart_rx_buf;

/* Called from UART RX interrupt — PRODUCER */
void USART1_IRQHandler(void)
{
    uint8_t byte = (uint8_t)(USART1->DR & 0xFFu);   /* read clears RXNE flag */

    if (!rb_push(&uart_rx_buf, byte)) {
        /* Buffer full — increment an overflow counter for diagnostics */
        uart_rx_overflow_count++;
    }
}

/* Called from application task/main loop — CONSUMER */
int uart_getc(void)
{
    uint8_t byte;
    return rb_pop(&uart_rx_buf, &byte) ? (int)byte : -1;
}

/* Read up to `len` bytes; returns number actually read */
size_t uart_read(uint8_t *dst, size_t len)
{
    size_t n = 0;
    while (n < len && rb_pop(&uart_rx_buf, &dst[n]))
        ++n;
    return n;
}
```

**Key point:** No `__disable_irq()` / `__enable_irq()` pair is needed. The `memory_order_release` in `rb_push` and `memory_order_acquire` in `rb_pop` are sufficient on ARMv7-M (Cortex-M3/M4/M7) because those MCUs are in-order and single-core.

---

### 5.3 ISR-Safe UART TX Buffer with DMA Kick-Off

The TX path is slightly trickier because the ISR *consumes* while the application *produces*, and DMA completion must re-arm the transfer.

```c
/* uart_tx.c */
#include "ring_buffer.h"
#include <string.h>

#define DMA_TX_BUF_SIZE 128u

static RingBuf  uart_tx_rb;
static uint8_t  dma_tx_staging[DMA_TX_BUF_SIZE];
static volatile bool dma_busy = false;

/* Internal: load bytes from ring into DMA staging buffer and start transfer. */
static void tx_start_dma(void)
{
    uint16_t n = 0;
    while (n < DMA_TX_BUF_SIZE && rb_pop(&uart_tx_rb, &dma_tx_staging[n]))
        ++n;

    if (n == 0) { dma_busy = false; return; }

    dma_busy = true;
    HAL_UART_Transmit_DMA(&huart1, dma_tx_staging, n);
}

/* DMA TX-complete callback — runs in ISR context */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
        tx_start_dma();                  /* drain next chunk */
}

/* Application API: enqueue bytes for transmission */
bool uart_write(const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        if (!rb_push(&uart_tx_rb, src[i]))
            return false;                /* overflow */

    /* If DMA is idle, kick off a new transfer */
    bool expected = false;
    if (atomic_compare_exchange_strong(
            (atomic_bool *)&dma_busy, &expected, true))
        tx_start_dma();

    return true;
}
```

---

### 5.4 Power-of-Two Optimisation

Enforce buffer size at compile time with a `static_assert`:

```c
#include <assert.h>

#define UART_BUF_SIZE 512u

static_assert((UART_BUF_SIZE & (UART_BUF_SIZE - 1u)) == 0u,
              "UART_BUF_SIZE must be a power of two");

#define UART_BUF_MASK (UART_BUF_SIZE - 1u)

/* Macro-based mask indexing — zero cost at runtime */
#define RB_IDX(i)  ((i) & UART_BUF_MASK)
```

On GCC/Clang with `-O2` the compiler will replace the `& MASK` with a single `AND` instruction and inline the `rb_push`/`rb_pop` functions entirely.

---

### 5.5 Multi-Producer / Multi-Consumer with C11 Atomics

When multiple threads (RTOS tasks) both write and read the buffer, a CAS (Compare-And-Swap) loop replaces the simple store:

```c
/* MPMC ring buffer — suitable for RTOS multi-task environments */
typedef struct {
    uint8_t             buf[RB_SIZE];
    atomic_uint_fast16_t head;
    atomic_uint_fast16_t tail;
} MpmcRingBuf;

bool mpmc_push(MpmcRingBuf *rb, uint8_t byte)
{
    uint_fast16_t t, h, next_t;
    do {
        t      = atomic_load_explicit(&rb->tail, memory_order_relaxed);
        h      = atomic_load_explicit(&rb->head, memory_order_acquire);
        next_t = t + 1u;
        if ((uint_fast16_t)(next_t - h) > RB_SIZE)
            return false;                /* full */
    } while (!atomic_compare_exchange_weak_explicit(
                 &rb->tail, &t, next_t,
                 memory_order_release, memory_order_relaxed));

    rb->buf[t & RB_MASK] = byte;
    return true;
}

bool mpmc_pop(MpmcRingBuf *rb, uint8_t *byte)
{
    uint_fast16_t h, t, next_h;
    do {
        h      = atomic_load_explicit(&rb->head, memory_order_relaxed);
        t      = atomic_load_explicit(&rb->tail, memory_order_acquire);
        next_h = h + 1u;
        if (h == t) return false;        /* empty */
    } while (!atomic_compare_exchange_weak_explicit(
                 &rb->head, &h, next_h,
                 memory_order_release, memory_order_relaxed));

    *byte = rb->buf[h & RB_MASK];
    return true;
}
```

> ⚠️ **Note:** The naive MPMC version above has a *slot reservation* race — a producer may reserve a slot but not yet write data when the consumer advances. For a production MPMC queue, use a sequence-number per slot (Dmitry Vyukov's bounded MPMC queue pattern).

---

## 6. Implementation in Rust

### 6.1 Safe SPSC Ring Buffer using `core::sync::atomic`

```rust
// ring_buffer.rs — no_std compatible SPSC lock-free ring buffer

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicU16, Ordering};

const RB_SIZE: usize = 256;
const RB_MASK: u16   = (RB_SIZE - 1) as u16;

pub struct RingBuf {
    buf:  UnsafeCell<[u8; RB_SIZE]>,
    head: AtomicU16,   // consumer
    tail: AtomicU16,   // producer
}

// SAFETY: single-producer / single-consumer guarantee upheld by the caller.
unsafe impl Sync for RingBuf {}
unsafe impl Send for RingBuf {}

impl RingBuf {
    pub const fn new() -> Self {
        Self {
            buf:  UnsafeCell::new([0u8; RB_SIZE]),
            head: AtomicU16::new(0),
            tail: AtomicU16::new(0),
        }
    }

    /// Push one byte (producer side).  Returns `false` if the buffer is full.
    pub fn push(&self, byte: u8) -> bool {
        let t = self.tail.load(Ordering::Relaxed);
        let h = self.head.load(Ordering::Acquire);

        if t.wrapping_sub(h) as usize >= RB_SIZE {
            return false; // full
        }

        // SAFETY: producer owns this slot exclusively.
        unsafe {
            (*self.buf.get())[(t & RB_MASK) as usize] = byte;
        }

        self.tail.store(t.wrapping_add(1), Ordering::Release);
        true
    }

    /// Pop one byte (consumer side).  Returns `None` if the buffer is empty.
    pub fn pop(&self) -> Option<u8> {
        let h = self.head.load(Ordering::Relaxed);
        let t = self.tail.load(Ordering::Acquire);

        if h == t {
            return None; // empty
        }

        // SAFETY: consumer owns this slot exclusively.
        let byte = unsafe {
            (*self.buf.get())[(h & RB_MASK) as usize]
        };

        self.head.store(h.wrapping_add(1), Ordering::Release);
        Some(byte)
    }

    pub fn is_empty(&self) -> bool {
        self.head.load(Ordering::Acquire) == self.tail.load(Ordering::Acquire)
    }

    pub fn len(&self) -> u16 {
        self.tail.load(Ordering::Acquire)
            .wrapping_sub(self.head.load(Ordering::Acquire))
    }
}
```

---

### 6.2 UART Driver Integration (bare-metal / `no_std`)

```rust
// uart_driver.rs — cortex-m bare-metal example
#![no_std]

use core::sync::atomic::{AtomicBool, Ordering};
use cortex_m::interrupt;
use crate::ring_buffer::RingBuf;

static UART_RX_BUF: RingBuf = RingBuf::new();
static UART_TX_BUF: RingBuf = RingBuf::new();
static DMA_BUSY:    AtomicBool = AtomicBool::new(false);

// ── RX path ──────────────────────────────────────────────────────────────────

/// Called from the USART IRQ handler (PRODUCER).
///
/// Register this with the NVIC; no mutex needed for SPSC.
pub fn uart_rx_isr(dr: u8) {
    if !UART_RX_BUF.push(dr) {
        // Overflow — could set a flag or toggle an LED
    }
}

/// Read up to `buf.len()` bytes from the RX buffer.  Returns bytes read.
pub fn uart_read(buf: &mut [u8]) -> usize {
    let mut n = 0usize;
    while n < buf.len() {
        match UART_RX_BUF.pop() {
            Some(b) => { buf[n] = b; n += 1; }
            None    => break,
        }
    }
    n
}

// ── TX path ──────────────────────────────────────────────────────────────────

/// Enqueue bytes for transmission (application context, PRODUCER).
pub fn uart_write(data: &[u8]) -> bool {
    for &b in data {
        if !UART_TX_BUF.push(b) { return false; }
    }
    // Kick off DMA if idle
    if DMA_BUSY
        .compare_exchange(false, true, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
    {
        start_dma_tx();
    }
    true
}

/// Called from DMA TX-complete IRQ.
pub fn uart_tx_dma_isr() {
    start_dma_tx();
}

fn start_dma_tx() {
    // Drain up to 64 bytes into a local staging array
    let mut staging = [0u8; 64];
    let mut n = 0usize;
    while n < staging.len() {
        match UART_TX_BUF.pop() {
            Some(b) => { staging[n] = b; n += 1; }
            None    => break,
        }
    }
    if n == 0 {
        DMA_BUSY.store(false, Ordering::Release);
        return;
    }
    // Hand staging[..n] to the DMA peripheral (platform-specific)
    unsafe { hal_uart_dma_transmit(staging.as_ptr(), n); }
}

extern "C" {
    fn hal_uart_dma_transmit(ptr: *const u8, len: usize);
}
```

---

### 6.3 Using `heapless::spsc::Queue`

For production `no_std` firmware the [`heapless`](https://docs.rs/heapless) crate provides a rigorously tested, fully safe SPSC queue:

```toml
# Cargo.toml
[dependencies]
heapless = "0.8"
```

```rust
// heapless_uart.rs
#![no_std]

use heapless::spsc::{Consumer, Producer, Queue};

// Declare a statically allocated queue for 256 bytes.
static mut RX_QUEUE: Queue<u8, 256> = Queue::new();

// In your initialisation code, split into producer / consumer halves:
//
//   let (mut rx_prod, mut rx_cons) = unsafe { RX_QUEUE.split() };
//
// Store rx_prod in interrupt context, rx_cons in task context.

// ── ISR (producer) ───────────────────────────────────────────────────────────
fn uart_rx_isr(prod: &mut Producer<'static, u8, 256>, byte: u8) {
    if prod.enqueue(byte).is_err() {
        // Buffer full — handle overflow
    }
}

// ── Application task (consumer) ──────────────────────────────────────────────
fn process_rx(cons: &mut Consumer<'static, u8, 256>) {
    while let Some(byte) = cons.dequeue() {
        handle_byte(byte);
    }
}

fn handle_byte(b: u8) {
    // Parse protocol, echo, etc.
    let _ = b;
}
```

> The `heapless::spsc` split ensures at **compile time** that only one producer and one consumer exist — the type system enforces the SPSC contract without any runtime overhead.

---

## 7. Capacity, Overflow and Flow Control

### 7.1 Choosing Buffer Size

| Baud Rate | Max bytes/s | 10 ms budget | Recommended size |
|-----------|-------------|--------------|-----------------|
| 9 600     | 960         | 10 B         | 32 B            |
| 115 200   | 11 520      | 115 B        | 256 B           |
| 921 600   | 92 160      | 921 B        | 2 048 B         |
| 4 000 000 | 400 000     | 4 000 B      | 8 192 B         |

Rule of thumb: size the buffer for the **worst-case burst** the application can tolerate before draining, multiplied by a safety factor of 2–4×.

### 7.2 Overflow Strategies

| Strategy | Behaviour | Use Case |
|----------|-----------|----------|
| **Drop newest** | ISR discards incoming bytes | Simple sensors, lossy-OK |
| **Drop oldest** | Overwrite head (streaming) | Audio, telemetry |
| **Assert / fault** | Halt system, log fault | Safety-critical |
| **Hardware flow control** | Assert RTS, pause sender | Reliable protocols |

### 7.3 Software Flow Control (XON/XOFF)

```c
#define XOFF 0x13
#define XON  0x11
#define HIGH_WATER  (RB_SIZE * 3 / 4)
#define LOW_WATER   (RB_SIZE * 1 / 4)

void check_flow_control(RingBuf *rx)
{
    uint16_t used = rb_count(rx);
    if (used > HIGH_WATER && !xoff_sent) {
        uart_send_byte(XOFF);
        xoff_sent = true;
    } else if (used < LOW_WATER && xoff_sent) {
        uart_send_byte(XON);
        xoff_sent = false;
    }
}
```

---

## 8. Common Pitfalls

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Non-atomic index reads on a shared-memory SMP system | Torn reads, data corruption | Use `_Atomic` / `std::atomic` |
| Missing memory barrier between data write and index update | Consumer sees stale / garbage data | Use `memory_order_release` on write, `memory_order_acquire` on read |
| Buffer size not a power of two with mask indexing | Wrap at wrong boundary | `static_assert` / `const_assert!` |
| Accessing both `head` and `tail` non-atomically to detect full/empty | Race window, incorrect full/empty | Read opposite index with `Acquire`, own index with `Relaxed` |
| TX buffer drained before DMA staging finishes | Partial last message | Stage into a separate DMA buffer before releasing ring slots |
| Forgetting `volatile` on MCU without C11 atomics | Compiler caches index in register | Replace with `<stdatomic.h>` or use `volatile` + `__DMB()` |
| Index variable overflow for non-power-of-two size | Incorrect occupancy after wrap | Use free-running indices with wrapping arithmetic only for power-of-two sizes |

---

## 9. Summary

Circular (ring) buffers are the **cornerstone of interrupt-driven UART drivers** because they decouple the hard real-time world of hardware interrupts from the soft real-time world of application logic.

Key takeaways:

- **Choose a power-of-two size** and use bitmask indexing (`& (SIZE-1)`) to eliminate expensive modulo operations on microcontrollers.
- **Use free-running (ever-incrementing) unsigned indices** — unsigned overflow wraps correctly, eliminates the "waste one slot" problem, and makes occupancy and full/empty checks trivially correct.
- **SPSC lock-free design is sufficient for ISR ↔ task communication** on a single-core MCU. A single producer and single consumer never need a mutex — only appropriate memory ordering (`release`/`acquire`) on the index updates.
- **In C**, use `<stdatomic.h>` (`_Atomic`, `atomic_store_explicit`, `atomic_load_explicit`) for correctness and portability across compilers and MCU families.
- **In Rust**, `core::sync::atomic::AtomicU16` with `Ordering::Release`/`Ordering::Acquire` provides the same guarantees in `no_std` environments. For production code prefer `heapless::spsc::Queue`, which enforces the SPSC contract at compile time via the type system.
- **Overflow must be handled explicitly** — choose a policy (drop newest, drop oldest, hardware flow control, fault) appropriate to your protocol and reliability requirements.
- **Size the buffer generously**: at high baud rates (≥ 921 600 baud) a single task preemption of a few milliseconds can represent thousands of bytes; under-sized buffers are the most common source of subtle UART data loss in embedded systems.

---

*Document: `15_Circular_Buffers.md` — Part of the UART Programming Series*