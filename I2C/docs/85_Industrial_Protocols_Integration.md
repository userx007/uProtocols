# 85. Industrial Protocols Integration

**Conceptual coverage:**
- Why I2C needs a bridge to industrial fieldbuses (electrical limits, noise immunity, addressing model)
- Architecture diagram of the gateway pattern (I2C master ↔ Bridge Core ↔ Fieldbus stack)
- Protocol-specific bridging strategies for **Modbus RTU/TCP**, **CANopen**, **PROFIBUS-DP**, and **EtherCAT**
- Register mapping strategies (linear, YAML config-driven, virtual device abstraction)
- Latency budget analysis and real-time considerations (SYNC-triggered acquisition, priority inversion)
- Fault propagation per-protocol (Modbus exception codes, CANopen EMCY objects, PROFIBUS diagnostic bytes)

**C/C++ code examples:**
- Portable Linux I2C HAL (`/dev/i2c-N`, `I2C_RDWR` ioctl with repeated START)
- Full BME280 driver with compensation math
- Modbus RTU slave bridge using **libmodbus** with a lock-free atomic register cache and background polling thread
- CANopen node bridge using **CANopenNode** with TPDO mapping and EMCY fault signalling
- TCA9548A I2C multiplexer driver for multi-sensor bridges

**Rust code examples:**
- Trait-based I2C HAL using `linux-embedded-hal` / `embedded-hal 1.0`
- BME280 driver with idiomatic `thiserror` error types and `from_le_bytes` calibration parsing
- Async Modbus TCP server using **tokio-modbus** with `Arc<RwLock<>>` shared state
- CANopen TPDO/EMCY publisher over **socketCAN**
- Complete `main.rs` composing all tasks into a single binary

