# 58. Priority Inversion Prevention in SPI Resource Sharing


- **The problem explained** — how a low-priority task holding the SPI mutex can block a high-priority task for an unbounded duration, with the Mars Pathfinder mission as a real-world example.
- **SPI-specific context** — why SPI makes this especially tricky: non-preemptable transfers, DMA holding the bus for long periods, multiple chip-selects per bus at different task priorities.
- **Four prevention strategies** — Priority Inheritance (PIP), Priority Ceiling (PCP), Lock-Free Queuing, and Critical Section/Interrupt Disable — with trade-off analysis for each.

**C/C++ examples (4 implementations):**
1. FreeRTOS mutex with automatic priority inheritance
2. POSIX `pthread_mutex` with `PTHREAD_PRIO_PROTECT` ceiling protocol
3. C++ lock-free SPSC ring buffer routing all transfers through a single high-priority manager task

**Rust examples (4 implementations):**
1. RTIC framework — ceiling protocol enforced **at compile time** (the strongest guarantee)
2. Embassy async mutex with `CriticalSectionRawMutex`
3. `heapless` SPSC lock-free queue for `no_std` environments
4. `critical-section` crate for short transfers on single-core Cortex-M

**Summary** with a practical best-practice checklist closing the document.

## Table of Contents

1. [Introduction](#introduction)
2. [What is Priority Inversion?](#what-is-priority-inversion)
3. [Priority Inversion in SPI Context](#priority-inversion-in-spi-context)
4. [Prevention Strategies](#prevention-strategies)
5. [C/C++ Implementation Examples](#cc-implementation-examples)
6. [Rust Implementation Examples](#rust-implementation-examples)
7. [Summary](#summary)

---

## Introduction

In real-time and embedded systems, multiple tasks often share hardware peripherals such as SPI (Serial Peripheral Interface) buses. SPI is a synchronous serial communication protocol commonly used to interface with sensors, displays, memory chips, and other peripherals. When multiple tasks of different priorities compete for access to a shared SPI resource, a classic concurrency hazard known as **priority inversion** can occur — potentially leading to missed deadlines, system instability, or even complete task starvation.

This document explores the mechanics of priority inversion in the SPI context, describes well-established prevention strategies, and provides concrete implementation examples in both C/C++ and Rust.

---

## What is Priority Inversion?

**Priority inversion** is a scheduling anomaly in which a high-priority task is effectively blocked by a low-priority task, with a medium-priority task running in between — bypassing the intended scheduling order.

### Classic Scenario

Consider three tasks: **H** (high priority), **M** (medium priority), and **L** (low priority).

```
Time →

L acquires SPI mutex
    H becomes ready, tries to acquire SPI mutex → BLOCKED (L holds it)
        M becomes ready → RUNS (preempts L, because M > L)
            M runs for a long time...
    L eventually resumes, finishes SPI transfer, releases mutex
H finally runs
```

H is blocked not just by L (which is expected), but also by M — which has no involvement with the SPI resource at all. This inversion of priority can be unbounded in duration, which is catastrophic for real-time systems.

> **Historical note:** The Mars Pathfinder mission (1997) suffered a real-time priority inversion bug exactly of this nature, causing the spacecraft's computer to reset repeatedly.

---

## Priority Inversion in SPI Context

SPI resources introduce several specific characteristics that make priority inversion particularly relevant:

### SPI Transfer Characteristics

- SPI transfers are typically **non-preemptable** once started (mid-transfer interruption corrupts the data frame).
- A single SPI bus may be shared by **multiple chip selects** (slaves), each accessed by tasks of different priorities.
- DMA-based SPI transfers may hold the bus for **extended periods**, increasing the inversion window.
- SPI bus speed configuration changes (e.g., different slaves operate at different clock speeds) may require the mutex to be held during reconfiguration as well as during transfer.

### Typical SPI Sharing Architecture

```
         ┌────────────────────────────────────┐
         │            SPI Bus (shared)        │
         └──────┬──────────┬──────────┬───────┘
                │          │          │
           CS0 (Display) CS1 (Flash) CS2 (Sensor)
                │          │          │
           Task H (high)  Task M    Task L (low)
```

If Task L acquires the SPI mutex to flash-write while Task H is waiting to update the display, priority inversion can occur.

---

## Prevention Strategies

### 1. Priority Inheritance Protocol (PIP)

The most widely used solution. When a high-priority task blocks on a mutex held by a lower-priority task, the OS **temporarily raises** the low-priority task's priority to match the blocker. This prevents medium-priority tasks from running ahead of the mutex holder.

- **Pros:** Transparent to application code; well-supported by RTOS (FreeRTOS, Zephyr, RTEMS, VxWorks).
- **Cons:** Does not bound inversion time to zero; chained inheritance adds complexity; not trivially composable with nested locks.

### 2. Priority Ceiling Protocol (PCP)

Every mutex is assigned a **ceiling priority** equal to the highest priority of any task that may ever acquire it. When a task acquires the mutex, its priority is immediately raised to the ceiling — regardless of whether contention currently exists.

- **Pros:** Bounded blocking; prevents deadlock; simpler analysis.
- **Cons:** Always incurs a priority boost even when uncontested; ceiling must be correctly configured at design time.

### 3. Immediate Priority Ceiling (Highest Locker Protocol)

A variant of PCP used in POSIX (`PTHREAD_PRIO_PROTECT`). The acquiring task's priority is immediately raised to the mutex ceiling upon lock, not just when contention occurs.

### 4. Lock-Free / Wait-Free SPI Queuing

Instead of having tasks directly access the SPI bus, they post transfer requests into a **lock-free queue** consumed by a single dedicated SPI manager task running at a fixed (highest) priority.

- **Pros:** Eliminates priority inversion entirely; provides natural request ordering and batching.
- **Cons:** Adds latency; more complex design; results are returned asynchronously.

### 5. Avoiding Shared Mutable State

On hardware with multiple SPI controllers, assign one controller per priority domain. If only one SPI bus is available, minimise the critical section to the absolute minimum (configure + transfer + release) without sleeping or doing additional work while holding the lock.

---

## C/C++ Implementation Examples

### Example 1: FreeRTOS — Mutex with Priority Inheritance

FreeRTOS mutexes (created with `xSemaphoreCreateMutex()`) implement the **priority inheritance protocol** automatically.

```c
#include "FreeRTOS.h"
#include "semphr.h"
#include "spi_driver.h"  // Hypothetical platform SPI driver

// Global SPI mutex — must be a FreeRTOS mutex (NOT a binary semaphore)
// to get priority inheritance behaviour.
static SemaphoreHandle_t g_spi_mutex = NULL;

/**
 * @brief Initialise the shared SPI resource and its protecting mutex.
 *        Call once before any tasks are started.
 */
void spi_resource_init(void)
{
    g_spi_mutex = xSemaphoreCreateMutex();
    configASSERT(g_spi_mutex != NULL);
    spi_hw_init();  // Configure SPI hardware
}

/**
 * @brief Perform a guarded SPI transfer.
 *        Priority inheritance is applied automatically by FreeRTOS
 *        if a higher-priority task blocks on this mutex.
 *
 * @param tx_buf  Pointer to transmit buffer (may be NULL for read-only)
 * @param rx_buf  Pointer to receive buffer  (may be NULL for write-only)
 * @param len     Number of bytes to transfer
 * @param cs_pin  Chip-select GPIO pin number
 * @return true on success, false on timeout
 */
bool spi_transfer_guarded(const uint8_t *tx_buf,
                           uint8_t       *rx_buf,
                           size_t         len,
                           uint32_t       cs_pin)
{
    // Attempt to acquire the mutex — wait up to 100 ms.
    // If a higher-priority task is also waiting, this task's priority
    // will be inherited by whoever currently holds the mutex.
    if (xSemaphoreTake(g_spi_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        // Timeout: could not acquire SPI bus
        return false;
    }

    // --- Critical section: exclusive SPI bus access ---
    spi_cs_assert(cs_pin);
    spi_hw_transfer(tx_buf, rx_buf, len);   // Blocking hardware transfer
    spi_cs_deassert(cs_pin);
    // --- End of critical section ---

    xSemaphoreGive(g_spi_mutex);
    return true;
}

// -----------------------------------------------------------------------
// Example tasks demonstrating the priority setup
// -----------------------------------------------------------------------

// High-priority task: display refresh (must meet tight deadline)
void task_display(void *pvParameters)
{
    uint8_t frame_buf[128];

    for (;;) {
        // Produce frame...
        bool ok = spi_transfer_guarded(frame_buf, NULL,
                                       sizeof(frame_buf), SPI_CS_DISPLAY);
        if (!ok) {
            // Handle SPI timeout — log error, use watchdog, etc.
        }
        vTaskDelay(pdMS_TO_TICKS(16));  // ~60 Hz
    }
}

// Low-priority task: flash wear-levelling (long, infrequent transfers)
void task_flash_write(void *pvParameters)
{
    uint8_t sector_buf[4096];

    for (;;) {
        prepare_sector_data(sector_buf);
        // If task_display is waiting, FreeRTOS will temporarily boost
        // this task's priority to match task_display while the mutex is held.
        spi_transfer_guarded(sector_buf, NULL,
                             sizeof(sector_buf), SPI_CS_FLASH);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

### Example 2: POSIX / Linux — Priority Ceiling Protocol with `pthread_mutex`

```c
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

// -----------------------------------------------------------------------
// SPI resource with POSIX Priority Ceiling Protocol
// -----------------------------------------------------------------------

static pthread_mutex_t g_spi_mutex;
static int             g_spi_fd = -1;

/**
 * @brief Initialise the SPI mutex with priority ceiling.
 *        The ceiling must be set to the maximum scheduling priority of
 *        any thread that will ever lock this mutex.
 */
int spi_mutex_init(void)
{
    pthread_mutexattr_t attr;
    int ret;

    ret = pthread_mutexattr_init(&attr);
    if (ret != 0) return ret;

    // Use the Priority Ceiling protocol (PTHREAD_PRIO_PROTECT).
    // The locking thread's priority is immediately raised to the
    // ceiling upon acquisition — no contention needed.
    ret = pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_PROTECT);
    if (ret != 0) goto cleanup;

    // Set the ceiling to the highest real-time priority in the system.
    // All threads using this mutex must have priority <= this ceiling.
    int ceiling = sched_get_priority_max(SCHED_FIFO);
    ret = pthread_mutexattr_setprioceiling(&attr, ceiling);
    if (ret != 0) goto cleanup;

    ret = pthread_mutex_init(&g_spi_mutex, &attr);

cleanup:
    pthread_mutexattr_destroy(&attr);
    return ret;
}

/**
 * @brief Perform a guarded, priority-ceiling-protected SPI transfer.
 */
bool spi_transfer_pcp(const uint8_t *tx_buf,
                       uint8_t       *rx_buf,
                       size_t         len)
{
    // Lock: this thread's priority is raised to the mutex ceiling
    // immediately, preventing any medium-priority thread from
    // preempting us while we hold the SPI bus.
    if (pthread_mutex_lock(&g_spi_mutex) != 0) {
        return false;
    }

    // --- Critical section ---
    spi_linux_transfer(g_spi_fd, tx_buf, rx_buf, len);
    // --- End of critical section ---

    pthread_mutex_unlock(&g_spi_mutex);
    // Priority returns to its normal value after unlock
    return true;
}
```

### Example 3: C++ — Lock-Free SPI Request Queue (Producer/Consumer Pattern)

This approach eliminates priority inversion entirely by routing all SPI access through a single, highest-priority consumer task.

```cpp
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>

// -----------------------------------------------------------------------
// Lock-free SPSC (Single Producer, Single Consumer) ring buffer
// for SPI transfer requests.
// -----------------------------------------------------------------------

struct SpiRequest {
    uint8_t          tx_buf[256];
    uint8_t          rx_buf[256];
    size_t           len;
    uint32_t         cs_pin;
    std::function<void(bool)> callback;  // Called with success flag
};

template <size_t Capacity>
class SpscQueue {
public:
    bool push(const SpiRequest &req) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) % Capacity;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }
        buf_[head] = req;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(SpiRequest &req) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }
        req = buf_[tail];
        tail_.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::array<SpiRequest, Capacity> buf_{};
};

// Global queue — producers post here, the SPI manager task consumes.
static SpscQueue<32> g_spi_queue;

/**
 * @brief Post a non-blocking SPI transfer request.
 *        The callback is invoked by the SPI manager task upon completion.
 *        No mutex is needed — priority inversion is impossible because
 *        tasks never directly hold the SPI bus.
 */
bool spi_post_transfer(const uint8_t *tx_buf,
                        uint8_t       *rx_buf,
                        size_t         len,
                        uint32_t       cs_pin,
                        std::function<void(bool)> cb)
{
    SpiRequest req{};
    if (tx_buf) std::memcpy(req.tx_buf, tx_buf, len);
    req.len      = len;
    req.cs_pin   = cs_pin;
    req.callback = std::move(cb);

    return g_spi_queue.push(req);
}

/**
 * @brief SPI manager task — runs at the HIGHEST priority.
 *        Drains the request queue and executes transfers sequentially.
 *        Because it is always the highest-priority runnable task,
 *        priority inversion cannot occur.
 */
void spi_manager_task(void * /*arg*/)
{
    SpiRequest req;
    for (;;) {
        while (g_spi_queue.pop(req)) {
            spi_cs_assert(req.cs_pin);
            bool ok = spi_hw_transfer(req.tx_buf,
                                      req.rx_buf,
                                      req.len);
            spi_cs_deassert(req.cs_pin);
            if (req.callback) req.callback(ok);
        }
        // Yield or wait for notification from producers
        task_yield();
    }
}
```

---

## Rust Implementation Examples

Rust's ownership model and type system make many concurrency bugs impossible to express, but priority inversion is a *scheduling* issue — not a memory-safety issue — so it still requires deliberate design. The examples below use `embedded-hal` traits and an RTOS abstraction.

### Example 1: Mutex with Priority Inheritance (RTIC Framework)

The [RTIC](https://rtic.rs) (Real-Time Interrupt-driven Concurrency) framework for embedded Rust uses the **Immediate Ceiling Priority Protocol** statically — enforced at compile time via its resource model. No runtime mutex is needed.

```rust
// Cargo.toml dependencies:
//   cortex-m-rtic = "1"
//   embedded-hal = "0.2"

#![no_std]
#![no_main]

use rtic::app;
use stm32f4xx_hal::{
    pac,
    spi::{Spi, Mode, Phase, Polarity},
    prelude::*,
};

/// RTIC assigns each resource a "ceiling priority" equal to the highest
/// priority task that accesses it. Access is mediated by compile-time
/// checked critical sections — no runtime overhead, no priority inversion.
#[app(device = pac, dispatchers = [EXTI0, EXTI1, EXTI2])]
mod app {
    use super::*;

    /// Shared SPI bus — RTIC wraps this in a priority-ceiling mutex.
    #[shared]
    struct Shared {
        spi: Spi<pac::SPI1>,
    }

    #[local]
    struct Local {}

    #[init]
    fn init(cx: init::Context) -> (Shared, Local, init::Monotonics) {
        let rcc    = cx.device.RCC.constrain();
        let clocks = rcc.cfgr.freeze();
        let gpioa  = cx.device.GPIOA.split();

        let sclk = gpioa.pa5.into_alternate();
        let miso = gpioa.pa6.into_alternate();
        let mosi = gpioa.pa7.into_alternate();

        let spi = Spi::new(
            cx.device.SPI1,
            (sclk, miso, mosi),
            Mode { polarity: Polarity::IdleLow, phase: Phase::CaptureOnFirstTransition },
            1.MHz(),
            &clocks,
        );

        (Shared { spi }, Local {}, init::Monotonics())
    }

    /// High-priority task (priority = 3): Display refresh.
    /// RTIC raises the ceiling when this task accesses `spi`.
    #[task(shared = [spi], priority = 3)]
    fn task_display(mut cx: task_display::Context) {
        let frame: [u8; 128] = [0xAA; 128];

        // `lock` is the RTIC ceiling-priority critical section.
        // If a lower-priority task holds the `spi` resource, this
        // task preempts it (via the ceiling mechanism), ensuring
        // bounded blocking. This is enforced at compile time.
        cx.shared.spi.lock(|spi| {
            // Chip-select would be handled here via GPIO
            spi.write(&frame).ok();
        });
    }

    /// Low-priority task (priority = 1): Flash wear-levelling.
    /// When this task holds `spi` and `task_display` becomes ready,
    /// the ceiling protocol ensures `task_display` is not delayed by
    /// any medium-priority task.
    #[task(shared = [spi], priority = 1)]
    fn task_flash(mut cx: task_flash::Context) {
        let sector: [u8; 256] = [0xFF; 256];

        cx.shared.spi.lock(|spi| {
            spi.write(&sector).ok();
        });
    }
}
```

### Example 2: Priority Inheritance Mutex with `embassy` (async embedded Rust)

```rust
// Uses embassy-sync::mutex::Mutex which, on RTOS backends,
// wraps a priority-inheritance mutex.

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_executor::Spawner;
use embedded_hal_async::spi::SpiBus;

/// Shared SPI bus protected by an async-aware mutex.
/// The underlying CriticalSectionRawMutex raises the MCU's BASEPRI
/// register, effectively implementing the ceiling protocol on
/// Cortex-M hardware.
static SPI_BUS: Mutex<CriticalSectionRawMutex, Option<SomeSpiDriver>> =
    Mutex::new(None);

/// Helper: acquire the SPI bus, perform a transfer, release.
/// All `.await` points inside the lock are eliminated (the lock is held
/// only for the synchronous transfer duration), minimising inversion risk.
async fn spi_transfer(tx: &[u8], rx: &mut [u8]) -> Result<(), SpiError> {
    let mut bus = SPI_BUS.lock().await;
    let spi = bus.as_mut().expect("SPI not initialised");
    spi.transfer(rx, tx).await?;
    Ok(())
}

/// High-priority task spawned with a high Embassy executor priority.
#[embassy_executor::task]
async fn high_priority_sensor_task() {
    loop {
        let cmd = [0x03u8, 0x00, 0x00, 0x00];  // Read command
        let mut result = [0u8; 4];
        spi_transfer(&cmd, &mut result).await.unwrap();
        // Process result...
        embassy_time::Timer::after_millis(10).await;
    }
}

/// Low-priority task for periodic data logging.
#[embassy_executor::task]
async fn low_priority_log_task() {
    loop {
        let log_entry = [0xDEu8, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00];
        let mut discard = [0u8; 8];
        spi_transfer(&log_entry, &mut discard).await.unwrap();
        embassy_time::Timer::after_millis(1000).await;
    }
}
```

### Example 3: Lock-Free SPI Queue in Rust using `heapless`

```rust
use heapless::spsc::{Queue, Producer, Consumer};
use core::sync::atomic::{AtomicBool, Ordering};

const QUEUE_SIZE: usize = 16;
const MAX_PAYLOAD: usize = 256;

/// A SPI transfer request with an optional completion flag.
pub struct SpiRequest {
    pub tx_data:  [u8; MAX_PAYLOAD],
    pub rx_data:  [u8; MAX_PAYLOAD],
    pub len:      usize,
    pub cs_index: u8,
    /// Set to `true` by the SPI manager when the transfer is complete.
    pub done:     AtomicBool,
}

impl Default for SpiRequest {
    fn default() -> Self {
        Self {
            tx_data:  [0u8; MAX_PAYLOAD],
            rx_data:  [0u8; MAX_PAYLOAD],
            len:      0,
            cs_index: 0,
            done:     AtomicBool::new(false),
        }
    }
}

/// Statically allocated SPSC queue — safe for use from interrupts.
static mut SPI_QUEUE: Queue<SpiRequest, QUEUE_SIZE> = Queue::new();

/// Post a transfer from any task (the producer side).
/// Returns `Err(req)` if the queue is full.
///
/// # Safety
/// `SPI_QUEUE` must only have a single producer. Enforce this via your
/// RTOS resource model or a `static mut` singleton pattern.
pub fn post_spi_transfer(
    producer: &mut Producer<'static, SpiRequest, QUEUE_SIZE>,
    tx: &[u8],
    len: usize,
    cs_index: u8,
) -> Result<(), ()> {
    let mut req = SpiRequest::default();
    req.tx_data[..len].copy_from_slice(&tx[..len]);
    req.len      = len;
    req.cs_index = cs_index;

    producer.enqueue(req).map_err(|_| ())
}

/// SPI manager — runs at the highest task priority, consumes requests.
/// Because no other task directly touches the SPI hardware, priority
/// inversion is structurally impossible.
pub fn spi_manager_run(
    consumer: &mut Consumer<'static, SpiRequest, QUEUE_SIZE>,
    spi:      &mut impl embedded_hal::blocking::spi::Transfer<u8>,
) {
    while let Some(mut req) = consumer.dequeue() {
        // Perform the transfer
        let buf = &mut req.rx_data[..req.len];
        buf.copy_from_slice(&req.tx_data[..req.len]);
        let _ = spi.transfer(buf);

        // Signal completion to the producer
        req.done.store(true, Ordering::Release);
    }
}

/// A producer task posts a request and polls for completion.
/// In practice, replace the spin-loop with a semaphore or notification.
pub fn producer_task_example(
    producer: &mut Producer<'static, SpiRequest, QUEUE_SIZE>,
) {
    let cmd = [0x9Fu8, 0x00, 0x00, 0x00];  // Read JEDEC ID
    post_spi_transfer(producer, &cmd, 4, 0).expect("SPI queue full");

    // (Spin-wait for demo purposes; a real system would block on a signal)
    // while !request.done.load(Ordering::Acquire) {}
}
```

### Example 4: Compile-Time Prevention with `critical-section` crate

```rust
use critical_section::Mutex;
use core::cell::RefCell;

/// On single-core Cortex-M systems, wrapping the SPI resource in a
/// `critical_section::Mutex` disables all interrupts during access.
/// This is the strongest form of the ceiling protocol (ceiling = max),
/// preventing priority inversion by construction — at the cost of
/// increased interrupt latency. Use only for very short transfers.
static SPI: Mutex<RefCell<Option<MySpi>>> = Mutex::new(RefCell::new(None));

pub fn short_spi_write(byte: u8) {
    critical_section::with(|cs| {
        // Interrupts are disabled for the duration of this closure.
        // No other task (at any priority) can preempt us here.
        if let Some(spi) = SPI.borrow_ref_mut(cs).as_mut() {
            spi.write(&[byte]).ok();
        }
    });
    // Interrupts re-enabled automatically when closure exits.
}
```

---

## Summary

Priority inversion is a serious concern whenever multiple tasks of differing priorities share an SPI bus. The key points to remember:

**The Problem.** A low-priority task holding the SPI mutex can be preempted by medium-priority tasks, blocking a high-priority task for an unbounded duration — even though the medium-priority task has no involvement with the SPI resource.

**Prevention Strategies.**

- **Priority Inheritance Protocol (PIP):** Supported natively by FreeRTOS mutexes and most RTOS. The holder is temporarily boosted to the waiter's priority. Easy to use but blocking is not bounded to zero.
- **Priority Ceiling Protocol (PCP):** Every mutex has a preset ceiling priority; the holder is immediately boosted. Provides bounded blocking and prevents deadlock. Use with POSIX `PTHREAD_PRIO_PROTECT` or RTIC's compile-time ceiling.
- **Lock-Free Queuing:** Eliminate shared mutex access entirely by routing all SPI transfers through a single high-priority manager task. Structurally prevents inversion; adds asynchrony and complexity.
- **Critical Section (Interrupt Disable):** Guarantees mutual exclusion on single-core systems; suitable only for very short transfers due to interrupt latency impact.

**Language Considerations.** C/C++ with FreeRTOS or POSIX pthreads provide priority inheritance/ceiling at runtime. Rust's RTIC framework enforces the ceiling protocol **at compile time** — if a resource access violates priority relationships, the code will not compile. Embassy provides async-aware mutexes with ceiling semantics. The `heapless` SPSC queue enables lock-free designs in `no_std` environments.

**Best Practice Checklist.**

- Always use `xSemaphoreCreateMutex()` (not binary semaphores) in FreeRTOS — only mutexes implement priority inheritance.
- Keep SPI critical sections as short as possible: assert CS → transfer → deassert CS → release mutex.
- Never sleep, yield, or perform blocking I/O while holding the SPI mutex.
- For systems with strict real-time requirements, prefer PCP (bounded blocking) over PIP (unbounded blocking).
- Consider the lock-free queue pattern for complex systems with many tasks and SPI slaves, trading latency for structural safety.
- In Rust, prefer RTIC for bare-metal RTOS-style work — its resource model makes priority inversion a compile-time error rather than a runtime hazard.

---

*Document: 58 — Priority Inversion Prevention | SPI Resource Sharing Across Tasks*