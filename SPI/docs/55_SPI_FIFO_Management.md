# 55. SPI FIFO Management

**Architecture & Concepts** — ASCII diagrams of the TX/RX FIFO hardware pipeline, a comparison table of FIFO depths across common MCUs (STM32, RP2040, ESP32, i.MX RT), and an explanation of threshold levels, packing, overrun/underrun conditions, and the FIFO-vs-DMA relationship.

**C/C++ Examples (3):**
- **STM32H7 bare-metal** — full FIFO threshold ISR with pre-fill, TX refill, RX drain, overrun handling, and `__WFI()` idle
- **RP2040 PL022** — both polled burst mode and interrupt-driven mode with raw register access
- **C++ template class** — a double-buffered `SpiFifoController<FIFO_DEPTH, BUFFER_SIZE>` with completion callbacks and HAL stubs

**Rust Examples (3):**
- **Embassy on RP2040** — async/await DMA-backed SPI transfer (zero CPU blocking during burst)
- **RTIC on STM32F4** — interrupt-driven, compile-time safe shared resource access
- **Generic `SpiHal` trait** — portable, `no_std` FIFO manager using `AtomicUsize`/`AtomicBool` for ISR-safe state

**Common Pitfalls** covers the five most frequent mistakes: forgetting RX drain during TX-only transfers, threshold-caused underrun, FIFO packing width mismatches, ISR/main race conditions, and incorrect OVR flag clearing sequences.


## Utilizing Hardware FIFOs for Burst Transfers and Reducing Interrupt Overhead

---

## Table of Contents

