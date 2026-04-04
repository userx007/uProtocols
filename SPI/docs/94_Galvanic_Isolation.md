Now I'll create the comprehensive markdown document on Galvanic Isolation for SPI applications.Here's a full breakdown of what the document covers:

**Topic Coverage** spans 13 sections:

- **Concept & Why it Matters** — explains galvanic isolation, the hazards it prevents (ground loops, common-mode transients, patient safety, fault containment), and when it is legally required.
- **Isolation Technologies** — compares capacitive (TI ISO-series), magnetic/iCoupler (ADI ADuM-series), optocouplers, and SiC gate driver isolators with their trade-offs.
- **Key Parameters** — a reference table covering working voltage, dV/dt immunity, propagation delay, skew, max data rate, and failsafe output states.
- **Common ICs** — ISO7341, ADuM1401, ADuM3151 (SPI-specific), Si8641, with wiring diagram for all four SPI signals through the barrier.
- **Hardware Design** — decoupling strategy, PCB layout rules (creepage/clearance, ground plane separation), isolated power supply options, and failsafe CS̄ behavior.
- **Timing Analysis** — how propagation delay limits SPI clock frequency, pulse-width distortion, and channel-to-channel skew effects.

**Code Examples:**

| Language | Example |
|---|---|
| C | Isolated SPI init + CS settling delay (STM32 HAL) |
| C | ADS8688 16-bit ADC driver with pipeline-aware read |
| C++ | Fault-counting safety wrapper with safe-state callback |
| Rust | Generic `IsolatedSpi<SPI>` wrapper using `embedded-hal` traits |
| Rust | `Ads8688<SPI>` driver with millivolt conversion |
| Rust | **Type-state** safety manager — fault transitions enforced at compile time |

- **Standards** — IEC 60747-17, IEC 60601-1 (medical), IEC 61508 (SIL), UL 1577, ISO 26262 (automotive)
- **Troubleshooting** — four common failure modes with root causes and solutions

# 94. Galvanic Isolation — Using Digital Isolators for Safety-Critical SPI Applications

---

## Table of Contents

