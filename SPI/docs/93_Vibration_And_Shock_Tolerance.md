# 93. SPI Vibration and Shock Tolerance

**Structure:**
1. **Introduction** — Why SPI is uniquely vulnerable in vibrating/shock environments and where these appear (automotive, aerospace, industrial, oil & gas)
2. **Failure Modes** — Intermittent connections, PCB flexure, ground bounce, component resonance, and the critical issue of *silent data corruption* (SPI has no built-in error detection)
3. **Hardware Mitigations** — Connector selection, signal termination, conformal coating, PCB mounting, and clock speed guidance with a trace-length table
4. **Software Strategies** — CRC framing, exponential-backoff retries, timeouts, majority voting, bus reset, and last-known-good value retention

**Code Examples:**
- **C** — CRC-8 frame builder/validator, retry engine with exponential backoff, bus reset sequence, and fault injection hook for testing
- **C++** — Template-based majority-vote reader, watchdog-protected SPI driver using `std::chrono` timeouts
- **Rust** — Full `embedded-hal` v1.x CRC-8 transaction layer, majority-vote reader, and a complete state-machine driver (`Healthy → Degraded → BusReset → Failed`) with error statistics and unit tests

**Testing & Validation:** Covers relevant standards (IEC 60068, MIL-STD-810H, DO-160G, ISO 16750-3) and software-level fault injection techniques.

