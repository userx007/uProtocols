# 79. Brown-Out Protection

**Conceptual depth:**
- What a brown-out is vs. a blackout, and its common causes
- Why I2C is uniquely vulnerable — mid-transaction corruption, SDA stuck-low bus lockup, peripheral state machine corruption, and threshold asymmetry between devices
- Hardware detection mechanisms: built-in BOD/BOR circuits, software ADC monitoring, and external supervisor ICs

**Hardware guidance:**
- BOD threshold configuration tables for STM32, AVR, ESP32, and RP2040
- Decoupling/bulk capacitance strategy, series resistors, and bus isolators

**C/C++ examples:**
1. STM32 — Programming option bytes to set the BOR level
2. STM32 — PVD (Power Voltage Detector) interrupt for early warning with clean I2C abort
3. AVR — Measuring VCC via the internal 1.1 V bandgap reference (no external hardware needed)
4. AVR — GPIO-based I2C bus recovery (9-clock SCL toggle + STOP condition)
5. Generic — Guarded transaction wrapper with retry and exponential back-off

**Rust examples:**
6. `embedded-hal` traits — `GuardedI2c<I2C>` struct that voltage-gates every transaction
7. Generic GPIO bus recovery using `InputPin`/`OutputPin` traits
8. Safe boot sequence with pre-init voltage check and per-loop transaction gating

**Summary:** Ties together the four layers — hardware, detection, protection, and recovery — into a coherent design philosophy.

## Detecting and Handling Voltage Drops During I2C Operation

---

## Table of Contents

