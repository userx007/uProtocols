# 56. CAN Kingdom Protocol

> **Plug-and-play CAN protocol with dynamic address allocation and capability exchange for modular systems.**

---

## Table of Contents

1. [Overview](#overview)
2. [Core Concepts](#core-concepts)
3. [Protocol Architecture](#protocol-architecture)
4. [Address Allocation](#address-allocation)
5. [Capability Exchange](#capability-exchange)
6. [Message Structure](#message-structure)
7. [Implementation in C/C++](#implementation-in-cc)
8. [Implementation in Rust](#implementation-in-rust)
9. [Advanced Topics](#advanced-topics)
10. [Summary](#summary)

---

## Overview

**CAN Kingdom** (CK) is an application-layer protocol developed by Kvaser AB (originally by Jannes Jägrevik) that runs on top of the standard CAN (Controller Area Network) bus. Unlike protocols such as CANopen or J1939 — which use fixed node identifiers or predefined object dictionaries — CAN Kingdom is designed around the metaphor of a **kingdom** with a **king** (master) and **citizens** (slave nodes).

Its distinguishing characteristics are:

- **Dynamic address allocation** — nodes do not have fixed CAN IDs burned into firmware; the king assigns them at runtime.
- **Capability exchange** — each citizen announces what it *can* do (its "capabilities"), and the king decides how to use it.
- **Modular plug-and-play** — adding, removing, or replacing a node does not require re-flashing any other node.
- **Deterministic communication** — after the configuration phase, real-time message routing is fixed and predictable.

CAN Kingdom is particularly suited to industrial automation, robotics, medical devices, and any system where hardware modules must be interchangeable without changing software on other nodes.

---

## Core Concepts

### The Kingdom Metaphor

| CAN Kingdom Term | Technical Meaning |
|---|---|
| **King** | The bus master / system controller |
| **Citizen** | A CAN node / slave device |
| **City Number** | The node's assigned CAN base address |
| **Letter** | A CAN message frame |
| **Page** | A data layout within a message |
| **Envelope** | The CAN identifier (COB-ID) that carries a letter |
| **Form** | The signal/data definition within a page |
| **Capital** | The default/fallback CAN ID a citizen uses before address allocation |
| **Mayor** | Optional sub-master for local sub-networks |

### Fundamental Phases

CAN Kingdom operation proceeds through three sequential phases:

1. **Enrollment Phase** — Citizens announce their presence using a well-known default address (the "capital"). The king discovers all nodes on the bus.
2. **Configuration Phase** — The king sends each citizen its assigned city number and defines which envelopes (CAN IDs) to use for which letters (messages).
3. **Operational Phase** — All communication proceeds using the dynamically allocated IDs. The system behaves deterministically.

---

## Protocol Architecture

```
+-----------------------------------------------------+
|                   Application Layer                  |
|           CAN Kingdom (King / Citizen Logic)         |
+-----------------------------------------------------+
|              CAN Kingdom Protocol Layer              |
|   (Enrollment, Configuration, Capability Exchange)   |
+-----------------------------------------------------+
|                   CAN Data Link Layer                |
|              (ISO 11898-1 / CAN 2.0A/B)              |
+-----------------------------------------------------+
|                   CAN Physical Layer                 |
|              (ISO 11898-2 / High-speed CAN)          |
+-----------------------------------------------------+
```

### Address Space

CAN Kingdom uses **11-bit CAN identifiers** (standard frame format, CAN 2.0A).

- City numbers range from `1` to `254` (0 and 255 are reserved).
- Each city number maps to a block of CAN IDs.
- The "capital" address (`0x780` base range) is used for enrollment before a city number is assigned.

### Reserved Identifiers

| CAN ID Range | Purpose |
|---|---|
| `0x780` – `0x7FF` | Enrollment / Capital messages |
| `0x000` | NMT-style emergency (king broadcast) |
| Assigned per city | Operational letter envelopes |

---

## Address Allocation

The address allocation (enrollment) process is the heart of CAN Kingdom's plug-and-play capability.

### Step-by-Step Enrollment

```
Citizen powers on
       |
       v
Citizen transmits "I am here" letter on Capital address
(contains: Serial Number, Module Type, Firmware Version)
       |
       v
King receives enrollment request
       |
       v
King looks up serial number in its database
  +----+----+
  |Known?   |
  YES       NO
  |         |
  |         v
  |    King assigns new city number
  |         |
  +----+----+
       |
       v
King sends "Set City Number" letter to citizen's Capital address
       |
       v
Citizen stores city number (until power loss or reset)
       |
       v
King sends capability configuration letters
       |
       v
Citizen enters operational mode
```

---

## Capability Exchange

Each citizen carries a **module description** — a list of the letters (messages) it can send and receive, and the forms (signal layouts) within each. During the configuration phase:

1. The king **reads** the citizen's capability list (optional — king may already know from a database).
2. The king **assigns envelopes** (CAN IDs) to the citizen's letters.
3. The king **defines pages** (data layout / scaling) for each letter.

This two-way negotiation allows the king to precisely control:
- Which CAN IDs a citizen uses.
- The byte layout of each message.
- Whether a message is transmitted, received, or both.

---

## Message Structure

### Enrollment Message (Citizen -> King)

```
Bits:  [10:3]  City = 0xF0 (capital prefix)
       [2:0]   Sub-address

Byte 0:     Message type = 0x01 (Enrollment Request)
Bytes 1-4:  Serial Number (32-bit, big-endian)
Byte 5:     Module Type
Byte 6:     Firmware Major Version
Byte 7:     Firmware Minor Version
```

### Set City Number Message (King -> Citizen)

```
Byte 0:     Message type = 0x02 (Set City Number)
Byte 1:     Assigned City Number (1-254)
Bytes 2-5:  Serial Number (echo, for confirmation)
Bytes 6-7:  Reserved (0x00)
```

### Set Envelope Message (King -> Citizen)

```
Byte 0:     Message type = 0x03 (Set Envelope)
Byte 1:     Letter Number (which message to configure)
Bytes 2-3:  Assigned CAN ID (11-bit, stored in 16-bit field)
Byte 4:     Direction (0x00 = Tx, 0x01 = Rx, 0x02 = Both)
Bytes 5-7:  Reserved
```

---

## Implementation in C/C++

### Common Definitions and Types

```c
/* can_kingdom.h */
#ifndef CAN_KINGDOM_H
#define CAN_KINGDOM_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---------------------------------------------------------------
 * CAN Kingdom constants
 * --------------------------------------------------------------- */
#define CK_CAPITAL_BASE_ID      0x780U   /* Base CAN ID for enrollment */
#define CK_KING_BROADCAST_ID    0x000U   /* King broadcast / emergency */
#define CK_MAX_CITIZENS         254U
#define CK_MAX_LETTERS          32U      /* Per citizen */
#define CK_MAX_FORMS            8U       /* Per letter / page */
#define CK_SERIAL_LEN           4U       /* Serial number bytes */

/* Message type codes */
#define CK_MSG_ENROLL_REQ       0x01U
#define CK_MSG_SET_CITY         0x02U
#define CK_MSG_SET_ENVELOPE     0x03U
#define CK_MSG_SET_PAGE         0x04U
#define CK_MSG_HEARTBEAT        0x10U
#define CK_MSG_OPERATIONAL      0x20U

/* Envelope direction flags */
#define CK_DIR_TX               0x00U
#define CK_DIR_RX               0x01U
#define CK_DIR_BOTH             0x02U

/* ---------------------------------------------------------------
 * Data structures
 * --------------------------------------------------------------- */

/** Raw CAN frame (hardware-agnostic) */
typedef struct {
    uint32_t id;        /* 11-bit CAN identifier */
    uint8_t  dlc;       /* Data length code (0-8) */
    uint8_t  data[8];
} can_frame_t;

/** Form: one signal within a letter's page */
typedef struct {
    uint8_t  byte_offset;   /* Start byte within the 8-byte payload */
    uint8_t  bit_offset;    /* Start bit within that byte */
    uint8_t  bit_length;    /* Signal width in bits */
    float    scale;
    float    offset;
    char     name[16];
} ck_form_t;

/** Letter: one logical CAN message a citizen can produce or consume */
typedef struct {
    uint8_t   letter_no;            /* Citizen-local letter index */
    uint32_t  assigned_can_id;      /* Assigned by the king */
    uint8_t   direction;            /* CK_DIR_TX / CK_DIR_RX / CK_DIR_BOTH */
    uint8_t   dlc;
    ck_form_t forms[CK_MAX_FORMS];
    uint8_t   form_count;
    bool      active;
} ck_letter_t;

/** Citizen record (maintained by the king) */
typedef struct {
    uint8_t   city_number;
    uint8_t   serial[CK_SERIAL_LEN];
    uint8_t   module_type;
    uint8_t   fw_major;
    uint8_t   fw_minor;
    ck_letter_t letters[CK_MAX_LETTERS];
    uint8_t   letter_count;
    bool      enrolled;
    bool      operational;
} ck_citizen_t;

#endif /* CAN_KINGDOM_H */
```

---

### Citizen Node Implementation (C)

```c
/* citizen.c — CAN Kingdom citizen node */
#include "can_kingdom.h"

/* ---------------------------------------------------------------
 * Citizen state
 * --------------------------------------------------------------- */
static uint8_t  g_city_number  = 0;          /* 0 = not yet assigned */
static bool     g_operational  = false;
static uint8_t  g_serial[CK_SERIAL_LEN] = { 0xDE, 0xAD, 0xBE, 0xEF };
static uint8_t  g_module_type  = 0x05;       /* E.g. "temperature sensor" */
static uint8_t  g_fw_major     = 1;
static uint8_t  g_fw_minor     = 0;

static ck_letter_t g_letters[CK_MAX_LETTERS];
static uint8_t g_letter_count = 0;

/* ---------------------------------------------------------------
 * Hardware abstraction -- implement these for your platform
 * --------------------------------------------------------------- */
extern void hal_can_send(const can_frame_t *frame);
extern bool hal_can_receive(can_frame_t *frame);  /* Non-blocking */
extern void hal_delay_ms(uint32_t ms);

/* ---------------------------------------------------------------
 * Build and transmit enrollment request
 * --------------------------------------------------------------- */
void ck_citizen_send_enrollment(void)
{
    can_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    /* Use capital address for enrollment */
    frame.id      = CK_CAPITAL_BASE_ID | 0x00U;   /* Sub-address 0 */
    frame.dlc     = 8;
    frame.data[0] = CK_MSG_ENROLL_REQ;
    frame.data[1] = g_serial[0];
    frame.data[2] = g_serial[1];
    frame.data[3] = g_serial[2];
    frame.data[4] = g_serial[3];
    frame.data[5] = g_module_type;
    frame.data[6] = g_fw_major;
    frame.data[7] = g_fw_minor;

    hal_can_send(&frame);
}

/* ---------------------------------------------------------------
 * Process a frame received from the king
 * --------------------------------------------------------------- */
static void ck_citizen_process_frame(const can_frame_t *frame)
{
    if (frame->dlc < 1) return;

    switch (frame->data[0]) {

    case CK_MSG_SET_CITY:
        /* King is assigning us a city number */
        if (frame->dlc >= 6) {
            uint8_t assigned = frame->data[1];
            /* Verify serial echo matches ours */
            if (frame->data[2] == g_serial[0] &&
                frame->data[3] == g_serial[1] &&
                frame->data[4] == g_serial[2] &&
                frame->data[5] == g_serial[3])
            {
                g_city_number = assigned;
                /* Acknowledge by re-enrolling on new city address */
                /* (implementation-specific) */
            }
        }
        break;

    case CK_MSG_SET_ENVELOPE:
        /* King is configuring a letter's CAN ID */
        if (frame->dlc >= 5 && g_city_number != 0) {
            uint8_t  letter_no = frame->data[1];
            uint16_t can_id    = (uint16_t)(frame->data[2]) |
                                 ((uint16_t)(frame->data[3]) << 8);
            uint8_t  direction = frame->data[4];

            if (letter_no < g_letter_count) {
                g_letters[letter_no].assigned_can_id = can_id & 0x7FFU;
                g_letters[letter_no].direction       = direction;
                g_letters[letter_no].active          = true;
            }
        }
        break;

    case CK_MSG_OPERATIONAL:
        /* King commands us to enter operational mode */
        g_operational = true;
        break;

    default:
        break;
    }
}

/* ---------------------------------------------------------------
 * Main citizen task -- call from your RTOS task or main loop
 * --------------------------------------------------------------- */
void ck_citizen_task(void)
{
    can_frame_t frame;

    if (g_city_number == 0) {
        /* Not yet enrolled -- broadcast enrollment request every 100 ms */
        ck_citizen_send_enrollment();
        hal_delay_ms(100);
    }

    /* Process incoming frames */
    while (hal_can_receive(&frame)) {
        ck_citizen_process_frame(&frame);
    }

    /* Operational: transmit our data letters */
    if (g_operational) {
        for (uint8_t i = 0; i < g_letter_count; i++) {
            if (g_letters[i].active &&
               (g_letters[i].direction == CK_DIR_TX ||
                g_letters[i].direction == CK_DIR_BOTH))
            {
                /* Populate data -- application-specific */
                can_frame_t tx;
                tx.id  = g_letters[i].assigned_can_id;
                tx.dlc = g_letters[i].dlc;
                /* ... fill tx.data from sensor readings ... */
                hal_can_send(&tx);
            }
        }
    }
}
```

---

### King (Master) Implementation (C++)

```cpp
// king.cpp -- CAN Kingdom master node
#include "can_kingdom.h"
#include <array>
#include <cstring>
#include <cstdio>

class CKKing {
public:
    static constexpr uint8_t MAX_CITIZENS = 254;

    CKKing() : next_city_number_(1) {
        citizen_count_ = 0;
    }

    // -------------------------------------------------------
    // Main receive handler -- call from CAN ISR or polling loop
    // -------------------------------------------------------
    void onFrameReceived(const can_frame_t& frame) {
        // Enrollment messages arrive on capital address range
        if ((frame.id & 0x7F0U) == CK_CAPITAL_BASE_ID) {
            handleEnrollment(frame);
            return;
        }

        // Find citizen by CAN ID range and dispatch
        for (size_t i = 0; i < citizen_count_; ++i) {
            dispatchToCitizen(citizens_[i], frame);
        }
    }

    // -------------------------------------------------------
    // Send the "enter operational" broadcast to all citizens
    // -------------------------------------------------------
    void broadcastOperational() {
        can_frame_t frame{};
        frame.id      = CK_KING_BROADCAST_ID;
        frame.dlc     = 1;
        frame.data[0] = CK_MSG_OPERATIONAL;
        hal_can_send(&frame);
    }

private:
    // -------------------------------------------------------
    // Handle incoming enrollment request
    // -------------------------------------------------------
    void handleEnrollment(const can_frame_t& frame) {
        if (frame.dlc < 8 || frame.data[0] != CK_MSG_ENROLL_REQ) return;

        const uint8_t* serial = &frame.data[1];

        // Check if this citizen is already known
        ck_citizen_t* citizen = findBySerial(serial);

        if (!citizen) {
            // New citizen -- register it
            if (citizen_count_ >= MAX_CITIZENS) return;
            citizen = &citizens_[citizen_count_++];
            memset(citizen, 0, sizeof(*citizen));
            memcpy(citizen->serial, serial, CK_SERIAL_LEN);
            citizen->module_type = frame.data[5];
            citizen->fw_major    = frame.data[6];
            citizen->fw_minor    = frame.data[7];
            citizen->city_number = next_city_number_++;
        }

        citizen->enrolled = true;
        printf("[King] Enrolling citizen SN=%02X%02X%02X%02X -> city=%u\n",
               serial[0], serial[1], serial[2], serial[3],
               citizen->city_number);

        // Send back city number assignment
        sendCityNumber(*citizen, serial);

        // Configure default envelopes (application-specific)
        configureDefaultEnvelopes(*citizen);
    }

    // -------------------------------------------------------
    // Send "Set City Number" letter to a citizen
    // -------------------------------------------------------
    void sendCityNumber(const ck_citizen_t& citizen,
                        const uint8_t* serial)
    {
        can_frame_t frame{};
        frame.id      = CK_CAPITAL_BASE_ID | 0x01U;
        frame.dlc     = 8;
        frame.data[0] = CK_MSG_SET_CITY;
        frame.data[1] = citizen.city_number;
        frame.data[2] = serial[0];
        frame.data[3] = serial[1];
        frame.data[4] = serial[2];
        frame.data[5] = serial[3];
        frame.data[6] = 0x00;
        frame.data[7] = 0x00;
        hal_can_send(&frame);
    }

    // -------------------------------------------------------
    // Assign CAN envelopes (IDs) to a citizen's letters
    // -------------------------------------------------------
    void sendSetEnvelope(const ck_citizen_t& citizen,
                         uint8_t letter_no,
                         uint16_t can_id,
                         uint8_t direction)
    {
        can_frame_t frame{};
        frame.id      = 0x600U | citizen.city_number;
        frame.dlc     = 8;
        frame.data[0] = CK_MSG_SET_ENVELOPE;
        frame.data[1] = letter_no;
        frame.data[2] = static_cast<uint8_t>(can_id & 0xFF);
        frame.data[3] = static_cast<uint8_t>((can_id >> 8) & 0x07);
        frame.data[4] = direction;
        frame.data[5] = 0x00;
        frame.data[6] = 0x00;
        frame.data[7] = 0x00;
        hal_can_send(&frame);
    }

    // -------------------------------------------------------
    // Example: assign default envelopes based on module type
    // -------------------------------------------------------
    void configureDefaultEnvelopes(const ck_citizen_t& citizen) {
        uint16_t base = static_cast<uint16_t>(0x100U + citizen.city_number * 2);
        // Letter 0: status/telemetry (citizen transmits)
        sendSetEnvelope(citizen, 0, base,     CK_DIR_TX);
        // Letter 1: command (citizen receives)
        sendSetEnvelope(citizen, 1, base + 1, CK_DIR_RX);
    }

    // -------------------------------------------------------
    // Dispatch an operational frame to the correct citizen
    // -------------------------------------------------------
    void dispatchToCitizen(ck_citizen_t& citizen,
                            const can_frame_t& frame)
    {
        for (uint8_t i = 0; i < citizen.letter_count; ++i) {
            if (citizen.letters[i].active &&
                citizen.letters[i].assigned_can_id == frame.id)
            {
                // Process received data -- application-specific
                (void)frame;
                break;
            }
        }
    }

    // -------------------------------------------------------
    // Find a citizen by serial number
    // -------------------------------------------------------
    ck_citizen_t* findBySerial(const uint8_t* serial) {
        for (size_t i = 0; i < citizen_count_; ++i) {
            if (memcmp(citizens_[i].serial, serial, CK_SERIAL_LEN) == 0) {
                return &citizens_[i];
            }
        }
        return nullptr;
    }

    void hal_can_send(const can_frame_t* frame);

    std::array<ck_citizen_t, MAX_CITIZENS> citizens_{};
    size_t  citizen_count_;
    uint8_t next_city_number_;
};
```

---

### Signal Encoding/Decoding Helper (C)

```c
/* ck_codec.c -- Encode and decode signals within CAN Kingdom pages */
#include "can_kingdom.h"

/**
 * Extract an unsigned integer signal from a CAN payload.
 *
 * @param data       8-byte CAN payload
 * @param bit_start  Start bit (0 = LSB of byte 0)
 * @param bit_len    Signal width in bits (1-32)
 * @return           Raw unsigned value
 */
uint32_t ck_extract_uint(const uint8_t *data,
                          uint8_t bit_start,
                          uint8_t bit_len)
{
    uint32_t value = 0;
    for (uint8_t i = 0; i < bit_len; ++i) {
        uint8_t abs_bit  = bit_start + i;
        uint8_t byte_idx = abs_bit / 8;
        uint8_t bit_idx  = abs_bit % 8;
        if (data[byte_idx] & (1U << bit_idx)) {
            value |= (1U << i);
        }
    }
    return value;
}

/**
 * Pack an unsigned integer signal into a CAN payload.
 */
void ck_pack_uint(uint8_t *data,
                  uint8_t bit_start,
                  uint8_t bit_len,
                  uint32_t value)
{
    for (uint8_t i = 0; i < bit_len; ++i) {
        uint8_t abs_bit  = bit_start + i;
        uint8_t byte_idx = abs_bit / 8;
        uint8_t bit_idx  = abs_bit % 8;
        if (value & (1U << i)) {
            data[byte_idx] |= (1U << bit_idx);
        } else {
            data[byte_idx] &= ~(1U << bit_idx);
        }
    }
}

/**
 * Apply scale and offset to obtain a physical value (e.g. temperature).
 */
float ck_to_physical(uint32_t raw, float scale, float offset)
{
    return (float)raw * scale + offset;
}

/* Example usage:
 *   uint8_t payload[8] = { 0x64, 0x00, ... };
 *   uint32_t raw  = ck_extract_uint(payload, 0, 16);    // bits 0-15
 *   float    temp = ck_to_physical(raw, 0.1f, -40.0f);  // 0.1 * raw - 40 deg C
 */
```

---

## Implementation in Rust

### Types and Protocol Definitions

```rust
// can_kingdom/src/types.rs

/// A raw CAN frame (hardware-agnostic).
#[derive(Debug, Clone, Default)]
pub struct CanFrame {
    pub id:   u32,       // 11-bit CAN identifier
    pub dlc:  u8,        // Data length code (0-8)
    pub data: [u8; 8],
}

/// Direction of a letter (from the citizen's perspective).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Direction {
    Tx,
    Rx,
    Both,
}

/// One signal definition within a letter's page.
#[derive(Debug, Clone)]
pub struct Form {
    pub byte_offset: u8,
    pub bit_offset:  u8,
    pub bit_length:  u8,
    pub scale:       f32,
    pub offset_val:  f32,
    pub name:        &'static str,
}

/// One logical CAN message a citizen can send or receive.
#[derive(Debug, Clone)]
pub struct Letter {
    pub letter_no:       u8,
    pub assigned_can_id: Option<u32>,
    pub direction:       Direction,
    pub dlc:             u8,
    pub forms:           Vec<Form>,
    pub active:          bool,
}

/// Protocol message type codes.
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MsgType {
    EnrollRequest  = 0x01,
    SetCity        = 0x02,
    SetEnvelope    = 0x03,
    SetPage        = 0x04,
    Heartbeat      = 0x10,
    Operational    = 0x20,
}

impl TryFrom<u8> for MsgType {
    type Error = u8;
    fn try_from(v: u8) -> Result<Self, Self::Error> {
        match v {
            0x01 => Ok(MsgType::EnrollRequest),
            0x02 => Ok(MsgType::SetCity),
            0x03 => Ok(MsgType::SetEnvelope),
            0x04 => Ok(MsgType::SetPage),
            0x10 => Ok(MsgType::Heartbeat),
            0x20 => Ok(MsgType::Operational),
            other => Err(other),
        }
    }
}

pub const CAPITAL_BASE_ID: u32 = 0x780;
pub const KING_BROADCAST_ID: u32 = 0x000;
```

---

### Citizen Node (Rust)

```rust
// can_kingdom/src/citizen.rs

use crate::types::*;

/// State machine for a CAN Kingdom citizen node.
pub struct Citizen {
    pub serial:      [u8; 4],
    pub module_type: u8,
    pub fw_major:    u8,
    pub fw_minor:    u8,
    city_number:     Option<u8>,
    operational:     bool,
    letters:         Vec<Letter>,
}

impl Citizen {
    pub fn new(serial: [u8; 4], module_type: u8, fw: (u8, u8)) -> Self {
        Self {
            serial,
            module_type,
            fw_major: fw.0,
            fw_minor: fw.1,
            city_number: None,
            operational: false,
            letters: Vec::new(),
        }
    }

    /// Add a letter (message capability) this citizen can handle.
    pub fn register_letter(&mut self, letter: Letter) {
        self.letters.push(letter);
    }

    /// Build an enrollment request frame to transmit on the capital address.
    pub fn build_enrollment_frame(&self) -> CanFrame {
        let mut frame = CanFrame::default();
        frame.id      = CAPITAL_BASE_ID;
        frame.dlc     = 8;
        frame.data[0] = MsgType::EnrollRequest as u8;
        frame.data[1] = self.serial[0];
        frame.data[2] = self.serial[1];
        frame.data[3] = self.serial[2];
        frame.data[4] = self.serial[3];
        frame.data[5] = self.module_type;
        frame.data[6] = self.fw_major;
        frame.data[7] = self.fw_minor;
        frame
    }

    /// Process a frame received from the king (or bus).
    pub fn process_frame(&mut self, frame: &CanFrame) -> Option<CanFrame> {
        if frame.dlc == 0 {
            return None;
        }

        let msg_type = MsgType::try_from(frame.data[0]).ok()?;

        match msg_type {
            MsgType::SetCity => self.handle_set_city(frame),
            MsgType::SetEnvelope => self.handle_set_envelope(frame),
            MsgType::Operational => {
                self.operational = true;
                println!("[Citizen] Entering operational mode.");
                None
            }
            _ => None,
        }
    }

    fn handle_set_city(&mut self, frame: &CanFrame) -> Option<CanFrame> {
        if frame.dlc < 6 {
            return None;
        }
        let assigned = frame.data[1];
        let echo     = [frame.data[2], frame.data[3],
                         frame.data[4], frame.data[5]];
        if echo == self.serial {
            self.city_number = Some(assigned);
            println!("[Citizen] Assigned city number: {assigned}");
        }
        None
    }

    fn handle_set_envelope(&mut self, frame: &CanFrame) -> Option<CanFrame> {
        if frame.dlc < 5 || self.city_number.is_none() {
            return None;
        }
        let letter_no = frame.data[1] as usize;
        let can_id    = u32::from(frame.data[2]) |
                        (u32::from(frame.data[3] & 0x07) << 8);
        let direction = match frame.data[4] {
            0x01 => Direction::Rx,
            0x02 => Direction::Both,
            _    => Direction::Tx,
        };

        if let Some(letter) = self.letters.get_mut(letter_no) {
            letter.assigned_can_id = Some(can_id);
            letter.direction       = direction;
            letter.active          = true;
            println!("[Citizen] Letter {letter_no} -> CAN ID 0x{can_id:03X}");
        }
        None
    }

    /// Returns true if the citizen is ready to communicate.
    pub fn is_operational(&self) -> bool {
        self.operational && self.city_number.is_some()
    }

    /// Collect frames to transmit in operational mode.
    pub fn collect_tx_frames<F>(&self, mut fill_data: F) -> Vec<CanFrame>
    where
        F: FnMut(u8, &mut [u8; 8]),
    {
        if !self.is_operational() {
            return Vec::new();
        }
        self.letters.iter()
            .filter(|l| l.active && matches!(l.direction,
                         Direction::Tx | Direction::Both))
            .filter_map(|l| {
                let can_id = l.assigned_can_id?;
                let mut frame = CanFrame {
                    id: can_id, dlc: l.dlc, data: [0u8; 8]
                };
                fill_data(l.letter_no, &mut frame.data);
                Some(frame)
            })
            .collect()
    }
}
```

---

### King (Master) Node (Rust)

```rust
// can_kingdom/src/king.rs

use crate::types::*;
use std::collections::HashMap;

/// Record the king keeps for each enrolled citizen.
#[derive(Debug)]
pub struct CitizenRecord {
    pub city_number:  u8,
    pub serial:       [u8; 4],
    pub module_type:  u8,
    pub fw_major:     u8,
    pub fw_minor:     u8,
    pub enrolled:     bool,
    pub operational:  bool,
    pub letters:      Vec<Letter>,
}

/// The CAN Kingdom king (master) node.
pub struct King {
    citizens:          HashMap<[u8; 4], CitizenRecord>,
    next_city_number:  u8,
}

impl King {
    pub fn new() -> Self {
        Self {
            citizens:         HashMap::new(),
            next_city_number: 1,
        }
    }

    /// Process a received CAN frame and return any frames to transmit.
    pub fn process_frame(&mut self, frame: &CanFrame) -> Vec<CanFrame> {
        let mut responses = Vec::new();

        if (frame.id & 0x7F0) == CAPITAL_BASE_ID {
            if let Some(frames) = self.handle_enrollment(frame) {
                responses.extend(frames);
            }
        }
        responses
    }

    fn handle_enrollment(&mut self, frame: &CanFrame) -> Option<Vec<CanFrame>> {
        if frame.dlc < 8 { return None; }
        if frame.data[0] != MsgType::EnrollRequest as u8 { return None; }

        let serial: [u8; 4] = [frame.data[1], frame.data[2],
                                frame.data[3], frame.data[4]];
        let module_type = frame.data[5];
        let fw_major    = frame.data[6];
        let fw_minor    = frame.data[7];

        let city_number = if let Some(rec) = self.citizens.get(&serial) {
            rec.city_number
        } else {
            let cn = self.next_city_number;
            self.next_city_number += 1;
            self.citizens.insert(serial, CitizenRecord {
                city_number:  cn,
                serial,
                module_type,
                fw_major,
                fw_minor,
                enrolled:    false,
                operational: false,
                letters:     Vec::new(),
            });
            cn
        };

        if let Some(rec) = self.citizens.get_mut(&serial) {
            rec.enrolled = true;
        }

        println!("[King] Enrolled SN={serial:02X?} -> city={city_number}");

        let mut frames = Vec::new();
        frames.push(Self::build_set_city_frame(city_number, &serial));

        let base_id = 0x100u32 + u32::from(city_number) * 2;
        frames.push(Self::build_set_envelope_frame(
            city_number, 0, base_id, Direction::Tx));
        frames.push(Self::build_set_envelope_frame(
            city_number, 1, base_id + 1, Direction::Rx));

        Some(frames)
    }

    /// Broadcast "enter operational" to all citizens.
    pub fn build_operational_broadcast() -> CanFrame {
        CanFrame {
            id:   KING_BROADCAST_ID,
            dlc:  1,
            data: [MsgType::Operational as u8, 0, 0, 0, 0, 0, 0, 0],
        }
    }

    fn build_set_city_frame(city: u8, serial: &[u8; 4]) -> CanFrame {
        CanFrame {
            id:  CAPITAL_BASE_ID | 0x01,
            dlc: 8,
            data: [
                MsgType::SetCity as u8,
                city,
                serial[0], serial[1], serial[2], serial[3],
                0x00, 0x00,
            ],
        }
    }

    fn build_set_envelope_frame(city: u8, letter: u8,
                                 can_id: u32, dir: Direction) -> CanFrame
    {
        let dir_byte = match dir {
            Direction::Tx   => 0x00,
            Direction::Rx   => 0x01,
            Direction::Both => 0x02,
        };
        CanFrame {
            id:  0x600 | u32::from(city),
            dlc: 8,
            data: [
                MsgType::SetEnvelope as u8,
                letter,
                (can_id & 0xFF) as u8,
                ((can_id >> 8) & 0x07) as u8,
                dir_byte,
                0x00, 0x00, 0x00,
            ],
        }
    }

    pub fn citizens(&self) -> &HashMap<[u8; 4], CitizenRecord> {
        &self.citizens
    }
}
```

---

### Signal Codec (Rust)

```rust
// can_kingdom/src/codec.rs

/// Extract an unsigned integer signal from a CAN payload.
pub fn extract_uint(data: &[u8; 8], bit_start: u8, bit_len: u8) -> u32 {
    let mut value: u32 = 0;
    for i in 0..bit_len {
        let abs_bit  = bit_start + i;
        let byte_idx = (abs_bit / 8) as usize;
        let bit_idx  = abs_bit % 8;
        if data[byte_idx] & (1 << bit_idx) != 0 {
            value |= 1 << i;
        }
    }
    value
}

/// Pack an unsigned integer signal into a CAN payload.
pub fn pack_uint(data: &mut [u8; 8], bit_start: u8, bit_len: u8, value: u32) {
    for i in 0..bit_len {
        let abs_bit  = bit_start + i;
        let byte_idx = (abs_bit / 8) as usize;
        let bit_idx  = abs_bit % 8;
        if value & (1 << i) != 0 {
            data[byte_idx] |=  1 << bit_idx;
        } else {
            data[byte_idx] &= !(1 << bit_idx);
        }
    }
}

/// Convert a raw integer value to a physical (scaled) float.
#[inline]
pub fn to_physical(raw: u32, scale: f32, offset: f32) -> f32 {
    raw as f32 * scale + offset
}

/// Convert a physical float back to a raw integer value.
#[inline]
pub fn from_physical(physical: f32, scale: f32, offset: f32) -> u32 {
    ((physical - offset) / scale).round() as u32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_round_trip_uint() {
        let mut payload = [0u8; 8];
        pack_uint(&mut payload, 4, 12, 0xABC);
        let out = extract_uint(&payload, 4, 12);
        assert_eq!(out, 0xABC);
    }

    #[test]
    fn test_physical_conversion() {
        // Temperature: raw 200, scale 0.5, offset -40.0 -> 60 C
        let phys = to_physical(200, 0.5, -40.0);
        assert!((phys - 60.0).abs() < 1e-4);

        let raw = from_physical(60.0, 0.5, -40.0);
        assert_eq!(raw, 200);
    }
}
```

---

### Complete Integration Example (Rust)

```rust
// can_kingdom/src/main.rs -- Simulated king + citizen on the same bus

mod types;
mod citizen;
mod king;
mod codec;

use types::*;
use citizen::Citizen;
use king::King;

fn main() {
    // Create citizen: temperature sensor, SN=DEADBEEF, fw v1.0
    let mut citizen = Citizen::new(
        [0xDE, 0xAD, 0xBE, 0xEF],
        0x05,
        (1, 0),
    );

    citizen.register_letter(Letter {
        letter_no:       0,
        assigned_can_id: None,
        direction:       Direction::Tx,
        dlc:             4,
        forms:           vec![Form {
            byte_offset: 0,
            bit_offset:  0,
            bit_length:  16,
            scale:       0.1,
            offset_val:  -40.0,
            name:        "temperature",
        }],
        active: false,
    });

    let mut king = King::new();

    // --- Phase 1: Enrollment ---
    let enroll_frame = citizen.build_enrollment_frame();
    println!("[Bus] Citizen -> King  : {:?}", enroll_frame);

    let king_responses = king.process_frame(&enroll_frame);
    for frame in &king_responses {
        println!("[Bus] King -> Citizen  : {:?}", frame);
        citizen.process_frame(frame);
    }

    // --- Phase 2: Operational broadcast ---
    let op_frame = King::build_operational_broadcast();
    println!("[Bus] King -> All      : {:?}", op_frame);
    citizen.process_frame(&op_frame);

    // --- Phase 3: Citizen transmits a temperature reading ---
    let tx_frames = citizen.collect_tx_frames(|letter_no, data| {
        if letter_no == 0 {
            // Encode 25.0 C: raw = (25.0 + 40.0) / 0.1 = 650
            let raw = codec::from_physical(25.0, 0.1, -40.0);
            codec::pack_uint(data, 0, 16, raw);
        }
    });

    for frame in &tx_frames {
        let raw  = codec::extract_uint(&frame.data, 0, 16);
        let temp = codec::to_physical(raw, 0.1, -40.0);
        println!(
            "[Bus] Citizen -> Bus   : CAN ID=0x{:03X}  raw={raw}  => {temp:.1} C",
            frame.id
        );
    }

    // Print enrollment summary
    println!("\n[King] Enrolled citizens:");
    for (serial, rec) in king.citizens() {
        println!(
            "  SN={serial:02X?}  city={}  module_type=0x{:02X}  fw={}.{}",
            rec.city_number, rec.module_type, rec.fw_major, rec.fw_minor
        );
    }
}
```

---

## Advanced Topics

### Heartbeat and Node Monitoring

In operational mode, citizens periodically transmit a **heartbeat** frame on a reserved letter so the king can detect node loss:

```c
/* C: Citizen heartbeat transmit */
void ck_citizen_send_heartbeat(uint8_t city_number)
{
    can_frame_t frame;
    frame.id      = 0x700U | city_number;   /* Heartbeat CAN ID convention */
    frame.dlc     = 1;
    frame.data[0] = CK_MSG_HEARTBEAT;
    hal_can_send(&frame);
}
```

### Hot-Plug / Re-enrollment

If a citizen is replaced or power-cycled, it re-sends its enrollment request. The king recognises the serial number and re-issues the same city number and configuration, ensuring zero disruption to the rest of the bus.

### Mayor (Sub-Master)

For large systems, a **mayor** acts as a local king for a sub-network and bridges to the main CAN Kingdom bus. The mayor:
- Has its own city number on the primary bus.
- Acts as king on a secondary CAN bus.
- Translates envelopes between both domains.

### Redundancy and Fail-Safe

CAN Kingdom supports:
- **Duplicate envelopes** — a message can be assigned two CAN IDs; recipients monitor both.
- **King redundancy** — a standby king monitors the active king's heartbeat and takes over if it goes silent.
- **Watchdog integration** — citizens can arm an internal watchdog that triggers a safe state if no king heartbeat is received within a configurable timeout.

---

## Summary

| Aspect | Details |
|---|---|
| **Standard** | Proprietary, defined by Kvaser AB |
| **Physical layer** | Standard CAN (ISO 11898), typically 250 kbit/s or 1 Mbit/s |
| **Frame format** | CAN 2.0A (11-bit identifiers) |
| **Address model** | Dynamic — king assigns city numbers at runtime |
| **Plug-and-play** | Yes — serial-number-based enrollment |
| **Configuration** | King-driven capability and envelope exchange |
| **Operational mode** | Deterministic, fixed CAN IDs post-configuration |
| **Typical use cases** | Industrial automation, robotics, modular embedded systems |

CAN Kingdom addresses one of the fundamental challenges in distributed embedded systems: **how to compose a working system from interchangeable hardware modules without manual configuration**. Its three-phase lifecycle — enroll, configure, operate — cleanly separates plug-and-play flexibility from real-time determinism.

The **C/C++ examples** demonstrate a minimal but complete implementation suitable for bare-metal microcontrollers with a hardware-abstraction layer for CAN I/O. The **Rust implementation** showcases type-safe, ownership-driven design — enums prevent invalid message type values at compile time, `Option<u32>` makes the "not yet assigned" envelope state explicit, and the codec unit tests verify signal round-trips without needing hardware. Both share identical protocol logic and are directly interoperable on the same CAN bus.