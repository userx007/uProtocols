# 39. Binary vs Text Protocols

**Content highlights:**

- **Conceptual depth** — Framing strategies (delimiter, length-prefix, COBS), number encoding, endianness, floating-point precision, and error detection (CRC-8/16/32, Fletcher, NMEA XOR)
- **8 code examples** across C and Rust:
  - C: Text CSV parser, Binary frame builder + CRC-16 state-machine parser, NMEA checksum validation, COBS encode/decode, C++ templated binary protocol
  - Rust: `FromStr`-based text parser with streaming `LineAccumulator`, binary frame builder/parser with `heapless`, typed `SensorPayload` with `to_be_bytes`, COBS encode/decode (all `no_std` compatible)
- **Hybrid approaches** — Base64-over-text, JSON/UART, MessagePack, CBOR
- **Decision guide** — A flowchart + comparison matrix to choose between formats
- **Summary** — Distills the key engineering take-aways including the common pattern of using text for debug/config and binary for telemetry

> **UART Topic Series** | Trade-offs between human-readable and binary message formats

---

## Table of Contents

1. [Introduction](#introduction)
2. [Text Protocols](#text-protocols)
3. [Binary Protocols](#binary-protocols)
4. [Detailed Trade-off Analysis](#detailed-trade-off-analysis)
5. [Framing and Delimiting Messages](#framing-and-delimiting-messages)
6. [Encoding Numbers](#encoding-numbers)
7. [Error Detection](#error-detection)
8. [C/C++ Implementation Examples](#cc-implementation-examples)
9. [Rust Implementation Examples](#rust-implementation-examples)
10. [Hybrid Approaches](#hybrid-approaches)
11. [Protocol Selection Guide](#protocol-selection-guide)
12. [Summary](#summary)

---

## Introduction

When designing a communication protocol over UART, one of the most fundamental architectural decisions is whether to use a **text-based** (ASCII/UTF-8) or **binary** message format. This choice affects bandwidth efficiency, implementation complexity, debuggability, interoperability, and robustness.

Both approaches have well-established use cases:

| Protocol Type | Real-world Examples |
|---|---|
| Text | AT commands, NMEA GPS, Modbus ASCII, HTTP headers, MQTT (payload) |
| Binary | Modbus RTU, COBS-framed packets, CANopen, custom sensor protocols |

Understanding the trade-offs allows engineers to make informed decisions for their specific embedded system requirements.

---

## Text Protocols

### Structure

Text protocols transmit data as human-readable ASCII or UTF-8 strings. Fields are separated by delimiters (commas, spaces, colons), and messages are terminated by newline characters (`\r\n` or `\n`).

**Example — AT Command style:**
```
AT+READ=SENSOR1\r\n
+READ: 23.45,60.2,1013\r\n
OK\r\n
```

**Example — NMEA GPS sentence:**
```
$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n
```

### Characteristics

- **Delimiter-framed**: Messages end with `\n` or `\r\n`; fields separated by `,` or ` `
- **Self-describing**: Field meaning is often implied by position or prefix
- **Human-typed**: Suitable for interactive terminal sessions
- **Variable length**: A value like `1234567890` takes 10 bytes as text but only 4 bytes as a 32-bit integer

---

## Binary Protocols

### Structure

Binary protocols encode data in its raw byte representation. A typical packet has a fixed or length-prefixed header, a payload of raw bytes, and a checksum footer.

**Example — Custom binary sensor packet:**
```
[0xAA] [0x55] [LEN:1] [CMD:1] [PAYLOAD:N] [CRC:2]
  SOF   SOF2   length   cmd     raw data    checksum
```

**Example — Modbus RTU Request (Read Holding Registers):**
```
[01] [03] [00 6B] [00 03] [B5 D2]
 ID  Func  Addr    Count   CRC16
```

### Characteristics

- **Length-prefixed or fixed-size frames**: Parser knows exactly how many bytes to consume
- **No wasted bytes**: Integers, floats, and arrays are packed tightly
- **Opaque**: Cannot be read with a terminal; requires a decoder
- **Deterministic timing**: Fixed-size frames allow precise latency budgeting

---

## Detailed Trade-off Analysis

### 1. Bandwidth Efficiency

Binary wins decisively here. Consider transmitting a 32-bit temperature sensor reading of `23456` (representing `234.56°C` with 2 decimal places):

| Format | Bytes Sent | Example |
|---|---|---|
| Text (decimal) | 5 bytes | `23456` |
| Text (with label) | 12 bytes | `TEMP=23456\r\n` |
| Binary (uint16) | 2 bytes | `0x5BA0` |
| Binary (float32) | 4 bytes | `0x43 0x6A 0x00 0x00` |

Over a slow 9600-baud link, this matters significantly: binary can achieve 4–10× better throughput.

### 2. Debuggability

Text wins completely. You can:

- Connect a terminal (PuTTY, minicom, screen) and read messages directly
- Log raw bytes to a file and parse later with standard tools (`grep`, `awk`, Python)
- Develop and test without specialized tooling

Binary data requires a protocol decoder. A single corrupted byte can make an entire frame unreadable without careful framing design.

### 3. Parsing Complexity

| Aspect | Text | Binary |
|---|---|---|
| Parser code size | Larger (`strtok`, `atoi`, `sscanf`) | Smaller (byte indexing, `memcpy`) |
| Parser CPU cost | Higher (string scanning) | Lower (direct cast / shift) |
| Robustness to noise | Poor (garbled text is hard to detect) | Better (checksum catches corruption) |
| Floating point | Requires `atof()` / `sprintf()` | Direct IEEE 754 cast |

### 4. Floating-Point Representation

Sending a float as text requires format decisions (how many decimals?) and introduces rounding artifacts. Binary sends the exact IEEE 754 bit pattern.

```
Float 3.14159265358979...
  Text: "3.141593\r\n" → 10 bytes, loses precision
  Binary: 0x40 0x49 0x0F 0xDB → 4 bytes, exact IEEE 754
```

### 5. Endianness

Binary protocols must declare byte order (big-endian / little-endian). Text has no such issue. Most embedded protocols use big-endian (network byte order), while x86/ARM typically use little-endian natively.

### 6. Extensibility

Text protocols are more naturally extensible: add a new field at the end of a CSV line or add a new command string. Binary protocols require careful versioning — adding a field changes the frame layout and breaks old parsers.

### 7. Embedded Resource Constraints

On microcontrollers (e.g., ATmega328P with 2 KB RAM), `printf` / `sprintf` for text formatting pulls in large libc code and uses significant stack space. Binary formatting with bit shifts uses minimal resources.

---

## Framing and Delimiting Messages

Correctly identifying message boundaries is critical in both protocol types.

### Text Framing: Line Termination

```
[PAYLOAD bytes][\r][\n]
```

The receiver buffers bytes until `\n` is seen. Simple but vulnerable to runaway data if `\n` is never received (buffer overflow).

### Binary Framing Options

#### Option A: Fixed-Length Frames
Every message is exactly N bytes. Simple to parse, wastes space for variable data.

#### Option B: Length-Prefix
```
[LEN : 1–2 bytes][PAYLOAD : LEN bytes][CRC : 2 bytes]
```
The receiver reads `LEN` first, then reads exactly that many payload bytes. Most common in practice.

#### Option C: Start-of-Frame + End-of-Frame Delimiters
```
[0xAA][0x55][PAYLOAD][0xCC]
```
Problem: if `0xCC` appears in payload data, the parser will de-sync. Requires byte stuffing.

#### Option D: COBS (Consistent Overhead Byte Stuffing)
COBS guarantees that a chosen delimiter byte (e.g., `0x00`) never appears in the payload. It adds at most 1 byte per 254 bytes of data — very efficient framing with strong synchronization properties.

```
Original:  [0x11][0x00][0x22][0x00][0x33]
COBS:      [0x02][0x11][0x03][0x22][0x02][0x33][0x00]
                                                 ^^^— frame delimiter
```

---

## Encoding Numbers

### Text Number Encoding

```
Value 65535 → "65535\0"  (6 bytes including null terminator)
Value 0     → "0\0"      (2 bytes)
Value -1024 → "-1024\0"  (6 bytes)
```

### Binary Integer Encoding

```
uint16_t 65535  → [0xFF][0xFF]           (2 bytes, big-endian)
int32_t  -1024  → [0xFF][0xFF][0xFC][0x00] (4 bytes, big-endian two's complement)
```

### Binary Float Encoding (IEEE 754)

```
float 3.14f → [0x40][0x48][0xF5][0xC3]  (4 bytes)
double 3.14 → [0x40][0x09][0x1E][0xB8][0x51][0xEB][0x85][0x1F]  (8 bytes)
```

---

## Error Detection

### Text Protocols
- **None**: Most simple text protocols have no error detection
- **XOR checksum**: NMEA appends `*HH` (2 hex digits of XOR of all characters between `$` and `*`)
- **CRC in hex**: Some protocols append a hex-encoded CRC

### Binary Protocols
- **CRC-8**: 1 byte, lightweight, used in 1-Wire, SMBus
- **CRC-16/CCITT**: 2 bytes, used in Modbus RTU, HDLC, X.25
- **CRC-32**: 4 bytes, used in Ethernet, ZIP, PNG
- **Fletcher-16**: 2 bytes, simple running sum checksum

CRC-16 (Modbus) is the most common choice for UART binary protocols balancing overhead vs. error detection capability.

---

## C/C++ Implementation Examples

### Example 1: Simple Text Protocol Parser (C)

```c
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define RX_BUF_SIZE 128

typedef struct {
    char    command[16];
    float   value;
    uint8_t sensor_id;
    int     valid;
} TextMessage;

/* Ring buffer for UART reception */
static char rx_buf[RX_BUF_SIZE];
static uint16_t rx_head = 0;

/* Called from UART RX interrupt */
void uart_rx_isr(uint8_t byte)
{
    if (rx_head < RX_BUF_SIZE - 1) {
        rx_buf[rx_head++] = (char)byte;
    }
}

/**
 * Parse a text message of the form:
 *   "CMD,<sensor_id>,<value>\r\n"
 *   Example: "TEMP,3,23.45\r\n"
 *
 * Returns 1 on success, 0 on failure.
 */
int parse_text_message(const char *line, TextMessage *msg)
{
    char buf[RX_BUF_SIZE];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Strip \r\n */
    char *p = buf;
    while (*p && *p != '\r' && *p != '\n') p++;
    *p = '\0';

    /* Tokenize */
    char *token = strtok(buf, ",");
    if (!token) return 0;
    strncpy(msg->command, token, sizeof(msg->command) - 1);

    token = strtok(NULL, ",");
    if (!token) return 0;
    msg->sensor_id = (uint8_t)atoi(token);

    token = strtok(NULL, ",");
    if (!token) return 0;
    msg->value = (float)atof(token);

    msg->valid = 1;
    return 1;
}

/* Format a text response */
int format_text_response(char *out, size_t out_size,
                         const char *cmd, uint8_t id, float val)
{
    return snprintf(out, out_size, "+%s,%u,%.2f\r\n", cmd, id, val);
}

/* --- Usage example --- */
void text_protocol_demo(void)
{
    const char *input = "TEMP,3,23.45\r\n";
    TextMessage msg = {0};

    if (parse_text_message(input, &msg)) {
        printf("CMD=%s  ID=%u  VAL=%.2f\n",
               msg.command, msg.sensor_id, msg.value);

        char response[64];
        format_text_response(response, sizeof(response),
                             msg.command, msg.sensor_id, msg.value * 1.8f + 32.0f);
        printf("Response: %s", response);
    }
}
```

---

### Example 2: Binary Protocol with CRC-16 (C)

```c
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------
 * Packet format:
 *   [0xAA][0x55][CMD:1][LEN:1][PAYLOAD:LEN][CRC16_HI][CRC16_LO]
 *  Total overhead = 6 bytes
 * --------------------------------------------------------------- */

#define SOF1        0xAAu
#define SOF2        0x55u
#define MAX_PAYLOAD 64u
#define HEADER_SIZE 4u    /* SOF1 + SOF2 + CMD + LEN */
#define CRC_SIZE    2u

typedef enum {
    CMD_READ_SENSOR  = 0x01,
    CMD_WRITE_CONFIG = 0x02,
    CMD_ACK          = 0x10,
    CMD_NACK         = 0x11,
} CmdId;

typedef struct __attribute__((packed)) {
    uint8_t  sof1;           /* 0xAA */
    uint8_t  sof2;           /* 0x55 */
    uint8_t  cmd;
    uint8_t  len;            /* payload length */
    uint8_t  payload[MAX_PAYLOAD];
    /* CRC appended after payload in wire format */
} BinaryFrame;

/* ---- CRC-16/CCITT (Modbus variant) ---- */
static uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x0001u)
            crc = (crc >> 1) ^ 0xA001u;  /* Modbus polynomial */
        else
            crc >>= 1;
    }
    return crc;
}

uint16_t crc16_buf(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = crc16_update(crc, data[i]);
    return crc;
}

/* ---- Frame builder ---- */
/**
 * Build a binary frame into `out` buffer.
 * Returns total bytes written, or -1 on error.
 */
int build_frame(uint8_t *out, size_t out_size,
                CmdId cmd, const uint8_t *payload, uint8_t plen)
{
    size_t total = HEADER_SIZE + plen + CRC_SIZE;
    if (total > out_size || plen > MAX_PAYLOAD)
        return -1;

    uint8_t *p = out;
    *p++ = SOF1;
    *p++ = SOF2;
    *p++ = (uint8_t)cmd;
    *p++ = plen;
    if (plen > 0) {
        memcpy(p, payload, plen);
        p += plen;
    }

    /* CRC covers CMD + LEN + PAYLOAD */
    uint16_t crc = crc16_buf(out + 2, 2u + plen);
    *p++ = (uint8_t)(crc >> 8);    /* big-endian CRC */
    *p++ = (uint8_t)(crc & 0xFFu);

    return (int)total;
}

/* ---- Frame parser state machine ---- */
typedef enum {
    PARSE_SOF1,
    PARSE_SOF2,
    PARSE_CMD,
    PARSE_LEN,
    PARSE_PAYLOAD,
    PARSE_CRC_HI,
    PARSE_CRC_LO,
} ParseState;

typedef struct {
    ParseState state;
    uint8_t    cmd;
    uint8_t    len;
    uint8_t    payload[MAX_PAYLOAD];
    uint8_t    payload_idx;
    uint8_t    crc_hi;
    int        frame_ready;
} FrameParser;

/**
 * Feed one byte into the parser.
 * Returns 1 when a complete, valid frame is available in `p`.
 */
int parser_feed(FrameParser *p, uint8_t byte)
{
    p->frame_ready = 0;

    switch (p->state) {
    case PARSE_SOF1:
        if (byte == SOF1) p->state = PARSE_SOF2;
        break;
    case PARSE_SOF2:
        p->state = (byte == SOF2) ? PARSE_CMD : PARSE_SOF1;
        break;
    case PARSE_CMD:
        p->cmd   = byte;
        p->state = PARSE_LEN;
        break;
    case PARSE_LEN:
        if (byte > MAX_PAYLOAD) { p->state = PARSE_SOF1; break; }
        p->len         = byte;
        p->payload_idx = 0;
        p->state       = (byte > 0) ? PARSE_PAYLOAD : PARSE_CRC_HI;
        break;
    case PARSE_PAYLOAD:
        p->payload[p->payload_idx++] = byte;
        if (p->payload_idx >= p->len)
            p->state = PARSE_CRC_HI;
        break;
    case PARSE_CRC_HI:
        p->crc_hi = byte;
        p->state  = PARSE_CRC_LO;
        break;
    case PARSE_CRC_LO: {
        uint16_t rx_crc  = ((uint16_t)p->crc_hi << 8) | byte;
        /* Reconstruct header + payload for CRC check */
        uint8_t hdr[2]   = { p->cmd, p->len };
        uint16_t calc    = crc16_buf(hdr, 2);
        calc             = /* continue over payload */
            crc16_buf(p->payload, p->len);
        /* Recalculate properly over cmd+len+payload */
        {
            uint16_t c = 0xFFFFu;
            c = crc16_update(c, p->cmd);
            c = crc16_update(c, p->len);
            for (int i = 0; i < p->len; i++)
                c = crc16_update(c, p->payload[i]);
            calc = c;
        }
        p->state = PARSE_SOF1;
        if (rx_crc == calc) {
            p->frame_ready = 1;
            return 1;
        }
        /* CRC mismatch: discard silently */
        break;
    }
    }
    return 0;
}

/* ---- Sensor payload encoding / decoding ---- */
typedef struct __attribute__((packed)) {
    uint8_t  sensor_id;
    int16_t  temperature_x100;  /* e.g. 2345 = 23.45 °C */
    uint16_t humidity_x100;     /* e.g. 6020 = 60.20 %  */
    uint32_t pressure_pa;       /* Pascal, e.g. 101325   */
} SensorPayload;

int encode_sensor(uint8_t *out, size_t out_size,
                  uint8_t id, float temp, float hum, uint32_t pres)
{
    SensorPayload sp = {
        .sensor_id         = id,
        .temperature_x100  = (int16_t)(temp * 100.0f),
        .humidity_x100     = (uint16_t)(hum * 100.0f),
        .pressure_pa       = pres,
    };
    return build_frame(out, out_size, CMD_READ_SENSOR,
                       (const uint8_t *)&sp, sizeof(sp));
}

void decode_sensor(const FrameParser *p)
{
    if (p->cmd != CMD_READ_SENSOR ||
        p->len  < sizeof(SensorPayload)) return;

    SensorPayload sp;
    memcpy(&sp, p->payload, sizeof(sp));

    printf("Sensor #%u: T=%.2f°C  H=%.2f%%  P=%lu Pa\n",
           sp.sensor_id,
           sp.temperature_x100 / 100.0f,
           sp.humidity_x100    / 100.0f,
           (unsigned long)sp.pressure_pa);
}
```

---

### Example 3: NMEA-style Text Protocol (C — checksum verification)

```c
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * Compute NMEA XOR checksum over characters between '$' and '*'.
 */
uint8_t nmea_checksum(const char *sentence)
{
    uint8_t cs = 0;
    const char *p = sentence;
    /* Skip leading '$' */
    if (*p == '$') p++;
    while (*p && *p != '*')
        cs ^= (uint8_t)*p++;
    return cs;
}

/**
 * Validate an NMEA sentence.
 * Format: $<data>*<HH>\r\n
 * Returns 1 if valid, 0 if checksum mismatch or malformed.
 */
int nmea_validate(const char *sentence)
{
    const char *star = strchr(sentence, '*');
    if (!star || strlen(star) < 3) return 0;

    uint8_t expected = nmea_checksum(sentence);
    uint8_t actual   = (uint8_t)strtoul(star + 1, NULL, 16);
    return expected == actual;
}

/**
 * Build an NMEA-style sentence with checksum.
 * `data` should NOT include '$', '*', or checksum.
 */
int nmea_build(char *out, size_t size, const char *data)
{
    /* Compute checksum by wrapping in $ and * */
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "$%s", data);
    uint8_t cs = nmea_checksum(tmp);
    return snprintf(out, size, "$%s*%02X\r\n", data, cs);
}

/* --- Usage example --- */
void nmea_demo(void)
{
    /* Build a custom sensor sentence */
    char sentence[64];
    nmea_build(sentence, sizeof(sentence), "PSENS,23.45,60.2,101325");
    printf("Built:    %s", sentence);

    /* Validate it */
    printf("Valid:    %s\n", nmea_validate(sentence) ? "YES" : "NO");

    /* Validate a known NMEA sentence */
    const char *gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    printf("GGA valid: %s\n", nmea_validate(gga) ? "YES" : "NO");
}
```

---

### Example 4: COBS Framing in C

```c
#include <stdint.h>
#include <stddef.h>

/**
 * COBS encode `src` (length `src_len`) into `dst`.
 * `dst` must be at least src_len + 2 bytes.
 * The frame delimiter 0x00 is appended automatically.
 * Returns number of bytes written to dst.
 */
size_t cobs_encode(const uint8_t *src, size_t src_len,
                   uint8_t *dst)
{
    size_t  read_idx  = 0;
    size_t  write_idx = 1;           /* Reserve byte for first code */
    size_t  code_idx  = 0;           /* Position of current overhead byte */
    uint8_t code      = 1;

    while (read_idx < src_len) {
        if (src[read_idx] == 0x00) {
            dst[code_idx]  = code;
            code_idx       = write_idx++;
            code           = 1;
        } else {
            dst[write_idx++] = src[read_idx];
            code++;
            if (code == 0xFF) {      /* Max block length reached */
                dst[code_idx]  = code;
                code_idx       = write_idx++;
                code           = 1;
            }
        }
        read_idx++;
    }
    dst[code_idx]    = code;
    dst[write_idx++] = 0x00;         /* Frame delimiter */
    return write_idx;
}

/**
 * COBS decode `src` into `dst`.
 * Stops at first 0x00 byte (frame delimiter).
 * Returns decoded length, or SIZE_MAX on error.
 */
size_t cobs_decode(const uint8_t *src, size_t src_len,
                   uint8_t *dst)
{
    size_t  read_idx  = 0;
    size_t  write_idx = 0;

    while (read_idx < src_len) {
        uint8_t code = src[read_idx++];
        if (code == 0x00) break;     /* End of frame */

        for (uint8_t i = 1; i < code; i++) {
            if (read_idx >= src_len) return SIZE_MAX;
            dst[write_idx++] = src[read_idx++];
        }
        if (code < 0xFF && read_idx < src_len)
            dst[write_idx++] = 0x00;
    }
    return write_idx;
}
```

---

### Example 5: C++ Binary Protocol with Templates

```cpp
#include <cstdint>
#include <cstring>
#include <array>
#include <optional>
#include <span>

namespace proto {

/* CRC-16/CCITT-FALSE */
constexpr uint16_t CRC16_POLY = 0x1021u;
constexpr uint16_t CRC16_INIT = 0xFFFFu;

constexpr uint16_t crc16_update(uint16_t crc, uint8_t byte) noexcept
{
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x8000u) ? (crc << 1) ^ CRC16_POLY : crc << 1;
    return crc;
}

constexpr uint16_t crc16(std::span<const uint8_t> data) noexcept
{
    uint16_t crc = CRC16_INIT;
    for (auto b : data)
        crc = crc16_update(crc, b);
    return crc;
}

/* ---- Header ---- */
struct [[nodiscard]] Header {
    uint8_t  sof1 = 0xAA;
    uint8_t  sof2 = 0x55;
    uint8_t  cmd;
    uint8_t  len;
};
static_assert(sizeof(Header) == 4);

/* ---- Typed payload serialisation ---- */
template<typename T>
concept Trivial = std::is_trivially_copyable_v<T>;

template<Trivial PayloadT, std::size_t BufN>
std::optional<std::size_t>
build_frame(std::array<uint8_t, BufN>& buf,
            uint8_t cmd,
            const PayloadT& payload) noexcept
{
    constexpr std::size_t total = sizeof(Header) + sizeof(PayloadT) + 2;
    if constexpr (total > BufN) return std::nullopt;

    Header hdr{ .cmd = cmd, .len = sizeof(PayloadT) };
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    std::memcpy(buf.data() + sizeof(hdr), &payload, sizeof(PayloadT));

    auto crc_span = std::span<const uint8_t>{ buf.data() + 2,
                                               sizeof(hdr) - 2 + sizeof(PayloadT) };
    uint16_t c = crc16(crc_span);
    buf[sizeof(hdr) + sizeof(PayloadT) + 0] = static_cast<uint8_t>(c >> 8);
    buf[sizeof(hdr) + sizeof(PayloadT) + 1] = static_cast<uint8_t>(c & 0xFFu);

    return total;
}

/* ---- Example payload types ---- */
struct [[gnu::packed]] SensorData {
    uint8_t  id;
    int16_t  temp_x100;
    uint16_t hum_x100;
    uint32_t pressure_pa;
};

struct [[gnu::packed]] ConfigData {
    uint8_t  sensor_id;
    uint16_t sample_rate_ms;
    uint8_t  flags;
};

} // namespace proto

/* --- Usage --- */
#include <cstdio>

void cpp_protocol_demo()
{
    std::array<uint8_t, 64> frame{};

    proto::SensorData sd{
        .id          = 3,
        .temp_x100   = 2345,
        .hum_x100    = 6020,
        .pressure_pa = 101325u
    };

    auto result = proto::build_frame(frame, 0x01, sd);
    if (result) {
        std::printf("Frame: %zu bytes\n", *result);
        for (std::size_t i = 0; i < *result; i++)
            std::printf("%02X ", frame[i]);
        std::printf("\n");
    }
}
```

---

## Rust Implementation Examples

### Example 6: Text Protocol Parser in Rust

```rust
use std::str::FromStr;

/// A parsed text message in the format:
///   "CMD,<sensor_id>,<value>\r\n"
///   Example: "TEMP,3,23.45"
#[derive(Debug, PartialEq)]
pub struct TextMessage {
    pub command:   String,
    pub sensor_id: u8,
    pub value:     f32,
}

#[derive(Debug, thiserror::Error)]
pub enum ParseError {
    #[error("Missing field: {0}")]
    MissingField(&'static str),
    #[error("Invalid integer: {0}")]
    InvalidInt(#[from] std::num::ParseIntError),
    #[error("Invalid float: {0}")]
    InvalidFloat(#[from] std::num::ParseFloatError),
}

impl FromStr for TextMessage {
    type Err = ParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        // Strip whitespace / CR / LF
        let s = s.trim_matches(|c| c == '\r' || c == '\n' || c == ' ');
        let mut parts = s.splitn(3, ',');

        let command = parts
            .next()
            .ok_or(ParseError::MissingField("command"))?
            .to_string();

        let sensor_id: u8 = parts
            .next()
            .ok_or(ParseError::MissingField("sensor_id"))?
            .trim()
            .parse()?;

        let value: f32 = parts
            .next()
            .ok_or(ParseError::MissingField("value"))?
            .trim()
            .parse()?;

        Ok(TextMessage { command, sensor_id, value })
    }
}

impl TextMessage {
    /// Format a response line.
    pub fn format_response(&self) -> String {
        format!("+{},{},{:.2}\r\n", self.command, self.sensor_id, self.value)
    }
}

// --- Streaming line accumulator ---

pub struct LineAccumulator {
    buf:      Vec<u8>,
    max_size: usize,
}

impl LineAccumulator {
    pub fn new(max_size: usize) -> Self {
        Self { buf: Vec::with_capacity(64), max_size }
    }

    /// Feed a byte. Returns `Some(line)` when a complete line is ready.
    pub fn feed(&mut self, byte: u8) -> Option<String> {
        if byte == b'\n' {
            let line = String::from_utf8_lossy(&self.buf).into_owned();
            self.buf.clear();
            Some(line)
        } else if byte != b'\r' {
            if self.buf.len() < self.max_size {
                self.buf.push(byte);
            }
            None
        } else {
            None  // Ignore CR
        }
    }
}

#[cfg(test)]
mod text_tests {
    use super::*;

    #[test]
    fn parse_valid_message() {
        let msg: TextMessage = "TEMP,3,23.45\r\n".parse().unwrap();
        assert_eq!(msg.command, "TEMP");
        assert_eq!(msg.sensor_id, 3);
        assert!((msg.value - 23.45).abs() < 0.001);
    }

    #[test]
    fn round_trip() {
        let original = TextMessage {
            command: "PRES".into(),
            sensor_id: 7,
            value: 1013.25,
        };
        let line = original.format_response();
        let parsed: TextMessage = line.trim_start_matches('+').parse().unwrap();
        assert_eq!(parsed.command, original.command);
        assert_eq!(parsed.sensor_id, original.sensor_id);
        assert!((parsed.value - original.value).abs() < 0.01);
    }
}
```

---

### Example 7: Binary Protocol with `zerocopy` / manual byte packing in Rust

```rust
use std::convert::TryInto;

// ---------------------------------------------------------------
// Frame format:
//   [0xAA][0x55][CMD:1][LEN:1][PAYLOAD:LEN][CRC_HI][CRC_LO]
// ---------------------------------------------------------------

const SOF1: u8 = 0xAA;
const SOF2: u8 = 0x55;
const MAX_PAYLOAD: usize = 64;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CmdId {
    ReadSensor  = 0x01,
    WriteConfig = 0x02,
    Ack         = 0x10,
    Nack        = 0x11,
}

impl TryFrom<u8> for CmdId {
    type Error = u8;
    fn try_from(v: u8) -> Result<Self, Self::Error> {
        match v {
            0x01 => Ok(Self::ReadSensor),
            0x02 => Ok(Self::WriteConfig),
            0x10 => Ok(Self::Ack),
            0x11 => Ok(Self::Nack),
            other => Err(other),
        }
    }
}

// CRC-16/Modbus
fn crc16_modbus(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            crc = if crc & 1 != 0 { (crc >> 1) ^ 0xA001 } else { crc >> 1 };
        }
    }
    crc
}

/// A validated, parsed frame.
#[derive(Debug)]
pub struct Frame {
    pub cmd:     CmdId,
    pub payload: heapless::Vec<u8, MAX_PAYLOAD>,
}

/// Build a binary frame into a stack-allocated buffer.
pub fn build_frame(
    cmd: CmdId,
    payload: &[u8],
) -> Option<heapless::Vec<u8, 72>> {
    if payload.len() > MAX_PAYLOAD {
        return None;
    }

    let mut buf: heapless::Vec<u8, 72> = heapless::Vec::new();

    buf.push(SOF1).ok()?;
    buf.push(SOF2).ok()?;
    buf.push(cmd as u8).ok()?;
    buf.push(payload.len() as u8).ok()?;
    buf.extend_from_slice(payload).ok()?;

    // CRC over CMD + LEN + PAYLOAD
    let crc = crc16_modbus(&buf[2..]);
    buf.push((crc >> 8) as u8).ok()?;
    buf.push((crc & 0xFF) as u8).ok()?;

    Some(buf)
}

/// Parse a complete frame from a byte slice.
/// Returns `Some(Frame)` on success, `None` if invalid.
pub fn parse_frame(data: &[u8]) -> Option<Frame> {
    if data.len() < 6 { return None; }
    if data[0] != SOF1 || data[1] != SOF2 { return None; }

    let cmd_byte = data[2];
    let len      = data[3] as usize;
    let total    = 4 + len + 2;

    if data.len() < total { return None; }

    // Verify CRC
    let rx_crc = u16::from_be_bytes([data[4 + len], data[4 + len + 1]]);
    let calc   = crc16_modbus(&data[2..4 + len]);
    if rx_crc != calc { return None; }

    let cmd     = CmdId::try_from(cmd_byte).ok()?;
    let mut payload: heapless::Vec<u8, MAX_PAYLOAD> = heapless::Vec::new();
    payload.extend_from_slice(&data[4..4 + len]).ok()?;

    Some(Frame { cmd, payload })
}

// ---- Typed payload encoding / decoding ----

/// Sensor reading payload (9 bytes packed).
#[derive(Debug, Clone, Copy)]
pub struct SensorPayload {
    pub sensor_id:       u8,
    pub temperature_x100: i16,   // e.g. 2345 → 23.45 °C
    pub humidity_x100:    u16,   // e.g. 6020 → 60.20 %
    pub pressure_pa:      u32,
}

impl SensorPayload {
    pub fn to_bytes(self) -> [u8; 9] {
        let mut b = [0u8; 9];
        b[0] = self.sensor_id;
        b[1..3].copy_from_slice(&self.temperature_x100.to_be_bytes());
        b[3..5].copy_from_slice(&self.humidity_x100.to_be_bytes());
        b[5..9].copy_from_slice(&self.pressure_pa.to_be_bytes());
        b
    }

    pub fn from_bytes(b: &[u8]) -> Option<Self> {
        if b.len() < 9 { return None; }
        Some(Self {
            sensor_id:        b[0],
            temperature_x100: i16::from_be_bytes(b[1..3].try_into().ok()?),
            humidity_x100:    u16::from_be_bytes(b[3..5].try_into().ok()?),
            pressure_pa:      u32::from_be_bytes(b[5..9].try_into().ok()?),
        })
    }

    pub fn temperature(&self) -> f32 { self.temperature_x100 as f32 / 100.0 }
    pub fn humidity(&self)    -> f32 { self.humidity_x100    as f32 / 100.0 }
}

#[cfg(test)]
mod binary_tests {
    use super::*;

    #[test]
    fn round_trip_sensor_frame() {
        let sp = SensorPayload {
            sensor_id:        3,
            temperature_x100: 2345,
            humidity_x100:    6020,
            pressure_pa:      101325,
        };
        let payload = sp.to_bytes();
        let frame   = build_frame(CmdId::ReadSensor, &payload).unwrap();
        let parsed  = parse_frame(&frame).unwrap();

        assert_eq!(parsed.cmd, CmdId::ReadSensor);
        let sp2 = SensorPayload::from_bytes(&parsed.payload).unwrap();
        assert_eq!(sp2.sensor_id, 3);
        assert!((sp2.temperature() - 23.45).abs() < 0.001);
        assert!((sp2.humidity()    - 60.20).abs() < 0.001);
        assert_eq!(sp2.pressure_pa, 101325);
    }

    #[test]
    fn detects_crc_error() {
        let payload = [0u8; 4];
        let mut frame = build_frame(CmdId::Ack, &payload).unwrap();
        // Corrupt last byte (CRC_LO)
        let last = frame.len() - 1;
        frame[last] ^= 0xFF;
        assert!(parse_frame(&frame).is_none());
    }
}
```

---

### Example 8: COBS Framing in Rust (no_std compatible)

```rust
/// COBS encoder — no heap allocation, no_std compatible.
/// `dst` must be at least `src.len() + 2` bytes.
/// Returns number of bytes written (includes trailing 0x00 delimiter).
pub fn cobs_encode(src: &[u8], dst: &mut [u8]) -> Option<usize> {
    if dst.len() < src.len() + 2 { return None; }

    let mut read      = 0usize;
    let mut write     = 1usize;
    let mut code_pos  = 0usize;
    let mut code: u8  = 1;

    while read < src.len() {
        if src[read] == 0x00 {
            dst[code_pos] = code;
            code_pos = write;
            write   += 1;
            code     = 1;
        } else {
            dst[write] = src[read];
            write += 1;
            code  += 1;
            if code == 0xFF {
                dst[code_pos] = code;
                code_pos = write;
                write   += 1;
                code     = 1;
            }
        }
        read += 1;
    }
    dst[code_pos] = code;
    dst[write]    = 0x00;
    Some(write + 1)
}

/// COBS decoder.
/// `dst` must be at least `src.len()` bytes.
/// Returns decoded length, or `None` on encoding error.
pub fn cobs_decode(src: &[u8], dst: &mut [u8]) -> Option<usize> {
    let mut read  = 0usize;
    let mut write = 0usize;

    while read < src.len() {
        let code = src[read];
        if code == 0x00 { break; }
        read += 1;

        for i in 1..code {
            if read >= src.len() { return None; }
            dst[write] = src[read];
            write += 1;
            read  += 1;
        }
        if code < 0xFF && read < src.len() {
            dst[write] = 0x00;
            write += 1;
        }
    }
    Some(write)
}

#[cfg(test)]
mod cobs_tests {
    use super::*;

    #[test]
    fn encode_decode_with_zeros() {
        let data = [0x11u8, 0x00, 0x22, 0x00, 0x33];
        let mut encoded = [0u8; 16];
        let mut decoded = [0u8; 16];

        let enc_len = cobs_encode(&data, &mut encoded).unwrap();
        // Verify no 0x00 in encoded data except the final delimiter
        assert!(encoded[..enc_len - 1].iter().all(|&b| b != 0x00));
        assert_eq!(encoded[enc_len - 1], 0x00);

        let dec_len = cobs_decode(&encoded[..enc_len], &mut decoded).unwrap();
        assert_eq!(&decoded[..dec_len], &data);
    }
}
```

---

## Hybrid Approaches

In practice, many production protocols combine elements of both paradigms:

### Header-Binary, Payload-Text

```
[LEN:2][TYPE:1]  <--- binary header
TEMP=23.45,HUM=60.20\r\n  <--- text payload
```
Used by some industrial gateways where the transport layer is binary but payload content is human-configurable.

### Base64 Encoding of Binary Data over Text Links

```
$BIN:YWJjZGVmZ2g=*3F\r\n
```
Binary data is Base64-encoded, sent as ASCII. Loses the space efficiency of binary but works over text-only channels (e.g., SMS, some logging systems). Increases data size by ~33%.

### JSON over UART

Increasingly popular for IoT devices, especially those bridging to web APIs:
```json
{"cmd":"read","id":3,"ts":1700000000}\r\n
{"sensor":3,"temp":23.45,"hum":60.2,"pres":101325}\r\n
```
Pros: Schema flexibility, ecosystem tooling. Cons: High RAM cost for parsing (`jsmn`, `cJSON`), verbose, no built-in checksumming.

### MessagePack / CBOR

Binary serialization formats that mimic JSON's structure:
- **MessagePack**: Compact binary encoding of JSON-like data; integer `23` becomes 1 byte instead of 2 characters
- **CBOR (RFC 7049)**: IETF standard, used in CoAP, constrained IoT devices

---

## Protocol Selection Guide

Use this decision tree to choose the right format for your system:

```
START
  │
  ├─► Is human readability during development critical?
  │     YES → Text protocol (AT-style, CSV, NMEA)
  │
  ├─► Is the link slower than 115200 baud OR do you transmit
  │   frequently (>10 msg/sec)?
  │     YES → Binary protocol
  │
  ├─► Are you running on a very constrained MCU (≤2 KB RAM)?
  │     YES → Binary protocol (avoid printf/sprintf overhead)
  │
  ├─► Do you need sub-millisecond deterministic latency?
  │     YES → Binary, fixed-size frames
  │
  ├─► Will messages be logged and analyzed offline?
  │     YES → Text (or binary with a decoder tool)
  │
  ├─► Do you need rich self-describing messages?
  │     YES → JSON / MessagePack / CBOR
  │
  └─► General recommendation:
        Development / debugging phase → Text
        Production / high-throughput  → Binary
```

### Quick Comparison Matrix

| Criterion | Text | Binary | Hybrid (JSON) |
|---|---|---|---|
| Bandwidth | Low | High | Medium |
| Parsing cost | High | Low | Very high |
| Debuggability | Excellent | Poor | Good |
| Type safety | None | Strong | Schema-defined |
| Error detection | Optional | Built-in | None |
| Endianness issues | None | Yes | None |
| Floating point precision | Lossy | Exact | Lossy |
| Embedded code size | Larger | Smaller | Largest |
| Extensibility | Easy | Requires versioning | Easy |
| no_std / bare-metal | Feasible | Ideal | Requires library |

---

## Summary

The choice between binary and text protocols for UART communication involves a set of well-understood trade-offs rather than a single correct answer:

**Text protocols** shine in interactive, developer-facing scenarios. Their strength is zero-tooling debuggability — a serial terminal is sufficient to read and write messages by hand. They suit baud rates where bandwidth is not the bottleneck and where flexibility and ease of extension are priorities. Their weakness is parsing overhead, variable message length, and the absence of built-in error detection.

**Binary protocols** are the workhorse of production embedded systems. They minimise byte count, reduce parsing cost to simple byte indexing and bit shifts, and naturally integrate checksums such as CRC-16 for error detection. The price is opacity: a logic analyser or a custom decoder is needed to inspect live traffic. Careful frame design — using length-prefix framing, COBS byte stuffing, or SOF/EOF delimiters — is essential for robust synchronisation after noise or reset events.

**Key implementation take-aways:**

- Always include a checksum in binary frames; CRC-16 (Modbus or CCITT) is the most common choice for UART.
- Use COBS framing when the channel is noisy and synchronisation robustness matters more than implementation simplicity.
- Pack multi-byte integers with an explicit byte order (big-endian is conventional for wire formats); document it in the protocol specification.
- For floating-point values, transmit scaled integers (e.g., temperature × 100 as `int16_t`) to avoid IEEE 754 endianness and precision concerns on resource-constrained devices.
- In Rust, `heapless` and manual `to_be_bytes()` / `from_be_bytes()` calls provide safe, `no_std`-compatible binary protocol implementation with zero heap allocation.
- In C/C++, `__attribute__((packed))` structures with `memcpy` (never pointer casting) provide portable binary serialisation without alignment traps.
- Hybrid approaches (JSON for initial prototyping, switching to binary for production) are a pragmatic strategy for teams that need rapid iteration early in development.

Ultimately, many mature embedded systems use both: a text-based debug/configuration interface (e.g., AT commands over a secondary UART) alongside a binary data telemetry channel (e.g., COBS-framed sensor packets over the primary UART).

---

*Part of the UART Programming Topic Series*