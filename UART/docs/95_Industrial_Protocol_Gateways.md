# 95. Industrial Protocol Gateways

- **Introduction** — what gateways are and why they exist
- **UART in Industrial Contexts** — RS-232/422/485 comparison, frame anatomy, why UART persists
- **Major Fieldbus Protocols** — Modbus RTU, CANopen, PROFIBUS DP, EtherNet/IP, HART
- **Gateway Architecture** — hardware block diagram, software layering, data flow
- **Protocol Conversion Concepts** — address mapping, data type/endianness conversion, timing analysis, CRC comparison table
- **C/C++ Examples** — CRC-16, frame builder/parser, RS-485 direction control HAL, a full Modbus→CANopen bridge class, and PROFIBUS SD2 framing
- **Rust Examples** — type-safe CRC and codec, async Tokio-based gateway loop, builder-pattern configuration with a tag database
- **Error Handling** — fault table, retry/last-good-value pattern with a generic C++ template
- **Performance Considerations** — throughput math at 9600 vs 115200 baud, batching and priority-queue strategies
- **Summary** — distilled principles for both C/C++ and Rust

## Converting Between UART and Industrial Fieldbus Protocols

---

## Table of Contents

1. [Introduction](#introduction)
2. [UART in Industrial Contexts](#uart-in-industrial-contexts)
3. [Major Industrial Fieldbus Protocols](#major-industrial-fieldbus-protocols)
4. [Gateway Architecture](#gateway-architecture)
5. [Protocol Conversion Concepts](#protocol-conversion-concepts)
6. [C/C++ Implementation Examples](#cc-implementation-examples)
7. [Rust Implementation Examples](#rust-implementation-examples)
8. [Error Handling and Fault Tolerance](#error-handling-and-fault-tolerance)
9. [Performance Considerations](#performance-considerations)
10. [Summary](#summary)

---

## Introduction

Industrial Protocol Gateways are specialized devices or software components that bridge communication between different industrial automation protocols. In modern industrial environments, heterogeneous systems—from legacy PLCs using RS-232/RS-485 UART-based communication to modern Ethernet-based fieldbuses—must coexist and exchange data reliably.

A **protocol gateway** translates data frames, timing requirements, electrical signaling, and protocol semantics between two or more incompatible communication standards. This document focuses specifically on the role of UART (Universal Asynchronous Receiver-Transmitter) as a foundational transport layer and how gateways convert between UART-based serial communication and industrial fieldbus protocols such as Modbus RTU, CANopen, PROFIBUS, DeviceNet, and EtherNet/IP.

**Key use cases include:**

- Integrating legacy RS-232/RS-485 serial devices into modern PROFINET or EtherNet/IP networks
- Bridging Modbus RTU sensors (UART-based) to a CANopen backbone
- Connecting HART-protocol instruments (layered over 4–20 mA, serial framing) to a PROFIBUS master
- Translating proprietary UART instrument protocols to standard OPC-UA over Ethernet

---

## UART in Industrial Contexts

### Electrical Standards

UART itself is a logical protocol; the physical layer is defined separately:

| Standard | Topology      | Max Distance | Max Speed    | Use Case                       |
|----------|--------------|-------------|-------------|-------------------------------|
| RS-232   | Point-to-point | ~15 m      | ~115 kbps   | HMI panels, legacy instruments |
| RS-422   | Differential  | ~1200 m    | 10 Mbps     | Long-distance, single drop     |
| RS-485   | Multidrop bus | ~1200 m    | 10 Mbps     | Multi-device Modbus networks   |

### UART Frame Structure

A UART frame consists of:

```
[IDLE] [START BIT] [D0][D1][D2][D3][D4][D5][D6][D7] [PARITY] [STOP BIT(s)] [IDLE]
```

Industrial serial protocols typically mandate specific configurations:

- **Modbus RTU**: 8N1 or 8E1, with inter-frame gaps defined by 3.5 character times
- **PROFIBUS DP**: 8E1 at fixed baud rates (9600 to 12 Mbps)
- **HART**: 1200 baud, Bell 202 FSK modulation over UART framing

### Why UART Persists in Industry

Despite being a decades-old technology, UART remains prevalent in industrial settings because:

- Enormous installed base of RS-485 Modbus devices (sensors, drives, meters)
- Simplicity, determinism, and low cost
- Electrically robust RS-485 differential signaling in noisy environments
- Intrinsic safety certification ease for RS-485 in hazardous areas

---

## Major Industrial Fieldbus Protocols

### Modbus RTU (UART-Native)

Modbus RTU is inherently a UART protocol operating over RS-232 or RS-485. It uses a master-slave architecture where only one master can initiate transactions.

**Frame structure:**

```
[Device Address 1B][Function Code 1B][Data N bytes][CRC16 2B]
```

**Function codes relevant to gateways:**

| Code | Operation          |
|------|--------------------|
| 0x01 | Read Coils         |
| 0x03 | Read Holding Registers |
| 0x06 | Write Single Register  |
| 0x10 | Write Multiple Registers |

### CANopen

CANopen runs over the CAN physical layer (ISO 11898), offering multi-master communication, 11-bit message IDs (CAN 2.0A), up to 1 Mbps, and a rich object dictionary model. It is widely used in motion control and embedded automation.

### PROFIBUS DP

PROFIBUS DP (Decentralized Peripherals) uses RS-485 physical wiring but with a deterministic token-passing protocol. It operates at fixed baud rates and supports both cyclic (DP) and acyclic (DPV1) data exchange.

### EtherNet/IP and PROFINET

These are Ethernet-based industrial protocols using standard IEEE 802.3 hardware. They encapsulate CIP (Common Industrial Protocol) or PROFINET frames in UDP/TCP/IP. Gateways that bridge UART devices to these networks must handle both the protocol translation and the Ethernet stack.

### HART Protocol

HART (Highway Addressable Remote Transducer) overlays FSK digital communication on a 4–20 mA analog signal. The digital layer uses UART-like framing at 1200 baud, making it a hybrid analog-digital fieldbus that gateways often need to handle.

---

## Gateway Architecture

### Hardware Architecture

A typical industrial protocol gateway contains:

```
┌─────────────────────────────────────────────────────────────┐
│                    Protocol Gateway                          │
│                                                             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │  UART Side   │    │  Processing  │    │  Fieldbus    │  │
│  │  RS-485/232  │◄──►│     Core     │◄──►│    Side      │  │
│  │  Transceiver │    │  MCU/CPU     │    │  Controller  │  │
│  └──────────────┘    └──────────────┘    └──────────────┘  │
│                            │                                │
│                    ┌───────┴────────┐                       │
│                    │  Config Store  │                       │
│                    │  EEPROM/Flash  │                       │
│                    └────────────────┘                       │
└─────────────────────────────────────────────────────────────┘
```

### Software Layers

```
Application Layer         [Data Mapping / Scaling / Tag Database]
                                        │
Translation Layer         [Protocol Parser ◄──► Protocol Builder]
                          │                                    │
UART Stack               [Modbus RTU / HART / Custom Serial]
Fieldbus Stack           [CANopen / PROFIBUS / EtherNet/IP]
                          │                                    │
HAL                      [UART Driver]           [Fieldbus Driver]
                          │                                    │
Hardware                 [RS-485 PHY]           [CAN/Ethernet PHY]
```

### Data Flow in a Gateway

The typical data flow for a Modbus RTU → CANopen gateway:

1. **Polling**: Gateway polls Modbus RTU slave via RS-485 UART
2. **Response parsing**: Parse RTU response frame, validate CRC
3. **Data mapping**: Map Modbus register values to CANopen Object Dictionary entries
4. **PDO transmission**: Transmit CANopen Process Data Object (PDO) on CAN bus
5. **Reverse path**: Receive CANopen SDO writes, map to Modbus write commands

---

## Protocol Conversion Concepts

### Address Mapping

Each protocol has a different addressing model. Gateways maintain a **mapping table**:

```
Modbus Slave 5, Register 40001 → CANopen Node 12, Object 0x6000, SubIdx 0x01
Modbus Slave 5, Register 40002 → CANopen Node 12, Object 0x6000, SubIdx 0x02
```

### Data Type Conversion

Protocols differ in endianness and data types:

- Modbus uses big-endian 16-bit registers; multi-register values (32-bit floats, 64-bit integers) must be assembled
- CANopen PDOs use little-endian encoding
- PROFIBUS data is application-defined in the GSD file

### Timing and Synchronization

Different protocols have radically different timing requirements:

| Protocol    | Cycle Time    | Determinism |
|-------------|--------------|-------------|
| Modbus RTU  | 10–1000 ms   | Non-deterministic |
| PROFIBUS DP | 1–10 ms      | Deterministic   |
| CANopen     | 0.5–100 ms   | Quasi-deterministic |
| EtherNet/IP | 1–500 ms     | Soft real-time  |

Gateways often need buffering and timestamp reconciliation to bridge these timing domains.

### CRC and Error Detection

| Protocol    | Error Detection   |
|-------------|------------------|
| Modbus RTU  | CRC-16 (polynomial 0x8005) |
| PROFIBUS DP | CRC-8 (Hamming distance 4) |
| CANopen/CAN | CRC-15 (hardware)         |
| EtherNet/IP | Ethernet FCS + TCP checksum |

---

## C/C++ Implementation Examples

### Example 1: Modbus RTU Frame Builder and Parser

```c
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define MODBUS_MAX_FRAME  256
#define MODBUS_CRC_POLY   0xA001   /* reflected 0x8005 */

/* ── CRC-16 for Modbus RTU ────────────────────────────────────────────── */
uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ MODBUS_CRC_POLY;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* ── Build a Modbus RTU Read Holding Registers request ───────────────── */
typedef struct {
    uint8_t  slave_id;
    uint8_t  function_code;
    uint16_t start_address;
    uint16_t quantity;
} ModbusReadRequest;

int modbus_build_read_request(const ModbusReadRequest *req,
                              uint8_t *buf, size_t buf_len)
{
    if (buf_len < 8) return -1;

    buf[0] = req->slave_id;
    buf[1] = req->function_code;          /* e.g. 0x03 */
    buf[2] = (req->start_address >> 8) & 0xFF;
    buf[3] =  req->start_address & 0xFF;
    buf[4] = (req->quantity >> 8) & 0xFF;
    buf[5] =  req->quantity & 0xFF;

    uint16_t crc = modbus_crc16(buf, 6);
    buf[6] = crc & 0xFF;         /* CRC low byte first in Modbus */
    buf[7] = (crc >> 8) & 0xFF;

    return 8;
}

/* ── Parse a Modbus RTU Read Holding Registers response ─────────────── */
typedef struct {
    bool     valid;
    uint8_t  slave_id;
    uint8_t  function_code;
    uint8_t  byte_count;
    uint16_t registers[125];   /* max 125 registers per response */
    uint8_t  register_count;
} ModbusReadResponse;

ModbusReadResponse modbus_parse_read_response(const uint8_t *buf, size_t len)
{
    ModbusReadResponse resp = {0};

    if (len < 5) return resp;   /* minimum valid length */

    /* Verify CRC */
    uint16_t crc_received = (uint16_t)buf[len-1] << 8 | buf[len-2];
    uint16_t crc_calc     = modbus_crc16(buf, (uint16_t)(len - 2));
    if (crc_received != crc_calc) return resp;   /* CRC mismatch */

    resp.slave_id      = buf[0];
    resp.function_code = buf[1];

    /* Check for exception response */
    if (resp.function_code & 0x80) {
        resp.valid = false;
        return resp;
    }

    resp.byte_count      = buf[2];
    resp.register_count  = resp.byte_count / 2;

    for (uint8_t i = 0; i < resp.register_count; i++) {
        resp.registers[i] = (uint16_t)buf[3 + i*2] << 8 | buf[4 + i*2];
    }

    resp.valid = true;
    return resp;
}
```

---

### Example 2: UART Driver Abstraction for Gateway Use

```c
#include <stdint.h>
#include <stddef.h>

/* Platform-agnostic UART HAL for a gateway */
typedef enum {
    UART_PARITY_NONE,
    UART_PARITY_EVEN,
    UART_PARITY_ODD
} UartParity;

typedef struct {
    uint32_t   baud_rate;
    uint8_t    data_bits;      /* 7 or 8 */
    UartParity parity;
    uint8_t    stop_bits;      /* 1 or 2 */
    bool       rs485_mode;     /* enable DE/RE driver control */
    uint16_t   inter_frame_gap_us; /* Modbus: 3.5 char times */
} UartConfig;

typedef struct {
    void   (*init)(const UartConfig *cfg);
    int    (*transmit)(const uint8_t *data, size_t len, uint32_t timeout_ms);
    int    (*receive)(uint8_t *buf, size_t max_len, uint32_t timeout_ms);
    void   (*flush_rx)(void);
    size_t (*rx_available)(void);
} UartDriver;

/* ── Inter-frame gap enforcement for Modbus RTU ──────────────────────── */
/* Modbus requires silence of 3.5 character times between frames.         */
/* At 9600 baud, 8N1: 1 char = 1.042 ms → 3.5 chars = 3.646 ms          */

static uint32_t modbus_inter_frame_gap_us(uint32_t baud_rate)
{
    /* 1 character = (1 + data_bits + parity + stop_bits) / baud_rate    */
    /* For 8N1: 10 bits per character                                     */
    uint32_t char_time_us = (10UL * 1000000UL) / baud_rate;
    uint32_t gap_us       = (35UL * char_time_us) / 10UL; /* 3.5 chars  */
    return (gap_us < 1750) ? 1750 : gap_us; /* min 1.75 ms per spec      */
}

/* ── RS-485 Transceiver Direction Control ────────────────────────────── */
/* RS-485 is half-duplex: DE (Driver Enable) must be asserted during TX   */
/* and deasserted before RX. This is critical for gateway reliability.    */

typedef struct {
    void (*assert_de)(void);    /* Pull DE high before transmit */
    void (*deassert_de)(void);  /* Pull DE low after transmit   */
    void (*delay_us)(uint32_t); /* Short delay for line settle  */
} Rs485Control;

int rs485_transmit(const UartDriver *uart, const Rs485Control *rs485,
                   const uint8_t *data, size_t len)
{
    rs485->assert_de();
    rs485->delay_us(10);   /* Allow driver to enable (line settle) */

    int result = uart->transmit(data, len, 100);

    rs485->delay_us(10);   /* Wait for last byte to clock out      */
    rs485->deassert_de();

    return result;
}
```

---

### Example 3: Modbus RTU to CANopen Data Bridge (C++)

```cpp
#include <cstdint>
#include <map>
#include <functional>
#include <vector>

/* ── CANopen Object Dictionary Entry ─────────────────────────────────── */
struct CanopenODEntry {
    uint16_t index;
    uint8_t  sub_index;
    uint32_t value;        /* 32-bit storage for simplicity              */
    uint8_t  data_type;    /* UNSIGNED16 = 0x06, REAL32 = 0x08, etc.    */
};

/* ── Mapping rule: one Modbus register → one CANopen OD entry ─────── */
struct MappingRule {
    uint8_t  modbus_slave;
    uint16_t modbus_register;
    uint16_t canopen_node;
    uint16_t canopen_index;
    uint8_t  canopen_sub_index;
    float    scale_factor;       /* engineering unit conversion          */
    float    offset;
};

/* ── Protocol Gateway Core (C++) ─────────────────────────────────────── */
class ModbusToCanOpenGateway {
public:
    using CanSendFn  = std::function<void(uint16_t node, uint16_t idx,
                                          uint8_t sub, uint32_t val)>;
    using UartSendFn = std::function<int(const uint8_t*, size_t)>;
    using UartRecvFn = std::function<int(uint8_t*, size_t, uint32_t)>;

    ModbusToCanOpenGateway(CanSendFn can_send,
                           UartSendFn uart_tx,
                           UartRecvFn uart_rx)
        : can_send_(can_send), uart_tx_(uart_tx), uart_rx_(uart_rx) {}

    void add_mapping(const MappingRule &rule) {
        mappings_.push_back(rule);
    }

    /* Called periodically by the gateway scheduler */
    void poll_and_bridge() {
        for (auto &rule : mappings_) {
            uint16_t raw_value = poll_modbus_register(
                rule.modbus_slave, rule.modbus_register);

            /* Apply scaling */
            float eng_value = static_cast<float>(raw_value)
                              * rule.scale_factor + rule.offset;

            /* Re-encode as fixed-point or IEEE 754 depending on type */
            uint32_t can_value = static_cast<uint32_t>(eng_value);

            /* Send via CANopen SDO write */
            can_send_(rule.canopen_node,
                      rule.canopen_index,
                      rule.canopen_sub_index,
                      can_value);
        }
    }

private:
    /* ── Poll one Modbus RTU slave register ─────────────────────────── */
    uint16_t poll_modbus_register(uint8_t slave_id, uint16_t reg_addr) {
        uint8_t request[8];

        /* Build FC03 request */
        request[0] = slave_id;
        request[1] = 0x03;                          /* Read Holding Regs */
        request[2] = (reg_addr >> 8) & 0xFF;
        request[3] =  reg_addr & 0xFF;
        request[4] = 0x00;
        request[5] = 0x01;                          /* Read 1 register   */

        uint16_t crc = crc16(request, 6);
        request[6]   = crc & 0xFF;
        request[7]   = (crc >> 8) & 0xFF;

        uart_tx_(request, 8);

        uint8_t response[7];
        int n = uart_rx_(response, sizeof(response), 50);
        if (n < 7) return 0;

        /* Validate slave ID, function code, and CRC */
        if (response[0] != slave_id || response[1] != 0x03) return 0;
        uint16_t crc_rx = (uint16_t)response[n-1] << 8 | response[n-2];
        if (crc_rx != crc16(response, (uint16_t)(n-2))) return 0;

        return (uint16_t)response[3] << 8 | response[4];
    }

    static uint16_t crc16(const uint8_t *data, uint16_t len) {
        uint16_t crc = 0xFFFF;
        for (uint16_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int b = 0; b < 8; b++)
                crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
        return crc;
    }

    CanSendFn              can_send_;
    UartSendFn             uart_tx_;
    UartRecvFn             uart_rx_;
    std::vector<MappingRule> mappings_;
};
```

---

### Example 4: PROFIBUS DP Frame Encapsulation (C)

```c
/*
 * PROFIBUS DP uses a layered frame structure over RS-485.
 * Three frame types exist: SD1 (no data), SD2 (variable data), SD3 (fixed 8 bytes).
 * This example shows SD2 (variable length) frame construction.
 */

#define PROFIBUS_SD2   0x68   /* Start Delimiter for variable length frame */
#define PROFIBUS_ED    0x16   /* End Delimiter                             */
#define PROFIBUS_FC_DATA_REQ  0x4D  /* FC: Data_Request (DP master → slave) */

typedef struct {
    uint8_t  da;       /* Destination Address */
    uint8_t  sa;       /* Source Address      */
    uint8_t  fc;       /* Function Code       */
    uint8_t *data;
    uint8_t  data_len;
} ProfibusFrame;

/* Build a PROFIBUS SD2 frame */
int profibus_build_sd2_frame(const ProfibusFrame *frame,
                              uint8_t *buf, size_t buf_len)
{
    uint8_t le  = frame->data_len + 3;  /* DA + SA + FC + data bytes */
    size_t  total = (size_t)(le) + 6;   /* 4 header + le + FCS + ED */

    if (buf_len < total) return -1;

    uint8_t *p = buf;
    *p++ = PROFIBUS_SD2;
    *p++ = le;
    *p++ = le;           /* LEr: repeated length for error detection */
    *p++ = PROFIBUS_SD2; /* SD2 repeated after lengths               */
    *p++ = frame->da;
    *p++ = frame->sa;
    *p++ = frame->fc;

    /* Compute FCS (simple modulo-256 sum of DA, SA, FC, data) */
    uint8_t fcs = frame->da + frame->sa + frame->fc;
    for (uint8_t i = 0; i < frame->data_len; i++) {
        *p++ = frame->data[i];
        fcs += frame->data[i];
    }

    *p++ = fcs;
    *p++ = PROFIBUS_ED;

    return (int)total;
}

/* Validate incoming PROFIBUS SD2 frame */
bool profibus_validate_sd2(const uint8_t *buf, size_t len, ProfibusFrame *out)
{
    if (len < 6) return false;
    if (buf[0] != PROFIBUS_SD2)   return false;
    if (buf[1] != buf[2])         return false;  /* LE == LEr check */
    if (buf[3] != PROFIBUS_SD2)   return false;
    if (buf[len-1] != PROFIBUS_ED) return false;

    uint8_t le  = buf[1];
    uint8_t fcs = 0;
    for (uint8_t i = 4; i < 4 + le; i++)
        fcs += buf[i];

    if (fcs != buf[4 + le]) return false;

    out->da       = buf[4];
    out->sa       = buf[5];
    out->fc       = buf[6];
    out->data     = (uint8_t *)(buf + 7);
    out->data_len = le - 3;

    return true;
}
```

---

## Rust Implementation Examples

### Example 5: Modbus RTU CRC and Frame Codec in Rust

```rust
/// Modbus RTU CRC-16 (polynomial 0x8005, reflected)
fn modbus_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

/// Modbus RTU function codes
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum FunctionCode {
    ReadCoils              = 0x01,
    ReadDiscreteInputs     = 0x02,
    ReadHoldingRegisters   = 0x03,
    ReadInputRegisters     = 0x04,
    WriteSingleCoil        = 0x05,
    WriteSingleRegister    = 0x06,
    WriteMultipleRegisters = 0x10,
}

/// A parsed Modbus RTU PDU (without address/CRC)
#[derive(Debug)]
pub enum ModbusPdu {
    ReadHoldingRegistersReq { start_addr: u16, count: u16 },
    ReadHoldingRegistersResp { registers: Vec<u16> },
    Exception { function_code: u8, exception_code: u8 },
}

/// Encode a ReadHoldingRegisters request frame
pub fn encode_read_holding_registers(
    slave_id: u8,
    start_addr: u16,
    count: u16,
) -> [u8; 8] {
    let mut frame = [0u8; 8];
    frame[0] = slave_id;
    frame[1] = FunctionCode::ReadHoldingRegisters as u8;
    frame[2] = (start_addr >> 8) as u8;
    frame[3] =  start_addr as u8;
    frame[4] = (count >> 8) as u8;
    frame[5] =  count as u8;
    let crc = modbus_crc16(&frame[..6]);
    frame[6] = (crc & 0xFF) as u8;
    frame[7] = (crc >> 8) as u8;
    frame
}

/// Decode a Modbus RTU response frame
#[derive(Debug, thiserror::Error)]
pub enum ModbusError {
    #[error("Frame too short: {0} bytes")]
    FrameTooShort(usize),
    #[error("CRC mismatch: expected {expected:#06x}, got {received:#06x}")]
    CrcMismatch { expected: u16, received: u16 },
    #[error("Modbus exception: FC={function_code:#04x}, code={exception_code}")]
    Exception { function_code: u8, exception_code: u8 },
    #[error("Unexpected slave ID: {0}")]
    WrongSlaveId(u8),
}

pub fn decode_read_holding_registers_response(
    buf: &[u8],
    expected_slave_id: u8,
) -> Result<Vec<u16>, ModbusError> {
    if buf.len() < 5 {
        return Err(ModbusError::FrameTooShort(buf.len()));
    }

    // Verify CRC
    let (data, crc_bytes) = buf.split_at(buf.len() - 2);
    let crc_received = u16::from_le_bytes([crc_bytes[0], crc_bytes[1]]);
    let crc_calculated = modbus_crc16(data);
    if crc_received != crc_calculated {
        return Err(ModbusError::CrcMismatch {
            expected: crc_calculated,
            received: crc_received,
        });
    }

    if buf[0] != expected_slave_id {
        return Err(ModbusError::WrongSlaveId(buf[0]));
    }

    // Check for exception response
    if buf[1] & 0x80 != 0 {
        return Err(ModbusError::Exception {
            function_code: buf[1],
            exception_code: buf[2],
        });
    }

    let byte_count = buf[2] as usize;
    let registers: Vec<u16> = buf[3..3 + byte_count]
        .chunks_exact(2)
        .map(|chunk| u16::from_be_bytes([chunk[0], chunk[1]]))
        .collect();

    Ok(registers)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_crc16() {
        // Known-good Modbus RTU CRC: FC03 request for slave 1, addr 0, count 1
        let frame = [0x01, 0x03, 0x00, 0x00, 0x00, 0x01];
        assert_eq!(modbus_crc16(&frame), 0x840A);
    }

    #[test]
    fn test_encode_read_request() {
        let frame = encode_read_holding_registers(1, 0x0000, 1);
        assert_eq!(frame[0], 0x01);
        assert_eq!(frame[1], 0x03);
        // CRC bytes
        assert_eq!(frame[6], 0x0A);
        assert_eq!(frame[7], 0x84);
    }
}
```

---

### Example 6: Async UART Gateway with Tokio in Rust

```rust
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_serial::SerialPortBuilderExt;
use std::time::Duration;

/// Configuration for one UART serial port
#[derive(Debug, Clone)]
pub struct UartPortConfig {
    pub path: String,
    pub baud_rate: u32,
    pub timeout_ms: u64,
}

/// Mapping: one Modbus register to one CANopen SDO
#[derive(Debug, Clone)]
pub struct DataMapping {
    pub modbus_slave: u8,
    pub modbus_register: u16,
    pub canopen_node: u8,
    pub canopen_index: u16,
    pub canopen_sub_index: u8,
    pub scale: f64,
    pub offset: f64,
}

/// Simulated CANopen SDO write (replace with actual CAN driver)
async fn canopen_sdo_write(node: u8, index: u16, sub: u8, value: u32) {
    println!(
        "CANopen SDO Write → Node:{node} [{index:#06x}:{sub:#04x}] = {value}"
    );
}

/// Run one Modbus poll-and-bridge cycle
async fn poll_and_bridge(
    port: &mut tokio_serial::SerialStream,
    mapping: &DataMapping,
    timeout_ms: u64,
) -> Result<(), Box<dyn std::error::Error>> {
    // Build request
    let request = encode_read_holding_registers(
        mapping.modbus_slave,
        mapping.modbus_register,
        1,
    );

    port.write_all(&request).await?;

    // Read response with timeout
    let mut response = vec![0u8; 7];
    tokio::time::timeout(
        Duration::from_millis(timeout_ms),
        port.read_exact(&mut response),
    )
    .await??;

    let registers = decode_read_holding_registers_response(
        &response,
        mapping.modbus_slave,
    )?;

    if let Some(&raw) = registers.first() {
        let eng_value = raw as f64 * mapping.scale + mapping.offset;
        let can_value = eng_value as u32;
        canopen_sdo_write(
            mapping.canopen_node,
            mapping.canopen_index,
            mapping.canopen_sub_index,
            can_value,
        )
        .await;
    }

    Ok(())
}

/// Gateway main loop
pub async fn run_gateway(
    port_config: UartPortConfig,
    mappings: Vec<DataMapping>,
    poll_interval_ms: u64,
) -> Result<(), Box<dyn std::error::Error>> {
    let mut port = tokio_serial::new(&port_config.path, port_config.baud_rate)
        .timeout(Duration::from_millis(port_config.timeout_ms))
        .open_native_async()?;

    let mut interval =
        tokio::time::interval(Duration::from_millis(poll_interval_ms));

    loop {
        interval.tick().await;

        for mapping in &mappings {
            match poll_and_bridge(&mut port, mapping, port_config.timeout_ms).await {
                Ok(()) => {}
                Err(e) => eprintln!(
                    "Gateway error polling slave {}: {e}",
                    mapping.modbus_slave
                ),
            }

            // Modbus inter-frame gap (3.5 char times at 9600 baud ≈ 4 ms)
            tokio::time::sleep(Duration::from_millis(4)).await;
        }
    }
}

// Re-export from previous example for compilation
fn encode_read_holding_registers(slave_id: u8, start_addr: u16, count: u16) -> [u8; 8] {
    let mut frame = [0u8; 8];
    frame[0] = slave_id;
    frame[1] = 0x03;
    frame[2] = (start_addr >> 8) as u8;
    frame[3] =  start_addr as u8;
    frame[4] = (count >> 8) as u8;
    frame[5] =  count as u8;
    let crc = modbus_crc16(&frame[..6]);
    frame[6] = (crc & 0xFF) as u8;
    frame[7] = (crc >> 8) as u8;
    frame
}

fn modbus_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            crc = if crc & 1 != 0 { (crc >> 1) ^ 0xA001 } else { crc >> 1 };
        }
    }
    crc
}

fn decode_read_holding_registers_response(
    buf: &[u8],
    expected_slave_id: u8,
) -> Result<Vec<u16>, String> {
    if buf.len() < 5 { return Err("Too short".into()); }
    if buf[0] != expected_slave_id { return Err("Wrong slave ID".into()); }
    let byte_count = buf[2] as usize;
    Ok(buf[3..3+byte_count]
        .chunks_exact(2)
        .map(|c| u16::from_be_bytes([c[0], c[1]]))
        .collect())
}
```

---

### Example 7: Type-Safe Protocol Mapping in Rust

```rust
use std::collections::HashMap;

/// Supported fieldbus protocols on the "north side" of the gateway
#[derive(Debug, Clone, Hash, PartialEq, Eq)]
pub enum FieldbusProtocol {
    CanopenSdo { node: u8, index: u16, sub_index: u8 },
    ProfibusInput { slave_addr: u8, byte_offset: u8 },
    EtherNetIpTag { instance: u16, attribute: u8 },
}

/// Supported serial sources on the "south side" of the gateway
#[derive(Debug, Clone, Hash, PartialEq, Eq)]
pub enum SerialSource {
    ModbusRtuCoil { slave: u8, address: u16 },
    ModbusRtuRegister { slave: u8, address: u16 },
    HartVariable { slave: u8, variable_code: u8 },
}

/// A validated, bi-directional mapping entry
pub struct GatewayMapping {
    pub source: SerialSource,
    pub destination: FieldbusProtocol,
    pub scale: f64,
    pub offset: f64,
    pub description: String,
}

/// Tag database: runtime value store
pub struct TagDatabase {
    values: HashMap<SerialSource, f64>,
}

impl TagDatabase {
    pub fn new() -> Self {
        Self { values: HashMap::new() }
    }

    pub fn update(&mut self, source: SerialSource, raw: u32, mapping: &GatewayMapping) {
        let eng = raw as f64 * mapping.scale + mapping.offset;
        self.values.insert(source, eng);
    }

    pub fn get(&self, source: &SerialSource) -> Option<f64> {
        self.values.get(source).copied()
    }
}

/// Gateway configuration builder (builder pattern)
pub struct GatewayConfigBuilder {
    mappings: Vec<GatewayMapping>,
}

impl GatewayConfigBuilder {
    pub fn new() -> Self {
        Self { mappings: Vec::new() }
    }

    pub fn add_mapping(
        mut self,
        source: SerialSource,
        destination: FieldbusProtocol,
        scale: f64,
        offset: f64,
        description: impl Into<String>,
    ) -> Self {
        self.mappings.push(GatewayMapping {
            source, destination, scale, offset,
            description: description.into(),
        });
        self
    }

    pub fn build(self) -> Vec<GatewayMapping> {
        self.mappings
    }
}

/// Usage example
fn configure_gateway() -> Vec<GatewayMapping> {
    GatewayConfigBuilder::new()
        .add_mapping(
            SerialSource::ModbusRtuRegister { slave: 5, address: 40001 },
            FieldbusProtocol::CanopenSdo { node: 12, index: 0x6000, sub_index: 0x01 },
            0.1,    // Scale: raw 0–32767 → 0.0–3276.7°C
            -273.15, // Offset: Kelvin to Celsius
            "Temperature sensor PT100 → CANopen process value",
        )
        .add_mapping(
            SerialSource::ModbusRtuRegister { slave: 5, address: 40002 },
            FieldbusProtocol::ProfibusInput { slave_addr: 3, byte_offset: 0 },
            0.01,
            0.0,
            "Pressure transmitter → PROFIBUS input byte",
        )
        .build()
}
```

---

## Error Handling and Fault Tolerance

### Common Fault Scenarios

Industrial protocol gateways must handle a variety of fault conditions gracefully:

| Fault                       | Detection Method                    | Recovery Strategy               |
|-----------------------------|-------------------------------------|---------------------------------|
| Modbus slave timeout        | No response within T_timeout        | Retry N times, then mark offline |
| CRC error in RTU frame      | CRC mismatch on received bytes      | Discard frame, log, retry        |
| RS-485 bus collision        | Framing error / noise               | Backoff, retransmit              |
| Fieldbus controller fault   | Fieldbus diagnostic flags           | Reinitialize controller         |
| Gateway overload            | Poll cycle overrun                  | Drop lowest-priority mappings   |
| Slave exception response    | FC | 0x80 in Modbus response        | Log code, substitute last-good value |

### Retry and Watchdog Pattern (C++)

```cpp
#include <cstdint>
#include <functional>
#include <optional>

template<typename T>
class FaultTolerantPoller {
public:
    using PollFn = std::function<std::optional<T>()>;

    FaultTolerantPoller(PollFn poll_fn,
                        uint8_t max_retries = 3,
                        uint32_t retry_delay_ms = 10)
        : poll_fn_(poll_fn),
          max_retries_(max_retries),
          retry_delay_ms_(retry_delay_ms),
          consecutive_failures_(0),
          online_(true) {}

    std::optional<T> poll() {
        for (uint8_t attempt = 0; attempt < max_retries_; attempt++) {
            auto result = poll_fn_();
            if (result.has_value()) {
                consecutive_failures_ = 0;
                online_ = true;
                last_good_value_ = result;
                return result;
            }
            // brief delay between retries (platform-specific)
        }

        consecutive_failures_++;
        if (consecutive_failures_ > max_retries_) {
            online_ = false;
        }

        // Return last known good value to prevent downstream NaN/zero
        return last_good_value_;
    }

    bool is_online() const { return online_; }
    uint32_t consecutive_failures() const { return consecutive_failures_; }

private:
    PollFn            poll_fn_;
    uint8_t           max_retries_;
    uint32_t          retry_delay_ms_;
    uint32_t          consecutive_failures_;
    bool              online_;
    std::optional<T>  last_good_value_;
};
```

---

## Performance Considerations

### Polling Throughput Analysis

For a Modbus RTU gateway polling N slaves at 9600 baud:

- Single register read: 8 bytes TX + 7 bytes RX = 15 bytes
- At 9600 baud (8N1): ~1.56 ms per byte → ~23 ms per transaction
- Plus inter-frame gap (3.5 chars ≈ 3.6 ms) and processing overhead

**Maximum theoretical poll rate per slave**: approximately 38 polls/second at 9600 baud. At 115200 baud this improves to roughly 440 polls/second.

For 32 Modbus slaves, a gateway at 9600 baud can realistically achieve only ~1 poll/second per slave—which is often acceptable for slowly changing process values (temperature, pressure) but inadequate for fast control loops.

### Optimization Strategies

- **Batch reads**: Use FC03 to read multiple registers in one request (up to 125 registers per transaction), reducing round-trip overhead dramatically
- **Priority queues**: Assign polling priorities; critical values poll every cycle, slow values every N cycles
- **Higher baud rates**: Upgrade to 115200 baud where device firmware allows
- **Parallel ports**: Use multiple RS-485 ports to split the device population across bus segments
- **Asynchronous I/O**: Use async/await (as shown in the Rust Tokio example) to overlap serial I/O with processing

---

## Summary

Industrial Protocol Gateways are essential integration components in factory automation, process control, and building management systems. They solve the fundamental interoperability challenge between the vast installed base of UART-based serial field devices (primarily Modbus RTU over RS-485) and modern fieldbus systems (CANopen, PROFIBUS DP, EtherNet/IP, PROFINET).

**Key architectural principles covered:**

- UART provides the physical transport (RS-232/422/485) for most legacy fieldbus protocols; understanding its electrical characteristics, timing constraints, and error modes is foundational
- The gateway consists of distinct layers: HAL drivers, protocol codecs, a translation/mapping engine, and a tag database
- Address mapping, data type conversion, scaling, and timing reconciliation are the four core translation tasks every gateway must perform
- Reliable RS-485 direction control (DE/RE assertion) is critical for half-duplex operation and is a common source of hard-to-diagnose bugs
- Fault tolerance—retries, last-good-value substitution, and device-offline detection—is not optional in industrial environments; processes must continue safely when a single sensor fails

**Language-specific takeaways:**

- **C/C++**: Preferred for bare-metal and RTOS-based gateway firmware; direct register-level UART control, zero-overhead CRC computation, and deterministic memory layouts make C the dominant language for embedded gateway MCUs
- **Rust**: Increasingly adopted for safety-critical gateway software, especially on Linux-based gateways; the type system eliminates an entire class of frame-parsing bugs, `tokio` enables efficient async I/O, and the borrow checker prevents buffer ownership errors common in C gateway code

Industrial protocol gateways are a microcosm of embedded systems engineering: they demand precision in protocol implementation, robustness in fault handling, and careful resource management—all while bridging the digital divide between yesterday's and tomorrow's industrial infrastructure.

---

*Document: 95 – Industrial Protocol Gateways | UART Series*