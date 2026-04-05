# 30. UART Throughput Maximization

**Concepts covered:**
- Theoretical throughput calculation with a reference table across common baud rates
- All major sources of overhead (interrupt-per-byte, polling, unaligned buffers, flow control stalls, clock error)
- Hardware FIFO configuration and threshold tuning
- Baud rate error analysis and fractional divider usage
- Interrupt latency reduction strategies for Cortex-M
- Protocol-level efficiency (sliding windows vs. ACK-per-frame, batching, checksum selection)
- Benchmarking techniques (GPIO toggle, DWT cycle counter, throughput statistics)

**Code examples provided (9 total):**

| # | Language | Topic |
|---|----------|-------|
| 1 | C | DMA circular receive + idle-line interrupt (STM32-style) |
| 2 | C | DMA double-buffered transmit |
| 3 | C | Linux `termios` VMIN/VTIME batched reads with throughput reporting |
| 4 | C | Zero-copy scatter-gather transmit with `writev()` |
| 5 | C | Polling transmit with prefetch optimization (bare-metal) |
| 6 | Rust | Async DMA receive with `embassy-rs` and `read_until_idle` |
| 7 | Rust | Lock-free SPSC ring buffer (`no_std`, atomic, ISR-safe) |
| 8 | Rust | Linux async UART with `tokio` + throughput measurement |
| 9 | Rust | Bare-metal double-buffered DMA transmit (PAC-level) |

The summary table ranks all 10 optimizations by typical impact and complexity so you can prioritize your implementation effort.

## Achieving Maximum Effective Data Rates with Minimal Overhead

---

## Table of Contents

