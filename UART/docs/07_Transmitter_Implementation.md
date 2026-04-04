# 07. UART Transmitter Implementation

**Core theory** — the UART frame structure from the TX perspective, ASCII timing diagrams showing the start/data/stop bit sequence, and a breakdown of every hardware component involved (shift register, bit counter, baud divider, state machine, FIFO, pin driver).

**C/C++ implementations**, four levels of complexity:
- *Bare-metal register-level* — direct MMIO writes to UART_DR/BRR/CR1 registers
- *Polling-based TX loop* — a ring buffer drained from the main loop
- *Interrupt-driven* (the production standard) — TXE ISR feeds a circular buffer, CPU is free between bytes
- *Software bit-bang* — a C++ class that serializes bytes on any GPIO using a delay loop, including parity support

**Rust implementations**, three patterns:
- *HAL/PAC TX* — using `stm32f4xx-hal` and the `embedded-hal` `Write` trait
- *RTIC interrupt-driven* — safe shared TX queue managed by the RTIC framework, eliminating race conditions at the type level
- *Bit-bang* — a generic `SoftUartTx<PIN, DELAY>` struct that works with any `embedded-hal`-compatible platform

**Advanced topics** — hardware FIFO-aware fill loops, software double buffering, and a table of TX-side error conditions with handling strategies (including the `TC` flag for detecting when the shift register is truly empty).

> Building UART transmit logic with shift registers and timing control

---

## Table of Contents

