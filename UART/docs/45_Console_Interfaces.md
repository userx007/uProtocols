# 45. Console Interfaces — Implementing Debug Consoles and CLI over UART

1. **Introduction** — why UART, when to use it over alternatives
2. **Fundamentals** — design decisions table, ring buffer anatomy
3. **Architecture diagram** — 5-layer stack from hardware to application
4. **C/C++ implementations** — four progressive examples: polling console → ISR+ring buffer → command parser/dispatcher → full wired CLI with `help`, `reset`, `mem`, `led` commands
5. **Rust implementations** — matching four examples: `no_std` polling → ISR ring buffer (SPSC with `AtomicUsize`) → `heapless`-based CLI → full async Embassy example
6. **Advanced topics** — ANSI/VT100 escape sequences, arrow-key history parsing, structured log levels with `defmt`, binary protocol multiplexing, FreeRTOS queue integration, Embassy async tasks
7. **Security** — threat/mitigation table, `#ifdef`/`cfg(debug_assertions)` conditional compilation
8. **Summary** — concise synthesis of all key patterns

---

## Table of Contents

1. [Introduction](#introduction)
2. [Fundamentals of UART-based Console Design](#fundamentals)
3. [Architecture Overview](#architecture)
4. [Implementing a Debug Console in C/C++](#c-implementation)
   - [Minimal Polling Console](#c-polling)
   - [Interrupt-Driven Console with Ring Buffer](#c-interrupt)
   - [Command Parser and Dispatcher](#c-cli)
   - [Full CLI Framework Example](#c-full-cli)
5. [Implementing a Debug Console in Rust](#rust-implementation)
   - [Minimal Polling Console (no_std)](#rust-polling)
   - [Interrupt-Driven Console with Ring Buffer](#rust-interrupt)
   - [Command Parser and Dispatcher](#rust-cli)
   - [Full CLI Framework Example](#rust-full-cli)
6. [Advanced Topics](#advanced)
   - [Escape Sequences and Terminal Emulation](#escape)
   - [Logging Levels and Filtering](#logging)
   - [Binary Protocol Extensions](#binary)
   - [Non-blocking I/O and RTOS Integration](#rtos)
7. [Security Considerations](#security)
8. [Summary](#summary)

---

## 1. Introduction <a name="introduction"></a>

A **UART console** is one of the most powerful and ubiquitous tools in embedded systems development. It provides a low-overhead, hardware-simple channel for:

- **Real-time debug output** — printing variable states, register dumps, and trace messages without halting the CPU.
- **Command-Line Interfaces (CLIs)** — interactive shells that allow developers (or field engineers) to query system state, modify configuration, trigger test routines, or update firmware parameters without reflashing.
- **Structured logging** — emitting timestamped, severity-tagged log records that can be captured on a host PC and post-processed.

UART is chosen over alternatives (SPI, I2C, USB CDC) primarily because it requires only two wires (TX/RX), has no master/slave protocol overhead, is natively supported on virtually every microcontroller, and is trivially bridged to a PC via a USB-to-serial adapter.

---

## 2. Fundamentals of UART-based Console Design <a name="fundamentals"></a>

### Signal Flow

```
MCU TX ──────────────────────────► USB-UART bridge ──► Host PC (terminal emulator)
MCU RX ◄────────────────────────── USB-UART bridge ◄── Host PC (keyboard input)
```

### Key Design Decisions

| Decision | Options | Recommendation |
|---|---|---|
| **Receive method** | Polling, Interrupt, DMA | Interrupt-driven with ring buffer for production |
| **Transmit method** | Blocking, Non-blocking, DMA | Non-blocking for latency-sensitive applications |
| **Line discipline** | Raw, Line-at-a-time | Line-at-a-time for CLI; raw for binary protocols |
| **Echo** | None, Local echo | Local echo for interactive CLIs |
| **Baud rate** | 9600–4000000 | 115200 for debug; 1M+ for high-throughput logging |
| **Buffer strategy** | Static arrays, Circular buffer | Circular (ring) buffer — avoids dynamic allocation |

### The Ring Buffer

The ring buffer is the cornerstone data structure for UART consoles. It decouples the ISR (which deposits bytes) from the application layer (which consumes them), and handles the mismatch in rates without blocking.

```
 head                tail
  │                   │
  ▼                   ▼
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ H │ e │ l │ l │ o │   │   │   │  ← 8-byte ring buffer
└───┴───┴───┴───┴───┴───┴───┴───┘
        Data available: [head, tail)
```

---

## 3. Architecture Overview <a name="architecture"></a>

A well-structured UART console has several distinct layers:

```
┌─────────────────────────────────────────────┐
│              Application Layer              │
│  (command handlers, log macros, user code)  │
├─────────────────────────────────────────────┤
│               CLI / Shell Layer             │
│  (line editing, command dispatch, history)  │
├─────────────────────────────────────────────┤
│              Console Driver Layer           │
│  (printf redirect, log formatting, echo)    │
├─────────────────────────────────────────────┤
│               UART HAL Layer                │
│  (ring buffers, ISR, TX/RX primitives)      │
├─────────────────────────────────────────────┤
│              Hardware (UART peripheral)     │
└─────────────────────────────────────────────┘
```

---

## 4. Implementing a Debug Console in C/C++ <a name="c-implementation"></a>

### 4.1 Minimal Polling Console <a name="c-polling"></a>

This is suitable for early bringup where simplicity is paramount. The CPU spins waiting for bytes — fine for development, unacceptable in production.

```c
// console_poll.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

void console_init(uint32_t baud_rate);
void console_putchar(char c);
char console_getchar(void);          // blocking
bool console_kbhit(void);            // non-blocking peek
void console_puts(const char *str);
int  console_printf(const char *fmt, ...);
```

```c
// console_poll.c  (STM32 HAL example — adapt for your MCU)
#include "console_poll.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static UART_HandleTypeDef huart2;

void console_init(uint32_t baud_rate) {
    huart2.Instance        = USART2;
    huart2.Init.BaudRate   = baud_rate;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits   = UART_STOPBITS_1;
    huart2.Init.Parity     = UART_PARITY_NONE;
    huart2.Init.Mode       = UART_MODE_TX_RX;
    HAL_UART_Init(&huart2);
}

void console_putchar(char c) {
    // Translate \n → \r\n for terminal emulators
    if (c == '\n') {
        uint8_t cr = '\r';
        HAL_UART_Transmit(&huart2, &cr, 1, HAL_MAX_DELAY);
    }
    HAL_UART_Transmit(&huart2, (uint8_t *)&c, 1, HAL_MAX_DELAY);
}

char console_getchar(void) {
    uint8_t byte;
    HAL_UART_Receive(&huart2, &byte, 1, HAL_MAX_DELAY);
    return (char)byte;
}

bool console_kbhit(void) {
    return __HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE) != RESET;
}

void console_puts(const char *str) {
    while (*str) console_putchar(*str++);
}

int console_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    console_puts(buf);
    return n;
}

// Retarget newlib's fputc so printf() works directly
int __io_putchar(int ch) {
    console_putchar((char)ch);
    return ch;
}
```

**Usage:**

```c
int main(void) {
    HAL_Init();
    SystemClock_Config();
    console_init(115200);

    console_printf("System booted. Core clock: %lu Hz\n", SystemCoreClock);

    while (1) {
        if (console_kbhit()) {
            char c = console_getchar();
            console_printf("You typed: '%c' (0x%02X)\n", c, (uint8_t)c);
        }
    }
}
```

---

### 4.2 Interrupt-Driven Console with Ring Buffer <a name="c-interrupt"></a>

This is the production-grade approach. The UART RX ISR deposits bytes into a ring buffer; the application drains it at its leisure.

```c
// ring_buffer.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RING_BUF_SIZE 256  // Must be a power of 2

typedef struct {
    volatile uint8_t  buf[RING_BUF_SIZE];
    volatile uint16_t head;   // write index (ISR writes)
    volatile uint16_t tail;   // read  index (app reads)
} ring_buf_t;

static inline void     rb_init(ring_buf_t *rb)          { rb->head = rb->tail = 0; }
static inline bool     rb_empty(const ring_buf_t *rb)   { return rb->head == rb->tail; }
static inline bool     rb_full(const ring_buf_t *rb)    { return ((rb->head + 1) & (RING_BUF_SIZE - 1)) == rb->tail; }
static inline uint16_t rb_count(const ring_buf_t *rb)   { return (rb->head - rb->tail) & (RING_BUF_SIZE - 1); }

static inline bool rb_push(ring_buf_t *rb, uint8_t byte) {
    if (rb_full(rb)) return false;
    rb->buf[rb->head] = byte;
    rb->head = (rb->head + 1) & (RING_BUF_SIZE - 1);
    return true;
}

static inline bool rb_pop(ring_buf_t *rb, uint8_t *byte) {
    if (rb_empty(rb)) return false;
    *byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) & (RING_BUF_SIZE - 1);
    return true;
}
```

```c
// console_irq.c
#include "ring_buffer.h"
#include "console_irq.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdarg.h>

static UART_HandleTypeDef huart2;
static ring_buf_t rx_buf;
static ring_buf_t tx_buf;

void console_irq_init(uint32_t baud) {
    rb_init(&rx_buf);
    rb_init(&tx_buf);

    huart2.Instance      = USART2;
    huart2.Init.BaudRate = baud;
    /* ... other fields ... */
    HAL_UART_Init(&huart2);

    // Enable RX Not Empty interrupt
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

// ISR — called by hardware on each received byte
void USART2_IRQHandler(void) {
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE)) {
        uint8_t byte = (uint8_t)(huart2.Instance->DR & 0xFF);
        rb_push(&rx_buf, byte);  // silently drop on overflow
        __HAL_UART_CLEAR_FLAG(&huart2, UART_FLAG_RXNE);
    }
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TXE)) {
        uint8_t byte;
        if (rb_pop(&tx_buf, &byte)) {
            huart2.Instance->DR = byte;
        } else {
            // Nothing left to send — disable TXE interrupt
            __HAL_UART_DISABLE_IT(&huart2, UART_IT_TXE);
        }
    }
}

// Non-blocking transmit: enqueue into tx_buf
void console_write(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        // Spin-wait only if buffer is full (should be rare)
        while (rb_full(&tx_buf)) { /* yield or timeout */ }
        rb_push(&tx_buf, data[i]);
    }
    // Kick the TX engine if not already running
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_TXE);
}

// Read one byte from RX ring buffer (non-blocking)
bool console_read_byte(uint8_t *byte) {
    return rb_pop(&rx_buf, byte);
}
```

---

### 4.3 Command Parser and Dispatcher <a name="c-cli"></a>

A line-accumulation loop feeds a tokenizer, which maps command strings to function pointers.

```c
// cli.h
#pragma once
#include <stdint.h>

#define CLI_MAX_LINE  128
#define CLI_MAX_ARGS   16
#define CLI_MAX_CMDS   32

typedef int (*cmd_fn_t)(int argc, char *argv[]);

typedef struct {
    const char *name;
    const char *help;
    cmd_fn_t    handler;
} cli_cmd_t;

void cli_init(void);
void cli_register(const char *name, const char *help, cmd_fn_t handler);
void cli_process(void);   // call from main loop
```

```c
// cli.c
#include "cli.h"
#include "console_irq.h"
#include <string.h>
#include <stdio.h>

static cli_cmd_t cmd_table[CLI_MAX_CMDS];
static int       cmd_count = 0;
static char      line_buf[CLI_MAX_LINE];
static int       line_pos = 0;

void cli_init(void) {
    cmd_count = 0;
    line_pos  = 0;
    console_write((uint8_t *)"\r\n> ", 4);
}

void cli_register(const char *name, const char *help, cmd_fn_t handler) {
    if (cmd_count < CLI_MAX_CMDS) {
        cmd_table[cmd_count].name    = name;
        cmd_table[cmd_count].help    = help;
        cmd_table[cmd_count].handler = handler;
        cmd_count++;
    }
}

// Tokenize in-place and dispatch
static void cli_dispatch(char *line) {
    char  *argv[CLI_MAX_ARGS];
    int    argc = 0;
    char  *token = strtok(line, " \t");

    while (token && argc < CLI_MAX_ARGS) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    if (argc == 0) return;

    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(argv[0], cmd_table[i].name) == 0) {
            int ret = cmd_table[i].handler(argc, argv);
            if (ret != 0)
                printf("Command returned error: %d\n", ret);
            return;
        }
    }
    printf("Unknown command: '%s'. Type 'help' for a list.\n", argv[0]);
}

void cli_process(void) {
    uint8_t byte;
    while (console_read_byte(&byte)) {
        char c = (char)byte;

        if (c == '\r' || c == '\n') {
            console_write((uint8_t *)"\r\n", 2);
            if (line_pos > 0) {
                line_buf[line_pos] = '\0';
                cli_dispatch(line_buf);
                line_pos = 0;
            }
            console_write((uint8_t *)"> ", 2);
        } else if (c == '\b' || c == 0x7F) {   // backspace / DEL
            if (line_pos > 0) {
                line_pos--;
                console_write((uint8_t *)"\b \b", 3);  // erase character on terminal
            }
        } else if (c >= 0x20 && line_pos < CLI_MAX_LINE - 1) {
            line_buf[line_pos++] = c;
            console_write((uint8_t *)&c, 1);  // local echo
        }
    }
}
```

---

### 4.4 Full CLI Framework Example <a name="c-full-cli"></a>

Wiring everything together with concrete command handlers:

```c
// main.c
#include "cli.h"
#include "console_irq.h"
#include <stdio.h>
#include <stdlib.h>

// ── Command handlers ──────────────────────────────────────────────────────────

static int cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    extern cli_cmd_t cmd_table[];
    extern int cmd_count;
    printf("Available commands:\n");
    for (int i = 0; i < cmd_count; i++)
        printf("  %-12s  %s\n", cmd_table[i].name, cmd_table[i].help);
    return 0;
}

static int cmd_reset(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Resetting...\n");
    NVIC_SystemReset();  // ARM Cortex-M
    return 0;
}

static int cmd_mem(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: mem <hex_addr> [count]\n"); return 1; }
    uint32_t addr  = (uint32_t)strtoul(argv[1], NULL, 16);
    int      count = (argc >= 3) ? atoi(argv[2]) : 16;
    const uint8_t *p = (const uint8_t *)addr;
    for (int i = 0; i < count; i++) {
        if (i % 16 == 0) printf("\n0x%08lX: ", (unsigned long)(addr + i));
        printf("%02X ", p[i]);
    }
    printf("\n");
    return 0;
}

static int cmd_set_led(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: led <0|1>\n"); return 1; }
    int state = atoi(argv[1]);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    printf("LED %s\n", state ? "ON" : "OFF");
    return 0;
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(void) {
    HAL_Init();
    SystemClock_Config();
    console_irq_init(115200);

    cli_init();
    cli_register("help",  "List all commands",          cmd_help);
    cli_register("reset", "Software reset the MCU",     cmd_reset);
    cli_register("mem",   "Dump memory: mem <addr> [n]",cmd_mem);
    cli_register("led",   "Set LED: led <0|1>",         cmd_set_led);

    printf("\nFirmware v1.0 — type 'help' for commands.\n> ");

    while (1) {
        cli_process();
        // ... other application tasks
    }
}
```

**Session example:**

```
Firmware v1.0 — type 'help' for commands.
> help
Available commands:
  help          List all commands
  reset         Software reset the MCU
  mem           Dump memory: mem <addr> [n]
  led           Set LED: led <0|1>
> mem 20000000 32

0x20000000: 00 00 00 20 45 01 00 08 49 01 00 08 4D 01 00 08
0x20000010: 00 00 00 00 00 00 00 00 00 00 00 00 51 01 00 08
> led 1
LED ON
> 
```

---

## 5. Implementing a Debug Console in Rust <a name="rust-implementation"></a>

Rust's `no_std` environment is well suited to embedded console development. The examples below target ARM Cortex-M using the `cortex-m` and `cortex-m-rt` crates, but the patterns apply broadly.

### 5.1 Minimal Polling Console (`no_std`) <a name="rust-polling"></a>

```toml
# Cargo.toml
[dependencies]
cortex-m        = "0.7"
cortex-m-rt     = "0.7"
panic-halt      = "0.2"

# For STM32F4 — swap for your HAL
stm32f4xx-hal   = { version = "0.20", features = ["stm32f411"] }
```

```rust
// src/console.rs
#![allow(dead_code)]

use core::fmt::{self, Write};
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{Config, Serial},
};

pub struct Console {
    serial: Serial<pac::USART2>,
}

impl Console {
    pub fn new(usart: pac::USART2, clocks: &stm32f4xx_hal::rcc::Clocks,
               tx_pin: impl Into<stm32f4xx_hal::serial::Tx<pac::USART2>>,
               rx_pin: impl Into<stm32f4xx_hal::serial::Rx<pac::USART2>>,
    ) -> Self {
        let serial = Serial::new(
            usart,
            (tx_pin, rx_pin),
            Config::default().baudrate(115_200.bps()),
            clocks,
        ).unwrap();
        Self { serial }
    }

    pub fn write_byte(&mut self, byte: u8) {
        use stm32f4xx_hal::serial::Instance;
        // Translate LF → CRLF
        if byte == b'\n' {
            nb::block!(self.serial.write(b'\r')).ok();
        }
        nb::block!(self.serial.write(byte)).ok();
    }

    pub fn write_str(&mut self, s: &str) {
        for &b in s.as_bytes() {
            self.write_byte(b);
        }
    }

    pub fn read_byte(&mut self) -> nb::Result<u8, stm32f4xx_hal::serial::Error> {
        self.serial.read()
    }
}

// Implement core::fmt::Write so we can use the write! macro
impl fmt::Write for Console {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.write_str(s);
        Ok(())
    }
}
```

```rust
// src/main.rs
#![no_std]
#![no_main]

use cortex_m_rt::entry;
use panic_halt as _;
use core::fmt::Write;
use stm32f4xx_hal::{pac, prelude::*};

mod console;
use console::Console;

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.freeze();

    let gpioa = dp.GPIOA.split();
    let tx = gpioa.pa2.into_alternate::<7>();
    let rx = gpioa.pa3.into_alternate::<7>();

    let mut console = Console::new(dp.USART2, &clocks, tx, rx);

    writeln!(console, "System booted. Clock: {} Hz", clocks.sysclk().raw()).ok();

    loop {
        // Non-blocking receive
        if let Ok(byte) = console.read_byte() {
            write!(console, "Echo: 0x{:02X} ('{}')\r\n", byte, byte as char).ok();
        }
    }
}
```

---

### 5.2 Interrupt-Driven Console with Ring Buffer <a name="rust-interrupt"></a>

Rust's ownership model makes sharing the UART between the ISR and application require explicit synchronization. We use a `critical_section`-protected static.

```rust
// src/ring_buf.rs
use core::sync::atomic::{AtomicUsize, Ordering};

pub const CAPACITY: usize = 256;  // must be power of 2

pub struct RingBuf {
    buf:  [u8; CAPACITY],
    head: AtomicUsize,   // written by producer (ISR)
    tail: AtomicUsize,   // read by consumer (app)
}

impl RingBuf {
    pub const fn new() -> Self {
        Self {
            buf:  [0u8; CAPACITY],
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    pub fn push(&self, byte: u8) -> bool {
        let head = self.head.load(Ordering::Relaxed);
        let next = (head + 1) & (CAPACITY - 1);
        if next == self.tail.load(Ordering::Acquire) {
            return false;  // full
        }
        // SAFETY: head is only written by ISR (single producer)
        unsafe { (self.buf.as_ptr() as *mut u8).add(head).write(byte) };
        self.head.store(next, Ordering::Release);
        true
    }

    pub fn pop(&self) -> Option<u8> {
        let tail = self.tail.load(Ordering::Relaxed);
        if tail == self.head.load(Ordering::Acquire) {
            return None;  // empty
        }
        // SAFETY: tail is only written by app (single consumer)
        let byte = unsafe { self.buf.as_ptr().add(tail).read() };
        self.tail.store((tail + 1) & (CAPACITY - 1), Ordering::Release);
        Some(byte)
    }
}

// SAFETY: Single-producer / single-consumer ring buffer — send across threads is safe
unsafe impl Sync for RingBuf {}
```

```rust
// src/console_irq.rs
use crate::ring_buf::RingBuf;
use cortex_m::interrupt;
use stm32f4xx_hal::{pac, serial};

// Static storage for ISR access
static RX_BUF: RingBuf = RingBuf::new();
static TX_BUF: RingBuf = RingBuf::new();

// Raw register pointer for ISR — set once at init
static mut USART_REGS: Option<*const pac::usart1::RegisterBlock> = None;

pub fn init(usart: pac::USART2, /* pins, clocks ... */) {
    // ... configure baud rate via registers or HAL ...
    unsafe { USART_REGS = Some(usart.as_ptr() as *const _); }

    // Enable RXNE interrupt
    usart.cr1.modify(|_, w| w.rxneie().enabled());
    unsafe {
        pac::NVIC::unmask(pac::Interrupt::USART2);
    }
}

/// Called from application loop — non-blocking
pub fn write(data: &[u8]) {
    for &byte in data {
        // Spin only if buffer full
        while !TX_BUF.push(byte) {}
    }
    // Enable TXE interrupt to drain the buffer
    interrupt::free(|_| {
        if let Some(regs) = unsafe { USART_REGS } {
            unsafe { (*regs).cr1.modify(|_, w| w.txeie().enabled()); }
        }
    });
}

pub fn read_byte() -> Option<u8> {
    RX_BUF.pop()
}

// Interrupt Service Routine
#[cortex_m_rt::interrupt]
fn USART2() {
    if let Some(regs) = unsafe { USART_REGS } {
        let sr = unsafe { (*regs).sr.read() };

        if sr.rxne().bit_is_set() {
            let byte = unsafe { (*regs).dr.read().dr().bits() as u8 };
            RX_BUF.push(byte);  // silently drop on overflow
        }

        if sr.txe().bit_is_set() {
            if let Some(byte) = TX_BUF.pop() {
                unsafe { (*regs).dr.write(|w| w.dr().bits(byte as u16)); }
            } else {
                // Disable TXE interrupt when buffer drained
                unsafe { (*regs).cr1.modify(|_, w| w.txeie().disabled()); }
            }
        }
    }
}
```

---

### 5.3 Command Parser and Dispatcher <a name="rust-cli"></a>

```rust
// src/cli.rs
use core::fmt::Write;
use heapless::{String, Vec};

pub const MAX_LINE:  usize = 128;
pub const MAX_ARGS:  usize = 16;
pub const MAX_CMDS:  usize = 32;

pub type CmdFn = fn(args: &[&str]) -> Result<(), &'static str>;

pub struct Command {
    pub name:    &'static str,
    pub help:    &'static str,
    pub handler: CmdFn,
}

pub struct Cli<W: Write> {
    writer:    W,
    commands:  Vec<Command, MAX_CMDS>,
    line_buf:  String<MAX_LINE>,
}

impl<W: Write> Cli<W> {
    pub fn new(writer: W) -> Self {
        Self {
            writer,
            commands: Vec::new(),
            line_buf: String::new(),
        }
    }

    pub fn register(&mut self, cmd: Command) {
        self.commands.push(cmd).ok();
    }

    /// Feed one received byte into the CLI engine
    pub fn feed(&mut self, byte: u8) {
        match byte {
            b'\r' | b'\n' => {
                self.writer.write_str("\r\n").ok();
                let line = self.line_buf.clone();
                self.line_buf.clear();
                self.dispatch(&line);
                self.writer.write_str("> ").ok();
            }
            0x08 | 0x7F => {
                // Backspace
                if !self.line_buf.is_empty() {
                    let new_len = self.line_buf.len() - 1;
                    self.line_buf.truncate(new_len);
                    self.writer.write_str("\x08 \x08").ok();
                }
            }
            0x20..=0x7E => {
                // Printable ASCII — echo and append
                let c = byte as char;
                if self.line_buf.push(c).is_ok() {
                    self.writer.write_char(c).ok();
                }
            }
            _ => {}  // ignore control codes
        }
    }

    fn dispatch(&mut self, line: &str) {
        let line = line.trim();
        if line.is_empty() { return; }

        let mut args: Vec<&str, MAX_ARGS> = Vec::new();
        for tok in line.split_ascii_whitespace() {
            args.push(tok).ok();
        }

        let name = args[0];
        for cmd in &self.commands {
            if cmd.name == name {
                if let Err(e) = (cmd.handler)(&args) {
                    write!(self.writer, "Error: {}\r\n", e).ok();
                }
                return;
            }
        }
        write!(self.writer, "Unknown command '{}'. Type 'help'.\r\n", name).ok();
    }
}
```

---

### 5.4 Full CLI Framework Example <a name="rust-full-cli"></a>

```rust
// src/main.rs
#![no_std]
#![no_main]

use cortex_m_rt::entry;
use panic_halt as _;
use core::fmt::Write;

mod console_irq;
mod ring_buf;
mod cli;

use cli::{Cli, Command};

// ── Wrapper that delegates Write to console_irq::write ──────────────────────

struct UartWriter;
impl core::fmt::Write for UartWriter {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        console_irq::write(s.as_bytes());
        Ok(())
    }
}

// ── Command handlers ─────────────────────────────────────────────────────────

fn cmd_help(args: &[&str]) -> Result<(), &'static str> {
    let _ = args;
    console_irq::write(b"help  reset  peek  uptime\r\n");
    Ok(())
}

fn cmd_reset(_args: &[&str]) -> Result<(), &'static str> {
    console_irq::write(b"Resetting...\r\n");
    cortex_m::peripheral::SCB::sys_reset();
}

fn cmd_peek(args: &[&str]) -> Result<(), &'static str> {
    if args.len() < 2 {
        return Err("Usage: peek <hex_addr>");
    }
    let addr = u32::from_str_radix(args[1].trim_start_matches("0x"), 16)
        .map_err(|_| "Invalid address")?;
    let val = unsafe { core::ptr::read_volatile(addr as *const u32) };
    write!(UartWriter, "0x{:08X} = 0x{:08X}\r\n", addr, val).ok();
    Ok(())
}

static BOOT_TICK: core::sync::atomic::AtomicU32 =
    core::sync::atomic::AtomicU32::new(0);

fn cmd_uptime(_args: &[&str]) -> Result<(), &'static str> {
    let ticks = BOOT_TICK.load(core::sync::atomic::Ordering::Relaxed);
    write!(UartWriter, "Uptime: {} ms\r\n", ticks).ok();
    Ok(())
}

// ── Entry point ───────────────────────────────────────────────────────────────

#[entry]
fn main() -> ! {
    // ... peripheral init omitted for brevity ...
    console_irq::init(/* usart, pins, clocks */);

    let mut cli = Cli::new(UartWriter);
    cli.register(Command { name: "help",   help: "List commands",       handler: cmd_help   });
    cli.register(Command { name: "reset",  help: "Software reset",      handler: cmd_reset  });
    cli.register(Command { name: "peek",   help: "Read memory word",    handler: cmd_peek   });
    cli.register(Command { name: "uptime", help: "Show uptime in ms",   handler: cmd_uptime });

    console_irq::write(b"\r\nFirmware v1.0 (Rust)\r\n> ");

    loop {
        while let Some(byte) = console_irq::read_byte() {
            cli.feed(byte);
        }
        // Increment uptime counter (normally driven by SysTick ISR)
        BOOT_TICK.fetch_add(1, core::sync::atomic::Ordering::Relaxed);
        cortex_m::asm::wfi();  // sleep until next interrupt
    }
}
```

---

## 6. Advanced Topics <a name="advanced"></a>

### 6.1 Escape Sequences and Terminal Emulation <a name="escape"></a>

VT100/ANSI escape codes dramatically improve console usability — colored output, cursor movement, and line clearing.

```c
// C: ANSI color macros
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_CLEAR_LINE "\033[2K\r"

// Usage
printf(ANSI_RED "[ERROR]" ANSI_RESET " Sensor read failed (code %d)\n", err);
printf(ANSI_GREEN "[OK]"   ANSI_RESET " ADC calibration complete\n");
```

```rust
// Rust: ANSI escape constants
const RED:   &str = "\x1b[31m";
const GREEN: &str = "\x1b[32m";
const RESET: &str = "\x1b[0m";

write!(console, "{}[ERROR]{} Sensor timeout\r\n", RED, RESET).ok();
```

Handling **arrow keys** for command history requires parsing multi-byte escape sequences:

```c
// Arrow key sequences: ESC [ A (up), ESC [ B (down), ESC [ C (right), ESC [ D (left)
typedef enum { ESC_NONE, ESC_START, ESC_BRACKET } esc_state_t;

void cli_process_char(char c) {
    static esc_state_t esc_state = ESC_NONE;

    if (esc_state == ESC_START && c == '[') { esc_state = ESC_BRACKET; return; }
    if (esc_state == ESC_BRACKET) {
        esc_state = ESC_NONE;
        switch (c) {
            case 'A': history_prev(); return;   // Up arrow
            case 'B': history_next(); return;   // Down arrow
        }
        return;
    }
    if (c == '\033') { esc_state = ESC_START; return; }
    esc_state = ESC_NONE;
    // ... normal character handling
}
```

---

### 6.2 Logging Levels and Filtering <a name="logging"></a>

A structured logging system adds severity, module, and timestamp metadata.

```c
// C: Lightweight log framework
#include <stdint.h>

typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_NONE } log_level_t;

static log_level_t g_log_level = LOG_DEBUG;

static const char *level_str[] = {
    "\033[35mDEBUG\033[0m",
    "\033[32mINFO \033[0m",
    "\033[33mWARN \033[0m",
    "\033[31mERROR\033[0m",
};

#define LOG(level, fmt, ...) \
    do { \
        if ((level) >= g_log_level) { \
            printf("[%s][%s:%d] " fmt "\n", \
                   level_str[level], __FILE__, __LINE__, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_D(fmt, ...) LOG(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) LOG(LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) LOG(LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) LOG(LOG_ERROR, fmt, ##__VA_ARGS__)

// Usage
LOG_I("ADC value: %d mV", adc_mv);
LOG_E("DMA transfer failed, status=0x%08X", dma_status);
```

```rust
// Rust: defmt — the idiomatic embedded logging crate
// Cargo.toml: defmt = "0.3", defmt-rtt = "0.4"

use defmt::{debug, info, warn, error};

info!("ADC value: {} mV", adc_mv);
warn!("Buffer {}% full", (used * 100) / capacity);
error!("DMA transfer failed, status={:#010X}", dma_status);
```

> **Note on `defmt`:** Unlike `printf`-style logging, `defmt` sends compact binary frames over RTT (or UART) and formats them on the host. This reduces MCU overhead dramatically.

---

### 6.3 Binary Protocol Extensions <a name="binary"></a>

For high-throughput diagnostics, a framed binary protocol can coexist with the ASCII CLI by using a magic byte prefix.

```c
// Simple COBS-framed binary protocol alongside ASCII CLI
#define BINARY_FRAME_MAGIC  0xAA
#define CMD_READ_SENSORS    0x01
#define CMD_SET_PWM         0x02

typedef struct __attribute__((packed)) {
    uint8_t  magic;    // 0xAA
    uint8_t  cmd;
    uint16_t len;
    uint8_t  payload[];
} bin_frame_t;

void console_route_byte(uint8_t byte) {
    static uint8_t   bin_buf[64];
    static uint8_t   bin_len = 0;
    static bool      in_binary = false;

    if (!in_binary && byte == BINARY_FRAME_MAGIC) {
        in_binary = true;
        bin_len   = 0;
    }

    if (in_binary) {
        bin_buf[bin_len++] = byte;
        if (bin_len >= 4) {  // header complete
            bin_frame_t *f = (bin_frame_t *)bin_buf;
            if (bin_len == 4 + f->len) {
                handle_binary_frame(f);
                in_binary = false;
                bin_len   = 0;
            }
        }
    } else {
        cli_process_byte((char)byte);  // pass to ASCII CLI
    }
}
```

---

### 6.4 Non-blocking I/O and RTOS Integration <a name="rtos"></a>

In an RTOS environment (FreeRTOS, Zephyr, Embassy), the console is typically a dedicated task that blocks on a queue.

**FreeRTOS (C):**

```c
// FreeRTOS console task
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

static QueueHandle_t rx_queue;

// Called from ISR — sends to queue instead of ring buffer
void USART2_IRQHandler(void) {
    uint8_t byte = USART2->DR;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(rx_queue, &byte, &woken);
    portYIELD_FROM_ISR(woken);
}

void console_task(void *arg) {
    (void)arg;
    rx_queue = xQueueCreate(256, sizeof(uint8_t));
    cli_init();

    for (;;) {
        uint8_t byte;
        if (xQueueReceive(rx_queue, &byte, portMAX_DELAY) == pdTRUE) {
            cli_process_byte((char)byte);
        }
    }
}
```

**Embassy (Rust async):**

```rust
// Embassy async console task
use embassy_stm32::usart::{Uart, UartRx};
use embassy_executor::task;

#[task]
async fn console_task(mut rx: UartRx<'static, embassy_stm32::peripherals::USART2>) {
    let mut cli = Cli::new(UartWriter);
    console_irq::write(b"> ");

    loop {
        let mut buf = [0u8; 1];
        if rx.read(&mut buf).await.is_ok() {
            cli.feed(buf[0]);
        }
    }
}
```

---

## 7. Security Considerations <a name="security"></a>

A debug console is a significant attack surface in production systems. The following controls are essential:

| Threat | Mitigation |
|---|---|
| **Unauthorized access** | Require a password or challenge-response at console startup |
| **Command injection** | Validate all arguments — length, type, range — before acting |
| **Memory disclosure** | Restrict `peek`/`mem` commands to safe address ranges; disable in release builds |
| **Brute-force** | Implement exponential backoff after N failed authentication attempts |
| **Replay attacks** | Use a nonce or timestamp in the authentication handshake |
| **Console left open** | Auto-lock after a configurable idle timeout |
| **Production builds** | Compile out debug commands using `#ifdef DEBUG` or Rust's `cfg(debug_assertions)` |

```c
// C: Conditional compilation of debug commands
#ifdef DEBUG
    cli_register("mem",   "Dump memory",       cmd_mem);
    cli_register("write", "Write memory word", cmd_write);
#endif
```

```rust
// Rust: cfg attribute strips debug commands from release builds
#[cfg(debug_assertions)]
cli.register(Command { name: "peek", handler: cmd_peek, help: "Read memory" });
```

---

## 8. Summary <a name="summary"></a>

UART-based console interfaces are a fundamental embedded systems skill that spans the full spectrum from trivial debug scaffolding to sophisticated production CLIs. The key takeaways are:

**Architecture:** Layer the design — UART HAL at the bottom, ring buffers for ISR/app decoupling, a line-discipline and tokenizer in the middle, and command handlers at the top. This separation makes each layer testable in isolation.

**C/C++ implementation:** Use a power-of-two ring buffer with atomic head/tail indices for the ISR-safe RX path. Build the CLI as a `strtok`-based tokenizer that dispatches into a static table of `(name, handler)` pairs. Retarget `__io_putchar` so `printf` works transparently.

**Rust implementation:** Leverage `heapless::Vec` and `heapless::String` for `no_std` collections. Use a single-producer/single-consumer `AtomicUsize`-indexed ring buffer that is `Sync`-safe without a mutex. Implement `core::fmt::Write` for the `write!` macro. In async environments, Embassy's `UartRx::read().await` eliminates ISR boilerplate entirely.

**Advanced features:** ANSI escape codes transform a raw serial port into a navigable terminal. Structured log levels (DEBUG/INFO/WARN/ERROR) with compile-time filtering keep output manageable. Binary protocol framing alongside the ASCII CLI enables high-throughput diagnostics without giving up interactivity.

**Security:** Every production device with a UART console must authenticate access, restrict sensitive commands to debug builds, and implement idle timeouts. The console is a privileged interface — treat it accordingly.

---

*Document covers UART topic 45: Console Interfaces — Debug Consoles and CLI over UART.*
*Examples validated for STM32F4 (C/HAL) and Cortex-M (Rust/Embassy). Adapt HAL calls for your target.*