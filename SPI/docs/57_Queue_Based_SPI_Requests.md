# 57. Queue-based SPI Requests

**Architecture** — The pattern centralizes all SPI bus access through a single manager task fed by a bounded FIFO queue. Producer tasks never touch the SPI peripheral directly; they submit a request descriptor and wait for a completion signal.

**Request Descriptor** — The core data structure carries the CS pin, SPI mode/clock, buffer pointers, transfer type (write/read/full-duplex), and a completion notification (semaphore for synchronous, callback for async).

**C/C++ (FreeRTOS)** — Five code examples covering: initialization, the manager task loop, synchronous blocking requests, asynchronous callback requests, and a C++ priority queue extension for mixed-criticality systems.

**Rust (Embassy/RTIC)** — Four code examples covering: the typed request struct with `Signal`-based completion, the async manager task owning the SPI peripheral, async callers using `await` with `with_timeout`, and an RTIC channel-based variant with software tasks.

**Advanced Patterns** — Chained multi-segment transactions (keep CS low across phases), DMA-backed transfers with ISR wake-up, and a static request pool to avoid heap fragmentation.

**Summary** — Highlights the key design decisions, the synchronous vs. asynchronous trade-off, Rust's safety advantages, and queue depth sizing guidance.

# 57. Queue-based SPI Requests
## Using RTOS Queues for Managing Multiple SPI Transaction Requests

---

## Table of Contents

