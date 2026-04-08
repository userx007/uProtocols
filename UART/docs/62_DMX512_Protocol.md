# 62. DMX512 Protocol

**Protocol fundamentals** — RS-485 physical layer, precise timing parameters (BREAK ≥88 µs, MAB ≥8 µs, 250 000 bps 8N2), packet anatomy with start codes, and why standard UART alone can't generate a valid DMX stream.

**C/C++ implementations:**
- **Linux transmitter** — `termios` at 250 kbps with `TIOCSBRK`/`TIOCCBRK` ioctls for BREAK generation
- **Linux receiver** — `PARMRK` mode to decode the `0xFF 0x00 0x00` BREAK signature from the kernel driver
- **STM32 transmitter** — GPIO bit-bang BREAK + HAL DMA packet send + RS-485 DE pin control
- **STM32 receiver** — UART framing-error ISR for BREAK detection, double-buffered universe
- **C++ class** — Thread-safe `DMX512Controller` with background 44 Hz transmit thread and fixture channel-block mapping

**Rust implementations:**
- **Linux transmitter** — `serialport` crate + raw `libc` ioctls for BREAK, proper `thiserror` error types
- **Embedded `no_std`** — `embedded-hal` trait generics separating logic from hardware, works on STM32/ESP32/etc.
- **`DmxManager`** — `Arc<Mutex<DmxUniverse>>` shared state with blocking linear fades and fixture address mapping

**Also included:** RDM (E1.20) frame construction in C, a debugging pitfalls table (baud rate, termination, stop bits, refresh rate), and recommended tooling.

## Stage Lighting Control Protocol Using Modified UART Timing

---

## Table of Contents

