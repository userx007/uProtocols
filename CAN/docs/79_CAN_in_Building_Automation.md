# 79. CAN in Building Automation

- **Why CAN for Building Automation** — a comparison table of CAN vs RS-485/Modbus vs Ethernet/BACnet IP across determinism, cable length, node count, and cost
- **Key Protocols** — CANopen (EN 50325-4, CiA profiles 401/404/443), BACnet/CAN, and LonWorks bridging
- **Network Architecture** — ASCII topology diagram showing the supervisory IP layer, CAN/IP gateway, and field device segments

**HVAC (C++ + Rust):**
- C++ node using CANopenNode with a PID valve controller, CO₂-based ventilation boost, and SDO-driven remote setpoints
- Rust publisher using raw SocketCAN to encode REAL32 temperatures in CANopen TPDO frames, handle NMT commands, and respond to SDO writes

**Lighting (C++ + Rust):**
- C++ DALI-over-CAN bridge translating RPDO scene commands to DALI IEC 62386 UART frames with broadcast addressing
- Rust scene controller doing daylight-linked and occupancy-linked scene selection across DALI segments

**Access Control (C++ + Rust):**
- C++ door node handling Wiegand credentials, credential whitelisting, door-held alarms, REX, and emergency object generation
- Rust audit logger consuming TPDOs from multiple access nodes into a structured JSONL audit trail

**Inter-system integration:** An SDO-based occupancy→HVAC gateway in C++ showing how CAN enables subsystem orchestration without a central server.

**Diagnostics + Security:** A heartbeat supervision monitor with CAN error frame decoding, plus a security section covering physical isolation, gateway filtering, CiA 452 authentication, and firmware signing.


