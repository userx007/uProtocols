# 11. UART Driver Architecture

**Architecture & Theory** — a four-layer model (Hardware → HAL → Driver → App Interface), a layer-responsibility table, and key design goals (portability, testability, safety, efficiency).

**C/C++ Implementation** — six complete, production-style source files:
- `uart_hal.h/.c` — register map, bit definitions, baud rate calc, IRQ control
- `ring_buffer.h` — lock-free SPSC ring buffer using power-of-2 masking
- `uart_driver.h/.c` — interrupt-driven driver with TX/RX ring buffers, blocking/non-blocking APIs, and a full ISR handler
- `uart_app.h/.c` — high-level `print`, `println`, `printf`, `readline`
- `main.c` — clean application usage with no register access

**Rust Implementation** — idiomatic `no_std` design using:
- `UartHal` trait for hardware abstraction
- `heapless::spsc` for interrupt-safe lock-free queues
- Generic `UartDriver<H: UartHal>` — MCU-agnostic
- `impl core::fmt::Write` for `write!`/`writeln!` macro support
- A **mock HAL** enabling full unit tests on the host without hardware

**Summary** — a recap table, key design decisions (SPSC, on-demand TX IRQ, `nb::Result`), and a **porting checklist** for bringing the architecture to a new MCU.

