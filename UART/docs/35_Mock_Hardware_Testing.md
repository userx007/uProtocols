# 35. Mock Hardware Testing — Unit Testing UART Drivers Without Physical Hardware

- **Introduction** — why mock testing matters (CI-friendliness, error injection, determinism), with a clear architecture diagram showing the HAL swap between test and production.
- **Core Concepts** — seam points (register access, status polling, ISR/DMA), and the Mock / Fake / Stub / Spy taxonomy applied specifically to UART.
- **C/C++ section** covers:
  - A complete `uart_hal.h` vtable abstraction with inline convenience wrappers
  - A full `uart_mock.c` implementation with TX log, RX queue, one-shot error injection, and call counters
  - A ring-buffer loopback mock for bidirectional protocol testing
  - Unity framework tests covering happy-path, timeout, mid-stream error, and flush-call verification
  - GoogleTest + GMock with `InSequence`, `EXPECT_CALL`, `SetArgPointee`, and `Times()` expectations
- **Rust section** covers:
  - A `trait Uart` with provided `write_all` / `read_exact` combinators
  - `MockUart` with `VecDeque` RX queue, `Vec` TX log, and `Option<UartError>` injection
  - Built-in `#[test]` suite for all edge cases
  - `FaultyUart` — fails after N successful operations for mid-buffer fault testing
  - `mockall` integration with `#[automock]` and `.expect_*()` DSL
  - `ChannelUart` using `std::sync::mpsc` for full-duplex multi-threaded simulation
