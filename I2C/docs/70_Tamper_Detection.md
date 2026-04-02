# 70. I²C Tamper Detection

**Structure overview:**

- **Threat Model** — a table mapping attacker types (passive eavesdropper, MitM, device impersonator, fault injector) to their likely techniques
- **Physical Attack Vectors** — six detailed attack categories: passive probing, bus injection, clock-stretching abuse, voltage glitching, address scanning, and device substitution
- **Electrical / Protocol / Software / Hardware Detection** — four layered detection tiers covering rise-time capacitance monitoring, ACK/NACK anomaly tracking, canary registers, timing fingerprinting, glitch detectors, and conductive mesh
- **C/C++ examples** (4 implementations):
  - Rise-time capacitance monitor using timer input capture (STM32 HAL)
  - Platform-agnostic protocol anomaly detector with error-rate windowing and scan detection
  - Templated timing fingerprinter with moving average and standard deviation
  - SCL clock-stretch watchdog using a one-shot hardware timer ISR
- **Rust examples** (3 implementations):
  - Type-safe tamper monitor using const-generic `ErrorRateTracker` and `KnownDevice` descriptors, fully `no_std`
  - `SecureI2c` wrapper around `embedded-hal` that automatically feeds timing and canary data into the monitor
  - Severity-classified tamper response handler with zeroization hook
- **Response Strategies** — a severity table from informational through permanent lockout, plus guidance on key zeroization, tamper counters, and evidence preservation
- **Summary** — consolidates the four detection layers and the design rationale

## Detecting Physical Attacks and Bus Manipulation Attempts

---

## Table of Contents

