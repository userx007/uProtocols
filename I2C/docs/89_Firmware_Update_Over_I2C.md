# 89. Firmware Update over I2C

**Architecture & Protocol** — ASCII block diagrams of the host/target layout and flash memory map, a complete packet format table (CMD + SEQ + LEN + PAYLOAD + CRC-16), the full command set with response codes, and a state-machine diagram walking through the update lifecycle.

**C/C++ Implementation** — Four code files: a portable `crc16` utility, a shared `fwupdate_protocol.h`, a Linux host driver (`fwupdate_host.c`) with chunked streaming and retry logic, and a bare-metal STM32 target bootloader with HAL I2C slave callbacks, flash erase/write helpers, and the application jump routine.

**Rust Implementation** — Three files: a `#![no_std]`-compatible `protocol.rs` with `TryFrom` for response codes and packet builder, a Linux host driver using `linux-embedded-hal` and `embedded-hal` traits with a full `perform_update()` orchestrator, and an embedded Rust target bootloader using `cortex-m-rt` with the same state machine logic.

**Supporting Sections** — Dual-bank staging strategy with a metadata struct, host-side exponential back-off retry, watchdog handling during flash erase, Ed25519 image signing layout, rollback protection via OTP fuse counters, and a hardware-in-the-loop test scenario table.

