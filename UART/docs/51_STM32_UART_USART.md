# 51. STM32 UART/USART — Detailed Reference

**Hardware & Theory**
- UART vs USART differences, STM32 peripheral architecture diagram, key registers (`BRR`, `CR1`–`CR3`, `SR`/`ISR`), GPIO alternate function table, and baud rate calculation with a worked example.

**C/C++ Code Examples**
- **HAL Polling** — blocking transmit/receive, echo loop, CubeMX init reference
- **HAL Interrupt** — single-byte re-arming pattern, line-accumulation callback
- **HAL DMA** — circular buffer, half/full transfer callbacks, and the **idle-line detection** technique for variable-length frames
- **LL API** — full manual init, byte-level TX/RX, RXNE interrupt with ring buffer integration
- **C++ Wrapper** — RAII `Uart` class with templated methods over HAL handles
- **Printf retargeting** — `_write` override for `printf` over UART

**Rust Code Examples**
- Polling echo with `nb::block!` and `core::fmt::Write`
- Interrupt-driven receive using the `Mutex<RefCell<Option<T>>>` pattern
- DMA TX transfer setup with `stm32f4xx-hal`

**Utilities & Patterns**
- Ring buffer implementation (power-of-2, ISR-safe, zero-copy)
- RS-485 DE pin, hardware flow control (RTS/CTS), LIN mode notes
- Error flag table with clear procedures
- Design decision guide at the end