1. [Overview](#overview)
2. [UART Frame Structure (TX Perspective)](#uart-frame-structure-tx-perspective)
3. [Core Components of a UART Transmitter](#core-components-of-a-uart-transmitter)
4. [Transmitter State Machine](#transmitter-state-machine)
5. [Shift Register Mechanics](#shift-register-mechanics)
6. [Baud Rate Timing Control](#baud-rate-timing-control)
7. [Implementation in C/C++](#implementation-in-cc)
   - [Bare-Metal Register-Level (C)](#bare-metal-register-level-c)
   - [Polling-Based TX Loop (C)](#polling-based-tx-loop-c)
   - [Interrupt-Driven Transmitter (C)](#interrupt-driven-transmitter-c)
   - [Software Bit-Bang UART TX (C++)](#software-bit-bang-uart-tx-c)
8. [Implementation in Rust](#implementation-in-rust)
   - [Bare-Metal PAC/HAL TX (Rust)](#bare-metal-pachalt-tx-rust)
   - [Interrupt-Driven TX with RTIC (Rust)](#interrupt-driven-tx-with-rtic-rust)
   - [Bit-Bang Software UART TX (Rust)](#bit-bang-software-uart-tx-rust)
9. [Double Buffering and TX FIFOs](#double-buffering-and-tx-fifos)
10. [Error Handling in Transmission](#error-handling-in-transmission)
11. [Summary](#summary)

---

## Overview

The **UART transmitter** is responsible for converting parallel data bytes into a serialized, timed bit stream on the TX line. Unlike the receiver — which must synchronize itself to an incoming signal — the transmitter owns the clock and therefore drives timing precisely. It must:

- Hold the TX line **high (idle)** when no data is being sent.
- Signal the **start of a frame** with a low Start bit.
- Clock out each **data bit** LSB-first at the correct baud rate.
- Optionally append a **parity bit**.
- Return the line **high** for one or two Stop bits.

Every one of these steps is coordinated by a shift register working in concert with a baud-rate timer/counter.

---

## UART Frame Structure (TX Perspective)

```
Idle   Start  D0   D1   D2   D3   D4   D5   D6   D7   Parity  Stop  Idle
 ___    ___________________________________________         ______________
|   |  |   |   |   |   |   |   |   |   |   |   |        |              |
| 1 |  | 0 | b0| b1| b2| b3| b4| b5| b6| b7| P |        |      1       |
|___|  |___|___|___|___|___|___|___|___|___|___|________|              |
       ^                                               ^
    START BIT                                      STOP BIT(S)
    (always 0)                                    (always 1)
```

The line is **active-low for start**, **active-high for stop and idle**.  
Each bit cell has a duration of exactly `1 / baud_rate` seconds.

---

## Core Components of a UART Transmitter

| Component | Purpose |
|---|---|
| **Shift Register** | Holds the data byte; shifts one bit out per baud tick |
| **Bit Counter** | Tracks how many bits have been sent (0–7 for data, +1 for parity) |
| **Baud Rate Generator** | Divides the system clock to produce the correct bit period |
| **State Machine** | Sequences IDLE → START → DATA → PARITY → STOP states |
| **TX Buffer / FIFO** | Decouples the CPU from real-time TX timing |
| **TX Pin Driver** | Drives the physical output line |

---

## Transmitter State Machine

```
         ┌─────────────────────────────────────────┐
         │                                         │
         ▼                                         │
    ┌─────────┐   byte loaded    ┌──────────┐      │
    │  IDLE   │─────────────────▶│  START   │      │
    │ TX=HIGH │                  │ TX=LOW   │      │
    └─────────┘                  └──────────┘      │
         ▲                            │            │
         │                    1 bit period         │
         │                            ▼            │
         │                       ┌────────┐        │
         │         bit_count==8  │  DATA  │        │
         │◀──────────────────────│ TX=bit │◀──┐    │
         │  (no parity/stop yet) └────────┘   │    │
         │                            │       │    │
         │                     shift + count  │    │
         │                            │       │    │
         │                    bit_count < 8 ──┘    │
         │                                         │
         │                       ┌────────┐        │
         │                       │  STOP  │        │
         └───────────────────────│ TX=HIGH│────────┘
                  stop done      └────────┘
```

---

## Shift Register Mechanics

The transmit shift register loads the byte to transmit and right-shifts one bit per baud clock. The LSB is placed on the TX line before each shift.

### Conceptual Operation

```
Initial load:  shift_reg = 0xA5 = 1010_0101b

Step 0 (Start):  TX = 0  (Start bit)
Step 1 (D0):     TX = shift_reg & 0x01 = 1  ;  shift_reg >>= 1  → 0101_0010
Step 2 (D1):     TX = shift_reg & 0x01 = 0  ;  shift_reg >>= 1  → 0010_1001
Step 3 (D2):     TX = shift_reg & 0x01 = 1  ;  shift_reg >>= 1  → 0001_0100
...
Step 8 (D7):     TX = shift_reg & 0x01 = 1  ;  shift_reg >>= 1  → 0000_0000
Step 9 (Stop):   TX = 1  (Stop bit)
```

---

## Baud Rate Timing Control

The baud rate generator divides the system clock:

```
baud_divider = system_clock_hz / baud_rate

Example: 16 MHz clock, 115200 baud
  baud_divider = 16,000,000 / 115,200 ≈ 139 ticks per bit
```

A hardware timer (or a software counter) counts to `baud_divider` and then fires a tick that advances the transmitter state machine by one step.

Many UARTs use a **16× oversampling** approach internally, but the transmitter only needs to act once per full bit period.

---

## Implementation in C/C++

### Bare-Metal Register-Level (C)

This example targets a generic ARM Cortex-M microcontroller with MMIO-mapped UART registers (similar to STM32 USART).

```c
#include <stdint.h>
#include <stdbool.h>

/* ── UART Register Map (example: STM32-style USART) ─────────────────── */
#define UART_BASE       0x40011000UL

#define UART_SR         (*(volatile uint32_t *)(UART_BASE + 0x00))  // Status
#define UART_DR         (*(volatile uint32_t *)(UART_BASE + 0x04))  // Data
#define UART_BRR        (*(volatile uint32_t *)(UART_BASE + 0x08))  // Baud Rate
#define UART_CR1        (*(volatile uint32_t *)(UART_BASE + 0x0C))  // Control 1

/* Status Register bits */
#define UART_SR_TXE     (1U << 7)   // TX Data Register Empty (ready for new byte)
#define UART_SR_TC      (1U << 6)   // Transmission Complete

/* Control Register bits */
#define UART_CR1_UE     (1U << 13)  // UART Enable
#define UART_CR1_TE     (1U << 3)   // Transmitter Enable
#define UART_CR1_TXEIE  (1U << 7)   // TXE Interrupt Enable

#define SYS_CLOCK_HZ    16000000UL
#define BAUD_RATE       115200UL

/* ── Initialization ──────────────────────────────────────────────────── */
void uart_tx_init(void)
{
    /* Set baud rate divisor */
    UART_BRR = (uint32_t)(SYS_CLOCK_HZ / BAUD_RATE);

    /* Enable UART and Transmitter */
    UART_CR1 |= UART_CR1_UE | UART_CR1_TE;
}

/* ── Blocking single-byte transmit ──────────────────────────────────── */
void uart_tx_byte(uint8_t byte)
{
    /* Wait until the TX data register is empty */
    while (!(UART_SR & UART_SR_TXE))
        ;

    UART_DR = byte;

    /* Optionally wait for transmission to fully complete */
    while (!(UART_SR & UART_SR_TC))
        ;
}

/* ── Blocking string transmit ────────────────────────────────────────── */
void uart_tx_string(const char *str)
{
    while (*str)
        uart_tx_byte((uint8_t)*str++);
}
```

---

### Polling-Based TX Loop (C)

A simple polling loop is suitable for small embedded systems where blocking is acceptable.

```c
#include <stdint.h>
#include <string.h>

#define TX_BUF_SIZE  256

static uint8_t  tx_buffer[TX_BUF_SIZE];
static uint16_t tx_head = 0;
static uint16_t tx_tail = 0;
static uint16_t tx_count = 0;

/* Enqueue a byte for transmission */
bool uart_enqueue(uint8_t byte)
{
    if (tx_count >= TX_BUF_SIZE)
        return false;                    /* Buffer full */

    tx_buffer[tx_head] = byte;
    tx_head = (tx_head + 1) % TX_BUF_SIZE;
    tx_count++;
    return true;
}

/* Must be called repeatedly from the main loop */
void uart_tx_service(void)
{
    if (tx_count == 0)
        return;

    if (!(UART_SR & UART_SR_TXE))
        return;                          /* Hardware not ready yet */

    uint8_t byte = tx_buffer[tx_tail];
    tx_tail = (tx_tail + 1) % TX_BUF_SIZE;
    tx_count--;

    UART_DR = byte;
}

/* ── Usage ───────────────────────────────────────────────────────────── */
int main(void)
{
    uart_tx_init();

    const char *msg = "Hello, UART!\r\n";
    for (size_t i = 0; i < strlen(msg); i++)
        uart_enqueue((uint8_t)msg[i]);

    while (1) {
        uart_tx_service();
        /* ... other application tasks ... */
    }
}
```

---

### Interrupt-Driven Transmitter (C)

The interrupt-driven approach is the most common production pattern. The TXE (TX Empty) interrupt fires when the hardware shift register is ready for the next byte, keeping the CPU free between bytes.

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define TX_BUF_SIZE  512

/* Circular ring buffer */
typedef struct {
    uint8_t  data[TX_BUF_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} RingBuffer;

static RingBuffer tx_ring = {0};

/* ── Ring buffer helpers ─────────────────────────────────────────────── */
static inline bool ring_push(RingBuffer *rb, uint8_t byte)
{
    if (rb->count >= TX_BUF_SIZE) return false;
    rb->data[rb->head] = byte;
    rb->head = (rb->head + 1) % TX_BUF_SIZE;
    rb->count++;
    return true;
}

static inline bool ring_pop(RingBuffer *rb, uint8_t *byte)
{
    if (rb->count == 0) return false;
    *byte = rb->data[rb->tail];
    rb->tail = (rb->tail + 1) % TX_BUF_SIZE;
    rb->count--;
    return true;
}

/* ── Public API: write bytes into the TX buffer ──────────────────────── */
bool uart_write(const uint8_t *data, size_t len)
{
    /* Disable TXE interrupt briefly to avoid race condition */
    UART_CR1 &= ~UART_CR1_TXEIE;

    for (size_t i = 0; i < len; i++) {
        if (!ring_push(&tx_ring, data[i])) {
            UART_CR1 |= UART_CR1_TXEIE;
            return false;               /* Buffer overflow */
        }
    }

    /* Kick off transmission if not already running */
    UART_CR1 |= UART_CR1_TXEIE;
    return true;
}

/* ── UART TXE Interrupt Service Routine ─────────────────────────────── */
void USART1_IRQHandler(void)
{
    if (UART_SR & UART_SR_TXE) {
        uint8_t byte;
        if (ring_pop(&tx_ring, &byte)) {
            UART_DR = byte;              /* Load next byte into hardware */
        } else {
            /* Nothing left to send — disable TXE interrupt */
            UART_CR1 &= ~UART_CR1_TXEIE;
        }
    }
}

/* ── Usage ───────────────────────────────────────────────────────────── */
int main(void)
{
    uart_tx_init();

    const char *hello = "Interrupt-driven UART TX\r\n";
    uart_write((const uint8_t *)hello, strlen(hello));

    while (1) {
        /* CPU is free; TX runs in background */
    }
}
```

---

### Software Bit-Bang UART TX (C++)

When no hardware UART is available, a GPIO pin can bit-bang the UART protocol using a precise delay loop tied to the baud rate.

```cpp
#include <cstdint>

// Platform-specific delay in microseconds (implement for your MCU)
extern void delay_us(uint32_t us);

// Platform-specific GPIO write (implement for your MCU)
extern void gpio_write(bool level);

class BitBangUartTx {
public:
    explicit BitBangUartTx(uint32_t baud_rate)
        : bit_period_us_(1'000'000UL / baud_rate)
    {
        gpio_write(true);   // Idle high
    }

    void send_byte(uint8_t byte) {
        // Start bit
        gpio_write(false);
        delay_us(bit_period_us_);

        // Data bits, LSB first
        for (int i = 0; i < 8; ++i) {
            gpio_write((byte >> i) & 0x01);
            delay_us(bit_period_us_);
        }

        // Stop bit
        gpio_write(true);
        delay_us(bit_period_us_);
    }

    void send_string(const char *str) {
        while (*str)
            send_byte(static_cast<uint8_t>(*str++));
    }

    void send_byte_with_parity(uint8_t byte, bool even_parity) {
        // Start bit
        gpio_write(false);
        delay_us(bit_period_us_);

        // Data bits, LSB first — accumulate parity
        uint8_t parity_bit = even_parity ? 0 : 1;  // Even=0, Odd=1 seed
        for (int i = 0; i < 8; ++i) {
            bool bit = (byte >> i) & 0x01;
            gpio_write(bit);
            parity_bit ^= static_cast<uint8_t>(bit);
            delay_us(bit_period_us_);
        }

        // Parity bit
        gpio_write(parity_bit & 0x01);
        delay_us(bit_period_us_);

        // Stop bit
        gpio_write(true);
        delay_us(bit_period_us_);
    }

private:
    uint32_t bit_period_us_;
};

/* ── Usage ───────────────────────────────────────────────────────────── */
int main()
{
    BitBangUartTx uart(9600);
    uart.send_string("Bit-bang UART TX @ 9600 baud\r\n");
    uart.send_byte_with_parity(0xA5, true);  // Even parity
    return 0;
}
```

---

## Implementation in Rust

### Bare-Metal PAC/HAL TX (Rust)

Using the `embedded-hal` trait and a PAC (Peripheral Access Crate) for an STM32-family microcontroller.

```rust
//! Cargo.toml dependencies:
//! stm32f4xx-hal = { version = "0.20", features = ["stm32f411"] }
//! embedded-hal = "1.0"

#![no_std]
#![no_main]

use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{Config, Serial},
};
use core::fmt::Write;

#[cortex_m_rt::entry]
fn main() -> ! {
    // Take ownership of peripherals
    let dp = pac::Peripherals::take().unwrap();

    // Configure clocks
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(16.MHz()).freeze();

    // Configure GPIO pins for USART1 (PA9 = TX, PA10 = RX)
    let gpioa = dp.GPIOA.split();
    let tx_pin = gpioa.pa9.into_alternate::<7>();
    let rx_pin = gpioa.pa10.into_alternate::<7>();

    // Configure UART at 115200 baud, 8N1
    let config = Config::default()
        .baudrate(115_200.bps())
        .wordlength_8()
        .parity_none()
        .stopbits(stm32f4xx_hal::serial::StopBits::STOP1);

    let mut serial = Serial::new(dp.USART1, (tx_pin, rx_pin), config, &clocks).unwrap();

    // Transmit using the Write trait (fmt::Write)
    writeln!(serial, "UART TX via embedded-hal on STM32\r").unwrap();

    // Transmit individual bytes
    let data: &[u8] = b"Hello from Rust!\r\n";
    for &byte in data {
        // Block until the TX register is empty, then send
        nb::block!(serial.write(byte)).unwrap();
    }

    loop {
        cortex_m::asm::wfi();  // Wait for interrupt
    }
}
```

---

### Interrupt-Driven TX with RTIC (Rust)

RTIC (Real-Time Interrupt-driven Concurrency) provides safe, zero-cost interrupt management. The TX buffer is a shared resource protected by the RTIC framework.

```rust
//! Cargo.toml:
//! rtic = { version = "2.0", features = ["thumbv7-backend"] }
//! heapless = "0.8"

#![no_std]
#![no_main]

use heapless::spsc::Queue;
use stm32f4xx_hal::{pac, prelude::*, serial};

#[rtic::app(device = stm32f4xx_hal::pac, peripherals = true)]
mod app {
    use super::*;

    const TX_QUEUE_SIZE: usize = 256;

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        tx: serial::Tx<pac::USART1>,
        tx_queue: Queue<u8, TX_QUEUE_SIZE>,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        let dp = ctx.device;
        let rcc = dp.RCC.constrain();
        let clocks = rcc.cfgr.sysclk(16.MHz()).freeze();

        let gpioa = dp.GPIOA.split();
        let tx_pin = gpioa.pa9.into_alternate::<7>();
        let rx_pin = gpioa.pa10.into_alternate::<7>();

        let mut serial = serial::Serial::new(
            dp.USART1,
            (tx_pin, rx_pin),
            serial::Config::default().baudrate(115_200.bps()),
            &clocks,
        )
        .unwrap();

        // Enable TXE interrupt
        serial.listen(serial::Event::TxEmpty);

        let (tx, _rx) = serial.split();

        // Pre-load a message into the TX queue
        let mut q: Queue<u8, TX_QUEUE_SIZE> = Queue::new();
        for &b in b"RTIC UART TX Ready\r\n" {
            q.enqueue(b).ok();
        }

        (Shared {}, Local { tx, tx_queue: q })
    }

    /// Software task: enqueue bytes for transmission
    #[task(local = [tx_queue], priority = 1)]
    async fn enqueue_data(ctx: enqueue_data::Context, data: &'static [u8]) {
        for &byte in data {
            while ctx.local.tx_queue.enqueue(byte).is_err() {
                // Queue full — yield and retry
                rtic::export::wfi();
            }
        }
    }

    /// Hardware interrupt: fires when USART1 TX register is empty
    #[task(binds = USART1, local = [tx, tx_queue], priority = 2)]
    fn usart1_isr(ctx: usart1_isr::Context) {
        match ctx.local.tx_queue.dequeue() {
            Some(byte) => {
                // Send next byte; hardware will re-trigger interrupt when done
                nb::block!(ctx.local.tx.write(byte)).ok();
            }
            None => {
                // Nothing left — disable TXE interrupt to stop re-triggering
                ctx.local.tx.unlisten(serial::Event::TxEmpty);
            }
        }
    }
}
```

---

### Bit-Bang Software UART TX (Rust)

A `no_std` software UART transmitter using `embedded-hal` output pin and delay traits.

```rust
#![no_std]

use embedded_hal::delay::DelayNs;
use embedded_hal::digital::OutputPin;

/// Software UART transmitter (bit-bang on any GPIO)
pub struct SoftUartTx<PIN, DELAY> {
    pin: PIN,
    delay: DELAY,
    bit_period_ns: u32,
}

impl<PIN, DELAY> SoftUartTx<PIN, DELAY>
where
    PIN: OutputPin,
    DELAY: DelayNs,
{
    /// Create a new software UART TX.
    /// `baud_rate`: bits per second (e.g. 9600, 115200)
    pub fn new(mut pin: PIN, delay: DELAY, baud_rate: u32) -> Self {
        // Idle state: TX line high
        pin.set_high().ok();
        let bit_period_ns = 1_000_000_000u32 / baud_rate;
        Self { pin, delay, bit_period_ns }
    }

    /// Transmit a single byte (8N1 format)
    pub fn send_byte(&mut self, byte: u8) {
        // Start bit: drive low
        self.pin.set_low().ok();
        self.delay.delay_ns(self.bit_period_ns);

        // Data bits: LSB first
        for bit_idx in 0..8u8 {
            if (byte >> bit_idx) & 0x01 != 0 {
                self.pin.set_high().ok();
            } else {
                self.pin.set_low().ok();
            }
            self.delay.delay_ns(self.bit_period_ns);
        }

        // Stop bit: drive high (idle)
        self.pin.set_high().ok();
        self.delay.delay_ns(self.bit_period_ns);
    }

    /// Transmit a byte slice
    pub fn send_bytes(&mut self, data: &[u8]) {
        for &byte in data {
            self.send_byte(byte);
        }
    }

    /// Transmit with even parity (9-bit frame: 8 data + 1 parity)
    pub fn send_byte_even_parity(&mut self, byte: u8) {
        // Start bit
        self.pin.set_low().ok();
        self.delay.delay_ns(self.bit_period_ns);

        // Data bits + accumulate parity
        let mut parity: u8 = 0;
        for bit_idx in 0..8u8 {
            let bit = (byte >> bit_idx) & 0x01;
            parity ^= bit;
            if bit != 0 {
                self.pin.set_high().ok();
            } else {
                self.pin.set_low().ok();
            }
            self.delay.delay_ns(self.bit_period_ns);
        }

        // Even parity bit (parity == 0 means even number of 1s already)
        if parity != 0 {
            self.pin.set_high().ok();   // Set parity to make count even
        } else {
            self.pin.set_low().ok();
        }
        self.delay.delay_ns(self.bit_period_ns);

        // Stop bit
        self.pin.set_high().ok();
        self.delay.delay_ns(self.bit_period_ns);
    }
}

/* ── Usage (with a concrete HAL) ─────────────────────────────────────── */
// fn main_example(pin: impl OutputPin, delay: impl DelayNs) {
//     let mut uart = SoftUartTx::new(pin, delay, 9_600);
//     uart.send_bytes(b"Hello from soft-UART!\r\n");
// }
```

---

## Double Buffering and TX FIFOs

Many UART peripherals include a **hardware FIFO** (typically 16–64 bytes deep). The CPU fills the FIFO and the hardware drains it bit-by-bit without further CPU involvement.

### FIFO-Aware TX Fill Loop (C)

```c
#define UART_FIFO_DEPTH  16
#define UART_SR_TXFF    (1U << 5)   // TX FIFO Full flag

/* Fill the FIFO as much as possible without blocking */
void uart_tx_fill_fifo(const uint8_t *data, size_t len, size_t *sent)
{
    *sent = 0;
    while (*sent < len && *sent < UART_FIFO_DEPTH) {
        if (UART_SR & UART_SR_TXFF) break;  // FIFO is full, stop
        UART_DR = data[(*sent)++];
    }
}
```

### Software Double Buffer (C)

```c
/* Two buffers: one being transmitted, one being filled by the application */
#define BUF_SIZE 128

static uint8_t buf_a[BUF_SIZE];
static uint8_t buf_b[BUF_SIZE];
static uint8_t *active_tx_buf   = buf_a;   // Currently transmitting
static uint8_t *fill_buf        = buf_b;   // Currently filling
static size_t   active_tx_len   = 0;
static size_t   active_tx_pos   = 0;
static bool     swap_requested  = false;

void uart_tx_swap_buffers(size_t fill_len)
{
    /* Request a buffer swap after the active one is exhausted */
    if (!swap_requested) {
        uint8_t *tmp  = active_tx_buf;
        active_tx_buf = fill_buf;
        fill_buf      = tmp;
        active_tx_len = fill_len;
        active_tx_pos = 0;
        swap_requested = true;
    }
}

/* Called from TXE ISR */
void uart_tx_isr_handler(void)
{
    if (active_tx_pos < active_tx_len) {
        UART_DR = active_tx_buf[active_tx_pos++];
    } else if (swap_requested) {
        swap_requested = false;
        /* Transmit continues from the freshly-swapped buffer on next ISR */
    } else {
        UART_CR1 &= ~UART_CR1_TXEIE;  /* No more data — idle */
    }
}
```

---

## Error Handling in Transmission

Unlike reception, the transmitter generates fewer hard errors, but several conditions require handling:

| Condition | Cause | Handling |
|---|---|---|
| **TX Overrun** | CPU writes DR before TXE flag is set | Check TXE before writing; use ISR |
| **TX Buffer Full** | Ring buffer / FIFO overflow | Return error code; apply back-pressure |
| **Framing Error (remote)** | Baud mismatch at receiver | Verify baud divider calculation |
| **Break Condition** | TX held low > one full frame | Can be intentional (LIN bus); detect at remote end |

### Detecting TX Completion (C)

```c
/* Wait for the very last bit to leave the shift register.
   TC (Transmission Complete) flag clears when DR is written,
   sets again once the shift register empties. */
void uart_flush(void)
{
    /* Wait until TC is set — shift register is truly empty */
    while (!(UART_SR & UART_SR_TC))
        ;
}

/* In Rust (embedded-hal nb pattern): */
// pub fn flush(&mut self) -> nb::Result<(), Error> {
//     if self.usart.sr.read().tc().bit_is_set() {
//         Ok(())
//     } else {
//         Err(nb::Error::WouldBlock)
//     }
// }
```

---

## Summary

The UART transmitter converts a parallel byte into a precisely-timed serial bit stream. Its core building blocks are:

- **Shift Register**: Serializes the data byte one bit per baud-rate tick, LSB first.
- **State Machine**: Sequences the line through IDLE → START → DATA bits → (PARITY) → STOP → IDLE.
- **Baud Rate Timer**: Divides the system clock to produce a fixed bit-period. Getting this right is critical — any deviation accumulates and causes the remote receiver to misread bits.
- **TX Buffer / FIFO**: Decouples the application layer from real-time bit-level timing. Ring buffers fed by TXE interrupts are the standard production pattern.
- **Interrupt-Driven Operation**: The TXE interrupt fires when the hardware data register is ready for a new byte, allowing the CPU to be fully free between bytes and enabling high-throughput, low-latency transmission without busy-waiting.

The implementation hierarchy — from polling → interrupt-driven → DMA-driven — trades simplicity for throughput and CPU efficiency. For maximum performance, a DMA engine (not covered here) can stream entire buffers to the UART without any CPU intervention once the transfer is initiated.

Both C and Rust implementations follow the same logical pattern: the language-level difference is that Rust's type system and ownership rules make it impossible to accidentally share the TX ring buffer across interrupt contexts without explicit synchronization, eliminating an entire class of race conditions at compile time.

---

*Next: [08. Receiver Implementation](08_Receiver_Implementation.md) — Synchronizing to incoming data, oversampling, and reconstructing bytes from the serial stream.*