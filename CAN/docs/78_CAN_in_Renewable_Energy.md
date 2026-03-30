# 78. CAN in Renewable Energy

**Architecture & Theory** — Why CAN suits renewable energy (EMI immunity, multi-master, prioritised arbitration), physical layer requirements (termination, galvanic isolation), and ID assignment strategies for both solar and wind systems.

**Per-Technology Deep Dives:**
- *Solar Inverters* — TPDO/RPDO mapping, power curtailment, MPPT telemetry, status flags
- *Wind Turbines* — Pitch controller hierarchy, emergency feather priority, generator/yaw coordination
- *BMS* — Pack state, cell limits, contactor control, SoC/SoH reporting, PCS power setpoints

**C/C++ Examples (6 listings):**
1. SocketCAN socket initialisation
2. Solar power curtailment command sender
3. Solar TPDO1 telemetry parser
4. Wind turbine emergency pitch-to-feather (extended frames)
5. Full BMS status decoder + PCS setpoint sender
6. CANopen heartbeat timeout monitor

**Rust Examples (4 listings):**
1. SocketCAN interface open
2. Solar telemetry receiver with `bitflags` status decoding
3. BMS state aggregator with critical fault detection and power setpoint transmission
4. CANopen heartbeat watchdog with `HashMap`-based node tracking

**Integration & Diagnostics** — Multi-segment solar farm topology, SYNC-based time alignment for wind pitch, dual-bus redundancy, bus-off recovery, and CANopen EMCY error code table.


## Monitoring and Control of Solar Inverters, Wind Turbines, and Battery Management Systems via CAN

---

## Table of Contents

