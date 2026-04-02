# 86. Zero-Copy I2C Transfers

**Concept coverage** — the document explains why copies accumulate in naive I2C stacks (cache coherency, alignment, lifetime uncertainty), defines the difference between true zero-copy and reduced-copy, and maps out the memory architecture options (static DMA buffers, IOMMU/SMMU scatter-gather, shared-memory rings).

**C/C++ examples:**
- **Example 1** — Bare-metal STM32 DMA TX driver wiring the app buffer directly to the I2C DR via DMA1 Stream 6, with ownership callback
- **Example 2** — Scatter-gather vectored read (register-addressed combined transfer) with two DMA descriptors pointing to separate app buffers — no concatenation copy
- **Example 3** — C++17 RAII `ZeroCopyBuffer` class with `loan_for_tx()` / `reclaim_after_rx()` state machine enforcing the ownership contract via `assert()`

**Rust examples:**
- **Example 4** — `DmaBuffer<N>` + `I2cDmaTransfer<N>` move semantics: buffer ownership transfers into the transfer struct, making concurrent access a compile error
- **Example 5** — `embedded-hal-async` trait with `async fn write_read()` using stack-allocated buffers across `.await` points — the async state machine keeps buffers live, DMA touches them in-place
- **Example 6** — Minimal demonstration explicitly showing the C "use after loan" bug is a compile-time error in Rust

**Linux kernel pattern**, a benchmark table (showing ~36× CPU cycle reduction), and a pitfalls section covering cache coherency, alignment, buffer lifetime, repeated-START requirements, SMBus PEC, and NACK recovery.

## Eliminating Data Copying in I2C Stacks for Performance Optimization

---

## Table of Contents

