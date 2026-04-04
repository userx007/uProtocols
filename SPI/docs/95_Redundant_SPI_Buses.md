The user wants a markdown document (not a Word doc), so I'll create a comprehensive `.md` file directly.The document covers the full topic across 10 sections:

- **Protocol fundamentals** — signal lines, SPI modes, frame timing
- **Failure modes & standards** — IEC 61508, ISO 26262, DO-178C drivers for redundancy
- **Four architectures** — Active-Active (lockstep), Active-Standby, Cold Standby, and Dual-Master, with trade-off tables
- **Fault detection** — CRC-8, timeouts, MISO loopback, lockstep comparison, heartbeat polling
- **C implementation** — a portable platform-abstraction layer with `SpiRedundantCtx`, full transfer/failover logic, CRC-protected register read/write, and statistics; plus a C++ RAII version using `embedded-hal`-style callbacks and `std::optional`
- **Rust implementation** — `no_std` library using `embedded_hal::SpiDevice` traits, with type-safe `BusHealth` states, both Active-Standby and Lockstep modes, and `embedded-hal-mock` unit tests
- **State machine** — bus health FSM (Healthy → Degraded → Failed) and a graceful degradation policy table
- **Hardware guidance** — isolation, power domains, dual-port vs. duplicated slaves, DMA considerations, and a wiring diagram for redundant IMUs
- **Testing strategy** — mock HAL injection, fault injection, relay-based hardware tests, and a metrics table with targets
- **Summary** — distilled design principles and a note on combining software redundancy with formal FMEA

# 95. Redundant SPI Buses
## Implementing Dual SPI Buses for Fault-Tolerant Systems

---

## Table of Contents

