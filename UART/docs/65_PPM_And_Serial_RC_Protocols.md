# 65. PPM and Serial RC Protocols

**Protocols documented:** PPM, PWM, SBUS, IBUS, CRSF (Crossfire/ExpressLRS), and SUMD — with electrical specs, frame structures, and timing diagrams for each.

**C/C++ examples:**
- PPM decoder using STM32 HAL input capture (timer-based)
- Full SBUS decoder with bit-unpacking + UART ring-buffer receiver
- IBUS decoder as a C++ class with checksum validation
- CRSF decoder with DVB-S2 CRC8 and state machine framing
- SUMD decoder with CRC16-CCITT
- FC arming logic and a multi-protocol abstract `RCInput` interface

**Rust examples (`no_std` compatible):**
- SBUS, IBUS, and CRSF parsers using streaming state machines
- `embedded-hal` 1.0 integration pattern
- Dead-band and normalization utilities

**Also included:** a protocol comparison table, SBUS debug/troubleshooting guide, CRSF latency tips, and PPM noise considerations.

## Remote Control Protocols over UART for Drones and RC Vehicles

---

## Table of Contents

1. [Introduction](#introduction)
2. [PPM — Pulse Position Modulation](#1-ppm--pulse-position-modulation)
3. [PWM — Single-Channel Pulse Width Modulation](#2-pwm--single-channel-pulse-width-modulation)
4. [SBUS Protocol](#3-sbus-protocol)
5. [IBUS Protocol](#4-ibus-protocol)
6. [CRSF (Crossfire) Protocol](#5-crsf-crossfire-protocol)
7. [SUMD Protocol](#6-sumd-protocol)
8. [Protocol Comparison Table](#7-protocol-comparison-table)
9. [C/C++ Implementation Examples](#8-cc-implementation-examples)
10. [Rust Implementation Examples](#9-rust-implementation-examples)
11. [Flight Controller Integration](#10-flight-controller-integration)
12. [Troubleshooting & Common Pitfalls](#11-troubleshooting--common-pitfalls)
13. [Summary](#12-summary)

---

## Introduction

RC (Remote Control) systems used in drones, fixed-wing aircraft, ground vehicles, and boats rely on
a range of protocols to transmit control data from a receiver to a flight controller or ESC (Electronic
Speed Controller). These protocols vary in the number of channels they support, their electrical
characteristics, latency, error detection, and whether they are analog or fully digital.

The transport layer for many of these protocols is **UART** (Universal Asynchronous
Receiver/Transmitter), though some use dedicated timing hardware (PPM/PWM). Understanding each
protocol is essential for building reliable control systems.

### Typical RC Signal Chain

```
TX Radio ──► RF Link ──► Receiver ──► Flight Controller
                                          │
                              ┌───────────┴────────────┐
                              │  PPM / SBUS / IBUS /   │
                              │  CRSF / SUMD / PWM     │
                              └────────────────────────┘
```

---

## 1. PPM — Pulse Position Modulation

### What is PPM?

PPM (also called PPM-SUM or CPPM — Combined PPM) encodes **multiple RC channels** in a single
analog signal using the **timing between pulses**. It was the dominant multi-channel protocol before
serial digital protocols became standard.

### Signal Characteristics

| Parameter        | Value                         |
|------------------|-------------------------------|
| Channels         | Typically 8–16                |
| Pulse width      | 0.3 ms (sync), 1–2 ms (data)  |
| Frame period     | ~20 ms (50 Hz)                |
| Voltage level    | 3.3 V or 5 V logic            |
| Direction        | Unidirectional (receiver → FC)|

### Frame Structure

```
│← ~1.0 ms →│← ~1.5 ms →│← ~2.0 ms →│  ...  │←─── sync gap ───→│
│  ch1 pulse │  ch2 pulse │  ch3 pulse │       │  ≥ 2.5 ms        │
▔            ▔            ▔                    ▔
```

Each channel is represented by the **gap between rising edges**:
- 1000 µs → minimum servo travel
- 1500 µs → center / neutral
- 2000 µs → maximum servo travel

The frame ends with a sync pulse (gap > 2.5 ms). Full 8-channel frame = ~20 ms.

### Reading PPM in Hardware

PPM requires either:
- An **input capture timer** (e.g., STM32 TIM in IC mode), or
- A **GPIO interrupt** measuring time between rising edges

---

## 2. PWM — Single-Channel Pulse Width Modulation

PWM is the **simplest** RC protocol — one wire per channel. Each wire carries a 50 Hz signal where
the pulse width encodes the servo position.

| Parameter     | Value                  |
|---------------|------------------------|
| Channels      | 1 per wire             |
| Pulse width   | 1–2 ms (50 Hz frame)   |
| Latency       | ~20 ms                 |
| Wiring        | 1 signal wire/channel  |

Despite being outdated for multi-channel use, PWM is still used to drive individual servos and
brushless motor ESCs.

---

## 3. SBUS Protocol

### Overview

SBUS (Futaba S-Bus) is the most widely used **serial digital RC protocol**. It is a serial protocol
that encodes **16 analog channels + 2 digital channels** in a compact binary frame.

### Electrical Characteristics

| Parameter       | Value                     |
|-----------------|---------------------------|
| Baud rate       | **100,000 bps**           |
| Data bits       | 8                         |
| Stop bits       | 2                         |
| Parity          | Even                      |
| Signal polarity | **Inverted** (active-low) |
| Voltage         | 3.3 V (5 V tolerant)      |

> ⚠️ **Important:** SBUS is electrically inverted. Most UART hardware requires a hardware inverter
> or the UART's inversion mode to be enabled.

### Frame Structure (25 bytes)

```
Byte  0:    0x0F  (start byte)
Bytes 1–22: 11-bit channel data, bit-packed across 22 bytes
Byte 23:    Flags byte (failsafe, frame lost, CH17, CH18)
Byte 24:    0x00  (end byte)
```

Channel data is **bit-packed** — each of the 16 channels uses exactly 11 bits:
- Range: 0–2047 (11-bit)
- Center: 1024
- Min/Max: typically mapped 172–1811

### SBUS Packet Unpacking (bit layout)

```
Channels packed into bytes 1–22:
CH1  = bits  0–10
CH2  = bits 11–21
CH3  = bits 22–32
...
CH16 = bits 165–175
```

---

## 4. IBUS Protocol

### Overview

IBUS (FlySky I-Bus) is a **bidirectional** digital serial protocol from FlySky, supporting both
channel data and sensor telemetry.

### Electrical Characteristics

| Parameter    | Value                       |
|--------------|-----------------------------|
| Baud rate    | **115,200 bps**             |
| Data bits    | 8                           |
| Stop bits    | 1                           |
| Parity       | None                        |
| Polarity     | Normal (non-inverted)       |
| Voltage      | 3.3 V / 5 V                 |

### Channel Frame Structure (32 bytes)

```
Byte  0:    0x20  (length, always 32)
Byte  1:    0x40  (command: channel data)
Bytes 2–29: 14 channels × 2 bytes each (little-endian uint16)
Bytes 30–31: Checksum (0xFFFF - sum of bytes 0–29)
```

- Channel range: 1000–2000 µs (matches PWM)
- Up to 14 channels per frame
- Checksum is a simple 16-bit sum complement

### IBUS Sensor Protocol

IBUS also supports sensor polling for telemetry (voltage, RPM, GPS, etc.) on the same wire using
half-duplex RS232-style communication.

---

## 5. CRSF (Crossfire) Protocol

### Overview

CRSF (TBS Crossfire Serial Protocol) is a **high-performance, low-latency** bidirectional protocol
used in long-range RC systems. It is also used by ExpressLRS (ELRS), the most popular open-source
RC link.

### Electrical Characteristics

| Parameter    | Value                        |
|--------------|------------------------------|
| Baud rate    | **420,000 bps** (or 115,200) |
| Data bits    | 8                            |
| Stop bits    | 1                            |
| Parity       | None                         |
| Polarity     | Normal                       |
| Direction    | **Bidirectional**            |

### Frame Structure

```
Byte 0:    Sync byte (0xC8 = FC address, or 0xEE = transmitter)
Byte 1:    Frame length (N bytes following)
Byte 2:    Frame type
Bytes 3…N-1: Payload
Byte N:    CRC8 (DVB-S2 polynomial)
```

### Channel Frame Type (0x16)

```
Payload: 22 bytes
  Channels 1–16: 11-bit packed (same packing as SBUS)
  Range: 172–1811 (maps to 988–2012 µs)
```

### Key CRSF Advantages

- **Update rate:** up to 500 Hz with ELRS
- **Telemetry:** link stats, battery voltage, GPS, attitude all returned to TX
- **CRC:** reliable error detection
- **Sync:** simple and fast re-sync

---

## 6. SUMD Protocol

SUMD (Graupner HoTT Sum signal Digital) is a digital sum signal from Graupner/Robbe.

| Parameter    | Value              |
|--------------|--------------------|
| Baud rate    | 115,200 bps        |
| Channels     | Up to 32           |
| Frame length | 2 + 2×N + 2 bytes  |
| CRC          | CRC16-CCITT        |

### Frame Structure

```
Byte 0:    0xA8  (start byte)
Byte 1:    Status (0x01 = live, 0x81 = failsafe)
Byte 2:    N channels
Bytes 3…:  N × 2-byte big-endian channel values (×8 for µs conversion)
Last 2:    CRC16-CCITT
```

---

## 7. Protocol Comparison Table

| Protocol | Baud     | Channels | Latency  | Bidirectional | Inverted | CRC     |
|----------|----------|----------|----------|---------------|----------|---------|
| PPM      | Analog   | 8–16     | ~20 ms   | No            | No       | None    |
| PWM      | Analog   | 1        | ~20 ms   | No            | No       | None    |
| SBUS     | 100,000  | 18       | ~6–9 ms  | No            | **Yes**  | None    |
| IBUS     | 115,200  | 14       | ~7 ms    | Yes           | No       | Sum16   |
| CRSF     | 420,000  | 16       | ~1–3 ms  | **Yes**       | No       | CRC8    |
| SUMD     | 115,200  | 32       | ~5–8 ms  | No            | No       | CRC16   |

---

## 8. C/C++ Implementation Examples

### 8.1 PPM Decoding (STM32 HAL — Input Capture)

```c
// ppm_decoder.c
// STM32 Timer configured in Input Capture mode, rising edge

#include "stm32f4xx_hal.h"

#define PPM_CHANNELS     8
#define PPM_SYNC_MIN_US  2500   // Minimum gap to detect sync pulse
#define PPM_PULSE_MIN_US 800
#define PPM_PULSE_MAX_US 2200

static uint16_t ppm_channels[PPM_CHANNELS];
static uint8_t  ppm_channel_index = 0;
static uint32_t ppm_last_capture  = 0;
static bool     ppm_valid         = false;

// Called from TIMx_IRQHandler or HAL_TIM_IC_CaptureCallback
void PPM_InputCapture_Callback(TIM_HandleTypeDef *htim)
{
    uint32_t capture = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
    uint32_t pulse_us;

    // Handle timer wrap-around (16-bit timer at 1 MHz = 1 µs/tick)
    if (capture >= ppm_last_capture) {
        pulse_us = capture - ppm_last_capture;
    } else {
        pulse_us = (0xFFFF - ppm_last_capture) + capture + 1;
    }
    ppm_last_capture = capture;

    if (pulse_us >= PPM_SYNC_MIN_US) {
        // Sync gap detected — reset channel index
        if (ppm_channel_index == PPM_CHANNELS) {
            ppm_valid = true;  // Full frame received
        }
        ppm_channel_index = 0;
    }
    else if (ppm_channel_index < PPM_CHANNELS) {
        if (pulse_us >= PPM_PULSE_MIN_US && pulse_us <= PPM_PULSE_MAX_US) {
            ppm_channels[ppm_channel_index++] = (uint16_t)pulse_us;
        }
    }
}

uint16_t PPM_GetChannel(uint8_t ch)
{
    if (ch >= PPM_CHANNELS) return 1500;
    return ppm_valid ? ppm_channels[ch] : 1500;
}

bool PPM_IsValid(void) { return ppm_valid; }
```

---

### 8.2 SBUS Decoder (C — Any UART)

```c
// sbus.h
#ifndef SBUS_H
#define SBUS_H

#include <stdint.h>
#include <stdbool.h>

#define SBUS_FRAME_LEN    25
#define SBUS_NUM_CHANNELS 16
#define SBUS_START_BYTE   0x0F
#define SBUS_END_BYTE     0x00

typedef struct {
    uint16_t channels[SBUS_NUM_CHANNELS]; // 0–2047 (11-bit)
    bool     ch17;
    bool     ch18;
    bool     failsafe;
    bool     frame_lost;
    bool     valid;
} SBUS_Frame_t;

bool SBUS_Parse(const uint8_t *buf, uint16_t len, SBUS_Frame_t *out);
uint16_t SBUS_ToMicros(uint16_t sbus_val);

#endif // SBUS_H
```

```c
// sbus.c
#include "sbus.h"
#include <string.h>

// Parse a 25-byte SBUS frame.
// Returns true if frame is structurally valid.
bool SBUS_Parse(const uint8_t *buf, uint16_t len, SBUS_Frame_t *out)
{
    if (len < SBUS_FRAME_LEN) return false;
    if (buf[0] != SBUS_START_BYTE) return false;
    if (buf[24] != SBUS_END_BYTE) return false;

    memset(out, 0, sizeof(*out));

    // Bit-unpack 16 × 11-bit channels from bytes 1–22
    // Each channel: 11 bits packed consecutively, LSB first
    const uint8_t *data = &buf[1];
    uint32_t       bits = 0;
    int            bit_count = 0;
    int            byte_idx  = 0;

    for (int ch = 0; ch < SBUS_NUM_CHANNELS; ch++) {
        // Accumulate bits from stream
        while (bit_count < 11) {
            bits |= ((uint32_t)data[byte_idx++] << bit_count);
            bit_count += 8;
        }
        out->channels[ch] = (uint16_t)(bits & 0x07FF); // mask 11 bits
        bits >>= 11;
        bit_count -= 11;
    }

    // Flags byte (byte index 23 = buf[23])
    uint8_t flags  = buf[23];
    out->ch17       = (flags & 0x01) != 0;
    out->ch18       = (flags & 0x02) != 0;
    out->frame_lost = (flags & 0x04) != 0;
    out->failsafe   = (flags & 0x08) != 0;
    out->valid      = !out->failsafe && !out->frame_lost;

    return true;
}

// Convert 11-bit SBUS value to microseconds
// SBUS 172 → 1000 µs, 1024 → 1500 µs, 1811 → 2000 µs
uint16_t SBUS_ToMicros(uint16_t sbus_val)
{
    // Linear mapping: y = (x - 172) * (1000/1639) + 1000
    // Using integer arithmetic:
    int32_t us = ((int32_t)(sbus_val - 172) * 1000) / 1639 + 1000;
    if (us < 900)  us = 900;
    if (us > 2100) us = 2100;
    return (uint16_t)us;
}
```

```c
// SBUS UART ring-buffer receiver (interrupt-driven, STM32 HAL example)
// sbus_uart.c

#include "sbus.h"
#include "stm32f4xx_hal.h"

#define RX_BUF_SIZE 64

static uint8_t     rx_buf[RX_BUF_SIZE];
static uint8_t     frame_buf[SBUS_FRAME_LEN];
static uint8_t     frame_pos  = 0;
static SBUS_Frame_t g_sbus    = {0};

extern UART_HandleTypeDef huart1;

void SBUS_UART_Init(void)
{
    // UART configured externally: 100000 baud, 8E2, inverted
    // Start DMA or interrupt receive
    HAL_UART_Receive_IT(&huart1, rx_buf, 1); // byte-by-byte
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;

    uint8_t byte = rx_buf[0];

    // Simple framing: look for start byte to re-sync
    if (frame_pos == 0 && byte != SBUS_START_BYTE) {
        HAL_UART_Receive_IT(huart, rx_buf, 1);
        return;
    }

    frame_buf[frame_pos++] = byte;

    if (frame_pos == SBUS_FRAME_LEN) {
        SBUS_Parse(frame_buf, SBUS_FRAME_LEN, &g_sbus);
        frame_pos = 0;
    }

    HAL_UART_Receive_IT(huart, rx_buf, 1);
}

const SBUS_Frame_t* SBUS_GetFrame(void) { return &g_sbus; }
```

---

### 8.3 IBUS Decoder (C++)

```cpp
// ibus.hpp
#pragma once
#include <cstdint>
#include <array>
#include <optional>

class IBUSDecoder {
public:
    static constexpr uint8_t  FRAME_LEN     = 32;
    static constexpr uint8_t  CMD_CHANNELS  = 0x40;
    static constexpr uint16_t CH_MIN        = 1000;
    static constexpr uint16_t CH_MAX        = 2000;
    static constexpr uint16_t CH_CENTER     = 1500;
    static constexpr int      NUM_CHANNELS  = 14;

    struct Frame {
        std::array<uint16_t, NUM_CHANNELS> channels{};
        bool valid = false;
    };

    // Feed one byte at a time; returns a completed Frame when ready.
    std::optional<Frame> feed(uint8_t byte);

private:
    uint8_t  buf_[FRAME_LEN]{};
    uint8_t  idx_  = 0;

    bool verify_checksum() const;
    Frame decode() const;
};
```

```cpp
// ibus.cpp
#include "ibus.hpp"
#include <numeric>

std::optional<IBUSDecoder::Frame> IBUSDecoder::feed(uint8_t byte)
{
    // Re-sync: wait for 0x20 (length) at position 0
    if (idx_ == 0 && byte != FRAME_LEN) return std::nullopt;
    // Byte 1 must be the command byte
    if (idx_ == 1 && byte != CMD_CHANNELS) {
        idx_ = 0;
        return std::nullopt;
    }

    buf_[idx_++] = byte;

    if (idx_ == FRAME_LEN) {
        idx_ = 0;
        if (verify_checksum()) {
            return decode();
        }
    }
    return std::nullopt;
}

bool IBUSDecoder::verify_checksum() const
{
    // Checksum = 0xFFFF - sum(bytes 0..29)
    uint16_t sum = 0;
    for (int i = 0; i < 30; ++i)
        sum += buf_[i];

    uint16_t expected = 0xFFFF - sum;
    uint16_t actual   = (uint16_t)buf_[30] | ((uint16_t)buf_[31] << 8);
    return expected == actual;
}

IBUSDecoder::Frame IBUSDecoder::decode() const
{
    Frame f;
    f.valid = true;
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        uint16_t val = (uint16_t)buf_[2 + ch * 2]
                     | ((uint16_t)buf_[3 + ch * 2] << 8);
        // Clamp to valid range
        if (val < CH_MIN) val = CH_MIN;
        if (val > CH_MAX) val = CH_MAX;
        f.channels[ch] = val;
    }
    return f;
}
```

---

### 8.4 CRSF Decoder (C++)

```cpp
// crsf.hpp
#pragma once
#include <cstdint>
#include <array>
#include <optional>
#include <functional>

class CRSFDecoder {
public:
    static constexpr uint8_t SYNC_BYTE       = 0xC8;
    static constexpr uint8_t FRAME_TYPE_CH   = 0x16; // RC Channels
    static constexpr int     NUM_CHANNELS    = 16;
    static constexpr int     MAX_FRAME_LEN   = 64;

    struct RCFrame {
        std::array<uint16_t, NUM_CHANNELS> channels{}; // µs values
        bool valid = false;
    };

    std::optional<RCFrame> feed(uint8_t byte);

    // Convert raw CRSF value (0–2047, center 992) to microseconds
    static uint16_t toMicros(uint16_t raw);

private:
    enum class State { SYNC, LEN, PAYLOAD };

    State   state_   = State::SYNC;
    uint8_t buf_[MAX_FRAME_LEN]{};
    uint8_t frame_len_ = 0;
    uint8_t idx_       = 0;

    static uint8_t crc8_dvb_s2(const uint8_t *data, uint8_t len);
    RCFrame decode_channels() const;
};
```

```cpp
// crsf.cpp
#include "crsf.hpp"

// CRC8 with DVB-S2 polynomial 0xD5
uint8_t CRSFDecoder::crc8_dvb_s2(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? (crc << 1) ^ 0xD5 : (crc << 1);
    }
    return crc;
}

std::optional<CRSFDecoder::RCFrame> CRSFDecoder::feed(uint8_t byte)
{
    switch (state_) {
    case State::SYNC:
        if (byte == SYNC_BYTE) {
            buf_[0] = byte;
            idx_    = 1;
            state_  = State::LEN;
        }
        break;

    case State::LEN:
        frame_len_ = byte;  // Length of remaining bytes (type + payload + crc)
        if (frame_len_ < 2 || frame_len_ > MAX_FRAME_LEN - 2) {
            state_ = State::SYNC;
        } else {
            buf_[idx_++] = byte;
            state_ = State::PAYLOAD;
        }
        break;

    case State::PAYLOAD:
        buf_[idx_++] = byte;
        if (idx_ == (uint8_t)(frame_len_ + 2)) { // sync + len + payload
            // Validate CRC: covers type + payload, excludes sync, len, crc itself
            uint8_t expected_crc = buf_[idx_ - 1];
            uint8_t computed_crc = crc8_dvb_s2(&buf_[2], frame_len_ - 1);
            state_ = State::SYNC;
            idx_   = 0;

            if (expected_crc == computed_crc && buf_[2] == FRAME_TYPE_CH) {
                return decode_channels();
            }
        }
        break;
    }
    return std::nullopt;
}

CRSFDecoder::RCFrame CRSFDecoder::decode_channels() const
{
    // Payload starts at buf_[3], 22 bytes of 11-bit packed channels
    const uint8_t *p = &buf_[3];
    RCFrame frame;
    frame.valid = true;

    uint32_t bits     = 0;
    int      bit_cnt  = 0;
    int      byte_idx = 0;

    for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        while (bit_cnt < 11) {
            bits    |= ((uint32_t)p[byte_idx++] << bit_cnt);
            bit_cnt += 8;
        }
        uint16_t raw = bits & 0x7FF;
        bits    >>= 11;
        bit_cnt  -= 11;
        frame.channels[ch] = toMicros(raw);
    }
    return frame;
}

// CRSF raw 172–1811 → 988–2012 µs (linear)
uint16_t CRSFDecoder::toMicros(uint16_t raw)
{
    int32_t us = ((int32_t)(raw - 172) * 1024) / 1639 + 988;
    if (us < 800)  us = 800;
    if (us > 2200) us = 2200;
    return (uint16_t)us;
}
```

---

### 8.5 SUMD Decoder (C)

```c
// sumd.h
#ifndef SUMD_H
#define SUMD_H

#include <stdint.h>
#include <stdbool.h>

#define SUMD_MAX_CHANNELS 32
#define SUMD_START_BYTE   0xA8
#define SUMD_LIVE         0x01
#define SUMD_FAILSAFE     0x81

typedef struct {
    uint8_t  n_channels;
    uint16_t channels[SUMD_MAX_CHANNELS]; // in µs × 8
    bool     failsafe;
    bool     valid;
} SUMD_Frame_t;

bool SUMD_Parse(const uint8_t *buf, uint16_t len, SUMD_Frame_t *out);

#endif
```

```c
// sumd.c
#include "sumd.h"
#include <string.h>

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0;
    for (uint16_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

bool SUMD_Parse(const uint8_t *buf, uint16_t len, SUMD_Frame_t *out)
{
    if (len < 5) return false;
    if (buf[0] != SUMD_START_BYTE) return false;

    uint8_t status     = buf[1];
    uint8_t n_channels = buf[2];
    uint16_t expected_len = 3 + n_channels * 2 + 2;

    if (len < expected_len) return false;
    if (n_channels > SUMD_MAX_CHANNELS) return false;

    // Validate CRC16-CCITT (over all bytes except the 2 CRC bytes)
    uint16_t crc_actual   = ((uint16_t)buf[expected_len - 2] << 8) | buf[expected_len - 1];
    uint16_t crc_computed = crc16_ccitt(buf, expected_len - 2);
    if (crc_actual != crc_computed) return false;

    out->failsafe  = (status == SUMD_FAILSAFE);
    out->valid     = !out->failsafe;
    out->n_channels = n_channels;

    for (uint8_t ch = 0; ch < n_channels; ++ch) {
        // 2 bytes big-endian; divide by 8 to get µs (range: 8000–16000 → 1000–2000 µs)
        uint16_t raw = ((uint16_t)buf[3 + ch * 2] << 8) | buf[4 + ch * 2];
        out->channels[ch] = raw / 8;  // convert to µs
    }
    return true;
}
```

---

## 9. Rust Implementation Examples

### 9.1 SBUS Decoder in Rust (`no_std` compatible)

```rust
// src/sbus.rs
// no_std compatible SBUS decoder using heapless types

#![allow(dead_code)]

/// SBUS frame is always 25 bytes
pub const SBUS_FRAME_LEN: usize = 25;
pub const SBUS_NUM_CHANNELS: usize = 16;
pub const SBUS_START_BYTE: u8 = 0x0F;
pub const SBUS_END_BYTE: u8 = 0x00;

#[derive(Debug, Clone, Default)]
pub struct SbusFrame {
    /// 16 channels, raw 11-bit values (0–2047)
    pub channels: [u16; SBUS_NUM_CHANNELS],
    pub ch17: bool,
    pub ch18: bool,
    pub frame_lost: bool,
    pub failsafe: bool,
}

impl SbusFrame {
    /// Parse a 25-byte SBUS buffer.
    pub fn parse(buf: &[u8; SBUS_FRAME_LEN]) -> Option<Self> {
        if buf[0] != SBUS_START_BYTE || buf[24] != SBUS_END_BYTE {
            return None;
        }

        let mut frame = SbusFrame::default();
        let data = &buf[1..23]; // 22 bytes of packed channel data

        // Unpack 16 × 11-bit channels from 22 bytes (LSB first)
        let mut bits: u32 = 0;
        let mut bit_count: usize = 0;
        let mut byte_idx: usize = 0;

        for ch in 0..SBUS_NUM_CHANNELS {
            while bit_count < 11 {
                bits |= (data[byte_idx] as u32) << bit_count;
                byte_idx += 1;
                bit_count += 8;
            }
            frame.channels[ch] = (bits & 0x7FF) as u16;
            bits >>= 11;
            bit_count -= 11;
        }

        let flags = buf[23];
        frame.ch17       = (flags & 0x01) != 0;
        frame.ch18       = (flags & 0x02) != 0;
        frame.frame_lost = (flags & 0x04) != 0;
        frame.failsafe   = (flags & 0x08) != 0;

        Some(frame)
    }

    /// Convert a raw 11-bit SBUS channel value to microseconds.
    /// Maps 172 → 1000 µs, 1024 → 1500 µs, 1811 → 2000 µs.
    pub fn to_micros(raw: u16) -> u16 {
        let us = (raw as i32 - 172) * 1000 / 1639 + 1000;
        us.clamp(900, 2100) as u16
    }
}

/// State machine for streaming SBUS bytes (ring-buffer style)
pub struct SbusParser {
    buf: [u8; SBUS_FRAME_LEN],
    pos: usize,
}

impl SbusParser {
    pub const fn new() -> Self {
        Self { buf: [0u8; SBUS_FRAME_LEN], pos: 0 }
    }

    /// Feed one byte; returns Some(SbusFrame) when a complete valid frame is parsed.
    pub fn feed(&mut self, byte: u8) -> Option<SbusFrame> {
        // Re-sync: only start collecting when we see the start byte
        if self.pos == 0 && byte != SBUS_START_BYTE {
            return None;
        }

        self.buf[self.pos] = byte;
        self.pos += 1;

        if self.pos == SBUS_FRAME_LEN {
            self.pos = 0;
            // Ownership trick: copy buffer for parsing
            let buf = self.buf;
            return SbusFrame::parse(&buf);
        }
        None
    }

    pub fn reset(&mut self) {
        self.pos = 0;
    }
}
```

---

### 9.2 IBUS Decoder in Rust

```rust
// src/ibus.rs

pub const IBUS_FRAME_LEN: usize = 32;
pub const IBUS_CMD_CHANNELS: u8 = 0x40;
pub const IBUS_NUM_CHANNELS: usize = 14;

#[derive(Debug, Clone, Default)]
pub struct IBusFrame {
    pub channels: [u16; IBUS_NUM_CHANNELS], // 1000–2000 µs
    pub valid: bool,
}

fn ibus_checksum(buf: &[u8; IBUS_FRAME_LEN]) -> bool {
    let sum: u16 = buf[..30].iter().map(|&b| b as u16).sum();
    let expected: u16 = 0xFFFF - sum;
    let actual = (buf[30] as u16) | ((buf[31] as u16) << 8);
    expected == actual
}

impl IBusFrame {
    pub fn parse(buf: &[u8; IBUS_FRAME_LEN]) -> Option<Self> {
        if buf[0] != IBUS_FRAME_LEN as u8 {
            return None;
        }
        if buf[1] != IBUS_CMD_CHANNELS {
            return None;
        }
        if !ibus_checksum(buf) {
            return None;
        }

        let mut frame = IBusFrame::default();
        frame.valid = true;

        for ch in 0..IBUS_NUM_CHANNELS {
            let lo = buf[2 + ch * 2] as u16;
            let hi = buf[3 + ch * 2] as u16;
            let val = lo | (hi << 8);
            frame.channels[ch] = val.clamp(1000, 2000);
        }
        Some(frame)
    }
}

pub struct IBusParser {
    buf: [u8; IBUS_FRAME_LEN],
    pos: usize,
}

impl IBusParser {
    pub const fn new() -> Self {
        Self { buf: [0u8; IBUS_FRAME_LEN], pos: 0 }
    }

    pub fn feed(&mut self, byte: u8) -> Option<IBusFrame> {
        if self.pos == 0 && byte != IBUS_FRAME_LEN as u8 {
            return None;
        }
        if self.pos == 1 && byte != IBUS_CMD_CHANNELS {
            self.pos = 0;
            return None;
        }

        self.buf[self.pos] = byte;
        self.pos += 1;

        if self.pos == IBUS_FRAME_LEN {
            self.pos = 0;
            let buf = self.buf;
            return IBusFrame::parse(&buf);
        }
        None
    }
}
```

---

### 9.3 CRSF Decoder in Rust

```rust
// src/crsf.rs

pub const CRSF_SYNC: u8 = 0xC8;
pub const CRSF_FRAMETYPE_RC: u8 = 0x16;
pub const CRSF_NUM_CHANNELS: usize = 16;
pub const CRSF_MAX_PAYLOAD: usize = 64;

#[derive(Debug, Clone, Default)]
pub struct CrsfRcFrame {
    pub channels: [u16; CRSF_NUM_CHANNELS], // µs values
}

/// CRC8 with DVB-S2 polynomial 0xD5
fn crc8_dvb_s2(data: &[u8]) -> u8 {
    let mut crc: u8 = 0;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            crc = if crc & 0x80 != 0 {
                (crc << 1) ^ 0xD5
            } else {
                crc << 1
            };
        }
    }
    crc
}

fn decode_channels(payload: &[u8]) -> CrsfRcFrame {
    let mut frame = CrsfRcFrame::default();
    let mut bits: u32 = 0;
    let mut bit_cnt: usize = 0;
    let mut byte_idx: usize = 0;

    for ch in 0..CRSF_NUM_CHANNELS {
        while bit_cnt < 11 {
            if byte_idx < payload.len() {
                bits |= (payload[byte_idx] as u32) << bit_cnt;
                byte_idx += 1;
            }
            bit_cnt += 8;
        }
        let raw = (bits & 0x7FF) as u16;
        bits >>= 11;
        bit_cnt -= 11;

        // Map 172–1811 → 988–2012 µs
        let us = ((raw as i32 - 172) * 1024 / 1639 + 988).clamp(800, 2200) as u16;
        frame.channels[ch] = us;
    }
    frame
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum State { Sync, Len, Payload }

pub struct CrsfParser {
    buf: [u8; CRSF_MAX_PAYLOAD],
    state: State,
    frame_len: usize,
    idx: usize,
}

impl CrsfParser {
    pub const fn new() -> Self {
        Self {
            buf: [0u8; CRSF_MAX_PAYLOAD],
            state: State::Sync,
            frame_len: 0,
            idx: 0,
        }
    }

    pub fn feed(&mut self, byte: u8) -> Option<CrsfRcFrame> {
        match self.state {
            State::Sync => {
                if byte == CRSF_SYNC {
                    self.buf[0] = byte;
                    self.idx = 1;
                    self.state = State::Len;
                }
            }
            State::Len => {
                self.frame_len = byte as usize;
                if self.frame_len < 2 || self.frame_len > CRSF_MAX_PAYLOAD - 2 {
                    self.state = State::Sync;
                } else {
                    self.buf[self.idx] = byte;
                    self.idx += 1;
                    self.state = State::Payload;
                }
            }
            State::Payload => {
                self.buf[self.idx] = byte;
                self.idx += 1;

                if self.idx == self.frame_len + 2 {
                    self.state = State::Sync;
                    let frame_type = self.buf[2];
                    let payload    = &self.buf[3..self.idx - 1];
                    let crc_rx     = self.buf[self.idx - 1];
                    let crc_calc   = crc8_dvb_s2(&self.buf[2..self.idx - 1]);

                    if crc_rx == crc_calc && frame_type == CRSF_FRAMETYPE_RC {
                        return Some(decode_channels(payload));
                    }
                }
            }
        }
        None
    }
}
```

---

### 9.4 Using the Decoders with `embedded-hal` UART (Rust)

```rust
// src/main.rs — example using embedded-hal 1.0 + RTIC or bare metal

use core::convert::Infallible;
use embedded_hal::serial::Read;

use crate::crsf::CrsfParser;
use crate::sbus::SbusParser;

/// Generic reader that feeds bytes to any parser.
/// Works with any embedded-hal UART peripheral.
pub fn poll_uart_sbus<U>(uart: &mut U, parser: &mut SbusParser)
    -> Option<crate::sbus::SbusFrame>
where
    U: Read<u8, Error = Infallible>,
{
    loop {
        match uart.read() {
            Ok(byte) => {
                if let Some(frame) = parser.feed(byte) {
                    return Some(frame);
                }
            }
            Err(_) => break, // No more bytes available
        }
    }
    None
}

/// Example: read CRSF channel and map to throttle
pub fn get_throttle(frame: &crate::crsf::CrsfRcFrame) -> f32 {
    let ch3 = frame.channels[2]; // Channel 3 (0-indexed: 2) = Throttle
    // Normalize to 0.0–1.0
    ((ch3 as f32 - 988.0) / (2012.0 - 988.0)).clamp(0.0, 1.0)
}

/// Example: dead-band function for sticks
pub fn apply_deadband(value: u16, center: u16, deadband: u16) -> i32 {
    let v = value as i32 - center as i32;
    if v.abs() <= deadband as i32 { 0 } else { v }
}
```

---

## 10. Flight Controller Integration

### Arming Logic with SBUS

A common safety requirement is that the throttle channel must be at minimum and a specific switch
must be activated to arm the motors.

```c
// fc_arming.c — Simplified FC arming logic using SBUS

#include "sbus.h"

#define CH_THROTTLE   2   // SBUS channel index (0-based)
#define CH_ARM        4   // Arming switch
#define ARM_THRESHOLD 1700
#define THROTTLE_MIN  1050

typedef enum { DISARMED, ARMING, ARMED } ArmState;

static ArmState arm_state = DISARMED;

void FC_UpdateArming(const SBUS_Frame_t *frame)
{
    if (!frame->valid || frame->failsafe) {
        arm_state = DISARMED;
        return;
    }

    uint16_t throttle = SBUS_ToMicros(frame->channels[CH_THROTTLE]);
    uint16_t arm_sw   = SBUS_ToMicros(frame->channels[CH_ARM]);

    switch (arm_state) {
    case DISARMED:
        if (arm_sw > ARM_THRESHOLD && throttle < THROTTLE_MIN)
            arm_state = ARMING;
        break;
    case ARMING:
        // Require arm switch held for 500 ms (implement with a timer)
        arm_state = ARMED;
        break;
    case ARMED:
        if (arm_sw < ARM_THRESHOLD)
            arm_state = DISARMED;
        break;
    }
}

int FC_IsArmed(void) { return arm_state == ARMED; }
```

### Multi-Protocol Abstraction (C++)

```cpp
// rc_input.hpp — Abstract RC input interface

#pragma once
#include <cstdint>
#include <array>

class RCInput {
public:
    static constexpr int MAX_CHANNELS = 18;

    virtual ~RCInput() = default;

    /// Update internal state from UART (call frequently in main loop or interrupt).
    virtual void update() = 0;

    /// Get channel value in µs (1000–2000, center 1500).
    virtual uint16_t getChannel(int ch) const = 0;

    /// Returns true if signal is present and valid.
    virtual bool isValid() const = 0;

    /// Returns true if receiver signals failsafe.
    virtual bool isFailsafe() const = 0;

    /// Utility: normalize channel to -1.0 to +1.0
    float normalized(int ch) const {
        return (static_cast<float>(getChannel(ch)) - 1500.0f) / 500.0f;
    }

    /// Utility: throttle 0.0 to 1.0 (min 1000, max 2000)
    float throttle() const {
        return (static_cast<float>(getChannel(2)) - 1000.0f) / 1000.0f;
    }
};
```

---

## 11. Troubleshooting & Common Pitfalls

### SBUS Not Decoding

| Symptom                        | Likely Cause                              | Fix                                           |
|--------------------------------|-------------------------------------------|-----------------------------------------------|
| No data / garbage bytes        | Polarity not inverted                     | Enable UART inversion or use hardware inverter|
| Partial frames / wrong values  | Wrong baud rate                           | Must be exactly 100,000 (not 115,200)         |
| Channels always 0              | Wrong stop bit count                      | Must be 2 stop bits, even parity              |
| Frame lost flag always set     | TX and RX SBUS signal grounded differently| Check GND reference between receiver and FC   |

### CRSF Latency Issues

- Ensure UART FIFO / DMA is used — interrupt-per-byte adds jitter
- ExpressLRS requires UART baud matching the link rate (420,000 is default)
- Use hardware flow control only if both sides support it

### PPM Sync Loss

- Check timer resolution: must be at least 1 µs (1 MHz timer clock)
- Use hardware input capture, not GPIO interrupts, on noisy lines
- Add 100 Ω series resistor on signal line to reduce ringing

### General Tips

- Always implement a **failsafe timeout** — if no valid frame arrives within 100 ms, cut throttle
- Log raw UART bytes during debug to verify baud/parity/stop bit settings
- Use a logic analyzer or oscilloscope to verify signal polarity before coding

---

## 12. Summary

RC protocols for drones and RC vehicles span a wide range of technologies:

**PPM** is a legacy analog pulse-train protocol encoding multiple channels in one wire using pulse
timing. It requires only a GPIO interrupt or timer input capture, but offers no error detection and
limited channel count.

**PWM** is the simplest single-channel analog signal, still used to drive individual servos and ESCs
but impractical for multi-channel use.

**SBUS** is the dominant serial digital protocol (100,000 baud, 8E2, inverted) supporting 18
channels in a 25-byte frame. Its bit-packed 11-bit channel data and electrical inversion make it
slightly tricky to implement but universally supported by flight controllers.

**IBUS** (FlySky, 115,200 baud, 8N1, non-inverted) offers 14 channels with a simple 32-byte
checksum-validated frame and bidirectional sensor telemetry on the same wire.

**CRSF** (TBS Crossfire / ExpressLRS, 420,000 baud) is the modern high-performance choice, offering
16 channels at up to 500 Hz update rate, robust CRC-8 error checking, and full bidirectional
telemetry. It is now the de facto standard for performance FPV drones.

**SUMD** (Graupner HoTT) supports up to 32 channels with CRC16-CCITT error checking and is used
primarily in the Graupner ecosystem.

All protocols share the concept of mapping channel values to 1000–2000 µs servo ranges for
interoperability. Implementation in both **C/C++** and **Rust** follows a streaming state-machine
pattern where bytes are fed one at a time to a parser that returns a complete frame when valid data
arrives. Abstraction layers over these parsers allow flight controller firmware to switch protocols
at compile time or runtime without changing higher-level control logic.

---

*Document covers: PPM, PWM, SBUS, IBUS, CRSF (Crossfire/ExpressLRS), SUMD protocols with
C/C++ and Rust (`no_std`) implementations suitable for embedded flight controller use.*