1. [Introduction](#introduction)
2. [The I2C Protocol — A Brief Refresher](#the-i2c-protocol--a-brief-refresher)
3. [The Copying Problem in Traditional I2C Stacks](#the-copying-problem-in-traditional-i2c-stacks)
4. [What Zero-Copy Actually Means](#what-zero-copy-actually-means)
5. [Memory Architecture for Zero-Copy I2C](#memory-architecture-for-zero-copy-i2c)
6. [DMA-Based Zero-Copy Transfers](#dma-based-zero-copy-transfers)
7. [Scatter-Gather (Vectored) I/O for I2C](#scatter-gather-vectored-io-for-i2c)
8. [Buffer Ownership and Lifetime Management](#buffer-ownership-and-lifetime-management)
9. [C/C++ Implementation Examples](#cc-implementation-examples)
10. [Rust Implementation Examples](#rust-implementation-examples)
11. [Linux Kernel I2C Zero-Copy Patterns](#linux-kernel-i2c-zero-copy-patterns)
12. [Benchmarking and Profiling](#benchmarking-and-profiling)
13. [Pitfalls and Edge Cases](#pitfalls-and-edge-cases)
14. [Summary](#summary)

---

## Introduction

The Inter-Integrated Circuit (I2C) protocol is ubiquitous in embedded systems, connecting microcontrollers to sensors, EEPROMs, display drivers, ADCs, and countless other peripherals. While I2C's bandwidth (100 kHz, 400 kHz, 1 MHz, or 3.4 MHz in Fast-Mode Plus) is modest compared to SPI or USB, the *software overhead* introduced by unnecessary data copies in the I2C driver stack can become the true bottleneck — particularly in high-frequency polling loops, bulk EEPROM operations, or latency-sensitive sensor fusion pipelines.

**Zero-Copy I2C** is a design paradigm that eliminates intermediate buffer copies between the application layer and the hardware peripheral. Instead of staging data through multiple temporary buffers, the hardware reads from (or writes to) application-owned memory directly — typically via DMA or carefully managed pointer passing through the driver stack.

### Why It Matters

Consider a system reading 128 bytes from an I2C sensor at 400 kHz (Fast Mode):

- Wire transfer time: ~2.5 ms (start + address + 128 bytes + stop)
- Each `memcpy` of 128 bytes on a Cortex-M4 @ 168 MHz: ~0.5 µs
- With 3 copies in a naive stack: ~1.5 µs overhead

At first glance, 1.5 µs seems trivial. But in a 1 kHz sensor loop running 24/7, this adds up; and on a Linux embedded board doing I2C EEPROM streaming or camera I2C configuration bursts (hundreds of register writes), the copies accumulate in cache pressure, increased interrupt latency, and wasted CPU cycles.

---

## The I2C Protocol — A Brief Refresher

I2C uses two wires — **SDA** (data) and **SCL** (clock) — in a multi-master, multi-slave topology. A transaction consists of:

```
START → [7-bit address + R/W bit] → ACK → [data bytes + ACK/NACK] → STOP
```

A **message** in software terms is a single address + direction + data blob. A **transfer** may chain multiple messages (called a *combined transfer*) using a *repeated START* instead of STOP between them, which is essential for register-addressed reads:

```
START → addr+W → reg_addr → REPEATED-START → addr+R → [data...] → STOP
```

This two-message combined transfer is the most common I2C pattern. Any zero-copy design must handle it correctly.

---

## The Copying Problem in Traditional I2C Stacks

A naive I2C stack performs copies at several layers:

```
Application Buffer
      │  memcpy (1) — app fills a "request" struct
      ▼
Driver Request Buffer
      │  memcpy (2) — driver stages into a DMA-safe region
      ▼
DMA-Coherent Buffer
      │  DMA engine transfers over I2C wire
      ▼
Hardware FIFO / Shift Register
```

On the read path, the copies are reversed, doubling the overhead. The causes are:

1. **Cache coherency**: On systems with a data cache (ARM Cortex-A, RISC-V with caches), DMA cannot safely access cached memory without cache maintenance operations (clean + invalidate). Drivers often copy into a statically allocated non-cached DMA buffer to avoid this complexity.
2. **Alignment constraints**: Some DMA engines require word-aligned or cache-line-aligned buffers. Copying lets the driver guarantee alignment.
3. **Lifetime uncertainty**: The driver cannot trust that the caller's buffer remains valid for the duration of an asynchronous DMA transfer, so it copies to a driver-owned buffer with known lifetime.
4. **API convenience**: Many RTOS and Linux I2C APIs accept `const uint8_t *` data, making it easy to copy implicitly rather than design for zero-copy.

---

## What Zero-Copy Actually Means

Zero-copy means **the hardware accesses application memory directly**, without intermediate copies. This requires:

1. **DMA-safe application buffers**: The application allocates memory in a region the DMA engine can access (physically contiguous, properly mapped, cache-coherent or explicitly flushed).
2. **Pointer-passing APIs**: The driver API accepts buffer pointers/descriptors rather than values to copy.
3. **Ownership transfer semantics**: The buffer must not be touched by the application while DMA is in flight. The API must clearly define when ownership returns.
4. **Cache maintenance (where needed)**: Before TX: cache flush (clean). After RX: cache invalidate. Modern SoCs with hardware cache coherency (CCI, CCN, SMMU) may handle this automatically.

### Zero-Copy vs. Reduced-Copy

True zero-copy means zero copies end-to-end. In practice, a *reduced-copy* approach (one copy instead of three) is often the achievable and pragmatic goal, especially on Cortex-M without MMU.

---

## Memory Architecture for Zero-Copy I2C

### Option A: Static DMA Buffers with Direct Application Use

The simplest approach: allocate one (or a pool of) DMA-safe buffers at startup. Applications fill them in place and pass them directly to the driver.

```
Application writes into DMA buffer → Driver programs DMA controller → Wire transfer
```

**Trade-off**: Applications must be aware of DMA buffer locations. No copy, but reduced abstraction.

### Option B: Scatter-Gather with IOMMU/SMMU

For Linux on Cortex-A SoCs: the IOMMU maps arbitrary virtual addresses to contiguous I/O addresses. The DMA engine sees contiguous I/O space even if the application buffer is physically scattered. The driver programs an IOVA (I/O Virtual Address) — no copy needed for physically discontiguous buffers.

### Option C: Shared Memory Ring Buffers

High-throughput multi-master I2C scenarios (e.g., a Linux host communicating with a co-processor via I2C in a virtualized setup) use shared memory rings, where producer and consumer share the same physical memory, eliminating all copies.

---

## DMA-Based Zero-Copy Transfers

### Synchronous Zero-Copy (Blocking, with Cache Flush)

The simplest hardware model: the application owns an aligned buffer, flushes the cache, programs the DMA, and waits.

```
App Buffer (aligned, in uncached region or explicitly flushed)
    │
    └──► DMA Controller ──► I2C TX FIFO ──► Wire
```

### Asynchronous Zero-Copy (Non-blocking, Ownership Transfer)

The application hands the buffer pointer to the driver. The driver programs DMA and returns immediately. A callback or semaphore signals completion and returns buffer ownership.

```
App hands ptr ──► Driver programs DMA ──► Returns immediately
                          │
                    [DMA running, buffer locked]
                          │
                    DMA complete interrupt
                          │
                    Driver signals app ──► App reclaims buffer
```

### Double-Buffering for Continuous Streaming

To keep I2C hardware busy without stalls, double buffering alternates between two DMA buffers: while DMA is reading from buffer A, the CPU fills buffer B.

```
Buffer A: [DMA in progress ────────]  [idle]  [DMA in progress]
Buffer B: [idle] [CPU filling ─────]  [DMA in progress ───────]
```

---

## Scatter-Gather (Vectored) I/O for I2C

Many I2C transactions naturally scatter across multiple buffers — for example, a register address byte (1 byte) followed by a payload (N bytes) living in different allocations. Traditional stacks concatenate these into a single temporary buffer before DMA. Scatter-gather DMA eliminates this copy.

A scatter-gather descriptor list describes each segment:

```c
struct i2c_sg_entry {
    uint32_t phys_addr;   // Physical (or IOVA) address of this segment
    uint16_t length;      // Byte count for this segment
    uint16_t flags;       // LAST, LINK, etc.
};
```

The DMA engine walks the list, transferring each segment in sequence. The I2C hardware never needs a contiguous buffer.

---

## Buffer Ownership and Lifetime Management

Ownership discipline is the hardest part of zero-copy design. The rules:

| Phase | Buffer Owner | Allowed Operations |
|---|---|---|
| Before `i2c_transfer_async()` | Application | Read, write freely |
| After `i2c_transfer_async()` | Driver/DMA | **MUST NOT touch** |
| After completion callback | Application | Read results, reuse |

Violations cause silent data corruption — among the hardest bugs to diagnose. Rust's ownership and borrowing system enforces this at compile time (see Rust section). In C/C++, it must be enforced by convention and careful API design.

---

## C/C++ Implementation Examples

### Example 1: Bare-Metal Cortex-M4 — DMA-Based Zero-Copy I2C TX

This example targets STM32 (HAL-free) style register programming, showing how to wire an application buffer directly to the I2C DMA channel without an intermediate copy.

```c
/*
 * zero_copy_i2c_stm32.c
 * Zero-copy I2C write using DMA on STM32F4 (bare-metal, no HAL)
 *
 * Assumes:
 *   - I2C1 peripheral, DMA1 Stream 6 (I2C1_TX)
 *   - Application buffer is in SRAM (DMA-accessible, no cache on M4 without cache extensions)
 *   - 7-bit slave address
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- Register base addresses (STM32F4 example) ---- */
#define I2C1_BASE       0x40005400UL
#define DMA1_BASE       0x40026000UL
#define DMA1_S6_BASE    (DMA1_BASE + 0x10 + 6 * 0x18)

/* ---- I2C registers ---- */
typedef struct {
    volatile uint32_t CR1, CR2, OAR1, OAR2, DR, SR1, SR2, CCR, TRISE, FLTR;
} I2C_TypeDef;

/* ---- DMA Stream registers ---- */
typedef struct {
    volatile uint32_t CR, NDTR;
    volatile uint32_t PAR;   /* Peripheral address */
    volatile uint32_t M0AR;  /* Memory 0 address   */
    volatile uint32_t M1AR;  /* Memory 1 address   */
    volatile uint32_t FCR;
} DMA_Stream_TypeDef;

#define I2C1    ((I2C_TypeDef *)    I2C1_BASE)
#define DMA1_S6 ((DMA_Stream_TypeDef *) DMA1_S6_BASE)

/* ---- Bit definitions ---- */
#define I2C_CR1_PE      (1U << 0)
#define I2C_CR1_START   (1U << 8)
#define I2C_CR1_STOP    (1U << 9)
#define I2C_CR2_DMAEN   (1U << 11)
#define I2C_CR2_LAST    (1U << 12)
#define I2C_SR1_SB      (1U << 0)   /* Start bit generated */
#define I2C_SR1_ADDR    (1U << 1)   /* Address sent */
#define I2C_SR1_BTF     (1U << 2)   /* Byte transfer finished */
#define I2C_SR1_TXE     (1U << 7)   /* TX register empty */

#define DMA_CR_EN       (1U << 0)
#define DMA_CR_TCIE     (1U << 4)   /* Transfer complete interrupt enable */
#define DMA_CR_DIR_M2P  (1U << 6)   /* Memory to peripheral */
#define DMA_CR_MINC     (1U << 10)  /* Memory increment mode */
#define DMA_CR_CHSEL_1  (1U << 25)  /* Channel 1 for I2C1_TX on DMA1 S6 */

/* ---- Transfer state ---- */
typedef enum {
    I2C_IDLE,
    I2C_BUSY,
    I2C_DONE,
    I2C_ERROR
} i2c_state_t;

static volatile i2c_state_t g_state = I2C_IDLE;

/* Callback type: called from DMA TC interrupt with user context */
typedef void (*i2c_completion_cb)(bool success, void *ctx);
static i2c_completion_cb g_cb  = NULL;
static void             *g_ctx = NULL;

/*
 * i2c_dma_write_async() — Zero-copy asynchronous I2C write.
 *
 * OWNERSHIP RULE: The caller MUST NOT modify `data` until the callback fires.
 * `data` must be DMA-accessible (SRAM on STM32F4 without cache).
 *
 * Returns: true if DMA was successfully programmed, false if bus busy.
 */
bool i2c_dma_write_async(uint8_t slave_addr_7bit,
                         const uint8_t *data,   /* application buffer — zero-copy */
                         uint16_t       len,
                         i2c_completion_cb cb,
                         void          *ctx)
{
    if (g_state != I2C_IDLE) return false;
    if (len == 0 || data == NULL) return false;

    g_cb    = cb;
    g_ctx   = ctx;
    g_state = I2C_BUSY;

    /* --- Program DMA1 Stream 6 to transfer from app buffer to I2C1->DR --- */
    DMA1_S6->CR   = 0;                              /* disable stream first */
    DMA1_S6->NDTR = len;                            /* number of bytes */
    DMA1_S6->PAR  = (uint32_t)&I2C1->DR;           /* peripheral: I2C data register */
    DMA1_S6->M0AR = (uint32_t)data;                 /* *** APP BUFFER — NO COPY *** */
    DMA1_S6->CR   = DMA_CR_CHSEL_1 |               /* channel 1 = I2C1_TX */
                    DMA_CR_DIR_M2P  |               /* memory → peripheral */
                    DMA_CR_MINC     |               /* increment memory address */
                    DMA_CR_TCIE;                    /* interrupt on complete */

    /* --- Enable I2C DMA request generation --- */
    I2C1->CR2 |= I2C_CR2_DMAEN | I2C_CR2_LAST;

    /* --- Generate START condition --- */
    I2C1->CR1 |= I2C_CR1_START;

    /* Poll for SB (start bit), then send address.
     * In a real driver this would be interrupt-driven too. */
    while (!(I2C1->SR1 & I2C_SR1_SB)) {}
    I2C1->DR = (slave_addr_7bit << 1) & 0xFE;      /* write direction: bit0 = 0 */
    while (!(I2C1->SR1 & I2C_SR1_ADDR)) {}
    (void)I2C1->SR2;                                /* clear ADDR by reading SR2 */

    /* --- Enable DMA stream — hardware now drives data from app buffer --- */
    DMA1_S6->CR |= DMA_CR_EN;

    return true;  /* caller must not touch `data` until callback */
}

/*
 * DMA1_Stream6_IRQHandler — fires when DMA transfer completes.
 * Issues I2C STOP and calls application callback.
 */
void DMA1_Stream6_IRQHandler(void)
{
    /* Clear transfer-complete flag (HISR bit 21 for stream 6) */
    /* DMA1->HIFCR = (1U << 21); */  /* abbreviated for clarity */

    /* Wait for BTF then issue STOP */
    while (!(I2C1->SR1 & I2C_SR1_BTF)) {}
    I2C1->CR1 |= I2C_CR1_STOP;

    /* Disable DMA request */
    I2C1->CR2 &= ~(I2C_CR2_DMAEN | I2C_CR2_LAST);
    DMA1_S6->CR &= ~DMA_CR_EN;

    g_state = I2C_IDLE;

    /* Notify application — it may now reuse the buffer */
    if (g_cb) {
        g_cb(true, g_ctx);
    }
}


/* ============================================================
 * Usage example
 * ============================================================ */

/* Application buffer — declared by caller, lives in accessible SRAM.
 * On STM32F4 (no D-cache), no flush needed.
 * On Cortex-M7 with D-cache, must call SCB_CleanDCache_by_Addr() first. */

/* Aligned to 4 bytes (good practice even where not strictly required) */
static uint8_t __attribute__((aligned(4))) sensor_config[] = {
    0x1A,  /* register address */
    0x02,  /* value: enable sensor, 200 Hz ODR */
    0x00,  /* padding register */
    0x80   /* INT_CTRL: active-high, push-pull */
};

static volatile bool transfer_done = false;

static void on_write_complete(bool success, void *ctx)
{
    (void)ctx;
    transfer_done = success;
    /* sensor_config is safe to use again from here */
}

void example_zero_copy_write(void)
{
    transfer_done = false;

    bool ok = i2c_dma_write_async(
        0x6A,              /* slave address (e.g., LSM6DSO IMU) */
        sensor_config,     /* app buffer — handed directly to DMA, no copy */
        sizeof(sensor_config),
        on_write_complete,
        NULL
    );

    if (!ok) {
        /* Handle busy */
        return;
    }

    /* Do other work here — or wait: */
    while (!transfer_done) { /* __WFI(); */ }
}
```

---

### Example 2: Zero-Copy Combined Read (Write-then-Read via Repeated START)

The register-addressed read is the most common I2C pattern. Zero-copy here means neither the register address byte nor the receive buffer is copied through an intermediate staging buffer.

```c
/*
 * zero_copy_i2c_read.c
 * Zero-copy register-addressed I2C read using two chained DMA descriptors.
 *
 * Transaction: [START | addr+W | reg | RSTART | addr+R | data... | STOP]
 *
 * This implementation uses a minimal scatter-gather approach:
 * TX DMA sends the register address from app memory.
 * RX DMA receives into app memory.
 * Neither path involves a memcpy.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* Platform-specific DMA/I2C register types omitted for brevity;
 * see Example 1 for definitions. */

/* ---- Scatter-gather descriptor for a zero-copy I2C segment ---- */
typedef struct {
    const uint8_t  *buf;      /* Pointer into application memory */
    uint16_t        len;      /* Byte count for this segment */
    bool            is_read;  /* true = RX from slave, false = TX to slave */
    bool            no_stop;  /* true = use REPEATED-START after segment */
} i2c_segment_t;

/* ---- Transfer descriptor (vectored, zero-copy) ---- */
typedef struct {
    uint8_t          slave_addr;
    i2c_segment_t   *segments;
    uint8_t          num_segments;
    uint8_t          current_seg;
    i2c_completion_cb cb;
    void             *ctx;
} i2c_xfer_t;

/* ---- Minimal scatter-gather I2C driver ---- */
static i2c_xfer_t g_xfer;
static volatile i2c_state_t g_xfer_state = I2C_IDLE;

/*
 * Start the next segment in a vectored transfer.
 * Called from interrupt context after each segment completes.
 */
static void start_next_segment(void)
{
    if (g_xfer.current_seg >= g_xfer.num_segments) {
        /* All segments done */
        I2C1->CR1 |= I2C_CR1_STOP;
        g_xfer_state = I2C_IDLE;
        if (g_xfer.cb) g_xfer.cb(true, g_xfer.ctx);
        return;
    }

    i2c_segment_t *seg = &g_xfer.segments[g_xfer.current_seg];
    g_xfer.current_seg++;

    if (seg->is_read) {
        /* Program RX DMA — receive directly into application buffer */
        /* DMA1_S0->M0AR = (uint32_t)seg->buf;  (RX stream for I2C1) */
        /* DMA1_S0->NDTR = seg->len; */
        /* DMA1_S0->CR |= DMA_CR_EN; */
        I2C1->CR2 |= I2C_CR2_DMAEN;
        if (!seg->no_stop) I2C1->CR2 |= I2C_CR2_LAST;
        /* Send address+R */
        I2C1->CR1 |= I2C_CR1_START;
        while (!(I2C1->SR1 & I2C_SR1_SB)) {}
        I2C1->DR = (g_xfer.slave_addr << 1) | 0x01;  /* read */
        while (!(I2C1->SR1 & I2C_SR1_ADDR)) {}
        (void)I2C1->SR2;
    } else {
        /* Program TX DMA — send directly from application buffer */
        /* DMA1_S6->M0AR = (uint32_t)seg->buf; */
        /* DMA1_S6->NDTR = seg->len; */
        /* DMA1_S6->CR |= DMA_CR_EN; */
        I2C1->CR2 |= I2C_CR2_DMAEN;
        /* Generate START (or REPEATED START) */
        I2C1->CR1 |= I2C_CR1_START;
        while (!(I2C1->SR1 & I2C_SR1_SB)) {}
        I2C1->DR = (g_xfer.slave_addr << 1) & 0xFE;  /* write */
        while (!(I2C1->SR1 & I2C_SR1_ADDR)) {}
        (void)I2C1->SR2;
    }
}

/*
 * i2c_vectored_transfer_async() — Zero-copy vectored (scatter-gather) I2C transfer.
 *
 * Segments point directly into application memory. No copies are made.
 * The caller MUST NOT access any segment buffer until the callback fires.
 *
 * Example: register-addressed read
 *   seg[0]: TX, {reg_addr_byte}, no_stop=true  → sends register address
 *   seg[1]: RX, {rx_buf, len},   no_stop=false → receives data into rx_buf
 */
bool i2c_vectored_transfer_async(uint8_t           slave_addr,
                                 i2c_segment_t    *segments,
                                 uint8_t           num_segments,
                                 i2c_completion_cb cb,
                                 void             *ctx)
{
    if (g_xfer_state != I2C_IDLE) return false;

    g_xfer.slave_addr    = slave_addr;
    g_xfer.segments      = segments;    /* points to app memory — no copy */
    g_xfer.num_segments  = num_segments;
    g_xfer.current_seg   = 0;
    g_xfer.cb            = cb;
    g_xfer.ctx           = ctx;
    g_xfer_state         = I2C_BUSY;

    start_next_segment();
    return true;
}


/* ============================================================
 * Usage: zero-copy register-addressed read from LSM6DSO IMU
 * ============================================================ */

/* These buffers live in application code. The driver gets pointers only. */
static uint8_t reg_addr_buf[1];                           /* TX: register address */
static uint8_t accel_data[6] __attribute__((aligned(4))); /* RX: 6 bytes of accel XYZ */

static volatile bool read_done = false;

static void on_read_done(bool ok, void *ctx)
{
    (void)ctx;
    read_done = ok;
    /* accel_data is safe to read from here — no copy was ever made */
}

void example_zero_copy_register_read(void)
{
    reg_addr_buf[0] = 0x28;  /* OUTX_L_A register of LSM6DSO */

    /* Two segments: TX reg address, then RX data. Zero copies. */
    i2c_segment_t segs[2] = {
        { .buf = reg_addr_buf, .len = 1, .is_read = false, .no_stop = true  },
        { .buf = accel_data,   .len = 6, .is_read = true,  .no_stop = false }
    };

    read_done = false;
    i2c_vectored_transfer_async(0x6A, segs, 2, on_read_done, NULL);

    while (!read_done) {}

    /* Parse accel_data directly from application buffer — no intermediate copy */
    int16_t ax = (int16_t)((accel_data[1] << 8) | accel_data[0]);
    int16_t ay = (int16_t)((accel_data[3] << 8) | accel_data[2]);
    int16_t az = (int16_t)((accel_data[5] << 8) | accel_data[4]);
    (void)ax; (void)ay; (void)az;
}
```

---

### Example 3: C++ — RAII Buffer Pool for Zero-Copy I2C

This C++ example provides a `ZeroCopyI2CBuffer` RAII wrapper that automatically manages DMA-safe buffer allocation and enforces the ownership protocol via move semantics.

```cpp
/*
 * ZeroCopyI2C.hpp
 * C++17 RAII wrapper for zero-copy I2C buffer management.
 * Enforces ownership transfer semantics at the type level.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <functional>
#include <atomic>
#include <span>       /* C++20 span, or use gsl::span */

/* ---- DMA-safe buffer pool ---- */
/* In real code: allocate from a linker-section in DMA-accessible SRAM.
 * Here we use aligned_alloc as a stand-in for DMA allocator. */
namespace dma {
    constexpr size_t ALIGNMENT = 32;  /* cache line on Cortex-A/A53 */

    inline uint8_t *alloc(size_t size) {
        /* Replace with your platform's DMA allocator:
         *   Linux kernel: dma_alloc_coherent()
         *   FreeRTOS:     pvPortMallocAligned()
         *   Bare metal:   custom SRAM pool */
        return static_cast<uint8_t *>(::operator new[](size,
            static_cast<std::align_val_t>(ALIGNMENT)));
    }

    inline void free(uint8_t *p) {
        ::operator delete[](p, static_cast<std::align_val_t>(ALIGNMENT));
    }

    /* Cache maintenance — stub; implement per platform */
    inline void cache_clean(const void *, size_t)    {}   /* flush before TX */
    inline void cache_invalidate(const void *, size_t) {} /* invalidate after RX */
}

/* ============================================================
 * ZeroCopyBuffer — RAII DMA-safe buffer with ownership tracking.
 *
 * Invariant: a buffer is either OWNED by the application
 *            or LOANED to the DMA engine, never both.
 * ============================================================ */
class ZeroCopyBuffer {
public:
    enum class State { Owned, Loaned, Empty };

    explicit ZeroCopyBuffer(size_t capacity)
        : data_(dma::alloc(capacity)), capacity_(capacity),
          len_(0), state_(State::Owned)
    {}

    /* Non-copyable: copying defeats the purpose */
    ZeroCopyBuffer(const ZeroCopyBuffer &) = delete;
    ZeroCopyBuffer &operator=(const ZeroCopyBuffer &) = delete;

    /* Movable */
    ZeroCopyBuffer(ZeroCopyBuffer &&o) noexcept
        : data_(o.data_), capacity_(o.capacity_), len_(o.len_),
          state_(o.state_.load())
    {
        o.data_ = nullptr;
        o.state_ = State::Empty;
    }

    ~ZeroCopyBuffer() {
        assert(state_ != State::Loaned &&
               "Buffer destroyed while loaned to DMA!");
        dma::free(data_);
    }

    /* Fill buffer from source (only allowed while Owned) */
    void fill(const void *src, size_t n) {
        assert(state_ == State::Owned && n <= capacity_);
        std::memcpy(data_, src, n);
        len_ = n;
    }

    /* Get a span over the data (only while Owned) */
    std::span<uint8_t> as_span() {
        assert(state_ == State::Owned);
        return {data_, len_};
    }

    std::span<const uint8_t> as_const_span() const {
        assert(state_ == State::Owned);
        return {data_, len_};
    }

    uint8_t *raw()       { return data_; }
    size_t   capacity()  const { return capacity_; }
    size_t   length()    const { return len_; }
    void     set_length(size_t n) { assert(n <= capacity_); len_ = n; }

    /* Loan to DMA: flushes cache and marks buffer inaccessible to caller */
    void loan_for_tx() {
        assert(state_ == State::Owned);
        dma::cache_clean(data_, len_);
        state_ = State::Loaned;
    }

    void loan_for_rx() {
        assert(state_ == State::Owned);
        state_ = State::Loaned;
    }

    /* Reclaim from DMA: called from completion callback */
    void reclaim_after_rx() {
        assert(state_ == State::Loaned);
        dma::cache_invalidate(data_, len_);
        state_ = State::Owned;
    }

    void reclaim_after_tx() {
        assert(state_ == State::Loaned);
        state_ = State::Owned;
    }

    State state() const { return state_.load(); }

private:
    uint8_t             *data_;
    size_t               capacity_;
    size_t               len_;
    std::atomic<State>   state_;
};


/* ============================================================
 * ZeroCopyI2CDriver — High-level zero-copy I2C interface.
 * ============================================================ */
class ZeroCopyI2CDriver {
public:
    using CompletionFn = std::function<void(bool success, ZeroCopyBuffer &)>;

    /* Async zero-copy write.
     * `buf` is loaned to the driver; reclaimed in `cb`. */
    bool write_async(uint8_t slave_addr,
                     ZeroCopyBuffer &buf,
                     CompletionFn cb)
    {
        if (busy_.exchange(true)) return false;
        buf.loan_for_tx();
        pending_cb_  = std::move(cb);
        pending_buf_ = &buf;

        /* Program hardware DMA with buf.raw() — no copy.
         * In a real driver: dma_program_tx(slave_addr, buf.raw(), buf.length()); */
        (void)slave_addr;

        return true;
    }

    /* Call from DMA completion ISR or deferred work */
    void on_dma_complete(bool success) {
        if (pending_buf_) {
            pending_buf_->reclaim_after_tx();
            if (pending_cb_) {
                pending_cb_(success, *pending_buf_);
            }
            pending_buf_ = nullptr;
        }
        busy_ = false;
    }

private:
    std::atomic<bool>  busy_{false};
    ZeroCopyBuffer    *pending_buf_{nullptr};
    CompletionFn       pending_cb_;
};


/* ============================================================
 * Usage example
 * ============================================================ */
void example_cpp_zero_copy()
{
    ZeroCopyI2CDriver driver;

    /* Application allocates a DMA-safe buffer */
    ZeroCopyBuffer buf(16);

    /* Prepare payload directly in DMA-safe buffer — no copy needed later */
    uint8_t config_data[] = {0x1A, 0x02, 0x00, 0x80};
    buf.fill(config_data, sizeof(config_data));

    bool launched = driver.write_async(
        0x6A,
        buf,
        [](bool ok, ZeroCopyBuffer &b) {
            /* Called when DMA completes. Buffer is Owned again. */
            if (ok) {
                /* Can now reuse b for next transfer — still no copy needed */
                (void)b;
            }
        }
    );

    /* Attempting to access buf.as_span() here would trigger assert()
     * because state_ == Loaned. In Rust this would be a compile error. */

    if (launched) {
        /* Simulate DMA completion (in real code: triggered by interrupt) */
        driver.on_dma_complete(true);
    }
}
```

---

## Rust Implementation Examples

Rust's ownership, borrowing, and lifetime system makes zero-copy I2C a natural fit: the borrow checker **statically enforces** the constraint that the DMA engine holds exclusive access to the buffer during a transfer. Invalid access patterns that cause silent corruption in C become **compile-time errors** in Rust.

### Example 4: Rust — Zero-Copy I2C with Ownership Transfer

```rust
//! zero_copy_i2c.rs
//! Zero-copy I2C transfer using ownership semantics in Rust.
//!
//! The buffer is moved into the transfer, preventing any concurrent access.
//! It is moved back out in the completion result.
//!
//! Compatible with embedded-hal 1.0 async I2C traits.

#![no_std]
#![allow(dead_code)]

use core::fmt;

/// Error type for I2C operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cError {
    ArbitrationLost,
    Bus,
    Nack,
    Overrun,
    Timeout,
    BusBusy,
}

impl fmt::Display for I2cError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            I2cError::ArbitrationLost => write!(f, "Arbitration lost"),
            I2cError::Bus             => write!(f, "Bus error"),
            I2cError::Nack            => write!(f, "NACK received"),
            I2cError::Overrun         => write!(f, "Overrun"),
            I2cError::Timeout         => write!(f, "Timeout"),
            I2cError::BusBusy         => write!(f, "Bus busy"),
        }
    }
}

/// A DMA-safe owned buffer.
///
/// In embedded contexts, this would typically come from a linker-section
/// allocator placing data in DMA-accessible SRAM (e.g., `#[link_section = ".dma_bss"]`).
///
/// Ownership of this buffer *moves* into a transfer, preventing any
/// simultaneous access from other code — the borrow checker enforces this.
pub struct DmaBuffer<const N: usize> {
    data: [u8; N],
    len:  usize,
}

impl<const N: usize> DmaBuffer<N> {
    pub const fn new() -> Self {
        Self { data: [0u8; N], len: 0 }
    }

    pub fn fill_from(&mut self, src: &[u8]) -> &mut Self {
        assert!(src.len() <= N, "Source exceeds buffer capacity");
        self.data[..src.len()].copy_from_slice(src);
        self.len = src.len();
        self
    }

    pub fn as_slice(&self) -> &[u8]     { &self.data[..self.len] }
    pub fn as_mut_slice(&mut self) -> &mut [u8] { &mut self.data[..self.len] }
    pub fn len(&self) -> usize          { self.len }
    pub fn set_len(&mut self, n: usize) { assert!(n <= N); self.len = n; }
    pub fn capacity(&self) -> usize     { N }

    /// Returns raw pointer for DMA programming.
    /// SAFETY: Caller must ensure DMA access is synchronized with CPU access.
    pub unsafe fn as_ptr(&self) -> *const u8  { self.data.as_ptr() }
    pub unsafe fn as_mut_ptr(&mut self) -> *mut u8 { self.data.as_mut_ptr() }
}

/// Result of a completed zero-copy transfer: buffer returned to caller.
pub struct TransferResult<const N: usize> {
    /// The original buffer — ownership returned after DMA complete.
    pub buffer: DmaBuffer<N>,
    /// Number of bytes transferred.
    pub bytes_transferred: usize,
    /// Error if the transfer failed.
    pub error: Option<I2cError>,
}

/// Represents an in-flight I2C DMA transfer.
///
/// The buffer is OWNED by this struct while the transfer is active.
/// The application cannot access the buffer until `complete()` is called.
///
/// This enforces the zero-copy ownership protocol at compile time:
/// there is no way to alias the buffer while it is owned here.
pub struct I2cDmaTransfer<const N: usize> {
    buffer:  DmaBuffer<N>,
    slave:   u8,
    is_read: bool,
}

impl<const N: usize> I2cDmaTransfer<N> {
    /// Poll for completion. Returns `Some(result)` when done.
    ///
    /// In async contexts, this would be an `async fn` using a waker.
    /// Here we use a synchronous poll for embedded bare-metal.
    pub fn poll(&mut self) -> Option<TransferResult<N>> {
        // In real code: check DMA transfer-complete flag in hardware register.
        // e.g., if DMA1_HISR.TCIF6 is set → transfer done.
        // For demonstration we assume it always completes immediately:
        let done = true; // replace with: unsafe { (*DMA1::ptr()).hisr.read().tcif6().bit() }

        if done {
            // Cache invalidate for RX (platform-specific)
            if self.is_read {
                // unsafe { cortex_m::asm::dsb(); }  // Data Synchronization Barrier
            }
            Some(TransferResult {
                bytes_transferred: self.buffer.len(),
                error: None,
                buffer: self.take_buffer(),
            })
        } else {
            None
        }
    }

    /// Block until transfer completes (for synchronous use).
    pub fn wait(mut self) -> TransferResult<N> {
        loop {
            if let Some(r) = self.poll() {
                return r;
            }
            // In FreeRTOS: taskYIELD(); in bare-metal: spin or __WFI();
        }
    }

    fn take_buffer(&mut self) -> DmaBuffer<N> {
        // We construct a new buffer from the current data.
        // In real code: `mem::replace` or a MaybeUninit field.
        let mut b = DmaBuffer::new();
        b.data[..self.buffer.len].copy_from_slice(&self.buffer.data[..self.buffer.len]);
        b.len = self.buffer.len;
        b
    }
}

/// Zero-copy I2C driver.
pub struct ZeroCopyI2c {
    slave_addr: u8,
    busy: bool,
}

impl ZeroCopyI2c {
    pub fn new(slave_addr: u8) -> Self {
        Self { slave_addr, busy: false }
    }

    /// Start an async zero-copy write.
    ///
    /// The buffer is MOVED into the returned `I2cDmaTransfer`.
    /// The caller loses access to it until `transfer.wait()` returns it.
    ///
    /// This ownership transfer means the compiler PREVENTS:
    ///   - Reading the buffer while DMA is writing to the wire
    ///   - Modifying the buffer mid-transfer
    ///   - Dropping the buffer before DMA completes
    ///
    /// All of these are compile-time errors, not runtime bugs.
    pub fn write_async<const N: usize>(
        &mut self,
        buffer: DmaBuffer<N>,   // MOVE: buffer ownership transfers here
    ) -> Result<I2cDmaTransfer<N>, (DmaBuffer<N>, I2cError)> {
        if self.busy {
            return Err((buffer, I2cError::BusBusy));
        }
        self.busy = true;

        // Program DMA: point hardware at buffer.as_ptr() — no copy.
        // unsafe {
        //     let dma = &*DMA1::ptr();
        //     dma.st[6].m0ar.write(|w| w.bits(buffer.as_ptr() as u32));
        //     dma.st[6].ndtr.write(|w| w.ndt().bits(buffer.len() as u16));
        //     dma.st[6].cr.modify(|_, w| w.en().enabled());
        // }
        // Issue I2C START + address...

        // Buffer is now owned by the transfer — application cannot access it.
        Ok(I2cDmaTransfer {
            buffer,
            slave: self.slave_addr,
            is_read: false,
        })
    }

    /// Start an async zero-copy read.
    ///
    /// `buffer` is moved into the transfer; on completion it will contain
    /// the received bytes from the slave with no intermediate copies.
    pub fn read_async<const N: usize>(
        &mut self,
        mut buffer: DmaBuffer<N>,
        len: usize,
    ) -> Result<I2cDmaTransfer<N>, (DmaBuffer<N>, I2cError)> {
        if self.busy {
            return Err((buffer, I2cError::BusBusy));
        }
        self.busy = true;
        buffer.set_len(len);

        // Program RX DMA to write into buffer.as_mut_ptr() — no copy.
        // unsafe {
        //     let dma = &*DMA1::ptr();
        //     dma.st[0].m0ar.write(|w| w.bits(buffer.as_mut_ptr() as u32));
        //     ...
        // }

        Ok(I2cDmaTransfer {
            buffer,
            slave: self.slave_addr,
            is_read: true,
        })
    }
}


/// Demonstration of compile-time ownership enforcement.
///
/// Attempting to access `buf` after `write_async` would be a compile error:
///
/// ```compile_fail
/// let buf = DmaBuffer::<8>::new();
/// let xfer = driver.write_async(buf).unwrap();
/// let _ = buf.as_slice();  // ERROR: use of moved value: `buf`
/// ```
pub fn example_rust_zero_copy_write() {
    let mut driver = ZeroCopyI2c::new(0x6A);

    // Allocate and fill a DMA-safe buffer
    let mut buf = DmaBuffer::<8>::new();
    buf.fill_from(&[0x1A, 0x02, 0x00, 0x80]);

    // Move buffer into async transfer — buf is no longer accessible here.
    match driver.write_async(buf) {
        Ok(transfer) => {
            // `buf` is gone. Only `transfer` holds the buffer now.
            // Do other work while DMA runs...

            // Synchronously wait for completion; buffer is returned.
            let result = transfer.wait();

            match result.error {
                None => {
                    // result.buffer is ours again.
                    // We can read or reuse it — still no copy.
                    let _payload = result.buffer.as_slice();
                }
                Some(e) => {
                    // Handle error — buffer is still returned
                    let _ = e;
                }
            }
        }
        Err((buf_returned, e)) => {
            // Transfer failed to start; buffer returned to caller.
            let _ = (buf_returned, e);
        }
    }
}
```

---

### Example 5: Rust — Zero-Copy with `embedded-hal-async` and RTIC

This example shows idiomatic zero-copy I2C with the `embedded-hal-async` 1.0 interface and RTIC (Real-Time Interrupt-driven Concurrency framework), where Rust's async machinery and the RTIC task model work together to ensure safe, zero-copy DMA access.

```rust
//! zero_copy_i2c_rtic.rs
//! Zero-copy I2C using embedded-hal-async and the RTIC ownership model.
//!
//! Key insight: async/.await suspends the task at `.await` points.
//! The buffer is guaranteed live for the entire awaited duration
//! because it is on the task's stack — no heap allocation, no copy.

#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

// External crates (in Cargo.toml):
//   embedded-hal-async = "1.0"
//   rtic = { version = "2", features = ["thumbv7-backend"] }
//   rtic-monotonics = "2"

use core::future::Future;

/// Trait for async zero-copy I2C (mirrors embedded-hal-async::i2c::I2c).
pub trait ZeroCopyI2cAsync {
    type Error: core::fmt::Debug;

    /// Write `bytes` to `address`.
    ///
    /// ZERO-COPY GUARANTEE: The implementation programs DMA with
    /// a pointer to `bytes` and `.await`s DMA completion.
    /// The borrow `&[u8]` remains valid across the await because
    /// Rust guarantees the reference outlives the future.
    fn write(
        &mut self,
        address: u8,
        bytes: &[u8],         // borrowed — zero-copy, lifetime enforced
    ) -> impl Future<Output = Result<(), Self::Error>>;

    /// Read `bytes.len()` bytes from `address` into `bytes`.
    ///
    /// ZERO-COPY GUARANTEE: DMA writes directly into `bytes`.
    fn read(
        &mut self,
        address: u8,
        bytes: &mut [u8],     // mutably borrowed — exclusive access enforced
    ) -> impl Future<Output = Result<(), Self::Error>>;

    /// Combined write-then-read (register-addressed read).
    ///
    /// ZERO-COPY GUARANTEE: `write_bytes` and `read_bytes` are separate
    /// application buffers; the driver passes both directly to DMA
    /// using scatter-gather, eliminating all intermediate copies.
    fn write_read(
        &mut self,
        address:     u8,
        write_bytes: &[u8],
        read_bytes:  &mut [u8],
    ) -> impl Future<Output = Result<(), Self::Error>>;
}

/// Simulated async I2C implementation (platform stub).
pub struct I2cDmaAsync {
    base_addr: u32,
}

impl I2cDmaAsync {
    pub fn new(base_addr: u32) -> Self {
        Self { base_addr }
    }
}

impl ZeroCopyI2cAsync for I2cDmaAsync {
    type Error = I2cError;

    async fn write(&mut self, _address: u8, bytes: &[u8]) -> Result<(), I2cError> {
        // 1. Flush D-cache for bytes (on cached Cortex-A)
        //    unsafe { cortex_m7::SCB::clean_dcache_by_slice(bytes); }

        // 2. Program TX DMA with bytes.as_ptr() — no copy.
        //    The `bytes` borrow is alive for the entire async fn body,
        //    guaranteeing DMA can safely access it.
        //    unsafe {
        //        dma_program_tx(bytes.as_ptr(), bytes.len());
        //        dma_start();
        //    }

        // 3. Await DMA completion (suspends task, allows other tasks to run).
        //    poll_fn(|cx| {
        //        if dma_complete() { Poll::Ready(()) }
        //        else { register_waker(cx.waker()); Poll::Pending }
        //    }).await;

        // Stub: pretend DMA completed.
        let _ = bytes;
        Ok(())
    }

    async fn read(&mut self, _address: u8, bytes: &mut [u8]) -> Result<(), I2cError> {
        // DMA writes directly into bytes — no receive buffer.
        // unsafe { dma_program_rx(bytes.as_mut_ptr(), bytes.len()); }
        // dma_complete_future().await?;
        // unsafe { cortex_m7::SCB::invalidate_dcache_by_slice(bytes); }

        bytes.fill(0xAB); // stub: simulate received data
        Ok(())
    }

    async fn write_read(
        &mut self,
        _address:     u8,
        write_bytes: &[u8],
        read_bytes:  &mut [u8],
    ) -> Result<(), I2cError> {
        // Scatter-gather: TX segment points to write_bytes, RX to read_bytes.
        // No copies — two DMA descriptors, two application buffers.
        //
        // TX descriptor: { phys(write_bytes), write_bytes.len(), TX }
        // RX descriptor: { phys(read_bytes),  read_bytes.len(),  RX }
        //
        // Hardware: START → addr+W → write_bytes → RSTART → addr+R → read_bytes → STOP

        let _ = write_bytes;
        read_bytes.fill(0xCD); // stub: simulate received data
        Ok(())
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cError { Bus, Nack, Timeout, BusBusy, ArbitrationLost }


/// RTIC task using zero-copy async I2C.
///
/// The buffers `reg_addr` and `accel` are local to the async task.
/// Rust's async state machine captures them in the task's state,
/// keeping them alive across `.await` points without heap allocation.
/// DMA accesses them in-place — zero copies, zero dynamic allocation.
async fn sensor_read_task(i2c: &mut I2cDmaAsync) {
    let reg_addr: [u8; 1] = [0x28u8];         // LSM6DSO OUTX_L_A register
    let mut accel = [0u8; 6];                   // receive buffer on task stack

    // Combined zero-copy write-read: one scatter-gather DMA operation.
    // `reg_addr` and `accel` are stack-allocated; DMA touches them directly.
    match i2c.write_read(0x6A, &reg_addr, &mut accel).await {
        Ok(()) => {
            let ax = i16::from_le_bytes([accel[0], accel[1]]);
            let ay = i16::from_le_bytes([accel[2], accel[3]]);
            let az = i16::from_le_bytes([accel[4], accel[5]]);

            // Convert raw to mg (2g full scale: 0.061 mg/LSB)
            let ax_mg = ax as f32 * 0.061;
            let ay_mg = ay as f32 * 0.061;
            let az_mg = az as f32 * 0.061;

            let _ = (ax_mg, ay_mg, az_mg);
        }
        Err(e) => {
            let _ = e; // log error
        }
    }
}

/// Entry point (simplified — real RTIC app uses #[rtic::app] macro).
pub async fn app_main() {
    let mut i2c = I2cDmaAsync::new(0x4000_5400);
    loop {
        sensor_read_task(&mut i2c).await;
        // Delay 1 ms — cortex_m::asm::delay(168_000);
    }
}
```

---

### Example 6: Rust — Compile-Time Buffer Aliasing Prevention

This example explicitly demonstrates how Rust prevents the use-after-loan bug that is silent in C.

```rust
//! aliasing_prevention.rs
//! Demonstrates compile-time prevention of buffer aliasing bugs.

pub struct DmaBuf([u8; 64]);

impl DmaBuf {
    pub fn new() -> Self { Self([0u8; 64]) }
    pub fn as_slice(&self) -> &[u8] { &self.0 }
    pub fn as_mut_slice(&mut self) -> &mut [u8] { &mut self.0 }
}

pub struct FakeI2c;

impl FakeI2c {
    /// Takes ownership of buffer for transfer duration.
    pub fn start_transfer(self, buf: DmaBuf) -> InFlight {
        InFlight { _bus: self, _buf: buf }
    }
}

/// While this exists, neither `FakeI2c` nor `DmaBuf` can be accessed.
pub struct InFlight { _bus: FakeI2c, _buf: DmaBuf }

impl InFlight {
    pub fn finish(self) -> (FakeI2c, DmaBuf) {
        (self._bus, self._buf)
    }
}

pub fn show_compile_time_safety() {
    let bus = FakeI2c;
    let buf = DmaBuf::new();

    let in_flight = bus.start_transfer(buf);

    // The following lines would ALL be compile errors:
    //
    // let _ = buf.as_slice();
    //   ERROR: use of moved value: `buf`
    //
    // let _ = bus;
    //   ERROR: use of moved value: `bus`
    //
    // in_flight._buf.as_slice();
    //   ERROR: field `_buf` of struct `InFlight` is private
    //
    // C has none of these protections. The bugs are silent.

    let (_bus, _buf) = in_flight.finish();
    // Now both are safely returned.
    let _ = _buf.as_slice();  // OK: DMA is done, buffer is ours again.
}
```

---

## Linux Kernel I2C Zero-Copy Patterns

On Linux, the I2C subsystem uses `struct i2c_msg` to describe transfers. Each message carries a pointer to user/driver-owned memory; the core I2C framework does not copy it, but individual adapters may:

```c
/*
 * linux_i2c_zero_copy.c
 * Using the Linux kernel I2C API in a driver to perform zero-copy transfers.
 * The key: i2c_msg.buf points directly to driver-managed DMA-coherent memory.
 */

#include <linux/i2c.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

struct my_i2c_dev {
    struct i2c_client *client;
    u8                *dma_buf;      /* DMA-coherent buffer — no copy needed */
    dma_addr_t         dma_handle;
    size_t             buf_size;
};

static int my_dev_probe(struct i2c_client *client)
{
    struct my_i2c_dev *dev;
    dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;

    dev->client   = client;
    dev->buf_size = 256;

    /* Allocate DMA-coherent buffer — the CPU and DMA engine share this memory
     * without cache coherency issues. No memcpy needed between kernel buf
     * and this buffer; all I2C operations use it directly. */
    dev->dma_buf = dma_alloc_coherent(&client->dev,
                                       dev->buf_size,
                                       &dev->dma_handle,
                                       GFP_KERNEL);
    if (!dev->dma_buf) return -ENOMEM;

    i2c_set_clientdata(client, dev);
    return 0;
}

/*
 * Zero-copy register-addressed read.
 * No intermediate buffers — both reg_addr_byte and rx_data live in dma_buf.
 */
static int my_dev_reg_read(struct my_i2c_dev *dev,
                            u8 reg, u8 *out, size_t len)
{
    struct i2c_msg msgs[2];

    /* TX message: register address — points into dma_buf[0] */
    dev->dma_buf[0] = reg;
    msgs[0].addr  = dev->client->addr;
    msgs[0].flags = 0;                    /* write */
    msgs[0].len   = 1;
    msgs[0].buf   = dev->dma_buf;         /* zero-copy: DMA-coherent region */

    /* RX message: receive data — points into dma_buf[1..] */
    msgs[1].addr  = dev->client->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = len;
    msgs[1].buf   = dev->dma_buf + 1;     /* zero-copy: same DMA-coherent region */

    int ret = i2c_transfer(dev->client->adapter, msgs, 2);
    if (ret != 2) return ret < 0 ? ret : -EIO;

    /* Copy from DMA-coherent buffer to caller only once, here at the boundary */
    memcpy(out, dev->dma_buf + 1, len);
    return 0;
}

/* If the caller provides a DMA-safe out buffer, even this final memcpy
 * can be eliminated by pointing msgs[1].buf directly to `out`:
 *
 *   msgs[1].buf = out;  // caller ensures out is DMA-safe
 *
 * This is the canonical Linux zero-copy I2C pattern. */
```

---

## Benchmarking and Profiling

### What to Measure

To quantify the benefit of zero-copy I2C, measure:

1. **Transfer latency**: Time from `transfer_start()` to completion callback, using a logic analyzer or DWT cycle counter.
2. **CPU utilization during transfer**: With DMA + zero-copy, the CPU is free; with PIO + copies, it is fully occupied.
3. **Cache pressure**: Use PMU (Performance Monitoring Unit) counters (`CCNT`, `D_REFILL`) to count D-cache misses.
4. **Throughput**: Bytes per second for bulk operations (EEPROM writes, sensor streaming).

### Instrumentation Example (Cortex-M DWT)

```c
/*
 * benchmark_i2c.c
 * Measuring I2C transfer overhead with ARM Cortex-M DWT cycle counter.
 */

#include <stdint.h>

/* DWT registers (Cortex-M3/M4/M7) */
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004)
#define DEM_CR     (*(volatile uint32_t *)0xE000EDFC)

static void dwt_enable(void)
{
    DEM_CR   |= (1U << 24);   /* Enable DEM_CR TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL  |= (1U << 0);   /* Enable CYCCNT */
}

typedef struct {
    uint32_t cycles_with_copy;
    uint32_t cycles_zero_copy;
    uint32_t bytes_transferred;
} BenchmarkResult;

BenchmarkResult benchmark_i2c_approaches(void)
{
    BenchmarkResult r = {0};
    r.bytes_transferred = 128;

    uint8_t src[128];
    uint8_t staging[128];  /* intermediate copy buffer */
    uint8_t dma_buf[128] __attribute__((aligned(32)));

    /* --- Approach 1: With copy --- */
    uint32_t t0 = DWT_CYCCNT;
    memcpy(staging, src, 128);           /* copy 1: app → staging */
    memcpy(dma_buf, staging, 128);       /* copy 2: staging → DMA buf */
    /* i2c_polled_transfer(dma_buf, 128); */  /* simulated */
    r.cycles_with_copy = DWT_CYCCNT - t0;

    /* --- Approach 2: Zero-copy --- */
    t0 = DWT_CYCCNT;
    /* SCB_CleanDCache_by_Addr(src, 128); */  /* cache flush only */
    /* i2c_dma_transfer(src, 128); */          /* DMA from app buffer directly */
    r.cycles_zero_copy = DWT_CYCCNT - t0;

    return r;
}
```

### Typical Results (Cortex-M7 @ 400 MHz, 128 bytes, 400 kHz I2C)

| Approach | CPU cycles | CPU time | CPU utilization |
|---|---|---|---|
| Polled + 2x memcpy | ~14,400 | ~36 µs | 100% (blocking) |
| DMA + 1x memcpy | ~1,800 | ~4.5 µs | ~5% (mostly waiting) |
| DMA + zero-copy | ~400 | ~1 µs | ~1% (just DMA setup) |
| I2C wire time | — | ~2,800 µs | 0% (hardware) |

The zero-copy approach reduces CPU overhead by ~36x compared to fully polled, freeing the CPU for other tasks during the wire transfer.

---

## Pitfalls and Edge Cases

### 1. Cache Coherency on Cortex-M7 / Cortex-A

The Cortex-M7 has a configurable D-cache. If enabled, DMA reads stale cache lines unless the CPU flushes (cleans) the cache before TX and invalidates it after RX.

```c
/* Before TX: clean (write-back dirty lines to SRAM) */
SCB_CleanDCache_by_Addr((uint32_t *)buf, len);

/* After RX: invalidate (discard stale cached lines) */
SCB_InvalidateDCache_by_Addr((uint32_t *)buf, len);
```

**Failure mode**: Without the flush, the slave receives old data. Without the invalidate, the CPU reads old data after DMA fills the buffer. These bugs are non-deterministic and cache-state-dependent — extremely hard to reproduce.

### 2. Buffer Alignment for DMA and Cache Operations

Cache maintenance operations on Cortex-M7 and Cortex-A work on cache lines (32 bytes on M7, 64 bytes on A53). Partial-line operations risk corrupting adjacent data if the buffer is not aligned.

```c
/* WRONG: unaligned buffer — cache clean may corrupt adjacent data */
uint8_t bad_buf[17];

/* CORRECT: cache-line aligned */
uint8_t good_buf[32] __attribute__((aligned(32)));
```

In Rust, use `#[repr(align(32))]`:
```rust
#[repr(align(32))]
struct AlignedBuf([u8; 32]);
```

### 3. Buffer Lifetime with Asynchronous Transfers

In C, nothing prevents the caller from freeing a stack-allocated buffer before DMA completes:

```c
void bad_example(void) {
    uint8_t local_buf[8] = {0x01, 0x02};  /* stack allocated */
    i2c_dma_write_async(0x6A, local_buf, 8, cb, NULL);
    /* function returns → stack frame destroyed → DMA reads garbage */
}  /* ← STACK FREED HERE, DMA STILL RUNNING */
```

**Mitigations in C**: Use static or heap-allocated DMA buffers. Add a `__attribute__((noinline))` annotation to prevent compiler optimizations from reusing the stack. Document the ownership rule explicitly.

**Mitigation in Rust**: The ownership system prevents this. A reference cannot outlive the function frame unless explicitly stated through lifetime annotations, and moving the buffer into the transfer struct guarantees lifetime extension.

### 4. I2C Repeated-START vs STOP-START

Zero-copy scatter-gather relies on the hardware supporting repeated START. Not all I2C controllers do — some issue a STOP between messages, which some sensors reject (e.g., HTS221, BMI270 require a repeated START for register reads). Always verify controller and slave datasheet compatibility.

### 5. SMBUS vs I2C Protocols

SMBus (System Management Bus) adds packet error checking (PEC) — a CRC byte appended to the payload. In a zero-copy TX flow, the driver must append the PEC to the DMA buffer before starting the transfer, or use a hardware PEC generator that appends it transparently. The latter is the only truly zero-copy option for SMBus.

### 6. Interrupt Latency and NACK Recovery

If the slave issues a NACK mid-transfer, the I2C controller generates an error interrupt. The DMA channel must be aborted and the buffer reclaimed immediately. Failure to abort the DMA channel leaves it pointing at the buffer with potential for spurious access.

```c
void I2C1_ER_IRQHandler(void) {
    /* Abort DMA immediately */
    DMA1_S6->CR &= ~DMA_CR_EN;
    while (DMA1_S6->CR & DMA_CR_EN) {}  /* wait for disable */

    /* Issue STOP */
    I2C1->CR1 |= I2C_CR1_STOP;
    I2C1->SR1 = 0;  /* clear errors */

    g_state = I2C_IDLE;
    if (g_cb) g_cb(false, g_ctx);  /* return buffer to app with error */
}
```

---

## Summary

Zero-copy I2C transfer eliminates the intermediate data copies that waste CPU cycles, pollute the cache, and inflate transfer latency in naive I2C driver stacks. The core techniques are:

**DMA with direct application buffer access** removes the staging copy by programming the DMA engine to read from (TX) or write to (RX) application-owned memory directly. The application must ensure the buffer is DMA-accessible, properly aligned, and not accessed while the DMA is in flight.

**Scatter-gather (vectored I/O)** extends zero-copy to multi-segment transactions (such as register-address + payload) by providing the DMA engine with a descriptor list of buffer segments. The hardware concatenates the segments on the wire without any software-side copy.

**Cache maintenance** is the critical platform-specific detail on systems with a data cache: the application must flush (clean) the cache before TX and invalidate it after RX. Neglecting this causes silent data corruption that is extremely difficult to debug.

**Buffer ownership discipline** is the software contract that makes zero-copy safe. A buffer may not be accessed by the application while the DMA engine holds it. In C and C++, this discipline must be enforced by API convention, assertions, and documentation. In Rust, the ownership and borrowing system enforces it statically at compile time: moving the buffer into the transfer type makes any concurrent access a compile error rather than a runtime bug.

**The Linux kernel I2C model** achieves zero-copy by allocating DMA-coherent memory once at device probe time and passing pointers directly in `struct i2c_msg`. When callers provide their own DMA-safe buffers, the final boundary copy disappears as well.

The performance benefit is most pronounced in high-frequency polling loops and bulk data streaming: a properly implemented zero-copy DMA path reduces CPU overhead from O(N) bytes copied to O(1) DMA descriptor setup, freeing the CPU for application code while the I2C wire transfer proceeds entirely in hardware.

---

*Document: 86 — Zero-Copy I2C Transfers | Revision 1.0*