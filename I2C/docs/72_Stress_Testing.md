# 72. I2C Stress Testing

**Concepts covered:**
- Why I2C is susceptible to stress failures (open-drain architecture, clock stretching, ACK timing)
- Bus saturation methodology — utilization formulas, ramp-up strategy, threshold detection
- Rapid transaction patterns — burst, interleaved R/W, variable-length, repeated START
- Reliability validation — soak testing, golden reference comparison
- Error injection — NACKs, clock stretching, glitches, incomplete transactions
- Bus recovery (NXP 9-clock-toggle procedure)
- Multi-master arbitration stress and thermal/environmental considerations

**Six code examples:**
| # | Language | Description |
|---|---|---|
| 1 | C | Linux `i2c-dev` saturation test with real-time TPS/latency/error-rate stats |
| 2 | C++ | Arduino/ESP32 burst patterns (rapid reads, interleaved R/W, variable length) |
| 3 | C | STM32 HAL golden reference reliability validator with auto bus recovery |
| 4 | Rust | Linux `i2cdev` saturation test with threaded statistics reporter |
| 5 | Rust | `embedded-hal` platform-independent reliability validator |
| 6 | Rust | Burst test with controlled error injection and recovery timing |

The **Summary** section consolidates all three testing pillars with guidance on interpreting KPI metrics and diagnosing common failure modes.


