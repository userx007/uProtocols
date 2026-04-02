# 69. I2C Bus Isolation

**Structure (10 sections):**

1. **Introduction** — Why bus isolation is needed and where it's mandated (IEC 60601, IEC 61010, ISO 26262)
2. **Why Galvanic Isolation?** — Electrical safety, ground loop prevention, and fault containment rationale
3. **Isolation Technologies** — Optocouplers, capacitive digital isolators, transformer-based isolators, and isolated DC-DC power — with their tradeoffs
4. **I2C-Specific Challenges** — The bidirectional SDA problem, clock stretching, bus capacitance/rise times, and stuck bus recovery
5. **Isolated I2C Topologies** — Simple 2-wire, multi-slave with I2C hub, and alert-line variants
6. **Key ICs** — Comparison table: ISO1540, ISO1541, ADUM1250/1251, Si8622
7. **C/C++ Examples** — Four examples covering: bus recovery (STM32 HAL), OOP transaction wrapper with retry/reset, power-good checking, and timing validation
8. **Rust Examples** — Three examples covering: `embedded-hal` isolation wrapper with power-good and retry, isolated bus scanner, and TMP117 temperature sensor reading
9. **Safety & Certification** — Isolation voltage ratings, creepage/clearance, FMEA considerations
10. **Troubleshooting Table + Summary** — Common failure modes with causes and resolutions, plus 7 key takeaways


