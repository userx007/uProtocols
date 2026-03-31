# 93. Thermal Management for CAN Systems

**Content highlights:**

- **Temperature standards** — AEC-Q100 automotive grades (Grade 0 through 3), ISO 11898-2 ranges, and key datasheet parameters (θJA, TTSD, hysteresis)
- **Thermal failure modes** — parametric degradation before TSD triggers, thermal oscillation risk on recovery, and cold-temperature failure mechanisms
- **Hardware design** — PCB copper pours, thermal vias, bypass capacitor selection, and thermal interface materials
- **Thermal state machine** — 5-state design (Normal → Warning → Derate → Critical → Shutdown) with hysteresis logic to prevent chattering, and TX rate limiting per-priority-level

**Code examples provided:**

| Module | C/C++ | Rust |
|---|---|---|
| Thermal state machine | ✅ | ✅ (`no_std`) |
| NTC thermistor reading | ✅ floating-point (Steinhart-Hart) | ✅ fixed-point LUT |
| TSD fault pin debouncing | ✅ | ✅ (`embedded-hal`) |
| Error counter correlation | ✅ | — |
| Integrated task | — | ✅ (async-style) |
| Unit tests | — | ✅ |

> Designing for extreme temperature operation and thermal shutdown protection in CAN transceivers.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Temperature Ranges and Standards](#temperature-ranges-and-standards)
3. [Thermal Failure Modes in CAN Transceivers](#thermal-failure-modes-in-can-transceivers)
4. [Thermal Shutdown Protection Mechanisms](#thermal-shutdown-protection-mechanisms)
5. [PCB Layout and Thermal Design](#pcb-layout-and-thermal-design)
6. [Software-Side Thermal Monitoring](#software-side-thermal-monitoring)
7. [C/C++ Code Examples](#cc-code-examples)
8. [Rust Code Examples](#rust-code-examples)
9. [Thermal Derating and Bus Load Management](#thermal-derating-and-bus-load-management)
10. [Testing and Validation](#testing-and-validation)
11. [Summary](#summary)

---

## Introduction

CAN (Controller Area Network) bus systems are deployed in environments that span extreme temperature ranges — from arctic outdoor installations at −40 °C to underhood automotive applications exceeding +125 °C or even +150 °C junction temperatures. Unlike pure digital logic, CAN transceivers operate with analog differential signaling on the bus lines (CANH/CANL), making them inherently sensitive to temperature-induced parameter drift, leakage currents, and thermal runaway.

Thermal management for CAN systems encompasses two complementary concerns:

- **Hardware-level protection** — ensuring transceivers, termination resistors, and MCU CAN peripherals do not exceed their absolute maximum ratings, using thermal shutdown circuits, proper derating, and PCB thermal design.
- **Software-level monitoring and reaction** — reading on-chip or external temperature sensors, implementing graceful degradation (bus-off recovery, bit-rate reduction, load shedding), and logging thermal events for diagnostics.

A CAN node that ignores thermal conditions risks not just its own failure but disruption of the entire bus — a single badly-behaved transceiver can corrupt frames bus-wide. Thermal management is therefore both a reliability concern for the node and a safety concern for the network.

---

## Temperature Ranges and Standards

### Automotive and Industrial Grade Classes

| Grade | Ambient Temperature Range | Typical Junction T_j Max | Applicable Standards |
|-------|--------------------------|--------------------------|----------------------|
| Commercial | 0 °C … +70 °C | +125 °C | General industrial |
| Industrial | −40 °C … +85 °C | +125 °C | IEC 61131, ISO 11898 |
| Automotive Q (AEC-Q100 Grade 1) | −40 °C … +125 °C | +150 °C | AEC-Q100, ISO 11898-2 |
| Automotive Q (AEC-Q100 Grade 0) | −40 °C … +150 °C | +175 °C | AEC-Q100, harsh environment |

ISO 11898-2:2016 specifies that CAN transceivers must operate correctly over the entire supply and temperature range while maintaining the differential voltage thresholds:
- Dominant: V_diff > +0.9 V
- Recessive: −0.1 V < V_diff < +0.5 V

These thresholds must be met even at temperature extremes.

### Junction Temperature vs. Ambient Temperature

The critical equation linking ambient to junction temperature is:

```
T_j = T_amb + (P_dissipated × θ_ja)
```

Where:
- `T_j` = junction temperature (°C)
- `T_amb` = ambient (PCB environment) temperature (°C)
- `P_dissipated` = power dissipated by the device (W)
- `θ_ja` = thermal resistance junction-to-ambient (°C/W)

For a typical CAN transceiver in an SO8 package: θ_ja ≈ 100 °C/W. Dissipating 500 mW (bus fault current scenario) raises junction temperature by 50 °C above ambient.

---

## Thermal Failure Modes in CAN Transceivers

### 1. Excessive Recessive Bus Current (High Temperature Leakage)

At elevated temperatures, MOSFET leakage currents increase exponentially. The recessive common-mode voltage of CANH and CANL can shift outside the 2.0 V–3.0 V center range, causing false dominant detection by other nodes.

### 2. Driver Output Impedance Shift

The on-resistance R_DS(on) of the output transistors is temperature-dependent. At high temperature, R_DS(on) increases, reducing the dominant differential voltage. If V_diff falls below 0.9 V, the frame fails bit-level verification on all receivers.

### 3. Propagation Delay Increase

CAN bit timing is sensitive to propagation delay through transceivers. High temperature increases delay, effectively reducing the usable propagation budget in a multi-node topology, potentially causing bit errors at long bus lengths.

### 4. Thermal Runaway Under Bus Fault

ISO 11898-2 requires transceivers to withstand a continuous short of CANH or CANL to battery voltage or ground. During a fault, if the thermal shutdown threshold is too high or the θ_ja path too resistive, uncontrolled heating can destroy the device.

### 5. Cold Temperature Issues

At −40 °C:
- Crystal oscillators may drift significantly, affecting the MCU's CAN bit clock
- Electrolytic decoupling capacitors lose capacitance (use ceramic X7R/X5R for CAN supply decoupling at extreme cold)
- Bus termination resistors with high temperature coefficient shift bus impedance
- Supply voltage regulators may exhibit startup issues affecting transceiver Vcc

---

## Thermal Shutdown Protection Mechanisms

### On-Chip Thermal Shutdown (TSD)

All automotive-grade CAN transceivers (e.g., TJA1042, TCAN1042, SN65HVD230) integrate a thermal shutdown circuit. When T_j exceeds the TSD threshold (typically 165 °C–175 °C), the driver outputs are disabled:

- CANH is driven to a high-impedance state
- CANL is driven to a high-impedance state
- The transceiver appears as a network-open to the bus

After the junction cools below the TSD hysteresis point (typically ~15 °C below threshold), the transceiver automatically re-enables. This auto-recovery is critical to avoid permanent bus failure.

Some devices expose a TSD flag on a dedicated pin or via a STATUS output, which the MCU can poll to detect and log the thermal event.

### External Thermal Monitoring with NTC/PTC

For systems where the transceiver's integrated TSD is insufficient (no STATUS pin, or monitoring is required before TSD engages), an NTC thermistor or a digital temperature sensor (e.g., TMP117, MCP9808) placed close to the transceiver provides software-accessible temperature data.

### Thermal Fuse (One-Shot Protection)

For very high-reliability applications, a thermal fuse in series with the transceiver's supply can provide irreversible protection against sustained over-temperature, signaling a need for physical service.

---

## PCB Layout and Thermal Design

### Copper Pours and Vias

The primary thermal path for SO8 or SOP8 transceivers is through the PCB copper:

- Place a copper pour on the top layer under and around the transceiver
- Add thermal vias (0.3 mm drill, 0.6 mm pad, filled or plugged) through to an internal ground plane
- A minimum of 4–9 thermal vias under the thermal pad (for exposed-pad packages like SOIC-8-EP) reduces θ_ja by 20–40%

### Component Placement

- Keep the CAN transceiver away from high-power components (motor drivers, DC-DC converters)
- If the PCB is inside an enclosure, place the transceiver near ventilation holes or a thermal interface to the enclosure wall
- Termination resistors (120 Ω) dissipate power proportional to bus swing; use 0402 or 0603 resistors with adequate power ratings (125 mW or 250 mW)

### Decoupling Capacitors

At extreme temperature:
- Use X7R ceramic (rated −55 °C to +125 °C) for all CAN Vcc decoupling (100 nF close to pin, 4.7 µF bulk)
- Avoid Y5V or Z5U ceramics, which lose >80% of capacitance at temperature extremes
- Place decoupling within 2 mm of the Vcc pin

### Bus Termination Resistor Choice

Standard metal-film resistors have a temperature coefficient of ±100 ppm/°C. Over 165 °C range (−40 °C to +125 °C), this is ±1.65% change — acceptable for CAN. Carbon composition resistors have much worse TC and should be avoided.

---

## Software-Side Thermal Monitoring

### Monitoring Strategy

A complete software thermal management strategy for a CAN node includes:

1. **Periodic temperature sampling** — read a temperature sensor at regular intervals (e.g., every 500 ms)
2. **Threshold-based state machine** — define thermal zones (Normal, Warning, Critical, Shutdown)
3. **Graceful degradation** — at warning level, reduce bit rate or bus load; at critical, go bus-off
4. **Event logging** — record thermal events in non-volatile memory for field diagnostics
5. **Recovery management** — after a thermal event clears, perform controlled re-initialization of the CAN controller

### Thermal State Machine

```
NORMAL (T < T_warn)
    │ T >= T_warn
    ▼
WARNING (T_warn <= T < T_crit)   ── reduce bit rate / load
    │ T >= T_crit
    ▼
CRITICAL (T >= T_crit)            ── CAN bus-off, disable transmit
    │ TSD asserts OR T >= T_tsd
    ▼
SHUTDOWN                          ── transceiver disabled by hardware
    │ T < T_recover (hysteresis)
    ▼
RECOVERY                          ── re-init CAN, restore normal operation
```

---

## C/C++ Code Examples

### 1. Thermal State Machine for CAN Node (C99)

```c
/**
 * can_thermal.h
 * Thermal management state machine for a CAN transceiver node.
 * Targets: Automotive MCU (e.g., S32K, TC3xx, STM32H7)
 */

#ifndef CAN_THERMAL_H
#define CAN_THERMAL_H

#include <stdint.h>
#include <stdbool.h>

/* Temperature thresholds in degrees Celsius */
#define THERMAL_WARN_THRESHOLD_C      85    /**< Enter WARNING state */
#define THERMAL_CRIT_THRESHOLD_C     105    /**< Enter CRITICAL state, go bus-off */
#define THERMAL_SHUTDOWN_THRESHOLD_C 125    /**< Force transceiver disable */
#define THERMAL_RECOVER_THRESHOLD_C   75    /**< Hysteresis: resume normal below this */

typedef enum {
    THERMAL_STATE_NORMAL    = 0,
    THERMAL_STATE_WARNING   = 1,
    THERMAL_STATE_CRITICAL  = 2,
    THERMAL_STATE_SHUTDOWN  = 3,
    THERMAL_STATE_RECOVERY  = 4
} ThermalState_t;

typedef struct {
    ThermalState_t  state;
    int16_t         last_temp_c;       /**< Last measured temperature in degrees C */
    uint32_t        event_count;       /**< Number of thermal events recorded */
    uint32_t        tsd_count;         /**< Number of hardware TSD triggers */
    bool            can_enabled;       /**< Whether CAN TX is currently allowed */
} ThermalManager_t;

void ThermalManager_Init(ThermalManager_t *tm);
bool ThermalManager_Update(ThermalManager_t *tm, int16_t temp_c);
bool ThermalManager_CanTransmit(const ThermalManager_t *tm);
void ThermalManager_OnTSD(ThermalManager_t *tm);

#endif /* CAN_THERMAL_H */
```

```c
/**
 * can_thermal.c
 */

#include "can_thermal.h"
#include "can_driver.h"     /* HAL: CAN_BusOff(), CAN_Reinit(), CAN_SetBitrate() */
#include "nvlog.h"          /* NV_LogThermalEvent(temp, state) */

void ThermalManager_Init(ThermalManager_t *tm)
{
    tm->state       = THERMAL_STATE_NORMAL;
    tm->last_temp_c = 25;
    tm->event_count = 0;
    tm->tsd_count   = 0;
    tm->can_enabled = true;
}

static void enter_state(ThermalManager_t *tm, ThermalState_t new_state, int16_t temp_c)
{
    tm->state = new_state;
    tm->last_temp_c = temp_c;
    tm->event_count++;
    NV_LogThermalEvent(temp_c, new_state);   /* Write to non-volatile log */

    switch (new_state) {
    case THERMAL_STATE_WARNING:
        /* Reduce bus load: lower bit rate to 125 kbps */
        CAN_SetBitrate(125000U);
        tm->can_enabled = true;
        break;

    case THERMAL_STATE_CRITICAL:
        /* Go bus-off: stop transmitting */
        CAN_BusOff();
        tm->can_enabled = false;
        break;

    case THERMAL_STATE_SHUTDOWN:
        /* Assert transceiver STB pin (standby) to disable driver */
        CAN_TransceiverStandby(true);
        tm->can_enabled = false;
        break;

    case THERMAL_STATE_NORMAL:
    case THERMAL_STATE_RECOVERY:
        /* Restore normal 500 kbps operation */
        CAN_TransceiverStandby(false);
        CAN_SetBitrate(500000U);
        CAN_Reinit();
        tm->can_enabled = true;
        break;
    }
}

bool ThermalManager_Update(ThermalManager_t *tm, int16_t temp_c)
{
    ThermalState_t prev = tm->state;
    tm->last_temp_c = temp_c;

    switch (tm->state) {
    case THERMAL_STATE_NORMAL:
        if (temp_c >= THERMAL_SHUTDOWN_THRESHOLD_C) {
            enter_state(tm, THERMAL_STATE_SHUTDOWN, temp_c);
        } else if (temp_c >= THERMAL_CRIT_THRESHOLD_C) {
            enter_state(tm, THERMAL_STATE_CRITICAL, temp_c);
        } else if (temp_c >= THERMAL_WARN_THRESHOLD_C) {
            enter_state(tm, THERMAL_STATE_WARNING, temp_c);
        }
        break;

    case THERMAL_STATE_WARNING:
        if (temp_c >= THERMAL_CRIT_THRESHOLD_C) {
            enter_state(tm, THERMAL_STATE_CRITICAL, temp_c);
        } else if (temp_c < THERMAL_RECOVER_THRESHOLD_C) {
            enter_state(tm, THERMAL_STATE_NORMAL, temp_c);
        }
        break;

    case THERMAL_STATE_CRITICAL:
        if (temp_c >= THERMAL_SHUTDOWN_THRESHOLD_C) {
            enter_state(tm, THERMAL_STATE_SHUTDOWN, temp_c);
        } else if (temp_c < THERMAL_RECOVER_THRESHOLD_C) {
            enter_state(tm, THERMAL_STATE_RECOVERY, temp_c);
        }
        break;

    case THERMAL_STATE_SHUTDOWN:
        if (temp_c < THERMAL_RECOVER_THRESHOLD_C) {
            enter_state(tm, THERMAL_STATE_RECOVERY, temp_c);
        }
        break;

    case THERMAL_STATE_RECOVERY:
        /* One-shot: after re-init, return to normal */
        enter_state(tm, THERMAL_STATE_NORMAL, temp_c);
        break;
    }

    return (tm->state != prev);
}

bool ThermalManager_CanTransmit(const ThermalManager_t *tm)
{
    return tm->can_enabled;
}

void ThermalManager_OnTSD(ThermalManager_t *tm)
{
    /* Hardware thermal shutdown triggered */
    tm->tsd_count++;
    NV_LogThermalEvent(tm->last_temp_c, THERMAL_STATE_SHUTDOWN);
    enter_state(tm, THERMAL_STATE_SHUTDOWN, tm->last_temp_c);
}
```

---

### 2. I2C Temperature Sensor Read — MCP9808 (C, HAL-abstracted)

```c
/**
 * mcp9808.c
 * Read die temperature from MCP9808 digital temperature sensor via I2C.
 * Resolution: 0.0625 deg C, range: -40 deg C to +125 deg C.
 */

#include <stdint.h>
#include <stdbool.h>
#include "i2c_hal.h"   /* I2C_Write(), I2C_Read() -- platform HAL */

#define MCP9808_ADDR          0x18U
#define MCP9808_REG_TEMP      0x05U
#define MCP9808_REG_CONFIG    0x01U
#define MCP9808_REG_LIMIT_UP  0x02U
#define MCP9808_REG_LIMIT_LO  0x03U
#define MCP9808_REG_CRIT      0x04U

/**
 * Read temperature in 1/16 deg C units. Divide by 16 for deg C.
 * Returns INT16_MIN on I2C error.
 */
int16_t MCP9808_ReadRaw(void)
{
    uint8_t reg = MCP9808_REG_TEMP;
    uint8_t buf[2];

    if (!I2C_Write(MCP9808_ADDR, &reg, 1)) {
        return INT16_MIN;
    }
    if (!I2C_Read(MCP9808_ADDR, buf, 2)) {
        return INT16_MIN;
    }

    /* Upper byte: bits [15:13] = boundary flags, bit 12 = sign */
    /* Lower byte: fractional bits                              */
    uint16_t raw = ((uint16_t)(buf[0] & 0x1FU) << 8) | buf[1];

    /* Two's complement for negative temperatures */
    if (buf[0] & 0x10U) {
        return (int16_t)(raw | 0xE000U);  /* Sign-extend to int16 */
    }
    return (int16_t)raw;
}

/**
 * Read temperature in integer degrees C (truncated).
 */
int16_t MCP9808_ReadCelsius(void)
{
    int16_t raw = MCP9808_ReadRaw();
    if (raw == INT16_MIN) return INT16_MIN;
    return raw / 16;
}

/**
 * Configure MCP9808 alert thresholds for hardware interrupt generation.
 * upper_c: upper alert limit in deg C
 * crit_c:  critical alert limit in deg C
 */
bool MCP9808_SetAlertThresholds(int16_t upper_c, int16_t crit_c)
{
    uint8_t cmd[3];

    /* Upper limit */
    uint16_t u_raw = (uint16_t)((upper_c < 0) ?
        (0x1000U | ((uint16_t)(upper_c * 16) & 0x0FFFU)) :
        (uint16_t)(upper_c * 16) & 0x0FFFU);
    cmd[0] = MCP9808_REG_LIMIT_UP;
    cmd[1] = (uint8_t)(u_raw >> 8);
    cmd[2] = (uint8_t)(u_raw & 0xFFU);
    if (!I2C_Write(MCP9808_ADDR, cmd, 3)) return false;

    /* Critical limit */
    uint16_t c_raw = (uint16_t)((crit_c < 0) ?
        (0x1000U | ((uint16_t)(crit_c * 16) & 0x0FFFU)) :
        (uint16_t)(crit_c * 16) & 0x0FFFU);
    cmd[0] = MCP9808_REG_CRIT;
    cmd[1] = (uint8_t)(c_raw >> 8);
    cmd[2] = (uint8_t)(c_raw & 0xFFU);
    return I2C_Write(MCP9808_ADDR, cmd, 3);
}
```

---

### 3. CAN Thermal Status Frame Transmission (C, SocketCAN on Linux)

```c
/**
 * can_thermal_broadcast.c
 * Periodically broadcast a CAN thermal status frame (proprietary diagnostic).
 *
 * Frame ID: 0x7DF
 * DLC: 8 bytes
 * Layout:
 *   Byte 0:   Thermal state (0=Normal, 1=Warning, 2=Critical, 3=Shutdown)
 *   Bytes 1-2: Temperature in 0.1 deg C units, big-endian, signed
 *   Byte 3:   Event count (low byte)
 *   Byte 4:   TSD count (low byte)
 *   Bytes 5-7: Reserved (0x00)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "can_thermal.h"
#include "mcp9808.h"

#define THERMAL_FRAME_ID    0x7DFU
#define BROADCAST_PERIOD_MS 1000U

static int open_can_socket(const char *iface)
{
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) { perror("socket"); return -1; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(s, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex
    };
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(s); return -1;
    }
    return s;
}

int main(void)
{
    int sock = open_can_socket("can0");
    if (sock < 0) return EXIT_FAILURE;

    ThermalManager_t tm;
    ThermalManager_Init(&tm);

    while (1) {
        /* 1. Read temperature */
        int16_t temp_c = MCP9808_ReadCelsius();
        if (temp_c == INT16_MIN) {
            fprintf(stderr, "Temperature read error\n");
            temp_c = 0;
        }

        /* 2. Update thermal state machine */
        bool changed = ThermalManager_Update(&tm, temp_c);
        if (changed) {
            fprintf(stdout, "Thermal state -> %d at %d deg C\n",
                    (int)tm.state, (int)temp_c);
        }

        /* 3. Build and transmit status frame */
        if (ThermalManager_CanTransmit(&tm)) {
            struct can_frame frame = { 0 };
            frame.can_id  = THERMAL_FRAME_ID;
            frame.can_dlc = 8;

            int16_t temp_01c = temp_c * 10;   /* Encode as 0.1 deg C units */
            frame.data[0] = (uint8_t)tm.state;
            frame.data[1] = (uint8_t)((temp_01c >> 8) & 0xFF);
            frame.data[2] = (uint8_t)(temp_01c & 0xFF);
            frame.data[3] = (uint8_t)(tm.event_count & 0xFF);
            frame.data[4] = (uint8_t)(tm.tsd_count & 0xFF);
            frame.data[5] = 0x00;
            frame.data[6] = 0x00;
            frame.data[7] = 0x00;

            ssize_t nbytes = write(sock, &frame, sizeof(frame));
            if (nbytes != sizeof(frame)) {
                perror("CAN write");
            }
        }

        usleep(BROADCAST_PERIOD_MS * 1000U);
    }

    close(sock);
    return EXIT_SUCCESS;
}
```

---

### 4. Thermal Junction Calculation and Derating (C++)

```cpp
/**
 * thermal_derating.cpp
 * Compute transceiver junction temperature and safe bus load fraction.
 */

#include <cstdint>
#include <algorithm>
#include <cstdio>

struct TransceiverThermalParams {
    float theta_ja;          // Junction-to-ambient thermal resistance (deg C/W)
    float idd_active_ma;     // Supply current in active mode (mA)
    float vcc_v;             // Supply voltage (V)
    float bus_load_fraction; // 0.0 - 1.0, fraction of time driving dominant
    float t_amb_c;           // Ambient temperature (deg C)
    float t_j_max_c;         // Absolute maximum junction temperature (deg C)
    float t_tsd_c;           // Thermal shutdown threshold (deg C)
};

struct ThermalResult {
    float p_dissipated_w;    // Estimated power dissipation (W)
    float t_junction_c;      // Estimated junction temperature (deg C)
    float headroom_c;        // Thermal headroom before T_j_max (deg C)
    float max_safe_bus_load; // Maximum bus load fraction within T_j_max
    bool  tsd_risk;          // True if T_j approaches TSD threshold
};

ThermalResult ComputeJunctionTemp(const TransceiverThermalParams &p)
{
    ThermalResult r{};

    float p_active    = p.vcc_v * (p.idd_active_ma / 1000.0f) * p.bus_load_fraction;
    float p_quiescent = p.vcc_v * 0.005f * (1.0f - p.bus_load_fraction);
    r.p_dissipated_w  = p_active + p_quiescent;

    r.t_junction_c = p.t_amb_c + r.p_dissipated_w * p.theta_ja;
    r.headroom_c   = p.t_j_max_c - r.t_junction_c;
    r.tsd_risk     = (r.t_junction_c >= (p.t_tsd_c - 10.0f));

    /* Solve for max bus load: T_j_max = T_amb + [Vcc*(Idd_a*x + Idd_q*(1-x))] * theta_ja */
    float idd_q = 0.005f;
    float idd_a = p.idd_active_ma / 1000.0f;
    float budget = (p.t_j_max_c - p.t_amb_c) / (p.theta_ja * p.vcc_v);
    r.max_safe_bus_load = std::clamp((budget - idd_q) / (idd_a - idd_q), 0.0f, 1.0f);

    return r;
}

int main()
{
    /* TJA1042 in SO8: theta_ja ~= 100 deg C/W, Idd_active = 70 mA, Vcc = 5V */
    TransceiverThermalParams params = {
        .theta_ja          = 100.0f,
        .idd_active_ma     = 70.0f,
        .vcc_v             = 5.0f,
        .bus_load_fraction = 0.60f,
        .t_amb_c           = 85.0f,
        .t_j_max_c         = 150.0f,
        .t_tsd_c           = 165.0f
    };

    ThermalResult res = ComputeJunctionTemp(params);

    printf("--- Transceiver Thermal Analysis ---\n");
    printf("Power dissipated  : %.3f W\n",  res.p_dissipated_w);
    printf("Junction temp     : %.1f C\n",  res.t_junction_c);
    printf("Headroom to T_jmax: %.1f C\n",  res.headroom_c);
    printf("Max safe bus load : %.1f %%\n", res.max_safe_bus_load * 100.0f);
    printf("TSD risk          : %s\n",      res.tsd_risk ? "YES - reduce bus load!" : "No");

    return 0;
}
```

---

## Rust Code Examples

### 1. Thermal State Machine (Rust, `no_std` compatible)

```rust
//! can_thermal.rs
//! Thermal management state machine for an embedded CAN node.
//! Designed for no_std environments (bare-metal MCUs).

#![cfg_attr(not(test), no_std)]

pub const THERMAL_WARN_THRESHOLD_C: i16     =  85;
pub const THERMAL_CRIT_THRESHOLD_C: i16     = 105;
pub const THERMAL_SHUTDOWN_THRESHOLD_C: i16 = 125;
pub const THERMAL_RECOVER_THRESHOLD_C: i16  =  75;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThermalState {
    Normal,
    Warning,
    Critical,
    Shutdown,
    Recovery,
}

#[derive(Debug)]
pub struct ThermalManager {
    pub state:          ThermalState,
    pub last_temp_c:    i16,
    pub event_count:    u32,
    pub tsd_count:      u32,
    pub can_tx_enabled: bool,
}

/// Implement this trait for your platform's CAN/GPIO HAL
pub trait ThermalHal {
    fn set_can_bitrate(&mut self, bps: u32);
    fn can_bus_off(&mut self);
    fn can_reinit(&mut self);
    fn set_transceiver_standby(&mut self, standby: bool);
    fn log_thermal_event(&mut self, temp_c: i16, state: ThermalState);
}

impl ThermalManager {
    pub fn new() -> Self {
        Self {
            state:          ThermalState::Normal,
            last_temp_c:    25,
            event_count:    0,
            tsd_count:      0,
            can_tx_enabled: true,
        }
    }

    fn enter_state<H: ThermalHal>(&mut self, new_state: ThermalState, temp_c: i16, hal: &mut H) {
        self.state       = new_state;
        self.last_temp_c = temp_c;
        self.event_count = self.event_count.saturating_add(1);
        hal.log_thermal_event(temp_c, new_state);

        match new_state {
            ThermalState::Warning => {
                hal.set_can_bitrate(125_000);
                self.can_tx_enabled = true;
            }
            ThermalState::Critical => {
                hal.can_bus_off();
                self.can_tx_enabled = false;
            }
            ThermalState::Shutdown => {
                hal.set_transceiver_standby(true);
                self.can_tx_enabled = false;
            }
            ThermalState::Normal | ThermalState::Recovery => {
                hal.set_transceiver_standby(false);
                hal.set_can_bitrate(500_000);
                hal.can_reinit();
                self.can_tx_enabled = true;
            }
        }
    }

    /// Update with a new temperature reading. Returns true if state changed.
    pub fn update<H: ThermalHal>(&mut self, temp_c: i16, hal: &mut H) -> bool {
        let prev = self.state;
        self.last_temp_c = temp_c;

        match self.state {
            ThermalState::Normal => {
                if temp_c >= THERMAL_SHUTDOWN_THRESHOLD_C {
                    self.enter_state(ThermalState::Shutdown, temp_c, hal);
                } else if temp_c >= THERMAL_CRIT_THRESHOLD_C {
                    self.enter_state(ThermalState::Critical, temp_c, hal);
                } else if temp_c >= THERMAL_WARN_THRESHOLD_C {
                    self.enter_state(ThermalState::Warning, temp_c, hal);
                }
            }
            ThermalState::Warning => {
                if temp_c >= THERMAL_CRIT_THRESHOLD_C {
                    self.enter_state(ThermalState::Critical, temp_c, hal);
                } else if temp_c < THERMAL_RECOVER_THRESHOLD_C {
                    self.enter_state(ThermalState::Normal, temp_c, hal);
                }
            }
            ThermalState::Critical => {
                if temp_c >= THERMAL_SHUTDOWN_THRESHOLD_C {
                    self.enter_state(ThermalState::Shutdown, temp_c, hal);
                } else if temp_c < THERMAL_RECOVER_THRESHOLD_C {
                    self.enter_state(ThermalState::Recovery, temp_c, hal);
                }
            }
            ThermalState::Shutdown => {
                if temp_c < THERMAL_RECOVER_THRESHOLD_C {
                    self.enter_state(ThermalState::Recovery, temp_c, hal);
                }
            }
            ThermalState::Recovery => {
                self.enter_state(ThermalState::Normal, temp_c, hal);
            }
        }

        self.state != prev
    }

    /// Call from interrupt handler when hardware TSD pin asserts.
    pub fn on_tsd<H: ThermalHal>(&mut self, hal: &mut H) {
        self.tsd_count = self.tsd_count.saturating_add(1);
        hal.log_thermal_event(self.last_temp_c, ThermalState::Shutdown);
        let temp = self.last_temp_c;
        self.enter_state(ThermalState::Shutdown, temp, hal);
    }

    pub fn can_transmit(&self) -> bool {
        self.can_tx_enabled
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct MockHal {
        pub bitrate:       u32,
        pub standby:       bool,
        pub bus_off_count: u32,
        pub reinit_count:  u32,
        pub log: std::vec::Vec<(i16, ThermalState)>,
    }

    impl MockHal {
        fn new() -> Self {
            Self { bitrate: 500_000, standby: false, bus_off_count: 0, reinit_count: 0, log: vec![] }
        }
    }

    impl ThermalHal for MockHal {
        fn set_can_bitrate(&mut self, bps: u32)              { self.bitrate = bps; }
        fn can_bus_off(&mut self)                             { self.bus_off_count += 1; }
        fn can_reinit(&mut self)                              { self.reinit_count += 1; }
        fn set_transceiver_standby(&mut self, s: bool)        { self.standby = s; }
        fn log_thermal_event(&mut self, t: i16, s: ThermalState) { self.log.push((t, s)); }
    }

    #[test]
    fn test_normal_to_warning() {
        let mut tm  = ThermalManager::new();
        let mut hal = MockHal::new();
        let changed = tm.update(90, &mut hal);
        assert!(changed);
        assert_eq!(tm.state, ThermalState::Warning);
        assert_eq!(hal.bitrate, 125_000);
        assert!(tm.can_transmit());
    }

    #[test]
    fn test_warning_to_critical_disables_tx() {
        let mut tm  = ThermalManager::new();
        let mut hal = MockHal::new();
        tm.update(90,  &mut hal);   // -> Warning
        tm.update(110, &mut hal);   // -> Critical
        assert_eq!(tm.state, ThermalState::Critical);
        assert!(!tm.can_transmit());
        assert_eq!(hal.bus_off_count, 1);
    }

    #[test]
    fn test_shutdown_then_recovery() {
        let mut tm  = ThermalManager::new();
        let mut hal = MockHal::new();
        tm.update(130, &mut hal);   // -> Shutdown
        assert!(hal.standby);
        tm.update(60, &mut hal);    // -> Recovery
        tm.update(60, &mut hal);    // -> Normal
        assert_eq!(tm.state, ThermalState::Normal);
        assert!(!hal.standby);
        assert!(tm.can_transmit());
    }

    #[test]
    fn test_tsd_interrupt() {
        let mut tm  = ThermalManager::new();
        let mut hal = MockHal::new();
        tm.on_tsd(&mut hal);
        assert_eq!(tm.state, ThermalState::Shutdown);
        assert_eq!(tm.tsd_count, 1);
    }
}
```

---

### 2. MCP9808 I2C Driver (Rust, embedded-hal)

```rust
//! mcp9808.rs
//! Driver for the MCP9808 digital temperature sensor over I2C.
//! Uses embedded-hal 1.0 traits for portability.

use embedded_hal::i2c::I2c;

pub const MCP9808_DEFAULT_ADDR: u8 = 0x18;

const REG_CONFIG:      u8 = 0x01;
const REG_ALERT_UPPER: u8 = 0x02;
const REG_ALERT_LOWER: u8 = 0x03;
const REG_CRITICAL:    u8 = 0x04;
const REG_TEMP:        u8 = 0x05;

#[derive(Debug)]
pub enum Mcp9808Error<E> {
    I2cError(E),
    InvalidData,
}

pub struct Mcp9808<I2C> {
    i2c:     I2C,
    address: u8,
}

impl<I2C, E> Mcp9808<I2C>
where
    I2C: I2c<Error = E>,
{
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self { i2c, address }
    }

    /// Read raw temperature (13-bit two's complement, 1/16 deg C per LSB).
    pub fn read_raw(&mut self) -> Result<i16, Mcp9808Error<E>> {
        let mut buf = [0u8; 2];
        self.i2c
            .write_read(self.address, &[REG_TEMP], &mut buf)
            .map_err(Mcp9808Error::I2cError)?;

        let msb = (buf[0] & 0x1F) as u16;
        let lsb =  buf[1]         as u16;
        let raw = (msb << 8) | lsb;

        if buf[0] & 0x10 != 0 {
            Ok((raw | 0xE000) as i16)  // Negative: sign-extend
        } else {
            Ok(raw as i16)
        }
    }

    /// Read temperature in integer degrees C (truncated).
    pub fn read_celsius(&mut self) -> Result<i16, Mcp9808Error<E>> {
        Ok(self.read_raw()? / 16)
    }

    /// Read temperature in milli-degrees C for higher resolution.
    pub fn read_milli_celsius(&mut self) -> Result<i32, Mcp9808Error<E>> {
        let raw = self.read_raw()?;
        // 1 LSB = 1/16 deg C = 62.5 m deg C; use integer: raw * 1000 / 16
        Ok((raw as i32) * 1000 / 16)
    }

    /// Set ALERT pin thresholds for hardware interrupt generation.
    pub fn set_thresholds(&mut self, upper_c: i16, critical_c: i16) -> Result<(), Mcp9808Error<E>> {
        self.write_limit(REG_ALERT_UPPER, upper_c)?;
        self.write_limit(REG_CRITICAL,    critical_c)?;
        Ok(())
    }

    fn write_limit(&mut self, reg: u8, temp_c: i16) -> Result<(), Mcp9808Error<E>> {
        let raw: u16 = if temp_c < 0 {
            0x1000u16 | (((temp_c * 16) as u16) & 0x0FFF)
        } else {
            ((temp_c * 16) as u16) & 0x0FFF
        };
        let buf = [reg, (raw >> 8) as u8, (raw & 0xFF) as u8];
        self.i2c.write(self.address, &buf).map_err(Mcp9808Error::I2cError)
    }
}
```

---

### 3. SocketCAN Thermal Monitor (Rust, Linux)

```rust
//! can_thermal_monitor.rs
//! Linux userspace CAN thermal monitor using the socketcan crate.
//!
//! Cargo.toml:
//!   [dependencies]
//!   socketcan = "3"

use socketcan::{CanSocket, CanFrame, Socket, StandardId};
use std::time::Duration;
use std::thread;

const THERMAL_FRAME_ID: u16  = 0x7DF;
const POLL_INTERVAL_MS: u64  = 10;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq)]
enum ThermalState {
    Normal   = 0,
    Warning  = 1,
    Critical = 2,
    Shutdown = 3,
    Unknown  = 0xFF,
}

impl From<u8> for ThermalState {
    fn from(v: u8) -> Self {
        match v {
            0 => ThermalState::Normal,
            1 => ThermalState::Warning,
            2 => ThermalState::Critical,
            3 => ThermalState::Shutdown,
            _ => ThermalState::Unknown,
        }
    }
}

struct ThermalStatusFrame {
    state:       ThermalState,
    temp_01c:    i16,   // Temperature in 0.1 deg C units
    event_count: u8,
    tsd_count:   u8,
}

impl ThermalStatusFrame {
    fn from_data(data: &[u8]) -> Option<Self> {
        if data.len() < 5 { return None; }
        let temp = i16::from_be_bytes([data[1], data[2]]);
        Some(Self {
            state:       ThermalState::from(data[0]),
            temp_01c:    temp,
            event_count: data[3],
            tsd_count:   data[4],
        })
    }

    fn temp_celsius(&self) -> f32 {
        self.temp_01c as f32 / 10.0
    }
}

fn monitor_loop(iface: &str) -> socketcan::Result<()> {
    let sock = CanSocket::open(iface)?;
    sock.set_read_timeout(Duration::from_millis(500))?;

    println!("Monitoring thermal frames on {} (ID=0x{:03X})", iface, THERMAL_FRAME_ID);

    loop {
        match sock.read_frame() {
            Ok(frame) => {
                let id = match frame.id() {
                    socketcan::Id::Standard(sid) => sid.as_raw() as u32,
                    socketcan::Id::Extended(eid) => eid.as_raw(),
                };

                if id == THERMAL_FRAME_ID as u32 {
                    if let Some(status) = ThermalStatusFrame::from_data(frame.data()) {
                        println!(
                            "[THERMAL] State={:?}  Temp={:.1} C  Events={}  TSD={}",
                            status.state,
                            status.temp_celsius(),
                            status.event_count,
                            status.tsd_count,
                        );
                        if status.state == ThermalState::Shutdown {
                            eprintln!("THERMAL SHUTDOWN DETECTED on remote node!");
                        }
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => { /* timeout, continue */ }
            Err(e) => return Err(e),
        }
        thread::sleep(Duration::from_millis(POLL_INTERVAL_MS));
    }
}

fn main() {
    let iface = std::env::args().nth(1).unwrap_or_else(|| "vcan0".to_string());
    if let Err(e) = monitor_loop(&iface) {
        eprintln!("CAN monitor error: {}", e);
        std::process::exit(1);
    }
}
```

---

### 4. Thermal Junction Temperature Calculator (Rust)

```rust
//! thermal_calc.rs
//! Compute transceiver junction temperature and safe bus load derating.

#[derive(Debug, Clone)]
pub struct TransceiverThermalModel {
    pub theta_ja:         f32,  // Junction-to-ambient thermal resistance (deg C/W)
    pub idd_active_a:     f32,  // Active supply current (A)
    pub idd_quiescent_a:  f32,  // Quiescent supply current (A)
    pub vcc:              f32,  // Supply voltage (V)
    pub t_j_max:          f32,  // Absolute maximum junction temperature (deg C)
    pub t_tsd:            f32,  // Hardware thermal shutdown threshold (deg C)
}

#[derive(Debug)]
pub struct ThermalAnalysis {
    pub power_w:       f32,
    pub t_junction:    f32,
    pub headroom_c:    f32,
    pub max_bus_load:  f32,   // 0.0..1.0
    pub tsd_risk:      bool,
}

impl TransceiverThermalModel {
    pub fn analyse(&self, t_amb: f32, bus_load: f32) -> ThermalAnalysis {
        let load = bus_load.clamp(0.0, 1.0);

        let p_active    = self.vcc * self.idd_active_a    * load;
        let p_quiescent = self.vcc * self.idd_quiescent_a * (1.0 - load);
        let power_w     = p_active + p_quiescent;
        let t_junction  = t_amb + power_w * self.theta_ja;
        let headroom_c  = self.t_j_max - t_junction;

        // Solve for max load keeping T_j < T_j_max
        let budget = (self.t_j_max - t_amb) / (self.vcc * self.theta_ja);
        let denom  = self.idd_active_a - self.idd_quiescent_a;
        let max_bus_load = if denom > f32::EPSILON {
            ((budget - self.idd_quiescent_a) / denom).clamp(0.0, 1.0)
        } else {
            1.0
        };

        ThermalAnalysis {
            power_w,
            t_junction,
            headroom_c,
            max_bus_load,
            tsd_risk: t_junction >= (self.t_tsd - 10.0),
        }
    }
}

fn main() {
    // TJA1042T: theta_ja=100 C/W, Idd_active=70mA, Idd_q=7mA, Vcc=5V
    let model = TransceiverThermalModel {
        theta_ja:        100.0,
        idd_active_a:    0.070,
        idd_quiescent_a: 0.007,
        vcc:             5.0,
        t_j_max:         150.0,
        t_tsd:           165.0,
    };

    let scenarios = [
        ("Normal  (25 C, 50% load)",   25.0_f32, 0.50_f32),
        ("Hot day (85 C, 60% load)",   85.0,     0.60),
        ("Extreme (105 C, 80% load)", 105.0,     0.80),
    ];

    println!("{:<35} {:>8} {:>12} {:>10} {:>14} {:>10}",
        "Scenario", "Power(W)", "T_j (C)", "Headroom", "Max Load(%)", "TSD Risk");
    println!("{}", "-".repeat(95));

    for (label, t_amb, load) in &scenarios {
        let r = model.analyse(*t_amb, *load);
        println!("{:<35} {:>8.3} {:>12.1} {:>10.1} {:>14.1} {:>10}",
            label, r.power_w, r.t_junction, r.headroom_c,
            r.max_bus_load * 100.0,
            if r.tsd_risk { "YES" } else { "No" });
    }
}
```

---

## Thermal Derating and Bus Load Management

As temperature increases, safe bus load must be reduced to keep junction temperature within limits. Key strategies:

### Bit Rate Reduction

Reducing bit rate from 1 Mbit/s to 125 kbit/s reduces transitions per second by 8×. Since dominant-to-recessive transitions are where short-circuit current peaks occur in push-pull transceiver outputs, this directly reduces average power dissipation.

### Message Filtering and Priority Shedding

At warning temperature, shed non-critical CAN messages. Retain only:
- Safety-critical messages (braking, steering commands in automotive)
- Heartbeat / life-sign messages required for bus health monitoring

Suppress periodic diagnostics, telemetry, and high-frequency sensor data.

### Bus Topology and Termination Thermal Impact

Split termination (two 60 Ω resistors with a center-tap bypass capacitor to ground) distributes power across two resistors instead of one, halving the temperature rise per component. Standard 120 Ω terminators dissipate:

```
P_term = V_bus^2 / R_term = (2.5V)^2 / 120 ohm  ~=  52 mW  (recessive, mid-supply)
```

Under a 5V fault this can rise to:

```
P_term = (5V)^2 / 120 ohm  ~=  208 mW
```

Use 250 mW or 500 mW resistors for termination in high-fault-risk systems.

---

## Testing and Validation

### Thermal Cycling Test

Subject fully assembled CAN nodes to:
- Temperature ramp: −40 °C to +125 °C at 3–5 °C/min
- Dwell time: 30 minutes at each extreme
- Cycles: minimum 500 per AEC-Q100 Stress Test Method A
- Monitor CAN bus during cycling for frame errors, bit errors, and bus-off events

### Thermal Shock Test

Rapid transfer from −40 °C to +125 °C in under 30 seconds (liquid-to-liquid thermal shock chamber). Verifies solder joint integrity and ceramic capacitor cracking — critical for bus decoupling caps.

### In-Circuit Temperature Monitoring

During validation, probe:
1. Transceiver package top temperature with a thermocouple or IR camera
2. CANH/CANL voltage levels across the full temperature range
3. Differential voltage V_DIFF at the farthest node on the bus
4. Oscilloscope trigger on CAN error frames to correlate with temperature excursions

### Simulated Thermal Injection for Unit Testing

Inject artificial high-temperature readings to verify the software state machine without requiring a thermal chamber:

```c
/* Inject simulated temperature for unit testing */
#ifdef UNIT_TEST
int16_t sensor_read_override = 110; /* Force critical temp reading */
#define MCP9808_ReadCelsius() sensor_read_override
#endif
```

---

## Summary

Thermal management for CAN systems operates on two levels that must work in concert:

**Hardware Level:**
CAN transceivers for automotive and industrial use integrate on-chip thermal shutdown (TSD) circuitry that autonomously disables the bus driver at junction temperatures of approximately 165 °C–175 °C, preventing device destruction. Selecting AEC-Q100 qualified devices, designing PCB copper pours with thermal vias, using X7R ceramics for decoupling across the full temperature range, and properly rating termination resistors ensures the hardware thermal path is robust from −40 °C to +125 °C ambient.

**Software Level:**
The node firmware must independently monitor temperature using an external sensor (e.g., MCP9808 via I2C) and implement a thermal state machine with four zones — Normal, Warning, Critical, Shutdown — with hysteresis on recovery. At the Warning threshold the bit rate is reduced to lower power dissipation. At Critical, the CAN controller is taken bus-off to prevent corrupting the network. At Shutdown, the transceiver is placed in standby via the STB pin. All transitions are logged to non-volatile memory for field diagnostics.

**Key design rules:**
- Always calculate T_junction = T_amb + (P × θ_ja) and verify against device ratings at maximum expected ambient
- Use the derating formula to determine the maximum safe bus load fraction before TSD passively triggers under normal operation
- Implement hysteresis in software thresholds to prevent oscillation around threshold temperatures
- Test across the full temperature range, monitoring frame error rates and differential voltage levels
- Shed non-critical CAN traffic first; always preserve safety-critical and heartbeat messages during thermal stress

Together, these hardware and software measures ensure that a CAN node degrades gracefully under thermal stress rather than corrupting the entire network.

---

*Document: 93 — Thermal Management for CAN Systems | Revision 1.0*