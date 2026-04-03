# 67. Wireless Modules (NRF24, LoRa)

- **SPI Fundamentals** — signal roles, the CSN/MISO/MOSI/CE pinout, and the general transaction pattern common to both modules
- **NRF24L01+** — full register map, command set, a complete C/C++ driver (`nrf24l01.h`) with init/TX/RX functions, and an equivalent Rust driver built on `embedded-hal` traits
- **LoRa SX127x** — register map, the address-encodes-RW-direction protocol (vs NRF24's command byte approach), a complete C driver (`lora_sx127x.h`) including frequency calculation (`Frf = freq × 2¹⁹ / 32MHz`), and a full Rust implementation
- **Advanced Topics** — interrupt-driven GPIO IRQ handlers for both modules, DMA-backed SPI burst reads (STM32 HAL example), and power management with sleep/wake patterns including a Rust `RadioPower` trait
- **Comparison table** — NRF24 vs LoRa across frequency, range, data rate, current draw, cost, and use-case fit
- **Summary** — distilling the 5-step programming model and the key SPI protocol differences between the two families

## Controlling Radio Transceivers Through SPI Command Interfaces

---

## Table of Contents

1. [Introduction](#introduction)
2. [SPI Fundamentals for Wireless Modules](#spi-fundamentals-for-wireless-modules)
3. [NRF24L01+ Module](#nrf24l01-module)
   - [Architecture & Register Map](#nrf24l01-architecture--register-map)
   - [SPI Command Protocol](#nrf24l01-spi-command-protocol)
   - [C/C++ Implementation](#nrf24l01-cc-implementation)
   - [Rust Implementation](#nrf24l01-rust-implementation)
4. [LoRa (SX1276/SX1278) Module](#lora-sx1276sx1278-module)
   - [Architecture & Register Map](#lora-architecture--register-map)
   - [SPI Command Protocol](#lora-spi-command-protocol)
   - [C/C++ Implementation](#lora-cc-implementation)
   - [Rust Implementation](#lora-rust-implementation)
5. [Advanced Topics](#advanced-topics)
   - [Interrupt-Driven Reception](#interrupt-driven-reception)
   - [DMA-Accelerated SPI Transfers](#dma-accelerated-spi-transfers)
   - [Power Management](#power-management)
6. [Comparison: NRF24 vs LoRa](#comparison-nrf24-vs-lora)
7. [Summary](#summary)

---

## Introduction

Radio transceivers like the **NRF24L01+** and **LoRa (SX127x)** family are among the most widely deployed wireless communication modules in embedded systems. Both are controlled entirely through a **SPI (Serial Peripheral Interface)** command interface, making them excellent examples of how the SPI protocol is leveraged to configure and operate complex RF hardware.

- **NRF24L01+** (Nordic Semiconductor): A 2.4 GHz ISM-band transceiver operating at data rates up to 2 Mbps. Common in short-range (≤100 m) applications such as keyboards, game controllers, and sensor networks.
- **LoRa SX127x** (Semtech): A sub-GHz (433/868/915 MHz) transceiver using Chirp Spread Spectrum (CSS) modulation, capable of multi-km range at the cost of lower data rates (0.3–37.5 kbps). Preferred for IoT, smart agriculture, and metering.

Both modules present an identical programming model to the host MCU: **read/write registers over SPI** to configure frequency, power, modulation parameters, and FIFOs, then assert/de-assert chip select to initiate transactions.

---

## SPI Fundamentals for Wireless Modules

Both NRF24 and LoRa modules use **SPI Mode 0** (CPOL=0, CPHA=0) with the following signal roles:

| Signal | Direction | Purpose |
|--------|-----------|---------|
| SCK    | Master → Slave | Clock |
| MOSI   | Master → Slave | Commands & write data |
| MISO   | Slave → Master | Status byte & read data |
| CSN/NSS | Master → Slave | Chip select (active low) |
| CE/RESET | Master → Slave | Module enable / reset (GPIO) |
| IRQ    | Slave → Master | Interrupt (optional) |

**General SPI transaction pattern** for both devices:

```
CSN LOW
  → send command byte (R_REGISTER / W_REGISTER / etc.)
  ← receive status byte (simultaneously on MISO)
  → send / receive data byte(s)
CSN HIGH
```

The **status byte** is returned on MISO during the command byte transfer on MOSI. This is a fundamental property exploited in efficient driver implementations.

---

## NRF24L01+ Module

### NRF24L01+ Architecture & Register Map

The NRF24L01+ contains:
- A 5-stage SPI register bank (addresses 0x00–0x1D)
- Two FIFO stacks (TX and RX, 3 levels × 32 bytes each)
- 6 data pipes with individual addresses and auto-acknowledgment
- Built-in Enhanced ShockBurst™ protocol (auto-ACK, auto-retransmit)

**Key Registers:**

| Address | Name     | Description |
|---------|----------|-------------|
| 0x00    | CONFIG   | Power mode, CRC, role (RX/TX) |
| 0x01    | EN_AA    | Auto-acknowledgment per pipe |
| 0x02    | EN_RXADDR| Enable RX addresses |
| 0x03    | SETUP_AW | Address width (3–5 bytes) |
| 0x04    | SETUP_RETR | Retransmit delay/count |
| 0x05    | RF_CH    | RF channel (0–125, freq = 2400+CH MHz) |
| 0x06    | RF_SETUP | Data rate, TX power |
| 0x07    | STATUS   | IRQ flags, TX/RX FIFO status |
| 0x0A–0x0F | RX_ADDR_P0–P5 | Receive pipe addresses |
| 0x10    | TX_ADDR  | Transmit address |
| 0x11–0x16 | RX_PW_Px | Payload widths per pipe |
| 0x17    | FIFO_STATUS | FIFO levels |
| 0x1C    | DYNPD    | Dynamic payload per pipe |
| 0x1D    | FEATURE  | Extended features enable |

### NRF24L01+ SPI Command Protocol

| Command | Byte | Description |
|---------|------|-------------|
| R_REGISTER | 0b000AAAAA | Read register at address A |
| W_REGISTER | 0b001AAAAA | Write register at address A |
| R_RX_PAYLOAD | 0x61 | Read RX payload (1–32 bytes) |
| W_TX_PAYLOAD | 0xA0 | Write TX payload (1–32 bytes) |
| FLUSH_TX | 0xE1 | Flush TX FIFO |
| FLUSH_RX | 0xE2 | Flush RX FIFO |
| REUSE_TX_PL | 0xE3 | Reuse last TX payload |
| NOP | 0xFF | No operation (read STATUS only) |

---

### NRF24L01+ C/C++ Implementation

```c
/*
 * nrf24l01.h - NRF24L01+ SPI Driver (C/C++)
 * Platform-agnostic; adapt spi_transfer() to your HAL.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ── Register Addresses ─────────────────────────────────────────── */
#define NRF_CONFIG      0x00
#define NRF_EN_AA       0x01
#define NRF_EN_RXADDR   0x02
#define NRF_SETUP_AW    0x03
#define NRF_SETUP_RETR  0x04
#define NRF_RF_CH       0x05
#define NRF_RF_SETUP    0x06
#define NRF_STATUS      0x07
#define NRF_RX_ADDR_P0  0x0A
#define NRF_TX_ADDR     0x10
#define NRF_RX_PW_P0    0x11
#define NRF_FIFO_STATUS 0x17
#define NRF_DYNPD       0x1C
#define NRF_FEATURE     0x1D

/* ── Commands ───────────────────────────────────────────────────── */
#define CMD_R_REGISTER    0x00   /* OR with 5-bit register address */
#define CMD_W_REGISTER    0x20   /* OR with 5-bit register address */
#define CMD_R_RX_PAYLOAD  0x61
#define CMD_W_TX_PAYLOAD  0xA0
#define CMD_FLUSH_TX      0xE1
#define CMD_FLUSH_RX      0xE2
#define CMD_NOP           0xFF

/* ── CONFIG bits ────────────────────────────────────────────────── */
#define CFG_PRIM_RX   (1 << 0)  /* 1 = RX, 0 = TX */
#define CFG_PWR_UP    (1 << 1)
#define CFG_CRC_2BYTE (1 << 2)
#define CFG_EN_CRC    (1 << 3)

/* ── STATUS bits ────────────────────────────────────────────────── */
#define STATUS_RX_DR  (1 << 6)  /* Data Ready (RX) */
#define STATUS_TX_DS  (1 << 5)  /* Data Sent (TX) */
#define STATUS_MAX_RT (1 << 4)  /* Max Retransmits */

/* ── Platform interface — implement these for your HAL ─────────── */
extern void     nrf_csn_low(void);
extern void     nrf_csn_high(void);
extern void     nrf_ce_low(void);
extern void     nrf_ce_high(void);
extern uint8_t  nrf_spi_byte(uint8_t out);   /* full-duplex single byte */
extern void     nrf_delay_us(uint32_t us);

/* ─────────────────────────────────────────────────────────────────
 * Low-level helpers
 * ───────────────────────────────────────────────────────────────── */

/**
 * Transfer a buffer over SPI while CSN is held low.
 * Returns the status byte received during the command byte phase.
 */
static inline uint8_t nrf_transfer(const uint8_t *tx, uint8_t *rx,
                                   uint8_t len)
{
    nrf_csn_low();
    uint8_t status = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t b = nrf_spi_byte(tx ? tx[i] : CMD_NOP);
        if (i == 0) status = b;          /* first byte returned = STATUS */
        if (rx)     rx[i]  = b;
    }
    nrf_csn_high();
    return status;
}

/** Read a single 1-byte register. */
static inline uint8_t nrf_read_reg(uint8_t reg)
{
    uint8_t cmd[2] = { (uint8_t)(CMD_R_REGISTER | (reg & 0x1F)), CMD_NOP };
    uint8_t buf[2];
    nrf_transfer(cmd, buf, 2);
    return buf[1];
}

/** Write a single 1-byte register. Returns STATUS. */
static inline uint8_t nrf_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t cmd[2] = { (uint8_t)(CMD_W_REGISTER | (reg & 0x1F)), val };
    return nrf_transfer(cmd, NULL, 2);
}

/** Write a multi-byte register (e.g. address registers). */
static void nrf_write_reg_buf(uint8_t reg, const uint8_t *buf, uint8_t len)
{
    nrf_csn_low();
    nrf_spi_byte(CMD_W_REGISTER | (reg & 0x1F));
    for (uint8_t i = 0; i < len; i++) nrf_spi_byte(buf[i]);
    nrf_csn_high();
}

/* ─────────────────────────────────────────────────────────────────
 * Initialization
 * ───────────────────────────────────────────────────────────────── */

/**
 * nrf_init_tx() - Configure module as a transmitter.
 * @channel : RF channel 0–125 (freq = 2400 + channel MHz)
 * @addr    : 5-byte TX (and pipe-0 RX) address for auto-ACK
 * @payload_len : fixed payload length in bytes (1–32)
 */
void nrf_init_tx(uint8_t channel, const uint8_t addr[5], uint8_t payload_len)
{
    nrf_ce_low();

    /* Power up, 2-byte CRC, TX mode */
    nrf_write_reg(NRF_CONFIG, CFG_PWR_UP | CFG_EN_CRC | CFG_CRC_2BYTE);
    nrf_delay_us(1500);          /* tpd2stby: 1.5 ms power-up delay */

    nrf_write_reg(NRF_EN_AA,       0x01);   /* Auto-ACK on pipe 0 */
    nrf_write_reg(NRF_EN_RXADDR,   0x01);   /* Enable pipe 0 */
    nrf_write_reg(NRF_SETUP_AW,    0x03);   /* 5-byte addresses */
    nrf_write_reg(NRF_SETUP_RETR,  0x1F);   /* 500 µs delay, 15 retries */
    nrf_write_reg(NRF_RF_CH,       channel);
    nrf_write_reg(NRF_RF_SETUP,    0x07);   /* 1 Mbps, 0 dBm */

    /* Pipe-0 RX address must match TX address for auto-ACK */
    nrf_write_reg_buf(NRF_RX_ADDR_P0, addr, 5);
    nrf_write_reg_buf(NRF_TX_ADDR,    addr, 5);

    nrf_write_reg(NRF_RX_PW_P0, payload_len);

    /* Clear any stale IRQ flags */
    nrf_write_reg(NRF_STATUS, STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT);

    /* Flush FIFOs */
    uint8_t flush_tx = CMD_FLUSH_TX;
    nrf_transfer(&flush_tx, NULL, 1);
    uint8_t flush_rx = CMD_FLUSH_RX;
    nrf_transfer(&flush_rx, NULL, 1);
}

/* ─────────────────────────────────────────────────────────────────
 * Transmit
 * ───────────────────────────────────────────────────────────────── */

typedef enum {
    NRF_TX_OK,
    NRF_TX_MAX_RT,   /* max retransmits — ACK never received */
    NRF_TX_ERROR
} nrf_tx_result_t;

/**
 * nrf_send() - Blocking transmit with auto-ACK check.
 * @payload : data to send
 * @len     : number of bytes (1–32)
 * Returns NRF_TX_OK on success, NRF_TX_MAX_RT if ACK timeout.
 */
nrf_tx_result_t nrf_send(const uint8_t *payload, uint8_t len)
{
    /* Load payload into TX FIFO */
    nrf_csn_low();
    nrf_spi_byte(CMD_W_TX_PAYLOAD);
    for (uint8_t i = 0; i < len; i++) nrf_spi_byte(payload[i]);
    nrf_csn_high();

    /* Pulse CE ≥ 10 µs to start transmission */
    nrf_ce_high();
    nrf_delay_us(15);
    nrf_ce_low();

    /* Poll STATUS for TX_DS (sent) or MAX_RT (failed) */
    uint32_t timeout = 200000;   /* ~200 ms timeout */
    while (--timeout) {
        uint8_t status = nrf_read_reg(NRF_STATUS);
        if (status & STATUS_TX_DS) {
            nrf_write_reg(NRF_STATUS, STATUS_TX_DS);
            return NRF_TX_OK;
        }
        if (status & STATUS_MAX_RT) {
            nrf_write_reg(NRF_STATUS, STATUS_MAX_RT);
            uint8_t flush = CMD_FLUSH_TX;
            nrf_transfer(&flush, NULL, 1);
            return NRF_TX_MAX_RT;
        }
        nrf_delay_us(1);
    }
    return NRF_TX_ERROR;
}

/* ─────────────────────────────────────────────────────────────────
 * Receive
 * ───────────────────────────────────────────────────────────────── */

/**
 * nrf_init_rx() - Switch module to receiver mode.
 * @channel : RF channel
 * @addr    : 5-byte pipe-0 address
 * @payload_len : expected payload width in bytes
 */
void nrf_init_rx(uint8_t channel, const uint8_t addr[5], uint8_t payload_len)
{
    nrf_ce_low();
    /* Same as TX init, but set PRIM_RX bit */
    nrf_write_reg(NRF_CONFIG,
                  CFG_PWR_UP | CFG_EN_CRC | CFG_CRC_2BYTE | CFG_PRIM_RX);
    nrf_delay_us(1500);
    nrf_write_reg(NRF_EN_AA,     0x01);
    nrf_write_reg(NRF_EN_RXADDR, 0x01);
    nrf_write_reg(NRF_SETUP_AW,  0x03);
    nrf_write_reg(NRF_RF_CH,     channel);
    nrf_write_reg(NRF_RF_SETUP,  0x07);
    nrf_write_reg_buf(NRF_RX_ADDR_P0, addr, 5);
    nrf_write_reg(NRF_RX_PW_P0, payload_len);
    nrf_write_reg(NRF_STATUS, STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT);
    uint8_t flush = CMD_FLUSH_RX;
    nrf_transfer(&flush, NULL, 1);

    nrf_ce_high();               /* CE HIGH = listening */
}

/**
 * nrf_data_ready() - Returns true if at least one packet is in RX FIFO.
 */
bool nrf_data_ready(void)
{
    return (nrf_read_reg(NRF_STATUS) & STATUS_RX_DR) != 0;
}

/**
 * nrf_read_payload() - Read one packet from RX FIFO.
 * @buf : destination buffer (must be ≥ payload_len bytes)
 * @len : expected payload length
 */
void nrf_read_payload(uint8_t *buf, uint8_t len)
{
    nrf_csn_low();
    nrf_spi_byte(CMD_R_RX_PAYLOAD);
    for (uint8_t i = 0; i < len; i++) buf[i] = nrf_spi_byte(CMD_NOP);
    nrf_csn_high();

    /* Clear RX_DR interrupt flag */
    nrf_write_reg(NRF_STATUS, STATUS_RX_DR);
}
```

**Usage example (C):**

```c
#include "nrf24l01.h"
#include <string.h>

/* STM32 HAL platform bindings (example) */
void     nrf_csn_low(void)             { HAL_GPIO_WritePin(NRF_CSN_GPIO, NRF_CSN_PIN, GPIO_PIN_RESET); }
void     nrf_csn_high(void)            { HAL_GPIO_WritePin(NRF_CSN_GPIO, NRF_CSN_PIN, GPIO_PIN_SET);   }
void     nrf_ce_low(void)              { HAL_GPIO_WritePin(NRF_CE_GPIO,  NRF_CE_PIN,  GPIO_PIN_RESET); }
void     nrf_ce_high(void)             { HAL_GPIO_WritePin(NRF_CE_GPIO,  NRF_CE_PIN,  GPIO_PIN_SET);   }
void     nrf_delay_us(uint32_t us)     { /* microsecond delay via DWT or timer */ }
uint8_t  nrf_spi_byte(uint8_t out) {
    uint8_t in;
    HAL_SPI_TransmitReceive(&hspi1, &out, &in, 1, 10);
    return in;
}

/* Transmitter main */
int main_tx(void) {
    const uint8_t addr[5] = { 0xE7, 0xE7, 0xE7, 0xE7, 0xE7 };
    nrf_init_tx(76, addr, 8);          /* channel 76 = 2476 MHz */

    uint8_t msg[8] = { 'H','e','l','l','o','!',0,0 };

    while (1) {
        nrf_tx_result_t r = nrf_send(msg, 8);
        if (r == NRF_TX_OK)     { /* success */ }
        else if (r == NRF_TX_MAX_RT) { /* no receiver found */ }
        HAL_Delay(1000);
    }
}

/* Receiver main */
int main_rx(void) {
    const uint8_t addr[5] = { 0xE7, 0xE7, 0xE7, 0xE7, 0xE7 };
    nrf_init_rx(76, addr, 8);

    uint8_t buf[8];
    while (1) {
        if (nrf_data_ready()) {
            nrf_read_payload(buf, 8);
            /* process buf */
        }
    }
}
```

---

### NRF24L01+ Rust Implementation

```rust
// nrf24l01.rs — NRF24L01+ driver using embedded-hal traits

use embedded_hal::digital::v2::OutputPin;
use embedded_hal::blocking::spi::Transfer;
use embedded_hal::blocking::delay::DelayUs;

/// NRF24L01+ register addresses
mod reg {
    pub const CONFIG:      u8 = 0x00;
    pub const EN_AA:       u8 = 0x01;
    pub const EN_RXADDR:   u8 = 0x02;
    pub const SETUP_AW:    u8 = 0x03;
    pub const SETUP_RETR:  u8 = 0x04;
    pub const RF_CH:       u8 = 0x05;
    pub const RF_SETUP:    u8 = 0x06;
    pub const STATUS:      u8 = 0x07;
    pub const RX_ADDR_P0:  u8 = 0x0A;
    pub const TX_ADDR:     u8 = 0x10;
    pub const RX_PW_P0:    u8 = 0x11;
    pub const FIFO_STATUS: u8 = 0x17;
}

/// SPI command bytes
mod cmd {
    pub const R_REGISTER:   u8 = 0x00;   // OR with register address
    pub const W_REGISTER:   u8 = 0x20;   // OR with register address
    pub const R_RX_PAYLOAD: u8 = 0x61;
    pub const W_TX_PAYLOAD: u8 = 0xA0;
    pub const FLUSH_TX:     u8 = 0xE1;
    pub const FLUSH_RX:     u8 = 0xE2;
    pub const NOP:          u8 = 0xFF;
}

pub const CFG_PWR_UP:    u8 = 1 << 1;
pub const CFG_PRIM_RX:   u8 = 1 << 0;
pub const CFG_EN_CRC:    u8 = 1 << 3;
pub const CFG_CRC_2BYTE: u8 = 1 << 2;
pub const STATUS_RX_DR:  u8 = 1 << 6;
pub const STATUS_TX_DS:  u8 = 1 << 5;
pub const STATUS_MAX_RT: u8 = 1 << 4;

/// Error type
#[derive(Debug)]
pub enum Nrf24Error<SpiE, PinE> {
    Spi(SpiE),
    Pin(PinE),
    MaxRetransmits,
    Timeout,
}

/// Result alias
pub type Result<T, SpiE, PinE> = core::result::Result<T, Nrf24Error<SpiE, PinE>>;

/// NRF24L01+ driver
pub struct Nrf24<SPI, CSN, CE, DELAY> {
    spi:   SPI,
    csn:   CSN,
    ce:    CE,
    delay: DELAY,
}

impl<SPI, CSN, CE, DELAY, SpiE, PinE> Nrf24<SPI, CSN, CE, DELAY>
where
    SPI:   Transfer<u8, Error = SpiE>,
    CSN:   OutputPin<Error = PinE>,
    CE:    OutputPin<Error = PinE>,
    DELAY: DelayUs<u32>,
{
    /// Construct a new driver instance.
    pub fn new(spi: SPI, csn: CSN, ce: CE, delay: DELAY) -> Self {
        Self { spi, csn, ce, delay }
    }

    // ── Low-level SPI helpers ────────────────────────────────────

    /// Assert CSN, transfer `buf` in-place (TX→RX), deassert CSN.
    /// Returns the STATUS byte (first byte clocked back on MISO).
    fn transfer(&mut self, buf: &mut [u8]) -> Result<u8, SpiE, PinE> {
        self.csn.set_low().map_err(Nrf24Error::Pin)?;
        let result = self.spi.transfer(buf);
        self.csn.set_high().map_err(Nrf24Error::Pin)?;
        let status = result.map_err(Nrf24Error::Spi)?[0];
        Ok(status)
    }

    /// Read a single-byte register.
    pub fn read_reg(&mut self, reg: u8) -> Result<u8, SpiE, PinE> {
        let mut buf = [cmd::R_REGISTER | (reg & 0x1F), cmd::NOP];
        self.transfer(&mut buf)?;
        Ok(buf[1])
    }

    /// Write a single-byte register.
    pub fn write_reg(&mut self, reg: u8, val: u8) -> Result<u8, SpiE, PinE> {
        let mut buf = [cmd::W_REGISTER | (reg & 0x1F), val];
        self.transfer(&mut buf)
    }

    /// Write a multi-byte register (e.g., address fields).
    pub fn write_reg_buf(&mut self, reg: u8, data: &[u8]) -> Result<(), SpiE, PinE> {
        self.csn.set_low().map_err(Nrf24Error::Pin)?;
        let cmd_byte = cmd::W_REGISTER | (reg & 0x1F);
        // Transfer command byte first, then data bytes
        let mut cmd_buf = [cmd_byte];
        self.spi.transfer(&mut cmd_buf).map_err(Nrf24Error::Spi)?;
        let mut data_copy = [0u8; 5];
        data_copy[..data.len()].copy_from_slice(data);
        self.spi.transfer(&mut data_copy[..data.len()]).map_err(Nrf24Error::Spi)?;
        self.csn.set_high().map_err(Nrf24Error::Pin)?;
        Ok(())
    }

    // ── Initialization ───────────────────────────────────────────

    /// Initialize as transmitter.
    pub fn init_tx(&mut self, channel: u8, addr: &[u8; 5], payload_len: u8)
        -> Result<(), SpiE, PinE>
    {
        self.ce.set_low().map_err(Nrf24Error::Pin)?;

        self.write_reg(reg::CONFIG, CFG_PWR_UP | CFG_EN_CRC | CFG_CRC_2BYTE)?;
        self.delay.delay_us(1500);

        self.write_reg(reg::EN_AA,      0x01)?;
        self.write_reg(reg::EN_RXADDR,  0x01)?;
        self.write_reg(reg::SETUP_AW,   0x03)?;   // 5-byte addresses
        self.write_reg(reg::SETUP_RETR, 0x1F)?;   // 500 µs, 15 retries
        self.write_reg(reg::RF_CH,      channel)?;
        self.write_reg(reg::RF_SETUP,   0x07)?;   // 1 Mbps, 0 dBm

        self.write_reg_buf(reg::RX_ADDR_P0, addr)?;
        self.write_reg_buf(reg::TX_ADDR,    addr)?;
        self.write_reg(reg::RX_PW_P0, payload_len)?;

        // Clear IRQ flags
        self.write_reg(reg::STATUS, STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT)?;

        let mut flush = [cmd::FLUSH_TX];
        self.transfer(&mut flush)?;
        let mut flush = [cmd::FLUSH_RX];
        self.transfer(&mut flush)?;

        Ok(())
    }

    /// Initialize as receiver (CE stays HIGH to listen continuously).
    pub fn init_rx(&mut self, channel: u8, addr: &[u8; 5], payload_len: u8)
        -> Result<(), SpiE, PinE>
    {
        self.ce.set_low().map_err(Nrf24Error::Pin)?;

        self.write_reg(reg::CONFIG,
            CFG_PWR_UP | CFG_EN_CRC | CFG_CRC_2BYTE | CFG_PRIM_RX)?;
        self.delay.delay_us(1500);

        self.write_reg(reg::EN_AA,     0x01)?;
        self.write_reg(reg::EN_RXADDR, 0x01)?;
        self.write_reg(reg::SETUP_AW,  0x03)?;
        self.write_reg(reg::RF_CH,     channel)?;
        self.write_reg(reg::RF_SETUP,  0x07)?;
        self.write_reg_buf(reg::RX_ADDR_P0, addr)?;
        self.write_reg(reg::RX_PW_P0, payload_len)?;
        self.write_reg(reg::STATUS, STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT)?;

        let mut flush = [cmd::FLUSH_RX];
        self.transfer(&mut flush)?;

        self.ce.set_high().map_err(Nrf24Error::Pin)?;  // start listening
        Ok(())
    }

    // ── Transmit ─────────────────────────────────────────────────

    /// Blocking send. Returns Ok(()) on ACK received, Err(MaxRetransmits) otherwise.
    pub fn send(&mut self, payload: &[u8]) -> Result<(), SpiE, PinE> {
        // Write payload to TX FIFO
        self.csn.set_low().map_err(Nrf24Error::Pin)?;
        let mut cmd_buf = [cmd::W_TX_PAYLOAD];
        self.spi.transfer(&mut cmd_buf).map_err(Nrf24Error::Spi)?;
        let mut data = [0u8; 32];
        let len = payload.len().min(32);
        data[..len].copy_from_slice(&payload[..len]);
        self.spi.transfer(&mut data[..len]).map_err(Nrf24Error::Spi)?;
        self.csn.set_high().map_err(Nrf24Error::Pin)?;

        // Pulse CE ≥ 10 µs
        self.ce.set_high().map_err(Nrf24Error::Pin)?;
        self.delay.delay_us(15);
        self.ce.set_low().map_err(Nrf24Error::Pin)?;

        // Poll STATUS
        for _ in 0..200_000u32 {
            let status = self.read_reg(reg::STATUS)?;
            if status & STATUS_TX_DS != 0 {
                self.write_reg(reg::STATUS, STATUS_TX_DS)?;
                return Ok(());
            }
            if status & STATUS_MAX_RT != 0 {
                self.write_reg(reg::STATUS, STATUS_MAX_RT)?;
                let mut flush = [cmd::FLUSH_TX];
                self.transfer(&mut flush)?;
                return Err(Nrf24Error::MaxRetransmits);
            }
            self.delay.delay_us(1);
        }
        Err(Nrf24Error::Timeout)
    }

    // ── Receive ──────────────────────────────────────────────────

    /// Returns true if an RX packet is waiting in the FIFO.
    pub fn data_ready(&mut self) -> Result<bool, SpiE, PinE> {
        let status = self.read_reg(reg::STATUS)?;
        Ok(status & STATUS_RX_DR != 0)
    }

    /// Read one packet from the RX FIFO into `buf`.
    pub fn read_payload(&mut self, buf: &mut [u8]) -> Result<(), SpiE, PinE> {
        self.csn.set_low().map_err(Nrf24Error::Pin)?;
        let mut cmd_byte = [cmd::R_RX_PAYLOAD];
        self.spi.transfer(&mut cmd_byte).map_err(Nrf24Error::Spi)?;
        self.spi.transfer(buf).map_err(Nrf24Error::Spi)?;
        self.csn.set_high().map_err(Nrf24Error::Pin)?;
        self.write_reg(reg::STATUS, STATUS_RX_DR)?;
        Ok(())
    }
}

// ── Usage example ────────────────────────────────────────────────
// fn main() -> ! {
//     let spi  = /* board SPI peripheral */;
//     let csn  = /* GPIO output */;
//     let ce   = /* GPIO output */;
//     let delay = /* delay impl */;
//
//     let mut radio = Nrf24::new(spi, csn, ce, delay);
//     let addr = [0xE7u8; 5];
//     radio.init_tx(76, &addr, 8).unwrap();
//
//     loop {
//         radio.send(b"Hello!  ").unwrap();
//         cortex_m::asm::delay(72_000_000); // ~1 s on 72 MHz
//     }
// }
```

---

## LoRa (SX1276/SX1278) Module

### LoRa Architecture & Register Map

The SX127x family operates in **LoRa mode** (vs. FSK/OOK). It uses a larger register space and a more complex state machine with explicit **operating modes**:

| Mode | RegOpMode value | Description |
|------|----------------|-------------|
| SLEEP | 0x80 | Lowest power; register writes only |
| STANDBY | 0x81 | Oscillator on; SPI accessible |
| TX | 0x83 | Transmitting |
| RXCONTINUOUS | 0x85 | Receive mode, no timeout |
| RXSINGLE | 0x86 | Receive one packet then back to standby |
| CAD | 0x87 | Channel Activity Detection |

**Key Registers (LoRa mode):**

| Address | Name | Description |
|---------|------|-------------|
| 0x01 | RegOpMode | Operating mode + LoRa/FSK select |
| 0x06–0x08 | RegFrMsb/Mid/Lsb | RF carrier frequency (Frf) |
| 0x09 | RegPaConfig | PA selection, output power |
| 0x0B | RegOcp | Over-current protection |
| 0x0C | RegLna | LNA gain |
| 0x0D | RegFifoAddrPtr | FIFO read/write pointer |
| 0x0E | RegFifoTxBaseAddr | TX FIFO base (default 0x80) |
| 0x0F | RegFifoRxBaseAddr | RX FIFO base (default 0x00) |
| 0x10 | RegFifoRxCurrentAddr | Start of last received packet |
| 0x12 | RegIrqFlags | IRQ status (clear by writing 1) |
| 0x13 | RegRxNbBytes | Number of bytes received |
| 0x1D | RegModemConfig1 | BW, coding rate, implicit header |
| 0x1E | RegModemConfig2 | SF, CRC enable |
| 0x1F | RegSymbTimeoutLsb | RX timeout in symbols |
| 0x20–0x21 | RegPreambleMsb/Lsb | Preamble length |
| 0x22 | RegPayloadLength | Payload length (explicit header mode) |
| 0x26 | RegModemConfig3 | AGC, low-data-rate optimize |
| 0x40 | RegDioMapping1 | DIO pin function mapping |
| 0x42 | RegVersion | Chip version (0x12 = SX1276) |

### LoRa SPI Command Protocol

The SX127x SPI interface is simpler than NRF24 — the first byte directly encodes address and read/write direction:

```
Byte 0:  [RW | A6 | A5 | A4 | A3 | A2 | A1 | A0]
         RW = 1 for write, 0 for read
         A[6:0] = register address

Byte 1+: data bytes (burst mode: auto-increments address)
```

There is **no separate command byte** — the address byte IS the command.

---

### LoRa C/C++ Implementation

```c
/*
 * lora_sx127x.h — SX1276/SX1278 LoRa SPI Driver (C/C++)
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ── Register Map ───────────────────────────────────────────────── */
#define LORA_REG_FIFO           0x00
#define LORA_REG_OP_MODE        0x01
#define LORA_REG_FR_MSB         0x06
#define LORA_REG_FR_MID         0x07
#define LORA_REG_FR_LSB         0x08
#define LORA_REG_PA_CONFIG      0x09
#define LORA_REG_OCP            0x0B
#define LORA_REG_LNA            0x0C
#define LORA_REG_FIFO_ADDR_PTR  0x0D
#define LORA_REG_FIFO_TX_BASE   0x0E
#define LORA_REG_FIFO_RX_BASE   0x0F
#define LORA_REG_FIFO_RX_CURR   0x10
#define LORA_REG_IRQ_FLAGS      0x12
#define LORA_REG_RX_NB_BYTES    0x13
#define LORA_REG_PKT_SNR        0x19
#define LORA_REG_PKT_RSSI       0x1A
#define LORA_REG_MODEM_CFG1     0x1D
#define LORA_REG_MODEM_CFG2     0x1E
#define LORA_REG_SYMB_TIMEOUT   0x1F
#define LORA_REG_PREAMBLE_MSB   0x20
#define LORA_REG_PREAMBLE_LSB   0x21
#define LORA_REG_PAYLOAD_LEN    0x22
#define LORA_REG_MODEM_CFG3     0x26
#define LORA_REG_FREQ_ERROR_MSB 0x28
#define LORA_REG_DIO_MAPPING1   0x40
#define LORA_REG_VERSION        0x42
#define LORA_REG_PA_DAC         0x4D

/* ── OpMode values ──────────────────────────────────────────────── */
#define LORA_MODE_LONG_RANGE    0x80   /* LoRa mode select bit */
#define LORA_MODE_SLEEP         0x00
#define LORA_MODE_STANDBY       0x01
#define LORA_MODE_TX            0x03
#define LORA_MODE_RX_CONT       0x05
#define LORA_MODE_RX_SINGLE     0x06
#define LORA_MODE_CAD           0x07

/* ── IRQ flags (RegIrqFlags, write 1 to clear) ──────────────────── */
#define LORA_IRQ_TX_DONE        (1 << 3)
#define LORA_IRQ_VALID_HEADER   (1 << 4)
#define LORA_IRQ_CRC_ERROR      (1 << 5)
#define LORA_IRQ_RX_DONE        (1 << 6)

/* ── SPI R/W bit ────────────────────────────────────────────────── */
#define LORA_SPI_WRITE          0x80

/* ── Platform interface ─────────────────────────────────────────── */
extern void    lora_cs_low(void);
extern void    lora_cs_high(void);
extern void    lora_reset_low(void);
extern void    lora_reset_high(void);
extern uint8_t lora_spi_byte(uint8_t out);
extern void    lora_delay_ms(uint32_t ms);

/* ─────────────────────────────────────────────────────────────────
 * Low-level register access
 * ───────────────────────────────────────────────────────────────── */

static inline uint8_t lora_read_reg(uint8_t reg)
{
    lora_cs_low();
    lora_spi_byte(reg & 0x7F);          /* MSB=0 → read */
    uint8_t val = lora_spi_byte(0x00);
    lora_cs_high();
    return val;
}

static inline void lora_write_reg(uint8_t reg, uint8_t val)
{
    lora_cs_low();
    lora_spi_byte(reg | LORA_SPI_WRITE); /* MSB=1 → write */
    lora_spi_byte(val);
    lora_cs_high();
}

/** Burst-write a buffer to the FIFO at current address pointer. */
static void lora_write_fifo(const uint8_t *buf, uint8_t len)
{
    lora_cs_low();
    lora_spi_byte(LORA_REG_FIFO | LORA_SPI_WRITE);
    for (uint8_t i = 0; i < len; i++) lora_spi_byte(buf[i]);
    lora_cs_high();
}

/** Burst-read len bytes from FIFO at current address pointer. */
static void lora_read_fifo(uint8_t *buf, uint8_t len)
{
    lora_cs_low();
    lora_spi_byte(LORA_REG_FIFO & 0x7F);
    for (uint8_t i = 0; i < len; i++) buf[i] = lora_spi_byte(0x00);
    lora_cs_high();
}

/** Set operating mode (preserves LoRa bit). */
static inline void lora_set_mode(uint8_t mode)
{
    lora_write_reg(LORA_REG_OP_MODE, LORA_MODE_LONG_RANGE | mode);
}

/* ─────────────────────────────────────────────────────────────────
 * Frequency calculation
 *
 * Frf = (Freq_Hz * 2^19) / 32_000_000
 * SX1276 FXOSC = 32 MHz, step = 32e6/2^19 ≈ 61.035 Hz
 * ───────────────────────────────────────────────────────────────── */
static void lora_set_frequency(uint32_t freq_hz)
{
    uint64_t frf = ((uint64_t)freq_hz << 19) / 32000000UL;
    lora_write_reg(LORA_REG_FR_MSB, (uint8_t)(frf >> 16));
    lora_write_reg(LORA_REG_FR_MID, (uint8_t)(frf >>  8));
    lora_write_reg(LORA_REG_FR_LSB, (uint8_t)(frf >>  0));
}

/* ─────────────────────────────────────────────────────────────────
 * Initialization
 * ───────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t frequency_hz;  /* e.g. 868000000 for 868 MHz */
    uint8_t  bandwidth;     /* 0=7.8kHz … 9=500kHz (RegModemCfg1[7:4]) */
    uint8_t  coding_rate;   /* 1=4/5 … 4=4/8 (RegModemCfg1[3:1]) */
    uint8_t  sf;            /* Spreading Factor 6–12 (RegModemCfg2[7:4]) */
    bool     crc_enable;
    int8_t   tx_power_dbm;  /* +2 to +17 dBm via PA_BOOST */
    uint16_t preamble_len;  /* default 8 */
} lora_config_t;

/**
 * lora_init() — Hardware reset, then configure LoRa parameters.
 * Returns 0 on success, -1 if chip not found.
 */
int lora_init(const lora_config_t *cfg)
{
    /* Hardware reset: ≥100 µs low, then ≥5 ms high */
    lora_reset_low();
    lora_delay_ms(1);
    lora_reset_high();
    lora_delay_ms(10);

    /* Verify chip version */
    uint8_t version = lora_read_reg(LORA_REG_VERSION);
    if (version != 0x12) return -1;   /* SX1276 = 0x12 */

    /* Enter SLEEP first to allow switching to LoRa mode */
    lora_write_reg(LORA_REG_OP_MODE, LORA_MODE_SLEEP);
    lora_delay_ms(10);
    lora_write_reg(LORA_REG_OP_MODE, LORA_MODE_LONG_RANGE | LORA_MODE_SLEEP);

    /* Frequency */
    lora_set_frequency(cfg->frequency_hz);

    /* Modem config 1: BW + CodingRate + explicit header */
    lora_write_reg(LORA_REG_MODEM_CFG1,
                   (cfg->bandwidth << 4) | (cfg->coding_rate << 1) | 0x00);

    /* Modem config 2: SF + CRC */
    lora_write_reg(LORA_REG_MODEM_CFG2,
                   (cfg->sf << 4) | (cfg->crc_enable ? 0x04 : 0x00) | 0x03);

    /* Low data rate optimize for SF11/SF12 with BW 125 kHz */
    uint8_t cfg3 = 0x04;  /* AGC auto on */
    if (cfg->sf >= 11 && cfg->bandwidth == 0) cfg3 |= 0x08;
    lora_write_reg(LORA_REG_MODEM_CFG3, cfg3);

    /* Preamble */
    lora_write_reg(LORA_REG_PREAMBLE_MSB, (uint8_t)(cfg->preamble_len >> 8));
    lora_write_reg(LORA_REG_PREAMBLE_LSB, (uint8_t)(cfg->preamble_len));

    /* TX power: use PA_BOOST pin (most modules) */
    int8_t pwr = cfg->tx_power_dbm;
    if (pwr < 2)  pwr = 2;
    if (pwr > 17) pwr = 17;
    lora_write_reg(LORA_REG_PA_CONFIG, 0x80 | (uint8_t)(pwr - 2));

    /* OCP: 120 mA */
    lora_write_reg(LORA_REG_OCP, 0x2B);

    /* LNA: max gain, boost on */
    lora_write_reg(LORA_REG_LNA, 0x23);

    /* FIFO base addresses */
    lora_write_reg(LORA_REG_FIFO_TX_BASE, 0x00);
    lora_write_reg(LORA_REG_FIFO_RX_BASE, 0x00);

    /* Standby */
    lora_set_mode(LORA_MODE_STANDBY);
    lora_delay_ms(10);

    return 0;
}

/* ─────────────────────────────────────────────────────────────────
 * Transmit
 * ───────────────────────────────────────────────────────────────── */

/**
 * lora_send() — Blocking transmit.
 * @data : payload bytes
 * @len  : payload length (1–255 for explicit header mode)
 * Returns 0 on TX_DONE, -1 on timeout.
 */
int lora_send(const uint8_t *data, uint8_t len)
{
    lora_set_mode(LORA_MODE_STANDBY);

    /* Point FIFO address pointer to TX base */
    lora_write_reg(LORA_REG_FIFO_ADDR_PTR, 0x00);
    lora_write_reg(LORA_REG_PAYLOAD_LEN,   len);

    /* Write payload into FIFO */
    lora_write_fifo(data, len);

    /* Clear IRQ flags */
    lora_write_reg(LORA_REG_IRQ_FLAGS, 0xFF);

    /* Start TX */
    lora_set_mode(LORA_MODE_TX);

    /* Poll TX_DONE (flag is set when transmission completes) */
    for (uint32_t i = 0; i < 10000; i++) {
        if (lora_read_reg(LORA_REG_IRQ_FLAGS) & LORA_IRQ_TX_DONE) {
            lora_write_reg(LORA_REG_IRQ_FLAGS, LORA_IRQ_TX_DONE);
            lora_set_mode(LORA_MODE_STANDBY);
            return 0;
        }
        lora_delay_ms(1);
    }
    lora_set_mode(LORA_MODE_STANDBY);
    return -1;
}

/* ─────────────────────────────────────────────────────────────────
 * Receive
 * ───────────────────────────────────────────────────────────────── */

/** Start continuous receive mode. Call lora_recv() periodically. */
void lora_start_rx(void)
{
    lora_write_reg(LORA_REG_FIFO_RX_BASE, 0x00);
    lora_write_reg(LORA_REG_FIFO_ADDR_PTR, 0x00);
    lora_write_reg(LORA_REG_IRQ_FLAGS, 0xFF);
    lora_set_mode(LORA_MODE_RX_CONT);
}

/**
 * lora_recv() — Non-blocking: read a packet if available.
 * @buf     : destination buffer
 * @max_len : buffer capacity
 * Returns number of bytes received, 0 if nothing ready, -1 on CRC error.
 */
int lora_recv(uint8_t *buf, uint8_t max_len)
{
    uint8_t irq = lora_read_reg(LORA_REG_IRQ_FLAGS);

    if (!(irq & LORA_IRQ_RX_DONE)) return 0;

    /* Clear IRQ */
    lora_write_reg(LORA_REG_IRQ_FLAGS, irq);

    if (irq & LORA_IRQ_CRC_ERROR) return -1;

    uint8_t nb = lora_read_reg(LORA_REG_RX_NB_BYTES);
    uint8_t ptr = lora_read_reg(LORA_REG_FIFO_RX_CURR);

    if (nb > max_len) nb = max_len;

    /* Set FIFO pointer to start of received packet */
    lora_write_reg(LORA_REG_FIFO_ADDR_PTR, ptr);
    lora_read_fifo(buf, nb);

    return (int)nb;
}

/** Read last packet RSSI in dBm (SX1276, 868 MHz band). */
int16_t lora_packet_rssi(void)
{
    return (int16_t)lora_read_reg(LORA_REG_PKT_RSSI) - 157;
}

/** Read last packet SNR in 0.25 dB units. */
int8_t lora_packet_snr(void)
{
    return (int8_t)lora_read_reg(LORA_REG_PKT_SNR);
}
```

**Usage example (C):**

```c
#include "lora_sx127x.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    lora_config_t cfg = {
        .frequency_hz = 868000000UL,  /* EU 868 MHz */
        .bandwidth    = 7,            /* 125 kHz */
        .coding_rate  = 1,            /* 4/5 */
        .sf           = 7,            /* SF7 — good speed/range balance */
        .crc_enable   = true,
        .tx_power_dbm = 14,
        .preamble_len = 8,
    };

    if (lora_init(&cfg) != 0) {
        printf("LoRa init failed — chip not detected!\r\n");
        return -1;
    }

    /* --- Transmit example --- */
    const char *msg = "Hello LoRa!";
    lora_send((const uint8_t *)msg, (uint8_t)strlen(msg));

    /* --- Receive loop --- */
    uint8_t buf[256];
    lora_start_rx();
    while (1) {
        int n = lora_recv(buf, sizeof(buf));
        if (n > 0) {
            buf[n] = '\0';
            printf("RX [%d dBm, SNR=%d]: %s\r\n",
                   lora_packet_rssi(),
                   lora_packet_snr() / 4,
                   buf);
        }
    }
}
```

---

### LoRa Rust Implementation

```rust
// lora_sx127x.rs — SX1276/SX1278 LoRa driver using embedded-hal

use embedded_hal::blocking::spi::{Transfer, Write};
use embedded_hal::digital::v2::OutputPin;
use embedded_hal::blocking::delay::{DelayMs, DelayUs};

/// Register addresses
mod reg {
    pub const FIFO:           u8 = 0x00;
    pub const OP_MODE:        u8 = 0x01;
    pub const FR_MSB:         u8 = 0x06;
    pub const FR_MID:         u8 = 0x07;
    pub const FR_LSB:         u8 = 0x08;
    pub const PA_CONFIG:      u8 = 0x09;
    pub const OCP:            u8 = 0x0B;
    pub const LNA:            u8 = 0x0C;
    pub const FIFO_ADDR_PTR:  u8 = 0x0D;
    pub const FIFO_TX_BASE:   u8 = 0x0E;
    pub const FIFO_RX_BASE:   u8 = 0x0F;
    pub const FIFO_RX_CURR:   u8 = 0x10;
    pub const IRQ_FLAGS:      u8 = 0x12;
    pub const RX_NB_BYTES:    u8 = 0x13;
    pub const PKT_SNR:        u8 = 0x19;
    pub const PKT_RSSI:       u8 = 0x1A;
    pub const MODEM_CFG1:     u8 = 0x1D;
    pub const MODEM_CFG2:     u8 = 0x1E;
    pub const PREAMBLE_MSB:   u8 = 0x20;
    pub const PREAMBLE_LSB:   u8 = 0x21;
    pub const PAYLOAD_LEN:    u8 = 0x22;
    pub const MODEM_CFG3:     u8 = 0x26;
    pub const DIO_MAPPING1:   u8 = 0x40;
    pub const VERSION:        u8 = 0x42;
    pub const PA_DAC:         u8 = 0x4D;
}

pub const LORA_SPI_WRITE:    u8 = 0x80;
pub const LORA_LONG_RANGE:   u8 = 0x80;
pub const MODE_SLEEP:        u8 = 0x00;
pub const MODE_STANDBY:      u8 = 0x01;
pub const MODE_TX:           u8 = 0x03;
pub const MODE_RX_CONT:      u8 = 0x05;

pub const IRQ_TX_DONE:  u8 = 1 << 3;
pub const IRQ_CRC_ERR:  u8 = 1 << 5;
pub const IRQ_RX_DONE:  u8 = 1 << 6;

/// LoRa configuration
pub struct LoraConfig {
    pub frequency_hz:  u32,
    pub bandwidth:     u8,       // 0–9
    pub coding_rate:   u8,       // 1–4
    pub spreading_factor: u8,   // 6–12
    pub crc_enable:    bool,
    pub tx_power_dbm:  i8,
    pub preamble_len:  u16,
}

impl Default for LoraConfig {
    fn default() -> Self {
        Self {
            frequency_hz:     868_000_000,
            bandwidth:        7,           // 125 kHz
            coding_rate:      1,           // 4/5
            spreading_factor: 7,
            crc_enable:       true,
            tx_power_dbm:     14,
            preamble_len:     8,
        }
    }
}

/// Error type
#[derive(Debug)]
pub enum LoraError<SpiE, PinE> {
    Spi(SpiE),
    Pin(PinE),
    WrongChip(u8),
    TxTimeout,
    CrcError,
}

pub type Result<T, SpiE, PinE> = core::result::Result<T, LoraError<SpiE, PinE>>;

/// LoRa SX127x driver
pub struct Lora<SPI, CS, RST, DELAY> {
    spi:   SPI,
    cs:    CS,
    rst:   RST,
    delay: DELAY,
}

impl<SPI, CS, RST, DELAY, SpiE, PinE> Lora<SPI, CS, RST, DELAY>
where
    SPI:   Transfer<u8, Error = SpiE> + Write<u8, Error = SpiE>,
    CS:    OutputPin<Error = PinE>,
    RST:   OutputPin<Error = PinE>,
    DELAY: DelayMs<u32> + DelayUs<u32>,
{
    pub fn new(spi: SPI, cs: CS, rst: RST, delay: DELAY) -> Self {
        Self { spi, cs, rst, delay }
    }

    // ── Register access ──────────────────────────────────────────

    fn read_reg(&mut self, reg: u8) -> Result<u8, SpiE, PinE> {
        self.cs.set_low().map_err(LoraError::Pin)?;
        let mut buf = [reg & 0x7F, 0x00];
        self.spi.transfer(&mut buf).map_err(LoraError::Spi)?;
        self.cs.set_high().map_err(LoraError::Pin)?;
        Ok(buf[1])
    }

    fn write_reg(&mut self, reg: u8, val: u8) -> Result<(), SpiE, PinE> {
        self.cs.set_low().map_err(LoraError::Pin)?;
        let mut buf = [reg | LORA_SPI_WRITE, val];
        self.spi.transfer(&mut buf).map_err(LoraError::Spi)?;
        self.cs.set_high().map_err(LoraError::Pin)?;
        Ok(())
    }

    fn write_fifo(&mut self, data: &[u8]) -> Result<(), SpiE, PinE> {
        self.cs.set_low().map_err(LoraError::Pin)?;
        // Write command byte for FIFO register
        self.spi.write(&[reg::FIFO | LORA_SPI_WRITE])
            .map_err(LoraError::Spi)?;
        self.spi.write(data).map_err(LoraError::Spi)?;
        self.cs.set_high().map_err(LoraError::Pin)?;
        Ok(())
    }

    fn read_fifo(&mut self, buf: &mut [u8]) -> Result<(), SpiE, PinE> {
        self.cs.set_low().map_err(LoraError::Pin)?;
        self.spi.write(&[reg::FIFO & 0x7F]).map_err(LoraError::Spi)?;
        self.spi.transfer(buf).map_err(LoraError::Spi)?;
        self.cs.set_high().map_err(LoraError::Pin)?;
        Ok(())
    }

    fn set_mode(&mut self, mode: u8) -> Result<(), SpiE, PinE> {
        self.write_reg(reg::OP_MODE, LORA_LONG_RANGE | mode)
    }

    fn set_frequency(&mut self, freq_hz: u32) -> Result<(), SpiE, PinE> {
        let frf = ((freq_hz as u64) << 19) / 32_000_000u64;
        self.write_reg(reg::FR_MSB, (frf >> 16) as u8)?;
        self.write_reg(reg::FR_MID, (frf >>  8) as u8)?;
        self.write_reg(reg::FR_LSB, (frf >>  0) as u8)?;
        Ok(())
    }

    // ── Initialization ───────────────────────────────────────────

    /// Reset, verify chip version, apply configuration.
    pub fn init(&mut self, cfg: &LoraConfig) -> Result<(), SpiE, PinE> {
        // Hardware reset
        self.rst.set_low().map_err(LoraError::Pin)?;
        self.delay.delay_ms(1);
        self.rst.set_high().map_err(LoraError::Pin)?;
        self.delay.delay_ms(10);

        // Verify chip
        let ver = self.read_reg(reg::VERSION)?;
        if ver != 0x12 {
            return Err(LoraError::WrongChip(ver));
        }

        // Must enter SLEEP before switching to LoRa mode
        self.write_reg(reg::OP_MODE, MODE_SLEEP)?;
        self.delay.delay_ms(10);
        self.write_reg(reg::OP_MODE, LORA_LONG_RANGE | MODE_SLEEP)?;

        self.set_frequency(cfg.frequency_hz)?;

        // Modem config 1: bandwidth + coding rate + explicit header
        self.write_reg(reg::MODEM_CFG1,
            (cfg.bandwidth << 4) | (cfg.coding_rate << 1))?;

        // Modem config 2: SF + CRC
        let crc_bit = if cfg.crc_enable { 0x04 } else { 0x00 };
        self.write_reg(reg::MODEM_CFG2,
            (cfg.spreading_factor << 4) | crc_bit | 0x03)?;

        // Low-data-rate optimization for SF11+ at 125 kHz
        let cfg3 = if cfg.spreading_factor >= 11 && cfg.bandwidth == 7 {
            0x0C  // AGC + LDR
        } else {
            0x04  // AGC only
        };
        self.write_reg(reg::MODEM_CFG3, cfg3)?;

        // Preamble length
        self.write_reg(reg::PREAMBLE_MSB, (cfg.preamble_len >> 8) as u8)?;
        self.write_reg(reg::PREAMBLE_LSB,  cfg.preamble_len as u8)?;

        // TX power (PA_BOOST, +2 to +17 dBm)
        let pwr = cfg.tx_power_dbm.clamp(2, 17) as u8;
        self.write_reg(reg::PA_CONFIG, 0x80 | (pwr - 2))?;

        // OCP 120 mA
        self.write_reg(reg::OCP, 0x2B)?;

        // LNA max gain + boost
        self.write_reg(reg::LNA, 0x23)?;

        // FIFO base addresses
        self.write_reg(reg::FIFO_TX_BASE, 0x00)?;
        self.write_reg(reg::FIFO_RX_BASE, 0x00)?;

        self.set_mode(MODE_STANDBY)?;
        self.delay.delay_ms(10);

        Ok(())
    }

    // ── Transmit ─────────────────────────────────────────────────

    /// Blocking transmit. Returns Ok(()) when TX_DONE IRQ fires.
    pub fn send(&mut self, payload: &[u8]) -> Result<(), SpiE, PinE> {
        self.set_mode(MODE_STANDBY)?;
        self.write_reg(reg::FIFO_ADDR_PTR, 0x00)?;
        self.write_reg(reg::PAYLOAD_LEN, payload.len() as u8)?;
        self.write_fifo(payload)?;
        self.write_reg(reg::IRQ_FLAGS, 0xFF)?;  // clear all flags

        self.set_mode(MODE_TX)?;

        for _ in 0..10_000u32 {
            let irq = self.read_reg(reg::IRQ_FLAGS)?;
            if irq & IRQ_TX_DONE != 0 {
                self.write_reg(reg::IRQ_FLAGS, IRQ_TX_DONE)?;
                self.set_mode(MODE_STANDBY)?;
                return Ok(());
            }
            self.delay.delay_ms(1);
        }
        self.set_mode(MODE_STANDBY)?;
        Err(LoraError::TxTimeout)
    }

    // ── Receive ──────────────────────────────────────────────────

    /// Begin continuous receive mode.
    pub fn start_rx(&mut self) -> Result<(), SpiE, PinE> {
        self.write_reg(reg::FIFO_RX_BASE, 0x00)?;
        self.write_reg(reg::FIFO_ADDR_PTR, 0x00)?;
        self.write_reg(reg::IRQ_FLAGS, 0xFF)?;
        self.set_mode(MODE_RX_CONT)
    }

    /// Non-blocking receive poll.
    /// Returns `Ok(Some(n))` with n bytes written to `buf`,
    /// `Ok(None)` if nothing ready, `Err(CrcError)` on bad packet.
    pub fn recv<'b>(&mut self, buf: &'b mut [u8])
        -> Result<Option<usize>, SpiE, PinE>
    {
        let irq = self.read_reg(reg::IRQ_FLAGS)?;

        if irq & IRQ_RX_DONE == 0 {
            return Ok(None);
        }

        self.write_reg(reg::IRQ_FLAGS, irq)?;

        if irq & IRQ_CRC_ERR != 0 {
            return Err(LoraError::CrcError);
        }

        let nb  = self.read_reg(reg::RX_NB_BYTES)? as usize;
        let ptr = self.read_reg(reg::FIFO_RX_CURR)?;
        let len = nb.min(buf.len());

        self.write_reg(reg::FIFO_ADDR_PTR, ptr)?;
        self.read_fifo(&mut buf[..len])?;

        Ok(Some(len))
    }

    /// Last packet RSSI in dBm.
    pub fn packet_rssi(&mut self) -> Result<i16, SpiE, PinE> {
        let raw = self.read_reg(reg::PKT_RSSI)? as i16;
        Ok(raw - 157)
    }

    /// Last packet SNR in tenths of dB (divide by 4 for dB).
    pub fn packet_snr(&mut self) -> Result<i8, SpiE, PinE> {
        Ok(self.read_reg(reg::PKT_SNR)? as i8)
    }
}

// ── Usage example ─────────────────────────────────────────────────
// fn main() -> ! {
//     let spi   = /* board SPI */;
//     let cs    = /* GPIO */;
//     let rst   = /* GPIO */;
//     let delay = /* delay provider */;
//
//     let mut lora = Lora::new(spi, cs, rst, delay);
//     lora.init(&LoraConfig::default()).unwrap();
//     lora.send(b"Hello LoRa!").unwrap();
//
//     let mut buf = [0u8; 256];
//     lora.start_rx().unwrap();
//     loop {
//         if let Ok(Some(n)) = lora.recv(&mut buf) {
//             // buf[..n] contains received data
//         }
//     }
// }
```

---

## Advanced Topics

### Interrupt-Driven Reception

Polling STATUS or IRQ registers is fine for simple applications but wastes CPU cycles. Both modules expose an **IRQ/DIO** pin that can trigger a GPIO interrupt.

**C — NRF24 IRQ handler (STM32 example):**

```c
/* In your GPIO IRQ handler: */
void EXTI9_5_IRQHandler(void) {
    if (__HAL_GPIO_EXTI_GET_IT(NRF_IRQ_PIN)) {
        __HAL_GPIO_EXTI_CLEAR_IT(NRF_IRQ_PIN);

        uint8_t status = nrf_read_reg(NRF_STATUS);

        if (status & STATUS_RX_DR) {
            /* Read all available packets from FIFO */
            while (!(nrf_read_reg(NRF_FIFO_STATUS) & 0x01)) {
                nrf_read_payload(rx_buffer, PAYLOAD_LEN);
                /* Signal application layer (e.g. via ring buffer) */
                ring_buffer_push(&rx_ring, rx_buffer, PAYLOAD_LEN);
            }
        }

        if (status & STATUS_TX_DS) {
            nrf_write_reg(NRF_STATUS, STATUS_TX_DS);
            tx_complete_flag = true;
        }

        if (status & STATUS_MAX_RT) {
            nrf_write_reg(NRF_STATUS, STATUS_MAX_RT);
            uint8_t flush = CMD_FLUSH_TX;
            nrf_transfer(&flush, NULL, 1);
            tx_failed_flag = true;
        }
    }
}
```

**C — LoRa DIO0 interrupt (RX_DONE mapped to DIO0):**

```c
/* Configure DIO0 → RX_DONE in RegDioMapping1 */
void lora_configure_irq(void) {
    /* DIO0 = 00 = RX_DONE/TX_DONE in LoRa mode */
    lora_write_reg(LORA_REG_DIO_MAPPING1, 0x00);
}

/* GPIO IRQ handler */
void EXTI_DIO0_IRQHandler(void) {
    uint8_t irq = lora_read_reg(LORA_REG_IRQ_FLAGS);
    lora_write_reg(LORA_REG_IRQ_FLAGS, irq);   /* clear all */

    if (irq & LORA_IRQ_RX_DONE) {
        if (!(irq & LORA_IRQ_CRC_ERROR)) {
            uint8_t nb  = lora_read_reg(LORA_REG_RX_NB_BYTES);
            uint8_t ptr = lora_read_reg(LORA_REG_FIFO_RX_CURR);
            lora_write_reg(LORA_REG_FIFO_ADDR_PTR, ptr);
            lora_read_fifo(application_rx_buf, nb);
            application_rx_len   = nb;
            application_rx_ready = true;
        }
    }
}
```

---

### DMA-Accelerated SPI Transfers

For high-throughput scenarios (e.g. burst-reading multiple NRF24 FIFO levels), DMA eliminates CPU stalls during long SPI transfers.

```c
/* STM32 HAL DMA SPI for LoRa FIFO read — async, callback-driven */

static uint8_t dma_tx_buf[256];   /* MOSI: address byte + dummy */
static uint8_t dma_rx_buf[256];   /* MISO: status + payload */

void lora_read_fifo_dma(uint8_t fifo_ptr, uint8_t len,
                        void (*callback)(uint8_t *, uint8_t))
{
    dma_user_callback = callback;
    dma_payload_len   = len;

    dma_tx_buf[0] = LORA_REG_FIFO & 0x7F;     /* read command */
    memset(dma_tx_buf + 1, 0x00, len);         /* dummy bytes */

    lora_write_reg(LORA_REG_FIFO_ADDR_PTR, fifo_ptr);

    lora_cs_low();
    /* Kick off DMA transfer: len+1 bytes full-duplex */
    HAL_SPI_TransmitReceive_DMA(&hspi1, dma_tx_buf, dma_rx_buf, len + 1);
    /* CS is deasserted in HAL_SPI_TxRxCpltCallback */
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi == &hspi1) {
        lora_cs_high();
        /* dma_rx_buf[0] = status byte, [1..len] = payload */
        if (dma_user_callback)
            dma_user_callback(dma_rx_buf + 1, dma_payload_len);
    }
}
```

---

### Power Management

Both modules support low-power sleep states. Proper power management is critical for battery-operated IoT devices.

```c
/* NRF24: Power down — ~900 nA current draw */
void nrf_power_down(void) {
    nrf_ce_low();
    /* Clear PWR_UP bit; keep register contents */
    uint8_t cfg = nrf_read_reg(NRF_CONFIG);
    nrf_write_reg(NRF_CONFIG, cfg & ~CFG_PWR_UP);
}

void nrf_power_up(void) {
    uint8_t cfg = nrf_read_reg(NRF_CONFIG);
    nrf_write_reg(NRF_CONFIG, cfg | CFG_PWR_UP);
    nrf_delay_us(1500);   /* tpd2stby */
}

/* LoRa: Sleep mode — ~1 µA current draw */
void lora_sleep(void) {
    lora_write_reg(LORA_REG_OP_MODE, LORA_MODE_LONG_RANGE | MODE_SLEEP);
}

void lora_wake(void) {
    lora_write_reg(LORA_REG_OP_MODE, LORA_MODE_LONG_RANGE | MODE_STANDBY);
    lora_delay_ms(10);    /* oscillator startup */
}
```

**Rust trait-based power management:**

```rust
/// A simple power-management trait for wireless modules.
pub trait RadioPower {
    type Error;
    fn sleep(&mut self)   -> core::result::Result<(), Self::Error>;
    fn wakeup(&mut self)  -> core::result::Result<(), Self::Error>;
}

impl<SPI, CS, RST, DELAY, SpiE, PinE> RadioPower for Lora<SPI, CS, RST, DELAY>
where
    SPI:   Transfer<u8, Error = SpiE> + Write<u8, Error = SpiE>,
    CS:    OutputPin<Error = PinE>,
    RST:   OutputPin<Error = PinE>,
    DELAY: DelayMs<u32> + DelayUs<u32>,
{
    type Error = LoraError<SpiE, PinE>;

    fn sleep(&mut self) -> Result<(), SpiE, PinE> {
        self.write_reg(reg::OP_MODE, LORA_LONG_RANGE | MODE_SLEEP)
    }

    fn wakeup(&mut self) -> Result<(), SpiE, PinE> {
        self.write_reg(reg::OP_MODE, LORA_LONG_RANGE | MODE_STANDBY)?;
        self.delay.delay_ms(10);
        Ok(())
    }
}
```

---

## Comparison: NRF24 vs LoRa

| Feature | NRF24L01+ | LoRa SX1276/SX1278 |
|---------|-----------|---------------------|
| **Frequency** | 2.4 GHz ISM | 433 / 868 / 915 MHz |
| **Modulation** | GFSK | Chirp Spread Spectrum (CSS) |
| **Data rate** | 250 kbps – 2 Mbps | 0.3 – 37.5 kbps |
| **Range (outdoor)** | ~100 m (line of sight) | 2–15 km (line of sight) |
| **Range (indoor)** | ~30 m | ~2 km |
| **TX current** | ~11 mA @ 0 dBm | ~90 mA @ +20 dBm |
| **RX current** | ~13.5 mA | ~10–12 mA |
| **Sleep current** | ~900 nA | ~1 µA |
| **Packet size** | 1–32 bytes | 1–255 bytes |
| **Encryption** | No (software only) | AES not built-in |
| **Auto-ACK** | Yes (Enhanced ShockBurst) | No (application layer) |
| **SPI speed** | Up to 8 MHz | Up to 10 MHz |
| **SPI mode** | Mode 0 | Mode 0 |
| **Cost** | ~$0.50 (module) | ~$4–10 (module) |
| **Typical use** | Short-range, high data rate | Long-range, low power IoT |

**When to choose NRF24L01+:**
- Sub-100 m range needed
- High data rate (sensor streaming, game controllers)
- Multiple nodes with built-in ACK required
- Low cost is critical

**When to choose LoRa:**
- Kilometre-range coverage needed
- Deep indoor/penetration required
- Low duty-cycle IoT (environmental sensors, smart meters)
- Gateway-to-node topology (LoRaWAN)

---

## Summary

Both the **NRF24L01+** and **LoRa SX127x** family exemplify how complex RF hardware can be fully controlled through a **clean SPI register interface**. The programming model is consistent across both:

1. **Hardware reset** to initialize module to known state
2. **Mode transition to SLEEP** (required before mode register changes)
3. **Register configuration** via SPI write commands (frequency, modulation, power, addresses)
4. **FIFO interaction** — write payload bytes to TX FIFO, then trigger transmission; read from RX FIFO on data-ready
5. **Status polling or interrupt handling** via STATUS/IRQ registers

The key SPI distinctions are:
- **NRF24** uses a dedicated command byte (`R_REGISTER` / `W_REGISTER` etc.) followed by data bytes; the STATUS byte is returned on MISO during the command phase
- **LoRa SX127x** encodes read/write direction directly in the register address MSB; burst access auto-increments the address pointer

In **C/C++**, drivers are typically structured around platform-agnostic HAL callbacks, enabling easy portability across STM32, AVR, ESP32, and other MCUs. In **Rust**, the `embedded-hal` trait system provides zero-cost abstractions that make drivers automatically compatible with any HAL implementation, offering compile-time type safety with no runtime overhead.

Both modules strongly benefit from **interrupt-driven operation** (GPIO IRQ on the IRQ/DIO pin) for responsive, power-efficient designs, and from **DMA-backed SPI transfers** when payload throughput is a concern. Proper **power management** — leveraging the sub-µA sleep modes — is essential for battery-operated deployments, particularly with LoRa-based sensors intended for multi-year field operation.