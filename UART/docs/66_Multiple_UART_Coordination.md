# 66. Multiple UART Coordination

**Hardware Fundamentals** — peripheral instance counts for common MCU families (STM32, RP2040, ESP32, nRF5340), pin multiplexing constraints, and independent baud clock behaviour.

**Architecture Patterns** — four progressive patterns from naive polling through interrupt ring buffers, DMA circular mode, and RTOS-per-task, with diagrams showing data flow.

**C/C++ Code Examples:**
- *Bare-metal register-level* driver for STM32F4 — three UARTs with ISR handlers, ring buffers, and baud rate macros
- *C++17 `MultiUartManager<N>` template* — per-channel line callbacks, statistics, and FreeRTOS-compatible design
- *DMA + IDLE-line interrupt* driver — circular DMA with wrap-around copy and double-buffer ping-pong

**Rust Code Examples:**
- *Embassy async tasks* — each UART as an independent `#[embassy_executor::task]`, with GPS NMEA parsing, radio packet framing, and a debug console
- *RTIC v2* — hardware-backed preemptive priorities, SPSC queues between ISRs and processing tasks

**Supporting sections** cover buffer sizing formulas, double buffering, NVIC priority ladder tables, per-channel error recovery, and three real-world use cases (industrial gateway, drone FC, automotive ECU).

## Managing Multiple UART Peripherals in a Single System

---

## Table of Contents

