# 83. Dual-Bank SPI Flash

**Concept sections** — Memory layout diagram, boot flow chart with rollback logic, SPI flash command reference, and the full update state machine.

**C/C++ implementation** — A `dual_bank_flash.h` header with all types, a low-level SPI driver using standard opcodes (`READ`, `PAGE PROGRAM`, `SECTOR ERASE`), streaming CRC32 verification (no full image buffering in RAM), the dual-bank manager with `begin_update` / `write_chunk` / `finalize_update` / `confirm_boot` / `rollback`, and a complete bootloader entry point.

**Rust implementation** — A `SpiFlash` trait for HAL abstraction, `repr(C, packed)` metadata and header structs, a generic `DualBankManager<F: SpiFlash>` with the same API surface, and an example application startup using the manager.

**Watchdog integration** — Code patterns showing how the watchdog + boot counter drives automatic rollback if new firmware fails to confirm itself within N boot attempts.

**Summary** — Covers the architecture, fail-safety guarantees, and notes on extending CRC32 with cryptographic signing for security-sensitive deployments.

## Implementing Fail-Safe Firmware Updates with Redundant Flash Banks

---

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Memory Layout and Bank Architecture](#memory-layout-and-bank-architecture)
4. [Boot Flow and Bank Selection](#boot-flow-and-bank-selection)
5. [SPI Flash Hardware Interface](#spi-flash-hardware-interface)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Integrity Verification](#integrity-verification)
9. [Update State Machine](#update-state-machine)
10. [Watchdog Integration](#watchdog-integration)
11. [Summary](#summary)

---

## Introduction

Dual-Bank SPI Flash is a firmware update strategy that provides **fail-safe, atomic firmware upgrades** on embedded systems. Instead of writing new firmware over the running image — which risks bricking the device if power is lost or the image is corrupted mid-write — two complete firmware slots are maintained in SPI flash. One bank runs the active firmware while the other receives the update. The bootloader switches banks only after the new image has been fully written and verified.

This pattern is essential for:

- **Field-deployed IoT devices** where physical access for recovery is impractical
- **Safety-critical systems** where downtime or corruption is unacceptable
- **OTA (Over-the-Air) update** scenarios with unreliable network connectivity
- Any embedded product where a failed update must not render the device unresponsive

---

## Core Concepts

**Bank A / Bank B** — Two equally-sized regions in SPI flash, each capable of holding a complete firmware image. Only one bank is "active" at any time; the other is the "inactive" or "update" slot.

**Bootloader** — A small, never-overwritten piece of code (often in internal MCU flash or a protected sector) that reads metadata to decide which bank to boot from.

**Boot Metadata / Flags** — A small structure in a dedicated flash region (or EEPROM) that tracks: which bank is active, whether a new image is pending, and a rollback counter.

**Commit / Confirm** — After booting from the new bank and verifying correct operation, the application writes a "confirmed" flag. If the system reboots before confirmation (e.g., power loss, watchdog reset), the bootloader rolls back to the known-good bank.

**CRC / Hash Verification** — Each bank image includes a checksum or cryptographic hash. The bootloader validates this before switching banks.

---

## Memory Layout and Bank Architecture

```
SPI Flash (e.g., 8 MB W25Q64)
┌─────────────────────────────────────────┐  0x000000
│         Bootloader Metadata             │  (4 KB)
│   (active bank, flags, boot counter)    │
├─────────────────────────────────────────┤  0x001000
│              Bank A                     │  (3.5 MB)
│   Firmware Image + Header (CRC/size)    │
│                                         │
├─────────────────────────────────────────┤  0x381000
│              Bank B                     │  (3.5 MB)
│   Firmware Image + Header (CRC/size)    │
│                                         │
├─────────────────────────────────────────┤  0x701000
│         Reserved / Log Area             │  (remaining)
└─────────────────────────────────────────┘  0x7FFFFF
```

Each bank begins with a small **image header** followed by the actual firmware binary:

```
Bank Image Layout:
┌──────────────┬───────────┬───────────┬──────────┬────────────────────┐
│  Magic (4B)  │ Version   │ Size (4B) │ CRC32    │  Firmware Binary   │
│  0xDEADBEEF  │ (4B)      │           │  (4B)    │  (up to bank size) │
└──────────────┴───────────┴───────────┴──────────┴────────────────────┘
```

---

## Boot Flow and Bank Selection

```
Power On / Reset
      │
      ▼
┌─────────────────┐
│  Read Boot Meta │  ← metadata sector (active bank, boot count, confirmed flag)
└────────┬────────┘
         │
         ▼
┌─────────────────────┐      ┌──────────────────────┐
│  Pending update?    │─ Yes─▶  Verify new bank CRC  │
└────────┬────────────┘      └──────────┬───────────┘
         │ No                           │
         │                    Valid? ───┼─── No ──▶ Clear pending, keep active bank
         │                             │ Yes
         │                             ▼
         │                  ┌─────────────────────┐
         │                  │  Switch active bank  │
         │                  │  Reset boot counter  │
         │                  └──────────┬──────────┘
         │                             │
         ▼                             ▼
┌─────────────────────────────────────────┐
│  Boot counter < MAX_BOOT_ATTEMPTS?      │
└────────┬──────────────┬─────────────────┘
         │ Yes          │ No
         ▼              ▼
   Increment      Rollback to
   counter &      previous bank
   boot image
         │
         ▼
┌─────────────────┐
│  Jump to Image  │
└─────────────────┘
         │
         ▼  (application runs)
┌─────────────────────────────┐
│  App confirms boot (writes  │
│  "confirmed" flag to flash) │
└─────────────────────────────┘
```

---

## SPI Flash Hardware Interface

SPI flash chips (e.g., Winbond W25Qxx, Macronix MX25Lxx) use a standard command set over SPI:

| Command       | Opcode | Description                            |
|---------------|--------|----------------------------------------|
| READ          | 0x03   | Read bytes from address                |
| PAGE PROGRAM  | 0x02   | Write up to 256 bytes (page)           |
| SECTOR ERASE  | 0x20   | Erase 4 KB sector                      |
| BLOCK ERASE   | 0xD8   | Erase 64 KB block                      |
| CHIP ERASE    | 0xC7   | Erase entire chip                      |
| WRITE ENABLE  | 0x06   | Must precede any write/erase           |
| READ STATUS   | 0x05   | Check WIP (Write-In-Progress) bit      |
| JEDEC ID      | 0x9F   | Read manufacturer/device ID            |

**Key hardware constraint**: Flash must be erased (to 0xFF) before programming. Writes can only clear bits (1→0); erase sets bits back to 1. Sector size is typically 4 KB, so firmware images must be written sector-by-sector.

---

## C/C++ Implementation

### Header: Dual-Bank Flash Manager

```c
/* dual_bank_flash.h */
#ifndef DUAL_BANK_FLASH_H
#define DUAL_BANK_FLASH_H

#include <stdint.h>
#include <stdbool.h>

/* ─── Flash Layout ─────────────────────────────────────────────────── */
#define FLASH_META_ADDR       0x000000UL
#define FLASH_META_SIZE       0x001000UL   /* 4 KB metadata sector    */
#define FLASH_BANK_A_ADDR     0x001000UL
#define FLASH_BANK_B_ADDR     0x381000UL
#define FLASH_BANK_SIZE       0x380000UL   /* 3.5 MB per bank         */
#define FLASH_PAGE_SIZE       256U
#define FLASH_SECTOR_SIZE     4096U

/* ─── Magic Numbers ────────────────────────────────────────────────── */
#define IMG_MAGIC             0xDEADBEEFUL
#define META_MAGIC            0xABCD1234UL

/* ─── Boot Settings ────────────────────────────────────────────────── */
#define MAX_BOOT_ATTEMPTS     3U

/* ─── Image Header (stored at start of each bank) ─────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;        /* IMG_MAGIC                               */
    uint32_t version;      /* Firmware version (BCD or semantic)      */
    uint32_t image_size;   /* Size of firmware body in bytes          */
    uint32_t crc32;        /* CRC32 of firmware body                  */
    uint8_t  reserved[48]; /* Padding to 64 bytes                     */
} image_header_t;

/* ─── Boot Metadata (stored at FLASH_META_ADDR) ───────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* META_MAGIC                           */
    uint8_t  active_bank;     /* 0 = Bank A, 1 = Bank B              */
    uint8_t  pending_bank;    /* 0xFF = none pending                  */
    uint8_t  boot_count;      /* Incremented each unconfirmed boot    */
    uint8_t  confirmed;       /* 1 = current bank is confirmed good   */
    uint32_t sequence;        /* Monotonic counter (anti-rollback)    */
    uint32_t crc32;           /* CRC32 of this metadata struct        */
    uint8_t  reserved[48];    /* Padding to 64 bytes total            */
} boot_meta_t;

typedef enum {
    BANK_A = 0,
    BANK_B = 1
} bank_id_t;

typedef enum {
    FLASH_OK            =  0,
    FLASH_ERR_TIMEOUT   = -1,
    FLASH_ERR_CRC       = -2,
    FLASH_ERR_SIZE      = -3,
    FLASH_ERR_MAGIC     = -4,
    FLASH_ERR_META      = -5,
} flash_err_t;

/* ─── Public API ───────────────────────────────────────────────────── */
flash_err_t flash_init(void);
flash_err_t flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
flash_err_t flash_write_page(uint32_t addr, const uint8_t *buf, uint32_t len);
flash_err_t flash_erase_sector(uint32_t addr);

uint32_t    flash_bank_addr(bank_id_t bank);
flash_err_t flash_read_meta(boot_meta_t *meta);
flash_err_t flash_write_meta(const boot_meta_t *meta);

flash_err_t flash_begin_update(bank_id_t target);
flash_err_t flash_write_chunk(bank_id_t bank, uint32_t offset,
                               const uint8_t *data, uint32_t len);
flash_err_t flash_finalize_update(bank_id_t bank, uint32_t version,
                                   uint32_t image_size, uint32_t crc32);
flash_err_t flash_confirm_boot(void);
flash_err_t flash_rollback(void);

uint32_t    crc32_compute(const uint8_t *data, uint32_t len);

#endif /* DUAL_BANK_FLASH_H */
```

### Low-Level SPI Flash Driver

```c
/* spi_flash_driver.c  —  Hardware-specific SPI flash commands */
#include "dual_bank_flash.h"
#include "spi_hal.h"          /* MCU-specific SPI HAL (user supplies)  */

/* ─── SPI Flash Opcodes ────────────────────────────────────────────── */
#define CMD_READ          0x03
#define CMD_PAGE_PROGRAM  0x02
#define CMD_SECTOR_ERASE  0x20
#define CMD_WRITE_ENABLE  0x06
#define CMD_READ_STATUS   0x05
#define CMD_JEDEC_ID      0x9F

#define STATUS_WIP_BIT    0x01   /* Write-In-Progress                  */
#define WRITE_TIMEOUT_MS  500U

/* ─── Wait for Write-In-Progress to clear ─────────────────────────── */
static flash_err_t wait_ready(void)
{
    uint32_t start = hal_tick_ms();
    uint8_t  status;

    do {
        hal_spi_cs_low();
        hal_spi_tx_byte(CMD_READ_STATUS);
        status = hal_spi_rx_byte();
        hal_spi_cs_high();

        if ((hal_tick_ms() - start) > WRITE_TIMEOUT_MS) {
            return FLASH_ERR_TIMEOUT;
        }
    } while (status & STATUS_WIP_BIT);

    return FLASH_OK;
}

/* ─── Send Write Enable latch ──────────────────────────────────────── */
static void write_enable(void)
{
    hal_spi_cs_low();
    hal_spi_tx_byte(CMD_WRITE_ENABLE);
    hal_spi_cs_high();
}

/* ─── Read bytes from flash ────────────────────────────────────────── */
flash_err_t flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    hal_spi_cs_low();
    hal_spi_tx_byte(CMD_READ);
    hal_spi_tx_byte((addr >> 16) & 0xFF);
    hal_spi_tx_byte((addr >>  8) & 0xFF);
    hal_spi_tx_byte( addr        & 0xFF);
    hal_spi_rx_buf(buf, len);
    hal_spi_cs_high();
    return FLASH_OK;
}

/* ─── Write one page (≤256 bytes, must not cross page boundary) ────── */
flash_err_t flash_write_page(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (len > FLASH_PAGE_SIZE) {
        return FLASH_ERR_SIZE;
    }

    flash_err_t err = wait_ready();
    if (err != FLASH_OK) return err;

    write_enable();

    hal_spi_cs_low();
    hal_spi_tx_byte(CMD_PAGE_PROGRAM);
    hal_spi_tx_byte((addr >> 16) & 0xFF);
    hal_spi_tx_byte((addr >>  8) & 0xFF);
    hal_spi_tx_byte( addr        & 0xFF);
    hal_spi_tx_buf(buf, len);
    hal_spi_cs_high();

    return wait_ready();
}

/* ─── Erase one 4KB sector (addr must be sector-aligned) ──────────── */
flash_err_t flash_erase_sector(uint32_t addr)
{
    flash_err_t err = wait_ready();
    if (err != FLASH_OK) return err;

    write_enable();

    hal_spi_cs_low();
    hal_spi_tx_byte(CMD_SECTOR_ERASE);
    hal_spi_tx_byte((addr >> 16) & 0xFF);
    hal_spi_tx_byte((addr >>  8) & 0xFF);
    hal_spi_tx_byte( addr        & 0xFF);
    hal_spi_cs_high();

    return wait_ready();
}
```

### CRC32 Computation

```c
/* crc32.c  —  IEEE 802.3 CRC32 (no lookup table, suitable for small MCUs) */
#include "dual_bank_flash.h"

uint32_t crc32_compute(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;

    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
        }
    }
    return ~crc;
}
```

### Dual-Bank Manager

```c
/* dual_bank_flash.c  —  High-level dual-bank firmware update logic */
#include "dual_bank_flash.h"
#include <string.h>

/* ─── Helper: get base address for a bank ──────────────────────────── */
uint32_t flash_bank_addr(bank_id_t bank)
{
    return (bank == BANK_A) ? FLASH_BANK_A_ADDR : FLASH_BANK_B_ADDR;
}

/* ─── Read and validate boot metadata ──────────────────────────────── */
flash_err_t flash_read_meta(boot_meta_t *meta)
{
    flash_err_t err = flash_read(FLASH_META_ADDR, (uint8_t *)meta,
                                  sizeof(boot_meta_t));
    if (err != FLASH_OK) return err;

    if (meta->magic != META_MAGIC) return FLASH_ERR_META;

    /* Verify metadata CRC (covers all fields except the crc32 field itself) */
    uint32_t computed = crc32_compute((uint8_t *)meta,
                                       offsetof(boot_meta_t, crc32));
    if (computed != meta->crc32) return FLASH_ERR_CRC;

    return FLASH_OK;
}

/* ─── Write boot metadata with updated CRC ─────────────────────────── */
flash_err_t flash_write_meta(const boot_meta_t *meta)
{
    boot_meta_t tmp;
    memcpy(&tmp, meta, sizeof(boot_meta_t));

    tmp.crc32 = crc32_compute((uint8_t *)&tmp,
                               offsetof(boot_meta_t, crc32));

    /* Erase the metadata sector first */
    flash_err_t err = flash_erase_sector(FLASH_META_ADDR);
    if (err != FLASH_OK) return err;

    /* Write back */
    return flash_write_page(FLASH_META_ADDR, (uint8_t *)&tmp,
                             sizeof(boot_meta_t));
}

/* ─── Verify bank image integrity ───────────────────────────────────── */
static flash_err_t verify_bank(bank_id_t bank)
{
    image_header_t hdr;
    uint32_t base = flash_bank_addr(bank);

    flash_err_t err = flash_read(base, (uint8_t *)&hdr, sizeof(image_header_t));
    if (err != FLASH_OK) return err;

    if (hdr.magic != IMG_MAGIC)       return FLASH_ERR_MAGIC;
    if (hdr.image_size > FLASH_BANK_SIZE - sizeof(image_header_t))
                                       return FLASH_ERR_SIZE;

    /* Stream-verify CRC without loading the full image into RAM */
    uint8_t  buf[FLASH_PAGE_SIZE];
    uint32_t remaining  = hdr.image_size;
    uint32_t read_addr  = base + sizeof(image_header_t);
    uint32_t running_crc = 0xFFFFFFFFUL;

    while (remaining) {
        uint32_t chunk = (remaining < FLASH_PAGE_SIZE)
                         ? remaining : FLASH_PAGE_SIZE;
        err = flash_read(read_addr, buf, chunk);
        if (err != FLASH_OK) return err;

        for (uint32_t i = 0; i < chunk; i++) {
            running_crc ^= buf[i];
            for (int b = 0; b < 8; b++)
                running_crc = (running_crc >> 1)
                              ^ (0xEDB88320UL & -(running_crc & 1));
        }
        read_addr += chunk;
        remaining -= chunk;
    }

    uint32_t final_crc = ~running_crc;
    return (final_crc == hdr.crc32) ? FLASH_OK : FLASH_ERR_CRC;
}

/* ─── Begin update: erase the target bank ──────────────────────────── */
flash_err_t flash_begin_update(bank_id_t target)
{
    uint32_t base     = flash_bank_addr(target);
    uint32_t sectors  = FLASH_BANK_SIZE / FLASH_SECTOR_SIZE;

    for (uint32_t s = 0; s < sectors; s++) {
        flash_err_t err = flash_erase_sector(base + s * FLASH_SECTOR_SIZE);
        if (err != FLASH_OK) return err;
    }
    return FLASH_OK;
}

/* ─── Write a chunk of firmware data to the update bank ────────────── */
flash_err_t flash_write_chunk(bank_id_t bank, uint32_t offset,
                               const uint8_t *data, uint32_t len)
{
    /* Offset is relative to start of firmware body (after header) */
    uint32_t base    = flash_bank_addr(bank) + sizeof(image_header_t);
    uint32_t written = 0;

    while (written < len) {
        uint32_t addr       = base + offset + written;
        uint32_t page_off   = addr % FLASH_PAGE_SIZE;
        uint32_t chunk      = FLASH_PAGE_SIZE - page_off;
        if (chunk > (len - written)) chunk = len - written;

        flash_err_t err = flash_write_page(addr, data + written, chunk);
        if (err != FLASH_OK) return err;
        written += chunk;
    }
    return FLASH_OK;
}

/* ─── Finalize: write image header and set pending flag ─────────────── */
flash_err_t flash_finalize_update(bank_id_t bank, uint32_t version,
                                   uint32_t image_size, uint32_t crc32)
{
    image_header_t hdr = {
        .magic      = IMG_MAGIC,
        .version    = version,
        .image_size = image_size,
        .crc32      = crc32,
    };
    memset(hdr.reserved, 0xFF, sizeof(hdr.reserved));

    /* Write header at the base of the bank */
    flash_err_t err = flash_write_page(flash_bank_addr(bank),
                                        (uint8_t *)&hdr,
                                        sizeof(image_header_t));
    if (err != FLASH_OK) return err;

    /* Verify before committing */
    err = verify_bank(bank);
    if (err != FLASH_OK) return err;

    /* Mark as pending in metadata */
    boot_meta_t meta;
    err = flash_read_meta(&meta);
    if (err != FLASH_OK) return err;

    meta.pending_bank = (uint8_t)bank;
    meta.boot_count   = 0;
    meta.confirmed    = 0;
    meta.sequence++;

    return flash_write_meta(&meta);
}

/* ─── Application calls this after successful boot to confirm ─────── */
flash_err_t flash_confirm_boot(void)
{
    boot_meta_t meta;
    flash_err_t err = flash_read_meta(&meta);
    if (err != FLASH_OK) return err;

    meta.confirmed  = 1;
    meta.boot_count = 0;

    return flash_write_meta(&meta);
}

/* ─── Force rollback to opposite bank ──────────────────────────────── */
flash_err_t flash_rollback(void)
{
    boot_meta_t meta;
    flash_err_t err = flash_read_meta(&meta);
    if (err != FLASH_OK) return err;

    meta.active_bank  = (meta.active_bank == BANK_A) ? BANK_B : BANK_A;
    meta.pending_bank = 0xFF;
    meta.confirmed    = 1;
    meta.boot_count   = 0;

    return flash_write_meta(&meta);
}
```

### Bootloader Entry Point (C)

```c
/* bootloader_main.c  —  Minimal bootloader: reads metadata, boots correct bank */
#include "dual_bank_flash.h"
#include <string.h>

/* MCU-specific: copy flash image to SRAM and jump to reset handler */
extern void load_and_jump(uint32_t flash_addr, uint32_t image_size);

void bootloader_main(void)
{
    boot_meta_t meta;

    /* ── Step 1: Read metadata. If corrupt, default to Bank A ──────── */
    if (flash_read_meta(&meta) != FLASH_OK) {
        meta.active_bank  = BANK_A;
        meta.pending_bank = 0xFF;
        meta.boot_count   = 0;
        meta.confirmed    = 1;
        meta.sequence     = 0;
        meta.magic        = META_MAGIC;
        flash_write_meta(&meta);
    }

    /* ── Step 2: If update pending, verify and switch ───────────────── */
    if (meta.pending_bank != 0xFF) {
        bank_id_t new_bank = (bank_id_t)meta.pending_bank;

        /* Re-verify the incoming image */
        if (verify_bank(new_bank) == FLASH_OK) {          /* internal fn */
            meta.active_bank  = (uint8_t)new_bank;
            meta.pending_bank = 0xFF;
            meta.boot_count   = 0;
            meta.confirmed    = 0;
            flash_write_meta(&meta);
        }
        /* else: leave active_bank unchanged, clear pending */
        else {
            meta.pending_bank = 0xFF;
            flash_write_meta(&meta);
        }
    }

    /* ── Step 3: Rollback if too many unconfirmed boots ─────────────── */
    if (!meta.confirmed && meta.boot_count >= MAX_BOOT_ATTEMPTS) {
        meta.active_bank  = (meta.active_bank == BANK_A) ? BANK_B : BANK_A;
        meta.boot_count   = 0;
        meta.confirmed    = 1;
        flash_write_meta(&meta);
    }

    /* ── Step 4: Increment boot counter (cleared by confirm_boot) ───── */
    if (!meta.confirmed) {
        meta.boot_count++;
        flash_write_meta(&meta);
    }

    /* ── Step 5: Read active image header and jump ──────────────────── */
    image_header_t hdr;
    uint32_t bank_base = flash_bank_addr((bank_id_t)meta.active_bank);
    flash_read(bank_base, (uint8_t *)&hdr, sizeof(image_header_t));

    load_and_jump(bank_base + sizeof(image_header_t), hdr.image_size);

    /* Should never reach here */
    while (1) {}
}
```

---

## Rust Implementation

### Project Structure

```
dual_bank_flash/
├── Cargo.toml
└── src/
    ├── lib.rs
    ├── driver.rs      # SPI flash HAL trait + low-level driver
    ├── meta.rs        # Boot metadata types
    ├── image.rs       # Image header + verification
    ├── manager.rs     # Dual-bank update logic
    └── crc.rs         # CRC32
```

### Cargo.toml

```toml
[package]
name    = "dual_bank_flash"
version = "0.1.0"
edition = "2021"

[dependencies]
# no_std compatible; for embedded use
embedded-hal = "1.0"

[features]
default = []
std     = []   # enable for host-side testing

[profile.release]
opt-level = "s"   # optimize for size on embedded
lto       = true
```

### Types and Metadata (meta.rs)

```rust
// src/meta.rs
use core::mem;

pub const META_MAGIC:    u32 = 0xABCD_1234;
pub const IMG_MAGIC:     u32 = 0xDEAD_BEEF;
pub const MAX_BOOT_TRIES: u8 = 3;

/// Which of the two flash banks is referenced
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
pub enum BankId {
    A = 0,
    B = 1,
}

impl BankId {
    pub fn opposite(self) -> Self {
        match self {
            BankId::A => BankId::B,
            BankId::B => BankId::A,
        }
    }
}

/// Error types for all flash operations
#[derive(Debug, PartialEq, Eq)]
pub enum FlashError {
    Timeout,
    Crc,
    BadMagic,
    BadSize,
    BadMeta,
    Spi,
}

/// Image header stored at start of each bank (64 bytes)
#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct ImageHeader {
    pub magic:      u32,
    pub version:    u32,
    pub image_size: u32,
    pub crc32:      u32,
    pub reserved:   [u8; 48],
}

impl ImageHeader {
    pub const SIZE: usize = mem::size_of::<Self>();
}

/// Boot metadata stored in dedicated metadata sector (64 bytes)
#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct BootMeta {
    pub magic:        u32,
    pub active_bank:  u8,
    pub pending_bank: u8,   // 0xFF = none pending
    pub boot_count:   u8,
    pub confirmed:    u8,
    pub sequence:     u32,
    pub crc32:        u32,
    pub reserved:     [u8; 48],
}

impl BootMeta {
    pub const SIZE: usize = mem::size_of::<Self>();
    pub const CRC_FIELD_OFFSET: usize = 16; // offset of crc32 field

    pub fn new_default() -> Self {
        Self {
            magic:        META_MAGIC,
            active_bank:  BankId::A as u8,
            pending_bank: 0xFF,
            boot_count:   0,
            confirmed:    1,
            sequence:     0,
            crc32:        0,
            reserved:     [0xFF; 48],
        }
    }
}
```

### CRC32 (crc.rs)

```rust
// src/crc.rs

/// IEEE 802.3 CRC32, no lookup table (minimal RAM usage)
pub fn crc32(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFF_FFFF;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            let mask = (crc & 1).wrapping_neg(); // 0xFFFF_FFFF or 0x0000_0000
            crc = (crc >> 1) ^ (0xEDB8_8320 & mask);
        }
    }
    !crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_known_vector() {
        // CRC32 of "123456789" = 0xCBF43926
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
    }
}
```

### SPI Flash HAL Trait (driver.rs)

```rust
// src/driver.rs
use crate::meta::FlashError;

/// Platform-independent SPI flash trait.
/// Implement this for your MCU's SPI peripheral.
pub trait SpiFlash {
    /// Read `len` bytes from `addr` into `buf`
    fn read(&mut self, addr: u32, buf: &mut [u8]) -> Result<(), FlashError>;

    /// Write one page (≤256 bytes) at `addr`.
    /// Caller must ensure address/length fit within a single page.
    fn write_page(&mut self, addr: u32, data: &[u8]) -> Result<(), FlashError>;

    /// Erase a 4 KB sector at (sector-aligned) `addr`
    fn erase_sector(&mut self, addr: u32) -> Result<(), FlashError>;
}

/// Flash memory layout constants
pub struct Layout;

impl Layout {
    pub const META_ADDR:    u32 = 0x0000_0000;
    pub const META_SIZE:    u32 = 0x0000_1000; // 4 KB
    pub const BANK_A_ADDR:  u32 = 0x0000_1000;
    pub const BANK_B_ADDR:  u32 = 0x0038_1000;
    pub const BANK_SIZE:    u32 = 0x0038_0000; // 3.5 MB
    pub const PAGE_SIZE:    u32 = 256;
    pub const SECTOR_SIZE:  u32 = 4096;

    pub fn bank_addr(bank: crate::meta::BankId) -> u32 {
        match bank {
            crate::meta::BankId::A => Self::BANK_A_ADDR,
            crate::meta::BankId::B => Self::BANK_B_ADDR,
        }
    }
}
```

### Dual-Bank Manager (manager.rs)

```rust
// src/manager.rs
use crate::{
    crc::crc32,
    driver::{Layout, SpiFlash},
    meta::{BankId, BootMeta, FlashError, ImageHeader,
           IMG_MAGIC, MAX_BOOT_TRIES, META_MAGIC},
};
use core::mem;

pub struct DualBankManager<F: SpiFlash> {
    flash: F,
}

impl<F: SpiFlash> DualBankManager<F> {
    pub fn new(flash: F) -> Self {
        Self { flash }
    }

    // ── Metadata I/O ────────────────────────────────────────────────

    fn read_meta(&mut self) -> Result<BootMeta, FlashError> {
        let mut raw = [0u8; BootMeta::SIZE];
        self.flash.read(Layout::META_ADDR, &mut raw)?;

        // SAFETY: BootMeta is repr(C, packed) with no invalid bit patterns
        let meta: BootMeta = unsafe { mem::transmute(raw) };

        if meta.magic != META_MAGIC {
            return Err(FlashError::BadMeta);
        }
        let computed = crc32(&raw[..BootMeta::CRC_FIELD_OFFSET]);
        if computed != meta.crc32 {
            return Err(FlashError::Crc);
        }
        Ok(meta)
    }

    fn write_meta(&mut self, meta: &mut BootMeta) -> Result<(), FlashError> {
        // SAFETY: as above
        let raw: [u8; BootMeta::SIZE] = unsafe { mem::transmute(*meta) };

        // Compute CRC over all fields before the crc32 field
        meta.crc32 = crc32(&raw[..BootMeta::CRC_FIELD_OFFSET]);
        let raw_final: [u8; BootMeta::SIZE] = unsafe { mem::transmute(*meta) };

        self.flash.erase_sector(Layout::META_ADDR)?;

        // Write in page-sized chunks (metadata fits in one page)
        self.flash.write_page(Layout::META_ADDR, &raw_final)
    }

    // ── Image Verification ───────────────────────────────────────────

    /// Verify the CRC of an image in a bank without loading it entirely into RAM.
    /// Uses a 256-byte rolling buffer.
    pub fn verify_bank(&mut self, bank: BankId) -> Result<ImageHeader, FlashError> {
        let base = Layout::bank_addr(bank);
        let mut hdr_raw = [0u8; ImageHeader::SIZE];
        self.flash.read(base, &mut hdr_raw)?;

        let hdr: ImageHeader = unsafe { mem::transmute(hdr_raw) };

        if hdr.magic != IMG_MAGIC {
            return Err(FlashError::BadMagic);
        }
        let max_body = Layout::BANK_SIZE as usize - ImageHeader::SIZE;
        if hdr.image_size as usize > max_body {
            return Err(FlashError::BadSize);
        }

        // Stream-verify CRC
        let mut buf = [0u8; 256];
        let mut remaining = hdr.image_size as usize;
        let mut addr = base + ImageHeader::SIZE as u32;
        let mut running: u32 = 0xFFFF_FFFF;

        while remaining > 0 {
            let chunk = remaining.min(buf.len());
            self.flash.read(addr, &mut buf[..chunk])?;

            for &byte in &buf[..chunk] {
                running ^= byte as u32;
                for _ in 0..8 {
                    let mask = (running & 1).wrapping_neg();
                    running = (running >> 1) ^ (0xEDB8_8320 & mask);
                }
            }
            addr += chunk as u32;
            remaining -= chunk;
        }

        if !running != hdr.crc32 {
            return Err(FlashError::Crc);
        }
        Ok(hdr)
    }

    // ── Update Sequence ──────────────────────────────────────────────

    /// Erase the target bank in preparation for writing a new image.
    pub fn begin_update(&mut self, target: BankId) -> Result<(), FlashError> {
        let base    = Layout::bank_addr(target);
        let sectors = Layout::BANK_SIZE / Layout::SECTOR_SIZE;

        for s in 0..sectors {
            self.flash.erase_sector(base + s * Layout::SECTOR_SIZE)?;
        }
        Ok(())
    }

    /// Write a chunk of firmware body data at `offset` bytes from firmware start.
    pub fn write_chunk(
        &mut self,
        bank:   BankId,
        offset: u32,
        data:   &[u8],
    ) -> Result<(), FlashError> {
        let base    = Layout::bank_addr(bank) + ImageHeader::SIZE as u32;
        let mut pos = 0usize;

        while pos < data.len() {
            let addr      = base + offset + pos as u32;
            let page_off  = (addr % Layout::PAGE_SIZE) as usize;
            let chunk     = (Layout::PAGE_SIZE as usize - page_off)
                             .min(data.len() - pos);
            self.flash.write_page(addr, &data[pos..pos + chunk])?;
            pos += chunk;
        }
        Ok(())
    }

    /// Write the image header, verify, then set the pending flag in metadata.
    pub fn finalize_update(
        &mut self,
        bank:       BankId,
        version:    u32,
        image_size: u32,
        image_crc:  u32,
    ) -> Result<(), FlashError> {
        let mut hdr = ImageHeader {
            magic:      IMG_MAGIC,
            version,
            image_size,
            crc32:      image_crc,
            reserved:   [0xFF; 48],
        };
        let hdr_bytes: [u8; ImageHeader::SIZE] = unsafe { mem::transmute(hdr) };
        self.flash.write_page(Layout::bank_addr(bank), &hdr_bytes)?;

        // Verify the complete bank before marking as pending
        self.verify_bank(bank)?;

        // Mark pending in metadata
        let mut meta = self.read_meta().unwrap_or_else(|_| {
            let mut m = BootMeta::new_default();
            m.magic = META_MAGIC;
            m
        });
        meta.pending_bank = bank as u8;
        meta.boot_count   = 0;
        meta.confirmed    = 0;
        meta.sequence     = meta.sequence.wrapping_add(1);
        self.write_meta(&mut meta)
    }

    // ── Boot Operations ──────────────────────────────────────────────

    /// Called by the bootloader: decides which bank to boot and updates metadata.
    /// Returns the (bank_addr, image_size) to jump to.
    pub fn bootloader_select(&mut self) -> Result<(u32, u32), FlashError> {
        let mut meta = self.read_meta().unwrap_or_else(|_| BootMeta::new_default());

        // Handle pending update
        if meta.pending_bank != 0xFF {
            let new_bank = if meta.pending_bank == BankId::A as u8 {
                BankId::A
            } else {
                BankId::B
            };

            if self.verify_bank(new_bank).is_ok() {
                meta.active_bank  = new_bank as u8;
                meta.pending_bank = 0xFF;
                meta.boot_count   = 0;
                meta.confirmed    = 0;
            } else {
                meta.pending_bank = 0xFF; // discard corrupt update
            }
            self.write_meta(&mut meta)?;
        }

        // Rollback if too many unconfirmed boots
        if meta.confirmed == 0 && meta.boot_count >= MAX_BOOT_TRIES {
            let fallback = if meta.active_bank == BankId::A as u8 {
                BankId::B
            } else {
                BankId::A
            };
            meta.active_bank  = fallback as u8;
            meta.boot_count   = 0;
            meta.confirmed    = 1;
            self.write_meta(&mut meta)?;
        }

        // Increment boot counter for unconfirmed boots
        if meta.confirmed == 0 {
            meta.boot_count += 1;
            self.write_meta(&mut meta)?;
        }

        // Read active image header
        let active = if meta.active_bank == BankId::A as u8 {
            BankId::A
        } else {
            BankId::B
        };
        let hdr = self.verify_bank(active)?;
        let exec_addr = Layout::bank_addr(active) + ImageHeader::SIZE as u32;
        Ok((exec_addr, hdr.image_size))
    }

    /// Called by the application after successful startup to confirm the boot.
    pub fn confirm_boot(&mut self) -> Result<(), FlashError> {
        let mut meta = self.read_meta()?;
        meta.confirmed  = 1;
        meta.boot_count = 0;
        self.write_meta(&mut meta)
    }

    /// Force immediate rollback to the other bank.
    pub fn rollback(&mut self) -> Result<(), FlashError> {
        let mut meta = self.read_meta()?;
        meta.active_bank  = meta.active_bank ^ 1; // toggle 0 ↔ 1
        meta.pending_bank = 0xFF;
        meta.confirmed    = 1;
        meta.boot_count   = 0;
        self.write_meta(&mut meta)
    }
}
```

### Application Usage (Rust)

```rust
// src/main.rs  (application firmware side)
#![no_std]
#![no_main]

use dual_bank_flash::manager::DualBankManager;
// MySpiFlash is your MCU-specific impl of the SpiFlash trait
use bsp::MySpiFlash;

#[no_mangle]
pub extern "C" fn main() -> ! {
    let spi = MySpiFlash::new();
    let mut mgr = DualBankManager::new(spi);

    // After boot: confirm we're running correctly
    // (in real code, run self-tests first)
    mgr.confirm_boot().expect("confirm failed");

    // ── Receiving an OTA update ──────────────────────────────────────
    // Determine which bank to write into (the inactive one)
    // For simplicity, we toggle here; production code reads metadata.
    let update_bank = dual_bank_flash::meta::BankId::B;

    // 1. Erase update bank
    mgr.begin_update(update_bank).expect("erase failed");

    // 2. Write firmware chunks as they arrive (e.g., over UART/network)
    let firmware_chunk: &[u8] = &[/* ... received bytes ... */];
    mgr.write_chunk(update_bank, 0, firmware_chunk).expect("write failed");

    // 3. Finalize: write header, verify, set pending flag
    let fw_size: u32 = 0x1_0000;
    let fw_crc:  u32 = 0xDEAD_1234; // pre-computed from server
    mgr.finalize_update(update_bank, 0x0100, fw_size, fw_crc)
       .expect("finalize failed");

    // 4. Reboot — bootloader will switch to the new bank
    bsp::system_reset();

    loop {}
}
```

---

## Integrity Verification

### Why CRC alone may not be enough

For security-sensitive products, a CRC32 catches accidental corruption but cannot prevent a malicious actor from substituting a valid-CRC image. Consider layering:

| Method         | Protection                         | Cost                       |
|----------------|------------------------------------|----------------------------|
| CRC32          | Accidental bit flips               | ~4 bytes, very fast        |
| SHA-256        | Collision resistance               | 32 bytes, moderate CPU     |
| HMAC-SHA-256   | Authentication (shared key)        | 32 bytes + key storage     |
| Ed25519 / ECDSA| Full asymmetric signature          | 64 bytes sig + public key  |

For most non-safety-critical IoT, CRC32 is sufficient. For over-the-air medical or industrial devices, use a cryptographic signature.

### Version Anti-Rollback

The `sequence` field in `BootMeta` (or a dedicated monotonic counter stored in eFuses or OTP flash) prevents downgrading to an older, potentially vulnerable firmware:

```c
/* In bootloader, after verifying new image: */
if (new_hdr.version < meta.sequence) {
    /* Reject: older image, possible downgrade attack */
    meta.pending_bank = 0xFF;
    flash_write_meta(&meta);
    boot_active_bank();
}
```

---

## Update State Machine

```
         ┌──────────────────────────────────────┐
         │           IDLE (confirmed)            │
         │  active = A, confirmed = 1            │
         └───────────┬──────────────────────────┘
                     │  OTA update arrives
                     ▼
         ┌──────────────────────────────────────┐
         │           ERASING Bank B             │
         └───────────┬──────────────────────────┘
                     │  erase complete
                     ▼
         ┌──────────────────────────────────────┐
         │           WRITING Bank B             │
         │  (chunk by chunk)                    │
         └───────────┬──────────────────────────┘
                     │  all chunks written
                     ▼
         ┌──────────────────────────────────────┐
         │         FINALIZING                   │
         │  write header, verify CRC            │
         └─────┬─────────────────┬──────────────┘
               │ CRC OK          │ CRC FAIL
               ▼                 ▼
    ┌───────────────────┐   ┌─────────────────────┐
    │  PENDING REBOOT   │   │  ABORTED (stay A)   │
    │  pending = B      │   │  image discarded    │
    └────────┬──────────┘   └─────────────────────┘
             │  system resets
             ▼
    ┌───────────────────────────────────────────────┐
    │  BOOTLOADER: verify B → switch active = B     │
    │  confirmed = 0, boot_count = 1                │
    └────────────────┬──────────────────────────────┘
                     │  app starts
                     ▼
    ┌──────────────────────────────────────────┐
    │  APP RUNNING on Bank B                   │
    │  (self-tests, watchdog window)           │
    └────────┬──────────────┬──────────────────┘
             │ confirm_boot()│ watchdog fires / resets
             ▼              │ (3× without confirm)
    ┌──────────────────┐    ▼
    │  CONFIRMED on B  │  ROLLBACK → back to A
    └──────────────────┘
```

---

## Watchdog Integration

A hardware watchdog is the last line of defense. If the new firmware boots but hangs before calling `confirm_boot()`, the watchdog fires, causing a reset. After `MAX_BOOT_ATTEMPTS` such resets, the bootloader rolls back.

```c
/* In application startup (C) */
void app_startup_sequence(void)
{
    watchdog_enable(WATCHDOG_TIMEOUT_MS);   /* Start watchdog ASAP     */

    /* Run self-tests — watchdog keeps ticking */
    if (!run_self_tests()) {
        /* Intentionally let watchdog fire → bootloader will rollback */
        while (1) {}
    }

    /* All good: confirm the boot and then kick the watchdog regularly */
    flash_confirm_boot();
    watchdog_kick();

    main_application_loop();   /* Must kick watchdog periodically     */
}
```

```rust
// In Rust application startup
fn startup(mut mgr: DualBankManager<impl SpiFlash>, mut wdg: impl Watchdog) -> ! {
    wdg.enable();

    if !run_self_tests() {
        loop {}   // Let watchdog fire → bootloader rollback
    }

    mgr.confirm_boot().unwrap();
    wdg.feed();

    application_loop(mgr, wdg)
}
```

**Important rules for watchdog + dual-bank:**

- Enable the watchdog **before** any slow initialization (flash reads, peripheral setup) to catch hangs.
- Feed the watchdog throughout the normal application loop.
- Never feed the watchdog inside error handlers — let it fire.
- The bootloader itself should have its own watchdog timeout in case flash reads hang.

---

## Summary

Dual-Bank SPI Flash firmware updates provide **atomic, fail-safe firmware replacement** for embedded systems by maintaining two complete firmware images in SPI flash. The key ideas are:

**Architecture** — Two equally-sized banks (A and B) hold independent firmware images. A small metadata sector tracks which bank is active, whether an update is pending, and how many times the device has booted from an unconfirmed image.

**Update Process** — New firmware is written into the inactive bank sector-by-sector (because flash requires erase-before-write). A CRC-verified image header is written last. The bootloader is instructed to switch banks on next boot only after the complete image passes verification.

**Fail-Safety** — A hardware watchdog + boot counter catches the case where new firmware boots but fails (crashes, hangs, fails self-tests) before calling `confirm_boot()`. After a configurable number of failed attempts, the bootloader automatically rolls back to the last known-good bank.

**C/C++ Implementation** highlights: a packed `boot_meta_t` struct with CRC in a dedicated sector; sector-by-sector erase before any write; streaming CRC verification to avoid loading multi-megabyte images into limited SRAM; and a simple bootloader that is itself never overwritten.

**Rust Implementation** highlights: a `SpiFlash` trait for HAL abstraction; `unsafe` transmutes into `repr(C, packed)` structs isolated in a thin layer; iterators for streaming CRC with no heap allocation; and a `DualBankManager<F>` generic over any SPI flash implementation.

**Security Extensions** — For production, supplement CRC32 with cryptographic image signing (ECDSA/Ed25519) and a monotonic sequence counter in OTP/eFuses to prevent downgrade attacks.

The pattern requires minimal hardware beyond the SPI flash itself — just a protected metadata region and a watchdog timer — making it applicable from tiny Cortex-M0 microcontrollers to larger application processors.

---

*Document: 83_Dual_Bank_SPI_Flash.md — Dual-Bank SPI Flash: Fail-Safe Firmware Updates*