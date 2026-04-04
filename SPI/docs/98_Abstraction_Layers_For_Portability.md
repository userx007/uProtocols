# 98. Abstraction Layers for Portability

**Topic 98 — Abstraction Layers for Portability** is a comprehensive, ~700-line technical reference structured as follows:

- **Why it matters**: Shows the concrete problem — the same SPI operation looks entirely different on STM32 vs. Linux, and an abstraction layer reconciles them behind one API.
- **C HAL Design**: A complete `spi_hal.h` interface with opaque handles, error codes, config structs, and full-duplex/write/read helpers — followed by both a full Linux `spidev` backend and an STM32 CubeMX backend, plus a real device driver (`MAX31855` thermocouple) that is 100% platform-agnostic.
- **C++ Approach**: A pure-virtual `ISpi` interface, a `LinuxSpi` concrete backend, and a `BME280` sensor driver that depends only on the interface. Zero coupling to any platform.
- **Rust Trait Design**: Custom `SpiBus`/`SpiDevice` traits, a Linux spidev backend, an Embassy/STM32 async backend, and a fully generic `MAX31865` RTD driver — then the same driver rewritten against `embedded-hal 1.0` for maximum ecosystem interoperability.
- **Build Systems**: CMake platform selection (C) and Cargo features + `#[cfg]` (Rust).
- **Testing**: Mock backends in both C and Rust that capture writes and inject fake reads — enabling full driver unit tests without hardware.
- **Advanced Patterns**: Async/non-blocking SPI (Embassy tasks), DMA-aware HAL extensions, and a simulation backend that logs all transactions.
- **Comparison Table**: C vs. C++ vs. Rust across dispatch cost, type safety, ecosystem, and async support.

