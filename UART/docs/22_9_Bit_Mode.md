# 22. UART 9-Bit Mode

**Conceptual sections:**
- Frame structure showing how bit 9 acts as the address/data flag
- Multi-processor bus topology with master and up to 254 slaves
- The address detection / mute mode state machine (hardware-accelerated filtering)
- Register tables for both STM32 and AVR ATmega peripherals

**C/C++ examples (4 total):**
1. **STM32 HAL** — master side, sending address and data frames via `HAL_UART_Transmit` with 9-bit word length
2. **AVR ATmega MPCM** — interrupt-driven slave with hardware mute mode, ring buffer, and `RXB8`-before-`UDR` read ordering
3. **STM32 bare-metal** — direct register access for both TX and RX, no HAL dependency
4. **C++ RAII wrapper** — a modern C++ abstraction with `std::span`, callbacks, and ownership semantics

**Rust examples (3 total):**
5. **`embedded-hal` abstraction** — `Frame9` struct, `Uart9BitWrite/Read` traits, `SlaveFilter` state machine
6. **STM32 PAC/HAL** — `stm32f4xx-hal` with 9-bit config and inline filter demonstration
7. **Interrupt-driven slave** — `heapless::spsc::Queue` for safe ISR→main data transfer, `AtomicBool` for addressed state

**Advanced topics:** end-of-message strategies, RS-485 direction control (DE pin + TC flag), and a comparison table against Modbus RTU and CAN Bus.

> **Topic:** Using 9-bit data frames for addressing in multi-processor systems

---

## Table of Contents

