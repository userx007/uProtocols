# 41. GPS Module Integration

- **UART Configuration** — wiring, 9600 8N1 settings, ISR ring buffer, and DMA + idle-line detection for STM32
- **NMEA 0183 Protocol** — sentence anatomy, field layout, and all major sentence types (GGA, RMC, GSA, GSV, VTG, GLL) with annotated examples
- **Checksum Validation** — XOR algorithm explained and implemented in both languages

**C/C++ Code:**
- `uart_gps.h` / `uart_gps.c` — ring buffer, UART ISR callback, sentence extractor, checksum validator, coordinate converter, full GGA and RMC parsers
- `main.c` — dispatch loop with formatted output
- DMA + idle-line interrupt approach for high-efficiency reception

**Rust Code:**
- `nmea.rs` — type-safe `GgaData`/`RmcData` structs, `NmeaError` enum, checksum validator, field iterator, GGA and RMC parsers
- `uart_reader.rs` — `serialport`-based reader and sentence dispatcher
- `main.rs` — full application loop with pattern matching
- Unit tests covering checksum, field parsing, and coordinate conversion

**Advanced Topics:** multi-constellation talker IDs, proprietary UBX commands, Haversine distance, and a pitfalls reference table.

## Parsing NMEA Sentences from GPS Receivers via UART

---

## Table of Contents

