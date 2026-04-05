# 17. UART Overrun Errors

**Concept & Hardware Mechanics** — explains the two-register shift/data architecture that makes overruns possible, how the ORE flag works, FIFO depths across common MCU families, and the CTS/RTS flow-control mechanism.

**C/C++ Examples (5 patterns):**
1. Bare-metal ORE detection & clearing (STM32F4 SR+DR sequence)
2. ISR-driven ring buffer with both hardware and software overrun counters
3. DMA circular buffer for high-throughput, low-CPU-overhead reception
4. C++ RAII `UartFlowGuard` that pauses the remote sender during long processing windows
5. Production-style error interrupt handler with timestamped error log

**Rust Examples (4 patterns):**
1. `embedded-hal`-based wrapper with typed error tracking and `drain()` helper
2. Lock-free SPSC ring buffer using `heapless` and atomics (ISR-safe, no heap, `no_std`)
3. STM32 PAC-level interrupt handler using the `stm32f4xx-hal` crate
4. **Typestate pattern** — the compiler enforces that a `Faulted` UART cannot be read until `recover()` is explicitly called, eliminating a whole class of silent-data bugs

**Also included:** RTOS considerations (FreeRTOS queue overrun detection, task priorities), a prevention strategy comparison table, and a final design-rules checklist.

