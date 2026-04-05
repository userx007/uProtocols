# 14. DMA Integration — Using Direct Memory Access for High-Throughput UART Transfers

**Architecture & Concepts (§1–6)** — DMA descriptor anatomy, STM32 stream/channel hierarchy, bus arbitration, and a complete configuration parameter table for both TX and RX paths.

**Critical: Cache Coherency (§7)** — The most common source of silent DMA bugs. The document explains exactly when to `CleanDCache` (before TX) vs. `InvalidateDCache` (after RX) and why.

**Transfer Modes (§5, §10)** — Normal (single-shot) vs. Circular vs. Ping-Pong vs. Scatter-Gather, with timing diagrams showing how HT/TC interrupts interleave with CPU processing.

**IDLE Line Detection (§9)** — The key technique for variable-length frames over DMA: instead of waiting for a full buffer, the hardware fires an interrupt whenever the line goes silent.

**Code examples:**
- **C** — STM32 HAL TX (blocking with cache flush), HAL RX with IDLE detection + ring buffer math, bare-metal register programming, and a C++ RAII wrapper with `std::span`
- **Rust** — Embassy async TX/RX (idiomatic `await`), RTIC ping-pong with atomic buffer swapping, low-level PAC register access mirroring the C bare-metal approach, and a practical `read_until_idle` with timeout pattern

**§16 Pitfalls table** covers the 10 most common DMA UART bugs in production embedded code, each with diagnosis and fix.


---

## Table of Contents

