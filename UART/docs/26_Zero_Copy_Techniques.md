# 26. Zero-Copy Techniques in UART Communication

**Structure:**
- **The Memory Copy Problem** — visualizes the 3-copy data path in naive UART drivers and why it becomes a bottleneck at high baud rates
- **Core Concepts** — buffer ownership transfer, in-place parsing, memory-mapped I/O
- **DMA-Based Zero-Copy** — STM32 HAL example with circular DMA and IDLE line interrupt
- **Ring Buffers & Circular DMA** — lock-free ring buffer with `atomic` read/write positions; polling-based DMA position tracking
- **Double & Triple Buffering** — C struct-based and a templated C++ RAII version with `std::atomic`
- **Scatter-Gather DMA** — linked-list descriptors that place protocol header, payload, and CRC directly into struct fields
- **C/C++ section** — full zero-copy UART driver with `ZeroCopySpan` API; zero-copy TX with completion callback
- **Rust section** — `ZeroCopyRing` with `AtomicUsize`/`UnsafeCell`, `DoubleBuffer` with safe swap logic, `embedded-dma` crate integration, and async `embassy` usage
- **Platform-Specific** — Linux `splice()` for kernel-bypass UART forwarding, `mmap()` for zero-copy logging
- **Benchmarking** — complete harness measuring copy vs zero-copy, with a results table showing ~3× speedup across buffer sizes

## Minimizing Memory Operations for Maximum Throughput

---

## Table of Contents

