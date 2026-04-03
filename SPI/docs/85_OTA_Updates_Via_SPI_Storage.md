# 85. OTA Updates via SPI Storage

**Conceptual coverage** — why SPI flash is ideal as a staging area, the A/B dual-bank partitioning strategy, and the OTA state machine (`IDLE → DOWNLOADING → VERIFYING → FLASHING → COMPLETE`).

**C/C++ examples** include:
- A complete low-level SPI flash driver for the W25Q series (read, page-program, sector-erase)
- A full `ota_manager` with `begin()`, `write_chunk()`, `finalize()` (CRC32 + SHA-256 verification), and `apply()` (copy SPI → internal flash)
- Bootloader integration with watchdog-based rollback
- Firmware self-confirmation on the application side

**Rust examples** include:
- A trait-based `SpiFlash` abstraction for hardware portability
- A generic `OtaManager<F: SpiFlash>` with the same pipeline, using Rust's type system to enforce correct usage
- An async Embassy task showing real-world RTOS integration with TCP streaming

**Security and reliability** sections cover cryptographic verification chains, ECDSA signing, anti-rollback via OTP fuses, AES-CTR decryption during flashing, and exponential-backoff retry logic.

**Over-the-Air Update Strategies Using SPI Flash as Staging Area**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Fundamentals of OTA via SPI Flash](#fundamentals)
3. [SPI Flash Architecture for OTA](#architecture)
4. [Partition Strategies](#partition-strategies)
5. [OTA Update Pipeline](#ota-update-pipeline)
6. [C/C++ Implementation](#c-cpp-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Security Considerations](#security-considerations)
9. [Error Handling and Rollback](#error-handling-and-rollback)
10. [Summary](#summary)

---

## 1. Introduction <a name="introduction"></a>

Over-the-Air (OTA) firmware updates are a critical capability for embedded systems deployed in the field. Rather than requiring physical access to a device's programming interface, OTA updates allow new firmware images to be delivered over a network connection and applied safely to the target device.

SPI (Serial Peripheral Interface) flash memory is an ideal staging area for OTA updates because:

- It provides non-volatile, byte-addressable storage external to the MCU.
- It is fast enough for buffering full firmware images (typical speeds of 20–80 MHz clock).
- It decouples the download phase from the programming phase, enabling atomic update application.
- It is electrically isolated from the internal flash, reducing the risk of a failed download corrupting the running firmware.

The general strategy is: **download the new firmware image into SPI flash → verify integrity → copy to internal flash → reboot**. The SPI flash acts as a reliable, independent "staging area" for the incoming image.

---

## 2. Fundamentals of OTA via SPI Flash <a name="fundamentals"></a>

### 2.1 Why a Staging Area?

Writing firmware directly to internal flash during a network transfer is risky:

- Network interruptions can leave partial images.
- Power loss mid-write corrupts the firmware.
- Internal flash has limited erase cycles and must be managed carefully.

A SPI flash staging area solves these problems:

- The download completes and is verified *before* the internal flash is touched.
- The actual programming step is fast and deterministic (no waiting on network I/O).
- A failed download simply discards the SPI buffer — the device keeps running.

### 2.2 A/B Partitioning Concept

The gold standard for OTA reliability is **A/B partitioning** (also called dual-bank):

```
+--------------------+      +--------------------+
|   Internal Flash   |      |    SPI Flash        |
+--------------------+      +--------------------+
| Bootloader         |      | OTA Staging Area   |
| Firmware Slot A    |      | Firmware Image NEW |
| Firmware Slot B    |      | Metadata / Header  |
| Flags/Metadata     |      +--------------------+
+--------------------+
```

- **Slot A** holds the currently running firmware.
- **Slot B** (or the SPI staging area) receives the new image.
- On verified success, the bootloader is instructed to boot from Slot B.
- The old Slot A becomes the new fallback.

### 2.3 OTA State Machine

```
IDLE → DOWNLOADING → VERIFYING → FLASHING → REBOOTING → (NEW FW RUNS)
         ↓ error        ↓ error      ↓ error
       ABORT          ABORT        ROLLBACK
```

---

## 3. SPI Flash Architecture for OTA <a name="architecture"></a>

### 3.1 SPI Flash Layout

A typical SPI flash layout for OTA staging:

```
Offset 0x000000  +--------------------------+
                 |  Header / Magic (256 B)  |
Offset 0x000100  +--------------------------+
                 |  Firmware Image           |
                 |  (up to 1 MB or more)    |
Offset 0x100000  +--------------------------+
                 |  CRC / Hash Block (64 B) |
Offset 0x100040  +--------------------------+
                 |  Reserved / Config       |
                 +--------------------------+
```

### 3.2 OTA Image Header

```c
// OTA image header stored at the beginning of SPI flash staging area
typedef struct {
    uint32_t magic;           // e.g., 0x0TA_F1A5 
    uint32_t version;         // Firmware version number
    uint32_t size;            // Image size in bytes
    uint32_t crc32;           // CRC32 of the firmware payload
    uint8_t  sha256[32];      // SHA-256 hash of firmware
    uint8_t  signature[64];   // Optional: ECDSA signature
    uint32_t target_addr;     // Internal flash destination address
    uint32_t flags;           // Compression, encryption flags
    uint8_t  reserved[52];    // Pad to 256 bytes
} ota_header_t;               // Total: 256 bytes
```

---

## 4. Partition Strategies <a name="partition-strategies"></a>

### 4.1 Single-Bank with SPI Staging (Minimal Flash)

For cost-constrained devices with limited internal flash:

```
Internal Flash: [Bootloader | Active FW      ]
SPI Flash:      [Staging Area (New FW image) ]

Flow:
1. Download new FW → SPI staging area
2. Verify CRC/Hash
3. Erase internal FW region
4. Copy SPI → Internal flash
5. Update boot flags
6. Reboot
```

**Risk:** Between step 3 (erase) and step 4 (copy), the device has no valid firmware. Power loss = brick.
**Mitigation:** Keep bootloader in a protected region that can re-flash from SPI on boot if the firmware region is empty.

### 4.2 Dual-Bank (A/B) with SPI Staging

```
Internal Flash: [Bootloader | Slot A (active) | Slot B (inactive)]
SPI Flash:      [Staging Area                                     ]

Flow:
1. Download new FW → SPI staging area
2. Verify integrity
3. Copy SPI → Slot B (inactive)
4. Set boot flag: "try Slot B"
5. Reboot
6. Bootloader boots Slot B, runs confirmation watchdog
7. New FW confirms itself: "set Slot B permanent"
8. On failure: bootloader reverts to Slot A
```

This is the safest and recommended approach.

### 4.3 Compressed Images

SPI flash can store a compressed image to reduce download time and storage requirements:

```
SPI Flash: [Header | zlib/LZ4 compressed image]
           → Decompress on-the-fly during copy to internal flash
```

---

## 5. OTA Update Pipeline <a name="ota-update-pipeline"></a>

### 5.1 Full Pipeline Overview

```
[Server]                      [Device]
   |                              |
   |-- Notify new version ------> |
   |                         Check version
   |                         if newer:
   |<-- Request firmware ---------|
   |                              |
   |-- Stream chunks -----------> |
   |                         Write to SPI flash
   |                         (chunk by chunk)
   |                              |
   |-- Transfer complete -------> |
   |                         Verify CRC/SHA256
   |                         if OK:
   |                           Copy SPI → internal flash
   |                           Set boot flags
   |                           Reboot
   |                         if FAIL:
   |                           Erase SPI staging area
   |                           Report error
```

---

## 6. C/C++ Implementation <a name="c-cpp-implementation"></a>

### 6.1 SPI Flash Driver Abstraction

```c
// spi_flash.h — Hardware abstraction layer for SPI flash
#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SPI_FLASH_PAGE_SIZE     256
#define SPI_FLASH_SECTOR_SIZE   4096
#define SPI_FLASH_STAGING_ADDR  0x000000UL
#define SPI_FLASH_STAGING_SIZE  (512 * 1024)  // 512 KB max image

typedef enum {
    SPI_FLASH_OK        = 0,
    SPI_FLASH_ERR_BUSY  = -1,
    SPI_FLASH_ERR_WRITE = -2,
    SPI_FLASH_ERR_ERASE = -3,
    SPI_FLASH_ERR_READ  = -4,
    SPI_FLASH_ERR_PARAM = -5,
} spi_flash_err_t;

spi_flash_err_t spi_flash_init(void);
spi_flash_err_t spi_flash_read(uint32_t addr, uint8_t *buf, size_t len);
spi_flash_err_t spi_flash_write_page(uint32_t addr, const uint8_t *buf, size_t len);
spi_flash_err_t spi_flash_erase_sector(uint32_t addr);
spi_flash_err_t spi_flash_erase_range(uint32_t addr, size_t len);
bool            spi_flash_is_busy(void);

#endif // SPI_FLASH_H
```

```c
// spi_flash.c — Low-level SPI flash driver (example for W25Q series)
#include "spi_flash.h"
#include "spi_hal.h"   // Platform SPI HAL (not shown)

// W25Q command set
#define CMD_WRITE_ENABLE    0x06
#define CMD_WRITE_DISABLE   0x04
#define CMD_READ_STATUS     0x05
#define CMD_PAGE_PROGRAM    0x02
#define CMD_READ_DATA       0x03
#define CMD_SECTOR_ERASE    0x20
#define CMD_CHIP_ERASE      0xC7

#define STATUS_WIP_BIT      0x01  // Write-in-progress

static void spi_select(void)   { spi_hal_cs_low();  }
static void spi_deselect(void) { spi_hal_cs_high(); }

static uint8_t read_status(void) {
    uint8_t status;
    spi_select();
    spi_hal_transfer_byte(CMD_READ_STATUS);
    status = spi_hal_transfer_byte(0xFF);
    spi_deselect();
    return status;
}

static void wait_not_busy(void) {
    while (read_status() & STATUS_WIP_BIT) {
        // Could yield to RTOS here in a real system
    }
}

spi_flash_err_t spi_flash_read(uint32_t addr, uint8_t *buf, size_t len) {
    if (!buf || len == 0) return SPI_FLASH_ERR_PARAM;
    wait_not_busy();

    spi_select();
    spi_hal_transfer_byte(CMD_READ_DATA);
    spi_hal_transfer_byte((addr >> 16) & 0xFF);
    spi_hal_transfer_byte((addr >>  8) & 0xFF);
    spi_hal_transfer_byte((addr >>  0) & 0xFF);
    for (size_t i = 0; i < len; i++) {
        buf[i] = spi_hal_transfer_byte(0xFF);
    }
    spi_deselect();
    return SPI_FLASH_OK;
}

spi_flash_err_t spi_flash_write_page(uint32_t addr, const uint8_t *buf, size_t len) {
    if (!buf || len == 0 || len > SPI_FLASH_PAGE_SIZE) return SPI_FLASH_ERR_PARAM;
    wait_not_busy();

    // Enable write
    spi_select();
    spi_hal_transfer_byte(CMD_WRITE_ENABLE);
    spi_deselect();

    // Page program
    spi_select();
    spi_hal_transfer_byte(CMD_PAGE_PROGRAM);
    spi_hal_transfer_byte((addr >> 16) & 0xFF);
    spi_hal_transfer_byte((addr >>  8) & 0xFF);
    spi_hal_transfer_byte((addr >>  0) & 0xFF);
    for (size_t i = 0; i < len; i++) {
        spi_hal_transfer_byte(buf[i]);
    }
    spi_deselect();
    wait_not_busy();
    return SPI_FLASH_OK;
}

spi_flash_err_t spi_flash_erase_sector(uint32_t addr) {
    addr &= ~(SPI_FLASH_SECTOR_SIZE - 1);  // Align to sector
    wait_not_busy();

    spi_select();
    spi_hal_transfer_byte(CMD_WRITE_ENABLE);
    spi_deselect();

    spi_select();
    spi_hal_transfer_byte(CMD_SECTOR_ERASE);
    spi_hal_transfer_byte((addr >> 16) & 0xFF);
    spi_hal_transfer_byte((addr >>  8) & 0xFF);
    spi_hal_transfer_byte((addr >>  0) & 0xFF);
    spi_deselect();
    wait_not_busy();
    return SPI_FLASH_OK;
}

spi_flash_err_t spi_flash_erase_range(uint32_t addr, size_t len) {
    uint32_t end = addr + len;
    uint32_t sector = addr & ~(SPI_FLASH_SECTOR_SIZE - 1);
    while (sector < end) {
        spi_flash_err_t err = spi_flash_erase_sector(sector);
        if (err != SPI_FLASH_OK) return err;
        sector += SPI_FLASH_SECTOR_SIZE;
    }
    return SPI_FLASH_OK;
}
```

### 6.2 OTA Manager

```c
// ota_manager.h
#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define OTA_MAGIC           0x4F544146UL  // "OTAF"
#define OTA_MAX_IMAGE_SIZE  (480 * 1024)  // 480 KB

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_FLASHING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ERROR,
} ota_state_t;

typedef enum {
    OTA_OK              =  0,
    OTA_ERR_TOO_LARGE   = -1,
    OTA_ERR_BAD_MAGIC   = -2,
    OTA_ERR_CRC_FAIL    = -3,
    OTA_ERR_HASH_FAIL   = -4,
    OTA_ERR_FLASH_FAIL  = -5,
    OTA_ERR_BUSY        = -6,
} ota_err_t;

// OTA header in SPI flash
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t crc32;
    uint8_t  sha256[32];
    uint32_t target_addr;
    uint32_t flags;
    uint8_t  reserved[172];  // Pad to 256 bytes
} ota_header_t;

ota_err_t ota_begin(uint32_t expected_size, uint32_t expected_version);
ota_err_t ota_write_chunk(const uint8_t *data, size_t len, uint32_t offset);
ota_err_t ota_finalize(uint32_t crc32, const uint8_t sha256[32]);
ota_err_t ota_apply(void);
ota_state_t ota_get_state(void);
void      ota_abort(void);

#endif // OTA_MANAGER_H
```

```c
// ota_manager.c — OTA update manager using SPI flash staging
#include "ota_manager.h"
#include "spi_flash.h"
#include "internal_flash.h"  // Internal flash write API
#include "crc32.h"
#include "sha256.h"
#include <string.h>

static ota_state_t s_state = OTA_STATE_IDLE;
static uint32_t    s_expected_size = 0;
static uint32_t    s_bytes_written = 0;

ota_err_t ota_begin(uint32_t expected_size, uint32_t expected_version) {
    (void)expected_version;  // Can be used for version guard logic

    if (s_state != OTA_STATE_IDLE) return OTA_ERR_BUSY;
    if (expected_size > OTA_MAX_IMAGE_SIZE) return OTA_ERR_TOO_LARGE;

    // Erase staging area (header sector + image sectors)
    size_t erase_size = sizeof(ota_header_t) + expected_size;
    spi_flash_err_t err = spi_flash_erase_range(SPI_FLASH_STAGING_ADDR, erase_size);
    if (err != SPI_FLASH_OK) return OTA_ERR_FLASH_FAIL;

    s_expected_size = expected_size;
    s_bytes_written = 0;
    s_state = OTA_STATE_DOWNLOADING;
    return OTA_OK;
}

ota_err_t ota_write_chunk(const uint8_t *data, size_t len, uint32_t offset) {
    if (s_state != OTA_STATE_DOWNLOADING) return OTA_ERR_BUSY;
    if (offset + len > s_expected_size)   return OTA_ERR_TOO_LARGE;

    // Image data is written after the header (256-byte offset)
    uint32_t flash_addr = SPI_FLASH_STAGING_ADDR + sizeof(ota_header_t) + offset;

    // Write page by page (SPI flash page = 256 bytes)
    size_t remaining = len;
    const uint8_t *src = data;
    while (remaining > 0) {
        // Handle unaligned start within a page
        uint32_t page_offset = flash_addr % SPI_FLASH_PAGE_SIZE;
        size_t write_len = SPI_FLASH_PAGE_SIZE - page_offset;
        if (write_len > remaining) write_len = remaining;

        spi_flash_err_t err = spi_flash_write_page(flash_addr, src, write_len);
        if (err != SPI_FLASH_OK) {
            s_state = OTA_STATE_ERROR;
            return OTA_ERR_FLASH_FAIL;
        }
        flash_addr  += write_len;
        src         += write_len;
        remaining   -= write_len;
    }

    s_bytes_written += (uint32_t)len;
    return OTA_OK;
}

ota_err_t ota_finalize(uint32_t expected_crc, const uint8_t expected_sha256[32]) {
    if (s_state != OTA_STATE_DOWNLOADING) return OTA_ERR_BUSY;
    if (s_bytes_written != s_expected_size) return OTA_ERR_CRC_FAIL;

    s_state = OTA_STATE_VERIFYING;

    // Read image back from SPI flash and verify
    uint8_t read_buf[256];
    uint32_t running_crc = CRC32_INIT;
    sha256_ctx_t sha_ctx;
    sha256_init(&sha_ctx);

    uint32_t remaining = s_expected_size;
    uint32_t flash_addr = SPI_FLASH_STAGING_ADDR + sizeof(ota_header_t);

    while (remaining > 0) {
        size_t chunk = (remaining > sizeof(read_buf)) ? sizeof(read_buf) : remaining;
        if (spi_flash_read(flash_addr, read_buf, chunk) != SPI_FLASH_OK) {
            s_state = OTA_STATE_ERROR;
            return OTA_ERR_FLASH_FAIL;
        }
        running_crc = crc32_update(running_crc, read_buf, chunk);
        sha256_update(&sha_ctx, read_buf, chunk);
        flash_addr += chunk;
        remaining  -= chunk;
    }

    running_crc = crc32_finalize(running_crc);
    if (running_crc != expected_crc) {
        s_state = OTA_STATE_ERROR;
        return OTA_ERR_CRC_FAIL;
    }

    uint8_t computed_hash[32];
    sha256_final(&sha_ctx, computed_hash);
    if (memcmp(computed_hash, expected_sha256, 32) != 0) {
        s_state = OTA_STATE_ERROR;
        return OTA_ERR_HASH_FAIL;
    }

    // Write verified header to SPI flash
    ota_header_t header = {
        .magic       = OTA_MAGIC,
        .version     = 0,  // Caller may set via a separate API
        .size        = s_expected_size,
        .crc32       = expected_crc,
        .target_addr = INTERNAL_FLASH_FW_ADDR,  // Defined elsewhere
        .flags       = 0,
    };
    memcpy(header.sha256, expected_sha256, 32);

    if (spi_flash_write_page(SPI_FLASH_STAGING_ADDR, (uint8_t*)&header, sizeof(ota_header_t))
        != SPI_FLASH_OK) {
        s_state = OTA_STATE_ERROR;
        return OTA_ERR_FLASH_FAIL;
    }

    s_state = OTA_STATE_VERIFYING;
    return OTA_OK;
}

ota_err_t ota_apply(void) {
    if (s_state != OTA_STATE_VERIFYING) return OTA_ERR_BUSY;
    s_state = OTA_STATE_FLASHING;

    // Erase internal flash firmware slot
    if (internal_flash_erase(INTERNAL_FLASH_FW_ADDR, s_expected_size) != 0) {
        s_state = OTA_STATE_ERROR;
        return OTA_ERR_FLASH_FAIL;
    }

    // Copy SPI staging → internal flash in chunks
    uint8_t buf[256];
    uint32_t spi_addr      = SPI_FLASH_STAGING_ADDR + sizeof(ota_header_t);
    uint32_t internal_addr = INTERNAL_FLASH_FW_ADDR;
    uint32_t remaining     = s_expected_size;

    while (remaining > 0) {
        size_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        if (spi_flash_read(spi_addr, buf, chunk) != SPI_FLASH_OK) {
            // Cannot recover here — trigger rollback from bootloader
            s_state = OTA_STATE_ERROR;
            return OTA_ERR_FLASH_FAIL;
        }
        if (internal_flash_write(internal_addr, buf, chunk) != 0) {
            s_state = OTA_STATE_ERROR;
            return OTA_ERR_FLASH_FAIL;
        }
        spi_addr      += chunk;
        internal_addr += chunk;
        remaining     -= chunk;
    }

    // Set boot flags for new firmware
    boot_flags_set_pending_verify();

    s_state = OTA_STATE_COMPLETE;
    return OTA_OK;
}

void ota_abort(void) {
    // Erase the staging area to prevent a partial image from being applied
    spi_flash_erase_range(SPI_FLASH_STAGING_ADDR,
                          sizeof(ota_header_t) + s_expected_size);
    s_state    = OTA_STATE_IDLE;
    s_bytes_written = 0;
    s_expected_size = 0;
}

ota_state_t ota_get_state(void) {
    return s_state;
}
```

### 6.3 Bootloader Integration

```c
// bootloader.c — Minimal bootloader that checks for a pending OTA image
#include "spi_flash.h"
#include "ota_manager.h"
#include "internal_flash.h"
#include "boot_flags.h"
#include <string.h>

#define BOOT_FLAG_TRY_NEW   0x01
#define BOOT_FLAG_CONFIRMED 0x02
#define BOOT_FLAG_ROLLBACK  0x04

void bootloader_main(void) {
    spi_flash_init();
    boot_flags_t flags = boot_flags_read();

    if (flags & BOOT_FLAG_TRY_NEW) {
        // A new firmware is staged. Attempt to apply.
        ota_header_t header;
        spi_flash_read(SPI_FLASH_STAGING_ADDR, (uint8_t*)&header, sizeof(header));

        if (header.magic == OTA_MAGIC) {
            // Re-verify CRC before applying
            // (abbreviated: full verify loop as shown in ota_manager.c)
            bool crc_ok = verify_spi_image_crc(&header);

            if (crc_ok) {
                apply_image_from_spi(&header);
                boot_flags_clear(BOOT_FLAG_TRY_NEW);
                boot_flags_set(BOOT_FLAG_CONFIRMED);
                // Start watchdog; new firmware must confirm within N seconds
                watchdog_start(OTA_CONFIRM_TIMEOUT_MS);
                jump_to_firmware(INTERNAL_FLASH_FW_ADDR);
            } else {
                // Bad CRC — stay on old firmware
                boot_flags_clear(BOOT_FLAG_TRY_NEW);
                boot_flags_set(BOOT_FLAG_ROLLBACK);
            }
        }
    }

    // Normal boot
    jump_to_firmware(INTERNAL_FLASH_FW_ADDR);
}
```

### 6.4 Firmware Self-Confirmation (Application Side)

```c
// ota_confirm.c — Called by application firmware after successful boot
#include "boot_flags.h"
#include "watchdog.h"

void ota_confirm_firmware(void) {
    // Pet the watchdog and mark firmware as good
    watchdog_feed();
    boot_flags_clear(BOOT_FLAG_TRY_NEW);
    boot_flags_set(BOOT_FLAG_CONFIRMED);
    // Now disable the OTA confirmation watchdog
    watchdog_stop_ota_timer();
}
```

---

## 7. Rust Implementation <a name="rust-implementation"></a>

### 7.1 SPI Flash Abstraction Trait

```rust
// spi_flash.rs — Trait-based SPI flash abstraction

use core::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpiFlashError {
    Busy,
    WriteFailed,
    EraseFailed,
    ReadFailed,
    InvalidParam,
}

impl fmt::Display for SpiFlashError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Busy        => write!(f, "Flash busy"),
            Self::WriteFailed => write!(f, "Flash write failed"),
            Self::EraseFailed => write!(f, "Flash erase failed"),
            Self::ReadFailed  => write!(f, "Flash read failed"),
            Self::InvalidParam => write!(f, "Invalid parameter"),
        }
    }
}

pub const PAGE_SIZE: usize = 256;
pub const SECTOR_SIZE: usize = 4096;

pub trait SpiFlash {
    fn read(&mut self, addr: u32, buf: &mut [u8]) -> Result<(), SpiFlashError>;
    fn write_page(&mut self, addr: u32, data: &[u8]) -> Result<(), SpiFlashError>;
    fn erase_sector(&mut self, addr: u32) -> Result<(), SpiFlashError>;

    /// Erase a range of sectors (rounds up to sector boundaries)
    fn erase_range(&mut self, addr: u32, len: usize) -> Result<(), SpiFlashError> {
        let start = addr & !(SECTOR_SIZE as u32 - 1);
        let end = addr + len as u32;
        let mut sector = start;
        while sector < end {
            self.erase_sector(sector)?;
            sector += SECTOR_SIZE as u32;
        }
        Ok(())
    }
}
```

### 7.2 OTA Header and State

```rust
// ota_types.rs — OTA data types

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OtaState {
    Idle,
    Downloading,
    Verifying,
    Flashing,
    Complete,
    Error,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OtaError {
    TooLarge,
    BadMagic,
    CrcMismatch,
    HashMismatch,
    FlashError(SpiFlashError),
    Busy,
    NotReady,
}

impl From<SpiFlashError> for OtaError {
    fn from(e: SpiFlashError) -> Self {
        OtaError::FlashError(e)
    }
}

pub const OTA_MAGIC: u32 = 0x4F544146; // "OTAF"
pub const HEADER_SIZE: usize = 256;
pub const MAX_IMAGE_SIZE: usize = 480 * 1024;
pub const STAGING_BASE_ADDR: u32 = 0x0000_0000;

#[repr(C, packed)]
pub struct OtaHeader {
    pub magic: u32,
    pub version: u32,
    pub size: u32,
    pub crc32: u32,
    pub sha256: [u8; 32],
    pub target_addr: u32,
    pub flags: u32,
    pub reserved: [u8; 172],
}

use super::spi_flash::SpiFlashError;
```

### 7.3 OTA Manager in Rust

```rust
// ota_manager.rs — Rust OTA manager using SPI flash staging

use crate::ota_types::*;
use crate::spi_flash::{SpiFlash, PAGE_SIZE};
use crate::crc32::Crc32;
use crate::sha256::Sha256;

pub struct OtaManager<F: SpiFlash> {
    flash: F,
    state: OtaState,
    expected_size: u32,
    bytes_written: u32,
}

impl<F: SpiFlash> OtaManager<F> {
    pub fn new(flash: F) -> Self {
        Self {
            flash,
            state: OtaState::Idle,
            expected_size: 0,
            bytes_written: 0,
        }
    }

    /// Begin a new OTA session. Erases the staging area.
    pub fn begin(&mut self, expected_size: u32) -> Result<(), OtaError> {
        if self.state != OtaState::Idle {
            return Err(OtaError::Busy);
        }
        if expected_size as usize > MAX_IMAGE_SIZE {
            return Err(OtaError::TooLarge);
        }

        let erase_len = HEADER_SIZE + expected_size as usize;
        self.flash.erase_range(STAGING_BASE_ADDR, erase_len)?;

        self.expected_size = expected_size;
        self.bytes_written = 0;
        self.state = OtaState::Downloading;
        Ok(())
    }

    /// Write a chunk of firmware data at a given offset within the image.
    pub fn write_chunk(&mut self, data: &[u8], offset: u32) -> Result<(), OtaError> {
        if self.state != OtaState::Downloading {
            return Err(OtaError::Busy);
        }
        if offset + data.len() as u32 > self.expected_size {
            return Err(OtaError::TooLarge);
        }

        let mut flash_addr = STAGING_BASE_ADDR + HEADER_SIZE as u32 + offset;
        let mut remaining = data;

        while !remaining.is_empty() {
            let page_offset = (flash_addr as usize) % PAGE_SIZE;
            let write_len = (PAGE_SIZE - page_offset).min(remaining.len());
            self.flash.write_page(flash_addr, &remaining[..write_len])?;
            flash_addr += write_len as u32;
            remaining = &remaining[write_len..];
        }

        self.bytes_written += data.len() as u32;
        Ok(())
    }

    /// Verify the received image against expected CRC and SHA-256.
    pub fn finalize(
        &mut self,
        expected_crc: u32,
        expected_sha256: &[u8; 32],
    ) -> Result<(), OtaError> {
        if self.state != OtaState::Downloading {
            return Err(OtaError::NotReady);
        }
        if self.bytes_written != self.expected_size {
            return Err(OtaError::CrcMismatch);
        }

        self.state = OtaState::Verifying;

        let mut buf = [0u8; 256];
        let mut crc = Crc32::new();
        let mut sha = Sha256::new();

        let mut remaining = self.expected_size;
        let mut addr = STAGING_BASE_ADDR + HEADER_SIZE as u32;

        while remaining > 0 {
            let chunk = (remaining as usize).min(buf.len());
            self.flash.read(addr, &mut buf[..chunk]).map_err(OtaError::from)?;
            crc.update(&buf[..chunk]);
            sha.update(&buf[..chunk]);
            addr += chunk as u32;
            remaining -= chunk as u32;
        }

        if crc.finalize() != expected_crc {
            self.state = OtaState::Error;
            return Err(OtaError::CrcMismatch);
        }

        let computed_hash = sha.finalize();
        if &computed_hash != expected_sha256 {
            self.state = OtaState::Error;
            return Err(OtaError::HashMismatch);
        }

        // Write the verified header
        let header = OtaHeader {
            magic: OTA_MAGIC,
            version: 0,
            size: self.expected_size,
            crc32: expected_crc,
            sha256: *expected_sha256,
            target_addr: 0x0800_8000, // Example: internal flash start
            flags: 0,
            reserved: [0u8; 172],
        };

        let header_bytes = unsafe {
            core::slice::from_raw_parts(
                &header as *const OtaHeader as *const u8,
                core::mem::size_of::<OtaHeader>(),
            )
        };

        // Write header in pages
        for (i, chunk) in header_bytes.chunks(PAGE_SIZE).enumerate() {
            self.flash.write_page(
                STAGING_BASE_ADDR + (i * PAGE_SIZE) as u32,
                chunk,
            )?;
        }

        self.state = OtaState::Verifying;
        Ok(())
    }

    /// Apply the verified image: copy from SPI flash to internal flash.
    pub fn apply<IF: InternalFlash>(&mut self, internal: &mut IF) -> Result<(), OtaError> {
        if self.state != OtaState::Verifying {
            return Err(OtaError::NotReady);
        }
        self.state = OtaState::Flashing;

        // Read header to get target address and size
        let mut header_bytes = [0u8; HEADER_SIZE];
        self.flash.read(STAGING_BASE_ADDR, &mut header_bytes)?;
        let header = unsafe { &*(header_bytes.as_ptr() as *const OtaHeader) };

        internal.erase(header.target_addr, header.size as usize)
            .map_err(|_| OtaError::FlashError(crate::spi_flash::SpiFlashError::EraseFailed))?;

        let mut buf = [0u8; 256];
        let mut spi_addr = STAGING_BASE_ADDR + HEADER_SIZE as u32;
        let mut int_addr = header.target_addr;
        let mut remaining = header.size;

        while remaining > 0 {
            let chunk = (remaining as usize).min(buf.len());
            self.flash.read(spi_addr, &mut buf[..chunk])?;
            internal.write(int_addr, &buf[..chunk])
                .map_err(|_| OtaError::FlashError(crate::spi_flash::SpiFlashError::WriteFailed))?;
            spi_addr += chunk as u32;
            int_addr += chunk as u32;
            remaining -= chunk as u32;
        }

        self.state = OtaState::Complete;
        Ok(())
    }

    /// Abort the current OTA session and clean up staging area.
    pub fn abort(&mut self) {
        let _ = self.flash.erase_range(
            STAGING_BASE_ADDR,
            HEADER_SIZE + self.expected_size as usize,
        );
        self.state = OtaState::Idle;
        self.bytes_written = 0;
        self.expected_size = 0;
    }

    pub fn state(&self) -> OtaState {
        self.state
    }
}

pub trait InternalFlash {
    type Error;
    fn erase(&mut self, addr: u32, len: usize) -> Result<(), Self::Error>;
    fn write(&mut self, addr: u32, data: &[u8]) -> Result<(), Self::Error>;
}
```

### 7.4 Example: RTOS-Integrated OTA Task (Rust + Embassy)

```rust
// ota_task.rs — Embassy async OTA download task (no_std)

use embassy_net::tcp::TcpSocket;
use crate::ota_manager::OtaManager;

#[embassy_executor::task]
async fn ota_download_task(mut socket: TcpSocket<'static>) {
    let flash = W25Q128::new(/* SPI peripheral */);
    let mut ota = OtaManager::new(flash);

    // Receive manifest (size + hashes) over TCP
    let mut manifest_buf = [0u8; 64];
    socket.read_exact(&mut manifest_buf).await.unwrap();
    let (expected_size, expected_crc, expected_sha256) = parse_manifest(&manifest_buf);

    if let Err(e) = ota.begin(expected_size) {
        log::error!("OTA begin failed: {:?}", e);
        return;
    }

    // Stream image data in chunks
    let mut chunk_buf = [0u8; 1024];
    let mut offset = 0u32;

    loop {
        let n = socket.read(&mut chunk_buf).await.unwrap();
        if n == 0 { break; }  // Connection closed

        if let Err(e) = ota.write_chunk(&chunk_buf[..n], offset) {
            log::error!("OTA write failed at offset {}: {:?}", offset, e);
            ota.abort();
            return;
        }
        offset += n as u32;
    }

    // Verify
    match ota.finalize(expected_crc, &expected_sha256) {
        Ok(()) => {
            log::info!("OTA verification passed. Applying...");
            let mut internal = InternalFlashDriver::new();
            if ota.apply(&mut internal).is_ok() {
                boot_flags_request_update();
                cortex_m::peripheral::SCB::sys_reset();
            }
        }
        Err(e) => {
            log::error!("OTA verification failed: {:?}", e);
            ota.abort();
        }
    }
}
```

---

## 8. Security Considerations <a name="security-considerations"></a>

### 8.1 Cryptographic Verification Chain

```
Server signs image with ECDSA private key
    │
    ▼
Device verifies signature with embedded public key
    │
    ▼
SHA-256 hash confirms bit-exact integrity
    │
    ▼
CRC32 provides fast pre-check (not cryptographic)
```

### 8.2 Anti-Rollback

```c
// anti_rollback.c — Prevent downgrade attacks

#define VERSION_FUSE_ADDR  0x1FFF7800  // OTP/fuse memory base

bool ota_version_is_acceptable(uint32_t new_version) {
    uint32_t min_version = read_otp_fuse(VERSION_FUSE_ADDR);
    return new_version >= min_version;
}

void ota_burn_version_fuse(uint32_t version) {
    // One-time burn: each bit represents a minimum version increment
    // Once burned, cannot be un-burned — prevents rollback
    uint32_t current = read_otp_fuse(VERSION_FUSE_ADDR);
    uint32_t new_fuse = current | ((1U << version) - 1U);
    write_otp_fuse(VERSION_FUSE_ADDR, new_fuse);
}
```

### 8.3 Encrypted Images

```c
// Decryption during copy (AES-128-CTR example)
void decrypt_and_flash(const uint8_t *key, const uint8_t *iv) {
    aes_ctr_ctx_t ctx;
    aes_ctr_init(&ctx, key, iv);

    uint8_t enc_buf[256], dec_buf[256];
    // ... read enc_buf from SPI, decrypt to dec_buf, write dec_buf to internal flash
    aes_ctr_decrypt(&ctx, enc_buf, dec_buf, sizeof(enc_buf));
}
```

---

## 9. Error Handling and Rollback <a name="error-handling-and-rollback"></a>

### 9.1 Watchdog-Based Rollback

```c
// Bootloader watchdog rollback logic

void bootloader_check_rollback(void) {
    if (boot_flags_get() & BOOT_FLAG_TRY_NEW) {
        if (watchdog_was_triggered()) {
            // New firmware failed to confirm in time — revert
            boot_flags_clear(BOOT_FLAG_TRY_NEW);
            boot_flags_set(BOOT_FLAG_ROLLBACK);
            jump_to_firmware(INTERNAL_FLASH_FALLBACK_ADDR);
        }
    }
}
```

### 9.2 Retry Logic in Rust

```rust
// Retry download with exponential backoff

async fn ota_with_retry<F: SpiFlash>(flash: F, server: &str) -> Result<(), OtaError> {
    let mut delay_ms = 1000u64;
    for attempt in 0..5 {
        match attempt_ota_download(flash, server).await {
            Ok(()) => return Ok(()),
            Err(e) => {
                log::warn!("OTA attempt {} failed: {:?}. Retry in {}ms", attempt, e, delay_ms);
                embassy_time::Timer::after_millis(delay_ms).await;
                delay_ms = (delay_ms * 2).min(30_000);
            }
        }
    }
    Err(OtaError::TooLarge) // Exhausted retries
}
```

---

## 10. Summary <a name="summary"></a>

OTA updates via SPI flash staging is a robust, industry-proven pattern for safely updating embedded firmware in the field. The SPI flash device acts as a reliable buffer that fully decouples the download and write phases, protecting the running system from interrupted transfers or corrupt images.

**Key architectural principles:**

- **Stage first, flash second.** Always complete and verify the download in SPI flash before touching internal flash. This makes the update atomic from the device's perspective.
- **Use dual-bank (A/B) partitioning** wherever internal flash space permits. This provides instant, zero-downtime rollback if the new firmware fails to self-confirm.
- **Verify cryptographically.** CRC32 catches bit errors; SHA-256 provides tamper detection; ECDSA signatures authenticate the source. Use all three layers.
- **Implement watchdog-based rollback** in the bootloader. Even if the application starts successfully, a hardware watchdog ensures automatic reversion if the new firmware crashes or hangs before confirming.
- **Protect against rollback attacks** using OTP fuse-based anti-rollback counters to prevent downgrading to vulnerable firmware versions.

**Language choice:**

- **C/C++** remains the dominant choice for constrained bare-metal targets (< 64 KB RAM) due to its minimal runtime overhead, direct hardware access, and mature toolchain ecosystem.
- **Rust** provides memory safety guarantees (no buffer overflows, no null dereferences) and an expressive type system with traits, making it excellent for safety-critical OTA pipelines. Embedded Rust frameworks like Embassy offer async task support suitable for concurrent network + flash operations.

Both approaches ultimately implement the same state machine: `IDLE → DOWNLOADING → VERIFYING → FLASHING → COMPLETE`, with error paths leading to `ABORT` or `ROLLBACK`. The critical investment is in a solid bootloader that treats SPI flash as a trusted staging area and knows how to recover from every failure mode.

---

*Document: Topic 85 — OTA Updates via SPI Storage | Embedded Systems SPI Reference Series*