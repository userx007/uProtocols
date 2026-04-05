# 33. Bit Error Rate Testing (BERT) in UART Communication

**Theory** — BER definition, statistical confidence (why you need ≥ 3/BER_target bits), noise sources in UART links, and the Q-factor relationship between SNR and BER.

**PRBS Patterns** — How LFSRs generate PRBS-7/9/15/23/31 sequences, with the full polynomial tap tables and recurrence formulas.

**C/C++ Code**
- A standalone `prbs_generator.h` implementing a configurable Fibonacci LFSR
- A complete POSIX UART BERT tool (`bert.c`) with live statistics, 95% confidence upper bounds, and an exit code tied to BER threshold
- A C++ `Bert` class using callbacks (suitable for embedded HAL abstraction) with PRBS-23
- A DMA-driven STM32 HAL example for hardware-accelerated testing

**Rust Code**
- A `Prbs` struct supporting PRBS-9 and PRBS-23, using `count_ones()` for efficient popcount
- A `BertStats` struct with BER, FER, confidence bounds, and a formatted report
- A synchronous `BertRunner` using the `serialport` crate with a CLI entry point
- An async variant using `tokio-serial` for non-blocking multi-port testing

**Interpretation** — BER threshold table, burst vs. random error diagnosis, and the ±2% baud tolerance check.

## Table of Contents

