# 84. CAN in Electric Vehicle Architecture

**Architecture & Design**
- Multi-bus EV topology diagram (Powertrain CAN → Gateway → Chassis CAN)
- ECU role table with CAN ID assignments and priority arbitration strategy

**C/C++ Code Examples**
- `BMS_EncodeStatus` / `BMS_DecodeStatus` — bitfield encoding for SOC, pack voltage, current, flags
- `VCU_BuildTorqueRequest` — alive counter + XOR checksum pattern (ASIL-compliant)
- `MCU_ValidateTorqueRequest` — watchdog with 3-miss fault reaction
- CAN FD 64-byte cell voltage frame (C++)
- `BmsController` state machine with fault detection and event-driven fault CAN message

**Rust Code Examples**
- `BmsStatus` struct with `encode()` / `decode()` using `Result<_, BmsDecodeError>`
- `TorqueRequestEncoder` with wrapping alive counter and `TorqueWatchdog` validator
- Async `ev_monitor_task` with `tokio::select!` and BMS timeout watchdog
- CHAdeMO DC fast charge negotiation (`ChademoVehicleMsg` / `ChademoEvseMsg`)

**Diagnostics & Safety**
- ISO-TP frame types for UDS over CAN
- UDS service table with EV-specific DIDs and routines
- Contactor weld detection routine (safety-critical, ISO 26262 context)


