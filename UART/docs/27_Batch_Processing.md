# 27. UART Batch Processing

**Concept & Theory** — explains why per-byte interrupts become a bottleneck at higher baud rates, with a concrete cycle-cost breakdown, and introduces the four main batch triggering strategies (size threshold, idle timeout, periodic timer, DMA events).

**C/C++ Examples** — five progressively complete code samples:
1. A lock-free SPSC ring buffer with memory barriers
2. TX batching using UART FIFO threshold interrupts (STM32 HAL style)
3. RX batching using circular DMA + idle line detection, with wrap-around handling
4. A timer-driven periodic flush for streaming use cases
5. A templated C++ `UartBatchDriver` class using `std::atomic`

**Rust Examples** — four examples:
1. A `no_std` lock-free `RingBuf<const N>` using `AtomicUsize` and `UnsafeCell`
2. Bare-metal ISR integration sharing static ring buffers between interrupt and app context
3. Embassy async `read_until_idle()` for DMA+idle-line batching with zero boilerplate
4. A `UartBatchWriter` with configurable auto-flush threshold

**Tuning Guide** — formulas for choosing batch size, idle timeout, and buffer depths based on baud rate and CPU clock.

## Grouping Transfers to Reduce Interrupt Overhead

---

## Table of Contents

1. [Introduction](#introduction)
2. [The Interrupt Overhead Problem](#the-interrupt-overhead-problem)
3. [Batch Processing Concepts](#batch-processing-concepts)
4. [Transmit Batching](#transmit-batching)
5. [Receive Batching](#receive-batching)
6. [DMA-Based Batch Transfers](#dma-based-batch-transfers)
7. [Ring Buffer Architecture](#ring-buffer-architecture)
8. [Implementation in C/C++](#implementation-in-cc)
9. [Implementation in Rust](#implementation-in-rust)
10. [Tuning Batch Parameters](#tuning-batch-parameters)
11. [Summary](#summary)

---

## Introduction

In UART communication, each byte transferred can, in the simplest configuration, trigger a CPU interrupt. On low-speed links or systems with modest throughput, this is acceptable. However, as data rates or packet sizes grow, the overhead of handling one interrupt per byte becomes a significant performance bottleneck. **Batch processing** is the technique of grouping multiple data bytes into a single transfer unit — deferring or consolidating interrupt handling so the CPU is interrupted far less frequently while still maintaining throughput and responsiveness.

Batch processing applies to both transmit (TX) and receive (RX) paths:

- **TX batching**: Fill a hardware FIFO or DMA buffer with many bytes at once, then let the hardware drain it while the CPU is free.
- **RX batching**: Use hardware FIFOs, DMA, or timer-based triggers to collect several incoming bytes before notifying the CPU.

---

## The Interrupt Overhead Problem

Every interrupt incurs cost:

| Cost Component | Typical Duration |
|---|---|
| IRQ latency (pipeline flush, exception entry) | 12–100 CPU cycles |
| Context save/restore (ISR prologue/epilogue) | 10–50 cycles |
| ISR body (read FIFO, write buffer, clear flag) | 20–100 cycles |
| Return from interrupt, pipeline refill | 10–30 cycles |

On a Cortex-M4 at 168 MHz handling a 115200 baud UART, a byte arrives approximately every **86 µs** (~14,476 bytes/sec). If each byte triggers an interrupt costing ~1 µs of CPU time, that is roughly **1.2% CPU overhead** — manageable. But at 3 Mbaud, bytes arrive every ~3.3 µs, and interrupt overhead can consume **30% or more** of the CPU budget, leaving little headroom for application logic.

Batch processing solves this by amortizing the fixed ISR entry/exit cost across many bytes.

---

## Batch Processing Concepts

### Batch Size and Latency Trade-off

Larger batches mean fewer interrupts and lower CPU overhead. However, they also introduce **latency**: a byte received at the start of a batch is not processed until the entire batch is complete. The optimal batch size balances:

- **Throughput**: larger batches → more efficient
- **Latency**: smaller batches → faster response
- **Buffer memory**: larger batches → more RAM required

### Triggering Strategies

Batches can be flushed (processed) based on several conditions:

| Strategy | Trigger | Use Case |
|---|---|---|
| **Size threshold** | N bytes accumulated | Fixed-size protocol frames |
| **Idle timeout** | No new byte for T µs | Variable-length frames |
| **Timer periodic** | Fixed interval (e.g., 1 ms) | Streaming data, audio |
| **DMA half/full complete** | Hardware event | High-throughput, low CPU |
| **Special delimiter** | Specific byte (e.g., `\n`) | ASCII line-oriented protocols |

---

## Transmit Batching

### Hardware FIFO Utilization

Most modern UARTs include a transmit FIFO (often 16–64 bytes deep). Instead of writing one byte and waiting for TX Empty interrupt, batch processing fills the FIFO completely in one ISR or DMA burst, then enables the FIFO-threshold interrupt to refill when the FIFO is half-empty.

```
Application          TX Ring Buffer         HW TX FIFO          UART Wire
-----------          --------------         -----------          ----------
write(data, 64) ---> [64 bytes]  ---fill--> [16 bytes] --drain-> ~~~bits~~~
                     [48 bytes remain]       [0 bytes]
                                                  ^
                              FIFO Half-Empty ISR |
                     [32 bytes] ---fill 16-->  [16 bytes]
```

### Transmit Flow Without Batching (per-byte IRQ)

```
CPU cycles per byte = IRQ_entry + ISR_body + IRQ_exit
                    ≈ 30 + 20 + 20 = 70 cycles
For 1000 bytes: 70,000 cycles wasted in interrupt machinery
```

### Transmit Flow With Batching (FIFO threshold)

```
CPU cycles per batch = IRQ_entry + (ISR_body_per_byte × batch_size) + IRQ_exit
                     ≈ 30 + (5 × 16) + 20 = 130 cycles for 16 bytes
Per byte cost: 130 / 16 ≈ 8 cycles  (vs 70 without batching)
```

---

## Receive Batching

### FIFO Threshold Interrupt

Instead of generating an interrupt on every received byte, the RX FIFO can be configured to interrupt only when N bytes have accumulated. The ISR then reads all N bytes at once.

### Idle Line Detection

Many UART peripherals (STM32, NXP, etc.) support an **idle line interrupt**: fired when the RX line goes idle after receiving data. This naturally triggers a batch flush at the end of a frame, regardless of frame size — ideal for variable-length packets.

### RX DMA with Half/Full Transfer Interrupts

DMA can copy received bytes directly to memory. Interrupts fire at half-buffer and full-buffer completion, giving the CPU a buffer of data to process at each wakeup rather than individual bytes.

---

## DMA-Based Batch Transfers

DMA (Direct Memory Access) is the most powerful form of UART batching. The CPU programs a DMA channel once, and hardware handles byte transfers autonomously.

### DMA Transfer Modes

| Mode | Interrupt When | Use Case |
|---|---|---|
| Normal | Transfer complete | Single fixed-size block |
| Circular | Half-complete & complete | Continuous streaming |
| Double-buffer | Alternating buffers fill | Zero-copy ping-pong |

### Circular DMA for RX (most common for streaming)

```
Memory Buffer [0..N-1]
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 ^           ^               ^
 |           |               |
 start    Half-TC ISR     Full-TC ISR
           process [0..N/2)  process [N/2..N)
```

---

## Ring Buffer Architecture

At the software level, batch processing relies on **ring buffers** (circular buffers) to decouple producers (ISR/DMA) from consumers (application).

```
Write ptr                     Read ptr
    v                             v
+---+---+---+---+---+---+---+---+
| D | A | T | A | . | . | . | . |
+---+---+---+---+---+---+---+---+
 <--- unread data --->  <-- free -->
```

Key properties:
- Lock-free single-producer single-consumer (SPSC) is possible with careful memory ordering
- Size must be a power of two for efficient modulo via bitmask
- ISR writes; application reads — no mutex needed in SPSC configuration

---

## Implementation in C/C++

### 1. Ring Buffer Implementation (C)

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define RING_BUF_SIZE  256   // Must be power of 2
#define RING_BUF_MASK  (RING_BUF_SIZE - 1)

typedef struct {
    uint8_t  buf[RING_BUF_SIZE];
    volatile uint32_t head;  // Written by producer (ISR)
    volatile uint32_t tail;  // Written by consumer (app)
} ring_buf_t;

static inline bool ring_buf_push(ring_buf_t *rb, uint8_t byte)
{
    uint32_t next_head = (rb->head + 1) & RING_BUF_MASK;
    if (next_head == rb->tail) {
        return false; // Buffer full
    }
    rb->buf[rb->head] = byte;
    // Memory barrier before updating head (compiler + CPU ordering)
    __asm volatile("dmb" ::: "memory");
    rb->head = next_head;
    return true;
}

static inline bool ring_buf_pop(ring_buf_t *rb, uint8_t *out)
{
    if (rb->tail == rb->head) {
        return false; // Buffer empty
    }
    *out = rb->buf[rb->tail];
    __asm volatile("dmb" ::: "memory");
    rb->tail = (rb->tail + 1) & RING_BUF_MASK;
    return true;
}

static inline uint32_t ring_buf_count(const ring_buf_t *rb)
{
    return (rb->head - rb->tail) & RING_BUF_MASK;
}
```

---

### 2. UART TX Batching with FIFO Threshold (C, STM32 HAL style)

```c
#include "stm32f4xx_hal.h"

#define TX_BATCH_SIZE   16    // Match UART TX FIFO depth
#define TX_BUF_SIZE     512

static ring_buf_t tx_ring;
static UART_HandleTypeDef *huart_ref;

// Call this from application to enqueue data
bool uart_batch_write(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (!ring_buf_push(&tx_ring, data[i])) {
            return false; // TX buffer overflow
        }
    }

    // Kick off transmission if not already running
    // (Enable TX empty interrupt)
    __HAL_UART_ENABLE_IT(huart_ref, UART_IT_TXE);
    return true;
}

// UART TX ISR — called when TX FIFO requests more data
void uart_tx_isr(UART_HandleTypeDef *huart)
{
    // Fill as many bytes as the FIFO can take in one burst
    uint32_t filled = 0;
    uint8_t byte;

    while (filled < TX_BATCH_SIZE && ring_buf_pop(&tx_ring, &byte)) {
        huart->Instance->DR = byte;  // Write to UART data register
        filled++;
    }

    if (ring_buf_count(&tx_ring) == 0) {
        // No more data — disable TX empty interrupt to stop ISR storm
        __HAL_UART_DISABLE_IT(huart, UART_IT_TXE);
    }
}
```

---

### 3. UART RX Batching with Idle Line Detection (C, STM32)

```c
#define RX_DMA_BUF_SIZE   128

static uint8_t rx_dma_buf[RX_DMA_BUF_SIZE];
static ring_buf_t rx_app_ring;

// Called once at init: configure DMA + enable idle line interrupt
void uart_batch_rx_init(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdma_rx)
{
    // Start circular DMA RX into rx_dma_buf
    HAL_UART_Receive_DMA(huart, rx_dma_buf, RX_DMA_BUF_SIZE);

    // Enable idle line interrupt
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);
}

// Track how many bytes DMA has written so far
static uint32_t dma_last_pos = 0;

// Flush bytes from DMA buffer into application ring buffer
static void uart_rx_flush_dma(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdma_rx)
{
    // Current DMA write position (counts down on STM32)
    uint32_t dma_pos = RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(hdma_rx);

    if (dma_pos == dma_last_pos) return; // Nothing new

    if (dma_pos > dma_last_pos) {
        // No wrap: copy linear region
        for (uint32_t i = dma_last_pos; i < dma_pos; i++) {
            ring_buf_push(&rx_app_ring, rx_dma_buf[i]);
        }
    } else {
        // Buffer wrapped: copy tail then head
        for (uint32_t i = dma_last_pos; i < RX_DMA_BUF_SIZE; i++) {
            ring_buf_push(&rx_app_ring, rx_dma_buf[i]);
        }
        for (uint32_t i = 0; i < dma_pos; i++) {
            ring_buf_push(&rx_app_ring, rx_dma_buf[i]);
        }
    }

    dma_last_pos = dma_pos;
}

// UART IRQ Handler — called on idle line or DMA half/full complete
void uart_rx_irq_handler(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdma_rx)
{
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(huart);
        uart_rx_flush_dma(huart, hdma_rx); // Flush partial batch on idle
    }
}

// DMA half-transfer callback (N/2 bytes received)
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    uart_rx_flush_dma(huart, NULL); // Process first half
}

// DMA full-transfer callback (full circular wrap)
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uart_rx_flush_dma(huart, NULL); // Process second half
}

// Application reads from ring buffer (no interrupt needed)
uint32_t uart_batch_read(uint8_t *out, uint32_t max_len)
{
    uint32_t count = 0;
    while (count < max_len && ring_buf_pop(&rx_app_ring, &out[count])) {
        count++;
    }
    return count;
}
```

---

### 4. Timed Batch Flush (C, periodic timer-driven)

```c
// Called from a 1 ms SysTick or hardware timer ISR
void uart_batch_timer_tick(void)
{
    static uint32_t last_rx_count = 0;
    uint32_t current_count = ring_buf_count(&rx_app_ring);

    // If data has arrived since last tick, signal application
    if (current_count > 0 && current_count != last_rx_count) {
        last_rx_count = current_count;
    } else if (current_count > 0) {
        // Count unchanged — line has been idle for one tick period
        // Signal application to process what's accumulated
        extern volatile bool rx_batch_ready;
        rx_batch_ready = true;
        last_rx_count = 0;
    }
}
```

---

### 5. C++ Class Wrapping Batch UART

```cpp
#include <cstdint>
#include <cstring>
#include <array>
#include <atomic>

template<uint32_t BUF_SIZE = 256>
class UartBatchDriver {
    static_assert((BUF_SIZE & (BUF_SIZE - 1)) == 0,
                  "BUF_SIZE must be a power of 2");

public:
    UartBatchDriver() : head_(0), tail_(0) {}

    // Producer (ISR context): push a received byte
    bool push(uint8_t byte) noexcept {
        const uint32_t next = (head_.load(std::memory_order_relaxed) + 1) & MASK;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buf_[head_.load(std::memory_order_relaxed)] = byte;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer (task context): pop up to `len` bytes, return count
    uint32_t pop_batch(uint8_t *out, uint32_t len) noexcept {
        uint32_t count = 0;
        uint32_t t = tail_.load(std::memory_order_relaxed);
        const uint32_t h = head_.load(std::memory_order_acquire);

        while (count < len && t != h) {
            out[count++] = buf_[t];
            t = (t + 1) & MASK;
        }

        tail_.store(t, std::memory_order_release);
        return count;
    }

    uint32_t available() const noexcept {
        return (head_.load(std::memory_order_acquire) -
                tail_.load(std::memory_order_relaxed)) & MASK;
    }

private:
    static constexpr uint32_t MASK = BUF_SIZE - 1;
    std::array<uint8_t, BUF_SIZE> buf_{};
    std::atomic<uint32_t> head_;
    std::atomic<uint32_t> tail_;
};

// Usage example
UartBatchDriver<512> uart0;

// In ISR:
// uart0.push(received_byte);

// In application task (process when 64+ bytes ready or on timeout):
void process_uart_data() {
    if (uart0.available() >= 64) {
        uint8_t frame[64];
        uint32_t n = uart0.pop_batch(frame, sizeof(frame));
        // process frame[0..n-1]
    }
}
```

---

## Implementation in Rust

Rust's ownership model and type system make it particularly well-suited for safe, zero-cost batch UART drivers.

### 1. Lock-Free SPSC Ring Buffer (Rust, `no_std`)

```rust
// Cargo.toml:
// [dependencies]
// heapless = "0.8"   (or use a custom implementation as below)

use core::sync::atomic::{AtomicUsize, Ordering};
use core::cell::UnsafeCell;

pub struct RingBuf<const N: usize> {
    buf:  UnsafeCell<[u8; N]>,
    head: AtomicUsize,  // written by producer (ISR)
    tail: AtomicUsize,  // written by consumer (app)
}

// SAFETY: SPSC — one writer (ISR), one reader (app task).
// Must never be used with multiple producers or consumers.
unsafe impl<const N: usize> Sync for RingBuf<N> {}

impl<const N: usize> RingBuf<N> {
    const MASK: usize = N - 1;

    pub const fn new() -> Self {
        assert!(N.is_power_of_two(), "N must be a power of two");
        Self {
            buf:  UnsafeCell::new([0u8; N]),
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    /// Push a byte — called from ISR (producer).
    /// Returns false if the buffer is full.
    pub fn push(&self, byte: u8) -> bool {
        let head = self.head.load(Ordering::Relaxed);
        let next = (head + 1) & Self::MASK;
        if next == self.tail.load(Ordering::Acquire) {
            return false; // full
        }
        // SAFETY: only the producer writes; no aliasing possible.
        unsafe { (*self.buf.get())[head] = byte; }
        self.head.store(next, Ordering::Release);
        true
    }

    /// Pop one byte — called from application (consumer).
    pub fn pop(&self) -> Option<u8> {
        let tail = self.tail.load(Ordering::Relaxed);
        if tail == self.head.load(Ordering::Acquire) {
            return None; // empty
        }
        // SAFETY: only the consumer reads at `tail`.
        let byte = unsafe { (*self.buf.get())[tail] };
        self.tail.store((tail + 1) & Self::MASK, Ordering::Release);
        Some(byte)
    }

    /// Pop up to `out.len()` bytes in one call — the batch pop.
    pub fn pop_batch(&self, out: &mut [u8]) -> usize {
        let mut count = 0;
        let mut tail = self.tail.load(Ordering::Relaxed);
        let head = self.head.load(Ordering::Acquire);

        while count < out.len() && tail != head {
            // SAFETY: SPSC contract; tail owned by consumer.
            out[count] = unsafe { (*self.buf.get())[tail] };
            count += 1;
            tail = (tail + 1) & Self::MASK;
        }

        self.tail.store(tail, Ordering::Release);
        count
    }

    pub fn len(&self) -> usize {
        let h = self.head.load(Ordering::Acquire);
        let t = self.tail.load(Ordering::Relaxed);
        (h.wrapping_sub(t)) & Self::MASK
    }
}
```

---

### 2. Static UART Ring Buffers and ISR Integration (Rust, embassy / bare-metal)

```rust
use core::sync::atomic::{compiler_fence, Ordering};

// Static buffers — shared between ISR and application
static RX_BUF: RingBuf<256> = RingBuf::new();
static TX_BUF: RingBuf<256> = RingBuf::new();

/// UART ISR — receives bytes, pushes to RX ring buffer.
/// Also drains TX ring buffer into UART data register.
#[cortex_m_rt::interrupt]
fn USART2() {
    // SAFETY: Accessing UART registers via PAC/HAL
    let uart = unsafe { &*stm32f4::stm32f407::USART2::ptr() };

    // --- RX: read all available bytes from UART FIFO ---
    while uart.sr.read().rxne().bit_is_set() {
        let byte = uart.dr.read().dr().bits() as u8;
        if !RX_BUF.push(byte) {
            // Buffer overflow — application is not keeping up
            // Could set an overflow flag here
        }
    }

    // --- TX: fill UART FIFO with pending bytes ---
    while uart.sr.read().txe().bit_is_set() {
        match TX_BUF.pop() {
            Some(byte) => {
                uart.dr.write(|w| unsafe { w.dr().bits(byte as u16) });
            }
            None => {
                // No more data — disable TX empty interrupt
                uart.cr1.modify(|_, w| w.txeie().disabled());
                break;
            }
        }
    }
}

/// Application API — write a batch to the UART
pub fn uart_write(data: &[u8]) -> usize {
    let mut written = 0;
    for &byte in data {
        if TX_BUF.push(byte) {
            written += 1;
        } else {
            break; // TX buffer full
        }
    }

    // Enable TX empty interrupt to start draining
    compiler_fence(Ordering::SeqCst);
    let uart = unsafe { &*stm32f4::stm32f407::USART2::ptr() };
    uart.cr1.modify(|_, w| w.txeie().enabled());

    written
}

/// Application API — read a batch from the UART receive buffer
pub fn uart_read(out: &mut [u8]) -> usize {
    RX_BUF.pop_batch(out)
}
```

---

### 3. DMA-Based RX Batch with Idle Line (Rust, Embassy async)

```rust
use embassy_stm32::usart::{Config, UartRx};
use embassy_stm32::dma::NoDma;
use embassy_time::{Duration, Timer};

const DMA_RX_BUF: usize = 128;

#[embassy_executor::task]
async fn uart_rx_task(mut rx: UartRx<'static, embassy_stm32::peripherals::USART2,
                                      embassy_stm32::peripherals::DMA1_CH5>)
{
    let mut dma_buf = [0u8; DMA_RX_BUF];
    let mut app_buf = [0u8; 512];
    let mut app_len = 0usize;

    loop {
        // read_until_idle blocks until either:
        //   - the DMA buffer fills, OR
        //   - the UART idle line interrupt fires (end of frame)
        // This is the Rust/Embassy equivalent of idle-line batch triggering.
        match rx.read_until_idle(&mut dma_buf).await {
            Ok(n) => {
                // n bytes arrived as a batch — process them
                if app_len + n <= app_buf.len() {
                    app_buf[app_len..app_len + n].copy_from_slice(&dma_buf[..n]);
                    app_len += n;
                }

                // Example: flush to application when we see a newline
                if let Some(pos) = app_buf[..app_len].iter().position(|&b| b == b'\n') {
                    let line = &app_buf[..pos];
                    process_line(line);
                    // Shift remaining bytes down
                    let remaining = app_len - pos - 1;
                    app_buf.copy_within(pos + 1..app_len, 0);
                    app_len = remaining;
                }
            }
            Err(e) => {
                // Handle framing / overrun errors
                app_len = 0; // Discard corrupt batch
                _ = e;
            }
        }
    }
}

fn process_line(line: &[u8]) {
    // Application-level batch processing of a complete line
    let _ = line;
}
```

---

### 4. TX Batch Writer with Flush Threshold (Rust)

```rust
pub struct UartBatchWriter<const FLUSH_AT: usize = 32> {
    tx_buf: RingBuf<256>,
    byte_count: usize,
}

impl<const FLUSH_AT: usize> UartBatchWriter<FLUSH_AT> {
    pub const fn new() -> Self {
        Self {
            tx_buf: RingBuf::new(),
            byte_count: 0,
        }
    }

    /// Accumulate bytes; auto-flush when threshold is reached.
    pub fn write(&mut self, data: &[u8]) {
        for &byte in data {
            let _ = self.tx_buf.push(byte);
            self.byte_count += 1;
        }

        if self.byte_count >= FLUSH_AT {
            self.flush();
        }
    }

    /// Force transmission of accumulated bytes now.
    pub fn flush(&mut self) {
        if self.tx_buf.len() == 0 {
            return;
        }
        // Enable TX interrupt; ISR will drain the buffer
        cortex_m::interrupt::free(|_| {
            enable_uart_tx_interrupt();
        });
        self.byte_count = 0;
    }
}

fn enable_uart_tx_interrupt() {
    let uart = unsafe { &*stm32f4::stm32f407::USART2::ptr() };
    uart.cr1.modify(|_, w| w.txeie().enabled());
}

// Usage
static mut WRITER: UartBatchWriter<64> = UartBatchWriter::new();

fn main_loop() {
    loop {
        let data = b"Hello from batch writer!\r\n";
        unsafe { WRITER.write(data); }
        // Bytes accumulate; flush fires automatically at 64 bytes
        // or explicitly via WRITER.flush()
    }
}
```

---

## Tuning Batch Parameters

### Choosing Batch / FIFO Threshold Size

```
Minimum useful batch size = IRQ_overhead_cycles / cycles_per_byte_ISR_work

Example (Cortex-M4 @ 168 MHz, 3 Mbaud):
  IRQ overhead       ≈ 70 cycles
  Bytes per second   = 3,000,000 / 10 = 300,000
  Cycles per byte    = 168,000,000 / 300,000 = 560
  Min batch          = 70 / 5 (ISR work per byte) = 14 bytes

→ Use 16 bytes (next power of 2, matches typical FIFO depth)
```

### Choosing Idle Timeout

```
Idle timeout > inter-character gap within a valid frame
Idle timeout < minimum gap between frames

For 115200 baud, 1 character = ~87 µs:
  Inter-byte timeout = 3–5 character times ≈ 260–435 µs
  Typical choice: 500 µs hardware idle detection or 1 ms timer
```

### Buffer Sizing

```
RX buffer ≥ max_frame_size × 2    (double-buffer headroom)
TX buffer ≥ max_burst_size × 2    (prevents TX stall)

Memory budget example:
  RX DMA buffer: 128 bytes  (hardware)
  RX app ring:   256 bytes  (software)
  TX ring:       256 bytes  (software)
  Total:         640 bytes  (negligible on most MCUs)
```

---

## Summary

UART batch processing is an essential optimization for any system where UART throughput or CPU efficiency matters. The core idea is simple: **group bytes, not individual characters**, so the fixed cost of interrupt handling is amortized across many bytes at once.

**Key takeaways:**

- **Hardware FIFOs** are the first tool to reach for — configure the TX/RX threshold interrupt rather than per-byte interrupts. This alone can reduce CPU interrupt overhead by 8–16×.

- **DMA circular mode** with half/full-complete callbacks eliminates per-byte CPU involvement entirely on the RX path, ideal for high-throughput streaming.

- **Idle line detection** provides natural frame boundaries for variable-length protocols, triggering a batch flush only when the sender has stopped sending — no polling needed.

- **Ring buffers** decouple the ISR/DMA (producer) from the application (consumer), allowing each to run at its own pace with minimal synchronization overhead.

- **In C/C++**, manual atomic operations (`volatile`, `__asm volatile("dmb")`, `std::atomic`) are needed to safely share ring buffer state between ISR and application contexts.

- **In Rust**, `AtomicUsize` with carefully chosen `Ordering` constraints provides the same guarantees with compiler-enforced safety, while Embassy's `read_until_idle()` makes async DMA+idle-line batching trivially correct.

- **Batch parameters** (size, timeout, buffer depth) must be tuned for the specific baud rate, frame structure, and latency budget of the application.

When implemented correctly, batch processing can reduce UART-related CPU overhead from 30% or more down to 1–3%, freeing the processor for real application work without sacrificing reliability or data integrity.

---

*Document: 27. UART Batch Processing — Grouping transfers to reduce interrupt overhead*