1. [Introduction](#introduction)
2. [Theoretical Background](#theoretical-background)
3. [BER Metrics and Formulas](#ber-metrics-and-formulas)
4. [PRBS Test Patterns](#prbs-test-patterns)
5. [BERT Architecture](#bert-architecture)
6. [Implementation in C/C++](#implementation-in-cc)
7. [Implementation in Rust](#implementation-in-rust)
8. [Hardware-Assisted BERT](#hardware-assisted-bert)
9. [Interpreting Results](#interpreting-results)
10. [Summary](#summary)

---

## Introduction

Bit Error Rate Testing (BERT) is a fundamental diagnostic and validation technique used to measure the quality and reliability of a UART serial communication link. While UART is a relatively simple asynchronous protocol, real-world links are affected by noise, impedance mismatches, cable length, baud rate tolerances, and electromagnetic interference — all of which introduce errors at the bit level.

A **Bit Error Rate (BER)** test works by transmitting a known, deterministic sequence of bits over a link and comparing what was received against what was expected. Any mismatch at a specific bit position constitutes a **bit error**. By counting errors over a sufficiently large sample, we can statistically characterise the link's health.

BER testing is used during:

- Hardware bring-up and validation of new UART designs
- Production testing of manufactured boards
- Stress testing at temperature or voltage extremes
- Cable and connector qualification
- Firmware regression testing after baud rate or clock changes

---

## Theoretical Background

### What is Bit Error Rate?

The Bit Error Rate is defined as:

```
BER = (Number of Bit Errors) / (Total Number of Bits Transmitted)
```

It is a dimensionless ratio, often expressed as a power of 10, for example:

- `BER = 1e-6` means one error per million bits transmitted
- `BER = 1e-9` is a common target for high-quality links (one error per billion bits)

### Sources of Bit Errors in UART

| Source | Description |
|---|---|
| **Thermal noise (AWGN)** | Random voltage fluctuations in resistors and transistors |
| **Clock/baud rate mismatch** | Transmitter and receiver baud rates may drift apart |
| **Framing errors** | Misaligned start/stop bit detection |
| **Signal reflections** | Impedance discontinuities on long cables |
| **Crosstalk** | Adjacent signal lines coupling energy into the UART line |
| **Power supply noise** | Voltage droop causing logic level uncertainty |
| **EMI/RFI** | External electromagnetic interference |

### Statistical Confidence

Because BER is a probabilistic measure, a minimum number of bits must be transmitted before the result is statistically meaningful. The rule of thumb for 95% confidence that no errors exist at a target BER is:

```
N_bits >= 3 / BER_target
```

For a target BER of `1e-9`, you must transmit at least **3 × 10^9 bits** without error. At 115200 baud with 10 bits per UART frame (1 start + 8 data + 1 stop), that takes approximately **7.2 hours** of continuous transmission.

---

## BER Metrics and Formulas

### Core Metrics

```
BER         = errors / total_bits
Frame Error Rate (FER) = frame_errors / total_frames
Error-Free Seconds (EFS) = seconds with zero errors / total seconds
Errored Seconds (ES)    = seconds with ≥1 error    / total seconds
```

### Confidence Interval

Given `k` observed errors in `n` transmitted bits, a simple Poisson approximation for the upper 95% confidence bound on BER is:

```
BER_upper = chi2(0.95, 2*(k+1)) / (2 * n)
```

For zero errors (`k=0`), this simplifies to:

```
BER_upper ≈ 3 / n      (95% confidence)
BER_upper ≈ 4.6 / n    (99% confidence)
```

### Q-Factor

The Q-factor relates BER to the signal-to-noise ratio for a Gaussian noise model:

```
BER ≈ (1/2) * erfc(Q / sqrt(2))
```

A `BER = 1e-9` corresponds to approximately `Q = 6`, and `BER = 1e-12` corresponds to `Q ≈ 7.03`.

---

## PRBS Test Patterns

### What is PRBS?

A **Pseudo-Random Bit Sequence (PRBS)** is a deterministic sequence that statistically resembles white noise. It is generated by a **Linear Feedback Shift Register (LFSR)**, which produces a maximal-length sequence of `2^n - 1` bits before repeating.

Common PRBS standards used in serial link testing:

| Pattern | Period (bits) | Taps | Use Case |
|---|---|---|---|
| PRBS-7 | 127 | [7, 6] | Short tests, quick checks |
| PRBS-9 | 511 | [9, 5] | General purpose UART |
| PRBS-15 | 32,767 | [15, 14] | Medium-speed links |
| PRBS-23 | 8,388,607 | [23, 18] | High-quality production test |
| PRBS-31 | 2,147,483,647 | [31, 28] | Long-duration stress test |

### LFSR Implementation

A Fibonacci LFSR for PRBS-7 uses the recurrence:

```
bit[n] = bit[n-7] XOR bit[n-6]
state  = (state << 1) | new_bit
output = MSB of state
```

---

## BERT Architecture

A complete BERT system for UART consists of:

```
┌─────────────────────────────────────────────────────────────────┐
│                        BERT System                              │
│                                                                 │
│  ┌──────────────┐    UART TX     ┌──────────────────────────┐   │
│  │  Pattern     │ ─────────────► │       DUT / Link         │   │
│  │  Generator   │                │  (cable, board, MCU)     │   │
│  │  (PRBS LFSR) │ ◄────────────  │                          │   │
│  └──────────────┘    UART RX     └──────────────────────────┘   │
│         │                                                       │
│         ▼                                                       │
│  ┌──────────────┐                                               │
│  │  Error       │  ── counts errors, computes BER, EFS, FER     │
│  │  Detector    │                                               │
│  └──────────────┘                                               │
│         │                                                       │
│         ▼                                                       │
│  ┌──────────────┐                                               │
│  │  Statistics  │  ── BER, confidence, eye diagram metrics      │
│  │  Engine      │                                               │
│  └──────────────┘                                               │
└─────────────────────────────────────────────────────────────────┘
```

Two topologies are common:

- **Loopback**: TX is wired directly back to RX on the same device. Tests the UART peripheral and driver but not the external link.
- **Through-mode**: Separate transmitter and receiver. Required for cable, connector, or board-to-board qualification.

---

## Implementation in C/C++

### 1. LFSR-Based PRBS Generator

```c
/**
 * @file prbs_generator.h
 * @brief PRBS-9 and PRBS-23 pattern generators using Fibonacci LFSR
 */

#include <stdint.h>
#include <stdbool.h>

/* PRBS-9: polynomial x^9 + x^5 + 1, taps at bits 9 and 5 */
typedef struct {
    uint32_t state;   /* current LFSR state, must be non-zero */
    uint32_t taps;    /* XOR tap mask */
    uint8_t  length;  /* LFSR length in bits */
} prbs_ctx_t;

/**
 * @brief Initialise a PRBS-9 generator.
 * @param ctx  Pointer to PRBS context.
 * @param seed Non-zero seed value (use 0x1FF for all-ones start).
 */
void prbs9_init(prbs_ctx_t *ctx, uint32_t seed) {
    ctx->state  = seed & 0x1FF;   /* 9-bit mask */
    ctx->taps   = (1u << 8) | (1u << 4); /* bits 9,5 => indices 8,4 */
    ctx->length = 9;
    if (ctx->state == 0) ctx->state = 1; /* forbidden all-zero state */
}

/**
 * @brief Produce the next PRBS bit.
 * @return 0 or 1
 */
uint8_t prbs_next_bit(prbs_ctx_t *ctx) {
    uint32_t feedback = 0;
    uint32_t tmp = ctx->state & ctx->taps;
    /* XOR all set tap bits together for Fibonacci LFSR */
    while (tmp) {
        feedback ^= (tmp & 1u);
        tmp >>= 1;
    }
    ctx->state = ((ctx->state << 1) | feedback) & ((1u << ctx->length) - 1u);
    return (uint8_t)feedback;
}

/**
 * @brief Fill a buffer with PRBS bytes (MSB-first packing).
 */
void prbs_fill_buffer(prbs_ctx_t *ctx, uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = 0;
        for (int b = 7; b >= 0; b--) {
            byte |= (prbs_next_bit(ctx) << b);
        }
        buf[i] = byte;
    }
}
```

---

### 2. BERT Core in C (POSIX / Linux)

```c
/**
 * @file bert.c
 * @brief UART Bit Error Rate Tester — POSIX implementation.
 *
 * Compile: gcc -O2 -o bert bert.c -lm
 * Usage:   ./bert /dev/ttyUSB0 115200 1000000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

/* ── PRBS-9 context (as defined above) ─────────────────────────── */
/* (include prbs_generator.h here in practice)                      */

typedef struct {
    uint64_t bits_sent;
    uint64_t bits_received;
    uint64_t bit_errors;
    uint64_t frame_errors;   /* UART framing / parity errors */
    uint64_t bytes_checked;
    double   elapsed_seconds;
    double   ber;
    double   ber_confidence_upper; /* 95% upper bound */
} bert_stats_t;

/* ── Utility: open UART port ────────────────────────────────────── */
static int uart_open(const char *dev, int baud_rate) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return -1; }

    struct termios tio;
    tcgetattr(fd, &tio);

    /* raw mode */
    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;

    /* map integer baud to termios constant */
    speed_t speed;
    switch (baud_rate) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        default:
            fprintf(stderr, "Unsupported baud rate %d\n", baud_rate);
            close(fd); return -1;
    }
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 1; /* 100 ms read timeout */
    tcsetattr(fd, TCSANOW, &tio);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* ── Count differing bits between two bytes ─────────────────────── */
static inline int popcount_diff(uint8_t a, uint8_t b) {
    return __builtin_popcount(a ^ b);
}

/* ── 95% upper BER confidence (k errors in n bits) ──────────────── */
static double ber_upper_95(uint64_t errors, uint64_t bits) {
    if (bits == 0) return 1.0;
    /* Poisson approximation: chi2(0.95, 2*(k+1)) / (2*n)         */
    /* For k=0: upper ≈ 3/n; for k>0 use simple approximation.    */
    double k = (double)errors;
    double n = (double)bits;
    double chi2_approx = 2.0 * (k + 1.0) * (1.0 + 1.0 / (4.0 * (k + 1.0)));
    return chi2_approx / (2.0 * n);
}

/* ── Print live statistics ──────────────────────────────────────── */
static void print_stats(const bert_stats_t *s) {
    printf("\r[BERT] Bits: %12llu  Errors: %8llu  "
           "BER: %.2e  UB95: %.2e  FER: %.2e  "
           "Elapsed: %.1fs",
           (unsigned long long)s->bits_sent,
           (unsigned long long)s->bit_errors,
           s->ber,
           s->ber_confidence_upper,
           (s->bytes_checked > 0)
               ? (double)s->frame_errors / s->bytes_checked : 0.0,
           s->elapsed_seconds);
    fflush(stdout);
}

/* ── Main BERT loop ─────────────────────────────────────────────── */
int bert_run(const char *device, int baud, uint64_t target_bits) {
    int fd = uart_open(device, baud);
    if (fd < 0) return EXIT_FAILURE;

    prbs_ctx_t tx_prbs, rx_prbs;
    prbs9_init(&tx_prbs, 0x1FF);
    prbs9_init(&rx_prbs, 0x1FF);

    bert_stats_t stats = {0};

    const size_t CHUNK = 256;
    uint8_t tx_buf[CHUNK], rx_buf[CHUNK];

    struct timespec t_start, t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    printf("[BERT] Starting: %s @ %d baud, target %.2e bits\n",
           device, baud, (double)target_bits);

    while (stats.bits_sent < target_bits) {
        /* Generate and transmit a chunk of PRBS bytes */
        prbs_fill_buffer(&tx_prbs, tx_buf, CHUNK);
        ssize_t wr = write(fd, tx_buf, CHUNK);
        if (wr < 0) { perror("write"); break; }
        stats.bits_sent += (uint64_t)wr * 8;

        /* Read back the same number of bytes (loopback mode) */
        size_t rx_total = 0;
        while (rx_total < (size_t)wr) {
            ssize_t rd = read(fd, rx_buf + rx_total,
                              (size_t)wr - rx_total);
            if (rd > 0) {
                rx_total += (size_t)rd;
            } else if (rd == 0 || (rd < 0 && errno == EAGAIN)) {
                /* timeout — count missing bytes as all-zero errors */
                memset(rx_buf + rx_total, 0x00,
                       (size_t)wr - rx_total);
                stats.frame_errors += ((size_t)wr - rx_total);
                rx_total = (size_t)wr;
            } else {
                perror("read"); goto done;
            }
        }
        stats.bits_received += rx_total * 8;

        /* Compare received bytes against reference PRBS */
        uint8_t ref_buf[CHUNK];
        prbs_fill_buffer(&rx_prbs, ref_buf, (size_t)wr);
        for (size_t i = 0; i < (size_t)wr; i++) {
            stats.bit_errors += popcount_diff(rx_buf[i], ref_buf[i]);
        }
        stats.bytes_checked += (size_t)wr;

        /* Update statistics */
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        stats.elapsed_seconds = (t_now.tv_sec  - t_start.tv_sec)
                               + (t_now.tv_nsec - t_start.tv_nsec) * 1e-9;
        stats.ber = (stats.bits_sent > 0)
                  ? (double)stats.bit_errors / stats.bits_sent
                  : 0.0;
        stats.ber_confidence_upper =
            ber_upper_95(stats.bit_errors, stats.bits_sent);

        print_stats(&stats);
    }

done:
    printf("\n[BERT] Complete.\n");
    printf("  Total bits  : %llu\n", (unsigned long long)stats.bits_sent);
    printf("  Bit errors  : %llu\n", (unsigned long long)stats.bit_errors);
    printf("  Measured BER: %.4e\n", stats.ber);
    printf("  BER 95%% UB  : %.4e\n", stats.ber_confidence_upper);
    printf("  Frame errors: %llu\n", (unsigned long long)stats.frame_errors);
    printf("  Duration    : %.2f s\n", stats.elapsed_seconds);
    printf("  Throughput  : %.2f kbit/s\n",
           stats.bits_sent / stats.elapsed_seconds / 1000.0);

    close(fd);
    return (stats.ber < 1e-6) ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <device> <baud> <bits>\n", argv[0]);
        fprintf(stderr, "  e.g. %s /dev/ttyUSB0 115200 10000000\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *device  = argv[1];
    int         baud    = atoi(argv[2]);
    uint64_t    target  = (uint64_t)strtoull(argv[3], NULL, 10);
    return bert_run(device, baud, target);
}
```

---

### 3. C++ BERT Class with PRBS-23

```cpp
/**
 * @file Bert.hpp
 * @brief Object-oriented BERT engine for embedded or desktop use.
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <chrono>
#include <stdexcept>

// ─── PRBS-23 generator ───────────────────────────────────────────
class Prbs23 {
public:
    Prbs23() : state_(0x7FFFFF) {}  // non-zero seed

    uint8_t next_byte() {
        uint8_t byte = 0;
        for (int b = 7; b >= 0; --b) {
            byte |= static_cast<uint8_t>(next_bit() << b);
        }
        return byte;
    }

    void reset(uint32_t seed = 0x7FFFFF) {
        state_ = seed & 0x7FFFFF;
        if (state_ == 0) state_ = 1;
    }

private:
    uint32_t state_;

    uint8_t next_bit() {
        // Polynomial: x^23 + x^18 + 1
        uint8_t fb = static_cast<uint8_t>(
            ((state_ >> 22) ^ (state_ >> 17)) & 1u);
        state_ = ((state_ << 1) | fb) & 0x7FFFFFu;
        return fb;
    }
};

// ─── Statistics snapshot ─────────────────────────────────────────
struct BertStats {
    uint64_t bits_tx       = 0;
    uint64_t bits_rx       = 0;
    uint64_t bit_errors    = 0;
    uint64_t frame_errors  = 0;
    double   ber           = 0.0;
    double   elapsed_s     = 0.0;

    double frame_error_rate() const {
        uint64_t frames = bits_tx / 10; // 10 bits per UART frame
        return (frames > 0)
            ? static_cast<double>(frame_errors) / static_cast<double>(frames)
            : 0.0;
    }
};

// ─── BERT engine ─────────────────────────────────────────────────
class Bert {
public:
    using TxFn   = std::function<bool(const uint8_t*, size_t)>;
    using RxFn   = std::function<ssize_t(uint8_t*, size_t)>;
    using LogFn  = std::function<void(const BertStats&)>;

    /**
     * @param tx_fn   Callable that transmits bytes; returns true on success.
     * @param rx_fn   Callable that receives bytes; returns count or -1.
     * @param chunk   Bytes per iteration (tune for your UART FIFO size).
     */
    Bert(TxFn tx_fn, RxFn rx_fn, size_t chunk = 128)
        : tx_(tx_fn), rx_(rx_fn), chunk_(chunk) {}

    /**
     * @brief Run BERT for a fixed number of bits.
     * @param target_bits    Stop after transmitting this many bits.
     * @param log_interval   Call log_fn every N bytes (0 = never).
     * @param log_fn         Optional statistics callback.
     */
    BertStats run(uint64_t target_bits,
                  uint64_t log_interval = 100000,
                  LogFn    log_fn       = nullptr)
    {
        tx_prbs_.reset();
        rx_prbs_.reset();
        stats_ = BertStats{};

        auto t0 = std::chrono::steady_clock::now();
        std::vector<uint8_t> tx_buf(chunk_), rx_buf(chunk_), ref_buf(chunk_);

        while (stats_.bits_tx < target_bits) {
            // Fill TX buffer with PRBS bytes
            for (auto& b : tx_buf) b = tx_prbs_.next_byte();

            if (!tx_(tx_buf.data(), chunk_)) {
                throw std::runtime_error("TX failed");
            }
            stats_.bits_tx += chunk_ * 8;

            // Receive
            ssize_t rd = rx_(rx_buf.data(), chunk_);
            if (rd < 0) {
                stats_.frame_errors += chunk_;
                std::fill(rx_buf.begin(), rx_buf.end(), 0x00);
                rd = static_cast<ssize_t>(chunk_);
            }
            stats_.bits_rx += static_cast<uint64_t>(rd) * 8;

            // Compare against reference PRBS
            for (size_t i = 0; i < static_cast<size_t>(rd); ++i) {
                ref_buf[i] = rx_prbs_.next_byte();
                stats_.bit_errors +=
                    __builtin_popcount(rx_buf[i] ^ ref_buf[i]);
            }

            // Update derived stats
            auto now = std::chrono::steady_clock::now();
            stats_.elapsed_s =
                std::chrono::duration<double>(now - t0).count();
            stats_.ber = (stats_.bits_tx > 0)
                ? static_cast<double>(stats_.bit_errors)
                  / static_cast<double>(stats_.bits_tx)
                : 0.0;

            // Optional progress callback
            if (log_fn && log_interval > 0 &&
                (stats_.bits_tx / 8) % log_interval == 0)
            {
                log_fn(stats_);
            }
        }
        return stats_;
    }

private:
    TxFn       tx_;
    RxFn       rx_;
    size_t     chunk_;
    Prbs23     tx_prbs_, rx_prbs_;
    BertStats  stats_;

    std::vector<uint8_t> dummy_; // silence -Wunused
};
```

---

## Implementation in Rust

### 1. PRBS Generator in Rust

```rust
// prbs.rs — PRBS-9 and PRBS-23 generators

/// Generic LFSR-based PRBS generator.
///
/// # Example
/// ```rust
/// let mut gen = Prbs::new_prbs9(0x1FF);
/// let byte = gen.next_byte();
/// ```
pub struct Prbs {
    state:  u32,
    taps:   u32,
    mask:   u32,    // 2^n - 1
}

impl Prbs {
    /// PRBS-9 (polynomial x^9 + x^5 + 1)
    pub fn new_prbs9(seed: u32) -> Self {
        let state = if seed == 0 { 1 } else { seed & 0x1FF };
        Self { state, taps: (1 << 8) | (1 << 4), mask: 0x1FF }
    }

    /// PRBS-23 (polynomial x^23 + x^18 + 1)
    pub fn new_prbs23(seed: u32) -> Self {
        let state = if seed == 0 { 1 } else { seed & 0x7FFFFF };
        Self { state, taps: (1 << 22) | (1 << 17), mask: 0x7FFFFF }
    }

    /// Produce the next PRBS bit (0 or 1).
    #[inline(always)]
    pub fn next_bit(&mut self) -> u8 {
        let feedback = (self.state & self.taps).count_ones() as u8 & 1;
        self.state = ((self.state << 1) | feedback as u32) & self.mask;
        feedback
    }

    /// Pack 8 PRBS bits into one byte (MSB first).
    pub fn next_byte(&mut self) -> u8 {
        let mut byte = 0u8;
        for b in (0..8).rev() {
            byte |= self.next_bit() << b;
        }
        byte
    }

    /// Fill a buffer with PRBS bytes.
    pub fn fill(&mut self, buf: &mut [u8]) {
        for slot in buf.iter_mut() {
            *slot = self.next_byte();
        }
    }

    /// Reset to a new seed.
    pub fn reset(&mut self, seed: u32) {
        self.state = if seed == 0 { 1 } else { seed & self.mask };
    }
}
```

---

### 2. BERT Statistics and Confidence

```rust
// bert_stats.rs — Statistics engine for BERT

#[derive(Debug, Default, Clone)]
pub struct BertStats {
    pub bits_tx:       u64,
    pub bits_rx:       u64,
    pub bit_errors:    u64,
    pub frame_errors:  u64,
    pub elapsed_secs:  f64,
}

impl BertStats {
    /// Measured BER.
    pub fn ber(&self) -> f64 {
        if self.bits_tx == 0 { return 0.0; }
        self.bit_errors as f64 / self.bits_tx as f64
    }

    /// 95% upper confidence bound (Poisson approximation).
    pub fn ber_upper_95(&self) -> f64 {
        if self.bits_tx == 0 { return 1.0; }
        let k = self.bit_errors as f64;
        let n = self.bits_tx    as f64;
        // Chi-squared Poisson upper: chi2(0.95, 2*(k+1)) / (2*n)
        let chi2 = 2.0 * (k + 1.0) * (1.0 + 1.0 / (4.0 * (k + 1.0)));
        chi2 / (2.0 * n)
    }

    /// Frame error rate (assuming 10 bits/UART frame).
    pub fn fer(&self) -> f64 {
        let frames = self.bits_tx / 10;
        if frames == 0 { return 0.0; }
        self.frame_errors as f64 / frames as f64
    }

    /// Throughput in kbit/s.
    pub fn throughput_kbps(&self) -> f64 {
        if self.elapsed_secs == 0.0 { return 0.0; }
        self.bits_tx as f64 / self.elapsed_secs / 1_000.0
    }

    /// Minimum bits to achieve 95% confidence at a target BER.
    pub fn min_bits_for_confidence(target_ber: f64) -> u64 {
        (3.0 / target_ber).ceil() as u64
    }

    pub fn print_report(&self) {
        println!("─── BERT Report ───────────────────────────");
        println!("  Bits transmitted : {}", self.bits_tx);
        println!("  Bit errors       : {}", self.bit_errors);
        println!("  Frame errors     : {}", self.frame_errors);
        println!("  BER              : {:.4e}", self.ber());
        println!("  BER 95% upper    : {:.4e}", self.ber_upper_95());
        println!("  FER              : {:.4e}", self.fer());
        println!("  Elapsed          : {:.2} s", self.elapsed_secs);
        println!("  Throughput       : {:.2} kbit/s", self.throughput_kbps());
        println!("───────────────────────────────────────────");
    }
}
```

---

### 3. Full BERT Runner in Rust (serialport crate)

```rust
// bert_runner.rs — UART BERT using the `serialport` crate
//
// Cargo.toml:
//   [dependencies]
//   serialport = "4"
//   clap = { version = "4", features = ["derive"] }

use serialport::{SerialPort, SerialPortBuilder};
use std::io::{Read, Write};
use std::time::{Duration, Instant};
use crate::prbs::Prbs;
use crate::bert_stats::BertStats;

const CHUNK: usize = 128;

pub struct BertRunner {
    port:     Box<dyn SerialPort>,
    tx_prbs:  Prbs,
    rx_prbs:  Prbs,
}

impl BertRunner {
    /// Open a UART port for loopback BER testing.
    pub fn new(path: &str, baud: u32) -> Result<Self, serialport::Error> {
        let port = serialport::new(path, baud)
            .data_bits(serialport::DataBits::Eight)
            .parity(serialport::Parity::None)
            .stop_bits(serialport::StopBits::One)
            .flow_control(serialport::FlowControl::None)
            .timeout(Duration::from_millis(200))
            .open()?;

        Ok(Self {
            port,
            tx_prbs: Prbs::new_prbs23(0x7FFFFF),
            rx_prbs: Prbs::new_prbs23(0x7FFFFF),
        })
    }

    /// Run a complete BER test, returning accumulated statistics.
    pub fn run(&mut self, target_bits: u64) -> BertStats {
        self.tx_prbs.reset(0x7FFFFF);
        self.rx_prbs.reset(0x7FFFFF);
        let mut stats = BertStats::default();
        let t0 = Instant::now();

        let mut tx_buf = vec![0u8; CHUNK];
        let mut rx_buf = vec![0u8; CHUNK];
        let mut ref_buf = vec![0u8; CHUNK];

        while stats.bits_tx < target_bits {
            // ── Transmit PRBS chunk ──────────────────────────────
            self.tx_prbs.fill(&mut tx_buf);
            match self.port.write_all(&tx_buf) {
                Ok(())  => stats.bits_tx += (CHUNK * 8) as u64,
                Err(e)  => { eprintln!("TX error: {e}"); break; }
            }

            // ── Receive ──────────────────────────────────────────
            let rd = match self.port.read(&mut rx_buf) {
                Ok(n)   => n,
                Err(_)  => {
                    // Timeout: count missing bytes as errors
                    stats.frame_errors += CHUNK as u64;
                    rx_buf.fill(0x00);
                    CHUNK
                }
            };
            stats.bits_rx += (rd * 8) as u64;

            // ── Compare against reference PRBS ───────────────────
            self.rx_prbs.fill(&mut ref_buf[..rd]);
            for (rx_byte, ref_byte) in rx_buf[..rd].iter().zip(ref_buf[..rd].iter()) {
                let diff = rx_byte ^ ref_byte;
                stats.bit_errors += diff.count_ones() as u64;
            }

            stats.elapsed_secs = t0.elapsed().as_secs_f64();

            // Live progress every 100k bits
            if stats.bits_tx % 100_000 == 0 {
                print!(
                    "\r[BERT] bits={:>12}  errors={:>8}  BER={:.2e}  {:.1}s   ",
                    stats.bits_tx, stats.bit_errors, stats.ber(), stats.elapsed_secs
                );
                let _ = std::io::stdout().flush();
            }
        }

        println!(); // newline after progress line
        stats
    }
}

// ─── CLI entry point ──────────────────────────────────────────────
fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 4 {
        eprintln!("Usage: {} <port> <baud> <target_bits>", args[0]);
        eprintln!("  e.g. {} /dev/ttyUSB0 115200 10000000", args[0]);
        std::process::exit(1);
    }

    let path        = &args[1];
    let baud: u32   = args[2].parse().expect("Invalid baud rate");
    let target: u64 = args[3].parse().expect("Invalid bit count");

    println!("[BERT] Opening {} @ {} baud", path, baud);
    println!("[BERT] Min bits for BER<1e-9 (95% conf): {}",
             BertStats::min_bits_for_confidence(1e-9));

    let mut runner = BertRunner::new(path, baud)
        .expect("Failed to open UART");

    let stats = runner.run(target);
    stats.print_report();

    if stats.ber() < 1e-6 {
        println!("[BERT] PASS: BER below 1e-6 threshold.");
        std::process::exit(0);
    } else {
        println!("[BERT] FAIL: BER {:.2e} exceeds threshold.", stats.ber());
        std::process::exit(1);
    }
}
```

---

### 4. Async BERT with Tokio (Rust)

```rust
// async_bert.rs — Non-blocking BERT using Tokio + tokio-serial

use tokio_serial::SerialPortBuilderExt;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::time::{timeout, Duration};

pub async fn run_async_bert(
    path: &str,
    baud: u32,
    target_bits: u64,
) -> BertStats {
    let mut port = tokio_serial::new(path, baud)
        .open_native_async()
        .expect("Failed to open serial port");

    let mut tx_prbs = Prbs::new_prbs23(0x7FFFFF);
    let mut rx_prbs = Prbs::new_prbs23(0x7FFFFF);
    let mut stats   = BertStats::default();
    let t0 = tokio::time::Instant::now();

    let mut tx_buf  = vec![0u8; 128];
    let mut rx_buf  = vec![0u8; 128];
    let mut ref_buf = vec![0u8; 128];

    while stats.bits_tx < target_bits {
        tx_prbs.fill(&mut tx_buf);
        port.write_all(&tx_buf).await.expect("TX error");
        stats.bits_tx += (tx_buf.len() * 8) as u64;

        let rd = match timeout(
            Duration::from_millis(200),
            port.read(&mut rx_buf)
        ).await {
            Ok(Ok(n))  => n,
            _           => {
                stats.frame_errors += tx_buf.len() as u64;
                rx_buf.fill(0);
                tx_buf.len()
            }
        };

        rx_prbs.fill(&mut ref_buf[..rd]);
        for (r, e) in rx_buf[..rd].iter().zip(ref_buf[..rd].iter()) {
            stats.bit_errors += (r ^ e).count_ones() as u64;
        }

        stats.elapsed_secs = t0.elapsed().as_secs_f64();

        // Yield to the async runtime periodically
        tokio::task::yield_now().await;
    }

    stats
}
```

---

## Hardware-Assisted BERT

Many modern microcontrollers include hardware support that reduces CPU load during BERT:

### STM32 UART with DMA (C example)

```c
/**
 * @brief STM32 HAL BERT using DMA-driven loopback.
 *
 * Assumes CubeMX has configured:
 *   - USART1 with DMA TX (DMA2_Stream7) and RX (DMA2_Stream2)
 *   - Loopback: USART1 TX physically wired to USART1 RX
 */

#include "main.h"
#include "prbs_generator.h"    /* as defined above */

#define BERT_BUF_SIZE  512

static uint8_t     tx_buf[BERT_BUF_SIZE];
static uint8_t     rx_buf[BERT_BUF_SIZE];
static volatile int rx_done_flag = 0;

extern UART_HandleTypeDef huart1;

/* Called from HAL_UART_RxCpltCallback */
void UART_BERT_RxCallback(void) {
    rx_done_flag = 1;
}

uint64_t uart_dma_bert(uint32_t iterations) {
    prbs_ctx_t tx_prbs, rx_prbs;
    prbs9_init(&tx_prbs, 0x1FF);
    prbs9_init(&rx_prbs, 0x1FF);
    uint64_t bit_errors = 0;

    for (uint32_t i = 0; i < iterations; i++) {
        /* Generate PRBS pattern */
        prbs_fill_buffer(&tx_prbs, tx_buf, BERT_BUF_SIZE);

        /* Start async RX before TX to avoid race */
        rx_done_flag = 0;
        HAL_UART_Receive_DMA(&huart1, rx_buf, BERT_BUF_SIZE);
        HAL_UART_Transmit_DMA(&huart1, tx_buf, BERT_BUF_SIZE);

        /* Wait for RX completion (with timeout) */
        uint32_t deadline = HAL_GetTick() + 1000;
        while (!rx_done_flag && HAL_GetTick() < deadline) {
            __WFI(); /* sleep until interrupt */
        }

        /* Compare */
        uint8_t ref[BERT_BUF_SIZE];
        prbs_fill_buffer(&rx_prbs, ref, BERT_BUF_SIZE);
        for (int j = 0; j < BERT_BUF_SIZE; j++) {
            bit_errors += __builtin_popcount(rx_buf[j] ^ ref[j]);
        }
    }
    return bit_errors;
}
```

### FPGA / Dedicated BERT Chip Considerations

When the UART link is very fast (>= 1 Mbit/s) or when testing at BER levels below `1e-10`, a dedicated BERT chip (e.g., Spirent, Ixia, or FPGA-based) is preferred because:

- Hardware pattern generators run without OS scheduling jitter
- Error counting is done in synchronous logic at full line rate
- Built-in eye diagram and jitter analysis
- The STM32 UART peripheral itself supports an internal **loopback mode** (`USART_CR3_HDSEL` or debug loopback) which bypasses the physical pin but still validates the peripheral and driver layer

---

## Interpreting Results

### BER Thresholds — General Guidelines

| BER Range | Link Assessment |
|---|---|
| < 1 × 10⁻¹² | Excellent; suitable for safety-critical systems |
| < 1 × 10⁻⁹  | Good; acceptable for most industrial UART links |
| 1×10⁻⁹ – 1×10⁻⁶ | Marginal; investigate signal integrity |
| 1×10⁻⁶ – 1×10⁻³ | Poor; check cabling, termination, baud tolerance |
| > 1×10⁻³ | Failing; likely a hardware or configuration fault |

### Burst Errors vs. Random Errors

The pattern of errors reveals the failure mode:

- **Random, uniformly distributed errors** → thermal noise, marginal SNR
- **Burst errors (many consecutive errors)** → cable damage, EMI burst, power glitch
- **Errors correlated with specific bit patterns** → ISI (inter-symbol interference), inadequate driver current
- **Errors increasing monotonically with baud rate** → clock accuracy, baud rate mismatch

### Baud Rate Tolerance Check

UART allows up to ±2% baud rate mismatch before framing errors become frequent. A quick check:

```c
/* Verify: receiver samples at 50% ± 2% of bit period */
double tolerance_pct = fabs(actual_baud - nominal_baud) / nominal_baud * 100.0;
if (tolerance_pct > 2.0) {
    printf("WARNING: %.2f%% baud mismatch — likely cause of errors\n",
           tolerance_pct);
}
```

---

## Summary

Bit Error Rate Testing is the definitive method for quantifying UART link quality beyond simple functional pass/fail checks. Key takeaways:

**Conceptual foundations:** BER is the ratio of erroneous bits to transmitted bits. Statistical validity requires transmitting at least `3/BER_target` bits to establish 95% confidence. PRBS patterns (PRBS-9, PRBS-23, PRBS-31) generated by LFSRs provide deterministic yet noise-like stimulus.

**Implementation:** A complete BERT system requires a pattern generator, a reference copy on the receive side (or self-synchronising descrambler), a bit comparator, and a statistics engine. In loopback mode the same device serves as both transmitter and receiver. In through-mode, separate devices test the external physical link.

**C/C++ approach:** POSIX `termios`-based UART access pairs naturally with a software PRBS generator. The `Bert` C++ class abstracts the transmit/receive callbacks, enabling the same engine to drive different hardware backends (direct file descriptor, HAL, or simulation). DMA-driven implementations on STM32 offload the CPU and allow testing at higher baud rates.

**Rust approach:** The `serialport` crate provides cross-platform UART access. The PRBS generator benefits from Rust's safe integer arithmetic and the `count_ones()` popcount intrinsic. The async variant using `tokio-serial` allows concurrent BER testing of multiple ports or integration into a larger async application.

**Diagnostics:** BER results above `1e-6` warrant investigation into signal integrity, clock accuracy, cable quality, and termination. Burst error patterns indicate EMI or power issues; uniform random errors indicate noise floor issues. Baud rate mismatch above ±2% is a common root cause of elevated BER in embedded UART systems.

---

*Document version 1.0 — UART Topic 33 of the UART Programming Reference Series*