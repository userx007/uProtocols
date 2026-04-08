# 63. MIDI Protocol — Musical Instrument Digital Interface over UART at 31.25 kbaud

**Protocol & Physical Layer** — the 31.25 kbaud rate explained as 1 MHz ÷ 32, the opto-isolated current-loop hardware interface, byte timing (320 µs/byte, ~1 ms per 3-byte message), and throughput ceiling (~1042 note events/second).

**Message Architecture** — the status/data byte split (MSB flag), the channel nibble encoding, a full message-length table, and detailed treatment of Note On/Off, CC, Pitch Bend (14-bit signed), Program Change, SysEx, and all real-time messages.

**C Code** — four focused files:
- `midi.h` — all constants, types, inline helpers (pitch bend encode/decode, note→frequency)
- `serial_posix.c` — Linux `termios2` / `TCSETS2` ioctl setup for custom 31250 baud
- `midi_parser.c` — streaming state-machine handling running status, SysEx, and interleaved real-time bytes
- `MidiPort.hpp` + `MidiClockMaster.cpp` — C++ RAII wrapper with async RX thread and a BPM-accurate clock generator

**Rust Code** — idiomatic Rust with:
- Newtype wrappers for `Channel` and `Note` (compile-time range safety)
- Full `MidiMessage` enum covering all message types
- Ownership-safe `MidiParser` with running status
- `MidiSender` with running status suppression
- A complete monitor `main.rs` and an atomic-BPM `MidiClock` generator

