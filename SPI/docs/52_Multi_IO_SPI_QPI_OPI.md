The document covers the full topic across 12 sections. Here's what's inside:

**Architecture & Theory** — Signal line mappings for QSPI (IO0–IO3) and OPI (IO0–IO7 + DQS), the W-X-Y notation for describing transaction modes (1-1-4, 4-4-4, 8-8-8, 8D-8D-8D), STR vs DTR timing diagrams, dummy cycle selection by frequency, and a full flash command reference table.

**C/C++ Code (4 examples):**
- Low-level STM32 QUADSPI register driver — direct CCR/AR/DR manipulation, auto-polling mode for WIP
- HAL-based Winbond W25Q256 driver — quad enable, 1-1-4 read, quad page program with page boundary splitting, sector erase
- OPI STR/DTR driver skeleton for Macronix MX25UM51245G — 16-bit OPI commands, DQS strobe setup
- Memory-mapped XIP mode — enabling the controller's automatic read-on-access mode at 0x90000000

**Rust Code (4 examples):**
- Trait abstractions (`QspiCommand`, `IoWidth`, `TransferRate`) for a portable driver API
- Full W25Q flash driver generic over any `QspiController` — idiomatic Rust with error propagation
- OPI driver using the **type-state pattern** — `ModeOpiStr` / `ModeOpiDtr` as compile-time guarantees (calling `read_dtr()` in STR mode is a compiler error)
- Async Embassy integration — non-blocking DMA-driven QSPI with concurrent task execution

**Common Pitfalls** — wrong dummy cycles, QE bit not set, page overflow, CS timing, clock polarity, erase-before-write, OPI DTR PCB layout, and mode byte issues with continuous-read mode.

