# 82. In-System Programming (ISP) via SPI

**Architecture & Protocol** — hardware signals, SPI Mode 0, AVR programming enable sequence, NOR flash opcodes (Write Enable, Page Program, Sector Erase, JEDEC ID, busy polling).

**C/C++ Examples:**
- Bit-bang GPIO SPI programmer with `libgpiod` — full erase/program/verify flow for SPI NOR flash
- AVR ISP 4-byte command framing — chip erase, page buffer loading, write & readback
- STM32 HAL + DMA bulk page programming for production throughput

**Rust Examples:**
- Platform-agnostic `SpiFlash<SPI>` driver using `embedded-hal 1.x` traits — erase, program, verify, and `program_image()` high-level API
- `AvrIsp` struct using `OutputPin`/`InputPin` generics — full ISP flow in safe Rust

**Advanced Topics:**
- Bootloader self-update from SPI flash to internal MCU flash (with firmware header + size validation)
- Dual-bank (A/B) FOTA state machine in Rust
- CRC-32 integrity checking, retry logic, Ed25519 signature verification, anti-rollback, and STM32 RDP flash lock

**Summary table** comparing C and Rust across hardware access, memory safety, error handling, portability, and ecosystem maturity.


## Table of Contents

1. [Introduction](#introduction)
2. [ISP Architecture and Concepts](#isp-architecture-and-concepts)
3. [SPI Protocol for Programming](#spi-protocol-for-programming)
4. [Memory Types and Programming Models](#memory-types-and-programming-models)
5. [ISP Implementation in C/C++](#isp-implementation-in-cc)
6. [ISP Implementation in Rust](#isp-implementation-in-rust)
7. [Bootloader Design via SPI](#bootloader-design-via-spi)
8. [Firmware Update (FOTA) Patterns](#firmware-update-fota-patterns)
9. [Error Handling and Verification](#error-handling-and-verification)
10. [Security Considerations](#security-considerations)
11. [Summary](#summary)

---

## Introduction

**In-System Programming (ISP)** is the ability to program or reprogram a microcontroller or memory device while it is soldered onto a circuit board and connected within its target system — without removal from the board. SPI (Serial Peripheral Interface) is one of the most widely used protocols for ISP because of its simplicity, speed, and near-universal support in flash memories and microcontrollers.

ISP via SPI allows:

- Factory programming of blank microcontrollers during manufacturing
- Field firmware updates without specialized hardware removal
- Bootloader-based over-the-air (OTA) or wired firmware update flows
- In-circuit debugging and memory inspection

The most common scenarios are:

| Scenario | Description |
|---|---|
| External Programmer → MCU | A dedicated programmer (e.g., AVRISP, ST-LINK) drives SPI to program an MCU |
| MCU → External Flash | A host MCU programs a connected SPI flash chip (e.g., W25Q series) |
| Bootloader Self-Update | An MCU's own bootloader receives new firmware over UART/USB/CAN and writes it to flash via internal SPI controller |
| Peer MCU Programming | One MCU programs a secondary MCU over SPI (common in multi-MCU systems) |

---

## ISP Architecture and Concepts

### Hardware Signals

A minimal ISP-via-SPI interface uses four signals:

```
Master (Programmer)          Target (MCU / Flash)
─────────────────────────────────────────────────
MOSI  ──────────────────────►  MOSI / SDI / DI
MISO  ◄──────────────────────  MISO / SDO / DO
SCK   ──────────────────────►  SCK / CLK
/CS   ──────────────────────►  /CS / /SS / /CE
/RST  ──────────────────────►  /RESET  (optional, MCU-specific)
```

For AVR microcontrollers an additional `RESET` pin is driven low before issuing SPI commands, which puts the device into a programmable state.

### Programming Enable Sequence (AVR Example)

```
1. Pull /RESET LOW
2. Apply power (or power-cycle if already running)
3. Wait ≥ 20 ms
4. Issue SPI Programming Enable command: 0xAC 0x53 0x00 0x00
5. Check echo byte in response
6. Begin programming commands
7. Pull /RESET HIGH to exit programming mode
```

### SPI Mode for ISP

Most SPI flash memories and programmable MCUs use **SPI Mode 0** (CPOL=0, CPHA=0), where data is sampled on the rising edge of SCK. Some devices (e.g., AT89S series) require Mode 0 or Mode 3. Always consult the target's datasheet.

---

## SPI Protocol for Programming

### Generic Flash Programming Command Cycle

A typical SPI NOR flash (e.g., Winbond W25Q128) uses the following command structure:

```
 Byte 0       Byte 1-3         Byte 4+
┌──────────┬────────────────┬──────────────────┐
│ Opcode   │ Address[23:0]  │ Data / Dummy     │
└──────────┴────────────────┴──────────────────┘
```

**Common opcodes:**

| Command | Opcode | Description |
|---|---|---|
| Write Enable | `0x06` | Must precede any write/erase |
| Write Disable | `0x04` | Re-enables write protection |
| Read Status Register | `0x05` | Poll busy flag (bit 0) |
| Page Program | `0x02` | Write up to 256 bytes |
| Sector Erase (4 KB) | `0x20` | Erase 4 KB sector |
| Block Erase (64 KB) | `0xD8` | Erase 64 KB block |
| Chip Erase | `0xC7` | Erase entire chip |
| Read Data | `0x03` | Read at up to 50 MHz |
| JEDEC ID | `0x9F` | Read manufacturer/device ID |

### Busy Polling Pattern

After any page program or erase command the flash enters a busy state. The host must poll the **WIP** (Write In Progress) bit in the Status Register:

```
Poll Loop:
  CS LOW
  Send 0x05 (Read Status Register 1)
  Read byte
  CS HIGH
  if (byte & 0x01) → still busy, wait and retry
  else             → operation complete
```

---

## Memory Types and Programming Models

### SPI NOR Flash

- Byte-addressable reads, page-granular writes (typically 256 bytes/page)
- Sector or block granular erasure required before write
- XIP (Execute In Place) capable on some MCUs via QSPI
- Examples: Winbond W25Qxxx, Micron MT25Q, ISSI IS25LP

### SPI NAND Flash

- Page-granular reads/writes, block-granular erase
- Requires bad-block management (BBM)
- Higher density per cost than NOR
- Not directly XIP capable

### AVR In-System Programming (Internal Flash)

- AVR internal flash is programmed by an external programmer driving the ISP SPI bus
- The MCU's SPI pins (`MOSI`, `MISO`, `SCK`, `RESET`) are shared with the ISP interface
- Commands are 4-byte frames; responses are interleaved with the 3rd or 4th byte

---

## ISP Implementation in C/C++

### Low-Level SPI Bit-Banging Programmer

The following example implements a software SPI master on a Linux system using GPIO (via `/dev/gpiochip`) to program an SPI NOR flash device:

```c
/*
 * spi_isp.c — Bit-bang SPI ISP programmer for NOR Flash
 * Targets: Linux GPIO via ioctl / gpiod
 *
 * Build: gcc -o spi_isp spi_isp.c -lgpiod
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <gpiod.h>

/* GPIO line numbers — adjust for your board */
#define GPIO_CHIP   "/dev/gpiochip0"
#define PIN_MOSI    10
#define PIN_MISO    9
#define PIN_SCK     11
#define PIN_CS      8
#define PIN_RESET   25   /* Optional: for MCU ISP */

/* SPI NOR Flash opcodes */
#define CMD_WRITE_ENABLE    0x06
#define CMD_WRITE_DISABLE   0x04
#define CMD_READ_STATUS1    0x05
#define CMD_PAGE_PROGRAM    0x02
#define CMD_SECTOR_ERASE    0x20
#define CMD_READ_DATA       0x03
#define CMD_CHIP_ERASE      0xC7
#define CMD_JEDEC_ID        0x9F

#define FLASH_PAGE_SIZE     256
#define FLASH_SECTOR_SIZE   4096

typedef struct {
    struct gpiod_chip   *chip;
    struct gpiod_line   *mosi;
    struct gpiod_line   *miso;
    struct gpiod_line   *sck;
    struct gpiod_line   *cs;
} spi_ctx_t;

/* ── GPIO helpers ─────────────────────────────────────────── */

static inline void spi_delay(void) { usleep(1); }  /* ~1 µs half-period */

static uint8_t spi_transfer_byte(spi_ctx_t *ctx, uint8_t tx)
{
    uint8_t rx = 0;
    for (int bit = 7; bit >= 0; bit--) {
        /* Setup MOSI before rising edge (Mode 0) */
        gpiod_line_set_value(ctx->mosi, (tx >> bit) & 1);
        spi_delay();
        gpiod_line_set_value(ctx->sck, 1);  /* Rising edge — sample */
        rx = (rx << 1) | gpiod_line_get_value(ctx->miso);
        spi_delay();
        gpiod_line_set_value(ctx->sck, 0);  /* Falling edge */
    }
    return rx;
}

/* ── CS control ───────────────────────────────────────────── */

static void cs_assert(spi_ctx_t *ctx)   { gpiod_line_set_value(ctx->cs, 0); }
static void cs_deassert(spi_ctx_t *ctx) { gpiod_line_set_value(ctx->cs, 1); }

/* ── Flash commands ───────────────────────────────────────── */

static void flash_write_enable(spi_ctx_t *ctx)
{
    cs_assert(ctx);
    spi_transfer_byte(ctx, CMD_WRITE_ENABLE);
    cs_deassert(ctx);
}

static bool flash_is_busy(spi_ctx_t *ctx)
{
    cs_assert(ctx);
    spi_transfer_byte(ctx, CMD_READ_STATUS1);
    uint8_t status = spi_transfer_byte(ctx, 0x00);
    cs_deassert(ctx);
    return (status & 0x01) != 0;  /* WIP bit */
}

static void flash_wait_ready(spi_ctx_t *ctx)
{
    while (flash_is_busy(ctx)) {
        usleep(100);  /* poll every 100 µs */
    }
}

static void flash_read_jedec_id(spi_ctx_t *ctx,
                                 uint8_t *mfr, uint8_t *dev_hi, uint8_t *dev_lo)
{
    cs_assert(ctx);
    spi_transfer_byte(ctx, CMD_JEDEC_ID);
    *mfr    = spi_transfer_byte(ctx, 0x00);
    *dev_hi = spi_transfer_byte(ctx, 0x00);
    *dev_lo = spi_transfer_byte(ctx, 0x00);
    cs_deassert(ctx);
}

static void flash_sector_erase(spi_ctx_t *ctx, uint32_t addr)
{
    flash_write_enable(ctx);
    cs_assert(ctx);
    spi_transfer_byte(ctx, CMD_SECTOR_ERASE);
    spi_transfer_byte(ctx, (addr >> 16) & 0xFF);
    spi_transfer_byte(ctx, (addr >> 8)  & 0xFF);
    spi_transfer_byte(ctx, (addr)       & 0xFF);
    cs_deassert(ctx);
    flash_wait_ready(ctx);
}

/*
 * flash_page_program — Write up to 256 bytes starting at addr.
 * addr must be page-aligned (addr & 0xFF == 0).
 */
static int flash_page_program(spi_ctx_t *ctx, uint32_t addr,
                               const uint8_t *data, size_t len)
{
    if (len == 0 || len > FLASH_PAGE_SIZE) return -1;

    flash_write_enable(ctx);
    cs_assert(ctx);
    spi_transfer_byte(ctx, CMD_PAGE_PROGRAM);
    spi_transfer_byte(ctx, (addr >> 16) & 0xFF);
    spi_transfer_byte(ctx, (addr >> 8)  & 0xFF);
    spi_transfer_byte(ctx, (addr)       & 0xFF);
    for (size_t i = 0; i < len; i++) {
        spi_transfer_byte(ctx, data[i]);
    }
    cs_deassert(ctx);
    flash_wait_ready(ctx);
    return 0;
}

static void flash_read(spi_ctx_t *ctx, uint32_t addr,
                        uint8_t *buf, size_t len)
{
    cs_assert(ctx);
    spi_transfer_byte(ctx, CMD_READ_DATA);
    spi_transfer_byte(ctx, (addr >> 16) & 0xFF);
    spi_transfer_byte(ctx, (addr >> 8)  & 0xFF);
    spi_transfer_byte(ctx, (addr)       & 0xFF);
    for (size_t i = 0; i < len; i++) {
        buf[i] = spi_transfer_byte(ctx, 0x00);
    }
    cs_deassert(ctx);
}

/* ── High-level: program a firmware image ─────────────────── */

int flash_program_image(spi_ctx_t *ctx, uint32_t base_addr,
                         const uint8_t *image, size_t image_size)
{
    printf("[ISP] Programming %zu bytes at 0x%06X\n", image_size, base_addr);

    /* 1. Erase all sectors covered by the image */
    size_t sectors = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    for (size_t s = 0; s < sectors; s++) {
        uint32_t addr = base_addr + s * FLASH_SECTOR_SIZE;
        printf("[ISP] Erasing sector @ 0x%06X\n", addr);
        flash_sector_erase(ctx, addr);
    }

    /* 2. Program page by page */
    size_t offset = 0;
    while (offset < image_size) {
        size_t chunk = image_size - offset;
        if (chunk > FLASH_PAGE_SIZE) chunk = FLASH_PAGE_SIZE;

        uint32_t addr = base_addr + (uint32_t)offset;
        if (flash_page_program(ctx, addr, image + offset, chunk) != 0) {
            fprintf(stderr, "[ISP] Page program failed at 0x%06X\n", addr);
            return -1;
        }
        offset += chunk;
        printf("[ISP] Programmed %zu / %zu bytes\r", offset, image_size);
        fflush(stdout);
    }

    /* 3. Verify */
    printf("\n[ISP] Verifying...\n");
    uint8_t verify_buf[FLASH_PAGE_SIZE];
    offset = 0;
    while (offset < image_size) {
        size_t chunk = image_size - offset;
        if (chunk > FLASH_PAGE_SIZE) chunk = FLASH_PAGE_SIZE;
        flash_read(ctx, base_addr + (uint32_t)offset, verify_buf, chunk);
        if (memcmp(verify_buf, image + offset, chunk) != 0) {
            fprintf(stderr, "[ISP] Verify FAILED at offset 0x%zX\n", offset);
            return -1;
        }
        offset += chunk;
    }

    printf("[ISP] Programming and verification SUCCESSFUL.\n");
    return 0;
}
```

### AVR ISP Programmer in C (4-byte command frames)

```c
/*
 * avr_isp.c — AVR ISP programming via SPI
 *
 * AVR ISP uses 4-byte command frames.
 * The echo/response byte is in byte 3 (or byte 2 for some commands).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* Platform-specific SPI transfer — returns received byte */
extern uint8_t spi_xfer(uint8_t tx);
extern void    spi_reset_low(void);
extern void    spi_reset_high(void);
extern void    delay_ms(uint32_t ms);

/* ── AVR ISP Commands ─────────────────────────────────────── */

#define AVR_ISP_PROG_ENABLE_1   0xAC
#define AVR_ISP_PROG_ENABLE_2   0x53

static bool avr_isp_enter_programming(void)
{
    spi_reset_low();
    delay_ms(25);

    /* Programming Enable: AC 53 00 00, echo 0x53 in byte 3 */
    spi_xfer(AVR_ISP_PROG_ENABLE_1);
    spi_xfer(AVR_ISP_PROG_ENABLE_2);
    spi_xfer(0x00);
    uint8_t echo = spi_xfer(0x00);

    if (echo != AVR_ISP_PROG_ENABLE_2) {
        fprintf(stderr, "[AVR-ISP] Enable failed: got 0x%02X\n", echo);
        return false;
    }
    return true;
}

static uint8_t avr_isp_read_signature(uint8_t index)
{
    /* Read Signature Byte: 0x30 0x00 <index> <don't care> */
    spi_xfer(0x30);
    spi_xfer(0x00);
    spi_xfer(index & 0x03);
    return spi_xfer(0x00);
}

static void avr_isp_chip_erase(void)
{
    spi_xfer(0xAC);  /* Chip Erase */
    spi_xfer(0x80);
    spi_xfer(0x00);
    spi_xfer(0x00);
    delay_ms(10);    /* tWD_ERASE */
}

/*
 * avr_isp_load_program_memory_page
 * Loads a word (low/high byte) into the page buffer at word address.
 * word_addr: 0-based word index within page
 */
static void avr_isp_load_page_word(uint8_t word_addr,
                                    uint8_t low, uint8_t high)
{
    /* Load Program Memory Page (Low byte) */
    spi_xfer(0x40);
    spi_xfer(0x00);
    spi_xfer(word_addr);
    spi_xfer(low);

    /* Load Program Memory Page (High byte) */
    spi_xfer(0x48);
    spi_xfer(0x00);
    spi_xfer(word_addr);
    spi_xfer(high);
}

/*
 * avr_isp_write_program_page
 * Commits the loaded page buffer to flash at the given page address.
 * page_addr: byte address >> 1 (word address of page start)
 */
static void avr_isp_write_program_page(uint16_t page_addr)
{
    spi_xfer(0x4C);                          /* Write Program Memory Page */
    spi_xfer((page_addr >> 8) & 0xFF);
    spi_xfer(page_addr & 0xFF);
    spi_xfer(0x00);
    delay_ms(5);                             /* tWD_FLASH */
}

static uint8_t avr_isp_read_program_byte(uint16_t word_addr, bool high)
{
    /* Read Program Memory: 0x20 (low) or 0x28 (high) */
    spi_xfer(high ? 0x28 : 0x20);
    spi_xfer((word_addr >> 8) & 0xFF);
    spi_xfer(word_addr & 0xFF);
    return spi_xfer(0x00);
}

/*
 * avr_isp_program_flash
 * Programs an entire firmware image into an AVR flash.
 * page_words: words per page (e.g., 64 for ATmega328P = 128 bytes/page)
 */
int avr_isp_program_flash(const uint8_t *image, size_t size,
                           uint8_t page_words)
{
    if (!avr_isp_enter_programming()) return -1;

    /* Read and display signature */
    printf("[AVR-ISP] Signature: 0x%02X 0x%02X 0x%02X\n",
           avr_isp_read_signature(0),
           avr_isp_read_signature(1),
           avr_isp_read_signature(2));

    avr_isp_chip_erase();

    size_t total_words = (size + 1) / 2;
    size_t word_idx = 0;

    while (word_idx < total_words) {
        uint16_t page_start = (uint16_t)(word_idx & ~(page_words - 1));

        /* Fill page buffer */
        for (uint8_t pw = 0; pw < page_words; pw++) {
            size_t idx = (page_start + pw) * 2;
            uint8_t lo = (idx     < size) ? image[idx]     : 0xFF;
            uint8_t hi = (idx + 1 < size) ? image[idx + 1] : 0xFF;
            avr_isp_load_page_word(pw, lo, hi);
        }

        avr_isp_write_program_page(page_start);
        word_idx += page_words;
    }

    /* Verify */
    for (size_t w = 0; w < total_words; w++) {
        uint8_t lo_exp = image[w * 2];
        uint8_t hi_exp = (w * 2 + 1 < size) ? image[w * 2 + 1] : 0xFF;
        uint8_t lo_rd  = avr_isp_read_program_byte((uint16_t)w, false);
        uint8_t hi_rd  = avr_isp_read_program_byte((uint16_t)w, true);
        if (lo_rd != lo_exp || hi_rd != hi_exp) {
            fprintf(stderr, "[AVR-ISP] Verify failed at word %zu\n", w);
            return -1;
        }
    }

    spi_reset_high();
    printf("[AVR-ISP] Programming complete.\n");
    return 0;
}
```

### Hardware SPI with DMA (STM32 HAL, C)

For production systems, hardware SPI with DMA greatly improves throughput:

```c
/*
 * flash_dma.c — STM32 HAL hardware SPI + DMA for bulk flash programming
 */

#include "stm32f4xx_hal.h"

extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern DMA_HandleTypeDef hdma_spi1_rx;

#define FLASH_CS_GPIO_Port  GPIOA
#define FLASH_CS_Pin        GPIO_PIN_4

static inline void FLASH_CS_LOW(void)
{
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
}

static inline void FLASH_CS_HIGH(void)
{
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
}

/*
 * flash_page_program_dma
 * Writes a full 256-byte page using DMA SPI transfer.
 * Returns HAL_OK on success.
 */
HAL_StatusTypeDef flash_page_program_dma(uint32_t addr,
                                          const uint8_t *data)
{
    uint8_t cmd[4] = {
        0x02,                    /* Page Program opcode */
        (addr >> 16) & 0xFF,
        (addr >> 8)  & 0xFF,
        (addr)       & 0xFF
    };
    uint8_t status;
    HAL_StatusTypeDef result;

    /* Write Enable */
    FLASH_CS_LOW();
    HAL_SPI_Transmit(&hspi1, (uint8_t[]){0x06}, 1, 10);
    FLASH_CS_HIGH();

    /* Page Program command + address */
    FLASH_CS_LOW();
    HAL_SPI_Transmit(&hspi1, cmd, 4, 10);

    /* DMA transmit for data payload */
    result = HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)data, 256);
    if (result != HAL_OK) {
        FLASH_CS_HIGH();
        return result;
    }

    /* Wait for DMA transfer complete (event-driven in real code) */
    while (HAL_SPI_GetState(&hspi1) == HAL_SPI_STATE_BUSY_TX) {
        __WFI();
    }
    FLASH_CS_HIGH();

    /* Poll WIP */
    do {
        FLASH_CS_LOW();
        HAL_SPI_Transmit(&hspi1, (uint8_t[]){0x05}, 1, 5);
        HAL_SPI_Receive(&hspi1, &status, 1, 5);
        FLASH_CS_HIGH();
    } while (status & 0x01);

    return HAL_OK;
}
```

---

## ISP Implementation in Rust

### Platform-Agnostic SPI Flash Programmer using `embedded-hal`

Rust's `embedded-hal` trait system enables hardware-agnostic ISP drivers:

```rust
// spi_flash_isp.rs
//
// Platform-agnostic SPI NOR flash ISP driver using embedded-hal 1.x
//
// Add to Cargo.toml:
//   embedded-hal = "1.0"
//   defmt = "0.3"    (optional, for logging)

use embedded_hal::spi::SpiDevice;  // handles CS automatically

const CMD_WRITE_ENABLE:  u8 = 0x06;
const CMD_READ_STATUS1:  u8 = 0x05;
const CMD_PAGE_PROGRAM:  u8 = 0x02;
const CMD_SECTOR_ERASE:  u8 = 0x20;
const CMD_READ_DATA:     u8 = 0x03;
const CMD_JEDEC_ID:      u8 = 0x9F;

pub const PAGE_SIZE:   usize = 256;
pub const SECTOR_SIZE: usize = 4096;

/// Error type for flash ISP operations
#[derive(Debug)]
pub enum FlashError<SpiError> {
    Spi(SpiError),
    VerifyFailed { offset: usize },
    Timeout,
}

/// SPI NOR flash ISP driver
pub struct SpiFlash<SPI> {
    spi: SPI,
}

impl<SPI> SpiFlash<SPI>
where
    SPI: SpiDevice,
{
    pub fn new(spi: SPI) -> Self {
        Self { spi }
    }

    /// Read JEDEC manufacturer + device ID (3 bytes)
    pub fn read_jedec_id(&mut self) -> Result<[u8; 3], FlashError<SPI::Error>> {
        let mut buf = [CMD_JEDEC_ID, 0x00, 0x00, 0x00];
        self.spi.transfer_in_place(&mut buf).map_err(FlashError::Spi)?;
        Ok([buf[1], buf[2], buf[3]])
    }

    /// Send Write Enable latch
    fn write_enable(&mut self) -> Result<(), FlashError<SPI::Error>> {
        self.spi
            .write(&[CMD_WRITE_ENABLE])
            .map_err(FlashError::Spi)
    }

    /// Poll WIP bit; returns Err(Timeout) after max_polls iterations
    fn wait_ready(&mut self, max_polls: u32) -> Result<(), FlashError<SPI::Error>> {
        for _ in 0..max_polls {
            let mut buf = [CMD_READ_STATUS1, 0x00];
            self.spi.transfer_in_place(&mut buf).map_err(FlashError::Spi)?;
            if buf[1] & 0x01 == 0 {
                return Ok(());
            }
        }
        Err(FlashError::Timeout)
    }

    /// Erase one 4 KB sector containing `addr`
    pub fn erase_sector(&mut self, addr: u32) -> Result<(), FlashError<SPI::Error>> {
        self.write_enable()?;
        let cmd = [
            CMD_SECTOR_ERASE,
            ((addr >> 16) & 0xFF) as u8,
            ((addr >> 8)  & 0xFF) as u8,
            (addr         & 0xFF) as u8,
        ];
        self.spi.write(&cmd).map_err(FlashError::Spi)?;
        self.wait_ready(50_000)  // up to ~5 s at 100 µs polling
    }

    /// Program one page (≤256 bytes). `addr` must be page-aligned.
    pub fn page_program(
        &mut self,
        addr: u32,
        data: &[u8],
    ) -> Result<(), FlashError<SPI::Error>> {
        assert!(data.len() <= PAGE_SIZE, "data exceeds page size");
        self.write_enable()?;

        let header = [
            CMD_PAGE_PROGRAM,
            ((addr >> 16) & 0xFF) as u8,
            ((addr >> 8)  & 0xFF) as u8,
            (addr         & 0xFF) as u8,
        ];
        // Send header then payload in one CS transaction
        self.spi.write(&header).map_err(FlashError::Spi)?;
        self.spi.write(data).map_err(FlashError::Spi)?;
        self.wait_ready(10_000)  // up to ~1 s
    }

    /// Read `len` bytes from `addr` into `buf`
    pub fn read(
        &mut self,
        addr: u32,
        buf: &mut [u8],
    ) -> Result<(), FlashError<SPI::Error>> {
        let header = [
            CMD_READ_DATA,
            ((addr >> 16) & 0xFF) as u8,
            ((addr >> 8)  & 0xFF) as u8,
            (addr         & 0xFF) as u8,
        ];
        self.spi.write(&header).map_err(FlashError::Spi)?;
        self.spi.read(buf).map_err(FlashError::Spi)?;
        Ok(())
    }

    /// High-level: erase, program, and verify a firmware image
    pub fn program_image(
        &mut self,
        base_addr: u32,
        image: &[u8],
    ) -> Result<(), FlashError<SPI::Error>> {
        // ── Erase ──────────────────────────────────────────────
        let sectors = image.len().div_ceil(SECTOR_SIZE);
        for s in 0..sectors {
            let addr = base_addr + (s * SECTOR_SIZE) as u32;
            self.erase_sector(addr)?;
        }

        // ── Program ────────────────────────────────────────────
        let mut offset = 0usize;
        while offset < image.len() {
            let chunk_size = (image.len() - offset).min(PAGE_SIZE);
            let addr = base_addr + offset as u32;
            self.page_program(addr, &image[offset..offset + chunk_size])?;
            offset += chunk_size;
        }

        // ── Verify ─────────────────────────────────────────────
        let mut read_buf = [0u8; PAGE_SIZE];
        offset = 0;
        while offset < image.len() {
            let chunk_size = (image.len() - offset).min(PAGE_SIZE);
            let addr = base_addr + offset as u32;
            self.read(addr, &mut read_buf[..chunk_size])?;
            if read_buf[..chunk_size] != image[offset..offset + chunk_size] {
                return Err(FlashError::VerifyFailed { offset });
            }
            offset += chunk_size;
        }

        Ok(())
    }
}
```

### AVR ISP Programmer in Rust (GPIO bit-bang)

```rust
// avr_isp.rs — AVR ISP over software SPI in Rust
//
// Uses embedded-hal OutputPin + InputPin for GPIO

use embedded_hal::digital::{InputPin, OutputPin};

pub struct AvrIsp<MOSI, MISO, SCK, RESET>
where
    MOSI:  OutputPin,
    MISO:  InputPin,
    SCK:   OutputPin,
    RESET: OutputPin,
{
    mosi:  MOSI,
    miso:  MISO,
    sck:   SCK,
    reset: RESET,
}

impl<MOSI, MISO, SCK, RESET> AvrIsp<MOSI, MISO, SCK, RESET>
where
    MOSI:  OutputPin,
    MISO:  InputPin,
    SCK:   OutputPin,
    RESET: OutputPin,
{
    pub fn new(mosi: MOSI, miso: MISO, sck: SCK, reset: RESET) -> Self {
        Self { mosi, miso, sck, reset }
    }

    /// Transfer one byte (SPI Mode 0), return received byte
    fn transfer(&mut self, tx: u8) -> u8 {
        let mut rx = 0u8;
        for bit in (0..8).rev() {
            // Set MOSI
            if (tx >> bit) & 1 == 1 {
                let _ = self.mosi.set_high();
            } else {
                let _ = self.mosi.set_low();
            }
            // Rising edge
            let _ = self.sck.set_high();
            if self.miso.is_high().unwrap_or(false) {
                rx |= 1 << bit;
            }
            // Falling edge
            let _ = self.sck.set_low();
        }
        rx
    }

    /// Enter ISP programming mode
    pub fn enter_programming(&mut self) -> bool {
        let _ = self.reset.set_low();
        // Platform-specific delay ~25 ms needed here
        self.transfer(0xAC);
        self.transfer(0x53);
        self.transfer(0x00);
        let echo = self.transfer(0x00);
        echo == 0x53
    }

    /// Exit ISP programming mode
    pub fn exit_programming(&mut self) {
        let _ = self.reset.set_high();
    }

    /// Read the 3-byte signature
    pub fn read_signature(&mut self) -> [u8; 3] {
        core::array::from_fn(|i| {
            self.transfer(0x30);
            self.transfer(0x00);
            self.transfer(i as u8);
            self.transfer(0x00)
        })
    }

    /// Chip erase (erases flash and EEPROM)
    pub fn chip_erase(&mut self) {
        self.transfer(0xAC);
        self.transfer(0x80);
        self.transfer(0x00);
        self.transfer(0x00);
        // Delay tWD_ERASE ≥ 9 ms needed here
    }

    /// Load one word (low/high) into the internal page buffer
    pub fn load_page_word(&mut self, word_addr: u8, low: u8, high: u8) {
        self.transfer(0x40); self.transfer(0x00); self.transfer(word_addr); self.transfer(low);
        self.transfer(0x48); self.transfer(0x00); self.transfer(word_addr); self.transfer(high);
    }

    /// Commit the page buffer to flash at `page_word_addr`
    pub fn write_page(&mut self, page_word_addr: u16) {
        self.transfer(0x4C);
        self.transfer(((page_word_addr >> 8) & 0xFF) as u8);
        self.transfer((page_word_addr & 0xFF) as u8);
        self.transfer(0x00);
        // Delay tWD_FLASH ≥ 4.5 ms needed here
    }

    /// Read a program memory byte (high = high byte of word)
    pub fn read_program_byte(&mut self, word_addr: u16, high: bool) -> u8 {
        self.transfer(if high { 0x28 } else { 0x20 });
        self.transfer(((word_addr >> 8) & 0xFF) as u8);
        self.transfer((word_addr & 0xFF) as u8);
        self.transfer(0x00)
    }

    /// Program a complete firmware image into AVR flash
    pub fn program_flash(&mut self, image: &[u8], page_words: usize) -> bool {
        if !self.enter_programming() {
            return false;
        }
        self.chip_erase();

        let total_words = (image.len() + 1) / 2;
        let mut word_idx = 0;

        while word_idx < total_words {
            // Align to page boundary
            let page_start = word_idx & !(page_words - 1);

            for pw in 0..page_words {
                let byte_idx = (page_start + pw) * 2;
                let lo = image.get(byte_idx).copied().unwrap_or(0xFF);
                let hi = image.get(byte_idx + 1).copied().unwrap_or(0xFF);
                self.load_page_word(pw as u8, lo, hi);
            }

            self.write_page(page_start as u16);
            word_idx += page_words;
        }

        // Verify
        for w in 0..total_words {
            let lo_exp = image.get(w * 2).copied().unwrap_or(0xFF);
            let hi_exp = image.get(w * 2 + 1).copied().unwrap_or(0xFF);
            if self.read_program_byte(w as u16, false) != lo_exp
                || self.read_program_byte(w as u16, true) != hi_exp
            {
                self.exit_programming();
                return false;
            }
        }

        self.exit_programming();
        true
    }
}
```

---

## Bootloader Design via SPI

A common embedded pattern is a **primary bootloader** that:

1. Checks a flag in non-volatile memory (EEPROM, flash, or backup register) indicating a pending update
2. If set: reads the new firmware image from an external SPI flash
3. Programs it into the MCU's internal flash using the internal flash write API
4. Clears the update flag
5. Jumps to the application

```c
/*
 * bootloader.c — Bootloader that reads firmware from SPI flash
 * and self-programs internal MCU flash (STM32-like pseudocode)
 */

#include <stdint.h>
#include <string.h>

#define APP_START_ADDR          0x08004000U  /* after bootloader sector */
#define SPI_FLASH_FW_ADDR       0x00010000U  /* where new firmware is stored */
#define UPDATE_FLAG_ADDR        0x08003FF8U  /* last 8 bytes of bootloader sector */
#define UPDATE_FLAG_MAGIC       0xDEADBEEFU

#define INTERNAL_FLASH_PAGE     2048U        /* STM32F0: 2 KB pages */

extern int    spi_flash_read(uint32_t addr, uint8_t *buf, size_t len);
extern int    internal_flash_erase_page(uint32_t addr);
extern int    internal_flash_write_word(uint32_t addr, uint32_t word);
extern void   jump_to_application(uint32_t app_addr);

static bool update_pending(void)
{
    uint32_t flag;
    memcpy(&flag, (void *)UPDATE_FLAG_ADDR, sizeof(flag));
    return flag == UPDATE_FLAG_MAGIC;
}

static void clear_update_flag(void)
{
    internal_flash_erase_page(UPDATE_FLAG_ADDR & ~(INTERNAL_FLASH_PAGE - 1));
    /* Re-program other data in that page as needed */
}

static int apply_firmware_update(void)
{
    /* Read firmware header from SPI flash */
    struct {
        uint32_t magic;
        uint32_t size;
        uint32_t crc32;
    } hdr;

    spi_flash_read(SPI_FLASH_FW_ADDR, (uint8_t *)&hdr, sizeof(hdr));
    if (hdr.magic != 0xFEEDFACEU) return -1;  /* invalid header */
    if (hdr.size > (256 * 1024))   return -1;  /* sanity limit */

    /* Erase target region in internal flash */
    uint32_t pages = (hdr.size + INTERNAL_FLASH_PAGE - 1) / INTERNAL_FLASH_PAGE;
    for (uint32_t p = 0; p < pages; p++) {
        internal_flash_erase_page(APP_START_ADDR + p * INTERNAL_FLASH_PAGE);
    }

    /* Copy from SPI flash to internal flash, 4 bytes at a time */
    uint32_t src = SPI_FLASH_FW_ADDR + sizeof(hdr);
    uint32_t dst = APP_START_ADDR;
    uint32_t remaining = hdr.size;

    while (remaining >= 4) {
        uint32_t word;
        spi_flash_read(src, (uint8_t *)&word, 4);
        internal_flash_write_word(dst, word);
        src       += 4;
        dst       += 4;
        remaining -= 4;
    }

    /* Handle remaining bytes (pad with 0xFF) */
    if (remaining > 0) {
        uint32_t word = 0xFFFFFFFF;
        spi_flash_read(src, (uint8_t *)&word, remaining);
        internal_flash_write_word(dst, word);
    }

    clear_update_flag();
    return 0;
}

void bootloader_main(void)
{
    if (update_pending()) {
        if (apply_firmware_update() == 0) {
            /* Reset to run new firmware cleanly */
            /* NVIC_SystemReset(); */
        }
        /* On failure: fall through to existing application or halt */
    }

    jump_to_application(APP_START_ADDR);
}
```

---

## Firmware Update (FOTA) Patterns

### Dual-Bank (A/B) Update Pattern

```
┌─────────────────────────────────────────────────┐
│                 SPI NOR Flash                    │
├───────────────────┬─────────────────────────────┤
│   Bank A (active) │  Bank B (new firmware)       │
│   0x00000000      │  0x00080000                  │
└───────────────────┴─────────────────────────────┘
         ▲
         │ Bootloader reads Bank A normally.
         │ On update: write new firmware to Bank B,
         │ verify, set flag → bootloader swaps banks.
```

```rust
// fota_dualbank.rs — Dual-bank firmware update state machine

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum BankSlot { A, B }

impl BankSlot {
    pub fn base_addr(self) -> u32 {
        match self {
            BankSlot::A => 0x0000_0000,
            BankSlot::B => 0x0008_0000,
        }
    }

    pub fn other(self) -> BankSlot {
        match self {
            BankSlot::A => BankSlot::B,
            BankSlot::B => BankSlot::A,
        }
    }
}

pub struct FotaManager<SPI> {
    flash: SpiFlash<SPI>,
    active_bank: BankSlot,
}

impl<SPI: embedded_hal::spi::SpiDevice> FotaManager<SPI> {
    pub fn new(flash: SpiFlash<SPI>, active: BankSlot) -> Self {
        Self { flash, active_bank: active }
    }

    /// Write new firmware to the inactive bank and verify it
    pub fn stage_update(
        &mut self,
        firmware: &[u8],
    ) -> Result<(), crate::FlashError<SPI::Error>> {
        let target = self.active_bank.other();
        self.flash.program_image(target.base_addr(), firmware)?;
        Ok(())
    }

    /// Mark the inactive bank as the new active bank (persisted to flash metadata)
    pub fn commit_swap(&mut self) {
        self.active_bank = self.active_bank.other();
        // In a real system: write self.active_bank to a metadata sector
    }
}
```

### CRC32 Integrity Check

Always verify firmware integrity before and after flashing:

```c
/* crc32.c — Standard CRC-32/ISO-HDLC */

#include <stdint.h>
#include <stddef.h>

static const uint32_t CRC32_TABLE[256] = { /* ... generated by polynomial 0xEDB88320 ... */ };

uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}
```

---

## Error Handling and Verification

### Read-Back Verification

Always read back programmed data and compare byte-for-byte:

```rust
pub fn verify_image<SPI: embedded_hal::spi::SpiDevice>(
    flash: &mut SpiFlash<SPI>,
    base_addr: u32,
    expected: &[u8],
) -> Result<bool, FlashError<SPI::Error>> {
    let mut buf = [0u8; PAGE_SIZE];
    let mut offset = 0usize;

    while offset < expected.len() {
        let len = (expected.len() - offset).min(PAGE_SIZE);
        flash.read(base_addr + offset as u32, &mut buf[..len])?;
        if buf[..len] != expected[offset..offset + len] {
            return Ok(false);
        }
        offset += len;
    }
    Ok(true)
}
```

### Retry Logic in C

```c
#define ISP_MAX_RETRIES  3

int flash_program_with_retry(spi_ctx_t *ctx, uint32_t addr,
                              const uint8_t *data, size_t len)
{
    for (int attempt = 0; attempt < ISP_MAX_RETRIES; attempt++) {
        if (flash_page_program(ctx, addr, data, len) == 0) {
            /* Verify */
            uint8_t verify[256];
            flash_read(ctx, addr, verify, len);
            if (memcmp(verify, data, len) == 0) return 0;
        }
        fprintf(stderr, "[ISP] Retry %d for addr 0x%06X\n", attempt + 1, addr);
        /* Optional: re-erase the sector before retrying */
        flash_sector_erase(ctx, addr & ~(FLASH_SECTOR_SIZE - 1));
    }
    return -1;
}
```

---

## Security Considerations

ISP via SPI introduces security risks that must be addressed in production systems:

### Firmware Authentication

Before writing any image, verify a cryptographic signature:

```rust
// Example: verify Ed25519 signature over firmware before flashing
// Uses the `ed25519-dalek` crate

use ed25519_dalek::{VerifyingKey, Signature, Verifier};

pub fn verify_firmware_signature(
    firmware: &[u8],
    signature: &[u8; 64],
    public_key: &[u8; 32],
) -> bool {
    let Ok(vk)  = VerifyingKey::from_bytes(public_key) else { return false; };
    let Ok(sig) = Signature::from_bytes(signature)     else { return false; };
    vk.verify(firmware, &sig).is_ok()
}
```

### Anti-Rollback Protection

Store a monotonic version counter in an OTP (one-time programmable) region or hardware fuse bits. Reject any firmware image with a version number lower than the current counter value.

### Flash Lock Bits and Read Protection

Most microcontrollers offer hardware readback protection. Enable these after final programming to prevent firmware extraction:

```c
/* STM32: Enable RDP Level 1 (read protection) via Option Bytes */
/* WARNING: This is irreversible at Level 2 */
void enable_flash_read_protection_level1(void)
{
    FLASH->OPTKEYR = 0x08192A3B;
    FLASH->OPTKEYR = 0x4C5D6E7F;
    FLASH->OPTR = (FLASH->OPTR & ~0xFF) | 0xBB;  /* RDP = 0xBB = Level 1 */
    FLASH->CR |= FLASH_CR_OPTSTRT;
    while (FLASH->SR & FLASH_SR_BSY);
}
```

---

## Summary

SPI-based In-System Programming is a foundational technique in embedded systems engineering. The key concepts are:

**Protocol Mechanics:** ISP over SPI relies on a simple 4-wire bus (MOSI, MISO, SCK, CS) and a standardized command set of opcodes, addresses, and data frames. Most NOR flash devices follow a Write Enable → Erase → Program → Verify → Poll sequence. AVR microcontrollers extend this with a 4-byte command framing protocol and a dedicated `RESET` line.

**Implementation Strategies:** For maximum portability, hardware-agnostic abstractions (Rust's `embedded-hal`, or HAL layers in C) decouple the ISP logic from specific silicon. Hardware SPI peripherals with DMA are preferred in production for throughput; bit-banging GPIO is used for flexibility or on platforms without SPI hardware.

**Bootloaders and FOTA:** The most powerful use of SPI ISP is in field-updatable systems. Bootloaders can receive new firmware over any transport (UART, USB, CAN, Ethernet) and write it to a staging area in an external SPI flash. Dual-bank (A/B) schemes ensure the device always has a known-good fallback. Integrity is verified via CRC-32 or SHA hashes; authenticity via digital signatures (Ed25519, ECDSA).

**Security:** Production ISP flows must verify image signatures, enforce anti-rollback version monotonicity, and activate hardware read protection after programming to prevent cloning or IP theft.

**Language Trade-offs:**

| Aspect | C/C++ | Rust |
|---|---|---|
| Hardware access | Direct register / HAL | `embedded-hal` traits |
| Memory safety | Manual (error-prone) | Enforced by compiler |
| Error handling | Return codes / errno | `Result<T, E>` type |
| Portability | HAL abstraction | Trait generics |
| Ecosystem | Mature, vendor SDKs | Growing (`probe-rs`, `embassy`) |

Both languages are well-suited for ISP. Rust's type system and ownership model eliminate entire classes of memory bugs common in C flash drivers, making it increasingly popular for safety-critical firmware update paths.

---

*Document: 82 — In-System Programming via SPI | Revision 1.0*