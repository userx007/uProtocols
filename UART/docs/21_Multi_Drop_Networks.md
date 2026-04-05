# 21. Multi-Drop Networks — Building RS-485 Networks with Address Recognition

**Physical Layer & Architecture** — RS-485 differential signalling, transceiver DE/~RE pin control, termination resistors, fail-safe biasing, and the relationship between cable length and maximum baud rate.

**Address Recognition Strategies** — pure software filtering, hardware 9-bit UART mode (mark/space parity, MPCM on AVR/STM32), and Modbus-style inter-frame silence detection.

**C/C++ Code Examples** covering:
- STM32 HAL RS-485 initialisation, frame serialisation, and CRC-16/Modbus
- Master polling loop with request/response
- Slave address-filtering and response handler
- AVR ATmega 9-bit multiprocessor UART mode with interrupt handler
- Linux userspace `TIOCSRS485` ioctl for kernel-managed DE control
- Retry logic with exponential back-off

**Rust Code Examples** covering:
- `Frame` type with `to_bytes()`/`from_bytes()` and CRC validation
- `Rs485Bus` abstraction over the `serialport` crate
- Type-safe `Master` polling loop and `Slave` state machine
- `main.rs` wiring the two roles together via CLI arguments

**Summary** — consolidates the physical, addressing, framing, and robustness topics and connects them to real-world protocols (Modbus RTU, DMX-512, LIN).

---

## Table of Contents

