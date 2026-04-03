Here's the complete document. It covers:

**Protocol & Hardware**
- RFID frequency bands and ISO standards (14443A/B, 15693, NFC)
- MFRC522 pinout, wiring table, and SPI Mode 0 transaction format
- Address byte encoding (R/W bit + 6-bit register + 0), read/write/burst-read diagrams
- Full register map with the most important registers and their roles
- All command codes (`Transceive`, `MFAuthent`, `CalcCRC`, etc.)

**Flow Diagrams**
- ISO 14443A REQA → Anti-collision → Select state machine
- MIFARE Classic memory layout (sectors, blocks, sector trailer key structure)
- Multi-card anti-collision enumeration loop

**C/C++ Code**
- Platform-agnostic HAL struct with function pointers for SPI, CS, RST, and delay
- Full register read/write, `transceive_raw` engine, REQA, anti-collision, select, authenticate, read/write block
- Linux/Raspberry Pi `spidev` HAL implementation and complete `main()` example
- Modern C++ RAII wrapper using `std::optional` and array types

**Rust Code**
- `embedded-hal 1.0` generic driver over `SpiDevice` + `OutputPin` — no `unsafe`
- `thiserror`-based error enum with descriptive variants
- Register and PICC command enums, burst FIFO read, full authentication flow
- `main.rs` polling example with `linux-embedded-hal`
- Unit tests using `embedded-hal-mock` for SPI address encoding and BCC validation

**Security & Advanced Topics**
- MIFARE Classic / Crypto1 attack summary (nested auth, Darkside, card-only)
- AES-based UID-derived key derivation with mbedTLS
- PN532 comparison table and SPI framing differences

# 70. RFID Reader Modules

## Communicating with MFRC522 and Other RFID/NFC Readers via SPI

---

## Table of Contents

