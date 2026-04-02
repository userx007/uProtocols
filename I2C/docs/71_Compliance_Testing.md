# 71. I2C Compliance Testing

**Structure:**
- NXP UM10204 mode table (SM / FM / FM+ / HS / UFM) with frequency limits
- All three compliance layers: electrical, timing, and protocol
- Pull-up resistor calculation formulas (Rp min/max)
- Key timing parameter tables for each mode

**C/C++ code:**
- `I2C_ModeParams` struct with constants for SM, FM, and FM+
- `i2c_run_compliance()` — full timing and voltage checker against captured waveform data
- `i2c_check_pullup()` — Rp compliance calculator
- `validate_stm32_i2c_timing()` — direct hardware register validation (STM32 TIMINGR)
- `i2c_validate_protocol_sequence()` — START/STOP/DATA event decoder
- CI regression test suite with explicit pass/fail expectations

**Rust code:**
- Type-safe `I2cModeParams` with const instances for all three modes
- `CheckStatus` enum with `Marginal { margin_pct }` for near-limit detection
- `I2cComplianceChecker` with builder pattern and full report Display formatting
- `PullupChecker` with `rp_min_ohm()` / `rp_max_ohm()` / `is_compliant()`
- `ProtocolAnalyzer` for bus event decoding and clock-stretch violation detection
- Full `#[cfg(test)]` unit test module

**Summary** covers the five most common compliance failure modes with root causes and fixes.

## Validating I2C Implementations Against NXP I2C Specification Requirements

---

## Table of Contents