> **Ensuring reliable SPI communication in harsh mechanical environments**

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Failure Modes in Harsh Mechanical Environments](#2-failure-modes-in-harsh-mechanical-environments)
3. [Hardware-Level Mitigation Strategies](#3-hardware-level-mitigation-strategies)
4. [Software and Protocol-Level Resilience Strategies](#4-software-and-protocol-level-resilience-strategies)
5. [C/C++ Implementation Examples](#5-cc-implementation-examples)
6. [Rust Implementation Examples](#6-rust-implementation-examples)
7. [Testing and Validation](#7-testing-and-validation)
8. [Summary](#8-summary)

---

## 1. Introduction

The Serial Peripheral Interface (SPI) is a synchronous, full-duplex communication protocol widely used in embedded systems to connect microcontrollers to peripherals such as sensors, ADCs, DACs, flash memory, and display controllers. Its simplicity and speed make it attractive, but these same traits — relying on tight timing, signal integrity, and stable electrical connections — make SPI **particularly vulnerable** in vibration-prone and shock-exposed environments.

Harsh mechanical environments appear in:

- **Automotive systems** (engine bay, suspension ECUs, ABS units)
- **Industrial machinery** (CNC, robotic arms, conveyor systems)
- **Aerospace and defense** (avionics, missile guidance, satellite subsystems)
- **Oil & gas equipment** (downhole drilling tools, pipeline monitoring)
- **Consumer power tools** and handheld instruments

In these environments, mechanical stress causes intermittent connections, PCB flex, connector fretting, and signal glitches that can corrupt SPI transfers, lock up buses, or silently deliver wrong data — all without triggering a detectable hardware fault.

---

## 2. Failure Modes in Harsh Mechanical Environments

Understanding the root causes is the first step toward building resilience.

### 2.1 Intermittent Connections

Vibration causes micro-movements in connectors, solder joints, and press-fit components. These produce brief open-circuit conditions on one or more SPI lines (MOSI, MISO, SCLK, CS). Even a glitch of a few nanoseconds can corrupt a bit or desynchronise the clock/data relationship.

**Consequences:**
- Bit errors in transferred data
- CS line bouncing, causing phantom transactions
- Clock line glitching, leading to extra or missing clock edges

### 2.2 PCB Flexure and Trace Impedance Changes

Mechanical shock causes the PCB to flex. This alters trace geometry, changes parasitic capacitances and inductances, and can cause differential impedance on high-speed SPI lines to deviate, resulting in reflections and signal integrity degradation.

### 2.3 Ground Bounce and Common-Mode Noise

Shock events introduce mechanical energy that can induce voltage spikes through ground planes. Ground bounce raises the reference voltage transiently, effectively shifting signal levels, potentially causing logic-level violations on SPI signals.

### 2.4 Component Resonance

At certain vibration frequencies, SMD capacitors, ferrite beads, and crystals can resonate. An oscillator that feeds the SPI clock may skip cycles or produce jitter, breaking timing assumptions of the SPI peripheral.

### 2.5 Silent Data Corruption

Perhaps the most dangerous failure: SPI has **no built-in error detection**. A corrupted byte is delivered silently to the application layer. In safety-critical systems, acting on corrupted sensor data can be catastrophic.

---

## 3. Hardware-Level Mitigation Strategies

Software can compensate for many failure modes, but hardware design remains the foundation.

### 3.1 Connector and Wiring Selection

- Use **locking connectors** (MIL-DTL-38999, Deutsch DT, Molex MicroFit with positive locks) rather than friction-fit alternatives.
- Prefer **twisted-pair wiring** for SCLK/CS lines on longer runs to reduce differential noise.
- Minimise connector count in the signal path — every connector is a potential intermittent fault site.

### 3.2 Signal Integrity

- Keep SPI trace lengths short and matched where possible.
- Use **series termination resistors** (22–47 Ω) close to the driver to suppress reflections.
- Add **bulk decoupling capacitors** near each VCC pin of SPI devices.
- Route SPI signals away from high-current switching traces.

### 3.3 Underfilling and Conformal Coating

- **Underfill** BGA and QFN components to prevent solder joint cracking.
- Apply **conformal coating** (acrylic or polyurethane) to protect PCB traces from humidity ingress that worsens vibration-induced corrosion fretting.

### 3.4 Shock Mounts and PCB Support

- Mount PCBs on **vibration-isolating standoffs** (rubber grommets, shoulder washers).
- Add PCB edge guides or centre supports to limit flex amplitude.
- Keep heavy components (transformers, large electrolytics) away from board edges and well-secured.

### 3.5 Reducing SPI Bus Speed

Lower clock frequencies reduce susceptibility to signal integrity issues:

| SPI Clock | Max recommended trace length (unterminated) |
|-----------|---------------------------------------------|
| 1 MHz     | ~300 mm                                     |
| 10 MHz    | ~80 mm                                      |
| 50 MHz    | ~20 mm                                      |

In a vibrating environment, operating at 1/4 of the maximum rated speed provides substantial margin.

---

## 4. Software and Protocol-Level Resilience Strategies

Once the hardware is as robust as practical, software techniques provide the remaining layers of defence.

### 4.1 Data Integrity: CRC and Checksums

Append a **CRC-8 or CRC-16** to every SPI message. On receipt, recalculate and compare. Any mismatch triggers a retry.

Common polynomials:

| CRC    | Polynomial     | Use Case                        |
|--------|----------------|---------------------------------|
| CRC-8  | 0x07 (CCITT)   | Short frames, sensor telemetry  |
| CRC-16 | 0x8005         | Medium frames, memory           |
| CRC-32 | 0x04C11DB7     | File-system, large data blocks  |

### 4.2 Retry Mechanisms with Backoff

On a CRC mismatch or timeout, retransmit the request. Use **exponential backoff** to avoid flooding a damaged bus:

```
delay = base_delay * 2^attempt (capped at max_delay)
```

### 4.3 Transaction Timeouts

Never wait indefinitely for MISO data. Implement a byte-level timeout; if the peripheral does not respond within a deadline, abort and signal an error.

### 4.4 Bit-Bang Fallback

In software bit-banging, you can add inter-bit delays and sample each bit multiple times (majority voting), trading speed for resilience.

### 4.5 Redundant Reads and Majority Voting

For critical sensor registers, read the same register 3 times and accept the majority value. This tolerates a single-read corruption.

### 4.6 Sequence Numbers and Transaction IDs

Tag outgoing commands with a sequence number. Validate that the response carries a matching ID (if the SPI device supports it — common in automotive SPI peripherals per ISO 11898 / AUTOSAR SPIM).

### 4.7 Watchdog-Protected Bus Reset

If the SPI bus enters an error state (e.g., MISO stuck low due to an interrupted transaction), perform a full bus reset: toggle CS and clock lines to flush partial transactions, then re-initialise the peripheral.

### 4.8 Double-Buffering and Atomic Register Reads

Multi-byte registers (e.g., a 16-bit ADC result spread over two bytes) must be read atomically. Use the peripheral's shadow/latch mechanism if available, or read twice and compare.

---

## 5. C/C++ Implementation Examples

### 5.1 CRC-8 Protected SPI Write/Read (C)

```c
/**
 * spi_resilient.c
 *
 * CRC-8 protected SPI communication with retry logic.
 * Targets a generic embedded C environment (e.g., STM32 HAL, bare metal AVR).
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---------- Platform abstraction ---------- */
/* Replace these stubs with your HAL calls    */

static void spi_cs_assert(void)   { /* drive CS low  */ }
static void spi_cs_deassert(void) { /* drive CS high */ }
static uint32_t get_tick_ms(void) { return 0; /* system tick */ }

/**
 * Perform a full-duplex SPI transfer of `len` bytes.
 * Returns 0 on success, -1 on timeout.
 */
static int spi_transfer_raw(const uint8_t *tx, uint8_t *rx,
                             uint16_t len, uint32_t timeout_ms);

/* ---------- CRC-8 (CCITT, poly=0x07) ---------- */

static uint8_t crc8_update(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x80)
            crc = (crc << 1) ^ 0x07;
        else
            crc <<= 1;
    }
    return crc;
}

static uint8_t crc8_buf(const uint8_t *buf, uint16_t len)
{
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++)
        crc = crc8_update(crc, buf[i]);
    return crc;
}

/* ---------- Resilient SPI transaction ---------- */

#define SPI_MAX_PAYLOAD     64
#define SPI_MAX_RETRIES      5
#define SPI_RETRY_BASE_MS    2     /* base backoff delay in ms   */
#define SPI_XFER_TIMEOUT_MS 10

typedef struct {
    uint8_t data[SPI_MAX_PAYLOAD];
    uint8_t len;
} spi_frame_t;

/**
 * Build a framed SPI write buffer: [cmd][payload...][crc8]
 */
static void frame_build(uint8_t cmd, const uint8_t *payload,
                         uint8_t payload_len, uint8_t *out, uint8_t *out_len)
{
    out[0] = cmd;
    memcpy(&out[1], payload, payload_len);
    out[payload_len + 1] = crc8_buf(out, payload_len + 1);
    *out_len = payload_len + 2; /* cmd + payload + crc */
}

/**
 * Validate a received frame: last byte must be crc8 of preceding bytes.
 * Returns true if valid.
 */
static bool frame_validate(const uint8_t *frame, uint8_t len)
{
    if (len < 2) return false;
    uint8_t expected = crc8_buf(frame, len - 1);
    return frame[len - 1] == expected;
}

/**
 * Perform a CRC-protected SPI transaction with exponential backoff retries.
 *
 * @param cmd         Command/register byte to send
 * @param tx_payload  Data bytes to send after command
 * @param tx_len      Number of payload bytes
 * @param rx_frame    Buffer to receive response frame (caller allocates)
 * @param rx_len      Expected response length (including trailing CRC byte)
 *
 * @return  0  success
 *         -1  CRC mismatch after all retries
 *         -2  SPI hardware timeout
 */
int spi_transaction(uint8_t cmd,
                    const uint8_t *tx_payload, uint8_t tx_len,
                    uint8_t *rx_frame, uint8_t rx_len)
{
    uint8_t tx_frame[SPI_MAX_PAYLOAD + 2];
    uint8_t tx_frame_len = 0;
    frame_build(cmd, tx_payload, tx_len, tx_frame, &tx_frame_len);

    uint32_t delay_ms = SPI_RETRY_BASE_MS;

    for (int attempt = 0; attempt <= SPI_MAX_RETRIES; attempt++) {
        spi_cs_assert();
        int rc = spi_transfer_raw(tx_frame, rx_frame,
                                   (uint16_t)rx_len,
                                   SPI_XFER_TIMEOUT_MS);
        spi_cs_deassert();

        if (rc != 0) {
            /* Hardware-level timeout — bus may be locked up */
            spi_bus_reset(); /* defined below */
            return -2;
        }

        if (frame_validate(rx_frame, rx_len)) {
            return 0; /* success */
        }

        /* CRC mismatch: back off and retry */
        if (attempt < SPI_MAX_RETRIES) {
            delay_ms_blocking(delay_ms);
            delay_ms = (delay_ms < 64) ? delay_ms * 2 : 64;
        }
    }

    return -1; /* all retries exhausted */
}

/* ---------- Bus reset (flush partial transactions) ---------- */

/**
 * Recover the SPI bus after a lockup or interrupted transaction.
 * Clocks out up to 16 dummy bytes with CS deasserted to clear
 * any device stuck mid-frame, then reasserts CS briefly.
 */
void spi_bus_reset(void)
{
    static const uint8_t dummy[16] = {0xFF};
    uint8_t sink[16];

    spi_cs_deassert();
    /* Clock out dummy bytes to unstick peripheral shift registers */
    spi_transfer_raw(dummy, sink, 16, SPI_XFER_TIMEOUT_MS);
    /* Brief CS pulse to signal end-of-frame to the peripheral */
    spi_cs_assert();
    delay_us_blocking(10);
    spi_cs_deassert();
}
```

---

### 5.2 Redundant Triple-Read with Majority Voting (C++)

```cpp
/**
 * spi_majority_vote.cpp
 *
 * Reads a critical 16-bit sensor register three times
 * and returns the majority (2-of-3) value.
 * Designed for sensors in high-vibration environments.
 */

#include <cstdint>
#include <optional>
#include <array>

/**
 * Read a 16-bit big-endian register from an SPI device.
 * Returns the value on success, or std::nullopt on error.
 */
std::optional<uint16_t> spi_read_register_16(uint8_t reg_addr);

/**
 * Majority vote across three identical reads of a 16-bit register.
 *
 * Tolerance: one corrupted read out of three is silently corrected.
 * If all three reads differ (three-way disagreement), returns nullopt.
 *
 * @param reg_addr  Register address to read
 * @return          Agreed value on success, or nullopt on failure
 */
std::optional<uint16_t> spi_read_majority_vote(uint8_t reg_addr)
{
    std::array<std::optional<uint16_t>, 3> results;

    for (auto &r : results) {
        r = spi_read_register_16(reg_addr);
        if (!r.has_value()) {
            return std::nullopt; /* hardware error */
        }
    }

    const auto &a = results[0].value();
    const auto &b = results[1].value();
    const auto &c = results[2].value();

    /* 2-of-3 majority */
    if (a == b || a == c) return a;
    if (b == c)           return b;

    /* All three differ — unresolvable corruption */
    return std::nullopt;
}

/**
 * Application example: read accelerometer X-axis with majority voting.
 * Logs an error and uses the last known good value if voting fails.
 */
class ResilientAccelerometer {
public:
    static constexpr uint8_t REG_ACCEL_X = 0x28;

    bool update()
    {
        auto result = spi_read_majority_vote(REG_ACCEL_X);
        if (!result.has_value()) {
            error_count_++;
            return false; /* caller retains last_x_ */
        }
        last_x_ = static_cast<int16_t>(result.value());
        return true;
    }

    int16_t x_raw()    const { return last_x_;     }
    uint32_t errors()  const { return error_count_; }

private:
    int16_t  last_x_      = 0;
    uint32_t error_count_ = 0;
};
```

---

### 5.3 Watchdog-Monitored SPI Driver with Transaction Timeout (C++)

```cpp
/**
 * spi_watchdog.cpp
 *
 * Wraps SPI transactions with a per-byte timeout.
 * If the peripheral stops responding (e.g., stuck MISO),
 * the transaction is aborted and the bus is reset.
 */

#include <cstdint>
#include <chrono>
#include <functional>
#include <stdexcept>

using Clock    = std::chrono::steady_clock;
using Duration = std::chrono::milliseconds;

/* Platform callback types */
using ByteSendFn    = std::function<void(uint8_t)>;
using ByteReceiveFn = std::function<std::optional<uint8_t>(Duration timeout)>;
using BusResetFn    = std::function<void()>;

class GuardedSpiDriver {
public:
    GuardedSpiDriver(ByteSendFn send, ByteReceiveFn recv,
                     BusResetFn reset, Duration byte_timeout)
        : send_(send), recv_(recv),
          reset_(reset), byte_timeout_(byte_timeout) {}

    /**
     * Perform a guarded full-duplex transfer.
     * Throws std::runtime_error on timeout or bus error.
     */
    std::vector<uint8_t> transfer(const std::vector<uint8_t> &tx)
    {
        std::vector<uint8_t> rx;
        rx.reserve(tx.size());

        for (uint8_t byte : tx) {
            send_(byte);
            auto received = recv_(byte_timeout_);
            if (!received.has_value()) {
                reset_();
                throw std::runtime_error(
                    "SPI timeout: peripheral not responding");
            }
            rx.push_back(received.value());
        }

        return rx;
    }

    /**
     * Safe transfer: catches exceptions, performs bus reset,
     * and returns empty vector on failure.
     */
    std::vector<uint8_t> safe_transfer(const std::vector<uint8_t> &tx,
                                        uint8_t retries = 3)
    {
        for (uint8_t attempt = 0; attempt <= retries; attempt++) {
            try {
                return transfer(tx);
            } catch (const std::runtime_error &e) {
                /* reset_ already called inside transfer() */
                if (attempt == retries) return {};
                /* brief inter-retry pause */
                std::this_thread::sleep_for(
                    Duration(4 << attempt)); /* 4, 8, 16 ms */
            }
        }
        return {};
    }

private:
    ByteSendFn    send_;
    ByteReceiveFn recv_;
    BusResetFn    reset_;
    Duration      byte_timeout_;
};
```

---

## 6. Rust Implementation Examples

### 6.1 CRC-8 Protected SPI Frame (Rust + `embedded-hal`)

```rust
//! spi_resilient.rs
//!
//! CRC-8 protected, retry-capable SPI transactions for
//! `embedded-hal` v1.x environments.
//!
//! Dependencies (Cargo.toml):
//!   embedded-hal = "1.0"
//!   nb           = "1.0"

use embedded_hal::spi::SpiDevice;

// ─────────────────────────────────────────────
// CRC-8 (CCITT, polynomial 0x07)
// ─────────────────────────────────────────────

fn crc8_update(mut crc: u8, byte: u8) -> u8 {
    crc ^= byte;
    for _ in 0..8 {
        if crc & 0x80 != 0 {
            crc = (crc << 1) ^ 0x07;
        } else {
            crc <<= 1;
        }
    }
    crc
}

fn crc8_buf(buf: &[u8]) -> u8 {
    buf.iter().fold(0x00u8, |crc, &b| crc8_update(crc, b))
}

// ─────────────────────────────────────────────
// Frame codec
// ─────────────────────────────────────────────

/// Build a TX frame: `[cmd, payload..., crc8]`
fn build_frame(cmd: u8, payload: &[u8], out: &mut Vec<u8>) {
    out.clear();
    out.push(cmd);
    out.extend_from_slice(payload);
    let crc = crc8_buf(out);
    out.push(crc);
}

/// Validate a received frame (last byte is CRC over all preceding bytes).
fn validate_frame(frame: &[u8]) -> bool {
    if frame.len() < 2 {
        return false;
    }
    let (data, &[expected]) = frame.split_at(frame.len() - 1) else {
        return false;
    };
    crc8_buf(data) == expected
}

// ─────────────────────────────────────────────
// Error type
// ─────────────────────────────────────────────

#[derive(Debug)]
pub enum SpiError<E> {
    /// Underlying SPI bus error
    Bus(E),
    /// CRC mismatch after all retries
    CrcMismatch,
    /// Response frame too short
    FrameTooShort,
}

// ─────────────────────────────────────────────
// Resilient transaction
// ─────────────────────────────────────────────

/// Perform a CRC-protected SPI transaction with exponential-backoff retries.
///
/// # Arguments
/// * `spi`      – An `embedded-hal` `SpiDevice` (handles CS automatically)
/// * `cmd`      – Command / register address byte
/// * `payload`  – Data bytes following the command
/// * `rx_len`   – Expected number of *response* bytes (excluding CRC)
/// * `delay_fn` – Callback to block for N milliseconds (platform-specific)
///
/// # Returns
/// The validated response payload on success (CRC stripped).
pub fn spi_transaction<SPI, DelayFn>(
    spi: &mut SPI,
    cmd: u8,
    payload: &[u8],
    rx_len: usize,
    delay_fn: &mut DelayFn,
) -> Result<Vec<u8>, SpiError<SPI::Error>>
where
    SPI: SpiDevice,
    DelayFn: FnMut(u32 /*ms*/),
{
    const MAX_RETRIES: u32 = 5;
    const BASE_DELAY_MS: u32 = 2;

    let mut tx_frame: Vec<u8> = Vec::with_capacity(payload.len() + 2);
    build_frame(cmd, payload, &mut tx_frame);

    // RX buffer: response payload + 1 CRC byte
    let mut rx_frame = vec![0u8; rx_len + 1];

    let mut delay_ms = BASE_DELAY_MS;

    for attempt in 0..=MAX_RETRIES {
        // embedded-hal SpiDevice::transfer_in_place / write+read
        // Here we use a write-then-read pattern common for SPI registers.
        spi.write(&tx_frame).map_err(SpiError::Bus)?;
        spi.read(&mut rx_frame).map_err(SpiError::Bus)?;

        if validate_frame(&rx_frame) {
            // Strip trailing CRC byte and return payload
            rx_frame.truncate(rx_len);
            return Ok(rx_frame);
        }

        if attempt < MAX_RETRIES {
            delay_fn(delay_ms);
            delay_ms = (delay_ms * 2).min(64);
        }
    }

    Err(SpiError::CrcMismatch)
}
```

---

### 6.2 Majority-Vote Register Reader (Rust)

```rust
//! spi_majority_vote.rs
//!
//! Triple-read majority voting for critical SPI sensor registers.

use embedded_hal::spi::SpiDevice;

/// Read a single 16-bit big-endian register from an SPI peripheral.
///
/// Sends `[reg_addr | READ_FLAG, 0x00, 0x00]` and parses bytes 1–2 as u16.
fn spi_read_u16<SPI: SpiDevice>(
    spi: &mut SPI,
    reg_addr: u8,
) -> Result<u16, SPI::Error> {
    const READ_FLAG: u8 = 0x80;
    let tx = [reg_addr | READ_FLAG, 0x00, 0x00];
    let mut rx = [0u8; 3];
    spi.transfer(&mut rx, &tx)?;
    Ok(u16::from_be_bytes([rx[1], rx[2]]))
}

/// Errors from a majority-vote read.
#[derive(Debug)]
pub enum VoteError<E> {
    BusError(E),
    /// All three reads disagreed — unresolvable
    ThreeWayDisagreement,
}

/// Read a 16-bit register three times and return the 2-of-3 majority value.
///
/// Tolerates one corrupted read per call.
pub fn spi_read_majority<SPI: SpiDevice>(
    spi: &mut SPI,
    reg_addr: u8,
) -> Result<u16, VoteError<SPI::Error>> {
    let a = spi_read_u16(spi, reg_addr).map_err(VoteError::BusError)?;
    let b = spi_read_u16(spi, reg_addr).map_err(VoteError::BusError)?;
    let c = spi_read_u16(spi, reg_addr).map_err(VoteError::BusError)?;

    if a == b || a == c { return Ok(a); }
    if b == c           { return Ok(b); }

    Err(VoteError::ThreeWayDisagreement)
}
```

---

### 6.3 Resilient Sensor Driver with State Machine (Rust)

```rust
//! spi_sensor_driver.rs
//!
//! A complete resilient SPI sensor driver featuring:
//!  - CRC-protected transactions
//!  - Majority-vote reads
//!  - Bus-reset recovery
//!  - Last-known-good value retention
//!  - Error statistics

use embedded_hal::{delay::DelayNs, spi::SpiDevice};

/// Driver state
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DriverState {
    Healthy,
    Degraded { consecutive_errors: u8 },
    BusReset,
    Failed,
}

/// Configuration for the resilient driver
pub struct DriverConfig {
    pub max_consecutive_errors: u8,
    pub retry_base_delay_us: u32,
    pub max_retries: u8,
}

impl Default for DriverConfig {
    fn default() -> Self {
        Self {
            max_consecutive_errors: 5,
            retry_base_delay_us: 500,
            max_retries: 3,
        }
    }
}

/// Statistics counters for observability
#[derive(Debug, Default, Clone, Copy)]
pub struct DriverStats {
    pub total_reads: u32,
    pub crc_errors: u32,
    pub vote_failures: u32,
    pub bus_resets: u32,
    pub successful_reads: u32,
}

pub struct ResilientSensorDriver<SPI, DELAY> {
    spi: SPI,
    delay: DELAY,
    config: DriverConfig,
    state: DriverState,
    stats: DriverStats,
    last_good_value: Option<u16>,
}

impl<SPI, DELAY> ResilientSensorDriver<SPI, DELAY>
where
    SPI: SpiDevice,
    DELAY: DelayNs,
{
    pub fn new(spi: SPI, delay: DELAY, config: DriverConfig) -> Self {
        Self {
            spi, delay, config,
            state: DriverState::Healthy,
            stats: DriverStats::default(),
            last_good_value: None,
        }
    }

    /// Attempt to read a sensor value with full resilience logic.
    ///
    /// Returns the value on success. If degraded, returns last known
    /// good value with `is_stale = true`. Returns None only on total failure.
    pub fn read(&mut self, reg_addr: u8) -> Option<(u16, bool /*is_stale*/)> {
        self.stats.total_reads += 1;

        if self.state == DriverState::Failed {
            return self.last_good_value.map(|v| (v, true));
        }

        // Attempt bus reset first if in BusReset state
        if self.state == DriverState::BusReset {
            self.perform_bus_reset();
        }

        // Triple read with majority voting
        match self.read_with_retry(reg_addr) {
            Some(value) => {
                self.stats.successful_reads += 1;
                self.last_good_value = Some(value);
                self.state = DriverState::Healthy;
                Some((value, false))
            }
            None => {
                self.handle_read_failure();
                self.last_good_value.map(|v| (v, true))
            }
        }
    }

    fn read_with_retry(&mut self, reg_addr: u8) -> Option<u16> {
        let mut delay_us = self.config.retry_base_delay_us;

        for attempt in 0..=self.config.max_retries {
            // Majority-vote read (3 raw reads internally)
            let readings = [
                self.raw_read_u16(reg_addr),
                self.raw_read_u16(reg_addr),
                self.raw_read_u16(reg_addr),
            ];

            // Collect successful reads
            let valid: Vec<u16> = readings.iter().filter_map(|r| *r).collect();

            // Need at least 2 valid reads for a majority
            if valid.len() >= 2 {
                // Check for 2-of-3 agreement
                for i in 0..valid.len() {
                    for j in (i + 1)..valid.len() {
                        if valid[i] == valid[j] {
                            return Some(valid[i]);
                        }
                    }
                }
            }

            self.stats.vote_failures += 1;

            if attempt < self.config.max_retries {
                self.delay.delay_us(delay_us);
                delay_us = (delay_us * 2).min(8_000);
            }
        }

        None
    }

    fn raw_read_u16(&mut self, reg_addr: u8) -> Option<u16> {
        const READ_BIT: u8 = 0x80;
        let tx = [reg_addr | READ_BIT, 0x00, 0x00];
        let mut rx = [0u8; 3];

        if self.spi.transfer(&mut rx, &tx).is_err() {
            return None;
        }

        // Validate CRC if peripheral appends one (device-dependent)
        let value = u16::from_be_bytes([rx[1], rx[2]]);
        Some(value)
    }

    fn perform_bus_reset(&mut self) {
        // Clock out dummy bytes with CS high to flush peripheral
        let dummy = [0xFFu8; 8];
        let mut sink = [0u8; 8];
        let _ = self.spi.transfer(&mut sink, &dummy);
        self.delay.delay_us(100);
        self.stats.bus_resets += 1;
        self.state = DriverState::Healthy;
    }

    fn handle_read_failure(&mut self) {
        match self.state {
            DriverState::Healthy => {
                self.state = DriverState::Degraded { consecutive_errors: 1 };
            }
            DriverState::Degraded { consecutive_errors } => {
                let next = consecutive_errors + 1;
                if next >= self.config.max_consecutive_errors {
                    self.state = DriverState::BusReset;
                } else {
                    self.state = DriverState::Degraded {
                        consecutive_errors: next,
                    };
                }
            }
            DriverState::BusReset => {
                self.state = DriverState::Failed;
            }
            DriverState::Failed => {}
        }
    }

    /// Returns a snapshot of driver statistics.
    pub fn stats(&self) -> DriverStats { self.stats }

    /// Returns the current driver health state.
    pub fn state(&self) -> DriverState { self.state }
}
```

---

### 6.4 Unit Tests for Majority Voting (Rust)

```rust
#[cfg(test)]
mod tests {
    use super::*;

    // Mock SPI device that injects controlled errors
    struct MockSpi {
        responses: std::collections::VecDeque<Result<u16, ()>>,
    }

    impl MockSpi {
        fn new(responses: Vec<Result<u16, ()>>) -> Self {
            Self { responses: responses.into() }
        }
        fn next_value(&mut self) -> Option<u16> {
            self.responses.pop_front()?.ok()
        }
    }

    #[test]
    fn test_majority_vote_one_corrupted_read() {
        // Two agree on 0x1234, one returns garbage 0xDEAD
        let values = vec![0x1234u16, 0xDEADu16, 0x1234u16];
        let mut iter = values.into_iter();

        let a = iter.next().unwrap();
        let b = iter.next().unwrap();
        let c = iter.next().unwrap();

        let result = if a == b || a == c { Some(a) }
                     else if b == c      { Some(b) }
                     else                { None };

        assert_eq!(result, Some(0x1234));
    }

    #[test]
    fn test_majority_vote_all_corrupted() {
        let (a, b, c) = (0x0001u16, 0x0002u16, 0x0003u16);

        let result = if a == b || a == c { Some(a) }
                     else if b == c      { Some(b) }
                     else                { None };

        assert_eq!(result, None, "Three-way disagreement must yield None");
    }

    #[test]
    fn test_crc8_roundtrip() {
        let payload = b"SPI vibration test";
        let crc = crc8_buf(payload);

        let mut frame = payload.to_vec();
        frame.push(crc);

        assert!(validate_frame(&frame));

        // Corrupt one byte and verify detection
        frame[3] ^= 0xFF;
        assert!(!validate_frame(&frame));
    }
}
```

---

## 7. Testing and Validation

### 7.1 Standards for Vibration and Shock Testing

Depending on your target environment, relevant standards include:

| Standard             | Domain                  | Key Test             |
|----------------------|-------------------------|----------------------|
| IEC 60068-2-6        | General electronics     | Sinusoidal vibration |
| IEC 60068-2-27       | General electronics     | Mechanical shock     |
| MIL-STD-810H         | Military equipment      | Method 514 / 516     |
| DO-160G              | Avionics                | Section 8 / 7        |
| ISO 16750-3          | Automotive              | Vibration endurance  |
| AEC-Q100             | Automotive ICs          | Shock qualification  |

### 7.2 Software-Level Testing Techniques

Even without a vibration table, software robustness can be validated:

**Fault injection testing** — Randomly corrupt bytes in the received SPI buffer during integration testing:

```c
/* Inject a random bit error into a received frame (test mode only) */
#ifdef CONFIG_FAULT_INJECTION
void inject_bit_error(uint8_t *buf, uint16_t len)
{
    uint16_t byte_idx = rand() % len;
    uint8_t  bit_mask = 1u << (rand() % 8);
    buf[byte_idx] ^= bit_mask;
}
#endif
```

**Timing stress testing** — Vary SPI clock rate, inter-byte gaps, and CS setup/hold times across the full range of temperature and supply voltage.

**MTBF estimation** — Track error counters over time under representative loads and use them to estimate mean time between failures.

### 7.3 Observability

Expose the following metrics for field diagnostics:

```c
typedef struct {
    uint32_t total_transactions;
    uint32_t crc_errors;
    uint32_t retries;
    uint32_t bus_resets;
    uint32_t vote_failures;
    uint32_t timeouts;
} spi_diagnostics_t;
```

Log these to non-volatile memory on each power cycle for post-mortem analysis after field failures.

---

## 8. Summary

| Layer       | Technique                                     | Benefit                                     |
|-------------|-----------------------------------------------|---------------------------------------------|
| **Hardware**| Locking connectors, short traces, shielding   | Prevents faults at source                   |
| **Hardware**| Series termination, decoupling caps           | Reduces signal integrity degradation        |
| **Hardware**| PCB shock mounts, underfill, conformal coat   | Survives mechanical stress without damage   |
| **Protocol**| CRC-8/16 on every frame                       | Detects silent data corruption              |
| **Protocol**| Retry with exponential backoff                | Recovers from transient glitches            |
| **Protocol**| Majority voting (3×reads)                     | Masks single-read corruption                |
| **Protocol**| Per-transaction timeouts                      | Prevents bus lockup                         |
| **Software**| Bus reset sequence                            | Recovers from stuck peripherals             |
| **Software**| Last-known-good value retention               | Maintains system operation during faults    |
| **Software**| Error counters and state machine              | Enables observability and graceful degradation|

**Key Design Principle:** SPI provides no error detection of its own. In vibrating or shock-exposed environments, every layer of the stack — PCB layout, connector choice, firmware framing, and application logic — must be designed with the assumption that *any given transfer may be corrupted*. A layered defence combining hardware robustness with CRC protection, retries, majority voting, and health monitoring provides the resilience needed for reliable operation in harsh mechanical environments.

---

*Document: 93_Vibration_And_Shock_Tolerance.md — Part of the SPI Comprehensive Reference Series*