# CiA 401 — Generic I/O Device Profile

**Structure:**
- Profile architecture with ASCII block diagrams showing the OD layout, signal paths, and PDO flow
- Full object dictionary table with all key index/sub-index entries and their roles

**Functional areas documented:**
- **Digital Inputs (0x6000)** — bank layout, bit addressing, C read example
- **Digital Outputs (0x6200)** — write path, error mode wiring, C write example
- **Analogue Inputs/Outputs (0x6401/0x6411)** — 16-bit normalised format, Q15 scaling
- **Polarity inversion** — XOR mechanism for both DI and DO
- **Filter/debounce** — per-bank ms counter algorithm in C
- **Interrupt on change** — rising/falling edge masks for DI; upper/lower/delta thresholds for AI
- **Scaling & engineering units** — offset + gain formula, CiA unit encoding table

**Code examples:**
- Flat C slave implementation (16DI/8DO) — full init, 1 ms task, failsafe handler, OD callback wiring
- C++ class wrapper (`Cia401IO`) — clean RTOS/embedded C++17 style
- Master-side SDO configuration and PDO handling in C

**Summary table** consolidates all objects, their indices, and behaviours in one quick-reference block.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Profile Architecture Overview](#2-profile-architecture-overview)
3. [Object Dictionary Layout](#3-object-dictionary-layout)
4. [Digital Inputs (0x6000–0x6003)](#4-digital-inputs)
5. [Digital Outputs (0x6200–0x6207)](#5-digital-outputs)
6. [Analogue Inputs (0x6401–0x6431)](#6-analogue-inputs)
7. [Analogue Outputs (0x6411–0x6443)](#7-analogue-outputs)
8. [Polarity Inversion](#8-polarity-inversion)
9. [Filter Time Constants](#9-filter-time-constants)
10. [Interrupt on Change](#10-interrupt-on-change)
11. [Scaling and Engineering Units](#11-scaling-and-engineering-units)
12. [PDO Mapping for I/O](#12-pdo-mapping-for-io)
13. [Complete 16-DI / 8-DO Slave Example in C](#13-complete-16-di--8-do-slave-example-in-c)
14. [C++ Class Wrapper Example](#14-c-class-wrapper-example)
15. [Master-Side Access Example](#15-master-side-access-example)
16. [Error Handling and Diagnostics](#16-error-handling-and-diagnostics)
17. [Summary](#17-summary)

---

## 1. Introduction

**CiA 401** (CANopen Device Profile for Generic I/O Modules) is a standardised
device profile defined by CAN in Automation (CiA). It specifies the Object
Dictionary (OD) entries, communication behaviour, and optional features for
devices that expose digital and analogue input/output channels over a CANopen
network.

CiA 401 is one of the oldest and most widely deployed CANopen profiles. It
applies to:

- Standalone I/O modules (DIN-rail, fieldbus couplers)
- Embedded controllers with I/O pins
- Safety I/O modules (combined with CiA 304)
- Mixed-signal devices (temperature, pressure, valve controllers)

### Why CiA 401 Matters

Without a common profile every vendor would invent their own OD layout. CiA 401
ensures that a generic I/O module from any manufacturer can be integrated by a
master that understands the profile — the master only needs to know the node-ID
and the device type (0x00000401).

```
  CANopen Network
  ================
  Master              Node 1           Node 2
  [PLC / EtherCAT]   [CiA-401 DI/DO]  [CiA-401 AI/AO]
       |                    |                |
       |<--- PDO (DI) ------|                |
       |---- PDO (DO) ----->|                |
       |<------- PDO (AI) ------------------ |
       |-------->SDO cfg-------------------> |
```

---

## 2. Profile Architecture Overview

```
  +----------------------------------------------------------+
  |                  CiA 401 Device                          |
  |                                                          |
  |  Physical Layer          Object Dictionary               |
  |  +-----------+           +----------------------------+  |
  |  | DI0..DI15 |---------->| 0x6000  Read DI bank 1     |  |
  |  | DO0..DO7  |<----------| 0x6200  Write DO bank 1    |  |
  |  | AI0..AI7  |---------->| 0x6401  AI values          |  |
  |  | AO0..AO3  |<----------| 0x6411  AO values          |  |
  |  +-----------+           +----------------------------+  |
  |                          |                            |  |
  |  Configuration           | 0x6002  Polarity DI        |  |
  |  +-----------+           | 0x6202  Polarity DO        |  |
  |  | Filter    |---------->| 0x6003  Filter DI          |  |
  |  | Polarity  |           | 0x6423  AI int-on-change   |  |
  |  | Scaling   |           | 0x6426  AI filter          |  |
  |  +-----------+           | 0x6431  AI scaling         |  |
  |                          +----------------------------+  |
  +----------------------------------------------------------+
            |  CAN Bus  |
  +---------+-----------+---------+
  | PDO1 TX (DI state)            |
  | PDO2 RX (DO command)          |
  | PDO3 TX (AI values)           |
  | SDO  (configuration access)   |
  +-------------------------------+
```

---

## 3. Object Dictionary Layout

The CiA 401 profile occupies a well-defined section of the Object Dictionary.

```
  Index    Sub  Access  Description
  -------  ---  ------  ------------------------------------------
  0x1000   00   RO      Device Type = 0x00000401
  0x1001   00   RO      Error Register
  0x6000   01   RO      Read Digital Inputs  Bank 1 (bits 0-7)
  0x6000   02   RO      Read Digital Inputs  Bank 2 (bits 8-15)
  0x6001   01   RO      Read Digital Inputs  Bank 3 (bits 16-23)
  0x6002   01   RW      Polarity   DI Bank 1
  0x6003   01   RW      Filter Time DI Bank 1  [ms]
  0x6005   00   RW      Global Interrupt Enable (DI)
  0x6006   01   RW      Interrupt Mask Rising  Edge Bank 1
  0x6007   01   RW      Interrupt Mask Falling Edge Bank 1
  0x6200   01   RW      Write Digital Outputs Bank 1 (bits 0-7)
  0x6202   01   RW      Polarity DO Bank 1
  0x6206   01   RW      Error Mode DO Bank 1 (0=hold, 1=set 0, 2=set 1)
  0x6207   01   RW      Error Value DO Bank 1
  0x6401   01   RO      Analogue Input Channel 1 value
  0x6401   02   RO      Analogue Input Channel 2 value
  ...
  0x6411   01   RW      Analogue Output Channel 1 value
  0x6423   01   RW      AI Interrupt Upper Limit Channel 1
  0x6424   01   RW      AI Interrupt Lower Limit Channel 1
  0x6425   01   RW      AI Interrupt Delta   Channel 1
  0x6426   01   RW      AI Filter Time Constant Channel 1 [ms]
  0x6431   01   RW      AI Offset Channel 1
  0x6432   01   RW      AI Gain   Channel 1
  0x6433   01   RW      AI Unit   Channel 1 (CiA encoding)
```

### Device Type Object (0x1000)

The least significant 16 bits must be 0x0401 for any CiA 401 device:

```
  Bits 31..16  Bits 15..0
  -----------  ----------
  Profile Ext  0x0401   <-- mandatory
```

---

## 4. Digital Inputs

### 4.1 Reading Digital Inputs — Object 0x6000

Each sub-index holds 8 digital input bits (one byte). For a 16-DI module:

```
  0x6000 sub1 : DI 0..7
  0x6000 sub2 : DI 8..15

  Bit layout inside each byte
  +---+---+---+---+---+---+---+---+
  | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
  +---+---+---+---+---+---+---+---+
    ^                           ^
    DI7 (sub1)                  DI0 (sub1)
```

Sub-index 0 always contains the number of available sub-indices (i.e., the
number of banks). This is the standard CANopen array convention.

### 4.2 C Read Example

```c
#include <stdint.h>

/* Simulated OD entry — on real hardware this is mapped to hardware registers */
static uint8_t di_bank[2];   /* bank[0] = DI0..7, bank[1] = DI8..15 */

/**
 * @brief  Read one digital input channel (0-based index).
 * @param  channel  0 .. 15
 * @return 0 or 1, or -1 on error
 */
int cia401_di_read(uint8_t channel)
{
    if (channel > 15) return -1;
    uint8_t bank = channel / 8;
    uint8_t bit  = channel % 8;
    return (di_bank[bank] >> bit) & 0x01;
}

/**
 * @brief  Update the DI shadow registers from hardware (call from ISR or task).
 */
void cia401_di_update_from_hw(void)
{
    /* Platform specific — read GPIO port registers */
    di_bank[0] = GPIO_ReadPort(GPIO_PORT_DI_LOW);   /* DI0..7  */
    di_bank[1] = GPIO_ReadPort(GPIO_PORT_DI_HIGH);  /* DI8..15 */
}
```

---

## 5. Digital Outputs

### 5.1 Writing Digital Outputs — Object 0x6200

Object 0x6200 follows the same byte-per-bank layout as 0x6000. Writing a byte
sets the physical outputs. The "error mode" and "error value" objects (0x6206,
0x6207) define the safe state when a node guard timeout or RPDO timeout occurs.

```
  Write Path:
  Master SDO/PDO write --> 0x6200 sub1 --> DO driver --> Physical pins
                                     ^
                              Polarity XOR (0x6202)
                                     |
                              Error hold/clear/set (0x6206)
```

### 5.2 C Write Example

```c
static uint8_t do_bank[1];      /* bank[0] = DO0..7 */
static uint8_t do_polarity[1];  /* from 0x6202      */
static uint8_t do_err_mode[1];  /* from 0x6206      */
static uint8_t do_err_val[1];   /* from 0x6207      */

/**
 * @brief  Write a complete DO bank (8 channels at once).
 *         This mirrors what happens when an RPDO updates 0x6200.
 * @param  bank    Bank index (0-based)
 * @param  value   8-bit bitmask of desired output states (after user logic)
 */
void cia401_do_write_bank(uint8_t bank, uint8_t value)
{
    if (bank > 0) return; /* only one bank in this example */

    /* Apply polarity inversion */
    uint8_t hw_value = value ^ do_polarity[bank];
    do_bank[bank]    = value;

    /* Drive physical outputs */
    GPIO_WritePort(GPIO_PORT_DO, hw_value);
}

/**
 * @brief  Called on communication error (NMT, node guard timeout).
 *         Applies the error behaviour defined in 0x6206 / 0x6207.
 */
void cia401_do_apply_error_state(void)
{
    uint8_t hw_value;
    switch (do_err_mode[0]) {
        case 0:  hw_value = do_bank[0];        break; /* hold last value */
        case 1:  hw_value = 0x00;              break; /* set all outputs 0 */
        case 2:  hw_value = do_err_val[0];     break; /* set to configured value */
        default: hw_value = 0x00;              break;
    }
    GPIO_WritePort(GPIO_PORT_DO, hw_value ^ do_polarity[0]);
}
```

---

## 6. Analogue Inputs

### 6.1 Object 0x6401 — AI Values

Each sub-index (1..n) holds one 16-bit signed integer representing the raw ADC
reading. The CiA 401 profile defines a normalised representation:

```
  +32767  = +Full Scale
       0  = Mid Scale (for bipolar ranges, e.g. ±10 V)
  -32768  = -Full Scale

  Example: 0..10 V  --> 0x0000 .. 0x7FFF
           -10..+10 V --> 0x8000 .. 0x7FFF
```

### 6.2 C AI Read and Update Example

```c
#include <stdint.h>

#define AI_CHANNELS  8

static int16_t  ai_raw[AI_CHANNELS];   /* OD 0x6401 sub1..sub8    */
static int32_t  ai_offset[AI_CHANNELS]; /* OD 0x6431               */
static int32_t  ai_gain[AI_CHANNELS];   /* OD 0x6432 (Q15 fixed pt)*/

/**
 * @brief  Read one AI channel value (with offset and gain applied).
 *         Returns scaled value in user units (e.g. millivolts).
 */
int32_t cia401_ai_read_scaled(uint8_t ch)
{
    if (ch >= AI_CHANNELS) return 0;
    /* gain is stored as Q15: 1.0 = 32768 */
    int32_t scaled = (int32_t)ai_raw[ch] * ai_gain[ch] / 32768;
    return scaled + ai_offset[ch];
}

/**
 * @brief  Sample all ADC channels and store into the OD shadow array.
 *         Should be called periodically (e.g. every 1 ms from a timer ISR).
 */
void cia401_ai_sample(void)
{
    for (uint8_t ch = 0; ch < AI_CHANNELS; ch++) {
        /* Platform-specific 16-bit ADC read */
        ai_raw[ch] = ADC_Read16(ch);
    }
}
```

---

## 7. Analogue Outputs

### 7.1 Object 0x6411 — AO Values

Same 16-bit signed integer format as AI. Writing sub-index 1..n drives the
corresponding DAC channel. The device applies scaling and offset before
converting the digital value to a physical voltage or current.

```
  Master RPDO --> 0x6411 sub1 --> [Offset + Gain] --> DAC --> Physical pin
```

### 7.2 C AO Write Example

```c
#define AO_CHANNELS 4

static int16_t ao_value[AO_CHANNELS];  /* OD 0x6411 sub1..sub4 */

/**
 * @brief  Write one AO channel.
 *         Called when an RPDO or SDO updates 0x6411.
 */
void cia401_ao_write(uint8_t ch, int16_t value)
{
    if (ch >= AO_CHANNELS) return;
    ao_value[ch] = value;
    /* Platform-specific DAC write, scale to 0..4095 for a 12-bit DAC */
    uint16_t dac = (uint16_t)((int32_t)(value + 32768) * 4095 / 65535);
    DAC_Write(ch, dac);
}
```

---

## 8. Polarity Inversion

Polarity inversion allows the logical sense of a digital channel to be inverted
in software without rewiring. It is controlled by:

| Object  | Description                        |
|---------|------------------------------------|
| 0x6002  | DI polarity mask, one bit per DI   |
| 0x6202  | DO polarity mask, one bit per DO   |

A bit value of **1** means the channel is inverted; **0** means normal polarity.

```
  Physical pin HIGH  --[XOR with polarity bit 1]--> logical 0
  Physical pin LOW   --[XOR with polarity bit 1]--> logical 1
  Physical pin HIGH  --[XOR with polarity bit 0]--> logical 1
```

### C Implementation

```c
static uint8_t di_polarity[2];  /* OD 0x6002 sub1..sub2 */

/**
 * @brief  Read DI bank with polarity applied.
 */
uint8_t cia401_di_read_bank(uint8_t bank)
{
    if (bank > 1) return 0;
    return di_bank[bank] ^ di_polarity[bank];
}
```

---

## 9. Filter Time Constants

Hardware inputs often bounce or carry noise. CiA 401 defines per-bank filter
time constants stored as 16-bit unsigned integers in milliseconds.

| Object  | Description                                 |
|---------|---------------------------------------------|
| 0x6003  | DI filter time (ms) per bank                |
| 0x6426  | AI filter time constant (ms) per channel    |

A value of 0 means no filtering. The device may implement this as a simple
debounce counter or a low-pass IIR filter.

```
  DI signal (bouncy)
  +------+  +--+  +----+
         |  |  |  |
         +--+  +--+
  Filter output (clean, delayed by filter_ms)
  +--------------------+
                        |
                        +-----
```

### C Debounce Implementation

```c
#define DI_BANKS      2
#define DI_TASK_MS    1        /* task period in ms */

static uint16_t di_filter_ms[DI_BANKS];  /* OD 0x6003 */
static uint16_t di_counter[DI_BANKS][8]; /* per-bit counter */
static uint8_t  di_stable[DI_BANKS];     /* debounced state  */

/**
 * @brief  Run debounce algorithm on raw DI sample.
 *         Call once per DI_TASK_MS milliseconds.
 */
void cia401_di_debounce(void)
{
    for (uint8_t bank = 0; bank < DI_BANKS; bank++) {
        uint8_t raw = di_bank[bank] ^ di_polarity[bank];
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint8_t new_state = (raw >> bit) & 1;
            uint8_t cur_state = (di_stable[bank] >> bit) & 1;
            if (new_state != cur_state) {
                di_counter[bank][bit] += DI_TASK_MS;
                if (di_counter[bank][bit] >= di_filter_ms[bank]) {
                    /* State confirmed stable — toggle */
                    di_stable[bank] ^= (1u << bit);
                    di_counter[bank][bit] = 0;
                }
            } else {
                di_counter[bank][bit] = 0;
            }
        }
    }
}
```

---

## 10. Interrupt on Change

CiA 401 defines a configurable interrupt-on-change (IoC) mechanism that causes
the device to send a PDO (or trigger an EMCY object) whenever a digital input
changes state, or an analogue input crosses a threshold.

### 10.1 Digital Input IoC

| Object  | Description                                    |
|---------|------------------------------------------------|
| 0x6005  | Global interrupt enable (0=disabled, 1=enabled)|
| 0x6006  | Rising-edge interrupt mask, one bit per DI     |
| 0x6007  | Falling-edge interrupt mask, one bit per DI    |

```
  DI state change detected
         |
  Is global interrupt enabled (0x6005)?
         |  Yes
  Is rising/falling mask bit set (0x6006/0x6007)?
         |  Yes
  Trigger event-driven PDO transmission (TPDO)
```

### 10.2 Analogue Input IoC

| Object  | Description                                  |
|---------|----------------------------------------------|
| 0x6423  | AI interrupt upper limit per channel         |
| 0x6424  | AI interrupt lower limit per channel         |
| 0x6425  | AI interrupt delta (deadband) per channel    |

The delta prevents chattering when the signal hovers near a threshold.

```
  AI value
    |
    |  Upper limit 0x6423 ............ -------- trigger if > upper
    |                              /
    |                             /   Deadband (delta 0x6425)
    |                            /
    |  Lower limit 0x6424 ....../ --------- trigger if < lower
    |
    +-- Time -->
```

### 10.3 C IoC Implementation

```c
static uint8_t  di_irq_enable;           /* OD 0x6005 */
static uint8_t  di_irq_rise[DI_BANKS];  /* OD 0x6006 */
static uint8_t  di_irq_fall[DI_BANKS];  /* OD 0x6007 */
static uint8_t  di_prev_stable[DI_BANKS]; /* previous stable state */

/**
 * @brief  Check for edge events and trigger PDO if configured.
 *         Call after cia401_di_debounce().
 */
void cia401_di_check_interrupt(void)
{
    if (!di_irq_enable) return;

    for (uint8_t bank = 0; bank < DI_BANKS; bank++) {
        uint8_t changed = di_stable[bank] ^ di_prev_stable[bank];
        if (!changed) continue;

        uint8_t rose = changed &  di_stable[bank] &  di_irq_rise[bank];
        uint8_t fell = changed & ~di_stable[bank] &  di_irq_fall[bank];

        if (rose || fell) {
            /* Trigger event-driven TPDO — implementation calls CANopen stack */
            CANopen_TPDO_Trigger(TPDO_DI_EVENT);
        }
        di_prev_stable[bank] = di_stable[bank];
    }
}
```

---

## 11. Scaling and Engineering Units

Analogue channels can be mapped to engineering units (volts, milliamps,
degrees Celsius, etc.) using the offset and gain objects.

| Object  | Description                              |
|---------|------------------------------------------|
| 0x6431  | AI offset, signed 32-bit, per channel    |
| 0x6432  | AI gain,   Q15 fixed-point, per channel  |
| 0x6433  | AI unit identifier (CiA encoding)        |
| 0x6441  | AO offset, per channel                   |
| 0x6442  | AO gain, per channel                     |

The conversion formula is:

```
  Scaled = (Raw * Gain / 32768) + Offset
```

Where **Gain = 32768** corresponds to a factor of 1.0.

### CiA Unit Encoding Examples (Object 0x6433)

```
  Value  Unit
  -----  ---------
  0x01   mV
  0x02   V
  0x04   mA
  0x10   °C
  0x11   °F
  0x30   Hz
```

### C Scaling Example

```c
/**
 * @brief  Convert raw AI count to millivolts for a 0..10 V range.
 *         Assumes gain=32768 (1.0), offset=0.
 *         Raw 0x7FFF maps to 10000 mV.
 */
int32_t cia401_raw_to_mv(int16_t raw)
{
    /* 10000 mV / 32767 counts * raw */
    return (int32_t)raw * 10000L / 32767L;
}

/**
 * @brief  Convert millivolts to raw AI count (for AO or limit comparison).
 */
int16_t cia401_mv_to_raw(int32_t mv)
{
    return (int16_t)(mv * 32767L / 10000L);
}
```

---

## 12. PDO Mapping for I/O

CiA 401 recommends specific default PDO mappings to minimise configuration
effort for generic masters.

### Default TPDO1 (Node transmits DI state)

```
  TPDO1 COB-ID: 0x180 + NodeID
  Transmission type: event-driven (on change) or cyclic

  Byte  Object         Description
  ----  -------------  ----------------------------
   0    0x6000 sub1    DI Bank 1 (channels 0..7)
   1    0x6000 sub2    DI Bank 2 (channels 8..15)
```

### Default RPDO1 (Node receives DO command)

```
  RPDO1 COB-ID: 0x200 + NodeID
  Transmission type: synchronous or event

  Byte  Object         Description
  ----  -------------  ----------------------------
   0    0x6200 sub1    DO Bank 1 (channels 0..7)
```

### ASCII PDO Flow Diagram

```
  Master                        Node (CiA 401)
  ======                        ==============
  |                                          |
  |--SYNC (0x80)------------------------>    |
  |                               sample DI  |
  |<--TPDO1 (0x180+NodeID) [DI0..7, DI8..15] |
  |                                          |
  |--RPDO1 (0x200+NodeID) [DO0..7]-------->  |
  |                               apply DO   |
  |                                          |
  |--SDO Wr 0x6003/01 = 5 ms (DI filter)-->  |
  |<--SDO ACK------------------------------  |
```

---

## 13. Complete 16-DI / 8-DO Slave Example in C

This section shows a minimal but complete CiA 401 slave firmware skeleton for
a device with 16 digital inputs and 8 digital outputs. It is based on a generic
CANopen stack API (e.g. CANopenNode).

### 13.1 Object Dictionary Definitions

```c
/* cia401_od.h — Object Dictionary entries for CiA 401 16DI/8DO */

#ifndef CIA401_OD_H
#define CIA401_OD_H

#include <stdint.h>

/* ----------------------------------------------------------------
 * CiA 401 data structures mapped into the OD
 * ---------------------------------------------------------------- */

/* Digital Inputs — OD 0x6000 */
extern uint8_t OD_DI_bank[2];      /* sub1 = bank0, sub2 = bank1   */

/* Digital Input configuration */
extern uint8_t OD_DI_polarity[2];  /* OD 0x6002                    */
extern uint16_t OD_DI_filter_ms[2];/* OD 0x6003 [ms]               */
extern uint8_t OD_DI_irq_en;       /* OD 0x6005                    */
extern uint8_t OD_DI_irq_rise[2];  /* OD 0x6006                    */
extern uint8_t OD_DI_irq_fall[2];  /* OD 0x6007                    */

/* Digital Outputs — OD 0x6200 */
extern uint8_t OD_DO_bank[1];      /* sub1 = bank0                 */
extern uint8_t OD_DO_polarity[1];  /* OD 0x6202                    */
extern uint8_t OD_DO_err_mode[1];  /* OD 0x6206: 0=hold,1=0,2=val  */
extern uint8_t OD_DO_err_val[1];   /* OD 0x6207                    */

#endif /* CIA401_OD_H */
```

### 13.2 Slave Main Module

```c
/* cia401_slave.c — 16DI / 8DO CiA 401 slave implementation */

#include "cia401_od.h"
#include "canopen_stack.h"     /* platform CANopen stack API      */
#include "platform_gpio.h"     /* board GPIO abstraction          */
#include <string.h>

/* ----------------------------------------------------------------
 * OD data (definitions)
 * ---------------------------------------------------------------- */
uint8_t  OD_DI_bank[2]        = {0, 0};
uint8_t  OD_DI_polarity[2]    = {0, 0};
uint16_t OD_DI_filter_ms[2]   = {5, 5};   /* 5 ms debounce default */
uint8_t  OD_DI_irq_en         = 1;
uint8_t  OD_DI_irq_rise[2]    = {0xFF, 0xFF}; /* all channels enabled */
uint8_t  OD_DI_irq_fall[2]    = {0xFF, 0xFF};
uint8_t  OD_DO_bank[1]        = {0};
uint8_t  OD_DO_polarity[1]    = {0};
uint8_t  OD_DO_err_mode[1]    = {1};        /* safe = outputs off  */
uint8_t  OD_DO_err_val[1]     = {0};

/* ----------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------- */
#define DI_BANKS   2
#define DO_BANKS   1
#define TASK_MS    1

static uint16_t deb_cnt[DI_BANKS][8];
static uint8_t  di_stable[DI_BANKS];
static uint8_t  di_prev[DI_BANKS];

/* ----------------------------------------------------------------
 * Hardware abstraction (implement per board)
 * ---------------------------------------------------------------- */
static inline uint8_t hw_di_read(uint8_t bank)
{
    return (bank == 0) ? GPIO_ReadByte(GPIO_DI_PORT_LOW)
                       : GPIO_ReadByte(GPIO_DI_PORT_HIGH);
}

static inline void hw_do_write(uint8_t bank, uint8_t value)
{
    (void)bank;
    GPIO_WriteByte(GPIO_DO_PORT, value);
}

/* ----------------------------------------------------------------
 * Debounce & polarity
 * ---------------------------------------------------------------- */
static void di_process(void)
{
    for (uint8_t b = 0; b < DI_BANKS; b++) {
        uint8_t raw = hw_di_read(b) ^ OD_DI_polarity[b];
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint8_t new_s = (raw        >> bit) & 1;
            uint8_t cur_s = (di_stable[b] >> bit) & 1;
            if (new_s != cur_s) {
                deb_cnt[b][bit] += TASK_MS;
                if (deb_cnt[b][bit] >= OD_DI_filter_ms[b]) {
                    di_stable[b] ^= (1u << bit);
                    deb_cnt[b][bit] = 0;
                }
            } else {
                deb_cnt[b][bit] = 0;
            }
        }
        OD_DI_bank[b] = di_stable[b]; /* update OD */
    }
}

/* ----------------------------------------------------------------
 * Interrupt-on-change
 * ---------------------------------------------------------------- */
static void di_check_ioc(void)
{
    if (!OD_DI_irq_en) return;
    bool trigger = false;
    for (uint8_t b = 0; b < DI_BANKS; b++) {
        uint8_t changed = di_stable[b] ^ di_prev[b];
        if (!changed) continue;
        uint8_t rose = changed &  di_stable[b] & OD_DI_irq_rise[b];
        uint8_t fell = changed & ~di_stable[b] & OD_DI_irq_fall[b];
        if (rose || fell) trigger = true;
        di_prev[b] = di_stable[b];
    }
    if (trigger) {
        CANopen_TPDO_Trigger(TPDO_INDEX_DI); /* request async TX */
    }
}

/* ----------------------------------------------------------------
 * DO update (called when RPDO1 is received or SDO writes 0x6200)
 * ---------------------------------------------------------------- */
void cia401_do_update(uint8_t bank, uint8_t value)
{
    if (bank >= DO_BANKS) return;
    OD_DO_bank[bank] = value;
    hw_do_write(bank, value ^ OD_DO_polarity[bank]);
}

/* ----------------------------------------------------------------
 * Error / failsafe handler (called by CANopen stack on NMT timeout)
 * ---------------------------------------------------------------- */
void cia401_do_failsafe(void)
{
    for (uint8_t b = 0; b < DO_BANKS; b++) {
        uint8_t safe;
        switch (OD_DO_err_mode[b]) {
            case 1:  safe = 0x00;              break;
            case 2:  safe = OD_DO_err_val[b];  break;
            default: safe = OD_DO_bank[b];     break; /* hold */
        }
        hw_do_write(b, safe ^ OD_DO_polarity[b]);
    }
}

/* ----------------------------------------------------------------
 * 1 ms periodic task — call from timer ISR or RTOS task
 * ---------------------------------------------------------------- */
void cia401_task_1ms(void)
{
    di_process();
    di_check_ioc();
}

/* ----------------------------------------------------------------
 * Initialisation
 * ---------------------------------------------------------------- */
void cia401_init(void)
{
    memset(deb_cnt,   0, sizeof(deb_cnt));
    memset(di_stable, 0, sizeof(di_stable));
    memset(di_prev,   0, sizeof(di_prev));

    GPIO_Init(GPIO_DI_PORT_LOW,  GPIO_MODE_INPUT);
    GPIO_Init(GPIO_DI_PORT_HIGH, GPIO_MODE_INPUT);
    GPIO_Init(GPIO_DO_PORT,      GPIO_MODE_OUTPUT);

    /* Register OD callbacks with the CANopen stack */
    CANopen_OD_Register(0x6200, 0x01, OD_ATTR_RW, OD_TYPE_U8,
                        &OD_DO_bank[0], cia401_do_update_cb);
}
```

### 13.3 OD Callback Wiring (stack glue)

```c
/* Called by the CANopen stack when an SDO or RPDO writes 0x6200/sub1 */
void cia401_do_update_cb(uint16_t index, uint8_t subindex, uint32_t value)
{
    (void)index;
    cia401_do_update(subindex - 1, (uint8_t)value);
}
```

---

## 14. C++ Class Wrapper Example

For embedded C++ projects (e.g. with mbed, Zephyr + C++17, or Arduino), a
class wrapper provides cleaner integration.

```cpp
/* Cia401IO.hpp */
#pragma once
#include <cstdint>
#include <functional>

class Cia401IO {
public:
    using TpdoTriggerFn = std::function<void(int pdoIndex)>;

    explicit Cia401IO(TpdoTriggerFn trigger)
        : tpdoTrigger_(trigger)
    {
        /* Set safe defaults */
        diFilter_ms_[0] = diFilter_ms_[1] = 5;
        diPolarity_[0]  = diPolarity_[1]  = 0;
        doPolarity_[0]  = 0;
        doErrMode_[0]   = 1; /* safe = all off */
        doErrVal_[0]    = 0;
        diIrqEn_        = true;
    }

    /* --- Configuration setters (called via SDO) --- */
    void setDiFilter(uint8_t bank, uint16_t ms)   { diFilter_ms_[bank] = ms; }
    void setDiPolarity(uint8_t bank, uint8_t mask) { diPolarity_[bank] = mask; }
    void setDoPolarity(uint8_t bank, uint8_t mask) { doPolarity_[bank] = mask; }
    void setDoErrMode(uint8_t bank, uint8_t mode)  { doErrMode_[bank]  = mode; }
    void setDoErrVal (uint8_t bank, uint8_t val)   { doErrVal_[bank]   = val;  }

    /* --- Periodic task (1 ms) --- */
    void tick1ms() {
        sampleHardware();
        debounce();
        checkIoC();
    }

    /* --- DO write (from RPDO or SDO) --- */
    void writeDO(uint8_t bank, uint8_t value) {
        doBank_[bank] = value;
        hwWriteDO(bank, value ^ doPolarity_[bank]);
    }

    /* --- Failsafe --- */
    void applyFailsafe() {
        for (uint8_t b = 0; b < DO_BANKS; b++) {
            uint8_t safe = (doErrMode_[b] == 1) ? 0x00 :
                           (doErrMode_[b] == 2) ? doErrVal_[b] : doBank_[b];
            hwWriteDO(b, safe ^ doPolarity_[b]);
        }
    }

    /* --- OD access --- */
    uint8_t getDiBank(uint8_t bank) const { return diStable_[bank]; }
    uint8_t getDOBank(uint8_t bank) const { return doBank_[bank];   }

private:
    static constexpr uint8_t DI_BANKS = 2;
    static constexpr uint8_t DO_BANKS = 1;
    static constexpr uint8_t TASK_MS  = 1;

    TpdoTriggerFn tpdoTrigger_;
    bool          diIrqEn_;
    uint16_t      diFilter_ms_[DI_BANKS];
    uint8_t       diPolarity_[DI_BANKS];
    uint8_t       doPolarity_[DO_BANKS];
    uint8_t       doErrMode_[DO_BANKS];
    uint8_t       doErrVal_[DO_BANKS];

    uint8_t  diRaw_[DI_BANKS]    = {};
    uint8_t  diStable_[DI_BANKS] = {};
    uint8_t  diPrev_[DI_BANKS]   = {};
    uint8_t  doBank_[DO_BANKS]   = {};
    uint16_t debCnt_[DI_BANKS][8] = {};

    void sampleHardware() {
        for (uint8_t b = 0; b < DI_BANKS; b++)
            diRaw_[b] = hwReadDI(b) ^ diPolarity_[b];
    }

    void debounce() {
        for (uint8_t b = 0; b < DI_BANKS; b++) {
            for (uint8_t bit = 0; bit < 8; bit++) {
                uint8_t ns = (diRaw_[b]    >> bit) & 1;
                uint8_t cs = (diStable_[b] >> bit) & 1;
                if (ns != cs) {
                    if ((debCnt_[b][bit] += TASK_MS) >= diFilter_ms_[b]) {
                        diStable_[b] ^= (1u << bit);
                        debCnt_[b][bit] = 0;
                    }
                } else {
                    debCnt_[b][bit] = 0;
                }
            }
        }
    }

    void checkIoC() {
        if (!diIrqEn_) return;
        for (uint8_t b = 0; b < DI_BANKS; b++) {
            uint8_t changed = diStable_[b] ^ diPrev_[b];
            if (changed) {
                tpdoTrigger_(0); /* trigger TPDO1 */
                diPrev_[b] = diStable_[b];
            }
        }
    }

    /* Platform hooks — implement per board */
    uint8_t hwReadDI(uint8_t bank);
    void    hwWriteDO(uint8_t bank, uint8_t value);
};
```

---

## 15. Master-Side Access Example

A CANopen master (e.g. a PLC or Linux master using SocketCAN + CANopenNode)
configures and reads a CiA 401 slave via SDO and PDO.

### 15.1 SDO Configuration on Startup

```c
/* master_cia401_config.c
 * Configure a CiA 401 slave at node-ID 3 during boot.
 */

#include "canopen_master.h"

#define NODE_ID   3

void configure_io_node(void)
{
    /* Set DI filter to 10 ms for both banks */
    SDO_Write(NODE_ID, 0x6003, 0x01, sizeof(uint16_t), 10);
    SDO_Write(NODE_ID, 0x6003, 0x02, sizeof(uint16_t), 10);

    /* Invert polarity on DI0 and DI4 (bank 1 bitmask = 0x11) */
    SDO_Write(NODE_ID, 0x6002, 0x01, sizeof(uint8_t), 0x11);

    /* Enable interrupt on rising edge for all DI bank 1 */
    SDO_Write(NODE_ID, 0x6005, 0x00, sizeof(uint8_t), 1);   /* global en  */
    SDO_Write(NODE_ID, 0x6006, 0x01, sizeof(uint8_t), 0xFF);/* all rising */

    /* Set DO error mode: safe = all off (mode 1) */
    SDO_Write(NODE_ID, 0x6206, 0x01, sizeof(uint8_t), 1);

    /* Configure PDO: RPDO1 transmit DOs, TPDO1 receive DIs */
    /* (PDO mapping typically pre-configured via EDS/DCF)    */
}
```

### 15.2 Runtime PDO Handling

```c
/* Called when TPDO1 from node 3 is received (DI state update) */
void on_tpdo_received(uint8_t node_id, uint8_t *data, uint8_t len)
{
    if (node_id != NODE_ID || len < 2) return;

    uint8_t di_low  = data[0]; /* DI 0..7  */
    uint8_t di_high = data[1]; /* DI 8..15 */

    /* Update application logic */
    app_process_di(di_low, di_high);
}

/* Send a DO command to node 3 via RPDO1 */
void send_do_command(uint8_t do_mask)
{
    uint8_t rpdo_data[1] = { do_mask };
    CANopen_RPDO_Send(NODE_ID, RPDO_INDEX_DO, rpdo_data, 1);
}
```

---

## 16. Error Handling and Diagnostics

CiA 401 integrates with the standard CANopen error framework.

### 16.1 Error Register (0x1001)

```
  Bit  Meaning
  ---  ----------------------------------------
   0   Generic error
   1   Current error
   2   Voltage error
   3   Temperature error
   4   Communication error
   5   Device profile (CiA 401) specific error
   6   Reserved
   7   Manufacturer specific error
```

### 16.2 Emergency Object

When a hardware fault is detected (e.g. short circuit on a DO), the device
transmits an Emergency (EMCY) message:

```
  EMCY COB-ID: 0x80 + NodeID
  Bytes: [ErrorCode(2)] [ErrorRegister(1)] [ManufSpecific(5)]

  Example: Short circuit on DO channel 2
  ErrorCode = 0x3210 (output stage short circuit)
  ErrorReg  = 0x20   (device profile specific)
  ManSpec   = [0x02, 0x00, 0x00, 0x00, 0x00]  (channel 2)
```

```c
/* Send EMCY on DO short circuit */
void cia401_report_do_fault(uint8_t channel)
{
    uint8_t man_spec[5] = { channel, 0, 0, 0, 0 };
    CANopen_EMCY_Send(0x3210, 0x20, man_spec);

    /* Update error register */
    OD_ErrorRegister |= 0x20; /* bit 5: profile specific */
}
```

---

## 17. Summary

```
  +-----------------------------------------------------------------+
  |                  CiA 401 Feature Summary                        |
  +--------------------+--------------------------------------------+
  | Feature            | Detail                                     |
  +--------------------+--------------------------------------------+
  | Profile ID         | 0x00000401 in object 0x1000                |
  | Digital Inputs     | 0x6000, 8 DI per sub-index (per bank)      |
  | Digital Outputs    | 0x6200, 8 DO per sub-index                 |
  | Analogue Inputs    | 0x6401, 16-bit signed, one sub per channel |
  | Analogue Outputs   | 0x6411, 16-bit signed, one sub per channel |
  | DI Polarity        | 0x6002, XOR per bit, per bank              |
  | DO Polarity        | 0x6202, XOR per bit, per bank              |
  | DI Filter          | 0x6003, ms debounce, per bank              |
  | AI Filter          | 0x6426, ms IIR, per channel                |
  | DI Interrupt       | 0x6005/6006/6007, rising/falling per bit   |
  | AI Interrupt       | 0x6423/6424/6425, upper/lower/delta limits |
  | AI Scaling         | 0x6431 offset + 0x6432 Q15 gain            |
  | DO Failsafe        | 0x6206 mode + 0x6207 value                 |
  | PDO defaults       | TPDO1=DI state, RPDO1=DO command           |
  | Error reporting    | 0x1001 register + EMCY 0x80+NodeID         |
  +--------------------+--------------------------------------------+
```

**Key design principles when implementing CiA 401:**

- Always implement the device type object (0x1000) correctly; it is the first
  thing a generic master checks.
- Polarity inversion is applied at the OD boundary — the raw hardware value and
  the logical OD value differ by the polarity XOR.
- Debounce (filter) runs on the raw hardware values *before* the polarity is
  applied to avoid artefacts.
- Interrupt-on-change should use the post-debounce, post-polarity stable value
  so that noise does not generate spurious PDOs.
- The DO failsafe state must be applied as soon as the CANopen stack signals a
  communication error (NMT heartbeat loss, guard timeout, or RPDO timeout).
- Analogue scaling (offset + gain) should be applied *after* the raw ADC value
  is stored in the OD so that the master can also read the raw value if desired
  (by setting gain=32768 and offset=0 for bypass mode).

CiA 401 is intentionally minimal and flexible. Its power lies in the consistent
addressing scheme that allows any CANopen master to discover and configure I/O
nodes without vendor-specific knowledge, provided the master holds the device's
Electronic Data Sheet (EDS) file.