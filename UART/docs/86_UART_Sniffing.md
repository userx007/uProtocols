# 86. UART Sniffing

- **Protocol fundamentals** — frame structure table, key parameters (baud, parity, stop bits, voltage levels)
- **What is sniffing?** — passive tap concept with ASCII wiring diagram and voltage-safety table
- **Hardware setup** — four approaches: USB-UART adapters, dual-UART MCU, logic analyser, bit-bang
- **Software architecture** — layered block diagram (reader → ring buffer → decoder → output)

**C/C++ examples (4 programs):**
1. `uart_sniffer.c` — Linux `termios` dual-channel sniffer with `pthreads`, `select()`, and hex dump
2. `uart_sniffer_mcu.c` — Bare-metal ring-buffer + ISR sniffer with packet-gap framing
3. `stm32_sniffer.c` — STM32 HAL interrupt-driven sniffer using `HAL_UART_RxCpltCallback`
4. `UartSniffer.hpp` — C++17 class with `std::thread`, idle-gap packet detection, and a callback interface

**Rust examples (3 programs):**
1. Cross-platform sniffer using the `serialport` crate with threading
2. Async sniffer with `tokio::spawn_blocking` for concurrent dual-port reading
3. State-machine frame decoder with sync hunting, CRC validation, and unit tests

**Additional sections:** offline Python analysis script, a troubleshooting table of 8 common failure modes, security/ethics considerations, and a final language-comparison summary table.

### Passive Monitoring of UART Communication for Debugging

---

## Table of Contents

