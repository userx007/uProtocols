# 91. Automotive SPI Requirements

**AEC-Q100** — Temperature grades (0–3), the seven test groups most relevant to SPI (HTOL, ESD, latch-up, HAST), and what device qualification means for signal integrity over the 15-year automotive lifetime.

**ISO 26262** — ASIL levels and their diagnostic coverage targets, and the five mandatory E2E safety mechanisms for SPI: CRC, rolling counter, data ID, timeout supervision, and alive counter.

**C/C++ examples:**
- CRC-8/AUTOSAR polynomial 0x2F (lookup-table, compile-time compatible)
- E2E Profile 2 sender (16-bit SPI frame with counter + CRC)
- E2E Profile 2 receiver with full status tracking (repeated, wrong sequence, timeout, CRC error)
- Full polling driver with watchdog servicing, hold-last-valid logic, and safe-state escalation
- Startup loopback self-test with four stuck-at and alternating-bit patterns

**Rust examples:**
- `const fn` compile-time CRC-8 table generation (no_std)
- Type-safe E2E sender/receiver using embedded-hal traits
- State-machine-based `AutomotiveSpiSensor` driver with diagnostic counters
- Loopback self-test with structured `SelfTestError` return type

**Testing and validation** covers HiL fault injection, FMEDA requirements, MISRA C:2012 compliance, and the Rust toolchain qualification path (Ferrocene).

## Meeting AEC-Q100 and ISO 26262 Requirements for Automotive SPI Systems

---

## Table of Contents