1. [Introduction](#introduction)
2. [Threat Model](#threat-model)
3. [Physical Attack Vectors](#physical-attack-vectors)
4. [Electrical Signal Anomaly Detection](#electrical-signal-anomaly-detection)
5. [Protocol-Level Tamper Detection](#protocol-level-tamper-detection)
6. [Software-Level Detection Strategies](#software-level-detection-strategies)
7. [Hardware-Assisted Detection](#hardware-assisted-detection)
8. [Implementation in C/C++](#implementation-in-cc)
9. [Implementation in Rust](#implementation-in-rust)
10. [Secure Response Strategies](#secure-response-strategies)
11. [Summary](#summary)

---

## Introduction

The I²C (Inter-Integrated Circuit) bus was originally designed for communication between chips on the same PCB in a trusted, controlled environment. As embedded systems increasingly appear in physically accessible or hostile deployments — industrial controllers, medical devices, smart meters, automotive ECUs, security tokens — the I²C bus becomes an attractive attack surface.

Tamper detection on I²C refers to the ability of a system to **recognize that the bus or a device attached to it is being physically probed, manipulated, replayed, or spoofed**, and to respond accordingly. This is distinct from logical security (encryption, authentication) and focuses on detecting the *physical act* of interference.

Tamper detection is a layered discipline. No single measure is sufficient; robust systems combine electrical monitoring, protocol analysis, firmware heuristics, and dedicated hardware to build a comprehensive detection posture.

---

## Threat Model

Understanding who the attacker is and what they want helps define what to detect:

| Threat Actor | Goal | Likely Attack |
|---|---|---|
| Passive eavesdropper | Read sensitive data (keys, PINs) | Probe wires, logic analyzer tap |
| Active bus manipulator | Inject commands, replay transactions | Man-in-the-middle, glitch injection |
| Device impersonator | Spoof a trusted slave device | Address spoofing, fake ACK |
| Physical extractor | Read memory from removed device | Cold-boot, direct flash reading |
| Fault injector | Cause misbehavior for side-channel | Voltage/clock glitching on I²C |

The I²C bus is particularly vulnerable because:

- It uses open-drain signaling — anyone can pull SDA or SCL low
- It has no built-in authentication or encryption
- Addresses are short (7-bit or 10-bit) and easily enumerated
- The multi-master protocol allows external devices to claim bus mastership

---

## Physical Attack Vectors

### 1. Passive Probing

An attacker attaches a logic analyzer or oscilloscope to the SDA/SCL lines. This is entirely invisible to the software unless physical tamper switches (mechanical or optical) detect the probe connection.

**Indicators:** No electrical indicator on the bus itself; requires physical enclosure protection or capacitance monitoring.

### 2. Bus Injection / Man-in-the-Middle (MitM)

The attacker cuts or taps the I²C lines and inserts a microcontroller that can selectively forward, modify, or block transactions. The attacker's device appears to be either the master or the slave depending on context.

**Indicators:**
- Timing anomalies (extra latency from the relay device)
- Spurious START/STOP conditions
- ACK received when no valid slave should be present
- NACK where a known-good slave normally ACKs

### 3. Clock Stretching Abuse

A rogue device or glitch tool holds SCL low indefinitely to stall the master, potentially causing timeout conditions that lead to system resets or insecure fallback states.

**Indicators:** SCL held low beyond a defined maximum clock-stretching timeout.

### 4. Voltage / Glitch Attacks

Brief voltage spikes or dips on VCC or the I²C lines are used to cause bit flips in registers or to skip instructions during a security check. These are typically very short (nanosecond to microsecond range).

**Indicators:** Noise on the bus power rail, unexpected bus errors, CRC mismatches on data that was previously reliable.

### 5. Address Scanning / Enumeration

An attacker connects to the bus and issues START + address + READ/WRITE cycles to discover what devices are present and their capabilities.

**Indicators:** Unexpected I²C transactions targeting addresses that the firmware has not initiated; unusual NACK storms.

### 6. Device Removal or Substitution

A trusted sensor is physically removed and replaced with a rogue device presenting the same I²C address. The replacement device responds correctly to simple queries but may return falsified data.

**Indicators:** Timing profile changes, unexpected capability responses, deviation from known device signature patterns.

---

## Electrical Signal Anomaly Detection

### Bus Capacitance Monitoring

Every device attached to the I²C bus adds capacitance to SDA and SCL. The rise time of the lines (from low to high, governed by the pull-up resistor and total capacitance) is therefore proportional to the number of attached devices. Adding a probe or tap device increases capacitance and lengthens rise times.

**Technique:** Measure the rise time of SDA/SCL using a timer capture input or an ADC. Establish a baseline at system initialization. Flag deviations beyond a tolerance band.

This is one of the few methods that can detect *purely passive* probing if the probe adds measurable capacitance.

### Voltage Level Monitoring

Valid I²C logic HIGH must be above 0.7 × VCC. Monitoring the idle-high voltage of SDA/SCL can reveal:
- Pull-up resistor tampering (replacing a 4.7 kΩ with a lower value to increase drive strength for MitM)
- Bus loading by an extra device

### SCL Timeout Detection

The I²C specification allows slave devices to stretch the clock (hold SCL low) but only for bounded periods. A hardware or software watchdog that asserts a tamper flag if SCL is held low beyond, say, 25 ms, catches clock-stretching attacks and bus stalls.

---

## Protocol-Level Tamper Detection

### Unexpected Address Activity

The firmware knows which I²C addresses it legitimately talks to. Any START condition followed by an address the firmware did not initiate — detectable on a multi-master bus or via a bus monitor peripheral — is suspicious.

### Transaction Sequencing Verification

For a known-good slave device, the sequence of register accesses follows a predictable pattern. Maintain a state machine of expected transactions. Deviation from the expected sequence (e.g., a READ of a high-security register without the preceding authentication register write) triggers a tamper event.

### Response Timing Profiling

Genuine devices respond with characteristic latencies — EEPROM devices have a well-defined write cycle time, sensors have a known conversion time. Building a timing fingerprint of each device and checking that responses arrive within expected windows catches device substitution attacks.

### Repeated or Replayed Transactions

On secure I²C buses, using challenge-response or monotonic sequence numbers in the data payload enables replay detection. If a transaction with a previously seen sequence number arrives, it is flagged as a replay.

### ACK/NACK Anomalies

- ACK when no slave is expected: Bus has a phantom device.
- NACK from a normally present slave: Device may have been removed, reset, or its address scrambled.
- Systematic NACK storm: Possible address enumeration attack in progress.

---

## Software-Level Detection Strategies

### Canary Registers

Dedicated read-only or write-protected registers on secure I²C peripherals (like secure elements) contain known constant values. Periodically reading and verifying these canary values detects device substitution (a replacement device is unlikely to know the expected canary value).

### Cryptographic Device Authentication

Use a secure element or a crypto-capable I²C device (e.g., Microchip ATECC608, NXP SE050) that supports challenge-response authentication. The host periodically issues a random nonce; the device must respond with an HMAC computed using a secret key. A device that cannot prove possession of the key is flagged.

### Error Rate Monitoring

Transient bus errors (arbitration loss, unexpected NACKs, overruns) are normal occasionally. A sudden spike in error rate is a strong indicator of active bus interference or glitch injection.

### Register State Verification

After writing to a configuration register on a peripheral, read it back and compare. A mismatch can indicate:
- Bit flip from a glitch attack
- A rogue MitM device that rewrote the value
- A device substitution that does not mirror the same register map

---

## Hardware-Assisted Detection

### Dedicated Tamper Detection Pins

Many microcontrollers (STM32, LPC, Kinetis) have dedicated TAMPER input pins connected to the Real-Time Clock (RTC) domain with battery backup. Connecting these to a physical enclosure switch, light sensor, or mesh grid allows hardware-level tamper detection independent of the I²C bus.

### Bus Monitor Peripherals

Some SoCs include a bus monitor mode where a peripheral can observe all I²C traffic without participating, generating interrupts or DMA events on every transaction. This allows a security co-processor to audit all bus traffic independently of the main application processor.

### Glitch Detectors

Dedicated voltage glitch detectors (present in secure microcontrollers like STM32L5, ATSAM, NXP LPC55S) monitor VCC and core voltage rails for spikes or dips that indicate fault injection. When triggered, they can assert a tamper flag, zeroize keys, or reset the device.

### Mesh / Shield Detection

High-security devices use a conductive mesh over the die or PCB area containing the I²C circuitry. The mesh has a continuously monitored resistance. Cutting or probing through the mesh breaks or shorts it, triggering a hardware tamper interrupt.

---

## Implementation in C/C++

### 8.1 Rise Time Capacitance Monitor (STM32 HAL)

```c
/*
 * I2C Tamper Detection - Bus Capacitance / Rise Time Monitor
 * Platform: STM32 with HAL, using TIM input capture on SDA line
 *
 * SDA is connected to a GPIO configured as TIM input capture.
 * We measure the time from SCL going high (trigger) to SDA reaching
 * the logic-high threshold, which is proportional to bus capacitance.
 */

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define RISE_TIME_BASELINE_NS   250U   /* Measured at production, typical for known good bus */
#define RISE_TIME_TOLERANCE_NS  80U    /* +/- tolerance before flagging tamper              */
#define RISE_TIME_SAMPLES       16U    /* Moving average window                             */

typedef struct {
    uint32_t samples[RISE_TIME_SAMPLES];
    uint8_t  index;
    uint32_t sum;
    uint32_t baseline_ns;
    bool     calibrated;
} RiseTimeMonitor_t;

static RiseTimeMonitor_t g_rise_monitor = {0};

/* Called from TIM input capture ISR with the captured timer count delta */
void RiseTameMonitor_Feed(RiseTimeMonitor_t *mon, uint32_t rise_time_ns)
{
    /* Update moving average */
    mon->sum -= mon->samples[mon->index];
    mon->samples[mon->index] = rise_time_ns;
    mon->sum += rise_time_ns;
    mon->index = (mon->index + 1U) % RISE_TIME_SAMPLES;

    if (!mon->calibrated) {
        /* During calibration phase, build baseline */
        mon->baseline_ns = mon->sum / RISE_TIME_SAMPLES;
        return;
    }

    uint32_t avg = mon->sum / RISE_TIME_SAMPLES;
    int32_t delta = (int32_t)avg - (int32_t)mon->baseline_ns;
    if (delta < 0) delta = -delta;

    if ((uint32_t)delta > RISE_TIME_TOLERANCE_NS) {
        /* Rise time has drifted — bus capacitance changed */
        Tamper_Trigger(TAMPER_SOURCE_BUS_CAPACITANCE, avg);
    }
}

/* Mark calibration complete after warm-up */
void RiseTimeMonitor_SetCalibrated(RiseTimeMonitor_t *mon)
{
    mon->baseline_ns = mon->sum / RISE_TIME_SAMPLES;
    mon->calibrated = true;
}
```

---

### 8.2 Protocol Anomaly Detector (Platform-Agnostic C)

```c
/*
 * I2C Protocol Tamper Detector
 * Tracks expected vs. actual transaction sequences and ACK/NACK patterns.
 * Integrates with any I2C driver that provides callback hooks.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define I2C_MAX_KNOWN_ADDRESSES   8U
#define ERROR_RATE_WINDOW         32U    /* transactions in sliding window */
#define ERROR_RATE_THRESHOLD      4U     /* max errors before tamper flag  */
#define SCAN_NACK_THRESHOLD       10U    /* consecutive NACKs = scan attack */

typedef enum {
    TAMPER_SOURCE_NONE             = 0x00,
    TAMPER_SOURCE_BUS_CAPACITANCE  = 0x01,
    TAMPER_SOURCE_PHANTOM_DEVICE   = 0x02,
    TAMPER_SOURCE_MISSING_DEVICE   = 0x04,
    TAMPER_SOURCE_HIGH_ERROR_RATE  = 0x08,
    TAMPER_SOURCE_ADDR_SCAN        = 0x10,
    TAMPER_SOURCE_TIMING_ANOMALY   = 0x20,
    TAMPER_SOURCE_SEQ_VIOLATION    = 0x40,
} TamperSource_t;

typedef struct {
    uint8_t  address;           /* 7-bit I2C address */
    bool     expected_present;  /* Should this device be on the bus? */
    bool     last_seen_ack;     /* Was this device ACKing last check? */
    uint32_t miss_count;        /* Consecutive missed ACKs */
} KnownDevice_t;

typedef struct {
    KnownDevice_t devices[I2C_MAX_KNOWN_ADDRESSES];
    uint8_t       device_count;

    /* Error rate tracking */
    uint8_t       error_window[ERROR_RATE_WINDOW];
    uint8_t       error_idx;
    uint8_t       error_sum;

    /* Address scan detection */
    uint8_t       consecutive_nacks;
    uint8_t       last_nack_addr;

    uint32_t      tamper_flags;
    void        (*on_tamper)(TamperSource_t source, uint32_t detail);
} I2CTamperCtx_t;

/* ------------------------------------------------------------------ */
/* Initialization                                                       */
/* ------------------------------------------------------------------ */

void I2CTamper_Init(I2CTamperCtx_t *ctx,
                    void (*tamper_cb)(TamperSource_t, uint32_t))
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->on_tamper = tamper_cb;
}

void I2CTamper_RegisterDevice(I2CTamperCtx_t *ctx,
                               uint8_t addr,
                               bool expected_present)
{
    if (ctx->device_count >= I2C_MAX_KNOWN_ADDRESSES) return;
    KnownDevice_t *dev = &ctx->devices[ctx->device_count++];
    dev->address          = addr;
    dev->expected_present = expected_present;
    dev->last_seen_ack    = expected_present;
    dev->miss_count       = 0U;
}

/* ------------------------------------------------------------------ */
/* Called after every I2C transaction attempt                           */
/* addr: 7-bit address, acked: true if slave ACKed, is_error: bus err  */
/* ------------------------------------------------------------------ */

void I2CTamper_RecordTransaction(I2CTamperCtx_t *ctx,
                                  uint8_t addr,
                                  bool acked,
                                  bool is_error)
{
    /* --- Error rate tracking --- */
    ctx->error_sum -= ctx->error_window[ctx->error_idx];
    ctx->error_window[ctx->error_idx] = is_error ? 1U : 0U;
    ctx->error_sum += ctx->error_window[ctx->error_idx];
    ctx->error_idx  = (ctx->error_idx + 1U) % ERROR_RATE_WINDOW;

    if (ctx->error_sum >= ERROR_RATE_THRESHOLD) {
        ctx->tamper_flags |= TAMPER_SOURCE_HIGH_ERROR_RATE;
        if (ctx->on_tamper) {
            ctx->on_tamper(TAMPER_SOURCE_HIGH_ERROR_RATE, ctx->error_sum);
        }
    }

    /* --- Address scan detection --- */
    if (!acked) {
        if (addr == ctx->last_nack_addr + 1U) {
            ctx->consecutive_nacks++;
        } else {
            ctx->consecutive_nacks = 1U;
        }
        ctx->last_nack_addr = addr;

        if (ctx->consecutive_nacks >= SCAN_NACK_THRESHOLD) {
            ctx->tamper_flags |= TAMPER_SOURCE_ADDR_SCAN;
            if (ctx->on_tamper) {
                ctx->on_tamper(TAMPER_SOURCE_ADDR_SCAN, addr);
            }
        }
    } else {
        ctx->consecutive_nacks = 0U;
    }

    /* --- Phantom / Missing device detection --- */
    bool addr_is_known = false;
    for (uint8_t i = 0U; i < ctx->device_count; i++) {
        KnownDevice_t *dev = &ctx->devices[i];
        if (dev->address != addr) continue;
        addr_is_known = true;

        if (acked && !dev->expected_present) {
            /* Unexpected ACK from an address we don't own */
            ctx->tamper_flags |= TAMPER_SOURCE_PHANTOM_DEVICE;
            if (ctx->on_tamper) {
                ctx->on_tamper(TAMPER_SOURCE_PHANTOM_DEVICE, addr);
            }
        } else if (!acked && dev->expected_present) {
            dev->miss_count++;
            if (dev->miss_count >= 3U) {
                /* Device has disappeared */
                ctx->tamper_flags |= TAMPER_SOURCE_MISSING_DEVICE;
                if (ctx->on_tamper) {
                    ctx->on_tamper(TAMPER_SOURCE_MISSING_DEVICE, addr);
                }
            }
        } else {
            dev->miss_count = 0U;
        }
        break;
    }

    if (!addr_is_known && acked) {
        /* ACK from an address we never registered */
        ctx->tamper_flags |= TAMPER_SOURCE_PHANTOM_DEVICE;
        if (ctx->on_tamper) {
            ctx->on_tamper(TAMPER_SOURCE_PHANTOM_DEVICE, addr);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Called by the host after reading a canary register from a device     */
/* ------------------------------------------------------------------ */

void I2CTamper_VerifyCanary(I2CTamperCtx_t *ctx,
                             uint8_t addr,
                             uint8_t expected,
                             uint8_t actual)
{
    if (expected != actual) {
        ctx->tamper_flags |= TAMPER_SOURCE_PHANTOM_DEVICE;
        if (ctx->on_tamper) {
            ctx->on_tamper(TAMPER_SOURCE_PHANTOM_DEVICE, addr);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Usage example                                                         */
/* ------------------------------------------------------------------ */

static void my_tamper_handler(TamperSource_t source, uint32_t detail)
{
    /* Log, alert, zeroize keys, reset system, etc. */
    (void)source;
    (void)detail;
    /* SystemZeroize(); */
    /* NVIC_SystemReset(); */
}

void Application_Init(void)
{
    static I2CTamperCtx_t tamper_ctx;

    I2CTamper_Init(&tamper_ctx, my_tamper_handler);

    /* Register all devices that should be present on the bus */
    I2CTamper_RegisterDevice(&tamper_ctx, 0x48, true);  /* Temperature sensor */
    I2CTamper_RegisterDevice(&tamper_ctx, 0x57, true);  /* EEPROM             */
    I2CTamper_RegisterDevice(&tamper_ctx, 0x60, true);  /* Secure element     */
}
```

---

### 8.3 Transaction Timing Fingerprint (C++)

```cpp
/*
 * I2C Device Timing Fingerprinter
 * Builds a statistical profile of response latency for each device.
 * Outlier latencies indicate device substitution or MitM relay.
 */

#include <cstdint>
#include <cmath>
#include <array>
#include <algorithm>

template<uint8_t N>
class TimingFingerprint {
public:
    explicit TimingFingerprint(uint32_t expected_us,
                                uint32_t tolerance_us)
        : m_expected_us(expected_us),
          m_tolerance_us(tolerance_us),
          m_index(0U),
          m_count(0U)
    {
        m_samples.fill(expected_us);
    }

    /* Feed a measured round-trip latency in microseconds */
    bool Feed(uint32_t measured_us)
    {
        m_samples[m_index] = measured_us;
        m_index = (m_index + 1U) % N;
        if (m_count < N) m_count++;

        uint32_t avg = Average();
        int32_t delta = static_cast<int32_t>(avg)
                      - static_cast<int32_t>(m_expected_us);
        if (delta < 0) delta = -delta;

        /* Flag if average has drifted beyond tolerance */
        return static_cast<uint32_t>(delta) > m_tolerance_us;
    }

    uint32_t Average() const
    {
        if (m_count == 0U) return m_expected_us;
        uint32_t sum = 0U;
        for (uint8_t i = 0U; i < m_count; i++) sum += m_samples[i];
        return sum / m_count;
    }

    uint32_t StandardDeviation() const
    {
        if (m_count < 2U) return 0U;
        uint32_t avg = Average();
        uint64_t var = 0U;
        for (uint8_t i = 0U; i < m_count; i++) {
            int64_t diff = static_cast<int64_t>(m_samples[i])
                         - static_cast<int64_t>(avg);
            var += static_cast<uint64_t>(diff * diff);
        }
        return static_cast<uint32_t>(
            static_cast<uint32_t>(std::sqrt(static_cast<double>(var / m_count)))
        );
    }

private:
    std::array<uint32_t, N> m_samples;
    uint32_t m_expected_us;
    uint32_t m_tolerance_us;
    uint8_t  m_index;
    uint8_t  m_count;
};

/* Concrete fingerprinter for a device expected to respond in 120 µs ±40 µs */
static TimingFingerprint<32> g_sensor_fp(120U, 40U);

bool CheckSensorTiming(uint32_t measured_us)
{
    return g_sensor_fp.Feed(measured_us); /* returns true if anomaly detected */
}
```

---

### 8.4 SCL Timeout Watchdog (C, RTOS-style)

```c
/*
 * SCL Clock-Stretch Watchdog
 * Monitors for SCL being held low beyond the allowed maximum.
 * Implemented using a hardware timer interrupt.
 */

#include <stdint.h>
#include <stdbool.h>

#define SCL_STRETCH_TIMEOUT_MS  25U   /* Max allowed clock stretch per I2C spec guidance */

static volatile bool g_scl_watchdog_active = false;
static volatile bool g_scl_tamper_detected = false;

/* Called by I2C peripheral driver when a transaction begins */
void SCLWatchdog_Start(void)
{
    g_scl_tamper_detected = false;
    g_scl_watchdog_active = true;
    Timer_StartOneShot(SCL_STRETCH_TIMEOUT_MS);   /* Platform-specific timer API */
}

/* Called by I2C peripheral driver when transaction completes normally */
void SCLWatchdog_Stop(void)
{
    g_scl_watchdog_active = false;
    Timer_Stop();
}

/* Timer ISR — SCL held low too long */
void SCLWatchdog_TimeoutISR(void)
{
    if (g_scl_watchdog_active) {
        g_scl_tamper_detected = true;
        g_scl_watchdog_active = false;
        /* Force-release SCL by resetting the I2C peripheral */
        I2C_ForceReset();                          /* Platform-specific reset   */
        Tamper_Trigger(TAMPER_SOURCE_TIMING_ANOMALY, SCL_STRETCH_TIMEOUT_MS);
    }
}

bool SCLWatchdog_TamperDetected(void)
{
    return g_scl_tamper_detected;
}
```

---

## Implementation in Rust

### 9.1 I2C Tamper Monitor (Rust, embedded-hal)

```rust
//! I2C Tamper Detection for embedded Rust
//!
//! Implements protocol anomaly detection and device fingerprinting
//! using the embedded-hal I2C trait.

#![no_std]

use core::fmt;

// ---------------------------------------------------------------------------
// Tamper event type
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TamperEvent {
    PhantomDevice { address: u8 },
    MissingDevice  { address: u8 },
    HighErrorRate  { errors_in_window: u8 },
    AddressScan    { last_address: u8 },
    TimingAnomaly  { address: u8, measured_us: u32 },
    CanaryMismatch { address: u8, expected: u8, actual: u8 },
    SequenceViolation { address: u8 },
}

// ---------------------------------------------------------------------------
// Known device descriptor
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct KnownDevice {
    pub address:          u8,
    pub expected_present: bool,
    pub miss_count:       u8,
    pub canary_register:  Option<u8>,   /* register address holding canary */
    pub canary_value:     Option<u8>,   /* expected value                  */
    pub timing_min_us:    u32,
    pub timing_max_us:    u32,
}

impl KnownDevice {
    pub fn new(address: u8, timing_min_us: u32, timing_max_us: u32) -> Self {
        Self {
            address,
            expected_present: true,
            miss_count: 0,
            canary_register: None,
            canary_value: None,
            timing_min_us,
            timing_max_us,
        }
    }

    pub fn with_canary(mut self, register: u8, value: u8) -> Self {
        self.canary_register = Some(register);
        self.canary_value    = Some(value);
        self
    }
}

// ---------------------------------------------------------------------------
// Error rate tracker (const-generic window size)
// ---------------------------------------------------------------------------

pub struct ErrorRateTracker<const W: usize> {
    window:  [bool; W],
    index:   usize,
    sum:     usize,
    threshold: usize,
}

impl<const W: usize> ErrorRateTracker<W> {
    pub const fn new(threshold: usize) -> Self {
        Self {
            window:    [false; W],
            index:     0,
            sum:       0,
            threshold,
        }
    }

    /// Returns true if the error rate has exceeded the threshold.
    pub fn record(&mut self, is_error: bool) -> bool {
        self.sum -= self.window[self.index] as usize;
        self.window[self.index] = is_error;
        self.sum += is_error as usize;
        self.index = (self.index + 1) % W;
        self.sum >= self.threshold
    }
}

// ---------------------------------------------------------------------------
// Main tamper monitor
// ---------------------------------------------------------------------------

pub struct I2CTamperMonitor<const MAX_DEVICES: usize, const ERR_WINDOW: usize> {
    devices:           [Option<KnownDevice>; MAX_DEVICES],
    error_tracker:     ErrorRateTracker<ERR_WINDOW>,
    consecutive_nacks: u8,
    last_nack_addr:    u8,
    scan_threshold:    u8,
}

impl<const MAX_DEVICES: usize, const ERR_WINDOW: usize>
    I2CTamperMonitor<MAX_DEVICES, ERR_WINDOW>
{
    pub const fn new(scan_threshold: u8, error_threshold: usize) -> Self {
        Self {
            devices:           [const { None }; MAX_DEVICES],
            error_tracker:     ErrorRateTracker::new(error_threshold),
            consecutive_nacks: 0,
            last_nack_addr:    0xFF,
            scan_threshold,
        }
    }

    pub fn register_device(&mut self, device: KnownDevice) -> Result<(), ()> {
        for slot in self.devices.iter_mut() {
            if slot.is_none() {
                *slot = Some(device);
                return Ok(());
            }
        }
        Err(())  /* No free slot */
    }

    /// Record an I2C transaction outcome.
    /// Returns a tamper event if one is detected, otherwise None.
    pub fn record_transaction(
        &mut self,
        address: u8,
        acked: bool,
        is_error: bool,
    ) -> Option<TamperEvent> {
        // --- Error rate ---
        if self.error_tracker.record(is_error) {
            return Some(TamperEvent::HighErrorRate {
                errors_in_window: self.error_tracker.sum as u8,
            });
        }

        // --- Address scan detection ---
        if !acked {
            if address == self.last_nack_addr.wrapping_add(1) {
                self.consecutive_nacks = self.consecutive_nacks.saturating_add(1);
            } else {
                self.consecutive_nacks = 1;
            }
            self.last_nack_addr = address;
            if self.consecutive_nacks >= self.scan_threshold {
                return Some(TamperEvent::AddressScan { last_address: address });
            }
        } else {
            self.consecutive_nacks = 0;
        }

        // --- Known device checks ---
        let known = self.devices.iter_mut().find_map(|slot| {
            slot.as_mut().filter(|d| d.address == address)
        });

        match known {
            Some(dev) => {
                if acked && !dev.expected_present {
                    return Some(TamperEvent::PhantomDevice { address });
                }
                if !acked && dev.expected_present {
                    dev.miss_count = dev.miss_count.saturating_add(1);
                    if dev.miss_count >= 3 {
                        return Some(TamperEvent::MissingDevice { address });
                    }
                } else {
                    dev.miss_count = 0;
                }
            }
            None if acked => {
                /* ACK from unregistered address */
                return Some(TamperEvent::PhantomDevice { address });
            }
            _ => {}
        }

        None
    }

    /// Verify that a canary register read matches the expected value.
    pub fn verify_canary(
        &self,
        address: u8,
        actual: u8,
    ) -> Option<TamperEvent> {
        let dev = self.devices.iter().find_map(|slot| {
            slot.as_ref().filter(|d| d.address == address)
        })?;

        let expected = dev.canary_value?;
        if actual != expected {
            Some(TamperEvent::CanaryMismatch { address, expected, actual })
        } else {
            None
        }
    }

    /// Verify a response latency against the device's timing fingerprint.
    pub fn verify_timing(
        &self,
        address: u8,
        measured_us: u32,
    ) -> Option<TamperEvent> {
        let dev = self.devices.iter().find_map(|slot| {
            slot.as_ref().filter(|d| d.address == address)
        })?;

        if measured_us < dev.timing_min_us || measured_us > dev.timing_max_us {
            Some(TamperEvent::TimingAnomaly { address, measured_us })
        } else {
            None
        }
    }
}
```

---

### 9.2 Secure I2C Transaction Wrapper (Rust)

```rust
//! Secure I2C transaction wrapper with tamper detection hooks.
//! Wraps embedded-hal I2C operations and feeds results into the tamper monitor.

use embedded_hal::i2c::{I2c, ErrorKind};

pub struct SecureI2c<I2C, const D: usize, const W: usize>
where
    I2C: I2c,
{
    inner:   I2C,
    monitor: I2CTamperMonitor<D, W>,
    get_time_us: fn() -> u32,   /* Platform-specific monotonic timestamp */
}

impl<I2C, const D: usize, const W: usize> SecureI2c<I2C, D, W>
where
    I2C: I2c,
{
    pub fn new(
        inner: I2C,
        monitor: I2CTamperMonitor<D, W>,
        get_time_us: fn() -> u32,
    ) -> Self {
        Self { inner, monitor, get_time_us }
    }

    /// Perform a write, returning the tamper event (if any) alongside the result.
    pub fn write_checked(
        &mut self,
        address: u8,
        bytes: &[u8],
    ) -> (Result<(), I2C::Error>, Option<TamperEvent>) {
        let t0 = (self.get_time_us)();
        let result = self.inner.write(address, bytes);
        let elapsed = (self.get_time_us)().wrapping_sub(t0);

        let acked    = result.is_ok();
        let is_error = result.as_ref()
            .err()
            .map(|e| !matches!(e.kind(), ErrorKind::NoAcknowledge(_)))
            .unwrap_or(false);

        let mut event = self.monitor.record_transaction(address, acked, is_error);

        if event.is_none() {
            event = self.monitor.verify_timing(address, elapsed);
        }

        (result, event)
    }

    /// Perform a read with a canary verification on the returned data.
    /// `canary_byte_index`: which byte in the response to treat as the canary.
    pub fn read_and_verify_canary(
        &mut self,
        address: u8,
        register: u8,
        buf: &mut [u8],
        canary_index: usize,
    ) -> (Result<(), I2C::Error>, Option<TamperEvent>) {
        let t0 = (self.get_time_us)();
        let result = self.inner.write_read(address, &[register], buf);
        let elapsed = (self.get_time_us)().wrapping_sub(t0);

        let acked    = result.is_ok();
        let is_error = result.as_ref()
            .err()
            .map(|e| !matches!(e.kind(), ErrorKind::NoAcknowledge(_)))
            .unwrap_or(false);

        let mut event = self.monitor.record_transaction(address, acked, is_error);

        if event.is_none() {
            event = self.monitor.verify_timing(address, elapsed);
        }

        if event.is_none() && result.is_ok() {
            if let Some(&actual) = buf.get(canary_index) {
                event = self.monitor.verify_canary(address, actual);
            }
        }

        (result, event)
    }
}
```

---

### 9.3 Responding to Tamper Events (Rust)

```rust
//! Tamper response handler — demonstrates layered escalation.

pub enum TamperSeverity {
    Low,     /* Log and continue */
    Medium,  /* Alert and increase monitoring frequency */
    High,    /* Zeroize sensitive material, enter lockout */
}

pub fn classify_tamper(event: &TamperEvent) -> TamperSeverity {
    match event {
        TamperEvent::AddressScan { .. }    => TamperSeverity::Low,
        TamperEvent::HighErrorRate { .. }  => TamperSeverity::Medium,
        TamperEvent::TimingAnomaly { .. }  => TamperSeverity::Medium,
        TamperEvent::MissingDevice { .. }  => TamperSeverity::Medium,
        TamperEvent::PhantomDevice { .. }  => TamperSeverity::High,
        TamperEvent::CanaryMismatch { .. } => TamperSeverity::High,
        TamperEvent::SequenceViolation {..}=> TamperSeverity::High,
    }
}

pub fn handle_tamper<K>(event: TamperEvent, zeroize_keys: impl FnOnce())
{
    match classify_tamper(&event) {
        TamperSeverity::Low => {
            /* audit_log::record(&event); */
        }
        TamperSeverity::Medium => {
            /* audit_log::record(&event); */
            /* alert::send_to_host(&event); */
        }
        TamperSeverity::High => {
            zeroize_keys();
            /* system::lockout(Duration::from_secs(300)); */
            /* system::reset(); */
        }
    }
}
```

---

## Secure Response Strategies

When a tamper event is detected, the system must respond appropriately. The correct response depends on the classification of the event and the security requirements of the application.

### Layered Response Model

| Severity | Response Actions |
|---|---|
| Informational | Increment tamper counter in non-volatile memory, log event with timestamp |
| Low | Increase monitoring frequency, alert host system or operator |
| Medium | Suspend sensitive operations, require re-authentication before resuming |
| High | Zeroize cryptographic keys and secrets, enter tamper lockout mode, require physical reset or factory service |
| Critical | Destroy all secrets (one-way), permanent lockout, require hardware replacement |

### Key Zeroization

When a high-severity tamper is detected, any keys, PINs, or sensitive data in RAM or accessible registers must be overwritten before the system can be further probed. In C, use volatile memset or a dedicated secure_memzero function. In Rust, use the `zeroize` crate to ensure the compiler does not optimize away the wipe.

### Tamper Counters and Lockout

Maintain a tamper event counter in battery-backed SRAM or a secure element's monotonic counter. Implement a lockout policy:
- After N tamper events, increase the delay before the next authentication attempt (exponential back-off).
- After M events, enter permanent lockout requiring physical service intervention.

### Evidence Preservation

Before zeroizing, log the tamper event type, timestamp, bus address, and any relevant bus state to a write-once log in secure storage. This provides forensic evidence for post-incident analysis without leaving sensitive material exposed.

---

## Summary

I²C tamper detection is a multi-layered discipline that addresses the fundamental openness of the I²C bus protocol. Because I²C provides no native authentication, encryption, or physical security, systems deployed in hostile or physically accessible environments must implement their own protection.

**Physical layer detection** focuses on measurable bus properties — rise time, capacitance, voltage levels, and SCL timing — to detect the addition of probing hardware or MitM relays. These techniques are passive from the protocol perspective and can catch attackers that make no protocol-level mistakes.

**Protocol layer detection** monitors the pattern of transactions, ACK/NACK responses, address activity, and error rates. A firmware-implemented state machine that models expected device behavior can identify device substitution, address scanning, and transaction replay.

**Application layer detection** uses cryptographic canary values, challenge-response authentication with secure elements, and data consistency checks to provide mathematically-grounded assurance that devices are genuine and untampered.

**Hardware assistance** from dedicated tamper pins, glitch detectors, bus monitor peripherals, and physical mesh shields provides independent detection channels that remain active even if the main application processor is compromised.

The C/C++ examples demonstrated practical implementations of a rise time monitor, a protocol anomaly tracker with error rate and scan detection, a timing fingerprinter using a moving average, and an SCL watchdog. The Rust examples showed how to express these concepts using type safety, const generics for zero-allocation data structures, and embedded-hal trait integration — enabling the same tamper logic to be reused across any microcontroller target supported by the Rust embedded ecosystem.

Effective tamper detection does not prevent a determined, well-resourced attacker from eventually reading a bus, but it dramatically increases the cost and detectability of attacks, provides a window in which to destroy sensitive material before it can be extracted, and creates an auditable record of intrusion attempts.

---

*Document: I²C Topic 70 — Tamper Detection*
*Scope: Detecting physical attacks and bus manipulation attempts*
*Languages: C/C++, Rust*