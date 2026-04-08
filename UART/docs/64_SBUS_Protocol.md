The document covers the full SBUS topic across 16 sections:

**Protocol fundamentals** — Physical/electrical layer, the non-standard 100,000 baud / 8E2 / inverted UART configuration, and the exact 25-byte frame structure with timing analysis.

**Bit-packing deep dive** — The 11-bit channel packing scheme across 22 bytes with a bit-level diagram, both an explicit unrolled decoder and a clean generic loop-based decoder, plus channel → pulse-width and channel → normalized float conversions.

**The inversion problem** — Full explanation of why standard UART misreads SBUS, with waveform diagrams, and four hardware inversion strategies (NPN transistor circuit, 74HC04 gate, dedicated IC, and MCU register bit) with a comparison table.

**C/C++ code** — Streaming byte-by-byte parser (ISR-safe), STM32 HAL integration with `USART_CR2_RXINV`, POSIX/Linux `termios2` setup for Raspberry Pi, and a frame encoder for testing/simulation.

**Rust code** — `no_std`-compatible `Parser` struct using `bitflags`, generic decoder, async `tokio` reader, and a full RTIC embedded example on STM32F4 with hardware UART inversion.

**Platform notes** — STM32 family inversion support matrix, Raspberry Pi setup, Arduino SoftwareSerial inverted mode, ESP32 IDF, and RP2040 PIO approach.

**Operational concerns** — Failsafe vs frame-lost handling with recommended thresholds, SBUS2 extension differences, and a common pitfalls table.

# 64. SBUS Protocol — FrSky SBUS RC Receiver Protocol with Inverted UART

---

## Table of Contents