1. [Introduction](#introduction)
2. [UART Communication with GPS Modules](#uart-communication-with-gps-modules)
3. [NMEA 0183 Protocol Overview](#nmea-0183-protocol-overview)
4. [Key NMEA Sentence Types](#key-nmea-sentence-types)
5. [Checksum Validation](#checksum-validation)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Advanced Topics](#advanced-topics)
9. [Summary](#summary)

---

## Introduction

GPS (Global Positioning System) modules are widely used in embedded systems for location tracking, navigation, timing, and geofencing applications. Most consumer and industrial GPS modules communicate over a standard **UART (Universal Asynchronous Receiver/Transmitter)** serial interface using the **NMEA 0183** protocol — a human-readable, ASCII-based messaging standard defined by the National Marine Electronics Association.

Understanding how to correctly configure the UART peripheral, receive a stream of NMEA sentences, parse them efficiently, and validate their integrity is a foundational skill in embedded GPS integration.

Typical GPS modules that use this interface include:
- **u-blox NEO-6M / NEO-M8N** — widely used hobbyist and industrial modules
- **Quectel L76 / L80** — compact SMD GPS receivers
- **SiRF Star IV / SiRFstar V** — high-sensitivity modules
- **MediaTek MT3339** — used in Adafruit Ultimate GPS

---

## UART Communication with GPS Modules

### Typical Hardware Configuration

GPS modules almost universally use a **3.3V TTL UART** interface. The default communication parameters are:

| Parameter     | Typical Value        |
|---------------|----------------------|
| Baud Rate     | 9600 bps (default)   |
| Data Bits     | 8                    |
| Parity        | None                 |
| Stop Bits     | 1                    |
| Flow Control  | None                 |

Many modules support higher baud rates (38400, 115200) after initial configuration via proprietary commands (e.g., UBX protocol for u-blox modules). For reliable parsing, the UART peripheral must be configured to match.

### Data Flow

The GPS module continuously transmits NMEA sentences at a configurable rate (typically 1 Hz). Each sentence ends with `\r\n` (CR LF). The host microcontroller must:

1. Receive bytes via UART interrupt or DMA into a ring buffer
2. Detect complete sentences delimited by `\r\n`
3. Pass complete sentences to the parser
4. Extract fields and validate the checksum

### Connection Diagram

```
GPS Module           Microcontroller
----------           ---------------
   TX  ─────────────►  RX (UART)
   RX  ◄─────────────  TX (UART)
  VCC  ─────────────►  3.3V
  GND  ─────────────►  GND
  PPS  ─────────────►  GPIO (optional, 1 pulse-per-second)
```

The **PPS (Pulse Per Second)** pin outputs a precise 1 Hz pulse synchronized to GPS time — useful for precision timekeeping.

---

## NMEA 0183 Protocol Overview

### Sentence Structure

Every NMEA 0183 sentence follows this format:

```
$<TalkerID><SentenceID>,<field1>,<field2>,...,<fieldN>*<checksum>\r\n
```

| Component      | Description                                                       |
|----------------|-------------------------------------------------------------------|
| `$`            | Start delimiter                                                   |
| `TalkerID`     | 2-character source identifier (e.g., `GP` = GPS, `GL` = GLONASS, `GN` = combined) |
| `SentenceID`   | 3-character sentence type (e.g., `GGA`, `RMC`, `GSA`)            |
| `,`            | Field delimiter                                                   |
| Fields         | Comma-separated ASCII values (empty fields are `,,`)              |
| `*`            | Checksum delimiter                                                |
| `checksum`     | 2-digit uppercase hex XOR of all bytes between `$` and `*`       |
| `\r\n`         | CRLF sentence terminator                                          |

### Example Sentence

```
$GPGGA,092750.000,5321.6802,N,00630.3372,W,1,8,1.03,61.7,M,55.2,M,,*76\r\n
```

- Talker: `GP` (GPS)
- Sentence: `GGA` (Global Positioning Fix Data)
- Time: 09:27:50 UTC
- Latitude: 53°21.6802' N
- Longitude: 006°30.3372' W
- Fix Quality: 1 (GPS fix)
- Satellites: 8
- HDOP: 1.03
- Altitude: 61.7m MSL
- Checksum: `76`

---

## Key NMEA Sentence Types

### GGA — Global Positioning Fix Data

The most comprehensive position sentence:

```
$GPGGA,HHMMSS.ss,LLLL.LL,a,YYYYY.YY,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
         |        |        | |         | | |   |   |   | |   |  |    |
         Time     Lat     N/S Lon      E/W Fix Sats HDOP Alt  |  Geoid Sep  DGPS
```

**Fix Quality Codes:**
- `0` = No fix
- `1` = GPS fix
- `2` = DGPS fix
- `4` = RTK fix
- `5` = Float RTK

### RMC — Recommended Minimum Navigation Information

The essential "minimum" sentence — includes position, velocity, date, and validity:

```
$GPRMC,HHMMSS.ss,A,LLLL.LL,a,YYYYY.YY,a,x.x,x.x,DDMMYY,x.x,a,m*hh
                  |                                                |
                  A=Active/V=Void                                 Mode
```

### GSA — GNSS DOP and Active Satellites

Reports dilution of precision values and which satellites are used in the fix:

```
$GPGSA,A,3,04,05,09,12,24,,,,,,,,,1.5,1.0,1.1*38
        | | |                      |   |   |
       Auto Fix  SV PRNs          PDOP HDOP VDOP
```

### GSV — GNSS Satellites in View

Reports azimuth, elevation, and SNR for each visible satellite:

```
$GPGSV,3,1,12,04,47,196,42,09,33,107,47,16,21,326,39,26,16,245,36*74
        | | |  |  |   |   |
       Total Msg# Sats  PRN El Az  SNR
```

### VTG — Track Made Good and Ground Speed

```
$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K,A*27
        |         |       |       |
       Track(True) Track(Mag) Knots  km/h
```

### GLL — Geographic Position

Simplified lat/lon + time:

```
$GPGLL,4916.45,N,12311.12,W,225444,A,A*1D
```

---

## Checksum Validation

The NMEA checksum is computed as the **XOR of all bytes between `$` and `*`** (exclusive).

```
Sentence: $GPGGA,...,data*4B\r\n
           ^                ^
           Start (excluded) Checksum delimiter (excluded)
```

Verification algorithm:
1. Find `$` (start)
2. XOR all bytes from the character after `$` up to (not including) `*`
3. Compare result with the 2-hex-digit value after `*`

---

## C/C++ Implementation

### 1. UART Ring Buffer Setup (Bare-metal / CMSIS)

```c
// uart_gps.h
#ifndef UART_GPS_H
#define UART_GPS_H

#include <stdint.h>
#include <stdbool.h>

#define GPS_UART         USART2
#define GPS_BAUD         9600
#define GPS_RX_BUF_SIZE  512
#define NMEA_MAX_LEN     100

// Ring buffer structure
typedef struct {
    uint8_t  buf[GPS_RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} RingBuffer;

// Parsed GGA data
typedef struct {
    bool     valid;
    uint8_t  hour, minute;
    float    second;
    double   latitude;   // decimal degrees, negative = South
    double   longitude;  // decimal degrees, negative = West
    uint8_t  fix_quality;
    uint8_t  satellites;
    float    hdop;
    float    altitude_m;
    float    geoid_sep_m;
} GGA_Data;

// Parsed RMC data
typedef struct {
    bool     valid;
    bool     active;     // A = active, V = void
    double   latitude;
    double   longitude;
    float    speed_knots;
    float    course_deg;
    uint8_t  day, month;
    uint16_t year;
} RMC_Data;

void     gps_uart_init(void);
bool     gps_read_sentence(char *sentence, uint16_t max_len);
bool     nmea_validate_checksum(const char *sentence);
bool     nmea_parse_gga(const char *sentence, GGA_Data *out);
bool     nmea_parse_rmc(const char *sentence, RMC_Data *out);

#endif // UART_GPS_H
```

```c
// uart_gps.c
#include "uart_gps.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// -- Ring Buffer Implementation ------------------------------------------

static RingBuffer rx_buf;

static inline bool rb_empty(const RingBuffer *rb) {
    return rb->head == rb->tail;
}

static inline bool rb_full(const RingBuffer *rb) {
    return ((rb->head + 1) % GPS_RX_BUF_SIZE) == rb->tail;
}

static inline void rb_push(RingBuffer *rb, uint8_t byte) {
    if (!rb_full(rb)) {
        rb->buf[rb->head] = byte;
        rb->head = (rb->head + 1) % GPS_RX_BUF_SIZE;
    }
}

static inline bool rb_pop(RingBuffer *rb, uint8_t *byte) {
    if (rb_empty(rb)) return false;
    *byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % GPS_RX_BUF_SIZE;
    return true;
}

// -- UART ISR (STM32-style) -----------------------------------------------

// Called from USART2_IRQHandler in your startup/interrupt code
void gps_uart_rx_callback(uint8_t byte) {
    rb_push(&rx_buf, byte);
}

// -- UART Init (STM32 HAL example, adapt to your platform) -----------------

void gps_uart_init(void) {
    // Example for STM32 using HAL -- adapt as needed
    // __HAL_RCC_USART2_CLK_ENABLE();
    // huart2.Instance = USART2;
    // huart2.Init.BaudRate = GPS_BAUD;
    // huart2.Init.WordLength = UART_WORDLENGTH_8B;
    // huart2.Init.StopBits = UART_STOPBITS_1;
    // huart2.Init.Parity = UART_PARITY_NONE;
    // huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    // huart2.Init.Mode = UART_MODE_RX;
    // HAL_UART_Init(&huart2);
    // HAL_UART_Receive_IT(&huart2, ...); // Enable interrupt reception

    memset(&rx_buf, 0, sizeof(rx_buf));
}

// -- Sentence Extraction ---------------------------------------------------

/**
 * Read one complete NMEA sentence from the ring buffer.
 * Returns true if a full sentence (ending with \r\n) was extracted.
 */
bool gps_read_sentence(char *sentence, uint16_t max_len) {
    static char line_buf[NMEA_MAX_LEN];
    static uint16_t line_pos = 0;

    uint8_t byte;
    while (rb_pop(&rx_buf, &byte)) {
        if (byte == '$') {
            // Start of new sentence — reset buffer
            line_pos = 0;
            line_buf[line_pos++] = '$';
        } else if (byte == '\n' && line_pos > 0) {
            // End of sentence
            if (line_pos > 0 && line_buf[line_pos - 1] == '\r') {
                line_buf[line_pos - 1] = '\0'; // Remove trailing \r
            } else {
                line_buf[line_pos] = '\0';
            }
            if (line_pos < max_len) {
                memcpy(sentence, line_buf, line_pos + 1);
                line_pos = 0;
                return true;
            }
            line_pos = 0;
        } else if (line_pos < NMEA_MAX_LEN - 1 && line_pos > 0) {
            line_buf[line_pos++] = (char)byte;
        }
    }
    return false;
}

// -- Checksum Validation ---------------------------------------------------

/**
 * Validate NMEA checksum.
 * Sentence format: $...*XX  where XX is the 2-hex-digit XOR checksum.
 */
bool nmea_validate_checksum(const char *sentence) {
    if (!sentence || sentence[0] != '$') return false;

    const char *star = strchr(sentence, '*');
    if (!star || strlen(star) < 3) return false;

    // Compute XOR of all bytes between '$' (exclusive) and '*' (exclusive)
    uint8_t computed = 0;
    for (const char *p = sentence + 1; p < star; p++) {
        computed ^= (uint8_t)*p;
    }

    // Parse provided checksum (2 hex digits after '*')
    uint8_t provided = (uint8_t)strtol(star + 1, NULL, 16);

    return computed == provided;
}

// -- NMEA Coordinate Conversion -------------------------------------------

/**
 * Convert NMEA latitude/longitude format (DDDMM.MMMM) to decimal degrees.
 */
static double nmea_coord_to_decimal(const char *coord, char direction) {
    if (!coord || coord[0] == '\0') return 0.0;

    double raw = atof(coord);
    int    degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100.0);
    double decimal = degrees + minutes / 60.0;

    if (direction == 'S' || direction == 'W') decimal = -decimal;
    return decimal;
}

// -- Minimal CSV field extractor ------------------------------------------

/**
 * Extract the Nth comma-separated field from a string into dst.
 * Returns true on success.
 */
static bool get_field(const char *src, int field_idx, char *dst, int dst_size) {
    int   idx = 0;
    const char *p = src;

    while (*p && idx < field_idx) {
        if (*p == ',') idx++;
        p++;
    }
    if (idx < field_idx) return false;

    int i = 0;
    while (*p && *p != ',' && *p != '*' && i < dst_size - 1) {
        dst[i++] = *p++;
    }
    dst[i] = '\0';
    return true;
}

// -- GGA Parser -----------------------------------------------------------

/**
 * Parse a $GPGGA or $GNGGA sentence into a GGA_Data structure.
 *
 * Field order:
 * $GPGGA,time,lat,N/S,lon,E/W,fix,sats,hdop,alt,M,geoid,M,dgps_age,dgps_id*cs
 *   0      1    2  3   4   5   6   7    8    9   10  11  12   13      14
 */
bool nmea_parse_gga(const char *sentence, GGA_Data *out) {
    if (!sentence || !out) return false;
    memset(out, 0, sizeof(*out));

    // Check sentence type
    if (strncmp(sentence + 1, "GPGGA", 5) != 0 &&
        strncmp(sentence + 1, "GNGGA", 5) != 0) {
        return false;
    }

    if (!nmea_validate_checksum(sentence)) return false;

    char field[32];

    // Field 1: UTC Time HHMMSS.ss
    if (get_field(sentence, 1, field, sizeof(field)) && field[0] != '\0') {
        char hh[3] = {field[0], field[1], '\0'};
        char mm[3] = {field[2], field[3], '\0'};
        out->hour   = (uint8_t)atoi(hh);
        out->minute = (uint8_t)atoi(mm);
        out->second = atof(field + 4);
    }

    // Field 2+3: Latitude and N/S
    char lat_str[16], lat_dir[4];
    get_field(sentence, 2, lat_str, sizeof(lat_str));
    get_field(sentence, 3, lat_dir, sizeof(lat_dir));
    out->latitude = nmea_coord_to_decimal(lat_str, lat_dir[0]);

    // Field 4+5: Longitude and E/W
    char lon_str[16], lon_dir[4];
    get_field(sentence, 4, lon_str, sizeof(lon_str));
    get_field(sentence, 5, lon_dir, sizeof(lon_dir));
    out->longitude = nmea_coord_to_decimal(lon_str, lon_dir[0]);

    // Field 6: Fix quality
    get_field(sentence, 6, field, sizeof(field));
    out->fix_quality = (uint8_t)atoi(field);
    out->valid = (out->fix_quality > 0);

    // Field 7: Number of satellites
    get_field(sentence, 7, field, sizeof(field));
    out->satellites = (uint8_t)atoi(field);

    // Field 8: HDOP
    get_field(sentence, 8, field, sizeof(field));
    out->hdop = atof(field);

    // Field 9: Altitude (MSL)
    get_field(sentence, 9, field, sizeof(field));
    out->altitude_m = atof(field);

    // Field 11: Geoid separation
    get_field(sentence, 11, field, sizeof(field));
    out->geoid_sep_m = atof(field);

    return true;
}

// -- RMC Parser -----------------------------------------------------------

/**
 * Parse a $GPRMC sentence into an RMC_Data structure.
 *
 * Field order:
 * $GPRMC,time,status,lat,N/S,lon,E/W,speed,course,date,mag_var,var_dir,mode*cs
 *   0      1     2    3   4   5   6    7      8     9     10     11     12
 */
bool nmea_parse_rmc(const char *sentence, RMC_Data *out) {
    if (!sentence || !out) return false;
    memset(out, 0, sizeof(*out));

    if (strncmp(sentence + 1, "GPRMC", 5) != 0 &&
        strncmp(sentence + 1, "GNRMC", 5) != 0) {
        return false;
    }

    if (!nmea_validate_checksum(sentence)) return false;

    char field[32];

    // Field 2: Status A=Active, V=Void
    get_field(sentence, 2, field, sizeof(field));
    out->active = (field[0] == 'A');
    out->valid  = out->active;

    // Fields 3+4: Latitude
    char lat_str[16], lat_dir[4];
    get_field(sentence, 3, lat_str, sizeof(lat_str));
    get_field(sentence, 4, lat_dir, sizeof(lat_dir));
    out->latitude = nmea_coord_to_decimal(lat_str, lat_dir[0]);

    // Fields 5+6: Longitude
    char lon_str[16], lon_dir[4];
    get_field(sentence, 5, lon_str, sizeof(lon_str));
    get_field(sentence, 6, lon_dir, sizeof(lon_dir));
    out->longitude = nmea_coord_to_decimal(lon_str, lon_dir[0]);

    // Field 7: Speed over ground (knots)
    get_field(sentence, 7, field, sizeof(field));
    out->speed_knots = atof(field);

    // Field 8: Course over ground (degrees true)
    get_field(sentence, 8, field, sizeof(field));
    out->course_deg = atof(field);

    // Field 9: Date DDMMYY
    if (get_field(sentence, 9, field, sizeof(field)) && strlen(field) == 6) {
        char dd[3] = {field[0], field[1], '\0'};
        char mo[3] = {field[2], field[3], '\0'};
        char yy[3] = {field[4], field[5], '\0'};
        out->day   = (uint8_t)atoi(dd);
        out->month = (uint8_t)atoi(mo);
        out->year  = 2000 + (uint16_t)atoi(yy);
    }

    return true;
}
```

### 2. Main Application Loop

```c
// main.c
#include <stdio.h>
#include "uart_gps.h"

int main(void) {
    gps_uart_init();

    char    sentence[NMEA_MAX_LEN];
    GGA_Data gga = {0};
    RMC_Data rmc = {0};

    printf("GPS Module Integration — NMEA Parser\n");
    printf("Waiting for GPS fix...\n");

    while (1) {
        if (gps_read_sentence(sentence, sizeof(sentence))) {
            // Identify and parse sentence type
            if (strncmp(sentence + 1, "GPGGA", 5) == 0 ||
                strncmp(sentence + 1, "GNGGA", 5) == 0) {

                if (nmea_parse_gga(sentence, &gga) && gga.valid) {
                    printf("[GGA] Time: %02u:%02u:%05.2f UTC | "
                           "Lat: %.6f | Lon: %.6f | "
                           "Alt: %.1fm | Sats: %u | HDOP: %.2f\n",
                           gga.hour, gga.minute, gga.second,
                           gga.latitude, gga.longitude,
                           gga.altitude_m, gga.satellites, gga.hdop);
                }

            } else if (strncmp(sentence + 1, "GPRMC", 5) == 0 ||
                       strncmp(sentence + 1, "GNRMC", 5) == 0) {

                if (nmea_parse_rmc(sentence, &rmc) && rmc.valid) {
                    printf("[RMC] %02u/%02u/%04u | Speed: %.1f kts | Course: %.1f°\n",
                           rmc.day, rmc.month, rmc.year,
                           rmc.speed_knots, rmc.course_deg);
                }
            }
        }

        // In a real RTOS/bare-metal application, yield here
        // or wait on a semaphore signalled by the ISR.
    }
    return 0;
}
```

### 3. DMA-Based Reception (STM32 HAL)

For high-baud-rate or low-CPU-overhead scenarios, DMA with idle-line detection is preferred over byte-level interrupts:

```c
// DMA + Idle Line Detection approach (STM32 HAL)
#include "stm32f4xx_hal.h"

#define DMA_BUF_SIZE  256

static uint8_t dma_buf[DMA_BUF_SIZE];
static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef  hdma_usart2_rx;

void gps_dma_uart_init(void) {
    // Configure UART with DMA
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 9600;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart2);

    // Start DMA receive in circular mode
    HAL_UART_Receive_DMA(&huart2, dma_buf, DMA_BUF_SIZE);

    // Enable UART Idle Line Interrupt to detect end of transmission burst
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
}

// In USART2_IRQHandler:
void USART2_IRQHandler(void) {
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart2);

        // Calculate how many bytes arrived
        uint16_t dma_remaining = __HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
        uint16_t received = DMA_BUF_SIZE - dma_remaining;

        // Process dma_buf[0..received-1] — copy to ring buffer, etc.
        for (uint16_t i = 0; i < received; i++) {
            rb_push(&rx_buf, dma_buf[i]);
        }

        // Restart DMA
        HAL_UART_Receive_DMA(&huart2, dma_buf, DMA_BUF_SIZE);
    }
    HAL_UART_IRQHandler(&huart2);
}
```

---

## Rust Implementation

### Project Setup

```toml
# Cargo.toml
[package]
name = "gps_nmea_parser"
version = "0.1.0"
edition = "2021"

[dependencies]
# For embedded no_std use: heapless, nb, cortex-m
# For std (desktop/testing) use:
serialport = "4.3"

[features]
default = ["std"]
std = []
```

### 1. NMEA Data Types and Parser

```rust
// src/nmea.rs

/// Parsed GGA (Global Positioning Fix Data) sentence
#[derive(Debug, Clone, Default)]
pub struct GgaData {
    pub valid:       bool,
    pub hour:        u8,
    pub minute:      u8,
    pub second:      f32,
    pub latitude:    f64,   // decimal degrees, negative = South
    pub longitude:   f64,   // decimal degrees, negative = West
    pub fix_quality: u8,
    pub satellites:  u8,
    pub hdop:        f32,
    pub altitude_m:  f32,
    pub geoid_sep_m: f32,
}

/// Parsed RMC (Recommended Minimum Navigation) sentence
#[derive(Debug, Clone, Default)]
pub struct RmcData {
    pub valid:        bool,
    pub active:       bool,
    pub latitude:     f64,
    pub longitude:    f64,
    pub speed_knots:  f32,
    pub course_deg:   f32,
    pub day:          u8,
    pub month:        u8,
    pub year:         u16,
}

/// NMEA parse errors
#[derive(Debug, PartialEq)]
pub enum NmeaError {
    InvalidStart,
    MissingChecksum,
    ChecksumMismatch,
    WrongSentenceType,
    ParseError(&'static str),
    FieldMissing,
}

impl core::fmt::Display for NmeaError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            NmeaError::InvalidStart       => write!(f, "Sentence does not start with '$'"),
            NmeaError::MissingChecksum    => write!(f, "No '*' checksum delimiter found"),
            NmeaError::ChecksumMismatch   => write!(f, "Checksum mismatch"),
            NmeaError::WrongSentenceType  => write!(f, "Unexpected sentence type"),
            NmeaError::ParseError(msg)    => write!(f, "Parse error: {msg}"),
            NmeaError::FieldMissing       => write!(f, "Required field missing"),
        }
    }
}

// -- Checksum Validation ---------------------------------------------------

/// Validate the XOR checksum of an NMEA sentence.
///
/// Format: $...*XX  where XX is uppercase hex XOR of bytes between $ and *
pub fn validate_checksum(sentence: &str) -> Result<(), NmeaError> {
    if !sentence.starts_with('$') {
        return Err(NmeaError::InvalidStart);
    }

    let star_pos = sentence.find('*').ok_or(NmeaError::MissingChecksum)?;

    // XOR all bytes between '$' (exclusive) and '*' (exclusive)
    let computed: u8 = sentence[1..star_pos]
        .bytes()
        .fold(0u8, |acc, b| acc ^ b);

    // Parse 2-character hex checksum after '*'
    let checksum_str = sentence.get(star_pos + 1..star_pos + 3)
        .ok_or(NmeaError::MissingChecksum)?;
    let provided = u8::from_str_radix(checksum_str, 16)
        .map_err(|_| NmeaError::ParseError("Invalid checksum hex"))?;

    if computed == provided {
        Ok(())
    } else {
        Err(NmeaError::ChecksumMismatch)
    }
}

// -- Coordinate Conversion ------------------------------------------------

/// Convert NMEA coordinate format (DDDMM.MMMM) + direction to decimal degrees.
fn nmea_to_decimal(coord: &str, direction: &str) -> Result<f64, NmeaError> {
    if coord.is_empty() {
        return Ok(0.0);
    }
    let raw: f64 = coord.parse().map_err(|_| NmeaError::ParseError("coordinate parse"))?;
    let degrees = (raw / 100.0).trunc() as i32;
    let minutes = raw - (degrees as f64 * 100.0);
    let mut decimal = degrees as f64 + minutes / 60.0;

    if direction == "S" || direction == "W" {
        decimal = -decimal;
    }
    Ok(decimal)
}

// -- Field Iterator -------------------------------------------------------

/// Iterate over comma-separated NMEA fields, stopping at '*'.
struct NmeaFields<'a> {
    remaining: &'a str,
    index:     usize,
}

impl<'a> NmeaFields<'a> {
    fn new(sentence: &'a str) -> Self {
        // Skip the "$GPGGA," prefix — we start at field 0 = the sentence ID
        NmeaFields { remaining: sentence, index: 0 }
    }

    /// Get the Nth field (0-indexed from the start of the sentence including type)
    fn get(sentence: &'a str, n: usize) -> Option<&'a str> {
        // Strip '$' and everything after '*'
        let inner = sentence.trim_start_matches('$');
        let inner = if let Some(pos) = inner.find('*') { &inner[..pos] } else { inner };
        inner.splitn(n + 2, ',').nth(n)
    }
}

// -- GGA Parser -----------------------------------------------------------

/// Parse a $GPGGA or $GNGGA sentence.
pub fn parse_gga(sentence: &str) -> Result<GgaData, NmeaError> {
    validate_checksum(sentence)?;

    // Accept GP, GN, GL talker prefixes
    let type_field = NmeaFields::get(sentence, 0).ok_or(NmeaError::FieldMissing)?;
    if !type_field.ends_with("GGA") {
        return Err(NmeaError::WrongSentenceType);
    }

    let get = |n: usize| NmeaFields::get(sentence, n).unwrap_or("");

    // Field 1: Time HHMMSS.ss
    let time_str = get(1);
    let (hour, minute, second) = if time_str.len() >= 6 {
        let h: u8  = time_str[0..2].parse().map_err(|_| NmeaError::ParseError("hour"))?;
        let m: u8  = time_str[2..4].parse().map_err(|_| NmeaError::ParseError("minute"))?;
        let s: f32 = time_str[4..].parse().map_err(|_| NmeaError::ParseError("second"))?;
        (h, m, s)
    } else {
        (0, 0, 0.0)
    };

    // Fields 2–5: Position
    let latitude  = nmea_to_decimal(get(2), get(3))?;
    let longitude = nmea_to_decimal(get(4), get(5))?;

    // Field 6: Fix quality
    let fix_quality: u8 = get(6).parse().unwrap_or(0);

    // Field 7: Satellites
    let satellites: u8 = get(7).parse().unwrap_or(0);

    // Field 8: HDOP
    let hdop: f32 = get(8).parse().unwrap_or(0.0);

    // Field 9: Altitude MSL
    let altitude_m: f32 = get(9).parse().unwrap_or(0.0);

    // Field 11: Geoid separation
    let geoid_sep_m: f32 = get(11).parse().unwrap_or(0.0);

    Ok(GgaData {
        valid: fix_quality > 0,
        hour,
        minute,
        second,
        latitude,
        longitude,
        fix_quality,
        satellites,
        hdop,
        altitude_m,
        geoid_sep_m,
    })
}

// -- RMC Parser -----------------------------------------------------------

/// Parse a $GPRMC or $GNRMC sentence.
pub fn parse_rmc(sentence: &str) -> Result<RmcData, NmeaError> {
    validate_checksum(sentence)?;

    let type_field = NmeaFields::get(sentence, 0).ok_or(NmeaError::FieldMissing)?;
    if !type_field.ends_with("RMC") {
        return Err(NmeaError::WrongSentenceType);
    }

    let get = |n: usize| NmeaFields::get(sentence, n).unwrap_or("");

    // Field 2: Status
    let active = get(2) == "A";

    // Fields 3–6: Position
    let latitude  = nmea_to_decimal(get(3), get(4))?;
    let longitude = nmea_to_decimal(get(5), get(6))?;

    // Field 7: Speed (knots)
    let speed_knots: f32 = get(7).parse().unwrap_or(0.0);

    // Field 8: Course (degrees)
    let course_deg: f32 = get(8).parse().unwrap_or(0.0);

    // Field 9: Date DDMMYY
    let date_str = get(9);
    let (day, month, year) = if date_str.len() == 6 {
        let d: u8  = date_str[0..2].parse().unwrap_or(0);
        let mo: u8 = date_str[2..4].parse().unwrap_or(0);
        let y: u16 = 2000 + date_str[4..6].parse::<u16>().unwrap_or(0);
        (d, mo, y)
    } else {
        (0, 0, 0)
    };

    Ok(RmcData {
        valid: active,
        active,
        latitude,
        longitude,
        speed_knots,
        course_deg,
        day,
        month,
        year,
    })
}
```

### 2. UART Reader and Sentence Accumulator

```rust
// src/uart_reader.rs
use serialport::{SerialPort, SerialPortBuilder};
use std::io::{BufRead, BufReader};
use std::time::Duration;

use crate::nmea::{parse_gga, parse_rmc, GgaData, RmcData, NmeaError};

pub struct GpsReader {
    reader: BufReader<Box<dyn SerialPort>>,
}

impl GpsReader {
    /// Open a serial port connected to a GPS module.
    ///
    /// # Example
    /// ```
    /// let gps = GpsReader::new("/dev/ttyUSB0", 9600).expect("Failed to open GPS port");
    /// ```
    pub fn new(port: &str, baud: u32) -> Result<Self, serialport::Error> {
        let port = serialport::new(port, baud)
            .timeout(Duration::from_millis(1000))
            .open()?;
        Ok(GpsReader {
            reader: BufReader::new(port),
        })
    }

    /// Read one NMEA sentence (blocking until `\n` is received).
    pub fn read_sentence(&mut self) -> Option<String> {
        let mut line = String::new();
        match self.reader.read_line(&mut line) {
            Ok(0)  => None,          // EOF
            Ok(_)  => Some(line.trim_end().to_string()),
            Err(_) => None,
        }
    }
}

/// Dispatch a raw NMEA sentence to the correct parser.
pub enum ParsedSentence {
    Gga(GgaData),
    Rmc(RmcData),
    Unknown,
}

pub fn dispatch(sentence: &str) -> Result<ParsedSentence, NmeaError> {
    if sentence.len() < 6 {
        return Ok(ParsedSentence::Unknown);
    }
    // Look at 3-char sentence type (positions 3–5 after $XX)
    let type_id = &sentence.get(3..6).unwrap_or("");
    match *type_id {
        "GGA" => parse_gga(sentence).map(ParsedSentence::Gga),
        "RMC" => parse_rmc(sentence).map(ParsedSentence::Rmc),
        _     => Ok(ParsedSentence::Unknown),
    }
}
```

### 3. Main Application

```rust
// src/main.rs
mod nmea;
mod uart_reader;

use uart_reader::{GpsReader, ParsedSentence, dispatch};

fn main() {
    // Adjust port and baud to match your system
    let port = "/dev/ttyUSB0";
    let baud = 9600;

    println!("GPS Module Integration — NMEA Parser (Rust)");
    println!("Opening {} at {} bps...", port, baud);

    let mut gps = match GpsReader::new(port, baud) {
        Ok(g)  => g,
        Err(e) => {
            eprintln!("Failed to open port: {e}");
            std::process::exit(1);
        }
    };

    println!("Waiting for GPS data...\n");

    loop {
        let Some(sentence) = gps.read_sentence() else { continue };

        if sentence.is_empty() || !sentence.starts_with('$') {
            continue;
        }

        match dispatch(&sentence) {
            Ok(ParsedSentence::Gga(gga)) if gga.valid => {
                println!(
                    "[GGA] {:02}:{:02}:{:05.2} UTC | \
                     Lat: {:.6}° | Lon: {:.6}° | \
                     Alt: {:.1}m | Sats: {} | HDOP: {:.2}",
                    gga.hour, gga.minute, gga.second,
                    gga.latitude, gga.longitude,
                    gga.altitude_m, gga.satellites, gga.hdop
                );
            }
            Ok(ParsedSentence::Rmc(rmc)) if rmc.valid => {
                println!(
                    "[RMC] {:02}/{:02}/{:04} | \
                     Speed: {:.1} kts | Course: {:.1}°",
                    rmc.day, rmc.month, rmc.year,
                    rmc.speed_knots, rmc.course_deg
                );
            }
            Err(e) => {
                eprintln!("[WARN] Parse error: {e} — Sentence: {sentence}");
            }
            _ => {} // Valid parse but no fix, or unknown sentence type
        }
    }
}
```

### 4. Unit Tests (Rust)

```rust
// In src/nmea.rs — append test module

#[cfg(test)]
mod tests {
    use super::*;

    const GGA_VALID: &str =
        "$GPGGA,092750.000,5321.6802,N,00630.3372,W,1,8,1.03,61.7,M,55.2,M,,*76";
    const RMC_VALID: &str =
        "$GPRMC,220516,A,5133.82,N,00042.24,W,173.8,231.8,130694,004.2,W*70";
    const BAD_CHECKSUM: &str =
        "$GPGGA,092750.000,5321.6802,N,00630.3372,W,1,8,1.03,61.7,M,55.2,M,,*77";

    #[test]
    fn test_checksum_valid() {
        assert!(validate_checksum(GGA_VALID).is_ok());
        assert!(validate_checksum(RMC_VALID).is_ok());
    }

    #[test]
    fn test_checksum_invalid() {
        assert_eq!(validate_checksum(BAD_CHECKSUM), Err(NmeaError::ChecksumMismatch));
    }

    #[test]
    fn test_parse_gga() {
        let gga = parse_gga(GGA_VALID).expect("GGA parse failed");
        assert!(gga.valid);
        assert_eq!(gga.hour, 9);
        assert_eq!(gga.minute, 27);
        assert_eq!(gga.satellites, 8);
        assert!((gga.altitude_m - 61.7).abs() < 0.01);
        // Latitude: 53°21.6802'N = 53 + 21.6802/60 = 53.361337°
        assert!((gga.latitude - 53.361337).abs() < 0.00001);
        // Longitude: 006°30.3372'W = -(6 + 30.3372/60) = -6.505620°
        assert!((gga.longitude - (-6.505620)).abs() < 0.00001);
    }

    #[test]
    fn test_parse_rmc() {
        let rmc = parse_rmc(RMC_VALID).expect("RMC parse failed");
        assert!(rmc.valid);
        assert!(rmc.active);
        assert_eq!(rmc.day,   13);
        assert_eq!(rmc.month,  6);
        assert_eq!(rmc.year, 1994);
        assert!((rmc.speed_knots - 173.8).abs() < 0.1);
        assert!((rmc.course_deg  - 231.8).abs() < 0.1);
    }

    #[test]
    fn test_coord_conversion() {
        // 5321.6802 N = 53.361337°
        let lat = nmea_to_decimal("5321.6802", "N").unwrap();
        assert!((lat - 53.361337).abs() < 0.00001);

        // 00630.3372 W = -6.505620°
        let lon = nmea_to_decimal("00630.3372", "W").unwrap();
        assert!((lon - (-6.505620)).abs() < 0.00001);
    }
}
```

---

## Advanced Topics

### Handling Multi-Constellation GNSS

Modern receivers output sentences from multiple constellations. The talker ID prefix determines the source:

| Talker | System         |
|--------|----------------|
| `GP`   | GPS (USA)      |
| `GL`   | GLONASS (Russia) |
| `GA`   | Galileo (EU)   |
| `GB`   | BeiDou (China) |
| `GN`   | Combined GNSS  |

Parsers should match on the 3-character sentence type (e.g., `GGA`, `RMC`) rather than the full 5-character identifier to remain talker-agnostic.

### Proprietary Extensions

Many modules extend NMEA with proprietary sentences starting with `$P`:

```
$PMTK251,115200*1F   — MediaTek: set baud rate to 115200
$PUBX,00,...         — u-blox: position/velocity output
$PGRME,...           — Garmin: estimated error information
```

These should be explicitly filtered or handled separately.

### Rate Configuration (u-blox example)

```c
// Send UBX-CFG-PRT to configure UART1 to 115200 bps
static const uint8_t ubx_set_baud[] = {
    0xB5, 0x62,             // UBX sync chars
    0x06, 0x00,             // Class: CFG, ID: PRT
    0x14, 0x00,             // Length: 20 bytes
    0x01,                   // Port ID: UART1
    0x00,                   // Reserved
    0x00, 0x00,             // txReady
    0xD0, 0x08, 0x00, 0x00, // Mode: 8N1
    0x00, 0xC2, 0x01, 0x00, // BaudRate: 115200
    0x07, 0x00,             // inProtoMask: UBX+NMEA+RTCM
    0x03, 0x00,             // outProtoMask: UBX+NMEA
    0x00, 0x00,             // flags
    0x00, 0x00,             // reserved
    0xBC, 0x5E              // CRC-8 Fletcher checksum
};
```

### Haversine Distance Calculation

Once you have decimal-degree coordinates, compute distance between two points:

```c
#include <math.h>
#define EARTH_RADIUS_M 6371000.0

double haversine_distance(double lat1, double lon1,
                          double lat2, double lon2) {
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
               sin(dlon / 2) * sin(dlon / 2);
    return EARTH_RADIUS_M * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}
```

### Common Pitfalls

| Issue | Cause | Solution |
|-------|-------|----------|
| Empty fields (`,,`) | No fix or optional field | Check for empty string before `atof`/`parse` |
| Garbled first sentence | Module powered mid-sentence | Discard until first `$` |
| Fix valid but coordinates 0.0 | `V` status in RMC | Always check `active`/`fix_quality` |
| Baud mismatch corruption | Rate changed without re-init | Re-init UART after sending configuration command |
| Duplicate positions | Multiple GGA/RMC per second | Timestamp-gate or only use latest per epoch |
| Coordinate sign error | Missed N/S or E/W field | Always parse direction field alongside coordinate |

---

## Summary

GPS module integration over UART revolves around three core tasks:

**1. UART Reception** — Configure the UART peripheral to 9600–115200 bps, 8N1, no flow control. Use interrupt-driven ring buffers or DMA with idle-line detection to efficiently accumulate incoming bytes without dropping data at higher update rates.

**2. NMEA Sentence Parsing** — Buffer incoming bytes until a `\r\n` terminator is detected, then dispatch to sentence-specific parsers. Always validate the XOR checksum before trusting any field data. Convert NMEA coordinate format (DDDMM.MMMM + direction) to signed decimal degrees for use in calculations.

**3. Fix Validation** — Never use position data without confirming validity: check `fix_quality > 0` in GGA, or `status == 'A'` (Active) in RMC. Track satellite count and HDOP to assess positional accuracy — HDOP below 2.0 with 6+ satellites is generally considered a good fix.

The C/C++ implementation demonstrates low-level UART ring-buffer management, ISR callbacks, field extraction with pointer arithmetic, and DMA-based reception for resource-constrained microcontrollers. The Rust implementation showcases type-safe parsing with `Result`-based error propagation, iterator-style field access, and built-in unit testing — making correctness guarantees explicit at compile time.

Both implementations share the same algorithmic approach and are readily portable to RTOS environments (FreeRTOS, Zephyr) by replacing the busy-wait loop with task-based blocking reads on a UART semaphore or message queue.

---

*Part of the UART Programming Series — Topic 41 of 50*