1. [Introduction](#introduction)
2. [Historical Background](#historical-background)
3. [Physical Layer](#physical-layer)
4. [Protocol Timing & Frame Structure](#protocol-timing--frame-structure)
5. [DMX512 Packet Anatomy](#dmx512-packet-anatomy)
6. [UART Configuration for DMX512](#uart-configuration-for-dmx512)
7. [DMX512 Universe & Addressing](#dmx512-universe--addressing)
8. [Programming in C/C++](#programming-in-cc)
    - [Linux UART Transmitter (C)](#linux-uart-transmitter-c)
    - [Linux UART Receiver (C)](#linux-uart-receiver-c)
    - [Embedded STM32 Transmitter (C)](#embedded-stm32-transmitter-c)
    - [Embedded STM32 Receiver (C)](#embedded-stm32-receiver-c)
    - [C++ Object-Oriented DMX Controller](#c-object-oriented-dmx-controller)
9. [Programming in Rust](#programming-in-rust)
    - [Rust Linux UART Transmitter](#rust-linux-uart-transmitter)
    - [Rust Embedded (no_std) Transmitter](#rust-embedded-nostd-transmitter)
    - [Rust DMX Universe Manager](#rust-dmx-universe-manager)
10. [RDM – Remote Device Management](#rdm--remote-device-management)
11. [Common Pitfalls & Debugging](#common-pitfalls--debugging)
12. [Summary](#summary)

---

## Introduction

**DMX512** (Digital Multiplex with 512 addresses) is a serial communication protocol
standardized as **ANSI E1.11** and widely used in professional entertainment lighting,
architectural illumination, stage effects, and AV control systems. Originally developed
by the **United States Institute for Theatre Technology (USITT)** in 1986, it provides a
deterministic, unidirectional broadcast bus that can carry **512 channels** of 8-bit
intensity (or parameter) data at a fixed, fast refresh rate.

The protocol rides on top of **RS-485 differential signaling** and uses a **modified
UART framing** — standard 8N2 (8 data bits, no parity, 2 stop bits) — but with a
non-standard reset sequence that cannot be produced directly from a typical UART without
explicit GPIO manipulation of the transmit enable/break line.

DMX512 is the universal language of stage lighting: virtually every dimmer pack,
moving head, LED fixture, fog machine, and pixel controller in live entertainment speaks
it.

---

## Historical Background

| Year | Milestone |
|------|-----------|
| 1986 | USITT DMX512 v1.0 — first standard |
| 1990 | DMX512/1990 revision — clarified electrical specs |
| 1998 | EIA-485 mandated as physical layer |
| 2004 | ANSI E1.11 (DMX512-A) — current standard, adds RDM hooks |
| 2006 | ANSI E1.20 RDM — bidirectional extension for remote device management |
| 2010s | Art-Net & sACN (E1.31) — DMX over Ethernet / IP |

---

## Physical Layer

DMX512 uses **EIA-485 (RS-485)** differential signaling:

```
Controller                           Fixtures
  ┌────────┐   XLR 5-pin (or 3-pin)   ┌────────┐   ┌────────┐
  │   TX+  ├──────────────────────────┤  RX+   │   │  RX+   │
  │   TX-  ├──────────────────────────┤  RX-   │   │  RX-   │
  │   GND  ├──────────────────────────┤  GND   │   │  GND   │
  └────────┘                          └────────┘   └────────┘
                                      ←── daisy chain ──────→
                                                   [120 Ω terminator]
```

**Key electrical characteristics:**

| Parameter | Value |
|-----------|-------|
| Signaling | Differential (RS-485) |
| Logic HIGH (Mark) | +2 V to +6 V (A > B) |
| Logic LOW (Space/Break) | −6 V to −2 V (B > A) |
| Baud rate | **250 000 bps** (exactly) |
| Max cable length | 300 m per segment |
| Max devices | 32 unit loads per segment (extenders available) |
| Termination | 120 Ω at far end |
| Connector | XLR-5 (pins 3+/4−, pin 1 GND); XLR-3 also common |

> **Critical:** DMX512 is **unidirectional** from controller to fixtures. RS-485 is
> half-duplex capable, but standard DMX uses it in transmit-only mode. RDM extends this
> with a request/response mechanism.

---

## Protocol Timing & Frame Structure

DMX512 departs from ordinary UART by prepending a **BREAK** + **MAB** sequence before
each packet. These cannot be generated by a standard UART TX line alone — they require
either direct GPIO control or a UART peripheral that supports line break signaling.

```
  ┌─────────────┬──────┬──────────────────────────────────────┐
  │    BREAK    │ MAB  │         DMX Packet (slots 0–512)     │
  └─────────────┴──────┴──────────────────────────────────────┘
   ≥ 88 µs LOW   ≥ 8 µs   up to 513 × 44 µs each
```

### Timing Parameters (ANSI E1.11)

| Symbol | Name | Min | Typical | Max |
|--------|------|-----|---------|-----|
| BREAK | Break duration | 88 µs | 176 µs | 1 s |
| MAB | Mark After Break | 8 µs | 16 µs | 1 s |
| MTBP | Mark Time Between Packets | 0 µs | 0 µs | 1 s |
| Slot rate | Bit time at 250 kbps | — | 4 µs | — |
| Slot frame | 11 bits (1 start + 8 data + 2 stop) | — | 44 µs | — |
| Full packet | BREAK + MAB + 513 slots | — | ~22.7 ms | — |
| Refresh rate | Full 512-ch universe | — | ~44 Hz | — |

### Single Slot Framing (UART 8N2)

```
  Idle  Start  D0  D1  D2  D3  D4  D5  D6  D7  Stop Stop  Idle
  HIGH   LOW   ←──────── 8 data bits ──────────→  HIGH HIGH  HIGH
         4µs   4µs each                            4µs  4µs
```

This is standard **8N2** framing (8 data bits, No parity, 2 stop bits), giving
**11 bit-times = 44 µs** per slot.

---

## DMX512 Packet Anatomy

```
Byte 0 (Slot 0):  Start Code
Bytes 1–512:      Channel data (channel 1 = byte 1 ... channel 512 = byte 512)

Start Code Values:
  0x00  →  Standard dimmer/intensity data (most common)
  0xCC  →  RDM alternate start code (with BREAK+MAB difference)
  0x17  →  ASCII text (test/label)
  0xCF  →  System information packet (SIP)
```

The **Start Code 0x00** is the normal lighting data packet. Fixtures ignore any start
code they don't understand, so multiple protocols can coexist on one universe.

---

## UART Configuration for DMX512

To transmit DMX512 from a UART peripheral, you need:

1. **Baud rate:** 250 000 bps (some hardware cannot divide to this exactly; verify)
2. **Data bits:** 8
3. **Parity:** None
4. **Stop bits:** 2
5. **Break generation:** The UART line must be held LOW for ≥ 88 µs before each packet.
   This is done by one of:
   - Sending a `0x00` byte at a **lower baud rate** (e.g., 100 000 bps → 100 µs LOW)
   - Temporarily switching the TX pin to GPIO output, pulling it LOW, then restoring
   - Using a hardware UART break-send feature (STM32, ESP32, etc.)

---

## Programming in C/C++

### Linux UART Transmitter (C)

This example targets a Linux system with an RS-485 USB dongle (e.g., `/dev/ttyUSB0`)
or a hardware UART exposed as a serial device. The BREAK is generated by temporarily
setting a very low baud rate.

```c
/* dmx512_tx_linux.c
 * DMX512 Transmitter for Linux using termios
 * Build: gcc -O2 -Wall -o dmx512_tx dmx512_tx_linux.c
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/serial.h>
#include <time.h>

#define DMX_CHANNELS        512
#define DMX_START_CODE      0x00

/* RS-485 direction control (optional, for hardware with DE pin) */
/* #define RS485_CONTROL */

typedef struct {
    int      fd;
    uint8_t  universe[DMX_CHANNELS + 1]; /* [0] = start code, [1..512] = channels */
} DMX_t;

/* ── Helper: set custom baud rate via serial_struct ────────────────────────── */
static int set_custom_baud(int fd, int baud)
{
    struct serial_struct ss;
    if (ioctl(fd, TIOCGSERIAL, &ss) < 0) return -1;
    ss.flags       = (ss.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
    ss.custom_divisor = (ss.baud_base + (baud / 2)) / baud;
    return ioctl(fd, TIOCSSERIAL, &ss);
}

/* ── Open and configure the serial port at 250 000 baud, 8N2 ───────────────── */
int dmx_open(DMX_t *dmx, const char *port)
{
    struct termios tty;

    dmx->fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (dmx->fd < 0) {
        perror("open");
        return -1;
    }

    if (tcgetattr(dmx->fd, &tty) != 0) {
        perror("tcgetattr");
        return -1;
    }

    /* Raw mode */
    cfmakeraw(&tty);

    /* 250 000 baud — try B250000 first (kernel 2.6.32+), fall back to custom */
#ifdef B250000
    cfsetispeed(&tty, B250000);
    cfsetospeed(&tty, B250000);
#else
    cfsetispeed(&tty, B38400);
    cfsetospeed(&tty, B38400);
    set_custom_baud(dmx->fd, 250000);
#endif

    /* 8 data bits */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    /* No parity */
    tty.c_cflag &= ~PARENB;

    /* 2 stop bits */
    tty.c_cflag |= CSTOPB;

    /* No hardware flow control */
    tty.c_cflag &= ~CRTSCTS;

    /* Enable receiver, local mode */
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(dmx->fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return -1;
    }

    /* Initialize universe: start code 0x00, all channels to 0 */
    memset(dmx->universe, 0, sizeof(dmx->universe));
    dmx->universe[0] = DMX_START_CODE;

    return 0;
}

/* ── Set a single channel (1-indexed) ─────────────────────────────────────── */
void dmx_set_channel(DMX_t *dmx, int channel, uint8_t value)
{
    if (channel >= 1 && channel <= DMX_CHANNELS)
        dmx->universe[channel] = value;
}

/* ── Send a BREAK by toggling serial_break ─────────────────────────────────── */
static void send_break(int fd)
{
    /* tcsendbreak() sends a break for 0.25–0.50 s on many systems — too long.
     * Use TIOCSBRK/TIOCCBRK for precise control.                             */
    ioctl(fd, TIOCSBRK, 0);          /* Assert BREAK (drive line LOW)        */

    /* Sleep ≥ 88 µs. Use 176 µs for safety. */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 176000 };
    nanosleep(&ts, NULL);

    ioctl(fd, TIOCCBRK, 0);          /* De-assert BREAK                      */

    /* MAB: Mark After Break ≥ 8 µs. Drive line HIGH (idle) for ~16 µs.      */
    ts.tv_nsec = 16000;
    nanosleep(&ts, NULL);
}

/* ── Transmit one complete DMX packet ─────────────────────────────────────── */
int dmx_send(DMX_t *dmx)
{
    send_break(dmx->fd);

    /* Write start code + 512 channel bytes = 513 bytes total */
    ssize_t written = write(dmx->fd, dmx->universe, DMX_CHANNELS + 1);
    if (written != DMX_CHANNELS + 1) {
        perror("write");
        return -1;
    }
    tcdrain(dmx->fd); /* Wait until all bytes are physically transmitted */
    return 0;
}

void dmx_close(DMX_t *dmx)
{
    if (dmx->fd >= 0) close(dmx->fd);
}

/* ── Demo: fade channel 1 up and down ─────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *port = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    DMX_t dmx = {0};

    if (dmx_open(&dmx, port) != 0) {
        fprintf(stderr, "Failed to open DMX port %s\n", port);
        return 1;
    }

    printf("DMX512 transmitting on %s — Ctrl+C to stop\n", port);

    uint8_t level = 0;
    int     direction = 1;

    for (;;) {
        dmx_set_channel(&dmx, 1, level);   /* Master intensity on ch 1 */
        dmx_set_channel(&dmx, 2, 255);     /* Red   full */
        dmx_set_channel(&dmx, 3, 0);       /* Green off  */
        dmx_set_channel(&dmx, 4, level);   /* Blue  follows master */

        if (dmx_send(&dmx) != 0) break;

        /* Update level for next frame */
        level = (uint8_t)(level + direction);
        if (level == 255) direction = -1;
        if (level == 0)   direction =  1;

        /* ~44 Hz refresh: 22.7 ms per packet — no extra sleep needed when
         * tcdrain() absorbs the transmission time.                         */
    }

    dmx_close(&dmx);
    return 0;
}
```

---

### Linux UART Receiver (C)

Receiving DMX512 is more complex: the kernel UART driver typically cannot signal the
BREAK condition to userspace reliably. The standard approach uses `PARMRK` + `IGNPAR`
off, which encodes a BREAK as the 3-byte sequence `\xFF \x00 \x00`.

```c
/* dmx512_rx_linux.c
 * DMX512 Receiver for Linux
 * Build: gcc -O2 -Wall -o dmx512_rx dmx512_rx_linux.c
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#define DMX_CHANNELS  512

typedef enum {
    RX_WAIT_BREAK,
    RX_WAIT_MAB,
    RX_READ_START_CODE,
    RX_READ_DATA
} DMX_RxState;

typedef struct {
    int          fd;
    DMX_RxState  state;
    int          channel_idx;
    uint8_t      universe[DMX_CHANNELS];
    uint8_t      start_code;
    int          break_detect; /* Pending break marker byte count */
} DMX_Rx_t;

int dmx_rx_open(DMX_Rx_t *rx, const char *port)
{
    struct termios tty;

    rx->fd = open(port, O_RDONLY | O_NOCTTY);
    if (rx->fd < 0) { perror("open"); return -1; }

    tcgetattr(rx->fd, &tty);
    cfmakeraw(&tty);

#ifdef B250000
    cfsetispeed(&tty, B250000);
#else
    cfsetispeed(&tty, B38400);
    /* set_custom_baud(rx->fd, 250000); */
#endif

    tty.c_cflag &= ~CSIZE; tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag |= CSTOPB;
    tty.c_cflag |= CREAD | CLOCAL;

    /* Enable parity-mark mode to detect BREAK as \xFF\x00\x00 */
    tty.c_iflag |= PARMRK;
    tty.c_iflag &= ~IGNPAR;

    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(rx->fd, TCSANOW, &tty);

    rx->state       = RX_WAIT_BREAK;
    rx->channel_idx = 0;
    rx->break_detect = 0;
    memset(rx->universe, 0, sizeof(rx->universe));
    return 0;
}

/* Returns 1 when a complete universe has been received */
int dmx_rx_process(DMX_Rx_t *rx)
{
    uint8_t byte;
    ssize_t n = read(rx->fd, &byte, 1);
    if (n <= 0) return 0;

    /* BREAK detection: kernel encodes break as 0xFF 0x00 0x00 (PARMRK) */
    if (rx->break_detect == 1) {
        if (byte == 0x00) {
            rx->break_detect = 2;
            return 0;
        }
        rx->break_detect = 0;
    }
    if (rx->break_detect == 2) {
        if (byte == 0x00) {
            /* BREAK confirmed! */
            rx->state        = RX_READ_START_CODE;
            rx->channel_idx  = 0;
            rx->break_detect = 0;
            return 0;
        }
        rx->break_detect = 0;
    }
    if (byte == 0xFF) {
        rx->break_detect = 1;
        return 0;
    }

    switch (rx->state) {
        case RX_WAIT_BREAK:
            break; /* Keep waiting */

        case RX_READ_START_CODE:
            rx->start_code = byte;
            if (byte == 0x00)
                rx->state = RX_READ_DATA;
            else
                rx->state = RX_WAIT_BREAK; /* Unknown start code — ignore */
            break;

        case RX_READ_DATA:
            if (rx->channel_idx < DMX_CHANNELS) {
                rx->universe[rx->channel_idx++] = byte;
            }
            if (rx->channel_idx >= DMX_CHANNELS) {
                rx->state = RX_WAIT_BREAK;
                return 1; /* Full universe received */
            }
            break;

        default:
            break;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char *port = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    DMX_Rx_t rx = {0};

    if (dmx_rx_open(&rx, port) != 0) return 1;

    printf("DMX512 listening on %s...\n", port);

    uint64_t packet_count = 0;
    for (;;) {
        if (dmx_rx_process(&rx)) {
            packet_count++;
            /* Print first 6 channels every 100 packets */
            if (packet_count % 100 == 0) {
                printf("[%llu] Ch1-6: %3d %3d %3d %3d %3d %3d\n",
                       (unsigned long long)packet_count,
                       rx.universe[0], rx.universe[1], rx.universe[2],
                       rx.universe[3], rx.universe[4], rx.universe[5]);
            }
        }
    }

    close(rx.fd);
    return 0;
}
```

---

### Embedded STM32 Transmitter (C)

On an STM32 microcontroller the UART peripheral can generate BREAK natively via the
`SBK` (Send Break) bit, and the half-duplex RS-485 driver direction pin can be controlled
via a GPIO.

```c
/* dmx512_stm32_tx.c
 * DMX512 Transmitter for STM32 (HAL-based, e.g., STM32F4)
 *
 * Hardware connections:
 *   USART1 TX  → RS-485 driver DI
 *   PA9  (DE)  → RS-485 driver DE (transmit enable, active HIGH)
 *   USART1 configured: 250000 baud, 8N2, no parity
 */

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdint.h>

#define DMX_CHANNELS    512
#define DMX_DE_PORT     GPIOA
#define DMX_DE_PIN      GPIO_PIN_9

extern UART_HandleTypeDef huart1;

static uint8_t  dmx_buffer[DMX_CHANNELS + 1]; /* [0]=start code, [1..512]=data */
static volatile int tx_complete = 0;

/* ── Called from UART TX-complete ISR or callback ──────────────────────────── */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        tx_complete = 1;
        /* Disable RS-485 transmitter after last byte is shifted out */
        HAL_GPIO_WritePin(DMX_DE_PORT, DMX_DE_PIN, GPIO_PIN_RESET);
    }
}

/* ── Send BREAK via bit-banging the TX line ─────────────────────────────────── */
static void dmx_send_break(void)
{
    /* Reconfigure TX pin as GPIO output */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = GPIO_PIN_10; /* PA10 = USART1_TX */
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Assert BREAK (drive LOW) for 176 µs */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
    DWT_Delay_us(176);

    /* MAB: drive HIGH for 16 µs */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
    DWT_Delay_us(16);

    /* Restore TX pin to USART1 alternate function */
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* ── Initialize DMX buffer ──────────────────────────────────────────────────── */
void DMX_Init(void)
{
    memset(dmx_buffer, 0, sizeof(dmx_buffer));
    dmx_buffer[0] = 0x00; /* Standard dimmer start code */
    HAL_GPIO_WritePin(DMX_DE_PORT, DMX_DE_PIN, GPIO_PIN_RESET);
}

/* ── Set a channel value (channel 1..512) ───────────────────────────────────── */
void DMX_SetChannel(uint16_t channel, uint8_t value)
{
    if (channel >= 1 && channel <= DMX_CHANNELS)
        dmx_buffer[channel] = value;
}

/* ── Transmit one DMX universe packet ──────────────────────────────────────── */
void DMX_Transmit(void)
{
    /* 1. Enable RS-485 driver */
    HAL_GPIO_WritePin(DMX_DE_PORT, DMX_DE_PIN, GPIO_PIN_SET);

    /* 2. Send BREAK + MAB */
    dmx_send_break();

    /* 3. DMA-transmit the 513-byte packet */
    tx_complete = 0;
    HAL_UART_Transmit_DMA(&huart1, dmx_buffer, DMX_CHANNELS + 1);

    /* 4. Wait for completion (in a real system use a task/flag) */
    while (!tx_complete) { /* yield or poll */ }
}

/* ── Main loop example ──────────────────────────────────────────────────────── */
void DMX_Demo(void)
{
    DMX_Init();

    uint8_t level = 0;
    int8_t  dir   = 1;

    for (;;) {
        DMX_SetChannel(1, level);   /* Intensity */
        DMX_SetChannel(2, 255);     /* Red  */
        DMX_SetChannel(3, 0);       /* Green */
        DMX_SetChannel(4, level);   /* Blue */

        DMX_Transmit();

        /* Soft fade */
        level += dir;
        if (level == 255) dir = -1;
        if (level == 0)   dir =  1;

        HAL_Delay(1); /* Optional inter-packet gap */
    }
}
```

---

### Embedded STM32 Receiver (C)

The STM32 UART can detect a BREAK via the `LBD` (Line Break Detection) flag or via
framing errors on the incoming byte stream.

```c
/* dmx512_stm32_rx.c
 * DMX512 Receiver for STM32 using UART idle-line + DMA or byte-by-byte ISR
 *
 * Strategy: Use UART framing error (FE) detection.
 * A BREAK causes a framing error on the start-code byte (received as 0x00).
 * This signals the start of a new packet.
 */

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdint.h>

#define DMX_CHANNELS  512

extern UART_HandleTypeDef huart2;

typedef enum {
    DMX_RX_IDLE,
    DMX_RX_START_CODE,
    DMX_RX_DATA
} DMX_RxState_t;

static uint8_t       dmx_rx_buf[DMX_CHANNELS];
static uint8_t       dmx_shadow[DMX_CHANNELS]; /* Double-buffer */
static int           dmx_rx_idx     = 0;
static DMX_RxState_t dmx_rx_state   = DMX_RX_IDLE;
static volatile int  dmx_rx_ready   = 0;

/* ── ISR: called per received byte (byte-by-byte interrupt mode) ────────────── */
void USART2_IRQHandler(void)
{
    uint32_t sr   = huart2.Instance->SR;
    uint8_t  data = (uint8_t)(huart2.Instance->DR & 0xFF);

    if (sr & USART_SR_FE) {
        /*
         * Framing error = BREAK condition detected.
         * The data byte that caused it is always 0x00 and should be discarded.
         * Transition to expecting the start code.
         */
        if (dmx_rx_state == DMX_RX_DATA && dmx_rx_idx > 0) {
            /* Copy completed universe to shadow buffer */
            memcpy(dmx_shadow, dmx_rx_buf, DMX_CHANNELS);
            dmx_rx_ready = 1;
        }
        dmx_rx_state = DMX_RX_START_CODE;
        dmx_rx_idx   = 0;
        return;
    }

    switch (dmx_rx_state) {
        case DMX_RX_IDLE:
            break;

        case DMX_RX_START_CODE:
            if (data == 0x00) {
                /* Standard dimmer start code — receive data */
                dmx_rx_state = DMX_RX_DATA;
            } else {
                /* Unsupported start code — ignore this packet */
                dmx_rx_state = DMX_RX_IDLE;
            }
            break;

        case DMX_RX_DATA:
            if (dmx_rx_idx < DMX_CHANNELS) {
                dmx_rx_buf[dmx_rx_idx++] = data;
            }
            break;
    }
}

/* ── Call from main loop: returns 1 if a fresh universe is available ────────── */
int DMX_GetUniverse(uint8_t *out_buf)
{
    if (!dmx_rx_ready) return 0;
    dmx_rx_ready = 0;
    memcpy(out_buf, dmx_shadow, DMX_CHANNELS);
    return 1;
}
```

---

### C++ Object-Oriented DMX Controller

A reusable C++ class that wraps the Linux DMX transmitter with support for multiple
universes and per-fixture channel mapping.

```cpp
// dmx512_controller.hpp
#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

constexpr int DMX_CHANNELS = 512;

class DMX512Controller {
public:
    explicit DMX512Controller(const std::string &port, int refresh_hz = 44);
    ~DMX512Controller();

    // Non-copyable, movable
    DMX512Controller(const DMX512Controller &) = delete;
    DMX512Controller &operator=(const DMX512Controller &) = delete;

    bool open();
    void close();
    bool is_open() const { return fd_ >= 0; }

    // Set a single channel (1-indexed, 1..512)
    void set(int channel, uint8_t value);

    // Set a block of channels starting at 'start_channel'
    void set_block(int start_channel, const uint8_t *data, int count);

    // Read current value
    uint8_t get(int channel) const;

    // Blackout: all channels to 0
    void blackout();

    // Fade a channel from current to target over duration_ms milliseconds
    // (non-blocking: queues the fade and returns immediately)
    void fade(int channel, uint8_t target, int duration_ms);

    // Start/stop continuous background transmission thread
    void start_transmit();
    void stop_transmit();

    // One-shot synchronous send
    bool send_once();

private:
    std::string  port_;
    int          fd_          = -1;
    int          refresh_hz_;

    mutable std::mutex          universe_mutex_;
    std::array<uint8_t, DMX_CHANNELS + 1> universe_; // [0]=SC, [1..512]=data

    std::thread      tx_thread_;
    std::atomic<bool> running_{false};

    void send_break();
    bool configure_uart();
    void tx_loop();
};

// dmx512_controller.cpp
#include "dmx512_controller.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <stdexcept>

DMX512Controller::DMX512Controller(const std::string &port, int refresh_hz)
    : port_(port), refresh_hz_(refresh_hz)
{
    universe_.fill(0);
    universe_[0] = 0x00; // Start code
}

DMX512Controller::~DMX512Controller()
{
    stop_transmit();
    close();
}

bool DMX512Controller::configure_uart()
{
    struct termios tty{};
    if (tcgetattr(fd_, &tty) != 0) return false;

    cfmakeraw(&tty);
#ifdef B250000
    cfsetispeed(&tty, B250000);
    cfsetospeed(&tty, B250000);
#else
    cfsetispeed(&tty, B38400);
    cfsetospeed(&tty, B38400);
#endif
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag |= CSTOPB;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    return tcsetattr(fd_, TCSANOW, &tty) == 0;
}

bool DMX512Controller::open()
{
    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;
    if (!configure_uart()) { ::close(fd_); fd_ = -1; return false; }
    return true;
}

void DMX512Controller::close()
{
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

void DMX512Controller::set(int channel, uint8_t value)
{
    if (channel < 1 || channel > DMX_CHANNELS) return;
    std::lock_guard<std::mutex> lock(universe_mutex_);
    universe_[channel] = value;
}

void DMX512Controller::set_block(int start, const uint8_t *data, int count)
{
    if (start < 1) return;
    int end = std::min(start + count - 1, DMX_CHANNELS);
    std::lock_guard<std::mutex> lock(universe_mutex_);
    for (int ch = start; ch <= end; ++ch)
        universe_[ch] = data[ch - start];
}

uint8_t DMX512Controller::get(int channel) const
{
    if (channel < 1 || channel > DMX_CHANNELS) return 0;
    std::lock_guard<std::mutex> lock(universe_mutex_);
    return universe_[channel];
}

void DMX512Controller::blackout()
{
    std::lock_guard<std::mutex> lock(universe_mutex_);
    for (int i = 1; i <= DMX_CHANNELS; ++i) universe_[i] = 0;
}

void DMX512Controller::send_break()
{
    ioctl(fd_, TIOCSBRK, 0);
    std::this_thread::sleep_for(std::chrono::microseconds(176));
    ioctl(fd_, TIOCCBRK, 0);
    std::this_thread::sleep_for(std::chrono::microseconds(16));
}

bool DMX512Controller::send_once()
{
    if (fd_ < 0) return false;
    send_break();
    std::array<uint8_t, DMX_CHANNELS + 1> snapshot;
    {
        std::lock_guard<std::mutex> lock(universe_mutex_);
        snapshot = universe_;
    }
    ssize_t w = write(fd_, snapshot.data(), snapshot.size());
    tcdrain(fd_);
    return w == static_cast<ssize_t>(snapshot.size());
}

void DMX512Controller::tx_loop()
{
    using clock = std::chrono::steady_clock;
    auto period = std::chrono::microseconds(1'000'000 / refresh_hz_);

    while (running_.load(std::memory_order_relaxed)) {
        auto t0 = clock::now();
        send_once();
        auto elapsed = clock::now() - t0;
        if (elapsed < period)
            std::this_thread::sleep_for(period - elapsed);
    }
}

void DMX512Controller::start_transmit()
{
    if (running_.exchange(true)) return; // Already running
    tx_thread_ = std::thread(&DMX512Controller::tx_loop, this);
}

void DMX512Controller::stop_transmit()
{
    if (!running_.exchange(false)) return;
    if (tx_thread_.joinable()) tx_thread_.join();
}

// ── Usage example ────────────────────────────────────────────────────────────
/*
int main()
{
    DMX512Controller dmx("/dev/ttyUSB0", 44);
    if (!dmx.open()) { fprintf(stderr, "Cannot open port\n"); return 1; }

    // Set a 4-channel RGBA fixture starting at channel 1
    uint8_t rgba[4] = {200, 255, 0, 128};
    dmx.set_block(1, rgba, 4);

    dmx.start_transmit(); // Background thread at 44 Hz

    // Fade channel 1 from 200 → 0 over 2 seconds
    for (int i = 200; i >= 0; --i) {
        dmx.set(1, static_cast<uint8_t>(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    dmx.blackout();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    dmx.stop_transmit();
    return 0;
}
*/
```

---

## Programming in Rust

### Rust Linux UART Transmitter

Using the `serialport` crate for port configuration and `nix` for `ioctl` break control.

```toml
# Cargo.toml
[package]
name    = "dmx512"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4"
nix        = { version = "0.29", features = ["ioctl", "term"] }
thiserror  = "1"
```

```rust
// src/dmx_tx.rs
use nix::libc;
use serialport::SerialPort;
use std::time::Duration;
use thiserror::Error;

pub const DMX_CHANNELS: usize = 512;

#[derive(Debug, Error)]
pub enum DmxError {
    #[error("Serial port error: {0}")]
    Serial(#[from] serialport::Error),
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Nix error: {0}")]
    Nix(#[from] nix::Error),
    #[error("Invalid channel (must be 1..=512)")]
    InvalidChannel,
}

pub type DmxResult<T> = Result<T, DmxError>;

/// DMX512 universe: index 0 = start code, indices 1..=512 = channel data.
pub struct DmxUniverse {
    data: [u8; DMX_CHANNELS + 1],
}

impl DmxUniverse {
    pub fn new() -> Self {
        let mut u = DmxUniverse { data: [0u8; DMX_CHANNELS + 1] };
        u.data[0] = 0x00; // Standard start code
        u
    }

    pub fn set(&mut self, channel: usize, value: u8) -> DmxResult<()> {
        if channel < 1 || channel > DMX_CHANNELS {
            return Err(DmxError::InvalidChannel);
        }
        self.data[channel] = value;
        Ok(())
    }

    pub fn get(&self, channel: usize) -> DmxResult<u8> {
        if channel < 1 || channel > DMX_CHANNELS {
            return Err(DmxError::InvalidChannel);
        }
        Ok(self.data[channel])
    }

    pub fn blackout(&mut self) {
        for v in self.data[1..].iter_mut() { *v = 0; }
    }

    pub fn as_bytes(&self) -> &[u8; DMX_CHANNELS + 1] {
        &self.data
    }
}

impl Default for DmxUniverse {
    fn default() -> Self { Self::new() }
}

pub struct DmxTransmitter {
    port: Box<dyn SerialPort>,
}

impl DmxTransmitter {
    /// Open a serial port configured for DMX512 (250 000 8N2).
    pub fn open(path: &str) -> DmxResult<Self> {
        let port = serialport::new(path, 250_000)
            .data_bits(serialport::DataBits::Eight)
            .parity(serialport::Parity::None)
            .stop_bits(serialport::StopBits::Two)
            .flow_control(serialport::FlowControl::None)
            .timeout(Duration::from_millis(100))
            .open()?;
        Ok(DmxTransmitter { port })
    }

    /// Assert the serial break condition using TIOCSBRK / TIOCCBRK ioctls.
    fn send_break(&mut self) -> DmxResult<()> {
        use std::os::unix::io::AsRawFd;
        let fd = self.port.as_raw_fd();

        // BREAK on (line driven LOW)
        unsafe { libc::ioctl(fd, libc::TIOCSBRK) };
        std::thread::sleep(Duration::from_micros(176)); // ≥ 88 µs BREAK

        // BREAK off (line returns HIGH)
        unsafe { libc::ioctl(fd, libc::TIOCCBRK) };
        std::thread::sleep(Duration::from_micros(16));  // ≥ 8 µs MAB

        Ok(())
    }

    /// Transmit one complete DMX packet.
    pub fn send(&mut self, universe: &DmxUniverse) -> DmxResult<()> {
        self.send_break()?;
        self.port.write_all(universe.as_bytes())?;
        self.port.flush()?;
        Ok(())
    }
}

fn main() -> DmxResult<()> {
    let path = std::env::args().nth(1).unwrap_or_else(|| "/dev/ttyUSB0".into());
    let mut tx = DmxTransmitter::open(&path)?;
    let mut universe = DmxUniverse::new();

    println!("DMX512 transmitting on {path}");

    let mut level: u8 = 0;
    let mut rising = true;

    loop {
        universe.set(1, level)?;
        universe.set(2, 255)?;  // Red full
        universe.set(3, 0)?;    // Green off
        universe.set(4, level)?; // Blue follows

        tx.send(&universe)?;

        // Soft fade
        if rising {
            level = level.saturating_add(1);
            if level == 255 { rising = false; }
        } else {
            level = level.saturating_sub(1);
            if level == 0 { rising = true; }
        }
    }
}
```

---

### Rust Embedded (no_std) Transmitter

For embedded targets (e.g., STM32 via `stm32f4xx-hal`) using `embedded-hal` traits.

```toml
# Cargo.toml (embedded target)
[dependencies]
embedded-hal   = "1"
stm32f4xx-hal  = { version = "0.21", features = ["stm32f401"] }
cortex-m       = "0.7"
cortex-m-rt    = "0.7"
nb             = "1"
```

```rust
// src/dmx_embedded.rs
#![no_std]

use embedded_hal::serial::Write;
use embedded_hal::digital::v2::OutputPin;

pub const DMX_CHANNELS: usize = 512;

/// Minimal delay trait (provided by the HAL or a DWT timer).
pub trait DelayUs {
    fn delay_us(&mut self, us: u32);
}

/// DMX512 transmitter over embedded-hal.
///
/// `TX`  – UART TX writer (configured 250 000 8N2)
/// `BRK` – GPIO pin for break generation (TX pin reconfigured as output)
///         or a dedicated RS-485 break pin.
/// `DE`  – RS-485 driver enable (active HIGH)
pub struct DmxEmbedded<TX, BRK, DE, DLY>
where
    TX:  Write<u8>,
    BRK: OutputPin,
    DE:  OutputPin,
    DLY: DelayUs,
{
    uart:  TX,
    brk:   BRK,
    de:    DE,
    delay: DLY,
}

impl<TX, BRK, DE, DLY> DmxEmbedded<TX, BRK, DE, DLY>
where
    TX:  Write<u8>,
    BRK: OutputPin,
    DE:  OutputPin,
    DLY: DelayUs,
{
    pub fn new(uart: TX, brk: BRK, de: DE, delay: DLY) -> Self {
        DmxEmbedded { uart, brk, de, delay }
    }

    fn send_break(&mut self) {
        // Drive BREAK line LOW for 176 µs
        let _ = self.brk.set_low();
        self.delay.delay_us(176);

        // MAB: drive HIGH for 16 µs
        let _ = self.brk.set_high();
        self.delay.delay_us(16);
    }

    /// Transmit a complete DMX universe.
    /// `channels`: slice of exactly 512 bytes (channel 1 = index 0).
    pub fn transmit(&mut self, channels: &[u8; DMX_CHANNELS]) {
        // Enable RS-485 driver
        let _ = self.de.set_high();

        // BREAK + MAB
        self.send_break();

        // Start code (0x00 = standard dimmer)
        self.write_byte(0x00);

        // 512 channel bytes
        for &byte in channels.iter() {
            self.write_byte(byte);
        }

        // Wait for final stop bits to clock out, then release RS-485 bus
        // (In practice, use a hardware "TC" interrupt / flag here)
        self.delay.delay_us(44); // One slot time safety margin
        let _ = self.de.set_low();
    }

    fn write_byte(&mut self, byte: u8) {
        loop {
            match self.uart.write(byte) {
                Ok(()) => break,
                Err(nb::Error::WouldBlock) => continue,
                Err(_) => break,
            }
        }
    }
}
```

---

### Rust DMX Universe Manager

A higher-level, threaded Rust manager that owns the transmitter and exposes an
`Arc<Mutex<DmxUniverse>>` for concurrent access.

```rust
// src/dmx_manager.rs
use crate::dmx_tx::{DmxError, DmxResult, DmxTransmitter, DmxUniverse, DMX_CHANNELS};
use std::{
    sync::{Arc, Mutex},
    thread,
    time::{Duration, Instant},
};

/// A fixture description: start address + number of channels.
#[derive(Debug, Clone)]
pub struct Fixture {
    pub name:    String,
    pub address: usize,   // DMX start address (1-indexed)
    pub channels: usize,  // Number of channels this fixture uses
}

impl Fixture {
    pub fn new(name: impl Into<String>, address: usize, channels: usize) -> Self {
        Fixture { name: name.into(), address, channels }
    }

    /// Write channel values relative to this fixture's base address.
    pub fn set_channels(
        &self,
        universe: &mut DmxUniverse,
        values: &[u8],
    ) -> DmxResult<()> {
        for (i, &v) in values.iter().take(self.channels).enumerate() {
            universe.set(self.address + i, v)?;
        }
        Ok(())
    }
}

pub struct DmxManager {
    universe:  Arc<Mutex<DmxUniverse>>,
    _tx_handle: thread::JoinHandle<()>,
}

impl DmxManager {
    /// Start a background transmit thread at the given refresh rate.
    pub fn start(port: &str, refresh_hz: u32) -> DmxResult<Self> {
        let universe = Arc::new(Mutex::new(DmxUniverse::new()));
        let uni_clone = Arc::clone(&universe);
        let port = port.to_string();

        let handle = thread::spawn(move || {
            let mut tx = match DmxTransmitter::open(&port) {
                Ok(t)  => t,
                Err(e) => { eprintln!("DMX open error: {e}"); return; }
            };

            let period = Duration::from_micros(1_000_000 / refresh_hz as u64);

            loop {
                let t0 = Instant::now();

                let snapshot = {
                    let guard = uni_clone.lock().unwrap();
                    // Clone just the 513-byte buffer
                    *guard.as_bytes()
                };

                // Build a temporary universe from the snapshot
                let mut tmp = DmxUniverse::new();
                for ch in 1..=DMX_CHANNELS {
                    let _ = tmp.set(ch, snapshot[ch]);
                }

                if let Err(e) = tx.send(&tmp) {
                    eprintln!("DMX send error: {e}");
                }

                let elapsed = t0.elapsed();
                if elapsed < period {
                    thread::sleep(period - elapsed);
                }
            }
        });

        Ok(DmxManager { universe, _tx_handle: handle })
    }

    /// Access the shared universe for modification.
    pub fn universe(&self) -> Arc<Mutex<DmxUniverse>> {
        Arc::clone(&self.universe)
    }

    /// Convenience: set a single channel.
    pub fn set(&self, channel: usize, value: u8) -> DmxResult<()> {
        self.universe.lock().unwrap().set(channel, value)
    }

    /// Convenience: blackout all channels.
    pub fn blackout(&self) {
        self.universe.lock().unwrap().blackout();
    }

    /// Linear fade from current to target over `duration`.
    /// Blocks the calling thread for the fade duration.
    pub fn fade_blocking(
        &self,
        channel: usize,
        target: u8,
        duration: Duration,
    ) -> DmxResult<()> {
        let start_val = self.universe.lock().unwrap().get(channel)?;
        let steps = 100u32;
        let step_dur = duration / steps;

        for step in 0..=steps {
            let t = step as f32 / steps as f32;
            let value = (start_val as f32 + (target as f32 - start_val as f32) * t) as u8;
            self.set(channel, value)?;
            thread::sleep(step_dur);
        }
        Ok(())
    }
}

// ── Usage example ────────────────────────────────────────────────────────────
fn main() -> DmxResult<()> {
    let mgr = DmxManager::start("/dev/ttyUSB0", 44)?;

    // Define fixtures
    let wash1 = Fixture::new("Wash1", 1, 4);   // RGBW at address 1..4
    let wash2 = Fixture::new("Wash2", 5, 4);   // RGBW at address 5..8
    let spot   = Fixture::new("Spot", 9, 8);   // 8-channel moving head

    // Set wash1 to warm white
    {
        let mut u = mgr.universe().lock().unwrap();
        wash1.set_channels(&mut u, &[255, 180, 60, 200])?;
        wash2.set_channels(&mut u, &[255, 180, 60, 200])?;
        spot.set_channels(&mut u, &[255, 128, 64, 0, 180, 90, 0, 0])?;
    }

    println!("Fading spot intensity from 255 → 0...");
    mgr.fade_blocking(9, 0, Duration::from_secs(3))?;

    println!("Blackout");
    mgr.blackout();
    thread::sleep(Duration::from_secs(1));

    Ok(())
}
```

---

## RDM – Remote Device Management

**ANSI E1.20 RDM** extends DMX512 to a request/response protocol. The controller can
query and configure fixtures (get serial numbers, set addresses, read lamp hours, etc.)
without physically touching the equipment.

### Key RDM Concepts

| Concept | Details |
|---------|---------|
| Discovery | Controller finds all RDM UIDs on the bus using binary-tree algorithm |
| UID | 48-bit unique identifier (6-byte manufacturer + device serial) |
| Alternate Start Code | 0xCC instead of 0x00; shorter BREAK timing |
| Half-duplex | Controller transmits, then releases bus; fixture responds within 2.8 ms |
| Parameter IDs (PIDs) | Standard commands: `DEVICE_INFO`, `DMX_START_ADDRESS`, `LAMP_HOURS`, etc. |

### Minimal RDM GET Request Frame (C)

```c
/* Build a minimal RDM GET_COMMAND for DEVICE_INFO (PID 0x0060) */

#include <stdint.h>
#include <string.h>

#define RDM_START_CODE      0xCC
#define RDM_SUB_START_CODE  0x01
#define RDM_GET_COMMAND     0x20
#define PID_DEVICE_INFO     0x0060

typedef struct __attribute__((packed)) {
    uint8_t  start_code;       /* 0xCC */
    uint8_t  sub_start_code;   /* 0x01 */
    uint8_t  message_length;   /* Bytes from start_code to end of PD */
    uint8_t  dest_uid[6];      /* Target fixture UID */
    uint8_t  src_uid[6];       /* Controller UID */
    uint8_t  tn;               /* Transaction number */
    uint8_t  port_id;          /* 0x01 for commands */
    uint8_t  msg_count;        /* Usually 0 */
    uint16_t sub_device;       /* 0x0000 = root device */
    uint8_t  command_class;    /* 0x20 = GET_COMMAND */
    uint16_t parameter_id;     /* PID */
    uint8_t  pdl;              /* Parameter data length */
    /* Parameter data follows (0 bytes for GET DEVICE_INFO) */
    uint16_t checksum;         /* Sum of all preceding bytes */
} RDM_Frame_t;

static uint16_t rdm_checksum(const uint8_t *data, int len)
{
    uint16_t sum = 0;
    for (int i = 0; i < len; ++i) sum += data[i];
    return sum;
}

void build_rdm_get_device_info(
    RDM_Frame_t *frame,
    const uint8_t dest_uid[6],
    const uint8_t src_uid[6],
    uint8_t tn)
{
    memset(frame, 0, sizeof(*frame));
    frame->start_code     = RDM_START_CODE;
    frame->sub_start_code = RDM_SUB_START_CODE;
    frame->message_length = 24; /* Fixed for zero-PDL GET */
    memcpy(frame->dest_uid, dest_uid, 6);
    memcpy(frame->src_uid,  src_uid,  6);
    frame->tn             = tn;
    frame->port_id        = 0x01;
    frame->msg_count      = 0;
    frame->sub_device     = 0x0000;
    frame->command_class  = RDM_GET_COMMAND;
    frame->parameter_id   = __builtin_bswap16(PID_DEVICE_INFO); /* Big-endian */
    frame->pdl            = 0;

    /* Checksum over bytes 0..22 (message_length - 2) */
    frame->checksum = __builtin_bswap16(
        rdm_checksum((uint8_t *)frame, frame->message_length - 2));
}
```

---

## Common Pitfalls & Debugging

### 1. Wrong Baud Rate

DMX512 requires **exactly 250 000 bps**. Many USB-serial adapters round to the nearest
supported rate. Always verify with an oscilloscope: one bit = **4.000 µs**.

### 2. Missing or Too-Short BREAK

A BREAK shorter than 88 µs will cause all receivers to ignore the packet. Measure from
the start of the LOW condition to the first rising edge of the MAB.

### 3. Missing 120 Ω Terminator

Without termination at the far end of the cable, signal reflections corrupt data at
distances beyond ~10 m. Symptoms: intermittent flickering, fixtures randomly resetting.

### 4. Single Stop Bit

Standard RS-232/UART defaults to 1 stop bit. DMX512 requires **2 stop bits**. Fixtures
will intermittently receive framing errors and go to blackout.

### 5. RS-485 Direction Pin Not Toggled

For half-duplex RS-485 transceivers (e.g., MAX485, SN75176), the DE/RE pins must be
driven HIGH for transmit and LOW for receive. Forgetting this means the driver bus
fights itself or never transmits at all.

### 6. Refresh Rate Too Low

ANSI E1.11 mandates that receivers shall implement a **hold** of at least 1 second
without a valid packet before going to a "loss of signal" state. In practice, fixtures
expect ~44 Hz. Slower rates cause visible flicker on dimmers.

### 7. Interleaved Start Codes

If multiple controllers share a universe (not recommended) and use different start codes,
ensure fixtures are configured to accept only the 0x00 standard start code, otherwise
they may misinterpret RDM or text frames as dimmer data.

### Debugging Tools

| Tool | Use |
|------|-----|
| Oscilloscope | Verify BREAK, MAB, bit timing, stop bits |
| Logic analyser (Saleae, etc.) | Decode all 513 bytes of a packet |
| ENTTEC Open DMX / USB Pro | Reference USB→DMX interfaces |
| QLC+ | Open-source lighting console for testing universes |
| Art-Net/sACN bridge | Test over IP before adding RS-485 hardware |

---

## Summary

DMX512 is a purpose-built adaptation of UART framing for deterministic, real-time
control of stage lighting and effects. Its key distinguishing features — the mandatory
BREAK+MAB preamble, the exact 250 000 bps baud rate, and the 8N2 framing — mean that
while the underlying mechanics are UART-compatible, dedicated hardware control or GPIO
manipulation is required to produce and detect the protocol correctly.

The protocol's broadcast-only, single-master architecture makes it simple and
interference-resistant, supporting up to 512 channels per universe at approximately
44 Hz refresh. Larger installations use multiple universes distributed over Art-Net
or sACN (E1.31) Ethernet, with per-universe DMX512 output nodes.

**RDM (ANSI E1.20)** extends the protocol to bidirectional communication, enabling
remote configuration and diagnostics without rewiring.

From a software perspective:

- **C on Linux** uses `termios` at 250 000 8N2 with `TIOCSBRK`/`TIOCCBRK` ioctls for
  the BREAK, and `PARMRK` for BREAK detection on receive.
- **C on embedded (STM32)** exploits UART framing-error interrupts for receive and
  GPIO bit-banging or hardware UART break generation for transmit, combined with DMA
  for efficient packet delivery.
- **C++** wraps these primitives into reusable transmitter and fixture-mapping classes
  with background transmission threads.
- **Rust on Linux** uses the `serialport` crate with raw `libc` ioctls for BREAK, and
  `Arc<Mutex<DmxUniverse>>` for safe concurrent access.
- **Rust embedded** leverages `embedded-hal` traits to keep hardware-agnostic logic
  separated from the HAL implementation, supporting `no_std` environments.

Understanding the electrical, timing, and framing requirements is essential: many bugs
in DMX implementations stem from incorrect baud rates, missing termination, single stop
bits, or incorrectly timed BREAK conditions rather than logic errors in the channel data
itself.

---

*Document: 62_DMX512_Protocol.md — Part of the UART Protocol Reference Series*