1. [Overview](#overview)
2. [9-Bit Frame Structure](#9-bit-frame-structure)
3. [Multi-Processor Communication Architecture](#multi-processor-communication-architecture)
4. [Address Detection Mechanism](#address-detection-mechanism)
5. [Hardware Configuration](#hardware-configuration)
6. [Programming in C/C++](#programming-in-cc)
7. [Programming in Rust](#programming-in-rust)
8. [Advanced Use Cases](#advanced-use-cases)
9. [Summary](#summary)

---

## Overview

In standard UART communication, data frames consist of a **start bit**, **7 or 8 data bits**, an optional **parity bit**, and one or more **stop bits**. The 9-bit mode extends this by adding a **ninth data bit** — often called the **address/data bit** or **TB8/RB8 bit** — that serves as a flag to distinguish between **address frames** and **data frames**.

This mechanism was designed specifically for **multi-processor** or **multi-node** serial networks where one master device communicates selectively with multiple slave devices over a shared bus. Rather than every slave parsing every byte, the ninth bit allows slaves to quickly filter traffic and wake up only when addressed.

### Key Benefits

- **Efficiency:** Slaves can ignore data frames not meant for them at the hardware level, reducing CPU interrupts.
- **Scalability:** Supports up to 254 addressable nodes (address `0x00` is often reserved for broadcast, `0xFF` for reset/global).
- **Low overhead:** No additional protocol bytes needed; addressing is embedded in the frame itself.
- **Determinism:** Useful in real-time systems where interrupt latency must be minimized.

---

## 9-Bit Frame Structure

A 9-bit UART frame looks as follows on the wire:

```
  ___                                                        ______
     |_START_|_D0_|_D1_|_D2_|_D3_|_D4_|_D5_|_D6_|_D7_|_D8_|STOP
                                                         ^
                                                    9th bit (address/data flag)
```

| Bit Position | Name        | Value Meaning                                    |
|:------------:|-------------|--------------------------------------------------|
| Bit 8 = `1`  | Address bit | The 8-bit payload (D0–D7) is a **node address** |
| Bit 8 = `0`  | Data bit    | The 8-bit payload (D0–D7) is **application data** |

### Protocol Flow

```
Master:   [ADDR: 0x03, bit8=1] --> [DATA: 0xAB, bit8=0] --> [DATA: 0xCD, bit8=0] ...
              ^                         ^                         ^
         All slaves wake up        Only slave 0x03          Only slave 0x03
         and compare address       accepts and processes    accepts and processes
```

---

## Multi-Processor Communication Architecture

### Typical Topology

```
            +--------+
            | Master |
            +---+----+
                |  (shared TX line)
    +-----------+-----------+-----------+
    |           |           |           |
+---+---+   +---+---+   +---+---+   +---+---+
|Slave 1|   |Slave 2|   |Slave 3|   |Slave N|
|0x01   |   |0x02   |   |0x03   |   |0xNN   |
+-------+   +-------+   +-------+   +-------+
```

In a **half-duplex** or **RS-485** variant, all nodes share the same differential pair. The master drives the bus, and only the addressed slave responds (switching its driver on).

### Node Address Assignment

- Address `0x00`: Often used for **broadcast** — all slaves process the subsequent data.
- Address `0xFF`: Often used as a **reset** or **synchronization** token.
- Addresses `0x01`–`0xFE`: Individual nodes (254 possible slaves).

---

## Address Detection Mechanism

### Mute Mode (UART IDLE / Address Mark Detection)

Most microcontroller UART peripherals implement a **mute mode** where the receiver hardware silences itself until a frame with bit 9 = `1` is received. This is sometimes called:

- **Address Mark Wakeup** (ARM/STM32 terminology)
- **Multi-processor Communication** (AVR/8051 terminology)
- **MPCM** — Multi-Processor Communication Mode (AVR ATmega)

When in mute mode:

1. The receiver ignores all incoming bytes (bit 9 = `0`).
2. When a byte arrives with bit 9 = `1`, the receiver exits mute mode.
3. The slave reads the address byte and compares it to its own address.
4. If it matches (or is a broadcast), the slave **stays awake** and processes subsequent data bytes.
5. If it does not match, the slave **re-enters mute mode** immediately.

This mechanism keeps the slave CPU's interrupt load minimal — it is only interrupted for frames it actually needs to handle.

---

## Hardware Configuration

### Register Overview (Generic / STM32-style)

| Register | Field     | Description                                      |
|----------|-----------|--------------------------------------------------|
| `CR1`    | `M`       | Word length: `1` = 9-bit mode                    |
| `CR1`    | `PCE`     | Parity control: `0` = disabled (use bit 9 manually) |
| `CR1`    | `WAKE`    | Wakeup method: `0` = idle line, `1` = address mark |
| `CR1`    | `RWU`     | Receiver wakeup: `1` = enter mute mode           |
| `CR2`    | `ADD`     | Node address (used with address-mark wakeup)     |
| `DR`/`TDR` | bits 0–8 | 9-bit transmit/receive data register           |

### AVR ATmega — MPCM Mode

| Register  | Field   | Description                                      |
|-----------|---------|--------------------------------------------------|
| `UCSRnA`  | `MPCM`  | Multi-Processor Communication Mode enable        |
| `UCSRnC`  | `UCSZn` | `111` = 9-bit character size                     |
| `UCSRnB`  | `TXB8`  | Transmit bit 9 (address/data flag)               |
| `UCSRnB`  | `RXB8`  | Received bit 9 (read before `UDRn`)              |

---

## Programming in C/C++

### Example 1 — STM32 HAL: Master Transmit with 9-Bit Frames

```c
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

UART_HandleTypeDef huart1;

/**
 * @brief Initialize UART in 9-bit word-length mode.
 *        No hardware parity — bit 9 is managed manually.
 */
void UART_9Bit_Init(void)
{
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_9B;  // 9-bit mode
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;    // No parity; we use bit 9 ourselves
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        /* Initialization error — handle appropriately */
        while (1);
    }
}

/**
 * @brief Send an address frame (bit 9 = 1).
 * @param address  8-bit node address (0x01 – 0xFE)
 */
void UART_SendAddress(uint8_t address)
{
    /* In 9-bit mode the HAL uses a uint16_t buffer.
       Set bit 8 (the 9th bit) to signal an address frame. */
    uint16_t frame = (uint16_t)address | 0x0100u;   // bit 8 = 1

    HAL_UART_Transmit(&huart1, (uint8_t *)&frame, 1, HAL_MAX_DELAY);
}

/**
 * @brief Send a data frame (bit 9 = 0).
 * @param data  8-bit payload byte
 */
void UART_SendData(uint8_t data)
{
    uint16_t frame = (uint16_t)data & 0x00FFu;      // bit 8 = 0

    HAL_UART_Transmit(&huart1, (uint8_t *)&frame, 1, HAL_MAX_DELAY);
}

/**
 * @brief Transmit a complete addressed message.
 * @param address  Target slave node address
 * @param payload  Pointer to data buffer
 * @param length   Number of data bytes to send
 */
void UART_SendMessage(uint8_t address, const uint8_t *payload, uint16_t length)
{
    /* 1. Broadcast the address — all slaves wake up and compare */
    UART_SendAddress(address);

    /* 2. Send data bytes — only the addressed slave processes these */
    for (uint16_t i = 0; i < length; i++) {
        UART_SendData(payload[i]);
    }
}

/* Usage example */
int main(void)
{
    HAL_Init();
    UART_9Bit_Init();

    uint8_t message[] = { 0xDE, 0xAD, 0xBE, 0xEF };

    /* Address slave node 0x03 and send 4 bytes */
    UART_SendMessage(0x03, message, sizeof(message));

    /* Broadcast to all slaves (address 0x00) */
    UART_SendMessage(0x00, message, sizeof(message));

    while (1) { /* application loop */ }
}
```

---

### Example 2 — AVR ATmega: MPCM Slave with Address Filtering

```c
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdbool.h>
#include <stdint.h>

#define MY_NODE_ADDRESS   0x03u
#define BROADCAST_ADDRESS 0x00u
#define BAUD              115200UL
#define UBRR_VALUE        ((F_CPU / (16UL * BAUD)) - 1)

/* Receive buffer */
#define RX_BUF_SIZE 64
static volatile uint8_t  rx_buffer[RX_BUF_SIZE];
static volatile uint8_t  rx_head = 0;
static volatile uint8_t  rx_tail = 0;
static volatile bool     addressed = false;

/**
 * @brief Initialise USART0 in 9-bit MPCM (Multi-Processor Communication) mode.
 *        The receiver starts in mute mode — it ignores data frames (bit9=0)
 *        until it sees an address frame (bit9=1).
 */
void USART_Init(void)
{
    /* Set baud rate */
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);

    /* Enable RX, TX, and RX Complete interrupt */
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);

    /* 9-bit character size: UCSZ02=1, UCSZ01=1, UCSZ00=1 */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);   // UCSZ00..01 in UCSR0C
    UCSR0B |= (1 << UCSZ02);                   // UCSZ02 in UCSR0B

    /* Enter MPCM mode — receiver ignores frames with bit9=0 */
    UCSR0A = (1 << MPCM0);

    sei();
}

/**
 * @brief USART RX Complete ISR.
 *
 *  When MPCM=1, only frames with RXB8=1 trigger this ISR.
 *  We compare the address; if it matches, clear MPCM to receive data.
 *  When MPCM=0, all subsequent frames trigger this ISR — we collect data
 *  until signalled by application logic to re-enter mute mode.
 */
ISR(USART_RX_vect)
{
    /* CRITICAL: Read RXB8 BEFORE reading UDR0 */
    bool bit9    = (UCSR0B & (1 << RXB80)) != 0;
    uint8_t data = UDR0;

    if (bit9) {
        /* This is an ADDRESS frame — check if it's for us */
        if (data == MY_NODE_ADDRESS || data == BROADCAST_ADDRESS) {
            /* Our address: exit mute mode, accept subsequent data */
            UCSR0A &= ~(1 << MPCM0);
            addressed = true;
        } else {
            /* Not for us: stay (or return to) mute mode */
            UCSR0A |= (1 << MPCM0);
            addressed = false;
        }
    } else {
        /* DATA frame — only reached when MPCM=0 (we are addressed) */
        uint8_t next = (rx_head + 1) % RX_BUF_SIZE;
        if (next != rx_tail) {
            rx_buffer[rx_head] = data;
            rx_head = next;
        }
        /* NOTE: Application must call USART_ReturnToMute() after
           receiving a complete message to re-enter mute mode. */
    }
}

/**
 * @brief Re-enter mute mode after processing a complete message.
 *        Call this at end-of-message so the slave ignores further
 *        data frames not directed at it.
 */
void USART_ReturnToMute(void)
{
    addressed = false;
    UCSR0A |= (1 << MPCM0);
}

/**
 * @brief Read one byte from the receive buffer (non-blocking).
 * @param out  Pointer to store received byte
 * @return     true if a byte was available, false otherwise
 */
bool USART_ReadByte(uint8_t *out)
{
    if (rx_head == rx_tail) return false;
    *out = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return true;
}

int main(void)
{
    USART_Init();

    while (1) {
        uint8_t byte;
        if (USART_ReadByte(&byte)) {
            /* Process received byte ... */

            /* When a complete message is done, return to mute mode */
            /* USART_ReturnToMute(); */
        }
    }
}
```

---

### Example 3 — Bare-Metal C: Master with Direct Register Access (STM32F4)

```c
#include <stdint.h>
#include <stdbool.h>

/* Base addresses — adapt for your target */
#define USART1_BASE   0x40011000UL
#define RCC_BASE      0x40023800UL

typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} USART_TypeDef;

#define USART1  ((USART_TypeDef *)USART1_BASE)

/* USART CR1 bit definitions */
#define USART_CR1_UE    (1u << 13)   // UART enable
#define USART_CR1_M     (1u << 12)   // 9-bit word length
#define USART_CR1_TE    (1u << 3)    // Transmitter enable
#define USART_CR1_RE    (1u << 2)    // Receiver enable
#define USART_CR1_RWU   (1u << 1)    // Receiver wakeup (mute)
#define USART_CR1_WAKE  (1u << 11)   // Wakeup method: 1=address mark

/* USART SR bit definitions */
#define USART_SR_RXNE   (1u << 5)    // Read data register not empty
#define USART_SR_TXE    (1u << 7)    // Transmit data register empty
#define USART_SR_TC     (1u << 6)    // Transmission complete

/**
 * @brief Configure USART1 for 9-bit address-mark wakeup mode at 115200 baud.
 *        Assumes APB2 clock = 84 MHz.
 */
void USART1_9Bit_Init(void)
{
    /* Enable USART1 clock (RCC_APB2ENR bit 4) — omitted for brevity */

    /* BRR = f_ck / baud = 84000000 / 115200 ≈ 730 (OVER8=0) */
    USART1->BRR = 730u;

    /* CR1: 9-bit word, address-mark wakeup, enable TX/RX, enable UART */
    USART1->CR1 = USART_CR1_M      /* 9-bit */
                | USART_CR1_WAKE   /* address-mark wakeup */
                | USART_CR1_TE
                | USART_CR1_RE
                | USART_CR1_UE;

    /* CR2: Set node address in ADD[3:0] for this slave (if acting as slave) */
    /* USART1->CR2 = (MY_NODE_ADDRESS & 0x0Fu); */
}

/**
 * @brief Transmit a 9-bit frame (master side).
 * @param data   8-bit payload
 * @param is_addr true → address frame (bit9=1), false → data frame (bit9=0)
 */
void USART1_Transmit9(uint8_t data, bool is_addr)
{
    /* Wait until TXE (transmit register empty) */
    while (!(USART1->SR & USART_SR_TXE));

    /* Build 9-bit value: bit 8 in DR[8] */
    uint32_t frame = (uint32_t)data;
    if (is_addr) {
        frame |= (1u << 8);   // Set bit 9
    }
    USART1->DR = frame & 0x1FFu;   // Write 9 bits
}

/**
 * @brief Receive a 9-bit frame (blocking).
 * @param is_addr  Output: true if bit9=1 (address frame)
 * @return         8-bit payload
 */
uint8_t USART1_Receive9(bool *is_addr)
{
    while (!(USART1->SR & USART_SR_RXNE));

    uint32_t frame = USART1->DR & 0x1FFu;
    *is_addr = (frame & (1u << 8)) != 0u;
    return (uint8_t)(frame & 0xFFu);
}

/**
 * @brief Master: send addressed message.
 */
void Master_SendMessage(uint8_t addr, const uint8_t *buf, uint32_t len)
{
    USART1_Transmit9(addr, true);          // Address frame

    for (uint32_t i = 0; i < len; i++) {
        USART1_Transmit9(buf[i], false);   // Data frames
    }

    /* Wait for last byte to complete */
    while (!(USART1->SR & USART_SR_TC));
}
```

---

### Example 4 — C++ RAII Wrapper for 9-Bit UART

```cpp
#include <cstdint>
#include <span>
#include <functional>

/**
 * @brief RAII wrapper for a 9-bit UART peripheral.
 *        Demonstrates a C++ abstraction over a bare-metal HAL.
 */
class Uart9Bit {
public:
    using RxCallback = std::function<void(uint8_t data, bool is_address)>;

    explicit Uart9Bit(uint32_t baud, uint8_t node_address)
        : node_address_(node_address)
    {
        init(baud);
    }

    ~Uart9Bit() { deinit(); }

    /* Non-copyable, non-movable (owns hardware resource) */
    Uart9Bit(const Uart9Bit &)            = delete;
    Uart9Bit &operator=(const Uart9Bit &) = delete;

    /**
     * @brief Transmit an address byte (bit9 = 1).
     */
    void sendAddress(uint8_t addr) {
        transmit9(addr, /*is_addr=*/true);
    }

    /**
     * @brief Transmit a data byte (bit9 = 0).
     */
    void sendData(uint8_t data) {
        transmit9(data, /*is_addr=*/false);
    }

    /**
     * @brief Transmit a complete message to a specific node.
     * @param target  Node address
     * @param payload Span of data bytes
     */
    void sendMessage(uint8_t target, std::span<const uint8_t> payload) {
        sendAddress(target);
        for (uint8_t byte : payload) {
            sendData(byte);
        }
    }

    /**
     * @brief Broadcast a message to all nodes (address 0x00).
     */
    void broadcast(std::span<const uint8_t> payload) {
        sendMessage(0x00, payload);
    }

    /**
     * @brief Register a callback invoked on each received frame.
     *        In a real implementation, called from ISR context.
     */
    void setRxCallback(RxCallback cb) {
        rx_callback_ = std::move(cb);
    }

    /**
     * @brief Returns this node's address.
     */
    [[nodiscard]] uint8_t nodeAddress() const noexcept {
        return node_address_;
    }

private:
    uint8_t    node_address_;
    RxCallback rx_callback_;

    void init(uint32_t baud) {
        /* Platform-specific UART initialisation (9-bit, address-mark wakeup) */
        (void)baud;
    }

    void deinit() {
        /* Disable peripheral, release resources */
    }

    void transmit9(uint8_t data, bool is_addr) {
        /* Platform-specific 9-bit transmit */
        (void)data;
        (void)is_addr;
    }
};

/* --- Slave application using the C++ wrapper --- */
int main()
{
    constexpr uint8_t MY_ADDR = 0x05;
    Uart9Bit uart(115200, MY_ADDR);

    bool in_message = false;

    uart.setRxCallback([&](uint8_t data, bool is_address) {
        if (is_address) {
            /* Address frame received */
            if (data == MY_ADDR || data == 0x00 /* broadcast */) {
                in_message = true;
            } else {
                in_message = false;   // Not for us
            }
        } else if (in_message) {
            /* Data frame for this slave — process it */
            (void)data;   // Handle data...
        }
    });

    /* Master side — transmit example */
    const uint8_t payload[] = { 0x01, 0x02, 0x03 };
    uart.sendMessage(MY_ADDR, std::span(payload));

    while (true) { /* event loop */ }
}
```

---

## Programming in Rust

### Example 5 — Rust: 9-Bit UART Abstraction with `embedded-hal`

```rust
//! UART 9-bit mode implementation for embedded Rust.
//! Targets a generic platform via `embedded-hal` traits.
//! 
//! Cargo.toml dependencies:
//!   embedded-hal = "1.0"
//!   nb = "1.0"

#![no_std]
#![no_main]

use core::convert::Infallible;

/// Represents a 9-bit UART frame.
/// The `address_bit` is `true` for address frames, `false` for data.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Frame9 {
    pub data: u8,
    pub address_bit: bool,
}

impl Frame9 {
    /// Create an address frame (bit 9 = 1).
    pub const fn address(addr: u8) -> Self {
        Self { data: addr, address_bit: true }
    }

    /// Create a data frame (bit 9 = 0).
    pub const fn data(byte: u8) -> Self {
        Self { data: byte, address_bit: false }
    }

    /// Encode as a 16-bit word for a hardware register (bit 8 = address flag).
    pub fn to_register(self) -> u16 {
        let mut v = self.data as u16;
        if self.address_bit {
            v |= 0x0100;
        }
        v
    }

    /// Decode from a 16-bit hardware register value.
    pub fn from_register(raw: u16) -> Self {
        Self {
            data: (raw & 0xFF) as u8,
            address_bit: (raw & 0x0100) != 0,
        }
    }
}

/// Trait for a UART peripheral that supports 9-bit frames.
pub trait Uart9BitWrite {
    type Error;
    fn write_frame(&mut self, frame: Frame9) -> nb::Result<(), Self::Error>;
    fn flush(&mut self) -> nb::Result<(), Self::Error>;
}

pub trait Uart9BitRead {
    type Error;
    fn read_frame(&mut self) -> nb::Result<Frame9, Self::Error>;
}

/// Master-side helper: sends an address frame followed by data frames.
pub fn send_message<U>(
    uart: &mut U,
    address: u8,
    payload: &[u8],
) -> Result<(), U::Error>
where
    U: Uart9BitWrite,
{
    // Transmit address frame
    nb::block!(uart.write_frame(Frame9::address(address)))?;

    // Transmit each data byte
    for &byte in payload {
        nb::block!(uart.write_frame(Frame9::data(byte)))?;
    }

    // Ensure all bytes are physically transmitted
    nb::block!(uart.flush())?;

    Ok(())
}

/// Slave-side address filter state machine.
#[derive(Debug, Default)]
pub struct SlaveFilter {
    my_address: u8,
    broadcast_address: u8,
    is_addressed: bool,
}

impl SlaveFilter {
    /// Create a new filter for the given node address.
    pub const fn new(my_address: u8) -> Self {
        Self {
            my_address,
            broadcast_address: 0x00,
            is_addressed: false,
        }
    }

    /// Process an incoming frame.
    /// Returns `Some(byte)` if the byte should be handled by the application,
    /// or `None` if the frame should be discarded.
    pub fn process(&mut self, frame: Frame9) -> Option<u8> {
        if frame.address_bit {
            // Address frame: check if this node should wake up
            self.is_addressed =
                frame.data == self.my_address ||
                frame.data == self.broadcast_address;
            None  // Address bytes are never forwarded to the application
        } else if self.is_addressed {
            // Data frame for this slave
            Some(frame.data)
        } else {
            // Data frame for a different slave — discard
            None
        }
    }

    /// Manually return to mute mode (e.g., after end-of-message marker).
    pub fn return_to_mute(&mut self) {
        self.is_addressed = false;
    }

    /// Returns whether this slave is currently addressed.
    pub fn is_addressed(&self) -> bool {
        self.is_addressed
    }
}
```

---

### Example 6 — Rust: STM32 Bare-Metal with `stm32f4xx-hal`

```rust
//! STM32F4 9-bit UART example using stm32f4xx-hal.
//!
//! Cargo.toml:
//!   [dependencies]
//!   stm32f4xx-hal = { version = "0.21", features = ["stm32f411"] }
//!   cortex-m = "0.7"
//!   cortex-m-rt = "0.7"
//!   nb = "1.0"

#![no_std]
#![no_main]

use cortex_m_rt::entry;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{Config, Serial},
};

// Re-use our Frame9 and SlaveFilter types from the previous example.
// (In a real project these would be in a shared crate.)

/// Newtype wrapper around the HAL serial that provides 9-bit frame access
/// by reaching into the PAC registers directly for the 9th bit.
struct Serial9Bit {
    /// The inner HAL serial (configured for 9-bit / no-parity)
    _serial: Serial<pac::USART1>,
    /// Direct reference to the USART1 PAC peripheral for bit-9 manipulation
    usart: pac::USART1,
}

impl Serial9Bit {
    fn transmit(&self, frame: Frame9) {
        // Wait for TXE (transmit data register empty)
        while self.usart.sr.read().txe().bit_is_clear() {}

        let raw: u16 = frame.to_register();
        // Write 9-bit value — bits [8:0] of the DR register
        // Safety: single-writer, no data race in single-threaded context
        self.usart.dr.write(|w| unsafe { w.dr().bits(raw) });
    }

    fn receive(&self) -> Frame9 {
        // Wait for RXNE (receive data register not empty)
        while self.usart.sr.read().rxne().bit_is_clear() {}

        let raw = self.usart.dr.read().dr().bits();
        Frame9::from_register(raw)
    }

    fn flush(&self) {
        while self.usart.sr.read().tc().bit_is_clear() {}
    }
}

// --- Frame9 and SlaveFilter (abbreviated inline) ---

#[derive(Debug, Clone, Copy)]
struct Frame9 { data: u8, address_bit: bool }

impl Frame9 {
    fn address(a: u8) -> Self { Self { data: a, address_bit: true } }
    fn data(d: u8)    -> Self { Self { data: d, address_bit: false } }
    fn to_register(self) -> u16 {
        (self.data as u16) | if self.address_bit { 0x0100 } else { 0 }
    }
    fn from_register(r: u16) -> Self {
        Self { data: (r & 0xFF) as u8, address_bit: (r & 0x0100) != 0 }
    }
}

struct SlaveFilter { my_addr: u8, addressed: bool }

impl SlaveFilter {
    fn new(addr: u8) -> Self { Self { my_addr: addr, addressed: false } }
    fn process(&mut self, f: Frame9) -> Option<u8> {
        if f.address_bit {
            self.addressed = f.data == self.my_addr || f.data == 0x00;
            None
        } else if self.addressed {
            Some(f.data)
        } else {
            None
        }
    }
}

// ----------------------------------------------------------------

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();

    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

    let gpioa = dp.GPIOA.split();
    let tx_pin = gpioa.pa9.into_alternate::<7>();
    let rx_pin = gpioa.pa10.into_alternate::<7>();

    // Configure UART for 9-bit mode:
    // WordLength = 9 bits, no parity (HAL maps this to M=1, PCE=0)
    let config = Config::default()
        .baudrate(115200.bps())
        .wordlength_9()        // <-- 9-bit word length
        .parity_none();

    let serial = Serial::new(dp.USART1, (tx_pin, rx_pin), config, &clocks)
        .unwrap();

    // NOTE: Serial::new hands ownership of the peripheral to the HAL.
    // To access raw DR/SR for bit-9 manipulation we need a PAC reference.
    // In a real project, use `serial.release()` or write a custom driver.

    // --- Slave example ---
    let mut filter = SlaveFilter::new(0x03);

    // Simulate receiving a sequence of frames (in reality, from ISR/DMA)
    let frames = [
        Frame9::address(0x03),  // Address frame for us
        Frame9::data(0xAB),     // Data byte 1
        Frame9::data(0xCD),     // Data byte 2
        Frame9::address(0x07),  // Address frame for another slave
        Frame9::data(0xFF),     // This should be ignored
    ];

    let mut received: [u8; 16] = [0u8; 16];
    let mut count = 0usize;

    for frame in &frames {
        if let Some(byte) = filter.process(*frame) {
            if count < received.len() {
                received[count] = byte;
                count += 1;
            }
        }
    }

    // received[0] = 0xAB, received[1] = 0xCD, count = 2
    // The 0xFF after address 0x07 was correctly discarded.
    assert_eq!(count, 2);
    assert_eq!(received[0], 0xAB);
    assert_eq!(received[1], 0xCD);

    loop {}
}
```

---

### Example 7 — Rust: Interrupt-Driven Slave with `heapless` Ring Buffer

```rust
//! Interrupt-driven 9-bit UART slave using a lock-free ring buffer.
//!
//! Cargo.toml:
//!   heapless = "0.8"
//!   cortex-m = { version = "0.7", features = ["critical-section-single-core"] }

#![no_std]
#![no_main]

use core::sync::atomic::{AtomicBool, Ordering};
use heapless::spsc::{Consumer, Producer, Queue};
use cortex_m::interrupt;

// Static single-producer single-consumer queue (ISR → main thread)
static mut RX_QUEUE: Queue<u8, 64> = Queue::new();
static IS_ADDRESSED: AtomicBool = AtomicBool::new(false);

/// ISR-side producer (set up in init, used only in ISR).
static mut RX_PRODUCER: Option<Producer<'static, u8, 64>> = None;
/// Application-side consumer.
static mut RX_CONSUMER: Option<Consumer<'static, u8, 64>> = None;

const MY_ADDRESS: u8 = 0x03;
const BROADCAST:  u8 = 0x00;

/// Called once at startup to split the queue.
fn init_queue() {
    // Safety: called exactly once before interrupts are enabled.
    let (prod, cons) = unsafe { RX_QUEUE.split() };
    unsafe {
        RX_PRODUCER = Some(prod);
        RX_CONSUMER = Some(cons);
    }
}

/// Simulates the UART RX interrupt handler.
/// On real hardware, register this as `#[interrupt] fn USART1()`.
fn uart_rx_isr(raw_dr: u16) {
    let frame = Frame9::from_register(raw_dr);

    if frame.address_bit {
        let for_us = frame.data == MY_ADDRESS || frame.data == BROADCAST;
        IS_ADDRESSED.store(for_us, Ordering::Relaxed);
        // Do NOT enqueue the address byte itself.
    } else if IS_ADDRESSED.load(Ordering::Relaxed) {
        // Safety: only this ISR writes to the producer.
        if let Some(prod) = unsafe { RX_PRODUCER.as_mut() } {
            // If the queue is full, the byte is silently dropped.
            let _ = prod.enqueue(frame.data);
        }
    }
    // Frames for other slaves are silently discarded.
}

/// Application-level receive — call from main loop.
fn app_receive() -> Option<u8> {
    // Safety: only the main thread reads from the consumer.
    unsafe { RX_CONSUMER.as_mut()?.dequeue() }
}

// ---- Inline Frame9 for completeness ----

#[derive(Clone, Copy)]
struct Frame9 { data: u8, address_bit: bool }

impl Frame9 {
    fn from_register(r: u16) -> Self {
        Self { data: (r & 0xFF) as u8, address_bit: (r & 0x0100) != 0 }
    }
}

// ---- Application entry ----

#[cortex_m_rt::entry]
fn main() -> ! {
    init_queue();

    // Enable UART RX interrupt here (platform-specific)...

    loop {
        while let Some(byte) = app_receive() {
            // Process byte from the addressed message
            let _ = byte;
        }

        // Enter low-power sleep waiting for next interrupt
        cortex_m::asm::wfi();
    }
}
```

---

## Advanced Use Cases

### End-of-Message Signalling

The protocol needs a way for slaves to know when a message has ended so they can return to mute mode. Common strategies:

1. **Fixed-length messages:** The master always sends N bytes after an address. The slave counts N bytes then re-mutes.
2. **Length prefix:** The first data byte after the address specifies the number of data bytes to follow.
3. **Escape / terminator byte:** A reserved byte value (e.g., `0xFE`) signals end-of-message.
4. **CRC frame:** The last frame contains a CRC; when the slave verifies it, it knows the message is complete.

```c
/* Example: length-prefixed protocol in C */
void Slave_ReceiveMessage(void)
{
    /* First data byte is the message length */
    uint8_t length = 0;
    while (!USART_ReadByte(&length));   // Wait for length byte

    uint8_t payload[64] = {0};
    for (uint8_t i = 0; i < length && i < sizeof(payload); i++) {
        while (!USART_ReadByte(&payload[i]));
    }

    /* Message complete — re-enter mute mode */
    USART_ReturnToMute();

    /* Process payload... */
}
```

### RS-485 Direction Control

In RS-485 networks, a GPIO controls the transceiver's TX-enable pin. The master must assert TX-enable before transmitting and de-assert after:

```c
#define RS485_DE_PIN   GPIO_PIN_12
#define RS485_DE_PORT  GPIOA

static inline void RS485_EnableTX(void)  { HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET); }
static inline void RS485_DisableTX(void) { HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET); }

void Master_RS485_SendMessage(uint8_t addr, const uint8_t *buf, uint32_t len)
{
    RS485_EnableTX();
    UART_SendAddress(addr);
    for (uint32_t i = 0; i < len; i++) UART_SendData(buf[i]);
    /* Wait for TC (transmission complete) before releasing the bus */
    while (!(USART1->SR & USART_SR_TC));
    RS485_DisableTX();
}
```

### Comparing 9-Bit Mode with Alternatives

| Feature                  | 9-Bit UART        | Software Protocol (e.g., Modbus RTU) | CAN Bus           |
|--------------------------|-------------------|--------------------------------------|-------------------|
| **Addressing overhead**  | Zero extra bytes  | 1+ bytes                             | 11/29-bit ID      |
| **Filtering**            | Hardware          | Software                             | Hardware          |
| **Max nodes**            | 254               | Protocol-defined                     | 127 (CAN)         |
| **Bus topology**         | Shared UART / RS-485 | RS-485                            | Differential pair |
| **CPU overhead**         | Very low          | Moderate                             | Very low          |
| **Standard support**     | MCU-dependent     | Widespread                           | Widespread        |

---

## Summary

**UART 9-bit mode** extends the standard 8-bit data frame with a ninth bit that acts as an **address/data discriminator**, enabling efficient multi-node communication over a shared serial bus.

**Core mechanism:** The master sets bit 9 = `1` when transmitting a node address and bit 9 = `0` for all subsequent data bytes. Slave peripherals use **mute mode** (MPCM on AVR, RWU/address-mark wakeup on STM32) to remain silent until they detect an address frame, dramatically reducing unnecessary interrupts.

**Key implementation points:**
- Configure the UART peripheral for **9-bit word length** with **no parity** (parity and the 9th bit share hardware resources on many MCUs).
- On AVR, enable **MPCM** and set the character size to 9-bit (`UCSZ[2:0] = 0b111`); read `RXB8` *before* `UDR` in the ISR.
- On STM32, set `M=1` in `CR1`, use `WAKE=1` for address-mark detection, and access bit 8 of the 16-bit `DR` register.
- In **Rust**, model 9-bit frames as a struct (`Frame9`) and implement a **state-machine filter** (`SlaveFilter`) that cleanly separates address and data processing. Use `heapless::spsc::Queue` for safe ISR-to-application data transfer.
- For RS-485 networks, ensure the transmitter's **DE/RE pin** is properly asserted before and de-asserted *after* the last byte (`TC` flag, not `TXE`).
- Choose an **end-of-message** convention (fixed-length, length-prefix, terminator byte, or CRC) at the application layer, since the UART protocol itself only distinguishes address from data frames.

9-bit UART mode is a mature, hardware-accelerated solution for lightweight multi-drop serial networks and remains widely used in industrial automation, motor-drive systems, and embedded multi-processor architectures where Modbus, CAN, or more complex bus protocols would be overkill.

---

*Document generated for UART Series — Chapter 22: 9-Bit Mode*