# 55. EMC Considerations for CAN Bus

> **Electromagnetic Compatibility (EMC) design practices for CAN physical layer to meet automotive EMC standards.**

---

## Table of Contents

1. [Introduction](#introduction)
2. [EMC Fundamentals for CAN](#emc-fundamentals-for-can)
3. [Automotive EMC Standards](#automotive-emc-standards)
4. [Physical Layer EMC Architecture](#physical-layer-emc-architecture)
5. [Common Mode Chokes and Filtering](#common-mode-chokes-and-filtering)
6. [Termination and Shielding](#termination-and-shielding)
7. [PCB Layout Guidelines](#pcb-layout-guidelines)
8. [Software / Register Configuration for EMC](#software--register-configuration-for-emc)
9. [C/C++ Implementation Examples](#cc-implementation-examples)
10. [Rust Implementation Examples](#rust-implementation-examples)
11. [EMC Testing and Diagnostics](#emc-testing-and-diagnostics)
12. [Summary](#summary)

---

## Introduction

Electromagnetic Compatibility (EMC) is a critical discipline in automotive electronics, governing how electronic systems behave in the presence of electromagnetic interference (EMI) and how much interference they themselves emit. For Controller Area Network (CAN) buses — which operate at up to 10 Mbit/s (CAN FD) in electrically hostile automotive environments — meeting EMC requirements is mandatory for homologation (type approval) and reliable operation.

CAN's differential signaling (CAN_H and CAN_L) provides inherent common-mode noise rejection, but without proper design care the bus can become a significant EMI radiator or a victim of radiated and conducted interference. This chapter covers the complete design stack: from physical layer component selection through PCB layout, software configuration, and compliance testing.

---

## EMC Fundamentals for CAN

### Differential vs. Common Mode

CAN uses a balanced differential pair:

- **Differential mode signal**: The intentional data signal, `V_diff = V_CANH - V_CANL`
- **Common mode voltage**: `V_cm = (V_CANH + V_CANL) / 2`, ideally constant at ~2.5 V

EMI problems arise primarily from **common-mode currents** — currents that flow in the same direction on both conductors and return through parasitic paths (chassis, ground planes, cable shields). These create magnetic fields proportional to the loop area enclosed by the current path.

### EMI Emission Mechanisms

```
┌─────────────────────────────────────────────────────────────────┐
│  CAN Transceiver                                                │
│                                                                 │
│  TX ──► [Driver] ──► CAN_H ──────────────────────────────────►  │
│                          └── Differential pair (twisted pair)   │
│  RX ◄── [Receiver] ◄── CAN_L ────────────────────────────────►  │
│                                                                 │
│  Common mode noise path:                                        │
│  GND ──────────────────────────────► Chassis ──► Antenna effect │
└─────────────────────────────────────────────────────────────────┘
```

Key emission sources:
- **Slew rate of the driver**: Faster edges → wider frequency content → more radiated EMI
- **Cable acting as antenna**: Unbalanced currents radiate from the wire harness
- **Ground bounce**: Switching currents through parasitic inductance of ground connections
- **Stub reflections**: Unterminated stubs resonate at λ/4 frequencies

### Susceptibility Mechanisms

CAN buses can be disrupted by:
- **Conducted interference**: Noise injected onto power supply lines (ISO 7637)
- **Radiated fields**: High-power RF transmitters (mobile phones, ignition systems)
- **ESD**: Electrostatic discharge at connector mating events
- **Load dump transients**: Up to +87 V transients when the alternator load is suddenly disconnected

---

## Automotive EMC Standards

### Key Standards

| Standard | Scope | Typical Limits |
|---|---|---|
| CISPR 25 | Vehicle radiated/conducted emissions | Class 5: 30–1000 MHz |
| ISO 11452-2 | Radiated immunity (absorber lined chamber) | 1–2000 MHz, up to 200 V/m |
| ISO 11452-4 | BCI (Bulk Current Injection) immunity | 1–400 MHz |
| ISO 7637-2 | Conducted transient immunity on 12 V supply | Pulse 1–5b |
| ISO 10605 | ESD immunity | ±8 kV contact, ±15 kV air |
| LV 124 / LV 148 | OEM-specific (VW Group) combined EMC | Superset of above |
| GMW3097 | GM worldwide EMC standard | Similar scope |

### OEM-Specific Requirements

Most OEMs mandate requirements beyond the base ISO/CISPR standards. Common additions:
- **Extended frequency range**: Up to 6 GHz for CISPR 25 Class 5
- **Tighter emission limits**: −6 dB relative to CISPR 25 nominal
- **Functional safety during immunity testing**: No DTC set, no bus-off events

---

## Physical Layer EMC Architecture

### CAN Physical Layer Block Diagram

```
  ECU                              Harness Connector
  ┌─────────────────────────────────────────────────────┐
  │                                                     │
  │  MCU        CAN Controller    CAN Transceiver       │
  │  ┌────┐     ┌──────────┐     ┌──────────────┐       │
  │  │    │TX──►│          │TX──►│              │       │
  │  │    │     │ ISO 11898│     │ ISO 11898-2  │CAN_H──┼──── CAN_H
  │  │    │     │   -1     │     │              │       │
  │  │    │◄─RX │          │◄─RX │              │CAN_L──┼──── CAN_L
  │  └────┘     └──────────┘     └──────┬───────┘       │
  │                                     │               │
  │             [Bypass caps]    [EMC filter network]   │
  │                                     │               │
  │                              ════GND══              │
  └─────────────────────────────────────────────────────┘
```

### EMC Filter Network — Component Selection

A robust EMC filter for a CAN bus node typically includes:

**1. Common-Mode Choke (CMC)**
- Placed in series with both CAN_H and CAN_L
- Typically 100–600 µH inductance
- Must handle CAN data rates without distorting differential signal
- Example: Würth 7427924 (100 µH, 100 Mbit/s rated)

**2. ESD Protection**
- Bidirectional TVS diodes on CAN_H and CAN_L to GND
- Clamping voltage must be within transceiver absolute maximum ratings
- Examples: PRTR5V0U2X, PESD2CAN

**3. Split Termination with Bypass Capacitor**
- 60 Ω split into two 120 Ω resistors with a capacitor (4.7–100 nF) to ground from the midpoint
- Provides common-mode filtering while maintaining 60 Ω differential termination
- Capacitor forms a low-pass filter for common-mode noise

**4. Series Resistors**
- Small resistors (0–33 Ω) in series on CAN_H and CAN_L
- Damp high-frequency ringing without significantly attenuating the signal
- Values up to 33 Ω acceptable for CAN FD up to 5 Mbit/s data phase

### Filter Network Schematic

```
                    CMC
   CAN_H_int ──[L1a]──┬──[R1]── CAN_H_ext ──► To connector
                      │
                    [C1] (bypass)
                      │
                     GND
                      │
                    [C2] (bypass)
                      │
   CAN_L_int ──[L1b]──┴──[R2]── CAN_L_ext ──► To connector
                                    │             │
                                [TVS_H]         [Rt/2]
                                [TVS_L]           │
                                    │          [Cterm]──GND
                                   GND            │
                                               [Rt/2]
                                                  │
                                                 GND

   L1a + L1b = common-mode choke (coupled inductors)
   R1, R2     = series resistors (10–33 Ω)
   C1, C2     = bypass caps (100 pF–10 nF)
   TVS_H/L    = ESD/transient protection
   Rt         = termination (2 × 60 Ω split = 120 Ω each)
   Cterm      = split termination bypass cap (4.7–47 nF)
```

---

## Common Mode Chokes and Filtering

### How the Common Mode Choke Works

A common-mode choke consists of two windings on a common ferrite core wound in opposition for differential signals:

- **Differential signal**: Magnetic fields cancel → low impedance → signal passes through
- **Common-mode noise**: Magnetic fields add → high impedance → noise is blocked

The key parameter is the **common-mode impedance** at the frequencies of concern (typically 30 MHz–1 GHz for CISPR 25).

### Selecting the Right Choke

| Parameter | Typical Requirement | Notes |
|---|---|---|
| Common-mode impedance | >100 Ω at 30 MHz, >500 Ω at 100 MHz | Higher is better for EMI |
| Differential mode impedance | <1 Ω at signal frequencies | Must not attenuate CAN signal |
| Current rating | ≥ 150 mA | CAN bus worst case |
| Rated voltage | ≥ 60 V | For automotive transients |
| Temperature range | −40 °C to +125 °C | Automotive grade |

### Bypass Capacitor Placement

Local decoupling of the transceiver Vcc pin is critical:
- 100 nF ceramic (X7R) as close as possible to Vcc pin
- Additional 4.7 µF bulk cap within 5 mm
- Both to a solid local ground plane, not just a trace

---

## Termination and Shielding

### Termination Strategy

**Classic (single-ended):** A single 120 Ω resistor at each end of the bus. Simple but provides no common-mode filtering.

**Split termination (recommended for EMC):**
```
CAN_H ─── 60 Ω ───┐
                  ├─── Cterm (4.7–100 nF) ─── GND
CAN_L ─── 60 Ω ───┘
```

The capacitor to ground provides a low-impedance path for common-mode noise to return to the local ground, preventing it from radiating.

**Capacitive network termination:** For star topologies where the bus length is short, a combination of series resistance and shunt capacitance can be used.

### Cable Shielding

For high-EMC-requirement harnesses:
- Use **shielded twisted pair (STP)** cable
- Shield must be connected to chassis ground at **one end only** (or through a capacitor at both ends) to avoid creating a ground loop
- Recommended: Direct chassis bond at ECU end, 10 nF ceramic to chassis at connector end

---

## PCB Layout Guidelines

Good PCB layout is the most cost-effective EMC mitigation. Key rules:

### Layer Stack-Up

```
Layer 1: Signal (CAN traces, short runs)
Layer 2: Ground plane (continuous, no splits under CAN area)
Layer 3: Power plane
Layer 4: Signal (MCU traces)
```

A continuous ground plane beneath CAN traces minimizes the return current loop area.

### Routing Rules

1. **Keep CAN_H and CAN_L parallel and adjacent** — maintains differential balance and reduces loop area
2. **Minimize trace length** between transceiver and connector — each millimeter is a potential antenna
3. **No vias in the CAN signal path** if avoidable — vias add inductance and create impedance discontinuities
4. **Route CAN traces over a solid ground plane** — never over splits or gaps
5. **Place the EMC filter network as close to the connector as possible**, not near the transceiver
6. **Star topology forbidden** — use a true bus topology with maximum 0.3 m stubs
7. **Separate CAN GND from digital/analog GND** at a single star point near the connector

### Recommended Trace Impedance

For CAN at 1 Mbit/s and below: Trace impedance is not critical. For CAN FD at 5–8 Mbit/s:
- Target **120 Ω differential impedance** (60 Ω each trace to ground plane)
- Achievable with ~0.2 mm traces on a 4-layer board with 0.2 mm dielectric

---

## Software / Register Configuration for EMC

While hardware design dominates EMC performance, software configuration of the CAN controller and transceiver significantly impacts emissions, particularly **slew rate control**.

### Slew Rate Control

Most modern CAN transceivers provide slew rate control via:
- A hardware pin (RS pin on TJA1042, for example)
- An SPI/I²C register interface (TJA1145, TCAN1145)
- An external resistor to set slew rate

Lower slew rates reduce high-frequency spectral content and thus radiated emissions, at the cost of reduced maximum data rate. For EMC compliance:

- **Classic CAN (≤1 Mbit/s)**: Use slowest compatible slew rate
- **CAN FD data phase (2–5 Mbit/s)**: Use intermediate slew rate
- **CAN FD data phase (>5 Mbit/s)**: Fast slew rate necessary, rely on hardware filtering

### Transceiver Mode Management

Automotive transceivers support multiple operating modes relevant to EMC:

| Mode | Description | EMC Impact |
|---|---|---|
| Normal | Full operation | Nominal emissions |
| Standby | Low power, wake on CAN | Reduced emissions |
| Sleep | Minimum power | Near-zero emissions |
| Listen-only | RX only, no TX | No TX emissions |

Keeping unused nodes in standby/sleep reduces total system EMI.

---

## C/C++ Implementation Examples

### Example 1: Slew Rate Configuration via SPI (TJA1145 Transceiver)

```c
/**
 * @file can_emc_config.c
 * @brief CAN transceiver EMC configuration for TJA1145 via SPI
 *
 * The TJA1145 is an automotive SBC (System Basis Chip) with integrated
 * CAN transceiver. EMC-relevant settings are accessed via SPI registers.
 */

#include <stdint.h>
#include <stdbool.h>
#include "spi_driver.h"   /* Platform SPI abstraction */
#include "can_emc_config.h"

/* ─── TJA1145 Register Map (EMC-relevant) ─────────────────────── */
#define TJA1145_REG_MODE_CTRL       0x01  /**< Mode control register       */
#define TJA1145_REG_CAN_CTRL        0x20  /**< CAN control register        */
#define TJA1145_REG_TRANSCEIVER     0x22  /**< Transceiver status register */
#define TJA1145_REG_WAKEUP_ENABLE   0x4B  /**< CAN wakeup enable           */

/* ─── Mode Control Bit Fields ────────────────────────────────────── */
#define MODE_CTRL_SLEEP             0x01  /**< Sleep mode                  */
#define MODE_CTRL_STANDBY           0x04  /**< Standby mode                */
#define MODE_CTRL_NORMAL            0x07  /**< Normal (active) mode        */

/* ─── CAN Control Bit Fields ─────────────────────────────────────── */
#define CAN_CTRL_PNCOK              (1 << 5) /**< Partial networking configured */
#define CAN_CTRL_CPNC               (1 << 4) /**< CAN partial network active    */
#define CAN_CTRL_CMC_MASK           (3 << 2) /**< CAN mode config mask          */
#define CAN_CTRL_CMC_NORMAL         (0 << 2) /**< Normal CAN mode               */
#define CAN_CTRL_CMC_LISTEN         (1 << 2) /**< Listen-only mode              */
#define CAN_CTRL_CFDCx_MASK         (3 << 0) /**< CAN FD config                 */

/* ─── Slew Rate Constants ────────────────────────────────────────── */
typedef enum {
    CAN_SLEW_FAST    = 0x00,  /**< Max slew, min emissions filtering  */
    CAN_SLEW_NORMAL  = 0x01,  /**< Nominal; balanced speed vs EMC     */
    CAN_SLEW_SLOW    = 0x03,  /**< Max filtering; CAN classic ≤500k   */
} can_slew_rate_t;

/* ─── Internal SPI Frame Helper ──────────────────────────────────── */
/**
 * @brief Write a register over SPI to TJA1145
 * @param reg     8-bit register address
 * @param value   8-bit data to write
 * @return true on success
 *
 * TJA1145 SPI frame: [A6..A1 | A0=0 | 0] [D7..D0]
 * A0=0 means WRITE, A0=1 means READ.
 */
static bool tja1145_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t frame[2];
    frame[0] = (reg << 1) & 0xFE;  /* Address, write bit=0 */
    frame[1] = value;
    return spi_transfer(TJA1145_SPI_CHANNEL, frame, NULL, 2);
}

static bool tja1145_read_reg(uint8_t reg, uint8_t *value)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) | 0x01), 0x00 };
    uint8_t rx[2];
    if (!spi_transfer(TJA1145_SPI_CHANNEL, tx, rx, 2)) return false;
    *value = rx[1];
    return true;
}

/* ─── EMC Configuration API ──────────────────────────────────────── */

/**
 * @brief Configure CAN transceiver for optimal EMC performance
 *
 * Sets slew rate, operating mode, and listen-only fallback.
 * Must be called after SPI and power-on initialization.
 *
 * @param slew    Desired slew rate (use CAN_SLEW_SLOW for ≤500kbit/s)
 * @return true if all register writes succeeded
 */
bool can_emc_init(can_slew_rate_t slew)
{
    uint8_t can_ctrl_val;

    /* Step 1: Transition to standby mode before reconfiguring */
    if (!tja1145_write_reg(TJA1145_REG_MODE_CTRL, MODE_CTRL_STANDBY)) {
        return false;
    }

    /* Step 2: Read current CAN control register */
    if (!tja1145_read_reg(TJA1145_REG_CAN_CTRL, &can_ctrl_val)) {
        return false;
    }

    /* Step 3: Apply slew rate setting (bits [1:0] in TJA1145 CAN control) */
    can_ctrl_val &= ~CAN_CTRL_CFDCx_MASK;
    can_ctrl_val |= (uint8_t)(slew & 0x03);

    /* Step 4: Set normal CAN mode (not listen-only) */
    can_ctrl_val &= ~CAN_CTRL_CMC_MASK;
    can_ctrl_val |= CAN_CTRL_CMC_NORMAL;

    if (!tja1145_write_reg(TJA1145_REG_CAN_CTRL, can_ctrl_val)) {
        return false;
    }

    /* Step 5: Return to normal operating mode */
    return tja1145_write_reg(TJA1145_REG_MODE_CTRL, MODE_CTRL_NORMAL);
}

/**
 * @brief Set transceiver to low-emission standby mode
 *
 * Call when the CAN bus is not expected to be active for >100 ms.
 * The transceiver will still wake on CAN activity if configured.
 */
bool can_emc_enter_standby(void)
{
    return tja1145_write_reg(TJA1145_REG_MODE_CTRL, MODE_CTRL_STANDBY);
}

/**
 * @brief Resume normal operation from standby
 */
bool can_emc_exit_standby(void)
{
    return tja1145_write_reg(TJA1145_REG_MODE_CTRL, MODE_CTRL_NORMAL);
}
```

---

### Example 2: Split-Termination and Filter Network Validation (C++)

```cpp
/**
 * @file can_emc_diagnostics.cpp
 * @brief Runtime diagnostics to verify EMC-related CAN physical layer health
 *
 * Monitors common-mode voltage, differential voltage levels, and error
 * counters to detect degraded EMC component performance (e.g., failed
 * CMC, open termination resistor, damaged TVS diode).
 */

#include <cstdint>
#include <array>
#include <optional>
#include "adc_driver.hpp"
#include "can_controller.hpp"
#include "logger.hpp"

namespace can_emc {

/* ─── Physical Layer Voltage Thresholds ──────────────────────────── */
constexpr float VCANH_RECESSIVE_MIN =  2.0f;  /* V — ISO 11898-2 */
constexpr float VCANH_RECESSIVE_MAX =  3.0f;
constexpr float VCANL_RECESSIVE_MIN =  2.0f;
constexpr float VCANL_RECESSIVE_MAX =  3.0f;
constexpr float VDIFF_DOMINANT_MIN  =  1.5f;  /* CAN_H - CAN_L dominant */
constexpr float VCM_NOMINAL         =  2.5f;  /* V — ideal common mode  */
constexpr float VCM_TOLERANCE       =  0.5f;  /* ±V acceptable deviation */

/* ─── CAN Error Counter Thresholds ──────────────────────────────── */
constexpr uint8_t TX_ERR_WARN_THRESHOLD = 96;
constexpr uint8_t RX_ERR_WARN_THRESHOLD = 96;
constexpr uint8_t BUS_OFF_THRESHOLD     = 255;

/**
 * @brief Snapshot of CAN physical layer measurements
 */
struct PhysicalLayerStatus {
    float    v_canh;          /**< CAN_H voltage (recessive state) */
    float    v_canl;          /**< CAN_L voltage (recessive state) */
    float    v_common_mode;   /**< (CAN_H + CAN_L) / 2            */
    float    v_diff_dominant; /**< CAN_H - CAN_L in dominant state */
    uint8_t  tx_error_count;  /**< Transmit error counter          */
    uint8_t  rx_error_count;  /**< Receive error counter           */
    bool     bus_off;         /**< Bus-off state flag              */
    bool     termination_ok;  /**< Termination detected            */
    bool     cm_voltage_ok;   /**< Common mode within spec         */
};

/**
 * @brief Measure and validate CAN physical layer parameters
 *
 * Reads ADC channels connected to CAN_H and CAN_L via high-impedance
 * voltage dividers, and polls the CAN controller error counters.
 *
 * @param adc    Reference to ADC driver
 * @param ctrl   Reference to CAN controller
 * @return Populated PhysicalLayerStatus structure
 */
PhysicalLayerStatus measure_physical_layer(AdcDriver& adc, CanController& ctrl)
{
    PhysicalLayerStatus status{};

    /* Read bus voltages during recessive phase using ADC */
    auto v_canh_raw = adc.read_channel(ADC_CHANNEL_CAN_H);
    auto v_canl_raw = adc.read_channel(ADC_CHANNEL_CAN_L);

    if (v_canh_raw && v_canl_raw) {
        status.v_canh = adc.to_voltage(*v_canh_raw);
        status.v_canl = adc.to_voltage(*v_canl_raw);
        status.v_common_mode = (status.v_canh + status.v_canl) / 2.0f;

        /* Check common-mode voltage for EMC filter health */
        float cm_deviation = status.v_common_mode - VCM_NOMINAL;
        status.cm_voltage_ok =
            (cm_deviation > -VCM_TOLERANCE) && (cm_deviation < VCM_TOLERANCE);

        if (!status.cm_voltage_ok) {
            Logger::warn("CAN common-mode voltage out of spec: %.2f V "
                         "(expected %.1f ± %.1f V)",
                         status.v_common_mode, VCM_NOMINAL, VCM_TOLERANCE);
            Logger::warn("Possible cause: split termination cap open/failed, "
                         "or CMC saturation");
        }
    }

    /* Read error counters from CAN controller */
    auto error_state = ctrl.get_error_state();
    status.tx_error_count = error_state.tec;
    status.rx_error_count = error_state.rec;
    status.bus_off        = error_state.bus_off;

    /*
     * Elevated error counters without bus-off can indicate:
     * - EMI susceptibility (high REC)
     * - Transmission distortion from reflections (high TEC)
     * - Failed termination (both elevated)
     */
    if (status.tx_error_count > TX_ERR_WARN_THRESHOLD) {
        Logger::warn("CAN TEC=%u — possible termination or EMI issue",
                     status.tx_error_count);
    }
    if (status.rx_error_count > RX_ERR_WARN_THRESHOLD) {
        Logger::warn("CAN REC=%u — possible EMI susceptibility or "
                     "damaged TVS diode", status.rx_error_count);
    }

    return status;
}

/**
 * @brief Perform a bus impedance check to verify termination
 *
 * With bus inactive and driver in recessive state, measures
 * the DC bus impedance. Correct termination = ~60 Ω total.
 *
 * This test must only be run when the bus is known idle.
 *
 * @param ctrl  CAN controller reference
 * @param adc   ADC driver reference
 * @return true if termination appears healthy (~60 Ω)
 */
bool verify_termination(CanController& ctrl, AdcDriver& adc)
{
    /* Set controller to listen-only to avoid disturbing the bus */
    ctrl.set_mode(CanMode::LISTEN_ONLY);

    /* Apply a known test current via internal pull network if available,
     * then measure resultant voltage drop.
     * This is a simplified heuristic — real measurement uses a 
     * calibrated current source external to the ECU.                  */

    float v_bus_idle = adc.to_voltage(adc.read_channel(ADC_CHANNEL_CAN_H).value_or(0));

    /* Healthy split-terminated bus at 5V supply:
     * V_CANH recessive ≈ 2.5 V (midpoint of two 60 Ω resistors) */
    bool ok = (v_bus_idle > 2.2f) && (v_bus_idle < 2.8f);

    ctrl.set_mode(CanMode::NORMAL);
    return ok;
}

} /* namespace can_emc */
```

---

### Example 3: Adaptive Slew Rate Based on Bus Utilization (C)

```c
/**
 * @file can_adaptive_slew.c
 * @brief Adaptive CAN slew rate control to minimize emissions dynamically
 *
 * When bus load is low, reduces slew rate to minimize emissions.
 * When transmitting high-priority/time-critical frames, increases
 * slew rate to ensure timing margins. This strategy reduces CISPR 25
 * peak emissions during idle and low-traffic periods.
 */

#include <stdint.h>
#include <stdbool.h>
#include "can_transceiver_hal.h"

/* RS pin values for NXP TJA1042T (pin-programmable slew rate) */
typedef enum {
    RS_SLOPE_FAST   = 0,  /**< RS=GND: high-speed mode, fastest slew  */
    RS_SLOPE_SLOW   = 1,  /**< RS=Vcc: slope control mode, slow slew  */
} rs_pin_state_t;

/* Bus load thresholds */
#define LOAD_HIGH_THRESHOLD    70u  /**< % — switch to fast slew       */
#define LOAD_LOW_THRESHOLD     30u  /**< % — switch back to slow slew  */
#define SLEW_HYSTERESIS_FRAMES 50u  /**< Min frames before re-evaluate */

static rs_pin_state_t current_slew  = RS_SLOPE_SLOW;
static uint32_t       frame_counter = 0u;

/**
 * @brief Update slew rate based on measured bus utilization
 *
 * Call periodically (e.g., every 100 ms) from a bus statistics task.
 *
 * @param bus_load_percent  Bus utilization in percent (0–100)
 */
void can_adaptive_slew_update(uint8_t bus_load_percent)
{
    frame_counter++;

    /* Apply hysteresis to prevent rapid toggling */
    if (frame_counter < SLEW_HYSTERESIS_FRAMES) {
        return;
    }
    frame_counter = 0u;

    rs_pin_state_t desired_slew;

    if (bus_load_percent >= LOAD_HIGH_THRESHOLD) {
        desired_slew = RS_SLOPE_FAST;
    } else if (bus_load_percent <= LOAD_LOW_THRESHOLD) {
        desired_slew = RS_SLOPE_SLOW;
    } else {
        /* Within hysteresis band — keep current setting */
        desired_slew = current_slew;
    }

    if (desired_slew != current_slew) {
        can_transceiver_set_rs_pin((uint8_t)desired_slew);
        current_slew = desired_slew;
    }
}

/**
 * @brief Force fast slew for time-critical transmission bursts
 *
 * Override adaptive algorithm for a critical transmission window.
 * Caller must call can_adaptive_slew_release() after transmission.
 */
void can_adaptive_slew_force_fast(void)
{
    can_transceiver_set_rs_pin((uint8_t)RS_SLOPE_FAST);
    current_slew = RS_SLOPE_FAST;
}

void can_adaptive_slew_release(void)
{
    /* Revert to slow slew outside critical window */
    can_transceiver_set_rs_pin((uint8_t)RS_SLOPE_SLOW);
    current_slew = RS_SLOPE_SLOW;
    frame_counter = SLEW_HYSTERESIS_FRAMES; /* Allow immediate re-evaluation */
}
```

---

## Rust Implementation Examples

### Example 4: TJA1145 SPI Driver with EMC Configuration (Rust)

```rust
//! can_emc_config.rs
//!
//! Rust driver for TJA1145 CAN SBC transceiver EMC configuration.
//! Uses `embedded-hal` traits for portability across MCU platforms.
//!
//! # Usage
//! ```rust
//! let transceiver = Tja1145::new(spi, cs_pin);
//! transceiver.init_emc(SlewRate::Slow)?;
//! ```

use embedded_hal::spi::SpiDevice;

/// TJA1145 register addresses (EMC-relevant subset)
#[allow(dead_code)]
mod reg {
    pub const MODE_CTRL:   u8 = 0x01;
    pub const CAN_CTRL:    u8 = 0x20;
    pub const TRANSCEIVER: u8 = 0x22;
    pub const SYS_EVENT:   u8 = 0x61;
}

/// Operating mode values for MODE_CTRL register
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum OperatingMode {
    Sleep   = 0x01,
    Standby = 0x04,
    Normal  = 0x07,
}

/// CAN slew rate configuration
///
/// Lower slew rates reduce radiated emissions at the cost of
/// reduced maximum signalling speed.
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum SlewRate {
    /// Maximum speed — use for CAN FD data phase > 2 Mbit/s
    Fast   = 0b00,
    /// Balanced — suitable for CAN FD up to 2 Mbit/s
    Normal = 0b01,
    /// Minimum slew — use for Classic CAN ≤ 500 kbit/s for best EMC
    Slow   = 0b11,
}

/// Error type for TJA1145 operations
#[derive(Debug)]
pub enum Tja1145Error<SpiError> {
    Spi(SpiError),
    InvalidMode,
    InitFailed,
}

impl<E> From<E> for Tja1145Error<E> {
    fn from(e: E) -> Self {
        Tja1145Error::Spi(e)
    }
}

/// TJA1145 SBC/Transceiver driver
pub struct Tja1145<SPI> {
    spi: SPI,
}

impl<SPI, E> Tja1145<SPI>
where
    SPI: SpiDevice<u8, Error = E>,
{
    /// Construct a new TJA1145 driver instance
    pub fn new(spi: SPI) -> Self {
        Self { spi }
    }

    /// Write an 8-bit value to a register
    ///
    /// TJA1145 SPI protocol: [addr<<1 | 0, data]
    fn write_reg(&mut self, reg: u8, val: u8) -> Result<(), Tja1145Error<E>> {
        let frame = [(reg << 1) & 0xFE, val];
        self.spi.write(&frame)?;
        Ok(())
    }

    /// Read an 8-bit register value
    ///
    /// TJA1145 SPI protocol: [addr<<1 | 1, 0x00] → [_, data]
    fn read_reg(&mut self, reg: u8) -> Result<u8, Tja1145Error<E>> {
        let mut buf = [(reg << 1) | 0x01, 0x00];
        self.spi.transfer_in_place(&mut buf)?;
        Ok(buf[1])
    }

    /// Modify specific bits in a register (read-modify-write)
    fn modify_reg(&mut self, reg: u8, mask: u8, value: u8) -> Result<(), Tja1145Error<E>> {
        let current = self.read_reg(reg)?;
        let updated = (current & !mask) | (value & mask);
        self.write_reg(reg, updated)
    }

    /// Set transceiver operating mode
    pub fn set_mode(&mut self, mode: OperatingMode) -> Result<(), Tja1145Error<E>> {
        self.write_reg(reg::MODE_CTRL, mode as u8)
    }

    /// Configure slew rate for EMC optimization
    ///
    /// Must be called while in Standby mode.
    /// The caller is responsible for mode transitions.
    pub fn set_slew_rate(&mut self, slew: SlewRate) -> Result<(), Tja1145Error<E>> {
        // Bits [1:0] of CAN_CTRL register control slew (CFDC field)
        const SLEW_MASK: u8 = 0x03;
        self.modify_reg(reg::CAN_CTRL, SLEW_MASK, slew as u8)
    }

    /// Full EMC-optimized initialization sequence
    ///
    /// 1. Enters standby to allow register writes
    /// 2. Configures slew rate
    /// 3. Returns to normal operating mode
    pub fn init_emc(&mut self, slew: SlewRate) -> Result<(), Tja1145Error<E>> {
        // Enter standby before configuration (required by TJA1145)
        self.set_mode(OperatingMode::Standby)?;

        // Apply EMC-optimized slew rate
        self.set_slew_rate(slew)?;

        // Return to normal operation
        self.set_mode(OperatingMode::Normal)?;

        Ok(())
    }

    /// Enter low-emission standby mode
    ///
    /// Reduces active CAN transceiver emissions when bus is idle.
    pub fn enter_standby(&mut self) -> Result<(), Tja1145Error<E>> {
        self.set_mode(OperatingMode::Standby)
    }

    /// Restore normal operation from standby
    pub fn resume_normal(&mut self) -> Result<(), Tja1145Error<E>> {
        self.set_mode(OperatingMode::Normal)
    }

    /// Read transceiver status register for diagnostic purposes
    ///
    /// Returns raw status byte; caller interprets bit fields.
    pub fn read_status(&mut self) -> Result<u8, Tja1145Error<E>> {
        self.read_reg(reg::TRANSCEIVER)
    }
}
```

---

### Example 5: CAN Physical Layer Monitor in Rust (no_std)

```rust
//! can_physical_monitor.rs
//!
//! `no_std` module for monitoring CAN physical layer health as a
//! proxy for EMC filter integrity. Detects degraded or failed
//! common-mode chokes, termination resistors, and TVS diodes
//! by correlating error counter trends with voltage anomalies.

#![no_std]

use core::sync::atomic::{AtomicU32, Ordering};

/// Window size for error rate calculation
const ERROR_WINDOW_FRAMES: u32 = 1000;

/// Thresholds for anomaly detection
const TEC_WARN: u8  = 96;
const TEC_CRIT: u8  = 127;
const REC_WARN: u8  = 96;
const REC_CRIT: u8  = 127;

/// Possible EMC-related fault diagnoses
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum EmcFaultType {
    /// High TEC without corresponding REC: likely open/wrong termination
    TerminationFault,
    /// High REC with low TEC: likely EMI susceptibility (victim)
    EmcSusceptibility,
    /// Both TEC and REC elevated: wiring fault or strong field
    SevereNoiseCoupling,
    /// Common-mode voltage out of range: CMC or split-termination cap failed
    CommonModeFilterFault,
    /// Bus-off state: repeated hard errors
    BusOff,
}

/// Snapshot of CAN error state for trend analysis
#[derive(Debug, Clone, Copy, Default)]
pub struct ErrorSnapshot {
    pub tec: u8,   ///< Transmit error counter
    pub rec: u8,   ///< Receive error counter
    pub bus_off: bool,
}

/// CAN EMC health monitor
pub struct EmcMonitor {
    /// Accumulated TEC increments over the observation window
    tec_accum: AtomicU32,
    /// Accumulated REC increments over the observation window
    rec_accum: AtomicU32,
    /// Total frames observed
    frame_count: AtomicU32,
    /// Last recorded common-mode voltage (mV × 10 for fixed-point)
    last_vcm_mv10: AtomicU32,
}

impl EmcMonitor {
    pub const fn new() -> Self {
        Self {
            tec_accum:    AtomicU32::new(0),
            rec_accum:    AtomicU32::new(0),
            frame_count:  AtomicU32::new(0),
            last_vcm_mv10: AtomicU32::new(25_000), // 2500.0 mV nominal
        }
    }

    /// Record an error counter snapshot (call from CAN ISR or task)
    pub fn record_snapshot(&self, snap: ErrorSnapshot) {
        self.tec_accum.fetch_add(snap.tec as u32, Ordering::Relaxed);
        self.rec_accum.fetch_add(snap.rec as u32, Ordering::Relaxed);
        self.frame_count.fetch_add(1, Ordering::Relaxed);
    }

    /// Update measured common-mode voltage (in millivolts)
    pub fn record_common_mode_mv(&self, vcm_mv: u32) {
        // Store as mv×10 for one decimal precision in fixed-point
        self.last_vcm_mv10.store(vcm_mv * 10, Ordering::Relaxed);
    }

    /// Analyse accumulated data and return detected fault, if any
    ///
    /// Call periodically (e.g., every 500 ms) from a diagnostic task.
    /// Resets accumulators after analysis.
    pub fn analyse(&self) -> Option<EmcFaultType> {
        let frames = self.frame_count.swap(0, Ordering::Relaxed);
        if frames < ERROR_WINDOW_FRAMES / 2 {
            return None; // Insufficient data
        }

        let tec_sum = self.tec_accum.swap(0, Ordering::Relaxed);
        let rec_sum = self.rec_accum.swap(0, Ordering::Relaxed);

        // Derive representative "current" error counter values
        let tec_avg = (tec_sum / frames.max(1)).min(255) as u8;
        let rec_avg = (rec_sum / frames.max(1)).min(255) as u8;

        // Bus-off check (highest priority)
        if tec_avg >= 255 {
            return Some(EmcFaultType::BusOff);
        }

        // Common-mode voltage check
        let vcm_mv10 = self.last_vcm_mv10.load(Ordering::Relaxed);
        let vcm_nominal_mv10 = 25_000u32; // 2500.0 mV
        let vcm_tolerance_mv10 = 5_000u32; // ±500.0 mV
        let cm_ok = vcm_mv10.abs_diff(vcm_nominal_mv10) <= vcm_tolerance_mv10;

        if !cm_ok {
            return Some(EmcFaultType::CommonModeFilterFault);
        }

        // Error counter pattern analysis
        match (tec_avg, rec_avg) {
            (t, r) if t >= TEC_CRIT && r >= REC_CRIT => {
                Some(EmcFaultType::SevereNoiseCoupling)
            }
            (t, _) if t >= TEC_WARN => {
                Some(EmcFaultType::TerminationFault)
            }
            (_, r) if r >= REC_WARN => {
                Some(EmcFaultType::EmcSusceptibility)
            }
            _ => None,
        }
    }
}

// Safety: AtomicU32 is Send+Sync; EmcMonitor is safe to share across tasks
unsafe impl Send for EmcMonitor {}
unsafe impl Sync for EmcMonitor {}
```

---

### Example 6: Rust — Adaptive Standby Mode Manager

```rust
//! can_standby_manager.rs
//!
//! Manages CAN transceiver standby transitions to reduce EMI
//! when the bus is idle. Implements a countdown-based policy:
//! after N consecutive idle frames, enters standby automatically.

use core::time::Duration;

/// Idle timeout before entering standby (reduces emissions during sleep modes)
const IDLE_TIMEOUT: Duration = Duration::from_millis(200);

/// Minimum active time before standby is allowed again
const MIN_ACTIVE_DURATION: Duration = Duration::from_millis(50);

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum StandbyState {
    Active,
    EnteringStandby,
    Standby,
    Waking,
}

pub struct StandbyManager {
    state:            StandbyState,
    idle_since_ms:    u64,
    active_since_ms:  u64,
}

impl StandbyManager {
    pub fn new(now_ms: u64) -> Self {
        Self {
            state:           StandbyState::Active,
            idle_since_ms:   now_ms,
            active_since_ms: now_ms,
        }
    }

    /// Notify the manager that CAN activity was detected
    pub fn on_bus_activity(&mut self, now_ms: u64) {
        match self.state {
            StandbyState::Standby | StandbyState::EnteringStandby => {
                self.state = StandbyState::Waking;
                self.active_since_ms = now_ms;
            }
            StandbyState::Waking => {
                let active_duration = now_ms.saturating_sub(self.active_since_ms);
                if active_duration >= MIN_ACTIVE_DURATION.as_millis() as u64 {
                    self.state = StandbyState::Active;
                }
            }
            StandbyState::Active => {
                self.idle_since_ms = now_ms; // Reset idle timer
            }
        }
    }

    /// Periodic tick — call every 10 ms
    ///
    /// Returns `true` if the transceiver standby pin should be asserted.
    pub fn tick(&mut self, now_ms: u64) -> bool {
        if self.state == StandbyState::Active {
            let idle_ms = now_ms.saturating_sub(self.idle_since_ms);
            if idle_ms >= IDLE_TIMEOUT.as_millis() as u64 {
                self.state = StandbyState::EnteringStandby;
            }
        }

        matches!(self.state,
            StandbyState::EnteringStandby | StandbyState::Standby)
    }

    /// Acknowledge that standby has been entered (call after asserting pin)
    pub fn confirm_standby(&mut self) {
        if self.state == StandbyState::EnteringStandby {
            self.state = StandbyState::Standby;
        }
    }

    pub fn current_state(&self) -> StandbyState {
        self.state
    }
}
```

---

## EMC Testing and Diagnostics

### CISPR 25 Radiated Emissions Test Setup

```
  Antenna (1 m height)         Vehicle/Harness Simulation
  ┌───────┐                    ┌──────────────────────────┐
  │ Log   │◄── 1 m ───────────►│  ECU DUT  │  CAN Harness │
  │ Per.  │                    │           │  (1.5 m loom)│
  │Antenna│                    └──────────────────────────┘
  └───────┘                                │
     │                             Bulk head connector
     ▼                                     │
  Spectrum Analyser                     Load box
  (9 kHz–6 GHz)                   (termination network)
```

### Key Frequency Ranges for CAN

| CAN Baudrate | Fundamental Frequency | Harmonics of Concern |
|---|---|---|
| 125 kbit/s   | 62.5 kHz | Up to 100th harmonic at 6.25 MHz |
| 500 kbit/s   | 250 kHz  | Up to 25th harmonic at 6.25 MHz  |
| 1 Mbit/s     | 500 kHz  | Up to 12th harmonic at 6 MHz     |
| 2 Mbit/s FD  | 1 MHz    | Up to 6th harmonic at 6 MHz      |
| 5 Mbit/s FD  | 2.5 MHz  | Up to 3rd harmonic at 7.5 MHz    |

### Diagnosing EMC Issues in the Field

A useful triage sequence when a CAN system fails EMC testing:

1. **Check bus utilization** — Reduce message rate; emissions should drop proportionally. If they don't, suspect ringing/reflection rather than direct radiation.
2. **Measure CM voltage** — Use an oscilloscope (differential probe) to measure `(V_CANH + V_CANL)/2`. Deviation from 2.5 V indicates CMC saturation or filter failure.
3. **Check bit timing eye diagram** — Compression or asymmetry suggests impedance mismatch or reflections from stub lengths or missing termination.
4. **Measure near-field** — Use a near-field probe over the PCB to locate the dominant EMI source (connector area, transceiver IC, or ground return path).
5. **Characterize slew rate** — Measure rise/fall time at the connector. For 500 kbit/s CAN, target 200–400 ns rise time.

---

## Summary

Achieving automotive EMC compliance for CAN physical layers requires a systematic, multi-layer approach combining hardware, layout, and software:

**Hardware fundamentals** center on the EMC filter network at the bus connector: a common-mode choke (100–600 µH) blocks common-mode noise without distorting the differential CAN signal, series resistors (10–33 Ω) damp high-frequency ringing, and split termination with a bypass capacitor (60+60 Ω / 4.7–100 nF) provides both correct impedance and common-mode filtering. ESD protection (bidirectional TVS diodes) safeguards against ISO 10605 and ISO 7637 transients.

**PCB layout** is often the largest single factor in radiated emissions performance. Routing CAN traces over a continuous ground plane, minimizing trace length between transceiver and connector, placing the filter network at the connector rather than the transceiver, and using a 4-layer stack-up with a dedicated ground plane all reduce loop area and thus radiated field strength.

**Software and transceiver configuration** allows dynamic optimisation: controlling the RS pin or SPI register of intelligent transceivers to reduce slew rate during low-traffic periods cuts spectral content at harmonics above the fundamental; entering standby/sleep mode on idle buses eliminates emissions almost entirely; and monitoring error counters and common-mode voltage in real time provides early warning of degraded EMC filter components.

**Standards compliance** requires testing to CISPR 25 (emissions), ISO 11452 (immunity), ISO 7637 (conducted transients), and ISO 10605 (ESD), with OEM-specific addenda (LV 124, GMW3097) typically imposing tighter limits. Systematically addressing each layer — component selection, layout, and firmware — produces a robust design that meets both EMC requirements and the reliability demands of automotive applications throughout their service life.

---

*Document: 55_EMC_Considerations.md | CAN Bus Technology Reference Series*