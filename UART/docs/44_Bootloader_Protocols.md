# 44. Bootloader Protocols — Firmware Update Mechanisms over UART

1. **Introduction** — why bootloaders matter and where UART fits in embedded update workflows.
2. **What Is a Bootloader?** — the 5-step lifecycle (init → boot decision → receive → verify → jump), with a flash memory map diagram.
3. **Why UART?** — comparison table against SPI, I2C, and USB DFU.
4. **Common Protocols** — XMODEM/XMODEM-CRC/XMODEM-1K (frame structure, control bytes), YMODEM/YMODEM-G, ZMODEM, the STM32 ROM bootloader command set, and custom binary framing.
5. **Protocol Architecture** — host ↔ target handshake flow diagram.
6. **C/C++ Code** — four complete examples: XMODEM receiver (target MCU), XMODEM sender (host), STM32 custom bootloader entry with jump-to-app, and STM32F4 flash erase/write HAL.
7. **Rust Code** — three examples: `no_std` XMODEM receiver using `embedded-hal` traits, a host-side CLI flasher using the `serialport` crate, and a custom framed protocol parser/state machine.
8. **Security** — threat/mitigation table (signature, encryption, anti-rollback, replay protection) plus a secure firmware header layout.
9. **Practical Design Checklist** — 10 production checklist items (watchdog, alignment, A/B partitions, etc.).
10. **Summary** — concise recap of protocol selection, C vs Rust trade-offs, security requirements, and flash safety.

---

## Table of Contents