## Creating Hardware-Independent SPI Interfaces for Cross-Platform Code

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Abstraction Layers Matter](#why-abstraction-layers-matter)
3. [Core Design Principles](#core-design-principles)
4. [Architecture Overview](#architecture-overview)
5. [C/C++ Implementation](#cc-implementation)
   - [HAL Interface Definition](#hal-interface-definition)
   - [Platform-Specific Backends](#platform-specific-backends)
   - [Device Driver Using the HAL](#device-driver-using-the-hal)
   - [C++ Object-Oriented Approach](#c-object-oriented-approach)
6. [Rust Implementation](#rust-implementation)
   - [Trait-Based Abstraction](#trait-based-abstraction)
   - [Platform Backends in Rust](#platform-backends-in-rust)
   - [Device Driver Using Traits](#device-driver-using-traits)
   - [Using embedded-hal](#using-embedded-hal)
7. [Cross-Platform Configuration and Build Systems](#cross-platform-configuration-and-build-systems)
8. [Testing Strategies](#testing-strategies)
9. [Advanced Patterns](#advanced-patterns)
   - [Async / Non-Blocking SPI](#async--non-blocking-spi)
   - [DMA-Aware Abstraction](#dma-aware-abstraction)
   - [Mocking and Simulation](#mocking-and-simulation)
10. [Comparison: C vs C++ vs Rust](#comparison-c-vs-c-vs-rust)
11. [Summary](#summary)

---

## Introduction

The Serial Peripheral Interface (SPI) bus is a synchronous, full-duplex serial communication protocol widely used in embedded systems for devices such as displays, sensors, flash memory, ADCs, and DACs. While the SPI protocol itself is standardized, its implementation differs significantly across microcontroller families, operating systems, and hardware abstraction libraries.

An **abstraction layer** for SPI provides a uniform, hardware-independent interface that separates the *what* (transfer data via SPI) from the *how* (use specific peripheral registers, Linux spidev ioctls, or a third-party SDK). This decoupling makes device driver code portable, testable, and maintainable across platforms.

---

## Why Abstraction Layers Matter

Without abstraction, a driver for an SPI-connected sensor might look like this on STM32:

```c
// Tightly coupled to STM32 HAL — NOT portable
HAL_SPI_Transmit(&hspi1, buf, len, HAL_MAX_DELAY);
```

And on a Raspberry Pi running Linux:

```c
// Tightly coupled to Linux spidev — NOT portable
ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
```

These two snippets are functionally equivalent but completely incompatible. An abstraction layer unifies them behind a single function signature, so the same device driver compiles and runs on both platforms without modification.

**Key benefits:**

- **Portability**: The same device driver code targets multiple MCUs, Linux SBCs, and simulation environments.
- **Testability**: Replace the real SPI backend with a mock or loopback for unit testing without hardware.
- **Maintainability**: Platform-specific code is isolated in one place; bugs are easier to find and fix.
- **Team scalability**: Hardware engineers maintain backends; firmware engineers maintain device drivers independently.

---

## Core Design Principles

1. **Dependency Inversion**: High-level modules (device drivers) depend on abstractions, not on concrete implementations.
2. **Minimal Interface**: Expose only what is universally available — transfer, chip-select control, and configuration. Do not leak platform-specific details.
3. **Zero-cost where possible**: In C++/Rust, abstractions can be resolved entirely at compile time (static dispatch) with no runtime overhead.
4. **Error propagation**: Abstract error types must be mapped from platform-specific errors without data loss.
5. **Lifetime and ownership clarity**: Especially in Rust, the abstraction must encode who owns the bus and for how long.

---

## Architecture Overview

The layered architecture looks like this:

```
┌─────────────────────────────────────────┐
│            Application / RTOS           │
├─────────────────────────────────────────┤
│         Device Driver Layer             │
│   (e.g., OLED, Flash, IMU driver)       │
│   Uses only the abstract SPI interface  │
├─────────────────────────────────────────┤
│        SPI Abstraction Layer (HAL)      │
│   spi_transfer(), spi_init(), etc.      │
├──────────────┬──────────────┬───────────┤
│  STM32 HAL   │  Linux spidev│  Mock/Test│
│   Backend    │   Backend    │  Backend  │
├──────────────┴──────────────┴───────────┤
│           Physical Hardware             │
└─────────────────────────────────────────┘
```

---

## C/C++ Implementation

### HAL Interface Definition

The foundation is a pure C header that defines the interface contract. All platforms must implement every function in this contract.

```c
/* spi_hal.h — Hardware-independent SPI interface */
#ifndef SPI_HAL_H
#define SPI_HAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle — the concrete type is defined by each backend */
typedef struct spi_bus_t* spi_handle_t;

/* Return codes */
typedef enum {
    SPI_OK            =  0,
    SPI_ERR_INIT      = -1,
    SPI_ERR_TIMEOUT   = -2,
    SPI_ERR_BUS_FAULT = -3,
    SPI_ERR_INVALID   = -4,
} spi_status_t;

/* Configuration */
typedef enum {
    SPI_MODE_0 = 0,   /* CPOL=0, CPHA=0 */
    SPI_MODE_1 = 1,   /* CPOL=0, CPHA=1 */
    SPI_MODE_2 = 2,   /* CPOL=1, CPHA=0 */
    SPI_MODE_3 = 3,   /* CPOL=1, CPHA=1 */
} spi_mode_t;

typedef enum {
    SPI_BIT_ORDER_MSB_FIRST = 0,
    SPI_BIT_ORDER_LSB_FIRST = 1,
} spi_bit_order_t;

typedef struct {
    uint32_t        clock_hz;       /* Requested SPI clock frequency in Hz      */
    spi_mode_t      mode;           /* SPI mode (clock polarity and phase)       */
    spi_bit_order_t bit_order;      /* MSB or LSB first                          */
    uint8_t         bits_per_word;  /* Typically 8; some platforms support 16    */
} spi_config_t;

/* ---- Lifecycle ------------------------------------------------------------ */

/**
 * @brief  Initialise the SPI bus identified by `bus_id`.
 *
 * @param  bus_id   Platform-defined bus identifier (e.g., 0 for SPI0).
 * @param  config   Desired configuration. Must not be NULL.
 * @param  out      On success, receives the opaque handle. Must not be NULL.
 * @return SPI_OK on success, negative error code on failure.
 */
spi_status_t spi_init(uint8_t bus_id, const spi_config_t* config,
                       spi_handle_t* out);

/**
 * @brief  Release all resources associated with the handle.
 *         The handle is invalid after this call.
 */
void spi_deinit(spi_handle_t handle);

/* ---- Chip Select ---------------------------------------------------------- */

/**
 * @brief  Assert (active-low) chip-select line `cs_id`.
 */
spi_status_t spi_cs_assert(spi_handle_t handle, uint8_t cs_id);

/**
 * @brief  Deassert chip-select line `cs_id`.
 */
spi_status_t spi_cs_deassert(spi_handle_t handle, uint8_t cs_id);

/* ---- Data Transfer -------------------------------------------------------- */

/**
 * @brief  Full-duplex transfer: simultaneously write `tx_buf` and read into
 *         `rx_buf` for `len` bytes.
 *
 *         Either `tx_buf` or `rx_buf` may be NULL:
 *           - NULL tx_buf  → send 0xFF dummy bytes (read-only transfer).
 *           - NULL rx_buf  → discard received data  (write-only transfer).
 */
spi_status_t spi_transfer(spi_handle_t handle,
                           const uint8_t* tx_buf,
                           uint8_t*       rx_buf,
                           size_t         len);

/**
 * @brief  Convenience: write only.
 */
static inline spi_status_t spi_write(spi_handle_t handle,
                                      const uint8_t* buf, size_t len) {
    return spi_transfer(handle, buf, NULL, len);
}

/**
 * @brief  Convenience: read only (sends dummy bytes).
 */
static inline spi_status_t spi_read(spi_handle_t handle,
                                     uint8_t* buf, size_t len) {
    return spi_transfer(handle, NULL, buf, len);
}

#ifdef __cplusplus
}
#endif

#endif /* SPI_HAL_H */
```

### Platform-Specific Backends

#### Linux spidev Backend

```c
/* spi_hal_linux.c — Linux spidev backend */
#include "spi_hal.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdlib.h>

/* Concrete type hidden from users of the HAL */
struct spi_bus_t {
    int      fd;
    uint32_t speed_hz;
    uint8_t  bits_per_word;
};

spi_status_t spi_init(uint8_t bus_id, const spi_config_t* config,
                       spi_handle_t* out)
{
    if (!config || !out) return SPI_ERR_INVALID;

    char path[32];
    snprintf(path, sizeof(path), "/dev/spidev0.%u", bus_id);

    int fd = open(path, O_RDWR);
    if (fd < 0) return SPI_ERR_INIT;

    uint8_t mode = (uint8_t)config->mode;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0)         { close(fd); return SPI_ERR_INIT; }
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &config->clock_hz) < 0)
                                                         { close(fd); return SPI_ERR_INIT; }
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &config->bits_per_word) < 0)
                                                         { close(fd); return SPI_ERR_INIT; }

    struct spi_bus_t* bus = malloc(sizeof(*bus));
    if (!bus) { close(fd); return SPI_ERR_INIT; }

    bus->fd            = fd;
    bus->speed_hz      = config->clock_hz;
    bus->bits_per_word = config->bits_per_word;
    *out = bus;
    return SPI_OK;
}

void spi_deinit(spi_handle_t handle) {
    if (!handle) return;
    close(handle->fd);
    free(handle);
}

/* On Linux, CS is managed by the kernel driver; these become no-ops or
   can be implemented via GPIO if software CS is required. */
spi_status_t spi_cs_assert(spi_handle_t handle, uint8_t cs_id) {
    (void)handle; (void)cs_id;
    return SPI_OK;
}

spi_status_t spi_cs_deassert(spi_handle_t handle, uint8_t cs_id) {
    (void)handle; (void)cs_id;
    return SPI_OK;
}

spi_status_t spi_transfer(spi_handle_t handle,
                           const uint8_t* tx_buf,
                           uint8_t*       rx_buf,
                           size_t         len)
{
    if (!handle || len == 0) return SPI_ERR_INVALID;

    /* spidev requires a non-NULL tx buffer; use a dummy if needed */
    uint8_t* dummy_tx = NULL;
    if (!tx_buf) {
        dummy_tx = calloc(len, 1);
        if (!dummy_tx) return SPI_ERR_INIT;
        memset(dummy_tx, 0xFF, len);
        tx_buf = dummy_tx;
    }

    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx_buf,
        .rx_buf        = (unsigned long)rx_buf,
        .len           = (uint32_t)len,
        .speed_hz      = handle->speed_hz,
        .bits_per_word = handle->bits_per_word,
        .delay_usecs   = 0,
    };

    int ret = ioctl(handle->fd, SPI_IOC_MESSAGE(1), &tr);
    free(dummy_tx);
    return (ret >= 0) ? SPI_OK : SPI_ERR_BUS_FAULT;
}
```

#### STM32 HAL Backend

```c
/* spi_hal_stm32.c — STM32 HAL backend (uses STM32CubeMX-generated hspi) */
#include "spi_hal.h"
#include "stm32f4xx_hal.h"  /* Replace with your STM32 family header */
#include <stdlib.h>
#include <string.h>

/* Forward-declared in main.c by CubeMX */
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;

struct spi_bus_t {
    SPI_HandleTypeDef* hspi;
};

static SPI_HandleTypeDef* resolve_bus(uint8_t bus_id) {
    switch (bus_id) {
        case 1:  return &hspi1;
        case 2:  return &hspi2;
        default: return NULL;
    }
}

spi_status_t spi_init(uint8_t bus_id, const spi_config_t* config,
                       spi_handle_t* out)
{
    if (!config || !out) return SPI_ERR_INVALID;

    SPI_HandleTypeDef* hspi = resolve_bus(bus_id);
    if (!hspi) return SPI_ERR_INVALID;

    /* CubeMX already initialised the peripheral; we just validate the mode. */
    /* A full implementation would reconfigure the peripheral here.          */
    (void)config;  /* Omitted for brevity in this example                    */

    struct spi_bus_t* bus = malloc(sizeof(*bus));
    if (!bus) return SPI_ERR_INIT;

    bus->hspi = hspi;
    *out = bus;
    return SPI_OK;
}

void spi_deinit(spi_handle_t handle) {
    free(handle);
}

spi_status_t spi_cs_assert(spi_handle_t handle, uint8_t cs_id) {
    (void)handle;
    /* Map cs_id to a GPIO pin — platform-specific GPIO HAL call */
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, (1u << cs_id), GPIO_PIN_RESET);
    return SPI_OK;
}

spi_status_t spi_cs_deassert(spi_handle_t handle, uint8_t cs_id) {
    (void)handle;
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, (1u << cs_id), GPIO_PIN_SET);
    return SPI_OK;
}

spi_status_t spi_transfer(spi_handle_t handle,
                           const uint8_t* tx_buf,
                           uint8_t*       rx_buf,
                           size_t         len)
{
    if (!handle || len == 0) return SPI_ERR_INVALID;

    HAL_StatusTypeDef result;

    if (tx_buf && rx_buf) {
        result = HAL_SPI_TransmitReceive(handle->hspi,
                                          (uint8_t*)tx_buf, rx_buf,
                                          (uint16_t)len, HAL_MAX_DELAY);
    } else if (tx_buf) {
        result = HAL_SPI_Transmit(handle->hspi,
                                   (uint8_t*)tx_buf,
                                   (uint16_t)len, HAL_MAX_DELAY);
    } else if (rx_buf) {
        /* Send dummy 0xFF bytes while reading */
        uint8_t* dummy = malloc(len);
        if (!dummy) return SPI_ERR_INIT;
        memset(dummy, 0xFF, len);
        result = HAL_SPI_TransmitReceive(handle->hspi, dummy, rx_buf,
                                          (uint16_t)len, HAL_MAX_DELAY);
        free(dummy);
    } else {
        return SPI_ERR_INVALID;
    }

    return (result == HAL_OK) ? SPI_OK : SPI_ERR_BUS_FAULT;
}
```

### Device Driver Using the HAL

A device driver written against the HAL is completely platform-agnostic:

```c
/* max31855_thermocouple.c — SPI thermocouple driver using the HAL */
#include "spi_hal.h"
#include "max31855.h"
#include <stdint.h>

/* The MAX31855 returns 32-bit temperature data on every SPI read.
   No writes are ever performed; CS must be driven by the host.   */

max31855_status_t max31855_read(spi_handle_t spi, uint8_t cs_id,
                                 float* out_temp_c)
{
    uint8_t raw[4];

    spi_status_t s = spi_cs_assert(spi, cs_id);
    if (s != SPI_OK) return MAX31855_ERR_SPI;

    s = spi_read(spi, raw, sizeof(raw));
    spi_cs_deassert(spi, cs_id);  /* always deassert, even on error */

    if (s != SPI_OK) return MAX31855_ERR_SPI;

    uint32_t word = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16)
                  | ((uint32_t)raw[2] <<  8) |  (uint32_t)raw[3];

    /* Bit 16: FAULT — open/short condition */
    if (word & (1u << 16)) return MAX31855_ERR_FAULT;

    /* Bits 31–18: 14-bit thermocouple temperature, 0.25°C resolution */
    int16_t raw_temp = (int16_t)(word >> 18);
    if (raw_temp & (1 << 13)) raw_temp |= (int16_t)0xC000; /* sign extend */

    *out_temp_c = raw_temp * 0.25f;
    return MAX31855_OK;
}
```

### C++ Object-Oriented Approach

C++ enables a cleaner abstraction using pure virtual base classes (interfaces):

```cpp
// SpiInterface.hpp — C++ abstract SPI interface
#pragma once
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <span>

enum class SpiError { Ok, BusFault, Timeout, InvalidArg, InitFailed };

class ISpi {
public:
    virtual ~ISpi() = default;

    virtual SpiError transfer(std::span<const uint8_t> tx,
                               std::span<uint8_t>       rx) = 0;

    virtual SpiError assertCs(uint8_t id)   = 0;
    virtual SpiError deassertCs(uint8_t id) = 0;

    /* Convenience helpers implemented once in the base */
    SpiError write(std::span<const uint8_t> buf) {
        return transfer(buf, {});
    }
    SpiError read(std::span<uint8_t> buf) {
        return transfer({}, buf);
    }
};

// LinuxSpi.hpp — concrete Linux backend
#include "SpiInterface.hpp"
#include <string>

class LinuxSpi final : public ISpi {
public:
    explicit LinuxSpi(const std::string& device, uint32_t speed_hz,
                      uint8_t mode = 0);
    ~LinuxSpi() override;

    SpiError transfer(std::span<const uint8_t> tx,
                       std::span<uint8_t>       rx) override;
    SpiError assertCs(uint8_t id)   override;
    SpiError deassertCs(uint8_t id) override;

private:
    int      fd_;
    uint32_t speed_hz_;
};

// Device driver — depends only on ISpi, not on LinuxSpi
#include "SpiInterface.hpp"
#include <array>

class Bme280Driver {
public:
    explicit Bme280Driver(ISpi& spi, uint8_t cs_id)
        : spi_(spi), cs_(cs_id) {}

    float readTemperature() {
        constexpr uint8_t TEMP_MSB_REG = 0xFA;
        std::array<uint8_t, 4> tx = { TEMP_MSB_REG | 0x80, 0, 0, 0 };
        std::array<uint8_t, 4> rx{};

        spi_.assertCs(cs_);
        spi_.transfer(tx, rx);
        spi_.deassertCs(cs_);

        int32_t raw = ((int32_t)rx[1] << 12) |
                      ((int32_t)rx[2] <<  4) |
                      ((int32_t)rx[3] >>  4);
        return compensateTemperature(raw);
    }

private:
    ISpi&   spi_;
    uint8_t cs_;

    float compensateTemperature(int32_t raw) {
        /* BME280 compensation formula (simplified) */
        return raw / 5120.0f;
    }
};

// main.cpp — wiring together platform + driver
int main() {
    LinuxSpi bus("/dev/spidev0.0", 1'000'000, 0);
    Bme280Driver sensor(bus, 0);

    float temp = sensor.readTemperature();
    printf("Temperature: %.2f °C\n", temp);
}
```

---

## Rust Implementation

### Trait-Based Abstraction

Rust's trait system is a natural fit for zero-cost hardware abstractions. Unlike C++ virtual dispatch, Rust allows both static (monomorphic) and dynamic (trait object) dispatch at the call site.

```rust
// spi_hal/src/lib.rs — SPI HAL trait definition

use core::fmt;

/// Error type for SPI operations.
/// Parameterised so each backend can carry its own error detail.
#[derive(Debug)]
pub enum SpiError<E> {
    Transfer(E),
    ChipSelect(E),
    InvalidArgument,
}

impl<E: fmt::Display> fmt::Display for SpiError<E> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SpiError::Transfer(e)   => write!(f, "SPI transfer error: {e}"),
            SpiError::ChipSelect(e) => write!(f, "SPI chip-select error: {e}"),
            SpiError::InvalidArgument => write!(f, "Invalid argument"),
        }
    }
}

/// Core SPI transfer trait.
/// Implementations provide full-duplex transfer over a concrete bus.
pub trait SpiBus {
    type Error;

    /// Full-duplex transfer. `tx` and `rx` must have the same length.
    fn transfer(&mut self, tx: &[u8], rx: &mut [u8]) -> Result<(), Self::Error>;

    /// Write only — discard incoming bytes.
    fn write(&mut self, tx: &[u8]) -> Result<(), Self::Error> {
        let mut discard = vec![0u8; tx.len()];
        self.transfer(tx, &mut discard)
    }

    /// Read only — send 0xFF dummy bytes.
    fn read(&mut self, rx: &mut [u8]) -> Result<(), Self::Error> {
        let dummy = vec![0xFFu8; rx.len()];
        self.transfer(&dummy, rx)
    }
}

/// Chip-select control, separate from data transfer.
pub trait SpiDevice {
    type Bus: SpiBus;
    type Error;

    /// Execute a transaction: assert CS, run the closure, deassert CS.
    fn transaction<F, R>(&mut self, f: F) -> Result<R, Self::Error>
    where
        F: FnOnce(&mut Self::Bus) -> Result<R, <Self::Bus as SpiBus>::Error>;
}
```

### Platform Backends in Rust

#### Linux spidev Backend

```rust
// spi_hal_linux/src/lib.rs

use spi_hal::{SpiBus, SpiDevice, SpiError};
use std::fs::{File, OpenOptions};
use std::os::unix::io::AsRawFd;

pub struct LinuxSpiBus {
    fd: File,
    speed_hz: u32,
}

#[derive(Debug)]
pub struct LinuxSpiError(std::io::Error);

impl std::fmt::Display for LinuxSpiError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "spidev error: {}", self.0)
    }
}

impl LinuxSpiBus {
    pub fn new(device: &str, speed_hz: u32, mode: u8) -> std::io::Result<Self> {
        let fd = OpenOptions::new().read(true).write(true).open(device)?;
        // Set mode and speed via ioctl (simplified — full impl would use nix crate)
        unsafe {
            let raw_fd = fd.as_raw_fd();
            libc::ioctl(raw_fd, SPI_IOC_WR_MODE as _, &mode);
            libc::ioctl(raw_fd, SPI_IOC_WR_MAX_SPEED_HZ as _, &speed_hz);
        }
        Ok(Self { fd, speed_hz })
    }
}

impl SpiBus for LinuxSpiBus {
    type Error = LinuxSpiError;

    fn transfer(&mut self, tx: &[u8], rx: &mut [u8]) -> Result<(), Self::Error> {
        assert_eq!(tx.len(), rx.len(), "tx and rx buffers must be equal length");

        // spi_ioc_transfer struct layout (simplified)
        #[repr(C)]
        struct SpiIocTransfer {
            tx_buf:        u64,
            rx_buf:        u64,
            len:           u32,
            speed_hz:      u32,
            delay_usecs:   u16,
            bits_per_word: u8,
            cs_change:     u8,
            pad:           u32,
        }

        let tr = SpiIocTransfer {
            tx_buf:        tx.as_ptr() as u64,
            rx_buf:        rx.as_mut_ptr() as u64,
            len:           tx.len() as u32,
            speed_hz:      self.speed_hz,
            delay_usecs:   0,
            bits_per_word: 8,
            cs_change:     0,
            pad:           0,
        };

        let ret = unsafe {
            libc::ioctl(self.fd.as_raw_fd(), SPI_IOC_MESSAGE_1 as _, &tr)
        };

        if ret < 0 {
            Err(LinuxSpiError(std::io::Error::last_os_error()))
        } else {
            Ok(())
        }
    }
}
```

#### RTIC / Embassy Async Backend (bare-metal)

```rust
// Async SPI backend for Embassy on STM32
use embassy_stm32::spi::{Config, Spi};
use embassy_stm32::peripherals::SPI1;
use spi_hal::SpiBus;

pub struct EmbassySpi<'d> {
    inner: Spi<'d, SPI1, embassy_stm32::mode::Async>,
}

impl<'d> EmbassySpi<'d> {
    pub fn new(spi: Spi<'d, SPI1, embassy_stm32::mode::Async>) -> Self {
        Self { inner: spi }
    }

    /// Async version — can be awaited inside an embassy task
    pub async fn transfer_async(
        &mut self,
        tx: &[u8],
        rx: &mut [u8],
    ) -> Result<(), embassy_stm32::spi::Error> {
        self.inner.transfer(rx, tx).await
    }
}

// Blocking impl satisfies the SpiBus trait for use in sync device drivers
impl<'d> SpiBus for EmbassySpi<'d> {
    type Error = embassy_stm32::spi::Error;

    fn transfer(&mut self, tx: &[u8], rx: &mut [u8]) -> Result<(), Self::Error> {
        // In an async context, use block_on or the async version
        embassy_futures::block_on(self.inner.transfer(rx, tx))
    }
}
```

### Device Driver Using Traits

A device driver written against `SpiBus` is fully portable — it compiles for Linux, STM32, RISC-V, and simulation targets without any changes:

```rust
// drivers/src/max31865.rs — RTD temperature sensor driver

use spi_hal::SpiBus;

pub struct Max31865<BUS> {
    bus: BUS,
}

#[derive(Debug)]
pub enum Max31865Error<E> {
    Spi(E),
    Fault(u8),
}

impl<BUS: SpiBus> Max31865<BUS> {
    /// Create a new driver, taking ownership of the SPI bus.
    pub fn new(mut bus: BUS) -> Result<Self, Max31865Error<BUS::Error>> {
        // Write configuration register: bias on, 1-shot mode, PT100
        let config_cmd = [0x80u8, 0xB2];
        bus.write(&config_cmd).map_err(Max31865Error::Spi)?;
        Ok(Self { bus })
    }

    /// Read the raw 15-bit RTD resistance ratio.
    pub fn read_raw(&mut self) -> Result<u16, Max31865Error<BUS::Error>> {
        let tx = [0x01u8, 0x00, 0x00]; // Read RTD MSB register
        let mut rx = [0u8; 3];
        self.bus.transfer(&tx, &mut rx).map_err(Max31865Error::Spi)?;

        // Check fault bit (LSB of low byte)
        if rx[2] & 0x01 != 0 {
            return Err(Max31865Error::Fault(rx[2]));
        }

        let raw = ((rx[1] as u16) << 7) | ((rx[2] as u16) >> 1);
        Ok(raw)
    }

    /// Convert raw value to temperature in °C (Callendar-Van Dusen, simplified).
    pub fn read_temperature(&mut self) -> Result<f32, Max31865Error<BUS::Error>> {
        let raw = self.read_raw()?;
        let resistance = (raw as f32 / 32768.0) * 400.0; // Rref = 400 Ω
        // Simplified linear approximation for PT100 (-50..+150°C range)
        Ok((resistance - 100.0) / 0.385)
    }
}
```

### Using embedded-hal

The [`embedded-hal`](https://crates.io/crates/embedded-hal) crate is the de-facto standard Rust HAL trait collection for embedded systems. Using it instead of a custom trait maximises driver reuse across all MCU families that provide `embedded-hal` implementations.

```rust
// Using embedded-hal 1.0 SpiDevice trait — maximum interoperability

use embedded_hal::spi::SpiDevice;

pub struct Lsm6dsox<SPI> {
    spi: SPI,
}

impl<SPI: SpiDevice> Lsm6dsox<SPI> {
    pub fn new(spi: SPI) -> Self {
        Self { spi }
    }

    /// Read the WHO_AM_I register (should return 0x6C for LSM6DSOX).
    pub fn who_am_i(&mut self) -> Result<u8, SPI::Error> {
        const WHO_AM_I_REG: u8 = 0x0F;

        // embedded-hal SpiDevice::transaction handles CS automatically
        let mut response = [0u8; 2];
        self.spi.transfer(&mut response, &[WHO_AM_I_REG | 0x80, 0x00])?;
        Ok(response[1])
    }

    pub fn read_accel_raw(
        &mut self,
    ) -> Result<(i16, i16, i16), SPI::Error> {
        const OUTX_L_A: u8 = 0x28;
        let mut buf = [0u8; 7]; // 1 addr byte + 6 data bytes
        let tx = [OUTX_L_A | 0x80, 0, 0, 0, 0, 0, 0];
        self.spi.transfer(&mut buf, &tx)?;

        let ax = i16::from_le_bytes([buf[1], buf[2]]);
        let ay = i16::from_le_bytes([buf[3], buf[4]]);
        let az = i16::from_le_bytes([buf[5], buf[6]]);
        Ok((ax, ay, az))
    }
}

// Works on ANY platform with an embedded-hal SpiDevice implementation:
// - rp2040-hal (Raspberry Pi Pico)
// - stm32f4xx-hal
// - nrf52840-hal
// - linux-embedded-hal (Raspberry Pi, etc.)
```

---

## Cross-Platform Configuration and Build Systems

### C — CMake with Platform Selection

```cmake
# CMakeLists.txt

cmake_minimum_required(VERSION 3.20)
project(SpiDemo C)

# Select backend via -DPLATFORM=linux|stm32|mock
set(PLATFORM "linux" CACHE STRING "Target platform")

add_library(spi_hal STATIC
    src/spi_hal_common.c
)
target_include_directories(spi_hal PUBLIC include/)

if(PLATFORM STREQUAL "linux")
    target_sources(spi_hal PRIVATE src/spi_hal_linux.c)
    target_compile_definitions(spi_hal PUBLIC SPI_BACKEND_LINUX)

elseif(PLATFORM STREQUAL "stm32")
    target_sources(spi_hal PRIVATE src/spi_hal_stm32.c)
    target_compile_definitions(spi_hal PUBLIC SPI_BACKEND_STM32)
    target_link_libraries(spi_hal PUBLIC stm32_hal)

elseif(PLATFORM STREQUAL "mock")
    target_sources(spi_hal PRIVATE src/spi_hal_mock.c)
    target_compile_definitions(spi_hal PUBLIC SPI_BACKEND_MOCK)
endif()
```

### Rust — Cargo Features and Conditional Compilation

```toml
# Cargo.toml

[package]
name    = "my_firmware"
version = "0.1.0"
edition = "2021"

[features]
default = []
linux   = ["dep:linux-embedded-hal"]
stm32   = ["dep:stm32f4xx-hal"]
mock    = []

[dependencies]
embedded-hal = "1.0"
linux-embedded-hal = { version = "0.4", optional = true }
stm32f4xx-hal      = { version = "0.21", optional = true, features = ["stm32f411"] }
```

```rust
// src/main.rs — conditional backend selection

#[cfg(feature = "linux")]
use linux_embedded_hal::SpidevDevice as PlatformSpi;

#[cfg(feature = "stm32")]
use stm32f4xx_hal::spi::Spi as PlatformSpi;

#[cfg(feature = "mock")]
use crate::mock::MockSpi as PlatformSpi;

fn main() {
    // PlatformSpi is resolved at compile time — zero-cost abstraction
    let spi: PlatformSpi = platform_init();
    let mut sensor = Lsm6dsox::new(spi);

    match sensor.who_am_i() {
        Ok(id) => println!("Device ID: {:#04x}", id),
        Err(e) => eprintln!("SPI error: {:?}", e),
    }
}
```

---

## Testing Strategies

### C — Mock Backend

```c
/* spi_hal_mock.c — records calls for unit testing */
#include "spi_hal.h"
#include <string.h>
#include <stdlib.h>

#define MOCK_BUF_SIZE 256

struct spi_bus_t {
    uint8_t  inject[MOCK_BUF_SIZE]; /* Data to return on next read */
    size_t   inject_len;
    uint8_t  captured[MOCK_BUF_SIZE]; /* Last data written */
    size_t   captured_len;
    uint32_t transfer_count;
};

static struct spi_bus_t g_mock;

spi_status_t spi_init(uint8_t id, const spi_config_t* cfg, spi_handle_t* out) {
    (void)id; (void)cfg;
    memset(&g_mock, 0, sizeof(g_mock));
    *out = &g_mock;
    return SPI_OK;
}

void spi_deinit(spi_handle_t h) { (void)h; }

spi_status_t spi_cs_assert(spi_handle_t h, uint8_t id)   { (void)h;(void)id; return SPI_OK; }
spi_status_t spi_cs_deassert(spi_handle_t h, uint8_t id) { (void)h;(void)id; return SPI_OK; }

spi_status_t spi_transfer(spi_handle_t handle,
                           const uint8_t* tx, uint8_t* rx, size_t len)
{
    if (tx) {
        size_t cap = len < MOCK_BUF_SIZE ? len : MOCK_BUF_SIZE;
        memcpy(handle->captured, tx, cap);
        handle->captured_len = cap;
    }
    if (rx && handle->inject_len >= len) {
        memcpy(rx, handle->inject, len);
    }
    handle->transfer_count++;
    return SPI_OK;
}

/* Test helper: inject response bytes */
void mock_spi_inject(spi_handle_t h, const uint8_t* data, size_t len) {
    memcpy(h->inject, data, len);
    h->inject_len = len;
}

/* --- Unit test (using Unity or similar framework) --- */
#include "unity.h"
#include "max31855.h"

void test_max31855_read_temperature(void) {
    spi_config_t cfg = { .clock_hz = 5000000, .mode = SPI_MODE_0, .bits_per_word = 8 };
    spi_handle_t spi;
    spi_init(0, &cfg, &spi);

    /* Inject raw bytes for 25.00°C: 0x0C800000 */
    uint8_t fake_data[] = { 0x0C, 0x80, 0x00, 0x00 };
    mock_spi_inject(spi, fake_data, 4);

    float temp;
    max31855_status_t s = max31855_read(spi, 0, &temp);

    TEST_ASSERT_EQUAL(MAX31855_OK, s);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, temp);
}
```

### Rust — Mock via Trait Impl

```rust
// tests/mock.rs — in-memory mock SPI for unit tests

use spi_hal::SpiBus;

pub struct MockSpi {
    pub inject: Vec<u8>,        // Data to return on reads
    pub captured: Vec<Vec<u8>>, // All transmitted buffers
}

impl MockSpi {
    pub fn new(inject: Vec<u8>) -> Self {
        Self { inject, captured: Vec::new() }
    }
}

#[derive(Debug)]
pub struct MockError;

impl SpiBus for MockSpi {
    type Error = MockError;

    fn transfer(&mut self, tx: &[u8], rx: &mut [u8]) -> Result<(), Self::Error> {
        self.captured.push(tx.to_vec());
        let n = rx.len().min(self.inject.len());
        rx[..n].copy_from_slice(&self.inject[..n]);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::drivers::Max31865;

    #[test]
    fn reads_correct_temperature() {
        // Inject WHO_AM_I response for LSM6DSOX (0x6C)
        let mock = MockSpi::new(vec![0x00, 0x6C]);
        let mut sensor = Lsm6dsox::new(mock);

        let id = sensor.who_am_i().expect("read failed");
        assert_eq!(id, 0x6C, "unexpected device ID");
    }
}
```

---

## Advanced Patterns

### Async / Non-Blocking SPI

For RTOS or async runtime environments, the abstraction should support non-blocking operations:

```rust
// Async SPI trait using Rust's async/await

use core::future::Future;

pub trait AsyncSpiBus {
    type Error;
    type TransferFuture<'a>: Future<Output = Result<(), Self::Error>> + 'a
    where
        Self: 'a;

    fn transfer_async<'a>(
        &'a mut self,
        tx: &'a [u8],
        rx: &'a mut [u8],
    ) -> Self::TransferFuture<'a>;
}

// Usage in an async Embassy task
#[embassy_executor::task]
async fn sensor_task(mut spi: impl AsyncSpiBus<Error = embassy_stm32::spi::Error>) {
    loop {
        let tx = [0x28u8 | 0x80, 0, 0]; // Read accel register
        let mut rx = [0u8; 3];
        spi.transfer_async(&tx, &mut rx).await.unwrap();

        let raw_x = i16::from_le_bytes([rx[1], rx[2]]);
        log::info!("Accel X: {}", raw_x);

        embassy_time::Timer::after_millis(10).await;
    }
}
```

### DMA-Aware Abstraction

For high-throughput applications, the HAL can expose DMA-capable transfers:

```c
/* spi_hal_dma.h — extension for DMA transfers */
#ifndef SPI_HAL_DMA_H
#define SPI_HAL_DMA_H

#include "spi_hal.h"

typedef void (*spi_dma_callback_t)(spi_status_t result, void* user_data);

/**
 * @brief  Begin a DMA-driven SPI transfer. Returns immediately.
 *         `callback` is invoked from interrupt context when complete.
 */
spi_status_t spi_transfer_dma(spi_handle_t      handle,
                                const uint8_t*    tx_buf,
                                uint8_t*          rx_buf,
                                size_t            len,
                                spi_dma_callback_t callback,
                                void*             user_data);

#endif /* SPI_HAL_DMA_H */
```

### Mocking and Simulation

A simulation backend enables complete hardware-in-the-loop testing without physical devices:

```rust
// Simulation backend: records all transactions to a log file

use std::fs::File;
use std::io::Write;
use spi_hal::SpiBus;

pub struct SimSpi {
    log: File,
    tick: u64,
}

impl SimSpi {
    pub fn new(log_path: &str) -> std::io::Result<Self> {
        Ok(Self {
            log: File::create(log_path)?,
            tick: 0,
        })
    }
}

impl SpiBus for SimSpi {
    type Error = std::io::Error;

    fn transfer(&mut self, tx: &[u8], rx: &mut [u8]) -> Result<(), Self::Error> {
        writeln!(self.log, "t={} TX={:02X?}", self.tick, tx)?;
        // Simulate a known response (loopback or pre-programmed table)
        rx.iter_mut().enumerate().for_each(|(i, b)| *b = tx.get(i).copied().unwrap_or(0xFF));
        writeln!(self.log, "t={} RX={:02X?}", self.tick, rx)?;
        self.tick += 1;
        Ok(())
    }
}
```

---

## Comparison: C vs C++ vs Rust

| Aspect | C | C++ | Rust |
|---|---|---|---|
| **Abstraction mechanism** | Function pointers / separate header+impl | Virtual base class / templates | Traits (static or dynamic dispatch) |
| **Dispatch overhead** | Runtime (vtable via fn-ptr) | Runtime (virtual) or zero-cost (template) | Zero-cost (monomorphic) or runtime (dyn Trait) |
| **Compile-time safety** | Minimal; mismatches found at runtime | Moderate; type system helps | Strong; ownership, lifetimes, exhaustive matching |
| **Cross-platform build** | CMake `-DPLATFORM=` feature flags | Same + template specialisation | Cargo features + `#[cfg(...)]` |
| **Ecosystem standard** | Custom per-project | Custom or third-party (libopencm3) | `embedded-hal` crate |
| **Mock / test support** | Replace `.c` file in build | Mock subclass | Mock struct implementing trait |
| **Async support** | Callbacks / RTOS primitives | `std::future` / RTOS | `async/await` + Embassy/RTIC |

---

## Summary

Creating hardware-independent SPI interfaces is a cornerstone of professional embedded firmware design. The key takeaways are:

**Architecture**: Separate the *interface* (what operations SPI provides) from the *implementation* (how a specific MCU or OS performs them). All device driver code must depend only on the interface.

**In C**, define an opaque handle type and a set of functions in a header. Provide one `.c` file per platform that implements them. CMake `PLATFORM` variables or `#ifdef` guards select the correct implementation at build time.

**In C++**, use pure virtual base classes (interfaces) to define the SPI contract. Concrete backends inherit from the interface. Device drivers accept a reference to the base class, enabling runtime or compile-time substitution. Templates allow zero-cost abstraction when the backend type is known at compile time.

**In Rust**, traits are the abstraction primitive. Define `SpiBus` and `SpiDevice` traits. Device drivers are generic over `T: SpiBus`. Cargo features and `#[cfg]` attributes select the correct backend implementation at compile time. Using `embedded-hal` 1.0 traits maximises ecosystem interoperability: thousands of driver crates work with any MCU that provides an `embedded-hal`-compatible HAL.

**Testing** becomes straightforward: replace the hardware backend with an in-memory mock that records transmitted bytes and returns pre-programmed responses. Device driver unit tests run on a host machine in seconds, without any hardware.

**DMA and async** extensions can be layered on top of the base abstraction. Define a separate `AsyncSpiBus` or `DmaSpiBus` trait/extension so drivers that need non-blocking transfers can opt in without complicating the synchronous API.

The investment in abstraction pays off immediately: the same driver compiles for a low-cost STM32, a Raspberry Pi running Linux, and a simulation environment running on a developer's laptop — with no source code changes.

---

*Document: SPI Topic 98 — Abstraction Layers for Portability*
*Covers: C/C++/Rust HAL design, platform backends, device drivers, mocking, async, DMA extensions*