1. [Introduction](#introduction)
2. [What is a Brown-Out Event?](#what-is-a-brown-out-event)
3. [Why Brown-Out is Particularly Dangerous for I2C](#why-brown-out-is-particularly-dangerous-for-i2c)
4. [Brown-Out Detection Mechanisms](#brown-out-detection-mechanisms)
5. [Hardware Considerations](#hardware-considerations)
6. [Programming Strategies](#programming-strategies)
7. [C/C++ Implementation](#cc-implementation)
8. [Rust Implementation](#rust-implementation)
9. [Recovery Procedures](#recovery-procedures)
10. [Platform-Specific Examples](#platform-specific-examples)
11. [Summary](#summary)

---

## Introduction

Brown-out protection is a critical but often overlooked aspect of robust I2C system design. While much attention is paid to signal integrity, bus speeds, and pull-up resistor values, voltage supply integrity is equally fundamental. A brown-out — a partial, temporary drop in supply voltage — can corrupt I2C transactions, lock up the bus, and leave connected peripherals in undefined states. Designing systems that gracefully detect, respond to, and recover from brown-out events is essential for any production-quality embedded application.

---

## What is a Brown-Out Event?

A **brown-out** (also called a voltage sag or supply droop) occurs when the supply voltage drops below the normal operating threshold but does not fall all the way to zero. Unlike a complete power failure (blackout), a brown-out is characterised by:

- A partial voltage reduction, typically still above the device's minimum reset threshold
- Transient or sustained duration (microseconds to seconds)
- Possible oscillation around a threshold level
- Different voltage levels seen by different ICs depending on their power rail topology

Common causes include:

- Inrush current from motors, relays, or high-power peripherals switching on
- Weak or ageing batteries
- Long or thin power supply wires with high resistance
- Shared power rails between digital and analogue loads
- USB power supply limitations (nominally 5 V but allowed as low as 4.75 V)
- Capacitor depletion during peak processing loads

A **Brown-Out Reset (BOR)** is the hardware mechanism built into many microcontrollers that triggers a controlled system reset when the supply voltage drops below a programmable threshold, preventing unpredictable behaviour.

---

## Why Brown-Out is Particularly Dangerous for I2C

The I2C protocol is a synchronous, multi-master serial bus that relies on precise clock and data signalling. Brown-out events are uniquely disruptive to I2C for several reasons:

### 1. Mid-Transaction Corruption

I2C transactions are not atomic from a power perspective. A voltage drop mid-transfer can corrupt a byte in flight — the receiver may latch incorrect data, interpret a clock edge incorrectly, or miss an ACK/NAK bit entirely. The result is silent data corruption or a hung transaction.

### 2. Bus Lockup (SDA Stuck Low)

The most common brown-out consequence for I2C is the **SDA stuck low** condition. If the bus voltage drops while a peripheral is driving SDA low (e.g., mid-byte), the peripheral may not release the line when power is restored. The master, upon restart, sees SDA asserted low and cannot generate a START condition, effectively dead-locking the bus.

### 3. Peripheral State Machine Corruption

I2C slave peripherals contain internal state machines. A voltage drop insufficient to trigger their internal POR (Power-On Reset) may leave their state machines in an intermediate, undefined state. The peripheral may be partially initialised, with registers holding stale or garbage values.

### 4. Threshold Asymmetry

In a mixed-voltage system (e.g., a 3.3 V microcontroller with 5 V sensors), different devices have different under-voltage lockout (UVLO) thresholds. A brown-out may reset the master while leaving slaves running (or vice versa), creating a desynchronised state.

### 5. Pull-up Resistor Voltage Dependency

I2C lines are pulled high by resistors tied to VCC (or a separate IOVCC). If VCC droops, the high-level voltage seen on SCL/SDA droops accordingly. This can violate the VIH (input high voltage) specification of receivers, causing them to misinterpret a logical high as a logical low.

---

## Brown-Out Detection Mechanisms

### Hardware Brown-Out Detectors (BOD)

Most modern microcontrollers include a built-in BOD circuit that:

- Continuously monitors the supply voltage with an internal comparator
- Compares VCC against a programmable reference voltage (threshold)
- Asserts a reset signal (or an interrupt) when VCC falls below threshold
- Holds the device in reset until voltage recovers (with hysteresis)

The BOD threshold is typically configurable via fuse bits (AVR), configuration words (PIC), or option bytes (STM32).

**Typical BOD threshold levels:**

| MCU Family | Typical Threshold Range |
|---|---|
| AVR (ATmega/ATtiny) | 1.8 V – 4.3 V (configurable via fuse bits) |
| STM32 | 2.0 V – 3.9 V (8 levels, programmable) |
| nRF52 series | 1.7 V – 3.6 V (configurable) |
| ESP32 | 2.43 V – 2.77 V (internal) |
| RP2040 | ~1.8 V (fixed, on-chip) |

### Software Voltage Monitoring

In addition to hardware BOD, software can monitor supply voltage using:

- An internal ADC channel connected to an internal bandgap reference (gives a ratio-metric measure of VCC)
- An external voltage divider feeding an ADC input
- An external supervisory IC (e.g., TI TLV803, Maxim MAX809) with a dedicated reset output

### External Supervisor ICs

Dedicated voltage supervisor ICs offer:

- Faster response time than software polling (typically < 1 µs)
- Lower power consumption
- Separate RESET output to drive MCU and peripheral reset pins simultaneously
- Adjustable thresholds via external resistors
- Watchdog functionality combined with voltage monitoring

---

## Hardware Considerations

### Decoupling Capacitors

Place adequate decoupling capacitors (100 nF ceramic + 10 µF bulk) as close as possible to each device's VCC pin. This provides local energy storage to ride through short brown-out transients before the BOD fires.

### Separate Power Domains

Use separate LDO regulators for the MCU core, I/O, and sensitive peripherals. A local UVLO (under-voltage lockout) on each domain ensures that each subsystem is properly powered before enabling the I2C bus.

### Bulk Capacitance on the I2C VCC Rail

Add bulk capacitance (47 µF–470 µF electrolytic or tantalum) on the supply rail that powers both the I2C master and slaves. This helps absorb transient current spikes and prevents the rail from collapsing under brief loads.

### Series Resistors on SCL/SDA

Low-value series resistors (10 Ω–47 Ω) on SCL and SDA lines can limit current and reduce ringing, but more importantly they slow signal edges enough to prevent glitches from being interpreted as valid clock edges during a supply transient.

### Bus Isolators

In fault-tolerant systems, consider I2C bus buffers or isolators (e.g., PCA9517, TCA9544A) that can be disabled by firmware during a detected brown-out, preventing a locked bus from propagating across sections of the design.

---

## Programming Strategies

### Strategy 1: BOD-Triggered Reset with I2C Re-initialisation

The simplest approach: configure the hardware BOD to reset the MCU at an appropriate voltage level, then ensure that the start-up code always re-initialises the I2C peripheral and performs an I2C bus recovery sequence before beginning normal operation.

### Strategy 2: Interrupt-Driven Graceful Shutdown

Some MCUs offer a brown-out early warning interrupt (before full reset) that gives firmware a short window (typically microseconds to milliseconds depending on capacitor hold-up time) to:

- Abort any pending I2C transaction cleanly (STOP condition)
- Save critical state to non-volatile memory (EEPROM/Flash)
- Tri-state or disable the I2C peripheral
- Signal downstream devices of imminent shutdown

### Strategy 3: Continuous Software Voltage Monitoring

Use a timer interrupt to periodically sample the supply voltage (via ADC). If voltage falls below a software-defined threshold (above the hardware BOD threshold), proactively:

- Pause I2C transactions between operations
- Retry failed transactions with exponential back-off
- Flag the system as "degraded" and reduce operating frequency

### Strategy 4: I2C Bus Recovery After Power Restore

After any reset (hardware or software), execute a standardised bus recovery procedure before enabling the I2C peripheral:

1. Configure SCL and SDA as GPIO outputs
2. Toggle SCL nine times (clock out any stuck byte)
3. Assert a STOP condition (SDA low → high while SCL high)
4. Re-enable the I2C peripheral
5. Re-initialise all I2C slaves

### Strategy 5: Transaction-Level Voltage Gating

Perform a voltage check before each I2C transaction. If voltage is below a safe threshold, defer the transaction and wait. This is particularly important for write operations to EEPROM or configuration registers where a corrupted write could cause lasting damage.

---

## C/C++ Implementation

### Example 1: STM32 — Configuring the Brown-Out Reset Level

```c
/**
 * STM32 Brown-Out Reset (BOR) Configuration
 *
 * STM32 BOR levels (for STM32F4):
 *   OB_BOR_LEVEL1 : 2.10V – 2.40V
 *   OB_BOR_LEVEL2 : 2.40V – 2.70V
 *   OB_BOR_LEVEL3 : 2.70V – 3.60V
 *   OB_BOR_OFF    : BOR disabled (only POR/PDR active)
 */
#include "stm32f4xx_hal.h"

HAL_StatusTypeDef configure_bor_level(void)
{
    FLASH_OBProgramInitTypeDef ob_init = {0};

    /* Unlock option bytes */
    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();

    /* Read current option bytes */
    HAL_FLASHEx_OBGetConfig(&ob_init);

    /* Set BOR level to 2 (VCC must be >= 2.4V for reliable I2C at 3.3V logic) */
    ob_init.OptionType = OPTIONBYTE_BOR;
    ob_init.BORLevel   = OB_BOR_LEVEL2;

    HAL_StatusTypeDef status = HAL_FLASHEx_OBProgram(&ob_init);

    /* Lock option bytes and flash */
    HAL_FLASH_OB_Lock();
    HAL_FLASH_Lock();

    /* Option bytes take effect after reset */
    HAL_FLASH_OB_Launch();

    return status;
}
```

---

### Example 2: STM32 — Power Voltage Detector (PVD) Interrupt for Early Warning

```c
/**
 * STM32 Power Voltage Detector (PVD) — Early Warning Before BOD Reset
 *
 * PVD fires an interrupt when VDD crosses the threshold.
 * This gives firmware a window to cleanly abort I2C before power is lost.
 */
#include "stm32f4xx_hal.h"

/* Flag set by PVD ISR, polled by I2C task */
volatile uint8_t g_voltage_critical = 0;

/* I2C handle — assumed to be initialised elsewhere */
extern I2C_HandleTypeDef hi2c1;

/**
 * @brief  Configure the PVD at ~2.9V threshold with rising+falling edge interrupt.
 */
void pvd_configure(void)
{
    PWR_PVDTypeDef pvd_config;

    /* Level 5: ~2.9V threshold on STM32F4 */
    pvd_config.PVDLevel = PWR_PVDLEVEL_5;
    pvd_config.Mode     = PWR_PVD_MODE_IT_RISING_FALLING;

    HAL_PWR_ConfigPVD(&pvd_config);

    /* Enable EXTI line 16 for PVD (MCU-specific) */
    HAL_NVIC_SetPriority(PVD_IRQn, 0, 0);  /* Highest priority */
    HAL_NVIC_EnableIRQ(PVD_IRQn);

    HAL_PWR_EnablePVD();
}

/**
 * @brief  PVD interrupt handler — called when voltage crosses threshold.
 */
void PVD_IRQHandler(void)
{
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_PVDO))
    {
        /* Voltage dropped below threshold */
        g_voltage_critical = 1;

        /*
         * Attempt to cleanly stop any ongoing I2C transaction.
         * HAL_I2C_Master_Abort_IT is non-blocking and safe from ISR context.
         */
        if (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY)
        {
            HAL_I2C_Master_Abort_IT(&hi2c1, 0x00);
        }
    }
    else
    {
        /* Voltage recovered above threshold */
        g_voltage_critical = 0;
    }

    __HAL_PWR_PVD_EXTI_CLEAR_FLAG();
}

/**
 * @brief  Safe I2C write — checks voltage before transmitting.
 *
 * @param  dev_addr   7-bit I2C device address (left-shifted)
 * @param  data       Pointer to data buffer
 * @param  length     Number of bytes to transmit
 * @return HAL_OK on success, HAL_ERROR if voltage is critical
 */
HAL_StatusTypeDef i2c_safe_write(uint16_t dev_addr,
                                  uint8_t  *data,
                                  uint16_t  length)
{
    /* Refuse to start a new transaction if voltage is degraded */
    if (g_voltage_critical)
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Master_Transmit(&hi2c1,
                                   dev_addr,
                                   data,
                                   length,
                                   HAL_MAX_DELAY);
}
```

---

### Example 3: AVR (ATmega) — Software VCC Measurement via Internal Bandgap

```c
/**
 * AVR ATmega — Measure VCC Using Internal 1.1V Bandgap Reference
 *
 * By measuring the bandgap voltage against VCC as reference, we get:
 *   VCC = (1100 mV * 1024) / ADC_reading
 *
 * This allows software-level voltage monitoring without external hardware.
 */
#include <avr/io.h>
#include <avr/power.h>
#include <util/delay.h>
#include <stdint.h>

#define BROWN_OUT_THRESHOLD_MV  2800u   /* Below 2.8 V — don't attempt I2C */
#define ADC_SETTLE_DELAY_US     10u     /* Wait for ADC MUX to settle       */

/**
 * @brief  Read VCC in millivolts using the internal 1.1V bandgap reference.
 * @return VCC in millivolts (e.g., 3300 for 3.3 V)
 */
uint16_t read_vcc_mv(void)
{
    /* Select AVCC as reference, measure the internal 1.1V bandgap (MUX 0x0E) */
    ADMUX = (1 << REFS0) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1);

    /* Allow the reference and MUX to settle */
    _delay_us(ADC_SETTLE_DELAY_US);

    /* Enable ADC and start conversion */
    ADCSRA |= (1 << ADEN) | (1 << ADSC);

    /* Wait for conversion to complete */
    while (ADCSRA & (1 << ADSC));

    uint16_t adc_val = ADC;

    /* Disable ADC to save power */
    ADCSRA &= ~(1 << ADEN);

    /* Calculate VCC: VCC(mV) = 1100 * 1024 / ADC */
    if (adc_val == 0) return 0xFFFF;   /* Guard against division by zero */

    return (uint16_t)((1100UL * 1024UL) / adc_val);
}

/**
 * @brief  Check if VCC is safe for I2C operation.
 * @return 1 if voltage is safe, 0 if brown-out condition detected
 */
uint8_t vcc_is_safe_for_i2c(void)
{
    uint16_t vcc = read_vcc_mv();
    return (vcc >= BROWN_OUT_THRESHOLD_MV) ? 1 : 0;
}

/**
 * @brief  AVR I2C (TWI) Bus Recovery Sequence
 *
 * Toggles SCL nine times to unstick any peripheral holding SDA low,
 * then generates a STOP condition to reset the bus state.
 *
 * Call this after every reset before re-initialising TWI peripheral.
 */
#include <avr/io.h>

#define SCL_PIN  PD0
#define SDA_PIN  PD1
#define I2C_DDR  DDRD
#define I2C_PORT PORTD
#define I2C_PIN  PIND

void i2c_bus_recovery(void)
{
    /* Take control of pins as GPIO */
    I2C_DDR  |=  (1 << SCL_PIN);          /* SCL as output */
    I2C_DDR  &= ~(1 << SDA_PIN);          /* SDA as input (allow slave to drive) */
    I2C_PORT |=  (1 << SDA_PIN);          /* Enable SDA pull-up */

    /* Toggle SCL 9 times — enough to clock out a stuck byte */
    for (uint8_t i = 0; i < 9; i++)
    {
        I2C_PORT &= ~(1 << SCL_PIN);       /* SCL low  */
        _delay_us(5);
        I2C_PORT |=  (1 << SCL_PIN);       /* SCL high */
        _delay_us(5);

        /* If SDA is released, the slave has finished */
        if (I2C_PIN & (1 << SDA_PIN)) break;
    }

    /* Generate STOP condition: SDA low → high while SCL is high */
    I2C_DDR  |= (1 << SDA_PIN);           /* SDA as output */
    I2C_PORT &= ~(1 << SDA_PIN);          /* SDA low  */
    _delay_us(5);
    I2C_PORT |=  (1 << SCL_PIN);          /* SCL high */
    _delay_us(5);
    I2C_PORT |=  (1 << SDA_PIN);          /* SDA high — STOP */
    _delay_us(5);

    /* Release pins back to TWI peripheral control */
    I2C_DDR  &= ~((1 << SCL_PIN) | (1 << SDA_PIN));
    I2C_PORT |=   (1 << SCL_PIN) | (1 << SDA_PIN);  /* Enable pull-ups */
}
```

---

### Example 4: Generic C — Transaction Wrapper with Retry and Voltage Guard

```c
/**
 * Generic I2C transaction wrapper with:
 *   - Pre-transaction voltage check
 *   - Retry with exponential back-off on failure
 *   - Post-failure bus recovery
 */
#include <stdint.h>
#include <stdbool.h>

/* Platform-specific hooks — implement for your target */
extern uint16_t   platform_read_vcc_mv(void);
extern int        platform_i2c_write(uint8_t addr, const uint8_t *buf, size_t len);
extern int        platform_i2c_read(uint8_t addr, uint8_t *buf, size_t len);
extern void       platform_i2c_bus_recover(void);
extern void       platform_delay_ms(uint32_t ms);

#define VCC_MIN_SAFE_MV     2800u
#define MAX_RETRIES         3u
#define RETRY_BASE_DELAY_MS 10u

typedef enum {
    I2C_GUARD_OK = 0,
    I2C_GUARD_UNDERVOLTAGE,
    I2C_GUARD_COMM_ERROR,
    I2C_GUARD_BUS_RECOVERED,
} i2c_guard_status_t;

/**
 * @brief  Write data to an I2C device with voltage guarding and retry logic.
 *
 * @param  addr    7-bit I2C slave address
 * @param  data    Pointer to data buffer
 * @param  length  Number of bytes to send
 * @return i2c_guard_status_t result code
 */
i2c_guard_status_t i2c_guarded_write(uint8_t        addr,
                                      const uint8_t *data,
                                      size_t         length)
{
    /* Step 1: Check supply voltage before starting */
    uint16_t vcc = platform_read_vcc_mv();
    if (vcc < VCC_MIN_SAFE_MV)
    {
        return I2C_GUARD_UNDERVOLTAGE;
    }

    /* Step 2: Attempt with retry and exponential back-off */
    uint32_t delay_ms = RETRY_BASE_DELAY_MS;

    for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        /* Re-check voltage on each retry */
        vcc = platform_read_vcc_mv();
        if (vcc < VCC_MIN_SAFE_MV)
        {
            return I2C_GUARD_UNDERVOLTAGE;
        }

        int result = platform_i2c_write(addr, data, length);

        if (result == 0)
        {
            return I2C_GUARD_OK;  /* Success */
        }

        /* Failed: wait with back-off before retry */
        platform_delay_ms(delay_ms);
        delay_ms *= 2;  /* Exponential back-off: 10ms, 20ms, 40ms */
    }

    /* All retries exhausted — attempt bus recovery */
    platform_i2c_bus_recover();

    return I2C_GUARD_BUS_RECOVERED;
}
```

---

## Rust Implementation

### Example 5: Rust — Voltage Monitor with embedded-hal Traits

```rust
//! Brown-Out Protection for I2C in Rust using embedded-hal traits.
//!
//! This module provides voltage-aware I2C transaction guards that work
//! across any embedded platform implementing the embedded-hal traits.

use core::fmt;
use embedded_hal::adc::{Channel, OneShot};
use embedded_hal::blocking::i2c::{Write, WriteRead};
use embedded_hal::blocking::delay::DelayMs;
use embedded_hal::digital::v2::OutputPin;

/// Threshold below which I2C transactions are refused (in millivolts)
const VCC_MIN_SAFE_MV: u32 = 2800;

/// Maximum number of retries for a failed I2C transaction
const MAX_RETRIES: u8 = 3;

/// Brown-out protection error variants
#[derive(Debug)]
pub enum BrownOutError<I2cErr, AdcErr> {
    /// Supply voltage is below the safe operating threshold
    UnderVoltage { measured_mv: u32 },
    /// I2C communication failed after all retries
    I2cError(I2cErr),
    /// ADC voltage measurement failed
    AdcError(AdcErr),
    /// Bus recovery was attempted — caller should re-initialise peripherals
    BusRecovered,
}

impl<I, A> fmt::Display for BrownOutError<I, A>
where
    I: fmt::Debug,
    A: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnderVoltage { measured_mv } => {
                write!(f, "Under-voltage: {}mV (min {}mV)", measured_mv, VCC_MIN_SAFE_MV)
            }
            Self::I2cError(e)  => write!(f, "I2C error: {:?}", e),
            Self::AdcError(e)  => write!(f, "ADC error: {:?}", e),
            Self::BusRecovered => write!(f, "I2C bus recovery performed"),
        }
    }
}

/// Measures VCC via a voltage divider connected to an ADC channel.
///
/// # Arguments
/// * `adc`     - ADC peripheral implementing `OneShot`
/// * `channel` - ADC channel connected to the voltage reference
/// * `vref_mv` - ADC full-scale reference voltage in millivolts
/// * `adc_bits`- ADC resolution (e.g., 12 for a 12-bit ADC)
/// * `divider` - Voltage divider ratio (e.g., 2 if VCC/2 is measured)
///
/// Returns VCC in millivolts, or an ADC error.
pub fn measure_vcc_mv<ADC, CH, W>(
    adc: &mut ADC,
    channel: &mut CH,
    vref_mv: u32,
    adc_bits: u32,
    divider: u32,
) -> Result<u32, ADC::Error>
where
    ADC: OneShot<ADC, W, CH>,
    CH: Channel<ADC>,
    W: Into<u32>,
{
    let raw: W = nb::block!(adc.read(channel))?;
    let raw_val: u32 = raw.into();
    let max_count: u32 = (1u32 << adc_bits) - 1;

    // VCC = (raw / max_count) * vref_mv * divider
    let vcc_mv = (raw_val * vref_mv * divider) / max_count;
    Ok(vcc_mv)
}

/// A voltage-aware wrapper around an I2C bus.
///
/// Checks supply voltage before every transaction and refuses to
/// proceed if the voltage is below `VCC_MIN_SAFE_MV`.
pub struct GuardedI2c<I2C> {
    bus: I2C,
    last_vcc_mv: u32,
}

impl<I2C> GuardedI2c<I2C>
where
    I2C: Write + WriteRead,
{
    /// Create a new `GuardedI2c` wrapping the given I2C bus.
    pub fn new(bus: I2C) -> Self {
        Self { bus, last_vcc_mv: 0 }
    }

    /// Return the last measured supply voltage (call `check_voltage` first).
    pub fn last_vcc_mv(&self) -> u32 {
        self.last_vcc_mv
    }

    /// Release the underlying I2C bus.
    pub fn release(self) -> I2C {
        self.bus
    }

    /// Perform a voltage-guarded I2C write with retry and back-off.
    ///
    /// # Arguments
    /// * `addr`    - 7-bit I2C slave address
    /// * `data`    - Byte slice to transmit
    /// * `vcc_mv`  - Current measured VCC in millivolts (caller provides)
    /// * `delay`   - Delay provider for back-off between retries
    pub fn write_guarded<D>(
        &mut self,
        addr: u8,
        data: &[u8],
        vcc_mv: u32,
        delay: &mut D,
    ) -> Result<(), BrownOutError<<I2C as Write>::Error, core::convert::Infallible>>
    where
        D: DelayMs<u32>,
    {
        // Refuse if voltage is critically low
        if vcc_mv < VCC_MIN_SAFE_MV {
            return Err(BrownOutError::UnderVoltage { measured_mv: vcc_mv });
        }

        self.last_vcc_mv = vcc_mv;

        let mut backoff_ms: u32 = 10;

        for attempt in 0..MAX_RETRIES {
            match self.bus.write(addr, data) {
                Ok(()) => return Ok(()),
                Err(e) => {
                    if attempt + 1 == MAX_RETRIES {
                        return Err(BrownOutError::I2cError(e));
                    }
                    // Exponential back-off between retries
                    delay.delay_ms(backoff_ms);
                    backoff_ms *= 2;
                }
            }
        }

        unreachable!()
    }

    /// Perform a voltage-guarded write-then-read (register read pattern).
    pub fn write_read_guarded<D>(
        &mut self,
        addr: u8,
        reg: &[u8],
        buf: &mut [u8],
        vcc_mv: u32,
        delay: &mut D,
    ) -> Result<(), BrownOutError<<I2C as WriteRead>::Error, core::convert::Infallible>>
    where
        D: DelayMs<u32>,
        I2C: WriteRead,
    {
        if vcc_mv < VCC_MIN_SAFE_MV {
            return Err(BrownOutError::UnderVoltage { measured_mv: vcc_mv });
        }

        self.last_vcc_mv = vcc_mv;

        let mut backoff_ms: u32 = 10;

        for attempt in 0..MAX_RETRIES {
            match self.bus.write_read(addr, reg, buf) {
                Ok(()) => return Ok(()),
                Err(e) => {
                    if attempt + 1 == MAX_RETRIES {
                        return Err(BrownOutError::I2cError(e));
                    }
                    delay.delay_ms(backoff_ms);
                    backoff_ms *= 2;
                }
            }
        }

        unreachable!()
    }
}
```

---

### Example 6: Rust — I2C Bus Recovery Using GPIO Bit-Banging

```rust
//! I2C Bus Recovery in Rust using embedded-hal GPIO traits.
//!
//! Implements SMBUS / NXP UM10204 recommended bus recovery procedure:
//! toggle SCL nine times, then generate a STOP condition.

use embedded_hal::digital::v2::{InputPin, OutputPin};
use embedded_hal::blocking::delay::DelayUs;

/// Error type for bus recovery operations
#[derive(Debug)]
pub enum RecoveryError<SclErr, SdaErr> {
    SclError(SclErr),
    SdaError(SdaErr),
    /// SDA remained stuck low even after 9 clock cycles
    BusStillLocked,
}

/// Perform I2C bus recovery by bit-banging SCL and monitoring SDA.
///
/// # Procedure
/// 1. Assert SCL as output, SDA as input with pull-up
/// 2. Toggle SCL 9 times — clocks out any stuck slave byte
/// 3. Generate STOP condition (SDA low → high while SCL high)
///
/// After calling this, re-initialise the I2C peripheral normally.
///
/// # Type Parameters
/// * `Scl`   - GPIO output pin connected to SCL
/// * `Sda`   - GPIO pin that can act as both input and output
/// * `Delay` - Delay provider supporting microsecond resolution
pub fn i2c_bus_recovery<Scl, SdaIn, SdaOut, Delay>(
    scl: &mut Scl,
    sda_in: &mut SdaIn,
    sda_out: &mut SdaOut,
    delay: &mut Delay,
    half_period_us: u32,
) -> Result<(), RecoveryError<Scl::Error, SdaOut::Error>>
where
    Scl: OutputPin,
    SdaIn: InputPin,
    SdaOut: OutputPin,
    Delay: DelayUs<u32>,
{
    // Ensure SCL starts high
    scl.set_high().map_err(RecoveryError::SclError)?;
    delay.delay_us(half_period_us);

    // Toggle SCL 9 times — enough to clock out a full byte + ACK
    for _cycle in 0..9 {
        scl.set_low().map_err(RecoveryError::SclError)?;
        delay.delay_us(half_period_us);

        scl.set_high().map_err(RecoveryError::SclError)?;
        delay.delay_us(half_period_us);

        // Check if SDA has been released by the slave
        if sda_in.is_high().unwrap_or(false) {
            break; // Slave released SDA — bus is free
        }
    }

    // Verify SDA is now high (released by slave)
    if sda_in.is_low().unwrap_or(true) {
        return Err(RecoveryError::BusStillLocked);
    }

    // Generate STOP condition: SDA low → SDA high while SCL is high
    sda_out.set_low().map_err(RecoveryError::SdaError)?;
    delay.delay_us(half_period_us);

    scl.set_high().map_err(RecoveryError::SclError)?;
    delay.delay_us(half_period_us);

    sda_out.set_high().map_err(RecoveryError::SdaError)?;
    delay.delay_us(half_period_us);

    Ok(())
}
```

---

### Example 7: Rust — Startup Sequence with Brown-Out Safe Boot

```rust
//! Safe I2C startup sequence for Rust embedded firmware.
//!
//! Demonstrates the recommended boot procedure:
//!   1. Wait for supply to stabilise
//!   2. Measure VCC
//!   3. If above threshold, perform bus recovery then init peripherals
//!   4. Enter main loop with ongoing voltage monitoring

use embedded_hal::blocking::delay::{DelayMs, DelayUs};

/// Minimum VCC required to begin I2C initialisation (millivolts)
const VCC_INIT_THRESHOLD_MV: u32 = 3000;

/// Time to wait after power-on before first voltage measurement (ms)
const POWER_STABILISATION_DELAY_MS: u32 = 50;

/// Result of the safe boot sequence
#[derive(Debug)]
pub enum BootResult {
    /// System initialised successfully
    Ok { vcc_mv: u32 },
    /// Voltage too low to safely initialise I2C
    UnderVoltage { vcc_mv: u32 },
    /// Bus recovery failed — hardware issue suspected
    BusRecoveryFailed,
}

/// Perform a voltage-safe I2C boot sequence.
///
/// This function should be called once at startup, before any I2C
/// peripheral initialisation. It handles:
///   - Supply stabilisation delay
///   - Pre-init voltage check
///   - Bus recovery
///   - Returns a `BootResult` indicating whether it is safe to proceed
pub fn safe_i2c_boot<D>(delay: &mut D, vcc_mv: u32) -> BootResult
where
    D: DelayMs<u32> + DelayUs<u32>,
{
    // Allow supply rails to stabilise after power-on
    delay.delay_ms(POWER_STABILISATION_DELAY_MS);

    // Check voltage before touching the I2C bus
    if vcc_mv < VCC_INIT_THRESHOLD_MV {
        return BootResult::UnderVoltage { vcc_mv };
    }

    // In a real system, call i2c_bus_recovery() here using GPIO pins
    // before re-enabling the I2C peripheral. For this example we
    // assume recovery succeeded.

    BootResult::Ok { vcc_mv }
}

/// Per-transaction voltage check helper.
///
/// Call this inside your main application loop before each I2C
/// operation to catch developing brown-out conditions.
pub fn pre_transaction_check(vcc_mv: u32) -> bool {
    vcc_mv >= VCC_MIN_SAFE_MV
}

const VCC_MIN_SAFE_MV: u32 = 2800;

/// Application-level example showing how to use the guards
/// in a typical sensor polling loop.
pub fn example_sensor_loop<D>(delay: &mut D)
where
    D: DelayMs<u32>,
{
    loop {
        // In production: measure VCC here via ADC
        let vcc_mv: u32 = 3300; // Placeholder — replace with real ADC reading

        if !pre_transaction_check(vcc_mv) {
            // Voltage degraded — skip this cycle and wait
            delay.delay_ms(100);
            continue;
        }

        // Proceed with I2C sensor read...
        // e.g., guarded_i2c.write_guarded(addr, &data, vcc_mv, delay)

        delay.delay_ms(10); // Normal polling interval
    }
}
```

---

## Recovery Procedures

### Standard Bus Recovery (NXP UM10204 Recommended)

The NXP I2C specification document UM10204 defines the following recovery procedure for a locked bus:

1. Check SDA. If SDA is high, generate a START condition followed by a STOP condition (clears any glitch state).
2. If SDA is low, toggle SCL until SDA is released (maximum 9 clock cycles — enough to clock out an entire byte plus ACK).
3. Generate a STOP condition.
4. Check SDA again. If still low, power cycle the bus (if hardware allows).
5. Re-enable the I2C peripheral.

### Peripheral Re-initialisation After Recovery

After bus recovery, always:

- Write all configuration registers explicitly — do not assume the peripheral retained its pre-brownout state.
- Re-read and verify critical registers (compare against expected values).
- Clear any error/interrupt flags in the peripheral before resuming normal operation.
- Log the recovery event with a timestamp for diagnostics.

### Watchdog Integration

Pair brown-out detection with an independent hardware watchdog timer (IWDG/WWDG). If the I2C bus becomes permanently locked and firmware cannot recover it within a timeout, the watchdog will reset the entire system, preventing a silent hang.

---

## Platform-Specific Examples

### STM32 — Option Byte BOR Threshold Reference

| BOR Level | Reset Voltage (Rising) | Reset Voltage (Falling) |
|---|---|---|
| BOR_OFF | ~1.8 V (POR only) | ~1.8 V |
| BOR_LEVEL1 | 2.10 V – 2.40 V | 2.00 V – 2.30 V |
| BOR_LEVEL2 | 2.40 V – 2.70 V | 2.30 V – 2.60 V |
| BOR_LEVEL3 | 2.70 V – 3.60 V | 2.60 V – 3.50 V |

*Recommended: BOR_LEVEL2 for 3.3 V I2C systems to ensure SDA/SCL drive levels remain valid throughout operation.*

### AVR — BOD Fuse Bit Reference (ATmega328P)

| BODLEVEL Fuse | BOD Trigger Voltage |
|---|---|
| 111 | BOD Disabled |
| 110 | 1.8 V |
| 101 | 2.7 V |
| 100 | 4.3 V |

*Recommended: BODLEVEL = 101 (2.7 V) for 3.3 V I2C systems.*

### ESP32 — Brown-Out Detector Configuration (ESP-IDF)

```c
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"

/*
 * ESP32 brown-out detector threshold.
 * Values 0–7: higher = higher trigger voltage.
 * Level 7 = ~2.77 V, Level 0 = ~2.43 V
 */
void esp32_configure_brownout(uint8_t level)
{
    REG_SET_FIELD(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_DBROWN_OUT_THRES, level);
    REG_SET_BIT(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_ENA);
}
```

### RP2040 — Monitoring VREG via ADC

```c
#include "pico/stdlib.h"
#include "hardware/adc.h"

/*
 * RP2040: ADC channel 3 is connected to VSYS/3 via an internal divider.
 * This allows monitoring the system supply voltage.
 */
float rp2040_read_vsys_voltage(void)
{
    adc_init();
    adc_gpio_init(29);      /* GPIO29 = VSYS/3 on Pico board */
    adc_select_input(3);    /* ADC channel 3 */

    uint16_t raw = adc_read();

    /* VSYS = (raw / 4095) * 3.3V * 3 (divider factor) */
    float vsys = (raw / 4095.0f) * 3.3f * 3.0f;
    return vsys;
}
```

---

## Summary

Brown-out protection for I2C is a multi-layered discipline that spans hardware design, MCU configuration, and application firmware. The key principles are:

**Hardware layer:** Configure the MCU's built-in BOD/BOR at an appropriate threshold for your supply voltage and I2C logic level. Add bulk and decoupling capacitance to ride through transients. Consider external supervisory ICs for critical applications.

**Detection layer:** Supplement hardware BOD with software voltage monitoring using an ADC. The bandgap reference technique on AVR and the VSYS channel on RP2040 allow VCC measurement without external hardware. Use the PVD (Power Voltage Detector) interrupt on STM32 for early-warning callback before a full reset.

**Protection layer:** Gate all I2C transactions behind a voltage check. Refuse to initiate new transactions if VCC is below a safe threshold. Implement retry logic with exponential back-off, since transient brown-outs may self-resolve.

**Recovery layer:** After every reset — whether caused by BOD, PVD, watchdog, or software — execute a standardised I2C bus recovery sequence before re-enabling the I2C peripheral. Toggle SCL nine times to unstick any peripheral holding SDA low, then generate a STOP condition. Re-initialise all slave peripherals explicitly.

**Architecture layer:** In Rust, model the voltage constraint as a type-level guard, rejecting transactions at compile time if voltage state has not been checked. In C, centralise I2C access through a guarded wrapper function that enforces voltage preconditions before every call.

By treating voltage as a first-class input to the I2C communication model — not an afterthought — embedded systems can achieve reliable I2C operation even in electrically challenging environments.

---

*Document: 79. Brown-Out Protection — Detecting and Handling Voltage Drops During I2C Operation*
*Covers: STM32, AVR (ATmega), ESP32, RP2040 | Languages: C/C++, Rust (embedded-hal)*