1. [Introduction](#introduction)
2. [What Is a Bootloader?](#what-is-a-bootloader)
3. [Why UART for Firmware Updates?](#why-uart-for-firmware-updates)
4. [Common Bootloader Protocols](#common-bootloader-protocols)
   - [XMODEM / XMODEM-CRC / XMODEM-1K](#xmodem)
   - [YMODEM / YMODEM-G](#ymodem)
   - [ZMODEM](#zmodem)
   - [STM32 UART Bootloader (AN2606)](#stm32-uart-bootloader)
   - [Custom Binary Protocols](#custom-binary-protocols)
5. [Protocol Architecture](#protocol-architecture)
6. [Programming in C/C++](#programming-in-cc)
   - [XMODEM Receiver (Target Side)](#xmodem-receiver-target-side-c)
   - [XMODEM Sender (Host Side)](#xmodem-sender-host-side-c)
   - [STM32 Custom Bootloader Example](#stm32-custom-bootloader-c)
   - [Flash Write Helper](#flash-write-helper-c)
7. [Programming in Rust](#programming-in-rust)
   - [XMODEM Receiver in Rust](#xmodem-receiver-in-rust)
   - [Host-Side Firmware Flasher in Rust](#host-side-firmware-flasher-in-rust)
   - [Custom Framed Protocol in Rust](#custom-framed-protocol-in-rust)
8. [Security Considerations](#security-considerations)
9. [Practical Design Checklist](#practical-design-checklist)
10. [Summary](#summary)

---

## Introduction

Bootloader protocols over UART define how a microcontroller or embedded system receives new firmware images from a host (PC, another MCU, or a field update device) without requiring a hardware debugger. This capability is essential for:

- Production programming on the factory floor
- In-field firmware updates (OTA-like, but wired)
- Disaster recovery when the main application is corrupted
- Development iteration without a JTAG/SWD probe

UART remains one of the most universal interfaces for bootloading because it is available on virtually every microcontroller, requires only two signal wires (TX/RX), and is well understood.

---

## What Is a Bootloader?

A **bootloader** is a small, trusted piece of firmware that executes first on power-up or reset. Its responsibilities are:

1. **Hardware initialisation** — clocks, UART peripheral, optional watchdog.
2. **Boot decision** — check whether an update is requested (via a GPIO, a flag in non-volatile memory, a magic byte received over UART, etc.).
3. **Firmware reception** — receive the new image via a defined wire protocol.
4. **Integrity verification** — CRC, checksum, or cryptographic signature check.
5. **Flash programming** — erase and write pages/sectors to the application area.
6. **Jump to application** — once the image is valid, transfer execution.

```
┌─────────────────────────────────────────────────────────┐
│                    FLASH MEMORY MAP                     │
├──────────────┬──────────────────────┬───────────────────┤
│  Bootloader  │   Application Area   │  Metadata / CRC   │
│  (0x08000000)│  (0x08004000 ...)    │  (top of flash)   │
└──────────────┴──────────────────────┴───────────────────┘
```

---

## Why UART for Firmware Updates?

| Feature          | UART | SPI | I2C | USB DFU |
|------------------|------|-----|-----|---------|
| Pin count        | 2    | 4   | 2   | 2       |
| Speed (typical)  | up to 4 Mbaud | up to 50 MHz | up to 1 Mbit | 12 Mbit (FS) |
| Complexity       | Low  | Medium | Low | High  |
| Universal MCU support | ✅ | ✅ | ✅ | ❌ |
| Long cable runs  | ✅ (RS-232/485) | ❌ | ❌ | ❌ |
| Human-readable debug | ✅ | ❌ | ❌ | ❌ |

UART shines in constrained environments and long-distance scenarios (RS-232/RS-485 variants).

---

## Common Bootloader Protocols

### XMODEM

**XMODEM** (1977, Ward Christensen) is the simplest and most widely supported transfer protocol. Three variants exist:

| Variant       | Block size | Error detection | Notes                     |
|---------------|------------|-----------------|---------------------------|
| XMODEM        | 128 bytes  | 8-bit checksum  | Original, avoid today     |
| XMODEM-CRC    | 128 bytes  | CRC-16/CCITT    | Preferred minimal variant |
| XMODEM-1K     | 1024 bytes | CRC-16          | Higher throughput         |

**Frame structure (XMODEM-CRC):**

```
┌──────┬────────────┬──────────────────┬──────────┬──────────┐
│ SOH  │ Block Num  │ ~Block Num (inv) │ 128 data │ CRC-16   │
│ 0x01 │ 0x01..0xFF │ 0xFE..0x00       │ bytes    │ 2 bytes  │
└──────┴────────────┴──────────────────┴──────────┴──────────┘
```

Control bytes:

| Symbol | Value | Meaning                  |
|--------|-------|--------------------------|
| SOH    | 0x01  | Start of 128-byte block  |
| STX    | 0x02  | Start of 1K block        |
| EOT    | 0x04  | End of transmission      |
| ACK    | 0x06  | Acknowledge (ok)         |
| NAK    | 0x15  | Negative ack (retry)     |
| CAN    | 0x18  | Cancel transfer          |
| 'C'    | 0x43  | Receiver requests CRC mode|

---

### YMODEM

**YMODEM** extends XMODEM with:
- A block 0 that carries the **filename and file size** as ASCII.
- Batch mode (multiple files in one session).
- 1K blocks by default.

**YMODEM-G** removes ACK/NAK handshaking for maximum speed on error-free hardware links (uses hardware flow control instead).

---

### ZMODEM

**ZMODEM** (1986) adds:
- Streaming with selective retransmit
- Crash recovery (resume interrupted transfer)
- Automatic start on detection

Used more in modem/BBS era; less common in embedded bootloaders today.

---

### STM32 UART Bootloader

ST Microelectronics provides a factory-programmed ROM bootloader on most STM32 devices. It is activated by pulling `BOOT0` high on reset and uses a specific command set:

| Command | Code  | Description              |
|---------|-------|--------------------------|
| Get     | 0x00  | Get supported commands   |
| GetID   | 0x02  | Get chip ID              |
| ReadMem | 0x11  | Read memory              |
| Go      | 0x21  | Jump to address          |
| WriteMem| 0x31  | Write memory             |
| Erase   | 0x43/0x44 | Erase pages          |
| WriteProtect/Unprotect | 0x63/0x73 | Flash protection |

Every command byte is sent as `CMD, ~CMD` (command and its complement for verification). Responses use `0x79` (ACK) and `0x1F` (NACK).

---

### Custom Binary Protocols

Most production bootloaders use a **custom framed protocol** with features tailored to the hardware:

```
┌────────┬────────┬──────────┬──────────────┬─────────┐
│ SYNC   │ CMD    │ LENGTH   │ PAYLOAD      │ CRC/CHK │
│ 0xAA55 │ 1 byte │ 2 bytes  │ 0..N bytes   │ 2 bytes │
└────────┴────────┴──────────┴──────────────┴─────────┘
```

Commands typically include: `CMD_START`, `CMD_DATA`, `CMD_END`, `CMD_VERIFY`, `CMD_RESET`.

---

## Protocol Architecture

```
HOST (PC / Master MCU)                TARGET (Bootloader)
─────────────────────                 ───────────────────
1. Open serial port
2. Assert update request              → GPIO or magic byte
3. Send protocol initiation           ← 'C' or NAK (XMODEM)
4. Loop: send data frames             ← ACK / NAK
5. Send EOT                           ← ACK
6. Verify CRC over flash              ← status response
7. Send JUMP / RESET command          → jump to app
```

---

## Programming in C/C++

### XMODEM Receiver (Target Side) C

```c
/* xmodem_receiver.c — runs on the microcontroller bootloader */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define XMODEM_SOH  0x01
#define XMODEM_EOT  0x04
#define XMODEM_ACK  0x06
#define XMODEM_NAK  0x15
#define XMODEM_CAN  0x18
#define XMODEM_CRC_REQ 'C'

#define XMODEM_BLOCK_SIZE   128
#define XMODEM_RETRIES      10
#define XMODEM_TIMEOUT_MS   3000

/* Platform hooks — implement for your MCU */
extern void     uart_putc(uint8_t c);
extern int      uart_getc_timeout(uint8_t *c, uint32_t timeout_ms);
extern bool     flash_write(uint32_t address, const uint8_t *data, uint32_t length);
extern uint32_t flash_get_app_base(void);
extern void     flash_erase_app_region(void);

/* CRC-16/CCITT (poly 0x1021, init 0x0000) */
static uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
        else
            crc <<= 1;
    }
    return crc;
}

static uint16_t crc16_buf(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0;
    while (len--) crc = crc16_update(crc, *buf++);
    return crc;
}

typedef enum {
    XMODEM_OK = 0,
    XMODEM_ERR_TIMEOUT,
    XMODEM_ERR_SEQ,
    XMODEM_ERR_CRC,
    XMODEM_ERR_CANCELLED,
    XMODEM_ERR_FLASH,
} xmodem_err_t;

xmodem_err_t xmodem_receive(void)
{
    uint8_t  buf[XMODEM_BLOCK_SIZE + 5]; /* SOH + seq + ~seq + data + CRC(2) */
    uint8_t  expected_seq = 1;
    uint32_t write_addr   = flash_get_app_base();
    uint8_t  byte;
    int      retries;

    flash_erase_app_region();

    /* Initiate CRC mode: send 'C' until we get SOH */
    for (retries = 0; retries < XMODEM_RETRIES; retries++) {
        uart_putc(XMODEM_CRC_REQ);
        if (uart_getc_timeout(&byte, XMODEM_TIMEOUT_MS) == 0) {
            if (byte == XMODEM_SOH) goto got_soh;
        }
    }
    return XMODEM_ERR_TIMEOUT;

got_soh:
    for (;;) {
        if (byte == XMODEM_EOT) {
            uart_putc(XMODEM_ACK);
            return XMODEM_OK;
        }
        if (byte == XMODEM_CAN) {
            return XMODEM_ERR_CANCELLED;
        }
        if (byte != XMODEM_SOH) {
            uart_putc(XMODEM_NAK);
            goto next_block;
        }

        /* Read remaining frame bytes */
        for (int i = 1; i < (int)(sizeof(buf)); i++) {
            if (uart_getc_timeout(&buf[i], XMODEM_TIMEOUT_MS) != 0) {
                uart_putc(XMODEM_NAK);
                goto next_block;
            }
        }
        buf[0] = byte;

        /* Verify sequence numbers */
        uint8_t seq  = buf[1];
        uint8_t nseq = buf[2];
        if ((seq + nseq) != 0xFF) {
            uart_putc(XMODEM_NAK);
            goto next_block;
        }
        if (seq != expected_seq) {
            if (seq == (uint8_t)(expected_seq - 1)) {
                /* Duplicate block — ACK but don't write */
                uart_putc(XMODEM_ACK);
                goto next_block;
            }
            uart_putc(XMODEM_CAN);
            return XMODEM_ERR_SEQ;
        }

        /* Verify CRC */
        uint16_t rcv_crc = ((uint16_t)buf[3 + XMODEM_BLOCK_SIZE] << 8)
                         |  buf[3 + XMODEM_BLOCK_SIZE + 1];
        uint16_t calc_crc = crc16_buf(&buf[3], XMODEM_BLOCK_SIZE);
        if (rcv_crc != calc_crc) {
            uart_putc(XMODEM_NAK);
            goto next_block;
        }

        /* Write to flash */
        if (!flash_write(write_addr, &buf[3], XMODEM_BLOCK_SIZE)) {
            uart_putc(XMODEM_CAN);
            return XMODEM_ERR_FLASH;
        }

        write_addr  += XMODEM_BLOCK_SIZE;
        expected_seq++;
        uart_putc(XMODEM_ACK);

next_block:
        if (uart_getc_timeout(&byte, XMODEM_TIMEOUT_MS) != 0)
            return XMODEM_ERR_TIMEOUT;
    }
}
```

---

### XMODEM Sender (Host Side) C

```c
/* xmodem_sender.c — runs on the host (PC or master MCU) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define XMODEM_SOH  0x01
#define XMODEM_EOT  0x04
#define XMODEM_ACK  0x06
#define XMODEM_NAK  0x15
#define XMODEM_CAN  0x18
#define XMODEM_BLOCK_SIZE 128

extern void     serial_putc(uint8_t c);
extern int      serial_getc_timeout(uint8_t *c, uint32_t ms);

static uint16_t crc16_buf(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0;
    while (len--) {
        crc ^= (uint16_t)(*buf++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

int xmodem_send(const uint8_t *data, uint32_t length)
{
    uint8_t  block[XMODEM_BLOCK_SIZE + 5];
    uint8_t  seq = 1;
    uint8_t  resp;
    uint32_t offset = 0;
    int      retries;

    /* Wait for 'C' (CRC mode request) */
    for (retries = 0; retries < 60; retries++) {
        if (serial_getc_timeout(&resp, 1000) == 0 && resp == 'C')
            break;
    }
    if (resp != 'C') return -1;

    while (offset < length) {
        /* Build block */
        uint32_t chunk = (length - offset < XMODEM_BLOCK_SIZE)
                       ? length - offset : XMODEM_BLOCK_SIZE;

        block[0] = XMODEM_SOH;
        block[1] = seq;
        block[2] = (uint8_t)(~seq);
        memcpy(&block[3], data + offset, chunk);
        /* Pad last block with 0x1A (CTRL-Z) if < 128 bytes */
        if (chunk < XMODEM_BLOCK_SIZE)
            memset(&block[3 + chunk], 0x1A, XMODEM_BLOCK_SIZE - chunk);

        uint16_t crc = crc16_buf(&block[3], XMODEM_BLOCK_SIZE);
        block[3 + XMODEM_BLOCK_SIZE]     = (uint8_t)(crc >> 8);
        block[3 + XMODEM_BLOCK_SIZE + 1] = (uint8_t)(crc & 0xFF);

        for (retries = 0; retries < 10; retries++) {
            for (int i = 0; i < (int)sizeof(block); i++)
                serial_putc(block[i]);

            if (serial_getc_timeout(&resp, 5000) == 0 && resp == XMODEM_ACK)
                break;
        }
        if (retries == 10) return -1;

        offset += XMODEM_BLOCK_SIZE;
        seq++;
    }

    /* End of transmission */
    for (retries = 0; retries < 10; retries++) {
        serial_putc(XMODEM_EOT);
        if (serial_getc_timeout(&resp, 2000) == 0 && resp == XMODEM_ACK)
            return 0;
    }
    return -1;
}
```

---

### STM32 Custom Bootloader C

```c
/* bootloader_main.c — STM32 custom UART bootloader entry */
#include "stm32f4xx_hal.h"
#include <string.h>

#define APP_START_ADDRESS   0x08008000UL  /* After 32 KB bootloader */
#define BOOT_MAGIC_ADDR     0x20000000UL  /* First SRAM word */
#define BOOT_MAGIC_VALUE    0xDEADBEEFUL
#define UART_TIMEOUT_MS     5000

typedef void (*app_entry_t)(void);

/* Check if we should stay in bootloader */
static bool should_update(void)
{
    /* Option 1: magic word in SRAM (set by app before reset) */
    if (*(volatile uint32_t *)BOOT_MAGIC_ADDR == BOOT_MAGIC_VALUE) {
        *(volatile uint32_t *)BOOT_MAGIC_ADDR = 0;
        return true;
    }
    /* Option 2: BOOT1 pin held high */
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_2) == GPIO_PIN_SET)
        return true;

    /* Option 3: Application vector table looks invalid */
    uint32_t app_sp = *(volatile uint32_t *)APP_START_ADDRESS;
    if (app_sp < 0x20000000UL || app_sp > 0x20020000UL)
        return true;

    return false;
}

/* Jump to application */
static void jump_to_app(void)
{
    uint32_t app_sp = *(volatile uint32_t *)(APP_START_ADDRESS);
    uint32_t app_pc = *(volatile uint32_t *)(APP_START_ADDRESS + 4);

    /* Disable all interrupts, reset peripherals */
    __disable_irq();
    HAL_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* Remap vector table */
    SCB->VTOR = APP_START_ADDRESS;

    /* Set stack pointer and jump */
    __set_MSP(app_sp);
    app_entry_t entry = (app_entry_t)app_pc;
    entry();
}

/* Simple 32-bit CRC for flash verification */
static uint32_t crc32_flash(uint32_t start, uint32_t length)
{
    CRC->CR = CRC_CR_RESET;
    volatile uint32_t *p = (volatile uint32_t *)start;
    for (uint32_t i = 0; i < length / 4; i++)
        CRC->DR = p[i];
    return CRC->DR;
}

void bootloader_main(void)
{
    HAL_Init();
    SystemClock_Config();   /* project-specific */
    MX_UART1_Init();        /* 115200 8N1 */
    MX_CRC_Init();

    if (!should_update()) {
        jump_to_app();
        /* never returns */
    }

    /* Signal ready for XMODEM */
    uart_puts("BL> Ready. Send firmware via XMODEM-CRC.\r\n");

    xmodem_err_t err = xmodem_receive();

    if (err == XMODEM_OK) {
        /* Verify flash CRC matches expected (stored at end of image) */
        uint32_t stored_crc = *(volatile uint32_t *)(APP_START_ADDRESS + 0x1FFFC);
        uint32_t calc_crc   = crc32_flash(APP_START_ADDRESS, 0x1FFFC);
        if (stored_crc == calc_crc) {
            uart_puts("BL> Update OK. Rebooting...\r\n");
            HAL_Delay(100);
            HAL_NVIC_SystemReset();
        } else {
            uart_puts("BL> CRC mismatch! Firmware rejected.\r\n");
        }
    } else {
        uart_puts("BL> Transfer failed.\r\n");
    }

    /* Fall into error loop */
    for (;;) HAL_Delay(1000);
}
```

---

### Flash Write Helper C

```c
/* flash_hal.c — STM32F4 flash erase + write */
#include "stm32f4xx_hal.h"

#define APP_FLASH_SECTOR    FLASH_SECTOR_2   /* sector 2 onward for app */
#define APP_FLASH_VOLTAGE   FLASH_VOLTAGE_RANGE_3

bool flash_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    HAL_FLASH_Unlock();

    /* Write byte by byte (or word-by-word for speed) */
    for (uint32_t i = 0; i < length; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                              address + i,
                              data[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
    }
    HAL_FLASH_Lock();
    return true;
}

void flash_erase_app_region(void)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = APP_FLASH_VOLTAGE,
        .Sector       = APP_FLASH_SECTOR,
        .NbSectors    = 6,   /* erase sectors 2-7 */
    };
    uint32_t sector_error;
    HAL_FLASHEx_Erase(&erase, &sector_error);

    HAL_FLASH_Lock();
}
```

---

## Programming in Rust

Rust is increasingly used in embedded systems. The `embedded-hal` trait ecosystem enables portable drivers, and the `serialport` crate powers host-side tooling.

### XMODEM Receiver in Rust

```rust
// xmodem_receiver.rs — no_std, runs on microcontroller
#![no_std]

use embedded_hal::serial::{Read, Write};
use core::fmt;

const SOH:  u8 = 0x01;
const EOT:  u8 = 0x04;
const ACK:  u8 = 0x06;
const NAK:  u8 = 0x15;
const CAN:  u8 = 0x18;
const CRC_C: u8 = b'C';
const BLOCK_SIZE: usize = 128;

#[derive(Debug)]
pub enum XmodemError {
    Timeout,
    BadSequence,
    CrcMismatch,
    Cancelled,
    FlashError,
}

fn crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0;
    for &byte in data {
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ 0x1021
            } else {
                crc << 1
            };
        }
    }
    crc
}

pub struct XmodemReceiver<UART, FLASH> {
    uart:       UART,
    flash:      FLASH,
    write_addr: u32,
}

impl<UART, FLASH> XmodemReceiver<UART, FLASH>
where
    UART:  Read<u8> + Write<u8>,
    FLASH: FlashWrite,
{
    pub fn new(uart: UART, flash: FLASH, base_address: u32) -> Self {
        Self { uart, flash, write_addr: base_address }
    }

    fn uart_send(&mut self, byte: u8) {
        // Ignore write errors on simple UART
        let _ = nb::block!(self.uart.write(byte));
    }

    fn uart_recv_timeout(&mut self, timeout_ticks: u32)
        -> Result<u8, XmodemError>
    {
        let mut ticks = 0u32;
        loop {
            match self.uart.read() {
                Ok(b)                           => return Ok(b),
                Err(nb::Error::WouldBlock)      => {},
                Err(nb::Error::Other(_))        => return Err(XmodemError::Timeout),
            }
            ticks += 1;
            if ticks >= timeout_ticks {
                return Err(XmodemError::Timeout);
            }
            // Platform delay: cortex_m::asm::nop() x N or timer check
        }
    }

    pub fn receive(&mut self) -> Result<u32, XmodemError> {
        const TIMEOUT: u32 = 3_000_000; // tune for your clock / baud
        let mut expected_seq: u8 = 1;
        let mut bytes_written: u32 = 0;

        // Initiate CRC mode
        for _ in 0..10 {
            self.uart_send(CRC_C);
            if let Ok(b) = self.uart_recv_timeout(TIMEOUT) {
                if b == SOH {
                    return self.receive_loop(b, &mut expected_seq,
                                             &mut bytes_written, TIMEOUT);
                }
            }
        }
        Err(XmodemError::Timeout)
    }

    fn receive_loop(
        &mut self,
        first_byte: u8,
        expected_seq: &mut u8,
        bytes_written: &mut u32,
        timeout: u32,
    ) -> Result<u32, XmodemError> {
        let mut byte = first_byte;
        let mut buf = [0u8; BLOCK_SIZE + 4]; // seq + ~seq + data + crc(2)

        loop {
            match byte {
                EOT => {
                    self.uart_send(ACK);
                    return Ok(*bytes_written);
                }
                CAN => return Err(XmodemError::Cancelled),
                SOH => {
                    // Read rest of frame
                    for i in 0..buf.len() {
                        buf[i] = self.uart_recv_timeout(timeout)?;
                    }

                    let seq  = buf[0];
                    let nseq = buf[1];

                    if seq.wrapping_add(nseq) != 0xFF {
                        self.uart_send(NAK);
                    } else if seq != *expected_seq {
                        self.uart_send(CAN);
                        return Err(XmodemError::BadSequence);
                    } else {
                        let data    = &buf[2..2 + BLOCK_SIZE];
                        let rcv_crc = ((buf[2 + BLOCK_SIZE] as u16) << 8)
                                    |  buf[2 + BLOCK_SIZE + 1] as u16;
                        let calc_crc = crc16(data);

                        if rcv_crc != calc_crc {
                            self.uart_send(NAK);
                        } else {
                            self.flash
                                .write(self.write_addr, data)
                                .map_err(|_| XmodemError::FlashError)?;

                            self.write_addr  += BLOCK_SIZE as u32;
                            *bytes_written   += BLOCK_SIZE as u32;
                            *expected_seq     = expected_seq.wrapping_add(1);
                            self.uart_send(ACK);
                        }
                    }
                }
                _ => self.uart_send(NAK),
            }

            byte = self.uart_recv_timeout(timeout)?;
        }
    }
}

/// Trait for flash write abstraction
pub trait FlashWrite {
    type Error;
    fn write(&mut self, address: u32, data: &[u8]) -> Result<(), Self::Error>;
    fn erase_region(&mut self, address: u32, length: u32) -> Result<(), Self::Error>;
}
```

---

### Host-Side Firmware Flasher in Rust

```rust
// main.rs — host flashing tool using serialport crate
// Cargo.toml deps: serialport = "4", clap = "4"

use serialport::{SerialPort, SerialPortType};
use std::{
    fs,
    io::{self, Read, Write},
    time::{Duration, Instant},
};

const SOH: u8 = 0x01;
const EOT: u8 = 0x04;
const ACK: u8 = 0x06;
const NAK: u8 = 0x15;
const CAN: u8 = 0x18;
const BLOCK_SIZE: usize = 128;

struct XmodemSender {
    port: Box<dyn SerialPort>,
}

impl XmodemSender {
    fn new(path: &str, baud: u32) -> Result<Self, Box<dyn std::error::Error>> {
        let port = serialport::new(path, baud)
            .timeout(Duration::from_millis(5000))
            .open()?;
        Ok(Self { port })
    }

    fn crc16(data: &[u8]) -> u16 {
        let mut crc: u16 = 0;
        for &b in data {
            crc ^= (b as u16) << 8;
            for _ in 0..8 {
                crc = if crc & 0x8000 != 0 {
                    (crc << 1) ^ 0x1021
                } else {
                    crc << 1
                };
            }
        }
        crc
    }

    fn wait_for_c(&mut self) -> Result<(), String> {
        let deadline = Instant::now() + Duration::from_secs(30);
        let mut buf = [0u8; 1];
        loop {
            if Instant::now() > deadline {
                return Err("Timeout waiting for 'C'".into());
            }
            if self.port.read(&mut buf).is_ok() && buf[0] == b'C' {
                return Ok(());
            }
        }
    }

    fn send_block(&mut self, seq: u8, data: &[u8]) -> Result<(), String> {
        assert!(data.len() <= BLOCK_SIZE);
        let mut block = [0x1Au8; BLOCK_SIZE + 5];  // pad with CTRL-Z
        block[0] = SOH;
        block[1] = seq;
        block[2] = !seq;
        block[3..3 + data.len()].copy_from_slice(data);

        let crc = Self::crc16(&block[3..3 + BLOCK_SIZE]);
        block[3 + BLOCK_SIZE]     = (crc >> 8) as u8;
        block[3 + BLOCK_SIZE + 1] = (crc & 0xFF) as u8;

        for attempt in 0..10 {
            self.port.write_all(&block)
                .map_err(|e| e.to_string())?;

            let mut resp = [0u8; 1];
            if self.port.read(&mut resp).is_ok() {
                match resp[0] {
                    ACK => return Ok(()),
                    CAN => return Err("Target cancelled".into()),
                    _   => {} // NAK or garbage — retry
                }
            }
            eprintln!("Retry {}/10 for block {}", attempt + 1, seq);
        }
        Err(format!("Block {} failed after 10 retries", seq))
    }

    pub fn send_file(&mut self, path: &str) -> Result<(), Box<dyn std::error::Error>> {
        let firmware = fs::read(path)?;
        println!("Firmware size: {} bytes ({} blocks)",
            firmware.len(),
            (firmware.len() + BLOCK_SIZE - 1) / BLOCK_SIZE
        );

        println!("Waiting for target...");
        self.wait_for_c()?;
        println!("Target ready. Sending...");

        let mut seq: u8 = 1;
        for (i, chunk) in firmware.chunks(BLOCK_SIZE).enumerate() {
            self.send_block(seq, chunk)?;
            seq = seq.wrapping_add(1);
            print!("\rBlock {}/{}", i + 1,
                (firmware.len() + BLOCK_SIZE - 1) / BLOCK_SIZE);
            io::stdout().flush().ok();
        }

        println!("\nSending EOT...");
        for _ in 0..10 {
            self.port.write_all(&[EOT])?;
            let mut resp = [0u8; 1];
            if self.port.read(&mut resp).is_ok() && resp[0] == ACK {
                println!("Transfer complete!");
                return Ok(());
            }
        }
        Err("EOT not acknowledged".into())
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let port_path = std::env::args().nth(1).expect("Usage: flasher <port> <firmware.bin>");
    let fw_path   = std::env::args().nth(2).expect("Usage: flasher <port> <firmware.bin>");

    let mut sender = XmodemSender::new(&port_path, 115200)?;
    sender.send_file(&fw_path)?;
    Ok(())
}
```

---

### Custom Framed Protocol in Rust

```rust
// framed_bootloader.rs — custom framed protocol, target side

#![no_std]

const SYNC_BYTE_1: u8 = 0xAA;
const SYNC_BYTE_2: u8 = 0x55;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq)]
pub enum Command {
    Start   = 0x01,   // Begin update, payload = total image size (4 bytes)
    Data    = 0x02,   // Chunk of firmware, payload = offset(4) + data(N)
    End     = 0x03,   // End of transfer, payload = whole-image CRC32 (4 bytes)
    Verify  = 0x04,   // Request flash verify
    Reset   = 0x05,   // Jump to app or hardware reset
}

#[repr(u8)]
pub enum Response {
    Ok      = 0x00,
    ErrCrc  = 0x01,
    ErrCmd  = 0x02,
    ErrFlash= 0x03,
}

/// Frame: [0xAA][0x55][CMD][LEN_HI][LEN_LO][PAYLOAD...][CRC16_HI][CRC16_LO]
pub struct FrameParser {
    state:    ParseState,
    cmd:      u8,
    length:   u16,
    payload:  [u8; 256],
    payload_i: usize,
}

#[derive(PartialEq)]
enum ParseState {
    Sync1, Sync2, Cmd, LenHi, LenLo, Payload, CrcHi, CrcLo,
}

impl FrameParser {
    pub fn new() -> Self {
        Self {
            state:     ParseState::Sync1,
            cmd:       0,
            length:    0,
            payload:   [0u8; 256],
            payload_i: 0,
        }
    }

    /// Feed one byte; returns Some((cmd, payload_slice)) when frame is complete.
    pub fn feed(&mut self, byte: u8) -> Option<(u8, &[u8])> {
        match self.state {
            ParseState::Sync1 => {
                if byte == SYNC_BYTE_1 { self.state = ParseState::Sync2; }
            }
            ParseState::Sync2 => {
                if byte == SYNC_BYTE_2 { self.state = ParseState::Cmd; }
                else                   { self.state = ParseState::Sync1; }
            }
            ParseState::Cmd   => { self.cmd = byte; self.state = ParseState::LenHi; }
            ParseState::LenHi => { self.length = (byte as u16) << 8; self.state = ParseState::LenLo; }
            ParseState::LenLo => {
                self.length |= byte as u16;
                self.payload_i = 0;
                if self.length == 0 { self.state = ParseState::CrcHi; }
                else                { self.state = ParseState::Payload; }
            }
            ParseState::Payload => {
                if (self.payload_i as usize) < self.payload.len() {
                    self.payload[self.payload_i as usize] = byte;
                }
                self.payload_i += 1;
                if self.payload_i >= self.length as usize {
                    self.state = ParseState::CrcHi;
                }
            }
            ParseState::CrcHi => {
                // Store CRC high byte (simplified — full impl stores and checks)
                self.state = ParseState::CrcLo;
            }
            ParseState::CrcLo => {
                // In production: verify CRC here before returning
                let cmd     = self.cmd;
                let payload = &self.payload[..self.length as usize];
                self.state  = ParseState::Sync1;
                return Some((cmd, payload));
            }
        }
        None
    }
}

/// Bootloader state machine
pub struct Bootloader<UART, FLASH> {
    uart:    UART,
    flash:   FLASH,
    parser:  FrameParser,
    started: bool,
    total:   u32,
}

impl<UART, FLASH> Bootloader<UART, FLASH>
where
    UART:  embedded_hal::serial::Read<u8> + embedded_hal::serial::Write<u8>,
    FLASH: crate::FlashWrite,
{
    pub fn new(uart: UART, flash: FLASH) -> Self {
        Self { uart, flash, parser: FrameParser::new(), started: false, total: 0 }
    }

    fn send_response(&mut self, r: Response) {
        let _ = nb::block!(self.uart.write(r as u8));
    }

    pub fn process_byte(&mut self, byte: u8) {
        if let Some((cmd, payload)) = self.parser.feed(byte) {
            match cmd {
                x if x == Command::Start as u8 => {
                    if payload.len() >= 4 {
                        self.total = u32::from_be_bytes([
                            payload[0], payload[1], payload[2], payload[3]
                        ]);
                        self.started = true;
                        // Erase flash here
                        self.send_response(Response::Ok);
                    } else {
                        self.send_response(Response::ErrCmd);
                    }
                }
                x if x == Command::Data as u8 => {
                    if !self.started || payload.len() < 4 {
                        self.send_response(Response::ErrCmd);
                        return;
                    }
                    let offset = u32::from_be_bytes([
                        payload[0], payload[1], payload[2], payload[3]
                    ]);
                    let data = &payload[4..];
                    let addr = 0x0800_8000u32 + offset;
                    match self.flash.write(addr, data) {
                        Ok(_) => self.send_response(Response::Ok),
                        Err(_) => self.send_response(Response::ErrFlash),
                    }
                }
                x if x == Command::End as u8 => {
                    // Verify CRC32 of entire image here, then reset
                    self.send_response(Response::Ok);
                }
                _ => self.send_response(Response::ErrCmd),
            }
        }
    }
}
```

---

## Security Considerations

Firmware updates over UART are a potential attack surface. Production designs should implement:

| Threat                        | Mitigation                                                          |
|-------------------------------|---------------------------------------------------------------------|
| Malicious firmware injection  | Cryptographic signature verification (ECDSA / RSA / Ed25519)        |
| Replay attacks                | Rolling counter / nonce in firmware header                          |
| Downgrade attacks             | Minimum version check; anti-rollback counter in OTP/eFuse           |
| Physical eavesdropping        | Encrypt firmware image (AES-128/256-CTR) before transmission        |
| Glitching / fault injection   | Double-check CRC; verify flash readback; use hardware CRC           |
| Unintended bootloader entry   | Require authenticated command; use secure boot chain                |

A minimal secure header layout:

```
┌──────────────────────────────────────────────────────────────────┐
│  Magic (4B) │ Version (4B) │ Size (4B) │ CRC32 (4B)             │
│  IV (16B for AES) │ Encrypted Payload │ Signature (64B ECDSA)   │
└──────────────────────────────────────────────────────────────────┘
```

---

## Practical Design Checklist

- [ ] **Baud rate** — use 115200 or 921600; confirm UART clock accuracy < 3%.
- [ ] **Erase before write** — flash pages must be erased (0xFF) before programming.
- [ ] **Alignment** — write on word/double-word boundaries as required by MCU.
- [ ] **Watchdog** — refresh watchdog during long flash operations; disable or configure for prolonged erase.
- [ ] **Power fail safety** — write new firmware to a staging area; swap only after CRC passes (A/B partition).
- [ ] **Rollback protection** — increment anti-rollback counter in OTP after successful update.
- [ ] **Timeouts** — every UART receive must have a timeout to avoid blocking forever.
- [ ] **CRC verification** — verify CRC on the flash contents after programming, not just on received data.
- [ ] **Bootloader lock** — write-protect bootloader flash pages after deployment.
- [ ] **Communication log** — if possible, log transfer errors for diagnostics.

---

## Summary

UART bootloader protocols provide a reliable, hardware-universal pathway for updating firmware in embedded systems. The key concepts are:

**Protocol choice** — XMODEM-CRC strikes the best balance of simplicity and reliability for constrained targets. YMODEM adds filename/size negotiation. Custom framed protocols give full control over command structure and can incorporate security features.

**C/C++ implementation** — The target-side receiver manages a block-level state machine: send `'C'`, receive 128-byte blocks with CRC-16 verification, write to flash, and ACK/NAK per block. The host-side sender mirrors this, padding the final block and handling retries.

**Rust implementation** — The `embedded-hal` trait abstractions decouple UART and flash access from protocol logic. `nb::block!` handles non-blocking I/O elegantly. The `serialport` crate makes host-side tooling straightforward. The type system enforces correct state transitions at compile time.

**Security** — Bare UART bootloaders are insecure by default. Production designs must add at minimum a CRC of the full image, and ideally a cryptographic signature, encryption of the image in transit, and anti-rollback measures.

**Flash safety** — Always erase before write, verify readback after write, and use an A/B partition scheme to guarantee a fallback image on failed updates.

A well-designed UART bootloader is small (2–8 KB), fast (a 128 KB image at 921600 baud transfers in ~2 seconds), and reliable enough for field deployment over RS-232, RS-485, or direct UART connections.

---

*Document generated for the UART Topics series — Topic 44 of the Bootloader Protocols module.*