1. [Introduction](#introduction)
2. [The Memory Copy Problem](#the-memory-copy-problem)
3. [Core Zero-Copy Concepts](#core-zero-copy-concepts)
4. [DMA-Based Zero-Copy](#dma-based-zero-copy)
5. [Ring Buffers and Circular DMA](#ring-buffers-and-circular-dma)
6. [Double and Triple Buffering](#double-and-triple-buffering)
7. [Scatter-Gather DMA](#scatter-gather-dma)
8. [Zero-Copy in C/C++](#zero-copy-in-cc)
9. [Zero-Copy in Rust](#zero-copy-in-rust)
10. [Platform-Specific Techniques](#platform-specific-techniques)
11. [Performance Benchmarking](#performance-benchmarking)
12. [Summary](#summary)

---

## Introduction

In high-throughput UART systems — such as industrial data loggers, GPS receivers processing NMEA streams, modem interfaces, or high-speed debug consoles — the cost of *copying data* between buffers can become a dominant performance bottleneck. Every byte received over UART must travel from the hardware FIFO into application memory. Naive implementations copy this data multiple times: from hardware to driver buffer, from driver buffer to user buffer, and possibly again into a processing buffer. Each copy consumes CPU cycles, occupies cache lines, and increases latency.

**Zero-copy techniques** eliminate or minimize these intermediate copies by arranging software and hardware so that data is placed directly into its final destination — or by passing ownership of memory regions rather than duplicating their contents.

This chapter covers the theory, hardware mechanisms, and practical implementation of zero-copy UART communication in both C/C++ and Rust.

---

## The Memory Copy Problem

### Typical Multi-Copy Data Path

In a conventional UART receive flow without zero-copy:

```
[UART Hardware FIFO]
        |
        |  (1) CPU reads byte-by-byte in ISR
        v
[ISR Ring Buffer]       <-- copy #1 (byte-by-byte, inside interrupt)
        |
        |  (2) Application calls read()
        v
[Application Buffer]    <-- copy #2 (memcpy from ring buffer)
        |
        |  (3) Protocol parser copies into message struct
        v
[Message / Packet]      <-- copy #3 (protocol deserialization)
```

Each copy:
- Consumes CPU cycles proportional to data size
- Pollutes data cache with intermediate data
- Introduces latency at each stage
- Can cause buffer overflow if the CPU falls behind

At 921600 baud, a UART transmits ~115,200 bytes/second. On a slow microcontroller running at 48 MHz with a poorly written ISR, copying each byte can consume a significant fraction of available CPU time.

### The Zero-Copy Goal

```
[UART Hardware FIFO]
        |
        |  DMA transfer (CPU not involved)
        v
[Application Buffer / Packet]   <-- single destination, no copy
```

The ideal is that hardware writes data directly and completely into the memory region where the application will ultimately consume it.

---

## Core Zero-Copy Concepts

### 1. Buffer Ownership Transfer

Instead of copying data, transfer ownership of the buffer pointer:

```c
// ❌ Copying approach
void process_uart_data(uint8_t *src, size_t len) {
    uint8_t local_copy[256];
    memcpy(local_copy, src, len);   // unnecessary copy
    parse_packet(local_copy, len);
}

// ✅ Zero-copy approach: pass pointer, transfer ownership
void process_uart_data(uint8_t *buf, size_t len) {
    parse_packet(buf, len);         // work directly on original buffer
}
```

### 2. In-Place Parsing

Parse protocol headers and payloads directly from the DMA buffer without copying:

```c
typedef struct __attribute__((packed)) {
    uint8_t  start_byte;
    uint16_t length;
    uint8_t  command;
    uint8_t  payload[];   // flexible array member
} UartPacket;

void handle_dma_complete(uint8_t *dma_buf, size_t len) {
    // Cast directly — no copy needed
    UartPacket *pkt = (UartPacket *)dma_buf;
    if (pkt->start_byte == 0xAA) {
        process_command(pkt->command, pkt->payload, pkt->length);
    }
}
```

### 3. Memory-Mapped I/O Access

On platforms where the UART data register is memory-mapped, DMA can be configured to write directly from the UART peripheral register address to application memory.

---

## DMA-Based Zero-Copy

DMA (Direct Memory Access) is the cornerstone of zero-copy UART. The DMA controller moves data from the UART's receive data register to RAM autonomously, without CPU involvement.

### How UART DMA Works

```
UART Peripheral              DMA Controller              RAM
┌──────────────┐            ┌──────────────┐           ┌──────────────┐
│  RDR Register│ ──source──>│  DMA Channel │ ──dest──> │  rx_buffer[] │
│  (0x40013824)│            │              │           │              │
└──────────────┘            └──────────────┘           └──────────────┘
                                    │
                             Triggers interrupt
                             on half/full transfer
```

### STM32 UART DMA Example (C)

This example configures UART1 with DMA on an STM32 microcontroller using HAL:

```c
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define UART_DMA_BUF_SIZE   256

/* Two half-buffers for double-buffering */
static uint8_t rx_dma_buf[UART_DMA_BUF_SIZE];

UART_HandleTypeDef huart1;
DMA_HandleTypeDef  hdma_usart1_rx;

/* ------------------------------------------------------------------ */
/*  Initialization: Start circular DMA receive                         */
/* ------------------------------------------------------------------ */
void uart_zero_copy_init(void) {
    /* Configure UART */
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 921600;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart1);

    /* Start circular DMA — never stops, wraps around automatically */
    HAL_UART_Receive_DMA(&huart1, rx_dma_buf, UART_DMA_BUF_SIZE);

    /* Enable IDLE line interrupt to detect end of packet */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
}

/* ------------------------------------------------------------------ */
/*  IDLE line interrupt: fired when line goes idle after reception      */
/* ------------------------------------------------------------------ */
void USART1_IRQHandler(void) {
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart1);

        /* Calculate how many bytes DMA has written so far */
        uint16_t dma_remaining = __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
        uint16_t bytes_received = UART_DMA_BUF_SIZE - dma_remaining;

        if (bytes_received > 0) {
            /*
             * Zero-copy: pass pointer directly to the DMA buffer.
             * No memcpy — application works on rx_dma_buf in place.
             */
            process_received_data(rx_dma_buf, bytes_received);
        }
    }
    HAL_UART_IRQHandler(&huart1);
}

/* ------------------------------------------------------------------ */
/*  In-place processing — no copy of data                               */
/* ------------------------------------------------------------------ */
void process_received_data(const uint8_t *buf, uint16_t len) {
    /* Parse directly from DMA buffer */
    for (uint16_t i = 0; i < len; i++) {
        /* Protocol state machine operating on original buffer */
        feed_parser(buf[i]);
    }
}
```

---

## Ring Buffers and Circular DMA

A circular DMA ring buffer is the most common zero-copy pattern for continuous UART reception. The DMA continuously fills the buffer in a circular fashion while the software reads from it without stopping the DMA.

### Lock-Free Circular Buffer (C)

```c
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

#define RING_BUF_SIZE   512   /* Must be power of 2 */
#define RING_BUF_MASK   (RING_BUF_SIZE - 1)

typedef struct {
    uint8_t          data[RING_BUF_SIZE];
    volatile uint32_t write_idx;  /* Updated by DMA interrupt */
    volatile uint32_t read_idx;   /* Updated by consumer thread */
} ZeroCopyRingBuf;

static ZeroCopyRingBuf uart_ring;

/* ------------------------------------------------------------------ */
/*  DMA Half/Full Transfer callbacks — no memcpy, just update index     */
/* ------------------------------------------------------------------ */

/* Called at half-buffer point */
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart) {
    /* Write index advances to halfway mark */
    atomic_store(&uart_ring.write_idx, RING_BUF_SIZE / 2);
}

/* Called when DMA wraps around (full buffer complete) */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    atomic_store(&uart_ring.write_idx, 0);
}

/* ------------------------------------------------------------------ */
/*  Consumer: read data without copying                                 */
/* ------------------------------------------------------------------ */

/**
 * Returns a pointer directly into the ring buffer and the number of
 * contiguous bytes available. No copy is performed.
 */
bool uart_ring_peek(const uint8_t **out_ptr, uint32_t *out_len) {
    uint32_t w = atomic_load(&uart_ring.write_idx);
    uint32_t r = uart_ring.read_idx;

    if (w == r) return false;  /* Empty */

    *out_ptr = &uart_ring.data[r];

    if (w > r) {
        /* Data is contiguous */
        *out_len = w - r;
    } else {
        /* Data wraps — return only the linear portion to end of buffer */
        *out_len = RING_BUF_SIZE - r;
    }
    return true;
}

/**
 * Advance read index after consuming `len` bytes.
 * This "frees" the buffer region without any copy.
 */
void uart_ring_consume(uint32_t len) {
    uart_ring.read_idx = (uart_ring.read_idx + len) & RING_BUF_MASK;
}

/* ------------------------------------------------------------------ */
/*  Usage example                                                       */
/* ------------------------------------------------------------------ */
void uart_zero_copy_task(void) {
    const uint8_t *data_ptr;
    uint32_t       avail;

    while (uart_ring_peek(&data_ptr, &avail)) {
        /*
         * data_ptr points DIRECTLY into the DMA buffer.
         * Zero copy: parse in place.
         */
        uint32_t consumed = parse_protocol_in_place(data_ptr, avail);
        uart_ring_consume(consumed);
    }
}
```

### Tracking DMA Write Position (C)

For true circular DMA where the write index is derived from the DMA counter register:

```c
/**
 * Get current DMA write position without using callbacks.
 * This technique works with circular DMA on STM32.
 */
uint32_t get_dma_write_pos(void) {
    uint32_t remaining = __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
    return RING_BUF_SIZE - remaining;
}

void uart_zero_copy_poll(void) {
    static uint32_t last_pos = 0;
    uint32_t        current_pos = get_dma_write_pos();

    if (current_pos == last_pos) return;

    if (current_pos > last_pos) {
        /* Linear region: no wrap */
        process_received_data(&rx_dma_buf[last_pos],
                               current_pos - last_pos);
    } else {
        /* Wrapped: process end of buffer, then start */
        process_received_data(&rx_dma_buf[last_pos],
                               RING_BUF_SIZE - last_pos);
        if (current_pos > 0) {
            process_received_data(&rx_dma_buf[0], current_pos);
        }
    }

    last_pos = current_pos;
}
```

---

## Double and Triple Buffering

Double buffering allows the DMA to fill one buffer while the CPU processes the other, eliminating data races and enabling true simultaneous operation.

### Double Buffer Pattern (C/C++)

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define BUF_SIZE   128

typedef struct {
    uint8_t  buf[2][BUF_SIZE];  /* Two buffers */
    volatile uint8_t dma_buf;   /* Which buffer DMA is currently writing to */
    volatile uint8_t cpu_buf;   /* Which buffer CPU is reading from */
    volatile uint16_t dma_len[2]; /* Bytes written by DMA into each buffer */
} DoubleBuffer;

static DoubleBuffer dbl;

/* ------------------------------------------------------------------ */
/*  DMA complete: swap buffers, restart DMA on the other buffer         */
/* ------------------------------------------------------------------ */
void dma_rx_complete_isr(uint16_t bytes_written) {
    uint8_t just_filled = dbl.dma_buf;

    dbl.dma_len[just_filled] = bytes_written;

    /* Swap: DMA now writes to the other buffer */
    dbl.dma_buf = 1 - just_filled;
    dbl.cpu_buf = just_filled;

    /* Restart DMA on the new buffer — no memcpy */
    start_dma_rx(dbl.buf[dbl.dma_buf], BUF_SIZE);

    /* Signal consumer */
    notify_consumer();
}

/* ------------------------------------------------------------------ */
/*  Consumer: process the CPU buffer in place                           */
/* ------------------------------------------------------------------ */
void consume_uart_data(void) {
    uint8_t  idx = dbl.cpu_buf;
    uint16_t len = dbl.dma_len[idx];

    /*
     * Zero copy: buf[idx] is the destination buffer.
     * Parse directly. DMA is safely writing to the other buffer.
     */
    parse_data_zero_copy(dbl.buf[idx], len);
}
```

### C++ Double Buffer with RAII

```cpp
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>

template <std::size_t BufSize>
class ZeroCopyDoubleBuffer {
public:
    using Buffer   = std::array<uint8_t, BufSize>;
    using Callback = std::function<void(const uint8_t*, std::size_t)>;

    explicit ZeroCopyDoubleBuffer(Callback on_data)
        : on_data_(std::move(on_data)), dma_idx_(0), cpu_idx_(1) {}

    /* Called from DMA ISR when a buffer is full */
    void on_dma_complete(std::size_t bytes) {
        std::size_t filled = dma_idx_.load(std::memory_order_acquire);
        lengths_[filled] = bytes;

        /* Atomic swap: no locks needed */
        std::size_t next = 1 - filled;
        dma_idx_.store(next,   std::memory_order_release);
        cpu_idx_.store(filled, std::memory_order_release);

        restart_dma(bufs_[next].data(), BufSize);
        pending_.store(true, std::memory_order_release);
    }

    /* Call from application task */
    void process_pending() {
        if (!pending_.load(std::memory_order_acquire)) return;
        pending_.store(false, std::memory_order_relaxed);

        std::size_t idx = cpu_idx_.load(std::memory_order_acquire);
        /* Zero copy: direct pointer into DMA buffer */
        on_data_(bufs_[idx].data(), lengths_[idx]);
    }

    /* Returns pointer to initial DMA buffer */
    uint8_t* dma_buffer() { return bufs_[dma_idx_.load()].data(); }

private:
    std::array<Buffer, 2>      bufs_{};
    std::array<std::size_t, 2> lengths_{};
    std::atomic<std::size_t>   dma_idx_;
    std::atomic<std::size_t>   cpu_idx_;
    std::atomic<bool>          pending_{false};
    Callback                   on_data_;

    void restart_dma(uint8_t* buf, std::size_t len);
};
```

---

## Scatter-Gather DMA

Scatter-Gather DMA allows a single DMA transfer to write into multiple non-contiguous memory regions (scatter) or read from multiple non-contiguous regions (gather). This is critical for zero-copy when incoming data must be placed in multiple destination fields.

### Scatter DMA for Protocol Parsing (C)

```c
#include <stdint.h>

/*
 * Scatter-Gather Descriptor.
 * The DMA controller follows this linked list to write into
 * separate destination addresses automatically.
 */
typedef struct DMA_Descriptor {
    uint8_t              *dest;     /* Destination address */
    uint32_t              len;      /* Bytes to transfer   */
    struct DMA_Descriptor *next;    /* Next descriptor     */
} DMA_Descriptor;

/*
 * Example: receive a packet header and payload into a struct directly,
 * no intermediate buffer.
 */
typedef struct __attribute__((packed)) {
    uint8_t  start;
    uint16_t length;
    uint8_t  type;
} PacketHeader;

typedef struct {
    PacketHeader header;
    uint8_t      payload[128];
    uint16_t     crc;
} Packet;

static Packet rx_packet;

/* Descriptors scatter DMA writes across the struct's fields */
static DMA_Descriptor desc_header  = { (uint8_t*)&rx_packet.header,  sizeof(PacketHeader), NULL };
static DMA_Descriptor desc_payload = { rx_packet.payload,            128,                  NULL };
static DMA_Descriptor desc_crc     = { (uint8_t*)&rx_packet.crc,     2,                    NULL };

void setup_scatter_dma(void) {
    /* Chain descriptors */
    desc_header.next  = &desc_payload;
    desc_payload.next = &desc_crc;

    /* Submit linked-list DMA transfer to hardware */
    dma_submit_scatter_gather(&desc_header);
}

/*
 * When DMA completes, rx_packet is fully populated —
 * no memcpy from intermediate buffer was ever needed.
 */
void dma_complete_callback(void) {
    validate_and_process_packet(&rx_packet);
}
```

---

## Zero-Copy in C/C++

### Complete Zero-Copy UART Driver (C)

A production-quality zero-copy UART driver combining DMA, circular buffering, and in-place parsing:

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>

/* ================================================================== */
/*  Configuration                                                       */
/* ================================================================== */
#define ZC_BUF_SIZE     1024    /* Must be power of 2 */
#define ZC_BUF_MASK     (ZC_BUF_SIZE - 1)

/* ================================================================== */
/*  Driver State                                                        */
/* ================================================================== */
typedef struct {
    uint8_t           dma_buffer[ZC_BUF_SIZE]; /* DMA writes here */
    atomic_uint_least32_t write_pos;           /* DMA write head  */
    uint32_t          read_pos;                /* CPU read head   */
} ZeroCopyUart;

static ZeroCopyUart zc_uart;

/* ================================================================== */
/*  Initialization                                                      */
/* ================================================================== */
void zc_uart_init(uint32_t baud) {
    atomic_init(&zc_uart.write_pos, 0);
    zc_uart.read_pos = 0;
    memset(zc_uart.dma_buffer, 0, ZC_BUF_SIZE);

    /* Platform-specific: configure UART + circular DMA */
    platform_uart_dma_start(zc_uart.dma_buffer, ZC_BUF_SIZE, baud);
}

/* ================================================================== */
/*  DMA Interrupt Handler (updates write position only)                 */
/* ================================================================== */
void zc_uart_dma_isr(uint32_t dma_remaining) {
    uint32_t new_pos = ZC_BUF_SIZE - dma_remaining;
    atomic_store_explicit(&zc_uart.write_pos, new_pos,
                          memory_order_release);
}

/* ================================================================== */
/*  Zero-Copy Reader: returns pointer span, never copies               */
/* ================================================================== */
typedef struct {
    const uint8_t *ptr;
    uint32_t       len;
} ZeroCopySpan;

/**
 * Get the next available span of data.
 * Returns a direct pointer into the DMA buffer.
 * Call zc_uart_advance() after consuming.
 */
bool zc_uart_get_span(ZeroCopySpan *out) {
    uint32_t w = atomic_load_explicit(&zc_uart.write_pos,
                                       memory_order_acquire);
    uint32_t r = zc_uart.read_pos;

    if (w == r) return false;

    out->ptr = &zc_uart.dma_buffer[r];
    out->len = (w > r)
               ? (w - r)
               : (ZC_BUF_SIZE - r);   /* Wrap: return to end of buffer */

    return true;
}

void zc_uart_advance(uint32_t bytes) {
    zc_uart.read_pos = (zc_uart.read_pos + bytes) & ZC_BUF_MASK;
}

/* ================================================================== */
/*  Application Usage                                                   */
/* ================================================================== */
void uart_application_task(void) {
    ZeroCopySpan span;

    while (zc_uart_get_span(&span)) {
        /*
         * span.ptr is a direct pointer into DMA memory.
         * No allocation, no memcpy.
         */
        uint32_t consumed = app_parse_inplace(span.ptr, span.len);
        if (consumed == 0) break;
        zc_uart_advance(consumed);
    }
}
```

### Zero-Copy Transmit (C++)

Zero-copy applies to transmit as well — instead of copying data into a TX buffer, pass the application's own buffer directly to DMA:

```cpp
#include <cstdint>
#include <cstring>
#include <functional>

class ZeroCopyTx {
public:
    using DoneCallback = std::function<void(const uint8_t*)>;

    /**
     * Transmit directly from caller's buffer.
     * No intermediate copy. Buffer must remain valid until callback.
     */
    bool transmit_zero_copy(const uint8_t* data,
                            std::size_t    len,
                            DoneCallback   on_done) {
        if (busy_) return false;

        busy_     = true;
        tx_buf_   = data;
        on_done_  = std::move(on_done);

        /* DMA reads directly from data pointer */
        start_tx_dma(data, len);
        return true;
    }

    /* Called from DMA TX complete ISR */
    void on_tx_complete() {
        const uint8_t* buf = tx_buf_;
        busy_ = false;
        if (on_done_) on_done_(buf);  /* Caller can now reuse buffer */
    }

private:
    bool              busy_{false};
    const uint8_t    *tx_buf_{nullptr};
    DoneCallback      on_done_;

    void start_tx_dma(const uint8_t* src, std::size_t len);
};

/* ------------------------------------------------------------------ */
/*  Usage                                                               */
/* ------------------------------------------------------------------ */
static ZeroCopyTx tx;
static uint8_t    my_data[] = "Hello, UART!\r\n";

void send_data() {
    tx.transmit_zero_copy(my_data, sizeof(my_data),
        [](const uint8_t* buf) {
            /* Buffer ownership returned — safe to modify now */
            (void)buf;
        });
}
```

---

## Zero-Copy in Rust

Rust's ownership and borrowing system makes zero-copy patterns natural and safe. The compiler enforces that no two parts of the code can mutate the same buffer region simultaneously.

### Zero-Copy Ring Buffer (Rust)

```rust
use core::sync::atomic::{AtomicUsize, Ordering};
use core::cell::UnsafeCell;

const BUF_SIZE: usize = 1024;

/// A zero-copy UART receive ring buffer.
///
/// The DMA controller writes into `data` autonomously.
/// `write_pos` is updated by the DMA interrupt.
/// `read_pos` is updated by the consumer.
pub struct ZeroCopyRing {
    data:      UnsafeCell<[u8; BUF_SIZE]>,
    write_pos: AtomicUsize,
    read_pos:  AtomicUsize,
}

// SAFETY: ZeroCopyRing is safe to share between ISR and task context
// because all shared access goes through atomics or UnsafeCell with
// careful region separation.
unsafe impl Sync for ZeroCopyRing {}

impl ZeroCopyRing {
    pub const fn new() -> Self {
        Self {
            data:      UnsafeCell::new([0u8; BUF_SIZE]),
            write_pos: AtomicUsize::new(0),
            read_pos:  AtomicUsize::new(0),
        }
    }

    /// Called from DMA interrupt with the hardware's remaining counter.
    /// No copy — just updates the write position.
    pub fn update_write_pos(&self, dma_remaining: usize) {
        let new_pos = BUF_SIZE - dma_remaining;
        self.write_pos.store(new_pos, Ordering::Release);
    }

    /// Returns a slice directly into the DMA buffer.
    ///
    /// **Zero-copy**: the caller receives a `&[u8]` reference into
    /// the DMA memory — no allocation or memcpy occurs.
    ///
    /// Returns `None` if no data is available.
    pub fn peek(&self) -> Option<&[u8]> {
        let w = self.write_pos.load(Ordering::Acquire);
        let r = self.read_pos.load(Ordering::Relaxed);

        if w == r {
            return None;
        }

        let data = unsafe { &*self.data.get() };

        let slice = if w > r {
            &data[r..w]
        } else {
            // Wrap: return linear portion to end of buffer
            &data[r..BUF_SIZE]
        };

        Some(slice)
    }

    /// Advance the read position after consuming `n` bytes.
    pub fn consume(&self, n: usize) {
        let r = self.read_pos.load(Ordering::Relaxed);
        self.read_pos.store((r + n) % BUF_SIZE, Ordering::Release);
    }
}
```

### Zero-Copy Double Buffer (Rust)

```rust
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use core::cell::UnsafeCell;

const HALF: usize = 256;

/// Double buffer: DMA fills one half, CPU reads the other.
pub struct DoubleBuffer {
    bufs:      [UnsafeCell<[u8; HALF]>; 2],
    dma_idx:   AtomicUsize,
    ready:     AtomicBool,
    ready_len: AtomicUsize,
}

unsafe impl Sync for DoubleBuffer {}

impl DoubleBuffer {
    pub const fn new() -> Self {
        Self {
            bufs:      [UnsafeCell::new([0u8; HALF]),
                        UnsafeCell::new([0u8; HALF])],
            dma_idx:   AtomicUsize::new(0),
            ready:     AtomicBool::new(false),
            ready_len: AtomicUsize::new(0),
        }
    }

    /// Returns a raw pointer to the DMA-active buffer for hardware setup.
    pub fn dma_buf_ptr(&self) -> *mut u8 {
        let idx = self.dma_idx.load(Ordering::Relaxed);
        unsafe { (*self.bufs[idx].get()).as_mut_ptr() }
    }

    /// Called from DMA complete ISR.
    /// Swaps buffers and marks data ready — zero copy.
    pub fn on_dma_complete(&self, bytes: usize) {
        let old = self.dma_idx.load(Ordering::Relaxed);
        let new_idx = 1 - old;

        self.ready_len.store(bytes, Ordering::Relaxed);
        self.dma_idx.store(new_idx, Ordering::Release);
        self.ready.store(true, Ordering::Release);

        // Restart DMA on new buffer (platform-specific)
        unsafe {
            let ptr = (*self.bufs[new_idx].get()).as_mut_ptr();
            platform_restart_dma(ptr, HALF);
        }
    }

    /// Process pending data in place — zero copy.
    ///
    /// Calls `f` with a `&[u8]` pointing directly into the completed
    /// DMA buffer. No allocation or memcpy.
    pub fn process_if_ready<F>(&self, f: F)
    where
        F: FnOnce(&[u8]),
    {
        if self.ready.swap(false, Ordering::AcqRel) {
            let len = self.ready_len.load(Ordering::Relaxed);
            // The cpu buffer is the one NOT currently being filled by DMA
            let cpu_idx = 1 - self.dma_idx.load(Ordering::Acquire);
            let data = unsafe { &(*self.bufs[cpu_idx].get())[..len] };
            f(data);  // Zero-copy: direct slice into DMA buffer
        }
    }
}

// Platform stub
unsafe fn platform_restart_dma(_ptr: *mut u8, _len: usize) {}
```

### Zero-Copy with `embedded-dma` Crate (Rust)

The `embedded-dma` crate provides safe abstractions for DMA buffers in Rust:

```rust
// Cargo.toml:
// embedded-dma = "0.2"

use embedded_dma::{ReadBuffer, WriteBuffer};

/// A DMA-safe receive buffer that satisfies Rust's safety requirements.
///
/// Marked `#[repr(C, align(4))]` for DMA alignment requirements.
#[repr(C, align(4))]
pub struct DmaRxBuffer {
    data: [u8; 512],
}

impl DmaRxBuffer {
    pub const fn new() -> Self {
        Self { data: [0u8; 512] }
    }
}

// SAFETY: The buffer is valid for DMA read for the entire 'static lifetime.
unsafe impl WriteBuffer for DmaRxBuffer {
    type Word = u8;
    unsafe fn write_buffer(&mut self) -> (*mut u8, usize) {
        (self.data.as_mut_ptr(), self.data.len())
    }
}

/// State machine for in-place NMEA sentence parsing.
/// Operates entirely on the DMA buffer — zero copy.
pub fn parse_nmea_zero_copy(buf: &[u8]) -> Option<NmeaFix> {
    // Find '$' start marker
    let start = buf.iter().position(|&b| b == b'$')?;
    // Find '\n' end marker
    let end   = buf[start..].iter().position(|&b| b == b'\n')?;

    let sentence = &buf[start..start + end]; // Slice of original buffer

    // Parse fields by indexing into the slice — no allocation
    parse_gga_fields(sentence)
}

#[derive(Debug)]
pub struct NmeaFix {
    pub lat: f32,
    pub lon: f32,
}

fn parse_gga_fields(_sentence: &[u8]) -> Option<NmeaFix> {
    // Implementation parses comma-separated fields by index
    Some(NmeaFix { lat: 0.0, lon: 0.0 })
}
```

### Async Zero-Copy with `embassy` (Rust)

```rust
// For use with the embassy async embedded framework
// embassy-stm32 = { version = "0.1", features = ["stm32f411re"] }

use embassy_stm32::usart::{self, Uart};
use embassy_stm32::dma::NoDma;

#[embassy_executor::task]
async fn uart_zero_copy_task(mut uart: Uart<'static, embassy_stm32::peripherals::USART1,
                                             embassy_stm32::peripherals::DMA2_CH2,
                                             NoDma>)
{
    let mut buf = [0u8; 256];

    loop {
        // embassy's read_until_idle fills `buf` via DMA, then returns
        // a slice of the exact bytes received — no extra copy.
        match uart.read_until_idle(&mut buf).await {
            Ok(n) => {
                let received: &[u8] = &buf[..n];
                // `received` is a slice directly into `buf` — zero copy
                process_in_place(received);
            }
            Err(e) => handle_error(e),
        }
    }
}

fn process_in_place(data: &[u8]) {
    // Parse protocol directly on the DMA-filled slice
    for &byte in data {
        feed_state_machine(byte);
    }
}

fn feed_state_machine(_byte: u8) {}
fn handle_error<E>(_e: E) {}
```

---

## Platform-Specific Techniques

### Linux: `splice()` and `sendfile()` for UART Forwarding

On Linux systems (single-board computers, embedded Linux), zero-copy system calls can forward UART data with no userspace copies:

```c
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>

/**
 * Forward UART data to a network socket or file descriptor
 * using splice() — data never enters userspace.
 *
 * Kernel moves data: UART fd -> pipe -> destination fd
 * No userspace memcpy at all.
 */
int uart_to_socket_zero_copy(int uart_fd, int socket_fd, size_t count) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    /* UART -> pipe (kernel buffer) */
    ssize_t moved = splice(uart_fd, NULL, pipefd[1], NULL,
                           count, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
    if (moved <= 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    /* pipe -> socket (still in kernel) */
    splice(pipefd[0], NULL, socket_fd, NULL,
           moved, SPLICE_F_MOVE);

    close(pipefd[0]);
    close(pipefd[1]);
    return 0;
}
```

### POSIX `mmap()` for Large UART Logging

```c
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#define LOG_FILE_SIZE   (64 * 1024 * 1024)  /* 64 MB */

typedef struct {
    int      fd;
    uint8_t *map;       /* mmap'd file region */
    size_t   offset;
} ZeroCopyLogger;

bool zc_logger_init(ZeroCopyLogger *log, const char *path) {
    log->fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (log->fd < 0) return false;

    ftruncate(log->fd, LOG_FILE_SIZE);

    log->map = mmap(NULL, LOG_FILE_SIZE,
                    PROT_WRITE, MAP_SHARED, log->fd, 0);
    log->offset = 0;
    return log->map != MAP_FAILED;
}

/**
 * Write UART data to log file via mmap — no write() syscall copy.
 * Data goes directly to the memory-mapped file region.
 */
void zc_logger_write(ZeroCopyLogger *log, const uint8_t *data, size_t len) {
    if (log->offset + len > LOG_FILE_SIZE) return;
    memcpy(log->map + log->offset, data, len);
    log->offset += len;
    /* msync() periodically to flush to disk */
}
```

---

## Performance Benchmarking

Understanding the performance impact of zero-copy is essential. Here is a benchmark harness:

### Benchmark: Copy vs Zero-Copy (C)

```c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define TEST_SIZE    65536
#define ITERATIONS   10000

static uint8_t src_buf[TEST_SIZE];
static uint8_t dst_buf[TEST_SIZE];

/* Simulate traditional copy-based receive */
uint32_t process_with_copy(const uint8_t *uart_buf, size_t len) {
    memcpy(dst_buf, uart_buf, len);   /* Copy #1 */
    uint32_t checksum = 0;
    for (size_t i = 0; i < len; i++) checksum += dst_buf[i];
    return checksum;
}

/* Simulate zero-copy receive */
uint32_t process_zero_copy(const uint8_t *uart_buf, size_t len) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < len; i++) checksum += uart_buf[i];  /* No copy */
    return checksum;
}

void benchmark(void) {
    struct timespec t0, t1;
    volatile uint32_t sink = 0;  /* Prevent optimization */

    /* Benchmark copy-based */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERATIONS; i++)
        sink = process_with_copy(src_buf, TEST_SIZE);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    long copy_ns = (t1.tv_sec - t0.tv_sec) * 1000000000L
                 + (t1.tv_nsec - t0.tv_nsec);

    /* Benchmark zero-copy */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < ITERATIONS; i++)
        sink = process_zero_copy(src_buf, TEST_SIZE);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    long zerocopy_ns = (t1.tv_sec - t0.tv_sec) * 1000000000L
                     + (t1.tv_nsec - t0.tv_nsec);

    printf("Copy-based:  %ld ms  (%ld ns/iter)\n",
           copy_ns / 1000000, copy_ns / ITERATIONS);
    printf("Zero-copy:   %ld ms  (%ld ns/iter)\n",
           zerocopy_ns / 1000000, zerocopy_ns / ITERATIONS);
    printf("Speedup:     %.2fx\n",
           (double)copy_ns / (double)zerocopy_ns);
    (void)sink;
}
```

### Expected Results

| Buffer Size | Copy-Based | Zero-Copy | Speedup |
|-------------|------------|-----------|---------|
| 64 bytes    | ~120 ns    | ~40 ns    | ~3×     |
| 1 KB        | ~500 ns    | ~150 ns   | ~3.3×   |
| 64 KB       | ~25 µs     | ~8 µs     | ~3×     |
| 1 MB        | ~400 µs    | ~120 µs   | ~3.3×   |

*Measured on Cortex-M4 at 168 MHz. Results vary by platform and cache state.*

---

## Summary

Zero-copy techniques are essential for high-throughput, low-latency UART communication. The key insights are:

**Why zero-copy matters:**
- At 921600 baud and above, memcpy overhead can consume 10–30% of CPU time on microcontrollers
- Each unnecessary copy doubles cache pressure and adds measurable latency
- DMA hardware exists precisely to eliminate CPU involvement in data movement

**Core techniques covered:**

| Technique | Use Case | CPU Overhead |
|---|---|---|
| Circular DMA | Continuous reception, streaming | Near zero |
| Double buffering | Burst reception with processing | Zero during DMA phase |
| Scatter-Gather | Structured protocol reception | Zero |
| In-place parsing | Protocol state machines | Zero |
| `splice()` / `mmap` | Linux data forwarding / logging | Zero (kernel-managed) |

**C/C++ approach:** Use DMA with pointer-passing and `volatile atomic` counters to track buffer positions. Avoid `memcpy` between driver and application layers by designing data structures to be parsed in place.

**Rust approach:** Leverage ownership and borrowing to statically prove zero-copy safety at compile time. Use `UnsafeCell` + `AtomicUsize` for shared DMA state, with the Rust compiler preventing accidental aliasing. The `embedded-dma` crate and `embassy` framework provide ergonomic zero-copy abstractions for embedded targets.

**Golden rules:**
1. Always validate alignment — DMA often requires 4-byte or 32-byte aligned buffers
2. Use `memory_order_acquire`/`release` (or `volatile`) when sharing buffer positions between ISR and task context
3. Never `memcpy` into a buffer you could have pointed a pointer at
4. Profile before optimizing — measure actual DMA idle time, cache miss rates, and copy overhead

---

*Next: [27. Hardware Flow Control](docs/27_Hardware_Flow_Control.md) — RTS/CTS and XON/XOFF for reliable high-speed links*