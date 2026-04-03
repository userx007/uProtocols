# 81. SPI Flash Bootloader

- **SPI Flash Fundamentals** — command opcodes, timing constraints, and electrical wiring
- **Bootloader Architecture** — layered design and complete boot sequence flowchart
- **Flash Memory Layout** — a concrete 4 MB address map with dual banks and OTA slot
- **C/C++ Implementation** — a complete, production-style SPI flash driver (`spi_flash.h/.c`), firmware header struct with CRC validation, and the full bootloader main logic with bank selection and jump-to-application
- **Rust Implementation** — `Cargo.toml`, a generic `SpiFlash<SPI>` driver using `embedded-hal 1.0` traits, a matching `FirmwareHeader` (with `repr(C, packed)` for C ABI compatibility), CRC streaming validation using the `crc` crate, and a `#![no_std]` `#[entry]` main
- **Security** — progression from CRC32 → SHA-256 → ECDSA secure boot chain
- **Dual-Bank OTA** — complete write-and-activate flow with rollback state machine
- **Debugging** — common failure modes table, watchdog integration, and safe timeout patterns
- **Summary** — concise recap of all key design decisions

## Implementing Bootloaders That Load Firmware from SPI Flash Memory

---

## Table of Contents

1. [Introduction](#introduction)
2. [SPI Flash Fundamentals](#spi-flash-fundamentals)
3. [Bootloader Architecture](#bootloader-architecture)
4. [SPI Hardware Interface](#spi-hardware-interface)
5. [Flash Memory Layout](#flash-memory-layout)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Firmware Integrity and Security](#firmware-integrity-and-security)
9. [Dual-Bank and OTA Update Support](#dual-bank-and-ota-update-support)
10. [Debugging and Error Handling](#debugging-and-error-handling)
11. [Summary](#summary)

---

## Introduction

A **SPI Flash Bootloader** is a small, trusted piece of software that executes first on a microcontroller or embedded processor. Its primary responsibility is to locate, validate, and load application firmware stored in an external (or internal) SPI NOR Flash memory chip before transferring execution control to that firmware.

SPI Flash bootloaders are common in:

- IoT devices that require field-updatable firmware
- Industrial controllers with large firmware images that exceed internal flash capacity
- Systems requiring A/B (dual-bank) firmware update schemes for fail-safe OTA (Over-The-Air) updates
- Products where the application firmware is encrypted or signed for security

### Key Responsibilities of a SPI Flash Bootloader

- Initialize the SPI peripheral and the flash chip
- Read firmware metadata (headers, version, size, CRC/hash)
- Validate firmware integrity (CRC32, SHA-256, digital signature)
- Copy firmware from SPI Flash to internal SRAM or execute-in-place (XIP) region
- Jump to the application entry point
- Handle update and rollback scenarios

---

## SPI Flash Fundamentals

SPI NOR Flash chips (e.g., Winbond W25Qxx, Macronix MX25L, ISSI IS25LP) communicate via the Serial Peripheral Interface (SPI) and expose a standard set of commands:

| Command | Opcode | Description |
|---|---|---|
| Read Data | `0x03` | Sequential read from address |
| Fast Read | `0x0B` | High-speed read with dummy byte |
| Page Program | `0x02` | Write up to 256 bytes |
| Sector Erase | `0x20` | Erase 4 KB sector |
| Block Erase (32K) | `0x52` | Erase 32 KB block |
| Block Erase (64K) | `0xD8` | Erase 64 KB block |
| Chip Erase | `0xC7` | Erase entire chip |
| Write Enable | `0x06` | Must precede any write |
| Read Status Reg | `0x05` | Check BUSY and WEL bits |
| Read JEDEC ID | `0x9F` | Identify manufacturer and capacity |
| Power Down | `0xB9` | Low power mode |
| Release Power Down | `0xAB` | Wake from power down |

### Typical SPI Flash Timing Constraints

- **Page Program time:** ~0.4–3 ms
- **Sector Erase (4 KB):** ~45–400 ms
- **Block Erase (64 KB):** ~0.5–2 s
- **Read speed:** Up to 133 MHz (Fast Read)

---

## Bootloader Architecture

A robust SPI Flash bootloader follows a layered architecture:

```
+-----------------------------+
|     Application Firmware    |  <- Loaded and jumped to
+-----------------------------+
|    Firmware Validation      |  <- CRC, hash, signature check
+-----------------------------+
|    Firmware Loader          |  <- Copy from SPI Flash to RAM/XIP
+-----------------------------+
|    Flash Driver (SPI HAL)   |  <- Chip-level read/write/erase
+-----------------------------+
|    SPI Peripheral Driver    |  <- Low-level SPI bit-banging or hardware SPI
+-----------------------------+
|    Hardware (MCU + Flash)   |
+-----------------------------+
```

### Boot Sequence Flow

```
Power On Reset
      │
      ▼
Initialize clocks, SPI, GPIO
      │
      ▼
Read Flash JEDEC ID → verify chip present
      │
      ▼
Read Firmware Header from Flash (address 0x000000)
      │
      ▼
Validate Header Magic Number
      │
      ├── INVALID ──► Enter DFU/Recovery Mode or halt
      │
      ▼
Check Firmware CRC32 / SHA-256
      │
      ├── FAIL ──► Try Bank B (OTA slot) or halt
      │
      ▼
Copy firmware image to SRAM (or use XIP)
      │
      ▼
Disable SPI, de-initialize peripherals
      │
      ▼
Set stack pointer, jump to App Reset Handler
```

---

## SPI Hardware Interface

### Electrical Connection (Typical)

```
MCU                    SPI NOR Flash
─────                  ─────────────
SPI_CLK  ────────────► CLK  (pin 6)
SPI_MOSI ────────────► DI   (pin 5)
SPI_MISO ◄────────────  DO   (pin 2)
GPIO_CS  ────────────► /CS  (pin 1)
3.3V     ────────────►  VCC  (pin 8)
GND      ────────────►  GND  (pin 4)
```

For Quad-SPI (QSPI) / execute-in-place configurations, additional IO2 and IO3 lines are used.

---

## Flash Memory Layout

A typical SPI Flash layout for a bootloader system:

```
SPI Flash Address Map (e.g., 4 MB W25Q32)
┌──────────────────────────────────┐  0x000000
│   Firmware Header (256 bytes)    │
├──────────────────────────────────┤  0x000100
│                                  │
│   Application Firmware           │
│   (Bank A - primary)             │
│                                  │
├──────────────────────────────────┤  0x100000  (1 MB)
│                                  │
│   OTA Firmware Slot              │
│   (Bank B - update pending)      │
│                                  │
├──────────────────────────────────┤  0x200000  (2 MB)
│   Boot Configuration / Flags     │
├──────────────────────────────────┤  0x201000
│   Factory / Fallback Image       │
├──────────────────────────────────┤  0x300000  (3 MB)
│   User Data / NVS                │
└──────────────────────────────────┘  0x400000  (4 MB)
```

---

## C/C++ Implementation

### 1. SPI Driver (Low-Level HAL)

```c
// spi_flash.h
#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdint.h>
#include <stdbool.h>

// SPI Flash command opcodes
#define FLASH_CMD_READ          0x03
#define FLASH_CMD_FAST_READ     0x0B
#define FLASH_CMD_PAGE_PROGRAM  0x02
#define FLASH_CMD_SECTOR_ERASE  0x20
#define FLASH_CMD_BLOCK_ERASE   0xD8
#define FLASH_CMD_CHIP_ERASE    0xC7
#define FLASH_CMD_WRITE_ENABLE  0x06
#define FLASH_CMD_READ_STATUS   0x05
#define FLASH_CMD_READ_JEDEC    0x9F
#define FLASH_CMD_POWER_DOWN    0xB9
#define FLASH_CMD_WAKEUP        0xAB

// Status register bits
#define FLASH_STATUS_BUSY       (1 << 0)
#define FLASH_STATUS_WEL        (1 << 1)

// Flash geometry (adjust for your chip)
#define FLASH_PAGE_SIZE         256
#define FLASH_SECTOR_SIZE       4096
#define FLASH_BLOCK_SIZE        65536
#define FLASH_TOTAL_SIZE        (4 * 1024 * 1024)  // 4 MB

// Flash layout addresses
#define FLASH_ADDR_HEADER       0x000000
#define FLASH_ADDR_APP_A        0x000100
#define FLASH_ADDR_APP_B        0x100000
#define FLASH_ADDR_BOOT_FLAGS   0x200000

typedef struct {
    uint32_t manufacturer_id;
    uint32_t device_id;
    uint32_t capacity_bytes;
} flash_jedec_id_t;

// HAL interface - implement these for your platform
void     spi_init(void);
void     spi_cs_assert(void);
void     spi_cs_deassert(void);
uint8_t  spi_transfer_byte(uint8_t data);

// Flash API
bool     flash_init(void);
bool     flash_read_jedec(flash_jedec_id_t *id);
bool     flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
bool     flash_write_enable(void);
bool     flash_wait_ready(uint32_t timeout_ms);
bool     flash_sector_erase(uint32_t addr);
bool     flash_page_program(uint32_t addr, const uint8_t *data, uint16_t len);
uint8_t  flash_read_status(void);

#endif // SPI_FLASH_H
```

```c
// spi_flash.c
#include "spi_flash.h"
#include "platform_hal.h"   // Your platform clock/delay functions
#include <string.h>

// -----------------------------------------------------------------------
// Low-level helpers
// -----------------------------------------------------------------------

static void flash_send_address(uint32_t addr)
{
    spi_transfer_byte((addr >> 16) & 0xFF);
    spi_transfer_byte((addr >>  8) & 0xFF);
    spi_transfer_byte((addr      ) & 0xFF);
}

uint8_t flash_read_status(void)
{
    uint8_t status;
    spi_cs_assert();
    spi_transfer_byte(FLASH_CMD_READ_STATUS);
    status = spi_transfer_byte(0xFF);
    spi_cs_deassert();
    return status;
}

bool flash_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = hal_get_tick_ms();
    while (flash_read_status() & FLASH_STATUS_BUSY) {
        if ((hal_get_tick_ms() - start) > timeout_ms) {
            return false;  // Timeout
        }
        hal_delay_us(100);
    }
    return true;
}

bool flash_write_enable(void)
{
    spi_cs_assert();
    spi_transfer_byte(FLASH_CMD_WRITE_ENABLE);
    spi_cs_deassert();

    // Verify WEL bit set
    return (flash_read_status() & FLASH_STATUS_WEL) != 0;
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

bool flash_init(void)
{
    spi_init();

    // Wake from power-down if necessary
    spi_cs_assert();
    spi_transfer_byte(FLASH_CMD_WAKEUP);
    spi_cs_deassert();
    hal_delay_us(30);  // tRES1: max 30 µs release time

    // Verify chip is responding
    flash_jedec_id_t id;
    return flash_read_jedec(&id);
}

bool flash_read_jedec(flash_jedec_id_t *id)
{
    spi_cs_assert();
    spi_transfer_byte(FLASH_CMD_READ_JEDEC);
    uint8_t mfr  = spi_transfer_byte(0xFF);
    uint8_t type = spi_transfer_byte(0xFF);
    uint8_t cap  = spi_transfer_byte(0xFF);
    spi_cs_deassert();

    if (mfr == 0xFF || mfr == 0x00) {
        return false;  // No chip detected
    }

    id->manufacturer_id = mfr;
    id->device_id       = (type << 8) | cap;
    id->capacity_bytes  = (cap >= 0x10) ? (1UL << cap) : 0;
    return true;
}

bool flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0) return false;

    spi_cs_assert();
    spi_transfer_byte(FLASH_CMD_FAST_READ);
    flash_send_address(addr);
    spi_transfer_byte(0x00);  // 1 dummy byte for Fast Read

    for (uint32_t i = 0; i < len; i++) {
        buf[i] = spi_transfer_byte(0xFF);
    }
    spi_cs_deassert();
    return true;
}

bool flash_sector_erase(uint32_t addr)
{
    // Align to sector boundary
    addr &= ~(FLASH_SECTOR_SIZE - 1);

    if (!flash_write_enable()) return false;

    spi_cs_assert();
    spi_transfer_byte(FLASH_CMD_SECTOR_ERASE);
    flash_send_address(addr);
    spi_cs_deassert();

    return flash_wait_ready(500);  // Sector erase up to 400 ms
}

bool flash_page_program(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || len > FLASH_PAGE_SIZE) return false;

    // Must not cross page boundary
    uint32_t page_offset = addr % FLASH_PAGE_SIZE;
    if (page_offset + len > FLASH_PAGE_SIZE) return false;

    if (!flash_write_enable()) return false;

    spi_cs_assert();
    spi_transfer_byte(FLASH_CMD_PAGE_PROGRAM);
    flash_send_address(addr);
    for (uint16_t i = 0; i < len; i++) {
        spi_transfer_byte(data[i]);
    }
    spi_cs_deassert();

    return flash_wait_ready(10);  // Page program max ~3 ms
}
```

---

### 2. Firmware Header and CRC Validation

```c
// firmware_header.h
#ifndef FIRMWARE_HEADER_H
#define FIRMWARE_HEADER_H

#include <stdint.h>
#include <stdbool.h>

#define FIRMWARE_MAGIC      0x464C5348   // "FLSH"
#define FIRMWARE_VERSION    1

// Firmware header stored at the start of each bank in SPI Flash
// Total size: 64 bytes (padded to cache-line friendly size)
typedef struct __attribute__((packed)) {
    uint32_t magic;          // Must equal FIRMWARE_MAGIC
    uint16_t header_version; // Header format version
    uint16_t flags;          // Bit 0: encrypted, Bit 1: signed
    uint32_t fw_version;     // Semantic version: 0xMMmmpppp
    uint32_t fw_size;        // Firmware size in bytes (excl. header)
    uint32_t load_address;   // Target address in RAM / XIP region
    uint32_t entry_point;    // Offset from load_address to Reset_Handler
    uint32_t crc32;          // CRC32 of firmware image (excl. header)
    uint8_t  sha256[32];     // SHA-256 of firmware image (optional)
    // Pad to 64 bytes total
    uint8_t  _reserved[4];
} firmware_header_t;

// Validate header and firmware integrity
bool firmware_header_validate(const firmware_header_t *hdr);
bool firmware_check_crc(uint32_t flash_addr, uint32_t size, uint32_t expected_crc);
uint32_t crc32_compute(const uint8_t *data, uint32_t len, uint32_t init);

#endif // FIRMWARE_HEADER_H
```

```c
// firmware_header.c
#include "firmware_header.h"
#include "spi_flash.h"
#include <string.h>

// Standard CRC32 (IEEE 802.3 polynomial: 0xEDB88320 reflected)
static const uint32_t crc32_table[256] = {
    // Pre-computed table entries (first 8 shown; generate full table at build time)
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    // ... (256 entries total - use a generator or lookup table header)
};

uint32_t crc32_compute(const uint8_t *data, uint32_t len, uint32_t init)
{
    uint32_t crc = init ^ 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

bool firmware_header_validate(const firmware_header_t *hdr)
{
    if (!hdr) return false;

    if (hdr->magic != FIRMWARE_MAGIC) {
        return false;
    }
    if (hdr->header_version != FIRMWARE_VERSION) {
        return false;
    }
    if (hdr->fw_size == 0 || hdr->fw_size > FLASH_TOTAL_SIZE) {
        return false;
    }
    if (hdr->load_address == 0 || hdr->entry_point >= hdr->fw_size) {
        return false;
    }
    return true;
}

// Stream CRC check directly from SPI Flash (avoids full RAM buffer)
bool firmware_check_crc(uint32_t flash_addr, uint32_t size, uint32_t expected_crc)
{
    uint8_t  chunk[256];
    uint32_t crc        = 0xFFFFFFFF;
    uint32_t remaining  = size;
    uint32_t offset     = 0;

    while (remaining > 0) {
        uint32_t to_read = (remaining > sizeof(chunk)) ? sizeof(chunk) : remaining;

        if (!flash_read(flash_addr + offset, chunk, to_read)) {
            return false;
        }

        for (uint32_t i = 0; i < to_read; i++) {
            crc = crc32_table[(crc ^ chunk[i]) & 0xFF] ^ (crc >> 8);
        }

        offset    += to_read;
        remaining -= to_read;
    }

    crc ^= 0xFFFFFFFF;
    return (crc == expected_crc);
}
```

---

### 3. Bootloader Main Logic

```c
// bootloader.c
#include "spi_flash.h"
#include "firmware_header.h"
#include "platform_hal.h"
#include <string.h>

// Boot flags stored in flash (non-volatile boot control)
#define BOOT_FLAG_MAGIC     0xB007B007
#define BOOT_FLAG_USE_BANK_B  (1 << 0)
#define BOOT_FLAG_OTA_PENDING (1 << 1)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t flags;
    uint32_t boot_count;     // Number of boot attempts for current image
    uint32_t reserved;
} boot_flags_t;

// Maximum failed boots before reverting to Bank A
#define MAX_BOOT_ATTEMPTS   3

// -----------------------------------------------------------------------
// Platform-specific: jump to application
// -----------------------------------------------------------------------

typedef void (*app_reset_handler_t)(void);

static void jump_to_application(uint32_t load_address, uint32_t entry_offset)
{
    // Disable interrupts before jumping
    hal_disable_irq();

    // Set up the application's vector table (if using VTOR remapping)
    // On ARM Cortex-M: SCB->VTOR = load_address;
    hal_set_vtor(load_address);

    // Set up the new stack pointer from the application's vector table
    uint32_t *app_vectors   = (uint32_t *)load_address;
    uint32_t  new_sp        = app_vectors[0];
    app_reset_handler_t app_entry = (app_reset_handler_t)(app_vectors[1]);
    // Alternatively, use entry_offset:
    // app_reset_handler_t app_entry =
    //     (app_reset_handler_t)(load_address + entry_offset + 1);  // +1 for Thumb

    // Set main stack pointer
    hal_set_msp(new_sp);

    // Jump!
    app_entry();

    // Should never reach here
    while (1) {}
}

// -----------------------------------------------------------------------
// Load firmware from SPI Flash to RAM
// -----------------------------------------------------------------------

static bool load_firmware_to_ram(uint32_t flash_addr, const firmware_header_t *hdr)
{
    uint8_t  *dest      = (uint8_t *)hdr->load_address;
    uint32_t  remaining = hdr->fw_size;
    uint32_t  offset    = 0;

    while (remaining > 0) {
        uint32_t chunk = (remaining > 512) ? 512 : remaining;

        if (!flash_read(flash_addr + sizeof(firmware_header_t) + offset,
                        dest + offset, chunk)) {
            return false;
        }

        offset    += chunk;
        remaining -= chunk;
    }
    return true;
}

// -----------------------------------------------------------------------
// Bootloader entry point
// -----------------------------------------------------------------------

void bootloader_main(void)
{
    // 1. Initialize hardware
    hal_system_init();
    hal_uart_init(115200);
    hal_log("SPI Flash Bootloader v1.0\r\n");

    // 2. Initialize SPI Flash
    if (!flash_init()) {
        hal_log("ERROR: SPI Flash init failed!\r\n");
        hal_reset();  // Or enter recovery mode
    }

    flash_jedec_id_t jedec;
    flash_read_jedec(&jedec);
    hal_log_fmt("Flash: MFR=0x%02X ID=0x%04X CAP=%u bytes\r\n",
                jedec.manufacturer_id, jedec.device_id, jedec.capacity_bytes);

    // 3. Read boot flags
    boot_flags_t boot_flags;
    flash_read(FLASH_ADDR_BOOT_FLAGS, (uint8_t *)&boot_flags, sizeof(boot_flags));

    bool use_bank_b = false;
    if (boot_flags.magic == BOOT_FLAG_MAGIC) {
        if ((boot_flags.flags & BOOT_FLAG_USE_BANK_B) &&
            (boot_flags.boot_count < MAX_BOOT_ATTEMPTS)) {
            use_bank_b = true;
            hal_log("Booting from Bank B (OTA image)\r\n");
        }
    }

    uint32_t fw_flash_addr = use_bank_b ? FLASH_ADDR_APP_B : FLASH_ADDR_APP_A;

    // 4. Read and validate firmware header
    firmware_header_t hdr;
    if (!flash_read(fw_flash_addr, (uint8_t *)&hdr, sizeof(hdr))) {
        hal_log("ERROR: Failed to read firmware header!\r\n");
        hal_halt();
    }

    if (!firmware_header_validate(&hdr)) {
        hal_log("ERROR: Invalid firmware header!\r\n");
        if (use_bank_b) {
            // Fall back to Bank A
            hal_log("Falling back to Bank A...\r\n");
            fw_flash_addr = FLASH_ADDR_APP_A;
            flash_read(fw_flash_addr, (uint8_t *)&hdr, sizeof(hdr));
            if (!firmware_header_validate(&hdr)) {
                hal_log("ERROR: Bank A also invalid! Halting.\r\n");
                hal_halt();
            }
        } else {
            hal_halt();
        }
    }

    hal_log_fmt("Firmware v%u.%u.%u size=%u bytes\r\n",
                (hdr.fw_version >> 24) & 0xFF,
                (hdr.fw_version >> 16) & 0xFF,
                 hdr.fw_version & 0xFFFF,
                 hdr.fw_size);

    // 5. Verify CRC32
    hal_log("Verifying CRC32...\r\n");
    if (!firmware_check_crc(fw_flash_addr + sizeof(firmware_header_t),
                             hdr.fw_size, hdr.crc32)) {
        hal_log("ERROR: CRC32 mismatch!\r\n");
        hal_halt();
    }
    hal_log("CRC32 OK\r\n");

    // 6. Load firmware into RAM
    hal_log("Loading firmware...\r\n");
    if (!load_firmware_to_ram(fw_flash_addr, &hdr)) {
        hal_log("ERROR: Firmware load failed!\r\n");
        hal_halt();
    }
    hal_log("Firmware loaded. Jumping to application...\r\n");

    // 7. Update boot attempt counter
    if (use_bank_b && boot_flags.magic == BOOT_FLAG_MAGIC) {
        boot_flags.boot_count++;
        // Write back - in a real system, use a dedicated NV storage write
        // (simplified here for clarity)
    }

    // 8. Jump to application
    jump_to_application(hdr.load_address, hdr.entry_point);

    // Never reached
    hal_halt();
}
```

---

## Rust Implementation

Rust's ownership model and embedded ecosystem (`embedded-hal`, `embassy`, `cortex-m`) make it well-suited for safe bootloader development.

### Cargo.toml Dependencies

```toml
[package]
name = "spi-flash-bootloader"
version = "0.1.0"
edition = "2021"

[dependencies]
cortex-m        = { version = "0.7", features = ["critical-section-single-core"] }
cortex-m-rt     = "0.7"
embedded-hal    = "1.0"
nb              = "1.1"
crc             = "3.0"
defmt           = "0.3"
defmt-rtt       = "0.4"
panic-halt      = "0.2"

# For STM32 HAL (substitute your target HAL)
# stm32f4xx-hal = { version = "0.20", features = ["stm32f411"] }

[profile.release]
opt-level     = "z"   # Optimize for size
lto           = true
codegen-units = 1
panic         = "abort"
debug         = false
```

---

### SPI Flash Driver in Rust

```rust
// src/flash.rs
#![allow(dead_code)]

use embedded_hal::spi::SpiDevice;
use defmt::{debug, error};

// Flash command opcodes
const CMD_READ:         u8 = 0x03;
const CMD_FAST_READ:    u8 = 0x0B;
const CMD_PAGE_PROGRAM: u8 = 0x02;
const CMD_SECTOR_ERASE: u8 = 0x20;
const CMD_WRITE_ENABLE: u8 = 0x06;
const CMD_READ_STATUS:  u8 = 0x05;
const CMD_READ_JEDEC:   u8 = 0x9F;
const CMD_POWER_DOWN:   u8 = 0xB9;
const CMD_WAKEUP:       u8 = 0xAB;

const STATUS_BUSY: u8 = 1 << 0;
const STATUS_WEL:  u8 = 1 << 1;

pub const PAGE_SIZE:   usize = 256;
pub const SECTOR_SIZE: usize = 4096;
pub const FLASH_SIZE:  usize = 4 * 1024 * 1024;

/// Flash memory layout
pub mod layout {
    pub const ADDR_HEADER:     u32 = 0x000000;
    pub const ADDR_APP_A:      u32 = 0x000100;
    pub const ADDR_APP_B:      u32 = 0x100000;
    pub const ADDR_BOOT_FLAGS: u32 = 0x200000;
}

#[derive(Debug, defmt::Format)]
pub struct JedecId {
    pub manufacturer: u8,
    pub device_type:  u8,
    pub capacity:     u8,
}

impl JedecId {
    pub fn capacity_bytes(&self) -> u32 {
        if self.capacity >= 0x10 {
            1u32 << self.capacity
        } else {
            0
        }
    }
}

#[derive(Debug, defmt::Format)]
pub enum FlashError {
    SpiError,
    Timeout,
    InvalidChip,
    WriteEnableFailed,
    AddressOutOfRange,
    LengthExceedsPage,
}

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

    /// Consume the driver, returning the underlying SPI device.
    pub fn release(self) -> SPI {
        self.spi
    }

    fn send_address(addr: u32) -> [u8; 3] {
        [
            ((addr >> 16) & 0xFF) as u8,
            ((addr >>  8) & 0xFF) as u8,
            ( addr        & 0xFF) as u8,
        ]
    }

    pub fn wakeup(&mut self) -> Result<(), FlashError> {
        self.spi
            .write(&[CMD_WAKEUP])
            .map_err(|_| FlashError::SpiError)?;
        // tRES1: 30 µs - caller must delay
        Ok(())
    }

    pub fn read_status(&mut self) -> Result<u8, FlashError> {
        let mut buf = [0u8; 1];
        self.spi
            .transfer(&mut buf, &[CMD_READ_STATUS])
            .map_err(|_| FlashError::SpiError)?;
        Ok(buf[0])
    }

    pub fn wait_ready(&mut self, max_retries: u32) -> Result<(), FlashError> {
        for _ in 0..max_retries {
            let status = self.read_status()?;
            if (status & STATUS_BUSY) == 0 {
                return Ok(());
            }
            // In a real system, yield or sleep here
            cortex_m::asm::nop();
        }
        Err(FlashError::Timeout)
    }

    pub fn read_jedec(&mut self) -> Result<JedecId, FlashError> {
        let mut buf = [0u8; 3];
        self.spi
            .transfer(&mut buf, &[CMD_READ_JEDEC])
            .map_err(|_| FlashError::SpiError)?;

        if buf[0] == 0xFF || buf[0] == 0x00 {
            return Err(FlashError::InvalidChip);
        }
        Ok(JedecId {
            manufacturer: buf[0],
            device_type:  buf[1],
            capacity:     buf[2],
        })
    }

    /// Read `len` bytes from `addr` into `buf`.
    pub fn read(&mut self, addr: u32, buf: &mut [u8]) -> Result<(), FlashError> {
        if addr as usize + buf.len() > FLASH_SIZE {
            return Err(FlashError::AddressOutOfRange);
        }

        let addr_bytes = Self::send_address(addr);
        // Fast Read: opcode + 3-byte address + 1 dummy byte
        let cmd = [CMD_FAST_READ, addr_bytes[0], addr_bytes[1], addr_bytes[2], 0x00];

        // Write command, then read data
        self.spi.write(&cmd).map_err(|_| FlashError::SpiError)?;
        self.spi.read(buf).map_err(|_| FlashError::SpiError)?;
        Ok(())
    }

    fn write_enable(&mut self) -> Result<(), FlashError> {
        self.spi
            .write(&[CMD_WRITE_ENABLE])
            .map_err(|_| FlashError::SpiError)?;
        let status = self.read_status()?;
        if (status & STATUS_WEL) == 0 {
            return Err(FlashError::WriteEnableFailed);
        }
        Ok(())
    }

    pub fn sector_erase(&mut self, addr: u32) -> Result<(), FlashError> {
        let addr = addr & !(SECTOR_SIZE as u32 - 1);  // Align to sector
        let addr_bytes = Self::send_address(addr);

        self.write_enable()?;
        self.spi
            .write(&[CMD_SECTOR_ERASE, addr_bytes[0], addr_bytes[1], addr_bytes[2]])
            .map_err(|_| FlashError::SpiError)?;
        self.wait_ready(50_000)  // ~400 ms / 10 µs per iteration
    }

    pub fn page_program(
        &mut self,
        addr: u32,
        data: &[u8],
    ) -> Result<(), FlashError> {
        if data.len() > PAGE_SIZE {
            return Err(FlashError::LengthExceedsPage);
        }
        let page_offset = (addr as usize) % PAGE_SIZE;
        if page_offset + data.len() > PAGE_SIZE {
            return Err(FlashError::LengthExceedsPage);
        }

        let addr_bytes = Self::send_address(addr);
        let cmd = [CMD_PAGE_PROGRAM, addr_bytes[0], addr_bytes[1], addr_bytes[2]];

        self.write_enable()?;
        self.spi.write(&cmd).map_err(|_| FlashError::SpiError)?;
        self.spi.write(data).map_err(|_| FlashError::SpiError)?;
        self.wait_ready(3_000)  // ~3 ms max
    }
}
```

---

### Firmware Header and Validation in Rust

```rust
// src/firmware.rs

use crc::{Crc, CRC_32_ISO_HDLC};

pub const CRC32: Crc<u32> = Crc::<u32>::new(&CRC_32_ISO_HDLC);

pub const FIRMWARE_MAGIC:   u32 = 0x464C5348;  // "FLSH"
pub const HEADER_VERSION:   u16 = 1;

/// Firmware header — must match layout in firmware_header.h (C side).
/// `repr(C, packed)` ensures identical memory layout.
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct FirmwareHeader {
    pub magic:          u32,
    pub header_version: u16,
    pub flags:          u16,
    pub fw_version:     u32,
    pub fw_size:        u32,
    pub load_address:   u32,
    pub entry_point:    u32,
    pub crc32:          u32,
    pub sha256:         [u8; 32],
    pub _reserved:      [u8; 4],
}

impl FirmwareHeader {
    /// Parse from a raw byte slice (exactly size_of::<FirmwareHeader>() bytes).
    pub fn from_bytes(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < core::mem::size_of::<FirmwareHeader>() {
            return None;
        }
        // SAFETY: FirmwareHeader is repr(C, packed) and we checked length.
        let hdr = unsafe {
            core::ptr::read_unaligned(bytes.as_ptr() as *const FirmwareHeader)
        };
        Some(hdr)
    }

    pub fn is_valid(&self) -> bool {
        self.magic == FIRMWARE_MAGIC
            && self.header_version == HEADER_VERSION
            && self.fw_size > 0
            && self.fw_size < (4 * 1024 * 1024)
            && self.load_address != 0
            && self.entry_point < self.fw_size
    }

    pub fn version_major(&self) -> u8 { ((self.fw_version >> 24) & 0xFF) as u8 }
    pub fn version_minor(&self) -> u8 { ((self.fw_version >> 16) & 0xFF) as u8 }
    pub fn version_patch(&self) -> u16 { (self.fw_version & 0xFFFF) as u16 }

    pub fn is_encrypted(&self) -> bool { (self.flags & 0x01) != 0 }
    pub fn is_signed(&self)    -> bool { (self.flags & 0x02) != 0 }
}

#[derive(Debug)]
pub enum ValidationError {
    InvalidHeader,
    CrcMismatch { expected: u32, computed: u32 },
    FlashReadError,
}

/// Verify CRC32 of the firmware image by streaming it in chunks.
/// `flash_read_fn` is a closure that reads from SPI Flash.
pub fn verify_firmware_crc<F>(
    header: &FirmwareHeader,
    flash_base: u32,
    mut flash_read_fn: F,
) -> Result<(), ValidationError>
where
    F: FnMut(u32, &mut [u8]) -> bool,
{
    let mut digest = CRC32.digest();
    let mut remaining = header.fw_size as usize;
    let mut offset: u32 = 0;
    let mut chunk = [0u8; 256];

    while remaining > 0 {
        let to_read = remaining.min(chunk.len());
        let read_addr = flash_base
            + core::mem::size_of::<FirmwareHeader>() as u32
            + offset;

        if !flash_read_fn(read_addr, &mut chunk[..to_read]) {
            return Err(ValidationError::FlashReadError);
        }

        digest.update(&chunk[..to_read]);
        offset    += to_read as u32;
        remaining -= to_read;
    }

    let computed = digest.finalize();
    if computed != header.crc32 {
        return Err(ValidationError::CrcMismatch {
            expected: header.crc32,
            computed,
        });
    }
    Ok(())
}
```

---

### Bootloader Main in Rust

```rust
// src/main.rs
#![no_std]
#![no_main]

use cortex_m::peripheral::SCB;
use cortex_m_rt::entry;
use defmt::*;
use defmt_rtt as _;
use panic_halt as _;

mod flash;
mod firmware;

use flash::{layout, SpiFlash};
use firmware::{verify_firmware_crc, FirmwareHeader, ValidationError};

// Replace with your platform's SPI initialization
fn init_spi() -> impl embedded_hal::spi::SpiDevice {
    // Platform-specific SPI setup omitted — return your SPI peripheral
    todo!("Initialize platform SPI peripheral")
}

fn delay_us(us: u32) {
    // Platform-specific delay
    for _ in 0..(us * 10) {
        cortex_m::asm::nop();
    }
}

/// Load firmware from SPI Flash into RAM.
fn load_firmware<SPI>(
    flash: &mut SpiFlash<SPI>,
    header: &FirmwareHeader,
    flash_base: u32,
) -> bool
where
    SPI: embedded_hal::spi::SpiDevice,
{
    let dest = header.load_address as *mut u8;
    let total = header.fw_size as usize;
    let hdr_size = core::mem::size_of::<FirmwareHeader>() as u32;
    let mut offset: u32 = 0;
    let mut remaining = total;

    while remaining > 0 {
        let chunk_size = remaining.min(512);
        // SAFETY: We validated load_address and fw_size in is_valid().
        let slice = unsafe {
            core::slice::from_raw_parts_mut(dest.add(offset as usize), chunk_size)
        };
        if flash.read(flash_base + hdr_size + offset, slice).is_err() {
            return false;
        }
        offset    += chunk_size as u32;
        remaining -= chunk_size;
    }
    true
}

/// Jump to the loaded application (ARM Cortex-M).
unsafe fn jump_to_app(load_address: u32) -> ! {
    // Update the vector table offset register
    let scb = &*cortex_m::peripheral::SCB::PTR;
    scb.vtor.write(load_address);

    // Read new SP and PC from app's vector table
    let vt = load_address as *const u32;
    let new_sp = vt.read_volatile();
    let reset_handler = vt.add(1).read_volatile();

    // Set stack pointer and jump
    cortex_m::asm::bootstrap(new_sp as *const u32, reset_handler as *const u32);
}

#[entry]
fn main() -> ! {
    info!("SPI Flash Bootloader (Rust) starting...");

    // 1. Initialize SPI Flash
    let spi = init_spi();
    let mut flash = SpiFlash::new(spi);

    flash.wakeup().expect("Flash wakeup failed");
    delay_us(30);

    let jedec = flash.read_jedec().expect("Failed to read JEDEC ID");
    info!(
        "Flash: MFR=0x{:02X} TYPE=0x{:02X} CAP=0x{:02X} ({} bytes)",
        jedec.manufacturer,
        jedec.device_type,
        jedec.capacity,
        jedec.capacity_bytes()
    );

    // 2. Determine boot bank (simplified; real system reads boot flags from flash)
    let fw_addr = layout::ADDR_APP_A;

    // 3. Read firmware header
    let mut hdr_bytes = [0u8; core::mem::size_of::<FirmwareHeader>()];
    flash
        .read(fw_addr, &mut hdr_bytes)
        .expect("Failed to read firmware header");

    let header = FirmwareHeader::from_bytes(&hdr_bytes)
        .expect("Header buffer too small");

    if !header.is_valid() {
        error!("Invalid firmware header! Halting.");
        loop { cortex_m::asm::bkpt(); }
    }

    info!(
        "Firmware v{}.{}.{}, size={} bytes, load_addr=0x{:08X}",
        header.version_major(),
        header.version_minor(),
        header.version_patch(),
        header.fw_size,
        header.load_address,
    );

    // 4. Verify CRC32
    info!("Verifying CRC32...");
    let result = verify_firmware_crc(&header, fw_addr, |addr, buf| {
        flash.read(addr, buf).is_ok()
    });

    match result {
        Ok(()) => info!("CRC32 OK"),
        Err(ValidationError::CrcMismatch { expected, computed }) => {
            error!("CRC mismatch: expected 0x{:08X}, got 0x{:08X}", expected, computed);
            loop { cortex_m::asm::bkpt(); }
        }
        Err(e) => {
            error!("Validation error: {:?}", e);
            loop { cortex_m::asm::bkpt(); }
        }
    }

    // 5. Load firmware into RAM
    info!("Loading firmware...");
    if !load_firmware(&mut flash, &header, fw_addr) {
        error!("Firmware load failed!");
        loop { cortex_m::asm::bkpt(); }
    }

    info!("Jumping to application at 0x{:08X}...", header.load_address);

    // 6. Jump to application
    unsafe {
        jump_to_app(header.load_address);
    }
}
```

---

## Firmware Integrity and Security

### CRC32 — Basic Integrity

CRC32 detects accidental corruption (bit flips, incomplete writes). It is **not** a security mechanism — an attacker can recalculate a valid CRC for modified firmware.

### SHA-256 — Cryptographic Hash

Including a SHA-256 in the header allows detection of tampering **when combined with a trusted root**. On its own, SHA-256 still doesn't prevent an attacker from replacing the entire image (hash included).

### ECDSA / RSA Signature Verification

For secure boot, the bootloader verifies an ECDSA P-256 or RSA-2048 signature over the firmware image using a **public key burned into the MCU's OTP (One-Time Programmable) memory** or trusted storage.

```c
// Simplified signature verification concept (using a TinyCrypt or mbedTLS call)
bool verify_firmware_signature(const firmware_header_t *hdr,
                                uint32_t flash_addr,
                                const uint8_t *public_key)
{
    uint8_t digest[32];
    sha256_compute_from_flash(flash_addr + sizeof(*hdr), hdr->fw_size, digest);
    return ecdsa_p256_verify(digest, hdr->signature, public_key);
}
```

### Secure Boot Chain

```
OTP Public Key (burned at factory)
         │
         ▼
Bootloader verifies App Signature
         │
         ▼
App verifies OTA package signature
         │
         ▼
Write new firmware to Bank B
         │
         ▼
Set OTA pending flag → reboot
         │
         ▼
Bootloader validates Bank B → boot from B
```

---

## Dual-Bank and OTA Update Support

A robust OTA (Over-The-Air) update flow uses two firmware banks in SPI Flash:

```c
// ota.c — writing a new firmware image to Bank B

#include "spi_flash.h"
#include "firmware_header.h"

#define OTA_CHUNK_SIZE  256

typedef enum {
    OTA_OK = 0,
    OTA_ERR_ERASE,
    OTA_ERR_WRITE,
    OTA_ERR_CRC,
} ota_result_t;

ota_result_t ota_write_image(const uint8_t *image, uint32_t image_size)
{
    // 1. Erase Bank B sectors
    uint32_t sectors = (image_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    for (uint32_t i = 0; i < sectors; i++) {
        if (!flash_sector_erase(FLASH_ADDR_APP_B + i * FLASH_SECTOR_SIZE)) {
            return OTA_ERR_ERASE;
        }
    }

    // 2. Write image in pages
    uint32_t written = 0;
    while (written < image_size) {
        uint32_t chunk = ((image_size - written) > OTA_CHUNK_SIZE)
                         ? OTA_CHUNK_SIZE : (image_size - written);
        if (!flash_page_program(FLASH_ADDR_APP_B + written,
                                image + written, chunk)) {
            return OTA_ERR_WRITE;
        }
        written += chunk;
    }

    // 3. Verify written data CRC
    const firmware_header_t *hdr = (const firmware_header_t *)image;
    if (!firmware_check_crc(FLASH_ADDR_APP_B + sizeof(firmware_header_t),
                             hdr->fw_size, hdr->crc32)) {
        return OTA_ERR_CRC;
    }

    // 4. Set boot flags to use Bank B on next reset
    boot_flags_t flags = {
        .magic       = BOOT_FLAG_MAGIC,
        .flags       = BOOT_FLAG_USE_BANK_B | BOOT_FLAG_OTA_PENDING,
        .boot_count  = 0,
        .reserved    = 0,
    };
    flash_sector_erase(FLASH_ADDR_BOOT_FLAGS);
    flash_page_program(FLASH_ADDR_BOOT_FLAGS,
                       (const uint8_t *)&flags, sizeof(flags));

    return OTA_OK;
}
```

### OTA State Machine

```
App receives new firmware over network (MQTT/HTTPS)
              │
              ▼
        Write to Bank B
              │
              ▼
    Verify Bank B CRC/Signature
              │
        PASS  │  FAIL → abort OTA, stay on Bank A
              ▼
   Set BOOT_FLAG_USE_BANK_B + boot_count=0
              │
              ▼
          Reboot
              │
              ▼
   Bootloader boots Bank B (boot_count++)
              │
         PASS │  FAIL/Timeout → revert to Bank A
              ▼
   App confirms boot success → clear OTA_PENDING flag
```

---

## Debugging and Error Handling

### Common Failure Modes

| Symptom | Likely Cause | Fix |
|---|---|---|
| JEDEC ID reads 0xFF | SPI CS not asserted / clock polarity wrong | Check CPOL/CPHA settings |
| CRC always fails | Wrong byte order in CRC polynomial | Verify reflected vs. non-reflected CRC |
| App never starts | Stack pointer not initialized | Verify VTOR and MSP setup |
| Flash reads zeros | Flash in power-down mode | Send wakeup command |
| Write fails silently | Write Enable not sent | Check WEL bit before programming |
| Corrupted read data | SPI clock too fast | Reduce SPI frequency |

### Defensive Timeout Example (C)

```c
// Always use timeouts on flash operations to prevent infinite loops
#define FLASH_TIMEOUT_MS    500

bool flash_wait_ready_safe(void)
{
    uint32_t deadline = hal_get_tick_ms() + FLASH_TIMEOUT_MS;
    while (1) {
        if (!(flash_read_status() & FLASH_STATUS_BUSY)) {
            return true;
        }
        if (hal_get_tick_ms() > deadline) {
            // Log, reset watchdog, or enter safe state
            hal_log("ERROR: Flash operation timed out!\r\n");
            return false;
        }
    }
}
```

### Watchdog Integration

```c
// Always kick the watchdog during long flash operations
bool flash_sector_erase_safe(uint32_t addr)
{
    if (!flash_write_enable()) return false;

    spi_cs_assert();
    spi_transfer_byte(FLASH_CMD_SECTOR_ERASE);
    flash_send_address(addr & ~(FLASH_SECTOR_SIZE - 1));
    spi_cs_deassert();

    uint32_t start = hal_get_tick_ms();
    while (flash_read_status() & FLASH_STATUS_BUSY) {
        hal_watchdog_kick();               // Prevent WDT reset during erase
        if ((hal_get_tick_ms() - start) > 500) {
            return false;
        }
        hal_delay_ms(1);
    }
    return true;
}
```

---

## Summary

A **SPI Flash Bootloader** is a foundational component in embedded systems that bridges the gap between bare-metal hardware initialization and application firmware execution. Key takeaways:

**Architecture:** The bootloader initializes the SPI peripheral, reads a firmware header from a known flash address, validates the header's magic number and integrity checksum (CRC32 or SHA-256), copies the firmware image into executable RAM (or leverages XIP), and finally transfers control by updating the vector table and jumping to the application's reset handler.

**Flash Driver:** The SPI flash driver wraps low-level SPI byte transfers into higher-level read, write-enable, page-program, and sector-erase operations. Every write must be preceded by a Write Enable command, and every program/erase operation must be followed by polling the BUSY status bit with a timeout.

**Firmware Header:** A fixed-size, packed structure placed at the start of each flash bank carries essential metadata — magic number, firmware size, load address, entry point offset, CRC32, and optionally a SHA-256 or ECDSA signature. Validation of this header is the first gate before any loading occurs.

**C/C++ vs. Rust:** Both languages can implement the same logic, but Rust's type system prevents entire classes of bugs (buffer overflows, use-after-free, integer overflow) at compile time — particularly valuable in safety-critical bootloader code. Rust's `embedded-hal` traits also enable hardware-agnostic driver code.

**Dual-Bank OTA:** Production systems should implement A/B (dual-bank) firmware slots so that a failed OTA update never bricks the device. The bootloader reads persistent boot flags to select the active bank and can roll back automatically if the application fails to confirm a successful boot within a timeout or attempt threshold.

**Security:** For products requiring secure boot, CRC32 must be supplemented with ECDSA or RSA signature verification using a public key stored in OTP memory. This ensures that only firmware signed by the manufacturer can be loaded, preventing firmware tampering and unauthorized code execution.

**Reliability:** All flash operations must use timeouts, the hardware watchdog must be kicked during long erase cycles, and the bootloader should never enter an infinite loop without a safe recovery path.

---

*Document generated for topic 81 — SPI Flash Bootloader | Embedded Systems Series*