1. [Overview](#1-overview)
2. [DMA Architecture and Concepts](#2-dma-architecture-and-concepts)
3. [Why DMA for UART?](#3-why-dma-for-uart)
4. [DMA Controller Internals](#4-dma-controller-internals)
5. [DMA Transfer Modes](#5-dma-transfer-modes)
6. [DMA Channel Configuration](#6-dma-channel-configuration)
7. [Memory Alignment and Cache Coherency](#7-memory-alignment-and-cache-coherency)
8. [DMA TX — Transmit Path](#8-dma-tx--transmit-path)
9. [DMA RX — Receive Path](#9-dma-rx--receive-path)
10. [Circular / Ping-Pong Buffering](#10-circular--ping-pong-buffering)
11. [Error Handling and Recovery](#11-error-handling-and-recovery)
12. [Integration with RTOS](#12-integration-with-rtos)
13. [Code Examples in C/C++](#13-code-examples-in-cc)
14. [Code Examples in Rust](#14-code-examples-in-rust)
15. [Performance Tuning and Benchmarking](#15-performance-tuning-and-benchmarking)
16. [Common Pitfalls](#16-common-pitfalls)
17. [Summary](#17-summary)

---

## 1. Overview

Direct Memory Access (DMA) is a hardware mechanism that allows peripherals to transfer data directly
to and from system memory **without CPU intervention**. For UART, this means that large blocks of
data can be sent or received while the CPU is free to execute application logic, manage other
peripherals, or enter a low-power sleep state.

In high-throughput embedded systems — data loggers, protocol bridges, modem stacks, industrial
controllers — DMA-driven UART is not an optimisation; it is a necessity. At 921600 baud, the CPU
receives an interrupt every ~10 µs if byte-by-byte ISR handling is used. DMA reduces that to one or
two interrupts per kilobyte-sized buffer, cutting interrupt overhead by two orders of magnitude.

---

## 2. DMA Architecture and Concepts

A DMA controller (DMAC) sits on the system bus between the CPU, memory, and peripherals. It is
programmed by the CPU with a **descriptor** — a small data structure specifying:

```
┌──────────────────────────────────────────────────────────┐
│                    DMA Descriptor                        │
│  Source Address     : 0x40013804  (UART DR register)     │
│  Destination Addr   : 0x20001000  (RX buffer in SRAM)    │
│  Transfer Count     : 512 bytes                          │
│  Transfer Width     : 8-bit                              │
│  Src Increment      : No  (peripheral register, fixed)   │
│  Dst Increment      : Yes (advance through buffer)       │
│  Trigger            : UART_RX_NOT_EMPTY                  │
│  Interrupt on TC    : Yes (Transfer Complete)            │
└──────────────────────────────────────────────────────────┘
```

The DMAC arbitrates bus access and moves one transfer unit (byte, half-word, or word) each time the
peripheral asserts its DMA request line. The CPU is only interrupted when the whole block is done.

### Key Terminology

| Term | Meaning |
|------|---------|
| **Channel / Stream** | One independent DMA path (source → destination) |
| **Transfer Width** | Size of each individual bus transaction (8/16/32 bit) |
| **Burst Size** | How many transfers are packed together on the bus |
| **Scatter-Gather** | DMAC follows a linked list of descriptors automatically |
| **TC Interrupt** | Transfer Complete — fires when all bytes have moved |
| **HT Interrupt** | Half-Transfer — fires at the midpoint (used for ping-pong) |
| **TE Interrupt** | Transfer Error — fires on bus fault or FIFO overflow |
| **Circular Mode** | DMAC automatically wraps the address counter back to the start |

---

## 3. Why DMA for UART?

### CPU Load Comparison

```
Byte-by-byte ISR @ 921600 baud (8N1 = 10 bits/symbol):
  Symbol rate   = 92,160 bytes/s
  ISR period    = 1 / 92,160 ≈ 10.85 µs
  ISR overhead  = ~40 CPU cycles (save/restore + handler body)
  At 168 MHz    = 40 / 168,000,000 ≈ 0.24 µs per ISR
  CPU load      = 0.24 µs / 10.85 µs ≈ 2.2% — just for ISR entry/exit!
  (Plus the actual handler work, cache misses, NVIC latency...)

DMA with 512-byte buffer @ 921600 baud:
  Buffer fill time  = 512 / 92,160 ≈ 5.56 ms
  Interrupts        = 1 (Transfer Complete) per 5.56 ms
  CPU is free for   ≈ 5.56 ms between interrupts
```

### Additional Benefits

- **Deterministic latency** — no jitter from nested ISR priorities competing for CPU.
- **Low-power** — CPU can sleep in WFI/WFE while DMA runs from clocked-down bus fabric.
- **Enables high baud rates** — 4 Mbit/s UART is impractical with ISR; trivial with DMA.
- **Double-buffering** — application processes buffer A while DMA fills buffer B simultaneously.

---

## 4. DMA Controller Internals

### STM32 DMA (representative example)

STM32F4/F7/H7 use a two-level hierarchy: **DMA controllers** (DMA1, DMA2) each with 8 **streams**,
each stream configurable to one of up to 8 **channels** (request lines from peripherals).

```
DMA2
 ├── Stream 0  →  Channel 4 = UART1_RX
 ├── Stream 1  →  Channel 4 = UART6_TX
 ├── Stream 2  →  Channel 4 = UART1_TX
 │             →  Channel 2 = USART2_RX
 ├── Stream 5  →  Channel 4 = USART1_RX (alternate)
 └── ...
```

Each stream has a FIFO (4 words deep) that decouples the peripheral data rate from the bus burst
rate, allowing the DMAC to issue efficient bus bursts even when the peripheral dribbles data slowly.

### Priority and Arbitration

When multiple streams request the bus simultaneously, a round-robin arbiter with software-
configurable priority levels (Low / Medium / High / Very High) resolves conflicts. UART DMA streams
should typically be set to **High** to avoid FIFO overflow at high baud rates.

---

## 5. DMA Transfer Modes

### 5.1 Normal (Single-Shot) Mode

The DMAC counts down from N to 0, fires the TC interrupt, and stops. The CPU must re-arm it for
the next transfer. Best for fixed-size packets (e.g., sending a known-length response frame).

```
Buffer:  [XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX]
          ↑ start                         ↑ end → TC interrupt → DMAC halts
```

### 5.2 Circular Mode

The DMAC wraps automatically. After the last byte, the address counter resets to the start of the
buffer and the DMA continues without CPU intervention. HT and TC interrupts fire at the half and
full points respectively. This is the standard mode for continuous UART reception.

```
Buffer:  [AAAAAAAAAAAAAAAA|BBBBBBBBBBBBBBBB]
                          ↑ HT interrupt    ↑ TC interrupt → wraps to start
          ← CPU processes A →  ← CPU processes B →
```

### 5.3 Ping-Pong (Double Buffer) Mode

STM32H7 and many other MCUs support hardware double-buffering. The DMAC alternates between two
memory buffers. While it fills buffer 1, the CPU reads buffer 0, and vice versa. Eliminates the
race window present in software ping-pong.

### 5.4 Scatter-Gather (Linked List) Mode

The DMAC reads a linked list of descriptors from memory. Each descriptor describes one segment.
Used for non-contiguous buffers (e.g., ring buffer wrapping around end of array). Found in
higher-end MCUs (STM32H7 BDMA, NXP eDMA, Nordic nRF UARTE EasyDMA).

---

## 6. DMA Channel Configuration

The complete set of parameters that must be configured before enabling a stream:

| Parameter | TX Typical | RX Typical |
|-----------|------------|------------|
| Direction | Memory → Peripheral | Peripheral → Memory |
| Source address | TX buffer in SRAM | UART data register |
| Destination address | UART data register | RX buffer in SRAM |
| Source increment | Yes | No |
| Destination increment | No | Yes |
| Transfer width | 8-bit | 8-bit |
| Buffer size | Packet length | RX buffer length |
| Mode | Normal | Circular |
| Priority | High | Very High |
| FIFO mode | Disable (direct) | Disable or 1/4 threshold |
| TC interrupt | Yes | Yes |
| HT interrupt | No | Yes (for ping-pong) |
| TE interrupt | Yes | Yes |

---

## 7. Memory Alignment and Cache Coherency

### 7.1 Alignment

DMA transfers have alignment requirements matching the transfer width:

- **8-bit width**: no alignment constraint
- **16-bit width**: buffer must be 2-byte aligned
- **32-bit width**: buffer must be 4-byte aligned

Misaligned transfers produce a bus fault on most architectures.

### 7.2 Cache Coherency (Critical on Cortex-M7, A-series)

On MCUs with data caches (D-Cache), the CPU's view of memory and the DMA controller's view can
diverge:

- **TX direction**: CPU writes to cache → DMA reads stale data from RAM. Fix: **clean** (flush)
  the cache lines before starting the DMA transfer.
- **RX direction**: DMA writes to RAM → CPU reads stale data from cache. Fix: **invalidate** cache
  lines after DMA completes, before the CPU reads the buffer.

```
Without cache management (WRONG):
  CPU writes TX buf → [Cache: fresh] [RAM: stale] → DMA sends stale bytes!

Correct TX flow:
  1. CPU fills TX buffer
  2. SCB_CleanDCache_by_Addr(tx_buf, len)   ← flush to RAM
  3. Enable DMA TX
  4. DMA reads from RAM → correct data sent

Correct RX flow:
  1. Enable DMA RX (circular)
  2. DMA TC interrupt fires
  3. SCB_InvalidateDCache_by_Addr(rx_buf, len)  ← discard stale cache lines
  4. CPU reads RX buffer → sees DMA-written data
```

Buffer placement in non-cacheable memory (via MPU attribute or linker section) is an alternative
that avoids explicit cache management at the cost of slower CPU access to those buffers.

---

## 8. DMA TX — Transmit Path

### Flow Diagram

```
Application
    │
    ▼ memcpy / fill
┌─────────────┐    SCB_Clean    ┌──────────┐   DMA request   ┌───────────┐
│  TX Buffer  │ ─────────────► │  DMAC    │ ──────────────► │  UART DR  │──► Wire
│  (SRAM)     │                │  Stream  │                  │  register │
└─────────────┘                └──────────┘                  └───────────┘
                                    │
                                 TC IRQ
                                    │
                               Signal app
                             (semaphore / flag)
```

### State Machine

```
IDLE ──[send(buf, len)]──► TRANSMITTING ──[TC IRQ]──► IDLE
                                │
                             [TE IRQ]
                                │
                              ERROR ──[reset]──► IDLE
```

---

## 9. DMA RX — Receive Path

UART reception is inherently asynchronous — data arrives at any time. DMA circular mode is ideal:

```
RX Buffer (circular, N bytes):
┌────────────────────────────────────────────────────────┐
│  0    1    2  ...  N/2-1 │ N/2  N/2+1  ...  N-1        │
│  ← first half (A) ──────│──── second half (B) ────────►│
│         HT fires here ──┘         TC fires here ────────┘
└────────────────────────────────────────────────────────┘
          DMAC write pointer moves →→→
```

The application reads from the half that the DMAC is NOT currently writing. The NDTR (number of
data items remaining) register tells exactly how many bytes have been received at any instant:

```
bytes_received = buffer_size - DMAC_NDTR
write_ptr      = buffer_start + bytes_received
```

### Idle Line Detection

Most UART peripherals (STM32, NXP LPUART, etc.) provide an **IDLE line interrupt** — fired when
the RX line goes silent for one full frame time after receiving data. This is the key to handling
variable-length packets with DMA:

```
DMA fills continuously, IDLE fires → read DMA write pointer → process bytes_received - last_pos
```

Without IDLE detection, the application either polls the NDTR register or waits for HT/TC, which
means latency of up to half-buffer-size bytes — unacceptable for command-response protocols.

---

## 10. Circular / Ping-Pong Buffering

### Software Ping-Pong with HT and TC

```
Time:  ──────────────────────────────────────────────────────────────────►
DMA:   [====fills buf[0..N/2-1]====][====fills buf[N/2..N-1]====][wraps..
IRQ:                                HT                            TC
CPU:                                [process buf[0..N/2-1]]       [process buf[N/2..N-1]]
```

The CPU always processes the half that the DMA just finished, while the DMA writes into the other
half. This provides maximum throughput with zero data copies.

### Ring Buffer Abstraction over Circular DMA

For variable-length UART frames, a software ring buffer on top of the circular DMA buffer provides
clean producer/consumer semantics:

```
DMA writes to:  dma_buf[0..DMA_BUF_SIZE-1]   (hardware circular)
App reads from: ring abstraction tracking head/tail inside dma_buf
```

The write pointer is computed from NDTR on every read access; the read pointer advances as the
application consumes bytes. No data movement is needed.

---

## 11. Error Handling and Recovery

### DMA Transfer Error (TE)

Caused by a bus fault: memory protection violation, AHB error response, or FIFO overflow. Recovery:

1. Disable the DMA stream (clear EN bit).
2. Clear all status flags (TEIF, FEIF, etc.).
3. Log the error.
4. Reconfigure and re-enable the stream.

### UART Overrun Error (ORE)

Occurs when a new byte arrives before the previous one was read by DMA. Indicates the FIFO or DMA
FIFO is overwhelmed. Recovery: clear ORE flag in UART status register, then re-enable DMA.

### FIFO Error (FE) and Noise Error (NE)

Indicate a framing or electrical problem on the line. The byte should be discarded; higher-layer
protocol (CRC, sequence numbers) must request retransmission.

### Watchdog / Timeout

A hardware timer monitors time since last DMA activity. If no data arrives within the expected
window, the timer fires and the system resets the DMA channel. This catches a stuck-at scenario
where DMA is enabled but the UART clock was disabled.

---

## 12. Integration with RTOS

### Blocking (Semaphore) Pattern

```
TX thread:
  fill buffer
  clean cache
  start DMA TX
  xSemaphoreTake(dma_tx_done, portMAX_DELAY)  ← blocks here
  continue...

TC ISR:
  xSemaphoreGiveFromISR(dma_tx_done, &woken)
  portYIELD_FROM_ISR(woken)
```

### Non-Blocking (Queue) Pattern

```
RX ISR (IDLE or HT/TC):
  compute available bytes
  xQueueSendFromISR(rx_queue, &frame_descriptor, &woken)

RX thread:
  xQueueReceive(rx_queue, &desc, portMAX_DELAY)
  process(desc.buf, desc.len)
```

### Zero-Copy with DMA Buffers

For maximum efficiency, pass a **pointer + length** to the application instead of copying. The
application must finish processing before the DMAC wraps around and overwrites the region. RTOS
ownership tokens or reference counting enforce this contract.

---

## 13. Code Examples in C/C++

### 13.1 STM32 HAL — DMA TX (Single-Shot)

```c
#include "stm32f4xx_hal.h"

/* Peripherals initialised by CubeMX / HAL_Init */
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef  hdma_usart1_tx;

#define TX_BUF_SIZE 256

/* Place in non-cacheable SRAM or manage cache manually */
static uint8_t tx_buffer[TX_BUF_SIZE] __attribute__((aligned(32)));

/* Semaphore for blocking the caller until transmission completes */
static volatile bool tx_complete = false;

/* Called from DMA TC IRQ by HAL */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        tx_complete = true;
    }
}

/**
 * @brief  Send len bytes from data over UART using DMA (blocking).
 */
HAL_StatusTypeDef uart_dma_send(const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > TX_BUF_SIZE) return HAL_ERROR;

    memcpy(tx_buffer, data, len);

    /* On Cortex-M7 with D-Cache: flush buffer to RAM before DMA reads it */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache_by_Addr((uint32_t *)tx_buffer,
                             (int32_t)((len + 31U) & ~31U));
#endif

    tx_complete = false;

    HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart1, tx_buffer, len);
    if (status != HAL_OK) return status;

    /* Busy-wait (replace with RTOS semaphore in production) */
    uint32_t timeout = HAL_GetTick() + 1000U;
    while (!tx_complete) {
        if (HAL_GetTick() > timeout) {
            HAL_UART_AbortTransmit(&huart1);
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}
```

---

### 13.2 STM32 HAL — DMA RX with IDLE Line Detection (Circular Buffer)

```c
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdbool.h>

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef  hdma_usart1_rx;

#define DMA_RX_BUF_SIZE  512U   /* Must be power-of-two for easy wrap math */

/* DMA writes here continuously in circular mode */
static uint8_t dma_rx_buf[DMA_RX_BUF_SIZE]
    __attribute__((aligned(32), section(".noinit")));

/* Application ring-buffer read head */
static volatile uint32_t rx_read_pos = 0U;

/* ------------------------------------------------------------------ */
/* Initialise UART with DMA RX in circular mode + IDLE IRQ            */
/* ------------------------------------------------------------------ */
void uart_dma_rx_init(void)
{
    /* Start circular DMA receive (HAL keeps it running automatically) */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, dma_rx_buf, DMA_RX_BUF_SIZE);

    /*
     * Disable the Half-Transfer interrupt that HAL enables by default.
     * We use IDLE-line detection instead for variable-length framing.
     */
    __HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
}

/* ------------------------------------------------------------------ */
/* Return number of bytes available in the DMA ring buffer            */
/* ------------------------------------------------------------------ */
static uint32_t rx_bytes_available(void)
{
    /* Write pointer = how far DMA has written = buf_size - NDTR */
    uint32_t write_pos = DMA_RX_BUF_SIZE -
                         __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
    if (write_pos >= rx_read_pos) {
        return write_pos - rx_read_pos;
    }
    /* Buffer has wrapped */
    return DMA_RX_BUF_SIZE - rx_read_pos + write_pos;
}

/* ------------------------------------------------------------------ */
/* Copy up to max_len bytes from the DMA ring buffer to dst           */
/* Returns actual number of bytes copied.                             */
/* ------------------------------------------------------------------ */
uint32_t uart_dma_rx_read(uint8_t *dst, uint32_t max_len)
{
    uint32_t available = rx_bytes_available();
    uint32_t to_copy   = (available < max_len) ? available : max_len;

    if (to_copy == 0U) return 0U;

    /* Invalidate D-Cache over the region we are about to read */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr((uint32_t *)dma_rx_buf, DMA_RX_BUF_SIZE);
#endif

    /* Handle linear vs. wrapped copy */
    uint32_t head_space = DMA_RX_BUF_SIZE - rx_read_pos;
    if (to_copy <= head_space) {
        memcpy(dst, &dma_rx_buf[rx_read_pos], to_copy);
    } else {
        memcpy(dst,             &dma_rx_buf[rx_read_pos], head_space);
        memcpy(dst + head_space, dma_rx_buf,              to_copy - head_space);
    }

    rx_read_pos = (rx_read_pos + to_copy) % DMA_RX_BUF_SIZE;
    return to_copy;
}

/* ------------------------------------------------------------------ */
/* HAL callback: fires on IDLE line or buffer half/full               */
/* ------------------------------------------------------------------ */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    (void)Size;
    if (huart->Instance == USART1) {
        /*
         * 'Size' is the total count of bytes received so far in this
         * circular session.  We let uart_dma_rx_read() derive the actual
         * write position from NDTR for accuracy.
         *
         * Signal the application task (replace with RTOS notification):
         */
        extern volatile bool rx_data_ready;
        rx_data_ready = true;
    }
}
```

---

### 13.3 Bare-Metal STM32 — Direct Register Programming (No HAL)

```c
/*
 * Configure DMA2 Stream2 Channel4 for USART1_RX (Circular, 8-bit, very high priority)
 * Target: STM32F407
 */

#include "stm32f407xx.h"
#include <stdint.h>

#define DMA_RX_BUF  0x20001000UL   /* SRAM address */
#define DMA_RX_LEN  512U

static void dma2_stream2_usart1_rx_init(void)
{
    /* 1. Enable DMA2 clock */
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    /* 2. Disable stream before configuring */
    DMA2_Stream2->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream2->CR & DMA_SxCR_EN) {}

    /* 3. Clear interrupt flags (LIFCR bits 16..21 for Stream2) */
    DMA2->LIFCR = 0x003F0000UL;

    /* 4. Peripheral address = USART1 data register */
    DMA2_Stream2->PAR  = (uint32_t)&USART1->DR;

    /* 5. Memory address = RX buffer */
    DMA2_Stream2->M0AR = DMA_RX_BUF;

    /* 6. Number of data items */
    DMA2_Stream2->NDTR = DMA_RX_LEN;

    /* 7. FIFO: direct mode (FIFO disabled) */
    DMA2_Stream2->FCR  = 0x00000000UL;

    /* 8. Stream control register:
     *   Channel 4, Very High priority, 8-bit→8-bit,
     *   Memory increment ON, Peripheral increment OFF,
     *   Circular mode ON, Direction = Peripheral-to-Memory,
     *   TC and HT and TE interrupts enabled
     */
    DMA2_Stream2->CR =
          (4UL  << DMA_SxCR_CHSEL_Pos)  /* Channel 4 (USART1_RX)  */
        | (3UL  << DMA_SxCR_PL_Pos)     /* Very High priority      */
        | (0UL  << DMA_SxCR_MSIZE_Pos)  /* Memory  size: 8-bit     */
        | (0UL  << DMA_SxCR_PSIZE_Pos)  /* Periph  size: 8-bit     */
        | DMA_SxCR_MINC                 /* Memory address increment */
        | DMA_SxCR_CIRC                 /* Circular mode           */
        | (0UL  << DMA_SxCR_DIR_Pos)    /* Periph → Memory         */
        | DMA_SxCR_TCIE                 /* TC interrupt            */
        | DMA_SxCR_HTIE                 /* HT interrupt            */
        | DMA_SxCR_TEIE;               /* TE interrupt            */

    /* 9. Enable USART1 DMA RX request */
    USART1->CR3 |= USART_CR3_DMAR;

    /* 10. Enable stream */
    DMA2_Stream2->CR |= DMA_SxCR_EN;

    /* 11. Enable IRQ in NVIC */
    NVIC_SetPriority(DMA2_Stream2_IRQn, 5);
    NVIC_EnableIRQ(DMA2_Stream2_IRQn);
}

/* ISR for DMA2 Stream2 */
void DMA2_Stream2_IRQHandler(void)
{
    uint32_t lisr = DMA2->LISR;

    if (lisr & DMA_LISR_TCIF2) {
        DMA2->LIFCR = DMA_LIFCR_CTCIF2;
        /* TC: second half ready — process dma_rx_buf[N/2 .. N-1] */
    }
    if (lisr & DMA_LISR_HTIF2) {
        DMA2->LIFCR = DMA_LIFCR_CHTIF2;
        /* HT: first half ready — process dma_rx_buf[0 .. N/2-1] */
    }
    if (lisr & DMA_LISR_TEIF2) {
        DMA2->LIFCR = DMA_LIFCR_CTEIF2;
        /* Handle transfer error: reset and re-enable */
        DMA2_Stream2->CR &= ~DMA_SxCR_EN;
        DMA2_Stream2->NDTR = DMA_RX_LEN;
        DMA2_Stream2->CR  |= DMA_SxCR_EN;
    }
}
```

---

### 13.4 C++ RAII Wrapper for DMA UART

```cpp
#include "stm32f4xx_hal.h"
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

/**
 * @brief  RAII wrapper around HAL DMA UART with type-safe buffer management.
 *
 * Instantiate once; call send() / receive() from application code.
 * The destructor aborts any in-flight transfer.
 */
class DmaUart {
public:
    static constexpr uint16_t TX_BUF_SIZE = 512;
    static constexpr uint16_t RX_BUF_SIZE = 1024;

    explicit DmaUart(UART_HandleTypeDef &huart,
                     DMA_HandleTypeDef  &hdma_rx)
        : m_huart(huart), m_hdma_rx(hdma_rx)
    {
        HAL_UARTEx_ReceiveToIdle_DMA(&m_huart, m_rx_buf, RX_BUF_SIZE);
        __HAL_DMA_DISABLE_IT(&m_hdma_rx, DMA_IT_HT);
    }

    ~DmaUart()
    {
        HAL_UART_Abort(&m_huart);
    }

    /* Non-copyable, non-movable (owns hardware resource) */
    DmaUart(const DmaUart &)             = delete;
    DmaUart &operator=(const DmaUart &)  = delete;

    /**
     * @brief  Transmit data (blocking, timeout 1 s).
     * @return true on success.
     */
    bool send(std::span<const uint8_t> data)
    {
        if (data.size() > TX_BUF_SIZE) return false;

        std::memcpy(m_tx_buf, data.data(), data.size());
        flush_dcache(m_tx_buf, TX_BUF_SIZE);

        m_tx_done = false;
        if (HAL_UART_Transmit_DMA(&m_huart, m_tx_buf,
                                   static_cast<uint16_t>(data.size())) != HAL_OK) {
            return false;
        }

        uint32_t deadline = HAL_GetTick() + 1000U;
        while (!m_tx_done) {
            if (HAL_GetTick() > deadline) {
                HAL_UART_AbortTransmit(&m_huart);
                return false;
            }
        }
        return true;
    }

    /**
     * @brief  Read available bytes from the DMA ring buffer.
     * @return Bytes actually read.
     */
    std::size_t receive(std::span<uint8_t> dst)
    {
        invalidate_dcache(m_rx_buf, RX_BUF_SIZE);

        uint32_t write_pos  = RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(&m_hdma_rx);
        uint32_t available  = (write_pos >= m_rx_head)
                             ? write_pos - m_rx_head
                             : RX_BUF_SIZE - m_rx_head + write_pos;

        std::size_t to_copy = std::min(available, dst.size());
        if (to_copy == 0U) return 0U;

        uint32_t head_space = RX_BUF_SIZE - m_rx_head;
        if (to_copy <= head_space) {
            std::memcpy(dst.data(), m_rx_buf + m_rx_head, to_copy);
        } else {
            std::memcpy(dst.data(),              m_rx_buf + m_rx_head, head_space);
            std::memcpy(dst.data() + head_space, m_rx_buf,             to_copy - head_space);
        }

        m_rx_head = (m_rx_head + to_copy) % RX_BUF_SIZE;
        return to_copy;
    }

    /* Called from HAL TX-complete callback */
    void on_tx_complete() { m_tx_done = true; }

    /* Called from HAL RX-event callback */
    void on_rx_event() { m_rx_event = true; }

    bool has_rx_data() const { return m_rx_event; }
    void clear_rx_event()    { m_rx_event = false; }

private:
    UART_HandleTypeDef &m_huart;
    DMA_HandleTypeDef  &m_hdma_rx;

    alignas(32) uint8_t m_tx_buf[TX_BUF_SIZE]{};
    alignas(32) uint8_t m_rx_buf[RX_BUF_SIZE]{};

    volatile bool   m_tx_done  = false;
    volatile bool   m_rx_event = false;
    uint32_t        m_rx_head  = 0U;

    static void flush_dcache(void *addr, uint32_t size)
    {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(addr),
                                 static_cast<int32_t>((size + 31U) & ~31U));
#else
        (void)addr; (void)size;
#endif
    }

    static void invalidate_dcache(void *addr, uint32_t size)
    {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
        SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t *>(addr),
                                      static_cast<int32_t>((size + 31U) & ~31U));
#else
        (void)addr; (void)size;
#endif
    }
};
```

---

## 14. Code Examples in Rust

### 14.1 Embassy (async Rust) — DMA UART TX/RX

Embassy is the leading async embedded Rust framework. It wraps DMA UART into fully async futures.

```rust
//! Embassy DMA UART example — STM32F4
//! Cargo.toml dependencies:
//!   embassy-stm32 = { version = "0.1", features = ["stm32f407vg", "time-driver-tim2"] }
//!   embassy-executor = { version = "0.5", features = ["arch-cortex-m", "executor-thread"] }
//!   embassy-time = "0.3"

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::{
    bind_interrupts,
    dma::NoDma,
    peripherals,
    usart::{self, Config, Uart},
};
use embassy_time::{Duration, Timer};

bind_interrupts!(struct Irqs {
    USART1 => usart::InterruptHandler<peripherals::USART1>;
});

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    // Configure UART with DMA channels
    let mut config = Config::default();
    config.baudrate = 921_600;

    let mut uart = Uart::new(
        p.USART1,
        p.PA10,        // RX
        p.PA9,         // TX
        Irqs,
        p.DMA2_CH7,    // TX DMA channel
        p.DMA2_CH2,    // RX DMA channel
        config,
    )
    .unwrap();

    // --- DMA TX: fire-and-forget async write ---
    let tx_data = b"Hello via DMA UART!\r\n";
    uart.write(tx_data).await.unwrap();

    // --- DMA RX: read exactly 16 bytes ---
    let mut rx_buf = [0u8; 16];
    uart.read(&mut rx_buf).await.unwrap();

    // --- Echo loop ---
    loop {
        let mut buf = [0u8; 64];
        match uart.read_until_idle(&mut buf).await {
            Ok(n) => {
                uart.write(&buf[..n]).await.unwrap();
            }
            Err(e) => {
                // Handle UART error (overrun, framing, etc.)
                let _ = e;
            }
        }
        Timer::after(Duration::from_micros(100)).await;
    }
}
```

---

### 14.2 Rust — Ping-Pong Ring Buffer over DMA (RTIC framework)

```rust
//! RTIC-based ping-pong DMA RX with IDLE detection.
//! Uses stm32f4xx-hal with DMA support.

#![no_std]
#![no_main]

use rtic::app;
use stm32f4xx_hal::{
    dma::{
        config::DmaConfig, MemoryToPeripheral, PeripheralToMemory,
        Stream2, StreamsTuple, Transfer,
    },
    pac,
    prelude::*,
    serial::{Config as SerialConfig, Serial},
};
use core::sync::atomic::{AtomicU32, Ordering};

const DMA_BUF_SIZE: usize = 512;

/// Aligned, static DMA buffer — must outlive all transfers.
/// `#[link_section]` places it in non-cacheable SRAM on H7.
#[repr(align(32))]
struct DmaBuf([u8; DMA_BUF_SIZE]);

static mut DMA_RX_BUF_A: DmaBuf = DmaBuf([0u8; DMA_BUF_SIZE]);
static mut DMA_RX_BUF_B: DmaBuf = DmaBuf([0u8; DMA_BUF_SIZE]);

/// Index of the buffer currently being filled by DMA (0 or 1).
static ACTIVE_BUF: AtomicU32 = AtomicU32::new(0);

#[app(device = stm32f4xx_hal::pac, peripherals = true)]
mod app {
    use super::*;

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        transfer: Transfer<
            Stream2<pac::DMA2>,
            4,   // channel
            pac::USART1,
            PeripheralToMemory,
            &'static mut [u8; DMA_BUF_SIZE],
        >,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local, init::Monotonics) {
        let dp = ctx.device;

        // Enable clocks
        let rcc = dp.RCC.constrain();
        let clocks = rcc.cfgr.sysclk(168.MHz()).freeze();

        let gpioa = dp.GPIOA.split();
        let tx_pin = gpioa.pa9.into_alternate();
        let rx_pin = gpioa.pa10.into_alternate();

        let serial = Serial::new(
            dp.USART1,
            (tx_pin, rx_pin),
            SerialConfig::default().baudrate(921_600.bps()),
            &clocks,
        )
        .unwrap();

        let (_, rx) = serial.split();

        // Set up DMA2 streams
        let dma2 = StreamsTuple::new(dp.DMA2);
        let dma_config = DmaConfig::default()
            .memory_increment(true)
            .transfer_complete_interrupt(true)
            .half_transfer_interrupt(true)
            .transfer_error_interrupt(true);

        // Safety: static mut, used only here and in ISR with atomic switching
        let buf_a = unsafe { &mut DMA_RX_BUF_A.0 };

        let transfer = Transfer::init_peripheral_to_memory(
            dma2.2,   // Stream2
            rx,
            buf_a,
            None,
            dma_config,
        );

        (Shared {}, Local { transfer }, init::Monotonics())
    }

    #[task(binds = DMA2_STREAM2, local = [transfer], priority = 4)]
    fn dma_rx_isr(ctx: dma_rx_isr::Context) {
        let transfer = ctx.local.transfer;

        if transfer.is_transfer_complete() {
            transfer.clear_transfer_complete();
            let active = ACTIVE_BUF.load(Ordering::Relaxed);
            let (new_buf, ready_buf) = if active == 0 {
                let b = unsafe { &mut DMA_RX_BUF_B.0 };
                let r = unsafe { &mut DMA_RX_BUF_A.0 };
                (b, r)
            } else {
                let b = unsafe { &mut DMA_RX_BUF_A.0 };
                let r = unsafe { &mut DMA_RX_BUF_B.0 };
                (b, r)
            };

            // Swap DMA to the idle buffer, keeping reception continuous
            transfer.next_transfer(new_buf).unwrap();
            ACTIVE_BUF.store(active ^ 1, Ordering::Relaxed);

            // Process the completed buffer (zero-copy reference)
            process_rx_frame(ready_buf);
        }

        if transfer.is_transfer_error() {
            transfer.clear_transfer_error();
            // Log error; DMA continues
        }
    }

    fn process_rx_frame(buf: &[u8]) {
        // Application frame processing goes here.
        // In a real system: push to a queue, validate CRC, dispatch.
        let _ = buf;
    }
}
```

---

### 14.3 Rust — Low-Level DMA Register Programming (no HAL)

```rust
//! Direct MMIO register manipulation for STM32F4 DMA2 Stream2 (USART1_RX).
//! Uses the `stm32f4` PAC (Peripheral Access Crate) for type-safe register access.

#![no_std]

use stm32f4::stm32f407::{DMA2, USART1};

/// Safety: caller must ensure no other code accesses these peripherals concurrently.
pub unsafe fn configure_dma2_stream2_usart1_rx(
    dma: &DMA2,
    _usart: &USART1,
    rx_buf_addr: u32,
    buf_len: u16,
) {
    // 1. Enable DMA2 clock via RCC (caller's responsibility here for brevity)

    // 2. Disable stream
    dma.s2cr.modify(|_, w| w.en().disabled());
    while dma.s2cr.read().en().is_enabled() {}

    // 3. Clear interrupt flags in LIFCR (stream 2: bits 16..21)
    dma.lifcr.write(|w| {
        w.ctcif2().set_bit()
         .chtif2().set_bit()
         .cteif2().set_bit()
         .cdmeif2().set_bit()
         .cfeif2().set_bit()
    });

    // 4. Peripheral address: USART1->DR at offset 0x04
    // (safe because USART1 base + DR offset is a known hardware address)
    let usart1_dr: u32 = 0x4001_1004;
    dma.s2par.write(|w| w.pa().bits(usart1_dr));

    // 5. Memory address
    dma.s2m0ar.write(|w| w.m0a().bits(rx_buf_addr));

    // 6. Number of data items
    dma.s2ndtr.write(|w| w.ndt().bits(buf_len));

    // 7. FIFO: direct mode (disable FIFO)
    dma.s2fcr.write(|w| w.dmdis().enabled()); // dmdis=0 → direct mode ON

    // 8. Configure stream control register
    dma.s2cr.write(|w| {
        w.chsel().bits(4)      // Channel 4 = USART1_RX
         .pl().very_high()     // Priority: very high
         .msize().bits8()      // Memory  data size: byte
         .psize().bits8()      // Periph  data size: byte
         .minc().incremented() // Memory increment
         .pinc().fixed()       // Peripheral: fixed address
         .circ().enabled()     // Circular mode
         .dir().peripheral_to_memory()
         .tcie().enabled()     // TC interrupt
         .htie().enabled()     // HT interrupt
         .teie().enabled()     // TE interrupt
    });

    // 9. Enable stream
    dma.s2cr.modify(|_, w| w.en().enabled());
}

// -----------------------------------------------------------------------
// Ring buffer reader — can be called from any context
// -----------------------------------------------------------------------

pub struct DmaRxReader {
    buf_start: *const u8,
    buf_len:   u32,
    read_pos:  u32,
}

impl DmaRxReader {
    /// # Safety
    /// `buf` must be the same static buffer handed to `configure_dma2_stream2_usart1_rx`.
    pub unsafe fn new(buf: *const u8, len: u32) -> Self {
        Self { buf_start: buf, buf_len: len, read_pos: 0 }
    }

    /// Return bytes available based on current DMA NDTR.
    pub fn available(&self, ndtr: u32) -> u32 {
        let write_pos = self.buf_len - ndtr;
        if write_pos >= self.read_pos {
            write_pos - self.read_pos
        } else {
            self.buf_len - self.read_pos + write_pos
        }
    }

    /// Read up to `dst.len()` bytes. Returns count actually read.
    ///
    /// # Safety
    /// No other writer may touch `buf` during this call (DMA is the only writer, which is fine).
    pub unsafe fn read(&mut self, dst: &mut [u8], ndtr: u32) -> usize {
        let avail = self.available(ndtr) as usize;
        let to_copy = dst.len().min(avail);
        if to_copy == 0 { return 0; }

        let head_space = (self.buf_len - self.read_pos) as usize;
        if to_copy <= head_space {
            core::ptr::copy_nonoverlapping(
                self.buf_start.add(self.read_pos as usize),
                dst.as_mut_ptr(),
                to_copy,
            );
        } else {
            core::ptr::copy_nonoverlapping(
                self.buf_start.add(self.read_pos as usize),
                dst.as_mut_ptr(),
                head_space,
            );
            core::ptr::copy_nonoverlapping(
                self.buf_start,
                dst.as_mut_ptr().add(head_space),
                to_copy - head_space,
            );
        }

        self.read_pos = (self.read_pos + to_copy as u32) % self.buf_len;
        to_copy
    }
}
```

---

### 14.4 Rust — Embassy `read_until_idle` with Timeout

```rust
//! Practical pattern: receive variable-length frames delimited by UART idle gap.
//! Embassy's `read_until_idle` uses the hardware IDLE-line interrupt + DMA internally.

use embassy_stm32::usart::{Error, Uart};
use embassy_time::{Duration, TimeoutError, with_timeout};

/// Maximum frame size we accept.
const MAX_FRAME: usize = 256;

/// Receive one frame (ends at IDLE gap) with a hard deadline.
/// Returns the number of bytes received, or an error.
pub async fn receive_frame(
    uart: &mut Uart<'_, impl embassy_stm32::usart::BasicInstance, impl embassy_stm32::dma::Channel, impl embassy_stm32::dma::Channel>,
    buf: &mut [u8; MAX_FRAME],
    timeout: Duration,
) -> Result<usize, FrameError> {
    match with_timeout(timeout, uart.read_until_idle(buf)).await {
        Ok(Ok(n))              => Ok(n),
        Ok(Err(Error::Overrun)) => Err(FrameError::Overrun),
        Ok(Err(Error::Framing)) => Err(FrameError::Framing),
        Ok(Err(e))             => Err(FrameError::Uart(e)),
        Err(TimeoutError)      => Err(FrameError::Timeout),
    }
}

#[derive(Debug)]
pub enum FrameError {
    Timeout,
    Overrun,
    Framing,
    Uart(embassy_stm32::usart::Error),
}

// Usage in an async task:
// let mut buf = [0u8; MAX_FRAME];
// match receive_frame(&mut uart, &mut buf, Duration::from_millis(100)).await {
//     Ok(n)  => process(&buf[..n]),
//     Err(e) => handle_error(e),
// }
```

---

## 15. Performance Tuning and Benchmarking

### Buffer Sizing

The DMA buffer size directly controls interrupt rate and processing latency:

```
interrupt_rate = baud_rate / (10 * buf_size_bytes)

At 921600 baud:
  buf =  64 bytes → IRQ every  0.69 ms  (high overhead, low latency)
  buf = 256 bytes → IRQ every  2.78 ms  (balanced)
  buf = 512 bytes → IRQ every  5.56 ms  (low overhead, higher latency)
  buf = 4096 bytes → IRQ every 44.4 ms  (very low overhead)
```

Choose based on your latency budget. For command-response protocols, 64–256 bytes is typical. For
bulk data streaming (logging, audio), 1–4 KB buffers minimise CPU load.

### Bus Bandwidth

DMA competes with the CPU on the AHB/AXI bus. On STM32H7, SRAM is partitioned across multiple
buses to allow simultaneous DMA and CPU access. Place DMA buffers in SRAM regions accessible by
both the DMAC and the CPU without bus contention.

### Burst Mode

Enabling DMA burst (e.g., 4-beat burst) reduces bus arbitration overhead for large transfers. Only
useful when the peripheral FIFO is large enough to accumulate burst-sized data, and the memory
region supports burst (most SRAM does).

### Benchmark Approach

```c
/* Measure effective DMA throughput */
uint32_t t0 = DWT->CYCCNT;
uart_dma_send(big_buf, 4096);          /* blocks until TC */
uint32_t cycles = DWT->CYCCNT - t0;

float cpu_mhz   = 168.0f;
float time_ms   = cycles / (cpu_mhz * 1000.0f);
float throughput = 4096.0f / (time_ms / 1000.0f);  /* bytes/s */
```

---

## 16. Common Pitfalls

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| **Cache not cleaned before TX DMA** | Corrupt / old data transmitted | `SCB_CleanDCache_by_Addr()` before enabling TX DMA |
| **Cache not invalidated after RX DMA** | Application reads stale data | `SCB_InvalidateDCache_by_Addr()` in TC/IDLE callback |
| **Buffer not `static` (stack allocated)** | Crash / random corruption when function returns | Declare RX/TX buffers as `static` or global |
| **Misaligned buffer for cache ops** | Hard fault (alignment fault) | `__attribute__((aligned(32)))` / `#[repr(align(32))]` |
| **NDTR read during DMA write** | Race condition, wrong byte count | Disable interrupts briefly or use atomic snapshot |
| **Not clearing DMA interrupt flags** | ISR fires repeatedly (interrupt storm) | Always clear TCIF/HTIF in the ISR before processing |
| **Forgetting to re-enable UART DMA after error** | RX silently stops after first overrun | Check ORE/FE flags in UART status and re-arm |
| **HT interrupt left enabled unnecessarily** | CPU wakes too often | Disable HT IRQ if using IDLE-line detection instead |
| **Buffer size not a multiple of cache line** | Partial cache line invalidation corrupts adjacent data | Round buffer sizes up to 32-byte (cache line) multiples |
| **No timeout on DMA TX** | System deadlocks if TX FIFO stalls | Always pair DMA start with a hardware or software timeout |

---

## 17. Summary

DMA integration transforms UART from a CPU-intensive, interrupt-per-byte peripheral into an
efficient bulk-data pipe with negligible processor overhead. The key architectural decisions are:

**Buffer strategy** — Circular mode with ping-pong (HT + TC interrupts) or IDLE-line detection is
the gold standard for RX. TX typically uses single-shot normal mode, re-armed per packet.

**Cache coherency** — On any MCU with a data cache (Cortex-M7, A-series), cache maintenance is
mandatory. For TX, clean (flush) before starting DMA. For RX, invalidate after DMA completes.
Failure to do so produces silent data corruption that is extremely difficult to debug.

**Error handling** — Every DMA stream has a Transfer Error flag. Every UART has overrun, framing,
and noise error flags. Production code must handle all of them with defined recovery paths.

**Latency vs. throughput** — Smaller DMA buffers reduce latency but increase CPU load (more
interrupts). Profile your baud rate and packet cadence to find the optimal buffer size for your
application.

**Language choice** — In C, HAL-based DMA APIs are quick to integrate but hide complexity;
bare-metal register programming gives full control. In Rust, Embassy's async UART with DMA provides
safe, zero-cost abstractions that compile to the same register-level code, with the added benefit
of compile-time lifetime tracking preventing use-after-free of DMA buffers — a common source of
hard-to-reproduce bugs in C codebases.

The combination of circular DMA, IDLE-line detection, and ping-pong buffering is the industry-
standard pattern for high-throughput embedded UART and is directly applicable to baud rates from
9600 all the way to 10+ Mbit/s on modern MCUs.

---

*End of Document — 14. DMA Integration*