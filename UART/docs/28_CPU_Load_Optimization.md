# 28. UART CPU Load Optimization

- **The Core Problem** — quantifies exactly how many CPU cycles are at stake per UART character at 115200 baud, grounding the rest of the discussion
- **Three Strategies** — polling (busy-wait and timed), interrupt-driven, and DMA — each with trade-offs clearly stated
- **Hybrid approaches** — DMA + idle-line interrupt, RTOS + ISR + queue
- **Ring buffer deep-dive** — why it's essential, how head/tail pointers work safely without locks

**Code examples (C/C++ and Rust):**

| Example | Language | Technique |
|---|---|---|
| Busy-wait + timed polling | C | Bare-metal register-level |
| Interrupt-driven + ring buffer | C | Bare-metal, full ISR + API |
| DMA + idle-line detection | C | STM32 HAL |
| RTOS task + ring buffer | C++ | FreeRTOS + heapless queue |
| Polling with embedded-hal | Rust | Portable via traits |
| Interrupt-driven | Rust | RTIC framework |
| DMA RX/TX | Rust | stm32f4xx-hal DMA API |

**Key pitfalls documented:** missing TC flag on TX shutdown, ISRs that do too much, volatile omission, DMA cache coherency on Cortex-M7/A, and premature TXEIE enable.

### Balancing Polling vs Interrupt Strategies for Efficiency

---

## Table of Contents