1. [Introduction](#introduction)
2. [Hardware Fundamentals](#hardware-fundamentals)
3. [Architecture Patterns](#architecture-patterns)
4. [C/C++ Implementation](#cc-implementation)
   - [Register-Level (Bare Metal)](#register-level-bare-metal)
   - [Interrupt-Driven Multi-UART Manager](#interrupt-driven-multi-uart-manager)
   - [DMA-Based Multi-UART](#dma-based-multi-uart)
5. [Rust Implementation](#rust-implementation)
   - [Async Multi-UART with Embassy](#async-multi-uart-with-embassy)
   - [RTIC-Based Coordination](#rtic-based-coordination)
6. [Buffer Management Strategies](#buffer-management-strategies)
7. [Priority and Arbitration](#priority-and-arbitration)
8. [Error Handling Across UARTs](#error-handling-across-uarts)
9. [Real-World Use Cases](#real-world-use-cases)
10. [Summary](#summary)

---

## Introduction

Modern embedded systems frequently require communication with multiple external devices simultaneously — a GPS module, a wireless transceiver, a debug console, and an RS-485 bus might all need dedicated UART channels on a single MCU. Managing multiple UART peripherals introduces challenges that simply do not exist with a single channel:

- **Resource contention**: CPU time and DMA channels must be shared fairly.
- **Timing sensitivity**: High-baud-rate channels can flood buffers while slower channels starve.
- **Error isolation**: A framing error on one UART must not stall processing on another.
- **Memory pressure**: Each channel requires its own ring buffer or DMA descriptor.
- **Synchronisation**: Tasks reading from one UART may need data correlated with another.

This document covers the full lifecycle of multi-UART design — hardware selection, software architecture, interrupt/DMA coordination, and idiomatic implementations in both **C/C++** and **Rust**.

---

## Hardware Fundamentals

### Peripheral Instances

Most Cortex-M MCUs expose between two and eight hardware UART/USART instances. Examples:

| MCU Family          | UART Count | Notes                                  |
|---------------------|------------|----------------------------------------|
| STM32F4xx           | 6 (USART)  | USART1/2/3 + UART4/5/6                 |
| STM32H7xx           | 8          | Includes LPUART                        |
| NXP i.MX RT1060     | 8 (LPUART) | All support DMA                        |
| RP2040              | 2 (UART)   | Extendable via PIO                     |
| ESP32               | 3 (UART)   | UART0 used by ROM bootloader           |
| nRF5340             | 2 (UARTE)  | EasyDMA always on                      |

### Pin Multiplexing Constraints

Hardware UARTs are bound to specific GPIO banks via the alternate-function (AF) matrix. Always consult the datasheet's pinout table before assigning UARTs to avoid AF conflicts with SPI, I2C, or timer peripherals sharing the same pins.

### Clock and Baud Rate Independence

Each UART peripheral derives its baud rate from an independent APB/AHB clock divider. With multiple UARTs you can simultaneously run:

```
UART1: 115200 baud  → GPS NMEA stream
UART2: 1000000 baud → GNSS binary protocol
UART3: 9600 baud    → Legacy sensor
UART4: 3000000 baud → High-speed radio link
```

There is no hardware coupling between their baud generators.

---

## Architecture Patterns

### Pattern 1 — Polled (Simplest, Not Recommended for Multi-UART)

```
while(1) {
    poll UART1 → process
    poll UART2 → process
    ...
}
```

Acceptable only for very low baud rates or when all channels are idle most of the time. Missing characters is almost guaranteed at high throughput.

### Pattern 2 — Interrupt-Driven Ring Buffers

Each UART ISR pushes received bytes into a dedicated ring buffer. The main loop (or an RTOS task) drains those buffers at its leisure. This is the most common production pattern.

```
[UART1 ISR] → ring_buf[0]   ← main task drains
[UART2 ISR] → ring_buf[1]   ← main task drains
[UART3 ISR] → ring_buf[2]   ← dedicated task drains
```

### Pattern 3 — DMA Circular Buffers

The DMA controller fills a memory buffer autonomously. The CPU is only woken on half-transfer and transfer-complete interrupts. This is the lowest CPU-overhead approach and is essential for channels above ~500 kbaud.

```
UART1 RX  →  DMA1 Stream5  →  dma_buf1[256]
UART2 RX  →  DMA1 Stream2  →  dma_buf2[256]
UART4 RX  →  DMA1 Stream4  →  dma_buf4[256]
```

### Pattern 4 — RTOS Task-Per-UART

Each UART gets its own RTOS task (FreeRTOS, Zephyr, Embassy). Tasks block on semaphores/channels, eliminating polling entirely. Priority assignment controls which UART has first claim on the CPU.

---

## C/C++ Implementation

### Register-Level (Bare Metal)

This example targets **STM32F4**, initialising three UARTs at different baud rates using direct register access (no HAL).

```c
/*
 * multi_uart_bare.h
 * Bare-metal multi-UART driver for STM32F4
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define UART_BUF_SIZE  256   /* Must be a power of 2 */
#define UART_BUF_MASK  (UART_BUF_SIZE - 1)
#define NUM_UARTS      3

typedef struct {
    volatile uint8_t  buf[UART_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    uint32_t          overrun_count;
} UartRingBuf;

typedef enum {
    UART_ID_1 = 0,   /* Debug console  115200 */
    UART_ID_2 = 1,   /* GPS module     9600   */
    UART_ID_3 = 2,   /* Radio link     500000 */
} UartId;

/* Public API */
void     multi_uart_init(void);
bool     uart_rx_available(UartId id);
uint8_t  uart_rx_get(UartId id);
void     uart_tx_byte(UartId id, uint8_t byte);
void     uart_tx_buf(UartId id, const uint8_t *data, uint16_t len);
uint32_t uart_overrun_count(UartId id);
```

```c
/*
 * multi_uart_bare.c
 * Bare-metal multi-UART driver — STM32F4 register-level
 *
 * Pinout assumed:
 *   UART1: PA9 (TX), PA10 (RX)  — APB2, 84 MHz
 *   UART2: PA2 (TX), PA3  (RX)  — APB1, 42 MHz
 *   UART3: PB10(TX), PB11 (RX)  — APB1, 42 MHz
 */
#include "multi_uart_bare.h"
#include "stm32f4xx.h"   /* CMSIS device header */

/* ── Ring buffer instances ─────────────────────────────────── */
static UartRingBuf rx_buf[NUM_UARTS];

/* ── Baud rate calculation ─────────────────────────────────── */
/*  USARTDIV = fck / (16 * baud)  for over8 = 0               */
#define BRR_APB2(baud)  ((84000000UL + (baud)/2) / (baud))
#define BRR_APB1(baud)  ((42000000UL + (baud)/2) / (baud))

/* ── Clock & GPIO init ─────────────────────────────────────── */
static void clocks_init(void)
{
    /* Enable GPIO clocks */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;

    /* Enable UART clocks */
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN | RCC_APB1ENR_USART3EN;

    __DSB();  /* Ensure clock enable has propagated */
}

static void gpio_uart_init(void)
{
    /* ── PA9/PA10 → UART1 (AF7) ── */
    GPIOA->MODER  |=  (2u << 18) | (2u << 20);      /* Alternate function */
    GPIOA->OSPEEDR|=  (3u << 18) | (3u << 20);      /* High speed         */
    GPIOA->AFR[1] |=  (7u <<  4) | (7u <<  8);      /* AF7 = USART1       */

    /* ── PA2/PA3  → UART2 (AF7) ── */
    GPIOA->MODER  |=  (2u <<  4) | (2u <<  6);
    GPIOA->OSPEEDR|=  (3u <<  4) | (3u <<  6);
    GPIOA->AFR[0] |=  (7u <<  8) | (7u << 12);

    /* ── PB10/PB11 → UART3 (AF7) ── */
    GPIOB->MODER  |=  (2u << 20) | (2u << 22);
    GPIOB->OSPEEDR|=  (3u << 20) | (3u << 22);
    GPIOB->AFR[1] |=  (7u <<  8) | (7u << 12);
}

/* ── UART peripheral init ──────────────────────────────────── */
static void uart_peripheral_init(USART_TypeDef *uart,
                                  uint32_t brr_val)
{
    uart->CR1 = 0;                      /* Reset; disable */
    uart->BRR = brr_val;
    uart->CR2 = 0;
    uart->CR3 = 0;
    uart->CR1 = USART_CR1_UE            /* Enable UART    */
              | USART_CR1_RE            /* RX enable      */
              | USART_CR1_TE            /* TX enable      */
              | USART_CR1_RXNEIE;       /* RX interrupt   */
}

void multi_uart_init(void)
{
    clocks_init();
    gpio_uart_init();

    uart_peripheral_init(USART1, BRR_APB2(115200));
    uart_peripheral_init(USART2, BRR_APB1(9600));
    uart_peripheral_init(USART3, BRR_APB1(500000));

    /* Configure NVIC priorities — lower number = higher priority */
    NVIC_SetPriority(USART1_IRQn, 5);
    NVIC_SetPriority(USART2_IRQn, 6);
    NVIC_SetPriority(USART3_IRQn, 6);

    NVIC_EnableIRQ(USART1_IRQn);
    NVIC_EnableIRQ(USART2_IRQn);
    NVIC_EnableIRQ(USART3_IRQn);
}

/* ── Generic ISR handler (called from each IRQ) ────────────── */
static void uart_isr_handler(USART_TypeDef *uart, UartRingBuf *rb)
{
    uint32_t sr = uart->SR;

    if (sr & USART_SR_RXNE)
    {
        uint8_t  byte    = (uint8_t)uart->DR;   /* Clears RXNE */
        uint16_t next    = (rb->head + 1) & UART_BUF_MASK;

        if (next != rb->tail) {
            rb->buf[rb->head] = byte;
            rb->head = next;
        } else {
            rb->overrun_count++;
        }
    }

    /* Clear overrun error if set (otherwise RXNE will stop firing) */
    if (sr & USART_SR_ORE) {
        (void)uart->DR;
    }
}

/* ── IRQ handlers ──────────────────────────────────────────── */
void USART1_IRQHandler(void) { uart_isr_handler(USART1, &rx_buf[UART_ID_1]); }
void USART2_IRQHandler(void) { uart_isr_handler(USART2, &rx_buf[UART_ID_2]); }
void USART3_IRQHandler(void) { uart_isr_handler(USART3, &rx_buf[UART_ID_3]); }

/* ── Public API ────────────────────────────────────────────── */
bool uart_rx_available(UartId id)
{
    return rx_buf[id].head != rx_buf[id].tail;
}

uint8_t uart_rx_get(UartId id)
{
    UartRingBuf *rb = &rx_buf[id];
    /* Caller must check uart_rx_available() first */
    uint8_t byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) & UART_BUF_MASK;
    return byte;
}

static USART_TypeDef *uart_instance(UartId id)
{
    static USART_TypeDef *const inst[NUM_UARTS] = {
        USART1, USART2, USART3
    };
    return inst[id];
}

void uart_tx_byte(UartId id, uint8_t byte)
{
    USART_TypeDef *u = uart_instance(id);
    while (!(u->SR & USART_SR_TXE));   /* Spin until TX empty */
    u->DR = byte;
}

void uart_tx_buf(UartId id, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uart_tx_byte(id, data[i]);
    }
}

uint32_t uart_overrun_count(UartId id)
{
    return rx_buf[id].overrun_count;
}
```

---

### Interrupt-Driven Multi-UART Manager

This higher-level C++ class wraps multiple UART channels with per-channel callbacks, suitable for use with **FreeRTOS** or bare-metal cooperative scheduling.

```cpp
/*
 * MultiUartManager.hpp
 * C++17 multi-UART coordinator with per-channel line callback
 */
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

template<std::size_t N_UARTS, std::size_t BUF_SIZE = 512>
class MultiUartManager
{
public:
    /* Callback invoked when a complete '\n'-terminated line is received */
    using LineCallback = std::function<void(uint8_t channel, std::string_view line)>;

    struct ChannelConfig {
        uint32_t    baud_rate;
        bool        use_dma;
        uint8_t     irq_priority;   /* 0 = highest on ARM Cortex-M */
        LineCallback on_line;
    };

    /* Register a channel before calling init() */
    void configure_channel(uint8_t ch, ChannelConfig cfg)
    {
        if (ch < N_UARTS) {
            m_config[ch] = std::move(cfg);
        }
    }

    /* Called from board-support init code */
    void init(void)
    {
        for (std::size_t i = 0; i < N_UARTS; ++i) {
            m_rx_buf[i].head = m_rx_buf[i].tail = 0;
            m_line_pos[i] = 0;
            platform_uart_init(static_cast<uint8_t>(i), m_config[i]);
        }
    }

    /* Called from each UART ISR — push one received byte */
    void push_byte(uint8_t channel, uint8_t byte)
    {
        auto &rb = m_rx_buf[channel];
        uint16_t next = (rb.head + 1) & (BUF_SIZE - 1);
        if (next != rb.tail) {
            rb.data[rb.head] = byte;
            rb.head = next;
        } else {
            ++m_stats[channel].overruns;
        }
    }

    /* Call from main loop / RTOS task to drain buffers */
    void process(void)
    {
        for (uint8_t ch = 0; ch < N_UARTS; ++ch) {
            drain_channel(ch);
        }
    }

    struct Stats {
        uint32_t bytes_rx  = 0;
        uint32_t bytes_tx  = 0;
        uint32_t overruns  = 0;
        uint32_t framing_errors = 0;
        uint32_t lines_dispatched = 0;
    };

    const Stats &stats(uint8_t ch) const { return m_stats[ch]; }

private:
    struct RingBuf {
        uint8_t  data[BUF_SIZE];
        volatile uint16_t head;
        volatile uint16_t tail;
    };

    void drain_channel(uint8_t ch)
    {
        auto &rb = m_rx_buf[ch];
        auto &lbuf = m_line_buf[ch];

        while (rb.tail != rb.head) {
            uint8_t byte = rb.data[rb.tail];
            rb.tail = (rb.tail + 1) & (BUF_SIZE - 1);
            ++m_stats[ch].bytes_rx;

            if (byte == '\n' || byte == '\r') {
                if (m_line_pos[ch] > 0) {
                    lbuf[m_line_pos[ch]] = '\0';
                    std::string_view line{
                        reinterpret_cast<char *>(lbuf.data()),
                        m_line_pos[ch]
                    };
                    if (m_config[ch].on_line) {
                        m_config[ch].on_line(ch, line);
                        ++m_stats[ch].lines_dispatched;
                    }
                    m_line_pos[ch] = 0;
                }
            } else if (m_line_pos[ch] < BUF_SIZE - 1) {
                lbuf[m_line_pos[ch]++] = byte;
            }
        }
    }

    /* Platform-specific hook (implemented in BSP) */
    extern void platform_uart_init(uint8_t channel,
                                    const ChannelConfig &cfg);

    std::array<ChannelConfig, N_UARTS> m_config{};
    std::array<RingBuf,       N_UARTS> m_rx_buf{};
    std::array<Stats,         N_UARTS> m_stats{};
    std::array<std::array<uint8_t, BUF_SIZE>, N_UARTS> m_line_buf{};
    std::array<uint16_t, N_UARTS> m_line_pos{};
};
```

```cpp
/*
 * main.cpp — Usage example with MultiUartManager
 */
#include "MultiUartManager.hpp"
#include <cstdio>

/* System with 3 UART channels */
static MultiUartManager<3> g_uart_mgr;

void app_uart_init(void)
{
    /* GPS module on UART0 */
    g_uart_mgr.configure_channel(0, {
        .baud_rate    = 9600,
        .use_dma      = false,
        .irq_priority = 6,
        .on_line = [](uint8_t ch, std::string_view line) {
            if (line.starts_with("$GPRMC")) {
                /* Parse NMEA sentence */
                printf("[GPS] %.*s\n", (int)line.size(), line.data());
            }
        }
    });

    /* Wireless radio on UART1 */
    g_uart_mgr.configure_channel(1, {
        .baud_rate    = 500000,
        .use_dma      = true,
        .irq_priority = 4,   /* Higher priority than GPS */
        .on_line = [](uint8_t ch, std::string_view line) {
            printf("[Radio] %.*s\n", (int)line.size(), line.data());
        }
    });

    /* Debug console on UART2 */
    g_uart_mgr.configure_channel(2, {
        .baud_rate    = 115200,
        .use_dma      = false,
        .irq_priority = 7,
        .on_line = [](uint8_t ch, std::string_view line) {
            printf("[DBG CMD] %.*s\n", (int)line.size(), line.data());
        }
    });

    g_uart_mgr.init();
}

/* Called from each UART ISR */
extern "C" void USART1_IRQHandler(void) {
    g_uart_mgr.push_byte(0, USART1->DR & 0xFF);
}
extern "C" void USART2_IRQHandler(void) {
    g_uart_mgr.push_byte(1, USART2->DR & 0xFF);
}
extern "C" void USART3_IRQHandler(void) {
    g_uart_mgr.push_byte(2, USART3->DR & 0xFF);
}

int main(void)
{
    app_uart_init();

    while (true) {
        g_uart_mgr.process();   /* Drain all ring buffers */
        /* Other system tasks ... */
    }
}
```

---

### DMA-Based Multi-UART

DMA circular mode nearly eliminates CPU interrupt load for RX. The pattern below uses STM32 HAL idioms, applicable to any MCU with DMA-capable UARTs.

```c
/*
 * dma_multi_uart.c
 * DMA circular RX for three UARTs, with IDLE-line detection
 * for packet boundary recognition.
 *
 * Key concept: UART IDLE interrupt fires when the RX line is
 * silent for one full character time — ideal for detecting
 * end-of-packet on variable-length protocols.
 */
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdbool.h>

#define DMA_BUF_SIZE   256

typedef struct {
    UART_HandleTypeDef *huart;
    DMA_HandleTypeDef  *hdma_rx;
    uint8_t             dma_buf[DMA_BUF_SIZE];
    uint8_t             proc_buf[DMA_BUF_SIZE]; /* Double-buffer for processing */
    uint16_t            last_dma_pos;
    void (*on_data)(const uint8_t *data, uint16_t len, uint8_t ch_id);
    uint8_t             ch_id;
} DmaUartChannel;

/* ── Channel table (populated in init) ────────────────────── */
#define MAX_DMA_CHANNELS  3
static DmaUartChannel s_channels[MAX_DMA_CHANNELS];
static uint8_t        s_channel_count = 0;

/* ── Register a channel ────────────────────────────────────── */
void dma_uart_register(UART_HandleTypeDef *huart,
                        DMA_HandleTypeDef  *hdma_rx,
                        void (*on_data)(const uint8_t*, uint16_t, uint8_t),
                        uint8_t ch_id)
{
    if (s_channel_count >= MAX_DMA_CHANNELS) return;

    DmaUartChannel *ch = &s_channels[s_channel_count++];
    ch->huart        = huart;
    ch->hdma_rx      = hdma_rx;
    ch->last_dma_pos = 0;
    ch->on_data      = on_data;
    ch->ch_id        = ch_id;

    /* Start DMA circular reception */
    HAL_UART_Receive_DMA(huart, ch->dma_buf, DMA_BUF_SIZE);

    /* Enable IDLE line interrupt */
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);
}

/* ── Process one channel — called from IDLE ISR ───────────── */
static void dma_channel_process(DmaUartChannel *ch)
{
    /* Current DMA write position (head) */
    uint16_t dma_pos = DMA_BUF_SIZE
                     - (uint16_t)__HAL_DMA_GET_COUNTER(ch->hdma_rx);

    if (dma_pos == ch->last_dma_pos) return;  /* Nothing new */

    uint16_t bytes_available;
    if (dma_pos > ch->last_dma_pos) {
        /* Linear region — no wrap */
        bytes_available = dma_pos - ch->last_dma_pos;
        memcpy(ch->proc_buf,
               &ch->dma_buf[ch->last_dma_pos],
               bytes_available);
    } else {
        /* Wrapped around circular buffer */
        uint16_t tail_len = DMA_BUF_SIZE - ch->last_dma_pos;
        uint16_t head_len = dma_pos;
        bytes_available   = tail_len + head_len;

        memcpy(ch->proc_buf,
               &ch->dma_buf[ch->last_dma_pos],
               tail_len);
        memcpy(&ch->proc_buf[tail_len],
               ch->dma_buf,
               head_len);
    }

    ch->last_dma_pos = dma_pos;

    if (ch->on_data && bytes_available > 0) {
        ch->on_data(ch->proc_buf, bytes_available, ch->ch_id);
    }
}

/* ── UART IRQ dispatch — called from each HAL IRQ handler ─── */
void dma_uart_irq_handler(UART_HandleTypeDef *huart)
{
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(huart);

        /* Find which channel this UART belongs to */
        for (uint8_t i = 0; i < s_channel_count; ++i) {
            if (s_channels[i].huart == huart) {
                dma_channel_process(&s_channels[i]);
                break;
            }
        }
    }
}

/* ── Usage example ─────────────────────────────────────────── */
static void on_uart1_data(const uint8_t *data, uint16_t len, uint8_t ch)
{
    /* Process GPS NMEA data */
    (void)ch;
    for (uint16_t i = 0; i < len; i++) {
        nmea_parser_push(data[i]);
    }
}

static void on_uart2_data(const uint8_t *data, uint16_t len, uint8_t ch)
{
    /* Forward radio packet to queue */
    radio_queue_push(data, len);
}

void system_uart_init(void)
{
    /* huart1/huart2 initialised by CubeMX or BSP */
    dma_uart_register(&huart1, &hdma_usart1_rx, on_uart1_data, 0);
    dma_uart_register(&huart2, &hdma_usart2_rx, on_uart2_data, 1);
}

/* STM32 UART IRQ handlers */
void USART1_IRQHandler(void)
{
    dma_uart_irq_handler(&huart1);
    HAL_UART_IRQHandler(&huart1);
}
void USART2_IRQHandler(void)
{
    dma_uart_irq_handler(&huart2);
    HAL_UART_IRQHandler(&huart2);
}
```

---

## Rust Implementation

### Async Multi-UART with Embassy

[Embassy](https://embassy.dev/) is the leading async embedded framework for Rust. Each UART runs as an independent async task, eliminating the need for manual ring buffers or ISR dispatch tables.

```rust
// multi_uart_embassy/src/main.rs
//
// Embassy async multi-UART example for STM32F4
// Cargo.toml deps:
//   embassy-stm32 = { version = "0.1", features = ["stm32f446re", "time-driver-any"] }
//   embassy-executor = { version = "0.5", features = ["arch-cortex-m", "executor-thread"] }
//   embassy-time = "0.3"
//   heapless = "0.8"
//   static_cell = "2"

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::usart::{Config as UartConfig, Uart};
use embassy_stm32::{bind_interrupts, peripherals, usart};
use embassy_time::{Duration, Instant};
use heapless::Vec;
use static_cell::StaticCell;

// Bind interrupt handlers to embassy's async UART driver
bind_interrupts!(struct Irqs {
    USART1 => usart::InterruptHandler<peripherals::USART1>;
    USART2 => usart::InterruptHandler<peripherals::USART2>;
    USART3 => usart::InterruptHandler<peripherals::USART3>;
});

// ── Task: GPS receiver on UART2 ──────────────────────────────
#[embassy_executor::task]
async fn gps_task(mut uart: Uart<'static, peripherals::USART2>) {
    let mut line_buf: Vec<u8, 128> = Vec::new();

    loop {
        let mut byte = [0u8; 1];
        match uart.read(&mut byte).await {
            Ok(_) => {
                let b = byte[0];
                if b == b'\n' {
                    if let Ok(s) = core::str::from_utf8(&line_buf) {
                        if s.starts_with("$GPRMC") {
                            defmt::info!("[GPS] GPRMC: {}", s);
                            // parse_nmea_rmc(s);
                        }
                    }
                    line_buf.clear();
                } else if b != b'\r' {
                    let _ = line_buf.push(b);
                }
            }
            Err(e) => {
                defmt::warn!("[GPS] UART error: {:?}", e);
                // Brief back-off on error to avoid busy loop
                embassy_time::Timer::after(Duration::from_millis(10)).await;
            }
        }
    }
}

// ── Task: Radio link on UART3 ────────────────────────────────
#[embassy_executor::task]
async fn radio_task(mut uart: Uart<'static, peripherals::USART3>) {
    const PACKET_MAGIC: u8 = 0xAA;
    let mut packet_buf: Vec<u8, 256> = Vec::new();
    let mut in_packet = false;
    let mut expected_len: usize = 0;

    loop {
        let mut byte = [0u8; 1];
        if uart.read(&mut byte).await.is_ok() {
            let b = byte[0];

            if !in_packet {
                if b == PACKET_MAGIC {
                    packet_buf.clear();
                    in_packet = true;
                }
            } else {
                if packet_buf.is_empty() {
                    expected_len = b as usize;
                }
                let _ = packet_buf.push(b);

                if packet_buf.len() == expected_len + 1 {
                    // Validate and dispatch packet
                    if validate_checksum(&packet_buf) {
                        defmt::info!("[Radio] Packet OK, {} bytes", packet_buf.len());
                        // dispatch_packet(&packet_buf);
                    } else {
                        defmt::warn!("[Radio] Bad checksum");
                    }
                    in_packet = false;
                }
            }
        }
    }
}

fn validate_checksum(buf: &[u8]) -> bool {
    if buf.is_empty() { return false; }
    let data = &buf[..buf.len() - 1];
    let checksum = buf[buf.len() - 1];
    let computed: u8 = data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b));
    computed == checksum
}

// ── Task: Debug console on UART1 ────────────────────────────
#[embassy_executor::task]
async fn debug_console_task(mut uart: Uart<'static, peripherals::USART1>) {
    let mut line: Vec<u8, 64> = Vec::new();

    loop {
        let mut byte = [0u8; 1];
        if uart.read(&mut byte).await.is_ok() {
            match byte[0] {
                b'\n' | b'\r' => {
                    if !line.is_empty() {
                        process_console_command(&line);
                        line.clear();
                    }
                }
                b => {
                    // Echo back
                    let _ = uart.write(&[b]).await;
                    let _ = line.push(b);
                }
            }
        }
    }
}

fn process_console_command(cmd: &[u8]) {
    match cmd {
        b"status" => defmt::info!("System OK"),
        b"reset"  => cortex_m::peripheral::SCB::sys_reset(),
        _         => defmt::warn!("Unknown command"),
    }
}

// ── Entry point ──────────────────────────────────────────────
#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    // Configure UART1 — Debug console 115200
    let uart1 = Uart::new(
        p.USART1, p.PA10, p.PA9,
        Irqs, p.DMA2_CH7, p.DMA2_CH2,
        UartConfig::default(),       // default = 115200 8N1
    ).unwrap();

    // Configure UART2 — GPS 9600
    let mut uart2_cfg = UartConfig::default();
    uart2_cfg.baudrate = 9600;
    let uart2 = Uart::new(
        p.USART2, p.PA3, p.PA2,
        Irqs, p.DMA1_CH6, p.DMA1_CH5,
        uart2_cfg,
    ).unwrap();

    // Configure UART3 — Radio 500000
    let mut uart3_cfg = UartConfig::default();
    uart3_cfg.baudrate = 500_000;
    let uart3 = Uart::new(
        p.USART3, p.PB11, p.PB10,
        Irqs, p.DMA1_CH3, p.DMA1_CH1,
        uart3_cfg,
    ).unwrap();

    spawner.spawn(debug_console_task(uart1)).unwrap();
    spawner.spawn(gps_task(uart2)).unwrap();
    spawner.spawn(radio_task(uart3)).unwrap();
}
```

---

### RTIC-Based Coordination

[RTIC](https://rtic.rs/) (Real-Time Interrupt-driven Concurrency) provides a hardware-based scheduler with fine-grained priority control. It is ideal when strict timing guarantees are required between UART channels.

```rust
// multi_uart_rtic/src/main.rs
//
// RTIC v2 multi-UART example
// Each UART has its own interrupt handler at a different priority.
// Higher-priority UARTs can preempt the processing of lower-priority ones.

#![no_std]
#![no_main]

use rtic::app;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{config::Config, Serial},
};
use heapless::spsc::{Consumer, Producer, Queue};

// SPSC queues — one per UART channel
// Must be 'static for safe sharing between ISR and task
static mut UART1_Q: Queue<u8, 256> = Queue::new();
static mut UART2_Q: Queue<u8, 256> = Queue::new();
static mut UART3_Q: Queue<u8, 256> = Queue::new();

#[app(device = stm32f4xx_hal::pac, peripherals = true, dispatchers = [EXTI0, EXTI1])]
mod app {
    use super::*;

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        // RX halves of each serial port
        uart1_rx: stm32f4xx_hal::serial::Rx<pac::USART1>,
        uart2_rx: stm32f4xx_hal::serial::Rx<pac::USART2>,
        uart3_rx: stm32f4xx_hal::serial::Rx<pac::USART3>,

        // Producers (used in ISRs)
        uart1_prod: Producer<'static, u8, 256>,
        uart2_prod: Producer<'static, u8, 256>,
        uart3_prod: Producer<'static, u8, 256>,

        // Consumers (used in processing tasks)
        uart1_cons: Consumer<'static, u8, 256>,
        uart2_cons: Consumer<'static, u8, 256>,
        uart3_cons: Consumer<'static, u8, 256>,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        let dp = ctx.device;

        let rcc = dp.RCC.constrain();
        let clocks = rcc.cfgr.sysclk(168.MHz()).freeze();

        let gpioa = dp.GPIOA.split();
        let gpiob = dp.GPIOB.split();

        // UART1 — Debug 115200
        let uart1 = Serial::new(
            dp.USART1,
            (gpioa.pa9.into_alternate(), gpioa.pa10.into_alternate()),
            Config::default().baudrate(115200.bps()),
            &clocks,
        ).unwrap();
        let (_, uart1_rx) = uart1.split();

        // UART2 — GPS 9600
        let uart2 = Serial::new(
            dp.USART2,
            (gpioa.pa2.into_alternate(), gpioa.pa3.into_alternate()),
            Config::default().baudrate(9600.bps()),
            &clocks,
        ).unwrap();
        let (_, uart2_rx) = uart2.split();

        // UART3 — Radio 500000
        let uart3 = Serial::new(
            dp.USART3,
            (gpiob.pb10.into_alternate(), gpiob.pb11.into_alternate()),
            Config::default().baudrate(500_000.bps()),
            &clocks,
        ).unwrap();
        let (_, uart3_rx) = uart3.split();

        // Safety: queues accessed only via split producers/consumers
        let (uart1_prod, uart1_cons) = unsafe { UART1_Q.split() };
        let (uart2_prod, uart2_cons) = unsafe { UART2_Q.split() };
        let (uart3_prod, uart3_cons) = unsafe { UART3_Q.split() };

        (
            Shared {},
            Local {
                uart1_rx, uart2_rx, uart3_rx,
                uart1_prod, uart2_prod, uart3_prod,
                uart1_cons, uart2_cons, uart3_cons,
            },
        )
    }

    // ── UART ISRs — highest priority, minimal work ──────────
    // Priority 3 is higher than the processing tasks (priority 2)
    // so a byte arriving on the radio link can preempt GPS processing.

    #[task(binds = USART1, local = [uart1_rx, uart1_prod], priority = 2)]
    fn uart1_isr(ctx: uart1_isr::Context) {
        if let Ok(byte) = ctx.local.uart1_rx.read() {
            let _ = ctx.local.uart1_prod.enqueue(byte);
        }
        process_uart1::spawn().ok();
    }

    #[task(binds = USART2, local = [uart2_rx, uart2_prod], priority = 2)]
    fn uart2_isr(ctx: uart2_isr::Context) {
        if let Ok(byte) = ctx.local.uart2_rx.read() {
            let _ = ctx.local.uart2_prod.enqueue(byte);
        }
        process_uart2::spawn().ok();
    }

    // Radio link at highest interrupt priority — preempts others
    #[task(binds = USART3, local = [uart3_rx, uart3_prod], priority = 3)]
    fn uart3_isr(ctx: uart3_isr::Context) {
        if let Ok(byte) = ctx.local.uart3_rx.read() {
            let _ = ctx.local.uart3_prod.enqueue(byte);
        }
        process_uart3::spawn().ok();
    }

    // ── Processing tasks — lower priority ───────────────────

    #[task(local = [uart1_cons, line_buf: heapless::Vec<u8,128> = heapless::Vec::new()], priority = 1)]
    fn process_uart1(ctx: process_uart1::Context) {
        while let Some(byte) = ctx.local.uart1_cons.dequeue() {
            if byte == b'\n' {
                if let Ok(s) = core::str::from_utf8(ctx.local.line_buf) {
                    defmt::info!("[Debug] {}", s);
                }
                ctx.local.line_buf.clear();
            } else {
                let _ = ctx.local.line_buf.push(byte);
            }
        }
    }

    #[task(local = [uart2_cons, nmea_buf: heapless::Vec<u8,128> = heapless::Vec::new()], priority = 1)]
    fn process_uart2(ctx: process_uart2::Context) {
        while let Some(byte) = ctx.local.uart2_cons.dequeue() {
            if byte == b'\n' {
                if ctx.local.nmea_buf.starts_with(b"$GPRMC") {
                    defmt::info!("[GPS] Got RMC sentence");
                }
                ctx.local.nmea_buf.clear();
            } else {
                let _ = ctx.local.nmea_buf.push(byte);
            }
        }
    }

    #[task(local = [uart3_cons], priority = 2)]
    fn process_uart3(ctx: process_uart3::Context) {
        while let Some(byte) = ctx.local.uart3_cons.dequeue() {
            // Radio data dispatched with higher priority
            defmt::trace!("[Radio] 0x{:02X}", byte);
        }
    }
}
```

---

## Buffer Management Strategies

### Ring Buffer Sizing Guidelines

Buffer starvation is the most common multi-UART bug. Size each buffer independently based on the worst-case burst size the channel can produce between main-loop iterations.

```
required_size = max_burst_bytes × safety_factor (1.5–2×)
max_burst_bytes = baud_rate / 10 × max_latency_seconds
```

**Example:** A 500 kbaud UART with up to 5 ms maximum main-loop latency:
```
max_burst = 50000 bytes/s × 0.005 s = 250 bytes
buffer    = 250 × 2 = 512 bytes (round up to power-of-2)
```

### Double Buffering (Ping-Pong)

For DMA-heavy designs, double buffering eliminates data races between the DMA writer and the CPU reader:

```c
/* Two equally-sized DMA buffers per UART */
static uint8_t dma_buf_a[512];   /* DMA writes here while CPU reads B */
static uint8_t dma_buf_b[512];   /* DMA writes here while CPU reads A */
static bool    active_buf = false; /* false = A active, true = B active */

void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    /* First half of active buffer is full — process it */
    process_uart_data(active_buf ? dma_buf_b : dma_buf_a, 256);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* Second half full — process and flip */
    process_uart_data(active_buf ? &dma_buf_b[256] : &dma_buf_a[256], 256);
    active_buf = !active_buf;
}
```

---

## Priority and Arbitration

### Interrupt Priority Mapping

ARM Cortex-M uses a numeric priority scheme where **lower numbers preempt higher numbers**. A consistent priority ladder prevents deadlocks:

| Channel         | Baud Rate   | NVIC Priority | Rationale                        |
|-----------------|-------------|---------------|----------------------------------|
| Radio link      | 500 000     | 3             | High throughput, time-critical   |
| GNSS binary     | 115 200     | 4             | Real-time position data          |
| Debug console   | 115 200     | 5             | Non-critical; can wait           |
| Legacy sensor   | 9 600       | 6             | Very low rate                    |

### CPU Budget Allocation

With RTOS, assign task stack and priority to reflect the data rate:

```c
/* FreeRTOS task creation for each UART */
xTaskCreate(radio_task,   "radio",  512, NULL, tskIDLE_PRIORITY + 5, NULL);
xTaskCreate(gps_task,     "gps",    256, NULL, tskIDLE_PRIORITY + 3, NULL);
xTaskCreate(debug_task,   "debug",  256, NULL, tskIDLE_PRIORITY + 1, NULL);
xTaskCreate(sensor_task,  "sensor", 128, NULL, tskIDLE_PRIORITY + 1, NULL);
```

---

## Error Handling Across UARTs

Each UART can produce independent error events. Errors on one channel must **never** block or corrupt the state of another.

```c
typedef enum {
    UART_ERR_NONE     = 0,
    UART_ERR_OVERRUN  = 1 << 0,   /* Software ring buffer full  */
    UART_ERR_FRAMING  = 1 << 1,   /* Bad stop bit               */
    UART_ERR_PARITY   = 1 << 2,   /* Parity mismatch            */
    UART_ERR_NOISE    = 1 << 3,   /* Noise detected on RX       */
    UART_ERR_DMA      = 1 << 4,   /* DMA transfer error         */
} UartErrorFlags;

typedef struct {
    UartErrorFlags  flags;
    uint32_t        count;
    uint32_t        timestamp_ms;
} UartErrorRecord;

static UartErrorRecord s_errors[NUM_UARTS];

void uart_record_error(uint8_t ch, UartErrorFlags flag)
{
    s_errors[ch].flags       |= flag;
    s_errors[ch].count++;
    s_errors[ch].timestamp_ms = HAL_GetTick();
}

/* Recovery strategy per channel */
void uart_recover_channel(uint8_t ch)
{
    /* 1. Flush the DMA or ring buffer */
    rx_buf[ch].head = rx_buf[ch].tail = 0;

    /* 2. Re-enable the peripheral after error clear */
    USART_TypeDef *u = uart_instance(ch);
    u->CR1 &= ~USART_CR1_UE;
    (void)u->SR;   /* Read SR + DR clears error flags */
    (void)u->DR;
    u->CR1 |= USART_CR1_UE;

    /* 3. Log recovery */
    uart_record_error(ch, UART_ERR_NONE);
    defmt_or_printf("[UART%d] Recovered from error\n", ch);
}
```

In Rust with Embassy, errors are handled at the `await` site and do not propagate across tasks:

```rust
// Rust — per-channel error isolation
loop {
    let mut buf = [0u8; 64];
    match uart.read_until_idle(&mut buf).await {
        Ok(n) => {
            process_data(&buf[..n]);
        }
        Err(embassy_stm32::usart::Error::Overrun) => {
            defmt::warn!("UART overrun — flushing");
            uart.blocking_flush().ok();
            // This task restarts its loop; other tasks are unaffected
        }
        Err(e) => {
            defmt::error!("UART error: {:?}", e);
            embassy_time::Timer::after(Duration::from_millis(50)).await;
        }
    }
}
```

---

## Real-World Use Cases

### Industrial Gateway (Modbus RTU + NMEA + Diagnostics)

```
UART1 (RS-485, 9600,  8E1) → Modbus RTU master — poll 16 slaves
UART2 (TTL,   9600,  8N1) → GPS NMEA for asset tracking
UART3 (TTL, 115200,  8N1) → Cellular modem AT commands
UART4 (TTL, 115200,  8N1) → Debug/firmware upgrade console
```

Key consideration: Modbus RTU requires precise inter-character timing (≤1.5 character times) and inter-frame silence detection (≥3.5 character times), best implemented with a hardware timer combined with the UART IDLE interrupt.

### Drone Flight Controller

```
UART1 (3.3V, 115200) → RC receiver (SBUS/CRSF, inverted signal)
UART2 (3.3V, 115200) → GPS + compass module
UART3 (3.3V, 115200) → ESC telemetry (DSHOT / bidirectional)
UART4 (3.3V, 115200) → FPV OSD chip
UART5 (3.3V,  57600) → Blackbox logger (SD card UART bridge)
```

Key consideration: SBUS is an inverted UART signal — either configure hardware inversion in the UART peripheral register, or add a logic inverter externally.

### Automotive ECU

```
UART1 (LIN bus transceiver, 19200) → Body control module
UART2 (K-Line, 10400)             → OBD-II diagnostics
UART3 (RS-232, 115200)            → Engineering debug port
```

Key consideration: LIN requires a specific break-field detection sequence. Some STM32 UART peripherals support LIN mode natively via `USART_CR2_LINEN`.

---

## Summary

Managing multiple UARTs in a single system is fundamentally an exercise in **resource partitioning and isolation**. The key principles are:

**Architecture** — Choose interrupt-driven ring buffers for simplicity, DMA circular mode for high throughput (>500 kbaud), and async tasks (Embassy/RTIC) for clean, maintainable code in Rust.

**Buffering** — Size each ring buffer independently based on the channel's baud rate and the worst-case scheduling latency. Use double buffering with DMA to avoid data races.

**Priorities** — Assign NVIC interrupt priorities and RTOS task priorities proportional to data rate and time sensitivity. Higher-baud channels should have higher priority to prevent byte loss.

**Error Isolation** — Each UART channel must handle its own overrun, framing, and noise errors without corrupting the state of other channels. Always clear hardware error flags immediately in the ISR.

**Language Choice** — C/C++ with direct register access gives the finest control and lowest overhead. The C++ `MultiUartManager` template provides a clean abstraction without dynamic allocation. Rust with Embassy offers memory-safe, zero-cost async concurrency where each UART literally runs as a cooperative task, making the code structure mirror the hardware topology naturally.

**Testing** — Validate multi-UART systems under worst-case conditions: all channels at maximum baud simultaneously, deliberate injection of framing errors, and buffer-fill stress tests to measure actual overrun rates before deployment.

---

*Document: 66_Multiple_UART_Coordination.md*
*Topic: Embedded Systems — UART Peripherals*
*Languages: C, C++17, Rust (Embassy, RTIC)*