1. [Introduction](#introduction)
2. [SPI FIFO Architecture](#spi-fifo-architecture)
3. [Why FIFO Management Matters](#why-fifo-management-matters)
4. [Key Concepts](#key-concepts)
5. [FIFO Threshold Configuration](#fifo-threshold-configuration)
6. [Burst Transfer Strategies](#burst-transfer-strategies)
7. [Interrupt Overhead Reduction](#interrupt-overhead-reduction)
8. [C/C++ Implementation Examples](#cc-implementation-examples)
9. [Rust Implementation Examples](#rust-implementation-examples)
10. [Common Pitfalls](#common-pitfalls)
11. [Summary](#summary)

---

## Introduction

The Serial Peripheral Interface (SPI) is a synchronous, full-duplex serial communication protocol widely used in embedded systems to connect microcontrollers to sensors, displays, flash memory, ADCs, and other peripherals. While basic SPI operation handles one byte at a time with an interrupt or polling loop per byte, this approach does not scale well for high-throughput or latency-sensitive applications.

**SPI FIFO (First-In, First-Out) Management** is the technique of leveraging hardware-integrated transmit (TX) and receive (RX) FIFO buffers built into modern SPI controllers. Instead of triggering a CPU interrupt or spinning in a polling loop for every single byte, the hardware can queue multiple bytes, notify the CPU only when the FIFO reaches a configured threshold, and sustain continuous bus traffic with minimal processor intervention.

This topic is critical for:

- **High-speed data streaming** (e.g., reading audio samples, sensor bursts)
- **Display refresh** (pushing large pixel buffers to TFT/OLED displays)
- **Flash memory access** (bulk read/write of NOR or NAND flash)
- **Reducing CPU load** in real-time systems where interrupt latency budget is tight

---

## SPI FIFO Architecture

### Physical Structure

A hardware SPI controller with FIFO support typically contains:

```
┌──────────────────────────────────────────────────────────┐
│                    SPI Controller                        │
│                                                          │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐  │
│  │  TX FIFO    │───▶│  Shift Reg  │───▶│  RX FIFO    │  │
│  │  (N words)  │    │  (N bits)   │    │  (N words)  │  │
│  └──────┬──────┘    └──────┬──────┘    └──────┬──────┘  │
│         │                 │                   │          │
│  TX Fill│Level      SCLK  │Gen          RX Fill│Level    │
│         ▼                 ▼                   ▼          │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              Status & Control Registers             │ │
│  │  TX_LEVEL  RX_LEVEL  TX_THRESH  RX_THRESH  SR/CR   │ │
│  └─────────────────────────────────────────────────────┘ │
│         │                                     │          │
│    TX Empty/                             RX Full/        │
│    Half-Full IRQ                         Threshold IRQ   │
└──────────────────────────────────────────────────────────┘
```

### FIFO Depth

Common FIFO depths by platform:

| Platform / MCU         | TX FIFO Depth | RX FIFO Depth | Word Width     |
|------------------------|---------------|---------------|----------------|
| STM32F4 (SPI v1)       | 1 byte        | 1 byte        | 8 or 16 bit    |
| STM32H7 (SPI v2)       | 16 bytes      | 16 bytes      | 4–32 bit       |
| RP2040 (PL022)         | 8 words       | 8 words       | 4–16 bit       |
| ESP32 (GPSPI)          | 64 bytes      | 64 bytes      | 1–32 bit       |
| NXP i.MX RT            | 16 words      | 16 words      | 8–32 bit       |
| Nordic nRF52840 (SPIM) | EasyDMA only  | EasyDMA only  | 8 bit          |

### Register Overview (Generic)

```
SPI_CR1   – Control Register 1:  Master/Slave, CPOL, CPHA, Baud, etc.
SPI_CR2   – Control Register 2:  FIFO threshold, DS (data size), TXEIE, RXNEIE
SPI_SR    – Status Register:     TXE, RXNE, FTLVL, FRLVL, BSY, OVR
SPI_DR    – Data Register:       Write to TX FIFO, Read from RX FIFO
SPI_TXFTHLV – TX FIFO threshold level (platform-specific)
SPI_RXFTHLV – RX FIFO threshold level (platform-specific)
```

---

## Why FIFO Management Matters

### Without FIFO (Byte-by-Byte Polling)

```
CPU Timeline:
  [Write DR] → [Poll TXE] → [Poll TXE] → [Poll TXE] → [Read DR]
      │              │            │            │             │
   Byte 0        Waiting...   Waiting...   Byte 1 sent   Byte 0 RX
```

Each byte requires the CPU to:
1. Write to the data register
2. Spin or wait for interrupt
3. Re-enter the handler / loop body
4. Check status bits again

For a 1 MHz SPI clock, each byte takes ~8 µs. At 10,000 bytes, that is 80 ms of pure busy-wait or 10,000 context switches.

### With FIFO (Burst Mode)

```
CPU Timeline:
  [Fill TX FIFO 8 bytes] → [Idle / other work] → [IRQ: refill / drain RX]
         │                        │                        │
    8 bytes queued        Hardware shifts bits       Only 1 IRQ per 8 bytes
```

The CPU writes a burst of bytes into the TX FIFO, the hardware clock engine drains it automatically, and the CPU is only woken when the FIFO hits a threshold — reducing interrupt frequency by the FIFO depth factor.

---

## Key Concepts

### 1. TX FIFO Threshold (TXFTHLV / TXFTH)

The TX FIFO threshold defines at what fill level the **TXE (TX Empty/Below Threshold)** interrupt or flag is set. A lower threshold means more frequent interrupts but smaller latency. A higher threshold means fewer interrupts but requires the software to keep up with the bus.

```
Threshold = FIFO_DEPTH / 2  →  "Half Empty" trigger (common default)
Threshold = 1               →  "Single Empty Slot" trigger (most responsive)
Threshold = FIFO_DEPTH      →  "Completely Empty" trigger (lowest overhead)
```

### 2. RX FIFO Threshold (RXFTHLV / RXFTH)

The RX FIFO threshold determines when an **RXNE (RX Not Empty / Above Threshold)** event fires. Setting this too high can cause RX FIFO overflow (overrun error) if the CPU is slow to drain.

### 3. FIFO Overrun and Underrun

| Condition  | Cause                                          | Effect                         |
|------------|------------------------------------------------|--------------------------------|
| TX Underrun | CPU did not refill TX FIFO in time            | MOSI idles, data gap on bus    |
| RX Overrun  | CPU did not drain RX FIFO before it filled    | Incoming bytes lost (OVR flag) |

### 4. FIFO Packing (Word Width)

Many controllers let you pack multiple 8-bit values into a single 16-bit or 32-bit FIFO entry (data packing). This doubles or quadruples effective throughput without changing the FIFO depth.

```c
// Write two 8-bit bytes as one 16-bit FIFO entry (STM32 packing)
*(__IO uint16_t *)&SPI1->DR = (byte1 << 8) | byte0;
```

### 5. FIFO vs. DMA

FIFO management and DMA are complementary:
- **FIFO alone**: CPU still refills/drains the FIFO, but less frequently
- **DMA + FIFO**: DMA feeds/drains the FIFO from memory; CPU is nearly free
- **FIFO without DMA**: Useful when DMA channels are scarce or transfers are short

---

## FIFO Threshold Configuration

### Threshold Selection Strategy

```
Transfer Length   │ Recommended TX Threshold  │ Recommended RX Threshold
──────────────────┼───────────────────────────┼─────────────────────────
< FIFO depth      │ 1 (fire when any slot free)│ Transfer length - 1
= FIFO depth      │ FIFO/2 (half-empty)        │ FIFO/2 (half-full)
> FIFO depth      │ FIFO/2                     │ FIFO/2
Streaming         │ FIFO/4 (aggressive)        │ FIFO/4
```

### Threshold Effects on Latency vs. Overhead

```
High Threshold ──────────────────────────────▶ Low Threshold
  Low IRQ count        Balanced          High IRQ count
  Low CPU overhead   trade-off           Low latency
  High latency risk                      High CPU overhead
  Risk of underrun                       Risk of starvation
```

---

## Burst Transfer Strategies

### Strategy 1: Threshold-Driven Interrupt Refill

```
1. Pre-fill TX FIFO to capacity before enabling SPI
2. Enable TX threshold interrupt
3. In ISR: refill TX FIFO up to (FIFO_DEPTH - current_level) words
4. When all bytes sent: disable TX interrupt, handle completion
```

### Strategy 2: Double Buffering

```
Buffer A ──▶ [TX FIFO] ──▶ SPI Bus
                  ▲
CPU prepares Buffer B while A is transmitting
On threshold IRQ: swap A↔B, refill from new Buffer A
```

### Strategy 3: Circular FIFO with DMA

```
Memory Ring ──▶ DMA ──▶ TX FIFO ──▶ SPI Bus
               DMA refills FIFO from ring automatically
               CPU only updates ring write pointer
```

---

## Interrupt Overhead Reduction

### Comparison of Transfer Methods

```
Method              CPU Cycles per Byte   Interrupts (1000 bytes)
─────────────────────────────────────────────────────────────────
Polling (no FIFO)        ~50–200               0 (CPU busy)
Byte IRQ (no FIFO)       ~100–400           1,000
FIFO (depth 8) IRQ        ~15–50              125
FIFO (depth 16) IRQ       ~10–30               63
DMA + FIFO                ~2–5 (setup)          1–2 (complete)
```

### ISR Design for Low Overhead

Key principles:
1. **Batch reads/writes** — service as many FIFO slots as possible per ISR entry
2. **Avoid dynamic allocation** in ISR — use static transfer descriptors
3. **Use FIFO level registers** — read `FTLVL`/`FRLVL` once, loop on that count
4. **Clear flags correctly** — some controllers require reading DR to clear RXNE

---

## C/C++ Implementation Examples

### Example 1: Basic FIFO Configuration (STM32H7, HAL-free)

```c
#include <stdint.h>
#include <stddef.h>
#include "stm32h7xx.h"

#define SPI_FIFO_DEPTH   16u      /* STM32H7 SPI FIFO: 16 bytes         */
#define SPI_FIFO_THRESH  8u       /* Trigger at half-full / half-empty   */

typedef struct {
    const uint8_t *tx_buf;
    uint8_t       *rx_buf;
    volatile size_t tx_remaining;
    volatile size_t rx_remaining;
    volatile int    done;
} spi_transfer_t;

static spi_transfer_t g_xfer;

/* ------------------------------------------------------------------ */
/* Configure SPI1 for FIFO-driven master operation                     */
/* ------------------------------------------------------------------ */
void spi_fifo_init(void)
{
    /* Enable clocks */
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    /* Reset SPI */
    SPI1->CR1 = 0;
    SPI1->CR2 = 0;
    SPI1->CFG1 = 0;
    SPI1->CFG2 = 0;

    /* CFG1: 8-bit data size, FIFO threshold = 8 bytes (FTHLV=7 → 8 frames) */
    SPI1->CFG1 = (7u  << SPI_CFG1_FTHLV_Pos)   /* RX FIFO threshold = 8    */
               | (7u  << SPI_CFG1_DSIZE_Pos)    /* Data size = 8 bits       */
               | (5u  << SPI_CFG1_MBR_Pos);     /* Baud = PCLK/64           */

    /* CFG2: Master mode, CPOL=0, CPHA=0, MSB first, SW NSS */
    SPI1->CFG2 = SPI_CFG2_MASTER
               | SPI_CFG2_SSM
               | SPI_CFG2_SSOE;

    /* Enable TX/RX FIFO threshold interrupts */
    SPI1->IER = SPI_IER_TXPIE    /* TX FIFO packet threshold             */
              | SPI_IER_RXPIE    /* RX FIFO packet threshold             */
              | SPI_IER_OVRIE;   /* Overrun error                        */

    NVIC_SetPriority(SPI1_IRQn, 2);
    NVIC_EnableIRQ(SPI1_IRQn);
}

/* ------------------------------------------------------------------ */
/* Start a non-blocking burst transfer                                 */
/* ------------------------------------------------------------------ */
void spi_fifo_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    g_xfer.tx_buf       = tx;
    g_xfer.rx_buf       = rx;
    g_xfer.tx_remaining = len;
    g_xfer.rx_remaining = len;
    g_xfer.done         = 0;

    /* Set transfer size register */
    SPI1->CR2 = (uint32_t)len;

    /* Enable SPI — hardware drives CS and begins clocking */
    SPI1->CR1 |= SPI_CR1_SPE;

    /* Kick-start: pre-fill TX FIFO before first IRQ */
    size_t prefill = (len < SPI_FIFO_DEPTH) ? len : SPI_FIFO_DEPTH;
    for (size_t i = 0; i < prefill; i++) {
        *(volatile uint8_t *)&SPI1->TXDR = g_xfer.tx_buf[i];
        g_xfer.tx_remaining--;
    }

    /* Start transfer */
    SPI1->CR1 |= SPI_CR1_CSTART;
}

/* ------------------------------------------------------------------ */
/* Interrupt Service Routine                                           */
/* ------------------------------------------------------------------ */
void SPI1_IRQHandler(void)
{
    uint32_t sr = SPI1->SR;

    /* ---- RX FIFO threshold reached: drain RX FIFO ---- */
    if (sr & SPI_SR_RXP) {
        while ((SPI1->SR & SPI_SR_RXWNE) && g_xfer.rx_remaining > 0) {
            uint8_t byte = *(volatile uint8_t *)&SPI1->RXDR;
            if (g_xfer.rx_buf) {
                *g_xfer.rx_buf++ = byte;
            }
            g_xfer.rx_remaining--;
        }
    }

    /* ---- TX FIFO below threshold: refill TX FIFO ---- */
    if (sr & SPI_SR_TXP) {
        /* How many free slots are in the TX FIFO? */
        uint32_t free_slots = SPI_FIFO_DEPTH
            - ((SPI1->SR & SPI_SR_TXLVL_Msk) >> SPI_SR_TXLVL_Pos);

        size_t to_send = (g_xfer.tx_remaining < free_slots)
                       ? g_xfer.tx_remaining : free_slots;

        for (size_t i = 0; i < to_send; i++) {
            *(volatile uint8_t *)&SPI1->TXDR = *g_xfer.tx_buf++;
            g_xfer.tx_remaining--;
        }

        /* Disable TX interrupt when nothing left to send */
        if (g_xfer.tx_remaining == 0) {
            SPI1->IER &= ~SPI_IER_TXPIE;
        }
    }

    /* ---- Overrun error: clear and flag ---- */
    if (sr & SPI_SR_OVR) {
        SPI1->IFCR = SPI_IFCR_OVRC;
        /* Handle error: signal application layer */
    }

    /* ---- Transfer complete ---- */
    if (sr & SPI_SR_EOT) {
        SPI1->IFCR = SPI_IFCR_EOTC;
        SPI1->CR1 &= ~SPI_CR1_SPE;
        g_xfer.done = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Wait for transfer completion (blocking poll on flag)                */
/* ------------------------------------------------------------------ */
void spi_fifo_wait(void)
{
    while (!g_xfer.done) {
        __WFI();   /* Sleep until next IRQ — saves power */
    }
}
```

---

### Example 2: RP2040 PL022 FIFO Management (C, bare-metal)

```c
#include "hardware/spi.h"
#include "hardware/regs/spi.h"
#include "pico/stdlib.h"
#include <string.h>

#define SPI_INST    spi0
#define SPI_BASE    SPI0_BASE
#define FIFO_DEPTH  8u          /* RP2040 PL022: 8-entry TX and RX FIFO */

/* Raw register access helpers */
#define SPI_REG(offset)     (*(volatile uint32_t *)(SPI_BASE + (offset)))
#define SSPSR               SPI_REG(SPI_SSPSR_OFFSET)
#define SSPCR1              SPI_REG(SPI_SSPCR1_OFFSET)
#define SSPDR               SPI_REG(SPI_SSPDR_OFFSET)
#define SSPIMSC             SPI_REG(SPI_SSPIMSC_OFFSET)
#define SSPICR              SPI_REG(SPI_SSPICR_OFFSET)

/* Status bit helpers */
#define TX_NOT_FULL()       (SSPSR & SPI_SSPSR_TNF_BITS)
#define RX_NOT_EMPTY()      (SSPSR & SPI_SSPSR_RNE_BITS)
#define SPI_BUSY()          (SSPSR & SPI_SSPSR_BSY_BITS)

/* ------------------------------------------------------------------ */
/* Burst write using FIFO — transmit only (ignore RX)                  */
/* ------------------------------------------------------------------ */
void spi_write_fifo_burst(const uint8_t *data, size_t len)
{
    size_t tx_sent = 0;
    size_t rx_drained = 0;  /* Must drain RX to prevent overrun */

    while (tx_sent < len || rx_drained < len) {

        /* Fill TX FIFO as much as possible */
        while (tx_sent < len && TX_NOT_FULL()) {
            SSPDR = data[tx_sent++];
        }

        /* Drain RX FIFO to prevent overrun */
        while (rx_drained < tx_sent && RX_NOT_EMPTY()) {
            (void)SSPDR;   /* Discard received byte */
            rx_drained++;
        }
    }

    /* Wait for shift register to finish */
    while (SPI_BUSY()) tight_loop_contents();
}

/* ------------------------------------------------------------------ */
/* Burst read/write using FIFO — full duplex                           */
/* ------------------------------------------------------------------ */
void spi_transfer_fifo_burst(const uint8_t *tx, uint8_t *rx, size_t len)
{
    size_t tx_pos = 0;
    size_t rx_pos = 0;

    while (rx_pos < len) {
        /*
         * Fill TX FIFO: can send up to FIFO_DEPTH bytes ahead of RX.
         * Limit TX ahead-of-RX to avoid TX FIFO overflow (not possible
         * with PL022, but good practice for portability).
         */
        while (tx_pos < len
               && TX_NOT_FULL()
               && (tx_pos - rx_pos) < FIFO_DEPTH)
        {
            SSPDR = (tx != NULL) ? tx[tx_pos] : 0xFF;
            tx_pos++;
        }

        /* Drain RX FIFO immediately */
        while (RX_NOT_EMPTY() && rx_pos < len) {
            uint8_t byte = (uint8_t)SSPDR;
            if (rx != NULL) rx[rx_pos] = byte;
            rx_pos++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Threshold-interrupt driven transfer (RP2040 FIFO interrupt mode)    */
/* ------------------------------------------------------------------ */

static volatile const uint8_t *g_tx_ptr;
static volatile uint8_t       *g_rx_ptr;
static volatile size_t         g_tx_left;
static volatile size_t         g_rx_left;
static volatile bool           g_done;

void spi0_irq_handler(void)
{
    /* Read masked interrupt status */
    uint32_t mis = SPI_REG(SPI_SSPMIS_OFFSET);

    /* TX FIFO half-empty: refill */
    if (mis & SPI_SSPMIS_TXMIS_BITS) {
        while (g_tx_left > 0 && TX_NOT_FULL()) {
            SSPDR = *g_tx_ptr++;
            g_tx_left--;
        }
        if (g_tx_left == 0) {
            /* Disable TX interrupt */
            SSPIMSC &= ~SPI_SSPIMSC_TXIM_BITS;
        }
    }

    /* RX FIFO half-full: drain */
    if (mis & SPI_SSPMIS_RXMIS_BITS) {
        while (g_rx_left > 0 && RX_NOT_EMPTY()) {
            *g_rx_ptr++ = (uint8_t)SSPDR;
            g_rx_left--;
        }
        if (g_rx_left == 0) {
            SSPIMSC &= ~SPI_SSPIMSC_RXIM_BITS;
            g_done = true;
        }
    }

    /* Overrun: clear and handle */
    if (mis & SPI_SSPMIS_RORMIS_BITS) {
        SSPICR = SPI_SSPICR_RORIC_BITS;
        /* Signal error to application */
    }
}

void spi_fifo_irq_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    g_tx_ptr  = tx;
    g_rx_ptr  = rx;
    g_tx_left = len;
    g_rx_left = len;
    g_done    = false;

    /* Enable TX and RX FIFO interrupts */
    SSPIMSC |= SPI_SSPIMSC_TXIM_BITS | SPI_SSPIMSC_RXIM_BITS;

    irq_set_exclusive_handler(SPI0_IRQ, spi0_irq_handler);
    irq_set_enabled(SPI0_IRQ, true);

    /* Pre-fill TX FIFO to start transfer */
    size_t prefill = (len < FIFO_DEPTH) ? len : FIFO_DEPTH;
    for (size_t i = 0; i < prefill; i++) {
        SSPDR = *g_tx_ptr++;
        g_tx_left--;
    }

    /* Wait for completion */
    while (!g_done) __wfi();

    irq_set_enabled(SPI0_IRQ, false);
}
```

---

### Example 3: FIFO Overrun Protection with Double Buffering (C++)

```cpp
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>

template<size_t FIFO_DEPTH, size_t BUFFER_SIZE>
class SpiFifoController {
public:
    using CompletionCallback = std::function<void(bool /*success*/)>;

    struct TransferBuffer {
        std::array<uint8_t, BUFFER_SIZE> data;
        size_t                           length = 0;
        bool                             active = false;
    };

    SpiFifoController() : tx_head_(0), tx_tail_(0), error_(false) {}

    /* --------------------------------------------------------------- */
    /* Queue a buffer for transmission.                                 */
    /* Returns false if both double-buffers are occupied.              */
    /* --------------------------------------------------------------- */
    bool enqueue(const uint8_t *data, size_t len, CompletionCallback cb)
    {
        if (len > BUFFER_SIZE) return false;

        // Find an inactive buffer slot
        for (auto &buf : buffers_) {
            if (!buf.active) {
                std::memcpy(buf.data.data(), data, len);
                buf.length = len;
                buf.active = true;
                callback_  = cb;
                if (!is_busy()) start_transfer(buf);
                return true;
            }
        }
        return false;  // Both buffers occupied
    }

    /* --------------------------------------------------------------- */
    /* Called from TX threshold ISR to refill the hardware FIFO        */
    /* --------------------------------------------------------------- */
    void on_tx_threshold()
    {
        auto &buf = active_buffer();
        size_t free_slots = FIFO_DEPTH - hw_fifo_level();
        size_t available  = buf.length - tx_head_;
        size_t to_send    = std::min(free_slots, available);

        for (size_t i = 0; i < to_send; i++) {
            hw_write_fifo(buf.data[tx_head_++]);
        }

        if (tx_head_ >= buf.length) {
            hw_disable_tx_irq();
        }
    }

    /* --------------------------------------------------------------- */
    /* Called from RX threshold ISR to drain the hardware FIFO         */
    /* --------------------------------------------------------------- */
    void on_rx_threshold()
    {
        auto &buf = active_buffer();
        while (hw_rx_available() && rx_tail_ < buf.length) {
            rx_shadow_[rx_tail_++] = hw_read_fifo();
        }

        if (rx_tail_ >= buf.length) {
            buf.active = false;
            hw_disable_rx_irq();

            if (callback_) {
                callback_(!error_);
            }

            // Start next buffer if queued
            for (auto &next : buffers_) {
                if (next.active) {
                    start_transfer(next);
                    break;
                }
            }
        }
    }

    /* --------------------------------------------------------------- */
    /* Called from overrun ISR                                          */
    /* --------------------------------------------------------------- */
    void on_overrun() {
        error_ = true;
        hw_clear_overrun_flag();
    }

private:
    void start_transfer(TransferBuffer &buf)
    {
        tx_head_ = 0;
        rx_tail_ = 0;
        error_   = false;

        hw_enable_tx_irq();
        hw_enable_rx_irq();

        // Pre-fill TX FIFO
        size_t prefill = std::min(buf.length, FIFO_DEPTH);
        for (size_t i = 0; i < prefill; i++) {
            hw_write_fifo(buf.data[tx_head_++]);
        }
    }

    TransferBuffer &active_buffer()
    {
        for (auto &b : buffers_) if (b.active) return b;
        return buffers_[0];  // Fallback (should not happen)
    }

    bool is_busy() const {
        for (const auto &b : buffers_) if (b.active) return true;
        return false;
    }

    // Hardware abstraction stubs — implement per platform:
    void   hw_write_fifo(uint8_t b)    { (void)b; /* write to SPI DR */ }
    uint8_t hw_read_fifo()              { return 0; /* read SPI DR */ }
    size_t  hw_fifo_level()             { return 0; /* read TXLVL */ }
    bool    hw_rx_available()           { return false; /* RXNE */ }
    void   hw_enable_tx_irq()           { /* set TXEIE */ }
    void   hw_disable_tx_irq()          { /* clear TXEIE */ }
    void   hw_enable_rx_irq()           { /* set RXNEIE */ }
    void   hw_disable_rx_irq()          { /* clear RXNEIE */ }
    void   hw_clear_overrun_flag()      { /* clear OVR */ }

    std::array<TransferBuffer, 2> buffers_;
    std::array<uint8_t, BUFFER_SIZE>  rx_shadow_{};
    CompletionCallback callback_;
    size_t tx_head_ = 0;
    size_t rx_tail_ = 0;
    bool   error_   = false;
};

/* Usage example */
void example_double_buffer_usage()
{
    constexpr size_t FIFO_DEPTH   = 8;
    constexpr size_t BUFFER_SIZE  = 256;

    SpiFifoController<FIFO_DEPTH, BUFFER_SIZE> ctrl;

    uint8_t display_frame[256] = {};  /* Pixel data */
    /* Fill display_frame ... */

    ctrl.enqueue(display_frame, sizeof(display_frame), [](bool ok) {
        if (ok) { /* Update display pointer */ }
        else    { /* Handle error */ }
    });
}
```

---

## Rust Implementation Examples

### Example 1: RP2040 SPI FIFO with Embassy (async/await)

```rust
//! SPI FIFO burst transfer using Embassy async runtime on RP2040.
//! Cargo.toml: embassy-rp, embassy-executor, embassy-time

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_rp::peripherals::SPI0;
use embassy_rp::spi::{self, Spi, Config as SpiConfig, Phase, Polarity};
use embassy_rp::{bind_interrupts, gpio};
use embassy_rp::gpio::{Level, Output};
use embassy_time::{Duration, Timer};
use defmt::*;
use defmt_rtt as _;
use panic_probe as _;

bind_interrupts!(struct Irqs {
    SPI0_IRQ => embassy_rp::spi::InterruptHandler<SPI0>;
});

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = embassy_rp::init(Default::default());

    // Configure SPI with Embassy — FIFO management is handled internally
    let mut spi_config = SpiConfig::default();
    spi_config.frequency  = 8_000_000;   // 8 MHz
    spi_config.phase      = Phase::CaptureOnFirstTransition;
    spi_config.polarity   = Polarity::IdleLow;

    let mut spi = Spi::new(
        p.SPI0,
        p.PIN_18,   // SCK
        p.PIN_19,   // MOSI
        p.PIN_16,   // MISO
        p.DMA_CH0,  // TX DMA
        p.DMA_CH1,  // RX DMA
        spi_config,
        Irqs,
    );

    let mut cs = Output::new(p.PIN_17, Level::High);

    // Large burst: write 512 bytes to an SPI flash or display
    let tx_data: [u8; 512] = core::array::from_fn(|i| i as u8);
    let mut rx_data = [0u8; 512];

    loop {
        cs.set_low();

        // Embassy uses DMA + FIFO internally; this awaits completion
        // without blocking the executor — other tasks can run meanwhile
        spi.transfer(&mut rx_data, &tx_data).await.unwrap();

        cs.set_high();

        info!("Burst transfer complete. First RX bytes: {:?}", &rx_data[..8]);
        Timer::after(Duration::from_millis(100)).await;
    }
}
```

---

### Example 2: STM32 SPI FIFO with RTIC (Interrupt-Driven, Bare-Metal)

```rust
//! RTIC-based SPI FIFO management on STM32F4.
//! Uses stm32f4xx-hal and RTIC v2.

#![no_std]
#![no_main]

use rtic::app;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    spi::{self, Spi, NoMiso},
};
use heapless::Vec;

const FIFO_DEPTH: usize = 8;
const BUF_SIZE:   usize = 128;

#[app(device = stm32f4xx_hal::pac, peripherals = true, dispatchers = [EXTI0])]
mod app {
    use super::*;

    #[shared]
    struct Shared {
        spi: pac::SPI1,
    }

    #[local]
    struct Local {
        tx_buf:    [u8; BUF_SIZE],
        rx_buf:    [u8; BUF_SIZE],
        tx_pos:    usize,
        rx_pos:    usize,
        transfer_len: usize,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        let rcc = ctx.device.RCC.constrain();
        let clocks = rcc.cfgr.freeze();

        let gpioa = ctx.device.GPIOA.split();

        // Configure GPIO for SPI1: PA5=SCK, PA6=MISO, PA7=MOSI
        let _sck  = gpioa.pa5.into_alternate::<5>();
        let _mosi = gpioa.pa7.into_alternate::<5>();
        let _miso = gpioa.pa6.into_alternate::<5>();

        // Enable SPI1 clock
        unsafe {
            let rcc = &*pac::RCC::ptr();
            rcc.apb2enr.modify(|_, w| w.spi1en().enabled());
        }

        let spi = ctx.device.SPI1;

        // Configure SPI1 registers directly for FIFO control
        // STM32F4 has 1-byte FIFO; F7/H7 have deeper FIFOs.
        // Here we configure for 8-bit data, master mode.
        spi.cr1.write(|w| unsafe {
            w.mstr().master()       // Master mode
             .br().bits(0b010)      // fPCLK/8
             .cpol().idle_low()
             .cpha().first_edge()
             .ssm().enabled()       // Software NSS
             .ssi().set_bit()
             .lsbfirst().msbfirst()
             .dff().eight_bit()     // 8-bit data frame
        });

        // Enable RXNE and ERR interrupts
        spi.cr2.write(|w| {
            w.rxneie().not_masked()  // RX buffer not empty IRQ
             .errie().not_masked()   // Error IRQ
        });

        // Enable SPI
        spi.cr1.modify(|_, w| w.spe().enabled());

        // Enable NVIC interrupt
        unsafe {
            cortex_m::peripheral::NVIC::unmask(pac::Interrupt::SPI1);
        }

        let mut tx_buf = [0u8; BUF_SIZE];
        // Fill with test pattern
        for (i, b) in tx_buf.iter_mut().enumerate() {
            *b = i as u8;
        }

        // Kick off the first byte (F4 has 1-deep FIFO, triggers TXEIE)
        let first_byte = tx_buf[0];
        spi.cr2.modify(|_, w| w.txeie().not_masked());
        spi.dr.write(|w| unsafe { w.dr().bits(first_byte as u16) });

        (
            Shared { spi },
            Local {
                tx_buf,
                rx_buf: [0u8; BUF_SIZE],
                tx_pos: 1,
                rx_pos: 0,
                transfer_len: BUF_SIZE,
            },
        )
    }

    /// SPI1 interrupt handler — services TX empty and RX not-empty flags
    #[task(binds = SPI1, shared = [spi], local = [tx_buf, rx_buf, tx_pos, rx_pos, transfer_len])]
    fn spi1_irq(ctx: spi1_irq::Context) {
        let spi = ctx.shared.spi;
        let tx_buf       = ctx.local.tx_buf;
        let rx_buf       = ctx.local.rx_buf;
        let tx_pos       = ctx.local.tx_pos;
        let rx_pos       = ctx.local.rx_pos;
        let transfer_len = *ctx.local.transfer_len;

        let sr = spi.sr.read();

        // ---- TX Empty: load next byte ----
        if sr.txe().bit_is_set() && *tx_pos < transfer_len {
            let byte = tx_buf[*tx_pos];
            spi.dr.write(|w| unsafe { w.dr().bits(byte as u16) });
            *tx_pos += 1;

            if *tx_pos >= transfer_len {
                // Disable TX empty interrupt — nothing left to send
                spi.cr2.modify(|_, w| w.txeie().masked());
            }
        }

        // ---- RX Not Empty: read received byte ----
        if sr.rxne().bit_is_set() {
            let byte = spi.dr.read().dr().bits() as u8;
            if *rx_pos < transfer_len {
                rx_buf[*rx_pos] = byte;
                *rx_pos += 1;
            }
        }

        // ---- Overrun error: clear by reading DR then SR ----
        if sr.ovr().bit_is_set() {
            let _ = spi.dr.read();
            let _ = spi.sr.read();
        }
    }

    #[idle]
    fn idle(_: idle::Context) -> ! {
        loop {
            cortex_m::asm::wfi();  // Sleep until next interrupt
        }
    }
}
```

---

### Example 3: Generic SPI FIFO Manager (no_std, Portable)

```rust
//! Platform-agnostic SPI FIFO manager.
//! Implement the `SpiHal` trait for your specific hardware.

#![no_std]

use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

/// Hardware Abstraction Layer trait for SPI FIFO operations.
pub trait SpiHal {
    type Error;

    /// Returns the maximum number of words that can currently be written
    /// to the TX FIFO without blocking.
    fn tx_fifo_free_slots(&self) -> usize;

    /// Returns the number of words available in the RX FIFO.
    fn rx_fifo_available(&self) -> usize;

    /// Write a single word to the TX FIFO (non-blocking).
    fn tx_write(&mut self, word: u8);

    /// Read a single word from the RX FIFO (non-blocking).
    fn rx_read(&mut self) -> u8;

    /// Set TX FIFO threshold interrupt enable.
    fn set_tx_irq(&mut self, enabled: bool);

    /// Set RX FIFO threshold interrupt enable.
    fn set_rx_irq(&mut self, enabled: bool);

    /// Clear overrun error flag.
    fn clear_overrun(&mut self);
}

/// Transfer descriptor — passed to the FIFO manager.
pub struct Transfer<'a> {
    pub tx: Option<&'a [u8]>,
    pub rx: Option<&'a mut [u8]>,
    pub len: usize,
}

/// State machine for FIFO-driven transfers.
pub struct FifoManager {
    tx_pos:    AtomicUsize,
    rx_pos:    AtomicUsize,
    total_len: AtomicUsize,
    done:      AtomicBool,
    overrun:   AtomicBool,
}

impl FifoManager {
    pub const fn new() -> Self {
        Self {
            tx_pos:    AtomicUsize::new(0),
            rx_pos:    AtomicUsize::new(0),
            total_len: AtomicUsize::new(0),
            done:      AtomicBool::new(false),
            overrun:   AtomicBool::new(false),
        }
    }

    /// Start a new transfer.  Call from task/thread context.
    pub fn start<H: SpiHal>(
        &self,
        hal: &mut H,
        tx: &[u8],
        len: usize,
        fifo_depth: usize,
    ) {
        self.tx_pos.store(0, Ordering::Relaxed);
        self.rx_pos.store(0, Ordering::Relaxed);
        self.total_len.store(len, Ordering::Relaxed);
        self.done.store(false, Ordering::Release);
        self.overrun.store(false, Ordering::Relaxed);

        // Pre-fill TX FIFO
        let prefill = len.min(fifo_depth).min(hal.tx_fifo_free_slots());
        for i in 0..prefill {
            hal.tx_write(if i < tx.len() { tx[i] } else { 0xFF });
        }
        self.tx_pos.store(prefill, Ordering::Relaxed);

        hal.set_tx_irq(true);
        hal.set_rx_irq(true);
    }

    /// Service TX threshold interrupt.  Call from ISR.
    pub fn on_tx_threshold<H: SpiHal>(&self, hal: &mut H, tx: &[u8]) {
        let total = self.total_len.load(Ordering::Relaxed);
        let mut tx_pos = self.tx_pos.load(Ordering::Relaxed);
        let free = hal.tx_fifo_free_slots();

        let to_send = free.min(total - tx_pos);
        for i in 0..to_send {
            let idx = tx_pos + i;
            hal.tx_write(if idx < tx.len() { tx[idx] } else { 0xFF });
        }
        tx_pos += to_send;
        self.tx_pos.store(tx_pos, Ordering::Relaxed);

        if tx_pos >= total {
            hal.set_tx_irq(false);
        }
    }

    /// Service RX threshold interrupt.  Call from ISR.
    pub fn on_rx_threshold<H: SpiHal>(&self, hal: &mut H, rx: &mut [u8]) {
        let total = self.total_len.load(Ordering::Relaxed);
        let mut rx_pos = self.rx_pos.load(Ordering::Relaxed);

        while hal.rx_fifo_available() > 0 && rx_pos < total {
            let byte = hal.rx_read();
            if rx_pos < rx.len() {
                rx[rx_pos] = byte;
            }
            rx_pos += 1;
        }
        self.rx_pos.store(rx_pos, Ordering::Relaxed);

        if rx_pos >= total {
            hal.set_rx_irq(false);
            self.done.store(true, Ordering::Release);
        }
    }

    /// Service overrun interrupt.  Call from ISR.
    pub fn on_overrun<H: SpiHal>(&self, hal: &mut H) {
        self.overrun.store(true, Ordering::Relaxed);
        hal.clear_overrun();
    }

    /// Returns true when the transfer is complete.
    pub fn is_done(&self) -> bool {
        self.done.load(Ordering::Acquire)
    }

    /// Returns true if an overrun occurred.
    pub fn had_overrun(&self) -> bool {
        self.overrun.load(Ordering::Relaxed)
    }
}

// ---------------------------------------------------------------------------
// Usage example (pseudo-code illustrating the API):
// ---------------------------------------------------------------------------
//
//  static FIFO_MGR: FifoManager = FifoManager::new();
//
//  fn send_display_frame(hal: &mut MyHal, frame: &[u8]) {
//      FIFO_MGR.start(hal, frame, frame.len(), 16);
//      while !FIFO_MGR.is_done() { cortex_m::asm::wfi(); }
//      assert!(!FIFO_MGR.had_overrun());
//  }
//
//  // In ISR:
//  fn spi_irq_handler(hal: &mut MyHal, tx: &[u8], rx: &mut [u8]) {
//      if tx_threshold_fired() { FIFO_MGR.on_tx_threshold(hal, tx); }
//      if rx_threshold_fired() { FIFO_MGR.on_rx_threshold(hal, rx); }
//      if overrun_fired()      { FIFO_MGR.on_overrun(hal); }
//  }
```

---

## Common Pitfalls

### 1. Forgetting to Drain the RX FIFO During TX-Only Transfers

Every byte clocked out on MOSI results in a byte clocked in on MISO, even if you do not care about the received data. If you only fill the TX FIFO and never read the RX FIFO, it will overflow and set the OVR flag, potentially corrupting future transfers.

```c
/* WRONG — TX only, RX FIFO fills and overruns */
for (int i = 0; i < len; i++) {
    while (!(SPI->SR & SPI_SR_TXE)); // wait for TX empty
    SPI->DR = data[i];
}

/* CORRECT — drain RX even if unused */
for (int i = 0; i < len; i++) {
    while (!(SPI->SR & SPI_SR_TXE));
    SPI->DR = data[i];
    while (!(SPI->SR & SPI_SR_RXNE));
    (void)SPI->DR;  // discard
}
```

### 2. Incorrect FIFO Threshold Causing Underrun

If the TX FIFO threshold is set too high (fires only when completely empty), the ISR may not be called in time to refill the FIFO, causing a gap in the SPI clock output.

```
Timeline (FIFO depth = 8, threshold = 8 "completely empty"):
  TX: [8 bytes] → [GAP — FIFO drained before ISR responds] → [8 bytes]
                      ↑ Underrun! CS should stay low but SCLK pauses.
```

**Fix**: Set threshold to `FIFO_DEPTH / 2` or lower, giving the ISR time to refill before the FIFO fully drains.

### 3. Mismatched Data Width and FIFO Packing

If you configure the SPI for 16-bit frames but write only 8-bit values to the data register, the hardware may pack them unexpectedly or wait for a second byte to complete a frame.

```c
/* Danger: writing 8-bit to a 16-bit configured SPI on STM32 */
/* Use byte-pointer cast to force 8-bit access to DR register */
*(__IO uint8_t *)&SPI->DR = byte;  /* Correct for 8-bit in 16-bit mode */
```

### 4. Race Condition Between ISR and Main Code

Accessing `tx_pos` / `rx_pos` counters from both ISR and main context without atomics or critical sections leads to torn reads.

```c
/* WRONG — non-atomic read of a counter updated in ISR */
if (tx_pos >= total_len) { ... }

/* CORRECT — disable IRQ briefly or use compiler barriers */
__disable_irq();
size_t pos = tx_pos;
__enable_irq();
if (pos >= total_len) { ... }
```

In Rust, use `AtomicUsize` with appropriate `Ordering` as shown in Example 3.

### 5. Not Clearing Error Flags Correctly

On many STM32 devices, the OVR (overrun) flag is cleared by reading `DR` then `SR` in sequence — not by writing to a clear register. Failing to clear it keeps the error flag set and prevents further reception.

```c
/* Clear OVR on STM32 F4/F7: read DR, then SR */
volatile uint32_t dummy = SPI->DR;
dummy = SPI->SR;
(void)dummy;
```

---

## Summary

SPI FIFO management is a foundational technique for achieving high-throughput, low-CPU-overhead serial communication in embedded systems. The core ideas are:

**Hardware FIFOs** allow the SPI controller to buffer multiple words and sustain continuous bus traffic without per-byte CPU intervention. By setting appropriate TX and RX FIFO threshold levels, the CPU is notified only when a batch of data needs to be loaded or unloaded, dramatically reducing interrupt frequency — by a factor equal to the FIFO depth compared to byte-by-byte interrupts.

**Burst transfers** pre-fill the TX FIFO before enabling the SPI clock, then refill from an ISR only when the FIFO level drops below the threshold. On the receive side, the RX FIFO must always be drained promptly to prevent overrun errors, even if the received bytes are discarded.

**Double buffering and the producer-consumer pattern** allow the CPU to prepare the next data block in the background while the current block is being transmitted, achieving near-100% bus utilization.

**In C/C++**, FIFO management is done through direct register writes with careful pointer casts (especially for byte vs. half-word access on STM32), combined with ISR-driven refill logic and atomic access patterns for shared state.

**In Rust**, FIFO management benefits from the ownership model and trait abstractions. Frameworks like Embassy provide async/await over DMA-backed FIFO transfers, while RTIC enables safe ISR-driven access with shared resource management enforced at compile time. The `SpiHal` trait pattern enables portable, testable FIFO logic decoupled from hardware specifics.

**Key rules to always follow:**
- Always drain the RX FIFO during TX-only transfers to prevent overrun
- Set TX thresholds at `FIFO_DEPTH / 2` or lower to avoid underrun
- Use correct data-register access widths matching the configured frame size
- Protect shared transfer state with atomics or critical sections
- Clear error flags using the exact sequence required by your hardware

When combined with DMA (see topic 56), FIFO management enables SPI throughput approaching the theoretical maximum of the bus clock with near-zero CPU involvement.

---

*Document: 55_SPI_FIFO_Management.md — Part of the Embedded Systems SPI Programming Series*