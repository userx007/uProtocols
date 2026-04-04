# 09. Clock Domain Crossing (CDC) in UART

**Theory** — what CDC is, why direct wiring across clock domains causes metastability, and the physics/math behind MTBF estimation.

**Three synchronization techniques** — double flip-flop (single bits), async FIFO with Gray-coded pointers (byte data), and four-phase handshake (low-bandwidth control signals), with their trade-offs explained.

**UART-specific concerns** — start bit detection latency, oversampling clock asynchrony, status register reads, and safe baud-rate reconfiguration.

**C/C++ examples:**
- A cycle-accurate double-FF synchronizer model
- Gray code encode/decode utilities
- A full asynchronous FIFO implementation with Gray-coded pointers
- An ARM Cortex-M RTOS ring buffer using `dmb`/`dsb` memory barriers
- An MTBF calculator

**Rust examples:**
- An `AtomicU8`-based `DffSync` struct with correct `Acquire`/`Release` ordering
- A `no_std`, lock-free `CdcFifo<const N>` SPSC FIFO with full unit tests covering roundtrip correctness, one-bit Gray code transitions, normal push/pop, and overflow behavior

> **Topic:** Handling asynchronous signals and metastability in UART receivers

---

## Table of Contents

1. [Introduction](#introduction)
2. [The Problem: Asynchronous Signal Crossing](#the-problem-asynchronous-signal-crossing)
3. [Metastability Explained](#metastability-explained)
4. [Clock Domain Crossing Techniques](#clock-domain-crossing-techniques)
   - [Double Flip-Flop Synchronizer](#1-double-flip-flop-synchronizer)
   - [FIFO-Based CDC](#2-fifo-based-cdc)
   - [Handshake Synchronizer](#3-handshake-synchronizer)
5. [UART-Specific CDC Challenges](#uart-specific-cdc-challenges)
6. [Implementation in C/C++](#implementation-in-cc)
   - [Bare-Metal CDC Simulation](#bare-metal-cdc-simulation)
   - [RTOS-Aware CDC Buffer](#rtos-aware-cdc-buffer)
7. [Implementation in Rust](#implementation-in-rust)
   - [Atomic-Based Synchronizer](#atomic-based-synchronizer)
   - [Lock-Free CDC FIFO](#lock-free-cdc-fifo)
8. [Measuring and Validating CDC Safety](#measuring-and-validating-cdc-safety)
9. [Summary](#summary)

---

## Introduction

In any digital system where a UART peripheral communicates with a processor or FPGA, the incoming serial data arrives from an **external, unsynchronized clock domain**. The UART receiver samples bits at its baud-rate clock, while the rest of the system—a CPU bus, DMA controller, or logic fabric—runs on a completely independent system clock. This boundary between two independent clocking regions is called a **Clock Domain Crossing (CDC)**.

Failing to handle CDC correctly is one of the most common sources of intermittent, hard-to-reproduce bugs in embedded and FPGA-based systems. Data corruption, system hangs, and undefined behavior can all trace back to improper CDC handling.

---

## The Problem: Asynchronous Signal Crossing

When a signal transitions in one clock domain and is sampled by a flip-flop in another, the sampling may occur during the signal's transition window—a period where the logic level is neither a valid HIGH nor a valid LOW. This violates the **setup and hold time** requirements of the receiving flip-flop.

```
Domain A (UART baud clock, e.g. 115200 Hz × 16 = 1.8432 MHz)
         ___     ___     ___     ___
        |   |   |   |   |   |   |   |
  ______|   |___|   |___|   |___|   |______

Domain B (System clock, e.g. 50 MHz)
   _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _
  |_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|

  Signal from Domain A sampled by Domain B:
  - Setup violation possible near any Domain A edge
```

The key principle: **you cannot simply wire a signal from clock domain A directly into logic in clock domain B.**

---

## Metastability Explained

**Metastability** is the condition where a flip-flop's output is in an indeterminate state—neither a valid 0 nor 1—for an unpredictable amount of time after a setup/hold violation. It is a fundamental analog phenomenon arising from the flip-flop's bistable latch physics.

### Key Properties

| Property | Description |
|---|---|
| **Duration** | Exponentially distributed; resolves within nanoseconds in the vast majority of cases |
| **Resolution probability** | MTBF (Mean Time Between Failures) can be calculated and made astronomically large |
| **Propagation** | If not resolved before the next clock edge, metastability can propagate downstream |
| **Detectability** | Cannot be detected in hardware; must be mitigated architecturally |

### MTBF Formula

```
MTBF = exp(t_resolve / τ) / (f_clk × f_data × T_w)
```

Where:
- `t_resolve` = resolution time window (period minus setup/hold)
- `τ` = flip-flop technology constant (from datasheet)
- `f_clk` = destination domain clock frequency
- `f_data` = data transition rate
- `T_w` = metastability window width

For a 50 MHz system clock, properly designed CDC synchronizers routinely achieve MTBF values exceeding millions of years.

---

## Clock Domain Crossing Techniques

### 1. Double Flip-Flop Synchronizer

The most fundamental CDC technique: pass the signal through **two consecutive flip-flops** clocked by the destination domain's clock. The first flip-flop may go metastable, but the second flip-flop samples a resolved value (with overwhelming probability) one full clock cycle later.

```
RX_in (Domain A)
    |
    ▼
  [FF1] ──── [FF2] ──── Synchronized output (Domain B)
     ↑           ↑
   clk_B       clk_B
```

This technique is appropriate for **single-bit control signals** (flags, enables, start bits), but **must not** be used for multi-bit data buses, as each bit may synchronize at different times, producing a transient invalid value.

---

### 2. FIFO-Based CDC

For multi-bit data (e.g., UART byte data), an **asynchronous FIFO** (First-In, First-Out buffer) with separate read and write clock domains is the standard solution. The FIFO manages the data transfer, using **Gray-coded pointers** for the read and write indices—Gray code ensures that only one bit changes per pointer increment, making it safe to synchronize a pointer across clock domains with a double flip-flop synchronizer.

```
Domain A (write)          Domain B (read)
    |                         |
 UART RX byte ──► [ASYNC FIFO] ──► CPU/DMA
    |                         |
  wr_clk                   rd_clk
  wr_ptr (Gray-coded) ──► synchronizer ──► full/empty logic
  rd_ptr (Gray-coded) ◄── synchronizer ◄── full/empty logic
```

---

### 3. Handshake Synchronizer

For low-bandwidth, latency-tolerant transfers, a **four-phase handshake** can be used:

1. Domain A asserts `req`
2. Domain B synchronizes `req`, processes data, asserts `ack`
3. Domain A synchronizes `ack`, deasserts `req`
4. Domain B deasserts `ack`

This guarantees glitch-free transfer at the cost of multiple clock cycles of latency per transfer (typically 4–6 cycles in each domain).

---

## UART-Specific CDC Challenges

UART introduces a particular set of CDC challenges:

1. **Start bit detection:** The falling edge of RX arrives asynchronously. The synchronizer adds latency, but this is acceptable as the receiver has a full bit period (~8–16 samples) to act.

2. **Oversampling and vote logic:** Most UART receivers oversample at 16× the baud rate to detect bit centers. The oversampling clock itself may be derived from a PLL that is asynchronous to the system bus clock.

3. **Status register reads:** When the CPU reads UART status bits (RX_READY, FRAMING_ERROR, etc.), those bits are set by the baud-rate clock domain and must be synchronized before the CPU reads them.

4. **Byte-wide data register:** The 8-bit received byte is multi-bit data. Transferring it to the system domain requires an asynchronous FIFO or a controlled handshake, never a direct bus connection.

5. **Baud rate clock glitches during reconfiguration:** If the baud rate divisor is changed while a transfer is in progress, glitches can occur. Proper CDC ensures reconfiguration is quiesced first.

---

## Implementation in C/C++

The following examples target embedded microcontrollers, simulating or implementing CDC mechanisms in software and/or demonstrating the correct hardware access patterns.

### Bare-Metal CDC Simulation

This example models a double flip-flop synchronizer in C, as would be implemented conceptually in a cycle-accurate software simulation or described for an FPGA soft-core:

```c
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Double flip-flop synchronizer state for a single-bit signal.
 *
 * Both flip-flops are updated on every rising edge of the destination clock.
 * In hardware this maps directly to two chained DFFs sharing a common clock.
 */
typedef struct {
    uint8_t ff1;   /**< First stage: may go metastable */
    uint8_t ff2;   /**< Second stage: resolved output */
} DFF2Sync;

/**
 * @brief Advance the synchronizer by one destination-clock cycle.
 *
 * Call this function once per destination clock cycle, passing the current
 * raw (unsynchronized) input from the source domain.
 *
 * @param sync  Pointer to synchronizer state
 * @param input Raw input from source clock domain (0 or 1)
 * @return      Synchronized, metastability-resolved output bit
 */
static inline uint8_t dff2_sync_tick(DFF2Sync *sync, uint8_t input)
{
    sync->ff2 = sync->ff1;   /* Second FF samples first FF's output */
    sync->ff1 = input & 0x1; /* First FF samples raw input           */
    return sync->ff2;
}

/* ------------------------------------------------------------------ */
/* Gray-coded pointer for asynchronous FIFO                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Convert a binary counter to Gray code.
 *
 * Only one bit changes between consecutive Gray code values, making
 * it safe to synchronize with a two-FF synchronizer.
 */
static inline uint8_t bin_to_gray(uint8_t binary)
{
    return binary ^ (binary >> 1);
}

/**
 * @brief Convert Gray code back to binary.
 */
static inline uint8_t gray_to_bin(uint8_t gray)
{
    uint8_t mask = gray >> 1;
    while (mask) {
        gray ^= mask;
        mask >>= 1;
    }
    return gray;
}

/* ------------------------------------------------------------------ */
/* Asynchronous FIFO for UART byte data (power-of-2 depth)            */
/* ------------------------------------------------------------------ */

#define FIFO_DEPTH   16u   /* Must be a power of 2 */
#define FIFO_MASK    (FIFO_DEPTH - 1u)
/* Use one extra pointer bit to distinguish full from empty */
#define PTR_BITS     5u    /* log2(FIFO_DEPTH) + 1 */
#define PTR_MASK     ((1u << PTR_BITS) - 1u)

typedef struct {
    uint8_t  buf[FIFO_DEPTH];

    /* Write side (UART baud-clock domain) */
    volatile uint8_t wr_ptr_bin;  /**< Binary write pointer         */
    volatile uint8_t wr_ptr_gray; /**< Gray-coded write pointer     */

    /* Read side (system clock domain) */
    volatile uint8_t rd_ptr_bin;  /**< Binary read pointer          */
    volatile uint8_t rd_ptr_gray; /**< Gray-coded read pointer      */

    /* Synchronized pointers (destination-domain copies)             */
    uint8_t  wr_ptr_gray_sync;   /**< Write pointer sync'd to read domain */
    uint8_t  rd_ptr_gray_sync;   /**< Read pointer sync'd to write domain */

    /* Two-stage synchronizer state for pointer crossing             */
    DFF2Sync wr_sync;  /**< Synchronizes wr_ptr into read domain  */
    DFF2Sync rd_sync;  /**< Synchronizes rd_ptr into write domain */
} AsyncFIFO;

/**
 * @brief Initialize an asynchronous FIFO.
 */
void async_fifo_init(AsyncFIFO *f)
{
    f->wr_ptr_bin  = 0;
    f->wr_ptr_gray = 0;
    f->rd_ptr_bin  = 0;
    f->rd_ptr_gray = 0;
    f->wr_ptr_gray_sync = 0;
    f->rd_ptr_gray_sync = 0;
    f->wr_sync = (DFF2Sync){0, 0};
    f->rd_sync = (DFF2Sync){0, 0};
}

/**
 * @brief Write one byte (called from UART / write clock domain).
 *
 * @return true on success, false if FIFO is full.
 */
bool async_fifo_write(AsyncFIFO *f, uint8_t byte)
{
    /* Synchronize the read pointer into the write domain */
    f->rd_ptr_gray_sync = dff2_sync_tick(&f->rd_sync, f->rd_ptr_gray);

    uint8_t wr_next_bin  = (f->wr_ptr_bin + 1u) & PTR_MASK;
    uint8_t wr_next_gray = bin_to_gray(wr_next_bin);

    /* FIFO full: write pointer (next) equals read pointer with MSB inverted */
    if (wr_next_gray == (f->rd_ptr_gray_sync ^ (1u << (PTR_BITS - 1u)))) {
        return false; /* FIFO full */
    }

    f->buf[f->wr_ptr_bin & FIFO_MASK] = byte;

    /* Update pointer atomically: binary first, then Gray code */
    f->wr_ptr_bin  = wr_next_bin;
    f->wr_ptr_gray = wr_next_gray; /* Gray pointer visible to read domain */

    return true;
}

/**
 * @brief Read one byte (called from CPU / read clock domain).
 *
 * @return true on success (byte placed in *out), false if FIFO is empty.
 */
bool async_fifo_read(AsyncFIFO *f, uint8_t *out)
{
    /* Synchronize the write pointer into the read domain */
    f->wr_ptr_gray_sync = dff2_sync_tick(&f->wr_sync, f->wr_ptr_gray);

    /* FIFO empty: read and write Gray pointers are equal */
    if (f->rd_ptr_gray == f->wr_ptr_gray_sync) {
        return false; /* FIFO empty */
    }

    *out = f->buf[f->rd_ptr_bin & FIFO_MASK];

    uint8_t rd_next_bin  = (f->rd_ptr_bin + 1u) & PTR_MASK;
    f->rd_ptr_bin  = rd_next_bin;
    f->rd_ptr_gray = bin_to_gray(rd_next_bin);

    return true;
}
```

---

### RTOS-Aware CDC Buffer

When running on an RTOS (e.g., FreeRTOS), the hardware typically provides a UART FIFO with an interrupt, but the buffer management must still respect the clock-domain boundary. The pattern below uses **memory barriers** to enforce ordering on ARM Cortex-M:

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ARM Cortex-M memory barrier intrinsics */
#if defined(__ARM_ARCH)
  #define MEM_BARRIER()  __asm volatile ("dmb sy" ::: "memory")
  #define DATA_SYNC()    __asm volatile ("dsb sy" ::: "memory")
#else
  /* Fallback for simulation / non-ARM targets */
  #define MEM_BARRIER()  __asm volatile ("" ::: "memory")
  #define DATA_SYNC()    __asm volatile ("" ::: "memory")
#endif

#define CDC_BUF_SIZE  256u
#define CDC_BUF_MASK  (CDC_BUF_SIZE - 1u)

/**
 * @brief Power-of-2 ring buffer for UART-to-system CDC.
 *
 * head: written only by the producer (UART ISR / write-clock domain).
 * tail: written only by the consumer (task / read-clock domain).
 *
 * The head and tail indices are declared volatile so the compiler never
 * caches them in a register across the memory barrier.
 */
typedef struct {
    uint8_t          buf[CDC_BUF_SIZE];
    volatile uint32_t head;   /**< Next write position (producer)   */
    volatile uint32_t tail;   /**< Next read position (consumer)    */
} CdcRingBuf;

static CdcRingBuf g_uart_cdc_buf;

/**
 * @brief Push a byte from the UART ISR (producer side).
 *
 * Call from the UART RX interrupt handler. The data is written to the
 * buffer BEFORE the head pointer is updated—this ordering is enforced
 * by the memory barrier so the consumer never sees a stale byte.
 */
bool cdc_buf_push(uint8_t byte)
{
    uint32_t next_head = (g_uart_cdc_buf.head + 1u) & CDC_BUF_MASK;

    /* Load tail once; it may be updated concurrently by consumer */
    if (next_head == g_uart_cdc_buf.tail) {
        return false; /* Overflow */
    }

    g_uart_cdc_buf.buf[g_uart_cdc_buf.head] = byte;

    /* Ensure byte is written to memory before the head pointer moves */
    MEM_BARRIER();
    g_uart_cdc_buf.head = next_head;

    return true;
}

/**
 * @brief Pop a byte in the consumer task (consumer side).
 *
 * The tail pointer is loaded AFTER a barrier, ensuring the consumer
 * reads the actual byte written by the producer and not a cached copy.
 */
bool cdc_buf_pop(uint8_t *out)
{
    /* Read head with barrier to see producer's latest write */
    MEM_BARRIER();
    uint32_t head = g_uart_cdc_buf.head;
    uint32_t tail = g_uart_cdc_buf.tail;

    if (head == tail) {
        return false; /* Empty */
    }

    *out = g_uart_cdc_buf.buf[tail];

    /* Ensure byte is read before advancing tail */
    MEM_BARRIER();
    g_uart_cdc_buf.tail = (tail + 1u) & CDC_BUF_MASK;

    return true;
}

/**
 * @brief Return the number of bytes available to read.
 */
uint32_t cdc_buf_available(void)
{
    MEM_BARRIER();
    return (g_uart_cdc_buf.head - g_uart_cdc_buf.tail) & CDC_BUF_MASK;
}
```

---

## Implementation in Rust

Rust's type system and ownership model make CDC patterns more expressive and safer. The `core::sync::atomic` module provides the necessary ordering primitives without requiring `unsafe` in most cases.

### Atomic-Based Synchronizer

```rust
use core::sync::atomic::{AtomicU8, Ordering};

/// Double flip-flop synchronizer for a single control bit.
///
/// In software this models the hardware synchronizer and ensures
/// the compiler emits properly ordered loads/stores. On bare-metal
/// the AtomicU8 prevents reordering across clock-domain boundaries.
pub struct DffSync {
    ff1: AtomicU8,
    ff2: AtomicU8,
}

impl DffSync {
    pub const fn new() -> Self {
        Self {
            ff1: AtomicU8::new(0),
            ff2: AtomicU8::new(0),
        }
    }

    /// Drive a new value into the synchronizer (source domain).
    ///
    /// Use `Release` ordering so prior writes in the source domain
    /// are visible before the flag value propagates.
    pub fn drive(&self, value: u8) {
        self.ff1.store(value & 1, Ordering::Release);
    }

    /// Advance synchronizer by one destination-clock tick.
    ///
    /// Returns the synchronized (resolved) output.
    /// Use `Acquire` ordering so subsequent reads in the destination
    /// domain observe all writes that preceded `drive`.
    pub fn tick(&self) -> u8 {
        let stage1 = self.ff1.load(Ordering::Acquire);
        self.ff2.store(stage1, Ordering::Relaxed);
        self.ff2.load(Ordering::Relaxed)
    }
}
```

---

### Lock-Free CDC FIFO

The following implements a production-quality, `no_std`-compatible, lock-free FIFO suitable for UART → system-bus CDC on embedded targets. It uses `AtomicUsize` for the head/tail pointers and carefully chosen `Ordering` values.

```rust
#![no_std]

use core::cell::UnsafeCell;
use core::mem::MaybeUninit;
use core::sync::atomic::{AtomicUsize, Ordering};

/// Power-of-2 asynchronous FIFO for clock domain crossing.
///
/// - `N` must be a power of two.
/// - The producer (UART ISR) calls [`push`].
/// - The consumer (system task) calls [`pop`].
///
/// Safety: single-producer, single-consumer (SPSC) only.
/// Shared between two clock domains via atomic head/tail pointers.
pub struct CdcFifo<const N: usize> {
    buf:  UnsafeCell<[MaybeUninit<u8>; N]>,
    head: AtomicUsize,   // written by producer only
    tail: AtomicUsize,   // written by consumer only
}

// SAFETY: SPSC contract: only one producer, one consumer, different cores/domains.
unsafe impl<const N: usize> Sync for CdcFifo<N> {}

impl<const N: usize> CdcFifo<N> {
    const MASK: usize = N - 1;

    /// Create a new, empty FIFO.
    pub const fn new() -> Self {
        assert!(N.is_power_of_two(), "CdcFifo size must be a power of two");
        Self {
            buf:  UnsafeCell::new([MaybeUninit::uninit(); N]),
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    /// Push one byte (producer / UART ISR side).
    ///
    /// Returns `Ok(())` on success, `Err(byte)` if the FIFO is full.
    ///
    /// # Ordering
    /// - `Relaxed` load of tail: the consumer only moves tail forward;
    ///   we only need to observe *some* consistent snapshot.
    /// - `Release` store of head: ensures the written byte is visible
    ///   to the consumer before the updated head pointer is.
    pub fn push(&self, byte: u8) -> Result<(), u8> {
        let head = self.head.load(Ordering::Relaxed);
        let tail = self.tail.load(Ordering::Acquire); // Acquire: see consumer's writes

        let next_head = (head + 1) & Self::MASK;
        if next_head == tail {
            return Err(byte); // FIFO full
        }

        // SAFETY: head is only written by this producer; index is within bounds.
        unsafe {
            (*self.buf.get())[head].write(byte);
        }

        // Release: byte must be written before head is updated.
        self.head.store(next_head, Ordering::Release);
        Ok(())
    }

    /// Pop one byte (consumer / system-domain side).
    ///
    /// Returns `Some(byte)` if data is available, `None` if empty.
    ///
    /// # Ordering
    /// - `Acquire` load of head: synchronizes with the producer's
    ///   `Release` store, ensuring the written byte is visible here.
    /// - `Release` store of tail: ensures the read is complete before
    ///   we signal the slot is free to the producer.
    pub fn pop(&self) -> Option<u8> {
        let tail = self.tail.load(Ordering::Relaxed);
        let head = self.head.load(Ordering::Acquire); // Acquire: see producer's writes

        if head == tail {
            return None; // FIFO empty
        }

        // SAFETY: tail is only written by this consumer; slot is initialized.
        let byte = unsafe { (*self.buf.get())[tail].assume_init() };

        // Release: byte must be read before tail advances.
        self.tail.store((tail + 1) & Self::MASK, Ordering::Release);
        Some(byte)
    }

    /// Returns the number of bytes currently in the FIFO.
    ///
    /// This is a snapshot; may change immediately after return.
    pub fn len(&self) -> usize {
        let head = self.head.load(Ordering::Acquire);
        let tail = self.tail.load(Ordering::Acquire);
        (head.wrapping_sub(tail)) & Self::MASK
    }

    /// Returns `true` if the FIFO contains no bytes.
    pub fn is_empty(&self) -> bool {
        self.head.load(Ordering::Acquire) == self.tail.load(Ordering::Acquire)
    }
}

// -----------------------------------------------------------------------
// Example usage: UART ISR → system task via CDC FIFO
// -----------------------------------------------------------------------

/// Global CDC FIFO: 256-byte capacity, zero-cost at link time.
static UART_CDC_FIFO: CdcFifo<256> = CdcFifo::new();

/// Simulated UART RX interrupt handler (producer domain).
///
/// On real hardware this would be registered as the UART IRQ handler
/// via the HAL's interrupt registration mechanism.
pub fn uart_rx_isr(received_byte: u8) {
    if UART_CDC_FIFO.push(received_byte).is_err() {
        // FIFO overflow: increment error counter, set error flag, etc.
        // Never block in an ISR.
    }
}

/// Simulated system-task UART reader (consumer domain).
///
/// Called periodically from a task or main loop.
pub fn system_task_read_uart(output: &mut [u8]) -> usize {
    let mut count = 0;
    while count < output.len() {
        match UART_CDC_FIFO.pop() {
            Some(b) => {
                output[count] = b;
                count += 1;
            }
            None => break,
        }
    }
    count
}

// -----------------------------------------------------------------------
// Gray code utilities for pointer synchronization
// -----------------------------------------------------------------------

/// Encode a binary counter value as Gray code.
///
/// Property: only one bit changes between consecutive values,
/// making pointer synchronization across clock domains safe.
#[inline]
pub fn bin_to_gray(b: usize) -> usize {
    b ^ (b >> 1)
}

/// Decode a Gray code value back to binary.
#[inline]
pub fn gray_to_bin(mut g: usize) -> usize {
    let mut mask = g >> 1;
    while mask != 0 {
        g ^= mask;
        mask >>= 1;
    }
    g
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gray_roundtrip() {
        for i in 0..256usize {
            assert_eq!(gray_to_bin(bin_to_gray(i)), i,
                "Gray roundtrip failed for {}", i);
        }
    }

    #[test]
    fn test_gray_one_bit_change() {
        for i in 0..255usize {
            let diff = bin_to_gray(i) ^ bin_to_gray(i + 1);
            // Exactly one bit must differ between consecutive Gray codes
            assert!(diff.is_power_of_two(),
                "More than one bit changed at transition {}->{}", i, i+1);
        }
    }

    #[test]
    fn test_fifo_push_pop() {
        let fifo: CdcFifo<8> = CdcFifo::new();
        assert!(fifo.is_empty());
        fifo.push(0xABu8).unwrap();
        fifo.push(0xCDu8).unwrap();
        assert_eq!(fifo.len(), 2);
        assert_eq!(fifo.pop(), Some(0xABu8));
        assert_eq!(fifo.pop(), Some(0xCDu8));
        assert_eq!(fifo.pop(), None);
    }

    #[test]
    fn test_fifo_overflow() {
        let fifo: CdcFifo<4> = CdcFifo::new();
        // Capacity is N-1 = 3 slots (one slot always kept empty to
        // distinguish full from empty)
        assert!(fifo.push(1).is_ok());
        assert!(fifo.push(2).is_ok());
        assert!(fifo.push(3).is_ok());
        assert!(fifo.push(4).is_err(), "Should be full");
    }
}
```

---

## Measuring and Validating CDC Safety

### MTBF Estimation in C

```c
#include <math.h>
#include <stdio.h>

/**
 * @brief Estimate MTBF for a double flip-flop synchronizer.
 *
 * @param f_clk_hz   Destination clock frequency in Hz
 * @param f_data_hz  Data transition rate in Hz
 * @param tau_s      Flip-flop technology constant (seconds); ~0.1 ns typical
 * @param t_res_s    Available resolution time (period - setup - hold), seconds
 * @return           MTBF in seconds
 */
double estimate_mtbf(double f_clk_hz, double f_data_hz,
                     double tau_s,    double t_res_s)
{
    double t_w = 1.0 / f_clk_hz;          /* One clock period         */
    double numerator   = exp(t_res_s / tau_s);
    double denominator = f_clk_hz * f_data_hz * t_w;
    return numerator / denominator;
}

int main(void)
{
    /* 50 MHz system clock, 1.8432 MHz baud clock, typical Kintex-7 DFF */
    double mtbf = estimate_mtbf(
        50e6,    /* f_clk: 50 MHz      */
        1.8432e6,/* f_data: baud clock */
        0.08e-9, /* tau: 80 ps         */
        15e-9    /* t_res: 15 ns       */
    );

    printf("Synchronizer MTBF: %.2e seconds (%.2e years)\n",
           mtbf, mtbf / (365.25 * 24 * 3600));
    return 0;
}
```

### CDC Checklist for UART Designs

| Item | Requirement |
|---|---|
| Single-bit flags (RX_READY, etc.) | Double flip-flop synchronizer |
| 8-bit received byte | Asynchronous FIFO with Gray-coded pointers |
| Baud rate divisor update | Quiesce transfer, then update, then re-enable |
| Status register (framing/parity error) | Synchronize bits individually before CPU read |
| DMA request signal | Edge detection + handshake synchronizer |
| FIFO almost-full threshold | Set conservatively to account for CDC latency |

---

## Summary

Clock Domain Crossing is an unavoidable concern in any UART system where the serial interface and the host system run on independent clocks. The fundamental failure mode is **metastability**: a flip-flop entering an indeterminate state when it samples a signal during its transition window.

The primary mitigation strategies, ranked from simplest to most complex, are:

1. **Double flip-flop synchronizer** — for single control bits. Simple, low area, two-cycle latency. Must not be used for multi-bit data.

2. **Asynchronous FIFO with Gray-coded pointers** — the correct solution for byte-wide UART data. The Gray-code property (one-bit transition per increment) allows FIFO pointers to cross clock domains safely via double-FF synchronizers.

3. **Four-phase handshake** — for control signals where latency is acceptable and the transfer rate is low. Robust but slow.

In **C/C++**, CDC is expressed through volatile variables, explicit memory barriers (`dmb`, `dsb` on ARM), and careful ordering of pointer/data updates in ring buffers. Correct ISR-to-task data handoff requires that data is written *before* the write pointer is updated, and the read pointer is updated only *after* data is consumed.

In **Rust**, the `core::sync::atomic` module provides the same guarantees with a higher-level API. Using `Release` stores for pointer updates and `Acquire` loads for pointer reads maps directly onto the hardware memory ordering required for sound CDC. Rust's type system enforces SPSC ownership at compile time, eliminating an entire class of concurrency bugs.

Regardless of language, the overriding rule is: **never sample multi-bit asynchronous data directly—always use an asynchronous FIFO or a properly ordered handshake**.

---

*Document: 09_Clock_Domain_Crossing.md | UART Deep-Dive Series*