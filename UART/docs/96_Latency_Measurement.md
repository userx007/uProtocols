# 96. UART Latency Measurement

**C/C++ examples:**
- Hardware timer measurement using the ARM DWT cycle counter
- GPIO toggle method for oscilloscope/logic-analyzer measurement
- POSIX round-trip latency test with full statistical reporting
- Interrupt dispatch latency measurement in an ISR
- DMA TX transfer latency benchmarking via HAL callbacks
- A C++ `UartLatencyAnalyzer` class with percentile and outlier detection

**Rust examples:**
- Bare-metal embedded latency measurement using `embedded-hal` traits and DWT
- A full Linux CLI round-trip benchmark using the `serialport` crate with `clap` argument parsing
- A reusable `OnlineStats` module using Welford's algorithm (works in `no_std`) and a `LatencyBudget` calculator with unit tests

**Optimization strategies** cover baud rate, `ASYNC_LOW_LATENCY`, `VMIN`/`VTIME` tuning, USB latency timer, `SCHED_FIFO` real-time scheduling, DMA trade-offs, and CPU frequency scaling — each with concrete commands or code snippets.

## Measuring and Optimizing End-to-End UART Latency

---

## Table of Contents

1. [Introduction](#introduction)
2. [Sources of UART Latency](#sources-of-uart-latency)
3. [Latency Measurement Fundamentals](#latency-measurement-fundamentals)
4. [C/C++ Implementation](#cc-implementation)
   - [Hardware Timer-Based Measurement](#hardware-timer-based-measurement)
   - [GPIO Toggle Method](#gpio-toggle-method)
   - [Round-Trip Latency Test](#round-trip-latency-test)
   - [Interrupt Latency Measurement](#interrupt-latency-measurement)
   - [DMA-Based Latency Benchmarking](#dma-based-latency-benchmarking)
   - [Statistical Latency Analysis](#statistical-latency-analysis)
5. [Rust Implementation](#rust-implementation)
   - [Embedded Rust Latency Measurement](#embedded-rust-latency-measurement)
   - [Rust Round-Trip Benchmark](#rust-round-trip-benchmark)
   - [Rust Statistical Collector](#rust-statistical-collector)
6. [Optimization Strategies](#optimization-strategies)
7. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) latency refers to the total elapsed time from the moment a byte or frame is ready to be transmitted until the final byte of the response is received and processed by the application. In real-time, industrial, and safety-critical systems, understanding and minimizing this latency is as important as the data rate itself.

**End-to-end UART latency** encompasses every stage in the communication pipeline:

- Application triggers a write
- Data moves through software buffers
- The UART peripheral serializes bits onto the wire
- The signal propagates (negligible at typical distances)
- The receiver deserializes the frame
- An interrupt or polling loop detects the received data
- The application reads the result

Seemingly simple, this pipeline can accumulate latency from dozens of microseconds to tens of milliseconds depending on configuration choices. This chapter covers how to measure each stage precisely and how to eliminate unnecessary delay.

---

## Sources of UART Latency

Understanding where time is lost is prerequisite to eliminating it.

| Latency Source | Typical Range | Controllable? |
|---|---|---|
| Software buffering / OS scheduling | 0.1 ms – 50 ms | Yes |
| UART TX shift register serialization | Fixed (baud-dependent) | Partially |
| FIFO drain time | 0 – few ms | Yes |
| Interrupt service latency | 1 µs – 100 µs | Yes |
| DMA setup overhead | 1 µs – 10 µs | Yes |
| Driver wakeup (`tcdrain`, `write()` return) | 0 – 5 ms | Yes |
| Cable / signal propagation | < 1 µs | No |
| Receiver FIFO timeout | 0 – several char times | Yes |

### Serialization Time

The minimum time to transmit `N` bytes at baud rate `B` with 1 start bit, 8 data bits, 1 stop bit (10 bits/byte):

```
T_serial = N * 10 / B   seconds
```

At 115200 baud: one byte = ~86.8 µs. This is irreducible at a given baud rate.

### VMIN/VTIME and OS-Level Buffering (Linux)

On Linux, the termios `VMIN`/`VTIME` settings directly affect how quickly received data is delivered to the application. Poorly chosen values are the single most common source of unexpected UART latency.

---

## Latency Measurement Fundamentals

### The GPIO Toggle Method

The most hardware-accurate measurement technique: assert a GPIO pin at the moment of software transmit, de-assert it when the response is fully received. A logic analyzer or oscilloscope then measures the pulse width directly in the physical domain, eliminating any software measurement overhead.

### High-Resolution Software Timers

When a logic analyzer is unavailable, `clock_gettime(CLOCK_MONOTONIC)` on Linux or hardware cycle counters on bare-metal MCUs provide nanosecond resolution.

### Round-Trip Time (RTT) Testing

Connect TX to RX in loopback, or use a known-latency echo responder on the remote end. Measure RTT, then subtract the known serialization time to isolate system overhead.

### Statistical Collection

Single measurements are misleading. Collect minimum, maximum, mean, and percentile distributions (P50, P95, P99) over thousands of iterations to characterize jitter as well as average latency.

---

## C/C++ Implementation

### Hardware Timer-Based Measurement

```c
/*
 * uart_latency_timer.c
 *
 * Bare-metal (ARM Cortex-M) UART latency measurement using
 * the DWT (Data Watchpoint and Trace) cycle counter.
 *
 * Connect UART TX to RX for loopback, or wire to an echo device.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---- DWT Cycle Counter (ARM Cortex-M3/M4/M7) ---- */
#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004)
#define CoreDebug_DEMCR (*(volatile uint32_t*)0xE000EDFC)

static inline void dwt_enable(void) {
    CoreDebug_DEMCR |= (1u << 24); /* TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL  |= 1u;               /* CYCCNTENA */
}

static inline uint32_t dwt_cycles(void) {
    return DWT_CYCCNT;
}

/* ---- UART Register Abstraction (USART1 on STM32) ---- */
#define USART1_SR  (*(volatile uint32_t*)0x40013800)
#define USART1_DR  (*(volatile uint32_t*)0x40013804)
#define USART_SR_TXE   (1u << 7)   /* TX data register empty  */
#define USART_SR_TC    (1u << 6)   /* TX complete             */
#define USART_SR_RXNE  (1u << 5)   /* RX not empty            */

/* Blocking byte transmit — returns cycle count at TX start */
static uint32_t uart_send_byte(uint8_t byte) {
    while (!(USART1_SR & USART_SR_TXE));   /* wait for DR empty */
    uint32_t t0 = dwt_cycles();
    USART1_DR = byte;
    return t0;
}

/* Blocking byte receive — returns cycle count at byte arrival */
static uint32_t uart_recv_byte(uint8_t *out) {
    while (!(USART1_SR & USART_SR_RXNE));
    uint32_t t1 = dwt_cycles();
    *out = (uint8_t)USART1_DR;
    return t1;
}

/* ---- Latency Measurement ---- */
#define SYSTEM_CORE_CLOCK 168000000UL   /* 168 MHz STM32F4 */
#define TRIALS            1000

typedef struct {
    uint32_t min_us;
    uint32_t max_us;
    uint32_t mean_us;
    uint32_t cycles_raw[TRIALS];
} LatencyStats;

static LatencyStats measure_loopback_latency(void) {
    LatencyStats stats = {0};
    stats.min_us = UINT32_MAX;

    uint64_t sum = 0;

    for (int i = 0; i < TRIALS; i++) {
        uint8_t recv;
        uint32_t t0 = uart_send_byte(0xAA);
        uint32_t t1 = uart_recv_byte(&recv);

        uint32_t delta_cycles = t1 - t0;
        uint32_t delta_us = (uint32_t)((uint64_t)delta_cycles * 1000000UL
                                        / SYSTEM_CORE_CLOCK);

        stats.cycles_raw[i] = delta_cycles;
        if (delta_us < stats.min_us) stats.min_us = delta_us;
        if (delta_us > stats.max_us) stats.max_us = delta_us;
        sum += delta_us;

        /* Small inter-frame gap to avoid back-to-back collisions */
        for (volatile int d = 0; d < 1000; d++);
    }

    stats.mean_us = (uint32_t)(sum / TRIALS);
    return stats;
}
```

---

### GPIO Toggle Method

```c
/*
 * uart_latency_gpio.c
 *
 * Assert GPIO at the moment of UART write; de-assert on RX complete.
 * Measure the pulse width externally with a logic analyzer.
 *
 * This is the gold standard — no software measurement error.
 */

#include <stdint.h>

/* ---- GPIO abstraction (GPIOA pin 5, STM32) ---- */
#define GPIOA_BSRR  (*(volatile uint32_t*)0x40020018)
#define PROBE_PIN   5u

static inline void probe_set(void)   { GPIOA_BSRR = (1u << PROBE_PIN); }
static inline void probe_clear(void) { GPIOA_BSRR = (1u << (PROBE_PIN + 16)); }

/* ---- UART Send + Receive with GPIO probe ---- */
void uart_latency_gpio_test(void) {
    uint8_t recv;

    probe_set();                       /* START: assert probe */

    uart_send_byte(0x55);              /* Transmit test byte  */

    /* Wait until TC (transmission complete) to mark wire-level start */
    while (!(USART1_SR & USART_SR_TC));

    uart_recv_byte(&recv);             /* Block until echo received */

    probe_clear();                     /* STOP: de-assert probe */

    /* Pulse width on scope = loopback latency at the wire level */
}
```

---

### Round-Trip Latency Test

```c
/*
 * uart_rtt_linux.c
 *
 * POSIX / Linux round-trip latency measurement.
 * Requires a loopback cable (TX->RX) or an echo server on the remote end.
 *
 * Compile: gcc -O2 -o uart_rtt uart_rtt_linux.c
 * Usage:   ./uart_rtt /dev/ttyUSB0 115200
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#define TRIALS      2000
#define PAYLOAD_LEN 1

/* ---- Utility: nanoseconds between two timespecs ---- */
static int64_t ts_diff_ns(struct timespec a, struct timespec b) {
    return (int64_t)(b.tv_sec  - a.tv_sec ) * 1000000000LL
         + (int64_t)(b.tv_nsec - a.tv_nsec);
}

/* ---- Configure serial port for low latency ---- */
static int open_serial(const char *dev, int baud_const) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); exit(1); }

    /* Switch to blocking after open */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty;
    tcgetattr(fd, &tty);

    cfmakeraw(&tty);
    cfsetispeed(&tty, baud_const);
    cfsetospeed(&tty, baud_const);

    tty.c_cflag  = CS8 | CREAD | CLOCAL; /* 8N1, no flow control */
    tty.c_iflag  = 0;
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;

    /* VMIN=1, VTIME=0: return immediately when ≥1 byte available.
     * This is critical for minimising receive latency. */
    tty.c_cc[VMIN]  = PAYLOAD_LEN;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* ---- Enable low-latency flag (Linux-specific) ---- */
static void set_low_latency(int fd) {
#ifdef TIOCGSERIAL
    struct serial_struct ss;
    if (ioctl(fd, TIOCGSERIAL, &ss) == 0) {
        ss.flags |= ASYNC_LOW_LATENCY;
        ioctl(fd, TIOCSSERIAL, &ss);
    }
#endif
}

/* ---- Statistics ---- */
typedef struct {
    int64_t samples[TRIALS];
    int64_t min_ns, max_ns;
    double  mean_ns;
    int     count;
} RttStats;

static void stats_add(RttStats *s, int64_t ns) {
    s->samples[s->count++] = ns;
    if (ns < s->min_ns) s->min_ns = ns;
    if (ns > s->max_ns) s->max_ns = ns;
    s->mean_ns += (double)ns;
}

static int cmp_int64(const void *a, const void *b) {
    int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
    return (x > y) - (x < y);
}

static void stats_report(RttStats *s) {
    s->mean_ns /= s->count;
    qsort(s->samples, s->count, sizeof(int64_t), cmp_int64);

    printf("=== UART Round-Trip Latency (%d trials) ===\n", s->count);
    printf("  Min  : %7.1f µs\n", s->min_ns  / 1000.0);
    printf("  P50  : %7.1f µs\n", s->samples[s->count / 2]           / 1000.0);
    printf("  P95  : %7.1f µs\n", s->samples[(int)(s->count * 0.95)] / 1000.0);
    printf("  P99  : %7.1f µs\n", s->samples[(int)(s->count * 0.99)] / 1000.0);
    printf("  Max  : %7.1f µs\n", s->max_ns  / 1000.0);
    printf("  Mean : %7.1f µs\n", s->mean_ns / 1000.0);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device> <baud>\n", argv[0]);
        return 1;
    }

    /* Map baud integer to termios constant */
    int baud_val = atoi(argv[2]);
    speed_t baud_const;
    switch (baud_val) {
        case 9600:   baud_const = B9600;   break;
        case 115200: baud_const = B115200; break;
        case 230400: baud_const = B230400; break;
        case 921600: baud_const = B921600; break;
        default:
            fprintf(stderr, "Unsupported baud rate\n");
            return 1;
    }

    int fd = open_serial(argv[1], baud_const);
    set_low_latency(fd);

    RttStats stats = { .min_ns = INT64_MAX };
    uint8_t tx_buf[PAYLOAD_LEN];
    uint8_t rx_buf[PAYLOAD_LEN];

    /* Warm-up: discard first few exchanges */
    for (int i = 0; i < 5; i++) {
        tx_buf[0] = 0xAA;
        write(fd, tx_buf, PAYLOAD_LEN);
        read(fd, rx_buf, PAYLOAD_LEN);
        usleep(1000);
    }
    tcflush(fd, TCIOFLUSH);

    for (int i = 0; i < TRIALS; i++) {
        tx_buf[0] = (uint8_t)(i & 0xFF);

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        ssize_t written = write(fd, tx_buf, PAYLOAD_LEN);
        if (written != PAYLOAD_LEN) { perror("write"); break; }

        /* Block until full response received */
        ssize_t total = 0;
        while (total < PAYLOAD_LEN) {
            ssize_t r = read(fd, rx_buf + total, PAYLOAD_LEN - total);
            if (r < 0) { perror("read"); goto done; }
            total += r;
        }

        clock_gettime(CLOCK_MONOTONIC, &t1);
        stats_add(&stats, ts_diff_ns(t0, t1));

        /* ~500 µs inter-frame gap */
        usleep(500);
    }

done:
    stats_report(&stats);
    close(fd);
    return 0;
}
```

---

### Interrupt Latency Measurement

```c
/*
 * uart_irq_latency.c
 *
 * Measure the latency from UART RX interrupt firing to the
 * ISR body executing — bare-metal ARM Cortex-M.
 *
 * Method:
 *  1. Record DWT cycle count at the moment the UART RX interrupt
 *     flag is polled as set (simulates when HW would fire IRQ).
 *  2. In the ISR, immediately capture DWT cycle count.
 *  3. Delta = interrupt dispatch latency.
 */

#include <stdint.h>

#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004)

volatile uint32_t irq_entry_cycle  = 0;
volatile uint32_t irq_detect_cycle = 0;
volatile bool     irq_fired        = false;

/* USART1 IRQ Handler — stored in vector table */
void USART1_IRQHandler(void) {
    irq_entry_cycle = DWT_CYCCNT;   /* Capture as early as possible */

    if (USART1_SR & USART_SR_RXNE) {
        volatile uint8_t data = (uint8_t)USART1_DR; /* Clear RXNE */
        (void)data;
        irq_fired = true;
    }
}

/* Called from main loop to enable RX interrupt and wait */
uint32_t measure_irq_latency_cycles(void) {
    irq_fired = false;

    /* Enable RXNE interrupt */
    /* USART1_CR1 |= USART_CR1_RXNEIE; */

    /* Send a byte to ourselves (loopback) */
    uint32_t detect_cycle = uart_send_byte(0x5A);

    /* Poll until IRQ fires */
    while (!irq_fired);

    /* Disable RXNE interrupt */
    /* USART1_CR1 &= ~USART_CR1_RXNEIE; */

    return irq_entry_cycle - detect_cycle;
}
```

---

### DMA-Based Latency Benchmarking

```c
/*
 * uart_dma_latency.c
 *
 * Measure UART DMA transfer latency on STM32 (HAL layer).
 * DMA TX: CPU is free during transfer — measure from DMA start
 *         to DMA TC (Transfer Complete) callback.
 */

#include "stm32f4xx_hal.h"
#include <stdint.h>

extern UART_HandleTypeDef huart1;

volatile uint32_t dma_start_tick = 0;
volatile uint32_t dma_done_tick  = 0;
volatile bool     dma_done       = false;

/* HAL DMA TX complete callback */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        dma_done_tick = DWT_CYCCNT;
        dma_done = true;
    }
}

uint32_t measure_dma_tx_latency(uint8_t *buf, uint16_t len) {
    dma_done = false;

    dma_start_tick = DWT_CYCCNT;
    HAL_UART_Transmit_DMA(&huart1, buf, len);

    while (!dma_done);

    return dma_done_tick - dma_start_tick;
}

/*
 * Expected latency:
 *   T = len * 10 / baud  seconds  (serialization time dominates)
 *   DMA overhead ≈ 1–5 µs additional
 *
 * Compare to polling TX: identical serialization time, but CPU is
 * busy-waiting instead of free. DMA saves CPU cycles, not wall time.
 */
```

---

### Statistical Latency Analysis

```cpp
/*
 * uart_latency_stats.cpp
 *
 * C++ latency analysis utility.
 * Collect measurements, compute percentiles, detect outliers.
 */

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <numeric>
#include <vector>
#include <cmath>

class UartLatencyAnalyzer {
public:
    explicit UartLatencyAnalyzer(size_t capacity = 10000)
    {
        samples_.reserve(capacity);
    }

    void record(int64_t latency_ns) {
        samples_.push_back(latency_ns);
    }

    struct Report {
        int64_t min_ns;
        int64_t max_ns;
        double  mean_ns;
        double  stddev_ns;
        int64_t p50_ns;
        int64_t p90_ns;
        int64_t p95_ns;
        int64_t p99_ns;
        size_t  outliers;      /* > mean + 3*σ */
        double  serialization_ns; /* theoretical minimum */
    };

    Report analyse(int baud_rate, int bytes_per_frame = 1) const {
        if (samples_.empty()) return {};

        std::vector<int64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());

        size_t n = sorted.size();

        double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / n;

        double variance = 0.0;
        for (auto v : sorted)
            variance += (v - mean) * (v - mean);
        variance /= n;
        double stddev = std::sqrt(variance);

        double serial_ns = (bytes_per_frame * 10.0 / baud_rate) * 1e9;

        size_t outliers = 0;
        for (auto v : sorted)
            if (v > mean + 3.0 * stddev) ++outliers;

        return {
            sorted.front(),
            sorted.back(),
            mean,
            stddev,
            sorted[n / 2],
            sorted[(size_t)(n * 0.90)],
            sorted[(size_t)(n * 0.95)],
            sorted[(size_t)(n * 0.99)],
            outliers,
            serial_ns
        };
    }

    void print_report(int baud_rate, int bytes = 1) const {
        auto r = analyse(baud_rate, bytes);
        double serial_us = r.serialization_ns / 1000.0;

        printf("=== UART Latency Analysis (%zu samples, %d baud) ===\n",
               samples_.size(), baud_rate);
        printf("  Theoretical min (serialization): %6.1f µs\n", serial_us);
        printf("  Measured minimum : %6.1f µs  (overhead: %.1f µs)\n",
               r.min_ns / 1000.0, (r.min_ns - r.serialization_ns) / 1000.0);
        printf("  P50              : %6.1f µs\n", r.p50_ns  / 1000.0);
        printf("  P90              : %6.1f µs\n", r.p90_ns  / 1000.0);
        printf("  P95              : %6.1f µs\n", r.p95_ns  / 1000.0);
        printf("  P99              : %6.1f µs\n", r.p99_ns  / 1000.0);
        printf("  Maximum          : %6.1f µs\n", r.max_ns  / 1000.0);
        printf("  Mean ± σ         : %.1f ± %.1f µs\n",
               r.mean_ns / 1000.0, r.stddev_ns / 1000.0);
        printf("  Outliers (>3σ)   : %zu (%.1f%%)\n",
               r.outliers, 100.0 * r.outliers / samples_.size());
    }

private:
    std::vector<int64_t> samples_;
};
```

---

## Rust Implementation

### Embedded Rust Latency Measurement

```rust
//! uart_latency_embedded.rs
//!
//! Bare-metal latency measurement for embedded Rust using RTIC and
//! the ARM DWT cycle counter. Targets STM32F4 (cortex-m4).
//!
//! [dependencies]
//! cortex-m     = "0.7"
//! cortex-m-rt  = "0.7"
//! stm32f4xx-hal = { version = "0.19", features = ["stm32f411"] }

#![no_std]
#![no_main]

use cortex_m::peripheral::DWT;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{Config, Serial},
};
use core::fmt::Write;

/// Read the DWT cycle counter (must be enabled first).
#[inline(always)]
fn cycles() -> u32 {
    // SAFETY: read-only access to DWT CYCCNT register
    unsafe { (*DWT::PTR).cyccnt.read() }
}

/// Serialization time in cycles for one UART frame (10 bits/byte).
fn serialization_cycles(baud: u32, core_clock: u32) -> u32 {
    // cycles_per_bit = core_clock / baud
    // cycles_per_frame = 10 * cycles_per_bit
    10 * core_clock / baud
}

pub struct LatencyMeasurement {
    pub min_cycles: u32,
    pub max_cycles: u32,
    pub sum_cycles: u64,
    pub count: u32,
}

impl LatencyMeasurement {
    pub const fn new() -> Self {
        Self {
            min_cycles: u32::MAX,
            max_cycles: 0,
            sum_cycles: 0,
            count: 0,
        }
    }

    /// Record one sample in cycles.
    pub fn record(&mut self, delta: u32) {
        if delta < self.min_cycles { self.min_cycles = delta; }
        if delta > self.max_cycles { self.max_cycles = delta; }
        self.sum_cycles = self.sum_cycles.saturating_add(delta as u64);
        self.count += 1;
    }

    /// Mean in cycles.
    pub fn mean_cycles(&self) -> u32 {
        if self.count == 0 { 0 } else { (self.sum_cycles / self.count as u64) as u32 }
    }

    /// Convert cycles to microseconds.
    pub fn cycles_to_us(cycles: u32, core_clock_hz: u32) -> u32 {
        (cycles as u64 * 1_000_000 / core_clock_hz as u64) as u32
    }
}

/// Measure loopback latency over `trials` iterations.
///
/// # Arguments
/// * `tx` - mutable reference to UART transmitter
/// * `rx` - mutable reference to UART receiver  
/// * `trials` - number of measurement iterations
/// * `core_clock` - CPU frequency in Hz
pub fn measure_loopback<TX, RX>(
    tx: &mut TX,
    rx: &mut RX,
    trials: u32,
    core_clock: u32,
) -> LatencyMeasurement
where
    TX: embedded_hal::serial::Write<u8>,
    RX: embedded_hal::serial::Read<u8>,
{
    let mut stats = LatencyMeasurement::new();

    for _ in 0..trials {
        // Record cycle count just before transmit
        let t0 = cycles();

        // Blocking write
        nb::block!(tx.write(0xAA_u8)).ok();
        nb::block!(tx.flush()).ok();

        // Blocking read until byte received
        let _byte = nb::block!(rx.read()).unwrap_or(0);

        let t1 = cycles();

        // Handle counter wraparound with wrapping subtraction
        stats.record(t1.wrapping_sub(t0));

        // Brief inter-frame delay (busy loop)
        cortex_m::asm::delay(core_clock / 10_000); // ~100 µs
    }

    stats
}
```

---

### Rust Round-Trip Benchmark

```rust
//! uart_rtt_bench.rs
//!
//! Linux/POSIX round-trip UART latency benchmark in Rust.
//! Connects to a loopback cable or echo device.
//!
//! [dependencies]
//! serialport = "4"
//! clap       = { version = "4", features = ["derive"] }

use serialport::{SerialPort, SerialPortBuilder};
use std::io::{Read, Write};
use std::time::{Duration, Instant};
use clap::Parser;

#[derive(Parser)]
struct Args {
    /// Serial device path, e.g. /dev/ttyUSB0
    #[arg(short, long)]
    device: String,

    /// Baud rate
    #[arg(short, long, default_value_t = 115200)]
    baud: u32,

    /// Number of measurement trials
    #[arg(short, long, default_value_t = 2000)]
    trials: usize,
}

struct Stats {
    samples: Vec<Duration>,
}

impl Stats {
    fn new(capacity: usize) -> Self {
        Self { samples: Vec::with_capacity(capacity) }
    }

    fn record(&mut self, d: Duration) {
        self.samples.push(d);
    }

    fn percentile(&self, sorted: &[Duration], pct: f64) -> Duration {
        let idx = ((sorted.len() as f64) * pct / 100.0) as usize;
        sorted[idx.min(sorted.len() - 1)]
    }

    fn report(&self, baud: u32, payload_bytes: usize) {
        let mut sorted = self.samples.clone();
        sorted.sort_unstable();

        let n = sorted.len() as f64;
        let mean = self.samples.iter().map(|d| d.as_nanos() as f64).sum::<f64>() / n;
        let variance = self.samples.iter()
            .map(|d| { let diff = d.as_nanos() as f64 - mean; diff * diff })
            .sum::<f64>() / n;
        let stddev = variance.sqrt();

        // Theoretical serialization time for round-trip (TX + RX)
        let serial_us = (payload_bytes as f64 * 10.0 / baud as f64) * 2.0 * 1e6;

        println!("=== UART Round-Trip Latency ({} trials, {} baud) ===", self.samples.len(), baud);
        println!("  Theoretical min (2× serialization): {:.1} µs", serial_us);
        println!("  Minimum  : {:.1} µs", sorted.first().unwrap().as_nanos() as f64 / 1000.0);
        println!("  P50      : {:.1} µs", self.percentile(&sorted, 50.0).as_nanos() as f64 / 1000.0);
        println!("  P95      : {:.1} µs", self.percentile(&sorted, 95.0).as_nanos() as f64 / 1000.0);
        println!("  P99      : {:.1} µs", self.percentile(&sorted, 99.0).as_nanos() as f64 / 1000.0);
        println!("  Maximum  : {:.1} µs", sorted.last().unwrap().as_nanos() as f64 / 1000.0);
        println!("  Mean ± σ : {:.1} ± {:.1} µs", mean / 1000.0, stddev / 1000.0);
    }
}

fn open_port(device: &str, baud: u32) -> Box<dyn SerialPort> {
    serialport::new(device, baud)
        .timeout(Duration::from_millis(500))
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .open()
        .unwrap_or_else(|e| {
            eprintln!("Failed to open {}: {}", device, e);
            std::process::exit(1);
        })
}

fn main() {
    let args = Args::parse();
    let mut port = open_port(&args.device, args.baud);
    let mut stats = Stats::new(args.trials);

    let payload: [u8; 1] = [0xAA];
    let mut rx_buf = [0u8; 1];

    // Warm-up passes
    for _ in 0..5 {
        port.write_all(&payload).ok();
        port.read_exact(&mut rx_buf).ok();
        std::thread::sleep(Duration::from_millis(1));
    }

    // Clear any stale data
    port.clear(serialport::ClearBuffer::All).ok();

    println!("Measuring {} trials on {} at {} baud...", args.trials, args.device, args.baud);

    for i in 0..args.trials {
        let tx = [i as u8 & 0xFF];

        let t0 = Instant::now();

        port.write_all(&tx)
            .unwrap_or_else(|e| eprintln!("Write error: {}", e));

        // Read until full payload received
        let mut received = 0;
        while received < payload.len() {
            match port.read(&mut rx_buf[received..]) {
                Ok(n) => received += n,
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {
                    eprintln!("Timeout at trial {}", i);
                    break;
                }
                Err(e) => { eprintln!("Read error: {}", e); break; }
            }
        }

        stats.record(t0.elapsed());

        // Inter-frame gap ~500 µs
        std::thread::sleep(Duration::from_micros(500));
    }

    stats.report(args.baud, payload.len());
}
```

---

### Rust Statistical Collector

```rust
//! latency_stats.rs
//!
//! Reusable latency statistics module for both std and no_std environments.
//! In no_std, use with a fixed-size heapless::Vec.

/// Latency sample collector with online statistics.
///
/// Maintains min, max, and Welford's online mean/variance
/// without storing all samples (memory-efficient for embedded).
#[derive(Debug, Clone)]
pub struct OnlineStats {
    count: u64,
    min_ns: i64,
    max_ns: i64,
    mean: f64,
    m2: f64,          // For Welford's algorithm
}

impl OnlineStats {
    pub const fn new() -> Self {
        Self {
            count: 0,
            min_ns: i64::MAX,
            max_ns: i64::MIN,
            mean: 0.0,
            m2: 0.0,
        }
    }

    /// Add a single latency sample in nanoseconds.
    pub fn update(&mut self, sample_ns: i64) {
        self.count += 1;

        if sample_ns < self.min_ns { self.min_ns = sample_ns; }
        if sample_ns > self.max_ns { self.max_ns = sample_ns; }

        // Welford's online algorithm for stable mean and variance
        let delta  = sample_ns as f64 - self.mean;
        self.mean += delta / self.count as f64;
        let delta2 = sample_ns as f64 - self.mean;
        self.m2   += delta * delta2;
    }

    pub fn count(&self) -> u64 { self.count }
    pub fn min_us(&self) -> f64 { self.min_ns as f64 / 1_000.0 }
    pub fn max_us(&self) -> f64 { self.max_ns as f64 / 1_000.0 }
    pub fn mean_us(&self) -> f64 { self.mean / 1_000.0 }

    /// Population standard deviation in microseconds.
    pub fn stddev_us(&self) -> f64 {
        if self.count < 2 {
            return 0.0;
        }
        (self.m2 / self.count as f64).sqrt() / 1_000.0
    }

    /// Coefficient of variation (σ/µ) — useful for jitter characterization.
    pub fn cv(&self) -> f64 {
        if self.mean == 0.0 { 0.0 } else { self.stddev_us() / self.mean_us() }
    }

    #[cfg(feature = "std")]
    pub fn print_summary(&self) {
        println!(
            "Samples: {} | Min: {:.1} µs | Mean: {:.1} µs | Max: {:.1} µs | σ: {:.1} µs | CV: {:.3}",
            self.count, self.min_us(), self.mean_us(), self.max_us(), self.stddev_us(), self.cv()
        );
    }
}

/// UART latency budget calculator.
///
/// Given a target maximum latency, compute how each component
/// must be bounded.
pub struct LatencyBudget {
    pub target_us:         f64,
    pub serialization_us:  f64,
    pub irq_overhead_us:   f64,
    pub driver_overhead_us: f64,
}

impl LatencyBudget {
    /// Compute serialization time in microseconds.
    ///
    /// # Arguments
    /// * `baud` - UART baud rate (bits/second)
    /// * `bytes` - Number of bytes in the frame
    pub fn serialization(baud: u32, bytes: u32) -> f64 {
        bytes as f64 * 10.0 / baud as f64 * 1_000_000.0
    }

    /// Remaining budget after accounting for known overheads.
    pub fn software_budget_us(&self) -> f64 {
        self.target_us
            - self.serialization_us
            - self.irq_overhead_us
            - self.driver_overhead_us
    }

    pub fn is_achievable(&self) -> bool {
        self.software_budget_us() >= 0.0
    }

    pub fn report(&self) {
        println!("=== UART Latency Budget ===");
        println!("  Target              : {:.1} µs", self.target_us);
        println!("  - Serialization     : {:.1} µs", self.serialization_us);
        println!("  - IRQ overhead      : {:.1} µs", self.irq_overhead_us);
        println!("  - Driver overhead   : {:.1} µs", self.driver_overhead_us);
        println!("  = Software budget   : {:.1} µs  [{}]",
            self.software_budget_us(),
            if self.is_achievable() { "ACHIEVABLE" } else { "INFEASIBLE — increase baud or relax target" });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_serialization_115200() {
        // 1 byte at 115200 baud = 10 bits / 115200 bps ≈ 86.8 µs
        let us = LatencyBudget::serialization(115200, 1);
        assert!((us - 86.8).abs() < 0.5, "Got {:.2} µs", us);
    }

    #[test]
    fn test_online_stats_constant() {
        let mut s = OnlineStats::new();
        for _ in 0..100 {
            s.update(1_000); // 1 µs constant
        }
        assert_eq!(s.count(), 100);
        assert!((s.mean_us() - 1.0).abs() < 0.001);
        assert!(s.stddev_us() < 0.001); // zero variance for constant input
    }

    #[test]
    fn test_budget_feasibility() {
        let budget = LatencyBudget {
            target_us: 500.0,
            serialization_us: LatencyBudget::serialization(115200, 1),
            irq_overhead_us: 5.0,
            driver_overhead_us: 10.0,
        };
        assert!(budget.is_achievable());
    }
}
```

---

## Optimization Strategies

### 1. Increase Baud Rate

The single most impactful change. Doubling the baud rate halves serialization time. Verify signal integrity and cable length allow the higher rate.

### 2. Linux: Set `ASYNC_LOW_LATENCY`

```c
struct serial_struct ss;
ioctl(fd, TIOCGSERIAL, &ss);
ss.flags |= ASYNC_LOW_LATENCY;
ioctl(fd, TIOCSSERIAL, &ss);
```

This reduces the USB/UART receive interrupt coalescing latency from ~16 ms to ~1 ms on many USB-serial adapters.

### 3. Tune `VMIN` / `VTIME`

| Goal | `VMIN` | `VTIME` |
|---|---|---|
| Lowest latency, fixed frame size N | N | 0 |
| Timeout-based receive | 0 | T (tenths of seconds) |
| Timeout after first byte | 1 | T |

Setting `VMIN=0, VTIME=0` makes `read()` non-blocking — use with `select()` or `poll()` for efficient waiting.

### 4. Minimize Frame Size

Shorter frames have less serialization time. Use binary protocols instead of ASCII where possible.

### 5. Avoid `tcdrain()`

`tcdrain()` blocks until the transmit buffer is fully drained including hardware FIFO. Use it only when you need a guaranteed quiescent line. Avoid it in the latency measurement loop.

### 6. Use DMA for Large Frames

DMA does not reduce latency of small (1–4 byte) transfers — the DMA setup overhead can exceed the benefit. DMA reduces CPU time and can improve throughput for larger frames.

### 7. Real-Time OS Priority

On Linux, set `SCHED_FIFO` priority for the UART thread:

```c
struct sched_param sp = { .sched_priority = 80 };
pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
```

### 8. CPU Frequency Scaling

Disable CPU frequency scaling (`cpufreq` governor to `performance`) to eliminate latency spikes from frequency transitions on Linux embedded boards.

### 9. USB-to-UART Adapter Latency

USB serial adapters add 1–16 ms of USB polling latency by default. Set the latency timer:

```bash
echo 1 > /sys/bus/usb-serial/devices/ttyUSB0/latency_timer
```

This reduces USB bulk transfer aggregation to 1 ms.

---

## Summary

UART end-to-end latency has two fundamentally different components: **irreducible serialization delay** (determined purely by baud rate and frame size) and **system overhead** (software buffering, interrupt dispatch, OS scheduling, driver configuration). Precise measurement is the essential first step — without it, optimizations are guesswork.

**Key measurement techniques:**
- **GPIO toggle + logic analyzer** — most accurate, eliminates all software overhead from the measurement
- **DWT cycle counter (bare-metal)** — nanosecond resolution on ARM Cortex-M with no OS dependency
- **`CLOCK_MONOTONIC` with RTT loop (Linux)** — portable, sufficient for characterizing latency above ~1 µs

**Key optimization levers, ranked by impact:**
1. Increase baud rate — directly reduces serialization time
2. Set `ASYNC_LOW_LATENCY` and tune `VMIN`/`VTIME` on Linux
3. Minimize frame payload size
4. Set USB latency timer to 1 ms for USB-serial adapters
5. Use `SCHED_FIFO` real-time scheduling for latency-sensitive threads
6. Disable CPU frequency scaling on embedded Linux boards

**Always collect statistical distributions** (P50, P95, P99) rather than single measurements. The worst-case tail latency — not the average — determines whether a real-time deadline will be met. The Rust `OnlineStats` and C++ `UartLatencyAnalyzer` examples in this chapter provide ready-made tools for systematic latency characterization.

At 115200 baud, the minimum latency for a single-byte loopback is approximately 87 µs. A well-tuned Linux system can achieve total round-trip latencies of 200–500 µs; a bare-metal MCU can achieve under 10 µs of overhead above the serialization floor.

---

*Topic 96 of the UART Programming Series*