1. [Introduction](#introduction)
2. [RS-485 Physical Layer](#rs-485-physical-layer)
3. [Multi-Drop Topology and Addressing](#multi-drop-topology-and-addressing)
4. [Address Recognition Strategies](#address-recognition-strategies)
5. [UART Hardware Support for 9-Bit Addressing](#uart-hardware-support-for-9-bit-addressing)
6. [Protocol Framing](#protocol-framing)
7. [C/C++ Implementation](#cc-implementation)
8. [Rust Implementation](#rust-implementation)
9. [Error Handling and Bus Arbitration](#error-handling-and-bus-arbitration)
10. [Practical Considerations](#practical-considerations)
11. [Summary](#summary)

---

## Introduction

A **multi-drop network** allows a single communication bus to connect one master node and many slave nodes, so that multiple devices share the same physical wires. RS-485 is the dominant physical-layer standard for such networks in industrial, automotive, building automation, and embedded systems. It supports:

- Up to **32 unit loads** (or 256 with 1/8-unit-load transceivers) on one bus segment
- Cable runs of **up to 1200 m** (at lower baud rates)
- **Half-duplex** (two-wire) or **full-duplex** (four-wire) configurations
- Data rates from a few hundred baud up to **10 Mbps**

The UART is the underlying serial engine that generates and receives the bit stream; RS-485 transceivers convert the single-ended UART signals to the differential pair required by the standard.

---

## RS-485 Physical Layer

### Differential Signaling

RS-485 uses a **differential pair** (A/B or D+/D−). The receiver compares the voltage between the two lines:

| Condition           | Differential Voltage (VA − VB) | Logic Level |
|---------------------|-------------------------------|-------------|
| Mark (idle / `1`)   | +200 mV to +6 V               | `1`         |
| Space (`0`)         | −200 mV to −6 V               | `0`         |
| Undefined zone      | −200 mV to +200 mV            | Undefined   |

### Transceiver Driver Enable

Most RS-485 chips expose a **Driver Enable (DE)** and/or **Receiver Enable (~RE)** pin. Before transmitting, the MCU must assert `DE` high; after transmission, it must de-assert `DE` to return the bus to a receive state.

```
MCU UART TX ──► [RS-485 Transceiver] ──► A/B differential bus
MCU UART RX ◄── [RS-485 Transceiver] ◄── A/B differential bus
MCU GPIO    ───► DE / ~RE (direction control)
```

### Termination and Biasing

- **Termination resistor** (120 Ω) at each end of the bus cable prevents reflections.
- **Bias resistors** (typically 560 Ω–1 kΩ) pull A high and B low to guarantee a defined idle state when no driver is active.

---

## Multi-Drop Topology and Addressing

In a multi-drop network, all nodes share the same bus. Without addressing, every frame is received by every node. An **address field** in each frame tells all nodes which one should process (and respond to) the data.

### Common Addressing Models

| Model                     | Description                                              |
|---------------------------|----------------------------------------------------------|
| **Software addressing**   | Every frame carries an address byte; slaves filter in firmware |
| **9-bit / UART address mode** | 9th data bit flags address frames vs. data frames    |
| **Multidrop with ACK**    | Master polls; slave responds only if its address matches |
| **Token-passing**         | Slaves take turns as temporary master                    |

The most widely implemented approach in UART-based networks is **9-bit addressing**, supported natively by many MCU UARTs (UART9B, RS-485 mode, or "multiprocessor communication" in STM32/AVR terminology).

---

## Address Recognition Strategies

### 1. Pure Software Address Filtering

Every frame begins with an address byte. Every slave receives and inspects every byte. If the address does not match, the rest of the frame is discarded.

**Pros:** Works with any UART hardware.  
**Cons:** High CPU load; every byte generates an interrupt.

### 2. 9-Bit UART (Mark/Space Parity or Wake-Up Mode)

The UART is configured for **9 data bits** (or 8 data bits + parity repurposed as the address flag). The 9th bit is:

- **`1`** (Mark) → this frame contains an address
- **`0`** (Space) → this frame contains data

Hardware logic in the UART can wake the CPU from sleep **only** when a mark frame arrives, greatly reducing CPU load. This is the "**Multiprocessor Communication Mode**" in STM32 (MPCM) and the "**Multi-Processor Communication**" in AVR.

```
Frame structure (9-bit mode):
┌────────┬────────────────────┬───────────┐
│ START  │  8 data bits       │  BIT9 = 1 │  ← Address frame
└────────┴────────────────────┴───────────┘
┌────────┬────────────────────┬───────────┐
│ START  │  8 data bits       │  BIT9 = 0 │  ← Data frame
└────────┴────────────────────┴───────────┘
```

### 3. Address Recognition with LIN/MODBUS Framing

Protocols such as **Modbus RTU** use a different strategy: inter-frame silence (3.5 character times) marks the start of a new frame, and the first byte is always the device address. No 9th bit is required.

---

## UART Hardware Support for 9-Bit Addressing

### STM32 — Multiprocessor Communication Mode (MPCM)

In STM32 HAL, set `WordLength = UART_WORDLENGTH_9B`, then use the MPCM bit or the hardware address detection register (`UART_AddressLength_4b` or `_7b`) available on USART peripherals that support the feature (e.g. USART1, USART2 on STM32F4/F7/H7).

### AVR — MPCM Mode

Set bit `MPCM0` in `UCSR0A`. When set, the UART ignores incoming frames where the 9th bit is 0 until it sees a frame with bit9 = 1, then compares the address byte.

### Configuring 9-Bit Frames on Linux (`termios`)

Linux serial drivers support 9-bit frames on hardware that exposes them; however, portable userspace code typically simulates the 9th bit via **sticky parity** (`PARODD` + `PARENB` + `CMSPAR`) or uses the `RS485` ioctl flags.

---

## Protocol Framing

A typical minimal frame layout for a multi-drop RS-485 network:

```
┌──────────┬──────────┬──────────┬────────────────┬──────────┐
│  ADDRESS │  COMMAND │  LENGTH  │  PAYLOAD (0-N) │   CRC16  │
│  1 byte  │  1 byte  │  1 byte  │  N bytes       │  2 bytes │
└──────────┴──────────┴──────────┴────────────────┴──────────┘
```

- **ADDRESS**: 0x01–0xFD are unicast addresses; 0xFF is broadcast.
- **COMMAND**: Application-specific opcode.
- **LENGTH**: Number of payload bytes.
- **CRC16**: Error detection (Modbus CRC or CRC-CCITT are common choices).

---

## C/C++ Implementation

### 1. RS-485 Transceiver Direction Control (STM32 HAL)

```c
// rs485.h
#ifndef RS485_H
#define RS485_H

#include "stm32f4xx_hal.h"

#define RS485_DE_PORT    GPIOA
#define RS485_DE_PIN     GPIO_PIN_1
#define RS485_UART       &huart2
#define RS485_TIMEOUT_MS 10
#define MY_ADDRESS       0x03    // This node's address
#define BROADCAST_ADDR   0xFF

typedef struct {
    uint8_t address;
    uint8_t command;
    uint8_t length;
    uint8_t payload[64];
    uint16_t crc;
} RS485Frame;

void rs485_init(void);
HAL_StatusTypeDef rs485_send_frame(const RS485Frame *frame);
HAL_StatusTypeDef rs485_receive_frame(RS485Frame *frame, uint32_t timeout);

#endif // RS485_H
```

```c
// rs485.c
#include "rs485.h"
#include <string.h>

extern UART_HandleTypeDef huart2;

/* ------------------------------------------------------------------ */
/* CRC-16 (Modbus polynomial 0xA001)                                   */
/* ------------------------------------------------------------------ */
static uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* Driver Enable helpers                                               */
/* ------------------------------------------------------------------ */
static inline void de_assert(void)
{
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET);
}

static inline void de_deassert(void)
{
    /* Allow last byte to finish transmitting before releasing the bus. */
    /* The HAL_UART_Transmit() call already blocks until TX complete,   */
    /* but we add a brief guard for the transceiver propagation delay.  */
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET);
}

/* ------------------------------------------------------------------ */
/* Initialise GPIO for DE pin (assumes UART already initialised)       */
/* ------------------------------------------------------------------ */
void rs485_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin   = RS485_DE_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RS485_DE_PORT, &gpio);

    de_deassert();   /* Start in receive mode */
}

/* ------------------------------------------------------------------ */
/* Transmit a frame                                                    */
/* ------------------------------------------------------------------ */
HAL_StatusTypeDef rs485_send_frame(const RS485Frame *frame)
{
    /* Build raw byte buffer: [addr][cmd][len][payload...][crcL][crcH] */
    uint8_t buf[3 + 64 + 2];
    size_t  pos = 0;

    buf[pos++] = frame->address;
    buf[pos++] = frame->command;
    buf[pos++] = frame->length;
    memcpy(&buf[pos], frame->payload, frame->length);
    pos += frame->length;

    uint16_t crc = crc16_modbus(buf, pos);
    buf[pos++] = (uint8_t)(crc & 0xFF);
    buf[pos++] = (uint8_t)(crc >> 8);

    de_assert();
    HAL_StatusTypeDef status =
        HAL_UART_Transmit(RS485_UART, buf, (uint16_t)pos, RS485_TIMEOUT_MS);
    de_deassert();

    return status;
}

/* ------------------------------------------------------------------ */
/* Receive and validate a frame addressed to this node                */
/* ------------------------------------------------------------------ */
HAL_StatusTypeDef rs485_receive_frame(RS485Frame *frame, uint32_t timeout)
{
    uint8_t header[3];

    /* Read address + command + length */
    if (HAL_UART_Receive(RS485_UART, header, 3, timeout) != HAL_OK)
        return HAL_TIMEOUT;

    uint8_t addr = header[0];
    uint8_t cmd  = header[1];
    uint8_t len  = header[2];

    /* Address filtering: accept unicast to MY_ADDRESS or broadcast */
    if (addr != MY_ADDRESS && addr != BROADCAST_ADDR) {
        /* Drain remaining bytes without processing them */
        uint8_t dummy[64 + 2];
        HAL_UART_Receive(RS485_UART, dummy, len + 2, timeout);
        return HAL_ERROR;   /* Caller may retry */
    }

    if (len > sizeof(frame->payload))
        return HAL_ERROR;

    /* Read payload */
    if (len > 0 &&
        HAL_UART_Receive(RS485_UART, frame->payload, len, timeout) != HAL_OK)
        return HAL_TIMEOUT;

    /* Read CRC */
    uint8_t crc_bytes[2];
    if (HAL_UART_Receive(RS485_UART, crc_bytes, 2, timeout) != HAL_OK)
        return HAL_TIMEOUT;

    /* Verify CRC over header + payload */
    uint8_t verify_buf[3 + 64];
    verify_buf[0] = addr;
    verify_buf[1] = cmd;
    verify_buf[2] = len;
    memcpy(&verify_buf[3], frame->payload, len);

    uint16_t expected = crc16_modbus(verify_buf, 3 + len);
    uint16_t received = (uint16_t)crc_bytes[0] | ((uint16_t)crc_bytes[1] << 8);

    if (expected != received)
        return HAL_ERROR;   /* CRC mismatch */

    frame->address = addr;
    frame->command = cmd;
    frame->length  = len;
    frame->crc     = received;

    return HAL_OK;
}
```

---

### 2. Master Node — Polling All Slaves

```c
// master.c  — polls each slave in sequence (request/response)
#include "rs485.h"
#include <stdio.h>

#define NUM_SLAVES    4
#define CMD_READ_TEMP 0x10
#define CMD_ACK       0x01

static const uint8_t slave_addresses[NUM_SLAVES] = {0x01, 0x02, 0x03, 0x04};

void master_poll_all(void)
{
    RS485Frame tx_frame = {0};
    RS485Frame rx_frame = {0};

    for (int i = 0; i < NUM_SLAVES; i++) {
        tx_frame.address = slave_addresses[i];
        tx_frame.command = CMD_READ_TEMP;
        tx_frame.length  = 0;   /* No payload in request */

        if (rs485_send_frame(&tx_frame) != HAL_OK) {
            printf("TX error to slave 0x%02X\n", slave_addresses[i]);
            continue;
        }

        /* Wait for the slave to respond */
        HAL_StatusTypeDef rx_status =
            rs485_receive_frame(&rx_frame, 50 /* ms */);

        if (rx_status == HAL_OK && rx_frame.command == CMD_ACK) {
            /* Interpret 2-byte little-endian temperature (tenths of °C) */
            int16_t temp_raw =
                (int16_t)rx_frame.payload[0] |
                ((int16_t)rx_frame.payload[1] << 8);
            printf("Slave 0x%02X: %.1f °C\n",
                   slave_addresses[i], temp_raw / 10.0);
        } else {
            printf("No response from slave 0x%02X (status=%d)\n",
                   slave_addresses[i], rx_status);
        }
    }
}
```

---

### 3. Slave Node — Respond to Addressed Requests

```c
// slave.c
#include "rs485.h"
#include <string.h>

#define CMD_READ_TEMP 0x10
#define CMD_ACK       0x01

/* Simulated temperature sensor reading (tenths of °C) */
static int16_t read_temperature_raw(void)
{
    return 235;   /* 23.5 °C */
}

void slave_task(void)
{
    RS485Frame rx_frame = {0};
    RS485Frame tx_frame = {0};

    /* Block until a frame arrives addressed to us */
    if (rs485_receive_frame(&rx_frame, HAL_MAX_DELAY) != HAL_OK)
        return;

    if (rx_frame.command == CMD_READ_TEMP) {
        int16_t temp = read_temperature_raw();

        tx_frame.address  = MY_ADDRESS;   /* Echo our own address in response */
        tx_frame.command  = CMD_ACK;
        tx_frame.length   = 2;
        tx_frame.payload[0] = (uint8_t)(temp & 0xFF);
        tx_frame.payload[1] = (uint8_t)(temp >> 8);

        rs485_send_frame(&tx_frame);
    }
}
```

---

### 4. 9-Bit Address Mode (AVR / ATmega, bare-metal)

```c
// avr_rs485_9bit.c — AVR ATmega328P, 9-bit multiprocessor mode
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>

#define BAUD         9600UL
#define UBRR_VALUE   (F_CPU / (16UL * BAUD) - 1)
#define MY_ADDRESS   0x05

static volatile bool address_matched = false;

void uart_init_9bit(void)
{
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);

    /* Enable receiver, transmitter, RX interrupt */
    UCSR0B = (1 << RXEN0)  |
             (1 << TXEN0)  |
             (1 << RXCIE0) |
             (1 << UCSZ02);   /* 9-bit character size (UCSZ02 = bit2) */

    /* 8N1 base, with UCSZ02 above this selects 9-bit */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);

    /* Enable multiprocessor communication mode */
    UCSR0A |= (1 << MPCM0);
}

/* Send a byte with optional address flag (bit9) */
void uart_send_9bit(uint8_t data, bool is_address)
{
    /* Wait for transmit buffer empty */
    while (!(UCSR0A & (1 << UDRE0)));

    /* Set/clear TXB8 (the 9th bit) BEFORE writing UDR0 */
    if (is_address)
        UCSR0B |=  (1 << TXB80);
    else
        UCSR0B &= ~(1 << TXB80);

    UDR0 = data;
}

ISR(USART_RX_vect)
{
    /* Read bit 9 before UDR0 */
    uint8_t bit9   = (UCSR0B >> RXB80) & 0x01;
    uint8_t status = UCSR0A;
    uint8_t data   = UDR0;

    if (status & ((1 << FE0) | (1 << DOR0) | (1 << UPE0))) {
        /* Frame error / overrun / parity — discard */
        address_matched = false;
        UCSR0A |= (1 << MPCM0);   /* Re-enable address filtering */
        return;
    }

    if (bit9) {
        /* This is an address frame */
        if (data == MY_ADDRESS) {
            address_matched = true;
            UCSR0A &= ~(1 << MPCM0);  /* Accept subsequent data frames */
        } else {
            address_matched = false;
            UCSR0A |= (1 << MPCM0);   /* Ignore until next address frame */
        }
    } else {
        /* Data frame — only processed if address previously matched */
        if (address_matched) {
            /* Handle data byte here */
            (void)data;   /* Replace with actual processing */
        }
    }
}
```

---

### 5. Linux Userspace RS-485 Driver Enable (`ioctl`)

```c
// linux_rs485.c — Enable RS-485 half-duplex mode via kernel ioctl
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/serial.h>
#include <stdio.h>
#include <string.h>

int rs485_open(const char *port, int baud)
{
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    /* Configure RS-485 hardware mode */
    struct serial_rs485 rs485conf = {0};
    rs485conf.flags  = SER_RS485_ENABLED |
                       SER_RS485_RTS_ON_SEND |   /* DE high during TX    */
                       SER_RS485_RX_DURING_TX;   /* optional: echo check */
    rs485conf.delay_rts_before_send = 0;   /* microseconds */
    rs485conf.delay_rts_after_send  = 0;

    if (ioctl(fd, TIOCSRS485, &rs485conf) < 0) {
        perror("ioctl TIOCSRS485");
        close(fd);
        return -1;
    }

    /* Configure terminal settings */
    struct termios tty = {0};
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);
    cfmakeraw(&tty);
    tty.c_cflag |= (CLOCAL | CREAD | CS8);
    tty.c_cflag &= ~(CRTSCTS | PARENB | CSTOPB);

    if (tcsetattr(fd, TCSANOW, &tty) < 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

/* Usage example */
int main(void)
{
    int fd = rs485_open("/dev/ttyS1", B115200);
    if (fd < 0) return 1;

    /* Send an addressed frame */
    uint8_t frame[] = { 0x03, 0x10, 0x00, 0x00, 0x00 }; /* addr cmd len crcL crcH */
    ssize_t n = write(fd, frame, sizeof(frame));
    printf("Sent %zd bytes\n", n);

    close(fd);
    return 0;
}
```

---

## Rust Implementation

Rust's strong type system and ownership model make it an excellent fit for protocol state machines. The examples below use the [`serialport`](https://crates.io/crates/serialport) crate for cross-platform serial I/O.

### `Cargo.toml`

```toml
[package]
name    = "rs485_multidrop"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4"
```

---

### 1. Frame Type and CRC

```rust
// src/frame.rs

/// Represents a single RS-485 protocol frame.
#[derive(Debug, Clone, PartialEq)]
pub struct Frame {
    pub address: u8,
    pub command: u8,
    pub payload: Vec<u8>,
}

pub const BROADCAST_ADDR: u8 = 0xFF;

/// CRC-16 Modbus (polynomial 0xA001).
pub fn crc16_modbus(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

impl Frame {
    /// Serialise the frame into a byte vector, appending CRC16.
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(3 + self.payload.len() + 2);
        buf.push(self.address);
        buf.push(self.command);
        buf.push(self.payload.len() as u8);
        buf.extend_from_slice(&self.payload);

        let crc = crc16_modbus(&buf);
        buf.push((crc & 0xFF) as u8);
        buf.push((crc >> 8) as u8);
        buf
    }

    /// Parse and validate a frame from a raw byte slice.
    /// Returns `None` if the slice is too short or the CRC fails.
    pub fn from_bytes(raw: &[u8]) -> Option<Self> {
        if raw.len() < 5 {
            return None;
        }
        let address = raw[0];
        let command = raw[1];
        let length  = raw[2] as usize;

        if raw.len() < 3 + length + 2 {
            return None;
        }

        let payload = raw[3..3 + length].to_vec();

        let expected_crc = crc16_modbus(&raw[..3 + length]);
        let received_crc = (raw[3 + length] as u16) |
                           ((raw[3 + length + 1] as u16) << 8);

        if expected_crc != received_crc {
            eprintln!("CRC mismatch: expected {:#06X}, got {:#06X}",
                      expected_crc, received_crc);
            return None;
        }

        Some(Frame { address, command, payload })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip() {
        let f = Frame {
            address: 0x03,
            command: 0x10,
            payload: vec![0xAB, 0xCD],
        };
        let bytes = f.to_bytes();
        let parsed = Frame::from_bytes(&bytes).expect("parse failed");
        assert_eq!(f, parsed);
    }

    #[test]
    fn crc_mismatch_returns_none() {
        let f = Frame { address: 0x01, command: 0x01, payload: vec![] };
        let mut bytes = f.to_bytes();
        *bytes.last_mut().unwrap() ^= 0xFF;   /* corrupt CRC */
        assert!(Frame::from_bytes(&bytes).is_none());
    }
}
```

---

### 2. RS-485 Bus Abstraction

```rust
// src/bus.rs
use std::io::{Read, Write};
use std::time::Duration;
use serialport::SerialPort;
use crate::frame::Frame;

/// Error type for bus operations.
#[derive(Debug)]
pub enum BusError {
    Io(std::io::Error),
    Timeout,
    InvalidFrame,
    AddressMismatch,
}

impl From<std::io::Error> for BusError {
    fn from(e: std::io::Error) -> Self {
        BusError::Io(e)
    }
}

pub struct Rs485Bus {
    port:       Box<dyn SerialPort>,
    my_address: u8,
}

impl Rs485Bus {
    /// Open a serial port configured for RS-485 half-duplex communication.
    ///
    /// Direction control (DE pin) is handled by the OS driver when the
    /// `TIOCSRS485` flag is set (Linux) or by hardware auto-direction.
    pub fn open(path: &str, baud: u32, my_address: u8) -> Result<Self, BusError> {
        let port = serialport::new(path, baud)
            .timeout(Duration::from_millis(100))
            .open()
            .map_err(|e| BusError::Io(std::io::Error::other(e.to_string())))?;

        Ok(Self { port, my_address })
    }

    /// Send a frame onto the bus.
    pub fn send(&mut self, frame: &Frame) -> Result<(), BusError> {
        let bytes = frame.to_bytes();
        self.port.write_all(&bytes)?;
        self.port.flush()?;
        Ok(())
    }

    /// Receive a frame.
    ///
    /// - Returns `Err(BusError::AddressMismatch)` if the frame is for a
    ///   different node (so the caller can decide to keep waiting or not).
    /// - Returns `Err(BusError::InvalidFrame)` if CRC fails.
    pub fn receive(&mut self) -> Result<Frame, BusError> {
        /* Read header: [address, command, length] */
        let mut header = [0u8; 3];
        self.read_exact_timeout(&mut header)?;

        let length = header[2] as usize;

        /* Read payload + 2-byte CRC */
        let mut rest = vec![0u8; length + 2];
        self.read_exact_timeout(&mut rest)?;

        /* Reassemble into a single slice for parsing */
        let mut raw = Vec::with_capacity(3 + length + 2);
        raw.extend_from_slice(&header);
        raw.extend_from_slice(&rest);

        let frame = Frame::from_bytes(&raw).ok_or(BusError::InvalidFrame)?;

        /* Address filter */
        if frame.address != self.my_address
            && frame.address != crate::frame::BROADCAST_ADDR
        {
            return Err(BusError::AddressMismatch);
        }

        Ok(frame)
    }

    /// Helper: read exactly `buf.len()` bytes, mapping `WouldBlock`
    /// and incomplete reads to `BusError::Timeout`.
    fn read_exact_timeout(&mut self, buf: &mut [u8]) -> Result<(), BusError> {
        let mut total = 0;
        while total < buf.len() {
            match self.port.read(&mut buf[total..]) {
                Ok(0) => return Err(BusError::Timeout),
                Ok(n) => total += n,
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {
                    return Err(BusError::Timeout)
                }
                Err(e) => return Err(BusError::Io(e)),
            }
        }
        Ok(())
    }
}
```

---

### 3. Master — Polling Loop

```rust
// src/master.rs
use crate::bus::{BusError, Rs485Bus};
use crate::frame::Frame;

pub const CMD_READ_TEMP: u8 = 0x10;
pub const CMD_ACK:       u8 = 0x01;

pub struct Master {
    bus:           Rs485Bus,
    slave_addresses: Vec<u8>,
}

impl Master {
    pub fn new(bus: Rs485Bus, slave_addresses: Vec<u8>) -> Self {
        Self { bus, slave_addresses }
    }

    /// Poll all known slaves and print their temperature readings.
    pub fn poll_all(&mut self) {
        for &addr in &self.slave_addresses.clone() {
            let request = Frame {
                address: addr,
                command: CMD_READ_TEMP,
                payload: vec![],
            };

            if let Err(e) = self.bus.send(&request) {
                eprintln!("TX error to 0x{:02X}: {:?}", addr, e);
                continue;
            }

            match self.bus.receive() {
                Ok(resp) if resp.command == CMD_ACK && resp.payload.len() >= 2 => {
                    let raw = i16::from_le_bytes([resp.payload[0], resp.payload[1]]);
                    println!("Slave 0x{:02X}: {:.1} °C", addr, raw as f32 / 10.0);
                }
                Ok(resp) => {
                    eprintln!("Unexpected response from 0x{:02X}: cmd=0x{:02X}",
                              addr, resp.command);
                }
                Err(BusError::Timeout) => {
                    eprintln!("Timeout waiting for 0x{:02X}", addr);
                }
                Err(e) => {
                    eprintln!("Error from 0x{:02X}: {:?}", addr, e);
                }
            }
        }
    }
}
```

---

### 4. Slave — State Machine

```rust
// src/slave.rs
use crate::bus::{BusError, Rs485Bus};
use crate::frame::Frame;
use crate::master::{CMD_ACK, CMD_READ_TEMP};

pub struct Slave {
    bus: Rs485Bus,
}

impl Slave {
    pub fn new(bus: Rs485Bus) -> Self {
        Self { bus }
    }

    /// Block until a frame addressed to this node arrives, then respond.
    pub fn run_forever(&mut self) {
        loop {
            match self.bus.receive() {
                Ok(frame) => self.handle(frame),
                Err(BusError::AddressMismatch) => {
                    /* Not for us — keep listening */
                }
                Err(BusError::Timeout) => {
                    /* Bus idle — keep listening */
                }
                Err(e) => {
                    eprintln!("Receive error: {:?}", e);
                }
            }
        }
    }

    fn handle(&mut self, frame: Frame) {
        match frame.command {
            CMD_READ_TEMP => {
                let temp_raw: i16 = 235;   /* 23.5 °C */
                let response = Frame {
                    address: frame.address,
                    command: CMD_ACK,
                    payload: temp_raw.to_le_bytes().to_vec(),
                };
                if let Err(e) = self.bus.send(&response) {
                    eprintln!("TX error: {:?}", e);
                }
            }
            unknown => {
                eprintln!("Unknown command 0x{:02X}", unknown);
            }
        }
    }
}
```

---

### 5. Main Entry Point

```rust
// src/main.rs
mod frame;
mod bus;
mod master;
mod slave;

use bus::Rs485Bus;
use master::Master;
use slave::Slave;
use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 3 {
        eprintln!("Usage: {} <master|slave> <port>", args[0]);
        eprintln!("Example: {} master /dev/ttyS1", args[0]);
        std::process::exit(1);
    }

    let role  = &args[1];
    let port  = &args[2];
    let baud  = 115_200;

    match role.as_str() {
        "master" => {
            let bus = Rs485Bus::open(port, baud, 0x00 /* master has no slave address */)
                .expect("Failed to open port");
            let mut m = Master::new(bus, vec![0x01, 0x02, 0x03, 0x04]);
            loop {
                m.poll_all();
                std::thread::sleep(std::time::Duration::from_secs(1));
            }
        }
        "slave" => {
            let my_addr: u8 = args.get(3)
                .and_then(|s| u8::from_str_radix(s.trim_start_matches("0x"), 16).ok())
                .unwrap_or(0x01);

            let bus = Rs485Bus::open(port, baud, my_addr)
                .expect("Failed to open port");
            Slave::new(bus).run_forever();
        }
        _ => {
            eprintln!("Unknown role '{}'. Use 'master' or 'slave'.", role);
            std::process::exit(1);
        }
    }
}
```

---

## Error Handling and Bus Arbitration

### Common Error Conditions

| Error                       | Cause                                   | Mitigation                                   |
|-----------------------------|-----------------------------------------|----------------------------------------------|
| **Framing error**           | Baud rate mismatch, noise               | Check baud rates, add termination/biasing    |
| **CRC failure**             | Electrical noise, partial frame         | Retransmit with exponential back-off         |
| **Timeout (no response)**   | Slave unpowered, wrong address          | Retry N times, then mark slave as offline    |
| **Bus contention**          | Two drivers active simultaneously       | Strict master/slave protocol; never let a slave initiate |
| **Overrun error**           | CPU too slow to read received bytes     | Use DMA RX or increase interrupt priority    |

### Retry Pattern (C)

```c
#define MAX_RETRIES 3

HAL_StatusTypeDef master_request_with_retry(uint8_t slave_addr,
                                             uint8_t command,
                                             RS485Frame *response)
{
    RS485Frame request = {
        .address = slave_addr,
        .command = command,
        .length  = 0,
    };

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (rs485_send_frame(&request) == HAL_OK) {
            HAL_StatusTypeDef s = rs485_receive_frame(response, 50);
            if (s == HAL_OK)
                return HAL_OK;
        }
        HAL_Delay(5 * (1 << attempt));   /* Exponential back-off: 5, 10, 20 ms */
    }
    return HAL_ERROR;
}
```

### Inter-Frame Gap (Modbus-Style)

For Modbus RTU compatibility, enforce a silence of at least **3.5 character times** between frames. At 9600 baud (≈ 1 ms/character) this is ≈ 3.5 ms; at 115200 baud it is ≈ 0.3 ms.

```c
/* Call after each frame, before the next transmission */
static void enforce_interframe_gap(uint32_t baud)
{
    uint32_t char_time_us = (10UL * 1000000UL) / baud;  /* 10 bits/char */
    uint32_t gap_us = (char_time_us * 35) / 10;          /* 3.5 chars     */
    /* At high baud rates, stay within timer resolution */
    HAL_Delay((gap_us + 999) / 1000);   /* round up to ms */
}
```

---

## Practical Considerations

### Baud Rate vs. Cable Length

As cable length increases, high-frequency attenuation limits the maximum safe baud rate. A practical rule of thumb from the RS-485 specification:

| Cable Length | Maximum Baud Rate |
|--------------|-------------------|
| < 15 m       | Up to 10 Mbps     |
| 100 m        | ≈ 1 Mbps          |
| 300 m        | ≈ 250 kbps        |
| 1200 m       | ≈ 100 kbps        |

### Ground Reference

RS-485 is differential but still requires a **signal ground** reference to keep the common-mode voltage within ±7 V of each node's GND. Always run a third wire for signal ground on long cable runs or across electrically noisy environments.

### Bus Idle and Fail-Safe Biasing

When no driver is active the bus voltage is undefined. Add **fail-safe bias resistors** (e.g. 560 Ω from A to VCC and B to GND) at the master end to guarantee that receivers see a mark (logic `1`) during idle. Some transceivers (e.g. MAX3430) have integrated fail-safe biasing.

### DE Timing

Ensure that the driver-enable signal is asserted **before** the UART starts clocking out bits, and de-asserted **after** the last stop bit has completed. On STM32, enable the UART RS-485 driver-enable hardware feature (`DEAT`/`DEDT` registers) to get sub-bit-time precision without software overhead.

### ESD and Isolation

For long cable runs in industrial environments, use **isolated RS-485 transceivers** (e.g. ADM2687E) and TVS diode protection on the A/B lines to guard against lightning surges and ground loop currents.

---

## Summary

RS-485 multi-drop networks combine a robust differential physical layer with software or hardware address recognition to enable reliable communication among dozens of nodes over long cables. The key design elements are:

**Physical layer:** RS-485 differential signalling provides noise immunity and allows cable runs up to 1200 m. Correct termination (120 Ω at each cable end) and fail-safe biasing are essential for reliable operation. The MCU UART connects to the bus through a transceiver whose DE pin must be controlled to switch between transmit and receive modes.

**Addressing:** Each frame carries an address byte identifying the intended recipient. Slaves filter frames in software or, more efficiently, in hardware using 9-bit UART mode (mark/space parity). In 9-bit mode, the UART wakes the CPU only on address frames, dramatically reducing interrupt overhead.

**Frame structure:** A minimal frame consists of an address byte, a command byte, a length byte, an optional payload, and a 16-bit CRC. CRC-16/Modbus is the conventional choice, providing strong error detection at low computational cost.

**Master/slave protocol:** A single master polls each slave in turn. Slaves respond only when addressed. This eliminates bus contention and simplifies arbitration. Broadcast frames (address `0xFF`) can be used for commands that all nodes should execute simultaneously.

**Robustness:** Retry logic with exponential back-off, inter-frame silence gaps, and strict DE timing prevent lost frames and bus collisions. Error counters and watchdog timeouts allow the master to detect and report offline nodes.

**C/C++ and Rust** both map naturally onto this architecture. C/C++ code targets bare-metal MCU HAL layers directly and exposes precise control over peripheral registers (e.g. STM32 HAL, AVR UCSR0A MPCM). Rust's type system enforces protocol correctness at compile time, and the `serialport` crate provides cross-platform serial I/O suitable for Linux-based gateway nodes or development/testing on a PC.

Together these techniques form the foundation of widely deployed industrial protocols such as Modbus RTU, DMX-512, LIN, and many proprietary sensor networks.

---

*End of Chapter 21 — Multi-Drop Networks*