> **Designing Layered Driver Architecture with HAL and Application Interfaces**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Layered Architecture Overview](#layered-architecture-overview)
3. [Hardware Abstraction Layer (HAL)](#hardware-abstraction-layer-hal)
4. [Driver Layer](#driver-layer)
5. [Application Interface Layer](#application-interface-layer)
6. [Interrupt-Driven and DMA Design](#interrupt-driven-and-dma-design)
7. [Error Handling and Status Reporting](#error-handling-and-status-reporting)
8. [C/C++ Implementation](#cc-implementation)
9. [Rust Implementation](#rust-implementation)
10. [Summary](#summary)

---

## Introduction

A **UART (Universal Asynchronous Receiver/Transmitter)** driver is one of the most fundamental components in embedded systems firmware. While a naive UART driver may directly manipulate hardware registers from application code, a production-grade driver requires a carefully designed **layered architecture** that separates hardware specifics from business logic, enables portability, simplifies testing, and provides clean, safe interfaces for application developers.

The layered driver architecture principle divides responsibilities across distinct abstraction layers:

```
┌──────────────────────────────────────────────────────┐
│                 Application Layer                    │  ← User/app code
├──────────────────────────────────────────────────────┤
│             Application Interface Layer              │  ← High-level API (read/write/printf)
├──────────────────────────────────────────────────────┤
│               UART Driver / Middleware               │  ← Buffering, flow control, framing
├──────────────────────────────────────────────────────┤
│         Hardware Abstraction Layer (HAL)             │  ← Register-level, platform-specific
├──────────────────────────────────────────────────────┤
│              Hardware (UART peripheral)              │  ← Physical registers & pins
└──────────────────────────────────────────────────────┘
```

Each layer communicates only with its immediate neighbors through well-defined interfaces. This separation of concerns is the foundation of maintainable embedded firmware.

---

## Layered Architecture Overview

### Layer Responsibilities

| Layer | Responsibility | Example |
|---|---|---|
| **Hardware** | Physical registers, clocks, pins | UART0 base address 0x4000C000 |
| **HAL** | Abstract register access, clock config | `hal_uart_set_baud()` |
| **Driver** | Buffering, IRQ handling, flow control | Ring buffer, TX/RX state machine |
| **App Interface** | High-level API, protocol framing | `uart_write_string()`, `uart_readline()` |
| **Application** | Business logic | Sensor polling, command parsing |

### Key Design Goals

- **Portability**: Swap the HAL for a new MCU without touching application code
- **Testability**: Mock the HAL in unit tests; test driver logic on host
- **Safety**: Enforce access discipline; prevent direct register manipulation from app code
- **Efficiency**: Use interrupts or DMA; avoid busy-wait polling in production
- **Maintainability**: Clear API contracts between layers

---

## Hardware Abstraction Layer (HAL)

The HAL is the only layer that knows about physical register addresses and MCU-specific details. It exposes a minimal, stable interface upward.

### HAL Responsibilities

- Configure UART peripheral clocks and GPIO pins
- Set baud rate, data bits, stop bits, parity
- Enable/disable the peripheral
- Read/write data registers directly
- Configure and enable interrupts
- Manage DMA channels (if applicable)

### HAL Interface Design Principles

- Functions are **thin wrappers** around register operations
- No buffering, no state machines — pure hardware control
- Parameters use **platform-independent types** (`uint32_t`, not `int`)
- Returns a status code (never crashes silently)

---

## Driver Layer

The driver layer sits above the HAL and implements the core UART logic:

- **TX/RX ring buffers** to decouple interrupt context from application context
- **Interrupt Service Routines (ISRs)** that feed/drain the hardware FIFO
- **Flow control** (hardware RTS/CTS or software XON/XOFF)
- **Error tracking** (framing errors, overrun, parity errors)
- **Blocking / non-blocking / callback modes**

The driver is MCU-agnostic — it calls only HAL functions and standard C/Rust constructs.

---

## Application Interface Layer

The application interface is the public API consumed by user code. It provides:

- Simple `read` / `write` / `flush` semantics
- Optional line-oriented interfaces (`readline`)
- Printf-style formatted output
- Timeout support
- Stream or file descriptor abstractions (POSIX-like)

---

## Interrupt-Driven and DMA Design

### Interrupt-Driven Model

The ISR handles byte-level hardware events and operates on ring buffers shared with the driver:

```
TX Path:  App writes to TX ring buffer → ISR drains ring buffer → UART hardware
RX Path:  UART hardware fills RX FIFO → ISR fills RX ring buffer → App reads
```

Critical design rules:
- ISR must be **minimal and fast** — no heap allocation, no blocking calls
- Shared data structures must be **atomically protected** (disable IRQ or use atomics)
- Ring buffer `head` and `tail` indices must be volatile or atomic

### DMA Model

For high-throughput applications, DMA transfers entire blocks:

```
TX DMA:  App fills DMA buffer → DMA controller streams to UART TX register
RX DMA:  UART RX register → DMA fills circular buffer → App reads on completion IRQ
```

DMA reduces CPU load but adds complexity: buffer ownership, cache coherency (on Cortex-A), and transfer completion events must be carefully managed.

---

## Error Handling and Status Reporting

A robust driver defines an explicit error type:

- `UART_OK` — success
- `UART_ERR_TIMEOUT` — operation did not complete in time
- `UART_ERR_OVERRUN` — RX buffer overflowed
- `UART_ERR_FRAMING` — invalid stop bit detected
- `UART_ERR_PARITY` — parity mismatch
- `UART_ERR_BUSY` — peripheral busy
- `UART_ERR_INVALID_ARG` — bad parameter

Error state should be **queryable** (poll) and optionally **reported via callback** for async designs.

---

## C/C++ Implementation

### 1. HAL Header (`uart_hal.h`)

```c
// uart_hal.h — Hardware Abstraction Layer for UART
// Platform: ARM Cortex-M (example: STM32-like)

#ifndef UART_HAL_H
#define UART_HAL_H

#include <stdint.h>
#include <stdbool.h>

// ─── Register Map (platform-specific) ────────────────────────────────────────

typedef struct {
    volatile uint32_t SR;    // Status Register
    volatile uint32_t DR;    // Data Register
    volatile uint32_t BRR;   // Baud Rate Register
    volatile uint32_t CR1;   // Control Register 1
    volatile uint32_t CR2;   // Control Register 2
    volatile uint32_t CR3;   // Control Register 3
} UART_RegDef_t;

// ─── Bit Definitions ──────────────────────────────────────────────────────────

#define UART_SR_RXNE     (1U << 5)   // RX Not Empty
#define UART_SR_TXE      (1U << 7)   // TX Empty
#define UART_SR_TC       (1U << 6)   // Transmission Complete
#define UART_SR_ORE      (1U << 3)   // Overrun Error
#define UART_SR_FE       (1U << 1)   // Framing Error
#define UART_SR_PE       (1U << 0)   // Parity Error

#define UART_CR1_UE      (1U << 13)  // UART Enable
#define UART_CR1_TE      (1U << 3)   // Transmitter Enable
#define UART_CR1_RE      (1U << 2)   // Receiver Enable
#define UART_CR1_RXNEIE  (1U << 5)   // RXNE Interrupt Enable
#define UART_CR1_TXEIE   (1U << 7)   // TXE Interrupt Enable
#define UART_CR1_TCIE    (1U << 6)   // TC Interrupt Enable

// ─── HAL Configuration Struct ─────────────────────────────────────────────────

typedef enum {
    UART_PARITY_NONE = 0,
    UART_PARITY_EVEN,
    UART_PARITY_ODD
} uart_parity_t;

typedef enum {
    UART_STOPBITS_1 = 0,
    UART_STOPBITS_2
} uart_stopbits_t;

typedef struct {
    UART_RegDef_t  *regs;           // Pointer to peripheral registers
    uint32_t        peripheral_clk; // Peripheral clock in Hz (e.g. 48000000)
    uint32_t        baud_rate;      // Desired baud rate (e.g. 115200)
    uart_parity_t   parity;
    uart_stopbits_t stop_bits;
    uint8_t         irq_number;     // NVIC IRQ number
    uint8_t         irq_priority;
} uart_hal_config_t;

// ─── HAL Status ───────────────────────────────────────────────────────────────

typedef enum {
    UART_HAL_OK = 0,
    UART_HAL_ERR_INVALID_ARG,
    UART_HAL_ERR_TIMEOUT,
    UART_HAL_ERR_HW_FAULT
} uart_hal_status_t;

// ─── HAL API ──────────────────────────────────────────────────────────────────

uart_hal_status_t uart_hal_init(const uart_hal_config_t *cfg);
uart_hal_status_t uart_hal_deinit(UART_RegDef_t *regs);

void     uart_hal_enable_tx_irq(UART_RegDef_t *regs);
void     uart_hal_disable_tx_irq(UART_RegDef_t *regs);
void     uart_hal_enable_rx_irq(UART_RegDef_t *regs);
void     uart_hal_disable_rx_irq(UART_RegDef_t *regs);

bool     uart_hal_is_tx_empty(UART_RegDef_t *regs);
bool     uart_hal_is_rx_ready(UART_RegDef_t *regs);
bool     uart_hal_has_error(UART_RegDef_t *regs);

void     uart_hal_write_byte(UART_RegDef_t *regs, uint8_t byte);
uint8_t  uart_hal_read_byte(UART_RegDef_t *regs);
uint32_t uart_hal_get_status(UART_RegDef_t *regs);
void     uart_hal_clear_errors(UART_RegDef_t *regs);

#endif // UART_HAL_H
```

### 2. HAL Implementation (`uart_hal.c`)

```c
// uart_hal.c — HAL Implementation

#include "uart_hal.h"
#include <stddef.h>

// ─── NVIC helpers (platform-specific stubs) ───────────────────────────────────

static void nvic_set_priority(uint8_t irq, uint8_t priority) {
    // Platform: NVIC->IP[irq] = priority << 4;
    (void)irq; (void)priority; // stub
}

static void nvic_enable_irq(uint8_t irq) {
    // Platform: NVIC->ISER[irq / 32] = 1U << (irq % 32);
    (void)irq; // stub
}

// ─── HAL Init ─────────────────────────────────────────────────────────────────

uart_hal_status_t uart_hal_init(const uart_hal_config_t *cfg) {
    if (!cfg || !cfg->regs || cfg->baud_rate == 0 || cfg->peripheral_clk == 0) {
        return UART_HAL_ERR_INVALID_ARG;
    }

    UART_RegDef_t *regs = cfg->regs;

    // Disable UART before configuration
    regs->CR1 &= ~UART_CR1_UE;

    // Configure baud rate: BRR = peripheral_clk / baud_rate
    regs->BRR = cfg->peripheral_clk / cfg->baud_rate;

    // Configure parity
    if (cfg->parity == UART_PARITY_EVEN) {
        regs->CR1 |= (1U << 10) | (1U << 9);  // PCE | even
    } else if (cfg->parity == UART_PARITY_ODD) {
        regs->CR1 |= (1U << 10);               // PCE | odd
        regs->CR1 &= ~(1U << 9);
    } else {
        regs->CR1 &= ~(1U << 10);              // No parity
    }

    // Configure stop bits
    regs->CR2 &= ~(0x3U << 12);
    if (cfg->stop_bits == UART_STOPBITS_2) {
        regs->CR2 |= (0x2U << 12);
    }

    // Enable TX, RX
    regs->CR1 |= UART_CR1_TE | UART_CR1_RE;

    // Configure and enable NVIC IRQ
    nvic_set_priority(cfg->irq_number, cfg->irq_priority);
    nvic_enable_irq(cfg->irq_number);

    // Enable RX interrupt, leave TX interrupt disabled (will enable on demand)
    regs->CR1 |= UART_CR1_RXNEIE;

    // Enable UART
    regs->CR1 |= UART_CR1_UE;

    return UART_HAL_OK;
}

uart_hal_status_t uart_hal_deinit(UART_RegDef_t *regs) {
    if (!regs) return UART_HAL_ERR_INVALID_ARG;
    regs->CR1 = 0;
    regs->CR2 = 0;
    regs->CR3 = 0;
    return UART_HAL_OK;
}

// ─── IRQ Control ──────────────────────────────────────────────────────────────

void uart_hal_enable_tx_irq(UART_RegDef_t *regs)  { regs->CR1 |=  UART_CR1_TXEIE; }
void uart_hal_disable_tx_irq(UART_RegDef_t *regs) { regs->CR1 &= ~UART_CR1_TXEIE; }
void uart_hal_enable_rx_irq(UART_RegDef_t *regs)  { regs->CR1 |=  UART_CR1_RXNEIE; }
void uart_hal_disable_rx_irq(UART_RegDef_t *regs) { regs->CR1 &= ~UART_CR1_RXNEIE; }

// ─── Status Queries ───────────────────────────────────────────────────────────

bool     uart_hal_is_tx_empty(UART_RegDef_t *regs) { return (regs->SR & UART_SR_TXE)  != 0; }
bool     uart_hal_is_rx_ready(UART_RegDef_t *regs) { return (regs->SR & UART_SR_RXNE) != 0; }
bool     uart_hal_has_error(UART_RegDef_t *regs)   { return (regs->SR & (UART_SR_ORE | UART_SR_FE | UART_SR_PE)) != 0; }
uint32_t uart_hal_get_status(UART_RegDef_t *regs)  { return regs->SR; }

void uart_hal_clear_errors(UART_RegDef_t *regs) {
    // A read of SR followed by a read of DR clears error flags on many MCUs
    volatile uint32_t dummy = regs->SR;
    dummy = regs->DR;
    (void)dummy;
}

// ─── Data I/O ─────────────────────────────────────────────────────────────────

void    uart_hal_write_byte(UART_RegDef_t *regs, uint8_t byte) { regs->DR = byte; }
uint8_t uart_hal_read_byte(UART_RegDef_t *regs)                { return (uint8_t)(regs->DR & 0xFF); }
```

### 3. Ring Buffer (`ring_buffer.h` / `ring_buffer.c`)

```c
// ring_buffer.h — Lock-free single-producer single-consumer ring buffer

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Buffer capacity must be a power of 2 for efficient masking
#define RING_BUF_SIZE 256U  // Must be power of 2

typedef struct {
    uint8_t  buf[RING_BUF_SIZE];
    volatile uint32_t head;  // Written by producer
    volatile uint32_t tail;  // Written by consumer
} ring_buffer_t;

static inline void  ring_buf_init(ring_buffer_t *rb)                  { rb->head = rb->tail = 0; }
static inline bool  ring_buf_empty(const ring_buffer_t *rb)           { return rb->head == rb->tail; }
static inline bool  ring_buf_full(const ring_buffer_t *rb)            { return (rb->head - rb->tail) == RING_BUF_SIZE; }
static inline uint32_t ring_buf_count(const ring_buffer_t *rb)        { return rb->head - rb->tail; }
static inline uint32_t ring_buf_space(const ring_buffer_t *rb)        { return RING_BUF_SIZE - ring_buf_count(rb); }

// Returns true on success; false if buffer full
static inline bool ring_buf_push(ring_buffer_t *rb, uint8_t byte) {
    if (ring_buf_full(rb)) return false;
    rb->buf[rb->head & (RING_BUF_SIZE - 1)] = byte;
    rb->head++;    // Atomic store on 32-bit ARM with appropriate ordering
    return true;
}

// Returns true on success; false if buffer empty
static inline bool ring_buf_pop(ring_buffer_t *rb, uint8_t *byte) {
    if (ring_buf_empty(rb)) return false;
    *byte = rb->buf[rb->tail & (RING_BUF_SIZE - 1)];
    rb->tail++;
    return true;
}

#endif // RING_BUFFER_H
```

### 4. UART Driver (`uart_driver.h` / `uart_driver.c`)

```c
// uart_driver.h — Driver Layer

#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include "uart_hal.h"
#include "ring_buffer.h"
#include <stddef.h>

typedef enum {
    UART_OK = 0,
    UART_ERR_TIMEOUT,
    UART_ERR_OVERRUN,
    UART_ERR_FRAMING,
    UART_ERR_PARITY,
    UART_ERR_BUSY,
    UART_ERR_INVALID_ARG,
    UART_ERR_NOT_INIT
} uart_status_t;

typedef void (*uart_rx_callback_t)(uint8_t byte, void *ctx);
typedef void (*uart_error_callback_t)(uart_status_t err, void *ctx);

typedef struct {
    // HAL config
    uart_hal_config_t  hal_cfg;

    // Buffers
    ring_buffer_t      tx_buf;
    ring_buffer_t      rx_buf;

    // Callbacks (optional)
    uart_rx_callback_t    rx_cb;
    uart_error_callback_t err_cb;
    void                 *cb_ctx;

    // State
    volatile bool      tx_busy;
    volatile uint32_t  error_flags;
    bool               initialized;
} uart_driver_t;

// ─── Driver API ───────────────────────────────────────────────────────────────

uart_status_t uart_driver_init(uart_driver_t *drv, const uart_hal_config_t *cfg);
uart_status_t uart_driver_deinit(uart_driver_t *drv);

// Register optional callbacks
void uart_driver_set_rx_callback(uart_driver_t *drv, uart_rx_callback_t cb, void *ctx);
void uart_driver_set_error_callback(uart_driver_t *drv, uart_error_callback_t cb, void *ctx);

// Non-blocking write: copies to TX buffer, returns bytes written
size_t uart_driver_write(uart_driver_t *drv, const uint8_t *data, size_t len);

// Non-blocking read: copies from RX buffer, returns bytes read
size_t uart_driver_read(uart_driver_t *drv, uint8_t *buf, size_t max_len);

// Blocking write with timeout (ms); 0 = no timeout
uart_status_t uart_driver_write_blocking(uart_driver_t *drv, const uint8_t *data,
                                         size_t len, uint32_t timeout_ms);

// Blocking read: wait until 'len' bytes received or timeout
uart_status_t uart_driver_read_blocking(uart_driver_t *drv, uint8_t *buf,
                                        size_t len, uint32_t timeout_ms);

// Query
bool         uart_driver_tx_busy(const uart_driver_t *drv);
uint32_t     uart_driver_rx_available(const uart_driver_t *drv);
uart_status_t uart_driver_get_error(uart_driver_t *drv);

// Called from ISR — do NOT call from application code
void uart_driver_irq_handler(uart_driver_t *drv);

#endif // UART_DRIVER_H
```

```c
// uart_driver.c — Driver Layer Implementation

#include "uart_driver.h"
#include <string.h>

// Platform-specific: get current time in ms (e.g., SysTick counter)
extern uint32_t platform_get_tick_ms(void);

// Platform-specific: enter / exit critical section
extern void platform_enter_critical(void);
extern void platform_exit_critical(void);

// ─── Init ─────────────────────────────────────────────────────────────────────

uart_status_t uart_driver_init(uart_driver_t *drv, const uart_hal_config_t *cfg) {
    if (!drv || !cfg) return UART_ERR_INVALID_ARG;

    memset(drv, 0, sizeof(*drv));
    drv->hal_cfg = *cfg;

    ring_buf_init(&drv->tx_buf);
    ring_buf_init(&drv->rx_buf);

    uart_hal_status_t st = uart_hal_init(cfg);
    if (st != UART_HAL_OK) return UART_ERR_INVALID_ARG;

    drv->initialized = true;
    return UART_OK;
}

uart_status_t uart_driver_deinit(uart_driver_t *drv) {
    if (!drv || !drv->initialized) return UART_ERR_NOT_INIT;
    uart_hal_deinit(drv->hal_cfg.regs);
    drv->initialized = false;
    return UART_OK;
}

// ─── Callbacks ────────────────────────────────────────────────────────────────

void uart_driver_set_rx_callback(uart_driver_t *drv, uart_rx_callback_t cb, void *ctx) {
    drv->rx_cb  = cb;
    drv->cb_ctx = ctx;
}

void uart_driver_set_error_callback(uart_driver_t *drv, uart_error_callback_t cb, void *ctx) {
    drv->err_cb = cb;
    drv->cb_ctx = ctx;
}

// ─── Non-blocking Write ───────────────────────────────────────────────────────

size_t uart_driver_write(uart_driver_t *drv, const uint8_t *data, size_t len) {
    if (!drv || !drv->initialized || !data) return 0;

    size_t written = 0;
    platform_enter_critical();
    for (size_t i = 0; i < len; i++) {
        if (!ring_buf_push(&drv->tx_buf, data[i])) break;
        written++;
    }
    // Kick off transmission if not already running
    if (!drv->tx_busy && !ring_buf_empty(&drv->tx_buf)) {
        drv->tx_busy = true;
        uart_hal_enable_tx_irq(drv->hal_cfg.regs);
    }
    platform_exit_critical();
    return written;
}

// ─── Non-blocking Read ────────────────────────────────────────────────────────

size_t uart_driver_read(uart_driver_t *drv, uint8_t *buf, size_t max_len) {
    if (!drv || !drv->initialized || !buf) return 0;

    size_t count = 0;
    platform_enter_critical();
    while (count < max_len && ring_buf_pop(&drv->rx_buf, &buf[count])) {
        count++;
    }
    platform_exit_critical();
    return count;
}

// ─── Blocking Write ───────────────────────────────────────────────────────────

uart_status_t uart_driver_write_blocking(uart_driver_t *drv, const uint8_t *data,
                                          size_t len, uint32_t timeout_ms) {
    if (!drv || !drv->initialized) return UART_ERR_NOT_INIT;

    size_t sent = 0;
    uint32_t start = platform_get_tick_ms();

    while (sent < len) {
        size_t n = uart_driver_write(drv, data + sent, len - sent);
        sent += n;

        if (sent < len) {
            if (timeout_ms && (platform_get_tick_ms() - start) >= timeout_ms) {
                return UART_ERR_TIMEOUT;
            }
            // Yield / __WFI() in a real RTOS context
        }
    }

    // Wait for TX to physically complete
    if (timeout_ms) {
        while (drv->tx_busy) {
            if ((platform_get_tick_ms() - start) >= timeout_ms) {
                return UART_ERR_TIMEOUT;
            }
        }
    }
    return UART_OK;
}

// ─── Blocking Read ────────────────────────────────────────────────────────────

uart_status_t uart_driver_read_blocking(uart_driver_t *drv, uint8_t *buf,
                                         size_t len, uint32_t timeout_ms) {
    if (!drv || !drv->initialized) return UART_ERR_NOT_INIT;

    size_t received = 0;
    uint32_t start  = platform_get_tick_ms();

    while (received < len) {
        size_t n = uart_driver_read(drv, buf + received, len - received);
        received += n;

        if (received < len) {
            if (timeout_ms && (platform_get_tick_ms() - start) >= timeout_ms) {
                return UART_ERR_TIMEOUT;
            }
        }
    }
    return UART_OK;
}

// ─── Status ───────────────────────────────────────────────────────────────────

bool         uart_driver_tx_busy(const uart_driver_t *drv)  { return drv->tx_busy; }
uint32_t     uart_driver_rx_available(const uart_driver_t *drv) { return ring_buf_count(&drv->rx_buf); }

uart_status_t uart_driver_get_error(uart_driver_t *drv) {
    uart_status_t err = (uart_status_t)drv->error_flags;
    drv->error_flags = 0;
    return err;
}

// ─── ISR Handler — called from MCU vector table ───────────────────────────────

void uart_driver_irq_handler(uart_driver_t *drv) {
    UART_RegDef_t *regs = drv->hal_cfg.regs;
    uint32_t sr = uart_hal_get_status(regs);

    // ── Hardware error handling ──────────────────────────────────────────────
    if (sr & (UART_SR_ORE | UART_SR_FE | UART_SR_PE)) {
        if (sr & UART_SR_ORE) drv->error_flags |= UART_ERR_OVERRUN;
        if (sr & UART_SR_FE)  drv->error_flags |= UART_ERR_FRAMING;
        if (sr & UART_SR_PE)  drv->error_flags |= UART_ERR_PARITY;

        uart_hal_clear_errors(regs);

        if (drv->err_cb) {
            drv->err_cb((uart_status_t)drv->error_flags, drv->cb_ctx);
        }
    }

    // ── RX: byte received ────────────────────────────────────────────────────
    if (sr & UART_SR_RXNE) {
        uint8_t byte = uart_hal_read_byte(regs);

        if (drv->rx_cb) {
            drv->rx_cb(byte, drv->cb_ctx);   // Callback mode
        } else {
            if (!ring_buf_push(&drv->rx_buf, byte)) {
                drv->error_flags |= UART_ERR_OVERRUN;  // RX software overrun
            }
        }
    }

    // ── TX: data register empty ───────────────────────────────────────────────
    if (sr & UART_SR_TXE) {
        uint8_t byte;
        if (ring_buf_pop(&drv->tx_buf, &byte)) {
            uart_hal_write_byte(regs, byte);
        } else {
            // Nothing left to send — disable TX interrupt
            uart_hal_disable_tx_irq(regs);
            drv->tx_busy = false;
        }
    }
}
```

### 5. Application Interface (`uart_app.h` / `uart_app.c`)

```c
// uart_app.h — High-level application interface

#ifndef UART_APP_H
#define UART_APP_H

#include "uart_driver.h"

// ─── Initialise application UART ──────────────────────────────────────────────

uart_status_t uart_app_init(uart_driver_t *drv, uint32_t baud_rate);

// ─── Simple string I/O ────────────────────────────────────────────────────────

uart_status_t uart_app_print(uart_driver_t *drv, const char *str);
uart_status_t uart_app_println(uart_driver_t *drv, const char *str);

// Printf-style formatted output (uses internal 256-byte stack buffer)
uart_status_t uart_app_printf(uart_driver_t *drv, const char *fmt, ...);

// Read a line (terminated by '\n') into buf; strips '\r\n'
uart_status_t uart_app_readline(uart_driver_t *drv, char *buf,
                                size_t max_len, uint32_t timeout_ms);

#endif // UART_APP_H
```

```c
// uart_app.c — Application Interface Implementation

#include "uart_app.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// Example HAL config targeting UART1 at a fixed base address
#define UART1_BASE  ((UART_RegDef_t *)0x40011000UL)
#define APB2_CLOCK  72000000UL

uart_status_t uart_app_init(uart_driver_t *drv, uint32_t baud_rate) {
    uart_hal_config_t cfg = {
        .regs            = UART1_BASE,
        .peripheral_clk  = APB2_CLOCK,
        .baud_rate       = baud_rate,
        .parity          = UART_PARITY_NONE,
        .stop_bits       = UART_STOPBITS_1,
        .irq_number      = 37,   // USART1 IRQ on STM32F1xx
        .irq_priority    = 5,
    };
    return uart_driver_init(drv, &cfg);
}

uart_status_t uart_app_print(uart_driver_t *drv, const char *str) {
    if (!str) return UART_ERR_INVALID_ARG;
    return uart_driver_write_blocking(drv, (const uint8_t *)str, strlen(str), 1000);
}

uart_status_t uart_app_println(uart_driver_t *drv, const char *str) {
    uart_status_t st = uart_app_print(drv, str);
    if (st == UART_OK) st = uart_app_print(drv, "\r\n");
    return st;
}

uart_status_t uart_app_printf(uart_driver_t *drv, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return UART_ERR_INVALID_ARG;
    return uart_driver_write_blocking(drv, (const uint8_t *)buf,
                                      (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1,
                                      1000);
}

uart_status_t uart_app_readline(uart_driver_t *drv, char *buf,
                                size_t max_len, uint32_t timeout_ms) {
    if (!buf || max_len < 2) return UART_ERR_INVALID_ARG;

    size_t pos   = 0;
    uint32_t start = platform_get_tick_ms();

    while (pos < max_len - 1) {
        uint8_t byte;
        if (uart_driver_read(drv, &byte, 1) == 1) {
            if (byte == '\n') break;
            if (byte != '\r') buf[pos++] = (char)byte;
        }
        if (timeout_ms && (platform_get_tick_ms() - start) >= timeout_ms) {
            buf[pos] = '\0';
            return UART_ERR_TIMEOUT;
        }
    }
    buf[pos] = '\0';
    return UART_OK;
}
```

### 6. Application Usage (`main.c`)

```c
// main.c — Application demonstrates clean separation from hardware

#include "uart_app.h"
#include <stdint.h>

// One global driver instance (or pass it around / use RTOS task context)
static uart_driver_t g_uart1;

// Vector table entry (called by hardware)
void USART1_IRQHandler(void) {
    uart_driver_irq_handler(&g_uart1);
}

int main(void) {
    // --- System / clock init (platform-specific) ---
    // SystemInit();
    // RCC_Config();
    // GPIO_Config();   // PA9 = TX, PA10 = RX

    // --- Initialise UART at 115200 baud ---
    uart_status_t st = uart_app_init(&g_uart1, 115200);
    if (st != UART_OK) {
        // Handle fatal init error (e.g. blink LED, halt)
        while (1);
    }

    uart_app_println(&g_uart1, "UART Driver Architecture Demo");

    char cmd_buf[64];
    uint32_t counter = 0;

    while (1) {
        uart_app_printf(&g_uart1, "[%lu] Enter command: ", counter++);

        st = uart_app_readline(&g_uart1, cmd_buf, sizeof(cmd_buf), 5000);

        if (st == UART_OK) {
            uart_app_printf(&g_uart1, "Received: '%s'\r\n", cmd_buf);
        } else if (st == UART_ERR_TIMEOUT) {
            uart_app_println(&g_uart1, "(timeout — no input)");
        }
    }
}
```

---

## Rust Implementation

Rust's ownership model, trait system, and zero-cost abstractions make it an excellent language for embedded driver architecture. We use the `embedded-hal` traits as the HAL interface and build a driver and application interface on top.

### Cargo.toml Dependencies

```toml
[dependencies]
embedded-hal   = "1.0"
nb             = "1.1"
heapless       = "0.8"    # fixed-size collections (no alloc required)
cortex-m       = "0.7"

[features]
default = []
```

### 1. HAL Trait Definition (`src/hal.rs`)

```rust
// src/hal.rs — Platform-agnostic HAL traits for UART

use core::fmt;

/// Low-level hardware errors
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HalError {
    Framing,
    Parity,
    Overrun,
    InvalidConfig,
    NotReady,
}

impl fmt::Display for HalError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            HalError::Framing      => write!(f, "Framing error"),
            HalError::Parity       => write!(f, "Parity error"),
            HalError::Overrun      => write!(f, "Overrun error"),
            HalError::InvalidConfig => write!(f, "Invalid configuration"),
            HalError::NotReady     => write!(f, "Peripheral not ready"),
        }
    }
}

/// UART configuration
#[derive(Debug, Clone, Copy)]
pub struct UartConfig {
    pub baud_rate:  u32,
    pub data_bits:  DataBits,
    pub stop_bits:  StopBits,
    pub parity:     Parity,
}

#[derive(Debug, Clone, Copy)]
pub enum DataBits { Eight, Nine }

#[derive(Debug, Clone, Copy)]
pub enum StopBits { One, Two }

#[derive(Debug, Clone, Copy)]
pub enum Parity { None, Even, Odd }

impl Default for UartConfig {
    fn default() -> Self {
        Self {
            baud_rate: 115_200,
            data_bits: DataBits::Eight,
            stop_bits: StopBits::One,
            parity:    Parity::None,
        }
    }
}

/// Core HAL trait: minimal register-level UART access
/// Implementors: MCU-specific register structs
pub trait UartHal {
    fn configure(&mut self, config: &UartConfig) -> Result<(), HalError>;

    fn enable(&mut self);
    fn disable(&mut self);

    /// Write a byte to the TX data register; returns Err if TX register full
    fn write_byte(&mut self, byte: u8) -> nb::Result<(), HalError>;

    /// Read a byte from the RX data register; returns Err(WouldBlock) if empty
    fn read_byte(&mut self) -> nb::Result<u8, HalError>;

    fn is_tx_empty(&self) -> bool;
    fn is_rx_ready(&self) -> bool;

    fn enable_rx_interrupt(&mut self);
    fn disable_rx_interrupt(&mut self);
    fn enable_tx_interrupt(&mut self);
    fn disable_tx_interrupt(&mut self);

    fn clear_errors(&mut self);
}
```

### 2. Ring Buffer (`src/ring_buffer.rs`)

```rust
// src/ring_buffer.rs — Heapless SPSC ring buffer wrapper

use heapless::spsc::{Consumer, Producer, Queue};

/// Convenience wrapper: fixed 256-byte SPSC queue
/// Use heapless::spsc for interrupt-safe lock-free operation
pub type UartQueue = Queue<u8, 256>;

/// Helper to split and store producer/consumer separately
pub struct RingBuffer {
    queue: UartQueue,
}

impl RingBuffer {
    pub const fn new() -> Self {
        Self { queue: Queue::new() }
    }

    pub fn split(&mut self) -> (Producer<'_, u8, 256>, Consumer<'_, u8, 256>) {
        self.queue.split()
    }
}
```

### 3. UART Driver (`src/driver.rs`)

```rust
// src/driver.rs — Platform-agnostic UART driver with interrupt support

use crate::hal::{HalError, UartConfig, UartHal};
use heapless::spsc::{Consumer, Producer};

/// Driver-level errors
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DriverError {
    Hal(HalError),
    TxBufferFull,
    RxBufferEmpty,
    Timeout,
    NotInitialized,
}

impl From<HalError> for DriverError {
    fn from(e: HalError) -> Self { DriverError::Hal(e) }
}

/// UART driver state, generic over HAL implementation
///
/// `H`  — HAL type implementing `UartHal`
/// `N`  — Ring buffer capacity (must match Producer/Consumer queue size)
pub struct UartDriver<H: UartHal> {
    hal:         H,
    tx_producer: Option<Producer<'static, u8, 256>>,
    tx_consumer: Option<Consumer<'static, u8, 256>>,
    rx_producer: Option<Producer<'static, u8, 256>>,
    rx_consumer: Option<Consumer<'static, u8, 256>>,
    initialized: bool,
}

impl<H: UartHal> UartDriver<H> {
    /// Create driver with HAL instance; call `init()` before use
    pub fn new(hal: H) -> Self {
        Self {
            hal,
            tx_producer: None,
            tx_consumer: None,
            rx_producer: None,
            rx_consumer: None,
            initialized: false,
        }
    }

    /// Initialise with split ring buffers and UART config
    /// `tx` and `rx` are borrowed from static `UartQueue` allocations
    pub fn init(
        &mut self,
        config: &UartConfig,
        tx: (Producer<'static, u8, 256>, Consumer<'static, u8, 256>),
        rx: (Producer<'static, u8, 256>, Consumer<'static, u8, 256>),
    ) -> Result<(), DriverError> {
        self.hal.configure(config)?;
        self.hal.enable_rx_interrupt();
        self.hal.enable();
        self.tx_producer = Some(tx.0);
        self.tx_consumer = Some(tx.1);
        self.rx_producer = Some(rx.0);
        self.rx_consumer = Some(rx.1);
        self.initialized  = true;
        Ok(())
    }

    /// Non-blocking: enqueue bytes for transmission
    /// Returns number of bytes actually enqueued
    pub fn write(&mut self, data: &[u8]) -> usize {
        let Some(prod) = self.tx_producer.as_mut() else { return 0 };
        let mut count = 0;
        for &byte in data {
            if prod.enqueue(byte).is_err() { break; }
            count += 1;
        }
        // Kick transmission if TX interrupt not already enabled
        self.hal.enable_tx_interrupt();
        count
    }

    /// Non-blocking: drain bytes from RX buffer into `buf`
    pub fn read(&mut self, buf: &mut [u8]) -> usize {
        let Some(cons) = self.rx_consumer.as_mut() else { return 0 };
        let mut count = 0;
        while count < buf.len() {
            match cons.dequeue() {
                Some(b) => { buf[count] = b; count += 1; }
                None    => break,
            }
        }
        count
    }

    /// Bytes available in the RX buffer
    pub fn rx_available(&self) -> usize {
        self.rx_consumer.as_ref().map(|c| c.len()).unwrap_or(0)
    }

    /// Called from interrupt context (UART ISR)
    /// SAFETY: must only be called from a single interrupt context
    pub fn on_interrupt(&mut self) {
        // RX: byte received
        if self.hal.is_rx_ready() {
            match self.hal.read_byte() {
                Ok(byte) => {
                    if let Some(prod) = self.rx_producer.as_mut() {
                        let _ = prod.enqueue(byte); // silently drop on overflow
                    }
                }
                Err(nb::Error::Other(e)) => {
                    self.hal.clear_errors();
                    // Optionally record error for polling
                    let _ = e;
                }
                Err(nb::Error::WouldBlock) => {}
            }
        }

        // TX: data register empty — feed next byte or disable interrupt
        if self.hal.is_tx_empty() {
            match self.tx_consumer.as_mut().and_then(|c| c.dequeue()) {
                Some(byte) => {
                    let _ = self.hal.write_byte(byte);
                }
                None => {
                    self.hal.disable_tx_interrupt();
                }
            }
        }
    }
}
```

### 4. Application Interface (`src/app.rs`)

```rust
// src/app.rs — High-level application API

use crate::driver::{DriverError, UartDriver};
use crate::hal::UartHal;
use core::fmt::Write;
use heapless::String;

/// Application-level UART wrapper with formatted I/O
pub struct UartApp<H: UartHal> {
    driver: UartDriver<H>,
}

impl<H: UartHal> UartApp<H> {
    pub fn new(driver: UartDriver<H>) -> Self {
        Self { driver }
    }

    /// Write a string slice
    pub fn print(&mut self, s: &str) -> usize {
        self.driver.write(s.as_bytes())
    }

    /// Write a string with CRLF
    pub fn println(&mut self, s: &str) -> usize {
        let n = self.driver.write(s.as_bytes());
        n + self.driver.write(b"\r\n")
    }

    /// Non-blocking read; returns how many bytes were read
    pub fn read(&mut self, buf: &mut [u8]) -> usize {
        self.driver.read(buf)
    }

    /// Read a line terminated by '\n'; returns the line without line ending
    /// Blocks (spins) until '\n' received or `max_polls` iterations elapsed
    pub fn readline<const N: usize>(
        &mut self,
        buf: &mut heapless::Vec<u8, N>,
        max_polls: usize,
    ) -> Result<(), DriverError> {
        buf.clear();
        for _ in 0..max_polls {
            let mut byte = [0u8; 1];
            if self.driver.read(&mut byte) == 1 {
                match byte[0] {
                    b'\n' => return Ok(()),
                    b'\r' => {}                        // strip CR
                    c     => {
                        buf.push(c).map_err(|_| DriverError::TxBufferFull)?;
                    }
                }
            }
        }
        Err(DriverError::Timeout)
    }

    /// Expose the inner driver (e.g. to install ISR)
    pub fn driver_mut(&mut self) -> &mut UartDriver<H> {
        &mut self.driver
    }
}

/// Allow using `write!()` macro with UartApp
impl<H: UartHal> Write for UartApp<H> {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let n = self.driver.write(s.as_bytes());
        if n == s.len() { Ok(()) } else { Err(core::fmt::Error) }
    }
}
```

### 5. Mock HAL for Unit Testing (`src/mock_hal.rs`)

```rust
// src/mock_hal.rs — Testable mock HAL (runs on host, no hardware needed)

use crate::hal::{HalError, UartConfig, UartHal};
use std::collections::VecDeque;

pub struct MockUart {
    pub tx_bytes:    Vec<u8>,        // bytes "transmitted"
    pub rx_inject:   VecDeque<u8>,   // bytes to "receive" from hardware
    pub rx_errors:   VecDeque<HalError>,
    rx_irq_enabled:  bool,
    tx_irq_enabled:  bool,
}

impl MockUart {
    pub fn new() -> Self {
        Self {
            tx_bytes: Vec::new(),
            rx_inject: VecDeque::new(),
            rx_errors: VecDeque::new(),
            rx_irq_enabled: false,
            tx_irq_enabled: false,
        }
    }

    pub fn inject_byte(&mut self, byte: u8) {
        self.rx_inject.push_back(byte);
    }
}

impl UartHal for MockUart {
    fn configure(&mut self, _config: &UartConfig) -> Result<(), HalError> { Ok(()) }
    fn enable(&mut self)  {}
    fn disable(&mut self) {}

    fn write_byte(&mut self, byte: u8) -> nb::Result<(), HalError> {
        self.tx_bytes.push(byte);
        Ok(())
    }

    fn read_byte(&mut self) -> nb::Result<u8, HalError> {
        if let Some(err) = self.rx_errors.pop_front() {
            return Err(nb::Error::Other(err));
        }
        self.rx_inject.pop_front()
            .ok_or(nb::Error::WouldBlock)
    }

    fn is_tx_empty(&self) -> bool  { true  }
    fn is_rx_ready(&self) -> bool  { !self.rx_inject.is_empty() || !self.rx_errors.is_empty() }

    fn enable_rx_interrupt(&mut self)  { self.rx_irq_enabled = true; }
    fn disable_rx_interrupt(&mut self) { self.rx_irq_enabled = false; }
    fn enable_tx_interrupt(&mut self)  { self.tx_irq_enabled = true; }
    fn disable_tx_interrupt(&mut self) { self.tx_irq_enabled = false; }

    fn clear_errors(&mut self) { self.rx_errors.clear(); }
}

// ─── Unit Tests ───────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::driver::UartDriver;
    use crate::ring_buffer::RingBuffer;
    use crate::hal::UartConfig;

    // Static queues (required for 'static lifetime)
    static mut TX_QUEUE: RingBuffer = RingBuffer::new();
    static mut RX_QUEUE: RingBuffer = RingBuffer::new();

    #[test]
    fn test_write_transmits_bytes() {
        let mock = MockUart::new();
        let mut driver = UartDriver::new(mock);

        // SAFETY: single-threaded test
        let (tx_prod, tx_cons) = unsafe { TX_QUEUE.split() };
        let (rx_prod, rx_cons) = unsafe { RX_QUEUE.split() };

        driver.init(&UartConfig::default(),
                    (tx_prod, tx_cons),
                    (rx_prod, rx_cons)).unwrap();

        let n = driver.write(b"Hello");
        assert_eq!(n, 5);
    }

    #[test]
    fn test_rx_via_interrupt() {
        let mut mock = MockUart::new();
        mock.inject_byte(b'A');
        mock.inject_byte(b'B');

        let mut driver = UartDriver::new(mock);
        let (tx_prod, tx_cons) = unsafe { TX_QUEUE.split() };
        let (rx_prod, rx_cons) = unsafe { RX_QUEUE.split() };
        driver.init(&UartConfig::default(),
                    (tx_prod, tx_cons),
                    (rx_prod, rx_cons)).unwrap();

        // Simulate two ISR calls
        driver.on_interrupt();
        driver.on_interrupt();

        let mut buf = [0u8; 4];
        let n = driver.read(&mut buf);
        assert_eq!(n, 2);
        assert_eq!(&buf[..2], b"AB");
    }
}
```

### 6. Embedded Application Entry (`src/main.rs`)

```rust
// src/main.rs — Embedded entry point (no_std)

#![no_std]
#![no_main]

use core::fmt::Write;
use cortex_m_rt::entry;

mod hal;
mod ring_buffer;
mod driver;
mod app;
// mod stm32_hal;  // Platform-specific HAL implementation (not shown here)

use ring_buffer::RingBuffer;
use driver::UartDriver;
use app::UartApp;
use hal::UartConfig;

// Static ring buffers live for the entire program lifetime
static mut TX_BUF: RingBuffer = RingBuffer::new();
static mut RX_BUF: RingBuffer = RingBuffer::new();

// Declare driver in static storage for ISR access
// In a real project: use a Mutex<RefCell<Option<UartDriver>>> or RTIC resources
static mut UART_DRIVER: Option<UartDriver<stm32_hal::Stm32Uart>> = None;

#[entry]
fn main() -> ! {
    // Platform init: clocks, GPIO, etc. (omitted for brevity)
    // let hw = stm32_hal::Stm32Uart::new(USART1_BASE, APB2_CLOCK);

    // Split static buffers
    let (tx_p, tx_c) = unsafe { TX_BUF.split() };
    let (rx_p, rx_c) = unsafe { RX_BUF.split() };

    // let mut driver = UartDriver::new(hw);
    // driver.init(&UartConfig::default(), (tx_p, tx_c), (rx_p, rx_c)).unwrap();
    // unsafe { UART_DRIVER = Some(driver); }

    // Wrap in App layer
    // let mut uart = UartApp::new(unsafe { UART_DRIVER.take().unwrap() });
    // writeln!(uart, "UART Driver Architecture (Rust) — Ready").ok();

    loop {
        // Application logic here
        cortex_m::asm::wfi();  // Wait for interrupt
    }
}

// ISR vector
#[cortex_m_rt::interrupt]
fn USART1() {
    unsafe {
        if let Some(drv) = UART_DRIVER.as_mut() {
            drv.on_interrupt();
        }
    }
}
```

---

## Summary

### Architecture Recap

A well-designed UART driver separates concerns across four distinct layers:

**HAL (Hardware Abstraction Layer)** contains all MCU-specific register access — baud rate calculation, CR/SR bit manipulation, NVIC configuration. It exposes a thin, stable interface upward and can be swapped entirely when porting to new hardware. In C, this is a struct of function pointers or a set of inline functions; in Rust, it is a trait implementation.

**Driver Layer** implements the stateful UART logic: interrupt-safe ring buffers (SPSC for lock-free ISR access), an ISR handler that feeds/drains the hardware FIFO, TX kickoff when new data is enqueued, and error tracking. This layer is entirely MCU-agnostic — it depends only on the HAL trait/interface.

**Application Interface Layer** provides ergonomic APIs: `print`, `println`, `printf`/`write!`, `readline`. It hides all buffering and interrupt mechanics. In Rust, implementing `core::fmt::Write` allows the standard `write!` and `writeln!` macros to work seamlessly.

**Application Layer** calls only the application interface. It never touches register addresses, never manipulates IRQ flags, and never directly accesses ring buffers. The hardware could be replaced transparently.

### Key Design Decisions

| Decision | Rationale |
|---|---|
| SPSC ring buffer | Lock-free: ISR (producer) and app (consumer) can operate concurrently without disabling interrupts for every access |
| TX interrupt on-demand | Enable TX IRQ only when data is available; disable when buffer drained — avoids spurious interrupts |
| `nb::Result` in Rust | Non-blocking HAL API using `WouldBlock` idiom — composable with `block!` macro for synchronous use |
| Static lifetimes for queues | Required on `no_std` targets without heap allocation; ensures buffers outlive all borrows |
| Mock HAL for tests | Driver logic can be fully unit-tested on a host without hardware — dramatically faster feedback loop |
| Error flags, not panics | Embedded systems must never crash silently; errors are surfaced via return codes or callbacks |

### Porting Checklist

When bringing this architecture to a new MCU:

1. Implement `uart_hal_init()` (C) or `impl UartHal for YourMcuUart` (Rust) for the new register map
2. Connect the peripheral IRQ vector to `uart_driver_irq_handler()` / `drv.on_interrupt()`
3. Provide `platform_get_tick_ms()` for timeout support (usually a SysTick counter)
4. Provide `platform_enter_critical()` / `platform_exit_critical()` if not using SPSC atomics
5. Update GPIO and clock configuration in the HAL init function
6. All driver and application code remains unchanged

This architecture scales from a bare Cortex-M0 with a single UART to a multi-core SoC with four UART instances, RTOS task isolation, and DMA, simply by extending the HAL layer and driver configuration — the application code never changes.

---

*Document generated for the UART Driver Architecture series — Topic 11 of the Embedded UART Programming Guide.*