# 99. UART Buffer Sizing Optimization

**Theory & Methodology** — the byte-arrival-rate formula, minimum buffer sizing math, headroom guidelines by system type, and why power-of-two sizes matter for efficient index arithmetic.

**Scenario walkthroughs** — four concrete examples: a GPS sensor at 9,600 baud, a debug console at 115,200, an industrial Modbus RTU bus, and a high-throughput DMA logger at 921,600 baud — each with worked calculations and a final recommendation.

**C/C++ code** — a full ISR-safe SPSC ring buffer with atomic head/tail indices, TX interrupt feeding, a DMA double-buffer (ping-pong) pattern, and a `constexpr` compile-time sizing helper.

**Rust code** — a lock-free `RingBuffer<const SIZE: usize>` using const generics (size validated at compile time via `assert!`), a `UartDriver<RX, TX>` struct that wraps both buffers with diagnostic counters, adaptive runtime resizing via `VecDeque`, and a `heapless::spsc::Queue` example for `no_std` targets.

**Diagnostics & Pitfalls** — overflow/underrun counters, high-watermark tracking, and the six most common mistakes (non-power-of-two sizing, missing memory barriers, ignoring hardware FIFOs, OS scheduling jitter on Linux, etc.).


> **Topic:** Determining optimal buffer sizes for different scenarios  
> **Category:** UART Programming — Advanced Topics  
> **Languages:** C/C++ · Rust

---

## Table of Contents

