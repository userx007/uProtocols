# 92. SPI in Industrial Temperature Range: Designing for -40°C to +125°C Operation

**Structure overview:**

- **Why temperature matters** — silicon-level effects on carrier mobility, threshold voltage, leakage, and how these propagate up to SPI bus timing (setup/hold times, VOH/VOL shifts, clock drift).
- **Electrical parameters** — a frequency derating table with K_temp correction factors, worst-case timing rules, and noise margin guidance.
- **Hardware design** — component selection table (temperature ratings, capacitor dielectrics), PCB layout rules, series termination, LDO headroom, and thermal cycling resilience.
- **Firmware strategies** — adaptive clock selection, CRC+retry, CS timing enforcement, cold-start sequencing, and continuous thermal monitoring.

**Four C examples:**
1. Temperature-zone lookup table with clock prescaler selection
2. Framed SPI transfer with CRC-8 and configurable retry
3. Cold-start sequencing (oscillator lock wait + peripheral POR poll)
4. Periodic thermal monitor with hysteresis and emergency shutdown

**Three Rust examples:**
1. Type-safe frequency selection via enum + lookup table (with unit tests)
2. Robust framed transfer with CRC-8 and `embedded-hal 1.0` traits
3. Full thermal state machine (`IndustrialSpiManager`) with zone transitions, hysteresis, and over-temperature detection (with unit tests)

- **Testing & validation** — soak, ramp, thermal shock, power-on-at-extremes, and logic-analyser waveform capture protocols.
- **Summary** — a concise checklist of hardware and firmware requirements.