## Galvanic Isolation Techniques Using Digital Isolators for Safety-Critical Systems

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Galvanic Isolation?](#why-galvanic-isolation)
3. [Isolation Technologies](#isolation-technologies)
4. [I2C-Specific Isolation Challenges](#i2c-specific-isolation-challenges)
5. [Isolated I2C Topologies](#isolated-i2c-topologies)
6. [Key ICs and Components](#key-ics-and-components)
7. [Programming Examples in C/C++](#programming-examples-in-cc)
8. [Programming Examples in Rust](#programming-examples-in-rust)
9. [Safety and Certification Considerations](#safety-and-certification-considerations)
10. [Troubleshooting Isolated I2C Buses](#troubleshooting-isolated-i2c-buses)
11. [Summary](#summary)

---

## Introduction

In many embedded systems — particularly those involving high-voltage power supplies, medical devices, industrial controllers, or motor drives — the I2C bus must cross a **galvanic isolation barrier**. This barrier physically separates two electrical domains that share no common ground, protecting both the sensitive microcontroller circuitry and the human operators from dangerous voltages, ground loops, and transient surges.

Bus isolation is not merely a protection measure; in safety-critical applications it is often **mandated by regulatory standards** such as IEC 60601 (medical), IEC 61010 (industrial), UL 1577, and ISO 26262 (automotive). Understanding how to correctly implement isolated I2C communication — including the hardware constraints and firmware implications — is essential for robust and certifiable designs.

---

## Why Galvanic Isolation?

### Electrical Safety

When a microcontroller communicates with a sensor or actuator in a high-voltage domain (e.g., the primary side of a power supply, a motor inverter, or a mains-connected measurement circuit), direct electrical connection creates serious hazards:

- **Electric shock risk** to operators or patients
- **Latch-up or destruction** of the MCU from voltage spikes
- **Common-mode noise** corrupting data integrity

Galvanic isolation breaks the DC path between domains while still allowing signal transmission, typically via magnetic coupling (transformers, inductive), optical coupling (optocouplers), or capacitive coupling (digital isolators).

### Ground Loop Prevention

In distributed systems with long cable runs, ground potential differences between nodes can create **ground loops** that inject 50/60 Hz noise or high-frequency transients. Isolation eliminates the ground reference dependency entirely.

### Fault Containment

Isolation limits fault propagation. A catastrophic failure on the high-voltage side (e.g., a short circuit or insulation breakdown) does not immediately destroy the control-side electronics, giving the system time to detect and respond to the fault.

---

## Isolation Technologies

### 1. Optocouplers (Photocouplers)

Optocouplers use an LED and photodiode pair. They are well understood and inexpensive but have significant limitations for I2C:

- **Low bandwidth**: Typical optocouplers have propagation delays of 5–50 µs, making Standard Mode (100 kHz) difficult and Fast Mode (400 kHz) nearly impossible without careful biasing.
- **Non-linearity**: The LED/photodiode characteristic is temperature-dependent.
- **Not bidirectional**: SDA is bidirectional on I2C; two optocouplers and additional logic are needed per signal direction.

Suitable mainly for very low-speed I2C (< 20 kHz) or one-way data paths.

### 2. Digital (Capacitive) Isolators

Modern **digital isolators** (e.g., Texas Instruments ISO1540/ISO1541, Analog Devices ADUM1250/ADUM1251) use on-chip capacitors with differential encoding. Key advantages:

- **High speed**: Up to 1 MHz I2C (Fast Mode Plus) or beyond
- **Low propagation delay**: Typically 10–60 ns
- **Bidirectional SDA handling**: Dedicated I2C isolators handle the open-drain SDA line transparently
- **Integrated flow control**: Auto-direction detection on SDA
- **Low power**: Quiescent current in µA range

These are the preferred solution for modern isolated I2C designs.

### 3. Inductive (Transformer-Based) Isolators

Devices like Silicon Labs Si86xx or Analog Devices ADUM series also use transformer coupling (on-chip coreless transformers). They offer:

- Excellent **common-mode transient immunity (CMTI)**: 25–150 kV/µs
- Wide isolation voltage ratings (up to 5 kVrms)
- High data rates (up to 150 Mbps for non-I2C variants)

For I2C specifically, dedicated transformer-based I2C isolators handle the open-drain protocol correctly.

### 4. Isolated DC-DC Power

Often overlooked: the **isolated side needs power too**. Isolated I2C designs typically pair a signal isolator with an isolated DC-DC converter (e.g., Murata MGJ series, Würth WRB series, or a custom flyback) to provide power to the remote side without a galvanic path.

---

## I2C-Specific Isolation Challenges

I2C presents unique challenges compared to isolating a simple unidirectional signal:

### Bidirectional SDA Line

SDA is open-drain and bidirectional. Any device on the bus (master or slave) can pull SDA low. A naive isolator would create a **latch condition**: a LOW driven on one side propagates across, causes the other side to appear LOW, which re-drives LOW, holding the line stuck forever.

**Solution**: Dedicated I2C isolators have **special SDA logic** that:
- Detects the direction of each edge
- Prevents feedback loops by temporarily disabling the output while the input is being driven
- Typically implements a 1–3 µs "glitch filter" and a direction-lock timeout

### Clock Stretching

I2C slaves may hold SCL LOW to pause the master (clock stretching). An isolated SCL path must faithfully propagate this in **both directions** — from slave to master. Some isolators only buffer SCL in one direction, breaking clock stretching compatibility. Always verify datasheet specifications for bidirectional SCL support.

### Bus Capacitance and Rise Times

I2C uses passive pull-up resistors. The maximum bus capacitance is 400 pF (Standard/Fast Mode). Adding an isolator introduces its own capacitance on each side. The pull-up resistor on each side of the barrier must be sized **independently** to meet the rise time requirements of that segment.

Rise time constraint:

```
t_rise = 0.8473 × R_pull × C_bus
t_rise must be < 1000 ns (Standard), < 300 ns (Fast), < 120 ns (Fast+)
```

With typical isolator input capacitance of 5–15 pF and a slave adding 50–100 pF, values of 2.2 kΩ–4.7 kΩ are common for Standard Mode.

### Stuck Bus Recovery

If the isolated side loses power or the isolator resets mid-transaction, the SDA line may be held LOW permanently by a slave waiting to complete a byte. Recovery requires sending up to **9 SCL pulses** to flush the slave state machine, followed by a STOP condition. This must be implemented in firmware and is complicated by the isolation barrier.

---

## Isolated I2C Topologies

### Topology 1: Simple 2-Wire Isolated I2C

```
MCU Side (Domain A)           |  Sensor Side (Domain B)
                               | (barrier)
MCU ──── SDA_A ─── ISO ─── SDA_B ──── Sensor
MCU ──── SCL_A ─── ISO ─── SCL_B ──── Sensor
3.3V ─── R_A ──────────────────────────
                              3.3V_iso ─── R_B ──
GND_A                                      GND_B
```

The ISO block is typically a device like the TI ISO1540 which handles bidirectional SDA and unidirectional SCL (or bidirectional SCL for clock stretching). Separate pull-up resistors on each domain segment.

### Topology 2: Multi-Slave with I2C Hub + Isolator

When multiple isolated slaves exist, an I2C hub/mux (e.g., TI PCA9548A) on the isolated side allows the single isolator to serve multiple slaves, reducing cost:

```
MCU ── ISO ── I2C Hub ── Slave 0
                     ├── Slave 1
                     └── Slave 2
```

### Topology 3: Isolated I2C with Alert Line

Some I2C slaves have an interrupt/alert line (SMBUS Alert). This needs a **separate unidirectional isolator channel** for the alert signal, plus proper handling in firmware.

---

## Key ICs and Components

| Device | Type | VCC | Max Speed | SDA Handling | CMTI |
|---|---|---|---|---|---|
| TI ISO1540 | Capacitive | 1.71–5.5V | 1 MHz | Bidirectional auto | 25 kV/µs |
| TI ISO1541 | Capacitive | 1.71–5.5V | 1 MHz | Unidirectional SDA | 25 kV/µs |
| ADI ADUM1250 | Transformer | 3–5.5V | 1 MHz | Bidirectional | 25 kV/µs |
| ADI ADUM1251 | Transformer | 3–5.5V | 1 MHz | Split directional | 25 kV/µs |
| Si8622 | Transformer | 2.5–5.5V | — | General 2ch | 150 kV/µs |

For the **ISO1540** (a popular choice), the key electrical limits are:
- Working isolation voltage: 1500 Vrms (basic), 560 Vrms reinforced
- Input current drive: 3 mA sink, open-drain output
- Propagation delay: 15 ns typical

---

## Programming Examples in C/C++

The software side of isolated I2C is largely transparent — the isolator is invisible to the firmware when operating correctly. However, **initialization sequences, error recovery, and timing constraints** require careful attention.

### Example 1: I2C Initialization with Bus Recovery (C, STM32 HAL)

```c
/**
 * @file i2c_isolated.c
 * @brief Isolated I2C initialization and bus recovery for STM32
 *
 * On isolated I2C buses, a stuck SDA condition can persist across power
 * cycles of the remote side. This recovery sequence clocks out a stuck
 * slave before re-enabling the I2C peripheral.
 */

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define I2C_SCL_PIN     GPIO_PIN_6
#define I2C_SDA_PIN     GPIO_PIN_7
#define I2C_GPIO_PORT   GPIOB
#define I2C_RECOVERY_CLOCKS  9      /* Max clocks needed to unstick a slave */
#define I2C_RECOVERY_DELAY   5      /* µs half-period for recovery clocks   */

/**
 * @brief Perform I2C bus recovery by bit-banging SCL pulses.
 *
 * Switches SCL/SDA to GPIO mode, drives 9 clock pulses, then generates
 * a STOP condition. Finally reconfigures pins to I2C alternate function.
 */
static void i2c_bus_recover(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Reconfigure SCL and SDA as GPIO outputs (open-drain) */
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = I2C_SCL_PIN;
    HAL_GPIO_Init(I2C_GPIO_PORT, &gpio);

    gpio.Pin = I2C_SDA_PIN;
    HAL_GPIO_Init(I2C_GPIO_PORT, &gpio);

    /* Release both lines */
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN | I2C_SDA_PIN, GPIO_PIN_SET);
    HAL_Delay(1);

    /* Clock up to 9 pulses — releases any slave holding SDA LOW */
    for (uint8_t i = 0; i < I2C_RECOVERY_CLOCKS; i++)
    {
        /* Check if SDA is already released */
        if (HAL_GPIO_ReadPin(I2C_GPIO_PORT, I2C_SDA_PIN) == GPIO_PIN_SET)
            break;

        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
        /* Delay equivalent to I2C_RECOVERY_DELAY µs */
        for (volatile uint32_t d = 0; d < 840; d++) __NOP();

        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
        for (volatile uint32_t d = 0; d < 840; d++) __NOP();
    }

    /* Generate STOP condition: SDA LOW while SCL HIGH, then SDA HIGH */
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
    for (volatile uint32_t d = 0; d < 840; d++) __NOP();
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    for (volatile uint32_t d = 0; d < 840; d++) __NOP();
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, GPIO_PIN_SET);

    /* Reconfigure pins back to I2C alternate function */
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Alternate = GPIO_AF4_I2C1;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = I2C_SCL_PIN | I2C_SDA_PIN;
    HAL_GPIO_Init(I2C_GPIO_PORT, &gpio);

    HAL_Delay(1);
}

/**
 * @brief Initialize the I2C peripheral with pre-recovery.
 * @param  hi2c  Pointer to HAL I2C handle
 * @return HAL status
 */
HAL_StatusTypeDef i2c_isolated_init(I2C_HandleTypeDef *hi2c)
{
    /* Perform bus recovery before enabling peripheral */
    i2c_bus_recover();

    hi2c->Instance             = I2C1;
    hi2c->Init.ClockSpeed      = 100000;   /* 100 kHz — safe for isolated bus */
    hi2c->Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c->Init.OwnAddress1     = 0;
    hi2c->Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c->Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    return HAL_I2C_Init(hi2c);
}
```

### Example 2: Isolated I2C Transaction with Timeout and Error Handling (C++)

```cpp
/**
 * @file isolated_i2c_master.cpp
 * @brief C++ wrapper for isolated I2C communication with robust error handling.
 *
 * Isolated buses can exhibit different failure modes than direct-connected
 * buses: the isolator may introduce additional propagation delay causing
 * marginal timing at high speeds, and power loss on the isolated side
 * causes immediate SDA/SCL indeterminate states.
 */

#include <cstdint>
#include <cstring>
#include "stm32f4xx_hal.h"

class IsolatedI2CMaster
{
public:
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 10;
    static constexpr uint8_t  MAX_RETRIES        = 3;

    /**
     * @brief Construct with reference to HAL I2C handle.
     *
     * The I2C peripheral must already be initialized at a speed compatible
     * with the chosen isolator. For ISO1540, up to 1 MHz is supported;
     * however, 100 kHz is recommended for long cable runs across the barrier.
     */
    explicit IsolatedI2CMaster(I2C_HandleTypeDef &handle)
        : m_hi2c(handle)
        , m_error_count(0)
    {}

    /**
     * @brief Write bytes to an isolated I2C device.
     *
     * @param  dev_addr  7-bit device address (not shifted)
     * @param  reg       Register address
     * @param  data      Pointer to data buffer
     * @param  len       Number of bytes to write
     * @return true on success, false on failure
     */
    bool write_register(uint8_t dev_addr, uint8_t reg,
                        const uint8_t *data, uint16_t len)
    {
        /* Prepend register address to data */
        uint8_t buf[len + 1];
        buf[0] = reg;
        std::memcpy(&buf[1], data, len);

        for (uint8_t attempt = 0; attempt < MAX_RETRIES; ++attempt)
        {
            HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
                &m_hi2c,
                static_cast<uint16_t>(dev_addr << 1),
                buf,
                static_cast<uint16_t>(len + 1),
                DEFAULT_TIMEOUT_MS
            );

            if (status == HAL_OK)
                return true;

            /*
             * On an isolated bus, HAL_BUSY often means the isolator is
             * reflecting a glitch or the remote side lost power. We reset
             * the I2C peripheral and retry rather than failing immediately.
             */
            handle_error(status);
        }

        ++m_error_count;
        return false;
    }

    /**
     * @brief Read bytes from an isolated I2C device.
     *
     * @param  dev_addr  7-bit device address
     * @param  reg       Register address
     * @param  data      Output buffer
     * @param  len       Number of bytes to read
     * @return true on success
     */
    bool read_register(uint8_t dev_addr, uint8_t reg,
                       uint8_t *data, uint16_t len)
    {
        for (uint8_t attempt = 0; attempt < MAX_RETRIES; ++attempt)
        {
            /* Write register pointer */
            HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
                &m_hi2c,
                static_cast<uint16_t>(dev_addr << 1),
                &reg, 1,
                DEFAULT_TIMEOUT_MS
            );

            if (status != HAL_OK) { handle_error(status); continue; }

            /*
             * Repeated START (Sr). Note: on isolated buses with capacitive
             * isolators, the repeated start propagates normally. With some
             * older optocoupler designs the repeated START may be filtered;
             * in that case, a full STOP + START sequence is safer.
             */
            status = HAL_I2C_Master_Receive(
                &m_hi2c,
                static_cast<uint16_t>((dev_addr << 1) | 0x01),
                data, len,
                DEFAULT_TIMEOUT_MS
            );

            if (status == HAL_OK)
                return true;

            handle_error(status);
        }

        ++m_error_count;
        return false;
    }

    uint32_t error_count() const { return m_error_count; }

private:
    I2C_HandleTypeDef &m_hi2c;
    uint32_t           m_error_count;

    /**
     * @brief Reset the I2C peripheral after an error.
     *
     * Critical for isolated buses: HAL_I2C_DeInit + HAL_I2C_Init fully
     * resets the peripheral state machine. Without this, a BUSY error from
     * an isolator glitch can permanently stall the peripheral.
     */
    void handle_error(HAL_StatusTypeDef /*status*/)
    {
        HAL_I2C_DeInit(&m_hi2c);
        HAL_Delay(1);
        HAL_I2C_Init(&m_hi2c);
        HAL_Delay(1);
    }
};
```

### Example 3: Verifying Isolator Power and Link Integrity (C)

```c
/**
 * @brief Check isolator readiness before attempting I2C transactions.
 *
 * Some digital isolators (e.g., ISO1540) have an active-low ENABLE pin
 * or a power-good signal from the associated isolated DC-DC converter.
 * This function checks those signals before enabling the I2C peripheral.
 *
 * @return true if isolated side is powered and ready
 */

#include <stdbool.h>
#include "stm32f4xx_hal.h"

#define ISO_PGOOD_PIN    GPIO_PIN_3
#define ISO_PGOOD_PORT   GPIOA

bool isolated_bus_ready(void)
{
    /*
     * The isolated DC-DC converter's power-good output goes HIGH when
     * the isolated rail (e.g., 3.3V_ISO) is within regulation.
     * Without this rail, SDA/SCL pull-ups on the remote side are absent,
     * causing every I2C transaction to fail with NACK or timeout.
     */
    if (HAL_GPIO_ReadPin(ISO_PGOOD_PORT, ISO_PGOOD_PIN) == GPIO_PIN_RESET)
    {
        return false;   /* Isolated side is unpowered */
    }

    /*
     * Allow the isolator and remote-side MCU/peripherals to settle
     * after power-good asserts. Typical DC-DC startup + isolator
     * propagation delay: 1–10 ms depending on components.
     */
    HAL_Delay(5);

    /*
     * Perform a general-call or known-address probe to verify link.
     * I2C_IsDeviceReady sends repeated START + address + checks ACK.
     */
    extern I2C_HandleTypeDef hi2c1;
    HAL_StatusTypeDef result = HAL_I2C_IsDeviceReady(
        &hi2c1,
        0x48 << 1,   /* Example: temperature sensor at 0x48 */
        3,           /* 3 trials */
        10           /* 10 ms timeout */
    );

    return (result == HAL_OK);
}
```

### Example 4: Isolated I2C at Fast Mode (400 kHz) — Timing Validation (C++)

```cpp
/**
 * @brief Validate that the I2C bus parameters meet Fast Mode timing
 *        requirements for an isolated bus segment.
 *
 * On an isolated bus, the isolator adds propagation delay t_pd (typically
 * 15–60 ns). This must be accounted for in the setup and hold time budget.
 *
 * I2C Fast Mode timing requirements (from spec):
 *   t_SU;STA  >= 600 ns   (Setup time for START)
 *   t_HD;DAT  >= 0 ns     (Data hold time)
 *   t_SU;DAT  >= 100 ns   (Data setup time)
 *   t_r       <= 300 ns   (Rise time)
 *   t_f       <= 300 ns   (Fall time)
 *
 * With an isolator adding t_pd on both SCL and SDA paths, effective
 * setup and hold times are reduced by t_pd.
 */

#include <cstdint>
#include <cstdio>

struct IsoI2CTimingParams
{
    float r_pull_ohm;      /* Pull-up resistor on isolated segment (Ω) */
    float c_bus_pF;        /* Total bus capacitance on isolated segment (pF) */
    float t_pd_ns;         /* Isolator propagation delay (ns) */
    float vdd_v;           /* Supply voltage of isolated side (V) */
    float vil_threshold_v; /* LOW input threshold (typically 0.3×VDD) */
};

struct TimingResult
{
    float t_rise_ns;
    float t_fall_ns;
    bool  meets_fast_mode;
    bool  meets_standard_mode;
    const char *recommendation;
};

/**
 * Calculate bus timing margins for an isolated I2C segment.
 * Uses the standard RC rise time formula with 0→VDD×0.7 threshold.
 */
TimingResult validate_isolated_i2c_timing(const IsoI2CTimingParams &p)
{
    TimingResult result{};

    /*
     * RC rise time to 70% of VDD (I2C uses V_IH = 0.7×VDD as HIGH threshold)
     * t_rise = -ln(1 - 0.7) × R × C = 1.2039 × R × C
     * Simplified to 0.8473 × R × C for 10%–90% transition (common approx)
     */
    result.t_rise_ns = 0.8473f
                       * p.r_pull_ohm
                       * (p.c_bus_pF * 1e-12f)
                       * 1e9f;   /* Convert to ns */

    /* Fall time is fast (driven by open-drain FET); typically 10–30 ns */
    result.t_fall_ns = 20.0f;

    /*
     * Effective data setup time after isolator delay.
     * If t_pd is large, the isolator's output changes late relative to
     * the clock edge — tightening the setup time window.
     */
    float t_setup_effective_ns = 100.0f - p.t_pd_ns;

    result.meets_fast_mode =
        (result.t_rise_ns <= 300.0f) &&
        (result.t_fall_ns <= 300.0f) &&
        (t_setup_effective_ns >= 0.0f);

    result.meets_standard_mode =
        (result.t_rise_ns <= 1000.0f) &&
        (t_setup_effective_ns >= 0.0f);

    if (result.meets_fast_mode)
        result.recommendation = "OK for Fast Mode (400 kHz)";
    else if (result.meets_standard_mode)
        result.recommendation = "Reduce pull-up or capacitance for Fast Mode";
    else
        result.recommendation = "Bus too slow — check component values";

    printf("Isolated I2C Timing Analysis:\n");
    printf("  Rise time:          %.1f ns  (limit: 300 ns FM, 1000 ns SM)\n",
           result.t_rise_ns);
    printf("  Effective setup:    %.1f ns  (must be >= 0)\n",
           t_setup_effective_ns);
    printf("  Recommendation:     %s\n", result.recommendation);

    return result;
}

/* Usage example */
int main(void)
{
    IsoI2CTimingParams params = {
        .r_pull_ohm      = 2200.0f,  /* 2.2 kΩ pull-up on isolated side */
        .c_bus_pF        = 80.0f,    /* Isolator (10pF) + sensor (70pF)  */
        .t_pd_ns         = 15.0f,    /* ISO1540 typical propagation delay */
        .vdd_v           = 3.3f,
        .vil_threshold_v = 0.99f     /* 0.3 × 3.3V */
    };

    TimingResult r = validate_isolated_i2c_timing(params);
    return r.meets_fast_mode ? 0 : 1;
}
```

---

## Programming Examples in Rust

Rust's embedded ecosystem (the `embedded-hal` crate family) abstracts hardware I2C behind traits, making isolated bus handling primarily a matter of implementing the right error recovery and initialization sequences.

### Example 5: Isolated I2C Wrapper in Rust (`embedded-hal`)

```rust
//! isolated_i2c.rs
//!
//! Provides a wrapper around an `embedded-hal` I2C bus that adds:
//!   - Power-good signal checking before transactions
//!   - Automatic retry with peripheral reset on error
//!   - Bus recovery (9-clock bit-bang) via GPIO access
//!
//! Compatible with any HAL that implements `embedded_hal::i2c::I2c`
//! and `embedded_hal::digital::OutputPin` / `InputPin`.

#![no_std]

use embedded_hal::i2c::{I2c, SevenBitAddress, Error, ErrorKind};
use embedded_hal::digital::{InputPin, OutputPin};

/// Maximum number of transaction retry attempts on an isolated bus.
const MAX_RETRIES: u8 = 3;

/// Number of SCL recovery clocks to send when the bus is stuck.
const RECOVERY_CLOCKS: u8 = 9;

/// Errors specific to an isolated I2C bus.
#[derive(Debug)]
pub enum IsoI2CError<E> {
    /// The isolated power domain is not ready (DC-DC not locked).
    IsolatedPowerNotReady,
    /// The underlying I2C peripheral returned an error after all retries.
    BusError(E),
    /// Bus recovery was attempted but SDA did not release.
    RecoveryFailed,
}

/// Wraps an I2C bus with isolation-aware error handling.
///
/// `I` is the underlying I2C implementation.
/// `P` is the power-good input pin type.
/// `SCL` and `SDA` are GPIO output types for bit-bang recovery.
pub struct IsolatedI2C<I, P, SCL, SDA>
where
    I:   I2c<SevenBitAddress>,
    P:   InputPin,
    SCL: OutputPin,
    SDA: OutputPin,
{
    bus:       I,
    pgood:     P,
    scl_gpio:  SCL,
    sda_gpio:  SDA,
    error_count: u32,
}

impl<I, P, SCL, SDA> IsolatedI2C<I, P, SCL, SDA>
where
    I:   I2c<SevenBitAddress>,
    P:   InputPin,
    SCL: OutputPin,
    SDA: OutputPin,
{
    /// Create a new isolated I2C wrapper.
    ///
    /// # Arguments
    ///
    /// * `bus`      - The underlying I2C peripheral handle
    /// * `pgood`    - Input pin connected to the DC-DC power-good output
    /// * `scl_gpio` - GPIO output for bit-bang bus recovery (same pin as SCL)
    /// * `sda_gpio` - GPIO output for bit-bang bus recovery (same pin as SDA)
    pub fn new(bus: I, pgood: P, scl_gpio: SCL, sda_gpio: SDA) -> Self {
        Self {
            bus,
            pgood,
            scl_gpio,
            sda_gpio,
            error_count: 0,
        }
    }

    /// Check whether the isolated power domain is active.
    fn isolated_power_ready(&mut self) -> bool {
        // Power-good is active-high in this example
        self.pgood.is_high().unwrap_or(false)
    }

    /// Perform bit-bang bus recovery.
    ///
    /// This function assumes the GPIO pins are already configured as
    /// open-drain outputs. The caller is responsible for switching
    /// the MCU pin mux between I2C alternate function and GPIO mode.
    ///
    /// In a real HAL this would use `AnyPin::into_output_od()` or similar.
    fn bus_recovery(&mut self, delay_fn: &mut dyn FnMut()) {
        // Release both lines
        let _ = self.scl_gpio.set_high();
        let _ = self.sda_gpio.set_high();
        delay_fn();

        for _ in 0..RECOVERY_CLOCKS {
            // Drive SCL low
            let _ = self.scl_gpio.set_low();
            delay_fn();
            // Drive SCL high
            let _ = self.scl_gpio.set_high();
            delay_fn();
        }

        // Generate STOP: SDA low while SCL high, then SDA high
        let _ = self.sda_gpio.set_low();
        delay_fn();
        let _ = self.scl_gpio.set_high();
        delay_fn();
        let _ = self.sda_gpio.set_high();
        delay_fn();
    }

    /// Write data to a register on an isolated I2C device.
    ///
    /// Checks power-good before the first attempt and retries up to
    /// `MAX_RETRIES` times on failure.
    pub fn write_register(
        &mut self,
        addr: u8,
        reg:  u8,
        data: &[u8],
    ) -> Result<(), IsoI2CError<I::Error>>
    {
        if !self.isolated_power_ready() {
            return Err(IsoI2CError::IsolatedPowerNotReady);
        }

        // Build write buffer: [register, data...]
        // Using a fixed-size stack buffer; adjust MAX_WRITE as needed.
        const MAX_WRITE: usize = 33; // 1 reg + 32 data bytes
        let total = data.len() + 1;
        assert!(total <= MAX_WRITE, "write exceeds buffer");

        let mut buf = [0u8; MAX_WRITE];
        buf[0] = reg;
        buf[1..total].copy_from_slice(data);

        for attempt in 0..MAX_RETRIES {
            match self.bus.write(addr, &buf[..total]) {
                Ok(()) => return Ok(()),
                Err(e) => {
                    self.error_count += 1;
                    if attempt == MAX_RETRIES - 1 {
                        return Err(IsoI2CError::BusError(e));
                    }
                    // On isolated buses, a BusError may mean the remote
                    // side briefly lost power or the isolator glitched.
                    // A short delay before retry is often sufficient.
                }
            }
        }
        unreachable!()
    }

    /// Read bytes from a register on an isolated I2C device.
    pub fn read_register(
        &mut self,
        addr: u8,
        reg:  u8,
        buf:  &mut [u8],
    ) -> Result<(), IsoI2CError<I::Error>>
    {
        if !self.isolated_power_ready() {
            return Err(IsoI2CError::IsolatedPowerNotReady);
        }

        for attempt in 0..MAX_RETRIES {
            // write_read performs: START + addr(W) + reg + Sr + addr(R) + read
            match self.bus.write_read(addr, &[reg], buf) {
                Ok(()) => return Ok(()),
                Err(e) => {
                    self.error_count += 1;
                    if attempt == MAX_RETRIES - 1 {
                        return Err(IsoI2CError::BusError(e));
                    }
                }
            }
        }
        unreachable!()
    }

    /// Return the accumulated error count since initialization.
    pub fn error_count(&self) -> u32 {
        self.error_count
    }
}
```

### Example 6: Scanning an Isolated I2C Bus in Rust

```rust
//! scan_isolated_bus.rs
//!
//! Scans all 7-bit I2C addresses on an isolated bus, printing which
//! devices respond. Includes power-ready check and graceful handling
//! of NACK responses (expected for unpopulated addresses).

#![no_std]
#![no_main]

use cortex_m_rt::entry;
use cortex_m_semihosting::hprintln;
use stm32f4xx_hal::{
    i2c::{I2c, Mode},
    pac,
    prelude::*,
};

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();
    let cp = cortex_m::peripheral::Peripherals::take().unwrap();

    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.freeze();

    let gpiob = dp.GPIOB.split();
    let scl = gpiob.pb6.into_alternate_open_drain::<4>();
    let sda = gpiob.pb7.into_alternate_open_drain::<4>();

    let mut i2c = dp.I2C1.i2c(
        (scl, sda),
        Mode::standard(100_000.Hz()),  // 100 kHz for isolated bus safety
        &clocks,
    );

    hprintln!("Scanning isolated I2C bus...").ok();

    let mut found = 0u8;

    // Scan addresses 0x08 through 0x77 (valid 7-bit I2C range)
    for addr in 0x08u8..=0x77 {
        // Attempt a 0-byte write (just address + ACK check)
        match i2c.write(addr, &[]) {
            Ok(()) => {
                hprintln!("  Device found at 0x{:02X}", addr).ok();
                found += 1;
            }
            Err(e) => {
                // NACK is expected and normal for unpopulated addresses.
                // Other errors (ArbitrationLoss, Bus) may indicate isolator
                // issues and should be logged separately.
                use stm32f4xx_hal::i2c::Error;
                match e {
                    Error::Nack => {} // Normal — no device at this address
                    _ => hprintln!("  Bus error at 0x{:02X}: {:?}", addr, e).ok().map(|_| ()),
                }
            }
        }
    }

    hprintln!("Scan complete. {} device(s) found.", found).ok();

    loop {
        cortex_m::asm::wfi();
    }
}
```

### Example 7: Isolated I2C Temperature Sensor Read (Rust, `embedded-hal`)

```rust
//! isolated_temp_sensor.rs
//!
//! Reads temperature from a TMP117 (or compatible) I2C sensor
//! on an isolated bus. Demonstrates power-domain startup sequence
//! and conversion of raw register values to physical units.
//!
//! The TMP117 uses a 16-bit signed temperature register:
//!   Temperature (°C) = raw_value × 0.0078125

#![no_std]

/// TMP117 I2C address with ADD0 pin to GND
const TMP117_ADDR: u8   = 0x48;
/// TMP117 temperature result register
const TMP117_TEMP_REG: u8 = 0x00;
/// Resolution: 7.8125 m°C per LSB (1/128 °C)
const TMP117_RESOLUTION: f32 = 0.0078125;

/// Result of a temperature read from the isolated sensor.
#[derive(Debug, Clone, Copy)]
pub struct TemperatureReading {
    pub celsius:    f32,
    pub raw:        i16,
    pub retries:    u8,
}

/// Read temperature from a TMP117 on an isolated I2C bus.
///
/// # Arguments
///
/// * `bus`   - Any type implementing `embedded_hal::i2c::I2c`
///
/// # Returns
///
/// `Ok(TemperatureReading)` on success, or an I2C error.
pub fn read_temperature<I>(bus: &mut I) -> Result<TemperatureReading, I::Error>
where
    I: embedded_hal::i2c::I2c,
{
    const RETRIES: u8 = 3;
    let mut last_err = None;

    for attempt in 0..RETRIES {
        let mut buf = [0u8; 2];

        match bus.write_read(TMP117_ADDR, &[TMP117_TEMP_REG], &mut buf) {
            Ok(()) => {
                // TMP117 stores temperature as big-endian signed 16-bit
                let raw = i16::from_be_bytes(buf);
                let celsius = (raw as f32) * TMP117_RESOLUTION;

                return Ok(TemperatureReading {
                    celsius,
                    raw,
                    retries: attempt,
                });
            }
            Err(e) => {
                last_err = Some(e);
                // On an isolated bus: after a failed read, the isolator
                // may need a brief idle period before the next attempt.
                // In a real system, insert a hardware delay here.
            }
        }
    }

    Err(last_err.unwrap())
}

/// Convert a raw TMP117 register value to Celsius and Fahrenheit.
///
/// Useful for display or logging without allocating a TemperatureReading.
pub fn raw_to_celsius(raw: i16) -> f32 {
    (raw as f32) * TMP117_RESOLUTION
}

pub fn celsius_to_fahrenheit(c: f32) -> f32 {
    c * 9.0 / 5.0 + 32.0
}
```

---

## Safety and Certification Considerations

### Isolation Voltage Ratings

Digital isolators are rated in two categories:

- **Working isolation voltage** (V_IORM or V_ISO): The maximum continuous RMS voltage across the barrier. For medical (2MOPP), this may need to be 4000 Vrms or higher.
- **Withstand voltage** (V_test): The 1-minute or 1-second test voltage used for production testing, typically 2× to 3× working voltage.

**Reinforced isolation** (providing 2× the protection of basic isolation) is required when the isolated barrier is the **only** protection between the operator and a hazardous voltage. Many digital isolators are only rated for basic isolation; reinforced-rated devices (e.g., ISO1540 reinforced grade) must be specifically selected.

### Creepage and Clearance

Beyond the IC itself, the PCB layout must maintain sufficient **creepage** (surface distance) and **clearance** (air gap) between conductors of the two domains. IEC 60601-1 requires:
- 8 mm creepage / 4 mm clearance for 250 V working, pollution degree 2

Place isolation slots (cutouts in the PCB) beneath the isolator to enforce minimum distances. Never route signal or power traces across the isolation barrier without crossing it through the approved components.

### FMEA and Fault Injection

In ISO 26262 or IEC 61508 designs, the isolation barrier must be analysed for:
- **Single-point failures**: Can isolator capacitor breakdown couple a fault voltage directly?
- **Diagnostic coverage**: Does the system detect if the isolator has failed?

Some designs add a secondary isolation monitor that compares voltages on both sides of the barrier through high-value resistors to detect breakdown.

---

## Troubleshooting Isolated I2C Buses

| Symptom | Likely Cause | Resolution |
|---|---|---|
| Bus always NACK | Isolated side unpowered | Check DC-DC power-good signal |
| Transactions work at 100 kHz but fail at 400 kHz | Isolator propagation delay too large for Fast Mode timing budget | Reduce speed, or use faster isolator |
| SDA stuck LOW after power cycle | Slave mid-transaction when power was lost | Perform 9-clock bus recovery |
| Intermittent errors, correlated with switching noise | Poor CMTI isolator selection | Replace with high-CMTI device (>100 kV/µs) |
| SCL stretching causes timeout | Isolator only passes SCL in one direction | Use bidirectional-SCL isolator (ISO1540, ADUM1250) |
| No communication across barrier | Missing pull-up resistors on isolated side | Add 2.2–4.7 kΩ to V_ISO on both SDA_B and SCL_B |
| Latch-up (both SDA sides stuck HIGH or LOW) | Feedback through non-I2C-aware isolator | Replace with I2C-specific isolator (handles open-drain bidirectionality) |

---

## Summary

I2C bus isolation using digital isolators is an essential technique for any design where the I2C communication path crosses a galvanic barrier — whether for safety, noise immunity, or fault containment. The key points are:

1. **Choose I2C-specific digital isolators** (e.g., TI ISO1540, ADI ADUM1250) rather than generic optocouplers or unidirectional isolators. These devices handle the bidirectional, open-drain nature of SDA correctly and support clock stretching.

2. **Isolate power as well as signals.** Every isolated segment requires its own regulated supply, typically from an isolated DC-DC converter. Check the power-good signal in firmware before initiating transactions.

3. **Account for isolator propagation delay** in timing budgets. At 400 kHz Fast Mode, a 60 ns propagation delay consumes a significant portion of the setup time margin. Validate rise times and setup times for each segment independently.

4. **Implement bus recovery in firmware.** After power loss on the isolated side or a mid-transaction reset, slaves may hold SDA LOW. Nine SCL clocks followed by a STOP condition are the standard recovery procedure and must be implemented via bit-banged GPIO before re-enabling the I2C peripheral.

5. **Design the PCB for the required isolation class.** Working voltage, creepage, clearance, and reinforced vs. basic isolation are physical PCB concerns that cannot be fixed in software. Route the isolation boundary clearly and enforce it with PCB slots.

6. **Choose the right speed for your environment.** For long cable runs, high noise environments, or maximum safety margin, 100 kHz Standard Mode across the isolator is far more robust than attempting 400 kHz or 1 MHz.

7. **In Rust and C/C++, the isolator is largely transparent** once correctly configured. The primary software concerns are: detecting power-domain readiness, retrying after transient failures, resetting the peripheral after hard faults, and implementing the bus recovery sequence.

By combining a well-chosen digital isolator IC, a properly designed PCB, isolated power, and robust firmware, isolated I2C communication can be made highly reliable even in the most demanding safety-critical applications.

---

*Document: 69 — Bus Isolation | Galvanic isolation techniques using digital isolators for safety-critical systems*