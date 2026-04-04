# 99. SPI Bus Sharing Strategies

**7 Strategies documented**, each with C/C++ and/or Rust code:

1. **Hardware CS Multiplexing** — per-device CS lines, reconfigure before each transaction; minimal overhead, no RTOS needed.
2. **Mutex / Lock-Based Arbitration** (FreeRTOS) — `xSemaphoreTake`/`Give` around every transaction; includes an ISR-safe deferred queue pattern.
3. **Token-Passing / Round-Robin** — time-sliced bus slots via a timer callback; gives bounded worst-case latency per user.
4. **Priority-Based Arbitration** — binary heap priority queue in C++17 and a `heapless::BinaryHeap` Rust version; high-priority tasks get bus access with minimal delay.
5. **DMA Double Buffering** — STM32 HAL DMA with swap-on-completion callback; maximises throughput while keeping the CPU free.
6. **Message Queue / Bus Manager** (the recommended production pattern) — a dedicated RTOS task owns the bus; all callers enqueue jobs and wait on a binary semaphore.
7. **MUX IC Bus Switching** — GPIO-selected 74HC4051-style mux for systems with too many devices or conflicting SPI modes.

**Rust section** covers `embedded-hal-bus` (`MutexDevice`, `CriticalSectionDevice`), Embassy's async `SpiDevice`, and a `no_std` priority queue with `heapless`.

The document ends with a **comparison table** and a **summary** of the five key principles (atomicity, ownership, configuration hygiene, ISR awareness, bounded latency).

## Coordinating Access to Shared SPI Buses in Complex Systems

---

## Table of Contents

