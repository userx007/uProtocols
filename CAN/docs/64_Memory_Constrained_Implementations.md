# 64. Memory-Constrained CAN Implementations

> **Topic:** Optimizing CAN stack footprint for microcontrollers with limited RAM and flash resources.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Understanding the Memory Landscape](#understanding-the-memory-landscape)
3. [CAN Stack Architecture Overview](#can-stack-architecture-overview)
4. [RAM Optimization Strategies](#ram-optimization-strategies)
5. [Flash / ROM Optimization Strategies](#flash--rom-optimization-strategies)
6. [Static vs. Dynamic Allocation](#static-vs-dynamic-allocation)
7. [Message Buffer Management](#message-buffer-management)
8. [Bit-Field Packing and Data Compression](#bit-field-packing-and-data-compression)
9. [Minimalist CAN Driver in C/C++](#minimalist-can-driver-in-cc)
10. [Minimalist CAN Driver in Rust](#minimalist-can-driver-in-rust)
11. [Interrupt-Driven Reception with Fixed Buffers](#interrupt-driven-reception-with-fixed-buffers)
12. [Compile-Time Configuration](#compile-time-configuration)
13. [Linker Script Optimization](#linker-script-optimization)
14. [Benchmarking and Measuring Footprint](#benchmarking-and-measuring-footprint)
15. [Summary](#summary)

---

## Introduction

Embedded systems in automotive, industrial, and IoT domains routinely rely on CAN (Controller Area Network) for deterministic real-time communication. Many target platforms — 8-bit AVRs, 16-bit MSP430s, or entry-level Cortex-M0/M0+ devices — offer as little as **2 KB to 32 KB of RAM** and **16 KB to 256 KB of flash**. A naïve CAN stack ported from a desktop-class environment can consume hundreds of kilobytes; on resource-constrained hardware this is simply not viable.

Memory-constrained CAN implementations demand a disciplined engineering approach:

- Every byte of RAM counts — dynamic allocation is often forbidden.
- Every function call costs code space — abstraction layers must earn their keep.
- Every feature that is not needed must be removed at compile time.
- Interrupt latency and determinism must not be sacrificed in the name of compactness.

This document walks through the complete toolbox of techniques for achieving a lean, correct, and maintainable CAN stack on microcontrollers with severe resource constraints.

---

## Understanding the Memory Landscape

### Typical Target Profiles

| MCU Class         | Flash        | RAM          | CAN Support               |
|-------------------|-------------|-------------|---------------------------|
| AVR (ATmega)      | 16–256 KB   | 1–16 KB     | External MCP2515 via SPI  |
| MSP430            | 16–256 KB   | 2–16 KB     | External TJA1050 via UART |
| Cortex-M0/M0+     | 32–256 KB   | 4–32 KB     | Integrated or SPI slave   |
| Cortex-M3/M4      | 128 KB–1 MB | 16–128 KB   | Integrated bxCAN / FDCAN  |
| Cortex-M4F (auto) | 1–4 MB      | 128–512 KB  | FDCAN, ISO TP, DiagOnCAN  |

### Memory Categories in a CAN Stack

```
+------------------+---------------------------+-----------------------------------+
| Category         | Typical Naive Size        | Optimized Target                  |
+------------------+---------------------------+-----------------------------------+
| RX message pool  | 1–8 KB (dynamic, heap)    | 64–512 B (static ring buffer)     |
| TX queue         | 512 B–2 KB (dynamic)      | 32–256 B (static FIFO)            |
| Filter table     | 256 B–1 KB               | 16–64 B (packed struct array)     |
| Stack code       | 10–60 KB                  | 1–8 KB (stripped, LTO'd)          |
| Protocol layers  | 4–20 KB (ISO TP, NM, etc) | Compile-time optional             |
+------------------+---------------------------+-----------------------------------+
```

The goal is to be **deliberate about every allocation**, keep data structures **cache-friendly and compact**, and rely on the **linker and compiler** as active optimization partners.

---

## CAN Stack Architecture Overview

A full-featured CAN stack (e.g., AUTOSAR ComStack) has many layers. On constrained hardware, most can be collapsed or removed:

```
Full Stack                          Minimal Stack
─────────────────────────          ─────────────────────────
  Application Layer                  Application Callbacks
       │                                    │
  COM / PDU Router                   ─ removed ─
       │                                    │
  CanIf (Interface)                  Thin shim (< 200 B)
       │                                    │
  CanDrv (Driver)                    CanDrv (optimized)
       │                                    │
  CAN HW Abstraction                 Direct register writes
       │                                    │
  CAN Hardware                       CAN Hardware
```

For deeply embedded targets, the goal is often a **single-layer driver** with:
- Statically allocated RX/TX buffers.
- Compile-time-defined filter lists.
- Interrupt-driven RX, polled or DMA-assisted TX.
- No heap usage whatsoever.

---

## RAM Optimization Strategies

### 1. Eliminate the Heap Entirely

Dynamic allocation (`malloc`/`free` or C++ `new`/`delete`) introduces fragmentation, non-determinism, and a hidden overhead of ~1–4 KB for the heap manager metadata itself. On targets with less than 8 KB of RAM, the heap is usually disabled entirely.

```c
/* In linker script or startup: heap size = 0 */
/* Verify at runtime in debug builds */
#ifdef DEBUG
extern uint8_t _heap_start;
extern uint8_t _heap_end;
static_assert(&_heap_end == &_heap_start, "Heap must be zero on this target");
#endif
```

### 2. Use `__attribute__((section))` to Control Placement

Critical buffers can be placed in fast SRAM (CCM on STM32, DTCM on H7):

```c
/* Place RX buffer in tightly-coupled memory for fast ISR access */
__attribute__((section(".dtcm_data")))
static can_frame_t g_rx_buffer[CAN_RX_BUFFER_SIZE];
```

### 3. Reuse Buffers via Unions

When RX and TX never overlap (half-duplex protocols or time-sliced operation), a union saves memory:

```c
typedef union {
    can_frame_t rx[4];
    can_frame_t tx[4];
} can_shared_buf_t;

static can_shared_buf_t g_can_buf; /* saves sizeof(can_frame_t) * 4 bytes */
```

### 4. Minimise Frame Struct Size

Pack the CAN frame tightly. Avoid padding by ordering fields largest-to-smallest:

```c
/* Unoptimised — compiler inserts 3 bytes of padding */
typedef struct {
    uint8_t  dlc;       /* 1 B */
    /* 3 B padding */
    uint32_t id;        /* 4 B */
    uint8_t  data[8];   /* 8 B */
} can_frame_loose_t;    /* = 16 B, wastes 3 B */

/* Optimised — no padding */
typedef struct {
    uint32_t id;        /* 4 B: CAN ID + flags in bits [28:0], EFF/RTR/ERR in [31:29] */
    uint8_t  data[8];   /* 8 B */
    uint8_t  dlc;       /* 1 B */
} __attribute__((packed)) can_frame_t; /* = 13 B */
```

---

## Flash / ROM Optimization Strategies

### 1. Link-Time Optimisation (LTO)

LTO allows the linker to inline, deduplicate, and prune code across translation units. It commonly saves **10–30%** of code size.

```makefile
# GCC / Clang
CFLAGS  += -Os -flto
LDFLAGS += -flto -Wl,--gc-sections
```

### 2. `-Os` vs `-O2` vs `-Oz`

| Flag  | Goal                        | Code Size | Speed |
|-------|-----------------------------|-----------|-------|
| `-O0` | No optimisation (debug)     | Largest   | Slow  |
| `-O2` | Balanced                    | Medium    | Fast  |
| `-Os` | Optimise for size           | Small     | Good  |
| `-Oz` | Aggressive size (Clang)     | Smallest  | Lower |

For CAN ISRs that must be fast, use `__attribute__((optimize("O2")))` locally even when global is `-Os`.

### 3. Remove Unused Protocol Layers

Use preprocessor guards to strip entire subsystems:

```c
/* can_config.h */
#define CAN_ENABLE_ISO_TP      0   /* ISO 15765-2 transport — not needed */
#define CAN_ENABLE_NM          0   /* Network Management — not needed */
#define CAN_ENABLE_DIAG        0   /* UDS diagnostics — not needed */
#define CAN_ENABLE_TIMESTAMP   0   /* No hardware timestamp unit */
#define CAN_RX_BUFFER_SIZE     4   /* Only 4 RX slots */
#define CAN_TX_BUFFER_SIZE     2   /* Only 2 TX slots */
```

### 4. `const` and `constexpr` for Read-Only Tables

Place filter tables, PDU descriptors, and baud rate tables in flash, not RAM:

```c
/* C */
static const can_filter_t CAN_FILTER_TABLE[] = {
    { .id = 0x100, .mask = 0x7FF, .flags = CAN_FILTER_STD },
    { .id = 0x200, .mask = 0x700, .flags = CAN_FILTER_STD },
};

/* C++ constexpr ensures compile-time evaluation and .rodata placement */
constexpr can_filter_t kFilterTable[] = {
    { 0x100, 0x7FF, CAN_FILTER_STD },
    { 0x200, 0x700, CAN_FILTER_STD },
};
```

### 5. `inline` and Macros for Hot Paths

Avoid function call overhead in the ISR path; use `static inline`:

```c
static inline uint8_t can_dlc_to_bytes(uint8_t dlc) {
    return (dlc > 8u) ? 8u : dlc;
}
```

---

## Static vs. Dynamic Allocation

The fundamental rule for constrained CAN stacks is: **all memory is allocated at compile time**.

### Static Ring Buffer (C)

```c
#define CAN_RX_RING_SIZE  8u   /* Must be power of 2 */
#define CAN_RX_RING_MASK  (CAN_RX_RING_SIZE - 1u)

typedef struct {
    can_frame_t frames[CAN_RX_RING_SIZE];
    volatile uint8_t head; /* written by ISR */
    volatile uint8_t tail; /* read by main loop */
} can_rx_ring_t;

static can_rx_ring_t g_rx_ring; /* 13 * 8 + 2 = 106 bytes of RAM */

/* Call from CAN RX ISR — no malloc, no branching on size */
static inline bool can_rx_ring_push(const can_frame_t *frame) {
    uint8_t next_head = (g_rx_ring.head + 1u) & CAN_RX_RING_MASK;
    if (next_head == g_rx_ring.tail) {
        return false; /* overflow — drop frame */
    }
    g_rx_ring.frames[g_rx_ring.head] = *frame;
    g_rx_ring.head = next_head;
    return true;
}

/* Call from task / main loop */
static inline bool can_rx_ring_pop(can_frame_t *frame) {
    if (g_rx_ring.tail == g_rx_ring.head) {
        return false; /* empty */
    }
    *frame = g_rx_ring.frames[g_rx_ring.tail];
    g_rx_ring.tail = (g_rx_ring.tail + 1u) & CAN_RX_RING_MASK;
    return true;
}
```

**RAM cost:** `sizeof(can_frame_t) * CAN_RX_RING_SIZE + 2` = 106 bytes for 8 frames.

---

## Message Buffer Management

### Double-Buffering for Zero-Copy ISR

On targets where copying 8 bytes in the ISR is too expensive, use double-buffering with index swapping:

```c
#define NUM_MAILBOXES  3u

typedef struct {
    can_frame_t buf[2];    /* ping-pong pair */
    volatile uint8_t active; /* index of buffer currently owned by ISR */
} can_mailbox_t;

static can_mailbox_t g_mailboxes[NUM_MAILBOXES];

/* ISR: write into inactive buffer, then atomically swap */
void CAN_RX_IRQHandler(void) {
    uint8_t mb_idx = CAN_HW_GetMailboxIdx(); /* hardware tells which mailbox fired */
    can_mailbox_t *mb = &g_mailboxes[mb_idx];
    uint8_t write_idx = mb->active ^ 1u;

    CAN_HW_ReadFrame(&mb->buf[write_idx]);   /* write to inactive side */
    mb->active = write_idx;                  /* atomic swap (single-byte write) */
}

/* Task: read from the active (most-recently-written) buffer */
bool can_mailbox_read(uint8_t mb_idx, can_frame_t *out) {
    if (mb_idx >= NUM_MAILBOXES) return false;
    *out = g_mailboxes[mb_idx].buf[g_mailboxes[mb_idx].active];
    return true;
}
```

---

## Bit-Field Packing and Data Compression

When PDU signal extraction must be ultra-compact, use bit manipulation rather than generated-code lookup tables:

```c
/* Extract a signal from a CAN payload using bit position and length */
static inline uint32_t can_extract_signal(
    const uint8_t *data,
    uint8_t  start_bit,  /* Intel byte order (LSB first) */
    uint8_t  bit_len,
    bool     is_signed)
{
    uint32_t raw = 0u;
    for (uint8_t i = 0u; i < bit_len; i++) {
        uint8_t bit_pos = start_bit + i;
        if (data[bit_pos >> 3u] & (1u << (bit_pos & 7u))) {
            raw |= (1u << i);
        }
    }
    if (is_signed && (raw & (1u << (bit_len - 1u)))) {
        raw |= ~((1u << bit_len) - 1u); /* sign-extend */
    }
    return raw;
}
```

For frequently-accessed signals, generate **inline unrolled extractors** at compile time:

```c
/* Compile-time-unrolled extractor for a known 12-bit signal at bit 4 */
#define CAN_SIGNAL_ENGINE_RPM(data) \
    (uint16_t)(((data)[0] >> 4u) | ((uint16_t)(data)[1] << 4u))
```

---

## Minimalist CAN Driver in C/C++

Below is a complete, self-contained CAN driver targeting an STM32F0 (Cortex-M0, 20 KB RAM, 64 KB Flash) with bxCAN peripheral. It uses zero dynamic allocation, interrupt-driven RX, and a compile-time-sized TX FIFO.

```c
/*============================================================
 * can_driver.h — Minimalist bxCAN driver for STM32F0
 *============================================================*/
#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Compile-time configuration ---- */
#ifndef CAN_RX_FIFO_DEPTH
#  define CAN_RX_FIFO_DEPTH  8u   /* power of 2 recommended */
#endif
#ifndef CAN_TX_FIFO_DEPTH
#  define CAN_TX_FIFO_DEPTH  4u
#endif

/* ---- Frame type ---- */
typedef struct {
    uint32_t id;       /* 11-bit or 29-bit CAN ID */
    uint8_t  data[8];
    uint8_t  dlc;      /* 0..8 */
    uint8_t  flags;    /* CAN_FLAG_EFF | CAN_FLAG_RTR */
} can_frame_t;

#define CAN_FLAG_EFF  0x01u  /* Extended (29-bit) ID */
#define CAN_FLAG_RTR  0x02u  /* Remote Transmission Request */

/* ---- API ---- */
void     can_init(uint32_t baud_kbps);
bool     can_transmit(const can_frame_t *frame);
bool     can_receive(can_frame_t *frame);
uint32_t can_rx_overflow_count(void);

#endif /* CAN_DRIVER_H */


/*============================================================
 * can_driver.c — Implementation
 *============================================================*/
#include "can_driver.h"
#include "stm32f0xx.h"  /* CMSIS device header */

/* ---- Static storage: zero heap usage ---- */
#define FIFO_MASK(depth)  ((depth) - 1u)

typedef struct {
    can_frame_t  buf[CAN_RX_FIFO_DEPTH];
    volatile uint8_t head;
    volatile uint8_t tail;
} rx_fifo_t;

typedef struct {
    can_frame_t  buf[CAN_TX_FIFO_DEPTH];
    uint8_t head;
    uint8_t tail;
} tx_fifo_t;

static rx_fifo_t     s_rx;
static tx_fifo_t     s_tx;
static volatile uint32_t s_rx_overflow;

/* ---- Baud rate table (stored in flash) ---- */
typedef struct { uint16_t prescaler; uint8_t ts1; uint8_t ts2; } bxcan_timing_t;

static const bxcan_timing_t k_timing_table[] = {
    /*  kbps  prescaler  TS1  TS2  (assuming 48 MHz APB1) */
    [0] = { 12u,  13u, 2u }, /* 250  kbps */
    [1] = {  6u,  13u, 2u }, /* 500  kbps */
    [2] = {  3u,  13u, 2u }, /* 1000 kbps */
};

static uint8_t baud_to_idx(uint32_t kbps) {
    if (kbps >= 1000u) return 2u;
    if (kbps >= 500u)  return 1u;
    return 0u;
}

/* ---- Init ---- */
void can_init(uint32_t baud_kbps) {
    /* Enable clocks */
    RCC->APB1ENR |= RCC_APB1ENR_CANEN;
    RCC->AHBENR  |= RCC_AHBENR_GPIOAEN;

    /* Configure PA11 (CAN_RX) and PA12 (CAN_TX) as AF4 */
    GPIOA->MODER   = (GPIOA->MODER & ~(0xFu << 22u)) | (0xAu << 22u);
    GPIOA->AFR[1] |= (4u << 12u) | (4u << 16u);

    /* Request init mode */
    CAN->MCR |= CAN_MCR_INRQ;
    while (!(CAN->MSR & CAN_MSR_INAK)) {}

    /* Disable sleep */
    CAN->MCR &= ~CAN_MCR_SLEEP;

    /* Apply timing */
    const bxcan_timing_t *t = &k_timing_table[baud_to_idx(baud_kbps)];
    CAN->BTR = (uint32_t)((t->prescaler - 1u) |
                          ((t->ts1 - 1u) << 16u) |
                          ((t->ts2 - 1u) << 20u));

    /* Single 32-bit filter: accept all (mask = 0) */
    CAN->FMR  |= CAN_FMR_FINIT;
    CAN->FA1R &= ~(1u << 0u);   /* deactivate filter 0 */
    CAN->FS1R |=  (1u << 0u);   /* 32-bit scale */
    CAN->FM1R &= ~(1u << 0u);   /* mask mode */
    CAN->sFilterRegister[0].FR1 = 0u;
    CAN->sFilterRegister[0].FR2 = 0u; /* mask = 0 → accept all */
    CAN->FA1R |=  (1u << 0u);   /* activate */
    CAN->FMR  &= ~CAN_FMR_FINIT;

    /* Enable FIFO0 message pending interrupt */
    CAN->IER |= CAN_IER_FMPIE0;
    NVIC_EnableIRQ(CEC_CAN_IRQn);
    NVIC_SetPriority(CEC_CAN_IRQn, 0u); /* highest priority */

    /* Leave init mode */
    CAN->MCR &= ~CAN_MCR_INRQ;
    while (CAN->MSR & CAN_MSR_INAK) {}
}

/* ---- Transmit (called from task context) ---- */
bool can_transmit(const can_frame_t *frame) {
    /* Find an empty hardware mailbox */
    uint8_t mb = 0u;
    for (; mb < 3u; mb++) {
        if (CAN->TSR & (CAN_TSR_TME0 << mb)) break;
    }
    if (mb == 3u) return false; /* no free mailbox */

    CAN_TxMailBox_TypeDef *mbox = &CAN->sTxMailBox[mb];

    if (frame->flags & CAN_FLAG_EFF) {
        mbox->TIR = (frame->id << 3u) | CAN_TI0R_EXID | CAN_TI0R_IDE;
    } else {
        mbox->TIR = (frame->id << 21u);
    }
    if (frame->flags & CAN_FLAG_RTR) {
        mbox->TIR |= CAN_TI0R_RTR;
    }

    mbox->TDTR = frame->dlc & 0x0Fu;
    mbox->TDLR = (uint32_t)frame->data[0]        |
                 ((uint32_t)frame->data[1] << 8u)  |
                 ((uint32_t)frame->data[2] << 16u) |
                 ((uint32_t)frame->data[3] << 24u);
    mbox->TDHR = (uint32_t)frame->data[4]        |
                 ((uint32_t)frame->data[5] << 8u)  |
                 ((uint32_t)frame->data[6] << 16u) |
                 ((uint32_t)frame->data[7] << 24u);

    mbox->TIR |= CAN_TI0R_TXRQ; /* arm transmit */
    return true;
}

/* ---- Receive (called from task / main loop) ---- */
bool can_receive(can_frame_t *frame) {
    if (s_rx.head == s_rx.tail) return false;
    *frame = s_rx.buf[s_rx.tail];
    s_rx.tail = (s_rx.tail + 1u) & (uint8_t)FIFO_MASK(CAN_RX_FIFO_DEPTH);
    return true;
}

uint32_t can_rx_overflow_count(void) { return s_rx_overflow; }

/* ---- ISR: CAN RX FIFO 0 message pending ---- */
void CEC_CAN_IRQHandler(void) {
    while (CAN->RF0R & CAN_RF0R_FMP0) { /* while messages pending */
        can_frame_t f;
        const CAN_FIFOMailBox_TypeDef *hw = &CAN->sFIFOMailBox[0];

        if (hw->RIR & CAN_RI0R_IDE) {
            f.id    = hw->RIR >> 3u;
            f.flags = CAN_FLAG_EFF;
        } else {
            f.id    = hw->RIR >> 21u;
            f.flags = 0u;
        }
        if (hw->RIR & CAN_RI0R_RTR) f.flags |= CAN_FLAG_RTR;

        f.dlc     = hw->RDTR & 0x0Fu;
        f.data[0] = (uint8_t)(hw->RDLR);
        f.data[1] = (uint8_t)(hw->RDLR >> 8u);
        f.data[2] = (uint8_t)(hw->RDLR >> 16u);
        f.data[3] = (uint8_t)(hw->RDLR >> 24u);
        f.data[4] = (uint8_t)(hw->RDHR);
        f.data[5] = (uint8_t)(hw->RDHR >> 8u);
        f.data[6] = (uint8_t)(hw->RDHR >> 16u);
        f.data[7] = (uint8_t)(hw->RDHR >> 24u);

        CAN->RF0R |= CAN_RF0R_RFOM0; /* release mailbox */

        uint8_t next = (s_rx.head + 1u) & (uint8_t)FIFO_MASK(CAN_RX_FIFO_DEPTH);
        if (next == s_rx.tail) {
            s_rx_overflow++;
        } else {
            s_rx.buf[s_rx.head] = f;
            s_rx.head = next;
        }
    }
}
```

### Compile-Time Size Verification

```c
/* can_assertions.h — catch regressions in CI */
#include <stddef.h>
_Static_assert(sizeof(can_frame_t)  <= 14u, "can_frame_t too large");
_Static_assert(sizeof(rx_fifo_t)    <= 128u, "RX FIFO exceeds budget");
_Static_assert((CAN_RX_FIFO_DEPTH & (CAN_RX_FIFO_DEPTH - 1u)) == 0u,
               "CAN_RX_FIFO_DEPTH must be power of 2");
```

---

## Minimalist CAN Driver in Rust

Rust's ownership model and zero-cost abstractions make it ideal for memory-safe embedded CAN development. Below is a complete `no_std` implementation targeting a generic CAN peripheral via `embedded-hal` traits, with a statically-allocated ring buffer.

```toml
# Cargo.toml
[package]
name    = "can-minimal"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal = { version = "1.0", default-features = false }
nb           = "1.1"

[profile.release]
opt-level = "s"      # optimise for size
lto       = true
codegen-units = 1    # better LTO
panic   = "abort"    # removes unwinding code (~2 KB saving)
```

```rust
//! can_driver.rs — `no_std`, no-heap CAN ring buffer + minimal driver
#![no_std]

use core::sync::atomic::{AtomicU8, Ordering};

// ── Frame type ──────────────────────────────────────────────────────────────

/// Compact CAN frame — 13 bytes, no padding
#[derive(Copy, Clone, Debug)]
#[repr(C, packed)]
pub struct CanFrame {
    /// CAN identifier. Bit 31 = EFF flag, bit 30 = RTR flag
    pub id: u32,
    pub data: [u8; 8],
    pub dlc: u8,
}

impl CanFrame {
    pub const EFF_FLAG: u32 = 1 << 31;
    pub const RTR_FLAG: u32 = 1 << 30;

    #[inline]
    pub fn new_std(id: u16, data: &[u8]) -> Self {
        let dlc = data.len().min(8) as u8;
        let mut f = CanFrame { id: id as u32, data: [0u8; 8], dlc };
        f.data[..dlc as usize].copy_from_slice(&data[..dlc as usize]);
        f
    }

    #[inline]
    pub fn is_extended(&self) -> bool { self.id & Self::EFF_FLAG != 0 }

    #[inline]
    pub fn raw_id(&self) -> u32 { self.id & 0x1FFF_FFFF }
}

// ── Static ring buffer ───────────────────────────────────────────────────────

/// Compile-time-sized lock-free single-producer / single-consumer ring buffer.
/// N must be a power of two.
pub struct RingBuffer<const N: usize> {
    buf:  [CanFrame; N],
    head: AtomicU8,   // written by ISR producer
    tail: AtomicU8,   // read  by task consumer
}

// SAFETY: we enforce single-producer / single-consumer discipline by convention.
unsafe impl<const N: usize> Sync for RingBuffer<N> {}

impl<const N: usize> RingBuffer<N> {
    const MASK: u8 = (N - 1) as u8;

    // Verify power-of-two at compile time
    const _ASSERT_POW2: () = assert!(N.is_power_of_two(), "N must be a power of two");
    const _ASSERT_FIT:  () = assert!(N <= 128,            "N must fit in u8 index");

    pub const fn new() -> Self {
        RingBuffer {
            buf:  [CanFrame { id: 0, data: [0u8; 8], dlc: 0 }; N],
            head: AtomicU8::new(0),
            tail: AtomicU8::new(0),
        }
    }

    /// Push a frame (called from ISR). Returns false on overflow (frame dropped).
    #[inline]
    pub fn push(&self, frame: CanFrame) -> bool {
        let head = self.head.load(Ordering::Relaxed);
        let next = (head.wrapping_add(1)) & Self::MASK;
        if next == self.tail.load(Ordering::Acquire) {
            return false; // full — drop
        }
        // SAFETY: only one producer; index is within bounds
        unsafe {
            let slot = &self.buf[head as usize] as *const CanFrame as *mut CanFrame;
            slot.write_volatile(frame);
        }
        self.head.store(next, Ordering::Release);
        true
    }

    /// Pop a frame (called from task). Returns None if empty.
    #[inline]
    pub fn pop(&self) -> Option<CanFrame> {
        let tail = self.tail.load(Ordering::Relaxed);
        if tail == self.head.load(Ordering::Acquire) {
            return None;
        }
        let frame = unsafe {
            (&self.buf[tail as usize] as *const CanFrame).read_volatile()
        };
        self.tail.store((tail.wrapping_add(1)) & Self::MASK, Ordering::Release);
        Some(frame)
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.head.load(Ordering::Relaxed) == self.tail.load(Ordering::Relaxed)
    }
}

// ── Filter ───────────────────────────────────────────────────────────────────

/// Compile-time-fixed filter entry stored in .rodata
#[derive(Copy, Clone)]
pub struct CanFilter {
    pub id:   u32,
    pub mask: u32, // 1 = must match, 0 = don't care
}

impl CanFilter {
    #[inline]
    pub const fn accepts(&self, frame_id: u32) -> bool {
        (frame_id & self.mask) == (self.id & self.mask)
    }
}

/// Software filter bank — kept in flash
pub struct FilterBank<const F: usize> {
    filters: [CanFilter; F],
    count:   usize,
}

impl<const F: usize> FilterBank<F> {
    pub const fn new(filters: [CanFilter; F]) -> Self {
        FilterBank { filters, count: F }
    }

    #[inline]
    pub fn accepts(&self, id: u32) -> bool {
        for i in 0..self.count {
            if self.filters[i].accepts(id) {
                return true;
            }
        }
        false
    }
}

// ── Driver ───────────────────────────────────────────────────────────────────

use core::cell::UnsafeCell;

/// Minimalist CAN driver wrapping a hardware peripheral register block.
/// `Regs` is a thin HAL abstraction (e.g. from a PAC crate).
pub struct CanDriver<const RX: usize, const F: usize> {
    rx_buf:     RingBuffer<RX>,
    filters:    FilterBank<F>,
    overflow:   AtomicU8,
}

impl<const RX: usize, const F: usize> CanDriver<RX, F> {
    pub const fn new(filters: FilterBank<F>) -> Self {
        CanDriver {
            rx_buf:   RingBuffer::new(),
            filters,
            overflow: AtomicU8::new(0),
        }
    }

    /// Called from hardware ISR — no allocation, no panic paths
    #[inline(always)]
    pub fn on_rx_irq(&self, raw_id: u32, data: [u8; 8], dlc: u8) {
        if !self.filters.accepts(raw_id) { return; }
        let frame = CanFrame { id: raw_id, data, dlc };
        if !self.rx_buf.push(frame) {
            // Saturating increment — won't panic on overflow
            let _ = self.overflow.fetch_update(
                Ordering::Relaxed, Ordering::Relaxed,
                |v| Some(v.saturating_add(1))
            );
        }
    }

    /// Called from task context
    #[inline]
    pub fn receive(&self) -> Option<CanFrame> {
        self.rx_buf.pop()
    }

    pub fn overflow_count(&self) -> u8 {
        self.overflow.load(Ordering::Relaxed)
    }
}

// ── Static global instance ────────────────────────────────────────────────────

/// All memory allocated statically — zero heap, zero runtime cost
static FILTER_BANK: FilterBank<2> = FilterBank::new([
    CanFilter { id: 0x100, mask: 0x7FF },
    CanFilter { id: 0x200, mask: 0x700 },
]);

/// 8-slot RX buffer, 2 filters — total RAM: sizeof(RingBuffer<8>) = ~108 bytes
static CAN: CanDriver<8, 2> = CanDriver::new(FilterBank::new([
    CanFilter { id: 0x100, mask: 0x7FF },
    CanFilter { id: 0x200, mask: 0x700 },
]));

// ── Application entry point (example) ────────────────────────────────────────

/// Example main loop — on a real target this would be in main.rs with #[entry]
pub fn example_main_loop() {
    loop {
        while let Some(frame) = CAN.receive() {
            // Process frame — all on the stack, no heap
            let _id  = frame.raw_id();
            let _dlc = frame.dlc as usize;
            // route by ID ...
        }
        // sleep / WFI here
    }
}
```

### Measuring Rust Binary Size

```bash
# Build release
cargo build --release --target thumbv6m-none-eabi

# Inspect section sizes
arm-none-eabi-size -A target/thumbv6m-none-eabi/release/can-minimal

# Typical output for the above driver:
# section     size      addr
# .text        892      0x08000000   ← code in flash
# .rodata       32      0x080003A0   ← filter table in flash
# .bss         116      0x20000000   ← ring buffer in RAM
# .data          0      0x20000074
# Total:      1040

# Symbol-level breakdown
cargo install cargo-bloat
cargo bloat --release --target thumbv6m-none-eabi -n 20
```

---

## Interrupt-Driven Reception with Fixed Buffers

A correct ISR design for memory-constrained systems must satisfy:

1. **No blocking** — ISR never waits for a lock.
2. **No allocation** — ISR only writes to pre-allocated slots.
3. **Deterministic latency** — bounded number of instructions.
4. **Safe concurrent access** — use atomic indices or critical sections.

### C Example: Critical-Section-Protected Shared Buffer

```c
#include <stdint.h>
#include <stdbool.h>

#define RX_SLOTS  4u

typedef struct {
    can_frame_t frames[RX_SLOTS];
    uint8_t     write_idx; /* updated only in ISR   */
    uint8_t     read_idx;  /* updated only in task  */
    uint8_t     count;     /* updated under CS      */
} can_shared_t;

static volatile can_shared_t s_shared;

/* --- ISR (no lock needed — only ISR touches write_idx) --- */
void CAN_RX_IRQHandler(void) {
    if (s_shared.count < RX_SLOTS) {
        CAN_HW_ReadFrame(
            (can_frame_t *)&s_shared.frames[s_shared.write_idx]);
        s_shared.write_idx = (s_shared.write_idx + 1u) % RX_SLOTS;
        s_shared.count++;   /* write last: consumer sees consistent state */
    }
    /* if count == RX_SLOTS: hardware FIFO holds frames until we catch up */
}

/* --- Task (enter critical section only to read count) --- */
bool can_task_receive(can_frame_t *out) {
    __disable_irq();
    bool avail = (s_shared.count > 0u);
    __enable_irq();

    if (!avail) return false;

    *out = s_shared.frames[s_shared.read_idx];
    s_shared.read_idx = (s_shared.read_idx + 1u) % RX_SLOTS;

    __disable_irq();
    s_shared.count--;
    __enable_irq();
    return true;
}
```

---

## Compile-Time Configuration

Centralise all memory budget decisions in a single header. This makes it trivial to produce multiple build targets (e.g., a "tiny" variant for 8 KB RAM MCUs and a "standard" variant for 32 KB MCUs):

```c
/* can_config.h */
#pragma once

/* ---- Build variant selection ---- */
#if   defined(TARGET_TINY)
#  define CAN_RX_BUFFER_SIZE   4u
#  define CAN_TX_BUFFER_SIZE   2u
#  define CAN_FILTER_COUNT     4u
#  define CAN_ENABLE_STATS     0
#  define CAN_ENABLE_ISO_TP    0
#  define CAN_ENABLE_NM        0

#elif defined(TARGET_STANDARD)
#  define CAN_RX_BUFFER_SIZE  16u
#  define CAN_TX_BUFFER_SIZE   8u
#  define CAN_FILTER_COUNT    16u
#  define CAN_ENABLE_STATS     1
#  define CAN_ENABLE_ISO_TP    1
#  define CAN_ENABLE_NM        0

#else
#  error "Define TARGET_TINY or TARGET_STANDARD"
#endif

/* ---- Sanity checks ---- */
#if (CAN_RX_BUFFER_SIZE & (CAN_RX_BUFFER_SIZE - 1u)) != 0u
#  error "CAN_RX_BUFFER_SIZE must be a power of 2"
#endif
#if CAN_FILTER_COUNT > 28u
#  error "bxCAN supports max 28 filter banks"
#endif

/* ---- Derived constants ---- */
#define CAN_RX_BUFFER_MASK  (CAN_RX_BUFFER_SIZE - 1u)
#define CAN_TX_BUFFER_MASK  (CAN_TX_BUFFER_SIZE  - 1u)
```

And for Rust using const generics and cargo features:

```toml
# Cargo.toml
[features]
default  = ["target-standard"]
target-tiny     = []
target-standard = []
```

```rust
// lib.rs
#[cfg(feature = "target-tiny")]
const RX_DEPTH: usize = 4;
#[cfg(feature = "target-standard")]
const RX_DEPTH: usize = 16;

pub static CAN_DRIVER: CanDriver<RX_DEPTH, 4> = CanDriver::new(/* ... */);
```

---

## Linker Script Optimization

The linker script is an often-overlooked tool for squeezing memory usage:

```ld
/* stm32f0_minimal.ld */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS {
    /* ---- FLASH sections ---- */
    .text : {
        *(.isr_vector)
        *(.text*)
        *(.rodata*)         /* const tables go here */
        . = ALIGN(4);
    } > FLASH

    /* ---- RAM sections ---- */
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    /* ---- Remove C++ exceptions (saves ~2–4 KB) ---- */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.ARM.exidx*)
        *(.gcc_except_table*)
    }

    /* ---- Stack at end of RAM ---- */
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
```

For CAN specifically: placing the RX ring buffer in a named section lets you verify its size in the map file and ensure it lands in fast SRAM on dual-bank devices.

---

## Benchmarking and Measuring Footprint

Always measure, never guess. After building:

```bash
# GCC — section sizes
arm-none-eabi-size -A build/firmware.elf

# Detailed symbol breakdown — identify bloat
arm-none-eabi-nm --size-sort --print-size build/firmware.elf | grep -v " U "

# Callgraph to find inlining failures
arm-none-eabi-objdump -d build/firmware.elf | grep "bl " | sort | uniq -c | sort -rn

# RAM usage: BSS + DATA
arm-none-eabi-size build/firmware.elf
# output: text  data  bss  dec  filename
#         5320    24  116  5460 firmware.elf
```

### Rust: `cargo-bloat` and `twiggy`

```bash
# Top contributors to binary size
cargo bloat --release --target thumbv6m-none-eabi -n 20

# Call-graph-aware size analysis
cargo install twiggy
twiggy top -n 20 target/thumbv6m-none-eabi/release/can-minimal.elf
twiggy dominators   target/thumbv6m-none-eabi/release/can-minimal.elf
```

### Golden Rules for Size Budgets

| Resource              | Budget (Tiny target) | Budget (Standard target) |
|-----------------------|---------------------|--------------------------|
| CAN driver code       | ≤ 1 KB flash        | ≤ 4 KB flash             |
| RX buffer             | ≤ 64 B RAM          | ≤ 256 B RAM              |
| TX buffer             | ≤ 32 B RAM          | ≤ 128 B RAM              |
| Filter table          | ≤ 32 B flash        | ≤ 128 B flash            |
| ISR worst-case cycles | ≤ 100 cycles        | ≤ 200 cycles             |

---

## Summary

Memory-constrained CAN implementations require a holistic approach across hardware selection, software architecture, and build tooling. The key principles are:

**Architecture**
- Collapse the stack to a single driver layer; remove every abstraction that does not earn its keep.
- Use static allocation exclusively — no heap, no fragmentation, no non-determinism.
- Size all buffers as powers of two to enable branchless ring-buffer arithmetic.
- Place read-only tables (filters, timing, PDU descriptors) in flash (`const` / `constexpr` / `.rodata`).

**C / C++ Techniques**
- Pack structs with `__attribute__((packed))` and field-ordering discipline.
- Gate every optional subsystem behind compile-time `#if` guards in a single `can_config.h`.
- Use `static inline` for hot-path helpers to eliminate call overhead.
- Verify budgets with `_Static_assert` and catch regressions in CI with `arm-none-eabi-size`.

**Rust Techniques**
- Use `const N: usize` generics to express buffer sizes as type-level parameters; the compiler enforces constraints at build time.
- Enable `panic = "abort"`, `lto = true`, and `opt-level = "s"` in `[profile.release]`.
- Exploit Rust's ownership system to eliminate runtime checks (bounds, null, UAF) without defensive code overhead.
- Use `cargo-bloat` and `twiggy` to pinpoint size regressions before they ship.

**Build System**
- Enable LTO and `--gc-sections` unconditionally on embedded targets.
- Provide named build variants (`TARGET_TINY` / `TARGET_STANDARD`) so the same codebase scales across hardware families.
- Review the map file after every significant change to confirm no new RAM or flash consumers have been introduced silently.

The result of applying these techniques consistently is a CAN driver that occupies **under 1 KB of flash** and **under 128 bytes of RAM** on the smallest targets — leaving the majority of the device's resources available for the application itself.

---

*Document: 64 — Memory-Constrained CAN Implementations | CAN Programming Reference Series*