**Advanced sections** cover General MIDI instrument map, MIDI Thru, USB-MIDI, MIDI 2.0 (UMP), bare-metal ISR ring-buffer pattern, and robustness/error-handling strategies.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Physical Layer & UART Configuration](#physical-layer--uart-configuration)
3. [MIDI Message Structure](#midi-message-structure)
4. [Message Types Reference](#message-types-reference)
5. [Running Status Optimization](#running-status-optimization)
6. [SysEx — System Exclusive Messages](#sysex--system-exclusive-messages)
7. [MIDI Timing & Real-Time Messages](#midi-timing--real-time-messages)
8. [Programming in C/C++](#programming-in-cc)
9. [Programming in Rust](#programming-in-rust)
10. [Advanced Topics](#advanced-topics)
11. [Summary](#summary)

---

## Introduction

**MIDI** (Musical Instrument Digital Interface) is a serial communication protocol standardized in 1983 that allows electronic musical instruments, computers, and other devices to communicate musical performance data. Despite its age, MIDI remains ubiquitous in professional audio, live performance, studio production, and embedded music systems.

MIDI transmits *events* — discrete musical actions — rather than audio waveforms. A MIDI message might say "Note C4 ON at velocity 64 on channel 1", leaving the receiving device to produce whatever sound it is programmed to make. This separation of control from sound generation is the core insight behind MIDI's longevity.

MIDI runs over a **31,250 baud (31.25 kbaud) asynchronous serial link** — a carefully chosen rate because it is an exact integer division of 1 MHz (1,000,000 / 32 = 31,250), making it synthesizable from common crystal frequencies of the era (1 MHz, 2 MHz, 4 MHz, 8 MHz, etc.).

### Key Characteristics

| Property | Value |
|---|---|
| Baud rate | 31,250 bps (31.25 kbaud) |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Logic level | Opto-isolated current loop (5 mA) |
| Connector | 5-pin DIN (classic), TRS 3.5 mm (modern), USB-MIDI, BLE-MIDI |
| Max cable length | ~15 m (spec), practical ~50 m |
| Channels | 16 logical channels per port |
| Message latency | ~1 ms per 3-byte message |

---

## Physical Layer & UART Configuration

### Hardware Interface

Classic MIDI uses a **current-loop opto-isolated interface**, not RS-232 voltage levels. The MIDI OUT port sources ~5 mA through a 220 Ω resistor into the opto-coupler LED in the MIDI IN of the receiving device. This galvanic isolation eliminates ground loops that cause audio hum — a crucial design decision for audio equipment.

```
MIDI OUT (source)                MIDI IN (sink)
─────────────────                ───────────────
     Vcc (+5V)
       │
     220Ω                        Pin 4 ──┐
       │                                 │
Pin 5 ─┤──────── cable ─────────────────>├── Opto LED
       │                                 │
Pin 2 ─┴─ Shield/GND             Pin 5 ─┴── GND
```

**Pin assignments (5-pin DIN):**
- Pin 2: Shield / ground reference (MIDI IN only)
- Pin 4: Current source (MIDI OUT) / Collector output (MIDI IN)
- Pin 5: Current sink (MIDI OUT) / Emitter output (MIDI IN)
- Pins 1, 3: Unused

### UART Configuration

```
Baud:     31250
Data:     8 bits
Parity:   None
Stop:     1
Flow:     None (no hardware or software flow control)
```

The choice of **31,250 baud exactly** means the bit period is:

```
T_bit = 1 / 31250 = 32 µs
T_byte = (1 start + 8 data + 1 stop) × 32 µs = 320 µs per byte
```

A 3-byte MIDI message (the most common size) therefore takes **960 µs ≈ 1 ms** to transmit — an important latency floor for real-time performance applications.

### Maximum Throughput

At 31.25 kbaud with 10 bits per byte:

```
Max throughput = 31250 / 10 = 3125 bytes/second
```

A saturated MIDI stream can carry about **3125 note events per second** (assuming 1-byte messages with running status) or **~1042 standard 3-byte note events per second** — enough for dense polyphonic performance but not infinite.

---

## MIDI Message Structure

Every MIDI byte is one of two types, distinguished by the MSB:

```
Status byte:  1xxxxxxx  (MSB = 1) — begins a new message
Data byte:    0xxxxxxx  (MSB = 0) — parameter value (0–127)
```

This fundamental rule means data values are always 0–127 (7-bit), while status bytes span 128–255.

### Status Byte Format

```
 7   6   5   4   3   2   1   0
┌───┬───────────┬───────────────┐
│ 1 │  Command  │    Channel    │
└───┴───────────┴───────────────┘
     3 bits       4 bits
     (0–7)        (0–15 → Ch 1–16)
```

The upper nibble encodes the **command type** (with MSB always 1), and the lower nibble encodes the **MIDI channel** (0–15, representing channels 1–16).

**Exception:** System messages (command nibble = 0xF) use the full lower nibble for the message type rather than a channel.

### Message Lengths

| Command | Nibble | Total Bytes | Example |
|---|---|---|---|
| Note Off | 0x8 | 3 | 0x80 0x3C 0x00 |
| Note On | 0x9 | 3 | 0x90 0x3C 0x64 |
| Poly Aftertouch | 0xA | 3 | 0xA0 0x3C 0x40 |
| Control Change | 0xB | 3 | 0xB0 0x07 0x7F |
| Program Change | 0xC | 2 | 0xC0 0x00 |
| Channel Pressure | 0xD | 2 | 0xD0 0x40 |
| Pitch Bend | 0xE | 3 | 0xE0 0x00 0x40 |
| SysEx Start | 0xF0 | variable | 0xF0 ... 0xF7 |
| Time Code | 0xF1 | 2 | 0xF1 0x00 |
| Song Position | 0xF2 | 3 | 0xF2 0x00 0x00 |
| Song Select | 0xF3 | 2 | 0xF3 0x00 |
| Tune Request | 0xF6 | 1 | 0xF6 |
| SysEx End | 0xF7 | 1 | 0xF7 |
| Timing Clock | 0xF8 | 1 | 0xF8 |
| Start | 0xFA | 1 | 0xFA |
| Continue | 0xFB | 1 | 0xFB |
| Stop | 0xFC | 1 | 0xFC |
| Active Sensing | 0xFE | 1 | 0xFE |
| System Reset | 0xFF | 1 | 0xFF |

---

## Message Types Reference

### Note On / Note Off

The most fundamental MIDI messages. Note On with velocity 0 is equivalent to Note Off (exploited by running status).

```
Note On:  [0x9n] [note 0–127] [velocity 0–127]
Note Off: [0x8n] [note 0–127] [velocity 0–127]

n = channel (0–15)
note: 60 = Middle C (C4), each semitone = +1
velocity: 0 = silent, 127 = maximum force
```

MIDI note to frequency:
```
f = 440 × 2^((note - 69) / 12)
```

### Control Change (CC)

Sends continuous controller values — volume, pan, modulation, sustain pedal, etc.

```
[0xBn] [controller 0–127] [value 0–127]
```

**Important controllers:**
| CC# | Function |
|---|---|
| 1 | Modulation Wheel |
| 7 | Channel Volume |
| 10 | Pan |
| 11 | Expression |
| 64 | Sustain Pedal (≥64 = on) |
| 121 | Reset All Controllers |
| 123 | All Notes Off |

### Pitch Bend

14-bit value (0–16383) centered at 8192 (no bend). Split across two 7-bit data bytes, LSB first.

```
[0xEn] [LSB 0–127] [MSB 0–127]

Decoded value = LSB | (MSB << 7)   // range: 0–16383
Center value  = 8192               // no pitch change
```

### Program Change

Selects instrument/patch (0–127). In General MIDI (GM), these map to standard instrument sounds.

```
[0xCn] [program 0–127]
```

---

## Running Status Optimization

**Running status** is a MIDI bandwidth optimization: if consecutive messages share the same status byte, the status byte can be omitted from subsequent messages. The receiver remembers the last status byte and applies it to bare data bytes.

```
Without running status (3 Note Ons):
0x90 0x3C 0x64   (Note On, C4, vel 100)
0x90 0x3E 0x64   (Note On, D4, vel 100)
0x90 0x40 0x64   (Note On, E4, vel 100)
= 9 bytes

With running status:
0x90 0x3C 0x64   (Note On, C4, vel 100)  ← status byte sent
     0x3E 0x64   (Note On, D4, vel 100)  ← status omitted
     0x40 0x64   (Note On, E4, vel 100)  ← status omitted
= 7 bytes (22% savings)
```

**Running status rules:**
- Only applies to **channel messages** (0x80–0xEF)
- **System Real-Time messages** (0xF8–0xFF) do NOT cancel running status
- **System Common messages** (0xF0–0xF6) DO cancel running status
- Receivers must support it; senders may optionally use it

---

## SysEx — System Exclusive Messages

SysEx messages carry manufacturer-specific or device-specific data of arbitrary length, used for patch dumps, firmware updates, parameter control, and proprietary extensions.

```
[0xF0] [manufacturer ID] [data bytes...] [0xF7]
```

- `0xF0` = SysEx Start
- `0xF7` = SysEx End (EOX)
- All bytes between must be data bytes (MSB = 0, values 0–127)
- Length is unlimited (can be megabytes for firmware updates)
- Real-time messages (0xF8–0xFF) may be interleaved within SysEx

**Manufacturer IDs:**
- 1-byte IDs: 0x01–0x7D (assigned by MMA)
- 3-byte IDs: 0x00 [byte2] [byte3] (extended, post-1987)
- 0x7D: Educational/non-commercial use
- 0x7E: Universal Non-Real-Time SysEx
- 0x7F: Universal Real-Time SysEx

**Example — GM System On:**
```
F0 7E 7F 09 01 F7
   │  │  │  │
   │  │  │  └── GM System On
   │  │  └───── Sub-ID #1: General MIDI
   │  └──────── Device ID: 0x7F = all devices
   └─────────── Universal Non-Real-Time
```

---

## MIDI Timing & Real-Time Messages

### MIDI Clock

MIDI Timing Clock (`0xF8`) is sent **24 times per quarter note** (24 PPQN — Pulses Per Quarter Note) by the master device. Slave devices use this to synchronize tempo.

```
At 120 BPM:
  Quarter note duration = 60s / 120 = 500 ms
  Clock interval = 500 ms / 24 = 20.833 ms
  Clock frequency = 48 Hz
```

### Start / Stop / Continue

- `0xFA` — **Start**: Begin playback from beginning
- `0xFC` — **Stop**: Halt playback (preserves position)
- `0xFB` — **Continue**: Resume from current position

### Song Position Pointer (SPP)

A 14-bit value (0–16383) representing the current position in **MIDI beats** (1 beat = 6 MIDI clocks = 1/16th note).

```
[0xF2] [LSB] [MSB]
Position in 16th notes = LSB | (MSB << 7)
```

### Active Sensing

`0xFE` — Sent every ~300 ms to indicate the connection is alive. If a receiver stops seeing Active Sensing after receiving at least one, it should silence all notes.

---

## Programming in C/C++

### Platform Overview

MIDI programming in C/C++ typically targets:
- **Linux**: `/dev/ttyS*`, `/dev/ttyUSB*`, or ALSA sequencer API
- **Windows**: `CreateFile()` on `COM*` ports, or WinMM MIDI API
- **Embedded (bare-metal)**: Direct UART peripheral registers
- **Arduino/AVR**: `Serial` or `HardwareSerial` at 31250 baud

The examples below use POSIX termios for Linux and a generic embedded HAL interface.

---

### C — MIDI Message Definitions and Parsing

```c
/* midi.h — Core MIDI types and constants */
#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Status byte masks ─────────────────────────────────────────── */
#define MIDI_STATUS_MASK        0xF0
#define MIDI_CHANNEL_MASK       0x0F
#define MIDI_IS_STATUS(b)       ((b) & 0x80)
#define MIDI_IS_REALTIME(b)     ((b) >= 0xF8)
#define MIDI_IS_SYSEX_START(b)  ((b) == 0xF0)
#define MIDI_IS_SYSEX_END(b)    ((b) == 0xF7)

/* ── Channel message status bytes ──────────────────────────────── */
#define MIDI_NOTE_OFF           0x80
#define MIDI_NOTE_ON            0x90
#define MIDI_POLY_AFTERTOUCH    0xA0
#define MIDI_CONTROL_CHANGE     0xB0
#define MIDI_PROGRAM_CHANGE     0xC0
#define MIDI_CHANNEL_PRESSURE   0xD0
#define MIDI_PITCH_BEND         0xE0

/* ── System messages ───────────────────────────────────────────── */
#define MIDI_SYSEX_START        0xF0
#define MIDI_TIME_CODE          0xF1
#define MIDI_SONG_POSITION      0xF2
#define MIDI_SONG_SELECT        0xF3
#define MIDI_TUNE_REQUEST       0xF6
#define MIDI_SYSEX_END          0xF7
#define MIDI_TIMING_CLOCK       0xF8
#define MIDI_START              0xFA
#define MIDI_CONTINUE           0xFB
#define MIDI_STOP               0xFC
#define MIDI_ACTIVE_SENSING     0xFE
#define MIDI_SYSTEM_RESET       0xFF

/* ── Common CC numbers ─────────────────────────────────────────── */
#define MIDI_CC_MODULATION      1
#define MIDI_CC_VOLUME          7
#define MIDI_CC_PAN             10
#define MIDI_CC_EXPRESSION      11
#define MIDI_CC_SUSTAIN         64
#define MIDI_CC_RESET_ALL       121
#define MIDI_CC_ALL_NOTES_OFF   123

/* ── Constants ─────────────────────────────────────────────────── */
#define MIDI_BAUD               31250
#define MIDI_MAX_CHANNELS       16
#define MIDI_NOTES              128
#define MIDI_CENTER_PITCH_BEND  8192
#define MIDI_CLOCK_PPQN         24
#define MIDI_MAX_SYSEX          4096    /* application limit */

/* ── Decoded message structure ─────────────────────────────────── */
typedef struct {
    uint8_t  status;     /* raw status byte */
    uint8_t  type;       /* command nibble (masked, e.g. MIDI_NOTE_ON) */
    uint8_t  channel;    /* 0–15  */
    uint8_t  data[2];    /* data bytes */
    uint8_t  data_len;   /* number of data bytes (0–2) */
    bool     is_sysex;
    uint8_t *sysex_buf;
    size_t   sysex_len;
} midi_message_t;

/* ── Return how many data bytes follow a given status byte ─────── */
static inline int midi_message_length(uint8_t status)
{
    if (MIDI_IS_REALTIME(status)) return 0;    /* 1 byte total */

    switch (status & MIDI_STATUS_MASK) {
        case MIDI_NOTE_OFF:
        case MIDI_NOTE_ON:
        case MIDI_POLY_AFTERTOUCH:
        case MIDI_CONTROL_CHANGE:
        case MIDI_PITCH_BEND:       return 2;
        case MIDI_PROGRAM_CHANGE:
        case MIDI_CHANNEL_PRESSURE: return 1;
        default: break;
    }

    /* System messages */
    switch (status) {
        case MIDI_SYSEX_START:  return -1;   /* variable */
        case MIDI_TIME_CODE:
        case MIDI_SONG_SELECT:  return 1;
        case MIDI_SONG_POSITION:return 2;
        case MIDI_TUNE_REQUEST:
        case MIDI_SYSEX_END:    return 0;
        default:                return 0;
    }
}

/* ── Decode pitch bend to signed 14-bit value (-8192 to +8191) ── */
static inline int16_t midi_decode_pitch_bend(uint8_t lsb, uint8_t msb)
{
    return (int16_t)((msb << 7) | lsb) - MIDI_CENTER_PITCH_BEND;
}

/* ── Encode signed pitch bend to two data bytes ─────────────────  */
static inline void midi_encode_pitch_bend(int16_t value,
                                          uint8_t *lsb, uint8_t *msb)
{
    uint16_t raw = (uint16_t)(value + MIDI_CENTER_PITCH_BEND);
    *lsb = raw & 0x7F;
    *msb = (raw >> 7) & 0x7F;
}

/* ── Convert MIDI note to frequency (Hz) ──────────────────────── */
static inline float midi_note_to_freq(uint8_t note)
{
    /* f = 440 * 2^((note-69)/12) */
    return 440.0f * (float)exp2((note - 69) / 12.0);
}

#endif /* MIDI_H */
```

---

### C — POSIX Serial Port Setup (Linux)

```c
/* serial_posix.c — Open and configure a POSIX serial port for MIDI */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

/*
 * NOTE: Standard POSIX termios does not define B31250.
 * On Linux with a USB-serial adapter or dedicated UART, you can set
 * custom baud rates using the BOTHER / TCSETS2 ioctl on kernels 2.6.32+.
 * Here we use that method. For actual MIDI hardware you may also use
 * rtmidi or ALSA's rawmidi interface.
 */
#include <sys/ioctl.h>
#include <asm/termbits.h>   /* struct termios2, BOTHER */

/**
 * open_midi_port - Open a serial port configured for MIDI
 * @path:    e.g. "/dev/ttyUSB0" or "/dev/ttyAMA0"
 * Returns:  file descriptor on success, -1 on error
 */
int open_midi_port(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    /* Use termios2 for custom baud rate (requires Linux ≥ 2.6.32) */
    struct termios2 tio;
    memset(&tio, 0, sizeof(tio));

    if (ioctl(fd, TCGETS2, &tio) < 0) {
        perror("TCGETS2");
        close(fd);
        return -1;
    }

    /* Raw mode: 8N1, no echo, no signal processing */
    tio.c_cflag &= ~(CBAUD | CSIZE | PARENB | CSTOPB);
    tio.c_cflag |=  (BOTHER | CS8 | CREAD | CLOCAL);
    tio.c_iflag  =  0;   /* no input processing */
    tio.c_oflag  =  0;   /* no output processing */
    tio.c_lflag  =  0;   /* raw, no echo */

    /* Set custom baud rate: 31250 */
    tio.c_ispeed = MIDI_BAUD;   /* 31250 */
    tio.c_ospeed = MIDI_BAUD;

    /* Read returns when at least 1 byte is available */
    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 0;

    if (ioctl(fd, TCSETS2, &tio) < 0) {
        perror("TCSETS2");
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}
```

---

### C — MIDI Parser (Streaming State Machine)

```c
/* midi_parser.c — Streaming MIDI parser with running status support */
#include "midi.h"
#include <string.h>

/* ── Parser state ──────────────────────────────────────────────── */
typedef enum {
    MIDI_PARSE_IDLE,
    MIDI_PARSE_DATA,
    MIDI_PARSE_SYSEX,
} midi_parse_state_t;

typedef struct {
    midi_parse_state_t  state;
    uint8_t             running_status;     /* last channel status */
    uint8_t             current_status;
    int                 expected_data;      /* bytes still needed */
    uint8_t             buf[3];
    int                 buf_idx;
    uint8_t             sysex_buf[MIDI_MAX_SYSEX];
    size_t              sysex_idx;
} midi_parser_t;

/* Callback type: called when a complete message is ready */
typedef void (*midi_callback_t)(const midi_message_t *msg, void *userdata);

/**
 * midi_parser_init - Initialise parser to known state
 */
void midi_parser_init(midi_parser_t *p)
{
    memset(p, 0, sizeof(*p));
    p->state = MIDI_PARSE_IDLE;
}

/**
 * midi_parser_feed - Feed one byte into the parser.
 *   Calls cb(msg, userdata) whenever a complete message is assembled.
 *   Handles:
 *     - Running status (channel messages)
 *     - Interleaved real-time messages (clock, start, stop, …)
 *     - SysEx with EOX termination
 */
void midi_parser_feed(midi_parser_t *p, uint8_t byte,
                      midi_callback_t cb, void *userdata)
{
    midi_message_t msg;
    memset(&msg, 0, sizeof(msg));

    /* ── Real-time: single byte, never interrupts running status ── */
    if (MIDI_IS_REALTIME(byte)) {
        msg.status   = byte;
        msg.type     = byte;
        msg.data_len = 0;
        cb(&msg, userdata);
        return;
    }

    /* ── SysEx end ─────────────────────────────────────────────── */
    if (MIDI_IS_SYSEX_END(byte)) {
        if (p->state == MIDI_PARSE_SYSEX) {
            msg.status    = MIDI_SYSEX_START;
            msg.is_sysex  = true;
            msg.sysex_buf = p->sysex_buf;
            msg.sysex_len = p->sysex_idx;
            cb(&msg, userdata);
        }
        p->state          = MIDI_PARSE_IDLE;
        p->running_status = 0;   /* SysEx cancels running status */
        return;
    }

    /* ── New status byte ───────────────────────────────────────── */
    if (MIDI_IS_STATUS(byte)) {
        if (MIDI_IS_SYSEX_START(byte)) {
            p->state          = MIDI_PARSE_SYSEX;
            p->sysex_idx      = 0;
            p->running_status = 0;
            return;
        }
        /* System Common cancels running status */
        if ((byte & 0xF8) == 0xF0) {
            p->running_status = 0;
        } else {
            /* Channel message — update running status */
            p->running_status = byte;
        }
        p->current_status  = byte;
        p->expected_data   = midi_message_length(byte);
        p->buf_idx         = 0;
        p->state           = (p->expected_data > 0)
                             ? MIDI_PARSE_DATA : MIDI_PARSE_IDLE;
        if (p->expected_data == 0) {
            /* 1-byte system message */
            msg.status   = byte;
            msg.type     = byte;
            msg.data_len = 0;
            cb(&msg, userdata);
        }
        return;
    }

    /* ── Data byte ─────────────────────────────────────────────── */
    switch (p->state) {
    case MIDI_PARSE_SYSEX:
        if (p->sysex_idx < MIDI_MAX_SYSEX)
            p->sysex_buf[p->sysex_idx++] = byte;
        return;

    case MIDI_PARSE_IDLE:
        /* Running status: bare data byte with no preceding status */
        if (p->running_status != 0) {
            p->current_status = p->running_status;
            p->expected_data  = midi_message_length(p->running_status);
            p->buf_idx        = 0;
            p->state          = MIDI_PARSE_DATA;
        } else {
            return;   /* discard: no valid status context */
        }
        /* fall through */

    case MIDI_PARSE_DATA:
        p->buf[p->buf_idx++] = byte;
        if (p->buf_idx >= p->expected_data) {
            /* Complete message */
            msg.status   = p->current_status;
            msg.type     = p->current_status & MIDI_STATUS_MASK;
            msg.channel  = p->current_status & MIDI_CHANNEL_MASK;
            msg.data_len = (uint8_t)p->expected_data;
            memcpy(msg.data, p->buf, p->expected_data);
            cb(&msg, userdata);
            /* Stay in DATA state for running status */
            p->buf_idx = 0;
        }
        break;
    }
}
```

---

### C — MIDI Message Sender with Running Status

```c
/* midi_sender.c — Build and transmit MIDI messages */
#include "midi.h"
#include <unistd.h>
#include <stdint.h>

typedef struct {
    int      fd;                /* serial file descriptor */
    uint8_t  running_status;   /* last sent status (0 = none) */
    bool     use_running;      /* enable running status optimisation */
} midi_sender_t;

static void midi_sender_init(midi_sender_t *s, int fd, bool use_running)
{
    s->fd             = fd;
    s->running_status = 0;
    s->use_running    = use_running;
}

static int uart_write_byte(int fd, uint8_t b)
{
    return (int)write(fd, &b, 1);
}

/**
 * midi_send_raw - Send a raw status + data bytes, applying running status.
 */
static void midi_send_raw(midi_sender_t *s, uint8_t status,
                           const uint8_t *data, int data_len)
{
    bool is_channel = (status < 0xF0);
    bool is_realtime = MIDI_IS_REALTIME(status);

    /* Emit status byte (suppress if running status matches) */
    if (!is_realtime) {
        if (!is_channel || !s->use_running ||
            s->running_status != status) {
            uart_write_byte(s->fd, status);
            if (is_channel)
                s->running_status = status;
            else
                s->running_status = 0;  /* system msg clears it */
        }
    } else {
        uart_write_byte(s->fd, status);
        /* Real-time: do NOT change running_status */
    }

    for (int i = 0; i < data_len; i++)
        uart_write_byte(s->fd, data[i]);
}

/* ── High-level helpers ─────────────────────────────────────────── */

void midi_send_note_on(midi_sender_t *s, uint8_t ch, uint8_t note, uint8_t vel)
{
    uint8_t data[2] = { note & 0x7F, vel & 0x7F };
    midi_send_raw(s, MIDI_NOTE_ON | (ch & 0x0F), data, 2);
}

void midi_send_note_off(midi_sender_t *s, uint8_t ch, uint8_t note, uint8_t vel)
{
    /* Using Note On with vel=0 is more efficient with running status */
    uint8_t data[2] = { note & 0x7F, 0x00 };
    midi_send_raw(s, MIDI_NOTE_ON | (ch & 0x0F), data, 2);
    (void)vel;
}

void midi_send_cc(midi_sender_t *s, uint8_t ch, uint8_t cc, uint8_t val)
{
    uint8_t data[2] = { cc & 0x7F, val & 0x7F };
    midi_send_raw(s, MIDI_CONTROL_CHANGE | (ch & 0x0F), data, 2);
}

void midi_send_program(midi_sender_t *s, uint8_t ch, uint8_t prog)
{
    uint8_t data[1] = { prog & 0x7F };
    midi_send_raw(s, MIDI_PROGRAM_CHANGE | (ch & 0x0F), data, 1);
}

void midi_send_pitch_bend(midi_sender_t *s, uint8_t ch, int16_t value)
{
    uint8_t lsb, msb;
    midi_encode_pitch_bend(value, &lsb, &msb);
    uint8_t data[2] = { lsb, msb };
    midi_send_raw(s, MIDI_PITCH_BEND | (ch & 0x0F), data, 2);
}

void midi_send_clock(midi_sender_t *s)
{
    midi_send_raw(s, MIDI_TIMING_CLOCK, NULL, 0);
}

void midi_send_start(midi_sender_t *s)
{
    midi_send_raw(s, MIDI_START, NULL, 0);
}

void midi_send_stop(midi_sender_t *s)
{
    midi_send_raw(s, MIDI_STOP, NULL, 0);
}

void midi_send_sysex(midi_sender_t *s, const uint8_t *data, size_t len)
{
    uart_write_byte(s->fd, MIDI_SYSEX_START);
    for (size_t i = 0; i < len; i++)
        uart_write_byte(s->fd, data[i] & 0x7F);
    uart_write_byte(s->fd, MIDI_SYSEX_END);
    s->running_status = 0;   /* SysEx cancels running status */
}
```

---

### C — Complete MIDI Monitor Example (Linux)

```c
/* midi_monitor.c — Print incoming MIDI messages to stdout */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "midi.h"

/* (include midi_parser_t and midi_parser_feed() from above) */

static volatile int running = 1;
static void sigint_handler(int s) { (void)s; running = 0; }

static const char *note_names[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static void print_note(uint8_t note)
{
    printf("%s%d", note_names[note % 12], (note / 12) - 1);
}

static void on_midi_message(const midi_message_t *msg, void *ud)
{
    (void)ud;
    uint8_t ch = msg->channel + 1;  /* display as 1-based */

    if (msg->is_sysex) {
        printf("SysEx [%zu bytes]: F0", msg->sysex_len);
        for (size_t i = 0; i < msg->sysex_len && i < 8; i++)
            printf(" %02X", msg->sysex_buf[i]);
        if (msg->sysex_len > 8) printf(" ...");
        printf(" F7\n");
        return;
    }

    switch (msg->type) {
    case MIDI_NOTE_ON:
        printf("Note On  ch=%2d note=", ch);
        print_note(msg->data[0]);
        printf(" vel=%3d%s\n", msg->data[1],
               msg->data[1] == 0 ? " (note off via running)" : "");
        break;
    case MIDI_NOTE_OFF:
        printf("Note Off ch=%2d note=", ch);
        print_note(msg->data[0]);
        printf(" vel=%3d\n", msg->data[1]);
        break;
    case MIDI_CONTROL_CHANGE:
        printf("CC       ch=%2d cc=%3d val=%3d\n",
               ch, msg->data[0], msg->data[1]);
        break;
    case MIDI_PROGRAM_CHANGE:
        printf("Program  ch=%2d prog=%3d\n", ch, msg->data[0]);
        break;
    case MIDI_PITCH_BEND: {
        int16_t bend = midi_decode_pitch_bend(msg->data[0], msg->data[1]);
        printf("PitchBnd ch=%2d val=%+6d\n", ch, bend);
        break;
    }
    case MIDI_CHANNEL_PRESSURE:
        printf("ChanPres ch=%2d val=%3d\n", ch, msg->data[0]);
        break;
    default:
        switch (msg->status) {
        case MIDI_TIMING_CLOCK:  printf("Clock\n");   break;
        case MIDI_START:         printf("Start\n");   break;
        case MIDI_STOP:          printf("Stop\n");    break;
        case MIDI_CONTINUE:      printf("Continue\n");break;
        case MIDI_ACTIVE_SENSING:printf("ActiveSense\n"); break;
        default:
            printf("Unknown  status=0x%02X\n", msg->status);
        }
    }
}

int main(int argc, char *argv[])
{
    const char *port = (argc > 1) ? argv[1] : "/dev/ttyUSB0";

    int fd = open_midi_port(port);
    if (fd < 0) return 1;

    signal(SIGINT, sigint_handler);
    printf("Listening on %s at 31250 baud...\n", port);

    midi_parser_t parser;
    midi_parser_init(&parser);

    uint8_t byte;
    while (running) {
        ssize_t n = read(fd, &byte, 1);
        if (n > 0)
            midi_parser_feed(&parser, byte, on_midi_message, NULL);
        else if (n < 0 && errno != EAGAIN)
            break;
    }

    close(fd);
    return 0;
}
```

---

### C++ — Object-Oriented MIDI Interface

```cpp
// MidiPort.hpp — C++ RAII wrapper for a MIDI serial port
#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <stdexcept>
#include "midi.h"   /* C types from above */

class MidiPort {
public:
    using MessageCallback = std::function<void(const midi_message_t&)>;

    explicit MidiPort(const std::string& device, bool use_running_status = true)
        : fd_(-1)
        , use_running_(use_running_status)
        , running_out_(0)
        , active_(false)
    {
        fd_ = open_midi_port(device.c_str());
        if (fd_ < 0)
            throw std::runtime_error("Cannot open MIDI port: " + device);
        midi_parser_init(&parser_);
    }

    ~MidiPort()
    {
        stopListening();
        if (fd_ >= 0) close(fd_);
    }

    /* Disable copy; allow move */
    MidiPort(const MidiPort&) = delete;
    MidiPort& operator=(const MidiPort&) = delete;

    /* ── Async receive ─────────────────────────────────────────── */
    void startListening(MessageCallback cb)
    {
        callback_ = std::move(cb);
        active_.store(true);
        rx_thread_ = std::thread([this] { rxLoop(); });
    }

    void stopListening()
    {
        active_.store(false);
        if (rx_thread_.joinable())
            rx_thread_.join();
    }

    /* ── Send helpers ──────────────────────────────────────────── */
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity)
    {
        sendRaw(MIDI_NOTE_ON | (channel & 0x0F),
                { note & 0x7Fu, velocity & 0x7Fu });
    }

    void noteOff(uint8_t channel, uint8_t note, uint8_t velocity = 0)
    {
        /* Note On vel=0 is idiomatic for running-status efficiency */
        sendRaw(MIDI_NOTE_ON | (channel & 0x0F),
                { note & 0x7Fu, 0x00u });
        (void)velocity;
    }

    void controlChange(uint8_t channel, uint8_t cc, uint8_t value)
    {
        sendRaw(MIDI_CONTROL_CHANGE | (channel & 0x0F),
                { cc & 0x7Fu, value & 0x7Fu });
    }

    void programChange(uint8_t channel, uint8_t program)
    {
        sendRaw(MIDI_PROGRAM_CHANGE | (channel & 0x0F),
                { program & 0x7Fu });
    }

    void pitchBend(uint8_t channel, int16_t value)
    {
        uint8_t lsb, msb;
        midi_encode_pitch_bend(value, &lsb, &msb);
        sendRaw(MIDI_PITCH_BEND | (channel & 0x0F), { lsb, msb });
    }

    void clock()  { sendRaw(MIDI_TIMING_CLOCK, {}); }
    void start()  { sendRaw(MIDI_START, {}); }
    void stop()   { sendRaw(MIDI_STOP, {}); }

    void sysex(const std::vector<uint8_t>& data)
    {
        uint8_t sof = MIDI_SYSEX_START;
        uint8_t eof = MIDI_SYSEX_END;
        write(fd_, &sof, 1);
        for (auto b : data) { uint8_t d = b & 0x7F; write(fd_, &d, 1); }
        write(fd_, &eof, 1);
        running_out_ = 0;
    }

private:
    void rxLoop()
    {
        uint8_t byte;
        while (active_.load()) {
            ssize_t n = read(fd_, &byte, 1);
            if (n > 0) {
                midi_parser_feed(&parser_, byte,
                    [](const midi_message_t *msg, void *ud) {
                        auto *self = static_cast<MidiPort*>(ud);
                        if (self->callback_) self->callback_(*msg);
                    }, this);
            }
        }
    }

    void sendRaw(uint8_t status, std::initializer_list<uint8_t> data)
    {
        bool is_channel  = (status < 0xF0);
        bool is_realtime = MIDI_IS_REALTIME(status);

        if (!is_realtime) {
            if (!is_channel || !use_running_ || running_out_ != status) {
                write(fd_, &status, 1);
                running_out_ = is_channel ? status : 0;
            }
        } else {
            write(fd_, &status, 1);
        }
        for (uint8_t b : data) write(fd_, &b, 1);
    }

    int                 fd_;
    bool                use_running_;
    uint8_t             running_out_;
    midi_parser_t       parser_;
    MessageCallback     callback_;
    std::thread         rx_thread_;
    std::atomic<bool>   active_;
};
```

---

### C++ — MIDI Clock Master

```cpp
// MidiClockMaster.cpp — Generate MIDI clock at a configurable BPM
#include "MidiPort.hpp"
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdio>

class MidiClockMaster {
public:
    MidiClockMaster(MidiPort& port, double bpm)
        : port_(port), bpm_(bpm), running_(false) {}

    void setBPM(double bpm)
    {
        bpm_.store(bpm);   /* atomic update — safe from any thread */
    }

    void start()
    {
        port_.start();
        running_.store(true);
        thread_ = std::thread([this] { clockLoop(); });
    }

    void stop()
    {
        running_.store(false);
        if (thread_.joinable()) thread_.join();
        port_.stop();
    }

private:
    void clockLoop()
    {
        using clock = std::chrono::steady_clock;
        using ns    = std::chrono::nanoseconds;

        auto next_tick = clock::now();

        while (running_.load()) {
            port_.clock();

            /* Compute interval: (60s / BPM) / 24 clocks per beat */
            double bpm     = bpm_.load();
            double beat_ns = 60.0e9 / bpm;
            double tick_ns = beat_ns / MIDI_CLOCK_PPQN;

            next_tick += ns(static_cast<long long>(tick_ns));
            std::this_thread::sleep_until(next_tick);
        }
    }

    MidiPort&            port_;
    std::atomic<double>  bpm_;
    std::atomic<bool>    running_;
    std::thread          thread_;
};

/* ── Usage example ─────────────────────────────────────────────── */
int main()
{
    MidiPort port("/dev/ttyUSB0");
    MidiClockMaster master(port, 120.0);

    /* Monitor incoming messages */
    port.startListening([](const midi_message_t& msg) {
        if (msg.type == MIDI_CONTROL_CHANGE && msg.data[0] == MIDI_CC_VOLUME)
            printf("Volume ch%d = %d\n", msg.channel + 1, msg.data[1]);
    });

    printf("MIDI clock at 120 BPM — press Enter to stop\n");
    master.start();
    getchar();
    master.stop();
    return 0;
}
```

---

## Programming in Rust

Rust's ownership model and type system are well-suited for embedded MIDI where you need zero-cost abstractions, deterministic memory, and compile-time enforcement of protocol constraints.

The examples use the `serialport` crate for cross-platform serial access and `no_std`-compatible patterns for bare-metal targets.

### Cargo.toml

```toml
[package]
name    = "midi-example"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4"          # cross-platform serial
thiserror  = "1"          # ergonomic error types
```

---

### Rust — MIDI Types and Parsing

```rust
// src/midi.rs — MIDI protocol types, constants, and parser

/// MIDI baud rate (exact integer division of 1 MHz)
pub const MIDI_BAUD: u32 = 31_250;

/// Clocks per quarter note (MIDI spec)
pub const MIDI_PPQN: u32 = 24;

/// Center value for 14-bit pitch bend
pub const MIDI_PITCH_BEND_CENTER: i16 = 0;

// ── Status byte constants ────────────────────────────────────────

pub const NOTE_OFF:          u8 = 0x80;
pub const NOTE_ON:           u8 = 0x90;
pub const POLY_AFTERTOUCH:   u8 = 0xA0;
pub const CONTROL_CHANGE:    u8 = 0xB0;
pub const PROGRAM_CHANGE:    u8 = 0xC0;
pub const CHANNEL_PRESSURE:  u8 = 0xD0;
pub const PITCH_BEND:        u8 = 0xE0;

pub const SYSEX_START:       u8 = 0xF0;
pub const TIME_CODE:         u8 = 0xF1;
pub const SONG_POSITION:     u8 = 0xF2;
pub const SONG_SELECT:       u8 = 0xF3;
pub const TUNE_REQUEST:      u8 = 0xF6;
pub const SYSEX_END:         u8 = 0xF7;
pub const TIMING_CLOCK:      u8 = 0xF8;
pub const START:             u8 = 0xFA;
pub const CONTINUE:          u8 = 0xFB;
pub const STOP:              u8 = 0xFC;
pub const ACTIVE_SENSING:    u8 = 0xFE;
pub const SYSTEM_RESET:      u8 = 0xFF;

// ── Strongly-typed MIDI channel (0–15) ──────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Channel(u8);

impl Channel {
    pub fn new(ch: u8) -> Self {
        assert!(ch < 16, "MIDI channel must be 0–15");
        Channel(ch)
    }

    /// 1-based display channel (1–16)
    pub fn display(self) -> u8 { self.0 + 1 }
    pub fn raw(self)     -> u8 { self.0 }
}

// ── Strongly-typed MIDI note (0–127) ────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Note(u8);

impl Note {
    pub fn new(n: u8) -> Self {
        assert!(n < 128);
        Note(n)
    }

    pub fn raw(self)   -> u8  { self.0 }

    /// Frequency in Hz: 440 × 2^((note−69)/12)
    pub fn frequency(self) -> f32 {
        440.0 * 2.0_f32.powf((self.0 as f32 - 69.0) / 12.0)
    }

    /// Name like "C4", "F#3"
    pub fn name(self) -> String {
        const NAMES: [&str; 12] = [
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
        ];
        let octave = (self.0 / 12) as i8 - 1;
        format!("{}{}", NAMES[(self.0 % 12) as usize], octave)
    }
}

// ── MIDI message enum ────────────────────────────────────────────

#[derive(Debug, Clone)]
pub enum MidiMessage {
    NoteOff          { channel: Channel, note: Note, velocity: u8 },
    NoteOn           { channel: Channel, note: Note, velocity: u8 },
    PolyAftertouch   { channel: Channel, note: Note, pressure: u8 },
    ControlChange    { channel: Channel, controller: u8, value: u8 },
    ProgramChange    { channel: Channel, program: u8 },
    ChannelPressure  { channel: Channel, pressure: u8 },
    PitchBend        { channel: Channel, value: i16 },  /* −8192..+8191 */

    /* System common */
    SysEx            (Vec<u8>),                 /* payload without F0/F7 */
    TimeCode         (u8),
    SongPosition     (u16),                     /* in MIDI beats */
    SongSelect       (u8),
    TuneRequest,

    /* System real-time */
    TimingClock,
    Start,
    Continue,
    Stop,
    ActiveSensing,
    SystemReset,
}

impl MidiMessage {
    /// Pitch bend: decode two 7-bit data bytes → signed value
    pub fn decode_pitch_bend(lsb: u8, msb: u8) -> i16 {
        let raw = ((msb as i16) << 7) | (lsb as i16);
        raw - 8192
    }

    /// Encode signed pitch bend → (lsb, msb)
    pub fn encode_pitch_bend(value: i16) -> (u8, u8) {
        let raw = (value + 8192) as u16;
        ((raw & 0x7F) as u8, ((raw >> 7) & 0x7F) as u8)
    }
}

// ── Parser ───────────────────────────────────────────────────────

#[derive(Debug)]
enum ParseState {
    Idle,
    CollectingData { status: u8, buf: [u8; 2], idx: usize, needed: usize },
    SysEx           { buf: Vec<u8> },
}

#[derive(Debug, Default)]
pub struct MidiParser {
    state:          ParseState,
    running_status: Option<u8>,
}

impl Default for ParseState {
    fn default() -> Self { ParseState::Idle }
}

/// Number of data bytes that follow a given status byte.
/// Returns None for SysEx (variable length).
fn data_bytes_for(status: u8) -> Option<usize> {
    match status & 0xF0 {
        0x80 | 0x90 | 0xA0 | 0xB0 | 0xE0 => Some(2),
        0xC0 | 0xD0                        => Some(1),
        0xF0 => match status {
            SYSEX_START  => None,         /* variable */
            TIME_CODE | SONG_SELECT => Some(1),
            SONG_POSITION           => Some(2),
            _                       => Some(0),
        },
        _ => Some(0),
    }
}

impl MidiParser {
    pub fn new() -> Self { Self::default() }

    /// Feed a single byte. Returns Some(MidiMessage) when complete.
    pub fn feed(&mut self, byte: u8) -> Option<MidiMessage> {
        /* ── Real-time: complete single-byte message ─────────── */
        if byte >= 0xF8 {
            return Some(match byte {
                TIMING_CLOCK   => MidiMessage::TimingClock,
                START          => MidiMessage::Start,
                CONTINUE       => MidiMessage::Continue,
                STOP           => MidiMessage::Stop,
                ACTIVE_SENSING => MidiMessage::ActiveSensing,
                SYSTEM_RESET   => MidiMessage::SystemReset,
                _              => return None,
            });
        }

        /* ── SysEx end ───────────────────────────────────────── */
        if byte == SYSEX_END {
            if let ParseState::SysEx { buf } = std::mem::take(&mut self.state) {
                self.running_status = None;
                return Some(MidiMessage::SysEx(buf));
            }
            self.state = ParseState::Idle;
            return None;
        }

        /* ── New status byte ─────────────────────────────────── */
        if byte & 0x80 != 0 {
            if byte == SYSEX_START {
                self.state          = ParseState::SysEx { buf: Vec::new() };
                self.running_status = None;
                return None;
            }
            /* System common cancels running status */
            if byte >= 0xF0 {
                self.running_status = None;
            } else {
                self.running_status = Some(byte);
            }
            match data_bytes_for(byte) {
                Some(0) => return self.dispatch(byte, &[]),
                Some(n) => {
                    self.state = ParseState::CollectingData {
                        status: byte, buf: [0; 2], idx: 0, needed: n
                    };
                }
                None => {}
            }
            return None;
        }

        /* ── Data byte ───────────────────────────────────────── */
        match &mut self.state {
            ParseState::SysEx { buf } => {
                buf.push(byte);
                None
            }
            ParseState::CollectingData { status, buf, idx, needed } => {
                buf[*idx] = byte;
                *idx += 1;
                if *idx == *needed {
                    let (s, b, n) = (*status, *buf, *needed);
                    self.state = ParseState::Idle;
                    self.dispatch(s, &b[..n])
                } else {
                    None
                }
            }
            ParseState::Idle => {
                /* Running status: bare data byte */
                if let Some(status) = self.running_status {
                    let needed = data_bytes_for(status).unwrap_or(0);
                    self.state = ParseState::CollectingData {
                        status, buf: [byte, 0], idx: 1, needed
                    };
                    if needed == 1 {
                        let s = status;
                        self.state = ParseState::Idle;
                        return self.dispatch(s, &[byte]);
                    }
                }
                None
            }
        }
    }

    fn dispatch(&self, status: u8, data: &[u8]) -> Option<MidiMessage> {
        let ch = Channel::new(status & 0x0F);
        let cmd = status & 0xF0;

        let note = |n: u8| Note::new(n & 0x7F);
        let d0   = data.first().copied().unwrap_or(0) & 0x7F;
        let d1   = data.get(1) .copied().unwrap_or(0) & 0x7F;

        Some(match cmd {
            0x80 => MidiMessage::NoteOff        { channel: ch, note: note(d0), velocity: d1 },
            0x90 => MidiMessage::NoteOn         { channel: ch, note: note(d0), velocity: d1 },
            0xA0 => MidiMessage::PolyAftertouch { channel: ch, note: note(d0), pressure: d1 },
            0xB0 => MidiMessage::ControlChange  { channel: ch, controller: d0, value: d1 },
            0xC0 => MidiMessage::ProgramChange  { channel: ch, program: d0 },
            0xD0 => MidiMessage::ChannelPressure{ channel: ch, pressure: d0 },
            0xE0 => MidiMessage::PitchBend      {
                channel: ch,
                value: MidiMessage::decode_pitch_bend(d0, d1),
            },
            0xF0 => match status {
                TIME_CODE    => MidiMessage::TimeCode(d0),
                SONG_POSITION=> MidiMessage::SongPosition(d0 as u16 | ((d1 as u16) << 7)),
                SONG_SELECT  => MidiMessage::SongSelect(d0),
                TUNE_REQUEST => MidiMessage::TuneRequest,
                _            => return None,
            },
            _ => return None,
        })
    }
}
```

---

### Rust — MIDI Sender

```rust
// src/sender.rs — Build and write MIDI messages to a serial port

use serialport::SerialPort;
use crate::midi::*;

pub struct MidiSender {
    port:           Box<dyn SerialPort>,
    running_status: Option<u8>,
    use_running:    bool,
}

impl MidiSender {
    pub fn new(port: Box<dyn SerialPort>, use_running_status: bool) -> Self {
        MidiSender { port, running_status: None, use_running: use_running_status }
    }

    // ── Public API ────────────────────────────────────────────────

    pub fn note_on(&mut self, ch: Channel, note: Note, vel: u8)
        -> std::io::Result<()>
    {
        self.send_channel(NOTE_ON | ch.raw(), &[note.raw(), vel & 0x7F])
    }

    pub fn note_off(&mut self, ch: Channel, note: Note)
        -> std::io::Result<()>
    {
        /* Note On vel=0 idiom for running-status efficiency */
        self.send_channel(NOTE_ON | ch.raw(), &[note.raw(), 0x00])
    }

    pub fn control_change(&mut self, ch: Channel, cc: u8, val: u8)
        -> std::io::Result<()>
    {
        self.send_channel(CONTROL_CHANGE | ch.raw(), &[cc & 0x7F, val & 0x7F])
    }

    pub fn program_change(&mut self, ch: Channel, prog: u8)
        -> std::io::Result<()>
    {
        self.send_channel(PROGRAM_CHANGE | ch.raw(), &[prog & 0x7F])
    }

    pub fn pitch_bend(&mut self, ch: Channel, value: i16)
        -> std::io::Result<()>
    {
        let (lsb, msb) = MidiMessage::encode_pitch_bend(value);
        self.send_channel(PITCH_BEND | ch.raw(), &[lsb, msb])
    }

    pub fn clock(&mut self) -> std::io::Result<()> {
        self.write_byte(TIMING_CLOCK)
    }

    pub fn start(&mut self) -> std::io::Result<()> {
        self.write_byte(START)
    }

    pub fn stop(&mut self) -> std::io::Result<()> {
        self.write_byte(STOP)
    }

    pub fn sysex(&mut self, payload: &[u8]) -> std::io::Result<()> {
        self.write_byte(SYSEX_START)?;
        for &b in payload {
            self.write_byte(b & 0x7F)?;
        }
        self.write_byte(SYSEX_END)?;
        self.running_status = None;   /* SysEx cancels running status */
        Ok(())
    }

    // ── Private helpers ───────────────────────────────────────────

    fn send_channel(&mut self, status: u8, data: &[u8]) -> std::io::Result<()> {
        let suppress = self.use_running
            && self.running_status == Some(status);

        if !suppress {
            self.port.write_all(&[status])?;
            self.running_status = Some(status);
        }
        self.port.write_all(data)
    }

    fn write_byte(&mut self, b: u8) -> std::io::Result<()> {
        self.port.write_all(&[b])
    }
}
```

---

### Rust — Complete MIDI Monitor Application

```rust
// src/main.rs — MIDI monitor: open port, print all incoming messages

mod midi;
mod sender;

use midi::{MidiParser, MidiMessage};
use serialport::SerialPort;
use std::time::Duration;

fn open_midi_port(device: &str) -> Box<dyn SerialPort> {
    serialport::new(device, midi::MIDI_BAUD)
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .timeout(Duration::from_millis(100))
        .open()
        .unwrap_or_else(|e| panic!("Cannot open {device}: {e}"))
}

fn print_message(msg: &MidiMessage) {
    match msg {
        MidiMessage::NoteOn { channel, note, velocity } => {
            if *velocity == 0 {
                println!("Note Off ch={:2} note={:<4} (via running status)",
                         channel.display(), note.name());
            } else {
                println!("Note On  ch={:2} note={:<4} vel={:3}  ({:.1} Hz)",
                         channel.display(), note.name(),
                         velocity, note.frequency());
            }
        }
        MidiMessage::NoteOff { channel, note, velocity } => {
            println!("Note Off ch={:2} note={:<4} vel={:3}",
                     channel.display(), note.name(), velocity);
        }
        MidiMessage::ControlChange { channel, controller, value } => {
            println!("CC       ch={:2} cc={:3} val={:3}",
                     channel.display(), controller, value);
        }
        MidiMessage::ProgramChange { channel, program } => {
            println!("Program  ch={:2} prog={:3}", channel.display(), program);
        }
        MidiMessage::PitchBend { channel, value } => {
            println!("PitchBnd ch={:2} val={:+6}", channel.display(), value);
        }
        MidiMessage::ChannelPressure { channel, pressure } => {
            println!("ChanPres ch={:2} val={:3}", channel.display(), pressure);
        }
        MidiMessage::SysEx(data) => {
            print!("SysEx [{} bytes]: F0", data.len());
            for b in data.iter().take(8) { print!(" {b:02X}"); }
            if data.len() > 8 { print!(" ..."); }
            println!(" F7");
        }
        MidiMessage::TimingClock  => print!("."),   /* frequent: dot only */
        MidiMessage::Start        => println!("▶ Start"),
        MidiMessage::Stop         => println!("■ Stop"),
        MidiMessage::Continue     => println!("▶ Continue"),
        MidiMessage::ActiveSensing => {}            /* ignore; very frequent */
        MidiMessage::SystemReset  => println!("⚡ System Reset"),
        other => println!("{:?}", other),
    }
}

fn main() {
    let device = std::env::args().nth(1)
        .unwrap_or_else(|| "/dev/ttyUSB0".to_string());

    let mut port   = open_midi_port(&device);
    let mut parser = MidiParser::new();
    let mut buf    = [0u8; 64];

    println!("Listening on {device} at {} baud…", midi::MIDI_BAUD);

    loop {
        match port.read(&mut buf) {
            Ok(n) => {
                for &byte in &buf[..n] {
                    if let Some(msg) = parser.feed(byte) {
                        print_message(&msg);
                    }
                }
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {}
            Err(e) => {
                eprintln!("Read error: {e}");
                break;
            }
        }
    }
}
```

---

### Rust — MIDI Clock Generator

```rust
// src/clock.rs — Precise MIDI clock generator using std::thread::sleep_until

use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};
use crate::sender::MidiSender;
use crate::midi::MIDI_PPQN;

pub struct MidiClock {
    bpm:     Arc<AtomicU64>,   /* BPM × 1000, stored as fixed-point */
    running: Arc<AtomicBool>,
}

impl MidiClock {
    /// Create but don't start the clock.
    pub fn new(bpm: f64) -> Self {
        MidiClock {
            bpm:     Arc::new(AtomicU64::new((bpm * 1000.0) as u64)),
            running: Arc::new(AtomicBool::new(false)),
        }
    }

    /// Atomically update BPM while the clock is running.
    pub fn set_bpm(&self, bpm: f64) {
        self.bpm.store((bpm * 1000.0) as u64, Ordering::Relaxed);
    }

    /// Start the clock, moving ownership of the sender to the thread.
    pub fn start(&self, mut sender: MidiSender)
    {
        self.running.store(true, Ordering::Release);
        let bpm_ref  = Arc::clone(&self.bpm);
        let run_ref  = Arc::clone(&self.running);

        thread::spawn(move || {
            sender.start().ok();

            let mut next = Instant::now();

            while run_ref.load(Ordering::Acquire) {
                sender.clock().ok();

                let bpm      = bpm_ref.load(Ordering::Relaxed) as f64 / 1000.0;
                /* tick interval = 60s / BPM / 24 pulses */
                let tick_ns  = (60_000_000_000.0 / bpm / MIDI_PPQN as f64) as u64;

                next += Duration::from_nanos(tick_ns);
                let now = Instant::now();
                if next > now {
                    thread::sleep(next - now);
                } else {
                    /* We're behind — skip the sleep, catch up */
                    next = now;
                }
            }

            sender.stop().ok();
        });
    }

    pub fn stop(&self) {
        self.running.store(false, Ordering::Release);
    }
}
```

---

## Advanced Topics

### General MIDI (GM)

GM Level 1 (1991) standardises which instrument sound each program number produces, so a GM-compliant device sounds roughly the same regardless of manufacturer. Key requirements:

- 24-voice minimum polyphony
- Channel 10 is always percussion
- 128 defined instruments (programs 0–127)

Selected GM instruments:
| Program | Instrument | Program | Instrument |
|---|---|---|---|
| 0 | Acoustic Grand Piano | 40 | Violin |
| 25 | Acoustic Guitar | 56 | Trumpet |
| 32 | Acoustic Bass | 73 | Flute |
| 48 | String Ensemble 1 | 118 | Synth Drum |

### MIDI Thru

MIDI **THRU** ports re-transmit everything received on MIDI IN with a small delay (typ. < 1 ms), allowing daisy-chaining of devices without a merge box. In software, a "MIDI Through" port does the same.

### MIDI over USB (USB-MIDI)

USB-MIDI (class USB Audio Device, subclass MIDI Streaming) wraps MIDI in USB packets with a 4-byte header per event. Most modern DAWs and controllers use USB-MIDI exclusively; no baud rate applies. The protocol data is identical MIDI; only the transport changes.

### MIDI 2.0

Released in 2020, MIDI 2.0 (UMP — Universal MIDI Packet) uses 32-bit words, extends resolution to 32-bit per parameter (vs. 7-bit in MIDI 1.0), adds 256 groups × 16 channels, bidirectional discovery (MIDI-CI), and is backward compatible via MIDI 1.0 profiles. The physical layer is typically USB or network; 31.25 kbaud DIN is no longer the primary transport.

### Embedded MIDI (Bare-Metal C)

On microcontrollers without an OS, MIDI reception typically uses interrupt-driven UART reception into a circular ring buffer, drained from the main loop or a lower-priority task:

```c
/* Typical bare-metal ISR (e.g., STM32 USART) */
#define MIDI_RX_BUF_SIZE 256

static uint8_t  rx_buf[MIDI_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0, rx_tail = 0;

void USART1_IRQHandler(void)
{
    if (USART1->SR & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)(USART1->DR & 0xFF);
        uint16_t next_head = (rx_head + 1) & (MIDI_RX_BUF_SIZE - 1);
        if (next_head != rx_tail) {          /* not full */
            rx_buf[rx_head] = byte;
            rx_head = next_head;
        }
        /* else: overflow — byte silently dropped */
    }
}

/* Called from main loop */
bool midi_rx_pop(uint8_t *out)
{
    if (rx_tail == rx_head) return false;
    *out   = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) & (MIDI_RX_BUF_SIZE - 1);
    return true;
}
```

### Error Handling and Robustness

Because MIDI has no checksums or acknowledgement, receivers must be robust against:

- **Framing errors** from cable glitches — discard the byte, re-synchronize on the next status byte
- **Buffer overflow** at high MIDI density — drop oldest data, log if possible
- **Stuck notes** — All Notes Off (CC 123) or System Reset clear all voices
- **Lost Active Sensing** — silence all notes after 330 ms without a message
- **SysEx overflow** — enforce a maximum SysEx buffer size and discard if exceeded

---

## Summary

| Aspect | Detail |
|---|---|
| **Transport** | Asynchronous serial UART — 31,250 baud, 8N1, no parity, no flow control |
| **Physical layer** | Opto-isolated 5 mA current loop; 5-pin DIN connector; galvanic isolation eliminates ground loops |
| **Byte encoding** | MSB=1 → status byte (command + channel); MSB=0 → data byte (7-bit, 0–127) |
| **Message types** | Note On/Off, CC, Pitch Bend, Program Change, SysEx, Timing Clock, Transport |
| **Channels** | 16 logical channels per physical port |
| **Running status** | Omit repeated status bytes to save bandwidth (~22% on dense streams) |
| **Latency** | ~320 µs per byte; ~960 µs per 3-byte message |
| **Timing clock** | 24 PPQN; at 120 BPM → 48 Hz clock signal |
| **SysEx** | Variable-length, manufacturer-specific; any data up to 127/byte |
| **Real-time msgs** | Single-byte, highest priority; can be inserted within SysEx |
| **C/C++ approach** | State-machine parser, ring-buffer ISR on embedded; termios2 / ALSA on Linux |
| **Rust approach** | Enum-based message type, ownership-safe parser, `serialport` crate for I/O |
| **MIDI 2.0** | 32-bit UMP packets, 256 channels, 32-bit resolution; USB transport; backward compatible |
| **Key use cases** | Keyboards, synthesizers, DAWs, drum machines, lighting control, embedded music systems |

MIDI over UART remains one of the most elegant protocol designs in embedded systems: a fixed 31.25 kbaud serial link carrying compact, deterministic event messages that express rich musical performance with minimal bandwidth and zero protocol overhead. The combination of a clear status/data byte distinction, running status compression, and interruptible real-time messages makes it robust and real-time capable even on the 8-bit microcontrollers of 1983 — and equally at home in modern Rust on an ARM Cortex-M today.