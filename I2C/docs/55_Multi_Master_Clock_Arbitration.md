# 55. Multi-Master Clock Arbitration in I²C

**Theory & Mechanisms**
- Open-drain wired-AND bus topology and how it physically enables arbitration
- Step-by-step clock synchronization algorithm — showing why effective SCL frequency is lower than any individual master, with timing diagrams
- Full arbitration algorithm from idle detection through bit-by-bit SDA monitoring and loss handling
- Special cases: repeated START, 10-bit addressing, general call, and ACK-phase edge cases

**C/C++ Examples**
- Complete bit-bang implementation with `scl_sync_high()` for clock sync and per-bit arbitration detection in `send_bit()`
- C++ retry manager class with exponential backoff and jitter
- STM32 HAL hardware peripheral example using the `ARLO` interrupt flag

**Rust Examples**
- Portable `embedded-hal` bit-bang driver with idiomatic error types
- `no_std`-compatible retry manager using an LFSR for jitter (no `rand` crate needed)
- Non-blocking state machine design for interrupt-driven architectures

**Edge Cases & Design Guidelines** covering bus lockup recovery, pull-up resistor sizing, the 9-clock recovery sequence, and a summary reference table.


> **Topic:** Detailed analysis of clock arbitration algorithms in multi-master scenarios

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [I²C Multi-Master Fundamentals](#2-i2c-multi-master-fundamentals)
3. [Clock Synchronization Mechanism](#3-clock-synchronization-mechanism)
4. [Bus Arbitration Mechanism](#4-bus-arbitration-mechanism)
5. [Arbitration Lost Scenarios](#5-arbitration-lost-scenarios)
6. [Clock Stretching in Multi-Master Context](#6-clock-stretching-in-multi-master-context)
7. [Implementation in C/C++](#7-implementation-in-cc)
8. [Implementation in Rust](#8-implementation-in-rust)
9. [Edge Cases and Pitfalls](#9-edge-cases-and-pitfalls)
10. [Summary](#10-summary)

---

## 1. Introduction

The I²C (Inter-Integrated Circuit) protocol, developed by Philips Semiconductors (now NXP), supports a **multi-master bus topology** — a design where two or more master devices may simultaneously initiate communication on the same two-wire bus (SDA and SCL). This introduces a fundamental challenge: what happens when two masters start transmitting at exactly the same time?

The solution lies in two tightly coupled mechanisms built into the I²C specification:

- **Clock Synchronization** — Resolving the simultaneous generation of SCL clock signals from multiple masters.
- **Bus Arbitration** — Determining which master gains control of the bus when concurrent transmissions begin.

Together these mechanisms allow the bus to resolve contention **non-destructively** and **without data corruption**, unlike protocols such as early CAN or RS-485 variants that require collision detection and retransmission.

---

## 2. I²C Multi-Master Fundamentals

### 2.1 Open-Drain / Open-Collector Bus Topology

The foundational enabler of multi-master I²C is the **open-drain (or open-collector) wiring** of both SDA and SCL lines. Both lines are pulled high by resistors (typically 1 kΩ to 10 kΩ depending on bus speed), and any device can pull a line low by asserting its output transistor.

This means the bus implements a **wired-AND logic**:

```
Bus voltage = HIGH  only if ALL devices release the line
Bus voltage = LOW   if ANY device pulls the line low
```

This property is the physical backbone of arbitration. A master that drives HIGH can simultaneously observe LOW if another master (or a slave stretching the clock) pulls the line down.

### 2.2 Bus States

| State        | SDA  | SCL  | Description                                    |
|--------------|------|------|------------------------------------------------|
| Idle         | HIGH | HIGH | No communication; bus is free                  |
| Start        | ↓LOW | HIGH | SDA falls while SCL is high (Start condition)  |
| Data Bit '1' | HIGH | —    | SDA high during SCL high phase                 |
| Data Bit '0' | LOW  | —    | SDA low during SCL high phase                  |
| Stop         | ↑HIGH| HIGH | SDA rises while SCL is high (Stop condition)   |

### 2.3 Who Can Be a Master?

In an I²C multi-master system, any node that can generate a Start condition and drive SCL is considered a master. A device may be a **master-only**, **slave-only**, or **multi-master** device that operates as either depending on the current bus state. When a device acts as a master, it drives SCL and initiates transactions. When arbitration is lost, it must revert to slave receiver mode immediately.

---

## 3. Clock Synchronization Mechanism

### 3.1 The Problem

When two masters simultaneously initiate a START condition and start generating SCL pulses, their individual clock signals may be out of phase or have slightly different frequencies. This creates an undefined SCL waveform on the bus unless resolved.

### 3.2 The Wired-AND Solution

Because SCL is open-drain, the combined SCL signal is the **logical AND** of all SCL outputs:

- A master holds SCL LOW until it is ready for the HIGH phase.
- SCL on the bus goes HIGH only when **all** masters release it.
- If any master is still in its LOW phase, the bus remains LOW.

This naturally synchronizes the clocks:

```
Master A SCL:  ___|‾‾‾|___|‾‾‾|___|‾‾‾|___
Master B SCL:  _______|‾‾‾‾‾‾‾|___|‾‾‾|___
Bus SCL:       _______|‾‾‾|___|___|‾‾‾|___
                       ^                ^
                 Both released    Both release simultaneously
```

### 3.3 Clock Sync Algorithm (Step-by-Step)

1. Each master generates its own internal clock with its own LOW and HIGH period.
2. When a master drives SCL LOW, it starts its own LOW period counter.
3. When a master wants to release SCL (go HIGH), it releases its pin but must wait and observe the actual bus state.
4. If the bus SCL is still LOW (another master has not released yet), the waiting master **extends its own HIGH period counter** (resets or pauses it) until the bus actually goes HIGH.
5. Once the bus goes HIGH, all masters start their HIGH period simultaneously.
6. The HIGH period ends as soon as **any** master pulls SCL LOW again (whichever master has the shortest HIGH period determines the bus HIGH time).
7. The LOW period is determined by the master with the **longest LOW period**.

This means:
- **Effective SCL HIGH time** = minimum of all masters' HIGH periods
- **Effective SCL LOW time** = maximum of all masters' LOW periods
- **Effective frequency** is lower than any individual master, but all remain synchronized

### 3.4 Timing Diagram

```
Master A:  _____|‾‾‾‾‾|_________|‾‾‾‾‾|___
           LOW=5 HIGH=5  LOW=9    HIGH=5

Master B:  _________|‾‾‾‾‾‾‾‾‾|___|‾‾‾‾‾‾‾‾‾|___
           LOW=9    HIGH=9      LOW=3  HIGH=9

Bus SCL:   _________|‾‾‾‾‾|_________|‾‾‾‾‾|___
           LOW=max(5,9)=9  HIGH=min(5,9)=5
                    ^--- Both release here, bus goes HIGH
```

---

## 4. Bus Arbitration Mechanism

### 4.1 The Problem

Clock synchronization ensures a clean SCL signal, but does not determine which master controls SDA (the data line). When two masters simultaneously transmit different data bits on SDA, the bus will reflect the wired-AND of both — one master may output HIGH while the other outputs LOW, and the bus will be LOW.

### 4.2 Arbitration by SDA Monitoring

Each master continuously **monitors SDA while transmitting**. The rule is:

> **If a master drives SDA HIGH but reads SDA LOW, it has lost arbitration.**

Since the bus is wired-AND:
- Master A drives HIGH → releases SDA (SDA can be pulled high)
- Master B drives LOW → pulls SDA down (SDA is LOW)
- Bus SDA = LOW (because of B)
- Master A reads LOW but drove HIGH → Master A **loses arbitration**
- Master A must immediately stop driving SCL and SDA, and go silent
- Master B continues unaware that any contention occurred

### 4.3 Arbitration Algorithm (Step-by-Step)

```
1. Both masters detect bus IDLE (SDA and SCL high)
2. Both generate a START condition
3. Both begin transmitting the 7-bit (or 10-bit) address + R/W bit
4. On each bit:
   a. Master drives its intended SDA value
   b. Master waits for SCL HIGH phase (after clock sync)
   c. Master reads actual SDA value on the bus
   d. If (drove HIGH) AND (bus reads LOW):
       → Arbitration LOST
       → Release SDA and SCL immediately
       → Set ARBL (Arbitration Lost) flag
       → Enter slave-receiver mode and monitor for Stop
   e. If values match: Continue to next bit
5. Winner continues until Stop condition
```

### 4.4 Why Lower Addresses Win

Because SDA bit transmission is MSB-first, and a LOW bit always wins over HIGH (wired-AND), a master transmitting a **lower address** will assert more LOW bits earlier in the sequence and therefore win arbitration against a master transmitting a higher address.

For example:
- Master A transmits address `0b1010_001` (0x51)
- Master B transmits address `0b1010_000` (0x50)

Bit 6 through bit 1 are identical. On bit 0:
- Master A drives HIGH
- Master B drives LOW
- Bus = LOW → Master A loses, Master B wins

This means **address-based priority** is implicit in the I²C arbitration design.

### 4.5 Arbitration with Identical Addresses

If two masters attempt to communicate with the same slave address, arbitration continues through the data phase. They can transmit the same address simultaneously without conflict. The arbitration is resolved in the data byte(s) following the address. This is generally undesirable and should be avoided in system design.

---

## 5. Arbitration Lost Scenarios

### 5.1 Standard Arbitration Loss

The most common case: two masters transmit different addresses. Resolved in the address phase as described above.

### 5.2 Arbitration Loss During Data Phase

If two masters address the same slave and transmit different data, arbitration is resolved during the data byte transmission. The losing master must immediately cease and monitor the bus.

### 5.3 Arbitration Loss vs. START Condition

A special case: one master is mid-transmission while another generates a (repeated) START condition. The I²C spec requires that a master that loses arbitration to a START condition must switch to slave mode immediately, because a START on a busy bus is only legal from another master (or can indicate a bus fault).

```
Master A: ...[DATA BITS]...
Master B:      [START][ADDR...]

If Master B asserts START (SDA falls while SCL is HIGH) during Master A's
data bit transmission, Master A observes a protocol violation on SDA and
must treat this as arbitration loss.
```

### 5.4 Arbitration Loss During Address ACK

A master also performs arbitration during the ACK bit sent by the slave. If the winning master's addressed slave does not ACK (drives SDA HIGH) but another agent holds SDA LOW, the master may misinterpret the situation. The firmware must handle this condition carefully, typically by treating unexpected LOW on SDA during ACK as a valid ACK (since a slave may be stretching or responding).

---

## 6. Clock Stretching in Multi-Master Context

### 6.1 What Is Clock Stretching?

A slave device can hold SCL LOW after a byte is received to buy time for internal processing. This is called **clock stretching**. The master must detect that SCL remains LOW after it releases it and wait until SCL is released.

### 6.2 Interaction with Multi-Master Arbitration

Clock stretching and multi-master arbitration interact in subtle ways:

- During stretching, the losing master (monitoring the bus post-arbitration) must distinguish between SCL held LOW by the winning master and SCL held LOW by the slave.
- The winning master must also detect stretching and wait, which affects its timing.
- If both are occurring simultaneously (slave stretching during active arbitration), the clock sync mechanism naturally handles it — the bus SCL remains LOW until ALL holders release it.

### 6.3 Timeout Handling

Hardware I²C peripherals on modern MCUs (STM32, NXP LPC, etc.) typically implement a **bus timeout** (SCL low timeout), which generates an error if SCL is held LOW beyond a configurable threshold. In multi-master systems, this timeout must be tuned to accommodate the combined effects of clock sync delays and slave stretching.

---

## 7. Implementation in C/C++

### 7.1 Bit-Bang Multi-Master with Arbitration Detection

```c
/**
 * I2C Multi-Master Bit-Bang Implementation with Clock Synchronization
 * and Arbitration Detection
 *
 * Platform: Generic embedded C (adapt GPIO macros to your MCU)
 *
 * Assumptions:
 *   - SDA and SCL are open-drain GPIOs
 *   - Reading the pin returns the actual bus voltage
 *   - Setting pin HIGH = releasing (high-impedance), LOW = pulling down
 */

#include <stdint.h>
#include <stdbool.h>

/* ─── Platform GPIO Abstraction ─────────────────────────────────── */

typedef enum {
    I2C_OK             = 0,
    I2C_ERR_ARB_LOST   = 1,
    I2C_ERR_NACK       = 2,
    I2C_ERR_BUS_BUSY   = 3,
    I2C_ERR_TIMEOUT    = 4,
} i2c_status_t;

/* Implement these for your target platform */
extern void  sda_high(void);   /* Release SDA (let pull-up take it HIGH) */
extern void  sda_low(void);    /* Assert SDA LOW                         */
extern bool  sda_read(void);   /* Read actual SDA bus level              */
extern void  scl_high(void);   /* Release SCL                            */
extern void  scl_low(void);    /* Assert SCL LOW                         */
extern bool  scl_read(void);   /* Read actual SCL bus level              */
extern void  delay_half_bit(void); /* Delay for half a bit period        */

/* ─── Clock Synchronization ─────────────────────────────────────── */

/**
 * Drive SCL HIGH with clock synchronization.
 *
 * In multi-master mode, after releasing SCL we must wait until the
 * actual bus SCL goes HIGH. A slower master or a stretching slave
 * may hold it LOW.
 *
 * Returns false if the bus does not release within the timeout.
 */
static bool scl_sync_high(uint32_t timeout_us) {
    scl_high();  /* Release our pull — bus goes HIGH only if all release */

    uint32_t elapsed = 0;
    while (!scl_read()) {
        delay_half_bit();
        elapsed++;
        if (elapsed > timeout_us) {
            return false;  /* Timeout: slave stretching or bus fault */
        }
    }
    return true;  /* SCL is now HIGH on the bus */
}

/* ─── Start Condition ────────────────────────────────────────────── */

/**
 * Generate a START condition.
 *
 * In multi-master mode, we must first check that the bus is idle.
 * SDA and SCL must both be HIGH before we pull SDA LOW.
 */
static i2c_status_t i2c_start(void) {
    /* Check bus is idle */
    if (!sda_read() || !scl_read()) {
        return I2C_ERR_BUS_BUSY;
    }

    /* SDA falls while SCL is HIGH → START condition */
    sda_low();
    delay_half_bit();
    scl_low();
    delay_half_bit();

    return I2C_OK;
}

/* ─── Send One Bit with Arbitration Detection ────────────────────── */

/**
 * Transmit one bit on SDA and detect arbitration loss.
 *
 * Arbitration rule:
 *   We drive SDA HIGH (release), but observe SDA LOW on the bus.
 *   This means another master is driving it LOW — we lost arbitration.
 *
 * Returns I2C_ERR_ARB_LOST if arbitration is lost, I2C_OK otherwise.
 */
static i2c_status_t i2c_send_bit(bool bit) {
    /* Drive SDA to intended value */
    if (bit) {
        sda_high();  /* Release — pull-up takes it high if no one else drives */
    } else {
        sda_low();   /* Pull LOW */
    }

    delay_half_bit();

    /* Raise SCL with synchronization (handles clock stretching + multi-master) */
    if (!scl_sync_high(1000)) {
        return I2C_ERR_TIMEOUT;
    }

    delay_half_bit();

    /* ─── ARBITRATION CHECK ─────────────────────────────────────
     * Read SDA while SCL is HIGH. If we drove HIGH but bus is LOW,
     * another master won arbitration.
     */
    bool bus_sda = sda_read();
    if (bit && !bus_sda) {
        /* We intended HIGH, bus is LOW — arbitration lost */
        scl_low();
        return I2C_ERR_ARB_LOST;
    }

    /* Lower SCL — our LOW phase begins */
    scl_low();
    delay_half_bit();

    return I2C_OK;
}

/* ─── Send One Byte ──────────────────────────────────────────────── */

/**
 * Transmit one byte (MSB first) and receive the ACK bit.
 *
 * Returns:
 *   I2C_OK         — byte sent, ACK received
 *   I2C_ERR_NACK   — byte sent, NACK received
 *   I2C_ERR_ARB_LOST — arbitration lost mid-byte
 */
static i2c_status_t i2c_send_byte(uint8_t byte, bool *acked) {
    i2c_status_t status;

    for (int bit = 7; bit >= 0; bit--) {
        bool b = (byte >> bit) & 0x01;
        status = i2c_send_bit(b);
        if (status != I2C_OK) {
            return status;
        }
    }

    /* ─── ACK Phase ─────────────────────────────────────────────
     * Release SDA so the slave (or master in read mode) can ACK.
     * A LOW on SDA during ACK = ACK, HIGH = NACK.
     */
    sda_high();
    delay_half_bit();

    if (!scl_sync_high(1000)) {
        return I2C_ERR_TIMEOUT;
    }

    delay_half_bit();
    *acked = !sda_read();  /* LOW = ACK, HIGH = NACK */

    scl_low();
    delay_half_bit();

    return (*acked) ? I2C_OK : I2C_ERR_NACK;
}

/* ─── Stop Condition ─────────────────────────────────────────────── */

static void i2c_stop(void) {
    sda_low();
    delay_half_bit();
    scl_high();   /* No sync needed here — we own the bus */
    delay_half_bit();
    sda_high();   /* SDA rises while SCL is HIGH → STOP */
    delay_half_bit();
}

/* ─── High-Level Write Transaction ──────────────────────────────── */

/**
 * Write bytes to a slave at 7-bit address.
 *
 * If arbitration is lost at any point, the function returns
 * I2C_ERR_ARB_LOST and the caller should retry after a backoff delay.
 */
i2c_status_t i2c_master_write(uint8_t addr7, const uint8_t *data,
                               uint8_t len) {
    i2c_status_t status;
    bool acked;

    status = i2c_start();
    if (status != I2C_OK) return status;

    /* Address byte: [ADDR7 | ADDR6 | ... | ADDR1 | ADDR0 | R/W(0=write)] */
    uint8_t addr_byte = (addr7 << 1) | 0x00;
    status = i2c_send_byte(addr_byte, &acked);
    if (status != I2C_OK) goto cleanup;
    if (!acked) { status = I2C_ERR_NACK; goto cleanup; }

    for (uint8_t i = 0; i < len; i++) {
        status = i2c_send_byte(data[i], &acked);
        if (status != I2C_OK) goto cleanup;
        if (!acked) { status = I2C_ERR_NACK; goto cleanup; }
    }

cleanup:
    if (status != I2C_ERR_ARB_LOST) {
        /* Only send STOP if we still own the bus */
        i2c_stop();
    }
    /* If arbitration was lost, the winning master will send STOP */
    return status;
}
```

---

### 7.2 Arbitration Lost Retry with Exponential Backoff (C++)

```cpp
/**
 * I2C Multi-Master Retry Manager (C++)
 *
 * Implements exponential backoff with jitter on arbitration loss,
 * which is the recommended practice to prevent repeated collisions.
 */

#include <cstdint>
#include <cstdlib>      // rand()
#include <chrono>
#include <thread>
#include <functional>

class I2CMasterRetry {
public:
    struct Config {
        uint32_t max_retries        = 8;
        uint32_t base_backoff_us    = 100;   /* Initial backoff: 100µs */
        uint32_t max_backoff_us     = 10000; /* Cap at 10ms             */
        float    backoff_multiplier = 2.0f;  /* Exponential factor      */
        bool     use_jitter         = true;  /* Add random jitter       */
    };

    I2CMasterRetry(Config cfg = {}) : cfg_(cfg) {}

    /**
     * Execute an I2C transaction with automatic retry on arbitration loss.
     *
     * @param transaction  Callable returning i2c_status_t
     * @return Final status after all retries
     */
    i2c_status_t execute(std::function<i2c_status_t()> transaction) {
        uint32_t backoff_us = cfg_.base_backoff_us;
        uint32_t attempt    = 0;

        while (attempt <= cfg_.max_retries) {
            i2c_status_t result = transaction();

            if (result != I2C_ERR_ARB_LOST) {
                return result;  /* Success, NACK, timeout — not arb loss */
            }

            if (attempt == cfg_.max_retries) {
                return I2C_ERR_ARB_LOST;  /* Exhausted retries */
            }

            /* ── Exponential Backoff with Optional Jitter ──────────
             *
             * Jitter prevents the "thundering herd" problem where
             * two masters repeatedly collide at the same time because
             * they both back off by the exact same duration.
             *
             * Jitter: actual_delay = backoff * rand(0.5, 1.5)
             */
            uint32_t delay_us = backoff_us;
            if (cfg_.use_jitter) {
                float jitter = 0.5f + (float)(rand() % 100) / 100.0f;
                delay_us = (uint32_t)(backoff_us * jitter);
            }

            std::this_thread::sleep_for(
                std::chrono::microseconds(delay_us)
            );

            /* Double the backoff for next attempt, capped at max */
            backoff_us = std::min(
                (uint32_t)(backoff_us * cfg_.backoff_multiplier),
                cfg_.max_backoff_us
            );

            attempt++;
        }

        return I2C_ERR_ARB_LOST;
    }

private:
    Config cfg_;
};

/* ─── Usage Example ──────────────────────────────────────────────── */

void example_multi_master_write(void) {
    I2CMasterRetry::Config cfg;
    cfg.max_retries     = 5;
    cfg.base_backoff_us = 200;
    cfg.use_jitter      = true;

    I2CMasterRetry retrier(cfg);

    uint8_t data[] = {0x42, 0x55};

    i2c_status_t result = retrier.execute([&]() {
        return i2c_master_write(0x48, data, sizeof(data));
    });

    if (result == I2C_OK) {
        /* Transaction succeeded */
    } else if (result == I2C_ERR_ARB_LOST) {
        /* All retries exhausted — log error, alert system */
    }
}
```

---

### 7.3 Hardware I²C Peripheral — STM32 HAL Arbitration Handling

```c
/**
 * STM32 HAL-Based Multi-Master I2C with Arbitration Lost ISR
 *
 * Uses STM32's hardware I2C peripheral (I2C1) with the HAL library.
 * The peripheral automatically handles clock sync and arbitration,
 * and sets the ARLO bit in SR1 when arbitration is lost.
 */

#include "stm32f4xx_hal.h"
#include <string.h>

#define I2C_TIMEOUT_MS  100
#define MAX_RETRIES     5

static I2C_HandleTypeDef hi2c1;
static volatile bool arb_lost_flag = false;

/* ─── I²C Error Callback (called from HAL ISR) ───────────────────── */

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C1) {
        uint32_t error = HAL_I2C_GetError(hi2c);

        if (error & HAL_I2C_ERROR_ARLO) {
            /* Arbitration Lost — hardware has already released bus */
            arb_lost_flag = true;

            /*
             * IMPORTANT: When ARLO is set, the hardware automatically:
             *   1. Releases SDA and SCL
             *   2. Switches to slave mode
             *   3. Sets ARLO bit in SR1
             *
             * We must NOT generate a STOP condition here.
             * The winning master will generate STOP.
             */
        }
    }
}

/* ─── Multi-Master Write with Retry ─────────────────────────────── */

HAL_StatusTypeDef i2c_multimaster_write(uint16_t dev_addr,
                                         uint8_t  reg_addr,
                                         uint8_t  *data,
                                         uint16_t  len) {
    uint8_t buf[len + 1];
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);

    uint32_t backoff_ms = 1;

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        arb_lost_flag = false;

        HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
            &hi2c1, dev_addr, buf, len + 1, I2C_TIMEOUT_MS
        );

        if (status == HAL_OK) {
            return HAL_OK;  /* Success */
        }

        if (arb_lost_flag) {
            /*
             * Arbitration lost — wait for the winning master to finish
             * (observe the STOP condition), then retry.
             *
             * Wait until bus is free: both SDA and SCL go HIGH.
             * The HAL's IsDeviceReady can be used as a bus-idle probe.
             */
            HAL_Delay(backoff_ms);
            backoff_ms = (backoff_ms * 2 > 50) ? 50 : backoff_ms * 2;
            continue;
        }

        /* Other error (NACK, timeout, bus error) — do not retry */
        return status;
    }

    return HAL_ERROR;
}

/* ─── Initialization ─────────────────────────────────────────────── */

void i2c_init(void) {
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 400000;    /* Fast mode: 400 kHz */
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0x00;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    HAL_I2C_Init(&hi2c1);

    /* Enable ARLO interrupt for arbitration-lost detection */
    __HAL_I2C_ENABLE_IT(&hi2c1, I2C_IT_ERR);
}
```

---

## 8. Implementation in Rust

### 8.1 Bit-Bang I²C Multi-Master in Rust (embedded-hal)

```rust
//! I2C Multi-Master Bit-Bang Driver (Rust / embedded-hal)
//!
//! Uses embedded-hal traits for GPIO, making it portable across
//! MCU families supported by the embedded-hal ecosystem.
//!
//! Cargo.toml dependencies:
//!   embedded-hal = "1.0"

use embedded_hal::digital::{InputPin, OutputPin, PinState};
use core::fmt;

/// Errors that can occur during an I2C transaction
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum I2cError {
    ArbitrationLost,
    Nack,
    BusBusy,
    Timeout,
    GpioError,
}

impl fmt::Display for I2cError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            I2cError::ArbitrationLost => write!(f, "I2C: Arbitration lost"),
            I2cError::Nack            => write!(f, "I2C: NACK received"),
            I2cError::BusBusy         => write!(f, "I2C: Bus busy"),
            I2cError::Timeout         => write!(f, "I2C: Timeout"),
            I2cError::GpioError       => write!(f, "I2C: GPIO error"),
        }
    }
}

/// Bit-bang I2C master with multi-master arbitration support
pub struct I2cMaster<SDA, SCL, DELAY>
where
    SDA:   InputPin + OutputPin,
    SCL:   InputPin + OutputPin,
    DELAY: FnMut(u32),  /* Delay in microseconds */
{
    sda:   SDA,
    scl:   SCL,
    delay: DELAY,
    half_bit_us: u32,
}

impl<SDA, SCL, DELAY> I2cMaster<SDA, SCL, DELAY>
where
    SDA:   InputPin + OutputPin,
    SCL:   InputPin + OutputPin,
    DELAY: FnMut(u32),
{
    pub fn new(sda: SDA, scl: SCL, delay: DELAY, freq_hz: u32) -> Self {
        let half_bit_us = 500_000 / freq_hz;  /* Half period in µs */
        I2cMaster { sda, scl, delay, half_bit_us }
    }

    // ── Low-Level GPIO Helpers ──────────────────────────────────────

    fn sda_high(&mut self) {
        let _ = self.sda.set_high();  /* Release → pull-up takes over */
    }

    fn sda_low(&mut self) {
        let _ = self.sda.set_low();
    }

    fn sda_read(&mut self) -> bool {
        self.sda.is_high().unwrap_or(false)
    }

    fn scl_high(&mut self) {
        let _ = self.scl.set_high();
    }

    fn scl_low(&mut self) {
        let _ = self.scl.set_low();
    }

    fn scl_read(&mut self) -> bool {
        self.scl.is_high().unwrap_or(false)
    }

    fn delay(&mut self) {
        (self.delay)(self.half_bit_us);
    }

    // ── Clock Synchronization ───────────────────────────────────────

    /// Release SCL and wait until the bus actually goes HIGH.
    ///
    /// Handles both slave clock stretching and multi-master clock
    /// synchronization (another master holding SCL LOW in its own
    /// LOW period).
    fn scl_sync_high(&mut self, timeout_ticks: u32) -> Result<(), I2cError> {
        self.scl_high();

        for _ in 0..timeout_ticks {
            if self.scl_read() {
                return Ok(());
            }
            (self.delay)(1);
        }

        Err(I2cError::Timeout)
    }

    // ── Start Condition ─────────────────────────────────────────────

    fn start(&mut self) -> Result<(), I2cError> {
        /* Verify bus is idle */
        if !self.sda_read() || !self.scl_read() {
            return Err(I2cError::BusBusy);
        }

        /* SDA falls while SCL is HIGH */
        self.sda_low();
        self.delay();
        self.scl_low();
        self.delay();

        Ok(())
    }

    // ── Stop Condition ──────────────────────────────────────────────

    fn stop(&mut self) {
        self.sda_low();
        self.delay();
        self.scl_high();
        self.delay();
        self.sda_high();    /* SDA rises while SCL is HIGH → STOP */
        self.delay();
    }

    // ── Send One Bit with Arbitration Detection ─────────────────────

    /// Drive one bit onto SDA and check for arbitration loss.
    ///
    /// # Arbitration Detection
    /// After releasing SCL HIGH, we sample SDA. If we intended to
    /// drive HIGH (released SDA) but the bus reads LOW, another
    /// master is driving it — we have lost arbitration.
    fn send_bit(&mut self, bit: bool) -> Result<(), I2cError> {
        if bit { self.sda_high(); } else { self.sda_low(); }
        self.delay();

        /* Wait for SCL to go HIGH — synchronizes with other masters */
        self.scl_sync_high(1000)?;
        self.delay();

        /* ── Arbitration Check ─────────────────────────────────────
         *
         * If we drove HIGH but bus shows LOW → arbitration lost.
         * The winning master's LOW dominates due to wired-AND.
         */
        let bus_sda = self.sda_read();
        if bit && !bus_sda {
            self.scl_low();
            return Err(I2cError::ArbitrationLost);
        }

        self.scl_low();
        self.delay();

        Ok(())
    }

    // ── Send One Byte ───────────────────────────────────────────────

    fn send_byte(&mut self, byte: u8) -> Result<bool, I2cError> {
        /* Transmit MSB first */
        for bit_pos in (0..8).rev() {
            let bit = (byte >> bit_pos) & 0x01 != 0;
            self.send_bit(bit)?;
        }

        /* ACK phase: release SDA, slave pulls it LOW to ACK */
        self.sda_high();
        self.delay();
        self.scl_sync_high(1000)?;
        self.delay();

        let acked = !self.sda_read();  /* LOW = ACK, HIGH = NACK */

        self.scl_low();
        self.delay();

        Ok(acked)
    }

    // ── Receive One Byte ────────────────────────────────────────────

    fn recv_byte(&mut self, send_ack: bool) -> Result<u8, I2cError> {
        let mut byte = 0u8;
        self.sda_high();  /* Release SDA for slave to drive */

        for bit_pos in (0..8).rev() {
            self.delay();
            self.scl_sync_high(1000)?;
            self.delay();

            if self.sda_read() {
                byte |= 1 << bit_pos;
            }

            self.scl_low();
        }

        /* Send ACK or NACK to slave */
        if send_ack { self.sda_low(); } else { self.sda_high(); }
        self.delay();
        self.scl_sync_high(1000)?;
        self.delay();
        self.scl_low();
        self.delay();
        self.sda_high();

        Ok(byte)
    }

    // ── Public API ──────────────────────────────────────────────────

    /// Write bytes to a 7-bit addressed slave.
    ///
    /// Returns `Err(I2cError::ArbitrationLost)` if another master
    /// wins the bus. The caller should back off and retry.
    pub fn write(&mut self, addr: u8, data: &[u8]) -> Result<(), I2cError> {
        self.start()?;

        let addr_byte = (addr << 1) | 0x00;  /* Write flag = 0 */
        let acked = self.send_byte(addr_byte)?;
        if !acked {
            self.stop();
            return Err(I2cError::Nack);
        }

        for &byte in data {
            let acked = self.send_byte(byte)?;
            if !acked {
                self.stop();
                return Err(I2cError::Nack);
            }
        }

        self.stop();
        Ok(())
    }

    /// Read bytes from a 7-bit addressed slave.
    pub fn read(&mut self, addr: u8, buf: &mut [u8]) -> Result<(), I2cError> {
        self.start()?;

        let addr_byte = (addr << 1) | 0x01;  /* Read flag = 1 */
        let acked = self.send_byte(addr_byte)?;
        if !acked {
            self.stop();
            return Err(I2cError::Nack);
        }

        let last = buf.len() - 1;
        for (i, slot) in buf.iter_mut().enumerate() {
            let send_ack = i != last;  /* NACK on last byte */
            *slot = self.recv_byte(send_ack)?;
        }

        self.stop();
        Ok(())
    }
}
```

---

### 8.2 Arbitration-Aware Retry Manager in Rust

```rust
//! Retry manager for I2C multi-master arbitration loss
//!
//! Implements exponential backoff with LFSR-based pseudo-random jitter,
//! suitable for no_std embedded environments (no heap allocation, no rand crate).

/// Lightweight LFSR-based pseudo-random number generator (no_std compatible)
struct Lfsr(u16);

impl Lfsr {
    fn new(seed: u16) -> Self {
        Lfsr(if seed == 0 { 0xACE1 } else { seed })
    }

    /// Returns a pseudo-random u16
    fn next(&mut self) -> u16 {
        /* Galois LFSR with taps at bits 15, 13, 12, 10 */
        let lsb = self.0 & 1;
        self.0 >>= 1;
        if lsb != 0 {
            self.0 ^= 0xB400;
        }
        self.0
    }

    /// Returns a value in the range [min, max)
    fn range(&mut self, min: u32, max: u32) -> u32 {
        let r = self.next() as u32;
        min + (r % (max - min))
    }
}

/// Configuration for the retry manager
pub struct RetryConfig {
    pub max_attempts:    u32,
    pub base_backoff_us: u32,
    pub max_backoff_us:  u32,
    pub jitter_percent:  u32,  /* e.g. 50 means ±50% jitter */
}

impl Default for RetryConfig {
    fn default() -> Self {
        RetryConfig {
            max_attempts:    6,
            base_backoff_us: 200,
            max_backoff_us:  8_000,
            jitter_percent:  50,
        }
    }
}

/// Execute an I2C transaction with automatic retry on arbitration loss.
///
/// # Arguments
/// * `cfg`         - Retry configuration
/// * `delay_us`    - Platform delay function (microseconds)
/// * `transaction` - Closure returning `Result<(), I2cError>`
///
/// # Returns
/// `Ok(())` on success, or the last error after all retries are exhausted.
pub fn with_arbitration_retry<F, D>(
    cfg:         &RetryConfig,
    delay_us:    &mut D,
    transaction: &mut F,
) -> Result<(), I2cError>
where
    F: FnMut() -> Result<(), I2cError>,
    D: FnMut(u32),
{
    let mut lfsr    = Lfsr::new(0xDEAD);
    let mut backoff = cfg.base_backoff_us;

    for attempt in 0..cfg.max_attempts {
        match transaction() {
            Ok(())                         => return Ok(()),
            Err(I2cError::ArbitrationLost) => {
                if attempt + 1 == cfg.max_attempts {
                    return Err(I2cError::ArbitrationLost);
                }

                /* Apply jitter: scale backoff by a factor in
                 * [1.0 - jitter%, 1.0 + jitter%] */
                let scale_min = 100 - cfg.jitter_percent;
                let scale_max = 100 + cfg.jitter_percent + 1;
                let scale     = lfsr.range(scale_min, scale_max);
                let delay     = (backoff * scale) / 100;
                let delay     = delay.min(cfg.max_backoff_us);

                delay_us(delay);

                /* Double the base backoff for next attempt */
                backoff = (backoff * 2).min(cfg.max_backoff_us);
            }
            Err(e) => return Err(e),  /* Non-arbitration errors are not retried */
        }
    }

    Err(I2cError::ArbitrationLost)
}

// ── Usage Example ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_retry_succeeds_on_third_attempt() {
        let mut call_count = 0u32;
        let cfg = RetryConfig::default();
        let mut delay = |_us: u32| {};  /* No-op delay in tests */

        let result = with_arbitration_retry(
            &cfg,
            &mut delay,
            &mut || {
                call_count += 1;
                if call_count < 3 {
                    Err(I2cError::ArbitrationLost)
                } else {
                    Ok(())
                }
            },
        );

        assert_eq!(result, Ok(()));
        assert_eq!(call_count, 3);
    }

    #[test]
    fn test_nack_is_not_retried() {
        let mut call_count = 0u32;
        let cfg = RetryConfig::default();
        let mut delay = |_us: u32| {};

        let result = with_arbitration_retry(
            &cfg,
            &mut delay,
            &mut || {
                call_count += 1;
                Err(I2cError::Nack)
            },
        );

        assert_eq!(result, Err(I2cError::Nack));
        assert_eq!(call_count, 1);  /* Not retried */
    }
}
```

---

### 8.3 State Machine for Arbitration-Aware Master (Rust)

```rust
//! Non-blocking I2C master state machine
//! Suitable for use in interrupt-driven or RTOS task contexts.

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum MasterState {
    Idle,
    SendingStart,
    SendingAddress { byte: u8, bit: u8 },
    WaitingAddressAck,
    SendingData { index: usize, bit: u8 },
    WaitingDataAck { next_index: usize },
    ArbitrationLost,
    Done,
    Error(I2cError),
}

pub struct I2cStateMachine<'a> {
    state:    MasterState,
    addr:     u8,
    data:     &'a [u8],
    cur_byte: u8,
}

impl<'a> I2cStateMachine<'a> {
    pub fn new(addr: u8, data: &'a [u8]) -> Self {
        I2cStateMachine {
            state:    MasterState::Idle,
            addr,
            data,
            cur_byte: 0,
        }
    }

    /// Called on each SCL rising edge (from timer interrupt or polling loop)
    pub fn tick(&mut self, sda_bus: bool) -> MasterState {
        self.state = match self.state {
            MasterState::Idle => MasterState::SendingStart,

            MasterState::SendingStart => {
                let addr_byte = (self.addr << 1) | 0x00;
                MasterState::SendingAddress { byte: addr_byte, bit: 7 }
            }

            MasterState::SendingAddress { byte, bit } => {
                let drove_high = (byte >> bit) & 0x01 != 0;

                /* Arbitration check: drove HIGH, bus reads LOW */
                if drove_high && !sda_bus {
                    return MasterState::ArbitrationLost;
                }

                if bit == 0 {
                    MasterState::WaitingAddressAck
                } else {
                    MasterState::SendingAddress { byte, bit: bit - 1 }
                }
            }

            MasterState::WaitingAddressAck => {
                if sda_bus {
                    /* SDA HIGH during ACK = NACK */
                    MasterState::Error(I2cError::Nack)
                } else if self.data.is_empty() {
                    MasterState::Done
                } else {
                    self.cur_byte = self.data[0];
                    MasterState::SendingData { index: 0, bit: 7 }
                }
            }

            MasterState::SendingData { index, bit } => {
                let drove_high = (self.cur_byte >> bit) & 0x01 != 0;

                if drove_high && !sda_bus {
                    return MasterState::ArbitrationLost;
                }

                if bit == 0 {
                    MasterState::WaitingDataAck { next_index: index + 1 }
                } else {
                    MasterState::SendingData { index, bit: bit - 1 }
                }
            }

            MasterState::WaitingDataAck { next_index } => {
                if sda_bus {
                    MasterState::Error(I2cError::Nack)
                } else if next_index >= self.data.len() {
                    MasterState::Done
                } else {
                    self.cur_byte = self.data[next_index];
                    MasterState::SendingData { index: next_index, bit: 7 }
                }
            }

            other => other,  /* Terminal states */
        };

        self.state
    }

    pub fn is_done(&self) -> bool {
        matches!(
            self.state,
            MasterState::Done | MasterState::ArbitrationLost | MasterState::Error(_)
        )
    }

    pub fn result(&self) -> Result<(), I2cError> {
        match self.state {
            MasterState::Done              => Ok(()),
            MasterState::ArbitrationLost   => Err(I2cError::ArbitrationLost),
            MasterState::Error(e)          => Err(e),
            _                              => Err(I2cError::BusBusy),
        }
    }
}
```

---

## 9. Edge Cases and Pitfalls

### 9.1 Repeated START in Multi-Master

A Repeated START (`Sr`) is used for combined read-write transactions without releasing the bus. In multi-master mode, a repeated START also participates in arbitration — another master may issue a START at the same time and win the bus. Firmware must handle `Sr` generation with the same arbitration detection as a normal START.

### 9.2 General Call Address (0x00)

The general call address (`0x00`) is addressed to all slaves. If two masters both address `0x00` simultaneously, arbitration proceeds normally through the data byte. However, since all slaves respond to `0x00`, the SDA line during ACK will be pulled LOW by multiple slaves, which must not be misinterpreted.

### 9.3 10-Bit Addressing

In 10-bit addressing mode, the first byte has the pattern `11110XX` where `XX` are the two MSBs of the address. Arbitration during the first byte is determined by those two bits. The second byte contains the remaining 8 address bits and also participates in arbitration. This extends the arbitration phase by one byte.

### 9.4 Bus Lockup Detection

A common failure mode: if a master is reset mid-transaction, the slave may be holding SDA LOW (waiting for more clocks to complete a byte). This locks the bus. A multi-master system should implement:

1. **Bus lockup detection**: Check if SDA is stuck LOW at startup.
2. **Recovery**: Send up to 9 clock pulses on SCL until the slave releases SDA, then issue a STOP.

```c
/* Bus recovery: up to 9 clocks to free a stuck slave */
void i2c_bus_recovery(void) {
    for (int i = 0; i < 9 && !sda_read(); i++) {
        scl_high();
        delay_half_bit();
        scl_low();
        delay_half_bit();
    }
    /* Issue STOP to reset slave state */
    sda_low();
    delay_half_bit();
    scl_high();
    delay_half_bit();
    sda_high();
}
```

### 9.5 Pull-Up Resistor Sizing

In multi-master systems, pull-up resistors must be chosen to accommodate the combined capacitance and the desired bus speed. A common mistake is using resistors too large (bus rises too slowly), causing false arbitration losses because SCL/SDA do not reach the HIGH threshold within the expected window.

| Bus Speed    | Typical Pull-Up (3.3V) |
|-------------|------------------------|
| 100 kHz (Sm) | 4.7 kΩ – 10 kΩ        |
| 400 kHz (Fm) | 1 kΩ – 2.2 kΩ         |
| 1 MHz (Fm+)  | 470 Ω – 1 kΩ          |

---

## 10. Summary

| Concept                   | Key Principle                                                                    |
|--------------------------|---------------------------------------------------------------------------------|
| **Open-Drain Bus**        | Wired-AND ensures LOW always wins; fundamental to arbitration                   |
| **Clock Synchronization** | SCL HIGH = min of all masters' HIGH; SCL LOW = max of all masters' LOW          |
| **Clock Stretching**      | Slave holds SCL LOW; all masters must wait; handled by `scl_sync_high()`        |
| **Arbitration Detection** | Master reads SDA mid-HIGH SCL; if drove HIGH but sees LOW → arbitration lost    |
| **Lower Address Wins**    | More early LOW bits → wins wired-AND arbitration; lower addresses have priority |
| **Arbitration Loss**      | Losing master releases bus immediately, does NOT generate STOP                  |
| **Retry Strategy**        | Exponential backoff with jitter; prevents repeated simultaneous collisions      |
| **Bus Recovery**          | Up to 9 clock pulses to free a stuck slave; then generate STOP                  |
| **10-Bit Addressing**     | Arbitration extends across both address bytes                                   |
| **Repeated START**        | Also subject to arbitration; treat same as initial START in multi-master code   |

### Design Guidelines

- **Always check bus idle** before generating a START; never attempt to interrupt an active transaction.
- **Never generate a STOP** after losing arbitration; the winning master is responsible for the STOP.
- **Implement jitter** in retry delays to prevent resonant collisions between two masters.
- **Monitor the ARLO flag** on hardware peripherals; do not rely solely on software timeout detection.
- **Choose pull-up resistors** conservatively for your target bus frequency and total capacitance.
- **Validate bus recovery** at startup and after system resets in multi-master designs.
- **Design for address uniqueness** — if two masters frequently address the same slave, redesign the system to avoid data-phase arbitration which is harder to debug.

---

*I²C Multi-Master Clock Arbitration — Part of the I²C Protocol Deep Dive Series*