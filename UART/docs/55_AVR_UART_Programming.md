# 55. AVR UART Programming

**Architecture & Theory** — UART frame structure, AVR USART block diagram with independent TX/RX paths and baud rate generator, all key register bits (UCSR0A/B/C, UDR0, UBRR) explained in detail.

**Baud Rate** — Both normal and double-speed formulas with an accuracy table at 16 MHz, including why 115200 baud requires U2X mode.

**C/C++ Code Examples:**
- Polling init, `uart_putchar`, `uart_getchar`, `uart_puts`, hex/decimal printers
- Full interrupt-driven driver using `USART_RX_vect` and `USART_UDRE_vect`
- Reusable power-of-2 ring buffer (`ring_buffer.h`) with fast bitmask modulo
- A clean `uart.c` / `uart.h` driver built on top of it
- `printf` / `scanf` redirection via `FDEV_SETUP_STREAM`

**Rust Examples:**
- HAL-based with `avr-hal` + `ufmt`
- Bare-metal with direct register addresses and `read_volatile`/`write_volatile`
- Interrupt-driven with `avr_device::interrupt::Mutex<RefCell<...>>` for ISR-safe shared state

**ATtiny specifics** — bit-banged software UART for ATtiny85, and the different register style of the megaTINY family (ATtiny416/817).

**Pitfalls** — Six common bugs with causes and fixes (overflow in UBRR macro, missing `sei()`, UDR0 read order, race conditions, stdio wiring, multi-USART confusion).

