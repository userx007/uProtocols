# CiA 402 — Motion Control & Drives Profile

1. **Introduction** — Purpose, goals, and scope of CiA 402 / IEC 61800-7
2. **State Machine** — Full ASCII state diagram with all 8 states, transition conditions, and a concise transition table showing the exact Control Word bit patterns required
3. **Control Word (0x6040)** — Bit-level ASCII layout, all bit definitions, and a macro table with common values
4. **Status Word (0x6041)** — Bit-level ASCII layout, state decoding logic with `DS402_DecodeState()` in C
5. **Drive Modes** — All 10 modes (PP/VL/PV/PT/HM/IP/CSP/CSV/CST) with key object references; PP ramp profile shown in ASCII
6. **Homing Methods** — Full method table, ASCII phase-by-phase sequence diagram, relevant objects, and homing status word bit table
7. **CSP Mode** — ASCII architecture diagram (master↔drive control loops) and SYNC timing diagram
8. **C/C++ Examples** — Header definitions, SDO abstraction layer, state machine controller, mode selection, PP absolute moves, homing sequence, CSP real-time ISR loop, C++ object-oriented wrapper, and error code decoding
9. **Summary** — Condensed reference table plus design recommendations for production use


## Table of Contents

1. [Introduction](#1-introduction)
2. [CiA 402 State Machine](#2-cia-402-state-machine)
3. [Control Word (0x6040)](#3-control-word-0x6040)
4. [Status Word (0x6041)](#4-status-word-0x6041)
5. [Supported Drive Modes](#5-supported-drive-modes)
6. [Homing Methods](#6-homing-methods)
7. [Synchronous Cyclic Position Mode (CSP)](#7-synchronous-cyclic-position-mode-csp)
8. [Programming Examples in C/C++](#8-programming-examples-in-cc)
9. [Summary](#9-summary)

---

## 1. Introduction

CiA 402 (also published as IEC 61800-7-201/301) is the CANopen device profile for **drives and motion control**. It defines a standardised object dictionary, state machine, and set of operating modes that allow motion controllers, servo drives, stepper drives, and variable-frequency drives from different manufacturers to be interchanged without rewriting application software.

Key design goals:

- Uniform **state machine** ensuring safe power-up and fault recovery sequences.
- Standardised **control word** and **status word** for state transitions and feedback.
- Multiple **drive modes** covering position, velocity, torque, and synchronous cyclic operation.
- A defined **homing procedure** that brings axes to a known reference position.

CiA 402 is today the dominant motion-control profile in industrial automation, used on CANopen, EtherCAT (CoE), PROFINET, and EtherNet/IP networks alike.

---

## 2. CiA 402 State Machine

The state machine governs every CiA 402 drive. Before a drive can move it must pass through well-defined states in a strict sequence. This prevents accidental motion and ensures that the power stage is only enabled when the controller has explicitly requested it.

### 2.1 State Diagram

```
                         ┌─────────────────────────────────────────┐
   Power-On / Reset ───► │         NOT READY TO SWITCH ON          │
                         └──────────────────┬──────────────────────┘
                                            │ (automatic, internal self-test)
                                            ▼
                         ┌─────────────────────────────────────────┐
         ┌───────────────│         SWITCH ON DISABLED              │◄──────────────┐
         │               └──────────────────┬──────────────────────┘               │
         │                                  │ Shutdown (CW bit2=1, bit1=1, bit0=0) │
         │                                  ▼                                      │
         │               ┌─────────────────────────────────────────┐               │
         │   ┌───────────│           READY TO SWITCH ON            │               │
         │   │           └──────────────────┬──────────────────────┘               │
         │   │ Quick Stop                   │ Switch On (CW bit2=1,bit1=1,bit0=1)  │
         │   │                              ▼                                      │
         │   │           ┌─────────────────────────────────────────┐               │
         │   │           │              SWITCHED ON                │               │
         │   │           └──────────────────┬──────────────────────┘               │
         │   │                              │ Enable Operation (CW bit3=1)         │
         │   │                              ▼                                      │
         │   │           ┌─────────────────────────────────────────┐               │
         │   └──────────►│           OPERATION ENABLED             │               │
         │               └──────┬──────────────────────────────────┘               │
         │                      │                                                  │
         │        Any Error ────►┌──────────────────────────────────────────┐      │
         │                       │                  FAULT                   │      │
         │                       └──────────────────┬───────────────────────┘      │
         │                                         │ Fault Reset (CW bit7 0→1)     │
         │                                         ▼                               │
         └────────────────────────────────────────► (returns to SWITCH ON DISABLED)┘

   Quick Stop Active: reached from OPERATION ENABLED via Quick Stop command (CW bit2=0)
```

### 2.2 State Descriptions

| State | Description |
|---|---|
| **Not Ready to Switch On** | Drive initialising. Power stage disabled. Not controllable. |
| **Switch On Disabled** | Initialisation complete. Waiting for enable sequence. High-voltage may be present. |
| **Ready to Switch On** | Drive accepts switch-on command. Power stage still off. |
| **Switched On** | Power stage energised but drive not following set-point. |
| **Operation Enabled** | Drive is active, following the demanded set-point. Motion is possible. |
| **Quick Stop Active** | Quick-stop deceleration ramp active. Entered on Quick Stop command or watchdog. |
| **Fault Reaction Active** | Drive has detected an error and is performing fault reaction (e.g. ramp to zero). |
| **Fault** | Drive locked. Must receive Fault Reset command to re-enter Switch On Disabled. |

### 2.3 State Transition Commands (Control Word Patterns)

```
CW bit pattern:  Bit7  Bit3  Bit2  Bit1  Bit0
                 FRes  EOP   QS    ES    SO

Transition          Bit7  Bit3  Bit2  Bit1  Bit0   Description
─────────────────────────────────────────────────────────────────
Shutdown              0     0     1     1     0    SWITCH-ON-DISABLED → READY TO SWITCH ON
Switch On             0     0     1     1     1    READY TO SWITCH ON → SWITCHED ON
Enable Op.            0     1     1     1     1    SWITCHED ON → OPERATION ENABLED
Disable Voltage       0     X     X     0     X    → SWITCH ON DISABLED
Quick Stop            0     X     0     1     X    → QUICK STOP ACTIVE
Disable Op.           0     0     1     1     1    OPERATION ENABLED → SWITCHED ON
Fault Reset    0→1    X     X     X     X    FAULT → SWITCH ON DISABLED
```

---

## 3. Control Word (0x6040)

The Control Word is a **16-bit write object** at index `0x6040`, sub-index `0x00`. The master writes this object to command state transitions and mode-specific actions.

### 3.1 Bit Assignment

```
Bit 15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
     │ r │ r │ r │ r │M3 │M2 │M1 │HS │FRS│ r │ r │ r │EOP│ QS│ ES│ SO│
     └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘

r   = Reserved
SO  = Switch On           (bit 0)
ES  = Enable Voltage      (bit 1)
QS  = Quick Stop          (bit 2)  — active LOW: 0 = Quick Stop
EOP = Enable Operation    (bit 3)
M1–M3 = Mode-specific     (bits 4–6)
HS  = Halt                (bit 8)
FRS = Fault Reset         (bit 7)  — rising edge triggers reset
```

### 3.2 Common Control Word Values

```c
/* Common Control Word values */
#define CW_SHUTDOWN            0x0006u   /* 0000 0000 0000 0110  Shutdown */
#define CW_SWITCH_ON           0x0007u   /* 0000 0000 0000 0111  Switch On */
#define CW_ENABLE_OPERATION    0x000Fu   /* 0000 0000 0000 1111  Enable Operation */
#define CW_DISABLE_VOLTAGE     0x0000u   /* 0000 0000 0000 0000  Disable Voltage */
#define CW_QUICK_STOP          0x0002u   /* 0000 0000 0000 0010  Quick Stop (QS=0) */
#define CW_FAULT_RESET         0x0080u   /* 0000 0000 1000 0000  Fault Reset (bit7 high) */
#define CW_HALT                0x010Fu   /* bit8 set, drive halts on ramp */

/* Profile Position mode: new set-point trigger */
#define CW_PP_NEW_SETPOINT     0x001Fu   /* Enable Op + bit4 (New Set-Point) */
#define CW_PP_ABS_IMMEDIATE    0x002Fu   /* bit5=1: change on the fly */
```

---

## 4. Status Word (0x6041)

The Status Word is a **16-bit read object** at index `0x6041`, sub-index `0x00`. The drive updates it continuously. The master monitors it via PDO or SDO to determine the current state and any drive-specific conditions.

### 4.1 Bit Assignment

```
Bit 15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
     │M7 │M6 │ r │ILA│M5 │M4 │RM │HA │WRN│SW │QSA│VE │FLT│OE │SO │RDY│
     └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘

RDY = Ready to Switch On  (bit 0)
SO  = Switched On         (bit 1)
OE  = Operation Enabled   (bit 2)
FLT = Fault               (bit 3)
VE  = Voltage Enabled     (bit 4)
QSA = Quick Stop Active   (bit 5)  — 0 = Quick Stop Active
SW  = Switch On Disabled  (bit 6)
WRN = Warning             (bit 7)
HA  = Halt Active         (bit 8)  — drive halted
RM  = Remote              (bit 9)
M4–M7 = Mode-specific     (bits 10–13)
ILA = Internal Limit Active (bit 11)
```

### 4.2 State Decoding

```c
/* Mask and value pairs for state identification */
#define SW_MASK_NOT_READY      0x004Fu   /* mask */
#define SW_VAL_NOT_READY       0x0000u   /* value: bits 0,1,2,3,5,6 = 000000 */

#define SW_MASK_SWITCH_ON_DIS  0x004Fu
#define SW_VAL_SWITCH_ON_DIS   0x0040u   /* bit6=1 */

#define SW_MASK_READY          0x006Fu
#define SW_VAL_READY           0x0021u   /* bit5=1, bit0=1 */

#define SW_MASK_SWITCHED_ON    0x006Fu
#define SW_VAL_SWITCHED_ON     0x0023u   /* bit5=1, bit1=1, bit0=1 */

#define SW_MASK_OP_ENABLED     0x006Fu
#define SW_VAL_OP_ENABLED      0x0027u   /* bit5=1, bit2=1, bit1=1, bit0=1 */

#define SW_MASK_FAULT          0x004Fu
#define SW_VAL_FAULT           0x0008u   /* bit3=1 */

typedef enum {
    DS402_STATE_UNKNOWN = 0,
    DS402_STATE_NOT_READY,
    DS402_STATE_SWITCH_ON_DISABLED,
    DS402_STATE_READY_TO_SWITCH_ON,
    DS402_STATE_SWITCHED_ON,
    DS402_STATE_OPERATION_ENABLED,
    DS402_STATE_QUICK_STOP_ACTIVE,
    DS402_STATE_FAULT_REACTION_ACTIVE,
    DS402_STATE_FAULT
} DS402_State_t;

DS402_State_t DS402_DecodeState(uint16_t sw)
{
    if ((sw & 0x004F) == 0x0000) return DS402_STATE_NOT_READY;
    if ((sw & 0x004F) == 0x0040) return DS402_STATE_SWITCH_ON_DISABLED;
    if ((sw & 0x006F) == 0x0021) return DS402_STATE_READY_TO_SWITCH_ON;
    if ((sw & 0x006F) == 0x0023) return DS402_STATE_SWITCHED_ON;
    if ((sw & 0x006F) == 0x0027) return DS402_STATE_OPERATION_ENABLED;
    if ((sw & 0x006F) == 0x0007) return DS402_STATE_QUICK_STOP_ACTIVE;
    if ((sw & 0x004F) == 0x000F) return DS402_STATE_FAULT_REACTION_ACTIVE;
    if ((sw & 0x004F) == 0x0008) return DS402_STATE_FAULT;
    return DS402_STATE_UNKNOWN;
}
```

---

## 5. Supported Drive Modes

The **Modes of Operation** object (`0x6060`) selects the active mode. The **Modes of Operation Display** object (`0x6061`) reflects the mode the drive has actually accepted.

### 5.1 Mode Table

```
Code  Abbrev  Name                            Set-point object
────────────────────────────────────────────────────────────────
  1    PP     Profile Position Mode           0x607A Target Position
  2    VL     Velocity Mode (legacy)          0x6042 vl Target Velocity
  3    PV     Profile Velocity Mode           0x60FF Target Velocity
  4    PT     Profile Torque Mode             0x6071 Target Torque
  6    HM     Homing Mode                     — (method via 0x6098)
  7    IP     Interpolated Position Mode      0x60C1 IP Data Record
  8    CSP    Cyclic Sync Position Mode       0x607A Target Position
  9    CSV    Cyclic Sync Velocity Mode       0x60FF Target Velocity
 10    CST    Cyclic Sync Torque Mode         0x6071 Target Torque
```

### 5.2 Profile Position Mode (PP)

The drive executes a **trapezoidal motion profile** from current to target position using the configured acceleration, deceleration, and maximum velocity ramps.

```
Velocity
   │
Vmax ┤      ┌──────────────────┐
   │     /                    \
   │    /  Accel                \ Decel
   │   /                         \
   └──┴─────────────────────────────┴──► Time
      t0                              t1
```

Key objects:

| Object | Description |
|---|---|
| `0x607A` | Target Position (INT32, user units) |
| `0x6081` | Profile Velocity (UINT32) |
| `0x6083` | Profile Acceleration (UINT32) |
| `0x6084` | Profile Deceleration (UINT32) |
| `0x6086` | Motion Profile Type (0=trapezoidal, 3=sin²) |
| `0x607D` | Software Position Limit [min, max] |

### 5.3 Profile Velocity Mode (PV)

Drive accelerates/decelerates to reach and hold a target velocity. Target is written to `0x60FF`.

### 5.4 Profile Torque Mode (PT)

Drive applies and ramps to a demanded torque. Target torque in `0x6071` in units of 0.1 % of rated torque.

### 5.5 Homing Mode (HM)

See Section 6 for full details.

### 5.6 Interpolated Position Mode (IP)

Pre-CiA 402 v3 synchronous mode. The master supplies a stream of position set-points (via `0x60C1`) synchronised to an SYNC interval. The drive interpolates between points.

### 5.7 Cyclic Synchronous Position (CSP)

Modern synchronous mode. Master sends absolute target position every SYNC cycle via PDO. Drive handles the trajectory locally, performing position following. See Section 7.

### 5.8 Cyclic Synchronous Velocity (CSV)

As CSP but the master sends target velocity (`0x60FF`) every cycle.

### 5.9 Cyclic Synchronous Torque (CST)

As CSP but the master sends target torque (`0x6071`) every cycle.

---

## 6. Homing Methods

Homing establishes a relationship between the physical axis and the logical position coordinate system. The homing method is selected via object `0x6098` (Homing Method). The homing speed is split into two phases, controlled by `0x6099` sub-indices 1 and 2, and the homing acceleration by `0x609A`.

### 6.1 Overview of Homing Methods

```
Method  Description
──────────────────────────────────────────────────────────────────────────
  1     Negative limit switch + index pulse
  2     Positive limit switch + index pulse
  3–4   Positive home switch + index pulse (direction variants)
  5–6   Negative home switch + index pulse (direction variants)
  7–14  Home switch with direction / edge variants (no index)
 17–18  Negative / positive limit switch (no index pulse)
 19–22  Positive home switch (no index pulse, direction variants)
 23–26  Negative home switch (no index pulse, direction variants)
 27–30  Negative / positive home switch, current position variants
 33–34  Index pulse only (no home switch)
 35     Current position (immediate — no movement)
-1     Manufacturer-specific
-2     Manufacturer-specific
-128   Manufacturer-specific
```

### 6.2 Homing Sequence (ASCII Flow)

```
   ┌──────────────────────────────────────────────────────────────────┐
   │            HOMING SEQUENCE (Method 3 example)                    │
   │                                                                  │
   │  Phase 1: Fast search for home switch                            │
   │  ──────────────────────────────────                              │
   │  Axis moves at homing_speed_1 (0x6099 sub1)                      │
   │                                                                  │
   │   Position                                                       │
   │      │                    HOME SW                                │
   │      │         ┌──────────┐                                      │
   │      │         │          │                                      │
   │  ────┼─────────┘          └─────────  (switch state)             │
   │      │  ──────────────────►           (axis direction)           │
   │      │             ▲                                             │
   │      │         rising edge detected → reverse                    │
   │                                                                  │
   │  Phase 2: Slow approach to index pulse / switch edge             │
   │  ────────────────────────────────────────────────                │
   │  Axis moves at homing_speed_2 (0x6099 sub2)                      │
   │                                                                  │
   │      │  ◄──────────── slow reverse until index or edge           │
   │      │                    ▲                                      │
   │      │              index pulse (encoder Z)                      │
   │      │              or home switch edge                          │
   │                                                                  │
   │  Phase 3: Homing offset applied                                  │
   │  ─────────────────────────────                                   │
   │  Position counter set to Home Offset (0x607C)                    │
   │  Status Word bit 12 (Homing Attained) = 1                        │
   └──────────────────────────────────────────────────────────────────┘
```

### 6.3 Homing-related Objects

| Object | Sub | Description |
|---|---|---|
| `0x6098` | 0 | Homing Method (INT8) |
| `0x6099` | 1 | Speed during search for switch (UINT32) |
| `0x6099` | 2 | Speed during search for zero (UINT32) |
| `0x609A` | 0 | Homing Acceleration (UINT32) |
| `0x607C` | 0 | Home Offset (INT32) — added to raw home position |
| `0x6040` | — | CW bit4: Start Homing (rising edge while in HM) |
| `0x6041` | — | SW bit10: Target Reached, bit12: Homing Attained |

### 6.4 Homing Status Word Bits

```
SW bit 13  12  10   Meaning
──────────────────────────────────────────────────────
     0    0    0   Homing in progress
     0    0    1   Homing in progress (target not yet reached)
     0    1    1   Homing completed successfully
     1    0    0   Homing error occurred; velocity not zero
     1    0    1   Homing error occurred; velocity is zero
```

---

## 7. Synchronous Cyclic Position Mode (CSP)

CSP is the preferred high-performance mode for multi-axis synchronised motion. The **master** (e.g. EtherCAT master, CANopen master with SYNC) is responsible for the complete trajectory. The drive only performs **position following** with its inner servo loop.

### 7.1 CSP Architecture

```
   MASTER (PLC / Motion Controller)                DRIVE
   ──────────────────────────────────              ─────────────────────────────
   ┌────────────────────────────────┐              ┌────────────────────────────┐
   │  Interpolator / Trajectory     │   SYNC+PDO   │                            │
   │  ┌──────────────────────────┐  │ ──────────►  │  Position Control Loop     │
   │  │ Position profile         │  │  Target Pos  │  ┌──────────────────────┐  │
   │  │ (spline / linear / etc.) │  │  0x607A      │  │  Kp · (Tgt - Act)    │  │
   │  └──────────────────────────┘  │              │  └──────────┬───────────┘  │
   │                                │  ◄─────────  │             │              │
   │  Reads: Actual position        │  Actual Pos  │  Velocity   │  Loop        │
   │         0x6064                 │  0x6064      │  ┌──────────▼───────────┐  │
   └────────────────────────────────┘              │  │  Velocity Controller │  │
                                                   │  └──────────┬───────────┘  │
                                                   │             │              │
                                                   │  Current  ──▼──  Motor     │
                                                   └────────────────────────────┘
```

### 7.2 Key Objects for CSP

| Object | Description |
|---|---|
| `0x6060` | Modes of Operation — write `8` for CSP |
| `0x6061` | Modes of Operation Display — read back to confirm mode |
| `0x607A` | Target Position (INT32) — written every SYNC cycle |
| `0x6064` | Position Actual Value (INT32) — read each cycle |
| `0x60B0` | Position Offset (INT32) — added to target position in drive |
| `0x60B1` | Velocity Offset (INT32) — feed-forward velocity |
| `0x60B2` | Torque Offset (INT16) — feed-forward torque |
| `0x60C2` | Interpolation Time Period (period + index) |
| `0x6065` | Following Error Window |
| `0x6066` | Following Error Time Out |

### 7.3 CSP SYNC Timing

```
  SYNC         ────┐   ┌────────┐   ┌────────┐   ┌────────
  (CANopen)        └───┘        └───┘        └───┘
                   ◄──── T_sync ────►

  PDO Rx       ─────────┐           ┌─────────────┐
  (Target Pos)          └───────────┘             └──────
                         ◄─ PDO Tx ─►
                         T_pos[n]    T_pos[n+1]

  Note: Master sends new target position just after each SYNC pulse.
        Drive latches it and begins trajectory following until next SYNC.
```

---

## 8. Programming Examples in C/C++

### 8.1 Object Dictionary Helper Macros

```c
/* canopen_ds402.h — CiA 402 definitions */
#ifndef CANOPEN_DS402_H
#define CANOPEN_DS402_H

#include <stdint.h>
#include <stdbool.h>

/* ── Object indices ─────────────────────────────────────────────── */
#define OD_MODES_OF_OPERATION       0x6060u
#define OD_MODES_OF_OPERATION_DISP  0x6061u
#define OD_CONTROL_WORD             0x6040u
#define OD_STATUS_WORD              0x6041u
#define OD_TARGET_POSITION          0x607Au
#define OD_POSITION_ACTUAL          0x6064u
#define OD_TARGET_VELOCITY          0x60FFu
#define OD_VELOCITY_ACTUAL          0x606Cu
#define OD_TARGET_TORQUE            0x6071u
#define OD_PROFILE_VELOCITY         0x6081u
#define OD_PROFILE_ACCEL            0x6083u
#define OD_PROFILE_DECEL            0x6084u
#define OD_SOFTWARE_POS_LIMIT       0x607Du
#define OD_HOME_OFFSET              0x607Cu
#define OD_HOMING_METHOD            0x6098u
#define OD_HOMING_SPEEDS            0x6099u
#define OD_HOMING_ACCEL             0x609Au
#define OD_POSITION_OFFSET          0x60B0u
#define OD_INTERP_TIME_PERIOD       0x60C2u
#define OD_FOLLOWING_ERR_WINDOW     0x6065u
#define OD_FOLLOWING_ERR_TIMEOUT    0x6066u
#define OD_ERROR_CODE               0x603Fu
#define OD_SUPPORTED_DRIVE_MODES    0x6502u

/* ── Drive modes ────────────────────────────────────────────────── */
typedef enum {
    DS402_MODE_PP  = 1,   /* Profile Position           */
    DS402_MODE_VL  = 2,   /* Velocity (legacy)          */
    DS402_MODE_PV  = 3,   /* Profile Velocity           */
    DS402_MODE_PT  = 4,   /* Profile Torque             */
    DS402_MODE_HM  = 6,   /* Homing                     */
    DS402_MODE_IP  = 7,   /* Interpolated Position      */
    DS402_MODE_CSP = 8,   /* Cyclic Sync Position       */
    DS402_MODE_CSV = 9,   /* Cyclic Sync Velocity       */
    DS402_MODE_CST = 10   /* Cyclic Sync Torque         */
} DS402_Mode_t;

/* ── Control Word bits ──────────────────────────────────────────── */
#define CW_BIT_SWITCH_ON         (1u << 0)
#define CW_BIT_ENABLE_VOLTAGE    (1u << 1)
#define CW_BIT_QUICK_STOP        (1u << 2)  /* 0 = Quick Stop */
#define CW_BIT_ENABLE_OPERATION  (1u << 3)
#define CW_BIT_NEW_SET_POINT     (1u << 4)  /* PP mode */
#define CW_BIT_CHANGE_ON_FLY     (1u << 5)  /* PP mode */
#define CW_BIT_ABS_REL           (1u << 6)  /* PP: 0=abs, 1=rel */
#define CW_BIT_FAULT_RESET       (1u << 7)
#define CW_BIT_HALT              (1u << 8)
#define CW_BIT_HM_START          (1u << 4)  /* HM mode: start homing */

/* ── Status Word bits ───────────────────────────────────────────── */
#define SW_BIT_READY_TO_SWITCH   (1u << 0)
#define SW_BIT_SWITCHED_ON       (1u << 1)
#define SW_BIT_OP_ENABLED        (1u << 2)
#define SW_BIT_FAULT             (1u << 3)
#define SW_BIT_VOLTAGE_ENABLED   (1u << 4)
#define SW_BIT_QUICK_STOP        (1u << 5)  /* 0 = Quick Stop active */
#define SW_BIT_SW_ON_DISABLED    (1u << 6)
#define SW_BIT_WARNING           (1u << 7)
#define SW_BIT_REMOTE            (1u << 9)
#define SW_BIT_TARGET_REACHED    (1u << 10)
#define SW_BIT_INTERNAL_LIMIT    (1u << 11)
#define SW_BIT_HM_ATTAINED       (1u << 12) /* HM: homing attained */
#define SW_BIT_HM_ERROR          (1u << 13) /* HM: homing error     */

/* ── Convenience masks for CW state commands ────────────────────── */
#define CW_SHUTDOWN          (CW_BIT_ENABLE_VOLTAGE | CW_BIT_QUICK_STOP)
#define CW_SWITCH_ON         (CW_BIT_SWITCH_ON | CW_BIT_ENABLE_VOLTAGE | CW_BIT_QUICK_STOP)
#define CW_ENABLE_OPERATION  (CW_BIT_SWITCH_ON | CW_BIT_ENABLE_VOLTAGE | \
                              CW_BIT_QUICK_STOP | CW_BIT_ENABLE_OPERATION)
#define CW_DISABLE_VOLTAGE   0x0000u
#define CW_QUICK_STOP        CW_BIT_ENABLE_VOLTAGE          /* QS bit = 0 */
#define CW_FAULT_RESET_HIGH  (CW_BIT_FAULT_RESET)
#define CW_FAULT_RESET_LOW   0x0000u

#endif /* CANOPEN_DS402_H */
```

### 8.2 Platform-agnostic SDO Abstraction

```c
/* canopen_sdo.h — minimal platform abstraction */
#ifndef CANOPEN_SDO_H
#define CANOPEN_SDO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** Return codes for SDO operations */
typedef enum {
    SDO_OK = 0,
    SDO_ERR_TIMEOUT,
    SDO_ERR_ABORT,
    SDO_ERR_PARAM
} SDO_Result_t;

/**
 * @brief  Write a 16-bit value to the remote node via SDO.
 * @note   Platform must implement this function.
 */
SDO_Result_t SDO_Write16(uint8_t nodeId, uint16_t index,
                         uint8_t subIndex, uint16_t value);

/**
 * @brief  Write a 32-bit value to the remote node via SDO.
 */
SDO_Result_t SDO_Write32(uint8_t nodeId, uint16_t index,
                         uint8_t subIndex, uint32_t value);

/**
 * @brief  Write an 8-bit (INT8/UINT8) value.
 */
SDO_Result_t SDO_Write8(uint8_t nodeId, uint16_t index,
                        uint8_t subIndex, uint8_t value);

/**
 * @brief  Read a 16-bit value from the remote node.
 */
SDO_Result_t SDO_Read16(uint8_t nodeId, uint16_t index,
                        uint8_t subIndex, uint16_t *value);

/**
 * @brief  Read a 32-bit value from the remote node.
 */
SDO_Result_t SDO_Read32(uint8_t nodeId, uint16_t index,
                        uint8_t subIndex, uint32_t *value);

#endif /* CANOPEN_SDO_H */
```

### 8.3 CiA 402 State Machine Controller

```c
/* ds402_ctrl.c — Drive enable / state-machine controller */
#include "canopen_ds402.h"
#include "canopen_sdo.h"
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────── */

static DS402_State_t GetState(uint8_t nodeId)
{
    uint16_t sw = 0;
    if (SDO_Read16(nodeId, OD_STATUS_WORD, 0, &sw) != SDO_OK)
        return DS402_STATE_UNKNOWN;
    return DS402_DecodeState(sw);
}

static SDO_Result_t WriteCW(uint8_t nodeId, uint16_t cw)
{
    return SDO_Write16(nodeId, OD_CONTROL_WORD, 0, cw);
}

/* ── Main enable sequence ────────────────────────────────────────── */

/**
 * @brief  Bring drive from any state to OPERATION ENABLED.
 *         Handles Fault clearing automatically.
 * @param  nodeId      CANopen node ID of the target drive.
 * @param  timeoutMs   Maximum time allowed per step (ms).
 * @return true on success.
 */
bool DS402_EnableDrive(uint8_t nodeId, uint32_t timeoutMs)
{
    DS402_State_t state;
    uint32_t elapsed = 0;
    const uint32_t POLL_MS = 5;   /* polling interval */

    /* Step 1 — clear fault if present */
    state = GetState(nodeId);
    if (state == DS402_STATE_FAULT) {
        WriteCW(nodeId, CW_FAULT_RESET_LOW);   /* ensure bit7 = 0 */
        HAL_Delay(10);
        WriteCW(nodeId, CW_FAULT_RESET_HIGH);  /* rising edge */
        HAL_Delay(50);
        WriteCW(nodeId, CW_FAULT_RESET_LOW);
    }

    /* Step 2 — SWITCH ON DISABLED → READY TO SWITCH ON */
    for (elapsed = 0; elapsed < timeoutMs; elapsed += POLL_MS) {
        state = GetState(nodeId);
        if (state == DS402_STATE_SWITCH_ON_DISABLED) {
            WriteCW(nodeId, CW_SHUTDOWN);
            break;
        }
        HAL_Delay(POLL_MS);
    }

    /* Step 3 — READY TO SWITCH ON → SWITCHED ON */
    for (elapsed = 0; elapsed < timeoutMs; elapsed += POLL_MS) {
        state = GetState(nodeId);
        if (state == DS402_STATE_READY_TO_SWITCH_ON) {
            WriteCW(nodeId, CW_SWITCH_ON);
            break;
        }
        HAL_Delay(POLL_MS);
    }

    /* Step 4 — SWITCHED ON → OPERATION ENABLED */
    for (elapsed = 0; elapsed < timeoutMs; elapsed += POLL_MS) {
        state = GetState(nodeId);
        if (state == DS402_STATE_SWITCHED_ON) {
            WriteCW(nodeId, CW_ENABLE_OPERATION);
            break;
        }
        HAL_Delay(POLL_MS);
    }

    /* Step 5 — Verify */
    for (elapsed = 0; elapsed < timeoutMs; elapsed += POLL_MS) {
        state = GetState(nodeId);
        if (state == DS402_STATE_OPERATION_ENABLED) return true;
        HAL_Delay(POLL_MS);
    }

    return false; /* timed out */
}

/**
 * @brief  Disable drive (OPERATION ENABLED → SWITCHED ON).
 */
bool DS402_DisableDrive(uint8_t nodeId)
{
    WriteCW(nodeId, CW_SWITCH_ON);   /* EOP bit cleared */
    return true;
}

/**
 * @brief  Issue Quick Stop.
 */
bool DS402_QuickStop(uint8_t nodeId)
{
    WriteCW(nodeId, CW_QUICK_STOP);
    return true;
}
```

### 8.4 Mode Selection

```c
/**
 * @brief  Set and confirm drive operating mode.
 */
bool DS402_SetMode(uint8_t nodeId, DS402_Mode_t mode, uint32_t timeoutMs)
{
    uint32_t elapsed = 0;
    uint16_t dispRaw = 0;
    const uint32_t POLL_MS = 5;

    /* Write desired mode */
    if (SDO_Write8(nodeId, OD_MODES_OF_OPERATION, 0, (uint8_t)mode) != SDO_OK)
        return false;

    /* Poll display object until drive confirms the mode */
    for (elapsed = 0; elapsed < timeoutMs; elapsed += POLL_MS) {
        if (SDO_Read16(nodeId, OD_MODES_OF_OPERATION_DISP, 0, &dispRaw) == SDO_OK) {
            if ((int8_t)(dispRaw & 0xFF) == (int8_t)mode) return true;
        }
        HAL_Delay(POLL_MS);
    }
    return false;
}
```

### 8.5 Profile Position Mode (PP) — Absolute Move

```c
/**
 * @brief  Configure PP ramp parameters.
 */
void DS402_PP_Configure(uint8_t nodeId,
                        uint32_t profileVelocity,
                        uint32_t profileAccel,
                        uint32_t profileDecel)
{
    SDO_Write32(nodeId, OD_PROFILE_VELOCITY, 0, profileVelocity);
    SDO_Write32(nodeId, OD_PROFILE_ACCEL,    0, profileAccel);
    SDO_Write32(nodeId, OD_PROFILE_DECEL,    0, profileDecel);
}

/**
 * @brief  Execute absolute profile position move.
 * @param  nodeId       Drive node ID.
 * @param  targetPos    Absolute target position (user units).
 * @param  waitDoneMs   0 = fire-and-forget, >0 = wait up to N ms.
 */
bool DS402_PP_MoveAbsolute(uint8_t nodeId, int32_t targetPos,
                            uint32_t waitDoneMs)
{
    const uint32_t POLL_MS = 5;
    uint32_t elapsed = 0;
    uint16_t sw = 0;

    /* Write target position */
    SDO_Write32(nodeId, OD_TARGET_POSITION, 0, (uint32_t)targetPos);

    /* Ensure ABS mode (bit6=0), trigger new set-point (bit4 rising edge) */
    WriteCW(nodeId, CW_ENABLE_OPERATION);                         /* bit4=0 */
    HAL_Delay(2);
    WriteCW(nodeId, CW_ENABLE_OPERATION | CW_BIT_NEW_SET_POINT);  /* bit4=1 */

    if (waitDoneMs == 0) return true;

    /* Wait until Target Reached (SW bit10) is set */
    for (elapsed = 0; elapsed < waitDoneMs; elapsed += POLL_MS) {
        if (SDO_Read16(nodeId, OD_STATUS_WORD, 0, &sw) == SDO_OK) {
            if (sw & SW_BIT_TARGET_REACHED) {
                WriteCW(nodeId, CW_ENABLE_OPERATION); /* clear bit4 */
                return true;
            }
            if (sw & SW_BIT_FAULT) return false;
        }
        HAL_Delay(POLL_MS);
    }
    return false; /* timed out */
}

/* Example usage */
void ExamplePP(void)
{
    const uint8_t DRIVE_NODE = 3;

    /* 1. Enable drive */
    if (!DS402_EnableDrive(DRIVE_NODE, 2000)) { /* error */ return; }

    /* 2. Select Profile Position Mode */
    if (!DS402_SetMode(DRIVE_NODE, DS402_MODE_PP, 1000)) { /* error */ return; }

    /* 3. Configure motion profile: 10000 cnt/s, accel/decel 50000 cnt/s² */
    DS402_PP_Configure(DRIVE_NODE, 10000, 50000, 50000);

    /* 4. Move to absolute position 100000 counts, wait up to 5 s */
    DS402_PP_MoveAbsolute(DRIVE_NODE, 100000, 5000);

    /* 5. Move again, relative +50000 counts */
    SDO_Write32(DRIVE_NODE, OD_TARGET_POSITION, 0, 50000);
    /* Set bit6 (relative) then trigger */
    WriteCW(DRIVE_NODE, CW_ENABLE_OPERATION | CW_BIT_ABS_REL);
    HAL_Delay(2);
    WriteCW(DRIVE_NODE, CW_ENABLE_OPERATION | CW_BIT_ABS_REL | CW_BIT_NEW_SET_POINT);
}
```

### 8.6 Homing Sequence

```c
/**
 * @brief  Configure and execute homing.
 * @param  nodeId         Drive node ID.
 * @param  method         Homing method code (e.g. 35 = current position).
 * @param  speedSwitch    Phase-1 speed (counts/s).
 * @param  speedZero      Phase-2 speed (counts/s).
 * @param  accel          Homing acceleration (counts/s²).
 * @param  homeOffset     Position offset applied at home (counts).
 * @param  timeoutMs      Max time to complete homing.
 */
bool DS402_Homing(uint8_t nodeId,
                  int8_t  method,
                  uint32_t speedSwitch,
                  uint32_t speedZero,
                  uint32_t accel,
                  int32_t  homeOffset,
                  uint32_t timeoutMs)
{
    const uint32_t POLL_MS = 10;
    uint32_t elapsed = 0;
    uint16_t sw = 0;

    /* Configure homing parameters */
    SDO_Write8 (nodeId, OD_HOMING_METHOD, 0,     (uint8_t)method);
    SDO_Write32(nodeId, OD_HOMING_SPEEDS, 1,     speedSwitch);
    SDO_Write32(nodeId, OD_HOMING_SPEEDS, 2,     speedZero);
    SDO_Write32(nodeId, OD_HOMING_ACCEL,  0,     accel);
    SDO_Write32(nodeId, OD_HOME_OFFSET,   0,     (uint32_t)homeOffset);

    /* Select Homing Mode */
    if (!DS402_SetMode(nodeId, DS402_MODE_HM, 1000)) return false;

    /* Start homing: rising edge on CW bit4 */
    WriteCW(nodeId, CW_ENABLE_OPERATION);
    HAL_Delay(5);
    WriteCW(nodeId, CW_ENABLE_OPERATION | CW_BIT_HM_START);

    /* Wait for Homing Attained (bit12) or Error (bit13) */
    for (elapsed = 0; elapsed < timeoutMs; elapsed += POLL_MS) {
        if (SDO_Read16(nodeId, OD_STATUS_WORD, 0, &sw) == SDO_OK) {
            if (sw & SW_BIT_HM_ATTAINED) {
                WriteCW(nodeId, CW_ENABLE_OPERATION); /* clear start bit */
                return true;
            }
            if (sw & SW_BIT_HM_ERROR) return false;
            if (sw & SW_BIT_FAULT)    return false;
        }
        HAL_Delay(POLL_MS);
    }
    return false;
}

/* Convenience: method 35 = "current position is home" */
bool DS402_Homing_SetCurrentAsHome(uint8_t nodeId)
{
    return DS402_Homing(nodeId, 35, 0, 0, 0, 0, 500);
}
```

### 8.7 Cyclic Synchronous Position Mode (CSP)

```c
/* ds402_csp.c — CSP real-time loop (bare-metal / RTOS timer ISR) */
#include "canopen_ds402.h"
#include "canopen_pdo.h"   /* platform PDO send/receive */

#define NUM_AXES   4

/* Axis descriptor */
typedef struct {
    uint8_t  nodeId;
    int32_t  targetPos;     /* written by trajectory engine each cycle */
    int32_t  actualPos;     /* read back each cycle                    */
    uint16_t statusWord;
    uint16_t controlWord;
} Axis_t;

static Axis_t g_axes[NUM_AXES];

/**
 * @brief  Initialise CSP mode for all axes.
 *         Must be called before starting the SYNC timer.
 */
void CSP_Init(void)
{
    for (int i = 0; i < NUM_AXES; i++) {
        uint8_t id = g_axes[i].nodeId;

        /* Enable drive */
        DS402_EnableDrive(id, 3000);

        /* Select CSP mode (code 8) */
        DS402_SetMode(id, DS402_MODE_CSP, 1000);

        /* Configure following error window */
        SDO_Write32(id, OD_FOLLOWING_ERR_WINDOW, 0, 5000); /* counts */
        SDO_Write32(id, OD_FOLLOWING_ERR_TIMEOUT, 0, 100); /* ms     */

        /* Latch current actual position as first target */
        SDO_Read32(id, OD_POSITION_ACTUAL, 0,
                   (uint32_t *)&g_axes[i].targetPos);
        g_axes[i].actualPos = g_axes[i].targetPos;
    }
}

/**
 * @brief  Called by SYNC ISR every interpolation period.
 *         Must complete within the SYNC interval.
 *
 *  Sequence:
 *   1. Trajectory engine updates g_axes[i].targetPos   (application layer)
 *   2. Send SYNC message (triggers PDO transmission in drives)
 *   3. Write new target positions via RPDO
 *   4. Read actual positions from TPDO
 */
void CSP_SyncCallback(void)
{
    /* 1. Trajectory engine fills g_axes[i].targetPos before this ISR */

    /* 2. Send SYNC — drives latch current actual values to TPDO */
    CANopen_SendSync();

    for (int i = 0; i < NUM_AXES; i++) {
        /* 3. Write target position via RPDO (mapped to 0x607A) */
        PDO_WriteInt32(g_axes[i].nodeId,
                       RPDO1, g_axes[i].targetPos);

        /* 4. Read actual position from TPDO (mapped to 0x6064) */
        PDO_ReadInt32(g_axes[i].nodeId,
                      TPDO1, &g_axes[i].actualPos);

        /* 5. Read status word for fault monitoring */
        PDO_ReadUint16(g_axes[i].nodeId,
                       TPDO1, &g_axes[i].statusWord);

        /* 6. Simple fault check — disable axis on error */
        if (g_axes[i].statusWord & SW_BIT_FAULT) {
            g_axes[i].controlWord = CW_FAULT_RESET_HIGH;
        } else {
            g_axes[i].controlWord = CW_ENABLE_OPERATION;
        }
        PDO_WriteUint16(g_axes[i].nodeId,
                        RPDO1, g_axes[i].controlWord);
    }
}

/**
 * @brief  Simple linear interpolation trajectory for one axis.
 *         Generates target positions for CSP_SyncCallback.
 *
 * @param  axis        Pointer to axis descriptor.
 * @param  goal        Absolute goal position (counts).
 * @param  syncHz      SYNC frequency in Hz.
 * @param  duration_s  Move duration in seconds.
 */
void CSP_LinearMove(Axis_t *axis, int32_t goal,
                    uint32_t syncHz, float duration_s)
{
    uint32_t steps = (uint32_t)(duration_s * (float)syncHz);
    int32_t  start = axis->actualPos;
    int32_t  delta = goal - start;

    for (uint32_t s = 0; s <= steps; s++) {
        axis->targetPos = start + (int32_t)((float)delta * (float)s / (float)steps);
        /* In a real system this would yield/wait for the next SYNC tick */
        HAL_Delay(1000u / syncHz);
    }
}
```

### 8.8 C++ Object-Oriented Drive Wrapper

```cpp
// DS402Drive.hpp — C++ wrapper for a CiA 402 drive
#pragma once
#include "canopen_ds402.h"
#include "canopen_sdo.h"
#include <cstdint>
#include <functional>
#include <stdexcept>

class DS402Drive
{
public:
    explicit DS402Drive(uint8_t nodeId)
        : m_nodeId(nodeId), m_state(DS402_STATE_UNKNOWN) {}

    // ── Lifecycle ─────────────────────────────────────────────────

    /** Enable drive (full state machine sequence). Throws on timeout. */
    void enable(uint32_t timeoutMs = 3000)
    {
        if (!DS402_EnableDrive(m_nodeId, timeoutMs))
            throw std::runtime_error("DS402: enable timeout");
        m_state = DS402_STATE_OPERATION_ENABLED;
    }

    void disable()  { DS402_DisableDrive(m_nodeId); }
    void quickStop(){ DS402_QuickStop(m_nodeId); }

    /** Clears fault and re-enables. */
    void faultReset(uint32_t timeoutMs = 3000)
    {
        writeCW(CW_FAULT_RESET_LOW);
        HAL_Delay(10);
        writeCW(CW_FAULT_RESET_HIGH);
        HAL_Delay(50);
        writeCW(CW_FAULT_RESET_LOW);
        enable(timeoutMs);
    }

    // ── Mode selection ────────────────────────────────────────────

    void setMode(DS402_Mode_t mode, uint32_t timeoutMs = 1000)
    {
        if (!DS402_SetMode(m_nodeId, mode, timeoutMs))
            throw std::runtime_error("DS402: mode set timeout");
        m_mode = mode;
    }

    // ── Profile Position ─────────────────────────────────────────

    void configureProfile(uint32_t vel, uint32_t accel, uint32_t decel)
    {
        DS402_PP_Configure(m_nodeId, vel, accel, decel);
    }

    bool moveAbsolute(int32_t pos, uint32_t waitMs = 0)
    {
        return DS402_PP_MoveAbsolute(m_nodeId, pos, waitMs);
    }

    // ── Homing ───────────────────────────────────────────────────

    bool home(int8_t method = 35, uint32_t timeoutMs = 10000)
    {
        return DS402_Homing(m_nodeId, method,
                            5000, 500, 10000, 0, timeoutMs);
    }

    // ── Status ───────────────────────────────────────────────────

    uint16_t statusWord() const
    {
        uint16_t sw = 0;
        SDO_Read16(m_nodeId, OD_STATUS_WORD, 0, &sw);
        return sw;
    }

    int32_t actualPosition() const
    {
        uint32_t raw = 0;
        SDO_Read32(m_nodeId, OD_POSITION_ACTUAL, 0, &raw);
        return static_cast<int32_t>(raw);
    }

    bool hasFault() const
    {
        return (statusWord() & SW_BIT_FAULT) != 0;
    }

    DS402_State_t state()
    {
        uint16_t sw = statusWord();
        m_state = DS402_DecodeState(sw);
        return m_state;
    }

private:
    uint8_t       m_nodeId;
    DS402_State_t m_state;
    DS402_Mode_t  m_mode = DS402_MODE_PP;

    void writeCW(uint16_t cw)
    {
        SDO_Write16(m_nodeId, OD_CONTROL_WORD, 0, cw);
    }
};

// ── Example application using the C++ wrapper ────────────────────

void ApplicationExample()
{
    DS402Drive drive(3);   /* node ID 3 */

    try {
        drive.enable();

        /* Home on current position */
        drive.setMode(DS402_MODE_HM);
        drive.home(35);

        /* Profile position moves */
        drive.setMode(DS402_MODE_PP);
        drive.configureProfile(20000, 100000, 100000);

        drive.moveAbsolute(50000, 5000);   /* to 50000, wait ≤5s  */
        drive.moveAbsolute(100000, 5000);  /* to 100000, wait ≤5s */
        drive.moveAbsolute(0, 10000);      /* back home, wait ≤10s */

        drive.disable();
    }
    catch (const std::runtime_error &e) {
        /* handle fault */
        if (drive.hasFault()) drive.faultReset();
    }
}
```

### 8.9 Error Code Retrieval

```c
/**
 * @brief  Read and decode the drive error code (0x603F).
 *         CiA 402 error codes follow IEC 61800-7.
 */
typedef struct {
    uint16_t    code;
    const char *description;
} DS402_ErrorEntry_t;

static const DS402_ErrorEntry_t k_ErrorTable[] = {
    { 0x0000, "No error" },
    { 0x1000, "Generic error" },
    { 0x2300, "Current at drive output too large" },
    { 0x3100, "DC link under-voltage" },
    { 0x3200, "DC link over-voltage" },
    { 0x4200, "Drive temperature" },
    { 0x5200, "Drive hardware fault" },
    { 0x6100, "Software reset / watchdog" },
    { 0x7300, "Encoder feedback fault" },
    { 0x7500, "Communication error (CAN)" },
    { 0x8100, "CAN overrun — lost RX message" },
    { 0x8110, "CAN overrun — lost RX PDO" },
    { 0x8120, "CAN in error passive mode" },
    { 0x8130, "Node guarding / heartbeat error" },
    { 0x8140, "CAN recovered from bus-off" },
    { 0xFF00, "Device specific" },
    { 0x0000, NULL }   /* sentinel */
};

const char *DS402_GetErrorDescription(uint8_t nodeId)
{
    uint16_t code = 0;
    if (SDO_Read16(nodeId, OD_ERROR_CODE, 0, &code) != SDO_OK)
        return "SDO read failed";

    for (const DS402_ErrorEntry_t *e = k_ErrorTable; e->description; e++) {
        if ((code & 0xFF00u) == (e->code & 0xFF00u)) return e->description;
    }
    return "Unknown error";
}
```

---

## 9. Summary

CiA 402 provides a **complete, interoperable framework** for controlling motion drives over CANopen and derivative fieldbus networks. The following table condenses the key elements:

| Topic | Key Detail |
|---|---|
| **State Machine** | 8 states. Mandatory sequence: Not Ready → Switch On Disabled → Ready → Switched On → Operation Enabled |
| **Fault Recovery** | Rising edge on Control Word bit 7 resets fault; returns to Switch On Disabled |
| **Control Word 0x6040** | 16-bit write object; bits 0–3 drive transitions; bit7 resets fault; mode bits in 4–6, 8 |
| **Status Word 0x6041** | 16-bit read object; bits 0–6 encode state; bit10 = Target Reached; bits 12–13 = homing result |
| **Drive Modes** | PP, PV, PT for profiled moves; HM for homing; CSP/CSV/CST for real-time synchronous control |
| **Profile Position (PP)** | Drive generates trapezoidal profile; master writes target and waits for bit10 |
| **Homing (HM)** | 35 standard methods; typical: fast search → slow approach → index pulse; result in SW bits 12–13 |
| **CSP** | Master owns trajectory; sends absolute position each SYNC; drive performs position following only |
| **Key Objects** | 0x6060 mode select, 0x607A target position, 0x6064 actual position, 0x6081–0x6084 profile parameters |
| **Error Codes** | 0x603F follows IEC 61800-7 standardised error code taxonomy |

### Design Recommendations

- Always poll **Modes of Operation Display (0x6061)** after writing 0x6060 to confirm mode acceptance before sending set-points.
- In PP mode, clear the **New Set-Point bit (bit4)** after Target Reached to prepare the drive for the next command.
- In CSP, ensure the **SYNC interval** matches the drive's configured **interpolation time period (0x60C2)**. A mismatch causes following errors or drive faults.
- Map **Control Word and Status Word to PDOs** in production systems to avoid SDO overhead in time-critical loops.
- Implement a **watchdog / heartbeat** on the CANopen network; most drives will perform a Quick Stop automatically if the SYNC or heartbeat is lost.
- Read **0x6502 (Supported Drive Modes)** at startup to discover which modes the connected drive actually supports before selecting a mode.

---

*Document based on CiA 402 v3.0 / IEC 61800-7-201. All object indices are in hexadecimal.*