1. [Introduction](#introduction)
2. [Theoretical Maximum Throughput](#theoretical-maximum-throughput)
3. [Sources of Overhead and Loss](#sources-of-overhead-and-loss)
4. [Hardware-Level Optimizations](#hardware-level-optimizations)
5. [DMA-Based Transfers](#dma-based-transfers)
6. [Circular Buffer Architectures](#circular-buffer-architectures)
7. [Double Buffering](#double-buffering)
8. [Flow Control Strategies](#flow-control-strategies)
9. [Baud Rate Selection and Clock Accuracy](#baud-rate-selection-and-clock-accuracy)
10. [Interrupt Latency Reduction](#interrupt-latency-reduction)
11. [FIFO Depth Tuning](#fifo-depth-tuning)
12. [Protocol-Level Efficiency](#protocol-level-efficiency)
13. [C/C++ Implementation Examples](#cc-implementation-examples)
14. [Rust Implementation Examples](#rust-implementation-examples)
15. [Benchmarking and Measurement](#benchmarking-and-measurement)
16. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) throughput maximization is the discipline of extracting the highest possible effective data rate from a serial link while keeping CPU overhead, latency, and error rates minimal. Although UART is an inherently serial and relatively slow protocol compared to SPI or USB, many embedded systems depend on it for host communication, logging, firmware updates, or sensor data streaming — all of which demand careful tuning to stay within timing budgets.

Effective throughput is not simply the configured baud rate. It is the actual amount of useful application data transferred per second after accounting for:

- Start/stop bit framing overhead (always present, up to ~20%)
- Parity bits (optional but common)
- Software processing delays
- Flow control stalls
- Buffer underruns and overruns
- Context-switch and interrupt latency

This chapter covers every layer of the stack — from hardware FIFO configuration to application-level batching — providing concrete, production-ready code in both C/C++ and Rust.

---

## Theoretical Maximum Throughput

A standard UART frame consists of:

- **1 start bit** (always)
- **5–9 data bits** (typically 8)
- **0–1 parity bit**
- **1–2 stop bits** (typically 1)

For the most common configuration (8N1: 8 data bits, no parity, 1 stop bit):

```
Frame size = 1 (start) + 8 (data) + 1 (stop) = 10 bits per byte
Throughput efficiency = 8/10 = 80%
```

At common baud rates:

| Baud Rate   | Bits/s  | Max bytes/s (8N1) | Effective kB/s |
|-------------|---------|-------------------|----------------|
| 9,600       | 9,600   | 960               | 0.94           |
| 115,200     | 115,200 | 11,520            | 11.25          |
| 921,600     | 921,600 | 92,160            | 90.0           |
| 3,000,000   | 3M      | 300,000           | 292.9          |
| 12,000,000  | 12M     | 1,200,000         | 1,171.9        |

> **Key insight:** Every optimization described in this chapter reduces the gap between the theoretical maximum and the real-world rate your application actually achieves.

---

## Sources of Overhead and Loss

Understanding the enemy is the first step toward defeating it:

### 1. Interrupt-per-Byte Processing
Raising a CPU interrupt for every received byte at 921,600 baud means ~92,160 interrupts/second. Each interrupt typically costs 50–200 CPU cycles (save/restore context), consuming significant CPU bandwidth even before the ISR body executes.

### 2. Polling Loops with Busy-Wait
Simple `while(!(UART->SR & TXE));` spin-loops block the CPU completely during transmission. At 115,200 baud, each byte takes ~87 µs — during which the CPU accomplishes nothing else.

### 3. Character-at-a-Time memcpy
Copying data one byte at a time through multiple software layers (application → ring buffer → DMA descriptor) adds latency and cache pressure.

### 4. Unaligned Buffer Access
Buffers not aligned to cache-line or word boundaries force the hardware DMA to issue sub-optimal bus transactions.

### 5. Flow Control Stalls
Mismatched RTS/CTS or software XON/XOFF handling can stall the transmit side for milliseconds at a time, destroying throughput.

### 6. Clock Accuracy
Poor oscillator accuracy causes baud rate error to accumulate, forcing the receiver to sample bits at the wrong moment, especially at high rates.

---

## Hardware-Level Optimizations

### Enable and Configure the Hardware FIFO

Most modern UART peripherals (16550-compatible UARTs, STM32 USARTs with FIFO, NXP LPUARTs) include an on-chip FIFO of 16, 32, 64, or 128 bytes. Without it, a receive interrupt fires on every single byte. With it correctly configured, you can absorb a burst of bytes before being interrupted.

**Key settings:**

- **RX FIFO threshold:** Set to 50–75% of FIFO depth. This gives time to service the interrupt before overflow while reducing interrupt frequency.
- **TX FIFO threshold:** Set low (e.g., 25%) so the FIFO refill interrupt fires before the FIFO drains completely.
- **RX timeout interrupt:** Always enable this — it fires when bytes stop arriving but the FIFO hasn't reached its threshold, preventing data from getting stuck.

### Choose 8N1 Over Other Frame Formats

For maximum efficiency:
- Avoid parity bits (reduces frame to 10 bits from 11)
- Use 1 stop bit instead of 2 (reduces frame to 10 bits from 11)
- Use 8 data bits (not 7)

Only deviate from 8N1 when an external standard mandates it.

---

## DMA-Based Transfers

Direct Memory Access (DMA) is the single most impactful optimization for high-throughput UART. Instead of the CPU moving each byte, the DMA controller performs the transfer autonomously between RAM and the UART data register.

### Transmit with DMA

1. Fill a buffer in RAM with the data to send.
2. Configure a DMA channel: source = buffer address, destination = UART TX data register, count = byte count.
3. Enable DMA. The CPU is free until the transfer completes.
4. On DMA transfer-complete interrupt, refill the buffer or mark it free.

### Receive with DMA (Circular Mode)

The most powerful receive strategy uses DMA in **circular mode** with a **half-transfer interrupt**:

- DMA continuously writes received bytes into a ring buffer.
- A half-transfer interrupt fires when the DMA is halfway through the buffer.
- A transfer-complete interrupt fires when the DMA wraps around.
- The CPU processes the first half while the DMA fills the second, and vice versa.
- An IDLE LINE interrupt (supported on many MCUs) fires when the line goes idle, catching the last partial batch.

---

## C/C++ Implementation Examples

### Example 1: DMA Circular Receive with Idle Line Detection (STM32 HAL style)

```c
/**
 * uart_throughput.c
 * High-throughput UART receive using DMA circular mode + IDLE line detection.
 * Target: STM32F4/F7/H7 family (adapt register names for other MCUs).
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define UART_DMA_BUF_SIZE   512     /* Must be power of 2 */
#define UART_RING_BUF_SIZE  4096    /* Application ring buffer, power of 2 */

/* ------------------------------------------------------------------ */
/*  DMA receive double-buffer (hardware writes here directly)          */
/* ------------------------------------------------------------------ */
static uint8_t  dma_rx_buf[UART_DMA_BUF_SIZE] __attribute__((aligned(32)));
static volatile uint32_t dma_write_pos = 0;   /* Tracks DMA head position */
static uint32_t app_read_pos  = 0;            /* Application read position */

/* ------------------------------------------------------------------ */
/*  Lock-free ring buffer for application layer                        */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t  data[UART_RING_BUF_SIZE];
    volatile uint32_t head;   /* Written by producer (ISR/DMA callback) */
    volatile uint32_t tail;   /* Read by consumer (application)         */
} RingBuffer;

static RingBuffer rx_ring;

/* Inline: available bytes in ring buffer */
static inline uint32_t ring_available(const RingBuffer *rb) {
    return (rb->head - rb->tail) & (UART_RING_BUF_SIZE - 1);
}

/* Inline: push bytes into ring buffer — called from ISR context */
static inline void ring_push(RingBuffer *rb, const uint8_t *src, uint32_t len) {
    uint32_t head = rb->head;
    for (uint32_t i = 0; i < len; i++) {
        rb->data[head & (UART_RING_BUF_SIZE - 1)] = src[i];
        head++;
    }
    /* Barrier ensures data is written before head is updated */
    __asm__ volatile ("dmb" ::: "memory");
    rb->head = head;
}

/* ------------------------------------------------------------------ */
/*  DMA transfer-complete / half-complete callback                     */
/*  Called from ISR — keep it short!                                   */
/* ------------------------------------------------------------------ */
static void process_dma_rx(uint32_t new_dma_pos) {
    uint32_t old_pos = dma_write_pos;
    if (new_dma_pos == old_pos) return;

    if (new_dma_pos > old_pos) {
        /* Linear segment: [old_pos, new_dma_pos) */
        ring_push(&rx_ring, &dma_rx_buf[old_pos], new_dma_pos - old_pos);
    } else {
        /* Wrap-around: [old_pos, end) then [0, new_dma_pos) */
        ring_push(&rx_ring, &dma_rx_buf[old_pos], UART_DMA_BUF_SIZE - old_pos);
        ring_push(&rx_ring, &dma_rx_buf[0],        new_dma_pos);
    }
    dma_write_pos = new_dma_pos;
}

/**
 * UART IDLE line IRQ handler (platform-specific register access shown).
 * On STM32: clear SR_IDLE flag, read DMA remaining count.
 */
void UART_IDLE_IRQHandler(void) {
    /* Clear idle flag (read SR then DR on F4, or write 1 to ICR on F7/H7) */
    volatile uint32_t tmp = UART->SR;
    tmp = UART->DR;
    (void)tmp;

    /* Current DMA position = buffer size - remaining count */
    uint32_t current_pos = UART_DMA_BUF_SIZE - DMA_Stream->NDTR;
    process_dma_rx(current_pos);
}

/**
 * DMA half-transfer callback — fired when DMA reaches midpoint.
 */
void DMA_HalfTransfer_Callback(void) {
    process_dma_rx(UART_DMA_BUF_SIZE / 2);
}

/**
 * DMA full-transfer callback — fired when DMA wraps to start.
 */
void DMA_FullTransfer_Callback(void) {
    process_dma_rx(0);  /* DMA has wrapped; new position is 0 */
}

/* ------------------------------------------------------------------ */
/*  Application-layer read — called from main loop, NOT from ISR       */
/* ------------------------------------------------------------------ */
uint32_t uart_read(uint8_t *dst, uint32_t max_len) {
    uint32_t avail = ring_available(&rx_ring);
    uint32_t count = (avail < max_len) ? avail : max_len;

    for (uint32_t i = 0; i < count; i++) {
        dst[i] = rx_ring.data[rx_ring.tail & (UART_RING_BUF_SIZE - 1)];
        rx_ring.tail++;
    }
    return count;
}
```

---

### Example 2: DMA Transmit with Double Buffering

```c
/**
 * High-throughput DMA transmit with double buffering.
 * While one buffer is being transmitted by DMA, the application
 * fills the other — eliminating transmit stalls.
 */

#define TX_BUF_SIZE  1024

typedef struct {
    uint8_t  buf[2][TX_BUF_SIZE];  /* Two alternating buffers          */
    uint32_t len[2];               /* Valid byte count in each buffer   */
    volatile uint8_t  active;      /* Which buffer DMA is currently using */
    volatile bool     busy;        /* DMA in progress                   */
} DmaTxState;

static DmaTxState tx_state;

/**
 * Start a DMA transmit of the active buffer.
 * Must be called with interrupts disabled or equivalent protection.
 */
static void dma_tx_start(void) {
    uint8_t  idx = tx_state.active;
    uint32_t len = tx_state.len[idx];
    if (len == 0) {
        tx_state.busy = false;
        return;
    }
    tx_state.busy = true;

    /* Platform: configure DMA, source = buf[idx], count = len, then enable */
    DMA_Configure_TX(tx_state.buf[idx], len);
    DMA_Enable_TX();
}

/**
 * DMA TX complete interrupt — swap buffers and restart if pending data.
 */
void DMA_TX_Complete_IRQHandler(void) {
    tx_state.len[tx_state.active] = 0;  /* Mark current buffer free */
    tx_state.active ^= 1;               /* Flip to the other buffer  */
    dma_tx_start();                     /* Start transmitting it     */
}

/**
 * Application: queue data for transmission.
 * Fills the idle buffer; if DMA is free, starts immediately.
 */
bool uart_write(const uint8_t *src, uint32_t len) {
    /* Select the buffer NOT currently being transmitted */
    uint8_t idle_idx = tx_state.active ^ (tx_state.busy ? 1 : 0);

    if (tx_state.len[idle_idx] + len > TX_BUF_SIZE) {
        return false;  /* Buffer full — caller must retry or flush */
    }

    memcpy(&tx_state.buf[idle_idx][tx_state.len[idle_idx]], src, len);
    tx_state.len[idle_idx] += len;

    /* If DMA is idle, kick it off now */
    if (!tx_state.busy) {
        tx_state.active = idle_idx;
        dma_tx_start();
    }
    return true;
}
```

---

### Example 3: FIFO Threshold Tuning and Interrupt Coalescing (POSIX/Linux)

```c
/**
 * Linux UART throughput maximization via termios and custom VMIN/VTIME.
 * Demonstrates batched reads to minimize syscall overhead.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <time.h>

#define READ_BUF_SIZE   4096    /* Large read buffer — one syscall per batch */
#define BAUD_RATE       B921600

/**
 * Open and configure UART for maximum throughput.
 * Key settings:
 *   - Raw mode (no line discipline processing)
 *   - VMIN=255, VTIME=1: block until 255 bytes arrive OR 100ms timeout
 *   - This coalesces up to 255 bytes per read() call
 */
int uart_open_highspeed(const char *dev) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return -1; }

    /* Clear O_NONBLOCK — we want blocking reads with VMIN/VTIME */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty;
    if (tcgetattr(fd, &tty) < 0) { perror("tcgetattr"); close(fd); return -1; }

    /* Raw mode: no processing of special characters */
    cfmakeraw(&tty);

    /* Baud rate */
    cfsetispeed(&tty, BAUD_RATE);
    cfsetospeed(&tty, BAUD_RATE);

    /* 8N1 */
    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    tty.c_cflag |= CS8 | CLOCAL | CREAD;

    /* Disable software flow control */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    /* Enable hardware flow control (RTS/CTS) if available */
    tty.c_cflag |= CRTSCTS;

    /*
     * VMIN=255, VTIME=1 (100 ms):
     *   - read() blocks until 255 bytes available OR 100 ms elapses
     *   - Dramatically reduces read() calls vs VMIN=1
     *   - Use larger VMIN for higher baud rates
     */
    tty.c_cc[VMIN]  = 255;
    tty.c_cc[VTIME] = 1;    /* Tenths of seconds */

    if (tcsetattr(fd, TCSANOW, &tty) < 0) {
        perror("tcsetattr"); close(fd); return -1;
    }

    /* Flush stale data */
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/**
 * Throughput-optimized receive loop.
 * Uses a large buffer and processes data in batches.
 */
void uart_rx_loop(int fd) {
    uint8_t  buf[READ_BUF_SIZE];
    uint64_t total_bytes = 0;
    struct timespec t_start, t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read"); break;
        }
        if (n == 0) continue;

        total_bytes += (uint64_t)n;

        /* --- Process buf[0..n-1] here as a batch --- */
        /* E.g., push into application ring buffer, parse frames, etc. */

        /* Throughput reporting every 5 seconds */
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        double elapsed = (t_now.tv_sec  - t_start.tv_sec) +
                         (t_now.tv_nsec - t_start.tv_nsec) * 1e-9;
        if (elapsed >= 5.0) {
            printf("Throughput: %.2f kB/s  (%.2f%% of theoretical max)\n",
                   (double)total_bytes / elapsed / 1024.0,
                   (double)total_bytes / elapsed / (921600.0 / 10.0) * 100.0);
            total_bytes = 0;
            t_start = t_now;
        }
    }
}
```

---

### Example 4: Zero-Copy Transmit with scatter-gather (Linux)

```c
/**
 * Zero-copy transmit using writev() to avoid memcpy between buffers.
 * Useful when data spans multiple non-contiguous memory regions.
 */

#include <sys/uio.h>

typedef struct {
    const uint8_t *data;
    size_t         len;
} TxChunk;

/**
 * Transmit multiple non-contiguous buffers in a single syscall.
 * Avoids copying into an intermediate buffer before writing to UART.
 */
ssize_t uart_tx_scatter(int fd, const TxChunk *chunks, int count) {
    /* Build iovec array on the stack (no heap allocation) */
    struct iovec iov[count];
    size_t total = 0;

    for (int i = 0; i < count; i++) {
        iov[i].iov_base = (void *)chunks[i].data;
        iov[i].iov_len  = chunks[i].len;
        total += chunks[i].len;
    }

    ssize_t written = writev(fd, iov, count);
    if (written < 0) { perror("writev"); return -1; }
    if ((size_t)written < total) {
        /* Partial write — handle remaining data */
        fprintf(stderr, "Partial write: %zd of %zu bytes\n", written, total);
    }
    return written;
}

/* Usage example */
void example_scatter_tx(int fd) {
    uint8_t header[]  = { 0xAA, 0x55, 0x10, 0x00 };
    uint8_t payload[256];  /* e.g., sensor data */
    uint8_t checksum[] = { 0x42 };

    memset(payload, 0xAB, sizeof(payload));  /* fill with dummy data */

    TxChunk chunks[] = {
        { header,   sizeof(header)   },
        { payload,  sizeof(payload)  },
        { checksum, sizeof(checksum) },
    };
    uart_tx_scatter(fd, chunks, 3);  /* One syscall for all three */
}
```

---

### Example 5: High-Speed Polling with CPU Spin (Bare-Metal, Latency-Critical)

```c
/**
 * For latency-critical paths where DMA setup overhead is unacceptable
 * (e.g., sending <8 bytes in a tight real-time loop).
 * Uses word-at-a-time writes to the FIFO where the UART supports it.
 */

#include <stdint.h>

/* Assumed UART register map — adapt to your hardware */
typedef struct {
    volatile uint32_t DR;   /* Data register       */
    volatile uint32_t SR;   /* Status register     */
    volatile uint32_t CR1;  /* Control register 1  */
} UART_TypeDef;

#define UART_SR_TXE   (1u << 7)   /* TX data register empty */
#define UART_SR_TC    (1u << 6)   /* Transmission complete   */
#define UART_SR_RXNE  (1u << 5)   /* RX not empty            */

/**
 * Send exactly len bytes by polling — no DMA, no interrupts.
 * Optimized: loop is tight, no function call overhead inside.
 */
void uart_tx_poll_fast(UART_TypeDef *uart, const uint8_t *buf, uint32_t len) {
    const uint8_t *end = buf + len;
    while (buf < end) {
        /* Spin on TXE — hardware empties the register quickly at high baud */
        while (!(uart->SR & UART_SR_TXE)) {
            __asm__ volatile ("nop");
        }
        uart->DR = *buf++;
    }
    /* Wait for final byte to complete (important before disabling UART) */
    while (!(uart->SR & UART_SR_TC)) {
        __asm__ volatile ("nop");
    }
}

/**
 * Prefetch-optimized version: loads next byte while waiting for TXE.
 * Hides memory latency on MCUs with cache or slow flash.
 */
void uart_tx_poll_prefetch(UART_TypeDef *uart, const uint8_t *buf, uint32_t len) {
    if (len == 0) return;
    uint8_t next = *buf++;
    len--;

    while (1) {
        /* Wait for TX empty */
        while (!(uart->SR & UART_SR_TXE)) { __asm__ volatile ("nop"); }

        uart->DR = next;
        if (len == 0) break;

        /* Prefetch the next byte while HW is busy */
        next = *buf++;
        len--;
    }

    while (!(uart->SR & UART_SR_TC)) { __asm__ volatile ("nop"); }
}
```

---

## Rust Implementation Examples

### Example 6: Async UART Receiver with embassy (embedded async Rust)

```rust
//! High-throughput UART receive using embassy-rs async runtime.
//! Demonstrates DMA-backed async reads with zero-copy processing.
//!
//! Cargo.toml dependencies:
//!   embassy-stm32 = { version = "0.1", features = ["stm32f429zi", "time-driver-any"] }
//!   embassy-executor = { version = "0.5", features = ["arch-cortex-m", "executor-thread"] }
//!   heapless = "0.8"

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::{
    bind_interrupts,
    usart::{self, Uart, Config},
    peripherals,
};
use heapless::Vec;

bind_interrupts!(struct Irqs {
    USART1 => usart::InterruptHandler<peripherals::USART1>;
});

const DMA_BUF: usize = 512;
const APP_BUF: usize = 4096;

#[embassy_executor::task]
async fn uart_rx_task(
    mut uart: Uart<'static, peripherals::USART1, peripherals::DMA2_CH2, peripherals::DMA2_CH7>,
) {
    let mut dma_buf = [0u8; DMA_BUF];
    let mut app_buf: Vec<u8, APP_BUF> = Vec::new();
    let mut total_bytes: u64 = 0;

    loop {
        // read_until_idle fills dma_buf using DMA and returns when the
        // UART line goes idle — equivalent to the idle-line interrupt strategy.
        // The CPU is fully released to other tasks while DMA runs.
        match uart.read_until_idle(&mut dma_buf).await {
            Ok(n) => {
                total_bytes += n as u64;

                // Zero-copy: process directly from DMA buffer
                process_batch(&dma_buf[..n], &mut app_buf);
            }
            Err(e) => {
                // Handle overrun, framing error, etc.
                defmt::error!("UART error: {:?}", e);
            }
        }
    }
}

/// Process a batch of received bytes — called with DMA buffer slice.
/// Avoids any additional copy; operates directly on the DMA buffer.
fn process_batch(data: &[u8], _app: &mut Vec<u8, APP_BUF>) {
    // Example: count newlines (frame boundary detection)
    let _frames = data.iter().filter(|&&b| b == b'\n').count();
    // In a real application: parse protocol frames, push to channel, etc.
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    let mut config = Config::default();
    config.baudrate = 921_600;
    // 8N1 is the default; hardware flow control can be enabled via config

    let uart = Uart::new(
        p.USART1,
        p.PA10,  // RX
        p.PA9,   // TX
        Irqs,
        p.DMA2_CH7,  // TX DMA
        p.DMA2_CH2,  // RX DMA
        config,
    ).unwrap();

    spawner.spawn(uart_rx_task(uart)).unwrap();
}
```

---

### Example 7: Lock-Free Ring Buffer in Rust (no_std compatible)

```rust
//! Single-producer single-consumer (SPSC) lock-free ring buffer for UART.
//! Safe to use between an ISR producer and a main-loop consumer.
//! Works in no_std environments without an allocator.

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicUsize, Ordering};

pub struct RingBuffer<const N: usize> {
    buf:  UnsafeCell<[u8; N]>,
    head: AtomicUsize,  // Written by producer (ISR)
    tail: AtomicUsize,  // Written by consumer (main loop)
}

// SAFETY: Single-producer, single-consumer — safe across ISR/main boundary.
unsafe impl<const N: usize> Sync for RingBuffer<N> {}

impl<const N: usize> RingBuffer<N> {
    /// N must be a power of two for mask-based indexing.
    pub const fn new() -> Self {
        assert!(N.is_power_of_two(), "RingBuffer size must be a power of two");
        Self {
            buf:  UnsafeCell::new([0u8; N]),
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    /// Available bytes to read (consumer side).
    #[inline]
    pub fn len(&self) -> usize {
        let head = self.head.load(Ordering::Acquire);
        let tail = self.tail.load(Ordering::Relaxed);
        head.wrapping_sub(tail) & (N - 1)
    }

    /// Free space for writing (producer side).
    #[inline]
    pub fn free(&self) -> usize {
        N - 1 - self.len()
    }

    /// Push a slice into the buffer. Returns number of bytes actually written.
    /// Called from ISR — no allocation, no locking.
    pub fn push(&self, data: &[u8]) -> usize {
        let head = self.head.load(Ordering::Relaxed);
        let tail = self.tail.load(Ordering::Acquire);
        let free = N - 1 - head.wrapping_sub(tail) & (N - 1);
        let count = data.len().min(free);

        // SAFETY: single producer, indices correctly bounded
        let buf = unsafe { &mut *self.buf.get() };
        for (i, &byte) in data[..count].iter().enumerate() {
            buf[head.wrapping_add(i) & (N - 1)] = byte;
        }

        // Release store: data is visible to consumer before head update
        self.head.store(head.wrapping_add(count), Ordering::Release);
        count
    }

    /// Pop up to `dst.len()` bytes from the buffer.
    /// Called from main loop — no allocation, no locking.
    pub fn pop(&self, dst: &mut [u8]) -> usize {
        let head = self.head.load(Ordering::Acquire);
        let tail = self.tail.load(Ordering::Relaxed);
        let avail = head.wrapping_sub(tail) & (N - 1);
        let count = dst.len().min(avail);

        // SAFETY: single consumer, indices correctly bounded
        let buf = unsafe { &*self.buf.get() };
        for (i, slot) in dst[..count].iter_mut().enumerate() {
            *slot = buf[tail.wrapping_add(i) & (N - 1)];
        }

        // Release store: consumed tail is visible to producer
        self.tail.store(tail.wrapping_add(count), Ordering::Release);
        count
    }
}
```

---

### Example 8: High-Throughput UART on Linux with Rust (tokio async I/O)

```rust
//! High-throughput UART receive on Linux using tokio + serialport.
//! Demonstrates async batched reading with throughput measurement.
//!
//! Cargo.toml:
//!   tokio     = { version = "1", features = ["full"] }
//!   tokio-serial = "5.4"
//!   bytes     = "1"

use std::time::{Duration, Instant};
use tokio::io::AsyncReadExt;
use tokio_serial::SerialPortBuilderExt;
use bytes::BytesMut;

const READ_BUF_SIZE: usize = 8192;   // Large buffer: fewer syscalls
const REPORT_INTERVAL: Duration = Duration::from_secs(5);

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let port_name = "/dev/ttyUSB0";
    let baud_rate = 921_600;

    // Open port — tokio-serial wraps it as an AsyncRead/AsyncWrite
    let mut port = tokio_serial::new(port_name, baud_rate)
        .data_bits(tokio_serial::DataBits::Eight)
        .parity(tokio_serial::Parity::None)
        .stop_bits(tokio_serial::StopBits::One)
        .flow_control(tokio_serial::FlowControl::Hardware)
        .timeout(Duration::from_millis(100))
        .open_native_async()?;

    // Pre-allocated reusable buffer — no per-read allocation
    let mut buf = BytesMut::with_capacity(READ_BUF_SIZE);
    buf.resize(READ_BUF_SIZE, 0);

    let mut total_bytes: u64 = 0;
    let mut report_time = Instant::now();

    println!("Listening on {} at {} baud...", port_name, baud_rate);

    loop {
        // Single async read fills the buffer — yields to tokio while waiting
        match port.read(&mut buf).await {
            Ok(0) => continue,
            Ok(n) => {
                total_bytes += n as u64;
                // Process buf[..n] — batch operation, not byte-by-byte
                process_batch(&buf[..n]);
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
            Err(e) => return Err(e.into()),
        }

        // Periodic throughput report
        if report_time.elapsed() >= REPORT_INTERVAL {
            let elapsed = report_time.elapsed().as_secs_f64();
            let kb_s = total_bytes as f64 / elapsed / 1024.0;
            let theoretical_max_kb_s = baud_rate as f64 / 10.0 / 1024.0;
            let efficiency = kb_s / theoretical_max_kb_s * 100.0;

            println!(
                "Throughput: {:.2} kB/s  ({:.1}% of {:.2} kB/s max)",
                kb_s, efficiency, theoretical_max_kb_s
            );

            total_bytes = 0;
            report_time = Instant::now();
        }
    }
}

fn process_batch(data: &[u8]) {
    // In a real system: parse frames, push to channel, decode protocol
    // Operating on a slice is cache-friendly and branch-predictor-friendly
    let _ = data.iter().fold(0u64, |acc, &b| acc + b as u64); // dummy work
}
```

---

### Example 9: Double-Buffered DMA Transmit in Rust (bare-metal, PAC-level)

```rust
//! Bare-metal double-buffered DMA transmit for STM32 using the PAC.
//! Demonstrates safe abstraction over unsafe hardware access.

use core::sync::atomic::{AtomicBool, AtomicU8, Ordering};

const TX_BUF_SIZE: usize = 1024;

/// Static transmit buffers — two alternating, DMA-accessible.
/// `repr(align)` ensures DMA alignment requirements are met.
#[repr(align(4))]
struct TxBuffers {
    bufs:   [[u8; TX_BUF_SIZE]; 2],
    lens:   [usize; 2],
    active: AtomicU8,
    busy:   AtomicBool,
}

static TX: TxBuffers = TxBuffers {
    bufs:   [[0u8; TX_BUF_SIZE]; 2],
    lens:   [0; 2],
    active: AtomicU8::new(0),
    busy:   AtomicBool::new(false),
};

/// Queue bytes for DMA transmit. Called from application context.
/// Returns the number of bytes accepted (may be less than `data.len()`).
pub fn uart_write(data: &[u8]) -> usize {
    let active = TX.active.load(Ordering::Relaxed) as usize;
    let idle   = active ^ if TX.busy.load(Ordering::Acquire) { 1 } else { 0 };

    // SAFETY: idle buffer is not accessed by DMA
    let idle_buf = unsafe {
        let ptr = TX.bufs.as_ptr().add(idle) as *mut u8;
        core::slice::from_raw_parts_mut(ptr, TX_BUF_SIZE)
    };
    let idle_len = unsafe { &mut *(TX.lens.as_ptr().add(idle) as *mut usize) };

    let space = TX_BUF_SIZE - *idle_len;
    let count = data.len().min(space);
    idle_buf[*idle_len..*idle_len + count].copy_from_slice(&data[..count]);
    *idle_len += count;

    // If DMA is idle, start it now
    if !TX.busy.load(Ordering::Acquire) {
        dma_tx_start(idle);
    }

    count
}

/// Start DMA transmission of buffer `idx`.
/// SAFETY: Must only be called when DMA is idle.
fn dma_tx_start(idx: usize) {
    let len = unsafe { *TX.lens.as_ptr().add(idx) };
    if len == 0 {
        TX.busy.store(false, Ordering::Release);
        return;
    }

    TX.active.store(idx as u8, Ordering::Relaxed);
    TX.busy.store(true, Ordering::Release);

    // Platform-specific DMA configuration would go here:
    // dma_set_source(TX.bufs[idx].as_ptr(), len);
    // dma_enable_tx();
    let _ = idx; // suppress unused warning in this example
}

/// Called from DMA TX-complete interrupt.
#[no_mangle]
pub extern "C" fn DMA1_Stream6_IRQHandler() {
    let prev_active = TX.active.load(Ordering::Relaxed) as usize;
    // Mark the just-completed buffer as empty
    unsafe { *(TX.lens.as_ptr().add(prev_active) as *mut usize) = 0 };

    // Flip to the other buffer
    let next = prev_active ^ 1;
    dma_tx_start(next);
}
```

---

## Double Buffering

Double buffering (also called ping-pong buffering) is the foundational technique that eliminates the most common cause of throughput loss: waiting for a buffer to drain before the CPU can fill it again.

```
Time:     |--- DMA sends Buffer A ---|--- DMA sends Buffer B ---|--- Buffer A ---
CPU:      |--- CPU fills Buffer B ---|--- CPU fills Buffer A ---|--- fills B   ---
```

**Rules for correct double buffering:**

1. The CPU must never write to the buffer currently active in the DMA.
2. An atomic flag (`busy` / `active`) must be updated before the DMA is started, not after.
3. Cache coherency: on Cortex-M7 or application processors with data cache, you must call `SCB_CleanDCache_by_Addr()` (C) or the equivalent cache flush before starting DMA, to ensure the DMA sees your writes and not stale cached data.
4. On the receive side, after DMA completes, call `SCB_InvalidateDCache_by_Addr()` before reading from the buffer so the CPU doesn't see stale cached values.

---

## Flow Control Strategies

Flow control prevents buffer overflows when the receiver cannot keep up with the transmitter.

### Hardware Flow Control (RTS/CTS) — Preferred

- Transmitter drives RTS (Request To Send) low when ready to send.
- Receiver drives CTS (Clear To Send) low when it has buffer space.
- Hardware-level: zero CPU involvement, zero latency, zero overhead.
- Enable with: `tty.c_cflag |= CRTSCTS;` (Linux) or via UART peripheral control register.

### Software Flow Control (XON/XOFF)

- Receiver sends `0x11` (XON) when buffer drops below low watermark.
- Receiver sends `0x13` (XOFF) when buffer exceeds high watermark.
- CPU overhead: each flow-control byte requires ISR processing.
- Latency: the transmitter continues sending for the duration of the round-trip before it sees XOFF — necessitating a large receive buffer.
- Avoid at high baud rates unless hardware flow control is impossible.

### Watermark Tuning

For receive ring buffers, set watermarks as a fraction of buffer size:

```c
#define RX_BUF_SIZE    4096
#define XOFF_WATERMARK (RX_BUF_SIZE * 3 / 4)   /* Assert XOFF at 75% full  */
#define XON_WATERMARK  (RX_BUF_SIZE * 1 / 4)   /* Assert XON  at 25% full  */
```

---

## Baud Rate Selection and Clock Accuracy

### Baud Rate Error

The UART peripheral derives its baud rate by dividing the peripheral clock. The actual achievable rate is:

```
Actual baud = PCLK / round(PCLK / desired_baud)
Error (%)   = |actual - desired| / desired × 100
```

Typical tolerance: ±2% maximum; ±1% recommended for reliable operation at all temperatures. Errors above 2% cause framing errors at high baud rates.

**Check your divider:**

```c
/* Example: PCLK = 84 MHz, desired = 921600 */
uint32_t pclk    = 84000000;
uint32_t baud    = 921600;
uint32_t div     = (pclk + baud/2) / baud;      /* Round to nearest */
uint32_t actual  = pclk / div;
int32_t  err_ppm = ((int32_t)(actual - baud) * 1000000) / (int32_t)baud;
/* err_ppm should be < ±20000 (2%) */
```

### Fractional Baud Rate Generators

Many modern UARTs (STM32 USARTs, NXP FLEXCOMMs) support fractional dividers, reducing baud rate error dramatically. Enable the fractional divider and set both the integer and fractional parts per the datasheet.

### Crystal vs RC Oscillator

- **External crystal/TCXO:** ±20–50 ppm — always use at baud rates ≥ 115,200.
- **Internal RC oscillator:** ±1–3% — acceptable only at ≤ 9,600 baud without calibration.
- **PLL-derived clock:** Check that the PLL multiplication/division chain introduces no additional jitter on the UART peripheral clock.

---

## Interrupt Latency Reduction

### Prioritize the UART ISR

On Cortex-M MCUs, assign the UART interrupt a high priority (low numeric value). This ensures the ISR fires before lower-priority work, reducing the window during which the receive FIFO can overflow.

```c
NVIC_SetPriority(USART1_IRQn, 1);   /* 0 = highest, 15 = lowest on most Cortex-M */
NVIC_EnableIRQ(USART1_IRQn);
```

### Minimize ISR Work

The ISR should do only:
1. Read the UART status register to determine the event.
2. Transfer bytes from the UART/FIFO into a ring buffer (or signal the DMA position).
3. Set a flag or post to a semaphore to wake the processing task.

**Never** do heavy processing, call `printf`, or take mutexes inside the UART ISR.

### Use Tail-Chained Interrupts (Cortex-M)

If the UART ISR completes while another ISR is pending, Cortex-M performs tail-chaining, re-entering without full context save/restore (~6 cycles instead of ~12+). This is automatic — no configuration required — but ISRs must exit cleanly.

---

## FIFO Depth Tuning

The FIFO threshold interrupt fires when the FIFO reaches the configured depth. Choosing the right threshold is a trade-off:

| Threshold | Pros | Cons |
|-----------|------|------|
| **Low (e.g., 1/4 full)** | ISR fires early, plenty of buffer time | More ISR calls, higher CPU overhead |
| **High (e.g., 3/4 full)** | Fewer ISR calls, lower CPU overhead | Less time to service before overflow |

**Recommended approach:** Set RX threshold to 3/4 full and always enable the RX timeout interrupt to catch the final partial FIFO load when data stops arriving.

---

## Protocol-Level Efficiency

Even with optimal hardware configuration, application protocol design can limit throughput:

### Avoid Per-Message Acknowledgment at High Rates

A request-response protocol where the transmitter waits for an ACK before sending the next frame limits throughput to roughly:

```
Effective rate ≈ frame_size / (frame_tx_time + round_trip_latency)
```

At 921,600 baud, a 64-byte frame takes 0.7 ms. A round-trip ACK adds another 0.7–5 ms, reducing throughput by 50–85%. Use **sliding window** or **streaming** protocols instead.

### Batching and Packetization

Group small messages into larger frames. A 256-byte frame has only 0.4% overhead for a 4-byte header, versus a 4-byte message with a 4-byte header having 100% overhead.

### Checksum Selection

- **CRC-32:** ~4 CPU cycles per byte on MCUs with hardware CRC — use it.
- **CRC-16:** Similar cost, lower error detection.
- **Simple sum:** Fastest but misses many error patterns.
- **SHA/HMAC:** Expensive — only for security-critical paths, not throughput-sensitive streams.

---

## Benchmarking and Measurement

You cannot optimize what you cannot measure. Use these techniques to quantify real-world UART throughput.

### Toggle a GPIO Pin

The simplest and most accurate method: toggle a GPIO at the start and end of a DMA transfer or ISR entry/exit and measure with a logic analyzer or oscilloscope.

```c
/* In DMA TX complete callback */
GPIO_SetHigh(DBG_PIN);    /* Mark DMA active */
dma_tx_start(next_buf);
GPIO_SetLow(DBG_PIN);     /* DMA complete */
```

### Timestamp Counter

Use the cycle counter (DWT->CYCCNT on Cortex-M) for sub-microsecond timing without a logic analyzer:

```c
#define DWT_CYCCNT   (*(volatile uint32_t *)0xE0001004)
#define DWT_CTRL     (*(volatile uint32_t *)0xE0001000)

/* Enable cycle counter */
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT_CTRL |= 1;

uint32_t t0 = DWT_CYCCNT;
uart_tx_poll_fast(UART1, data, len);
uint32_t t1 = DWT_CYCCNT;
uint32_t cycles = t1 - t0;
/* At 168 MHz: time_us = cycles / 168 */
```

### Throughput Calculator

```c
/**
 * Running throughput statistics — safe for use in logging callbacks.
 */
typedef struct {
    uint32_t bytes_total;
    uint32_t window_start_ms;
    float    last_kbps;
} ThroughputStats;

void stats_update(ThroughputStats *s, uint32_t bytes, uint32_t now_ms) {
    s->bytes_total += bytes;
    uint32_t elapsed = now_ms - s->window_start_ms;
    if (elapsed >= 1000) {
        s->last_kbps = (float)s->bytes_total / elapsed;  /* kB/s */
        s->bytes_total = 0;
        s->window_start_ms = now_ms;
    }
}
```

---

## Summary

UART throughput maximization is a multi-layered discipline. The following table ranks optimizations by typical impact:

| Rank | Optimization | Typical Gain | Complexity |
|------|-------------|-------------|------------|
| 1 | **DMA circular receive + idle-line interrupt** | 60–90% CPU reduction | Medium |
| 2 | **DMA double-buffered transmit** | Eliminates TX stalls | Medium |
| 3 | **Hardware RTS/CTS flow control** | Prevents overrun at any speed | Low |
| 4 | **Large batched reads (VMIN/VTIME on Linux)** | 10–50× fewer syscalls | Low |
| 5 | **FIFO threshold tuning** | Reduces ISR frequency 4–16× | Low |
| 6 | **Lock-free SPSC ring buffer** | Eliminates mutex overhead | Medium |
| 7 | **Cache-aligned DMA buffers** | 10–30% DMA bus efficiency | Low |
| 8 | **Fractional baud rate generator** | Reduces framing errors | Low |
| 9 | **Streaming protocol (no per-frame ACK)** | 2–10× protocol efficiency | High |
| 10 | **Zero-copy scatter-gather transmit** | Eliminates memcpy overhead | Medium |

### Key Principles to Remember

**DMA is the foundation.** Any high-throughput UART system that moves bytes through CPU registers is wasting potential. Let the DMA do the heavy lifting.

**Batch everything.** Whether it is read() calls on Linux or application-level frame assembly, more data per operation means less per-byte overhead.

**Measure before and after.** Hardware behavior is non-intuitive. Always validate that an optimization actually improved throughput for your specific hardware and workload.

**The ISR is sacred.** Keep interrupt handlers minimal — copy data, set a flag, exit. All processing belongs in task or main-loop context.

**Match your buffers to your baud rate.** At 921,600 baud, the UART produces ~90 kB/s. Your ring buffer must be large enough to absorb at least 10–50 ms of data between processing iterations — that is 900 bytes to 4,500 bytes minimum.

With DMA, double buffering, correct FIFO configuration, hardware flow control, and appropriately sized buffers, it is routinely possible to achieve 95–99% of the theoretical maximum throughput of a UART link across C/C++ and Rust implementations alike.

---

*This document is part of the UART Programming Reference series. See also:*
*28\_Error\_Detection\_Correction.md | 29\_Power\_Management.md | 31\_Multi\_UART\_Management.md*