# 52. Multi-IO SPI (QPI / OPI)
### Quad (4-bit) and Octal (8-bit) SPI Modes for Ultra-High-Speed Flash Memory

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Standard SPI Recap](#2-standard-spi-recap)
3. [Multi-IO SPI Architecture](#3-multi-io-spi-architecture)
   - 3.1 [Dual SPI (DSPI)](#31-dual-spi-dspi)
   - 3.2 [Quad SPI (QSPI / QPI)](#32-quad-spi-qspi--qpi)
   - 3.3 [Octal SPI (OSPI / OPI)](#33-octal-spi-ospi--opi)
4. [Signal Lines and Pin Mapping](#4-signal-lines-and-pin-mapping)
5. [Command Structure and Protocol Phases](#5-command-structure-and-protocol-phases)
   - 5.1 [STR vs DTR (Single vs Double Transfer Rate)](#51-str-vs-dtr-single-vs-double-transfer-rate)
   - 5.2 [Dummy Cycles](#52-dummy-cycles)
   - 5.3 [Address Modes (3-byte vs 4-byte)](#53-address-modes-3-byte-vs-4-byte)
6. [Flash Memory Commands Reference](#6-flash-memory-commands-reference)
7. [Performance Analysis](#7-performance-analysis)
8. [Hardware Considerations](#8-hardware-considerations)
   - 8.1 [QSPI Controller (XSPI/QSPI IP)](#81-qspi-controller-xspiقspi-ip)
   - 8.2 [Memory-Mapped (XIP) Mode](#82-memory-mapped-xip-mode)
9. [Programming: C/C++](#9-programming-cc)
   - 9.1 [Low-Level Register Driver (STM32 QSPI)](#91-low-level-register-driver-stm32-qspi)
   - 9.2 [HAL-Based QSPI Flash Driver](#92-hal-based-qspi-flash-driver)
   - 9.3 [OPI (Octal SPI) Driver Skeleton](#93-opi-octal-spi-driver-skeleton)
   - 9.4 [Memory-Mapped XIP Mode (C)](#94-memory-mapped-xip-mode-c)
10. [Programming: Rust](#10-programming-rust)
    - 10.1 [Rust HAL Trait Abstractions](#101-rust-hal-trait-abstractions)
    - 10.2 [Rust QSPI Flash Driver](#102-rust-qspi-flash-driver)
    - 10.3 [Rust OPI Driver Skeleton](#103-rust-opi-driver-skeleton)
    - 10.4 [Async QSPI with Embassy](#104-async-qspi-with-embassy)
11. [Common Pitfalls and Debugging](#11-common-pitfalls-and-debugging)
12. [Summary](#12-summary)

---

## 1. Introduction

Standard SPI transfers data on a single wire — one bit per clock cycle. As flash memory capacities and system performance requirements grew, this became a critical bottleneck. **Multi-IO SPI** (also called **Extended SPI** or **Multi-line SPI**) solves this by using multiple I/O lines simultaneously to transfer data in parallel.

The two dominant variants are:

| Mode | I/O Lines | Data Bits/Clock | Common Names |
|------|-----------|-----------------|--------------|
| Dual SPI | 2 | 2 | DSPI |
| Quad SPI | 4 | 4 | QSPI, QPI, QuadSPI |
| Octal SPI | 8 | 8 | OSPI, OPI, OctalSPI |
| Octal DTR | 8 (both edges) | 16 | DOPI, Octal-DTR |

These modes are especially important for:

- **Execute-in-Place (XIP)**: Running code directly from external flash without copying to RAM.
- **NOR Flash storage**: High-bandwidth read-heavy workloads.
- **IoT and embedded systems**: Where external RAM/Flash augments limited on-chip memory.
- **Bootloaders**: Fast firmware loading from external flash at startup.

---

## 2. Standard SPI Recap

Classic SPI uses 4 wires:

```
Master          Slave (Flash)
  CS   ──────►  CS
  SCK  ──────►  SCK
  MOSI ──────►  SI   (Master Out, Slave In)
  MISO ◄──────  SO   (Master In, Slave Out)
```

Data rate: **1 bit per clock edge** (SDR), or **2 bits per clock** (DDR).

For a 100 MHz clock, maximum throughput is:
- SDR: 100 Mbit/s = **12.5 MB/s**
- DDR: 200 Mbit/s = **25 MB/s**

Multi-IO SPI replaces MOSI/MISO with multiple bidirectional I/O lines (IO0–IO7), multiplying throughput by 4x or 8x.

---

## 3. Multi-IO SPI Architecture

### 3.1 Dual SPI (DSPI)

Uses 2 I/O lines. Primarily used in the **data phase only** (address and command are still 1-bit). Provides a 2× data throughput improvement over standard SPI.

```
  IO0 ◄──────►  IO0 (was MOSI)
  IO1 ◄──────►  IO1 (was MISO)
```

### 3.2 Quad SPI (QSPI / QPI)

Uses 4 I/O lines. Two modes exist:

**Extended SPI mode (1-1-4):** Only the data phase uses 4 lines.
```
Command: 1-bit (IO0 only)
Address: 1-bit (IO0 only)
Data:    4-bit (IO0, IO1, IO2, IO3)
```

**QPI mode (4-4-4):** Command, address, and data all use 4 lines.
```
Command: 4-bit (IO0–IO3)
Address: 4-bit (IO0–IO3)
Data:    4-bit (IO0–IO3)
```

QPI mode requires a one-time initialization command to enable it in the flash chip (e.g., `0x38` or `0x35` depending on manufacturer).

### 3.3 Octal SPI (OSPI / OPI)

Uses 8 I/O lines. Two sub-modes:

**STR (Single Transfer Rate) — 8-8-8:**
- 8 bits transferred per clock cycle
- At 200 MHz: **200 MB/s** throughput

**DTR (Double Transfer Rate) — 8D-8D-8D:**
- 8 bits per clock **edge** (16 bits per cycle)
- At 200 MHz: **400 MB/s** throughput
- Requires careful PCB layout (impedance control, length matching)

OPI is common in HyperBus-compatible devices and modern NOR flash (e.g., Macronix MX25UM51245G, Winbond W25Q256JW).

---

## 4. Signal Lines and Pin Mapping

### QSPI Pin Assignments

```
Pin     Direction    Function
────────────────────────────────────────
nCS     Output       Chip Select (active low)
CLK     Output       Serial Clock
IO0     Bidir        Data bit 0 (MOSI in 1-bit mode)
IO1     Bidir        Data bit 1 (MISO in 1-bit mode)
IO2     Bidir        Data bit 2 (Write Protect in 1-bit mode)
IO3     Bidir        Data bit 3 (HOLD/RESET in 1-bit mode)
```

> **Important:** In standard SPI mode, IO2 is typically pulled high (WP# disabled) and IO3 pulled high (HOLD# disabled). These pins must be properly managed when switching to Quad mode.

### OPI Pin Assignments

```
Pin     Direction    Function
────────────────────────────────────────
nCS     Output       Chip Select (active low)
CLK     Output       Serial Clock (STR: use only rising edge)
DQSDM  Input        Data Strobe (DTR mode only — source-synchronous)
IO0–IO7 Bidir       8-bit parallel data bus
```

In **DTR mode**, the DQS (Data Strobe) signal is used instead of CLK for data capture on the master side. This eliminates setup/hold timing problems at high frequencies.

---

## 5. Command Structure and Protocol Phases

A Multi-IO SPI transaction has up to 5 phases:

```
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│ COMMAND  │ ADDRESS  │ ALT BYTE │  DUMMY   │   DATA   │
│ (1–8 IO) │ (1–8 IO) │ (opt.)   │ (cycles) │ (1–8 IO) │
└──────────┴──────────┴──────────┴──────────┴──────────┘
```

The notation **W-X-Y** (or **W-X-Y-Z**) is commonly used:

```
W = I/O lines for Command
X = I/O lines for Address
Y = I/O lines for Data

Examples:
  1-1-1   Standard SPI
  1-1-2   Dual output (data only, 2 lines)
  1-1-4   Quad output (data only, 4 lines)  ← most common "QSPI read"
  1-4-4   Quad I/O (addr+data, 4 lines)
  4-4-4   Full QPI mode
  8-8-8   Full OPI STR mode
  8D-8D-8D  OPI DTR mode
```

### 5.1 STR vs DTR (Single vs Double Transfer Rate)

```
STR Mode (data captured on rising edge only):
  CLK  ─┐ ┌─┐ ┌─┐ ┌─┐
         └─┘ └─┘ └─┘ └─
  DATA  [D0  ][D1  ][D2  ][D3  ]
  Capture: ↑   ↑   ↑   ↑

DTR Mode (data captured on both edges):
  CLK  ─┐ ┌─┐ ┌─┐ ┌─┐
         └─┘ └─┘ └─┘ └─
  DATA  [D0][D1][D2][D3][D4][D5][D6][D7]
  Capture: ↑  ↓  ↑  ↓  ↑  ↓  ↑  ↓
```

DTR doubles throughput at the same clock frequency, but requires DQS strobe synchronization and tighter PCB design rules.

### 5.2 Dummy Cycles

After the address (and optional alternate byte), **dummy cycles** are inserted to allow the flash chip time to prepare data. This accounts for internal access latency (tACC).

```
Typical dummy cycles for QSPI fast read:
  Clock frequency 0–50  MHz: 0 dummy cycles
  Clock frequency 50–80 MHz: 4 dummy cycles
  Clock frequency 80–104 MHz: 6 dummy cycles
  Clock frequency >104 MHz:  8 dummy cycles (check datasheet)
```

Incorrect dummy cycles cause garbled or all-0xFF reads — a very common bug!

### 5.3 Address Modes (3-byte vs 4-byte)

```
3-Byte Addressing: supports up to 16 MB (2^24 bytes)
4-Byte Addressing: supports up to 4 GB (2^32 bytes)
```

Devices larger than 16 MB require 4-byte addressing. Some devices use a Bank Register to select 16 MB regions (legacy mode). Most modern devices support 4-byte address commands directly (e.g., `0x0C` instead of `0x0B` for fast read).

---

## 6. Flash Memory Commands Reference

### Common QSPI Flash Commands (JEDEC Standard)

```
Command  Hex   Mode    Description
─────────────────────────────────────────────────────────
RDID     0x9F  1-1-1   Read JEDEC ID (Manufacturer + Device)
RDSR1    0x05  1-1-1   Read Status Register 1
RDSR2    0x35  1-1-1   Read Status Register 2
WRSR     0x01  1-1-1   Write Status Register (enable QUAD bit)
WREN     0x06  1-1-1   Write Enable
WRDI     0x04  1-1-1   Write Disable
READ     0x03  1-1-1   Normal Read (max ~50 MHz)
FAST_RD  0x0B  1-1-1   Fast Read with dummy cycles
DREAD    0x3B  1-1-2   Dual Output Fast Read
QREAD    0x6B  1-1-4   Quad Output Fast Read    ← most used
QIO_RD   0xEB  1-4-4   Quad I/O Fast Read
QPI_RD   0xEB  4-4-4   QPI Mode Read
PP       0x02  1-1-1   Page Program (256 bytes)
QPP      0x32  1-1-4   Quad Page Program
SE       0x20  1-1-1   Sector Erase (4 KB)
BE32     0x52  1-1-1   Block Erase (32 KB)
BE64     0xD8  1-1-1   Block Erase (64 KB)
CE       0xC7  1-1-1   Chip Erase
ENT_QPI  0x38  1-1-1   Enter QPI Mode (Winbond)
EXT_QPI  0xFF  4-4-4   Exit QPI Mode
```

### OPI-Specific Commands (Macronix Example)

```
Command  Hex         Mode    Description
──────────────────────────────────────────────────────────────
8READ    0xEC13      8-8-8   OPI STR Read
8DREAD   0xEE11      8D-8D-8D OPI DTR Read
8PP      0x12ED      8-8-8   OPI Page Program
RDCR2    0x71        1-1-1   Read Config Register 2 (OPI enable bit)
WRCR2    0x72        1-1-1   Write Config Register 2 (enable OPI)
```

---

## 7. Performance Analysis

Theoretical maximum throughput at 100 MHz clock:

```
Mode          Lines  SDR/DTR  Throughput
──────────────────────────────────────────────────
Standard SPI    1    SDR      12.5 MB/s
Dual SPI        2    SDR      25.0 MB/s
Quad SPI        4    SDR      50.0 MB/s
Quad SPI        4    DDR      100.0 MB/s
Octal SPI       8    STR      100.0 MB/s
Octal SPI       8    DTR      200.0 MB/s

At 200 MHz (OPI DTR):          400.0 MB/s
```

Real-world throughput is reduced by:
- Command + Address + Dummy phase overhead (typically 5–10%)
- CS assertion/deassertion latency
- DMA transfer overhead
- PCB signal integrity limits at high frequencies

---

## 8. Hardware Considerations

### 8.1 QSPI Controller (XSPI/QSPI IP)

Most modern MCUs and SoCs include a dedicated QSPI/OSPI controller with:

- **Indirect mode**: CPU or DMA issues commands and transfers data through FIFOs.
- **Automatic polling mode**: Hardware polls a status register bit (e.g., WIP — Write In Progress) and interrupts when done.
- **Memory-mapped mode**: Flash appears as a contiguous address range in the CPU's memory map (XIP).

STM32 examples: QUADSPI (F4/F7/H7), OCTOSPI (H7/U5/WB55), XSPI (H5/N6).

### 8.2 Memory-Mapped (XIP) Mode

In XIP (eXecute-In-Place) mode, the QSPI controller automatically issues read commands whenever the CPU accesses the mapped address region. No software intervention is needed per read.

```
CPU address 0x90000000 → QSPI controller → Flash chip
                                           read command issued automatically
```

This allows code to run directly from external flash — critical for MCUs with limited internal flash.

---

## 9. Programming: C/C++

### 9.1 Low-Level Register Driver (STM32 QSPI)

```c
/**
 * @file  qspi_lowlevel.c
 * @brief Low-level QSPI register driver for STM32 QUADSPI peripheral.
 *        Demonstrates direct register access for educational clarity.
 *        Target: STM32F746 / STM32H743.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ─── QUADSPI Register Map (STM32H7) ─── */
typedef struct {
    volatile uint32_t CR;       /* Control Register                     0x00 */
    volatile uint32_t DCR;      /* Device Configuration Register        0x04 */
    volatile uint32_t SR;       /* Status Register                      0x08 */
    volatile uint32_t FCR;      /* Flag Clear Register                  0x0C */
    volatile uint32_t DLR;      /* Data Length Register                 0x10 */
    volatile uint32_t CCR;      /* Communication Configuration Register 0x14 */
    volatile uint32_t AR;       /* Address Register                     0x18 */
    volatile uint32_t ABR;      /* Alternate Bytes Register             0x1C */
    volatile uint32_t DR;       /* Data Register                        0x20 */
    volatile uint32_t PSMKR;    /* Polling Status Mask Register         0x24 */
    volatile uint32_t PSMAR;    /* Polling Status Match Register        0x28 */
    volatile uint32_t PIR;      /* Polling Interval Register            0x2C */
    volatile uint32_t LPTR;     /* Low-Power Timeout Register           0x30 */
} QUADSPI_TypeDef;

#define QUADSPI_BASE    0xA0001000UL
#define QUADSPI         ((QUADSPI_TypeDef *)QUADSPI_BASE)

/* CR Bits */
#define QUADSPI_CR_EN           (1U << 0)   /* QSPI Enable              */
#define QUADSPI_CR_ABORT        (1U << 1)   /* Abort request            */
#define QUADSPI_CR_DMAEN        (1U << 2)   /* DMA Enable               */
#define QUADSPI_CR_TCEN         (1U << 3)   /* Timeout counter enable   */
#define QUADSPI_CR_SSHIFT       (1U << 4)   /* Sample shift (half cycle)*/
#define QUADSPI_CR_FTHRES_Pos   8U
#define QUADSPI_CR_FTHRES_Msk   (0x1FU << QUADSPI_CR_FTHRES_Pos)

/* SR Bits */
#define QUADSPI_SR_TEF          (1U << 0)   /* Transfer error flag      */
#define QUADSPI_SR_TCF          (1U << 1)   /* Transfer complete flag   */
#define QUADSPI_SR_FTF          (1U << 2)   /* FIFO threshold flag      */
#define QUADSPI_SR_SMF          (1U << 3)   /* Status match flag        */
#define QUADSPI_SR_TOF          (1U << 4)   /* Timeout flag             */
#define QUADSPI_SR_BUSY         (1U << 5)   /* Busy flag                */

/* CCR Mode encoding (IMODE, ADMODE, ABMODE, DMODE) */
#define CCR_MODE_NONE           0U          /* No lines (phase skipped) */
#define CCR_MODE_1LINE          1U          /* 1 line (standard SPI)    */
#define CCR_MODE_2LINES         2U          /* 2 lines (dual SPI)       */
#define CCR_MODE_4LINES         3U          /* 4 lines (quad SPI)       */

/* CCR Bit fields */
#define CCR_INSTRUCTION_Pos     0U
#define CCR_IMODE_Pos           8U
#define CCR_ADMODE_Pos          10U
#define CCR_ADSIZE_Pos          12U         /* 0=8b,1=16b,2=24b,3=32b   */
#define CCR_ABMODE_Pos          14U
#define CCR_ABSIZE_Pos          16U
#define CCR_DCYC_Pos            18U         /* Dummy cycles (0–31)      */
#define CCR_DMODE_Pos           24U
#define CCR_FMODE_Pos           26U         /* 0=indirect write,        */
                                            /* 1=indirect read,         */
                                            /* 2=auto polling,          */
                                            /* 3=memory mapped          */
#define CCR_FMODE_INDIRECT_WRITE 0U
#define CCR_FMODE_INDIRECT_READ  1U
#define CCR_FMODE_AUTO_POLLING   2U
#define CCR_FMODE_MEM_MAPPED     3U

/* Flash commands */
#define CMD_WRITE_ENABLE        0x06U
#define CMD_READ_STATUS1        0x05U
#define CMD_WRITE_STATUS        0x01U
#define CMD_QUAD_ENABLE_SR2     0x31U       /* Write SR2 with QUAD bit  */
#define CMD_READ_JEDEC_ID       0x9FU
#define CMD_QUAD_IO_FAST_READ   0xEBU       /* 1-4-4 read               */
#define CMD_QUAD_PAGE_PROGRAM   0x32U       /* 1-1-4 page program       */
#define CMD_SECTOR_ERASE        0x20U       /* 4 KB sector erase        */
#define CMD_BLOCK_ERASE_64K     0xD8U
#define CMD_CHIP_ERASE          0xC7U
#define CMD_ENTER_QPI           0x38U
#define CMD_EXIT_QPI            0xFFU
#define CMD_RESET_ENABLE        0x66U
#define CMD_RESET_MEMORY        0x99U

#define FLASH_STATUS_WIP        (1U << 0)   /* Write In Progress bit    */
#define FLASH_STATUS_WEL        (1U << 1)   /* Write Enable Latch       */
#define FLASH_STATUS_QUAD       (1U << 1)   /* QUAD bit in SR2          */

#define QSPI_TIMEOUT_MS         5000U

/* ─── Helper: busy-wait until not busy ─── */
static void qspi_wait_not_busy(void)
{
    /* In production: add a timeout counter to avoid infinite loops */
    while (QUADSPI->SR & QUADSPI_SR_BUSY) { /* spin */ }
}

static void qspi_clear_flags(void)
{
    QUADSPI->FCR = QUADSPI_SR_TEF | QUADSPI_SR_TCF |
                   QUADSPI_SR_SMF | QUADSPI_SR_TOF;
}

/* ─── Initialize QSPI peripheral ─── */
void qspi_init(void)
{
    /* Assumes RCC clock to QSPI already enabled by caller */
    qspi_wait_not_busy();

    /* CR: prescaler /2 (QSPI clock = AHB/2), FIFO threshold = 4 */
    QUADSPI->CR = (1U << 24)   /* PRESCALER = 1  → divide by 2      */
                | (3U << QUADSPI_CR_FTHRES_Pos)  /* FIFO threshold 4 */
                | QUADSPI_CR_SSHIFT              /* Sample shift      */
                | QUADSPI_CR_EN;                 /* Enable            */

    /* DCR: Flash size = 2^(25) = 32 MB (FSIZE = 24 → size = 2^(24+1)) */
    QUADSPI->DCR = (24U << 16) /* FSIZE: 2^25 bytes = 32 MB         */
                 | (1U << 8)   /* CSHT: CS high time = 2 clocks      */
                 | (0U << 0);  /* CKMODE 0: CLK low when idle        */
}

/* ─── Send command with no data (e.g., WREN, Chip Erase) ─── */
void qspi_command_only(uint8_t cmd, uint8_t imode)
{
    qspi_wait_not_busy();
    qspi_clear_flags();

    QUADSPI->CCR = ((uint32_t)cmd          << CCR_INSTRUCTION_Pos)
                 | ((uint32_t)imode        << CCR_IMODE_Pos)
                 | (CCR_MODE_NONE          << CCR_ADMODE_Pos)
                 | (CCR_MODE_NONE          << CCR_DMODE_Pos)
                 | (CCR_FMODE_INDIRECT_WRITE << CCR_FMODE_Pos);

    /* Wait for completion (no data phase) */
    while (!(QUADSPI->SR & QUADSPI_SR_TCF)) { /* spin */ }
    qspi_clear_flags();
}

/* ─── Read N bytes via QSPI (1-1-4 Quad Output) ─── */
int qspi_quad_read(uint32_t address, uint8_t *buf, uint32_t len, uint8_t dummy_cycles)
{
    if (!buf || !len) return -1;

    qspi_wait_not_busy();
    qspi_clear_flags();

    /* Set data length (hardware counts from DLR down to 0) */
    QUADSPI->DLR = len - 1U;

    /*
     * CCR for 1-1-4 Quad Output Fast Read (0x6B):
     *   IMODE  = 1 line (command on IO0)
     *   ADMODE = 1 line (address on IO0)
     *   ADSIZE = 24-bit (3-byte address)
     *   DCYC   = dummy_cycles
     *   DMODE  = 4 lines (data on IO0–IO3)
     *   FMODE  = Indirect read
     */
    QUADSPI->CCR = (CMD_QUAD_IO_FAST_READ   << CCR_INSTRUCTION_Pos)
                 | (CCR_MODE_1LINE           << CCR_IMODE_Pos)
                 | (CCR_MODE_4LINES          << CCR_ADMODE_Pos)  /* 1-4-4 */
                 | (2U                       << CCR_ADSIZE_Pos)  /* 24-bit addr */
                 | ((uint32_t)dummy_cycles   << CCR_DCYC_Pos)
                 | (CCR_MODE_4LINES          << CCR_DMODE_Pos)
                 | (CCR_FMODE_INDIRECT_READ  << CCR_FMODE_Pos);

    /* Trigger transfer by writing address register */
    QUADSPI->AR = address;

    /* Read data from FIFO byte-by-byte (DMA preferred in production) */
    volatile uint8_t *dr_byte = (volatile uint8_t *)&QUADSPI->DR;
    for (uint32_t i = 0; i < len; i++) {
        while (!(QUADSPI->SR & QUADSPI_SR_FTF) &&
               !(QUADSPI->SR & QUADSPI_SR_TCF)) { /* wait for FIFO */ }
        buf[i] = *dr_byte;
    }

    while (!(QUADSPI->SR & QUADSPI_SR_TCF)) { /* wait complete */ }
    qspi_clear_flags();
    return 0;
}

/* ─── Write enable latch ─── */
void qspi_write_enable(void)
{
    qspi_command_only(CMD_WRITE_ENABLE, CCR_MODE_1LINE);
}

/* ─── Auto-polling: wait for flash WIP bit to clear ─── */
void qspi_auto_poll_ready(void)
{
    qspi_wait_not_busy();
    qspi_clear_flags();

    QUADSPI->DLR  = 0U;         /* Match on 1 byte */
    QUADSPI->PSMKR = FLASH_STATUS_WIP;   /* Mask: only check bit 0 */
    QUADSPI->PSMAR = 0x00U;              /* Match: WIP = 0 (not busy) */
    QUADSPI->PIR   = 0x0010U;           /* Poll every 16 cycles */

    QUADSPI->CCR = (CMD_READ_STATUS1     << CCR_INSTRUCTION_Pos)
                 | (CCR_MODE_1LINE       << CCR_IMODE_Pos)
                 | (CCR_MODE_NONE        << CCR_ADMODE_Pos)
                 | (CCR_MODE_1LINE       << CCR_DMODE_Pos)
                 | (CCR_FMODE_AUTO_POLLING << CCR_FMODE_Pos);

    /* Block until status match (WIP cleared) */
    while (!(QUADSPI->SR & QUADSPI_SR_SMF)) { /* spin */ }
    qspi_clear_flags();
}

/* ─── Quad Page Program (1-1-4): write up to 256 bytes ─── */
int qspi_quad_page_program(uint32_t address, const uint8_t *data, uint32_t len)
{
    if (!data || len == 0 || len > 256) return -1;

    qspi_write_enable();
    qspi_wait_not_busy();
    qspi_clear_flags();

    QUADSPI->DLR = len - 1U;

    QUADSPI->CCR = (CMD_QUAD_PAGE_PROGRAM   << CCR_INSTRUCTION_Pos)
                 | (CCR_MODE_1LINE          << CCR_IMODE_Pos)
                 | (CCR_MODE_1LINE          << CCR_ADMODE_Pos)
                 | (2U                      << CCR_ADSIZE_Pos)   /* 24-bit */
                 | (0U                      << CCR_DCYC_Pos)     /* no dummy */
                 | (CCR_MODE_4LINES         << CCR_DMODE_Pos)
                 | (CCR_FMODE_INDIRECT_WRITE << CCR_FMODE_Pos);

    QUADSPI->AR = address;

    volatile uint8_t *dr_byte = (volatile uint8_t *)&QUADSPI->DR;
    for (uint32_t i = 0; i < len; i++) {
        while (!(QUADSPI->SR & QUADSPI_SR_FTF)) { /* wait for FIFO space */ }
        *dr_byte = data[i];
    }

    while (!(QUADSPI->SR & QUADSPI_SR_TCF)) { /* wait for send complete */ }
    qspi_clear_flags();

    /* Wait for flash to finish programming */
    qspi_auto_poll_ready();
    return 0;
}

/* ─── Sector erase (4 KB) ─── */
int qspi_sector_erase(uint32_t address)
{
    qspi_write_enable();
    qspi_wait_not_busy();
    qspi_clear_flags();

    QUADSPI->CCR = (CMD_SECTOR_ERASE        << CCR_INSTRUCTION_Pos)
                 | (CCR_MODE_1LINE          << CCR_IMODE_Pos)
                 | (CCR_MODE_1LINE          << CCR_ADMODE_Pos)
                 | (2U                      << CCR_ADSIZE_Pos)
                 | (CCR_MODE_NONE           << CCR_DMODE_Pos)
                 | (CCR_FMODE_INDIRECT_WRITE << CCR_FMODE_Pos);

    QUADSPI->AR = address;

    while (!(QUADSPI->SR & QUADSPI_SR_TCF)) { /* spin */ }
    qspi_clear_flags();

    qspi_auto_poll_ready();
    return 0;
}
```

---

### 9.2 HAL-Based QSPI Flash Driver

```c
/**
 * @file  w25q_qspi_hal.c
 * @brief Winbond W25Q256JV QSPI flash driver using STM32 HAL.
 *        Demonstrates a portable, production-ready HAL approach.
 */

#include "stm32h7xx_hal.h"
#include "w25q_qspi_hal.h"
#include <string.h>

/* Flash geometry */
#define W25Q_JEDEC_ID           0xEF4019U   /* Winbond W25Q256 */
#define W25Q_PAGE_SIZE          256U
#define W25Q_SECTOR_SIZE        4096U
#define W25Q_BLOCK_SIZE_32K     (32U * 1024U)
#define W25Q_BLOCK_SIZE_64K     (64U * 1024U)
#define W25Q_FLASH_SIZE         (32U * 1024U * 1024U)  /* 32 MB */

/* Commands */
#define W25Q_CMD_READ_ID        0x9FU
#define W25Q_CMD_WRITE_ENABLE   0x06U
#define W25Q_CMD_READ_SR1       0x05U
#define W25Q_CMD_READ_SR2       0x35U
#define W25Q_CMD_WRITE_SR       0x01U
#define W25Q_CMD_QREAD          0x6BU   /* Quad Output Read       1-1-4 */
#define W25Q_CMD_QIO_READ       0xEBU   /* Quad I/O Read          1-4-4 */
#define W25Q_CMD_QPP            0x32U   /* Quad Page Program      1-1-4 */
#define W25Q_CMD_SECTOR_ERASE   0x20U
#define W25Q_CMD_BLOCK_ERASE    0xD8U
#define W25Q_CMD_CHIP_ERASE     0xC7U

#define W25Q_SR1_BUSY           0x01U
#define W25Q_SR2_QUAD           0x02U   /* QE bit in Status Reg 2 */

#define QSPI_TIMEOUT            5000U

/* ─── Internal: send instruction only ─── */
static HAL_StatusTypeDef w25q_send_cmd(QSPI_HandleTypeDef *hqspi,
                                       uint8_t instruction,
                                       uint32_t imode)
{
    QSPI_CommandTypeDef cmd = {
        .Instruction       = instruction,
        .InstructionMode   = imode,
        .AddressMode       = QSPI_ADDRESS_NONE,
        .AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE,
        .DummyCycles       = 0,
        .DataMode          = QSPI_DATA_NONE,
        .DdrMode           = QSPI_DDR_MODE_DISABLE,
        .SIOOMode          = QSPI_SIOO_INST_EVERY_CMD,
    };
    return HAL_QSPI_Command(hqspi, &cmd, QSPI_TIMEOUT);
}

/* ─── Enable QUAD mode in flash Status Register 2 ─── */
HAL_StatusTypeDef w25q_enable_quad_mode(QSPI_HandleTypeDef *hqspi)
{
    uint8_t sr[2] = {0};
    QSPI_CommandTypeDef cmd;

    /* Read SR1 */
    cmd.Instruction     = W25Q_CMD_READ_SR1;
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.AddressMode     = QSPI_ADDRESS_NONE;
    cmd.DataMode        = QSPI_DATA_1_LINE;
    cmd.NbData          = 1;
    cmd.DummyCycles     = 0;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DdrMode         = QSPI_DDR_MODE_DISABLE;
    cmd.SIOOMode        = QSPI_SIOO_INST_EVERY_CMD;
    HAL_QSPI_Command(hqspi, &cmd, QSPI_TIMEOUT);
    HAL_QSPI_Receive(hqspi, &sr[0], QSPI_TIMEOUT);

    /* Read SR2 */
    cmd.Instruction = W25Q_CMD_READ_SR2;
    HAL_QSPI_Command(hqspi, &cmd, QSPI_TIMEOUT);
    HAL_QSPI_Receive(hqspi, &sr[1], QSPI_TIMEOUT);

    /* Already enabled? */
    if (sr[1] & W25Q_SR2_QUAD) return HAL_OK;

    /* Write Enable */
    w25q_send_cmd(hqspi, W25Q_CMD_WRITE_ENABLE, QSPI_INSTRUCTION_1_LINE);

    /* Write SR1+SR2 with QE bit set */
    sr[1] |= W25Q_SR2_QUAD;
    cmd.Instruction = W25Q_CMD_WRITE_SR;
    cmd.DataMode    = QSPI_DATA_1_LINE;
    cmd.NbData      = 2;
    HAL_QSPI_Command(hqspi, &cmd, QSPI_TIMEOUT);
    HAL_QSPI_Transmit(hqspi, sr, QSPI_TIMEOUT);

    /* Wait for SR write to complete */
    QSPI_AutoPollingTypeDef poll = {
        .Match     = 0x00,
        .Mask      = W25Q_SR1_BUSY,
        .MatchMode = QSPI_MATCH_MODE_AND,
        .Interval  = 0x10,
        .AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE,
        .StatusBytesSize = 1,
    };
    cmd.Instruction = W25Q_CMD_READ_SR1;
    cmd.NbData      = 1;
    return HAL_QSPI_AutoPolling(hqspi, &cmd, &poll, QSPI_TIMEOUT);
}

/* ─── Quad Output Fast Read (1-1-4, 0x6B) ─── */
HAL_StatusTypeDef w25q_quad_read(QSPI_HandleTypeDef *hqspi,
                                 uint32_t address,
                                 uint8_t *buffer,
                                 uint32_t length)
{
    QSPI_CommandTypeDef cmd = {
        .Instruction       = W25Q_CMD_QREAD,
        .InstructionMode   = QSPI_INSTRUCTION_1_LINE,
        .Address           = address,
        .AddressSize       = QSPI_ADDRESS_24_BITS,
        .AddressMode       = QSPI_ADDRESS_1_LINE,
        .AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE,
        .DummyCycles       = 8,            /* W25Q256: 8 dummy at >80 MHz */
        .DataMode          = QSPI_DATA_4_LINES,
        .NbData            = length,
        .DdrMode           = QSPI_DDR_MODE_DISABLE,
        .SIOOMode          = QSPI_SIOO_INST_EVERY_CMD,
    };

    HAL_StatusTypeDef ret = HAL_QSPI_Command(hqspi, &cmd, QSPI_TIMEOUT);
    if (ret != HAL_OK) return ret;
    return HAL_QSPI_Receive(hqspi, buffer, QSPI_TIMEOUT);
}

/* ─── Quad Page Program (1-1-4) with automatic 256-byte chunking ─── */
HAL_StatusTypeDef w25q_write(QSPI_HandleTypeDef *hqspi,
                             uint32_t address,
                             const uint8_t *data,
                             uint32_t length)
{
    QSPI_AutoPollingTypeDef poll = {
        .Match           = 0x00,
        .Mask            = W25Q_SR1_BUSY,
        .MatchMode       = QSPI_MATCH_MODE_AND,
        .Interval        = 0x10,
        .AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE,
        .StatusBytesSize = 1,
    };

    while (length > 0) {
        /* Compute bytes until next page boundary */
        uint32_t page_offset = address % W25Q_PAGE_SIZE;
        uint32_t chunk = W25Q_PAGE_SIZE - page_offset;
        if (chunk > length) chunk = length;

        /* Write Enable */
        w25q_send_cmd(hqspi, W25Q_CMD_WRITE_ENABLE, QSPI_INSTRUCTION_1_LINE);

        QSPI_CommandTypeDef cmd = {
            .Instruction       = W25Q_CMD_QPP,
            .InstructionMode   = QSPI_INSTRUCTION_1_LINE,
            .Address           = address,
            .AddressSize       = QSPI_ADDRESS_24_BITS,
            .AddressMode       = QSPI_ADDRESS_1_LINE,
            .AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE,
            .DummyCycles       = 0,
            .DataMode          = QSPI_DATA_4_LINES,
            .NbData            = chunk,
            .DdrMode           = QSPI_DDR_MODE_DISABLE,
            .SIOOMode          = QSPI_SIOO_INST_EVERY_CMD,
        };

        HAL_StatusTypeDef ret = HAL_QSPI_Command(hqspi, &cmd, QSPI_TIMEOUT);
        if (ret != HAL_OK) return ret;
        ret = HAL_QSPI_Transmit(hqspi, (uint8_t *)data, QSPI_TIMEOUT);
        if (ret != HAL_OK) return ret;

        /* Poll until write done */
        cmd.Instruction = W25Q_CMD_READ_SR1;
        cmd.AddressMode = QSPI_ADDRESS_NONE;
        cmd.DataMode    = QSPI_DATA_1_LINE;
        cmd.NbData      = 1;
        cmd.DummyCycles = 0;
        ret = HAL_QSPI_AutoPolling(hqspi, &cmd, &poll, QSPI_TIMEOUT);
        if (ret != HAL_OK) return ret;

        address += chunk;
        data    += chunk;
        length  -= chunk;
    }
    return HAL_OK;
}

/* ─── Sector erase (4 KB) ─── */
HAL_StatusTypeDef w25q_sector_erase(QSPI_HandleTypeDef *hqspi, uint32_t address)
{
    w25q_send_cmd(hqspi, W25Q_CMD_WRITE_ENABLE, QSPI_INSTRUCTION_1_LINE);

    QSPI_CommandTypeDef cmd = {
        .Instruction       = W25Q_CMD_SECTOR_ERASE,
        .InstructionMode   = QSPI_INSTRUCTION_1_LINE,
        .Address           = address & ~(W25Q_SECTOR_SIZE - 1U),
        .AddressSize       = QSPI_ADDRESS_24_BITS,
        .AddressMode       = QSPI_ADDRESS_1_LINE,
        .AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE,
        .DummyCycles       = 0,
        .DataMode          = QSPI_DATA_NONE,
        .DdrMode           = QSPI_DDR_MODE_DISABLE,
        .SIOOMode          = QSPI_SIOO_INST_EVERY_CMD,
    };
    HAL_StatusTypeDef ret = HAL_QSPI_Command(hqspi, &cmd, QSPI_TIMEOUT);
    if (ret != HAL_OK) return ret;

    QSPI_AutoPollingTypeDef poll = {
        .Match           = 0x00, .Mask = W25Q_SR1_BUSY,
        .MatchMode       = QSPI_MATCH_MODE_AND,
        .Interval        = 0x10, .AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE,
        .StatusBytesSize = 1,
    };
    cmd.Instruction = W25Q_CMD_READ_SR1;
    cmd.AddressMode = QSPI_ADDRESS_NONE;
    cmd.DataMode    = QSPI_DATA_1_LINE;
    cmd.NbData      = 1;
    return HAL_QSPI_AutoPolling(hqspi, &cmd, &poll, QSPI_TIMEOUT * 10);
}
```

---

### 9.3 OPI (Octal SPI) Driver Skeleton

```c
/**
 * @file  opi_driver.c
 * @brief Octal SPI (OPI) driver skeleton for STM32U5 OCTOSPI peripheral.
 *        Target device: Macronix MX25UM51245G (512 Mbit OPI flash).
 *        This demonstrates the STR (Single Transfer Rate) 8-8-8 mode.
 */

#include "stm32u5xx_hal.h"

/* Macronix MX25UM51245G OPI Commands (sent as 2-byte command in 8-8-8 mode) */
#define MX_CMD_RDID             0x9F60U     /* Read ID                   */
#define MX_CMD_RDCR2            0x7186U     /* Read Config Register 2    */
#define MX_CMD_WRCR2            0x728DU     /* Write Config Register 2   */
#define MX_CMD_WREN             0x06F9U     /* Write Enable              */
#define MX_CMD_RDSR             0x05FAU     /* Read Status Register      */
#define MX_CMD_READ_OPI         0xEC13U     /* OPI STR Read (8-8-8)      */
#define MX_CMD_READ_DOPI        0xEE11U     /* OPI DTR Read (8D-8D-8D)   */
#define MX_CMD_PP_OPI           0x12EDU     /* OPI Page Program          */
#define MX_CMD_SE_OPI           0x21DEU     /* OPI Sector Erase 4KB      */

/* CR2 register address for OPI enable */
#define MX_CR2_ADDR             0x00000000U
/* CR2 values: 0x00=SPI, 0x01=OPI-STR, 0x02=OPI-DTR */
#define MX_CR2_OPI_STR          0x01U
#define MX_CR2_OPI_DTR          0x02U

#define OPI_DUMMY_CYCLES_STR    20U         /* Typical for 200 MHz STR   */
#define OPI_TIMEOUT             10000U

extern OSPI_HandleTypeDef hospi1;           /* Configured by CubeMX      */

/* ─── Enable OPI STR mode on Macronix flash ─── */
HAL_StatusTypeDef opi_enable_str_mode(void)
{
    uint8_t cr2_val = MX_CR2_OPI_STR;

    /* Step 1: Write Enable (still in SPI mode) */
    OSPI_RegularCmdTypeDef cmd = {
        .OperationType     = HAL_OSPI_OPTYPE_COMMON_CFG,
        .Instruction       = 0x06,
        .InstructionMode   = HAL_OSPI_INSTRUCTION_1_LINE,
        .InstructionSize   = HAL_OSPI_INSTRUCTION_8_BITS,
        .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE,
        .AddressMode       = HAL_OSPI_ADDRESS_NONE,
        .DataMode          = HAL_OSPI_DATA_NONE,
        .DummyCycles       = 0,
        .DQSMode           = HAL_OSPI_DQS_DISABLE,
        .SIOOMode          = HAL_OSPI_SIOO_INST_EVERY_CMD,
    };
    HAL_OSPI_Command(&hospi1, &cmd, OPI_TIMEOUT);

    /* Step 2: Write CR2 = 0x01 to enable OPI STR */
    cmd.Instruction      = 0x72;            /* WRCR2 in SPI mode         */
    cmd.Address          = MX_CR2_ADDR;
    cmd.AddressMode      = HAL_OSPI_ADDRESS_1_LINE;
    cmd.AddressSize      = HAL_OSPI_ADDRESS_32_BITS;
    cmd.DataMode         = HAL_OSPI_DATA_1_LINE;
    cmd.NbData           = 1;
    HAL_OSPI_Command(&hospi1, &cmd, OPI_TIMEOUT);
    HAL_OSPI_Transmit(&hospi1, &cr2_val, OPI_TIMEOUT);

    /* Allow flash to switch mode (tREADY2 typically < 200 ns) */
    HAL_Delay(1);

    /* Step 3: Reconfigure OCTOSPI controller for 8-8-8 mode */
    /* (Done by reconfiguring hospi1 — omitted for brevity, use CubeMX) */
    return HAL_OK;
}

/* ─── OPI STR Read (8-8-8 mode, 2-byte command, 4-byte address) ─── */
HAL_StatusTypeDef opi_str_read(uint32_t address, uint8_t *buffer, uint32_t length)
{
    OSPI_RegularCmdTypeDef cmd = {
        .OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG,
        /* 2-byte command: 0xEC sent, 0x13 sent (MSB first on all 8 IO lines) */
        .Instruction        = MX_CMD_READ_OPI,
        .InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES,
        .InstructionSize    = HAL_OSPI_INSTRUCTION_16_BITS,
        .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE,

        .Address            = address,
        .AddressMode        = HAL_OSPI_ADDRESS_8_LINES,
        .AddressSize        = HAL_OSPI_ADDRESS_32_BITS,
        .AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_DISABLE,

        .AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE,
        .DummyCycles        = OPI_DUMMY_CYCLES_STR,

        .DataMode           = HAL_OSPI_DATA_8_LINES,
        .DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE,
        .NbData             = length,

        .DQSMode            = HAL_OSPI_DQS_DISABLE,
        .SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD,
    };

    HAL_StatusTypeDef ret = HAL_OSPI_Command(&hospi1, &cmd, OPI_TIMEOUT);
    if (ret != HAL_OK) return ret;
    return HAL_OSPI_Receive(&hospi1, buffer, OPI_TIMEOUT);
}

/* ─── OPI DTR Read (8D-8D-8D mode, with DQS data strobe) ─── */
HAL_StatusTypeDef opi_dtr_read(uint32_t address, uint8_t *buffer, uint32_t length)
{
    OSPI_RegularCmdTypeDef cmd = {
        .OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG,
        .Instruction        = MX_CMD_READ_DOPI,      /* 0xEE11 */
        .InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES,
        .InstructionSize    = HAL_OSPI_INSTRUCTION_16_BITS,
        .InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_ENABLE, /* Both edges */

        .Address            = address,
        .AddressMode        = HAL_OSPI_ADDRESS_8_LINES,
        .AddressSize        = HAL_OSPI_ADDRESS_32_BITS,
        .AddressDtrMode     = HAL_OSPI_ADDRESS_DTR_ENABLE,

        .AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE,
        .DummyCycles        = 20U,           /* Datasheet value for DTR   */

        .DataMode           = HAL_OSPI_DATA_8_LINES,
        .DataDtrMode        = HAL_OSPI_DATA_DTR_ENABLE,   /* Both edges   */
        .NbData             = length,

        .DQSMode            = HAL_OSPI_DQS_ENABLE,  /* Use DQS strobe    */
        .SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD,
    };

    HAL_StatusTypeDef ret = HAL_OSPI_Command(&hospi1, &cmd, OPI_TIMEOUT);
    if (ret != HAL_OK) return ret;
    return HAL_OSPI_Receive(&hospi1, buffer, OPI_TIMEOUT);
}
```

---

### 9.4 Memory-Mapped XIP Mode (C)

```c
/**
 * @file  qspi_xip.c
 * @brief Configure QSPI in Memory-Mapped (XIP) mode.
 *        After this, the flash is accessible at 0x90000000 as normal memory.
 */

#include "stm32h7xx_hal.h"

#define XIP_BASE_ADDRESS    0x90000000UL
#define CMD_QIO_READ        0xEBU
#define DUMMY_CYCLES_READ   6U

extern QSPI_HandleTypeDef hqspi;

HAL_StatusTypeDef qspi_enable_memory_mapped(void)
{
    QSPI_CommandTypeDef      cmd;
    QSPI_MemoryMappedTypeDef cfg;

    /*
     * Command configuration for memory-mapped read.
     * The controller issues this command automatically for every read.
     * Using 1-4-4 Quad I/O for best performance (address + data on 4 lines).
     */
    cmd.InstructionMode    = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction        = CMD_QIO_READ;
    cmd.AddressMode        = QSPI_ADDRESS_4_LINES;
    cmd.AddressSize        = QSPI_ADDRESS_24_BITS;
    cmd.AlternateByteMode  = QSPI_ALTERNATE_BYTES_4_LINES;
    cmd.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    cmd.AlternateBytes     = 0xF0;          /* Mode bits: prevent continuous mode exit */
    cmd.DummyCycles        = DUMMY_CYCLES_READ;
    cmd.DataMode           = QSPI_DATA_4_LINES;
    cmd.DdrMode            = QSPI_DDR_MODE_DISABLE;
    cmd.SIOOMode           = QSPI_SIOO_INST_ONLY_FIRST_CMD; /* Send cmd once  */

    /* Memory-mapped timeout: deassert CS after 0x40 idle cycles */
    cfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_ENABLE;
    cfg.TimeOutPeriod     = 0x40;

    return HAL_QSPI_MemoryMapped(&hqspi, &cmd, &cfg);
}

/* ─── Usage: access flash as normal memory ─── */
void xip_usage_example(void)
{
    qspi_enable_memory_mapped();

    /* Flash is now at 0x90000000 — any read access issues a QSPI command */
    const uint8_t  *byte_ptr  = (const uint8_t  *)XIP_BASE_ADDRESS;
    const uint32_t *word_ptr  = (const uint32_t *)XIP_BASE_ADDRESS;

    uint8_t  first_byte = byte_ptr[0];
    uint32_t first_word = word_ptr[0];

    (void)first_byte;
    (void)first_word;

    /*
     * You can also call functions stored in flash:
     *   typedef void (*flash_func_t)(void);
     *   flash_func_t fn = (flash_func_t)(XIP_BASE_ADDRESS | 0x01); // Thumb bit
     *   fn();
     *
     * Or use linker script to place code sections in external flash region.
     */
}
```

---

## 10. Programming: Rust

### 10.1 Rust HAL Trait Abstractions

```rust
//! qspi_traits.rs
//!
//! Trait definitions for a portable QSPI/OPI flash driver in Rust.
//! Designed for embedded-hal style composability.

use core::fmt;

/// I/O line width for a transaction phase.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IoWidth {
    None,       // Phase absent
    Single,     // 1-bit (standard SPI)
    Dual,       // 2-bit
    Quad,       // 4-bit
    Octal,      // 8-bit
}

/// Transfer rate mode.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransferRate {
    Sdr,    // Single Transfer Rate (data on rising edge)
    Dtr,    // Double Transfer Rate (data on both edges)
}

/// Address width in bytes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AddressSize {
    ThreeByte,   // 24-bit addressing (up to 16 MB)
    FourByte,    // 32-bit addressing (up to 4 GB)
}

/// Complete description of a multi-IO SPI transaction.
#[derive(Debug, Clone)]
pub struct QspiCommand {
    pub instruction:    Option<u16>,         // None = skip command phase
    pub instruction_io: IoWidth,
    pub address:        Option<u32>,         // None = skip address phase
    pub address_io:     IoWidth,
    pub address_size:   AddressSize,
    pub alt_bytes:      Option<u8>,          // Alternate/mode bytes
    pub alt_io:         IoWidth,
    pub dummy_cycles:   u8,
    pub data_io:        IoWidth,
    pub data_len:       u32,
    pub rate:           TransferRate,
}

impl QspiCommand {
    /// Shorthand constructor for a standard write command (no data).
    pub fn write_only(instruction: u8) -> Self {
        Self {
            instruction:    Some(instruction as u16),
            instruction_io: IoWidth::Single,
            address:        None,
            address_io:     IoWidth::None,
            address_size:   AddressSize::ThreeByte,
            alt_bytes:      None,
            alt_io:         IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::None,
            data_len:       0,
            rate:           TransferRate::Sdr,
        }
    }

    /// Shorthand for a quad read command (1-4-4 format).
    pub fn quad_read(address: u32, length: u32, dummy: u8) -> Self {
        Self {
            instruction:    Some(0xEB),
            instruction_io: IoWidth::Single,
            address:        Some(address),
            address_io:     IoWidth::Quad,
            address_size:   AddressSize::ThreeByte,
            alt_bytes:      Some(0xF0),         // Mode bits
            alt_io:         IoWidth::Quad,
            dummy_cycles:   dummy,
            data_io:        IoWidth::Quad,
            data_len:       length,
            rate:           TransferRate::Sdr,
        }
    }

    /// Shorthand for an OPI STR read command.
    pub fn opi_str_read(address: u32, length: u32, dummy: u8) -> Self {
        Self {
            instruction:    Some(0xEC13),       // 16-bit command
            instruction_io: IoWidth::Octal,
            address:        Some(address),
            address_io:     IoWidth::Octal,
            address_size:   AddressSize::FourByte,
            alt_bytes:      None,
            alt_io:         IoWidth::None,
            dummy_cycles:   dummy,
            data_io:        IoWidth::Octal,
            data_len:       length,
            rate:           TransferRate::Sdr,
        }
    }
}

/// Error type for QSPI operations.
#[derive(Debug)]
pub enum QspiError {
    Timeout,
    TransferError,
    InvalidParameter,
    BusyTimeout,
}

impl fmt::Display for QspiError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            QspiError::Timeout         => write!(f, "QSPI timeout"),
            QspiError::TransferError   => write!(f, "QSPI transfer error"),
            QspiError::InvalidParameter => write!(f, "Invalid parameter"),
            QspiError::BusyTimeout     => write!(f, "Flash WIP timeout"),
        }
    }
}

/// Low-level QSPI controller trait.
pub trait QspiController {
    fn execute_read(&mut self, cmd: &QspiCommand, buf: &mut [u8]) -> Result<(), QspiError>;
    fn execute_write(&mut self, cmd: &QspiCommand, data: &[u8]) -> Result<(), QspiError>;
    fn execute_no_data(&mut self, cmd: &QspiCommand) -> Result<(), QspiError>;
    fn poll_status(&mut self, cmd: &QspiCommand, mask: u8, expected: u8) -> Result<(), QspiError>;
}
```

---

### 10.2 Rust QSPI Flash Driver

```rust
//! w25q_driver.rs
//!
//! Winbond W25Q series QSPI flash driver in Rust.
//! Generic over any QspiController implementation.

use crate::qspi_traits::{
    AddressSize, IoWidth, QspiCommand, QspiController, QspiError, TransferRate,
};

/// Flash geometry constants.
const PAGE_SIZE:   usize = 256;
const SECTOR_SIZE: usize = 4096;

/// Flash command codes.
mod cmd {
    pub const WREN:         u8 = 0x06;
    pub const WRDI:         u8 = 0x04;
    pub const RDSR1:        u8 = 0x05;
    pub const RDSR2:        u8 = 0x35;
    pub const WRSR:         u8 = 0x01;
    pub const FAST_READ:    u8 = 0x0B;
    pub const QUAD_READ:    u8 = 0x6B;   // 1-1-4
    pub const QIO_READ:     u8 = 0xEB;   // 1-4-4
    pub const QPP:          u8 = 0x32;   // Quad Page Program 1-1-4
    pub const SE:           u8 = 0x20;   // Sector Erase 4KB
    pub const BE64:         u8 = 0xD8;   // Block Erase 64KB
    pub const CHIP_ERASE:   u8 = 0xC7;
    pub const READ_JEDEC:   u8 = 0x9F;
    pub const ENTER_QPI:    u8 = 0x38;
    pub const EXIT_QPI:     u8 = 0xFF;
}

/// Status register bits.
mod status {
    pub const WIP:  u8 = 1 << 0;   // Write In Progress
    pub const WEL:  u8 = 1 << 1;   // Write Enable Latch
}

mod sr2 {
    pub const QE:   u8 = 1 << 1;   // Quad Enable
}

/// W25Q QSPI flash driver state machine.
pub struct W25qFlash<C: QspiController> {
    ctrl:        C,
    dummy_cycles: u8,
    quad_enabled: bool,
}

impl<C: QspiController> W25qFlash<C> {
    /// Create a new driver. `dummy_cycles` depends on clock speed
    /// (e.g., 8 for >80 MHz, 6 for 50–80 MHz).
    pub fn new(ctrl: C, dummy_cycles: u8) -> Self {
        Self { ctrl, dummy_cycles, quad_enabled: false }
    }

    /// Release the underlying controller.
    pub fn release(self) -> C { self.ctrl }

    // ── Private helpers ──────────────────────────────────────────────────

    fn write_enable(&mut self) -> Result<(), QspiError> {
        self.ctrl.execute_no_data(&QspiCommand::write_only(cmd::WREN))
    }

    fn read_sr1(&mut self) -> Result<u8, QspiError> {
        let mut buf = [0u8; 1];
        let cmd = QspiCommand {
            instruction:    Some(cmd::RDSR1 as u16),
            instruction_io: IoWidth::Single,
            address:        None,
            address_io:     IoWidth::None,
            address_size:   AddressSize::ThreeByte,
            alt_bytes:      None,
            alt_io:         IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::Single,
            data_len:       1,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_read(&cmd, &mut buf)?;
        Ok(buf[0])
    }

    fn wait_not_busy(&mut self) -> Result<(), QspiError> {
        let poll_cmd = QspiCommand {
            instruction:    Some(cmd::RDSR1 as u16),
            instruction_io: IoWidth::Single,
            address:        None,
            address_io:     IoWidth::None,
            address_size:   AddressSize::ThreeByte,
            alt_bytes:      None,
            alt_io:         IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::Single,
            data_len:       1,
            rate:           TransferRate::Sdr,
        };
        // Poll until WIP bit clears
        self.ctrl.poll_status(&poll_cmd, status::WIP, 0x00)
    }

    // ── Public API ───────────────────────────────────────────────────────

    /// Read the JEDEC ID (3 bytes: manufacturer, memory type, capacity).
    pub fn read_jedec_id(&mut self) -> Result<[u8; 3], QspiError> {
        let mut id = [0u8; 3];
        let cmd = QspiCommand {
            instruction:    Some(cmd::READ_JEDEC as u16),
            instruction_io: IoWidth::Single,
            address:        None, address_io: IoWidth::None,
            address_size:   AddressSize::ThreeByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::Single,
            data_len:       3,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_read(&cmd, &mut id)?;
        Ok(id)
    }

    /// Enable the QUAD bit in Status Register 2 (one-time initialization).
    pub fn enable_quad_mode(&mut self) -> Result<(), QspiError> {
        // Read current SR2
        let mut sr2 = [0u8; 1];
        let cmd = QspiCommand {
            instruction:    Some(cmd::RDSR2 as u16),
            instruction_io: IoWidth::Single,
            address:        None, address_io: IoWidth::None,
            address_size:   AddressSize::ThreeByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::Single,
            data_len:       1,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_read(&cmd, &mut sr2)?;

        if sr2[0] & sr2::QE != 0 {
            self.quad_enabled = true;
            return Ok(());   // Already enabled
        }

        self.write_enable()?;

        let sr_write = [0x00u8, sr2[0] | sr2::QE];  // SR1=0, SR2 with QE set
        let write_cmd = QspiCommand {
            instruction:    Some(cmd::WRSR as u16),
            instruction_io: IoWidth::Single,
            address:        None, address_io: IoWidth::None,
            address_size:   AddressSize::ThreeByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::Single,
            data_len:       2,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_write(&write_cmd, &sr_write)?;
        self.wait_not_busy()?;
        self.quad_enabled = true;
        Ok(())
    }

    /// Quad Output Fast Read (1-1-4): optimized for high-bandwidth reads.
    pub fn read_quad(&mut self, address: u32, buf: &mut [u8]) -> Result<(), QspiError> {
        if !self.quad_enabled {
            return Err(QspiError::InvalidParameter);
        }

        let cmd = QspiCommand {
            instruction:    Some(cmd::QUAD_READ as u16),
            instruction_io: IoWidth::Single,
            address:        Some(address),
            address_io:     IoWidth::Single,
            address_size:   AddressSize::ThreeByte,
            alt_bytes:      None,
            alt_io:         IoWidth::None,
            dummy_cycles:   self.dummy_cycles,
            data_io:        IoWidth::Quad,
            data_len:       buf.len() as u32,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_read(&cmd, buf)
    }

    /// Quad I/O Fast Read (1-4-4): address AND data on 4 lines.
    pub fn read_quad_io(&mut self, address: u32, buf: &mut [u8]) -> Result<(), QspiError> {
        if !self.quad_enabled {
            return Err(QspiError::InvalidParameter);
        }
        let cmd = QspiCommand::quad_read(address, buf.len() as u32, self.dummy_cycles);
        self.ctrl.execute_read(&cmd, buf)
    }

    /// Write data using Quad Page Program (1-1-4).
    /// Handles page boundary splitting automatically.
    pub fn write(&mut self, mut address: u32, mut data: &[u8]) -> Result<(), QspiError> {
        if !self.quad_enabled {
            return Err(QspiError::InvalidParameter);
        }

        while !data.is_empty() {
            // Bytes remaining in current page
            let page_offset = (address as usize) % PAGE_SIZE;
            let chunk_len   = (PAGE_SIZE - page_offset).min(data.len());
            let (chunk, rest) = data.split_at(chunk_len);

            self.write_enable()?;

            let cmd = QspiCommand {
                instruction:    Some(cmd::QPP as u16),
                instruction_io: IoWidth::Single,
                address:        Some(address),
                address_io:     IoWidth::Single,
                address_size:   AddressSize::ThreeByte,
                alt_bytes:      None,
                alt_io:         IoWidth::None,
                dummy_cycles:   0,
                data_io:        IoWidth::Quad,
                data_len:       chunk_len as u32,
                rate:           TransferRate::Sdr,
            };
            self.ctrl.execute_write(&cmd, chunk)?;
            self.wait_not_busy()?;

            address += chunk_len as u32;
            data     = rest;
        }
        Ok(())
    }

    /// Erase a 4 KB sector. Address is automatically aligned to sector boundary.
    pub fn erase_sector(&mut self, address: u32) -> Result<(), QspiError> {
        let aligned = address & !(SECTOR_SIZE as u32 - 1);
        self.write_enable()?;

        let cmd = QspiCommand {
            instruction:    Some(cmd::SE as u16),
            instruction_io: IoWidth::Single,
            address:        Some(aligned),
            address_io:     IoWidth::Single,
            address_size:   AddressSize::ThreeByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::None,
            data_len:       0,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_no_data(&cmd)?;
        self.wait_not_busy()
    }

    /// Erase the entire chip. Can take seconds — use sparingly.
    pub fn erase_chip(&mut self) -> Result<(), QspiError> {
        self.write_enable()?;
        self.ctrl.execute_no_data(&QspiCommand::write_only(cmd::CHIP_ERASE))?;
        self.wait_not_busy()
    }
}
```

---

### 10.3 Rust OPI Driver Skeleton

```rust
//! opi_flash.rs
//!
//! Octal SPI (OPI) flash driver skeleton for Macronix MX25UM51245G.
//! Demonstrates the type-state pattern for STR/DTR mode enforcement.

use core::marker::PhantomData;
use crate::qspi_traits::{QspiController, QspiError, QspiCommand, IoWidth,
                          AddressSize, TransferRate};

/// Type-state markers for OPI operating modes.
pub struct ModeUninitialized;
pub struct ModeSpi;
pub struct ModeOpiStr;
pub struct ModeOpiDtr;

/// OPI Commands for Macronix MX25UM51245G.
/// In OPI mode, commands are 16 bits wide.
mod opi_cmd {
    pub const WREN_SPI:     u8  = 0x06;         // In SPI mode
    pub const WRCR2_SPI:    u8  = 0x72;         // Write CR2 (SPI mode)
    pub const RDCR2_SPI:    u8  = 0x71;         // Read CR2  (SPI mode)
    pub const READ_OPI:     u16 = 0xEC13;       // OPI STR read
    pub const READ_DOPI:    u16 = 0xEE11;       // OPI DTR read
    pub const PP_OPI:       u16 = 0x12ED;       // OPI page program
    pub const SE_OPI:       u16 = 0x21DE;       // OPI sector erase
    pub const RDSR_OPI:     u16 = 0x05FA;       // Read status (OPI mode)
    pub const WREN_OPI:     u16 = 0x06F9;       // Write enable (OPI mode)
}

const OPI_CR2_ADDR:     u32 = 0x0000_0000;
const OPI_CR2_STR:      u8  = 0x01;
const OPI_CR2_DTR:      u8  = 0x02;
const OPI_DUMMY_STR:    u8  = 20;
const OPI_DUMMY_DTR:    u8  = 20;

/// Type-state OPI flash driver.
/// The mode parameter `M` enforces at compile time that you cannot call
/// OPI-mode reads before enabling OPI, or DTR reads in STR mode.
pub struct OpiFlash<C: QspiController, M> {
    ctrl:  C,
    _mode: PhantomData<M>,
}

impl<C: QspiController> OpiFlash<C, ModeUninitialized> {
    pub fn new(ctrl: C) -> Self {
        Self { ctrl, _mode: PhantomData }
    }
}

impl<C: QspiController> OpiFlash<C, ModeSpi> {
    /// Transition to OPI STR mode.
    /// Consumes `self` and returns a new driver typed for OPI STR.
    pub fn enable_opi_str(mut self) -> Result<OpiFlash<C, ModeOpiStr>, QspiError> {
        // 1) Write Enable (in SPI mode)
        self.ctrl.execute_no_data(&QspiCommand::write_only(opi_cmd::WREN_SPI))?;

        // 2) Write CR2 register to enable OPI STR
        let cr2 = [OPI_CR2_STR];
        let cmd = QspiCommand {
            instruction:    Some(opi_cmd::WRCR2_SPI as u16),
            instruction_io: IoWidth::Single,
            address:        Some(OPI_CR2_ADDR),
            address_io:     IoWidth::Single,
            address_size:   AddressSize::FourByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::Single,
            data_len:       1,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_write(&cmd, &cr2)?;

        // Allow mode switch to settle (tREADY2 < 200 ns)
        // In production: insert a short delay here via systick or nop loop

        Ok(OpiFlash { ctrl: self.ctrl, _mode: PhantomData })
    }
}

impl<C: QspiController> OpiFlash<C, ModeOpiStr> {
    /// OPI STR Read — 8-8-8 mode, SDR.
    pub fn read(&mut self, address: u32, buf: &mut [u8]) -> Result<(), QspiError> {
        let cmd = QspiCommand::opi_str_read(address, buf.len() as u32, OPI_DUMMY_STR);
        self.ctrl.execute_read(&cmd, buf)
    }

    /// OPI STR Page Program — 8-8-8 mode.
    pub fn write_page(&mut self, address: u32, data: &[u8]) -> Result<(), QspiError> {
        // Write Enable in OPI mode
        let wren = QspiCommand {
            instruction:    Some(opi_cmd::WREN_OPI),
            instruction_io: IoWidth::Octal,
            address:        None, address_io: IoWidth::None,
            address_size:   AddressSize::FourByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::None,
            data_len:       0,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_no_data(&wren)?;

        let cmd = QspiCommand {
            instruction:    Some(opi_cmd::PP_OPI),
            instruction_io: IoWidth::Octal,
            address:        Some(address),
            address_io:     IoWidth::Octal,
            address_size:   AddressSize::FourByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::Octal,
            data_len:       data.len() as u32,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_write(&cmd, data)?;
        self.wait_not_busy()
    }

    fn wait_not_busy(&mut self) -> Result<(), QspiError> {
        let cmd = QspiCommand {
            instruction:    Some(opi_cmd::RDSR_OPI),
            instruction_io: IoWidth::Octal,
            address:        Some(0x0000_0000),
            address_io:     IoWidth::Octal,
            address_size:   AddressSize::FourByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   4,
            data_io:        IoWidth::Octal,
            data_len:       1,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.poll_status(&cmd, 0x01, 0x00)  // WIP bit = 0
    }

    /// Transition to OPI DTR mode (requires hardware reconfiguration).
    pub fn enable_dtr(mut self) -> Result<OpiFlash<C, ModeOpiDtr>, QspiError> {
        // In OPI-STR mode, we can write CR2 to switch to DTR
        let wren = QspiCommand {
            instruction:    Some(opi_cmd::WREN_OPI),
            instruction_io: IoWidth::Octal,
            address:        None, address_io: IoWidth::None,
            address_size:   AddressSize::FourByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::None, data_len: 0,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_no_data(&wren)?;

        let cr2 = [OPI_CR2_DTR];
        let cmd = QspiCommand {
            instruction:    Some(opi_cmd::PP_OPI),  // WRCR2 in OPI mode
            instruction_io: IoWidth::Octal,
            address:        Some(OPI_CR2_ADDR),
            address_io:     IoWidth::Octal,
            address_size:   AddressSize::FourByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   0,
            data_io:        IoWidth::Octal,
            data_len:       1,
            rate:           TransferRate::Sdr,
        };
        self.ctrl.execute_write(&cmd, &cr2)?;

        Ok(OpiFlash { ctrl: self.ctrl, _mode: PhantomData })
    }
}

impl<C: QspiController> OpiFlash<C, ModeOpiDtr> {
    /// OPI DTR Read — 8D-8D-8D mode.
    /// This function can ONLY be called in DTR mode (enforced by the type system).
    pub fn read_dtr(&mut self, address: u32, buf: &mut [u8]) -> Result<(), QspiError> {
        let cmd = QspiCommand {
            instruction:    Some(opi_cmd::READ_DOPI),
            instruction_io: IoWidth::Octal,
            address:        Some(address),
            address_io:     IoWidth::Octal,
            address_size:   AddressSize::FourByte,
            alt_bytes:      None, alt_io: IoWidth::None,
            dummy_cycles:   OPI_DUMMY_DTR,
            data_io:        IoWidth::Octal,
            data_len:       buf.len() as u32,
            rate:           TransferRate::Dtr,   // DTR enforced here
        };
        self.ctrl.execute_read(&cmd, buf)
    }
}
```

---

### 10.4 Async QSPI with Embassy

```rust
//! embassy_qspi.rs
//!
//! Demonstrates QSPI flash integration with the Embassy async runtime
//! on an STM32H7 target. Uses embassy-stm32 QSPI peripheral driver.
//!
//! Note: Embassy's QSPI API is PAC/HAL-specific; this example shows
//! the structural pattern, not a verbatim-compilable snippet.

#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

use embassy_executor::Spawner;
use embassy_stm32::qspi::{Qspi, Config as QspiConfig, TransferConfig, AddressSize};
use embassy_stm32::qspi::enums::{MemorySize, ChipSelectHighTime, ClockMode,
                                   DummyCycles, FrequencyBin};
use embassy_time::{Duration, Timer};

/// Async task: demonstrates non-blocking QSPI read with Embassy.
#[embassy_executor::task]
async fn flash_reader(mut qspi: Qspi<'static, embassy_stm32::peripherals::QUADSPI>) {
    // Configure: 1-4-4 Quad I/O Fast Read (0xEB), 6 dummy cycles
    let xfer = TransferConfig {
        iwidth:       embassy_stm32::qspi::enums::QspiWidth::QUAD,
        awidth:       embassy_stm32::qspi::enums::QspiWidth::QUAD,
        dwidth:       embassy_stm32::qspi::enums::QspiWidth::QUAD,
        instruction:  0xEB,
        address:      Some(0x0000_0000),
        dummy:        DummyCycles::_6,
    };

    let mut buf = [0u8; 1024];

    loop {
        // Await completion — CPU is free for other tasks while DMA transfers
        match qspi.read(&mut buf, xfer.clone()).await {
            Ok(()) => {
                // Process buf here (CRC check, decompression, etc.)
                defmt::info!("Read {} bytes from flash, first: {:#04x}", buf.len(), buf[0]);
            }
            Err(e) => {
                defmt::error!("QSPI read error: {:?}", e);
                // Back off and retry
                Timer::after(Duration::from_millis(100)).await;
            }
        }

        // Simulate periodic read pattern
        Timer::after(Duration::from_secs(1)).await;
    }
}

/// Demonstrate concurrent flash access and other work with async.
#[embassy_executor::task]
async fn background_work() {
    loop {
        // This runs concurrently with flash_reader, while DMA handles the transfer
        defmt::info!("Doing background processing...");
        Timer::after(Duration::from_millis(500)).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    // Configure QSPI peripheral via embassy-stm32
    let mut config = QspiConfig::default();
    config.memory_size       = MemorySize::_32MiB;
    config.address_size      = AddressSize::_24bit;
    config.prescaler         = 1;               // AHB / (1+1) = half AHB
    config.cs_high_time      = ChipSelectHighTime::_2Cycle;
    config.mode              = ClockMode::Mode0;

    // Pin assignment is board-specific (defined in embassy-stm32 board configs)
    let qspi = Qspi::new_bk1(
        p.QUADSPI,
        p.PE2,   // CLK
        p.PB6,   // nCS
        p.PD11,  // IO0
        p.PD12,  // IO1
        p.PE2,   // IO2
        p.PD13,  // IO3
        p.DMA2_CH7,
        config,
    );

    // Spawn async tasks — both run cooperatively on a single thread
    spawner.spawn(flash_reader(qspi)).unwrap();
    spawner.spawn(background_work()).unwrap();
}
```

---

## 11. Common Pitfalls and Debugging

**Wrong dummy cycles** are the most frequent bug. The symptom is reading all-0xFF or garbled data even though the command phase appears correct on a logic analyzer. Always verify dummy cycles against the flash datasheet table for your exact clock frequency.

**QE bit not set** before attempting Quad reads. IO2 and IO3 are used as WP# and HOLD# in standard SPI mode. If the QE bit in SR2 is not set, trying to use 4-line data mode drives these pins unexpectedly and the flash may not respond correctly.

**Page boundary overflow** in write operations. Flash page programming wraps within a 256-byte page — writing across a page boundary without splitting the transfer results in data being written to the wrong location. Always split writes at page boundaries.

**CS timing violations.** The flash requires a minimum CS-high time (tSHSL) between transactions, often 7–20 ns. Violating this can cause incomplete commands or incorrect data.

**Clock polarity (CPOL/CPHA).** QSPI flash devices typically use Mode 0 (CPOL=0, CPHA=0). Using Mode 3 may work electrically but can cause timing issues at high frequencies.

**Erase before write.** Flash can only program 1→0. Erasing resets bits to 1. Writing to non-erased flash without erasing first will appear to succeed but the data will be corrupted (AND of old and new values).

**PCB layout for OPI DTR.** At 200 MHz DTR, signal integrity is critical. IO0–IO7, DQS, and CLK traces must be length-matched (±25 ps), impedance controlled (50 Ω), and properly terminated. Violations cause intermittent read errors that are very difficult to debug.

**Mode bits in 1-4-4 mode.** The 1-4-4 Quad I/O read (0xEB) uses alternate bytes (mode bits) after the address. Setting these incorrectly (particularly bit[7:4]) can cause the flash to enter or stay in "continuous read" mode, which skips the command byte on subsequent transactions — causing all subsequent reads to fail.

---

## 12. Summary

Multi-IO SPI encompasses three generations of parallel flash memory interfaces, each trading complexity for throughput:

**Dual SPI** offers a modest 2× gain with minimal hardware changes, suitable where standard SPI falls just short of bandwidth requirements.

**Quad SPI (QPI)** is the embedded industry standard for external NOR flash. It provides a 4× throughput gain in 1-1-4 mode and up to 8× in full 4-4-4 QPI mode. At 100 MHz it delivers 50 MB/s SDR reads, sufficient for executing most embedded applications directly from external flash (XIP). Virtually every modern MCU (STM32, NXP RT, Microchip SAM, Nordic, etc.) includes a dedicated QSPI controller with memory-mapped mode support.

**Octal SPI (OPI)** pushes this further to 8 parallel data lines. In STR mode at 200 MHz it achieves 200 MB/s; in DTR mode (both clock edges) it reaches 400 MB/s — comparable to DDR SDRAM in raw bandwidth. OPI is found in high-end MCUs (STM32H7, STM32U5, i.MX RT1060+) and is the standard interface for PSRAM and high-density NOR flash used in AI inference, graphics, and multimedia applications.

The key engineering concerns are: proper mode initialization (QE/OPI bits), correct dummy cycle configuration per operating frequency, page-boundary-aware write scheduling, and — for OPI DTR — strict PCB signal integrity requirements.

From a software perspective, Rust's type-state pattern provides compile-time safety guarantees for mode transitions (you literally cannot call a DTR read function in STR mode), while C/C++ HAL drivers offer maximum portability across toolchains and RTOS environments. Both approaches benefit from DMA-driven transfers and hardware auto-polling to maximize CPU availability during slow flash erase operations.

---

*Document: `52_Multi_IO_SPI_QPI_OPI.md` — Part of the Embedded SPI Programming Reference Series*