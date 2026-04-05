# 13. Interrupt Service Routines (ISR) for UART TX/RX

**Architecture & Theory**
- How the CPU reaches an ISR (IRQ → context save → execute → restore → EOI)
- All UART interrupt sources (RXNE, TXE, TC, OE, FE, PE, break detect) with typical flag names
- Priority assignment rationale (RX > TX; never highest system priority)

**Design Patterns**
- SPSC lock-free ring buffer (power-of-two, head/tail with memory barriers)
- RX ISR: snapshot SR → read DR → push to ring; error detection before push
- TX ISR: pop from ring → write DR; disable TXEIE when buffer empties ("enable on demand")
- Double / ping-pong buffering for FIFO-heavy peripherals

**C/C++ Examples**
- `uart_ring.h` — templated ring buffer with `_Static_assert` power-of-two check
- `uart_isr.c` — full STM32/CMSIS `USART1_IRQHandler` with RX + TX paths and error counters
- `posix_uart_async.c` — Linux `SIGIO`-driven async RX (the bare-metal ISR equivalent on Linux)
- `uart_isr.hpp` — C++17 `SpscRingBuffer<T, N>` template + `UartDriver` class with `handle_irq()`

**Rust Examples**
- `ring_buffer.rs` — `unsafe` SPSC ring using `AtomicUsize` with `Acquire`/`Release` ordering
- `uart_isr.rs` — `#[interrupt] fn USART1()` with `cortex-m-rt`, `uart_write`/`uart_read` API
- `posix_uart_async.rs` — `tokio-serial` async RX task (equivalent to the ISR in async Rust)
- `isr_safety.rs` — type-level critical-section token pattern to enforce shared-state access rules at compile time

**Summary table** with golden rules and the baud-rate latency budget rule of thumb.

## Table of Contents

