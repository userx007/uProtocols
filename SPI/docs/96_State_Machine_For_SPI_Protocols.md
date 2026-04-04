# 96. State Machine for SPI Protocols

- **SPI fundamentals** — the four-wire bus, transaction sequence, and why multi-phase protocols demand structure
- **Why state machines** — the failure modes of unstructured code vs. the clarity, testability, and error-handling benefits of SM designs
- **Design patterns** — table-driven, switch-case, and function-pointer/jump-table approaches

**C/C++ examples:**
1. A generic EEPROM read/write SM with a clean HAL abstraction and header/source split
2. An advanced W25Qxx NOR flash SM in C++ using a class with a jump-table executor, covering write-enable → erase → poll-busy → page-program → poll-busy chains
3. An interrupt-driven SM wired directly to a SPI ISR for zero-latency byte handling

**Rust examples:**
1. A **typestate** SPI SM where invalid state transitions are compile-time errors — you literally cannot call `send_address()` before `send_command()`
2. An **async Embassy** SM for W25Qxx that yields to the executor during busy-polling, enabling concurrency without threads

**Real-world:** A BME280 sensor SM covering power-on reset, chip ID check, calibration reads, configuration, forced-mode triggering, and compensation math.

**Error handling:** Recovery state patterns with retry counters, bus reset sequences, and fatal error escalation.

**Testing:** Mock-bus unit tests in Rust that verify correct byte sequences without any hardware.

> **Implementing complex SPI device protocols using state machines**

---

## Table of Contents