> **Topic:** Handling buffer overflow when data arrives faster than processing

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [What Is a UART Overrun Error?](#2-what-is-a-uart-overrun-error)
3. [Root Causes](#3-root-causes)
4. [Hardware Mechanics](#4-hardware-mechanics)
5. [Detection Methods](#5-detection-methods)
6. [Prevention Strategies](#6-prevention-strategies)
7. [C/C++ Implementation Examples](#7-cc-implementation-examples)
8. [Rust Implementation Examples](#8-rust-implementation-examples)
9. [RTOS Considerations](#9-rtos-considerations)
10. [Summary](#10-summary)

---

## 1. Introduction

UART (Universal Asynchronous Receiver/Transmitter) communication is one of the most
ubiquitous serial protocols in embedded systems. Its simplicity makes it attractive,
but that same simplicity hides a subtle hazard: **overrun errors**. An overrun occurs
when incoming data arrives faster than the software (or DMA) can consume it, causing
the hardware to discard bytes before they are read.

Overrun errors are silent data killers. Unlike framing or parity errors, which indicate
a corrupt byte, an overrun means a byte was simply **never seen** by the application.
In protocols without checksums or acknowledgements this can be catastrophic.

---

## 2. What Is a UART Overrun Error?

A UART receiver contains at least two registers:

| Register | Role |
|---|---|
| **Shift Register (RSR)** | Assembles incoming bits one at a time from the RX line |
| **Receive Data Register (RDR)** | Holds the last completed byte, ready for software to read |

The flow is:

```
RX Line → Shift Register (RSR) → Receive Data Register (RDR) → CPU / DMA
```

An overrun happens when a **new byte finishes shifting in** while the **previous byte
still sits unread in the RDR**. The hardware has nowhere to store the new byte; it sets
the **Overrun Error (ORE)** flag in the status register and the new byte is lost.

```
┌─────────────────────────────────────────────────────────────────────┐
│  Time →                                                             │
│                                                                     │
│  Byte A ──► [RSR] ──► [RDR]  ← CPU hasn't read yet!               │
│                                                                     │
│  Byte B ──► [RSR] ──► ??? ── RDR full → OVERRUN! Byte B LOST      │
│                                                                     │
│                          ORE flag set in status register            │
└─────────────────────────────────────────────────────────────────────┘
```

On many MCUs (STM32, NXP Kinetis, PIC32, etc.) the ORE flag must be cleared explicitly
before further reception is possible; on others the UART stalls entirely until software
resets the error.

---

## 3. Root Causes

### 3.1 Software Too Slow

The most common cause. The CPU is busy doing other work — rendering a display, running
an algorithm, handling a higher-priority interrupt — and does not drain the receive
buffer in time.

### 3.2 Baud Rate vs. Processing Time Mismatch

At 921600 baud a new byte arrives every **~10.8 µs**. If the interrupt service routine
(ISR) takes 15 µs to execute, overruns are mathematically guaranteed.

### 3.3 Interrupt Latency

Long critical sections (`__disable_irq()` blocks) or priority inversions in an RTOS can
delay the RX interrupt long enough for the hardware buffer to fill.

### 3.4 No Hardware FIFO (or FIFO Too Small)

Many MCUs offer a 1-byte RDR. Some provide a small FIFO (4–32 bytes). Without a FIFO,
every single byte must be serviced within one bit-time of the stop bit.

### 3.5 DMA Misconfiguration

When using DMA, a misconfigured transfer length or wrap-around in circular mode can
leave the DMA engine unable to accept new data, causing the hardware FIFO (if present)
to back-fill and ultimately overflow.

### 3.6 Polling Mode at High Baud Rates

Polling (`while(!(UART->SR & RXNE)) {}`) is fine for debug output at 115200 baud
but breaks at higher rates in systems with any concurrency.

---

## 4. Hardware Mechanics

### 4.1 Status Register Flags (STM32 USART Example)

```
USART_SR (Status Register)
 Bit 3 – ORE  : Overrun Error
 Bit 2 – NE   : Noise Error
 Bit 1 – FE   : Framing Error
 Bit 0 – PE   : Parity Error
```

On **STM32F1/F2/F4**, clearing ORE requires a two-step sequence:
1. Read `USART_SR`
2. Read `USART_DR`

On **STM32L4/G4/H7** (with the new USART v3 IP), clearing is done by writing `1` to
the `ORECF` bit in `USART_ICR`.

### 4.2 Hardware FIFOs

Some peripherals include receive FIFOs:

| MCU Family | RX FIFO Depth |
|---|---|
| STM32H7 LPUART | 8 bytes |
| NXP i.MX RT | 4 bytes |
| ESP32 UART | 128 bytes |
| PL011 (RPi) | 16 bytes |
| 16550 (PC UART) | 16 bytes |

Enabling the FIFO and setting the watermark threshold to trigger an interrupt before the
FIFO is completely full gives software a window of multiple byte-times to respond.

### 4.3 Flow Control (Hardware CTS/RTS)

Hardware flow control uses the **RTS** (Request To Send) output to tell the remote
transmitter to pause. When the receive buffer is almost full, the MCU de-asserts RTS,
the sender stops, and no data is lost. This is the most reliable overrun prevention
when both ends support it.

---

## 5. Detection Methods

### 5.1 Polling the Status Register

```c
if (USART1->SR & USART_SR_ORE) {
    // Overrun detected — clear the flag and handle
}
```

### 5.2 Interrupt-Driven Detection

Configure the UART peripheral to generate an interrupt on error flags (EIE bit or
dedicated error interrupt). The ISR checks the ORE flag immediately.

### 5.3 Software Ring-Buffer Full Detection

Maintain a ring buffer in the RX ISR. If the ISR tries to enqueue a byte but the buffer
is full, that is a **software overrun** — the hardware received the byte but software
cannot store it.

### 5.4 Counters and Telemetry

Increment a diagnostic counter every time an overrun is detected. Expose the counter via
a debug UART channel, SWO trace, or RTT. Even in production firmware, monitoring this
counter is invaluable.

---

## 6. Prevention Strategies

| Strategy | Effectiveness | Cost |
|---|---|---|
| Hardware flow control (CTS/RTS) | ★★★★★ | Extra pins, both ends must support |
| DMA circular buffer | ★★★★☆ | DMA channel, more complex code |
| Large ring buffer in ISR | ★★★☆☆ | RAM usage |
| FIFO watermark interrupt | ★★★★☆ | Must be supported by hardware |
| Reduce baud rate | ★★★★★ | Lower throughput |
| Increase interrupt priority | ★★★☆☆ | May starve other IRQs |
| Reduce critical section duration | ★★★★☆ | Code restructuring |
| RTOS task with high priority | ★★★☆☆ | RTOS required |

---

## 7. C/C++ Implementation Examples

### 7.1 Basic Overrun Detection and Clearing (STM32 HAL / Bare-Metal)

```c
#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* ── Diagnostic counter ─────────────────────────────────────────────── */
static volatile uint32_t uart_overrun_count = 0;

/**
 * @brief  Check and clear an overrun error on USART1 (STM32F4).
 *         On F1/F2/F4 the ORE flag is cleared by reading SR then DR.
 * @return true if an overrun was detected and cleared
 */
bool UART_CheckAndClearOverrun(void)
{
    if (USART1->SR & USART_SR_ORE) {
        /* Step 1: read SR (already done by the if-condition above,
                   but we must do it again in sequence per RM) */
        volatile uint32_t tmp = USART1->SR;
        (void)tmp;

        /* Step 2: read DR to clear the flag */
        tmp = USART1->DR;
        (void)tmp;

        uart_overrun_count++;
        return true;
    }
    return false;
}

uint32_t UART_GetOverrunCount(void) { return uart_overrun_count; }
```

---

### 7.2 Ring Buffer with Software Overrun Tracking

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── Ring buffer ─────────────────────────────────────────────────────── */
#define UART_RX_BUF_SIZE  256u   /* Must be a power of 2 */
#define UART_RX_BUF_MASK  (UART_RX_BUF_SIZE - 1u)

typedef struct {
    volatile uint8_t  buf[UART_RX_BUF_SIZE];
    volatile uint16_t head;          /* Written by ISR  */
    volatile uint16_t tail;          /* Read  by app    */
    volatile uint32_t hw_overruns;   /* Hardware ORE count  */
    volatile uint32_t sw_overruns;   /* Software buffer-full count */
} UartRxRing;

static UartRxRing rx_ring = {0};

/* ── ISR — called from USART1_IRQHandler ─────────────────────────────── */
void UART_RxISR(void)
{
    uint32_t sr = USART1->SR;

    /* 1. Handle hardware overrun FIRST */
    if (sr & USART_SR_ORE) {
        volatile uint32_t dummy = USART1->DR;  /* clears ORE on F4 */
        (void)dummy;
        rx_ring.hw_overruns++;
        return;   /* Byte was lost; nothing to enqueue */
    }

    /* 2. Read the received byte */
    if (sr & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)(USART1->DR & 0xFFu);

        uint16_t next_head = (rx_ring.head + 1u) & UART_RX_BUF_MASK;

        if (next_head == rx_ring.tail) {
            /* Software buffer full — software overrun */
            rx_ring.sw_overruns++;
            /* Byte is discarded; oldest byte stays intact */
        } else {
            rx_ring.buf[rx_ring.head] = byte;
            rx_ring.head = next_head;
        }
    }
}

/* ── Application API ─────────────────────────────────────────────────── */
bool UART_Read(uint8_t *out_byte)
{
    if (rx_ring.tail == rx_ring.head) {
        return false;   /* Buffer empty */
    }
    *out_byte = rx_ring.buf[rx_ring.tail];
    rx_ring.tail = (rx_ring.tail + 1u) & UART_RX_BUF_MASK;
    return true;
}

uint16_t UART_BytesAvailable(void)
{
    return (rx_ring.head - rx_ring.tail) & UART_RX_BUF_MASK;
}

void UART_GetErrorStats(uint32_t *hw_oe, uint32_t *sw_oe)
{
    *hw_oe = rx_ring.hw_overruns;
    *sw_oe = rx_ring.sw_overruns;
}
```

---

### 7.3 DMA Circular Buffer (STM32 LL / Bare-Metal)

With DMA the hardware fills a circular buffer autonomously. The CPU reads from wherever
the DMA write pointer currently sits, so as long as the buffer is large enough and
drained fast enough, no interrupts are needed per byte.

```c
#include "stm32f4xx.h"
#include <string.h>
#include <stdint.h>

#define DMA_BUF_SIZE   512u

static uint8_t  dma_rx_buf[DMA_BUF_SIZE];
static uint16_t last_dma_pos = 0;

/**
 * @brief  Initialise USART1 + DMA2 Stream2 for circular RX (STM32F4).
 *         Call once after clock and GPIO init.
 */
void UART_DMA_Init(void)
{
    /* Enable clocks */
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* Configure USART1 — 115200, 8N1 */
    USART1->BRR  = 0x0683u;   /* APB2=84 MHz → 115200 baud */
    USART1->CR1  = USART_CR1_RE | USART_CR1_UE;
    USART1->CR3  = USART_CR3_DMAR;   /* Enable DMA for receiver */

    /* DMA2 Stream 2, Channel 4 (USART1_RX) */
    DMA2_Stream2->CR  = 0;            /* Reset */
    while (DMA2_Stream2->CR & DMA_SxCR_EN) {}  /* Wait for disable */

    DMA2_Stream2->PAR  = (uint32_t)&USART1->DR;
    DMA2_Stream2->M0AR = (uint32_t)dma_rx_buf;
    DMA2_Stream2->NDTR = DMA_BUF_SIZE;
    DMA2_Stream2->CR   = (4u << DMA_SxCR_CHSEL_Pos)  /* Channel 4 */
                       | DMA_SxCR_MINC               /* Memory increment */
                       | DMA_SxCR_CIRC               /* Circular mode    */
                       | DMA_SxCR_EN;                /* Enable           */
}

/**
 * @brief  Call periodically from the main loop (or a timer ISR).
 *         Processes any bytes the DMA has written since the last call.
 *
 * @param  out      Destination buffer
 * @param  out_len  Max bytes to copy
 * @return Number of bytes copied
 */
uint16_t UART_DMA_Process(uint8_t *out, uint16_t out_len)
{
    /* DMA NDTR counts DOWN; derive current write position */
    uint16_t dma_pos = DMA_BUF_SIZE - (uint16_t)DMA2_Stream2->NDTR;
    uint16_t count   = 0;

    while (last_dma_pos != dma_pos && count < out_len) {
        out[count++] = dma_rx_buf[last_dma_pos];
        last_dma_pos = (last_dma_pos + 1u) % DMA_BUF_SIZE;
    }

    /* Check for DMA overrun: if (dma_pos == last_dma_pos) and FIFO
       overflow interrupt fired, the circular wrap overtook us. */
    if (DMA2->LISR & DMA_LISR_FEIF2) {
        DMA2->LIFCR = DMA_LIFCR_CFEIF2;  /* Clear FIFO error flag */
        /* Log or count the overrun here */
    }

    return count;
}
```

---

### 7.4 C++ RAII Overrun Guard

```cpp
#include <cstdint>
#include <atomic>
#include <functional>

/**
 * @brief  RAII guard that temporarily pauses UART reception
 *         (de-asserts RTS) while a critical section executes,
 *         preventing overruns during long processing windows.
 *
 *  Usage:
 *      {
 *          UartFlowGuard guard([]{ RTS_Pause(); }, []{ RTS_Resume(); });
 *          doLongComputation();
 *      }  // RTS re-asserted here
 */
class UartFlowGuard {
public:
    explicit UartFlowGuard(std::function<void()> pause_fn,
                           std::function<void()> resume_fn)
        : resume_(std::move(resume_fn))
    {
        pause_fn();
    }

    ~UartFlowGuard() { resume_(); }

    /* Non-copyable, non-movable */
    UartFlowGuard(const UartFlowGuard&)            = delete;
    UartFlowGuard& operator=(const UartFlowGuard&) = delete;

private:
    std::function<void()> resume_;
};

/* ── Example hardware helpers (platform-specific) ────────────────────── */
static inline void RTS_Pause()
{
    GPIOA->BSRR = GPIO_BSRR_BR12;  /* PA12 = RTS, active-low: set high */
}

static inline void RTS_Resume()
{
    GPIOA->BSRR = GPIO_BSRR_BS12;  /* PA12 = RTS, assert low again     */
}

/* ── Usage in application code ───────────────────────────────────────── */
void ProcessLargeDataset(const uint8_t *data, size_t len)
{
    UartFlowGuard guard(RTS_Pause, RTS_Resume);

    /* Remote sender is now paused — no overrun risk during this block */
    for (size_t i = 0; i < len; ++i) {
        /* ... heavy computation ... */
    }
}   /* RTS re-asserted at scope exit */
```

---

### 7.5 STM32 USART Error Interrupt Handler (Production Pattern)

```c
#include "stm32f4xx.h"

/* Error log entry for deferred analysis */
typedef struct {
    uint32_t timestamp_ms;
    uint8_t  error_flags;    /* Bit-field: ORE | NE | FE | PE */
} UartErrorEntry;

#define ERROR_LOG_DEPTH  16u
static UartErrorEntry  error_log[ERROR_LOG_DEPTH];
static uint8_t         error_log_idx = 0;
extern uint32_t        system_tick_ms;   /* Provided by SysTick */

/* Trigger: USART1 interrupt with EIE bit set in CR3               */
/* Also triggered for RXNE; we handle both in one handler.          */
void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;
    uint32_t dr = USART1->DR;   /* Reading DR clears RXNE and ORE on F4 */

    uint8_t err = 0;

    if (sr & USART_SR_ORE) { err |= 0x04u; }
    if (sr & USART_SR_NE)  { err |= 0x02u; }
    if (sr & USART_SR_FE)  { err |= 0x01u; }

    if (err) {
        /* Log for later analysis — do NOT block the ISR */
        error_log[error_log_idx].timestamp_ms = system_tick_ms;
        error_log[error_log_idx].error_flags  = err;
        error_log_idx = (error_log_idx + 1u) % ERROR_LOG_DEPTH;
        return;   /* Byte associated with error is discarded */
    }

    if (sr & USART_SR_RXNE) {
        /* Valid byte — push to ring buffer (function from §7.2) */
        /* UART_PushByte((uint8_t)dr); */
        (void)dr;
    }
}
```

---

## 8. Rust Implementation Examples

Rust's ownership model and type system make it natural to encode protocol invariants
at compile time. The examples below target embedded `no_std` environments using the
`embedded-hal` traits and the `heapless` crate for fixed-capacity ring buffers.

### 8.1 Detecting Overrun with `embedded-hal` Serial Trait

```rust
//! Cargo.toml dependencies:
//!   embedded-hal = "0.2"
//!   nb           = "1.0"

use embedded_hal::serial::Read;
use nb::Error as NbError;

/// Error type that distinguishes hardware UART errors.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UartError {
    Overrun,
    Framing,
    Parity,
    Noise,
    Other,
}

/// Counters for diagnostic telemetry.
#[derive(Debug, Default)]
pub struct UartStats {
    pub hw_overruns: u32,
    pub framing:     u32,
    pub parity:      u32,
    pub noise:       u32,
}

/// Wrapper around any `embedded-hal` serial reader that tracks errors.
pub struct TrackedSerial<S> {
    inner: S,
    pub stats: UartStats,
}

impl<S, E> TrackedSerial<S>
where
    S: Read<u8, Error = E>,
    E: Into<UartError>,
{
    pub fn new(serial: S) -> Self {
        Self { inner: serial, stats: UartStats::default() }
    }

    /// Non-blocking read; returns `Ok(byte)`, `Err(WouldBlock)`,
    /// or an `Err(Other(UartError))` on hardware fault.
    pub fn read(&mut self) -> nb::Result<u8, UartError> {
        match self.inner.read() {
            Ok(byte) => Ok(byte),

            Err(NbError::WouldBlock) => Err(NbError::WouldBlock),

            Err(NbError::Other(e)) => {
                let uart_err = e.into();
                match uart_err {
                    UartError::Overrun  => self.stats.hw_overruns += 1,
                    UartError::Framing  => self.stats.framing     += 1,
                    UartError::Parity   => self.stats.parity      += 1,
                    UartError::Noise    => self.stats.noise        += 1,
                    UartError::Other    => {}
                }
                Err(NbError::Other(uart_err))
            }
        }
    }

    /// Drain all available bytes into `buf`. Returns bytes read.
    /// Overrun errors are counted but do not stop draining.
    pub fn drain(&mut self, buf: &mut [u8]) -> usize {
        let mut n = 0;
        for slot in buf.iter_mut() {
            match self.read() {
                Ok(b)                     => { *slot = b; n += 1; }
                Err(NbError::WouldBlock)  => break,
                Err(NbError::Other(_))    => { /* counted above; skip byte */ }
            }
        }
        n
    }
}
```

---

### 8.2 Lock-Free Ring Buffer (no_std, no heap)

```rust
//! Cargo.toml dependencies:
//!   heapless = "0.7"
//!   cortex-m = "0.7"

#![no_std]

use core::sync::atomic::{AtomicUsize, Ordering};
use heapless::spsc::{Consumer, Producer, Queue};

/// Fixed-capacity SPSC queue for UART bytes.
/// The `Queue` is statically allocated; producer lives in the ISR,
/// consumer in the application task.
static mut RX_QUEUE: Queue<u8, 256> = Queue::new();

/// Safety: only called once at init. Split into producer/consumer halves.
pub fn init_queue() -> (Producer<'static, u8, 256>, Consumer<'static, u8, 256>) {
    unsafe { RX_QUEUE.split() }
}

/// Global overrun counters (atomic for ISR safety without disabling interrupts).
static HW_OVERRUNS: AtomicUsize = AtomicUsize::new(0);
static SW_OVERRUNS: AtomicUsize = AtomicUsize::new(0);

/// Call from the UART RX interrupt with the producer half.
///
/// # Safety
/// Must only be called from the interrupt context that owns the `Producer`.
#[inline(always)]
pub fn isr_push_byte(producer: &mut Producer<'static, u8, 256>, byte: u8, hw_ore: bool) {
    if hw_ore {
        HW_OVERRUNS.fetch_add(1, Ordering::Relaxed);
        return; // Byte already lost at hardware level
    }

    if producer.enqueue(byte).is_err() {
        // Queue full — software overrun
        SW_OVERRUNS.fetch_add(1, Ordering::Relaxed);
    }
}

/// Application-side reader.
pub struct UartReader {
    consumer: Consumer<'static, u8, 256>,
}

impl UartReader {
    pub fn new(consumer: Consumer<'static, u8, 256>) -> Self {
        Self { consumer }
    }

    /// Read one byte; returns `None` if the buffer is empty.
    #[inline]
    pub fn read_byte(&mut self) -> Option<u8> {
        self.consumer.dequeue()
    }

    /// Read up to `buf.len()` bytes. Returns number of bytes read.
    pub fn read_bytes(&mut self, buf: &mut [u8]) -> usize {
        let mut n = 0;
        while n < buf.len() {
            match self.consumer.dequeue() {
                Some(b) => { buf[n] = b; n += 1; }
                None    => break,
            }
        }
        n
    }

    pub fn hw_overruns() -> usize { HW_OVERRUNS.load(Ordering::Relaxed) }
    pub fn sw_overruns() -> usize { SW_OVERRUNS.load(Ordering::Relaxed) }
}
```

---

### 8.3 STM32 PAC-Level Overrun Handling (stm32f4 crate)

```rust
//! Cargo.toml dependencies:
//!   stm32f4xx-hal = { version = "0.21", features = ["stm32f411"] }
//!   cortex-m-rt   = "0.7"

use stm32f4xx_hal::{
    pac::{self, interrupt, Interrupt, USART1},
    prelude::*,
};
use cortex_m::peripheral::NVIC;
use cortex_m_rt::entry;
use core::sync::atomic::{AtomicU32, Ordering};

static OVERRUN_COUNT: AtomicU32 = AtomicU32::new(0);
static RX_BYTE_COUNT: AtomicU32 = AtomicU32::new(0);

#[entry]
fn main() -> ! {
    let dp   = pac::Peripherals::take().unwrap();
    let rcc  = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

    let gpioa = dp.GPIOA.split();
    let _tx   = gpioa.pa9.into_alternate::<7>();
    let _rx   = gpioa.pa10.into_alternate::<7>();

    /* Configure USART1 at 115200 baud */
    let usart = dp.USART1;
    usart.brr.write(|w| unsafe { w.bits(0x0683) });
    usart.cr1.write(|w| w.re().enabled().ue().enabled());

    /* Enable RXNE interrupt */
    usart.cr1.modify(|_, w| w.rxneie().enabled());

    /* Enable error interrupts (ORE, NE, FE) */
    usart.cr3.modify(|_, w| w.eie().enabled());

    unsafe { NVIC::unmask(Interrupt::USART1) };

    loop {
        /* Application logic — periodically read overrun counter */
        let oe = OVERRUN_COUNT.load(Ordering::Relaxed);
        let rx = RX_BYTE_COUNT.load(Ordering::Relaxed);
        let _ = (oe, rx); // use values (e.g., log via SWO or another UART)
        cortex_m::asm::wfi();
    }
}

#[interrupt]
fn USART1() {
    // SAFETY: exclusive access because we are in the ISR
    let usart = unsafe { &(*USART1::ptr()) };

    let sr = usart.sr.read();

    // Must read DR to clear flags on STM32F4
    let dr = usart.dr.read().dr().bits() as u8;

    if sr.ore().bit_is_set() {
        OVERRUN_COUNT.fetch_add(1, Ordering::Relaxed);
        // dr was read above, ORE is now cleared — receiver resumes
        return;
    }

    if sr.rxne().bit_is_set() {
        RX_BYTE_COUNT.fetch_add(1, Ordering::Relaxed);
        // Push `dr` to ring buffer here
        let _ = dr;
    }
}
```

---

### 8.4 Typestate-Based UART with Overflow Recovery

```rust
//! Demonstrates using Rust's typestate pattern to enforce that
//! an overrun condition must be acknowledged before the UART
//! can return to normal operation.

use core::marker::PhantomData;

// ── States ─────────────────────────────────────────────────────────────
pub struct Healthy;
pub struct Faulted;

// ── UART wrapper ────────────────────────────────────────────────────────
pub struct TypestateUart<State> {
    hw_reg: u32,  // Simulated hardware register base address
    _state: PhantomData<State>,
}

impl TypestateUart<Healthy> {
    pub fn new(hw_reg: u32) -> Self {
        Self { hw_reg, _state: PhantomData }
    }

    /// Attempt to read a byte. If an overrun is detected, transition
    /// to the `Faulted` state — caller must call `recover()` before
    /// reading again.
    pub fn read(self) -> Result<(u8, Self), TypestateUart<Faulted>> {
        let (byte, overrun) = Self::hw_read(self.hw_reg);

        if overrun {
            Err(TypestateUart::<Faulted> {
                hw_reg: self.hw_reg,
                _state: PhantomData,
            })
        } else {
            Ok((byte, self))
        }
    }

    fn hw_read(reg: u32) -> (u8, bool) {
        // Stub: replace with actual MMIO access
        let _ = reg;
        (0xABu8, false)
    }
}

impl TypestateUart<Faulted> {
    /// Explicit recovery: clears the hardware error flag and returns
    /// a `Healthy` UART. The lost byte count is incremented.
    pub fn recover(self, lost_bytes: &mut u32) -> TypestateUart<Healthy> {
        *lost_bytes += 1;
        Self::hw_clear_ore(self.hw_reg);
        TypestateUart::<Healthy> {
            hw_reg: self.hw_reg,
            _state: PhantomData,
        }
    }

    fn hw_clear_ore(reg: u32) {
        // Stub: perform the actual clear sequence here
        let _ = reg;
    }
}

// ── Usage example ────────────────────────────────────────────────────────
fn application_loop(uart: TypestateUart<Healthy>) {
    let mut uart = uart;
    let mut lost: u32 = 0;
    let mut received: [u8; 64] = [0u8; 64];
    let mut idx = 0;

    loop {
        match uart.read() {
            Ok((byte, healthy)) => {
                uart = healthy;
                if idx < received.len() {
                    received[idx] = byte;
                    idx += 1;
                }
            }
            Err(faulted) => {
                // Compiler FORCES us to handle the fault before continuing.
                // We cannot accidentally ignore the error state.
                uart = faulted.recover(&mut lost);
            }
        }
    }
}
```

> **Note:** In the typestate example the compiler guarantees at the type level that a
> `Faulted` UART cannot be silently used for reads — `.read()` is only available on
> `TypestateUart<Healthy>`. This eliminates an entire class of bugs where firmware
> reads stale or garbage data after an overrun.

---

## 9. RTOS Considerations

### 9.1 Task Priority

Assign the UART receive task (or the ISR that feeds its queue) a priority **higher than
any task that consumes the data but lower than hard-real-time tasks**. A missed deadline
in a lower-priority task is acceptable; a missed byte in the receive ISR is not.

### 9.2 Deferred Processing with Message Queues

```
  ISR (high-prio) → copies byte into OS message queue
  App task (normal-prio) → reads from OS queue, does heavy processing
```

This decouples timing: the ISR is always fast, and the application task can take as long
as the queue depth allows.

### 9.3 Detecting Queue Overrun in FreeRTOS

```c
BaseType_t xHigherPriorityTaskWoken = pdFALSE;

if (xQueueSendFromISR(xRxQueue, &byte, &xHigherPriorityTaskWoken) != pdPASS) {
    /* Queue full — RTOS-level overrun */
    rtos_overrun_count++;
}

portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
```

### 9.4 Idle Hook for Buffer Draining

Register a FreeRTOS idle-hook that drains any remaining ring buffer contents when no
other task is running. This provides a last-resort drain without burning CPU on polling
in higher-priority tasks.

---

## 10. Summary

| Concept | Key Point |
|---|---|
| **What is ORE?** | Hardware sets the Overrun Error flag when a new byte arrives before the previous one was read |
| **Data loss** | The newly arrived byte is silently discarded — no partial byte, just absence |
| **Detection** | Check the ORE/status-register flag in an interrupt or after each read |
| **Clearing** | Platform-specific: read SR+DR on STM32F4; write ORECF on STM32L4/H7 |
| **Best prevention** | Hardware CTS/RTS flow control (needs pin support on both sides) |
| **Software mitigation** | Large ring buffer in the RX ISR; process quickly or use DMA |
| **DMA advantage** | Hardware fills the buffer autonomously; CPU is only involved at watermark or transfer-complete |
| **Rust advantage** | Typestate pattern enforces fault acknowledgement at compile time; atomics enable lock-free ISR-safe counters |
| **Telemetry** | Always count both hardware (ORE) and software (buffer-full) overruns for field diagnostics |
| **RTOS** | Use a high-priority ISR + OS message queue to decouple receive timing from processing timing |

### Design Rules Checklist

- [ ] Enable the ORE interrupt (`EIE` / `RXNEIE`) — do not poll in tight loops at high baud rates
- [ ] Clear the ORE flag **immediately** in the ISR; delayed clearing stalls the receiver
- [ ] Size the ring buffer for worst-case burst length, not average throughput
- [ ] Enable hardware flow control whenever the protocol and pinout allow it
- [ ] Use DMA for baud rates above ~460800 or on CPU-intensive systems
- [ ] Maintain and expose overrun counters — silent data loss is the hardest bug to diagnose
- [ ] Test overrun handling deliberately: inject bursts, disable interrupts briefly, verify recovery

---

*Document: `17_Overrun_Errors.md` — Part of the UART Programming Reference Series*