- **Advanced Techniques** — echo/loopback simulation, periodic error injection (every Nth byte), ISR callback triggering
- **Test Coverage Strategy** — a structured checklist of happy-path, boundary, error, protocol, and interaction tests, plus a CI pipeline diagram
- **Summary table** comparing C/C++ vs Rust across every dimension

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Core Concepts & Architecture](#2-core-concepts--architecture)
3. [Abstraction Layer Design](#3-abstraction-layer-design)
4. [C/C++ Implementation](#4-cc-implementation)
   - 4.1 [HAL Abstraction with Function Pointers](#41-hal-abstraction-with-function-pointers)
   - 4.2 [Mock UART Backend](#42-mock-uart-backend)
   - 4.3 [Ring-Buffer Loopback Mock](#43-ring-buffer-loopback-mock)
   - 4.4 [Testing with Unity Framework](#44-testing-with-unity-framework)
   - 4.5 [C++ Mock with GoogleTest & GMock](#45-c-mock-with-googletest--gmock)
5. [Rust Implementation](#5-rust-implementation)
   - 5.1 [Trait-Based UART Abstraction](#51-trait-based-uart-abstraction)
   - 5.2 [Mock UART with embedded-hal](#52-mock-uart-with-embedded-hal)
   - 5.3 [Testing with the Mock](#53-testing-with-the-mock)
   - 5.4 [Simulating Errors & Edge Cases](#54-simulating-errors--edge-cases)
   - 5.5 [Using `mockall` for Advanced Mocking](#55-using-mockall-for-advanced-mocking)
6. [Advanced Techniques](#6-advanced-techniques)
   - 6.1 [Loopback & Echo Simulation](#61-loopback--echo-simulation)
   - 6.2 [Simulating Framing & Parity Errors](#62-simulating-framing--parity-errors)
   - 6.3 [Timing & Interrupt Simulation](#63-timing--interrupt-simulation)
7. [Test Coverage Strategies](#7-test-coverage-strategies)
8. [Summary](#8-summary)

---

## 1. Introduction

Embedded UART drivers are notoriously difficult to test because they depend on physical
hardware — a microcontroller's UART peripheral, a connected device, or a loopback cable.
**Mock Hardware Testing** solves this by replacing the real hardware interface with a
software simulation (a *mock*) that behaves identically from the driver's perspective,
while giving tests full control over inputs, outputs, and error conditions.

### Why mock UART hardware?

| Problem with real hardware     | Mock solution                              |
|--------------------------------|--------------------------------------------|
| Board may not be available in CI | Mock runs anywhere (PC, cloud, VM)       |
| Hard to inject framing errors  | Mock can inject arbitrary error flags      |
| Race conditions are timing-dependent | Mock provides deterministic behavior |
| Requires physical loopback cable | Mock has a software loopback buffer      |
| Slow to flash and run          | Mock tests execute in milliseconds         |
| Difficult to measure coverage  | Standard host-side tooling (gcov, llvm-cov)|

### Key Principle: Dependency Inversion

The UART *driver logic* must never directly call hardware registers. Instead, it calls an
**abstract interface** (function pointers in C, a virtual class in C++, or a trait in Rust).
Tests substitute the real hardware implementation with a mock implementation of that
same interface.

```
┌─────────────────────────────────────────────────────┐
│                  Test Code                          │
│                                                     │
│   ┌──────────────┐        ┌──────────────────────┐  │
│   │  UART Driver │◄──────►│   Mock UART Backend  │  │
│   │  (logic only)│        │  (in-memory buffers) │  │
│   └──────────────┘        └──────────────────────┘  │
│                                                     │
│              ▼ swap in production ▼                 │
│                                                     │
│   ┌──────────────┐        ┌──────────────────────┐  │
│   │  UART Driver │◄──────►│  Real HW Peripheral  │  │
│   │  (logic only)│        │  (registers/DMA/IRQ) │  │
│   └──────────────┘        └──────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

---

## 2. Core Concepts & Architecture

### 2.1 Seam Points

A *seam* is any place where you can substitute behaviour without editing the code under
test. For UART drivers the three natural seams are:

- **Register access** — replace `UART->DR = byte` with a write callback.
- **Status polling** — replace `while(!(UART->SR & TXE))` with a mock status flag.
- **Interrupt/DMA completion** — replace ISR registration with a mock trigger function.

### 2.2 Mock vs Fake vs Stub vs Spy

| Term    | Description                                              | UART example                             |
|---------|----------------------------------------------------------|------------------------------------------|
| **Stub**  | Returns canned values, no behaviour                    | Always reports RX ready                  |
| **Fake**  | Simplified working implementation                      | Ring-buffer loopback                     |
| **Mock**  | Records calls, asserts expectations                    | Checks `write()` called with exact bytes |
| **Spy**   | Wraps real object, records interactions                | Wraps real UART, logs every byte         |

---

## 3. Abstraction Layer Design

The hardware abstraction layer (HAL) is the pivot point for mocking. Define it once;
implement it twice — once for hardware, once for tests.

```
uart_hal.h       ← interface definition (shared)
uart_hal_hw.c    ← real peripheral implementation
uart_hal_mock.c  ← test mock implementation
uart_driver.c    ← business logic; uses only uart_hal.h
```

---

## 4. C/C++ Implementation

### 4.1 HAL Abstraction with Function Pointers

```c
/* uart_hal.h — portable UART hardware abstraction layer */
#ifndef UART_HAL_H
#define UART_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Error codes */
typedef enum {
    UART_OK              = 0,
    UART_ERR_TIMEOUT     = -1,
    UART_ERR_FRAMING     = -2,
    UART_ERR_PARITY      = -3,
    UART_ERR_OVERRUN     = -4,
    UART_ERR_NOT_READY   = -5,
} uart_status_t;

/* Opaque driver handle */
typedef struct uart_handle uart_handle_t;

/* HAL operations — one vtable per UART instance */
typedef struct {
    /* Transmit a single byte; block until sent or timeout expires */
    uart_status_t (*write_byte)(uart_handle_t *h, uint8_t byte,
                                uint32_t timeout_ms);

    /* Read a single byte; block until available or timeout expires */
    uart_status_t (*read_byte)(uart_handle_t *h, uint8_t *byte,
                               uint32_t timeout_ms);

    /* Non-blocking TX/RX ready queries */
    bool (*tx_ready)(uart_handle_t *h);
    bool (*rx_available)(uart_handle_t *h);

    /* Flush TX FIFO; wait until shift register is empty */
    uart_status_t (*flush)(uart_handle_t *h);

    /* Optional: inject error state (used by mocks/test hooks) */
    void (*inject_error)(uart_handle_t *h, uart_status_t err);
} uart_ops_t;

/* Concrete handle definition */
struct uart_handle {
    const uart_ops_t *ops;   /* pointer to vtable */
    void             *priv;  /* implementation-private data */
};

/* Convenience wrappers so callers don't dereference ops manually */
static inline uart_status_t
uart_write_byte(uart_handle_t *h, uint8_t b, uint32_t ms)
{ return h->ops->write_byte(h, b, ms); }

static inline uart_status_t
uart_read_byte(uart_handle_t *h, uint8_t *b, uint32_t ms)
{ return h->ops->read_byte(h, b, ms); }

static inline bool uart_tx_ready(uart_handle_t *h)
{ return h->ops->tx_ready(h); }

static inline bool uart_rx_available(uart_handle_t *h)
{ return h->ops->rx_available(h); }

static inline uart_status_t uart_flush(uart_handle_t *h)
{ return h->ops->flush(h); }

/* Write a buffer, byte by byte */
uart_status_t uart_write(uart_handle_t *h,
                         const uint8_t *buf, size_t len,
                         uint32_t timeout_ms);

/* Read exactly len bytes */
uart_status_t uart_read(uart_handle_t *h,
                        uint8_t *buf, size_t len,
                        uint32_t timeout_ms);

#endif /* UART_HAL_H */
```

```c
/* uart_driver.c — driver logic that knows nothing about real hardware */
#include "uart_hal.h"
#include <string.h>

uart_status_t uart_write(uart_handle_t *h,
                         const uint8_t *buf, size_t len,
                         uint32_t timeout_ms)
{
    for (size_t i = 0; i < len; i++) {
        uart_status_t s = uart_write_byte(h, buf[i], timeout_ms);
        if (s != UART_OK) return s;
    }
    return uart_flush(h);
}

uart_status_t uart_read(uart_handle_t *h,
                        uint8_t *buf, size_t len,
                        uint32_t timeout_ms)
{
    for (size_t i = 0; i < len; i++) {
        uart_status_t s = uart_read_byte(h, &buf[i], timeout_ms);
        if (s != UART_OK) return s;
    }
    return UART_OK;
}
```

---

### 4.2 Mock UART Backend

```c
/* uart_mock.h */
#ifndef UART_MOCK_H
#define UART_MOCK_H

#include "uart_hal.h"
#include <stddef.h>

#define MOCK_BUF_SIZE 256

typedef struct {
    /* Data the mock will hand to read_byte() calls */
    uint8_t  rx_data[MOCK_BUF_SIZE];
    size_t   rx_head, rx_tail;

    /* Data captured from write_byte() calls */
    uint8_t  tx_data[MOCK_BUF_SIZE];
    size_t   tx_len;

    /* Error to inject on next operation (UART_OK = no error) */
    uart_status_t pending_error;

    /* Counters for call verification */
    uint32_t write_calls;
    uint32_t read_calls;
    uint32_t flush_calls;
} uart_mock_priv_t;

/* Initialise a handle backed by the mock */
void uart_mock_init(uart_handle_t *h, uart_mock_priv_t *priv);

/* Feed bytes that read_byte() will return */
void uart_mock_feed_rx(uart_handle_t *h,
                       const uint8_t *data, size_t len);

/* Inject an error to be returned on the next operation */
void uart_mock_inject_error(uart_handle_t *h, uart_status_t err);

/* Reset all state (between tests) */
void uart_mock_reset(uart_handle_t *h);

#endif /* UART_MOCK_H */
```

```c
/* uart_mock.c */
#include "uart_mock.h"
#include <string.h>
#include <assert.h>

/* ── helpers ───────────────────────────────────────────────────────────── */

static uart_mock_priv_t *priv_of(uart_handle_t *h)
{
    return (uart_mock_priv_t *)h->priv;
}

static uart_status_t consume_error(uart_mock_priv_t *p)
{
    uart_status_t e = p->pending_error;
    p->pending_error = UART_OK;   /* one-shot */
    return e;
}

/* ── ops ────────────────────────────────────────────────────────────────── */

static uart_status_t mock_write_byte(uart_handle_t *h, uint8_t byte,
                                     uint32_t timeout_ms)
{
    (void)timeout_ms;
    uart_mock_priv_t *p = priv_of(h);
    p->write_calls++;

    uart_status_t e = consume_error(p);
    if (e != UART_OK) return e;

    assert(p->tx_len < MOCK_BUF_SIZE && "mock TX buffer overflow");
    p->tx_data[p->tx_len++] = byte;
    return UART_OK;
}

static uart_status_t mock_read_byte(uart_handle_t *h, uint8_t *byte,
                                    uint32_t timeout_ms)
{
    (void)timeout_ms;
    uart_mock_priv_t *p = priv_of(h);
    p->read_calls++;

    uart_status_t e = consume_error(p);
    if (e != UART_OK) return e;

    if (p->rx_head == p->rx_tail)
        return UART_ERR_TIMEOUT;   /* buffer empty → simulate timeout */

    *byte = p->rx_data[p->rx_head++];
    return UART_OK;
}

static bool mock_tx_ready(uart_handle_t *h)
{
    (void)h;
    return true;   /* mock is always ready to accept bytes */
}

static bool mock_rx_available(uart_handle_t *h)
{
    uart_mock_priv_t *p = priv_of(h);
    return p->rx_head != p->rx_tail;
}

static uart_status_t mock_flush(uart_handle_t *h)
{
    uart_mock_priv_t *p = priv_of(h);
    p->flush_calls++;
    return UART_OK;
}

static void mock_inject_error(uart_handle_t *h, uart_status_t err)
{
    priv_of(h)->pending_error = err;
}

/* ── vtable ─────────────────────────────────────────────────────────────── */

static const uart_ops_t mock_ops = {
    .write_byte   = mock_write_byte,
    .read_byte    = mock_read_byte,
    .tx_ready     = mock_tx_ready,
    .rx_available = mock_rx_available,
    .flush        = mock_flush,
    .inject_error = mock_inject_error,
};

/* ── public API ─────────────────────────────────────────────────────────── */

void uart_mock_init(uart_handle_t *h, uart_mock_priv_t *priv)
{
    memset(priv, 0, sizeof(*priv));
    h->ops  = &mock_ops;
    h->priv = priv;
}

void uart_mock_feed_rx(uart_handle_t *h, const uint8_t *data, size_t len)
{
    uart_mock_priv_t *p = priv_of(h);
    assert(p->rx_tail + len <= MOCK_BUF_SIZE && "mock RX buffer overflow");
    memcpy(&p->rx_data[p->rx_tail], data, len);
    p->rx_tail += len;
}

void uart_mock_inject_error(uart_handle_t *h, uart_status_t err)
{
    priv_of(h)->pending_error = err;
}

void uart_mock_reset(uart_handle_t *h)
{
    memset(priv_of(h), 0, sizeof(uart_mock_priv_t));
}
```

---

### 4.3 Ring-Buffer Loopback Mock

For protocols that require bidirectional conversation (e.g. AT-commands), wire two mock
instances together so TX of one feeds RX of the other.

```c
/* loopback_mock.c — connect two mock handles for full-duplex simulation */
#include "uart_mock.h"
#include <string.h>

typedef struct {
    uart_handle_t *peer;   /* bytes written here go to peer's RX buffer */
} loopback_priv_t;

static uart_status_t lb_write_byte(uart_handle_t *h, uint8_t byte,
                                   uint32_t timeout_ms)
{
    (void)timeout_ms;
    loopback_priv_t *lp = (loopback_priv_t *)h->priv;
    /* Write into the peer's mock RX queue */
    uart_mock_feed_rx(lp->peer, &byte, 1);
    return UART_OK;
}

/* Remaining ops delegate to the inner mock for read/flush/status */
```

---

### 4.4 Testing with Unity Framework

[Unity](https://github.com/ThrowTheSwitch/Unity) is a popular, lightweight C test runner
used in embedded projects.

```c
/* test_uart_driver.c */
#include "unity.h"
#include "uart_hal.h"
#include "uart_mock.h"
#include <string.h>

static uart_handle_t    mock_handle;
static uart_mock_priv_t mock_data;

void setUp(void)
{
    uart_mock_init(&mock_handle, &mock_data);
}

void tearDown(void)
{
    uart_mock_reset(&mock_handle);
}

/* ── Test: write sends the correct bytes ──────────────────────────────── */
void test_uart_write_sends_exact_bytes(void)
{
    const uint8_t payload[] = { 0xAA, 0xBB, 0xCC };

    uart_status_t s = uart_write(&mock_handle, payload, sizeof(payload), 100);

    TEST_ASSERT_EQUAL(UART_OK, s);
    TEST_ASSERT_EQUAL(sizeof(payload), mock_data.tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, mock_data.tx_data, sizeof(payload));
    TEST_ASSERT_EQUAL(1, mock_data.flush_calls);
}

/* ── Test: read retrieves injected bytes ──────────────────────────────── */
void test_uart_read_returns_fed_bytes(void)
{
    const uint8_t incoming[] = { 0x01, 0x02, 0x03, 0x04 };
    uart_mock_feed_rx(&mock_handle, incoming, sizeof(incoming));

    uint8_t buf[4] = {0};
    uart_status_t s = uart_read(&mock_handle, buf, sizeof(buf), 100);

    TEST_ASSERT_EQUAL(UART_OK, s);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(incoming, buf, sizeof(incoming));
}

/* ── Test: timeout when RX buffer is empty ────────────────────────────── */
void test_uart_read_timeout_on_empty_buffer(void)
{
    uint8_t byte;
    uart_status_t s = uart_read_byte(&mock_handle, &byte, 50);
    TEST_ASSERT_EQUAL(UART_ERR_TIMEOUT, s);
}

/* ── Test: injected TX framing error is propagated ────────────────────── */
void test_uart_write_propagates_framing_error(void)
{
    uart_mock_inject_error(&mock_handle, UART_ERR_FRAMING);
    const uint8_t buf[] = { 0xFF };

    uart_status_t s = uart_write(&mock_handle, buf, 1, 100);
    TEST_ASSERT_EQUAL(UART_ERR_FRAMING, s);
    /* No bytes should have been recorded */
    TEST_ASSERT_EQUAL(0, mock_data.tx_len);
}

/* ── Test: overrun error mid-read ─────────────────────────────────────── */
void test_uart_read_overrun_mid_stream(void)
{
    const uint8_t first[] = { 0x11, 0x22 };
    uart_mock_feed_rx(&mock_handle, first, sizeof(first));

    uint8_t buf[4] = {0};
    /* Read 2 bytes fine, then inject overrun for byte 3 */
    TEST_ASSERT_EQUAL(UART_OK, uart_read_byte(&mock_handle, &buf[0], 10));
    TEST_ASSERT_EQUAL(UART_OK, uart_read_byte(&mock_handle, &buf[1], 10));
    uart_mock_inject_error(&mock_handle, UART_ERR_OVERRUN);
    TEST_ASSERT_EQUAL(UART_ERR_OVERRUN, uart_read_byte(&mock_handle, &buf[2], 10));

    TEST_ASSERT_EQUAL_HEX8(0x11, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22, buf[1]);
}

/* ── Test: flush is called exactly once per uart_write() ──────────────── */
void test_flush_called_once(void)
{
    const uint8_t data[] = "Hello";
    uart_write(&mock_handle, data, sizeof(data) - 1, 100);
    TEST_ASSERT_EQUAL(1, mock_data.flush_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uart_write_sends_exact_bytes);
    RUN_TEST(test_uart_read_returns_fed_bytes);
    RUN_TEST(test_uart_read_timeout_on_empty_buffer);
    RUN_TEST(test_uart_write_propagates_framing_error);
    RUN_TEST(test_uart_read_overrun_mid_stream);
    RUN_TEST(test_flush_called_once);
    return UNITY_END();
}
```

---

### 4.5 C++ Mock with GoogleTest & GMock

```cpp
// uart_gmock.hpp — GMock-based UART mock (C++)
#pragma once
#include <gmock/gmock.h>
#include "uart_hal.h"

class UartInterface {
public:
    virtual ~UartInterface() = default;
    virtual uart_status_t write_byte(uint8_t byte, uint32_t timeout_ms) = 0;
    virtual uart_status_t read_byte(uint8_t *byte, uint32_t timeout_ms)  = 0;
    virtual bool          tx_ready()      = 0;
    virtual bool          rx_available()  = 0;
    virtual uart_status_t flush()         = 0;
};

class MockUart : public UartInterface {
public:
    MOCK_METHOD(uart_status_t, write_byte,
                (uint8_t byte, uint32_t timeout_ms), (override));
    MOCK_METHOD(uart_status_t, read_byte,
                (uint8_t *byte, uint32_t timeout_ms),  (override));
    MOCK_METHOD(bool,          tx_ready,    (),  (override));
    MOCK_METHOD(bool,          rx_available,(),  (override));
    MOCK_METHOD(uart_status_t, flush,       (),  (override));
};
```

```cpp
// test_uart_gmock.cpp
#include <gtest/gtest.h>
#include "uart_gmock.hpp"
#include "uart_driver.hpp"   // C++ wrapper around the driver logic
using ::testing::_;
using ::testing::Return;
using ::testing::InSequence;
using ::testing::DoAll;
using ::testing::SetArgPointee;

class UartDriverTest : public ::testing::Test {
protected:
    MockUart mock;
    // UartDriver wraps UartInterface; inject mock via constructor
    UartDriver driver{mock};
};

TEST_F(UartDriverTest, WriteSendsAllBytes)
{
    const std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};

    // Expect write_byte called 4× with successive values
    {
        InSequence seq;
        for (uint8_t b : payload)
            EXPECT_CALL(mock, write_byte(b, _)).WillOnce(Return(UART_OK));
    }
    EXPECT_CALL(mock, flush()).WillOnce(Return(UART_OK));

    EXPECT_EQ(UART_OK, driver.write(payload.data(), payload.size(), 100));
}

TEST_F(UartDriverTest, ReadReturnsInjectedBytes)
{
    const std::vector<uint8_t> incoming = {0xCA, 0xFE};

    {
        InSequence seq;
        for (uint8_t b : incoming) {
            EXPECT_CALL(mock, read_byte(_, _))
                .WillOnce(DoAll(SetArgPointee<0>(b), Return(UART_OK)));
        }
    }

    std::vector<uint8_t> buf(2);
    EXPECT_EQ(UART_OK, driver.read(buf.data(), buf.size(), 100));
    EXPECT_EQ(incoming, buf);
}

TEST_F(UartDriverTest, WritePropagatesFramingError)
{
    EXPECT_CALL(mock, write_byte(_, _))
        .WillOnce(Return(UART_ERR_FRAMING));

    const uint8_t byte = 0xFF;
    EXPECT_EQ(UART_ERR_FRAMING, driver.write(&byte, 1, 100));
}

TEST_F(UartDriverTest, FlushCalledExactlyOnce)
{
    EXPECT_CALL(mock, write_byte(_, _))
        .WillRepeatedly(Return(UART_OK));
    EXPECT_CALL(mock, flush()).Times(1).WillOnce(Return(UART_OK));

    const uint8_t buf[] = {1, 2, 3};
    driver.write(buf, sizeof(buf), 100);
}
```

---

## 5. Rust Implementation

### 5.1 Trait-Based UART Abstraction

```rust
// src/uart_hal.rs — abstract UART trait
use core::fmt;

/// Errors a UART operation may produce.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UartError {
    Timeout,
    Framing,
    Parity,
    Overrun,
    BufferFull,
}

impl fmt::Display for UartError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Timeout    => write!(f, "UART timeout"),
            Self::Framing    => write!(f, "UART framing error"),
            Self::Parity     => write!(f, "UART parity error"),
            Self::Overrun    => write!(f, "UART overrun"),
            Self::BufferFull => write!(f, "UART buffer full"),
        }
    }
}

/// Core trait every UART backend must implement.
pub trait Uart {
    /// Transmit a single byte (blocking).
    fn write_byte(&mut self, byte: u8) -> Result<(), UartError>;

    /// Receive a single byte (blocking).
    fn read_byte(&mut self) -> Result<u8, UartError>;

    /// Returns `true` if the transmitter can accept a byte immediately.
    fn tx_ready(&self) -> bool;

    /// Returns `true` if at least one byte is waiting in the RX buffer.
    fn rx_available(&self) -> bool;

    /// Block until the TX shift register drains completely.
    fn flush(&mut self) -> Result<(), UartError>;

    // ── Provided higher-level methods ─────────────────────────────────────

    /// Write a slice of bytes, flushing when done.
    fn write_all(&mut self, data: &[u8]) -> Result<(), UartError> {
        for &b in data {
            self.write_byte(b)?;
        }
        self.flush()
    }

    /// Read exactly `buf.len()` bytes.
    fn read_exact(&mut self, buf: &mut [u8]) -> Result<(), UartError> {
        for slot in buf.iter_mut() {
            *slot = self.read_byte()?;
        }
        Ok(())
    }
}
```

---

### 5.2 Mock UART with embedded-hal

```rust
// src/mock_uart.rs — software mock implementing the Uart trait
use std::collections::VecDeque;
use crate::uart_hal::{Uart, UartError};

/// A UART mock suitable for unit tests.
pub struct MockUart {
    /// Bytes that `read_byte()` will return (pre-loaded by tests).
    rx_queue: VecDeque<u8>,

    /// Bytes captured by `write_byte()` (inspected by tests).
    tx_log: Vec<u8>,

    /// When `Some(e)`, the next operation returns this error (one-shot).
    pending_error: Option<UartError>,

    /// Call counters for assertion-based tests.
    pub write_calls: usize,
    pub read_calls:  usize,
    pub flush_calls: usize,
}

impl MockUart {
    pub fn new() -> Self {
        Self {
            rx_queue:      VecDeque::new(),
            tx_log:        Vec::new(),
            pending_error: None,
            write_calls:   0,
            read_calls:    0,
            flush_calls:   0,
        }
    }

    /// Pre-load bytes for the mock to deliver via `read_byte()`.
    pub fn feed_rx(&mut self, data: &[u8]) {
        self.rx_queue.extend(data.iter().copied());
    }

    /// Schedule an error to be returned on the very next operation.
    pub fn inject_error(&mut self, error: UartError) {
        self.pending_error = Some(error);
    }

    /// Inspect all bytes that the driver has transmitted.
    pub fn tx_bytes(&self) -> &[u8] {
        &self.tx_log
    }

    /// Reset all state between tests.
    pub fn reset(&mut self) {
        *self = Self::new();
    }

    /// Consume and return a pending error (if any).
    fn take_error(&mut self) -> Option<UartError> {
        self.pending_error.take()
    }
}

impl Uart for MockUart {
    fn write_byte(&mut self, byte: u8) -> Result<(), UartError> {
        self.write_calls += 1;
        if let Some(e) = self.take_error() {
            return Err(e);
        }
        self.tx_log.push(byte);
        Ok(())
    }

    fn read_byte(&mut self) -> Result<u8, UartError> {
        self.read_calls += 1;
        if let Some(e) = self.take_error() {
            return Err(e);
        }
        self.rx_queue
            .pop_front()
            .ok_or(UartError::Timeout)   /* empty queue = timeout */
    }

    fn tx_ready(&self) -> bool {
        true   /* mock is always ready */
    }

    fn rx_available(&self) -> bool {
        !self.rx_queue.is_empty()
    }

    fn flush(&mut self) -> Result<(), UartError> {
        self.flush_calls += 1;
        Ok(())
    }
}
```

---

### 5.3 Testing with the Mock

```rust
// tests/uart_driver_tests.rs
#[cfg(test)]
mod tests {
    use crate::mock_uart::MockUart;
    use crate::uart_hal::{Uart, UartError};

    fn make_mock() -> MockUart {
        MockUart::new()
    }

    // ── write_all sends exact bytes in order ─────────────────────────────

    #[test]
    fn write_all_sends_correct_bytes() {
        let mut uart = make_mock();
        let payload = [0xDE, 0xAD, 0xBE, 0xEF];

        uart.write_all(&payload).expect("write_all must succeed");

        assert_eq!(uart.tx_bytes(), &payload,
                   "transmitted bytes must match payload exactly");
        assert_eq!(uart.flush_calls, 1,
                   "flush must be called exactly once after writing");
    }

    // ── read_exact retrieves fed bytes ───────────────────────────────────

    #[test]
    fn read_exact_returns_fed_bytes() {
        let mut uart = make_mock();
        let incoming: &[u8] = &[0xCA, 0xFE, 0xBA, 0xBE];
        uart.feed_rx(incoming);

        let mut buf = [0u8; 4];
        uart.read_exact(&mut buf).expect("read_exact must succeed");

        assert_eq!(&buf, incoming);
    }

    // ── timeout when RX buffer is empty ──────────────────────────────────

    #[test]
    fn read_byte_returns_timeout_on_empty_buffer() {
        let mut uart = make_mock();
        assert_eq!(uart.read_byte(), Err(UartError::Timeout));
    }

    // ── injected framing error propagates through write_all ───────────────

    #[test]
    fn write_all_propagates_injected_error() {
        let mut uart = make_mock();
        uart.inject_error(UartError::Framing);

        let result = uart.write_all(&[0xFF, 0x00]);

        assert_eq!(result, Err(UartError::Framing));
        assert!(uart.tx_bytes().is_empty(),
                "no bytes should be logged when first write fails");
    }

    // ── overrun error mid-read ────────────────────────────────────────────

    #[test]
    fn read_exact_aborts_on_overrun_mid_stream() {
        let mut uart = make_mock();
        uart.feed_rx(&[0x11, 0x22]);

        let mut buf = [0u8; 4];

        // Read first two bytes manually
        assert_eq!(uart.read_byte(), Ok(0x11));
        assert_eq!(uart.read_byte(), Ok(0x22));

        // Inject overrun before byte 3
        uart.inject_error(UartError::Overrun);
        assert_eq!(uart.read_byte(), Err(UartError::Overrun));
    }

    // ── rx_available reflects fed data ───────────────────────────────────

    #[test]
    fn rx_available_reflects_queue_state() {
        let mut uart = make_mock();
        assert!(!uart.rx_available(), "initially nothing available");

        uart.feed_rx(&[0xAB]);
        assert!(uart.rx_available(), "should be available after feed");

        uart.read_byte().unwrap();
        assert!(!uart.rx_available(), "should be empty after consuming");
    }

    // ── flush is called after every write_all ─────────────────────────────

    #[test]
    fn flush_called_once_per_write_all() {
        let mut uart = make_mock();
        uart.write_all(b"Hello").unwrap();
        assert_eq!(uart.flush_calls, 1);

        uart.write_all(b"World").unwrap();
        assert_eq!(uart.flush_calls, 2);
    }
}
```

---

### 5.4 Simulating Errors & Edge Cases

```rust
// src/error_scenarios.rs — comprehensive error injection helpers
use crate::mock_uart::MockUart;
use crate::uart_hal::UartError;
use std::collections::VecDeque;

/// A mock that returns a specific error after N successful operations.
pub struct FaultyUart {
    inner:       MockUart,
    fail_after:  usize,       /* succeed for this many ops, then fail */
    op_count:    usize,
    fault:       UartError,
}

impl FaultyUart {
    pub fn new(fail_after: usize, fault: UartError) -> Self {
        Self {
            inner: MockUart::new(),
            fail_after,
            op_count: 0,
            fault,
        }
    }

    fn check_fault(&mut self) -> Result<(), UartError> {
        self.op_count += 1;
        if self.op_count > self.fail_after {
            Err(self.fault)
        } else {
            Ok(())
        }
    }
}

use crate::uart_hal::Uart;

impl Uart for FaultyUart {
    fn write_byte(&mut self, byte: u8) -> Result<(), UartError> {
        self.check_fault()?;
        self.inner.write_byte(byte)
    }

    fn read_byte(&mut self) -> Result<u8, UartError> {
        self.check_fault()?;
        self.inner.read_byte()
    }

    fn tx_ready(&self) -> bool { self.inner.tx_ready() }
    fn rx_available(&self) -> bool { self.inner.rx_available() }

    fn flush(&mut self) -> Result<(), UartError> {
        self.inner.flush()
    }
}

#[cfg(test)]
mod faulty_tests {
    use super::*;
    use crate::uart_hal::{Uart, UartError};

    #[test]
    fn write_fails_at_third_byte() {
        // First 2 write_byte() succeed, 3rd returns Overrun
        let mut uart = FaultyUart::new(2, UartError::Overrun);
        assert!(uart.write_byte(0x01).is_ok());
        assert!(uart.write_byte(0x02).is_ok());
        assert_eq!(uart.write_byte(0x03), Err(UartError::Overrun));
    }

    #[test]
    fn write_all_aborts_on_mid_buffer_error() {
        let mut uart = FaultyUart::new(3, UartError::Framing);
        let result = uart.write_all(&[0xAA, 0xBB, 0xCC, 0xDD, 0xEE]);
        assert_eq!(result, Err(UartError::Framing));
    }
}
```

---

### 5.5 Using `mockall` for Advanced Mocking

The [`mockall`](https://crates.io/crates/mockall) crate auto-generates mock
implementations from trait definitions, with built-in expectation checking.

```toml
# Cargo.toml
[dev-dependencies]
mockall = "0.13"
```

```rust
// src/uart_hal.rs — annotate the trait for mockall
use mockall::automock;

#[automock]   // generates MockUart automatically
pub trait Uart {
    fn write_byte(&mut self, byte: u8)  -> Result<(), UartError>;
    fn read_byte(&mut self)             -> Result<u8,  UartError>;
    fn tx_ready(&self)                  -> bool;
    fn rx_available(&self)              -> bool;
    fn flush(&mut self)                 -> Result<(), UartError>;
}
```

```rust
// tests/mockall_tests.rs
#[cfg(test)]
mod mockall_tests {
    use mockall::predicate::*;
    use crate::uart_hal::{MockUart, UartError};

    #[test]
    fn expects_write_with_specific_bytes() {
        let mut uart = MockUart::new();

        // Expect exactly these bytes in this order
        uart.expect_write_byte()
            .with(eq(0xDE)).times(1).returning(|_| Ok(()));
        uart.expect_write_byte()
            .with(eq(0xAD)).times(1).returning(|_| Ok(()));
        uart.expect_flush()
            .times(1).returning(|| Ok(()));

        // The driver under test
        uart.write_byte(0xDE).unwrap();
        uart.write_byte(0xAD).unwrap();
        uart.flush().unwrap();
        // mockall verifies all expectations on drop
    }

    #[test]
    fn read_returns_stubbed_sequence() {
        let mut uart = MockUart::new();
        let bytes = [0x01u8, 0x02, 0x03];
        let mut iter = bytes.into_iter();

        uart.expect_read_byte()
            .times(3)
            .returning(move || Ok(iter.next().unwrap()));

        for expected in bytes {
            assert_eq!(uart.read_byte().unwrap(), expected);
        }
    }
}
```

---

## 6. Advanced Techniques

### 6.1 Loopback & Echo Simulation

An echo mock immediately pushes every transmitted byte back into its own RX queue,
simulating a loopback cable or a device that echoes commands.

```rust
// src/echo_uart.rs
use std::collections::VecDeque;
use crate::uart_hal::{Uart, UartError};

pub struct EchoUart {
    rx_queue: VecDeque<u8>,
}

impl EchoUart {
    pub fn new() -> Self { Self { rx_queue: VecDeque::new() } }
}

impl Uart for EchoUart {
    fn write_byte(&mut self, byte: u8) -> Result<(), UartError> {
        self.rx_queue.push_back(byte);   /* echo immediately */
        Ok(())
    }

    fn read_byte(&mut self) -> Result<u8, UartError> {
        self.rx_queue.pop_front().ok_or(UartError::Timeout)
    }

    fn tx_ready(&self)     -> bool { true }
    fn rx_available(&self) -> bool { !self.rx_queue.is_empty() }
    fn flush(&mut self)    -> Result<(), UartError> { Ok(()) }
}

#[cfg(test)]
mod echo_tests {
    use super::*;
    use crate::uart_hal::Uart;

    #[test]
    fn loopback_echo_round_trip() {
        let mut uart = EchoUart::new();
        let message = b"PING";

        uart.write_all(message).unwrap();

        let mut response = [0u8; 4];
        uart.read_exact(&mut response).unwrap();
        assert_eq!(&response, message);
    }
}
```

---

### 6.2 Simulating Framing & Parity Errors

```c
/* C: inject error patterns matching real hardware behaviour */

/* Simulate a UART that corrupts every Nth byte with a framing error */
typedef struct {
    uart_mock_priv_t base;
    uint32_t         every_n;      /* inject error every N bytes */
    uint32_t         byte_count;
} periodic_error_priv_t;

static uart_status_t periodic_write_byte(uart_handle_t *h, uint8_t byte,
                                          uint32_t timeout_ms)
{
    periodic_error_priv_t *p = (periodic_error_priv_t *)h->priv;
    p->byte_count++;
    if (p->byte_count % p->every_n == 0)
        return UART_ERR_FRAMING;
    p->base.tx_data[p->base.tx_len++] = byte;
    return UART_OK;
}
```

---

### 6.3 Timing & Interrupt Simulation

Real UART drivers often use interrupts or DMA. In tests, interrupts can be simulated by
calling the ISR handler directly after each mock operation.

```c
/* C: simulate ISR-driven RX */
typedef void (*uart_rx_callback_t)(uint8_t byte, void *ctx);

typedef struct {
    uart_mock_priv_t base;
    uart_rx_callback_t on_rx;
    void              *rx_ctx;
} irq_mock_priv_t;

/* Simulate hardware calling the RX ISR for each fed byte */
void uart_irq_mock_trigger_rx(uart_handle_t *h)
{
    irq_mock_priv_t *p = (irq_mock_priv_t *)h->priv;
    while (p->base.rx_head != p->base.rx_tail && p->on_rx) {
        uint8_t byte = p->base.rx_data[p->base.rx_head++];
        p->on_rx(byte, p->rx_ctx);   /* fire the ISR callback */
    }
}
```

```rust
// Rust: simulate async UART with a channel
use std::sync::mpsc::{self, Sender, Receiver};
use crate::uart_hal::{Uart, UartError};

pub struct ChannelUart {
    tx: Sender<u8>,
    rx: Receiver<u8>,
}

impl ChannelUart {
    /// Creates a linked pair (driver side, test side).
    pub fn pair() -> (Self, Self) {
        let (tx_a, rx_b) = mpsc::channel();
        let (tx_b, rx_a) = mpsc::channel();
        (Self { tx: tx_a, rx: rx_a }, Self { tx: tx_b, rx: rx_b })
    }
}

impl Uart for ChannelUart {
    fn write_byte(&mut self, byte: u8) -> Result<(), UartError> {
        self.tx.send(byte).map_err(|_| UartError::BufferFull)
    }

    fn read_byte(&mut self) -> Result<u8, UartError> {
        self.rx.recv_timeout(std::time::Duration::from_millis(100))
            .map_err(|_| UartError::Timeout)
    }

    fn tx_ready(&self) -> bool { true }
    fn rx_available(&self) -> bool {
        // peek without blocking — try_recv and put back
        false  // simplified; use a buffered wrapper in production
    }
    fn flush(&mut self) -> Result<(), UartError> { Ok(()) }
}

#[cfg(test)]
mod channel_tests {
    use super::*;
    use crate::uart_hal::Uart;
    use std::thread;

    #[test]
    fn channel_uart_full_duplex_exchange() {
        let (mut driver_side, mut test_side) = ChannelUart::pair();

        // Spawn a "device" thread that reads a byte and echoes it doubled
        let handle = thread::spawn(move || {
            let b = test_side.read_byte().expect("device: read failed");
            test_side.write_byte(b.wrapping_mul(2))
                     .expect("device: write failed");
        });

        driver_side.write_byte(0x05).expect("driver: write failed");
        let response = driver_side.read_byte().expect("driver: read failed");

        handle.join().unwrap();
        assert_eq!(response, 0x0A);   /* 5 × 2 = 10 = 0x0A */
    }
}
```

---

## 7. Test Coverage Strategies

A robust mock test suite for a UART driver should cover the following categories:

### 7.1 Happy-path tests
- Write a single byte — confirm it appears in TX log.
- Write a buffer — confirm byte order, count, and that flush is called once.
- Read a single byte — confirm it matches what was fed.
- Read a multi-byte buffer — confirm all bytes match in order.

### 7.2 Boundary conditions
- Write zero-length buffer — should succeed with a single flush call.
- Read zero-length buffer — should succeed immediately.
- Fill TX buffer to capacity — confirm behaviour at max size.
- Empty RX buffer read — must return `Timeout` / `UART_ERR_TIMEOUT`.

### 7.3 Error injection tests
- `Framing` error on first byte of a write — no bytes logged, error returned.
- `Parity` error mid-buffer — partial write, error returned at fault position.
- `Overrun` error mid-read — correct bytes returned up to fault, error at fault.
- Consecutive errors — confirm one-shot semantics (error fires once, then clears).

### 7.4 Protocol-level tests
Using echo or loopback mocks:
- AT command `send → receive response` round-trip.
- Modbus RTU frame encoding/decoding.
- SLIP/HDLC packet framing verification.

### 7.5 Call-count / interaction tests
- `flush()` called exactly once per `write_all()`.
- `read_byte()` called exactly N times for an N-byte read.
- No unexpected calls when a zero-length write is issued.

### 7.6 Suggested CI pipeline structure

```
┌──────────────────────────────────────────┐
│   Host PC (CI runner)                    │
│                                          │
│   cmake --build . --target uart_tests    │
│   ./uart_tests             (Unity/GTest) │
│   cargo test               (Rust)        │
│   gcov / llvm-cov -- coverage report     │
└──────────────────────────────────────────┘
         ↓ all pass ↓
┌──────────────────────────────────────────┐
│   Cross-compile + hardware-in-the-loop   │
│   (optional gate before release)         │
└──────────────────────────────────────────┘
```

---

## 8. Summary

| Aspect | C/C++ approach | Rust approach |
|---|---|---|
| **Abstraction mechanism** | Function-pointer vtable (`uart_ops_t`) or virtual class | `trait Uart` |
| **Mock type** | Struct with `uart_ops_t` pointing to mock functions | Struct implementing `Uart` |
| **Error injection** | `pending_error` field, consumed once | `Option<UartError>` field via `.take()` |
| **Call verification** | Manual counters (`write_calls`, `flush_calls`) | Counters or `mockall` expectations |
| **Advanced mocking** | GMock `MOCK_METHOD` + `EXPECT_CALL` | `mockall::automock` + `.expect_*()` |
| **Loopback simulation** | Two linked mock structs | `EchoUart` or `mpsc::channel` pair |
| **Concurrency testing** | POSIX threads + shared mock | `std::sync::mpsc` channel UART |
| **Test framework** | Unity (C), GoogleTest (C++) | Rust's built-in `#[test]` |
| **Coverage tooling** | `gcov`, `lcov`, `llvm-cov` | `cargo llvm-cov`, `cargo tarpaulin` |

**Key takeaways:**

1. **Design for testability from day one.** The hardware abstraction layer is not optional
   overhead — it is what makes the driver verifiable without silicon.

2. **Mocks are zero-cost in production.** The mock object and test code are compiled only
   when `#[cfg(test)]` / `#ifdef UNIT_TEST` is active; the release binary contains only
   the real HAL implementation.

3. **Error injection is the most valuable test category.** Happy-path tests confirm
   correctness; error-injection tests confirm robustness. Real hardware makes errors
   hard to reproduce reliably; mocks make them trivial.

4. **Graduate from unit mocks to HIL tests.** Mock tests run fast and catch logic bugs.
   Hardware-in-the-loop (HIL) tests then validate timing, interrupt latency, and DMA
   behaviour that no software simulation can replicate.

5. **Keep the mock simple.** A mock that is too clever becomes a maintenance burden.
   Prefer a ring buffer + error flag over a full protocol state machine in the mock
   itself.

---

*Document: 35_Mock_Hardware_Testing.md — part of the Embedded UART Programming Series*