# 67. UART Multiplexing

**Hardware** — topology diagrams for fan-in, fan-out, and full-duplex bidirectional mux configurations; a comparison table of common ICs (TMUX1208, TS5A23157, SN74LVC4066, etc.); electrical considerations for pull-ups, signal integrity, ESD, and bus contention.

**Timing** — baud-rate-vs-switch-time analysis, frame boundary rules, and the critical distinction between FIFO-empty and shift-register-empty (TC flag) when deciding it's safe to switch.

**C/C++ code:**
- `uart_mux.h/.c` — portable HAL-agnostic driver with function pointers for GPIO, delay, and idle-check
- `UartMuxManager` (C++) — FreeRTOS mutex-protected manager with per-channel baud reconfiguration
- DMA-backed queue — enqueue transfers per channel, automatic IRQ-driven channel switching

**Rust code:**
- Type-state `UartMux<..., Idle>` / `UartMux<..., Active<CH>>` — the compiler prevents UART access without a selected channel (zero runtime cost)
- Embassy async multiplexer — async mutex + task-per-device cooperative pattern

**Error handling** — framing error detection/recovery, RX glitch suppression during switch, loopback self-test pattern.

### Using Analog Switches or Muxes for Shared UART Resources

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why UART Multiplexing?](#why-uart-multiplexing)
3. [Core Concepts](#core-concepts)
4. [Hardware Topologies](#hardware-topologies)
   - [Many-to-One (Fan-In)](#many-to-one-fan-in)
   - [One-to-Many (Fan-Out)](#one-to-many-fan-out)
   - [Bidirectional Full-Duplex Mux](#bidirectional-full-duplex-mux)
5. [Key Components](#key-components)
6. [Electrical Considerations](#electrical-considerations)
7. [Protocol and Timing Constraints](#protocol-and-timing-constraints)
8. [C/C++ Implementation](#cc-implementation)
   - [GPIO-Controlled Mux Driver](#gpio-controlled-mux-driver)
   - [Thread-Safe UART Mux Manager](#thread-safe-uart-mux-manager)
   - [DMA-Backed Multiplexed UART](#dma-backed-multiplexed-uart)
9. [Rust Implementation](#rust-implementation)
   - [Type-State Mux Driver](#type-state-mux-driver)
   - [Async UART Multiplexer](#async-uart-multiplexer)
10. [Error Handling and Edge Cases](#error-handling-and-edge-cases)
11. [Testing and Validation](#testing-and-validation)
12. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) is one of the oldest and most widely used serial communication protocols in embedded systems. Despite its simplicity, a fundamental limitation often surfaces in real-world designs: **most microcontrollers provide far fewer hardware UART peripherals than the number of devices that need to communicate serially**.

**UART Multiplexing** solves this problem by sharing one or more UART hardware peripherals among multiple devices using analog switches (analog muxes), digital switches, or dedicated multiplexer ICs. Instead of dedicating one UART per device, the firmware selects the active channel by controlling the mux's address/select lines before each transaction.

This document covers:
- The hardware strategies and ICs commonly used
- Critical electrical and timing considerations
- Complete C/C++ and Rust driver implementations
- Thread-safe and async patterns for real systems

---

## Why UART Multiplexing?

| Problem | Without Muxing | With Muxing |
|---|---|---|
| 8 GPS modules on one MCU | Requires 8 UART peripherals | 1 UART + 1× 8:1 mux |
| Debug console + 3 sensors | 4 UARTs needed | 1 UART + 4:1 mux |
| PCB space | Many UART pins exposed | Only select lines added |
| Cost | Higher-pin MCU required | Cheap mux IC (~$0.30) |
| Power | All UARTs clocked | One UART + mux sleep |

Common real-world use cases include:
- **IoT gateways** connecting multiple sensor nodes
- **Test & measurement** rigs switching between DUT ports
- **Industrial controllers** managing multiple RS-232/RS-485 nodes
- **Development boards** sharing a single USB-UART bridge across subsystems
- **Modular robotics** where motor controllers each speak UART

---

## Core Concepts

### UART Signal Lines Involved

A standard UART connection uses:

```
MCU TX  ──────►  Device RX
MCU RX  ◄──────  Device TX
(optionally: RTS, CTS for flow control)
```

A multiplexer is inserted between the MCU and the device bank:

```
                  ┌──────────────┐
MCU TX ──────────►│              │──► Device 0 RX
MCU RX ◄──────────│   UART MUX   │◄── Device 0 TX
                  │              │──► Device 1 RX
SEL[0..n] ───────►│   (analog    │◄── Device 1 TX
                  │    switch)   │──► Device N RX
                  │              │◄── Device N TX
                  └──────────────┘
```

### Multiplexing Principle

The select lines (SEL) are driven by GPIO outputs on the MCU. Before any UART transaction:
1. Assert the target channel's address on SEL lines
2. Allow propagation delay (typically 10–100 ns for analog muxes)
3. Execute the UART transaction
4. Optionally assert a "neutral" / disabled state if bus contention is a concern

---

## Hardware Topologies

### Many-to-One (Fan-In)

Multiple devices share a single UART RX line on the MCU. Only one device transmits at a time.

```
Device 0 TX ──┐
Device 1 TX ──┤  4:1 Analog Mux  ├──► MCU RX
Device 2 TX ──┤  (e.g. TS3A4751) │
Device 3 TX ──┘
                SEL[0..1] ◄── MCU GPIO

MCU TX ────────────────────────────────► All Device RX (broadcast)
                                         OR individual switches
```

**Use case:** Multiple sensors, only one is active at a time. The MCU selects which sensor's output it listens to. TX can be broadcast if all devices are configured to ignore frames not addressed to them (address-based framing), or individual TX switches can be added.

### One-to-Many (Fan-Out)

The MCU's TX is switched to one device at a time. Device TX lines may be OR-tied (with protection) or individually switched.

```
                  ┌──────────────┐
MCU TX ──────────►│   1:4 Demux  │──► Device 0 RX
                  │  (e.g.       │──► Device 1 RX
SEL[0..1] ───────►│  SN74HC139)  │──► Device 2 RX
                  └──────────────┘──► Device 3 RX

Device 0 TX ──┐
Device 1 TX ──┤  4:1 Mux  ├──► MCU RX
Device 2 TX ──┤           │
Device 3 TX ──┘
```

### Bidirectional Full-Duplex Mux

A full UART mux switches both TX and RX (and optionally RTS/CTS) simultaneously. Bidirectional analog switches such as the **TS3A4751**, **MAX4617**, or **SN74LVC4066** handle both directions in a single chip:

```
                    ┌─────────────────────────┐
  MCU TX ──────────►│ Y_TX     A0_TX ──────────┤──► Dev0 RX
  MCU RX ◄──────────│ Y_RX     A0_RX ◄──────────┤──── Dev0 TX
                    │                           │
  SEL[0] GPIO ─────►│ S0       A1_TX ──────────┤──► Dev1 RX
  SEL[1] GPIO ─────►│ S1       A1_RX ◄──────────┤──── Dev1 TX
                    │          ...              │
                    └─────────────────────────┘
```

---

## Key Components

### Popular Analog Switch / Mux ICs

| Part | Channels | VCC Range | RON (Ω) | Max Freq | Notes |
|---|---|---|---|---|---|
| **TS3A4751** | 4×SPST | 1.65–5.5V | 0.6 | 400 MHz | Very low RON, USB-grade |
| **SN74LVC4066** | 4×SPST | 1.65–5.5V | 4–7 | 200 MHz | Classic CMOS switch |
| **MAX4617** | 8:1 SPST | 2.7–5.5V | 35 | 100 MHz | 8-channel single |
| **CD74HC4051** | 8:1 analog | 2–6V | 50–125 | 40 MHz | Cheap, 3-bit select |
| **TS5A23157** | 2×DPDT | 1.65–5.5V | 0.9 | 500 MHz | DPDT, great for TX+RX pair |
| **TMUX1208** | 8:1 or 4:2 | 1.62–5.5V | 1.5 | 1 GHz | TI recommended for UART |

### Mux Selection Criteria

- **RON (On-Resistance):** For UART speeds ≤ 1 Mbaud, nearly any low-RON mux works. High-speed (>3 Mbaud) requires RON < 10 Ω and careful PCB layout.
- **Propagation / Enable Time (tpd, ten):** Must be less than one UART bit period. At 9600 baud (104 µs/bit), even the slowest mux is fine. At 1 Mbaud (1 µs/bit), use a mux with tpd < 100 ns.
- **Voltage level compatibility:** Level-shifting muxes exist for mixed 3.3V/5V systems.
- **Break-before-make:** Preferred for UART to prevent bus contention between TX sources.

---

## Electrical Considerations

### Bus Contention

When switching channels, there is a brief moment where the old and new device TX lines could both be connected. To prevent contention:
- Use muxes with **break-before-make** switching
- Disable UART TX (set TX pin as high-Z input) before switching
- Add a propagation delay in firmware between channel select and first byte

### Pull-Up / Pull-Down Resistors

UART idle state is logic HIGH. Unconnected mux inputs or floating device TX lines can cause spurious framing errors:
- Add **10 kΩ pull-up** resistors on device TX lines to VCC
- These ensure idle-HIGH even when the MCU's RX is not connected to that device

### Signal Integrity

For higher baud rates through a mux:
- Keep PCB traces short (< 5 cm for >1 Mbaud)
- Add a **33–100 Ω series resistor** between mux output and device to dampen ringing
- Decouple VCC of mux IC with 100 nF ceramic close to the supply pin

### ESD and Overvoltage

Place TVS diodes or ESD protection arrays on device-side UART lines, especially for connectors. The **TPD4E004** or **PRTR5V0U2X** are popular choices.

---

## Protocol and Timing Constraints

### Minimum Switch Time Requirement

```
t_switch_required < 1 bit period = 1 / baud_rate

At  9600 baud: 104.2 µs  (very relaxed)
At115200 baud:   8.7 µs  (relaxed)
At   1 Mbaud:    1.0 µs  (use fast mux + firmware delay)
At   3 Mbaud:  333 ns    (requires careful IC selection)
```

### Frame Boundary Switching

**Always switch channel at a UART frame boundary** — never in the middle of a byte. The correct sequence is:

```
1. Wait for current TX to complete (shift register empty, not just FIFO empty)
2. Wait for RX idle (no active reception)
3. Assert new SEL address on GPIO
4. Wait ≥ t_prop of mux IC (from datasheet)
5. Begin UART transaction with new device
```

Switching mid-frame produces a framing error on the device currently being de-selected (it sees the TX line go idle prematurely).

---

## C/C++ Implementation

### GPIO-Controlled Mux Driver

A simple, portable C driver for a generic N-channel UART mux. This targets an MCU like STM32 but can be adapted to any HAL.

```c
/*
 * uart_mux.h
 * Generic UART multiplexer driver for analog switch ICs
 * Supports up to 8 channels (3 select lines)
 */

#ifndef UART_MUX_H
#define UART_MUX_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum channels supported (determined by number of SEL pins) */
#define UART_MUX_MAX_CHANNELS   8
#define UART_MUX_MAX_SEL_PINS   3

/* Result codes */
typedef enum {
    UART_MUX_OK          =  0,
    UART_MUX_ERR_INVALID = -1,
    UART_MUX_ERR_BUSY    = -2,
    UART_MUX_ERR_TIMEOUT = -3,
} uart_mux_result_t;

/* Platform abstraction: GPIO write function pointer */
typedef void (*gpio_write_fn)(uint8_t pin_id, bool state);

/* Platform abstraction: delay in microseconds */
typedef void (*delay_us_fn)(uint32_t us);

/* Platform abstraction: check UART TX complete */
typedef bool (*uart_tx_idle_fn)(void);

/* Platform abstraction: check UART RX idle */
typedef bool (*uart_rx_idle_fn)(void);

/* Select pin descriptor */
typedef struct {
    uint8_t pin_id;    /* Platform-specific pin identifier */
    bool    active_hi; /* True if pin HIGH = selected bit is 1 */
} uart_mux_sel_pin_t;

/* Mux configuration */
typedef struct {
    uint8_t              num_channels;
    uint8_t              num_sel_pins;
    uart_mux_sel_pin_t   sel_pins[UART_MUX_MAX_SEL_PINS];
    uint8_t              enable_pin;    /* 0xFF = not used */
    bool                 enable_active_hi;
    uint32_t             propagation_delay_us; /* t_prop from datasheet */
    gpio_write_fn        gpio_write;
    delay_us_fn          delay_us;
    uart_tx_idle_fn      tx_idle;
    uart_rx_idle_fn      rx_idle;
    uint32_t             idle_timeout_us;
} uart_mux_config_t;

/* Mux instance state */
typedef struct {
    uart_mux_config_t cfg;
    int8_t            active_channel;   /* -1 = none selected */
    uint32_t          switch_count;     /* Statistics */
    uint32_t          timeout_count;
} uart_mux_t;

/* API */
uart_mux_result_t uart_mux_init(uart_mux_t *mux, const uart_mux_config_t *cfg);
uart_mux_result_t uart_mux_select(uart_mux_t *mux, uint8_t channel);
uart_mux_result_t uart_mux_deselect(uart_mux_t *mux);
int8_t            uart_mux_get_active(const uart_mux_t *mux);

#endif /* UART_MUX_H */
```

```c
/*
 * uart_mux.c
 * UART multiplexer implementation
 */

#include "uart_mux.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static void apply_sel_address(uart_mux_t *mux, uint8_t channel)
{
    for (uint8_t i = 0; i < mux->cfg.num_sel_pins; i++) {
        bool bit_val = (channel >> i) & 0x01;
        bool pin_state = mux->cfg.sel_pins[i].active_hi ? bit_val : !bit_val;
        mux->cfg.gpio_write(mux->cfg.sel_pins[i].pin_id, pin_state);
    }
}

static void set_enable(uart_mux_t *mux, bool enabled)
{
    if (mux->cfg.enable_pin == 0xFF) return;
    bool pin_state = mux->cfg.enable_active_hi ? enabled : !enabled;
    mux->cfg.gpio_write(mux->cfg.enable_pin, pin_state);
}

static uart_mux_result_t wait_uart_idle(uart_mux_t *mux)
{
    uint32_t elapsed = 0;
    const uint32_t poll_us = 10;

    while (elapsed < mux->cfg.idle_timeout_us) {
        if (mux->cfg.tx_idle() && mux->cfg.rx_idle()) {
            return UART_MUX_OK;
        }
        mux->cfg.delay_us(poll_us);
        elapsed += poll_us;
    }

    mux->timeout_count++;
    return UART_MUX_ERR_TIMEOUT;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

uart_mux_result_t uart_mux_init(uart_mux_t *mux, const uart_mux_config_t *cfg)
{
    if (!mux || !cfg) return UART_MUX_ERR_INVALID;
    if (cfg->num_channels == 0 ||
        cfg->num_channels > UART_MUX_MAX_CHANNELS) return UART_MUX_ERR_INVALID;
    if (cfg->num_sel_pins == 0 ||
        cfg->num_sel_pins > UART_MUX_MAX_SEL_PINS) return UART_MUX_ERR_INVALID;
    if (!cfg->gpio_write || !cfg->delay_us) return UART_MUX_ERR_INVALID;

    memset(mux, 0, sizeof(*mux));
    mux->cfg = *cfg;
    mux->active_channel = -1;

    /* Start with mux disabled / channel 0 selected but outputs tristated */
    set_enable(mux, false);
    apply_sel_address(mux, 0);

    return UART_MUX_OK;
}

uart_mux_result_t uart_mux_select(uart_mux_t *mux, uint8_t channel)
{
    if (!mux) return UART_MUX_ERR_INVALID;
    if (channel >= mux->cfg.num_channels) return UART_MUX_ERR_INVALID;

    /* Already on correct channel? */
    if (mux->active_channel == (int8_t)channel) return UART_MUX_OK;

    /* Step 1: Wait for any in-progress UART transaction to complete */
    uart_mux_result_t result = wait_uart_idle(mux);
    if (result != UART_MUX_OK) {
        /* Log and proceed anyway — don't deadlock */
    }

    /* Step 2: Disable mux output to prevent contention during switching */
    set_enable(mux, false);

    /* Step 3: Apply new channel address to SEL pins */
    apply_sel_address(mux, channel);

    /* Step 4: Wait for propagation delay (mux datasheet t_en / t_prop) */
    mux->cfg.delay_us(mux->cfg.propagation_delay_us);

    /* Step 5: Enable mux output */
    set_enable(mux, true);

    mux->active_channel = (int8_t)channel;
    mux->switch_count++;

    return UART_MUX_OK;
}

uart_mux_result_t uart_mux_deselect(uart_mux_t *mux)
{
    if (!mux) return UART_MUX_ERR_INVALID;

    wait_uart_idle(mux);
    set_enable(mux, false);
    mux->active_channel = -1;

    return UART_MUX_OK;
}

int8_t uart_mux_get_active(const uart_mux_t *mux)
{
    if (!mux) return -1;
    return mux->active_channel;
}
```

### Thread-Safe UART Mux Manager

In an RTOS environment, multiple tasks may want to use the UART simultaneously. This C++ class wraps the mux with a mutex and a task queue (FreeRTOS example):

```cpp
// uart_mux_manager.hpp
// Thread-safe UART mux manager for FreeRTOS
#pragma once

#include "uart_mux.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <functional>
#include <array>
#include <cstdint>

constexpr uint8_t MAX_MUX_CHANNELS = 8;

class UartMuxManager {
public:
    using TransactionFn = std::function<void(uint8_t channel)>;

    struct ChannelConfig {
        uint32_t baud_rate;
        bool     enabled;
        uint32_t timeout_ms;
    };

    explicit UartMuxManager(uart_mux_t *mux_hw)
        : mux_(mux_hw)
        , mutex_(xSemaphoreCreateMutex())
    {}

    ~UartMuxManager() {
        if (mutex_) vSemaphoreDelete(mutex_);
    }

    /* Configure per-channel settings (baud, timeout) */
    bool configure_channel(uint8_t ch, const ChannelConfig &cfg) {
        if (ch >= MAX_MUX_CHANNELS) return false;
        channel_cfg_[ch] = cfg;
        return true;
    }

    /*
     * Execute a transaction on the given channel.
     * Acquires mutex, selects channel, calls fn, releases.
     * Returns false on timeout acquiring the lock.
     */
    bool transact(uint8_t channel, TransactionFn fn,
                  TickType_t lock_timeout = pdMS_TO_TICKS(100))
    {
        if (xSemaphoreTake(mutex_, lock_timeout) != pdTRUE) {
            return false;
        }

        /* Reconfigure UART baud if channel has different rate */
        uint32_t baud = channel_cfg_[channel].baud_rate;
        if (baud != current_baud_) {
            reconfigure_uart_baud(baud);
            current_baud_ = baud;
        }

        /* Select mux channel (waits for UART idle internally) */
        uart_mux_select(mux_, channel);

        /* Execute user transaction */
        fn(channel);

        /* Deselect — leaves bus in high-Z / idle state */
        uart_mux_deselect(mux_);

        xSemaphoreGive(mutex_);
        return true;
    }

    /* Non-blocking attempt */
    bool try_transact(uint8_t channel, TransactionFn fn) {
        return transact(channel, fn, 0);
    }

private:
    void reconfigure_uart_baud(uint32_t baud) {
        /* Platform-specific: reconfigure USART peripheral */
        /* e.g. HAL_UART_DeInit / HAL_UART_Init on STM32    */
        (void)baud;
    }

    uart_mux_t     *mux_;
    SemaphoreHandle_t mutex_;
    std::array<ChannelConfig, MAX_MUX_CHANNELS> channel_cfg_{};
    uint32_t        current_baud_ = 115200;
};
```

Usage example in FreeRTOS tasks:

```cpp
// Example usage across two RTOS tasks

UartMuxManager mux_mgr(&hw_mux);

// Task 1: Read GPS on channel 2
void task_gps(void *) {
    for (;;) {
        mux_mgr.transact(2, [](uint8_t ch) {
            uart_send_string("$PMTK220,1000*1F\r\n");
            uint8_t buf[128];
            uart_receive(buf, sizeof(buf), 200 /*ms*/);
            parse_nmea(buf);
        });
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Task 2: Poll temperature sensor on channel 5
void task_temp(void *) {
    for (;;) {
        bool ok = mux_mgr.transact(5, [](uint8_t ch) {
            uart_send_byte(0x01); /* request temperature */
            uint8_t resp[4];
            uart_receive(resp, sizeof(resp), 50 /*ms*/);
            process_temperature(resp);
        }, pdMS_TO_TICKS(50));  /* 50 ms lock timeout */

        if (!ok) log_error("UART mux busy");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

### DMA-Backed Multiplexed UART

For high-throughput applications, DMA transfers can be chained with mux switching using a transfer-complete callback:

```c
/*
 * uart_mux_dma.c
 * DMA-based UART mux: queue transfers per channel,
 * switch and fire DMA automatically.
 *
 * Demonstrates the pattern; HAL calls are STM32-style pseudocode.
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "uart_mux.h"

#define DMA_QUEUE_DEPTH  8
#define DMA_BUF_SIZE     256

typedef enum { XFER_TX, XFER_RX } xfer_dir_t;

typedef struct {
    uint8_t     channel;
    xfer_dir_t  dir;
    uint8_t    *buf;
    uint16_t    len;
    void      (*complete_cb)(uint8_t channel, uint8_t *buf, uint16_t len);
} dma_xfer_t;

typedef struct {
    uart_mux_t  *mux;
    dma_xfer_t   queue[DMA_QUEUE_DEPTH];
    uint8_t      head, tail, count;
    bool         busy;
    uint8_t      dma_buf[DMA_BUF_SIZE];
} uart_mux_dma_t;

static uart_mux_dma_t s_dma_ctx;

static void start_next_transfer(uart_mux_dma_t *ctx);

/* Enqueue a DMA transfer request */
bool uart_mux_dma_enqueue(uint8_t channel, xfer_dir_t dir,
                           uint8_t *buf, uint16_t len,
                           void (*cb)(uint8_t, uint8_t *, uint16_t))
{
    uart_mux_dma_t *ctx = &s_dma_ctx;
    if (ctx->count >= DMA_QUEUE_DEPTH) return false;

    dma_xfer_t *xfer = &ctx->queue[ctx->tail];
    xfer->channel    = channel;
    xfer->dir        = dir;
    xfer->buf        = buf;
    xfer->len        = len;
    xfer->complete_cb = cb;

    ctx->tail = (ctx->tail + 1) % DMA_QUEUE_DEPTH;
    ctx->count++;

    if (!ctx->busy) {
        start_next_transfer(ctx);
    }
    return true;
}

static void start_next_transfer(uart_mux_dma_t *ctx)
{
    if (ctx->count == 0) {
        ctx->busy = false;
        uart_mux_deselect(ctx->mux);
        return;
    }

    ctx->busy = true;
    dma_xfer_t *xfer = &ctx->queue[ctx->head];

    /* Select the mux channel */
    uart_mux_select(ctx->mux, xfer->channel);

    if (xfer->dir == XFER_TX) {
        memcpy(ctx->dma_buf, xfer->buf, xfer->len);
        /* HAL_UART_Transmit_DMA(&huart1, ctx->dma_buf, xfer->len); */
    } else {
        /* HAL_UART_Receive_DMA(&huart1, ctx->dma_buf, xfer->len); */
    }
}

/* Called from DMA complete IRQ / callback */
void uart_mux_dma_complete_irq(void)
{
    uart_mux_dma_t *ctx = &s_dma_ctx;
    dma_xfer_t *xfer    = &ctx->queue[ctx->head];

    /* Notify caller */
    if (xfer->complete_cb) {
        xfer->complete_cb(xfer->channel, ctx->dma_buf, xfer->len);
    }

    ctx->head  = (ctx->head + 1) % DMA_QUEUE_DEPTH;
    ctx->count--;

    /* Schedule next transfer */
    start_next_transfer(ctx);
}
```

---

## Rust Implementation

### Type-State Mux Driver

Rust's type system allows us to encode the mux state at compile time, making it impossible to use the UART without selecting a channel first.

```rust
// uart_mux.rs
// Type-state UART multiplexer driver
// Ensures channel is selected before UART access via the type system

use core::marker::PhantomData;

// ----------------------------------------------------------------
// Type states
// ----------------------------------------------------------------

/// Mux is idle: no channel selected
pub struct Idle;

/// Mux has an active channel selected (channel encoded as const generic)
pub struct Active<const CH: u8>;

// ----------------------------------------------------------------
// Platform abstraction traits
// ----------------------------------------------------------------

/// Trait abstracting GPIO output operations
pub trait GpioOutput {
    fn set_high(&mut self);
    fn set_low(&mut self);
}

/// Trait for blocking delay
pub trait DelayUs {
    fn delay_us(&mut self, us: u32);
}

/// Trait representing a UART peripheral with basic TX/RX
pub trait UartPeripheral {
    type Error;
    fn write(&mut self, data: &[u8]) -> Result<(), Self::Error>;
    fn read(&mut self, buf: &mut [u8]) -> Result<usize, Self::Error>;
    fn flush(&mut self) -> Result<(), Self::Error>;
}

// ----------------------------------------------------------------
// Select pin abstraction
// ----------------------------------------------------------------

/// Up to 3 select lines for 8 channels
pub struct SelectPins<S0, S1, S2> {
    pub s0: S0,
    pub s1: Option<S1>,
    pub s2: Option<S2>,
}

impl<S0, S1, S2> SelectPins<S0, S1, S2>
where
    S0: GpioOutput,
    S1: GpioOutput,
    S2: GpioOutput,
{
    pub fn apply(&mut self, channel: u8) {
        if channel & 0x01 != 0 { self.s0.set_high(); } else { self.s0.set_low(); }
        if let Some(ref mut s) = self.s1 {
            if channel & 0x02 != 0 { s.set_high(); } else { s.set_low(); }
        }
        if let Some(ref mut s) = self.s2 {
            if channel & 0x04 != 0 { s.set_high(); } else { s.set_low(); }
        }
    }
}

// ----------------------------------------------------------------
// Main mux struct
// ----------------------------------------------------------------

pub struct UartMux<UART, S0, S1, S2, EN, DLY, State> {
    uart:              UART,
    sel:               SelectPins<S0, S1, S2>,
    enable:            Option<EN>,
    delay:             DLY,
    num_channels:      u8,
    prop_delay_us:     u32,
    _state:            PhantomData<State>,
}

impl<UART, S0, S1, S2, EN, DLY> UartMux<UART, S0, S1, S2, EN, DLY, Idle>
where
    UART: UartPeripheral,
    S0:   GpioOutput,
    S1:   GpioOutput,
    S2:   GpioOutput,
    EN:   GpioOutput,
    DLY:  DelayUs,
{
    /// Construct a new mux in Idle state
    pub fn new(
        uart: UART,
        sel: SelectPins<S0, S1, S2>,
        enable: Option<EN>,
        delay: DLY,
        num_channels: u8,
        prop_delay_us: u32,
    ) -> Self {
        UartMux {
            uart,
            sel,
            enable,
            delay,
            num_channels,
            prop_delay_us,
            _state: PhantomData,
        }
    }

    /// Select a channel, transitioning to Active<CH> state
    /// This is the ONLY way to get a mux in Active state
    pub fn select<const CH: u8>(
        mut self,
    ) -> Result<UartMux<UART, S0, S1, S2, EN, DLY, Active<CH>>, Self>
    {
        if CH >= self.num_channels {
            return Err(self);
        }

        // Disable enable pin (break-before-make)
        if let Some(ref mut en) = self.enable {
            en.set_low();
        }

        // Apply channel address to SEL pins
        self.sel.apply(CH);

        // Wait for propagation
        self.delay.delay_us(self.prop_delay_us);

        // Re-enable
        if let Some(ref mut en) = self.enable {
            en.set_high();
        }

        Ok(UartMux {
            uart:          self.uart,
            sel:           self.sel,
            enable:        self.enable,
            delay:         self.delay,
            num_channels:  self.num_channels,
            prop_delay_us: self.prop_delay_us,
            _state:        PhantomData,
        })
    }
}

impl<UART, S0, S1, S2, EN, DLY, const CH: u8>
    UartMux<UART, S0, S1, S2, EN, DLY, Active<CH>>
where
    UART: UartPeripheral,
    S0:   GpioOutput,
    S1:   GpioOutput,
    S2:   GpioOutput,
    EN:   GpioOutput,
    DLY:  DelayUs,
{
    /// Access the UART — only available in Active state!
    pub fn uart(&mut self) -> &mut UART {
        &mut self.uart
    }

    /// Deselect channel, returning to Idle state
    pub fn deselect(mut self) -> UartMux<UART, S0, S1, S2, EN, DLY, Idle> {
        // Flush before deselecting
        let _ = self.uart.flush();

        if let Some(ref mut en) = self.enable {
            en.set_low();
        }

        UartMux {
            uart:          self.uart,
            sel:           self.sel,
            enable:        self.enable,
            delay:         self.delay,
            num_channels:  self.num_channels,
            prop_delay_us: self.prop_delay_us,
            _state:        PhantomData,
        }
    }

    /// Convenience: write bytes on the currently selected channel
    pub fn write(&mut self, data: &[u8]) -> Result<(), UART::Error> {
        self.uart.write(data)
    }

    /// Convenience: read bytes on the currently selected channel
    pub fn read(&mut self, buf: &mut [u8]) -> Result<usize, UART::Error> {
        self.uart.read(buf)
    }
}
```

Usage demonstrating compile-time guarantees:

```rust
// main.rs (embedded, no_std)
#![no_std]
#![no_main]

use uart_mux::{UartMux, SelectPins};

fn application(mut mux: UartMux</* ... */, Idle>) {
    // ✅ Select channel 0 — compiler verifies this is an Idle mux
    let mut mux_active = mux.select::<0>().unwrap();

    // ✅ Write is only available in Active state
    mux_active.write(b"AT\r\n").unwrap();
    let mut buf = [0u8; 64];
    let n = mux_active.read(&mut buf).unwrap();

    // ✅ Deselect returns ownership as Idle again
    let mux_idle = mux_active.deselect();

    // ✅ Select a different channel
    let mut mux_ch3 = mux_idle.select::<3>().unwrap();
    mux_ch3.write(b"HELLO\n").unwrap();
    let mux_idle = mux_ch3.deselect();

    // ❌ This would NOT compile:
    // mux_active.write(b"data"); // mux_active was moved into deselect()
}
```

### Async UART Multiplexer

Using `embassy` (async embedded Rust framework) for non-blocking multiplexed UART with cooperative multitasking:

```rust
// uart_mux_async.rs
// Async UART mux using Embassy + channel-based task coordination

use embassy_sync::mutex::Mutex;
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;
use embassy_time::{Duration, Timer};
use embassy_stm32::usart::{self, Uart};
use embassy_stm32::gpio::{Output, Level, Speed};

/// Shared mux resource guarded by an async mutex
pub struct AsyncUartMux {
    uart: Uart<'static, usart::USART1>,
    sel0: Output<'static>,
    sel1: Output<'static>,
    sel2: Output<'static>,
    num_channels: u8,
}

impl AsyncUartMux {
    pub fn new(
        uart: Uart<'static, usart::USART1>,
        sel0: Output<'static>,
        sel1: Output<'static>,
        sel2: Output<'static>,
        num_channels: u8,
    ) -> Self {
        Self { uart, sel0, sel1, sel2, num_channels }
    }

    fn apply_channel(&mut self, ch: u8) {
        if ch & 0x01 != 0 { self.sel0.set_high(); } else { self.sel0.set_low(); }
        if ch & 0x02 != 0 { self.sel1.set_high(); } else { self.sel1.set_low(); }
        if ch & 0x04 != 0 { self.sel2.set_high(); } else { self.sel2.set_low(); }
    }

    /// Select channel, execute async closure, deselect
    pub async fn transact<F, Fut, R>(&mut self, channel: u8, f: F) -> Option<R>
    where
        F: FnOnce(&mut Uart<'static, usart::USART1>) -> Fut,
        Fut: core::future::Future<Output = R>,
    {
        if channel >= self.num_channels {
            return None;
        }

        self.apply_channel(channel);

        // Brief propagation delay (yielding to executor, not blocking)
        Timer::after(Duration::from_micros(5)).await;

        let result = f(&mut self.uart).await;

        Some(result)
    }
}

// Global mux wrapped in async mutex for multi-task access
static MUX: Mutex<ThreadModeRawMutex, Option<AsyncUartMux>> = Mutex::new(None);

// GPS task: reads NMEA from channel 2
#[embassy_executor::task]
async fn gps_task() {
    loop {
        let mut guard = MUX.lock().await;
        if let Some(ref mut mux) = *guard {
            mux.transact(2, |uart| async move {
                let mut buf = [0u8; 128];
                // embassy UART async read with timeout
                match embassy_time::with_timeout(
                    Duration::from_millis(200),
                    uart.read(&mut buf),
                ).await {
                    Ok(Ok(_)) => process_nmea(&buf),
                    _ => {}
                }
            }).await;
        }
        drop(guard); // Release lock before sleeping
        Timer::after(Duration::from_millis(1000)).await;
    }
}

// Modem task: AT commands on channel 0
#[embassy_executor::task]
async fn modem_task() {
    loop {
        let mut guard = MUX.lock().await;
        if let Some(ref mut mux) = *guard {
            mux.transact(0, |uart| async move {
                uart.write(b"AT+CREG?\r\n").await.ok();
                let mut resp = [0u8; 64];
                embassy_time::with_timeout(
                    Duration::from_millis(500),
                    uart.read(&mut resp),
                ).await.ok();
            }).await;
        }
        drop(guard);
        Timer::after(Duration::from_millis(5000)).await;
    }
}

fn process_nmea(_buf: &[u8]) { /* parse GPS data */ }
```

---

## Error Handling and Edge Cases

### Spurious Framing Errors

**Cause:** Switching mid-byte causes the receiving device to see a premature STOP bit.

**Detection (C):**
```c
if (USART1->ISR & USART_ISR_FE) {
    /* Framing error: likely switched mid-frame */
    USART1->ICR = USART_ICR_FECF; /* Clear flag */
    uart_mux_switch_error_count++;
    /* Re-sync: wait for line idle, then retry */
}
```

**Prevention:** Always poll `TXE` (TX Empty) **and** `TC` (Transfer Complete) flags, not just TXE:
```c
/* Wait until shift register is truly empty, not just FIFO */
while (!(USART1->ISR & USART_ISR_TC));
```

### Glitches on RX During Channel Switch

When the mux de-asserts one device and asserts another, the RX line may glitch if both devices' TX lines are driven simultaneously (before break-before-make completes). The MCU UART can misinterpret the glitch as a start bit.

**Mitigation:** Disable UART RX interrupt during the switch window:
```c
void safe_mux_switch(uart_mux_t *mux, uint8_t new_channel) {
    __disable_irq();             /* Critical section */
    USART1->CR1 &= ~USART_CR1_RXNEIE; /* Disable RX interrupt */
    uart_mux_select(mux, new_channel);
    USART1->RDR;                 /* Flush any junk byte latched during switch */
    USART1->ICR = 0x121F55;     /* Clear all error flags */
    USART1->CR1 |= USART_CR1_RXNEIE;  /* Re-enable RX interrupt */
    __enable_irq();
}
```

### Channel Isolation Leakage

Analog switches have finite off-state isolation (typically –60 to –80 dB). At high baud rates, crosstalk from an inactive channel's transitions can cause bit errors. If this is observed:
- Use a mux with higher isolation rating
- Add Schmitt-trigger buffers on the device TX outputs to sharpen edges

---

## Testing and Validation

### Loopback Self-Test (C)

Connect an unused mux channel back to itself (TX to RX via a jumper). Use this for startup self-test:

```c
#define LOOPBACK_CHANNEL  7  /* Reserved for self-test */
#define LOOPBACK_PATTERN  0xA5

bool uart_mux_self_test(uart_mux_t *mux)
{
    uart_mux_select(mux, LOOPBACK_CHANNEL);

    uint8_t tx_byte = LOOPBACK_PATTERN;
    uint8_t rx_byte = 0x00;

    uart_send_byte(tx_byte);

    uint32_t timeout = 1000; /* 1 ms */
    while (!uart_rx_available() && timeout--) {
        delay_us(1);
    }

    if (timeout == 0) {
        uart_mux_deselect(mux);
        return false; /* RX timeout */
    }

    rx_byte = uart_read_byte();
    uart_mux_deselect(mux);

    return (rx_byte == tx_byte);
}
```

### Channel Switch Timing Measurement

Use a logic analyzer trigger to measure the gap between the last byte of channel N and the first byte of channel N+1. This gap must be:
```
t_gap = t_UART_idle_wait + t_gpio_write + t_prop_delay
      ≥ 2 × (1 bit period at current baud)   [safety margin]
```

For automated regression testing, a second microcontroller can act as a "mux tester" — counting framing errors per channel over thousands of switches.

---

## Summary

UART Multiplexing is a cost-effective and widely applicable technique for extending the number of UART-connected devices beyond the physical peripheral count of a microcontroller.

**Key hardware takeaways:**
- Use bidirectional analog switches (TMUX1208, TS5A23157, SN74LVC4066) for full TX+RX muxing
- Choose mux ICs with break-before-make switching and low RON for signal integrity
- Always pull inactive device TX lines HIGH to prevent spurious framing errors
- Budget for propagation delay in your timing analysis — especially above 115200 baud

**Key firmware takeaways:**
- Always wait for UART TX complete (shift register empty, not just FIFO) before switching
- Flush / ignore the RX byte captured during a mux switch glitch
- In RTOS environments, protect the mux with a mutex and serialize all transactions
- Use DMA + completion callbacks for high-throughput multi-channel designs

**Rust-specific advantages:**
- Type-state patterns enforce correct mux usage at compile time (no channel selected = no UART access)
- Async mutexes + Embassy tasks provide clean cooperative multitasking without raw IRQ management
- Zero-cost abstractions mean the type-state overhead is entirely compile-time

**C/C++ best practices:**
- Centralize all mux switching through a single driver function to prevent accidental mid-frame switches
- Log switch count and timeout count as runtime diagnostics
- Add a loopback self-test channel for factory and power-on validation

UART multiplexing scales well from 2 channels (a single SPDT switch costing cents) up to 16+ channels using cascaded mux ICs, making it a versatile building block in any embedded system designer's toolkit.

---

*Document: 67_UART_Multiplexing.md | Topic: Embedded UART Systems | Covers: C/C++, Rust, Hardware Design*