1. [Introduction](#introduction)
2. [SPI Bus Fundamentals Recap](#spi-bus-fundamentals-recap)
3. [Why Bus Sharing Is Challenging](#why-bus-sharing-is-challenging)
4. [Strategy 1: Hardware Chip Select Multiplexing](#strategy-1-hardware-chip-select-multiplexing)
5. [Strategy 2: Software Mutex / Lock-Based Arbitration](#strategy-2-software-mutex--lock-based-arbitration)
6. [Strategy 3: Token-Passing / Round-Robin Scheduling](#strategy-3-token-passing--round-robin-scheduling)
7. [Strategy 4: Priority-Based Arbitration](#strategy-4-priority-based-arbitration)
8. [Strategy 5: DMA-Based Double Buffering](#strategy-5-dma-based-double-buffering)
9. [Strategy 6: Message Queue / Bus Manager Pattern](#strategy-6-message-queue--bus-manager-pattern)
10. [Strategy 7: SPI Bus Switching / Multiplexer ICs](#strategy-7-spi-bus-switching--multiplexer-ics)
11. [Rust Implementations](#rust-implementations)
12. [Comparison Table](#comparison-table)
13. [Summary](#summary)

---

## Introduction

The Serial Peripheral Interface (SPI) is a synchronous, full-duplex serial communication protocol widely used in embedded systems to connect microcontrollers to peripherals such as sensors, displays, flash memory, ADCs, and DACs. In small systems with only one or two peripherals, SPI bus management is trivial. However, in complex systems — multi-sensor platforms, IoT gateways, industrial controllers, or safety-critical avionics — a single SPI bus may need to be shared among many devices and multiple software tasks or interrupt service routines (ISRs).

Shared SPI buses introduce concurrency problems: if two tasks simultaneously attempt to drive the clock and data lines, data corruption, bus contention, and incorrect device selection can result. Solving this reliably requires deliberate **bus sharing strategies** that balance correctness, latency, throughput, and code complexity.

This document covers the most important strategies for SPI bus sharing, with practical C/C++ and Rust code examples suitable for bare-metal, RTOS-based, and `embedded-hal`-based systems.

---

## SPI Bus Fundamentals Recap

Before discussing sharing strategies, a brief recap of the SPI bus structure:

```
Master                     Slave 0       Slave 1       Slave 2
  SCK  ─────────────────────── SCK ─────── SCK ─────── SCK
  MOSI ─────────────────────── MOSI ────── MOSI ─────── MOSI
  MISO ─────────────────────── MISO ────── MISO ─────── MISO
  CS0  ──────────────────────── CS  (low = selected)
  CS1  ──────────────────────────────────── CS
  CS2  ────────────────────────────────────────────────── CS
```

Key properties relevant to bus sharing:

- **MOSI / MISO / SCK** are shared among all slaves; only CS lines differ.
- A transaction must be **atomic**: CS must stay asserted for the entire transfer.
- SPI has **no built-in arbitration** (unlike I²C); the master is solely responsible.
- Configuration parameters (CPOL, CPHA, clock speed) may differ between devices, requiring reconfiguration between transactions.

---

## Why Bus Sharing Is Challenging

Several factors make SPI bus sharing non-trivial:

1. **No bus arbitration hardware** — the master must implement all coordination in software.
2. **CS line timing** — a transaction interrupted mid-way leaves the CS asserted and the slave in an undefined state.
3. **Configuration reconfiguration** — different slaves may require different SPI modes (CPOL/CPHA) or clock frequencies.
4. **DMA and interrupt interactions** — DMA transfers run asynchronously; a new transaction must not begin until DMA completion.
5. **RTOS task scheduling** — preemptive schedulers can interrupt a transaction at any point without additional protection.
6. **ISR vs. task access** — ISRs cannot use blocking mutexes; special care is needed.

---

## Strategy 1: Hardware Chip Select Multiplexing

### Concept

The simplest sharing strategy: route each device to a unique CS line, and enforce that only one CS is asserted at a time. The bus configuration (mode, speed) must be set before each transaction.

### When to Use

- Few devices, simple bare-metal system, no RTOS.
- All devices use the same SPI mode and clock speed (or reconfiguration is cheap).

### C/C++ Example (bare-metal STM32-style HAL)

```c
#include <stdint.h>
#include <stdbool.h>

// SPI and GPIO abstraction (platform-specific)
typedef struct {
    volatile uint32_t *CS_port;
    uint32_t           CS_pin;
    uint8_t            spi_mode;   // CPOL<<1 | CPHA
    uint32_t           clock_hz;
} spi_device_t;

// Global bus-in-use flag (set/clear must be atomic in real systems)
static volatile bool spi_bus_busy = false;

// Reconfigure SPI peripheral for the given device
static void spi_configure(const spi_device_t *dev) {
    SPI1->CR1 &= ~SPI_CR1_SPE;                 // disable SPI
    SPI1->CR1  = (dev->spi_mode << 0)          // CPHA / CPOL
               | SPI_CR1_MSTR                   // master mode
               | compute_baudrate(dev->clock_hz); // baud rate prescaler
    SPI1->CR1 |= SPI_CR1_SPE;                  // re-enable
}

// Assert CS (active-low)
static inline void cs_assert(const spi_device_t *dev) {
    *dev->CS_port &= ~dev->CS_pin;
}

// Deassert CS
static inline void cs_deassert(const spi_device_t *dev) {
    *dev->CS_port |= dev->CS_pin;
}

// Transfer a single byte
static uint8_t spi_transfer_byte(uint8_t tx) {
    while (!(SPI1->SR & SPI_SR_TXE));   // wait for TX empty
    SPI1->DR = tx;
    while (!(SPI1->SR & SPI_SR_RXNE));  // wait for RX not empty
    return (uint8_t)SPI1->DR;
}

// Full transaction: configure → assert CS → transfer → deassert CS
bool spi_transaction(const spi_device_t *dev,
                     const uint8_t *tx_buf, uint8_t *rx_buf,
                     size_t len) {
    if (spi_bus_busy) return false;      // Simple busy check (not thread-safe!)
    spi_bus_busy = true;

    spi_configure(dev);
    cs_assert(dev);

    for (size_t i = 0; i < len; i++) {
        uint8_t rx = spi_transfer_byte(tx_buf ? tx_buf[i] : 0xFF);
        if (rx_buf) rx_buf[i] = rx;
    }

    cs_deassert(dev);
    spi_bus_busy = false;
    return true;
}
```

> **Note:** The `spi_bus_busy` flag above is not thread-safe. In a multi-task environment, a proper mutex is required (see Strategy 2).

---

## Strategy 2: Software Mutex / Lock-Based Arbitration

### Concept

In an RTOS environment (FreeRTOS, Zephyr, ThreadX), a **mutex** protects the SPI bus as a shared resource. Any task that wants to perform a transaction must first acquire the mutex, and release it when done. The RTOS scheduler blocks waiting tasks and resumes them when the mutex becomes available.

### When to Use

- RTOS-based system with multiple tasks accessing the same SPI bus.
- Acceptable to block a task while waiting for the bus.
- ISRs must NOT use this strategy directly (use deferred processing instead).

### C/C++ Example (FreeRTOS)

```c
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

// SPI bus handle
typedef struct {
    SemaphoreHandle_t mutex;
    SPI_HandleTypeDef *hspi;        // HAL SPI handle (STM32 CubeHAL)
} spi_bus_t;

// Per-device descriptor
typedef struct {
    spi_bus_t  *bus;
    GPIO_TypeDef *cs_port;
    uint16_t    cs_pin;
    uint32_t    timeout_ms;
} spi_dev_t;

// Initialize bus (called once at startup)
void spi_bus_init(spi_bus_t *bus, SPI_HandleTypeDef *hspi) {
    bus->hspi  = hspi;
    bus->mutex = xSemaphoreCreateMutex();
    configASSERT(bus->mutex != NULL);
}

// Perform a transaction with mutex protection
HAL_StatusTypeDef spi_transact(spi_dev_t *dev,
                                uint8_t *tx, uint8_t *rx,
                                uint16_t size) {
    spi_bus_t *bus = dev->bus;

    // Acquire mutex – block up to timeout_ms
    if (xSemaphoreTake(bus->mutex,
                       pdMS_TO_TICKS(dev->timeout_ms)) != pdTRUE) {
        return HAL_TIMEOUT;
    }

    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET); // CS low
    HAL_StatusTypeDef status;

    if (tx && rx) {
        status = HAL_SPI_TransmitReceive(bus->hspi, tx, rx, size,
                                         dev->timeout_ms);
    } else if (tx) {
        status = HAL_SPI_Transmit(bus->hspi, tx, size, dev->timeout_ms);
    } else {
        status = HAL_SPI_Receive(bus->hspi, rx, size, dev->timeout_ms);
    }

    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);   // CS high
    xSemaphoreGive(bus->mutex);                                    // release
    return status;
}

// -----------------------------------------------------------------------
// Usage example: two tasks sharing the same bus
// -----------------------------------------------------------------------

static spi_bus_t shared_bus;
static spi_dev_t flash_dev  = { &shared_bus, GPIOA, GPIO_PIN_4,  100 };
static spi_dev_t sensor_dev = { &shared_bus, GPIOB, GPIO_PIN_12, 50  };

void task_flash(void *arg) {
    uint8_t cmd[4] = { 0x03, 0x00, 0x10, 0x00 };  // READ command
    uint8_t data[64];
    for (;;) {
        spi_transact(&flash_dev, cmd, data, sizeof(data));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void task_sensor(void *arg) {
    uint8_t req[2] = { 0x80 | 0x28, 0x00 };  // Auto-increment read
    uint8_t result[2];
    for (;;) {
        spi_transact(&sensor_dev, req, result, sizeof(result));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### ISR-Safe Deferred Approach

ISRs cannot call `xSemaphoreTake` (blocking). Instead, post a message to a queue and handle it in a dedicated task:

```c
typedef struct {
    spi_dev_t *dev;
    uint8_t    tx[16];
    size_t     len;
} spi_request_t;

static QueueHandle_t spi_queue;

// Called from ISR
void sensor_data_ready_isr(void) {
    spi_request_t req = {
        .dev = &sensor_dev,
        .tx  = { 0xA8, 0x00 },
        .len = 2,
    };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(spi_queue, &req, &woken);
    portYIELD_FROM_ISR(woken);
}

// Dedicated SPI manager task
void task_spi_manager(void *arg) {
    spi_request_t req;
    for (;;) {
        if (xQueueReceive(spi_queue, &req, portMAX_DELAY) == pdTRUE) {
            uint8_t rx[16];
            spi_transact(req.dev, req.tx, rx, req.len);
            // process rx...
        }
    }
}
```

---

## Strategy 3: Token-Passing / Round-Robin Scheduling

### Concept

Each bus user takes turns accessing the bus in a fixed round-robin order. A central "token" (counter or flag) indicates whose turn it is. This prevents starvation but can waste time when some users have nothing to transfer.

### When to Use

- Multiple equally-priority tasks with predictable, periodic transfer needs.
- Real-time systems requiring bounded worst-case latency.
- Useful in time-triggered (TT) architectures.

### C/C++ Example

```c
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "timers.h"

#define MAX_SPI_USERS 4

typedef void (*spi_user_callback_t)(void);

typedef struct {
    spi_user_callback_t users[MAX_SPI_USERS];
    uint8_t             num_users;
    uint8_t             current;
    SemaphoreHandle_t   done_sem;   // signalled when user finishes
    TimerHandle_t       slot_timer; // time-slice timer
} spi_token_bus_t;

static spi_token_bus_t token_bus;

// Called by each user when its transfer is complete
void spi_token_release(spi_token_bus_t *bus) {
    xSemaphoreGive(bus->done_sem);
}

// Timer callback advances to the next token holder
static void token_advance(TimerHandle_t xTimer) {
    spi_token_bus_t *bus = (spi_token_bus_t *)pvTimerGetTimerID(xTimer);
    bus->current = (bus->current + 1) % bus->num_users;

    // Invoke next user's transfer callback
    if (bus->users[bus->current]) {
        bus->users[bus->current]();
    }
}

void spi_token_bus_init(spi_token_bus_t *bus,
                         spi_user_callback_t *users,
                         uint8_t count,
                         uint32_t slot_ms) {
    bus->num_users = count;
    bus->current   = 0;
    for (uint8_t i = 0; i < count; i++) bus->users[i] = users[i];

    bus->done_sem   = xSemaphoreCreateBinary();
    bus->slot_timer = xTimerCreate("SPIToken", pdMS_TO_TICKS(slot_ms),
                                   pdTRUE, bus, token_advance);
    xTimerStart(bus->slot_timer, 0);
}

// -----------------------------------------------------------------------
// Usage: three periodic users sharing the bus
// -----------------------------------------------------------------------

static void user_flash_transfer(void)  { /* start flash read... */ }
static void user_sensor_transfer(void) { /* start sensor read... */ }
static void user_display_transfer(void){ /* start display write... */ }

spi_user_callback_t users[] = {
    user_flash_transfer,
    user_sensor_transfer,
    user_display_transfer,
};

void app_main(void) {
    spi_token_bus_init(&token_bus, users, 3, /*slot_ms=*/5);
    // Each user gets a 5 ms slot every 15 ms
}
```

---

## Strategy 4: Priority-Based Arbitration

### Concept

Assign priorities to SPI users. High-priority requests (e.g., safety-critical sensor reads) preempt lower-priority ones after the current transaction completes. This is often implemented with a **priority queue** in the bus manager.

### C/C++ Example (priority queue with FreeRTOS)

```c
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>

#define SPI_QUEUE_DEPTH 16

typedef struct {
    uint8_t        priority;    // 0 = highest
    spi_dev_t     *dev;
    uint8_t        tx[32];
    uint8_t       *rx;
    size_t         len;
    SemaphoreHandle_t done;     // caller waits on this
} spi_job_t;

static QueueHandle_t spi_prio_queue;

// Submit a job; blocks until complete
void spi_submit_blocking(uint8_t priority,
                          spi_dev_t *dev,
                          const uint8_t *tx, uint8_t *rx,
                          size_t len) {
    spi_job_t job = {
        .priority = priority,
        .dev      = dev,
        .rx       = rx,
        .len      = len,
        .done     = xSemaphoreCreateBinary(),
    };
    if (tx) memcpy(job.tx, tx, len);

    // FreeRTOS queues are FIFO; for true priority ordering,
    // sort on insert or use a heap-based queue.
    // Here we demonstrate with a sorted insert helper.
    spi_queue_insert_sorted(spi_prio_queue, &job);

    // Block until bus manager signals completion
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
}

// Bus manager task — processes jobs in priority order
void task_spi_bus_manager(void *arg) {
    spi_job_t job;
    for (;;) {
        if (xQueueReceive(spi_prio_queue, &job, portMAX_DELAY)) {
            spi_transact(job.dev, job.tx, job.rx, job.len);
            xSemaphoreGive(job.done);
        }
    }
}

// -----------------------------------------------------------------------
// Sorted-insert helper (linear scan; suitable for short queues)
// -----------------------------------------------------------------------
void spi_queue_insert_sorted(QueueHandle_t q, const spi_job_t *new_job) {
    // In production: use a heap or priority-aware queue structure.
    // FreeRTOS 10+ supports event groups; a true priority queue
    // requires a custom implementation or third-party library.
    xQueueSendToBack(q, new_job, portMAX_DELAY); // simplified
}
```

### Real priority queue with a binary heap (C++17)

```cpp
#include <queue>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>

struct SpiJob {
    int          priority;  // lower = more urgent
    std::function<void()> execute;

    bool operator>(const SpiJob &o) const { return priority > o.priority; }
};

class SpiBusManager {
public:
    void submit(int priority, std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            queue_.push({ priority, std::move(fn) });
        }
        cv_.notify_one();
    }

    void run() {  // Called from dedicated bus-manager thread
        for (;;) {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this]{ return !queue_.empty(); });
            SpiJob job = queue_.top();
            queue_.pop();
            lk.unlock();
            job.execute();
        }
    }

private:
    std::priority_queue<SpiJob,
                        std::vector<SpiJob>,
                        std::greater<SpiJob>> queue_;
    std::mutex              mu_;
    std::condition_variable cv_;
};
```

---

## Strategy 5: DMA-Based Double Buffering

### Concept

DMA transfers offload the CPU. However, starting a new DMA transfer before the previous one completes corrupts both transactions. Double buffering (two transfer buffers, A and B) allows preparation of the next transfer while the DMA finishes the current one. A completion callback swaps the active buffer and starts the next DMA transfer.

### When to Use

- High-throughput SPI (e.g., LCD frame buffer updates, audio streaming).
- CPU must remain free during transfers.
- Latency-sensitive: next transfer should start with minimal delay after previous completes.

### C/C++ Example (STM32 HAL with DMA callbacks)

```c
#include "stm32f4xx_hal.h"
#include <string.h>

#define BUF_SIZE 256

typedef struct {
    SPI_HandleTypeDef *hspi;
    uint8_t            buf[2][BUF_SIZE];  // double buffer
    uint8_t            active;            // which buffer is in DMA
    volatile bool      transfer_done;
} spi_dma_ctx_t;

static spi_dma_ctx_t dma_ctx;

// Prepare the inactive buffer with new data
void spi_dma_fill_next(spi_dma_ctx_t *ctx, const uint8_t *data, size_t len) {
    uint8_t next = ctx->active ^ 1;
    memcpy(ctx->buf[next], data, len);
}

// Kick off a DMA transfer using the active buffer
void spi_dma_start(spi_dma_ctx_t *ctx, size_t len) {
    ctx->transfer_done = false;
    HAL_SPI_Transmit_DMA(ctx->hspi, ctx->buf[ctx->active], len);
}

// HAL DMA complete callback
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance == SPI1) {
        dma_ctx.active ^= 1;               // swap buffers
        dma_ctx.transfer_done = true;
    }
}

// Application loop: continuously stream data
void app_stream_loop(const uint8_t *stream, size_t frame_size) {
    // Pre-fill both buffers
    memcpy(dma_ctx.buf[0], stream, frame_size);
    memcpy(dma_ctx.buf[1], stream + frame_size, frame_size);
    dma_ctx.active = 0;

    spi_dma_start(&dma_ctx, frame_size);

    for (size_t offset = 2 * frame_size; ; offset += frame_size) {
        // While DMA works on active buffer, fill inactive buffer
        spi_dma_fill_next(&dma_ctx, stream + offset, frame_size);

        // Wait for DMA to complete (use semaphore in RTOS)
        while (!dma_ctx.transfer_done);

        // Buffer swapped in callback; start next transfer immediately
        spi_dma_start(&dma_ctx, frame_size);
    }
}
```

---

## Strategy 6: Message Queue / Bus Manager Pattern

### Concept

A dedicated **bus manager task** owns the SPI bus exclusively. All other tasks submit transfer requests via a message queue. The bus manager dequeues and executes one request at a time, optionally notifying the requester of completion via a callback or semaphore. This completely decouples the SPI hardware from application logic.

This is the **most robust** strategy for complex RTOS-based systems and is widely used in production embedded firmware.

### C/C++ Example (FreeRTOS, full pattern)

```c
// spi_bus_manager.h
#pragma once
#include "FreeRTOS.h"
#include "semphr.h"
#include <stddef.h>
#include <stdint.h>

#define SPI_MGR_QUEUE_DEPTH 8
#define SPI_MGR_MAX_PAYLOAD 64

typedef enum {
    SPI_OP_WRITE,
    SPI_OP_READ,
    SPI_OP_WRITE_READ,
} spi_op_t;

typedef struct {
    spi_op_t          op;
    spi_dev_t        *dev;
    uint8_t           tx_buf[SPI_MGR_MAX_PAYLOAD];
    uint8_t          *rx_buf;         // Caller-provided receive buffer
    uint16_t          len;
    SemaphoreHandle_t completion;     // Binary semaphore: signalled when done
    HAL_StatusTypeDef result;         // Filled by manager
} spi_msg_t;

void     spi_mgr_init(void);
bool     spi_mgr_enqueue(spi_msg_t *msg, uint32_t timeout_ms);
void     spi_mgr_task(void *arg);  // Register as FreeRTOS task

// spi_bus_manager.c
#include "spi_bus_manager.h"
#include "queue.h"

static QueueHandle_t spi_mgr_queue;

void spi_mgr_init(void) {
    spi_mgr_queue = xQueueCreate(SPI_MGR_QUEUE_DEPTH, sizeof(spi_msg_t *));
    configASSERT(spi_mgr_queue != NULL);

    xTaskCreate(spi_mgr_task, "SPIMgr",
                configMINIMAL_STACK_SIZE * 4,
                NULL, tskIDLE_PRIORITY + 4, NULL);
}

bool spi_mgr_enqueue(spi_msg_t *msg, uint32_t timeout_ms) {
    return xQueueSend(spi_mgr_queue, &msg,
                      pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void spi_mgr_task(void *arg) {
    spi_msg_t *msg;
    for (;;) {
        if (xQueueReceive(spi_mgr_queue, &msg, portMAX_DELAY) != pdTRUE)
            continue;

        switch (msg->op) {
        case SPI_OP_WRITE:
            msg->result = HAL_SPI_Transmit(
                msg->dev->bus->hspi, msg->tx_buf, msg->len, 100);
            break;
        case SPI_OP_READ:
            msg->result = HAL_SPI_Receive(
                msg->dev->bus->hspi, msg->rx_buf, msg->len, 100);
            break;
        case SPI_OP_WRITE_READ:
            msg->result = HAL_SPI_TransmitReceive(
                msg->dev->bus->hspi, msg->tx_buf, msg->rx_buf, msg->len, 100);
            break;
        }

        xSemaphoreGive(msg->completion);  // Signal caller
    }
}

// -----------------------------------------------------------------------
// Usage from application task
// -----------------------------------------------------------------------
void task_read_sensor(void *arg) {
    spi_msg_t msg = {
        .op         = SPI_OP_WRITE_READ,
        .dev        = &sensor_dev,
        .tx_buf     = { 0x80 | 0x28, 0x00 },
        .rx_buf     = (uint8_t[2]){},
        .len        = 2,
        .completion = xSemaphoreCreateBinary(),
    };

    for (;;) {
        spi_mgr_enqueue(&msg, 100);
        xSemaphoreTake(msg.completion, portMAX_DELAY);
        // msg.rx_buf now contains sensor data
        process_sensor_data(msg.rx_buf);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

## Strategy 7: SPI Bus Switching / Multiplexer ICs

### Concept

When the number of devices exceeds available CS lines, or when devices have conflicting SPI modes, a **SPI multiplexer IC** (e.g., 74HC4051, TMUX1208, MAX4619) can switch the MOSI/MISO/SCK signals to different device groups. The software selects the active group via GPIO, then uses standard CS lines within that group.

### C/C++ Example

```c
#include <stdint.h>

// 3-bit address to select one of 8 MUX channels
#define MUX_SEL_A_PIN  GPIO_PIN_0
#define MUX_SEL_B_PIN  GPIO_PIN_1
#define MUX_SEL_C_PIN  GPIO_PIN_2
#define MUX_SEL_PORT   GPIOC

typedef enum {
    MUX_CHANNEL_SENSORS  = 0,  // Binary 000
    MUX_CHANNEL_DISPLAY  = 1,  // Binary 001
    MUX_CHANNEL_STORAGE  = 2,  // Binary 010
    MUX_CHANNEL_RADIO    = 3,  // Binary 011
} spi_mux_channel_t;

void spi_mux_select(spi_mux_channel_t ch) {
    uint32_t pins = ((ch & 1) ? MUX_SEL_A_PIN : 0)
                  | ((ch & 2) ? MUX_SEL_B_PIN : 0)
                  | ((ch & 4) ? MUX_SEL_C_PIN : 0);

    // Clear all three select pins, then set required ones atomically
    HAL_GPIO_WritePin(MUX_SEL_PORT,
                      MUX_SEL_A_PIN | MUX_SEL_B_PIN | MUX_SEL_C_PIN,
                      GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUX_SEL_PORT, pins, GPIO_PIN_SET);

    // Setup time: multiplexer switching delay (check datasheet, typically <10ns)
    __NOP(); __NOP();
}

// Transaction on a muxed bus
bool spi_mux_transact(spi_mux_channel_t channel,
                       spi_dev_t *dev,
                       const uint8_t *tx, uint8_t *rx,
                       size_t len) {
    // Must hold mutex to protect both MUX selection and transfer
    if (xSemaphoreTake(shared_bus.mutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return false;

    spi_mux_select(channel);
    spi_configure(dev);

    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(dev->bus->hspi, (uint8_t*)tx, rx, len, 50);
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);

    xSemaphoreGive(shared_bus.mutex);
    return true;
}
```

---

## Rust Implementations

Rust's `embedded-hal` trait ecosystem provides type-safe, zero-cost abstractions for SPI that map naturally onto the sharing strategies above. The `embedded-hal` 1.0 `SpiBus` and `SpiDevice` traits explicitly model bus vs. device separation.

### Rust: Mutex-Based Bus Sharing with `embedded-hal-bus`

The [`embedded-hal-bus`](https://crates.io/crates/embedded-hal-bus) crate provides ready-made sharing wrappers.

```toml
# Cargo.toml
[dependencies]
embedded-hal     = "1.0"
embedded-hal-bus = "0.2"
```

```rust
use embedded_hal::spi::SpiDevice;
use embedded_hal_bus::spi::{MutexDevice, CriticalSectionDevice};
use std::sync::Mutex;

// Example: sharing a bus between two devices using std Mutex
// (Use CriticalSectionDevice for no_std / bare-metal)

fn share_spi_example<BUS>(spi_bus: BUS)
where
    BUS: embedded_hal::spi::SpiBus,
{
    let bus = Mutex::new(spi_bus);

    // Each device gets its own CS pin and a reference to the shared bus
    let mut flash_cs  = DummyPin::new();  // Implement OutputPin
    let mut sensor_cs = DummyPin::new();

    let mut flash_dev  = MutexDevice::new(&bus, flash_cs,  embassy_time::Delay);
    let mut sensor_dev = MutexDevice::new(&bus, sensor_cs, embassy_time::Delay);

    // Both devices can now be used independently; the mutex is acquired
    // automatically on each transaction and released when done.
    let mut buf = [0u8; 4];
    flash_dev.transfer_in_place(&mut buf).unwrap();
    // ... sensor_dev.write(&[0x80]).unwrap();
}
```

### Rust: Critical-Section Device (no_std bare-metal)

```rust
#![no_std]
#![no_main]

use cortex_m::interrupt;
use embedded_hal_bus::spi::CriticalSectionDevice;
use embedded_hal::spi::SpiDevice;

struct FlashDriver<SPI: SpiDevice> {
    spi: SPI,
}

impl<SPI: SpiDevice> FlashDriver<SPI> {
    fn read_id(&mut self) -> Result<[u8; 3], SPI::Error> {
        let mut buf = [0x9F, 0x00, 0x00, 0x00];
        self.spi.transfer_in_place(&mut buf)?;
        Ok([buf[1], buf[2], buf[3]])
    }
}

struct SensorDriver<SPI: SpiDevice> {
    spi: SPI,
}

impl<SPI: SpiDevice> SensorDriver<SPI> {
    fn read_temperature(&mut self) -> Result<i16, SPI::Error> {
        let tx = [0x80 | 0x05, 0x00, 0x00];
        let mut rx = [0u8; 3];
        self.spi.transfer(&mut rx, &tx)?;
        Ok(i16::from_be_bytes([rx[1], rx[2]]))
    }
}

// At init time: wrap the bus in a critical-section-protected cell
// and hand out CriticalSectionDevice to each driver
```

### Rust: Async Bus Manager with Embassy

[Embassy](https://embassy.dev) uses `async`/`await` and its own mutex for SPI sharing:

```rust
use embassy_embedded_hal::shared_bus::asynch::spi::SpiDevice;
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_stm32::spi::Spi;

// Declare a static shared bus (zero heap allocation)
static SPI_BUS: Mutex<ThreadModeRawMutex, Spi<'static, SPI1, DMA2_CH3, DMA2_CH2>>
    = Mutex::new(unsafe { core::mem::zeroed() });

#[embassy_executor::task]
async fn task_flash() {
    let cs  = Output::new(p.PA4, Level::High, Speed::High);
    let mut dev = SpiDevice::new(&SPI_BUS, cs);

    let mut buf = [0x9F, 0, 0, 0];
    dev.transfer_in_place(&mut buf).await.unwrap();
    // flash JEDEC ID is in buf[1..4]
}

#[embassy_executor::task]
async fn task_sensor() {
    let cs  = Output::new(p.PB12, Level::High, Speed::High);
    let mut dev = SpiDevice::new(&SPI_BUS, cs);

    let cmd = [0x80u8 | 0x28, 0x00];
    let mut result = [0u8; 2];
    dev.transfer(&mut result, &cmd).await.unwrap();
    // result contains sensor data
}

// Embassy automatically serializes tasks through the async mutex:
// task_flash and task_sensor can both run concurrently and
// the SPI bus is granted to one at a time without blocking the executor.
```

### Rust: Priority Queue Bus Manager (no_std with heapless)

```rust
use heapless::BinaryHeap;
use heapless::binary_heap::Min;
use core::sync::atomic::{AtomicBool, Ordering};

const QUEUE_CAP: usize = 8;

#[derive(Eq, PartialEq)]
struct SpiRequest {
    priority: u8,             // 0 = highest priority
    device_id: u8,
    payload: [u8; 32],
    len: usize,
}

impl Ord for SpiRequest {
    fn cmp(&self, other: &Self) -> core::cmp::Ordering {
        // Min-heap: lower priority value = served first
        self.priority.cmp(&other.priority)
    }
}
impl PartialOrd for SpiRequest {
    fn partial_cmp(&self, other: &Self) -> Option<core::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

static BUS_BUSY: AtomicBool = AtomicBool::new(false);

struct SpiBusManager {
    queue: BinaryHeap<SpiRequest, Min, QUEUE_CAP>,
}

impl SpiBusManager {
    pub fn submit(&mut self, req: SpiRequest) -> Result<(), SpiRequest> {
        self.queue.push(req).map_err(|e| e)
    }

    /// Call from the bus manager loop or SPI TX-complete interrupt
    pub fn service_next(&mut self) {
        if BUS_BUSY.load(Ordering::Acquire) {
            return; // previous transfer still in progress
        }
        if let Some(req) = self.queue.pop() {
            BUS_BUSY.store(true, Ordering::Release);
            // Dispatch req to hardware SPI + DMA
            spi_start_dma(req.device_id, &req.payload[..req.len]);
        }
    }
}

/// Called from SPI DMA-complete ISR
fn spi_tx_complete_isr() {
    BUS_BUSY.store(false, Ordering::Release);
    // Wake bus manager or call service_next() directly
}

fn spi_start_dma(_device_id: u8, _data: &[u8]) {
    // Platform-specific DMA setup
}
```

---

## Comparison Table

| Strategy | Complexity | Latency | Throughput | ISR-Safe | RTOS Required |
|---|---|---|---|---|---|
| Hardware CS Multiplexing | Low | Low | Medium | Yes | No |
| Software Mutex (Blocking) | Low | Medium | Medium | No (use deferred) | Yes |
| Token Passing / Round-Robin | Medium | Bounded | Medium | Partial | Yes |
| Priority-Based Arbitration | High | Low (HP tasks) | Medium | No (deferred) | Recommended |
| DMA Double Buffering | High | Very Low | High | Yes (DMA CB) | No |
| Message Queue / Bus Manager | Medium | Low | Medium-High | Yes (enqueue from ISR) | Yes |
| MUX IC Bus Switching | Low-Medium | Low | Low-Medium | Partial | No |

---

## Summary

SPI bus sharing is a fundamental challenge in any embedded system with multiple peripherals on a shared bus. The correct strategy depends on the system's constraints:

**For simple bare-metal systems**, hardware chip select lines with careful software sequencing and an `spi_bus_busy` flag are usually sufficient. Pair this with a software mutex when moving to an RTOS.

**For RTOS-based systems with moderate complexity**, the **message queue / bus manager pattern** (Strategy 6) offers the best balance of correctness, ISR safety, and maintainability. It is the most commonly used pattern in production embedded firmware.

**For high-throughput use cases** (displays, audio, high-speed logging), **DMA double buffering** (Strategy 5) keeps the CPU free and achieves near-continuous bus utilization.

**For systems with strict real-time latency requirements**, **priority-based arbitration** (Strategy 4) ensures safety-critical tasks get bus access with bounded delay.

**For hardware-constrained systems** with many devices and few CS pins, a **MUX IC** (Strategy 7) extends the bus at the cost of channel-switching overhead.

**In Rust**, the `embedded-hal-bus` crate provides production-quality implementations of the mutex and critical-section strategies, while Embassy's async mutex offers an efficient solution for async/await-based firmware. Both enforce correct CS timing at the type level, making bus sharing bugs harder to introduce.

The key principles that apply across all strategies are:

1. **Atomicity** — a transaction (CS assert → transfer → CS deassert) must never be interrupted.
2. **Ownership** — at most one entity drives the bus at a time.
3. **Configuration hygiene** — reconfigure SPI mode and clock rate before each device transaction.
4. **ISR awareness** — blocking primitives must not be used inside ISRs; use deferred processing or lock-free queues instead.
5. **Bounded latency** — for real-time systems, ensure the worst-case wait for bus access is provably bounded.

By combining these principles with the appropriate strategy, complex multi-device SPI systems can be made both correct and efficient.

---

*Document: 99_SPI_Bus_Sharing_Strategies.md | SPI Embedded Systems Series*