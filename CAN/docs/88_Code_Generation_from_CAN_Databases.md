# 88. Code Generation from CAN Databases

- **DBC Format** — grammar breakdown of `BO_`/`SG_` lines, the encoding fields (start bit, length, byte order, factor, offset, min/max, unit)
- **Generation Pipeline** — Parser → IR/AST → multi-target emitter diagram and the two core algorithms (bit extraction, physical scaling)
- **C/C++ output** — `typedef struct` per message, `engine_status_t` / `throttle_cmd_t`; `extract_bits_le` / `insert_bits_le` helpers; full pack + unpack implementations; a working **`dbcgen.cpp`** tool that reads a real DBC file and writes `.h`/`.c`
- **Rust output** — `struct` + `CanMessage` trait with `pack`/`unpack`; `TryFrom<u64>` for enum signals; `CodecError`; round-trip unit tests; a **`build.rs`** integration that generates code at compile time via `include!`; a standalone **`dbcgen.rs`** binary
- **Documentation generation** — Markdown table emitter in Rust producing signal reference docs
- **Advanced topics** — multiplexed signals (`M`/`mN` → C `union`), Motorola big-endian extraction, CAN FD (64-byte payloads)
- **Build integration** — CMake `add_custom_command`, Cargo `build.rs`, GitHub Actions staleness check
- **Summary table** — nine-row quick reference covering all key aspects

## Table of Contents

