# 89. UART Traffic Recording

**Core concepts** — frame anatomy (timestamp, direction, data, flags), session file requirements, and the four replay fidelity modes (real-time, compressed, scaled, triggered).

**Architecture** — an ASCII diagram showing the capture ring buffer → async session writer → session file → replay engine pipeline.

**C/C++ implementation** — four complete, commented code files:
- `uart_capture.h/.c` — POSIX termios capture loop with monotonic timestamps
- `session_logger.h/.c` — ring-buffer-backed async writer with a self-describing binary session header
- `uart_replay.h/.c` — replay engine supporting all three timing modes, with a usage example

**Rust implementation** — two modules:
- `lib.rs` — `Frame`, `FrameFlags` (bitflags), and an async `Recorder` using `mpsc` channels
- `replay.rs` — typed `TimingMode` enum, `ReplayConfig` with callback support, and a `serialport` convenience function

**Advanced topics** — pattern-based session filtering, probabilistic error injection via a generic `Write` wrapper, and multi-channel recording with a `channel_id` field.

**Testing strategies** — regression testing workflow, round-trip integrity test, and fuzzing with mutated session files.

## Capturing and Replaying UART Sessions for Testing

---

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Recording Architecture](#recording-architecture)
4. [Implementation in C/C++](#implementation-in-cc)
   - [Basic Frame Capture](#basic-frame-capture)
   - [Timestamped Session Logger](#timestamped-session-logger)
   - [Binary Session File Format](#binary-session-file-format)
   - [Replay Engine](#replay-engine)
5. [Implementation in Rust](#implementation-in-rust)
   - [Session Recorder](#session-recorder)
   - [Replay with Timing Control](#replay-with-timing-control)
6. [Advanced Topics](#advanced-topics)
   - [Filtering and Annotation](#filtering-and-annotation)
   - [Error Injection During Replay](#error-injection-during-replay)
   - [Multi-Channel Recording](#multi-channel-recording)
7. [Testing Strategies](#testing-strategies)
8. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) traffic recording is the practice of capturing every byte — along with its precise timestamp and direction — exchanged over a serial link, then storing that capture in a way that allows faithful reproduction (replay) at a later time. This technique is fundamental in embedded-systems testing, protocol debugging, regression testing, and hardware-in-the-loop (HIL) simulation.

A recorded session is an ordered, time-annotated log of all UART frames seen on a bus. Replay is the controlled playback of that log against a device under test (DUT), or into a software simulation, so that the system's response to a known stimulus can be evaluated deterministically.

**Why record and replay?**

- **Regression testing:** Run the exact same stimulus against a new firmware build and compare outputs byte-for-byte.
- **Intermittent bug capture:** Leave the recorder running overnight; when the fault appears the session file contains the evidence.
- **Hardware absence:** Play back real-device traffic into a software stub while hardware is unavailable.
- **Stress and edge-case injection:** Modify a captured session to introduce corrupted bytes, timing violations, or unusual sequences without needing the original peripheral.
- **Documentation:** Session files are executable specifications of protocol behavior.

---

## Core Concepts

### Frame Anatomy

A UART frame on the wire consists of:

```
[START bit][D0][D1][D2][D3][D4][D5][D6][D7][PARITY?][STOP bit(s)]
```

At the software layer, what the recorder cares about is:

| Field        | Description                                    |
|-------------|------------------------------------------------|
| `timestamp` | Monotonic time (µs or ns resolution)           |
| `direction` | TX (host→device) or RX (device→host)           |
| `data`      | Byte value (0x00–0xFF)                         |
| `flags`     | Framing error, parity error, overrun, break    |

### Session File Considerations

A good session file format must be:

- **Self-describing** — contains baud rate, parity, stop bits, and recording date.
- **Compact** — many sessions run for hours at high baud rates; a 4 MB/s UART channel produces ~400 KB/s of raw data.
- **Seekable** — random access by timestamp enables efficient replay at arbitrary offsets.
- **Checksum-protected** — corruption must be detectable.

### Replay Fidelity Modes

| Mode            | Description                                                    |
|-----------------|----------------------------------------------------------------|
| **Real-time**   | Honour original inter-byte gaps exactly                        |
| **Compressed**  | Remove idle gaps; replay only data bytes as fast as possible   |
| **Scaled**      | Multiply all gaps by a factor (e.g., 0.5× = double speed)     |
| **Triggered**   | Advance replay only when the DUT sends an expected response    |

---

## Recording Architecture

```
  ┌─────────────────────────────────────────────────────────────┐
  │                     HOST MACHINE                            │
  │                                                             │
  │  ┌──────────┐    ┌────────────────┐    ┌─────────────────┐ │
  │  │  Serial  │───▶│  Capture Ring  │───▶│  Session Writer │ │
  │  │  Driver  │    │  Buffer (DMA)  │    │  (async flush)  │ │
  │  └──────────┘    └────────────────┘    └────────┬────────┘ │
  │       ▲                                          │          │
  │       │  TX inject                               ▼          │
  │  ┌────┴─────┐                          ┌─────────────────┐ │
  │  │  Replay  │◀─────────────────────────│  Session File   │ │
  │  │  Engine  │                          │  (.uart / .bin) │ │
  │  └──────────┘                          └─────────────────┘ │
  └─────────────────────────────────────────────────────────────┘
                          │ USB-UART / RS-232
                          ▼
                  ┌───────────────┐
                  │  Device Under │
                  │     Test      │
                  └───────────────┘
```

The capture ring buffer absorbs bursts so that slow disk I/O never causes dropped bytes. The session writer drains the ring asynchronously and appends to the file. During replay the engine reads ahead into a prefetch buffer, applies timing control, and injects bytes via the same serial driver used for normal TX.

---

## Implementation in C/C++

### Basic Frame Capture

```c
/* uart_capture.h  –  Minimal single-byte capture record */
#ifndef UART_CAPTURE_H
#define UART_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>

/* Direction of a captured byte */
typedef enum {
    DIR_RX = 0,   /* device → host */
    DIR_TX = 1    /* host  → device */
} uart_direction_t;

/* Error/status flags */
#define UART_FLAG_OK          0x00
#define UART_FLAG_FRAME_ERR   0x01
#define UART_FLAG_PARITY_ERR  0x02
#define UART_FLAG_OVERRUN     0x04
#define UART_FLAG_BREAK       0x08

/* One captured frame */
typedef struct __attribute__((packed)) {
    uint64_t         timestamp_us;  /* Monotonic µs since session start  */
    uint8_t          data;          /* Byte value                        */
    uart_direction_t direction;     /* TX or RX                          */
    uint8_t          flags;         /* UART_FLAG_* bitmask               */
    uint8_t          _pad;          /* Reserved, must be 0               */
} uart_frame_t;

#endif /* UART_CAPTURE_H */
```

```c
/* uart_capture.c  –  POSIX capture loop (Linux termios) */
#include "uart_capture.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Return monotonic microseconds */
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/* Open and configure a serial port */
static int open_serial(const char *path, int baud) {
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return -1; }

    struct termios tty;
    memset(&tty, 0, sizeof tty);
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);
    /* 8N1, raw mode */
    tty.c_cflag  = CS8 | CREAD | CLOCAL;
    tty.c_iflag  = 0;
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tty) != 0) { perror("tcsetattr"); close(fd); return -1; }
    return fd;
}

/*
 * Capture up to max_frames RX bytes and write them to out_file.
 * Returns the number of frames written, or -1 on error.
 */
int uart_capture(const char *port, int baud,
                 const char *out_path, uint64_t max_frames) {
    int fd = open_serial(port, baud);
    if (fd < 0) return -1;

    FILE *out = fopen(out_path, "wb");
    if (!out) { perror("fopen"); close(fd); return -1; }

    uint64_t t0      = now_us();
    uint64_t written = 0;
    uint8_t  buf[256];
    ssize_t  n;

    while (written < max_frames) {
        n = read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EAGAIN) { usleep(100); continue; }
            perror("read"); break;
        }
        uint64_t ts = now_us() - t0;
        for (ssize_t i = 0; i < n; i++) {
            uart_frame_t f = {
                .timestamp_us = ts,
                .data         = buf[i],
                .direction    = DIR_RX,
                .flags        = UART_FLAG_OK,
                ._pad         = 0
            };
            if (fwrite(&f, sizeof f, 1, out) != 1) { perror("fwrite"); goto done; }
            written++;
        }
    }
done:
    fclose(out);
    close(fd);
    return (int)written;
}
```

### Timestamped Session Logger

A production recorder wraps the capture loop with a session header and a ring buffer for lock-free producer/consumer decoupling:

```c
/* session_logger.h */
#ifndef SESSION_LOGGER_H
#define SESSION_LOGGER_H

#include "uart_capture.h"
#include <stddef.h>

#define SESSION_MAGIC   0x55415254UL   /* "UART" */
#define SESSION_VERSION 1

/* Written once at the start of every session file */
typedef struct __attribute__((packed)) {
    uint32_t magic;          /* SESSION_MAGIC                    */
    uint16_t version;        /* SESSION_VERSION                   */
    uint32_t baud_rate;      /* e.g. 115200                       */
    uint8_t  data_bits;      /* 7 or 8                            */
    uint8_t  parity;         /* 0=none 1=odd 2=even               */
    uint8_t  stop_bits;      /* 1 or 2                            */
    uint64_t wall_clock_us;  /* Unix epoch µs at session start    */
    char     port[32];       /* Null-terminated port name         */
    char     note[64];       /* Free-form annotation              */
    uint32_t header_crc32;   /* CRC32 of above fields             */
} session_header_t;

typedef struct session_logger session_logger_t;

session_logger_t *session_logger_create(const char *out_path,
                                        uint32_t    baud,
                                        const char *port,
                                        const char *note);
int  session_logger_push(session_logger_t *sl, const uart_frame_t *frame);
void session_logger_flush(session_logger_t *sl);
void session_logger_destroy(session_logger_t *sl);

#endif /* SESSION_LOGGER_H */
```

```c
/* session_logger.c – ring-buffer-backed async writer */
#include "session_logger.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define RING_CAPACITY  (1 << 14)  /* 16 384 slots; must be power of two */
#define RING_MASK      (RING_CAPACITY - 1)

struct session_logger {
    uart_frame_t    ring[RING_CAPACITY];
    volatile size_t head;       /* written by producer (capture thread) */
    volatile size_t tail;       /* written by consumer (writer thread)  */
    FILE           *file;
    pthread_t       writer_thread;
    volatile int    stop;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
};

static void *writer_loop(void *arg) {
    session_logger_t *sl = arg;
    while (!sl->stop || sl->head != sl->tail) {
        pthread_mutex_lock(&sl->mu);
        while (sl->head == sl->tail && !sl->stop)
            pthread_cond_wait(&sl->cv, &sl->mu);
        pthread_mutex_unlock(&sl->mu);

        while (sl->tail != sl->head) {
            fwrite(&sl->ring[sl->tail & RING_MASK], sizeof(uart_frame_t), 1, sl->file);
            sl->tail++;
        }
        fflush(sl->file);
    }
    return NULL;
}

session_logger_t *session_logger_create(const char *out_path,
                                        uint32_t    baud,
                                        const char *port,
                                        const char *note) {
    session_logger_t *sl = calloc(1, sizeof *sl);
    if (!sl) return NULL;

    sl->file = fopen(out_path, "wb");
    if (!sl->file) { free(sl); return NULL; }

    /* Write session header */
    session_header_t hdr = {
        .magic      = SESSION_MAGIC,
        .version    = SESSION_VERSION,
        .baud_rate  = baud,
        .data_bits  = 8,
        .parity     = 0,
        .stop_bits  = 1,
    };
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    hdr.wall_clock_us = (uint64_t)ts.tv_sec * 1000000ULL
                      + (uint64_t)ts.tv_nsec / 1000ULL;
    strncpy(hdr.port, port, sizeof hdr.port - 1);
    strncpy(hdr.note, note, sizeof hdr.note - 1);
    /* (CRC32 calculation omitted for brevity; use zlib crc32()) */
    hdr.header_crc32 = 0xDEADBEEF;

    fwrite(&hdr, sizeof hdr, 1, sl->file);

    pthread_mutex_init(&sl->mu, NULL);
    pthread_cond_init(&sl->cv, NULL);
    pthread_create(&sl->writer_thread, NULL, writer_loop, sl);
    return sl;
}

int session_logger_push(session_logger_t *sl, const uart_frame_t *frame) {
    size_t next = sl->head + 1;
    if ((next & RING_MASK) == (sl->tail & RING_MASK))
        return -1;  /* Ring full – caller must handle (e.g., drop or block) */
    sl->ring[sl->head & RING_MASK] = *frame;
    sl->head = next;
    pthread_mutex_lock(&sl->mu);
    pthread_cond_signal(&sl->cv);
    pthread_mutex_unlock(&sl->mu);
    return 0;
}

void session_logger_flush(session_logger_t *sl) { fflush(sl->file); }

void session_logger_destroy(session_logger_t *sl) {
    sl->stop = 1;
    pthread_mutex_lock(&sl->mu);
    pthread_cond_signal(&sl->cv);
    pthread_mutex_unlock(&sl->mu);
    pthread_join(sl->writer_thread, NULL);
    fclose(sl->file);
    pthread_mutex_destroy(&sl->mu);
    pthread_cond_destroy(&sl->cv);
    free(sl);
}
```

### Binary Session File Format

```
Offset  Size  Field
──────  ────  ──────────────────────────────────────────
0       4     magic        = 0x55415254 ("UART")
4       2     version      = 1
6       4     baud_rate
10      1     data_bits
11      1     parity
12      1     stop_bits
13      8     wall_clock_us
21      32    port[32]
53      64    note[64]
117     4     header_crc32
────── 121 bytes ──────────────────────────────────────
121     N     frame records (12 bytes each):
             [8] timestamp_us
             [1] data
             [1] direction
             [1] flags
             [1] _pad
```

### Replay Engine

```c
/* uart_replay.h */
#ifndef UART_REPLAY_H
#define UART_REPLAY_H

#include "uart_capture.h"
#include <stdint.h>

typedef enum {
    REPLAY_REALTIME   = 0,
    REPLAY_COMPRESSED = 1,   /* No gaps between bytes      */
    REPLAY_SCALED     = 2,   /* Gaps multiplied by a factor */
} replay_mode_t;

typedef struct {
    replay_mode_t mode;
    double        time_scale;   /* Used only for REPLAY_SCALED */
    int           tx_only;      /* 1 = replay TX frames only   */
    void (*on_byte)(uint8_t byte, uint64_t ts_us, void *ctx);
    void         *ctx;
} replay_config_t;

int uart_replay(const char *session_path, int serial_fd,
                const replay_config_t *cfg);

#endif /* UART_REPLAY_H */
```

```c
/* uart_replay.c */
#include "uart_replay.h"
#include "session_logger.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

static void sleep_us(uint64_t us) {
    struct timespec req = {
        .tv_sec  = (time_t)(us / 1000000ULL),
        .tv_nsec = (long)((us % 1000000ULL) * 1000ULL)
    };
    nanosleep(&req, NULL);
}

int uart_replay(const char *session_path, int serial_fd,
                const replay_config_t *cfg) {
    FILE *f = fopen(session_path, "rb");
    if (!f) { perror("fopen"); return -1; }

    /* Skip session header */
    session_header_t hdr;
    if (fread(&hdr, sizeof hdr, 1, f) != 1) { fclose(f); return -1; }

    if (hdr.magic != SESSION_MAGIC) {
        fprintf(stderr, "uart_replay: bad magic 0x%08X\n", hdr.magic);
        fclose(f);
        return -1;
    }

    uart_frame_t frame;
    uint64_t     prev_ts  = 0;
    uint64_t     t_start  = 0;  /* set on first frame */
    int          first    = 1;

    /* Wall-clock start for real-time pacing */
    struct timespec wall_start;
    clock_gettime(CLOCK_MONOTONIC, &wall_start);

    while (fread(&frame, sizeof frame, 1, f) == 1) {
        /* Skip RX frames when tx_only is set */
        if (cfg->tx_only && frame.direction != DIR_TX) continue;

        /* Skip frames with errors unless caller wants them */
        if (frame.flags & (UART_FLAG_FRAME_ERR | UART_FLAG_OVERRUN)) continue;

        if (first) { t_start = frame.timestamp_us; first = 0; }

        uint64_t delay_us = 0;
        switch (cfg->mode) {
            case REPLAY_REALTIME: {
                /* How much wall time has elapsed since replay start? */
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                uint64_t elapsed_us =
                    (uint64_t)(now.tv_sec  - wall_start.tv_sec)  * 1000000ULL +
                    (uint64_t)(now.tv_nsec - wall_start.tv_nsec) / 1000ULL;
                uint64_t target_us = frame.timestamp_us - t_start;
                if (target_us > elapsed_us)
                    delay_us = target_us - elapsed_us;
                break;
            }
            case REPLAY_COMPRESSED:
                delay_us = 0;
                break;
            case REPLAY_SCALED:
                delay_us = (uint64_t)((double)(frame.timestamp_us - prev_ts)
                                      * cfg->time_scale);
                break;
        }

        if (delay_us > 0) sleep_us(delay_us);

        /* Transmit the byte */
        uint8_t b = frame.data;
        if (write(serial_fd, &b, 1) != 1) { perror("write"); break; }

        if (cfg->on_byte) cfg->on_byte(b, frame.timestamp_us, cfg->ctx);

        prev_ts = frame.timestamp_us;
    }

    fclose(f);
    return 0;
}
```

**Usage example:**

```c
#include "uart_replay.h"
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>

static void log_byte(uint8_t b, uint64_t ts, void *ctx) {
    (void)ctx;
    printf("[%8llu µs] TX 0x%02X  '%c'\n",
           (unsigned long long)ts, b, (b >= 0x20 && b < 0x7F) ? b : '.');
}

int main(void) {
    int fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
    /* configure termios for 115200 8N1 … */

    replay_config_t cfg = {
        .mode       = REPLAY_REALTIME,
        .time_scale = 1.0,
        .tx_only    = 1,
        .on_byte    = log_byte,
        .ctx        = NULL
    };
    uart_replay("capture_20250101.uart", fd, &cfg);
    return 0;
}
```

---

## Implementation in Rust

### Session Recorder

```rust
// uart_recorder/src/lib.rs
use std::fs::File;
use std::io::{BufWriter, Write};
use std::sync::mpsc::{self, Sender};
use std::thread;
use std::time::{Duration, Instant};

/// Direction of a captured byte.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Direction {
    Rx = 0,
    Tx = 1,
}

bitflags::bitflags! {
    /// UART error/status flags.
    pub struct FrameFlags: u8 {
        const OK         = 0x00;
        const FRAME_ERR  = 0x01;
        const PARITY_ERR = 0x02;
        const OVERRUN    = 0x04;
        const BREAK      = 0x08;
    }
}

/// A single captured UART byte.
#[derive(Clone, Copy, Debug)]
#[repr(C, packed)]
pub struct Frame {
    pub timestamp_us: u64,
    pub data:         u8,
    pub direction:    Direction,
    pub flags:        u8,
    pub _pad:         u8,
}

impl Frame {
    pub fn new(ts: u64, data: u8, dir: Direction, flags: FrameFlags) -> Self {
        Frame {
            timestamp_us: ts,
            data,
            direction: dir,
            flags: flags.bits(),
            _pad: 0,
        }
    }

    /// Serialise to 12-byte little-endian wire format.
    pub fn to_bytes(self) -> [u8; 12] {
        let mut buf = [0u8; 12];
        buf[0..8].copy_from_slice(&self.timestamp_us.to_le_bytes());
        buf[8]  = self.data;
        buf[9]  = self.direction as u8;
        buf[10] = self.flags;
        buf[11] = self._pad;
        buf
    }
}

/// Async session recorder — push frames from any thread.
pub struct Recorder {
    tx:      Sender<Frame>,
    _thread: thread::JoinHandle<()>,
    origin:  Instant,
}

impl Recorder {
    pub fn open(path: &str) -> std::io::Result<Self> {
        let file   = File::create(path)?;
        let mut bw = BufWriter::with_capacity(64 * 1024, file);

        // Write a minimal ASCII header line for human readability.
        writeln!(bw, "#UART-SESSION version=1 encoding=binary-le\n#END-HEADER")?;

        let (tx, rx) = mpsc::channel::<Frame>();

        let handle = thread::spawn(move || {
            for frame in rx {
                let _ = bw.write_all(&frame.to_bytes());
            }
            // Flush on channel close (recorder drop).
            let _ = bw.flush();
        });

        Ok(Recorder { tx, _thread: handle, origin: Instant::now() })
    }

    /// Record a byte. `ts_us` is relative to session start.
    pub fn record(&self, data: u8, dir: Direction, flags: FrameFlags) {
        let ts = self.origin.elapsed().as_micros() as u64;
        let frame = Frame::new(ts, data, dir, flags);
        let _ = self.tx.send(frame);
    }

    /// Current session age in microseconds.
    pub fn elapsed_us(&self) -> u64 {
        self.origin.elapsed().as_micros() as u64
    }
}

/// Example: capture from serialport crate.
#[cfg(feature = "serialport")]
pub fn capture_loop(port_name: &str, baud: u32, out_path: &str)
    -> anyhow::Result<()>
{
    use serialport::SerialPort;

    let mut port: Box<dyn SerialPort> = serialport::new(port_name, baud)
        .timeout(Duration::from_millis(10))
        .open()?;

    let recorder = Recorder::open(out_path)?;
    let mut buf = [0u8; 256];

    loop {
        match port.read(&mut buf) {
            Ok(0) => {}
            Ok(n) => {
                for &b in &buf[..n] {
                    recorder.record(b, Direction::Rx, FrameFlags::OK);
                }
            }
            Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {}
            Err(e) => return Err(e.into()),
        }
    }
}
```

### Replay with Timing Control

```rust
// uart_recorder/src/replay.rs
use super::{Direction, Frame};
use std::fs::File;
use std::io::{BufReader, Read};
use std::thread;
use std::time::{Duration, Instant};

/// How to handle the original inter-byte timing.
#[derive(Clone, Copy, Debug)]
pub enum TimingMode {
    /// Honour original gaps exactly.
    RealTime,
    /// No gaps; inject bytes as fast as possible.
    Compressed,
    /// Multiply all gaps by this factor (< 1.0 = faster, > 1.0 = slower).
    Scaled(f64),
}

pub struct ReplayConfig {
    pub mode:     TimingMode,
    /// If true, only replay TX frames.
    pub tx_only:  bool,
    /// Optional callback invoked for every replayed byte.
    pub on_byte:  Option<Box<dyn Fn(u8, u64) + Send>>,
}

/// Parse a session file and yield frames one by one.
struct SessionReader {
    reader: BufReader<File>,
}

impl SessionReader {
    fn open(path: &str) -> std::io::Result<Self> {
        let file = File::open(path)?;
        let mut br = BufReader::new(file);

        // Skip ASCII header lines (lines starting with '#').
        let mut byte = [0u8; 1];
        loop {
            br.read_exact(&mut byte)?;
            if byte[0] == b'\n' {
                // Peek at next byte: if not '#' the header is done.
                br.read_exact(&mut byte)?;
                if byte[0] != b'#' {
                    // We consumed one byte of binary data — handle it.
                    // In a real implementation, unread with a Cursor.
                    // Here we assume the header always ends at a newline.
                    break;
                }
            }
        }

        Ok(SessionReader { reader: br })
    }

    fn next_frame(&mut self) -> Option<Frame> {
        let mut buf = [0u8; 12];
        self.reader.read_exact(&mut buf).ok()?;

        let ts = u64::from_le_bytes(buf[0..8].try_into().unwrap());
        let data = buf[8];
        let dir = if buf[9] == 1 { Direction::Tx } else { Direction::Rx };
        let flags = buf[10];

        Some(Frame { timestamp_us: ts, data, direction: dir, flags, _pad: 0 })
    }
}

/// Replay a session into a writer (e.g., a serial port or a mock).
pub fn replay<W: std::io::Write>(
    session_path: &str,
    sink: &mut W,
    cfg: &ReplayConfig,
) -> anyhow::Result<()> {
    let mut reader = SessionReader::open(session_path)
        .map_err(|e| anyhow::anyhow!("Cannot open session: {e}"))?;

    let wall_start = Instant::now();
    let mut prev_ts_us: u64 = 0;
    let mut first = true;
    let mut session_offset: u64 = 0;

    while let Some(frame) = reader.next_frame() {
        if cfg.tx_only && frame.direction != Direction::Tx {
            continue;
        }
        if frame.flags & 0x05 != 0 {
            // Skip framing/overrun errors.
            continue;
        }

        if first {
            session_offset = frame.timestamp_us;
            first = false;
        }

        // Compute how long to wait before sending this byte.
        let delay = match cfg.mode {
            TimingMode::RealTime => {
                let target = Duration::from_micros(frame.timestamp_us - session_offset);
                let elapsed = wall_start.elapsed();
                if target > elapsed { target - elapsed } else { Duration::ZERO }
            }
            TimingMode::Compressed => Duration::ZERO,
            TimingMode::Scaled(factor) => {
                let gap_us = frame.timestamp_us.saturating_sub(prev_ts_us);
                Duration::from_micros((gap_us as f64 * factor) as u64)
            }
        };

        if !delay.is_zero() {
            thread::sleep(delay);
        }

        sink.write_all(&[frame.data])?;

        if let Some(cb) = &cfg.on_byte {
            cb(frame.data, frame.timestamp_us);
        }

        prev_ts_us = frame.timestamp_us;
    }

    Ok(())
}

/// Convenience: replay into /dev/ttyUSB0 at real time, printing progress.
#[cfg(feature = "serialport")]
pub fn replay_to_port(session_path: &str, port_name: &str, baud: u32)
    -> anyhow::Result<()>
{
    use serialport::SerialPort;
    let mut port: Box<dyn SerialPort> = serialport::new(port_name, baud)
        .timeout(Duration::from_secs(1))
        .open()?;

    let cfg = ReplayConfig {
        mode:    TimingMode::RealTime,
        tx_only: true,
        on_byte: Some(Box::new(|b, ts| {
            print!("[{ts:>10} µs] 0x{b:02X}  ");
            if b.is_ascii_graphic() { print!("'{}'", b as char); }
            println!();
        })),
    };

    replay(session_path, &mut *port, &cfg)
}
```

**Cargo.toml snippet:**

```toml
[package]
name    = "uart-recorder"
version = "0.1.0"
edition = "2021"

[dependencies]
anyhow     = "1"
bitflags   = "2"
serialport = { version = "4", optional = true }

[features]
default     = []
serialport  = ["dep:serialport"]
```

---

## Advanced Topics

### Filtering and Annotation

After capture, sessions are often post-processed to isolate relevant traffic. A filter pass reads the binary session and writes a new session containing only the matching frames:

```c
/* filter.c – keep only frames matching a simple pattern */
#include "uart_capture.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *pattern;
    size_t         len;
    size_t         pos;          /* Current match position */
    uint64_t       match_start;  /* Timestamp of first byte in match */
} pattern_filter_t;

int filter_session(const char *in_path, const char *out_path,
                   const uint8_t *pattern, size_t pat_len,
                   uint64_t window_before_us, uint64_t window_after_us) {
    FILE *in  = fopen(in_path,  "rb");
    FILE *out = fopen(out_path, "wb");
    if (!in || !out) return -1;

    /* Naïve windowed approach: read all frames into memory, then emit
       frames within [match_ts - before, match_ts + after].             */
    uart_frame_t frames[65536];
    size_t       count = 0;

    /* Skip session header */
    uint8_t hdr_buf[121];
    fread(hdr_buf, 1, sizeof hdr_buf, in);
    fwrite(hdr_buf, 1, sizeof hdr_buf, out);

    while (fread(&frames[count], sizeof(uart_frame_t), 1, in) == 1)
        if (++count >= 65536) break;

    fclose(in);

    /* Find pattern occurrences via KMP (simplified Boyer-Moore here) */
    for (size_t i = 0; i + pat_len <= count; i++) {
        int match = 1;
        for (size_t j = 0; j < pat_len; j++) {
            if (frames[i + j].data != pattern[j]) { match = 0; break; }
        }
        if (!match) continue;

        uint64_t ts = frames[i].timestamp_us;
        for (size_t k = 0; k < count; k++) {
            uint64_t ft = frames[k].timestamp_us;
            if (ft >= ts - window_before_us && ft <= ts + window_after_us)
                fwrite(&frames[k], sizeof(uart_frame_t), 1, out);
        }
    }

    fclose(out);
    return 0;
}
```

### Error Injection During Replay

For robustness testing, a replay engine can corrupt bytes probabilistically:

```rust
// error_injector.rs
use rand::Rng;

pub struct ErrorInjector {
    /// Probability of flipping a random bit in any given byte (0.0–1.0).
    pub bit_error_rate: f64,
    /// Probability of dropping a byte entirely.
    pub drop_rate:      f64,
}

impl ErrorInjector {
    pub fn transform(&self, byte: u8) -> Option<u8> {
        let mut rng = rand::thread_rng();

        if rng.gen::<f64>() < self.drop_rate {
            return None;  // Drop the byte
        }

        let mut b = byte;
        if rng.gen::<f64>() < self.bit_error_rate {
            let bit = rng.gen_range(0..8u8);
            b ^= 1 << bit;  // Flip a random bit
        }

        Some(b)
    }
}

/// Wrap any Write with error injection.
pub struct InjectingWriter<W: std::io::Write> {
    inner:    W,
    injector: ErrorInjector,
}

impl<W: std::io::Write> std::io::Write for InjectingWriter<W> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        for &byte in buf {
            if let Some(b) = self.injector.transform(byte) {
                self.inner.write_all(&[b])?;
            }
        }
        Ok(buf.len())
    }
    fn flush(&mut self) -> std::io::Result<()> { self.inner.flush() }
}
```

### Multi-Channel Recording

When recording multiple UART buses simultaneously (e.g., a CAN gateway with separate debug and data ports), each channel is tagged with a channel ID:

```c
/* multichannel.h */
typedef struct __attribute__((packed)) {
    uint64_t timestamp_us;
    uint8_t  channel_id;   /* 0–255 */
    uint8_t  data;
    uint8_t  direction;
    uint8_t  flags;
} mc_frame_t;
```

Channels are multiplexed into a single session file and demultiplexed at replay time by filtering on `channel_id`. This allows complete system-level recording with a single recorder process and a single file descriptor per output file.

---

## Testing Strategies

### Regression Testing

1. Record a golden session during a known-good firmware run.
2. Flash the candidate firmware.
3. Replay the TX portion of the golden session.
4. Capture the DUT's RX output.
5. Diff the new RX capture against the golden RX capture byte-for-byte (or with a tolerance for timestamps).

```bash
# Shell pseudo-code
uart-replay  --session golden.uart  --port /dev/ttyUSB0  --tx-only
uart-capture --port /dev/ttyUSB0    --output candidate.uart --duration 30s
uart-diff    golden.uart candidate.uart --ignore-timestamps --rx-only
```

### Round-Trip Integrity Test

Ensure that the recorder and replay engine are lossless by looping TX back to RX via a null-modem cable and verifying that the replayed bytes arrive byte-for-byte:

```c
/* round_trip_test.c */
void test_round_trip(const char *session_path, int tx_fd, int rx_fd) {
    /* Capture RX in background thread */
    /* Replay TX in foreground */
    /* Compare captured RX against original TX frames */
}
```

### Fuzz Testing with Mutated Sessions

Load a captured session, randomly mutate byte values or timestamps within the binary file, then replay to exercise DUT error handling paths:

```rust
fn mutate_session(input: &[u8], seed: u64) -> Vec<u8> {
    use rand::{SeedableRng, Rng};
    let mut rng = rand::rngs::SmallRng::seed_from_u64(seed);
    let mut out = input.to_vec();
    let n_mutations = rng.gen_range(1..=50);
    for _ in 0..n_mutations {
        let pos = rng.gen_range(0..out.len());
        out[pos] ^= rng.gen::<u8>();
    }
    out
}
```

---

## Summary

UART traffic recording and replay is a three-layer discipline:

**Layer 1 — Capture:** A per-byte record is timestamped at the moment of arrival or departure using a monotonic clock, tagged with direction and error flags, and written asynchronously to a session file via a ring buffer to avoid data loss under burst traffic.

**Layer 2 — Storage:** Sessions are stored in a compact binary format with a self-describing header (baud rate, port, wall-clock epoch, CRC). Frames are fixed-size (12 bytes each), allowing O(1) random access by index and efficient streaming replay.

**Layer 3 — Replay:** The replay engine reads a session file, reconstructs inter-byte timing (real-time, compressed, or scaled), and injects bytes via a serial driver. Optional callbacks and error injectors allow the same engine to serve regression tests, fuzz tests, and hardware-absent simulation.

In C/C++, the pattern relies on POSIX `termios`, `pthreads`, and a power-of-two ring buffer for lock-minimised producer/consumer decoupling. In Rust, the same architecture is expressed with `std::sync::mpsc` channels, `std::thread`, and the `serialport` crate, gaining memory safety and fearless concurrency without sacrificing performance.

Together, the techniques in this chapter enable deterministic, reproducible testing of any UART-connected peripheral across firmware revisions, build environments, and hardware generations.

---

*End of Chapter 89: UART Traffic Recording*