1. [Introduction](#introduction)
2. [The Core Problem: CPU Time and UART](#the-core-problem-cpu-time-and-uart)
3. [Strategy 1: Polling](#strategy-1-polling)
   - [Busy-Wait Polling](#busy-wait-polling)
   - [Timed / Periodic Polling](#timed--periodic-polling)
4. [Strategy 2: Interrupt-Driven UART](#strategy-2-interrupt-driven-uart)
   - [Receive Interrupt](#receive-interrupt)
   - [Transmit Interrupt](#transmit-interrupt)
5. [Strategy 3: DMA-Assisted UART](#strategy-3-dma-assisted-uart)
6. [Hybrid Strategies](#hybrid-strategies)
7. [Ring Buffer Implementation](#ring-buffer-implementation)
8. [C/C++ Code Examples](#cc-code-examples)
   - [Polling Example (Bare-Metal C)](#polling-example-bare-metal-c)
   - [Interrupt-Driven Example (Bare-Metal C)](#interrupt-driven-example-bare-metal-c)
   - [DMA-Driven Example (STM32 HAL C)](#dma-driven-example-stm32-hal-c)
   - [RTOS Task with Ring Buffer (C++)](#rtos-task-with-ring-buffer-c)
9. [Rust Code Examples](#rust-code-examples)
   - [Polling Example (Rust embedded-hal)](#polling-example-rust-embedded-hal)
   - [Interrupt-Driven Example (Rust RTIC)](#interrupt-driven-example-rust-rtic)
   - [DMA-Driven Example (Rust)](#dma-driven-example-rust)
10. [Performance Comparison Table](#performance-comparison-table)
11. [Decision Framework](#decision-framework)
12. [Common Pitfalls and Best Practices](#common-pitfalls-and-best-practices)
13. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) is one of the most common serial communication interfaces in embedded systems. While UART itself is conceptually simple, its efficient integration into a system — without wasting CPU cycles — is a nuanced engineering challenge.

The central question is: **how does the CPU know when UART data has arrived or when the transmit buffer is ready for more data?** The answer determines how much CPU time is consumed by UART handling, how responsive the system is to other tasks, and how robust it is under heavy load.

There are three fundamental approaches:

- **Polling** — the CPU continuously or periodically checks UART status registers.
- **Interrupts** — the UART hardware signals the CPU only when something needs attention.
- **DMA (Direct Memory Access)** — data is transferred between UART hardware and memory with minimal CPU involvement.

Choosing the right strategy — or combining them — is the heart of UART CPU load optimization.

---

## The Core Problem: CPU Time and UART

Consider a UART running at 115200 baud. Each character is 10 bits (start + 8 data + stop), giving:

```
Characters per second = 115200 / 10 = 11,520 chars/sec
Time per character    = ~86.8 µs
```

On a 72 MHz microcontroller, 86.8 µs represents approximately **6,250 CPU cycles** per character. If the CPU is stuck polling a status register waiting for each character, it burns those cycles doing nothing useful. At higher baud rates (1 Mbaud, 5 Mbaud), the window shrinks dramatically, but the fundamental trade-off remains.

The goal of CPU load optimization is to ensure the CPU is **busy doing useful work** rather than waiting for UART events — while still processing every byte reliably and without loss.

---

## Strategy 1: Polling

### Busy-Wait Polling

The CPU continuously reads the UART status register in a tight loop until data is available.

**Advantages:**
- Simple to implement and debug.
- Deterministic timing — data is processed the instant it is seen.
- No interrupt latency or context-switch overhead.

**Disadvantages:**
- CPU is 100% occupied during the wait — no other work is done.
- Unacceptable in multitasking systems unless used very briefly.
- Power consumption stays high because the CPU never enters sleep.

**Suitable for:** initialization sequences, bootloaders, single-purpose diagnostic tools, or very short blocking transfers where simplicity outweighs efficiency.

### Timed / Periodic Polling

Instead of a tight loop, the CPU checks the UART status register at regular intervals — e.g., from a timer ISR or RTOS tick — and returns to other work in between.

**Advantages:**
- Frees the CPU between checks.
- Easy to integrate with cooperative schedulers.

**Disadvantages:**
- Introduces latency equal to the polling period.
- If the polling period is longer than the time for the UART FIFO to overflow, data can be lost.
- Still wastes some CPU time even when no data is present.

**Suitable for:** low baud-rate links with a hardware FIFO, or systems with a well-defined polling budget.

---

## Strategy 2: Interrupt-Driven UART

The UART peripheral raises a hardware interrupt when:
- A byte has been received (RX interrupt).
- The transmit holding register is empty and ready for a new byte (TX interrupt / TXRDY).
- An error condition has occurred (framing, parity, overrun).

The CPU can sleep or perform other work between interrupts, waking up only when the UART needs service.

### Receive Interrupt

When a byte arrives, the UART asserts its RX interrupt line. The CPU's interrupt controller (e.g., NVIC on ARM Cortex-M) suspends the current task, runs the ISR, and returns. The ISR should:

1. Read the received byte from the data register.
2. Store it in a ring buffer (circular buffer).
3. Return — any heavy processing happens in the main loop or a task.

### Transmit Interrupt

When the TX holding register empties, a TX interrupt fires. The ISR:

1. Checks the ring buffer for pending output bytes.
2. If bytes are available, writes one to the TX data register.
3. If the buffer is empty, disables the TX interrupt to stop future firings.

This ping-pong pattern means the CPU is only called upon when the hardware is actually ready, not in a spinning loop.

**Advantages:**
- Very low CPU load at moderate baud rates.
- Responsive and low-latency.
- Works well with RTOS — the ISR posts a semaphore or message, a task handles processing.

**Disadvantages:**
- Interrupt overhead (register save/restore, ~10–50 cycles on Cortex-M) per byte.
- At very high baud rates (> 1 Mbaud), interrupt-per-byte can itself become a CPU load bottleneck.
- ISR must be kept short — no blocking, no heavy computation.

**Suitable for:** the majority of embedded UART use cases. The interrupt-driven approach with a ring buffer is the industry standard.

---

## Strategy 3: DMA-Assisted UART

DMA allows the UART hardware to deposit received bytes directly into a memory buffer — or send bytes from memory — without CPU intervention. The CPU is only interrupted when a complete block has been transferred (or half-transferred, for double-buffering).

**Receive with DMA:**
The DMA controller is configured with a destination buffer address and a transfer count. Each byte received by the UART triggers a DMA request; the DMA controller writes the byte and decrements its counter. The CPU receives a single interrupt when the buffer is full (or half-full).

**Transmit with DMA:**
The CPU prepares a buffer of bytes to send, configures the DMA, and the DMA feeds bytes to the UART automatically at the UART's pace. One interrupt fires when the transfer completes.

**Advantages:**
- Extremely low CPU load — one interrupt per buffer, not per byte.
- Ideal for high-throughput links (1 Mbaud+).
- Enables true zero-copy data paths.

**Disadvantages:**
- More complex setup (DMA channel configuration, memory alignment, cache coherency on Cortex-A).
- Fixed buffer sizes — requires careful design for variable-length packets.
- Idle-line detection or timeout mechanisms needed when messages are shorter than the DMA buffer.

**Suitable for:** high-speed data logging, protocol bridges, streaming audio/sensor data, or any scenario with large or continuous data flows.

---

## Hybrid Strategies

Real-world systems often combine strategies:

- **DMA for RX, interrupt for TX** — receive large blocks via DMA; use interrupt-driven ring buffer for transmit (where bursts are smaller).
- **DMA with UART idle-line interrupt** — configure DMA for a maximum packet size, but also enable the UART idle-line interrupt. When the line goes idle after a burst of bytes, the ISR terminates the DMA transfer early and processes whatever was received.
- **RTOS + interrupt + ring buffer** — the ISR fills the ring buffer, a high-priority task drains it, a lower-priority task processes the data. This separates concerns and allows priority-based scheduling.

---

## Ring Buffer Implementation

A ring buffer (circular buffer) is the cornerstone of efficient interrupt-driven UART. It decouples the ISR (producer) from the application (consumer) without requiring locks if one writer and one reader access separate indices.

```
  head →  [ D ][ A ][ T ][ A ][ _ ][ _ ][ _ ][ _ ] ← tail
```

- The **ISR writes** to `head` and advances it.
- The **application reads** from `tail` and advances it.
- Buffer is **full** when `(head + 1) % SIZE == tail`.
- Buffer is **empty** when `head == tail`.

For safe single-producer/single-consumer access on a single-core system (no cache coherency issues), no mutex is needed — only `volatile` qualifiers and memory barriers where required.

---

## C/C++ Code Examples

### Polling Example (Bare-Metal C)

```c
/* ============================================================
 * UART CPU Load Optimization — Strategy 1: Busy-Wait Polling
 * Target: Generic bare-metal (e.g., STM32 register-level)
 * ============================================================ */

#include <stdint.h>
#include "stm32f4xx.h"   /* or your MCU header */

/* Wait until TX register is empty, then send one byte.
 * WARNING: Blocks until UART is ready — 100% CPU usage during wait. */
void uart_poll_send_byte(USART_TypeDef *uart, uint8_t byte)
{
    /* TXE (Transmit Data Register Empty) bit in SR */
    while (!(uart->SR & USART_SR_TXE))
    {
        /* Spin — CPU does nothing else here */
    }
    uart->DR = byte;
}

/* Send a null-terminated string — blocks until every byte is sent. */
void uart_poll_send_string(USART_TypeDef *uart, const char *str)
{
    while (*str)
    {
        uart_poll_send_byte(uart, (uint8_t)*str++);
    }
    /* Wait for transmission complete (TC) before returning */
    while (!(uart->SR & USART_SR_TC))
    {
        /* Spin */
    }
}

/* Receive one byte — blocks indefinitely until data arrives. */
uint8_t uart_poll_receive_byte(USART_TypeDef *uart)
{
    while (!(uart->SR & USART_SR_RXNE))
    {
        /* RXNE = RX Not Empty: spin until set */
    }
    return (uint8_t)uart->DR;
}

/* Timed polling variant: call this from a periodic timer ISR or RTOS tick.
 * Returns 1 if a byte was available, 0 if not.
 * Non-blocking — returns immediately regardless of data availability. */
int uart_timed_poll(USART_TypeDef *uart, uint8_t *out_byte)
{
    if (uart->SR & USART_SR_RXNE)
    {
        *out_byte = (uint8_t)uart->DR;
        return 1;  /* data available */
    }
    return 0;  /* nothing to read this tick */
}
```

---

### Interrupt-Driven Example (Bare-Metal C)

```c
/* ============================================================
 * UART CPU Load Optimization — Strategy 2: Interrupt-Driven
 * with Ring Buffer (Single-Producer / Single-Consumer)
 * Target: ARM Cortex-M (STM32-style registers)
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include "stm32f4xx.h"

/* ---- Ring Buffer ------------------------------------------ */

#define RX_BUF_SIZE  256u   /* Must be a power of 2 for fast wrap */
#define TX_BUF_SIZE  256u

typedef struct {
    volatile uint8_t  buf[RX_BUF_SIZE];
    volatile uint32_t head;   /* Written by ISR  */
    volatile uint32_t tail;   /* Read  by app    */
} RingBuf;

static RingBuf rx_ring = {0};
static RingBuf tx_ring = {0};

/* Returns number of bytes available in ring buffer */
static inline uint32_t ring_count(const RingBuf *r)
{
    return (r->head - r->tail) & (RX_BUF_SIZE - 1u);
}

/* Returns 1 if ring buffer has space */
static inline int ring_can_write(const RingBuf *r)
{
    return ((r->head + 1u) & (RX_BUF_SIZE - 1u)) != r->tail;
}

/* ISR-safe push — called from ISR only */
static inline void ring_push(RingBuf *r, uint8_t byte)
{
    uint32_t next_head = (r->head + 1u) & (RX_BUF_SIZE - 1u);
    if (next_head != r->tail)   /* not full */
    {
        r->buf[r->head] = byte;
        r->head = next_head;
    }
    /* If full, byte is silently dropped — add overrun counter in production */
}

/* App-safe pop — called from main/task only */
static inline int ring_pop(RingBuf *r, uint8_t *out)
{
    if (r->tail == r->head)
        return 0;   /* empty */
    *out = r->buf[r->tail];
    r->tail = (r->tail + 1u) & (RX_BUF_SIZE - 1u);
    return 1;
}

/* ---- UART Initialization ---------------------------------- */

void uart_interrupt_init(USART_TypeDef *uart, uint32_t baud_rate,
                         uint32_t periph_clk_hz)
{
    /* Set baud rate divisor */
    uart->BRR = periph_clk_hz / baud_rate;

    /* Enable UART, TX, RX */
    uart->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    /* Enable RX-Not-Empty interrupt.
     * TX-Empty interrupt (TXEIE) is enabled only when we have data to send. */
    uart->CR1 |= USART_CR1_RXNEIE;

    /* Enable UART IRQ in NVIC — priority should be chosen carefully */
    NVIC_SetPriority(USART1_IRQn, 5);
    NVIC_EnableIRQ(USART1_IRQn);
}

/* ---- ISR -------------------------------------------------- */

/* This ISR handles both RX and TX events for USART1.
 * Keep it SHORT — no blocking calls, no printf. */
void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;

    /* --- RX: byte received --------------------------------- */
    if (sr & USART_SR_RXNE)
    {
        uint8_t byte = (uint8_t)USART1->DR;   /* Reading DR clears RXNE */
        ring_push(&rx_ring, byte);
    }

    /* --- TX: holding register empty, send next byte -------- */
    if ((sr & USART_SR_TXE) && (USART1->CR1 & USART_CR1_TXEIE))
    {
        uint8_t byte;
        if (ring_pop(&tx_ring, &byte))
        {
            USART1->DR = byte;   /* Load next byte */
        }
        else
        {
            /* Nothing left to send — disable TXE interrupt to stop firing */
            USART1->CR1 &= ~USART_CR1_TXEIE;
        }
    }

    /* --- Error handling (optional but recommended) --------- */
    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_PE))
    {
        volatile uint32_t dummy = USART1->DR;  /* Clear error flags by reading DR */
        (void)dummy;
    }
}

/* ---- Application API -------------------------------------- */

/* Queue bytes for transmission. Enables TXE interrupt to start sending.
 * Safe to call from main loop or RTOS task. */
int uart_send(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        /* Disable IRQ briefly to safely push to TX ring */
        __disable_irq();
        int ok = ring_can_write(&tx_ring);
        if (ok)
            ring_push(&tx_ring, data[i]);
        /* Enable TXE interrupt — ISR will drain the buffer */
        USART1->CR1 |= USART_CR1_TXEIE;
        __enable_irq();

        if (!ok)
            return -1;  /* Buffer full — caller should retry or block */
    }
    return 0;
}

/* Read received bytes from the ring buffer. Non-blocking.
 * Returns number of bytes actually read. */
size_t uart_receive(uint8_t *buf, size_t max_len)
{
    size_t count = 0;
    while (count < max_len && ring_pop(&rx_ring, &buf[count]))
        count++;
    return count;
}
```

---

### DMA-Driven Example (STM32 HAL C)

```c
/* ============================================================
 * UART CPU Load Optimization — Strategy 3: DMA + Idle-Line
 * Target: STM32 using HAL (easily adapted to LL or bare-metal)
 *
 * Technique: DMA receives into a double buffer. UART idle-line
 * interrupt signals end of packet without waiting for DMA full.
 * CPU cost: ONE interrupt per message, regardless of size.
 * ============================================================ */

#include "stm32f4xx_hal.h"
#include <string.h>

#define DMA_BUF_SIZE  512

/* Double buffer: DMA writes to one half while app processes other */
static uint8_t  dma_buf_a[DMA_BUF_SIZE];
static uint8_t  dma_buf_b[DMA_BUF_SIZE];
static uint8_t *active_buf   = dma_buf_a;
static uint8_t *process_buf  = dma_buf_b;

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef  hdma_usart1_rx;

/* ---- Initialization --------------------------------------- */

void uart_dma_init(void)
{
    /* Start DMA receive into active_buf — circular mode keeps it running */
    HAL_UART_Receive_DMA(&huart1, active_buf, DMA_BUF_SIZE);

    /* Enable UART Idle Line Detection interrupt — fires when line goes idle */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
}

/* ---- UART IRQ Handler ------------------------------------- */
/* Add this call inside the USART1_IRQHandler in stm32f4xx_it.c */

void uart_dma_idle_handler(UART_HandleTypeDef *huart)
{
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(huart);

        /* Calculate how many bytes DMA has written so far */
        uint32_t dma_remaining = __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
        uint32_t bytes_received = DMA_BUF_SIZE - dma_remaining;

        if (bytes_received > 0)
        {
            /* Swap buffers: hand the filled buffer to the app,
             * restart DMA on the fresh buffer — zero data loss. */
            uint8_t *filled = active_buf;
            active_buf      = process_buf;
            process_buf     = filled;

            /* Restart DMA on the new active buffer */
            HAL_UART_AbortReceive(huart);
            HAL_UART_Receive_DMA(huart, active_buf, DMA_BUF_SIZE);

            /* Notify application — e.g., post to RTOS queue */
            uart_notify_data_ready(process_buf, bytes_received);
        }
    }
}

/* ---- Application Processing (called from task/main) ------- */

/* CPU only runs this when a complete message has arrived.
 * Between messages: CPU is free for other work, DMA runs autonomously. */
void uart_notify_data_ready(const uint8_t *data, uint32_t len)
{
    /* Example: pass to protocol parser */
    protocol_parse(data, len);
}

/* ---- DMA TX: Send a buffer with zero CPU involvement ------- */

static volatile int tx_complete = 1;

void uart_dma_send(const uint8_t *data, uint32_t len)
{
    /* Wait for previous TX to finish (could use semaphore in RTOS) */
    while (!tx_complete)
        ;
    tx_complete = 0;
    HAL_UART_Transmit_DMA(&huart1, (uint8_t *)data, len);
}

/* HAL callback — called by HAL from DMA TX complete ISR */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
        tx_complete = 1;
}
```

---

### RTOS Task with Ring Buffer (C++)

```cpp
/* ============================================================
 * UART CPU Load Optimization — RTOS Integration (FreeRTOS)
 * Pattern: ISR → ring buffer → binary semaphore → task
 * This pattern decouples ISR timing from application processing.
 * ============================================================ */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <cstdint>
#include <cstring>
#include <array>

/* ---- Thread-Safe Ring Buffer (C++) ----------------------- */

template<std::size_t N>
class UartRingBuffer {
    static_assert((N & (N - 1)) == 0, "Size must be power of 2");
public:
    bool push_from_isr(uint8_t byte)
    {
        uint32_t next = (head_ + 1u) & mask_;
        if (next == tail_) return false;   /* full */
        buf_[head_] = byte;
        head_ = next;
        return true;
    }

    bool pop(uint8_t &out)
    {
        if (head_ == tail_) return false;  /* empty */
        out   = buf_[tail_];
        tail_ = (tail_ + 1u) & mask_;
        return true;
    }

    uint32_t available() const
    {
        return (head_ - tail_) & mask_;
    }

private:
    static constexpr uint32_t mask_ = N - 1u;
    volatile std::array<uint8_t, N> buf_{};
    volatile uint32_t head_{0};
    volatile uint32_t tail_{0};
};

static UartRingBuffer<512> g_rx_buf;
static SemaphoreHandle_t   g_rx_sem;   /* Binary semaphore */

/* ---- ISR (called from hardware interrupt) ----------------- */

extern "C" void USART1_IRQHandler(void)
{
    BaseType_t higher_prio_woken = pdFALSE;

    if (/* RXNE flag set */ true)
    {
        uint8_t byte = 0; /* = (uint8_t)USART1->DR; */
        g_rx_buf.push_from_isr(byte);

        /* Signal the UART task — does NOT context-switch here */
        xSemaphoreGiveFromISR(g_rx_sem, &higher_prio_woken);
    }

    /* If a higher-priority task was unblocked, yield to it now */
    portYIELD_FROM_ISR(higher_prio_woken);
}

/* ---- UART Receive Task ------------------------------------ */

/* This task blocks on the semaphore. The CPU is free (or idle)
 * between UART events — only wakes when ISR provides data. */
void uart_receive_task(void *param)
{
    (void)param;
    uint8_t packet[128];
    std::size_t packet_len = 0;

    for (;;)
    {
        /* Block until ISR signals — zero CPU load while waiting */
        if (xSemaphoreTake(g_rx_sem, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            uint8_t byte;
            while (g_rx_buf.pop(byte))
            {
                packet[packet_len++] = byte;

                /* Example: look for line ending as packet delimiter */
                if (byte == '\n' || packet_len >= sizeof(packet))
                {
                    process_packet(packet, packet_len);
                    packet_len = 0;
                }
            }
        }
        else
        {
            /* Timeout: check for partial packets, send keep-alive, etc. */
        }
    }
}

/* ---- Startup ---------------------------------------------- */

void uart_rtos_init(void)
{
    g_rx_sem = xSemaphoreCreateBinary();
    xTaskCreate(uart_receive_task, "UartRx", 512, nullptr, 5, nullptr);
}
```

---

## Rust Code Examples

### Polling Example (Rust embedded-hal)

```rust
// ============================================================
// UART CPU Load Optimization — Strategy 1: Polling
// Uses embedded-hal traits for portability across MCUs
// ============================================================

use embedded_hal::serial::{Read, Write};
use nb::block;   // nb = non-blocking: block! spins until ready

/// Send a byte using busy-wait polling.
/// Blocks until the UART transmit register is ready.
/// CPU load: 100% during wait — use sparingly.
pub fn uart_poll_send_byte<T>(uart: &mut T, byte: u8)
where
    T: Write<u8>,
{
    // block! converts nb::Result into a blocking call
    block!(uart.write(byte)).unwrap_or(());
}

/// Send a byte slice — blocks until all bytes are sent.
pub fn uart_poll_send<T>(uart: &mut T, data: &[u8])
where
    T: Write<u8>,
{
    for &byte in data {
        block!(uart.write(byte)).unwrap_or(());
    }
    // Flush ensures TC (transmission complete) before returning
    block!(uart.flush()).unwrap_or(());
}

/// Non-blocking receive attempt — returns Ok(byte) or WouldBlock.
/// Call this from a periodic timer task for timed polling.
pub fn uart_try_receive<T>(uart: &mut T) -> Option<u8>
where
    T: Read<u8>,
{
    match uart.read() {
        Ok(byte)                => Some(byte),
        Err(nb::Error::WouldBlock) => None,         // Not ready — try again later
        Err(nb::Error::Other(_))   => None,         // Error — handle in production
    }
}

/// Example: timed polling called every millisecond from a timer ISR.
/// accumulate_fn is a closure that stores received bytes.
pub fn uart_timed_poll_tick<T, F>(uart: &mut T, mut accumulate: F)
where
    T: Read<u8>,
    F: FnMut(u8),
{
    // Non-blocking — returns immediately, drains any available bytes
    while let Some(byte) = uart_try_receive(uart) {
        accumulate(byte);
    }
}
```

---

### Interrupt-Driven Example (Rust RTIC)

```rust
// ============================================================
// UART CPU Load Optimization — Strategy 2: Interrupt-Driven
// Using RTIC (Real-Time Interrupt-driven Concurrency) framework
// Target: STM32F4 via stm32f4xx-hal
//
// RTIC enforces safe shared-resource access at compile time —
// no mutexes needed for resources accessed by a single priority.
// ============================================================

#![no_std]
#![no_main]

use panic_halt as _;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{config::Config, Event, Serial},
};
use heapless::spsc::{Consumer, Producer, Queue};  // lock-free SPSC queue

const BUF_SIZE: usize = 256;

// heapless::spsc::Queue is a lock-free single-producer/single-consumer queue
// perfectly suited for ISR → task data passing
static mut RX_QUEUE: Queue<u8, BUF_SIZE> = Queue::new();

#[rtic::app(device = pac, peripherals = true)]
mod app {
    use super::*;

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        rx_prod: Producer<'static, u8, BUF_SIZE>,
        rx_cons: Consumer<'static, u8, BUF_SIZE>,
        serial:  Serial<pac::USART1>,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local, init::Monotonics) {
        let rcc = ctx.device.RCC.constrain();
        let clocks = rcc.cfgr.use_hse(8.MHz()).sysclk(72.MHz()).freeze();

        let gpioa = ctx.device.GPIOA.split();
        let tx_pin = gpioa.pa9.into_alternate();
        let rx_pin = gpioa.pa10.into_alternate();

        let mut serial = Serial::new(
            ctx.device.USART1,
            (tx_pin, rx_pin),
            Config::default().baudrate(115_200.bps()),
            &clocks,
        )
        .unwrap();

        // Enable RX interrupt — fires when a byte arrives
        serial.listen(Event::Rxne);

        let (prod, cons) = unsafe { RX_QUEUE.split() };

        (
            Shared {},
            Local { rx_prod: prod, rx_cons: cons, serial },
            init::Monotonics(),
        )
    }

    // ---- UART ISR (hardware interrupt, highest priority) ----
    // RTIC routes USART1 IRQ here automatically.
    // Accesses rx_prod (local to this task — no lock needed).
    #[task(binds = USART1, local = [serial, rx_prod])]
    fn usart1_isr(ctx: usart1_isr::Context) {
        let serial   = ctx.local.serial;
        let rx_prod  = ctx.local.rx_prod;

        // Read byte — clears RXNE flag
        if let Ok(byte) = serial.read() {
            // enqueue never blocks — if full, byte is lost
            // In production: track overruns
            let _ = rx_prod.enqueue(byte);
        }

        // If TX interrupt enabled and TX ready, send next byte
        // (TX ring buffer handling would go here)
    }

    // ---- Application task (runs at lower priority) ----------
    // Blocks efficiently using RTIC's async or idle loop.
    // Called when RTIC determines it should run (cooperative).
    #[idle(local = [rx_cons])]
    fn idle(ctx: idle::Context) -> ! {
        let rx_cons = ctx.local.rx_cons;

        loop {
            // Drain all available bytes from the queue
            while let Some(byte) = rx_cons.dequeue() {
                process_byte(byte);
            }

            // With RTIC, you can call cortex_m::asm::wfi() here
            // to sleep until the next interrupt — zero idle CPU load
            cortex_m::asm::wfi();
        }
    }
}

fn process_byte(byte: u8) {
    // Protocol parsing, buffering, command dispatch, etc.
    // This runs in the idle task — ISR is never blocked by this logic.
    let _ = byte;
}
```

---

### DMA-Driven Example (Rust)

```rust
// ============================================================
// UART CPU Load Optimization — Strategy 3: DMA Transfer
// Using stm32f4xx-hal DMA + UART
//
// The HAL's transfer() method uses DMA to send/receive a full
// buffer. CPU is free during transfer; one interrupt at completion.
// ============================================================

#![no_std]
#![no_main]

use panic_halt as _;
use cortex_m_rt::entry;
use stm32f4xx_hal::{
    dma::{StreamsTuple, Transfer, MemoryToPeripheral, PeripheralToMemory},
    pac,
    prelude::*,
    serial::{Config, Serial},
};

const MSG_BUF_SIZE: usize = 128;

#[entry]
fn main() -> ! {
    let dp  = pac::Peripherals::take().unwrap();
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(72.MHz()).freeze();

    let gpioa  = dp.GPIOA.split();
    let tx_pin = gpioa.pa9.into_alternate();
    let rx_pin = gpioa.pa10.into_alternate();

    // Split serial into independent TX and RX handles
    let serial = Serial::new(
        dp.USART1,
        (tx_pin, rx_pin),
        Config::default().baudrate(115_200.bps()),
        &clocks,
    )
    .unwrap();
    let (tx, rx) = serial.split();

    // Set up DMA streams
    let streams = StreamsTuple::new(dp.DMA2);

    // ---- DMA TX: transmit a buffer with zero CPU involvement ----
    // 'static buffer required for DMA safety (must outlive transfer)
    let tx_buf: &'static mut [u8; MSG_BUF_SIZE] =
        cortex_m::singleton!(: [u8; MSG_BUF_SIZE] = [0u8; MSG_BUF_SIZE]).unwrap();

    // Fill the TX buffer with data
    let message = b"Hello from DMA UART!\r\n";
    tx_buf[..message.len()].copy_from_slice(message);

    // Start DMA transfer: CPU sets it up once, DMA handles the rest
    let tx_transfer = Transfer::init_memory_to_peripheral(
        streams.7,   // DMA2 Stream7 for USART1_TX
        tx,
        tx_buf as &'static mut [u8],
        None,
        Default::default(),
    );
    tx_transfer.start(|_serial| {});

    // CPU is free here — DMA is transmitting autonomously
    // You could do other work, or sleep:
    // cortex_m::asm::wfi();

    // ---- DMA RX: receive a packet with zero per-byte CPU cost ----
    let rx_buf: &'static mut [u8; MSG_BUF_SIZE] =
        cortex_m::singleton!(: [u8; MSG_BUF_SIZE] = [0u8; MSG_BUF_SIZE]).unwrap();

    let mut rx_transfer = Transfer::init_peripheral_to_memory(
        streams.5,   // DMA2 Stream5 for USART1_RX
        rx,
        rx_buf as &'static mut [u8],
        None,
        Default::default(),
    );
    rx_transfer.start(|_serial| {});

    // Blocking wait for DMA RX complete (in real system: use interrupt/semaphore)
    // is_done() returns true when DMA counter reaches zero
    while !rx_transfer.is_done() {
        cortex_m::asm::nop();   // In production: sleep or do other work
    }

    // At this point, rx_buf contains the received data — process it
    let (returned_buf, _, _, _) = rx_transfer.free();
    process_received_dma(returned_buf);

    loop {
        cortex_m::asm::wfi();   // Sleep until next event
    }
}

fn process_received_dma(buf: &[u8]) {
    // All bytes available at once — parse complete packet
    let _ = buf;
}
```

---

## Performance Comparison Table

| Strategy | CPU Load (typical) | Latency | Complexity | Baud Rate Fit | Power |
|---|---|---|---|---|---|
| Busy-wait polling | ~100% | Minimal | Very Low | < 9600 | High |
| Timed polling | Low–Medium | 1 poll period | Low | < 115200 | Medium |
| Interrupt per byte | Very Low | ~1–10 µs | Medium | Up to 1 Mbaud | Low |
| DMA block transfer | Near Zero | Buffer fill time | High | Any | Very Low |
| DMA + idle-line IRQ | Near Zero | End of message | High | Any | Very Low |
| RTOS + ISR + queue | Very Low | ~1 tick | Medium-High | Up to 1 Mbaud | Low |

---

## Decision Framework

```
Is the UART transfer only during startup/initialization?
    └─ YES → Busy-wait polling is acceptable and simple.

Is baud rate ≤ 9600 and system is single-tasking?
    └─ YES → Timed polling is reasonable.

Is baud rate between 9600 and 1 Mbaud?
    └─ YES → Interrupt-driven with ring buffer is the standard choice.
             Add RTOS semaphore/queue if running a multitasking system.

Is baud rate > 1 Mbaud, or are packets large (> 64 bytes)?
    └─ YES → Use DMA.
             Add idle-line interrupt if packets are variable length.

Are there strict power constraints (battery-powered IoT)?
    └─ YES → DMA + deep sleep between messages. Avoid polling at all costs.

Is this a safety-critical system?
    └─ YES → Add HW FIFO + overflow detection + error counters to any strategy.
```

---

## Common Pitfalls and Best Practices

**Pitfall 1: Forgetting the TX complete flag**
Disabling UART or going to sleep immediately after writing to the data register can cut off the last byte. Always wait for TC (Transmission Complete), not just TXE (TX Empty), before shutting down.

**Pitfall 2: ISR doing too much work**
A UART ISR that calls `printf`, processes a full protocol frame, or takes a mutex will cause jitter and missed bytes. Keep ISRs to: read/write hardware register, push/pop ring buffer, signal a semaphore. Nothing more.

**Pitfall 3: Ring buffer size not a power of two**
Modulo arithmetic on non-power-of-two sizes requires a division instruction. On Cortex-M0 (no hardware divide), this is 20+ cycles per access. Use power-of-two sizes and bitwise masking.

**Pitfall 4: Volatile omission in ring buffer**
Without `volatile` (C) or the correct atomics/barriers (Rust), the compiler may cache ring buffer indices in registers and miss ISR updates. In Rust, RTIC handles this correctly; in C, mark `head` and `tail` as `volatile`.

**Pitfall 5: DMA cache coherency (Cortex-A / Cortex-M7)**
On processors with data cache, DMA writes to memory may not be visible to the CPU until the cache line is invalidated. Call `SCB_InvalidateDCache_by_Addr()` (STM32 HAL) or equivalent after DMA RX completes, and `SCB_CleanDCache_by_Addr()` before DMA TX starts.

**Pitfall 6: Enabling TXE interrupt prematurely**
If the TXE interrupt is enabled before data is pushed to the TX ring buffer, the ISR fires immediately (TXE is already set), finds nothing to send, disables itself, and the actual data is never sent. Always push to the ring buffer before enabling TXEIE.

**Best Practice: Separate RX and TX ring buffers**
One buffer for receive, one for transmit. This avoids head-of-line blocking and simplifies reasoning about each direction independently.

**Best Practice: Add statistics**
Maintain counters for: RX overruns, TX underruns, framing errors, ISR call counts. These are invaluable for debugging and for measuring actual CPU load in the field.

**Best Practice: Measure, don't guess**
Use a logic analyzer or oscilloscope on a GPIO toggled at ISR entry/exit to measure actual ISR duration and frequency. Calculate true CPU load as: `(ISR duration × ISR calls per second) / CPU frequency × 100%`.

---

## Summary

UART CPU load optimization is about choosing the right mechanism for the right operating condition — and the choices cascade through the entire system design.

**Polling** is the simplest approach but is fundamentally wasteful. It is appropriate only for brief, bounded transfers during initialization or in minimal systems where CPU occupancy is of no concern.

**Interrupt-driven UART with a ring buffer** is the workhorse of embedded systems. It delivers low latency, low CPU load, and manageable complexity. The key insight is that the ISR does the minimum possible work — it reads a byte and posts it — while all higher-level processing happens in a task or main loop. This approach scales well from 9600 baud up to approximately 1 Mbaud.

**DMA-driven UART** removes per-byte CPU cost entirely, letting the DMA controller shuttle data between the UART peripheral and memory while the CPU sleeps or handles other work. The critical enhancement is pairing DMA with the UART idle-line interrupt so that variable-length packets are captured completely without requiring the DMA buffer to fill before delivery.

In **RTOS-based systems**, the interrupt-driven pattern extends naturally: the ISR fills a queue or ring buffer and posts a semaphore; a dedicated task blocks on that semaphore and runs only when data is ready. The CPU spends its remaining cycles on application logic, sleeping between events.

The overarching principle is: **the CPU should react to UART events, not wait for them.** Hardware (UART peripheral, DMA, interrupt controller) exists precisely to offload time-keeping and data movement from the CPU. Letting the hardware do its job — and letting the CPU sleep in between — is the foundation of efficient, responsive, and power-conscious embedded UART design.

---

*Document: UART CPU Load Optimization | Topic 28 of UART Programming Series*