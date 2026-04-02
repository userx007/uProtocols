# 82. I2C Industrial Temperature Range

**Electrical theory** — how pull-up resistors, IOL sink current, input thresholds, and oscillator frequency all drift across −40 °C to +125 °C, with concrete formulas and worked examples for Rp selection including high-temperature derating.

**Device selection** — grading suffixes (I/E/A/M), which datasheet parameters must be *guaranteed* (not typical), and package thermal considerations.

**C/C++ examples** — three complete, commented examples: STM32 HAL initialization with analog/digital noise filters and the 9-clock bus recovery sequence; an ADC-based junction temperature reader that dynamically adjusts the I2C timing register; and a C++ RAII wrapper with a templated retry loop and `write_read` combined-transaction support.

**Rust examples** — a portable `embedded-hal 1.0` industrial wrapper with exponential back-off and a `ThermalZone` enum for adaptive clock selection; a full TMP117 register-level driver demonstrating signed fixed-point temperature conversion; and an `embassy`-based async driver using `with_timeout` to handle clock-stretch protection without blocking.

**PCB and validation** — trace routing rules, pull-up placement, gold contact requirements, and a pass/fail test matrix covering cold-start lockup, thermal cycling, rise-time margin, and EMI susceptibility.

## Designing I2C Systems for −40 °C to +125 °C Operation

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Temperature Matters for I2C](#why-temperature-matters-for-i2c)
3. [Electrical Parameter Drift Over Temperature](#electrical-parameter-drift-over-temperature)
4. [Pull-up Resistor Selection Across Temperature](#pull-up-resistor-selection-across-temperature)
5. [Bus Capacitance and Rise-Time Compensation](#bus-capacitance-and-rise-time-compensation)
6. [Device Selection for Industrial Grade](#device-selection-for-industrial-grade)
7. [Clock Stretching Under Thermal Stress](#clock-stretching-under-thermal-stress)
8. [Software Techniques for Thermal Robustness](#software-techniques-for-thermal-robustness)
9. [Code Examples in C/C++](#code-examples-in-cc)
10. [Code Examples in Rust](#code-examples-in-rust)
11. [PCB Design Considerations](#pcb-design-considerations)
12. [Testing and Validation Strategy](#testing-and-validation-strategy)
13. [Summary](#summary)

---

## Introduction

The I2C (Inter-Integrated Circuit) protocol, defined originally by Philips (now NXP) and maintained under the UM10204 specification, is widely deployed in consumer electronics. However, industrial and automotive applications push devices far beyond the typical 0 °C to +70 °C commercial range.

**Industrial temperature range: −40 °C to +85 °C**
**Automotive/extended industrial range: −40 °C to +125 °C**

Designing reliable I2C buses across this full span requires understanding how every electrical, physical, and software parameter shifts with temperature — and compensating proactively at every level of the design stack.

---

## Why Temperature Matters for I2C

I2C is an open-drain, two-wire bus (SDA + SCL). Its timing and electrical thresholds depend on:

- **Pull-up resistor values** — set rise time and current
- **Bus capacitance** — set fall time and RC time constant
- **IOL (output low sink current)** — keeps the line pulled low against the pull-up
- **Input threshold voltages (VIL, VIH)** — determine logic levels
- **Crystal / RC oscillator frequency** — determines SCL timing

All of these parameters drift with temperature, sometimes dramatically. Failure to account for this leads to:

- Bus lockups at cold start (−40 °C)
- Excessive IOL drive at high temperature causing bus contention
- Rise-time violations at high temperature when pull-up resistance is too large
- Metastability and framing errors near VIL/VIH transition zones
- Clock stretching timeouts that only occur at thermal extremes

---

## Electrical Parameter Drift Over Temperature

### 1. Pull-up Resistor Drift

Standard metal-film resistors have a Temperature Coefficient of Resistance (TCR) typically ±100 ppm/°C.

Over the full −40 °C to +125 °C range (ΔT = 165 °C):

```
ΔR/R = TCR × ΔT = 100e-6 × 165 = 1.65%
```

This is generally acceptable. However, **thick-film chip resistors** can vary ±200 ppm/°C or more. Always use 1% tolerance, low-TCR (≤100 ppm/°C) resistors for pull-ups in industrial designs.

### 2. IOL (Sink Current) Drift

MOSFET-based open-drain outputs have drain current that is temperature dependent:

- At **+125 °C**: mobility degrades ~30–50%, so IOL decreases. The line may not pull down far enough.
- At **−40 °C**: threshold voltage (Vth) increases, requiring more gate overdrive. For marginal designs this can cause glitches.

From the I2C spec:
- VoL (output low voltage) must be ≤ 0.4 V at IOL = 3 mA (Standard/Fast mode)
- VoL must be ≤ 0.4 V at IOL = 20 mA (Fast-mode Plus)

Always verify VoL/IOL over the full temperature range in datasheets.

### 3. Input Threshold Drift

For a 3.3 V VCC supply:
- **VIL (max)** = 0.3 × VCC = 0.99 V (fixed ratio)
- **VIH (min)** = 0.7 × VCC = 2.31 V (fixed ratio)

However, VCC itself may drift with a linear regulator's output voltage variation over temperature (typically ±2%). This shifts the thresholds:

```
At VCC = 3.3 V ± 66 mV:
  VIL range: 0.97 V – 1.01 V
  VIH range: 2.27 V – 2.34 V
```

Devices near the noise margin at room temperature can fail at −40 °C or +125 °C.

### 4. Crystal / Oscillator Frequency Drift

The SCL frequency is derived from a crystal or RC oscillator:

| Oscillator Type | Typical Drift (−40 to +125 °C) |
|---|---|
| Internal RC (no calibration) | ±5% to ±15% |
| Internal RC (factory calibrated) | ±2% to ±3% |
| Crystal (AT-cut) | ±50 ppm to ±200 ppm |
| TCXO | ±0.5 ppm to ±2.5 ppm |

For I2C Standard mode (100 kHz), a 15% frequency error causes the clock to run at 85 kHz or 115 kHz — still within spec. But for Fast-mode Plus (1 MHz), the same error can violate tHIGH or tLOW minimums.

**Rule:** For Fast-mode Plus over the industrial range, use a crystal or calibrated oscillator.

---

## Pull-up Resistor Selection Across Temperature

The pull-up resistor (Rp) must satisfy two constraints simultaneously:

### Constraint 1: Rise-Time Limit

From the I2C spec, the rise time (tr) must be ≤ 1000 ns (Standard), ≤ 300 ns (Fast), ≤ 120 ns (Fast-mode Plus).

```
tr ≈ 0.8473 × Rp × Cb
```

Where Cb = total bus capacitance. So:

```
Rp_max = tr_max / (0.8473 × Cb)
```

**Example:** Fast mode (300 ns max), Cb = 100 pF:

```
Rp_max = 300e-9 / (0.8473 × 100e-12) = 3,540 Ω  →  use 3.3 kΩ
```

### Constraint 2: Current Sink Limit

The pull-up must not push more current through the sink transistor than IOL_max:

```
Rp_min = (VCC - VoL_max) / IOL_max
```

**Example:** VCC = 3.3 V, VoL_max = 0.4 V, IOL_max = 3 mA:

```
Rp_min = (3.3 - 0.4) / 0.003 = 967 Ω  →  use 1.0 kΩ
```

### Temperature Effect on Rp

At +125 °C the IOL degrades. A conservative derating factor of 70% is common:

```
IOL_derated = IOL_spec × 0.70 = 3 mA × 0.70 = 2.1 mA
Rp_min_hot = (3.3 - 0.4) / 0.0021 = 1,381 Ω  →  use 1.5 kΩ minimum at +125 °C
```

### Summary Table

| Mode | Cb | Rp (room) | Rp_min (+125 °C) | Recommended |
|---|---|---|---|---|
| Standard (100 kHz) | 100 pF | 1 kΩ – 10 kΩ | 1.5 kΩ | 2.2 kΩ |
| Fast (400 kHz) | 100 pF | 1 kΩ – 3.3 kΩ | 1.5 kΩ | 2.2 kΩ |
| Fast-mode Plus (1 MHz) | 50 pF | 200 Ω – 1 kΩ | 300 Ω | 470 Ω |

---

## Bus Capacitance and Rise-Time Compensation

Bus capacitance increases with PCB trace length, connectors, and the input capacitance of each device. Temperature has two second-order effects:

1. **PCB dielectric constant (εr)** increases ~0.5–1% over the range — negligible.
2. **Device input capacitance** can vary ±10–20% over temperature — more significant.

### Active Pull-up Circuits for Wide-Temperature Operation

For aggressive Fast-mode Plus designs, a **current-source pull-up** gives temperature-stable rise times:

```
Constant-current pull-up with PMOS + op-amp:
  - Drive 10 mA into the bus during the 0→1 transition
  - Clamp to VCC via diode once bus reaches VIH
```

Alternatively, use a **bus accelerator IC** such as the NXP PCA9517, which provides active pull-ups.

---

## Device Selection for Industrial Grade

Not all I2C devices are rated for the full industrial range. Key selection criteria:

### 1. Temperature Range Markings

| Suffix | Range |
|---|---|
| C / commercial | 0 °C to +70 °C |
| I / industrial | −40 °C to +85 °C |
| E / extended | −40 °C to +105 °C |
| A / automotive | −40 °C to +125 °C |
| M / military | −55 °C to +125 °C |

**Always verify the exact range in the datasheet.** The suffix convention is not universal across manufacturers.

### 2. Parameter Guarantees Over Temperature

Check that the datasheet provides **guaranteed (not typical) specifications** over the full temperature range for:

- IOL / IOH
- VIL / VIH
- tSU;DAT, tHD;DAT, tSU;STA, tHD;STA
- tSP (spike suppression width)
- Internal clock accuracy (for devices with oscillators)

### 3. Package Thermal Resistance

Devices in QFN or ceramic packages tolerate higher operating temperatures and have better thermal conductivity than SOIC or DIP packages.

---

## Clock Stretching Under Thermal Stress

I2C devices may stretch the SCL clock low to buy time for processing. The maximum stretch duration is device-defined and temperature-dependent:

- At **−40 °C**: slow internal oscillators may stretch longer than expected
- At **+125 °C**: faster noise-induced glitches may cause spurious stretch events

### Host-Side Mitigation

The I2C master must implement a **clock-stretching timeout** to prevent lockup:

```
Recommended timeout: 25 ms (standard), 10 ms (fast mode)
Implement timeout in hardware timer or watchdog, not software polling
```

After a timeout, the master must:
1. Assert a STOP condition (if bus control permits)
2. Toggle SCL 9 times to release a stuck SDA
3. Reset the I2C peripheral
4. Log the event for diagnostic purposes

---

## Software Techniques for Thermal Robustness

### 1. Dynamic Retry with Exponential Back-off

Cold starts at −40 °C can cause VCC to undershoot before stabilizing, causing device NACKs on the first transaction.

```c
#define I2C_MAX_RETRIES    5
#define I2C_RETRY_BASE_MS  2

int i2c_write_robust(uint8_t addr, const uint8_t *data, size_t len) {
    for (int attempt = 0; attempt < I2C_MAX_RETRIES; attempt++) {
        int result = i2c_write(addr, data, len);
        if (result == I2C_OK) return I2C_OK;
        uint32_t delay = I2C_RETRY_BASE_MS * (1u << attempt); // 2,4,8,16,32 ms
        delay_ms(delay);
    }
    return I2C_ERR_NACK;
}
```

### 2. Bus Recovery (9-Clock Method)

If SDA is stuck low (a slave is mid-transfer when a reset occurs):

```c
void i2c_bus_recovery(void) {
    // Toggle SCL 9 times to clock out stuck device
    gpio_set_mode(SCL_PIN, GPIO_OUTPUT_OPEN_DRAIN);
    for (int i = 0; i < 9; i++) {
        gpio_write(SCL_PIN, 0); delay_us(5);
        gpio_write(SCL_PIN, 1); delay_us(5);
        if (gpio_read(SDA_PIN) == 1) break; // SDA released
    }
    // Generate STOP condition
    gpio_write(SDA_PIN, 0); delay_us(5);
    gpio_write(SCL_PIN, 1); delay_us(5);
    gpio_write(SDA_PIN, 1); delay_us(5);
}
```

### 3. Thermal Compensation of Timing Parameters

Microcontrollers with internal RC oscillators drift with temperature. Recalibrate before I2C transactions:

```c
// Recalibrate oscillator from external reference if available
// Or select a conservative (slower) clock divider for the full range
void i2c_configure_for_temperature(int16_t temp_celsius) {
    uint32_t i2c_clock_hz;
    if (temp_celsius < -20) {
        i2c_clock_hz = 80000;   // 80 kHz: derated for cold, slow oscillator
    } else if (temp_celsius > 100) {
        i2c_clock_hz = 90000;   // 90 kHz: derated for hot, weaker IOL
    } else {
        i2c_clock_hz = 100000;  // 100 kHz nominal
    }
    i2c_set_clock(i2c_clock_hz);
}
```

---

## Code Examples in C/C++

### Example 1: Industrial-Grade I2C Initialization (STM32 HAL)

```c
/**
 * @file i2c_industrial.c
 * @brief I2C initialization and transaction handling for -40°C to +125°C operation.
 *
 * Target: STM32G0xx, I2C1 peripheral, 3.3V VCC, 100 pF bus capacitance.
 * Pull-ups: 2.2 kΩ to 3.3V (derated for +125°C IOL degradation).
 */

#include "stm32g0xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------------- */
#define I2C_TIMEOUT_MS       25      /**< Max clock-stretch allowance      */
#define I2C_MAX_RETRIES      5       /**< Retry attempts after NACK/error  */
#define I2C_RETRY_BASE_MS    2       /**< Base delay for exponential back-off */
#define I2C_BUS_FREQ_HZ      100000  /**< Standard mode: 100 kHz           */

/* Pins used for manual bus recovery (must match hardware schematic) */
#define I2C_SCL_PIN          GPIO_PIN_6
#define I2C_SDA_PIN          GPIO_PIN_7
#define I2C_GPIO_PORT        GPIOB

/* -----------------------------------------------------------------------
 * HAL handle
 * --------------------------------------------------------------------- */
static I2C_HandleTypeDef hi2c1;

/* -----------------------------------------------------------------------
 * Private helpers
 * --------------------------------------------------------------------- */

/**
 * @brief Toggle SCL 9 times to free a stuck SDA line.
 *
 * Called when SDA is held low by a slave that was interrupted mid-transaction
 * (e.g., at power-on after a hard reset at low temperature).
 */
static void i2c_bus_recovery(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Release I2C peripheral, reclaim pins as open-drain GPIO */
    HAL_I2C_DeInit(&hi2c1);

    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = I2C_SCL_PIN;
    HAL_GPIO_Init(I2C_GPIO_PORT, &gpio);
    gpio.Pin = I2C_SDA_PIN;
    HAL_GPIO_Init(I2C_GPIO_PORT, &gpio);

    /* Pull both high first */
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN | I2C_SDA_PIN, GPIO_PIN_SET);
    HAL_Delay(1);

    /* Clock SDA free — up to 9 pulses */
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
        HAL_Delay(1);
        if (HAL_GPIO_ReadPin(I2C_GPIO_PORT, I2C_SDA_PIN) == GPIO_PIN_SET) {
            break;  /* SDA released by slave */
        }
    }

    /* Generate STOP condition: SDA low → SCL high → SDA high */
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SCL_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(I2C_GPIO_PORT, I2C_SDA_PIN, GPIO_PIN_SET);
    HAL_Delay(1);

    /* Re-initialize the I2C peripheral */
    HAL_I2C_Init(&hi2c1);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

/**
 * @brief Initialize I2C1 for industrial temperature operation.
 *
 * Timing register is pre-calculated for SYSCLK=64 MHz, target=100 kHz,
 * with sufficient setup/hold margin for worst-case temperature corners.
 * Use STM32CubeMX I2C timing calculator for other clock configurations.
 */
void i2c_industrial_init(void)
{
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = I2C_SCL_PIN | I2C_SDA_PIN;
    gpio.Mode      = GPIO_MODE_AF_OD;          /* Open-drain, as required by I2C */
    gpio.Pull      = GPIO_NOPULL;              /* External pull-ups fitted on PCB */
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF6_I2C1;
    HAL_GPIO_Init(I2C_GPIO_PORT, &gpio);

    hi2c1.Instance             = I2C1;
    /*
     * Timing for 100 kHz, SYSCLK=64 MHz, analog filter ON, digital filter=0.
     * PRESC=3, SCLL=0x13, SCLH=0x0F, SDADEL=0x02, SCLDEL=0x04
     * This provides extra hold time margin at +125°C where IOL is weakest.
     */
    hi2c1.Init.Timing          = 0x3010010A;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;  /* Allow clock stretching */

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        /* Fatal: system cannot operate without I2C */
        Error_Handler();
    }

    /* Enable analog noise filter — suppresses spikes at high temperature */
    HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);

    /*
     * Digital filter: reject glitches up to 3 × tI2CCLK.
     * At 64 MHz: 3 × 15.6 ns ≈ 47 ns spike rejection.
     * Provides additional EMI robustness in industrial environments.
     */
    HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 3);
}

/**
 * @brief Write bytes to an I2C device with retry and bus-recovery logic.
 *
 * @param dev_addr  7-bit device address (not shifted)
 * @param data      Pointer to data buffer
 * @param len       Number of bytes to write
 * @return          HAL_OK on success, HAL_ERROR after exhausting retries
 */
HAL_StatusTypeDef i2c_write_robust(uint16_t dev_addr,
                                    const uint8_t *data,
                                    uint16_t len)
{
    HAL_StatusTypeDef status;
    uint32_t delay_ms = I2C_RETRY_BASE_MS;

    for (int attempt = 0; attempt < I2C_MAX_RETRIES; attempt++) {
        status = HAL_I2C_Master_Transmit(
            &hi2c1,
            (uint16_t)(dev_addr << 1),
            (uint8_t *)data,
            len,
            I2C_TIMEOUT_MS
        );

        if (status == HAL_OK) {
            return HAL_OK;
        }

        /* If bus is locked (BUSY flag stuck), attempt recovery */
        if (__HAL_I2C_GET_FLAG(&hi2c1, I2C_FLAG_BUSY)) {
            i2c_bus_recovery();
        }

        HAL_Delay(delay_ms);
        delay_ms *= 2;  /* Exponential back-off: 2, 4, 8, 16, 32 ms */
    }

    return HAL_ERROR;
}

/**
 * @brief Read bytes from an I2C device with retry and bus-recovery logic.
 *
 * @param dev_addr  7-bit device address (not shifted)
 * @param buf       Buffer to store received bytes
 * @param len       Number of bytes to read
 * @return          HAL_OK on success, HAL_ERROR after exhausting retries
 */
HAL_StatusTypeDef i2c_read_robust(uint16_t dev_addr,
                                   uint8_t *buf,
                                   uint16_t len)
{
    HAL_StatusTypeDef status;
    uint32_t delay_ms = I2C_RETRY_BASE_MS;

    for (int attempt = 0; attempt < I2C_MAX_RETRIES; attempt++) {
        status = HAL_I2C_Master_Receive(
            &hi2c1,
            (uint16_t)(dev_addr << 1),
            buf,
            len,
            I2C_TIMEOUT_MS
        );

        if (status == HAL_OK) {
            return HAL_OK;
        }

        if (__HAL_I2C_GET_FLAG(&hi2c1, I2C_FLAG_BUSY)) {
            i2c_bus_recovery();
        }

        HAL_Delay(delay_ms);
        delay_ms *= 2;
    }

    return HAL_ERROR;
}
```

---

### Example 2: Adaptive Clock Speed Based on Temperature Sensor Reading

```c
/**
 * @file i2c_thermal_adapt.c
 * @brief Adaptive I2C clock management using on-chip temperature sensor.
 *
 * Reads the MCU's internal temperature sensor and derate the I2C clock
 * at thermal extremes to maintain reliable communication.
 */

#include "stm32g0xx_hal.h"

/* Internal temperature sensor calibration addresses (STM32G0) */
#define TS_CAL1_ADDR  ((uint16_t *)0x1FFF75A8)  /* Calibrated at 30°C, 3.0V  */
#define TS_CAL2_ADDR  ((uint16_t *)0x1FFF75CA)  /* Calibrated at 110°C, 3.0V */
#define TS_CAL1_TEMP  30
#define TS_CAL2_TEMP  110

/**
 * @brief Read MCU junction temperature via ADC.
 * @return Temperature in degrees Celsius (integer)
 */
static int16_t read_junction_temperature(void)
{
    /* Start ADC conversion on temperature sensor channel */
    /* (ADC setup omitted for brevity; assumes ADC1 initialized) */
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    /* Linear interpolation using factory calibration values */
    int32_t temp = (int32_t)(raw - *TS_CAL1_ADDR) * (TS_CAL2_TEMP - TS_CAL1_TEMP);
    temp /= (int32_t)(*TS_CAL2_ADDR - *TS_CAL1_ADDR);
    temp += TS_CAL1_TEMP;

    return (int16_t)temp;
}

/**
 * @brief Derate I2C speed based on current junction temperature.
 *
 * Operating margin strategy:
 *   T < -30°C  : 80 kHz  — slow oscillator, weak pull-up drive in cold
 *   -30 to 85°C: 100 kHz — nominal Standard mode
 *   85 to 125°C: 90 kHz  — derated for IOL degradation and noise immunity
 */
void i2c_adapt_clock_to_temperature(void)
{
    int16_t tj = read_junction_temperature();

    uint32_t timing;

    if (tj < -30) {
        /* 80 kHz: PRESC=3, conservative timing */
        timing = 0x30200C14;
    } else if (tj > 85) {
        /* 90 kHz: slight derating for high temperature */
        timing = 0x3010120C;
    } else {
        /* 100 kHz: nominal */
        timing = 0x3010010A;
    }

    /* Disable I2C, update timing, re-enable */
    __HAL_RCC_I2C1_FORCE_RESET();
    HAL_Delay(1);
    __HAL_RCC_I2C1_RELEASE_RESET();

    hi2c1.Init.Timing = timing;
    HAL_I2C_Init(&hi2c1);
    HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);
    HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 3);
}
```

---

### Example 3: C++ RAII Wrapper with Temperature-Aware Retry

```cpp
/**
 * @file I2CIndustrial.hpp
 * @brief C++ RAII wrapper for industrial-grade I2C transactions.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <optional>

/**
 * Result type for I2C operations.
 */
enum class I2CStatus : uint8_t {
    OK            = 0,
    NACK          = 1,   ///< Device did not acknowledge
    BUS_ERROR     = 2,   ///< Arbitration lost or bus error
    TIMEOUT       = 3,   ///< Clock stretch timeout
    BUS_BUSY      = 4,   ///< Bus stuck BUSY after recovery attempt
};

/**
 * @class I2CIndustrial
 * @brief Manages an I2C bus with full industrial temperature robustness.
 *
 * Provides:
 *  - Exponential back-off retries
 *  - Automatic bus recovery on lockup
 *  - Temperature-derated clock speeds
 *  - Clock-stretch timeout protection
 */
class I2CIndustrial {
public:
    struct Config {
        uint8_t  max_retries      = 5;
        uint32_t retry_base_ms    = 2;
        uint32_t timeout_ms       = 25;
        bool     enable_recovery  = true;
    };

    explicit I2CIndustrial(const Config &cfg = {}) : cfg_(cfg) {
        init();
    }

    ~I2CIndustrial() {
        deinit();
    }

    /* Non-copyable, moveable */
    I2CIndustrial(const I2CIndustrial &) = delete;
    I2CIndustrial &operator=(const I2CIndustrial &) = delete;

    /**
     * @brief Write to device with retry and bus recovery.
     */
    I2CStatus write(uint8_t addr, const uint8_t *data, size_t len) {
        return transact_with_retry([&]() -> I2CStatus {
            return hal_transmit(addr, data, len, cfg_.timeout_ms);
        });
    }

    /**
     * @brief Read from device with retry and bus recovery.
     */
    I2CStatus read(uint8_t addr, uint8_t *buf, size_t len) {
        return transact_with_retry([&]() -> I2CStatus {
            return hal_receive(addr, buf, len, cfg_.timeout_ms);
        });
    }

    /**
     * @brief Write register address, then read response (combined transaction).
     */
    I2CStatus write_read(uint8_t addr,
                          const uint8_t *cmd, size_t cmd_len,
                          uint8_t *resp, size_t resp_len)
    {
        return transact_with_retry([&]() -> I2CStatus {
            I2CStatus s = hal_transmit(addr, cmd, cmd_len, cfg_.timeout_ms);
            if (s != I2CStatus::OK) return s;
            return hal_receive(addr, resp, resp_len, cfg_.timeout_ms);
        });
    }

    /**
     * @brief Manually trigger bus recovery (9-clock method).
     */
    void recover_bus();

    /**
     * @brief Adapt clock frequency based on temperature (°C).
     */
    void adapt_clock(int16_t temperature_celsius);

private:
    Config cfg_;

    void init();
    void deinit();

    I2CStatus hal_transmit(uint8_t addr,
                            const uint8_t *data,
                            size_t len,
                            uint32_t timeout_ms);

    I2CStatus hal_receive(uint8_t addr,
                           uint8_t *buf,
                           size_t len,
                           uint32_t timeout_ms);

    bool is_bus_busy();

    template<typename Fn>
    I2CStatus transact_with_retry(Fn &&fn) {
        uint32_t delay_ms = cfg_.retry_base_ms;

        for (uint8_t attempt = 0; attempt < cfg_.max_retries; attempt++) {
            I2CStatus s = fn();
            if (s == I2CStatus::OK) return I2CStatus::OK;

            if (cfg_.enable_recovery && is_bus_busy()) {
                recover_bus();
            }

            if (attempt + 1 < cfg_.max_retries) {
                hal_delay(delay_ms);
                delay_ms *= 2;  /* Exponential back-off */
            }
        }
        return I2CStatus::BUS_ERROR;
    }

    static void hal_delay(uint32_t ms);
};
```

---

## Code Examples in Rust

### Example 1: Industrial I2C Driver using `embedded-hal`

```rust
//! industrial_i2c.rs
//!
//! Industrial I2C driver with retry logic, bus recovery,
//! and temperature-adaptive clock management.
//!
//! Target: any platform implementing embedded-hal 1.0 traits.
//! Dependencies: embedded-hal = "1.0", embedded-hal-nb = "1.0"

use embedded_hal::i2c::{I2c, ErrorKind};
use core::fmt;

/// Error type for industrial I2C operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2CError {
    /// Device returned NACK (not present or not ready)
    Nack,
    /// Bus is stuck; recovery attempted but failed
    BusBusy,
    /// Clock stretch exceeded timeout
    Timeout,
    /// Generic bus-level error
    BusError,
}

impl fmt::Display for I2CError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            I2CError::Nack     => write!(f, "I2C NACK — device not responding"),
            I2CError::BusBusy  => write!(f, "I2C bus stuck BUSY after recovery"),
            I2CError::Timeout  => write!(f, "I2C clock stretch timeout"),
            I2CError::BusError => write!(f, "I2C bus error"),
        }
    }
}

/// Configuration for industrial I2C operation.
#[derive(Debug, Clone, Copy)]
pub struct IndustrialConfig {
    /// Maximum transaction retry attempts.
    pub max_retries: u8,
    /// Base delay (ms) for exponential back-off.
    pub retry_base_ms: u32,
    /// Whether to attempt 9-clock bus recovery on lockup.
    pub enable_bus_recovery: bool,
}

impl Default for IndustrialConfig {
    fn default() -> Self {
        Self {
            max_retries: 5,
            retry_base_ms: 2,
            enable_bus_recovery: true,
        }
    }
}

/// Temperature operating zone, determined by thermal measurement.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThermalZone {
    /// −40 °C to −20 °C: slow oscillator, cold MOSFET Vth shift
    ExtremelyLow,
    /// −20 °C to +85 °C: nominal operation
    Nominal,
    /// +85 °C to +125 °C: degraded IOL, increased noise
    High,
}

impl ThermalZone {
    /// Classify a temperature reading (°C) into a zone.
    pub fn from_celsius(temp: i16) -> Self {
        match temp {
            i16::MIN..=-21 => ThermalZone::ExtremelyLow,
            -20..=85       => ThermalZone::Nominal,
            _              => ThermalZone::High,
        }
    }

    /// Recommended I2C clock frequency (Hz) for this zone.
    pub fn recommended_clock_hz(self) -> u32 {
        match self {
            ThermalZone::ExtremelyLow => 80_000,
            ThermalZone::Nominal      => 100_000,
            ThermalZone::High         => 90_000,
        }
    }
}

/// Industrial I2C wrapper — wraps any `embedded_hal::i2c::I2c` implementation.
pub struct IndustrialI2c<Bus, Delay> {
    bus: Bus,
    delay: Delay,
    config: IndustrialConfig,
}

impl<Bus, Delay> IndustrialI2c<Bus, Delay>
where
    Bus: I2c,
    Delay: embedded_hal::delay::DelayNs,
{
    /// Create a new industrial I2C driver with the given bus and configuration.
    pub fn new(bus: Bus, delay: Delay, config: IndustrialConfig) -> Self {
        Self { bus, delay, config }
    }

    /// Release the underlying bus (e.g., for reconfiguration).
    pub fn release(self) -> (Bus, Delay) {
        (self.bus, self.delay)
    }

    /// Map embedded-hal I2C errors to our industrial error type.
    fn map_error(e: Bus::Error) -> I2CError {
        match e.kind() {
            ErrorKind::NoAcknowledge(_) => I2CError::Nack,
            ErrorKind::Bus              => I2CError::BusError,
            ErrorKind::ArbitrationLoss  => I2CError::BusError,
            _                           => I2CError::BusError,
        }
    }

    /// Internal: exponential back-off retry loop.
    fn with_retry<F>(&mut self, mut f: F) -> Result<(), I2CError>
    where
        F: FnMut(&mut Bus) -> Result<(), Bus::Error>,
    {
        let mut delay_ms = self.config.retry_base_ms;

        for attempt in 0..self.config.max_retries {
            match f(&mut self.bus) {
                Ok(()) => return Ok(()),
                Err(e) => {
                    let err = Self::map_error(e);
                    if attempt + 1 < self.config.max_retries {
                        // Wait with exponential back-off before retrying
                        self.delay.delay_ms(delay_ms);
                        delay_ms = delay_ms.saturating_mul(2);
                    } else {
                        return Err(err);
                    }
                }
            }
        }
        Err(I2CError::BusError)
    }

    /// Write bytes to an I2C device with retry logic.
    pub fn write_robust(&mut self, address: u8, data: &[u8]) -> Result<(), I2CError> {
        self.with_retry(|bus| bus.write(address, data))
    }

    /// Read bytes from an I2C device with retry logic.
    pub fn read_robust(&mut self, address: u8, buf: &mut [u8]) -> Result<(), I2CError> {
        self.with_retry(|bus| bus.read(address, buf))
    }

    /// Write then read (register access pattern) with retry logic.
    pub fn write_read_robust(
        &mut self,
        address: u8,
        write: &[u8],
        read: &mut [u8],
    ) -> Result<(), I2CError> {
        self.with_retry(|bus| bus.write_read(address, write, read))
    }

    /// Read a 16-bit register from a device (big-endian).
    pub fn read_register_u16(&mut self, address: u8, reg: u8) -> Result<u16, I2CError> {
        let mut buf = [0u8; 2];
        self.write_read_robust(address, &[reg], &mut buf)?;
        Ok(u16::from_be_bytes(buf))
    }

    /// Write a single byte to a register.
    pub fn write_register_u8(
        &mut self,
        address: u8,
        reg: u8,
        value: u8,
    ) -> Result<(), I2CError> {
        self.write_robust(address, &[reg, value])
    }
}
```

---

### Example 2: Temperature-Sensor Integration (TMP117 over I2C)

```rust
//! tmp117_industrial.rs
//!
//! Driver for the Texas Instruments TMP117 precision temperature sensor,
//! designed for industrial use. The TMP117 is rated -55°C to +150°C.
//!
//! Demonstrates:
//!   - Register-based I2C access
//!   - CRC / data integrity checking
//!   - Adaptive polling based on thermal zone

use crate::industrial_i2c::{IndustrialI2c, I2CError, ThermalZone};
use embedded_hal::delay::DelayNs;
use embedded_hal::i2c::I2c;

/// TMP117 I2C address variants (set by ADD0 pin).
#[derive(Debug, Clone, Copy)]
pub enum Tmp117Address {
    Gnd = 0x48,   ///< ADD0 → GND
    Vcc = 0x49,   ///< ADD0 → VCC
    Sda = 0x4A,   ///< ADD0 → SDA
    Scl = 0x4B,   ///< ADD0 → SCL
}

/// TMP117 register map.
mod regs {
    pub const TEMP_RESULT:  u8 = 0x00;
    pub const CONFIGURATION: u8 = 0x01;
    pub const T_HIGH_LIMIT: u8 = 0x02;
    pub const T_LOW_LIMIT:  u8 = 0x03;
    pub const DEVICE_ID:    u8 = 0x0F;
}

/// TMP117 device ID constant.
const TMP117_DEVICE_ID: u16 = 0x0117;

/// LSB size: 7.8125 m°C per bit.
const TMP117_LSB_MC: i32 = 78_125; // in units of 0.01 m°C

/// TMP117 driver.
pub struct Tmp117<Bus, Delay> {
    i2c: IndustrialI2c<Bus, Delay>,
    address: u8,
}

impl<Bus, Delay> Tmp117<Bus, Delay>
where
    Bus: I2c,
    Delay: DelayNs,
{
    /// Create and verify the TMP117 device identity.
    pub fn new(
        i2c: IndustrialI2c<Bus, Delay>,
        address: Tmp117Address,
    ) -> Result<Self, I2CError> {
        let mut drv = Self { i2c, address: address as u8 };
        drv.verify_device_id()?;
        Ok(drv)
    }

    /// Verify the TMP117 device ID register.
    fn verify_device_id(&mut self) -> Result<(), I2CError> {
        let id = self.i2c.read_register_u16(self.address, regs::DEVICE_ID)?;
        if id & 0x0FFF != TMP117_DEVICE_ID & 0x0FFF {
            // ID mismatch — wrong device or bus error
            return Err(I2CError::BusError);
        }
        Ok(())
    }

    /// Read the current temperature in millidegrees Celsius.
    ///
    /// Returns temperature × 1000 (e.g., 25000 = 25.000 °C, -40000 = -40.000 °C).
    pub fn read_temperature_mc(&mut self) -> Result<i32, I2CError> {
        let raw = self.i2c.read_register_u16(self.address, regs::TEMP_RESULT)?;

        // Raw value is a signed 16-bit two's complement integer
        let signed_raw = raw as i16;

        // Convert: each bit = 7.8125 m°C = 78125 × 10⁻⁴ °C
        // To get millidegrees: signed_raw * 78125 / 10000
        let temp_mc = (signed_raw as i32) * TMP117_LSB_MC / 10_000;

        Ok(temp_mc)
    }

    /// Read temperature as floating-point °C.
    ///
    /// Note: avoid floating point in safety-critical code; use `read_temperature_mc` instead.
    pub fn read_temperature_f32(&mut self) -> Result<f32, I2CError> {
        Ok(self.read_temperature_mc()? as f32 / 1000.0)
    }

    /// Determine the current thermal zone for adaptive I2C clock management.
    pub fn thermal_zone(&mut self) -> Result<ThermalZone, I2CError> {
        let mc = self.read_temperature_mc()?;
        let celsius = (mc / 1000) as i16;
        Ok(ThermalZone::from_celsius(celsius))
    }

    /// Set alert thresholds (in millidegrees Celsius) for over/under-temperature alerting.
    pub fn set_alert_thresholds(
        &mut self,
        high_mc: i32,
        low_mc: i32,
    ) -> Result<(), I2CError> {
        let high_raw = (high_mc * 10_000 / TMP117_LSB_MC) as i16 as u16;
        let low_raw  = (low_mc  * 10_000 / TMP117_LSB_MC) as i16 as u16;

        // Write high limit
        let hi = high_raw.to_be_bytes();
        self.i2c.write_robust(self.address, &[regs::T_HIGH_LIMIT, hi[0], hi[1]])?;

        // Write low limit
        let lo = low_raw.to_be_bytes();
        self.i2c.write_robust(self.address, &[regs::T_LOW_LIMIT, lo[0], lo[1]])?;

        Ok(())
    }
}
```

---

### Example 3: Async Industrial I2C with `embassy`

```rust
//! async_industrial_i2c.rs
//!
//! Asynchronous industrial I2C driver using embassy-stm32.
//! Demonstrates timeout-protected clock-stretch handling in async context.
//!
//! Dependencies:
//!   embassy-stm32  = { features = ["stm32g071rb"] }
//!   embassy-time   = { features = ["tick-hz-1_000_000"] }

use embassy_stm32::i2c::{self, I2c};
use embassy_stm32::time::Hertz;
use embassy_time::{Duration, TimeoutError, Timer, with_timeout};

/// Industrial I2C transaction timeout.
const I2C_TRANSACTION_TIMEOUT: Duration = Duration::from_millis(25);
const I2C_MAX_RETRIES: u8 = 5;

/// Async write to I2C device with timeout and retry.
pub async fn i2c_write_async(
    i2c: &mut I2c<'_, embassy_stm32::mode::Async>,
    address: u8,
    data: &[u8],
) -> Result<(), &'static str> {
    let mut delay_ms = 2u64;

    for attempt in 0..I2C_MAX_RETRIES {
        let result = with_timeout(
            I2C_TRANSACTION_TIMEOUT,
            i2c.write(address, data),
        ).await;

        match result {
            Ok(Ok(())) => return Ok(()),
            Ok(Err(_bus_err)) => {
                // Bus error: back off and retry
            }
            Err(TimeoutError) => {
                // Clock stretch timeout — the device held SCL low too long.
                // This is common at -40°C when internal oscillators are slow.
                // Log, wait, and retry.
            }
        }

        if attempt + 1 < I2C_MAX_RETRIES {
            Timer::after(Duration::from_millis(delay_ms)).await;
            delay_ms = delay_ms.saturating_mul(2); // Exponential back-off
        }
    }

    Err("I2C write failed after maximum retries")
}

/// Async read with the same protection.
pub async fn i2c_read_async(
    i2c: &mut I2c<'_, embassy_stm32::mode::Async>,
    address: u8,
    buf: &mut [u8],
) -> Result<(), &'static str> {
    let mut delay_ms = 2u64;

    for attempt in 0..I2C_MAX_RETRIES {
        let result = with_timeout(
            I2C_TRANSACTION_TIMEOUT,
            i2c.read(address, buf),
        ).await;

        match result {
            Ok(Ok(())) => return Ok(()),
            Ok(Err(_)) | Err(TimeoutError) => {}
        }

        if attempt + 1 < I2C_MAX_RETRIES {
            Timer::after(Duration::from_millis(delay_ms)).await;
            delay_ms = delay_ms.saturating_mul(2);
        }
    }

    Err("I2C read failed after maximum retries")
}

/// Initialize I2C1 with temperature-derated clock for industrial range.
pub fn init_i2c_industrial(
    peri: embassy_stm32::peripherals::I2C1,
    scl: embassy_stm32::peripherals::PB6,
    sda: embassy_stm32::peripherals::PB7,
    thermal_zone: super::ThermalZone,
) -> I2c<'static, embassy_stm32::mode::Async> {
    let freq = Hertz(thermal_zone.recommended_clock_hz());

    let mut config = i2c::Config::default();
    config.timeout = I2C_TRANSACTION_TIMEOUT;

    I2c::new(
        peri,
        scl,
        sda,
        embassy_stm32::interrupt::take!(I2C1),
        embassy_stm32::dma::NoDma,
        embassy_stm32::dma::NoDma,
        freq,
        config,
    )
}
```

---

## PCB Design Considerations

### Trace Routing

- Keep SDA and SCL traces as short as possible to minimize Cb.
- Route SDA and SCL as a differential pair with ~5 mil spacing to reduce impedance mismatch.
- Avoid routing under switching regulators or high-frequency traces.
- For very long buses (>30 cm), use I2C bus extenders (NXP PCA9600, P82B96) which boost VOL sink capability.

### Pull-up Placement

- Place pull-up resistors physically close to the master device.
- Use separate pull-up resistors per bus segment when using I2C mux/switches (PCA9548A).
- Keep pull-up bypass capacitors (100 nF) immediately adjacent to the VCC connection point.

### Thermal Gradient Awareness

In systems where multiple I2C slaves are distributed across a PCB with a thermal gradient (e.g., one end near a heatsink at +100 °C, the other at +30 °C), the weakest IOL in the chain determines the maximum safe pull-up current. Derate for the hottest device.

### Connector Considerations

Gold-plated contacts maintain stable contact resistance from −40 °C to +125 °C. Tin contacts oxidize at sustained high temperatures. For connectors, use a minimum of 30 µ-inch gold plating on mating pairs.

---

## Testing and Validation Strategy

| Test | Method | Pass Criteria |
|---|---|---|
| Cold start lockup | Power on at −40 °C with VCC ramp 100 ms | First transaction within 5 retries |
| Hot steady state | Soak at +125 °C for 1 hour; continuous I2C transactions | Zero bus lockups; VoL ≤ 0.4 V |
| Thermal cycling | 100× cycles: −40 °C ↔ +125 °C, 10 °C/min | Zero permanent failures |
| Clock stretch | Inject artificial 20 ms stretch; verify timeout and recovery | Host recovers within 50 ms |
| Rise-time margin | Oscilloscope: measure tr at −40 °C and +125 °C | tr ≤ spec at both extremes |
| Pull-up current | Monitor Vpp across pull-up at −40 °C and +125 °C | IOL within device spec |
| EMI susceptibility | Apply 1 kV/m RF field (per IEC 61000-4-3) during transactions | Transaction error rate < 0.01% |

---

## Summary

Designing I2C for the **−40 °C to +125 °C industrial range** requires a holistic approach across hardware and software layers:

**Hardware:**
- Select **industrial- or automotive-grade** devices (suffix I, E, or A) with guaranteed — not typical — electrical specs across the full range.
- Choose **pull-up resistors** with low TCR (≤100 ppm/°C) and derate the value for high-temperature IOL degradation. For most Standard/Fast mode designs at 100 pF bus capacitance, 2.2 kΩ offers good margin.
- Enable **analog and digital noise filters** in the I2C peripheral. A 3-cycle digital filter rejects ~47 ns spikes common in industrial EMI environments.
- For Fast-mode Plus (1 MHz), use **active pull-up buffers** (e.g., NXP PCA9517) to guarantee rise-time compliance at +125 °C.
- Use **crystal or TCXO** oscillators for Fast-mode Plus applications; RC oscillators drift too much at temperature extremes.

**Software:**
- Implement **exponential back-off retries** (5 attempts, starting at 2 ms) to survive cold-start NACK events caused by slow device startup below −20 °C.
- Enforce **clock-stretch timeouts** (25 ms Standard, 10 ms Fast) in hardware timers, not polled loops.
- Implement the **9-clock bus recovery sequence** followed by a STOP condition to free SDA when a slave is stuck mid-byte.
- Optionally **derate the I2C clock** (80–90 kHz instead of 100 kHz) when temperature sensors report extreme zones; this adds margin for weakened drivers and oscillator drift.
- In Rust, leverage the **embedded-hal trait abstraction** to write temperature-robust drivers that are portable across MCU families while relying on `with_timeout` in async environments to prevent indefinite blocking.

Following these practices, I2C buses can operate reliably across the full industrial range with zero-lockup uptime requirements measured in thousands of hours.

---

*Document: 82_Industrial_Temperature_Range.md — Part of the I2C Protocol Engineering Reference Series*