1. [Overview](#1-overview)
2. [Physical & Electrical Layer](#2-physical--electrical-layer)
3. [UART Configuration](#3-uart-configuration)
4. [Packet Structure](#4-packet-structure)
5. [Channel Decoding](#5-channel-decoding)
6. [Flags Byte](#6-flags-byte)
7. [Inverted Signal — The Core Challenge](#7-inverted-signal--the-core-challenge)
8. [Hardware Inversion Strategies](#8-hardware-inversion-strategies)
9. [Software Inversion Strategies](#9-software-inversion-strategies)
10. [C/C++ Implementation](#10-cc-implementation)
11. [Rust Implementation](#11-rust-implementation)
12. [Platform-Specific Notes](#12-platform-specific-notes)
13. [Failsafe & Lost-Frame Handling](#13-failsafe--lost-frame-handling)
14. [SBUS2 Extension](#14-sbus2-extension)
15. [Common Pitfalls](#15-common-pitfalls)
16. [Summary](#16-summary)

---

## 1. Overview

**SBUS** (Serial Bus) is a digital RC (Radio Control) receiver-to-flight-controller serial protocol developed by **Futaba** and adopted widely by **FrSky** and other RC manufacturers. It encodes up to **16 proportional servo channels** (11-bit each) plus **2 digital channels** and frame-loss/failsafe flags into a compact 25-byte packet transmitted continuously at a fixed rate.

SBUS is now the de-facto standard for high-quality RC links in drone autopilots (ArduPilot, PX4, Betaflight, iNav), RC simulators, and robotics because:

- It carries 16 channels on a **single wire**
- It is inherently latency-bounded (~7 ms update rate in fast mode)
- It is supported by virtually every modern flight controller

The **defining characteristic** that makes SBUS non-trivial to implement is its **inverted UART signal**: logic 0 (space) is HIGH voltage (~3.3 V) and logic 1 (mark) is LOW voltage (~0 V) — the opposite of standard RS-232/TTL UART polarity.

---

## 2. Physical & Electrical Layer

```
RC Receiver (FrSky, Futaba, etc.)
      │
  SBUS pin  ───────────────────────────► Flight Controller / MCU
      │         Inverted TTL, ~3.3 V
      │         Single wire, one-directional
      │
     GND ───────────────────────────────► GND
     VCC ───────────────────────────────► 3.3 V / 5 V (receiver supply)
```

| Parameter        | Value                         |
|------------------|-------------------------------|
| Signal level     | 3.3 V TTL (inverted polarity) |
| Direction        | Receiver → Controller (simplex) |
| Cable            | Servo-style 3-pin connector (GND, VCC, Signal) |
| Line idle state  | LOW (logic 1 = LOW in inverted UART) |
| Connector        | JST-ZH 1.5 mm or standard servo 2.54 mm |

> **Important**: Never connect a 5 V SBUS signal directly to a 3.3 V MCU UART RX pin without level shifting; some FrSky receivers (e.g. R-XSR, XM+) output 3.3 V natively, but others (older Futaba) may output 5 V.

---

## 3. UART Configuration

SBUS uses a non-standard UART configuration:

| Parameter      | Value                |
|----------------|----------------------|
| Baud rate      | **100,000 bps**      |
| Data bits      | **8**                |
| Stop bits      | **2**                |
| Parity         | **Even**             |
| Signal polarity| **Inverted**         |
| Update rate    | ~7 ms (fast) or ~14 ms (normal mode) |

The **100,000 baud rate** is non-standard and requires either:
- A UART peripheral capable of arbitrary baud rates (most ARM Cortex-M peripherals can do this)
- A dedicated SBUS library/peripheral

The **2 stop bits + even parity** means each byte is framed as:
```
[START(0)] [D0..D7] [PARITY] [STOP1] [STOP2]
```
That is 11 bits total per byte on the wire (compared to 10 bits for 8N1).

---

## 4. Packet Structure

Each SBUS frame is exactly **25 bytes** long:

```
Byte  0:    0x0F  (Start byte — always 0x0F)
Bytes 1–22: Channel data (16 channels × 11 bits packed into 176 bits = 22 bytes)
Byte 23:    Flags byte
Byte 24:    0x00  (End byte — always 0x00)
```

### Visual Frame Layout

```
Byte:  [0x0F] [B1 ] [B2 ] [B3 ] [B4 ] ... [B22] [FLG] [0x00]
         ↑                                          ↑     ↑
       Start                                      Flags  End
```

### Timing at 100,000 baud (fast mode, ~7 ms period)

```
Frame transmission time = 25 bytes × 11 bits/byte ÷ 100,000 bps = 2.75 ms
Inter-frame gap          ≈ 4.25 ms (fast) or 11.25 ms (normal)
Total frame period       ≈ 7 ms (fast) or 14 ms (normal)
```

---

## 5. Channel Decoding

The 16 channels are packed **LSB-first** contiguously into bytes 1–22. Each channel is 11 bits wide with valid range typically **172–1811** (corresponding to servo pulse widths of ~1000–2000 µs). Midpoint is 992 (≈1500 µs).

### Bit Packing Diagram (Channels 1–3 shown)

```
Byte 1         Byte 2          Byte 3
76543210       76543210        76543210
[ch1  7:0] [ch2 2:0|ch1 10:8] [ch2 10:3]  ...
```

### Extraction Formula

For channel `n` (0-indexed, 0–15):

```
bit_offset = n * 11
byte_index = bit_offset / 8      (integer division)
bit_shift  = bit_offset % 8
value      = ((payload[byte_index] | (payload[byte_index+1] << 8) | (payload[byte_index+2] << 16))
              >> bit_shift) & 0x7FF
```

### Channel Value → Servo Pulse Width Mapping

| SBUS Value | Pulse Width | Meaning          |
|------------|-------------|------------------|
| 172        | ~1000 µs    | Minimum          |
| 992        | ~1500 µs    | Center / Midpoint|
| 1811       | ~2000 µs    | Maximum          |

Linear mapping formula:
```
pulse_us = ((sbus_value - 172) / (1811 - 172)) * 1000 + 1000
```

---

## 6. Flags Byte

Byte 23 (the flags byte) encodes status information:

```
Bit 7: Digital channel 17 (1 = active)
Bit 6: Digital channel 18 (1 = active)
Bit 5: Frame lost       (1 = receiver lost signal for this frame)
Bit 4: Failsafe active  (1 = failsafe positions are being output)
Bits 3:0: Reserved (always 0)
```

### Flag Definitions

| Flag          | Bit | Meaning                                                              |
|---------------|-----|----------------------------------------------------------------------|
| `CH17`        | 7   | Binary channel 17 (on/off)                                           |
| `CH18`        | 6   | Binary channel 18 (on/off)                                           |
| `FRAME_LOST`  | 5   | Current frame was not received cleanly (e.g. packet loss)            |
| `FAILSAFE`    | 4   | Receiver failsafe triggered (link lost for extended period)          |

```
#define SBUS_FLAG_CH17        (1 << 7)
#define SBUS_FLAG_CH18        (1 << 6)
#define SBUS_FLAG_FRAME_LOST  (1 << 5)
#define SBUS_FLAG_FAILSAFE    (1 << 4)
```

---

## 7. Inverted Signal — The Core Challenge

Standard UART (RS-232/TTL):
```
Idle  ──────┐                         ┌────── Idle
            │   Start  Data bits  Stop│
            └───0──────10101010───1───┘
  Idle = HIGH (Mark = 1)
  Start bit = LOW
```

SBUS Inverted UART:
```
Idle  ──────┐                         ┌────── Idle
(LOW)       │  Start   Data bits  Stop│ (LOW)
            └───1──────01010101───0───┘
  Idle = LOW (inverted Mark = 0 voltage)
  Start bit = HIGH voltage pulse
```

This inversion means:
- **A standard UART RX pin will misread every byte** — you must either invert the signal in hardware or configure the UART peripheral for inverted input
- The idle line is LOW (0 V) instead of HIGH
- The start bit appears as a HIGH pulse, not a LOW pulse

---

## 8. Hardware Inversion Strategies

### 8.1 Single NPN Transistor Inverter

The simplest and most common approach — a single NPN transistor (e.g. 2N3904, BC547) acts as an inverter:

```
SBUS Signal (inverted) ──[4.7kΩ]──► Base (B)
                                         │NPN
3.3V ──[10kΩ]──────────────────────► Collector (C) ──► MCU UART RX
                                         │
                                        GND (E)
```

When SBUS line is HIGH (idle LOW signal = HIGH voltage) → transistor saturates → MCU RX pulled LOW
When SBUS line is LOW (start bit HIGH voltage) → transistor cuts off → MCU RX pulled HIGH via 10 kΩ to 3.3 V

This restores normal UART polarity at the MCU pin.

### 8.2 Logic Gate Inverter

A single inverter gate (74HC04, 74LVC1G04) provides clean inversion with proper drive strength:

```
SBUS ──► [74LVC1G04] ──► MCU UART RX
```

### 8.3 Dedicated SBUS Inverter ICs

Some flight controller boards include dedicated SBUS inversion circuits (e.g. MAX3232 configured as inverter, or SN74LVC2G04).

### 8.4 Hardware Comparison

| Method               | Cost   | Speed     | Level Shift       | Notes                    |
|----------------------|--------|-----------|-------------------|--------------------------|
| NPN Transistor       | ~$0.05 | Good      | 5V→3.3V OK        | Slight edge distortion   |
| Logic Gate (74HC04)  | ~$0.30 | Excellent | Need matching VCC | Clean signal             |
| Dedicated IC         | ~$0.50 | Excellent | Yes               | Best for production      |
| MCU UART inversion   | $0     | Perfect   | None              | Preferred if MCU supports|

---

## 9. Software Inversion Strategies

Many modern MCUs (STM32, Nordic nRF52, NXP i.MX RT, ESP32) support **hardware UART signal inversion** via a register bit — no external components needed.

### 9.1 STM32 UART Inversion

STM32 USARTs support individual TX/RX pin inversion via the `CR2` register:

```c
// STM32 HAL approach
USART2->CR2 |= USART_CR2_RXINV;   // Invert RX pin polarity
```

### 9.2 ESP32 UART Inversion

```c
// ESP-IDF
uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV);
```

### 9.3 RP2040 (Raspberry Pi Pico) — No Hardware Inversion

The RP2040 PIO (Programmable I/O) subsystem can implement inverted UART in firmware, or use a transistor inverter.

---

## 10. C/C++ Implementation

### 10.1 Data Structures

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SBUS_NUM_CHANNELS    16
#define SBUS_FRAME_SIZE      25
#define SBUS_START_BYTE      0x0F
#define SBUS_END_BYTE        0x00

#define SBUS_FLAG_CH17        (1 << 7)
#define SBUS_FLAG_CH18        (1 << 6)
#define SBUS_FLAG_FRAME_LOST  (1 << 5)
#define SBUS_FLAG_FAILSAFE    (1 << 4)

#define SBUS_VALUE_MIN       172
#define SBUS_VALUE_MID       992
#define SBUS_VALUE_MAX       1811

typedef struct {
    uint16_t channels[SBUS_NUM_CHANNELS]; // 11-bit values [172..1811]
    bool     ch17;
    bool     ch18;
    bool     frame_lost;
    bool     failsafe;
} sbus_frame_t;

typedef struct {
    uint8_t      buf[SBUS_FRAME_SIZE];
    uint8_t      idx;
    bool         synced;
    sbus_frame_t last_valid;
    uint32_t     frame_count;
    uint32_t     error_count;
} sbus_parser_t;
```

### 10.2 Frame Parser (Byte-Streaming, ISR-Safe)

```c
/**
 * Feed one byte at a time from UART RX interrupt or polling.
 * Returns true when a complete valid frame has been parsed.
 */
bool sbus_parse_byte(sbus_parser_t *p, uint8_t byte, sbus_frame_t *out) {
    // Resync: look for start byte
    if (!p->synced) {
        if (byte == SBUS_START_BYTE) {
            p->buf[0] = byte;
            p->idx    = 1;
            p->synced = true;
        }
        return false;
    }

    p->buf[p->idx++] = byte;

    if (p->idx < SBUS_FRAME_SIZE) {
        return false; // Not complete yet
    }

    // Full frame received — validate
    p->synced = false; // Reset for next frame

    if (p->buf[0] != SBUS_START_BYTE || p->buf[24] != SBUS_END_BYTE) {
        p->error_count++;
        return false;
    }

    // Decode 16 × 11-bit channels from bytes 1..22
    const uint8_t *d = &p->buf[1];

    out->channels[0]  = ((d[0]       | (d[1]  << 8)) & 0x07FF);
    out->channels[1]  = ((d[1]  >> 3 | (d[2]  << 5)) & 0x07FF);
    out->channels[2]  = ((d[2]  >> 6 | (d[3]  << 2) | (d[4]  << 10)) & 0x07FF);
    out->channels[3]  = ((d[4]  >> 1 | (d[5]  << 7)) & 0x07FF);
    out->channels[4]  = ((d[5]  >> 4 | (d[6]  << 4)) & 0x07FF);
    out->channels[5]  = ((d[6]  >> 7 | (d[7]  << 1) | (d[8]  << 9))  & 0x07FF);
    out->channels[6]  = ((d[8]  >> 2 | (d[9]  << 6)) & 0x07FF);
    out->channels[7]  = ((d[9]  >> 5 | (d[10] << 3)) & 0x07FF);
    out->channels[8]  = ((d[10]      | (d[11] << 8)) & 0x07FF);
    out->channels[9]  = ((d[11] >> 3 | (d[12] << 5)) & 0x07FF);
    out->channels[10] = ((d[12] >> 6 | (d[13] << 2) | (d[14] << 10)) & 0x07FF);
    out->channels[11] = ((d[14] >> 1 | (d[15] << 7)) & 0x07FF);
    out->channels[12] = ((d[15] >> 4 | (d[16] << 4)) & 0x07FF);
    out->channels[13] = ((d[16] >> 7 | (d[17] << 1) | (d[18] << 9))  & 0x07FF);
    out->channels[14] = ((d[18] >> 2 | (d[19] << 6)) & 0x07FF);
    out->channels[15] = ((d[19] >> 5 | (d[20] << 3)) & 0x07FF);

    // Flags byte is at index 23 (d[22])
    uint8_t flags     = d[22];
    out->ch17         = (flags & SBUS_FLAG_CH17)       != 0;
    out->ch18         = (flags & SBUS_FLAG_CH18)       != 0;
    out->frame_lost   = (flags & SBUS_FLAG_FRAME_LOST) != 0;
    out->failsafe     = (flags & SBUS_FLAG_FAILSAFE)   != 0;

    p->last_valid  = *out;
    p->frame_count++;
    return true;
}
```

### 10.3 Generic (Loop-Based) Decoder

```c
/**
 * Generic 11-bit channel extraction using bit arithmetic.
 * Equivalent to the explicit version above but shorter.
 */
void sbus_decode_channels_generic(const uint8_t *payload22,
                                   uint16_t channels[SBUS_NUM_CHANNELS])
{
    for (int ch = 0; ch < SBUS_NUM_CHANNELS; ch++) {
        uint32_t bit_pos  = (uint32_t)ch * 11;
        uint32_t byte_idx = bit_pos >> 3;          // / 8
        uint32_t bit_off  = bit_pos & 0x7;         // % 8

        uint32_t raw = (uint32_t)payload22[byte_idx]
                     | ((uint32_t)payload22[byte_idx + 1] << 8)
                     | ((uint32_t)payload22[byte_idx + 2] << 16);

        channels[ch] = (uint16_t)((raw >> bit_off) & 0x07FF);
    }
}
```

### 10.4 Channel Value Normalization

```c
/**
 * Map SBUS value [172..1811] to normalized float [-1.0 .. +1.0]
 */
float sbus_to_normalized(uint16_t sbus_val) {
    float v = ((float)sbus_val - SBUS_VALUE_MID) /
              ((SBUS_VALUE_MAX - SBUS_VALUE_MIN) / 2.0f);
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return v;
}

/**
 * Map SBUS value [172..1811] to servo pulse width [1000..2000] µs
 */
uint16_t sbus_to_pulse_us(uint16_t sbus_val) {
    int32_t us = 1000 + ((int32_t)(sbus_val - SBUS_VALUE_MIN) * 1000)
                       / (SBUS_VALUE_MAX - SBUS_VALUE_MIN);
    if (us < 1000) us = 1000;
    if (us > 2000) us = 2000;
    return (uint16_t)us;
}
```

### 10.5 STM32 HAL Platform Integration

```c
/* ---------------------------------------------------------------
 * STM32 UART setup for SBUS (100,000 baud, 8E2, inverted RX)
 * Example: USART2, PD6 = RX
 * --------------------------------------------------------------- */
#include "stm32f4xx_hal.h"

static UART_HandleTypeDef huart2;
static uint8_t            sbus_rx_byte;
static sbus_parser_t      parser;
static sbus_frame_t       frame;
static volatile bool      frame_ready;

void sbus_uart_init(void) {
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    // PD6 as USART2_RX alternate function
    GPIO_InitTypeDef gpio = {
        .Pin       = GPIO_PIN_6,
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_NOPULL,
        .Speed     = GPIO_SPEED_FREQ_HIGH,
        .Alternate = GPIO_AF7_USART2,
    };
    HAL_GPIO_Init(GPIOD, &gpio);

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 100000;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_2;
    huart2.Init.Parity       = UART_PARITY_EVEN;
    huart2.Init.Mode         = UART_MODE_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);

    // Invert RX pin in hardware — no external inverter needed
    USART2->CR2 |= USART_CR2_RXINV;

    // Enable byte-received interrupt
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    HAL_UART_Receive_IT(&huart2, &sbus_rx_byte, 1);
}

// Called from HAL UART RX complete callback
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h) {
    if (h->Instance == USART2) {
        sbus_frame_t tmp;
        if (sbus_parse_byte(&parser, sbus_rx_byte, &tmp)) {
            frame       = tmp;
            frame_ready = true;
        }
        HAL_UART_Receive_IT(&huart2, &sbus_rx_byte, 1);
    }
}

// Main loop usage
void app_main(void) {
    sbus_uart_init();
    memset(&parser, 0, sizeof(parser));

    while (1) {
        if (frame_ready) {
            frame_ready = false;

            if (!frame.failsafe && !frame.frame_lost) {
                for (int i = 0; i < 16; i++) {
                    uint16_t us = sbus_to_pulse_us(frame.channels[i]);
                    // drive_servo(i, us);
                    (void)us;
                }
            }
        }
    }
}
```

### 10.6 SBUS Frame Encoder (for simulators / loopback testing)

```c
/**
 * Encode an SBUS frame from 16 channel values.
 * Output buffer must be SBUS_FRAME_SIZE (25) bytes.
 */
void sbus_encode_frame(const uint16_t channels[SBUS_NUM_CHANNELS],
                       bool ch17, bool ch18,
                       bool frame_lost, bool failsafe,
                       uint8_t out[SBUS_FRAME_SIZE])
{
    memset(out, 0, SBUS_FRAME_SIZE);
    out[0]  = SBUS_START_BYTE;
    out[24] = SBUS_END_BYTE;

    // Pack 16 × 11-bit channels into bytes 1..22
    uint8_t *d = &out[1];

    d[0]  =  (channels[0]        & 0xFF);
    d[1]  =  (channels[0]  >> 8) | (channels[1]  << 3);
    d[2]  =  (channels[1]  >> 5) | (channels[2]  << 6);
    d[3]  =  (channels[2]  >> 2);
    d[4]  =  (channels[2]  >> 10)| (channels[3]  << 1);
    d[5]  =  (channels[3]  >> 7) | (channels[4]  << 4);
    d[6]  =  (channels[4]  >> 4) | (channels[5]  << 7);
    d[7]  =  (channels[5]  >> 1);
    d[8]  =  (channels[5]  >> 9) | (channels[6]  << 2);
    d[9]  =  (channels[6]  >> 6) | (channels[7]  << 5);
    d[10] =  (channels[7]  >> 3);
    d[11] =  (channels[8]        & 0xFF);
    d[12] =  (channels[8]  >> 8) | (channels[9]  << 3);
    d[13] =  (channels[9]  >> 5) | (channels[10] << 6);
    d[14] =  (channels[10] >> 2);
    d[15] =  (channels[10] >> 10)| (channels[11] << 1);
    d[16] =  (channels[11] >> 7) | (channels[12] << 4);
    d[17] =  (channels[12] >> 4) | (channels[13] << 7);
    d[18] =  (channels[13] >> 1);
    d[19] =  (channels[13] >> 9) | (channels[14] << 2);
    d[20] =  (channels[14] >> 6) | (channels[15] << 5);
    d[21] =  (channels[15] >> 3);

    // Flags
    out[23] = (ch17        ? SBUS_FLAG_CH17        : 0)
            | (ch18        ? SBUS_FLAG_CH18        : 0)
            | (frame_lost  ? SBUS_FLAG_FRAME_LOST  : 0)
            | (failsafe    ? SBUS_FLAG_FAILSAFE     : 0);
}
```

### 10.7 POSIX Linux / Raspberry Pi (UART + termios)

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

/**
 * Open and configure a UART port for SBUS on Linux.
 * Assumes hardware inverter is present (e.g. transistor or 74HC04).
 * /dev/ttyAMA0, /dev/ttyUSB0, /dev/serial0, etc.
 */
int sbus_open_serial(const char *dev) {
    int fd = open(dev, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    // 100,000 baud — use cfsetispeed with B115200 then patch the divisor,
    // or use custom baud via BOTHER on Linux ≥ 3.x
    cfsetispeed(&tty, B115200); // placeholder; override below

    tty.c_cflag  = CS8 | CSTOPB | PARENB | CREAD | CLOCAL; // 8E2
    tty.c_iflag  = IGNPAR; // ignore parity errors (we do our own framing)
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &tty);

    // Set custom 100,000 baud via Linux BOTHER / struct termios2
    // (requires linux/termios.h and ioctl TCSETS2)
    struct termios2 tty2;
    ioctl(fd, TCGETS2, &tty2);
    tty2.c_cflag &= ~CBAUD;
    tty2.c_cflag |= BOTHER;
    tty2.c_ispeed = 100000;
    tty2.c_ospeed = 100000;
    ioctl(fd, TCSETS2, &tty2);

    return fd;
}

int main(void) {
    int fd = sbus_open_serial("/dev/serial0");
    if (fd < 0) return 1;

    sbus_parser_t parser;
    sbus_frame_t  frame;
    memset(&parser, 0, sizeof(parser));

    uint8_t byte;
    while (1) {
        ssize_t n = read(fd, &byte, 1);
        if (n == 1) {
            if (sbus_parse_byte(&parser, byte, &frame)) {
                printf("CH1=%4u CH2=%4u CH3=%4u CH4=%4u FS=%d FL=%d\n",
                       frame.channels[0], frame.channels[1],
                       frame.channels[2], frame.channels[3],
                       frame.failsafe, frame.frame_lost);
            }
        } else if (n < 0 && errno != EAGAIN) {
            perror("read");
            break;
        }
    }

    close(fd);
    return 0;
}
```

---

## 11. Rust Implementation

### 11.1 Core Types and Constants

```rust
// src/sbus.rs

/// Number of proportional servo channels
pub const NUM_CHANNELS: usize = 16;
/// SBUS frame size in bytes
pub const FRAME_SIZE: usize = 25;
/// Start byte (always 0x0F)
pub const START_BYTE: u8 = 0x0F;
/// End byte (always 0x00)
pub const END_BYTE: u8 = 0x00;

/// SBUS channel value range
pub const VALUE_MIN: u16 = 172;
pub const VALUE_MID: u16 = 992;
pub const VALUE_MAX: u16 = 1811;

bitflags::bitflags! {
    /// SBUS flags byte (byte 23)
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
    pub struct Flags: u8 {
        const CH17       = 0b1000_0000;
        const CH18       = 0b0100_0000;
        const FRAME_LOST = 0b0010_0000;
        const FAILSAFE   = 0b0001_0000;
    }
}

/// A decoded SBUS frame
#[derive(Debug, Clone, Copy, Default)]
pub struct Frame {
    /// 16 proportional channels, 11-bit values [172..1811]
    pub channels: [u16; NUM_CHANNELS],
    pub flags: Flags,
}

impl Frame {
    /// Is the link alive and providing fresh data?
    #[inline]
    pub fn is_valid(&self) -> bool {
        !self.flags.intersects(Flags::FAILSAFE | Flags::FRAME_LOST)
    }

    /// Map a channel to normalized [-1.0, +1.0]
    #[inline]
    pub fn normalized(&self, ch: usize) -> f32 {
        let v = self.channels[ch] as f32;
        let mid = VALUE_MID as f32;
        let range = (VALUE_MAX - VALUE_MIN) as f32 / 2.0;
        ((v - mid) / range).clamp(-1.0, 1.0)
    }

    /// Map a channel to servo pulse width [1000, 2000] µs
    #[inline]
    pub fn pulse_us(&self, ch: usize) -> u16 {
        let v = self.channels[ch].clamp(VALUE_MIN, VALUE_MAX);
        let pulse = 1000u32 + (v as u32 - VALUE_MIN as u32) * 1000
            / (VALUE_MAX - VALUE_MIN) as u32;
        pulse as u16
    }
}
```

### 11.2 Zero-Allocation Streaming Parser

```rust
/// Parse error kinds
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParseError {
    /// Start byte mismatch (0x0F expected)
    BadStartByte,
    /// End byte mismatch (0x00 expected)
    BadEndByte,
}

/// Streaming byte-by-byte SBUS parser (no_std compatible)
#[derive(Debug, Default)]
pub struct Parser {
    buf: [u8; FRAME_SIZE],
    idx: usize,
    synced: bool,
    pub frame_count: u32,
    pub error_count: u32,
}

impl Parser {
    /// Feed one byte. Returns `Some(Ok(Frame))` on valid complete frame,
    /// `Some(Err(...))` on framing error, `None` if more bytes needed.
    pub fn feed(&mut self, byte: u8) -> Option<Result<Frame, ParseError>> {
        if !self.synced {
            if byte == START_BYTE {
                self.buf[0] = byte;
                self.idx    = 1;
                self.synced = true;
            }
            return None;
        }

        self.buf[self.idx] = byte;
        self.idx += 1;

        if self.idx < FRAME_SIZE {
            return None;
        }

        // Frame complete — reset for next
        self.synced = false;
        self.idx    = 0;

        // Validate start/end bytes
        if self.buf[0] != START_BYTE {
            self.error_count += 1;
            return Some(Err(ParseError::BadStartByte));
        }
        if self.buf[24] != END_BYTE {
            self.error_count += 1;
            return Some(Err(ParseError::BadEndByte));
        }

        self.frame_count += 1;
        Some(Ok(decode_frame(&self.buf)))
    }

    /// Feed a slice of bytes, collecting all complete frames
    pub fn feed_slice<'a>(
        &mut self,
        bytes: &[u8],
        frames: &'a mut [Frame],
    ) -> &'a [Frame] {
        let mut count = 0;
        for &b in bytes {
            if count >= frames.len() {
                break;
            }
            if let Some(Ok(frame)) = self.feed(b) {
                frames[count] = frame;
                count += 1;
            }
        }
        &frames[..count]
    }
}

/// Decode 25-byte raw buffer → Frame (assumes bytes already validated)
fn decode_frame(buf: &[u8; FRAME_SIZE]) -> Frame {
    let d = &buf[1..]; // bytes 1..24 (payload + flags)

    let mut channels = [0u16; NUM_CHANNELS];

    // Explicit unrolled decode for performance on small MCUs
    channels[0]  = (( d[0]        | (d[1]  << 8)) & 0x07FF) as u16;
    channels[1]  = (( d[1]  >> 3  | (d[2]  << 5)) & 0x07FF) as u16;
    channels[2]  = (((d[2] as u32 >> 6) | ((d[3] as u32) << 2) | ((d[4] as u32) << 10)) & 0x07FF) as u16;
    channels[3]  = (( d[4]  >> 1  | (d[5]  << 7)) & 0x07FF) as u16;
    channels[4]  = (( d[5]  >> 4  | (d[6]  << 4)) & 0x07FF) as u16;
    channels[5]  = (((d[6] as u32 >> 7) | ((d[7] as u32) << 1) | ((d[8] as u32) << 9))  & 0x07FF) as u16;
    channels[6]  = (( d[8]  >> 2  | (d[9]  << 6)) & 0x07FF) as u16;
    channels[7]  = (( d[9]  >> 5  | (d[10] << 3)) & 0x07FF) as u16;
    channels[8]  = (( d[10]       | (d[11] << 8)) & 0x07FF) as u16;
    channels[9]  = (( d[11] >> 3  | (d[12] << 5)) & 0x07FF) as u16;
    channels[10] = (((d[12] as u32 >> 6) | ((d[13] as u32) << 2) | ((d[14] as u32) << 10)) & 0x07FF) as u16;
    channels[11] = (( d[14] >> 1  | (d[15] << 7)) & 0x07FF) as u16;
    channels[12] = (( d[15] >> 4  | (d[16] << 4)) & 0x07FF) as u16;
    channels[13] = (((d[16] as u32 >> 7) | ((d[17] as u32) << 1) | ((d[18] as u32) << 9))  & 0x07FF) as u16;
    channels[14] = (( d[18] >> 2  | (d[19] << 6)) & 0x07FF) as u16;
    channels[15] = (( d[19] >> 5  | (d[20] << 3)) & 0x07FF) as u16;

    let flags = Flags::from_bits_truncate(d[22]);

    Frame { channels, flags }
}
```

### 11.3 Generic Loop-Based Decoder (Alternative)

```rust
/// Generic 11-bit channel extraction using arithmetic — no_std safe
pub fn decode_channels_generic(payload: &[u8], channels: &mut [u16; NUM_CHANNELS]) {
    for ch in 0..NUM_CHANNELS {
        let bit_pos  = ch * 11;
        let byte_idx = bit_pos / 8;
        let bit_off  = bit_pos % 8;

        let raw = (payload[byte_idx] as u32)
                | ((payload[byte_idx + 1] as u32) << 8)
                | ((payload[byte_idx + 2] as u32) << 16);

        channels[ch] = ((raw >> bit_off) & 0x07FF) as u16;
    }
}
```

### 11.4 Frame Encoder

```rust
/// Encode a Frame into a 25-byte SBUS packet
pub fn encode_frame(frame: &Frame) -> [u8; FRAME_SIZE] {
    let mut out = [0u8; FRAME_SIZE];
    out[0]  = START_BYTE;
    out[24] = END_BYTE;

    let ch = &frame.channels;
    let d  = &mut out[1..24];

    d[0]  =   ch[0]        as u8;
    d[1]  =  (ch[0]  >> 8 | ch[1]  << 3) as u8;
    d[2]  =  (ch[1]  >> 5 | ch[2]  << 6) as u8;
    d[3]  =  (ch[2]  >> 2)               as u8;
    d[4]  =  (ch[2]  >> 10| ch[3]  << 1) as u8;
    d[5]  =  (ch[3]  >> 7 | ch[4]  << 4) as u8;
    d[6]  =  (ch[4]  >> 4 | ch[5]  << 7) as u8;
    d[7]  =  (ch[5]  >> 1)               as u8;
    d[8]  =  (ch[5]  >> 9 | ch[6]  << 2) as u8;
    d[9]  =  (ch[6]  >> 6 | ch[7]  << 5) as u8;
    d[10] =  (ch[7]  >> 3)               as u8;
    d[11] =   ch[8]        as u8;
    d[12] =  (ch[8]  >> 8 | ch[9]  << 3) as u8;
    d[13] =  (ch[9]  >> 5 | ch[10] << 6) as u8;
    d[14] =  (ch[10] >> 2)               as u8;
    d[15] =  (ch[10] >> 10| ch[11] << 1) as u8;
    d[16] =  (ch[11] >> 7 | ch[12] << 4) as u8;
    d[17] =  (ch[12] >> 4 | ch[13] << 7) as u8;
    d[18] =  (ch[13] >> 1)               as u8;
    d[19] =  (ch[13] >> 9 | ch[14] << 2) as u8;
    d[20] =  (ch[14] >> 6 | ch[15] << 5) as u8;
    d[21] =  (ch[15] >> 3)               as u8;
    d[22] =   frame.flags.bits();

    out
}
```

### 11.5 Async Serial Reader (tokio + serialport)

```rust
// Cargo.toml dependencies:
// tokio = { version = "1", features = ["full"] }
// serialport = "4"
// tokio-util = { version = "0.7", features = ["codec"] }
// bytes = "1"

use std::io;
use std::time::Duration;
use tokio::io::AsyncReadExt;

pub async fn run_sbus_reader(device: &str) -> io::Result<()> {
    // Open serial port at 100,000 baud, 8E2
    // Assumes hardware inverter; use serialport builder
    let port = serialport::new(device, 100_000)
        .data_bits(serialport::DataBits::Eight)
        .stop_bits(serialport::StopBits::Two)
        .parity(serialport::Parity::Even)
        .timeout(Duration::from_millis(20))
        .open()
        .expect("Failed to open serial port");

    let mut async_port = tokio_serial::SerialStream::open(
        &tokio_serial::new(device, 100_000)
            .data_bits(tokio_serial::DataBits::Eight)
            .stop_bits(tokio_serial::StopBits::Two)
            .parity(tokio_serial::Parity::Even),
    )?;

    let mut parser = Parser::default();
    let mut buf = [0u8; 64];

    loop {
        let n = async_port.read(&mut buf).await?;
        for &byte in &buf[..n] {
            match parser.feed(byte) {
                Some(Ok(frame)) if frame.is_valid() => {
                    println!(
                        "Throttle: {:.2}  Roll: {:.2}  Pitch: {:.2}  Yaw: {:.2}",
                        frame.normalized(2),
                        frame.normalized(0),
                        frame.normalized(1),
                        frame.normalized(3),
                    );
                }
                Some(Ok(_frame)) => {
                    eprintln!("Frame lost or failsafe!");
                }
                Some(Err(e)) => {
                    eprintln!("Parse error: {:?}", e);
                }
                None => {}
            }
        }
    }
}
```

### 11.6 no_std Embedded Example (RTIC on STM32)

```rust
// Cargo.toml (embedded target)
// [dependencies]
// cortex-m-rtic = "1"
// stm32f4xx-hal = { version = "0.20", features = ["stm32f405"] }
// nb = "1"

#![no_std]
#![no_main]

use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{config::Config, Event, Serial},
};

#[rtic::app(device = pac, peripherals = true)]
mod app {
    use super::*;
    use crate::sbus::{Frame, Parser};

    #[shared]
    struct Shared {
        frame: Frame,
    }

    #[local]
    struct Local {
        rx: stm32f4xx_hal::serial::Rx<pac::USART2>,
        parser: Parser,
    }

    #[init]
    fn init(cx: init::Context) -> (Shared, Local, init::Monotonics) {
        let rcc = cx.device.RCC.constrain();
        let clocks = rcc.cfgr.sysclk(168.MHz()).freeze();

        let gpiod = cx.device.GPIOD.split();
        let rx_pin = gpiod.pd6.into_alternate::<7>();

        // Configure USART2: 100,000 baud, 8E2
        let config = Config::default()
            .baudrate(100_000.bps())
            .wordlength_8()
            .parity_even()
            .stopbits(stm32f4xx_hal::serial::config::StopBits::STOP2);

        let mut serial = Serial::new(
            cx.device.USART2,
            (stm32f4xx_hal::serial::NoTx, rx_pin),
            config,
            &clocks,
        )
        .unwrap();

        // Invert RX signal in hardware register — no transistor needed
        unsafe {
            (*pac::USART2::ptr()).cr2.modify(|_, w| w.rxinv().set_bit());
        }

        serial.listen(Event::Rxne);
        let rx = serial.split().1;

        (
            Shared { frame: Frame::default() },
            Local { rx, parser: Parser::default() },
            init::Monotonics(),
        )
    }

    #[task(binds = USART2, local = [rx, parser], shared = [frame])]
    fn usart2_irq(cx: usart2_irq::Context) {
        if let Ok(byte) = cx.local.rx.read() {
            if let Some(Ok(f)) = cx.local.parser.feed(byte) {
                *cx.shared.frame.lock(|fr| *fr = f);
            }
        }
    }
}
```

---

## 12. Platform-Specific Notes

### 12.1 STM32 Family

| MCU            | UART Inversion Support  | Notes                            |
|----------------|-------------------------|----------------------------------|
| STM32F1        | ❌ No                   | External inverter required        |
| STM32F3/F4/F7  | ✅ Yes (`RXINV` bit)    | Set `CR2 |= USART_CR2_RXINV`     |
| STM32G0/G4/H7  | ✅ Yes                  | Same `RXINV` bit in CR2          |
| STM32L0/L4/L5  | ✅ Yes                  | Low-power variants also support  |

### 12.2 Raspberry Pi (Linux)

- Use `/dev/serial0` (hardware UART, not mini-UART `/dev/ttyAMA1`)
- Set 100,000 baud via `BOTHER` + `TCSETS2` ioctl (see Section 10.7)
- Even parity + 2 stop bits = `CS8 | PARENB | CSTOPB`
- Hardware inverter (transistor or 74HC04) **required** — RPi GPIO has no UART inversion support
- Disable Linux serial console on `/dev/serial0` first: `sudo raspi-config` → Interface Options → Serial

### 12.3 Arduino (AVR)

AVR UARTs do not support custom baud rates or signal inversion natively:
- Use `SoftwareSerial` library with inverted mode: `SoftwareSerial sbus(PIN, -1, true)` — the third argument enables inversion
- Configure baud rate as 100,000 bps

```cpp
#include <SoftwareSerial.h>
SoftwareSerial sbus_serial(10, -1, true); // RX=10, TX=none, inverted=true

void setup() {
    sbus_serial.begin(100000);
}
```

### 12.4 ESP32

```c
// ESP-IDF
uart_config_t cfg = {
    .baud_rate  = 100000,
    .data_bits  = UART_DATA_8_BITS,
    .parity     = UART_PARITY_EVEN,
    .stop_bits  = UART_STOP_BITS_2,
    .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
};
uart_param_config(UART_NUM_1, &cfg);
uart_set_pin(UART_NUM_1, UART_PIN_NO_CHANGE, GPIO_NUM_16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0);

// Invert RX in hardware — no external inverter needed
uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV);
```

### 12.5 RP2040 (Raspberry Pi Pico)

The RP2040's PL011 UART does not have a signal inversion register, but the **PIO** subsystem can bit-bang an inverted UART:

```
PIO state machine:
  - Configure pin as input
  - Wait for HIGH pulse (inverted start bit)
  - Sample 8 data bits + parity with correct timing
  - Push to RX FIFO
```

Alternatively, use a 74LVC1G04 or NPN transistor inverter and connect to the standard UART.

---

## 13. Failsafe & Lost-Frame Handling

Correct handling of SBUS flags is critical for safety in RC systems:

```c
void handle_sbus_frame(const sbus_frame_t *f) {
    if (f->failsafe) {
        /*
         * FAILSAFE: The RC link has been lost for an extended period.
         * The receiver is now outputting pre-programmed failsafe positions.
         * ACTION: Engage autonomous failsafe (e.g. Return-to-Home, land,
         *         disarm, hold position). Do NOT use channel values.
         */
        trigger_system_failsafe();
        return;
    }

    if (f->frame_lost) {
        /*
         * FRAME_LOST: A single frame was lost (glitch or interference).
         * The receiver is still connected but this packet is stale.
         * ACTION: Hold last known-good channel values. If consecutive
         *         frame_lost count exceeds threshold, enter failsafe.
         */
        consecutive_lost++;
        if (consecutive_lost > LOST_FRAME_THRESHOLD) {
            trigger_system_failsafe();
        }
        return;
    }

    consecutive_lost = 0;
    // Use f->channels[] normally
    apply_channels(f->channels);
}
```

### Recommended Thresholds

| Scenario                        | Recommendation                         |
|---------------------------------|----------------------------------------|
| Single frame lost               | Hold last values, do NOT act           |
| 10+ consecutive frames lost     | Enter hold/loiter mode                 |
| Failsafe flag set               | Immediately engage emergency procedure |
| No SBUS data for >100 ms        | Assume total link failure              |

---

## 14. SBUS2 Extension

**SBUS2** is a Futaba extension that adds telemetry slots in the inter-frame gap. The SBUS portion of the frame is **identical** to SBUS1 — the same 25-byte structure, same encoding. SBUS2 appends telemetry data after the 0x00 end byte in the gap period.

Key points:
- SBUS2 frames start with `0x0F` and are structurally identical to SBUS
- Telemetry slots appear in time slots after the frame end byte
- FrSky receivers do NOT implement SBUS2 telemetry (FrSky uses SPORT telemetry instead)
- For most applications, SBUS and SBUS2 decoders are interchangeable

---

## 15. Common Pitfalls

| Problem                          | Cause                                              | Fix                                                  |
|----------------------------------|----------------------------------------------------|------------------------------------------------------|
| Garbled / no data                | Forgot to invert signal                            | Add hardware inverter or enable UART `RXINV` bit     |
| All channels read 172 or 1811   | Wrong baud rate (e.g. 115,200 instead of 100,000) | Set exactly 100,000 baud                             |
| Occasional corrupt frames        | 1 stop bit instead of 2                            | Configure 2 stop bits                                |
| Channels offset by 1 or 2       | Even parity not configured                         | Enable even parity                                   |
| Parser never syncs               | Start byte check failing due to inversion          | Confirm inversion working; check oscilloscope        |
| Channels stuck after link loss   | Not checking `frame_lost` / `failsafe` flags       | Always check flags before using channel values       |
| STM32F1 issues                   | F1 UART has no RXINV                               | Use external transistor or 74HC04 inverter           |
| Frame rate seems half expected   | Receiver in normal mode vs fast mode               | Configure receiver for fast mode (14 ms → 7 ms)     |
| End byte sometimes 0x04          | Some receivers use 0x04 as end byte                | Accept both 0x00 and 0x04 as valid end bytes         |

> **Note**: A small number of receivers (particularly older Futaba) use `0x04` as the end byte rather than `0x00`. To maximise compatibility, validate only the start byte (0x0F) and rely on the 25-byte fixed length for framing.

---

## 16. Summary

SBUS is a compact, single-wire serial protocol for RC systems that encodes 16 proportional channels (11-bit resolution) plus 2 digital channels and status flags in a 25-byte frame transmitted at 100,000 baud with 8-bit / even parity / 2 stop bits — and crucially, **inverted signal polarity**.

### Key Technical Facts

| Aspect               | Detail                                                 |
|----------------------|--------------------------------------------------------|
| Frame size           | 25 bytes (fixed)                                       |
| Channels             | 16 proportional (11-bit) + 2 digital                  |
| Baud rate            | 100,000 bps (non-standard)                            |
| Framing              | 8E2 (8 data, even parity, 2 stop bits)                |
| Signal polarity      | **Inverted** — idle LOW, start bit = HIGH pulse        |
| Channel range        | 172 (min) → 992 (mid) → 1811 (max)                   |
| Update rate          | ~7 ms (fast mode) or ~14 ms (normal mode)             |
| Flags                | CH17, CH18, Frame Lost, Failsafe                       |

### Implementation Checklist

- ✅ Set UART to exactly 100,000 baud
- ✅ Configure 8 data bits, even parity, 2 stop bits
- ✅ Invert the signal (hardware inverter **or** MCU UART `RXINV` register bit)
- ✅ Parse 25-byte frames starting with 0x0F, ending with 0x00
- ✅ Unpack 16 × 11-bit channels from bytes 1–22 using bit shifting
- ✅ Check `frame_lost` and `failsafe` flags before using channel values
- ✅ Implement a timeout watchdog (>100 ms no frame = link failure)

SBUS's combination of multi-channel density, deterministic timing, and wide ecosystem support makes it the dominant choice for modern RC systems, despite the added complexity of the inverted UART signal. With hardware UART inversion now available on most ARM Cortex-M devices, this is a one-register configuration, making SBUS straightforward to integrate in both C/C++ and Rust embedded firmware.

---

*Document: 64_SBUS_Protocol.md | Topic: UART Protocols Series*