1. [Introduction](#introduction)
2. [What is Galvanic Isolation?](#what-is-galvanic-isolation)
3. [Why Galvanic Isolation Matters for SPI](#why-galvanic-isolation-matters-for-spi)
4. [Isolation Technologies](#isolation-technologies)
5. [Key Electrical Parameters](#key-electrical-parameters)
6. [Common Isolator ICs for SPI](#common-isolator-ics-for-spi)
7. [Hardware Design Principles](#hardware-design-principles)
8. [SPI Timing Considerations with Isolators](#spi-timing-considerations-with-isolators)
9. [Programming: C/C++ Examples](#programming-cc-examples)
10. [Programming: Rust Examples](#programming-rust-examples)
11. [Safety Standards and Compliance](#safety-standards-and-compliance)
12. [Troubleshooting](#troubleshooting)
13. [Summary](#summary)

---

## Introduction

Galvanic isolation is a fundamental technique in embedded systems design that physically and electrically separates two circuit domains while still allowing signal and/or power transfer between them. In safety-critical SPI (Serial Peripheral Interface) applications — such as medical devices, industrial motor controllers, battery management systems, and power converters — the absence of galvanic isolation can lead to dangerous ground loops, equipment damage, and, most critically, risk to human life.

This document provides a thorough treatment of the concept, relevant hardware components, timing implications, and practical firmware code in both **C/C++** and **Rust**.

---

## What is Galvanic Isolation?

Galvanic isolation ensures there is **no direct electrical (ohmic) path** between two circuit sections. The two sides — commonly called the **primary side** (controller/host) and the **secondary side** (field/load) — can operate at entirely different potentials, or even different ground references, without creating a destructive current path between them.

Signal transfer across the isolation barrier is achieved through:

- **Capacitive coupling** (used by Texas Instruments ISO-series, Analog Devices ADuM-series)
- **Magnetic (inductive/transformer) coupling** (used by Analog Devices iCoupler technology)
- **Optical coupling** (optocouplers — slower, but low cost)
- **Giant Magnetoresistance (GMR)** (less common in digital isolators)

```
  ┌───────────────────┐           ┌───────────────────┐
  │   Host Domain     │           │   Field Domain    │
  │  (MCU / FPGA)     │  ═══════  │  (ADC, DAC,       │
  │  VCC = 3.3V       │  BARRIER  │   Motor Drive)    │
  │  GND1             │           │  VCC2 = varies    │
  └───────────────────┘           │  GND2             │
                                  └───────────────────┘
         No direct DC path between GND1 and GND2
```

---

## Why Galvanic Isolation Matters for SPI

SPI is a synchronous, full-duplex serial protocol with four signals: **SCLK**, **MOSI**, **MISO**, and **CS̄** (chip select). In safety-critical environments, several hazards make isolation mandatory:

### 1. Ground Potential Differences

In industrial or automotive environments, different circuit boards or subsystems may have ground potentials separated by hundreds of volts (or more during fault conditions). Without isolation, this difference appears directly on the MCU's I/O pins, destroying them instantly.

### 2. Common-Mode Transients

High-voltage switching (IGBTs, MOSFETs in motor drives) creates fast common-mode voltage transients (dV/dt). These can capacitively couple noise into unisolated signal lines, causing erroneous readings or MCU resets.

### 3. Patient/Operator Safety (Medical)

IEC 60601-1 mandates creepage and clearance distances, and isolation ratings that protect patients and operators from dangerous leakage currents. Even milliamps at mains voltage can be lethal.

### 4. Fault Containment

If the high-voltage side fails (e.g., a shorted IGBT), isolation prevents the failure from propagating to the MCU and the rest of the control system.

### 5. Floating References

Many sensors (e.g., isolated current sense, high-side voltage measurement) have their ground floating relative to the MCU. Isolated SPI allows clean communication without a common reference.

---

## Isolation Technologies

### Capacitive Isolators

Capacitive coupling uses on-chip oxide capacitors to transfer encoded signals across the barrier. They offer:

- Very high dV/dt immunity (up to 150 kV/µs)
- Low propagation delay (< 10 ns typical)
- Low power consumption
- Integrated isolated power options

**Example:** Texas Instruments ISO7341 (quad channel, up to 100 Mbps)

### Magnetic (iCoupler) Isolators

Analog Devices' iCoupler technology uses planar air-core transformers fabricated on-die. Characteristics:

- Excellent noise immunity
- Low power (no optocoupler LED current required)
- Data rates up to 150 Mbps

**Example:** Analog Devices ADuM1401 (quad channel)

### Optocouplers

The traditional isolation method using an LED and photodetector. Characteristics:

- Inexpensive and widely available
- **Limited bandwidth** (typically < 10 Mbps)
- CTR degradation over lifetime/temperature
- Higher propagation delay (typically 50–500 ns)
- Not recommended for high-speed SPI (> 1 MHz)

### Silicon Carbide (SiC) Gate Driver Isolators

A specialized subset used when driving SiC or GaN power transistors at very high switching speeds. Often combine isolation with high-current gate drive capability.

---

## Key Electrical Parameters

When selecting a digital isolator for SPI, the following parameters are critical:

| Parameter | Description | Typical Range |
|---|---|---|
| **Working Voltage (VIOWM)** | Continuous isolation voltage | 100 V – 1500 V (RMS) |
| **Transient Isolation Voltage** | Peak surge withstand | 5 kV – 10 kV (1-min test) |
| **Reinforced vs. Basic Isolation** | Safety classification per IEC 60747-17 | Reinforced = 2× basic |
| **dV/dt Immunity** | Common-mode transient immunity | 25 kV/µs – 150 kV/µs |
| **Propagation Delay** | Signal latency through isolator | 5 ns – 150 ns |
| **Propagation Delay Skew** | Difference between channels | 1 ns – 20 ns |
| **Maximum Data Rate** | Highest supported signal frequency | 1 Mbps – 150 Mbps |
| **Output Default State** | Output level during VCC loss | High or Low (failsafe) |
| **Power Supply Range** | VCC1 and VCC2 | 1.8 V – 5.5 V |

### Isolation Voltage vs. Working Voltage

It is important to distinguish between:

- **Rated Isolation Voltage** (the test voltage applied in manufacturing/qualification — a dielectric withstand test)
- **Working Voltage (VIOWM)** — the maximum continuous voltage the isolator can sustain in normal operation
- **Reinforced Isolation** — provides double or triple the protection of basic isolation; required when the isolated side is accessible to users

---

## Common Isolator ICs for SPI

### Texas Instruments ISO7741 / ISO7341 Series

- 4-channel digital isolators (configurable direction)
- Up to 100 Mbps data rate
- 5 kV RMS isolation (1-min)
- dV/dt immunity: 100 kV/µs
- Propagation delay: ~10 ns
- Packages: SOIC-16 with wide body for creepage

### Analog Devices ADuM1401 / ADuM3401

- 4-channel iCoupler (3+1 or 2+2 direction)
- Up to 25 Mbps
- 2.5 kV RMS isolation
- Low power operation

### Analog Devices ADuM3151 (SPI-Specific)

- Designed specifically for 4-wire SPI isolation
- On-chip CS̄ timing management
- Prevents metastability at isolator startup
- Up to 17 MHz SPI clock

### Silicon Labs Si8641 / Si8645

- 4-channel isolators
- 5 kV RMS isolation
- Up to 150 Mbps
- Integrated isolated DC/DC power option (Si86xx series)

### Wiring the Isolator for SPI

```
    Host Side (GND1)                      Field Side (GND2)
   ┌──────────────┐                      ┌──────────────┐
   │  MCU         │                      │  SPI Device  │
   │  SCLK ───────┼──→ [ISO CH1] ──────→─┼──── SCLK     │
   │  MOSI ───────┼──→ [ISO CH2] ──────→─┼──── MOSI     │
   │  MISO ───────┼──← [ISO CH3] ──────←─┼──── MISO     │
   │  CS̄   ───────┼──→ [ISO CH4] ──────→─┼──── CS̄       │
   └──────────────┘    ════════════      └──────────────┘
                        ISOLATION
                         BARRIER
```

Note: MISO is the only channel running in the reverse direction (field → host). Most quad isolators support configurable channel direction; choose accordingly.

---

## Hardware Design Principles

### Decoupling and Bypassing

Each VCC pin of an isolator must have a 100 nF ceramic capacitor placed as close as possible to the pin, plus a 10 µF bulk capacitor per power rail. Poor bypassing is the single most common cause of isolator misbehavior.

```
VCC1 ──┬── 10 µF ── GND1
       └── 100 nF ── GND1   (placed < 1 mm from pin)

VCC2 ──┬── 10 µF ── GND2
       └── 100 nF ── GND2   (placed < 1 mm from pin)
```

### PCB Layout

- The two ground planes (**GND1** and **GND2**) must **never connect** on the PCB except through a defined safety earth or the isolator's own barrier.
- Maintain the required **creepage and clearance** distances across the isolation gap on the PCB (typically ≥ 8 mm for reinforced isolation at 250 V AC mains).
- Route isolated signals orthogonally to minimize capacitive coupling.
- Do not place copper pour across the isolation slot.

### Isolated Power Supply

The field side (GND2 domain) needs its own power supply. Options:

1. **Isolated DC/DC module** (e.g., Murata MGJ2D, Recom R-78series): recommended for high power.
2. **Integrated isolated power** in the isolator IC (e.g., Texas Instruments ISO7741-Q1 with ISOW7841).
3. **Transformer + rectifier**: traditional, high power, bulky.

### Failsafe Output States

Choose an isolator whose output default state (when VCC is absent) matches your application's safe state. For example:

- CS̄ should default **HIGH** (deselect) if the field-side supply fails, preventing spurious SPI transactions.
- SCLK should default **LOW** for SPI Mode 0/1.

---

## SPI Timing Considerations with Isolators

Every isolator adds propagation delay and pulse-width distortion. This has critical implications for SPI timing.

### Propagation Delay

Each isolator channel introduces a delay, typically 10–100 ns. For SPI, the total round-trip delay matters:

```
Total delay = tpd(SCLK) + tpd(MOSI) + tpd(MISO_return)
```

For an ISO7341 with 10 ns per channel:

```
Total ≈ 10 ns (SCLK) + 10 ns (MOSI) + 10 ns (MISO) = 30 ns round-trip
```

### Maximum SPI Clock Frequency

To maintain correct sampling, the setup and hold times of the SPI device on the field side must still be satisfied after the isolator delays. A practical rule of thumb:

```
f_SCLK_max ≈ 1 / (4 × tpd_max)
```

For a 10 ns isolator: `f_max ≈ 1 / (40 ns) = 25 MHz`
For a 100 ns isolator: `f_max ≈ 2.5 MHz`

### Pulse Width Distortion

Isolators may distort narrow pulses. The minimum pulse width supported is typically specified in the datasheet as `t_PW(min)`. For SCLK especially, ensure the SPI clock duty cycle is close to 50% and the half-period is greater than the isolator's minimum pulse width.

### Channel-to-Channel Skew

For SPI, MOSI and SCLK must remain coherent. Channel-to-channel skew within a quad isolator (propagation delay difference between channels) must be small compared to the SPI bit time. Typical values are 2–5 ns, which is acceptable up to ~100 MHz.

---

## Programming: C/C++ Examples

The C/C++ examples are written for a bare-metal embedded environment (e.g., STM32 HAL or similar ARM Cortex-M platform), but the concepts apply universally.

### Example 1: Basic Isolated SPI Initialization (STM32 HAL)

```c
/**
 * @file isolated_spi.c
 * @brief Isolated SPI driver for safety-critical applications.
 *
 * Hardware: STM32F4 MCU + ISO7341 digital isolator + ADS8688 ADC
 *
 * The isolator adds ~10 ns propagation delay per channel.
 * Maximum recommended SPI clock: 10 MHz for this configuration.
 */

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* SPI handle (configured via CubeMX or manually) */
extern SPI_HandleTypeDef hspi1;

/* Chip-select GPIO — must default HIGH when field supply is absent */
#define ISO_SPI_CS_PORT     GPIOA
#define ISO_SPI_CS_PIN      GPIO_PIN_4

/* Timeout in ms for SPI transfers */
#define ISO_SPI_TIMEOUT_MS  10U

/* Minimum CS de-assert time after transfer (field-side device requirement) */
#define ISO_CS_DEASSERT_NS  100U   /* 100 ns — verified against isolator tpd */

/**
 * @brief Assert chip select (active LOW).
 * The CS line passes through an isolator channel.
 * A small software delay may be necessary to allow the isolator
 * output to settle before the first SCLK edge.
 */
static inline void iso_spi_cs_assert(void)
{
    HAL_GPIO_WritePin(ISO_SPI_CS_PORT, ISO_SPI_CS_PIN, GPIO_PIN_RESET);

    /*
     * Insert a settling delay equal to the isolator propagation delay.
     * For ISO7341 (tpd ~10 ns), this is negligible at system clock.
     * For slower optocouplers (tpd ~150 ns), this may require a NOP loop.
     * Adjust based on device datasheet.
     */
    __DSB();  /* Data Synchronization Barrier — ensures GPIO write completes */
    __NOP();  /* ~6 ns on a 168 MHz Cortex-M4; add more NOPs if required     */
}

static inline void iso_spi_cs_deassert(void)
{
    HAL_GPIO_WritePin(ISO_SPI_CS_PORT, ISO_SPI_CS_PIN, GPIO_PIN_SET);
}

/**
 * @brief Initialize the isolated SPI interface.
 *
 * Sets SPI clock to 10 MHz (safe with ISO7341 tpd of 10 ns).
 * SPI Mode 0: CPOL=0, CPHA=0.
 */
void iso_spi_init(void)
{
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;    /* CPOL = 0 */
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;     /* CPHA = 0 */
    hspi1.Init.NSS               = SPI_NSS_SOFT;        /* Software CS control */
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16; /* 84/16 = 5.25 MHz */
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        /* Safety-critical: halt or enter safe state on init failure */
        Error_Handler();
    }

    /* Ensure CS is de-asserted at startup */
    iso_spi_cs_deassert();
}

/**
 * @brief Perform a full-duplex SPI transaction through the isolator.
 *
 * @param tx_buf  Transmit buffer (MOSI)
 * @param rx_buf  Receive buffer  (MISO)
 * @param length  Number of bytes to transfer
 * @return true on success, false on error
 *
 * Safety Note: Always check the return value in safety-critical code.
 * A failed SPI transfer must trigger a safe-state response (e.g.,
 * disable PWM outputs, assert fault signal).
 */
bool iso_spi_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t length)
{
    HAL_StatusTypeDef status;

    iso_spi_cs_assert();

    status = HAL_SPI_TransmitReceive(&hspi1,
                                     (uint8_t *)tx_buf,
                                     rx_buf,
                                     length,
                                     ISO_SPI_TIMEOUT_MS);

    iso_spi_cs_deassert();

    if (status != HAL_OK) {
        /* Log error; in a real system, trigger fault handling */
        return false;
    }

    return true;
}
```

---

### Example 2: Reading an Isolated ADC (16-bit, SPI Mode 1)

This example reads from an isolated 16-bit ADC (e.g., ADS8688) used in a high-voltage power monitoring application.

```c
/**
 * @file isolated_adc.c
 * @brief Reading an isolated SPI ADC across a digital isolator barrier.
 *
 * Target device: ADS8688 (Texas Instruments, 16-bit, 8-channel)
 * Isolator:      ISO7341 (Texas Instruments)
 * Application:   High-voltage battery monitoring (HV side isolated from MCU)
 */

#include "isolated_spi.h"
#include <stdint.h>
#include <stdbool.h>

/* ADS8688 command definitions */
#define ADS8688_CMD_NO_OP       0x0000U  /* Continue previous operation */
#define ADS8688_CMD_STDBY       0x8200U  /* Standby mode                */
#define ADS8688_CMD_PWR_DN      0x8300U  /* Power down                  */
#define ADS8688_CMD_RST         0x8500U  /* Full device reset            */
#define ADS8688_CMD_AUTO_RST    0xA000U  /* Auto-scan reset sequence     */
#define ADS8688_CMD_MAN_CH(n)   (0xC000U | ((n) << 7))  /* Manual channel select */

/* Voltage reference (internal 4.096 V reference) */
#define ADS8688_VREF_MV         4096U
#define ADS8688_ADC_BITS        16U
#define ADS8688_FULL_SCALE      ((1U << ADS8688_ADC_BITS) - 1U)

/**
 * @brief Send a 32-bit command frame to ADS8688 and receive 32-bit response.
 *
 * ADS8688 uses a 32-bit transaction:
 *   TX: [16-bit command][16-bit don't-care]
 *   RX: [16-bit previous result][16-bit status/channel info]
 */
static uint32_t ads8688_send_cmd(uint16_t cmd)
{
    uint8_t tx[4] = {
        (uint8_t)(cmd >> 8),
        (uint8_t)(cmd & 0xFF),
        0x00,
        0x00
    };
    uint8_t rx[4] = {0};

    if (!iso_spi_transfer(tx, rx, 4)) {
        /* Transfer failure: return error sentinel */
        return 0xFFFFFFFFU;
    }

    return ((uint32_t)rx[0] << 24) |
           ((uint32_t)rx[1] << 16) |
           ((uint32_t)rx[2] <<  8) |
           ((uint32_t)rx[3]);
}

/**
 * @brief Read a single channel from the isolated ADS8688 ADC.
 *
 * @param channel  Channel number (0–7)
 * @param mv_out   Output: converted voltage in millivolts
 * @return true on success, false on communication error
 *
 * Note: Due to the pipelined nature of ADS8688, the result returned
 * during the command frame is from the PREVIOUS conversion. A dummy
 * read is required after channel selection to obtain fresh data.
 */
bool ads8688_read_channel_mv(uint8_t channel, int32_t *mv_out)
{
    if (channel > 7 || mv_out == NULL) {
        return false;
    }

    /* Step 1: Select channel — result from this call is stale (previous channel) */
    ads8688_send_cmd(ADS8688_CMD_MAN_CH(channel));

    /* Step 2: Issue NO_OP — result is now valid for selected channel */
    uint32_t response = ads8688_send_cmd(ADS8688_CMD_NO_OP);

    if (response == 0xFFFFFFFFU) {
        return false;  /* Communication error through isolator */
    }

    /* Extract 16-bit ADC result from upper 16 bits of response */
    uint16_t raw_adc = (uint16_t)(response >> 16);

    /*
     * Convert to millivolts.
     * ADS8688 range with VREF = 4.096 V:
     *   Default input range = ±VREF*1.25 = ±5.12 V
     *   but for unipolar: 0 to VREF*1.25
     *
     * For safety-critical applications, verify full-scale calibration.
     */
    *mv_out = ((int32_t)raw_adc * (int32_t)ADS8688_VREF_MV * 125)
              / ((int32_t)ADS8688_FULL_SCALE * 100);

    return true;
}

/**
 * @brief Reset the isolated ADC to a known safe state.
 *
 * Called during system startup and after any fault condition.
 * Ensures the ADC is not left in an undefined state across the barrier.
 */
void ads8688_reset(void)
{
    ads8688_send_cmd(ADS8688_CMD_RST);

    /* Allow 1 ms for device to complete reset (datasheet: t_reset < 500 µs) */
    HAL_Delay(1);
}
```

---

### Example 3: Fault Detection and Safe-State Handling (C++)

```cpp
/**
 * @file iso_spi_safety.cpp
 * @brief Safety wrapper for isolated SPI with fault detection.
 *
 * Demonstrates a watchdog-based approach for detecting isolator supply
 * failure and entering a defined safe state.
 */

#include "isolated_spi.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <array>

/// Maximum consecutive transfer failures before declaring fault
constexpr uint8_t  MAX_FAIL_COUNT   = 3;

/// SPI transfer result with error classification
enum class IsoSpiError : uint8_t {
    None          = 0,
    Timeout       = 1,
    BarrierFault  = 2,  ///< Consistent failure suggesting isolator supply loss
    ChecksumError = 3,
    BusyFault     = 4,
};

struct IsoSpiResult {
    bool        success;
    IsoSpiError error;
    uint32_t    raw_data;
};

/**
 * @class IsolatedSpiDriver
 * @brief Thread-safe (interrupt-safe on bare-metal) isolated SPI driver
 *        with fault counting and safe-state assertion.
 */
class IsolatedSpiDriver {
public:
    using SafeStateCb = std::function<void()>;

    /**
     * @brief Construct driver with a safe-state callback.
     *
     * The callback is invoked when the fault threshold is exceeded.
     * It should: disable PWM outputs, open contactors, assert fault LED, etc.
     */
    explicit IsolatedSpiDriver(SafeStateCb safe_state_fn)
        : safe_state_fn_(safe_state_fn)
        , fail_count_(0)
        , in_fault_(false)
    {}

    /**
     * @brief Transfer data through the isolator with fault monitoring.
     */
    IsoSpiResult transfer(const uint8_t* tx, uint8_t* rx, uint16_t len)
    {
        if (in_fault_) {
            return { false, IsoSpiError::BarrierFault, 0 };
        }

        bool ok = iso_spi_transfer(tx, rx, len);

        if (!ok) {
            ++fail_count_;
            if (fail_count_ >= MAX_FAIL_COUNT) {
                declare_fault();
                return { false, IsoSpiError::BarrierFault, 0 };
            }
            return { false, IsoSpiError::Timeout, 0 };
        }

        /* Success — reset failure counter */
        fail_count_ = 0;
        return { true, IsoSpiError::None, 0 };
    }

    /**
     * @brief Check if the driver has entered fault state.
     */
    bool is_faulted() const { return in_fault_; }

    /**
     * @brief Clear fault state (requires external system validation first).
     *
     * Safety Note: In IEC 61508 SIL-2 and above applications, a fault
     * clearance should require explicit operator acknowledgment or
     * hardware interlock reset.
     */
    void clear_fault()
    {
        fail_count_ = 0;
        in_fault_   = false;
    }

private:
    void declare_fault()
    {
        in_fault_ = true;
        if (safe_state_fn_) {
            safe_state_fn_();  ///< Invoke application-specific safe state
        }
    }

    SafeStateCb safe_state_fn_;
    uint8_t     fail_count_;
    bool        in_fault_;
};

/* ─── Application-level safe state function ─── */

static void enter_safe_state(void)
{
    /* Example: Disable all gate drive signals, open output contactors */
    HAL_GPIO_WritePin(FAULT_PORT, FAULT_PIN, GPIO_PIN_SET);   /* Assert fault output */
    /* Additional system-specific safe-state actions here */
}

/* ─── Usage example ─── */

void app_isolated_spi_demo(void)
{
    IsolatedSpiDriver driver(enter_safe_state);

    std::array<uint8_t, 4> tx_buf = { 0xC0, 0x00, 0x00, 0x00 };
    std::array<uint8_t, 4> rx_buf = {};

    for (int i = 0; i < 100; ++i) {
        auto result = driver.transfer(tx_buf.data(), rx_buf.data(), 4);

        if (!result.success) {
            if (result.error == IsoSpiError::BarrierFault) {
                /* System has entered safe state automatically */
                break;
            }
            /* Transient error — retry logic or degraded operation */
            continue;
        }

        /* Process valid ADC reading */
        uint16_t adc_val = ((uint16_t)rx_buf[0] << 8) | rx_buf[1];
        (void)adc_val;  /* Use value in application */
    }
}
```

---

## Programming: Rust Examples

The Rust examples use the [`embedded-hal`](https://github.com/rust-embedded/embedded-hal) trait abstractions, which are hardware-agnostic. They are compatible with any platform that implements `embedded-hal` (STM32, nRF52, RP2040, etc.).

### Example 1: Isolated SPI HAL Wrapper (Rust)

```rust
//! isolated_spi.rs
//! 
//! A safe, generic wrapper for SPI communication across a digital isolator.
//! Uses embedded-hal traits for hardware independence.
//!
//! Dependencies (Cargo.toml):
//!   embedded-hal = "1.0"

use embedded_hal::spi::{Operation, SpiDevice};
use embedded_hal::digital::OutputPin;
use core::fmt;

/// Errors that can occur during isolated SPI communication.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IsoSpiError<E> {
    /// Underlying SPI bus error
    Spi(E),
    /// Transfer failed (possible isolator barrier issue)
    TransferFailed,
    /// Invalid parameter
    InvalidParam,
}

impl<E: fmt::Debug> fmt::Display for IsoSpiError<E> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Spi(e)          => write!(f, "SPI error: {:?}", e),
            Self::TransferFailed  => write!(f, "Isolated SPI transfer failed"),
            Self::InvalidParam    => write!(f, "Invalid parameter"),
        }
    }
}

/// A wrapper around an `SpiDevice` that represents an SPI bus
/// routed through a galvanic isolation barrier.
///
/// This type makes the isolation boundary explicit in the type system.
/// Any access to the inner bus must go through this wrapper,
/// which enforces proper error handling and delay insertion.
pub struct IsolatedSpi<SPI> {
    /// The underlying SPI device (already includes CS management)
    spi: SPI,
    /// Count of consecutive communication failures
    fail_count: u8,
    /// Maximum failures before the device is considered faulted
    max_failures: u8,
}

impl<SPI, E> IsolatedSpi<SPI>
where
    SPI: SpiDevice<Error = E>,
{
    /// Create a new `IsolatedSpi` wrapper.
    ///
    /// # Parameters
    /// - `spi`: An `SpiDevice` implementation (handles CS internally)
    /// - `max_failures`: Threshold for fault declaration (typically 3–5)
    pub fn new(spi: SPI, max_failures: u8) -> Self {
        Self {
            spi,
            fail_count: 0,
            max_failures,
        }
    }

    /// Returns `true` if the device has exceeded its failure threshold.
    pub fn is_faulted(&self) -> bool {
        self.fail_count >= self.max_failures
    }

    /// Reset the failure counter (use with caution in safety-critical code).
    pub fn clear_fault(&mut self) {
        self.fail_count = 0;
    }

    /// Perform a full-duplex SPI transfer through the isolation barrier.
    ///
    /// # Safety Invariant
    /// The caller must handle `Err` results, especially `TransferFailed`,
    /// and must not continue normal operation if `is_faulted()` returns true.
    pub fn transfer(&mut self, data: &mut [u8]) -> Result<(), IsoSpiError<E>> {
        if self.is_faulted() {
            return Err(IsoSpiError::TransferFailed);
        }

        self.spi
            .transfer_in_place(data)
            .map_err(|e| {
                self.fail_count = self.fail_count.saturating_add(1);
                IsoSpiError::Spi(e)
            })?;

        // Successful transfer — reset failure counter
        self.fail_count = 0;
        Ok(())
    }

    /// Perform a write-only transfer (MOSI only, ignore MISO).
    pub fn write(&mut self, data: &[u8]) -> Result<(), IsoSpiError<E>> {
        if self.is_faulted() {
            return Err(IsoSpiError::TransferFailed);
        }

        self.spi
            .write(data)
            .map_err(|e| {
                self.fail_count = self.fail_count.saturating_add(1);
                IsoSpiError::Spi(e)
            })?;

        self.fail_count = 0;
        Ok(())
    }
}
```

---

### Example 2: Isolated ADC Driver (Rust, ADS8688)

```rust
//! ads8688.rs
//! 
//! Driver for the ADS8688 16-bit SPI ADC, accessed through an
//! ISO7341 galvanic isolator.

use crate::isolated_spi::{IsolatedSpi, IsoSpiError};
use embedded_hal::spi::SpiDevice;

/// ADS8688 channel selection commands
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ads8688Channel {
    Ch0 = 0,
    Ch1 = 1,
    Ch2 = 2,
    Ch3 = 3,
    Ch4 = 4,
    Ch5 = 5,
    Ch6 = 6,
    Ch7 = 7,
}

impl Ads8688Channel {
    fn command_word(self) -> u16 {
        0xC000 | ((self as u16) << 7)
    }
}

/// Voltage range configuration for ADS8688 input channels
#[derive(Debug, Clone, Copy)]
pub enum InputRange {
    /// ±2.5 × VREF
    Bipolar2_5x,
    /// ±1.25 × VREF (default)
    Bipolar1_25x,
    /// 0 to 2.5 × VREF
    Unipolar2_5x,
    /// 0 to 1.25 × VREF
    Unipolar1_25x,
}

/// ADS8688 driver using an isolated SPI bus.
pub struct Ads8688<SPI> {
    iso_spi: IsolatedSpi<SPI>,
    /// Internal voltage reference in millivolts (4096 mV for internal ref)
    vref_mv: u32,
}

impl<SPI, E> Ads8688<SPI>
where
    SPI: SpiDevice<Error = E>,
    E: core::fmt::Debug,
{
    /// Construct the driver with a known VREF (internal = 4096 mV).
    pub fn new(iso_spi: IsolatedSpi<SPI>, vref_mv: u32) -> Self {
        Self { iso_spi, vref_mv }
    }

    /// Send a 32-bit command frame and return the 32-bit response.
    ///
    /// ADS8688 transaction format:
    /// TX: [CMD_HIGH][CMD_LOW][0x00][0x00]
    /// RX: [RESULT_HIGH][RESULT_LOW][CH_INFO][STATUS]
    fn send_cmd(&mut self, cmd: u16) -> Result<u32, IsoSpiError<E>> {
        let mut buf: [u8; 4] = [
            (cmd >> 8) as u8,
            (cmd & 0xFF) as u8,
            0x00,
            0x00,
        ];

        self.iso_spi.transfer(&mut buf)?;

        Ok(u32::from_be_bytes(buf))
    }

    /// Reset the device to a known state.
    ///
    /// Must be called after power-up and after any fault condition.
    pub fn reset(&mut self) -> Result<(), IsoSpiError<E>> {
        self.send_cmd(0x8500)?;  // RST command
        // In a real system, insert a delay here: e.g., timer.delay_us(500)
        Ok(())
    }

    /// Read a single ADC channel.
    ///
    /// Returns the result in millivolts (signed, for bipolar ranges).
    ///
    /// # Pipelining Note
    /// ADS8688 is pipelined: the result returned during a command frame
    /// corresponds to the *previous* conversion. This function issues
    /// an extra NO_OP to flush the pipeline and return fresh data.
    pub fn read_channel_mv(&mut self, channel: Ads8688Channel) -> Result<i32, IsoSpiError<E>> {
        // Pipeline flush: select channel (discard stale result)
        self.send_cmd(channel.command_word())?;

        // Read fresh result with NO_OP
        let response = self.send_cmd(0x0000)?;

        let raw_adc = (response >> 16) as u16;
        let mv = self.raw_to_mv(raw_adc, InputRange::Bipolar1_25x);

        Ok(mv)
    }

    /// Convert raw ADC code to millivolts for a given input range.
    fn raw_to_mv(&self, raw: u16, range: InputRange) -> i32 {
        let full_scale: u32 = 0xFFFF;

        // Scale factor relative to VREF (numerator/denominator)
        let (num, den, bipolar): (i64, i64, bool) = match range {
            InputRange::Bipolar2_5x   => (250, 100, true),
            InputRange::Bipolar1_25x  => (125, 100, true),
            InputRange::Unipolar2_5x  => (250, 100, false),
            InputRange::Unipolar1_25x => (125, 100, false),
        };

        if bipolar {
            // Bipolar: two's complement, centered at 0x8000
            let signed_raw = raw as i16;  // Two's complement interpretation
            ((signed_raw as i64) * (self.vref_mv as i64) * num)
                / ((full_scale as i64) * den / 2) as i32
        } else {
            // Unipolar: 0 to full-scale
            (((raw as i64) * (self.vref_mv as i64) * num)
                / ((full_scale as i64) * den)) as i32
        }
    }

    /// Check if the underlying isolated SPI has exceeded fault threshold.
    pub fn is_comms_faulted(&self) -> bool {
        self.iso_spi.is_faulted()
    }
}
```

---

### Example 3: Safe-State Manager with Ownership (Rust)

Rust's ownership model provides a powerful way to statically enforce safe-state transitions — once a fault is declared, the SPI resource can be consumed and made unavailable until an explicit reset is performed.

```rust
//! safe_state.rs
//!
//! Uses Rust's type system to enforce safe-state transitions.
//! An `IsolatedSpiHandle` can only exist in `Operational` or `Faulted` state.
//! Transitions are explicit and type-checked at compile time.

use core::marker::PhantomData;
use embedded_hal::spi::SpiDevice;
use crate::ads8688::Ads8688;

/// Type-state marker: system operating normally
pub struct Operational;

/// Type-state marker: system in fault / safe state
pub struct Faulted;

/// A handle to the isolated SPI subsystem, parameterized by state.
///
/// When in `Operational` state, data acquisition is permitted.
/// When in `Faulted` state, only safe-state actions are available.
/// The transition is irreversible without an explicit `recover()`.
pub struct IsolatedSystem<SPI, State> {
    adc: Ads8688<SPI>,
    _state: PhantomData<State>,
}

impl<SPI, E> IsolatedSystem<SPI, Operational>
where
    SPI: SpiDevice<Error = E>,
    E: core::fmt::Debug,
{
    /// Create a new system handle in the `Operational` state.
    pub fn new(adc: Ads8688<SPI>) -> Self {
        Self { adc, _state: PhantomData }
    }

    /// Attempt to read an ADC channel.
    ///
    /// On success: returns the reading and keeps the system operational.
    /// On fault:   *consumes* the handle and returns a `Faulted` handle,
    ///             making it impossible to continue data acquisition
    ///             without an explicit recovery step.
    pub fn read_channel(
        mut self,
        channel: crate::ads8688::Ads8688Channel,
    ) -> Result<(i32, Self), IsolatedSystem<SPI, Faulted>>
    {
        match self.adc.read_channel_mv(channel) {
            Ok(mv) => Ok((mv, self)),
            Err(_) if self.adc.is_comms_faulted() => {
                // Fault threshold exceeded: transition to Faulted state
                // The Operational handle is consumed — caller cannot ignore this
                Err(IsolatedSystem { adc: self.adc, _state: PhantomData })
            }
            Err(_) => {
                // Transient error — system remains Operational
                // Return a sentinel value and the handle
                Ok((i32::MIN, self))
            }
        }
    }
}

impl<SPI, E> IsolatedSystem<SPI, Faulted>
where
    SPI: SpiDevice<Error = E>,
    E: core::fmt::Debug,
{
    /// Perform the safe-state actions: disable outputs, log fault.
    ///
    /// This is the only action permitted when faulted.
    pub fn execute_safe_state(&self) {
        // Platform-specific: disable PWM, open contactors, etc.
        // In embedded context: write to GPIO, call HAL functions
    }

    /// Attempt recovery after hardware has been verified safe.
    ///
    /// In safety-critical systems (IEC 61508 SIL-2+), this should
    /// require external confirmation (e.g., operator acknowledgment,
    /// power cycle, or hardware interlock).
    pub fn recover(mut self) -> IsolatedSystem<SPI, Operational> {
        self.adc.is_comms_faulted();  // Would call clear in a real implementation
        IsolatedSystem { adc: self.adc, _state: PhantomData }
    }
}

/// Example application loop using the type-state API
pub fn run_acquisition<SPI, E>(system: IsolatedSystem<SPI, Operational>)
where
    SPI: SpiDevice<Error = E>,
    E: core::fmt::Debug,
{
    let mut current_system = system;

    loop {
        match current_system.read_channel(crate::ads8688::Ads8688Channel::Ch0) {
            Ok((mv, next_system)) => {
                // Process reading
                let _ = mv;
                current_system = next_system;
            }
            Err(faulted_system) => {
                // System is now faulted — CANNOT call read_channel anymore
                // Compiler enforces this: `faulted_system` is IsolatedSystem<_, Faulted>
                faulted_system.execute_safe_state();
                // Loop exits; recovery requires human/system intervention
                break;
            }
        }
    }
}
```

---

## Safety Standards and Compliance

Isolated SPI designs for safety-critical products must meet relevant standards:

### IEC 60747-17 (Digital Isolators)

The primary component-level standard for digital isolators. Defines:
- **Basic isolation**: one level of protection
- **Reinforced isolation**: equivalent to two basic insulations in series
- Working voltage, transient voltage, and partial discharge requirements

### IEC 60601-1 (Medical Electrical Equipment)

- Specifies **Means of Patient Protection (MOPP)** and **Means of Operator Protection (MOOP)**
- Leakage current limits: 10 µA (patient-applied parts, normal conditions)
- Creepage and clearance requirements based on pollution degree and overvoltage category

### IEC 61508 (Functional Safety)

- SIL (Safety Integrity Level) classification: SIL 1–4
- Requires systematic analysis of hardware and software failure modes
- Diagnostic coverage, safe failure fraction, and proof test interval requirements

### UL 1577 (Optocouplers) / UL 60950-1 / UL 62368-1

- North American safety standards for isolation components and end products
- Component-level testing for isolators (partial discharge, dielectric withstand)

### IEC 62386 / ISO 26262 (Automotive)

- ASIL (Automotive Safety Integrity Level) requirements for automotive isolated SPI (e.g., battery management, motor controllers)

---

## Troubleshooting

### Issue: SPI data corruption at high clock speeds

**Cause:** Propagation delay of isolator reduces setup/hold margin at the SPI device.

**Solution:**
- Reduce SPI clock frequency. Calculate maximum safe frequency as `1 / (4 × tpd_max)`.
- Use a digital isolator with lower propagation delay.
- Add pipeline registers at the field side if protocol allows.

### Issue: Intermittent communication failures

**Cause 1:** Ground plane coupling — GND1 and GND2 inadvertently connected through a chassis or connector ground, creating a partial path and noise injection.

**Solution:** Audit PCB and connector wiring for unintended GND connections.

**Cause 2:** Insufficient bypass capacitance on VCC2.

**Solution:** Verify 100 nF ceramic is placed < 1 mm from every VCC pin.

### Issue: Isolator output stuck at default state

**Cause:** VCC2 (field-side supply) is absent or below minimum operating voltage.

**Solution:** Verify isolated power supply is operational. Check for overload or short on the field side.

### Issue: Excessive EMI from isolated SPI traces

**Cause:** High dV/dt switching combined with long trace lengths acts as an antenna.

**Solution:**
- Keep SPI traces on the field side as short as possible.
- Add series termination resistors (22–33 Ω) close to the isolator outputs.
- Use ground-referenced shielding for traces spanning long distances.

---

## Summary

Galvanic isolation in SPI systems is not merely a convenience feature — it is a **fundamental safety requirement** in any application where high voltages, floating references, ground potential differences, or human safety are involved. Key takeaways:

**Technology choice matters.** Capacitive and magnetic digital isolators vastly outperform optocouplers in bandwidth, propagation delay, and long-term reliability. For SPI above 1 MHz, use dedicated digital isolators (ISO7341, ADuM3151, Si8641).

**Timing is the primary design constraint.** Every isolator adds propagation delay and channel-to-channel skew. Derate SPI clock frequency accordingly: reduce to `1/(4 × tpd_max)` as a safe starting point.

**PCB layout is safety-critical.** Maintain required creepage/clearance distances across the isolation gap. Never allow GND1 and GND2 to connect except through the defined isolation path. Decouple both VCC rails independently.

**Firmware must handle failures.** Isolated SPI can fail silently if the field-side supply collapses. Implement fault counting, safe-state callbacks, and — in Rust — leverage the type system to make fault states unignorable at compile time.

**Compliance requires documentation.** Select isolators with recognized certifications (IEC 60747-17, UL 1577) matching your product's safety classification. Reinforced isolation is required wherever the isolated side is accessible to users.

The C/C++ examples demonstrate concrete HAL-level drivers, ADC communication with pipeline awareness, and a C++ fault-management wrapper. The Rust examples show how `embedded-hal` traits provide hardware-agnostic isolation, and how type-state programming can make unsafe API usage a **compile-time error** rather than a runtime hazard — an approach particularly well-suited to safety-critical embedded development.

---

*Document: 94 — Galvanic Isolation: Using Digital Isolators for Safety-Critical SPI Applications*  
*Revision: 1.0*