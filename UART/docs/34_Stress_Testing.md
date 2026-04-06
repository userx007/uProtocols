# 34. UART Stress Testing


| # | Test | What It Finds |
|---|---|---|
| 1 | **Max Throughput** | Byte loss under sustained TX burst |
| 2 | **Buffer Overrun** | FIFO overflow, silent byte drop |
| 3 | **Error Injection** | Parity/framing error detection & recovery |
| 4 | **Baud Boundary** | ±3% tolerance margin validation |
| 5 | **24h Soak** | Clock drift, memory leaks, descriptor exhaustion |
| 6 | **ISR Latency** | Worst-case interrupt response under load |
| 7 | **Multi-Port Concurrent** | DMA arbitration, per-port BER |
| 8 | **Edge Frame Sizes** | Parser behaviour at 0, 1, 255, 4096-byte payloads |
| 9 | **PRBS Loopback** | Full bit-pattern coverage, stuck-at faults |
| 10 | **Clock Instability** | Baud accuracy across PLL configurations |

The **Rust implementations** leverage `Arc<AtomicU64>` for lockless counters and the `serialport` crate for cross-platform access — the compiler enforces thread-safety that C code must achieve manually. The **C/C++ examples** cover both Linux POSIX `termios` (for host-side testing) and bare-metal STM32 HAL patterns (for embedded targets).

