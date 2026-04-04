# 88. Anti-tampering Measures

- **Threat model table** — classifies attackers from logic-analyser probing up to FIB lab attacks, with risk levels
- **Attack vectors** — passive sniffing, active injection, clock/power glitching, CS# manipulation, MISO substitution
- **C/C++ code** — four complete modules: ISR-driven timing anomaly detector (DWT cycle counter), HMAC-SHA256 authenticated transaction wrapper (mbedTLS), glitch pulse-width detector, and a lock-free C++ CS# monitor using `std::atomic`
- **Rust code** — three modules: `TimingGuard<SPI>` wrapping `embedded-hal 1.x`, `AuthenticatedSpi<SPI>` with HMAC + replay counter, and a typed `AnomalyFsm` with unit tests
- **Response table** — graduated lockout from "log" through "zeroize all keys + halt"
- **Hardware techniques table** — tamper meshes, potting, secure elements, signal conditioning
- **Design checklist** — 20-point review covering physical layer, detection firmware, auth, and operations

## Detecting Physical Attacks on SPI Bus Lines and Devices

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Threat Model](#2-threat-model)
3. [Attack Vectors on SPI Buses](#3-attack-vectors-on-spi-buses)
4. [Detection Techniques](#4-detection-techniques)
   - 4.1 [Signal Integrity Monitoring](#41-signal-integrity-monitoring)
   - 4.2 [Timing Anomaly Detection](#42-timing-anomaly-detection)
   - 4.3 [Cryptographic Transaction Verification](#43-cryptographic-transaction-verification)
   - 4.4 [Physical Layer Sensing](#44-physical-layer-sensing)
   - 4.5 [Behavioral Fingerprinting](#45-behavioral-fingerprinting)
5. [C/C++ Implementation](#5-cc-implementation)
   - 5.1 [Timing Anomaly Detector](#51-timing-anomaly-detector)
   - 5.2 [HMAC Transaction Verification](#52-hmac-transaction-verification)
   - 5.3 [Glitch Detection via GPIO Interrupt](#53-glitch-detection-via-gpio-interrupt)
   - 5.4 [CS# Line Monitor](#54-cs-line-monitor)
6. [Rust Implementation](#6-rust-implementation)
   - 6.1 [Timing Guard with embedded-hal](#61-timing-guard-with-embedded-hal)
   - 6.2 [Authenticated SPI Wrapper](#62-authenticated-spi-wrapper)
   - 6.3 [Anomaly State Machine](#63-anomaly-state-machine)
7. [Response and Countermeasures](#7-response-and-countermeasures)
8. [Hardware-Assisted Protections](#8-hardware-assisted-protections)
9. [Design Checklist](#9-design-checklist)
10. [Summary](#10-summary)

---

## 1. Introduction

The Serial Peripheral Interface (SPI) bus — consisting of **SCLK**, **MOSI**, **MISO**, and **CS#** lines — is ubiquitous in embedded systems: flash memories, secure elements, ADCs, display controllers, and cryptographic co-processors all communicate over it. This wide deployment makes SPI an attractive physical attack surface.

**Anti-tampering measures** are the hardware and software mechanisms that detect, resist, and respond to unauthorized physical manipulation of SPI bus lines and devices. Without them, an attacker with physical access can:

- Passively eavesdrop on plaintext device communication
- Inject malicious transactions (read/write flash, bypass authentication)
- Glitch the clock or power supply to corrupt execution
- Desolder and clone SPI flash containing firmware or keys
- Perform man-in-the-middle (MitM) attacks by interposing a rogue device

Anti-tampering is a required element of any security-critical product: IoT devices, payment terminals, medical instruments, and industrial controllers.

---

## 2. Threat Model

Before choosing countermeasures, the threat model must be clearly defined:

| Attacker Capability | Example Attack | Risk Level |
|---|---|---|
| Oscilloscope / logic analyser on exposed PCB | Passive sniffing of SPI traffic | Medium |
| Clip-on probes, SOIC clip | Live bus interception without desoldering | Medium |
| Glitch injector (voltage/clock) | Fault injection during authenticated boot | High |
| Rework station + SPI programmer | SPI flash extraction and cloning | High |
| FIB (Focused Ion Beam) lab equipment | Cutting/bridging PCB traces | Critical |
| Decapsulation + microprobing | Direct contact with die bond wires | Critical |

The software techniques in this document address the **Medium** and some **High** risk levels. Critical-level attacks require hardware countermeasures (tamper meshes, potting compound, secure elements).

---

## 3. Attack Vectors on SPI Buses

### 3.1 Passive Eavesdropping

Logic analysers or oscilloscopes placed on SPI traces capture all transactions. If the traffic is unencrypted (the default), secrets are immediately exposed.

### 3.2 Active Injection / Replay

An attacker splices into the MOSI or CS# line to inject crafted commands — for example, issuing a "write enable" followed by a sector erase to an SPI flash, or replaying a valid authentication token.

### 3.3 Clock Glitching

By injecting rogue pulses onto SCLK, the attacker can skip instructions in the target MCU or cause bit-slipping in SPI register shifts, leading to incorrect data interpretation.

### 3.4 Power Glitching

Momentary voltage droops on Vcc can cause the SPI controller or connected device to misinterpret a command byte, for example changing a "read" into a "write enable".

### 3.5 CS# Manipulation

Forcing CS# low (or high at unexpected times) can abort or concatenate transactions in ways the firmware did not anticipate, breaking authentication state machines.

### 3.6 MISO Substitution

In MitM scenarios the attacker intercepts the return path (MISO), substituting fabricated read data — for example, returning a constant "0x00" for every memory read to simulate a zeroed device, or feeding a pre-captured authentication response.

---

## 4. Detection Techniques

### 4.1 Signal Integrity Monitoring

**Concept:** Measure timing and voltage characteristics of SPI transactions at runtime and compare them to a learned baseline. Anomalies indicate probing, interposition, or glitching.

Key metrics to monitor:
- Transaction duration (SCLK period constancy)
- Inter-transaction idle times
- CS# assertion-to-first-clock setup time
- CS# de-assertion-to-last-clock hold time

### 4.2 Timing Anomaly Detection

**Concept:** Record hardware timestamps at CS# assertion, first SCLK edge, and CS# de-assertion using a free-running hardware timer. Compare intervals against acceptable windows derived from the configured SPI clock rate.

A glitch or injected transaction will typically appear as an anomalously short or early/late transition.

### 4.3 Cryptographic Transaction Verification

**Concept:** All SPI transactions with a security-critical device (secure element, encrypted flash) are wrapped with a **message authentication code (MAC)** — typically HMAC-SHA256. The device refuses commands whose MAC does not match. This defeats:
- Replay attacks (include a nonce or counter in the MAC input)
- Injection attacks (attacker cannot forge valid MACs without the key)

### 4.4 Physical Layer Sensing

**Concept:** Use dedicated sensing circuitry or additional MCU GPIO inputs to monitor for unexpected electrical events:

- **Capacitance change** on SPI traces (a clip-on probe adds ~2–10 pF; detectable with careful RC measurement)
- **Stub reflection** on long SPI lines (signal integrity degradation)
- **Differential sensing** if SPI is routed through a tamper-detection mesh

### 4.5 Behavioral Fingerprinting

**Concept:** SPI devices have characteristic response latencies (e.g., an SPI flash's "read status register" command takes a deterministic number of clock cycles before MISO goes low). A rogue interposer device will differ measurably from the genuine device's timing profile.

---

## 5. C/C++ Implementation

### 5.1 Timing Anomaly Detector

```c
/**
 * spi_tamper_timing.c
 *
 * Detects SPI transaction timing anomalies using a hardware timer.
 * Assumes an ARM Cortex-M device with DWT cycle counter and
 * GPIO interrupts on CS# and SCLK.
 *
 * Build: arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2
 */

#include <stdint.h>
#include <stdbool.h>
#include "platform.h"   /* Vendor HAL for GPIO, DWT, NVIC */

/* ── Configuration ──────────────────────────────────────────── */
#define SPI_CLOCK_HZ          8000000UL   /* Configured SPI bus speed    */
#define CPU_CLOCK_HZ         168000000UL  /* Core clock (Cortex-M4 example) */
#define CYCLES_PER_SPI_BIT   (CPU_CLOCK_HZ / SPI_CLOCK_HZ)

/* Acceptable window: ±20% around nominal for each metric.
   Tighten in production after characterisation. */
#define TIMING_TOLERANCE_PCT  20

#define CS_ASSERT_SETUP_MIN_CYCLES   \
    ((CYCLES_PER_SPI_BIT / 2) * (100 - TIMING_TOLERANCE_PCT) / 100)
#define CS_ASSERT_SETUP_MAX_CYCLES   \
    ((CYCLES_PER_SPI_BIT * 4) * (100 + TIMING_TOLERANCE_PCT) / 100)

#define MAX_TRANSACTION_BYTES  256

/* ── Tamper event log ────────────────────────────────────────── */
typedef enum {
    TAMPER_NONE            = 0,
    TAMPER_EARLY_CLOCK     = (1 << 0),   /* Clock before CS# settled    */
    TAMPER_LATE_DEASSERT   = (1 << 1),   /* CS# held abnormally long    */
    TAMPER_SPURIOUS_CS     = (1 << 2),   /* CS# glitch < min xact len   */
    TAMPER_CLOCK_GLITCH    = (1 << 3),   /* Unexpected SCLK pulse       */
    TAMPER_EXCESS_BYTES    = (1 << 4),   /* More bytes than expected    */
} tamper_flag_t;

typedef struct {
    uint32_t     timestamp_cycles; /* DWT->CYCCNT at detection time     */
    tamper_flag_t flags;
    uint8_t      byte_count;       /* Bytes seen in offending xact      */
} tamper_event_t;

#define TAMPER_LOG_SIZE 16
static volatile tamper_event_t tamper_log[TAMPER_LOG_SIZE];
static volatile uint32_t       tamper_log_head = 0;
static volatile uint32_t       tamper_flags_accumulated = 0;

/* ── Internal state ──────────────────────────────────────────── */
static volatile uint32_t cs_assert_cycle   = 0;
static volatile uint32_t first_clock_cycle = 0;
static volatile uint32_t last_clock_cycle  = 0;
static volatile uint32_t byte_count        = 0;
static volatile bool     in_transaction    = false;

/* ── DWT cycle counter helpers ───────────────────────────────── */
static inline uint32_t dwt_get_cycles(void)
{
    return DWT->CYCCNT;
}

static inline void dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT       = 0;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
}

/* ── Tamper logging ──────────────────────────────────────────── */
static void log_tamper(tamper_flag_t flags, uint8_t bytes)
{
    uint32_t idx = tamper_log_head & (TAMPER_LOG_SIZE - 1);
    tamper_log[idx].timestamp_cycles = dwt_get_cycles();
    tamper_log[idx].flags            = flags;
    tamper_log[idx].byte_count       = bytes;
    tamper_log_head++;

    tamper_flags_accumulated |= (uint32_t)flags;

    /* Trigger a tamper response — application-defined */
    spi_tamper_response_callback(flags);
}

/* ── GPIO interrupt: CS# line ────────────────────────────────── */
void CS_GPIO_IRQHandler(void)
{
    uint32_t now = dwt_get_cycles();

    if (gpio_read(SPI_CS_PIN) == GPIO_LOW) {
        /* ── CS# asserted (transaction start) ── */
        if (in_transaction) {
            /* CS# asserted while already low: spurious glitch */
            log_tamper(TAMPER_SPURIOUS_CS, (uint8_t)byte_count);
        }
        cs_assert_cycle   = now;
        first_clock_cycle = 0;
        byte_count        = 0;
        in_transaction    = true;

    } else {
        /* ── CS# de-asserted (transaction end) ── */
        if (!in_transaction) {
            return; /* Ignore if we somehow missed the assertion */
        }

        uint32_t xact_duration = now - cs_assert_cycle;
        uint32_t min_1byte     = CYCLES_PER_SPI_BIT * 8 *
                                 (100 - TIMING_TOLERANCE_PCT) / 100;

        if (xact_duration < min_1byte) {
            /* Transaction ended before even one byte could transfer */
            log_tamper(TAMPER_SPURIOUS_CS, 0);
        }

        if (byte_count > MAX_TRANSACTION_BYTES) {
            log_tamper(TAMPER_EXCESS_BYTES, (uint8_t)byte_count);
        }

        in_transaction = false;
    }

    gpio_irq_clear(SPI_CS_PIN);
}

/* ── GPIO interrupt: SCLK line ───────────────────────────────── */
void SCLK_GPIO_IRQHandler(void)
{
    uint32_t now = dwt_get_cycles();

    if (!in_transaction) {
        /* Clock outside a CS# window: definite anomaly */
        log_tamper(TAMPER_CLOCK_GLITCH, 0);
        gpio_irq_clear(SPI_SCLK_PIN);
        return;
    }

    if (first_clock_cycle == 0) {
        /* First clock edge after CS# assertion */
        first_clock_cycle = now;
        uint32_t setup    = now - cs_assert_cycle;

        if (setup < CS_ASSERT_SETUP_MIN_CYCLES) {
            log_tamper(TAMPER_EARLY_CLOCK, 0);
        } else if (setup > CS_ASSERT_SETUP_MAX_CYCLES) {
            log_tamper(TAMPER_LATE_DEASSERT, 0);
        }
    } else {
        /* Subsequent edges: check inter-bit timing */
        uint32_t interval = now - last_clock_cycle;
        uint32_t min_iv   = CYCLES_PER_SPI_BIT *
                            (100 - TIMING_TOLERANCE_PCT) / 100;
        uint32_t max_iv   = CYCLES_PER_SPI_BIT *
                            (100 + TIMING_TOLERANCE_PCT) / 100;

        if (interval < min_iv) {
            log_tamper(TAMPER_CLOCK_GLITCH, (uint8_t)byte_count);
        }

        /* Count bytes (8 clocks = 1 byte, rising edge only) */
        static uint32_t edge_count = 0;
        if (++edge_count % 8 == 0) {
            byte_count++;
        }
        (void)max_iv; /* used in stricter modes */
    }

    last_clock_cycle = now;
    gpio_irq_clear(SPI_SCLK_PIN);
}

/* ── Initialisation ──────────────────────────────────────────── */
void spi_tamper_init(void)
{
    dwt_enable();

    /* Configure CS# pin as input with interrupt on both edges */
    gpio_configure_input(SPI_CS_PIN, GPIO_PULL_NONE);
    gpio_irq_enable(SPI_CS_PIN, GPIO_IRQ_BOTH_EDGES);

    /* Configure SCLK pin as input with interrupt on rising edge */
    gpio_configure_input(SPI_SCLK_PIN, GPIO_PULL_NONE);
    gpio_irq_enable(SPI_SCLK_PIN, GPIO_IRQ_RISING_EDGE);
}

/* ── Query API ───────────────────────────────────────────────── */
bool spi_tamper_detected(void)
{
    return tamper_flags_accumulated != 0;
}

uint32_t spi_tamper_get_flags(void)
{
    return tamper_flags_accumulated;
}

void spi_tamper_clear_flags(void)
{
    tamper_flags_accumulated = 0;
}
```

---

### 5.2 HMAC Transaction Verification

```c
/**
 * spi_authenticated.c
 *
 * Wraps SPI transactions with HMAC-SHA256 authentication.
 * Prevents replay attacks with a 32-bit monotonic counter.
 * Uses mbedTLS (or any HMAC provider with the same API).
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "mbedtls/md.h"
#include "platform_spi.h"

/* ── Constants ───────────────────────────────────────────────── */
#define HMAC_LEN          32U   /* SHA-256 output bytes              */
#define KEY_LEN           32U   /* 256-bit symmetric key             */
#define MAX_PAYLOAD       128U  /* Maximum command+data payload      */
#define FRAME_OVERHEAD    (4U + HMAC_LEN)  /* counter(4) + HMAC(32) */

/* ── Key storage (in practice: stored in a secure element or OTP) */
static const uint8_t g_spi_key[KEY_LEN] = {
    0xDE,0xAD,0xBE,0xEF, 0xCA,0xFE,0xBA,0xBE,
    0x01,0x23,0x45,0x67, 0x89,0xAB,0xCD,0xEF,
    0xFE,0xDC,0xBA,0x98, 0x76,0x54,0x32,0x10,
    0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88
};

/* ── Monotonic counter (must survive warm resets; back with NVM) */
static uint32_t g_tx_counter = 0;
static uint32_t g_rx_counter = 0;  /* Last validated received counter */

/* ── Frame layout ────────────────────────────────────────────── *
 *
 *  ┌──────────┬──────────────────┬────────────────────────────┐
 *  │ counter  │ payload (cmd+data)│        HMAC-SHA256         │
 *  │  4 bytes │   N bytes (≤128) │          32 bytes          │
 *  └──────────┴──────────────────┴────────────────────────────┘
 *
 *  HMAC is computed over (counter || payload).
 * ─────────────────────────────────────────────────────────────*/

typedef struct __attribute__((packed)) {
    uint32_t counter;
    uint8_t  payload[MAX_PAYLOAD];
    uint8_t  hmac[HMAC_LEN];
} spi_auth_frame_t;

/* ── Internal: compute HMAC over counter + payload ───────────── */
static int compute_hmac(uint32_t     counter,
                        const uint8_t *payload,
                        size_t         payload_len,
                        uint8_t        out[HMAC_LEN])
{
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    int ret = 0;

    mbedtls_md_init(&ctx);
    ret = mbedtls_md_setup(&ctx, info, /*hmac=*/1);
    if (ret) goto cleanup;

    ret = mbedtls_md_hmac_starts(&ctx, g_spi_key, KEY_LEN);
    if (ret) goto cleanup;

    /* Feed counter in big-endian */
    uint8_t cnt_be[4] = {
        (counter >> 24) & 0xFF,
        (counter >> 16) & 0xFF,
        (counter >>  8) & 0xFF,
        (counter      ) & 0xFF
    };
    ret = mbedtls_md_hmac_update(&ctx, cnt_be, sizeof(cnt_be));
    if (ret) goto cleanup;

    ret = mbedtls_md_hmac_update(&ctx, payload, payload_len);
    if (ret) goto cleanup;

    ret = mbedtls_md_hmac_finish(&ctx, out);

cleanup:
    mbedtls_md_free(&ctx);
    return ret;
}

/* ── Constant-time comparison (prevent timing oracle attacks) ── */
static bool ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

/* ── Public API: send an authenticated SPI command ───────────── */
int spi_auth_send(const uint8_t *payload, size_t payload_len)
{
    if (!payload || payload_len == 0 || payload_len > MAX_PAYLOAD) {
        return -1;
    }

    spi_auth_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    frame.counter = ++g_tx_counter;
    memcpy(frame.payload, payload, payload_len);

    int ret = compute_hmac(frame.counter, payload, payload_len, frame.hmac);
    if (ret != 0) {
        return -2;
    }

    /* Transmit: counter(4) + payload(N) + hmac(32) */
    size_t frame_len = sizeof(uint32_t) + payload_len + HMAC_LEN;

    spi_cs_assert();
    ret = spi_transmit((uint8_t *)&frame, frame_len);
    spi_cs_deassert();

    return ret;
}

/* ── Public API: receive and verify an authenticated response ── */
int spi_auth_receive(uint8_t *out_buf, size_t buf_len,
                     size_t *out_len, size_t expected_payload_len)
{
    if (!out_buf || !out_len || expected_payload_len > MAX_PAYLOAD) {
        return -1;
    }

    spi_auth_frame_t frame;
    size_t frame_len = sizeof(uint32_t) + expected_payload_len + HMAC_LEN;

    spi_cs_assert();
    int ret = spi_receive((uint8_t *)&frame, frame_len);
    spi_cs_deassert();

    if (ret != 0) {
        return -2;
    }

    /* 1. Replay check: counter must be strictly greater than last accepted */
    if (frame.counter <= g_rx_counter) {
        /* Possible replay attack */
        return -3;
    }

    /* 2. HMAC verification */
    uint8_t expected_hmac[HMAC_LEN];
    ret = compute_hmac(frame.counter, frame.payload,
                       expected_payload_len, expected_hmac);
    if (ret != 0) {
        return -4;
    }

    if (!ct_equal(frame.hmac, expected_hmac, HMAC_LEN)) {
        /* Authentication failure: injection or corruption */
        return -5;
    }

    /* 3. Accept */
    g_rx_counter = frame.counter;
    size_t copy_len = (expected_payload_len < buf_len)
                      ? expected_payload_len : buf_len;
    memcpy(out_buf, frame.payload, copy_len);
    *out_len = copy_len;

    return 0;
}
```

---

### 5.3 Glitch Detection via GPIO Interrupt

```c
/**
 * spi_glitch_detect.c
 *
 * Detects voltage/clock glitches on the SPI SCLK line by monitoring
 * pulse width using a high-resolution hardware timer.
 * Legitimate SPI clocks have a period of (1 / SPI_CLOCK_HZ).
 * A glitch pulse is typically < 10 ns — far shorter than any valid clock.
 */

#include <stdint.h>
#include "platform.h"

#define GLITCH_MIN_PULSE_NS   50U    /* Pulses shorter than this = glitch */
#define CPU_NS_PER_CYCLE      (1000000000UL / CPU_CLOCK_HZ)

/* Track rising-edge timestamp */
static volatile uint32_t last_rising_cycle = 0;
static volatile uint32_t glitch_count      = 0;

void SCLK_RISING_IRQHandler(void)
{
    last_rising_cycle = dwt_get_cycles();
    gpio_irq_clear(SPI_SCLK_PIN);
}

void SCLK_FALLING_IRQHandler(void)
{
    uint32_t now          = dwt_get_cycles();
    uint32_t pulse_cycles = now - last_rising_cycle;
    uint32_t pulse_ns     = pulse_cycles * CPU_NS_PER_CYCLE;

    if (pulse_ns < GLITCH_MIN_PULSE_NS) {
        glitch_count++;
        /* Immediate lockout: halt SPI controller and notify system */
        spi_controller_disable();
        security_alert(ALERT_GLITCH_DETECTED, pulse_ns);
    }

    gpio_irq_clear(SPI_SCLK_PIN);
}

void glitch_detector_init(void)
{
    dwt_enable();
    gpio_configure_input(SPI_SCLK_PIN, GPIO_PULL_DOWN);
    gpio_irq_enable(SPI_SCLK_PIN, GPIO_IRQ_RISING_EDGE);
    /* Configure falling edge on same pin via a secondary IRQ handler */
    gpio_irq_enable_falling(SPI_SCLK_PIN, GPIO_IRQ_FALLING_EDGE);
}

uint32_t glitch_get_count(void)
{
    return glitch_count;
}
```

---

### 5.4 CS# Line Monitor

```cpp
/**
 * SpiChipSelectMonitor.hpp  (C++17)
 *
 * Monitors CS# for spurious assertions, unexpected hold-downs,
 * and overlapping transactions using a lock-free ring buffer.
 */

#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <functional>

enum class CsEvent : uint8_t {
    Assert   = 0,
    Deassert = 1,
};

struct CsRecord {
    uint32_t cycle_stamp;
    CsEvent  event;
};

template <std::size_t N = 64>
class SpiChipSelectMonitor {
public:
    using AlertCallback = std::function<void(const char *reason)>;

    explicit SpiChipSelectMonitor(AlertCallback cb,
                                  uint32_t min_xact_cycles,
                                  uint32_t max_xact_cycles)
        : alert_cb_(cb),
          min_cycles_(min_xact_cycles),
          max_cycles_(max_xact_cycles),
          head_(0), tail_(0),
          active_(false), assert_cycle_(0) {}

    /* Called from ISR on both CS# edges */
    void on_cs_edge(uint32_t cycle, CsEvent ev) noexcept {
        /* Write to ring buffer (single producer: ISR) */
        std::size_t next = (head_.load(std::memory_order_relaxed) + 1) & (N - 1);
        buf_[next] = { cycle, ev };
        head_.store(next, std::memory_order_release);
        process(cycle, ev);
    }

    /* Call from main loop to drain deferred checks */
    void poll() {
        /* Additional deferred processing can go here */
    }

    std::size_t alert_count() const noexcept {
        return alert_count_.load(std::memory_order_relaxed);
    }

private:
    void process(uint32_t cycle, CsEvent ev) noexcept {
        if (ev == CsEvent::Assert) {
            if (active_) {
                fire_alert("CS# asserted while already active (overlap)");
            }
            active_       = true;
            assert_cycle_ = cycle;

        } else {
            if (!active_) {
                fire_alert("CS# deasserted without prior assertion");
                return;
            }
            uint32_t duration = cycle - assert_cycle_;
            active_ = false;

            if (duration < min_cycles_) {
                fire_alert("CS# transaction too short (spurious?)");
            } else if (duration > max_cycles_) {
                fire_alert("CS# held abnormally long (bus stuck / attack?)");
            }
        }
    }

    void fire_alert(const char *reason) noexcept {
        alert_count_.fetch_add(1, std::memory_order_relaxed);
        if (alert_cb_) {
            alert_cb_(reason);
        }
    }

    AlertCallback            alert_cb_;
    uint32_t                 min_cycles_;
    uint32_t                 max_cycles_;
    std::array<CsRecord, N>  buf_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
    std::atomic<std::size_t> alert_count_{0};
    bool                     active_;
    uint32_t                 assert_cycle_;
};
```

---

## 6. Rust Implementation

### 6.1 Timing Guard with embedded-hal

```rust
//! spi_timing_guard.rs
//!
//! A timing guard that wraps every SPI transaction and checks that
//! its duration falls within an acceptable window derived from the
//! configured clock rate. Uses `embedded-hal` 1.x traits and
//! `cortex-m::peripheral::DWT` for cycle counting.
//!
//! [dependencies]
//! embedded-hal = "1.0"
//! cortex-m     = { version = "0.7", features = ["critical-section-single-core"] }

#![no_std]

use core::time::Duration;
use embedded_hal::spi::{Operation, SpiDevice};
use cortex_m::peripheral::DWT;

/// Acceptable transaction duration as a multiple of the nominal duration.
/// E.g. tolerance = 0.20 means ±20 % around nominal is allowed.
const TIMING_TOLERANCE: f32 = 0.20;

/// Result type for tamper-detected errors.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TamperError<E> {
    /// Underlying SPI error.
    Spi(E),
    /// Transaction completed too fast — possible glitch injection.
    TooFast { cycles: u32, minimum: u32 },
    /// Transaction completed too slow — possible bus contention / MitM.
    TooSlow { cycles: u32, maximum: u32 },
    /// CS# was held between transactions — unexpected bus activity.
    LineBusy,
}

/// Wraps an `SpiDevice` implementation and adds timing-based
/// anti-tamper checks around every transaction.
pub struct TimingGuard<SPI> {
    inner: SPI,
    /// Nominal cycles for an 8-bit transfer at the configured clock.
    cycles_per_byte: u32,
    cpu_clock_hz: u32,
    spi_clock_hz: u32,
    alert_count: u32,
}

impl<SPI> TimingGuard<SPI>
where
    SPI: SpiDevice,
{
    /// Create a new `TimingGuard`.
    ///
    /// `cpu_clock_hz` — core clock frequency  
    /// `spi_clock_hz` — SPI peripheral clock frequency
    pub fn new(inner: SPI, cpu_clock_hz: u32, spi_clock_hz: u32) -> Self {
        let cycles_per_byte = cpu_clock_hz / spi_clock_hz * 8;
        Self {
            inner,
            cycles_per_byte,
            cpu_clock_hz,
            spi_clock_hz,
            alert_count: 0,
        }
    }

    /// Returns the number of timing violations detected.
    pub fn alert_count(&self) -> u32 {
        self.alert_count
    }

    /// Execute a guarded transaction: measure elapsed DWT cycles and
    /// verify them against acceptable bounds.
    pub fn transaction_guarded(
        &mut self,
        operations: &mut [Operation<'_, u8>],
    ) -> Result<(), TamperError<SPI::Error>> {

        // Compute expected duration based on total bytes
        let total_bytes: usize = operations
            .iter()
            .map(|op| match op {
                Operation::Read(b)       => b.len(),
                Operation::Write(b)      => b.len(),
                Operation::Transfer(r,_) => r.len(),
                Operation::TransferInPlace(b) => b.len(),
                Operation::DelayNs(_)    => 0,
            })
            .sum();

        let nominal_cycles = self.cycles_per_byte * total_bytes as u32;
        let tolerance      = (nominal_cycles as f32 * TIMING_TOLERANCE) as u32;
        let min_cycles     = nominal_cycles.saturating_sub(tolerance);
        let max_cycles     = nominal_cycles.saturating_add(tolerance)
                             + self.cpu_clock_hz / 1_000; // +1 ms interrupt margin

        // Snapshot before
        let t0 = DWT::cycle_count();

        // Execute
        self.inner
            .transaction(operations)
            .map_err(TamperError::Spi)?;

        // Snapshot after
        let elapsed = DWT::cycle_count().wrapping_sub(t0);

        // Validate
        if total_bytes > 0 && elapsed < min_cycles {
            self.alert_count = self.alert_count.saturating_add(1);
            return Err(TamperError::TooFast {
                cycles:  elapsed,
                minimum: min_cycles,
            });
        }

        if elapsed > max_cycles {
            self.alert_count = self.alert_count.saturating_add(1);
            return Err(TamperError::TooSlow {
                cycles:  elapsed,
                maximum: max_cycles,
            });
        }

        Ok(())
    }
}
```

---

### 6.2 Authenticated SPI Wrapper

```rust
//! spi_authenticated.rs
//!
//! HMAC-SHA256 authenticated SPI transaction wrapper.
//! Prevents injection and replay attacks.
//! Uses the `hmac` + `sha2` crates (pure-Rust, no_std compatible).
//!
//! [dependencies]
//! hmac    = { version = "0.12", default-features = false }
//! sha2    = { version = "0.10", default-features = false }
//! heapless = "0.8"

#![no_std]

use hmac::{Hmac, Mac};
use sha2::Sha256;
use heapless::Vec;

type HmacSha256 = Hmac<Sha256>;

const HMAC_LEN:    usize = 32;
const MAX_PAYLOAD: usize = 128;
const KEY_LEN:     usize = 32;

/// Errors from authenticated SPI operations.
#[derive(Debug, PartialEq, Eq)]
pub enum AuthError {
    /// Payload exceeds maximum allowed size.
    PayloadTooLarge,
    /// HMAC verification failed — possible injection or corruption.
    AuthenticationFailed,
    /// Received counter ≤ last accepted — replay attack detected.
    ReplayDetected,
    /// Underlying SPI bus error.
    SpiBus,
    /// HMAC engine error (key length, etc.).
    HmacInit,
}

/// Authenticated SPI wrapper.  
/// `K` — key length (must equal `KEY_LEN`).
pub struct AuthenticatedSpi<SPI> {
    inner: SPI,
    key:   [u8; KEY_LEN],
    tx_counter: u32,
    rx_counter: u32,
}

impl<SPI> AuthenticatedSpi<SPI>
where
    SPI: embedded_hal::spi::SpiDevice,
{
    pub fn new(inner: SPI, key: [u8; KEY_LEN]) -> Self {
        Self {
            inner,
            key,
            tx_counter: 0,
            rx_counter: 0,
        }
    }

    /// Compute HMAC-SHA256 over (counter_be || payload).
    fn compute_hmac(
        &self,
        counter: u32,
        payload: &[u8],
    ) -> Result<[u8; HMAC_LEN], AuthError> {
        let mut mac = HmacSha256::new_from_slice(&self.key)
            .map_err(|_| AuthError::HmacInit)?;

        let counter_be = counter.to_be_bytes();
        mac.update(&counter_be);
        mac.update(payload);

        let result = mac.finalize().into_bytes();
        let mut out = [0u8; HMAC_LEN];
        out.copy_from_slice(&result);
        Ok(out)
    }

    /// Constant-time comparison to prevent timing oracles.
    fn ct_eq(a: &[u8], b: &[u8]) -> bool {
        if a.len() != b.len() {
            return false;
        }
        let mut diff: u8 = 0;
        for (x, y) in a.iter().zip(b.iter()) {
            diff |= x ^ y;
        }
        diff == 0
    }

    /// Send an authenticated frame: [counter(4)] [payload(N)] [HMAC(32)].
    pub fn send_authenticated(
        &mut self,
        payload: &[u8],
    ) -> Result<(), AuthError> {
        if payload.len() > MAX_PAYLOAD {
            return Err(AuthError::PayloadTooLarge);
        }

        self.tx_counter = self.tx_counter.wrapping_add(1);
        let counter = self.tx_counter;

        let mac = self.compute_hmac(counter, payload)?;

        // Build frame in a heapless buffer
        let mut frame: Vec<u8, { 4 + MAX_PAYLOAD + HMAC_LEN }> = Vec::new();
        frame.extend_from_slice(&counter.to_be_bytes()).ok();
        frame.extend_from_slice(payload).ok();
        frame.extend_from_slice(&mac).ok();

        self.inner
            .write(&frame)
            .map_err(|_| AuthError::SpiBus)
    }

    /// Receive and verify an authenticated response.
    ///
    /// Returns the verified payload on success.
    pub fn receive_authenticated(
        &mut self,
        expected_payload_len: usize,
    ) -> Result<Vec<u8, MAX_PAYLOAD>, AuthError> {
        if expected_payload_len > MAX_PAYLOAD {
            return Err(AuthError::PayloadTooLarge);
        }

        let frame_len = 4 + expected_payload_len + HMAC_LEN;
        let mut frame_buf: Vec<u8, { 4 + MAX_PAYLOAD + HMAC_LEN }> =
            Vec::new();
        frame_buf
            .resize(frame_len, 0u8)
            .map_err(|_| AuthError::PayloadTooLarge)?;

        self.inner
            .read(&mut frame_buf)
            .map_err(|_| AuthError::SpiBus)?;

        // Parse counter
        let counter = u32::from_be_bytes([
            frame_buf[0], frame_buf[1], frame_buf[2], frame_buf[3],
        ]);

        // 1. Replay check
        if counter <= self.rx_counter {
            return Err(AuthError::ReplayDetected);
        }

        // 2. Extract payload and received MAC
        let payload_slice  = &frame_buf[4..4 + expected_payload_len];
        let received_mac   = &frame_buf[4 + expected_payload_len..frame_len];

        // 3. Compute expected MAC
        let expected_mac = self.compute_hmac(counter, payload_slice)?;

        // 4. Constant-time comparison
        if !Self::ct_eq(received_mac, &expected_mac) {
            return Err(AuthError::AuthenticationFailed);
        }

        // 5. Accept
        self.rx_counter = counter;
        let mut out: Vec<u8, MAX_PAYLOAD> = Vec::new();
        out.extend_from_slice(payload_slice).ok();
        Ok(out)
    }
}
```

---

### 6.3 Anomaly State Machine

```rust
//! spi_anomaly_fsm.rs
//!
//! A finite state machine that classifies SPI bus anomalies and
//! triggers appropriate graduated responses.
//! Designed for `no_std` environments; uses only core types.

#![no_std]

use core::sync::atomic::{AtomicU32, Ordering};

/// Tamper event categories.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TamperKind {
    TimingViolation,
    AuthFailure,
    ReplayAttack,
    GlitchDetected,
    SpuriousChipSelect,
    ExcessiveBytes,
}

/// Security level — escalates with repeated violations.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum SecurityLevel {
    /// Normal operation.
    Normal   = 0,
    /// Elevated: log, reduce capabilities.
    Elevated = 1,
    /// High: disable non-essential peripherals, alert operator.
    High     = 2,
    /// Critical: zeroize keys, halt SPI, require physical reset.
    Critical = 3,
}

/// Callbacks invoked on level transitions (extern "C" for C FFI).
pub trait TamperHandler {
    fn on_elevated(&mut self, event: TamperKind, count: u32);
    fn on_high(&mut self, event: TamperKind, count: u32);
    fn on_critical(&mut self, event: TamperKind, count: u32);
}

/// Thresholds before escalation to the next level.
const THRESHOLD_ELEVATED: u32 = 3;
const THRESHOLD_HIGH:      u32 = 7;
const THRESHOLD_CRITICAL:  u32 = 12;

/// Immediate-escalation events (skip straight to Critical).
fn is_critical_immediate(kind: TamperKind) -> bool {
    matches!(kind, TamperKind::GlitchDetected | TamperKind::ReplayAttack)
}

pub struct AnomalyFsm<H: TamperHandler> {
    handler:       H,
    level:         SecurityLevel,
    event_count:   u32,
    last_event:    Option<TamperKind>,
}

impl<H: TamperHandler> AnomalyFsm<H> {
    pub fn new(handler: H) -> Self {
        Self {
            handler,
            level:       SecurityLevel::Normal,
            event_count: 0,
            last_event:  None,
        }
    }

    /// Report a tamper event. Returns the new security level.
    pub fn report(&mut self, kind: TamperKind) -> SecurityLevel {
        self.last_event = Some(kind);

        // Immediate escalation for severe events
        if is_critical_immediate(kind) {
            self.transition(SecurityLevel::Critical, kind);
            return self.level;
        }

        self.event_count = self.event_count.saturating_add(1);

        let new_level = match self.event_count {
            n if n >= THRESHOLD_CRITICAL => SecurityLevel::Critical,
            n if n >= THRESHOLD_HIGH     => SecurityLevel::High,
            n if n >= THRESHOLD_ELEVATED => SecurityLevel::Elevated,
            _                            => SecurityLevel::Normal,
        };

        if new_level > self.level {
            self.transition(new_level, kind);
        }

        self.level
    }

    fn transition(&mut self, target: SecurityLevel, kind: TamperKind) {
        self.level = target;
        let count  = self.event_count;

        match target {
            SecurityLevel::Normal   => {}
            SecurityLevel::Elevated => self.handler.on_elevated(kind, count),
            SecurityLevel::High     => self.handler.on_high(kind, count),
            SecurityLevel::Critical => self.handler.on_critical(kind, count),
        }
    }

    pub fn current_level(&self) -> SecurityLevel {
        self.level
    }

    pub fn event_count(&self) -> u32 {
        self.event_count
    }
}

// ── Example handler ───────────────────────────────────────────

struct SystemHandler;

impl TamperHandler for SystemHandler {
    fn on_elevated(&mut self, event: TamperKind, count: u32) {
        // Reduce SPI clock speed, increase logging verbosity
        // e.g. hal::spi::set_clock(SPI_CLOCK_HZ / 2);
        let _ = (event, count);
    }

    fn on_high(&mut self, event: TamperKind, count: u32) {
        // Disable DMA on SPI bus, alert watchdog
        let _ = (event, count);
    }

    fn on_critical(&mut self, event: TamperKind, count: u32) {
        // Zeroize all key material, disable SPI controller,
        // write tamper record to write-once NVM, trigger reset
        let _ = (event, count);
        // safety_zeroize_keys();
        // spi_controller_disable_permanent();
        // cortex_m::peripheral::SCB::sys_reset();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct MockHandler {
        elevated_calls: u32,
        high_calls:     u32,
        critical_calls: u32,
    }

    impl MockHandler {
        fn new() -> Self {
            Self { elevated_calls: 0, high_calls: 0, critical_calls: 0 }
        }
    }

    impl TamperHandler for MockHandler {
        fn on_elevated(&mut self, _: TamperKind, _: u32) {
            self.elevated_calls += 1;
        }
        fn on_high(&mut self, _: TamperKind, _: u32) {
            self.high_calls += 1;
        }
        fn on_critical(&mut self, _: TamperKind, _: u32) {
            self.critical_calls += 1;
        }
    }

    #[test]
    fn test_gradual_escalation() {
        let mut fsm = AnomalyFsm::new(MockHandler::new());

        for _ in 0..THRESHOLD_ELEVATED {
            fsm.report(TamperKind::TimingViolation);
        }
        assert_eq!(fsm.current_level(), SecurityLevel::Elevated);
        assert_eq!(fsm.handler.elevated_calls, 1);

        for _ in 0..(THRESHOLD_HIGH - THRESHOLD_ELEVATED) {
            fsm.report(TamperKind::AuthFailure);
        }
        assert_eq!(fsm.current_level(), SecurityLevel::High);
    }

    #[test]
    fn test_immediate_critical() {
        let mut fsm = AnomalyFsm::new(MockHandler::new());
        fsm.report(TamperKind::GlitchDetected);
        assert_eq!(fsm.current_level(), SecurityLevel::Critical);
        assert_eq!(fsm.handler.critical_calls, 1);
    }
}
```

---

## 7. Response and Countermeasures

When a tamper event is detected, the system must respond. Responses are graduated by severity:

| Severity | Trigger Condition | Response Actions |
|---|---|---|
| **Log** | Single timing anomaly | Record to tamper log with timestamp; continue operation |
| **Throttle** | 3+ violations | Reduce SPI clock to minimum; disable DMA transfers |
| **Alert** | 7+ violations or auth failure | Assert tamper output pin; send alert to host MCU; increase authentication frequency |
| **Lockout** | 12+ violations or glitch/replay detected | Disable SPI controller; zeroize session keys; require authenticated re-initialisation |
| **Zeroize** | Glitch or hardware mesh trigger | Zeroize all key material; write tamper record to OTP; assert tamper line; halt |

### 7.1 Key Zeroisation (C)

```c
/**
 * Securely zeroize a key buffer.
 * volatile prevents the compiler from optimising the write away.
 */
void secure_zeroize(void *buf, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    while (len--) {
        *p++ = 0x00;
    }
    /* Memory barrier: ensure writes are visible before continuing */
    __DSB();
    __ISB();
}
```

### 7.2 Key Zeroisation (Rust)

```rust
use core::sync::atomic::{compiler_fence, Ordering};

/// Securely zeroize a key buffer.
/// Uses `compiler_fence` to prevent the optimizer eliminating the write.
pub fn secure_zeroize(buf: &mut [u8]) {
    for byte in buf.iter_mut() {
        unsafe { core::ptr::write_volatile(byte, 0u8) };
    }
    compiler_fence(Ordering::SeqCst);
}
```

---

## 8. Hardware-Assisted Protections

Software detection complements but cannot replace hardware defences. The following hardware measures should be combined with the software techniques above:

| Technique | Description | Defeats |
|---|---|---|
| **Tamper mesh** | PCB conductive mesh over SPI traces; breaks if trace is cut | FIB, microprobing |
| **Potting compound** | Epoxy encapsulation of PCB assembly | Physical probe access |
| **Active shielding** | Driven shield layer with continuity monitoring | Passive probing |
| **Secure Element** | Dedicated tamper-resistant IC (e.g. ATECC608) | Key extraction, cloning |
| **Signal conditioning** | Series resistors + filtering caps on SPI lines | Glitch injection amplitude |
| **Power filtering** | Bulk decoupling + ferrite beads on Vcc | Power glitching |
| **Independent supply monitoring** | Brown-out detect + Vcc comparator | Voltage glitching |
| **EMI shielding can** | Metal shield over SPI area | Side-channel (EM) analysis |

### Secure Element Integration (C example)

```c
/**
 * spi_secure_element.c
 *
 * Pattern for using a secure element (e.g. ATECC608B) as an
 * authentication oracle for SPI flash transactions.
 * The secure element handles key storage and HMAC computation
 * in a tamper-resistant enclosure — keys never leave the IC.
 */

#include "atca_basic.h"   /* cryptoauthlib */

#define SE_KEY_SLOT  0    /* Key slot configured during personalisation */

int se_compute_hmac(const uint8_t *data, size_t len,
                    uint8_t out_mac[32])
{
    /* Request HMAC from secure element over I2C/SPI interface.
       The key material never enters the host MCU. */
    ATCA_STATUS status = atcab_mac(MAC_MODE_BLOCK2_TEMPKEY,
                                   SE_KEY_SLOT,
                                   data,
                                   out_mac);
    return (status == ATCA_SUCCESS) ? 0 : -1;
}
```

---

## 9. Design Checklist

Use this checklist during hardware design review and firmware audit:

**Physical Layer**
- [ ] SPI traces are on inner PCB layers (not exposed on top/bottom copper)
- [ ] Series termination resistors (22–33 Ω) on SCLK, MOSI, CS# lines
- [ ] Decoupling capacitors within 1 mm of each device's Vcc pin
- [ ] Brown-out detection enabled on host MCU with reset threshold ≥ 2.5 V
- [ ] Tamper mesh or potting compound if device is accessible to end-user

**Firmware — Detection**
- [ ] CS# transitions monitored with hardware timer timestamps
- [ ] SCLK pulse width checked against configured clock rate
- [ ] Glitch pulses shorter than 50 ns trigger immediate lockout
- [ ] Tamper event log with timestamps written to persistent NVM
- [ ] Anomaly FSM with graduated response levels implemented

**Firmware — Authentication**
- [ ] All security-critical SPI commands wrapped with HMAC-SHA256
- [ ] Nonce or monotonic counter included in every authenticated frame
- [ ] HMAC comparison is constant-time (no early exit on mismatch)
- [ ] Session keys stored only in RAM (not flash) where feasible
- [ ] Key zeroisation on tamper detection with memory barrier

**Firmware — Operational**
- [ ] SPI controller disabled (not just CS# high) when bus is idle
- [ ] SPI clock minimised to required speed (reduces EM emission)
- [ ] Watchdog timer cannot be petted by SPI interrupt handler alone
- [ ] Tamper alerts logged with real-time clock timestamp where available

---

## 10. Summary

SPI bus anti-tampering is a multi-layer discipline spanning physical construction, electrical design, and firmware architecture. The key principles are:

**Detect early:** Hardware-timer-based monitoring of CS# and SCLK signals catches glitches and injection attempts at the nanosecond level — far faster than any polling approach. The C examples demonstrate ISR-driven detection using DWT cycle counters and GPIO interrupts, while the Rust `TimingGuard` wraps the `embedded-hal` `SpiDevice` trait to add transparent timing validation.

**Authenticate every transaction:** Unencrypted SPI traffic is trivially readable with a $20 logic analyser. HMAC-SHA256 with a monotonic counter eliminates both eavesdropping value and replay attacks. The `AuthenticatedSpi<SPI>` Rust wrapper shows how this can be composed generically over any `embedded-hal` SPI device. In production, keys must reside in a secure element — never in MCU flash.

**Respond proportionately:** The `AnomalyFsm` state machine escalates from logging through throttling to full lockout, ensuring that transient electrical noise does not cause unnecessary denial-of-service while a determined attacker is still caught and contained.

**Combine with hardware:** No software measure survives a well-equipped laboratory attacker with physical access to exposed PCB traces. Inner-layer routing, tamper meshes, potting compound, and a hardware secure element are necessary complements to the firmware techniques described here.

Together, these measures raise the cost of a successful physical attack on the SPI bus to a level that is economically impractical for all but the most motivated adversaries — and provide a forensic trail when attacks are attempted.

---

*Document: 88 — Anti-tampering Measures | SPI Security Series*