> **Implementing bootloader protocols for field firmware upgrades via I2C**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [Protocol Design](#protocol-design)
4. [Bootloader Fundamentals](#bootloader-fundamentals)
5. [C/C++ Implementation](#cc-implementation)
6. [Rust Implementation](#rust-implementation)
7. [Error Handling & Recovery](#error-handling--recovery)
8. [Security Considerations](#security-considerations)
9. [Testing Strategies](#testing-strategies)
10. [Summary](#summary)

---

## Introduction

Firmware Update over I2C (also known as **In-Field Firmware Upgrade** or **FOTA/FUOTA** via I2C) is a technique that allows a host processor to transfer a new firmware image to a target device — a microcontroller, sensor, FPGA, or peripheral — entirely through the standard two-wire I2C bus. This approach eliminates the need for dedicated programming interfaces (JTAG, SWD, ISP) in deployed systems, dramatically reducing maintenance cost and enabling remote or automated upgrades.

### Why I2C for Firmware Updates?

- **Ubiquity** — I2C is present on almost every embedded platform and is frequently already wired between subsystems.
- **Low pin count** — only SDA and SCL are required; no extra GPIO, UART, or SPI lines needed.
- **Multi-device** — a single I2C bus can serve many target devices, each with a unique 7-bit or 10-bit address.
- **Simplicity** — deterministic, byte-oriented transactions are easy to implement on tiny bootloaders with minimal flash footprint.

### Typical Use Cases

- Updating firmware on **sensor nodes** in industrial IoT installations.
- Upgrading **co-processors** (power managers, motor controllers, display controllers) attached to a main SoC.
- Deploying **security patches** to remote field devices via a gateway that communicates locally over I2C.
- **Production programming** of blank devices assembled on a PCB.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                        HOST SYSTEM                       │
│  ┌──────────────┐     ┌──────────────────────────────┐  │
│  │  New Firmware│────▶│  I2C Bootloader Host Driver  │  │
│  │  Image (.bin)│     │  (Chunking, CRC, Protocol)   │  │
│  └──────────────┘     └──────────────┬───────────────┘  │
└─────────────────────────────────────┼───────────────────┘
                                       │ I2C Bus (SDA/SCL)
┌──────────────────────────────────────┼───────────────────┐
│                        TARGET DEVICE │                    │
│  ┌──────────────────────────────────▼────────────────┐  │
│  │              I2C Slave Interface                    │  │
│  └──────────────────────┬──────────────────────────── ┘  │
│                          │                               │
│  ┌───────────────────────▼───────────────────────────┐  │
│  │                    BOOTLOADER                       │  │
│  │  ┌─────────────┐  ┌──────────┐  ┌──────────────┐  │  │
│  │  │ Cmd Parser  │  │ Flash    │  │ Integrity    │  │  │
│  │  │ & State     │  │ Writer   │  │ Check (CRC)  │  │  │
│  │  │ Machine     │  │          │  │              │  │  │
│  │  └─────────────┘  └──────────┘  └──────────────┘  │  │
│  └───────────────────────────────────────────────────┘  │
│                          │                               │
│  ┌───────────────────────▼───────────────────────────┐  │
│  │              APPLICATION FLASH                      │  │
│  │   [Bootloader Region] | [Application Region]        │  │
│  └───────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

### Memory Map

A typical flash layout separates the bootloader from application code:

| Region          | Start Address  | Size     | Description                          |
|-----------------|----------------|----------|--------------------------------------|
| Bootloader      | `0x08000000`   | 16 KB    | Resident bootloader; never overwritten |
| Metadata        | `0x08004000`   | 4 KB     | Version, CRC, flags                  |
| Application     | `0x08005000`   | 236 KB   | Active firmware image                |
| Swap/Staging    | `0x08040000`   | 236 KB   | Incoming firmware (double-buffering) |
| EEPROM/Config   | `0x08080000`   | 4 KB     | Persistent settings                  |

> **Important:** The bootloader region must be write-protected (via hardware lock bits) so that a failed update can never corrupt the recovery path.

---

## Protocol Design

A well-designed I2C firmware update protocol is structured around a small set of **commands**, each transmitted as a sequence of I2C write/read transactions.

### Packet Format

```
┌──────────┬──────────┬────────────┬──────────────────────┬───────────┐
│  CMD (1B)│  SEQ (2B)│  LEN (2B)  │    PAYLOAD (0-64B)   │  CRC (2B) │
└──────────┴──────────┴────────────┴──────────────────────┴───────────┘
```

| Field   | Size    | Description                              |
|---------|---------|------------------------------------------|
| CMD     | 1 byte  | Command identifier (see table below)     |
| SEQ     | 2 bytes | Sequence / chunk number (little-endian)  |
| LEN     | 2 bytes | Payload length in bytes                  |
| PAYLOAD | 0–64 B  | Command-specific data                    |
| CRC     | 2 bytes | CRC-16/CCITT over CMD+SEQ+LEN+PAYLOAD    |

### Command Set

| Command         | Code   | Direction     | Description                            |
|-----------------|--------|---------------|----------------------------------------|
| `CMD_PING`      | `0x01` | Host → Target | Check bootloader is alive              |
| `CMD_START`     | `0x02` | Host → Target | Begin update; send image size + CRC    |
| `CMD_DATA`      | `0x03` | Host → Target | Transmit one data chunk                |
| `CMD_END`       | `0x04` | Host → Target | Signal end of transfer                 |
| `CMD_VERIFY`    | `0x05` | Host → Target | Request integrity verification         |
| `CMD_APPLY`     | `0x06` | Host → Target | Copy staging area → application, reboot|
| `CMD_ABORT`     | `0x07` | Host → Target | Abort update, discard staging area     |
| `CMD_STATUS`    | `0x08` | Target → Host | Return current bootloader state        |
| `CMD_VERSION`   | `0x09` | Target → Host | Return current firmware version        |

### Response Codes

| Code   | Meaning                       |
|--------|-------------------------------|
| `0x00` | `ACK` — success               |
| `0x01` | `NAK_CRC` — packet CRC error  |
| `0x02` | `NAK_SEQ` — sequence mismatch |
| `0x03` | `NAK_LEN` — invalid length    |
| `0x04` | `NAK_FLASH` — flash write error|
| `0x05` | `NAK_BUSY` — not ready        |
| `0xFF` | `NAK_UNKNOWN` — unknown error |

### State Machine

```
          ┌──────────────────────────────────────────┐
          │                                          │
      POWER-ON                                  CMD_ABORT
          │                                          │
          ▼                                          │
    ┌──────────┐   CMD_START    ┌────────────┐       │
    │   IDLE   │──────────────▶│ RECEIVING  │───────┘
    └──────────┘                └─────┬──────┘
          ▲                           │
          │                     All chunks
          │                     received
          │                           ▼
          │                    ┌────────────┐
          │    CRC FAIL        │ VERIFYING  │
          │◀───────────────────│            │
          │                    └─────┬──────┘
          │                          │ CRC OK
          │                          ▼
          │                    ┌────────────┐
          │   CMD_APPLY        │  VERIFIED  │
          │                    └─────┬──────┘
          │                          │
          │                          ▼
          │                    ┌────────────┐
          └────────────────────│  APPLYING  │
             Reboot complete   └────────────┘
```

---

## Bootloader Fundamentals

Before writing protocol code, several hardware-level tasks must be handled correctly.

### Entering Bootloader Mode

The target must decide at power-on whether to run the application or the bootloader. Common strategies:

1. **Magic word in RAM** — the application writes a known value (`0xDEADC0DE`) to a specific RAM address, then resets. The bootloader checks this value before jumping.
2. **Dedicated GPIO pin** — a boot-select pin (often pulled high by default) is driven low by the host to force bootloader entry.
3. **Flag in non-volatile memory** — a persistent byte in EEPROM or flash metadata indicates an update is pending.
4. **Application validity check** — if the application vector table is invalid (e.g., blank flash), the bootloader stays resident automatically.

### Jumping to Application

After a successful update the bootloader must cleanly transfer control:

```c
typedef void (*app_entry_t)(void);

void bootloader_jump_to_application(uint32_t app_start_addr) {
    uint32_t stack_pointer = *(volatile uint32_t *)app_start_addr;
    uint32_t reset_handler = *(volatile uint32_t *)(app_start_addr + 4);

    // Disable all peripherals and interrupts
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    // Relocate vector table
    SCB->VTOR = app_start_addr;

    // Set stack pointer and jump
    __set_MSP(stack_pointer);
    app_entry_t entry = (app_entry_t)reset_handler;
    entry();
}
```

---

## C/C++ Implementation

### 1. CRC-16 / CCITT Utility

```c
// crc16.h
#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stddef.h>

#define CRC16_INIT  0xFFFFU
#define CRC16_POLY  0x1021U   // CRC-16/CCITT (IBM 3740)

uint16_t crc16_update(uint16_t crc, uint8_t byte);
uint16_t crc16_buffer(const uint8_t *buf, size_t len);

#endif // CRC16_H
```

```c
// crc16.c
#include "crc16.h"

uint16_t crc16_update(uint16_t crc, uint8_t byte) {
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000U) ? (crc << 1) ^ CRC16_POLY : (crc << 1);
    }
    return crc;
}

uint16_t crc16_buffer(const uint8_t *buf, size_t len) {
    uint16_t crc = CRC16_INIT;
    for (size_t i = 0; i < len; i++) {
        crc = crc16_update(crc, buf[i]);
    }
    return crc;
}
```

---

### 2. Protocol Definitions (Shared Header)

```c
// fwupdate_protocol.h
#ifndef FWUPDATE_PROTOCOL_H
#define FWUPDATE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* ---- I2C Addressing ---- */
#define FWU_I2C_ADDR        0x42U   // 7-bit address of the target bootloader

/* ---- Commands ---- */
#define CMD_PING            0x01U
#define CMD_START           0x02U
#define CMD_DATA            0x03U
#define CMD_END             0x04U
#define CMD_VERIFY          0x05U
#define CMD_APPLY           0x06U
#define CMD_ABORT           0x07U
#define CMD_STATUS          0x08U
#define CMD_VERSION         0x09U

/* ---- Response / ACK ---- */
#define ACK                 0x00U
#define NAK_CRC             0x01U
#define NAK_SEQ             0x02U
#define NAK_LEN             0x03U
#define NAK_FLASH           0x04U
#define NAK_BUSY            0x05U
#define NAK_UNKNOWN         0xFFU

/* ---- Packet limits ---- */
#define FWU_MAX_PAYLOAD     64U
#define FWU_HEADER_SIZE     5U      // CMD(1) + SEQ(2) + LEN(2)
#define FWU_CRC_SIZE        2U
#define FWU_MAX_PACKET      (FWU_HEADER_SIZE + FWU_MAX_PAYLOAD + FWU_CRC_SIZE)

/* ---- Bootloader states ---- */
typedef enum {
    BL_STATE_IDLE       = 0,
    BL_STATE_RECEIVING  = 1,
    BL_STATE_VERIFYING  = 2,
    BL_STATE_VERIFIED   = 3,
    BL_STATE_APPLYING   = 4,
    BL_STATE_ERROR      = 5,
} bl_state_t;

/* ---- Packet structure ---- */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;
    uint16_t seq;
    uint16_t len;
    uint8_t  payload[FWU_MAX_PAYLOAD];
    uint16_t crc;
} fwu_packet_t;

/* ---- CMD_START payload ---- */
typedef struct __attribute__((packed)) {
    uint32_t image_size;    // Total firmware image size in bytes
    uint32_t image_crc32;   // CRC-32 of the entire image
    uint16_t chunk_size;    // Bytes per CMD_DATA payload
    uint8_t  version[4];    // New version: major.minor.patch.build
} fwu_start_payload_t;

#endif // FWUPDATE_PROTOCOL_H
```

---

### 3. Host-Side Driver (Linux / embedded Linux)

```c
// fwupdate_host.c  –  runs on the host (e.g., a Raspberry Pi or i.MX gateway)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include "fwupdate_protocol.h"
#include "crc16.h"

#define I2C_DEVICE      "/dev/i2c-1"
#define RETRY_COUNT     3
#define ACK_TIMEOUT_MS  200

/* Platform I2C abstraction */
typedef struct {
    int fd;
} i2c_handle_t;

static int i2c_open(i2c_handle_t *h, const char *dev, uint8_t addr) {
    h->fd = open(dev, O_RDWR);
    if (h->fd < 0) return -errno;
    if (ioctl(h->fd, I2C_SLAVE, addr) < 0) return -errno;
    return 0;
}

static void i2c_close(i2c_handle_t *h) {
    if (h->fd >= 0) close(h->fd);
}

static int i2c_write(i2c_handle_t *h, const uint8_t *buf, size_t len) {
    ssize_t n = write(h->fd, buf, len);
    return (n == (ssize_t)len) ? 0 : -EIO;
}

static int i2c_read(i2c_handle_t *h, uint8_t *buf, size_t len) {
    ssize_t n = read(h->fd, buf, len);
    return (n == (ssize_t)len) ? 0 : -EIO;
}

/* Build and send a packet, then read back one-byte ACK/NAK */
static int fwu_send_command(i2c_handle_t *h,
                             uint8_t cmd, uint16_t seq,
                             const uint8_t *payload, uint16_t payload_len) {
    if (payload_len > FWU_MAX_PAYLOAD) return -EINVAL;

    uint8_t pkt[FWU_MAX_PACKET];
    uint16_t pkt_len = 0;

    pkt[pkt_len++] = cmd;
    pkt[pkt_len++] = (uint8_t)(seq & 0xFF);
    pkt[pkt_len++] = (uint8_t)(seq >> 8);
    pkt[pkt_len++] = (uint8_t)(payload_len & 0xFF);
    pkt[pkt_len++] = (uint8_t)(payload_len >> 8);

    if (payload && payload_len > 0) {
        memcpy(&pkt[pkt_len], payload, payload_len);
        pkt_len += payload_len;
    }

    uint16_t crc = crc16_buffer(pkt, pkt_len);
    pkt[pkt_len++] = (uint8_t)(crc & 0xFF);
    pkt[pkt_len++] = (uint8_t)(crc >> 8);

    int rc = i2c_write(h, pkt, pkt_len);
    if (rc != 0) return rc;

    /* Small gap — target needs time to process and prepare response */
    usleep(ACK_TIMEOUT_MS * 1000UL);

    uint8_t resp;
    rc = i2c_read(h, &resp, 1);
    if (rc != 0) return rc;

    return (resp == ACK) ? 0 : (int)resp;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int fwu_ping(i2c_handle_t *h) {
    return fwu_send_command(h, CMD_PING, 0, NULL, 0);
}

int fwu_start(i2c_handle_t *h,
              uint32_t image_size, uint32_t image_crc32,
              uint16_t chunk_size, const uint8_t version[4]) {
    fwu_start_payload_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.image_size  = image_size;
    sp.image_crc32 = image_crc32;
    sp.chunk_size  = chunk_size;
    memcpy(sp.version, version, 4);
    return fwu_send_command(h, CMD_START, 0,
                             (const uint8_t *)&sp, sizeof(sp));
}

int fwu_send_data(i2c_handle_t *h, uint16_t chunk_idx,
                  const uint8_t *data, uint16_t data_len) {
    return fwu_send_command(h, CMD_DATA, chunk_idx, data, data_len);
}

int fwu_end(i2c_handle_t *h) {
    return fwu_send_command(h, CMD_END, 0, NULL, 0);
}

int fwu_verify(i2c_handle_t *h) {
    return fwu_send_command(h, CMD_VERIFY, 0, NULL, 0);
}

int fwu_apply(i2c_handle_t *h) {
    return fwu_send_command(h, CMD_APPLY, 0, NULL, 0);
}

int fwu_abort(i2c_handle_t *h) {
    return fwu_send_command(h, CMD_ABORT, 0, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* High-Level Update Orchestrator                                       */
/* ------------------------------------------------------------------ */

int fwu_perform_update(const char *i2c_dev, uint8_t target_addr,
                        const uint8_t *image, size_t image_size) {
    i2c_handle_t h = { .fd = -1 };
    int rc;
    const uint16_t CHUNK_SIZE = 32;

    printf("[FWU] Opening I2C device %s, target 0x%02X\n",
           i2c_dev, target_addr);
    rc = i2c_open(&h, i2c_dev, target_addr);
    if (rc) { fprintf(stderr, "[FWU] Open failed: %d\n", rc); return rc; }

    /* 1. Ping */
    printf("[FWU] Pinging bootloader...\n");
    for (int r = 0; r < RETRY_COUNT; r++) {
        rc = fwu_ping(&h);
        if (rc == 0) break;
        usleep(50000);
    }
    if (rc) { fprintf(stderr, "[FWU] Ping failed\n"); goto done; }

    /* 2. Compute whole-image CRC-32 (simple XOR-based for illustration) */
    uint32_t img_crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < image_size; i++) {
        img_crc ^= image[i];
        for (int b = 0; b < 8; b++) {
            img_crc = (img_crc & 1) ? (img_crc >> 1) ^ 0xEDB88320UL
                                     : (img_crc >> 1);
        }
    }
    img_crc ^= 0xFFFFFFFFUL;

    /* 3. CMD_START */
    const uint8_t new_version[4] = {1, 2, 0, 0};
    printf("[FWU] Starting update: %zu bytes, CRC32=0x%08X\n",
           image_size, img_crc);
    rc = fwu_start(&h, (uint32_t)image_size, img_crc,
                   CHUNK_SIZE, new_version);
    if (rc) { fprintf(stderr, "[FWU] CMD_START failed: %d\n", rc); goto done; }

    /* 4. Stream data chunks */
    size_t offset = 0;
    uint16_t chunk_idx = 0;
    while (offset < image_size) {
        uint16_t chunk_len = (uint16_t)((image_size - offset) < CHUNK_SIZE
                              ? (image_size - offset) : CHUNK_SIZE);

        for (int r = 0; r < RETRY_COUNT; r++) {
            rc = fwu_send_data(&h, chunk_idx,
                               image + offset, chunk_len);
            if (rc == 0) break;
            if (rc == NAK_SEQ) {
                fprintf(stderr, "[FWU] Sequence error at chunk %u\n",
                        chunk_idx);
                goto abort;
            }
            usleep(10000);
        }
        if (rc) {
            fprintf(stderr, "[FWU] Data chunk %u failed: %d\n",
                    chunk_idx, rc);
            goto abort;
        }

        offset     += chunk_len;
        chunk_idx  += 1;

        if (chunk_idx % 32 == 0) {
            printf("[FWU] Progress: %zu/%zu bytes (%.1f%%)\n",
                   offset, image_size,
                   100.0 * (double)offset / (double)image_size);
        }
    }

    /* 5. CMD_END */
    rc = fwu_end(&h);
    if (rc) { fprintf(stderr, "[FWU] CMD_END failed: %d\n", rc); goto abort; }

    /* 6. CMD_VERIFY */
    printf("[FWU] Requesting CRC verification...\n");
    rc = fwu_verify(&h);
    if (rc) { fprintf(stderr, "[FWU] Verify failed: %d\n", rc); goto abort; }

    /* 7. CMD_APPLY — target will reboot after this */
    printf("[FWU] Applying firmware and rebooting target...\n");
    rc = fwu_apply(&h);
    if (rc == 0) {
        printf("[FWU] Update successful!\n");
    } else {
        fprintf(stderr, "[FWU] Apply failed: %d\n", rc);
    }
    goto done;

abort:
    fwu_abort(&h);
    fprintf(stderr, "[FWU] Update aborted.\n");

done:
    i2c_close(&h);
    return rc;
}
```

---

### 4. Target-Side Bootloader (Bare-Metal C, e.g., STM32)

```c
// bootloader_target.c  –  runs on the target microcontroller
#include <stdint.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "fwupdate_protocol.h"
#include "crc16.h"

/* ---- Flash layout ---- */
#define APP_START_ADDR      0x08005000UL
#define STAGING_START_ADDR  0x08040000UL
#define STAGING_FLASH_PAGES 59          // number of 4KB pages in staging area

/* ---- I2C RX/TX buffers ---- */
#define I2C_RX_BUF_SIZE     (FWU_MAX_PACKET + 4U)
static uint8_t  rx_buf[I2C_RX_BUF_SIZE];
static uint8_t  tx_byte;

/* ---- Bootloader state ---- */
static volatile bl_state_t  bl_state    = BL_STATE_IDLE;
static uint32_t             expected_size;
static uint32_t             expected_crc32;
static uint16_t             expected_chunk_size;
static uint32_t             bytes_written;
static uint16_t             next_seq;
static uint32_t             running_crc;

extern I2C_HandleTypeDef hi2c1;

/* ---- Flash helpers ---- */
static int flash_erase_staging(void) {
    FLASH_EraseInitTypeDef erase;
    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector       = FLASH_SECTOR_2;   // Adjust to your layout
    erase.NbSectors    = 4;

    uint32_t error_sector;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef s = HAL_FLASHEx_Erase(&erase, &error_sector);
    HAL_FLASH_Lock();
    return (s == HAL_OK) ? 0 : -1;
}

static int flash_write_word(uint32_t addr, uint32_t word) {
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef s = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                             addr, (uint64_t)word);
    HAL_FLASH_Lock();
    return (s == HAL_OK) ? 0 : -1;
}

static int flash_write_chunk(uint32_t base_addr,
                              const uint8_t *data, uint16_t len) {
    // Write word-aligned chunks
    uint32_t addr = base_addr;
    for (uint16_t i = 0; i < len; i += 4) {
        uint32_t word = 0xFFFFFFFFUL;
        uint16_t remaining = len - i;
        uint8_t bytes = (remaining < 4) ? remaining : 4;
        memcpy(&word, &data[i], bytes);
        if (flash_write_word(addr, word) != 0) return -1;
        addr += 4;
    }
    return 0;
}

/* ---- Packet parser & dispatcher ---- */
static uint8_t process_packet(const uint8_t *raw, uint16_t raw_len) {
    if (raw_len < FWU_HEADER_SIZE + FWU_CRC_SIZE) return NAK_LEN;

    uint8_t  cmd = raw[0];
    uint16_t seq = (uint16_t)(raw[1] | (raw[2] << 8));
    uint16_t len = (uint16_t)(raw[3] | (raw[4] << 8));

    if (len > FWU_MAX_PAYLOAD) return NAK_LEN;

    // Verify CRC
    uint16_t pkt_crc  = (uint16_t)(raw[FWU_HEADER_SIZE + len] |
                         (raw[FWU_HEADER_SIZE + len + 1] << 8));
    uint16_t calc_crc = crc16_buffer(raw, FWU_HEADER_SIZE + len);
    if (pkt_crc != calc_crc) return NAK_CRC;

    const uint8_t *payload = &raw[FWU_HEADER_SIZE];

    switch (cmd) {

    case CMD_PING:
        return ACK;

    case CMD_START: {
        if (bl_state != BL_STATE_IDLE) return NAK_BUSY;
        if (len < sizeof(fwu_start_payload_t)) return NAK_LEN;

        const fwu_start_payload_t *sp = (const fwu_start_payload_t *)payload;
        expected_size       = sp->image_size;
        expected_crc32      = sp->image_crc32;
        expected_chunk_size = sp->chunk_size;

        if (flash_erase_staging() != 0) return NAK_FLASH;

        bytes_written = 0;
        next_seq      = 0;
        running_crc   = 0xFFFFFFFFUL;
        bl_state      = BL_STATE_RECEIVING;
        return ACK;
    }

    case CMD_DATA: {
        if (bl_state != BL_STATE_RECEIVING) return NAK_BUSY;
        if (seq != next_seq) return NAK_SEQ;

        uint32_t write_addr = STAGING_START_ADDR + bytes_written;
        if (flash_write_chunk(write_addr, payload, len) != 0)
            return NAK_FLASH;

        // Update running CRC-32
        for (uint16_t i = 0; i < len; i++) {
            running_crc ^= payload[i];
            for (int b = 0; b < 8; b++) {
                running_crc = (running_crc & 1)
                              ? (running_crc >> 1) ^ 0xEDB88320UL
                              : (running_crc >> 1);
            }
        }

        bytes_written += len;
        next_seq++;
        return ACK;
    }

    case CMD_END:
        if (bl_state != BL_STATE_RECEIVING) return NAK_BUSY;
        if (bytes_written != expected_size) return NAK_LEN;
        bl_state = BL_STATE_VERIFYING;
        return ACK;

    case CMD_VERIFY: {
        if (bl_state != BL_STATE_VERIFYING) return NAK_BUSY;
        uint32_t final_crc = running_crc ^ 0xFFFFFFFFUL;
        if (final_crc != expected_crc32) {
            bl_state = BL_STATE_ERROR;
            return NAK_CRC;
        }
        bl_state = BL_STATE_VERIFIED;
        return ACK;
    }

    case CMD_APPLY: {
        if (bl_state != BL_STATE_VERIFIED) return NAK_BUSY;
        bl_state = BL_STATE_APPLYING;
        // Copy staging → application region, then reboot
        // (In practice this is done after sending ACK)
        return ACK;
    }

    case CMD_ABORT:
        bl_state = BL_STATE_IDLE;
        return ACK;

    case CMD_STATUS:
        return (uint8_t)bl_state;

    default:
        return NAK_UNKNOWN;
    }
}

/* ---- I2C interrupt callbacks (HAL) ---- */
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c,
                           uint8_t TransferDirection,
                           uint16_t AddrMatchCode) {
    (void)AddrMatchCode;
    if (TransferDirection == I2C_DIRECTION_TRANSMIT) {
        HAL_I2C_Slave_Seq_Receive_IT(hi2c, rx_buf,
                                      sizeof(rx_buf),
                                      I2C_FIRST_AND_LAST_FRAME);
    } else {
        tx_byte = process_packet(rx_buf, sizeof(rx_buf));
        HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &tx_byte, 1,
                                       I2C_FIRST_AND_LAST_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    tx_byte = process_packet(rx_buf,
                              FWU_HEADER_SIZE +
                              (uint16_t)(rx_buf[3] | (rx_buf[4] << 8)) +
                              FWU_CRC_SIZE);
}

/* ---- Main bootloader loop ---- */
void bootloader_run(void) {
    HAL_I2C_EnableListen_IT(&hi2c1);

    while (bl_state != BL_STATE_APPLYING) {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }

    // Copy staging area to application region
    // ... (platform-specific flash copy omitted for brevity) ...

    // Jump to application
    HAL_I2C_DeInit(&hi2c1);
    bootloader_jump_to_application(APP_START_ADDR);
}
```

---

## Rust Implementation

Rust is increasingly used in embedded contexts (via `embedded-hal`) and on Linux hosts (via `linux-embedded-hal`). Below are both roles.

### 1. Shared Protocol Types (no_std)

```rust
// protocol.rs  –  #![no_std] compatible
#![allow(dead_code)]

/// Command identifiers
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Command {
    Ping    = 0x01,
    Start   = 0x02,
    Data    = 0x03,
    End     = 0x04,
    Verify  = 0x05,
    Apply   = 0x06,
    Abort   = 0x07,
    Status  = 0x08,
    Version = 0x09,
}

/// Response codes returned by the target
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Response {
    Ack        = 0x00,
    NakCrc     = 0x01,
    NakSeq     = 0x02,
    NakLen     = 0x03,
    NakFlash   = 0x04,
    NakBusy    = 0x05,
    NakUnknown = 0xFF,
}

impl TryFrom<u8> for Response {
    type Error = u8;
    fn try_from(v: u8) -> Result<Self, Self::Error> {
        match v {
            0x00 => Ok(Response::Ack),
            0x01 => Ok(Response::NakCrc),
            0x02 => Ok(Response::NakSeq),
            0x03 => Ok(Response::NakLen),
            0x04 => Ok(Response::NakFlash),
            0x05 => Ok(Response::NakBusy),
            0xFF => Ok(Response::NakUnknown),
            other => Err(other),
        }
    }
}

/// Bootloader state (returned by CMD_STATUS)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum BootloaderState {
    Idle      = 0,
    Receiving = 1,
    Verifying = 2,
    Verified  = 3,
    Applying  = 4,
    Error     = 5,
}

pub const MAX_PAYLOAD: usize = 64;
pub const HEADER_SIZE: usize = 5;  // cmd(1) + seq(2) + len(2)
pub const CRC_SIZE:    usize = 2;

/// CRC-16/CCITT running computation
pub fn crc16_update(mut crc: u16, byte: u8) -> u16 {
    crc ^= (byte as u16) << 8;
    for _ in 0..8 {
        crc = if crc & 0x8000 != 0 {
            (crc << 1) ^ 0x1021
        } else {
            crc << 1
        };
    }
    crc
}

pub fn crc16_buf(data: &[u8]) -> u16 {
    data.iter().fold(0xFFFF_u16, |crc, &b| crc16_update(crc, b))
}

/// Serialize a command packet into `buf`. Returns the packet length.
pub fn build_packet(
    buf: &mut [u8; HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE],
    cmd: Command,
    seq: u16,
    payload: &[u8],
) -> Result<usize, &'static str> {
    if payload.len() > MAX_PAYLOAD {
        return Err("payload too large");
    }

    let plen = payload.len();
    buf[0] = cmd as u8;
    buf[1] = (seq & 0xFF) as u8;
    buf[2] = (seq >> 8) as u8;
    buf[3] = (plen & 0xFF) as u8;
    buf[4] = (plen >> 8) as u8;
    buf[5..5 + plen].copy_from_slice(payload);

    let crc = crc16_buf(&buf[..HEADER_SIZE + plen]);
    buf[HEADER_SIZE + plen]     = (crc & 0xFF) as u8;
    buf[HEADER_SIZE + plen + 1] = (crc >> 8) as u8;

    Ok(HEADER_SIZE + plen + CRC_SIZE)
}
```

---

### 2. Host-Side Driver (Linux, using `linux-embedded-hal`)

```rust
// fwupdate_host.rs
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;
use std::{thread, time::Duration, io};
use crate::protocol::*;

pub struct FwUpdateHost<I2C> {
    i2c: I2C,
    addr: u8,
    chunk_size: usize,
}

#[derive(Debug)]
pub enum FwUpdateError {
    I2c(String),
    ProtocolNak(Response),
    InvalidResponse(u8),
    ImageTooLarge,
    VerifyFailed,
}

impl<I2C: I2c> FwUpdateHost<I2C> {
    pub fn new(i2c: I2C, addr: u8, chunk_size: usize) -> Self {
        assert!(chunk_size <= MAX_PAYLOAD, "chunk_size exceeds MAX_PAYLOAD");
        FwUpdateHost { i2c, addr, chunk_size }
    }

    /// Send a packet and read back the 1-byte ACK/NAK response.
    fn send_command(
        &mut self,
        cmd: Command,
        seq: u16,
        payload: &[u8],
    ) -> Result<(), FwUpdateError> {
        let mut buf = [0u8; HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE];
        let pkt_len = build_packet(&mut buf, cmd, seq, payload)
            .map_err(|e| FwUpdateError::I2c(e.to_string()))?;

        self.i2c
            .write(self.addr, &buf[..pkt_len])
            .map_err(|e| FwUpdateError::I2c(format!("{:?}", e)))?;

        // Give the target time to process
        thread::sleep(Duration::from_millis(20));

        let mut resp = [0u8; 1];
        self.i2c
            .read(self.addr, &mut resp)
            .map_err(|e| FwUpdateError::I2c(format!("{:?}", e)))?;

        match Response::try_from(resp[0]) {
            Ok(Response::Ack) => Ok(()),
            Ok(nak) => Err(FwUpdateError::ProtocolNak(nak)),
            Err(v) => Err(FwUpdateError::InvalidResponse(v)),
        }
    }

    pub fn ping(&mut self) -> Result<(), FwUpdateError> {
        self.send_command(Command::Ping, 0, &[])
    }

    pub fn start_update(
        &mut self,
        image_size: u32,
        image_crc32: u32,
        version: [u8; 4],
    ) -> Result<(), FwUpdateError> {
        let mut payload = [0u8; 14];
        payload[0..4].copy_from_slice(&image_size.to_le_bytes());
        payload[4..8].copy_from_slice(&image_crc32.to_le_bytes());
        payload[8..10].copy_from_slice(&(self.chunk_size as u16).to_le_bytes());
        payload[10..14].copy_from_slice(&version);
        self.send_command(Command::Start, 0, &payload)
    }

    pub fn send_data_chunk(
        &mut self,
        seq: u16,
        chunk: &[u8],
    ) -> Result<(), FwUpdateError> {
        self.send_command(Command::Data, seq, chunk)
    }

    pub fn end_transfer(&mut self) -> Result<(), FwUpdateError> {
        self.send_command(Command::End, 0, &[])
    }

    pub fn verify(&mut self) -> Result<(), FwUpdateError> {
        self.send_command(Command::Verify, 0, &[])
    }

    pub fn apply(&mut self) -> Result<(), FwUpdateError> {
        self.send_command(Command::Apply, 0, &[])
    }

    pub fn abort(&mut self) -> Result<(), FwUpdateError> {
        self.send_command(Command::Abort, 0, &[])
    }

    /// High-level: perform a complete firmware update.
    pub fn perform_update(
        &mut self,
        image: &[u8],
        version: [u8; 4],
    ) -> Result<(), FwUpdateError> {
        println!("[FWU] Pinging bootloader...");
        self.ping()?;

        let image_crc32 = crc32_image(image);
        println!("[FWU] Image: {} bytes, CRC32=0x{:08X}", image.len(), image_crc32);

        println!("[FWU] Sending CMD_START...");
        self.start_update(image.len() as u32, image_crc32, version)?;

        let chunks: Vec<&[u8]> = image.chunks(self.chunk_size).collect();
        let total = chunks.len();

        for (i, chunk) in chunks.iter().enumerate() {
            let rc = self.send_data_chunk(i as u16, chunk);
            if let Err(e) = rc {
                eprintln!("[FWU] Chunk {} failed: {:?}", i, e);
                let _ = self.abort();
                return Err(e);
            }
            if i % 32 == 0 {
                println!("[FWU] Progress: {}/{} chunks ({:.1}%)",
                    i, total, 100.0 * i as f64 / total as f64);
            }
        }

        println!("[FWU] Sending CMD_END...");
        self.end_transfer()?;

        println!("[FWU] Verifying CRC...");
        self.verify()?;

        println!("[FWU] Applying firmware...");
        self.apply()?;

        println!("[FWU] Update complete!");
        Ok(())
    }
}

/// Simple CRC-32 (IEEE 802.3 polynomial)
fn crc32_image(data: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFF_u32;
    for &b in data {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xEDB8_8320
            } else {
                crc >> 1
            };
        }
    }
    crc ^ 0xFFFF_FFFF
}

/// Example main — load a .bin file and flash it to target 0x42 on /dev/i2c-1
pub fn example_main() -> Result<(), Box<dyn std::error::Error>> {
    use std::fs;
    let image = fs::read("firmware.bin")?;

    let i2c = I2cdev::new("/dev/i2c-1")?;
    let mut host = FwUpdateHost::new(i2c, 0x42, 32);
    host.perform_update(&image, [1, 2, 0, 0])?;
    Ok(())
}
```

---

### 3. Target-Side Bootloader (Embedded Rust, `no_std`)

```rust
// bootloader_target.rs  –  #![no_std], runs on e.g. nRF52 or STM32
#![no_std]
#![no_main]

use cortex_m_rt::entry;
use embedded_hal::i2c::{I2c, Operation};
use nrf52840_hal::{self as hal, pac, twis::Twis};
use crate::protocol::*;

const STAGING_BASE: u32 = 0x0004_0000;
const APP_BASE:     u32 = 0x0000_8000;

struct Bootloader {
    state:          BootloaderState,
    expected_size:  u32,
    expected_crc32: u32,
    chunk_size:     u16,
    bytes_written:  u32,
    next_seq:       u16,
    running_crc32:  u32,
}

impl Bootloader {
    fn new() -> Self {
        Bootloader {
            state:          BootloaderState::Idle,
            expected_size:  0,
            expected_crc32: 0,
            chunk_size:     32,
            bytes_written:  0,
            next_seq:       0,
            running_crc32:  0xFFFF_FFFF,
        }
    }

    /// Process one incoming packet and return the 1-byte response.
    fn handle_packet(&mut self, raw: &[u8]) -> u8 {
        if raw.len() < HEADER_SIZE + CRC_SIZE {
            return Response::NakLen as u8;
        }

        let cmd_byte = raw[0];
        let seq  = u16::from_le_bytes([raw[1], raw[2]]);
        let plen = u16::from_le_bytes([raw[3], raw[4]]) as usize;

        if HEADER_SIZE + plen + CRC_SIZE > raw.len() {
            return Response::NakLen as u8;
        }

        let payload_slice = &raw[HEADER_SIZE..HEADER_SIZE + plen];
        let recv_crc = u16::from_le_bytes([
            raw[HEADER_SIZE + plen],
            raw[HEADER_SIZE + plen + 1],
        ]);
        let calc_crc = crc16_buf(&raw[..HEADER_SIZE + plen]);
        if recv_crc != calc_crc {
            return Response::NakCrc as u8;
        }

        match cmd_byte {
            0x01 /* PING */ => Response::Ack as u8,

            0x02 /* START */ => {
                if self.state != BootloaderState::Idle {
                    return Response::NakBusy as u8;
                }
                if plen < 14 {
                    return Response::NakLen as u8;
                }
                self.expected_size  = u32::from_le_bytes(payload_slice[0..4].try_into().unwrap());
                self.expected_crc32 = u32::from_le_bytes(payload_slice[4..8].try_into().unwrap());
                self.chunk_size     = u16::from_le_bytes([payload_slice[8], payload_slice[9]]);
                self.bytes_written  = 0;
                self.next_seq       = 0;
                self.running_crc32  = 0xFFFF_FFFF;

                // Erase staging flash (platform-specific)
                if flash_erase(STAGING_BASE, self.expected_size).is_err() {
                    return Response::NakFlash as u8;
                }

                self.state = BootloaderState::Receiving;
                Response::Ack as u8
            }

            0x03 /* DATA */ => {
                if self.state != BootloaderState::Receiving {
                    return Response::NakBusy as u8;
                }
                if seq != self.next_seq {
                    return Response::NakSeq as u8;
                }
                let addr = STAGING_BASE + self.bytes_written;
                if flash_write(addr, payload_slice).is_err() {
                    return Response::NakFlash as u8;
                }
                // Update CRC
                for &b in payload_slice {
                    self.running_crc32 ^= b as u32;
                    for _ in 0..8 {
                        self.running_crc32 = if self.running_crc32 & 1 != 0 {
                            (self.running_crc32 >> 1) ^ 0xEDB8_8320
                        } else {
                            self.running_crc32 >> 1
                        };
                    }
                }
                self.bytes_written += plen as u32;
                self.next_seq       = self.next_seq.wrapping_add(1);
                Response::Ack as u8
            }

            0x04 /* END */ => {
                if self.state != BootloaderState::Receiving {
                    return Response::NakBusy as u8;
                }
                if self.bytes_written != self.expected_size {
                    return Response::NakLen as u8;
                }
                self.state = BootloaderState::Verifying;
                Response::Ack as u8
            }

            0x05 /* VERIFY */ => {
                if self.state != BootloaderState::Verifying {
                    return Response::NakBusy as u8;
                }
                let final_crc = self.running_crc32 ^ 0xFFFF_FFFF;
                if final_crc != self.expected_crc32 {
                    self.state = BootloaderState::Error;
                    return Response::NakCrc as u8;
                }
                self.state = BootloaderState::Verified;
                Response::Ack as u8
            }

            0x06 /* APPLY */ => {
                if self.state != BootloaderState::Verified {
                    return Response::NakBusy as u8;
                }
                self.state = BootloaderState::Applying;
                Response::Ack as u8
            }

            0x07 /* ABORT */ => {
                self.state = BootloaderState::Idle;
                Response::Ack as u8
            }

            0x08 /* STATUS */ => self.state as u8,

            _ => Response::NakUnknown as u8,
        }
    }
}

/// Platform-specific flash erase (stub — replace with HAL calls)
fn flash_erase(base: u32, size: u32) -> Result<(), ()> {
    // ... nvmc / flash peripheral calls ...
    Ok(())
}

/// Platform-specific flash write (stub — replace with HAL calls)
fn flash_write(addr: u32, data: &[u8]) -> Result<(), ()> {
    // ... nvmc / flash peripheral calls ...
    Ok(())
}

#[entry]
fn main() -> ! {
    let p = pac::Peripherals::take().unwrap();
    // ... configure clocks, I2C slave peripheral ...

    let mut bl = Bootloader::new();
    let mut rx  = [0u8; HEADER_SIZE + MAX_PAYLOAD + CRC_SIZE];

    loop {
        // Wait for I2C write from host, then process, then reply
        // (HAL TWIS event-driven loop — platform-specific)

        // Pseudocode:
        // if twis.events_write().read().events_write().bit() {
        //     let n = twis.rxd_amount();
        //     rx[..n].copy_from_slice(twis.read_rxd(n));
        //     let resp = bl.handle_packet(&rx[..n]);
        //     twis.queue_tx_byte(resp);
        // }

        if bl.state == BootloaderState::Applying {
            // Copy staging → app, then jump
            jump_to_application(APP_BASE);
        }
    }
}

fn jump_to_application(addr: u32) -> ! {
    use core::arch::asm;
    unsafe {
        let sp  = *(addr as *const u32);
        let pc  = *((addr + 4) as *const u32);
        asm!(
            "msr msp, {0}",
            "bx  {1}",
            in(reg) sp,
            in(reg) pc,
            options(noreturn)
        );
    }
}
```

---

## Error Handling & Recovery

Robust firmware update over I2C must survive partial failures without bricking the device.

### Dual-Bank / Staging Strategy

The target always receives the image into a **staging area** separate from the running application. The `CMD_APPLY` command triggers copying from staging to application. If power is lost during transfer, the staging area is corrupt but the application is untouched. A **metadata flag** in non-volatile memory records whether staging is valid before `CMD_APPLY` is issued.

```c
// Metadata structure written to flash before CMD_APPLY
typedef struct {
    uint32_t magic;         // 0xB007AB1E
    uint32_t staging_crc32;
    uint32_t staging_size;
    uint8_t  staging_valid; // 0x01 = valid staging, 0x00 = invalid
    uint8_t  apply_pending; // 0x01 = apply after next reset
    uint8_t  reserved[2];
} bl_metadata_t;
```

### Host-Side Retry Logic

```c
// With exponential back-off
int fwu_send_with_retry(i2c_handle_t *h,
                         uint8_t cmd, uint16_t seq,
                         const uint8_t *payload, uint16_t len,
                         int max_retries) {
    int delay_ms = 10;
    for (int attempt = 0; attempt < max_retries; attempt++) {
        int rc = fwu_send_command(h, cmd, seq, payload, len);
        if (rc == 0)         return 0;
        if (rc == NAK_SEQ)   return rc;   // Don't retry sequence errors
        if (rc == NAK_FLASH) return rc;   // Don't retry flash errors
        usleep(delay_ms * 1000UL);
        delay_ms *= 2;                    // Exponential back-off
        if (delay_ms > 2000) delay_ms = 2000;
    }
    return -ETIMEDOUT;
}
```

### Watchdog Considerations

The bootloader should kick the hardware watchdog during long flash erase operations (which can stall execution for hundreds of milliseconds):

```c
void flash_erase_with_wdg(uint32_t sector) {
    HAL_IWDG_Refresh(&hiwdg);   // Kick before erase
    // ... erase ...
    HAL_IWDG_Refresh(&hiwdg);   // Kick after erase
}
```

---

## Security Considerations

### Firmware Image Signing

For production deployments, each firmware image should be digitally signed. The bootloader verifies the signature before writing to flash. A common approach uses **Ed25519** (compact 64-byte signatures, 32-byte public key):

```
Image layout with signature:
┌──────────────────────────────────────────────────────┐
│  Firmware Header (64 bytes)                           │
│    magic[4] | version[4] | size[4] | crc32[4] | ...  │
├──────────────────────────────────────────────────────┤
│  Application Binary                                   │
├──────────────────────────────────────────────────────┤
│  Ed25519 Signature (64 bytes)                         │
└──────────────────────────────────────────────────────┘
```

The bootloader stores the **public key** in write-protected flash and refuses to apply any image whose signature fails verification.

### Rollback Protection

Maintain a **version counter** in OTP (One-Time Programmable) memory or an incrementing fuse register. The bootloader rejects images with a version number lower than the current fuse value:

```c
bool bootloader_version_ok(uint32_t new_version) {
    uint32_t min_version = otp_read_min_version();
    return new_version >= min_version;
}
```

### I2C Bus Security Notes

- I2C has **no built-in authentication**. Any device with physical access to the bus can attempt an update.
- Use a **challenge-response handshake** (e.g., HMAC-SHA256) at the start of the `CMD_START` sequence in security-sensitive applications.
- Consider **encrypted image delivery** (AES-128-CTR is lightweight enough for constrained targets).

---

## Testing Strategies

### Unit Testing the Protocol Layer (C)

```c
// test_protocol.c (runs on host, uses stub I2C)
#include <assert.h>
#include "fwupdate_protocol.h"
#include "crc16.h"

static uint8_t stub_rx_buf[FWU_MAX_PACKET];
static uint8_t stub_tx_byte;

void test_crc16(void) {
    uint8_t data[] = {0x01, 0x00, 0x00, 0x00, 0x00};
    uint16_t crc = crc16_buffer(data, sizeof(data));
    assert(crc != 0);  // CRC of any non-trivial input must be non-zero

    // Check idempotency: same data → same CRC
    assert(crc == crc16_buffer(data, sizeof(data)));
    printf("test_crc16: PASS\n");
}

void test_packet_corruption(void) {
    uint8_t pkt[FWU_MAX_PACKET] = {0};
    pkt[0] = CMD_PING;
    // Deliberately corrupt CRC
    pkt[FWU_HEADER_SIZE]   = 0xDE;
    pkt[FWU_HEADER_SIZE+1] = 0xAD;

    uint8_t resp = process_packet(pkt, FWU_HEADER_SIZE + FWU_CRC_SIZE);
    assert(resp == NAK_CRC);
    printf("test_packet_corruption: PASS\n");
}

int main(void) {
    test_crc16();
    test_packet_corruption();
    return 0;
}
```

### Hardware-in-the-Loop Test

Use a Raspberry Pi (or second MCU) as host and connect it to the DUT via I2C. Automate the following scenarios:

| Scenario                        | Expected Result                        |
|---------------------------------|----------------------------------------|
| Normal full update              | ACK all commands, new firmware boots   |
| CRC corruption in chunk         | NAK_CRC on affected chunk; retry succeeds |
| Power loss during receive       | On reboot: app unchanged, bootloader idle |
| Power loss during apply         | Metadata flag triggers re-copy on boot |
| Downgrade attempt (sig. invalid)| CMD_APPLY returns NAK                 |
| I2C clock stretch tolerance     | Update completes even at 10 kHz        |

---

## Summary

Firmware Update over I2C is a practical and cost-effective field upgrade mechanism for embedded systems. The key design principles are:

**Reliability** — Use double-buffering (staging area) and persistent metadata flags so that any power interruption during transfer leaves the running application intact. The bootloader should never overwrite the application until the staging area has been fully received and independently verified.

**Integrity** — Protect every packet with CRC-16 at the transport layer and verify the entire image with CRC-32 (or stronger) before applying. Sequence numbers prevent lost or reordered chunks from silently corrupting the image.

**Security** — Raw I2C provides no authentication. Production firmware update pipelines should sign images with Ed25519 or ECDSA, verify signatures inside the bootloader against a stored public key, and implement rollback protection via OTP fuse counters.

**Robustness** — The host must implement retry logic with back-off, explicit abort on unrecoverable errors, and progress reporting. The target bootloader must run with the watchdog active and kick it during long flash erase operations.

**Footprint** — A minimal I2C bootloader (CRC engine, flash writer, state machine, I2C slave ISR) can fit comfortably in 8–16 KB of flash, making it viable even on the smallest microcontrollers.

The C/C++ examples above are suitable for bare-metal MCUs using HAL libraries (STM32, NXP, Microchip), while the Rust examples leverage `embedded-hal` traits for portability across the growing ecosystem of Rust-supported targets such as nRF52, RP2040, and STM32.

---

*Document: 89_Firmware_Update_Over_I2C.md — Part of the I2C Programming Reference Series*