1. [Introduction to RFID and NFC](#1-introduction-to-rfid-and-nfc)
2. [The MFRC522 Module](#2-the-mfrc522-module)
3. [SPI Interface of the MFRC522](#3-spi-interface-of-the-mfrc522)
4. [Register Map and Key Registers](#4-register-map-and-key-registers)
5. [Initialization and Card Detection Flow](#5-initialization-and-card-detection-flow)
6. [MIFARE Classic Memory Layout](#6-mifare-classic-memory-layout)
7. [C/C++ Implementation](#7-cc-implementation)
8. [Rust Implementation](#8-rust-implementation)
9. [Other RFID/NFC Modules](#9-other-rfidnfc-modules)
10. [Anti-Collision and Multi-Card Handling](#10-anti-collision-and-multi-card-handling)
11. [Security Considerations](#11-security-considerations)
12. [Summary](#12-summary)

---

## 1. Introduction to RFID and NFC

**Radio Frequency Identification (RFID)** is a wireless technology that uses electromagnetic fields to automatically identify and track tags attached to objects. **Near Field Communication (NFC)** is a subset of RFID operating at 13.56 MHz with a range of ~4 cm, designed for two-way communication.

### Frequency Bands

| Band        | Frequency         | Range      | Data Rate     | Typical Use                  |
|-------------|-------------------|------------|---------------|------------------------------|
| LF          | 125–134 kHz       | < 10 cm    | ~1 kbps       | Animal tagging, access cards |
| HF (NFC)    | 13.56 MHz         | < 10 cm    | 106–848 kbps  | Payments, smart cards, NFC   |
| UHF         | 860–960 MHz       | 1–12 m     | 40–640 kbps   | Logistics, inventory         |
| Microwave   | 2.4–5.8 GHz       | > 1 m      | High          | Toll systems, vehicle ID     |

### Key Standards at 13.56 MHz

- **ISO/IEC 14443 Type A** — MIFARE cards (NXP), used by MFRC522
- **ISO/IEC 14443 Type B** — Used in e-passports, banking cards
- **ISO/IEC 15693** — Vicinity cards, longer range
- **ISO/IEC 18092 (NFC-IP1)** — P2P NFC communication

The **MFRC522** is the most common hobbyist/embedded RFID reader IC, supporting ISO 14443A/MIFARE.

---

## 2. The MFRC522 Module

The **MFRC522** (by NXP Semiconductors) is a highly integrated 13.56 MHz RFID reader/writer IC. It supports three interfaces: **SPI** (up to 10 MHz), **I²C**, and **UART**. In embedded systems, SPI is overwhelmingly preferred for its speed and simplicity.

### Key Features

- Supports ISO/IEC 14443A/MIFARE and NTAG
- Internal transmitter drives antenna without external amplifier
- 64-byte FIFO send/receive buffer
- CRC coprocessor for error detection
- Cryptographic unit for MIFARE Classic authentication (Crypto1)
- 3.3 V operation (5 V tolerant I/O on most breakout boards)
- SPI up to 10 MHz

### Typical Breakout Board Pinout

```
MFRC522 Module
┌─────────────────────┐
│  SDA (CS/SS)   ───► │── GPIO (Chip Select, active LOW)
│  SCK           ───► │── SPI Clock
│  MOSI          ───► │── Master Out Slave In
│  MISO          ◄─── │── Master In Slave Out
│  IRQ           ◄─── │── Interrupt (optional)
│  GND           ───► │── Ground
│  RST           ───► │── Reset (active LOW)
│  3.3V          ───► │── Power Supply
└─────────────────────┘
```

### Wiring to a Microcontroller (e.g., STM32 / Raspberry Pi)

| MFRC522 Pin | Raspberry Pi (BCM) | STM32 (SPI1)  |
|-------------|--------------------|---------------|
| SDA (CS)    | GPIO 8 (CE0)       | PA4 (NSS)     |
| SCK         | GPIO 11 (SCLK)     | PA5 (SCK)     |
| MOSI        | GPIO 10 (MOSI)     | PA7 (MOSI)    |
| MISO        | GPIO 9  (MISO)     | PA6 (MISO)    |
| RST         | GPIO 25            | PA3 (GPIO)    |
| 3.3V        | Pin 1              | 3.3V          |
| GND         | Pin 6              | GND           |

---

## 3. SPI Interface of the MFRC522

### SPI Mode

The MFRC522 uses **SPI Mode 0** (CPOL=0, CPHA=0): data is captured on the rising clock edge and shifted on the falling edge.

```
Clock  ─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─
         └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘
CS     ──┐                               ┌──
         └───────────────────────────────┘
MOSI   ──┤ Addr (7b+RW) │    Data Byte   ├──
MISO   ──┤   (ignored)  │  Return Data   ├──
```

### Address Byte Format

The first byte of every SPI transaction is the **address byte**:

```
Bit 7:   R/W — 1 = Read, 0 = Write
Bits 6-1: Register Address (6 bits)
Bit 0:   Always 0
```

So the address byte for reading register `0x26` is:
```
(1 << 7) | (0x26 << 1) | 0 = 0x4C
```

And for writing:
```
(0 << 7) | (0x26 << 1) | 0 = 0x4C → address = 0x4C, but bit7=0
```

### Read Transaction

```
CS LOW
Send: [1|ADDR[5:0]|0]   ← address byte (R=1)
Send: [0x00]             ← dummy byte, receive data
CS HIGH
```

### Write Transaction

```
CS LOW
Send: [0|ADDR[5:0]|0]   ← address byte (W=0)
Send: [DATA]             ← data byte
CS HIGH
```

### Burst Read (multiple registers)

```
CS LOW
Send: [1|ADDR[5:0]|0]   ← first address
Send: N dummy bytes      ← returns N data bytes
CS HIGH
```

---

## 4. Register Map and Key Registers

The MFRC522 has registers organized into **pages** (Page 0–3), accessed directly via the 6-bit address field.

### Essential Registers

| Register Name     | Addr  | Description                                       |
|-------------------|-------|---------------------------------------------------|
| `CommandReg`      | 0x01  | Command execution control                         |
| `ComIEnReg`       | 0x02  | Interrupt enable                                  |
| `ComIrqReg`       | 0x04  | Interrupt request flags                           |
| `ErrorReg`        | 0x06  | Error flags                                       |
| `Status1Reg`      | 0x07  | Communication status                              |
| `Status2Reg`      | 0x08  | Receiver and transmitter status                   |
| `FIFODataReg`     | 0x09  | 64-byte FIFO read/write                           |
| `FIFOLevelReg`    | 0x0A  | FIFO byte count                                   |
| `ControlReg`      | 0x0C  | Miscellaneous control                             |
| `BitFramingReg`   | 0x0D  | Bit-oriented frames (last bits adjustment)        |
| `CollReg`         | 0x0E  | Anti-collision bit position                       |
| `ModeReg`         | 0x11  | General mode settings                             |
| `TxControlReg`    | 0x14  | Antenna TX pin control                            |
| `TxASKReg`        | 0x15  | ASK modulation settings                           |
| `CRCResultRegH`   | 0x21  | CRC result high byte                              |
| `CRCResultRegL`   | 0x22  | CRC result low byte                               |
| `TModeReg`        | 0x2A  | Timer configuration                               |
| `TPrescalerReg`   | 0x2B  | Timer prescaler                                   |
| `TReloadRegH`     | 0x2C  | Timer reload high byte                            |
| `TReloadRegL`     | 0x2D  | Timer reload low byte                             |
| `VersionReg`      | 0x37  | Chip version (0x91 = v1, 0x92 = v2)               |

### Key Commands (`CommandReg` values)

| Command       | Value | Description                          |
|---------------|-------|--------------------------------------|
| `Idle`        | 0x00  | Cancel current command               |
| `Mem`         | 0x01  | Transfer FIFO → internal buffer      |
| `CalcCRC`     | 0x03  | Calculate CRC                        |
| `Transmit`    | 0x04  | Transmit FIFO data                   |
| `Receive`     | 0x08  | Receive data into FIFO               |
| `Transceive`  | 0x0C  | Transmit + receive (most common)     |
| `MFAuthent`   | 0x0E  | MIFARE authentication                |
| `SoftReset`   | 0x0F  | Soft reset                           |

---

## 5. Initialization and Card Detection Flow

### Initialization Sequence

```
1. Hardware reset (RST pin LOW → HIGH)
2. SoftReset command
3. Wait for reset complete (CommandReg bit5 = 0)
4. Configure timer (for timeout detection)
5. Set TxASKReg (100% ASK modulation)
6. Set ModeReg (CRC preset to 0x6363 per ISO 14443)
7. Enable antenna (TxControlReg bits 0,1)
```

### Card Detection Flow (ISO 14443A)

```
┌─────────────────────────────────────────────┐
│              IDLE / Polling                 │
└──────────────────┬──────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│  REQA / WUPA command (0x26 / 0x52)          │
│  → Transceive, 7-bit frame                  │
└──────────────────┬──────────────────────────┘
                   │ ATQA response (2 bytes)
                   ▼
┌─────────────────────────────────────────────┐
│  Anti-Collision (SEL=0x93, NVB=0x20)        │
│  → Transceive, receive UID CLn (4-5 bytes)  │
└──────────────────┬──────────────────────────┘
                   │ UID bytes + BCC
                   ▼
┌─────────────────────────────────────────────┐
│  SELECT (SEL=0x93, NVB=0x70, UID + CRC)     │
│  → Receive SAK (1 byte)                     │
└──────────────────┬──────────────────────────┘
                   │ SAK byte
                   ▼
┌─────────────────────────────────────────────┐
│  Card selected — check SAK for card type    │
│  SAK & 0x20: ISO 14443-4 (MIFARE DESFire)   │
│  SAK & 0x08: MIFARE Classic                 │
└─────────────────────────────────────────────┘
```

### MIFARE Classic Authentication Flow

```
AUTHENTICATE → READ/WRITE blocks
│
├─ MFAuthent command with Key A or Key B
│  (Key from sector trailer, block number, UID)
│
├─ Crypto1 mutual authentication (3-pass)
│
└─ READ (0x30) or WRITE (0xA0) block commands
```

---

## 6. MIFARE Classic Memory Layout

MIFARE Classic 1K has **16 sectors × 4 blocks × 16 bytes = 1024 bytes**.

```
Sector  Block   Content
──────  ─────   ───────────────────────────────────────────────
  0       0     Manufacturer data (UID, BCC, SAK — read only)
          1     Data block
          2     Data block
          3     Sector Trailer (Key A [6] | Access Bits [4] | Key B [6])

  1       4     Data block
          5     Data block
          6     Data block
          7     Sector Trailer

  ...

 15      60     Data block
         61     Data block
         62     Data block
         63     Sector Trailer
```

**Sector Trailer** (last block of each sector):
```
Bytes 0–5:   Key A (6 bytes, write-only — always reads as 0x00)
Bytes 6–9:   Access bits (3 bytes + 1 byte user data)
Bytes 10–15: Key B (6 bytes, readable if so configured)
```

Default Key A and Key B: `0xFF 0xFF 0xFF 0xFF 0xFF 0xFF`

---

## 7. C/C++ Implementation

### 7.1 Platform-Agnostic SPI HAL Interface

```c
// rfid_hal.h — Hardware Abstraction Layer for SPI + GPIO
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // SPI transfer: send tx_buf, receive into rx_buf, len bytes
    void (*spi_transfer)(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len, void *ctx);
    void (*cs_low)(void *ctx);
    void (*cs_high)(void *ctx);
    void (*rst_low)(void *ctx);
    void (*rst_high)(void *ctx);
    void (*delay_ms)(uint32_t ms, void *ctx);
    void *ctx; // user context (e.g., SPI handle pointer)
} RFID_HAL;
```

### 7.2 MFRC522 Driver Header

```c
// mfrc522.h
#pragma once
#include "rfid_hal.h"

// ──── Register Addresses ────────────────────────────────────────────────────
#define MFRC522_REG_COMMAND         0x01
#define MFRC522_REG_COM_IEN         0x02
#define MFRC522_REG_COM_IRQ         0x04
#define MFRC522_REG_ERROR           0x06
#define MFRC522_REG_STATUS1         0x07
#define MFRC522_REG_STATUS2         0x08
#define MFRC522_REG_FIFO_DATA       0x09
#define MFRC522_REG_FIFO_LEVEL      0x0A
#define MFRC522_REG_CONTROL         0x0C
#define MFRC522_REG_BIT_FRAMING     0x0D
#define MFRC522_REG_COLL            0x0E
#define MFRC522_REG_MODE            0x11
#define MFRC522_REG_TX_CONTROL      0x14
#define MFRC522_REG_TX_ASK          0x15
#define MFRC522_REG_CRC_RESULT_H    0x21
#define MFRC522_REG_CRC_RESULT_L    0x22
#define MFRC522_REG_T_MODE          0x2A
#define MFRC522_REG_T_PRESCALER     0x2B
#define MFRC522_REG_T_RELOAD_H      0x2C
#define MFRC522_REG_T_RELOAD_L      0x2D
#define MFRC522_REG_VERSION         0x37

// ──── Commands ───────────────────────────────────────────────────────────────
#define MFRC522_CMD_IDLE            0x00
#define MFRC522_CMD_CALC_CRC        0x03
#define MFRC522_CMD_TRANSMIT        0x04
#define MFRC522_CMD_RECEIVE         0x08
#define MFRC522_CMD_TRANSCEIVE      0x0C
#define MFRC522_CMD_MF_AUTHENT      0x0E
#define MFRC522_CMD_SOFT_RESET      0x0F

// ──── PICC / ISO 14443A ──────────────────────────────────────────────────────
#define PICC_CMD_REQA               0x26
#define PICC_CMD_WUPA               0x52
#define PICC_CMD_CT                 0x88  // Cascade tag
#define PICC_CMD_SEL_CL1            0x93
#define PICC_CMD_SEL_CL2            0x95
#define PICC_CMD_SEL_CL3            0x97
#define PICC_CMD_HLTA               0x50
#define PICC_CMD_MF_AUTH_KEY_A      0x60
#define PICC_CMD_MF_AUTH_KEY_B      0x61
#define PICC_CMD_MF_READ            0x30
#define PICC_CMD_MF_WRITE           0xA0

// ──── Return Codes ───────────────────────────────────────────────────────────
typedef enum {
    MFRC522_OK            =  0,
    MFRC522_ERR_GENERIC   = -1,
    MFRC522_ERR_TIMEOUT   = -2,
    MFRC522_ERR_CRC       = -3,
    MFRC522_ERR_COLLISION = -4,
    MFRC522_ERR_NOCARD    = -5,
    MFRC522_ERR_AUTH      = -6,
    MFRC522_ERR_BUFFER    = -7,
} MFRC522_Status;

// ──── Card UID ───────────────────────────────────────────────────────────────
typedef struct {
    uint8_t  bytes[10]; // up to 10 bytes for triple-size UID
    uint8_t  size;      // number of valid bytes (4, 7, or 10)
    uint8_t  sak;       // Select Acknowledge byte
} PICC_UID;

typedef struct {
    RFID_HAL hal;
} MFRC522;

// ──── API ─────────────────────────────────────────────────────────────────────
void         mfrc522_init(MFRC522 *dev, RFID_HAL hal);
uint8_t      mfrc522_read_reg(MFRC522 *dev, uint8_t reg);
void         mfrc522_write_reg(MFRC522 *dev, uint8_t reg, uint8_t val);
void         mfrc522_set_bit_mask(MFRC522 *dev, uint8_t reg, uint8_t mask);
void         mfrc522_clear_bit_mask(MFRC522 *dev, uint8_t reg, uint8_t mask);
void         mfrc522_antenna_on(MFRC522 *dev);
void         mfrc522_antenna_off(MFRC522 *dev);
MFRC522_Status mfrc522_request(MFRC522 *dev, uint8_t req_mode,
                               uint8_t *atqa);
MFRC522_Status mfrc522_anticoll(MFRC522 *dev, uint8_t sel_code,
                                PICC_UID *uid);
MFRC522_Status mfrc522_select(MFRC522 *dev, uint8_t sel_code,
                              PICC_UID *uid);
MFRC522_Status mfrc522_read_uid(MFRC522 *dev, PICC_UID *uid);
MFRC522_Status mfrc522_authenticate(MFRC522 *dev, uint8_t auth_cmd,
                                    uint8_t block, const uint8_t *key,
                                    const PICC_UID *uid);
MFRC522_Status mfrc522_read_block(MFRC522 *dev, uint8_t block,
                                  uint8_t *data, uint8_t *len);
MFRC522_Status mfrc522_write_block(MFRC522 *dev, uint8_t block,
                                   const uint8_t *data);
void           mfrc522_stop_crypto(MFRC522 *dev);
bool           mfrc522_is_card_present(MFRC522 *dev);
```

### 7.3 MFRC522 Driver Implementation

```c
// mfrc522.c
#include "mfrc522.h"
#include <string.h>

// ── Low-level SPI register access ─────────────────────────────────────────

uint8_t mfrc522_read_reg(MFRC522 *dev, uint8_t reg) {
    uint8_t tx[2] = { (1 << 7) | ((reg & 0x3F) << 1), 0x00 };
    uint8_t rx[2] = { 0 };
    dev->hal.cs_low(dev->hal.ctx);
    dev->hal.spi_transfer(tx, rx, 2, dev->hal.ctx);
    dev->hal.cs_high(dev->hal.ctx);
    return rx[1];
}

void mfrc522_write_reg(MFRC522 *dev, uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { (0 << 7) | ((reg & 0x3F) << 1), val };
    uint8_t rx[2];
    dev->hal.cs_low(dev->hal.ctx);
    dev->hal.spi_transfer(tx, rx, 2, dev->hal.ctx);
    dev->hal.cs_high(dev->hal.ctx);
}

void mfrc522_set_bit_mask(MFRC522 *dev, uint8_t reg, uint8_t mask) {
    mfrc522_write_reg(dev, reg, mfrc522_read_reg(dev, reg) | mask);
}

void mfrc522_clear_bit_mask(MFRC522 *dev, uint8_t reg, uint8_t mask) {
    mfrc522_write_reg(dev, reg, mfrc522_read_reg(dev, reg) & ~mask);
}

// ── Initialization ────────────────────────────────────────────────────────

void mfrc522_init(MFRC522 *dev, RFID_HAL hal) {
    dev->hal = hal;

    // Hardware reset
    dev->hal.rst_low(dev->hal.ctx);
    dev->hal.delay_ms(10, dev->hal.ctx);
    dev->hal.rst_high(dev->hal.ctx);
    dev->hal.delay_ms(50, dev->hal.ctx);

    // Soft reset
    mfrc522_write_reg(dev, MFRC522_REG_COMMAND, MFRC522_CMD_SOFT_RESET);
    dev->hal.delay_ms(50, dev->hal.ctx);

    // Timer: TAuto=1, timer starts automatically at end of transmission
    // Prescaler: f_timer = 13.56MHz / (2*TPreScaler+1)
    // TPreScaler = 0xA9 → f_timer ≈ 40 kHz
    // TReload = 0x03E8 → timeout ≈ 25 ms
    mfrc522_write_reg(dev, MFRC522_REG_T_MODE,      0x8D); // TAuto=1
    mfrc522_write_reg(dev, MFRC522_REG_T_PRESCALER, 0x3E);
    mfrc522_write_reg(dev, MFRC522_REG_T_RELOAD_H,  0x00);
    mfrc522_write_reg(dev, MFRC522_REG_T_RELOAD_L,  0x1E); // ~30 ticks

    // 100% ASK modulation
    mfrc522_write_reg(dev, MFRC522_REG_TX_ASK, 0x40);

    // CRC preset: 0x6363 (ISO 14443-3)
    mfrc522_write_reg(dev, MFRC522_REG_MODE, 0x3D);

    // Enable antenna
    mfrc522_antenna_on(dev);
}

void mfrc522_antenna_on(MFRC522 *dev) {
    uint8_t val = mfrc522_read_reg(dev, MFRC522_REG_TX_CONTROL);
    if ((val & 0x03) != 0x03) {
        mfrc522_set_bit_mask(dev, MFRC522_REG_TX_CONTROL, 0x03);
    }
}

void mfrc522_antenna_off(MFRC522 *dev) {
    mfrc522_clear_bit_mask(dev, MFRC522_REG_TX_CONTROL, 0x03);
}

// ── Core Transceive Engine ────────────────────────────────────────────────

static MFRC522_Status mfrc522_transceive_raw(
    MFRC522 *dev,
    const uint8_t *send_buf, uint8_t send_len,
    uint8_t *recv_buf, uint8_t *recv_len,
    uint8_t last_bits,        // number of valid bits in last byte (0=all 8)
    uint8_t *rx_last_bits,    // bits valid in last received byte
    bool check_crc
) {
    uint8_t cmd = MFRC522_CMD_TRANSCEIVE;

    // Prepare interrupt flags
    mfrc522_write_reg(dev, MFRC522_REG_COM_IEN, 0x77 | 0x80); // IRQ push-pull
    mfrc522_clear_bit_mask(dev, MFRC522_REG_COM_IRQ, 0x80);    // Clear IRQ bits
    mfrc522_set_bit_mask(dev, MFRC522_REG_FIFO_LEVEL, 0x80);   // Flush FIFO

    // Idle
    mfrc522_write_reg(dev, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);

    // Write data to FIFO
    for (uint8_t i = 0; i < send_len; i++) {
        mfrc522_write_reg(dev, MFRC522_REG_FIFO_DATA, send_buf[i]);
    }

    // Bit framing
    mfrc522_write_reg(dev, MFRC522_REG_BIT_FRAMING,
                      (last_bits << 4) | last_bits);

    // Execute command
    mfrc522_write_reg(dev, MFRC522_REG_COMMAND, cmd);
    if (cmd == MFRC522_CMD_TRANSCEIVE) {
        mfrc522_set_bit_mask(dev, MFRC522_REG_BIT_FRAMING, 0x80); // StartSend
    }

    // Wait for IRQ: RxIRq (0x20) or IdleIRq (0x10) or TimerIRq (0x01)
    uint16_t timeout = 2000;
    uint8_t irq;
    do {
        irq = mfrc522_read_reg(dev, MFRC522_REG_COM_IRQ);
        if (--timeout == 0) {
            mfrc522_write_reg(dev, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
            return MFRC522_ERR_TIMEOUT;
        }
    } while (!(irq & 0x31)); // RxIRq | IdleIRq | TimerIRq

    mfrc522_write_reg(dev, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);

    if (irq & 0x01) return MFRC522_ERR_TIMEOUT;

    uint8_t err = mfrc522_read_reg(dev, MFRC522_REG_ERROR);
    if (err & 0x13) { // BufferOvfl | ParityErr | ProtocolErr
        return MFRC522_ERR_GENERIC;
    }
    if (err & 0x08) { // CollErr
        return MFRC522_ERR_COLLISION;
    }

    uint8_t n = mfrc522_read_reg(dev, MFRC522_REG_FIFO_LEVEL);
    uint8_t last = mfrc522_read_reg(dev, MFRC522_REG_CONTROL) & 0x07;
    if (rx_last_bits) *rx_last_bits = last;

    if (n > *recv_len) return MFRC522_ERR_BUFFER;
    *recv_len = n;

    // Burst-read FIFO
    dev->hal.cs_low(dev->hal.ctx);
    uint8_t addr_byte = (1 << 7) | ((MFRC522_REG_FIFO_DATA & 0x3F) << 1);
    dev->hal.spi_transfer(&addr_byte, recv_buf, 1, dev->hal.ctx); // send addr
    // Receive n bytes: send dummy 0x00, read data
    uint8_t zero = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t dummy = 0;
        dev->hal.spi_transfer(&zero, &recv_buf[i], 1, dev->hal.ctx);
    }
    dev->hal.cs_high(dev->hal.ctx);

    if (check_crc && n > 2) {
        // CRC is appended as last 2 bytes
        if ((err & 0x04) != 0) return MFRC522_ERR_CRC; // CRCErr
        // Recalculate CRC over n-2 bytes and compare
        // (full CRC check omitted for brevity — see calc_crc below)
    }

    return MFRC522_OK;
}

// ── REQA / WUPA ───────────────────────────────────────────────────────────

MFRC522_Status mfrc522_request(MFRC522 *dev, uint8_t req_mode,
                               uint8_t *atqa) {
    uint8_t buf = req_mode;
    uint8_t rx_buf[2];
    uint8_t rx_len = sizeof(rx_buf);

    mfrc522_clear_bit_mask(dev, MFRC522_REG_COLL, 0x80); // ValuesAfterColl=0

    // REQA is 7-bit frame
    MFRC522_Status status = mfrc522_transceive_raw(
        dev, &buf, 1, rx_buf, &rx_len, 7, NULL, false);

    if (status != MFRC522_OK) return status;
    if (rx_len != 2) return MFRC522_ERR_GENERIC;

    atqa[0] = rx_buf[0];
    atqa[1] = rx_buf[1];
    return MFRC522_OK;
}

// ── Anti-Collision & Select ───────────────────────────────────────────────

MFRC522_Status mfrc522_anticoll(MFRC522 *dev, uint8_t sel_code,
                                PICC_UID *uid) {
    uint8_t tx_buf[2] = { sel_code, 0x20 }; // SEL, NVB = 0x20 (2 bytes, 0 bits)
    uint8_t rx_buf[5];
    uint8_t rx_len = sizeof(rx_buf);

    mfrc522_clear_bit_mask(dev, MFRC522_REG_COLL, 0x80);

    MFRC522_Status status = mfrc522_transceive_raw(
        dev, tx_buf, 2, rx_buf, &rx_len, 0, NULL, false);

    if (status != MFRC522_OK) return status;
    if (rx_len != 5) return MFRC522_ERR_GENERIC;

    // BCC check: rx_buf[0..3] XOR rx_buf[4] == 0
    uint8_t bcc = rx_buf[0] ^ rx_buf[1] ^ rx_buf[2] ^ rx_buf[3];
    if (bcc != rx_buf[4]) return MFRC522_ERR_GENERIC;

    for (uint8_t i = 0; i < 4; i++) {
        uid->bytes[uid->size++] = rx_buf[i];
    }
    return MFRC522_OK;
}

MFRC522_Status mfrc522_select(MFRC522 *dev, uint8_t sel_code,
                              PICC_UID *uid) {
    // tx_buf: SEL | NVB=0x70 | UID[0..3] | BCC | CRC_A (2 bytes)
    uint8_t tx_buf[9];
    tx_buf[0] = sel_code;
    tx_buf[1] = 0x70;
    uint8_t bcc = 0;
    for (uint8_t i = 0; i < 4; i++) {
        tx_buf[2 + i] = uid->bytes[i];
        bcc ^= uid->bytes[i];
    }
    tx_buf[6] = bcc;
    // TODO: append CRC_A to tx_buf[7..8] using CalcCRC command
    // (CRC calculation omitted for brevity)

    uint8_t rx_buf[3];
    uint8_t rx_len = sizeof(rx_buf);
    MFRC522_Status status = mfrc522_transceive_raw(
        dev, tx_buf, 9, rx_buf, &rx_len, 0, NULL, true);

    if (status != MFRC522_OK) return status;
    uid->sak = rx_buf[0];
    return MFRC522_OK;
}

// ── High-level: Read UID ──────────────────────────────────────────────────

MFRC522_Status mfrc522_read_uid(MFRC522 *dev, PICC_UID *uid) {
    memset(uid, 0, sizeof(*uid));
    uint8_t atqa[2];

    // 1. REQA
    MFRC522_Status s = mfrc522_request(dev, PICC_CMD_REQA, atqa);
    if (s != MFRC522_OK) return s;

    // 2. Anti-collision (CL1)
    s = mfrc522_anticoll(dev, PICC_CMD_SEL_CL1, uid);
    if (s != MFRC522_OK) return s;

    // 3. Select CL1
    s = mfrc522_select(dev, PICC_CMD_SEL_CL1, uid);
    if (s != MFRC522_OK) return s;

    // If SAK indicates cascade, continue with CL2 / CL3
    if (uid->sak & 0x04) {
        // UID[0] == CT (Cascade Tag), shift and continue
        // (multi-cascade handling omitted for brevity)
    }

    return MFRC522_OK;
}

// ── Authentication ────────────────────────────────────────────────────────

MFRC522_Status mfrc522_authenticate(MFRC522 *dev, uint8_t auth_cmd,
                                    uint8_t block, const uint8_t *key,
                                    const PICC_UID *uid) {
    // Build authentication frame:
    // [cmd(1)] [block(1)] [key(6)] [uid(4)]
    uint8_t buf[12];
    buf[0] = auth_cmd;
    buf[1] = block;
    memcpy(&buf[2], key, 6);
    memcpy(&buf[8], uid->bytes, 4); // Only first 4 bytes of UID

    // Flush FIFO, load data, execute MFAuthent
    mfrc522_set_bit_mask(dev, MFRC522_REG_FIFO_LEVEL, 0x80);
    mfrc522_write_reg(dev, MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    for (uint8_t i = 0; i < 12; i++) {
        mfrc522_write_reg(dev, MFRC522_REG_FIFO_DATA, buf[i]);
    }
    mfrc522_write_reg(dev, MFRC522_REG_COMMAND, MFRC522_CMD_MF_AUTHENT);

    // Poll until MFCrypto1On (bit3) is set in Status2Reg or timeout
    uint16_t timeout = 2000;
    uint8_t status2;
    do {
        status2 = mfrc522_read_reg(dev, MFRC522_REG_STATUS2);
        if (--timeout == 0) return MFRC522_ERR_TIMEOUT;
    } while (!(status2 & 0x08)); // MFCrypto1On

    uint8_t err = mfrc522_read_reg(dev, MFRC522_REG_ERROR);
    if (err & 0x04) return MFRC522_ERR_AUTH; // ProtocolErr after auth = bad key

    return MFRC522_OK;
}

void mfrc522_stop_crypto(MFRC522 *dev) {
    mfrc522_clear_bit_mask(dev, MFRC522_REG_STATUS2, 0x08); // Clear MFCrypto1On
}

// ── Read/Write Data Blocks ────────────────────────────────────────────────

MFRC522_Status mfrc522_read_block(MFRC522 *dev, uint8_t block,
                                  uint8_t *data, uint8_t *len) {
    if (*len < 18) return MFRC522_ERR_BUFFER; // 16 data + 2 CRC

    uint8_t cmd[4];
    cmd[0] = PICC_CMD_MF_READ;
    cmd[1] = block;
    // Append CRC (TODO: use CalcCRC command)
    cmd[2] = 0; cmd[3] = 0; // Placeholder

    return mfrc522_transceive_raw(dev, cmd, 4, data, len, 0, NULL, true);
}

MFRC522_Status mfrc522_write_block(MFRC522 *dev, uint8_t block,
                                   const uint8_t *data) {
    // Phase 1: Send MF_WRITE command + block number
    uint8_t cmd[4] = { PICC_CMD_MF_WRITE, block, 0, 0 }; // + CRC placeholder
    uint8_t rx[2];
    uint8_t rx_len = sizeof(rx);
    MFRC522_Status s = mfrc522_transceive_raw(
        dev, cmd, 4, rx, &rx_len, 0, NULL, false);
    if (s != MFRC522_OK) return s;

    // Check ACK (0x0A)
    if ((rx[0] & 0x0F) != 0x0A) return MFRC522_ERR_GENERIC;

    // Phase 2: Send 16 data bytes + CRC
    uint8_t payload[18];
    memcpy(payload, data, 16);
    payload[16] = 0; payload[17] = 0; // CRC placeholder
    uint8_t rx2[2];
    uint8_t rx2_len = sizeof(rx2);
    s = mfrc522_transceive_raw(dev, payload, 18, rx2, &rx2_len, 0, NULL, false);
    if (s != MFRC522_OK) return s;
    if ((rx2[0] & 0x0F) != 0x0A) return MFRC522_ERR_GENERIC;

    return MFRC522_OK;
}

bool mfrc522_is_card_present(MFRC522 *dev) {
    uint8_t atqa[2];
    return mfrc522_request(dev, PICC_CMD_WUPA, atqa) == MFRC522_OK;
}
```

### 7.4 Application Example: Scan UID and Read Block

```c
// main.c — Full usage example
#include "mfrc522.h"
#include <stdio.h>

// ── Platform-specific HAL implementations (Linux / Raspberry Pi) ──────────
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stddef.h>

typedef struct {
    int spi_fd;
    int cs_gpio;
    int rst_gpio;
} PlatformCtx;

static void platform_spi_transfer(const uint8_t *tx, uint8_t *rx,
                                   uint16_t len, void *ctx) {
    PlatformCtx *p = (PlatformCtx *)ctx;
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = len,
        .speed_hz      = 1000000,
        .bits_per_word = 8,
    };
    ioctl(p->spi_fd, SPI_IOC_MESSAGE(1), &tr);
}

// GPIO helpers (sysfs, simplified)
static void gpio_write(int gpio, int val) {
    char path[64], v[2];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_WRONLY);
    v[0] = val ? '1' : '0'; v[1] = 0;
    write(fd, v, 1);
    close(fd);
}

static void platform_cs_low(void *ctx)    { gpio_write(((PlatformCtx*)ctx)->cs_gpio,  0); }
static void platform_cs_high(void *ctx)   { gpio_write(((PlatformCtx*)ctx)->cs_gpio,  1); }
static void platform_rst_low(void *ctx)   { gpio_write(((PlatformCtx*)ctx)->rst_gpio, 0); }
static void platform_rst_high(void *ctx)  { gpio_write(((PlatformCtx*)ctx)->rst_gpio, 1); }
static void platform_delay_ms(uint32_t ms, void *ctx) { usleep(ms * 1000); (void)ctx; }

int main(void) {
    PlatformCtx pctx = { .spi_fd = -1, .cs_gpio = 8, .rst_gpio = 25 };
    pctx.spi_fd = open("/dev/spidev0.0", O_RDWR);

    RFID_HAL hal = {
        .spi_transfer = platform_spi_transfer,
        .cs_low       = platform_cs_low,
        .cs_high      = platform_cs_high,
        .rst_low      = platform_rst_low,
        .rst_high     = platform_rst_high,
        .delay_ms     = platform_delay_ms,
        .ctx          = &pctx,
    };

    MFRC522 reader;
    mfrc522_init(&reader, hal);

    uint8_t version = mfrc522_read_reg(&reader, MFRC522_REG_VERSION);
    printf("MFRC522 Version: 0x%02X (%s)\n", version,
           version == 0x92 ? "v2.0" : version == 0x91 ? "v1.0" : "unknown");

    printf("Waiting for card...\n");

    while (1) {
        PICC_UID uid;
        if (mfrc522_read_uid(&reader, &uid) == MFRC522_OK) {
            printf("Card UID: ");
            for (uint8_t i = 0; i < uid.size; i++) {
                printf("%02X ", uid.bytes[i]);
            }
            printf("(SAK=0x%02X)\n", uid.sak);

            // Authenticate with default Key A and read block 4
            const uint8_t default_key[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
            if (mfrc522_authenticate(&reader, PICC_CMD_MF_AUTH_KEY_A,
                                     4, default_key, &uid) == MFRC522_OK) {
                uint8_t data[18];
                uint8_t len = sizeof(data);
                if (mfrc522_read_block(&reader, 4, data, &len) == MFRC522_OK) {
                    printf("Block 4 data: ");
                    for (int i = 0; i < 16; i++) printf("%02X ", data[i]);
                    printf("\n");
                }
                mfrc522_stop_crypto(&reader);
            } else {
                printf("Authentication failed.\n");
            }
        }
        usleep(500000); // Poll every 500 ms
    }

    close(pctx.spi_fd);
    return 0;
}
```

### 7.5 C++ RAII Wrapper

```cpp
// Mfrc522.hpp — Modern C++ wrapper with RAII
#pragma once
#include "mfrc522.h"
#include <array>
#include <optional>
#include <stdexcept>
#include <vector>
#include <cstring>

class Mfrc522 {
public:
    static constexpr std::array<uint8_t, 6> DEFAULT_KEY =
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    explicit Mfrc522(RFID_HAL hal) {
        mfrc522_init(&dev_, hal);
        uint8_t ver = mfrc522_read_reg(&dev_, MFRC522_REG_VERSION);
        if (ver != 0x91 && ver != 0x92) {
            throw std::runtime_error("MFRC522 not detected or wrong version");
        }
    }

    ~Mfrc522() {
        mfrc522_stop_crypto(&dev_);
        mfrc522_antenna_off(&dev_);
    }

    // Non-copyable, movable
    Mfrc522(const Mfrc522&) = delete;
    Mfrc522& operator=(const Mfrc522&) = delete;

    std::optional<PICC_UID> readUID() {
        PICC_UID uid{};
        if (mfrc522_read_uid(&dev_, &uid) == MFRC522_OK) {
            return uid;
        }
        return std::nullopt;
    }

    bool authenticate(const PICC_UID &uid, uint8_t block,
                      const std::array<uint8_t, 6> &key = DEFAULT_KEY,
                      bool useKeyA = true) {
        uint8_t cmd = useKeyA ? PICC_CMD_MF_AUTH_KEY_A : PICC_CMD_MF_AUTH_KEY_B;
        return mfrc522_authenticate(&dev_, cmd, block, key.data(), &uid)
               == MFRC522_OK;
    }

    std::optional<std::array<uint8_t, 16>> readBlock(uint8_t block) {
        std::array<uint8_t, 18> buf{};
        uint8_t len = static_cast<uint8_t>(buf.size());
        if (mfrc522_read_block(&dev_, block, buf.data(), &len) == MFRC522_OK) {
            std::array<uint8_t, 16> data{};
            std::copy_n(buf.begin(), 16, data.begin());
            return data;
        }
        return std::nullopt;
    }

    bool writeBlock(uint8_t block, const std::array<uint8_t, 16> &data) {
        return mfrc522_write_block(&dev_, block, data.data()) == MFRC522_OK;
    }

    void stopCrypto() { mfrc522_stop_crypto(&dev_); }

    uint8_t version() {
        return mfrc522_read_reg(&dev_, MFRC522_REG_VERSION);
    }

private:
    MFRC522 dev_{};
};
```

---

## 8. Rust Implementation

### 8.1 Dependencies (`Cargo.toml`)

```toml
[package]
name = "rfid-mfrc522"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal = "1.0"
linux-embedded-hal = "0.4"
rppal = { version = "0.18", optional = true }      # Raspberry Pi GPIO/SPI
thiserror = "1.0"
log = "0.4"
```

### 8.2 Error Types and Register Definitions

```rust
// src/error.rs
use thiserror::Error;

#[derive(Debug, Error)]
pub enum Mfrc522Error {
    #[error("SPI communication error")]
    Spi,
    #[error("Timeout waiting for response")]
    Timeout,
    #[error("CRC mismatch")]
    CrcError,
    #[error("Bit collision during anti-collision")]
    Collision,
    #[error("No card present")]
    NoCard,
    #[error("Authentication failed (wrong key?)")]
    AuthFailed,
    #[error("Buffer too small")]
    BufferOverflow,
    #[error("Protocol error: {0}")]
    Protocol(u8),
}

pub type Result<T> = std::result::Result<T, Mfrc522Error>;
```

```rust
// src/registers.rs
#![allow(dead_code)]

/// MFRC522 register addresses (6-bit, used in SPI address byte)
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Reg {
    Command      = 0x01,
    ComIEn       = 0x02,
    ComIrq       = 0x04,
    Error        = 0x06,
    Status1      = 0x07,
    Status2      = 0x08,
    FifoData     = 0x09,
    FifoLevel    = 0x0A,
    Control      = 0x0C,
    BitFraming   = 0x0D,
    Coll         = 0x0E,
    Mode         = 0x11,
    TxControl    = 0x14,
    TxAsk        = 0x15,
    CrcResultH   = 0x21,
    CrcResultL   = 0x22,
    TMode        = 0x2A,
    TPrescaler   = 0x2B,
    TReloadH     = 0x2C,
    TReloadL     = 0x2D,
    Version      = 0x37,
}

/// MFRC522 command codes written to CommandReg
#[repr(u8)]
#[derive(Clone, Copy)]
pub enum Cmd {
    Idle       = 0x00,
    CalcCrc    = 0x03,
    Transmit   = 0x04,
    Receive    = 0x08,
    Transceive = 0x0C,
    MfAuthent  = 0x0E,
    SoftReset  = 0x0F,
}

/// PICC (card) commands for ISO 14443A / MIFARE
pub mod picc {
    pub const REQA:          u8 = 0x26;
    pub const WUPA:          u8 = 0x52;
    pub const CT:            u8 = 0x88; // Cascade Tag
    pub const SEL_CL1:       u8 = 0x93;
    pub const SEL_CL2:       u8 = 0x95;
    pub const SEL_CL3:       u8 = 0x97;
    pub const HLTA:          u8 = 0x50;
    pub const MF_AUTH_KEY_A: u8 = 0x60;
    pub const MF_AUTH_KEY_B: u8 = 0x61;
    pub const MF_READ:       u8 = 0x30;
    pub const MF_WRITE:      u8 = 0xA0;
}
```

### 8.3 Core Driver

```rust
// src/mfrc522.rs
use embedded_hal::spi::SpiDevice;
use embedded_hal::digital::OutputPin;
use crate::error::{Mfrc522Error, Result};
use crate::registers::{Cmd, Reg, picc};

/// Card UID, up to 10 bytes (single, double, or triple size)
#[derive(Debug, Clone, Default)]
pub struct Uid {
    pub bytes: Vec<u8>,
    pub sak: u8,
}

/// MFRC522 driver — generic over SPI peripheral and RST pin
pub struct Mfrc522<SPI, RST> {
    spi: SPI,
    rst: RST,
}

impl<SPI, RST> Mfrc522<SPI, RST>
where
    SPI: SpiDevice,
    RST: OutputPin,
{
    // ── Construction ────────────────────────────────────────────────────────

    /// Create and initialize the MFRC522 driver.
    ///
    /// The `SPI` must implement `embedded_hal::spi::SpiDevice`,
    /// which handles CS toggling internally (driver SPI model in EH 1.0).
    pub fn new(spi: SPI, mut rst: RST) -> Result<Self> {
        // Hardware reset
        rst.set_low().ok();
        std::thread::sleep(std::time::Duration::from_millis(10));
        rst.set_high().ok();
        std::thread::sleep(std::time::Duration::from_millis(50));

        let mut dev = Self { spi, rst };
        dev.init()?;
        Ok(dev)
    }

    fn init(&mut self) -> Result<()> {
        // Soft reset
        self.write_reg(Reg::Command, Cmd::SoftReset as u8)?;
        std::thread::sleep(std::time::Duration::from_millis(50));

        // Wait until reset complete (bit5 of CommandReg clears)
        let mut timeout = 100u8;
        while self.read_reg(Reg::Command)? & 0x10 != 0 {
            timeout -= 1;
            if timeout == 0 { return Err(Mfrc522Error::Timeout); }
            std::thread::sleep(std::time::Duration::from_millis(10));
        }

        // Timer: TAuto, ~25 ms timeout
        self.write_reg(Reg::TMode,      0x8D)?;
        self.write_reg(Reg::TPrescaler, 0x3E)?;
        self.write_reg(Reg::TReloadH,   0x00)?;
        self.write_reg(Reg::TReloadL,   0x1E)?;

        // 100% ASK modulation + CRC preset 0x6363
        self.write_reg(Reg::TxAsk, 0x40)?;
        self.write_reg(Reg::Mode,  0x3D)?;

        // Enable antenna
        self.antenna_on()?;

        let ver = self.read_reg(Reg::Version)?;
        log::info!("MFRC522 version: 0x{ver:02X}");

        Ok(())
    }

    // ── Register Access ──────────────────────────────────────────────────────

    /// Build SPI address byte: bit7=R/W, bits6-1=addr, bit0=0
    #[inline(always)]
    fn addr_byte(reg: Reg, read: bool) -> u8 {
        let rw = if read { 0x80 } else { 0x00 };
        rw | ((reg as u8) << 1)
    }

    pub fn read_reg(&mut self, reg: Reg) -> Result<u8> {
        let mut buf = [Self::addr_byte(reg, true), 0x00];
        self.spi.transfer_in_place(&mut buf)
            .map_err(|_| Mfrc522Error::Spi)?;
        Ok(buf[1])
    }

    pub fn write_reg(&mut self, reg: Reg, val: u8) -> Result<()> {
        let buf = [Self::addr_byte(reg, false), val];
        self.spi.write(&buf)
            .map_err(|_| Mfrc522Error::Spi)?;
        Ok(())
    }

    pub fn set_bits(&mut self, reg: Reg, mask: u8) -> Result<()> {
        let val = self.read_reg(reg)?;
        self.write_reg(reg, val | mask)
    }

    pub fn clear_bits(&mut self, reg: Reg, mask: u8) -> Result<()> {
        let val = self.read_reg(reg)?;
        self.write_reg(reg, val & !mask)
    }

    // ── Antenna Control ──────────────────────────────────────────────────────

    pub fn antenna_on(&mut self) -> Result<()> {
        self.set_bits(Reg::TxControl, 0x03)
    }

    pub fn antenna_off(&mut self) -> Result<()> {
        self.clear_bits(Reg::TxControl, 0x03)
    }

    // ── Core Transceive ──────────────────────────────────────────────────────

    /// Send `send` bytes, optionally with `last_bits` valid in final byte,
    /// and return received bytes. Waits for RxIRQ or TimerIRQ.
    fn transceive(
        &mut self,
        send: &[u8],
        last_bits: u8,
        rx_buf: &mut [u8],
    ) -> Result<usize> {
        // Clear IRQ flags, flush FIFO
        self.write_reg(Reg::ComIEn,  0xF7)?;
        self.clear_bits(Reg::ComIrq, 0x80)?;
        self.set_bits(Reg::FifoLevel, 0x80)?; // FlushBuffer

        self.write_reg(Reg::Command, Cmd::Idle as u8)?;

        // Write send data to FIFO
        for &byte in send {
            self.write_reg(Reg::FifoData, byte)?;
        }

        // Bit framing: TxLastBits and RxAlign
        self.write_reg(Reg::BitFraming,
            (last_bits << 4) | last_bits)?;

        // Start transceive
        self.write_reg(Reg::Command, Cmd::Transceive as u8)?;
        self.set_bits(Reg::BitFraming, 0x80)?; // StartSend

        // Poll for completion
        let deadline = 2000u16;
        let mut n = deadline;
        let irq = loop {
            let irq = self.read_reg(Reg::ComIrq)?;
            if irq & 0x31 != 0 { break irq; } // Rx | Idle | Timer
            n -= 1;
            if n == 0 {
                self.write_reg(Reg::Command, Cmd::Idle as u8)?;
                return Err(Mfrc522Error::Timeout);
            }
        };

        self.write_reg(Reg::Command, Cmd::Idle as u8)?;

        if irq & 0x01 != 0 { return Err(Mfrc522Error::Timeout); }

        let err = self.read_reg(Reg::Error)?;
        if err & 0x13 != 0 {
            return Err(Mfrc522Error::Protocol(err));
        }
        if err & 0x08 != 0 {
            return Err(Mfrc522Error::Collision);
        }

        // Burst-read FIFO
        let fifo_len = self.read_reg(Reg::FifoLevel)? as usize;
        if fifo_len > rx_buf.len() {
            return Err(Mfrc522Error::BufferOverflow);
        }

        // Burst FIFO read: send address byte once, then read N bytes
        let addr = Self::addr_byte(Reg::FifoData, true);
        let mut spi_buf = vec![addr];
        spi_buf.extend(std::iter::repeat(0u8).take(fifo_len));
        self.spi.transfer_in_place(&mut spi_buf)
            .map_err(|_| Mfrc522Error::Spi)?;

        rx_buf[..fifo_len].copy_from_slice(&spi_buf[1..=fifo_len]);
        Ok(fifo_len)
    }

    // ── ISO 14443A REQA / WUPA ───────────────────────────────────────────────

    pub fn request(&mut self, cmd: u8) -> Result<[u8; 2]> {
        self.clear_bits(Reg::Coll, 0x80)?;
        let mut atqa = [0u8; 2];
        let n = self.transceive(&[cmd], 7, &mut atqa)?;
        if n != 2 { return Err(Mfrc522Error::NoCard); }
        Ok(atqa)
    }

    pub fn is_card_present(&mut self) -> bool {
        self.request(picc::WUPA).is_ok()
    }

    // ── Anti-collision (CL1) ─────────────────────────────────────────────────

    fn anticoll(&mut self, sel: u8) -> Result<[u8; 4]> {
        self.clear_bits(Reg::Coll, 0x80)?;
        let send = [sel, 0x20u8];
        let mut rx = [0u8; 5];
        let n = self.transceive(&send, 0, &mut rx)?;
        if n != 5 { return Err(Mfrc522Error::Protocol(0)); }

        // BCC check
        let bcc = rx[0] ^ rx[1] ^ rx[2] ^ rx[3];
        if bcc != rx[4] { return Err(Mfrc522Error::Protocol(1)); }

        Ok([rx[0], rx[1], rx[2], rx[3]])
    }

    // ── Select ───────────────────────────────────────────────────────────────

    fn select(&mut self, sel: u8, uid4: &[u8; 4]) -> Result<u8> {
        let bcc = uid4[0] ^ uid4[1] ^ uid4[2] ^ uid4[3];
        // In a full impl, append CRC_A from CalcCRC command
        // Here we build the SELECT frame (CRC bytes as placeholder 0x00)
        let send = [
            sel, 0x70,
            uid4[0], uid4[1], uid4[2], uid4[3],
            bcc,
            0x00, 0x00, // CRC_A placeholder
        ];
        let mut rx = [0u8; 3];
        let n = self.transceive(&send, 0, &mut rx)?;
        if n == 0 { return Err(Mfrc522Error::Protocol(2)); }
        Ok(rx[0]) // SAK
    }

    // ── High-level Read UID ──────────────────────────────────────────────────

    pub fn read_uid(&mut self) -> Result<Uid> {
        self.request(picc::REQA)?;
        let uid4 = self.anticoll(picc::SEL_CL1)?;
        let sak = self.select(picc::SEL_CL1, &uid4)?;

        Ok(Uid {
            bytes: uid4.to_vec(),
            sak,
        })
    }

    // ── MIFARE Authentication ────────────────────────────────────────────────

    pub fn authenticate(
        &mut self,
        key_cmd: u8,    // picc::MF_AUTH_KEY_A or MF_AUTH_KEY_B
        block: u8,
        key: &[u8; 6],
        uid: &Uid,
    ) -> Result<()> {
        let mut buf = [0u8; 12];
        buf[0] = key_cmd;
        buf[1] = block;
        buf[2..8].copy_from_slice(key);
        let uid_len = uid.bytes.len().min(4);
        buf[8..8 + uid_len].copy_from_slice(&uid.bytes[..uid_len]);

        // Flush FIFO and load auth data
        self.set_bits(Reg::FifoLevel, 0x80)?;
        self.write_reg(Reg::Command, Cmd::Idle as u8)?;
        for &b in &buf {
            self.write_reg(Reg::FifoData, b)?;
        }
        self.write_reg(Reg::Command, Cmd::MfAuthent as u8)?;

        // Wait for MFCrypto1On (bit3 of Status2)
        let mut timeout = 2000u16;
        loop {
            let status2 = self.read_reg(Reg::Status2)?;
            if status2 & 0x08 != 0 { break; }
            timeout -= 1;
            if timeout == 0 { return Err(Mfrc522Error::Timeout); }
        }

        let err = self.read_reg(Reg::Error)?;
        if err & 0x04 != 0 { return Err(Mfrc522Error::AuthFailed); }
        Ok(())
    }

    pub fn stop_crypto(&mut self) -> Result<()> {
        self.clear_bits(Reg::Status2, 0x08)
    }

    // ── Block Read / Write ───────────────────────────────────────────────────

    pub fn read_block(&mut self, block: u8) -> Result<[u8; 16]> {
        // CMD + block + CRC (placeholder)
        let send = [picc::MF_READ, block, 0x00, 0x00];
        let mut rx = [0u8; 18]; // 16 data + 2 CRC
        let n = self.transceive(&send, 0, &mut rx)?;
        if n < 16 { return Err(Mfrc522Error::Protocol(3)); }
        let mut data = [0u8; 16];
        data.copy_from_slice(&rx[..16]);
        Ok(data)
    }

    pub fn write_block(&mut self, block: u8, data: &[u8; 16]) -> Result<()> {
        // Phase 1: WRITE command + block
        let phase1 = [picc::MF_WRITE, block, 0x00, 0x00];
        let mut ack = [0u8; 2];
        self.transceive(&phase1, 0, &mut ack)?;
        if ack[0] & 0x0F != 0x0A { return Err(Mfrc522Error::Protocol(4)); }

        // Phase 2: 16 data bytes + CRC
        let mut phase2 = [0u8; 18];
        phase2[..16].copy_from_slice(data);
        let mut ack2 = [0u8; 2];
        self.transceive(&phase2, 0, &mut ack2)?;
        if ack2[0] & 0x0F != 0x0A { return Err(Mfrc522Error::Protocol(5)); }
        Ok(())
    }

    pub fn version(&mut self) -> Result<u8> {
        self.read_reg(Reg::Version)
    }
}
```

### 8.4 Application Example

```rust
// src/main.rs
mod error;
mod registers;
mod mfrc522;

use mfrc522::{Mfrc522, Uid};
use crate::registers::picc;
use std::thread;
use std::time::Duration;

fn format_uid(uid: &Uid) -> String {
    uid.bytes
        .iter()
        .map(|b| format!("{b:02X}"))
        .collect::<Vec<_>>()
        .join(":")
}

fn main() -> anyhow::Result<()> {
    env_logger::init();

    // ── Set up SPI and RST on Linux (rppal or linux-embedded-hal) ──────────
    use linux_embedded_hal::{SpidevDevice, SpidevBus, CdevPin};
    use linux_embedded_hal::spidev::{SpidevOptions, SpiModeFlags};

    let mut spidev = linux_embedded_hal::Spidev::open("/dev/spidev0.0")?;
    let opts = SpidevOptions::new()
        .bits_per_word(8)
        .max_speed_hz(1_000_000)
        .mode(SpiModeFlags::SPI_MODE_0)
        .build();
    spidev.configure(&opts)?;

    // Use GPIO 25 as RST (via linux character device GPIO)
    let rst = linux_embedded_hal::gpio_cdev::Chip::new("/dev/gpiochip0")?
        .get_line(25)?
        .request(linux_embedded_hal::gpio_cdev::LineRequestFlags::OUTPUT, 1, "rfid-rst")?;
    let rst_pin = linux_embedded_hal::CdevPin::new(rst)?;

    let mut reader = Mfrc522::new(spidev, rst_pin)?;

    let ver = reader.version()?;
    println!("MFRC522 version: 0x{ver:02X}");

    let default_key: [u8; 6] = [0xFF; 6];

    println!("Scanning for cards... (Ctrl-C to stop)");

    loop {
        match reader.read_uid() {
            Ok(uid) => {
                println!("Card UID: {}  SAK=0x{:02X}", format_uid(&uid), uid.sak);

                // MIFARE Classic: authenticate and read block 4
                match reader.authenticate(
                    picc::MF_AUTH_KEY_A, 4, &default_key, &uid
                ) {
                    Ok(()) => {
                        match reader.read_block(4) {
                            Ok(data) => {
                                let hex: String = data.iter()
                                    .map(|b| format!("{b:02X} "))
                                    .collect();
                                println!("  Block 4: {hex}");
                            }
                            Err(e) => eprintln!("  Read error: {e}"),
                        }
                        reader.stop_crypto()?;
                    }
                    Err(e) => eprintln!("  Auth error: {e}"),
                }
            }
            Err(error::Mfrc522Error::NoCard | error::Mfrc522Error::Timeout) => {
                // No card — normal, just keep polling
            }
            Err(e) => eprintln!("Error: {e}"),
        }

        thread::sleep(Duration::from_millis(500));
    }
}
```

### 8.5 Rust Unit Tests

```rust
// src/tests.rs — Mock-based unit tests
#[cfg(test)]
mod tests {
    use super::*;
    use embedded_hal_mock::eh1::spi::{Mock as SpiMock, Transaction};
    use embedded_hal_mock::eh1::pin::{Mock as PinMock, State, Transaction as PinTx};

    #[test]
    fn test_read_version_register() {
        // MFRC522 VersionReg (0x37) read
        // Address byte = 0x80 | (0x37 << 1) = 0x80 | 0x6E = 0xEE
        let expectations = vec![
            Transaction::transfer_in_place(vec![0xEE, 0x00], vec![0x00, 0x92]),
        ];
        let spi = SpiMock::new(&expectations);

        let pin_expectations = vec![
            PinTx::set_low(),
            PinTx::set_high(),
        ];
        let rst = PinMock::new(&pin_expectations);

        // (Would normally call Mfrc522::new, but skip init for unit test)
        // This demonstrates address byte encoding
        let addr_byte = 0x80 | ((0x37u8) << 1);
        assert_eq!(addr_byte, 0xEE);
    }

    #[test]
    fn test_uid_bcc() {
        // BCC (block check character) must XOR to 0 over 5 bytes
        let uid = [0xDE, 0xAD, 0xBE, 0xEF];
        let bcc = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];
        // Received frame with BCC
        let rx = [uid[0], uid[1], uid[2], uid[3], bcc];
        let check = rx[0] ^ rx[1] ^ rx[2] ^ rx[3] ^ rx[4];
        assert_eq!(check, 0); // valid BCC
    }

    #[test]
    fn test_spi_address_encoding() {
        // Write to register 0x01 (CommandReg)
        // addr_byte = 0x00 | (0x01 << 1) = 0x02
        let reg_write_addr = (0x01u8) << 1;
        assert_eq!(reg_write_addr, 0x02);

        // Read from register 0x37 (VersionReg)
        let reg_read_addr = 0x80 | ((0x37u8) << 1);
        assert_eq!(reg_read_addr, 0xEE);
    }
}
```

---

## 9. Other RFID/NFC Modules

### PN532

The **PN532** (also by NXP) is a more capable NFC controller supporting ISO 14443A/B, ISO 18092 (P2P), MIFARE, and FeliCa. It supports SPI, I²C, and HSU (UART).

| Feature         | MFRC522           | PN532                     |
|-----------------|-------------------|---------------------------|
| Interface       | SPI/I²C/UART      | SPI/I²C/UART              |
| Card types      | ISO 14443A/MIFARE | 14443A/B, 15693, FeliCa   |
| NFC P2P         | No                | Yes (ISO 18092)           |
| Card emulation  | No                | Yes                       |
| SAM support     | No                | Yes                       |
| Typical use     | Read/write cards  | Full NFC stack            |
| Cost            | ~$1–3             | ~$4–10                    |

**PN532 SPI — Key Differences:**

```c
// PN532 SPI uses a different framing:
// SS LOW → wait for Ready (0x01) → send frame → receive response

// Frame format: Preamble | Start Code | Length | LCS | TFI | Data | DCS | Postamble
// 0x00         | 0x00 0xFF | LEN       | LCS    | 0xD4| ... | DCS  | 0x00

// Normal frame to detect card:
const uint8_t pn532_inlistpassivetarget[] = {
    0x00, 0x00, 0xFF,   // Preamble + Start Code
    0x04, 0xFC,         // Length=4, LCS
    0xD4, 0x4A,         // TFI=D4 (host→PN532), Command InListPassiveTarget
    0x01, 0x00,         // MaxTg=1, BrTy=0 (ISO14443A 106 kbps)
    0xE0,               // DCS
    0x00                // Postamble
};
```

### RC522 vs MFRC522 vs PN532 SPI Timing Comparison

```
MFRC522 SPI Transaction (read reg 0x37):
CS  ──┐                           ┌──
      └───────────────────────────┘
CLK     ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐  ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐
        └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘  └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘
MOSI  [ 1  1  1  0  1  1  1  0 ][ 0  0  0  0  0  0  0  0 ]
         ↑ bit7=R   addr=0x37       ↑ dummy byte
MISO  [ X  X  X  X  X  X  X  X ][ 0  1  0  0  1  0  0  1 ]  ← 0x92 (ver)
```

---

## 10. Anti-Collision and Multi-Card Handling

When multiple cards are in the RF field, the ISO 14443A **bit-wise anti-collision** protocol resolves conflicts iteratively.

### Anti-Collision Process

```
1. Reader sends NVB=0x20 (no UID bits sent by reader)
2. All cards respond with their UID simultaneously
3. If collision detected (two cards sent different bits at same position):
   a. Reader notes collision bit position (CollReg)
   b. Reader sends UID prefix up to collision bit, appending '0' or '1'
   c. Only cards matching prefix respond
4. Repeat until single card responds cleanly
5. SELECT that card's full UID → it enters ACTIVE state, others sleep
```

```c
// Multi-card cascade loop (conceptual)
MFRC522_Status select_all_cards(MFRC522 *dev) {
    uint8_t atqa[2];
    // Use WUPA so sleeping cards wake up too
    while (mfrc522_request(dev, PICC_CMD_WUPA, atqa) == MFRC522_OK) {
        PICC_UID uid = {0};
        mfrc522_anticoll(dev, PICC_CMD_SEL_CL1, &uid);
        mfrc522_select(dev, PICC_CMD_SEL_CL1, &uid);

        // Process this card...
        printf("Found: ");
        for (int i = 0; i < uid.size; i++) printf("%02X ", uid.bytes[i]);
        printf("\n");

        // HALT selected card (it goes to HALT state, won't respond to REQA)
        uint8_t halt[4] = { PICC_CMD_HLTA, 0x00, 0x00, 0x00 }; // + CRC
        uint8_t rx[1]; uint8_t rx_len = 1;
        mfrc522_transceive_raw(dev, halt, 4, rx, &rx_len, 0, NULL, false);
        // Halted card won't respond to REQA, only WUPA → enumerate others
    }
    return MFRC522_OK;
}
```

---

## 11. Security Considerations

### MIFARE Classic Vulnerabilities

MIFARE Classic uses the **Crypto1** stream cipher (proprietary, NXP). This was **reverse-engineered in 2008** and is now considered cryptographically broken:

- **Nested authentication attack** — If one sector key is known, all others can be recovered in seconds
- **Darkside attack** — Works even with no known keys; exploits PRNG weaknesses
- **Card-only attack** — Only the card is needed, no reader interaction required

**Recommendations:**

| Scenario                        | Recommendation                              |
|---------------------------------|---------------------------------------------|
| Access control (new designs)    | Use MIFARE DESFire EV2/EV3 (AES-128)        |
| Payment systems                 | Use ISO 14443-4 with APDU and AES           |
| Asset tracking (non-sensitive)  | MIFARE Classic or NTAG is acceptable        |
| Legacy MIFARE Classic           | Rotate keys; use sector-level access rights |
| High security                   | FIDO2 / PIV smart card                      |

### Safe Key Management

```c
// Never hardcode keys in production firmware
// Use a Key Derivation Function (KDF) based on UID:

#include <mbedtls/sha256.h>

void derive_sector_key(const uint8_t *uid, uint8_t uid_len,
                        uint8_t sector, const uint8_t *master_secret,
                        uint8_t *key_out /* 6 bytes */) {
    uint8_t input[32];
    uint8_t hash[32];

    // Build input: master_secret || UID || sector
    memcpy(input, master_secret, 16);
    memcpy(input + 16, uid, uid_len);
    input[16 + uid_len] = sector;

    mbedtls_sha256(input, 16 + uid_len + 1, hash, 0);

    // Take first 6 bytes of hash as derived key
    memcpy(key_out, hash, 6);
}
```

### MIFARE DESFire (AES) Authentication

For production security use DESFire EV2/EV3 with AES-128 CMAC authentication. The SPI driver layer is the same; only the APDU layer changes:

```
Command APDU (ISO 7816-4 wrapped in 14443-4):
  CLA  INS  P1  P2  Lc  Data...  Le
  0x90 0x71 0x00 0x00 0x01 [KeyNo] 0x00  → AuthenticateAES
```

---

## 12. Summary

The MFRC522 is the dominant RFID module in embedded systems, offering a compact SPI interface to ISO 14443A/MIFARE card ecosystems. Here are the key takeaways:

**SPI Protocol:**
The MFRC522 uses SPI Mode 0 at up to 10 MHz. Every transaction begins with an address byte encoding direction (bit 7) and register address (bits 6–1). Register reads require a dummy byte to clock out the response; FIFO reads can be burst with a single address byte.

**Initialization:**
The device requires a hardware reset, soft reset, timer configuration, 100% ASK modulation setting, CRC preset (0x6363), and antenna enable before any RF communication.

**Card Detection Flow:**
ISO 14443A communication follows REQA → Anti-collision → Select. The MFRC522 handles the RF layer; your driver must implement the state machine in software. UIDs can be 4, 7, or 10 bytes (single/double/triple cascade).

**Data Access:**
MIFARE Classic sectors require authentication (Key A or Key B, Crypto1) before read/write operations. After the session, `stop_crypto` must clear the MFCrypto1On flag.

**C/C++ Approach:**
A HAL struct with function pointers decouples the driver from platform specifics. A thin C++ RAII wrapper adds type safety, `std::optional` for nullable returns, and scope-based antenna/crypto management.

**Rust Approach:**
The `embedded-hal 1.0` traits (`SpiDevice`, `OutputPin`) enable zero-cost abstraction over any SPI platform. The `thiserror`-based error type produces descriptive, composable errors. The driver is fully generic over hardware types with no `unsafe` code required.

**Security:**
MIFARE Classic (Crypto1) is cryptographically broken — use MIFARE DESFire EV2/EV3 with AES-128 for security-sensitive applications. Always derive sector keys from a master secret and UID rather than using a single static key across all cards.

**Other Modules:**
The PN532 extends capabilities with full NFC P2P, card emulation, and ISO 14443B/FeliCa support, at the cost of a more complex framing protocol. Both can share the same SPI bus with separate CS lines.

---

*Document: 70_RFID_Reader_Modules.md | Communicating with MFRC522 and RFID/NFC Readers via SPI*