1. [Introduction](#introduction)
2. [AEC-Q100 Overview and SPI Implications](#aec-q100-overview-and-spi-implications)
3. [ISO 26262 Functional Safety and SPI](#iso-26262-functional-safety-and-spi)
4. [Automotive SPI Hardware Requirements](#automotive-spi-hardware-requirements)
5. [Communication Integrity and Error Detection](#communication-integrity-and-error-detection)
6. [Fault Detection and Diagnostic Coverage](#fault-detection-and-diagnostic-coverage)
7. [Timing, Synchronisation, and Watchdog](#timing-synchronisation-and-watchdog)
8. [Safe State Handling and Graceful Degradation](#safe-state-handling-and-graceful-degradation)
9. [C/C++ Implementation Examples](#cc-implementation-examples)
10. [Rust Implementation Examples](#rust-implementation-examples)
11. [Testing and Validation Strategies](#testing-and-validation-strategies)
12. [Summary](#summary)

---

## Introduction

The Serial Peripheral Interface (SPI) protocol, originally developed by Motorola in the 1980s, is now a cornerstone communication bus in modern automotive Electronic Control Units (ECUs). It connects microcontrollers to sensors, actuators, EEPROMs, ADCs, power management ICs, and safety monitors operating at speeds from a few MHz up to 80 MHz or beyond.

In safety-critical automotive environments, however, the basic SPI protocol — a synchronous, full-duplex, four-wire serial interface — offers no built-in error detection, no addressing scheme, and no acknowledgment mechanism. This is entirely adequate for consumer electronics, but it falls critically short in automotive systems where a missed sensor value or a corrupted command byte can have life-threatening consequences.

Automotive SPI implementations must therefore be hardened at every layer: the physical silicon must meet the stress qualifications of **AEC-Q100**, and the software stack governing each transaction must conform to the functional safety requirements of **ISO 26262**.

This document describes both standards as they apply to SPI, the engineering practices and software patterns needed to satisfy them, and concrete code examples in **C/C++** and **Rust**.

---

## AEC-Q100 Overview and SPI Implications

### What Is AEC-Q100?

AEC-Q100 is a stress qualification standard published by the Automotive Electronics Council (AEC). It defines the minimum stress test requirements for integrated circuits intended for use in automotive applications. The standard is grouped into several test grades based on the maximum ambient operating temperature:

| Grade | Operating Temperature Range |
|-------|-----------------------------|
| Grade 0 | −40 °C to +150 °C |
| Grade 1 | −40 °C to +125 °C |
| Grade 2 | −40 °C to +105 °C |
| Grade 3 | −40 °C to +85 °C |

Most automotive ECUs (powertrain, ADAS, body control) target **Grade 1**, while under-hood and transmission applications often require **Grade 0**.

### Key AEC-Q100 Test Groups Relevant to SPI

AEC-Q100 covers seven test groups. Those most relevant to SPI peripheral ICs and microcontrollers are:

**Group A — Accelerated Environmental Stress Tests**
- HTOL (High Temperature Operating Life): Devices biased under worst-case conditions at elevated temperature for 1000 hours minimum. SPI bus transactions must remain reliable across this duration.
- Temperature Cycling: Repeated thermal excursions expose solder joints and bond wire fatigue. SPI signal integrity must be maintained.
- HAST (Highly Accelerated Stress Test): Humidity + temperature + bias; checks for corrosion on SPI I/O pads.

**Group B — Accelerated Lifetime Simulation Tests**
- Hot Carrier Injection (HCI) and NBTI: Degrade MOSFET threshold voltage over time, which can affect SPI I/O drive strength and input thresholds at end-of-life.
- TDDB (Time-Dependent Dielectric Breakdown): Relevant to gate oxide integrity in high-frequency SPI transceivers.

**Group C — Package Assembly Integrity Tests**
- Wire bond pull, die shear, moisture sensitivity. SPI bond wires connect pads to lead frames.

**Group E — Electrical Verification Tests**
- ESD (HBM, CDM, MM): SPI lines routed to connectors must withstand electrostatic discharge events. AEC-Q100 Grade 1 typically mandates ≥2 kV HBM per pin.
- Latch-up immunity: A latch-up event on a SPI line can permanently damage the device or cause uncontrolled outputs.

### What AEC-Q100 Means for SPI Design

When selecting SPI peripheral ICs (sensors, DACs, EEPROMs), the designer must:

1. Verify that the part carries an AEC-Q100 qualification report for the required grade.
2. Check that the part's electrical characteristics — VIH, VIL, IOH, IOL, CLOAD — are characterised across the full automotive temperature range, not just at 25 °C.
3. Confirm ESD ratings meet system-level requirements. Connector-accessible SPI lines may need additional TVS protection.
4. Account for parameter drift over life. An ADC whose VREF shifts 1% after 1000 hours HTOL may degrade SPI measurement accuracy in safety-relevant paths.

---

## ISO 26262 Functional Safety and SPI

### Standard Structure

ISO 26262 "Road Vehicles — Functional Safety" is derived from IEC 61508. It covers the entire safety lifecycle of E/E systems in passenger cars (and since the 2018 edition, also motorcycles, trucks, and buses). The standard is organised into parts:

- **Part 3**: Concept phase — hazard analysis, ASIL determination
- **Part 4**: Product development at system level
- **Part 5**: Product development at hardware level
- **Part 6**: Product development at software level
- **Part 9**: Automotive Safety Integrity Level (ASIL)-oriented and safety-oriented analyses

### Automotive Safety Integrity Levels (ASIL)

ISO 26262 assigns an ASIL to each safety goal based on three factors: **Severity** (S0–S3), **Exposure** (E0–E4), and **Controllability** (C0–C3). The resulting ASIL ranges from QM (Quality Management, no safety requirement) through ASIL A, B, C, to ASIL D (most stringent).

| ASIL | Typical Diagnostic Coverage | Random Hardware Failure Metric |
|------|-----------------------------|-------------------------------|
| A | 60–90% | < 10⁻⁷ per hour |
| B | 60–90% | < 10⁻⁷ per hour |
| C | 90–99% | < 10⁻⁸ per hour |
| D | 99%+ | < 10⁻⁸ per hour |

A SPI bus carrying safety-relevant data — for example, a steering angle sensor in an EPS system or a pressure sensor in a brake-by-wire application — must meet the ASIL assigned to that safety function, which is commonly ASIL C or ASIL D.

### Safety Mechanisms Required on SPI Paths

ISO 26262 Part 6 (software) requires that "E2E" (End-to-End) communication safety measures are applied to all safety-relevant inter-component communications. ISO 26262 references the AUTOSAR E2E protection library as a recommended implementation approach. For SPI, this translates into requiring:

1. **CRC protection** on every safety-relevant message
2. **Sequence counter** (rolling counter) to detect message loss, repetition, and insertion
3. **Data ID** (message identifier) to detect routing or substitution errors
4. **Timeout supervision** to detect communication loss
5. **Alive counter** to detect frozen or stuck sender
6. **Status/error flags** in the SPI response frame (leveraging full-duplex simultaneous transmit/receive)

---

## Automotive SPI Hardware Requirements

### Electrical and Timing Margins

Unlike a best-case bench setup, automotive SPI must function correctly across:

- **Temperature**: −40 °C to +125 °C (Grade 1)
- **Supply voltage variation**: Nominal 5 V or 3.3 V ±10%, transient dips to 3.5 V (load dump events can spike to 40 V on 12 V bus, handled by external clamping)
- **Clock frequencies**: 1 MHz to 20 MHz for safety-critical paths; higher frequencies require tighter PCB routing and controlled impedance traces
- **Capacitive loading**: Harness and multi-drop configurations add capacitance; each picofarad reduces signal edge rates

### SPI Mode Selection

Automotive peripheral ICs most commonly support **SPI Mode 1 (CPOL=0, CPHA=1)** or **SPI Mode 3 (CPOL=1, CPHA=1)**. Mixing modes on a shared bus requires careful CS management and mode switching delays.

### Hardware Safety Features in Automotive-Grade SPI Controllers

Modern automotive microcontrollers (Infineon AURIX, NXP S32K, Renesas RH850) include dedicated SPI hardware features for safety:

- **Parity checking** on received frames (detects single-bit errors)
- **Frame length checking** (detects truncated or extended frames)
- **CS timeout detection** (detects stuck-low CS pin)
- **SPI FIFO with DMA** (frees CPU while maintaining deterministic latency)
- **Loopback mode** (connects MOSI to MISO internally for self-test)

---

## Communication Integrity and Error Detection

### AUTOSAR E2E Profile 2 for SPI

AUTOSAR E2E Profile 2 is widely used for SPI sensor data. It adds an 8-bit CRC, an 8-bit alive counter, and a 16-bit data ID. The structure of a 16-bit SPI frame with E2E protection is:

```
Bits [15:8]  — Protected data (e.g., sensor value)
Bits [7:4]   — Alive counter (0–15, wraps around)
Bits [3:0]   — CRC-4 over data + counter + data ID
```

For longer frames (32-bit or 64-bit), Profile 4 or 5 uses CRC-32 (0xF4ACFB13 polynomial, as used by Ethernet).

### CRC Polynomial Selection

| Profile | CRC Width | Polynomial | Hamming Distance |
|---------|-----------|------------|-----------------|
| E2E P01 | 8-bit | 0x1D (SAE J1850) | HD=4 up to 119 bits |
| E2E P02 | 8-bit | 0x2F (CRC-8/AUTOSAR) | HD=5 up to 119 bits |
| E2E P04 | 16-bit | 0x1021 (CRC-CCITT) | HD=6 up to 4095 bits |
| E2E P05 | 32-bit | 0xF4ACFB13 | HD=6 up to 4095 bytes |

---

## Fault Detection and Diagnostic Coverage

ISO 26262 Part 5 defines diagnostic coverage (DC) as the percentage of random hardware failures detected or controlled by safety mechanisms. For SPI paths to achieve ASIL D, DC must reach 99%.

The following table maps failure modes to diagnostic measures:

| Failure Mode | Example | Diagnostic Measure | Typical DC |
|---|---|---|---|
| Single-bit flip in data | MISO bit 3 inverted | CRC check | 99%+ |
| Stuck-at MISO HIGH | Sensor IC powered off | Timeout + alive counter | 97% |
| Wrong frame delivered | Multi-slave CS glitch | Data ID check | 98% |
| Repeated old value | Clock stretch fault | Sequence counter | 99% |
| Missing frame | DMA underrun | Timeout supervision | 99% |
| Short-circuit MOSI–MISO | PCB damage | Loopback test at startup | 90% |
| SPI clock stopped | MCU clock fault | Independent watchdog | 95% |

---

## Timing, Synchronisation, and Watchdog

### Deterministic SPI Scheduling

Safety-relevant SPI sensors must be polled on a deterministic schedule. In an AUTOSAR-based system, the SPI manager (SPI Handler/Driver, AUTOSAR SWC) runs its jobs under the OS scheduler at fixed cyclic periods, typically 1 ms, 2 ms, or 5 ms depending on the sensor's update rate.

Latency from sensor measurement to ECU consumption must be bounded and documented in the FMEA. A typical budget for an ASIL D steering angle sensor is:

- Sensor sampling: 0.5 ms
- SPI transfer: 0.1 ms (at 10 MHz for 16-bit frame)
- DMA interrupt + SW processing: 0.1 ms
- **Total worst case: 0.7 ms** (must be less than the control loop's tolerance)

### Watchdog Integration

An independent window watchdog must supervise the SPI communication task. If the task overruns or is starved, the watchdog detects this and triggers a safe state. On Infineon AURIX, the Safety Watchdog (SMU + STM) provides this. On NXP S32K, the WDOG peripheral with early interrupt serves the same role.

---

## Safe State Handling and Graceful Degradation

When an E2E error is detected on a safety-relevant SPI channel, the response must be defined in the safety concept. Common strategies are:

- **Hold last valid value** with a maximum hold-time (e.g., 20 ms), then enter reduced functionality
- **Switch to a redundant sensor** (if dual-channel architecture is employed, e.g., ASIL D decomposition into two ASIL B paths)
- **Transition to safe state**: e.g., apply maximum braking in ABS, limit torque in EPS
- **DTC storage and fault lamp**: Log the fault in non-volatile memory for workshop diagnostics (UDS service 0x19)

---

## C/C++ Implementation Examples

### 1. CRC-8 Computation (AUTOSAR E2E Profile 2 Polynomial 0x2F)

```c
/**
 * @file spi_crc8_autosar.c
 * @brief CRC-8 using AUTOSAR E2E Profile 2 polynomial (0x2F)
 *        HD=5, suitable for ASIL D protection of SPI frames up to 119 bits.
 */

#include <stdint.h>
#include <stddef.h>

/* Pre-computed lookup table for CRC-8/AUTOSAR polynomial 0x2F */
static const uint8_t crc8_table[256] = {
    0x00U, 0x2FU, 0x5EU, 0x71U, 0xBCU, 0x93U, 0xE2U, 0xCDU,
    0x57U, 0x78U, 0x09U, 0x26U, 0xEBU, 0xC4U, 0xB5U, 0x9AU,
    0xAEU, 0x81U, 0xF0U, 0xDFU, 0x12U, 0x3DU, 0x4CU, 0x63U,
    0xF9U, 0xD6U, 0xA7U, 0x88U, 0x45U, 0x6AU, 0x1BU, 0x34U,
    /* ... (full 256-entry table omitted for brevity, generated by
           polynomial 0x2F with reflected input and output = false,
           initial value = 0xFF, final XOR = 0xFF) ... */
    /* In production, generate with a tool like pycrc or CRC RevEng */
};

/**
 * @brief Compute CRC-8/AUTOSAR over a byte array.
 *
 * @param data    Pointer to the data buffer.
 * @param length  Number of bytes to process.
 * @return        8-bit CRC result.
 */
uint8_t SPI_CRC8_Autosar(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFFU; /* Initial value per AUTOSAR E2E Profile 2 */

    for (size_t i = 0U; i < length; i++) {
        crc = crc8_table[crc ^ data[i]];
    }

    return (crc ^ 0xFFU); /* Final XOR per AUTOSAR E2E Profile 2 */
}
```

### 2. AUTOSAR E2E Profile 2 — Sender Side (16-bit SPI Frame)

```c
/**
 * @file spi_e2e_sender.c
 * @brief AUTOSAR E2E Profile 2 frame protection for a 16-bit SPI word.
 *
 * Frame layout (16 bits total):
 *   Bits [15:8]  = Data byte (sensor value)
 *   Bits [7:4]   = Counter (4-bit rolling, 0–15)
 *   Bits [3:0]   = CRC-4 over {DataID, Data, Counter}
 *
 * For ASIL D systems, extend to 32-bit frame with 8-bit CRC.
 */

#include <stdint.h>
#include <stdbool.h>

#define E2E_DATA_ID     0xA5U   /**< Unique per-signal identifier */
#define E2E_COUNTER_MAX 15U

static uint8_t s_tx_counter = 0U; /* Sender rolling counter */

/** @brief Build a 4-bit CRC from a nibble-based lookup (simplified). */
static uint8_t compute_crc4(uint8_t data_id, uint8_t data, uint8_t counter)
{
    /* In production, use a proper CRC-4/ITU implementation.
       This illustrates the data composition. */
    uint8_t buf[3] = { data_id, data, counter & 0x0FU };
    uint8_t crc = 0U;
    for (int i = 0; i < 3; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80U) ? ((crc << 1U) ^ 0x0CU) : (crc << 1U);
        }
    }
    return (crc >> 4U) & 0x0FU;
}

/**
 * @brief Protect a data byte using E2E Profile 2 before SPI transmission.
 *
 * @param data  The raw sensor byte to protect.
 * @return      16-bit word ready for SPI transmission.
 */
uint16_t E2E_P02_Protect(uint8_t data)
{
    uint8_t crc = compute_crc4(E2E_DATA_ID, data, s_tx_counter);

    uint16_t frame = ((uint16_t)data << 8U) |
                     ((uint16_t)(s_tx_counter & 0x0FU) << 4U) |
                     ((uint16_t)(crc & 0x0FU));

    /* Advance rolling counter */
    s_tx_counter = (s_tx_counter >= E2E_COUNTER_MAX) ? 0U : (s_tx_counter + 1U);

    return frame;
}
```

### 3. AUTOSAR E2E Profile 2 — Receiver Side with Status Tracking

```c
/**
 * @file spi_e2e_receiver.c
 * @brief AUTOSAR E2E Profile 2 frame check on the receiver side.
 *
 * Tracks: CRC errors, counter jumps, repeated frames, timeout.
 * Returns E2E check status compatible with AUTOSAR E2E library return codes.
 */

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    E2E_STATUS_OK             = 0x00U, /**< Frame valid, counter incremented */
    E2E_STATUS_REPEATED       = 0x01U, /**< Counter unchanged — repeated frame */
    E2E_STATUS_WRONG_SEQUENCE = 0x02U, /**< Counter jumped by more than 1 */
    E2E_STATUS_CRC_ERROR      = 0x04U, /**< CRC mismatch */
    E2E_STATUS_TIMEOUT        = 0x08U, /**< No new frame within timeout window */
    E2E_STATUS_NOT_AVAILABLE  = 0x10U  /**< First call, no previous state */
} E2E_CheckStatusType;

typedef struct {
    uint8_t  last_counter;       /**< Counter value from the previous valid frame */
    bool     first_frame;        /**< True until the first valid frame is received */
    uint32_t no_new_data_cnt;    /**< Counts consecutive cycles with no new data */
    uint32_t max_no_new_data;    /**< Timeout: max cycles without a new frame     */
} E2E_ReceiverStateType;

static uint8_t compute_crc4(uint8_t data_id, uint8_t data, uint8_t counter);
/* (implementation same as sender side) */

/**
 * @brief Check an incoming 16-bit SPI frame.
 *
 * @param frame    Raw 16-bit word received via SPI.
 * @param state    Persistent receiver state (must survive between calls).
 * @param out_data Pointer where the extracted data byte is written (only if OK).
 * @return         E2E check status.
 */
E2E_CheckStatusType E2E_P02_Check(uint16_t          frame,
                                   E2E_ReceiverStateType *state,
                                   uint8_t          *out_data)
{
    uint8_t data    = (uint8_t)((frame >> 8U) & 0xFFU);
    uint8_t counter = (uint8_t)((frame >> 4U) & 0x0FU);
    uint8_t rx_crc  = (uint8_t)( frame        & 0x0FU);

    /* 1. CRC check */
    uint8_t calc_crc = compute_crc4(E2E_DATA_ID, data, counter);
    if (calc_crc != rx_crc) {
        state->no_new_data_cnt++;
        return E2E_STATUS_CRC_ERROR;
    }

    /* 2. Timeout check */
    if (state->no_new_data_cnt >= state->max_no_new_data) {
        state->no_new_data_cnt = 0U;
        return E2E_STATUS_TIMEOUT;
    }

    /* 3. First frame — no counter history yet */
    if (state->first_frame) {
        state->last_counter = counter;
        state->first_frame  = false;
        *out_data = data;
        return E2E_STATUS_NOT_AVAILABLE;
    }

    /* 4. Counter continuity check */
    uint8_t expected_counter = (state->last_counter + 1U) & 0x0FU;
    E2E_CheckStatusType status;

    if (counter == state->last_counter) {
        status = E2E_STATUS_REPEATED;
    } else if (counter == expected_counter) {
        state->last_counter    = counter;
        state->no_new_data_cnt = 0U;
        *out_data              = data;
        status                 = E2E_STATUS_OK;
    } else {
        /* Counter jumped — frames were lost */
        state->last_counter = counter;
        status              = E2E_STATUS_WRONG_SEQUENCE;
    }

    return status;
}
```

### 4. SPI Driver with Fault Injection and Watchdog Servicing (C/C++)

```cpp
/**
 * @file spi_automotive_driver.cpp
 * @brief Automotive SPI master driver with:
 *   - E2E protection
 *   - Window watchdog servicing
 *   - Safe-state transition on persistent faults
 *   - Diagnostic counter logging
 *
 * Targeting Infineon AURIX TC3xx (abstracted via HAL macros).
 */

#include <cstdint>
#include <cstring>
#include <atomic>

/* ---- Platform HAL (replace with real BSP headers) ---- */
extern void HAL_SPI_TransferBlocking(uint16_t tx, uint16_t *rx);
extern void HAL_WDG_Service(void);
extern void HAL_SafeState_Enter(const char *reason);
extern uint32_t HAL_GetTimestamp_ms(void);

/* ---- Configuration ---- */
static constexpr uint8_t  MAX_CONSECUTIVE_ERRORS = 3U;
static constexpr uint32_t SPI_TIMEOUT_MS         = 10U;  // 10 ms task period

/* ---- Diagnostic counters (accessible by DEM / UDS 0x19) ---- */
struct SpiDiagnostics {
    uint32_t crc_errors        = 0U;
    uint32_t counter_jumps     = 0U;
    uint32_t repeated_frames   = 0U;
    uint32_t timeouts          = 0U;
    uint32_t total_transfers   = 0U;
};

static SpiDiagnostics s_diag;
static uint8_t        s_consecutive_errors = 0U;
static uint8_t        s_last_valid_data    = 0U;
static uint32_t       s_last_valid_ts_ms   = 0U;

/**
 * @brief Execute one complete SPI polling cycle for a safety sensor.
 *
 * Called every 1 ms by the OS task scheduler.
 *
 * @return true if a valid measurement was obtained, false otherwise.
 */
bool SpiSensor_Poll(uint8_t *measurement_out)
{
    /* Service the window watchdog before the SPI transaction */
    HAL_WDG_Service();

    /* Build outgoing request frame (command byte in high byte) */
    uint16_t tx_frame = E2E_P02_Protect(0xABU); /* 0xAB = read-sensor command */
    uint16_t rx_frame = 0U;

    HAL_SPI_TransferBlocking(tx_frame, &rx_frame);
    s_diag.total_transfers++;

    E2E_ReceiverStateType e2e_state = {}; /* Normally persistent — simplified here */
    e2e_state.max_no_new_data = 5U;
    uint8_t data = 0U;

    E2E_CheckStatusType status = E2E_P02_Check(rx_frame, &e2e_state, &data);

    switch (status) {
        case E2E_STATUS_OK:
            s_last_valid_data   = data;
            s_last_valid_ts_ms  = HAL_GetTimestamp_ms();
            s_consecutive_errors = 0U;
            *measurement_out    = data;
            return true;

        case E2E_STATUS_REPEATED:
            s_diag.repeated_frames++;
            /* Repeated frame: use last valid, do not reset error counter */
            break;

        case E2E_STATUS_WRONG_SEQUENCE:
            s_diag.counter_jumps++;
            s_consecutive_errors++;
            break;

        case E2E_STATUS_CRC_ERROR:
            s_diag.crc_errors++;
            s_consecutive_errors++;
            break;

        case E2E_STATUS_TIMEOUT:
            s_diag.timeouts++;
            s_consecutive_errors++;
            break;

        default:
            break;
    }

    /* Check if hold-time for last valid value has been exceeded */
    uint32_t age_ms = HAL_GetTimestamp_ms() - s_last_valid_ts_ms;
    if (age_ms <= 20U) {
        /* Within hold-time: propagate last valid value */
        *measurement_out = s_last_valid_data;
    }

    /* Transition to safe state if errors persist */
    if (s_consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
        HAL_SafeState_Enter("SPI: Persistent E2E errors on steering angle sensor");
        /* Does not return */
    }

    return false;
}
```

### 5. SPI Loopback Self-Test at Startup (C)

```c
/**
 * @file spi_selftest.c
 * @brief Hardware loopback self-test executed during ECU startup.
 *
 * Connects MOSI to MISO via internal loopback (most automotive MCUs
 * support this in the SPI controller's test mode register).
 * Tests data integrity across the full byte range with a known pattern.
 * Must pass before the ECU transitions from INIT to RUN state.
 */

#include <stdint.h>
#include <stdbool.h>

extern void HAL_SPI_EnableLoopback(void);
extern void HAL_SPI_DisableLoopback(void);
extern void HAL_SPI_TransferBlocking(uint16_t tx, uint16_t *rx);

#define SELFTEST_PATTERN_COUNT  4U

static const uint16_t test_patterns[SELFTEST_PATTERN_COUNT] = {
    0x0000U,  /* All zeros */
    0xFFFFU,  /* All ones  */
    0xAAAAU,  /* Alternating 10 */
    0x5555U   /* Alternating 01 */
};

/**
 * @brief Run SPI loopback self-test.
 * @return true if all patterns passed, false on any mismatch.
 */
bool SPI_LoopbackSelfTest(void)
{
    HAL_SPI_EnableLoopback();

    bool result = true;

    for (uint8_t i = 0U; i < SELFTEST_PATTERN_COUNT; i++) {
        uint16_t rx = 0U;
        HAL_SPI_TransferBlocking(test_patterns[i], &rx);

        if (rx != test_patterns[i]) {
            result = false;
            break;
        }
    }

    HAL_SPI_DisableLoopback();
    return result;
}
```

---

## Rust Implementation Examples

Rust's ownership model, zero-cost abstractions, and `no_std` / `no_alloc` support make it well-suited for embedded automotive firmware. The `embedded-hal` crate provides a standardised SPI trait used across microcontroller families.

### 1. CRC-8 AUTOSAR in Rust (Const Generics, no_std)

```rust
//! crc8_autosar.rs
//!
//! CRC-8/AUTOSAR (polynomial 0x2F) implementation for ASIL D E2E protection.
//! Runs in no_std environments. Table is computed at compile time.

#![no_std]

/// Pre-computed CRC-8 lookup table using AUTOSAR polynomial 0x2F.
/// Generated at compile time via const fn.
const fn build_crc8_table() -> [u8; 256] {
    let mut table = [0u8; 256];
    let mut i = 0usize;
    while i < 256 {
        let mut crc = i as u8;
        let mut j = 0u8;
        while j < 8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ 0x2F;
            } else {
                crc <<= 1;
            }
            j += 1;
        }
        table[i] = crc;
        i += 1;
    }
    table
}

static CRC8_TABLE: [u8; 256] = build_crc8_table();

/// Compute CRC-8/AUTOSAR over a byte slice.
///
/// # Arguments
/// * `data` — Input bytes to protect.
///
/// # Returns
/// CRC-8 byte suitable for inclusion in an E2E-protected SPI frame.
pub fn crc8_autosar(data: &[u8]) -> u8 {
    let mut crc: u8 = 0xFF; // Initial value
    for &byte in data {
        crc = CRC8_TABLE[(crc ^ byte) as usize];
    }
    crc ^ 0xFF // Final XOR
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_known_vector() {
        // AUTOSAR E2E Profile 2 test vector: input [0x00, 0xF2, 0x01, 0x83, 0x45]
        // Expected CRC: 0xDF  (per AUTOSAR Specification of E2E Library, R21-11)
        let data = [0x00u8, 0xF2, 0x01, 0x83, 0x45];
        assert_eq!(crc8_autosar(&data), 0xDF);
    }

    #[test]
    fn test_empty_slice() {
        // CRC of empty slice = initial XOR final = 0xFF ^ 0xFF = 0x00
        assert_eq!(crc8_autosar(&[]), 0x00);
    }
}
```

### 2. E2E Profile 2 Frame Protection in Rust

```rust
//! e2e_profile2.rs
//!
//! AUTOSAR E2E Profile 2 sender and receiver for 16-bit SPI frames.
//! Designed for no_std, no_alloc embedded targets.

#![no_std]

use crate::crc8_autosar::crc8_autosar;

const E2E_DATA_ID: u8 = 0xA5;
const COUNTER_MAX: u8 = 15;

/// Sender state: maintains the rolling counter between calls.
pub struct E2ESender {
    counter: u8,
}

impl E2ESender {
    /// Create a new sender with counter initialised to 0.
    pub const fn new() -> Self {
        Self { counter: 0 }
    }

    /// Protect a data byte and build a 16-bit SPI frame.
    ///
    /// Frame layout:
    ///   Bits [15:8]  — data
    ///   Bits [7:4]   — 4-bit counter
    ///   Bits [3:0]   — 4-bit CRC (lower nibble of CRC-8)
    pub fn protect(&mut self, data: u8) -> u16 {
        let buf = [E2E_DATA_ID, data, self.counter & 0x0F];
        let crc_byte = crc8_autosar(&buf);
        let crc_nibble = crc_byte & 0x0F;

        let frame: u16 = ((data as u16) << 8)
            | ((self.counter as u16 & 0x0F) << 4)
            | (crc_nibble as u16);

        // Advance the rolling counter
        self.counter = if self.counter >= COUNTER_MAX {
            0
        } else {
            self.counter + 1
        };

        frame
    }
}

/// Status returned by the E2E receiver after checking a frame.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum E2EStatus {
    /// Frame is valid and counter incremented by exactly 1.
    Ok,
    /// CRC mismatch; frame is corrupted.
    CrcError,
    /// Counter unchanged — frame was repeated or duplicated.
    Repeated,
    /// Counter jumped by more than 1 — one or more frames were lost.
    WrongSequence,
    /// First ever frame received; no prior state to compare.
    NotAvailable,
}

/// Receiver state: remembers the last counter for continuity checks.
pub struct E2EReceiver {
    last_counter: Option<u8>,
}

impl E2EReceiver {
    pub const fn new() -> Self {
        Self { last_counter: None }
    }

    /// Check an incoming 16-bit SPI frame.
    ///
    /// Returns `(E2EStatus, Option<u8>)` where the `Option<u8>` holds the
    /// extracted data byte on `Ok` or `NotAvailable`, and `None` on errors.
    pub fn check(&mut self, frame: u16) -> (E2EStatus, Option<u8>) {
        let data       = ((frame >> 8) & 0xFF) as u8;
        let counter    = ((frame >> 4) & 0x0F) as u8;
        let rx_crc     = ( frame       & 0x0F) as u8;

        // Verify CRC
        let buf = [E2E_DATA_ID, data, counter];
        let expected_crc = crc8_autosar(&buf) & 0x0F;
        if rx_crc != expected_crc {
            return (E2EStatus::CrcError, None);
        }

        let status = match self.last_counter {
            None => {
                // First valid frame
                self.last_counter = Some(counter);
                return (E2EStatus::NotAvailable, Some(data));
            }
            Some(last) => {
                let expected = (last + 1) & 0x0F;
                if counter == last {
                    E2EStatus::Repeated
                } else if counter == expected {
                    self.last_counter = Some(counter);
                    E2EStatus::Ok
                } else {
                    self.last_counter = Some(counter);
                    E2EStatus::WrongSequence
                }
            }
        };

        let data_out = if status == E2EStatus::Ok {
            Some(data)
        } else {
            None
        };

        (status, data_out)
    }
}
```

### 3. Automotive SPI Driver with embedded-hal and Fault State Machine

```rust
//! automotive_spi.rs
//!
//! Automotive-grade SPI polling driver using embedded-hal SPI traits.
//! Implements:
//!   - E2E frame protection (Profile 2)
//!   - Consecutive error tracking → safe state
//!   - Hold-last-valid with configurable timeout
//!   - Diagnostic counters for UDS/DEM integration

#![no_std]

use embedded_hal::blocking::spi::Transfer;
use crate::e2e_profile2::{E2ESender, E2EReceiver, E2EStatus};

/// Configuration for the automotive SPI sensor driver.
pub struct SpiSensorConfig {
    /// Maximum consecutive E2E errors before entering safe state.
    pub max_consecutive_errors: u8,
    /// Maximum age (in poll cycles) of a held-last-valid value.
    pub max_hold_cycles: u32,
}

impl Default for SpiSensorConfig {
    fn default() -> Self {
        Self {
            max_consecutive_errors: 3,
            max_hold_cycles: 20, // 20 cycles × 1 ms = 20 ms hold-time
        }
    }
}

/// Diagnostic counters exposed to the application layer.
#[derive(Default, Debug)]
pub struct SpiDiagnostics {
    pub crc_errors:      u32,
    pub counter_jumps:   u32,
    pub repeated_frames: u32,
    pub total_transfers: u32,
}

/// Driver state machine.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum DriverState {
    Init,
    Running,
    HoldLastValid,
    SafeState,
}

/// Automotive SPI sensor driver.
pub struct AutomotiveSpiSensor<SPI> {
    spi:                SPI,
    sender:             E2ESender,
    receiver:           E2EReceiver,
    config:             SpiSensorConfig,
    diag:               SpiDiagnostics,
    state:              DriverState,
    last_valid_value:   Option<u8>,
    hold_cycle_counter: u32,
    consecutive_errors: u8,
}

impl<SPI, E> AutomotiveSpiSensor<SPI>
where
    SPI: Transfer<u8, Error = E>,
{
    /// Construct a new sensor driver.
    pub fn new(spi: SPI, config: SpiSensorConfig) -> Self {
        Self {
            spi,
            sender:             E2ESender::new(),
            receiver:           E2EReceiver::new(),
            config,
            diag:               SpiDiagnostics::default(),
            state:              DriverState::Init,
            last_valid_value:   None,
            hold_cycle_counter: 0,
            consecutive_errors: 0,
        }
    }

    /// Poll the sensor. Call once per scheduling period (e.g., every 1 ms).
    ///
    /// Returns `Ok(Some(value))` when a fresh valid measurement is available.
    /// Returns `Ok(None)` when holding the last valid value or still
    /// in the `NotAvailable` state on the first frame.
    /// Returns `Err(())` when the safe state has been entered.
    pub fn poll(&mut self) -> Result<Option<u8>, ()> {
        if self.state == DriverState::SafeState {
            return Err(());
        }

        // Build outgoing command frame (read-sensor opcode protected by E2E)
        let tx_frame = self.sender.protect(0xAB); // 0xAB = read command
        let tx_high  = ((tx_frame >> 8) & 0xFF) as u8;
        let tx_low   = ( tx_frame       & 0xFF) as u8;

        let mut buf = [tx_high, tx_low];
        self.spi.transfer(&mut buf).map_err(|_| ())?;

        let rx_frame: u16 = ((buf[0] as u16) << 8) | (buf[1] as u16);
        self.diag.total_transfers += 1;

        let (status, data) = self.receiver.check(rx_frame);

        match status {
            E2EStatus::Ok | E2EStatus::NotAvailable => {
                if let Some(value) = data {
                    self.last_valid_value   = Some(value);
                    self.hold_cycle_counter = 0;
                    self.consecutive_errors = 0;
                    self.state              = DriverState::Running;
                    return Ok(Some(value));
                }
            }

            E2EStatus::Repeated => {
                self.diag.repeated_frames += 1;
                // Do not advance consecutive error counter for repetitions
            }

            E2EStatus::CrcError => {
                self.diag.crc_errors    += 1;
                self.consecutive_errors += 1;
            }

            E2EStatus::WrongSequence => {
                self.diag.counter_jumps += 1;
                self.consecutive_errors += 1;
            }
        }

        // Check safe state threshold
        if self.consecutive_errors >= self.config.max_consecutive_errors {
            self.state = DriverState::SafeState;
            return Err(());
        }

        // Hold last valid value within the hold window
        if let Some(value) = self.last_valid_value {
            self.hold_cycle_counter += 1;
            if self.hold_cycle_counter <= self.config.max_hold_cycles {
                self.state = DriverState::HoldLastValid;
                return Ok(Some(value));
            } else {
                // Hold-time expired — escalate to safe state
                self.state = DriverState::SafeState;
                return Err(());
            }
        }

        Ok(None)
    }

    /// Access accumulated diagnostic counters (for DEM / UDS 0x19).
    pub fn diagnostics(&self) -> &SpiDiagnostics {
        &self.diag
    }

    /// Release ownership of the underlying SPI peripheral.
    pub fn release(self) -> SPI {
        self.spi
    }
}
```

### 4. SPI Loopback Self-Test in Rust

```rust
//! spi_selftest.rs
//!
//! Startup loopback self-test for the SPI bus.
//! Must be completed before the ECU transitions to the RUN phase.

#![no_std]

use embedded_hal::blocking::spi::Transfer;

/// Test patterns covering stuck-at-0, stuck-at-1, and alternating bit faults.
const TEST_PATTERNS: &[[u8; 2]] = &[
    [0x00, 0x00], // All zeros
    [0xFF, 0xFF], // All ones
    [0xAA, 0xAA], // Alternating 10
    [0x55, 0x55], // Alternating 01
    [0xDE, 0xAD], // Pseudo-random word
];

/// Error type for the loopback self-test.
#[derive(Debug)]
pub enum SelfTestError<E> {
    /// SPI transfer returned a hardware error.
    SpiError(E),
    /// Data received during loopback did not match transmitted data.
    /// Payload: (pattern_index, tx_byte_0, rx_byte_0, tx_byte_1, rx_byte_1)
    DataMismatch(usize, u8, u8, u8, u8),
}

/// Run the SPI loopback self-test.
///
/// The caller must have configured the SPI controller in loopback mode
/// (MOSI internally connected to MISO) before calling this function.
///
/// # Returns
/// `Ok(())` if all patterns pass, `Err(SelfTestError)` on any failure.
pub fn spi_loopback_selftest<SPI, E>(
    spi: &mut SPI,
) -> Result<(), SelfTestError<E>>
where
    SPI: Transfer<u8, Error = E>,
{
    for (idx, pattern) in TEST_PATTERNS.iter().enumerate() {
        let mut buf = *pattern; // Copy pattern into mutable buffer
        spi.transfer(&mut buf).map_err(SelfTestError::SpiError)?;

        if buf != *pattern {
            return Err(SelfTestError::DataMismatch(
                idx,
                pattern[0], buf[0],
                pattern[1], buf[1],
            ));
        }
    }

    Ok(())
}
```

---

## Testing and Validation Strategies

### Unit Testing: E2E Layer

Every E2E check function must be covered by unit tests using known AUTOSAR test vectors, bit-flip injection (each bit of the data, counter, and CRC fields), counter boundary conditions (counter at 0, 14, 15, wrap to 0), and missing-frame simulations.

### Hardware-in-the-Loop (HiL) Testing

HiL testing injects realistic fault scenarios that cannot be reproduced on a desktop:

- **Bit-error injection via FPGA**: A logic analyser / signal manipulator intercepts the SPI MISO line and flips individual bits at configurable rates.
- **Power interruption during SPI transfer**: Simulates ECU brownout mid-transaction; verifies that partial frames are detected by CRC.
- **Temperature-swept testing**: Verifies signal integrity and timing margins at −40 °C and +125 °C.
- **EMC burst injection**: IEC 61000-4-4 EFT pulses applied to the SPI harness; CRC and E2E mechanisms must absorb errors without triggering spurious safe states.

### FMEA / FMEDA

A Failure Mode and Effects Analysis (FMEA) or FMEDA must document each hardware failure mode of the SPI path, its probability (lambda), the diagnostic coverage of the software measures, and the resulting residual risk. This deliverable is a mandatory input to the ISO 26262 Part 5 hardware safety analysis and is reviewed by the system safety manager.

### Regression and Static Analysis

All safety-relevant SPI software components must pass:

- **MISRA C:2012** compliance (for C/C++ code): Rules 10.x (essential type checking), 15.x (control flow), 17.x (functions), 21.x (standard libraries) are particularly relevant.
- **Rust's type system** provides many MISRA-equivalent guarantees by default (no undefined behaviour, no implicit casts, exhaustive match).
- **Static analysis tools**: Polyspace (MathWorks), Klocwork, or Frama-C for C/C++; `cargo audit`, `clippy`, and `cargo-geiger` for Rust.

---

## Summary

Automotive SPI systems operating in safety-critical roles must satisfy two distinct but complementary qualification frameworks:

**AEC-Q100** governs the physical device qualification. Every IC on the SPI bus — the microcontroller, sensor, EEPROM, or power monitor — must carry an AEC-Q100 qualification certificate at the appropriate temperature grade. The qualification ensures that the device will meet its electrical specifications across the full automotive temperature range and across its intended operating lifetime (typically 15 years / 150,000 km). Key tests include HTOL, temperature cycling, ESD, and latch-up immunity. Software engineers must also account for parameter drift documented in the AEC-Q100 qualification report, since aging effects can push signal timing outside valid SPI windows at end-of-life.

**ISO 26262** governs functional safety at the system, hardware, and software levels. For SPI paths carrying safety-relevant data, the standard mandates end-to-end (E2E) communication protection including CRC, rolling counter, data ID, and timeout supervision. The AUTOSAR E2E library, particularly Profile 2 (8-bit CRC) and Profile 5 (32-bit CRC), provides a standardised, tool-verified implementation of these measures. At the hardware level, automotive MCUs provide loopback self-test, parity, and frame-length checking. The software layer adds safe-state transitions, hold-last-valid logic, and diagnostic counters that feed the diagnostic event manager (DEM) and are accessible via UDS service 0x19.

The code examples in this document cover the full sender–receiver E2E pipeline in both **C/C++** and **Rust**, as well as startup loopback self-tests, watchdog servicing integration, and safe-state escalation. Rust's ownership model and compile-time guarantees offer particular advantages for ASIL C/D development, as they eliminate entire classes of undefined behaviour (buffer overflows, data races, uninitialised memory) that would otherwise require runtime checks or MISRA compliance rules to address. C/C++ remains the dominant language in production ECU firmware due to ecosystem maturity, but Rust adoption in automotive safety software is increasing, supported by growing qualification toolchain availability (e.g., Ferrocene, the safety-certified Rust compiler).

Together, AEC-Q100 and ISO 26262 compliance transforms a basic four-wire SPI bus into a robust, deterministic, and fault-tolerant communication backbone fit for modern ADAS, EPS, ABS, and powertrain control applications.

---

*Document version 1.0 — Topic 91: Automotive SPI Requirements*