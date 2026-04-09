# 97. UART Bandwidth Utilization

**Theory** — Derives the theoretical maximum from first principles: frame structure (start/stop/parity bits), frame efficiency per configuration (8N1 = 80%, 8E1 = 72.7%, etc.), and the formula chain from baud rate down to payload bytes/sec.

**Overhead taxonomy** — Systematically breaks down every efficiency loss: frame bits, inter-character gaps, flow control (hardware CTS and software XON/XOFF), interrupt/DMA latency with concrete timing examples, protocol headers/checksums, half-duplex turnaround, and OS scheduling jitter.

**Worked calculations** — Shows the full formula `Effective = Baud × Frame_eff × Line_util × Proto_eff` with numeric examples for streaming vs. command/response scenarios, plus a reference table across common baud rates.

**C/C++ examples** include a reusable bandwidth calculator struct, a POSIX/Linux real-time measurement loop using `clock_gettime`, and a bare-metal STM32 HAL interrupt byte counter.

**Rust examples** include a complete bandwidth calculator with `const fn` compile-time budget assertions (useful for `no_std` embedded targets), and a `serialport`-crate based live measurement tool.

**Summary** — Gives the practical rule of thumb: 60–80% utilization for full-duplex streaming, 30–50% for half-duplex command/response, and lists the key optimisation levers (DMA, larger packets, fewer stop bits, dropping redundant parity).

## Calculating Effective Throughput vs. Theoretical Maximum

---

## Table of Contents