## Integration with HVAC, Lighting, and Access Control Systems Using CAN-Based Protocols

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why CAN for Building Automation?](#why-can-for-building-automation)
3. [Key CAN-Based Protocols for Buildings](#key-can-based-protocols-for-buildings)
   - [CANopen (EN 50325-4)](#canopen-en-50325-4)
   - [BACnet/CAN](#bacnetcan)
   - [LonWorks over CAN](#lonworks-over-can)
4. [Network Architecture](#network-architecture)
5. [HVAC Integration](#hvac-integration)
   - [Concepts and Object Model](#hvac-concepts-and-object-model)
   - [C/C++ Example: HVAC Controller Node](#cc-example-hvac-controller-node)
   - [Rust Example: HVAC Sensor Publisher](#rust-example-hvac-sensor-publisher)
6. [Lighting Control Integration](#lighting-control-integration)
   - [Concepts and Object Model](#lighting-concepts-and-object-model)
   - [C/C++ Example: DALI-over-CAN Bridge](#cc-example-dali-over-can-bridge)
   - [Rust Example: Scene-Based Lighting Controller](#rust-example-scene-based-lighting-controller)
7. [Access Control Integration](#access-control-integration)
   - [Concepts and Object Model](#access-control-concepts-and-object-model)
   - [C/C++ Example: Access Control Door Node](#cc-example-access-control-door-node)
   - [Rust Example: Access Event Logger](#rust-example-access-event-logger)
8. [Inter-System Integration: HVAC + Access Control](#inter-system-integration-hvac--access-control)
   - [C/C++ Example: Occupancy-Driven HVAC](#cc-example-occupancy-driven-hvac)
9. [Error Handling and Diagnostics](#error-handling-and-diagnostics)
   - [C/C++ Example: Bus Diagnostic Monitor](#cc-example-bus-diagnostic-monitor)
10. [Security Considerations](#security-considerations)
11. [Summary](#summary)

---

## Introduction

Building automation systems (BAS), also called building management systems (BMS), orchestrate the mechanical, electrical, and electromechanical services in a facility. These services include HVAC (heating, ventilation and air conditioning), lighting, access control, fire detection, and energy metering. Historically, each subsystem operated on a proprietary fieldbus or standalone wiring scheme, resulting in siloed "islands" that were difficult to integrate, maintain, and extend.

The **Controller Area Network (CAN bus)**, originally developed by Robert Bosch GmbH in 1983 for automotive use, brings several compelling properties to building automation:

- **Deterministic, real-time message delivery** with priority arbitration
- **Robust differential signalling** (ISO 11898) tolerant of electrically noisy environments
- **Multi-master bus access** with no single point of failure
- **Long cable runs** (up to ~1 km at 50 kbit/s) suitable for floor-level distribution
- **Low node cost** with widespread silicon availability

Higher-layer protocols—primarily **CANopen**, and to a lesser extent **BACnet/CAN** and **SAE J1939-derived** variants—have been standardised on top of raw CAN to address application-layer concerns: device profiles, network management, object dictionaries, and interoperability.

---

## Why CAN for Building Automation?

| Property | CAN Bus | RS-485 / Modbus | Ethernet/BACnet IP |
|---|---|---|---|
| Determinism | Hard real-time (CSMA/CA) | Polling, master-slave | Best-effort / UDP |
| Cable length | Up to 1 km @ 50 kbit/s | Up to 1.2 km | 100 m per segment |
| Node count | Up to 127 (CANopen) | Up to 247 | Unlimited (IP routing) |
| Wiring cost | Low (2-wire + shield) | Low | Moderate (Cat5/6) |
| Noise immunity | Excellent | Good | Good (isolated) |
| Hot-plugging | Supported | Requires bus quiesce | Supported |
| Multi-master | Yes | No (single master) | Yes |
| Typical bitrate | 125 kbit/s – 1 Mbit/s | 9.6 – 115.2 kbit/s | 10/100/1000 Mbit/s |

CAN occupies the **field device layer**: sensors, actuators, and local controllers. IP-based systems serve the **supervisory layer** (SCADA, BMS dashboards, cloud analytics). Gateways bridge the two worlds.

---

## Key CAN-Based Protocols for Buildings

### CANopen (EN 50325-4)

CANopen, standardised as EN 50325-4 and maintained by CiA (CAN in Automation), is the dominant higher-layer protocol for building automation. It defines:

- **Object Dictionary (OD):** A structured table of every configurable and measurable parameter on a node, indexed by a 16-bit index and 8-bit sub-index.
- **Process Data Objects (PDO):** High-priority, low-latency messages for cyclic or event-driven real-time data (e.g., temperature readings, dimmer setpoints).
- **Service Data Objects (SDO):** Lower-priority, confirmed read/write access to any OD entry for configuration and diagnostics.
- **Network Management (NMT):** Start, stop, reset, and heartbeat supervision of nodes.
- **Emergency Objects (EMCY):** Node-initiated error reporting.
- **Synchronisation Object (SYNC):** Global clock pulse for synchronised PDO transmission.

Relevant CiA device profiles for buildings:

| CiA Profile | Application |
|---|---|
| CiA 401 | Generic I/O modules (digital/analog I/O) |
| CiA 404 | Measuring devices and closed-loop controllers (HVAC) |
| CiA 417 | Lift/escalator systems |
| CiA 443 | Room automation (lighting, HVAC, shading) |

### BACnet/CAN

BACnet (ANSI/ASHRAE Standard 135) defines a rich object model for building systems (AI, AO, BI, BO, Schedule, Trend Log, etc.) and specifies **BACnet/MS-TP** (RS-485) as the primary serial datalink. While less common, BACnet can be tunnelled or adapted over CAN at the datalink layer, allowing BACnet objects to be exchanged between CAN-connected field devices and upstream BACnet/IP networks via gateway controllers.

### LonWorks over CAN

Echelon's LonWorks protocol (ISO/IEC 14908) uses its own FT-10 twisted-pair physical layer but some vendors produce CAN-to-LonWorks bridges, enabling co-existence with installed LonWorks infrastructure.

---

## Network Architecture

```
  ┌─────────────────────────────────────────────────────────┐
  │               Supervisory / BMS Layer (Ethernet/IP)      │
  │   ┌──────────┐   ┌──────────┐   ┌──────────────────┐    │
  │   │ BMS SCADA│   │ Analytics│   │  Cloud Gateway   │    │
  │   └────┬─────┘   └─────┬────┘   └────────┬─────────┘    │
  └────────┼───────────────┼─────────────────┼──────────────┘
           │               │                 │
           └───────────────┴────────┬────────┘
                                    │ BACnet/IP or Modbus TCP
                          ┌─────────┴──────────┐
                          │   CAN/IP Gateway   │
                          │  (Protocol Bridge) │
                          └─────────┬──────────┘
                                    │ CAN Bus (ISO 11898-2)
           ┌────────────────────────┼────────────────────────┐
           │                        │                        │
  ┌────────┴────────┐   ┌───────────┴──────┐   ┌────────────┴───────┐
  │  HVAC Controller│   │ Lighting Manager  │   │ Access Control Node│
  │  (CANopen NMT   │   │ (CiA 443 Profile) │   │ (Custom Profile)   │
  │   Master)       │   └──────────┬────────┘   └────────────┬───────┘
  └──────────┬──────┘              │                         │
             │              ┌──────┴──────┐           ┌──────┴──────┐
      ┌──────┴──────┐       │ DALI Driver │           │  Card Reader│
      │ VAV Box     │       │ (per zone)  │           │  (Wiegand)  │
      │ Damper Ctrl │       └─────────────┘           └─────────────┘
      └─────────────┘
```

Typical segment parameters for a building automation CAN bus:

- **Bitrate:** 125 kbit/s (up to 500 m cable) or 250 kbit/s (up to 250 m)
- **Termination:** 120 Ω at each physical end
- **Cable:** Shielded twisted pair, e.g., Belden 3105A
- **Node count:** ≤ 64 nodes per segment (practical limit; 127 is CANopen maximum)
- **Topology:** Linear bus with short stubs (< 0.3 m recommended)

---

## HVAC Integration

### HVAC Concepts and Object Model

HVAC systems consist of air-handling units (AHUs), variable air volume (VAV) boxes, fan coil units (FCUs), chillers, boilers, and thermostats. On a CANopen network each physical device is a **node** with an Object Dictionary. Typical OD entries for an HVAC controller (CiA 404):

| OD Index | Sub-Index | Name | Type | Description |
|---|---|---|---|---|
| 0x6000 | 0x01 | Room Temperature | REAL32 | Measured °C |
| 0x6001 | 0x01 | Temperature Setpoint | REAL32 | Target °C |
| 0x6002 | 0x01 | Valve Position | UINT8 | 0–100 % |
| 0x6003 | 0x01 | Fan Speed | UINT8 | 0–100 % |
| 0x6004 | 0x01 | CO₂ Concentration | UINT16 | ppm |
| 0x6005 | 0x01 | Occupancy Status | UINT8 | 0=vacant, 1=occupied |
| 0x1001 | 0x00 | Error Register | UINT8 | CANopen standard |

### C/C++ Example: HVAC Controller Node

The following example uses the open-source **CANopenNode** library (https://github.com/CANopenNode/CANopenNode) and a SocketCAN Linux backend.

```cpp
// hvac_node.cpp
// CANopen HVAC Controller Node using CANopenNode on Linux/SocketCAN
// Compile: g++ -std=c++17 -o hvac_node hvac_node.cpp -lCANopenNode -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>
#include "CANopen.h"          // CANopenNode main header
#include "OD.h"               // Generated Object Dictionary header

// ---------------------------------------------------------------------------
// Hardware abstraction: read sensors via I2C / ADC (stubs for example)
// ---------------------------------------------------------------------------
static float read_room_temperature_celsius(void) {
    // In production: read SHT31 or similar sensor over I2C
    return 21.5f + 0.1f * sinf((float)time(NULL) / 60.0f);
}

static uint16_t read_co2_ppm(void) {
    // In production: read SCD40 or MH-Z19 sensor
    return 450 + (uint16_t)(rand() % 200);
}

static void set_valve_position(uint8_t percent) {
    // In production: drive PWM or modulating actuator
    printf("[HVAC] Valve position set to %u%%\n", percent);
}

static void set_fan_speed(uint8_t percent) {
    // In production: drive VFD via 0-10 V or PWM
    printf("[HVAC] Fan speed set to %u%%\n", percent);
}

// ---------------------------------------------------------------------------
// PID controller for zone temperature regulation
// ---------------------------------------------------------------------------
typedef struct {
    float kp, ki, kd;
    float integral;
    float prev_error;
    float output_min, output_max;
} PID_t;

static void pid_init(PID_t *pid, float kp, float ki, float kd,
                     float out_min, float out_max) {
    pid->kp = kp; pid->ki = ki; pid->kd = kd;
    pid->integral = 0.0f; pid->prev_error = 0.0f;
    pid->output_min = out_min; pid->output_max = out_max;
}

static float pid_update(PID_t *pid, float setpoint, float measured, float dt) {
    float error    = setpoint - measured;
    pid->integral += error * dt;
    float derivative = (error - pid->prev_error) / dt;
    pid->prev_error  = error;

    float output = pid->kp * error
                 + pid->ki * pid->integral
                 + pid->kd * derivative;

    // Clamp output
    if (output < pid->output_min) { output = pid->output_min; pid->integral -= error * dt; }
    if (output > pid->output_max) { output = pid->output_max; pid->integral -= error * dt; }
    return output;
}

// ---------------------------------------------------------------------------
// CANopenNode application callbacks
// ---------------------------------------------------------------------------
volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig) { (void)sig; g_running = 0; }

// Called by CANopenNode when an NMT command is received
void CO_NMT_callback(CO_NMT_internalState_t state) {
    printf("[NMT] State changed to %d\n", (int)state);
}

// Called when a PDO is received (e.g., occupancy from access control node)
void CO_PDO_receive_callback(CO_t *co, uint8_t nodeId, uint16_t index,
                              uint8_t subIndex, uint32_t value) {
    (void)co; (void)nodeId;
    if (index == 0x6005 && subIndex == 0x01) {
        // Occupancy status from access control node — adjust setpoints
        if (value == 0) {
            // Room vacant: relax setpoints to save energy
            OD_set_f32(OD_ENTRY_H6001, 0x01, 18.0f, false); // economy setpoint
            printf("[HVAC] Room vacant – switching to economy setpoint 18°C\n");
        } else {
            OD_set_f32(OD_ENTRY_H6001, 0x01, 21.5f, false); // comfort setpoint
            printf("[HVAC] Room occupied – switching to comfort setpoint 21.5°C\n");
        }
    }
}

// ---------------------------------------------------------------------------
// Main application thread
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    const char *can_interface = (argc > 1) ? argv[1] : "can0";
    const uint8_t node_id     = (argc > 2) ? (uint8_t)atoi(argv[2]) : 0x05;

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    printf("[HVAC] Initialising CANopen node 0x%02X on %s\n", node_id, can_interface);

    // --- CANopenNode initialisation ---
    CO_t *CO = CO_new(NULL, NULL);
    if (!CO) { fprintf(stderr, "CO_new() failed\n"); return EXIT_FAILURE; }

    uint32_t errInfo = 0;
    CO_ReturnError_t err = CO_CANinit(CO, (void *)can_interface, 125);  // 125 kbit/s
    if (err != CO_ERROR_NO) {
        fprintf(stderr, "CO_CANinit() error %d\n", err); return EXIT_FAILURE;
    }

    err = CO_CANopenInit(CO, NULL, NULL, OD, NULL,
                         CO_CONFIG_DEFAULT, node_id, 500, 500, &errInfo);
    if (err != CO_ERROR_NO) {
        fprintf(stderr, "CO_CANopenInit() error %d errInfo 0x%08X\n", err, errInfo);
        return EXIT_FAILURE;
    }

    CO_CANsetNormalMode(CO->CANmodule);

    // --- PID for valve control ---
    PID_t valve_pid;
    pid_init(&valve_pid, 5.0f, 0.1f, 1.0f, 0.0f, 100.0f);

    const float dt = 1.0f;   // 1-second control loop

    // --- Main control loop ---
    while (g_running) {
        uint32_t timeDiff_us = (uint32_t)(dt * 1e6f);

        // Drive the CANopenNode state machine
        CO_process(CO, false, timeDiff_us, NULL);

        // Read physical sensors
        float temperature = read_room_temperature_celsius();
        uint16_t co2_ppm  = read_co2_ppm();

        // Write measured values into Object Dictionary
        OD_set_f32(OD_ENTRY_H6000, 0x01, temperature, false);
        OD_set_u16(OD_ENTRY_H6004, 0x01, co2_ppm, false);

        // Read setpoint from OD (may have been updated via SDO from BMS)
        float setpoint = 21.5f;
        OD_get_f32(OD_ENTRY_H6001, 0x01, &setpoint, false);

        // Compute valve demand via PID
        float valve_demand = pid_update(&valve_pid, setpoint, temperature, dt);
        uint8_t valve_pct  = (uint8_t)valve_demand;
        OD_set_u8(OD_ENTRY_H6002, 0x01, valve_pct, false);
        set_valve_position(valve_pct);

        // CO₂-based ventilation override: if CO₂ > 1000 ppm force fan ≥ 60 %
        uint8_t fan_pct = (uint8_t)(fabsf(valve_demand) * 0.5f);
        if (co2_ppm > 1000) fan_pct = (fan_pct < 60) ? 60 : fan_pct;
        OD_set_u8(OD_ENTRY_H6003, 0x01, fan_pct, false);
        set_fan_speed(fan_pct);

        printf("[HVAC] T=%.1f°C SP=%.1f°C Valve=%u%% Fan=%u%% CO2=%uppm\n",
               temperature, setpoint, valve_pct, fan_pct, co2_ppm);

        // Trigger synchronous TPDO transmission (sends temperature + CO2 on bus)
        CO_process_SYNC(CO, timeDiff_us, NULL);

        sleep(1);
    }

    printf("[HVAC] Shutting down...\n");
    CO_CANsetConfigurationMode(CO->CANmodule);
    CO_delete(CO);
    return EXIT_SUCCESS;
}
```

**Build notes:** Link against `CANopenNode` (compiled from source) and a SocketCAN driver adapter. The Object Dictionary file `OD.c`/`OD.h` is generated by the **CANopen Device Designer** tool or `OD_gen` from a `.eds` (Electronic Data Sheet) file.

---

### Rust Example: HVAC Sensor Publisher

Using the `socketcan` and `canopen` crates on Linux:

```rust
// hvac_sensor.rs
// Publishes temperature and CO2 readings as CANopen TPDOs via SocketCAN
//
// Cargo.toml dependencies:
// socketcan = "3"
// canopen   = "0.3"
// tokio     = { version = "1", features = ["full"] }

use std::time::{Duration, Instant};
use socketcan::{CanFrame, CanSocket, Socket, EmbeddedFrame};
use std::error::Error;

// CANopen TPDO1 base COB-ID for node 0x05: 0x180 + node_id = 0x185
const NODE_ID: u32 = 0x05;
const TPDO1_COB_ID: u32 = 0x180 + NODE_ID;   // Temperature + CO2
const TPDO2_COB_ID: u32 = 0x280 + NODE_ID;   // Fan speed + Valve position
const EMCY_COB_ID:  u32 = 0x80  + NODE_ID;   // Emergency

// ---------------------------------------------------------------------------
// Simulated sensor reads (replace with actual I2C/SPI calls in production)
// ---------------------------------------------------------------------------
fn read_temperature_celsius() -> f32 {
    // SHT31 I2C sensor: 0x6000_0001 in Object Dictionary
    21.5_f32 + (std::time::UNIX_EPOCH.elapsed().unwrap_or_default().as_secs_f32() / 60.0).sin() * 0.8
}

fn read_co2_ppm() -> u16 {
    // SCD40 sensor: 0x6004_0001
    450 + (std::time::UNIX_EPOCH.elapsed().unwrap_or_default().subsec_millis() % 300) as u16
}

// ---------------------------------------------------------------------------
// Encode IEEE 754 f32 into 4 LE bytes (CANopen REAL32 layout)
// ---------------------------------------------------------------------------
fn f32_to_le_bytes(value: f32) -> [u8; 4] {
    value.to_le_bytes()
}

// ---------------------------------------------------------------------------
// Build a CANopen TPDO1 frame: [temperature (4B)] [co2_ppm (2B)] [pad (2B)]
// Total 8 bytes — maximum CAN frame payload
// ---------------------------------------------------------------------------
fn build_tpdo1(temperature: f32, co2_ppm: u16) -> CanFrame {
    let mut data = [0u8; 8];
    let temp_bytes = f32_to_le_bytes(temperature);
    data[0..4].copy_from_slice(&temp_bytes);
    data[4] = (co2_ppm & 0xFF) as u8;
    data[5] = (co2_ppm >> 8) as u8;
    // bytes 6-7: reserved / future use

    CanFrame::new(
        socketcan::StandardId::new(TPDO1_COB_ID as u16).unwrap().into(),
        &data,
    ).expect("TPDO1 frame construction failed")
}

// ---------------------------------------------------------------------------
// Build a CANopen TPDO2 frame: [fan_speed (1B)] [valve_pos (1B)] [pad (6B)]
// ---------------------------------------------------------------------------
fn build_tpdo2(fan_speed: u8, valve_pos: u8) -> CanFrame {
    let mut data = [0u8; 8];
    data[0] = fan_speed;
    data[1] = valve_pos;

    CanFrame::new(
        socketcan::StandardId::new(TPDO2_COB_ID as u16).unwrap().into(),
        &data,
    ).expect("TPDO2 frame construction failed")
}

// ---------------------------------------------------------------------------
// Build a CANopen Emergency frame for sensor fault
//   Bytes 0-1: Error code (0x5000 = Device Hardware)
//   Byte  2:   Error register (0x04 = Communication)
//   Bytes 3-7: Manufacturer-specific diagnostic data
// ---------------------------------------------------------------------------
fn build_emergency(error_code: u16, error_register: u8) -> CanFrame {
    let mut data = [0u8; 8];
    data[0] = (error_code & 0xFF) as u8;
    data[1] = (error_code >> 8)   as u8;
    data[2] = error_register;
    // bytes 3-7: vendor diagnostic info (zero here)

    CanFrame::new(
        socketcan::StandardId::new(EMCY_COB_ID as u16).unwrap().into(),
        &data,
    ).expect("Emergency frame construction failed")
}

// ---------------------------------------------------------------------------
// CANopen Heartbeat frame: [NMT state (1B)]
//   0x05 = Operational
// ---------------------------------------------------------------------------
fn build_heartbeat() -> CanFrame {
    CanFrame::new(
        socketcan::StandardId::new((0x700 + NODE_ID) as u16).unwrap().into(),
        &[0x05_u8],  // Operational state
    ).expect("Heartbeat frame failed")
}

// ---------------------------------------------------------------------------
// Main publish loop
// ---------------------------------------------------------------------------
fn main() -> Result<(), Box<dyn Error>> {
    let interface = std::env::args().nth(1).unwrap_or_else(|| "can0".to_string());
    let socket = CanSocket::open(&interface)?;

    println!("[HVAC-Rust] Node 0x{NODE_ID:02X} publishing on {interface} @ 125 kbit/s");

    let pdo_interval    = Duration::from_millis(1000);  // 1 Hz sensor updates
    let heartbeat_interval = Duration::from_millis(500); // 2 Hz heartbeat
    let mut last_pdo   = Instant::now();
    let mut last_hb    = Instant::now();

    // Simulate a valve position driven by a simple on/off controller
    let setpoint_celsius: f32 = 21.5;
    let mut valve_pos: u8 = 0;
    let mut fan_speed: u8 = 30;

    loop {
        let now = Instant::now();

        // --- Heartbeat ---
        if now.duration_since(last_hb) >= heartbeat_interval {
            socket.write_frame(&build_heartbeat())?;
            last_hb = now;
        }

        // --- Sensor PDOs ---
        if now.duration_since(last_pdo) >= pdo_interval {
            let temperature = read_temperature_celsius();
            let co2_ppm     = read_co2_ppm();

            // Simple bang-bang valve control
            if temperature > setpoint_celsius + 0.5 {
                valve_pos = 100;
                fan_speed = 80;
            } else if temperature < setpoint_celsius - 0.5 {
                valve_pos = 0;
                fan_speed = 20;
            }

            // CO2 ventilation boost
            if co2_ppm > 1000 {
                fan_speed = fan_speed.max(60);
                eprintln!("[HVAC-Rust] CO2 alert: {co2_ppm} ppm — boosting ventilation");
            }

            // Transmit TPDO1 (temperature + CO2)
            socket.write_frame(&build_tpdo1(temperature, co2_ppm))?;

            // Transmit TPDO2 (fan + valve)
            socket.write_frame(&build_tpdo2(fan_speed, valve_pos))?;

            println!(
                "[HVAC-Rust] T={temperature:.1}°C CO2={co2_ppm}ppm Valve={valve_pos}% Fan={fan_speed}%"
            );

            last_pdo = now;
        }

        // Check for incoming frames (SDO writes from BMS, NMT commands, etc.)
        if let Ok(frame) = socket.read_frame_timeout(Duration::from_millis(10)) {
            let cob_id = frame.raw_id();
            match cob_id {
                // SDO Download (BMS writing our setpoint)
                id if id == (0x600 + NODE_ID) => {
                    let d = frame.data();
                    if d.len() >= 8 && d[1] == 0x01 && d[2] == 0x60 {
                        // Index 0x6001, sub 0x01 = temperature setpoint
                        let sp_bytes = [d[4], d[5], d[6], d[7]];
                        let new_sp = f32::from_le_bytes(sp_bytes);
                        println!("[HVAC-Rust] New setpoint received via SDO: {new_sp:.1}°C");
                    }
                }
                // NMT command addressed to our node or broadcast (0x000)
                0x000 => {
                    if frame.data().len() >= 2 {
                        let cmd     = frame.data()[0];
                        let node    = frame.data()[1];
                        if node == NODE_ID as u8 || node == 0x00 {
                            match cmd {
                                0x01 => println!("[HVAC-Rust] NMT: Start"),
                                0x02 => println!("[HVAC-Rust] NMT: Stop"),
                                0x80 => println!("[HVAC-Rust] NMT: Pre-operational"),
                                0x81 => println!("[HVAC-Rust] NMT: Reset node"),
                                0x82 => println!("[HVAC-Rust] NMT: Reset communication"),
                                _    => {}
                            }
                        }
                    }
                }
                _ => {}
            }
        }
    }
}
```

---

## Lighting Control Integration

### Lighting Concepts and Object Model

Building lighting systems combine dimmable luminaires, occupancy sensors, daylight sensors, and scene controllers. **DALI** (Digital Addressable Lighting Interface, IEC 62386) is the dominant fieldbus for individual luminaire control, but CAN/CANopen serves as the backbone interconnecting DALI segments with other building systems.

CiA 443 defines room automation including lighting. Key OD entries:

| OD Index | Sub-Index | Name | Type | Description |
|---|---|---|---|---|
| 0x6100 | 0x01 | Actual Brightness | UINT8 | 0–254 (DALI scale) |
| 0x6101 | 0x01 | Target Brightness | UINT8 | 0–254 |
| 0x6102 | 0x01 | Scene Number | UINT8 | 0=off, 1–16 = scene |
| 0x6103 | 0x01 | Occupancy Detected | BOOL | From PIR sensor |
| 0x6104 | 0x01 | Daylight Level | UINT16 | lux |
| 0x6105 | 0x01 | Fade Time | UINT8 | DALI fade time code |

### C/C++ Example: DALI-over-CAN Bridge

This controller receives lighting commands on CAN and drives a DALI bus segment.

```cpp
// dali_can_bridge.cpp
// Bridges CAN (CANopen PDO) lighting commands to DALI IEC 62386 bus
// Compile: g++ -std=c++17 -o dali_can_bridge dali_can_bridge.cpp -lpthread

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <termios.h>   // UART for DALI master IC (e.g. DALI-2 USB stick or UART bridge)

#define NODE_ID          0x07
#define RPDO1_COB_ID    (0x200 + NODE_ID)   // Receive: lighting commands from BMS
#define RPDO2_COB_ID    (0x300 + NODE_ID)   // Receive: scene activation
#define TPDO1_COB_ID    (0x180 + NODE_ID)   // Transmit: actual luminaire status
#define HEARTBEAT_COB   (0x700 + NODE_ID)
#define DALI_MAX_ADDR   63                  // DALI short addresses 0-63

// DALI command codes (IEC 62386 Part 102)
#define DALI_CMD_OFF              0x00
#define DALI_CMD_UP               0x01
#define DALI_CMD_DOWN             0x02
#define DALI_CMD_MAX_LEVEL        0x05
#define DALI_CMD_MIN_LEVEL        0x06
#define DALI_CMD_DAPC             0xFF   // Direct Arc Power Control (in DAPC byte)
#define DALI_CMD_GO_TO_SCENE      0x10   // + scene (0-15)
#define DALI_CMD_RECALL_MAX       0x05
#define DALI_CMD_QUERY_ACTUAL     0xA0   // Query actual level

// Lighting scene definitions (lux target → DALI level mapping)
typedef struct {
    const char *name;
    uint8_t     dali_level;   // 0-254
    uint8_t     fade_time;    // DALI fade time code 0-15
} LightScene_t;

static const LightScene_t scenes[] = {
    { "Off",           0,   1  },   // Scene 0
    { "Presence",    200,   3  },   // Scene 1 – normal occupancy
    { "Presentation",120,   5  },   // Scene 2 – meeting with projector
    { "Cleaning",    254,   2  },   // Scene 3 – full brightness
    { "Night",        20,   7  },   // Scene 4 – minimal for security
};
static const uint8_t NUM_SCENES = (uint8_t)(sizeof(scenes) / sizeof(scenes[0]));

// Runtime state per DALI address
static uint8_t dali_actual_level[DALI_MAX_ADDR + 1] = {0};

// CAN socket file descriptor
static int can_fd = -1;
// DALI UART file descriptor (to DALI master IC)
static int dali_uart_fd = -1;

volatile sig_atomic_t g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

// ---------------------------------------------------------------------------
// DALI transmission via UART to DALI master IC
// Frame format (proprietary bridge): [0xAA][address_byte][command_byte][0x55]
// ---------------------------------------------------------------------------
static void dali_send(uint8_t address_byte, uint8_t command_byte) {
    if (dali_uart_fd < 0) {
        // Simulation mode: just print
        printf("[DALI] TX: addr=0x%02X cmd=0x%02X\n", address_byte, command_byte);
        return;
    }
    uint8_t frame[4] = { 0xAA, address_byte, command_byte, 0x55 };
    write(dali_uart_fd, frame, sizeof(frame));
    tcdrain(dali_uart_fd);
    usleep(2500);  // DALI settling time ≥ 2.4 ms
}

// DALI Direct Arc Power Control: set luminaire at short_addr to level (0-254)
static void dali_set_level(uint8_t short_addr, uint8_t level) {
    // DAPC command: address byte = (short_addr << 1) | 0 (DAPC bit = 0)
    uint8_t addr_byte = (uint8_t)((short_addr & 0x3F) << 1);
    dali_send(addr_byte, level);
    dali_actual_level[short_addr] = level;
}

// Activate a DALI scene on all luminaires (broadcast)
static void dali_activate_scene(uint8_t scene_num) {
    if (scene_num >= NUM_SCENES) return;
    printf("[DALI] Activating scene %u (%s)\n",
           scene_num, scenes[scene_num].name);

    // Send "Go to Scene" to broadcast address (0xFF = broadcast)
    uint8_t cmd = (uint8_t)(DALI_CMD_GO_TO_SCENE + (scene_num & 0x0F));
    dali_send(0xFF, cmd);   // Broadcast

    // Update local state (assume all luminaires comply)
    for (int i = 0; i <= DALI_MAX_ADDR; i++) {
        dali_actual_level[i] = scenes[scene_num].dali_level;
    }
}

// ---------------------------------------------------------------------------
// Build and transmit CANopen TPDO1 (status report)
// Payload: [avg_level (1B)][scene_active (1B)][fault_count (2B)][pad (4B)]
// ---------------------------------------------------------------------------
static void send_status_pdo(uint8_t avg_level, uint8_t scene, uint16_t faults) {
    struct can_frame frame = {0};
    frame.can_id  = TPDO1_COB_ID;
    frame.can_dlc = 8;
    frame.data[0] = avg_level;
    frame.data[1] = scene;
    frame.data[2] = (uint8_t)(faults & 0xFF);
    frame.data[3] = (uint8_t)(faults >> 8);
    write(can_fd, &frame, sizeof(frame));
}

static void send_heartbeat(void) {
    struct can_frame frame = {0};
    frame.can_id  = HEARTBEAT_COB;
    frame.can_dlc = 1;
    frame.data[0] = 0x05;  // Operational
    write(can_fd, &frame, sizeof(frame));
}

// ---------------------------------------------------------------------------
// Open SocketCAN interface
// ---------------------------------------------------------------------------
static int open_can(const char *iface) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return -1; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {0};
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    // Only receive PDOs and NMT targeted at us
    struct can_filter rfilter[] = {
        { RPDO1_COB_ID, CAN_SFF_MASK },
        { RPDO2_COB_ID, CAN_SFF_MASK },
        { 0x000,        CAN_SFF_MASK },   // NMT broadcast
    };
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, rfilter, sizeof(rfilter));
    return fd;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);
    const char *can_iface = (argc > 1) ? argv[1] : "can0";

    can_fd = open_can(can_iface);
    if (can_fd < 0) return EXIT_FAILURE;

    printf("[Light] DALI-CAN Bridge node 0x%02X on %s\n", NODE_ID, can_iface);

    // Initialise: all lights off
    dali_activate_scene(0);

    uint8_t current_scene    = 0;
    uint16_t fault_count     = 0;
    struct timespec last_hb, now;
    clock_gettime(CLOCK_MONOTONIC, &last_hb);

    while (g_running) {
        struct can_frame rx;
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(can_fd, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };  // 100 ms timeout

        if (select(can_fd + 1, &rfds, NULL, NULL, &tv) > 0) {
            if (read(can_fd, &rx, sizeof(rx)) > 0) {
                uint32_t cob = rx.can_id & CAN_SFF_MASK;

                if (cob == RPDO1_COB_ID && rx.can_dlc >= 2) {
                    // Byte 0: target brightness (0-254), Byte 1: DALI address (0-63, 0xFF=broadcast)
                    uint8_t target  = rx.data[0];
                    uint8_t dali_addr = rx.data[1];
                    printf("[Light] RPDO1: Set addr %u → level %u\n", dali_addr, target);
                    if (dali_addr == 0xFF) {
                        for (int i = 0; i <= DALI_MAX_ADDR; i++) dali_set_level(i, target);
                    } else if (dali_addr <= DALI_MAX_ADDR) {
                        dali_set_level(dali_addr, target);
                    }
                    current_scene = 0xFF;  // Manual override, no scene active
                }

                if (cob == RPDO2_COB_ID && rx.can_dlc >= 1) {
                    // Byte 0: scene number
                    uint8_t scene = rx.data[0];
                    dali_activate_scene(scene);
                    current_scene = scene;
                }

                if (cob == 0x000 && rx.can_dlc >= 2) {
                    uint8_t cmd  = rx.data[0];
                    uint8_t node = rx.data[1];
                    if (node == NODE_ID || node == 0x00) {
                        if (cmd == 0x81 || cmd == 0x82) {  // Reset
                            dali_activate_scene(0); current_scene = 0;
                            fault_count = 0;
                            printf("[Light] NMT Reset: lights off\n");
                        }
                    }
                }
            }
        }

        // Heartbeat every 500 ms
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - last_hb.tv_sec) * 1000
                        + (now.tv_nsec - last_hb.tv_nsec) / 1000000;
        if (elapsed_ms >= 500) {
            send_heartbeat();
            // Compute average DALI level across all addresses
            uint32_t sum = 0;
            for (int i = 0; i <= DALI_MAX_ADDR; i++) sum += dali_actual_level[i];
            uint8_t avg = (uint8_t)(sum / (DALI_MAX_ADDR + 1));
            send_status_pdo(avg, current_scene, fault_count);
            last_hb = now;
        }
    }

    printf("[Light] Shutting down — lights off\n");
    dali_activate_scene(0);
    close(can_fd);
    return EXIT_SUCCESS;
}
```

---

### Rust Example: Scene-Based Lighting Controller

```rust
// lighting_scene_controller.rs
// Listens on CAN for occupancy events and daylight readings,
// then selects and broadcasts the appropriate lighting scene.
//
// Cargo.toml:
// socketcan = "3"
// tokio     = { version = "1", features = ["full", "time"] }

use socketcan::{CanFrame, CanSocket, Socket, EmbeddedFrame};
use std::error::Error;
use std::time::{Duration, Instant};

const NODE_ID: u32 = 0x08;
// Receive occupancy from access control node (0x04)
const OCCUPANCY_COB: u32 = 0x180 + 0x04;
// Receive daylight sensor readings from sensor node (0x06)
const DAYLIGHT_COB:  u32 = 0x180 + 0x06;
// Transmit scene selection to DALI bridge node (0x07)
const LIGHTING_RPDO2_COB: u32 = 0x300 + 0x07;
// Heartbeat
const HEARTBEAT_COB: u32 = 0x700 + NODE_ID;

#[derive(Debug, Clone, Copy, PartialEq)]
enum Scene {
    Off         = 0,
    Presence    = 1,
    Presentation= 2,
    Cleaning    = 3,
    Night       = 4,
}

impl Scene {
    fn name(&self) -> &'static str {
        match self {
            Scene::Off          => "Off",
            Scene::Presence     => "Presence",
            Scene::Presentation => "Presentation",
            Scene::Cleaning     => "Cleaning",
            Scene::Night        => "Night",
        }
    }
}

#[derive(Debug, Default)]
struct RoomState {
    occupied: bool,
    daylight_lux: u16,
    manual_override: bool,
    manual_scene: Scene,
}

impl Default for Scene {
    fn default() -> Self { Scene::Off }
}

// Select scene based on occupancy and daylight
fn select_scene(state: &RoomState, time_hour: u8) -> Scene {
    if state.manual_override {
        return state.manual_scene;
    }
    if !state.occupied {
        // No occupancy: night security lighting only between 20:00-06:00
        if time_hour >= 20 || time_hour < 6 {
            return Scene::Night;
        }
        return Scene::Off;
    }
    // Occupied: use daylight-linked control
    if state.daylight_lux > 800 {
        // Ample daylight: minimal artificial light
        return Scene::Night;  // repurposed as "daylight save" level
    }
    Scene::Presence
}

fn build_scene_frame(scene: Scene) -> CanFrame {
    CanFrame::new(
        socketcan::StandardId::new(LIGHTING_RPDO2_COB as u16).unwrap().into(),
        &[scene as u8],
    ).expect("scene frame")
}

fn build_heartbeat() -> CanFrame {
    CanFrame::new(
        socketcan::StandardId::new(HEARTBEAT_COB as u16).unwrap().into(),
        &[0x05],
    ).expect("heartbeat frame")
}

fn main() -> Result<(), Box<dyn Error>> {
    let iface = std::env::args().nth(1).unwrap_or_else(|| "can0".to_string());
    let socket = CanSocket::open(&iface)?;

    println!("[Lighting-Rust] Scene controller node 0x{NODE_ID:02X} on {iface}");

    let mut state = RoomState::default();
    let mut current_scene = Scene::Off;
    let mut last_hb = Instant::now();
    let mut last_scene_tx = Instant::now();

    loop {
        // Heartbeat every 500 ms
        if last_hb.elapsed() >= Duration::from_millis(500) {
            socket.write_frame(&build_heartbeat())?;
            last_hb = Instant::now();
        }

        // Evaluate scene every 5 seconds (or on state change)
        let now_hour = {
            // Simplified: use seconds-mod-86400 / 3600 as hour
            let secs = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)?.as_secs();
            ((secs % 86400) / 3600) as u8
        };

        if last_scene_tx.elapsed() >= Duration::from_secs(5) {
            let desired = select_scene(&state, now_hour);
            if desired != current_scene {
                println!("[Lighting-Rust] Scene change: {} → {}", current_scene.name(), desired.name());
                socket.write_frame(&build_scene_frame(desired))?;
                current_scene = desired;
            }
            last_scene_tx = Instant::now();
        }

        // Read incoming CAN frames
        match socket.read_frame_timeout(Duration::from_millis(50)) {
            Ok(frame) => {
                let cob = frame.raw_id() & 0x7FF;
                let data = frame.data();

                match cob {
                    id if id == OCCUPANCY_COB && !data.is_empty() => {
                        // TPDO from access control node:
                        // byte 5 = occupancy (0=vacant, 1=occupied)
                        if data.len() > 5 {
                            let occ = data[5] != 0;
                            if occ != state.occupied {
                                println!("[Lighting-Rust] Occupancy: {}", if occ { "OCCUPIED" } else { "VACANT" });
                                state.occupied = occ;
                                // Force immediate scene evaluation
                                let desired = select_scene(&state, now_hour);
                                if desired != current_scene {
                                    socket.write_frame(&build_scene_frame(desired))?;
                                    current_scene = desired;
                                }
                            }
                        }
                    }

                    id if id == DAYLIGHT_COB && data.len() >= 2 => {
                        // Daylight sensor TPDO: bytes 0-1 = lux (u16 LE)
                        let lux = u16::from_le_bytes([data[0], data[1]]);
                        state.daylight_lux = lux;
                        println!("[Lighting-Rust] Daylight: {lux} lux");
                    }

                    0x000 if data.len() >= 2 => {
                        // NMT command
                        let cmd  = data[0];
                        let node = data[1];
                        if node == NODE_ID as u8 || node == 0 {
                            if cmd == 0x81 || cmd == 0x82 {
                                println!("[Lighting-Rust] NMT Reset");
                                state = RoomState::default();
                                socket.write_frame(&build_scene_frame(Scene::Off))?;
                                current_scene = Scene::Off;
                            }
                        }
                    }

                    _ => {}
                }
            }
            Err(_) => {} // Timeout — normal
        }
    }
}
```

---

## Access Control Integration

### Access Control Concepts and Object Model

Access control nodes manage door locks, card readers (Wiegand/OSDP), keypads, REX (request-to-exit) sensors, and door position switches. On CAN/CANopen these nodes publish events and receive lock/unlock commands.

| OD Index | Sub-Index | Name | Type | Description |
|---|---|---|---|---|
| 0x6200 | 0x01 | Door Lock State | UINT8 | 0=locked, 1=unlocked |
| 0x6200 | 0x02 | Door Position | UINT8 | 0=closed, 1=open, 2=forced |
| 0x6201 | 0x01 | Card Credential | UINT64 | Last presented card UID |
| 0x6201 | 0x02 | Access Decision | UINT8 | 0=denied, 1=granted |
| 0x6202 | 0x01 | Alarm State | UINT8 | Bitmask: forced/held/tamper |
| 0x6203 | 0x01 | Occupancy Count | INT16 | People in zone (+ = in, - = error) |

### C/C++ Example: Access Control Door Node

```cpp
// access_door_node.cpp
// CANopen access control door controller
// Handles card credentials, publishes events, drives electric strike
// Compile: g++ -std=c++17 -o access_door access_door_node.cpp -lpthread

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <signal.h>

#define NODE_ID          0x04
// TPDO1: door status and last event
#define TPDO1_COB       (0x180 + NODE_ID)
// TPDO2: occupancy count (zone people counter)
#define TPDO2_COB       (0x280 + NODE_ID)
// RPDO1: lock/unlock commands from BMS
#define RPDO1_COB       (0x200 + NODE_ID)
#define HEARTBEAT_COB   (0x700 + NODE_ID)
#define EMCY_COB        (0x080 + NODE_ID)

// Access control events
#define EVT_ACCESS_GRANTED     0x01
#define EVT_ACCESS_DENIED      0x02
#define EVT_DOOR_FORCED        0x03
#define EVT_DOOR_HELD          0x04
#define EVT_DOOR_CLOSED        0x05
#define EVT_TAMPER             0x06
#define EVT_REX                0x07

// Credential whitelist (in production: query OSDP panel or internal flash)
static const uint64_t WHITELIST[] = {
    0xA1B2C3D4E5F60001ULL,   // Badge 1
    0xA1B2C3D4E5F60002ULL,   // Badge 2
    0xA1B2C3D4E5F60003ULL,   // Badge 3
};
static const size_t WHITELIST_LEN = sizeof(WHITELIST) / sizeof(WHITELIST[0]);

// Door state
typedef struct {
    uint8_t  lock_state;     // 0=locked, 1=unlocked
    uint8_t  door_position;  // 0=closed, 1=open, 2=forced
    uint8_t  alarm_mask;     // bit 0=forced, bit 1=held, bit 2=tamper
    int16_t  occupancy;      // people in zone
    uint32_t held_timer_s;   // seconds door has been open
} DoorState_t;

static DoorState_t g_door = {0};
static int can_fd = -1;
volatile sig_atomic_t g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

// ---------------------------------------------------------------------------
// Hardware abstraction stubs
// ---------------------------------------------------------------------------
static void set_electric_strike(uint8_t unlock) {
    printf("[Access] Electric strike: %s\n", unlock ? "UNLOCK" : "LOCK");
    // In production: GPIO toggle + relay driver
}

static uint8_t read_door_sensor(void) {
    // 0=closed, 1=open — read from GPIO
    return 0;
}

static uint8_t read_rex_sensor(void) {
    // Request-to-exit PIR on secure side
    return 0;
}

static uint64_t read_wiegand_credential(void) {
    // In production: read Wiegand data-0/data-1 GPIO interrupt accumulation
    // Returns 0 if no card presented, card UID otherwise
    return 0;
}

// ---------------------------------------------------------------------------
// Credential validation
// ---------------------------------------------------------------------------
static uint8_t validate_credential(uint64_t uid) {
    for (size_t i = 0; i < WHITELIST_LEN; i++) {
        if (WHITELIST[i] == uid) return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CAN frame builders
// ---------------------------------------------------------------------------
static void send_tpdo1(uint8_t event, uint64_t credential, uint8_t decision) {
    // TPDO1 layout (8 bytes):
    // [0]   door_lock_state
    // [1]   door_position + alarm_mask
    // [2]   event code
    // [3]   access decision
    // [4-7] last 4 bytes of credential UID
    struct can_frame f = {0};
    f.can_id  = TPDO1_COB;
    f.can_dlc = 8;
    f.data[0] = g_door.lock_state;
    f.data[1] = (uint8_t)(g_door.door_position | (g_door.alarm_mask << 4));
    f.data[2] = event;
    f.data[3] = decision;
    f.data[4] = (uint8_t)(credential & 0xFF);
    f.data[5] = (uint8_t)((credential >> 8)  & 0xFF);
    f.data[6] = (uint8_t)((credential >> 16) & 0xFF);
    f.data[7] = (uint8_t)((credential >> 24) & 0xFF);
    write(can_fd, &f, sizeof(f));
}

static void send_tpdo2(void) {
    // TPDO2: occupancy count (signed 16-bit LE) + alarm mask
    struct can_frame f = {0};
    f.can_id  = TPDO2_COB;
    f.can_dlc = 4;
    f.data[0] = (uint8_t)(g_door.occupancy & 0xFF);
    f.data[1] = (uint8_t)((g_door.occupancy >> 8) & 0xFF);
    f.data[2] = g_door.alarm_mask;
    f.data[3] = 0x00;
    write(can_fd, &f, sizeof(f));
}

static void send_heartbeat(void) {
    struct can_frame f = {0};
    f.can_id = HEARTBEAT_COB; f.can_dlc = 1; f.data[0] = 0x05;
    write(can_fd, &f, sizeof(f));
}

static void send_emergency(uint16_t err_code, uint8_t err_reg) {
    struct can_frame f = {0};
    f.can_id  = EMCY_COB; f.can_dlc = 8;
    f.data[0] = (uint8_t)(err_code & 0xFF);
    f.data[1] = (uint8_t)(err_code >> 8);
    f.data[2] = err_reg;
    write(can_fd, &f, sizeof(f));
    printf("[Access] EMCY: code=0x%04X reg=0x%02X\n", err_code, err_reg);
}

// ---------------------------------------------------------------------------
// Open SocketCAN
// ---------------------------------------------------------------------------
static int open_can(const char *iface) {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return -1; }
    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr = { .can_family = AF_CAN, .can_ifindex = ifr.ifr_ifindex };
    bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);
    const char *iface = argc > 1 ? argv[1] : "can0";
    can_fd = open_can(iface);
    if (can_fd < 0) return EXIT_FAILURE;

    printf("[Access] Door node 0x%02X on %s\n", NODE_ID, iface);
    g_door.lock_state = 0;  // Start locked
    set_electric_strike(0);

    struct timespec last_hb, now;
    clock_gettime(CLOCK_MONOTONIC, &last_hb);
    uint32_t cycle = 0;

    while (g_running) {
        // --- Read physical inputs ---
        uint64_t credential = read_wiegand_credential();
        uint8_t  door_open  = read_door_sensor();
        uint8_t  rex        = read_rex_sensor();

        // --- Credential processing ---
        if (credential != 0) {
            uint8_t granted = validate_credential(credential);
            if (granted) {
                printf("[Access] Access GRANTED for UID 0x%llX\n",
                       (unsigned long long)credential);
                g_door.lock_state = 1;
                set_electric_strike(1);
                g_door.occupancy++;
                send_tpdo1(EVT_ACCESS_GRANTED, credential, 1);
            } else {
                printf("[Access] Access DENIED for UID 0x%llX\n",
                       (unsigned long long)credential);
                // Emit emergency object for repeated denials
                static uint8_t deny_count = 0;
                if (++deny_count >= 3) {
                    send_emergency(0x8100, 0x10);  // 0x8100 = Monitoring, Comm err
                    deny_count = 0;
                }
                send_tpdo1(EVT_ACCESS_DENIED, credential, 0);
            }
        }

        // --- REX (exit from secure side — always grant) ---
        if (rex) {
            printf("[Access] REX activated\n");
            g_door.lock_state = 1;
            set_electric_strike(1);
            if (g_door.occupancy > 0) g_door.occupancy--;
            send_tpdo1(EVT_REX, 0, 1);
        }

        // --- Door position tracking ---
        if (door_open && g_door.door_position == 0) {
            g_door.door_position = 1;
            g_door.held_timer_s  = 0;
            send_tpdo1(EVT_ACCESS_GRANTED, 0, 1);
        }
        if (!door_open && g_door.door_position == 1) {
            g_door.door_position  = 0;
            g_door.lock_state     = 0;   // Re-lock on close
            g_door.held_timer_s   = 0;
            set_electric_strike(0);
            send_tpdo1(EVT_DOOR_CLOSED, 0, 0);
        }

        // --- Door-held alarm (> 30 seconds open) ---
        if (g_door.door_position == 1) {
            g_door.held_timer_s++;
            if (g_door.held_timer_s > 30 && !(g_door.alarm_mask & 0x02)) {
                g_door.alarm_mask |= 0x02;
                printf("[Access] ALARM: Door held open > 30s\n");
                send_tpdo1(EVT_DOOR_HELD, 0, 0);
            }
        } else {
            g_door.alarm_mask &= (uint8_t)~0x02;
        }

        // --- Process incoming CAN frames (RPDO / NMT) ---
        struct can_frame rx;
        while (read(can_fd, &rx, sizeof(rx)) > 0) {
            uint32_t cob = rx.can_id & CAN_SFF_MASK;
            if (cob == RPDO1_COB && rx.can_dlc >= 1) {
                // Byte 0: 0x00=lock, 0x01=unlock, 0x02=momentary unlock
                if (rx.data[0] == 0x01) {
                    g_door.lock_state = 1; set_electric_strike(1);
                    printf("[Access] Remote UNLOCK command\n");
                } else if (rx.data[0] == 0x00) {
                    g_door.lock_state = 0; set_electric_strike(0);
                    printf("[Access] Remote LOCK command\n");
                }
            }
        }

        // --- Periodic TPDO2 (occupancy) every 10 cycles ---
        if (cycle % 10 == 0) send_tpdo2();

        // --- Heartbeat every 500 ms ---
        clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (now.tv_sec - last_hb.tv_sec) * 1000
                + (now.tv_nsec - last_hb.tv_nsec) / 1000000;
        if (ms >= 500) { send_heartbeat(); last_hb = now; }

        cycle++;
        usleep(100000);  // 100 ms control loop
    }

    // Ensure door is locked on shutdown
    g_door.lock_state = 0;
    set_electric_strike(0);
    close(can_fd);
    return EXIT_SUCCESS;
}
```

---

### Rust Example: Access Event Logger

```rust
// access_event_logger.rs
// Subscribes to all access control TPDOs on the CAN bus and logs events
// with timestamps to a structured audit trail.
//
// Cargo.toml:
// socketcan  = "3"
// chrono     = { version = "0.4", features = ["serde"] }
// serde      = { version = "1", features = ["derive"] }
// serde_json = "1"

use socketcan::{CanFrame, CanSocket, Socket, EmbeddedFrame};
use std::error::Error;
use std::fs::OpenOptions;
use std::io::Write;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

// Watch TPDOs from all potential access control nodes (0x04, 0x09, 0x0A, 0x0B)
const ACCESS_NODE_IDS: &[u32] = &[0x04, 0x09, 0x0A, 0x0B];

#[derive(Debug)]
struct AccessEvent {
    timestamp_unix: u64,
    node_id: u8,
    lock_state: u8,
    door_position: u8,
    alarm_mask: u8,
    event_code: u8,
    decision: u8,
    credential_partial: u32,
    occupancy: i16,
}

impl AccessEvent {
    fn event_name(&self) -> &'static str {
        match self.event_code {
            0x01 => "ACCESS_GRANTED",
            0x02 => "ACCESS_DENIED",
            0x03 => "DOOR_FORCED",
            0x04 => "DOOR_HELD",
            0x05 => "DOOR_CLOSED",
            0x06 => "TAMPER",
            0x07 => "REX",
            _    => "UNKNOWN",
        }
    }

    fn to_json(&self) -> String {
        format!(
            r#"{{"ts":{},"node":"0x{:02X}","event":"{}","lock":{},"door":{},"alarm":"0x{:02X}","decision":{},"cred_partial":"0x{:08X}","occupancy":{}}}"#,
            self.timestamp_unix,
            self.node_id,
            self.event_name(),
            self.lock_state,
            self.door_position,
            self.alarm_mask,
            self.decision,
            self.credential_partial,
            self.occupancy,
        )
    }
}

fn parse_tpdo1(node_id: u8, data: &[u8]) -> Option<AccessEvent> {
    if data.len() < 8 { return None; }
    Some(AccessEvent {
        timestamp_unix: SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs(),
        node_id,
        lock_state: data[0],
        door_position: data[1] & 0x0F,
        alarm_mask: (data[1] >> 4) & 0x0F,
        event_code: data[2],
        decision: data[3],
        credential_partial: u32::from_le_bytes([data[4], data[5], data[6], data[7]]),
        occupancy: 0,  // populated from TPDO2
    })
}

fn parse_tpdo2_occupancy(data: &[u8]) -> i16 {
    if data.len() < 2 { return 0; }
    i16::from_le_bytes([data[0], data[1]])
}

fn main() -> Result<(), Box<dyn Error>> {
    let iface = std::env::args().nth(1).unwrap_or_else(|| "can0".to_string());
    let log_path = std::env::args().nth(2).unwrap_or_else(|| "access_audit.jsonl".to_string());

    let socket = CanSocket::open(&iface)?;
    let mut log_file = OpenOptions::new().create(true).append(true).open(&log_path)?;

    println!("[Logger] Listening for access events on {iface}");
    println!("[Logger] Writing audit log to {log_path}");

    let mut occupancy_cache = std::collections::HashMap::<u8, i16>::new();

    loop {
        match socket.read_frame_timeout(Duration::from_millis(1000)) {
            Ok(frame) => {
                let cob = frame.raw_id() & 0x7FF;
                let data = frame.data();

                // Check if this is a TPDO from a known access control node
                for &node_id in ACCESS_NODE_IDS {
                    let tpdo1_cob = 0x180 + node_id;
                    let tpdo2_cob = 0x280 + node_id;

                    if cob == tpdo1_cob {
                        if let Some(mut event) = parse_tpdo1(node_id as u8, data) {
                            // Attach latest occupancy for this node
                            event.occupancy = *occupancy_cache.get(&(node_id as u8)).unwrap_or(&0);

                            let json = event.to_json();
                            println!("[Logger] {json}");
                            writeln!(log_file, "{json}")?;
                            log_file.flush()?;
                        }
                    }

                    if cob == tpdo2_cob {
                        let occ = parse_tpdo2_occupancy(data);
                        occupancy_cache.insert(node_id as u8, occ);
                        println!("[Logger] Node 0x{node_id:02X} occupancy: {occ}");
                    }
                }

                // Log emergency frames from any node
                if (cob & 0x780) == 0x080 {
                    let emitting_node = (cob & 0x07F) as u8;
                    if data.len() >= 3 {
                        let err_code = u16::from_le_bytes([data[0], data[1]]);
                        let err_reg  = data[2];
                        let ts = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();
                        let json = format!(
                            r#"{{"ts":{ts},"node":"0x{emitting_node:02X}","event":"EMERGENCY","err_code":"0x{err_code:04X}","err_reg":"0x{err_reg:02X}"}}"#
                        );
                        eprintln!("[Logger] EMERGENCY: {json}");
                        writeln!(log_file, "{json}")?;
                        log_file.flush()?;
                    }
                }
            }
            Err(_) => {
                // Timeout — print a periodic status
                print!(".");
                std::io::stdout().flush().ok();
            }
        }
    }
}
```

---

## Inter-System Integration: HVAC + Access Control

### C/C++ Example: Occupancy-Driven HVAC

This gateway node subscribes to access control occupancy events and updates HVAC setpoints, demonstrating the cross-system orchestration that CAN enables over a single cable pair.

```cpp
// occupancy_hvac_gateway.cpp
// Listens to access control TPDO2 (occupancy), writes HVAC setpoints via SDO
// Compile: g++ -std=c++17 -o occ_hvac_gw occupancy_hvac_gateway.cpp -lpthread

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <signal.h>
#include <time.h>

#define GW_NODE_ID       0x01       // Gateway = NMT master
#define HVAC_NODE_ID     0x05
#define ACCESS_NODE_ID   0x04

// SDO to HVAC node
#define SDO_TX_COB      (0x600 + HVAC_NODE_ID)   // Gateway → HVAC (SDO request)
#define SDO_RX_COB      (0x580 + HVAC_NODE_ID)   // HVAC → Gateway (SDO response)

// Access control occupancy TPDO2
#define ACCESS_TPDO2    (0x280 + ACCESS_NODE_ID)

// HVAC Object Dictionary indices
#define OD_TEMP_SETPOINT_IDX  0x6001
#define OD_TEMP_SETPOINT_SUB  0x01
#define OD_OCCUPANCY_IDX      0x6005
#define OD_OCCUPANCY_SUB      0x01

// Setpoints
#define SETPOINT_COMFORT_C   21.5f
#define SETPOINT_ECONOMY_C   18.0f
#define SETPOINT_STANDBY_C   15.0f

static int can_fd = -1;
volatile sig_atomic_t g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

// ---------------------------------------------------------------------------
// Build CANopen SDO Download (write) request frame
// Expedited transfer for 4-byte REAL32 value
// CCS = 0b001 (Initiate Download), n=0, e=1 (expedited), s=1 (size indicated)
// Command byte = 0x23 for 4-byte expedited download
// ---------------------------------------------------------------------------
static struct can_frame build_sdo_write_f32(uint16_t idx, uint8_t sub, float value) {
    struct can_frame f = {0};
    f.can_id  = SDO_TX_COB;
    f.can_dlc = 8;
    f.data[0] = 0x23;                     // Download 4 bytes, expedited
    f.data[1] = (uint8_t)(idx & 0xFF);
    f.data[2] = (uint8_t)(idx >> 8);
    f.data[3] = sub;
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));     // IEEE 754 bit pattern
    f.data[4] = (uint8_t)(raw & 0xFF);
    f.data[5] = (uint8_t)((raw >> 8)  & 0xFF);
    f.data[6] = (uint8_t)((raw >> 16) & 0xFF);
    f.data[7] = (uint8_t)((raw >> 24) & 0xFF);
    return f;
}

static struct can_frame build_sdo_write_u8(uint16_t idx, uint8_t sub, uint8_t value) {
    struct can_frame f = {0};
    f.can_id  = SDO_TX_COB;
    f.can_dlc = 8;
    f.data[0] = 0x2F;  // Download 1 byte, expedited
    f.data[1] = (uint8_t)(idx & 0xFF);
    f.data[2] = (uint8_t)(idx >> 8);
    f.data[3] = sub;
    f.data[4] = value;
    return f;
}

static void sdo_write_hvac_setpoint(float celsius) {
    struct can_frame f = build_sdo_write_f32(OD_TEMP_SETPOINT_IDX,
                                              OD_TEMP_SETPOINT_SUB, celsius);
    write(can_fd, &f, sizeof(f));
    printf("[GW] SDO write HVAC setpoint → %.1f°C\n", celsius);

    // Wait for SDO response (0x60 = success)
    struct can_frame rx;
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    fd_set rfds; FD_ZERO(&rfds); FD_SET(can_fd, &rfds);
    if (select(can_fd + 1, &rfds, NULL, NULL, &tv) > 0) {
        if (read(can_fd, &rx, sizeof(rx)) > 0) {
            if ((rx.can_id & CAN_SFF_MASK) == SDO_RX_COB) {
                if (rx.data[0] == 0x60) printf("[GW] SDO write confirmed\n");
                else printf("[GW] SDO error: 0x%02X\n", rx.data[0]);
            }
        }
    } else {
        printf("[GW] SDO timeout — HVAC node not responding\n");
    }
}

static void sdo_write_hvac_occupancy(uint8_t occupied) {
    struct can_frame f = build_sdo_write_u8(OD_OCCUPANCY_IDX,
                                             OD_OCCUPANCY_SUB, occupied);
    write(can_fd, &f, sizeof(f));
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);
    const char *iface = argc > 1 ? argv[1] : "can0";

    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(can_fd, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr = { .can_family = AF_CAN,
                                  .can_ifindex = ifr.ifr_ifindex };
    bind(can_fd, (struct sockaddr *)&addr, sizeof(addr));

    printf("[GW] Occupancy-HVAC gateway on %s\n", iface);

    // Initial state: economy setpoint
    sdo_write_hvac_setpoint(SETPOINT_ECONOMY_C);
    sdo_write_hvac_occupancy(0);

    int16_t prev_occupancy = 0;

    while (g_running) {
        struct can_frame rx;
        fd_set rfds; FD_ZERO(&rfds); FD_SET(can_fd, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };

        if (select(can_fd + 1, &rfds, NULL, NULL, &tv) > 0) {
            if (read(can_fd, &rx, sizeof(rx)) > 0) {
                uint32_t cob = rx.can_id & CAN_SFF_MASK;

                if (cob == ACCESS_TPDO2 && rx.can_dlc >= 2) {
                    int16_t occupancy = (int16_t)((uint16_t)(rx.data[1] << 8) | rx.data[0]);
                    uint8_t alarm     = (rx.can_dlc >= 3) ? rx.data[2] : 0;

                    printf("[GW] Access TPDO2: occupancy=%d alarm=0x%02X\n", occupancy, alarm);

                    // Only act on occupancy transitions
                    int was_occupied = (prev_occupancy > 0);
                    int is_occupied  = (occupancy > 0);

                    if (is_occupied && !was_occupied) {
                        printf("[GW] → Zone now OCCUPIED: comfort setpoint\n");
                        sdo_write_hvac_setpoint(SETPOINT_COMFORT_C);
                        sdo_write_hvac_occupancy(1);
                    } else if (!is_occupied && was_occupied) {
                        printf("[GW] → Zone now VACANT: economy setpoint\n");
                        sdo_write_hvac_setpoint(SETPOINT_ECONOMY_C);
                        sdo_write_hvac_occupancy(0);
                    }

                    prev_occupancy = occupancy;
                }
            }
        }
    }

    printf("[GW] Shutdown — standby setpoint\n");
    sdo_write_hvac_setpoint(SETPOINT_STANDBY_C);
    close(can_fd);
    return EXIT_SUCCESS;
}
```

---

## Error Handling and Diagnostics

### C/C++ Example: Bus Diagnostic Monitor

```cpp
// can_bus_monitor.cpp
// Monitors CANopen heartbeats, detects missing nodes, logs bus errors
// Compile: g++ -std=c++17 -o can_monitor can_bus_monitor.cpp -lpthread

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <signal.h>

#define MAX_NODES 128

typedef struct {
    uint8_t  node_id;
    uint8_t  nmt_state;        // Last reported NMT state
    uint64_t last_heartbeat_ms;
    uint32_t heartbeat_timeout_ms;
    uint32_t missed_heartbeats;
    uint32_t emergency_count;
} NodeMonitor_t;

static NodeMonitor_t g_nodes[MAX_NODES] = {0};
static int can_fd = -1;
volatile sig_atomic_t g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}

static const char *nmt_state_name(uint8_t state) {
    switch (state) {
        case 0x00: return "Bootup";
        case 0x04: return "Stopped";
        case 0x05: return "Operational";
        case 0x7F: return "Pre-operational";
        default:   return "Unknown";
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sig_handler);
    const char *iface = argc > 1 ? argv[1] : "can0";

    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    // Enable error frame reception
    can_err_mask_t err_mask = CAN_ERR_TX_TIMEOUT | CAN_ERR_BUSOFF
                            | CAN_ERR_BUSERROR  | CAN_ERR_LOSTARB
                            | CAN_ERR_PROT;
    setsockopt(can_fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(can_fd, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr = { .can_family = AF_CAN,
                                  .can_ifindex = ifr.ifr_ifindex };
    bind(can_fd, (struct sockaddr *)&addr, sizeof(addr));

    printf("[Monitor] CAN bus diagnostic on %s\n", iface);

    // Register known nodes with their heartbeat periods
    uint8_t monitored[] = { 0x04, 0x05, 0x07, 0x08 };
    for (size_t i = 0; i < sizeof(monitored); i++) {
        uint8_t n = monitored[i];
        g_nodes[n].node_id = n;
        g_nodes[n].heartbeat_timeout_ms = 1500;  // 3× 500 ms heartbeat
        g_nodes[n].last_heartbeat_ms = now_ms();
    }

    uint64_t last_check = now_ms();

    while (g_running) {
        struct can_frame rx;
        fd_set rfds; FD_ZERO(&rfds); FD_SET(can_fd, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };

        if (select(can_fd + 1, &rfds, NULL, NULL, &tv) > 0) {
            if (read(can_fd, &rx, sizeof(rx)) > 0) {
                uint32_t cob = rx.can_id & CAN_EFF_MASK;

                // Heartbeat: COB-ID = 0x700 + node_id
                if ((cob & 0x780) == 0x700 && !(rx.can_id & CAN_ERR_FLAG)) {
                    uint8_t nid = (uint8_t)(cob & 0x7F);
                    if (nid > 0 && nid < MAX_NODES) {
                        NodeMonitor_t *nd = &g_nodes[nid];
                        nd->node_id = nid;
                        nd->last_heartbeat_ms = now_ms();
                        uint8_t prev_state = nd->nmt_state;
                        nd->nmt_state = rx.data[0];
                        if (nd->missed_heartbeats > 0) {
                            printf("[Monitor] Node 0x%02X back online (%s)\n",
                                   nid, nmt_state_name(nd->nmt_state));
                        }
                        if (nd->nmt_state != prev_state) {
                            printf("[Monitor] Node 0x%02X NMT: %s\n",
                                   nid, nmt_state_name(nd->nmt_state));
                        }
                        nd->missed_heartbeats = 0;
                    }
                }

                // Emergency object: COB-ID = 0x080 + node_id
                if ((cob & 0x780) == 0x080 && !(rx.can_id & CAN_ERR_FLAG)) {
                    uint8_t nid = (uint8_t)(cob & 0x7F);
                    if (nid < MAX_NODES) {
                        g_nodes[nid].emergency_count++;
                        uint16_t err_code = (uint16_t)((rx.data[1] << 8) | rx.data[0]);
                        printf("[Monitor] EMCY from 0x%02X: code=0x%04X reg=0x%02X total=%u\n",
                               nid, err_code, rx.data[2], g_nodes[nid].emergency_count);
                    }
                }

                // CAN bus error frame
                if (rx.can_id & CAN_ERR_FLAG) {
                    if (rx.can_id & CAN_ERR_BUSOFF)
                        printf("[Monitor] BUS-OFF condition detected!\n");
                    if (rx.can_id & CAN_ERR_LOSTARB)
                        printf("[Monitor] Arbitration lost (bit %u)\n", rx.data[0]);
                    if (rx.can_id & CAN_ERR_PROT)
                        printf("[Monitor] Protocol error: type=0x%02X loc=0x%02X\n",
                               rx.data[2], rx.data[3]);
                }
            }
        }

        // Check for timed-out heartbeats every second
        uint64_t ms = now_ms();
        if (ms - last_check >= 1000) {
            for (int n = 1; n < MAX_NODES; n++) {
                NodeMonitor_t *nd = &g_nodes[n];
                if (nd->node_id == 0 || nd->heartbeat_timeout_ms == 0) continue;
                if (ms - nd->last_heartbeat_ms > nd->heartbeat_timeout_ms) {
                    nd->missed_heartbeats++;
                    printf("[Monitor] Node 0x%02X HEARTBEAT TIMEOUT (missed %u)\n",
                           n, nd->missed_heartbeats);
                    if (nd->missed_heartbeats >= 3) {
                        printf("[Monitor] Node 0x%02X presumed OFFLINE\n", n);
                    }
                }
            }
            last_check = ms;
        }
    }

    close(can_fd);
    return EXIT_SUCCESS;
}
```

---

## Security Considerations

Standard CAN bus has no built-in authentication or encryption—every node on a segment can read all frames and, unless filtered by hardware, inject frames. In building automation this is mitigated by several means:

**Physical security:** The CAN cable is routed inside conduit or within the building structure, reducing the attack surface compared to wireless protocols. Patch panels and DIN-rail controllers are housed in locked electrical enclosures.

**Network segmentation:** Separate CAN segments for HVAC, lighting, and access control are connected only through gateway nodes. The gateway enforces a whitelist of allowed COB-IDs crossing segment boundaries.

**CANopen Application-Layer Authentication (experimental, CiA 452):** This draft specification defines HMAC-based message authentication appended to SDO transactions, protecting configuration writes from unauthorised sources.

**Gateway-enforced message filtering:** The IP/CAN gateway (supervisory interface) accepts only SDO writes from authenticated SCADA sessions; raw CAN injection from the IP side is blocked.

**Firmware signing:** Node firmware updates delivered via CAN (via CANopen's LSS/firmware download profile) must carry a valid signature verified before flashing.

**Access control node hardening:** Badge reader nodes should be located entirely on the secure side of the door or within tamper-evident enclosures monitored by the tamper alarm bit in their TPDO.

---

## Summary

CAN bus, through the CANopen application layer, provides a robust, cost-effective, and deterministic fieldbus infrastructure for modern building automation. The key takeaways from this document are:

**Protocol Choice:** CANopen (CiA 443, CiA 401, CiA 404) is the standard of choice, providing a well-specified Object Dictionary, standardised PDO/SDO communication patterns, and NMT-based node lifecycle management. BACnet/IP or Modbus TCP at the supervisory layer connects to CAN via dedicated gateway nodes.

**HVAC Integration:** Temperature, CO₂, humidity, valve position, and fan speed are mapped into the CANopen Object Dictionary. PID control loops run locally on the node, with setpoints updated over SDO by the BMS. Occupancy signals from access control nodes directly influence economy vs. comfort setpoints—without passing through the supervisory layer—enabling fast, reliable energy optimisation.

**Lighting Control:** DALI luminaires are aggregated behind DALI-to-CAN bridge nodes. Scene commands arrive via RPDO and are translated into DALI broadcast commands. Daylight-linked and occupancy-linked scene selection is implemented in a dedicated Rust scene controller that subscribes to data from both lighting and access control segments.

**Access Control:** Door nodes publish credential events, door position, alarm states, and occupancy counts as TPDOs. Lock/unlock commands are received as RPDOs. Emergency objects signal repeated access denials or tamper events. The Rust event logger demonstrates how to aggregate these events into a structured audit trail.

**Inter-System Integration:** The occupancy-driven HVAC gateway example shows how a single CAN segment unifying access control and HVAC data eliminates the need for a centralised server to mediate simple occupancy-to-setpoint logic—improving response latency and resilience.

**Error Handling:** The CANopen Emergency object (EMCY) provides a standardised, priority-elevated fault notification mechanism. The bus diagnostic monitor demonstrates how to supervise heartbeats from all nodes, detect offline nodes, and log bus-level errors (bus-off, arbitration loss, protocol violations) via the Linux SocketCAN error frame interface.

**Security:** CAN lacks inherent authentication; mitigation relies on physical security, CAN segment isolation at gateways, application-layer authentication drafts (CiA 452), and firmware signing. Access control nodes should always reside on the secure side of the controlled boundary.

Together, these building blocks—CAN physical layer, CANopen application layer, standardised device profiles, and cross-system gateway orchestration—form a reliable, maintainable, and energy-efficient automation backbone suitable for commercial buildings, hospitals, data centres, and industrial facilities.

---

*Document: 79 – CAN in Building Automation | Protocols: CANopen EN 50325-4, CiA 401/404/443, BACnet/CAN | Languages: C/C++17, Rust (socketcan, canopen crates) | Physical Layer: ISO 11898-2 (CAN HS)*