1. [Introduction](#introduction)
2. [Background: SPI Protocol Basics](#background-spi-protocol-basics)
3. [The Problem: Concurrent SPI Access](#the-problem-concurrent-spi-access)
4. [RTOS Queue Architecture for SPI](#rtos-queue-architecture-for-spi)
5. [Request Descriptor Design](#request-descriptor-design)
6. [Implementation in C/C++ (FreeRTOS)](#implementation-in-cc-freertos)
7. [Implementation in Rust (Embassy / RTIC)](#implementation-in-rust-embassy--rtic)
8. [Advanced Patterns](#advanced-patterns)
9. [Error Handling and Timeout Management](#error-handling-and-timeout-management)
10. [Performance Considerations](#performance-considerations)
11. [Summary](#summary)

---

## Introduction

In embedded systems, the SPI (Serial Peripheral Interface) bus is one of the most widely used communication interfaces, connecting microcontrollers to sensors, displays, flash memory, ADCs, and more. In simple, single-threaded firmware, SPI access is straightforward: initiate a transfer, wait for completion, move on. However, in **multi-tasking RTOS environments**, multiple tasks may need to use the same SPI bus simultaneously — creating contention, race conditions, and unpredictable timing.

**Queue-based SPI request management** is a design pattern that centralizes all SPI bus access through a dedicated task and a message queue. Rather than competing directly for the bus, producer tasks submit transaction descriptors to a queue. A single **SPI manager task** drains the queue sequentially, executing one transaction at a time and notifying the requester upon completion.

This document covers the architecture, data structures, implementation strategies, and trade-offs of this pattern in both **C/C++ with FreeRTOS** and **Rust with Embassy/RTIC**.

---

## Background: SPI Protocol Basics

SPI is a synchronous, full-duplex, master-slave serial protocol. Key signals:

| Signal | Direction | Description |
|--------|-----------|-------------|
| SCLK   | Master → Slave | Clock signal |
| MOSI   | Master → Slave | Master Out Slave In |
| MISO   | Slave → Master | Master In Slave Out |
| CS/SS  | Master → Slave | Chip Select (active-low) |

A typical SPI transaction sequence:
1. Assert CS low
2. Transfer N bytes (clock out MOSI, sample MISO simultaneously)
3. Deassert CS high

SPI operates in one of four **modes** (CPOL × CPHA), controlling clock polarity and phase. Different peripherals on the same bus may require different modes — an important complication when sharing the bus.

---

## The Problem: Concurrent SPI Access

Consider a system with:
- Task A: reads temperature from an SPI sensor every 100 ms
- Task B: writes display pixels to an SPI LCD continuously
- Task C: reads/writes configuration to an SPI flash chip on demand

Without coordination, Task A and Task C might both assert CS and start clocking simultaneously, corrupting both transactions. Even with a simple mutex, problems remain:

- **Priority inversion**: a low-priority task holds the SPI mutex; a high-priority task blocks; a medium-priority task preempts and delays everything.
- **No batching**: each task acquires/releases the mutex per byte, causing excessive context switches.
- **No mode switching**: if the LCD needs SPI Mode 0 and the flash needs SPI Mode 3, each task must reconfigure the peripheral — a race condition without careful ordering.
- **No back-pressure**: a slow SPI bus can cause faster tasks to pile up, consuming stack and CPU in busy-wait or semaphore-wait loops.

A queue-based manager elegantly solves all of these.

---

## RTOS Queue Architecture for SPI

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Application Tasks                            │
│                                                                      │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐              │
│  │   Task A     │   │   Task B     │   │   Task C     │              │
│  │  (Sensor)    │   │  (Display)   │   │   (Flash)    │              │
│  └──────┬───────┘   └──────┬───────┘   └───────┬──────┘              │
│         │                  │                   │                     │
│         └──────────────────┼───────────────────┘                     │
│                            │  xQueueSend(spi_request_queue, ...)     │
│                            ▼                                         │
│               ┌────────────────────────┐                             │
│               │   SPI Request Queue    │  (bounded, e.g. 16 items)   │
│               │  [req0][req1][req2]... │                             │
│               └────────────┬───────────┘                             │
│                            │  xQueueReceive(...)                     │
│                            ▼                                         │
│               ┌────────────────────────┐                             │
│               │   SPI Manager Task     │  (highest SPI priority)     │
│               │  - Dequeues requests   │                             │
│               │  - Configures SPI mode │                             │
│               │  - Asserts CS          │                             │
│               │  - Executes transfer   │                             │
│               │  - Deasserts CS        │                             │
│               │  - Signals completion  │                             │
│               └────────────┬───────────┘                             │
│                            │                                         │
│                            ▼                                         │
│               ┌────────────────────────┐                             │
│               │     SPI Hardware       │                             │
│               │  (SCLK/MOSI/MISO/CS)   │                             │
│               └────────────────────────┘                             │
└──────────────────────────────────────────────────────────────────────┘
```

Key properties of this architecture:
- **Single writer to SPI hardware**: only the manager task touches the SPI peripheral.
- **Non-blocking submission**: tasks enqueue requests and either block on a completion semaphore or proceed asynchronously with a callback.
- **Ordered execution**: FIFO queue ensures requests are served in submission order (priority queues are also possible).
- **Centralized error handling**: one place to handle timeouts, bus errors, and retries.

---

## Request Descriptor Design

A well-designed SPI request descriptor carries everything the manager task needs to execute the transaction without further interaction with the caller.

### Essential Fields

```c
typedef enum {
    SPI_TRANSFER_WRITE,         // TX only (ignore MISO)
    SPI_TRANSFER_READ,          // RX only (keep MOSI at 0 or 0xFF)
    SPI_TRANSFER_FULL_DUPLEX,   // Simultaneous TX and RX
} spi_transfer_type_t;

typedef struct {
    /* Target device identification */
    uint8_t             device_id;      // Index into device config table
    uint8_t             cs_pin;         // GPIO pin for chip select
    uint8_t             spi_mode;       // 0..3 (CPOL, CPHA)
    uint32_t            clock_hz;       // Transfer clock frequency

    /* Buffer pointers */
    spi_transfer_type_t transfer_type;
    const uint8_t      *tx_buf;         // Data to send (NULL for read-only)
    uint8_t            *rx_buf;         // Buffer to receive into (NULL for write-only)
    size_t              length;         // Number of bytes

    /* Completion notification */
    SemaphoreHandle_t   done_sem;       // Signalled on completion (synchronous)
    void              (*callback)(struct spi_request *req, int status); // OR async callback
    void               *user_data;      // Caller context passed to callback

    /* Result */
    int                 status;         // 0 = success, negative = error code
} spi_request_t;
```

The `done_sem`/`callback` duality supports two usage patterns:
- **Synchronous**: caller blocks on `done_sem` until the manager signals completion.
- **Asynchronous**: caller provides a callback; the manager invokes it from its own task context upon completion, and the caller continues immediately after enqueuing.

---

## Implementation in C/C++ (FreeRTOS)

### 1. Initialization

```c
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "spi_hal.h"   // Board-specific SPI HAL

#define SPI_QUEUE_DEPTH     16
#define SPI_MANAGER_STACK   512
#define SPI_MANAGER_PRIO    (configMAX_PRIORITIES - 2)

static QueueHandle_t s_spi_queue;

void spi_manager_init(void)
{
    s_spi_queue = xQueueCreate(SPI_QUEUE_DEPTH, sizeof(spi_request_t *));
    configASSERT(s_spi_queue != NULL);

    xTaskCreate(spi_manager_task, "SPI_MGR",
                SPI_MANAGER_STACK, NULL,
                SPI_MANAGER_PRIO, NULL);
}
```

Note we queue **pointers** to `spi_request_t`, not copies. The request structs live in the caller's stack (for synchronous use) or in heap/static storage (for async use). Queuing pointers is faster and avoids copying large descriptors.

---

### 2. The Manager Task

```c
static void spi_manager_task(void *arg)
{
    spi_request_t *req;

    for (;;)
    {
        /* Block indefinitely waiting for the next request */
        if (xQueueReceive(s_spi_queue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        /* --- Configure SPI peripheral for this device --- */
        spi_hal_set_mode(req->spi_mode);
        spi_hal_set_clock(req->clock_hz);

        /* --- Assert chip select --- */
        gpio_write(req->cs_pin, 0);

        /* --- Execute transfer --- */
        int status = 0;
        switch (req->transfer_type) {
            case SPI_TRANSFER_WRITE:
                status = spi_hal_write(req->tx_buf, req->length);
                break;
            case SPI_TRANSFER_READ:
                status = spi_hal_read(req->rx_buf, req->length);
                break;
            case SPI_TRANSFER_FULL_DUPLEX:
                status = spi_hal_transfer(req->tx_buf, req->rx_buf, req->length);
                break;
        }

        /* --- Deassert chip select --- */
        gpio_write(req->cs_pin, 1);

        /* --- Store result and notify caller --- */
        req->status = status;

        if (req->done_sem != NULL) {
            xSemaphoreGive(req->done_sem);     // Unblock synchronous caller
        } else if (req->callback != NULL) {
            req->callback(req, status);        // Invoke async callback
        }
    }
}
```

---

### 3. Synchronous Request (Blocking Caller)

```c
/**
 * Submit an SPI request and block until it completes.
 * Returns 0 on success, negative error code on failure.
 */
int spi_request_sync(spi_request_t *req, TickType_t timeout_ticks)
{
    /* Create a binary semaphore on the caller's stack region
       (must remain valid until the manager signals it) */
    StaticSemaphore_t sem_buf;
    req->done_sem  = xSemaphoreCreateBinaryStatic(&sem_buf);
    req->callback  = NULL;

    /* Enqueue the request pointer */
    if (xQueueSend(s_spi_queue, &req, timeout_ticks) != pdTRUE) {
        vSemaphoreDelete(req->done_sem);
        return -EBUSY;
    }

    /* Block until the manager signals completion */
    if (xSemaphoreTake(req->done_sem, timeout_ticks) != pdTRUE) {
        /* Timeout: request may still be in queue — caller must handle this */
        vSemaphoreDelete(req->done_sem);
        return -ETIMEDOUT;
    }

    vSemaphoreDelete(req->done_sem);
    return req->status;
}

/* Example usage from a sensor task */
void sensor_task(void *arg)
{
    uint8_t cmd[2]  = { 0xF3, 0x00 };   // "trigger temperature read"
    uint8_t resp[3] = { 0 };

    spi_request_t req = {
        .device_id    = DEVICE_ID_TEMP_SENSOR,
        .cs_pin       = GPIO_PIN_CS_TEMP,
        .spi_mode     = 0,
        .clock_hz     = 1000000,
        .transfer_type = SPI_TRANSFER_FULL_DUPLEX,
        .tx_buf       = cmd,
        .rx_buf       = resp,
        .length       = sizeof(cmd),
    };

    int ret = spi_request_sync(&req, pdMS_TO_TICKS(50));
    if (ret == 0) {
        int16_t raw_temp = (resp[1] << 8) | resp[2];
        process_temperature(raw_temp);
    }
}
```

---

### 4. Asynchronous Request (Non-blocking Caller)

```c
typedef struct {
    spi_request_t  req;
    TaskHandle_t   notify_task;
} async_spi_ctx_t;

static void on_spi_complete(spi_request_t *req, int status)
{
    async_spi_ctx_t *ctx = (async_spi_ctx_t *)req->user_data;
    /* Use task notification to wake the original task with the status */
    xTaskNotifyFromISR(ctx->notify_task,
                       (uint32_t)status,
                       eSetValueWithOverwrite,
                       NULL);
}

void display_task(void *arg)
{
    static uint8_t framebuf[320 * 240 / 2];  // Hypothetical 4bpp display
    static async_spi_ctx_t ctx;

    ctx.notify_task = xTaskGetCurrentTaskHandle();
    ctx.req = (spi_request_t){
        .device_id    = DEVICE_ID_LCD,
        .cs_pin       = GPIO_PIN_CS_LCD,
        .spi_mode     = 0,
        .clock_hz     = 40000000,
        .transfer_type = SPI_TRANSFER_WRITE,
        .tx_buf       = framebuf,
        .rx_buf       = NULL,
        .length       = sizeof(framebuf),
        .done_sem     = NULL,
        .callback     = on_spi_complete,
        .user_data    = &ctx,
    };

    for (;;) {
        render_frame(framebuf);

        /* Submit without blocking */
        xQueueSend(s_spi_queue, &ctx.req, portMAX_DELAY);

        /* Do other work here, or simply wait for notification */
        uint32_t result;
        xTaskNotifyWait(0, 0xFFFFFFFF, &result, portMAX_DELAY);

        if ((int)result != 0)
            handle_display_error((int)result);
    }
}
```

---

### 5. Priority Queue Extension (C++)

For mixed-criticality systems, a priority queue ensures urgent requests jump ahead:

```cpp
#include <queue>
#include <functional>

struct SpiRequestPriority {
    int priority;           // Higher = more urgent
    spi_request_t *req;

    bool operator<(const SpiRequestPriority &o) const {
        return priority < o.priority;  // max-heap
    }
};

class SpiPriorityQueue {
public:
    SpiPriorityQueue() {
        mutex_  = xSemaphoreCreateMutex();
        ready_  = xSemaphoreCreateBinary();
    }

    void push(spi_request_t *req, int priority) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        pq_.push({ priority, req });
        xSemaphoreGive(mutex_);
        xSemaphoreGive(ready_);   // Signal manager
    }

    spi_request_t *pop_blocking() {
        xSemaphoreTake(ready_, portMAX_DELAY);
        xSemaphoreTake(mutex_, portMAX_DELAY);
        auto top = pq_.top();
        pq_.pop();
        xSemaphoreGive(mutex_);
        return top.req;
    }

private:
    std::priority_queue<SpiRequestPriority> pq_;
    SemaphoreHandle_t mutex_;
    SemaphoreHandle_t ready_;
};
```

---

## Implementation in Rust (Embassy / RTIC)

Rust's ownership model and async/await make queue-based SPI management both safer and more expressive. Here we use **Embassy** (async embedded framework) with its built-in channel primitives.

### 1. Request Type Definition

```rust
use embassy_sync::channel::{Channel, Sender, Receiver};
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;
use embassy_sync::signal::Signal;

/// SPI transfer direction
#[derive(Debug, Clone, Copy)]
pub enum TransferType {
    WriteOnly,
    ReadOnly,
    FullDuplex,
}

/// A single SPI transaction request
pub struct SpiRequest<'a> {
    pub cs_pin:        u8,
    pub spi_mode:      u8,
    pub clock_hz:      u32,
    pub transfer_type: TransferType,
    pub tx_buf:        Option<&'a [u8]>,
    pub rx_buf:        Option<&'a mut [u8]>,
    /// Signal posted by manager when done; carries result
    pub done:          &'a Signal<ThreadModeRawMutex, Result<(), SpiError>>,
}

#[derive(Debug)]
pub enum SpiError {
    BusError,
    Timeout,
    InvalidConfig,
}
```

### 2. Channel Setup and Manager Task

```rust
use embassy_executor::Spawner;
use embassy_stm32::spi::{Config, Spi};
use embassy_stm32::gpio::{Output, Level, Speed};
use embassy_time::{Duration, with_timeout};

// Static channel: up to 8 pending requests, no heap allocation
static SPI_CHANNEL: Channel<ThreadModeRawMutex, SpiRequest<'static>, 8> = Channel::new();

/// Manager task — sole owner of the SPI peripheral
#[embassy_executor::task]
async fn spi_manager_task(mut spi: Spi<'static, SPI1, DMA1_CH3, DMA1_CH2>) {
    let rx = SPI_CHANNEL.receiver();

    loop {
        // Await next request
        let req = rx.receive().await;

        // Reconfigure SPI for this device (mode, clock)
        let mut config = Config::default();
        config.mode = match req.spi_mode {
            0 => embassy_stm32::spi::MODE_0,
            1 => embassy_stm32::spi::MODE_1,
            2 => embassy_stm32::spi::MODE_2,
            3 => embassy_stm32::spi::MODE_3,
            _ => {
                req.done.signal(Err(SpiError::InvalidConfig));
                continue;
            }
        };
        config.frequency = req.clock_hz.into();
        spi.set_config(&config).ok();

        // Assert CS (in a real system: use req.cs_pin to drive a GPIO)
        // gpio_cs[req.cs_pin as usize].set_low();

        let result = match req.transfer_type {
            TransferType::WriteOnly => {
                if let Some(tx) = req.tx_buf {
                    spi.write(tx).await.map_err(|_| SpiError::BusError)
                } else {
                    Err(SpiError::InvalidConfig)
                }
            }
            TransferType::ReadOnly => {
                if let Some(rx_buf) = req.rx_buf {
                    spi.read(rx_buf).await.map_err(|_| SpiError::BusError)
                } else {
                    Err(SpiError::InvalidConfig)
                }
            }
            TransferType::FullDuplex => {
                match (req.tx_buf, req.rx_buf) {
                    (Some(tx), Some(rx_buf)) => {
                        spi.transfer(rx_buf, tx).await.map_err(|_| SpiError::BusError)
                    }
                    _ => Err(SpiError::InvalidConfig),
                }
            }
        };

        // Deassert CS
        // gpio_cs[req.cs_pin as usize].set_high();

        // Notify caller
        req.done.signal(result);
    }
}
```

### 3. Synchronous Caller (Async Await)

```rust
use embassy_sync::signal::Signal;

async fn read_sensor(sender: Sender<'static, ThreadModeRawMutex, SpiRequest<'static>, 8>)
    -> Result<i16, SpiError>
{
    let tx_data: [u8; 2] = [0xF3, 0x00];
    let mut rx_data: [u8; 3] = [0u8; 3];
    let done_signal: Signal<ThreadModeRawMutex, Result<(), SpiError>> = Signal::new();

    // Build request — lifetimes enforced by borrow checker
    let req = SpiRequest {
        cs_pin:        0,
        spi_mode:      0,
        clock_hz:      1_000_000,
        transfer_type: TransferType::FullDuplex,
        tx_buf:        Some(&tx_data),
        rx_buf:        Some(&mut rx_data),
        done:          &done_signal,
    };

    // Submit and await completion (zero-cost abstraction — no thread blocking)
    sender.send(req).await;
    done_signal.wait().await?;

    let raw = ((rx_data[1] as i16) << 8) | rx_data[2] as i16;
    Ok(raw)
}
```

### 4. Async with Timeout

```rust
use embassy_time::{Duration, with_timeout, TimeoutError};

async fn read_sensor_with_timeout(
    sender: Sender<'static, ThreadModeRawMutex, SpiRequest<'static>, 8>
) -> Result<i16, SpiError> {
    match with_timeout(Duration::from_millis(50), read_sensor(sender)).await {
        Ok(result) => result,
        Err(TimeoutError) => Err(SpiError::Timeout),
    }
}
```

### 5. RTIC-style Queue with Message Passing

For **RTIC** (Real-Time Interrupt-driven Concurrency), the pattern maps naturally to software tasks and queues:

```rust
#[rtic::app(device = stm32f4xx_hal::pac, dispatchers = [EXTI0, EXTI1])]
mod app {
    use rtic_sync::{channel, make_channel};
    use stm32f4xx_hal::spi::Spi;

    // Create a channel for up to 8 SPI requests
    const CAPACITY: usize = 8;

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        spi: Spi<SPI1>,
        spi_rx: channel::Receiver<'static, SpiCmd, CAPACITY>,
    }

    #[derive(Clone)]
    pub struct SpiCmd {
        pub data: [u8; 32],
        pub len:  usize,
        pub cs:   u8,
    }

    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        let (tx, rx) = make_channel!(SpiCmd, CAPACITY);

        // Store sender somewhere accessible (e.g., shared resource or passed to tasks)
        let spi = configure_spi(cx.device.SPI1);

        // Spawn producer tasks, giving them a clone of the sender
        sensor_task::spawn(tx.clone()).ok();
        display_task::spawn(tx).ok();

        (Shared {}, Local { spi, spi_rx: rx })
    }

    /// SPI manager — highest priority software task
    #[task(local = [spi, spi_rx], priority = 3)]
    async fn spi_manager(cx: spi_manager::Context) {
        loop {
            let cmd = cx.local.spi_rx.recv().await.unwrap();
            // Assert CS, transfer, deassert CS
            let _ = cx.local.spi.transfer_in_place(&mut cmd.data[..cmd.len]);
        }
    }

    #[task(priority = 1)]
    async fn sensor_task(_cx: sensor_task::Context, tx: channel::Sender<'static, SpiCmd, CAPACITY>) {
        loop {
            let cmd = SpiCmd { data: [0xF3; 32], len: 2, cs: 0 };
            tx.send(cmd).await.ok();
            embassy_time::Timer::after_millis(100).await;
        }
    }

    #[task(priority = 1)]
    async fn display_task(_cx: display_task::Context, tx: channel::Sender<'static, SpiCmd, CAPACITY>) {
        loop {
            let cmd = SpiCmd { data: [0xAA; 32], len: 32, cs: 1 };
            tx.send(cmd).await.ok();
            embassy_time::Timer::after_millis(16).await;
        }
    }
}
```

---

## Advanced Patterns

### Chained Transactions

Some SPI peripherals require multi-phase exchanges (e.g., send a command byte, then send data). Use a linked list or small array of transactions within one request:

```c
#define SPI_MAX_SEGMENTS 4

typedef struct {
    const uint8_t *tx_buf;
    uint8_t       *rx_buf;
    size_t         length;
    bool           keep_cs_asserted;  // Don't deassert CS between segments
} spi_segment_t;

typedef struct {
    uint8_t         cs_pin;
    uint8_t         spi_mode;
    uint32_t        clock_hz;
    spi_segment_t   segments[SPI_MAX_SEGMENTS];
    uint8_t         num_segments;
    SemaphoreHandle_t done_sem;
    int             status;
} spi_chained_request_t;
```

The manager iterates segments, keeping CS low between them, only releasing it after the last segment.

### DMA-backed Transfers

For high throughput, the manager task triggers a DMA SPI transfer and suspends itself (via task notification or semaphore), then the DMA complete ISR wakes it:

```c
static SemaphoreHandle_t s_dma_done;

void SPI1_DMA_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    if (DMA_GetITStatus(DMA1_Stream3, DMA_IT_TCIF3)) {
        DMA_ClearITPendingBit(DMA1_Stream3, DMA_IT_TCIF3);
        xSemaphoreGiveFromISR(s_dma_done, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

// Inside manager task, after starting DMA:
SPI_DMA_Start(req->tx_buf, req->rx_buf, req->length);
xSemaphoreTake(s_dma_done, portMAX_DELAY);  // Yield until DMA complete
```

### Request Pooling

Allocating `spi_request_t` on the heap per transfer causes fragmentation. Use a static pool:

```c
#define POOL_SIZE 8
static spi_request_t  s_pool[POOL_SIZE];
static StaticQueue_t  s_pool_queue_buf;
static uint8_t        s_pool_storage[POOL_SIZE * sizeof(spi_request_t *)];
static QueueHandle_t  s_pool_queue;

void spi_pool_init(void) {
    s_pool_queue = xQueueCreateStatic(POOL_SIZE, sizeof(spi_request_t *),
                                      s_pool_storage, &s_pool_queue_buf);
    for (int i = 0; i < POOL_SIZE; i++) {
        spi_request_t *p = &s_pool[i];
        xQueueSend(s_pool_queue, &p, 0);
    }
}

spi_request_t *spi_pool_alloc(TickType_t timeout) {
    spi_request_t *p = NULL;
    xQueueReceive(s_pool_queue, &p, timeout);
    return p;
}

void spi_pool_free(spi_request_t *req) {
    xQueueSend(s_pool_queue, &req, 0);
}
```

---

## Error Handling and Timeout Management

### Queue Full Scenarios

When the queue is full, `xQueueSend` with a finite timeout will fail. The caller must handle this gracefully:

```c
int ret = xQueueSend(s_spi_queue, &req, pdMS_TO_TICKS(10));
if (ret != pdTRUE) {
    log_error("SPI queue full — dropping request for device %d", req->device_id);
    return -EAGAIN;
}
```

Alternative strategies:
- **Drop oldest** (overwrite queue front — unusual, requires custom implementation)
- **Drop lowest priority** (requires priority-aware queue)
- **Block caller** (`portMAX_DELAY`) — risks deadlock if queue is permanently full

### Transfer Timeouts

The manager task itself should time-bound each transfer to prevent a stuck peripheral from starving all other requests:

```c
// In manager task, use a watchdog or timed semaphore on DMA completion:
TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(200);
if (xSemaphoreTake(s_dma_done, deadline - xTaskGetTickCount()) != pdTRUE) {
    spi_hal_abort();     // Reset SPI peripheral
    req->status = -ETIMEDOUT;
    // Notify caller of timeout
}
```

### Rust: Typed Error Propagation

Rust's `Result<T, E>` makes error propagation explicit and zero-cost:

```rust
#[derive(Debug, defmt::Format)]
pub enum SpiError {
    BusError(u32),   // HAL error code
    Timeout,
    QueueFull,
    InvalidConfig,
}

impl From<embassy_stm32::spi::Error> for SpiError {
    fn from(e: embassy_stm32::spi::Error) -> Self {
        SpiError::BusError(e as u32)
    }
}

// Usage: ? operator propagates errors up the call stack
async fn write_register(addr: u8, val: u8, tx: Sender<...>) -> Result<(), SpiError> {
    let data = [addr | 0x80, val];
    submit_write(tx, &data).await?;   // Propagates SpiError automatically
    Ok(())
}
```

---

## Performance Considerations

### Queue Depth Sizing

| Queue depth | Trade-off |
|-------------|-----------|
| Too shallow | Producers block/drop requests; reduces throughput |
| Too deep    | Large bursts of stale data; increased latency for urgent requests; memory overhead |

A practical formula: `queue_depth = max_producers × max_burst_per_producer + 2` as a starting point, then profile under load.

### Latency vs. Throughput

- **Synchronous mode** (caller blocks): adds one context switch overhead per transaction (~microseconds on typical ARM Cortex-M). Best for latency-sensitive, infrequent transactions.
- **Asynchronous mode** (callback): zero caller overhead after submission. Best for high-throughput streaming (e.g., display refresh).

### Memory Usage

Each pending request occupies memory. In C with 8-request queue of pointers (4 bytes each), the queue itself is tiny (32 bytes). The request structs — if stack-allocated by synchronous callers — cost only during the blocking period.

In Rust with Embassy's `Channel`, the ring buffer is statically allocated at compile time: `size_of::<SpiRequest>() × CAPACITY` bytes in flash/BSS.

---

## Summary

Queue-based SPI request management is an essential architectural pattern for multi-tasking embedded systems with shared SPI buses. By funneling all SPI transactions through a single manager task and a bounded FIFO queue, the design eliminates bus contention, enables clean mode switching between devices, provides natural back-pressure, and centralizes error handling.

**Key takeaways:**

- The **request descriptor** carries all information needed for autonomous execution: device identity, SPI mode/clock, buffer pointers, transfer type, and a completion notification mechanism.
- The **manager task** is the sole owner of the SPI peripheral, sequentially dequeuing and executing requests — no mutexes, no race conditions.
- **Synchronous callers** block on a semaphore or async signal after enqueuing, seamlessly integrating with RTOS task scheduling.
- **Asynchronous callers** submit with a callback and continue immediately, ideal for high-throughput streaming workloads.
- **Rust with Embassy/RTIC** provides compile-time queue sizing, ownership-enforced buffer lifetime safety, and zero-cost async abstractions — eliminating entire classes of bugs possible in C.
- **Advanced patterns** — chained transactions, DMA integration, priority queues, request pooling — build naturally on the base architecture to meet real-world requirements.
- **Error handling** must account for queue-full conditions, transfer timeouts, and hardware faults; centralizing this in the manager task keeps producer tasks simple.

This pattern scales from small 8-bit microcontrollers running FreeRTOS with kilobytes of RAM to high-performance Cortex-M7 systems, and is directly applicable wherever multiple software components compete for a shared serial bus.

---

*Document: 57 — Queue-based SPI Requests | Embedded Systems RTOS Patterns Series*