### Bare-metal UART on ATmega and ATtiny Microcontrollers

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [UART Fundamentals](#2-uart-fundamentals)
3. [AVR UART Hardware Architecture](#3-avr-uart-hardware-architecture)
4. [Key Registers](#4-key-registers)
5. [Baud Rate Calculation](#5-baud-rate-calculation)
6. [Basic UART Initialization in C/C++](#6-basic-uart-initialization-in-cc)
7. [Transmit and Receive in C/C++](#7-transmit-and-receive-in-cc)
8. [Interrupt-Driven UART in C/C++](#8-interrupt-driven-uart-in-cc)
9. [Ring Buffer (Circular Buffer) Implementation in C/C++](#9-ring-buffer-circular-buffer-implementation-in-cc)
10. [AVR UART in Rust](#10-avr-uart-in-rust)
11. [ATtiny UART Specifics](#11-attiny-uart-specifics)
12. [Common Pitfalls and Debugging Tips](#12-common-pitfalls-and-debugging-tips)
13. [Summary](#13-summary)

---

## 1. Introduction

**UART** (Universal Asynchronous Receiver/Transmitter) is one of the oldest and most widely used serial communication protocols in embedded systems. On AVR microcontrollers — the ATmega and ATtiny families that power platforms like Arduino, as well as countless industrial and hobbyist designs — UART is implemented in hardware as the **USART** (Universal Synchronous/Asynchronous Receiver/Transmitter) peripheral. This guide covers bare-metal UART programming: directly manipulating hardware registers without any HAL, framework, or Arduino library in between.

Bare-metal UART is essential when you need:
- **Precise timing control** not achievable via library abstractions
- **Minimal code footprint** for memory-constrained devices
- **Interrupt-driven I/O** for non-blocking communication
- **Custom framing, parity, or baud rates**
- **Full understanding** of what the hardware is doing

---

## 2. UART Fundamentals

UART communication is **asynchronous** — there is no shared clock line. Both devices must agree on a **baud rate** (bits per second) in advance. A UART frame consists of:

```
IDLE  START  D0  D1  D2  D3  D4  D5  D6  D7  [PARITY]  STOP  IDLE
HIGH   LOW   ←────────── Data bits ──────────→           HIGH
```

| Field        | Value / Options                          |
|--------------|------------------------------------------|
| Start bit    | Always 1 bit, logic LOW                  |
| Data bits    | 5, 6, 7, 8 (most common), or 9           |
| Parity bit   | None, Even, or Odd (optional)            |
| Stop bits    | 1 or 2                                   |
| Idle line    | Logic HIGH                               |
| Common rates | 9600, 19200, 38400, 57600, **115200** bps|

The most common configuration is **8N1**: 8 data bits, No parity, 1 stop bit.

---

## 3. AVR UART Hardware Architecture

Most ATmega devices (ATmega328P, ATmega2560, ATmega32U4, etc.) feature one or more **USART** peripherals. Each USART contains three independent sub-units:

```
                    ┌─────────────────────────────────────┐
                    │            AVR USART                │
                    │                                     │
   TXD pin ◄────────┤  Transmitter                        │
                    │  ┌──────────┐   ┌────────────────┐  │
                    │  │  Shift   │◄──│  UDR (TX buf)  │◄─├── CPU Write
                    │  │ Register │   └────────────────┘  │
                    │  └──────────┘                       │
                    │                                     │
  RXD pin ─────────►│  Receiver                           │
                    │  ┌──────────┐   ┌────────────────┐  │
                    │  │  Shift   │──►│  UDR (RX buf)  │──├── CPU Read
                    │  │ Register │   └────────────────┘  │
                    │  └──────────┘                       │
                    │                                     │
                    │  Baud Rate Generator                │
                    │  ┌──────────────────┐               │
                    │  │  UBRR (16-bit)   │               │
                    │  └──────────────────┘               │
                    └─────────────────────────────────────┘
```

The **transmitter** and **receiver** are independent, making UART full-duplex by default. The **baud rate generator** divides the system clock to produce the correct bit timing for both.

---

## 4. Key Registers

For **ATmega328P** (USART0), the relevant registers are:

### UDR0 — USART Data Register
Reading `UDR0` retrieves received data. Writing `UDR0` sends data.

### UCSR0A — USART Control and Status Register A

| Bit | Name  | Description                                   |
|-----|-------|-----------------------------------------------|
| 7   | RXC0  | RX Complete (1 = unread data in UDR0)         |
| 6   | TXC0  | TX Complete (1 = shift register empty)        |
| 5   | UDRE0 | Data Register Empty (1 = ready to accept TX)  |
| 4   | FE0   | Frame Error                                   |
| 3   | DOR0  | Data Overrun                                  |
| 2   | UPE0  | Parity Error                                  |
| 1   | U2X0  | Double Speed mode                             |
| 0   | MPCM0 | Multi-processor Communication Mode            |

### UCSR0B — USART Control and Status Register B

| Bit | Name   | Description                         |
|-----|--------|-------------------------------------|
| 7   | RXCIE0 | RX Complete Interrupt Enable        |
| 6   | TXCIE0 | TX Complete Interrupt Enable        |
| 5   | UDRIE0 | Data Register Empty Interrupt Enable|
| 4   | RXEN0  | Receiver Enable                     |
| 3   | TXEN0  | Transmitter Enable                  |
| 2   | UCSZ02 | Character Size bit 2                |
| 1   | RXB80  | Receive Data Bit 8 (9-bit mode)     |
| 0   | TXB80  | Transmit Data Bit 8 (9-bit mode)    |

### UCSR0C — USART Control and Status Register C

| Bit | Name         | Description                                  |
|-----|--------------|----------------------------------------------|
| 7:6 | UMSEL0[1:0]  | Mode: 00=Async, 01=Sync, 10=reserved, 11=SPI |
| 5:4 | UPM0[1:0]   | Parity: 00=None, 10=Even, 11=Odd              |
| 3   | USBS0        | Stop bits: 0=1 bit, 1=2 bits                 |
| 2:1 | UCSZ0[1:0]  | Character size (with UCSZ02)                  |
| 0   | UCPOL0       | Clock polarity (synchronous mode only)       |

### UBRR0H / UBRR0L — Baud Rate Registers
Combined 12-bit register holding the baud rate divisor.

---

## 5. Baud Rate Calculation

The baud rate register value is calculated from the CPU clock frequency `F_CPU` and the desired baud rate:

**Normal Speed (U2X = 0):**
```
UBRR = (F_CPU / (16 × BAUD)) - 1
```

**Double Speed (U2X = 1):**
```
UBRR = (F_CPU / (8 × BAUD)) - 1
```

**Error calculation:**

The actual baud rate differs slightly from the desired value due to integer rounding. Always check the error percentage:

```
Actual Baud = F_CPU / (16 × (UBRR + 1))
Error %     = |Actual - Desired| / Desired × 100
```

Errors below **2%** are generally acceptable. Above that, communication becomes unreliable.

**Common baud rate / UBRR table at 16 MHz:**

| Baud Rate | UBRR  | Actual Baud | Error   |
|-----------|-------|-------------|---------|
| 9600      | 103   | 9615        | 0.16%   |
| 19200     | 51    | 19231       | 0.16%   |
| 38400     | 25    | 38462       | 0.16%   |
| 57600     | 16    | 58824       | 2.12% ⚠ |
| 115200    | 8     | 111111      | 3.54% ✗ |
| 115200    | 8 + U2X=1 | 115385 | 0.16% ✓  |

> **Note:** 115200 baud at 16 MHz requires Double Speed mode (`U2X0 = 1`).

---

## 6. Basic UART Initialization in C/C++

```c
/*
 * AVR bare-metal UART initialization for ATmega328P
 * Target: ATmega328P @ 16 MHz
 * Toolchain: avr-gcc
 */

#include <avr/io.h>
#include <stdint.h>

#define F_CPU       16000000UL
#define BAUD        9600UL

/* Baud rate register value — cast avoids overflow on 8-bit arithmetic */
#define UBRR_VALUE  ((uint16_t)((F_CPU / (16UL * BAUD)) - 1))

void uart_init(void)
{
    /* Set baud rate — write high byte FIRST */
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);

    /* Enable transmitter and receiver */
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);

    /*
     * Set frame format:
     *   UMSEL01:UMSEL00 = 00  → Asynchronous UART
     *   UPM01:UPM00     = 00  → No parity
     *   USBS0           = 0   → 1 stop bit
     *   UCSZ01:UCSZ00   = 11  → 8-bit character size
     */
    UCSR0C = (0 << UMSEL01) | (0 << UMSEL00) |
             (0 << UPM01)   | (0 << UPM00)   |
             (0 << USBS0)   |
             (1 << UCSZ01)  | (1 << UCSZ00);
}
```

### Initialization for 115200 Baud at 16 MHz (Double Speed Mode)

```c
#define BAUD        115200UL
#define UBRR_VALUE  ((uint16_t)((F_CPU / (8UL * BAUD)) - 1))   /* U2X mode */

void uart_init_115200(void)
{
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);

    /* Enable double speed mode */
    UCSR0A = (1 << U2X0);

    UCSR0B = (1 << RXEN0) | (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);   /* 8N1 */
}
```

---

## 7. Transmit and Receive in C/C++

### Polling-Based (Blocking) I/O

```c
#include <avr/io.h>
#include <stdint.h>
#include <stddef.h>   /* size_t */

/*
 * uart_putchar() — Transmit a single byte (blocking)
 *
 * Waits until the UDR0 transmit buffer is empty (UDRE0 flag),
 * then loads the byte into UDR0.
 */
void uart_putchar(uint8_t data)
{
    /* Wait until Data Register Empty flag is set */
    while (!(UCSR0A & (1 << UDRE0)))
        ;   /* spin */

    UDR0 = data;
}

/*
 * uart_getchar() — Receive a single byte (blocking)
 *
 * Waits until the RX Complete flag is set, then returns the byte.
 * Call uart_has_data() first if you need a non-blocking check.
 */
uint8_t uart_getchar(void)
{
    /* Wait for data to arrive */
    while (!(UCSR0A & (1 << RXC0)))
        ;

    /* Check for frame/parity/overrun errors before reading */
    if (UCSR0A & ((1 << FE0) | (1 << DOR0) | (1 << UPE0))) {
        /* Error: still must read UDR0 to clear the flag */
        (void)UDR0;
        return 0xFF;   /* sentinel error value */
    }

    return UDR0;
}

/*
 * uart_has_data() — Non-blocking check for received data
 */
uint8_t uart_has_data(void)
{
    return (UCSR0A & (1 << RXC0)) ? 1 : 0;
}

/*
 * uart_puts() — Transmit a null-terminated string
 */
void uart_puts(const char *str)
{
    while (*str)
        uart_putchar((uint8_t)*str++);
}

/*
 * uart_write() — Transmit a buffer of bytes
 */
void uart_write(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        uart_putchar(buf[i]);
}

/*
 * uart_print_hex() — Print a byte as two hex digits
 */
void uart_print_hex(uint8_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putchar(hex[val >> 4]);
    uart_putchar(hex[val & 0x0F]);
}

/*
 * uart_print_uint16() — Print an unsigned 16-bit integer as decimal
 */
void uart_print_uint16(uint16_t val)
{
    char buf[6];
    uint8_t i = 0;

    if (val == 0) {
        uart_putchar('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    /* Digits are in reverse order */
    while (i > 0)
        uart_putchar(buf[--i]);
}
```

### Usage Example

```c
int main(void)
{
    uart_init();

    uart_puts("AVR UART Ready\r\n");

    while (1) {
        /* Echo received bytes back, uppercase */
        if (uart_has_data()) {
            uint8_t ch = uart_getchar();

            /* Simple uppercase conversion */
            if (ch >= 'a' && ch <= 'z')
                ch -= 32;

            uart_putchar(ch);
        }
    }
}
```

---

## 8. Interrupt-Driven UART in C/C++

Polling ties the CPU to UART operations. For real embedded applications, interrupt-driven I/O is preferred: the CPU is free to do other work and the ISR handles data when it arrives or when the transmitter is ready.

AVR USART provides three interrupt vectors:

| Vector           | Condition                            |
|------------------|--------------------------------------|
| `USART_RX_vect`  | Byte fully received (RXC0 = 1)       |
| `USART_UDRE_vect`| Transmit buffer empty (UDRE0 = 1)    |
| `USART_TX_vect`  | Transmit shift register empty        |

The most useful pair is **RX Complete** for receiving and **UDRE** for transmitting from a buffer.

```c
/*
 * Interrupt-Driven UART with simple ping-pong buffers
 * ATmega328P @ 16 MHz, 9600 baud
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define F_CPU       16000000UL
#define BAUD        9600UL
#define UBRR_VALUE  ((uint16_t)((F_CPU / (16UL * BAUD)) - 1))

/* Receive buffer */
#define RX_BUF_SIZE 64
static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;
static volatile uint8_t rx_overflow = 0;

/* Transmit buffer */
#define TX_BUF_SIZE 64
static volatile uint8_t tx_buf[TX_BUF_SIZE];
static volatile uint8_t tx_head = 0;
static volatile uint8_t tx_tail = 0;

/* ── ISRs ─────────────────────────────────────────────────────────── */

/*
 * RX Complete ISR — fires when a new byte lands in UDR0
 */
ISR(USART_RX_vect)
{
    uint8_t status = UCSR0A;
    uint8_t data   = UDR0;     /* Must read UDR0 to clear interrupt */

    if (status & ((1 << FE0) | (1 << DOR0) | (1 << UPE0)))
        return;                /* Discard error bytes */

    uint8_t next = (rx_head + 1) % RX_BUF_SIZE;
    if (next == rx_tail) {
        rx_overflow = 1;       /* Buffer full — drop byte */
        return;
    }
    rx_buf[rx_head] = data;
    rx_head = next;
}

/*
 * Data Register Empty ISR — fires when UDR0 can accept another byte
 * Loads the next byte from tx_buf, or disables itself when done.
 */
ISR(USART_UDRE_vect)
{
    if (tx_tail != tx_head) {
        UDR0 = tx_buf[tx_tail];
        tx_tail = (tx_tail + 1) % TX_BUF_SIZE;
    } else {
        /* Nothing left to send — disable UDRE interrupt */
        UCSR0B &= ~(1 << UDRIE0);
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

void uart_init(void)
{
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);

    UCSR0B = (1 << RXCIE0) |   /* RX Complete Interrupt Enable */
             (1 << RXEN0)  |   /* Receiver Enable */
             (1 << TXEN0);     /* Transmitter Enable */
                               /* UDRIE0 enabled only when data to send */

    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);   /* 8N1 */
}

/*
 * uart_getchar_nb() — Non-blocking read from RX buffer
 * Returns 1 and sets *out if data available, else returns 0.
 */
uint8_t uart_getchar_nb(uint8_t *out)
{
    if (rx_head == rx_tail)
        return 0;

    *out   = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return 1;
}

/*
 * uart_putchar() — Queue a byte for TX (non-blocking if buffer has space)
 * Returns 1 on success, 0 if buffer is full.
 */
uint8_t uart_putchar(uint8_t data)
{
    uint8_t next = (tx_head + 1) % TX_BUF_SIZE;
    if (next == tx_tail)
        return 0;   /* Buffer full */

    tx_buf[tx_head] = data;
    tx_head = next;

    /* Enable UDRE interrupt to start (or continue) transmitting */
    UCSR0B |= (1 << UDRIE0);
    return 1;
}

/*
 * uart_puts() — Queue a null-terminated string
 */
void uart_puts(const char *str)
{
    while (*str)
        uart_putchar((uint8_t)*str++);
}

/*
 * uart_rx_available() — How many bytes are waiting in the RX buffer
 */
uint8_t uart_rx_available(void)
{
    return (uint8_t)((rx_head - rx_tail + RX_BUF_SIZE) % RX_BUF_SIZE);
}

/*
 * uart_rx_overflow() — Check and clear overflow flag
 */
uint8_t uart_rx_overflow(void)
{
    uint8_t ov = rx_overflow;
    rx_overflow = 0;
    return ov;
}

/* ── main ─────────────────────────────────────────────────────────── */

int main(void)
{
    uart_init();
    sei();   /* Enable global interrupts */

    uart_puts("IRQ-driven UART ready\r\n");

    while (1) {
        uint8_t ch;
        if (uart_getchar_nb(&ch)) {
            /* Echo with prefix */
            uart_puts("RX: ");
            uart_putchar(ch);
            uart_puts("\r\n");
        }

        if (uart_rx_overflow())
            uart_puts("!! RX OVERFLOW\r\n");

        /* CPU is free to do other work here */
    }
}
```

---

## 9. Ring Buffer (Circular Buffer) Implementation in C/C++

A well-structured ring buffer is fundamental for robust interrupt-driven UART. Here is a clean, reusable implementation with a power-of-two size constraint to allow fast modulo via bitmask:

```c
/*
 * ring_buffer.h — Generic power-of-2 ring buffer for UART
 *
 * SIZE must be a power of 2 (8, 16, 32, 64, 128, 256).
 * The mask trick (& (SIZE-1)) replaces modulo with a single AND.
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#define RING_BUF_SIZE  64u                       /* Must be power of 2 */
#define RING_BUF_MASK  (RING_BUF_SIZE - 1u)

typedef struct {
    uint8_t  data[RING_BUF_SIZE];
    volatile uint8_t head;   /* written by producer */
    volatile uint8_t tail;   /* read  by consumer   */
} ring_buf_t;

/* Returns number of bytes available to read */
static inline uint8_t ring_buf_count(const ring_buf_t *rb)
{
    return (uint8_t)((rb->head - rb->tail) & RING_BUF_MASK);
}

/* Returns number of bytes free to write */
static inline uint8_t ring_buf_free(const ring_buf_t *rb)
{
    return (uint8_t)((RING_BUF_SIZE - 1u) - ring_buf_count(rb));
}

/* Returns 1 if empty */
static inline uint8_t ring_buf_empty(const ring_buf_t *rb)
{
    return rb->head == rb->tail;
}

/* Returns 1 if full */
static inline uint8_t ring_buf_full(const ring_buf_t *rb)
{
    return ring_buf_count(rb) == (RING_BUF_SIZE - 1u);
}

/*
 * ring_buf_put() — Write one byte; returns 0 if full (data lost).
 * Call from producer context (e.g. main or RX ISR).
 */
static inline uint8_t ring_buf_put(ring_buf_t *rb, uint8_t byte)
{
    if (ring_buf_full(rb))
        return 0;
    rb->data[rb->head & RING_BUF_MASK] = byte;
    rb->head++;
    return 1;
}

/*
 * ring_buf_get() — Read one byte; returns 0 if empty.
 * Call from consumer context (e.g. main or TX ISR).
 */
static inline uint8_t ring_buf_get(ring_buf_t *rb, uint8_t *out)
{
    if (ring_buf_empty(rb))
        return 0;
    *out = rb->data[rb->tail & RING_BUF_MASK];
    rb->tail++;
    return 1;
}

#endif /* RING_BUFFER_H */
```

### Full UART Driver Using ring_buffer.h

```c
/*
 * uart.h
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>

void    uart_init(uint32_t baud);
uint8_t uart_putc(uint8_t c);
void    uart_puts(const char *s);
void    uart_write(const uint8_t *buf, size_t len);
uint8_t uart_getc(uint8_t *c);
uint8_t uart_available(void);

#endif
```

```c
/*
 * uart.c
 */
#include "uart.h"
#include "ring_buffer.h"
#include <avr/io.h>
#include <avr/interrupt.h>

static ring_buf_t rx_ring = {0};
static ring_buf_t tx_ring = {0};

ISR(USART_RX_vect)
{
    uint8_t status = UCSR0A;
    uint8_t data   = UDR0;
    if (!(status & ((1 << FE0) | (1 << DOR0) | (1 << UPE0))))
        ring_buf_put(&rx_ring, data);
}

ISR(USART_UDRE_vect)
{
    uint8_t byte;
    if (ring_buf_get(&tx_ring, &byte))
        UDR0 = byte;
    else
        UCSR0B &= ~(1 << UDRIE0);
}

void uart_init(uint32_t baud)
{
    uint16_t ubrr = (uint16_t)((F_CPU / (16UL * baud)) - 1);
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr);
    UCSR0B = (1 << RXCIE0) | (1 << RXEN0) | (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

uint8_t uart_putc(uint8_t c)
{
    uint8_t ok = ring_buf_put(&tx_ring, c);
    UCSR0B |= (1 << UDRIE0);   /* Kick transmitter */
    return ok;
}

void uart_puts(const char *s)
{
    while (*s) uart_putc((uint8_t)*s++);
}

void uart_write(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) uart_putc(buf[i]);
}

uint8_t uart_getc(uint8_t *c)
{
    return ring_buf_get(&rx_ring, c);
}

uint8_t uart_available(void)
{
    return ring_buf_count(&rx_ring);
}
```

---

## 10. AVR UART in Rust

Rust on AVR requires the `avr-hal` crate ecosystem and a nightly compiler with the `avr-unknown-gnu-atmega328` target. The `avr-hal` project provides safe, idiomatic Rust wrappers over the hardware registers.

### Project Setup

**`.cargo/config.toml`:**
```toml
[build]
target = "avr-unknown-gnu-atmega328"

[profile.dev]
panic = "abort"
lto = true
opt-level = "s"

[profile.release]
panic = "abort"
lto = true
opt-level = "s"
```

**`Cargo.toml`:**
```toml
[package]
name = "avr-uart-demo"
version = "0.1.0"
edition = "2021"

[dependencies]
arduino-hal = { git = "https://github.com/Rahix/avr-hal", features = ["arduino-uno"] }
panic-halt  = "0.2"
ufmt        = "0.2"

[lib]
# Required for AVR — no standard entry point
```

### Basic UART using avr-hal

```rust
/*!
 * AVR UART in Rust — Basic example using avr-hal
 * Target: Arduino Uno (ATmega328P)
 *
 * Build: cargo build --release
 * Flash: avrdude -p atmega328p -c arduino -P /dev/ttyUSB0 \
 *                -b 115200 -U flash:w:target/.../avr-uart-demo.elf
 */

#![no_std]
#![no_main]

use panic_halt as _;
use arduino_hal::prelude::*;
use ufmt::uwriteln;

#[arduino_hal::entry]
fn main() -> ! {
    // Acquire peripherals from the HAL
    let dp      = arduino_hal::Peripherals::take().unwrap();
    let pins    = arduino_hal::pins!(dp);

    // Initialise UART at 9600 baud (8N1 by default)
    let mut serial = arduino_hal::default_serial!(dp, pins, 9600);

    uwriteln!(&mut serial, "AVR Rust UART ready").unwrap();

    loop {
        // uart_read() blocks until a byte arrives
        let byte = nb::block!(serial.read()).unwrap();

        // Echo back with a note
        uwriteln!(&mut serial, "Received: {}", byte).unwrap();
    }
}
```

### Bare-Metal UART in Rust (No HAL)

For complete control without the HAL layer, use `avr-device` and access registers directly. This mirrors the C/C++ approach exactly:

```rust
/*!
 * Bare-metal UART in Rust — direct register access
 * ATmega328P @ 16 MHz, 9600 baud
 */

#![no_std]
#![no_main]

use core::ptr::{read_volatile, write_volatile};
use panic_halt as _;

// ATmega328P USART0 register addresses
const UCSR0A: *mut u8 = 0xC0 as *mut u8;
const UCSR0B: *mut u8 = 0xC1 as *mut u8;
const UCSR0C: *mut u8 = 0xC2 as *mut u8;
const UBRR0L: *mut u8 = 0xC4 as *mut u8;
const UBRR0H: *mut u8 = 0xC5 as *mut u8;
const UDR0:   *mut u8 = 0xC6 as *mut u8;

// UCSR0A bits
const UDRE0: u8 = 1 << 5;
const RXC0:  u8 = 1 << 7;
const U2X0:  u8 = 1 << 1;

// UCSR0B bits
const RXEN0:  u8 = 1 << 4;
const TXEN0:  u8 = 1 << 3;
const RXCIE0: u8 = 1 << 7;

// UCSR0C bits
const UCSZ01: u8 = 1 << 2;
const UCSZ00: u8 = 1 << 1;

const F_CPU: u32 = 16_000_000;
const BAUD:  u32 = 9600;
const UBRR:  u16 = ((F_CPU / (16 * BAUD)) - 1) as u16;

struct Uart;

impl Uart {
    /// # Safety
    /// Must only be called once during system init.
    unsafe fn init(&self) {
        write_volatile(UBRR0H, (UBRR >> 8) as u8);
        write_volatile(UBRR0L,  UBRR       as u8);

        // Enable TX and RX
        write_volatile(UCSR0B, RXEN0 | TXEN0);

        // 8N1: UCSZ01 | UCSZ00, async mode
        write_volatile(UCSR0C, UCSZ01 | UCSZ00);
    }

    /// Transmit one byte — blocking
    unsafe fn putc(&self, byte: u8) {
        // Wait until transmit buffer is empty
        while read_volatile(UCSR0A) & UDRE0 == 0 {}
        write_volatile(UDR0, byte);
    }

    /// Receive one byte — blocking
    unsafe fn getc(&self) -> u8 {
        // Wait until data is available
        while read_volatile(UCSR0A) & RXC0 == 0 {}
        read_volatile(UDR0)
    }

    /// Transmit a string
    unsafe fn puts(&self, s: &str) {
        for b in s.bytes() {
            self.putc(b);
        }
    }

    /// Non-blocking check
    unsafe fn has_data(&self) -> bool {
        read_volatile(UCSR0A) & RXC0 != 0
    }
}

/// Minimal write! support for UART
struct UartWriter(Uart);

impl core::fmt::Write for UartWriter {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        unsafe { self.0.puts(s); }
        Ok(())
    }
}

#[no_mangle]
pub extern "C" fn main() -> ! {
    let uart = Uart;
    unsafe {
        uart.init();
        uart.puts("Bare-metal Rust UART\r\n");

        loop {
            if uart.has_data() {
                let ch = uart.getc();
                uart.puts("RX: ");
                uart.putc(ch);
                uart.puts("\r\n");
            }
        }
    }
}
```

### Interrupt-Driven UART in Rust

```rust
/*!
 * Interrupt-driven UART in Rust using avr-hal + avr-device
 * Demonstrates safe shared-state access with critical sections.
 */

#![no_std]
#![no_main]
#![feature(abi_avr_interrupt)]

use core::cell::RefCell;
use avr_device::interrupt::{self, Mutex};
use arduino_hal::prelude::*;
use panic_halt as _;

// ── Shared ring buffer ──────────────────────────────────────────────

const BUF_SIZE: usize = 64;

struct RingBuf {
    data: [u8; BUF_SIZE],
    head: usize,
    tail: usize,
}

impl RingBuf {
    const fn new() -> Self {
        Self { data: [0; BUF_SIZE], head: 0, tail: 0 }
    }

    fn push(&mut self, byte: u8) -> bool {
        let next = (self.head + 1) % BUF_SIZE;
        if next == self.tail { return false; }    // full
        self.data[self.head] = byte;
        self.head = next;
        true
    }

    fn pop(&mut self) -> Option<u8> {
        if self.head == self.tail { return None; } // empty
        let byte = self.data[self.tail];
        self.tail = (self.tail + 1) % BUF_SIZE;
        Some(byte)
    }

    fn is_empty(&self) -> bool { self.head == self.tail }
}

// ── Global state protected by a Mutex ──────────────────────────────
// avr_device::interrupt::Mutex uses critical sections (cli/sei pairs)
// to protect shared data — safe even from ISR context.

static RX_BUF: Mutex<RefCell<RingBuf>> =
    Mutex::new(RefCell::new(RingBuf::new()));

static SERIAL: Mutex<RefCell<Option<arduino_hal::hal::usart::Usart0>>> =
    Mutex::new(RefCell::new(None));

// ── RX Complete ISR ─────────────────────────────────────────────────

#[avr_device::interrupt(atmega328p)]
fn USART_RX() {
    interrupt::free(|cs| {
        let mut serial_ref = SERIAL.borrow(cs).borrow_mut();
        if let Some(serial) = serial_ref.as_mut() {
            if let Ok(byte) = serial.read() {
                RX_BUF.borrow(cs).borrow_mut().push(byte);
            }
        }
    });
}

// ── Entry point ─────────────────────────────────────────────────────

#[arduino_hal::entry]
fn main() -> ! {
    let dp   = arduino_hal::Peripherals::take().unwrap();
    let pins = arduino_hal::pins!(dp);

    let mut serial = arduino_hal::default_serial!(dp, pins, 9600);

    // Move serial into global — enable RX interrupt
    interrupt::free(|cs| {
        *SERIAL.borrow(cs).borrow_mut() = Some(serial);
    });

    // Enable global interrupts
    unsafe { avr_device::interrupt::enable() };

    loop {
        interrupt::free(|cs| {
            let mut buf = RX_BUF.borrow(cs).borrow_mut();
            while let Some(byte) = buf.pop() {
                // Echo: in real code, push to TX ring buffer instead
                let _ = byte; // process byte here
            }
        });

        // CPU can do other work here
        arduino_hal::delay_ms(1);
    }
}
```

---

## 11. ATtiny UART Specifics

Many ATtiny devices lack a hardware UART — they may have only a **USI** (Universal Serial Interface) or — in newer families — a **USART** in **LIN/UART** mode.

### ATtiny85 — Software UART (Bit-Banging)

The ATtiny85 has no hardware UART. A software UART bit-bangs the TX pin using timer-based delays:

```c
/*
 * Minimal software UART TX for ATtiny85 @ 8 MHz, 9600 baud
 * Uses _delay_loop_2() for timing — calibrate for your F_CPU.
 *
 * TX_PIN: PB3 (physical pin 2)
 */

#define F_CPU       8000000UL
#include <avr/io.h>
#include <util/delay.h>

#define UART_TX_PIN  PB3
#define BAUD_DELAY_US  (1000000UL / 9600UL)   /* ≈ 104 µs per bit */

void soft_uart_init(void)
{
    DDRB  |=  (1 << UART_TX_PIN);   /* TX pin as output */
    PORTB |=  (1 << UART_TX_PIN);   /* Idle HIGH */
}

void soft_uart_putchar(uint8_t data)
{
    /* Start bit */
    PORTB &= ~(1 << UART_TX_PIN);
    _delay_us(BAUD_DELAY_US);

    /* Data bits LSB first */
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x01)
            PORTB |=  (1 << UART_TX_PIN);
        else
            PORTB &= ~(1 << UART_TX_PIN);
        _delay_us(BAUD_DELAY_US);
        data >>= 1;
    }

    /* Stop bit */
    PORTB |= (1 << UART_TX_PIN);
    _delay_us(BAUD_DELAY_US);
}

void soft_uart_puts(const char *s)
{
    while (*s) soft_uart_putchar((uint8_t)*s++);
}
```

> **Note:** Software UART is sensitive to interrupts. Disable interrupts during transmission with `cli()` / `sei()` if any ISRs might interfere with timing.

### ATtiny USART (ATtiny416/817/1616 series — megaTINY family)

Newer ATtiny devices in Microchip's **megaTINY** family (ATtiny416, ATtiny817, ATtiny1616, etc.) have a real USART peripheral, but its register names differ significantly from ATmega:

```c
/*
 * Hardware UART on ATtiny416 (megaTINY/XMEGA-style USART)
 * F_CPU = 3.333 MHz (default 20 MHz / 6), BAUD = 9600
 *
 * The BAUD register directly accepts a 16-bit fractional value.
 * Formula: BAUD_REG = (64 * F_CPU) / (16 * BAUD)
 */

#define F_CPU        3333333UL
#define BAUD_RATE    9600UL
#define BAUD_REG     ((uint16_t)((64.0 * F_CPU) / (16.0 * BAUD_RATE)))

void usart0_init(void)
{
    /* PA1 = TXD, PA2 = RXD on ATtiny416 */
    PORTA.DIR |= PIN1_bm;          /* TXD as output */

    USART0.BAUD   = BAUD_REG;
    USART0.CTRLB  = USART_TXEN_bm | USART_RXEN_bm;
    USART0.CTRLC  = USART_CHSIZE_8BIT_gc;   /* 8N1 */
}

void usart0_putchar(uint8_t data)
{
    while (!(USART0.STATUS & USART_DREIF_bm))
        ;
    USART0.TXDATAL = data;
}

uint8_t usart0_getchar(void)
{
    while (!(USART0.STATUS & USART_RXCIF_bm))
        ;
    return USART0.RXDATAL;
}
```

---

## 12. Common Pitfalls and Debugging Tips

### Pitfall 1 — Wrong Baud Rate

**Symptom:** Garbled output (`ÿ`, `?`, random characters).

**Cause:** UBRR set to a wrong value, often because `F_CPU` is mismatched or integer overflow occurred.

**Fix:**
```c
/* WRONG — integer overflow on 8-bit! */
#define UBRR_BAD   (F_CPU / 16 / BAUD - 1)    /* intermediate values overflow */

/* CORRECT — force 32-bit arithmetic */
#define UBRR_GOOD  ((uint16_t)((F_CPU / (16UL * BAUD)) - 1))
```

### Pitfall 2 — Missing `sei()` for Interrupt-Driven UART

**Symptom:** Program hangs, no data received or sent.

**Cause:** Global interrupt flag not set. Both `RXCIE0` and `UDRIE0` require global interrupts enabled.

**Fix:** Call `sei()` after `uart_init()`.

### Pitfall 3 — Reading UDR0 Twice in Error Handling

**Symptom:** Lost bytes or wrong error detection.

**Cause:** `UCSR0A` (with error flags) and `UDR0` must be read in a specific order. Reading `UDR0` clears `RXC0` and the error flags.

**Fix:**
```c
/* Always read status FIRST, then UDR0 */
uint8_t status = UCSR0A;
uint8_t data   = UDR0;   /* This clears RXC0 */
if (status & (1 << FE0)) { /* frame error */ }
```

### Pitfall 4 — TX Buffer Race Condition

**Symptom:** Occasional duplicate characters or missed bytes.

**Cause:** Non-atomic read-modify-write on the head/tail pointers or enabling UDRIE0 with a non-atomic OR.

**Fix:** Disable interrupts briefly when modifying shared state, or use `uint8_t` (inherently atomic on 8-bit AVR) for head/tail pointers within a single buffer.

### Pitfall 5 — `printf` / `scanf` Not Working

**Symptom:** Nothing happens when using stdio functions.

**Cause:** avr-libc's `printf` / `scanf` require stdio stream redirection.

**Fix:**
```c
#include <stdio.h>
#include <avr/io.h>

static int uart_putchar_stdio(char c, FILE *stream)
{
    (void)stream;
    uart_putchar((uint8_t)c);
    return 0;
}

static int uart_getchar_stdio(FILE *stream)
{
    (void)stream;
    return (int)uart_getchar();
}

static FILE uart_stream = FDEV_SETUP_STREAM(
    uart_putchar_stdio, uart_getchar_stdio, _FDEV_SETUP_RW);

int main(void)
{
    uart_init();
    stdout = &uart_stream;
    stdin  = &uart_stream;

    printf("Hello from printf!\r\n");
    /* ... */
}
```

> **Warning:** `printf` with floating-point support adds ~1.5 KB to flash. Use `printf_P` with `PSTR()` to keep format strings in flash, and `ufmt` / manual formatting on memory-constrained devices.

### Pitfall 6 — ATmega2560 Has Multiple USARTs

The ATmega2560 has four USARTs. Register names include a number: `UCSR0A` vs `UCSR1A`. Ensure you're referencing the correct peripheral for your wiring.

---

## 13. Summary

AVR UART programming at the bare-metal level requires understanding the hardware architecture of the USART peripheral and its key registers. The following table consolidates the essential points:

| Topic                  | Key Points                                                                                   |
|------------------------|----------------------------------------------------------------------------------------------|
| **Baud Rate**          | `UBRR = F_CPU / (16 × BAUD) - 1`; use `UL` suffixes to prevent overflow; use U2X for 115200 baud at 16 MHz |
| **Frame Format**       | Configure UCSR0C: data bits (UCSZ), parity (UPM), stop bits (USBS), mode (UMSEL)           |
| **Enable**             | Set `RXEN0` and `TXEN0` in UCSR0B to activate receiver and transmitter independently        |
| **Polling TX**         | Wait for `UDRE0` flag in UCSR0A, then write to UDR0                                         |
| **Polling RX**         | Wait for `RXC0` flag in UCSR0A, then read UDR0                                              |
| **Error Detection**    | Read UCSR0A first (checking FE0, DOR0, UPE0), then read UDR0 to clear flags                |
| **IRQ-Driven TX**      | Use ring buffer + UDRE ISR; enable `UDRIE0` when data is queued, disable when empty         |
| **IRQ-Driven RX**      | Use ring buffer + USART_RX ISR with `RXCIE0` enabled                                        |
| **Ring Buffer**        | Power-of-2 size with bitmask (`& (N-1)`) replaces modulo; `uint8_t` head/tail is atomic     |
| **Rust / avr-hal**     | `arduino_hal::default_serial!()` wraps USART; use `avr_device::interrupt::Mutex` for ISR-safe globals |
| **ATtiny85**           | No hardware UART — bit-bang with `_delay_us()`; disable interrupts during TX for accuracy   |
| **megaTINY USART**     | Different register names (USART0.BAUD, USART0.CTRLB); fractional baud formula: `64 × F_CPU / (16 × BAUD)` |
| **stdio integration**  | Use `FDEV_SETUP_STREAM` to redirect `printf`/`scanf` through UART; avoid floats to save flash |
| **Global interrupts**  | Must call `sei()` for any interrupt-driven mode to work                                      |

Mastering bare-metal UART unlocks reliable, efficient serial communication on AVR devices and builds the foundation for implementing more complex protocols — SPI, I2C, DMX512, MODBUS RTU, and beyond — all of which follow similar patterns of register configuration, flag polling, and interrupt management.

---

*Document generated for the AVR Bare-Metal Programming reference series.*