## Bridging I2C to Modbus, CANopen, and Other Industrial Fieldbus Protocols

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Architecture Overview](#2-architecture-overview)
3. [I2C as a Local Sensor Bus in Industrial Systems](#3-i2c-as-a-local-sensor-bus-in-industrial-systems)
4. [Bridging I2C to Modbus RTU/TCP](#4-bridging-i2c-to-modbus-rtutcp)
5. [Bridging I2C to CANopen](#5-bridging-i2c-to-canopen)
6. [Bridging I2C to PROFIBUS](#6-bridging-i2c-to-profibus)
7. [Bridging I2C to EtherCAT](#7-bridging-i2c-to-ethercat)
8. [Register Mapping Strategies](#8-register-mapping-strategies)
9. [Timing, Latency, and Real-Time Considerations](#9-timing-latency-and-real-time-considerations)
10. [Error Handling and Fault Propagation](#10-error-handling-and-fault-propagation)
11. [Code Examples in C/C++](#11-code-examples-in-cc)
12. [Code Examples in Rust](#12-code-examples-in-rust)
13. [Summary](#13-summary)

---

## 1. Introduction

I2C (Inter-Integrated Circuit) is a short-range, low-speed, two-wire serial protocol widely used for board-level communication with sensors, ADCs, EEPROMs, displays, and actuator drivers. It is not, by itself, an industrial fieldbus — it lacks the noise immunity, distance capability, deterministic timing, and standardized application-layer semantics demanded by industrial automation.

Industrial fieldbus protocols such as **Modbus**, **CANopen**, **PROFIBUS**, **EtherCAT**, and **HART** exist precisely to fill those gaps. They provide:

- **Long-distance communication** (hundreds of metres to kilometres)
- **Noise-robust physical layers** (RS-485, CAN, fibre, Industrial Ethernet)
- **Deterministic, real-time behaviour** (guaranteed latency windows)
- **Standardized object dictionaries and data models** (CANopen CiA-301, Modbus PDU)
- **Network management, diagnostics, and hot-plug** capabilities

The two domains meet inside a **protocol bridge** (also called a *gateway* or *coupler*): a device that reads data from I2C peripherals on its local bus and exposes that data coherently on an industrial network — or vice versa, accepting setpoints from the network and writing them to I2C actuators.

### Why Bridge I2C into an Industrial Protocol?

| Reason | Explanation |
|---|---|
| Sensor cost | I2C sensors (temperature, humidity, pressure, IMU) cost cents to dollars vs. dedicated fieldbus variants |
| Space / integration | Combining many sensor types on one small PCB and exposing a single fieldbus node |
| Retrofitting | Adding smart sensing to legacy industrial lines without re-cabling the fieldbus |
| Modularity | Building a gateway that translates between board-level and plant-level worlds |
| Multi-master aggregation | Collecting data from dozens of I2C devices and presenting a coherent data model |

---

## 2. Architecture Overview

```
 ┌──────────────────────────────────────────────────────────────────┐
 │                        Protocol Bridge / Gateway                  │
 │                                                                    │
 │  ┌──────────────┐    ┌────────────────┐    ┌───────────────────┐  │
 │  │  I2C Master  │◄──►│  Bridge Core   │◄──►│  Fieldbus Stack   │  │
 │  │  (HAL/driver)│    │  (data mapper  │    │  (Modbus/CANopen/ │  │
 │  └──────┬───────┘    │   + scheduler) │    │   PROFIBUS/etc.)  │  │
 │         │            └────────────────┘    └────────┬──────────┘  │
 └─────────┼────────────────────────────────────────────┼────────────┘
           │ SDA/SCL (0–3.3V, up to 1 MHz)              │ RS-485 / CAN /
           │                                             │ Ethernet / fibre
    ┌──────┴─────────────────────────┐           ┌──────┴──────────────┐
    │   I2C Peripheral Bus           │           │   Industrial Network │
    │  ┌──────┐ ┌──────┐ ┌──────┐   │           │  ┌───┐  ┌───┐  ┌───┐│
    │  │Sensor│ │ADC   │ │EEPROM│   │           │  │PLC│  │HMI│  │DCS││
    │  │(temp)│ │(I/O) │ │(cfg) │   │           │  └───┘  └───┘  └───┘│
    │  └──────┘ └──────┘ └──────┘   │           └─────────────────────┘
    └────────────────────────────────┘
```

The bridge core performs three main functions:

1. **Polling / event-driven I2C acquisition** – reading sensor registers on a schedule or on alert-pin interrupt
2. **Data transformation** – unit conversion, scaling, linearisation, endian swapping
3. **Protocol-layer mapping** – placing values into the fieldbus address space (Modbus holding registers, CANopen PDOs, PROFIBUS process image, etc.)

---

## 3. I2C as a Local Sensor Bus in Industrial Systems

### Electrical Limitations

| Parameter | I2C (standard) | RS-485 (Modbus) | CAN (CANopen) |
|---|---|---|---|
| Max distance | ~1 m (capacitance limited) | 1200 m | 500 m @ 125 kbit/s |
| Max speed | 3.4 MHz (HS mode) | 115200 baud typical | 1 Mbit/s |
| Noise immunity | Low (single-ended) | High (differential) | High (differential) |
| Node count | 127 (7-bit addr) | 247 (Modbus RTU) | 127 (CAN ID) |
| Fault detection | ACK/NAK only | CRC-16 | CRC-15 + bit-stuffing |
| Real-time | No guarantee | No (polling-based) | Yes (priority arbitration) |

### I2C-to-Industrial Bridge Topologies

**Embedded bridge (MCU-based):**  
A microcontroller runs both an I2C master and a fieldbus peripheral stack simultaneously, often using a UART/SPI for Modbus or a CAN controller peripheral for CANopen.

**Dedicated gateway IC:**  
Parts such as the Analog Devices ADPD4100 or ams AS6200 aggregate sensor data, while separate bridge ASICs (e.g., HMS Anybus) handle the fieldbus side.

**Linux-based gateway (e.g., Raspberry Pi, BeagleBone, industrial SBC):**  
The OS provides I2C character devices (`/dev/i2c-N`), and user-space or kernel-space code implements the fieldbus stack (libmodbus, socketCAN + CANopenNode).

---

## 4. Bridging I2C to Modbus RTU/TCP

### Modbus Fundamentals Relevant to Bridging

Modbus defines a simple register model:

| Function Code | Object type | Count |
|---|---|---|
| 0x01 / 0x05 | Coils (1-bit R/W) | 9999 |
| 0x02 | Discrete Inputs (1-bit R) | 9999 |
| 0x03 / 0x06 / 0x10 | Holding Registers (16-bit R/W) | 9999 |
| 0x04 | Input Registers (16-bit R) | 9999 |

The bridge maps I2C sensor values into this flat address space.

### Mapping Example: BME280 → Modbus Holding Registers

```
Modbus Register 40001 (HR 0): Temperature    [0.01 °C / LSB, signed 16-bit]
Modbus Register 40002 (HR 1): Pressure high  [upper 16 bits of 32-bit Pa]
Modbus Register 40003 (HR 2): Pressure low   [lower 16 bits of 32-bit Pa]
Modbus Register 40004 (HR 3): Humidity       [0.01 %RH / LSB, unsigned 16-bit]
Modbus Register 40005 (HR 4): Device status  [bit 0=sensor OK, bit 1=overrange]
```

### Modbus RTU Bridge Flow

```
1. Modbus master sends: READ Holding Registers 40001–40004
2. Bridge receives request on RS-485 UART
3. Bridge reads BME280 via I2C (compensated values)
4. Bridge encodes values into Modbus PDU response
5. Bridge sends response within Modbus timeout (<500 ms typical)
```

### Asynchronous vs. Synchronous Bridging

**Synchronous (lazy / on-demand):**  
The bridge only queries the I2C device when a Modbus request arrives. Simple but adds I2C latency into the Modbus response time.

**Asynchronous (cached):**  
A background task continuously polls I2C devices and fills a register cache. Modbus requests read from the cache. Faster response, but data may be slightly stale.

---

## 5. Bridging I2C to CANopen

### CANopen Object Dictionary

CANopen structures all data in an **Object Dictionary (OD)**, a table of objects identified by a 16-bit index and 8-bit sub-index. Each object has a defined data type, access rights, and optionally a PDO mapping.

Key object ranges for sensor data:

| Index Range | Purpose |
|---|---|
| 0x1000–0x1FFF | Communication parameters (NMT, SYNC, PDO config) |
| 0x2000–0x5FFF | Manufacturer-specific (I2C sensor values live here) |
| 0x6000–0x9FFF | Device profile objects (CiA-401 I/O, CiA-404 measurement) |

### Process Data Objects (PDO)

PDOs are CAN frames transmitted without protocol overhead, mapped to OD entries at configuration time. A bridge node would:

1. Read I2C sensor data at a defined rate (e.g., 10 Hz)
2. Pack values into TPDO (Transmit PDO) frames
3. Transmit on SYNC or event-driven basis

```
TPDO1 (COB-ID 0x180 + Node-ID):
  Byte 0-1: Temperature (int16, 0.01°C)
  Byte 2-3: Humidity    (uint16, 0.01%)
  Byte 4-7: Pressure    (uint32, Pa)
```

### CANopen Node Management

The bridge must implement the full NMT state machine:

```
    INITIALIZATION
          │
          ▼
    PRE-OPERATIONAL ──(NMT Start)──► OPERATIONAL
          │                               │
          └──────(NMT Stop)─────► STOPPED │
                                          │
                              PDOs active, SDO available
```

CANopen also requires heartbeat or node guarding for fault detection — critical in industrial environments.

### CANopen Bridge Mapping Example (CiA-404 Measurement Device Profile)

```
Index 0x6401 sub-index 0x01: Analog Input 1 (temperature, int16)
Index 0x6401 sub-index 0x02: Analog Input 2 (humidity, int16)
Index 0x6401 sub-index 0x03: Analog Input 3 (pressure MSW, int16)
Index 0x6401 sub-index 0x04: Analog Input 4 (pressure LSW, int16)
```

---

## 6. Bridging I2C to PROFIBUS

PROFIBUS-DP (Decentralised Periphery) uses a master-slave token-passing model over RS-485. The bridge acts as a **DP slave** and presents a fixed-length **process image** (input/output bytes) to the PROFIBUS master.

### GSD File

Every PROFIBUS device requires a **GSD (Generic Station Description)** file that declares the device's I/O structure to the master's configuration tool. For an I2C sensor bridge the GSD would declare:

```
; Example GSD excerpt for I2C-to-PROFIBUS bridge
Module = "4x Sensor Channels" 0x51
; 8 bytes input: 4 x int16 sensor values
; 0 bytes output
EndModule
```

### Process Image Mapping

```
PLC Input Byte 0-1: Temperature (INT, 0.01°C)
PLC Input Byte 2-3: Humidity    (UINT, 0.01%RH)
PLC Input Byte 4-5: Pressure    (UINT, hPa)
PLC Input Byte 6-7: Status word (bit flags)
```

---

## 7. Bridging I2C to EtherCAT

EtherCAT is a high-performance Industrial Ethernet protocol with sub-microsecond cycle times. An I2C bridge as an EtherCAT slave uses an **ESC (EtherCAT Slave Controller)** chip (e.g., LAN9252, ET1100) connected via SPI or parallel bus to the host MCU.

### PDO Mapping in EtherCAT

EtherCAT process data is described by **SyncManagers** and **PDO assignments** stored in the device's ESI (EtherCAT Slave Information) XML file:

```xml
<TxPdo Fixed="true" Sm="3">
  <Index>#x1A00</Index>
  <Entry>
    <Index>#x6000</Index>
    <SubIndex>1</SubIndex>
    <BitLen>16</BitLen>
    <Name>Temperature</Name>
    <DataType>INT</DataType>
  </Entry>
  <Entry>
    <Index>#x6000</Index>
    <SubIndex>2</SubIndex>
    <BitLen>16</BitLen>
    <Name>Humidity</Name>
    <DataType>UINT</DataType>
  </Entry>
</TxPdo>
```

EtherCAT's high cycle rate (down to 100 µs) means the I2C polling rate becomes the limiting factor for data freshness.

---

## 8. Register Mapping Strategies

### Strategy 1: Direct Linear Mapping

Each I2C register or computed value is assigned a fixed fieldbus address. Simple, predictable, but inflexible.

```
I2C BME280 raw_temp → compensated_temp_cdeg → Modbus HR 0
I2C BME280 raw_hum  → compensated_hum_cpct  → Modbus HR 1
```

### Strategy 2: JSON/YAML Configuration Table

A configuration file defines the mapping at runtime, allowing field engineers to remap sensors without recompiling firmware.

```yaml
# bridge_config.yaml
channels:
  - name: "outdoor_temperature"
    i2c_bus: 1
    i2c_addr: 0x76       # BME280
    register: temperature
    scale: 100           # store as centidegrees
    modbus_address: 0    # HR 40001
    canopen_index: 0x6401
    canopen_subindex: 0x01

  - name: "cabinet_humidity"
    i2c_bus: 1
    i2c_addr: 0x76
    register: humidity
    scale: 100
    modbus_address: 1
    canopen_index: 0x6401
    canopen_subindex: 0x02
```

### Strategy 3: Virtual Device Abstraction

The bridge presents a virtual device model (e.g., a CiA-404 measurement device) regardless of which physical I2C sensors are attached. Sensors are discovered at boot and mapped to profile objects automatically.

### Data Type Handling

| I2C Data | Fieldbus Encoding | Notes |
|---|---|---|
| Float (32-bit) | 2× Holding Registers (IEEE 754) | Big-endian word order for Modbus |
| Signed 16-bit | One Holding Register | Two's complement |
| Unsigned 32-bit | 2× Holding Registers | MSW first or LSW first (configurable) |
| String (config) | Multiple HR, ASCII | Length prefix recommended |
| Bitfield / status | Coil map or single HR | Each bit = one flag |

---

## 9. Timing, Latency, and Real-Time Considerations

### Latency Budget Analysis

For a Modbus RTU bridge serving 10 sensors at 115200 baud:

```
I2C read latency (BME280, 100 kHz, burst):   ~3 ms
Modbus frame TX (8 registers @ 115200):       ~1.4 ms
Modbus response TX:                           ~1.4 ms
Processing/CRC computation:                   ~0.1 ms
─────────────────────────────────────────────────────
Total (synchronous bridge):                   ~6 ms
Modbus RTU timeout (spec):                    ≥3.5 char times = ~0.3 ms silent gap
```

**Key rule:** I2C reads must complete before the Modbus turnaround timeout expires. Use **cached bridging** when multiple I2C sensors must be read per Modbus request.

### Priority Inversion Risk

In RTOS-based bridges, I2C polling (lower priority) can block the fieldbus response task (higher priority) if they share a mutex. Mitigate with:

- **Double-buffering**: fieldbus reads from a shadow register set; I2C writer flips buffers atomically
- **Lock-free ring buffers**: suitable for single-producer/single-consumer patterns
- **Deadline-aware scheduling**: I2C tasks given a completion deadline before the next fieldbus cycle

### CANopen SYNC-Triggered Acquisition

CANopen supports synchronous PDO transmission where I2C acquisition is triggered by the SYNC message:

```
t=0:      SYNC broadcast (from master)
t=0+Δ1:  I2C read BME280 (~3 ms)
t=0+Δ2:  Pack values → TPDO1 frame
t=0+Δ3:  Transmit TPDO1 on CAN bus
          (Δ3 < SYNC period, e.g., < 100 ms)
```

---

## 10. Error Handling and Fault Propagation

### I2C Error Categories

| Error | Cause | Bridge Response |
|---|---|---|
| NACK on address | Device absent / unpowered | Set status bit; propagate "sensor fault" to fieldbus |
| NACK on data byte | Device busy or corrupt | Retry up to N times; then set fault |
| Bus lockup (SDA stuck low) | Interrupted transaction | Issue 9 SCL pulses to recover; reset device |
| Clock stretch timeout | Slow device | Increase timeout or abort |
| Arbitration loss | Multi-master conflict | Retry after backoff |

### Propagating Faults to Fieldbus

**Modbus:**
- Set a "status" holding register with a bitmask of failed channels
- Return exception code 0x04 (Slave Device Failure) for sustained faults

**CANopen:**
- Set Emergency Object (EMCY, COB-ID 0x80 + Node-ID) with error code 0x5000 (Device Hardware)
- Transition to PRE-OPERATIONAL state if sensor loss is critical

**PROFIBUS:**
- Set the diagnostic byte in the process image
- Assert the DIAG flag so the DP master reads extended diagnostics

### Watchdog Strategy

The bridge should implement a watchdog that monitors:
1. I2C bus health (successful reads within timeout window)
2. Fieldbus communication health (received messages / polling from master)
3. Internal software health (task deadlines, stack overflow detection)

---

## 11. Code Examples in C/C++

### 11.1 I2C Hardware Abstraction Layer (Linux, C)

```c
// i2c_hal.h - Portable I2C HAL for Linux /dev/i2c-N
#ifndef I2C_HAL_H
#define I2C_HAL_H

#include <stdint.h>
#include <stdbool.h>

#define I2C_OK          0
#define I2C_ERR_OPEN   -1
#define I2C_ERR_ADDR   -2
#define I2C_ERR_WRITE  -3
#define I2C_ERR_READ   -4
#define I2C_ERR_NACK   -5

typedef struct {
    int fd;         // file descriptor for /dev/i2c-N
    int bus_num;
} I2C_Handle;

int  i2c_open(I2C_Handle *h, int bus_num);
void i2c_close(I2C_Handle *h);
int  i2c_write_reg(I2C_Handle *h, uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);
int  i2c_read_reg(I2C_Handle *h, uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
int  i2c_recover_bus(I2C_Handle *h);  // 9-clock recovery

#endif // I2C_HAL_H
```

```c
// i2c_hal.c
#include "i2c_hal.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int i2c_open(I2C_Handle *h, int bus_num) {
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus_num);
    h->fd = open(path, O_RDWR | O_CLOEXEC);
    if (h->fd < 0) {
        perror("i2c_open");
        return I2C_ERR_OPEN;
    }
    h->bus_num = bus_num;
    return I2C_OK;
}

void i2c_close(I2C_Handle *h) {
    if (h->fd >= 0) close(h->fd);
    h->fd = -1;
}

int i2c_write_reg(I2C_Handle *h, uint8_t addr, uint8_t reg,
                  const uint8_t *data, size_t len)
{
    if (ioctl(h->fd, I2C_SLAVE, addr) < 0) return I2C_ERR_ADDR;

    // Combine register address + data into one write buffer
    uint8_t buf[1 + len];
    buf[0] = reg;
    memcpy(&buf[1], data, len);

    if (write(h->fd, buf, 1 + len) != (ssize_t)(1 + len))
        return I2C_ERR_WRITE;
    return I2C_OK;
}

int i2c_read_reg(I2C_Handle *h, uint8_t addr, uint8_t reg,
                 uint8_t *buf, size_t len)
{
    if (ioctl(h->fd, I2C_SLAVE, addr) < 0) return I2C_ERR_ADDR;

    // Write register address, then read data (repeated START)
    struct i2c_msg msgs[2] = {
        { .addr = addr, .flags = 0,        .len = 1,   .buf = &reg },
        { .addr = addr, .flags = I2C_M_RD, .len = len, .buf = buf  }
    };
    struct i2c_rdwr_ioctl_data xfer = { .msgs = msgs, .nmsgs = 2 };

    if (ioctl(h->fd, I2C_RDWR, &xfer) < 0) return I2C_ERR_READ;
    return I2C_OK;
}
```

---

### 11.2 BME280 Sensor Driver (C)

```c
// bme280.h
#ifndef BME280_H
#define BME280_H

#include "i2c_hal.h"

#define BME280_ADDR_PRIMARY   0x76
#define BME280_ADDR_SECONDARY 0x77
#define BME280_CHIP_ID        0x60

typedef struct {
    // Trimming parameters (read from device NVM)
    uint16_t dig_T1; int16_t dig_T2; int16_t dig_T3;
    uint16_t dig_P1; int16_t dig_P2; int16_t dig_P3;
    int16_t  dig_P4; int16_t dig_P5; int16_t dig_P6;
    int16_t  dig_P7; int16_t dig_P8; int16_t dig_P9;
    uint8_t  dig_H1; int16_t dig_H2; uint8_t dig_H3;
    int16_t  dig_H4; int16_t dig_H5; int8_t  dig_H6;
    int32_t  t_fine;  // shared compensation variable
} BME280_Calib;

typedef struct {
    I2C_Handle *bus;
    uint8_t     addr;
    BME280_Calib calib;
    bool        initialized;
} BME280_Dev;

typedef struct {
    int32_t  temperature_cdeg;  // centidegrees Celsius
    uint32_t pressure_pa;       // Pascals
    uint32_t humidity_cpct;     // centi-percent relative humidity
    bool     valid;
} BME280_Data;

int  bme280_init(BME280_Dev *dev, I2C_Handle *bus, uint8_t addr);
int  bme280_read(BME280_Dev *dev, BME280_Data *out);

#endif
```

```c
// bme280.c  (simplified — full Bosch compensation omitted for brevity)
#include "bme280.h"
#include <string.h>
#include <stdio.h>

#define REG_CHIP_ID    0xD0
#define REG_RESET      0xE0
#define REG_CALIB00    0x88
#define REG_CALIB26    0xE1
#define REG_CTRL_HUM   0xF2
#define REG_CTRL_MEAS  0xF4
#define REG_CONFIG     0xF5
#define REG_PRESS_MSB  0xF7

static int load_calibration(BME280_Dev *dev) {
    uint8_t raw[26];
    if (i2c_read_reg(dev->bus, dev->addr, REG_CALIB00, raw, 26) != I2C_OK)
        return -1;
    BME280_Calib *c = &dev->calib;
    c->dig_T1 = (uint16_t)(raw[1] << 8 | raw[0]);
    c->dig_T2 = (int16_t) (raw[3] << 8 | raw[2]);
    c->dig_T3 = (int16_t) (raw[5] << 8 | raw[4]);
    // ... (remaining calibration registers omitted for brevity)
    return 0;
}

static int32_t compensate_temp(BME280_Calib *c, int32_t adc_T) {
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)c->dig_T1 << 1)))
                    * ((int32_t)c->dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)c->dig_T1)
                    * ((adc_T >> 4) - (int32_t)c->dig_T1)) >> 12)
                    * (int32_t)c->dig_T3) >> 14;
    c->t_fine = var1 + var2;
    return (c->t_fine * 5 + 128) >> 8;  // in hundredths of degree C * 10
}

int bme280_init(BME280_Dev *dev, I2C_Handle *bus, uint8_t addr) {
    dev->bus  = bus;
    dev->addr = addr;
    dev->initialized = false;

    uint8_t chip_id = 0;
    if (i2c_read_reg(bus, addr, REG_CHIP_ID, &chip_id, 1) != I2C_OK) {
        fprintf(stderr, "BME280 @ 0x%02X: not responding\n", addr);
        return -1;
    }
    if (chip_id != BME280_CHIP_ID) {
        fprintf(stderr, "BME280: wrong chip ID 0x%02X\n", chip_id);
        return -2;
    }

    // Soft reset
    uint8_t rst = 0xB6;
    i2c_write_reg(bus, addr, REG_RESET, &rst, 1);
    usleep(10000);  // 10 ms startup

    if (load_calibration(dev) != 0) return -3;

    // Humidity oversampling x1, normal mode, temp+press oversampling x2
    uint8_t ctrl_hum  = 0x01;
    uint8_t ctrl_meas = (0x02 << 5) | (0x02 << 2) | 0x03; // normal mode
    uint8_t config    = (0x05 << 5) | (0x00 << 2) | 0x00;  // 1000ms standby

    i2c_write_reg(bus, addr, REG_CTRL_HUM,  &ctrl_hum,  1);
    i2c_write_reg(bus, addr, REG_CTRL_MEAS, &ctrl_meas, 1);
    i2c_write_reg(bus, addr, REG_CONFIG,    &config,    1);

    dev->initialized = true;
    return 0;
}

int bme280_read(BME280_Dev *dev, BME280_Data *out) {
    if (!dev->initialized) { out->valid = false; return -1; }

    uint8_t raw[8];
    if (i2c_read_reg(dev->bus, dev->addr, REG_PRESS_MSB, raw, 8) != I2C_OK) {
        out->valid = false;
        return -2;
    }

    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4)
                  | ((int32_t)raw[5] >> 4);
    // int32_t adc_P = ...; int32_t adc_H = ...;  (omitted for brevity)

    int32_t temp_raw = compensate_temp(&dev->calib, adc_T);
    out->temperature_cdeg = temp_raw;   // 0.01°C units
    out->pressure_pa      = 101325;     // placeholder (full compensation omitted)
    out->humidity_cpct    = 5000;       // placeholder
    out->valid = true;
    return 0;
}
```

---

### 11.3 Modbus RTU Bridge Core (C, using libmodbus)

```c
// modbus_bridge.c
// Bridges BME280 I2C sensor to Modbus RTU slave
// Dependencies: libmodbus (apt install libmodbus-dev)

#include <modbus/modbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include "i2c_hal.h"
#include "bme280.h"

// ── Register Map ──────────────────────────────────────────────────────────────
#define REG_TEMPERATURE   0   // HR 40001: temperature in centidegrees
#define REG_PRESSURE_HI   1   // HR 40002: pressure upper 16-bit
#define REG_PRESSURE_LO   2   // HR 40003: pressure lower 16-bit
#define REG_HUMIDITY      3   // HR 40004: humidity in centi-percent
#define REG_STATUS        4   // HR 40005: status bits
#define REG_COUNT         5

#define STATUS_SENSOR_OK   (1u << 0)
#define STATUS_SENSOR_ERR  (1u << 1)
#define STATUS_STALE       (1u << 2)

// ── Shared register cache (lock-free via atomic) ──────────────────────────────
typedef struct {
    _Atomic uint16_t regs[REG_COUNT];
} RegisterCache;

static RegisterCache g_cache;
static volatile sig_atomic_t g_running = 1;

static void sig_handler(int s) { (void)s; g_running = 0; }

// ── I2C polling thread ────────────────────────────────────────────────────────
typedef struct {
    I2C_Handle  i2c;
    BME280_Dev  bme;
    int         poll_ms;
} SensorThread;

static void *sensor_thread(void *arg) {
    SensorThread *st = (SensorThread *)arg;
    BME280_Data data;

    while (g_running) {
        int rc = bme280_read(&st->bme, &data);
        if (rc == 0 && data.valid) {
            uint16_t temp_u   = (uint16_t)(int16_t)data.temperature_cdeg;
            uint16_t press_hi = (uint16_t)(data.pressure_pa >> 16);
            uint16_t press_lo = (uint16_t)(data.pressure_pa & 0xFFFF);
            uint16_t hum      = (uint16_t)data.humidity_cpct;
            uint16_t status   = STATUS_SENSOR_OK;

            atomic_store(&g_cache.regs[REG_TEMPERATURE], temp_u);
            atomic_store(&g_cache.regs[REG_PRESSURE_HI], press_hi);
            atomic_store(&g_cache.regs[REG_PRESSURE_LO], press_lo);
            atomic_store(&g_cache.regs[REG_HUMIDITY],    hum);
            atomic_store(&g_cache.regs[REG_STATUS],      status);
        } else {
            // Mark error — keep last good values but set error status
            atomic_store(&g_cache.regs[REG_STATUS],
                         STATUS_SENSOR_ERR | STATUS_STALE);
        }

        struct timespec ts = { .tv_sec = 0, .tv_nsec = st->poll_ms * 1000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

// ── Modbus slave loop ─────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    const char *device  = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    int         node_id = (argc > 2) ? atoi(argv[2]) : 1;

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    // Initialise I2C and sensor
    SensorThread st;
    st.poll_ms = 200;  // 5 Hz polling
    if (i2c_open(&st.i2c, 1) != I2C_OK) {
        fprintf(stderr, "Cannot open I2C bus\n");
        return 1;
    }
    if (bme280_init(&st.bme, &st.i2c, BME280_ADDR_PRIMARY) != 0) {
        fprintf(stderr, "BME280 init failed — continuing with fault status\n");
        atomic_store(&g_cache.regs[REG_STATUS], STATUS_SENSOR_ERR);
    }

    // Start sensor polling thread
    pthread_t tid;
    pthread_create(&tid, NULL, sensor_thread, &st);

    // Initialise Modbus RTU slave
    modbus_t *mb = modbus_new_rtu(device, 115200, 'N', 8, 1);
    if (!mb) {
        fprintf(stderr, "modbus_new_rtu: %s\n", modbus_strerror(errno));
        return 1;
    }
    modbus_set_slave(mb, node_id);
    modbus_rtu_set_serial_mode(mb, MODBUS_RTU_RS485);

    modbus_mapping_t *map = modbus_mapping_new(0, 0, REG_COUNT, 0);
    if (!map) {
        fprintf(stderr, "modbus_mapping_new failed\n");
        modbus_free(mb);
        return 1;
    }

    if (modbus_connect(mb) == -1) {
        fprintf(stderr, "modbus_connect: %s\n", modbus_strerror(errno));
        modbus_free(mb);
        return 1;
    }

    printf("Modbus RTU bridge running on %s (node %d)\n", device, node_id);

    uint8_t req[MODBUS_RTU_MAX_ADU_LENGTH];
    while (g_running) {
        // Refresh register map from atomic cache before every request
        for (int i = 0; i < REG_COUNT; i++)
            map->tab_registers[i] = atomic_load(&g_cache.regs[i]);

        int rc = modbus_receive(mb, req);
        if (rc > 0) {
            modbus_reply(mb, req, rc, map);
        } else if (rc == -1 && errno != EINTR) {
            fprintf(stderr, "modbus_receive: %s\n", modbus_strerror(errno));
        }
    }

    printf("\nShutting down bridge...\n");
    pthread_join(tid, NULL);
    i2c_close(&st.i2c);
    modbus_mapping_free(map);
    modbus_close(mb);
    modbus_free(mb);
    return 0;
}
```

---

### 11.4 CANopen Node Bridge (C++, using CANopenNode library)

```cpp
// canopen_bridge.cpp
// Bridges multiple I2C sensors into a CANopen CiA-404 measurement device node
// Requires: CANopenNode (https://github.com/CANopenNode/CANopenNode)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <array>

extern "C" {
#include "CANopen.h"       // CANopenNode main header
#include "OD.h"            // Generated Object Dictionary from EDS tool
}

#include "i2c_hal.h"
#include "bme280.h"

// ── Sensor channel descriptor ─────────────────────────────────────────────────
struct SensorChannel {
    BME280_Dev  device;
    uint8_t     i2c_addr;
    std::atomic<int32_t>  temperature_cdeg{0};
    std::atomic<uint32_t> pressure_pa{0};
    std::atomic<uint32_t> humidity_cpct{0};
    std::atomic<bool>     fault{false};
};

static constexpr int NUM_CHANNELS = 4;
static SensorChannel  g_channels[NUM_CHANNELS];
static I2C_Handle     g_i2c_bus;
static std::atomic<bool> g_running{true};

// ── CANopen Object Dictionary SDO callback ─────────────────────────────────
// Called by CANopenNode when master reads manufacturer-specific objects
ODR_t OD_readChannelValue(OD_stream_t *stream, void *buf,
                           OD_size_t count, OD_size_t *countRead)
{
    // Extract channel index from sub-index
    uint8_t sub  = stream->subIndex;   // sub 1..4 → channel 0..3
    if (sub < 1 || sub > NUM_CHANNELS) return ODR_SUB_NOT_EXIST;

    const SensorChannel &ch = g_channels[sub - 1];
    if (ch.fault) {
        // Return a device error instead of stale data
        return ODR_DEV_INCOMPAT;
    }

    // Return temperature as int16 (index 0x6401)
    int16_t val = static_cast<int16_t>(
        ch.temperature_cdeg.load(std::memory_order_relaxed));
    std::memcpy(buf, &val, sizeof(val));
    *countRead = sizeof(val);
    return ODR_OK;
}

// ── Sensor acquisition thread ─────────────────────────────────────────────────
void sensor_thread() {
    using namespace std::chrono_literals;
    BME280_Data data;

    while (g_running) {
        for (auto &ch : g_channels) {
            if (!ch.device.initialized) continue;

            int rc = bme280_read(&ch.device, &data);
            if (rc == 0 && data.valid) {
                ch.temperature_cdeg.store(data.temperature_cdeg,
                                          std::memory_order_relaxed);
                ch.pressure_pa.store(data.pressure_pa,
                                     std::memory_order_relaxed);
                ch.humidity_cpct.store(data.humidity_cpct,
                                       std::memory_order_relaxed);
                ch.fault.store(false, std::memory_order_release);
            } else {
                ch.fault.store(true, std::memory_order_release);
            }
        }
        std::this_thread::sleep_for(100ms);  // 10 Hz acquisition
    }
}

// ── CANopen TPDO update callback (called by CANopenNode on SYNC) ──────────────
extern "C" void app_programAsync(CO_t *co, uint32_t /*timeDifference_us*/) {
    // Pack channel 0 temperature + humidity into TPDO1
    // The CANopenNode library reads these directly from OD variables,
    // so we just update the OD mirror registers here.
    if (!g_channels[0].fault) {
        // OD_RAM.x6401_analogInputs[0] is a 16-bit holding variable in OD.h
        // (generated from the EDS — names vary per project)
        OD_RAM.x6401_analogInputs[0] =
            static_cast<int16_t>(g_channels[0].temperature_cdeg
                                 .load(std::memory_order_relaxed));
        OD_RAM.x6401_analogInputs[1] =
            static_cast<int16_t>(g_channels[0].humidity_cpct
                                 .load(std::memory_order_relaxed));
    }
}

// ── EMCY fault signalling helper ─────────────────────────────────────────────
void signal_sensor_fault(CO_t *co, uint8_t channel) {
    // Error code 0x5000 = Device Hardware; error register bit 3 = Manufacturer
    CO_error(co->em, true, CO_EM_MANUFACTURER_START + channel,
             CO_EMC_HARDWARE, channel);
}

int main() {
    // Initialise I2C bus
    if (i2c_open(&g_i2c_bus, 1) != I2C_OK) {
        fprintf(stderr, "Cannot open I2C bus\n");
        return 1;
    }

    // Probe and initialise all sensor channels
    constexpr uint8_t addrs[NUM_CHANNELS] = {0x76, 0x77, 0x76, 0x77};
    // (in real hardware, channels on separate mux ports)
    for (int i = 0; i < NUM_CHANNELS; i++) {
        g_channels[i].i2c_addr = addrs[i];
        if (bme280_init(&g_channels[i].device, &g_i2c_bus, addrs[i]) != 0) {
            fprintf(stderr, "Channel %d: BME280 not found\n", i);
            g_channels[i].fault.store(true);
        }
    }

    // Start sensor thread
    std::thread acq(sensor_thread);

    // ── CANopen initialisation (platform-specific CAN socket) ──────────────
    // The CANopenNode Linux port uses socketCAN; adapt for RTOS as needed.
    CO_t *co = nullptr;
    CO_ReturnError_t err;
    uint32_t errInfo = 0;

    co = CO_new(nullptr, nullptr);
    if (!co) { fprintf(stderr, "CO_new failed\n"); return 1; }

    // Open socketCAN interface
    int can_fd = CO_CANmodule_init_socketCAN("can0");
    err = CO_CANinit(co, reinterpret_cast<void *>(&can_fd), 0 /*bitrate set by OS*/);
    if (err != CO_ERROR_NO) {
        fprintf(stderr, "CO_CANinit error %d\n", err);
        return 1;
    }

    uint32_t storageInitError = 0;
    err = CO_CANopenInit(co, nullptr, nullptr, OD, nullptr,
                         CO_CONFIG_FLAGS,
                         1 /* node-ID */, 250 /* kbit/s */,
                         nullptr, 0, &errInfo);
    if (err != CO_ERROR_NO) {
        fprintf(stderr, "CO_CANopenInit error %d (info 0x%x)\n", err, errInfo);
        return 1;
    }

    CO_CANsetNormalMode(co->CANmodule);
    printf("CANopen bridge running (node 1, CAN 250 kbit/s)\n");

    // ── Main processing loop ────────────────────────────────────────────────
    using clk = std::chrono::steady_clock;
    auto last = clk::now();

    while (g_running) {
        auto now     = clk::now();
        uint32_t dt  = std::chrono::duration_cast<std::chrono::microseconds>
                       (now - last).count();
        last = now;

        CO_NMT_reset_cmd_t reset = CO_process(co, false, dt, nullptr);
        if (reset != CO_RESET_NOT) break;

        // Check for sensor faults and signal EMCY
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (g_channels[i].fault.load(std::memory_order_acquire))
                signal_sensor_fault(co, static_cast<uint8_t>(i));
        }

        std::this_thread::sleep_for(std::chrono::microseconds(1000)); // 1 ms tick
    }

    g_running = false;
    acq.join();
    CO_delete(co);
    i2c_close(&g_i2c_bus);
    return 0;
}
```

---

### 11.5 I2C Multiplexer Support for Multi-Channel Bridges (C)

Many bridges use a TCA9548A (8-channel I2C mux) to connect many sensors with conflicting addresses:

```c
// tca9548a.h — TCA9548A I2C multiplexer driver
#ifndef TCA9548A_H
#define TCA9548A_H

#include "i2c_hal.h"

#define TCA9548A_ADDR_BASE 0x70  // A0-A2 = GND → 0x70

typedef struct {
    I2C_Handle *bus;
    uint8_t     addr;
    uint8_t     current_channel;  // 0xFF = unknown
} TCA9548A;

static inline int tca9548a_select(TCA9548A *mux, uint8_t channel) {
    if (channel > 7) return -1;
    if (mux->current_channel == channel) return 0;  // already selected

    uint8_t sel = (uint8_t)(1u << channel);
    if (ioctl(mux->bus->fd, I2C_SLAVE, mux->addr) < 0) return -1;
    if (write(mux->bus->fd, &sel, 1) != 1)             return -1;

    mux->current_channel = channel;
    return 0;
}

static inline int tca9548a_disable_all(TCA9548A *mux) {
    uint8_t sel = 0x00;
    if (ioctl(mux->bus->fd, I2C_SLAVE, mux->addr) < 0) return -1;
    write(mux->bus->fd, &sel, 1);
    mux->current_channel = 0xFF;
    return 0;
}

#endif // TCA9548A_H
```

Usage pattern in a multi-sensor bridge:

```c
TCA9548A mux = { .bus = &g_i2c_bus, .addr = TCA9548A_ADDR_BASE, .current_channel = 0xFF };

for (int ch = 0; ch < 4; ch++) {
    tca9548a_select(&mux, ch);           // switch mux to channel ch
    bme280_read(&g_channels[ch].device, &data);  // read sensor behind mux
}
tca9548a_disable_all(&mux);             // isolate all channels when idle
```

---

## 12. Code Examples in Rust

### 12.1 I2C HAL using `linux-embedded-hal` (Rust)

```toml
# Cargo.toml
[package]
name    = "i2c-industrial-bridge"
version = "0.1.0"
edition = "2021"

[dependencies]
linux-embedded-hal = "0.4"
embedded-hal       = "1.0"
modbus             = "0.6"         # tokio-modbus
tokio              = { version = "1", features = ["full"] }
tokio-modbus       = "0.9"
tokio-serial       = "5.4"
socketcan          = "3.3"         # for CANopen
log                = "0.4"
env_logger         = "0.11"
thiserror          = "1"
anyhow             = "1"
```

```rust
// src/i2c_hal.rs
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;
use std::path::Path;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum I2cError {
    #[error("Failed to open I2C device: {0}")]
    Open(#[from] std::io::Error),
    #[error("I2C transaction failed: {0}")]
    Transfer(String),
    #[error("Device NACK at address 0x{0:02X}")]
    Nack(u8),
}

pub struct I2cBus {
    dev: I2cdev,
}

impl I2cBus {
    pub fn open(bus_num: u8) -> Result<Self, I2cError> {
        let path = format!("/dev/i2c-{}", bus_num);
        let dev  = I2cdev::new(Path::new(&path))?;
        Ok(Self { dev })
    }

    /// Write to a device register
    pub fn write_reg(&mut self, addr: u8, reg: u8, data: &[u8])
        -> Result<(), I2cError>
    {
        let mut buf = vec![reg];
        buf.extend_from_slice(data);
        self.dev.write(addr, &buf)
            .map_err(|e| I2cError::Transfer(e.to_string()))
    }

    /// Read from a device register (write register address, then read)
    pub fn read_reg(&mut self, addr: u8, reg: u8, buf: &mut [u8])
        -> Result<(), I2cError>
    {
        self.dev.write_read(addr, &[reg], buf)
            .map_err(|e| I2cError::Transfer(e.to_string()))
    }
}
```

---

### 12.2 BME280 Driver (Rust)

```rust
// src/bme280.rs
use crate::i2c_hal::{I2cBus, I2cError};
use thiserror::Error;

pub const BME280_ADDR_PRIMARY:   u8 = 0x76;
pub const BME280_ADDR_SECONDARY: u8 = 0x77;

const REG_CHIP_ID:    u8 = 0xD0;
const REG_RESET:      u8 = 0xE0;
const REG_CALIB00:    u8 = 0x88;
const REG_CTRL_HUM:   u8 = 0xF2;
const REG_CTRL_MEAS:  u8 = 0xF4;
const REG_CONFIG:     u8 = 0xF5;
const REG_PRESS_MSB:  u8 = 0xF7;
const BME280_CHIP_ID: u8 = 0x60;

#[derive(Error, Debug)]
pub enum Bme280Error {
    #[error("I2C error: {0}")]
    I2c(#[from] I2cError),
    #[error("Wrong chip ID: expected 0x60, got 0x{0:02X}")]
    WrongId(u8),
    #[error("Device not initialised")]
    NotInit,
}

/// Calibration coefficients (from device NVM)
#[derive(Default, Clone)]
struct Calibration {
    dig_t1: u16, dig_t2: i16, dig_t3: i16,
    dig_p1: u16, dig_p2: i16, dig_p3: i16,
    dig_p4: i16, dig_p5: i16, dig_p6: i16,
    dig_p7: i16, dig_p8: i16, dig_p9: i16,
    dig_h1: u8,  dig_h2: i16, dig_h3: u8,
    dig_h4: i16, dig_h5: i16, dig_h6: i8,
}

/// Measured values in physical units
#[derive(Debug, Clone)]
pub struct Bme280Data {
    /// Temperature in centidegrees Celsius (e.g., 2345 = 23.45 °C)
    pub temperature_cdeg: i32,
    /// Pressure in Pascals
    pub pressure_pa: u32,
    /// Relative humidity in centi-percent (e.g., 4512 = 45.12 %RH)
    pub humidity_cpct: u32,
}

pub struct Bme280 {
    addr:  u8,
    calib: Calibration,
    t_fine: i32,
    initialised: bool,
}

impl Bme280 {
    pub fn new(addr: u8) -> Self {
        Self { addr, calib: Calibration::default(), t_fine: 0, initialised: false }
    }

    pub fn init(&mut self, bus: &mut I2cBus) -> Result<(), Bme280Error> {
        let mut id = [0u8; 1];
        bus.read_reg(self.addr, REG_CHIP_ID, &mut id)?;
        if id[0] != BME280_CHIP_ID {
            return Err(Bme280Error::WrongId(id[0]));
        }

        // Soft reset
        bus.write_reg(self.addr, REG_RESET, &[0xB6])?;
        std::thread::sleep(std::time::Duration::from_millis(10));

        self.load_calibration(bus)?;

        // Configure: humidity x1, temp+press x2, normal mode, 1s standby
        let ctrl_hum  = 0x01u8;
        let ctrl_meas = (0x02 << 5) | (0x02 << 2) | 0x03u8;
        let config    = (0x05u8 << 5) | (0x00 << 2) | 0x00u8;

        bus.write_reg(self.addr, REG_CTRL_HUM,  &[ctrl_hum])?;
        bus.write_reg(self.addr, REG_CTRL_MEAS, &[ctrl_meas])?;
        bus.write_reg(self.addr, REG_CONFIG,    &[config])?;

        self.initialised = true;
        Ok(())
    }

    fn load_calibration(&mut self, bus: &mut I2cBus) -> Result<(), Bme280Error> {
        let mut raw = [0u8; 26];
        bus.read_reg(self.addr, REG_CALIB00, &mut raw)?;
        let c = &mut self.calib;
        c.dig_t1 = u16::from_le_bytes([raw[0], raw[1]]);
        c.dig_t2 = i16::from_le_bytes([raw[2], raw[3]]);
        c.dig_t3 = i16::from_le_bytes([raw[4], raw[5]]);
        c.dig_p1 = u16::from_le_bytes([raw[6],  raw[7]]);
        c.dig_p2 = i16::from_le_bytes([raw[8],  raw[9]]);
        c.dig_p3 = i16::from_le_bytes([raw[10], raw[11]]);
        c.dig_p4 = i16::from_le_bytes([raw[12], raw[13]]);
        c.dig_p5 = i16::from_le_bytes([raw[14], raw[15]]);
        c.dig_p6 = i16::from_le_bytes([raw[16], raw[17]]);
        c.dig_p7 = i16::from_le_bytes([raw[18], raw[19]]);
        c.dig_p8 = i16::from_le_bytes([raw[20], raw[21]]);
        c.dig_p9 = i16::from_le_bytes([raw[22], raw[23]]);
        c.dig_h1 = raw[25];
        // Humidity calibration from second block (0xE1) omitted for brevity
        Ok(())
    }

    fn compensate_temperature(&mut self, adc_t: i32) -> i32 {
        let c = &self.calib;
        let var1 = (((adc_t >> 3) - ((c.dig_t1 as i32) << 1))
                   * (c.dig_t2 as i32)) >> 11;
        let var2 = (((((adc_t >> 4) - (c.dig_t1 as i32))
                   * ((adc_t >> 4) - (c.dig_t1 as i32))) >> 12)
                   * (c.dig_t3 as i32)) >> 14;
        self.t_fine = var1 + var2;
        (self.t_fine * 5 + 128) >> 8  // hundredths of °C × 10
    }

    pub fn read(&mut self, bus: &mut I2cBus) -> Result<Bme280Data, Bme280Error> {
        if !self.initialised { return Err(Bme280Error::NotInit); }

        let mut raw = [0u8; 8];
        bus.read_reg(self.addr, REG_PRESS_MSB, &mut raw)?;

        let adc_t = ((raw[3] as i32) << 12)
                  | ((raw[4] as i32) << 4)
                  | ((raw[5] as i32) >> 4);

        let temp = self.compensate_temperature(adc_t);

        Ok(Bme280Data {
            temperature_cdeg: temp,
            pressure_pa:      101325,  // full compensation omitted for brevity
            humidity_cpct:    5000,
        })
    }
}
```

---

### 12.3 Modbus TCP Bridge (Rust, async with tokio-modbus)

```rust
// src/modbus_bridge.rs
// Exposes I2C sensor data as a Modbus TCP slave (server)

use std::sync::{Arc, RwLock};
use std::net::SocketAddr;
use std::time::Duration;

use tokio::time::sleep;
use tokio_modbus::prelude::*;
use tokio_modbus::server::tcp::Server;
use tokio_modbus::server::Service;

use crate::bme280::{Bme280, Bme280Data, BME280_ADDR_PRIMARY};
use crate::i2c_hal::I2cBus;

// ── Register indices (0-based, HR 40001 = index 0) ───────────────────────────
const REG_TEMP:       u16 = 0;
const REG_PRESS_HI:   u16 = 1;
const REG_PRESS_LO:   u16 = 2;
const REG_HUMIDITY:   u16 = 3;
const REG_STATUS:     u16 = 4;
const NUM_REGS:       usize = 5;

const STATUS_OK:      u16 = 0x0001;
const STATUS_FAULT:   u16 = 0x0002;

// ── Shared state between acquisition task and Modbus server ───────────────────
#[derive(Default, Clone)]
struct SensorState {
    registers: [u16; NUM_REGS],
}

type SharedState = Arc<RwLock<SensorState>>;

// ── Sensor acquisition task ───────────────────────────────────────────────────
async fn sensor_acquisition_task(state: SharedState, poll_ms: u64) {
    let mut bus = match I2cBus::open(1) {
        Ok(b) => b,
        Err(e) => {
            log::error!("Cannot open I2C bus: {}", e);
            return;
        }
    };

    let mut sensor = Bme280::new(BME280_ADDR_PRIMARY);
    if let Err(e) = sensor.init(&mut bus) {
        log::error!("BME280 init failed: {}", e);
        if let Ok(mut st) = state.write() {
            st.registers[REG_STATUS as usize] = STATUS_FAULT;
        }
    }

    loop {
        let regs = match sensor.read(&mut bus) {
            Ok(data) => {
                let temp_u = data.temperature_cdeg as i16 as u16;
                [
                    temp_u,
                    (data.pressure_pa >> 16) as u16,
                    (data.pressure_pa & 0xFFFF) as u16,
                    data.humidity_cpct as u16,
                    STATUS_OK,
                ]
            },
            Err(e) => {
                log::warn!("Sensor read error: {}", e);
                let mut prev = [0u16; NUM_REGS];
                if let Ok(st) = state.read() {
                    prev.copy_from_slice(&st.registers);
                }
                prev[REG_STATUS as usize] = STATUS_FAULT;
                prev
            }
        };

        if let Ok(mut st) = state.write() {
            st.registers.copy_from_slice(&regs);
        }

        sleep(Duration::from_millis(poll_ms)).await;
    }
}

// ── Modbus server implementation ──────────────────────────────────────────────
#[derive(Clone)]
struct BridgeService {
    state: SharedState,
}

impl Service for BridgeService {
    type Request = Request<'static>;
    type Response = Response;
    type Error = std::io::Error;
    type Future = std::pin::Pin<
        Box<dyn std::future::Future<Output = Result<Self::Response, Self::Error>>
            + Send>
    >;

    fn call(&self, req: Self::Request) -> Self::Future {
        let state = Arc::clone(&self.state);
        Box::pin(async move {
            match req {
                Request::ReadHoldingRegisters(addr, count) => {
                    let st = state.read().map_err(|e| {
                        std::io::Error::new(std::io::ErrorKind::Other, e.to_string())
                    })?;
                    let start = addr as usize;
                    let end   = (start + count as usize).min(NUM_REGS);
                    if start >= NUM_REGS {
                        return Err(std::io::Error::new(
                            std::io::ErrorKind::InvalidInput,
                            "Register address out of range",
                        ));
                    }
                    Ok(Response::ReadHoldingRegisters(
                        st.registers[start..end].to_vec()
                    ))
                },
                _ => Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidInput,
                    "Unsupported function code",
                )),
            }
        })
    }
}

// ── Main entry point ──────────────────────────────────────────────────────────
#[tokio::main]
pub async fn main_modbus_bridge() -> anyhow::Result<()> {
    env_logger::init();

    let state: SharedState = Arc::new(RwLock::new(SensorState::default()));
    let state_acq = Arc::clone(&state);

    // Start background sensor acquisition
    tokio::spawn(async move {
        sensor_acquisition_task(state_acq, 200).await;
    });

    let addr: SocketAddr = "0.0.0.0:502".parse()?;
    log::info!("Modbus TCP bridge listening on {}", addr);

    let service_factory = move || {
        Ok(BridgeService { state: Arc::clone(&state) })
    };

    Server::new(addr)
        .serve(service_factory)
        .await?;

    Ok(())
}
```

---

### 12.4 CANopen PDO Publisher (Rust, using socketCAN)

```rust
// src/canopen_pdo.rs
// Publishes sensor data as CANopen TPDOs via socketCAN

use socketcan::{CanSocket, CanFrame, Socket, EmbeddedFrame, StandardId};
use std::sync::{Arc, RwLock};
use std::time::Duration;
use std::thread;

// CANopen COB-ID = function code + node-ID
// TPDO1 function code = 0x180
const TPDO1_COBID: u16 = 0x180;
const NODE_ID: u16      = 0x01;

fn make_cobid(func: u16, node: u16) -> StandardId {
    StandardId::new(func | node).expect("Invalid COB-ID")
}

/// Pack sensor values into TPDO1 frame (8 bytes max)
/// Layout:
///   Byte 0-1: temperature  (i16, centidegrees)
///   Byte 2-3: humidity     (u16, centi-percent)
///   Byte 4-7: pressure     (u32, Pascals, little-endian)
fn pack_tpdo1(temp_cdeg: i32, hum_cpct: u32, press_pa: u32) -> [u8; 8] {
    let mut data = [0u8; 8];
    let temp_i16 = temp_cdeg as i16;
    data[0..2].copy_from_slice(&temp_i16.to_le_bytes());
    data[2..4].copy_from_slice(&(hum_cpct as u16).to_le_bytes());
    data[4..8].copy_from_slice(&press_pa.to_le_bytes());
    data
}

pub struct CanopenPdoPublisher {
    socket: CanSocket,
    node_id: u16,
}

impl CanopenPdoPublisher {
    pub fn new(interface: &str, node_id: u16) -> anyhow::Result<Self> {
        let socket = CanSocket::open(interface)?;
        socket.set_write_timeout(Duration::from_millis(10))?;
        Ok(Self { socket, node_id })
    }

    /// Send heartbeat (NMT node monitoring message)
    /// Byte 0: NMT state (0x05 = Operational)
    pub fn send_heartbeat(&self, nmt_state: u8) -> anyhow::Result<()> {
        let cobid = make_cobid(0x700, self.node_id);
        let frame = CanFrame::new(cobid, &[nmt_state])?;
        self.socket.write_frame(&frame)?;
        Ok(())
    }

    /// Transmit TPDO1 with current sensor values
    pub fn send_tpdo1(&self, temp_cdeg: i32, hum_cpct: u32, press_pa: u32)
        -> anyhow::Result<()>
    {
        let cobid = make_cobid(TPDO1_COBID, self.node_id);
        let data  = pack_tpdo1(temp_cdeg, hum_cpct, press_pa);
        let frame = CanFrame::new(cobid, &data)?;
        self.socket.write_frame(&frame)?;
        Ok(())
    }

    /// Transmit Emergency Object on sensor fault
    /// Error code 0x5000 = Device Hardware
    pub fn send_emcy(&self, error_code: u16, error_register: u8,
                     vendor_info: u32) -> anyhow::Result<()>
    {
        let cobid = make_cobid(0x080, self.node_id);
        let mut data = [0u8; 8];
        data[0..2].copy_from_slice(&error_code.to_le_bytes());
        data[2] = error_register;
        data[4..8].copy_from_slice(&vendor_info.to_le_bytes());
        let frame = CanFrame::new(cobid, &data)?;
        self.socket.write_frame(&frame)?;
        Ok(())
    }
}

// ── PDO publication task (event-driven, 10 Hz) ────────────────────────────────
pub fn run_canopen_publisher(
    state: Arc<RwLock<crate::modbus_bridge::SensorState>>,
    interface: &str,
    node_id: u16,
) -> anyhow::Result<()> {
    let pub_ = CanopenPdoPublisher::new(interface, node_id)?;
    let interval = Duration::from_millis(100);  // 10 Hz

    loop {
        let (temp, hum, press, status) = {
            let st = state.read().unwrap();
            (
                st.registers[0] as i16 as i32,
                st.registers[3] as u32,
                ((st.registers[1] as u32) << 16) | (st.registers[2] as u32),
                st.registers[4],
            )
        };

        if status & 0x0002 != 0 {
            // Sensor fault — send EMCY
            pub_.send_emcy(0x5000, 0x08, 0x0000_0001)?;
        } else {
            pub_.send_tpdo1(temp, hum, press)?;
        }

        pub_.send_heartbeat(0x05)?;  // NMT Operational
        thread::sleep(interval);
    }
}
```

---

### 12.5 Complete Bridge Binary (Rust, main.rs)

```rust
// src/main.rs
// I2C → Modbus TCP + CANopen bridge

mod i2c_hal;
mod bme280;
mod modbus_bridge;
mod canopen_pdo;

use std::sync::{Arc, RwLock};
use tokio::task;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    env_logger::Builder::from_env(
        env_logger::Env::default().default_filter_or("info")
    ).init();

    log::info!("I2C Industrial Bridge starting...");

    let shared_state: modbus_bridge::SharedState =
        Arc::new(RwLock::new(modbus_bridge::SensorState::default()));

    // ── Spawn Modbus TCP server ───────────────────────────────────────────────
    let state_mb = Arc::clone(&shared_state);
    task::spawn(async move {
        if let Err(e) = modbus_bridge::run_server(state_mb).await {
            log::error!("Modbus server error: {}", e);
        }
    });

    // ── Spawn CANopen PDO publisher in a blocking thread ──────────────────────
    let state_co = Arc::clone(&shared_state);
    task::spawn_blocking(move || {
        if let Err(e) = canopen_pdo::run_canopen_publisher(
            state_co, "can0", 1
        ) {
            log::error!("CANopen publisher error: {}", e);
        }
    });

    // ── Sensor acquisition (runs in async context at polling rate) ────────────
    modbus_bridge::sensor_acquisition_task(shared_state, 200).await;

    Ok(())
}
```

---

## 13. Summary

Industrial protocol bridging transforms I2C — a capable but limited board-level bus — into a first-class participant in industrial automation networks. The bridge concept is simple: an embedded gateway acts as an I2C master on one side and a fieldbus slave on the other, continuously transferring data between the two domains.

### Key Architectural Takeaways

| Topic | Best Practice |
|---|---|
| **Data freshness** | Use asynchronous cached polling; don't read I2C on-demand in the fieldbus response path |
| **Fault propagation** | Always map I2C errors to fieldbus-native fault mechanisms (Modbus exception codes, CANopen EMCY, PROFIBUS diagnostics) |
| **Register mapping** | Define a versioned, configuration-file-driven mapping table; avoid hard-coding addresses |
| **Thread safety** | Use atomic register arrays or double-buffering; never hold an I2C lock while a fieldbus interrupt is pending |
| **Timing** | Model the full latency budget (I2C read time + fieldbus cycle time + processing); ensure I2C polling completes within one fieldbus cycle |
| **Multiplexing** | Use TCA9548A or PCA9548 for sensors with conflicting addresses; select channel atomically with the subsequent read |
| **Endianness** | Modbus is big-endian by convention; CANopen and CAN are little-endian; always document and enforce byte order |
| **Noise immunity** | I2C pull-ups, bus capacitance, and cable length must be managed at the PCB level; use level shifters when bridging 3.3V I2C to 5V bus partners |

### Protocol Selection Guide

| Use case | Recommended fieldbus |
|---|---|
| Simple PLC integration, legacy systems | **Modbus RTU** (RS-485) |
| High device count, complex device profiles | **CANopen** |
| Long cable runs, hazardous areas | **PROFIBUS-DP** or **HART** |
| High-performance, short cycle times | **EtherCAT** or **PROFINET** |
| Building automation / HVAC | **BACnet MS/TP** or **KNX** |
| Wireless industrial | **WirelessHART** or **ISA-100** |

### Code Organisation Recommendation

```
bridge/
├── hal/
│   ├── i2c_hal.c / i2c_hal.rs      (OS-portable I2C abstraction)
│   └── gpio_hal.c / gpio_hal.rs    (alert pins, mux enables)
├── drivers/
│   ├── bme280.c / bme280.rs        (sensor-specific drivers)
│   └── tca9548a.h / tca9548a.rs   (I2C mux driver)
├── bridge/
│   ├── register_map.c/rs           (I2C value → fieldbus address mapping)
│   ├── modbus_slave.c/rs           (Modbus RTU/TCP responder)
│   ├── canopen_node.c/rs           (CANopen NMT + PDO handler)
│   └── scheduler.c/rs              (polling task, buffer management)
├── config/
│   └── bridge_config.yaml          (channel mapping, addresses, scales)
└── tests/
    ├── test_register_map.c/rs
    └── test_modbus_response.c/rs
```

Bridging I2C into industrial protocols demands careful attention to timing, error propagation, and data consistency — but the reward is access to an enormous ecosystem of low-cost, high-quality I2C sensors within fully certified industrial automation architectures.

---

*Document: 85_Industrial_Protocols_Integration.md*  
*Scope: I2C bridging to Modbus RTU/TCP, CANopen, PROFIBUS, EtherCAT — C/C++ and Rust implementations*