1. [Introduction](#introduction)
2. [SPI Protocol Fundamentals](#spi-protocol-fundamentals)
3. [Why Redundancy?](#why-redundancy)
4. [Redundancy Architectures](#redundancy-architectures)
5. [Fault Detection Strategies](#fault-detection-strategies)
6. [Implementation in C/C++](#implementation-in-cc)
7. [Implementation in Rust](#implementation-in-rust)
8. [Switchover Logic and State Machines](#switchover-logic-and-state-machines)
9. [Hardware Considerations](#hardware-considerations)
10. [Testing and Validation](#testing-and-validation)
11. [Summary](#summary)

---

## Introduction

Redundant SPI (Serial Peripheral Interface) buses are a critical design pattern in safety-critical and high-availability embedded systems. By operating two (or more) independent SPI buses in parallel, a system can continue to communicate with peripherals even when one bus experiences a fault — whether electrical, logical, or due to component failure.

This pattern appears in aerospace avionics, automotive ECUs (Electronic Control Units), industrial PLCs, medical devices, and any application where a communication failure can lead to catastrophic outcomes or unacceptable downtime.

---

## SPI Protocol Fundamentals

SPI is a synchronous serial communication protocol using four main lines:

| Signal | Direction | Description |
|--------|-----------|-------------|
| SCLK   | Master → Slave | Clock signal |
| MOSI   | Master → Slave | Master Out Slave In |
| MISO   | Slave → Master | Master In Slave Out |
| CS/SS  | Master → Slave | Chip Select (active low) |

SPI has four modes defined by Clock Polarity (CPOL) and Clock Phase (CPHA):

| Mode | CPOL | CPHA | Clock Idle | Data Sampled |
|------|------|------|------------|--------------|
| 0    | 0    | 0    | Low        | Rising edge  |
| 1    | 0    | 1    | Low        | Falling edge |
| 2    | 1    | 0    | High       | Falling edge |
| 3    | 1    | 1    | High       | Rising edge  |

A typical SPI frame (8-bit):

```
CS:   ‾‾‾‾|_________________________________|‾‾‾‾
SCLK: ______|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|____
MOSI: ------< D7 >< D6 >< D5 >< D4 >< D3 >...
```

---

## Why Redundancy?

### Failure Modes in SPI Systems

SPI buses can fail in several ways:

- **Electrical faults**: Short circuits, open circuits, ESD damage
- **Signal integrity issues**: Impedance mismatches, reflections at high frequencies
- **Component failure**: Master controller GPIO damage, slave peripheral failure
- **Firmware bugs**: Stuck states, DMA descriptor corruption
- **Environmental**: EMI interference, temperature-induced failures, vibration-caused connection loss
- **Soft errors**: Bit flips due to radiation (common in space/aerospace)

### Cost of Failure

In safety-critical systems, a failed SPI bus can mean:

- Loss of sensor data (IMU, pressure sensors, temperature sensors)
- Inability to control actuators (motor drivers, DACs)
- Complete subsystem shutdown

### Standards Requiring Redundancy

- **IEC 61508** (Functional Safety of E/E/PE Safety-related Systems)
- **ISO 26262** (Road Vehicles — Functional Safety)
- **DO-178C / DO-254** (Avionics Software/Hardware)
- **IEC 62061** (Safety of Machinery)

---

## Redundancy Architectures

### 1. Active-Active (Lockstep / Dual-Write)

Both buses operate simultaneously. Reads can be compared for validation; writes go to both.

```
         ┌─────────────┐
         │   Master    │
         └──┬───────┬──┘
            │       │
         SPI-A   SPI-B
            │       │
         ┌──▼───────▼──┐
         │   Slave     │
         │  (dual bus) │
         └─────────────┘
```

**Pros:** Immediate fault detection, zero switchover time  
**Cons:** Both buses must be constantly driven; slave must support dual interfaces

### 2. Active-Standby (Hot Standby)

One bus is active; the other is powered but idle, ready to take over.

```
         ┌─────────────┐
         │   Master    │
         └──┬───────┬──┘
            │       │
         SPI-A   SPI-B (standby)
         (active)    │
            │       │
         ┌──▼───────▼──┐
         │   Slave     │
         └─────────────┘
```

**Pros:** Simple switchover logic; lower EMI since only one bus is active  
**Cons:** Standby bus not validated until needed; switchover takes some time

### 3. Active-Passive (Cold Standby)

Secondary bus is powered down until needed.

**Pros:** Lowest power consumption  
**Cons:** Longest switchover time; secondary bus untested until activated

### 4. Dual Master Architecture

Two masters each connect to the same set of slaves, useful when the master itself may fail.

```
   ┌────────┐    ┌────────┐
   │Master A│    │Master B│
   └───┬────┘    └────┬───┘
       │   SPI-A      │
       └──────┬───────┘
              │ SPI-B
              │
         ┌────▼────┐
         │  Slave  │
         └─────────┘
```

---

## Fault Detection Strategies

### 1. CRC / Checksum Verification

Every transaction includes a CRC or checksum. A mismatch indicates bus corruption.

```c
uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x31;
            else            crc <<= 1;
        }
    }
    return crc;
}
```

### 2. Timeout Detection

A watchdog or timer detects if an SPI transaction does not complete within a defined window.

### 3. MISO Loopback Check

During idle periods, send a known pattern and verify the echo (requires hardware support or loopback jumper).

### 4. Dual-Read Comparison (Lockstep)

Read the same register on both buses and compare; a discrepancy triggers a fault.

### 5. Heartbeat / Periodic Poll

Regularly poll a slave register whose value is known (e.g., device ID or status register). Failure to respond correctly flags the bus.

---

## Implementation in C/C++

### Platform Abstraction Layer

The following example targets a bare-metal ARM microcontroller (e.g., STM32) using HAL-style SPI, but the design is portable.

#### spi_redundant.h

```c
#ifndef SPI_REDUNDANT_H
#define SPI_REDUNDANT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Bus identifiers */
typedef enum {
    SPI_BUS_A = 0,
    SPI_BUS_B = 1,
    SPI_BUS_COUNT
} SpiBusId;

/* Bus health state */
typedef enum {
    BUS_STATE_OK       = 0,
    BUS_STATE_DEGRADED = 1,  /* errors detected but still functional */
    BUS_STATE_FAILED   = 2   /* bus considered non-operational */
} SpiBusState;

/* Statistics per bus */
typedef struct {
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t error_count;
    uint32_t crc_errors;
    uint32_t timeout_errors;
    uint32_t switchover_count;
} SpiBusStats;

/* Redundant SPI context */
typedef struct {
    SpiBusId   active_bus;
    SpiBusState bus_state[SPI_BUS_COUNT];
    SpiBusStats stats[SPI_BUS_COUNT];
    uint32_t   error_threshold;   /* errors before declaring bus failed */
    bool       lockstep_enabled;  /* compare both buses on every read */
} SpiRedundantCtx;

/* Function return codes */
typedef enum {
    SPI_OK              =  0,
    SPI_ERR_TIMEOUT     = -1,
    SPI_ERR_CRC         = -2,
    SPI_ERR_MISMATCH    = -3,  /* lockstep comparison failed */
    SPI_ERR_BUS_FAILED  = -4,
    SPI_ERR_ALL_FAILED  = -5
} SpiResult;

/* Low-level HAL hooks — implement per platform */
typedef struct {
    SpiResult (*transfer)(SpiBusId bus, const uint8_t *tx, uint8_t *rx,
                          size_t len, uint32_t timeout_ms);
    void      (*assert_cs)(SpiBusId bus);
    void      (*deassert_cs)(SpiBusId bus);
    uint32_t  (*get_tick_ms)(void);
} SpiHal;

/* API */
void      spi_redundant_init(SpiRedundantCtx *ctx, const SpiHal *hal,
                              uint32_t error_threshold, bool lockstep);
SpiResult spi_redundant_transfer(SpiRedundantCtx *ctx, const uint8_t *tx,
                                  uint8_t *rx, size_t len, uint32_t timeout_ms);
SpiResult spi_redundant_write_reg(SpiRedundantCtx *ctx, uint8_t reg,
                                   uint8_t value);
SpiResult spi_redundant_read_reg(SpiRedundantCtx *ctx, uint8_t reg,
                                  uint8_t *value);
void      spi_redundant_get_stats(const SpiRedundantCtx *ctx, SpiBusId bus,
                                   SpiBusStats *out);
bool      spi_redundant_manual_switch(SpiRedundantCtx *ctx, SpiBusId target);
SpiBusId  spi_redundant_active_bus(const SpiRedundantCtx *ctx);

#endif /* SPI_REDUNDANT_H */
```

#### spi_redundant.c

```c
#include "spi_redundant.h"
#include <string.h>

/* Forward declarations */
static SpiResult try_bus(SpiRedundantCtx *ctx, SpiBusId bus,
                          const uint8_t *tx, uint8_t *rx,
                          size_t len, uint32_t timeout_ms);
static void      mark_bus_error(SpiRedundantCtx *ctx, SpiBusId bus,
                                 uint32_t error_flags);
static bool      try_failover(SpiRedundantCtx *ctx);

/* ---------------------------------------------------------------
 * CRC-8/MAXIM implementation
 * --------------------------------------------------------------- */
static uint8_t crc8_compute(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return crc;
}

/* ---------------------------------------------------------------
 * Initialisation
 * --------------------------------------------------------------- */
static const SpiHal *g_hal;  /* store HAL pointer (single instance) */

void spi_redundant_init(SpiRedundantCtx *ctx, const SpiHal *hal,
                         uint32_t error_threshold, bool lockstep) {
    memset(ctx, 0, sizeof(*ctx));
    g_hal              = hal;
    ctx->active_bus    = SPI_BUS_A;
    ctx->error_threshold = error_threshold;
    ctx->lockstep_enabled = lockstep;

    for (int i = 0; i < SPI_BUS_COUNT; i++) {
        ctx->bus_state[i] = BUS_STATE_OK;
    }
}

/* ---------------------------------------------------------------
 * Internal: attempt a raw transfer on a specific bus
 * --------------------------------------------------------------- */
static SpiResult try_bus(SpiRedundantCtx *ctx, SpiBusId bus,
                          const uint8_t *tx, uint8_t *rx,
                          size_t len, uint32_t timeout_ms) {
    if (ctx->bus_state[bus] == BUS_STATE_FAILED) {
        return SPI_ERR_BUS_FAILED;
    }

    g_hal->assert_cs(bus);
    SpiResult result = g_hal->transfer(bus, tx, rx, len, timeout_ms);
    g_hal->deassert_cs(bus);

    if (result == SPI_OK) {
        ctx->stats[bus].tx_bytes += len;
        ctx->stats[bus].rx_bytes += len;
    } else {
        mark_bus_error(ctx, bus, (result == SPI_ERR_TIMEOUT) ? 2 : 1);
    }

    return result;
}

/* ---------------------------------------------------------------
 * Internal: record an error and potentially mark bus failed
 * --------------------------------------------------------------- */
static void mark_bus_error(SpiRedundantCtx *ctx, SpiBusId bus,
                            uint32_t error_flags) {
    ctx->stats[bus].error_count++;

    if (error_flags & 1) ctx->stats[bus].crc_errors++;
    if (error_flags & 2) ctx->stats[bus].timeout_errors++;

    if (ctx->stats[bus].error_count >= ctx->error_threshold) {
        ctx->bus_state[bus] = BUS_STATE_FAILED;
    } else {
        ctx->bus_state[bus] = BUS_STATE_DEGRADED;
    }
}

/* ---------------------------------------------------------------
 * Internal: attempt to switch to the other bus
 * Returns true if a healthy bus is now active
 * --------------------------------------------------------------- */
static bool try_failover(SpiRedundantCtx *ctx) {
    SpiBusId candidate = (ctx->active_bus == SPI_BUS_A) ? SPI_BUS_B : SPI_BUS_A;

    if (ctx->bus_state[candidate] != BUS_STATE_FAILED) {
        ctx->active_bus = candidate;
        ctx->stats[candidate].switchover_count++;
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------
 * Public: full-duplex transfer with automatic failover
 * --------------------------------------------------------------- */
SpiResult spi_redundant_transfer(SpiRedundantCtx *ctx, const uint8_t *tx,
                                   uint8_t *rx, size_t len,
                                   uint32_t timeout_ms) {
    SpiBusId primary = ctx->active_bus;
    uint8_t  rx_b[256];  /* secondary bus receive buffer (max 256 bytes) */

    /* --- Lockstep: transfer on BOTH buses and compare --- */
    if (ctx->lockstep_enabled) {
        SpiResult r_a = try_bus(ctx, SPI_BUS_A, tx, rx,   len, timeout_ms);
        SpiResult r_b = try_bus(ctx, SPI_BUS_B, tx, rx_b, len, timeout_ms);

        if (r_a == SPI_OK && r_b == SPI_OK) {
            if (memcmp(rx, rx_b, len) != 0) {
                /* Data mismatch — one bus is corrupted */
                mark_bus_error(ctx, SPI_BUS_B, 1);
                ctx->active_bus = SPI_BUS_A;
                return SPI_ERR_MISMATCH;
            }
            return SPI_OK;
        }

        /* One bus failed; use the surviving one */
        if (r_a != SPI_OK && r_b == SPI_OK) {
            memcpy(rx, rx_b, len);
            ctx->active_bus = SPI_BUS_B;
            return SPI_OK;
        }
        if (r_b != SPI_OK && r_a == SPI_OK) {
            ctx->active_bus = SPI_BUS_A;
            return SPI_OK;
        }

        /* Both failed */
        return SPI_ERR_ALL_FAILED;
    }

    /* --- Active-Standby: try primary, failover on error --- */
    SpiResult result = try_bus(ctx, primary, tx, rx, len, timeout_ms);

    if (result != SPI_OK) {
        if (try_failover(ctx)) {
            /* Retry on secondary bus */
            result = try_bus(ctx, ctx->active_bus, tx, rx, len, timeout_ms);
        } else {
            return SPI_ERR_ALL_FAILED;
        }
    }

    return result;
}

/* ---------------------------------------------------------------
 * Public: write a register (command byte + data byte)
 * --------------------------------------------------------------- */
SpiResult spi_redundant_write_reg(SpiRedundantCtx *ctx, uint8_t reg,
                                    uint8_t value) {
    uint8_t tx[3] = { reg, value, 0x00 };
    uint8_t rx[3];

    /* Append CRC over address + value */
    tx[2] = crc8_compute(tx, 2);

    return spi_redundant_transfer(ctx, tx, rx, sizeof(tx), 10);
}

/* ---------------------------------------------------------------
 * Public: read a register (send address, receive value + CRC)
 * --------------------------------------------------------------- */
SpiResult spi_redundant_read_reg(SpiRedundantCtx *ctx, uint8_t reg,
                                   uint8_t *value) {
    uint8_t tx[3] = { (uint8_t)(reg | 0x80), 0x00, 0x00 };
    uint8_t rx[3] = {0};

    SpiResult result = spi_redundant_transfer(ctx, tx, rx, sizeof(tx), 10);
    if (result != SPI_OK) return result;

    /* Verify CRC of received data */
    if (crc8_compute(&rx[1], 1) != rx[2]) {
        mark_bus_error(ctx, ctx->active_bus, 1);
        return SPI_ERR_CRC;
    }

    *value = rx[1];
    return SPI_OK;
}

/* ---------------------------------------------------------------
 * Public: statistics
 * --------------------------------------------------------------- */
void spi_redundant_get_stats(const SpiRedundantCtx *ctx, SpiBusId bus,
                               SpiBusStats *out) {
    if (bus < SPI_BUS_COUNT) {
        *out = ctx->stats[bus];
    }
}

bool spi_redundant_manual_switch(SpiRedundantCtx *ctx, SpiBusId target) {
    if (target < SPI_BUS_COUNT && ctx->bus_state[target] != BUS_STATE_FAILED) {
        ctx->active_bus = target;
        ctx->stats[target].switchover_count++;
        return true;
    }
    return false;
}

SpiBusId spi_redundant_active_bus(const SpiRedundantCtx *ctx) {
    return ctx->active_bus;
}
```

#### Example main.c Usage

```c
#include "spi_redundant.h"
#include "platform_hal.h"  /* platform-specific SPI HAL */
#include <stdio.h>

static const SpiHal hal = {
    .transfer    = platform_spi_transfer,
    .assert_cs   = platform_spi_cs_assert,
    .deassert_cs = platform_spi_cs_deassert,
    .get_tick_ms = platform_get_tick_ms
};

int main(void) {
    SpiRedundantCtx ctx;

    /* Initialise with:
     *   - threshold of 5 errors before bus declared failed
     *   - lockstep mode disabled (active-standby) */
    spi_redundant_init(&ctx, &hal, 5, false);

    /* Read device ID from a sensor (register 0x0F) */
    uint8_t device_id = 0;
    SpiResult r = spi_redundant_read_reg(&ctx, 0x0F, &device_id);

    if (r == SPI_OK) {
        printf("Device ID: 0x%02X on bus %s\n",
               device_id,
               spi_redundant_active_bus(&ctx) == SPI_BUS_A ? "A" : "B");
    } else {
        printf("Read failed: %d\n", r);
    }

    /* Configure a register on both buses */
    r = spi_redundant_write_reg(&ctx, 0x20, 0x77);

    SpiBusStats stats;
    spi_redundant_get_stats(&ctx, SPI_BUS_A, &stats);
    printf("Bus A: %u errors, %u switchovers\n",
           stats.error_count, stats.switchover_count);

    return 0;
}
```

### C++ Object-Oriented Version

```cpp
#include <cstdint>
#include <cstring>
#include <array>
#include <functional>
#include <optional>

class RedundantSpi {
public:
    enum class Bus : uint8_t { A = 0, B = 1 };
    enum class Mode { ActiveStandby, Lockstep };

    struct Config {
        uint32_t error_threshold = 5;
        Mode     mode            = Mode::ActiveStandby;
        uint32_t timeout_ms      = 10;
    };

    struct Stats {
        uint32_t transfers    = 0;
        uint32_t errors       = 0;
        uint32_t crc_errors   = 0;
        uint32_t switchovers  = 0;
    };

    /* HAL callbacks */
    using TransferFn   = std::function<bool(Bus, const uint8_t*, uint8_t*, size_t)>;
    using ChipSelectFn = std::function<void(Bus, bool)>;

    RedundantSpi(TransferFn transfer_fn, ChipSelectFn cs_fn, Config cfg = {})
        : transfer_cb_(std::move(transfer_fn))
        , cs_cb_(std::move(cs_fn))
        , cfg_(cfg)
        , active_bus_(Bus::A)
    {
        failed_[0] = failed_[1] = false;
        stats_[0]  = stats_[1] = {};
    }

    /* Perform a transfer; returns true if successful */
    bool transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
        if (cfg_.mode == Mode::Lockstep) {
            return lockstep_transfer(tx, rx, len);
        }
        return active_standby_transfer(tx, rx, len);
    }

    /* Write a single register */
    bool write_register(uint8_t reg, uint8_t val) {
        uint8_t tx[2] = { reg, val };
        uint8_t rx[2];
        return transfer(tx, rx, 2);
    }

    /* Read a single register */
    std::optional<uint8_t> read_register(uint8_t reg) {
        uint8_t tx[2] = { static_cast<uint8_t>(reg | 0x80), 0x00 };
        uint8_t rx[2] = {0};
        if (!transfer(tx, rx, 2)) return std::nullopt;
        return rx[1];
    }

    Bus     active_bus()              const { return active_bus_; }
    Stats   stats(Bus bus)            const { return stats_[idx(bus)]; }
    bool    is_failed(Bus bus)        const { return failed_[idx(bus)]; }

    bool force_switch(Bus target) {
        if (!failed_[idx(target)]) {
            active_bus_ = target;
            stats_[idx(target)].switchovers++;
            return true;
        }
        return false;
    }

private:
    static constexpr int idx(Bus b) { return static_cast<int>(b); }
    static constexpr Bus other(Bus b) {
        return (b == Bus::A) ? Bus::B : Bus::A;
    }

    bool raw_transfer(Bus bus, const uint8_t *tx, uint8_t *rx, size_t len) {
        if (failed_[idx(bus)]) return false;

        cs_cb_(bus, true);
        bool ok = transfer_cb_(bus, tx, rx, len);
        cs_cb_(bus, false);

        stats_[idx(bus)].transfers++;
        if (!ok) {
            stats_[idx(bus)].errors++;
            if (stats_[idx(bus)].errors >= cfg_.error_threshold) {
                failed_[idx(bus)] = true;
            }
        }
        return ok;
    }

    bool active_standby_transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
        if (raw_transfer(active_bus_, tx, rx, len)) return true;

        Bus fallback = other(active_bus_);
        if (!failed_[idx(fallback)]) {
            active_bus_ = fallback;
            stats_[idx(fallback)].switchovers++;
            return raw_transfer(active_bus_, tx, rx, len);
        }
        return false;  /* both buses failed */
    }

    bool lockstep_transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
        std::array<uint8_t, 256> rx_b{};
        bool ok_a = raw_transfer(Bus::A, tx, rx,        len);
        bool ok_b = raw_transfer(Bus::B, tx, rx_b.data(), len);

        if (ok_a && ok_b) {
            if (std::memcmp(rx, rx_b.data(), len) != 0) {
                /* Mismatch — prefer Bus A, flag B */
                failed_[idx(Bus::B)] = true;
                active_bus_ = Bus::A;
            }
            return true;
        }
        if (ok_a) { active_bus_ = Bus::A; return true; }
        if (ok_b) {
            std::memcpy(rx, rx_b.data(), len);
            active_bus_ = Bus::B;
            return true;
        }
        return false;
    }

    TransferFn   transfer_cb_;
    ChipSelectFn cs_cb_;
    Config       cfg_;
    Bus          active_bus_;
    bool         failed_[2];
    Stats        stats_[2];
};
```

---

## Implementation in Rust

Rust's ownership model and type system make it an excellent choice for safety-critical systems. The following implementation uses `no_std` and `embedded-hal` traits.

### Cargo.toml Dependencies

```toml
[package]
name = "redundant-spi"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal = "1.0"

[features]
default = []
defmt = ["dep:defmt"]

[dependencies.defmt]
version = "0.3"
optional = true
```

### src/lib.rs — Core Library

```rust
#![no_std]

use core::fmt;
use embedded_hal::spi::SpiDevice;

/// Identifies one of the redundant SPI buses
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BusId {
    A,
    B,
}

impl BusId {
    fn other(self) -> Self {
        match self {
            BusId::A => BusId::B,
            BusId::B => BusId::A,
        }
    }
}

/// Health state of a single SPI bus
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BusHealth {
    Healthy,
    Degraded { error_count: u32 },
    Failed,
}

impl BusHealth {
    fn is_usable(&self) -> bool {
        !matches!(self, BusHealth::Failed)
    }
}

/// Operational mode for the redundant SPI controller
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RedundancyMode {
    /// Only the active bus is used; failover occurs on error
    ActiveStandby,
    /// Both buses execute every transfer; results are compared
    Lockstep,
}

/// Per-bus statistics
#[derive(Debug, Default, Clone, Copy)]
pub struct BusStats {
    pub transfers:  u32,
    pub errors:     u32,
    pub switchovers: u32,
}

/// Error type for redundant SPI operations
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RedundantSpiError<E> {
    /// The underlying SPI transfer failed on the active bus
    BusError(E),
    /// Both buses have failed
    AllBusesFailed,
    /// Lockstep comparison revealed a data mismatch between buses
    LockstepMismatch,
    /// A CRC verification failed
    CrcError,
}

impl<E: fmt::Debug> fmt::Display for RedundantSpiError<E> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::BusError(e)       => write!(f, "Bus error: {:?}", e),
            Self::AllBusesFailed    => write!(f, "All SPI buses have failed"),
            Self::LockstepMismatch  => write!(f, "Lockstep data mismatch"),
            Self::CrcError          => write!(f, "CRC verification failed"),
        }
    }
}

/// CRC-8/MAXIM implementation
fn crc8(data: &[u8]) -> u8 {
    let mut crc: u8 = 0xFF;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            crc = if crc & 0x80 != 0 {
                (crc << 1) ^ 0x31
            } else {
                crc << 1
            };
        }
    }
    crc
}

/// Configuration for the redundant SPI controller
pub struct RedundantSpiConfig {
    pub mode:            RedundancyMode,
    pub error_threshold: u32,
    pub use_crc:         bool,
}

impl Default for RedundantSpiConfig {
    fn default() -> Self {
        Self {
            mode:            RedundancyMode::ActiveStandby,
            error_threshold: 5,
            use_crc:         false,
        }
    }
}

/// Main redundant SPI controller
///
/// `SpiA` and `SpiB` implement `embedded_hal::spi::SpiDevice`.
pub struct RedundantSpi<SpiA, SpiB> {
    bus_a:      SpiA,
    bus_b:      SpiB,
    config:     RedundantSpiConfig,
    active:     BusId,
    health:     [BusHealth; 2],
    stats:      [BusStats; 2],
}

impl<SpiA, SpiB, E> RedundantSpi<SpiA, SpiB>
where
    SpiA: SpiDevice<Error = E>,
    SpiB: SpiDevice<Error = E>,
    E: fmt::Debug,
{
    /// Create a new redundant SPI controller.
    pub fn new(bus_a: SpiA, bus_b: SpiB, config: RedundantSpiConfig) -> Self {
        Self {
            bus_a,
            bus_b,
            config,
            active:  BusId::A,
            health:  [BusHealth::Healthy, BusHealth::Healthy],
            stats:   [BusStats::default(), BusStats::default()],
        }
    }

    fn bus_idx(id: BusId) -> usize {
        match id { BusId::A => 0, BusId::B => 1 }
    }

    fn record_error(&mut self, bus: BusId) {
        let idx = Self::bus_idx(bus);
        self.stats[idx].errors += 1;

        let errors = self.stats[idx].errors;
        self.health[idx] = if errors >= self.config.error_threshold {
            BusHealth::Failed
        } else {
            BusHealth::Degraded { error_count: errors }
        };
    }

    fn try_failover(&mut self) -> bool {
        let candidate = self.active.other();
        let idx = Self::bus_idx(candidate);
        if self.health[idx].is_usable() {
            self.active = candidate;
            self.stats[idx].switchovers += 1;
            true
        } else {
            false
        }
    }

    /// Perform a SPI transfer with automatic failover.
    ///
    /// `words` is a mutable slice; after the call it contains the
    /// received data from the active bus.
    pub fn transfer(
        &mut self,
        words: &mut [u8],
    ) -> Result<(), RedundantSpiError<E>> {
        match self.config.mode {
            RedundancyMode::Lockstep    => self.lockstep_transfer(words),
            RedundancyMode::ActiveStandby => self.standby_transfer(words),
        }
    }

    fn standby_transfer(
        &mut self,
        words: &mut [u8],
    ) -> Result<(), RedundantSpiError<E>> {
        let primary = self.active;
        let result = self.raw_transfer(primary, words);

        match result {
            Ok(_) => Ok(()),
            Err(e) => {
                self.record_error(primary);
                if self.try_failover() {
                    let secondary = self.active;
                    self.raw_transfer(secondary, words)
                        .map_err(|_| RedundantSpiError::AllBusesFailed)
                } else {
                    Err(RedundantSpiError::BusError(e))
                }
            }
        }
    }

    fn lockstep_transfer(
        &mut self,
        words: &mut [u8],
    ) -> Result<(), RedundantSpiError<E>> {
        // Allocate a secondary receive buffer on the stack (max 64 bytes here)
        let mut buf_b = [0u8; 64];
        let len = words.len().min(buf_b.len());

        buf_b[..len].copy_from_slice(&words[..len]);

        let ok_a = self.raw_transfer_a(&mut words[..len]).is_ok();
        let ok_b = self.raw_transfer_b(&mut buf_b[..len]).is_ok();

        match (ok_a, ok_b) {
            (true, true) => {
                if words[..len] != buf_b[..len] {
                    // Prefer Bus A; mark Bus B as failed
                    self.record_error(BusId::B);
                    Err(RedundantSpiError::LockstepMismatch)
                } else {
                    Ok(())
                }
            }
            (true, false) => {
                self.record_error(BusId::B);
                self.active = BusId::A;
                Ok(())
            }
            (false, true) => {
                self.record_error(BusId::A);
                words[..len].copy_from_slice(&buf_b[..len]);
                self.active = BusId::B;
                Ok(())
            }
            (false, false) => Err(RedundantSpiError::AllBusesFailed),
        }
    }

    /// Low-level transfer on whichever bus is `active`.
    fn raw_transfer(&mut self, bus: BusId, words: &mut [u8]) -> Result<(), E> {
        self.stats[Self::bus_idx(bus)].transfers += 1;
        match bus {
            BusId::A => self.bus_a.transfer_in_place(words),
            BusId::B => self.bus_b.transfer_in_place(words),
        }
    }

    fn raw_transfer_a(&mut self, words: &mut [u8]) -> Result<(), E> {
        self.stats[0].transfers += 1;
        self.bus_a.transfer_in_place(words)
    }

    fn raw_transfer_b(&mut self, words: &mut [u8]) -> Result<(), E> {
        self.stats[1].transfers += 1;
        self.bus_b.transfer_in_place(words)
    }

    /// Write to a device register.
    ///
    /// If `use_crc` is set in config, appends a CRC-8 byte.
    pub fn write_register(
        &mut self,
        reg: u8,
        value: u8,
    ) -> Result<(), RedundantSpiError<E>> {
        let mut buf = if self.config.use_crc {
            let crc = crc8(&[reg, value]);
            [reg, value, crc, 0]
        } else {
            [reg, value, 0, 0]
        };

        let len = if self.config.use_crc { 3 } else { 2 };
        self.transfer(&mut buf[..len])
    }

    /// Read from a device register.
    ///
    /// If `use_crc` is set, verifies the CRC of the response.
    pub fn read_register(
        &mut self,
        reg: u8,
    ) -> Result<u8, RedundantSpiError<E>> {
        let mut buf = [reg | 0x80, 0x00, 0x00];
        let len = if self.config.use_crc { 3 } else { 2 };

        self.transfer(&mut buf[..len])?;

        if self.config.use_crc {
            let expected = crc8(&buf[1..2]);
            if expected != buf[2] {
                return Err(RedundantSpiError::CrcError);
            }
        }

        Ok(buf[1])
    }

    /// Manually switch to a specific bus (e.g. for testing or maintenance).
    pub fn switch_to(&mut self, bus: BusId) -> bool {
        let idx = Self::bus_idx(bus);
        if self.health[idx].is_usable() {
            self.active = bus;
            self.stats[idx].switchovers += 1;
            true
        } else {
            false
        }
    }

    /// Current active bus
    pub fn active_bus(&self) -> BusId { self.active }

    /// Health of a specific bus
    pub fn bus_health(&self, bus: BusId) -> BusHealth {
        self.health[Self::bus_idx(bus)]
    }

    /// Statistics for a specific bus
    pub fn bus_stats(&self, bus: BusId) -> &BusStats {
        &self.stats[Self::bus_idx(bus)]
    }
}
```

### src/main.rs — Example Application

```rust
#![no_std]
#![no_main]

use redundant_spi::{
    BusId, RedundantSpi, RedundantSpiConfig, RedundancyMode,
};
// Platform imports (e.g. stm32f4xx-hal or rp2040-hal)
// use your_hal::spi::Spi;
// use your_hal::prelude::*;

#[cortex_m_rt::entry]
fn main() -> ! {
    // --- Hardware initialisation (platform-specific) ---
    // let dp = stm32f4xx_hal::stm32::Peripherals::take().unwrap();
    // let spi_a = Spi::new(dp.SPI1, pins_a, mode, freq, clocks);
    // let spi_b = Spi::new(dp.SPI2, pins_b, mode, freq, clocks);

    // Placeholder mock SPIs for illustration
    let spi_a = MockSpi::new(BusId::A);
    let spi_b = MockSpi::new(BusId::B);

    let config = RedundantSpiConfig {
        mode:            RedundancyMode::ActiveStandby,
        error_threshold: 3,
        use_crc:         true,
    };

    let mut spi = RedundantSpi::new(spi_a, spi_b, config);

    // Read device ID (register 0x0F)
    match spi.read_register(0x0F) {
        Ok(id) => {
            // Use defmt or a serial logger in real code
            let _ = id;
        }
        Err(_e) => {
            // Handle error — attempt recovery, assert safety output, etc.
        }
    }

    // Write configuration register
    let _ = spi.write_register(0x20, 0x77);

    // Inspect health
    let health_a = spi.bus_health(BusId::A);
    let stats_b  = spi.bus_stats(BusId::B);
    let _ = (health_a, stats_b);

    loop {}
}
```

### Unit Tests

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use embedded_hal_mock::eh1::spi::{Mock as SpiMock, Transaction};

    #[test]
    fn test_active_standby_failover() {
        // Bus A returns an error; Bus B should be used
        let expectations_a = vec![
            Transaction::transfer_in_place(
                vec![0x8F, 0x00],
                Err(embedded_hal_mock::eh1::MockError::Io(
                    std::io::ErrorKind::TimedOut
                )),
            ),
        ];
        let expectations_b = vec![
            Transaction::transfer_in_place(vec![0x8F, 0x00], vec![0x8F, 0x47]),
        ];

        let spi_a = SpiMock::new(&expectations_a);
        let spi_b = SpiMock::new(&expectations_b);

        let mut spi = RedundantSpi::new(
            spi_a,
            spi_b,
            RedundantSpiConfig {
                mode: RedundancyMode::ActiveStandby,
                error_threshold: 5,
                use_crc: false,
            },
        );

        let result = spi.read_register(0x0F);
        assert!(result.is_ok());
        assert_eq!(spi.active_bus(), BusId::B);
    }

    #[test]
    fn test_lockstep_mismatch_detected() {
        let expectations_a = vec![
            Transaction::transfer_in_place(vec![0x8F, 0x00], vec![0x8F, 0x47]),
        ];
        let expectations_b = vec![
            Transaction::transfer_in_place(vec![0x8F, 0x00], vec![0x8F, 0xFF]),
        ];

        let spi_a = SpiMock::new(&expectations_a);
        let spi_b = SpiMock::new(&expectations_b);

        let mut spi = RedundantSpi::new(
            spi_a,
            spi_b,
            RedundantSpiConfig {
                mode: RedundancyMode::Lockstep,
                error_threshold: 5,
                use_crc: false,
            },
        );

        let result = spi.read_register(0x0F);
        assert!(matches!(
            result,
            Err(redundant_spi::RedundantSpiError::LockstepMismatch)
        ));
    }
}
```

---

## Switchover Logic and State Machines

The bus health and switchover logic is best represented as a finite state machine:

```
          ┌─────────────────────────────────────────────────────┐
          │                   Bus State Machine                 │
          │                                                     │
          │   ┌──────────┐   errors > 0        ┌──────────────┐ │
          │   │          │ ──────────────────▶ │              │ │
          │   │ HEALTHY  │                     │  DEGRADED    │ │
          │   │          │ ◀────────────────── │              │ │
          │   └──────────┘   reset / recovery  └──────┬───────┘ │
          │                                           │         │
          │                              errors ≥ threshold     │
          │                                           │         │
          │                                           ▼         │
          │                                    ┌──────────────┐ │
          │                                    │   FAILED     │ │
          │                                    │  (no retry)  │ │
          │                                    └──────────────┘ │
          └─────────────────────────────────────────────────────┘
```

### Graceful Degradation Policy

| Active Bus | Standby Bus | Action |
|------------|-------------|--------|
| Healthy    | Healthy     | Normal operation on active bus |
| Degraded   | Healthy     | Continue on active; log warning |
| Failed     | Healthy     | Immediate failover to standby |
| Healthy    | Failed      | Continue on active; raise alert |
| Degraded   | Degraded    | Use active; escalate to system alert |
| Failed     | Failed      | Emergency stop / safe state |

---

## Hardware Considerations

### Isolation Between Buses

To ensure true independence, each SPI bus should have:

- **Separate PCB routing layers** — prevents a solder bridge or trace fault on one bus from affecting the other
- **Independent power domains** — use separate LDOs for Bus A and Bus B
- **TVS diode protection** on each signal line
- **Series termination resistors** (22–47 Ω) to control signal integrity

### Slave Device Options

| Approach | Description | Pros | Cons |
|----------|-------------|------|------|
| Dual-port slaves | Devices with two independent SPI interfaces | Simplest wiring | Expensive; rare |
| Duplicated slaves | Two identical slave ICs, one per bus | True hardware redundancy | Higher BOM cost |
| Bus switches (mux) | Single slave; buses switched via analog mux | Low cost | Mux itself is a failure point |

### Example: Redundant IMU Wiring

```
MCU                     IMU (Primary)          IMU (Redundant)
────────────────────    ─────────────────      ─────────────────
SPI1_SCLK ──────────────▶ SCL                
SPI1_MOSI ──────────────▶ SDA (MOSI)          
SPI1_MISO ◀──────────────  SDO               
SPI1_CS   ──────────────▶ CS                  

SPI2_SCLK ────────────────────────────────────▶ SCL
SPI2_MOSI ────────────────────────────────────▶ SDA (MOSI)
SPI2_MISO ◀────────────────────────────────────  SDO
SPI2_CS   ────────────────────────────────────▶ CS
```

### DMA Considerations

When using DMA for SPI transfers in a redundant system:

- Allocate independent DMA channels for each bus
- Use separate DMA descriptor buffers to avoid a corrupted descriptor affecting both buses
- Implement DMA error interrupts and link them to the bus health tracker

---

## Testing and Validation

### Unit Testing Strategy

1. **Mock HAL injection** — use mock SPI implementations (as shown in Rust tests above) to simulate bus errors, timeouts, and data corruption without hardware
2. **Fault injection** — deliberately inject errors at configurable rates to test switchover logic
3. **Timeout simulation** — use adjustable timer mocks to test timeout paths

### Integration Testing

1. **Hardware fault simulation** — use a relay or FET to physically disconnect Bus A mid-operation and verify failover
2. **Bit error injection** — use a test fixture that introduces controlled bit flips on MISO
3. **Power rail interruption** — cut Vcc to Bus A's transceiver and verify Bus B takes over

### Metrics to Validate

| Metric | Target |
|--------|--------|
| Switchover time | < 1 ms for hot standby |
| False positive failover rate | < 1 per 10⁶ transactions |
| CRC detection coverage | ≥ 99.6% (single-byte CRC-8) |
| Data mismatch detection | 100% (lockstep mode) |
| MTBF improvement | ≥ 2× vs single bus |

---

## Summary

Redundant SPI buses are a fundamental building block for fault-tolerant embedded systems. The key design decisions and takeaways are:

**Architecture choice matters:** Active-Active (lockstep) offers the strongest fault detection and zero latency on switchover, but requires more hardware resources. Active-Standby is simpler and lower-power, with a brief switchover delay.

**Abstraction is essential:** A clean Hardware Abstraction Layer (HAL) separating the redundancy logic from platform-specific SPI drivers makes the system testable and portable. Both the C and Rust implementations above demonstrate this with injectable HAL callbacks.

**Fault detection layers:** CRC verification catches data corruption; timeout detection catches hung buses; lockstep comparison catches asymmetric failures. Combining all three gives defence in depth.

**Graceful degradation:** The system should not simply binary-flip from "working" to "failed" — a degraded state with increasing error counts allows the application to log warnings, alert operators, and prepare for a potential full failure.

**Hardware independence matters:** The redundancy provided by dual software paths is undermined if both buses share a common power rail, PCB layer, or physical connector. True fault tolerance requires hardware independence in addition to software logic.

**Rust advantages:** Rust's type system makes illegal states (e.g., treating a failed bus as active) representable at compile time. The `embedded-hal` trait abstraction makes unit testing with mock SPIs straightforward and avoids the need for hardware in the loop for most test cases.

**Test with real faults:** Simulated faults are necessary but not sufficient. Physical fault injection — pulling connectors, injecting noise, cutting power rails — is required to validate that the system behaves correctly in the conditions it was designed to handle.

In safety-critical applications, the redundant SPI pattern should be combined with a formal FMEA (Failure Mode and Effects Analysis) to ensure every identified failure mode has been addressed either through detection, isolation, or tolerance.

---

*Document version 1.0 — Topic 95: Redundant SPI Buses*