1. [Introduction](#introduction)
2. [ISR Fundamentals for UART](#isr-fundamentals-for-uart)
3. [Interrupt Sources and Priority](#interrupt-sources-and-priority)
4. [RX Interrupt Service Routine](#rx-interrupt-service-routine)
5. [TX Interrupt Service Routine](#tx-interrupt-service-routine)
6. [Double-Buffering and Ping-Pong Buffers](#double-buffering-and-ping-pong-buffers)
7. [Error Handling inside ISRs](#error-handling-inside-isrs)
8. [Latency Reduction Techniques](#latency-reduction-techniques)
9. [C/C++ Implementation](#cc-implementation)
10. [Rust Implementation](#rust-implementation)
11. [Summary](#summary)

---

## Introduction

A **UART Interrupt Service Routine (ISR)** is a short, highly optimised piece of code that runs
automatically in response to a hardware event — such as a byte arriving in the receive holding
register, or the transmit holding register becoming empty. Unlike polling, where the CPU wastes
cycles continuously checking status flags, interrupts allow the CPU to do useful work and only
divert execution when the UART actually needs attention.

Efficient ISR design is critical in any real-time or resource-constrained system:

- **Minimal latency** prevents receive FIFO overruns and keeps transmit pipelines full.
- **Minimal ISR duration** reduces the time other interrupts (or the main thread) are blocked.
- **No blocking calls** inside an ISR — no `malloc`, no `printf`, no mutexes.
- **Correct data hand-off** to the application layer via lock-free ring buffers or similar.

---

## ISR Fundamentals for UART

### How the CPU reaches an ISR

```
  Hardware event           CPU             ISR
  (byte received)  ──►  saves context ──► runs ISR ──► restores context ──► resumes main
                         (registers,           ▲
                          PC, PSW)             │
                                        Interrupt Vector Table entry
                                        points to ISR function address
```

Key phases:

1. **Interrupt request (IRQ)** — UART asserts an interrupt line to the interrupt controller (NVIC on ARM Cortex-M, PIC on AVR, APIC on x86).
2. **Context save** — hardware (or compiler-generated prologue) saves the CPU registers needed to resume execution.
3. **ISR execution** — the ISR reads/writes the UART data register, updates shared buffers, clears the interrupt flag.
4. **Context restore** — registers are restored; execution continues where it was interrupted.
5. **End-of-interrupt (EOI)** — on some architectures (e.g. x86 with PIC/APIC) the ISR must explicitly signal EOI to the interrupt controller.

### Constraints that define "efficient"

| Constraint | Reason |
|---|---|
| As few instructions as possible | Every instruction inside the ISR delays other work |
| No dynamic memory allocation | `malloc` is non-reentrant and unpredictably slow |
| No blocking synchronisation primitives | Mutexes can sleep; ISRs must never sleep |
| No floating-point (unless saved) | FPU registers need extra save/restore overhead |
| Clear the interrupt flag *before* processing data (on level-triggered systems) | Prevents missed interrupts |
| Use `volatile` or memory barriers for shared variables | Prevents compiler/CPU reordering |

---

## Interrupt Sources and Priority

A UART peripheral typically generates several independent interrupt sources. All may share a single
IRQ line (read the interrupt status register to distinguish) or have individual IRQ lines depending
on the silicon:

| Interrupt source | Typical flag name | Meaning |
|---|---|---|
| **RX data ready** | `RXRDY`, `RXNE` | ≥1 byte available in RX holding register or FIFO |
| **TX holding register empty** | `TXRDY`, `TXE`, `THRE` | TX register/FIFO has room; send next byte |
| **RX FIFO half-full** | `RXHF` | FIFO-based UARTs; batch read possible |
| **RX timeout** | `RXTOUT` | No new data for N bit-periods; flush partial frames |
| **TX complete** | `TC` | Last stop bit shifted out; safe to disable TX driver |
| **Overrun error** | `OE` | New byte arrived before previous one was read |
| **Framing error** | `FE` | Stop bit was 0 instead of 1 |
| **Parity error** | `PE` | Parity check failed |
| **Break detect** | `BI` | RX held low > 1 frame |

### Priority assignment guidelines

- Assign **higher priority** to RX than TX — a missed receive byte is unrecoverable (overrun);
  a delayed transmit byte only reduces throughput.
- Do **not** assign the UART ISR the highest priority in the system unless the baud rate demands it;
  leave room for safety-critical IRQs (watchdog, hard-fault).

---

## RX Interrupt Service Routine

The canonical RX ISR does exactly three things:

1. Read the status register.
2. Read the data register (this clears the interrupt on most UARTs).
3. Write the byte into a ring buffer for the application to consume.

```
RX ISR timeline (per byte at 115200 baud → 86.8 µs/byte):

  Byte N arrives ──► ISR fires ──► read DR ──► push to ring buf ──► EOI ──► return
  │←────────────────── must finish in < 86.8 µs ──────────────────────────►│
```

### Ring buffer design for RX

A **power-of-two sized** lock-free single-producer / single-consumer ring buffer is the standard
pattern. The ISR is the sole *producer*; the application is the sole *consumer*. No mutex is needed
as long as there is exactly one producer and one consumer.

```
  head (written by ISR)          tail (read by application)
    │                              │
    ▼                              ▼
  [ 0x41 | 0x42 | 0x43 | 0x44 | 0x45 | .... | .... | .... ]
           ^───── unread data ─────^
```

- `head` advances when the ISR pushes a byte.
- `tail` advances when the application pops a byte.
- Buffer is **full** when `(head + 1) & mask == tail`.
- Buffer is **empty** when `head == tail`.

---

## TX Interrupt Service Routine

TX is driven from the "TX register empty" interrupt. The pattern is:

1. Application fills a TX ring buffer and enables the TX interrupt.
2. ISR fires, pops one byte from the ring buffer, writes it to the TX data register.
3. When the ring buffer becomes empty the ISR **disables the TX interrupt** (otherwise it would fire
   continuously with nothing to send).

```
Application:         push bytes into TX ring buf ──► enable TXE interrupt
                                                           │
                                                           ▼
TX ISR:   ◄── TXE fires ── pop byte ── write to DR ── buffer empty? ── yes ──► disable TXE IRQ
               ▲                                           │
               └───────────────── no ──────────────────────┘
```

This "enable on demand / disable when empty" pattern avoids the ISR firing wastefully.

---

## Double-Buffering and Ping-Pong Buffers

For high-throughput transfers a simple ring buffer can be replaced with a **ping-pong (double)
buffer**:

```
  Buffer A  ──► ISR fills A ──► signal app ──► app processes A
                                                              │
  Buffer B  ◄────────────────────────────── ISR now fills B ◄┘
```

- One buffer is always being filled by the ISR (DMA or interrupt-driven).
- The other is available to the application.
- Swap atomically using a single flag or pointer.

This is most useful when the UART has a hardware FIFO (e.g. 64-byte FIFO on STM32 LPUART).

---

## Error Handling inside ISRs

Errors must be handled *inside* the ISR, not deferred, because the status flags are often cleared
automatically when the data register is read:

```
ISR:
  status = UART->SR
  if (status & OVERRUN_FLAG)   ── log/increment overrun counter; recover
  if (status & FRAMING_ERROR)  ── log/increment framing counter; discard byte
  if (status & PARITY_ERROR)   ── log/increment parity counter; discard byte
  data = UART->DR              ── reading DR clears all flags on many UARTs
  push data to ring buf
```

Keep error counters as `volatile` integers readable by the application for diagnostics.

---

## Latency Reduction Techniques

| Technique | Effect |
|---|---|
| Use a **power-of-two ring buffer** | Replace modulo (%) with bitwise AND (&) — avoids divide |
| **Inline critical path** | Mark ISR and buffer ops `__attribute__((always_inline))` in C |
| Place ISR in **SRAM / ITCM** | Avoid flash wait-states on every instruction fetch |
| Enable **UART FIFO** mode | Interrupt fires once per N bytes, amortising ISR overhead |
| Use **DMA with half/full-transfer interrupts** | CPU not involved per byte; ISR fires per block |
| Minimise ISR **prologue/epilogue** | Use `__attribute__((interrupt))` or `#[naked]` + manual save |
| Avoid function calls inside ISR | Call overhead + compiler register saves add latency |
| Set ISR **CPU affinity** (multi-core) | Prevents cache-line bouncing on SMP systems |

---

## C/C++ Implementation

### Shared ring buffer (header)

```c
/* uart_ring.h – lock-free SPSC ring buffer for UART ISR use */
#ifndef UART_RING_H
#define UART_RING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define UART_RX_BUF_SIZE  256u   /* Must be a power of two */
#define UART_TX_BUF_SIZE  256u   /* Must be a power of two */
#define UART_BUF_MASK(sz) ((sz) - 1u)

/* Compile-time assertion that sizes are powers of two */
_Static_assert((UART_RX_BUF_SIZE & (UART_RX_BUF_SIZE - 1u)) == 0,
               "RX buffer size must be a power of two");
_Static_assert((UART_TX_BUF_SIZE & (UART_TX_BUF_SIZE - 1u)) == 0,
               "TX buffer size must be a power of two");

typedef struct {
    volatile uint8_t  buf[UART_RX_BUF_SIZE];
    volatile uint32_t head;   /* written by ISR  (producer) */
    volatile uint32_t tail;   /* written by app  (consumer) */
} UartRxRing;

typedef struct {
    volatile uint8_t  buf[UART_TX_BUF_SIZE];
    volatile uint32_t head;   /* written by app  (producer) */
    volatile uint32_t tail;   /* written by ISR  (consumer) */
} UartTxRing;

typedef struct {
    volatile uint32_t overruns;
    volatile uint32_t framing_errors;
    volatile uint32_t parity_errors;
} UartErrors;

/* Returns true if push succeeded (buffer not full) */
static inline bool rx_ring_push(UartRxRing *r, uint8_t byte)
{
    uint32_t next_head = (r->head + 1u) & UART_BUF_MASK(UART_RX_BUF_SIZE);
    if (next_head == r->tail) {
        return false;  /* full – overrun in software */
    }
    r->buf[r->head] = byte;
    /* Memory barrier: ensure data is written before head advances */
    __asm__ volatile ("" ::: "memory");
    r->head = next_head;
    return true;
}

/* Returns true if pop succeeded (buffer not empty) */
static inline bool rx_ring_pop(UartRxRing *r, uint8_t *out)
{
    if (r->tail == r->head) {
        return false;  /* empty */
    }
    *out = r->buf[r->tail];
    __asm__ volatile ("" ::: "memory");
    r->tail = (r->tail + 1u) & UART_BUF_MASK(UART_RX_BUF_SIZE);
    return true;
}

static inline bool tx_ring_push(UartTxRing *r, uint8_t byte)
{
    uint32_t next_head = (r->head + 1u) & UART_BUF_MASK(UART_TX_BUF_SIZE);
    if (next_head == r->tail) {
        return false;  /* full */
    }
    r->buf[r->head] = byte;
    __asm__ volatile ("" ::: "memory");
    r->head = next_head;
    return true;
}

static inline bool tx_ring_pop(UartTxRing *r, uint8_t *out)
{
    if (r->tail == r->head) {
        return false;  /* empty */
    }
    *out = r->buf[r->tail];
    __asm__ volatile ("" ::: "memory");
    r->tail = (r->tail + 1u) & UART_BUF_MASK(UART_TX_BUF_SIZE);
    return true;
}

static inline bool tx_ring_empty(const UartTxRing *r)
{
    return r->head == r->tail;
}

#endif /* UART_RING_H */
```

---

### STM32-style UART ISR (C, Cortex-M / CMSIS)

```c
/* uart_isr.c – ISR for a CMSIS/STM32-style UART peripheral */
#include "uart_ring.h"
#include "stm32xx.h"    /* provides USART_TypeDef, NVIC_*, etc. */

/* -----------------------------------------------------------------
 * Register-level bit definitions (typical STM32 USART)
 * ----------------------------------------------------------------- */
#define SR_RXNE  (1u << 5)   /* RX Not Empty  – data ready to read  */
#define SR_TXE   (1u << 7)   /* TX Empty      – holding reg free    */
#define SR_TC    (1u << 6)   /* Transmission Complete                */
#define SR_ORE   (1u << 3)   /* Overrun Error                       */
#define SR_FE    (1u << 1)   /* Framing Error                       */
#define SR_PE    (1u << 0)   /* Parity Error                        */

#define CR1_RXNEIE (1u << 5) /* Enable RXNE interrupt               */
#define CR1_TXEIE  (1u << 7) /* Enable TXE interrupt                */

/* Peripheral and shared state */
static USART_TypeDef * const UART = USART1;

static UartRxRing g_rx;
static UartTxRing g_tx;
static UartErrors g_errors;

/* -----------------------------------------------------------------
 * ISR – placed in SRAM for zero wait-state execution.
 * On Cortex-M the vector table entry must point here.
 * Use __attribute__((section(".ram_text"))) and the linker script
 * to position this function in SRAM.
 * ----------------------------------------------------------------- */
__attribute__((section(".ram_text")))
void USART1_IRQHandler(void)
{
    uint32_t sr = UART->SR;   /* Snapshot status; reading clears some flags */

    /* ── RX path ─────────────────────────────────────────────── */
    if (sr & SR_RXNE) {
        uint8_t data = (uint8_t)(UART->DR);  /* Reading DR clears RXNE + error flags */

        if (sr & SR_ORE) {
            g_errors.overruns++;
            /* Byte is lost; do not push corrupt data */
        } else if (sr & SR_FE) {
            g_errors.framing_errors++;
            /* Discard; framing error means data is unreliable */
        } else if (sr & SR_PE) {
            g_errors.parity_errors++;
            /* Discard; parity failed */
        } else {
            /* Good byte – push to ring buffer */
            if (!rx_ring_push(&g_rx, data)) {
                g_errors.overruns++;   /* Software overrun: ring buffer full */
            }
        }
    }

    /* ── TX path ─────────────────────────────────────────────── */
    if (sr & SR_TXE) {
        uint8_t out;
        if (tx_ring_pop(&g_tx, &out)) {
            UART->DR = out;
        } else {
            /* Ring buffer empty – disable TXE interrupt to stop spurious firing */
            UART->CR1 &= ~CR1_TXEIE;
        }
    }
}

/* -----------------------------------------------------------------
 * Public API (called from application, not from ISR)
 * ----------------------------------------------------------------- */

/** Queue bytes for transmission. Returns number of bytes accepted. */
size_t uart_write(const uint8_t *data, size_t len)
{
    size_t sent = 0;
    for (size_t i = 0; i < len; i++) {
        if (!tx_ring_push(&g_tx, data[i])) {
            break;
        }
        sent++;
    }
    /* Enable TXE interrupt to start draining the ring buffer */
    if (sent > 0) {
        UART->CR1 |= CR1_TXEIE;
    }
    return sent;
}

/** Read up to len bytes from the RX ring buffer. Returns bytes read. */
size_t uart_read(uint8_t *buf, size_t len)
{
    size_t count = 0;
    while (count < len && rx_ring_pop(&g_rx, &buf[count])) {
        count++;
    }
    return count;
}

/** Return snapshot of error counters. */
UartErrors uart_get_errors(void)
{
    UartErrors snap;
    /* Read with interrupts disabled for consistent snapshot */
    __disable_irq();
    snap = g_errors;
    __enable_irq();
    return snap;
}
```

---

### POSIX / Linux UART ISR equivalent (C, signal-driven I/O)

On Linux the closest equivalent to a hardware ISR is **`SIGIO`** (signal-driven I/O). The
`SIGIO` handler is invoked by the kernel when data arrives, giving non-blocking asynchronous RX.

```c
/* posix_uart_async.c – signal-driven UART on Linux */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>

#define RX_BUF_SZ 4096u

/* Shared between signal handler and main – must be volatile */
static volatile uint8_t  g_rx_buf[RX_BUF_SZ];
static volatile uint32_t g_rx_head = 0;
static volatile uint32_t g_rx_tail = 0;
static volatile uint32_t g_rx_overruns = 0;

static int g_fd = -1;

/* ----------------------------------------------------------------
 * SIGIO handler – behaves like a hardware UART RX ISR.
 * Runs asynchronously; must be async-signal-safe.
 * ---------------------------------------------------------------- */
static void sigio_handler(int signo)
{
    (void)signo;
    uint8_t tmp[64];   /* Read in chunks to drain quickly */
    ssize_t n;

    while ((n = read(g_fd, tmp, sizeof(tmp))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            uint32_t next = (g_rx_head + 1u) & (RX_BUF_SZ - 1u);
            if (next == g_rx_tail) {
                g_rx_overruns++;   /* Software overrun */
                continue;
            }
            g_rx_buf[g_rx_head] = tmp[i];
            __asm__ volatile ("" ::: "memory");   /* Compiler barrier */
            g_rx_head = next;
        }
    }
    /* n == -1 with errno == EAGAIN means we drained all available data */
}

/* ----------------------------------------------------------------
 * Initialise serial port and register signal handler
 * ---------------------------------------------------------------- */
int uart_open(const char *dev, int baud_const)
{
    g_fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (g_fd < 0) return -1;

    struct termios tty = {0};
    cfmakeraw(&tty);
    cfsetspeed(&tty, baud_const);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    tcsetattr(g_fd, TCSANOW, &tty);

    /* Register SIGIO handler */
    struct sigaction sa = { .sa_handler = sigio_handler, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGIO, &sa, NULL);

    /* Direct SIGIO to this process, enable async notification */
    fcntl(g_fd, F_SETOWN, getpid());
    int flags = fcntl(g_fd, F_GETFL);
    fcntl(g_fd, F_SETFL, flags | O_ASYNC | O_NONBLOCK);

    return 0;
}

/** Read bytes accumulated by the signal handler. */
size_t uart_read(uint8_t *out, size_t max)
{
    size_t count = 0;
    while (count < max && g_rx_tail != g_rx_head) {
        out[count++] = g_rx_buf[g_rx_tail];
        __asm__ volatile ("" ::: "memory");
        g_rx_tail = (g_rx_tail + 1u) & (RX_BUF_SZ - 1u);
    }
    return count;
}

int main(void)
{
    if (uart_open("/dev/ttyUSB0", B115200) < 0) {
        perror("uart_open");
        return 1;
    }

    printf("Listening on /dev/ttyUSB0 at 115200 baud...\n");

    uint8_t buf[128];
    while (1) {
        size_t n = uart_read(buf, sizeof(buf));
        if (n > 0) {
            /* Process received data */
            fwrite(buf, 1, n, stdout);
            fflush(stdout);
        }
        /* Do other work here instead of blocking */
        usleep(1000);
    }
}
```

---

### C++ UART ISR with a templated ring buffer

```cpp
// uart_isr.hpp – C++17 template ring buffer + ISR wrapper
#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <optional>

/* ----------------------------------------------------------------
 * Lock-free SPSC ring buffer using C++ atomics.
 * Capacity must be a power of two.
 * ---------------------------------------------------------------- */
template<typename T, std::size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static constexpr std::size_t kMask = Capacity - 1;

    T data_[Capacity];
    std::atomic<std::size_t> head_{0};  // producer index
    std::atomic<std::size_t> tail_{0};  // consumer index

public:
    /** Push one element (producer side – called from ISR). */
    [[nodiscard]] bool push(const T& val) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t next = (h + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;   // full
        }
        data_[h] = val;
        head_.store(next, std::memory_order_release);
        return true;
    }

    /** Pop one element (consumer side – called from application). */
    [[nodiscard]] std::optional<T> pop() noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) {
            return std::nullopt;   // empty
        }
        T val = data_[t];
        tail_.store((t + 1) & kMask, std::memory_order_release);
        return val;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }
};

/* ----------------------------------------------------------------
 * UART driver using the ring buffers above.
 * Instantiate once at file scope; register uart_isr_handler as
 * the vector for your peripheral's IRQ.
 * ---------------------------------------------------------------- */
struct UartRegs {
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t CR1;
};

class UartDriver {
public:
    static constexpr uint32_t SR_RXNE  = 1u << 5;
    static constexpr uint32_t SR_TXE   = 1u << 7;
    static constexpr uint32_t SR_ORE   = 1u << 3;
    static constexpr uint32_t SR_FE    = 1u << 1;
    static constexpr uint32_t SR_PE    = 1u << 0;
    static constexpr uint32_t CR1_TXEIE = 1u << 7;

    explicit UartDriver(UartRegs* regs) noexcept : regs_(regs) {}

    /** Must be called from the UART ISR (or ISR wrapper). */
    void __attribute__((always_inline)) handle_irq() noexcept {
        const uint32_t sr = regs_->SR;

        /* ── RX path ─────── */
        if (sr & SR_RXNE) {
            const uint8_t byte = static_cast<uint8_t>(regs_->DR);
            if (sr & (SR_ORE | SR_FE | SR_PE)) {
                errors_++;
            } else if (!rx_.push(byte)) {
                sw_overruns_++;
            }
        }

        /* ── TX path ─────── */
        if (sr & SR_TXE) {
            auto byte = tx_.pop();
            if (byte.has_value()) {
                regs_->DR = *byte;
            } else {
                regs_->CR1 &= ~CR1_TXEIE;  // nothing left; stop TXE IRQs
            }
        }
    }

    /** Enqueue bytes for transmission; enables TX interrupt. */
    std::size_t write(const uint8_t* data, std::size_t len) noexcept {
        std::size_t sent = 0;
        for (std::size_t i = 0; i < len; i++) {
            if (!tx_.push(data[i])) break;
            sent++;
        }
        if (sent > 0) {
            regs_->CR1 |= CR1_TXEIE;
        }
        return sent;
    }

    /** Read bytes from the RX ring buffer. */
    std::size_t read(uint8_t* buf, std::size_t max) noexcept {
        std::size_t count = 0;
        while (count < max) {
            auto byte = rx_.pop();
            if (!byte.has_value()) break;
            buf[count++] = *byte;
        }
        return count;
    }

    uint32_t hw_errors()   const noexcept { return errors_; }
    uint32_t sw_overruns() const noexcept { return sw_overruns_; }

private:
    UartRegs* regs_;
    SpscRingBuffer<uint8_t, 256> rx_;
    SpscRingBuffer<uint8_t, 256> tx_;
    volatile uint32_t errors_     = 0;
    volatile uint32_t sw_overruns_ = 0;
};

/* Global instance – address known to linker for vector table */
extern UartDriver g_uart1;

extern "C" void USART1_IRQHandler(void) {
    g_uart1.handle_irq();
}
```

---

## Rust Implementation

Rust's ownership model and type system make it possible to enforce ISR safety at compile time.
The `cortex-m` + `cortex-m-rt` crates provide the `#[interrupt]` attribute macro that generates
correctly attributed ISR functions for ARM Cortex-M targets.

### Cargo.toml dependencies

```toml
[dependencies]
cortex-m     = "0.7"
cortex-m-rt  = "0.7"
heapless     = "0.8"    # lock-free ring buffers backed by static storage
critical-section = "1.1"

[features]
default = ["stm32f4xx-hal/stm32f411"]  # adjust for your MCU
```

---

### Lock-free ring buffer in Rust

```rust
// ring_buffer.rs – SPSC ring buffer safe for ISR use
//
// Uses core::sync::atomic for portable memory ordering without std.

use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicUsize, Ordering};

pub struct RingBuffer<const N: usize> {
    buf:  UnsafeCell<[u8; N]>,
    head: AtomicUsize,   // producer (ISR for RX, app for TX)
    tail: AtomicUsize,   // consumer (app for RX, ISR for TX)
}

// SAFETY: We enforce SPSC discipline – exactly one producer, one consumer.
// The ISR and the application thread never access the same index simultaneously.
unsafe impl<const N: usize> Sync for RingBuffer<N> {}

impl<const N: usize> RingBuffer<N> {
    const MASK: usize = N - 1;

    pub const fn new() -> Self {
        // N must be a power of two – enforced at compile time below.
        assert!(N.is_power_of_two(), "RingBuffer size must be a power of two");
        Self {
            buf:  UnsafeCell::new([0u8; N]),
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    /// Push one byte (producer side). Returns `false` if full.
    ///
    /// # Safety
    /// Must be called from at most one producer context at a time.
    pub unsafe fn push(&self, byte: u8) -> bool {
        let head = self.head.load(Ordering::Relaxed);
        let next = (head + 1) & Self::MASK;
        if next == self.tail.load(Ordering::Acquire) {
            return false;   // full
        }
        (*self.buf.get())[head] = byte;
        self.head.store(next, Ordering::Release);
        true
    }

    /// Pop one byte (consumer side). Returns `None` if empty.
    ///
    /// # Safety
    /// Must be called from at most one consumer context at a time.
    pub unsafe fn pop(&self) -> Option<u8> {
        let tail = self.tail.load(Ordering::Relaxed);
        if tail == self.head.load(Ordering::Acquire) {
            return None;    // empty
        }
        let byte = (*self.buf.get())[tail];
        self.tail.store((tail + 1) & Self::MASK, Ordering::Release);
        Some(byte)
    }

    pub fn is_empty(&self) -> bool {
        self.tail.load(Ordering::Acquire) == self.head.load(Ordering::Acquire)
    }
}
```

---

### UART ISR implementation (Rust, `cortex-m-rt`)

```rust
// uart_isr.rs – UART1 interrupt handler for Cortex-M (STM32-style)

#![no_std]
#![no_main]

use core::sync::atomic::{AtomicU32, Ordering};
use cortex_m_rt::entry;
use cortex_m::peripheral::NVIC;
// `pac` is the peripheral access crate generated by svd2rust for your MCU.
use stm32f4::stm32f411::Interrupt;
use stm32f4::stm32f411 as pac;

mod ring_buffer;
use ring_buffer::RingBuffer;

// ──────────────────────────────────────────────────────────────────────────────
// Static storage – all buffers live here. 'static lifetime, zero-init.
// ──────────────────────────────────────────────────────────────────────────────

static RX_BUF: RingBuffer<256> = RingBuffer::new();
static TX_BUF: RingBuffer<256> = RingBuffer::new();

// Error counters – written in ISR, read by application
static HW_ERRORS:   AtomicU32 = AtomicU32::new(0);
static SW_OVERRUNS: AtomicU32 = AtomicU32::new(0);

// ──────────────────────────────────────────────────────────────────────────────
// Register bit masks (STM32F4 USART)
// ──────────────────────────────────────────────────────────────────────────────
const SR_RXNE:   u32 = 1 << 5;  // RX Not Empty
const SR_TXE:    u32 = 1 << 7;  // TX Empty
const SR_ORE:    u32 = 1 << 3;  // Overrun Error
const SR_FE:     u32 = 1 << 1;  // Framing Error
const SR_PE:     u32 = 1 << 0;  // Parity Error
const CR1_TXEIE: u32 = 1 << 7;  // TX Empty Interrupt Enable

// ──────────────────────────────────────────────────────────────────────────────
// Interrupt Service Routine
//
// The `#[interrupt]` attribute (from cortex-m-rt) places this function in the
// correct slot of the vector table and generates the correct ABI.
// ──────────────────────────────────────────────────────────────────────────────
#[interrupt]
fn USART1() {
    // SAFETY: We are the sole accessor of USART1 registers from this ISR.
    // No other code touches USART1 concurrently (enforced via architecture:
    // this IRQ is not re-entrant on Cortex-M by default, and the app only
    // accesses CR1 through `uart_write`, which disables/enables the IRQ bit
    // atomically from a critical section).
    let uart = unsafe { &*pac::USART1::ptr() };

    let sr = uart.sr.read().bits();

    // ── RX path ────────────────────────────────────────────────────────────
    if sr & SR_RXNE != 0 {
        // Reading DR clears RXNE and all error flags simultaneously.
        let data = uart.dr.read().bits() as u8;

        if sr & (SR_ORE | SR_FE | SR_PE) != 0 {
            HW_ERRORS.fetch_add(1, Ordering::Relaxed);
            // Byte already consumed by the DR read; do not push corrupt data.
        } else {
            // SAFETY: RX_BUF has exactly one producer (this ISR).
            if unsafe { !RX_BUF.push(data) } {
                SW_OVERRUNS.fetch_add(1, Ordering::Relaxed);
            }
        }
    }

    // ── TX path ────────────────────────────────────────────────────────────
    if sr & SR_TXE != 0 {
        // SAFETY: TX_BUF has exactly one consumer (this ISR).
        match unsafe { TX_BUF.pop() } {
            Some(byte) => {
                uart.dr.write(|w| unsafe { w.bits(byte as u32) });
            }
            None => {
                // Nothing left to send – disable TXEIE to stop spurious IRQs.
                uart.cr1.modify(|r, w| unsafe {
                    w.bits(r.bits() & !CR1_TXEIE)
                });
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API (called from application context, not from ISR)
// ──────────────────────────────────────────────────────────────────────────────

/// Enqueue bytes for UART transmission.
/// Returns the number of bytes successfully queued.
pub fn uart_write(data: &[u8]) -> usize {
    let uart = unsafe { &*pac::USART1::ptr() };
    let mut sent = 0usize;

    for &byte in data {
        // SAFETY: TX_BUF has exactly one producer (application context).
        if unsafe { TX_BUF.push(byte) } {
            sent += 1;
        } else {
            break;  // TX buffer full
        }
    }

    if sent > 0 {
        // Enable TXEIE to start draining (critical section: 1 RMW on CR1)
        cortex_m::interrupt::free(|_| {
            uart.cr1.modify(|r, w| unsafe {
                w.bits(r.bits() | CR1_TXEIE)
            });
        });
    }

    sent
}

/// Read bytes accumulated in the RX ring buffer.
/// Returns the number of bytes written into `buf`.
pub fn uart_read(buf: &mut [u8]) -> usize {
    let mut count = 0usize;
    for slot in buf.iter_mut() {
        // SAFETY: RX_BUF has exactly one consumer (application context).
        match unsafe { RX_BUF.pop() } {
            Some(byte) => { *slot = byte; count += 1; }
            None       => break,
        }
    }
    count
}

/// Snapshot error counters.
pub fn uart_errors() -> (u32, u32) {
    (
        HW_ERRORS.load(Ordering::Relaxed),
        SW_OVERRUNS.load(Ordering::Relaxed),
    )
}

// ──────────────────────────────────────────────────────────────────────────────
// Application entry point
// ──────────────────────────────────────────────────────────────────────────────
#[entry]
fn main() -> ! {
    // (Peripheral clock enable, baud rate config, GPIO setup omitted for brevity)

    // Enable USART1 interrupt in NVIC
    unsafe { NVIC::unmask(Interrupt::USART1) };

    let greeting = b"UART ISR ready\r\n";
    uart_write(greeting);

    loop {
        let mut buf = [0u8; 64];
        let n = uart_read(&mut buf);
        if n > 0 {
            // Echo received bytes back
            uart_write(&buf[..n]);
        }

        let (hw_err, sw_err) = uart_errors();
        if hw_err > 0 || sw_err > 0 {
            // Handle / log errors
            let _ = (hw_err, sw_err);
        }

        cortex_m::asm::wfi();  // Sleep until next interrupt
    }
}
```

---

### Rust: POSIX async UART using `tokio` + `serialport`

For Linux/macOS targets Rust offers async I/O via `tokio` — an ergonomic, high-performance
alternative to raw signal handlers:

```rust
// posix_uart_async.rs – async UART RX/TX using tokio-serial
//
// Cargo.toml:
//   tokio       = { version = "1", features = ["full"] }
//   tokio-serial = "5"
//   bytes        = "1"

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_serial::SerialPortBuilderExt;
use std::time::Duration;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let port_name = "/dev/ttyUSB0";
    let baud_rate = 115_200;

    let mut port = tokio_serial::new(port_name, baud_rate)
        .timeout(Duration::from_millis(100))
        .open_native_async()?;

    println!("Opened {} at {} baud", port_name, baud_rate);

    // Spawn a dedicated RX task – equivalent to the RX ISR in bare-metal
    let (mut reader, mut writer) = tokio::io::split(port);

    let rx_task = tokio::spawn(async move {
        let mut buf = [0u8; 256];
        loop {
            match reader.read(&mut buf).await {
                Ok(0) => break,   // EOF
                Ok(n) => {
                    // Process received bytes (here: just print hex)
                    print!("RX [{}]: ", n);
                    for b in &buf[..n] { print!("{:02X} ", b); }
                    println!();
                }
                Err(e) => {
                    eprintln!("RX error: {}", e);
                    break;
                }
            }
        }
    });

    // TX: send a greeting, then echo stdin
    writer.write_all(b"Hello from Rust async UART\r\n").await?;

    let mut stdin = tokio::io::stdin();
    let mut line  = [0u8; 128];
    loop {
        let n = stdin.read(&mut line).await?;
        if n == 0 { break; }
        writer.write_all(&line[..n]).await?;
    }

    rx_task.await?;
    Ok(())
}
```

---

### Rust: Compile-time enforcement of ISR constraints

Rust's type system can enforce correct ISR discipline that C/C++ can only document:

```rust
// isr_safety.rs – type-level ISR safety patterns

use core::cell::Cell;
use core::marker::PhantomData;

/// Marker type: may only be created inside a critical section (interrupts disabled).
pub struct CriticalSectionToken(PhantomData<*const ()>);   // Not Send + not Sync

/// A value guarded by a critical section.
/// Like a Mutex<T> but for single-core bare-metal: access requires disabling IRQs.
pub struct CsCell<T>(Cell<T>);

unsafe impl<T> Sync for CsCell<T> where T: Copy {}

impl<T: Copy> CsCell<T> {
    pub const fn new(val: T) -> Self { Self(Cell::new(val)) }

    /// Read value – only callable with interrupts disabled (token proves it).
    pub fn get(&self, _token: &CriticalSectionToken) -> T {
        self.0.get()
    }

    /// Write value – only callable with interrupts disabled.
    pub fn set(&self, _token: &CriticalSectionToken, val: T) {
        self.0.set(val)
    }
}

/// Execute a closure with interrupts disabled, passing a proof token.
/// On Cortex-M this maps directly to PRIMASK manipulation.
pub fn with_cs<R>(f: impl FnOnce(&CriticalSectionToken) -> R) -> R {
    cortex_m::interrupt::free(|_| {
        // SAFETY: We just disabled interrupts; the token cannot escape this closure.
        f(unsafe { &CriticalSectionToken(PhantomData) })
    })
}

// Usage: the compiler *prevents* accessing shared state without a CS token.
static SHARED_STATE: CsCell<u32> = CsCell::new(0);

fn update_from_app() {
    with_cs(|cs| {
        let val = SHARED_STATE.get(cs);
        SHARED_STATE.set(cs, val + 1);
    });
}
```

---

## Summary

| Topic | Key Points |
|---|---|
| **Why ISRs?** | Eliminate polling waste; react to UART events with hardware-driven timing |
| **RX ISR** | Read SR → read DR → push byte to ring buffer; DR read clears flags on most UARTs |
| **TX ISR** | Pop from ring buffer → write DR; disable TXE interrupt when buffer is empty |
| **Ring buffer** | Power-of-two size; SPSC lock-free; head/tail with acquire/release ordering |
| **Error handling** | Check SR *before* reading DR; maintain counters for OE, FE, PE |
| **Latency** | Use SRAM placement, FIFO mode, `always_inline`, and power-of-two masks |
| **C/C++** | `volatile` + compiler barriers for shared data; `__attribute__((section))` for SRAM ISR |
| **Rust** | `AtomicUsize` with `Ordering::Acquire/Release`; `#[interrupt]` attribute; `unsafe` clearly scoped |
| **POSIX Linux** | `SIGIO` (C) or `tokio-serial` (Rust) give ISR-equivalent async notification |
| **Golden rules** | No `malloc`, no blocking, no floating-point (unless saved), minimal instruction count inside ISR |

> **Rule of thumb:** If the ISR takes longer than one UART frame period to complete, increase the
> FIFO threshold or switch to DMA. At 115200 baud one frame is ≈ 86 µs; at 3 Mbit/s it is ≈ 3 µs —
> leaving very little budget for instruction execution inside the ISR.