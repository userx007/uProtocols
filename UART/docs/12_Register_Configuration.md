# 12. UART Register Configuration

**Architecture & Register Reference**
- Full 16550 register map with every bit field documented (LCR, DLL/DLM, FCR, IER, IIR, LSR, MCR, MSR, SCR)
- STM32 USART and ARM PL011 register maps as additional platform references
- Baud rate calculation tables and formulas for all three platforms

**C/C++ Examples**
- `uart16550_regs.h` — complete register abstraction layer with all bit constants
- Full 9-step initialisation sequence with correct DLAB handling
- STM32 USART bare-metal init (including the ×25/÷100 fractional BRR trick)
- ARM PL011 bare-metal init (with IBRD/FBRD latching note)
- C++ class for runtime baud/parity reconfiguration
- Register dump / diagnostic utility

**Rust Examples**
- `volatile`-safe `Reg` wrapper using `read_volatile`/`write_volatile`
- Full 16550 driver with `core::fmt::Write` support for `write!` / `writeln!`
- STM32 USART driver with fractional BRR calculation
- PL011 driver with the critical LCR_H write-to-latch behaviour documented
- All three implement `fmt::Write` for no-alloc formatted output

**Pitfalls table** covers the 10 most common register-configuration bugs, each with symptom and fix.

> **Series:** UART Programming Guide  
> **Topic:** Programming UART peripheral registers for initialization and control

---

## Table of Contents

