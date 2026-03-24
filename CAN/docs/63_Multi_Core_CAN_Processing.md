# 63. Multi-Core CAN Processing

> Distributing CAN processing across multiple CPU cores with lock-free queues and synchronization strategies.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [Core Concepts](#core-concepts)
   - [Lock-Free Data Structures](#lock-free-data-structures)
   - [Memory Ordering and Atomics](#memory-ordering-and-atomics)
   - [Core Affinity and NUMA Awareness](#core-affinity-and-numa-awareness)
4. [Design Patterns](#design-patterns)
   - [Producer-Consumer Pattern](#producer-consumer-pattern)
   - [Work Stealing Queues](#work-stealing-queues)
   - [Message Partitioning by CAN ID](#message-partitioning-by-can-id)
5. [C/C++ Implementation](#cc-implementation)
   - [Lock-Free SPSC Ring Buffer](#lock-free-spsc-ring-buffer)
   - [Lock-Free MPMC Queue](#lock-free-mpmc-queue)
   - [Multi-Core Dispatcher](#multi-core-dispatcher)
   - [Core Affinity Setup](#core-affinity-setup)
   - [Zero-Copy Message Pipeline](#zero-copy-message-pipeline)
6. [Rust Implementation](#rust-implementation)
   - [Lock-Free SPSC Queue in Rust](#lock-free-spsc-queue-in-rust)
   - [Multi-Core CAN Dispatcher in Rust](#multi-core-can-dispatcher-in-rust)
   - [Crossbeam-Based Pipeline](#crossbeam-based-pipeline)
   - [Rayon-Based Parallel Frame Processing](#rayon-based-parallel-frame-processing)
7. [Synchronization Strategies](#synchronization-strategies)
   - [Hazard Pointers](#hazard-pointers)
   - [RCU (Read-Copy-Update)](#rcu-read-copy-update)
   - [Epoch-Based Reclamation](#epoch-based-reclamation)
8. [Performance Considerations](#performance-considerations)
   - [Cache Line Padding](#cache-line-padding)
   - [Batching and Coalescing](#batching-and-coalescing)
   - [NUMA-Aware Allocation](#numa-aware-allocation)
9. [Real-World Topology Examples](#real-world-topology-examples)
10. [Testing and Validation](#testing-and-validation)
11. [Summary](#summary)

---

## Introduction

Modern embedded and automotive systems increasingly rely on multi-core processors — from dual-core microcontrollers like the Renesas RH850/U2A to high-performance SoCs like the NXP S32G used in domain controllers. As CAN networks grow in density (CAN FD at 8 Mbit/s, multiple buses, hundreds of signals per second), single-core designs face hard throughput ceilings.

**Multi-Core CAN Processing** distributes the CAN workload — reception, filtering, decoding, signal routing, and logging — across CPU cores to achieve:

- **Higher throughput** — parallel decoding of hundreds of frames per millisecond.
- **Lower latency** — dedicated interrupt cores avoid contention with application logic.
- **Determinism** — isolating CAN ISR cores from OS-scheduled cores reduces jitter.
- **Scalability** — adding processing pipelines without refactoring the entire stack.

The central challenge is **safe, efficient inter-core communication**. Naive locking (mutexes, spinlocks) introduces priority inversion, cache thrashing, and latency spikes that defeat the purpose of parallelism. The solution is **lock-free algorithms** backed by atomic operations and careful memory ordering.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        CAN Hardware                             │
│          (CAN0, CAN1, CAN2 … CANn controllers)                  │
└───────────────┬────────────────┬────────────────────────────────┘
                │ IRQ            │ IRQ
                ▼                ▼
┌───────────────────┐    ┌───────────────────┐
│   Core 0          │    │   Core 1           │
│  (IRQ Handler /   │    │  (IRQ Handler /    │
│   RX Producer)    │    │   RX Producer)     │
│                   │    │                    │
│  SPSC Ring Buf ──►│    │  SPSC Ring Buf ──► │
└────────┬──────────┘    └────────┬───────────┘
         │ lock-free enqueue       │ lock-free enqueue
         ▼                         ▼
┌─────────────────────────────────────────────┐
│           MPMC Dispatch Queue               │
│        (shared between all workers)         │
└────────┬──────────────────────┬─────────────┘
         │                      │
         ▼                      ▼
┌─────────────────┐    ┌─────────────────┐
│   Core 2        │    │   Core 3        │
│  Worker Thread  │    │  Worker Thread  │
│  (Decode/Route) │    │  (Decode/Route) │
│                 │    │                 │
│  Signal DB      │    │  Signal DB      │
│  Application    │    │  Logging        │
└─────────────────┘    └─────────────────┘
```

**Key principle**: IRQ cores are pinned and produce frames into per-core SPSC queues. Worker cores consume from a shared MPMC queue, performing decoding and routing. No locks cross the IRQ boundary.

---

## Core Concepts

### Lock-Free Data Structures

A **lock-free** structure guarantees system-wide progress: at least one thread completes its operation in a finite number of steps, even if others are arbitrarily delayed or suspended. This is crucial for CAN IRQ handlers, which must never block.

| Structure | Producers | Consumers | Use Case |
|-----------|-----------|-----------|----------|
| SPSC Ring Buffer | 1 | 1 | IRQ core → single worker |
| MPSC Queue | Many | 1 | All cores → logger |
| MPMC Queue | Many | Many | Dispatch pool |
| Work-Stealing Deque | 1 owner | Many stealers | Load balancing |

### Memory Ordering and Atomics

Modern CPUs (and compilers) reorder memory operations for performance. In multi-core contexts, incorrect ordering causes data races without data races in the language-level sense. The C++11/Rust memory model defines:

| Ordering | Guarantee |
|----------|-----------|
| `Relaxed` | No ordering constraint — only atomicity |
| `Acquire` | No load/store after this load can move before it |
| `Release` | No load/store before this store can move after it |
| `AcqRel` | Both Acquire and Release |
| `SeqCst` | Total sequential consistency — most expensive |

For a SPSC queue: the producer uses `Release` on the write index; the consumer uses `Acquire` on the read index. This pairs correctly and avoids the overhead of `SeqCst`.

### Core Affinity and NUMA Awareness

On Linux (including real-time embedded Linux), cores can be pinned using `pthread_setaffinity_np`. On AUTOSAR/bare-metal, core assignment is done at startup via OS abstraction layers. Pinning ensures:

- IRQ handlers run on dedicated cores, avoiding migration.
- L1/L2 caches stay warm for per-core structures.
- NUMA domains are respected: avoid allocating shared queues on the "wrong" NUMA node.

---

## Design Patterns

### Producer-Consumer Pattern

The most common multi-core CAN pattern. One or more IRQ/DMA cores write received frames; one or more worker cores decode them.

```
CAN ISR (Core 0) ──[SPSC]──► Decoder (Core 2)
CAN ISR (Core 1) ──[SPSC]──► Decoder (Core 3)
```

The SPSC queue is wait-free: both producer and consumer always complete in O(1) without helping each other.

### Work Stealing Queues

Used for load balancing. Each worker has a private deque. When idle, it "steals" from the tail of a busy worker's deque. Used in high-density CAN scenarios where frame rates are uneven across buses.

### Message Partitioning by CAN ID

A deterministic sharding strategy: route frames to workers based on `CAN_ID % num_workers`. This ensures:
- All frames for a given ID are always processed in order by the same core.
- No additional synchronization for per-signal state.
- Cache affinity: each worker's signal state stays in its L1/L2.

---

## C/C++ Implementation

### Lock-Free SPSC Ring Buffer

A classic power-of-two ring buffer using `std::atomic`. This is the backbone of IRQ-to-worker communication.

```cpp
// spsc_ring.hpp
#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <cstddef>
#include <cassert>

/// Single-Producer Single-Consumer lock-free ring buffer.
/// Template parameter N must be a power of two.
/// CacheLineSize ensures head/tail indices reside on separate cache lines
/// to prevent false sharing.
template<typename T, std::size_t N, std::size_t CacheLineSize = 64>
class SPSCRing {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

    struct alignas(CacheLineSize) AlignedAtomic {
        std::atomic<std::size_t> val{0};
    };

    AlignedAtomic head_; // written by consumer, read by producer
    AlignedAtomic tail_; // written by producer, read by consumer

    // Pad the buffer itself to avoid sharing a cache line with indices
    alignas(CacheLineSize) std::array<T, N> buf_;

    static constexpr std::size_t MASK = N - 1;

public:
    SPSCRing() = default;
    SPSCRing(const SPSCRing&) = delete;
    SPSCRing& operator=(const SPSCRing&) = delete;

    /// Push item from the producer side (e.g. CAN ISR).
    /// Returns false if full (caller must handle overflow — log or drop).
    [[nodiscard]] bool push(T item) noexcept {
        const std::size_t t = tail_.val.load(std::memory_order_relaxed);
        const std::size_t next = (t + 1) & MASK;
        // Acquire: ensure we see all consumer writes to head
        if (next == head_.val.load(std::memory_order_acquire))
            return false; // full
        buf_[t] = std::move(item);
        // Release: make buf_[t] visible before advancing tail
        tail_.val.store(next, std::memory_order_release);
        return true;
    }

    /// Pop item from the consumer side (worker thread).
    /// Returns std::nullopt if empty.
    [[nodiscard]] std::optional<T> pop() noexcept {
        const std::size_t h = head_.val.load(std::memory_order_relaxed);
        // Acquire: ensure we see all producer writes to buf_ and tail
        if (h == tail_.val.load(std::memory_order_acquire))
            return std::nullopt; // empty
        T item = std::move(buf_[h]);
        // Release: signal producer that slot is free
        head_.val.store((h + 1) & MASK, std::memory_order_release);
        return item;
    }

    /// Approximate size — not precise under concurrent access.
    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::size_t t = tail_.val.load(std::memory_order_relaxed);
        const std::size_t h = head_.val.load(std::memory_order_relaxed);
        return (t - h) & MASK;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.val.load(std::memory_order_acquire)
            == tail_.val.load(std::memory_order_acquire);
    }
};
```

### Lock-Free MPMC Queue

When multiple producer cores and multiple consumer cores share a queue, we need an MPMC (Multi-Producer Multi-Consumer) structure. This uses a slot-based approach with sequence numbers.

```cpp
// mpmc_queue.hpp
#pragma once
#include <atomic>
#include <vector>
#include <optional>
#include <cstddef>

/// Lock-free MPMC bounded queue based on Dmitry Vyukov's design.
/// Each slot has a sequence number; producers claim a slot by CAS-ing
/// the sequence from (pos) to (pos+1); consumers release by setting (pos+N).
template<typename T>
class MPMCQueue {
    struct Slot {
        std::atomic<std::size_t> seq;
        T data;
    };

    static constexpr std::size_t CACHE_LINE = 64;

    alignas(CACHE_LINE) std::atomic<std::size_t> enqueue_pos_{0};
    alignas(CACHE_LINE) std::atomic<std::size_t> dequeue_pos_{0};

    std::vector<Slot> slots_;
    std::size_t capacity_;
    std::size_t mask_;

public:
    explicit MPMCQueue(std::size_t capacity)
        : slots_(capacity), capacity_(capacity), mask_(capacity - 1)
    {
        assert((capacity & (capacity - 1)) == 0 && "capacity must be power of 2");
        for (std::size_t i = 0; i < capacity; ++i)
            slots_[i].seq.store(i, std::memory_order_relaxed);
    }

    /// Enqueue. Returns false if full.
    [[nodiscard]] bool enqueue(T item) noexcept {
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = slots_[pos & mask_];
            std::size_t seq = slot.seq.load(std::memory_order_acquire);
            std::intptr_t diff = static_cast<std::intptr_t>(seq)
                               - static_cast<std::intptr_t>(pos);
            if (diff == 0) {
                // Slot is free; try to claim it
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed))
                {
                    slot.data = std::move(item);
                    slot.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // queue full
            } else {
                // Another producer advanced; reload pos
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    /// Dequeue. Returns std::nullopt if empty.
    [[nodiscard]] std::optional<T> dequeue() noexcept {
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = slots_[pos & mask_];
            std::size_t seq = slot.seq.load(std::memory_order_acquire);
            std::intptr_t diff = static_cast<std::intptr_t>(seq)
                               - static_cast<std::intptr_t>(pos + 1);
            if (diff == 0) {
                // Data available; try to claim dequeue slot
                if (dequeue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed))
                {
                    T item = std::move(slot.data);
                    slot.seq.store(pos + capacity_, std::memory_order_release);
                    return item;
                }
            } else if (diff < 0) {
                return std::nullopt; // empty
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }
};
```

### Multi-Core Dispatcher

Ties together the IRQ producers and worker consumers:

```cpp
// can_dispatcher.cpp
#include "spsc_ring.hpp"
#include "mpmc_queue.hpp"
#include <linux/can.h>
#include <pthread.h>
#include <sched.h>
#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdio>

// CAN frame plus metadata
struct CANFrame {
    can_frame  raw;
    uint8_t    bus_id;    // which CAN bus received it
    uint64_t   timestamp_ns;
};

// Per-core SPSC buffer (IRQ → Dispatcher thread)
// 4096 slots ≈ 4096 × ~24 bytes = ~96 KB per core
static constexpr std::size_t SPSC_DEPTH = 4096;
static constexpr std::size_t MPMC_DEPTH = 16384;
static constexpr int         NUM_CAN_CORES = 2;   // cores running CAN ISRs
static constexpr int         NUM_WORKERS   = 4;   // decoder/router cores

using FrameQueue = SPSCRing<CANFrame, SPSC_DEPTH>;
using DispatchQ  = MPMCQueue<CANFrame>;

// One SPSC per IRQ core
static FrameQueue g_rx_queues[NUM_CAN_CORES];

// Shared dispatch queue consumed by workers
static DispatchQ  g_dispatch{MPMC_DEPTH};

// Shutdown flag
static std::atomic<bool> g_stop{false};

// ── Utility: pin thread to a specific CPU core ────────────────────────────
void pin_thread(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t self = pthread_self();
    if (pthread_setaffinity_np(self, sizeof(cpu_set_t), &cpuset) != 0)
        perror("pthread_setaffinity_np");
}

// ── Simulated CAN ISR (in real code this is triggered by hardware) ─────────
// IRQ handler fills the per-core SPSC queue. Must never block.
void can_isr_handler(int core_index, const CANFrame& frame) {
    if (!g_rx_queues[core_index].push(frame)) {
        // Queue full: overflow counter or log (no blocking!)
        // In production: increment a per-core overrun counter
    }
}

// ── Dispatcher thread: drains SPSC → pushes to MPMC dispatch queue ────────
// Runs on the same core as the IRQ to maximise cache locality.
void dispatcher_thread(int core_index, int cpu_id) {
    pin_thread(cpu_id);
    FrameQueue& q = g_rx_queues[core_index];

    while (!g_stop.load(std::memory_order_relaxed)) {
        // Drain all available frames in one pass (batching reduces overhead)
        while (auto frame = q.pop()) {
            // CAN ID partitioning: deterministic routing for ordered processing
            // (optional: could also push to per-worker queues directly)
            if (!g_dispatch.enqueue(std::move(*frame))) {
                // Dispatch queue full — log overrun
            }
        }
        // Brief pause avoids 100% CPU spin; use eventfd in production
        // for interrupt-driven wake-up
        std::this_thread::yield();
    }
}

// ── Worker thread: decodes and routes CAN frames ──────────────────────────
void worker_thread(int worker_id, int cpu_id,
                   std::function<void(int, const CANFrame&)> handler)
{
    pin_thread(cpu_id);

    while (!g_stop.load(std::memory_order_relaxed)) {
        auto frame = g_dispatch.dequeue();
        if (frame) {
            handler(worker_id, *frame);
        } else {
            std::this_thread::yield();
        }
    }
}

// ── Example decode handler ────────────────────────────────────────────────
void decode_handler(int worker_id, const CANFrame& f) {
    // Deterministic routing by CAN ID ensures ordered processing per signal
    uint32_t id = f.raw.can_id & CAN_SFF_MASK;
    // Route to appropriate signal database partition
    (void)worker_id;
    // ... actual DBC decoding here ...
    (void)id;
}

// ── Main: launch all threads ───────────────────────────────────────────────
int main() {
    // Dispatcher threads pinned to cores 0 and 1 (where IRQs are affined)
    std::vector<std::thread> dispatchers;
    for (int i = 0; i < NUM_CAN_CORES; ++i)
        dispatchers.emplace_back(dispatcher_thread, i, i);

    // Worker threads pinned to cores 2–5
    std::vector<std::thread> workers;
    for (int i = 0; i < NUM_WORKERS; ++i)
        workers.emplace_back(worker_thread, i, i + NUM_CAN_CORES, decode_handler);

    // ... run for some time, then:
    g_stop.store(true, std::memory_order_seq_cst);
    for (auto& t : dispatchers) t.join();
    for (auto& t : workers)     t.join();
    return 0;
}
```

### Core Affinity Setup

Setting up IRQ affinity on Linux for a CAN interface:

```cpp
#include <cstdio>
#include <cstring>

/// Set the SMP IRQ affinity for a given IRQ number to a specific CPU mask.
/// \param irq_number  The interrupt number (from /proc/interrupts)
/// \param cpu_mask    Hex bitmask, e.g. "1" for CPU0, "2" for CPU1, "c" for CPU2+3
void set_irq_affinity(int irq_number, const char* cpu_mask) {
    char path[128];
    snprintf(path, sizeof(path),
             "/proc/irq/%d/smp_affinity", irq_number);
    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen smp_affinity"); return; }
    fputs(cpu_mask, f);
    fclose(f);
}

/// Isolate a core from the general scheduler using isolcpus (kernel boot param)
/// and prevent IRQ balancing:
///   Kernel cmdline: isolcpus=0,1 nohz_full=0,1 rcu_nocbs=0,1
///   Then at runtime:
void setup_can_affinity() {
    // CAN0 IRQ (e.g. IRQ 45) → Core 0 only (mask = 0x1)
    set_irq_affinity(45, "1");
    // CAN1 IRQ (e.g. IRQ 46) → Core 1 only (mask = 0x2)
    set_irq_affinity(46, "2");
    // Stop irqbalance from moving IRQs back
    // system("systemctl stop irqbalance");
}
```

### Zero-Copy Message Pipeline

Avoid copies in hot paths by passing indices into a pre-allocated frame pool:

```cpp
// zero_copy_pool.hpp
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

/// Pre-allocated frame pool with reference counting.
/// ISR stores directly into pool slots; only slot indices flow through queues.
template<std::size_t POOL_SIZE>
class FramePool {
public:
    struct Slot {
        CANFrame frame;
        std::atomic<int> ref_count{0};
    };

    /// Acquire a free slot index. Returns INVALID if pool is exhausted.
    static constexpr uint32_t INVALID = UINT32_MAX;

    [[nodiscard]] uint32_t acquire() noexcept {
        for (uint32_t i = 0; i < POOL_SIZE; ++i) {
            int expected = 0;
            if (slots_[i].ref_count.compare_exchange_strong(
                    expected, 1, std::memory_order_acq_rel))
                return i;
        }
        return INVALID;
    }

    void retain(uint32_t idx) noexcept {
        slots_[idx].ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    void release(uint32_t idx) noexcept {
        slots_[idx].ref_count.fetch_sub(1, std::memory_order_release);
    }

    CANFrame& operator[](uint32_t idx) noexcept { return slots_[idx].frame; }
    const CANFrame& operator[](uint32_t idx) const noexcept { return slots_[idx].frame; }

private:
    alignas(64) std::array<Slot, POOL_SIZE> slots_;
};

// Usage: ISR acquires slot, fills frame, pushes index through SPSC.
// Worker receives index, processes frame, calls release().
```

---

## Rust Implementation

Rust's ownership model and fearless concurrency make lock-free multi-core CAN code safer than C++. The `std::sync::atomic` module mirrors C++ atomics; crates like `crossbeam` and `rayon` provide battle-tested primitives.

### Lock-Free SPSC Queue in Rust

```rust
// spsc_ring.rs
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::mem::MaybeUninit;

const CACHE_LINE: usize = 64;

/// Cache-line padded atomic to prevent false sharing.
#[repr(align(64))]
struct CachePadded(AtomicUsize);

/// Single-Producer Single-Consumer lock-free ring buffer.
/// N must be a power of two.
pub struct SPSCRing<T, const N: usize> {
    head: CachePadded,                         // written by consumer
    tail: CachePadded,                         // written by producer
    buf:  [UnsafeCell<MaybeUninit<T>>; N],
}

// SAFETY: We guarantee single-producer single-consumer usage.
unsafe impl<T: Send, const N: usize> Send for SPSCRing<T, N> {}
unsafe impl<T: Send, const N: usize> Sync for SPSCRing<T, N> {}

impl<T, const N: usize> SPSCRing<T, N> {
    const MASK: usize = N - 1;

    pub fn new() -> Self {
        assert!(N.is_power_of_two(), "N must be a power of two");
        // SAFETY: MaybeUninit array initialisation
        let buf = unsafe {
            let mut arr: [UnsafeCell<MaybeUninit<T>>; N] = MaybeUninit::uninit().assume_init();
            for slot in arr.iter_mut() {
                *slot = UnsafeCell::new(MaybeUninit::uninit());
            }
            arr
        };
        Self {
            head: CachePadded(AtomicUsize::new(0)),
            tail: CachePadded(AtomicUsize::new(0)),
            buf,
        }
    }

    /// Push from producer side. Returns Err(item) if full.
    pub fn push(&self, item: T) -> Result<(), T> {
        let tail = self.tail.0.load(Ordering::Relaxed);
        let next = (tail + 1) & Self::MASK;
        if next == self.head.0.load(Ordering::Acquire) {
            return Err(item); // full
        }
        // SAFETY: Only producer writes to buf[tail].
        unsafe {
            (*self.buf[tail].get()).write(item);
        }
        self.tail.0.store(next, Ordering::Release);
        Ok(())
    }

    /// Pop from consumer side. Returns None if empty.
    pub fn pop(&self) -> Option<T> {
        let head = self.head.0.load(Ordering::Relaxed);
        if head == self.tail.0.load(Ordering::Acquire) {
            return None; // empty
        }
        // SAFETY: Only consumer reads buf[head]; slot is initialised (tail advanced).
        let item = unsafe { (*self.buf[head].get()).assume_init_read() };
        self.head.0.store((head + 1) & Self::MASK, Ordering::Release);
        Some(item)
    }
}

impl<T, const N: usize> Drop for SPSCRing<T, N> {
    fn drop(&mut self) {
        // Drain remaining items to run their destructors
        while self.pop().is_some() {}
    }
}
```

### Multi-Core CAN Dispatcher in Rust

```rust
// can_dispatcher.rs
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::Duration;
use libc::{cpu_set_t, pthread_setaffinity_np, pthread_self, CPU_SET, CPU_ZERO};

#[derive(Clone, Copy, Debug)]
pub struct CanFrame {
    pub id:           u32,
    pub bus:          u8,
    pub dlc:          u8,
    pub data:         [u8; 8],
    pub timestamp_ns: u64,
}

const SPSC_SIZE: usize = 4096;
const MPMC_SIZE: usize = 16384;

/// Pin the current thread to a specific CPU core.
pub fn pin_to_core(core: usize) {
    unsafe {
        let mut set: cpu_set_t = std::mem::zeroed();
        CPU_ZERO(&mut set);
        CPU_SET(core, &mut set);
        let tid = pthread_self();
        let ret = pthread_setaffinity_np(
            tid,
            std::mem::size_of::<cpu_set_t>(),
            &set,
        );
        if ret != 0 {
            eprintln!("Failed to set CPU affinity for core {}: {}", core, ret);
        }
    }
}

pub fn run_multi_core_pipeline(num_workers: usize) {
    let stop = Arc::new(AtomicBool::new(false));

    // Shared MPMC dispatch queue via crossbeam
    let (tx, rx) = crossbeam::channel::bounded::<CanFrame>(MPMC_SIZE);

    // --- Dispatcher threads (simulate CAN ISR → queue) ---
    let dispatchers: Vec<_> = (0..2).map(|core_id| {
        let tx = tx.clone();
        let stop = Arc::clone(&stop);
        thread::spawn(move || {
            pin_to_core(core_id);
            let mut seq: u64 = 0;
            while !stop.load(Ordering::Relaxed) {
                // Simulate receiving a CAN frame from hardware
                let frame = CanFrame {
                    id:           (seq % 0x7FF) as u32,
                    bus:          core_id as u8,
                    dlc:          8,
                    data:         [0u8; 8],
                    timestamp_ns: seq * 1_000,
                };
                seq += 1;
                if tx.try_send(frame).is_err() {
                    // Overflow — increment overrun counter
                }
                thread::sleep(Duration::from_micros(100));
            }
        })
    }).collect();

    // --- Worker threads ---
    let workers: Vec<_> = (0..num_workers).map(|worker_id| {
        let rx = rx.clone();
        let stop = Arc::clone(&stop);
        thread::spawn(move || {
            pin_to_core(2 + worker_id);
            while !stop.load(Ordering::Relaxed) {
                match rx.try_recv() {
                    Ok(frame) => decode_frame(worker_id, &frame),
                    Err(_)    => thread::yield_now(),
                }
            }
        })
    }).collect();

    // Run for demonstration
    thread::sleep(Duration::from_millis(500));
    stop.store(true, Ordering::SeqCst);

    for d in dispatchers { d.join().unwrap(); }
    for w in workers     { w.join().unwrap(); }
}

fn decode_frame(worker_id: usize, frame: &CanFrame) {
    // Deterministic partitioning ensures ordered signal processing
    let _partition = (frame.id as usize) % 4;
    println!(
        "[Worker {}] BUS={} ID=0x{:03X} DLC={} ts={}ns",
        worker_id, frame.bus, frame.id, frame.dlc, frame.timestamp_ns
    );
}
```

### Crossbeam-Based Pipeline

Using the `crossbeam` crate for ergonomic and correct lock-free channels:

```rust
// crossbeam_pipeline.rs
use crossbeam::channel::{bounded, select, Receiver, Sender};
use crossbeam::deque::{Injector, Steal, Worker};
use std::sync::Arc;
use std::thread;

#[derive(Clone, Debug)]
pub struct CanFrame {
    pub id:   u32,
    pub data: [u8; 8],
}

/// Build a work-stealing pipeline for CAN frame processing.
/// 
/// Architecture:
///   Injector (global queue) ← multiple producers
///   Worker deques           ← each thread has a local deque
///   Stealing                ← idle threads steal from busy ones
pub fn work_stealing_pipeline(num_workers: usize) {
    // Global injector — producers push frames here
    let injector: Arc<Injector<CanFrame>> = Arc::new(Injector::new());

    // Per-worker deques and stealers
    let workers: Vec<Worker<CanFrame>> = (0..num_workers)
        .map(|_| Worker::new_fifo())
        .collect();
    let stealers: Vec<_> = workers.iter().map(|w| w.stealer()).collect();

    let (stop_tx, stop_rx) = bounded::<()>(1);

    let worker_threads: Vec<_> = workers.into_iter().enumerate().map(|(id, worker)| {
        let injector = Arc::clone(&injector);
        let stealers = stealers.clone();
        let stop_rx  = stop_rx.clone();

        thread::spawn(move || {
            loop {
                // 1. Try own deque first (hottest cache)
                let task = worker.pop().or_else(|| {
                    // 2. Try global injector
                    loop {
                        match injector.steal_batch_and_pop(&worker) {
                            Steal::Success(t) => break Some(t),
                            Steal::Empty      => break None,
                            Steal::Retry      => continue,
                        }
                    }
                }).or_else(|| {
                    // 3. Steal from other workers
                    stealers.iter()
                        .filter(|_| true) // could skip own index
                        .find_map(|s| loop {
                            match s.steal() {
                                Steal::Success(t) => break Some(t),
                                Steal::Empty      => break None,
                                Steal::Retry      => continue,
                            }
                        })
                });

                if let Some(frame) = task {
                    process_frame(id, &frame);
                } else {
                    // Check for shutdown
                    if stop_rx.try_recv().is_ok() { break; }
                    thread::yield_now();
                }
            }
        })
    }).collect();

    // Simulate CAN frame injection
    let injector_ref = Arc::clone(&injector);
    let producer = thread::spawn(move || {
        for i in 0..1000u32 {
            injector_ref.push(CanFrame { id: i & 0x7FF, data: [0u8; 8] });
            thread::yield_now();
        }
    });

    producer.join().unwrap();
    let _ = stop_tx.send(());
    for t in worker_threads { t.join().unwrap(); }
}

fn process_frame(worker_id: usize, frame: &CanFrame) {
    let _ = (worker_id, frame); // decode, route, log …
}
```

### Rayon-Based Parallel Frame Processing

For batch processing workloads (e.g. post-processing logged CAN traces):

```rust
// rayon_batch.rs
use rayon::prelude::*;

#[derive(Debug, Clone)]
pub struct CanFrame {
    pub id:   u32,
    pub data: [u8; 8],
}

#[derive(Debug)]
pub struct DecodedSignal {
    pub frame_id: u32,
    pub name:     String,
    pub value:    f64,
}

/// Decode a batch of CAN frames in parallel across all available cores.
/// Each frame is independent: ideal for Rayon's data-parallelism.
pub fn parallel_decode_batch(frames: &[CanFrame]) -> Vec<DecodedSignal> {
    frames.par_iter()
        .flat_map(|frame| decode_frame_signals(frame))
        .collect()
}

fn decode_frame_signals(frame: &CanFrame) -> Vec<DecodedSignal> {
    // Simulate DBC-based signal decoding
    match frame.id {
        0x100 => {
            let raw = u16::from_le_bytes([frame.data[0], frame.data[1]]);
            vec![DecodedSignal {
                frame_id: frame.id,
                name:  "EngineRPM".to_string(),
                value: raw as f64 * 0.25,
            }]
        }
        0x200 => {
            let raw = frame.data[0] as i8;
            vec![DecodedSignal {
                frame_id: frame.id,
                name:  "SteeringAngle".to_string(),
                value: raw as f64 * 1.5,
            }]
        }
        _ => vec![],
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parallel_decode() {
        let frames: Vec<CanFrame> = (0..10_000)
            .map(|i| CanFrame {
                id:   if i % 2 == 0 { 0x100 } else { 0x200 },
                data: [i as u8; 8],
            })
            .collect();

        let signals = parallel_decode_batch(&frames);
        assert_eq!(signals.len(), 10_000);
    }
}
```

---

## Synchronization Strategies

### Hazard Pointers

Used when workers need to read shared signal state that can be concurrently updated (e.g. a live signal database). A hazard pointer "protects" a pointer from being freed by a concurrent updater.

```cpp
// hazard_ptr_example.cpp — simplified illustration
#include <atomic>
#include <array>
#include <thread>

struct SignalDB {
    float engine_rpm;
    float vehicle_speed;
    // ... hundreds of signals ...
};

static std::atomic<SignalDB*> g_signal_db{new SignalDB{}};

// Per-thread hazard pointer slot
static constexpr int MAX_THREADS = 8;
static std::atomic<SignalDB*> g_hazard[MAX_THREADS];

// Worker reads the signal DB safely under hazard pointer protection
void worker_read(int thread_id) {
    while (true) {
        SignalDB* ptr;
        // Announce we are reading: loop until stable
        do {
            ptr = g_signal_db.load(std::memory_order_acquire);
            g_hazard[thread_id].store(ptr, std::memory_order_release);
        } while (ptr != g_signal_db.load(std::memory_order_acquire));

        // Safe to dereference ptr — updater will not free it
        float rpm = ptr->engine_rpm;
        (void)rpm;

        // Done reading
        g_hazard[thread_id].store(nullptr, std::memory_order_release);
        std::this_thread::yield();
    }
}

// Updater replaces the signal DB and waits for readers to finish
void updater(SignalDB* new_db) {
    SignalDB* old = g_signal_db.exchange(new_db, std::memory_order_acq_rel);
    // Scan hazard pointers: wait until no thread holds 'old'
    bool safe = false;
    while (!safe) {
        safe = true;
        for (int i = 0; i < MAX_THREADS; ++i) {
            if (g_hazard[i].load(std::memory_order_acquire) == old) {
                safe = false;
                std::this_thread::yield();
                break;
            }
        }
    }
    delete old;
}
```

### RCU (Read-Copy-Update)

RCU is the gold standard for read-heavy shared data in Linux kernels and is applicable in user-space CAN systems via `liburcu`:

```cpp
// rcu_signal_db.cpp (using liburcu-qsbr)
// Build: g++ -o rcu_demo rcu_signal_db.cpp -lurcu-qsbr
#include <urcu/urcu-qsbr.h>
#include <urcu/rculist.h>
#include <atomic>
#include <cstdio>

struct SignalDB {
    struct rcu_head rcu;
    float engine_rpm;
    float vehicle_speed;
};

static SignalDB* g_db = nullptr;

// Reader (CAN worker thread) — quiescent-state based
void reader_thread() {
    rcu_register_thread();
    for (int i = 0; i < 1000; ++i) {
        rcu_read_lock();
        SignalDB* db = rcu_dereference(g_db);
        if (db) {
            float rpm = db->engine_rpm; // safe read
            (void)rpm;
        }
        rcu_read_unlock();
        rcu_quiescent_state(); // signal quiescent state for grace period
    }
    rcu_unregister_thread();
}

// Writer: copy-update-publish
void update_signal_db(float new_rpm) {
    SignalDB* old_db = rcu_dereference(g_db);
    SignalDB* new_db = new SignalDB(*old_db);
    new_db->engine_rpm = new_rpm;

    rcu_assign_pointer(g_db, new_db); // publish atomically
    synchronize_rcu();                 // wait for all readers using old_db
    delete old_db;
}
```

### Epoch-Based Reclamation

Used in `crossbeam-epoch` (Rust) — the basis for crossbeam's lock-free data structures. Workers pin themselves to the current epoch; objects are garbage-collected only when all threads have advanced past the epoch they were destroyed in.

```rust
// epoch_example.rs
use crossbeam_epoch::{self as epoch, Atomic, Owned, Shared};
use std::sync::atomic::Ordering;

struct SignalDB {
    engine_rpm: f32,
    vehicle_speed: f32,
}

struct SharedState {
    db: Atomic<SignalDB>,
}

impl SharedState {
    fn read(&self) -> f32 {
        // Pin current thread to epoch — prevents reclamation during access
        let guard = epoch::pin();
        let db: Shared<SignalDB> = self.db.load(Ordering::Acquire, &guard);
        // SAFETY: db is valid while guard is alive (epoch pinned)
        unsafe { db.deref().engine_rpm }
    }

    fn update(&self, new_rpm: f32) {
        let new_db = Owned::new(SignalDB {
            engine_rpm: new_rpm,
            vehicle_speed: 0.0,
        });
        let guard = epoch::pin();
        let old = self.db.swap(new_db, Ordering::AcqRel, &guard);
        // Defer deletion until all threads have passed a safe epoch
        unsafe { guard.defer_destroy(old); }
    }
}
```

---

## Performance Considerations

### Cache Line Padding

False sharing — two threads writing to different variables that happen to share a 64-byte cache line — causes catastrophic performance on multi-core systems. Always pad hot atomic variables:

```cpp
// BAD: head and tail share a cache line → false sharing
struct BadQueue {
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};

// GOOD: each on its own cache line
struct GoodQueue {
    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;
};
```

```rust
// Rust: use repr(align) or a wrapper
#[repr(align(64))]
struct CachePadded<T>(T);
```

### Batching and Coalescing

Minimise the cost of atomic operations by batching. A dispatcher that drains 32 frames per atomic operation amortises the cache coherence overhead:

```cpp
void dispatcher_batch(FrameQueue& q, DispatchQ& dispatch) {
    constexpr int BATCH = 32;
    CANFrame batch[BATCH];
    int count = 0;
    while (count < BATCH) {
        auto f = q.pop();
        if (!f) break;
        batch[count++] = std::move(*f);
    }
    for (int i = 0; i < count; ++i)
        dispatch.enqueue(batch[i]);
}
```

### NUMA-Aware Allocation

On NUMA systems (common in automotive SoCs with multiple processor clusters), allocate shared queues on the NUMA node of the core that writes to them most:

```cpp
#include <numa.h>

// Allocate MPMC queue on NUMA node 0 (where IRQ cores reside)
void* mem = numa_alloc_onnode(sizeof(MPMCQueue<CANFrame>), /*node=*/0);
MPMCQueue<CANFrame>* dispatch = new (mem) MPMCQueue<CANFrame>(16384);
```

---

## Real-World Topology Examples

### Automotive Domain Controller (e.g. NXP S32G3)

```
Cluster A (Cortex-R52, real-time):
  Core 0 → CAN0/1 IRQ handling + SPSC producer
  Core 1 → CAN2/3 IRQ handling + SPSC producer

Cluster B (Cortex-A53, application):
  Core 2 → CAN frame dispatcher + MPMC consumer
  Core 3 → Signal decoder / DBC routing
  Core 4 → Logging / Ethernet forwarding
  Core 5 → Diagnostics / UDS gateway
```

### Multi-Bus CAN Logger (Linux PC)

```
IRQ Core 0 (isolated):
  SocketCAN read loop → SPSC[0]

IRQ Core 1 (isolated):
  SocketCAN read loop → SPSC[1]

Worker Core 2:
  MPMC consumer → MF4/BLFA2MF decode + write

Worker Core 3:
  MPMC consumer → Real-time signal monitoring

Worker Core 4:
  MPMC consumer → CAN → Ethernet bridge (SOME/IP)
```

---

## Testing and Validation

### Stress Test: Concurrent Producer/Consumer (C++)

```cpp
#include <thread>
#include <atomic>
#include <cassert>
#include "spsc_ring.hpp"

void test_spsc_stress() {
    SPSCRing<uint64_t, 1024> ring;
    constexpr uint64_t N = 1'000'000;
    std::atomic<uint64_t> consumed{0};

    std::thread producer([&]() {
        for (uint64_t i = 0; i < N; ++i) {
            while (!ring.push(i)) std::this_thread::yield();
        }
    });

    std::thread consumer([&]() {
        uint64_t expected = 0;
        while (expected < N) {
            if (auto v = ring.pop()) {
                assert(*v == expected++);
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();
    assert(consumed.load() == N);
    printf("SPSC stress test passed: %llu messages\n", (unsigned long long)N);
}
```

### Rust: Property-Based Testing with `proptest`

```rust
// tests/spsc_proptest.rs
use proptest::prelude::*;
use your_crate::SPSCRing;

proptest! {
    #[test]
    fn spsc_preserves_order(items in prop::collection::vec(0u32..10_000, 1..500)) {
        let ring: SPSCRing<u32, 1024> = SPSCRing::new();
        let mut popped = Vec::new();

        for &item in &items {
            // May fail if full; just stop pushing
            if ring.push(item).is_err() { break; }
        }
        while let Some(v) = ring.pop() {
            popped.push(v);
        }

        // Order must be preserved (FIFO)
        for (i, (&pushed, &got)) in items.iter().zip(popped.iter()).enumerate() {
            prop_assert_eq!(pushed, got,
                "Order violated at index {}: pushed {}, got {}", i, pushed, got);
        }
    }
}
```

### Key Metrics to Measure

| Metric | Tool | Target |
|--------|------|--------|
| Throughput (frames/sec) | `perf stat`, custom counters | > 1M frames/s per core |
| End-to-end latency (ISR → decode) | hardware timestamps | < 10 µs (CAN FD) |
| Overrun rate | per-core atomic counters | 0 under nominal load |
| Cache miss rate | `perf -e cache-misses` | < 1% for hot path |
| CPU utilisation per core | `top`, `htop` | IRQ core < 30% idle |

---

## Summary

Multi-core CAN processing addresses the throughput and latency limits of single-core designs by distributing reception, decoding, routing, and logging across dedicated CPU cores. The key takeaways are:

**Architecture**: Separate IRQ/production cores from worker/consumer cores. Use per-core SPSC ring buffers for the IRQ → worker handoff (zero lock overhead in the interrupt path) and MPMC queues for the shared worker pool.

**Lock-Free Foundations**: The SPSC ring buffer is the simplest and fastest correct primitive; MPMC adds CAS-based arbitration. Both depend critically on correct `Acquire`/`Release` memory ordering — not `SeqCst` (too slow) and not `Relaxed` (unsafe).

**Determinism via Partitioning**: Routing frames by `CAN_ID % num_workers` ensures per-signal ordering without additional synchronization and keeps each worker's signal state cache-warm.

**False Sharing**: Always align hot atomic indices to cache line boundaries (`alignas(64)` in C++, `#[repr(align(64))]` in Rust). Unpadded structures can lose 5–10× throughput to cache coherence traffic.

**Shared State Updates**: Use RCU or epoch-based reclamation for shared signal databases. These allow readers to proceed lock-free while writers safely replace and reclaim old data.

**Rust Advantages**: Rust's type system prevents data races at compile time; `crossbeam` provides production-grade lock-free channels and work-stealing deques; `rayon` enables painless parallel batch processing.

**Testing**: Stress-test queues with millions of messages; verify FIFO ordering; measure cache miss rates and overrun counters under realistic CAN bus loads. A well-tuned multi-core CAN pipeline can decode and route well over **1 million CAN frames per second** on a 4-core embedded SoC.

---

*Document: 63_Multi_Core_CAN_Processing.md*  
*Series: Embedded CAN Systems Engineering*