1. [Introduction](#introduction)
2. [NXP I2C Specification Overview](#nxp-i2c-specification-overview)
3. [Compliance Testing Categories](#compliance-testing-categories)
4. [Electrical Compliance](#electrical-compliance)
5. [Timing Compliance](#timing-compliance)
6. [Protocol Compliance](#protocol-compliance)
7. [Implementation in C/C++](#implementation-in-cc)
8. [Implementation in Rust](#implementation-in-rust)
9. [Test Automation and Frameworks](#test-automation-and-frameworks)
10. [Common Compliance Failures and Fixes](#common-compliance-failures-and-fixes)
11. [Summary](#summary)

---

## Introduction

I2C (Inter-Integrated Circuit) compliance testing is the process of systematically verifying that an I2C controller, peripheral, or system implementation conforms to the requirements defined in the NXP I2C specification (formerly Philips). Compliance is not merely an academic exercise — it is a prerequisite for interoperability between devices from different vendors, reliable operation across voltage levels and operating conditions, and certification for commercial products.

The NXP I2C specification defines three broad layers of requirements:

- **Electrical requirements**: voltage levels, current drive, capacitance limits
- **Timing requirements**: setup/hold times, clock frequencies, rise/fall times
- **Protocol requirements**: START/STOP conditions, arbitration, clock stretching, addressing, ACK/NACK handling

Compliance testing validates all three layers through a combination of hardware measurements and software-driven protocol analysis.

---

## NXP I2C Specification Overview

The NXP UM10204 specification defines several operating modes:

| Mode               | Max Clock Frequency | Notes                              |
|--------------------|---------------------|------------------------------------|
| Standard Mode (SM) | 100 kHz             | Original specification             |
| Fast Mode (FM)     | 400 kHz             | Most common embedded use           |
| Fast Mode Plus (FM+)| 1 MHz              | Higher current drive required      |
| High-Speed Mode (HS)| 3.4 MHz            | Special current-source protocol    |
| Ultra Fast Mode (UFM)| 5 MHz             | Unidirectional; no ACK             |

Each mode has distinct electrical and timing parameters that must be verified independently.

---

## Compliance Testing Categories

### 3.1 Mandatory vs. Optional Requirements

The specification distinguishes between mandatory requirements ("shall") and recommendations ("should"). Compliance testing focuses first on mandatory items:

- SCL/SDA voltage levels (VOL, VOH, VIL, VIH)
- Minimum/maximum clock periods
- Setup and hold times for START, STOP, and data
- Address recognition (7-bit and 10-bit)
- ACK/NACK generation and detection
- Bus-free time between transactions

### 3.2 Test Hierarchy

```
Compliance Tests
├── Level 1: Electrical
│   ├── DC parametric (voltage levels, leakage)
│   └── AC parametric (rise/fall times, capacitance)
├── Level 2: Timing
│   ├── Clock parameters (fSCL, tLOW, tHIGH)
│   ├── Setup/hold times (tSU:DAT, tHD:DAT, tSU:STA, tHD:STA)
│   └── Bus free time (tBUF)
└── Level 3: Protocol
    ├── Address phase
    ├── Data phase
    ├── Clock stretching
    ├── Arbitration
    └── Error recovery
```

---

## Electrical Compliance

### 4.1 Voltage Level Requirements

For Standard/Fast Mode with VDD = 3.3V:

| Parameter | Symbol | Min    | Max    |
|-----------|--------|--------|--------|
| Low output voltage | VOL | — | 0.4V |
| High output voltage | VOH | VDD - 0.5V | — |
| Low input threshold | VIL | — | 0.3 × VDD |
| High input threshold | VIH | 0.7 × VDD | — |

### 4.2 Pull-up Resistor Calculation

The pull-up resistor value is critical for compliance. The NXP spec requires:

```
Rp(min) = (VDD - VOL(max)) / IOL(max)
Rp(max) = tr(max) / (0.8473 × Cb)
```

Where:
- `IOL` = sink current capability (3 mA for FM, 20 mA for FM+)
- `Cb` = bus capacitance
- `tr` = maximum allowed rise time

---

## Timing Compliance

### 5.1 Key Timing Parameters (Fast Mode, 400 kHz)

| Parameter | Symbol     | Min    | Max    |
|-----------|------------|--------|--------|
| SCL clock frequency | fSCL | — | 400 kHz |
| SCL low period | tLOW | 1.3 µs | — |
| SCL high period | tHIGH | 0.6 µs | — |
| Setup time for STOP | tSU:STO | 0.6 µs | — |
| Hold time START | tHD:STA | 0.6 µs | — |
| Data setup time | tSU:DAT | 100 ns | — |
| Data hold time | tHD:DAT | 0 ns | 0.9 µs |
| Rise time | tr | — | 300 ns |
| Fall time | tf | — | 300 ns |
| Bus free time | tBUF | 1.3 µs | — |

### 5.2 Clock Stretching Compliance

A device that stretches the clock must:
1. Only pull SCL low (never drive it high)
2. Release SCL before the master times out
3. Not hold SCL low for longer than the implementation-defined maximum

---

## Protocol Compliance

### 6.1 START and STOP Conditions

A **START condition** is defined as SDA transitioning HIGH-to-LOW while SCL is HIGH.
A **STOP condition** is defined as SDA transitioning LOW-to-HIGH while SCL is HIGH.

Any deviation — such as SDA changing while SCL is LOW during a supposed START — is a protocol violation.

### 6.2 Arbitration

When two masters simultaneously initiate a transfer:
- Each master monitors SDA while driving it
- If a master drives SDA HIGH but reads SDA LOW, another master is driving it LOW — the HIGH driver must immediately release the bus
- The master that drives LOW wins arbitration and continues uninterrupted

### 6.3 10-bit Addressing

The 10-bit address sequence uses a two-byte header:
```
First byte:  1111 0XX R/W   (XX = upper 2 bits of address)
Second byte: XXXXXXXX       (lower 8 bits of address)
```
Each byte requires an ACK from the peripheral.

---

## Implementation in C/C++

### 7.1 Timing Verification Framework

The following C implementation provides a software-based compliance checker that validates timing parameters captured from a logic analyzer or oscilloscope via CSV export.

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* ============================================================
 * I2C Compliance Timing Parameters (Fast Mode, 400 kHz)
 * All values in nanoseconds unless noted
 * ============================================================ */

typedef struct {
    const char *mode_name;
    uint32_t f_scl_max_hz;    /* Maximum SCL frequency (Hz) */
    uint32_t t_low_min_ns;    /* Minimum SCL low period */
    uint32_t t_high_min_ns;   /* Minimum SCL high period */
    uint32_t t_hd_sta_min_ns; /* Hold time START condition */
    uint32_t t_su_sta_min_ns; /* Setup time repeated START */
    uint32_t t_su_sto_min_ns; /* Setup time STOP condition */
    uint32_t t_buf_min_ns;    /* Bus free time between STOP and START */
    uint32_t t_su_dat_min_ns; /* Data setup time */
    uint32_t t_hd_dat_max_ns; /* Data hold time maximum */
    uint32_t t_r_max_ns;      /* Rise time maximum */
    uint32_t t_f_max_ns;      /* Fall time maximum */
    float    v_il_ratio;      /* VIL as fraction of VDD */
    float    v_ih_ratio;      /* VIH as fraction of VDD */
    float    v_ol_max_v;      /* VOL maximum in volts */
} I2C_ModeParams;

/* NXP UM10204 Table 10 parameters */
static const I2C_ModeParams i2c_params[] = {
    {
        .mode_name       = "Standard Mode (100 kHz)",
        .f_scl_max_hz    = 100000,
        .t_low_min_ns    = 4700,
        .t_high_min_ns   = 4000,
        .t_hd_sta_min_ns = 4000,
        .t_su_sta_min_ns = 4700,
        .t_su_sto_min_ns = 4000,
        .t_buf_min_ns    = 4700,
        .t_su_dat_min_ns = 250,
        .t_hd_dat_max_ns = 3450,
        .t_r_max_ns      = 1000,
        .t_f_max_ns      = 300,
        .v_il_ratio      = 0.3f,
        .v_ih_ratio      = 0.7f,
        .v_ol_max_v      = 0.4f,
    },
    {
        .mode_name       = "Fast Mode (400 kHz)",
        .f_scl_max_hz    = 400000,
        .t_low_min_ns    = 1300,
        .t_high_min_ns   = 600,
        .t_hd_sta_min_ns = 600,
        .t_su_sta_min_ns = 600,
        .t_su_sto_min_ns = 600,
        .t_buf_min_ns    = 1300,
        .t_su_dat_min_ns = 100,
        .t_hd_dat_max_ns = 900,
        .t_r_max_ns      = 300,
        .t_f_max_ns      = 300,
        .v_il_ratio      = 0.3f,
        .v_ih_ratio      = 0.7f,
        .v_ol_max_v      = 0.4f,
    },
    {
        .mode_name       = "Fast Mode Plus (1 MHz)",
        .f_scl_max_hz    = 1000000,
        .t_low_min_ns    = 500,
        .t_high_min_ns   = 260,
        .t_hd_sta_min_ns = 260,
        .t_su_sta_min_ns = 260,
        .t_su_sto_min_ns = 260,
        .t_buf_min_ns    = 500,
        .t_su_dat_min_ns = 50,
        .t_hd_dat_max_ns = 0,   /* No hold time in FM+ */
        .t_r_max_ns      = 120,
        .t_f_max_ns      = 120,
        .v_il_ratio      = 0.3f,
        .v_ih_ratio      = 0.7f,
        .v_ol_max_v      = 0.4f,
    },
};

/* ============================================================
 * Compliance test result structure
 * ============================================================ */

typedef enum {
    COMPLIANCE_PASS = 0,
    COMPLIANCE_FAIL_TIMING,
    COMPLIANCE_FAIL_VOLTAGE,
    COMPLIANCE_FAIL_PROTOCOL,
    COMPLIANCE_MARGINAL,    /* Within 10% of limit */
} ComplianceResult;

typedef struct {
    const char      *test_name;
    ComplianceResult result;
    double           measured_value;
    double           limit_value;
    bool             is_max_limit;  /* true = measured must be <= limit */
    char             unit[16];
} ComplianceTestRecord;

#define MAX_TEST_RECORDS 64

typedef struct {
    ComplianceTestRecord records[MAX_TEST_RECORDS];
    uint32_t             count;
    uint32_t             pass_count;
    uint32_t             fail_count;
    uint32_t             marginal_count;
    const I2C_ModeParams *params;
} ComplianceReport;

/* ============================================================
 * Captured waveform data (from logic analyzer CSV)
 * ============================================================ */

typedef struct {
    double scl_low_ns;
    double scl_high_ns;
    double t_hd_sta_ns;
    double t_su_sta_ns;
    double t_su_sto_ns;
    double t_buf_ns;
    double t_su_dat_ns;
    double t_hd_dat_ns;
    double t_r_ns;         /* SDA rise time */
    double t_f_ns;         /* SDA fall time */
    double scl_freq_hz;
    double vdd_v;
    double vol_measured_v;
    double voh_measured_v;
} I2C_WaveformCapture;

/* ============================================================
 * Core compliance checking functions
 * ============================================================ */

static void record_test(ComplianceReport *report, const char *name,
                         double measured, double limit, bool is_max,
                         const char *unit)
{
    if (report->count >= MAX_TEST_RECORDS) return;

    ComplianceTestRecord *rec = &report->records[report->count++];
    rec->test_name      = name;
    rec->measured_value = measured;
    rec->limit_value    = limit;
    rec->is_max_limit   = is_max;
    snprintf(rec->unit, sizeof(rec->unit), "%s", unit);

    bool pass = is_max ? (measured <= limit) : (measured >= limit);

    if (!pass) {
        rec->result = COMPLIANCE_FAIL_TIMING;
        report->fail_count++;
        return;
    }

    /* Marginal: within 10% of the limit */
    double margin = is_max
        ? (limit - measured) / limit
        : (measured - limit) / limit;

    if (margin < 0.10) {
        rec->result = COMPLIANCE_MARGINAL;
        report->marginal_count++;
    } else {
        rec->result = COMPLIANCE_PASS;
        report->pass_count++;
    }
}

ComplianceReport i2c_run_compliance(const I2C_WaveformCapture *wave,
                                    const I2C_ModeParams *params)
{
    ComplianceReport report = {0};
    report.params = params;

    /* --- Timing Tests --- */

    /* SCL frequency */
    record_test(&report, "SCL Frequency",
                wave->scl_freq_hz, (double)params->f_scl_max_hz,
                true, "Hz");

    /* SCL low period */
    record_test(&report, "tLOW (SCL low period)",
                wave->scl_low_ns, (double)params->t_low_min_ns,
                false, "ns");

    /* SCL high period */
    record_test(&report, "tHIGH (SCL high period)",
                wave->scl_high_ns, (double)params->t_high_min_ns,
                false, "ns");

    /* START hold time */
    record_test(&report, "tHD:STA (START hold time)",
                wave->t_hd_sta_ns, (double)params->t_hd_sta_min_ns,
                false, "ns");

    /* Repeated START setup time */
    record_test(&report, "tSU:STA (repeated START setup)",
                wave->t_su_sta_ns, (double)params->t_su_sta_min_ns,
                false, "ns");

    /* STOP setup time */
    record_test(&report, "tSU:STO (STOP setup time)",
                wave->t_su_sto_ns, (double)params->t_su_sto_min_ns,
                false, "ns");

    /* Bus free time */
    record_test(&report, "tBUF (bus free time)",
                wave->t_buf_ns, (double)params->t_buf_min_ns,
                false, "ns");

    /* Data setup time */
    record_test(&report, "tSU:DAT (data setup time)",
                wave->t_su_dat_ns, (double)params->t_su_dat_min_ns,
                false, "ns");

    /* Data hold time (maximum) */
    if (params->t_hd_dat_max_ns > 0) {
        record_test(&report, "tHD:DAT (data hold time)",
                    wave->t_hd_dat_ns, (double)params->t_hd_dat_max_ns,
                    true, "ns");
    }

    /* SDA rise time */
    record_test(&report, "tr (SDA rise time)",
                wave->t_r_ns, (double)params->t_r_max_ns,
                true, "ns");

    /* SDA fall time */
    record_test(&report, "tf (SDA fall time)",
                wave->t_f_ns, (double)params->t_f_max_ns,
                true, "ns");

    /* --- Voltage Tests --- */
    double v_il_max = wave->vdd_v * params->v_il_ratio;
    double v_ih_min = wave->vdd_v * params->v_ih_ratio;

    record_test(&report, "VOL (output low voltage)",
                wave->vol_measured_v, params->v_ol_max_v,
                true, "V");

    /* For a qualitative check — VOH margin */
    double voh_margin = wave->voh_measured_v - (wave->vdd_v - 0.5);
    record_test(&report, "VOH margin (VDD - 0.5V)",
                wave->voh_measured_v, wave->vdd_v - 0.5,
                false, "V");

    (void)v_il_max;
    (void)v_ih_min;

    return report;
}

/* ============================================================
 * Report generation
 * ============================================================ */

void i2c_print_compliance_report(const ComplianceReport *report)
{
    static const char *status_str[] = {
        [COMPLIANCE_PASS]          = "PASS    ",
        [COMPLIANCE_FAIL_TIMING]   = "FAIL    ",
        [COMPLIANCE_FAIL_VOLTAGE]  = "FAIL(V) ",
        [COMPLIANCE_FAIL_PROTOCOL] = "FAIL(P) ",
        [COMPLIANCE_MARGINAL]      = "MARGINAL",
    };

    printf("=======================================================\n");
    printf("  I2C Compliance Report: %s\n", report->params->mode_name);
    printf("=======================================================\n");
    printf("%-30s %-10s %-14s %-14s\n",
           "Test", "Status", "Measured", "Limit");
    printf("%-30s %-10s %-14s %-14s\n",
           "------------------------------",
           "--------", "-----------", "-------");

    for (uint32_t i = 0; i < report->count; i++) {
        const ComplianceTestRecord *rec = &report->records[i];
        printf("%-30s %-10s %-10.1f %-4s  %s%-10.1f %-4s\n",
               rec->test_name,
               status_str[rec->result],
               rec->measured_value, rec->unit,
               rec->is_max_limit ? "<= " : ">= ",
               rec->limit_value, rec->unit);
    }

    printf("-------------------------------------------------------\n");
    printf("Total: %u  |  Pass: %u  |  Marginal: %u  |  Fail: %u\n",
           report->count,
           report->pass_count,
           report->marginal_count,
           report->fail_count);
    printf("Overall: %s\n",
           report->fail_count == 0
               ? (report->marginal_count == 0 ? "COMPLIANT" : "COMPLIANT (with marginal items)")
               : "NON-COMPLIANT");
    printf("=======================================================\n");
}

/* ============================================================
 * Pull-up resistor compliance calculator
 * ============================================================ */

typedef struct {
    double rp_min_ohm;   /* Minimum pull-up resistance */
    double rp_max_ohm;   /* Maximum pull-up resistance */
    double rp_actual_ohm;
    bool   is_compliant;
} PullupCompliance;

PullupCompliance i2c_check_pullup(double vdd_v, double vol_max_v,
                                   double iol_ma, double cb_pf,
                                   double tr_max_ns, double rp_actual_ohm)
{
    PullupCompliance result;

    /* Rp(min) prevents exceeding IOL at VOL */
    result.rp_min_ohm = (vdd_v - vol_max_v) / (iol_ma / 1000.0);

    /* Rp(max) ensures rise time complies: tr = 0.8473 * Rp * Cb */
    result.rp_max_ohm = (tr_max_ns * 1e-9) / (0.8473 * cb_pf * 1e-12);

    result.rp_actual_ohm = rp_actual_ohm;
    result.is_compliant  = (rp_actual_ohm >= result.rp_min_ohm) &&
                            (rp_actual_ohm <= result.rp_max_ohm);

    return result;
}

/* ============================================================
 * Protocol compliance: START/STOP validator
 * ============================================================ */

typedef enum {
    LINE_HIGH = 1,
    LINE_LOW  = 0,
} LineState;

typedef struct {
    double    timestamp_ns;
    LineState scl;
    LineState sda;
} BusSample;

typedef enum {
    EVENT_NONE,
    EVENT_START,
    EVENT_REPEATED_START,
    EVENT_STOP,
    EVENT_DATA_BIT,
    EVENT_VIOLATION,
} BusEvent;

typedef struct {
    BusEvent event;
    double   timestamp_ns;
    const char *description;
} EventRecord;

BusEvent detect_condition(const BusSample *prev, const BusSample *curr)
{
    /* SCL is HIGH for both samples */
    if (prev->scl == LINE_HIGH && curr->scl == LINE_HIGH) {
        if (prev->sda == LINE_HIGH && curr->sda == LINE_LOW)
            return EVENT_START;    /* SDA falls while SCL high */
        if (prev->sda == LINE_LOW && curr->sda == LINE_HIGH)
            return EVENT_STOP;     /* SDA rises while SCL high */
    }
    /* SDA changes while SCL is LOW: valid data transition */
    if (prev->scl == LINE_LOW && curr->scl == LINE_LOW) {
        /* SDA change during SCL low is allowed (setup for next bit) */
        return EVENT_NONE;
    }
    /* SCL rises: sample data bit */
    if (prev->scl == LINE_LOW && curr->scl == LINE_HIGH) {
        return EVENT_DATA_BIT;
    }
    return EVENT_NONE;
}

int i2c_validate_protocol_sequence(const BusSample *samples,
                                    uint32_t n_samples,
                                    EventRecord *events_out,
                                    uint32_t max_events)
{
    int event_count = 0;

    for (uint32_t i = 1; i < n_samples && event_count < (int)max_events; i++) {
        BusEvent ev = detect_condition(&samples[i-1], &samples[i]);
        if (ev != EVENT_NONE) {
            events_out[event_count].event        = ev;
            events_out[event_count].timestamp_ns = samples[i].timestamp_ns;
            event_count++;
        }
    }
    return event_count;
}

/* ============================================================
 * Example usage
 * ============================================================ */

int main(void)
{
    /* Simulated captured waveform for Fast Mode */
    I2C_WaveformCapture capture = {
        .scl_low_ns    = 1450.0,   /* OK: > 1300 ns */
        .scl_high_ns   = 680.0,    /* OK: > 600 ns  */
        .t_hd_sta_ns   = 620.0,    /* Marginal: > 600 ns by tiny margin */
        .t_su_sta_ns   = 710.0,
        .t_su_sto_ns   = 650.0,
        .t_buf_ns      = 1500.0,
        .t_su_dat_ns   = 115.0,
        .t_hd_dat_ns   = 450.0,
        .t_r_ns        = 290.0,    /* Marginal: < 300 ns limit */
        .t_f_ns        = 180.0,
        .scl_freq_hz   = 395000.0, /* OK: < 400 kHz */
        .vdd_v         = 3.3,
        .vol_measured_v = 0.35,
        .voh_measured_v = 2.95,
    };

    ComplianceReport report = i2c_run_compliance(&capture, &i2c_params[1]);
    i2c_print_compliance_report(&report);

    /* Pull-up resistor check */
    PullupCompliance pu = i2c_check_pullup(
        3.3,   /* VDD */
        0.4,   /* VOL max */
        3.0,   /* IOL in mA */
        100.0, /* Bus capacitance in pF */
        300.0, /* tr max in ns */
        1500.0 /* Actual Rp */
    );

    printf("\nPull-up Resistor Compliance:\n");
    printf("  Rp min: %.0f Ω\n", pu.rp_min_ohm);
    printf("  Rp max: %.0f Ω\n", pu.rp_max_ohm);
    printf("  Rp actual: %.0f Ω  =>  %s\n",
           pu.rp_actual_ohm, pu.is_compliant ? "COMPLIANT" : "NON-COMPLIANT");

    return report.fail_count == 0 ? 0 : 1;
}
```

### 7.2 Hardware-Level Register Validation (STM32 Example)

```c
/* Validate STM32 I2C peripheral timing register against NXP spec */
#include "stm32f4xx_hal.h"

typedef struct {
    uint32_t presc;
    uint32_t scldel;
    uint32_t sdadel;
    uint32_t sclh;
    uint32_t scll;
    uint32_t pclk_hz;
} STM32_I2C_TimingConfig;

typedef struct {
    bool     t_low_ok;
    bool     t_high_ok;
    bool     t_su_dat_ok;
    bool     t_hd_dat_ok;
    double   actual_t_low_ns;
    double   actual_t_high_ns;
    double   actual_t_su_dat_ns;
    double   actual_t_hd_dat_ns;
} STM32_TimingValidation;

STM32_TimingValidation validate_stm32_i2c_timing(
    const STM32_I2C_TimingConfig *cfg,
    const I2C_ModeParams *spec)
{
    STM32_TimingValidation v = {0};

    double t_presc_ns = (1.0e9 * (cfg->presc + 1)) / cfg->pclk_hz;

    v.actual_t_low_ns    = (cfg->scll + 1) * t_presc_ns;
    v.actual_t_high_ns   = (cfg->sclh + 1) * t_presc_ns;
    v.actual_t_su_dat_ns = (cfg->scldel + 1) * t_presc_ns;
    v.actual_t_hd_dat_ns = cfg->sdadel * t_presc_ns;

    v.t_low_ok    = v.actual_t_low_ns    >= spec->t_low_min_ns;
    v.t_high_ok   = v.actual_t_high_ns   >= spec->t_high_min_ns;
    v.t_su_dat_ok = v.actual_t_su_dat_ns >= spec->t_su_dat_min_ns;
    v.t_hd_dat_ok = spec->t_hd_dat_max_ns == 0 ||
                    v.actual_t_hd_dat_ns <= spec->t_hd_dat_max_ns;

    return v;
}
```

---

## Implementation in Rust

### 8.1 Compliance Parameter Types and Validation

```rust
//! I2C Compliance Testing Library
//! Validates I2C implementations against NXP UM10204 specification.

use std::fmt;

/// Operating mode parameters from NXP UM10204
#[derive(Debug, Clone)]
pub struct I2cModeParams {
    pub name: &'static str,
    pub f_scl_max_hz: u32,
    pub t_low_min_ns: u32,
    pub t_high_min_ns: u32,
    pub t_hd_sta_min_ns: u32,
    pub t_su_sta_min_ns: u32,
    pub t_su_sto_min_ns: u32,
    pub t_buf_min_ns: u32,
    pub t_su_dat_min_ns: u32,
    pub t_hd_dat_max_ns: Option<u32>, // None = no maximum
    pub t_r_max_ns: u32,
    pub t_f_max_ns: u32,
    pub v_il_ratio: f64,
    pub v_ih_ratio: f64,
    pub v_ol_max_v: f64,
}

impl I2cModeParams {
    /// Fast Mode (400 kHz) parameters per NXP UM10204 Table 10
    pub const FAST_MODE: Self = Self {
        name: "Fast Mode (400 kHz)",
        f_scl_max_hz: 400_000,
        t_low_min_ns: 1300,
        t_high_min_ns: 600,
        t_hd_sta_min_ns: 600,
        t_su_sta_min_ns: 600,
        t_su_sto_min_ns: 600,
        t_buf_min_ns: 1300,
        t_su_dat_min_ns: 100,
        t_hd_dat_max_ns: Some(900),
        t_r_max_ns: 300,
        t_f_max_ns: 300,
        v_il_ratio: 0.3,
        v_ih_ratio: 0.7,
        v_ol_max_v: 0.4,
    };

    /// Standard Mode (100 kHz) parameters
    pub const STANDARD_MODE: Self = Self {
        name: "Standard Mode (100 kHz)",
        f_scl_max_hz: 100_000,
        t_low_min_ns: 4700,
        t_high_min_ns: 4000,
        t_hd_sta_min_ns: 4000,
        t_su_sta_min_ns: 4700,
        t_su_sto_min_ns: 4000,
        t_buf_min_ns: 4700,
        t_su_dat_min_ns: 250,
        t_hd_dat_max_ns: Some(3450),
        t_r_max_ns: 1000,
        t_f_max_ns: 300,
        v_il_ratio: 0.3,
        v_ih_ratio: 0.7,
        v_ol_max_v: 0.4,
    };

    /// Fast Mode Plus (1 MHz) parameters
    pub const FAST_MODE_PLUS: Self = Self {
        name: "Fast Mode Plus (1 MHz)",
        f_scl_max_hz: 1_000_000,
        t_low_min_ns: 500,
        t_high_min_ns: 260,
        t_hd_sta_min_ns: 260,
        t_su_sta_min_ns: 260,
        t_su_sto_min_ns: 260,
        t_buf_min_ns: 500,
        t_su_dat_min_ns: 50,
        t_hd_dat_max_ns: None,
        t_r_max_ns: 120,
        t_f_max_ns: 120,
        v_il_ratio: 0.3,
        v_ih_ratio: 0.7,
        v_ol_max_v: 0.4,
    };
}

/// Result of a single compliance check
#[derive(Debug, Clone, PartialEq)]
pub enum CheckStatus {
    Pass,
    Marginal { margin_pct: f64 },
    Fail,
}

impl fmt::Display for CheckStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CheckStatus::Pass => write!(f, "PASS    "),
            CheckStatus::Marginal { margin_pct } => {
                write!(f, "MARGINAL ({:.1}% margin)", margin_pct)
            }
            CheckStatus::Fail => write!(f, "FAIL    "),
        }
    }
}

/// A single compliance measurement record
#[derive(Debug, Clone)]
pub struct ComplianceRecord {
    pub name: String,
    pub status: CheckStatus,
    pub measured: f64,
    pub limit: f64,
    pub is_max_limit: bool,
    pub unit: String,
}

impl fmt::Display for ComplianceRecord {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let limit_op = if self.is_max_limit { "<=" } else { ">=" };
        write!(
            f,
            "{:<32} {}  {:>10.1} {:<4}  {} {:>10.1} {}",
            self.name,
            self.status,
            self.measured,
            self.unit,
            limit_op,
            self.limit,
            self.unit
        )
    }
}

/// Complete compliance report
#[derive(Debug)]
pub struct ComplianceReport {
    pub mode_name: String,
    pub records: Vec<ComplianceRecord>,
}

impl ComplianceReport {
    pub fn pass_count(&self) -> usize {
        self.records
            .iter()
            .filter(|r| r.status == CheckStatus::Pass)
            .count()
    }

    pub fn fail_count(&self) -> usize {
        self.records
            .iter()
            .filter(|r| r.status == CheckStatus::Fail)
            .count()
    }

    pub fn marginal_count(&self) -> usize {
        self.records
            .iter()
            .filter(|r| matches!(r.status, CheckStatus::Marginal { .. }))
            .count()
    }

    pub fn is_compliant(&self) -> bool {
        self.fail_count() == 0
    }
}

impl fmt::Display for ComplianceReport {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "{}", "=".repeat(78))?;
        writeln!(f, "  I2C Compliance Report: {}", self.mode_name)?;
        writeln!(f, "{}", "=".repeat(78))?;
        for rec in &self.records {
            writeln!(f, "{}", rec)?;
        }
        writeln!(f, "{}", "-".repeat(78))?;
        writeln!(
            f,
            "Total: {} | Pass: {} | Marginal: {} | Fail: {}",
            self.records.len(),
            self.pass_count(),
            self.marginal_count(),
            self.fail_count()
        )?;
        write!(
            f,
            "Overall: {}",
            if self.is_compliant() { "COMPLIANT" } else { "NON-COMPLIANT" }
        )
    }
}

/// Captured waveform measurements (from logic analyzer or oscilloscope)
#[derive(Debug, Clone)]
pub struct WaveformCapture {
    pub scl_freq_hz: f64,
    pub scl_low_ns: f64,
    pub scl_high_ns: f64,
    pub t_hd_sta_ns: f64,
    pub t_su_sta_ns: f64,
    pub t_su_sto_ns: f64,
    pub t_buf_ns: f64,
    pub t_su_dat_ns: f64,
    pub t_hd_dat_ns: f64,
    pub t_r_ns: f64,
    pub t_f_ns: f64,
    pub vdd_v: f64,
    pub vol_measured_v: f64,
    pub voh_measured_v: f64,
}

/// Core compliance checker
pub struct I2cComplianceChecker {
    params: I2cModeParams,
    marginal_threshold: f64, // fraction of limit; default 0.10
}

impl I2cComplianceChecker {
    pub fn new(params: I2cModeParams) -> Self {
        Self {
            params,
            marginal_threshold: 0.10,
        }
    }

    pub fn with_marginal_threshold(mut self, threshold: f64) -> Self {
        self.marginal_threshold = threshold;
        self
    }

    fn check_min(
        &self,
        name: &str,
        measured: f64,
        min: f64,
        unit: &str,
    ) -> ComplianceRecord {
        let status = if measured < min {
            CheckStatus::Fail
        } else {
            let margin = (measured - min) / min;
            if margin < self.marginal_threshold {
                CheckStatus::Marginal { margin_pct: margin * 100.0 }
            } else {
                CheckStatus::Pass
            }
        };
        ComplianceRecord {
            name: name.to_string(),
            status,
            measured,
            limit: min,
            is_max_limit: false,
            unit: unit.to_string(),
        }
    }

    fn check_max(
        &self,
        name: &str,
        measured: f64,
        max: f64,
        unit: &str,
    ) -> ComplianceRecord {
        let status = if measured > max {
            CheckStatus::Fail
        } else {
            let margin = (max - measured) / max;
            if margin < self.marginal_threshold {
                CheckStatus::Marginal { margin_pct: margin * 100.0 }
            } else {
                CheckStatus::Pass
            }
        };
        ComplianceRecord {
            name: name.to_string(),
            status,
            measured,
            limit: max,
            is_max_limit: true,
            unit: unit.to_string(),
        }
    }

    /// Run all compliance checks against a waveform capture
    pub fn check(&self, wave: &WaveformCapture) -> ComplianceReport {
        let p = &self.params;
        let mut records = Vec::new();

        // Timing checks
        records.push(self.check_max(
            "SCL Frequency",
            wave.scl_freq_hz,
            p.f_scl_max_hz as f64,
            "Hz",
        ));
        records.push(self.check_min(
            "tLOW (SCL low)",
            wave.scl_low_ns,
            p.t_low_min_ns as f64,
            "ns",
        ));
        records.push(self.check_min(
            "tHIGH (SCL high)",
            wave.scl_high_ns,
            p.t_high_min_ns as f64,
            "ns",
        ));
        records.push(self.check_min(
            "tHD:STA (START hold)",
            wave.t_hd_sta_ns,
            p.t_hd_sta_min_ns as f64,
            "ns",
        ));
        records.push(self.check_min(
            "tSU:STA (rSTART setup)",
            wave.t_su_sta_ns,
            p.t_su_sta_min_ns as f64,
            "ns",
        ));
        records.push(self.check_min(
            "tSU:STO (STOP setup)",
            wave.t_su_sto_ns,
            p.t_su_sto_min_ns as f64,
            "ns",
        ));
        records.push(self.check_min(
            "tBUF (bus free time)",
            wave.t_buf_ns,
            p.t_buf_min_ns as f64,
            "ns",
        ));
        records.push(self.check_min(
            "tSU:DAT (data setup)",
            wave.t_su_dat_ns,
            p.t_su_dat_min_ns as f64,
            "ns",
        ));
        if let Some(hd_max) = p.t_hd_dat_max_ns {
            records.push(self.check_max(
                "tHD:DAT (data hold)",
                wave.t_hd_dat_ns,
                hd_max as f64,
                "ns",
            ));
        }
        records.push(self.check_max(
            "tr (SDA rise time)",
            wave.t_r_ns,
            p.t_r_max_ns as f64,
            "ns",
        ));
        records.push(self.check_max(
            "tf (SDA fall time)",
            wave.t_f_ns,
            p.t_f_max_ns as f64,
            "ns",
        ));

        // Voltage checks
        records.push(self.check_max(
            "VOL (output low)",
            wave.vol_measured_v,
            p.v_ol_max_v,
            "V",
        ));
        let voh_min = wave.vdd_v - 0.5;
        records.push(self.check_min(
            "VOH (output high)",
            wave.voh_measured_v,
            voh_min,
            "V",
        ));

        ComplianceReport {
            mode_name: p.name.to_string(),
            records,
        }
    }
}

/// Pull-up resistor compliance calculator
pub struct PullupChecker {
    pub vdd_v: f64,
    pub vol_max_v: f64,
    pub iol_ma: f64,
    pub cb_pf: f64,
    pub tr_max_ns: f64,
}

impl PullupChecker {
    pub fn rp_min_ohm(&self) -> f64 {
        (self.vdd_v - self.vol_max_v) / (self.iol_ma / 1000.0)
    }

    pub fn rp_max_ohm(&self) -> f64 {
        (self.tr_max_ns * 1e-9) / (0.8473 * self.cb_pf * 1e-12)
    }

    pub fn is_compliant(&self, rp_actual_ohm: f64) -> bool {
        rp_actual_ohm >= self.rp_min_ohm() && rp_actual_ohm <= self.rp_max_ohm()
    }
}

/// I2C bus event types for protocol compliance
#[derive(Debug, Clone, PartialEq)]
pub enum BusEvent {
    Start,
    RepeatedStart,
    Stop,
    DataBit(bool),
    Violation(String),
}

/// Bus sample (logic analyzer sample point)
#[derive(Debug, Clone, Copy)]
pub struct BusSample {
    pub timestamp_ns: f64,
    pub scl: bool,
    pub sda: bool,
}

/// Protocol-level compliance analyzer
pub struct ProtocolAnalyzer {
    params: I2cModeParams,
}

impl ProtocolAnalyzer {
    pub fn new(params: I2cModeParams) -> Self {
        Self { params }
    }

    /// Decode bus events from a sequence of samples
    pub fn decode(&self, samples: &[BusSample]) -> Vec<(f64, BusEvent)> {
        let mut events = Vec::new();

        for window in samples.windows(2) {
            let prev = &window[0];
            let curr = &window[1];

            // Both SCL high: detect START or STOP
            if prev.scl && curr.scl {
                if prev.sda && !curr.sda {
                    events.push((curr.timestamp_ns, BusEvent::Start));
                } else if !prev.sda && curr.sda {
                    events.push((curr.timestamp_ns, BusEvent::Stop));
                }
            }
            // SCL rises: sample data
            if !prev.scl && curr.scl {
                events.push((curr.timestamp_ns, BusEvent::DataBit(curr.sda)));
            }
        }

        events
    }

    /// Validate that a decoded byte matches expected value
    pub fn validate_byte(bits: &[bool]) -> Option<u8> {
        if bits.len() != 8 {
            return None;
        }
        let mut byte = 0u8;
        for &bit in bits {
            byte = (byte << 1) | (bit as u8);
        }
        Some(byte)
    }

    /// Check for clock stretching compliance
    /// Returns None if compliant, Some(duration_ns) of violation if not
    pub fn check_clock_stretch(
        &self,
        samples: &[BusSample],
        max_stretch_ns: f64,
    ) -> Option<f64> {
        let mut stretch_start: Option<f64> = None;

        for s in samples {
            if !s.scl {
                // SCL held low
                if stretch_start.is_none() {
                    stretch_start = Some(s.timestamp_ns);
                }
            } else if let Some(start) = stretch_start {
                let duration = s.timestamp_ns - start;
                if duration > max_stretch_ns {
                    return Some(duration);
                }
                stretch_start = None;
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_capture() -> WaveformCapture {
        WaveformCapture {
            scl_freq_hz: 395_000.0,
            scl_low_ns: 1450.0,
            scl_high_ns: 680.0,
            t_hd_sta_ns: 620.0,
            t_su_sta_ns: 710.0,
            t_su_sto_ns: 650.0,
            t_buf_ns: 1500.0,
            t_su_dat_ns: 115.0,
            t_hd_dat_ns: 450.0,
            t_r_ns: 290.0,
            t_f_ns: 180.0,
            vdd_v: 3.3,
            vol_measured_v: 0.35,
            voh_measured_v: 2.95,
        }
    }

    #[test]
    fn test_fast_mode_compliant_capture() {
        let checker = I2cComplianceChecker::new(I2cModeParams::FAST_MODE);
        let report = checker.check(&sample_capture());
        assert!(report.is_compliant(), "Expected compliant capture to pass");
    }

    #[test]
    fn test_scl_too_fast_fails() {
        let mut cap = sample_capture();
        cap.scl_freq_hz = 420_000.0; // Exceeds 400 kHz limit
        let checker = I2cComplianceChecker::new(I2cModeParams::FAST_MODE);
        let report = checker.check(&cap);
        assert!(!report.is_compliant(), "Over-frequency should fail");
    }

    #[test]
    fn test_pullup_compliance() {
        let pu = PullupChecker {
            vdd_v: 3.3,
            vol_max_v: 0.4,
            iol_ma: 3.0,
            cb_pf: 100.0,
            tr_max_ns: 300.0,
        };
        assert!(pu.is_compliant(1500.0));
        assert!(!pu.is_compliant(100.0));   // Too low (exceeds IOL)
        assert!(!pu.is_compliant(50_000.0)); // Too high (rise time too slow)
    }

    #[test]
    fn test_protocol_decode_start_stop() {
        let samples = vec![
            BusSample { timestamp_ns: 0.0,   scl: true,  sda: true  },
            BusSample { timestamp_ns: 100.0, scl: true,  sda: false }, // START
            BusSample { timestamp_ns: 200.0, scl: false, sda: false },
            BusSample { timestamp_ns: 300.0, scl: false, sda: true  },
            BusSample { timestamp_ns: 400.0, scl: true,  sda: true  }, // STOP
        ];

        let analyzer = ProtocolAnalyzer::new(I2cModeParams::FAST_MODE);
        let events = analyzer.decode(&samples);

        assert!(events.iter().any(|(_, e)| *e == BusEvent::Start));
        assert!(events.iter().any(|(_, e)| *e == BusEvent::Stop));
    }
}

fn main() {
    let capture = WaveformCapture {
        scl_freq_hz: 395_000.0,
        scl_low_ns: 1450.0,
        scl_high_ns: 680.0,
        t_hd_sta_ns: 620.0,
        t_su_sta_ns: 710.0,
        t_su_sto_ns: 650.0,
        t_buf_ns: 1500.0,
        t_su_dat_ns: 115.0,
        t_hd_dat_ns: 450.0,
        t_r_ns: 290.0,
        t_f_ns: 180.0,
        vdd_v: 3.3,
        vol_measured_v: 0.35,
        voh_measured_v: 2.95,
    };

    let checker = I2cComplianceChecker::new(I2cModeParams::FAST_MODE)
        .with_marginal_threshold(0.05);

    let report = checker.check(&capture);
    println!("{}", report);

    // Pull-up check
    let pu = PullupChecker {
        vdd_v: 3.3,
        vol_max_v: 0.4,
        iol_ma: 3.0,
        cb_pf: 100.0,
        tr_max_ns: 300.0,
    };
    println!("\nPull-up Compliance:");
    println!("  Rp min: {:.0} Ω", pu.rp_min_ohm());
    println!("  Rp max: {:.0} Ω", pu.rp_max_ohm());

    let rp_actual = 1500.0_f64;
    println!(
        "  Rp actual: {:.0} Ω => {}",
        rp_actual,
        if pu.is_compliant(rp_actual) { "COMPLIANT" } else { "NON-COMPLIANT" }
    );

    std::process::exit(if report.is_compliant() { 0 } else { 1 });
}
```

---

## Test Automation and Frameworks

### 9.1 Automated Test Pipeline

A complete compliance test pipeline integrates:

1. **Logic Analyzer / Oscilloscope** — captures raw SCL/SDA waveforms with timestamps
2. **CSV/Binary Export** — waveform data exported to a file for offline analysis
3. **Compliance Engine** (C or Rust library) — loads waveform, runs all checks
4. **Report Generator** — produces HTML/PDF report with pass/fail per parameter
5. **Regression Gate** — CI/CD integration; fails build on any compliance failure

### 9.2 Regression Test Example

```c
/* compliance_test_suite.c — run as part of CI */

static const struct {
    const char *name;
    I2C_WaveformCapture capture;
    bool expect_pass;
} test_cases[] = {
    {
        "FM nominal operation",
        {
            .scl_freq_hz    = 395000,  .scl_low_ns  = 1400,
            .scl_high_ns    = 700,     .t_hd_sta_ns = 700,
            .t_su_sta_ns    = 800,     .t_su_sto_ns = 700,
            .t_buf_ns       = 1500,    .t_su_dat_ns = 150,
            .t_hd_dat_ns    = 400,     .t_r_ns      = 200,
            .t_f_ns         = 150,     .vdd_v       = 3.3,
            .vol_measured_v = 0.30,    .voh_measured_v = 3.0,
        },
        .expect_pass = true,
    },
    {
        "FM SCL too fast",
        {
            .scl_freq_hz    = 420000,  .scl_low_ns  = 1100,
            .scl_high_ns    = 500,     .t_hd_sta_ns = 700,
            .t_su_sta_ns    = 800,     .t_su_sto_ns = 700,
            .t_buf_ns       = 1500,    .t_su_dat_ns = 150,
            .t_hd_dat_ns    = 400,     .t_r_ns      = 200,
            .t_f_ns         = 150,     .vdd_v       = 3.3,
            .vol_measured_v = 0.30,    .voh_measured_v = 3.0,
        },
        .expect_pass = false,
    },
    {
        "FM VOL out of spec",
        {
            .scl_freq_hz    = 395000,  .scl_low_ns  = 1400,
            .scl_high_ns    = 700,     .t_hd_sta_ns = 700,
            .t_su_sta_ns    = 800,     .t_su_sto_ns = 700,
            .t_buf_ns       = 1500,    .t_su_dat_ns = 150,
            .t_hd_dat_ns    = 400,     .t_r_ns      = 200,
            .t_f_ns         = 150,     .vdd_v       = 3.3,
            .vol_measured_v = 0.55,    /* FAIL: > 0.4V limit */
            .voh_measured_v = 3.0,
        },
        .expect_pass = false,
    },
};

int run_compliance_suite(void)
{
    int failures = 0;
    const I2C_ModeParams *fm = &i2c_params[1]; /* Fast Mode */

    for (size_t i = 0; i < sizeof(test_cases)/sizeof(test_cases[0]); i++) {
        ComplianceReport r = i2c_run_compliance(&test_cases[i].capture, fm);
        bool passed = (r.fail_count == 0);
        bool expected = test_cases[i].expect_pass;

        printf("[%s] %s: %s\n",
               (passed == expected) ? "OK" : "UNEXPECTED",
               test_cases[i].name,
               passed ? "PASS" : "FAIL");

        if (passed != expected) failures++;
    }

    return failures;
}
```

---

## Common Compliance Failures and Fixes

### 10.1 Rise Time Too Slow

**Symptom:** `tr` exceeds specification limit (e.g., >300 ns for FM).

**Cause:** Pull-up resistor too large relative to bus capacitance.

**Fix:** Reduce Rp value, or add an active pull-up (GPIO open-drain + controlled drive during rise), or use an I2C buffer with fast slew rate.

### 10.2 SCL Frequency Exceeds Maximum

**Symptom:** Logic analyzer shows fSCL > 400 kHz in Fast Mode.

**Cause:** Prescaler or clock divider misconfigured; system clock frequency changed after I2C init.

**Fix:** Recalculate timing registers. Always derive fSCL from the actual peripheral clock (`PCLK`), not a compile-time assumption.

### 10.3 tSU:DAT Violation

**Symptom:** Data bit on SDA changes too close to the rising edge of SCL.

**Cause:** SDA is being driven after SCL is already transitioning, violating the 100 ns (FM) setup margin.

**Fix:** Increase `SCLDEL` register field (STM32) or equivalent. Ensure SDA is stable before SCL rises.

### 10.4 Arbitration Lost Spuriously

**Symptom:** Master reports arbitration loss even though it is the only master.

**Cause:** Electrical noise on SDA is interpreted as another master driving LOW.

**Fix:** Add filtering capacitance on SDA/SCL; check PCB layout for long, unshielded traces; ensure proper pull-up sizing.

### 10.5 Clock Stretching Timeout

**Symptom:** Master times out waiting for peripheral to release SCL.

**Cause:** Peripheral is stretching longer than the master's timeout, or the peripheral has a bug that prevents SCL release.

**Fix:** Increase master timeout register; verify peripheral firmware releases SCL after ISR completion; check for nested interrupts blocking the peripheral's response.

---

## Summary

I2C compliance testing is a structured discipline that validates three interdependent layers — electrical, timing, and protocol — against the NXP UM10204 specification.

**Key takeaways:**

- **Electrical compliance** ensures correct voltage levels and pull-up resistor sizing. The resistor value must be bounded: low enough to guarantee VOL at the specified IOL, and high enough to allow rise times within spec for the given bus capacitance.

- **Timing compliance** covers a set of minimum and maximum constraints on SCL frequency, clock high/low periods, setup/hold times around START, STOP, and data transitions, and bus free time. All parameters are mode-specific and must be re-validated when changing clock frequencies or operating modes.

- **Protocol compliance** verifies correct generation and detection of START, STOP, repeated START conditions, proper ACK/NACK handling, arbitration behavior in multi-master systems, and 10-bit addressing sequences.

- **Software compliance frameworks** (demonstrated in C and Rust above) automate the checking of captured waveform data against specification tables, calculate marginal cases, and integrate into CI/CD pipelines to prevent regressions.

- **Common failure modes** include oversized pull-up resistors causing slow rise times, misconfigured timing registers after a clock frequency change, insufficient data setup time (tSU:DAT), and clock stretching timeouts due to peripheral firmware delays.

A compliant I2C implementation is the foundation for interoperability across the ecosystem. Testing should be performed at initial bring-up, after any hardware revision, after any firmware change that modifies clock configuration, and at temperature and voltage extremes for production qualification.

---

*References: NXP UM10204 I2C-bus specification and user manual, Rev. 7.0 — October 2021.*