1. [Introduction](#introduction)
2. [UART Register Architecture Overview](#uart-register-architecture-overview)
3. [Core Register Set](#core-register-set)
   - [Line Control Register (LCR)](#line-control-register-lcr)
   - [Divisor Latch Registers (DLL / DLM)](#divisor-latch-registers-dll--dlm)
   - [FIFO Control Register (FCR)](#fifo-control-register-fcr)
   - [Interrupt Enable Register (IER)](#interrupt-enable-register-ier)
   - [Interrupt Identification Register (IIR)](#interrupt-identification-register-iir)
   - [Line Status Register (LSR)](#line-status-register-lsr)
   - [Modem Control Register (MCR)](#modem-control-register-mcr)
   - [Modem Status Register (MSR)](#modem-status-register-msr)
   - [Scratch Register (SCR)](#scratch-register-scr)
4. [Initialization Sequence](#initialization-sequence)
5. [Baud Rate Calculation](#baud-rate-calculation)
6. [Platform Examples](#platform-examples)
   - [16550 UART (PC / Embedded Classic)](#16550-uart-pc--embedded-classic)
   - [STM32 USART](#stm32-usart)
   - [ARM PL011 UART](#arm-pl011-uart)
7. [Code Examples — C/C++](#code-examples--cc)
   - [16550 Register Abstraction Layer](#16550-register-abstraction-layer)
   - [16550 Full Initialization](#16550-full-initialization)
   - [STM32 USART Register Init (bare-metal)](#stm32-usart-register-init-bare-metal)
   - [ARM PL011 Register Init (bare-metal)](#arm-pl011-register-init-bare-metal)
   - [Runtime Reconfiguration](#runtime-reconfiguration)
   - [Register Dump / Debug Utility](#register-dump--debug-utility)
8. [Code Examples — Rust](#code-examples--rust)
   - [Register Block with volatile access](#register-block-with-volatile-access)
   - [16550 Driver in Rust](#16550-driver-in-rust)
   - [STM32 USART Rust Init](#stm32-usart-rust-init)
   - [PL011 Rust Init](#pl011-rust-init)
9. [Common Pitfalls](#common-pitfalls)
10. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) peripherals expose their complete configuration and state through **memory-mapped I/O (MMIO) registers** (or, on legacy x86, port-mapped I/O). Every aspect of UART operation — baud rate, frame format, FIFO depth, interrupt enables, hardware flow control, modem signalling — is controlled by reading and writing these registers.

Understanding register-level programming is essential for:

- **Bare-metal firmware** where no OS driver layer exists
- **Bootloaders** that must initialise a console before the OS loads
- **Custom drivers** in RTOSes or embedded Linux
- **Debugging** hardware or existing drivers at the lowest level
- **Security-sensitive** code that must not trust an untrusted HAL

This document focuses on three widely encountered UART families:

| Family | Typical Use |
|--------|-------------|
| **16550** (and 16C550, 16C750 …) | PC legacy, many SoCs, FPGAs |
| **STM32 USART** | STM32 microcontroller family |
| **ARM PL011** | ARM reference design, Raspberry Pi, QEMU |

The register maps differ in detail but share the same logical structure. The programming patterns transfer directly.

---

## UART Register Architecture Overview

```
Base Address (MMIO or Port)
│
├── Offset 0x00  ─── RHR / THR / DLL    (receive / transmit / divisor low)
├── Offset 0x04  ─── IER / DLM          (interrupt enable / divisor high)
├── Offset 0x08  ─── FCR / IIR          (FIFO control W / identification R)
├── Offset 0x0C  ─── LCR               (line control — format, DLAB)
├── Offset 0x10  ─── MCR               (modem control)
├── Offset 0x14  ─── LSR               (line status — RX ready, TX empty)
├── Offset 0x18  ─── MSR               (modem status)
└── Offset 0x1C  ─── SCR               (scratch / test)
```

> **DLAB (Divisor Latch Access Bit):** Setting bit 7 of LCR maps offset 0x00 / 0x04 to the baud-rate divisor registers (DLL, DLM) rather than the data and interrupt registers. This multiplexing is the single most common source of bugs.

---

## Core Register Set

### Line Control Register (LCR)

Controls the serial frame format and DLAB access.

```
Bit 7 : DLAB  — 1 = divisor latch access; 0 = normal RHR/THR/IER access
Bit 6 : Break — 1 = force TX line LOW (break condition)
Bit 5 : Stick — 1 = parity bit forced (1 if EPS=1; 0 if EPS=0)
Bit 4 : EPS   — 0 = odd parity;  1 = even parity
Bit 3 : PEN   — 1 = parity enabled
Bit 2 : STB   — 0 = 1 stop bit;  1 = 1.5/2 stop bits
Bit 1-0: WLS  — 00=5-bit 01=6-bit 10=7-bit 11=8-bit word length
```

Common value for **8N1**: `0x03` (8-bit, no parity, 1 stop bit).

---

### Divisor Latch Registers (DLL / DLM)

Together they form a 16-bit divisor that sets the baud rate:

```
Divisor = UART_CLK / (16 × Baud_Rate)

DLL = Divisor & 0xFF          (low byte, offset 0x00 with DLAB=1)
DLM = (Divisor >> 8) & 0xFF  (high byte, offset 0x04 with DLAB=1)
```

---

### FIFO Control Register (FCR) — write-only

```
Bit 7-6 : RXTL  — RX FIFO trigger level (00=1B, 01=4B, 10=8B, 11=14B)
Bit 5-4 : TXTL  — TX FIFO trigger level (16C750 only)
Bit 2   : XFIFO — clear TX FIFO
Bit 1   : RFIFO — clear RX FIFO
Bit 0   : FEN   — enable FIFOs (must be 1 for FIFO operation)
```

Write `0x07` to enable FIFOs and clear both on startup.

---

### Interrupt Enable Register (IER)

```
Bit 7-4 : reserved
Bit 3   : EDSSI — modem status interrupt
Bit 2   : ELSI  — line status / error interrupt
Bit 1   : ETBEI — TX holding register empty interrupt
Bit 0   : ERBFI — RX data available interrupt
```

---

### Interrupt Identification Register (IIR) — read-only

```
Bit 7-6 : FIFOs enabled status
Bit 3-1 : interrupt source ID
           011 = line status error (highest priority)
           010 = RX data available
           110 = RX timeout (FIFO mode)
           001 = TX holding register empty
           000 = modem status (lowest priority)
Bit 0   : 0 = interrupt pending; 1 = no interrupt pending
```

---

### Line Status Register (LSR) — read-only

```
Bit 7 : FIFO error flag
Bit 6 : TEMT  — TX empty (shift register and holding register both empty)
Bit 5 : THRE  — TX holding register empty (safe to write next byte)
Bit 4 : BI    — break interrupt
Bit 3 : FE    — framing error
Bit 2 : PE    — parity error
Bit 1 : OE    — overrun error
Bit 0 : DR    — data ready (byte waiting in RX FIFO / holding register)
```

---

### Modem Control Register (MCR)

```
Bit 4 : LOOP — internal loopback mode (diagnostics)
Bit 3 : OUT2 — general purpose output / enable UART IRQ on PC
Bit 2 : OUT1 — general purpose output
Bit 1 : RTS  — Request To Send (active LOW on RS-232 line)
Bit 0 : DTR  — Data Terminal Ready
```

---

### Modem Status Register (MSR) — read-only

```
Bit 7 : DCD  — Data Carrier Detect
Bit 6 : RI   — Ring Indicator
Bit 5 : DSR  — Data Set Ready
Bit 4 : CTS  — Clear To Send
Bit 3-0: delta bits (change since last read)
```

---

### Scratch Register (SCR)

An 8-bit register with no hardware effect — used to confirm UART presence and store driver state.

---

## Initialization Sequence

The canonical sequence that every UART driver must follow:

```
1.  Disable all interrupts          (IER = 0x00)
2.  Set DLAB = 1                    (LCR |= 0x80)
3.  Write baud divisor low byte     (DLL = divisor & 0xFF)
4.  Write baud divisor high byte    (DLM = divisor >> 8)
5.  Clear DLAB, set frame format    (LCR = format_byte)   ← DLAB=0 here!
6.  Enable and reset FIFOs          (FCR = 0xC7)
7.  Set MCR (DTR, RTS, OUT2)        (MCR = 0x0B)
8.  Re-enable desired interrupts    (IER = desired_mask)
9.  Verify: read LSR, IIR, MSR
```

Step 5 is critical: DLAB must be **cleared in the same write** that sets the frame format, or any IER write in step 8 will corrupt the divisor high byte.

---

## Baud Rate Calculation

| Baud Rate | Clock 1.8432 MHz (÷16) | Clock 25 MHz (÷16) | Clock 48 MHz (÷16) |
|-----------|------------------------|--------------------|--------------------|
| 9600      | 0x000C (12)            | 0x00A0 (163)       | 0x0138 (312)       |
| 115200    | 0x0001 (1)             | 0x000D (14)        | 0x001A (26)        |
| 921600    | —                      | —                  | 0x0003 (3)         |

```
Divisor = UART_CLK / (16 × Baud)
```

Always check the calculated divisor for acceptable error (< 2% is standard).

---

## Platform Examples

### 16550 UART (PC / Embedded Classic)

| Offset | Register         | Notes                          |
|--------|-----------------|-------------------------------|
| 0x00   | RHR / THR / DLL | Read=RX, Write=TX, DLAB=DLL   |
| 0x01   | IER / DLM       | DLAB=1 → divisor high byte     |
| 0x02   | FCR / IIR       | Write=FCR, Read=IIR            |
| 0x03   | LCR             |                                |
| 0x04   | MCR             |                                |
| 0x05   | LSR             |                                |
| 0x06   | MSR             |                                |
| 0x07   | SCR             |                                |

---

### STM32 USART

Key registers (32-bit, byte-addressed at 4-byte offsets):

| Offset | Register | Purpose                          |
|--------|----------|----------------------------------|
| 0x00   | SR       | Status (RXNE, TXE, TC, …)       |
| 0x04   | DR       | Data register (RX and TX)        |
| 0x08   | BRR      | Baud rate (mantissa + fraction)  |
| 0x0C   | CR1      | Main control (UE, TE, RE, …)    |
| 0x10   | CR2      | Stop bits, LIN, …               |
| 0x14   | CR3      | DMA, hardware flow control       |
| 0x18   | GTPR     | Guard time, prescaler            |

STM32 uses a **fractional baud rate divider**:
```
BRR = fCK / Baud  (with 4-bit fraction field for fine tuning)
```

---

### ARM PL011 UART

| Offset | Register | Purpose                           |
|--------|----------|-----------------------------------|
| 0x000  | DR       | Data register                     |
| 0x004  | RSR/ECR  | Error status                      |
| 0x018  | FR       | Flag register (TXFF, RXFE, BUSY)  |
| 0x024  | IBRD     | Integer baud rate divisor         |
| 0x028  | FBRD     | Fractional baud rate divisor      |
| 0x02C  | LCR_H    | Line control (format, FEN)        |
| 0x030  | CR       | Control (UARTEN, TXE, RXE)       |
| 0x038  | IMSC     | Interrupt mask                    |
| 0x044  | ICR      | Interrupt clear                   |

PL011 baud calculation:
```
BRD  = UARTCLK / (16 × Baud)
IBRD = floor(BRD)
FBRD = round((BRD - IBRD) × 64)
```

---

## Code Examples — C/C++

### 16550 Register Abstraction Layer

```c
/* uart16550_regs.h — 16550 register map and bit definitions */
#ifndef UART16550_REGS_H
#define UART16550_REGS_H

#include <stdint.h>

/* Register offsets (byte-stride, MMIO base + offset) */
#define UART_RHR    0x00   /* RX Holding Register      (DLAB=0, R)  */
#define UART_THR    0x00   /* TX Holding Register      (DLAB=0, W)  */
#define UART_DLL    0x00   /* Divisor Latch Low        (DLAB=1, RW) */
#define UART_IER    0x01   /* Interrupt Enable         (DLAB=0, RW) */
#define UART_DLM    0x01   /* Divisor Latch High       (DLAB=1, RW) */
#define UART_FCR    0x02   /* FIFO Control             (W)          */
#define UART_IIR    0x02   /* Interrupt Identification (R)          */
#define UART_LCR    0x03   /* Line Control             (RW)         */
#define UART_MCR    0x04   /* Modem Control            (RW)         */
#define UART_LSR    0x05   /* Line Status              (R)          */
#define UART_MSR    0x06   /* Modem Status             (R)          */
#define UART_SCR    0x07   /* Scratch                  (RW)         */

/* --- LCR bits --- */
#define LCR_WLS5    0x00   /* 5-bit word length  */
#define LCR_WLS6    0x01   /* 6-bit              */
#define LCR_WLS7    0x02   /* 7-bit              */
#define LCR_WLS8    0x03   /* 8-bit              */
#define LCR_STB2    (1<<2) /* 2 stop bits        */
#define LCR_PEN     (1<<3) /* parity enable      */
#define LCR_EPS     (1<<4) /* even parity select */
#define LCR_STICK   (1<<5) /* stick parity       */
#define LCR_BREAK   (1<<6) /* force break        */
#define LCR_DLAB    (1<<7) /* divisor latch      */

#define LCR_8N1     (LCR_WLS8)                 /* 8-N-1 */
#define LCR_8E1     (LCR_WLS8 | LCR_PEN | LCR_EPS) /* 8-E-1 */
#define LCR_8O1     (LCR_WLS8 | LCR_PEN)       /* 8-O-1 */

/* --- FCR bits --- */
#define FCR_FEN     (1<<0) /* FIFO enable        */
#define FCR_RXCLR   (1<<1) /* clear RX FIFO      */
#define FCR_TXCLR   (1<<2) /* clear TX FIFO      */
#define FCR_RXTL_1  (0<<6) /* RX trigger: 1 byte */
#define FCR_RXTL_4  (1<<6) /* RX trigger: 4 byte */
#define FCR_RXTL_8  (2<<6) /* RX trigger: 8 byte */
#define FCR_RXTL_14 (3<<6) /* RX trigger: 14 byte*/

/* --- IER bits --- */
#define IER_ERBFI   (1<<0) /* RX data available  */
#define IER_ETBEI   (1<<1) /* TX holding empty   */
#define IER_ELSI    (1<<2) /* line status error  */
#define IER_EDSSI   (1<<3) /* modem status       */

/* --- LSR bits --- */
#define LSR_DR      (1<<0) /* data ready         */
#define LSR_OE      (1<<1) /* overrun error      */
#define LSR_PE      (1<<2) /* parity error       */
#define LSR_FE      (1<<3) /* framing error      */
#define LSR_BI      (1<<4) /* break interrupt    */
#define LSR_THRE    (1<<5) /* TX holding empty   */
#define LSR_TEMT    (1<<6) /* TX empty           */
#define LSR_ERR_MASK (LSR_OE | LSR_PE | LSR_FE | LSR_BI)

/* --- MCR bits --- */
#define MCR_DTR     (1<<0)
#define MCR_RTS     (1<<1)
#define MCR_OUT1    (1<<2)
#define MCR_OUT2    (1<<3) /* enable interrupt to CPU on PC */
#define MCR_LOOP    (1<<4)

/* --- IIR bits --- */
#define IIR_IPEND   (1<<0) /* 0 = interrupt pending */
#define IIR_ID_MASK 0x0E
#define IIR_ID_MSR  0x00
#define IIR_ID_THRE 0x02
#define IIR_ID_RDA  0x04
#define IIR_ID_LSR  0x06
#define IIR_ID_RXTO 0x0C

/* Memory-mapped register accessors (32-bit bus, byte registers) */
typedef volatile uint8_t uart_reg_t;

static inline uint8_t uart_read(uintptr_t base, unsigned offset) {
    return *(uart_reg_t *)(base + offset);
}

static inline void uart_write(uintptr_t base, unsigned offset, uint8_t val) {
    *(uart_reg_t *)(base + offset) = val;
}

#endif /* UART16550_REGS_H */
```

---

### 16550 Full Initialization

```c
/* uart16550.c — Complete initialization and basic I/O */
#include "uart16550_regs.h"
#include <stdint.h>
#include <stdbool.h>

/* ---- Configuration structure ---- */
typedef struct {
    uintptr_t base;        /* MMIO base address              */
    uint32_t  uart_clk;   /* input clock frequency in Hz    */
    uint32_t  baud_rate;  /* desired baud rate              */
    uint8_t   lcr_format; /* LCR_8N1, LCR_8E1, etc.        */
    uint8_t   ier_mask;   /* interrupt enables              */
    bool      enable_fifo;
    uint8_t   rx_trigger; /* FCR_RXTL_1/4/8/14             */
} uart16550_config_t;

/* ---- Baud rate computation ---- */
static uint16_t uart_calc_divisor(uint32_t clk_hz, uint32_t baud) {
    /* Round to nearest integer */
    return (uint16_t)((clk_hz + (8u * baud)) / (16u * baud));
}

/* ---- Core initialization ---- */
int uart16550_init(const uart16550_config_t *cfg) {
    uintptr_t b   = cfg->base;
    uint16_t  div = uart_calc_divisor(cfg->uart_clk, cfg->baud_rate);

    if (div == 0) return -1;

    /* Step 1: Disable all interrupts */
    uart_write(b, UART_IER, 0x00);

    /* Step 2-4: Set DLAB, write divisor (low then high) */
    uart_write(b, UART_LCR, LCR_DLAB);
    uart_write(b, UART_DLL, (uint8_t)(div & 0xFF));
    uart_write(b, UART_DLM, (uint8_t)(div >> 8));

    /* Step 5: Clear DLAB + set frame format in SAME write */
    uart_write(b, UART_LCR, cfg->lcr_format & ~LCR_DLAB);

    /* Step 6: FIFO setup */
    if (cfg->enable_fifo) {
        uart_write(b, UART_FCR,
                   FCR_FEN | FCR_RXCLR | FCR_TXCLR | cfg->rx_trigger);
    } else {
        uart_write(b, UART_FCR, 0x00);
    }

    /* Step 7: Assert DTR, RTS; enable OUT2 for PC-style IRQ */
    uart_write(b, UART_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

    /* Step 8: Enable interrupts */
    uart_write(b, UART_IER, cfg->ier_mask);

    /* Step 9: Verify — scratch register round-trip */
    uart_write(b, UART_SCR, 0xA5);
    if (uart_read(b, UART_SCR) != 0xA5) return -2; /* UART not present */

    return 0;
}

/* ---- Transmit a single byte (polled) ---- */
void uart16550_putc(uintptr_t base, uint8_t ch) {
    /* Wait until TX holding register is empty */
    while (!(uart_read(base, UART_LSR) & LSR_THRE))
        ; /* spin */
    uart_write(base, UART_THR, ch);
}

/* ---- Receive a single byte (polled, blocking) ---- */
uint8_t uart16550_getc(uintptr_t base) {
    while (!(uart_read(base, UART_LSR) & LSR_DR))
        ;
    return uart_read(base, UART_RHR);
}

/* ---- Non-blocking receive ---- */
bool uart16550_trygetc(uintptr_t base, uint8_t *out) {
    if (uart_read(base, UART_LSR) & LSR_DR) {
        *out = uart_read(base, UART_RHR);
        return true;
    }
    return false;
}

/* ---- Check and clear line errors ---- */
uint8_t uart16550_check_errors(uintptr_t base) {
    uint8_t lsr = uart_read(base, UART_LSR);
    return lsr & LSR_ERR_MASK; /* caller inspects OE/PE/FE/BI bits */
}

/* ---- Example main ---- */
#ifdef UART_EXAMPLE_MAIN
#include <string.h>

#define COM1_BASE   0x3F8UL   /* x86 COM1 port (port-mapped on real x86) */
#define UART0_BASE  0x09000000UL  /* example SoC MMIO address */

int main(void) {
    uart16550_config_t cfg = {
        .base        = UART0_BASE,
        .uart_clk    = 1843200,   /* 1.8432 MHz crystal */
        .baud_rate   = 115200,
        .lcr_format  = LCR_8N1,
        .ier_mask    = 0x00,      /* polled mode */
        .enable_fifo = true,
        .rx_trigger  = FCR_RXTL_8,
    };

    if (uart16550_init(&cfg) != 0) {
        /* handle error */
        return 1;
    }

    const char *msg = "UART register config OK\r\n";
    for (size_t i = 0; msg[i]; i++)
        uart16550_putc(UART0_BASE, (uint8_t)msg[i]);

    return 0;
}
#endif
```

---

### STM32 USART Register Init (bare-metal)

```c
/* stm32_usart.h — STM32F4 USART register map */
#ifndef STM32_USART_H
#define STM32_USART_H

#include <stdint.h>

/* USART register block (each field is 32-bit) */
typedef struct {
    volatile uint32_t SR;    /* 0x00 Status              */
    volatile uint32_t DR;    /* 0x04 Data                */
    volatile uint32_t BRR;   /* 0x08 Baud rate           */
    volatile uint32_t CR1;   /* 0x0C Control 1           */
    volatile uint32_t CR2;   /* 0x10 Control 2           */
    volatile uint32_t CR3;   /* 0x14 Control 3           */
    volatile uint32_t GTPR;  /* 0x18 Guard time/prescaler*/
} USART_TypeDef;

/* SR bits */
#define USART_SR_PE     (1u<<0)   /* parity error        */
#define USART_SR_FE     (1u<<1)   /* framing error       */
#define USART_SR_NE     (1u<<2)   /* noise error         */
#define USART_SR_ORE    (1u<<3)   /* overrun error       */
#define USART_SR_IDLE   (1u<<4)   /* idle line detected  */
#define USART_SR_RXNE   (1u<<5)   /* RX not empty        */
#define USART_SR_TC     (1u<<6)   /* transmission complete*/
#define USART_SR_TXE    (1u<<7)   /* TX data register empty*/

/* CR1 bits */
#define USART_CR1_SBK   (1u<<0)   /* send break          */
#define USART_CR1_RWU   (1u<<1)   /* receiver wakeup     */
#define USART_CR1_RE    (1u<<2)   /* receiver enable     */
#define USART_CR1_TE    (1u<<3)   /* transmitter enable  */
#define USART_CR1_IDLEIE (1u<<4)  /* idle interrupt en   */
#define USART_CR1_RXNEIE (1u<<5)  /* RXNE interrupt en   */
#define USART_CR1_TCIE  (1u<<6)   /* TC interrupt en     */
#define USART_CR1_TXEIE (1u<<7)   /* TXE interrupt en    */
#define USART_CR1_PEIE  (1u<<8)   /* PE interrupt en     */
#define USART_CR1_PS    (1u<<9)   /* parity select(0=even)*/
#define USART_CR1_PCE   (1u<<10)  /* parity control en   */
#define USART_CR1_WAKE  (1u<<11)  /* wakeup method       */
#define USART_CR1_M     (1u<<12)  /* word length (0=8-bit)*/
#define USART_CR1_UE    (1u<<13)  /* USART enable        */
#define USART_CR1_OVER8 (1u<<15)  /* oversampling mode   */

/* CR2 bits (stop bits) */
#define USART_CR2_STOP_1   (0u<<12)
#define USART_CR2_STOP_0_5 (1u<<12)
#define USART_CR2_STOP_2   (2u<<12)
#define USART_CR2_STOP_1_5 (3u<<12)

/* CR3 bits */
#define USART_CR3_CTSE  (1u<<9)   /* CTS enable          */
#define USART_CR3_RTSE  (1u<<8)   /* RTS enable          */
#define USART_CR3_DMAT  (1u<<7)   /* DMA enable transmit */
#define USART_CR3_DMAR  (1u<<6)   /* DMA enable receive  */

#endif /* STM32_USART_H */
```

```c
/* stm32_usart_init.c */
#include "stm32_usart.h"

/* APB2 bus frequency (example: 84 MHz for STM32F4 @ 168 MHz) */
#define APB2_FREQ_HZ  84000000UL

/**
 * Compute BRR value.
 * With OVER8=0 (16× oversampling):
 *   BRR[15:4] = USARTDIV integer part
 *   BRR[3:0]  = USARTDIV fractional part (4 bits)
 */
static uint32_t usart_calc_brr(uint32_t fck, uint32_t baud) {
    /* Multiply by 25 to keep fractional precision without floats */
    uint32_t tmp = (fck * 25u) / (4u * baud);
    uint32_t mantissa = tmp / 100u;
    uint32_t frac = ((tmp - mantissa * 100u) * 16u + 50u) / 100u;
    return (mantissa << 4) | (frac & 0x0F);
}

void stm32_usart_init(USART_TypeDef *uart, uint32_t baud) {
    /* 
     * Prerequisite (done by caller / system init):
     *   - Enable USART clock in RCC_APB1ENR / RCC_APB2ENR
     *   - Configure GPIO pins to alternate function (AF7 for USART1/2/3)
     */

    /* 1. Disable USART before configuration */
    uart->CR1 &= ~USART_CR1_UE;

    /* 2. Set baud rate */
    uart->BRR = usart_calc_brr(APB2_FREQ_HZ, baud);

    /* 3. Set frame format: 8-bit, 1 stop bit (default CR2 state) */
    uart->CR2 = USART_CR2_STOP_1;   /* explicit: 1 stop bit */

    /* 4. CR3: no flow control, no DMA */
    uart->CR3 = 0x00;

    /* 5. CR1: 8-bit, no parity, enable TX and RX */
    uart->CR1 = USART_CR1_TE | USART_CR1_RE;

    /* 6. Enable USART */
    uart->CR1 |= USART_CR1_UE;

    /* 7. Wait for TX line to be ready (TC set after enabling TE) */
    while (!(uart->SR & USART_SR_TC))
        ;
}

void stm32_usart_putc(USART_TypeDef *uart, uint8_t ch) {
    while (!(uart->SR & USART_SR_TXE))
        ;
    uart->DR = ch;
}

uint8_t stm32_usart_getc(USART_TypeDef *uart) {
    while (!(uart->SR & USART_SR_RXNE))
        ;
    return (uint8_t)(uart->DR & 0xFF);
}

/* Enable 8E1 with hardware flow control */
void stm32_usart_init_8e1_hwfc(USART_TypeDef *uart, uint32_t baud) {
    uart->CR1 &= ~USART_CR1_UE;

    uart->BRR  = usart_calc_brr(APB2_FREQ_HZ, baud);
    uart->CR2  = USART_CR2_STOP_1;
    uart->CR3  = USART_CR3_CTSE | USART_CR3_RTSE;

    /* 8-bit, even parity (PS=0, PCE=1), M=0 keeps 8-bit data + 1 parity */
    uart->CR1  = USART_CR1_TE | USART_CR1_RE | USART_CR1_PCE;

    uart->CR1 |= USART_CR1_UE;
    while (!(uart->SR & USART_SR_TC))
        ;
}
```

---

### ARM PL011 Register Init (bare-metal)

```c
/* pl011.h — ARM PrimeCell PL011 UART register map */
#ifndef PL011_H
#define PL011_H

#include <stdint.h>

typedef struct {
    volatile uint32_t DR;      /* 0x000 Data                   */
    volatile uint32_t RSR_ECR; /* 0x004 Receive Status/Error   */
    volatile uint32_t _res0[4];
    volatile uint32_t FR;      /* 0x018 Flag                   */
    volatile uint32_t _res1;
    volatile uint32_t ILPR;    /* 0x020 IrDA Low-Power         */
    volatile uint32_t IBRD;    /* 0x024 Integer Baud Rate Div  */
    volatile uint32_t FBRD;    /* 0x028 Fractional Baud Rate   */
    volatile uint32_t LCR_H;   /* 0x02C Line Control           */
    volatile uint32_t CR;      /* 0x030 Control                */
    volatile uint32_t IFLS;    /* 0x034 Interrupt FIFO Levels  */
    volatile uint32_t IMSC;    /* 0x038 Interrupt Mask Set/Clear*/
    volatile uint32_t RIS;     /* 0x03C Raw Interrupt Status   */
    volatile uint32_t MIS;     /* 0x040 Masked Interrupt Status*/
    volatile uint32_t ICR;     /* 0x044 Interrupt Clear        */
    volatile uint32_t DMACR;   /* 0x048 DMA Control            */
} PL011_TypeDef;

/* FR bits */
#define PL011_FR_CTS    (1u<<0)
#define PL011_FR_DSR    (1u<<1)
#define PL011_FR_DCD    (1u<<2)
#define PL011_FR_BUSY   (1u<<3)
#define PL011_FR_RXFE   (1u<<4)  /* RX FIFO empty  */
#define PL011_FR_TXFF   (1u<<5)  /* TX FIFO full   */
#define PL011_FR_RXFF   (1u<<6)  /* RX FIFO full   */
#define PL011_FR_TXFE   (1u<<7)  /* TX FIFO empty  */

/* LCR_H bits */
#define PL011_LCR_BRK   (1u<<0)
#define PL011_LCR_PEN   (1u<<1)  /* parity enable  */
#define PL011_LCR_EPS   (1u<<2)  /* even parity    */
#define PL011_LCR_STP2  (1u<<3)  /* 2 stop bits    */
#define PL011_LCR_FEN   (1u<<4)  /* FIFO enable    */
#define PL011_LCR_WLEN5 (0u<<5)
#define PL011_LCR_WLEN6 (1u<<5)
#define PL011_LCR_WLEN7 (2u<<5)
#define PL011_LCR_WLEN8 (3u<<5)
#define PL011_LCR_SPS   (1u<<7)  /* stick parity   */

/* CR bits */
#define PL011_CR_UARTEN (1u<<0)  /* UART enable    */
#define PL011_CR_SIREN  (1u<<1)
#define PL011_CR_SIRLP  (1u<<2)
#define PL011_CR_LBE    (1u<<7)  /* loopback       */
#define PL011_CR_TXE    (1u<<8)  /* TX enable      */
#define PL011_CR_RXE    (1u<<9)  /* RX enable      */
#define PL011_CR_CTSEN  (1u<<15) /* CTS flow ctrl  */
#define PL011_CR_RTSEN  (1u<<14) /* RTS flow ctrl  */

#endif /* PL011_H */
```

```c
/* pl011_init.c */
#include "pl011.h"

#define PL011_CLK_HZ  48000000UL   /* 48 MHz reference clock */

void pl011_init(PL011_TypeDef *uart, uint32_t baud) {
    /* 1. Disable UART */
    uart->CR = 0;

    /* 2. Wait for current TX to complete */
    while (uart->FR & PL011_FR_BUSY)
        ;

    /* 3. Flush FIFOs: disable FEN then re-enable later */
    uart->LCR_H = 0;

    /* 4. Clear pending interrupts */
    uart->ICR = 0x7FF;

    /* 5. Set baud rate divisors
     *    BRD = UARTCLK / (16 × baud)
     *    IBRD = integer part
     *    FBRD = round(fractional part × 64)
     */
    uint32_t brd_x64 = (PL011_CLK_HZ * 4u) / baud; /* ×4 = ×64/16 */
    uart->IBRD = brd_x64 >> 6;           /* integer part */
    uart->FBRD = brd_x64 & 0x3F;         /* fractional 6-bit */

    /* 6. Line control: 8-bit, no parity, 1 stop, FIFOs enabled */
    uart->LCR_H = PL011_LCR_WLEN8 | PL011_LCR_FEN;

    /* 7. Enable UART, TX, RX */
    uart->CR = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
}

void pl011_putc(PL011_TypeDef *uart, uint8_t ch) {
    while (uart->FR & PL011_FR_TXFF)  /* wait while TX FIFO full */
        ;
    uart->DR = ch;
}

uint8_t pl011_getc(PL011_TypeDef *uart) {
    while (uart->FR & PL011_FR_RXFE)  /* wait while RX FIFO empty */
        ;
    return (uint8_t)(uart->DR & 0xFF);
}
```

---

### Runtime Reconfiguration

```cpp
/* uart_reconfig.cpp — C++ class for runtime baud/format change */
#include "uart16550_regs.h"
#include <cstdint>

class Uart16550 {
public:
    explicit Uart16550(uintptr_t base, uint32_t clk)
        : base_(base), clk_(clk) {}

    /* Change baud rate without disrupting frame format */
    void set_baud(uint32_t baud) {
        wait_tx_empty();
        uint16_t div = static_cast<uint16_t>((clk_ + 8u * baud) / (16u * baud));

        /* Save current LCR to restore format bits */
        uint8_t lcr_saved = reg_read(UART_LCR);

        /* Set DLAB */
        reg_write(UART_LCR, lcr_saved | LCR_DLAB);

        /* Write new divisor */
        reg_write(UART_DLL, static_cast<uint8_t>(div & 0xFF));
        reg_write(UART_DLM, static_cast<uint8_t>(div >> 8));

        /* Restore LCR (clears DLAB) */
        reg_write(UART_LCR, lcr_saved & ~LCR_DLAB);
    }

    /* Change parity at runtime */
    void set_parity(bool enable, bool even) {
        wait_tx_empty();
        uint8_t lcr = reg_read(UART_LCR) & ~(LCR_PEN | LCR_EPS);
        if (enable) {
            lcr |= LCR_PEN;
            if (even) lcr |= LCR_EPS;
        }
        reg_write(UART_LCR, lcr);
    }

    /* Enable loopback for self-test */
    void set_loopback(bool enable) {
        uint8_t mcr = reg_read(UART_MCR);
        if (enable) mcr |= MCR_LOOP;
        else        mcr &= ~MCR_LOOP;
        reg_write(UART_MCR, mcr);
    }

private:
    uintptr_t base_;
    uint32_t  clk_;

    void wait_tx_empty() {
        while (!(reg_read(UART_LSR) & LSR_TEMT))
            ;
    }
    uint8_t reg_read(unsigned off) const {
        return *reinterpret_cast<volatile uint8_t *>(base_ + off);
    }
    void reg_write(unsigned off, uint8_t val) const {
        *reinterpret_cast<volatile uint8_t *>(base_ + off) = val;
    }
};
```

---

### Register Dump / Debug Utility

```c
/* uart_dump.c — print all register values for diagnostics */
#include "uart16550_regs.h"
#include <stdio.h>

void uart16550_dump(uintptr_t base) {
    uint8_t lcr = uart_read(base, UART_LCR);
    uint8_t ier = uart_read(base, UART_IER);
    uint8_t iir = uart_read(base, UART_IIR);
    uint8_t mcr = uart_read(base, UART_MCR);
    uint8_t lsr = uart_read(base, UART_LSR);
    uint8_t msr = uart_read(base, UART_MSR);

    /* Read divisor (requires DLAB) */
    uart_write(base, UART_LCR, lcr | LCR_DLAB);
    uint8_t dll = uart_read(base, UART_DLL);
    uint8_t dlm = uart_read(base, UART_DLM);
    uart_write(base, UART_LCR, lcr);           /* restore */

    uint16_t div = ((uint16_t)dlm << 8) | dll;

    printf("=== UART 16550 Register Dump @ 0x%08lX ===\n", (unsigned long)base);
    printf("LCR : 0x%02X  (DLAB=%d WLS=%d PEN=%d EPS=%d STB=%d)\n",
           lcr,
           (lcr >> 7) & 1,
           (lcr & 0x03) + 5,        /* word length */
           (lcr >> 3) & 1,
           (lcr >> 4) & 1,
           (lcr >> 2) & 1);
    printf("DIV : 0x%04X (%u)\n", div, div);
    printf("IER : 0x%02X  (ERBFI=%d ETBEI=%d ELSI=%d EDSSI=%d)\n",
           ier,
           (ier >> 0) & 1, (ier >> 1) & 1,
           (ier >> 2) & 1, (ier >> 3) & 1);
    printf("IIR : 0x%02X  (IPEND=%d ID=0x%X FIFO=%d)\n",
           iir, iir & 1, (iir >> 1) & 7, (iir >> 6) & 3);
    printf("MCR : 0x%02X  (DTR=%d RTS=%d OUT1=%d OUT2=%d LOOP=%d)\n",
           mcr,
           (mcr >> 0) & 1, (mcr >> 1) & 1,
           (mcr >> 2) & 1, (mcr >> 3) & 1, (mcr >> 4) & 1);
    printf("LSR : 0x%02X  (DR=%d OE=%d PE=%d FE=%d BI=%d THRE=%d TEMT=%d)\n",
           lsr,
           (lsr >> 0) & 1, (lsr >> 1) & 1, (lsr >> 2) & 1,
           (lsr >> 3) & 1, (lsr >> 4) & 1, (lsr >> 5) & 1, (lsr >> 6) & 1);
    printf("MSR : 0x%02X  (CTS=%d DSR=%d RI=%d DCD=%d)\n",
           msr,
           (msr >> 4) & 1, (msr >> 5) & 1,
           (msr >> 6) & 1, (msr >> 7) & 1);
}
```

---

## Code Examples — Rust

### Register Block with volatile access

```rust
// uart_mmio.rs — Safe volatile register access without external crates

use core::ptr::{read_volatile, write_volatile};

pub struct Reg {
    addr: *mut u8,
}

impl Reg {
    /// # Safety: `addr` must be a valid MMIO address for this register
    #[inline(always)]
    pub const unsafe fn new(addr: *mut u8) -> Self {
        Self { addr }
    }

    #[inline(always)]
    pub fn read(&self) -> u8 {
        // SAFETY: caller guarantees the pointer is valid MMIO
        unsafe { read_volatile(self.addr) }
    }

    #[inline(always)]
    pub fn write(&self, val: u8) {
        // SAFETY: caller guarantees the pointer is valid MMIO
        unsafe { write_volatile(self.addr, val) }
    }

    #[inline(always)]
    pub fn modify(&self, mask: u8, val: u8) {
        let cur = self.read();
        self.write((cur & !mask) | (val & mask));
    }
}

// Offset constants for 16550 layout
pub mod offset {
    pub const RHR: usize = 0x00;
    pub const THR: usize = 0x00;
    pub const DLL: usize = 0x00;
    pub const IER: usize = 0x01;
    pub const DLM: usize = 0x01;
    pub const FCR: usize = 0x02;
    pub const IIR: usize = 0x02;
    pub const LCR: usize = 0x03;
    pub const MCR: usize = 0x04;
    pub const LSR: usize = 0x05;
    pub const MSR: usize = 0x06;
    pub const SCR: usize = 0x07;
}

// LCR bit flags
pub mod lcr {
    pub const WLS5: u8  = 0x00;
    pub const WLS6: u8  = 0x01;
    pub const WLS7: u8  = 0x02;
    pub const WLS8: u8  = 0x03;
    pub const STB2: u8  = 1 << 2;
    pub const PEN:  u8  = 1 << 3;
    pub const EPS:  u8  = 1 << 4;
    pub const DLAB: u8  = 1 << 7;
    pub const N8_1: u8  = WLS8;         // 8N1
    pub const E8_1: u8  = WLS8 | PEN | EPS; // 8E1
}

// LSR bit flags
pub mod lsr {
    pub const DR:   u8 = 1 << 0;
    pub const OE:   u8 = 1 << 1;
    pub const PE:   u8 = 1 << 2;
    pub const FE:   u8 = 1 << 3;
    pub const BI:   u8 = 1 << 4;
    pub const THRE: u8 = 1 << 5;
    pub const TEMT: u8 = 1 << 6;
}

// FCR values
pub mod fcr {
    pub const FEN:    u8 = 1 << 0;
    pub const RXCLR:  u8 = 1 << 1;
    pub const TXCLR:  u8 = 1 << 2;
    pub const TL_1:   u8 = 0 << 6;
    pub const TL_4:   u8 = 1 << 6;
    pub const TL_8:   u8 = 2 << 6;
    pub const TL_14:  u8 = 3 << 6;
    pub const INIT:   u8 = FEN | RXCLR | TXCLR | TL_8;
}

// MCR bits
pub mod mcr {
    pub const DTR:  u8 = 1 << 0;
    pub const RTS:  u8 = 1 << 1;
    pub const OUT2: u8 = 1 << 3;
    pub const LOOP: u8 = 1 << 4;
}
```

---

### 16550 Driver in Rust

```rust
// uart16550.rs — Full 16550 driver with safe public API

use crate::uart_mmio::{fcr, lcr, lsr, mcr, offset, Reg};
use core::ptr::read_volatile;

/// Error type for UART operations
#[derive(Debug)]
pub enum UartError {
    NotPresent,
    InvalidBaud,
    Overrun,
    Parity,
    Framing,
}

/// Configuration for UART initialisation
pub struct UartConfig {
    pub uart_clk:   u32,
    pub baud_rate:  u32,
    pub format:     u8,   // use lcr::N8_1 / E8_1 / O8_1
    pub ier_mask:   u8,
    pub enable_fifo: bool,
}

impl Default for UartConfig {
    fn default() -> Self {
        Self {
            uart_clk:    1_843_200,
            baud_rate:   115_200,
            format:      lcr::N8_1,
            ier_mask:    0x00,
            enable_fifo: true,
        }
    }
}

/// 16550-compatible UART driver
pub struct Uart16550 {
    base: usize,
}

impl Uart16550 {
    /// Construct driver at given MMIO base address.
    ///
    /// # Safety
    /// `base` must be a valid, aligned MMIO base for a 16550-compatible UART.
    pub const unsafe fn new(base: usize) -> Self {
        Self { base }
    }

    fn reg(&self, off: usize) -> Reg {
        // SAFETY: caller of `new` guarantees base validity
        unsafe { Reg::new((self.base + off) as *mut u8) }
    }

    fn calc_divisor(clk: u32, baud: u32) -> Result<u16, UartError> {
        if baud == 0 {
            return Err(UartError::InvalidBaud);
        }
        let div = (clk + 8 * baud) / (16 * baud);
        if div == 0 || div > 0xFFFF {
            return Err(UartError::InvalidBaud);
        }
        Ok(div as u16)
    }

    /// Initialise UART with given configuration.
    pub fn init(&self, cfg: &UartConfig) -> Result<(), UartError> {
        let div = Self::calc_divisor(cfg.uart_clk, cfg.baud_rate)?;

        // Step 1: disable all interrupts
        self.reg(offset::IER).write(0x00);

        // Step 2-4: DLAB=1, write divisor
        self.reg(offset::LCR).write(lcr::DLAB);
        self.reg(offset::DLL).write((div & 0xFF) as u8);
        self.reg(offset::DLM).write((div >> 8) as u8);

        // Step 5: clear DLAB, set format (single atomic write)
        self.reg(offset::LCR).write(cfg.format & !lcr::DLAB);

        // Step 6: FIFO control
        if cfg.enable_fifo {
            self.reg(offset::FCR).write(fcr::INIT);
        } else {
            self.reg(offset::FCR).write(0x00);
        }

        // Step 7: assert DTR, RTS, enable OUT2
        self.reg(offset::MCR).write(mcr::DTR | mcr::RTS | mcr::OUT2);

        // Step 8: enable selected interrupts
        self.reg(offset::IER).write(cfg.ier_mask);

        // Step 9: verify UART presence via scratch register round-trip
        self.reg(offset::SCR).write(0xA5);
        if self.reg(offset::SCR).read() != 0xA5 {
            return Err(UartError::NotPresent);
        }

        Ok(())
    }

    /// Transmit a byte (polled — spins until TX holding register is empty).
    pub fn putc(&self, byte: u8) {
        while self.reg(offset::LSR).read() & lsr::THRE == 0 {
            core::hint::spin_loop();
        }
        self.reg(offset::THR).write(byte);
    }

    /// Receive a byte (polled — blocks until data is available).
    pub fn getc(&self) -> u8 {
        while self.reg(offset::LSR).read() & lsr::DR == 0 {
            core::hint::spin_loop();
        }
        self.reg(offset::RHR).read()
    }

    /// Non-blocking receive — returns `None` if no data available.
    pub fn try_getc(&self) -> Option<u8> {
        if self.reg(offset::LSR).read() & lsr::DR != 0 {
            Some(self.reg(offset::RHR).read())
        } else {
            None
        }
    }

    /// Send a string (convenience wrapper).
    pub fn puts(&self, s: &str) {
        for &b in s.as_bytes() {
            if b == b'\n' {
                self.putc(b'\r');
            }
            self.putc(b);
        }
    }

    /// Read and return current LSR, checking for errors.
    pub fn check_errors(&self) -> Result<(), UartError> {
        let lsr = self.reg(offset::LSR).read();
        if lsr & lsr::OE != 0 { return Err(UartError::Overrun); }
        if lsr & lsr::PE != 0 { return Err(UartError::Parity);  }
        if lsr & lsr::FE != 0 { return Err(UartError::Framing); }
        Ok(())
    }

    /// Change baud rate at runtime without changing format.
    pub fn set_baud(&self, clk: u32, baud: u32) -> Result<(), UartError> {
        let div = Self::calc_divisor(clk, baud)?;

        // Wait for TX to drain
        while self.reg(offset::LSR).read() & lsr::TEMT == 0 {
            core::hint::spin_loop();
        }

        let lcr_saved = self.reg(offset::LCR).read();
        self.reg(offset::LCR).write(lcr_saved | lcr::DLAB);
        self.reg(offset::DLL).write((div & 0xFF) as u8);
        self.reg(offset::DLM).write((div >> 8) as u8);
        self.reg(offset::LCR).write(lcr_saved & !lcr::DLAB);

        Ok(())
    }

    /// Enable or disable internal loopback mode (useful for self-test).
    pub fn set_loopback(&self, enable: bool) {
        let mcr = self.reg(offset::MCR);
        let val = mcr.read();
        mcr.write(if enable { val | mcr::LOOP } else { val & !mcr::LOOP });
    }
}

// Implement core::fmt::Write so the UART can be used with write!/writeln!
use core::fmt;

impl fmt::Write for Uart16550 {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.puts(s);
        Ok(())
    }
}

// Example usage (in a bare-metal main or BSP)
#[cfg(feature = "example")]
fn example_main() {
    use core::fmt::Write;

    const UART0_BASE: usize = 0x0900_0000;

    // SAFETY: address is a valid MMIO UART on this platform
    let mut uart = unsafe { Uart16550::new(UART0_BASE) };

    uart.init(&UartConfig::default()).expect("UART init failed");

    writeln!(uart, "Boot OK — UART register init complete").unwrap();

    // Self-test via loopback
    uart.set_loopback(true);
    uart.putc(0x55);
    assert_eq!(uart.getc(), 0x55);
    uart.set_loopback(false);
}
```

---

### STM32 USART Rust Init

```rust
// stm32_usart.rs — STM32 USART register-level driver in Rust (no HAL)

use core::ptr::{read_volatile, write_volatile};

const APB2_FREQ: u32 = 84_000_000; // 84 MHz

/// USART register block offsets (byte offsets for 32-bit registers)
mod regs {
    pub const SR:   usize = 0x00;
    pub const DR:   usize = 0x04;
    pub const BRR:  usize = 0x08;
    pub const CR1:  usize = 0x0C;
    pub const CR2:  usize = 0x10;
    pub const CR3:  usize = 0x14;
}

mod sr {
    pub const RXNE: u32 = 1 << 5;
    pub const TC:   u32 = 1 << 6;
    pub const TXE:  u32 = 1 << 7;
}

mod cr1 {
    pub const RE:     u32 = 1 << 2;
    pub const TE:     u32 = 1 << 3;
    pub const RXNEIE: u32 = 1 << 5;
    pub const TXEIE:  u32 = 1 << 7;
    pub const PCE:    u32 = 1 << 10;
    pub const PS:     u32 = 1 << 9;  // 0=even, 1=odd
    pub const M:      u32 = 1 << 12; // 0=8-bit, 1=9-bit (used with parity)
    pub const UE:     u32 = 1 << 13;
}

mod cr2 {
    pub const STOP_1:   u32 = 0 << 12;
    pub const STOP_2:   u32 = 2 << 12;
    pub const STOP_MASK: u32 = 3 << 12;
}

mod cr3 {
    pub const RTSE: u32 = 1 << 8;
    pub const CTSE: u32 = 1 << 9;
}

pub struct Usart {
    base: usize,
}

impl Usart {
    /// # Safety: `base` must point to a valid STM32 USART peripheral
    pub const unsafe fn new(base: usize) -> Self {
        Self { base }
    }

    fn read32(&self, off: usize) -> u32 {
        unsafe { read_volatile((self.base + off) as *const u32) }
    }

    fn write32(&self, off: usize, val: u32) {
        unsafe { write_volatile((self.base + off) as *mut u32, val) }
    }

    fn modify32(&self, off: usize, clear: u32, set: u32) {
        let v = self.read32(off);
        self.write32(off, (v & !clear) | set);
    }

    /// Compute BRR for 16× oversampling
    fn calc_brr(fck: u32, baud: u32) -> u32 {
        // Fixed-point: multiply numerator × 25 to keep fraction
        let tmp = (fck * 25) / (4 * baud);
        let mantissa = tmp / 100;
        let frac = ((tmp - mantissa * 100) * 16 + 50) / 100;
        (mantissa << 4) | (frac & 0x0F)
    }

    /// Initialise for 8N1 polled operation
    pub fn init(&self, baud: u32) {
        // Disable USART
        self.modify32(regs::CR1, cr1::UE, 0);

        // Baud rate
        self.write32(regs::BRR, Self::calc_brr(APB2_FREQ, baud));

        // Stop bits: 1 stop
        self.modify32(regs::CR2, cr2::STOP_MASK, cr2::STOP_1);

        // No flow control
        self.modify32(regs::CR3, cr3::RTSE | cr3::CTSE, 0);

        // CR1: 8-bit, no parity, TX+RX enable
        self.write32(regs::CR1, cr1::TE | cr1::RE);

        // Enable USART
        self.modify32(regs::CR1, 0, cr1::UE);

        // Wait for TC (transmit complete after enabling TE)
        while self.read32(regs::SR) & sr::TC == 0 {
            core::hint::spin_loop();
        }
    }

    pub fn putc(&self, byte: u8) {
        while self.read32(regs::SR) & sr::TXE == 0 {
            core::hint::spin_loop();
        }
        self.write32(regs::DR, byte as u32);
    }

    pub fn getc(&self) -> u8 {
        while self.read32(regs::SR) & sr::RXNE == 0 {
            core::hint::spin_loop();
        }
        (self.read32(regs::DR) & 0xFF) as u8
    }

    /// Enable or disable interrupt-driven mode
    pub fn set_rxne_irq(&self, enable: bool) {
        self.modify32(
            regs::CR1,
            cr1::RXNEIE,
            if enable { cr1::RXNEIE } else { 0 },
        );
    }
}

use core::fmt;

impl fmt::Write for Usart {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for &b in s.as_bytes() {
            if b == b'\n' {
                self.putc(b'\r');
            }
            self.putc(b);
        }
        Ok(())
    }
}
```

---

### PL011 Rust Init

```rust
// pl011.rs — ARM PL011 UART register driver in Rust

use core::ptr::{read_volatile, write_volatile};

mod regs {
    pub const DR:    usize = 0x000;
    pub const FR:    usize = 0x018;
    pub const IBRD:  usize = 0x024;
    pub const FBRD:  usize = 0x028;
    pub const LCR_H: usize = 0x02C;
    pub const CR:    usize = 0x030;
    pub const IMSC:  usize = 0x038;
    pub const ICR:   usize = 0x044;
}

mod fr {
    pub const RXFE: u32 = 1 << 4;
    pub const TXFF: u32 = 1 << 5;
    pub const BUSY: u32 = 1 << 3;
}

mod lcr_h {
    pub const FEN:   u32 = 1 << 4;
    pub const WLEN8: u32 = 3 << 5;
    pub const PEN:   u32 = 1 << 1;
    pub const EPS:   u32 = 1 << 2;
}

mod cr_bits {
    pub const UARTEN: u32 = 1 << 0;
    pub const TXE:    u32 = 1 << 8;
    pub const RXE:    u32 = 1 << 9;
    pub const CTSEN:  u32 = 1 << 15;
    pub const RTSEN:  u32 = 1 << 14;
}

pub struct Pl011 {
    base: usize,
}

impl Pl011 {
    /// # Safety: `base` must be a valid PL011 MMIO base address
    pub const unsafe fn new(base: usize) -> Self {
        Self { base }
    }

    fn read(&self, off: usize) -> u32 {
        unsafe { read_volatile((self.base + off) as *const u32) }
    }

    fn write(&self, off: usize, val: u32) {
        unsafe { write_volatile((self.base + off) as *mut u32, val) }
    }

    /// Initialise PL011 for 8N1 at given baud.
    /// `uartclk` is the reference clock frequency in Hz.
    pub fn init(&self, uartclk: u32, baud: u32) {
        // 1. Disable UART
        self.write(regs::CR, 0);

        // 2. Wait for any ongoing transmission to finish
        while self.read(regs::FR) & fr::BUSY != 0 {
            core::hint::spin_loop();
        }

        // 3. Flush: clear FEN in LCR_H (disables FIFOs, flushing them)
        self.write(regs::LCR_H, 0);

        // 4. Clear all pending interrupts
        self.write(regs::ICR, 0x7FF);

        // 5. Compute integer and fractional baud rate divisors
        //    BRD × 64 = (uartclk × 4) / baud
        let brd64 = (uartclk as u64 * 4) / baud as u64;
        let ibrd = (brd64 >> 6) as u32;  // integer part
        let fbrd = (brd64 & 0x3F) as u32; // fractional 6-bit part

        self.write(regs::IBRD, ibrd);
        self.write(regs::FBRD, fbrd);

        // 6. LCR_H: 8-bit, no parity, 1 stop, enable FIFOs
        //    NOTE: must be written AFTER IBRD/FBRD (they latch on LCR_H write)
        self.write(regs::LCR_H, lcr_h::WLEN8 | lcr_h::FEN);

        // 7. Unmask no interrupts (polled mode)
        self.write(regs::IMSC, 0x000);

        // 8. Enable UART, TX, RX
        self.write(regs::CR, cr_bits::UARTEN | cr_bits::TXE | cr_bits::RXE);
    }

    pub fn putc(&self, byte: u8) {
        while self.read(regs::FR) & fr::TXFF != 0 {
            core::hint::spin_loop();
        }
        self.write(regs::DR, byte as u32);
    }

    pub fn getc(&self) -> u8 {
        while self.read(regs::FR) & fr::RXFE != 0 {
            core::hint::spin_loop();
        }
        (self.read(regs::DR) & 0xFF) as u8
    }

    /// Enable RX interrupt for interrupt-driven use
    pub fn enable_rx_irq(&self) {
        let imsc = self.read(regs::IMSC);
        self.write(regs::IMSC, imsc | (1 << 4)); // RXIM bit
    }
}

use core::fmt;

impl fmt::Write for Pl011 {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for &b in s.as_bytes() {
            if b == b'\n' {
                self.putc(b'\r');
            }
            self.putc(b);
        }
        Ok(())
    }
}
```

---

## Common Pitfalls

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| **Forgetting to clear DLAB** | Corrupted IER (baud divisor MSB overwritten) | Always write LCR with DLAB=0 after setting divisor |
| **Separate LCR writes for DLAB clear and format** | Same as above | Use single `LCR = format_byte` (DLAB bit implicitly 0) |
| **Not waiting for TEMT before baud change** | Garbled last byte | Poll `LSR & TEMT` before touching divisor registers |
| **Wrong clock source** | Completely wrong baud rate | Verify RCC/CCM settings; print calculated vs expected divisor |
| **Off-by-one in BRR (STM32 fractional)** | Baud error > 2% | Use the ×25/÷100 integer method or hardware-recommended formula |
| **PL011: writing BRR without LCR_H write after** | Baud rate not applied | PL011 latches IBRD/FBRD only when LCR_H is written |
| **Reading IIR instead of FCR (write-only)** | Reads back IIR, not FCR state | FCR is write-only; use a driver shadow register if needed |
| **Missing OUT2 (MCR) on PC platforms** | Interrupt never reaches CPU | Set `MCR_OUT2` to gate IRQ to the interrupt controller |
| **No volatile qualifier in C/C++** | Compiler optimises away register reads | Always declare as `volatile uint8_t *` or `volatile uint32_t *` |
| **No `read_volatile` / `write_volatile` in Rust** | UB; compiler removes accesses | Always use `read_volatile` / `write_volatile` for MMIO |

---

## Summary

Register-level UART programming rests on a small, consistent set of principles regardless of platform:

**Configuration follows a mandatory order.** Interrupts must be disabled first; divisor registers must be written while DLAB is set; DLAB must be cleared — ideally in the same write that sets the frame format — before normal operation begins. Violating this order silently corrupts the baud rate or interrupt configuration.

**Baud rate accuracy is arithmetic.** The formula `Divisor = CLK / (16 × Baud)` must use the actual peripheral clock, which may differ from the CPU clock after prescalers. Errors above 2% cause unreliable communication. STM32 and PL011 both provide fractional divisor fields specifically to reduce this error; the integer-only 16550 compensates with a precisely-tuned crystal (1.8432 MHz).

**Register access must be volatile.** In C/C++, every MMIO pointer must be `volatile`-qualified; in Rust, `read_volatile` and `write_volatile` must be used. Without this, the compiler is free to eliminate or reorder reads and writes, producing drivers that pass code review but fail at runtime.

**Platform differences are structural, not conceptual.** The 16550, STM32 USART, and PL011 all expose the same logical controls — baud divisor, frame format, FIFO management, status flags, interrupt enables — through differently organised register maps. Mastering one makes the others immediately approachable.

**The LSR is your primary diagnostic tool.** The Line Status Register exposes data-ready, TX-empty, overrun, parity, framing, and break status in a single read. A register dump routine that prints LCR, IER, LSR, MCR, and the computed divisor is the fastest path to diagnosing any initialisation or communication fault.

---

*Next: [13. Interrupt Handling](13_Interrupt_Handling.md) — UART interrupt sources, ISR design, and deferred processing patterns.*