## High-Load Scenarios and Edge Case Validation

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Stress Test UART?](#why-stress-test-uart)
3. [Categories of Stress Tests](#categories-of-stress-tests)
4. [Maximum Throughput Testing](#1-maximum-throughput-testing)
5. [Buffer Overflow and Overrun Testing](#2-buffer-overflow-and-overrun-testing)
6. [Noise and Line Error Injection](#3-noise-and-line-error-injection)
7. [Baud Rate Boundary Testing](#4-baud-rate-boundary-testing)
8. [Long-Duration Soak Testing](#5-long-duration-soak-testing)
9. [Interrupt Latency Under Load](#6-interrupt-latency-under-load)
10. [Multi-Port Concurrent Stress](#7-multi-port-concurrent-stress)
11. [Edge Case: Zero-Length and Maximum-Length Frames](#8-edge-case-zero-length-and-maximum-length-frames)
12. [Loopback Integrity Testing](#9-loopback-integrity-testing)
13. [Power and Clock Instability Simulation](#10-power-and-clock-instability-simulation)
14. [Rust Implementation](#rust-implementation)
15. [Test Metrics and Reporting](#test-metrics-and-reporting)
16. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) is one of the oldest and most pervasive
serial communication interfaces in embedded systems. While it is conceptually simple — data
bits, a start bit, optional parity, and stop bits — real-world deployments face demanding
conditions: sustained high data rates, noisy industrial environments, interrupt-saturated
MCU cores, and corner-case frame formats that exercise the peripheral in unexpected ways.

**Stress testing** goes beyond functional verification. It deliberately pushes the UART
hardware and software stack to — and past — their stated limits to discover:

- Latent bugs in interrupt service routines (ISRs)
- Race conditions in ring buffers shared between ISRs and application code
- Hardware overrun errors that silently drop bytes
- Parity and framing errors that propagate unchecked
- Memory leaks in dynamic DMA descriptor pools
- Timing sensitivities near baud-rate tolerances

This document describes systematic stress-testing strategies, provides reference
implementations in **C/C++** and **Rust**, and discusses the metrics you should capture
to characterise your UART subsystem under load.

---

## Why Stress Test UART?

| Risk Category | Consequence if Untested |
|---|---|
| Overrun (RX FIFO full) | Silent byte loss, corrupted protocol frames |
| Framing error propagation | Receiver stuck waiting for valid stop bit |
| ISR latency spikes | Bytes lost when MCU services higher-priority IRQs |
| DMA descriptor exhaustion | Transmit stalls; system hangs |
| Clock drift accumulation | Increasing BER over long transmissions |
| Buffer aliasing bugs | Data corruption in ring-buffer head/tail wrap |

A UART that works at 10 % load can fail catastrophically at 90 % — stress tests find
these faults before customers do.

---

## Categories of Stress Tests

```
┌─────────────────────────────────────────────────────────────────┐
│                    UART STRESS TEST TAXONOMY                    │
├───────────────────┬─────────────────────────────────────────────┤
│  THROUGHPUT       │  Max baud, continuous TX, burst TX          │
│  OVERRUN          │  RX faster than service rate, FIFO fill     │
│  ERROR INJECTION  │  Parity flip, framing break, noise          │
│  BOUNDARY         │  Min/max frame size, baud edges, 7/8/9 bit  │
│  LONGEVITY        │  Soak (hours/days), byte-count integrity    │
│  CONCURRENCY      │  Multi-port, DMA + CPU mix, IRQ contention  │
│  LATENCY          │  ISR response time under competing load     │
└───────────────────┴─────────────────────────────────────────────┘
```

---

## 1. Maximum Throughput Testing

The goal is to determine the **sustainable TX and RX rates** before byte loss occurs.
The transmitter sends data at full speed; the receiver measures the fraction of bytes
correctly received.

### C/C++ Implementation

```cpp
// uart_stress_throughput.c / .cpp
// Target: Linux host using POSIX termios, or adapt for bare-metal CMSIS HAL.
// Build: gcc -O2 -pthread uart_stress_throughput.c -o stress_throughput

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

// ── Configuration ─────────────────────────────────────────────────────────────
#define BAUD_RATE       B921600
#define TX_CHUNK_SIZE   4096          // bytes per write() call
#define TEST_DURATION_S 30            // seconds to run the soak
#define PATTERN_BYTE    0xA5          // known fill pattern
#define PORT_TX         "/dev/ttyUSB0"
#define PORT_RX         "/dev/ttyUSB1"

// ── Shared state ──────────────────────────────────────────────────────────────
static volatile sig_atomic_t g_stop = 0;
static uint64_t g_tx_bytes = 0;
static uint64_t g_rx_bytes = 0;
static uint64_t g_rx_errors = 0;   // pattern mismatches

static void handle_sigint(int sig) { (void)sig; g_stop = 1; }

// ── Open and configure a serial port ─────────────────────────────────────────
static int open_serial(const char *path, bool read_only)
{
    int flags = read_only ? O_RDONLY : O_WRONLY;
    int fd = open(path, flags | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); exit(EXIT_FAILURE); }

    struct termios tty;
    memset(&tty, 0, sizeof tty);
    cfsetispeed(&tty, BAUD_RATE);
    cfsetospeed(&tty, BAUD_RATE);

    // 8N1, no flow control, raw mode
    tty.c_cflag  = CS8 | CREAD | CLOCAL;
    tty.c_iflag  = 0;   // disable all input processing
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) { perror("tcsetattr"); exit(1); }
    tcflush(fd, TCIOFLUSH);

    // Restore blocking mode for the test
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    return fd;
}

// ── Transmit thread ───────────────────────────────────────────────────────────
static void *tx_thread(void *arg)
{
    int fd = *(int *)arg;
    uint8_t buf[TX_CHUNK_SIZE];
    memset(buf, PATTERN_BYTE, sizeof buf);

    while (!g_stop) {
        ssize_t n = write(fd, buf, sizeof buf);
        if (n > 0) g_tx_bytes += (uint64_t)n;
        else if (n < 0 && errno != EAGAIN) { perror("write"); break; }
    }
    return NULL;
}

// ── Receive thread ────────────────────────────────────────────────────────────
static void *rx_thread(void *arg)
{
    int fd = *(int *)arg;
    uint8_t buf[TX_CHUNK_SIZE];

    while (!g_stop) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0) {
            g_rx_bytes += (uint64_t)n;
            // Validate pattern
            for (ssize_t i = 0; i < n; i++) {
                if (buf[i] != PATTERN_BYTE) g_rx_errors++;
            }
        } else if (n < 0 && errno != EAGAIN) { perror("read"); break; }
    }
    return NULL;
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(void)
{
    signal(SIGINT, handle_sigint);

    int fd_tx = open_serial(PORT_TX, false);
    int fd_rx = open_serial(PORT_RX, true);

    pthread_t ttx, trx;
    pthread_create(&ttx, NULL, tx_thread, &fd_tx);
    pthread_create(&trx, NULL, rx_thread, &fd_rx);

    // Timed soak
    for (int s = 0; s < TEST_DURATION_S && !g_stop; s++) {
        sleep(1);
        printf("[%3ds] TX: %8lu kB  RX: %8lu kB  Errors: %lu\n",
               s + 1,
               (unsigned long)(g_tx_bytes / 1024),
               (unsigned long)(g_rx_bytes / 1024),
               (unsigned long)g_rx_errors);
    }
    g_stop = 1;

    pthread_join(ttx, NULL);
    pthread_join(trx, NULL);
    close(fd_tx);
    close(fd_rx);

    double loss = (g_tx_bytes > 0)
        ? 100.0 * (double)(g_tx_bytes - g_rx_bytes) / (double)g_tx_bytes
        : 0.0;

    printf("\n=== Throughput Stress Test Result ===\n");
    printf("  Total TX : %lu bytes\n", (unsigned long)g_tx_bytes);
    printf("  Total RX : %lu bytes\n", (unsigned long)g_rx_bytes);
    printf("  Loss     : %.4f %%\n",   loss);
    printf("  Errors   : %lu byte mismatches\n", (unsigned long)g_rx_errors);
    return (g_rx_errors == 0 && loss < 0.001) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

---

## 2. Buffer Overflow and Overrun Testing

**Hardware overrun** occurs when the RX shift register completes a frame but the
receive FIFO is still full from a previous byte that software has not read yet.
This test deliberately creates that condition by throttling the read side.

### C/C++ Implementation

```cpp
// uart_stress_overrun.c
// Deliberately delays the consumer to provoke hardware FIFO overrun.
// Requires a loopback cable or second UART feeding data.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/serial.h>   // struct serial_icounter_struct

// Open helper (same as above, omitted for brevity — paste from Section 1)
extern int open_serial(const char *path, bool read_only);

// Read the hardware overrun counter from the Linux serial driver
static int get_overrun_count(int fd)
{
    struct serial_icounter_struct ic;
    if (ioctl(fd, TIOCGICOUNT, &ic) < 0) { perror("TIOCGICOUNT"); return -1; }
    return ic.overrun;
}

int main(void)
{
    int fd = open_serial("/dev/ttyUSB0", true);  // RX-only

    int overrun_before = get_overrun_count(fd);
    printf("Overrun count before: %d\n", overrun_before);

    uint8_t buf[1];
    uint64_t bytes = 0;

    // Read only ONE byte every 5 ms — far too slow for 921600 baud.
    // At 921600 baud, ~115 KB/s arrive; we consume ~200 B/s → FIFO fills fast.
    printf("Throttled read for 5 seconds (expect overruns)...\n");
    for (int ms = 0; ms < 5000; ms++) {
        read(fd, buf, 1);
        bytes++;
        usleep(1000);  // 1 ms pause
    }

    int overrun_after = get_overrun_count(fd);
    printf("Overrun count after : %d\n", overrun_after);
    printf("Bytes consumed      : %lu\n", (unsigned long)bytes);
    printf("Hardware overruns   : %d\n", overrun_after - overrun_before);

    close(fd);
    return 0;
}
```

### Bare-Metal Overrun Detection (STM32 HAL, C)

```c
// STM32 HAL example — overrun error detection in UART ISR
// Place in stm32xx_it.c or equivalent

#include "stm32f4xx_hal.h"

extern UART_HandleTypeDef huart1;

// Ring buffer (must be power-of-two size for lockless masking)
#define RX_BUF_SIZE 256u
static uint8_t  rx_buf[RX_BUF_SIZE];
static uint32_t rx_head = 0;   // written by ISR
static uint32_t rx_tail = 0;   // read by application

static uint32_t overrun_count = 0;
static uint32_t framing_err_count = 0;
static uint32_t parity_err_count  = 0;

// Called by HAL_UART_IRQHandler when a byte is received
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;

    // Check error flags BEFORE reading DR (clears flags on some MCUs)
    uint32_t sr = huart->Instance->SR;

    if (sr & USART_SR_ORE) { overrun_count++;     }
    if (sr & USART_SR_FE)  { framing_err_count++; }
    if (sr & USART_SR_PE)  { parity_err_count++;  }

    uint32_t next = (rx_head + 1u) & (RX_BUF_SIZE - 1u);
    if (next != rx_tail) {
        rx_buf[rx_head] = (uint8_t)(huart->Instance->DR & 0xFFu);
        rx_head = next;
    } else {
        // Software ring buffer full — also an overrun at SW level
        (void)huart->Instance->DR;  // discard to clear flag
        overrun_count++;
    }

    // Re-arm single-byte receive
    HAL_UART_Receive_IT(huart, &rx_buf[rx_head], 1u);
}

// Application-level error report
void uart_stress_report(void)
{
    printf("ORE: %lu  FE: %lu  PE: %lu\n",
           (unsigned long)overrun_count,
           (unsigned long)framing_err_count,
           (unsigned long)parity_err_count);
}
```

---

## 3. Noise and Line Error Injection

Framing and parity errors are most reliably injected by:

- **Software**: configuring sender with a different parity mode than receiver
- **Hardware**: bit-banging a break condition, or using a line glitcher
- **Loopback + manipulation**: send known data, flip bits in the OS before re-injection

### C/C++ Parity Mismatch Injection

```cpp
// uart_stress_error_inject.cpp
// Sender configured as 8E1; receiver configured as 8N1.
// Every received byte will trigger a parity error at the UART hardware.

#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <linux/serial.h>
#include <sys/ioctl.h>

static int open_uart_parity(const char *dev, speed_t baud, bool even_parity)
{
    int fd = open(dev, O_RDWR | O_NOCTTY);
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetspeed(&tty, baud);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    if (even_parity) tty.c_cflag |= PARENB;   // enable even parity
    tty.c_iflag = INPCK | PARMRK;             // mark parity errors with 0xFF 0x00 prefix
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int main(void)
{
    // TX with even parity, RX without — every byte causes parity error on RX
    int tx = open_uart_parity("/dev/ttyUSB0", B115200, true);
    int rx = open_uart_parity("/dev/ttyUSB1", B115200, false);

    const char msg[] = "STRESS_TEST_PARITY_MISMATCH";
    write(tx, msg, sizeof msg - 1);

    uint8_t buf[64];
    ssize_t n = read(rx, buf, sizeof buf);
    printf("Received %zd bytes (expect 0xFF 0x00 parity-error markers):\n", n);
    for (ssize_t i = 0; i < n; i++) printf("%02X ", buf[i]);
    printf("\n");

    // Check for PARMRK sequences: 0xFF 0x00 <byte> = parity/framing error marker
    int error_count = 0;
    for (ssize_t i = 0; i + 2 < n; i++) {
        if (buf[i] == 0xFF && buf[i+1] == 0x00) {
            printf("  → Parity/framing error on byte 0x%02X at pos %zd\n",
                   buf[i+2], i+2);
            error_count++;
            i += 2;
        }
    }
    printf("Total parity errors detected: %d\n", error_count);

    close(tx); close(rx);
    return 0;
}
```

---

## 4. Baud Rate Boundary Testing

The UART spec allows ±2 % baud rate tolerance. Testing near these boundaries
reveals marginal clock configurations on the target MCU.

### C/C++ Baud Rate Sweep

```cpp
// uart_stress_baud_sweep.cpp
// Sweeps baud rates from -3% to +3% of nominal and measures byte error rate.

#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

// Set a *custom* (non-standard) baud rate using Linux ASYNC_SPD_CUST
static int set_custom_baud(int fd, int baud)
{
    struct serial_struct ss;
    ioctl(fd, TIOCGSERIAL, &ss);
    ss.flags = (ss.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
    ss.custom_divisor = ss.baud_base / baud;
    if (ss.custom_divisor == 0) ss.custom_divisor = 1;
    return ioctl(fd, TIOCSSERIAL, &ss);
}

static void configure_raw(int fd)
{
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetspeed(&tty, B38400);          // placeholder — custom baud overrides
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = tty.c_oflag = tty.c_lflag = 0;
    tty.c_cc[VMIN] = 1;
    tcsetattr(fd, TCSANOW, &tty);
}

int main(void)
{
    int nominal = 115200;
    int fd_tx   = open("/dev/ttyUSB0", O_WRONLY | O_NOCTTY);
    int fd_rx   = open("/dev/ttyUSB1", O_RDONLY | O_NOCTTY);
    configure_raw(fd_tx);
    configure_raw(fd_rx);

    // Offsets: -3%, -2%, -1%, 0%, +1%, +2%, +3%
    int offsets[] = { -3, -2, -1, 0, 1, 2, 3 };

    const uint8_t payload[16] = {
        0xDE,0xAD,0xBE,0xEF,0xCA,0xFE,0xBA,0xBE,
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF
    };

    for (int i = 0; i < 7; i++) {
        int actual_baud = nominal + (nominal * offsets[i] / 100);
        set_custom_baud(fd_tx, actual_baud);
        set_custom_baud(fd_rx, nominal);    // RX always at nominal

        printf("TX baud: %d (%+d%%)  → ", actual_baud, offsets[i]);
        fflush(stdout);

        write(fd_tx, payload, sizeof payload);
        usleep(20000);  // settle

        uint8_t rbuf[sizeof payload];
        ssize_t n = read(fd_rx, rbuf, sizeof rbuf);
        int errs = 0;
        for (ssize_t b = 0; b < n && b < (ssize_t)sizeof payload; b++)
            if (rbuf[b] != payload[b]) errs++;

        printf("RX %zd bytes, %d errors\n", n, errs);
        usleep(50000);
    }

    close(fd_tx); close(fd_rx);
    return 0;
}
```

---

## 5. Long-Duration Soak Testing

A 24-hour (or multi-day) soak test with an incrementing counter payload detects:

- Accumulating clock drift causing late framing errors
- Memory leaks in driver descriptor pools
- Thermal effects on oscillator accuracy

### C/C++ Soak Test with Sequence Numbers

```cpp
// uart_stress_soak.cpp
// 24-hour loopback soak with 32-bit incrementing sequence numbers.
// Any out-of-order or missing value is flagged immediately.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

#define SOAK_HOURS   24
#define FRAME_SIZE   8        // 4-byte seq + 4-byte ~seq (ones complement check)
#define BAUD         B230400

static volatile sig_atomic_t g_stop = 0;
static uint64_t g_tx_count  = 0;
static uint64_t g_rx_ok     = 0;
static uint64_t g_rx_bad    = 0;
static uint64_t g_rx_lost   = 0;

static void handle_alarm(int s) { (void)s; g_stop = 1; }

// Build a frame: [seq(4)] [~seq(4)]
static void build_frame(uint8_t *buf, uint32_t seq)
{
    memcpy(buf,     &seq,  4);
    uint32_t inv = ~seq;
    memcpy(buf + 4, &inv, 4);
}

// Validate a received frame
static bool validate_frame(const uint8_t *buf, uint32_t expected_seq)
{
    uint32_t seq, inv;
    memcpy(&seq, buf,     4);
    memcpy(&inv, buf + 4, 4);
    return (seq == expected_seq) && (inv == ~seq);
}

static void *tx_worker(void *arg)
{
    int fd = *(int *)arg;
    uint8_t frame[FRAME_SIZE];
    uint32_t seq = 0;
    while (!g_stop) {
        build_frame(frame, seq++);
        if (write(fd, frame, FRAME_SIZE) == FRAME_SIZE)
            __atomic_add_fetch(&g_tx_count, 1, __ATOMIC_RELAXED);
        // No deliberate throttle — maximize throughput
    }
    return NULL;
}

static void *rx_worker(void *arg)
{
    int fd = *(int *)arg;
    uint8_t frame[FRAME_SIZE];
    uint32_t expected = 0;
    size_t   acc      = 0;     // accumulator index into frame

    while (!g_stop) {
        uint8_t byte;
        ssize_t n = read(fd, &byte, 1);
        if (n != 1) continue;

        frame[acc++] = byte;
        if (acc < FRAME_SIZE) continue;
        acc = 0;

        uint32_t rx_seq;
        memcpy(&rx_seq, frame, 4);

        if (rx_seq > expected)
            __atomic_add_fetch(&g_rx_lost, rx_seq - expected, __ATOMIC_RELAXED);

        if (validate_frame(frame, rx_seq))
            __atomic_add_fetch(&g_rx_ok,  1, __ATOMIC_RELAXED);
        else
            __atomic_add_fetch(&g_rx_bad, 1, __ATOMIC_RELAXED);

        expected = rx_seq + 1;
    }
    return NULL;
}

int main(void)
{
    signal(SIGALRM, handle_alarm);
    alarm(SOAK_HOURS * 3600u);

    // Open loopback port (TX and RX on same fd for loopback cable)
    int fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetspeed(&tty, BAUD);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = tty.c_oflag = tty.c_lflag = 0;
    tty.c_cc[VMIN] = 1;
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);

    pthread_t ttx, trx;
    pthread_create(&ttx, NULL, tx_worker, &fd);
    pthread_create(&trx, NULL, rx_worker, &fd);

    time_t start = time(NULL);
    while (!g_stop) {
        sleep(60);
        double elapsed = difftime(time(NULL), start) / 3600.0;
        printf("[%.2f h] TX:%lu OK:%lu BAD:%lu LOST:%lu\n",
               elapsed,
               (unsigned long)g_tx_count,
               (unsigned long)g_rx_ok,
               (unsigned long)g_rx_bad,
               (unsigned long)g_rx_lost);
    }

    g_stop = 1;
    pthread_join(ttx, NULL);
    pthread_join(trx, NULL);
    close(fd);

    printf("\n=== Soak Test Complete ===\n");
    printf("TX frames  : %lu\n", (unsigned long)g_tx_count);
    printf("RX OK      : %lu\n", (unsigned long)g_rx_ok);
    printf("RX corrupt : %lu\n", (unsigned long)g_rx_bad);
    printf("RX lost    : %lu\n", (unsigned long)g_rx_lost);
    return (g_rx_bad == 0 && g_rx_lost == 0) ? 0 : 1;
}
```

---

## 6. Interrupt Latency Under Load

When the CPU is busy with competing ISRs (SPI, timer, ADC), UART RX latency
increases. This test measures the worst-case ISR response time.

### C/C++ Latency Measurement (Linux HRTIMER)

```cpp
// uart_stress_latency.cpp
// Sends a trigger byte; receiver timestamps arrival vs. expected arrival.
// Requires hardware timestamping support (SO_TIMESTAMPING) or HRTIMER.

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <float.h>

#define ITERATIONS  10000
#define BAUD_RATE   B115200
#define BIT_TIME_NS (1000000000LL / 115200LL)   // ~8680 ns per bit

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(void)
{
    int fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetspeed(&tty, BAUD_RATE);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = tty.c_oflag = tty.c_lflag = 0;
    tty.c_cc[VMIN] = 1;
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);

    // Latency = time from TX write() call to RX read() return
    // For 8N1 at 115200: one frame = 10 bits = ~86.8 µs
    int64_t expected_ns = BIT_TIME_NS * 10;   // 10 bits per frame
    int64_t min_lat = INT64_MAX, max_lat = 0, sum_lat = 0;
    int     overdue = 0;                       // > 2× expected

    uint8_t tx = 0x55, rx;
    for (int i = 0; i < ITERATIONS; i++) {
        int64_t t0 = now_ns();
        write(fd, &tx, 1);
        read(fd,  &rx, 1);
        int64_t lat = now_ns() - t0;

        if (lat < min_lat) min_lat = lat;
        if (lat > max_lat) max_lat = lat;
        sum_lat += lat;
        if (lat > expected_ns * 2) overdue++;

        if (i % 1000 == 999)
            printf("  [%5d] lat=%6lld µs\n", i+1, (long long)(lat / 1000));
    }

    printf("\n=== Interrupt Latency Report (%d iterations) ===\n", ITERATIONS);
    printf("  Expected  : %lld µs\n", (long long)(expected_ns / 1000));
    printf("  Min       : %lld µs\n", (long long)(min_lat / 1000));
    printf("  Avg       : %lld µs\n", (long long)(sum_lat / ITERATIONS / 1000));
    printf("  Max       : %lld µs\n", (long long)(max_lat / 1000));
    printf("  >2× exp   : %d occurrences\n", overdue);
    close(fd);
    return (overdue > ITERATIONS / 100) ? 1 : 0;  // fail if >1% overdue
}
```

---

## 7. Multi-Port Concurrent Stress

Systems with many UART ports (e.g., 8-port RS-485 concentrators) must be tested
with all ports simultaneously active to expose DMA arbitration issues.

### C/C++ Multi-Port Stress

```cpp
// uart_stress_multiport.cpp
// Spawns one TX and one RX thread per port pair.
// Compile: g++ -O2 -pthread uart_stress_multiport.cpp -o multi_stress

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <atomic>
#include <vector>
#include <string>

struct PortPair {
    std::string tx_dev;
    std::string rx_dev;
    int         fd_tx = -1;
    int         fd_rx = -1;
    std::atomic<uint64_t> rx_ok   {0};
    std::atomic<uint64_t> rx_err  {0};
    int         port_id = 0;
};

static volatile int g_run = 1;

static int open_port(const char *dev, int flags)
{
    int fd = open(dev, flags | O_NOCTTY);
    struct termios tty;
    memset(&tty, 0, sizeof tty);
    cfsetspeed(&tty, B115200);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_cc[VMIN] = 1;
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

static void *port_tx(void *arg)
{
    PortPair *p = (PortPair *)arg;
    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)(i & 0xFF);
    while (g_run) write(p->fd_tx, buf, sizeof buf);
    return nullptr;
}

static void *port_rx(void *arg)
{
    PortPair *p = (PortPair *)arg;
    uint8_t buf[256];
    uint8_t expected = 0;
    while (g_run) {
        ssize_t n = read(p->fd_rx, buf, sizeof buf);
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == expected) p->rx_ok++;
            else                    p->rx_err++;
            expected++;
        }
    }
    return nullptr;
}

int main(void)
{
    // Define your port pairs here
    std::vector<PortPair> ports(4);
    const char *devs[] = {
        "/dev/ttyUSB0", "/dev/ttyUSB1",
        "/dev/ttyUSB2", "/dev/ttyUSB3",
        "/dev/ttyUSB4", "/dev/ttyUSB5",
        "/dev/ttyUSB6", "/dev/ttyUSB7"
    };

    for (int i = 0; i < 4; i++) {
        ports[i].port_id = i;
        ports[i].tx_dev  = devs[i * 2];
        ports[i].rx_dev  = devs[i * 2 + 1];
        ports[i].fd_tx   = open_port(devs[i*2],   O_WRONLY);
        ports[i].fd_rx   = open_port(devs[i*2+1], O_RDONLY);
    }

    std::vector<pthread_t> threads(8);
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i*2],   nullptr, port_tx, &ports[i]);
        pthread_create(&threads[i*2+1], nullptr, port_rx, &ports[i]);
    }

    sleep(30);  // 30-second concurrent stress
    g_run = 0;

    for (auto &t : threads) pthread_join(t, nullptr);

    printf("=== Multi-Port Stress Results ===\n");
    bool pass = true;
    for (auto &p : ports) {
        uint64_t total = p.rx_ok + p.rx_err;
        double ber = total ? 100.0 * p.rx_err / total : 0.0;
        printf("  Port %d: OK=%lu ERR=%lu BER=%.4f%%\n",
               p.port_id,
               (unsigned long)(uint64_t)p.rx_ok,
               (unsigned long)(uint64_t)p.rx_err,
               ber);
        if (ber > 0.0001) pass = false;
        close(p.fd_tx); close(p.fd_rx);
    }
    return pass ? 0 : 1;
}
```

---

## 8. Edge Case: Zero-Length and Maximum-Length Frames

Protocol parsers often have special handling for empty payloads or maximum-size
frames. These edge cases must be explicitly tested.

### C/C++ Frame Edge Case Testing

```cpp
// uart_stress_edge_frames.cpp
// Tests protocol framing with zero, one, and maximum payload sizes.
// Frame format: [SOF:1][LEN:2][PAYLOAD:N][CRC16:2][EOF:1]

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#define SOF      0x7E
#define EOF_BYTE 0x7F
#define MAX_PAYLOAD 4096

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

static size_t build_frame(uint8_t *out, const uint8_t *payload, uint16_t len)
{
    size_t idx = 0;
    out[idx++] = SOF;
    out[idx++] = (len >> 8) & 0xFF;
    out[idx++] =  len       & 0xFF;
    memcpy(out + idx, payload, len); idx += len;
    uint16_t crc = crc16_ccitt(payload, len);
    out[idx++] = (crc >> 8) & 0xFF;
    out[idx++] =  crc       & 0xFF;
    out[idx++] = EOF_BYTE;
    return idx;
}

static bool parse_and_verify(const uint8_t *raw, size_t raw_len,
                              const uint8_t *expected_payload, uint16_t expected_len)
{
    if (raw_len < 6u)          return false;
    if (raw[0] != SOF)         return false;
    uint16_t len = ((uint16_t)raw[1] << 8) | raw[2];
    if (len != expected_len)   return false;
    if (raw[3 + len + 2] != EOF_BYTE) return false;

    uint16_t rx_crc = ((uint16_t)raw[3 + len] << 8) | raw[3 + len + 1];
    uint16_t calc   = crc16_ccitt(raw + 3, len);
    if (rx_crc != calc)        return false;
    return memcmp(raw + 3, expected_payload, len) == 0;
}

int main(void)
{
    // Test sizes: 0, 1, 127, 128, 255, 256, 4095, 4096
    uint16_t sizes[] = { 0, 1, 127, 128, 255, 256, 4095, MAX_PAYLOAD };
    uint8_t  payload[MAX_PAYLOAD];
    for (int i = 0; i < MAX_PAYLOAD; i++) payload[i] = (uint8_t)(i & 0xFF);

    uint8_t frame[MAX_PAYLOAD + 8];
    bool all_pass = true;

    for (size_t t = 0; t < sizeof sizes / sizeof sizes[0]; t++) {
        uint16_t sz  = sizes[t];
        size_t   flen = build_frame(frame, payload, sz);

        bool ok = parse_and_verify(frame, flen, payload, sz);
        printf("  Frame size %5u bytes: %s\n", sz, ok ? "PASS" : "FAIL");
        if (!ok) all_pass = false;
    }

    // Edge: corrupt CRC
    {
        size_t flen = build_frame(frame, payload, 16);
        frame[flen - 3] ^= 0xFF;   // flip CRC byte
        bool ok = !parse_and_verify(frame, flen, payload, 16);   // expect failure
        printf("  Corrupt CRC detection: %s\n", ok ? "PASS" : "FAIL");
        if (!ok) all_pass = false;
    }

    // Edge: truncated frame
    {
        size_t flen = build_frame(frame, payload, 64);
        bool ok = !parse_and_verify(frame, flen - 2, payload, 64);
        printf("  Truncated frame detection: %s\n", ok ? "PASS" : "FAIL");
        if (!ok) all_pass = false;
    }

    printf("\nEdge Case Result: %s\n", all_pass ? "ALL PASS" : "SOME FAILURES");
    return all_pass ? 0 : 1;
}
```

---

## 9. Loopback Integrity Testing

The simplest hardware configuration — a single wire looping TX back to RX —
enables fast integrity tests without a second device.

### C/C++ PRBS Loopback Test

```cpp
// uart_stress_prbs_loopback.cpp
// Sends a PRBS-15 (pseudo-random binary sequence) and compares loopback.
// PRBS ensures all bit patterns are exercised, catching stuck-at faults.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#define PRBS_LEN   32768u   // one PRBS-15 period = 2^15 - 1 bits ≈ 4096 bytes

// PRBS-15: x^15 + x^14 + 1
static void gen_prbs15(uint8_t *buf, size_t bytes)
{
    uint16_t lfsr = 0x0001u;
    for (size_t i = 0; i < bytes; i++) {
        uint8_t byte = 0;
        for (int b = 7; b >= 0; b--) {
            uint16_t bit = ((lfsr >> 14) ^ (lfsr >> 13)) & 1u;
            lfsr = (lfsr << 1) | bit;
            byte |= (uint8_t)(bit << b);
        }
        buf[i] = byte;
    }
}

int main(void)
{
    int fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetspeed(&tty, B230400);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = tty.c_oflag = tty.c_lflag = 0;
    tty.c_cc[VMIN] = 1;
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);

    uint8_t tx_buf[PRBS_LEN], rx_buf[PRBS_LEN];
    gen_prbs15(tx_buf, PRBS_LEN);
    memset(rx_buf, 0, PRBS_LEN);

    write(fd, tx_buf, PRBS_LEN);
    tcdrain(fd);   // wait for TX to complete

    // Drain loopback
    size_t received = 0;
    while (received < PRBS_LEN) {
        ssize_t n = read(fd, rx_buf + received, PRBS_LEN - received);
        if (n > 0) received += (size_t)n;
    }

    int bit_errors = 0;
    for (size_t i = 0; i < PRBS_LEN; i++) {
        uint8_t diff = tx_buf[i] ^ rx_buf[i];
        while (diff) { bit_errors += diff & 1; diff >>= 1; }
    }

    double ber = (double)bit_errors / (double)(PRBS_LEN * 8);
    printf("PRBS-15 Loopback: %zu bytes, %d bit errors, BER = %.2e\n",
           PRBS_LEN, bit_errors, ber);
    close(fd);
    return (bit_errors == 0) ? 0 : 1;
}
```

---

## 10. Power and Clock Instability Simulation

Voltage brownouts and oscillator spread affect UART baud accuracy.
This test sweeps the MCU's PLL settings (or uses an external programmable
oscillator) to simulate clock variations while the UART remains active.

### C/C++ Bare-Metal Clock Stress (STM32, C)

```c
// Dynamically changes the STM32 system clock during active UART transmission
// to test UART's baud accuracy across clock configurations.

#include "stm32f4xx_hal.h"

extern UART_HandleTypeDef huart1;

// UART baud register must be reconfigured after clock switch
static void reconfigure_uart_baud(UART_HandleTypeDef *h, uint32_t new_pclk)
{
    uint32_t baud    = 115200u;
    uint32_t div     = (new_pclk + (baud / 2u)) / baud;
    h->Instance->BRR = div;
}

void uart_clock_stress_test(void)
{
    // Clock configurations to iterate: HCLK / APB1_DIV pairs
    typedef struct { uint32_t hclk_mhz; uint32_t apb1_div; } ClkCfg;
    ClkCfg configs[] = {
        { 168, RCC_HCLK_DIV4 },  // PCLK1 = 42 MHz (nominal)
        { 120, RCC_HCLK_DIV2 },  // PCLK1 = 60 MHz
        {  84, RCC_HCLK_DIV2 },  // PCLK1 = 42 MHz
        {  48, RCC_HCLK_DIV1 },  // PCLK1 = 48 MHz
    };

    uint8_t payload[] = "CLOCK_STRESS_TEST_PATTERN_ABCDEF";

    for (size_t i = 0; i < sizeof configs / sizeof configs[0]; i++) {
        // Switch APB1 prescaler (affects UART clock)
        RCC_ClkInitTypeDef clk = {0};
        uint32_t flash_lat;
        HAL_RCC_GetClockConfig(&clk, &flash_lat);
        clk.APB1CLKDivider = configs[i].apb1_div;
        HAL_RCC_ClockConfig(&clk, flash_lat);

        uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
        reconfigure_uart_baud(&huart1, pclk1);

        // Transmit and check loopback
        uint8_t rbuf[sizeof payload];
        HAL_UART_Transmit(&huart1, payload, sizeof payload - 1, 100);
        HAL_StatusTypeDef st = HAL_UART_Receive(&huart1, rbuf,
                                                sizeof payload - 1, 200);

        printf("PCLK1=%lu Hz: %s\n", (unsigned long)pclk1,
               (st == HAL_OK && memcmp(payload, rbuf, sizeof payload - 1) == 0)
               ? "PASS" : "FAIL");
    }
}
```

---

## Rust Implementation

Rust's ownership model and type system prevent entire classes of UART stress-test
bugs at compile time: no double-free on port handles, no data races on shared
counters without `Arc<Mutex<>>` or atomics.

### Cargo.toml Dependencies

```toml
[package]
name    = "uart_stress"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4"          # cross-platform serial I/O
crc        = "3"          # CRC computation
rand       = "0.8"        # PRBS / random payload
```

### Rust — Throughput and Overrun Stress Test

```rust
// src/bin/throughput.rs
// cargo run --bin throughput -- /dev/ttyUSB0 /dev/ttyUSB1 921600 30

use serialport::{SerialPort, SerialPortBuilder};
use std::{
    io::{Read, Write},
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        Arc,
    },
    thread,
    time::Duration,
};

const CHUNK: usize  = 4096;
const FILL: u8      = 0xA5;

fn open_port(path: &str, baud: u32) -> Box<dyn SerialPort> {
    serialport::new(path, baud)
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .timeout(Duration::from_millis(100))
        .open()
        .unwrap_or_else(|e| panic!("Cannot open {path}: {e}"))
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let (tx_dev, rx_dev) = (&args[1], &args[2]);
    let baud: u32        = args[3].parse().unwrap_or(921600);
    let secs: u64        = args[4].parse().unwrap_or(30);

    let stop      = Arc::new(AtomicBool::new(false));
    let tx_bytes  = Arc::new(AtomicU64::new(0));
    let rx_bytes  = Arc::new(AtomicU64::new(0));
    let rx_errors = Arc::new(AtomicU64::new(0));

    // ── TX thread ──────────────────────────────────────────────────────────
    let (stop_tx, tx_cnt) = (Arc::clone(&stop), Arc::clone(&tx_bytes));
    let mut tx_port = open_port(tx_dev, baud);
    let tx_handle = thread::spawn(move || {
        let buf = vec![FILL; CHUNK];
        while !stop_tx.load(Ordering::Relaxed) {
            match tx_port.write(&buf) {
                Ok(n)  => { tx_cnt.fetch_add(n as u64, Ordering::Relaxed); }
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {}
                Err(e) => { eprintln!("TX error: {e}"); break; }
            }
        }
    });

    // ── RX thread ──────────────────────────────────────────────────────────
    let (stop_rx, rx_cnt, rx_err) = (
        Arc::clone(&stop), Arc::clone(&rx_bytes), Arc::clone(&rx_errors),
    );
    let mut rx_port = open_port(rx_dev, baud);
    let rx_handle = thread::spawn(move || {
        let mut buf = vec![0u8; CHUNK];
        while !stop_rx.load(Ordering::Relaxed) {
            match rx_port.read(&mut buf) {
                Ok(n) => {
                    rx_cnt.fetch_add(n as u64, Ordering::Relaxed);
                    let errs = buf[..n].iter().filter(|&&b| b != FILL).count();
                    rx_err.fetch_add(errs as u64, Ordering::Relaxed);
                }
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {}
                Err(e) => { eprintln!("RX error: {e}"); break; }
            }
        }
    });

    // ── Timed soak ─────────────────────────────────────────────────────────
    for s in 1..=secs {
        thread::sleep(Duration::from_secs(1));
        println!(
            "[{:3}s] TX: {:8} kB  RX: {:8} kB  Errors: {}",
            s,
            tx_bytes.load(Ordering::Relaxed) / 1024,
            rx_bytes.load(Ordering::Relaxed) / 1024,
            rx_errors.load(Ordering::Relaxed),
        );
    }

    stop.store(true, Ordering::Relaxed);
    tx_handle.join().unwrap();
    rx_handle.join().unwrap();

    let tx = tx_bytes.load(Ordering::Relaxed);
    let rx = rx_bytes.load(Ordering::Relaxed);
    let er = rx_errors.load(Ordering::Relaxed);
    let loss = if tx > 0 { 100.0 * (tx - rx) as f64 / tx as f64 } else { 0.0 };

    println!("\n=== Throughput Stress Result ===");
    println!("  TX bytes : {tx}");
    println!("  RX bytes : {rx}");
    println!("  Loss     : {loss:.4} %");
    println!("  Errors   : {er} byte mismatches");

    std::process::exit(if er == 0 && loss < 0.001 { 0 } else { 1 });
}
```

### Rust — PRBS Loopback with CRC Framing

```rust
// src/bin/prbs_framed.rs
// Framed PRBS loopback: [SOF][LEN:2][PAYLOAD][CRC16][EOF]
// cargo run --bin prbs_framed -- /dev/ttyUSB0

use crc::{Crc, CRC_16_IBM_SDLC};  // CRC-16/CCITT
use serialport::SerialPort;
use std::{io::{Read, Write}, time::Duration};

const SOF:      u8    = 0x7E;
const EOF_BYTE: u8    = 0x7F;
const CCITT:    Crc<u16> = Crc::<u16>::new(&CRC_16_IBM_SDLC);

fn build_frame(payload: &[u8]) -> Vec<u8> {
    let len = payload.len() as u16;
    let crc = CCITT.checksum(payload);
    let mut f = Vec::with_capacity(payload.len() + 6);
    f.push(SOF);
    f.push((len >> 8) as u8);
    f.push(len as u8);
    f.extend_from_slice(payload);
    f.push((crc >> 8) as u8);
    f.push(crc as u8);
    f.push(EOF_BYTE);
    f
}

fn parse_frame<'a>(raw: &'a [u8], expected: &[u8]) -> Result<(), &'static str> {
    if raw.len() < 6              { return Err("too short"); }
    if raw[0] != SOF              { return Err("bad SOF"); }
    let len = ((raw[1] as usize) << 8) | raw[2] as usize;
    if raw.len() < len + 6       { return Err("truncated"); }
    if raw[3 + len + 2] != EOF_BYTE { return Err("bad EOF"); }
    let payload  = &raw[3..3 + len];
    let rx_crc   = ((raw[3 + len] as u16) << 8) | raw[3 + len + 1] as u16;
    let calc_crc = CCITT.checksum(payload);
    if rx_crc != calc_crc        { return Err("CRC mismatch"); }
    if payload != expected        { return Err("data mismatch"); }
    Ok(())
}

// PRBS-15 generator
fn prbs15(n: usize) -> Vec<u8> {
    let mut lfsr = 1u16;
    (0..n).map(|_| {
        let mut byte = 0u8;
        for b in (0..8).rev() {
            let bit = ((lfsr >> 14) ^ (lfsr >> 13)) & 1;
            lfsr = (lfsr << 1) | bit;
            byte |= (bit as u8) << b;
        }
        byte
    }).collect()
}

fn main() {
    let dev = std::env::args().nth(1).unwrap_or_else(|| "/dev/ttyUSB0".into());
    let mut port: Box<dyn SerialPort> = serialport::new(&dev, 230400)
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .timeout(Duration::from_secs(5))
        .open()
        .expect("open failed");

    // Test payload sizes with PRBS data
    let sizes = [0usize, 1, 127, 128, 255, 256, 1023, 4096];
    let mut all_pass = true;

    for &sz in &sizes {
        let payload = prbs15(sz);
        let frame   = build_frame(&payload);
        let flen    = frame.len();

        port.write_all(&frame).expect("write failed");

        let mut rx = vec![0u8; flen];
        port.read_exact(&mut rx).expect("read failed");

        match parse_frame(&rx, &payload) {
            Ok(())   => println!("  Frame {:5} bytes: PASS", sz),
            Err(msg) => { println!("  Frame {:5} bytes: FAIL ({msg})"); all_pass = false; }
        }
    }

    // Edge: corrupt a CRC byte
    {
        let payload = prbs15(64);
        let mut frame = build_frame(&payload);
        let flen = frame.len();
        frame[flen - 3] ^= 0xFF;
        let caught = parse_frame(&frame, &payload).is_err();
        println!("  Corrupt CRC detection: {}", if caught { "PASS" } else { "FAIL" });
        if !caught { all_pass = false; }
    }

    println!("\nResult: {}", if all_pass { "ALL PASS" } else { "SOME FAILURES" });
    std::process::exit(if all_pass { 0 } else { 1 });
}
```

### Rust — Soak Test with Sequence Numbers

```rust
// src/bin/soak.rs
// 24-hour loopback soak with 32-bit sequence numbers.
// cargo run --bin soak -- /dev/ttyUSB0 24

use serialport::SerialPort;
use std::{
    io::{Read, Write},
    sync::{atomic::{AtomicBool, AtomicU64, Ordering}, Arc},
    thread,
    time::{Duration, Instant},
};

const FRAME: usize = 8; // [seq:4][~seq:4]

fn make_frame(seq: u32) -> [u8; FRAME] {
    let mut f = [0u8; FRAME];
    f[..4].copy_from_slice(&seq.to_le_bytes());
    f[4..].copy_from_slice(&(!seq).to_le_bytes());
    f
}

fn check_frame(raw: &[u8; FRAME]) -> Option<u32> {
    let seq  = u32::from_le_bytes(raw[..4].try_into().unwrap());
    let iinv = u32::from_le_bytes(raw[4..].try_into().unwrap());
    if iinv == !seq { Some(seq) } else { None }
}

fn open_rw(dev: &str) -> Box<dyn SerialPort> {
    serialport::new(dev, 230400)
        .timeout(Duration::from_millis(200))
        .open()
        .unwrap_or_else(|e| panic!("open {dev}: {e}"))
}

fn main() {
    let dev   = std::env::args().nth(1).unwrap_or_else(|| "/dev/ttyUSB0".into());
    let hours: u64 = std::env::args().nth(2).and_then(|s| s.parse().ok()).unwrap_or(24);

    let stop  = Arc::new(AtomicBool::new(false));
    let tx_n  = Arc::new(AtomicU64::new(0));
    let rx_ok = Arc::new(AtomicU64::new(0));
    let rx_ba = Arc::new(AtomicU64::new(0));
    let rx_ls = Arc::new(AtomicU64::new(0));

    // ── TX ─────────────────────────────────────────────────────────────────
    let (s1, c1) = (Arc::clone(&stop), Arc::clone(&tx_n));
    let mut tx = open_rw(&dev);
    let th_tx = thread::spawn(move || {
        let mut seq = 0u32;
        while !s1.load(Ordering::Relaxed) {
            let _ = tx.write_all(&make_frame(seq));
            seq = seq.wrapping_add(1);
            c1.fetch_add(1, Ordering::Relaxed);
        }
    });

    // ── RX ─────────────────────────────────────────────────────────────────
    let (s2, c_ok, c_ba, c_ls) = (
        Arc::clone(&stop), Arc::clone(&rx_ok),
        Arc::clone(&rx_ba), Arc::clone(&rx_ls),
    );
    let mut rx = open_rw(&dev);
    let th_rx = thread::spawn(move || {
        let mut expected = 0u32;
        let mut buf = [0u8; FRAME];
        let mut acc = 0usize;
        let mut tmp = [0u8; 1];
        while !s2.load(Ordering::Relaxed) {
            if rx.read(&mut tmp).is_ok() {
                buf[acc] = tmp[0];
                acc += 1;
                if acc == FRAME {
                    acc = 0;
                    match check_frame(&buf) {
                        Some(seq) => {
                            if seq > expected {
                                c_ls.fetch_add((seq - expected) as u64, Ordering::Relaxed);
                            }
                            c_ok.fetch_add(1, Ordering::Relaxed);
                            expected = seq.wrapping_add(1);
                        }
                        None => { c_ba.fetch_add(1, Ordering::Relaxed); }
                    }
                }
            }
        }
    });

    // ── Reporting ──────────────────────────────────────────────────────────
    let deadline = Instant::now() + Duration::from_secs(hours * 3600);
    let mut next_report = Instant::now() + Duration::from_secs(60);
    let start = Instant::now();

    while Instant::now() < deadline {
        thread::sleep(Duration::from_secs(1));
        if Instant::now() >= next_report {
            next_report += Duration::from_secs(60);
            let elapsed = start.elapsed().as_secs_f64() / 3600.0;
            println!(
                "[{:.2}h] TX:{} OK:{} BAD:{} LOST:{}",
                elapsed,
                tx_n.load(Ordering::Relaxed),
                rx_ok.load(Ordering::Relaxed),
                rx_ba.load(Ordering::Relaxed),
                rx_ls.load(Ordering::Relaxed),
            );
        }
    }

    stop.store(true, Ordering::Relaxed);
    th_tx.join().unwrap();
    th_rx.join().unwrap();

    let bad  = rx_ba.load(Ordering::Relaxed);
    let lost = rx_ls.load(Ordering::Relaxed);
    println!("\n=== Soak Complete ===");
    println!("TX:{} OK:{} BAD:{} LOST:{}",
        tx_n.load(Ordering::Relaxed),
        rx_ok.load(Ordering::Relaxed), bad, lost);
    std::process::exit(if bad == 0 && lost == 0 { 0 } else { 1 });
}
```

---

## Test Metrics and Reporting

All stress tests should capture and report the following KPIs:

| Metric | Description | Acceptable Threshold |
|---|---|---|
| **Byte Error Rate (BER)** | Fraction of corrupted bytes | < 10⁻⁶ in production |
| **Frame Loss Rate** | Missing sequence numbers / total frames | < 10⁻⁵ |
| **Hardware Overrun Count** | `TIOCGICOUNT.overrun` or MCU SR.ORE | 0 at rated speed |
| **Max ISR Latency** | Worst-case time from byte-ready to read() | < 2× frame time |
| **Framing Errors** | Stop-bit violations | 0 at nominal baud |
| **Parity Errors** | Bit-flip events during noise injection | Counted and flagged |
| **Throughput Efficiency** | `RX_bytes / TX_bytes × 100` | ≥ 99.999 % |

### Recommended Test Matrix

```
┌────────────────────────┬────────┬─────────────────────────────────────────┐
│  Test                  │ Dur.   │ Pass Criteria                           │
├────────────────────────┼────────┼─────────────────────────────────────────┤
│  Max Throughput        │ 30 min │ Loss < 0.001%, BER = 0                  │
│  Buffer Overrun        │  5 min │ Overrun count logged; SW handles cleanly│
│  Parity Injection      │  2 min │ All parity errors detected; no hang     │
│  Baud Boundary ±3%     │  5 min │ Pass at ±2%, graceful fail at ±3%       │
│  24h Soak              │ 24 hr  │ Zero frame loss, zero corruption        │
│  ISR Latency           │ 10K it │ Max < 2× frame time; >1% overdue = FAIL│
│  4-Port Concurrent     │ 30 min │ BER < 0.0001% per port                  │
│  Edge Frame Sizes      │  1 min │ All sizes parse correctly               │
│  PRBS Loopback         │  1 run │ Zero bit errors                         │
│  Clock Instability     │ 10 min │ Pass across all clock configurations    │
└────────────────────────┴────────┴─────────────────────────────────────────┘
```

---

## Summary

UART stress testing is not optional for production embedded systems — it is the
mechanism by which you prove that the interface remains reliable under the full
range of operating conditions the device will encounter in the field.

The key insights from this chapter are:

**Hardware limits are invisible until you hit them.** The RX FIFO is typically
only 4–32 bytes deep on common MCUs. Without adequate ISR priority and fast
service routines, even moderate baud rates will produce silent overruns.

**Software ring buffers must be lockless-safe.** Any shared buffer between an ISR
and the application thread requires careful memory ordering. In C, use `volatile`
or memory barriers; in Rust, use atomics or `Mutex<>` — the compiler will not
allow races.

**Framing errors require active recovery.** A single malformed stop bit can
desynchronise a parser indefinitely if the software has no re-sync mechanism. Test
explicit error-injection to verify your parser recovers.

**Soak tests catch what unit tests miss.** Clock drift, thermal oscillator
variation, and accumulating descriptor fragmentation only manifest over hours or
days of continuous operation. A 24-hour soak with sequence-numbered frames is the
minimum bar for mission-critical UART interfaces.

**Rust's ownership model is a force multiplier.** Thread-safety violations that
silently corrupt C UART drivers at runtime — double-read of a ring-buffer slot,
non-atomic counter updates — are compile-time errors in Rust, dramatically
reducing the debugging cycle for stress-test failures.

**Measure, don't assume.** Always instrument your stress tests with byte counts,
error counters, ISR latency histograms, and hardware error registers. The
difference between "seems to work" and "verified to 10⁻⁶ BER" is a complete set
of captured metrics.

---

*Document: 34_Stress_Testing.md — Part of the UART Programming Reference Series*