1. [Introduction](#introduction)
2. [DBC File Format](#dbc-file-format)
3. [Code Generation Concepts](#code-generation-concepts)
4. [Toolchain Overview](#toolchain-overview)
5. [C/C++ Code Generation](#cc-code-generation)
   - [Type Definitions](#type-definitions)
   - [Pack / Unpack Functions](#pack--unpack-functions)
   - [Signal Scaling and Offset](#signal-scaling-and-offset)
   - [Full Generated Header Example](#full-generated-header-example)
   - [A Minimal DBC-to-C Generator in C++](#a-minimal-dbc-to-c-generator-in-c)
6. [Rust Code Generation](#rust-code-generation)
   - [Rust Type Definitions and Traits](#rust-type-definitions-and-traits)
   - [Pack / Unpack in Rust](#pack--unpack-in-rust)
   - [Build-Script Code Generation with `build.rs`](#build-script-code-generation-with-buildrs)
   - [A Minimal DBC Parser and Generator in Rust](#a-minimal-dbc-parser-and-generator-in-rust)
7. [Documentation Generation](#documentation-generation)
8. [Advanced Topics](#advanced-topics)
   - [Multiplexed Messages](#multiplexed-messages)
   - [Endianness Handling](#endianness-handling)
   - [Extended Frames and FD](#extended-frames-and-fd)
9. [Integration into Build Systems](#integration-into-build-systems)
10. [Summary](#summary)

---

## Introduction

Modern automotive, industrial, and embedded systems heavily rely on the **Controller Area Network (CAN)** bus for inter-ECU communication. A CAN network is typically described by a **DBC file** (Database CAN) — a text-based format that enumerates every message, signal, data type, scaling factor, and enumeration on the bus.

Manually writing packing and unpacking code for hundreds of CAN signals is error-prone and difficult to keep in sync with the DBC specification as it evolves. **Code generation** solves this by automatically producing:

| Artefact | Description |
|---|---|
| **Type definitions** | `struct`, `enum`, `typedef` / Rust `struct` + `enum` per message |
| **Pack functions** | Serialise a typed structure into a raw 8-byte CAN frame payload |
| **Unpack functions** | Deserialise a raw payload into the typed structure |
| **Scaling helpers** | Apply/remove factor + offset + unit conversion |
| **Documentation** | Human-readable HTML/Markdown/Doxygen from the DBC |
| **Test stubs** | Round-trip unit tests |

The generated code is checked into source control alongside the DBC; any DBC change triggers a regeneration step in CI.

---

## DBC File Format

A minimal DBC file looks like:

```
VERSION ""

NS_ :

BS_:

BU_: ECU_ENGINE ECU_DASH

BO_ 100 ENGINE_STATUS: 8 ECU_ENGINE
 SG_ ENGINE_RPM : 0|16@1+ (0.25,0) [0|16383.75] "RPM" ECU_DASH
 SG_ COOLANT_TEMP : 16|8@1+ (1,-40) [-40|215] "degC" ECU_DASH
 SG_ ENGINE_ON : 24|1@1+ (1,0) [0|1] "" ECU_DASH

BO_ 200 THROTTLE_CMD: 4 ECU_DASH
 SG_ THROTTLE_POS : 0|10@1+ (0.1,0) [0|100] "%" ECU_ENGINE
 SG_ BRAKE_ACTIVE : 10|1@1+ (1,0) [0|1] "" ECU_ENGINE

VAL_ 100 ENGINE_ON 0 "OFF" 1 "ON" ;
```

### Signal Encoding Grammar

```
SG_ <name> : <start_bit>|<length>@<byte_order><value_type> (<factor>,<offset>) [<min>|<max>] "<unit>" <receivers>
```

| Field | Meaning |
|---|---|
| `start_bit` | LSB position (Intel) or MSB position (Motorola) |
| `length` | Bit length of signal |
| `@1` / `@0` | `1` = Intel (little-endian), `0` = Motorola (big-endian) |
| `+` / `-` | Unsigned / Signed |
| `factor`, `offset` | `physical = raw * factor + offset` |
| `min`, `max` | Physical range |

---

## Code Generation Concepts

The generator pipeline follows these stages:

```
┌──────────────┐     ┌───────────────┐     ┌──────────────────────────┐
│  DBC Parser  │────▶│  IR / AST     │────▶│  Code / Doc Emitter      │
│  (lexer +    │     │  Messages,    │     │  C header, C source,     │
│   parser)    │     │  Signals,     │     │  Rust module,            │
└──────────────┘     │  Enums        │     │  Markdown / HTML docs    │
                     └───────────────┘     └──────────────────────────┘
```

The **Intermediate Representation (IR)** decouples parsing from emission, allowing multiple output targets from one parse run.

### Key algorithms

**Bit-mask extraction (Intel/little-endian)**

```
raw_value = (payload >> start_bit) & ((1 << length) - 1)
```

**Bit-mask extraction (Motorola/big-endian)**  
Motorola start bit refers to the MSB; bits are numbered differently — the extraction must account for byte reversal.

**Physical value**

```
physical = (double)raw * factor + offset
```

**Raw from physical (for packing)**

```
raw = (uint64_t)round((physical - offset) / factor)
```

---

## Toolchain Overview

Popular open-source generators:

| Tool | Language | Output |
|---|---|---|
| **cantools** | Python | C, Python, documentation |
| **dbcc** | C | C source |
| **can-dbc-codegen** | Rust (build.rs) | Rust modules |
| **Vector CANdb++** | Proprietary | C, C++ |
| **OpenDBC** | Python | C, HTML docs |

This document shows how to implement these concepts yourself and how to use generated code.

---

## C/C++ Code Generation

### Type Definitions

The generator creates a `struct` per message, plus `enum` types for enumerated signals.

```c
/* ── AUTO-GENERATED — DO NOT EDIT ──────────────────────────────────────
 * Source : vehicle_network.dbc
 * Tool   : mydbcgen v1.0
 * Date   : 2025-08-01
 * ────────────────────────────────────────────────────────────────────── */
#ifndef VEHICLE_NETWORK_H
#define VEHICLE_NETWORK_H

#include <stdint.h>
#include <stdbool.h>

/* ── Message IDs ──────────────────────────────────────────────────────── */
#define CAN_ID_ENGINE_STATUS  (0x064U)   /* 100 dec, 8 bytes */
#define CAN_ID_THROTTLE_CMD   (0x0C8U)   /* 200 dec, 4 bytes */

/* ── Enumerations ─────────────────────────────────────────────────────── */
typedef enum {
    ENGINE_ON_OFF = 0,
    ENGINE_ON_ON  = 1
} engine_on_t;

/* ── Message structures ───────────────────────────────────────────────── */

/**
 * @brief ENGINE_STATUS (CAN ID 0x064, 8 bytes)
 * Sender: ECU_ENGINE
 *
 * Signals:
 *   ENGINE_RPM   [0..16383.75 RPM]   resolution 0.25
 *   COOLANT_TEMP [-40..215 degC]     resolution 1, offset -40
 *   ENGINE_ON    {OFF=0, ON=1}
 */
typedef struct {
    double      engine_rpm;       /**< RPM,   raw*0.25,        range [0, 16383.75]  */
    double      coolant_temp;     /**< degC,  raw*1 + (-40),   range [-40, 215]     */
    engine_on_t engine_on;        /**< enum,  raw*1,           range [0, 1]         */
} engine_status_t;

/**
 * @brief THROTTLE_CMD (CAN ID 0x0C8, 4 bytes)
 * Sender: ECU_DASH
 *
 * Signals:
 *   THROTTLE_POS  [0..100 %]  resolution 0.1
 *   BRAKE_ACTIVE  {0,1}
 */
typedef struct {
    double  throttle_pos;   /**< %,    raw*0.1,  range [0, 100] */
    bool    brake_active;   /**< bool, raw*1,    range [0, 1]   */
} throttle_cmd_t;

#endif /* VEHICLE_NETWORK_H */
```

---

### Pack / Unpack Functions

```c
/* vehicle_network.c — AUTO-GENERATED */
#include "vehicle_network.h"
#include <math.h>
#include <string.h>

/* ════════════════════════════════════════════════════════════════════════
 * Helpers
 * ════════════════════════════════════════════════════════════════════════ */

/** Extract an unsigned bit-field from a little-endian (Intel) payload */
static inline uint64_t extract_bits_le(const uint8_t *data, uint8_t start, uint8_t len)
{
    uint64_t raw = 0;
    memcpy(&raw, data, 8);            /* byte-copy avoids strict-aliasing UB */
    raw >>= start;
    raw  &= (len < 64) ? ((UINT64_C(1) << len) - 1u) : UINT64_MAX;
    return raw;
}

/** Insert an unsigned bit-field into a little-endian payload */
static inline void insert_bits_le(uint8_t *data, uint8_t start, uint8_t len,
                                   uint64_t value)
{
    uint64_t mask = (len < 64) ? ((UINT64_C(1) << len) - 1u) : UINT64_MAX;
    uint64_t buf  = 0;
    memcpy(&buf, data, 8);
    buf &= ~(mask << start);          /* clear target bits */
    buf |=  (value & mask) << start;  /* write new bits    */
    memcpy(data, &buf, 8);
}

/* ════════════════════════════════════════════════════════════════════════
 * ENGINE_STATUS — CAN ID 0x064
 * ════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Unpack a raw 8-byte CAN payload into engine_status_t.
 * @param dst   Destination struct (physical values).
 * @param data  Raw payload (8 bytes).
 */
void engine_status_unpack(engine_status_t *dst, const uint8_t data[8])
{
    /* ENGINE_RPM : bits [0..15], factor=0.25, offset=0, unsigned */
    {
        uint64_t raw = extract_bits_le(data, 0, 16);
        dst->engine_rpm = (double)raw * 0.25 + 0.0;
    }

    /* COOLANT_TEMP : bits [16..23], factor=1, offset=-40, unsigned */
    {
        uint64_t raw = extract_bits_le(data, 16, 8);
        dst->coolant_temp = (double)raw * 1.0 + (-40.0);
    }

    /* ENGINE_ON : bit [24], factor=1, offset=0, unsigned → enum */
    {
        uint64_t raw = extract_bits_le(data, 24, 1);
        dst->engine_on = (engine_on_t)raw;
    }
}

/**
 * @brief Pack an engine_status_t into a raw 8-byte CAN payload.
 * @param src   Source struct (physical values).
 * @param data  Output payload buffer (8 bytes, must be zeroed by caller).
 */
void engine_status_pack(const engine_status_t *src, uint8_t data[8])
{
    /* ENGINE_RPM */
    {
        uint64_t raw = (uint64_t)llround((src->engine_rpm - 0.0) / 0.25);
        insert_bits_le(data, 0, 16, raw);
    }

    /* COOLANT_TEMP */
    {
        uint64_t raw = (uint64_t)llround((src->coolant_temp - (-40.0)) / 1.0);
        insert_bits_le(data, 16, 8, raw);
    }

    /* ENGINE_ON */
    {
        uint64_t raw = (uint64_t)src->engine_on;
        insert_bits_le(data, 24, 1, raw);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * THROTTLE_CMD — CAN ID 0x0C8
 * ════════════════════════════════════════════════════════════════════════ */

void throttle_cmd_unpack(throttle_cmd_t *dst, const uint8_t data[8])
{
    /* THROTTLE_POS : bits [0..9], factor=0.1, offset=0 */
    {
        uint64_t raw = extract_bits_le(data, 0, 10);
        dst->throttle_pos = (double)raw * 0.1 + 0.0;
    }

    /* BRAKE_ACTIVE : bit [10] */
    {
        uint64_t raw = extract_bits_le(data, 10, 1);
        dst->brake_active = (bool)raw;
    }
}

void throttle_cmd_pack(const throttle_cmd_t *src, uint8_t data[8])
{
    /* THROTTLE_POS */
    {
        uint64_t raw = (uint64_t)llround((src->throttle_pos - 0.0) / 0.1);
        insert_bits_le(data, 0, 10, raw);
    }

    /* BRAKE_ACTIVE */
    {
        insert_bits_le(data, 10, 1, (uint64_t)src->brake_active);
    }
}
```

---

### Signal Scaling and Offset

```c
/* ── Inline accessors with range clamping (optional safety layer) ─────── */

static inline double engine_rpm_clamp(double v)
{
    if (v < 0.0)       return 0.0;
    if (v > 16383.75)  return 16383.75;
    return v;
}

static inline double coolant_temp_clamp(double v)
{
    if (v < -40.0) return -40.0;
    if (v > 215.0) return 215.0;
    return v;
}

/* Usage example */
void example_usage(void)
{
    /* --- UNPACK --- */
    uint8_t raw_frame[8] = { 0xE8, 0x03,  /* ENGINE_RPM raw=1000 → 250 RPM */
                              0x5A,        /* COOLANT_TEMP raw=90 → 50 degC  */
                              0x01,        /* ENGINE_ON = 1 (ON)             */
                              0x00, 0x00, 0x00, 0x00 };

    engine_status_t status;
    engine_status_unpack(&status, raw_frame);
    /* status.engine_rpm   == 250.0  */
    /* status.coolant_temp == 50.0   */
    /* status.engine_on    == ENGINE_ON_ON */

    /* --- PACK --- */
    engine_status_t cmd = {
        .engine_rpm   = engine_rpm_clamp(3500.0),
        .coolant_temp = coolant_temp_clamp(85.0),
        .engine_on    = ENGINE_ON_ON
    };
    uint8_t out[8] = {0};
    engine_status_pack(&cmd, out);
}
```

---

### Full Generated Header Example

The generator should also emit version metadata and a cyclic redundancy check over the DBC to detect stale generated code:

```c
/* At the top of the generated header */
#define VEHICLE_NETWORK_DBC_VERSION   "1.3.0"
#define VEHICLE_NETWORK_DBC_CRC32     (0xA3F21C8BU)

/* Compile-time assertion: fail if generator version mismatch */
_Static_assert(sizeof(uint8_t) == 1, "uint8_t must be 1 byte");
```

---

### A Minimal DBC-to-C Generator in C++

This shows the generator itself — a C++ program that reads a `.dbc` and emits a `.h` / `.c` pair.

```cpp
// dbcgen.cpp — minimal DBC-to-C code generator
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

struct Signal {
    std::string name;
    int         start_bit;
    int         length;
    bool        is_intel;   // true = little-endian
    bool        is_signed;
    double      factor;
    double      offset;
    double      min_val;
    double      max_val;
    std::string unit;
};

struct Message {
    uint32_t           id;
    std::string        name;
    int                dlc;
    std::string        sender;
    std::vector<Signal> signals;
};

// ── Parse one SG_ line ───────────────────────────────────────────────────
std::optional<Signal> parse_signal(const std::string &line)
{
    // SG_ NAME : START|LEN@BYTE_ORDER VALUE_TYPE (FACTOR,OFFSET) [MIN|MAX] "UNIT" RECEIVERS
    static const std::regex re(
        R"(^\s*SG_\s+(\w+)\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*\(([^,]+),([^)]+)\)\s*\[([^|]+)\|([^\]]+)\]\s*"([^"]*)")");

    std::smatch m;
    if (!std::regex_search(line, m, re)) return std::nullopt;

    Signal s;
    s.name       = m[1];
    s.start_bit  = std::stoi(m[2]);
    s.length     = std::stoi(m[3]);
    s.is_intel   = (m[4] == "1");
    s.is_signed  = (m[5] == "-");
    s.factor     = std::stod(m[6]);
    s.offset     = std::stod(m[7]);
    s.min_val    = std::stod(m[8]);
    s.max_val    = std::stod(m[9]);
    s.unit       = m[10];
    return s;
}

// ── Parse one BO_ block ──────────────────────────────────────────────────
std::optional<Message> parse_message_header(const std::string &line)
{
    static const std::regex re(R"(^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+))");
    std::smatch m;
    if (!std::regex_search(line, m, re)) return std::nullopt;
    Message msg;
    msg.id     = static_cast<uint32_t>(std::stoul(m[1]));
    msg.name   = m[2];
    msg.dlc    = std::stoi(m[3]);
    msg.sender = m[4];
    return msg;
}

// ── Lower-case helper ────────────────────────────────────────────────────
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// ── Emit the .h file ─────────────────────────────────────────────────────
void emit_header(const std::vector<Message> &messages, std::ostream &out)
{
    out << "/* AUTO-GENERATED — DO NOT EDIT */\n"
        << "#pragma once\n"
        << "#include <stdint.h>\n"
        << "#include <stdbool.h>\n\n";

    for (const auto &msg : messages) {
        out << "/* " << msg.name << " (0x" << std::hex << msg.id << std::dec
            << ", " << msg.dlc << " bytes) — sender: " << msg.sender << " */\n";
        out << "#define CAN_ID_" << msg.name
            << " (0x" << std::hex << msg.id << std::dec << "U)\n\n";

        out << "typedef struct {\n";
        for (const auto &sig : msg.signals) {
            std::string type = sig.is_signed ? "int64_t" : "uint64_t";
            // Use double when factor != 1 or offset != 0
            bool use_double = (sig.factor != 1.0 || sig.offset != 0.0);
            if (use_double) type = "double";
            out << "    " << type << " " << lower(sig.name)
                << "; /**< " << sig.unit << ", raw*" << sig.factor
                << "+" << sig.offset
                << ", [" << sig.min_val << ".." << sig.max_val << "] */\n";
        }
        std::string sname = lower(msg.name);
        out << "} " << sname << "_t;\n\n";

        out << "void " << sname << "_unpack(" << sname
            << "_t *dst, const uint8_t data[8]);\n";
        out << "void " << sname << "_pack(const " << sname
            << "_t *src, uint8_t data[8]);\n\n";
    }
}

// ── Emit the .c file ─────────────────────────────────────────────────────
void emit_source(const std::vector<Message> &messages,
                 const std::string &header_name, std::ostream &out)
{
    out << "/* AUTO-GENERATED — DO NOT EDIT */\n"
        << "#include \"" << header_name << "\"\n"
        << "#include <string.h>\n#include <math.h>\n\n"
        << "static inline uint64_t extract_le(const uint8_t *d, int s, int n) {\n"
        << "    uint64_t r=0; memcpy(&r,d,8); r>>=s;\n"
        << "    return (n<64)?r&((UINT64_C(1)<<n)-1):r;\n}\n\n"
        << "static inline void insert_le(uint8_t *d,int s,int n,uint64_t v){\n"
        << "    uint64_t m=(n<64)?((UINT64_C(1)<<n)-1):~UINT64_C(0),b=0;\n"
        << "    memcpy(&b,d,8); b&=~(m<<s); b|=(v&m)<<s; memcpy(d,&b,8);\n}\n\n";

    for (const auto &msg : messages) {
        std::string sname = lower(msg.name);

        /* UNPACK */
        out << "void " << sname << "_unpack(" << sname
            << "_t *dst, const uint8_t data[8]) {\n";
        for (const auto &sig : messages[0].signals) {  // iterate this message's signals
            (void)sig; // placeholder
        }
        for (const auto &s : msg.signals) {
            out << "    { uint64_t r=extract_le(data," << s.start_bit
                << "," << s.length << ");\n";
            bool use_double = (s.factor != 1.0 || s.offset != 0.0);
            if (use_double)
                out << "      dst->" << lower(s.name)
                    << "=(double)r*" << s.factor << "+" << s.offset << "; }\n";
            else
                out << "      dst->" << lower(s.name) << "=r; }\n";
        }
        out << "}\n\n";

        /* PACK */
        out << "void " << sname << "_pack(const " << sname
            << "_t *src, uint8_t data[8]) {\n";
        for (const auto &s : msg.signals) {
            bool use_double = (s.factor != 1.0 || s.offset != 0.0);
            out << "    { uint64_t r=";
            if (use_double)
                out << "(uint64_t)llround((src->" << lower(s.name)
                    << "-" << s.offset << ")/" << s.factor << ")";
            else
                out << "(uint64_t)src->" << lower(s.name);
            out << "; insert_le(data," << s.start_bit
                << "," << s.length << ",r); }\n";
        }
        out << "}\n\n";
    }
}

// ── main ─────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "Usage: dbcgen <file.dbc>\n");
        return 1;
    }

    std::ifstream f(argv[1]);
    if (!f) { std::perror(argv[1]); return 1; }

    std::vector<Message> messages;
    std::string line;
    Message *current = nullptr;

    while (std::getline(f, line)) {
        if (auto msg = parse_message_header(line)) {
            messages.push_back(std::move(*msg));
            current = &messages.back();
        } else if (current) {
            if (auto sig = parse_signal(line))
                current->signals.push_back(std::move(*sig));
            else if (line.empty() || line[0] != ' ')
                current = nullptr;
        }
    }

    std::ofstream hdr("can_db.h");
    emit_header(messages, hdr);

    std::ofstream src("can_db.c");
    emit_source(messages, "can_db.h", src);

    std::printf("Generated can_db.h and can_db.c for %zu messages.\n",
                messages.size());
    return 0;
}
```

---

## Rust Code Generation

### Rust Type Definitions and Traits

```rust
// can_db.rs — AUTO-GENERATED — DO NOT EDIT
// Source: vehicle_network.dbc  |  Tool: dbc-codegen  |  Date: 2025-08-01

#![allow(dead_code, non_camel_case_types)]

/// ENGINE_STATUS — CAN ID 0x064, 8 bytes, sender: ECU_ENGINE
#[derive(Debug, Clone, PartialEq)]
pub struct EngineStatus {
    /// Engine speed in RPM.  Physical = raw × 0.25.  Range [0, 16383.75]
    pub engine_rpm: f64,
    /// Coolant temperature in °C.  Physical = raw × 1 − 40.  Range [−40, 215]
    pub coolant_temp: f64,
    /// Engine on/off state.
    pub engine_on: EngineOn,
}

/// Enumeration for ENGINE_ON signal
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum EngineOn {
    Off = 0,
    On  = 1,
}

impl TryFrom<u64> for EngineOn {
    type Error = u64;
    fn try_from(v: u64) -> Result<Self, Self::Error> {
        match v {
            0 => Ok(EngineOn::Off),
            1 => Ok(EngineOn::On),
            x => Err(x),
        }
    }
}

/// THROTTLE_CMD — CAN ID 0x0C8, 4 bytes, sender: ECU_DASH
#[derive(Debug, Clone, PartialEq)]
pub struct ThrottleCmd {
    /// Throttle position in %.  Physical = raw × 0.1.  Range [0, 100]
    pub throttle_pos: f64,
    /// Brake pedal active flag.
    pub brake_active: bool,
}
```

---

### Pack / Unpack in Rust

```rust
// ══════════════════════════════════════════════════════════════════════════
// Bit manipulation helpers
// ══════════════════════════════════════════════════════════════════════════

/// Extract `len` bits starting at `start` from a little-endian 8-byte payload.
#[inline]
fn extract_bits_le(data: &[u8; 8], start: u32, len: u32) -> u64 {
    debug_assert!(len <= 64);
    debug_assert!(start + len <= 64);

    let raw = u64::from_le_bytes(*data);
    let mask = if len < 64 { (1u64 << len) - 1 } else { u64::MAX };
    (raw >> start) & mask
}

/// Insert `len` bits of `value` at `start` into a little-endian payload.
#[inline]
fn insert_bits_le(data: &mut [u8; 8], start: u32, len: u32, value: u64) {
    debug_assert!(len <= 64);
    debug_assert!(start + len <= 64);

    let mask = if len < 64 { (1u64 << len) - 1 } else { u64::MAX };
    let mut raw = u64::from_le_bytes(*data);
    raw &= !(mask << start);
    raw |= (value & mask) << start;
    *data = raw.to_le_bytes();
}

// ══════════════════════════════════════════════════════════════════════════
// A trait for CAN message codec
// ══════════════════════════════════════════════════════════════════════════

/// Every generated message type implements this trait.
pub trait CanMessage: Sized {
    const ID:  u32;
    const DLC: u8;

    fn unpack(data: &[u8; 8]) -> Result<Self, CodecError>;
    fn pack(&self) -> [u8; 8];
}

/// Codec error type
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CodecError {
    /// An enumeration value was out of range.
    InvalidEnumValue { signal: &'static str, raw: u64 },
    /// A signal value was outside the declared physical range.
    OutOfRange { signal: &'static str },
}

// ══════════════════════════════════════════════════════════════════════════
// EngineStatus codec
// ══════════════════════════════════════════════════════════════════════════

impl CanMessage for EngineStatus {
    const ID:  u32 = 0x064;
    const DLC: u8  = 8;

    fn unpack(data: &[u8; 8]) -> Result<Self, CodecError> {
        // ENGINE_RPM : start=0, len=16, intel, unsigned, factor=0.25, offset=0
        let rpm_raw = extract_bits_le(data, 0, 16);
        let engine_rpm = rpm_raw as f64 * 0.25_f64 + 0.0_f64;

        // COOLANT_TEMP : start=16, len=8, intel, unsigned, factor=1, offset=-40
        let temp_raw = extract_bits_le(data, 16, 8);
        let coolant_temp = temp_raw as f64 * 1.0_f64 + (-40.0_f64);

        // ENGINE_ON : start=24, len=1, intel, unsigned, factor=1, offset=0
        let on_raw = extract_bits_le(data, 24, 1);
        let engine_on = EngineOn::try_from(on_raw)
            .map_err(|raw| CodecError::InvalidEnumValue { signal: "ENGINE_ON", raw })?;

        Ok(EngineStatus { engine_rpm, coolant_temp, engine_on })
    }

    fn pack(&self) -> [u8; 8] {
        let mut data = [0u8; 8];

        // ENGINE_RPM
        let rpm_raw = ((self.engine_rpm - 0.0) / 0.25).round() as u64;
        insert_bits_le(&mut data, 0, 16, rpm_raw);

        // COOLANT_TEMP
        let temp_raw = ((self.coolant_temp - (-40.0)) / 1.0).round() as u64;
        insert_bits_le(&mut data, 16, 8, temp_raw);

        // ENGINE_ON
        insert_bits_le(&mut data, 24, 1, self.engine_on as u64);

        data
    }
}

// ══════════════════════════════════════════════════════════════════════════
// ThrottleCmd codec
// ══════════════════════════════════════════════════════════════════════════

impl CanMessage for ThrottleCmd {
    const ID:  u32 = 0x0C8;
    const DLC: u8  = 4;

    fn unpack(data: &[u8; 8]) -> Result<Self, CodecError> {
        let thr_raw  = extract_bits_le(data, 0, 10);
        let brake_raw = extract_bits_le(data, 10, 1);

        Ok(ThrottleCmd {
            throttle_pos: thr_raw as f64 * 0.1,
            brake_active: brake_raw != 0,
        })
    }

    fn pack(&self) -> [u8; 8] {
        let mut data = [0u8; 8];
        insert_bits_le(&mut data, 0, 10, (self.throttle_pos / 0.1).round() as u64);
        insert_bits_le(&mut data, 10, 1, self.brake_active as u64);
        data
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Usage example (round-trip test)
// ══════════════════════════════════════════════════════════════════════════

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn engine_status_round_trip() {
        let original = EngineStatus {
            engine_rpm:   3500.0,
            coolant_temp: 85.0,
            engine_on:    EngineOn::On,
        };

        let packed   = original.pack();
        let unpacked = EngineStatus::unpack(&packed).unwrap();

        // Floating-point round-trip within half an LSB
        assert!((unpacked.engine_rpm   - original.engine_rpm).abs() < 0.125);
        assert!((unpacked.coolant_temp - original.coolant_temp).abs() < 0.5);
        assert_eq!(unpacked.engine_on, original.engine_on);
    }

    #[test]
    fn throttle_cmd_round_trip() {
        let original = ThrottleCmd { throttle_pos: 72.5, brake_active: false };
        let packed   = original.pack();
        let unpacked = ThrottleCmd::unpack(&packed).unwrap();
        assert!((unpacked.throttle_pos - original.throttle_pos).abs() < 0.05);
        assert_eq!(unpacked.brake_active, original.brake_active);
    }
}
```

---

### Build-Script Code Generation with `build.rs`

Rust's `build.rs` runs **before** the main compilation and can generate source files into `OUT_DIR`, which the library then includes with `include!`.

```toml
# Cargo.toml
[package]
name    = "ecu_node"
version = "0.1.0"
edition = "2021"

[build-dependencies]
# A real project would use the `dbc-codegen` crate or a custom parser
regex  = "1"
```

```rust
// build.rs
use std::{
    env,
    fs::{self, File},
    io::{BufRead, BufReader, Write},
    path::Path,
};

fn snake(s: &str) -> String {
    s.chars()
     .enumerate()
     .map(|(i, c)| {
         if c.is_uppercase() && i > 0 { format!("_{}", c.to_lowercase()) }
         else { c.to_lowercase().to_string() }
     })
     .collect()
}

fn pascal(s: &str) -> String {
    s.split('_')
     .map(|w| {
         let mut c = w.chars();
         match c.next() {
             None    => String::new(),
             Some(f) => f.to_uppercase().collect::<String>() + c.as_str(),
         }
     })
     .collect()
}

fn main() {
    let dbc_path = "vehicle_network.dbc";
    println!("cargo:rerun-if-changed={dbc_path}");

    let out_dir  = env::var("OUT_DIR").unwrap();
    let out_path = Path::new(&out_dir).join("can_db.rs");
    let mut out  = File::create(&out_path).unwrap();

    // Very minimal parser — a real tool would use `dbc` or `can-dbc` crate
    let f = BufReader::new(File::open(dbc_path).unwrap());
    writeln!(out, "// AUTO-GENERATED by build.rs from {dbc_path}").unwrap();
    writeln!(out, "#![allow(dead_code)]").unwrap();

    for line in f.lines().map(|l| l.unwrap()) {
        if line.starts_with("BO_ ") {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() >= 4 {
                let id: u32 = parts[1].parse().unwrap_or(0);
                let name     = parts[2].trim_end_matches(':');
                let dlc: u8  = parts[3].parse().unwrap_or(8);
                let sname    = snake(name);
                let pname    = pascal(name);

                writeln!(out,
                    "\n/// Message {name} (ID 0x{id:03X}, {dlc} bytes)"
                ).unwrap();
                writeln!(out,
                    "pub const CAN_ID_{name}: u32 = 0x{id:03X};").unwrap();
                writeln!(out,
                    "pub const CAN_DLC_{name}: u8 = {dlc};").unwrap();
                writeln!(out,
                    "#[derive(Debug, Clone, PartialEq, Default)]").unwrap();
                writeln!(out,
                    "pub struct {pname} {{ /* fields filled in per-signal */ }}"
                ).unwrap();
                writeln!(out,
                    "pub fn {sname}_unpack(_data: &[u8;8]) -> {pname} \
                     {{ {pname}::default() }}"
                ).unwrap();
                writeln!(out,
                    "pub fn {sname}_pack(_msg: &{pname}) -> [u8;8] \
                     {{ [0u8;8] }}"
                ).unwrap();
            }
        }
    }

    println!("cargo:rustc-env=CAN_DB_GENERATED={out_path:?}");
}
```

```rust
// src/lib.rs — consumes the generated file
include!(concat!(env!("OUT_DIR"), "/can_db.rs"));

pub fn dispatch(id: u32, payload: &[u8; 8]) {
    match id {
        CAN_ID_ENGINE_STATUS => {
            let _msg = engine_status_unpack(payload);
            // process ...
        }
        CAN_ID_THROTTLE_CMD => {
            let _msg = throttle_cmd_unpack(payload);
        }
        _ => {}
    }
}
```

---

### A Minimal DBC Parser and Generator in Rust

```rust
// src/bin/dbcgen.rs — standalone code generator binary

use std::{fs, io::{self, BufRead, Write}, path::PathBuf};
use regex::Regex;

#[derive(Debug)]
struct Signal {
    name:      String,
    start_bit: u32,
    length:    u32,
    is_intel:  bool,
    is_signed: bool,
    factor:    f64,
    offset:    f64,
    min_val:   f64,
    max_val:   f64,
    unit:      String,
}

#[derive(Debug)]
struct Message {
    id:      u32,
    name:    String,
    dlc:     u8,
    sender:  String,
    signals: Vec<Signal>,
}

fn parse_dbc(path: &PathBuf) -> io::Result<Vec<Message>> {
    let re_msg = Regex::new(
        r"^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+)").unwrap();
    let re_sig = Regex::new(
        r#"^\s*SG_\s+(\w+)\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*\(([^,]+),([^)]+)\)\s*\[([^|]+)\|([^\]]+)\]\s*"([^"]*)""#
    ).unwrap();

    let file   = fs::File::open(path)?;
    let reader = io::BufReader::new(file);
    let mut messages: Vec<Message> = Vec::new();
    let mut cur: Option<usize>     = None;

    for line in reader.lines() {
        let line = line?;
        if let Some(caps) = re_msg.captures(&line) {
            messages.push(Message {
                id:      caps[1].parse().unwrap(),
                name:    caps[2].to_string(),
                dlc:     caps[3].parse().unwrap(),
                sender:  caps[4].to_string(),
                signals: Vec::new(),
            });
            cur = Some(messages.len() - 1);
        } else if let (Some(idx), Some(caps)) = (cur, re_sig.captures(&line)) {
            messages[idx].signals.push(Signal {
                name:      caps[1].to_string(),
                start_bit: caps[2].parse().unwrap(),
                length:    caps[3].parse().unwrap(),
                is_intel:  &caps[4] == "1",
                is_signed: &caps[5] == "-",
                factor:    caps[6].parse().unwrap(),
                offset:    caps[7].parse().unwrap(),
                min_val:   caps[8].parse().unwrap(),
                max_val:   caps[9].parse().unwrap(),
                unit:      caps[10].to_string(),
            });
        } else if !line.starts_with(' ') && !line.starts_with('\t') {
            cur = None;
        }
    }
    Ok(messages)
}

fn pascal(s: &str) -> String {
    s.split('_').map(|w| {
        let mut c = w.chars();
        c.next().map(|f| f.to_uppercase().collect::<String>() + c.as_str())
          .unwrap_or_default()
    }).collect()
}

fn snake(s: &str) -> String { s.to_lowercase() }

fn emit_rust(messages: &[Message], mut w: impl Write) -> io::Result<()> {
    writeln!(w, "// AUTO-GENERATED — DO NOT EDIT\n#![allow(dead_code)]\n")?;

    // Bit helpers (inlined into the generated file)
    writeln!(w, "{}", r#"
#[inline] fn bits_le(d: &[u8;8], s: u32, n: u32) -> u64 {
    let r = u64::from_le_bytes(*d);
    let m = if n < 64 { (1u64 << n) - 1 } else { u64::MAX };
    (r >> s) & m
}
#[inline] fn put_le(d: &mut [u8;8], s: u32, n: u32, v: u64) {
    let m = if n < 64 { (1u64 << n) - 1 } else { u64::MAX };
    let mut r = u64::from_le_bytes(*d);
    r &= !(m << s); r |= (v & m) << s;
    *d = r.to_le_bytes();
}
"#)?;

    for msg in messages {
        let pname = pascal(&msg.name);
        let sname = snake(&msg.name);

        writeln!(w, "/// {} (ID 0x{:03X}, {} bytes, sender: {})",
                 msg.name, msg.id, msg.dlc, msg.sender)?;
        writeln!(w, "pub const CAN_ID_{}: u32 = 0x{:03X};", msg.name, msg.id)?;
        writeln!(w, "#[derive(Debug, Clone, PartialEq)]\npub struct {pname} {{")?;

        for sig in &msg.signals {
            let ftype = if sig.factor != 1.0 || sig.offset != 0.0 { "f64" }
                        else if sig.is_signed { "i64" } else { "u64" };
            writeln!(w, "    /// {} [{}..{}] {}", sig.name, sig.min_val,
                     sig.max_val, sig.unit)?;
            writeln!(w, "    pub {}: {ftype},", snake(&sig.name))?;
        }
        writeln!(w, "}}\n")?;

        // unpack
        writeln!(w, "pub fn {sname}_unpack(d: &[u8;8]) -> {pname} {{")?;
        for sig in &msg.signals {
            let use_f = sig.factor != 1.0 || sig.offset != 0.0;
            writeln!(w, "    let _{} = bits_le(d, {}, {});",
                     snake(&sig.name), sig.start_bit, sig.length)?;
            if use_f {
                writeln!(w, "    let {} = _{} as f64 * {} + {};",
                         snake(&sig.name), snake(&sig.name),
                         sig.factor, sig.offset)?;
            }
        }
        writeln!(w, "    {pname} {{")?;
        for sig in &msg.signals {
            writeln!(w, "        {0}: {0},", snake(&sig.name))?;
        }
        writeln!(w, "    }}\n}}\n")?;

        // pack
        writeln!(w, "pub fn {sname}_pack(m: &{pname}) -> [u8;8] {{")?;
        writeln!(w, "    let mut d = [0u8;8];")?;
        for sig in &msg.signals {
            let use_f = sig.factor != 1.0 || sig.offset != 0.0;
            let raw = if use_f {
                format!("((m.{} - {}) / {}).round() as u64",
                        snake(&sig.name), sig.offset, sig.factor)
            } else {
                format!("m.{} as u64", snake(&sig.name))
            };
            writeln!(w, "    put_le(&mut d, {}, {}, {raw});",
                     sig.start_bit, sig.length)?;
        }
        writeln!(w, "    d\n}}\n")?;
    }
    Ok(())
}

fn main() -> io::Result<()> {
    let dbc  = PathBuf::from("vehicle_network.dbc");
    let msgs = parse_dbc(&dbc)?;
    let out  = fs::File::create("can_db_generated.rs")?;
    emit_rust(&msgs, out)?;
    eprintln!("Generated can_db_generated.rs ({} messages)", msgs.len());
    Ok(())
}
```

---

## Documentation Generation

In addition to code, generators typically produce human-readable documentation. Here is a Markdown emitter:

```rust
fn emit_markdown(messages: &[Message], mut w: impl Write) -> io::Result<()> {
    writeln!(w, "# CAN Database Reference\n")?;
    writeln!(w, "> Auto-generated from `vehicle_network.dbc`\n")?;

    for msg in messages {
        writeln!(w, "## {} (ID `0x{:03X}`)\n", msg.name, msg.id)?;
        writeln!(w, "- **DLC**: {} bytes", msg.dlc)?;
        writeln!(w, "- **Sender**: {}\n", msg.sender)?;
        writeln!(w, "| Signal | Bits | Byte Order | Type | Factor | Offset | Min | Max | Unit |")?;
        writeln!(w, "|---|---|---|---|---|---|---|---|---|")?;
        for sig in &msg.signals {
            writeln!(w,
                "| `{}` | {}..{} | {} | {} | {} | {} | {} | {} |",
                sig.name,
                sig.start_bit,
                sig.start_bit + sig.length - 1,
                if sig.is_intel { "Intel LE" } else { "Motorola BE" },
                if sig.is_signed { "signed" } else { "unsigned" },
                sig.factor,
                sig.offset,
                sig.min_val,
                sig.max_val,
                sig.unit,
            )?;
        }
        writeln!(w)?;
    }
    Ok(())
}
```

**Example output:**

| Signal | Bits | Byte Order | Type | Factor | Offset | Min | Max | Unit |
|---|---|---|---|---|---|---|---|---|
| `ENGINE_RPM` | 0..15 | Intel LE | unsigned | 0.25 | 0 | 0 | 16383.75 | RPM |
| `COOLANT_TEMP` | 16..23 | Intel LE | unsigned | 1 | -40 | -40 | 215 | degC |
| `ENGINE_ON` | 24..24 | Intel LE | unsigned | 1 | 0 | 0 | 1 | — |

---

## Advanced Topics

### Multiplexed Messages

DBC files support signal multiplexing (a single message carries different signals depending on a mux selector signal):

```
BO_ 300 SENSOR_MUX: 8 ECU_SENSOR
 SG_ MUX_ID    M  : 0|4@1+  (1,0) [0|15]  ""    Vector__XXX
 SG_ TEMP_1    m0 : 4|12@1+ (0.1,0) [0|400] "K" Vector__XXX
 SG_ PRESSURE  m1 : 4|16@1+ (0.01,0) [0|655] "kPa" Vector__XXX
```

Generated C handling:

```c
typedef enum { SENSOR_MUX_TEMP = 0, SENSOR_MUX_PRESSURE = 1 } sensor_mux_id_t;

typedef struct {
    sensor_mux_id_t mux_id;
    union {
        struct { double temp_1; }     m0;
        struct { double pressure; }   m1;
    } data;
} sensor_mux_t;

void sensor_mux_unpack(sensor_mux_t *dst, const uint8_t data[8])
{
    dst->mux_id = (sensor_mux_id_t)extract_bits_le(data, 0, 4);
    switch (dst->mux_id) {
        case SENSOR_MUX_TEMP:
            dst->data.m0.temp_1 = extract_bits_le(data, 4, 12) * 0.1;
            break;
        case SENSOR_MUX_PRESSURE:
            dst->data.m1.pressure = extract_bits_le(data, 4, 16) * 0.01;
            break;
        default: break;
    }
}
```

---

### Endianness Handling

Motorola (big-endian) bit numbering requires different extraction logic:

```c
/**
 * @brief Extract a Motorola (big-endian) signal.
 * @param data      8-byte CAN payload
 * @param msb_pos   Position of MSB in DBC numbering (Motorola start bit)
 * @param length    Bit length
 */
static uint64_t extract_bits_be(const uint8_t *data, int msb_pos, int length)
{
    uint64_t result = 0;
    int bit = msb_pos;

    for (int i = length - 1; i >= 0; --i) {
        int byte_idx = bit / 8;
        int bit_idx  = 7 - (bit % 8);
        if ((data[byte_idx] >> bit_idx) & 1u)
            result |= (UINT64_C(1) << i);

        /* Advance to next bit in Motorola order */
        if (bit % 8 == 0)
            bit += 15;
        else
            --bit;
    }
    return result;
}
```

---

### Extended Frames and FD

```c
/* CAN FD supports up to 64-byte payloads */
#define CANFD_MAX_DLC 64

typedef struct {
    uint32_t id;        /**< 29-bit extended ID if bit 31 set */
    uint8_t  flags;     /**< CAN_FLAG_FD, CAN_FLAG_BRS, ... */
    uint8_t  dlc;
    uint8_t  data[CANFD_MAX_DLC];
} canfd_frame_t;

/* Generated pack function signature for FD message */
void large_sensor_data_pack(const large_sensor_data_t *src,
                             uint8_t data[64]);
```

---

## Integration into Build Systems

### CMake

```cmake
# CMakeLists.txt
find_program(DBCGEN dbcgen REQUIRED)

add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/can_db.h
            ${CMAKE_CURRENT_BINARY_DIR}/can_db.c
    COMMAND ${DBCGEN} ${CMAKE_CURRENT_SOURCE_DIR}/vehicle_network.dbc
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vehicle_network.dbc
    COMMENT "Generating CAN database code from DBC"
)

add_library(can_db
    ${CMAKE_CURRENT_BINARY_DIR}/can_db.c
)
target_include_directories(can_db PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
```

### Cargo / Rust

```toml
# Cargo.toml — the build.rs approach handles this automatically.
# For the standalone binary approach, add a bin target:
[[bin]]
name = "dbcgen"
path = "src/bin/dbcgen.rs"

[dependencies]
regex = "1"
```

### CI Verification (GitHub Actions)

```yaml
# .github/workflows/can_gen.yml
- name: Regenerate CAN DB
  run: |
    dbcgen vehicle_network.dbc
    git diff --exit-code can_db.h can_db.c \
      || (echo "Generated files are stale! Re-run dbcgen." && exit 1)
```

---

## Summary

| Aspect | Key Points |
|---|---|
| **DBC Format** | Text format: `BO_` for messages, `SG_` for signals; factor/offset/range/unit per signal |
| **Signal Extraction** | Bit-shift + mask; Intel (LE) vs. Motorola (BE) use different bit numbering |
| **Physical Conversion** | `physical = raw × factor + offset`; invert for packing |
| **C/C++ Output** | `struct` per message, `extract_bits_le` / `insert_bits_le` helpers; Doxygen comments |
| **Rust Output** | `struct` + `CanMessage` trait; `TryFrom<u64>` for enums; `build.rs` for compile-time generation |
| **Multiplexing** | `M`/`mN` annotations map to C `union` or Rust `enum` variants |
| **Endianness** | Intel start bit = LSB position; Motorola start bit = MSB; require separate algorithms |
| **Documentation** | Emit Markdown / HTML tables directly from the DBC IR |
| **Build Integration** | CMake `add_custom_command`; Rust `build.rs` + `include!`; CI staleness check |
| **Safety** | Range clamping on pack; `Result`/error enum on unpack; compile-time DBC hash check |

Code generation from DBC files eliminates an entire class of manual errors, keeps the implementation perfectly in sync with the network specification, and allows documentation and tests to be regenerated automatically whenever the DBC evolves. For any project with more than a handful of CAN signals, it is the professional standard approach.