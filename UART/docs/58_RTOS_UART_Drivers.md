# 58. RTOS UART Drivers

- **Why UART needs special handling** in an RTOS — preemption, shared resources, ISR↔task communication, DMA, power management
- **Core concepts** — interrupt-driven I/O, DMA double-buffering, ring buffers, and OS primitives (mutex, semaphore, queue)
- **FreeRTOS** — complete queue-based C driver (`uart_driver.h/.c`), DMA+task-notification pattern, and a FreeRTOS Rust example via `freertos-rust` + `stm32f4xx-hal`
- **Zephyr** — all three API tiers (polling, interrupt-driven, async DMA) in C, plus a Zephyr Rust binding example
- **Embassy** — async/await UART read/write and a DMA-backed line-buffered command dispatcher in Rust
- **Cross-platform abstraction** — a Rust trait-based layer using `embedded-io-async` and a C HAL vtable pattern
- **Advanced patterns** — byte-stuffed framing with CRC-8 (C and Rust), Zephyr shell/CLI integration, and FreeRTOS/Zephyr power management hooks
- **Debugging table** — 8 common pitfalls with root causes and fixes
- **Summary** — synthesises all platforms and key architectural principles

## Integrating UART with FreeRTOS, Zephyr, and Other RTOS Platforms

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why UART Needs Special Handling in an RTOS](#why-uart-needs-special-handling-in-an-rtos)
3. [Core Concepts](#core-concepts)
   - [Interrupt-Driven I/O](#interrupt-driven-io)
   - [DMA-Assisted Transfers](#dma-assisted-transfers)
   - [Ring Buffers / Circular Queues](#ring-buffers--circular-queues)
   - [Thread Safety and Mutual Exclusion](#thread-safety-and-mutual-exclusion)
4. [FreeRTOS UART Driver](#freertos-uart-driver)
   - [Architecture Overview](#freertos-architecture-overview)
   - [Queue-Based UART in C](#queue-based-uart-in-c-freertos)
   - [DMA + Notification Pattern in C](#dma--notification-pattern-in-c-freertos)
   - [FreeRTOS UART in Rust (via embassy-rtos)](#freertos-uart-in-rust)
5. [Zephyr RTOS UART Driver](#zephyr-rtos-uart-driver)
   - [Architecture Overview](#zephyr-architecture-overview)
   - [Polling API in C](#polling-api-in-c-zephyr)
   - [Interrupt-Driven API in C](#interrupt-driven-api-in-c-zephyr)
   - [Async (DMA) API in C](#async-dma-api-in-c-zephyr)
   - [Zephyr UART in Rust (via zephyr-sys)](#zephyr-uart-in-rust)
6. [Embassy (Async Rust RTOS)](#embassy-async-rust-rtos)
   - [Architecture Overview](#embassy-architecture-overview)
   - [Async UART Read/Write](#async-uart-readwrite-embassy)
   - [UART with DMA in Embassy](#uart-with-dma-in-embassy)
7. [Cross-Platform Abstraction Layer](#cross-platform-abstraction-layer)
   - [Trait-Based Abstraction in Rust](#trait-based-abstraction-in-rust)
   - [HAL Abstraction in C](#hal-abstraction-in-c)
8. [Advanced Patterns](#advanced-patterns)
   - [Framed Packet Protocol over UART](#framed-packet-protocol-over-uart)
   - [UART Shell / CLI Integration](#uart-shell--cli-integration)
   - [Power Management and Tickless Idle](#power-management-and-tickless-idle)
9. [Debugging and Common Pitfalls](#debugging-and-common-pitfalls)
10. [Summary](#summary)

---

## Introduction

The Universal Asynchronous Receiver-Transmitter (UART) is one of the oldest and most widely-used serial communication peripherals in embedded systems. While bare-metal UART drivers can be simple—a polling loop reading a status register—integrating UART into a Real-Time Operating System (RTOS) demands considerably more care. The RTOS introduces preemption, task scheduling, shared resources, and strict timing requirements that a naive driver design can easily violate.

This document covers the design, implementation, and use of UART drivers in three major RTOS environments:

- **FreeRTOS** – the industry's most widely deployed embedded RTOS, used heavily on ARM Cortex-M, RISC-V, and other architectures.
- **Zephyr RTOS** – a modern, Linux Foundation-hosted RTOS with a rich driver model and device tree–based hardware abstraction.
- **Embassy** – a modern async/await Rust RTOS designed for bare-metal microcontrollers, representing the frontier of safe embedded systems programming.

Code examples are provided in both **C/C++** and **Rust** throughout.

---

## Why UART Needs Special Handling in an RTOS

In a single-threaded bare-metal program, the CPU either busy-waits for a character or checks a flag in a super-loop. This is simple but wastes CPU cycles and cannot tolerate tasks with hard real-time deadlines.

An RTOS changes the landscape in several ways:

| Challenge | Implication for UART |
|---|---|
| **Preemptive scheduling** | A task can be interrupted mid-transmission; the driver must be re-entrant or protected. |
| **Multiple tasks sharing one port** | A mutex or other serialization mechanism is needed. |
| **Blocking vs. non-blocking I/O** | Tasks must be able to block waiting for data without spinning. |
| **ISR ↔ Task communication** | Data received in an ISR must be safely handed to an application task. |
| **DMA transfers** | DMA completion must wake a task, not poll. |
| **Tickless/low-power idle** | The UART must be able to wake the system from sleep. |

A well-designed RTOS UART driver uses **interrupt-driven** or **DMA-driven** I/O, communicates with application tasks via **OS primitives** (queues, semaphores, notifications), and guarantees **mutual exclusion** on shared transmit and receive paths.

---

## Core Concepts

### Interrupt-Driven I/O

Rather than polling a register, the UART peripheral fires an interrupt when:
- **RX:** A character has been received (or the FIFO has reached a threshold).
- **TX:** The transmit data register is empty and ready for the next byte.

The ISR deposits received bytes into a ring buffer or OS queue, and signals the waiting task. On the transmit side, the ISR pulls the next byte from a buffer and writes it to the hardware, continuing until the buffer is empty.

```
  [Application Task]  ←──(queue/semaphore)──  [RX ISR]  ←──  [UART HW]
  [Application Task]  ──►(queue/semaphore)──►  [TX ISR]  ──►  [UART HW]
```

### DMA-Assisted Transfers

Direct Memory Access allows the UART peripheral to transfer data to/from RAM without CPU involvement. The CPU programs the DMA controller with a source/destination address and byte count, then goes to sleep. When the transfer completes (or a half-transfer interrupt fires), the DMA raises an interrupt to wake the task.

DMA is especially valuable for:
- High-baud-rate UART (1 Mbps+) where per-byte ISRs would dominate CPU time.
- Long messages (firmware update payloads, log dumps).
- Low-power applications where the CPU must stay in a sleep state.

### Ring Buffers / Circular Queues

Between the ISR and the application task sits a ring buffer—a fixed-size array treated as a circular FIFO. The ISR writes to the **head** pointer; the task reads from the **tail** pointer. Because head and tail updates must be atomic with respect to each other, careful attention to volatile, memory barriers, or critical sections is required.

```c
typedef struct {
    uint8_t  buf[RING_BUF_SIZE];
    uint16_t head;  // written by ISR
    uint16_t tail;  // read by task
} ring_buf_t;
```

### Thread Safety and Mutual Exclusion

All RTOS primitives used in UART drivers serve one of two purposes:

| Primitive | Purpose |
|---|---|
| **Mutex** | Serialize access to the transmit path from multiple tasks. |
| **Binary semaphore / task notification** | Signal a waiting task from the ISR that new data is available. |
| **Queue** | Transfer byte(s) or complete messages from ISR to task. |
| **Event flags / event groups** | Signal multiple conditions (TX complete, RX line idle, error). |

---

## FreeRTOS UART Driver

### FreeRTOS Architecture Overview

FreeRTOS does not ship a standard UART driver; each port provides its own. The canonical pattern on STM32/ARM is:

1. Initialize the UART peripheral and enable RX/TX interrupts (and optionally DMA).
2. Create a **FreeRTOS Queue** (`xQueueCreate`) to hold received bytes or messages.
3. In the **RX ISR**, call `xQueueSendFromISR` to push bytes, then yield if a higher-priority task was unblocked (`portYIELD_FROM_ISR`).
4. Application tasks call `xQueueReceive` with a timeout to block until data arrives.
5. Wrap the transmit path in a **Mutex** so multiple tasks can call `uart_write` safely.

---

### Queue-Based UART in C (FreeRTOS)

```c
/* uart_driver.h */
#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>
#include <stdbool.h>

#define UART_RX_QUEUE_SIZE   256
#define UART_TX_BUF_SIZE     256

typedef struct {
    /* Hardware register base (platform-specific) */
    void            *hw_base;
    /* RX queue: bytes are deposited by ISR, consumed by tasks */
    QueueHandle_t    rx_queue;
    /* TX mutex: serialises concurrent transmit calls */
    SemaphoreHandle_t tx_mutex;
    /* TX busy semaphore: given by ISR when TX FIFO is empty */
    SemaphoreHandle_t tx_done_sem;
    /* Shadow TX buffer (ISR reads from here) */
    uint8_t          tx_buf[UART_TX_BUF_SIZE];
    volatile uint16_t tx_head;
    volatile uint16_t tx_tail;
} uart_handle_t;

bool uart_init(uart_handle_t *h, void *hw_base, uint32_t baud);
bool uart_write(uart_handle_t *h, const uint8_t *data, uint16_t len,
                TickType_t timeout);
uint16_t uart_read(uart_handle_t *h, uint8_t *buf, uint16_t max_len,
                   TickType_t timeout);

/* Called from UART ISR – do NOT call from tasks */
void uart_isr_handler(uart_handle_t *h);

#endif /* UART_DRIVER_H */
```

```c
/* uart_driver.c  –  STM32-style register layout, adapt as needed */
#include "uart_driver.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <string.h>

/* ---- Minimal register map (adapt to your MCU) ---- */
typedef struct {
    volatile uint32_t SR;    /* Status register  */
    volatile uint32_t DR;    /* Data register    */
    volatile uint32_t BRR;   /* Baud-rate        */
    volatile uint32_t CR1;   /* Control 1        */
    volatile uint32_t CR2;   /* Control 2        */
} UART_Regs_t;

#define UART_SR_RXNE   (1u << 5)   /* RX not empty */
#define UART_SR_TXE    (1u << 7)   /* TX empty     */
#define UART_SR_TC     (1u << 6)   /* TX complete  */
#define UART_CR1_UE    (1u << 13)
#define UART_CR1_TE    (1u << 3)
#define UART_CR1_RE    (1u << 2)
#define UART_CR1_RXNEIE (1u << 5)
#define UART_CR1_TXEIE  (1u << 7)

bool uart_init(uart_handle_t *h, void *hw_base, uint32_t baud)
{
    h->hw_base = hw_base;
    h->tx_head = 0;
    h->tx_tail = 0;

    /* Create OS primitives */
    h->rx_queue    = xQueueCreate(UART_RX_QUEUE_SIZE, sizeof(uint8_t));
    h->tx_mutex    = xSemaphoreCreateMutex();
    h->tx_done_sem = xSemaphoreCreateBinary();

    if (!h->rx_queue || !h->tx_mutex || !h->tx_done_sem)
        return false;

    /* Give the TX semaphore once – TX is idle at start */
    xSemaphoreGive(h->tx_done_sem);

    /* Hardware init (baud, 8N1, RX interrupt enable) */
    UART_Regs_t *uart = (UART_Regs_t *)hw_base;
    /* (clock setup and NVIC enable omitted – platform specific) */
    uart->BRR  = /* PCLK / baud */ 0;   /* fill in for your clock */
    uart->CR1  = UART_CR1_UE | UART_CR1_TE | UART_CR1_RE | UART_CR1_RXNEIE;

    return true;
}

/* -------------------------------------------------------
 * uart_write – called from any task
 * Copies data into the TX shadow buffer, enables TX ISR,
 * and blocks until the ISR has drained the buffer.
 * ------------------------------------------------------- */
bool uart_write(uart_handle_t *h, const uint8_t *data, uint16_t len,
                TickType_t timeout)
{
    if (len == 0 || len > UART_TX_BUF_SIZE)
        return false;

    /* Only one task may transmit at a time */
    if (xSemaphoreTake(h->tx_mutex, timeout) != pdTRUE)
        return false;

    /* Re-arm the "TX done" semaphore before we start */
    xSemaphoreTake(h->tx_done_sem, 0);

    memcpy(h->tx_buf, data, len);
    h->tx_head = 0;
    h->tx_tail = (uint16_t)len;

    /* Kick off by enabling TX-empty interrupt */
    UART_Regs_t *uart = (UART_Regs_t *)h->hw_base;
    uart->CR1 |= UART_CR1_TXEIE;

    /* Block until the ISR signals all bytes are sent */
    bool ok = (xSemaphoreTake(h->tx_done_sem, timeout) == pdTRUE);

    xSemaphoreGive(h->tx_mutex);
    return ok;
}

/* -------------------------------------------------------
 * uart_read – called from any task
 * Blocks for up to `timeout` ticks for each byte.
 * Returns the number of bytes actually read.
 * ------------------------------------------------------- */
uint16_t uart_read(uart_handle_t *h, uint8_t *buf, uint16_t max_len,
                   TickType_t timeout)
{
    uint16_t count = 0;
    while (count < max_len) {
        uint8_t byte;
        if (xQueueReceive(h->rx_queue, &byte, (count == 0) ? timeout : 0)
                != pdTRUE)
            break;
        buf[count++] = byte;
    }
    return count;
}

/* -------------------------------------------------------
 * uart_isr_handler – called from the UART interrupt vector
 * MUST be called with the FreeRTOS ISR-safe API.
 * ------------------------------------------------------- */
void uart_isr_handler(uart_handle_t *h)
{
    UART_Regs_t *uart  = (UART_Regs_t *)h->hw_base;
    BaseType_t   yield = pdFALSE;

    /* ---- RX: new byte received ---- */
    if (uart->SR & UART_SR_RXNE) {
        uint8_t byte = (uint8_t)uart->DR;   /* reading DR clears RXNE */
        xQueueSendFromISR(h->rx_queue, &byte, &yield);
    }

    /* ---- TX: transmit data register empty ---- */
    if (uart->SR & UART_SR_TXE) {
        if (h->tx_head < h->tx_tail) {
            uart->DR = h->tx_buf[h->tx_head++];
        } else {
            /* Buffer exhausted – disable TX interrupt, signal task */
            uart->CR1 &= ~UART_CR1_TXEIE;
            xSemaphoreGiveFromISR(h->tx_done_sem, &yield);
        }
    }

    portYIELD_FROM_ISR(yield);
}
```

```c
/* example_task.c  –  application usage */
#include "uart_driver.h"
#include "FreeRTOS.h"
#include "task.h"

static uart_handle_t g_uart;

void uart_task(void *pvParam)
{
    /* Initialise UART2, 115200 baud */
    uart_init(&g_uart, (void *)0x40004400 /* USART2 base */, 115200);

    uint8_t rx_buf[64];
    for (;;) {
        /* Block indefinitely waiting for up to 63 bytes */
        uint16_t n = uart_read(&g_uart, rx_buf, sizeof(rx_buf) - 1,
                               portMAX_DELAY);
        if (n > 0) {
            rx_buf[n] = '\0';
            /* Echo back with a prefix */
            const char *prefix = "ECHO: ";
            uart_write(&g_uart, (const uint8_t *)prefix, 6, pdMS_TO_TICKS(100));
            uart_write(&g_uart, rx_buf, n, pdMS_TO_TICKS(100));
        }
    }
}

/* Interrupt vector – wired in startup / vector table */
void USART2_IRQHandler(void)
{
    uart_isr_handler(&g_uart);
}
```

---

### DMA + Notification Pattern in C (FreeRTOS)

For high-throughput scenarios, DMA eliminates per-byte ISR overhead. The driver programs the DMA controller to fill a double buffer; when one half completes, the task is woken via a **direct task notification**.

```c
/* uart_dma.c  –  FreeRTOS + DMA double-buffer pattern */
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <string.h>

#define DMA_BUF_HALF   64          /* bytes per half-buffer  */
#define DMA_BUF_TOTAL  (DMA_BUF_HALF * 2)

static uint8_t   s_dma_rx_buf[DMA_BUF_TOTAL];  /* DMA writes here      */
static TaskHandle_t s_rx_task_handle;           /* Task to notify       */

/* Notification bit values */
#define NOTIFY_RX_HALF  (1u << 0)
#define NOTIFY_RX_FULL  (1u << 1)
#define NOTIFY_TX_DONE  (1u << 2)

/* -------------------------------------------------------
 * DMA Half-Transfer ISR  (first 64 bytes are ready)
 * ------------------------------------------------------- */
void DMA1_Stream5_HalfTransfer_IRQHandler(void)
{
    BaseType_t yield = pdFALSE;
    /* Clear DMA HT flag (platform-specific register write omitted) */
    vTaskNotifyGiveFromISR(s_rx_task_handle, &yield);
    /* Pass which half via direct notification bits */
    xTaskNotifyFromISR(s_rx_task_handle, NOTIFY_RX_HALF,
                       eSetBits, &yield);
    portYIELD_FROM_ISR(yield);
}

/* -------------------------------------------------------
 * DMA Transfer-Complete ISR  (second 64 bytes are ready)
 * ------------------------------------------------------- */
void DMA1_Stream5_TransferComplete_IRQHandler(void)
{
    BaseType_t yield = pdFALSE;
    xTaskNotifyFromISR(s_rx_task_handle, NOTIFY_RX_FULL,
                       eSetBits, &yield);
    portYIELD_FROM_ISR(yield);
}

/* -------------------------------------------------------
 * Application task using DMA double-buffer
 * ------------------------------------------------------- */
void uart_dma_task(void *pvParam)
{
    s_rx_task_handle = xTaskGetCurrentTaskHandle();
    /* (DMA and UART peripheral init omitted – platform specific) */

    for (;;) {
        uint32_t notif = 0;
        /* Block until DMA fires either notification bit */
        xTaskNotifyWait(0, NOTIFY_RX_HALF | NOTIFY_RX_FULL,
                        &notif, portMAX_DELAY);

        if (notif & NOTIFY_RX_HALF) {
            /* Process first half while DMA fills second half */
            process_rx_data(s_dma_rx_buf, DMA_BUF_HALF);
        }
        if (notif & NOTIFY_RX_FULL) {
            /* Process second half while DMA refills first half */
            process_rx_data(s_dma_rx_buf + DMA_BUF_HALF, DMA_BUF_HALF);
        }
    }
}
```

---

### FreeRTOS UART in Rust

The `freertos-rust` crate provides FreeRTOS bindings for Rust. For hardware access the `stm32f4xx-hal` crate is used below.

```rust
// Cargo.toml dependencies:
// freertos-rust = "0.1"
// stm32f4xx-hal = { version = "0.20", features = ["stm32f401"] }

#![no_std]
#![no_main]

use freertos_rust::{Queue, Task, Duration};
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{Config, Serial},
};
use core::fmt::Write;

/// Global RX queue shared between the UART ISR and the application task.
static mut RX_QUEUE: Option<Queue<u8>> = None;

#[cortex_m_rt::entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.freeze();
    let gpioa = dp.GPIOA.split();

    // Configure PA9/PA10 as USART1 TX/RX
    let tx_pin = gpioa.pa9.into_alternate();
    let rx_pin = gpioa.pa10.into_alternate();

    let mut serial = Serial::new(
        dp.USART1,
        (tx_pin, rx_pin),
        Config::default().baudrate(115200.bps()),
        &clocks,
    )
    .unwrap();

    // Enable RXNE interrupt
    serial.listen(stm32f4xx_hal::serial::Event::Rxne);

    // Create the queue before starting FreeRTOS
    unsafe {
        RX_QUEUE = Some(Queue::new(256).expect("queue alloc failed"));
    }

    // Spawn the echo task
    Task::new()
        .name("uart_echo")
        .stack_size(512)
        .start(move |_| {
            let queue = unsafe { RX_QUEUE.as_ref().unwrap() };
            let mut tx = serial.split().0; // take TX half

            loop {
                // Block waiting for a byte (portMAX_DELAY equivalent)
                if let Ok(byte) = queue.receive(Duration::infinite()) {
                    // Simple echo
                    let _ = tx.write_char(byte as char);
                }
            }
        })
        .unwrap();

    freertos_rust::start_scheduler();
}

/// USART1 interrupt handler – called when a byte arrives
#[cortex_m_rt::interrupt]
fn USART1() {
    // Read DR to clear RXNE flag (reading clears it on STM32)
    let dp = unsafe { pac::Peripherals::steal() };
    let byte = dp.USART1.dr.read().dr().bits() as u8;

    if let Some(q) = unsafe { RX_QUEUE.as_ref() } {
        let _ = q.send_from_isr(byte);
    }
}
```

---

## Zephyr RTOS UART Driver

### Zephyr Architecture Overview

Zephyr models all UART peripherals as **device tree nodes**. The application obtains a handle via `DEVICE_DT_GET` and uses one of three API tiers:

| API Tier | Mechanism | Use Case |
|---|---|---|
| **Polling** | Busy-wait on `uart_poll_in` / `uart_poll_out` | Tiny systems, boot / diagnostics |
| **Interrupt-driven** | Callback registered with `uart_irq_callback_set` | General purpose, small messages |
| **Async (DMA)** | `uart_rx_enable` / `uart_tx` + callback | High-throughput, DMA-backed |

Zephyr's UART API is defined in `<zephyr/drivers/uart.h>`.

---

### Polling API in C (Zephyr)

```c
/* zephyr_uart_poll.c */
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <string.h>

#define UART_NODE DT_NODELABEL(uart0)

void uart_poll_example(void)
{
    const struct device *uart_dev = DEVICE_DT_GET(UART_NODE);

    if (!device_is_ready(uart_dev)) {
        printk("UART device not ready\n");
        return;
    }

    const char *msg = "Hello from Zephyr polling UART!\r\n";
    for (int i = 0; msg[i] != '\0'; i++) {
        uart_poll_out(uart_dev, msg[i]);
    }

    printk("Waiting for input...\n");
    char c;
    while (uart_poll_in(uart_dev, &c) < 0) {
        k_sleep(K_MSEC(1));   /* yield to other threads while waiting */
    }
    printk("Received: %c\n", c);
}
```

---

### Interrupt-Driven API in C (Zephyr)

```c
/* zephyr_uart_irq.c */
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <string.h>

#define UART_NODE   DT_NODELABEL(uart1)
#define RX_BUF_SIZE 512
#define MSG_SIZE    128

/* Ring buffer holding received bytes */
static uint8_t  s_rx_ring[RX_BUF_SIZE];
static uint16_t s_rx_head = 0;
static uint16_t s_rx_tail = 0;

/* Semaphore: ISR gives, task takes */
static K_SEM_DEFINE(s_rx_sem, 0, 1);

/* TX buffer – filled by task, drained by ISR */
static uint8_t   s_tx_buf[MSG_SIZE];
static uint16_t  s_tx_pos = 0;
static uint16_t  s_tx_len = 0;
static K_SEM_DEFINE(s_tx_done, 1, 1);   /* starts available */

/* -------------------------------------------------------
 * UART ISR callback (called from Zephyr IRQ thread)
 * ------------------------------------------------------- */
static void uart_irq_cb(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {

        /* ----- RX ----- */
        if (uart_irq_rx_ready(dev)) {
            uint8_t byte;
            while (uart_fifo_read(dev, &byte, 1) == 1) {
                uint16_t next = (s_rx_head + 1) % RX_BUF_SIZE;
                if (next != s_rx_tail) {          /* guard overflow */
                    s_rx_ring[s_rx_head] = byte;
                    s_rx_head = next;
                }
            }
            k_sem_give(&s_rx_sem);
        }

        /* ----- TX ----- */
        if (uart_irq_tx_ready(dev)) {
            if (s_tx_pos < s_tx_len) {
                int sent = uart_fifo_fill(dev,
                                          s_tx_buf + s_tx_pos,
                                          s_tx_len - s_tx_pos);
                s_tx_pos += (uint16_t)sent;
            } else {
                /* All bytes sent – disable TX interrupt */
                uart_irq_tx_disable(dev);
                k_sem_give(&s_tx_done);
            }
        }
    }
}

/* -------------------------------------------------------
 * Public API: initialise
 * ------------------------------------------------------- */
const struct device *uart_irq_init(void)
{
    const struct device *dev = DEVICE_DT_GET(UART_NODE);
    if (!device_is_ready(dev)) return NULL;

    uart_irq_callback_user_data_set(dev, uart_irq_cb, NULL);
    uart_irq_rx_enable(dev);
    return dev;
}

/* -------------------------------------------------------
 * Public API: blocking transmit
 * ------------------------------------------------------- */
int uart_irq_write(const struct device *dev, const uint8_t *data,
                   uint16_t len, k_timeout_t timeout)
{
    if (k_sem_take(&s_tx_done, timeout) != 0) return -ETIMEDOUT;

    memcpy(s_tx_buf, data, MIN(len, MSG_SIZE));
    s_tx_pos = 0;
    s_tx_len = len;
    uart_irq_tx_enable(dev);
    return 0;
}

/* -------------------------------------------------------
 * Public API: non-blocking read from ring buffer
 * ------------------------------------------------------- */
uint16_t uart_irq_read(uint8_t *buf, uint16_t max, k_timeout_t timeout)
{
    /* Wait until at least one byte is available */
    if (s_rx_head == s_rx_tail) {
        if (k_sem_take(&s_rx_sem, timeout) != 0) return 0;
    }

    uint16_t count = 0;
    while (count < max && s_rx_tail != s_rx_head) {
        buf[count++] = s_rx_ring[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1) % RX_BUF_SIZE;
    }
    return count;
}

/* -------------------------------------------------------
 * Application thread
 * ------------------------------------------------------- */
#define STACK_SIZE 1024
#define PRIORITY   5

K_THREAD_STACK_DEFINE(s_uart_stack, STACK_SIZE);
static struct k_thread s_uart_thread;

static void uart_app_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg2); ARG_UNUSED(arg3);
    const struct device *dev = uart_irq_init();
    if (!dev) return;

    const char *banner = "[Zephyr UART ready]\r\n";
    uart_irq_write(dev, (const uint8_t *)banner, strlen(banner),
                   K_FOREVER);

    uint8_t rx[64];
    for (;;) {
        uint16_t n = uart_irq_read(rx, sizeof(rx), K_FOREVER);
        if (n > 0) {
            uart_irq_write(dev, rx, n, K_FOREVER);
        }
    }
}

void start_uart_thread(void)
{
    k_thread_create(&s_uart_thread, s_uart_stack,
                    K_THREAD_STACK_SIZEOF(s_uart_stack),
                    uart_app_thread, NULL, NULL, NULL,
                    PRIORITY, 0, K_NO_WAIT);
}
```

---

### Async (DMA) API in C (Zephyr)

```c
/* zephyr_uart_async.c  –  Zephyr async UART with DMA */
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <string.h>

#define UART_NODE   DT_NODELABEL(uart2)
#define DMA_BUF_SZ  256

/* Double RX buffers: while Zephyr fills one, we process the other */
static uint8_t s_rx_a[DMA_BUF_SZ];
static uint8_t s_rx_b[DMA_BUF_SZ];
static bool    s_use_a = true;

static K_SEM_DEFINE(s_rx_ready, 0, 1);
static uint8_t  *s_rx_ready_buf  = NULL;
static uint32_t  s_rx_ready_len  = 0;
static K_SEM_DEFINE(s_tx_done,  1, 1);

/* -------------------------------------------------------
 * Async event callback
 * ------------------------------------------------------- */
static void uart_async_cb(const struct device *dev,
                           struct uart_event  *evt,
                           void               *user_data)
{
    ARG_UNUSED(user_data);

    switch (evt->type) {

    case UART_TX_DONE:
        k_sem_give(&s_tx_done);
        break;

    case UART_TX_ABORTED:
        k_sem_give(&s_tx_done);   /* unblock regardless */
        break;

    case UART_RX_RDY:
        /* DMA data is ready – record pointer and length */
        s_rx_ready_buf = evt->data.rx.buf + evt->data.rx.offset;
        s_rx_ready_len = evt->data.rx.len;
        k_sem_give(&s_rx_ready);
        break;

    case UART_RX_BUF_REQUEST:
        /* Zephyr asks for a new buffer to continue DMA */
        {
            uint8_t *next = s_use_a ? s_rx_b : s_rx_a;
            s_use_a = !s_use_a;
            uart_rx_buf_rsp(dev, next, DMA_BUF_SZ);
        }
        break;

    case UART_RX_BUF_RELEASED:
        /* The buffer we provided is no longer in use by the driver */
        break;

    case UART_RX_DISABLED:
        /* Called when uart_rx_disable() completes */
        break;

    case UART_RX_STOPPED:
        /* RX stopped due to error; restart */
        uart_rx_enable(dev, s_rx_a, DMA_BUF_SZ, 100 /* idle timeout µs */);
        break;
    }
}

/* -------------------------------------------------------
 * Init + application task
 * ------------------------------------------------------- */
static void uart_async_task(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    const struct device *dev = DEVICE_DT_GET(UART_NODE);
    if (!device_is_ready(dev)) return;

    uart_callback_set(dev, uart_async_cb, NULL);

    /* Kick off DMA reception into the first buffer */
    uart_rx_enable(dev, s_rx_a, DMA_BUF_SZ, 100 /* idle timeout µs */);

    const char *banner = "[Zephyr Async UART ready]\r\n";
    uart_tx(dev, (const uint8_t *)banner, strlen(banner), SYS_FOREVER_US);
    k_sem_take(&s_tx_done, K_FOREVER);

    for (;;) {
        /* Block until DMA delivers data */
        k_sem_take(&s_rx_ready, K_FOREVER);

        /* s_rx_ready_buf / s_rx_ready_len are set by the callback */
        if (s_rx_ready_buf && s_rx_ready_len > 0) {
            /* Echo the data back via async TX */
            k_sem_take(&s_tx_done, K_FOREVER);
            uart_tx(dev, s_rx_ready_buf, s_rx_ready_len, SYS_FOREVER_US);
        }
    }
}

#define ASYNC_STACK_SIZE 2048
#define ASYNC_PRIORITY   4
K_THREAD_STACK_DEFINE(s_async_stack, ASYNC_STACK_SIZE);
static struct k_thread s_async_thread;

void start_async_uart(void)
{
    k_thread_create(&s_async_thread, s_async_stack,
                    K_THREAD_STACK_SIZEOF(s_async_stack),
                    uart_async_task, NULL, NULL, NULL,
                    ASYNC_PRIORITY, 0, K_NO_WAIT);
}
```

---

### Zephyr UART in Rust

```rust
// zephyr_uart_rust.rs
// Uses the zephyr-sys crate for raw bindings or zephyr (safe wrapper)
// Requires: zephyr = { version = "0.1", features = ["uart"] }

#![no_std]
#![no_main]

use zephyr::{
    device::uart::{BaudRate, Uart, UartConfig},
    kernel::{sem::Semaphore, thread::Thread},
    printk,
};

#[zephyr::main]
fn main() {
    // Obtain the UART device from the device tree at compile-time
    let uart = Uart::get_by_label("uart0").expect("uart0 not found");

    let config = UartConfig {
        baud_rate: BaudRate::Baud115200,
        ..Default::default()
    };
    uart.configure(&config).expect("UART configure failed");

    let msg = b"Hello from Zephyr + Rust!\r\n";
    for &byte in msg {
        uart.poll_out(byte);
    }

    // Simple blocking read loop
    loop {
        let mut buf = [0u8; 64];
        let mut count = 0usize;

        while count < buf.len() {
            match uart.poll_in() {
                Ok(byte) => {
                    buf[count] = byte;
                    count += 1;
                    if byte == b'\n' { break; }
                }
                Err(_) => {
                    zephyr::kernel::thread::sleep(
                        core::time::Duration::from_millis(1)
                    );
                }
            }
        }

        // Echo
        for &b in &buf[..count] {
            uart.poll_out(b);
        }
    }
}
```

---

## Embassy (Async Rust RTOS)

### Embassy Architecture Overview

Embassy is a modern Rust async executor designed for embedded systems. Instead of RTOS threads and queues, Embassy leverages Rust's **async/await** and futures to express concurrency without dynamic allocation or context-switch overhead.

UART in Embassy works through the `embedded-io-async` traits:

- `embedded_io_async::Read` – async read.
- `embedded_io_async::Write` – async write.
- `embassy_stm32::usart::Uart` – concrete implementation backed by DMA or interrupt.

Because everything is async, tasks naturally yield when waiting for I/O and consume zero CPU, just like a traditional RTOS task blocking on a queue—but with no OS scheduler or heap required.

---

### Async UART Read/Write (Embassy)

```rust
// embassy_uart_basic.rs
// Target: STM32F401 (adapt feature flags for your MCU)
// embassy-stm32 = { version = "0.1", features = ["stm32f401re", "time-driver-tim2"] }
// embassy-executor = { version = "0.5", features = ["arch-cortex-m"] }

#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

use embassy_executor::Spawner;
use embassy_stm32::{
    bind_interrupts,
    peripherals,
    usart::{Config, Uart, InterruptHandler},
};
use embassy_time::{Duration, Timer};
use embedded_io_async::Write;

// Bind the USART2 interrupt to Embassy's handler
bind_interrupts!(struct Irqs {
    USART2 => InterruptHandler<peripherals::USART2>;
});

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    let mut config = Config::default();
    config.baudrate = 115200;

    // PA2 = TX, PA3 = RX for USART2 on STM32F401
    let mut uart = Uart::new(
        p.USART2,
        p.PA3,   // RX
        p.PA2,   // TX
        Irqs,
        p.DMA1_CH6,  // TX DMA
        p.DMA1_CH5,  // RX DMA
        config,
    )
    .unwrap();

    let banner = b"Embassy async UART ready\r\n";
    uart.write_all(banner).await.unwrap();

    let mut buf = [0u8; 64];
    loop {
        // Read exactly 1 byte, yield until it arrives (zero CPU spin)
        match uart.read(&mut buf[..1]).await {
            Ok(_) => {
                // Echo the byte
                uart.write_all(&buf[..1]).await.unwrap();
            }
            Err(e) => {
                // On error, wait briefly then continue
                Timer::after(Duration::from_millis(10)).await;
                let _ = e;
            }
        }
    }
}
```

---

### UART with DMA in Embassy

```rust
// embassy_uart_dma.rs  –  line-buffered protocol over UART
//
// Demonstrates reading a newline-terminated "frame" with DMA,
// processing it, and writing a response – all in a single async task.

#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

use embassy_executor::Spawner;
use embassy_stm32::{
    bind_interrupts, peripherals,
    usart::{Config, Uart, InterruptHandler},
};
use embassy_time::{Duration, Timer};
use embedded_io_async::{Read, Write};
use heapless::Vec;

bind_interrupts!(struct Irqs {
    USART1 => InterruptHandler<peripherals::USART1>;
});

/// Read bytes until a '\n' (LF) is found or the buffer fills up.
/// Returns the slice including the newline.
async fn read_line<'a>(
    uart: &mut Uart<'_, peripherals::USART1, peripherals::DMA2_CH7, peripherals::DMA2_CH2>,
    buf: &'a mut [u8],
) -> &'a [u8] {
    let mut pos = 0usize;
    while pos < buf.len() {
        let n = uart.read(&mut buf[pos..pos + 1]).await.unwrap_or(0);
        if n == 0 { continue; }
        if buf[pos] == b'\n' {
            pos += 1;
            break;
        }
        pos += 1;
    }
    &buf[..pos]
}

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    let mut cfg = Config::default();
    cfg.baudrate = 115200;

    let mut uart = Uart::new(
        p.USART1,
        p.PA10,     // RX
        p.PA9,      // TX
        Irqs,
        p.DMA2_CH7, // TX DMA stream
        p.DMA2_CH2, // RX DMA stream
        cfg,
    )
    .unwrap();

    uart.write_all(b"Ready. Send commands terminated with \\n\r\n")
        .await
        .unwrap();

    let mut line_buf = [0u8; 128];

    loop {
        let line = read_line(&mut uart, &mut line_buf).await;

        // Trim trailing CR/LF
        let trimmed = line.trim_ascii_end();

        // Dispatch simple commands
        let response: &[u8] = match trimmed {
            b"ping"    => b"pong\r\n",
            b"version" => b"v1.0.0\r\n",
            b"reset"   => {
                uart.write_all(b"Resetting...\r\n").await.unwrap();
                Timer::after(Duration::from_millis(100)).await;
                cortex_m::peripheral::SCB::sys_reset();
            }
            _          => b"unknown command\r\n",
        };

        uart.write_all(response).await.unwrap();
    }
}
```

---

## Cross-Platform Abstraction Layer

A production system often needs to run on multiple RTOS platforms. Writing platform-agnostic code requires an abstraction layer.

### Trait-Based Abstraction in Rust

The `embedded-io` / `embedded-io-async` crates define standard traits. Implementing them on your platform-specific UART type gives you code that compiles on FreeRTOS, Zephyr, or bare-metal.

```rust
// uart_abstract.rs  –  RTOS-agnostic UART protocol layer

use embedded_io_async::{Read, Write, ErrorType};
use core::fmt;

/// A simple request-response protocol that works with any async UART.
pub struct ModbusAsciiLayer<U>
where
    U: Read + Write,
{
    uart: U,
    timeout_ms: u64,
}

impl<U> ModbusAsciiLayer<U>
where
    U: Read + Write,
    <U as ErrorType>::Error: fmt::Debug,
{
    pub fn new(uart: U, timeout_ms: u64) -> Self {
        Self { uart, timeout_ms }
    }

    /// Send a request frame and receive the response.
    pub async fn transaction(
        &mut self,
        request: &[u8],
        response_buf: &mut [u8],
    ) -> Result<usize, <U as ErrorType>::Error> {
        // Send
        self.uart.write_all(request).await?;

        // Read response (simplified: read until buffer full)
        let mut pos = 0usize;
        while pos < response_buf.len() {
            let n = self.uart.read(&mut response_buf[pos..]).await?;
            pos += n;
            if pos > 0 && response_buf[pos - 1] == b'\n' {
                break;
            }
        }
        Ok(pos)
    }
}

// The same ModbusAsciiLayer compiles against:
//   embassy_stm32::usart::Uart      (Embassy)
//   zephyr::device::uart::AsyncUart (Zephyr-Rust)
//   any other embedded-io implementor
```

---

### HAL Abstraction in C

```c
/* uart_hal.h  –  RTOS-agnostic UART HAL for C projects */
#ifndef UART_HAL_H
#define UART_HAL_H

#include <stdint.h>
#include <stdbool.h>

/* Opaque handle – filled in by each platform implementation */
typedef struct uart_hal_ctx uart_hal_ctx_t;

typedef struct {
    bool  (*init)  (uart_hal_ctx_t *ctx, uint32_t baud);
    bool  (*write) (uart_hal_ctx_t *ctx, const uint8_t *buf, uint16_t len,
                    uint32_t timeout_ms);
    int   (*read)  (uart_hal_ctx_t *ctx, uint8_t *buf, uint16_t max,
                    uint32_t timeout_ms);
    void  (*deinit)(uart_hal_ctx_t *ctx);
} uart_hal_ops_t;

typedef struct {
    uart_hal_ctx_t  *ctx;
    const uart_hal_ops_t *ops;
} uart_hal_t;

/* Convenience wrappers */
static inline bool uart_hal_write(uart_hal_t *h, const uint8_t *b, uint16_t l,
                                   uint32_t t)
{
    return h->ops->write(h->ctx, b, l, t);
}
static inline int uart_hal_read(uart_hal_t *h, uint8_t *b, uint16_t m,
                                 uint32_t t)
{
    return h->ops->read(h->ctx, b, m, t);
}

/* Platform-specific constructors declared in separate files */
uart_hal_t *uart_hal_freertos_create(void *hw_base, uint32_t baud);
uart_hal_t *uart_hal_zephyr_create(const char *device_label, uint32_t baud);

#endif /* UART_HAL_H */
```

---

## Advanced Patterns

### Framed Packet Protocol over UART

Raw UART is byte-stream based; real protocols need framing to delineate messages. A common scheme uses a start byte, length, payload, and CRC.

```c
/* framing.c  –  COBS-lite: start/stop byte framing with CRC-8 */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define FRAME_START  0x7E
#define FRAME_END    0x7F
#define FRAME_ESC    0x7D
#define FRAME_XOR    0x20

static uint8_t crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

/* Encode `payload` into `out` using byte-stuffing.
 * Returns total encoded length including START and END bytes. */
uint16_t frame_encode(const uint8_t *payload, uint16_t plen,
                      uint8_t *out, uint16_t out_max)
{
    if (out_max < 4) return 0;   /* too small */
    uint16_t i = 0;
    out[i++] = FRAME_START;

    for (uint16_t j = 0; j < plen; j++) {
        uint8_t b = payload[j];
        if (b == FRAME_START || b == FRAME_END || b == FRAME_ESC) {
            if (i + 2 > out_max) return 0;
            out[i++] = FRAME_ESC;
            out[i++] = b ^ FRAME_XOR;
        } else {
            if (i + 1 > out_max) return 0;
            out[i++] = b;
        }
    }

    uint8_t crc = crc8(payload, plen);
    if (crc == FRAME_START || crc == FRAME_END || crc == FRAME_ESC) {
        if (i + 2 > out_max) return 0;
        out[i++] = FRAME_ESC;
        out[i++] = crc ^ FRAME_XOR;
    } else {
        if (i + 1 > out_max) return 0;
        out[i++] = crc;
    }

    if (i + 1 > out_max) return 0;
    out[i++] = FRAME_END;
    return i;
}
```

```rust
// framing.rs  –  same protocol in Rust
const FRAME_START: u8 = 0x7E;
const FRAME_END:   u8 = 0x7F;
const FRAME_ESC:   u8 = 0x7D;
const FRAME_XOR:   u8 = 0x20;

fn crc8(data: &[u8]) -> u8 {
    data.iter().fold(0u8, |mut crc, &b| {
        crc ^= b;
        for _ in 0..8 {
            crc = if crc & 0x80 != 0 { (crc << 1) ^ 0x07 } else { crc << 1 };
        }
        crc
    })
}

/// Encode a payload with byte-stuffing into `out`.
/// Returns `Some(len)` on success, `None` if `out` is too small.
pub fn frame_encode(payload: &[u8], out: &mut [u8]) -> Option<usize> {
    let mut i = 0;
    let mut emit = |buf: &mut [u8], idx: &mut usize, byte: u8| -> bool {
        if *idx >= buf.len() { return false; }
        buf[*idx] = byte;
        *idx += 1;
        true
    };

    if !emit(out, &mut i, FRAME_START) { return None; }

    let mut write_stuffed = |buf: &mut [u8], idx: &mut usize, b: u8| -> bool {
        if b == FRAME_START || b == FRAME_END || b == FRAME_ESC {
            emit(buf, idx, FRAME_ESC) && emit(buf, idx, b ^ FRAME_XOR)
        } else {
            emit(buf, idx, b)
        }
    };

    for &b in payload {
        if !write_stuffed(out, &mut i, b) { return None; }
    }
    let crc = crc8(payload);
    if !write_stuffed(out, &mut i, crc) { return None; }
    if !emit(out, &mut i, FRAME_END)    { return None; }

    Some(i)
}
```

---

### UART Shell / CLI Integration

Zephyr ships a built-in shell that sits on top of the UART driver. Enabling it in `prj.conf` requires only:

```conf
# prj.conf
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_UART_CONSOLE=y
```

Then register custom commands in C:

```c
/* shell_commands.c */
#include <zephyr/shell/shell.h>

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    shell_print(sh, "System uptime: %lld ms", k_uptime_get());
    return 0;
}

static int cmd_echo(const struct shell *sh, size_t argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        shell_print(sh, "%s", argv[i]);
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_myapp,
    SHELL_CMD(status, NULL, "Print system status", cmd_status),
    SHELL_CMD(echo,   NULL, "Echo arguments",      cmd_echo),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(myapp, &sub_myapp, "My application commands", NULL);
```

---

### Power Management and Tickless Idle

On battery-powered devices, the MCU must enter low-power modes between UART activity. Both FreeRTOS and Zephyr support tickless idle; the UART driver must cooperate.

```c
/* FreeRTOS pre/post-sleep hooks for STM32 low-power UART */
#include "FreeRTOS.h"
#include "task.h"

/* Called just before the CPU enters WFI (wait-for-interrupt) sleep */
void vApplicationPreSleepProcessing(TickType_t xExpectedIdleTime)
{
    ARG_UNUSED(xExpectedIdleTime);
    /* Ensure any pending UART TX is complete before sleeping */
    /* On STM32: wait for TC (transfer complete) flag */
    while (!(USART2->SR & UART_SR_TC)) {}
    /* Optionally reconfigure clock source for low-power mode */
}

/* Called just after the CPU wakes from sleep */
void vApplicationPostSleepProcessing(TickType_t xExpectedIdleTime)
{
    ARG_UNUSED(xExpectedIdleTime);
    /* Re-enable clocks if changed in pre-sleep hook */
}
```

For Zephyr, device power management is handled via PM hooks:

```c
/* zephyr_uart_pm.c */
#include <zephyr/pm/device.h>

static int uart_pm_action(const struct device *dev,
                          enum pm_device_action action)
{
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        /* Flush TX, disable RX wake interrupt or enable UART wakeup */
        uart_irq_rx_disable(dev);
        /* Configure UART as a wakeup source if hardware supports it */
        return 0;
    case PM_DEVICE_ACTION_RESUME:
        uart_irq_rx_enable(dev);
        return 0;
    default:
        return -ENOTSUP;
    }
}

PM_DEVICE_DT_DEFINE(DT_NODELABEL(uart0), uart_pm_action);
```

---

## Debugging and Common Pitfalls

| Issue | Root Cause | Fix |
|---|---|---|
| **Characters lost at high baud rates** | RX ISR too slow, FIFO overrun | Enable RX DMA; reduce ISR latency; increase FIFO threshold |
| **Task starvation** | UART ISR priority too high, starves RTOS tick | Set UART IRQ priority below `configMAX_SYSCALL_INTERRUPT_PRIORITY` (FreeRTOS) |
| **Deadlock on transmit** | Task takes TX mutex then waits on TX-done semaphore, ISR never runs because IRQ is masked | Never mask IRQs while holding OS primitives |
| **Garbled data after sleep/wake** | UART peripheral not re-initialized on resume | Re-configure UART registers in PM resume hook |
| **`xQueueSendFromISR` assertion** | Called with FreeRTOS scheduler suspended | Never call `FromISR` APIs while scheduler is suspended |
| **Embassy `read` never returns** | DMA channel not configured; `bind_interrupts!` macro missing | Ensure DMA streams assigned; `bind_interrupts!` is required |
| **Zephyr `uart_rx_enable` returns -ENODEV** | Async UART not enabled in device tree | Add `uart-async;` property and correct DMA channels to DTS node |
| **Stack overflow in UART task** | Deep call chain from ISR callback | Increase task stack size; profile with `uxTaskGetStackHighWaterMark` |

**General guidelines:**

- Always set the UART interrupt priority **below** the RTOS systick priority but above general application tasks on FreeRTOS (`configMAX_SYSCALL_INTERRUPT_PRIORITY`).
- In Zephyr, use `CONFIG_UART_INTERRUPT_DRIVEN=y` or `CONFIG_UART_ASYNC_API=y` in `prj.conf` to enable the desired API tier.
- For Embassy, always pair a UART with matching DMA streams and call `bind_interrupts!` – omitting either causes silent compile-time or runtime failures.
- Use a logic analyser or UART sniffer to verify framing at the hardware level before debugging software.

---

## Summary

Integrating UART into an RTOS is fundamentally about choosing the right OS primitives to bridge the gap between hardware interrupts and application tasks. This document covered:

**FreeRTOS** uses queues, mutexes, and task notifications as building blocks. A typical driver deposits received bytes into an `xQueue` from the RX ISR and drains a shadow buffer from the TX ISR, notifying the application task via a binary semaphore. DMA patterns extend this with `xTaskNotifyFromISR` to wake tasks on half/full transfer completion.

**Zephyr** offers three tiered APIs—polling, interrupt-driven, and async DMA—selected at build time. The interrupt-driven API uses an ISR callback registered with `uart_irq_callback_set` and Zephyr kernel semaphores. The async API provides a clean double-buffer DMA model with `uart_rx_enable` / `uart_tx` and event callbacks, making it the best choice for high-throughput applications.

**Embassy** represents the modern Rust approach: async/await replaces RTOS threads and queues entirely. UART tasks simply `await` on reads and writes; the executor suspends them when I/O is not ready and resumes them—via DMA completion interrupts—with zero polling overhead.

**Rust across all platforms** benefits from the `embedded-io-async` trait, which provides a platform-agnostic `Read`/`Write` interface. Protocol code written against these traits is portable across Embassy, Zephyr-Rust, and bare-metal HALs without modification.

Key architectural principles that apply across all RTOS environments:

- Use **interrupt-driven or DMA-driven** I/O; never busy-wait in production drivers.
- Bridge ISR and task domains with **OS-safe primitives** (`FromISR` variants in FreeRTOS, `k_sem_give` in Zephyr ISR callbacks, Embassy's interrupt-driven futures).
- Protect shared transmit paths with **mutual exclusion** (mutex or semaphore).
- Design for **power management** from the start: the UART driver must cooperate with sleep/wake cycles.
- Add a **framing layer** above raw bytes to give your protocol message boundaries and integrity checking.

---

*Document version: 1.0 — Covers FreeRTOS 10.x, Zephyr 3.x, and Embassy 0.5.x*