## Bus Saturation Tests, Rapid Transaction Patterns, and Reliability Validation

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Stress Test I2C?](#why-stress-test-i2c)
3. [Key Stress Testing Concepts](#key-stress-testing-concepts)
4. [Bus Saturation Testing](#bus-saturation-testing)
5. [Rapid Transaction Patterns](#rapid-transaction-patterns)
6. [Reliability Validation](#reliability-validation)
7. [Error Injection and Recovery Testing](#error-injection-and-recovery-testing)
8. [Thermal and Environmental Stress](#thermal-and-environmental-stress)
9. [Multi-Master Stress Testing](#multi-master-stress-testing)
10. [Automated Test Frameworks](#automated-test-frameworks)
11. [Code Examples in C/C++](#code-examples-in-cc)
12. [Code Examples in Rust](#code-examples-in-rust)
13. [Metrics and Analysis](#metrics-and-analysis)
14. [Summary](#summary)

---

## Introduction

I2C (Inter-Integrated Circuit) stress testing is the systematic process of subjecting an I2C bus and its connected devices to extreme operating conditions in order to identify weaknesses, validate reliability, and verify correct behavior under worst-case scenarios. Unlike functional testing—which verifies that the bus works correctly under normal conditions—stress testing intentionally pushes the system beyond typical operating parameters to expose latent defects, timing hazards, race conditions, and hardware design margins.

Stress testing is an essential phase in embedded systems development, particularly in safety-critical, industrial, automotive, and high-availability applications where unplanned downtime or communication failures are unacceptable. A device that passes functional tests may still fail in production due to edge cases that only manifest under sustained load, rapid transaction sequences, voltage fluctuations, or temperature extremes.

---

## Why Stress Test I2C?

The I2C protocol, while robust for many use cases, has several characteristics that make it susceptible to failure under stress:

**Open-drain bus architecture** means that pull-up resistors govern rise times. Under heavy load or at high temperatures, capacitance increases and the rise time degrades, potentially causing bus errors or clock stretching violations.

**Shared bus arbitration** across multiple masters can produce non-deterministic contention patterns that are difficult to reproduce without deliberate load generation.

**Clock stretching** by slow slave devices can block the entire bus, creating bottlenecks under high transaction rates.

**ACK/NACK handling** at high speeds is timing-sensitive. Missed ACKs, glitches from electrical noise, or firmware latency can cause silent data corruption or lockups.

**Bus lockup states** can occur when a transaction is interrupted mid-byte (e.g., by a reset or power glitch), leaving the bus in an inconsistent state that prevents further communication.

Stress testing addresses all these failure modes by creating controlled, reproducible conditions that expose them before deployment.

---

## Key Stress Testing Concepts

### Bus Utilization (Saturation)

Bus utilization expresses how much of the theoretical bus bandwidth is consumed by active transactions. At 100% utilization, the bus carries transactions continuously with no idle time. Practical saturation testing targets 80–100% utilization to evaluate bus behavior at and beyond design capacity.

**Formula:**

```
Bus Utilization (%) = (Active Transaction Time / Total Time) × 100
```

For a 400 kHz Fast-mode I2C bus transmitting 10-byte packets:

- Each packet ≈ (1 START + 8-bit address + ACK + 8 data bytes × 9 bits + 1 STOP) ≈ 92 bit-times
- At 400 kHz: 92 / 400,000 ≈ 230 µs per transaction
- Maximum transaction rate ≈ 4,347 transactions/second

### Transaction Latency

Latency is the time from initiating a transaction to receiving a complete response. Under stress, latency can increase due to clock stretching, bus arbitration delays, and software overhead.

### Error Rate

The number of NACK responses, arbitration losses, bus timeouts, and CRC failures per unit time. A reliable I2C bus should exhibit zero errors under normal stress loads and defined, recoverable errors at extreme loads.

### Recovery Time

The time required for the bus to recover from an error state (NACK, bus lockup, or timeout) and resume normal operation.

---

## Bus Saturation Testing

Bus saturation testing fills the I2C bus to maximum capacity by generating continuous transactions as fast as the hardware allows. The goal is to determine:

- The point at which the bus begins to show errors (saturation threshold)
- How the system behaves when that threshold is exceeded
- Whether error recovery mechanisms function correctly under load

### Saturation Test Strategy

A typical saturation test proceeds in phases:

1. **Baseline** – Measure error rate, latency, and throughput at 10%, 25%, 50% bus utilization
2. **Ramp-up** – Incrementally increase transaction rate to 75%, 90%, 100% utilization
3. **Sustained saturation** – Maintain 100% utilization for an extended period (minutes to hours)
4. **Overload** – Attempt to exceed the theoretical maximum and verify graceful degradation

---

## Rapid Transaction Patterns

Rapid transaction patterns simulate real-world workloads where multiple sensors, actuators, or peripherals communicate in rapid succession. These patterns stress the bus in ways that random or uniform transaction loads do not.

### Burst Patterns

Short bursts of many transactions followed by brief idle periods. Common in sensor fusion applications where an IMU, barometer, and magnetometer are all read within a tight timing window.

### Interleaved Patterns

Alternating reads and writes to different devices. This tests address recognition logic in slave devices and ensures masters handle arbitration correctly.

### Variable-Length Transactions

Mixing short 1-byte register reads with long multi-byte DMA transfers. This stresses buffer management and clock stretching handling.

### Repeated Start Conditions

Using repeated START (Sr) instead of STOP+START between transactions. This is more efficient but harder on slave device state machines.

---

## Reliability Validation

Reliability validation determines whether the I2C bus and all connected devices maintain correct operation across:

- Extended time periods (soak testing, often 24–72 hours)
- Temperature ranges (cold boot, thermal cycling, high-temperature operation)
- Power supply variations (voltage margining: ±5%, ±10%)
- Mechanical vibration and shock (for automotive/industrial)

### Soak Testing

A soak test runs a representative workload continuously for an extended period while monitoring for errors. Any error—even a single NACK—is logged with a timestamp, transaction context, and bus state for post-test analysis.

### Golden Reference Comparison

Each transaction's response is compared against a known-good reference value. Discrepancies indicate data corruption, even if no protocol-level error (NACK, timeout) was detected. This is particularly important for sensor data integrity.

---

## Error Injection and Recovery Testing

Deliberately injecting errors validates that the system's error handling and recovery code paths function correctly under stress.

### Types of Injected Errors

- **NACK injection** – A test device is programmed to NACK specific transactions
- **Clock stretching** – A slave holds SCL low for abnormally long periods
- **Bus glitches** – Brief noise pulses on SDA or SCL simulated via GPIO toggling
- **Incomplete transactions** – Master is reset mid-transaction to leave bus in unknown state
- **Address collision** – Two devices respond to the same address (for bus scan stress tests)

### Bus Recovery Procedure

The standard I2C bus recovery procedure (per NXP application note UM10204) involves:

1. Toggling SCL up to 9 times to allow a stuck slave to complete its byte and release SDA
2. Generating a STOP condition
3. Resetting the I2C peripheral

This must be validated under stress to ensure it reliably clears bus lockups.

---

## Thermal and Environmental Stress

I2C electrical characteristics—particularly rise/fall times—are temperature-dependent. Stress testing should include:

- **Low temperature** (−40°C for automotive): Higher resistance, slower firmware, potential clock drift
- **High temperature** (+85°C or +125°C): Increased leakage current, faster timing margins erode
- **Power supply margining**: Reduced VCC lowers VOH/VOL margins and changes pull-up current

---

## Multi-Master Stress Testing

In multi-master I2C systems, stress testing must validate the arbitration mechanism:

- **Simultaneous transaction initiation** by two masters on the same bus
- **Arbitration loss detection and retry** logic under high load
- **Priority starvation** – verifying that a low-priority master eventually gets bus access even when a high-priority master is continuously active

---

## Automated Test Frameworks

Production stress testing should be automated and integrated into CI/CD pipelines. A typical framework includes:

- A test controller (PC or SBC) connected to the target via I2C or UART debug interface
- A load generator script that programs transaction patterns
- A monitoring process that logs all transactions and errors in real time
- A pass/fail evaluator that compares results against acceptance criteria
- A reporting system that generates test certificates

---

## Code Examples in C/C++

### Example 1: Bus Saturation Test (Linux I2C-dev, C)

```c
/*
 * i2c_stress_saturation.c
 *
 * Bus saturation stress test using Linux i2c-dev interface.
 * Generates continuous read transactions to a target I2C device
 * and measures throughput, latency, and error rate.
 *
 * Compile: gcc -O2 -o i2c_stress i2c_stress_saturation.c -lpthread
 * Usage:   ./i2c_stress /dev/i2c-1 0x48 60
 *          (bus device, slave address, test duration in seconds)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#define MAX_READ_LEN    32
#define STATS_INTERVAL  5   /* Print statistics every N seconds */

typedef struct {
    uint64_t total_transactions;
    uint64_t successful_transactions;
    uint64_t failed_transactions;
    uint64_t nack_errors;
    uint64_t timeout_errors;
    uint64_t total_latency_us;
    uint64_t max_latency_us;
    uint64_t min_latency_us;
} stress_stats_t;

typedef struct {
    int      fd;
    uint8_t  slave_addr;
    uint8_t  reg_addr;
    uint8_t  read_len;
    int      duration_sec;
    volatile int stop_flag;
    stress_stats_t stats;
} stress_ctx_t;

/* Get current time in microseconds */
static uint64_t time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

/* Perform a single I2C register read with latency measurement */
static int i2c_read_reg(int fd, uint8_t addr, uint8_t reg,
                        uint8_t *data, uint8_t len,
                        uint64_t *latency_us) {
    struct i2c_rdwr_ioctl_data msgset;
    struct i2c_msg msgs[2];
    uint64_t t_start, t_end;
    int ret;

    msgs[0].addr  = addr;
    msgs[0].flags = 0;            /* Write */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    msgs[1].addr  = addr;
    msgs[1].flags = I2C_M_RD;    /* Read */
    msgs[1].len   = len;
    msgs[1].buf   = data;

    msgset.msgs  = msgs;
    msgset.nmsgs = 2;

    t_start = time_us();
    ret = ioctl(fd, I2C_RDWR, &msgset);
    t_end = time_us();

    *latency_us = t_end - t_start;
    return ret;
}

/* Statistics printer thread */
static void *stats_thread(void *arg) {
    stress_ctx_t *ctx = (stress_ctx_t *)arg;
    uint64_t prev_total = 0;
    int elapsed = 0;

    while (!ctx->stop_flag) {
        sleep(STATS_INTERVAL);
        elapsed += STATS_INTERVAL;

        uint64_t cur_total = ctx->stats.total_transactions;
        uint64_t delta = cur_total - prev_total;
        double tps = (double)delta / STATS_INTERVAL;
        double error_rate = (ctx->stats.failed_transactions * 100.0) /
                            (cur_total ? cur_total : 1);
        double avg_lat = ctx->stats.successful_transactions ?
            (double)ctx->stats.total_latency_us /
            ctx->stats.successful_transactions : 0.0;

        printf("[%3ds] TPS: %7.1f | Total: %8lu | Errors: %6lu (%.2f%%) "
               "| Avg Lat: %.0f µs | Max Lat: %lu µs\n",
               elapsed, tps, cur_total, ctx->stats.failed_transactions,
               error_rate, avg_lat, ctx->stats.max_latency_us);
        fflush(stdout);
        prev_total = cur_total;
    }
    return NULL;
}

/* Main stress test loop */
static void run_saturation_test(stress_ctx_t *ctx) {
    uint8_t data[MAX_READ_LEN];
    uint64_t latency_us;
    uint64_t t_end = time_us() + (uint64_t)ctx->duration_sec * 1000000ULL;

    ctx->stats.min_latency_us = UINT64_MAX;

    while (time_us() < t_end && !ctx->stop_flag) {
        int ret = i2c_read_reg(ctx->fd, ctx->slave_addr, ctx->reg_addr,
                               data, ctx->read_len, &latency_us);

        ctx->stats.total_transactions++;

        if (ret < 0) {
            ctx->stats.failed_transactions++;
            if (errno == EREMOTEIO || errno == EIO)
                ctx->stats.nack_errors++;
            else if (errno == ETIMEDOUT)
                ctx->stats.timeout_errors++;
        } else {
            ctx->stats.successful_transactions++;
            ctx->stats.total_latency_us += latency_us;
            if (latency_us > ctx->stats.max_latency_us)
                ctx->stats.max_latency_us = latency_us;
            if (latency_us < ctx->stats.min_latency_us)
                ctx->stats.min_latency_us = latency_us;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <i2c-dev> <slave-addr-hex> <duration-sec>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *dev_path  = argv[1];
    uint8_t slave_addr    = (uint8_t)strtoul(argv[2], NULL, 16);
    int     duration_sec  = atoi(argv[3]);

    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    stress_ctx_t ctx = {
        .fd           = fd,
        .slave_addr   = slave_addr,
        .reg_addr     = 0x00,   /* Adjust for your device */
        .read_len     = 8,
        .duration_sec = duration_sec,
        .stop_flag    = 0,
    };
    memset(&ctx.stats, 0, sizeof(ctx.stats));

    printf("I2C Saturation Stress Test\n");
    printf("Device: %s | Slave: 0x%02X | Duration: %d sec\n\n",
           dev_path, slave_addr, duration_sec);

    pthread_t stat_tid;
    pthread_create(&stat_tid, NULL, stats_thread, &ctx);

    run_saturation_test(&ctx);

    ctx.stop_flag = 1;
    pthread_join(stat_tid, NULL);
    close(fd);

    /* Final report */
    printf("\n=== FINAL RESULTS ===\n");
    printf("Total Transactions:   %lu\n", ctx.stats.total_transactions);
    printf("Successful:           %lu\n", ctx.stats.successful_transactions);
    printf("Failed:               %lu\n", ctx.stats.failed_transactions);
    printf("  NACK Errors:        %lu\n", ctx.stats.nack_errors);
    printf("  Timeout Errors:     %lu\n", ctx.stats.timeout_errors);
    printf("Avg Latency:          %.1f µs\n",
           ctx.stats.successful_transactions ?
           (double)ctx.stats.total_latency_us /
           ctx.stats.successful_transactions : 0.0);
    printf("Max Latency:          %lu µs\n", ctx.stats.max_latency_us);
    printf("Min Latency:          %lu µs\n",
           ctx.stats.min_latency_us == UINT64_MAX ? 0 :
           ctx.stats.min_latency_us);
    printf("Error Rate:           %.4f%%\n",
           ctx.stats.total_transactions ?
           (double)ctx.stats.failed_transactions * 100.0 /
           ctx.stats.total_transactions : 0.0);

    return (ctx.stats.failed_transactions == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

---

### Example 2: Rapid Transaction Burst Pattern (C++, Arduino/Embedded)

```cpp
/*
 * i2c_burst_stress.cpp
 *
 * Rapid burst transaction stress test for embedded targets (Arduino/ESP32/STM32
 * compatible via Wire library). Sends burst patterns of mixed read/write
 * transactions and validates response integrity.
 */

#include <Arduino.h>
#include <Wire.h>

// ---- Configuration --------------------------------------------------------
static constexpr uint8_t  SLAVE_ADDR      = 0x68;  // MPU-6050 or similar
static constexpr uint8_t  REG_WHO_AM_I    = 0x75;
static constexpr uint8_t  REG_ACCEL_XOUT  = 0x3B;
static constexpr uint8_t  EXPECTED_WHO_AM = 0x68;
static constexpr uint32_t BURST_SIZE      = 50;    // Transactions per burst
static constexpr uint32_t BURST_PAUSE_MS  = 1;     // Pause between bursts
static constexpr uint32_t TEST_DURATION_S = 60;    // Total test time

// ---- Statistics -----------------------------------------------------------
struct StressStats {
    uint32_t total_tx      = 0;
    uint32_t ok_tx         = 0;
    uint32_t nack_errors   = 0;
    uint32_t data_errors   = 0;
    uint32_t timeout_errors = 0;
    uint32_t max_latency_us = 0;
    uint64_t total_latency_us = 0;

    void reset() { memset(this, 0, sizeof(*this)); }

    void print() const {
        Serial.printf("  Total:    %u\n", total_tx);
        Serial.printf("  OK:       %u (%.2f%%)\n", ok_tx,
                      total_tx ? ok_tx * 100.0f / total_tx : 0.0f);
        Serial.printf("  NACK:     %u\n", nack_errors);
        Serial.printf("  DataErr:  %u\n", data_errors);
        Serial.printf("  Timeout:  %u\n", timeout_errors);
        if (ok_tx > 0)
            Serial.printf("  AvgLat:   %llu µs\n", total_latency_us / ok_tx);
        Serial.printf("  MaxLat:   %u µs\n", max_latency_us);
    }
};

static StressStats g_stats;

// ---- I2C Helpers ----------------------------------------------------------

/**
 * @brief  Read N bytes from a device register with timing.
 * @return true on success, false on any error.
 */
static bool i2c_read_timed(uint8_t addr, uint8_t reg,
                            uint8_t *buf, uint8_t len,
                            uint32_t &latency_us) {
    uint32_t t0 = micros();

    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {  // Repeated START
        latency_us = micros() - t0;
        return false;
    }

    uint8_t received = Wire.requestFrom((uint8_t)addr, len, (uint8_t)true);
    latency_us = micros() - t0;

    if (received != len) return false;

    for (uint8_t i = 0; i < len; i++)
        buf[i] = Wire.read();

    return true;
}

/**
 * @brief  Write a single byte to a device register.
 */
static bool i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission(true) == 0;
}

// ---- Burst Pattern Generators ---------------------------------------------

/**
 * @brief  Pattern A: Rapid repeated reads of the same register.
 *         Tests sustained read throughput and data consistency.
 */
static void pattern_rapid_reads(uint32_t count) {
    uint8_t buf[1];
    uint32_t latency;

    for (uint32_t i = 0; i < count; i++) {
        g_stats.total_tx++;
        bool ok = i2c_read_timed(SLAVE_ADDR, REG_WHO_AM_I, buf, 1, latency);

        if (!ok) {
            g_stats.nack_errors++;
        } else if (buf[0] != EXPECTED_WHO_AM) {
            g_stats.data_errors++;
            Serial.printf("[!] Data error: expected 0x%02X, got 0x%02X\n",
                          EXPECTED_WHO_AM, buf[0]);
        } else {
            g_stats.ok_tx++;
            g_stats.total_latency_us += latency;
            if (latency > g_stats.max_latency_us)
                g_stats.max_latency_us = latency;
        }
    }
}

/**
 * @brief  Pattern B: Interleaved reads and writes.
 *         Tests bus transitions and slave state machine robustness.
 */
static void pattern_interleaved_rw(uint32_t count) {
    uint8_t buf[14];   // Full accelerometer + gyro block
    uint32_t latency;
    static uint8_t pwr_val = 0;

    for (uint32_t i = 0; i < count; i++) {
        g_stats.total_tx++;

        if (i % 2 == 0) {
            // Read: 14-byte sensor block
            bool ok = i2c_read_timed(SLAVE_ADDR, REG_ACCEL_XOUT,
                                     buf, 14, latency);
            if (!ok) {
                g_stats.nack_errors++;
            } else {
                g_stats.ok_tx++;
                g_stats.total_latency_us += latency;
                if (latency > g_stats.max_latency_us)
                    g_stats.max_latency_us = latency;
            }
        } else {
            // Write: toggle a benign config register
            bool ok = i2c_write_byte(SLAVE_ADDR, 0x6B, pwr_val++ & 0x01);
            g_stats.total_tx++;
            if (ok) g_stats.ok_tx++;
            else    g_stats.nack_errors++;
        }
    }
}

/**
 * @brief  Pattern C: Variable-length burst.
 *         Alternates between 1-byte and 14-byte reads.
 */
static void pattern_variable_length(uint32_t count) {
    static const uint8_t lengths[] = {1, 2, 6, 14};
    uint8_t buf[14];
    uint32_t latency;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t len = lengths[i % 4];
        g_stats.total_tx++;

        bool ok = i2c_read_timed(SLAVE_ADDR, REG_ACCEL_XOUT,
                                 buf, len, latency);
        if (!ok) {
            g_stats.nack_errors++;
        } else {
            g_stats.ok_tx++;
            g_stats.total_latency_us += latency;
            if (latency > g_stats.max_latency_us)
                g_stats.max_latency_us = latency;
        }
    }
}

// ---- Bus Recovery ---------------------------------------------------------

/**
 * @brief  Attempt to recover a locked I2C bus.
 *         Toggles SCL 9 times then issues a STOP condition.
 *
 *         WARNING: Manipulates GPIO directly — adjust pin numbers for
 *         your platform.
 */
static void i2c_bus_recovery(uint8_t scl_pin, uint8_t sda_pin) {
    Serial.println("[!] Attempting I2C bus recovery...");
    Wire.end();

    pinMode(scl_pin, OUTPUT);
    pinMode(sda_pin, INPUT_PULLUP);

    for (int i = 0; i < 9; i++) {
        if (digitalRead(sda_pin)) break;  // SDA released — done
        digitalWrite(scl_pin, HIGH); delayMicroseconds(5);
        digitalWrite(scl_pin, LOW);  delayMicroseconds(5);
    }

    // Generate STOP: SDA rises while SCL is HIGH
    pinMode(sda_pin, OUTPUT);
    digitalWrite(sda_pin, LOW);  delayMicroseconds(5);
    digitalWrite(scl_pin, HIGH); delayMicroseconds(5);
    digitalWrite(sda_pin, HIGH); delayMicroseconds(5);

    Wire.begin();
    Wire.setClock(400000);
    Serial.println("[+] Recovery complete. Bus re-initialized.");
}

// ---- Main Test Loop -------------------------------------------------------

void setup() {
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(400000);  // 400 kHz Fast-mode
    delay(100);

    Serial.println("====================================");
    Serial.println("  I2C Burst Stress Test");
    Serial.printf("  Slave: 0x%02X | Duration: %us\n",
                  SLAVE_ADDR, TEST_DURATION_S);
    Serial.println("====================================\n");
}

void loop() {
    static uint32_t phase = 0;
    static uint32_t t_start = millis();

    uint32_t elapsed_s = (millis() - t_start) / 1000;
    if (elapsed_s >= TEST_DURATION_S) {
        Serial.println("\n====== FINAL RESULTS ======");
        g_stats.print();
        Serial.println("Test complete. Halting.");
        while (true) delay(1000);
    }

    // Cycle through the three burst patterns
    switch (phase % 3) {
        case 0:
            Serial.printf("[%3us] Pattern A: Rapid reads\n", elapsed_s);
            pattern_rapid_reads(BURST_SIZE);
            break;
        case 1:
            Serial.printf("[%3us] Pattern B: Interleaved R/W\n", elapsed_s);
            pattern_interleaved_rw(BURST_SIZE);
            break;
        case 2:
            Serial.printf("[%3us] Pattern C: Variable length\n", elapsed_s);
            pattern_variable_length(BURST_SIZE);
            break;
    }

    phase++;
    delay(BURST_PAUSE_MS);
}
```

---

### Example 3: Golden Reference Reliability Validator (C, bare-metal STM32 HAL)

```c
/*
 * i2c_reliability_validator.c
 *
 * Reliability validation using golden reference comparison.
 * Each transaction result is compared against a pre-stored reference
 * value to detect silent data corruption.
 *
 * Target: STM32 with HAL I2C driver.
 */

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart2;

#define SLAVE_ADDR          (0x68 << 1)  /* HAL uses 8-bit address */
#define REG_DEVICE_ID       0x75
#define GOLDEN_DEVICE_ID    0x68
#define READ_BLOCK_LEN      6
#define REG_ACCEL_START     0x3B
#define MAX_CONSEC_ERRORS   5
#define TEST_CYCLES         100000UL

typedef struct {
    uint32_t cycles;
    uint32_t pass;
    uint32_t fail;
    uint32_t bus_recoveries;
    uint32_t max_consec_fail;
    uint32_t cur_consec_fail;
} val_stats_t;

static val_stats_t stats;

/* Printf via UART */
static void uart_printf(const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, len, HAL_MAX_DELAY);
}

/* Read a single register and compare to golden reference */
static HAL_StatusTypeDef validate_device_id(void) {
    uint8_t reg = REG_DEVICE_ID;
    uint8_t result = 0;

    HAL_StatusTypeDef status =
        HAL_I2C_Master_Transmit(&hi2c1, SLAVE_ADDR, &reg, 1, 10);
    if (status != HAL_OK) return status;

    status = HAL_I2C_Master_Receive(&hi2c1, SLAVE_ADDR, &result, 1, 10);
    if (status != HAL_OK) return status;

    return (result == GOLDEN_DEVICE_ID) ? HAL_OK : HAL_ERROR;
}

/* Read a block of registers and validate against reference snapshot */
static HAL_StatusTypeDef validate_register_block(
    uint8_t reg, const uint8_t *reference, uint8_t len) {

    uint8_t buf[READ_BLOCK_LEN];
    HAL_StatusTypeDef status =
        HAL_I2C_Mem_Read(&hi2c1, SLAVE_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                         buf, len, 50);
    if (status != HAL_OK) return status;

    /* For static config registers, compare byte-for-byte */
    return (memcmp(buf, reference, len) == 0) ? HAL_OK : HAL_ERROR;
}

/* Attempt I2C bus recovery using HAL GPIO */
static void recover_i2c_bus(void) {
    stats.bus_recoveries++;
    uart_printf("[!] Recovery attempt #%lu\r\n", stats.bus_recoveries);

    /* De-init I2C peripheral */
    HAL_I2C_DeInit(&hi2c1);

    /* Toggle SCL 9 times via GPIO */
    GPIO_InitTypeDef GPIO_InitStruct = {
        .Pin  = GPIO_PIN_6,    /* Adjust to your SCL pin */
        .Mode = GPIO_MODE_OUTPUT_OD,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        HAL_Delay(1);
    }
    /* STOP condition */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_Delay(1);

    /* Re-init I2C */
    extern void MX_I2C1_Init(void);
    MX_I2C1_Init();
}

void i2c_reliability_validation_run(void) {
    memset(&stats, 0, sizeof(stats));

    /* Capture golden reference for config registers */
    uint8_t golden_cfg[READ_BLOCK_LEN] = {0};
    HAL_I2C_Mem_Read(&hi2c1, SLAVE_ADDR, REG_ACCEL_START,
                     I2C_MEMADD_SIZE_8BIT,
                     golden_cfg, READ_BLOCK_LEN, 50);

    uart_printf("=== Reliability Validation Start ===\r\n");
    uart_printf("Cycles: %lu\r\n\r\n", TEST_CYCLES);

    for (uint32_t i = 0; i < TEST_CYCLES; i++) {
        stats.cycles++;

        HAL_StatusTypeDef res = validate_device_id();

        if (res == HAL_OK) {
            stats.pass++;
            stats.cur_consec_fail = 0;
        } else {
            stats.fail++;
            stats.cur_consec_fail++;

            if (stats.cur_consec_fail > stats.max_consec_fail)
                stats.max_consec_fail = stats.cur_consec_fail;

            uart_printf("[%lu] FAIL (consec=%lu)\r\n", i, stats.cur_consec_fail);

            /* Attempt recovery after threshold of consecutive errors */
            if (stats.cur_consec_fail >= MAX_CONSEC_ERRORS) {
                recover_i2c_bus();
                stats.cur_consec_fail = 0;
                HAL_Delay(10);
            }
        }

        /* Periodic progress */
        if (i % 10000 == 0 && i > 0) {
            uart_printf("[Progress] %lu/%lu | Pass: %lu | Fail: %lu | "
                        "Recoveries: %lu\r\n",
                        i, TEST_CYCLES, stats.pass, stats.fail,
                        stats.bus_recoveries);
        }
    }

    uart_printf("\r\n=== VALIDATION RESULTS ===\r\n");
    uart_printf("Total Cycles:   %lu\r\n", stats.cycles);
    uart_printf("Pass:           %lu (%.2f%%)\r\n", stats.pass,
                (float)stats.pass / stats.cycles * 100.0f);
    uart_printf("Fail:           %lu\r\n", stats.fail);
    uart_printf("Bus Recoveries: %lu\r\n", stats.bus_recoveries);
    uart_printf("Max Consec Fail:%lu\r\n", stats.max_consec_fail);
    uart_printf("RESULT: %s\r\n",
                (stats.fail == 0) ? "PASS" : "FAIL");
}
```

---

## Code Examples in Rust

### Example 4: Bus Saturation Test (Rust, Linux `i2cdev`)

```rust
//! i2c_stress_saturation.rs
//!
//! I2C bus saturation stress test using the `i2cdev` crate on Linux.
//!
//! [dependencies]
//! i2cdev  = "0.6"
//! clap    = { version = "4", features = ["derive"] }
//!
//! Usage: cargo run -- --bus 1 --addr 0x48 --duration 60

use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use i2cdev::core::I2CDevice;
use i2cdev::linux::LinuxI2CDevice;

// ---- Statistics -----------------------------------------------------------
#[derive(Debug, Default, Clone)]
struct StressStats {
    total_transactions:      u64,
    successful_transactions: u64,
    failed_transactions:     u64,
    nack_errors:             u64,
    total_latency_us:        u64,
    max_latency_us:          u64,
    min_latency_us:          u64,
}

impl StressStats {
    fn record_success(&mut self, latency_us: u64) {
        self.total_transactions      += 1;
        self.successful_transactions += 1;
        self.total_latency_us        += latency_us;
        if latency_us > self.max_latency_us {
            self.max_latency_us = latency_us;
        }
        if self.min_latency_us == 0 || latency_us < self.min_latency_us {
            self.min_latency_us = latency_us;
        }
    }

    fn record_failure(&mut self) {
        self.total_transactions  += 1;
        self.failed_transactions += 1;
        self.nack_errors         += 1;
    }

    fn avg_latency_us(&self) -> f64 {
        if self.successful_transactions == 0 {
            return 0.0;
        }
        self.total_latency_us as f64 / self.successful_transactions as f64
    }

    fn error_rate_pct(&self) -> f64 {
        if self.total_transactions == 0 {
            return 0.0;
        }
        self.failed_transactions as f64 / self.total_transactions as f64 * 100.0
    }

    fn print_summary(&self) {
        println!("=== STRESS TEST RESULTS ===");
        println!("  Total Transactions:   {}", self.total_transactions);
        println!("  Successful:           {}", self.successful_transactions);
        println!("  Failed:               {}", self.failed_transactions);
        println!("    NACK Errors:        {}", self.nack_errors);
        println!("  Avg Latency:          {:.1} µs", self.avg_latency_us());
        println!("  Max Latency:          {} µs", self.max_latency_us);
        println!("  Min Latency:          {} µs", self.min_latency_us);
        println!("  Error Rate:           {:.4}%", self.error_rate_pct());
    }
}

// ---- I2C Read Helper ------------------------------------------------------

/// Perform a write-then-read transaction (register read).
/// Returns (data_bytes, latency_us) on success, Err on failure.
fn i2c_read_reg(
    dev: &mut LinuxI2CDevice,
    reg: u8,
    len: usize,
) -> Result<(Vec<u8>, u64), std::io::Error> {
    let t_start = Instant::now();

    dev.write(&[reg])?;
    let mut buf = vec![0u8; len];
    dev.read(&mut buf)?;

    let latency_us = t_start.elapsed().as_micros() as u64;
    Ok((buf, latency_us))
}

// ---- Saturation Test -------------------------------------------------------

fn run_saturation_test(
    bus_num:     u8,
    slave_addr:  u16,
    duration:    Duration,
    stats_arc:   Arc<Mutex<StressStats>>,
    stop_flag:   Arc<std::sync::atomic::AtomicBool>,
) {
    let dev_path = format!("/dev/i2c-{}", bus_num);
    let mut dev  = LinuxI2CDevice::new(&dev_path, slave_addr)
        .expect("Failed to open I2C device");

    let t_end = Instant::now() + duration;

    while Instant::now() < t_end
        && !stop_flag.load(std::sync::atomic::Ordering::Relaxed)
    {
        match i2c_read_reg(&mut dev, 0x00, 8) {
            Ok((_data, latency_us)) => {
                let mut s = stats_arc.lock().unwrap();
                s.record_success(latency_us);
            }
            Err(_e) => {
                let mut s = stats_arc.lock().unwrap();
                s.record_failure();
            }
        }
    }
}

// ---- Statistics Reporter ---------------------------------------------------

fn stats_reporter(
    stats_arc: Arc<Mutex<StressStats>>,
    stop_flag: Arc<std::sync::atomic::AtomicBool>,
    interval:  Duration,
) {
    let mut prev_total: u64 = 0;
    let mut elapsed_s: u32  = 0;

    while !stop_flag.load(std::sync::atomic::Ordering::Relaxed) {
        thread::sleep(interval);
        elapsed_s += interval.as_secs() as u32;

        let s = stats_arc.lock().unwrap().clone();
        let delta = s.total_transactions.saturating_sub(prev_total);
        let tps   = delta as f64 / interval.as_secs_f64();

        println!(
            "[{:3}s] TPS: {:7.1} | Total: {:8} | Errors: {:6} ({:.2}%) \
             | Avg Lat: {:.0} µs | Max: {} µs",
            elapsed_s,
            tps,
            s.total_transactions,
            s.failed_transactions,
            s.error_rate_pct(),
            s.avg_latency_us(),
            s.max_latency_us,
        );

        prev_total = s.total_transactions;
    }
}

fn main() {
    // Configuration — replace with clap args for CLI use
    let bus_num:    u8       = 1;
    let slave_addr: u16      = 0x48;
    let duration             = Duration::from_secs(60);
    let stats_interval       = Duration::from_secs(5);

    println!("I2C Saturation Stress Test (Rust)");
    println!("Bus: /dev/i2c-{} | Slave: 0x{:02X} | Duration: {}s\n",
             bus_num, slave_addr, duration.as_secs());

    let stats_arc  = Arc::new(Mutex::new(StressStats::default()));
    let stop_flag  = Arc::new(std::sync::atomic::AtomicBool::new(false));

    // Spawn statistics reporter thread
    let reporter_stats = Arc::clone(&stats_arc);
    let reporter_stop  = Arc::clone(&stop_flag);
    let reporter_handle = thread::spawn(move || {
        stats_reporter(reporter_stats, reporter_stop, stats_interval);
    });

    // Run the saturation test on the current thread
    run_saturation_test(bus_num, slave_addr, duration,
                        Arc::clone(&stats_arc), Arc::clone(&stop_flag));

    // Signal reporter to stop and wait
    stop_flag.store(true, std::sync::atomic::Ordering::Relaxed);
    reporter_handle.join().ok();

    // Print final results
    println!();
    stats_arc.lock().unwrap().print_summary();
}
```

---

### Example 5: Reliability Validator with Golden Reference (Rust, `embedded-hal`)

```rust
//! i2c_reliability_validator.rs
//!
//! Platform-independent I2C reliability validator for embedded targets
//! using the `embedded-hal` I2C trait. Compatible with any HAL crate
//! that implements `embedded_hal::i2c::I2c`.
//!
//! [dependencies]
//! embedded-hal = "1.0"

use embedded_hal::i2c::I2c;

// ---- Configuration --------------------------------------------------------
const SLAVE_ADDR:       u8   = 0x68;
const REG_WHO_AM_I:     u8   = 0x75;
const GOLDEN_WHO_AM_I:  u8   = 0x68;
const REG_ACCEL_BLOCK:  u8   = 0x3B;
const BLOCK_LEN:        usize = 6;
const MAX_CONSEC_FAIL:  u32  = 5;
const TEST_CYCLES:      u32  = 100_000;

// ---- Statistics -----------------------------------------------------------
#[derive(Debug, Default)]
struct ValidationStats {
    cycles:          u32,
    pass:            u32,
    fail:            u32,
    bus_recoveries:  u32,
    max_consec_fail: u32,
    cur_consec_fail: u32,
}

impl ValidationStats {
    fn record_pass(&mut self) {
        self.cycles += 1;
        self.pass   += 1;
        self.cur_consec_fail = 0;
    }

    fn record_fail(&mut self) {
        self.cycles          += 1;
        self.fail            += 1;
        self.cur_consec_fail += 1;
        if self.cur_consec_fail > self.max_consec_fail {
            self.max_consec_fail = self.cur_consec_fail;
        }
    }

    fn pass_rate_pct(&self) -> f32 {
        if self.cycles == 0 { return 0.0; }
        self.pass as f32 / self.cycles as f32 * 100.0
    }
}

// ---- I2C Helpers ----------------------------------------------------------

/// Read a single register from an I2C device.
fn read_register<I>(i2c: &mut I, addr: u8, reg: u8)
    -> Result<u8, I::Error>
where
    I: I2c,
{
    let mut buf = [0u8; 1];
    i2c.write_read(addr, &[reg], &mut buf)?;
    Ok(buf[0])
}

/// Read a block of registers.
fn read_block<I>(i2c: &mut I, addr: u8, reg: u8, buf: &mut [u8])
    -> Result<(), I::Error>
where
    I: I2c,
{
    i2c.write_read(addr, &[reg], buf)
}

// ---- Bus Recovery ---------------------------------------------------------

/// Platform-specific bus recovery hook.
/// The closure receives the SCL toggle count and should toggle SCL accordingly.
///
/// This is a trait-based approach — on real hardware, inject your GPIO
/// control through this closure or a concrete recovery type.
fn recover_bus<F>(stats: &mut ValidationStats, toggle_scl: F)
where
    F: Fn(u32),
{
    stats.bus_recoveries += 1;

    // Toggle SCL 9 times to free a stuck slave
    for i in 0..9 {
        toggle_scl(i);
    }
    // The actual re-initialization is platform-specific;
    // the caller is responsible for re-initializing the I2C peripheral
    // after this function returns.
}

// ---- Validator Core -------------------------------------------------------

/// Run the reliability validation loop.
///
/// # Arguments
/// * `i2c`      - Mutable reference to an embedded-hal I2C implementation
/// * `log_fn`   - Logging callback (receives formatted string slices)
/// * `delay_fn` - Delay callback (receives millisecond count)
pub fn run_validation<I, L, D>(
    i2c:      &mut I,
    log_fn:   &mut L,
    delay_fn: &mut D,
) -> bool
where
    I: I2c,
    L: FnMut(&str),
    D: FnMut(u32),
{
    let mut stats = ValidationStats::default();

    // Capture golden reference: static accelerometer config block
    let mut golden_block = [0u8; BLOCK_LEN];
    if read_block(i2c, SLAVE_ADDR, REG_ACCEL_BLOCK, &mut golden_block).is_err() {
        log_fn("ERROR: Failed to read golden reference. Aborting.");
        return false;
    }

    log_fn("=== I2C Reliability Validation ===");

    for cycle in 0..TEST_CYCLES {
        // Primary check: device ID register
        let id_result = read_register(i2c, SLAVE_ADDR, REG_WHO_AM_I);

        let ok = match id_result {
            Ok(id) if id == GOLDEN_WHO_AM_I => true,
            Ok(id) => {
                // Data error (NACK not triggered, but value is wrong)
                let msg = alloc_format!(
                    "[{}] DATA ERROR: expected 0x{:02X}, got 0x{:02X}",
                    cycle, GOLDEN_WHO_AM_I, id
                );
                log_fn(&msg);
                false
            }
            Err(_) => {
                // Bus/NACK error
                let msg = alloc_format!("[{}] BUS ERROR", cycle);
                log_fn(&msg);
                false
            }
        };

        if ok {
            stats.record_pass();
        } else {
            stats.record_fail();

            if stats.cur_consec_fail >= MAX_CONSEC_FAIL {
                log_fn("Recovery threshold reached. Triggering bus recovery.");
                // NOTE: In a real system, pass a concrete SCL toggle fn
                recover_bus(&mut stats, |_| {
                    // Platform-specific GPIO toggle goes here
                });
                stats.cur_consec_fail = 0;
                delay_fn(10);
            }
        }

        // Periodic progress log
        if cycle % 10_000 == 0 && cycle > 0 {
            let msg = alloc_format!(
                "[Progress] {}/{} | Pass: {} | Fail: {} | Recoveries: {}",
                cycle, TEST_CYCLES,
                stats.pass, stats.fail, stats.bus_recoveries
            );
            log_fn(&msg);
        }
    }

    // Final report
    log_fn("=== VALIDATION RESULTS ===");
    let result_msg = alloc_format!(
        "Cycles: {} | Pass: {} ({:.2}%) | Fail: {} | Recoveries: {} | MaxConsecFail: {}",
        stats.cycles,
        stats.pass,
        stats.pass_rate_pct(),
        stats.fail,
        stats.bus_recoveries,
        stats.max_consec_fail,
    );
    log_fn(&result_msg);
    log_fn(if stats.fail == 0 { "RESULT: PASS" } else { "RESULT: FAIL" });

    stats.fail == 0
}

/// Minimal no_std compatible format helper.
/// In std environments, replace with format!() directly.
macro_rules! alloc_format {
    ($($arg:tt)*) => {{
        // In no_std: use heapless::String or a fixed-size buffer.
        // In std: simply use format!().
        format!($($arg)*)
    }};
}
```

---

### Example 6: Burst Pattern Stress Test with Error Injection (Rust)

```rust
//! i2c_burst_with_injection.rs
//!
//! Burst transaction stress test with controlled error injection.
//! Demonstrates how to interleave valid transactions with injected
//! failures to validate error recovery logic.
//!
//! Runs on Linux using the i2cdev crate.

use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq)]
enum TransactionType {
    NormalRead,
    LongRead,
    QuickWrite,
    InjectedFail,  // Simulates an error by writing to a non-existent address
}

#[derive(Debug, Default)]
struct BurstStats {
    normal_ok:    u32,
    normal_fail:  u32,
    long_ok:      u32,
    long_fail:    u32,
    write_ok:     u32,
    write_fail:   u32,
    injected_ok:  u32,   // Should always be 0 (injected errors should fail)
    injected_fail: u32,  // Should match injection count
    recovery_time_us_total: u64,
    recovery_count: u32,
}

impl BurstStats {
    fn print(&self) {
        println!("=== BURST STRESS RESULTS ===");
        println!("  Normal Read:    OK={} FAIL={}", self.normal_ok,   self.normal_fail);
        println!("  Long Read:      OK={} FAIL={}", self.long_ok,     self.long_fail);
        println!("  Write:          OK={} FAIL={}", self.write_ok,    self.write_fail);
        println!("  Injected Error: OK={} FAIL={} (FAIL expected)",
                 self.injected_ok, self.injected_fail);
        println!("  Recoveries:     {}", self.recovery_count);
        if self.recovery_count > 0 {
            println!("  Avg Recovery:   {} µs",
                     self.recovery_time_us_total / self.recovery_count as u64);
        }
    }
}

/// Generate a deterministic burst sequence with periodic error injection.
fn generate_burst_sequence(burst_len: usize, inject_rate: usize)
    -> Vec<TransactionType>
{
    (0..burst_len).map(|i| {
        if inject_rate > 0 && i % inject_rate == 0 {
            TransactionType::InjectedFail
        } else {
            match i % 3 {
                0 => TransactionType::NormalRead,
                1 => TransactionType::LongRead,
                _ => TransactionType::QuickWrite,
            }
        }
    }).collect()
}

/// Execute one transaction and return (success, latency_us).
/// In a real implementation, replace the simulated calls with actual I2C ops.
fn execute_transaction(tx_type: TransactionType) -> (bool, u64) {
    let t = Instant::now();

    // Simulated I2C operations (replace with LinuxI2CDevice calls)
    let success = match tx_type {
        TransactionType::NormalRead => {
            std::thread::sleep(Duration::from_micros(250));
            true  // Normally succeeds
        }
        TransactionType::LongRead => {
            std::thread::sleep(Duration::from_micros(800));
            true
        }
        TransactionType::QuickWrite => {
            std::thread::sleep(Duration::from_micros(150));
            true
        }
        TransactionType::InjectedFail => {
            // Simulate addressing a non-existent device → NACK
            std::thread::sleep(Duration::from_micros(50));
            false
        }
    };

    (success, t.elapsed().as_micros() as u64)
}

/// Simulate a bus recovery and return time taken in µs.
fn simulate_recovery() -> u64 {
    let t = Instant::now();
    // In real code: toggle SCL, re-init peripheral
    std::thread::sleep(Duration::from_micros(500));
    t.elapsed().as_micros() as u64
}

fn run_burst_test(
    num_bursts:    u32,
    burst_len:     usize,
    inject_rate:   usize,
    pause_between: Duration,
) -> BurstStats {
    let mut stats = BurstStats::default();
    let sequence   = generate_burst_sequence(burst_len, inject_rate);

    for burst_num in 0..num_bursts {
        let mut consec_fail = 0u32;

        println!("Burst {}/{}", burst_num + 1, num_bursts);

        for &tx in &sequence {
            let (ok, _lat) = execute_transaction(tx);

            match (tx, ok) {
                (TransactionType::NormalRead,   true)  => stats.normal_ok   += 1,
                (TransactionType::NormalRead,   false) => {
                    stats.normal_fail += 1;
                    consec_fail += 1;
                }
                (TransactionType::LongRead,     true)  => stats.long_ok     += 1,
                (TransactionType::LongRead,     false) => {
                    stats.long_fail += 1;
                    consec_fail += 1;
                }
                (TransactionType::QuickWrite,   true)  => stats.write_ok    += 1,
                (TransactionType::QuickWrite,   false) => {
                    stats.write_fail += 1;
                    consec_fail += 1;
                }
                (TransactionType::InjectedFail, false) => {
                    stats.injected_fail += 1;
                    consec_fail = 0;  // Expected failure — don't count for recovery
                }
                (TransactionType::InjectedFail, true) => {
                    // Should never happen — injected fails are expected to fail
                    eprintln!("WARNING: Injected failure unexpectedly succeeded!");
                    stats.injected_ok += 1;
                }
                _ => {}
            }

            // Trigger recovery after 3 unexpected consecutive failures
            if consec_fail >= 3 {
                println!("  [!] {} consecutive failures — recovering bus", consec_fail);
                let recovery_us = simulate_recovery();
                stats.recovery_count += 1;
                stats.recovery_time_us_total += recovery_us;
                consec_fail = 0;
            }
        }

        std::thread::sleep(pause_between);
    }

    stats
}

fn main() {
    println!("I2C Burst Stress Test with Error Injection (Rust)\n");

    let stats = run_burst_test(
        20,                           // 20 bursts
        50,                           // 50 transactions per burst
        10,                           // Inject error every 10th transaction
        Duration::from_millis(5),     // 5 ms pause between bursts
    );

    println!();
    stats.print();

    // Validate injection worked as expected
    assert!(stats.injected_ok == 0, "Injected errors should always fail");
    println!("\nAssertion passed: All injected errors correctly failed.");
}
```

---

## Metrics and Analysis

### Key Performance Indicators (KPIs)

| Metric | Acceptable Range | Action if Exceeded |
|---|---|---|
| Error Rate | < 0.001% | Investigate pull-up resistance, capacitance, speed |
| Average Latency | ≤ 2× nominal | Check for clock stretching, buffer underruns |
| Max Latency | ≤ 10× nominal | Set or verify clock stretching timeout |
| Bus Utilization at Error | > 90% | Consider faster bus mode (Fast+, Hi-speed) |
| Recovery Time | < 50 ms | Optimize recovery procedure |
| Max Consecutive Failures | < 3 | Improve noise filtering, shielding |

### Interpreting Results

**High error rate at high utilization** suggests the pull-up resistors are too weak (value too large) for the bus capacitance, causing excessive rise times. Try lower-value pull-ups or switch to an I2C bus driver IC.

**Intermittent data errors without NACK** (golden reference mismatches) often indicate EMI/noise coupling onto the SDA line mid-byte. Solutions include shorter traces, ground planes, or ferrite beads.

**Increasing latency under sustained load** points to clock stretching by a slave device that cannot keep up with the transaction rate. Check the device's maximum clock frequency specification and whether it requires inter-transaction delays.

**Bus lockup under rapid pattern tests** usually means a slave's state machine does not handle repeated START conditions correctly, or a master reset interrupts a transaction. Validate the recovery procedure works reliably.

---

## Summary

I2C stress testing is a multi-dimensional discipline that validates bus reliability through bus saturation tests, rapid transaction patterns, and systematic reliability validation. It goes beyond basic functional testing by exposing failure modes that only emerge under sustained load, worst-case timing, environmental extremes, and error conditions.

**Bus saturation testing** establishes the bus throughput ceiling and confirms that error rates remain acceptable at maximum utilization, typically measuring transactions per second, error rate percentage, and latency statistics across a range of bus loads.

**Rapid transaction patterns** — including burst sequences, interleaved reads/writes, variable-length transactions, and repeated START patterns — simulate real-world sensor fusion and peripheral management workloads, stressing both the bus hardware and slave device firmware.

**Reliability validation** uses golden reference comparison, soak testing, and error injection to detect not only protocol-level failures (NACK, timeout) but also silent data corruption. The bus recovery procedure — SCL toggling followed by peripheral re-initialization — must be validated under stress to ensure it reliably clears all lockup states.

In C/C++, stress tests can be implemented using the Linux `i2c-dev` interface for host-side testing or STM32/Arduino HAL libraries for target-side embedded testing. In Rust, the `i2cdev` crate provides Linux I2C access, while the `embedded-hal` I2C trait enables platform-independent embedded validators that compile for any supported microcontroller.

A complete stress testing program combines automated load generation, real-time monitoring, error injection, recovery validation, and statistical reporting against defined acceptance criteria. Any embedded system relying on I2C for safety, reliability, or real-time performance should pass stress testing before production release.

---

*Document: 72 — I2C Stress Testing | Bus saturation tests, rapid transaction patterns, and reliability validation*