# 79. Ground Bounce Prevention in SPI Systems

**Document Structure:**

- **Physics & Theory** — The `V = L × dI/dt` equation explained, why SPI is especially susceptible (simultaneous edge switching on SCLK/MOSI/MISO/CS), and a table of package inductance by type (DIP to BGA)
- **Hardware Strategies** — Ground planes, decoupling capacitor placement/sizing, series termination resistors, star ground topology for mixed-signal, ferrite beads, and GPIO slew rate control
- **9 Code Examples** spanning both C/C++ and Rust:
  - **C/C++**: STM32 GPIO drive strength config, noise-profile–based clock prescaler selection, staggered multi-slave CS assertion, DMA burst transfers with inter-burst recovery gaps, and RP2040 direct register access for pad slew rate
  - **Rust (Embassy)**: Noise-aware SPI config builder, async staggered CS transfers with `with_cs` closures, chunked burst transmission with recovery gaps, and an adaptive clock rate controller that reduces frequency on detected errors
- **Verification Checklist** — Oscilloscope measurement targets and a hardware/firmware checklist
- **Summary** — Distills the core principle: minimize `L`, minimize `dI/dt`, or do both


## Designing Power Distribution to Minimize Ground Bounce Effects

---

## Table of Contents

1. [Introduction](#introduction)
2. [What Is Ground Bounce?](#what-is-ground-bounce)
3. [Physics of Ground Bounce in SPI](#physics-of-ground-bounce-in-spi)
4. [Sources of Ground Bounce in SPI](#sources-of-ground-bounce-in-spi)
5. [Hardware Design Strategies](#hardware-design-strategies)
6. [Software and Firmware Strategies](#software-and-firmware-strategies)
7. [C/C++ Implementation Examples](#cc-implementation-examples)
8. [Rust Implementation Examples](#rust-implementation-examples)
9. [Measurement and Verification](#measurement-and-verification)
10. [Summary](#summary)

---

## Introduction

Serial Peripheral Interface (SPI) is a high-speed synchronous communication protocol widely used in embedded systems for connecting microcontrollers to peripherals such as ADCs, DACs, memory devices, displays, and sensors. Operating at clock speeds from a few MHz to hundreds of MHz in modern systems, SPI signals switch rapidly — and this rapid switching is a primary contributor to a phenomenon known as **ground bounce**.

Ground bounce is one of the most insidious signal integrity problems in digital systems. It can cause data corruption, spurious glitches, increased electromagnetic interference (EMI), and even hardware damage if not properly managed. This document examines ground bounce from first principles, explains why SPI is particularly susceptible, and provides concrete hardware and software mitigation strategies, illustrated with C/C++ and Rust code examples.

---

## What Is Ground Bounce?

**Ground bounce** (also called **simultaneous switching noise** or **SSN**) is a transient voltage fluctuation on the ground (GND) or power (VCC) rails that occurs when multiple output drivers switch simultaneously. The term "bounce" reflects the oscillatory nature of the voltage deviation — the ground reference does not stay at 0 V but instead temporarily rises or falls, causing the effective logic levels of signals to shift.

### Simplified Model

The ground rail is not a perfect zero-impedance conductor. It has:

- **Inductance (L)**: From PCB traces, via holes, bond wires, and package leads
- **Resistance (R)**: From copper conductors and contact resistance
- **Capacitance (C)**: Between power and ground planes

When a driver switches from logic HIGH to LOW, it discharges the load capacitance through the ground path. The rate of change of current `dI/dt` through the parasitic inductance `L` generates a voltage:

```
V_bounce = L × (dI/dt)
```

For a single output switching with `dI = 10 mA` in `dt = 1 ns` through `L = 5 nH`:

```
V_bounce = 5 nH × (10 mA / 1 ns) = 50 mV
```

Fifty millivolts on the ground rail is enough to misinterpret logic levels in 1.8 V systems, and when multiple lines switch simultaneously (as commonly happens in SPI), effects multiply.

---

## Physics of Ground Bounce in SPI

### SPI Signal Characteristics

A typical SPI transaction involves:

| Signal | Direction         | Behavior During Transaction          |
|--------|-------------------|--------------------------------------|
| SCLK   | Master → Slave    | Toggles continuously at clock rate   |
| MOSI   | Master → Slave    | Switches on every clock edge         |
| MISO   | Slave → Master    | Switches on every clock edge         |
| CS/SS  | Master → Slave    | Asserts low at start, deasserts high |

When a byte is transmitted, all these signals are switching, often simultaneously. The current demand spikes during each clock edge.

### Why SPI Is Particularly Susceptible

1. **High clock frequencies**: Modern SPI runs at 10–100+ MHz, meaning switching edges every 5–50 ns.
2. **Multiple simultaneous edges**: SCLK, MOSI, and MISO may all switch within picoseconds of each other at each clock edge.
3. **CS assertion/deassertion**: The CS line switching causes a large, sudden current demand from any logic-level translator or buffer driven by it.
4. **DMA burst transfers**: DMA-driven SPI can transmit continuously, creating sustained high-frequency switching events.
5. **Multi-device buses**: Multiple SPI slaves on the same bus multiplies the aggregate switching current.

### The Inductive Ground Loop

```
+3.3V Supply
     |
     |   L_vcc (package + trace inductance)
     |
[MCU/Peripheral]
     |
     |   L_gnd (package + trace inductance)
     |
   GND plane
```

The voltage drop across `L_gnd` is the ground bounce voltage. The VCC-side inductance similarly causes power rail sag. Together, these cause the IC's internal supply to oscillate, producing noise that couples into data lines, analog references, and neighboring circuits.

---

## Sources of Ground Bounce in SPI

### 1. Package and Bond Wire Inductance

IC packages introduce several nanohenries of inductance in power and ground pins:

| Package Type       | Typical Ground Inductance |
|--------------------|--------------------------|
| DIP (through-hole) | 15–30 nH                 |
| SOIC               | 4–8 nH                   |
| QFP                | 3–7 nH                   |
| BGA                | 0.5–2 nH                 |
| Flip-chip          | < 0.2 nH                 |

### 2. PCB Trace and Via Inductance

- Typical PCB trace: ~1 nH per mm at 0.1 mm width
- A via: ~0.5–1 nH
- A ground via to plane: ~0.3–0.5 nH

### 3. Decoupling Capacitor Lead Inductance

Even decoupling capacitors have inductance from their leads and mounting pads. A 100 nF 0402 capacitor has approximately 0.3–0.6 nH of effective series inductance (ESL), which limits its effectiveness above ~300–500 MHz.

### 4. Shared Ground Impedance

When multiple ICs share a common ground path, the switching of one IC creates a voltage on the shared ground that appears as noise at every other IC's ground pin.

---

## Hardware Design Strategies

### Strategy 1: Ground Plane

The single most effective hardware mitigation is an **unbroken ground plane** (typically on an inner layer of a multi-layer PCB). The plane provides:

- Very low inductance (< 0.1 nH for distributed current return)
- Controlled return current paths directly under signal traces
- High capacitance to the power plane (natural decoupling)

**Rules:**
- Never route SPI signals across splits or slots in the ground plane
- Keep SPI signal traces as short as possible
- Route SCLK, MOSI, MISO as close together as possible, with ground plane return below

### Strategy 2: Decoupling Capacitors

Decoupling capacitors placed close to IC power pins act as local charge reservoirs, supplying the transient current demand rather than the bulk supply rail.

**Recommended decoupling strategy for SPI ICs:**

| Capacitor Value | Purpose                        | Placement                          |
|-----------------|--------------------------------|------------------------------------|
| 100 nF (0402)   | High-frequency decoupling      | Within 0.5 mm of each VCC pin      |
| 10 µF (ceramic) | Mid-frequency bulk decoupling  | Within 5 mm of each IC             |
| 100 µF (electrolytic) | Low-frequency bulk reserve | Per power domain, near entry point |

Multiple values in parallel cover different frequency ranges because each capacitor's self-resonant frequency (SRF) limits its effectiveness above that frequency.

### Strategy 3: Series Termination Resistors

A series resistor (22–100 Ω) on each SPI line slows down signal edges, reducing `dI/dt` and thus reducing `V_bounce`. This is particularly effective for:

- SCLK lines (reduces the clock edge's drive current)
- MOSI and MISO (reduces data edge noise)

The resistor should be placed at the **driver** end, close to the pin, not at the receiver.

### Strategy 4: Separate Power Domains and Star Ground

In mixed-signal systems (e.g., MCU + ADC over SPI), use separate power domains with a single star ground connection point:

```
          +3.3V_analog          +3.3V_digital
               |                      |
           [ADC/DAC]               [MCU]
               |                      |
         GND_analog             GND_digital
               \                    /
                \                  /
                 +--- Star point --+
                          |
                        GND
```

This prevents digital switching noise from corrupting analog ground, which is crucial for ADC/DAC accuracy.

### Strategy 5: Ferrite Beads

A ferrite bead in series with the VCC line of a noise-sensitive peripheral filters high-frequency current spikes without the DC resistance of a resistor. It acts as a frequency-dependent impedance:

- Low impedance at DC and low frequencies (passes supply current)
- High impedance at high frequencies (blocks switching transients)

Choose ferrite beads rated for the required current and with impedance peaking in the 100–300 MHz range for SPI applications.

### Strategy 6: Controlled Slew Rate GPIO

Most modern MCUs allow configuring GPIO output slew rate (drive strength). Reducing the slew rate of SPI pins decreases `dI/dt` at the cost of slightly longer edge transitions.

---

## Software and Firmware Strategies

While hardware design is the primary defense against ground bounce, firmware can contribute meaningfully through:

1. **Controlling SPI clock frequency** — lower frequency = slower edges = less `dI/dt`
2. **Avoiding simultaneous switching** — staggering CS assertions, sequencing transfers
3. **Configuring GPIO drive strength** — register-level control of output impedance
4. **DMA burst management** — inserting inter-burst gaps to allow supply recovery
5. **Clock phase/polarity selection** — CPOL/CPHA configuration affects switching patterns

---

## C/C++ Implementation Examples

### Example 1: GPIO Drive Strength Configuration (STM32 HAL)

Reducing GPIO drive strength directly reduces `dI/dt` on SPI lines.

```c
#include "stm32f4xx_hal.h"

/**
 * @brief Configure SPI GPIO pins with reduced drive strength
 *        to minimize ground bounce on SCLK, MOSI, MISO, CS lines.
 *
 *        STM32 GPIO speed settings (approximate edge times):
 *        GPIO_SPEED_FREQ_LOW       ~  2 MHz  (slowest, lowest bounce)
 *        GPIO_SPEED_FREQ_MEDIUM    ~  25 MHz
 *        GPIO_SPEED_FREQ_HIGH      ~  50 MHz
 *        GPIO_SPEED_FREQ_VERY_HIGH ~ 100 MHz (fastest, highest bounce)
 */
void SPI1_GPIO_Init_LowBounce(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA5 = SCLK, PA7 = MOSI */
    gpio.Pin   = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    /* Use MEDIUM speed unless >10 MHz SPI is needed */
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PA6 = MISO */
    gpio.Pin  = GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP; /* Prevent floating MISO glitches */
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PA4 = CS (software-controlled) */
    gpio.Pin   = GPIO_PIN_4;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW; /* CS changes rarely — use lowest speed */
    HAL_GPIO_Init(GPIOA, &gpio);
}
```

---

### Example 2: SPI Clock Frequency Selection Based on Bus Load

```c
#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef enum {
    SPI_NOISE_PROFILE_LOW  = 0, /* Few devices, short traces, good decoupling */
    SPI_NOISE_PROFILE_MED  = 1, /* Moderate bus length or device count */
    SPI_NOISE_PROFILE_HIGH = 2  /* Long traces, many devices, poor supply */
} SPI_NoiseProfile_t;

/**
 * @brief Return appropriate SPI prescaler for a given noise profile.
 *        Lower clock = less dI/dt = less ground bounce.
 *        APB2 clock = 84 MHz on STM32F4 with typical config.
 */
uint32_t SPI_GetPrescaler(SPI_NoiseProfile_t profile)
{
    switch (profile) {
        case SPI_NOISE_PROFILE_LOW:
            return SPI_BAUDRATEPRESCALER_4;   /* 84/4  = 21 MHz */
        case SPI_NOISE_PROFILE_MED:
            return SPI_BAUDRATEPRESCALER_8;   /* 84/8  = 10.5 MHz */
        case SPI_NOISE_PROFILE_HIGH:
        default:
            return SPI_BAUDRATEPRESCALER_16;  /* 84/16 = 5.25 MHz */
    }
}

/**
 * @brief Initialize SPI1 with noise-aware clock frequency.
 */
HAL_StatusTypeDef SPI1_Init_NoiseAware(SPI_NoiseProfile_t profile)
{
    extern SPI_HandleTypeDef hspi1;

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;   /* CPOL=0 */
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;    /* CPHA=0 */
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_GetPrescaler(profile);
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;

    return HAL_SPI_Init(&hspi1);
}
```

---

### Example 3: Staggered Multi-Slave CS Assertion

Asserting multiple CS lines simultaneously causes simultaneous current spikes in all selected peripherals. Staggering the assertions reduces the peak ground current demand.

```c
#include "stm32f4xx_hal.h"
#include <stdint.h>

#define CS_STAGGER_DELAY_US  2U   /* Minimum stagger between CS assertions */

#define CS_ADC_PIN     GPIO_PIN_4
#define CS_DAC_PIN     GPIO_PIN_12
#define CS_FLASH_PIN   GPIO_PIN_11
#define CS_PORT        GPIOB

/**
 * @brief Delay in microseconds (busy-wait).
 *        Replace with DWT cycle counter for production code.
 */
static void delay_us(uint32_t us)
{
    /* Assumes 168 MHz clock; adjust cycles_per_us accordingly */
    volatile uint32_t cycles = us * 168U;
    while (cycles--);
}

/**
 * @brief Assert a single SPI chip-select with settling time.
 *        Allows supply voltage and ground to stabilize before
 *        the first clock edge, reducing bounce-induced corruption.
 */
void SPI_AssertCS(GPIO_TypeDef *port, uint16_t pin, uint32_t settle_us)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET); /* CS active low */
    delay_us(settle_us);                          /* Allow supply to settle */
}

/**
 * @brief Deassert a single SPI chip-select with recovery time.
 *        Allows ground to recover before the next transaction.
 */
void SPI_DeassertCS(GPIO_TypeDef *port, uint16_t pin, uint32_t recover_us)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    delay_us(recover_us);
}

/**
 * @brief Example: read from ADC and DAC on separate SPI transactions,
 *        staggering CS assertions to prevent simultaneous switching.
 */
void SPI_StaggeredMultiSlave_Example(SPI_HandleTypeDef *hspi)
{
    uint8_t adc_buf[2] = {0};
    uint8_t dac_buf[2] = {0xC0, 0x80}; /* Example DAC command */

    /* --- ADC Transaction --- */
    SPI_AssertCS(CS_PORT, CS_ADC_PIN, CS_STAGGER_DELAY_US);
    HAL_SPI_Receive(hspi, adc_buf, sizeof(adc_buf), 100);
    SPI_DeassertCS(CS_PORT, CS_ADC_PIN, CS_STAGGER_DELAY_US);

    /* Stagger gap: allow ground to recover before next assertion */
    delay_us(CS_STAGGER_DELAY_US * 2U);

    /* --- DAC Transaction --- */
    SPI_AssertCS(CS_PORT, CS_DAC_PIN, CS_STAGGER_DELAY_US);
    HAL_SPI_Transmit(hspi, dac_buf, sizeof(dac_buf), 100);
    SPI_DeassertCS(CS_PORT, CS_DAC_PIN, CS_STAGGER_DELAY_US);
}
```

---

### Example 4: DMA Transfer with Inter-Burst Gap

Continuous DMA-driven SPI transfers create sustained high-frequency switching. Inserting a small gap between DMA bursts allows the power supply to recover.

```c
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <string.h>

#define BURST_SIZE_BYTES     32U     /* Bytes per DMA burst */
#define INTER_BURST_DELAY_US 10U     /* Supply recovery time between bursts */
#define MAX_TRANSFER_BYTES   256U

extern SPI_HandleTypeDef  hspi1;
extern DMA_HandleTypeDef  hdma_spi1_tx;

volatile uint8_t dma_complete = 0;

/* DMA complete callback */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        dma_complete = 1;
    }
}

/**
 * @brief Transmit a large buffer over SPI using DMA,
 *        divided into bursts with supply-recovery gaps.
 *
 * @param data    Pointer to data buffer
 * @param length  Total bytes to transmit
 */
HAL_StatusTypeDef SPI_TransmitBurst(const uint8_t *data, uint16_t length)
{
    uint16_t offset    = 0;
    uint16_t remaining = length;

    HAL_GPIO_WritePin(CS_PORT, CS_ADC_PIN, GPIO_PIN_RESET);

    while (remaining > 0) {
        uint16_t chunk = (remaining > BURST_SIZE_BYTES)
                         ? BURST_SIZE_BYTES
                         : remaining;

        dma_complete = 0;

        HAL_StatusTypeDef status =
            HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)(data + offset), chunk);

        if (status != HAL_OK) {
            HAL_GPIO_WritePin(CS_PORT, CS_ADC_PIN, GPIO_PIN_SET);
            return status;
        }

        /* Wait for DMA to complete */
        uint32_t timeout = HAL_GetTick() + 100;
        while (!dma_complete) {
            if (HAL_GetTick() > timeout) {
                HAL_GPIO_WritePin(CS_PORT, CS_ADC_PIN, GPIO_PIN_SET);
                return HAL_TIMEOUT;
            }
        }

        offset    += chunk;
        remaining -= chunk;

        /* Inter-burst supply recovery gap */
        if (remaining > 0) {
            volatile uint32_t cycles = INTER_BURST_DELAY_US * 168U;
            while (cycles--);
        }
    }

    HAL_GPIO_WritePin(CS_PORT, CS_ADC_PIN, GPIO_PIN_SET);
    return HAL_OK;
}
```

---

### Example 5: Direct Register Manipulation for Slew Rate (RP2040 / Pico C SDK)

```c
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <stdint.h>

#define SPI_PORT    spi0
#define PIN_SCLK    18
#define PIN_MOSI    19
#define PIN_MISO    16
#define PIN_CS      17

/**
 * @brief Configure SPI pins on RP2040 with slow slew rate.
 *        The RP2040 PADS_BANK0 register controls slew rate per GPIO:
 *          SLEWFAST bit (bit 0): 0 = slow, 1 = fast
 *        Slow slew rate reduces dI/dt, reducing ground bounce.
 *
 *        Drive strength is also configurable:
 *          DRIVE bits [5:4]: 00=2mA, 01=4mA, 10=8mA, 11=12mA
 */
void SPI_Init_LowBounce(void)
{
    spi_init(SPI_PORT, 4 * 1000 * 1000); /* 4 MHz — conservative for noisy board */

    /* Configure SPI function on GPIO pins */
    gpio_set_function(PIN_SCLK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);

    /* CS as software GPIO */
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1); /* Deassert */

    /*
     * Directly access pad control registers to set:
     *   - Slow slew rate (SLEWFAST = 0)
     *   - Reduced drive strength (2 mA instead of 4 mA default)
     *
     * PADS_BANK0 base = 0x4001C000
     * GPIO_N offset   = 0x04 + N * 0x04
     * Bit 0  = SLEWFAST
     * Bits 5:4 = DRIVE (00=2mA, 01=4mA, 10=8mA, 11=12mA)
     */
    volatile uint32_t *pad_sclk = (volatile uint32_t *)(0x4001C000 + 0x04 + PIN_SCLK * 4);
    volatile uint32_t *pad_mosi = (volatile uint32_t *)(0x4001C000 + 0x04 + PIN_MOSI * 4);
    volatile uint32_t *pad_cs   = (volatile uint32_t *)(0x4001C000 + 0x04 + PIN_CS   * 4);

    /* Clear SLEWFAST (bit 0) → slow slew rate */
    *pad_sclk &= ~(1u << 0);
    *pad_mosi &= ~(1u << 0);
    *pad_cs   &= ~(1u << 0);

    /* Set drive strength to 2 mA: DRIVE[5:4] = 00 */
    *pad_sclk &= ~(0x3u << 4);
    *pad_mosi &= ~(0x3u << 4);
    *pad_cs   &= ~(0x3u << 4);
}
```

---

## Rust Implementation Examples

### Example 6: SPI Initialization with Noise-Aware Configuration (Embassy / STM32)

```rust
//! Ground bounce prevention via SPI clock rate and GPIO speed control.
//! Uses Embassy async framework with STM32 HAL.
//!
//! Cargo.toml dependencies:
//!   embassy-stm32 = { version = "0.1", features = ["stm32f401ce", "time-driver-any"] }
//!   embassy-executor = { version = "0.4", features = ["arch-cortex-m"] }
//!   embassy-time = "0.3"

use embassy_stm32::gpio::{Level, Output, Pull, Speed};
use embassy_stm32::spi::{self, Spi, Config as SpiConfig};
use embassy_stm32::time::Hertz;
use embassy_time::{Duration, Timer};

/// Noise profile determines SPI clock frequency.
/// Lower frequency → slower edges → less dI/dt → less ground bounce.
#[derive(Debug, Clone, Copy)]
pub enum NoiseProfile {
    /// Short traces, good decoupling, few devices — up to ~20 MHz
    Low,
    /// Moderate trace length or device count — up to ~10 MHz
    Medium,
    /// Long traces, many devices, or poor power supply — up to ~5 MHz
    High,
}

impl NoiseProfile {
    pub fn max_clock_hz(&self) -> u32 {
        match self {
            NoiseProfile::Low    => 20_000_000,
            NoiseProfile::Medium => 10_000_000,
            NoiseProfile::High   =>  5_000_000,
        }
    }
}

/// Build an SpiConfig tuned for minimum ground bounce.
///
/// Key settings:
/// - Clock frequency limited by noise profile
/// - CPOL=0, CPHA=0 (Mode 0) — edges only during active half-cycle
pub fn build_spi_config(profile: NoiseProfile) -> SpiConfig {
    let mut cfg = SpiConfig::default();
    cfg.frequency = Hertz(profile.max_clock_hz());
    // CPOL=0, CPHA=0 is the Embassy default (Mode 0)
    cfg
}

/// Construct a GPIO output with the lowest drive speed for CS lines.
///
/// `Speed::Low` maps to ~2 MHz GPIO bandwidth, significantly reducing
/// the dI/dt of CS assertion/deassertion transients.
pub fn make_cs_pin<'d, T: embassy_stm32::gpio::Pin>(
    pin: impl embassy_stm32::Peripheral<P = T> + 'd,
) -> Output<'d, T> {
    Output::new(pin, Level::High, Speed::Low)
}
```

---

### Example 7: Staggered Multi-Slave SPI Transfers in Rust (Embassy Async)

```rust
//! Demonstrates staggered CS assertion timing to prevent simultaneous
//! switching noise from multiple SPI peripherals sharing a bus.

use embassy_stm32::gpio::Output;
use embassy_stm32::spi::Spi;
use embassy_time::{Duration, Timer};

/// Minimum stagger delay between CS assertions.
/// 2 µs allows the ground and power rail to settle after one assertion
/// before inducing another transient with the next CS line.
const CS_SETTLE_US: u64  = 2;
const CS_RECOVER_US: u64 = 2;

/// Assert CS, wait for supply/ground to settle, run a closure, then deassert.
async fn with_cs<'a, T, F, Fut>(cs: &mut Output<'a, T>, f: F) -> Fut::Output
where
    T: embassy_stm32::gpio::Pin,
    F: FnOnce() -> Fut,
    Fut: core::future::Future,
{
    cs.set_low();
    Timer::after(Duration::from_micros(CS_SETTLE_US)).await;

    let result = f().await;

    Timer::after(Duration::from_micros(CS_RECOVER_US)).await;
    cs.set_high();

    result
}

/// Perform staggered reads from two SPI devices on the same bus.
/// The inter-transaction gap prevents simultaneous power demand spikes.
pub async fn read_adc_then_sensor<'a, T1, T2>(
    spi: &mut Spi<'a, embassy_stm32::peripherals::SPI1,
                      embassy_stm32::peripherals::DMA2_CH3,
                      embassy_stm32::peripherals::DMA2_CH0>,
    cs_adc: &mut Output<'a, T1>,
    cs_sensor: &mut Output<'a, T2>,
) -> ([u8; 2], [u8; 4])
where
    T1: embassy_stm32::gpio::Pin,
    T2: embassy_stm32::gpio::Pin,
{
    // --- First device: ADC ---
    let mut adc_buf = [0u8; 2];
    with_cs(cs_adc, || async {
        spi.transfer_in_place(&mut adc_buf).await.ok();
    }).await;

    // Inter-transaction stagger gap: ground recovery time
    Timer::after(Duration::from_micros(CS_SETTLE_US * 2)).await;

    // --- Second device: Sensor ---
    let mut sensor_buf = [0u8; 4];
    with_cs(cs_sensor, || async {
        spi.transfer_in_place(&mut sensor_buf).await.ok();
    }).await;

    (adc_buf, sensor_buf)
}
```

---

### Example 8: Burst Transfer with Supply Recovery Gaps in Rust

```rust
//! Chunked DMA-style SPI transfer with inter-burst recovery gaps.
//! Prevents sustained ground bounce from continuous large transfers.

use embassy_stm32::spi::Spi;
use embassy_stm32::gpio::Output;
use embassy_time::{Duration, Timer};

/// Number of bytes per burst.
/// Smaller bursts = more frequent recovery gaps = less sustained bounce.
const BURST_BYTES: usize = 32;

/// Recovery time between bursts in microseconds.
/// 10 µs is conservative; may be reduced based on measured supply response.
const BURST_RECOVERY_US: u64 = 10;

/// Transmit a large buffer over SPI in fixed-size bursts,
/// inserting a supply-recovery gap between each burst.
///
/// # Arguments
/// * `spi` - Mutable SPI peripheral reference
/// * `cs`  - Chip select output (active low)
/// * `data` - Slice of bytes to transmit
pub async fn transmit_burst<'a, T>(
    spi: &mut Spi<'a, embassy_stm32::peripherals::SPI1,
                      embassy_stm32::peripherals::DMA2_CH3,
                      embassy_stm32::peripherals::DMA2_CH0>,
    cs: &mut Output<'a, T>,
    data: &[u8],
) -> Result<(), embassy_stm32::spi::Error>
where
    T: embassy_stm32::gpio::Pin,
{
    cs.set_low();

    let chunks: core::slice::Chunks<'_, u8> = data.chunks(BURST_BYTES);
    let total_chunks = data.len().div_ceil(BURST_BYTES);

    for (i, chunk) in chunks.enumerate() {
        spi.write(chunk).await?;

        // Insert recovery gap between all bursts except the last
        if i + 1 < total_chunks {
            Timer::after(Duration::from_micros(BURST_RECOVERY_US)).await;
        }
    }

    cs.set_high();
    Ok(())
}
```

---

### Example 9: Runtime Noise Monitoring and Adaptive Clock Scaling (Rust)

This example demonstrates a software feedback loop that monitors for SPI errors (which often indicate signal integrity problems from ground bounce) and dynamically reduces clock speed if errors are detected.

```rust
//! Adaptive SPI clock rate controller.
//! Detects transfer errors attributable to ground bounce and
//! reduces clock frequency to improve signal integrity.

use embassy_stm32::spi::{Spi, Config as SpiConfig, Error as SpiError};
use embassy_stm32::time::Hertz;
use embassy_time::{Duration, Timer};

/// Adaptive SPI clock controller.
pub struct AdaptiveSpiController {
    /// Current clock frequency in Hz
    current_hz: u32,
    /// Minimum allowed clock frequency
    min_hz: u32,
    /// Maximum allowed clock frequency
    max_hz: u32,
    /// Consecutive error count
    error_count: u32,
    /// Consecutive success count (used to attempt clock increase)
    success_count: u32,
}

impl AdaptiveSpiController {
    pub fn new(initial_hz: u32, min_hz: u32, max_hz: u32) -> Self {
        Self {
            current_hz: initial_hz,
            min_hz,
            max_hz,
            error_count: 0,
            success_count: 0,
        }
    }

    /// Notify the controller of a successful transfer.
    /// After enough successes, attempt to increase the clock rate.
    pub fn record_success(&mut self) -> Option<u32> {
        self.error_count = 0;
        self.success_count += 1;

        // After 100 consecutive successes, try increasing clock by 10%
        if self.success_count >= 100 {
            self.success_count = 0;
            let new_hz = (self.current_hz as f64 * 1.1) as u32;
            if new_hz <= self.max_hz {
                self.current_hz = new_hz;
                return Some(self.current_hz);
            }
        }
        None
    }

    /// Notify the controller of a transfer error (possible ground bounce).
    /// Reduces clock rate by 25% to improve signal integrity.
    pub fn record_error(&mut self) -> Option<u32> {
        self.success_count = 0;
        self.error_count += 1;

        // On any error, immediately reduce clock by 25%
        let new_hz = (self.current_hz as f64 * 0.75) as u32;
        if new_hz >= self.min_hz {
            self.current_hz = new_hz;
            Some(self.current_hz)
        } else {
            self.current_hz = self.min_hz;
            Some(self.min_hz)
        }
    }

    pub fn current_hz(&self) -> u32 {
        self.current_hz
    }

    pub fn error_count(&self) -> u32 {
        self.error_count
    }
}

/// Example usage of the adaptive controller in an async task.
/// In practice, reconfiguring SPI frequency requires reinitializing
/// the peripheral — this shows the control logic pattern.
pub async fn adaptive_transfer_example(data: &[u8]) {
    let mut controller = AdaptiveSpiController::new(
        10_000_000, // Start at 10 MHz
        1_000_000,  // Never go below 1 MHz
        20_000_000, // Never exceed 20 MHz
    );

    // Simulate transfer loop (replace with real SPI calls in production)
    for _ in 0..10 {
        // Simulate: true = success, false = error
        let transfer_ok = true; // Replace: spi.write(data).await.is_ok()

        if transfer_ok {
            if let Some(new_hz) = controller.record_success() {
                // In real code: reinitialize SPI with new_hz
                // defmt::info!("Increasing SPI clock to {} Hz", new_hz);
                let _ = new_hz;
            }
        } else {
            if let Some(new_hz) = controller.record_error() {
                // In real code: reinitialize SPI with new_hz
                // defmt::warn!("Ground bounce suspected! Reducing clock to {} Hz", new_hz);
                let _ = new_hz;
                Timer::after(Duration::from_millis(1)).await; // Allow supply to stabilize
            }
        }
    }
}
```

---

## Measurement and Verification

After implementing mitigation strategies, verify their effectiveness with:

### Oscilloscope Measurements

1. **Ground bounce amplitude**: Probe between IC ground pin and chassis ground. Target < 50 mV peak for 3.3 V systems.
2. **Power rail noise**: Probe VCC at the IC power pin. Target < 5% of supply voltage (< 165 mV for 3.3 V).
3. **SPI signal integrity**: Examine SCLK for jitter and MISO/MOSI for glitches during rapid switching.

### Checklist for Ground Bounce Mitigation

| Item                              | Verified? |
|-----------------------------------|-----------|
| Solid, unbroken ground plane      | ☐         |
| 100 nF decoupling per VCC pin     | ☐         |
| 10 µF bulk decoupling per IC      | ☐         |
| Series termination resistors      | ☐         |
| GPIO drive strength reduced       | ☐         |
| SPI clock ≤ minimum needed        | ☐         |
| Staggered CS assertions           | ☐         |
| SPI traces over solid ground      | ☐         |
| Analog/digital ground star point  | ☐         |

---

## Summary

Ground bounce in SPI systems arises from the parasitic inductance in IC packages, PCB traces, and via holes — inductance through which the rapidly changing SPI switching currents flow. The resulting `V = L × dI/dt` transient momentarily lifts the local ground above the ideal 0 V, corrupting logic levels, introducing jitter, and increasing EMI.

**The primary hardware defenses are:**

- A continuous, unbroken ground plane that minimizes path inductance for return currents
- Well-placed decoupling capacitors (100 nF close, 10 µF in the vicinity) that supply local transient current demand
- Series termination resistors on SPI lines that slow edge rates and reduce peak `dI/dt`
- Reduced GPIO drive strength configured in hardware registers

**The primary firmware defenses are:**

- Setting SPI clock frequency to the minimum required for the application — slower clocks produce slower edges and less noise
- Staggering CS assertions across multiple slaves to prevent simultaneous switching current spikes
- Dividing large DMA transfers into bursts with inter-burst recovery gaps so the supply voltage has time to stabilize
- Adaptive clock rate reduction in response to detected transfer errors, which often indicate signal integrity problems

The most cost-effective approach is always to address ground bounce at the PCB layout stage. Once hardware is fixed, software can provide additional robustness through frequency management, timing control, and error-adaptive behavior. Together, these techniques ensure reliable SPI communication even in electrically demanding environments.

---

*Document: SPI Topic #79 — Ground Bounce Prevention*
*Languages: C/C++ (STM32 HAL, RP2040 Pico SDK) | Rust (Embassy async)*