1. [Introduction](#introduction)
2. [Theoretical Foundations](#theoretical-foundations)
3. [Buffer Types in UART Systems](#buffer-types-in-uart-systems)
4. [Sizing Formulas and Methodology](#sizing-formulas-and-methodology)
5. [Scenario-Based Sizing Strategies](#scenario-based-sizing-strategies)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Dynamic Buffer Management](#dynamic-buffer-management)
9. [Diagnostics and Tuning](#diagnostics-and-tuning)
10. [Common Pitfalls](#common-pitfalls)
11. [Summary](#summary)

---

## Introduction

Buffer sizing is one of the most consequential — and most frequently mishandled — decisions in UART driver design. A buffer that is too small causes data loss, overflow errors, and unstable communication. A buffer that is too large wastes precious RAM, can introduce latency, and on microcontrollers may crowd out other critical allocations.

Unlike higher-level protocols where the OS manages memory dynamically, UART buffers — especially on embedded targets — are typically statically allocated. Getting the size right at design time therefore requires a disciplined analysis of:

- **Baud rate** and the resulting byte arrival rate
- **Application latency** — how long can the software delay before reading the buffer?
- **Message framing** — fixed packets, variable-length frames, or raw streams
- **Peak traffic** vs. average traffic patterns
- **Hardware FIFO depth** on the UART peripheral itself
- **Available RAM** and competing allocations

This document covers the full methodology: from first principles through production-ready C/C++ and Rust implementations.

---

## Theoretical Foundations

### Byte Arrival Rate

At a given baud rate, each byte on a standard 8N1 UART (8 data bits, no parity, 1 stop bit) consumes **10 bit-times** (1 start + 8 data + 1 stop).

```
bytes_per_second = baud_rate / 10
```

| Baud Rate   | Bytes/sec | Bytes/ms | Bytes/µs |
|-------------|-----------|----------|----------|
| 9,600       | 960       | 0.96     | 0.00096  |
| 115,200     | 11,520    | 11.52    | 0.01152  |
| 460,800     | 46,080    | 46.08    | 0.04608  |
| 921,600     | 92,160    | 92.16    | 0.09216  |
| 1,000,000   | 100,000   | 100.00   | 0.10000  |
| 3,000,000   | 300,000   | 300.00   | 0.30000  |

### Minimum Buffer Size

The absolute minimum RX buffer must hold all bytes that can arrive during the **worst-case scheduling latency** of your application — the longest time the MCU or OS can delay before servicing the UART:

```
min_rx_buffer = ceil(bytes_per_second × max_latency_seconds)
```

For safety, apply a headroom factor (typically 2×–4×):

```
safe_rx_buffer = min_rx_buffer × headroom_factor
```

### TX Buffer

The TX buffer must hold at least one complete outgoing message (or burst), so the application thread can hand off data and return immediately without blocking:

```
min_tx_buffer = max_single_message_bytes
```

For throughput-optimised systems with bursty output, size it to hold the largest burst plus inter-burst spacing.

---

## Buffer Types in UART Systems

### 1. Hardware FIFO

Most modern UART peripherals (STM32, NXP, ESP32, Nordic, FTDI chips, 16550-compatible UARTs) include a small hardware FIFO — typically 8, 16, 32, or 64 bytes — that gives the CPU extra time before the first interrupt fires or before overflow. Always consult your specific peripheral datasheet.

```
effective_software_latency_budget = hardware_fifo_depth / bytes_per_second + software_latency
```

### 2. Software Circular (Ring) Buffer

The standard approach: a fixed-size, statically allocated ring buffer where the UART ISR writes and the application reads (or vice versa for TX). Thread-safety is achieved with critical sections, atomic head/tail indices, or lock-free techniques.

### 3. Double Buffer (Ping-Pong)

Two equal-sized buffers alternating between "filling" and "processing" roles. Eliminates contention at the cost of 2× memory. Especially useful for DMA-based transfers.

### 4. DMA Scatter-Gather

Used in high-throughput systems; the hardware writes directly to a memory region with no CPU involvement until a completion interrupt fires. Buffer sizing here depends on DMA transfer unit and application processing rate.

---

## Sizing Formulas and Methodology

### Step-by-Step Process

```
1. Determine baud_rate and frame format (bits per byte)
2. Compute bytes_per_second = baud_rate / bits_per_byte
3. Measure or estimate max_software_latency (ms)
4. Compute raw_min = bytes_per_second × max_software_latency / 1000
5. Account for hardware FIFO: effective_min = max(raw_min - hw_fifo, 1)
6. Apply headroom: buffer_size = next_power_of_two(effective_min × headroom)
7. Validate against available RAM
8. Profile under realistic load and adjust
```

### Why Power-of-Two?

Ring buffer index wrapping is computed as `index & (size - 1)` — a single AND instruction — rather than `index % size`, which on small MCUs requires a division. Always round up to the next power of two unless memory is extremely constrained.

### Headroom Guidelines

| System Type              | Headroom Factor |
|--------------------------|-----------------|
| Bare-metal, polled       | 4×–8×           |
| Bare-metal, ISR-driven   | 2×–4×           |
| RTOS with deterministic scheduling | 2×  |
| Linux/POSIX (userspace)  | 1.5×–2×         |
| High-reliability / safety-critical | 8×+   |

---

## Scenario-Based Sizing Strategies

### Scenario A: Low-Rate Sensor (e.g., GPS NMEA @ 9,600 baud)

- Byte rate: 960 bytes/s
- Typical message: 80–100 bytes (one NMEA sentence)
- Application reads every 100 ms

```
raw_min = 960 × 0.100 = 96 bytes
headroom × 2 = 192 bytes → round up to 256 bytes
```

**Recommended RX buffer: 256 bytes**

### Scenario B: High-Speed Debug Console (115,200 baud)

- Byte rate: 11,520 bytes/s
- RTOS task period: 10 ms
- Hardware FIFO: 16 bytes

```
raw_min = 11,520 × 0.010 = 115.2 bytes
effective = 115.2 - 16 = 99.2 → 100 bytes
× 2 headroom = 200 → round to 256 bytes
```

**Recommended RX buffer: 256 bytes, TX buffer: 512 bytes**

### Scenario C: Industrial Modbus RTU (RS-485 @ 115,200 baud)

- Max frame: 256 bytes (Modbus RTU limit)
- Inter-frame gap enforced by protocol (3.5 char times ≈ 0.3 ms @ 115,200)
- Application processes one frame at a time

```
RX buffer ≥ 2 × max_frame = 512 bytes (double-buffer for pipeline)
TX buffer ≥ max_frame = 256 bytes → round to 256
```

**Recommended RX buffer: 512 bytes, TX buffer: 256 bytes**

### Scenario D: High-Throughput Logging (921,600 baud)

- Byte rate: 92,160 bytes/s
- DMA block size: 1,024 bytes
- Target: zero-loss logging

```
DMA buffer = 2 × 1024 = 2,048 bytes (double-buffer)
Software ring buffer behind DMA: 8,192 bytes
```

**Recommended: 2 KB DMA ping-pong + 8 KB software ring buffer**

---

## C/C++ Implementation

### Core Ring Buffer Structure

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>

/* Buffer size MUST be a power of two */
#define UART_RX_BUFFER_SIZE  256U
#define UART_TX_BUFFER_SIZE  512U

#define BUFFER_MASK(size)  ((size) - 1U)

/* Compile-time assertion: size is power of two */
_Static_assert((UART_RX_BUFFER_SIZE & (UART_RX_BUFFER_SIZE - 1)) == 0,
               "RX buffer size must be a power of two");
_Static_assert((UART_TX_BUFFER_SIZE & (UART_TX_BUFFER_SIZE - 1)) == 0,
               "TX buffer size must be a power of two");

typedef struct {
    uint8_t  data[UART_RX_BUFFER_SIZE];
    volatile uint32_t head;   /* written by ISR  */
    volatile uint32_t tail;   /* read  by app    */
    uint32_t overflow_count;
} uart_rx_ring_t;

typedef struct {
    uint8_t  data[UART_TX_BUFFER_SIZE];
    volatile uint32_t head;   /* read  by ISR    */
    volatile uint32_t tail;   /* written by app  */
} uart_tx_ring_t;

/* Global instances */
static uart_rx_ring_t g_uart_rx;
static uart_tx_ring_t g_uart_tx;
```

### ISR-Safe Write (RX side, called from interrupt)

```c
/**
 * @brief  Insert one byte into the RX ring buffer.
 *         Called from UART RX interrupt — must be ISR-safe.
 * @return true  byte was stored
 * @return false buffer was full (byte dropped, overflow counter incremented)
 */
static inline bool uart_rx_isr_put(uart_rx_ring_t *rb, uint8_t byte)
{
    uint32_t next_head = (rb->head + 1U) & BUFFER_MASK(UART_RX_BUFFER_SIZE);

    if (next_head == rb->tail) {
        /* Buffer full — drop byte, record overflow */
        rb->overflow_count++;
        return false;
    }

    rb->data[rb->head] = byte;
    /* Store barrier: ensure data is written before head advances */
    __atomic_store_n(&rb->head, next_head, __ATOMIC_RELEASE);
    return true;
}

/**
 * @brief  Read one byte from the RX ring buffer.
 *         Called from application context.
 * @return true  byte available, stored in *out
 * @return false buffer empty
 */
static inline bool uart_rx_get(uart_rx_ring_t *rb, uint8_t *out)
{
    uint32_t tail = __atomic_load_n(&rb->tail, __ATOMIC_ACQUIRE);

    if (tail == rb->head) {
        return false;  /* empty */
    }

    *out = rb->data[tail];
    __atomic_store_n(&rb->tail,
                     (tail + 1U) & BUFFER_MASK(UART_RX_BUFFER_SIZE),
                     __ATOMIC_RELEASE);
    return true;
}

/** @brief Number of bytes available to read */
static inline uint32_t uart_rx_available(const uart_rx_ring_t *rb)
{
    return (rb->head - rb->tail) & BUFFER_MASK(UART_RX_BUFFER_SIZE);
}

/** @brief Free space remaining */
static inline uint32_t uart_rx_free(const uart_rx_ring_t *rb)
{
    return UART_RX_BUFFER_SIZE - 1U - uart_rx_available(rb);
}
```

### TX Side

```c
/**
 * @brief  Queue one byte for transmission.
 *         Called from application context. Enables TX interrupt on first byte.
 */
bool uart_tx_put(uart_tx_ring_t *rb, uint8_t byte)
{
    uint32_t next_tail = (rb->tail + 1U) & BUFFER_MASK(UART_TX_BUFFER_SIZE);

    if (next_tail == rb->head) {
        return false;  /* buffer full */
    }

    rb->data[rb->tail] = byte;
    __atomic_store_n(&rb->tail, next_tail, __ATOMIC_RELEASE);

    /* Enable TX-empty interrupt (hardware-specific) */
    UART_ENABLE_TXE_IRQ();
    return true;
}

/**
 * @brief  Write a block of bytes into the TX buffer.
 * @return Number of bytes actually queued (may be less than len if buffer fills)
 */
size_t uart_tx_write(uart_tx_ring_t *rb, const uint8_t *data, size_t len)
{
    size_t written = 0;
    while (written < len && uart_tx_put(rb, data[written])) {
        written++;
    }
    return written;
}

/**
 * @brief  TX-empty ISR handler: feeds next byte to UART data register.
 */
void UART_TXE_IRQHandler(void)
{
    uart_tx_ring_t *rb = &g_uart_tx;
    uint32_t head = __atomic_load_n(&rb->head, __ATOMIC_ACQUIRE);

    if (head == rb->tail) {
        /* No more data — disable TX-empty interrupt */
        UART_DISABLE_TXE_IRQ();
        return;
    }

    UART_DR = rb->data[head];  /* Send byte */
    __atomic_store_n(&rb->head,
                     (head + 1U) & BUFFER_MASK(UART_TX_BUFFER_SIZE),
                     __ATOMIC_RELEASE);
}
```

### Dynamic Buffer Sizing Helper (C++)

```cpp
#include <cstddef>
#include <cmath>

namespace uart_util {

/**
 * @brief Compute the optimal ring buffer size for a given scenario.
 *
 * @param baud_rate        UART baud rate (bits/sec)
 * @param bits_per_frame   Total bit-times per byte (e.g. 10 for 8N1)
 * @param max_latency_ms   Worst-case software latency before buffer is read
 * @param hw_fifo_depth    Hardware FIFO depth (bytes), 0 if none
 * @param headroom_factor  Safety multiplier (recommend 2.0 – 4.0)
 * @return                 Recommended buffer size (next power of two, >= 16)
 */
constexpr size_t compute_buffer_size(
    uint32_t baud_rate,
    uint32_t bits_per_frame,
    float    max_latency_ms,
    uint32_t hw_fifo_depth,
    float    headroom_factor)
{
    float bytes_per_ms  = static_cast<float>(baud_rate) /
                          static_cast<float>(bits_per_frame) / 1000.0f;
    float raw_min       = bytes_per_ms * max_latency_ms;
    float effective_min = raw_min - static_cast<float>(hw_fifo_depth);
    if (effective_min < 1.0f) effective_min = 1.0f;
    float with_headroom = effective_min * headroom_factor;
    size_t size = static_cast<size_t>(std::ceil(with_headroom));

    /* Round up to next power of two, minimum 16 */
    if (size < 16) size = 16;
    size_t pow2 = 16;
    while (pow2 < size) pow2 <<= 1;
    return pow2;
}

/* Example: GPS sensor at 9,600 baud, polled every 50 ms, no HW FIFO */
constexpr size_t GPS_RX_BUF = compute_buffer_size(9600, 10, 50.0f, 0, 3.0f);
// GPS_RX_BUF == 256

/* Example: Industrial RS-485 @ 115,200 baud, RTOS 10 ms task, 16-byte FIFO */
constexpr size_t RS485_RX_BUF = compute_buffer_size(115200, 10, 10.0f, 16, 2.0f);
// RS485_RX_BUF == 256

} // namespace uart_util
```

### DMA Double-Buffer Pattern (C, STM32-style)

```c
#define DMA_HALF_SIZE   512U
#define DMA_FULL_SIZE   (2U * DMA_HALF_SIZE)

static uint8_t  g_dma_rx_buf[DMA_FULL_SIZE];
static uint8_t  g_app_buf[4096U];
static uint32_t g_app_buf_head = 0;
static uint32_t g_app_buf_tail = 0;

/* Called on DMA Half-Transfer interrupt */
void DMA_HalfTransfer_IRQHandler(void)
{
    /* First half is full — copy to app ring buffer */
    memcpy(&g_app_buf[g_app_buf_head], g_dma_rx_buf, DMA_HALF_SIZE);
    g_app_buf_head = (g_app_buf_head + DMA_HALF_SIZE) % sizeof(g_app_buf);
}

/* Called on DMA Full-Transfer interrupt */
void DMA_FullTransfer_IRQHandler(void)
{
    /* Second half is full — copy to app ring buffer */
    memcpy(&g_app_buf[g_app_buf_head],
           g_dma_rx_buf + DMA_HALF_SIZE,
           DMA_HALF_SIZE);
    g_app_buf_head = (g_app_buf_head + DMA_HALF_SIZE) % sizeof(g_app_buf);
}
```

---

## Rust Implementation

### Lock-Free Ring Buffer

```rust
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicUsize, Ordering};

/// A statically-allocated, lock-free SPSC ring buffer for UART use.
/// SIZE must be a power of two.
pub struct RingBuffer<const SIZE: usize> {
    buf:  UnsafeCell<[u8; SIZE]>,
    head: AtomicUsize,   // writer index (ISR writes, app reads for RX)
    tail: AtomicUsize,   // reader index
}

// SAFETY: We use atomic indices for synchronisation;
// only one producer and one consumer at a time (SPSC).
unsafe impl<const SIZE: usize> Sync for RingBuffer<SIZE> {}

impl<const SIZE: usize> RingBuffer<SIZE> {
    const MASK: usize = SIZE - 1;

    // Compile-time check that SIZE is a power of two
    const _ASSERT_POWER_OF_TWO: () = assert!(
        SIZE.is_power_of_two(),
        "RingBuffer SIZE must be a power of two"
    );

    pub const fn new() -> Self {
        Self {
            buf:  UnsafeCell::new([0u8; SIZE]),
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    /// Write one byte — called from producer context (e.g. ISR).
    /// Returns `Err(())` if the buffer is full.
    #[inline]
    pub fn write(&self, byte: u8) -> Result<(), ()> {
        let head = self.head.load(Ordering::Relaxed);
        let next = (head + 1) & Self::MASK;

        if next == self.tail.load(Ordering::Acquire) {
            return Err(()); // full
        }

        // SAFETY: head is only modified by this (producer) side;
        // no aliasing with the reader.
        unsafe {
            (*self.buf.get())[head] = byte;
        }
        self.head.store(next, Ordering::Release);
        Ok(())
    }

    /// Read one byte — called from consumer context (e.g. application task).
    /// Returns `None` if the buffer is empty.
    #[inline]
    pub fn read(&self) -> Option<u8> {
        let tail = self.tail.load(Ordering::Relaxed);

        if tail == self.head.load(Ordering::Acquire) {
            return None; // empty
        }

        // SAFETY: tail is only modified by this (consumer) side.
        let byte = unsafe { (*self.buf.get())[tail] };
        self.tail.store((tail + 1) & Self::MASK, Ordering::Release);
        Some(byte)
    }

    /// Number of bytes available to read.
    #[inline]
    pub fn available(&self) -> usize {
        let head = self.head.load(Ordering::Acquire);
        let tail = self.tail.load(Ordering::Acquire);
        (head.wrapping_sub(tail)) & Self::MASK
    }

    /// Free space remaining.
    #[inline]
    pub fn free(&self) -> usize {
        SIZE - 1 - self.available()
    }

    /// Read a contiguous slice of up to `len` bytes into `dst`.
    /// Returns the number of bytes actually read.
    pub fn read_slice(&self, dst: &mut [u8]) -> usize {
        let mut count = 0;
        for slot in dst.iter_mut() {
            match self.read() {
                Some(b) => { *slot = b; count += 1; }
                None    => break,
            }
        }
        count
    }
}
```

### Static Buffer Instances with Computed Sizes

```rust
/// Compute optimal buffer size at compile time.
///
/// Returns the next power-of-two >= (baud_rate / bits_per_frame * latency_ms / 1000
/// * headroom), with a minimum of 16.
const fn optimal_buffer_size(
    baud_rate:      u32,
    bits_per_frame: u32,
    latency_ms:     u32,
    hw_fifo:        u32,
    headroom:       u32,   // integer, e.g. 2 for 2×
) -> usize {
    let bytes_per_ms: u32 = baud_rate / bits_per_frame / 1000;
    let raw_min: u32 = bytes_per_ms * latency_ms;
    let effective = if raw_min > hw_fifo { raw_min - hw_fifo } else { 1 };
    let with_headroom = effective * headroom;
    let mut size: usize = if with_headroom < 16 { 16 } else { with_headroom as usize };

    // Round up to next power of two
    let mut pow2: usize = 16;
    while pow2 < size { pow2 <<= 1; }
    pow2
}

// Sensor UART: 9,600 baud, 50 ms polling, no HW FIFO, 3× headroom
const SENSOR_RX_SIZE: usize = optimal_buffer_size(9_600, 10, 50, 0, 3);
// → 256

// High-speed UART: 460,800 baud, 5 ms latency, 32-byte FIFO, 2× headroom
const HIGHSPEED_RX_SIZE: usize = optimal_buffer_size(460_800, 10, 5, 32, 2);
// → 512

// Static buffer instances
static SENSOR_RX:    RingBuffer<SENSOR_RX_SIZE>    = RingBuffer::new();
static HIGHSPEED_RX: RingBuffer<HIGHSPEED_RX_SIZE> = RingBuffer::new();
```

### UART Driver Struct with Configurable Buffers

```rust
use core::sync::atomic::{AtomicU32, Ordering};

pub struct UartDriver<const RX: usize, const TX: usize> {
    rx_buf:         RingBuffer<RX>,
    tx_buf:         RingBuffer<TX>,
    rx_overflow:    AtomicU32,
    tx_underrun:    AtomicU32,
}

impl<const RX: usize, const TX: usize> UartDriver<RX, TX> {
    pub const fn new() -> Self {
        Self {
            rx_buf:      RingBuffer::new(),
            tx_buf:      RingBuffer::new(),
            rx_overflow: AtomicU32::new(0),
            tx_underrun: AtomicU32::new(0),
        }
    }

    /// Called from RX interrupt.
    #[inline]
    pub fn isr_rx_byte(&self, byte: u8) {
        if self.rx_buf.write(byte).is_err() {
            self.rx_overflow.fetch_add(1, Ordering::Relaxed);
        }
    }

    /// Called from TX-empty interrupt.
    /// Returns the next byte to transmit, or `None` to disable the interrupt.
    #[inline]
    pub fn isr_tx_next(&self) -> Option<u8> {
        let b = self.tx_buf.read();
        if b.is_none() {
            self.tx_underrun.fetch_add(1, Ordering::Relaxed);
        }
        b
    }

    /// Application: read one received byte.
    pub fn read(&self) -> Option<u8> {
        self.rx_buf.read()
    }

    /// Application: read up to `n` bytes.
    pub fn read_slice(&self, dst: &mut [u8]) -> usize {
        self.rx_buf.read_slice(dst)
    }

    /// Application: enqueue a byte for transmission.
    pub fn write(&self, byte: u8) -> Result<(), ()> {
        self.tx_buf.write(byte)
    }

    /// Application: enqueue a slice for transmission.
    pub fn write_slice(&self, src: &[u8]) -> usize {
        let mut n = 0;
        for &b in src {
            if self.tx_buf.write(b).is_err() { break; }
            n += 1;
        }
        n
    }

    /// Diagnostic counters.
    pub fn overflow_count(&self)  -> u32 { self.rx_overflow.load(Ordering::Relaxed) }
    pub fn underrun_count(&self)  -> u32 { self.tx_underrun.load(Ordering::Relaxed) }
    pub fn rx_available(&self)    -> usize { self.rx_buf.available() }
    pub fn tx_free(&self)         -> usize { self.tx_buf.free() }
}

// Instantiate a driver for the sensor UART
static SENSOR_UART: UartDriver<SENSOR_RX_SIZE, 256> = UartDriver::new();
```

### Using `heapless` for No-std Environments

```rust
// In Cargo.toml:
// heapless = "0.8"

use heapless::spsc::{Queue, Producer, Consumer};

const HEAPLESS_RX_SIZE: usize = 256;

// Static queue — heapless handles the SPSC safety guarantees
static mut UART_QUEUE: Queue<u8, HEAPLESS_RX_SIZE> = Queue::new();

fn init_uart_queue() -> (
    Producer<'static, u8, HEAPLESS_RX_SIZE>,
    Consumer<'static, u8, HEAPLESS_RX_SIZE>,
) {
    // SAFETY: called once at startup before ISRs are enabled
    unsafe { UART_QUEUE.split() }
}
```

---

## Dynamic Buffer Management

For Linux/POSIX environments or embedded systems with a heap, buffers can be sized at runtime based on measured conditions.

### Adaptive Sizing in C (POSIX)

```c
#include <stdlib.h>
#include <termios.h>
#include <math.h>

typedef struct {
    uint8_t *data;
    size_t   capacity;
    size_t   head;
    size_t   tail;
} dynamic_ring_t;

/**
 * @brief  Allocate a dynamic ring buffer sized for the given baud rate and latency.
 *
 * @param baud_rate       UART baud rate
 * @param max_latency_ms  Worst-case latency (ms)
 * @param headroom        Safety factor (e.g. 2.0)
 */
dynamic_ring_t *uart_buf_create(uint32_t baud_rate,
                                float    max_latency_ms,
                                float    headroom)
{
    float bytes_per_ms = (float)baud_rate / 10.0f / 1000.0f;
    float raw          = bytes_per_ms * max_latency_ms * headroom;
    size_t size        = 16;
    while (size < (size_t)ceilf(raw)) size <<= 1;

    dynamic_ring_t *rb = malloc(sizeof(*rb));
    if (!rb) return NULL;

    rb->data = malloc(size);
    if (!rb->data) { free(rb); return NULL; }

    rb->capacity = size;
    rb->head = rb->tail = 0;
    return rb;
}

void uart_buf_destroy(dynamic_ring_t *rb)
{
    if (rb) { free(rb->data); free(rb); }
}

/** Write a byte; returns false if full. */
bool uart_buf_write(dynamic_ring_t *rb, uint8_t byte)
{
    size_t next = (rb->head + 1) & (rb->capacity - 1);
    if (next == rb->tail) return false;
    rb->data[rb->head] = byte;
    rb->head = next;
    return true;
}

/** Read a byte; returns false if empty. */
bool uart_buf_read(dynamic_ring_t *rb, uint8_t *out)
{
    if (rb->tail == rb->head) return false;
    *out = rb->data[rb->tail];
    rb->tail = (rb->tail + 1) & (rb->capacity - 1);
    return true;
}
```

### Adaptive Resizing in Rust

```rust
use std::collections::VecDeque;

pub struct AdaptiveUartBuffer {
    inner:       VecDeque<u8>,
    capacity:    usize,
    overflow:    u64,
    high_water:  usize,
}

impl AdaptiveUartBuffer {
    pub fn new(initial_capacity: usize) -> Self {
        Self {
            inner:      VecDeque::with_capacity(initial_capacity),
            capacity:   initial_capacity,
            overflow:   0,
            high_water: 0,
        }
    }

    pub fn push(&mut self, byte: u8) {
        if self.inner.len() >= self.capacity {
            self.overflow += 1;
            return;
        }
        self.inner.push_back(byte);
        if self.inner.len() > self.high_water {
            self.high_water = self.inner.len();
        }
    }

    pub fn pop(&mut self) -> Option<u8> {
        self.inner.pop_front()
    }

    /// Recommend a new capacity based on observed high watermark.
    /// Returns `Some(new_size)` if resizing is advisable.
    pub fn recommend_resize(&self) -> Option<usize> {
        let utilisation = self.high_water as f64 / self.capacity as f64;
        if utilisation > 0.75 {
            // High utilisation: double the capacity
            Some(self.capacity * 2)
        } else if utilisation < 0.25 && self.capacity > 64 {
            // Low utilisation: halve the capacity (minimum 64)
            Some((self.capacity / 2).max(64))
        } else {
            None
        }
    }

    pub fn apply_resize(&mut self, new_capacity: usize) {
        self.capacity = new_capacity;
        self.inner.reserve(new_capacity.saturating_sub(self.inner.len()));
    }

    pub fn diagnostics(&self) -> BufferDiagnostics {
        BufferDiagnostics {
            capacity:    self.capacity,
            used:        self.inner.len(),
            high_water:  self.high_water,
            overflow:    self.overflow,
            utilisation: self.high_water as f64 / self.capacity as f64,
        }
    }
}

#[derive(Debug)]
pub struct BufferDiagnostics {
    pub capacity:    usize,
    pub used:        usize,
    pub high_water:  usize,
    pub overflow:    u64,
    pub utilisation: f64,
}
```

---

## Diagnostics and Tuning

Effective buffer sizing is an empirical process. Always instrument your driver.

### Key Metrics to Track

| Metric               | Meaning                                      | Action if Non-Zero / High  |
|----------------------|----------------------------------------------|----------------------------|
| `rx_overflow_count`  | ISR dropped bytes due to full buffer         | Increase RX buffer or reduce latency |
| `tx_underrun_count`  | TX ISR fired but buffer was empty            | Increase TX buffer or batch writes   |
| `rx_high_watermark`  | Peak fill level (% of capacity)              | If >75%, consider doubling size      |
| `mean_fill_level`    | Average fill level                           | If <10%, buffer may be oversized     |
| `max_latency_ms`     | Measured worst-case read latency             | Re-run sizing formula with real data |

### C Diagnostics Snippet

```c
typedef struct {
    uint32_t rx_overflow;
    uint32_t tx_underrun;
    uint32_t rx_high_watermark;
    uint32_t rx_reads;
    uint64_t rx_bytes_total;
} uart_stats_t;

static uart_stats_t g_stats;

/* In your application polling loop: */
void uart_poll(void)
{
    uint32_t avail = uart_rx_available(&g_uart_rx);

    /* Track high watermark */
    if (avail > g_stats.rx_high_watermark) {
        g_stats.rx_high_watermark = avail;
    }

    uint8_t byte;
    while (uart_rx_get(&g_uart_rx, &byte)) {
        process_byte(byte);
        g_stats.rx_bytes_total++;
    }
    g_stats.rx_reads++;

    /* Log if overflow detected */
    if (g_uart_rx.overflow_count != g_stats.rx_overflow) {
        g_stats.rx_overflow = g_uart_rx.overflow_count;
        log_warning("UART RX overflow! Count=%u HWM=%u/%u",
                    g_stats.rx_overflow,
                    g_stats.rx_high_watermark,
                    UART_RX_BUFFER_SIZE);
    }
}
```

---

## Common Pitfalls

### 1. Non-Power-of-Two Buffer Size

Using modulo (`%`) for index wrapping introduces variable latency due to integer division, and prevents many compiler optimisations. **Always use power-of-two sizes.**

```c
/* WRONG — slow and error-prone */
head = (head + 1) % buffer_size;

/* CORRECT — single AND instruction */
head = (head + 1) & (BUFFER_SIZE - 1);
```

### 2. Forgetting the Hardware FIFO

Many developers size the software buffer without accounting for the peripheral's FIFO, leading to buffers that are larger than necessary. Read your datasheet.

### 3. Sharing Buffer Between ISR and Application Without Barriers

On CPUs with out-of-order execution or write buffering (Cortex-M3/M4 and above, x86), the compiler or CPU can reorder memory accesses. Always use memory barriers or atomic operations for the index variables.

### 4. Oversizing the TX Buffer

A very large TX buffer does not increase throughput — it just hides latency. If you need to send 128 bytes at 115,200 baud, a 128-byte TX buffer is sufficient; a 4 KB buffer wastes RAM with no benefit.

### 5. Not Testing Under Peak Load

Buffers often look fine in normal operation but overflow during burst traffic (boot messages, error dumps, sensor spikes). Always stress-test at maximum expected data rate.

### 6. Ignoring OS Scheduling Jitter (Linux/POSIX)

On Linux, even with a real-time kernel, scheduling latency can spike to 10–100 ms under load. Use `cyclictest` to measure your actual worst-case latency and size accordingly — do not assume 1–2 ms.

---

## Summary

Optimal UART buffer sizing is a quantitative discipline, not guesswork. The core methodology is:

1. **Compute byte arrival rate** from baud rate and frame format.
2. **Measure worst-case scheduling latency** — empirically, not assumed.
3. **Subtract hardware FIFO depth** from the raw requirement.
4. **Apply a headroom factor** appropriate to your scheduling model.
5. **Round up to the next power of two** for efficient ring buffer arithmetic.
6. **Instrument and verify** — track overflow counts, high-watermarks, and latency under real load.

For **C/C++**, statically-allocated ring buffers with atomic index operations are the standard pattern. Compile-time `_Static_assert` (C11) or `static_assert` (C++) enforce the power-of-two constraint at build time. For DMA-based high-throughput systems, use a double-buffer (ping-pong) scheme sized to the DMA transfer unit.

For **Rust**, the `const fn` sizing helper and const generics (`RingBuffer<const SIZE: usize>`) allow the buffer to be sized and validated entirely at compile time, with zero runtime overhead. The `heapless::spsc::Queue` crate provides a battle-tested SPSC queue for `no_std` environments.

Across both languages, the key principle is the same: **a buffer is a time-domain resource**. Its size represents the maximum time the system can tolerate between data arrivals and data consumption. Design it based on real timing measurements, verify it under peak load, and monitor it in production.

---

*Document: UART Programming Series — Topic 99 of 100*  
*Generated for embedded systems and systems programming reference.*