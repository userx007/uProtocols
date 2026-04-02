# 73. I2C Temperature Testing

**Theory** — why temperature degrades I2C reliability (leakage current, threshold voltage drift, oscillator frequency shift, pull-up resistor tolerance, and PCB/connector fatigue).

**Standards** — a table mapping commercial, industrial, and automotive grade temperature ranges to relevant standards (AEC-Q100, IEC 60068, MIL-STD-883).

**Key parameters** — a reference table of every I2C timing and electrical parameter (t_r, t_f, t_SU, f_SCL, V_OL, leakage) that must be verified at temperature extremes.

**C/C++ code examples:**
- Linux `i2c-dev` based transaction harness with retry logic, latency stats, and per-temperature pass/fail reporting
- Bare-metal ARM Cortex-M DWT cycle-counter timing measurement stub
- `libgpiod`-based stuck-SDA bus recovery tool

**Rust code examples:**
- Full test harness using `linux-embedded-hal` + `embedded-hal` traits, with retry logic and latency statistics
- CSV logger for post-processing results
- 7-bit address bus scan to verify device presence at each temperature
- PRBS register integrity test (write known pattern, read back, compare)

**Test procedure** — chamber soak sequence, test point selection, acceptance criteria table, and a failure mode root-cause reference table.

## Verifying I2C Operation Across Extended Temperature Ranges

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Temperature Affects I2C](#why-temperature-affects-i2c)
3. [Temperature Ranges and Standards](#temperature-ranges-and-standards)
4. [Key I2C Parameters Affected by Temperature](#key-i2c-parameters-affected-by-temperature)
5. [Test Strategy and Setup](#test-strategy-and-setup)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Automated Test Sequences](#automated-test-sequences)
9. [Interpreting Results and Pass/Fail Criteria](#interpreting-results-and-passfail-criteria)
10. [Summary](#summary)

---

## Introduction

I2C (Inter-Integrated Circuit) buses are used in a wide range of embedded systems, from consumer electronics to industrial controllers and automotive ECUs. While I2C is well understood at room temperature (25 °C), real-world deployments expose devices to environments ranging from arctic cold (−40 °C) to desert heat (>85 °C) or even higher in automotive under-hood applications (>125 °C).

Temperature testing verifies that the I2C bus remains **electrically reliable**, that devices **correctly respond** to transactions, and that **timing parameters** remain within specification across the entire operating temperature range. Skipping this step is a common source of field failures that are difficult to reproduce on the bench.

---

## Why Temperature Affects I2C

Temperature changes affect the physical and electrical properties of semiconductors and passive components in several ways relevant to I2C operation:

### Semiconductor Behaviour

- **Threshold voltages** (V_IL, V_IH) drift with temperature. MOSFETs and BJTs in I/O cells change their switching points as carrier mobility and threshold voltage vary.
- **Leakage currents** increase exponentially with temperature (roughly doubling every 10 °C). At high temperatures, leakage on SDA/SCL can pull lines low even when the bus should be idle.
- **Drive strength** of open-drain output stages changes; at high temperature, sink current capability may drop, slowing fall times.

### Passive Components

- **Pull-up resistors** have temperature coefficients (typically ±100–200 ppm/°C for carbon film, ±25 ppm/°C for metal film). A pull-up that correctly terminates the bus at 25 °C may be too high or too low at temperature extremes.
- **Parasitic capacitance** on PCB traces changes slightly with temperature, affecting RC time constants and therefore rise times.

### Oscillator / Clock Sources

- **Clock frequency drift**: I2C masters generate SCL from a system clock. Crystal oscillators drift (typically ±20–50 ppm), while RC oscillators drift significantly more. At extreme temperatures, SCL frequency may fall outside the tolerance window that slave devices expect.
- **Setup and hold times** depend on clock period; if the master clock speeds up or slows down, timing margins change.

### Connector and PCB Effects

- Thermal expansion causes micro-fractures in solder joints and changes contact resistance in connectors, potentially introducing intermittent open-circuit faults.
- PCB substrate dielectric properties (FR4 Tg ~130–180 °C) change above their glass transition temperature.

---

## Temperature Ranges and Standards

| Classification       | Typical Range          | Common Standards            |
|----------------------|------------------------|-----------------------------|
| Commercial           | 0 °C to +70 °C         | Consumer electronics        |
| Industrial           | −40 °C to +85 °C       | IEC 60068, MIL-STD-810      |
| Automotive Grade 2   | −40 °C to +105 °C      | AEC-Q100 Grade 2            |
| Automotive Grade 1   | −40 °C to +125 °C      | AEC-Q100 Grade 1            |
| Automotive Grade 0   | −40 °C to +150 °C      | AEC-Q100 Grade 0            |
| Military / Aerospace | −55 °C to +125/+175 °C | MIL-STD-883, DO-160         |

The **I2C specification** (NXP UM10204) itself does not define a temperature range — it is left to the device datasheet. Always cross-reference the I2C peripheral's datasheet with the target deployment range.

---

## Key I2C Parameters Affected by Temperature

The following electrical and timing parameters must be verified at temperature extremes (and at intermediate points for thorough characterisation):

| Parameter      | Symbol    | Standard Mode Spec  | Fast Mode Spec  | Affected By                         |
|----------------|-----------|---------------------|-----------------|-------------------------------------|
| SCL frequency  | f_SCL     | ≤ 100 kHz           | ≤ 400 kHz       | Master clock drift                  |
| Rise time      | t_r       | ≤ 1000 ns           | 20–300 ns       | Pull-up R, bus capacitance          |
| Fall time      | t_f       | ≤ 300 ns            | 20–300 ns       | Driver sink current, temperature    |
| Setup time SDA | t_SU;DAT  | ≥ 250 ns            | ≥ 100 ns        | Clock frequency, leakage            |
| Hold time SDA  | t_HD;DAT  | ≥ 0 ns              | ≥ 0 ns          | Driver strength                     |
| LOW period     | t_LOW     | ≥ 4.7 µs            | ≥ 1.3 µs        | Master clock source                 |
| HIGH period    | t_HIGH    | ≥ 4.0 µs            | ≥ 0.6 µs        | Master clock source                 |
| V_OL (output)  | V_OL      | ≤ 0.4 V at 3 mA     | ≤ 0.4 V         | Driver sink strength vs. temperature|
| Leakage        | I_leak    | ≤ 10 µA             | ≤ 10 µA         | Exponential with temperature        |

---

## Test Strategy and Setup

### Equipment

- **Thermal chamber** (environmental oven / temperature cycling chamber)
- **I2C analyser / logic analyser** with protocol decode (e.g., Saleae Logic, Rigol, Tektronix)
- **Oscilloscope** with ≥200 MHz bandwidth (to capture rise/fall times accurately)
- **Current probe or multimeter** (to measure pull-up current at temperature)
- **DUT board** with connectors rated for the target temperature range
- **Temperature sensors** (placed directly on or near the I2C bus components on the DUT)

### Test Points

Instrument the following on the PCB:
- SDA line (test point before and after the bus buffer, if used)
- SCL line
- V_CC of I2C master and slaves
- Pull-up resistor junction (V_PULL)

### Temperature Soak Procedure

1. Set chamber to target temperature.
2. Allow **thermal soak** time: minimum 15 minutes for small PCBs, up to 60 minutes for dense assemblies. Monitor the on-board temperature sensors to confirm stabilisation.
3. Begin automated I2C test sequence (see code examples below).
4. Log all results with timestamps and actual temperature readings.
5. Repeat at: −40 °C, −20 °C, 0 °C, +25 °C (reference), +40 °C, +60 °C, +85 °C, and target maximum.

### What to Test at Each Temperature Point

- **Bus scan** (address discovery) — confirms all expected slaves respond
- **Read/write register transactions** — verifies data integrity
- **Repeated-start transactions** — tests combined format reliability
- **10-bit addressing** (if used)
- **Clock stretching** (if slaves implement it)
- **Error injection and recovery** — bus lockup recovery (SDA stuck low)
- **Timing capture** — SCL frequency, rise/fall times, setup/hold margins

---

## C/C++ Implementation

### 1. Basic I2C Health Check with Linux I2C-dev

```c
/**
 * i2c_temp_test.c
 * I2C temperature test harness using Linux i2c-dev API.
 * Compile: gcc -o i2c_temp_test i2c_temp_test.c -lrt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define I2C_BUS         "/dev/i2c-1"
#define SENSOR_ADDR     0x48          /* e.g. TMP102 or LM75 */
#define TEST_REGISTER   0x00          /* Temperature register */
#define ITERATIONS      1000          /* Transactions per temperature point */
#define RETRY_LIMIT     3

/* ---------- Low-level helpers ---------- */

static int i2c_open(const char *bus) {
    int fd = open(bus, O_RDWR);
    if (fd < 0) {
        perror("Failed to open I2C bus");
    }
    return fd;
}

static int i2c_set_slave(int fd, uint8_t addr) {
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        perror("Failed to set I2C slave address");
        return -1;
    }
    return 0;
}

/**
 * Write a register address then read back n bytes.
 * Returns 0 on success, negative on failure.
 */
static int i2c_read_register(int fd, uint8_t reg, uint8_t *buf, size_t len) {
    struct i2c_rdwr_ioctl_data msgset;
    struct i2c_msg msgs[2];

    msgs[0].addr  = SENSOR_ADDR;
    msgs[0].flags = 0;               /* Write */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    msgs[1].addr  = SENSOR_ADDR;
    msgs[1].flags = I2C_M_RD;       /* Read */
    msgs[1].len   = (uint16_t)len;
    msgs[1].buf   = buf;

    msgset.msgs  = msgs;
    msgset.nmsgs = 2;

    return (ioctl(fd, I2C_RDWR, &msgset) < 0) ? -errno : 0;
}

/* ---------- Test result structure ---------- */

typedef struct {
    double   temperature_c;      /* Chamber temperature (°C)         */
    uint32_t transactions;       /* Total attempted                  */
    uint32_t failures;           /* Hard failures (no ACK / timeout) */
    uint32_t crc_errors;         /* Data integrity errors            */
    uint32_t retries_needed;     /* Transactions needing a retry     */
    double   min_latency_us;     /* Minimum transaction latency (µs) */
    double   max_latency_us;
    double   avg_latency_us;
} TestResult;

/* ---------- Monotonic time helper ---------- */

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

/* ---------- Simple pseudo-checksum for data integrity ---------- */

static uint8_t checksum_u16(uint8_t hi, uint8_t lo) {
    return (uint8_t)((hi ^ lo) & 0xFF);
}

/* ---------- Core test function ---------- */

int run_i2c_temperature_test(double chamber_temp_c, TestResult *result) {
    int fd = i2c_open(I2C_BUS);
    if (fd < 0) return -1;
    if (i2c_set_slave(fd, SENSOR_ADDR) < 0) { close(fd); return -1; }

    memset(result, 0, sizeof(*result));
    result->temperature_c  = chamber_temp_c;
    result->min_latency_us = 1e12;  /* Will be replaced on first iteration */

    double total_latency = 0.0;

    for (uint32_t i = 0; i < ITERATIONS; i++) {
        uint8_t buf[2]  = {0};
        int     ret     = -1;
        int     attempt = 0;

        double t_start = now_us();

        /* Retry loop */
        while (attempt < RETRY_LIMIT) {
            ret = i2c_read_register(fd, TEST_REGISTER, buf, 2);
            if (ret == 0) break;
            attempt++;
            result->retries_needed++;
            usleep(100);   /* Brief recovery pause before retry */
        }

        double latency = now_us() - t_start;

        result->transactions++;

        if (ret != 0) {
            result->failures++;
            fprintf(stderr, "[%.1f°C] Transaction %u failed after %d retries: %s\n",
                    chamber_temp_c, i, attempt, strerror(-ret));
            continue;
        }

        /* Integrity check: verify hi/lo bytes not both 0xFF (stuck bus) */
        if (buf[0] == 0xFF && buf[1] == 0xFF) {
            result->crc_errors++;
        }

        /* Update latency stats */
        if (latency < result->min_latency_us) result->min_latency_us = latency;
        if (latency > result->max_latency_us) result->max_latency_us = latency;
        total_latency += latency;
    }

    result->avg_latency_us = total_latency / (double)result->transactions;
    close(fd);
    return 0;
}

/* ---------- Report printer ---------- */

void print_result(const TestResult *r) {
    printf("============================================================\n");
    printf("Chamber Temperature : %.1f °C\n",   r->temperature_c);
    printf("Transactions        : %u\n",         r->transactions);
    printf("Failures            : %u  (%.2f%%)\n",
           r->failures, 100.0 * r->failures / r->transactions);
    printf("Data Errors         : %u\n",         r->crc_errors);
    printf("Retries Needed      : %u\n",         r->retries_needed);
    printf("Latency min/avg/max : %.1f / %.1f / %.1f µs\n",
           r->min_latency_us, r->avg_latency_us, r->max_latency_us);
    printf("PASS/FAIL           : %s\n",
           (r->failures == 0 && r->crc_errors == 0) ? "PASS" : "FAIL");
    printf("============================================================\n");
}

/* ---------- Main ---------- */

int main(void) {
    /*
     * In a real setup, chamber_temp is read from a calibrated thermocouple
     * or the chamber's RS-232/GPIB interface. Here it is supplied manually.
     */
    double test_temps[] = { -40.0, -20.0, 0.0, 25.0, 40.0, 60.0, 85.0 };
    size_t n_temps = sizeof(test_temps) / sizeof(test_temps[0]);

    for (size_t t = 0; t < n_temps; t++) {
        printf("\n>>> Waiting for operator to set chamber to %.1f °C and press ENTER...\n",
               test_temps[t]);
        getchar();   /* In automated setups, replace with chamber soak confirmation */

        TestResult result;
        if (run_i2c_temperature_test(test_temps[t], &result) == 0) {
            print_result(&result);
        } else {
            fprintf(stderr, "ERROR: Could not open I2C bus at %.1f °C\n", test_temps[t]);
        }
    }

    return 0;
}
```

---

### 2. Timing Measurement Stub (Embedded / Bare-Metal C)

On bare-metal microcontrollers, software timing can supplement or replace an oscilloscope for latency logging:

```c
/**
 * bare_metal_i2c_timing.c
 * Bare-metal I2C timing capture (ARM Cortex-M DWT cycle counter example).
 * Adapt I2C_WriteRead() to your HAL (STM32 HAL, NXP SDK, etc.)
 */

#include <stdint.h>
#include <stdbool.h>

/* ---- DWT cycle counter (ARM Cortex-M3/M4/M7) ---- */
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define CoreDebug_DEMCR (*(volatile uint32_t *)0xE000EDFC)

static void dwt_enable(void) {
    CoreDebug_DEMCR |= (1u << 24);   /* Enable trace */
    DWT_CYCCNT  = 0;
    DWT_CTRL   |= 1u;                /* Enable cycle counter */
}

static inline uint32_t dwt_cycles(void) { return DWT_CYCCNT; }

/* ---- I2C abstraction (replace with your HAL) ---- */
extern bool I2C_WriteRead(uint8_t dev_addr,
                          const uint8_t *wr_buf, uint16_t wr_len,
                          uint8_t *rd_buf,       uint16_t rd_len);

/* ---- Timing test ---- */

#define SAMPLES     512
#define SENSOR_ADDR 0x48
#define REG_TEMP    0x00
#define CPU_HZ      168000000UL    /* Adjust to your MCU */

typedef struct {
    uint32_t cycles_min;
    uint32_t cycles_max;
    uint64_t cycles_sum;
    uint32_t fail_count;
    uint32_t sample_count;
} I2cTimingStats;

void i2c_timing_test(I2cTimingStats *stats) {
    uint8_t reg   = REG_TEMP;
    uint8_t buf[2];

    stats->cycles_min   = UINT32_MAX;
    stats->cycles_max   = 0;
    stats->cycles_sum   = 0;
    stats->fail_count   = 0;
    stats->sample_count = SAMPLES;

    dwt_enable();

    for (uint32_t i = 0; i < SAMPLES; i++) {
        uint32_t t0 = dwt_cycles();
        bool ok = I2C_WriteRead(SENSOR_ADDR, &reg, 1, buf, 2);
        uint32_t dt = dwt_cycles() - t0;

        if (!ok) {
            stats->fail_count++;
            continue;
        }

        if (dt < stats->cycles_min) stats->cycles_min = dt;
        if (dt > stats->cycles_max) stats->cycles_max = dt;
        stats->cycles_sum += dt;
    }
}

void i2c_print_timing_stats(const I2cTimingStats *s) {
    uint32_t success = s->sample_count - s->fail_count;
    uint32_t avg     = (success > 0) ? (uint32_t)(s->cycles_sum / success) : 0;

    /* Convert cycles to microseconds */
    uint32_t min_us = (s->cycles_min * 1000) / (CPU_HZ / 1000);
    uint32_t max_us = (s->cycles_max * 1000) / (CPU_HZ / 1000);
    uint32_t avg_us = (avg           * 1000) / (CPU_HZ / 1000);

    /* Output via UART/ITM/semihosting — replace printf as needed */
    printf("Samples: %u  Failures: %u\n", s->sample_count, s->fail_count);
    printf("Latency (µs): min=%u  avg=%u  max=%u\n", min_us, avg_us, max_us);
}
```

---

### 3. Bus Recovery Check (C — Stuck-SDA Detection)

At high temperatures, leakage can hold SDA low and lock the bus. Detect and recover:

```c
/**
 * Checks whether the I2C bus is stuck and attempts recovery by clocking SCL.
 * Uses Linux GPIO (libgpiod) to bit-bang recovery pulses.
 *
 * Requires: libgpiod  (apt install libgpiod-dev)
 * Compile:  gcc -o i2c_recovery i2c_recovery.c -lgpiod
 */

#include <stdio.h>
#include <unistd.h>
#include <gpiod.h>

#define GPIO_CHIP   "/dev/gpiochip0"
#define SCL_LINE    11
#define SDA_LINE    10
#define RECOVERY_CLOCKS 9       /* NXP I2C spec recommends up to 9 clocks */

int i2c_bus_recovery(void) {
    struct gpiod_chip *chip = gpiod_chip_open(GPIO_CHIP);
    if (!chip) { perror("gpiod_chip_open"); return -1; }

    struct gpiod_line *scl = gpiod_chip_get_line(chip, SCL_LINE);
    struct gpiod_line *sda = gpiod_chip_get_line(chip, SDA_LINE);

    gpiod_line_request_output(scl, "i2c_recover", 1);
    gpiod_line_request_input (sda, "i2c_recover");

    int sda_val = gpiod_line_get_value(sda);
    if (sda_val == 1) {
        printf("SDA is HIGH — bus appears free.\n");
        gpiod_chip_close(chip);
        return 0;
    }

    printf("SDA stuck LOW — attempting recovery with %d clock pulses...\n",
           RECOVERY_CLOCKS);

    for (int i = 0; i < RECOVERY_CLOCKS; i++) {
        gpiod_line_set_value(scl, 0);
        usleep(5);   /* >4.7 µs LOW period (standard mode) */
        gpiod_line_set_value(scl, 1);
        usleep(5);   /* >4.0 µs HIGH period */

        sda_val = gpiod_line_get_value(sda);
        if (sda_val == 1) {
            printf("SDA released after %d clock(s).\n", i + 1);
            /* Send STOP condition */
            gpiod_line_request_output(sda, "i2c_recover", 0);
            usleep(5);
            gpiod_line_set_value(sda, 1);
            gpiod_chip_close(chip);
            return 0;
        }
    }

    fprintf(stderr, "ERROR: SDA still stuck after %d clocks. "
                    "Check for hardware fault.\n", RECOVERY_CLOCKS);
    gpiod_chip_close(chip);
    return -1;
}
```

---

## Rust Implementation

### 1. I2C Temperature Test with `linux-embedded-hal`

```rust
//! i2c_temp_test/src/main.rs
//!
//! I2C temperature testing using linux-embedded-hal.
//!
//! Cargo.toml dependencies:
//! [dependencies]
//! linux-embedded-hal = "0.4"
//! embedded-hal = "1.0"
//! anyhow = "1"

use std::time::{Duration, Instant};
use std::thread::sleep;
use std::io::{self, BufRead, Write};

use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;

const SENSOR_ADDR: u8  = 0x48;
const REG_TEMP: u8     = 0x00;
const ITERATIONS: u32  = 1_000;
const RETRY_LIMIT: u32 = 3;

// ─── Result type ─────────────────────────────────────────────────────────────

#[derive(Debug, Default)]
struct TestResult {
    temperature_c:   f64,
    transactions:    u32,
    failures:        u32,
    data_errors:     u32,
    retries:         u32,
    min_latency_us:  f64,
    max_latency_us:  f64,
    avg_latency_us:  f64,
}

impl TestResult {
    fn passed(&self) -> bool {
        self.failures == 0 && self.data_errors == 0
    }
}

// ─── Core test function ───────────────────────────────────────────────────────

fn run_test(chamber_temp: f64) -> anyhow::Result<TestResult> {
    let mut i2c = I2cdev::new("/dev/i2c-1")?;

    let mut result = TestResult {
        temperature_c:  chamber_temp,
        min_latency_us: f64::MAX,
        ..Default::default()
    };

    let mut total_latency = 0.0_f64;
    let mut successful    = 0_u32;

    for _ in 0..ITERATIONS {
        let mut buf = [0u8; 2];
        let mut attempt = 0_u32;
        let mut last_err: Option<String> = None;

        let t_start = Instant::now();

        let ok = loop {
            let res = i2c.write_read(SENSOR_ADDR, &[REG_TEMP], &mut buf);
            match res {
                Ok(_) => break true,
                Err(e) => {
                    attempt += 1;
                    last_err = Some(e.to_string());
                    if attempt >= RETRY_LIMIT {
                        break false;
                    }
                    result.retries += 1;
                    sleep(Duration::from_micros(100));
                }
            }
        };

        let latency_us = t_start.elapsed().as_secs_f64() * 1e6;
        result.transactions += 1;

        if !ok {
            result.failures += 1;
            eprintln!(
                "[{:.1}°C] Transaction failed after {} retries: {}",
                chamber_temp,
                attempt,
                last_err.unwrap_or_default()
            );
            continue;
        }

        // Integrity check — both bytes 0xFF indicates a stuck/floating bus
        if buf[0] == 0xFF && buf[1] == 0xFF {
            result.data_errors += 1;
        }

        if latency_us < result.min_latency_us { result.min_latency_us = latency_us; }
        if latency_us > result.max_latency_us { result.max_latency_us = latency_us; }
        total_latency += latency_us;
        successful    += 1;
    }

    if successful > 0 {
        result.avg_latency_us = total_latency / successful as f64;
    }
    if result.min_latency_us == f64::MAX {
        result.min_latency_us = 0.0;
    }

    Ok(result)
}

// ─── Reporter ─────────────────────────────────────────────────────────────────

fn print_result(r: &TestResult) {
    let failure_pct = 100.0 * r.failures as f64 / r.transactions.max(1) as f64;
    println!("============================================================");
    println!("Chamber Temperature : {:.1} °C",   r.temperature_c);
    println!("Transactions        : {}",           r.transactions);
    println!("Failures            : {} ({:.2}%)", r.failures, failure_pct);
    println!("Data Errors         : {}",           r.data_errors);
    println!("Retries             : {}",           r.retries);
    println!(
        "Latency min/avg/max : {:.1} / {:.1} / {:.1} µs",
        r.min_latency_us, r.avg_latency_us, r.max_latency_us
    );
    println!("Result              : {}", if r.passed() { "PASS ✓" } else { "FAIL ✗" });
    println!("============================================================");
}

// ─── Main ─────────────────────────────────────────────────────────────────────

fn main() -> anyhow::Result<()> {
    let test_temps = [-40.0_f64, -20.0, 0.0, 25.0, 40.0, 60.0, 85.0];
    let stdin = io::stdin();

    for &temp in &test_temps {
        print!(
            "\n>>> Set chamber to {:.1} °C, soak, then press ENTER to start test...",
            temp
        );
        io::stdout().flush()?;
        let _ = stdin.lock().lines().next(); // wait for ENTER

        match run_test(temp) {
            Ok(result) => print_result(&result),
            Err(e)     => eprintln!("ERROR at {:.1}°C: {}", temp, e),
        }
    }

    Ok(())
}
```

---

### 2. Statistical Analysis and CSV Logger (Rust)

```rust
//! Extends the test framework to log results to a CSV file
//! for post-processing in Python, Excel, or a plotting tool.

use std::fs::OpenOptions;
use std::io::Write;

fn write_csv_header(path: &str) -> anyhow::Result<()> {
    let mut f = OpenOptions::new().create(true).truncate(true).write(true).open(path)?;
    writeln!(f,
        "temperature_c,transactions,failures,failure_pct,data_errors,\
         retries,min_latency_us,avg_latency_us,max_latency_us,passed"
    )?;
    Ok(())
}

fn append_csv_row(path: &str, r: &TestResult) -> anyhow::Result<()> {
    let mut f = OpenOptions::new().append(true).open(path)?;
    let failure_pct = 100.0 * r.failures as f64 / r.transactions.max(1) as f64;
    writeln!(
        f,
        "{:.1},{},{},{:.4},{},{},{:.2},{:.2},{:.2},{}",
        r.temperature_c,
        r.transactions,
        r.failures,
        failure_pct,
        r.data_errors,
        r.retries,
        r.min_latency_us,
        r.avg_latency_us,
        r.max_latency_us,
        r.passed()
    )?;
    Ok(())
}
```

---

### 3. I2C Bus Scan Across Temperature (Rust)

Scan all 7-bit addresses to confirm all expected devices respond:

```rust
//! Scans the I2C bus and returns found device addresses.

use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;

fn i2c_bus_scan() -> anyhow::Result<Vec<u8>> {
    let mut i2c = I2cdev::new("/dev/i2c-1")?;
    let mut found = Vec::new();
    let mut dummy = [0u8; 1];

    // Addresses 0x00–0x07 and 0x78–0x7F are reserved
    for addr in 0x08_u8..=0x77 {
        match i2c.read(addr, &mut dummy) {
            Ok(_)  => {
                println!("  [FOUND] 0x{:02X}", addr);
                found.push(addr);
            }
            Err(_) => {} // NAK = no device at this address
        }
    }
    Ok(found)
}

/// Verify that all expected devices are present at this temperature point.
fn verify_device_presence(
    expected: &[u8],
    chamber_temp: f64,
) -> anyhow::Result<bool> {
    let found = i2c_bus_scan()?;
    let mut all_present = true;

    for &addr in expected {
        if found.contains(&addr) {
            println!("[{:.1}°C] 0x{:02X} — PRESENT", chamber_temp, addr);
        } else {
            eprintln!("[{:.1}°C] 0x{:02X} — MISSING!", chamber_temp, addr);
            all_present = false;
        }
    }
    Ok(all_present)
}
```

---

### 4. Register Integrity Test with PRBS Pattern (Rust)

Write a pseudo-random byte sequence to a writable register, read it back, and verify:

```rust
//! Writes known patterns to a writable scratch/configuration register
//! and verifies the read-back at each temperature point.

use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;

const WRITABLE_REG: u8 = 0x01;  // e.g., TMP102 configuration register
const DEVICE_ADDR:  u8 = 0x48;

/// Simple maximal-length LFSR for PRBS pattern generation (16-bit).
fn lfsr_next(state: u16) -> u16 {
    let bit = ((state >> 15) ^ (state >> 13) ^ (state >> 12) ^ (state >> 10)) & 1;
    (state << 1) | bit
}

fn register_integrity_test(chamber_temp: f64) -> anyhow::Result<(u32, u32)> {
    let mut i2c   = I2cdev::new("/dev/i2c-1")?;
    let mut state = 0xACE1_u16;  // Non-zero seed
    let mut pass  = 0_u32;
    let mut fail  = 0_u32;

    for _ in 0..256 {
        state = lfsr_next(state);
        let pattern = (state & 0xFF) as u8;

        // Write
        i2c.write(DEVICE_ADDR, &[WRITABLE_REG, pattern])?;

        // Read back
        let mut rd = [0u8; 1];
        i2c.write_read(DEVICE_ADDR, &[WRITABLE_REG], &mut rd)?;

        if rd[0] == pattern {
            pass += 1;
        } else {
            eprintln!(
                "[{:.1}°C] Integrity FAIL: wrote 0x{:02X}, read 0x{:02X}",
                chamber_temp, pattern, rd[0]
            );
            fail += 1;
        }
    }

    println!(
        "[{:.1}°C] Register integrity: {} passed, {} failed",
        chamber_temp, pass, fail
    );
    Ok((pass, fail))
}
```

---

## Automated Test Sequences

A complete temperature test campaign typically combines the above building blocks:

```
for each temperature_point in test_plan:
    1. Set chamber temperature
    2. Wait for thermal soak (monitor DUT sensor)
    3. Run bus_scan()         → verify all expected devices respond
    4. Run timing_test()      → log latency statistics
    5. Run read_write_test()  → check data integrity at ITERATIONS
    6. Run register_integrity_test()  → PRBS pattern verification
    7. Run bus_recovery_test()        → simulate stuck SDA, verify recovery
    8. Log all results with timestamp and actual temperature
    9. Advance to next temperature_point
end for

Generate pass/fail report and latency vs. temperature chart.
```

### Acceptance Criteria (Example)

| Metric                  | Pass Threshold                 |
|-------------------------|--------------------------------|
| Failure rate            | < 0.1% per temperature point   |
| Data integrity errors   | 0                              |
| SCL rise time           | Within device datasheet spec   |
| Bus recovery            | SDA released within 9 clocks   |
| Device presence (scan)  | 100% of expected addresses ACK |

---

## Interpreting Results and Pass/Fail Criteria

### Common Failure Modes by Temperature

| Temperature | Symptom                         | Root Cause                                 | Fix                                             |
|-------------|---------------------------------|--------------------------------------------|-------------------------------------------------|
| Low (<0 °C) | Slow rise times, timeout errors | Pull-up value too high; capacitance up     | Lower pull-up resistor or add bus buffer        |
| Low (<0 °C) | Master clock too fast/slow      | RC oscillator drift                        | Use crystal-stabilised clock source             |
| High (>85°C)| SDA stuck low intermittently    | Excessive leakage current on bus lines     | Lower pull-up; use bus buffer with low leakage  |
| High (>85°C)| Address collisions / phantom ACK| Multiple slave leakage summing on SDA      | Reduce bus length, add buffer/isolator          |
| Any extreme | Register data corruption        | I/O threshold mismatch (V_IL shifts)       | Match V_CC to slave V_IO requirement            |
| Thermal cycling| Intermittent NO ACK          | Solder joint / connector fatigue           | X-ray, reflow, use temperature-rated connectors |

### Analysing Latency vs. Temperature Charts

Plot `avg_latency_us` against `temperature_c`. A healthy bus shows:
- Slight increase in latency at cold temperatures (slower rise times → SCL period lengthens if clock-stretching slave)
- Relatively flat response in the mid-range
- Small increase at high temperatures if the slave adds clock stretching due to internal timing changes

A sudden step change in latency (e.g., 3 µs → 12 µs) at a specific temperature point indicates the bus entering a marginal region and deserves investigation on an oscilloscope.

---

## Summary

Temperature testing for I2C is a systematic process of confirming that the bus meets electrical and timing requirements across the full operating range of the deployed product. Key takeaways:

**Why it matters:** Semiconductor leakage, I/O threshold drift, pull-up resistor tolerance, and clock source accuracy all vary with temperature. A bus that works at 25 °C may fail reliably at −40 °C or 85 °C without any observable fault at room temperature.

**What to test:** Bus scan (device presence), read/write data integrity, register pattern integrity, transaction latency, rise/fall timing, and stuck-bus recovery.

**C/C++ approach:** The Linux `i2c-dev` API provides `I2C_RDWR` ioctl for combined write-read transactions. On bare metal, DWT cycle counters on ARM Cortex-M give sub-microsecond latency measurement. `libgpiod` enables bit-bang bus recovery if the hardware I2C controller cannot release a stuck bus.

**Rust approach:** `linux-embedded-hal` wraps the kernel I2C device into the `embedded-hal` `I2c` trait. This allows the same test logic to run on Linux hosts (Raspberry Pi, BeagleBone) and, with a different HAL backend, on bare-metal targets. The type system enforces error handling, and the PRBS pattern test provides high-confidence data integrity verification.

**Automation:** Drive a thermal chamber programmatically (via GPIB, RS-232, or Ethernet), log actual DUT temperatures from on-board sensors, and script the full test sequence to produce a machine-readable CSV and human-readable pass/fail report. This catches corner-case failures that manual spot-checking misses.

**Acceptance:** Define clear numerical thresholds for failure rate, data integrity, latency bounds, and timing margins before testing begins. Any result exceeding those thresholds triggers a root-cause investigation rather than a subjective judgment call in the field.

---

*Document: 73 — I2C Temperature Testing | Revision 1.0*