1. [Introduction](#introduction)
2. [CAN Bus Fundamentals in Renewable Energy](#can-bus-fundamentals-in-renewable-energy)
3. [Solar Inverter Monitoring and Control](#solar-inverter-monitoring-and-control)
4. [Wind Turbine Control Systems](#wind-turbine-control-systems)
5. [Battery Management Systems (BMS)](#battery-management-systems-bms)
6. [CANopen in Renewable Energy](#canopen-in-renewable-energy)
7. [C/C++ Code Examples](#cc-code-examples)
8. [Rust Code Examples](#rust-code-examples)
9. [System Integration and Topologies](#system-integration-and-topologies)
10. [Fault Handling and Diagnostics](#fault-handling-and-diagnostics)
11. [Summary](#summary)

---

## Introduction

The Controller Area Network (CAN) bus, originally designed for automotive applications by Bosch in the 1980s, has found wide adoption in industrial and renewable energy systems. Its robustness, deterministic timing, multi-master capability, and inherent error detection make it ideal for the electrically noisy environments typical of solar farms, wind parks, and battery storage installations.

In renewable energy systems, CAN serves as the communication backbone between:

- **Solar inverters** and central monitoring systems
- **Wind turbine** control units (pitch controllers, yaw drives, generator converters)
- **Battery Management Systems (BMS)** and energy management controllers
- **SCADA** (Supervisory Control and Data Acquisition) gateways

CAN typically operates at speeds from **125 kbit/s** (longer distances, up to 500 m) to **1 Mbit/s** (short distances, < 40 m). In renewable energy installations, **250 kbit/s** or **500 kbit/s** are the most common choices, balancing cable length with update rate requirements.

---

## CAN Bus Fundamentals in Renewable Energy

### Why CAN for Renewable Energy?

| Property | Benefit in Renewable Energy |
|---|---|
| Differential signaling (CAN_H / CAN_L) | Immune to common-mode EMI from inverters and power converters |
| Multi-master bus | Any node can initiate communication; no single point of failure |
| Message priority (11-bit / 29-bit ID) | Critical fault messages preempt telemetry traffic |
| CRC + ACK + bit stuffing | Built-in error detection; automatic retransmission |
| Bus-off recovery | Faulty node isolates itself, protecting the rest of the network |
| Up to 127 nodes (CANopen) | Enough for large string inverter arrays or turbine subsystems |

### Physical Layer

A renewable energy CAN segment requires:

- **120 Ω termination resistors** at both ends of the trunk
- **Twisted-pair cable** (e.g., DeviceNet or CANopen-certified cable)
- **Node drop lines** kept < 30 cm to avoid reflections
- **Galvanic isolation** at each node (especially inverters connected to high-voltage DC bus)

### Message Frame Structure (Standard 11-bit ID)

```
SOF | ID[10:0] | RTR | IDE | r0 | DLC[3:0] | DATA[0..7] | CRC[14:0] | CRC_DEL | ACK | ACK_DEL | EOF
```

In renewable energy applications, the **11-bit identifier** is typically structured as:

```
[10:8]  Function code (3 bits)  — e.g., PDO, SDO, NMT in CANopen
[7:0]   Node ID      (8 bits)  — 1–127 for CANopen
```

---

## Solar Inverter Monitoring and Control

### Key Parameters Transmitted via CAN

Solar inverters expose real-time data and accept control commands over CAN. Typical process data objects (PDOs) include:

**Telemetry (Inverter → Master):**

- DC input voltage (V_PV), DC input current (I_PV)
- AC output voltage (V_AC), AC output current (I_AC), frequency (f_AC)
- Active power (P), reactive power (Q), apparent power (S)
- Inverter temperature, heatsink temperature
- Daily energy yield (Wh), total energy (kWh)
- MPPT (Maximum Power Point Tracking) operating point
- Error/warning status word

**Control (Master → Inverter):**

- Power setpoint (active / reactive power curtailment)
- Grid code compliance commands (FRT — Fault Ride Through)
- Start / Stop / Standby commands
- MPPT mode selection

### CAN ID Scheme — Solar Array Example

```
CAN ID = (Function << 7) | Node_ID

Function codes:
  0x01 = NMT (network management)
  0x03 = TPDO1 (inverter real-time data)
  0x04 = RPDO1 (power setpoint from controller)
  0x05 = TPDO2 (energy counters, temperature)
  0x06 = SDO (service data, configuration)
  0x07 = Emergency (EMCY)
```

### Typical PDO Mapping — Single-Phase Inverter

**TPDO1 (8 bytes, 100 ms cycle):**

| Bytes | Parameter | Unit | Scale |
|---|---|---|---|
| 0–1 | V_DC (PV voltage) | V | × 0.1 |
| 2–3 | I_DC (PV current) | A | × 0.01 |
| 4–5 | P_AC (AC power) | W | × 1 |
| 6 | Temperature | °C | offset −40 |
| 7 | Status flags | — | bitmask |

**Status Flags Byte:**

```
Bit 7: Grid fault
Bit 6: Overtemperature
Bit 5: DC overvoltage
Bit 4: Islanding detected
Bit 3: MPPT active
Bit 2: Feed-in active
Bit 1: Standby
Bit 0: Error present
```

---

## Wind Turbine Control Systems

### Architecture

A modern wind turbine contains multiple CAN-connected subsystems:

```
Main Controller (MCS)
    │
    ├── Pitch Controller Node 1 (Blade 1)
    ├── Pitch Controller Node 2 (Blade 2)
    ├── Pitch Controller Node 3 (Blade 3)
    ├── Generator Converter / Frequency Inverter
    ├── Yaw Drive Controller
    ├── Condition Monitoring Unit (vibration, RPM)
    └── Meteorological Station (wind speed, direction)
```

### Critical Signals

**Pitch Control (safety-critical, high priority):**

- Blade angle setpoint and actual angle (0.01° resolution)
- Pitch motor speed and torque
- Pitch battery / capacitor voltage (for emergency feathering)
- Limit switch status (0°, 90° endstops)

**Generator Converter:**

- Rotor speed (RPM), generator torque setpoint
- DC link voltage, active/reactive power output
- Grid synchronisation status

**Yaw System:**

- Nacelle position (°), wind direction (°)
- Cable twist counter (number of full rotations)
- Yaw motor current

### Safety Priority in CAN ID Assignment

For wind turbines, message priority maps to urgency:

```
Priority 0 (ID 0x000–0x07F): Emergency stop, overspeed trip
Priority 1 (ID 0x080–0x0FF): Pitch safety commands
Priority 2 (ID 0x100–0x17F): Generator torque setpoint
Priority 3 (ID 0x180–0x2FF): Real-time telemetry PDOs
Priority 4 (ID 0x300–0x57F): Slow telemetry (temperature, counters)
Priority 5 (ID 0x580–0x5FF): SDO configuration
Priority 6 (ID 0x700–0x77F): NMT heartbeat
```

Lower numerical CAN ID = higher bus priority (arbitration is bitwise AND).

---

## Battery Management Systems (BMS)

### Role of CAN in BMS

Battery Energy Storage Systems (BESS) use CAN to interface the BMS with:

- **Power Conversion System (PCS)** / bidirectional inverter
- **Energy Management System (EMS)**
- **Thermal Management Controller**
- **Protection Relay / Contactor Controller**

### Standard Protocols

Several BMS CAN protocols have emerged as de-facto standards:

- **CANopen DS401 / DS302** — generic I/O and network management
- **SMA Battery Protocol (SunSpec)** — widely used in residential/commercial storage
- **CATL / BYD proprietary** — common in grid-scale lithium-iron-phosphate (LFP) systems
- **CiA 454** (CANopen for energy storage) — standardised object dictionary for BESS

### Key BMS → PCS Messages

**State of Charge and Limits (sent every 100–500 ms):**

| Parameter | Description |
|---|---|
| SoC | State of Charge (0–100 %) |
| SoH | State of Health (0–100 %) |
| V_total | Total pack voltage |
| I_actual | Pack current (positive = charging) |
| T_max | Maximum cell temperature |
| T_min | Minimum cell temperature |
| V_cell_max | Highest individual cell voltage |
| V_cell_min | Lowest individual cell voltage |
| I_charge_max | Maximum allowed charge current |
| I_discharge_max | Maximum allowed discharge current |
| V_charge_max | Charge voltage limit |
| V_discharge_min | Discharge cutoff voltage |

**BMS Status / Fault Word (sent every 100 ms):**

```
Bit 15: Emergency stop request
Bit 14: Pre-charge active
Bit 13: Main contactor closed
Bit 12: Charge contactor closed
Bit  8: Overtemperature warning
Bit  7: Overtemperature fault
Bit  6: Overcurrent fault
Bit  5: Overvoltage fault
Bit  4: Undervoltage fault
Bit  3: Cell imbalance warning
Bit  2: Communication timeout fault
Bit  1: BMS fault (general)
Bit  0: BMS OK
```

### PCS → BMS Control Messages

```
Power setpoint    : +kW (charge), −kW (discharge)
Operating mode    : Idle / Charge / Discharge / Balancing
Contactor command : Open / Close main/charge contactors
```

---

## CANopen in Renewable Energy

CANopen (CiA 301) is the dominant higher-layer protocol in European renewable energy equipment. It defines:

- **NMT** (Network Management): Initialisation, pre-operational, operational, stopped states
- **PDO** (Process Data Objects): Real-time cyclic or event-driven data exchange
- **SDO** (Service Data Objects): Parameter configuration, firmware upload
- **EMCY** (Emergency): Fault reporting with error code and manufacturer data
- **SYNC**: Bus-wide synchronisation object for coordinated PDO transmission
- **Heartbeat / Node Guarding**: Watchdog mechanism detecting lost nodes

### NMT State Machine

```
    INITIALISING
         │
         ▼
    PRE-OPERATIONAL ◄────────────────────┐
         │  NMT Start                    │ NMT Stop
         ▼                               │
    OPERATIONAL ──────────────────────────┘
         │  NMT Reset Node
         ▼
    INITIALISING (restart)
```

In renewable energy systems, the **master** (e.g., data logger or SCADA gateway) transitions inverters and BMS nodes to OPERATIONAL state after verifying heartbeats. Any node failing to send a heartbeat within the configured guard time triggers a fault response.

---

## C/C++ Code Examples

### 1. Linux SocketCAN — Opening a CAN Interface

```c
/* can_init.c — Open a raw SocketCAN socket on Linux */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int can_open(const char *interface_name)
{
    int sock;
    struct sockaddr_can addr;
    struct ifreq ifr;

    /* Create a raw CAN socket */
    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    /* Bind to the named interface (e.g. "can0") */
    strncpy(ifr.ifr_name, interface_name, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    return sock;
}
```

---

### 2. Solar Inverter — Sending a Power Curtailment Command (C)

```c
/* solar_control.c — Send active power setpoint to inverter node */
#include <linux/can.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define RPDO1_FUNC_CODE  0x200U   /* CANopen RPDO1 function code */
#define INVERTER_NODE_ID 0x05U    /* Target inverter node ID     */

/*
 * Encode and send an RPDO1 power setpoint.
 *
 * RPDO1 layout (8 bytes):
 *   [0–1] : Active power setpoint   (int16, unit = 10 W, range 0–65535 = 0–655 kW)
 *   [2–3] : Reactive power setpoint (int16, unit = 10 VAR, signed)
 *   [4]   : Control byte
 *             Bit 0 = Start
 *             Bit 1 = Stop
 *             Bit 2 = Curtailment enable
 *   [5–7] : Reserved (set to 0)
 */
int solar_send_power_setpoint(int sock,
                               uint8_t  node_id,
                               uint16_t active_power_W,
                               int16_t  reactive_power_VAR,
                               uint8_t  control_flags)
{
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));

    frame.can_id  = RPDO1_FUNC_CODE | node_id;
    frame.can_dlc = 8;

    /* Active power: scale to 10 W units */
    uint16_t p_scaled = active_power_W / 10U;
    frame.data[0] = (uint8_t)(p_scaled & 0xFF);
    frame.data[1] = (uint8_t)(p_scaled >> 8);

    /* Reactive power: scale to 10 VAR units (signed) */
    int16_t q_scaled = reactive_power_VAR / 10;
    frame.data[2] = (uint8_t)(q_scaled & 0xFF);
    frame.data[3] = (uint8_t)((q_scaled >> 8) & 0xFF);

    /* Control byte */
    frame.data[4] = control_flags;

    /* Reserved */
    frame.data[5] = 0;
    frame.data[6] = 0;
    frame.data[7] = 0;

    ssize_t written = write(sock, &frame, sizeof(frame));
    return (written == sizeof(frame)) ? 0 : -1;
}

/* Example usage:
 *   curtail inverter 5 to 30,000 W with curtailment enabled
 *   solar_send_power_setpoint(sock, 0x05, 30000, 0, 0x05);
 */
```

---

### 3. Solar Inverter — Receiving and Parsing Telemetry (C)

```c
/* solar_rx.c — Receive and decode TPDO1 from a solar inverter */
#include <linux/can.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TPDO1_FUNC_CODE 0x180U

typedef struct {
    float    v_dc;         /* PV voltage, V        */
    float    i_dc;         /* PV current, A        */
    int16_t  p_ac;         /* AC active power, W   */
    int8_t   temperature;  /* Inverter temp, °C    */
    uint8_t  status;       /* Status flags         */
    uint8_t  node_id;      /* Source node          */
} SolarTelemetry;

/* Status flag masks */
#define STATUS_GRID_FAULT    0x80
#define STATUS_OVERTEMP      0x40
#define STATUS_DC_OVERVOLT   0x20
#define STATUS_ISLANDING     0x10
#define STATUS_MPPT_ACTIVE   0x08
#define STATUS_FEED_IN       0x04
#define STATUS_STANDBY       0x02
#define STATUS_ERROR         0x01

int solar_parse_tpdo1(const struct can_frame *frame, SolarTelemetry *out)
{
    if (frame->can_dlc < 8) {
        return -1; /* Short frame — reject */
    }

    uint32_t func_code = frame->can_id & 0x780U;
    if (func_code != TPDO1_FUNC_CODE) {
        return -1; /* Not a TPDO1 */
    }

    out->node_id = (uint8_t)(frame->can_id & 0x7FU);

    /* V_DC: bytes 0–1, scale × 0.1 V */
    uint16_t v_raw = (uint16_t)frame->data[0] |
                     ((uint16_t)frame->data[1] << 8);
    out->v_dc = v_raw * 0.1f;

    /* I_DC: bytes 2–3, scale × 0.01 A */
    uint16_t i_raw = (uint16_t)frame->data[2] |
                     ((uint16_t)frame->data[3] << 8);
    out->i_dc = i_raw * 0.01f;

    /* P_AC: bytes 4–5, signed, unit = 1 W */
    out->p_ac = (int16_t)((uint16_t)frame->data[4] |
                           ((uint16_t)frame->data[5] << 8));

    /* Temperature: byte 6, offset −40 °C */
    out->temperature = (int8_t)frame->data[6] - 40;

    /* Status flags: byte 7 */
    out->status = frame->data[7];

    return 0;
}

void solar_print_telemetry(const SolarTelemetry *t)
{
    printf("Node %02X | V_DC=%.1f V | I_DC=%.2f A | P_AC=%d W | T=%d°C\n",
           t->node_id, t->v_dc, t->i_dc, t->p_ac, t->temperature);

    if (t->status & STATUS_GRID_FAULT)  printf("  [!] Grid fault\n");
    if (t->status & STATUS_OVERTEMP)    printf("  [!] Overtemperature\n");
    if (t->status & STATUS_MPPT_ACTIVE) printf("  [+] MPPT active\n");
    if (t->status & STATUS_FEED_IN)     printf("  [+] Feed-in active\n");
}
```

---

### 4. Wind Turbine — Pitch Controller Emergency Feathering (C++)

```cpp
/* pitch_control.cpp — Emergency pitch-to-feather command via CAN */
#include <linux/can.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

/* CAN IDs (29-bit extended for this turbine variant) */
#define CAN_ID_PITCH_EMCY_FEATHER  0x00000010UL  /* Highest priority — all blades */
#define CAN_ID_PITCH_SETPOINT_B1   0x00000181UL  /* Blade 1 angle setpoint        */
#define CAN_ID_PITCH_SETPOINT_B2   0x00000182UL  /* Blade 2 angle setpoint        */
#define CAN_ID_PITCH_SETPOINT_B3   0x00000183UL  /* Blade 3 angle setpoint        */

#define FEATHER_ANGLE_DEG   90.0f  /* Full feather = 90° */
#define SCALE_ANGLE_TO_INT  100.0f /* 0.01° per unit    */

/*
 * Encode a blade pitch angle setpoint.
 * Bytes 0–1: Angle setpoint (int16, unit = 0.01°)
 * Bytes 2–3: Angular velocity limit (uint16, unit = 0.01 °/s)
 * Byte  4  : Mode (0=normal, 1=emergency)
 * Bytes 5–7: Reserved
 */
static void encode_pitch_setpoint(struct can_frame &frame,
                                   uint32_t can_id,
                                   float    angle_deg,
                                   float    rate_deg_per_s,
                                   uint8_t  mode)
{
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = can_id | CAN_EFF_FLAG;  /* Extended frame */
    frame.can_dlc = 8;

    int16_t  angle_raw = static_cast<int16_t>(angle_deg * SCALE_ANGLE_TO_INT);
    uint16_t rate_raw  = static_cast<uint16_t>(rate_deg_per_s * SCALE_ANGLE_TO_INT);

    frame.data[0] = static_cast<uint8_t>(angle_raw & 0xFF);
    frame.data[1] = static_cast<uint8_t>((angle_raw >> 8) & 0xFF);
    frame.data[2] = static_cast<uint8_t>(rate_raw & 0xFF);
    frame.data[3] = static_cast<uint8_t>((rate_raw >> 8) & 0xFF);
    frame.data[4] = mode;
}

/*
 * Send emergency feather command to all three blades.
 * Uses the highest-priority CAN ID (0x010) first,
 * then individual blade setpoints.
 */
int pitch_emergency_feather(int sock)
{
    struct can_frame frame;

    /* Broadcast emergency stop (1 byte, all-blades) */
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = CAN_ID_PITCH_EMCY_FEATHER | CAN_EFF_FLAG;
    frame.can_dlc = 1;
    frame.data[0] = 0xFF;  /* Emergency feather code */

    if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
        perror("Emergency feather broadcast failed");
        return -1;
    }

    /* Individual blade setpoints: 90°, max rate 10 °/s, emergency mode */
    const uint32_t blade_ids[3] = {
        CAN_ID_PITCH_SETPOINT_B1,
        CAN_ID_PITCH_SETPOINT_B2,
        CAN_ID_PITCH_SETPOINT_B3
    };

    for (int i = 0; i < 3; ++i) {
        encode_pitch_setpoint(frame, blade_ids[i],
                              FEATHER_ANGLE_DEG,
                              10.0f,  /* 10 °/s pitch rate */
                              1);     /* emergency mode    */
        if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
            fprintf(stderr, "Blade %d setpoint write failed\n", i + 1);
            return -1;
        }
    }

    printf("Emergency feather command sent to all 3 blades.\n");
    return 0;
}
```

---

### 5. Battery Management System — BMS Status Decoder and PCS Setpoint (C++)

```cpp
/* bms_interface.cpp — BMS CAN interface for grid storage */
#include <linux/can.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <unistd.h>

/* BMS → PCS CAN IDs (example: CATL-style mapping) */
#define BMS_CAN_ID_STATUS        0x351U  /* SoC, SoH, voltages     */
#define BMS_CAN_ID_LIMITS        0x355U  /* Current/voltage limits  */
#define BMS_CAN_ID_TEMPERATURE   0x356U  /* Cell temperatures       */
#define BMS_CAN_ID_FAULT         0x35AU  /* Fault/alarm flags       */

/* PCS → BMS CAN IDs */
#define PCS_CAN_ID_POWER_SETPT   0x35EU  /* Power setpoint to BMS   */

/* Fault bitmask definitions */
#define BMS_FAULT_EMCY_STOP      (1U << 15)
#define BMS_FAULT_PRECHARGE      (1U << 14)
#define BMS_FAULT_MAIN_CONTACTOR (1U << 13)
#define BMS_FAULT_OVERTEMP_WARN  (1U <<  8)
#define BMS_FAULT_OVERTEMP_FAULT (1U <<  7)
#define BMS_FAULT_OVERCURRENT    (1U <<  6)
#define BMS_FAULT_OVERVOLTAGE    (1U <<  5)
#define BMS_FAULT_UNDERVOLTAGE   (1U <<  4)
#define BMS_FAULT_IMBALANCE_WARN (1U <<  3)
#define BMS_FAULT_COMM_TIMEOUT   (1U <<  2)
#define BMS_FAULT_GENERAL        (1U <<  1)
#define BMS_OK                   (1U <<  0)

struct BmsStatus {
    float    soc_pct;             /* State of Charge [%]      */
    float    soh_pct;             /* State of Health [%]      */
    float    v_total;             /* Pack voltage [V]         */
    float    i_actual;            /* Pack current [A]         */
    float    t_max_degC;          /* Max cell temperature     */
    float    t_min_degC;          /* Min cell temperature     */
    float    v_cell_max;          /* Max cell voltage [mV]    */
    float    v_cell_min;          /* Min cell voltage [mV]    */
    float    i_charge_max_A;      /* Max charge current [A]   */
    float    i_discharge_max_A;   /* Max discharge current [A]*/
    uint16_t fault_flags;         /* Fault/status bitmask     */
};

/* Parse 0x351: Pack status */
static int parse_bms_status(const struct can_frame *f, BmsStatus *s)
{
    if (f->can_id != BMS_CAN_ID_STATUS || f->can_dlc < 8) return -1;

    /* SoC: byte 0, unit = 0.5% */
    s->soc_pct = f->data[0] * 0.5f;

    /* SoH: byte 1, unit = 0.5% */
    s->soh_pct = f->data[1] * 0.5f;

    /* V_total: bytes 2–3, unit = 0.1 V */
    uint16_t v_raw = (uint16_t)f->data[2] | ((uint16_t)f->data[3] << 8);
    s->v_total = v_raw * 0.1f;

    /* I_actual: bytes 4–5, signed, unit = 0.1 A */
    int16_t i_raw = (int16_t)((uint16_t)f->data[4] | ((uint16_t)f->data[5] << 8));
    s->i_actual = i_raw * 0.1f;

    /* Fault flags: bytes 6–7 */
    s->fault_flags = (uint16_t)f->data[6] | ((uint16_t)f->data[7] << 8);

    return 0;
}

/* Parse 0x355: Current and voltage limits */
static int parse_bms_limits(const struct can_frame *f, BmsStatus *s)
{
    if (f->can_id != BMS_CAN_ID_LIMITS || f->can_dlc < 8) return -1;

    uint16_t ic_raw = (uint16_t)f->data[0] | ((uint16_t)f->data[1] << 8);
    s->i_charge_max_A = ic_raw * 0.1f;

    uint16_t id_raw = (uint16_t)f->data[2] | ((uint16_t)f->data[3] << 8);
    s->i_discharge_max_A = id_raw * 0.1f;

    return 0;
}

/* Parse 0x356: Cell temperatures */
static int parse_bms_temperature(const struct can_frame *f, BmsStatus *s)
{
    if (f->can_id != BMS_CAN_ID_TEMPERATURE || f->can_dlc < 4) return -1;

    s->t_max_degC = (int8_t)f->data[0] * 1.0f;
    s->t_min_degC = (int8_t)f->data[1] * 1.0f;
    return 0;
}

/* Send PCS power setpoint → BMS (0x35E) */
int bms_send_power_setpoint(int sock, float power_kW)
{
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = PCS_CAN_ID_POWER_SETPT;
    frame.can_dlc = 4;

    /* Power: int16, unit = 10 W (positive = charge, negative = discharge) */
    int16_t p_raw = (int16_t)(power_kW * 100.0f); /* kW × 100 = 10 W units */
    frame.data[0] = (uint8_t)(p_raw & 0xFF);
    frame.data[1] = (uint8_t)((p_raw >> 8) & 0xFF);
    frame.data[2] = 0x01; /* Command valid */
    frame.data[3] = 0x00; /* Reserved      */

    return (write(sock, &frame, sizeof(frame)) == sizeof(frame)) ? 0 : -1;
}

/* Print BMS status with fault interpretation */
void bms_print_status(const BmsStatus *s)
{
    printf("=== BMS Status ===\n");
    printf("  SoC: %.1f %%  SoH: %.1f %%\n", s->soc_pct, s->soh_pct);
    printf("  Pack voltage: %.1f V\n", s->v_total);
    printf("  Current: %.1f A (%s)\n", s->i_actual,
           s->i_actual >= 0.0f ? "charging" : "discharging");
    printf("  Temp max: %.0f°C  min: %.0f°C\n", s->t_max_degC, s->t_min_degC);
    printf("  Max charge:    %.1f A\n", s->i_charge_max_A);
    printf("  Max discharge: %.1f A\n", s->i_discharge_max_A);

    if (s->fault_flags & BMS_FAULT_EMCY_STOP)      printf("  [!!!] EMERGENCY STOP requested!\n");
    if (s->fault_flags & BMS_FAULT_OVERTEMP_FAULT)  printf("  [!]   Overtemperature FAULT\n");
    if (s->fault_flags & BMS_FAULT_OVERCURRENT)     printf("  [!]   Overcurrent FAULT\n");
    if (s->fault_flags & BMS_FAULT_OVERVOLTAGE)     printf("  [!]   Overvoltage FAULT\n");
    if (s->fault_flags & BMS_FAULT_UNDERVOLTAGE)    printf("  [!]   Undervoltage FAULT\n");
    if (s->fault_flags & BMS_FAULT_OVERTEMP_WARN)   printf("  [W]   Overtemperature WARNING\n");
    if (s->fault_flags & BMS_FAULT_IMBALANCE_WARN)  printf("  [W]   Cell imbalance WARNING\n");
    if (s->fault_flags & BMS_OK)                    printf("  [OK]  BMS operational\n");
}
```

---

### 6. CANopen Heartbeat Monitor (C)

```c
/* heartbeat_monitor.c — Detect lost nodes in a CANopen renewable energy network */
#include <linux/can.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define HEARTBEAT_FUNC_CODE  0x700U   /* CANopen heartbeat function code */
#define MAX_NODES            128
#define HEARTBEAT_TIMEOUT_MS 1500     /* 1.5 × expected 1000 ms heartbeat */

/* CANopen NMT states (heartbeat payload byte) */
typedef enum {
    NMT_BOOT_UP        = 0x00,
    NMT_STOPPED        = 0x04,
    NMT_OPERATIONAL    = 0x05,
    NMT_PRE_OPERATIONAL = 0x7F
} NmtState;

typedef struct {
    uint8_t    node_id;
    NmtState   state;
    struct timespec last_seen;
    int        online;
} NodeEntry;

static NodeEntry nodes[MAX_NODES];

static long timespec_diff_ms(const struct timespec *a, const struct timespec *b)
{
    return (long)(a->tv_sec - b->tv_sec) * 1000L +
           (long)(a->tv_nsec - b->tv_nsec) / 1000000L;
}

void heartbeat_update(const struct can_frame *frame)
{
    uint32_t func = frame->can_id & 0x780U;
    if (func != HEARTBEAT_FUNC_CODE || frame->can_dlc < 1) return;

    uint8_t node_id = frame->can_id & 0x7FU;
    if (node_id == 0 || node_id >= MAX_NODES) return;

    NodeEntry *n = &nodes[node_id];
    n->node_id = node_id;
    n->state   = (NmtState)frame->data[0];
    clock_gettime(CLOCK_MONOTONIC, &n->last_seen);

    if (!n->online) {
        printf("[+] Node %d came online (state=0x%02X)\n", node_id, n->state);
        n->online = 1;
    }
}

void heartbeat_check_all(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    for (int i = 1; i < MAX_NODES; ++i) {
        NodeEntry *n = &nodes[i];
        if (!n->online) continue;

        long ms = timespec_diff_ms(&now, &n->last_seen);
        if (ms > HEARTBEAT_TIMEOUT_MS) {
            printf("[!] Node %d LOST (last seen %ld ms ago)\n", n->node_id, ms);
            n->online = 0;
            /* Production code: trigger alarm, attempt NMT reset, notify SCADA */
        }
    }
}
```

---

## Rust Code Examples

### 1. Opening a SocketCAN Interface (Rust)

```rust
// can_interface.rs — Open SocketCAN using the `socketcan` crate
// Cargo.toml: socketcan = "3"

use socketcan::{CanSocket, Socket};

fn open_can_socket(interface: &str) -> Result<CanSocket, socketcan::Error> {
    let socket = CanSocket::open(interface)?;
    println!("Opened CAN socket on {}", interface);
    Ok(socket)
}
```

---

### 2. Solar Inverter Telemetry — Receiver and Parser (Rust)

```rust
// solar_rx.rs — Receive and decode solar inverter TPDO1 frames

use socketcan::{CanFrame, CanSocket, Frame, Socket};

const TPDO1_FUNC_CODE: u32 = 0x180;

/// Decoded solar inverter telemetry
#[derive(Debug, Default)]
pub struct SolarTelemetry {
    pub node_id:     u8,
    pub v_dc:        f32,   // PV voltage [V]
    pub i_dc:        f32,   // PV current [A]
    pub p_ac:        i16,   // AC active power [W]
    pub temperature: i8,    // Inverter temperature [°C]
    pub status:      u8,    // Status flags
}

bitflags::bitflags! {
    /// Solar inverter status flags (byte 7 of TPDO1)
    pub struct InverterStatus: u8 {
        const GRID_FAULT   = 0x80;
        const OVERTEMP     = 0x40;
        const DC_OVERVOLT  = 0x20;
        const ISLANDING    = 0x10;
        const MPPT_ACTIVE  = 0x08;
        const FEED_IN      = 0x04;
        const STANDBY      = 0x02;
        const ERROR        = 0x01;
    }
}

/// Parse a CAN data frame into SolarTelemetry.
/// Returns None if the frame is not a TPDO1 or has insufficient data.
pub fn parse_solar_tpdo1(frame: &CanFrame) -> Option<SolarTelemetry> {
    let id = frame.raw_id();
    let func = id & 0x780;

    if func != TPDO1_FUNC_CODE {
        return None;
    }

    let data = frame.data();
    if data.len() < 8 {
        return None;
    }

    let v_raw = u16::from_le_bytes([data[0], data[1]]);
    let i_raw = u16::from_le_bytes([data[2], data[3]]);
    let p_raw = i16::from_le_bytes([data[4], data[5]]);

    Some(SolarTelemetry {
        node_id:     (id & 0x7F) as u8,
        v_dc:        v_raw as f32 * 0.1,
        i_dc:        i_raw as f32 * 0.01,
        p_ac:        p_raw,
        temperature: data[6] as i8 - 40,
        status:      data[7],
    })
}

/// Main receive loop for solar telemetry
pub fn solar_monitor_loop(socket: &CanSocket) {
    loop {
        match socket.read_frame() {
            Ok(frame) => {
                if let Some(tel) = parse_solar_tpdo1(&frame) {
                    println!(
                        "Node {:02X} | V_DC={:.1}V | I_DC={:.2}A | P_AC={}W | T={}°C",
                        tel.node_id, tel.v_dc, tel.i_dc, tel.p_ac, tel.temperature
                    );
                    let status = InverterStatus::from_bits_truncate(tel.status);
                    if status.contains(InverterStatus::GRID_FAULT) {
                        eprintln!("  [ALARM] Node {:02X}: Grid fault!", tel.node_id);
                    }
                    if status.contains(InverterStatus::MPPT_ACTIVE) {
                        println!("  [OK] Node {:02X}: MPPT tracking", tel.node_id);
                    }
                }
            }
            Err(e) => eprintln!("CAN read error: {e}"),
        }
    }
}
```

---

### 3. Battery Management System Interface (Rust)

```rust
// bms_interface.rs — BMS CAN interface for grid-scale storage

use socketcan::{CanFrame, CanSocket, EmbeddedFrame, Frame, Socket, StandardId};

const BMS_ID_STATUS:      u16 = 0x351;
const BMS_ID_LIMITS:      u16 = 0x355;
const BMS_ID_TEMPERATURE: u16 = 0x356;
const BMS_ID_FAULT:       u16 = 0x35A;
const PCS_ID_POWER_SETPT: u16 = 0x35E;

/// BMS fault flags
#[derive(Debug, Default, Clone, Copy)]
pub struct BmsFaultFlags(pub u16);

impl BmsFaultFlags {
    pub fn emergency_stop(self)      -> bool { self.0 & 0x8000 != 0 }
    pub fn precharge_active(self)    -> bool { self.0 & 0x4000 != 0 }
    pub fn main_contactor(self)      -> bool { self.0 & 0x2000 != 0 }
    pub fn overtemp_warning(self)    -> bool { self.0 & 0x0100 != 0 }
    pub fn overtemp_fault(self)      -> bool { self.0 & 0x0080 != 0 }
    pub fn overcurrent(self)         -> bool { self.0 & 0x0040 != 0 }
    pub fn overvoltage(self)         -> bool { self.0 & 0x0020 != 0 }
    pub fn undervoltage(self)        -> bool { self.0 & 0x0010 != 0 }
    pub fn cell_imbalance(self)      -> bool { self.0 & 0x0008 != 0 }
    pub fn comm_timeout(self)        -> bool { self.0 & 0x0004 != 0 }
    pub fn bms_ok(self)              -> bool { self.0 & 0x0001 != 0 }
}

/// Aggregated BMS state — populated incrementally as frames arrive
#[derive(Debug, Default)]
pub struct BmsState {
    pub soc_pct:          f32,
    pub soh_pct:          f32,
    pub v_pack:           f32,   // [V]
    pub i_actual:         f32,   // [A] positive = charging
    pub t_max:            i8,    // [°C]
    pub t_min:            i8,    // [°C]
    pub i_charge_max:     f32,   // [A]
    pub i_discharge_max:  f32,   // [A]
    pub faults:           BmsFaultFlags,
}

impl BmsState {
    /// Update state from a received CAN frame
    pub fn update_from_frame(&mut self, frame: &CanFrame) {
        let id = frame.raw_id() as u16;
        let d  = frame.data();

        match id {
            BMS_ID_STATUS if d.len() >= 8 => {
                self.soc_pct  = d[0] as f32 * 0.5;
                self.soh_pct  = d[1] as f32 * 0.5;
                self.v_pack   = u16::from_le_bytes([d[2], d[3]]) as f32 * 0.1;
                self.i_actual = i16::from_le_bytes([d[4], d[5]]) as f32 * 0.1;
                self.faults   = BmsFaultFlags(u16::from_le_bytes([d[6], d[7]]));
            }
            BMS_ID_LIMITS if d.len() >= 4 => {
                self.i_charge_max    = u16::from_le_bytes([d[0], d[1]]) as f32 * 0.1;
                self.i_discharge_max = u16::from_le_bytes([d[2], d[3]]) as f32 * 0.1;
            }
            BMS_ID_TEMPERATURE if d.len() >= 2 => {
                self.t_max = d[0] as i8;
                self.t_min = d[1] as i8;
            }
            _ => {}
        }
    }

    /// Check if any critical fault is active
    pub fn is_critical_fault(&self) -> bool {
        self.faults.emergency_stop()
            || self.faults.overtemp_fault()
            || self.faults.overcurrent()
            || self.faults.overvoltage()
            || self.faults.undervoltage()
    }
}

/// Send a power setpoint (kW) from PCS to BMS.
/// Positive = charge, Negative = discharge.
pub fn send_power_setpoint(
    socket: &CanSocket,
    power_kw: f32,
) -> Result<(), socketcan::Error> {
    let p_raw = (power_kw * 100.0) as i16; // 10 W units
    let payload: [u8; 4] = [
        (p_raw & 0xFF) as u8,
        ((p_raw >> 8) & 0xFF) as u8,
        0x01, // command valid
        0x00, // reserved
    ];

    let id = StandardId::new(PCS_ID_POWER_SETPT).expect("Valid ID");
    let frame = CanFrame::new(id, &payload).expect("Valid frame");
    socket.write_frame(&frame)?;
    Ok(())
}

/// Main BMS monitoring loop
pub fn bms_monitor_loop(socket: &CanSocket) {
    let mut state = BmsState::default();

    loop {
        match socket.read_frame() {
            Ok(frame) => {
                state.update_from_frame(&frame);

                if state.is_critical_fault() {
                    eprintln!("[CRITICAL] BMS fault detected! Requesting PCS shutdown.");
                    // In a real system: send emergency stop, open contactors
                    let _ = send_power_setpoint(socket, 0.0);
                }

                println!(
                    "BMS | SoC={:.1}% | V={:.1}V | I={:.1}A | T={}–{}°C | Faults={:#06X}",
                    state.soc_pct,
                    state.v_pack,
                    state.i_actual,
                    state.t_min,
                    state.t_max,
                    state.faults.0
                );
            }
            Err(e) => eprintln!("CAN read error: {e}"),
        }
    }
}
```

---

### 4. Wind Turbine — CANopen Heartbeat Monitor (Rust)

```rust
// heartbeat_monitor.rs — CANopen heartbeat watchdog for turbine nodes

use std::collections::HashMap;
use std::time::{Duration, Instant};
use socketcan::{CanFrame, CanSocket, Frame, Socket};

const HEARTBEAT_FUNC: u32     = 0x700;
const TIMEOUT:        Duration = Duration::from_millis(1500);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NmtState {
    BootUp        = 0x00,
    Stopped       = 0x04,
    Operational   = 0x05,
    PreOperational = 0x7F,
    Unknown,
}

impl From<u8> for NmtState {
    fn from(v: u8) -> Self {
        match v {
            0x00 => Self::BootUp,
            0x04 => Self::Stopped,
            0x05 => Self::Operational,
            0x7F => Self::PreOperational,
            _    => Self::Unknown,
        }
    }
}

#[derive(Debug)]
struct NodeRecord {
    state:     NmtState,
    last_seen: Instant,
    online:    bool,
}

pub struct HeartbeatMonitor {
    nodes: HashMap<u8, NodeRecord>,
}

impl HeartbeatMonitor {
    pub fn new() -> Self {
        Self { nodes: HashMap::new() }
    }

    /// Process an incoming CAN frame and update heartbeat records
    pub fn update(&mut self, frame: &CanFrame) {
        let id   = frame.raw_id();
        let func = id & 0x780;
        if func != HEARTBEAT_FUNC { return; }

        let node_id = (id & 0x7F) as u8;
        if node_id == 0 { return; }

        let data = frame.data();
        if data.is_empty() { return; }

        let state = NmtState::from(data[0]);
        let entry = self.nodes.entry(node_id).or_insert(NodeRecord {
            state,
            last_seen: Instant::now(),
            online: false,
        });

        if !entry.online {
            println!("[+] Node {node_id} came online ({state:?})");
            entry.online = true;
        }
        entry.state     = state;
        entry.last_seen = Instant::now();
    }

    /// Check all registered nodes for timeout
    pub fn check_timeouts(&mut self) -> Vec<u8> {
        let now   = Instant::now();
        let mut lost = Vec::new();

        for (id, record) in self.nodes.iter_mut() {
            if record.online && now.duration_since(record.last_seen) > TIMEOUT {
                eprintln!("[!] Node {id} LOST — timeout exceeded {TIMEOUT:?}");
                record.online = false;
                lost.push(*id);
            }
        }
        lost
    }
}

/// Run heartbeat monitoring — call check_timeouts periodically
pub fn run_heartbeat_monitor(socket: &CanSocket) {
    let mut monitor = HeartbeatMonitor::new();
    let mut last_check = Instant::now();

    loop {
        if let Ok(frame) = socket.read_frame() {
            monitor.update(&frame);
        }

        if last_check.elapsed() >= Duration::from_millis(500) {
            let lost = monitor.check_timeouts();
            if !lost.is_empty() {
                // Trigger alarm, attempt NMT reset, notify SCADA
                eprintln!("Lost nodes: {lost:?}");
            }
            last_check = Instant::now();
        }
    }
}
```

---

## System Integration and Topologies

### Multi-Segment Architecture (Solar Farm Example)

In a utility-scale solar installation, multiple CAN segments are bridged through gateways to a SCADA system:

```
String 1 (Inverters 1–8) ─── CAN Segment A ──┐
String 2 (Inverters 9–16)─── CAN Segment B ──┤
String 3 (Inverters 17–24)── CAN Segment C ──┤── CAN/Ethernet Gateway ── SCADA
Battery Storage (BMS 1–3) ── CAN Segment D ──┤
Met Station + Tracker ─────── CAN Segment E ──┘
```

Each CAN segment runs at 250 kbit/s. The gateway aggregates data and provides Modbus TCP / IEC 61850 / SunSpec Modbus interfaces toward the plant control system.

### Time Synchronisation

In wind turbines with coordinated blade pitch, the **CANopen SYNC** object (ID 0x080) is broadcast by the master controller at a fixed interval (typically 10 ms). All PDO-transmitting nodes latch their data on SYNC reception and transmit their TPDO immediately after, ensuring all blade telemetry is time-aligned within one bus arbitration period.

### Redundancy Approaches

- **Dual CAN bus**: Safety-critical nodes (pitch controllers) connect to two independent CAN segments; the controller arbitrates between them
- **Watchdog timers**: Each node has a hardware watchdog; if the CAN stack stops feeding it (e.g., due to bus-off), the node resets to a safe state
- **Node guarding (CANopen)**: Master polls nodes with a guard message; nodes reply with their NMT state; timeout triggers NMT reset command

---

## Fault Handling and Diagnostics

### CANopen Emergency Object

When a node detects a fault (e.g., inverter DC overvoltage, BMS cell imbalance), it broadcasts an **Emergency (EMCY)** frame:

```
CAN ID = 0x080 | Node_ID

Bytes 0–1: Error code   (16-bit, standardised in CiA 301)
Byte  2  : Error register (from object 0x1001)
Bytes 3–7: Manufacturer-specific error information
```

Common error codes in renewable energy:

| Error Code | Meaning |
|---|---|
| 0x3100 | DC Overvoltage |
| 0x3200 | DC Undervoltage |
| 0x4210 | Overtemperature — device |
| 0x5112 | Power supply voltage too low |
| 0x7305 | Communication — heartbeat timeout |
| 0x8130 | Life guard / heartbeat error |
| 0xFF10 | Grid frequency out of range (manufacturer-specific) |

### CAN Bus Error States

```
Active Error  →  Warning Limit (>96 errors)  →  Bus-Off (>255 TX errors)
     ↑                                                    ↓
     └────────────── Recovery (128+8 recessive bits) ─────┘
```

In renewable energy, bus-off typically indicates:

- Faulty termination or broken cable
- Damaged node transceiver (common near power electronics)
- Ground loop between inverters at different DC potentials

Always provide **galvanic isolation** (optocouplers or magnetic isolators) on CAN transceivers in inverter nodes to prevent this.

---

## Summary

CAN bus has established itself as a robust, proven communication technology in renewable energy systems. Its inherent strengths — differential signaling for EMI immunity, hardware-level error detection, message prioritisation through ID-based arbitration, and automatic fault isolation with bus-off recovery — address the specific challenges of electrically noisy photovoltaic and wind installations.

**Solar inverter** applications leverage CAN for real-time MPPT telemetry, grid-compliance control (power curtailment, reactive power support), and fault reporting. Arrays of string inverters connect as CANopen nodes, with a master logger or SCADA gateway reading PDOs at 100–500 ms intervals and issuing RPDO setpoints for grid code compliance.

**Wind turbine** control systems depend on CAN for safety-critical pitch control — where message latency and bus reliability directly affect structural safety — as well as generator converter coordination, yaw control, and condition monitoring. The strict message priority hierarchy ensures that emergency feather commands always pre-empt routine telemetry, and CANopen SYNC enables time-aligned PDO transmission across pitch controller nodes.

**Battery Management Systems** communicate pack state, cell limits, temperatures, and fault conditions to Power Conversion Systems via well-defined CAN frame mappings. The BMS uses CAN to enforce charge and discharge current limits in real time, protecting cell longevity and preventing thermal runaway. The PCS sends power setpoints subject to these limits, enabling grid services such as peak shaving, frequency response, and arbitrage.

Across all three applications, **CANopen** (CiA 301) provides the higher-layer framework: NMT state management for node lifecycle, PDO mapping for real-time data, SDO for parameterisation, EMCY objects for fault reporting, and heartbeat monitoring for network integrity. **C/C++** implementations using Linux SocketCAN offer direct kernel-level access to the CAN hardware with minimal overhead. **Rust** implementations using the `socketcan` crate bring memory safety and strong typing to embedded and gateway software, reducing the risk of data corruption in mission-critical energy infrastructure.

As renewable energy systems scale toward multi-megawatt installations and multi-protocol environments, CAN continues to serve as the low-latency, deterministic real-time backbone — complemented by higher-bandwidth protocols (EtherCAT, CANopen FD, Profinet) for data-intensive applications — ensuring reliable monitoring and control across the full operational lifetime of solar, wind, and storage assets.

---

*Document: 78 — CAN in Renewable Energy | Part of the CAN Bus Technology Series*