> **Implementing UART on STM32 microcontrollers with HAL and LL APIs**

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [UART vs USART — Key Differences](#2-uart-vs-usart--key-differences)
3. [STM32 UART Hardware Architecture](#3-stm32-uart-hardware-architecture)
4. [Clock and Baud Rate Configuration](#4-clock-and-baud-rate-configuration)
5. [HAL API — Polling, Interrupt, DMA Modes](#5-hal-api--polling-interrupt-dma-modes)
   - 5.1 [Polling Mode (C)](#51-polling-mode-c)
   - 5.2 [Interrupt Mode (C)](#52-interrupt-mode-c)
   - 5.3 [DMA Mode (C)](#53-dma-mode-c)
6. [LL API — Low-Level Direct Register Access (C)](#6-ll-api--low-level-direct-register-access-c)
7. [C++ UART Wrapper Class](#7-c-uart-wrapper-class)
8. [Rust on STM32 — UART with `stm32f4xx-hal`](#8-rust-on-stm32--uart-with-stm32f4xx-hal)
   - 8.1 [Basic Polling in Rust](#81-basic-polling-in-rust)
   - 8.2 [Interrupt-Driven UART in Rust](#82-interrupt-driven-uart-in-rust)
   - 8.3 [DMA UART in Rust](#83-dma-uart-in-rust)
9. [Common Peripheral Configurations](#9-common-peripheral-configurations)
10. [Error Handling and Status Flags](#10-error-handling-and-status-flags)
11. [Printf Retargeting over UART (C)](#11-printf-retargeting-over-uart-c)
12. [Ring Buffer / Circular RX in C](#12-ring-buffer--circular-rx-in-c)
13. [Summary](#13-summary)

---

## 1. Introduction

UART (Universal Asynchronous Receiver/Transmitter) is one of the most fundamental serial communication
protocols in embedded systems. STM32 microcontrollers provide multiple USART/UART peripherals that can
operate as plain UART (asynchronous) or as USART (synchronous, with a clock line).

STM32 UART peripherals are used for:
- Debug output and logging (e.g., via ST-Link Virtual COM Port)
- Communication with GPS modules, GSM modems, Bluetooth modules (HC-05, HM-10), etc.
- Host–target communication with a PC or Raspberry Pi
- Bootloader protocols (STM32 ROM bootloader uses UART1)
- RS-232 / RS-485 when combined with a level-shifter IC

STM32Cube provides two layers of abstraction:
- **HAL (Hardware Abstraction Layer):** High-level, portable, easy to use; some overhead.
- **LL (Low-Level):** Nearly register-level; maximum performance with minimal overhead.

---

## 2. UART vs USART — Key Differences

| Feature | UART | USART |
|---|---|---|
| Clock | Asynchronous (no clock line) | Can be synchronous (CK pin) or async |
| Data framing | Start + data + parity + stop bits | Same, plus optional synchronous mode |
| Flow control | Optional (RTS/CTS) | Optional (RTS/CTS) |
| STM32 naming | UARTx (e.g., UART4) | USARTx (e.g., USART1) |
| Full-duplex | Yes | Yes (async) / Half-duplex (sync) |

On STM32, **USART** peripherals are a superset: when configured in asynchronous mode they behave
identically to UART. Pure UART peripherals (UART4, UART5, etc.) simply lack the synchronous mode
capability.

---

## 3. STM32 UART Hardware Architecture

```
                  ┌─────────────────────────────────────────────┐
  APB Bus ────────┤  BRR  │  CR1  │  CR2  │  CR3  │  SR/ISR     │
                  │                                             │
  TX Pin ───────  │  TX Shift Register ◄── TDR (Transmit Data)  │
  RX Pin ───────  │  RX Shift Register ──► RDR (Receive Data)   │
                  │                                             │
                  │  NVIC IRQ ──► USARTx_IRQHandler             │
                  │  DMA Request ──► DMA Controller             │
                  └─────────────────────────────────────────────┘
```

**Key registers (STM32F4/F7/H7):**

| Register | Purpose |
|---|---|
| `BRR` | Baud Rate Register — integer + fractional divisor |
| `CR1` | Main control: UE (enable), RE/TE (RX/TX enable), parity, word length, interrupts |
| `CR2` | Stop bits, clock phase/polarity, LIN mode |
| `CR3` | Flow control (RTS/CTS), DMA enable, half-duplex |
| `SR` / `ISR` | Status flags: TXE, TC, RXNE, ORE, FE, NE, PE |
| `DR` / `TDR`/`RDR` | Data register (write to transmit, read to receive) |

**Typical GPIO alternate functions (STM32F4 example):**

| USART | TX | RX | AF |
|---|---|---|---|
| USART1 | PA9 | PA10 | AF7 |
| USART2 | PA2 | PA3 | AF7 |
| USART3 | PB10 | PB11 | AF7 |
| UART4 | PA0 | PA1 | AF8 |
| USART6 | PC6 | PC7 | AF8 |

---

## 4. Clock and Baud Rate Configuration

The UART baud rate is derived from the peripheral clock (PCLK1 for USART2/3/4/5/6,
PCLK2 for USART1/6 on some devices):

```
BaudRate = f_PCLK / (8 × (2 − OVER8) × USARTDIV)
```

For OVER8 = 0 (16× oversampling, most common):
```
USARTDIV = f_PCLK / (16 × BaudRate)
```

**Example:** PCLK = 42 MHz, target baud = 115200

```
USARTDIV = 42,000,000 / (16 × 115200) = 22.786
BRR mantissa = 22, fraction = 0.786 × 16 ≈ 13 (0xD)
BRR = (22 << 4) | 13 = 0x016D
```

HAL and LL both compute this automatically from the `BaudRate` field — you rarely set `BRR` manually.

---

## 5. HAL API — Polling, Interrupt, DMA Modes

### 5.1 Polling Mode (C)

Polling is the simplest mode. The CPU actively waits for TX/RX to complete. Suitable for
simple debug output or low-frequency transfers.

```c
/* main.c — HAL UART Polling example (STM32F4, CubeIDE generated init shown separately) */
#include "main.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;  /* generated by CubeMX */

/* ── Transmit a C-string ─────────────────────────────────── */
void UART_SendString(const char *str)
{
    HAL_UART_Transmit(&huart2,
                      (uint8_t *)str,
                      (uint16_t)strlen(str),
                      HAL_MAX_DELAY);  /* blocks until done */
}

/* ── Receive a fixed number of bytes ─────────────────────── */
HAL_StatusTypeDef UART_ReceiveBlocking(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    return HAL_UART_Receive(&huart2, buf, len, timeout_ms);
}

/* ── Echo loop ───────────────────────────────────────────── */
void Echo_Loop(void)
{
    uint8_t rxByte;

    UART_SendString("STM32 UART Echo Ready\r\n");

    while (1)
    {
        /* Wait up to 100 ms for a byte */
        if (HAL_UART_Receive(&huart2, &rxByte, 1, 100) == HAL_OK)
        {
            HAL_UART_Transmit(&huart2, &rxByte, 1, HAL_MAX_DELAY);

            if (rxByte == '\r')
            {
                uint8_t nl = '\n';
                HAL_UART_Transmit(&huart2, &nl, 1, HAL_MAX_DELAY);
            }
        }
    }
}

/* ── Typical CubeMX-generated USART2 init (for reference) ── */
void MX_USART2_UART_Init(void)
{
    huart2.Instance        = USART2;
    huart2.Init.BaudRate   = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits   = UART_STOPBITS_1;
    huart2.Init.Parity     = UART_PARITY_NONE;
    huart2.Init.Mode       = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK)
        Error_Handler();
}
```

**Limitations of polling:**
- CPU is fully blocked during transfer; no other work can be done.
- Poor choice for receiving variable-length or asynchronous data.

---

### 5.2 Interrupt Mode (C)

Interrupt-driven UART frees the CPU during transfer. The HAL disables the interrupt once the
requested number of bytes has been received/sent, then invokes a callback.

```c
/* uart_interrupt.c — HAL UART Interrupt-driven receive */
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart2;

#define RX_BUF_SIZE  64

static uint8_t  rxBuf[RX_BUF_SIZE];
static volatile uint8_t rxDone = 0;

/* ── Start first receive before the main loop ────────────── */
void UART_IT_Init(void)
{
    /* Kick off non-blocking receive for 1 byte at a time */
    HAL_UART_Receive_IT(&huart2, rxBuf, 1);
}

/* ── HAL calls this when requested bytes are received ─────── */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        /* Echo received byte */
        HAL_UART_Transmit_IT(&huart2, rxBuf, 1);

        /* Re-arm for next byte */
        HAL_UART_Receive_IT(&huart2, rxBuf, 1);

        rxDone = 1;
    }
}

/* ── HAL calls this on TX complete ───────────────────────── */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        /* Transmission finished — could signal a semaphore here (RTOS) */
    }
}

/* ── Error callback ──────────────────────────────────────── */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        /* Clear error flags and re-arm */
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        HAL_UART_Receive_IT(&huart2, rxBuf, 1);
    }
}

/* ── IRQ must be routed in stm32f4xx_it.c ─────────────────── */
/* void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); } */
```

**Receiving a complete line (interrupt + buffer accumulation):**

```c
/* Line-oriented receive using a ring approach */
static uint8_t  rxByte;
static char     lineBuf[128];
static uint8_t  lineIdx = 0;

void UART_LineReceive_Start(void)
{
    HAL_UART_Receive_IT(&huart2, &rxByte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) return;

    if (rxByte == '\n' || lineIdx >= sizeof(lineBuf) - 1)
    {
        lineBuf[lineIdx] = '\0';
        lineIdx = 0;
        Process_Line(lineBuf);   /* handle the complete line */
    }
    else if (rxByte != '\r')
    {
        lineBuf[lineIdx++] = (char)rxByte;
    }

    HAL_UART_Receive_IT(&huart2, &rxByte, 1);  /* re-arm */
}
```

---

### 5.3 DMA Mode (C)

DMA (Direct Memory Access) transfers data autonomously without CPU involvement.
Best for high-speed or large data transfers, or when the CPU must do other work simultaneously.

```c
/* uart_dma.c — HAL UART DMA transmit and receive */
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef  hdma_usart2_rx;
extern DMA_HandleTypeDef  hdma_usart2_tx;

#define DMA_RX_BUF_SIZE  256

static uint8_t dmaTxBuf[256];
static uint8_t dmaRxBuf[DMA_RX_BUF_SIZE];

/* ── DMA Transmit ────────────────────────────────────────── */
void UART_DMA_Send(const uint8_t *data, uint16_t len)
{
    /* Copy to DMA buffer (must remain valid until TxCpltCallback) */
    memcpy(dmaTxBuf, data, len);
    HAL_UART_Transmit_DMA(&huart2, dmaTxBuf, len);
}

/* ── Start circular DMA receive (continuous, no re-arming needed) */
void UART_DMA_StartCircularReceive(void)
{
    HAL_UART_Receive_DMA(&huart2, dmaRxBuf, DMA_RX_BUF_SIZE);
}

/* ── DMA Half-Transfer callback (first half of buffer ready) ─ */
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        /* Process first 128 bytes while DMA fills second half */
        Process_Data(dmaRxBuf, DMA_RX_BUF_SIZE / 2);
    }
}

/* ── DMA Full-Transfer callback (second half ready) ──────── */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        /* Process second 128 bytes */
        Process_Data(&dmaRxBuf[DMA_RX_BUF_SIZE / 2], DMA_RX_BUF_SIZE / 2);
    }
}

/* ── Idle-line detection (variable-length DMA receive) ───── */
/* Enable in CubeMX: USART Global Interrupt + IDLE line detection */
void USART2_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart2);

        /* Calculate how many bytes DMA has actually received */
        uint32_t dmaRemaining = __HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
        uint32_t received     = DMA_RX_BUF_SIZE - dmaRemaining;

        if (received > 0)
        {
            HAL_UART_DMAStop(&huart2);
            Process_Data(dmaRxBuf, (uint16_t)received);
            HAL_UART_Receive_DMA(&huart2, dmaRxBuf, DMA_RX_BUF_SIZE); /* restart */
        }
    }
    HAL_UART_IRQHandler(&huart2);
}
```

---

## 6. LL API — Low-Level Direct Register Access (C)

The LL API maps almost directly to hardware registers, giving maximum throughput with
minimal library overhead. It is suitable for performance-critical code or when HAL overhead
must be eliminated.

```c
/* uart_ll.c — LL API UART example (STM32F4, USART2) */
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"

/* ── Initialize USART2 at 115200, 8N1 ────────────────────── */
void LL_UART_Init_Custom(void)
{
    /* 1. Enable clocks */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);

    /* 2. Configure GPIO: PA2=TX, PA3=RX, AF7 */
    LL_GPIO_InitTypeDef gpioInit = {0};
    gpioInit.Pin        = LL_GPIO_PIN_2 | LL_GPIO_PIN_3;
    gpioInit.Mode       = LL_GPIO_MODE_ALTERNATE;
    gpioInit.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
    gpioInit.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpioInit.Pull       = LL_GPIO_PULL_UP;
    gpioInit.Alternate  = LL_GPIO_AF_7;
    LL_GPIO_Init(GPIOA, &gpioInit);

    /* 3. Configure USART2 */
    LL_USART_InitTypeDef uartInit = {0};
    uartInit.BaudRate            = 115200;
    uartInit.DataWidth           = LL_USART_DATAWIDTH_8B;
    uartInit.StopBits            = LL_USART_STOPBITS_1;
    uartInit.Parity              = LL_USART_PARITY_NONE;
    uartInit.TransferDirection   = LL_USART_DIRECTION_TX_RX;
    uartInit.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    uartInit.OverSampling        = LL_USART_OVERSAMPLING_16;
    LL_USART_Init(USART2, &uartInit);

    LL_USART_Enable(USART2);

    /* Wait for TEACK and REACK (F7/H7 devices) */
    /* while (!LL_USART_IsActiveFlag_TEACK(USART2)); */
    /* while (!LL_USART_IsActiveFlag_REACK(USART2)); */
}

/* ── Blocking send single byte ───────────────────────────── */
void LL_UART_SendByte(uint8_t byte)
{
    while (!LL_USART_IsActiveFlag_TXE(USART2))
        ;   /* wait for TX buffer empty */
    LL_USART_TransmitData8(USART2, byte);
}

/* ── Blocking send string ────────────────────────────────── */
void LL_UART_SendString(const char *str)
{
    while (*str)
        LL_UART_SendByte((uint8_t)*str++);

    /* Wait for transmission complete before returning */
    while (!LL_USART_IsActiveFlag_TC(USART2))
        ;
}

/* ── Non-blocking receive check ──────────────────────────── */
int LL_UART_ReadByteNonBlocking(uint8_t *out)
{
    if (LL_USART_IsActiveFlag_RXNE(USART2))
    {
        *out = LL_USART_ReceiveData8(USART2);
        return 1;
    }

    /* Check for overrun error */
    if (LL_USART_IsActiveFlag_ORE(USART2))
        LL_USART_ClearFlag_ORE(USART2);

    return 0;
}

/* ── Enable RXNE interrupt (LL style) ───────────────────── */
void LL_UART_EnableRxInterrupt(void)
{
    LL_USART_EnableIT_RXNE(USART2);

    NVIC_SetPriority(USART2_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
    NVIC_EnableIRQ(USART2_IRQn);
}

/* ── IRQ Handler (LL style, no HAL) ─────────────────────── */
void USART2_IRQHandler(void)
{
    if (LL_USART_IsActiveFlag_RXNE(USART2) &&
        LL_USART_IsEnabledIT_RXNE(USART2))
    {
        uint8_t data = LL_USART_ReceiveData8(USART2);
        /* Put into software ring buffer */
        RingBuf_Push(data);
    }

    if (LL_USART_IsActiveFlag_ORE(USART2))
        LL_USART_ClearFlag_ORE(USART2);
}
```

---

## 7. C++ UART Wrapper Class

A lightweight C++ wrapper provides RAII initialization and type-safe interface over HAL:

```cpp
// Uart.hpp
#pragma once
#include "stm32f4xx_hal.h"
#include <cstdint>
#include <cstring>

class Uart
{
public:
    explicit Uart(UART_HandleTypeDef &handle)
        : m_handle(handle) {}

    /* Initialize must be called once (or rely on CubeMX MX_USARTx_UART_Init) */
    bool init(uint32_t baudRate = 115200)
    {
        m_handle.Init.BaudRate    = baudRate;
        m_handle.Init.WordLength  = UART_WORDLENGTH_8B;
        m_handle.Init.StopBits    = UART_STOPBITS_1;
        m_handle.Init.Parity      = UART_PARITY_NONE;
        m_handle.Init.Mode        = UART_MODE_TX_RX;
        m_handle.Init.HwFlowCtl   = UART_HWCONTROL_NONE;
        m_handle.Init.OverSampling = UART_OVERSAMPLING_16;
        return HAL_UART_Init(&m_handle) == HAL_OK;
    }

    bool write(const uint8_t *data, uint16_t len, uint32_t timeout = HAL_MAX_DELAY)
    {
        return HAL_UART_Transmit(&m_handle, data, len, timeout) == HAL_OK;
    }

    bool writeString(const char *str, uint32_t timeout = HAL_MAX_DELAY)
    {
        return write(reinterpret_cast<const uint8_t *>(str),
                     static_cast<uint16_t>(strlen(str)), timeout);
    }

    bool read(uint8_t *buf, uint16_t len, uint32_t timeout = 1000)
    {
        return HAL_UART_Receive(&m_handle, buf, len, timeout) == HAL_OK;
    }

    bool startReceiveIT(uint8_t *buf, uint16_t len)
    {
        return HAL_UART_Receive_IT(&m_handle, buf, len) == HAL_OK;
    }

    bool startTransmitDMA(uint8_t *buf, uint16_t len)
    {
        return HAL_UART_Transmit_DMA(&m_handle, buf, len) == HAL_OK;
    }

    UART_HandleTypeDef &handle() { return m_handle; }

private:
    UART_HandleTypeDef &m_handle;
};

// ─────────────────────────────────────────────────────────────
// Usage in main.cpp:
//
//   extern UART_HandleTypeDef huart2;
//   Uart uart(huart2);
//
//   uart.writeString("Hello from C++\r\n");
//   uint8_t buf[32];
//   uart.read(buf, sizeof(buf), 500);
// ─────────────────────────────────────────────────────────────
```

---

## 8. Rust on STM32 — UART with `stm32f4xx-hal`

The `stm32f4xx-hal` crate wraps the STM32F4 peripherals in a safe, zero-cost abstraction.
UART is accessed via the `serial` module, which implements `embedded-hal` traits.

**Cargo.toml dependencies:**

```toml
[dependencies]
cortex-m         = "0.7"
cortex-m-rt      = "0.7"
panic-halt       = "0.2"
stm32f4xx-hal    = { version = "0.20", features = ["stm32f401"] }
nb               = "1.0"
heapless         = "0.8"   # for fixed-size String/Vec on no_std
```

---

### 8.1 Basic Polling in Rust

```rust
// src/main.rs — Polling UART on STM32F401
#![no_std]
#![no_main]

use cortex_m_rt::entry;
use panic_halt as _;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{Config, Serial},
};
use core::fmt::Write;

#[entry]
fn main() -> ! {
    // Take ownership of device and core peripherals
    let dp = pac::Peripherals::take().unwrap();
    let cp = cortex_m::Peripherals::take().unwrap();

    // Configure clocks: 84 MHz system clock
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr
        .use_hse(8.MHz())
        .sysclk(84.MHz())
        .pclk1(42.MHz())
        .freeze();

    // Configure delay provider
    let mut delay = cp.SYST.delay(&clocks);

    // Configure GPIO
    let gpioa = dp.GPIOA.split();
    let tx_pin = gpioa.pa2.into_alternate();
    let rx_pin = gpioa.pa3.into_alternate();

    // Configure USART2: 115200 baud, 8N1
    let serial = Serial::new(
        dp.USART2,
        (tx_pin, rx_pin),
        Config::default()
            .baudrate(115200.bps())
            .wordlength_8()
            .parity_none()
            .stopbits(stm32f4xx_hal::serial::StopBits::STOP1),
        &clocks,
    )
    .unwrap();

    let (mut tx, mut rx) = serial.split();

    // Use core::fmt::Write trait for formatted output
    writeln!(tx, "STM32 UART in Rust!\r").unwrap();

    loop {
        // Non-blocking receive using nb::block!
        match nb::block!(rx.read()) {
            Ok(byte) => {
                // Echo the byte back
                nb::block!(tx.write(byte)).ok();
                if byte == b'\r' {
                    nb::block!(tx.write(b'\n')).ok();
                }
            }
            Err(_) => {
                // Error (overrun, framing, etc.) — just continue
            }
        }
    }
}
```

---

### 8.2 Interrupt-Driven UART in Rust

```rust
// src/main.rs — Interrupt-driven UART receive with a static Mutex<RefCell>
#![no_std]
#![no_main]

use cortex_m::interrupt::Mutex;
use cortex_m_rt::entry;
use core::cell::RefCell;
use panic_halt as _;

use stm32f4xx_hal::{
    pac,
    pac::interrupt,
    prelude::*,
    serial::{Config, Rx, Serial, Tx},
};

// Shared state protected by critical section
static G_TX: Mutex<RefCell<Option<Tx<pac::USART2>>>> = Mutex::new(RefCell::new(None));
static G_RX: Mutex<RefCell<Option<Rx<pac::USART2>>>> = Mutex::new(RefCell::new(None));

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();

    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).pclk1(42.MHz()).freeze();

    let gpioa = dp.GPIOA.split();
    let tx_pin = gpioa.pa2.into_alternate();
    let rx_pin = gpioa.pa3.into_alternate();

    let mut serial = Serial::new(
        dp.USART2,
        (tx_pin, rx_pin),
        Config::default().baudrate(115200.bps()),
        &clocks,
    )
    .unwrap();

    // Enable the RXNE interrupt in the USART peripheral
    serial.listen(stm32f4xx_hal::serial::Event::Rxne);
    let (tx, rx) = serial.split();

    // Move TX and RX into global statics
    cortex_m::interrupt::free(|cs| {
        G_TX.borrow(cs).replace(Some(tx));
        G_RX.borrow(cs).replace(Some(rx));
    });

    // Enable USART2 interrupt in NVIC
    unsafe { pac::NVIC::unmask(pac::Interrupt::USART2) };

    loop {
        // Main loop is free to do other work
        cortex_m::asm::wfi();  // Wait for interrupt (power-saving)
    }
}

// Interrupt handler
#[interrupt]
fn USART2() {
    cortex_m::interrupt::free(|cs| {
        let mut rx_ref = G_RX.borrow(cs).borrow_mut();
        let mut tx_ref = G_TX.borrow(cs).borrow_mut();

        if let (Some(rx), Some(tx)) = (rx_ref.as_mut(), tx_ref.as_mut()) {
            if let Ok(byte) = rx.read() {
                // Echo the received byte
                nb::block!(tx.write(byte)).ok();
            }
        }
    });
}
```

---

### 8.3 DMA UART in Rust

```rust
// src/main.rs — DMA-based UART TX using stm32f4xx-hal
#![no_std]
#![no_main]

use cortex_m_rt::entry;
use panic_halt as _;
use stm32f4xx_hal::{
    dma::{StreamsTuple, config::DmaConfig, Transfer, MemoryToPeripheral},
    pac,
    prelude::*,
    serial::{Config, Serial, Tx},
};

// Static buffer for DMA (must be 'static for safe DMA operation)
static TX_BUFFER: &[u8] = b"Hello from DMA UART!\r\n";

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();

    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).pclk1(42.MHz()).freeze();

    let gpioa = dp.GPIOA.split();

    let serial = Serial::new(
        dp.USART2,
        (gpioa.pa2.into_alternate(), gpioa.pa3.into_alternate()),
        Config::default().baudrate(115200.bps()),
        &clocks,
    )
    .unwrap();

    let (tx, _rx) = serial.split();

    // Set up DMA1 streams
    let streams = StreamsTuple::new(dp.DMA1);
    let dma_stream = streams.6;  // DMA1 Stream6 = USART2 TX

    let dma_config = DmaConfig::default()
        .memory_increment(true)
        .transfer_complete_interrupt(true);

    // Create a DMA transfer: memory (TX_BUFFER) → USART2 TX
    let mut transfer = Transfer::init_memory_to_peripheral(
        dma_stream,
        tx,
        TX_BUFFER,
        None,
        dma_config,
    );

    // Start the transfer
    transfer.start(|_serial| {});

    // Wait for completion
    while !transfer.is_idle() {
        cortex_m::asm::nop();
    }

    loop {
        cortex_m::asm::wfi();
    }
}
```

---

## 9. Common Peripheral Configurations

### 9.1 RS-485 Half-Duplex (DE Pin)

STM32 USART supports a driver-enable (DE) pin for RS-485 transceivers (e.g., MAX485):

```c
/* HAL — enable RS-485 DE pin via CR3 */
huart2.AdvancedInit.AdvFeatureInit  = UART_ADVFEATURE_TXINV_INIT;
__HAL_UART_ENABLE_IT(&huart2, UART_IT_TC);

/* In CR3: set DEM bit for hardware DE control */
SET_BIT(USART2->CR3, USART_CR3_DEM);

/* DE pin = PA1, AF7, push-pull */
/* Configure in GPIO init as alternate function */
```

### 9.2 Hardware Flow Control (RTS/CTS)

```c
huart1.Init.HwFlowCtl = UART_HWCONTROL_RTS_CTS;
/* GPIO: configure CTS (input) and RTS (output) as AF */
```

### 9.3 LIN Mode

```c
/* CR2: enable LIN mode */
SET_BIT(USART1->CR2, USART_CR2_LINEN);
/* LBDL: LIN break detection length (10 or 11 bits) */
```

---

## 10. Error Handling and Status Flags

| Flag | Meaning | How to clear |
|---|---|---|
| `RXNE` | RX buffer not empty (data ready) | Read `DR`/`RDR` |
| `TXE` | TX buffer empty (can write new byte) | Write `DR`/`TDR` |
| `TC` | Transmission complete (shift reg empty) | Read SR then write DR, or `__HAL_UART_CLEAR_FLAG` |
| `ORE` | Overrun error (byte lost) | Read SR then read DR, or `CLEAR_OREFLAG` |
| `FE` | Framing error (wrong stop bit) | Read SR then read DR |
| `NE` | Noise error | Read SR then read DR |
| `PE` | Parity error | Read SR then read DR |

```c
/* C — Check and clear all error flags */
void UART_ClearErrors(UART_HandleTypeDef *huart)
{
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);

    /* Re-arm interrupt if it was disabled by HAL error state */
    if (huart->RxState == HAL_UART_STATE_ERROR)
    {
        HAL_UART_AbortReceive(huart);
        HAL_UART_Receive_IT(huart, rxBuf, 1);
    }
}
```

---

## 11. Printf Retargeting over UART (C)

Redirect `printf` to USART2 (GCC / newlib / STM32CubeIDE):

```c
/* syscalls.c or retarget.c */
#include "main.h"
#include <errno.h>
#include <sys/unistd.h>

extern UART_HandleTypeDef huart2;

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

int _write(int file, char *ptr, int len)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO)
    {
        HAL_UART_Transmit(&huart2, (uint8_t *)ptr, (uint16_t)len, HAL_MAX_DELAY);
        return len;
    }
    errno = EBADF;
    return -1;
}
```

After this, `printf("Temp: %d°C\r\n", temp);` works transparently over UART.

> **Note:** For Rust, use `core::fmt::Write` on the `Tx` handle directly (shown in §8.1). The
> `rtt-target` or `defmt` crates are more efficient for debugging on Rust embedded targets.

---

## 12. Ring Buffer / Circular RX in C

A software ring buffer decouples the ISR (which receives bytes at hardware speed) from the
application (which processes them at its own pace):

```c
/* ringbuf.h */
#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stdbool.h>

#define RING_BUF_SIZE  256   /* Must be power of 2 */

typedef struct {
    volatile uint8_t  buf[RING_BUF_SIZE];
    volatile uint32_t head;  /* written by ISR */
    volatile uint32_t tail;  /* read by application */
} RingBuf_t;

static inline void RingBuf_Init(RingBuf_t *rb)
{
    rb->head = rb->tail = 0;
}

static inline bool RingBuf_Push(RingBuf_t *rb, uint8_t byte)
{
    uint32_t next = (rb->head + 1) & (RING_BUF_SIZE - 1);
    if (next == rb->tail) return false;  /* buffer full */
    rb->buf[rb->head] = byte;
    rb->head = next;
    return true;
}

static inline bool RingBuf_Pop(RingBuf_t *rb, uint8_t *out)
{
    if (rb->tail == rb->head) return false;  /* empty */
    *out = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) & (RING_BUF_SIZE - 1);
    return true;
}

static inline uint32_t RingBuf_Available(const RingBuf_t *rb)
{
    return (rb->head - rb->tail) & (RING_BUF_SIZE - 1);
}

#endif /* RINGBUF_H */
```

```c
/* uart_ring.c — Using the ring buffer with UART interrupt */
#include "ringbuf.h"
#include "main.h"

static RingBuf_t uartRxBuf;
static uint8_t   rxByte;

void UART_Ring_Init(void)
{
    RingBuf_Init(&uartRxBuf);
    HAL_UART_Receive_IT(&huart2, &rxByte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        RingBuf_Push(&uartRxBuf, rxByte);
        HAL_UART_Receive_IT(&huart2, &rxByte, 1);
    }
}

/* Called from main loop — never blocks */
bool UART_Ring_ReadByte(uint8_t *out)
{
    return RingBuf_Pop(&uartRxBuf, out);
}

/* Read a complete line (blocks until '\n' or timeout) */
bool UART_Ring_ReadLine(char *lineBuf, uint16_t maxLen, uint32_t timeoutMs)
{
    uint16_t i = 0;
    uint32_t tStart = HAL_GetTick();
    uint8_t  ch;

    while ((HAL_GetTick() - tStart) < timeoutMs)
    {
        if (RingBuf_Pop(&uartRxBuf, &ch))
        {
            if (ch == '\n')
            {
                lineBuf[i] = '\0';
                return true;
            }
            if (ch != '\r' && i < maxLen - 1)
                lineBuf[i++] = (char)ch;
        }
    }
    lineBuf[i] = '\0';
    return false;  /* timeout */
}
```

---

## 13. Summary

| Aspect | Key Points |
|---|---|
| **Hardware** | USART = UART + optional synchronous clock. STM32 has 2–8 USART/UART peripherals depending on variant. Baud rate derived from APB clock via BRR register. |
| **HAL Polling** | Simplest API; CPU blocks until transfer completes. Use for debug output or low-speed, rare transfers. `HAL_UART_Transmit` / `HAL_UART_Receive`. |
| **HAL Interrupt** | CPU-free during transfer; callback fires on completion. Ideal for responsive single-byte reception. Must re-arm after each callback. `HAL_UART_Receive_IT`. |
| **HAL DMA** | Maximum throughput; zero CPU during transfer. Circular DMA + idle-line interrupt is the gold standard for high-speed variable-length receive. `HAL_UART_Receive_DMA`. |
| **LL API** | Near-register-level access; minimal overhead. Best for tight timing requirements. Requires manual flag polling or IRQ management. |
| **C++ Wrapper** | Thin RAII wrapper over HAL handle; adds type safety and encapsulation with no overhead. |
| **Rust HAL** | `stm32f4xx-hal` serial module; `nb::block!` for blocking reads; `embedded-hal` trait compatibility; `Mutex<RefCell<Option<T>>>` pattern for ISR shared state. |
| **Printf Retarget** | Override `_write` / `__io_putchar` to route `printf` over UART. Useful for debug builds. |
| **Ring Buffer** | Essential pattern for robust UART receive: ISR pushes bytes at hardware speed; application pops at its own pace without loss. |
| **Error Handling** | Always handle ORE (overrun), FE (framing), and NE (noise). HAL sets `huart->ErrorCode`; LL requires manual flag inspection. |
| **RS-485** | Use USART DE pin (CR3 DEM bit) for hardware direction control; no software GPIO toggling needed on supported STM32 variants. |

### Design Decision Guide

```
Need simple debug output?          → HAL Polling (Transmit only)
Need responsive RX, low data rate? → HAL Interrupt + Ring Buffer
Need high throughput or bulk TX?   → HAL DMA (Transmit_DMA)
Need continuous background RX?     → HAL DMA Circular + Idle Line IRQ
Need absolute minimum overhead?    → LL API
Using Rust?                        → stm32f4xx-hal serial + nb::block! or interrupts
Building a library / BSP?          → C++ Uart wrapper over HAL
```

---

*Document covers STM32F4 series as primary reference. Register names and HAL function signatures
are consistent across STM32F0/F1/F3/F7/H7/L0/L4 families with minor differences noted in
the respective HAL documentation.*