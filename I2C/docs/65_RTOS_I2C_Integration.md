# 65. RTOS I2C Integration

**Structure overview:**
- **Why RTOS matters** — race conditions, priority inversion, blocking schedulers, ISR/task interaction
- **Core primitives** — mutex, semaphore, task notification, message queue
- **FreeRTOS** — three progressively advanced patterns: mutex-protected HAL wrapper, queue-based manager task, and DMA + ISR-driven transfers with semaphore signalling
- **Zephyr** — synchronous API, async callback pattern, and devicetree configuration
- **Rust/Embassy** — async/await shared bus with `Mutex<ThreadModeRawMutex, I2c<...>>` and timeout handling
- **Rust/RTIC** — compile-time resource ceiling analysis, priority-preemptive tasks, `wfi` idle
- **Bus sharing strategies** — global mutex, per-device handles, priority queues
- **Error handling** — NACK, timeout, bus recovery (9-clock pulse sequence), retry patterns
- **Deadlock prevention** — priority inversion explained, RAII mutex guard in C++
- **Performance** — DMA vs polling, burst transfers, clock speed table
- **Testing** — `embedded-hal-mock` unit tests in Rust, Tracealyzer hooks for FreeRTOS


## Integrating I2C Drivers with FreeRTOS, Zephyr, and Other Real-Time Operating Systems

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Why RTOS Matters for I2C](#2-why-rtos-matters-for-i2c)
3. [Core RTOS Concepts Relevant to I2C](#3-core-rtos-concepts-relevant-to-i2c)
4. [FreeRTOS I2C Integration](#4-freertos-i2c-integration)
   - 4.1 [Mutex-Protected I2C Bus](#41-mutex-protected-i2c-bus)
   - 4.2 [Task-Based I2C Driver](#42-task-based-i2c-driver)
   - 4.3 [DMA + Interrupt-Driven I2C with Semaphores](#43-dma--interrupt-driven-i2c-with-semaphores)
5. [Zephyr RTOS I2C Integration](#5-zephyr-rtos-i2c-integration)
   - 5.1 [Zephyr I2C API Overview](#51-zephyr-i2c-api-overview)
   - 5.2 [Synchronous I2C in Zephyr](#52-synchronous-i2c-in-zephyr)
   - 5.3 [Asynchronous I2C with Callbacks](#53-asynchronous-i2c-with-callbacks)
   - 5.4 [Zephyr Device Tree Configuration](#54-zephyr-device-tree-configuration)
6. [Rust Embedded RTOS I2C Integration](#6-rust-embedded-rtos-i2c-integration)
   - 6.1 [Embassy (Async Embedded Rust)](#61-embassy-async-embedded-rust)
   - 6.2 [RTIC (Real-Time Interrupt-driven Concurrency)](#62-rtic-real-time-interrupt-driven-concurrency)
7. [Bus Sharing and Arbitration Strategies](#7-bus-sharing-and-arbitration-strategies)
8. [Error Handling in RTOS Contexts](#8-error-handling-in-rtos-contexts)
9. [Deadlock Prevention and Priority Inversion](#9-deadlock-prevention-and-priority-inversion)
10. [Performance Considerations](#10-performance-considerations)
11. [Testing and Debugging RTOS I2C Drivers](#11-testing-and-debugging-rtos-i2c-drivers)
12. [Summary](#12-summary)

---

## 1. Introduction

The Inter-Integrated Circuit (I2C) bus is one of the most widely used communication protocols in embedded systems, connecting microcontrollers to sensors, displays, EEPROMs, real-time clocks, and many other peripheral devices. When a bare-metal I2C driver operates in a single-threaded, polling environment, its design is relatively straightforward. However, as soon as an application grows to use a Real-Time Operating System (RTOS), the challenge fundamentally changes.

In an RTOS environment, multiple tasks may need to access the same I2C bus simultaneously. Interrupts must interact safely with task-level code. Blocking operations must not starve other tasks. And deterministic timing — the hallmark of a real-time system — must be preserved even when peripheral communications are involved.

This document covers the theory, design patterns, and concrete code examples needed to correctly and safely integrate I2C drivers with popular RTOSes including **FreeRTOS** and **Zephyr**, as well as the emerging **Rust embedded** ecosystem using Embassy and RTIC frameworks.

---

## 2. Why RTOS Matters for I2C

In a bare-metal environment, a typical I2C transaction might look like this:

```c
// Bare-metal blocking I2C read (polling)
i2c_start();
i2c_write_byte(device_addr << 1 | I2C_WRITE);
i2c_write_byte(register_addr);
i2c_repeated_start();
i2c_write_byte(device_addr << 1 | I2C_READ);
uint8_t data = i2c_read_byte_nack();
i2c_stop();
```

This works fine in isolation. In an RTOS, however, several problems emerge:

| Problem | Description |
|---|---|
| **Race conditions** | Two tasks attempt I2C transactions simultaneously, corrupting bus state |
| **Priority inversion** | A low-priority task holds the I2C mutex, blocking a high-priority task |
| **Blocking the scheduler** | Spin-wait polling wastes CPU time that other tasks could use |
| **ISR / task interaction** | Interrupt-driven drivers must signal task-level code safely |
| **Timeout management** | I2C errors (stuck SDA, missing ACK) must not deadlock the system |
| **Bus recovery** | After errors, the bus must be recoverable without rebooting |

Proper RTOS integration addresses all of these using mutexes, semaphores, message queues, and task notifications.

---

## 3. Core RTOS Concepts Relevant to I2C

Before examining specific implementations, it is important to understand the RTOS primitives used throughout this document.

### Mutex (Mutual Exclusion Lock)

A mutex ensures that only one task can access the I2C bus at a time. Unlike a binary semaphore, a proper mutex supports **priority inheritance**, preventing priority inversion.

```
Task A (high prio) ─────────────────────────── blocks ───── runs
Task B (low prio)  ──── acquires mutex ──────────────────── releases mutex
```

### Binary Semaphore / Counting Semaphore

Used to signal completion of an interrupt-driven operation. An ISR "gives" the semaphore when the DMA transfer completes; the waiting task "takes" it, unblocking.

```
ISR:  xSemaphoreGiveFromISR(sem, &xHigherPriorityTaskWoken);
Task: xSemaphoreTake(sem, portMAX_DELAY);
```

### Task Notification

A lightweight alternative to semaphores for single-consumer signalling. Each task has a 32-bit notification value built into the TCB, avoiding dynamic allocation.

### Message Queue

Used to pipeline I2C requests — producers enqueue descriptors; a dedicated I2C manager task dequeues and processes them in order.

---

## 4. FreeRTOS I2C Integration

FreeRTOS is the most widely deployed RTOS in the embedded world, used extensively with STM32, ESP32, NXP, and many other platforms.

### 4.1 Mutex-Protected I2C Bus

The simplest and most robust pattern is wrapping every I2C transaction with a mutex. This prevents concurrent access while preserving the HAL's existing blocking implementation.

**`i2c_rtos.h`**

```c
#ifndef I2C_RTOS_H
#define I2C_RTOS_H

#include "FreeRTOS.h"
#include "semphr.h"
#include <stdint.h>
#include <stdbool.h>

/* Platform-specific I2C handle - replace with your HAL type */
typedef void* I2C_HandleTypeDef_t;

typedef struct {
    I2C_HandleTypeDef_t  *hal_handle;   /* Underlying HAL I2C handle        */
    SemaphoreHandle_t     mutex;         /* Mutex protecting bus access       */
    StaticSemaphore_t     mutex_buf;     /* Static allocation for mutex       */
    uint32_t              timeout_ms;    /* Default timeout for transactions  */
} I2C_RTOS_Handle_t;

bool     i2c_rtos_init(I2C_RTOS_Handle_t *h, I2C_HandleTypeDef_t *hal,
                        uint32_t timeout_ms);
bool     i2c_rtos_write(I2C_RTOS_Handle_t *h, uint8_t addr,
                         const uint8_t *data, size_t len);
bool     i2c_rtos_read(I2C_RTOS_Handle_t *h, uint8_t addr,
                        uint8_t *data, size_t len);
bool     i2c_rtos_write_reg(I2C_RTOS_Handle_t *h, uint8_t addr,
                              uint8_t reg, const uint8_t *data, size_t len);
bool     i2c_rtos_read_reg(I2C_RTOS_Handle_t *h, uint8_t addr,
                             uint8_t reg, uint8_t *data, size_t len);
void     i2c_rtos_deinit(I2C_RTOS_Handle_t *h);

#endif /* I2C_RTOS_H */
```

**`i2c_rtos.c`**

```c
#include "i2c_rtos.h"
#include "stm32xx_hal.h"   /* Replace with your platform HAL */
#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 * Initialise the RTOS-aware I2C handle.
 * Uses static semaphore allocation to avoid heap fragmentation.
 * ────────────────────────────────────────────────────────────────── */
bool i2c_rtos_init(I2C_RTOS_Handle_t *h,
                   I2C_HandleTypeDef_t *hal,
                   uint32_t timeout_ms)
{
    if (!h || !hal) return false;

    h->hal_handle = hal;
    h->timeout_ms = timeout_ms;

    /* Create mutex with priority inheritance to prevent priority inversion */
    h->mutex = xSemaphoreCreateMutexStatic(&h->mutex_buf);
    if (!h->mutex) return false;

    return true;
}

/* ──────────────────────────────────────────────────────────────────
 * Internal helper: acquire bus mutex with timeout.
 * ────────────────────────────────────────────────────────────────── */
static bool _acquire(I2C_RTOS_Handle_t *h)
{
    return xSemaphoreTake(h->mutex,
                          pdMS_TO_TICKS(h->timeout_ms)) == pdTRUE;
}

static void _release(I2C_RTOS_Handle_t *h)
{
    xSemaphoreGive(h->mutex);
}

/* ──────────────────────────────────────────────────────────────────
 * Write raw bytes to a device.
 * ────────────────────────────────────────────────────────────────── */
bool i2c_rtos_write(I2C_RTOS_Handle_t *h,
                    uint8_t addr,
                    const uint8_t *data,
                    size_t len)
{
    if (!_acquire(h)) return false;          /* Timeout waiting for bus  */

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        (I2C_HandleTypeDef *)h->hal_handle,
        (uint16_t)(addr << 1),              /* HAL expects 8-bit address */
        (uint8_t *)data,
        (uint16_t)len,
        h->timeout_ms
    );

    _release(h);
    return status == HAL_OK;
}

/* ──────────────────────────────────────────────────────────────────
 * Read raw bytes from a device.
 * ────────────────────────────────────────────────────────────────── */
bool i2c_rtos_read(I2C_RTOS_Handle_t *h,
                   uint8_t addr,
                   uint8_t *data,
                   size_t len)
{
    if (!_acquire(h)) return false;

    HAL_StatusTypeDef status = HAL_I2C_Master_Receive(
        (I2C_HandleTypeDef *)h->hal_handle,
        (uint16_t)(addr << 1),
        data,
        (uint16_t)len,
        h->timeout_ms
    );

    _release(h);
    return status == HAL_OK;
}

/* ──────────────────────────────────────────────────────────────────
 * Write to a register address (write address, then data).
 * Uses a single transaction — no repeated start needed.
 * ────────────────────────────────────────────────────────────────── */
bool i2c_rtos_write_reg(I2C_RTOS_Handle_t *h,
                         uint8_t addr,
                         uint8_t reg,
                         const uint8_t *data,
                         size_t len)
{
    if (!_acquire(h)) return false;

    /* Build contiguous buffer: [reg_addr | data...] */
    uint8_t buf[1 + len];      /* VLA — use static buffer if stack is tight */
    buf[0] = reg;
    memcpy(&buf[1], data, len);

    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        (I2C_HandleTypeDef *)h->hal_handle,
        (uint16_t)(addr << 1),
        buf,
        (uint16_t)(1 + len),
        h->timeout_ms
    );

    _release(h);
    return status == HAL_OK;
}

/* ──────────────────────────────────────────────────────────────────
 * Read from a register: write register address, repeated START, read data.
 * ────────────────────────────────────────────────────────────────── */
bool i2c_rtos_read_reg(I2C_RTOS_Handle_t *h,
                        uint8_t addr,
                        uint8_t reg,
                        uint8_t *data,
                        size_t len)
{
    if (!_acquire(h)) return false;

    HAL_StatusTypeDef status;

    /* Phase 1: send register address */
    status = HAL_I2C_Master_Transmit(
        (I2C_HandleTypeDef *)h->hal_handle,
        (uint16_t)(addr << 1),
        &reg, 1,
        h->timeout_ms
    );

    if (status == HAL_OK) {
        /* Phase 2: read data */
        status = HAL_I2C_Master_Receive(
            (I2C_HandleTypeDef *)h->hal_handle,
            (uint16_t)(addr << 1),
            data,
            (uint16_t)len,
            h->timeout_ms
        );
    }

    _release(h);
    return status == HAL_OK;
}

void i2c_rtos_deinit(I2C_RTOS_Handle_t *h)
{
    if (h && h->mutex) {
        vSemaphoreDelete(h->mutex);
        h->mutex = NULL;
    }
}
```

**Usage in application tasks:**

```c
/* Sensor task — reads temperature from LM75 every 100 ms */
static I2C_RTOS_Handle_t g_i2c;

void vSensorTask(void *pvParameters)
{
    const uint8_t LM75_ADDR = 0x48;   /* A0-A2 = GND */
    uint8_t raw[2];
    int16_t raw16;
    float   temp_c;

    for (;;) {
        if (i2c_rtos_read_reg(&g_i2c, LM75_ADDR, 0x00, raw, 2)) {
            raw16   = (int16_t)((raw[0] << 8) | raw[1]) >> 5;
            temp_c  = raw16 * 0.125f;
            /* Publish to queue, event group, etc. */
        } else {
            /* Handle error — bus busy, device missing, etc. */
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

### 4.2 Task-Based I2C Driver

For more complex systems, a dedicated **I2C manager task** serialises all bus operations through a message queue. Callers enqueue a request and optionally block on a completion semaphore.

```c
/* ──────────────────────────────────────────────────────────────────
 * i2c_manager.h  —  Queue-based I2C request/response pattern
 * ────────────────────────────────────────────────────────────────── */
#ifndef I2C_MANAGER_H
#define I2C_MANAGER_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include <stdint.h>
#include <stdbool.h>

#define I2C_MAX_DATA_LEN    64u

typedef enum {
    I2C_OP_WRITE,
    I2C_OP_READ,
    I2C_OP_WRITE_REG,
    I2C_OP_READ_REG,
} I2C_OpType_t;

typedef struct {
    I2C_OpType_t        op;
    uint8_t             addr;           /* 7-bit device address           */
    uint8_t             reg;            /* Register (for WRITE_REG/READ_REG) */
    uint8_t             data[I2C_MAX_DATA_LEN];
    size_t              len;
    SemaphoreHandle_t   done_sem;       /* Caller waits on this           */
    bool               *result;         /* Written by manager task        */
} I2C_Request_t;

void    i2c_manager_init(void);
bool    i2c_manager_submit(I2C_Request_t *req, uint32_t timeout_ms);

#endif /* I2C_MANAGER_H */
```

```c
/* ──────────────────────────────────────────────────────────────────
 * i2c_manager.c  —  Manager task processes requests sequentially
 * ────────────────────────────────────────────────────────────────── */
#include "i2c_manager.h"
#include "i2c_rtos.h"
#include <string.h>

#define I2C_QUEUE_DEPTH     8u
#define MANAGER_STACK_DEPTH 512u
#define MANAGER_PRIORITY    (configMAX_PRIORITIES - 2)

static QueueHandle_t       s_queue;
static StaticQueue_t       s_queue_buf;
static uint8_t             s_queue_storage[I2C_QUEUE_DEPTH * sizeof(I2C_Request_t *)];

static StackType_t         s_task_stack[MANAGER_STACK_DEPTH];
static StaticTask_t        s_task_tcb;

extern I2C_RTOS_Handle_t   g_i2c;          /* Application-level handle   */

/* ── Manager task ──────────────────────────────────────────────── */
static void vI2CManagerTask(void *pvParams)
{
    I2C_Request_t *req;

    for (;;) {
        /* Block indefinitely until a request arrives */
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        bool ok = false;

        switch (req->op) {
        case I2C_OP_WRITE:
            ok = i2c_rtos_write(&g_i2c, req->addr, req->data, req->len);
            break;
        case I2C_OP_READ:
            ok = i2c_rtos_read(&g_i2c, req->addr, req->data, req->len);
            break;
        case I2C_OP_WRITE_REG:
            ok = i2c_rtos_write_reg(&g_i2c, req->addr, req->reg,
                                    req->data, req->len);
            break;
        case I2C_OP_READ_REG:
            ok = i2c_rtos_read_reg(&g_i2c, req->addr, req->reg,
                                   req->data, req->len);
            break;
        }

        if (req->result)    *req->result = ok;
        if (req->done_sem)  xSemaphoreGive(req->done_sem);
    }
}

void i2c_manager_init(void)
{
    s_queue = xQueueCreateStatic(I2C_QUEUE_DEPTH,
                                  sizeof(I2C_Request_t *),
                                  s_queue_storage,
                                  &s_queue_buf);

    xTaskCreateStatic(vI2CManagerTask,
                      "I2C_MGR",
                      MANAGER_STACK_DEPTH,
                      NULL,
                      MANAGER_PRIORITY,
                      s_task_stack,
                      &s_task_tcb);
}

bool i2c_manager_submit(I2C_Request_t *req, uint32_t timeout_ms)
{
    return xQueueSend(s_queue, &req,
                      pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
```

**Synchronous (blocking) caller pattern:**

```c
void vDisplayTask(void *pvParams)
{
    static StaticSemaphore_t sem_buf;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&sem_buf);

    for (;;) {
        bool result = false;
        uint8_t cmd = 0xAE;     /* SSD1306 display off */

        I2C_Request_t req = {
            .op       = I2C_OP_WRITE,
            .addr     = 0x3C,
            .len      = 1,
            .done_sem = done,
            .result   = &result,
        };
        req.data[0] = cmd;

        i2c_manager_submit(&req, 100);
        xSemaphoreTake(done, pdMS_TO_TICKS(200));  /* Await completion */

        if (!result) { /* handle error */ }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

### 4.3 DMA + Interrupt-Driven I2C with Semaphores

For high-throughput or power-sensitive applications, the CPU should not block during I2C transfers. Instead, DMA handles the data movement; the I2C peripheral fires an interrupt on completion; the ISR signals a task-level semaphore.

```c
/* ──────────────────────────────────────────────────────────────────
 * DMA-driven I2C with FreeRTOS binary semaphore signalling
 *
 * Platform: STM32 HAL (adapt as needed for your MCU)
 * ────────────────────────────────────────────────────────────────── */

#include "stm32xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t s_dma_done;
static StaticSemaphore_t s_dma_done_buf;

extern I2C_HandleTypeDef hi2c1;   /* HAL peripheral handle */

/* ── Call once at startup ──────────────────────────────────────── */
void i2c_dma_init(void)
{
    s_dma_done = xSemaphoreCreateBinaryStatic(&s_dma_done_buf);
}

/* ── STM32 HAL callback — called from ISR context ─────────────── */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != hi2c1.Instance) return;

    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_dma_done, &woken);
    portYIELD_FROM_ISR(woken);   /* Yield if higher-priority task unblocked */
}

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != hi2c1.Instance) return;

    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_dma_done, &woken);
    portYIELD_FROM_ISR(woken);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance != hi2c1.Instance) return;

    /* Give semaphore even on error so the task unblocks and checks status */
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_dma_done, &woken);
    portYIELD_FROM_ISR(woken);
}

/* ── Task-level DMA write ──────────────────────────────────────── */
bool i2c_dma_write(uint8_t addr, uint8_t *data, size_t len,
                   uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;

    /* Kick off non-blocking DMA transfer */
    status = HAL_I2C_Master_Transmit_DMA(&hi2c1,
                                          (uint16_t)(addr << 1),
                                          data, (uint16_t)len);
    if (status != HAL_OK) return false;

    /* Block this task until ISR signals completion */
    if (xSemaphoreTake(s_dma_done,
                       pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        /* Timeout — abort transfer and recover bus */
        HAL_I2C_Master_Abort_IT(&hi2c1, (uint16_t)(addr << 1));
        return false;
    }

    return HAL_I2C_GetError(&hi2c1) == HAL_I2C_ERROR_NONE;
}
```

**Key point:** `xSemaphoreGiveFromISR` and `portYIELD_FROM_ISR` are the **only** FreeRTOS APIs safe to call from an ISR. Never call `xSemaphoreTake` or any blocking API from interrupt context.

---

## 5. Zephyr RTOS I2C Integration

Zephyr provides a mature, hardware-abstracted I2C subsystem that is RTOS-aware by design. It uses the **device driver model** with compile-time device tree configuration.

### 5.1 Zephyr I2C API Overview

Zephyr's I2C API (`<zephyr/drivers/i2c.h>`) provides:

| Function | Description |
|---|---|
| `i2c_write(dev, buf, len, addr)` | Write bytes to device |
| `i2c_read(dev, buf, len, addr)` | Read bytes from device |
| `i2c_write_read(dev, addr, wbuf, wlen, rbuf, rlen)` | Write then read (single transaction) |
| `i2c_reg_write_byte(dev, addr, reg, val)` | Write single register byte |
| `i2c_reg_read_byte(dev, addr, reg, val)` | Read single register byte |
| `i2c_transfer(dev, msgs, num_msgs, addr)` | Low-level message array |

All Zephyr I2C calls are thread-safe by default — the driver handles locking internally.

### 5.2 Synchronous I2C in Zephyr

```c
/*
 * Zephyr synchronous I2C example: read temperature from BME280
 *
 * Build system: CMakeLists.txt + prj.conf + devicetree overlay
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bme280_sample, LOG_LEVEL_INF);

/* Device tree node — &i2c0 { bme280@76 { ... }; }; */
#define BME280_NODE   DT_NODELABEL(bme280)
#define BME280_ADDR   DT_REG_ADDR(BME280_NODE)

static const struct device *i2c_dev =
    DEVICE_DT_GET(DT_BUS(BME280_NODE));

/* ── Register definitions ──────────────────────────────────────── */
#define BME280_REG_CHIP_ID      0xD0
#define BME280_REG_RESET        0xE0
#define BME280_REG_CTRL_MEAS    0xF4
#define BME280_REG_PRESS_MSB    0xF7
#define BME280_CHIP_ID          0x60

static int bme280_read_chip_id(uint8_t *chip_id)
{
    return i2c_reg_read_byte(i2c_dev, BME280_ADDR,
                             BME280_REG_CHIP_ID, chip_id);
}

static int bme280_soft_reset(void)
{
    return i2c_reg_write_byte(i2c_dev, BME280_ADDR,
                              BME280_REG_RESET, 0xB6);
}

static int bme280_read_raw(uint8_t raw[6])
{
    /*
     * Use i2c_write_read for an atomic write-then-read:
     * Write register address, get repeated START, then read 6 bytes.
     */
    uint8_t reg = BME280_REG_PRESS_MSB;
    return i2c_write_read(i2c_dev, BME280_ADDR,
                          &reg, sizeof(reg),
                          raw, 6);
}

/* ── Thread entry ──────────────────────────────────────────────── */
void sensor_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device not ready");
        return;
    }

    uint8_t chip_id;
    if (bme280_read_chip_id(&chip_id) != 0 || chip_id != BME280_CHIP_ID) {
        LOG_ERR("BME280 not found (id=0x%02X)", chip_id);
        return;
    }
    LOG_INF("BME280 detected, chip_id=0x%02X", chip_id);

    bme280_soft_reset();
    k_msleep(10);   /* Wait for reset to complete */

    /* Force mode: measure once, return to sleep */
    i2c_reg_write_byte(i2c_dev, BME280_ADDR,
                       BME280_REG_CTRL_MEAS, 0x25);

    for (;;) {
        uint8_t raw[6];
        if (bme280_read_raw(raw) == 0) {
            int32_t adc_P = ((int32_t)raw[0] << 12) |
                            ((int32_t)raw[1] << 4)  |
                            (raw[2] >> 4);
            /* Further compensation math omitted for brevity */
            LOG_INF("ADC pressure raw: %d", adc_P);
        } else {
            LOG_WRN("I2C read failed");
        }

        k_sleep(K_MSEC(1000));
    }
}

K_THREAD_DEFINE(sensor_tid, 1024,
                sensor_thread, NULL, NULL, NULL,
                5, 0, 0);
```

### 5.3 Asynchronous I2C with Callbacks

Zephyr also supports asynchronous I2C (where supported by hardware):

```c
#include <zephyr/drivers/i2c.h>

static struct i2c_msg msgs[2];
static uint8_t        reg_buf[1] = { 0x00 };
static uint8_t        data_buf[2];

/* Callback fires in ISR context — keep it short */
static void i2c_async_callback(const struct device *dev,
                                int result,
                                void *user_data)
{
    struct k_sem *done = (struct k_sem *)user_data;

    if (result != 0) {
        /* Log from ISR-safe log backend */
    }

    k_sem_give(done);   /* Signal waiting thread */
}

static K_SEM_DEFINE(i2c_done, 0, 1);

int async_read_example(const struct device *i2c_dev, uint8_t addr)
{
    msgs[0].buf   = reg_buf;
    msgs[0].len   = sizeof(reg_buf);
    msgs[0].flags = I2C_MSG_WRITE;

    msgs[1].buf   = data_buf;
    msgs[1].len   = sizeof(data_buf);
    msgs[1].flags = I2C_MSG_READ | I2C_MSG_STOP;

    int rc = i2c_transfer_cb(i2c_dev, msgs, 2, addr,
                              i2c_async_callback, &i2c_done);
    if (rc != 0) return rc;

    /* Block until callback fires (with timeout) */
    return k_sem_take(&i2c_done, K_MSEC(100));
}
```

### 5.4 Zephyr Device Tree Configuration

Zephyr's I2C configuration lives in the Device Tree, making it portable across hardware:

```dts
/* boards/arm/myboard/myboard.dts  (or overlay file) */

&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_FAST>;  /* 400 kHz */

    bme280: bme280@76 {
        compatible = "bosch,bme280";
        reg = <0x76>;
        label = "BME280";
    };

    oled: ssd1306@3c {
        compatible = "solomon,ssd1306fb";
        reg = <0x3c>;
        label = "SSD1306";
        width   = <128>;
        height  = <64>;
        segment-offset = <0>;
        page-offset    = <0>;
        display-offset = <0>;
    };
};
```

**prj.conf** for enabling I2C:

```kconfig
CONFIG_I2C=y
CONFIG_I2C_ASYNC=y          # Enable async support (if needed)
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
```

---

## 6. Rust Embedded RTOS I2C Integration

Rust's embedded ecosystem provides memory-safe, zero-cost abstraction for I2C peripherals with strong compile-time guarantees. Two major frameworks handle real-time concurrency:

- **Embassy**: async/await–based cooperative multitasking
- **RTIC**: preemptive, interrupt-driven concurrency framework

Both implement the `embedded-hal` I2C traits, enabling portable driver code.

### 6.1 Embassy (Async Embedded Rust)

Embassy provides an async executor optimised for embedded targets. I2C operations are `async fn`, freeing the executor to run other tasks during bus wait states.

```toml
# Cargo.toml
[dependencies]
embassy-executor  = { version = "0.5", features = ["arch-cortex-m", "executor-thread"] }
embassy-stm32    = { version = "0.1", features = ["stm32f411re", "time-driver-any", "unstable-pac"] }
embassy-time     = { version = "0.3" }
embassy-sync     = { version = "0.5" }
embedded-hal-async = { version = "1.0" }
defmt            = "0.3"
defmt-rtt        = "0.4"
panic-probe      = { version = "0.3", features = ["print-defmt"] }
```

```rust
//! Embassy async I2C example — STM32F411
//!
//! Demonstrates:
//!   - Async I2C reads from two tasks sharing one bus via Mutex
//!   - Task-level timeout with embassy_time::with_timeout
//!   - Typed error handling

#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

use defmt::*;
use embassy_executor::Spawner;
use embassy_stm32::{
    bind_interrupts,
    i2c::{self, I2c},
    peripherals,
    time::Hertz,
};
use embassy_sync::{blocking_mutex::raw::ThreadModeRawMutex, mutex::Mutex};
use embassy_time::{Duration, Timer, with_timeout};
use embedded_hal_async::i2c::I2c as AsyncI2c;
use static_cell::StaticCell;
use {defmt_rtt as _, panic_probe as _};

bind_interrupts!(struct Irqs {
    I2C1_EV => i2c::EventInterruptHandler<peripherals::I2C1>;
    I2C1_ER => i2c::ErrorInterruptHandler<peripherals::I2C1>;
});

// ── Shared I2C bus using Embassy Mutex ─────────────────────────────
type SharedI2C = Mutex<ThreadModeRawMutex, I2c<'static, peripherals::I2C1>>;
static I2C_BUS: StaticCell<SharedI2C> = StaticCell::new();

// ── Sensor addresses ───────────────────────────────────────────────
const LM75_ADDR:  u8 = 0x48;
const SSD1306_ADDR: u8 = 0x3C;

// ── LM75 temperature sensor task ───────────────────────────────────
#[embassy_executor::task]
async fn temperature_task(bus: &'static SharedI2C) {
    loop {
        let result = with_timeout(Duration::from_millis(100), async {
            let mut bus = bus.lock().await;

            // Write register pointer (0x00 = temperature register)
            bus.write(LM75_ADDR, &[0x00]).await?;

            // Read 2 bytes
            let mut raw = [0u8; 2];
            bus.read(LM75_ADDR, &mut raw).await?;

            Ok::<_, i2c::Error>(raw)
        }).await;

        match result {
            Ok(Ok(raw)) => {
                let raw16  = i16::from_be_bytes(raw) >> 5;
                let temp   = raw16 as f32 * 0.125;
                info!("Temperature: {:.2} °C", temp);
            }
            Ok(Err(e)) => warn!("I2C error: {:?}", e),
            Err(_)     => warn!("I2C timeout"),
        }

        Timer::after(Duration::from_millis(500)).await;
    }
}

// ── OLED display heartbeat task ────────────────────────────────────
#[embassy_executor::task]
async fn display_task(bus: &'static SharedI2C) {
    // SSD1306 init sequence (abbreviated)
    let init_cmds: &[u8] = &[
        0x00,   // Command byte
        0xAE,   // Display off
        0xD5, 0x80,  // Set display clock
        0xA8, 0x3F,  // Multiplex ratio 64
        0xD3, 0x00,  // Display offset 0
        0x40,        // Start line 0
        0x8D, 0x14,  // Enable charge pump
        0xAF,        // Display on
    ];

    {
        let mut bus = bus.lock().await;
        if let Err(e) = bus.write(SSD1306_ADDR, init_cmds).await {
            warn!("OLED init failed: {:?}", e);
            return;
        }
    }

    info!("OLED initialised");

    loop {
        // Heartbeat: toggle a single pixel via data write
        {
            let mut bus = bus.lock().await;
            // Set column/page address, then write pixel data
            let _ = bus.write(SSD1306_ADDR, &[0x00, 0x21, 63, 63]).await;
            let _ = bus.write(SSD1306_ADDR, &[0x00, 0x22, 3, 3]).await;
            let _ = bus.write(SSD1306_ADDR, &[0x40, 0xFF]).await;
        }

        Timer::after(Duration::from_millis(1000)).await;

        {
            let mut bus = bus.lock().await;
            let _ = bus.write(SSD1306_ADDR, &[0x40, 0x00]).await;
        }

        Timer::after(Duration::from_millis(1000)).await;
    }
}

// ── Main entry point ───────────────────────────────────────────────
#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    let i2c = I2c::new(
        p.I2C1,
        p.PB8,   // SCL
        p.PB9,   // SDA
        Irqs,
        p.DMA1_CH6,
        p.DMA1_CH0,
        Hertz(400_000),
        Default::default(),
    );

    let i2c_bus = I2C_BUS.init(Mutex::new(i2c));

    spawner.spawn(temperature_task(i2c_bus)).unwrap();
    spawner.spawn(display_task(i2c_bus)).unwrap();
}
```

**Key Embassy patterns:**
- `Mutex<ThreadModeRawMutex, I2c<'static, _>>` — safe shared ownership of I2C bus across tasks
- `with_timeout(Duration, async { ... })` — prevents a stuck peripheral from deadlocking the executor
- Ownership of `I2c` is enforced at compile time — no data races possible

---

### 6.2 RTIC (Real-Time Interrupt-driven Concurrency)

RTIC uses Rust's ownership model to implement a hardware-concurrency framework with zero runtime overhead. Resources are statically declared and access is enforced at compile time.

```toml
# Cargo.toml
[dependencies]
rtic             = { version = "2.0", features = ["thumbv7-backend"] }
rtic-monotonics  = { version = "1.0", features = ["cortex-m-systick"] }
stm32f4xx-hal    = { version = "0.20", features = ["stm32f411", "rtic"] }
embedded-hal     = "1.0"
heapless         = "0.8"
```

```rust
//! RTIC I2C example — interrupt-driven I2C with shared resource
//!
//! Demonstrates:
//!   - RTIC resource sharing (I2C bus declared as shared resource)
//!   - Software tasks for periodic sensor reading
//!   - Compile-time priority ceiling enforcement

#![no_std]
#![no_main]

use rtic::app;
use rtic_monotonics::systick::prelude::*;
use stm32f4xx_hal::{
    i2c::{I2c, Mode},
    pac,
    prelude::*,
};
use heapless::Vec;

systick_monotonic!(Mono, 1000);

// ── Device address constants ────────────────────────────────────────
const MPU6050_ADDR: u8 = 0x68;
const MPU6050_WHO_AM_I: u8 = 0x75;
const MPU6050_ACCEL_XOUT_H: u8 = 0x3B;
const MPU6050_PWR_MGMT_1: u8 = 0x6B;

#[app(device = stm32f4xx_hal::pac, peripherals = true, dispatchers = [USART1])]
mod app {
    use super::*;
    use embedded_hal::i2c::I2c as I2cTrait;

    // ── Shared resources (protected by RTIC ceiling analysis) ────────
    #[shared]
    struct Shared {
        i2c: I2c<pac::I2C1>,
    }

    // ── Local resources (per-task, no lock needed) ───────────────────
    #[local]
    struct Local {
        led: stm32f4xx_hal::gpio::Pin<'C', 13, stm32f4xx_hal::gpio::Output>,
    }

    // ── Initialisation ───────────────────────────────────────────────
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        let rcc = ctx.device.RCC.constrain();
        let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

        Mono::start(ctx.core.SYST, 84_000_000);

        let gpiob = ctx.device.GPIOB.split();
        let gpioc = ctx.device.GPIOC.split();

        let scl = gpiob.pb8.into_alternate_open_drain();
        let sda = gpiob.pb9.into_alternate_open_drain();

        let i2c = I2c::new(
            ctx.device.I2C1,
            (scl, sda),
            Mode::Fast { frequency: 400_000.Hz() },
            &clocks,
        );

        let led = gpioc.pc13.into_push_pull_output();

        // Spawn periodic task
        sensor_read::spawn().unwrap();

        (Shared { i2c }, Local { led })
    }

    // ── Idle task ────────────────────────────────────────────────────
    #[idle]
    fn idle(_ctx: idle::Context) -> ! {
        loop {
            cortex_m::asm::wfi();  // Wait for interrupt — saves power
        }
    }

    // ── Periodic sensor task — runs every 100 ms ─────────────────────
    #[task(shared = [i2c], local = [led], priority = 1)]
    async fn sensor_read(mut ctx: sensor_read::Context) {
        // Wake MPU6050 (clear sleep bit in PWR_MGMT_1)
        ctx.shared.i2c.lock(|i2c| {
            let _ = i2c.write(MPU6050_ADDR,
                              &[MPU6050_PWR_MGMT_1, 0x00]);
        });

        loop {
            Mono::delay(100.millis()).await;

            // Read 6 bytes of accelerometer data
            let mut raw = [0u8; 6];

            let ok = ctx.shared.i2c.lock(|i2c| {
                // Write register pointer
                if i2c.write(MPU6050_ADDR, &[MPU6050_ACCEL_XOUT_H]).is_err() {
                    return false;
                }
                // Read 6 bytes
                i2c.read(MPU6050_ADDR, &mut raw).is_ok()
            });

            if ok {
                let ax = i16::from_be_bytes([raw[0], raw[1]]);
                let ay = i16::from_be_bytes([raw[2], raw[3]]);
                let az = i16::from_be_bytes([raw[4], raw[5]]);

                // Convert to g (16384 LSB/g at ±2g range)
                let _ax_g = ax as f32 / 16384.0;
                let _ay_g = ay as f32 / 16384.0;
                let _az_g = az as f32 / 16384.0;

                ctx.local.led.toggle();
            }
        }
    }

    // ── Hardware task — fired by I2C event interrupt ─────────────────
    #[task(binds = I2C1_EV, shared = [i2c], priority = 2)]
    fn i2c_event(mut ctx: i2c_event::Context) {
        ctx.shared.i2c.lock(|_i2c| {
            // Handle interrupt-driven I2C events if using async HAL
            // For synchronous HAL, this is not needed
        });
    }
}
```

**Key RTIC patterns:**
- `#[shared]` resources are locked with `ctx.shared.resource.lock(|r| { ... })` — RTIC automatically computes priority ceilings at compile time
- `#[task(priority = N)]` defines preemption levels — no runtime scheduler needed
- `cortex_m::asm::wfi()` in idle saves power between events

---

## 7. Bus Sharing and Arbitration Strategies

When multiple devices share one I2C bus, arbitration and transaction atomicity are critical concerns.

### Strategy 1: Global Bus Mutex (Recommended)

One mutex guards the entire bus. All consumers lock it before any byte is sent and release it after the STOP condition. This guarantees atomicity of multi-phase operations (write register → repeated START → read).

```
Task A: [LOCK] START → WRITE addr → WRITE reg → RS → READ data STOP [UNLOCK]
Task B: [waits...................................................] [LOCK] ...
```

### Strategy 2: Per-Device Handles with Shared Bus

```c
/* Wrapper: per-device handle sharing a common bus mutex */
typedef struct {
    I2C_RTOS_Handle_t  *bus;   /* Shared bus handle */
    uint8_t             addr;  /* This device's 7-bit address */
} I2C_Device_t;

bool device_read_reg(I2C_Device_t *dev, uint8_t reg,
                     uint8_t *out, size_t len)
{
    return i2c_rtos_read_reg(dev->bus, dev->addr, reg, out, len);
}
```

### Strategy 3: Bus Manager Task with Priority Queue

For systems with many devices and hard deadlines, a dedicated manager task can implement priority-ordered I2C scheduling:

```c
typedef struct {
    I2C_Request_t base;
    UBaseType_t   priority;    /* Higher = processed sooner */
} I2C_PriorityRequest_t;

/* Use a priority queue (heap) instead of FIFO queue */
/* FreeRTOS doesn't have built-in priority queues —   */
/* implement with a sorted linked list or use a       */
/* software timer to re-order incoming requests.      */
```

---

## 8. Error Handling in RTOS Contexts

Robust RTOS I2C drivers must handle:

| Error | Cause | Recovery |
|---|---|---|
| **NACK** | Device missing, bus address wrong, busy | Abort, log, retry after delay |
| **Arbitration loss** | Multi-master conflict | Retry immediately (I2C standard) |
| **Timeout** | Device held SDA/SCL low | Bus recovery: 9 SCL clocks + STOP |
| **Bus busy** | Spurious START with no STOP | Software reset of I2C peripheral |
| **Overrun** | DMA/FIFO not serviced in time | Check interrupt priority and CPU load |

```c
/* Bus recovery: send 9 SCL pulses to release stuck SDA */
void i2c_bus_recover(GPIO_TypeDef *scl_port, uint16_t scl_pin,
                     GPIO_TypeDef *sda_port, uint16_t sda_pin)
{
    /* Switch pins to GPIO output temporarily */
    HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_SET);

    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_RESET);
        vTaskDelay(pdMS_TO_TICKS(1));
        HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(1));

        if (HAL_GPIO_ReadPin(sda_port, sda_pin) == GPIO_PIN_SET)
            break;   /* SDA released — bus free */
    }

    /* Generate STOP: SCL high, SDA low → high */
    HAL_GPIO_WritePin(sda_port, sda_pin, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(1));
    HAL_GPIO_WritePin(sda_port, sda_pin, GPIO_PIN_SET);

    /* Re-initialise I2C peripheral */
    HAL_I2C_Init(&hi2c1);
}
```

In Rust/Embassy, errors are handled through `Result`:

```rust
async fn robust_read(bus: &SharedI2C, addr: u8, reg: u8) -> Option<u8> {
    const MAX_RETRIES: usize = 3;

    for attempt in 0..MAX_RETRIES {
        let result = with_timeout(Duration::from_millis(50), async {
            let mut bus = bus.lock().await;
            bus.write(addr, &[reg]).await?;
            let mut val = [0u8; 1];
            bus.read(addr, &mut val).await?;
            Ok::<u8, _>(val[0])
        }).await;

        match result {
            Ok(Ok(val)) => return Some(val),
            Ok(Err(e))  => {
                defmt::warn!("I2C error on attempt {}: {:?}", attempt, e);
                Timer::after(Duration::from_millis(10)).await;
            }
            Err(_) => {
                defmt::warn!("I2C timeout on attempt {}", attempt);
            }
        }
    }

    defmt::error!("I2C failed after {} retries", MAX_RETRIES);
    None
}
```

---

## 9. Deadlock Prevention and Priority Inversion

### Priority Inversion

Priority inversion occurs when a high-priority task is blocked waiting for a resource held by a low-priority task, while a medium-priority task pre-empts the low-priority task.

```
Time →
High  ────── waits for mutex ─────────────────── runs ──
Mid   ─────────────────── runs (pre-empts Low) ──────────
Low   ── holds mutex ─── pre-empted ────────────── gives mutex
                          ↑ inversion window
```

**Solution: Use a mutex with priority inheritance** (not a plain binary semaphore):

```c
/* FreeRTOS — always use xSemaphoreCreateMutex() for shared resources */
h->mutex = xSemaphoreCreateMutex();      /* Has priority inheritance */
/* NOT: xSemaphoreCreateBinary()         (no priority inheritance)   */
```

### Deadlock Prevention Checklist

1. **Always specify a timeout** — never pass `portMAX_DELAY` in production code on shared resources
2. **Consistent lock ordering** — if ever acquiring multiple mutexes, always acquire in the same global order
3. **Never call blocking I2C from ISR context** — only `FromISR` variants are safe in ISRs
4. **Release mutex in all code paths** — use RAII patterns in C++ / Rust to guarantee release

```cpp
/* C++ RAII mutex guard for FreeRTOS */
class I2CBusGuard {
    SemaphoreHandle_t m_mutex;
    bool              m_held;
public:
    explicit I2CBusGuard(SemaphoreHandle_t mutex, uint32_t timeout_ms)
        : m_mutex(mutex)
        , m_held(xSemaphoreTake(mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)
    {}

    ~I2CBusGuard()
    {
        if (m_held) xSemaphoreGive(m_mutex);
    }

    explicit operator bool() const { return m_held; }

    /* Non-copyable */
    I2CBusGuard(const I2CBusGuard &) = delete;
    I2CBusGuard &operator=(const I2CBusGuard &) = delete;
};

/* Usage */
bool safe_write(I2C_RTOS_Handle_t *h, uint8_t addr,
                const uint8_t *data, size_t len)
{
    I2CBusGuard guard(h->mutex, h->timeout_ms);
    if (!guard) return false;   /* Could not acquire mutex */

    return HAL_I2C_Master_Transmit(
        (I2C_HandleTypeDef *)h->hal_handle,
        (uint16_t)(addr << 1), (uint8_t *)data,
        (uint16_t)len, h->timeout_ms) == HAL_OK;
    /* Mutex automatically released when guard destructs */
}
```

---

## 10. Performance Considerations

### Clock Speed vs. Cable Length

| Mode | Speed | Max cable (approx.) | Notes |
|---|---|---|---|
| Standard | 100 kHz | ~1 m | Most compatible |
| Fast | 400 kHz | ~30 cm | Requires pull-up tuning |
| Fast-Plus | 1 MHz | ~10 cm | Stronger drivers needed |
| High-Speed | 3.4 MHz | ~10 cm | Special current sources |

### Reducing Blocking Time

```c
/* Bad: poll-wait inside mutex — holds bus while CPU spins */
bool bad_read(I2C_RTOS_Handle_t *h, uint8_t addr, uint8_t *buf, size_t len)
{
    xSemaphoreTake(h->mutex, portMAX_DELAY);
    while (!i2c_hw_rx_ready());   /* Spins — holds mutex, starves other tasks */
    i2c_hw_read(buf, len);
    xSemaphoreGive(h->mutex);
    return true;
}

/* Good: DMA/interrupt driven — mutex released after kick-off,
         semaphore signals completion from ISR */
bool good_read(I2C_RTOS_Handle_t *h, uint8_t addr, uint8_t *buf, size_t len)
{
    xSemaphoreTake(h->mutex, portMAX_DELAY);
    HAL_I2C_Master_Receive_DMA(h->hal_handle, addr << 1, buf, len);
    /* Mutex still held while DMA runs — atomicity guarantee */
    xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(h->timeout_ms));
    xSemaphoreGive(h->mutex);
    return HAL_I2C_GetError(h->hal_handle) == HAL_I2C_ERROR_NONE;
}
```

### Batching Transfers

Combine multiple register reads into one transaction to reduce start/stop overhead:

```c
/* Instead of: 6 separate reads (12 I2C phases) */
i2c_read_reg(h, addr, REG_X_H, &raw[0], 1);
i2c_read_reg(h, addr, REG_X_L, &raw[1], 1);
// ... etc.

/* Do: 1 burst read (2 I2C phases) — 6× less overhead */
i2c_read_reg(h, addr, REG_X_H, raw, 6);   /* Auto-increments register */
```

---

## 11. Testing and Debugging RTOS I2C Drivers

### Logic Analyser Capture

Always use a logic analyser (Saleae, Pulseview/sigrok) to verify:
- Correct device address (7-bit + R/W bit)
- ACK/NACK for each byte
- Correct repeated START timing
- No spurious STOP conditions mid-transaction

### Unit Testing with Mocks (Rust)

```rust
// Using embedded-hal-mock for unit testing I2C drivers in Rust

#[cfg(test)]
mod tests {
    use embedded_hal_mock::eh1::i2c::{Mock, Transaction};

    #[test]
    fn test_lm75_read() {
        let expected = vec![
            Transaction::write(0x48, vec![0x00]),      // Write reg pointer
            Transaction::read(0x48, vec![0x0C, 0x80]), // Read 25.5°C = 0x0C80
        ];

        let mut i2c = Mock::new(&expected);

        // Call your driver function
        let mut buf = [0u8; 2];
        i2c.write(0x48, &[0x00]).unwrap();
        i2c.read(0x48, &mut buf).unwrap();

        let raw16  = (i16::from_be_bytes(buf)) >> 5;
        let temp   = raw16 as f32 * 0.125;
        assert!((temp - 25.5).abs() < 0.001);

        i2c.done();  // Asserts all expected transactions occurred
    }
}
```

### FreeRTOS Trace with Percepio Tracealyzer

```c
/* Add trace hooks to I2C driver for Tracealyzer */
#include "trcRecorder.h"

traceString i2c_ch;

void i2c_trace_init(void)
{
    i2c_ch = xTraceRegisterString("I2C");
}

bool i2c_rtos_write_traced(I2C_RTOS_Handle_t *h,
                            uint8_t addr,
                            const uint8_t *data, size_t len)
{
    vTracePrint(i2c_ch, "TX start");
    bool ok = i2c_rtos_write(h, addr, data, len);
    vTracePrint(i2c_ch, ok ? "TX ok" : "TX fail");
    return ok;
}
```

---

## 12. Summary

RTOS integration transforms a simple I2C driver into a robust, concurrent, real-time component. The key principles are:

**Thread Safety via Mutexes:** Every I2C transaction — from START through STOP — must be atomic from the perspective of other tasks. A mutex (with priority inheritance) is the correct primitive for this, not a binary semaphore.

**Non-Blocking where Possible:** DMA + interrupt-driven transfers free the CPU during data movement. The task blocks on a semaphore rather than spinning, allowing the scheduler to run other work.

**Queue-Based Architectures:** For complex multi-device systems, a dedicated I2C manager task with a request queue serialises access in a controlled, auditable manner, decouples producers from the bus, and enables priority-ordered scheduling.

**RTOS-Specific APIs:** FreeRTOS, Zephyr, and Rust embedded frameworks each have their own idioms. Zephyr's device model provides the highest level of abstraction and portability. Embassy's async/await model provides ergonomic, composable concurrency with compile-time safety. RTIC provides zero-overhead preemptive concurrency with statically verified resource access.

**Error Handling and Recovery:** Timeouts must be specified at every blocking call. Bus recovery (9-clock pulse sequence) must be implemented for stuck-bus scenarios. Errors must propagate to application code, not silently drop data.

**Priority Inversion Prevention:** Always use proper mutexes with priority inheritance. Use RAII guards in C++ and Rust's ownership system to guarantee lock release in all code paths, including error paths.

By applying these patterns, I2C drivers become reliable building blocks in real-time systems — deterministic, safe under concurrent access, and recoverable from hardware faults.

---

*Document: 65_RTOS_I2C_Integration.md | Revision 1.0*
*Covers: FreeRTOS, Zephyr RTOS, Embassy (Rust), RTIC (Rust)*
*Target platforms: STM32, ESP32, Nordic nRF, generic ARM Cortex-M*