1. [Introduction](#introduction)
2. [SPI Protocol Fundamentals](#spi-protocol-fundamentals)
3. [Why State Machines for SPI?](#why-state-machines-for-spi)
4. [State Machine Design Patterns](#state-machine-design-patterns)
5. [C/C++ Implementation](#cc-implementation)
   - [Basic SPI State Machine in C](#basic-spi-state-machine-in-c)
   - [Advanced SPI Flash Memory Protocol (C++)](#advanced-spi-flash-memory-protocol-c)
   - [Interrupt-Driven SPI State Machine (C)](#interrupt-driven-spi-state-machine-c)
6. [Rust Implementation](#rust-implementation)
   - [Basic SPI State Machine in Rust](#basic-spi-state-machine-in-rust)
   - [Async SPI State Machine with Embassy (Rust)](#async-spi-state-machine-with-embassy-rust)
7. [Real-World Example: BME280 Sensor Protocol](#real-world-example-bme280-sensor-protocol)
8. [Error Handling and Recovery States](#error-handling-and-recovery-states)
9. [Testing State Machines](#testing-state-machines)
10. [Summary](#summary)

---

## Introduction

SPI (Serial Peripheral Interface) is a synchronous serial communication protocol widely used in embedded systems to connect microcontrollers to peripheral devices such as flash memory, sensors, display controllers, ADCs, and more. While basic SPI transfers are straightforward, real-world device protocols are often multi-phase, stateful transactions involving command bytes, address bytes, dummy cycles, data payloads, and precise timing — a level of complexity that demands a disciplined architectural approach.

**State machines** provide exactly that discipline. By modeling the SPI communication flow as a finite set of discrete states and the transitions between them, developers gain clarity, testability, and maintainability even for deeply complex protocols. This document explores the theory, design patterns, and practical implementations of SPI protocol state machines in both C/C++ and Rust.

---

## SPI Protocol Fundamentals

SPI uses four signals:

| Signal | Direction | Purpose |
|--------|-----------|---------|
| **SCLK** | Master → Slave | Clock signal |
| **MOSI** | Master → Slave | Master Out Slave In (data) |
| **MISO** | Slave → Master | Master In Slave Out (data) |
| **CS/SS** | Master → Slave | Chip Select / Slave Select (active low) |

A typical SPI transaction follows this sequence:

1. Assert CS low
2. Transmit command byte(s) on MOSI
3. Optionally transmit address byte(s)
4. Optional dummy/wait cycles
5. Exchange data (read and/or write)
6. De-assert CS high

The exact protocol varies per device. For example, an SPI NOR flash requires a **Write Enable** command before every write, an **Erase** command with a 24-bit address, and a polling loop to detect operation completion — a multi-state sequence that is difficult to manage without a state machine.

---

## Why State Machines for SPI?

### Problems without state machines

Without structured state management, SPI protocol code tends to:

- Use deeply nested `if`/`else` or `switch` chains
- Mix protocol logic with hardware abstraction
- Make error recovery hard to reason about
- Be difficult to test in isolation
- Break silently when timing constraints are violated

### Benefits of state machines

- **Explicit state**: Every phase of the protocol is named and visible
- **Clear transitions**: Conditions to move between states are explicit
- **Error states**: Timeouts and failures become first-class citizens
- **Testability**: States and transitions can be unit-tested without hardware
- **Non-blocking designs**: State machines pair naturally with interrupt-driven or async SPI

---

## State Machine Design Patterns

Three common patterns appear in embedded SPI state machine implementations:

### 1. Table-driven state machine

A 2D table maps `(current_state, event) → (next_state, action)`. Clean and data-oriented but verbose to define.

### 2. Switch-case state machine

A `switch` on the current state, with case logic handling events and calling transition functions. The most common embedded pattern — readable and efficient.

### 3. Function-pointer / jump-table state machine

Each state is a function. The "current state" is a function pointer. Transitions are assignments. Eliminates `switch` overhead and is easily extended.

The examples in this document use the **switch-case** and **function-pointer** patterns, as they are most idiomatic in C/C++ and Rust respectively.

---

## C/C++ Implementation

### Basic SPI State Machine in C

This example models a generic SPI EEPROM read protocol with states for idle, command, address, and data phases.

```c
// spi_eeprom_sm.h
#ifndef SPI_EEPROM_SM_H
#define SPI_EEPROM_SM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// SPI EEPROM opcodes (e.g., AT25xxx series)
#define EEPROM_OP_READ   0x03
#define EEPROM_OP_WRITE  0x02
#define EEPROM_OP_WREN   0x06  // Write Enable
#define EEPROM_OP_RDSR   0x05  // Read Status Register

// State enumeration — every protocol phase is named
typedef enum {
    SPI_SM_IDLE = 0,
    SPI_SM_ASSERT_CS,
    SPI_SM_SEND_CMD,
    SPI_SM_SEND_ADDR_HIGH,
    SPI_SM_SEND_ADDR_MID,
    SPI_SM_SEND_ADDR_LOW,
    SPI_SM_TRANSFER_DATA,
    SPI_SM_DEASSERT_CS,
    SPI_SM_COMPLETE,
    SPI_SM_ERROR,
    SPI_SM_STATE_COUNT
} spi_sm_state_t;

// Events that drive transitions
typedef enum {
    SPI_EVT_START_READ,
    SPI_EVT_START_WRITE,
    SPI_EVT_TX_COMPLETE,   // Hardware signals byte transmitted
    SPI_EVT_RX_READY,      // Hardware signals byte received
    SPI_EVT_TIMEOUT,
    SPI_EVT_ABORT
} spi_sm_event_t;

// State machine context — all mutable state lives here
typedef struct {
    spi_sm_state_t  state;
    uint8_t         opcode;
    uint32_t        address;        // 24-bit device address
    uint8_t        *data_buf;
    size_t          data_len;
    size_t          data_idx;
    bool            is_write;
    uint32_t        timeout_ms;
    uint32_t        elapsed_ms;
    void           *hw_ctx;         // Opaque hardware context (HAL handle)
} spi_sm_ctx_t;

// HAL callbacks — platform must implement these
typedef struct {
    void (*cs_assert)(void *hw_ctx);
    void (*cs_deassert)(void *hw_ctx);
    void (*tx_byte)(void *hw_ctx, uint8_t byte);
    uint8_t (*rx_byte)(void *hw_ctx);
    uint32_t (*get_tick_ms)(void);
} spi_hal_t;

void spi_sm_init(spi_sm_ctx_t *ctx, void *hw_ctx);
void spi_sm_process(spi_sm_ctx_t *ctx, const spi_hal_t *hal, spi_sm_event_t event);
bool spi_sm_start_read(spi_sm_ctx_t *ctx, uint32_t addr, uint8_t *buf, size_t len);
bool spi_sm_start_write(spi_sm_ctx_t *ctx, uint32_t addr, const uint8_t *buf, size_t len);
spi_sm_state_t spi_sm_get_state(const spi_sm_ctx_t *ctx);

#endif // SPI_EEPROM_SM_H
```

```c
// spi_eeprom_sm.c
#include "spi_eeprom_sm.h"
#include <string.h>

#define SPI_SM_TIMEOUT_MS 500

void spi_sm_init(spi_sm_ctx_t *ctx, void *hw_ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state  = SPI_SM_IDLE;
    ctx->hw_ctx = hw_ctx;
}

spi_sm_state_t spi_sm_get_state(const spi_sm_ctx_t *ctx) {
    return ctx->state;
}

bool spi_sm_start_read(spi_sm_ctx_t *ctx, uint32_t addr,
                       uint8_t *buf, size_t len) {
    if (ctx->state != SPI_SM_IDLE) return false;
    ctx->opcode   = EEPROM_OP_READ;
    ctx->address  = addr;
    ctx->data_buf = buf;
    ctx->data_len = len;
    ctx->data_idx = 0;
    ctx->is_write = false;
    return true;
}

bool spi_sm_start_write(spi_sm_ctx_t *ctx, uint32_t addr,
                        const uint8_t *buf, size_t len) {
    if (ctx->state != SPI_SM_IDLE) return false;
    ctx->opcode   = EEPROM_OP_WRITE;
    ctx->address  = addr;
    ctx->data_buf = (uint8_t *)buf;  // safe: write path reads it only
    ctx->data_len = len;
    ctx->data_idx = 0;
    ctx->is_write = true;
    return true;
}

// Core state machine — driven by events from ISR or polling loop
void spi_sm_process(spi_sm_ctx_t *ctx, const spi_hal_t *hal,
                    spi_sm_event_t event) {

    // Global timeout guard — any state can time out
    if (event == SPI_EVT_TIMEOUT || event == SPI_EVT_ABORT) {
        hal->cs_deassert(ctx->hw_ctx);
        ctx->state = (event == SPI_EVT_TIMEOUT) ? SPI_SM_ERROR : SPI_SM_IDLE;
        return;
    }

    switch (ctx->state) {

        // ---------------------------------------------------------------
        case SPI_SM_IDLE:
            if (event == SPI_EVT_START_READ || event == SPI_EVT_START_WRITE) {
                ctx->elapsed_ms = hal->get_tick_ms();
                ctx->state = SPI_SM_ASSERT_CS;
                // Fall through immediately — no hardware action needed yet
                // Real HW: schedule next tick or raise a software event
            }
            break;

        // ---------------------------------------------------------------
        case SPI_SM_ASSERT_CS:
            hal->cs_assert(ctx->hw_ctx);
            ctx->state = SPI_SM_SEND_CMD;
            hal->tx_byte(ctx->hw_ctx, ctx->opcode);
            break;

        // ---------------------------------------------------------------
        case SPI_SM_SEND_CMD:
            if (event == SPI_EVT_TX_COMPLETE) {
                ctx->state = SPI_SM_SEND_ADDR_HIGH;
                hal->tx_byte(ctx->hw_ctx, (ctx->address >> 16) & 0xFF);
            }
            break;

        // ---------------------------------------------------------------
        case SPI_SM_SEND_ADDR_HIGH:
            if (event == SPI_EVT_TX_COMPLETE) {
                ctx->state = SPI_SM_SEND_ADDR_MID;
                hal->tx_byte(ctx->hw_ctx, (ctx->address >> 8) & 0xFF);
            }
            break;

        // ---------------------------------------------------------------
        case SPI_SM_SEND_ADDR_MID:
            if (event == SPI_EVT_TX_COMPLETE) {
                ctx->state = SPI_SM_SEND_ADDR_LOW;
                hal->tx_byte(ctx->hw_ctx, ctx->address & 0xFF);
            }
            break;

        // ---------------------------------------------------------------
        case SPI_SM_SEND_ADDR_LOW:
            if (event == SPI_EVT_TX_COMPLETE) {
                ctx->state = SPI_SM_TRANSFER_DATA;
                if (ctx->is_write) {
                    hal->tx_byte(ctx->hw_ctx, ctx->data_buf[ctx->data_idx]);
                } else {
                    hal->tx_byte(ctx->hw_ctx, 0xFF); // clock in first read byte
                }
            }
            break;

        // ---------------------------------------------------------------
        case SPI_SM_TRANSFER_DATA:
            if (ctx->is_write && event == SPI_EVT_TX_COMPLETE) {
                ctx->data_idx++;
                if (ctx->data_idx < ctx->data_len) {
                    hal->tx_byte(ctx->hw_ctx, ctx->data_buf[ctx->data_idx]);
                } else {
                    ctx->state = SPI_SM_DEASSERT_CS;
                    hal->cs_deassert(ctx->hw_ctx);
                }
            } else if (!ctx->is_write && event == SPI_EVT_RX_READY) {
                ctx->data_buf[ctx->data_idx++] = hal->rx_byte(ctx->hw_ctx);
                if (ctx->data_idx < ctx->data_len) {
                    hal->tx_byte(ctx->hw_ctx, 0xFF); // clock next byte
                } else {
                    ctx->state = SPI_SM_DEASSERT_CS;
                    hal->cs_deassert(ctx->hw_ctx);
                }
            }
            break;

        // ---------------------------------------------------------------
        case SPI_SM_DEASSERT_CS:
            // CS already de-asserted; wait one tick then signal complete
            ctx->state = SPI_SM_COMPLETE;
            break;

        // ---------------------------------------------------------------
        case SPI_SM_COMPLETE:
        case SPI_SM_ERROR:
            // Caller must reset state machine via spi_sm_init() or
            // transition back to IDLE after inspecting state
            break;

        default:
            ctx->state = SPI_SM_ERROR;
            break;
    }
}
```

---

### Advanced SPI Flash Memory Protocol (C++)

This C++ example uses a **class-based state machine with a function-pointer jump table** to model a W25Qxx NOR flash erase-write sequence, which requires Write Enable, Page Program, and status polling states.

```cpp
// spi_flash_sm.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <array>

// W25Qxx NOR Flash opcodes
enum class FlashOp : uint8_t {
    WriteEnable  = 0x06,
    WriteDisable = 0x04,
    ReadStatus1  = 0x05,
    PageProgram  = 0x02,
    SectorErase  = 0x20,
    ReadData     = 0x03,
    ChipErase    = 0xC7,
};

// Protocol states — explicit, named, exhaustive
enum class FlashState {
    Idle,
    WriteEnable,
    WaitWriteEnable,
    SectorErase,
    WaitEraseBusy,
    WriteEnableForProgram,
    WaitWriteEnableForProgram,
    PageProgram,
    WaitProgramBusy,
    ReadStatus,
    Complete,
    Error
};

// Hardware abstraction layer (pure virtual interface)
struct ISpiBus {
    virtual ~ISpiBus() = default;
    virtual void csAssert()                                   = 0;
    virtual void csDeassert()                                 = 0;
    virtual void transfer(const uint8_t *tx, uint8_t *rx, size_t len) = 0;
};

// Callback for operation completion
using FlashCallback = std::function<void(bool success)>;

class SpiFlashSM {
public:
    static constexpr uint32_t PAGE_SIZE        = 256;
    static constexpr uint32_t SECTOR_SIZE      = 4096;
    static constexpr uint32_t BUSY_POLL_MS     = 1;
    static constexpr uint32_t ERASE_TIMEOUT_MS = 400;
    static constexpr uint32_t PROG_TIMEOUT_MS  = 3;

    explicit SpiFlashSM(ISpiBus &bus) : bus_(bus) {}

    // Initiate a sector-erase + page-program sequence
    bool startEraseThenWrite(uint32_t address, const uint8_t *data,
                              size_t len, FlashCallback cb) {
        if (state_ != FlashState::Idle) return false;
        address_      = address & ~(SECTOR_SIZE - 1); // align to sector
        write_data_   = data;
        write_len_    = len;
        write_offset_ = 0;
        callback_     = std::move(cb);
        transition(FlashState::WriteEnable);
        executeState();
        return true;
    }

    // Call this from a timer ISR or RTOS tick — drives the polling loop
    void tick(uint32_t now_ms) {
        tick_ms_ = now_ms;
        if (state_ == FlashState::WaitEraseBusy ||
            state_ == FlashState::WaitProgramBusy) {
            if (now_ms - last_poll_ms_ >= BUSY_POLL_MS) {
                last_poll_ms_ = now_ms;
                executeState();
            }
        }
    }

    FlashState getState() const { return state_; }

private:
    ISpiBus          &bus_;
    FlashState        state_      = FlashState::Idle;
    uint32_t          address_    = 0;
    const uint8_t    *write_data_ = nullptr;
    size_t            write_len_  = 0;
    size_t            write_offset_ = 0;
    FlashCallback     callback_;
    uint32_t          tick_ms_      = 0;
    uint32_t          last_poll_ms_ = 0;
    uint32_t          op_start_ms_  = 0;

    void transition(FlashState next) {
        state_ = next;
        op_start_ms_ = tick_ms_;
    }

    // Check the WIP (Write In Progress) bit in Status Register 1
    bool isDeviceBusy() {
        uint8_t tx[2] = { static_cast<uint8_t>(FlashOp::ReadStatus1), 0xFF };
        uint8_t rx[2] = {};
        bus_.csAssert();
        bus_.transfer(tx, rx, 2);
        bus_.csDeassert();
        return (rx[1] & 0x01) != 0;  // WIP bit
    }

    void sendWriteEnable() {
        uint8_t cmd = static_cast<uint8_t>(FlashOp::WriteEnable);
        bus_.csAssert();
        bus_.transfer(&cmd, nullptr, 1);
        bus_.csDeassert();
    }

    void sendSectorErase(uint32_t addr) {
        uint8_t cmd[4] = {
            static_cast<uint8_t>(FlashOp::SectorErase),
            static_cast<uint8_t>((addr >> 16) & 0xFF),
            static_cast<uint8_t>((addr >>  8) & 0xFF),
            static_cast<uint8_t>( addr        & 0xFF)
        };
        bus_.csAssert();
        bus_.transfer(cmd, nullptr, 4);
        bus_.csDeassert();
    }

    void sendPageProgram(uint32_t addr, const uint8_t *data, size_t len) {
        // Max 256 bytes per Page Program command
        uint8_t header[4] = {
            static_cast<uint8_t>(FlashOp::PageProgram),
            static_cast<uint8_t>((addr >> 16) & 0xFF),
            static_cast<uint8_t>((addr >>  8) & 0xFF),
            static_cast<uint8_t>( addr        & 0xFF)
        };
        bus_.csAssert();
        bus_.transfer(header, nullptr, 4);
        bus_.transfer(data, nullptr, len);
        bus_.csDeassert();
    }

    // The core state machine executor
    void executeState() {
        switch (state_) {

            case FlashState::WriteEnable:
                sendWriteEnable();
                transition(FlashState::WaitWriteEnable);
                break;

            case FlashState::WaitWriteEnable:
                if (!isDeviceBusy()) {
                    transition(FlashState::SectorErase);
                    executeState();  // no delay needed, proceed immediately
                }
                break;

            case FlashState::SectorErase:
                sendSectorErase(address_);
                transition(FlashState::WaitEraseBusy);
                break;

            case FlashState::WaitEraseBusy:
                if (tick_ms_ - op_start_ms_ > ERASE_TIMEOUT_MS) {
                    finalize(false);
                    return;
                }
                if (!isDeviceBusy()) {
                    transition(FlashState::WriteEnableForProgram);
                    executeState();
                }
                break;

            case FlashState::WriteEnableForProgram:
                sendWriteEnable();
                transition(FlashState::WaitWriteEnableForProgram);
                break;

            case FlashState::WaitWriteEnableForProgram:
                if (!isDeviceBusy()) {
                    transition(FlashState::PageProgram);
                    executeState();
                }
                break;

            case FlashState::PageProgram: {
                size_t chunk = write_len_ - write_offset_;
                if (chunk > PAGE_SIZE) chunk = PAGE_SIZE;
                sendPageProgram(address_ + write_offset_,
                                write_data_ + write_offset_, chunk);
                write_offset_ += chunk;
                transition(FlashState::WaitProgramBusy);
                break;
            }

            case FlashState::WaitProgramBusy:
                if (tick_ms_ - op_start_ms_ > PROG_TIMEOUT_MS) {
                    finalize(false);
                    return;
                }
                if (!isDeviceBusy()) {
                    if (write_offset_ < write_len_) {
                        // More pages to write
                        transition(FlashState::WriteEnableForProgram);
                        executeState();
                    } else {
                        finalize(true);
                    }
                }
                break;

            case FlashState::Complete:
            case FlashState::Error:
                break;  // terminal states — caller checks getState()

            default:
                finalize(false);
                break;
        }
    }

    void finalize(bool success) {
        transition(success ? FlashState::Complete : FlashState::Error);
        if (callback_) {
            callback_(success);
            callback_ = nullptr;
        }
    }
};
```

---

### Interrupt-Driven SPI State Machine (C)

This example shows how to couple a state machine to SPI hardware interrupts on a bare-metal microcontroller (STM32-style HAL).

```c
// spi_isr_sm.c — interrupt-driven SPI state machine
#include <stdint.h>
#include <stdbool.h>

// Simplified register-level SPI peripheral (STM32-like)
typedef struct {
    volatile uint32_t CR1;   // Control register 1
    volatile uint32_t CR2;   // Control register 2
    volatile uint32_t SR;    // Status register
    volatile uint32_t DR;    // Data register
} SPI_TypeDef;

#define SPI_SR_TXE   (1U << 1)  // TX buffer empty
#define SPI_SR_RXNE  (1U << 0)  // RX buffer not empty
#define SPI_SR_BSY   (1U << 7)  // Busy flag
#define SPI_CR2_TXEIE  (1U << 7)
#define SPI_CR2_RXNEIE (1U << 6)

// GPIO macros (platform-specific)
#define CS_LOW()   (GPIOA->BSRR = (1U << (4 + 16)))
#define CS_HIGH()  (GPIOA->BSRR = (1U << 4))

// ---------------------------------------------------------------------------
// State machine types

typedef enum {
    ISR_SM_IDLE = 0,
    ISR_SM_TX_CMD,
    ISR_SM_TX_ADDR,
    ISR_SM_RX_DATA,
    ISR_SM_CLEANUP,
    ISR_SM_DONE,
    ISR_SM_ERROR
} isr_sm_state_t;

typedef struct {
    isr_sm_state_t   state;
    uint8_t          cmd;
    uint16_t         addr;
    uint8_t         *rx_buf;
    uint8_t          rx_len;
    uint8_t          rx_idx;
    uint8_t          phase;     // sub-step within a state
    volatile bool    done;
    volatile bool    error;
} isr_sm_t;

static isr_sm_t g_sm;
static SPI_TypeDef *const SPI1 = (SPI_TypeDef *)0x40013000; // example address

// ---------------------------------------------------------------------------
// Public API

void spi_sm_start(uint8_t cmd, uint16_t addr,
                  uint8_t *rx_buf, uint8_t rx_len) {
    g_sm.state   = ISR_SM_TX_CMD;
    g_sm.cmd     = cmd;
    g_sm.addr    = addr;
    g_sm.rx_buf  = rx_buf;
    g_sm.rx_len  = rx_len;
    g_sm.rx_idx  = 0;
    g_sm.phase   = 0;
    g_sm.done    = false;
    g_sm.error   = false;

    CS_LOW();

    // Enable TXE interrupt to kick off the first byte
    SPI1->CR2 |= SPI_CR2_TXEIE;
}

bool spi_sm_is_done(void)  { return g_sm.done;  }
bool spi_sm_has_error(void){ return g_sm.error; }

// ---------------------------------------------------------------------------
// SPI Interrupt Service Routine — state machine lives here

void SPI1_IRQHandler(void) {
    uint32_t sr = SPI1->SR;

    switch (g_sm.state) {

        // -------------------------------------------------------------------
        case ISR_SM_TX_CMD:
            if (sr & SPI_SR_TXE) {
                SPI1->DR = g_sm.cmd;
                g_sm.state = ISR_SM_TX_ADDR;
                g_sm.phase = 0;
                // Keep TXE interrupt enabled
            }
            break;

        // -------------------------------------------------------------------
        case ISR_SM_TX_ADDR:
            if (sr & SPI_SR_TXE) {
                if (g_sm.phase == 0) {
                    SPI1->DR = (g_sm.addr >> 8) & 0xFF; // MSB
                    g_sm.phase = 1;
                } else {
                    SPI1->DR = g_sm.addr & 0xFF;         // LSB
                    // Switch to RX mode after this byte drains
                    SPI1->CR2 &= ~SPI_CR2_TXEIE;
                    SPI1->CR2 |=  SPI_CR2_RXNEIE;
                    g_sm.state = ISR_SM_RX_DATA;
                    // Send dummy byte to clock in first RX byte
                    // (must wait until TX shift register is empty first)
                }
            }
            break;

        // -------------------------------------------------------------------
        case ISR_SM_RX_DATA:
            if (sr & SPI_SR_RXNE) {
                // Discard echoed bytes from CMD/ADDR phase if needed
                // Here we assume full-duplex and we only need the data bytes
                uint8_t b = (uint8_t)SPI1->DR;
                if (g_sm.rx_idx < g_sm.rx_len) {
                    g_sm.rx_buf[g_sm.rx_idx++] = b;
                }

                if (g_sm.rx_idx < g_sm.rx_len) {
                    SPI1->DR = 0xFF; // clock next byte
                } else {
                    // All bytes received — wait for bus not busy
                    SPI1->CR2 &= ~SPI_CR2_RXNEIE;
                    g_sm.state = ISR_SM_CLEANUP;
                    // Poll BSY flag in main loop or use a timer
                }
            }
            break;

        // -------------------------------------------------------------------
        case ISR_SM_CLEANUP:
            // Called once BSY clears (can also be polled in main loop)
            if (!(sr & SPI_SR_BSY)) {
                CS_HIGH();
                g_sm.state = ISR_SM_DONE;
                g_sm.done  = true;
            }
            break;

        default:
            SPI1->CR2 &= ~(SPI_CR2_TXEIE | SPI_CR2_RXNEIE);
            CS_HIGH();
            g_sm.state = ISR_SM_ERROR;
            g_sm.error = true;
            break;
    }
}
```

---

## Rust Implementation

### Basic SPI State Machine in Rust

Rust's type system makes state machines particularly powerful — **invalid state transitions become compile-time errors** using the typestate pattern.

```rust
// spi_sm.rs — Typestate-based SPI state machine in Rust

use std::marker::PhantomData;

// ─── Protocol states as zero-sized types ────────────────────────────────────
pub struct Idle;
pub struct AssertCs;
pub struct SendCommand;
pub struct SendAddress;
pub struct TransferData;
pub struct DeassertCs;
pub struct Complete;

// ─── Typestate state machine ─────────────────────────────────────────────────
pub struct SpiSm<State, Bus: SpiBus> {
    bus:     Bus,
    opcode:  u8,
    address: u32,
    buf:     Vec<u8>,
    idx:     usize,
    _state:  PhantomData<State>,
}

// ─── Hardware abstraction trait ───────────────────────────────────────────────
pub trait SpiBus {
    type Error;
    fn cs_assert(&mut self)   -> Result<(), Self::Error>;
    fn cs_deassert(&mut self) -> Result<(), Self::Error>;
    fn transfer(&mut self, tx: u8) -> Result<u8, Self::Error>;
}

// ─── Idle → AssertCs ─────────────────────────────────────────────────────────
impl<Bus: SpiBus> SpiSm<Idle, Bus> {
    pub fn new(bus: Bus) -> Self {
        SpiSm {
            bus,
            opcode: 0,
            address: 0,
            buf: Vec::new(),
            idx: 0,
            _state: PhantomData,
        }
    }

    /// Configure a read transaction and assert CS.
    /// Transition is only possible from `Idle` — enforced at compile time.
    pub fn begin_read(
        mut self,
        opcode: u8,
        address: u32,
        len: usize,
    ) -> Result<SpiSm<SendCommand, Bus>, Bus::Error> {
        self.bus.cs_assert()?;
        Ok(SpiSm {
            bus:     self.bus,
            opcode,
            address,
            buf:     vec![0u8; len],
            idx:     0,
            _state:  PhantomData,
        })
    }
}

// ─── SendCommand → SendAddress ────────────────────────────────────────────────
impl<Bus: SpiBus> SpiSm<SendCommand, Bus> {
    pub fn send_command(mut self) -> Result<SpiSm<SendAddress, Bus>, Bus::Error> {
        self.bus.transfer(self.opcode)?;
        Ok(SpiSm { _state: PhantomData, ..self })
    }
}

// ─── SendAddress → TransferData ───────────────────────────────────────────────
impl<Bus: SpiBus> SpiSm<SendAddress, Bus> {
    pub fn send_address(mut self) -> Result<SpiSm<TransferData, Bus>, Bus::Error> {
        // Send 24-bit address MSB-first
        self.bus.transfer(((self.address >> 16) & 0xFF) as u8)?;
        self.bus.transfer(((self.address >>  8) & 0xFF) as u8)?;
        self.bus.transfer(( self.address        & 0xFF) as u8)?;
        Ok(SpiSm { _state: PhantomData, ..self })
    }
}

// ─── TransferData → DeassertCs ────────────────────────────────────────────────
impl<Bus: SpiBus> SpiSm<TransferData, Bus> {
    pub fn transfer_all(mut self) -> Result<SpiSm<DeassertCs, Bus>, Bus::Error> {
        for byte in self.buf.iter_mut() {
            *byte = self.bus.transfer(0xFF)?; // clock in data
        }
        Ok(SpiSm { _state: PhantomData, ..self })
    }
}

// ─── DeassertCs → Complete ────────────────────────────────────────────────────
impl<Bus: SpiBus> SpiSm<DeassertCs, Bus> {
    pub fn finish(mut self) -> Result<SpiSm<Complete, Bus>, Bus::Error> {
        self.bus.cs_deassert()?;
        Ok(SpiSm { _state: PhantomData, ..self })
    }
}

// ─── Complete — extract results ───────────────────────────────────────────────
impl<Bus: SpiBus> SpiSm<Complete, Bus> {
    pub fn data(&self) -> &[u8] {
        &self.buf
    }

    /// Return the bus for reuse (back to Idle)
    pub fn release(self) -> Bus {
        self.bus
    }
}

// ─── Usage example ────────────────────────────────────────────────────────────
#[cfg(test)]
mod tests {
    use super::*;

    struct MockBus {
        pub tx_log: Vec<u8>,
        pub rx_seq: Vec<u8>,
        pub rx_idx: usize,
        pub cs_state: bool,
    }

    impl SpiBus for MockBus {
        type Error = ();
        fn cs_assert(&mut self)   -> Result<(), ()> { self.cs_state = true;  Ok(()) }
        fn cs_deassert(&mut self) -> Result<(), ()> { self.cs_state = false; Ok(()) }
        fn transfer(&mut self, tx: u8) -> Result<u8, ()> {
            self.tx_log.push(tx);
            let rx = self.rx_seq.get(self.rx_idx).copied().unwrap_or(0);
            self.rx_idx += 1;
            Ok(rx)
        }
    }

    #[test]
    fn test_read_sequence() {
        let bus = MockBus {
            tx_log: Vec::new(),
            rx_seq: vec![0xDE, 0xAD, 0xBE, 0xEF],
            rx_idx: 0,
            cs_state: false,
        };

        let sm = SpiSm::<Idle, _>::new(bus);

        // The typestate pattern guarantees this sequence at compile time.
        // You CANNOT call send_address() before send_command() — it won't compile.
        let result = sm
            .begin_read(0x03, 0x001000, 4)
            .unwrap()
            .send_command()
            .unwrap()
            .send_address()
            .unwrap()
            .transfer_all()
            .unwrap()
            .finish()
            .unwrap();

        assert_eq!(result.data(), &[0xDE, 0xAD, 0xBE, 0xEF]);
        println!("Read bytes: {:02X?}", result.data());
    }
}
```

---

### Async SPI State Machine with Embassy (Rust)

Embassy is the leading async embedded Rust framework. The following example demonstrates a non-blocking SPI state machine for a W25Qxx flash device using `async/await`.

```rust
// flash_sm_async.rs — Async SPI flash state machine using Embassy

#![no_std]
#![no_main]

use embassy_stm32::spi::{Config as SpiConfig, Spi};
use embassy_stm32::gpio::{Level, Output, Speed};
use embassy_time::{Duration, Timer};
use defmt::*;

// Flash opcodes
const OP_WREN: u8 = 0x06;
const OP_RDSR: u8 = 0x05;
const OP_READ: u8 = 0x03;
const OP_PP:   u8 = 0x02;  // Page Program
const OP_SE:   u8 = 0x20;  // Sector Erase

const BUSY_BIT: u8 = 0x01;

// Errors
#[derive(Debug, defmt::Format)]
pub enum FlashError {
    SpiError,
    Timeout,
    WriteProtected,
}

// The async state machine as a struct with methods.
// Embassy tasks own the SPI bus — no shared access issues.
pub struct AsyncFlashSm<'d, T: embassy_stm32::spi::Instance> {
    spi: Spi<'d, T>,
    cs:  Output<'d>,
}

impl<'d, T: embassy_stm32::spi::Instance> AsyncFlashSm<'d, T> {

    pub fn new(spi: Spi<'d, T>, cs: Output<'d>) -> Self {
        Self { spi, cs }
    }

    // ─── Low-level helpers ─────────────────────────────────────────────────

    fn cs_assert(&mut self)   { self.cs.set_low();  }
    fn cs_deassert(&mut self) { self.cs.set_high(); }

    async fn send(&mut self, data: &[u8]) -> Result<(), FlashError> {
        self.spi.write(data).await.map_err(|_| FlashError::SpiError)
    }

    async fn recv(&mut self, buf: &mut [u8]) -> Result<(), FlashError> {
        self.spi.read(buf).await.map_err(|_| FlashError::SpiError)
    }

    // ─── State: Send Write Enable ──────────────────────────────────────────
    async fn state_write_enable(&mut self) -> Result<(), FlashError> {
        self.cs_assert();
        self.send(&[OP_WREN]).await?;
        self.cs_deassert();
        Ok(())
    }

    // ─── State: Poll busy flag ─────────────────────────────────────────────
    async fn state_wait_not_busy(&mut self, timeout: Duration) -> Result<(), FlashError> {
        let start = embassy_time::Instant::now();
        loop {
            self.cs_assert();
            self.send(&[OP_RDSR]).await?;
            let mut sr = [0u8; 1];
            self.recv(&mut sr).await?;
            self.cs_deassert();

            if sr[0] & BUSY_BIT == 0 {
                return Ok(());
            }

            if start.elapsed() > timeout {
                return Err(FlashError::Timeout);
            }

            // Yield the executor — other tasks run while we wait
            Timer::after(Duration::from_millis(1)).await;
        }
    }

    // ─── State: Sector Erase ──────────────────────────────────────────────
    async fn state_sector_erase(&mut self, addr: u32) -> Result<(), FlashError> {
        self.cs_assert();
        self.send(&[
            OP_SE,
            ((addr >> 16) & 0xFF) as u8,
            ((addr >>  8) & 0xFF) as u8,
            ( addr        & 0xFF) as u8,
        ]).await?;
        self.cs_deassert();
        Ok(())
    }

    // ─── State: Page Program ──────────────────────────────────────────────
    async fn state_page_program(
        &mut self,
        addr: u32,
        data: &[u8],
    ) -> Result<(), FlashError> {
        let header = [
            OP_PP,
            ((addr >> 16) & 0xFF) as u8,
            ((addr >>  8) & 0xFF) as u8,
            ( addr        & 0xFF) as u8,
        ];
        self.cs_assert();
        self.send(&header).await?;
        self.send(data).await?;
        self.cs_deassert();
        Ok(())
    }

    // ─── High-level operation: Erase sector then write pages ──────────────
    //
    // This is the "state machine orchestrator" — each await point is an
    // implicit state transition. The async executor handles the scheduling.
    pub async fn erase_and_write(
        &mut self,
        sector_addr: u32,
        data: &[u8],
    ) -> Result<(), FlashError> {

        info!("[SM] State: WriteEnable (before erase)");
        self.state_write_enable().await?;

        info!("[SM] State: SectorErase @ {:#010x}", sector_addr);
        self.state_sector_erase(sector_addr).await?;

        info!("[SM] State: WaitEraseBusy");
        self.state_wait_not_busy(Duration::from_millis(400)).await?;

        // Write data in 256-byte pages
        let mut offset = 0usize;
        while offset < data.len() {
            let chunk_end = (offset + 256).min(data.len());
            let chunk = &data[offset..chunk_end];
            let page_addr = sector_addr + offset as u32;

            info!("[SM] State: WriteEnable (before page program)");
            self.state_write_enable().await?;

            info!("[SM] State: PageProgram @ {:#010x}, {} bytes", page_addr, chunk.len());
            self.state_page_program(page_addr, chunk).await?;

            info!("[SM] State: WaitProgramBusy");
            self.state_wait_not_busy(Duration::from_millis(5)).await?;

            offset = chunk_end;
        }

        info!("[SM] State: Complete");
        Ok(())
    }

    // ─── Read operation state machine ─────────────────────────────────────
    pub async fn read(
        &mut self,
        addr: u32,
        buf: &mut [u8],
    ) -> Result<(), FlashError> {
        info!("[SM] State: AssertCS + SendCommand (READ)");
        self.cs_assert();
        self.send(&[
            OP_READ,
            ((addr >> 16) & 0xFF) as u8,
            ((addr >>  8) & 0xFF) as u8,
            ( addr        & 0xFF) as u8,
        ]).await?;

        info!("[SM] State: TransferData ({} bytes)", buf.len());
        self.recv(buf).await?;

        info!("[SM] State: DeassertCS");
        self.cs_deassert();

        Ok(())
    }
}
```

---

## Real-World Example: BME280 Sensor Protocol

The BME280 is a common humidity/pressure/temperature sensor that uses a two-phase SPI protocol: a register read requires writing an address byte with bit 7 set, then reading the response. Its compensation data must be read and parsed before measurements are valid — a perfect state machine use case.

```c
// bme280_sm.c — BME280 initialization and burst-read state machine

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// BME280 register addresses
#define BME280_REG_ID       0xD0
#define BME280_REG_RESET    0xE0
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG   0xF5
#define BME280_REG_DATA     0xF7  // 8 bytes: press MSB/LSB/XLSB, temp MSB/LSB/XLSB, hum MSB/LSB
#define BME280_REG_CALIB00  0x88  // 26 bytes of trim data
#define BME280_REG_CALIB26  0xE1  // 7 more bytes

// SPI read: address with bit7 set; SPI write: address with bit7 clear
#define BME280_SPI_READ(reg)  ((reg) | 0x80)
#define BME280_SPI_WRITE(reg) ((reg) & 0x7F)

// Chip ID
#define BME280_CHIP_ID 0x60

typedef enum {
    BME280_SM_POWER_ON_RESET = 0,
    BME280_SM_CHECK_ID,
    BME280_SM_READ_CALIB_00,
    BME280_SM_READ_CALIB_26,
    BME280_SM_CONFIGURE,
    BME280_SM_READY,
    BME280_SM_TRIGGER_MEAS,
    BME280_SM_WAIT_MEAS,
    BME280_SM_READ_DATA,
    BME280_SM_PROCESS_DATA,
    BME280_SM_IDLE,
    BME280_SM_ERROR
} bme280_sm_state_t;

typedef struct {
    // Calibration trimming parameters (abbreviated)
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    // ... (remaining 18 parameters omitted for brevity)
    int32_t  t_fine;  // shared between temperature and pressure compensation
} bme280_calib_t;

typedef struct {
    bme280_sm_state_t state;
    bme280_calib_t    calib;
    uint8_t           raw[8];       // raw measurement burst
    float             temperature;
    float             pressure;
    float             humidity;
    uint32_t          tick_ms;
    uint32_t          meas_start_ms;
    void             *spi_ctx;
} bme280_ctx_t;

// Platform SPI ops
extern void  spi_cs_assert(void *ctx);
extern void  spi_cs_deassert(void *ctx);
extern void  spi_write_byte(void *ctx, uint8_t b);
extern uint8_t spi_read_byte(void *ctx);

static void bme280_read_regs(bme280_ctx_t *ctx, uint8_t reg,
                              uint8_t *buf, uint8_t len) {
    spi_cs_assert(ctx->spi_ctx);
    spi_write_byte(ctx->spi_ctx, BME280_SPI_READ(reg));
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = spi_read_byte(ctx->spi_ctx);
    }
    spi_cs_deassert(ctx->spi_ctx);
}

static void bme280_write_reg(bme280_ctx_t *ctx, uint8_t reg, uint8_t val) {
    spi_cs_assert(ctx->spi_ctx);
    spi_write_byte(ctx->spi_ctx, BME280_SPI_WRITE(reg));
    spi_write_byte(ctx->spi_ctx, val);
    spi_cs_deassert(ctx->spi_ctx);
}

// Parse calibration data from raw bytes
static void bme280_parse_calib(bme280_ctx_t *ctx, const uint8_t *raw) {
    ctx->calib.dig_T1 = (uint16_t)(raw[1] << 8 | raw[0]);
    ctx->calib.dig_T2 = (int16_t) (raw[3] << 8 | raw[2]);
    ctx->calib.dig_T3 = (int16_t) (raw[5] << 8 | raw[4]);
    ctx->calib.dig_P1 = (uint16_t)(raw[7] << 8 | raw[6]);
    // ... parse remaining parameters
}

// Simplified temperature compensation (from BME280 datasheet)
static float bme280_compensate_temp(bme280_ctx_t *ctx, int32_t adc_T) {
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)ctx->calib.dig_T1 << 1))) *
                    (int32_t)ctx->calib.dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)ctx->calib.dig_T1) *
                      ((adc_T >> 4) - (int32_t)ctx->calib.dig_T1)) >> 12) *
                    (int32_t)ctx->calib.dig_T3) >> 14;
    ctx->calib.t_fine = var1 + var2;
    return (float)((ctx->calib.t_fine * 5 + 128) >> 8) / 100.0f;
}

// State machine tick — call from main loop or RTOS task
void bme280_sm_tick(bme280_ctx_t *ctx, uint32_t now_ms) {
    ctx->tick_ms = now_ms;
    uint8_t buf[26];

    switch (ctx->state) {

        case BME280_SM_POWER_ON_RESET:
            // Issue soft reset
            bme280_write_reg(ctx, BME280_REG_RESET, 0xB6);
            ctx->meas_start_ms = now_ms;
            ctx->state = BME280_SM_CHECK_ID;
            break;

        case BME280_SM_CHECK_ID:
            if (now_ms - ctx->meas_start_ms < 3) break; // 2ms startup
            bme280_read_regs(ctx, BME280_REG_ID, buf, 1);
            if (buf[0] == BME280_CHIP_ID) {
                ctx->state = BME280_SM_READ_CALIB_00;
            } else {
                ctx->state = BME280_SM_ERROR;
            }
            break;

        case BME280_SM_READ_CALIB_00:
            bme280_read_regs(ctx, BME280_REG_CALIB00, buf, 26);
            bme280_parse_calib(ctx, buf);
            ctx->state = BME280_SM_READ_CALIB_26;
            break;

        case BME280_SM_READ_CALIB_26:
            bme280_read_regs(ctx, BME280_REG_CALIB26, buf, 7);
            // parse humidity calibration from buf...
            ctx->state = BME280_SM_CONFIGURE;
            break;

        case BME280_SM_CONFIGURE:
            // Oversample: hum x1, temp x2, press x4; forced mode
            bme280_write_reg(ctx, BME280_REG_CTRL_HUM,  0x01);
            bme280_write_reg(ctx, BME280_REG_CONFIG,     0xA0); // t_sb=1000ms, IIR off
            bme280_write_reg(ctx, BME280_REG_CTRL_MEAS,  0x57); // osrs_t=010, osrs_p=100, normal mode
            ctx->state = BME280_SM_READY;
            break;

        case BME280_SM_READY:
            // Trigger a forced measurement from external request
            // (transition to TRIGGER_MEAS is driven externally)
            break;

        case BME280_SM_TRIGGER_MEAS:
            // Forced mode: write mode bits to trigger single measurement
            bme280_write_reg(ctx, BME280_REG_CTRL_MEAS, 0x56 | 0x01);
            ctx->meas_start_ms = now_ms;
            ctx->state = BME280_SM_WAIT_MEAS;
            break;

        case BME280_SM_WAIT_MEAS:
            // BME280 typical measurement time for these settings: ~9ms
            if (now_ms - ctx->meas_start_ms >= 10) {
                ctx->state = BME280_SM_READ_DATA;
            }
            break;

        case BME280_SM_READ_DATA:
            bme280_read_regs(ctx, BME280_REG_DATA, ctx->raw, 8);
            ctx->state = BME280_SM_PROCESS_DATA;
            break;

        case BME280_SM_PROCESS_DATA: {
            int32_t adc_T = (int32_t)(
                ((uint32_t)ctx->raw[3] << 12) |
                ((uint32_t)ctx->raw[4] <<  4) |
                ((uint32_t)ctx->raw[5] >>  4));
            ctx->temperature = bme280_compensate_temp(ctx, adc_T);
            // ... compute pressure and humidity similarly
            ctx->state = BME280_SM_IDLE;
            break;
        }

        case BME280_SM_IDLE:
            // Data is ready; caller reads ctx->temperature, pressure, humidity
            break;

        case BME280_SM_ERROR:
        default:
            break;
    }
}
```

---

## Error Handling and Recovery States

Robust SPI state machines must handle failures explicitly. Key error scenarios include:

**Timeout** — the peripheral never responds. The state machine tracks `elapsed` time and transitions to an `ERROR` state if a deadline is exceeded.

**CRC/parity mismatch** — some SPI devices (e.g., SD cards) include CRC. A mismatch should trigger a retry sub-state or transition to ERROR after N retries.

**Unexpected response** — e.g., chip ID mismatch. Hard error; machine stops.

**Bus contention / DMA underrun** — hardware-level errors signaled via status flags. Require bus reset.

```c
// Error recovery state pattern
typedef enum {
    SM_STATE_NORMAL_OPERATION,
    SM_STATE_ERROR_DETECTED,
    SM_STATE_BUS_RESET,       // De-assert CS, toggle SCLK to flush device state
    SM_STATE_RETRY_INIT,      // Re-run initialization sequence
    SM_STATE_FATAL_ERROR      // Give up; notify application layer
} sm_recovery_state_t;

typedef struct {
    sm_recovery_state_t recovery_state;
    uint8_t             retry_count;
    uint8_t             max_retries;
    uint32_t            reset_start_ms;
} sm_error_ctx_t;

void sm_handle_error(sm_error_ctx_t *ectx, uint32_t now_ms) {
    switch (ectx->recovery_state) {

        case SM_STATE_ERROR_DETECTED:
            if (ectx->retry_count < ectx->max_retries) {
                ectx->recovery_state = SM_STATE_BUS_RESET;
                ectx->reset_start_ms = now_ms;
            } else {
                ectx->recovery_state = SM_STATE_FATAL_ERROR;
            }
            break;

        case SM_STATE_BUS_RESET:
            // Hold CS high for at least 8 clock cycles + device recovery time
            if (now_ms - ectx->reset_start_ms >= 5) {
                ectx->retry_count++;
                ectx->recovery_state = SM_STATE_RETRY_INIT;
            }
            break;

        case SM_STATE_RETRY_INIT:
            // Signal parent state machine to re-enter INIT state
            break;

        case SM_STATE_FATAL_ERROR:
            // Log, assert LED, notify RTOS error handler
            break;

        default:
            break;
    }
}
```

---

## Testing State Machines

Because state machines separate protocol logic from hardware, they are straightforward to unit-test with mock buses.

```rust
// tests/spi_sm_tests.rs

#[cfg(test)]
mod tests {
    use super::*;

    // A mock SPI bus that records all traffic
    struct RecordingBus {
        tx_log: Vec<u8>,
        rx_queue: std::collections::VecDeque<u8>,
        cs_log: Vec<&'static str>,
    }

    impl RecordingBus {
        fn new(rx_data: &[u8]) -> Self {
            RecordingBus {
                tx_log: Vec::new(),
                rx_queue: rx_data.iter().copied().collect(),
                cs_log: Vec::new(),
            }
        }
    }

    impl SpiBus for RecordingBus {
        type Error = ();
        fn cs_assert(&mut self)   -> Result<(), ()> { self.cs_log.push("assert"); Ok(()) }
        fn cs_deassert(&mut self) -> Result<(), ()> { self.cs_log.push("deassert"); Ok(()) }
        fn transfer(&mut self, tx: u8) -> Result<u8, ()> {
            self.tx_log.push(tx);
            Ok(self.rx_queue.pop_front().unwrap_or(0xFF))
        }
    }

    #[test]
    fn test_read_sends_correct_command_and_address() {
        // Arrange
        let mock = RecordingBus::new(&[0xAA, 0xBB, 0xCC, 0xDD]);
        let sm = SpiSm::<Idle, _>::new(mock);

        // Act: execute full read state sequence
        let done = sm
            .begin_read(0x03, 0x123456, 4).unwrap()
            .send_command().unwrap()
            .send_address().unwrap()
            .transfer_all().unwrap()
            .finish().unwrap();

        // Assert: correct bytes were transmitted
        assert_eq!(done.bus.tx_log[0], 0x03,   "opcode");
        assert_eq!(done.bus.tx_log[1], 0x12,   "addr high");
        assert_eq!(done.bus.tx_log[2], 0x34,   "addr mid");
        assert_eq!(done.bus.tx_log[3], 0x56,   "addr low");

        // Assert: CS was asserted then deasserted
        assert_eq!(done.bus.cs_log, vec!["assert", "deassert"]);

        // Assert: correct data received
        assert_eq!(done.data(), &[0xAA, 0xBB, 0xCC, 0xDD]);
    }

    #[test]
    fn test_compile_time_enforcement() {
        // The following would NOT compile — this is the typestate guarantee:
        //
        // let sm = SpiSm::<Idle, _>::new(MockBus::default());
        // sm.send_command();  // ERROR: method not found for Idle state
        //
        // let sm = sm.begin_read(0x03, 0x0, 1).unwrap();
        // sm.send_address();  // ERROR: must call send_command() first
    }
}
```

---

## Summary

SPI protocol state machines are a foundational pattern for embedded firmware. The key insights are:

**Explicit states prevent ambiguity.** Naming every protocol phase — idle, assert CS, send command, send address, transfer data, deassert CS, complete, error — makes the code self-documenting and leaves no room for "in between" states to go unhandled.

**Transitions encode protocol rules.** Moving between states only on defined events (TX complete, RX ready, timeout) means the protocol specification lives directly in the code structure, not in comments.

**Error states are first-class.** Every SPI state machine should have explicit error and recovery states with retry logic and timeout guards. Devices can and do fail, and the state machine must handle that gracefully.

**C/C++ and Rust serve different constraints.** In C, the `switch`-on-enum pattern is portable and efficient for resource-constrained targets. C++ class-based machines with function pointers scale well to more complex protocols. Rust's typestate pattern provides unparalleled compile-time safety — invalid state transitions simply do not compile. Rust's `async/await` with Embassy makes non-blocking, multi-step SPI protocols clean and composable without sacrificing zero-cost abstractions.

**Non-blocking designs scale.** Interrupt-driven and async state machines allow the CPU to perform other work during SPI wait periods (e.g., flash erase, status polling), which is critical in real-time systems with multiple concurrent peripherals.

**Unit testing is easy.** Because the state machine accepts a HAL abstraction (C) or a trait (Rust), the entire protocol logic can be exercised with a mock bus — no hardware required, no oscilloscope needed to debug protocol violations.

By applying these principles, complex SPI device protocols — multi-phase flash operations, sensor initialization sequences, display controller commands — become manageable, maintainable, and reliable even on the most resource-constrained embedded targets.

---

*Document: 96 — State Machine for SPI Protocols | Embedded Systems Series*