1. [Introduction](#introduction)
2. [UART Protocol Fundamentals](#uart-protocol-fundamentals)
3. [What is UART Sniffing?](#what-is-uart-sniffing)
4. [Hardware Setup for UART Sniffing](#hardware-setup-for-uart-sniffing)
5. [Software Architecture](#software-architecture)
6. [Programming in C/C++](#programming-in-cc)
   - [Linux (termios) UART Sniffer](#linux-termios-uart-sniffer)
   - [Microcontroller UART Sniffer (AVR/ARM)](#microcontroller-uart-sniffer-avrarm)
   - [Interrupt-Driven UART Sniffer (STM32 HAL)](#interrupt-driven-uart-sniffer-stm32-hal)
   - [Dual-Port UART Bus Monitor (C++)](#dual-port-uart-bus-monitor-c)
7. [Programming in Rust](#programming-in-rust)
   - [Cross-Platform Serial Port Sniffer](#cross-platform-serial-port-sniffer)
   - [Async Dual-Channel Sniffer with tokio](#async-dual-channel-sniffer-with-tokio)
   - [Protocol Decoder with State Machine](#protocol-decoder-with-state-machine)
8. [Decoding and Analysing Captured Data](#decoding-and-analysing-captured-data)
9. [Common Pitfalls and Troubleshooting](#common-pitfalls-and-troubleshooting)
10. [Security and Ethical Considerations](#security-and-ethical-considerations)
11. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) is one of the oldest and most widely used serial communication protocols in embedded systems, IoT devices, industrial controllers, and consumer electronics. **UART Sniffing** is the practice of passively observing — without interfering with — an active UART communication channel. It is an essential technique in:

- **Firmware reverse engineering** — understanding undocumented device protocols
- **Hardware bring-up** — verifying that two devices communicate correctly
- **Production debugging** — capturing intermittent errors in field devices
- **Security auditing** — discovering credentials, commands, or data leaked over serial lines
- **Protocol analysis** — reconstructing proprietary binary or text-based protocols

Unlike active communication (where your system is a sender or receiver), sniffing is a **read-only, passive** activity. The sniffer observes the electrical signals on the bus and reconstructs the data stream without affecting the devices under test.

---

## UART Protocol Fundamentals

Before sniffing, a solid understanding of the UART framing model is required.

### Frame Structure

```
Idle  Start  D0  D1  D2  D3  D4  D5  D6  D7  Parity  Stop
 ¯¯¯¯  _      _   ¯   _   ¯   ¯   _   ¯   _    ¯       ¯¯¯¯
```

| Field   | Level      | Duration         | Notes                               |
|---------|------------|------------------|-------------------------------------|
| Idle    | HIGH (1)   | —                | Line rests high when no data        |
| Start   | LOW (0)    | 1 bit period     | Always one start bit                |
| Data    | D0 – D7    | 5–9 bit periods  | LSB first by convention             |
| Parity  | 0 or 1     | 1 bit (optional) | Even, Odd, Mark, Space, or None     |
| Stop    | HIGH (1)   | 1–2 bit periods  | Returns line to idle                |

### Key Parameters to Know Before Sniffing

| Parameter     | Typical Values                                  |
|---------------|--------------------------------------------------|
| Baud rate     | 300, 1200, 9600, 19200, 38400, 57600, 115200 bps |
| Data bits     | 7 or 8 (8 is most common)                       |
| Parity        | None, Even, Odd                                  |
| Stop bits     | 1 or 2                                           |
| Voltage level | 3.3 V, 5 V (TTL), ±12 V (RS-232)               |

> **Critical:** Mismatching even one parameter will produce garbled data or silence. Use an oscilloscope or logic analyser first if parameters are unknown.

---

## What is UART Sniffing?

UART sniffing means connecting a third observer to the TX and/or RX lines of an existing UART link — **without breaking the circuit**.

```
  Device A                                    Device B
  ┌────────┐                                  ┌────────┐
  │   TX ──┼──────────────────────────────────┼── RX   │
  │   RX ──┼──────────────────────────────────┼── TX   │
  │   GND ─┼──────────────────────────────────┼── GND  │
  └────────┘                                  └────────┘
       │                    │
       └────────┐  ┌────────┘
                │  │
           ┌────┴──┴────┐
           │  Sniffer   │
           │  RX1  RX2  │   ← two RX channels, no TX
           └────────────┘
```

The sniffer connects:
- Its **RX1** to Device A's TX line (capturing A → B traffic)
- Its **RX2** to Device B's TX line (capturing B → A traffic)
- **No TX line is driven** — the sniffer must never transmit on the bus

### Voltage-Level Safety

| Bus Voltage | Sniffer Input Tolerance | Action Required                  |
|-------------|------------------------|----------------------------------|
| 3.3 V TTL   | 3.3 V                  | Direct connection OK             |
| 5 V TTL     | 3.3 V MCU              | Use voltage divider or level shifter |
| RS-232 ±12 V| 3.3/5 V                | **Mandatory** MAX3232 / SP3232 converter |

---

## Hardware Setup for UART Sniffing

### Option 1 — USB-to-UART Adapter (PC-based Sniffing)

The simplest approach for benchtop debugging uses two USB-to-TTL adapters:

```
  Bus TX (A→B)  ──────►  Adapter 1 RX  ──► USB ──► /dev/ttyUSB0
  Bus TX (B→A)  ──────►  Adapter 2 RX  ──► USB ──► /dev/ttyUSB1
  Common GND    ──────►  Both adapters GND
```

Common USB adapters: CP2102, CH340, FT232RL, PL2303.

### Option 2 — Dual-UART Microcontroller

An STM32, ESP32, or RP2040 with two hardware UART peripherals configured in receive-only mode. One core/task handles each channel with timestamps.

### Option 3 — Logic Analyser

Tools like the Saleae Logic or a cheap 8-channel clone can decode UART at the hardware level. This is the most reliable hardware approach but requires separate software (Sigrok/PulseView, Saleae Logic 2).

### Option 4 — Bit-Bang Sniffing (Low Baud Rates Only)

At baud rates up to ~19200, a GPIO pin configured as input and sampled at >8× the baud rate can reconstruct the data in software. Not recommended for 115200+ bps.

---

## Software Architecture

A robust UART sniffer requires the following components:

```
┌─────────────────────────────────────────────────────────┐
│                    UART Sniffer Software                │
│                                                         │
│  ┌──────────┐   ┌──────────┐   ┌───────────────────┐  │
│  │  Port A  │   │  Port B  │   │   Ring Buffer     │  │
│  │  Reader  │──►│  Reader  │──►│   (Thread-safe)   │  │
│  └──────────┘   └──────────┘   └────────┬──────────┘  │
│                                          │              │
│                               ┌──────────▼──────────┐  │
│                               │  Protocol Decoder   │  │
│                               │  (State Machine)    │  │
│                               └──────────┬──────────┘  │
│                                          │              │
│                    ┌─────────────────────┼──────────┐  │
│                    │                     │          │  │
│             ┌──────▼───┐        ┌───────▼───┐      │  │
│             │  Console  │        │  Log File │      │  │
│             │  Display  │        │  (Binary  │      │  │
│             └──────────┘        │   + Text) │      │  │
│                                 └───────────┘      │  │
└─────────────────────────────────────────────────────────┘
```

Key design requirements:

1. **Non-blocking I/O** — sniffer must never stall waiting for data
2. **Thread/task safety** — concurrent reads from two ports
3. **Timestamping** — microsecond-resolution timestamps on each byte/frame
4. **Ring buffers** — handle bursts without losing data
5. **Protocol decoding** — optional layer to interpret the raw stream

---

## Programming in C/C++

### Linux (termios) UART Sniffer

This example opens two serial ports and logs all received bytes with timestamps.

```c
/*
 * uart_sniffer.c — Dual-channel passive UART sniffer for Linux
 *
 * Usage:  gcc -O2 -pthread uart_sniffer.c -o uart_sniffer
 *         ./uart_sniffer /dev/ttyUSB0 /dev/ttyUSB1 115200
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <termios.h>
#include <sys/select.h>

#define BUFFER_SIZE   4096
#define MAX_LINE_HEX    16   /* bytes per hex dump line */

/* ------------------------------------------------------------------ */
/* Baud-rate mapping                                                   */
/* ------------------------------------------------------------------ */
static speed_t baud_constant(int baud)
{
    switch (baud) {
        case     300: return B300;
        case    1200: return B1200;
        case    2400: return B2400;
        case    4800: return B4800;
        case    9600: return B9600;
        case   19200: return B19200;
        case   38400: return B38400;
        case   57600: return B57600;
        case  115200: return B115200;
        case  230400: return B230400;
        case  460800: return B460800;
        case  921600: return B921600;
        default:
            fprintf(stderr, "Unsupported baud rate %d\n", baud);
            exit(EXIT_FAILURE);
    }
}

/* ------------------------------------------------------------------ */
/* Open and configure a serial port in raw, read-only mode            */
/* ------------------------------------------------------------------ */
static int open_uart(const char *path, int baud)
{
    int fd = open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror(path);
        exit(EXIT_FAILURE);
    }

    struct termios tty;
    memset(&tty, 0, sizeof tty);
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }

    speed_t speed = baud_constant(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    cfmakeraw(&tty);           /* raw mode — no processing            */
    tty.c_cflag |= CLOCAL;    /* ignore modem control lines          */
    tty.c_cflag &= ~CRTSCTS;  /* no hardware flow control            */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;        /* 8 data bits                        */
    tty.c_cflag &= ~PARENB;    /* no parity                          */
    tty.c_cflag &= ~CSTOPB;    /* 1 stop bit                         */

    tty.c_cc[VMIN]  = 0;       /* non-blocking read                  */
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
    return fd;
}

/* ------------------------------------------------------------------ */
/* Get current time as a formatted string with microsecond precision  */
/* ------------------------------------------------------------------ */
static void get_timestamp(char *buf, size_t len)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm_info = localtime(&ts.tv_sec);
    char time_buf[32];
    strftime(time_buf, sizeof time_buf, "%H:%M:%S", tm_info);
    snprintf(buf, len, "%s.%06ld", time_buf, ts.tv_nsec / 1000);
}

/* ------------------------------------------------------------------ */
/* Print a hex + ASCII dump line                                       */
/* ------------------------------------------------------------------ */
static void hex_dump(const uint8_t *data, size_t len,
                     const char *channel, const char *ts)
{
    for (size_t offset = 0; offset < len; offset += MAX_LINE_HEX) {
        size_t row = (len - offset < MAX_LINE_HEX)
                     ? (len - offset) : MAX_LINE_HEX;

        printf("[%s] %s  %04zx  ", ts, channel, offset);

        /* Hex bytes */
        for (size_t i = 0; i < MAX_LINE_HEX; i++) {
            if (i < row) printf("%02x ", data[offset + i]);
            else         printf("   ");
            if (i == 7)  printf(" ");
        }

        /* ASCII representation */
        printf(" |");
        for (size_t i = 0; i < row; i++) {
            uint8_t c = data[offset + i];
            printf("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
        }
        printf("|\n");
    }
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Per-channel sniffer thread arguments                               */
/* ------------------------------------------------------------------ */
typedef struct {
    int         fd;
    const char *channel_name;   /* "A->B" or "B->A" */
    FILE       *log_file;
} channel_args_t;

/* ------------------------------------------------------------------ */
/* Sniffer thread: reads from one UART port and dumps all data        */
/* ------------------------------------------------------------------ */
static void *sniffer_thread(void *arg)
{
    channel_args_t *ch = (channel_args_t *)arg;
    uint8_t buf[BUFFER_SIZE];
    char ts[32];

    printf("[INFO] Sniffing channel %s on fd=%d\n",
           ch->channel_name, ch->fd);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(ch->fd, &rfds);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ret = select(ch->fd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (ret == 0) continue;   /* timeout — no data, loop again   */

        ssize_t n = read(ch->fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("read");
            break;
        }
        if (n == 0) continue;

        get_timestamp(ts, sizeof ts);
        hex_dump(buf, (size_t)n, ch->channel_name, ts);

        /* Write raw binary to log file if provided */
        if (ch->log_file) {
            fwrite(buf, 1, (size_t)n, ch->log_file);
            fflush(ch->log_file);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr,
            "Usage: %s <port_A> <port_B> <baud>\n"
            "Example: %s /dev/ttyUSB0 /dev/ttyUSB1 115200\n",
            argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    int baud = atoi(argv[3]);
    int fd_a = open_uart(argv[1], baud);
    int fd_b = open_uart(argv[2], baud);

    FILE *log_a = fopen("channel_a.bin", "wb");
    FILE *log_b = fopen("channel_b.bin", "wb");

    channel_args_t ch_a = { fd_a, "A->B", log_a };
    channel_args_t ch_b = { fd_b, "B->A", log_b };

    pthread_t thr_a, thr_b;
    pthread_create(&thr_a, NULL, sniffer_thread, &ch_a);
    pthread_create(&thr_b, NULL, sniffer_thread, &ch_b);

    printf("[INFO] UART sniffer running. Press Ctrl+C to stop.\n");
    printf("[INFO] Port A (%s) → channel A->B\n", argv[1]);
    printf("[INFO] Port B (%s) → channel B->A\n", argv[2]);

    pthread_join(thr_a, NULL);
    pthread_join(thr_b, NULL);

    if (log_a) fclose(log_a);
    if (log_b) fclose(log_b);
    close(fd_a);
    close(fd_b);
    return EXIT_SUCCESS;
}
```

---

### Microcontroller UART Sniffer (AVR/ARM)

For embedded sniffers running on a bare-metal MCU with two hardware UARTs, here is a portable ring-buffer approach:

```c
/*
 * uart_sniffer_mcu.c — Bare-metal dual-UART passive sniffer
 *
 * Targets: AVR ATmega2560, STM32, or any MCU with 2 hardware UARTs.
 * Adapt UART_Init / UART_TX to your HAL.
 *
 * UART0 = sniff channel A->B  (RX only, configured as input)
 * UART1 = sniff channel B->A  (RX only, configured as input)
 * UART2 = output channel      (sends decoded data to PC)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Ring buffer                                                         */
/* ------------------------------------------------------------------ */
#define RING_SIZE 256   /* must be a power of 2 */
#define RING_MASK (RING_SIZE - 1)

typedef struct {
    volatile uint8_t  buf[RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint8_t  channel;   /* 'A' or 'B' */
} ring_buf_t;

static ring_buf_t rx_a = { .channel = 'A' };
static ring_buf_t rx_b = { .channel = 'B' };

static inline bool ring_put(ring_buf_t *r, uint8_t byte)
{
    uint16_t next = (r->head + 1) & RING_MASK;
    if (next == r->tail) return false;   /* overflow — drop byte     */
    r->buf[r->head] = byte;
    r->head = next;
    return true;
}

static inline bool ring_get(ring_buf_t *r, uint8_t *byte)
{
    if (r->head == r->tail) return false;
    *byte = r->buf[r->tail];
    r->tail = (r->tail + 1) & RING_MASK;
    return true;
}

static inline bool ring_empty(const ring_buf_t *r)
{
    return r->head == r->tail;
}

/* ------------------------------------------------------------------ */
/* ISR stubs — replace with actual MCU vector names                   */
/* ------------------------------------------------------------------ */

/* Example for AVR ATmega2560:
 * ISR(USART0_RX_vect)  { ring_put(&rx_a, UDR0); }
 * ISR(USART1_RX_vect)  { ring_put(&rx_b, UDR1); }
 */

/* Generic C99 placeholder — call these from your actual ISRs */
void uart0_rx_isr(void) {
    /* uint8_t byte = UART0->DR;  (your register) */
    /* ring_put(&rx_a, byte); */
}

void uart1_rx_isr(void) {
    /* uint8_t byte = UART1->DR; */
    /* ring_put(&rx_b, byte); */
}

/* ------------------------------------------------------------------ */
/* Output helpers — send decoded data via UART2 to PC                 */
/* ------------------------------------------------------------------ */
static void uart_tx_char(char c)
{
    /* Platform-specific transmit — replace with your HAL */
    (void)c;
    /* e.g. while (!(UCSR2A & (1<<UDRE2))); UDR2 = c; */
}

static void uart_tx_str(const char *s)
{
    while (*s) uart_tx_char(*s++);
}

static void uart_tx_hex(uint8_t byte)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_tx_char(hex[byte >> 4]);
    uart_tx_char(hex[byte & 0x0F]);
    uart_tx_char(' ');
}

/* ------------------------------------------------------------------ */
/* Sniffer processing — call from main loop                           */
/* ------------------------------------------------------------------ */
#define IDLE_THRESHOLD_MS  5   /* ms gap → treat as end of packet    */

typedef struct {
    uint32_t last_rx_tick;     /* replace with your tick counter     */
    uint8_t  packet[128];
    uint8_t  packet_len;
    bool     in_packet;
} packet_state_t;

static packet_state_t pkt_a, pkt_b;

static uint32_t get_tick_ms(void)
{
    /* Replace with SysTick / timer counter */
    return 0;
}

static void flush_packet(packet_state_t *ps, char channel)
{
    if (ps->packet_len == 0) return;
    char header[16];
    /* Format: "[A] len=5: " */
    uart_tx_char('[');
    uart_tx_char(channel);
    uart_tx_char(']');
    uart_tx_char(' ');
    for (uint8_t i = 0; i < ps->packet_len; i++) {
        uart_tx_hex(ps->packet[i]);
    }
    uart_tx_str("\r\n");
    ps->packet_len = 0;
    ps->in_packet  = false;
}

static void process_channel(ring_buf_t *r, packet_state_t *ps)
{
    uint8_t byte;
    uint32_t now = get_tick_ms();

    /* Flush packet on idle gap */
    if (ps->in_packet &&
        (now - ps->last_rx_tick) > IDLE_THRESHOLD_MS) {
        flush_packet(ps, (char)r->channel);
    }

    while (ring_get(r, &byte)) {
        ps->last_rx_tick = get_tick_ms();
        ps->in_packet    = true;
        if (ps->packet_len < sizeof ps->packet) {
            ps->packet[ps->packet_len++] = byte;
        }
    }
}

void sniffer_main_loop(void)
{
    while (1) {
        process_channel(&rx_a, &pkt_a);
        process_channel(&rx_b, &pkt_b);
        /* Other tasks can run here */
    }
}
```

---

### Interrupt-Driven UART Sniffer (STM32 HAL)

```c
/*
 * stm32_sniffer.c — STM32 HAL-based UART sniffer
 *
 * Uses UART2 (sniff A->B) + UART3 (sniff B->A) in receive-only mode.
 * UART1 outputs decoded data to a PC terminal.
 *
 * HAL callbacks are registered to avoid polling overhead.
 */

#include "main.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;  /* Debug output to PC        */
extern UART_HandleTypeDef huart2;  /* Sniff channel A (RX only) */
extern UART_HandleTypeDef huart3;  /* Sniff channel B (RX only) */

#define SNIFF_BUF_LEN  1

static uint8_t sniff_rx_a;   /* single-byte DMA/IT target */
static uint8_t sniff_rx_b;

/* Accumulated frame buffers */
#define FRAME_BUF_SIZE 256
static uint8_t frame_a[FRAME_BUF_SIZE];
static uint8_t frame_b[FRAME_BUF_SIZE];
static uint16_t frame_a_len = 0;
static uint16_t frame_b_len = 0;

/* ------------------------------------------------------------------ */
/* Start sniffing — call from main() after MX_USARTx_UART_Init()     */
/* ------------------------------------------------------------------ */
void Sniffer_Start(void)
{
    HAL_UART_Receive_IT(&huart2, &sniff_rx_a, SNIFF_BUF_LEN);
    HAL_UART_Receive_IT(&huart3, &sniff_rx_b, SNIFF_BUF_LEN);
}

/* ------------------------------------------------------------------ */
/* HAL RX-complete callback — fires on every received byte            */
/* ------------------------------------------------------------------ */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        /* Channel A byte received */
        if (frame_a_len < FRAME_BUF_SIZE) {
            frame_a[frame_a_len++] = sniff_rx_a;
        }
        /* Re-arm the interrupt */
        HAL_UART_Receive_IT(&huart2, &sniff_rx_a, SNIFF_BUF_LEN);
    }
    else if (huart->Instance == USART3) {
        /* Channel B byte received */
        if (frame_b_len < FRAME_BUF_SIZE) {
            frame_b[frame_b_len++] = sniff_rx_b;
        }
        HAL_UART_Receive_IT(&huart3, &sniff_rx_b, SNIFF_BUF_LEN);
    }
}

/* ------------------------------------------------------------------ */
/* Periodic flush — call from a 10 ms TIM interrupt or main loop      */
/* ------------------------------------------------------------------ */
void Sniffer_Flush(void)
{
    char line[512];
    int  pos;

    if (frame_a_len > 0) {
        pos = snprintf(line, sizeof line, "[A->B] %3u bytes: ", frame_a_len);
        for (uint16_t i = 0; i < frame_a_len && pos < 480; i++) {
            pos += snprintf(line + pos, sizeof line - pos,
                            "%02X ", frame_a[i]);
        }
        pos += snprintf(line + pos, sizeof line - pos, "\r\n");
        HAL_UART_Transmit(&huart1, (uint8_t *)line, pos, 100);
        frame_a_len = 0;
    }

    if (frame_b_len > 0) {
        pos = snprintf(line, sizeof line, "[B->A] %3u bytes: ", frame_b_len);
        for (uint16_t i = 0; i < frame_b_len && pos < 480; i++) {
            pos += snprintf(line + pos, sizeof line - pos,
                            "%02X ", frame_b[i]);
        }
        pos += snprintf(line + pos, sizeof line - pos, "\r\n");
        HAL_UART_Transmit(&huart1, (uint8_t *)line, pos, 100);
        frame_b_len = 0;
    }
}
```

---

### Dual-Port UART Bus Monitor (C++)

A more advanced C++ class with protocol-agnostic packet detection using idle-gap heuristics:

```cpp
/*
 * UartSniffer.hpp — C++17 dual-channel UART bus monitor
 *
 * Compile: g++ -std=c++17 -O2 -pthread UartSniffer.cpp -o sniffer
 */

#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace uart {

using Bytes     = std::vector<uint8_t>;
using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;
using PacketCb  = std::function<void(const std::string& channel,
                                     const Bytes& data,
                                     Timestamp ts)>;

struct SniffConfig {
    std::string port_a;          /* Device A TX line */
    std::string port_b;          /* Device B TX line */
    int         baud_rate  = 115200;
    int         data_bits  = 8;
    bool        parity     = false;
    int         stop_bits  = 1;
    std::chrono::milliseconds packet_gap{5}; /* idle gap → packet boundary */
    bool        hex_dump   = true;
    std::string log_path   = "";
};

class UartSniffer {
public:
    explicit UartSniffer(SniffConfig cfg, PacketCb on_packet = nullptr)
        : cfg_(std::move(cfg))
        , on_packet_(std::move(on_packet))
        , running_(false)
    {}

    ~UartSniffer() { stop(); }

    void start()
    {
        running_ = true;
        thr_a_ = std::thread(&UartSniffer::reader_loop, this,
                              cfg_.port_a, "A->B");
        thr_b_ = std::thread(&UartSniffer::reader_loop, this,
                              cfg_.port_b, "B->A");
    }

    void stop()
    {
        running_ = false;
        if (thr_a_.joinable()) thr_a_.join();
        if (thr_b_.joinable()) thr_b_.join();
    }

private:
    /* ---- Platform I/O: open a serial port (POSIX) ---- */
    int open_port(const std::string& path) const;

    /* ---- Format helpers ---- */
    static std::string hex_line(const Bytes& data, size_t offset, size_t len);
    static std::string timestamp_str(Timestamp ts);

    /* ---- Main reader loop (one per channel) ---- */
    void reader_loop(const std::string& path, const std::string& ch_name)
    {
        int fd = open_port(path);
        if (fd < 0) return;

        Bytes packet;
        auto  last_rx = std::chrono::steady_clock::now();
        std::array<uint8_t, 4096> buf{};

        while (running_) {
            auto now  = std::chrono::steady_clock::now();
            auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - last_rx);

            /* Flush accumulated packet on idle gap */
            if (!packet.empty() && idle >= cfg_.packet_gap) {
                dispatch(ch_name, packet, last_rx);
                packet.clear();
            }

            /* Non-blocking read (select with 1 ms timeout) */
            fd_set rfds;
            FD_ZERO(&rfds); FD_SET(fd, &rfds);
            struct timeval tv{0, 1000};
            if (select(fd + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue;

            ssize_t n = ::read(fd, buf.data(), buf.size());
            if (n <= 0) continue;

            last_rx = std::chrono::steady_clock::now();
            packet.insert(packet.end(), buf.data(), buf.data() + n);
        }

        /* Final flush */
        if (!packet.empty()) dispatch(ch_name, packet, last_rx);
        ::close(fd);
    }

    void dispatch(const std::string& ch, const Bytes& pkt, Timestamp ts)
    {
        if (cfg_.hex_dump) print_hex(ch, pkt, ts);
        if (on_packet_)    on_packet_(ch, pkt, ts);
    }

    void print_hex(const std::string& ch, const Bytes& data, Timestamp ts)
    {
        std::lock_guard<std::mutex> lk(print_mtx_);
        printf("[%s] %s  %zu bytes\n",
               timestamp_str(ts).c_str(), ch.c_str(), data.size());
        for (size_t off = 0; off < data.size(); off += 16) {
            size_t row = std::min<size_t>(16, data.size() - off);
            printf("  %04zx  ", off);
            for (size_t i = 0; i < 16; i++) {
                if (i < row) printf("%02x ", data[off + i]);
                else         printf("   ");
                if (i == 7)  printf(" ");
            }
            printf(" |");
            for (size_t i = 0; i < row; i++) {
                uint8_t c = data[off + i];
                printf("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
            }
            printf("|\n");
        }
        fflush(stdout);
    }

    SniffConfig          cfg_;
    PacketCb             on_packet_;
    std::atomic<bool>    running_;
    std::thread          thr_a_, thr_b_;
    std::mutex           print_mtx_;
};

} // namespace uart

/* ------------------------------------------------------------------ */
/* Usage example (main.cpp)                                            */
/* ------------------------------------------------------------------ */
/*
int main()
{
    uart::SniffConfig cfg;
    cfg.port_a    = "/dev/ttyUSB0";
    cfg.port_b    = "/dev/ttyUSB1";
    cfg.baud_rate = 115200;
    cfg.packet_gap = std::chrono::milliseconds(10);

    uart::UartSniffer sniffer(cfg, [](const std::string& ch,
                                      const uart::Bytes& data,
                                      uart::Timestamp ts) {
        // Custom packet handler — e.g. protocol parser
        printf("Packet on %s: %zu bytes\n", ch.c_str(), data.size());
    });

    sniffer.start();
    std::this_thread::sleep_for(std::chrono::minutes(10));
    sniffer.stop();
}
*/
```

---

## Programming in Rust

### Cross-Platform Serial Port Sniffer

```toml
# Cargo.toml
[package]
name    = "uart-sniffer"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4"
chrono     = { version = "0.4", features = ["std"] }
clap       = { version = "4", features = ["derive"] }
hex        = "0.4"
```

```rust
// src/main.rs — Cross-platform passive UART sniffer
//
// Run: cargo run -- --port-a /dev/ttyUSB0 --port-b /dev/ttyUSB1 --baud 115200

use chrono::Local;
use clap::Parser;
use serialport::{SerialPort, DataBits, Parity, StopBits};
use std::{
    io::{self, Read},
    sync::{Arc, Mutex},
    thread,
    time::Duration,
};

/// Passive dual-channel UART sniffer
#[derive(Parser, Debug)]
#[command(about = "Passive dual-channel UART sniffer")]
struct Args {
    #[arg(long)] port_a: String,
    #[arg(long)] port_b: String,
    #[arg(long, default_value = "115200")] baud: u32,
}

/// A single sniff record
struct Record {
    channel:   &'static str,
    timestamp: String,
    data:      Vec<u8>,
}

impl Record {
    fn hex_dump(&self) {
        println!("[{}] {}  {} bytes", self.timestamp, self.channel, self.data.len());
        for (offset, chunk) in self.data.chunks(16).enumerate() {
            let base = offset * 16;
            let hex_part: String = chunk
                .iter()
                .enumerate()
                .map(|(i, b)| {
                    if i == 8 { format!(" {:02x} ", b) }
                    else      { format!("{:02x} ", b)  }
                })
                .collect();
            let ascii_part: String = chunk
                .iter()
                .map(|&b| if b.is_ascii_graphic() || b == b' ' { b as char } else { '.' })
                .collect();
            println!("  {:04x}  {:<49} |{}|", base, hex_part, ascii_part);
        }
    }
}

/// Open a serial port in read-only mode
fn open_port(path: &str, baud: u32) -> Box<dyn SerialPort> {
    serialport::new(path, baud)
        .data_bits(DataBits::Eight)
        .parity(Parity::None)
        .stop_bits(StopBits::One)
        .timeout(Duration::from_millis(100))
        .open()
        .unwrap_or_else(|e| panic!("Failed to open {path}: {e}"))
}

/// Spawn a reader thread for one channel
fn spawn_reader(
    mut port: Box<dyn SerialPort>,
    channel: &'static str,
    log: Arc<Mutex<Vec<Record>>>,
) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        let mut buf = [0u8; 4096];
        loop {
            match port.read(&mut buf) {
                Ok(0) => continue,
                Ok(n) => {
                    let now = Local::now().format("%H:%M:%S%.6f").to_string();
                    let record = Record {
                        channel,
                        timestamp: now,
                        data: buf[..n].to_vec(),
                    };
                    record.hex_dump();          // print immediately
                    log.lock().unwrap().push(record);
                }
                Err(ref e) if e.kind() == io::ErrorKind::TimedOut => continue,
                Err(e) => {
                    eprintln!("[{}] Read error: {}", channel, e);
                    break;
                }
            }
        }
    })
}

fn main() {
    let args = Args::parse();

    println!("[INFO] UART Sniffer");
    println!("[INFO] Port A ({}) → channel A->B @ {} baud", args.port_a, args.baud);
    println!("[INFO] Port B ({}) → channel B->A @ {} baud", args.port_b, args.baud);

    let port_a = open_port(&args.port_a, args.baud);
    let port_b = open_port(&args.port_b, args.baud);

    let log: Arc<Mutex<Vec<Record>>> = Arc::new(Mutex::new(Vec::new()));

    let h_a = spawn_reader(port_a, "A->B", Arc::clone(&log));
    let h_b = spawn_reader(port_b, "B->A", Arc::clone(&log));

    h_a.join().ok();
    h_b.join().ok();
}
```

---

### Async Dual-Channel Sniffer with tokio

```toml
# Cargo.toml (async variant)
[dependencies]
serialport  = "4"
tokio       = { version = "1", features = ["full"] }
chrono      = "0.4"
tokio-util  = "0.7"
bytes       = "1"
```

```rust
// src/async_sniffer.rs — tokio-based async UART sniffer
//
// Reads from two serial ports concurrently using tokio tasks.

use chrono::Local;
use serialport::SerialPort;
use std::{io::Read, sync::Arc, time::Duration};
use tokio::sync::mpsc;
use tokio::task;

#[derive(Debug)]
struct Frame {
    channel: &'static str,
    time:    String,
    data:    Vec<u8>,
}

/// Blocking read loop, run inside `spawn_blocking`
fn blocking_read_loop(
    mut port: Box<dyn SerialPort>,
    channel: &'static str,
    tx: mpsc::UnboundedSender<Frame>,
) {
    let mut buf = [0u8; 1024];
    loop {
        match port.read(&mut buf) {
            Ok(0) => continue,
            Ok(n) => {
                let frame = Frame {
                    channel,
                    time: Local::now().format("%H:%M:%S%.6f").to_string(),
                    data: buf[..n].to_vec(),
                };
                if tx.send(frame).is_err() {
                    break;  // receiver dropped — shut down
                }
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
            Err(e) => {
                eprintln!("[{}] Error: {}", channel, e);
                break;
            }
        }
    }
}

#[tokio::main]
async fn main() {
    let baud = 115200u32;

    let port_a = serialport::new("/dev/ttyUSB0", baud)
        .timeout(Duration::from_millis(50))
        .open()
        .expect("Cannot open port A");

    let port_b = serialport::new("/dev/ttyUSB1", baud)
        .timeout(Duration::from_millis(50))
        .open()
        .expect("Cannot open port B");

    let (tx, mut rx) = mpsc::unbounded_channel::<Frame>();

    let tx_a = tx.clone();
    let tx_b = tx;

    // Run blocking I/O in dedicated thread pool slots
    task::spawn_blocking(move || blocking_read_loop(port_a, "A->B", tx_a));
    task::spawn_blocking(move || blocking_read_loop(port_b, "B->A", tx_b));

    println!("[INFO] Async UART sniffer running...");

    // Process all incoming frames in a single async task
    while let Some(frame) = rx.recv().await {
        print_frame(&frame);
    }
}

fn print_frame(f: &Frame) {
    println!("[{}] {}  {} bytes", f.time, f.channel, f.data.len());
    for (i, chunk) in f.data.chunks(16).enumerate() {
        let hex: String = chunk.iter()
            .map(|b| format!("{:02x} ", b))
            .collect();
        let ascii: String = chunk.iter()
            .map(|&b| if b.is_ascii_graphic() { b as char } else { '.' })
            .collect();
        println!("  {:04x}  {:<48} |{}|", i * 16, hex, ascii);
    }
}
```

---

### Protocol Decoder with State Machine

A reusable Rust state machine that parses a simple framed protocol on top of the raw UART stream:

```rust
// src/decoder.rs — Packet protocol decoder
//
// Assumes a simple framing: [0xAA][0x55][LEN:1][PAYLOAD:LEN][CRC:1]
// Recognizes framing errors, CRC failures, and valid packets.

#[derive(Debug, Clone, PartialEq)]
pub enum DecodeResult {
    NeedMore,
    Packet(Vec<u8>),
    FramingError { dropped: usize },
    CrcError { expected: u8, got: u8 },
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum State {
    HuntSync1,
    HuntSync2,
    ReadLen,
    ReadPayload,
    ReadCrc,
}

pub struct FrameDecoder {
    state:       State,
    payload_buf: Vec<u8>,
    expected_len: u8,
    hunt_pos:    usize,   /* bytes skipped during sync hunt */
}

const SYNC1: u8 = 0xAA;
const SYNC2: u8 = 0x55;

impl FrameDecoder {
    pub fn new() -> Self {
        Self {
            state:        State::HuntSync1,
            payload_buf:  Vec::with_capacity(256),
            expected_len: 0,
            hunt_pos:     0,
        }
    }

    /// Feed one byte and return a decode result.
    pub fn feed(&mut self, byte: u8) -> DecodeResult {
        match self.state {
            State::HuntSync1 => {
                if byte == SYNC1 {
                    self.state    = State::HuntSync2;
                    self.hunt_pos = 0;
                } else {
                    self.hunt_pos += 1;
                }
                DecodeResult::NeedMore
            }

            State::HuntSync2 => {
                if byte == SYNC2 {
                    self.state = State::ReadLen;
                    DecodeResult::NeedMore
                } else if byte == SYNC1 {
                    // Could be start of a new frame
                    DecodeResult::NeedMore
                } else {
                    let dropped = self.hunt_pos + 2;
                    self.state   = State::HuntSync1;
                    self.hunt_pos = 0;
                    DecodeResult::FramingError { dropped }
                }
            }

            State::ReadLen => {
                self.expected_len = byte;
                self.payload_buf.clear();
                self.state = if byte == 0 {
                    State::ReadCrc
                } else {
                    State::ReadPayload
                };
                DecodeResult::NeedMore
            }

            State::ReadPayload => {
                self.payload_buf.push(byte);
                if self.payload_buf.len() >= self.expected_len as usize {
                    self.state = State::ReadCrc;
                }
                DecodeResult::NeedMore
            }

            State::ReadCrc => {
                let expected_crc = self.calc_crc();
                self.state = State::HuntSync1;
                if byte == expected_crc {
                    DecodeResult::Packet(self.payload_buf.clone())
                } else {
                    DecodeResult::CrcError { expected: expected_crc, got: byte }
                }
            }
        }
    }

    /// Simple XOR checksum over all payload bytes
    fn calc_crc(&self) -> u8 {
        self.payload_buf.iter().fold(0u8, |acc, &b| acc ^ b)
    }

    /// Feed a slice of bytes and collect results
    pub fn feed_slice(&mut self, data: &[u8]) -> Vec<DecodeResult> {
        data.iter()
            .filter_map(|&b| {
                let r = self.feed(b);
                if r == DecodeResult::NeedMore { None } else { Some(r) }
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_frame(payload: &[u8]) -> Vec<u8> {
        let crc = payload.iter().fold(0u8, |a, &b| a ^ b);
        let mut frame = vec![SYNC1, SYNC2, payload.len() as u8];
        frame.extend_from_slice(payload);
        frame.push(crc);
        frame
    }

    #[test]
    fn test_valid_packet() {
        let mut dec = FrameDecoder::new();
        let frame = make_frame(&[0x01, 0x02, 0x03]);
        let results = dec.feed_slice(&frame);
        assert_eq!(results.len(), 1);
        assert_eq!(results[0], DecodeResult::Packet(vec![0x01, 0x02, 0x03]));
    }

    #[test]
    fn test_crc_error() {
        let mut dec = FrameDecoder::new();
        let mut frame = make_frame(&[0xDE, 0xAD]);
        *frame.last_mut().unwrap() ^= 0xFF; // corrupt CRC
        let results = dec.feed_slice(&frame);
        assert!(matches!(results[0], DecodeResult::CrcError { .. }));
    }

    #[test]
    fn test_junk_before_sync() {
        let mut dec = FrameDecoder::new();
        let mut data = vec![0x00, 0xFF, 0x11]; // junk
        data.extend(make_frame(&[0xAB]));
        let results = dec.feed_slice(&data);
        assert_eq!(results.len(), 1);
        assert_eq!(results[0], DecodeResult::Packet(vec![0xAB]));
    }
}
```

---

## Decoding and Analysing Captured Data

### Offline Analysis Script (Python/Rust post-processing)

After capturing raw binary logs, common analysis tasks include:

1. **Baud-rate detection** — measure the shortest pulse width in a logic capture; its reciprocal is the baud rate.
2. **Packet boundary detection** — idle gaps in the timestamp stream indicate packet separators.
3. **Pattern search** — search for known magic bytes, command codes, or ASCII strings.
4. **Frequency analysis** — most-common byte values reveal encoding (ASCII text has many printable values; binary protocols may have uniform distribution).

```python
#!/usr/bin/env python3
# analyse.py — post-process a raw binary sniffer log

import sys
from collections import Counter

def hex_dump(data: bytes, channel: str, offset: int = 0) -> None:
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_part  = ' '.join(f'{b:02x}' for b in chunk)
        ascii_part = ''.join(chr(b) if 0x20 <= b < 0x7f else '.' for b in chunk)
        print(f'[{channel}] {offset+i:04x}  {hex_part:<48}  |{ascii_part}|')

def analyse(path: str) -> None:
    with open(path, 'rb') as f:
        data = f.read()

    print(f'File: {path}  ({len(data)} bytes)\n')
    hex_dump(data, 'RAW')

    counts = Counter(data)
    print('\nTop 10 most frequent bytes:')
    for byte, count in counts.most_common(10):
        bar = '█' * (count * 40 // len(data))
        print(f'  0x{byte:02x} ({chr(byte) if 0x20 <= byte < 0x7f else " "}) '
              f'{count:5d}  {bar}')

    # Search for ASCII strings > 4 chars
    print('\nPrintable strings (len >= 4):')
    current = []
    for b in data:
        if 0x20 <= b < 0x7f:
            current.append(chr(b))
        else:
            if len(current) >= 4:
                print('  ' + ''.join(current))
            current = []

if __name__ == '__main__':
    analyse(sys.argv[1] if len(sys.argv) > 1 else 'channel_a.bin')
```

---

## Common Pitfalls and Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| All bytes are `0xFF` | Voltage mismatch — sniffer input floating or over-voltage | Check voltage levels; add pull-down or level shifter |
| Garbled / wrong characters | Wrong baud rate | Use oscilloscope to measure bit width; auto-detect baud |
| Missing first byte | No pull-up on idle line | Ensure TX line rests HIGH before first byte |
| Framing errors on every frame | Wrong data bits / stop bits / parity | Verify 8N1 or query device datasheet |
| Intermittent data loss | Buffer overflow on sniffer | Increase ring/read buffer; use DMA on MCU |
| Break condition confused with 0x00 | Break is >1 frame of LOW | Check for extended LOW level (break detection) |
| One channel always silent | Crossed RX wires | Swap the two RX inputs |
| Timestamps drift | System clock skew on dual USB adapters | Use hardware-timestamped logic analyser for precise timing |

---

## Security and Ethical Considerations

UART sniffing is a powerful technique and must be applied responsibly:

- **Only sniff hardware you own or have explicit written permission to analyse.** Sniffing third-party production hardware in a commercial context may violate terms of service or local law.
- **Captured data may contain sensitive information** — passwords, session tokens, encryption keys, personally identifiable information, or health data. Handle logs securely and apply need-to-know access.
- **Do not broadcast or publish captured data** from devices you do not own without appropriate disclosure.
- **Industrial / safety-critical systems** — never connect to active safety-critical UART buses (medical devices, automotive CAN gateways, industrial PLCs) without taking the system offline first. A miswired probe may inject noise and cause dangerous behaviour.
- **Responsible disclosure** — if you discover a security vulnerability via UART sniffing, follow coordinated disclosure guidelines before publishing.

---

## Summary

UART Sniffing is the practice of **passively monitoring UART bus traffic** to capture, decode, and analyse serial data exchanged between two devices. The core workflow consists of four phases:

1. **Hardware setup** — tap the TX lines of both devices, match voltage levels (3.3 V / 5 V / RS-232), and connect two independent receive inputs to the sniffer platform (USB-UART adapter, MCU, or logic analyser).

2. **Port configuration** — open the serial port(s) in read-only, raw mode with matching baud rate, data bits, parity, and stop bits. Mismatching any parameter produces garbage or silence.

3. **Capture and buffering** — use interrupt-driven or DMA reception with ring buffers to ensure no bytes are lost. On a PC, `select()`/`epoll` or tokio tasks prevent blocking. On a microcontroller, ISR-based ring buffers and periodic flushing are the standard approach.

4. **Decoding and analysis** — apply idle-gap heuristics to segment the byte stream into logical packets, then use a state machine or grammar-based parser to decode protocol framing. Post-process binary logs offline for frequency analysis, string extraction, and CRC validation.

**Language comparison:**

| Aspect | C/C++ | Rust |
|--------|-------|------|
| Control | Maximum (direct register access, zero-copy DMA) | High (unsafe blocks allow the same) |
| Safety | Manual memory management; risk of buffer overflows | Ownership model prevents data races and overflows at compile time |
| Portability | termios (POSIX) or CMSIS/HAL for MCUs | `serialport` crate works on Linux, macOS, and Windows |
| Async support | pthreads or RTOS tasks | tokio / async-std for concurrent multi-port reading |
| Recommended for | Bare-metal MCU firmware, latency-critical capture | PC-side tools, long-running loggers, protocol analysis |

UART sniffing is an indispensable skill in embedded systems development, hardware security research, and production debugging — providing transparent visibility into the serial communication layer of virtually any embedded device.