---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Temperature Matters for SPI](#why-temperature-matters)
3. [Key Electrical Parameters vs. Temperature](#key-electrical-parameters)
4. [Hardware Design Considerations](#hardware-design-considerations)
5. [Firmware & Software Strategies](#firmware--software-strategies)
6. [C/C++ Code Examples](#cc-code-examples)
7. [Rust Code Examples](#rust-code-examples)
8. [Testing & Validation](#testing--validation)
9. [Summary](#summary)

---

## Introduction

The **Serial Peripheral Interface (SPI)** is a synchronous, full-duplex communication bus widely used in embedded systems to connect microcontrollers to peripherals such as sensors, ADCs, DACs, EEPROMs, displays, and RF modules. In consumer electronics, SPI devices typically operate in the **0°C to +70°C** commercial temperature range. However, many industrial, automotive, and aerospace applications demand operation across the **extended industrial temperature range of -40°C to +125°C**.

This extreme thermal envelope introduces a host of electrical, mechanical, and software challenges that must be carefully addressed from the ground up. A system that works flawlessly on the bench at room temperature can fail catastrophically when deployed in a cold warehouse, a hot engine bay, or an outdoor cabinet exposed to direct sunlight.

---

## Why Temperature Matters for SPI

### Physical Effects on Silicon

Semiconductor characteristics shift significantly with temperature:

| Parameter | At -40°C | At +25°C | At +125°C |
|-----------|----------|----------|-----------|
| Carrier mobility | Higher (faster switching) | Nominal | Lower (slower switching) |
| Threshold voltage (Vth) | Higher | Nominal | Lower |
| Leakage current | Near-zero | Low | Significantly elevated |
| On-resistance (RDS(on)) | Lower | Nominal | Higher |
| Supply current | Lower | Nominal | Higher |

### Effects on the SPI Bus

These silicon-level changes manifest on the SPI bus as:

- **Increased propagation delay** at high temperatures due to reduced drive strength.
- **Slower rise/fall times** at low temperatures due to increased capacitance and reduced overdrive.
- **Timing margin erosion** — setup/hold time requirements widen at temperature extremes.
- **Noise margin degradation** — VOH/VOL and VIH/VIL thresholds shift, reducing noise immunity.
- **Oscillator frequency drift** — The SPI clock source (typically a crystal or RC oscillator) changes frequency with temperature, which can violate timing specs of attached peripherals.

### Effects on Passive Components

- **PCB traces**: Resistance increases with temperature (~0.4%/°C for copper), increasing series resistance on long SPI lines.
- **Bypass capacitors**: X7R and X5R ceramics are rated for -55°C to +125°C / -55°C to +85°C respectively; using Y5V or Z5U capacitors (rated only 0°C to +85°C) leads to dramatic capacitance loss — up to 80% — outside their range.
- **Pull-up/pull-down resistors**: Value shifts by ±1% to ±5% over the range for typical thick-film resistors, affecting bus termination.
- **Connectors and solder joints**: Thermal cycling causes micro-fractures and contact resistance increases over time.

---

## Key Electrical Parameters vs. Temperature

### Clock Frequency Derating

The maximum safe SPI clock frequency must be **derated** at temperature extremes. A typical microcontroller SPI peripheral may support 40 MHz at 25°C but only 20 MHz at 125°C due to increased propagation delays and setup/hold time requirements of attached devices.

**General derating guideline:**

```
f_max_actual = f_max_nominal × K_temp

Where K_temp:
  -40°C:  0.80 – 0.90  (cold derating, slow rise times)
  +25°C:  1.00          (nominal)
  +85°C:  0.85 – 0.95
  +125°C: 0.60 – 0.80  (hot derating, increased leakage and delays)
```

Always consult the actual datasheet timing diagrams across temperature for both the master and every peripheral device on the bus.

### Setup and Hold Times

The SPI timing parameters most sensitive to temperature are:

- **tSU** (data setup time before clock edge): Increases at high temperature.
- **tHD** (data hold time after clock edge): Increases at low temperature.
- **tCS** (chip select assert-to-clock): Increases at both extremes.
- **tCLKH / tCLKL** (clock high/low pulse width minimums): Increase at extremes.

**Worst-case design rule:** Design timing margins using the *maximum* of all extremes, not the typical 25°C values. Leave at least 20–30% margin on top of the worst-case datasheet values.

### VOH / VOL Shift

At +125°C, the output drive strength of many CMOS devices drops:

```
VOH_min drops toward VDD/2
VOL_max rises toward VDD/2
```

This can cause logic level interpretation failures if the receiving device's VIH or VIL thresholds are also shifting unfavorably. Use **dedicated level-translators** rated across the full industrial range if interfacing devices with different supply voltages at temperature extremes.

---

## Hardware Design Considerations

### 1. Component Selection

**Always specify industrial-grade components.** Verify the exact temperature range in the component datasheet — "industrial" marketing language is not sufficient.

| Component Type | Acceptable Temperature Marking |
|----------------|-------------------------------|
| Microcontrollers | -40°C to +85°C (industrial) or -40°C to +125°C (automotive/mil) |
| SPI Peripherals | Must match or exceed system requirement |
| Crystal oscillators | -40°C to +85°C or wider; TCXO/VCTCXO for tight frequency stability |
| Decoupling capacitors | X7R (±15%, -55°C to +125°C) or C0G/NP0 (±30ppm, -55°C to +125°C) |
| Pull-up resistors | AEC-Q200 rated; 1% metal film or better |
| Connectors | Rated contact resistance across range; Molex MX150, TE AMP series, etc. |

### 2. PCB Layout for Thermal Stability

- **Minimize SPI trace length**: Every millimeter of trace adds parasitic capacitance and resistance that worsen with temperature. Keep SPI traces under 10 cm where possible.
- **Matched trace lengths**: For high-speed SPI (>10 MHz), route CLK, MOSI, MISO, and CS traces with matched lengths to avoid skew-induced timing violations.
- **Ground planes**: Use solid ground planes under SPI traces. They reduce impedance variation with temperature and provide a thermal mass that buffers rapid temperature swings.
- **Thermal relief on SPI connectors**: Use thermal reliefs on pads connected to ground planes to allow proper soldering, but use solid vias for the critical SPI signal paths.
- **Decoupling placement**: Place 100nF X7R decoupling capacitors within 1mm of each VDD pin. Add 10µF bulk capacitors per power domain.

### 3. Series Resistors for Ringing Suppression

At low temperatures, fast-switching SPI signals can cause ringing on the bus due to PCB trace inductance:

```
Recommended series termination: 22Ω – 100Ω in series with MOSI, MISO, and CLK
```

These resistors damp ringing without significantly degrading timing margins at low clock speeds (< 10 MHz). For high-speed SPI, use the minimum value consistent with signal integrity simulation.

### 4. Voltage Regulators

- Use regulators rated across the full temperature range.
- Verify load regulation and line regulation do not degrade unacceptably at extremes.
- LDO dropout voltage increases at low temperature (higher RDS(on)); ensure sufficient headroom.
- Consider a **power-on-reset (POR)** circuit that holds the MCU in reset until supply rails are stable — critical during cold-start transients.

### 5. Thermal Cycling Resilience

Repeated thermal cycling between -40°C and +125°C creates mechanical stress due to differential coefficients of thermal expansion (CTE):

- Use **strain-relief** on PCB-to-cable connections.
- Consider **conformal coating** for boards exposed to condensation during cold-to-warm transitions.
- Avoid mixing ceramic and electrolytic capacitors in parallel for decoupling, as their temperature characteristics differ widely.

---

## Firmware & Software Strategies

### 1. Temperature-Compensated Clock Frequency

The simplest and most robust strategy is to **reduce the SPI clock frequency** at power-up to a value safe across the entire temperature range, then optionally increase it if temperature measurements indicate operation in a benign zone.

```
Conservative single-speed approach:
    f_SPI = min(f_max_peripheral_at_125C, f_max_MCU_at_125C) × 0.75

Dynamic approach:
    f_SPI = f(T) based on lookup table or formula
```

### 2. Bus Error Detection and Recovery

At temperature extremes, bit errors become more likely. Implement:

- **CRC or checksum** on every SPI transaction where possible.
- **Read-back verification** after writes to SPI-attached EEPROMs, DACs, and configuration registers.
- **Retry logic** with exponential backoff on CRC failures.
- **Watchdog timers** to recover from bus hangs caused by glitched clock edges.

### 3. Chip Select Timing

Many SPI peripherals require a minimum CS de-assert time (tCSH) between transactions, and this requirement **increases at high temperatures**. Enforce explicit delays in firmware rather than relying on loop overhead, which is unpredictable.

### 4. Power Sequencing at Cold Start

At -40°C, capacitors charge more slowly and oscillators take longer to stabilize. The firmware must:

- Wait for oscillator lock before initializing SPI.
- Wait for peripheral devices' power-on reset to complete.
- Include a startup delay appropriate to the slowest peripheral's POR time at cold temperature.

### 5. In-Situ Temperature Monitoring

Many MCUs include on-chip temperature sensors. Use these to:

- Adjust SPI clock dividers dynamically.
- Log thermal events for field diagnostics.
- Trigger protective shutdown if temperature exceeds limits.

---

## C/C++ Code Examples

### Example 1: Temperature-Compensated SPI Clock Selection

```c
/**
 * spi_temp_comp.c
 *
 * SPI clock frequency selection based on measured die temperature.
 * Designed for industrial range: -40°C to +125°C.
 *
 * Assumes:
 *   - MCU HAL provides spi_set_baudrate_prescaler()
 *   - Temperature is read from internal sensor via adc_read_temp()
 *   - Base clock: 48 MHz system clock
 *
 * SPI prescalers available: /2, /4, /8, /16, /32, /64, /128
 * Resulting frequencies:  24M, 12M, 6M, 3M, 1.5M, 750k, 375kHz
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal_spi.h"
#include "hal_adc.h"

/* SPI prescaler values (MCU-specific, adjust for your HAL) */
typedef enum {
    SPI_PRESCALER_2   = 0,  /* 24.0 MHz */
    SPI_PRESCALER_4   = 1,  /* 12.0 MHz */
    SPI_PRESCALER_8   = 2,  /*  6.0 MHz */
    SPI_PRESCALER_16  = 3,  /*  3.0 MHz */
    SPI_PRESCALER_32  = 4,  /*  1.5 MHz */
    SPI_PRESCALER_64  = 5,  /*  750 kHz */
    SPI_PRESCALER_128 = 6   /*  375 kHz */
} spi_prescaler_t;

/**
 * Temperature thresholds for SPI clock derating.
 *
 * Design rule: at -40°C, slow rise times require reduced frequency.
 *              at +85°C to +125°C, increased delay requires reduction.
 *              Between -10°C and +70°C is the "sweet spot" for max speed.
 */
typedef struct {
    int16_t          temp_min_c;   /* Lower bound of temperature zone (°C) */
    int16_t          temp_max_c;   /* Upper bound of temperature zone (°C) */
    spi_prescaler_t  prescaler;    /* SPI prescaler to use in this zone     */
    uint32_t         freq_hz;      /* Resulting frequency for logging       */
} spi_temp_zone_t;

static const spi_temp_zone_t spi_temp_table[] = {
    /* Zone          min    max    prescaler            freq       */
    /* Extreme cold */ { -40,  -20,  SPI_PRESCALER_32,  1500000UL },
    /* Cold         */ { -20,  -10,  SPI_PRESCALER_16,  3000000UL },
    /* Cool         */ { -10,  +70,  SPI_PRESCALER_4,  12000000UL },
    /* Warm         */ { +70,  +85,  SPI_PRESCALER_8,   6000000UL },
    /* Hot          */ { +85, +105,  SPI_PRESCALER_16,  3000000UL },
    /* Extreme hot  */ {+105, +130,  SPI_PRESCALER_32,  1500000UL },
};

#define SPI_TEMP_TABLE_LEN  (sizeof(spi_temp_table) / sizeof(spi_temp_table[0]))

/**
 * Select the appropriate SPI prescaler for the current temperature.
 *
 * @param temp_c  Current temperature in degrees Celsius.
 * @return        Prescaler enumeration value.
 */
static spi_prescaler_t spi_select_prescaler(int16_t temp_c)
{
    for (size_t i = 0; i < SPI_TEMP_TABLE_LEN; i++) {
        if (temp_c >= spi_temp_table[i].temp_min_c &&
            temp_c <  spi_temp_table[i].temp_max_c)
        {
            return spi_temp_table[i].prescaler;
        }
    }
    /* Fallback: safest (slowest) setting */
    return SPI_PRESCALER_128;
}

/**
 * Initialise or reconfigure SPI based on current temperature.
 * Call this at startup and periodically (e.g., every 10 seconds) to
 * adapt to environmental changes.
 *
 * @param spi_handle  Opaque handle to the SPI peripheral.
 * @return            true on success, false if temperature read failed.
 */
bool spi_init_temperature_compensated(spi_handle_t *spi_handle)
{
    int16_t temp_c;

    if (!adc_read_temp_celsius(&temp_c)) {
        /* If temperature sensor fails, use safest frequency */
        temp_c = 125;
    }

    spi_prescaler_t prescaler = spi_select_prescaler(temp_c);

    /* Halt ongoing transfers before reconfiguring */
    spi_disable(spi_handle);
    spi_set_baudrate_prescaler(spi_handle, prescaler);
    spi_enable(spi_handle);

    return true;
}
```

---

### Example 2: Robust SPI Transaction with CRC and Retry

```c
/**
 * spi_robust_transfer.c
 *
 * Industrial-grade SPI transfer with:
 *   - Explicit CS timing (respects tCS, tCSH across temperature)
 *   - CRC-8 integrity check (Dallas/Maxim polynomial 0x31)
 *   - Retry logic with configurable attempts
 *   - Timeout protection against bus hangs
 *
 * Compatible with SPI Mode 0 (CPOL=0, CPHA=0).
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "hal_spi.h"
#include "hal_gpio.h"
#include "hal_timer.h"

/* ─── Configuration ─────────────────────────────────────────────────────── */

/** Minimum CS assert-to-clock delay (nanoseconds) — worst case at +125°C.
 *  Typical value from peripheral datasheet: 50–200 ns. We use 500 ns here
 *  as a conservative value including PCB propagation delay. */
#define SPI_CS_SETUP_NS        500U

/** Minimum CS de-assert pulse width between transactions (nanoseconds).
 *  Many SPI devices need at least 50–500 ns; we use 1000 ns for margin. */
#define SPI_CS_HOLD_NS        1000U

/** Maximum number of retries on CRC failure. */
#define SPI_MAX_RETRIES          3U

/** Timeout for a single byte transfer (microseconds). */
#define SPI_BYTE_TIMEOUT_US    100U

/* ─── CRC-8 (polynomial 0x31, used by Sensirion, Dallas, etc.) ──────────── */

static const uint8_t crc8_table[256] = {
    /* Generated for polynomial 0x31, reflected input/output */
    0x00,0x31,0x62,0x53,0xC4,0xF5,0xA6,0x97,
    0xB9,0x88,0xDB,0xEA,0x7D,0x4C,0x1F,0x2E,
    /* ... (full table omitted for brevity; generate with crc8_generate_table()) */
};

/**
 * Compute CRC-8 over a buffer.
 *
 * @param data    Pointer to input data.
 * @param length  Number of bytes.
 * @return        8-bit CRC.
 */
static uint8_t crc8_compute(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

/* ─── Timing helpers ────────────────────────────────────────────────────── */

/**
 * Spin-wait for a small number of nanoseconds.
 * Implementation is platform-specific; on Cortex-M this typically uses
 * DWT cycle counter. Using nop loops is NOT recommended — they are
 * unreliable across compiler optimisation levels.
 */
static inline void delay_ns(uint32_t ns)
{
    hal_timer_delay_ns(ns);   /* Provided by HAL */
}

/* ─── SPI Frame Definition ──────────────────────────────────────────────── */

typedef struct {
    uint8_t  cmd;             /* Command/register address byte  */
    uint8_t  payload[8];      /* Data payload (up to 8 bytes)   */
    uint8_t  payload_len;     /* Actual bytes in payload        */
    uint8_t  crc;             /* CRC appended as last byte      */
} spi_frame_t;

/**
 * Construct a SPI frame with CRC appended.
 */
static void spi_frame_build(spi_frame_t *frame,
                             uint8_t cmd,
                             const uint8_t *data,
                             uint8_t data_len)
{
    uint8_t buf[9];
    frame->cmd = cmd;
    frame->payload_len = (data_len <= 8) ? data_len : 8;
    memcpy(frame->payload, data, frame->payload_len);

    /* CRC covers cmd + payload */
    buf[0] = cmd;
    memcpy(&buf[1], frame->payload, frame->payload_len);
    frame->crc = crc8_compute(buf, 1 + frame->payload_len);
}

/* ─── Core Transfer ─────────────────────────────────────────────────────── */

typedef enum {
    SPI_OK             =  0,
    SPI_ERR_TIMEOUT    = -1,
    SPI_ERR_CRC        = -2,
    SPI_ERR_BUS        = -3,
} spi_status_t;

/**
 * Perform one SPI transaction: assert CS, transfer frame, check response CRC,
 * deassert CS. On failure, retries up to SPI_MAX_RETRIES times.
 *
 * @param spi       SPI peripheral handle.
 * @param cs_pin    GPIO pin for chip select (active low).
 * @param tx_frame  Frame to transmit.
 * @param rx_buf    Buffer for received bytes (must be >= tx_len + 1 for CRC).
 * @param rx_len    Expected number of receive bytes (excluding CRC byte).
 * @return          SPI_OK or error code.
 */
spi_status_t spi_transfer_robust(spi_handle_t  *spi,
                                  gpio_pin_t     cs_pin,
                                  const spi_frame_t *tx_frame,
                                  uint8_t        *rx_buf,
                                  uint8_t         rx_len)
{
    uint8_t tx_buf[10];
    uint8_t rx_raw[10];
    uint8_t tx_len;

    /* Build transmit buffer: [cmd][payload][crc] */
    tx_buf[0]  = tx_frame->cmd;
    memcpy(&tx_buf[1], tx_frame->payload, tx_frame->payload_len);
    tx_buf[1 + tx_frame->payload_len] = tx_frame->crc;
    tx_len = 1 + tx_frame->payload_len + 1;  /* cmd + payload + crc */

    for (uint8_t attempt = 0; attempt <= SPI_MAX_RETRIES; attempt++) {

        /* ── Assert CS with setup delay ─────────────────────────────── */
        gpio_write(cs_pin, GPIO_LOW);
        delay_ns(SPI_CS_SETUP_NS);

        /* ── Transfer bytes with per-byte timeout ───────────────────── */
        spi_status_t status = SPI_OK;
        for (uint8_t i = 0; i < tx_len; i++) {
            if (!spi_transfer_byte(spi, tx_buf[i], &rx_raw[i],
                                   SPI_BYTE_TIMEOUT_US)) {
                status = SPI_ERR_TIMEOUT;
                break;
            }
        }
        /* Read extra response bytes if peripheral sends more than we sent */
        if (status == SPI_OK && rx_len > tx_len) {
            for (uint8_t i = tx_len; i < rx_len + 1; i++) {
                if (!spi_transfer_byte(spi, 0xFF, &rx_raw[i],
                                       SPI_BYTE_TIMEOUT_US)) {
                    status = SPI_ERR_TIMEOUT;
                    break;
                }
            }
        }

        /* ── Deassert CS with hold delay ────────────────────────────── */
        gpio_write(cs_pin, GPIO_HIGH);
        delay_ns(SPI_CS_HOLD_NS);

        if (status != SPI_OK) {
            continue;   /* Retry on timeout */
        }

        /* ── Verify response CRC ────────────────────────────────────── */
        uint8_t expected_crc = crc8_compute(rx_raw, rx_len);
        uint8_t received_crc = rx_raw[rx_len];

        if (expected_crc != received_crc) {
            status = SPI_ERR_CRC;
            continue;   /* Retry on CRC mismatch */
        }

        /* ── Copy valid response to caller buffer ───────────────────── */
        memcpy(rx_buf, rx_raw, rx_len);
        return SPI_OK;
    }

    return SPI_ERR_CRC;   /* All retries exhausted */
}
```

---

### Example 3: Cold-Start Sequencing with Oscillator Lock Wait

```c
/**
 * spi_cold_start.c
 *
 * Robust startup sequence for SPI systems at cold temperatures (-40°C).
 *
 * At -40°C:
 *  - Crystal oscillators can take 10–50 ms to start (vs. 2–5 ms at 25°C).
 *  - Peripheral POR circuits take longer due to slower capacitor charge.
 *  - VDD ramp rate is slower, requiring longer stabilisation wait.
 *
 * This example uses polling with a maximum timeout to avoid infinite hangs.
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal_spi.h"
#include "hal_rcc.h"   /* Reset & Clock Control */
#include "hal_timer.h"
#include "hal_gpio.h"

/* Peripheral-specific POR time at -40°C (consult datasheet) */
#define PERIPH_POR_COLD_MS       50U

/* Maximum wait for oscillator lock (ms) */
#define OSC_LOCK_TIMEOUT_MS     100U

/* SPI device "ready" register address and expected ready value */
#define DEV_REG_STATUS          0x00U
#define DEV_STATUS_READY        0x55U

/**
 * Wait for the system oscillator to lock, with timeout.
 *
 * @return true if oscillator locked, false if timeout expired.
 */
static bool wait_for_oscillator_lock(void)
{
    uint32_t start_ms = hal_timer_get_ms();

    while (!hal_rcc_is_osc_locked()) {
        if ((hal_timer_get_ms() - start_ms) >= OSC_LOCK_TIMEOUT_MS) {
            return false;   /* Oscillator failed to lock */
        }
        hal_timer_delay_ms(1);
    }
    return true;
}

/**
 * Wait for SPI peripheral device to complete its internal power-on reset.
 * Polls a status register; many devices output 0xFF during reset.
 *
 * @param spi     SPI handle.
 * @param cs_pin  Chip select pin.
 * @return        true when device ready, false on timeout.
 */
static bool wait_for_device_ready(spi_handle_t *spi, gpio_pin_t cs_pin)
{
    uint32_t start_ms = hal_timer_get_ms();
    uint8_t  status   = 0x00;

    /* Allow extended POR time for cold startup */
    hal_timer_delay_ms(PERIPH_POR_COLD_MS);

    while (status != DEV_STATUS_READY) {
        /* Read status register */
        gpio_write(cs_pin, GPIO_LOW);
        hal_timer_delay_ns(500);
        spi_transfer_byte(spi, DEV_REG_STATUS, &status, 1000);
        spi_transfer_byte(spi, 0xFF,            &status, 1000);
        gpio_write(cs_pin, GPIO_HIGH);
        hal_timer_delay_ns(1000);

        if ((hal_timer_get_ms() - start_ms) >= 500U) {
            return false;   /* Device not responding */
        }
        hal_timer_delay_ms(5);
    }
    return true;
}

/**
 * Full industrial cold-start initialisation sequence.
 *
 * Call this as the very first step after reset, before any application code.
 *
 * @return true on successful init, false if any step failed.
 */
bool spi_industrial_startup(spi_handle_t *spi, gpio_pin_t cs_pin)
{
    /* Step 1: Wait for oscillator to stabilise */
    if (!wait_for_oscillator_lock()) {
        /* Log error: oscillator did not lock — hardware fault */
        return false;
    }

    /* Step 2: Hold SPI CS deasserted during device POR */
    gpio_write(cs_pin, GPIO_HIGH);

    /* Step 3: Initialise SPI at the safe cold-start frequency */
    spi_init_temperature_compensated(spi);

    /* Step 4: Wait for attached device to complete its POR */
    if (!wait_for_device_ready(spi, cs_pin)) {
        /* Log error: SPI device did not become ready */
        return false;
    }

    /* Step 5: Optionally reconfigure to optimal speed for current temperature */
    spi_init_temperature_compensated(spi);

    return true;
}
```

---

### Example 4: Periodic Temperature Monitoring and Bus Reconfiguration

```c
/**
 * spi_thermal_monitor.c
 *
 * Periodic task (intended for RTOS or superloop) that:
 *  - Reads the on-chip temperature sensor.
 *  - Adjusts SPI clock if temperature zone changes.
 *  - Logs thermal events.
 *  - Triggers protective action if temperature exceeds safe limits.
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal_adc.h"
#include "hal_spi.h"
#include "log.h"

#define TEMP_HYSTERESIS_C       5    /* Prevent rapid zone switching  */
#define TEMP_SHUTDOWN_C       130    /* Emergency shutdown threshold  */
#define TEMP_WARN_HIGH_C      115    /* Warning at high temperature   */
#define TEMP_WARN_LOW_C       -35    /* Warning at low temperature    */

static int16_t last_zone_temp = 25;  /* Track last zone for hysteresis */

/**
 * Determine if a temperature zone change warrants SPI reconfiguration.
 * Applies hysteresis to prevent oscillation near zone boundaries.
 */
static bool should_reconfigure(int16_t current_temp)
{
    int16_t delta = current_temp - last_zone_temp;
    if (delta < 0) delta = -delta;  /* abs() */
    return delta >= TEMP_HYSTERESIS_C;
}

/**
 * Periodic thermal monitoring task.
 * Call every 10 seconds from a timer callback or RTOS task.
 *
 * @param spi  SPI peripheral handle to reconfigure if needed.
 */
void spi_thermal_monitor_tick(spi_handle_t *spi)
{
    int16_t temp_c;

    if (!adc_read_temp_celsius(&temp_c)) {
        LOG_ERROR("Temperature sensor read failed — using safe defaults");
        temp_c = TEMP_WARN_HIGH_C;   /* Assume worst case */
    }

    /* Check for out-of-range conditions */
    if (temp_c >= TEMP_SHUTDOWN_C) {
        LOG_CRITICAL("Temperature %d°C exceeds shutdown limit!", temp_c);
        system_emergency_shutdown();
        return;
    }
    if (temp_c >= TEMP_WARN_HIGH_C) {
        LOG_WARN("High temperature warning: %d°C", temp_c);
    }
    if (temp_c <= TEMP_WARN_LOW_C) {
        LOG_WARN("Low temperature warning: %d°C", temp_c);
    }

    /* Reconfigure SPI clock if temperature has moved to a new zone */
    if (should_reconfigure(temp_c)) {
        LOG_INFO("Reconfiguring SPI for temperature %d°C "
                 "(was %d°C)", temp_c, last_zone_temp);
        spi_init_temperature_compensated(spi);
        last_zone_temp = temp_c;
    }
}
```

---

## Rust Code Examples

### Example 1: Temperature-Compensated SPI Configuration

```rust
//! spi_temp_comp.rs
//!
//! Temperature-based SPI clock selection for industrial range -40°C to +125°C.
//! Uses embedded-hal traits for hardware abstraction.
//!
//! Dependencies (Cargo.toml):
//!   embedded-hal = "1.0"
//!   nb = "1.0"

use embedded_hal::spi::SpiBus;

/// SPI clock frequency options, ordered from slowest to fastest.
/// Values represent the actual frequency in Hz.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum SpiFrequency {
    Hz375k  =  375_000,
    Hz750k  =  750_000,
    Hz1_5M  = 1_500_000,
    Hz3M    = 3_000_000,
    Hz6M    = 6_000_000,
    Hz12M   = 12_000_000,
    Hz24M   = 24_000_000,
}

/// A temperature zone with its corresponding safe SPI frequency.
#[derive(Debug, Clone, Copy)]
struct TempZone {
    /// Inclusive lower bound (°C).
    min_temp: i16,
    /// Exclusive upper bound (°C).
    max_temp: i16,
    /// Maximum safe SPI frequency in this zone.
    frequency: SpiFrequency,
}

/// Temperature-to-frequency lookup table.
/// Designed with conservative derating at both cold and hot extremes.
const TEMP_ZONES: &[TempZone] = &[
    TempZone { min_temp: -40, max_temp: -20, frequency: SpiFrequency::Hz1_5M },
    TempZone { min_temp: -20, max_temp: -10, frequency: SpiFrequency::Hz3M   },
    TempZone { min_temp: -10, max_temp:  70, frequency: SpiFrequency::Hz12M  },
    TempZone { min_temp:  70, max_temp:  85, frequency: SpiFrequency::Hz6M   },
    TempZone { min_temp:  85, max_temp: 105, frequency: SpiFrequency::Hz3M   },
    TempZone { min_temp: 105, max_temp: 130, frequency: SpiFrequency::Hz1_5M },
];

/// Select the appropriate SPI frequency for the given temperature.
///
/// Falls back to the slowest safe frequency if `temp_c` is out of the
/// table range (which should not occur in a correctly calibrated system).
///
/// # Arguments
/// * `temp_c` - Current temperature in degrees Celsius.
///
/// # Returns
/// The [`SpiFrequency`] variant appropriate for `temp_c`.
pub fn select_spi_frequency(temp_c: i16) -> SpiFrequency {
    TEMP_ZONES
        .iter()
        .find(|zone| temp_c >= zone.min_temp && temp_c < zone.max_temp)
        .map(|zone| zone.frequency)
        .unwrap_or(SpiFrequency::Hz375k)   // Safest fallback
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_frequency_selection() {
        assert_eq!(select_spi_frequency(-40), SpiFrequency::Hz1_5M);
        assert_eq!(select_spi_frequency(-15), SpiFrequency::Hz3M);
        assert_eq!(select_spi_frequency(  0), SpiFrequency::Hz12M);
        assert_eq!(select_spi_frequency( 25), SpiFrequency::Hz12M);
        assert_eq!(select_spi_frequency( 75), SpiFrequency::Hz6M);
        assert_eq!(select_spi_frequency( 90), SpiFrequency::Hz3M);
        assert_eq!(select_spi_frequency(110), SpiFrequency::Hz1_5M);
        // Out of range — should return slowest safe frequency
        assert_eq!(select_spi_frequency(200), SpiFrequency::Hz375k);
    }
}
```

---

### Example 2: Robust SPI Transaction with CRC-8 and Retry

```rust
//! spi_robust.rs
//!
//! Industrial-grade SPI transaction manager with:
//!   - CRC-8 integrity checking (polynomial 0x31)
//!   - Configurable retry logic
//!   - Explicit CS timing control
//!   - Comprehensive error types
//!
//! Uses embedded-hal 1.0 traits.

use core::fmt;
use embedded_hal::spi::SpiBus;
use embedded_hal::digital::OutputPin;

// ─── Error Types ─────────────────────────────────────────────────────────────

/// Errors that can occur during a robust SPI transaction.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpiError {
    /// CRC mismatch — data corruption detected.
    CrcMismatch { expected: u8, got: u8 },
    /// All retry attempts exhausted.
    RetriesExhausted { attempts: u8 },
    /// Underlying SPI bus error.
    BusError,
    /// Chip-select GPIO error.
    GpioError,
}

impl fmt::Display for SpiError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SpiError::CrcMismatch { expected, got } =>
                write!(f, "CRC mismatch: expected 0x{:02X}, got 0x{:02X}", expected, got),
            SpiError::RetriesExhausted { attempts } =>
                write!(f, "All {} retry attempts exhausted", attempts),
            SpiError::BusError  => write!(f, "SPI bus error"),
            SpiError::GpioError => write!(f, "CS GPIO error"),
        }
    }
}

// ─── CRC-8 (polynomial 0x31, init 0xFF) ──────────────────────────────────────

/// Compute CRC-8 with polynomial 0x31 and initial value 0xFF.
/// Used by Sensirion sensors, Dallas 1-Wire derivatives, and many others.
pub fn crc8(data: &[u8]) -> u8 {
    const POLY: u8 = 0x31;
    let mut crc: u8 = 0xFF;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

// ─── SPI Frame ────────────────────────────────────────────────────────────────

/// Maximum payload length per frame (adjustable for your protocol).
const MAX_PAYLOAD: usize = 8;

/// A structured SPI frame: [command][payload..][crc].
pub struct SpiFrame {
    pub command:     u8,
    pub payload:     [u8; MAX_PAYLOAD],
    pub payload_len: usize,
}

impl SpiFrame {
    /// Construct a new frame and compute its CRC.
    pub fn new(command: u8, payload: &[u8]) -> Self {
        let len = payload.len().min(MAX_PAYLOAD);
        let mut p = [0u8; MAX_PAYLOAD];
        p[..len].copy_from_slice(&payload[..len]);
        SpiFrame { command, payload: p, payload_len: len }
    }

    /// Serialise the frame into a flat byte buffer including CRC.
    /// Returns the number of bytes written.
    pub fn serialise(&self, buf: &mut [u8]) -> usize {
        assert!(buf.len() >= self.payload_len + 2,
                "Buffer too small for frame serialisation");
        buf[0] = self.command;
        buf[1..=self.payload_len].copy_from_slice(&self.payload[..self.payload_len]);
        let crc_input = &buf[..=self.payload_len];
        let crc = crc8(crc_input);
        buf[self.payload_len + 1] = crc;
        self.payload_len + 2   // command + payload + crc
    }
}

// ─── Robust Transfer ─────────────────────────────────────────────────────────

/// Configuration for the robust SPI transfer driver.
#[derive(Debug, Clone, Copy)]
pub struct RobustSpiConfig {
    /// Number of retry attempts on CRC failure (in addition to the first try).
    pub max_retries: u8,
    /// CS setup time in nanoseconds (assert-to-clock). Use 500 ns minimum.
    pub cs_setup_ns: u32,
    /// CS hold time in nanoseconds (clock-to-deassert + inter-frame minimum).
    pub cs_hold_ns:  u32,
}

impl Default for RobustSpiConfig {
    fn default() -> Self {
        RobustSpiConfig {
            max_retries: 3,
            cs_setup_ns: 500,
            cs_hold_ns:  1000,
        }
    }
}

/// Transfer a framed message over SPI with CRC verification and retry.
///
/// # Type Parameters
/// * `SPI` - A type implementing `SpiBus<u8>`.
/// * `CS`  - A type implementing `OutputPin`.
/// * `DELAY` - A callable `Fn(u32)` that delays for the given nanoseconds.
///
/// # Returns
/// On success, fills `rx_buf` with the response payload (excluding CRC byte)
/// and returns `Ok(bytes_received)`.
pub fn spi_transfer_robust<SPI, CS, DELAY>(
    spi:     &mut SPI,
    cs:      &mut CS,
    delay:   DELAY,
    frame:   &SpiFrame,
    rx_buf:  &mut [u8],
    config:  &RobustSpiConfig,
) -> Result<usize, SpiError>
where
    SPI: SpiBus<u8>,
    CS:  OutputPin,
    DELAY: Fn(u32),
{
    let mut tx_buf = [0u8; MAX_PAYLOAD + 2];
    let tx_len = frame.serialise(&mut tx_buf);
    let rx_with_crc_len = rx_buf.len() + 1;   // Include CRC byte

    let mut rx_raw = [0u8; MAX_PAYLOAD + 2];

    for attempt in 0..=config.max_retries {

        // ── Assert CS ────────────────────────────────────────────────────
        cs.set_low().map_err(|_| SpiError::GpioError)?;
        delay(config.cs_setup_ns);

        // ── Perform full-duplex transfer ─────────────────────────────────
        // Fill TX extension with 0xFF (MOSI idles high for most devices)
        let transfer_len = tx_len.max(rx_with_crc_len);
        for b in tx_buf[tx_len..transfer_len].iter_mut() { *b = 0xFF; }

        let bus_result = spi.transfer(
            &mut rx_raw[..transfer_len],
            &tx_buf[..transfer_len],
        );

        // ── Deassert CS ──────────────────────────────────────────────────
        cs.set_high().map_err(|_| SpiError::GpioError)?;
        delay(config.cs_hold_ns);

        bus_result.map_err(|_| SpiError::BusError)?;

        // ── Verify CRC ───────────────────────────────────────────────────
        let resp_data = &rx_raw[..rx_buf.len()];
        let expected_crc = crc8(resp_data);
        let received_crc = rx_raw[rx_buf.len()];

        if expected_crc == received_crc {
            rx_buf.copy_from_slice(resp_data);
            return Ok(rx_buf.len());
        }

        // Log the mismatch (in a real system, use defmt or log crate)
        let _ = (expected_crc, received_crc, attempt);
    }

    Err(SpiError::RetriesExhausted { attempts: config.max_retries + 1 })
}

#[cfg(test)]
mod crc_tests {
    use super::*;

    #[test]
    fn crc8_known_value() {
        // Sensirion SHT4x CRC test vector: data [0xBE, 0xEF] → CRC 0x92
        assert_eq!(crc8(&[0xBE, 0xEF]), 0x92);
    }

    #[test]
    fn frame_serialise_length() {
        let frame = SpiFrame::new(0xAB, &[0x01, 0x02, 0x03]);
        let mut buf = [0u8; 5];
        let len = frame.serialise(&mut buf);
        // cmd(1) + payload(3) + crc(1) = 5
        assert_eq!(len, 5);
        assert_eq!(buf[0], 0xAB);
        assert_eq!(buf[1], 0x01);
        assert_eq!(buf[4], crc8(&[0xAB, 0x01, 0x02, 0x03]));
    }
}
```

---

### Example 3: Industrial SPI Manager with Thermal State Machine

```rust
//! spi_industrial_manager.rs
//!
//! A complete industrial SPI manager that integrates:
//!   - Startup sequencing with cold-temperature delays
//!   - Thermal state machine for dynamic reconfiguration
//!   - Hysteresis on zone transitions
//!   - Emergency shutdown on over-temperature

use core::fmt;

/// Thermal operating zones for the SPI system.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThermalZone {
    ExtremeCold,   // -40°C to -20°C
    Cold,          // -20°C to -10°C
    Normal,        // -10°C to +70°C
    Warm,          //  70°C to  85°C
    Hot,           //  85°C to 105°C
    ExtremeHot,    // 105°C to 125°C
    OverTemp,      // > 125°C — fault condition
}

impl ThermalZone {
    /// Classify a temperature measurement into a zone.
    pub fn from_temp(temp_c: i16) -> Self {
        match temp_c {
            i16::MIN..=-21 => ThermalZone::ExtremeCold,
            -20..=-11      => ThermalZone::Cold,
            -10..=69       => ThermalZone::Normal,
            70..=84        => ThermalZone::Warm,
            85..=104       => ThermalZone::Hot,
            105..=124      => ThermalZone::ExtremeHot,
            _              => ThermalZone::OverTemp,
        }
    }

    /// Maximum recommended SPI frequency (Hz) for this zone.
    pub fn max_spi_freq_hz(&self) -> u32 {
        match self {
            ThermalZone::ExtremeCold => 1_500_000,
            ThermalZone::Cold        => 3_000_000,
            ThermalZone::Normal      => 12_000_000,
            ThermalZone::Warm        => 6_000_000,
            ThermalZone::Hot         => 3_000_000,
            ThermalZone::ExtremeHot  => 1_500_000,
            ThermalZone::OverTemp    => 0,            // No operation
        }
    }

    /// Minimum CS hold time (ns) required in this zone.
    pub fn cs_hold_ns(&self) -> u32 {
        match self {
            ThermalZone::ExtremeCold => 2_000,
            ThermalZone::Cold        => 1_500,
            ThermalZone::Normal      => 1_000,
            ThermalZone::Warm        => 1_200,
            ThermalZone::Hot         => 1_500,
            ThermalZone::ExtremeHot  => 2_000,
            ThermalZone::OverTemp    => 0,
        }
    }
}

impl fmt::Display for ThermalZone {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

/// State of the industrial SPI manager.
pub struct IndustrialSpiManager {
    /// Current thermal zone.
    current_zone: ThermalZone,
    /// Last measured temperature (°C).
    last_temp_c:  i16,
    /// Hysteresis: minimum temperature change before reconfiguring.
    hysteresis_c: i16,
    /// Total number of zone transitions observed (for diagnostics).
    zone_changes: u32,
    /// Whether the system is in emergency shutdown.
    emergency_stop: bool,
}

/// Result of a thermal monitoring tick.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThermalEvent {
    /// No change, continue normal operation.
    NoChange,
    /// Temperature zone changed; SPI should be reconfigured.
    ZoneChanged { new_zone: ThermalZone },
    /// Temperature exceeds safe limit; shutdown required.
    OverTemperature,
}

impl IndustrialSpiManager {
    /// Create a new manager, starting in the Normal zone.
    pub fn new() -> Self {
        IndustrialSpiManager {
            current_zone:   ThermalZone::Normal,
            last_temp_c:    25,
            hysteresis_c:   5,
            zone_changes:   0,
            emergency_stop: false,
        }
    }

    /// Process a new temperature measurement.
    ///
    /// Returns a [`ThermalEvent`] indicating whether firmware must act.
    pub fn update_temperature(&mut self, temp_c: i16) -> ThermalEvent {
        let new_zone = ThermalZone::from_temp(temp_c);

        if new_zone == ThermalZone::OverTemp {
            self.emergency_stop = true;
            return ThermalEvent::OverTemperature;
        }

        // Apply hysteresis: only reconfigure if temperature moved enough
        let delta = (temp_c - self.last_temp_c).unsigned_abs() as i16;
        if delta < self.hysteresis_c && new_zone == self.current_zone {
            return ThermalEvent::NoChange;
        }

        self.last_temp_c = temp_c;

        if new_zone != self.current_zone {
            self.zone_changes += 1;
            self.current_zone = new_zone;
            return ThermalEvent::ZoneChanged { new_zone };
        }

        ThermalEvent::NoChange
    }

    /// Return the current SPI configuration parameters.
    pub fn spi_config(&self) -> (u32, u32) {
        (
            self.current_zone.max_spi_freq_hz(),
            self.current_zone.cs_hold_ns(),
        )
    }

    /// Is the system in emergency shutdown?
    pub fn is_emergency_stop(&self) -> bool {
        self.emergency_stop
    }

    /// Total zone transitions observed since startup.
    pub fn zone_change_count(&self) -> u32 {
        self.zone_changes
    }
}

#[cfg(test)]
mod manager_tests {
    use super::*;

    #[test]
    fn test_zone_transitions() {
        let mut mgr = IndustrialSpiManager::new();

        // Cold start — jump to extreme cold
        let event = mgr.update_temperature(-40);
        assert_eq!(event, ThermalEvent::ZoneChanged {
            new_zone: ThermalZone::ExtremeCold
        });
        assert_eq!(mgr.zone_change_count(), 1);

        // Tiny change within zone and within hysteresis — no event
        let event = mgr.update_temperature(-38);
        assert_eq!(event, ThermalEvent::NoChange);

        // Warm up to normal
        let event = mgr.update_temperature(25);
        assert!(matches!(event, ThermalEvent::ZoneChanged { .. }));

        // Over-temperature fault
        let event = mgr.update_temperature(130);
        assert_eq!(event, ThermalEvent::OverTemperature);
        assert!(mgr.is_emergency_stop());
    }

    #[test]
    fn test_freq_derating() {
        assert!(ThermalZone::Normal.max_spi_freq_hz() >
                ThermalZone::Hot.max_spi_freq_hz());
        assert!(ThermalZone::Normal.max_spi_freq_hz() >
                ThermalZone::ExtremeCold.max_spi_freq_hz());
        assert_eq!(ThermalZone::OverTemp.max_spi_freq_hz(), 0);
    }
}
```

---

## Testing & Validation

### Bench Testing Protocol

A rigorous thermal validation campaign should include all of the following test categories:

**1. Soak Tests**

Place the fully assembled board in a temperature chamber and soak at each extreme (-40°C and +125°C) for at least 30 minutes before running the SPI test suite. Devices are not at thermal equilibrium immediately upon reaching the setpoint.

**2. Ramp Tests**

Run the SPI bus continuously while ramping the chamber from -40°C to +125°C at 2°C/min. Log all errors, CRC failures, and timing violations. This exercises the full temperature gradient, including the crossover points where simultaneous worst-case combinations (e.g., cold MCU, hot peripheral) may briefly exist.

**3. Thermal Shock Tests**

Transfer the board rapidly (< 30 seconds) from a -40°C environment to a +125°C environment. Repeat 100+ cycles per IEC 60068-2-14. This validates solder joint integrity and connector reliability.

**4. Power-On At Extremes**

Apply power to the system at -40°C and verify the startup sequence completes correctly. Repeat at +125°C. Pay special attention to oscillator start-up time and peripheral POR behaviour.

**5. Timing Analysis with Logic Analyser**

Use a logic analyser with digital/analog mixed-signal capability (e.g., Saleae Logic Pro 16) to capture SPI waveforms at both temperature extremes. Measure:

- Clock period and duty cycle.
- MOSI/MISO setup and hold times relative to clock edges.
- CS timing (tCS, tCSH).
- Rise and fall times of all SPI signals.

Compare all measured values against the worst-case datasheet specifications.

### Code-Level Testing

The Rust examples above include `#[cfg(test)]` modules with unit tests that can be run with `cargo test`. For the C code, use Unity or CppUTest with mock HAL implementations to test CRC computation, retry logic, and temperature zone selection without hardware.

---

## Summary

Designing SPI systems for the industrial temperature range of **-40°C to +125°C** is far more than a component-selection exercise. It requires an integrated engineering approach spanning hardware and firmware:

**Hardware:**
- Select components explicitly rated for the full temperature range; never rely on marketing language.
- Use X7R or C0G/NP0 ceramic capacitors for decoupling; avoid Y5V/Z5U which lose most of their capacitance outside 0°C to +85°C.
- Keep SPI traces short, impedance-controlled, and length-matched. Use solid ground planes.
- Add series termination resistors (22–100 Ω) to dampen ringing at cold temperatures.
- Include a hardware power-on-reset circuit to hold the MCU in reset during slow cold-start VDD ramps.

**Firmware:**
- **Derate the SPI clock frequency** at both temperature extremes. Use a lookup table keyed on measured temperature to select the correct clock divider, with hysteresis to prevent rapid switching.
- **Enforce explicit timing delays** for CS setup/hold. Do not rely on loop overhead.
- **Add CRC integrity checking** and retry logic to every SPI transaction. Bit errors increase at temperature extremes.
- **Implement a cold-start sequence** that waits for oscillator lock and peripheral POR completion before initiating any SPI communication.
- **Monitor temperature continuously** and adapt the SPI configuration dynamically. Include over-temperature shutdown logic.

**Validation:**
- Perform soak, ramp, and thermal shock tests across the full -40°C to +125°C range.
- Capture and analyse SPI waveforms at both extremes to verify all timing margins.
- Run the SPI test suite at temperature, not just on the bench at room temperature.

The combination of careful component selection, conservative timing design, and adaptive firmware produces SPI systems that operate reliably across the full industrial temperature range throughout their product lifetime.

---

*Document: 92_Industrial_Temperature_Range.md | SPI Series | Revision 1.0*