> **Topic:** High-voltage battery management, motor control, and charging communication over CAN.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [EV CAN Network Topology](#2-ev-can-network-topology)
3. [High-Voltage Battery Management System (BMS)](#3-high-voltage-battery-management-system-bms)
4. [Motor Control Unit (MCU) Communication](#4-motor-control-unit-mcu-communication)
5. [Charging Communication (AC & DC)](#5-charging-communication-ac--dc)
6. [CAN Message Structures and Arbitration in EV Context](#6-can-message-structures-and-arbitration-in-ev-context)
7. [C/C++ Implementation Examples](#7-cc-implementation-examples)
8. [Rust Implementation Examples](#8-rust-implementation-examples)
9. [Safety, Fault Handling & Diagnostics (UDS/OBD-II)](#9-safety-fault-handling--diagnostics-udsobd-ii)
10. [Real-World Protocol References](#10-real-world-protocol-references)
11. [Summary](#11-summary)

---

## 1. Introduction

Modern Battery Electric Vehicles (BEVs) and Plug-in Hybrid Electric Vehicles (PHEVs) rely on a
carefully orchestrated network of Electronic Control Units (ECUs) that must communicate in
real time with tight latency and reliability constraints. The Controller Area Network (CAN) bus —
originally developed by Bosch in the 1980s — remains the backbone of this architecture, even as
newer protocols such as CAN FD, Ethernet, and LIN play supplementary roles.

### Why CAN in EVs?

| Property | Relevance to EVs |
|---|---|
| Deterministic latency | Critical for BMS cell balancing and torque commands |
| Differential signaling | High noise immunity near high-voltage inverters |
| Multi-master bus | All ECUs can broadcast without a central coordinator |
| Error detection & fault confinement | Safety-critical systems require robust error handling |
| Low cost, mature tooling | Widely supported in automotive silicon |

### CAN Variants Used

- **Classical CAN (ISO 11898-1)** — 1 Mbit/s maximum, most ECUs
- **CAN FD (ISO 11898-1:2015)** — Up to 8 Mbit/s data phase, used for high-throughput BMS telemetry
- **CAN XL** — Emerging, up to 20 Mbit/s, under evaluation for next-gen platforms

---

## 2. EV CAN Network Topology

A typical EV has multiple CAN segments (buses), separated by gateways, to isolate
safety-critical domains from comfort/infotainment systems.

```
  ┌────────────────────────────────────────────────────────────────┐
  │                    POWERTRAIN CAN (500 kbit/s – 1 Mbit/s)      │
  │                                                                │
  │  ┌───────┐   ┌───────┐   ┌───────┐   ┌──────────┐  ┌───────┐ │
  │  │  BMS  │   │  MCU  │   │  OBC  │   │ DC-DC    │  │ DCFC  │ │
  │  │(HV)   │   │(Motor)│   │(AC Ch)│   │Converter │  │(Chadm)│ │
  │  └───┬───┘   └───┬───┘   └───┬───┘   └────┬─────┘  └───┬───┘ │
  └──────┼───────────┼───────────┼────────────┼────────────┼──────┘
         │           │           │            │            │
  ═══════╪═══════════╪═══════════╪════════════╪════════════╪═══ CAN H/L
         │           │           │            │            │
  ┌──────┴───────────┴───────────┴────────────┴────────────┴──────┐
  │                       GATEWAY / VCU                            │
  └──────────────────────────┬─────────────────────────────────────┘
                             │
  ┌──────────────────────────▼─────────────────────────────────────┐
  │              CHASSIS / BODY CAN (250 kbit/s)                   │
  │   ABS/ESC │ TPMS │ Steering │ Climate │ Instrument Cluster     │
  └────────────────────────────────────────────────────────────────┘
```

### Key ECUs and Their CAN Roles

| ECU | Primary CAN Role | Typical CAN ID Range |
|---|---|---|
| BMS (Battery Management System) | Cell voltages, SOC, SOH, temperature, fault flags | 0x300–0x3FF |
| MCU (Motor Control Unit) | Torque request/response, speed, inverter status | 0x200–0x2FF |
| VCU (Vehicle Control Unit) | Supervisory commands, mode selection | 0x100–0x1FF |
| OBC (On-Board Charger) | AC charging state, power limits | 0x600–0x6FF |
| DCFC (DC Fast Charge controller) | CHAdeMO/CCS power negotiation | 0x700–0x7FF |
| DC-DC Converter | 12 V bus voltage, current | 0x500–0x5FF |

---

## 3. High-Voltage Battery Management System (BMS)

### 3.1 Architecture Overview

The BMS monitors and protects the high-voltage (HV) battery pack (typically 300–800 V, 40–150 kWh).
It communicates critical state information over CAN and must respond to faults in milliseconds.

**Core BMS CAN messages:**

| Message Name | CAN ID | DLC | Cycle Time | Content |
|---|---|---|---|---|
| BMS_Status | 0x300 | 8 | 10 ms | SOC, SOH, pack voltage, current |
| BMS_CellVoltages_1 | 0x301 | 8 | 100 ms | Cells 1–4 voltages (16-bit each) |
| BMS_CellVoltages_2 | 0x302 | 8 | 100 ms | Cells 5–8 voltages |
| BMS_Temperature | 0x303 | 8 | 100 ms | Min/max/avg temperatures, sensor count |
| BMS_Limits | 0x304 | 8 | 20 ms | Max charge/discharge current, power limits |
| BMS_Fault | 0x305 | 8 | On event | Fault codes, DTC, contactor state |
| BMS_SOC_Detail | 0x306 | 8 | 1000 ms | Full SOC%, capacity Ah, energy Wh |

### 3.2 CAN Signal Encoding for BMS

```
BMS_Status (0x300, 8 bytes, 10 ms cyclic):

Byte:  7       6       5       4       3       2       1       0
      ┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
      │ Flags │ Flags │ Curr H│ Curr L│ Volt H│ Volt L│ SOH % │ SOC % │
      └───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘

  SOC%    : Byte 0,     range 0–100,   resolution 0.5%, offset 0
  SOH%    : Byte 1,     range 0–100,   resolution 1%,   offset 0
  Pack Voltage: Bytes 2-3 (uint16 BE), range 0–1000 V, resolution 0.1 V
  Pack Current: Bytes 4-5 (int16 BE),  range ±2000 A,  resolution 0.1 A
  Flags   : Bytes 6-7  (bit-fields, see below)

Flags Byte 6:
  Bit 7: Fault active
  Bit 6: Contactor closed
  Bit 5: Charging active
  Bit 4: Balancing active
  Bits 3-0: Reserved
```

---

## 4. Motor Control Unit (MCU) Communication

### 4.1 Torque Request / Response Pattern

The VCU sends a torque request to the MCU every 10 ms. The MCU responds with actual torque,
speed, and inverter status. This is the core of the drive-by-wire system.

```
VCU → MCU: MCU_TorqueRequest (0x200, 8 bytes, 10 ms)

Byte 0-1: Requested torque    (int16, 0.1 Nm/bit, ±3000 Nm range)
Byte 2-3: Max motor speed     (uint16, 1 RPM/bit)
Byte 4  : Drive mode          (0=Off, 1=Drive, 2=Regen, 3=Neutral)
Byte 5  : Regen level         (0–15, regen braking intensity)
Byte 6  : Alive counter       (rolling 0–255, watchdog)
Byte 7  : Checksum            (XOR of bytes 0–6)

MCU → VCU: MCU_Status (0x201, 8 bytes, 10 ms)

Byte 0-1: Actual torque       (int16, 0.1 Nm/bit)
Byte 2-3: Motor speed (RPM)   (int16)
Byte 4-5: DC link voltage     (uint16, 0.1 V/bit)
Byte 6  : Inverter temp (°C)  (uint8, -40°C offset)
Byte 7  : Status flags        (ready, fault, HV_active, etc.)
```

### 4.2 Watchdog / Alive Counter Pattern

EV safety architectures mandate that safety-critical CAN messages include an alive counter
(rolling counter) and a checksum. Missing three consecutive messages or a counter mismatch
triggers a safety reaction (torque reduction or shutdown).

---

## 5. Charging Communication (AC & DC)

### 5.1 AC Charging (On-Board Charger)

The OBC negotiates power with the EVSE (Electric Vehicle Supply Equipment) via IEC 61851 /
PWM pilot signal for basic AC charging, and optionally via ISO 15118 over PLC. The OBC reports
its state over CAN.

```
OBC_Status (0x600, 8 bytes, 100 ms):

Byte 0  : Charge state  (0=Idle, 1=PreCharge, 2=Charging, 3=Full, 4=Fault)
Byte 1  : AC voltage    (uint8, 2 V/bit, 0–510 V)
Byte 2  : AC current    (uint8, 0.5 A/bit, 0–127.5 A)
Byte 3-4: Charged energy Wh (uint16, 10 Wh/bit)
Byte 5  : Charge power  (uint8, 100 W/bit)
Byte 6  : Error flags
Byte 7  : Pilot duty %  (CP signal duty cycle × 2)
```

### 5.2 DC Fast Charging (CHAdeMO / CCS Combo)

DC fast charging protocols require a bidirectional power negotiation between the vehicle
(EV) and the EVSE. CHAdeMO uses CAN directly; CCS uses PLC (ISO 15118) but the internal
vehicle side still communicates the charger state over CAN.

**CHAdeMO CAN Messages (vehicle side):**

```
From EV to EVSE:
  0x100: Max battery voltage, rated capacity, current SOC, charge current request
  0x101: Fault flags, charge enable, weld detection request

From EVSE to EV:
  0x108: Output voltage, output current
  0x109: EVSE status, fault flags, remaining time
```

---

## 6. CAN Message Structures and Arbitration in EV Context

### 6.1 Priority Assignment

Lower CAN IDs win arbitration. EV systems assign priorities as follows:

| Priority | CAN ID Range | Message Type |
|---|---|---|
| Highest | 0x001–0x0FF | Emergency shutdown, contactor open commands |
| High | 0x100–0x1FF | VCU supervisory, torque requests |
| Medium-High | 0x200–0x3FF | MCU status, BMS status |
| Medium | 0x400–0x5FF | Thermal management, DC-DC |
| Low | 0x600–0x6FF | Charging, non-time-critical |
| Lowest | 0x700–0x7FF | Diagnostics, logging |

### 6.2 CAN FD for High-Resolution BMS Data

Classical CAN limits payloads to 8 bytes. CAN FD extends this to 64 bytes, enabling the
BMS to send all cell voltages of a 96-cell pack in a single message.

```
BMS_AllCellVoltages (CAN FD, 0x310, 64 bytes, 500 ms):

Bytes 0–1:   Cell 1 voltage (uint16 BE, 0.1 mV/bit)
Bytes 2–3:   Cell 2 voltage
...
Bytes 62–63: Cell 32 voltage (or padding)
```

---

## 7. C/C++ Implementation Examples

### 7.1 BMS Status Message Encoding and Decoding (C)

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── BMS Status message (CAN ID 0x300, DLC=8) ─────────────────────── */

#define CAN_ID_BMS_STATUS       0x300U
#define CAN_ID_BMS_FAULT        0x305U
#define CAN_ID_MCU_TORQUE_REQ   0x200U
#define CAN_ID_MCU_STATUS       0x201U

/* BMS Status decoded structure */
typedef struct {
    float    soc_pct;          /* 0.0–100.0 % */
    float    soh_pct;          /* 0.0–100.0 % */
    float    pack_voltage_v;   /* Volts        */
    float    pack_current_a;   /* Amperes (+ = discharge, - = charge) */
    bool     fault_active;
    bool     contactor_closed;
    bool     charging_active;
    bool     balancing_active;
} BmsStatus_t;

/* Encode BmsStatus_t → 8-byte CAN payload */
void BMS_EncodeStatus(const BmsStatus_t *s, uint8_t data[8])
{
    /* Byte 0: SOC, resolution 0.5%, range 0–100% → raw 0–200 */
    data[0] = (uint8_t)(s->soc_pct / 0.5f);

    /* Byte 1: SOH, resolution 1% */
    data[1] = (uint8_t)(s->soh_pct);

    /* Bytes 2-3: Pack voltage, 0.1 V/bit (big-endian) */
    uint16_t raw_volt = (uint16_t)(s->pack_voltage_v / 0.1f);
    data[2] = (uint8_t)(raw_volt >> 8);
    data[3] = (uint8_t)(raw_volt & 0xFF);

    /* Bytes 4-5: Pack current, 0.1 A/bit, signed (big-endian) */
    int16_t raw_curr = (int16_t)(s->pack_current_a / 0.1f);
    data[4] = (uint8_t)((uint16_t)raw_curr >> 8);
    data[5] = (uint8_t)((uint16_t)raw_curr & 0xFF);

    /* Byte 6: Flags */
    data[6] = 0;
    if (s->fault_active)     data[6] |= (1u << 7);
    if (s->contactor_closed) data[6] |= (1u << 6);
    if (s->charging_active)  data[6] |= (1u << 5);
    if (s->balancing_active) data[6] |= (1u << 4);

    /* Byte 7: Reserved */
    data[7] = 0;
}

/* Decode 8-byte CAN payload → BmsStatus_t */
void BMS_DecodeStatus(const uint8_t data[8], BmsStatus_t *s)
{
    s->soc_pct        = (float)data[0] * 0.5f;
    s->soh_pct        = (float)data[1];

    uint16_t raw_volt = ((uint16_t)data[2] << 8) | data[3];
    s->pack_voltage_v = (float)raw_volt * 0.1f;

    int16_t raw_curr  = (int16_t)(((uint16_t)data[4] << 8) | data[5]);
    s->pack_current_a = (float)raw_curr * 0.1f;

    uint8_t flags     = data[6];
    s->fault_active     = (flags >> 7) & 1u;
    s->contactor_closed = (flags >> 6) & 1u;
    s->charging_active  = (flags >> 5) & 1u;
    s->balancing_active = (flags >> 4) & 1u;
}
```

---

### 7.2 MCU Torque Request with Alive Counter and Checksum (C)

```c
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int16_t  torque_requested_nm;   /* 0.1 Nm/bit */
    uint16_t max_speed_rpm;
    uint8_t  drive_mode;            /* 0=Off, 1=Drive, 2=Regen, 3=Neutral */
    uint8_t  regen_level;           /* 0–15 */
} VcuTorqueRequest_t;

static uint8_t s_alive_counter = 0;

/* Simple XOR checksum over bytes 0..6 */
static uint8_t calc_checksum(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}

/* Build MCU torque request CAN frame (0x200, 8 bytes) */
bool VCU_BuildTorqueRequest(const VcuTorqueRequest_t *req, uint8_t data[8])
{
    /* Clamp torque to ±3000 Nm (raw ±30000) */
    int16_t raw_torque = req->torque_requested_nm;
    if (raw_torque >  30000) raw_torque =  30000;
    if (raw_torque < -30000) raw_torque = -30000;

    data[0] = (uint8_t)((uint16_t)raw_torque >> 8);
    data[1] = (uint8_t)((uint16_t)raw_torque & 0xFF);

    data[2] = (uint8_t)(req->max_speed_rpm >> 8);
    data[3] = (uint8_t)(req->max_speed_rpm & 0xFF);

    data[4] = req->drive_mode & 0x03u;
    data[5] = req->regen_level & 0x0Fu;

    /* Rolling alive counter — increment each frame */
    data[6] = s_alive_counter++;

    /* XOR checksum over bytes 0–6 */
    data[7] = calc_checksum(data, 7);

    return true;
}

/* Validate received torque request (MCU side) */
typedef struct {
    uint8_t last_alive;
    uint8_t miss_count;
} McuWatchdog_t;

bool MCU_ValidateTorqueRequest(const uint8_t data[8], McuWatchdog_t *wd)
{
    /* Verify checksum */
    uint8_t expected = calc_checksum(data, 7);
    if (data[7] != expected) {
        return false;
    }

    /* Verify alive counter incremented by 1 (wrap 255→0 is valid) */
    uint8_t current = data[6];
    uint8_t delta   = (uint8_t)(current - wd->last_alive);
    if (delta != 1u) {
        wd->miss_count++;
        if (wd->miss_count >= 3) {
            /* Safety reaction: reduce torque / shutdown */
            return false;
        }
    } else {
        wd->miss_count = 0;
    }

    wd->last_alive = current;
    return true;
}
```

---

### 7.3 CAN FD BMS Cell Voltage Broadcast (C++)

```cpp
#include <cstdint>
#include <array>
#include <vector>
#include <algorithm>
#include <cstring>

constexpr uint32_t CANFD_ID_BMS_CELL_VOLTAGES = 0x310U;
constexpr uint8_t  MAX_CELLS = 32;

struct CellVoltageFrame {
    uint32_t can_id;
    uint8_t  dlc;       /* CAN FD DLC: 0–15 (15 = 64 bytes) */
    uint8_t  data[64];
};

/**
 * @brief Encode up to 32 cell voltages into a 64-byte CAN FD frame.
 *
 * Encoding: uint16 big-endian per cell, 0.1 mV per bit.
 * So 3.700 V = 37000 raw.
 */
CellVoltageFrame BMS_EncodeCellVoltages(
    const std::vector<float>& cell_voltages_v)
{
    CellVoltageFrame frame{};
    frame.can_id = CANFD_ID_BMS_CELL_VOLTAGES;
    frame.dlc    = 15;  /* 64 bytes */
    std::memset(frame.data, 0, sizeof(frame.data));

    size_t count = std::min(cell_voltages_v.size(), static_cast<size_t>(MAX_CELLS));

    for (size_t i = 0; i < count; ++i) {
        uint16_t raw = static_cast<uint16_t>(cell_voltages_v[i] * 10000.0f); /* V → 0.1mV */
        frame.data[i * 2]     = static_cast<uint8_t>(raw >> 8);
        frame.data[i * 2 + 1] = static_cast<uint8_t>(raw & 0xFF);
    }

    return frame;
}

/**
 * @brief Decode a 64-byte CAN FD cell voltage frame.
 */
std::vector<float> BMS_DecodeCellVoltages(const CellVoltageFrame& frame)
{
    std::vector<float> voltages;
    voltages.reserve(MAX_CELLS);

    for (int i = 0; i < MAX_CELLS; ++i) {
        uint16_t raw = (static_cast<uint16_t>(frame.data[i * 2]) << 8)
                     |  static_cast<uint16_t>(frame.data[i * 2 + 1]);
        if (raw == 0) break; /* Treat 0 as padding / no more cells */
        voltages.push_back(static_cast<float>(raw) / 10000.0f); /* 0.1mV → V */
    }

    return voltages;
}
```

---

### 7.4 BMS State Machine with CAN Fault Handling (C++)

```cpp
#include <cstdint>
#include <functional>

enum class BmsState : uint8_t {
    Idle = 0,
    Precharge,
    Ready,
    Charging,
    Fault,
    Shutdown
};

struct BmsFaultFlags {
    bool overvoltage     : 1;
    bool undervoltage    : 1;
    bool overcurrent     : 1;
    bool overtemperature : 1;
    bool undertemperature: 1;
    bool isolation_fault : 1;
    uint8_t reserved     : 2;
};

class BmsController {
public:
    using CanSendFn = std::function<void(uint32_t id, const uint8_t*, uint8_t)>;

    explicit BmsController(CanSendFn send_fn)
        : send_(std::move(send_fn)), state_(BmsState::Idle) {}

    /* Called every 10 ms from CAN Rx task */
    void OnCanMessage(uint32_t id, const uint8_t* data, uint8_t dlc)
    {
        switch (id) {
        case 0x100:  /* VCU command */
            HandleVcuCommand(data, dlc);
            break;
        case 0x200:  /* MCU status — monitor DC link */
            HandleMcuStatus(data, dlc);
            break;
        default:
            break;
        }
    }

    /* 10 ms cyclic task */
    void Cyclic10ms()
    {
        BmsFaultFlags faults = CheckFaults();
        if (HasAnyFault(faults) && state_ != BmsState::Fault) {
            EnterFault(faults);
        }
        TransmitStatus();
    }

private:
    void HandleVcuCommand(const uint8_t* data, uint8_t dlc)
    {
        if (dlc < 1) return;
        uint8_t cmd = data[0];
        if (cmd == 0x01 && state_ == BmsState::Idle)   state_ = BmsState::Precharge;
        if (cmd == 0x02 && state_ == BmsState::Ready)  state_ = BmsState::Charging;
        if (cmd == 0x00)                                state_ = BmsState::Shutdown;
    }

    void HandleMcuStatus(const uint8_t* data, uint8_t dlc)
    {
        if (dlc < 6) return;
        uint16_t raw_v = (static_cast<uint16_t>(data[4]) << 8) | data[5];
        dc_link_voltage_v_ = static_cast<float>(raw_v) * 0.1f;
    }

    BmsFaultFlags CheckFaults()
    {
        BmsFaultFlags f{};
        /* In a real system, read from cell monitoring ICs via SPI/I2C */
        f.overvoltage      = (pack_voltage_v_ > 420.0f);
        f.undervoltage     = (pack_voltage_v_ < 280.0f);
        f.overcurrent      = (pack_current_a_ > 500.0f || pack_current_a_ < -500.0f);
        f.overtemperature  = (max_temp_c_ > 55.0f);
        f.undertemperature = (min_temp_c_ < -20.0f);
        return f;
    }

    static bool HasAnyFault(const BmsFaultFlags& f)
    {
        return f.overvoltage || f.undervoltage || f.overcurrent
            || f.overtemperature || f.undertemperature || f.isolation_fault;
    }

    void EnterFault(const BmsFaultFlags& faults)
    {
        state_ = BmsState::Fault;

        /* Transmit fault message (0x305) immediately */
        uint8_t msg[8] = {};
        msg[0] = static_cast<uint8_t>(state_);
        msg[1] = *reinterpret_cast<const uint8_t*>(&faults);
        /* Bytes 2-5: DTC codes (simplified) */
        msg[2] = faults.overvoltage     ? 0x01 : 0x00;
        msg[3] = faults.undervoltage    ? 0x02 : 0x00;
        msg[4] = faults.overcurrent     ? 0x03 : 0x00;
        msg[5] = faults.overtemperature ? 0x04 : 0x00;
        send_(0x305, msg, 8);
    }

    void TransmitStatus()
    {
        uint8_t data[8];
        BmsStatus_t status{};
        status.soc_pct        = soc_pct_;
        status.soh_pct        = soh_pct_;
        status.pack_voltage_v = pack_voltage_v_;
        status.pack_current_a = pack_current_a_;
        status.fault_active     = (state_ == BmsState::Fault);
        status.contactor_closed = (state_ == BmsState::Ready ||
                                   state_ == BmsState::Charging);
        status.charging_active  = (state_ == BmsState::Charging);

        /* Use encode function from section 7.1 */
        BMS_EncodeStatus(&status, data);
        send_(0x300, data, 8);
    }

    /* Placeholder: see section 7.1 for full implementation */
    struct BmsStatus_t {
        float soc_pct, soh_pct, pack_voltage_v, pack_current_a;
        bool fault_active, contactor_closed, charging_active, balancing_active;
    };
    void BMS_EncodeStatus(const BmsStatus_t*, uint8_t[8]) { /* ... */ }

    CanSendFn   send_;
    BmsState    state_;
    float       pack_voltage_v_   = 400.0f;
    float       pack_current_a_   = 0.0f;
    float       dc_link_voltage_v_= 0.0f;
    float       soc_pct_          = 80.0f;
    float       soh_pct_          = 98.0f;
    float       max_temp_c_       = 25.0f;
    float       min_temp_c_       = 20.0f;
};
```

---

## 8. Rust Implementation Examples

### 8.1 BMS Message Definitions with `embedded-can` (Rust)

```toml
# Cargo.toml
[dependencies]
embedded-can   = "0.4"
heapless       = "0.8"
defmt          = "0.3"   # optional: embedded logging
```

```rust
//! bms_can.rs — BMS CAN message encoding/decoding in Rust

use core::convert::TryFrom;

/// BMS Status message (CAN ID 0x300)
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct BmsStatus {
    /// State of Charge in percent (0.0–100.0)
    pub soc_pct: f32,
    /// State of Health in percent (0.0–100.0)
    pub soh_pct: f32,
    /// Pack terminal voltage in Volts
    pub pack_voltage_v: f32,
    /// Pack current in Amperes (+ve = discharge)
    pub pack_current_a: f32,
    /// Status flags
    pub fault_active:     bool,
    pub contactor_closed: bool,
    pub charging_active:  bool,
    pub balancing_active: bool,
}

/// Error type for decode failures
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum BmsDecodeError {
    InvalidLength,
    ValueOutOfRange,
}

impl BmsStatus {
    pub const CAN_ID: u32 = 0x300;
    pub const DLC: usize  = 8;

    /// Encode to 8-byte CAN payload
    pub fn encode(&self) -> [u8; 8] {
        let mut data = [0u8; 8];

        // Byte 0: SOC — 0.5 %/bit
        data[0] = (self.soc_pct / 0.5) as u8;

        // Byte 1: SOH — 1 %/bit
        data[1] = self.soh_pct as u8;

        // Bytes 2-3: Pack voltage — 0.1 V/bit, big-endian
        let raw_v = (self.pack_voltage_v / 0.1) as u16;
        data[2] = (raw_v >> 8) as u8;
        data[3] = (raw_v & 0xFF) as u8;

        // Bytes 4-5: Pack current — 0.1 A/bit, signed big-endian
        let raw_a = (self.pack_current_a / 0.1) as i16;
        let raw_a_u = raw_a as u16;
        data[4] = (raw_a_u >> 8) as u8;
        data[5] = (raw_a_u & 0xFF) as u8;

        // Byte 6: Flags
        data[6] = (self.fault_active     as u8) << 7
                | (self.contactor_closed as u8) << 6
                | (self.charging_active  as u8) << 5
                | (self.balancing_active as u8) << 4;

        // Byte 7: Reserved
        data[7] = 0;

        data
    }

    /// Decode from 8-byte CAN payload
    pub fn decode(data: &[u8]) -> Result<Self, BmsDecodeError> {
        if data.len() < Self::DLC {
            return Err(BmsDecodeError::InvalidLength);
        }

        let soc_pct = (data[0] as f32) * 0.5;
        let soh_pct =  data[1] as f32;

        let raw_v = ((data[2] as u16) << 8) | (data[3] as u16);
        let pack_voltage_v = raw_v as f32 * 0.1;

        let raw_a = (((data[4] as u16) << 8) | (data[5] as u16)) as i16;
        let pack_current_a = raw_a as f32 * 0.1;

        let flags = data[6];

        Ok(BmsStatus {
            soc_pct,
            soh_pct,
            pack_voltage_v,
            pack_current_a,
            fault_active:     (flags >> 7) & 1 != 0,
            contactor_closed: (flags >> 6) & 1 != 0,
            charging_active:  (flags >> 5) & 1 != 0,
            balancing_active: (flags >> 4) & 1 != 0,
        })
    }
}
```

---

### 8.2 MCU Torque Request with Rolling Counter (Rust)

```rust
//! mcu_torque.rs — MCU torque request frame builder

/// VCU → MCU torque request (CAN ID 0x200)
#[derive(Debug, Clone, Copy)]
pub struct TorqueRequest {
    /// Requested torque in Nm (±3000 Nm range)
    pub torque_nm:      f32,
    /// Maximum allowed motor speed in RPM
    pub max_speed_rpm:  u16,
    /// Drive mode
    pub mode:           DriveMode,
    /// Regenerative braking level 0–15
    pub regen_level:    u8,
}

#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum DriveMode {
    Off     = 0,
    Drive   = 1,
    Regen   = 2,
    Neutral = 3,
}

pub struct TorqueRequestEncoder {
    alive_counter: u8,
}

impl TorqueRequestEncoder {
    pub fn new() -> Self {
        Self { alive_counter: 0 }
    }

    /// Encode a torque request frame, incrementing the alive counter
    pub fn encode(&mut self, req: &TorqueRequest) -> [u8; 8] {
        let mut data = [0u8; 8];

        // Clamp and encode torque: 0.1 Nm/bit
        let torque_clamped = req.torque_nm.clamp(-3000.0, 3000.0);
        let raw_torque = (torque_clamped / 0.1) as i16;
        let raw_torque_u = raw_torque as u16;
        data[0] = (raw_torque_u >> 8) as u8;
        data[1] = (raw_torque_u & 0xFF) as u8;

        data[2] = (req.max_speed_rpm >> 8) as u8;
        data[3] = (req.max_speed_rpm & 0xFF) as u8;

        data[4] = req.mode as u8;
        data[5] = req.regen_level & 0x0F;

        // Rolling alive counter (wraps 0–255)
        data[6] = self.alive_counter;
        self.alive_counter = self.alive_counter.wrapping_add(1);

        // XOR checksum over bytes 0..6
        data[7] = data[..7].iter().fold(0u8, |acc, &b| acc ^ b);

        data
    }
}

/// MCU-side watchdog validator
pub struct TorqueWatchdog {
    last_alive:  u8,
    miss_count:  u8,
    initialized: bool,
}

#[derive(Debug, PartialEq)]
pub enum WatchdogResult {
    Valid,
    ChecksumError,
    AliveCounterMissed { misses: u8 },
    SafetyTriggered,
}

impl TorqueWatchdog {
    pub fn new() -> Self {
        Self { last_alive: 0, miss_count: 0, initialized: false }
    }

    pub fn validate(&mut self, data: &[u8; 8]) -> WatchdogResult {
        // Verify checksum
        let expected = data[..7].iter().fold(0u8, |acc, &b| acc ^ b);
        if data[7] != expected {
            return WatchdogResult::ChecksumError;
        }

        let current = data[6];

        if !self.initialized {
            self.last_alive = current;
            self.initialized = true;
            return WatchdogResult::Valid;
        }

        // Check alive counter delta (wrapping arithmetic)
        let delta = current.wrapping_sub(self.last_alive);
        if delta != 1 {
            self.miss_count = self.miss_count.saturating_add(1);
            if self.miss_count >= 3 {
                return WatchdogResult::SafetyTriggered;
            }
            return WatchdogResult::AliveCounterMissed { misses: self.miss_count };
        }

        self.miss_count = 0;
        self.last_alive = current;
        WatchdogResult::Valid
    }
}
```

---

### 8.3 EV CAN Bus Router with `tokio` (Rust, async)

```rust
//! ev_can_router.rs
//! Async CAN message router for EV powertrain network.
//! Demonstrates routing BMS, MCU, and OBC messages in a real-time task.

use std::sync::Arc;
use tokio::sync::mpsc;
use tokio::time::{self, Duration};

/// Generic CAN frame representation
#[derive(Debug, Clone)]
pub struct CanFrame {
    pub id:   u32,
    pub dlc:  u8,
    pub data: [u8; 8],
}

/// Decoded EV powertrain event
#[derive(Debug, Clone)]
pub enum EvEvent {
    BmsStatus(BmsStatus),
    McuStatus { torque_nm: f32, speed_rpm: i16, inverter_temp_c: i8 },
    ObcStatus { charge_state: u8, power_w: u32 },
    FaultDetected { source: &'static str, code: u8 },
}

// (BmsStatus imported from bms_can.rs above)
use crate::bms_can::BmsStatus;

/// Route an incoming CAN frame to an EV event
pub fn route_can_frame(frame: &CanFrame) -> Option<EvEvent> {
    match frame.id {
        0x300 => {
            BmsStatus::decode(&frame.data)
                .ok()
                .map(EvEvent::BmsStatus)
        }
        0x201 => {
            if frame.dlc >= 7 {
                let raw_t = i16::from_be_bytes([frame.data[0], frame.data[1]]);
                let raw_s = i16::from_be_bytes([frame.data[2], frame.data[3]]);
                let temp  = frame.data[6] as i8 - 40; // offset −40°C
                Some(EvEvent::McuStatus {
                    torque_nm:       raw_t as f32 * 0.1,
                    speed_rpm:       raw_s,
                    inverter_temp_c: temp,
                })
            } else {
                None
            }
        }
        0x305 => {
            if frame.dlc >= 2 {
                Some(EvEvent::FaultDetected {
                    source: "BMS",
                    code: frame.data[1],
                })
            } else {
                None
            }
        }
        0x600 => {
            if frame.dlc >= 6 {
                let power_w = (frame.data[5] as u32) * 100;
                Some(EvEvent::ObcStatus {
                    charge_state: frame.data[0],
                    power_w,
                })
            } else {
                None
            }
        }
        _ => None,
    }
}

/// Async EV CAN bus monitor task
pub async fn ev_monitor_task(
    mut rx: mpsc::Receiver<CanFrame>,
    event_tx: mpsc::Sender<EvEvent>,
) {
    // Watchdog: BMS must send a message every 100 ms
    let mut bms_watchdog = time::interval(Duration::from_millis(150));

    loop {
        tokio::select! {
            Some(frame) = rx.recv() => {
                if let Some(event) = route_can_frame(&frame) {
                    // Reset watchdog on valid BMS message
                    if matches!(event, EvEvent::BmsStatus(_)) {
                        bms_watchdog.reset();
                    }
                    let _ = event_tx.send(event).await;
                }
            }
            _ = bms_watchdog.tick() => {
                // BMS timeout — trigger fault
                let _ = event_tx.send(EvEvent::FaultDetected {
                    source: "BMS_WATCHDOG",
                    code: 0xFF,
                }).await;
            }
        }
    }
}
```

---

### 8.4 CHAdeMO DC Fast Charge Negotiation (Rust)

```rust
//! chademo.rs — CHAdeMO CAN message encode/decode for DC fast charging

/// CHAdeMO vehicle → EVSE message (CAN ID 0x100)
/// Sent every 100 ms during charging session
#[derive(Debug, Clone, Copy)]
pub struct ChademoVehicleMsg {
    /// Maximum battery voltage the vehicle can accept (V)
    pub max_voltage_v:   u16,
    /// Rated battery capacity (0.1 kWh per bit)
    pub capacity_kwh_x10: u8,
    /// Current state of charge (%)
    pub soc_pct:         u8,
    /// Requested charge current (A)
    pub requested_current_a: u8,
    /// Enable charge flag
    pub charge_enable:   bool,
    /// Fault flags
    pub fault_battery:   bool,
    pub fault_vehicle:   bool,
}

impl ChademoVehicleMsg {
    pub const CAN_ID: u32 = 0x100;

    pub fn encode(&self) -> [u8; 8] {
        let mut data = [0u8; 8];
        data[0] = (self.max_voltage_v >> 8) as u8;
        data[1] = (self.max_voltage_v & 0xFF) as u8;
        data[2] = self.capacity_kwh_x10;
        data[3] = self.soc_pct;
        data[4] = self.requested_current_a;
        data[5] = (self.charge_enable    as u8)
                | (self.fault_battery    as u8) << 1
                | (self.fault_vehicle    as u8) << 2;
        data[6] = 0; // reserved
        data[7] = 0;
        data
    }
}

/// CHAdeMO EVSE → vehicle message (CAN ID 0x108)
#[derive(Debug, Clone, Copy)]
pub struct ChademoEvseMsg {
    /// EVSE output voltage (V)
    pub output_voltage_v:  u16,
    /// EVSE output current (A)
    pub output_current_a:  u8,
    /// EVSE available current (A)
    pub available_current_a: u8,
    /// EVSE status flags
    pub status_evse_ready: bool,
    pub status_charging:   bool,
    pub fault_evse:        bool,
}

impl ChademoEvseMsg {
    pub const CAN_ID: u32 = 0x108;

    pub fn decode(data: &[u8]) -> Option<Self> {
        if data.len() < 6 { return None; }
        let voltage = ((data[0] as u16) << 8) | data[1] as u16;
        let flags   = data[4];
        Some(Self {
            output_voltage_v:   voltage,
            output_current_a:   data[2],
            available_current_a: data[3],
            status_evse_ready: (flags >> 0) & 1 != 0,
            status_charging:   (flags >> 1) & 1 != 0,
            fault_evse:        (flags >> 7) & 1 != 0,
        })
    }
}
```

---

## 9. Safety, Fault Handling & Diagnostics (UDS/OBD-II)

### 9.1 ISO 15765-2 (ISO-TP) for Diagnostics over CAN

Long diagnostic messages (UDS, OBD-II) are transported using ISO 15765-2 (ISO-TP)
segmentation over CAN (physical addressing, 11-bit or 29-bit).

```
Single Frame (SF):   CAN DLC <= 8, PCI = 0x0N (N = data length)
First Frame (FF):    PCI = 0x1N NN (N = total length up to 4095 bytes)
Consecutive Frame:   PCI = 0x2N (N = sequence 1–15, then wraps)
Flow Control (FC):   PCI = 0x3N (0=CTS, 1=Wait, 2=Overflow)
```

### 9.2 UDS Service Examples Relevant to EVs

| UDS Service | SID | EV Use Case |
|---|---|---|
| ReadDataByIdentifier | 0x22 | Read SOC (DID 0xF45A), SOH (DID 0xF45B) |
| WriteDataByIdentifier | 0x2E | BMS calibration |
| RoutineControl | 0x31 | Cell balancing routine, HV isolation test |
| DiagnosticSessionControl | 0x10 | Enter extended session for BMS programming |
| ECUReset | 0x11 | BMS / MCU soft reset after firmware update |
| ReadDTCInformation | 0x19 | Retrieve BMS/MCU Diagnostic Trouble Codes |
| ClearDiagnosticInformation | 0x14 | Clear DTCs after repair |

### 9.3 Contactor Weld Detection (Safety-Critical Routine)

```c
/* UDS RoutineControl (0x31) — Weld Detection Routine */
/* Request:  [0x31, 0x01, 0xEA, 0x01]  (startRoutine, routineId=0xEA01) */
/* Response: [0x71, 0x01, 0xEA, 0x01, result_byte] */
/* result_byte: 0x00=No weld, 0x01=Main+ welded, 0x02=Main- welded, 0x03=Pre-charge welded */

typedef enum {
    WELD_NONE        = 0x00,
    WELD_MAIN_POS    = 0x01,
    WELD_MAIN_NEG    = 0x02,
    WELD_PRECHARGE   = 0x03,
} WeldDetectionResult;

/* Simplified weld detection: open contactors, measure pack voltage */
WeldDetectionResult BMS_RunWeldDetection(void)
{
    /* Step 1: Command contactors open */
    BMS_OpenAllContactors();
    HAL_Delay(50); /* Allow settling */

    /* Step 2: Measure residual voltage across output terminals */
    float terminal_v = ADC_MeasureTerminalVoltage();
    float pack_v     = ADC_MeasurePackVoltage();

    /* Step 3: If terminal voltage > threshold, a contactor is welded */
    if (terminal_v > (pack_v * 0.9f)) {
        /* Determine which one by individual contactor test sequence */
        return WELD_MAIN_POS; /* Simplified */
    }
    return WELD_NONE;
}
```

---

## 10. Real-World Protocol References

| Standard | Description | EV Relevance |
|---|---|---|
| ISO 11898-1 | CAN data link layer | Foundation for all CAN in vehicles |
| ISO 11898-2 | CAN physical layer | Differential signaling, bus termination |
| ISO 15765-2 | ISO-TP (transport protocol) | UDS/OBD-II segmented messages |
| ISO 14229 (UDS) | Unified Diagnostic Services | BMS/MCU diagnostics, DTC management |
| ISO 15118 | Vehicle-to-Grid communication | AC/DC charging, PLC over CAN gateway |
| CHAdeMO 2.0 | DC fast charging (Japan/legacy) | CAN-native protocol at 500 kbit/s |
| SAE J1939 | Heavy-duty vehicle networking | Commercial EVs, buses, trucks |
| SAE J2284 | CAN for passenger car powertrain | Bit rate, topology guidelines |
| GB/T 27930 | Chinese DC fast charging standard | CAN-based, common in China EVs |
| IEC 61851 | EV conductive charging system | CP/PP signal, EVSE communication |

---

## 11. Summary

CAN bus is the nervous system of modern electric vehicle powertrains, coordinating three
critical domains: high-voltage battery management, motor control, and charging communication.

**Battery Management System (BMS):** The BMS is the most safety-critical CAN node. It
broadcasts state-of-charge, state-of-health, cell voltages, temperature data, and current
limits at cyclic rates of 10–1000 ms. Fault messages are event-driven and carry Diagnostic
Trouble Codes. CAN FD is increasingly used to transmit all cell voltages in a single 64-byte
frame. The BMS enforces protection limits in hardware but uses CAN to coordinate system-level
responses such as contactor control and charger power limiting.

**Motor Control Unit (MCU):** The VCU issues torque requests to the MCU every 10 ms with an
alive counter and XOR checksum for functional safety (ASIL-C/D per ISO 26262). Missing three
frames triggers a torque-to-zero reaction. The MCU reports actual torque, motor speed, DC link
voltage, and inverter temperature. Drive mode transitions (Drive / Regen / Neutral) and
regenerative braking levels are encoded in the torque request frame.

**Charging Communication:** AC charging uses the OBC, which reports charge state and power over
CAN while negotiating with the EVSE via IEC 61851 PWM or ISO 15118 PLC. DC fast charging uses
CHAdeMO (a CAN-native protocol) or CCS (PLC externally, CAN internally), where the vehicle
negotiates maximum voltage, current requests, and session state with the EVSE. Both protocols
require the BMS to provide real-time SOC, maximum voltage, and current limits.

**Safety Architecture:** CAN message integrity in EVs relies on alive counters, XOR or CRC-8
checksums, and timeout watchdogs at both the transmitter and receiver. UDS (ISO 14229) over
ISO-TP is used for diagnostics, firmware updates, and safety routines such as contactor weld
detection. Message priority is encoded in the CAN ID, ensuring emergency shutdown commands
always win bus arbitration.

**C/C++ vs Rust:** C/C++ remains dominant in production ECUs due to AUTOSAR compatibility and
mature toolchain support (e.g., Vector CANdb++, PEAK PCAN). Rust is gaining traction for
new embedded EV components due to its memory safety guarantees, fearless concurrency, and
strong type system — which maps naturally to the strict encoding/decoding contracts required
by CAN message specifications.

---

*Document: 84 — CAN in Electric Vehicle Architecture | Revision 1.0*