1. [Introduction](#introduction)
2. [Theoretical Maximum Bandwidth](#theoretical-maximum-bandwidth)
3. [Sources of Overhead and Efficiency Loss](#sources-of-overhead-and-efficiency-loss)
4. [Effective Throughput Calculation](#effective-throughput-calculation)
5. [Protocol-Level Overhead](#protocol-level-overhead)
6. [Software and System Overhead](#software-and-system-overhead)
7. [Code Examples in C/C++](#code-examples-in-cc)
8. [Code Examples in Rust](#code-examples-in-rust)
9. [Measuring Real-World Throughput](#measuring-real-world-throughput)
10. [Optimization Strategies](#optimization-strategies)
11. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) bandwidth utilization is the relationship between the data rate theoretically achievable at a given baud rate and the actual payload throughput delivered to an application. Understanding this gap is essential for embedded systems engineers when sizing communication links, diagnosing bottlenecks, and selecting appropriate protocols for time-sensitive applications.

A common misconception is that a 115,200 baud UART link delivers 115,200 bits per second of *useful data*. In reality, framing overhead, flow control pauses, interrupt latency, buffering delays, and application-layer protocols all reduce the effective throughput — often to 60–80% of the theoretical maximum, and sometimes far lower.

---

## Theoretical Maximum Bandwidth

### Baud Rate vs. Bit Rate

In UART, **baud rate** equals **bit rate** because each symbol carries exactly one bit. Thus:

```
Theoretical bit rate = Baud rate (bps)
```

### Frame Structure

Every UART character is wrapped in a frame:

```
[Start bit][D0][D1][D2][D3][D4][D5][D6][D7][Parity?][Stop bit(s)]
```

| Element         | Bits           |
|-----------------|----------------|
| Start bit       | 1 (always)     |
| Data bits       | 5, 6, 7, or 8  |
| Parity bit      | 0 or 1         |
| Stop bits       | 1, 1.5, or 2   |

**Most common configuration: 8N1** (8 data bits, no parity, 1 stop bit)

```
Total bits per character = 1 + 8 + 0 + 1 = 10 bits
```

### Theoretical Character Rate

```
Characters/sec = Baud rate / Bits per frame

Example (8N1 @ 115200 baud):
  Characters/sec = 115200 / 10 = 11,520 characters/sec
  Theoretical payload = 11,520 bytes/sec ≈ 11.25 KB/s
```

### Frame Efficiency

```
Frame efficiency = Data bits / Total frame bits

8N1:   8 / 10 = 80.0%
8E1:   8 / 11 = 72.7%
8N2:   8 / 11 = 72.7%
7E1:   7 / 10 = 70.0%
```

---

## Sources of Overhead and Efficiency Loss

### 1. Frame Overhead (Hardware Level)

The start and stop bits are mandatory framing, consuming 2 out of 10 bits in 8N1 — a constant 20% overhead regardless of software behavior.

### 2. Inter-character Gaps

Hardware does not guarantee back-to-back character transmission. Even a single idle bit between characters reduces throughput. At high baud rates, gaps introduced by slow transmit FIFOs or software polling compound quickly.

### 3. Flow Control

- **Hardware (RTS/CTS):** The sender pauses when the receiver deasserts CTS. Any pause consumes real time without transmitting data.
- **Software (XON/XOFF):** Control characters (0x11/0x13) are injected into the data stream, consuming payload bytes and adding latency when the transmitter must wait.

### 4. Interrupt and DMA Latency

On microcontrollers, each received byte may trigger an interrupt. ISR entry/exit overhead, interrupt priority contention, and context switch time mean the CPU may not service the UART for tens of microseconds — long enough to lose bytes at high baud rates without a hardware FIFO.

```
At 115200 baud, one character arrives every 86.8 µs.
An ISR with 5 µs latency leaves a 17-cycle window at 48 MHz — tight but acceptable.
At 921600 baud, one character arrives every 10.9 µs — a 5 µs ISR consumes 46% of available time.
```

### 5. Software Protocol Overhead

Application-layer framing (headers, checksums, length fields, delimiters) further reduces the ratio of application payload to transmitted bytes. A simple command/response protocol might add 8–20 bytes of overhead per transaction.

### 6. Half-Duplex Turnaround

RS-485 and similar half-duplex buses require direction-switching delays (DE/RE pin toggling, line settling time). Each turnaround may cost 1–5 character times of dead time.

### 7. CPU and OS Scheduling

On Linux/RTOS systems, application read/write calls go through kernel buffers (tty layer). Scheduling jitter, system calls, and ring buffer management add latency and can reduce throughput if the application does not keep up with the hardware.

---

## Effective Throughput Calculation

### General Formula

```
Effective throughput = Baud rate
                       × Frame efficiency
                       × Line utilization factor
                       × Protocol efficiency

Where:
  Frame efficiency      = Data bits / (1 + data bits + parity + stop bits)
  Line utilization      = 1 - (idle_time / total_time)
  Protocol efficiency   = Payload bytes / Total bytes transmitted
```

### Worked Example

Assume:
- 115200 baud, 8N1
- 5% inter-character gap (line utilization = 0.95)
- Application protocol: 4-byte header + 64-byte payload + 2-byte CRC per packet

```
Frame efficiency    = 8/10 = 0.80
Line utilization    = 0.95
Protocol efficiency = 64 / (4 + 64 + 2) = 64/70 = 0.914

Effective throughput = 115200 × 0.80 × 0.95 × 0.914
                     = 115200 × 0.694
                     ≈ 79,950 bps
                     ≈ 9,994 bytes/sec of application payload
```

This represents about **69.4%** of the raw baud rate.

### Quick Reference Table

| Baud Rate | Config | Theoretical (KB/s) | Typical Effective (KB/s) | Efficiency |
|-----------|--------|--------------------|--------------------------|------------|
| 9,600     | 8N1    | 0.96               | 0.72–0.84                | 75–87%     |
| 57,600    | 8N1    | 5.76               | 4.32–5.04                | 75–87%     |
| 115,200   | 8N1    | 11.52              | 7.68–9.98                | 67–87%     |
| 230,400   | 8N1    | 23.04              | 13.8–18.4                | 60–80%     |
| 921,600   | 8N1    | 92.16              | 46.1–73.7                | 50–80%     |

---

## Protocol-Level Overhead

### Request/Response Transactions

In a typical command/response protocol, only one side transmits at a time and must wait for a response before sending the next command:

```
Utilization = Payload / (Payload + Overhead + RTT_dead_time)

Example:
  Payload = 32 bytes → 32 × 10 / 115200 = 2.78 ms to transmit
  Overhead bytes = 8 → 8 × 10 / 115200 = 0.69 ms
  Round-trip latency = 5 ms (processing + response + gaps)

  Useful throughput = 32 bytes / (2.78 + 0.69 + 5) ms
                    = 32 / 8.47 ms
                    = 3778 bytes/sec  ←  only 32.8% of theoretical!
```

### Streaming Protocols

Streaming with large packets is far more efficient. Using 256-byte packets with 8 bytes overhead:

```
Protocol efficiency = 256 / (256 + 8) = 97.0%
Effective throughput ≈ 115200 × 0.80 × 0.97 ≈ 89,395 bps ≈ 10.93 KB/s
```

---

## Code Examples in C/C++

### Bandwidth Calculation Utility

```c
#include <stdio.h>
#include <stdint.h>

typedef struct {
    uint32_t baud_rate;
    uint8_t  data_bits;     // 5, 6, 7, or 8
    uint8_t  parity_bits;   // 0 = none, 1 = even/odd
    float    stop_bits;     // 1.0, 1.5, or 2.0
} uart_config_t;

typedef struct {
    uint32_t packet_payload_bytes;
    uint32_t packet_overhead_bytes;  // header + checksum + delimiters
    float    line_utilization;       // 0.0–1.0 (accounts for gaps, flow control)
    float    turnaround_ms;          // half-duplex direction switch time, ms
} link_params_t;

/** Return total bits per UART frame */
static float frame_bits(const uart_config_t *cfg)
{
    return 1.0f                   /* start bit */
         + cfg->data_bits
         + cfg->parity_bits
         + cfg->stop_bits;
}

/** Frame efficiency: payload bits / total frame bits */
static float frame_efficiency(const uart_config_t *cfg)
{
    return (float)cfg->data_bits / frame_bits(cfg);
}

/** Theoretical maximum payload throughput (bytes/sec) */
static float theoretical_throughput(const uart_config_t *cfg)
{
    float chars_per_sec = (float)cfg->baud_rate / frame_bits(cfg);
    return chars_per_sec;  /* each char = 1 byte (data_bits=8 assumed) */
}

/** Protocol efficiency for a given packet structure */
static float protocol_efficiency(const link_params_t *lp)
{
    uint32_t total = lp->packet_payload_bytes + lp->packet_overhead_bytes;
    if (total == 0) return 0.0f;
    return (float)lp->packet_payload_bytes / (float)total;
}

/** Effective payload throughput (bytes/sec) */
static float effective_throughput(const uart_config_t *cfg,
                                   const link_params_t *lp)
{
    float th   = theoretical_throughput(cfg);
    float fe   = frame_efficiency(cfg);
    float pe   = protocol_efficiency(lp);
    float util = lp->line_utilization;

    /* Account for half-duplex turnaround dead time */
    float packet_tx_time_ms = ((float)(lp->packet_payload_bytes +
                                       lp->packet_overhead_bytes)
                               * frame_bits(cfg) * 1000.0f)
                              / (float)cfg->baud_rate;

    float effective_util = util;
    if (lp->turnaround_ms > 0.0f) {
        float cycle_ms = packet_tx_time_ms + lp->turnaround_ms;
        effective_util *= packet_tx_time_ms / cycle_ms;
    }

    return th * fe * effective_util * pe;
}

/** Utilization percentage relative to raw baud rate */
static float utilization_percent(const uart_config_t *cfg,
                                  const link_params_t *lp)
{
    float eff = effective_throughput(cfg, lp);
    /* baud rate in bits/sec → bytes/sec conversion */
    float raw_bytes_sec = (float)cfg->baud_rate / 8.0f;
    return (eff / raw_bytes_sec) * 100.0f;
}

void print_bandwidth_report(const uart_config_t *cfg,
                             const link_params_t *lp)
{
    printf("=== UART Bandwidth Utilization Report ===\n");
    printf("Baud rate         : %u bps\n",  cfg->baud_rate);
    printf("Frame config      : %dN%.0f\n", cfg->data_bits, cfg->stop_bits);
    printf("Frame bits        : %.1f\n",    frame_bits(cfg));
    printf("Frame efficiency  : %.1f %%\n", frame_efficiency(cfg) * 100.0f);
    printf("Theoretical max   : %.1f bytes/sec (%.2f KB/s)\n",
           theoretical_throughput(cfg),
           theoretical_throughput(cfg) / 1024.0f);
    printf("Protocol eff.     : %.1f %%  (%u payload / %u total bytes)\n",
           protocol_efficiency(lp) * 100.0f,
           lp->packet_payload_bytes,
           lp->packet_payload_bytes + lp->packet_overhead_bytes);
    printf("Line utilization  : %.1f %%\n", lp->line_utilization * 100.0f);
    printf("Effective payload : %.1f bytes/sec (%.2f KB/s)\n",
           effective_throughput(cfg, lp),
           effective_throughput(cfg, lp) / 1024.0f);
    printf("Overall utiliz.   : %.1f %% of baud rate\n",
           utilization_percent(cfg, lp));
    printf("=========================================\n\n");
}

int main(void)
{
    /* Example 1: 115200 8N1, streaming large packets */
    uart_config_t cfg1 = { .baud_rate=115200, .data_bits=8,
                            .parity_bits=0, .stop_bits=1.0f };
    link_params_t lp1  = { .packet_payload_bytes=256,
                            .packet_overhead_bytes=8,
                            .line_utilization=0.95f,
                            .turnaround_ms=0.0f };
    print_bandwidth_report(&cfg1, &lp1);

    /* Example 2: 115200 8N1, small command/response packets */
    link_params_t lp2  = { .packet_payload_bytes=16,
                            .packet_overhead_bytes=8,
                            .line_utilization=0.90f,
                            .turnaround_ms=1.0f };
    print_bandwidth_report(&cfg1, &lp2);

    /* Example 3: 9600 8E1, legacy sensor with parity */
    uart_config_t cfg3 = { .baud_rate=9600, .data_bits=8,
                            .parity_bits=1, .stop_bits=1.0f };
    link_params_t lp3  = { .packet_payload_bytes=10,
                            .packet_overhead_bytes=4,
                            .line_utilization=0.80f,
                            .turnaround_ms=0.5f };
    print_bandwidth_report(&cfg3, &lp3);

    return 0;
}
```

### Real-Time Throughput Measurement (POSIX / Linux)

```c
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <errno.h>

#define MEASURE_DURATION_SEC  5
#define READ_BUFFER_SIZE      4096

typedef struct {
    uint64_t bytes_received;
    uint64_t bytes_transmitted;
    struct timespec start;
    struct timespec end;
} throughput_stats_t;

static inline uint64_t timespec_diff_us(const struct timespec *a,
                                         const struct timespec *b)
{
    return (uint64_t)((b->tv_sec - a->tv_sec) * 1000000LL
                    + (b->tv_nsec - a->tv_nsec) / 1000LL);
}

int open_uart(const char *device, int baud_flag)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    cfmakeraw(&tty);
    cfsetispeed(&tty, baud_flag);
    cfsetospeed(&tty, baud_flag);
    tty.c_cflag |= (CLOCAL | CREAD | CS8);  /* 8N1 */
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    return fd;
}

void measure_rx_throughput(int fd, throughput_stats_t *stats,
                            unsigned duration_sec)
{
    uint8_t buf[READ_BUFFER_SIZE];
    stats->bytes_received = 0;

    clock_gettime(CLOCK_MONOTONIC, &stats->start);

    struct timespec deadline = stats->start;
    deadline.tv_sec += duration_sec;

    while (1) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec &&
             now.tv_nsec >= deadline.tv_nsec)) {
            break;
        }

        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            stats->bytes_received += (uint64_t)n;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("read");
            break;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &stats->end);
}

void print_throughput_report(const throughput_stats_t *stats,
                              uint32_t baud_rate)
{
    uint64_t elapsed_us = timespec_diff_us(&stats->start, &stats->end);
    double elapsed_sec  = elapsed_us / 1e6;
    double rx_bps       = (stats->bytes_received * 8.0) / elapsed_sec;
    double rx_kbs       = stats->bytes_received / 1024.0 / elapsed_sec;

    /* Theoretical max for 8N1 */
    double theoretical_bytes_sec = (double)baud_rate / 10.0;
    double utilization = (rx_bps / baud_rate) * 100.0;

    printf("=== Measured Throughput ===\n");
    printf("Duration          : %.3f s\n",  elapsed_sec);
    printf("Bytes received    : %llu\n",    (unsigned long long)stats->bytes_received);
    printf("Rx throughput     : %.1f bps (%.2f KB/s)\n", rx_bps, rx_kbs);
    printf("Theoretical max   : %.1f bytes/sec\n", theoretical_bytes_sec);
    printf("Baud utilization  : %.1f %%\n", utilization);
    printf("===========================\n");
}

/*
 * Usage:
 *   int fd = open_uart("/dev/ttyUSB0", B115200);
 *   throughput_stats_t stats = {0};
 *   measure_rx_throughput(fd, &stats, MEASURE_DURATION_SEC);
 *   print_throughput_report(&stats, 115200);
 *   close(fd);
 */
```

### Embedded MCU: FIFO-Based Byte Counter (STM32 HAL)

```c
/* Interrupt-driven byte counter for throughput estimation on bare-metal MCU */
#include "stm32f4xx_hal.h"
#include <stdint.h>

extern UART_HandleTypeDef huart2;

static volatile uint32_t g_rx_byte_count = 0;
static volatile uint32_t g_tick_start    = 0;

/* Call once to start a measurement window */
void bw_measure_start(void)
{
    g_rx_byte_count = 0;
    g_tick_start    = HAL_GetTick();  /* millisecond tick */
    /* Enable RXNE interrupt */
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);
}

/* ISR: called by HAL_UART_IRQHandler when RXNE fires */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        g_rx_byte_count++;
    }
}

/* Call after the measurement window to get results */
void bw_measure_report(uint32_t baud_rate)
{
    uint32_t elapsed_ms  = HAL_GetTick() - g_tick_start;
    if (elapsed_ms == 0) return;

    uint32_t bytes        = g_rx_byte_count;
    uint32_t bytes_per_s  = (bytes * 1000U) / elapsed_ms;
    uint32_t bps          = bytes_per_s * 10U;  /* 8N1: 10 bits/byte */
    uint32_t utilization  = (bps * 100U) / baud_rate;

    printf("[BW] %lu bytes in %lu ms = %lu bytes/s (%lu%%)\r\n",
           bytes, elapsed_ms, bytes_per_s, utilization);
}
```

---

## Code Examples in Rust

### Bandwidth Calculator

```rust
/// UART bandwidth utilization calculator
#[derive(Debug, Clone)]
pub struct UartConfig {
    pub baud_rate:    u32,
    pub data_bits:    u8,   // 5, 6, 7, or 8
    pub parity_bits:  u8,   // 0 = none, 1 = even/odd
    pub stop_bits:    f32,  // 1.0, 1.5, or 2.0
}

#[derive(Debug, Clone)]
pub struct LinkParams {
    pub payload_bytes:    u32,
    pub overhead_bytes:   u32,  // header + checksum + delimiters
    pub line_utilization: f32,  // 0.0–1.0
    pub turnaround_ms:    f32,  // half-duplex switch time, 0.0 for full-duplex
}

#[derive(Debug)]
pub struct BandwidthReport {
    pub frame_bits:            f32,
    pub frame_efficiency_pct:  f32,
    pub theoretical_bytes_sec: f32,
    pub protocol_efficiency_pct: f32,
    pub effective_bytes_sec:   f32,
    pub baud_utilization_pct:  f32,
}

impl UartConfig {
    pub fn frame_bits(&self) -> f32 {
        1.0 + self.data_bits as f32 + self.parity_bits as f32 + self.stop_bits
    }

    pub fn frame_efficiency(&self) -> f32 {
        self.data_bits as f32 / self.frame_bits()
    }

    /// Maximum characters (bytes) per second
    pub fn theoretical_chars_per_sec(&self) -> f32 {
        self.baud_rate as f32 / self.frame_bits()
    }
}

impl LinkParams {
    pub fn total_bytes(&self) -> u32 {
        self.payload_bytes + self.overhead_bytes
    }

    pub fn protocol_efficiency(&self) -> f32 {
        if self.total_bytes() == 0 { return 0.0; }
        self.payload_bytes as f32 / self.total_bytes() as f32
    }
}

pub fn calculate_bandwidth(cfg: &UartConfig, lp: &LinkParams) -> BandwidthReport {
    let frame_eff  = cfg.frame_efficiency();
    let proto_eff  = lp.protocol_efficiency();
    let theoretical = cfg.theoretical_chars_per_sec();

    // Effective line utilisation accounting for half-duplex turnaround
    let packet_tx_ms = (lp.total_bytes() as f32 * cfg.frame_bits() * 1000.0)
                       / cfg.baud_rate as f32;
    let effective_util = if lp.turnaround_ms > 0.0 {
        let cycle_ms = packet_tx_ms + lp.turnaround_ms;
        lp.line_utilization * (packet_tx_ms / cycle_ms)
    } else {
        lp.line_utilization
    };

    let effective = theoretical * frame_eff * effective_util * proto_eff;

    // Utilisation relative to raw baud rate (bits → bytes: /8 not /10,
    // because we are comparing payload bytes to raw capacity in bytes)
    let baud_util = (effective / (cfg.baud_rate as f32 / 8.0)) * 100.0;

    BandwidthReport {
        frame_bits:              cfg.frame_bits(),
        frame_efficiency_pct:    frame_eff * 100.0,
        theoretical_bytes_sec:   theoretical,
        protocol_efficiency_pct: proto_eff * 100.0,
        effective_bytes_sec:     effective,
        baud_utilization_pct:    baud_util,
    }
}

pub fn print_report(cfg: &UartConfig, lp: &LinkParams) {
    let r = calculate_bandwidth(cfg, lp);
    println!("=== UART Bandwidth Report ===");
    println!("Baud rate          : {} bps", cfg.baud_rate);
    println!("Frame config       : {}{}{}",
             cfg.data_bits,
             if cfg.parity_bits == 0 { "N" } else { "E" },
             cfg.stop_bits as u8);
    println!("Frame bits         : {:.1}", r.frame_bits);
    println!("Frame efficiency   : {:.1}%", r.frame_efficiency_pct);
    println!("Theoretical max    : {:.1} bytes/sec  ({:.2} KB/s)",
             r.theoretical_bytes_sec, r.theoretical_bytes_sec / 1024.0);
    println!("Protocol eff.      : {:.1}%  ({}/{} bytes payload/total)",
             r.protocol_efficiency_pct, lp.payload_bytes, lp.total_bytes());
    println!("Line utilisation   : {:.1}%", lp.line_utilization * 100.0);
    println!("Effective payload  : {:.1} bytes/sec  ({:.2} KB/s)",
             r.effective_bytes_sec, r.effective_bytes_sec / 1024.0);
    println!("Baud utilisation   : {:.1}%", r.baud_utilization_pct);
    println!("=============================\n");
}

fn main() {
    // Example 1: 115200 8N1, streaming with large packets
    let cfg = UartConfig {
        baud_rate: 115_200,
        data_bits: 8,
        parity_bits: 0,
        stop_bits: 1.0,
    };
    let lp_stream = LinkParams {
        payload_bytes: 256,
        overhead_bytes: 8,
        line_utilization: 0.95,
        turnaround_ms: 0.0,
    };
    print_report(&cfg, &lp_stream);

    // Example 2: 115200 8N1, small command/response, half-duplex
    let lp_cmd = LinkParams {
        payload_bytes: 16,
        overhead_bytes: 8,
        line_utilization: 0.90,
        turnaround_ms: 1.0,
    };
    print_report(&cfg, &lp_cmd);

    // Example 3: 9600 baud with parity
    let cfg_slow = UartConfig {
        baud_rate: 9_600,
        data_bits: 8,
        parity_bits: 1,
        stop_bits: 1.0,
    };
    let lp_slow = LinkParams {
        payload_bytes: 10,
        overhead_bytes: 4,
        line_utilization: 0.80,
        turnaround_ms: 0.5,
    };
    print_report(&cfg_slow, &lp_slow);
}
```

### Real-Time Throughput Measurement in Rust (Linux / serialport crate)

```rust
use serialport::{SerialPort, SerialPortSettings, DataBits, StopBits, Parity, FlowControl};
use std::time::{Duration, Instant};
use std::io::Read;

pub struct ThroughputMeasurement {
    pub bytes_received:    u64,
    pub elapsed:           Duration,
    pub bytes_per_sec:     f64,
    pub bits_per_sec:      f64,
    pub baud_utilization:  f64,   // fraction 0.0–1.0
}

impl ThroughputMeasurement {
    pub fn print(&self, baud_rate: u32) {
        println!("--- Throughput Measurement ---");
        println!("Elapsed       : {:.3} s",  self.elapsed.as_secs_f64());
        println!("Bytes rx      : {}",       self.bytes_received);
        println!("Throughput    : {:.1} bytes/s  ({:.2} KB/s)",
                 self.bytes_per_sec, self.bytes_per_sec / 1024.0);
        println!("Baud rate     : {} bps",   baud_rate);
        println!("Utilisation   : {:.1}%",   self.baud_utilization * 100.0);
        println!("------------------------------\n");
    }
}

pub fn measure_rx_throughput(
    port: &mut dyn SerialPort,
    duration: Duration,
    baud_rate: u32,
) -> ThroughputMeasurement {
    let mut buf = vec![0u8; 4096];
    let mut total_bytes: u64 = 0;
    let start = Instant::now();

    // Set read timeout to 10 ms so the loop remains responsive
    port.set_timeout(Duration::from_millis(10))
        .expect("set_timeout");

    while start.elapsed() < duration {
        match port.read(&mut buf) {
            Ok(n) if n > 0 => total_bytes += n as u64,
            Ok(_)          => {}   // timeout / no data
            Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {}
            Err(e) => {
                eprintln!("Read error: {e}");
                break;
            }
        }
    }

    let elapsed       = start.elapsed();
    let elapsed_secs  = elapsed.as_secs_f64();
    let bytes_per_sec = total_bytes as f64 / elapsed_secs;
    let bits_per_sec  = bytes_per_sec * 10.0;   // 8N1: 10 bits per byte
    let utilization   = bits_per_sec / baud_rate as f64;

    ThroughputMeasurement {
        bytes_received: total_bytes,
        elapsed,
        bytes_per_sec,
        bits_per_sec,
        baud_utilization: utilization,
    }
}

pub fn open_uart(
    device: &str,
    baud_rate: u32,
) -> Result<Box<dyn SerialPort>, serialport::Error> {
    let settings = SerialPortSettings {
        baud_rate,
        data_bits: DataBits::Eight,
        stop_bits: StopBits::One,
        parity:    Parity::None,
        flow_control: FlowControl::None,
        timeout:   Duration::from_millis(100),
    };
    serialport::open_with_settings(device, &settings)
}

fn main() {
    let baud_rate = 115_200u32;
    let device    = "/dev/ttyUSB0";

    match open_uart(device, baud_rate) {
        Ok(mut port) => {
            println!("Measuring throughput on {} for 5 seconds...", device);
            let result = measure_rx_throughput(
                port.as_mut(),
                Duration::from_secs(5),
                baud_rate,
            );
            result.print(baud_rate);
        }
        Err(e) => eprintln!("Could not open {device}: {e}"),
    }
}
```

### Compile-Time Constant Bandwidth Assertions (Rust)

```rust
/// Compile-time checks to ensure a given protocol fits within baud rate budget.
/// Useful in const contexts (embedded no_std targets).

const BAUD_RATE: u32       = 115_200;
const FRAME_BITS: u32      = 10;          // 8N1
const CHARS_PER_SEC: u32   = BAUD_RATE / FRAME_BITS;  // = 11,520

const PACKET_PAYLOAD: u32  = 64;
const PACKET_OVERHEAD: u32 = 8;
const PACKET_TOTAL: u32    = PACKET_PAYLOAD + PACKET_OVERHEAD;

/// Minimum required packet rate for an application needing at least
/// `min_payload_bytes_sec` bytes/sec of payload.
const fn min_packets_per_sec(min_payload_bytes_sec: u32) -> u32 {
    // ceiling division
    (min_payload_bytes_sec + PACKET_PAYLOAD - 1) / PACKET_PAYLOAD
}

/// Maximum achievable packet rate given UART speed.
const MAX_PACKETS_PER_SEC: u32 = CHARS_PER_SEC / PACKET_TOTAL;

// Compile-time assertion: ensure 1 KB/s application requirement is achievable
const _: () = assert!(
    MAX_PACKETS_PER_SEC >= min_packets_per_sec(1024),
    "UART baud rate insufficient for required application throughput"
);

fn main() {
    println!("Max packet rate  : {} packets/sec", MAX_PACKETS_PER_SEC);
    println!("Max payload rate : {} bytes/sec",
             MAX_PACKETS_PER_SEC * PACKET_PAYLOAD);
    println!("For 1 KB/s min   : {} packets/sec needed",
             min_packets_per_sec(1024));
}
```

---

## Measuring Real-World Throughput

### Test Pattern Method

The sender transmits a known test pattern (e.g., incrementing bytes or constant 0x55/0xAA) at maximum rate. The receiver counts received bytes over a fixed window and computes throughput. This isolates hardware/driver throughput from application-layer overhead.

### Timestamp Injection

For protocol-level analysis, inject timestamps into the data stream. By correlating send and receive timestamps, round-trip latency and per-transaction efficiency can be measured independently.

### Logic Analyzer / Oscilloscope

A hardware logic analyzer captures the exact bit-level timing. Measuring idle gaps between characters directly reveals line utilization and allows calculation of actual versus theoretical character rate.

### Key Metrics to Monitor

| Metric                      | What It Reveals                                      |
|-----------------------------|------------------------------------------------------|
| Rx bytes / elapsed time     | Actual payload throughput                            |
| Frame error rate (FE/OE)    | Baud rate mismatch, noise, or FIFO overrun           |
| Overrun errors              | CPU/DMA not keeping up; increase buffer or use DMA   |
| TX FIFO stall time          | Software not feeding transmitter fast enough         |
| Interrupt latency histogram | Whether ISR jitter risks byte loss                   |

---

## Optimization Strategies

### 1. Use DMA for Large Transfers

DMA eliminates per-byte CPU overhead, allowing the processor to prepare the next buffer while the previous one transmits. Throughput approaches the theoretical frame efficiency limit.

### 2. Increase Packet Size

Larger packets amortize per-packet protocol overhead across more payload bytes. A 256-byte packet with 8 bytes of overhead achieves 97% protocol efficiency vs. 67% for a 16-byte packet with the same overhead.

### 3. Choose the Right Baud Rate

Select the highest baud rate that the link (cable length, capacitance, driver strength) and both endpoints can reliably sustain. Marginal links at high baud rates introduce framing errors, requiring retransmission that destroys effective throughput.

### 4. Minimize Stop Bits

Using 1 stop bit instead of 2 increases frame efficiency from 72.7% to 80% (8N1 vs. 8N2). Modern receivers lock on quickly and rarely need 2 stop bits.

### 5. Eliminate Parity If Error Checking Exists at a Higher Layer

Parity adds 1 bit per frame (reducing efficiency from 80% to 72.7%) but only detects single-bit errors and cannot correct them. If the application layer uses CRC or checksums, parity is redundant overhead.

### 6. Use Hardware Flow Control Sparingly

CTS-gated pauses can devastate throughput if the receiver is slow. Profile actual RTS/CTS assertion patterns and ensure receive buffers are large enough to keep CTS asserted during normal operation.

### 7. Batch Small Writes

On Linux/POSIX systems, each `write()` syscall adds overhead. Batch small messages into larger writes using a staging buffer. This is especially important at high baud rates where the kernel's tty layer can become the bottleneck.

---

## Summary

UART bandwidth utilization is determined by multiple compounding factors that each reduce effective payload throughput below the raw baud rate:

**Frame overhead** is the irreducible baseline — 8N1 wastes 20% of all transmitted bits on start/stop framing. Parity and extra stop bits worsen this further.

**Protocol overhead** from headers, checksums, and delimiters reduces the fraction of transmitted bytes that carry application data. Small packets with fixed overhead are particularly inefficient; large packets approach near-100% protocol efficiency.

**Line utilization** captures idle time caused by inter-character gaps, flow control pauses, and half-duplex direction switching. In practice, a well-tuned full-duplex streaming link may achieve 90–95% line utilization, while a half-duplex command/response system can fall below 50%.

**Software overhead** — interrupt latency, DMA setup, OS scheduling, and application processing — limits how quickly the CPU can feed the transmitter or drain the receiver. At baud rates above ~230,400 bps, software-driven UART often becomes the bottleneck rather than the hardware.

The overall effective throughput is the product of all these factors:

```
Effective throughput ≈ Baud rate × 0.80 (8N1) × 0.90 (line util) × 0.90 (protocol eff)
                     ≈ 0.648 × Baud rate
```

A realistic rule of thumb for a well-implemented 8N1 full-duplex streaming link is **60–80% of baud rate** as useful payload throughput. Command/response half-duplex systems may achieve only **30–50%**. Measuring actual throughput with the techniques shown above, and iteratively optimizing packet structure, DMA usage, and baud rate selection, allows engineers to approach the theoretical frame efficiency ceiling for their application.

---

*Document: 97_Bandwidth_Utilization.md | UART Series*