# 100. I2C Migration to I3C

- **Protocol comparison** — electrical characteristics, speed, addressing, and feature tables (I2C Standard/FM+/FM vs. I3C SDR/HDR-DDR)
- **Key I3C concepts** — Controller/Target roles, DAA, CCC, IBIs, HDR modes
- **Migration planning** — inventory assessment, the three strategy options (pure I3C, mixed, parallel buses), and a full checklist
- **Hardware considerations** — pull-up resistor sizing, voltage compatibility, and bus capacitance limits
- **C/C++ implementation** — core data structures, DAA sequence, CCC dispatch, private read/write, IBI ISR, a C++ wrapper class, and a before/after sensor driver migration example
- **Rust implementation** — trait definitions, HAL impl with MMIO register access, accelerometer device driver, IBI atomic flag pattern, and a complete `main()` application loop
- **Backward compatibility** — how to register and use legacy I2C devices on an I3C bus
- **In-Band Interrupts** — mechanism, IBI vs. GPIO comparison table, enable sequence
- **DAA** — sequence diagram, address space management
- **Hot-Join** — sequence and handler
- **Testing** — Unity-based C unit tests with mock HAL, Rust unit tests with a mock `I3cController` impl, and protocol analyzer validation points
- **Common pitfalls** — address collision, missing pull-ups, speed mismatch, IBI flooding, Hot-Join races
- **Summary** — key advantages and a step-by-step migration strategy recap


**Planning and executing migration from legacy I2C to next-generation I3C protocol**

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [I2C vs I3C: Protocol Comparison](#2-i2c-vs-i3c-protocol-comparison)
3. [I3C Key Concepts](#3-i3c-key-concepts)
4. [Migration Planning](#4-migration-planning)
5. [Hardware Considerations](#5-hardware-considerations)
6. [Software Architecture](#6-software-architecture)
7. [C/C++ Implementation](#7-cc-implementation)
8. [Rust Implementation](#8-rust-implementation)
9. [Backward Compatibility: I2C Legacy Devices on I3C Bus](#9-backward-compatibility-i2c-legacy-devices-on-i3c-bus)
10. [In-Band Interrupts (IBI)](#10-in-band-interrupts-ibi)
11. [Dynamic Address Assignment (DAA)](#11-dynamic-address-assignment-daa)
12. [Hot-Join Support](#12-hot-join-support)
13. [Testing and Validation](#13-testing-and-validation)
14. [Common Migration Pitfalls](#14-common-migration-pitfalls)
15. [Summary](#15-summary)

---

## 1. Introduction

I3C (Improved Inter Integrated Circuit), standardized by MIPI Alliance, is the successor to the venerable I2C bus that has been in use since the 1980s. While I2C remains ubiquitous in embedded systems, I3C offers dramatically higher throughput, lower power consumption, and richer protocol features — all while maintaining backward compatibility with existing I2C devices.

Migration from I2C to I3C is not a simple drop-in replacement. It requires careful planning across hardware selection, bus topology, addressing schemes, driver architecture, and application software. This document covers the full migration journey from assessment through implementation and validation.

**Why migrate?**

| Motivation | Detail |
|---|---|
| Speed | I3C SDR reaches 12.5 MHz vs. I2C Fast-mode Plus at 1 MHz |
| Power | Single-wire operation possible; lower voltage swing |
| Features | In-Band Interrupts (IBI), Hot-Join, Dynamic Address Assignment |
| Sensor ecosystem | Modern MEMS sensors ship I3C-native |
| Standardization | Unified command set (CCC) across vendors |

---

## 2. I2C vs I3C: Protocol Comparison

### Electrical Characteristics

| Parameter | I2C Standard | I2C Fast-mode+ | I3C SDR | I3C HDR-DDR |
|---|---|---|---|---|
| Max clock | 100 kHz | 1 MHz | 12.5 MHz | 25 MHz (effective) |
| Logic high | VDD (pull-up) | VDD (pull-up) | Open-drain + push-pull | Push-pull |
| Logic low | Open-drain | Open-drain | Open-drain (start) | Push-pull |
| Pull-up resistor | Required | Required | Only for legacy | Not required |
| Wires | SDA + SCL | SDA + SCL | SDA + SCL | SDA + SCL |
| Multi-master | Yes | Yes | Yes (arbitration) | No |

### Protocol Features

| Feature | I2C | I3C |
|---|---|---|
| Addressing | 7-bit static / 10-bit static | 7-bit dynamic (DAA) + static fallback |
| Interrupts | Separate IRQ wire required | In-Band Interrupt (IBI) on SDA |
| Device discovery | None | Dynamic Address Assignment |
| Hot-plug | Not defined | Hot-Join supported |
| Error detection | ACK/NACK only | Parity on HDR-DDR, better error reporting |
| Command set | Vendor-specific | Standardized CCC (Common Command Codes) |
| Bus speed switching | Manual | Controller-initiated via CCC |

---

## 3. I3C Key Concepts

### 3.1 Roles: Controller vs. Target

In I3C, terminology shifted from I2C's "master/slave":

- **Controller**: Initiates transactions, assigns addresses, manages the bus.
- **Target**: Responds to controller; can initiate IBI.
- **Secondary Controller**: A target that can request controller role handover.

### 3.2 Dynamic Address Assignment (DAA)

Unlike I2C's hard-wired 7-bit addresses, I3C targets can be assigned addresses at runtime via the `ENTDAA` (Enter Dynamic Address Assignment) CCC. Each target has a unique 48-bit Provisioned ID (PID) that allows the controller to identify and address it.

### 3.3 Common Command Codes (CCC)

CCC frames provide a standardized way to configure I3C targets:

- **Broadcast CCC**: Sent to all targets (e.g., `ENEC` — Enable Events Command).
- **Direct CCC**: Sent to a specific target (e.g., `SETMRL` — Set Max Read Length).

### 3.4 In-Band Interrupts (IBI)

A target can assert an interrupt by pulling SDA low at the start of a new I3C frame, before the controller arbitrates. The controller grants the IBI, and the target sends its dynamic address plus optional data bytes.

### 3.5 HDR Modes

Beyond Standard Data Rate (SDR), I3C supports:

- **HDR-DDR**: Double Data Rate, up to 25 MHz effective.
- **HDR-TSP / HDR-TSL**: Ternary symbol modes, reduces transition count.
- **HDR-BT**: Bulk Transport mode for large payloads.

---

## 4. Migration Planning

### 4.1 Inventory Assessment

Before touching code, audit your system:

```
1. List all I2C devices (address, speed, pull-up requirements)
2. Identify which devices have I3C-native variants
3. Note which devices must remain as legacy I2C
4. Map interrupt lines (candidates for IBI migration)
5. Check SoC or microcontroller for I3C controller support
```

### 4.2 Migration Strategy Options

**Option A — Pure I3C Bus (recommended for new designs)**
All devices are I3C-capable. Eliminates legacy overhead entirely.

**Option B — Mixed I3C/I2C Bus**
I3C controller manages both I3C targets and legacy I2C devices. The I3C controller handles I2C transactions at the appropriate speed. This is the most common real-world scenario.

**Option C — Parallel Buses**
Keep the existing I2C bus intact; add an I3C bus for new devices. Safest migration path for complex existing systems, but uses additional pins.

### 4.3 Migration Checklist

```
[ ] SoC/MCU I3C peripheral confirmed (check if it supports controller mode)
[ ] Pull-up resistor values re-evaluated for I3C SDR speeds
[ ] Legacy I2C devices assigned static I3C addresses (if mixed bus)
[ ] Device driver updates planned
[ ] IBI handler design done
[ ] DAA sequence implemented
[ ] CCC initialization sequence designed
[ ] Power supply validated for I3C output levels
[ ] Testing plan written (loopback, scope capture, protocol analyzer)
```

---

## 5. Hardware Considerations

### 5.1 Pull-up Resistors

I3C uses push-pull driving during data transfers. Pull-up resistors are still required for:

- Open-drain START/STOP conditions.
- Legacy I2C device compatibility.
- Idle bus state.

For a mixed I3C/legacy-I2C bus at 12.5 MHz, use weak pull-ups (e.g., 2.2 kΩ to 3.3 kΩ) to avoid RC time constant limitations. Contrast with I2C Fast-mode which typically uses 1 kΩ–4.7 kΩ.

### 5.2 Voltage Level Compatibility

I3C targets may operate at 1.0 V, 1.2 V, 1.8 V, or 3.3 V. If your legacy I2C devices operate at 3.3 V and your I3C devices at 1.8 V, a level-shifter or a 1.8 V-tolerant I3C controller is required.

### 5.3 Bus Capacitance

I3C SDR push-pull transitions are faster and can cause more ringing on high-capacitance buses. Keep total bus capacitance below 250 pF (per MIPI I3C spec). Use series termination resistors (10–22 Ω) near the controller if ringing is an issue.

---

## 6. Software Architecture

### 6.1 Driver Layering

```
┌──────────────────────────────────────────────┐
│           Application Layer                  │
│   (sensor read, actuator control, etc.)      │
├──────────────────────────────────────────────┤
│           Device Driver Layer                │
│   (device-specific CCC, register maps)       │
├──────────────────────────────────────────────┤
│           I3C Bus Abstraction Layer          │
│   (DAA, IBI, CCC dispatch, error handling)   │
├──────────────────────────────────────────────┤
│           HAL / Platform I3C Driver          │
│   (SoC-specific register access)             │
└──────────────────────────────────────────────┘
```

### 6.2 Key Data Structures

The bus abstraction layer needs to track:

- Controller state machine state.
- Device table: mapping of dynamic address → PID → device driver.
- IBI queue for incoming interrupt requests.
- Pending CCC transactions.

---

## 7. C/C++ Implementation

### 7.1 Core Data Structures (C)

```c
#include <stdint.h>
#include <stdbool.h>

/* I3C Device Descriptor */
typedef struct {
    uint8_t  dynamic_addr;       /* Assigned dynamic address (7-bit) */
    uint64_t pid;                /* Provisioned ID (48-bit in 64-bit field) */
    uint8_t  static_addr;        /* Static I2C address, 0 if pure I3C */
    bool     is_i2c_legacy;      /* True = legacy I2C device on I3C bus */
    bool     ibi_enabled;        /* In-Band Interrupt enabled */
    uint8_t  bcr;                /* Bus Characteristics Register */
    uint8_t  dcr;                /* Device Characteristics Register */
    uint8_t  max_read_len;       /* Max payload for read transactions */
    uint8_t  max_write_len;      /* Max payload for write transactions */
} i3c_device_t;

/* I3C Controller Context */
typedef struct {
    volatile uint32_t *regs;     /* Base address of I3C controller registers */
    i3c_device_t       devices[16];
    uint8_t            device_count;
    uint8_t            free_addr; /* Next address to assign during DAA */
} i3c_controller_t;

/* CCC (Common Command Code) frame */
typedef struct {
    uint8_t  ccc_id;             /* CCC identifier byte */
    bool     is_direct;          /* Broadcast=false, Direct=true */
    uint8_t  target_addr;        /* Only valid if is_direct */
    uint8_t  data[16];           /* Payload bytes */
    uint8_t  data_len;
} i3c_ccc_t;
```

### 7.2 Initialization and DAA (C)

```c
#include "i3c_hal.h"   /* Platform HAL, provides i3c_hal_write_reg() etc. */

/* CCC IDs (subset) */
#define CCC_RSTDAA      0x06   /* Reset Dynamic Address Assignment */
#define CCC_ENTDAA      0x07   /* Enter Dynamic Address Assignment */
#define CCC_DISEC       0x01   /* Disable Events Command */
#define CCC_ENEC        0x00   /* Enable Events Command */
#define CCC_SETMWL      0x09   /* Set Max Write Length (direct) */
#define CCC_SETMRL      0x0A   /* Set Max Read Length (direct) */
#define CCC_GETPID      0x8D   /* Get Provisioned ID (direct) */
#define CCC_GETBCR      0x8E   /* Get Bus Characteristics Register */
#define CCC_GETDCR      0x8F   /* Get Device Characteristics Register */

/* IBI Event Enable mask */
#define IBI_EVENT_MR    (1u << 0)  /* Master Request */
#define IBI_EVENT_HJ    (1u << 3)  /* Hot-Join */
#define IBI_EVENT_IBI   (1u << 0)  /* In-Band Interrupt */

/**
 * @brief Initialize the I3C controller hardware.
 * @param ctrl  Controller context (regs must already be mapped).
 */
void i3c_init(i3c_controller_t *ctrl)
{
    /* Reset controller */
    i3c_hal_write_reg(ctrl->regs, I3C_REG_CTRL, I3C_CTRL_RESET);
    i3c_hal_wait_ready(ctrl->regs);

    /* Configure timing for SDR 12.5 MHz (values are platform-specific) */
    i3c_hal_write_reg(ctrl->regs, I3C_REG_SCL_TIMING,
                      I3C_SCL_PP_HIGH(2) | I3C_SCL_PP_LOW(2) |
                      I3C_SCL_OD_HIGH(8) | I3C_SCL_OD_LOW(16));

    /* Enable IBI and Hot-Join acceptance */
    i3c_hal_write_reg(ctrl->regs, I3C_REG_IBI_CTRL,
                      I3C_IBI_ACCEPT | I3C_HJ_ACCEPT);

    /* Enable controller */
    i3c_hal_write_reg(ctrl->regs, I3C_REG_CTRL, I3C_CTRL_ENABLE);

    ctrl->device_count = 0;
    ctrl->free_addr    = 0x08; /* Addresses 0x00–0x07 are reserved */
}

/**
 * @brief Send broadcast CCC RSTDAA to clear all dynamic addresses,
 *        then run ENTDAA to discover and assign addresses.
 * @return Number of devices discovered, or -1 on error.
 */
int i3c_do_daa(i3c_controller_t *ctrl)
{
    /* Step 1: Broadcast RSTDAA to clear previous DAA state */
    i3c_ccc_t ccc_reset = {
        .ccc_id    = CCC_RSTDAA,
        .is_direct = false,
        .data_len  = 0
    };
    if (i3c_send_ccc(ctrl, &ccc_reset) != 0) {
        return -1;
    }

    /* Step 2: Broadcast ENTDAA — devices respond with 48-bit PID + BCR + DCR */
    int discovered = 0;

    /* ENTDAA is a special iterative transaction handled in hardware/HAL */
    while (i3c_hal_entdaa_step(ctrl->regs) == I3C_ENTDAA_DEVICE_PRESENT) {
        uint8_t pid_buf[6];
        uint8_t bcr, dcr;

        i3c_hal_entdaa_read_pid(ctrl->regs, pid_buf, &bcr, &dcr);

        uint64_t pid = 0;
        for (int i = 0; i < 6; i++) {
            pid = (pid << 8) | pid_buf[i];
        }

        /* Assign next free dynamic address */
        uint8_t dyn_addr = ctrl->free_addr++;

        i3c_hal_entdaa_assign_addr(ctrl->regs, dyn_addr);

        /* Record in device table */
        i3c_device_t *dev = &ctrl->devices[ctrl->device_count++];
        dev->dynamic_addr = dyn_addr;
        dev->pid          = pid;
        dev->static_addr  = 0;
        dev->is_i2c_legacy = false;
        dev->bcr          = bcr;
        dev->dcr          = dcr;
        dev->ibi_enabled  = (bcr & 0x02) != 0; /* BCR[1] = IBI capable */

        discovered++;
    }

    return discovered;
}
```

### 7.3 CCC Send and Private Transfer (C)

```c
/**
 * @brief Send a CCC frame (broadcast or direct).
 */
int i3c_send_ccc(i3c_controller_t *ctrl, const i3c_ccc_t *ccc)
{
    i3c_hal_transfer_t xfer = {0};

    xfer.type     = I3C_XFER_CCC;
    xfer.ccc_id   = ccc->ccc_id;
    xfer.is_direct = ccc->is_direct;
    xfer.addr     = ccc->target_addr;
    xfer.tx_buf   = ccc->data;
    xfer.tx_len   = ccc->data_len;

    return i3c_hal_transfer(ctrl->regs, &xfer);
}

/**
 * @brief Private write to an I3C target.
 */
int i3c_write(i3c_controller_t *ctrl, uint8_t dyn_addr,
              const uint8_t *data, uint16_t len)
{
    i3c_hal_transfer_t xfer = {
        .type   = I3C_XFER_PRIVATE_WRITE,
        .addr   = dyn_addr,
        .tx_buf = data,
        .tx_len = len,
        .rx_buf = NULL,
        .rx_len = 0
    };
    return i3c_hal_transfer(ctrl->regs, &xfer);
}

/**
 * @brief Private read from an I3C target.
 */
int i3c_read(i3c_controller_t *ctrl, uint8_t dyn_addr,
             uint8_t *data, uint16_t len)
{
    i3c_hal_transfer_t xfer = {
        .type   = I3C_XFER_PRIVATE_READ,
        .addr   = dyn_addr,
        .tx_buf = NULL,
        .tx_len = 0,
        .rx_buf = data,
        .rx_len = len
    };
    return i3c_hal_transfer(ctrl->regs, &xfer);
}

/**
 * @brief Legacy I2C write on I3C bus.
 *        The I3C controller emits an I2C-compatible frame.
 */
int i3c_i2c_write(i3c_controller_t *ctrl, uint8_t i2c_addr,
                  const uint8_t *data, uint16_t len)
{
    i3c_hal_transfer_t xfer = {
        .type   = I3C_XFER_I2C_WRITE,
        .addr   = i2c_addr,
        .tx_buf = data,
        .tx_len = len
    };
    return i3c_hal_transfer(ctrl->regs, &xfer);
}
```

### 7.4 In-Band Interrupt Handling (C)

```c
/**
 * @brief IBI callback type — called from ISR context (or deferred task).
 * @param dyn_addr  Dynamic address of the requesting target.
 * @param payload   Optional IBI data bytes.
 * @param payload_len  Length of payload (0 if no MDB).
 */
typedef void (*i3c_ibi_callback_t)(uint8_t dyn_addr,
                                   const uint8_t *payload,
                                   uint8_t payload_len);

static i3c_ibi_callback_t g_ibi_handler = NULL;

void i3c_register_ibi_handler(i3c_ibi_callback_t handler)
{
    g_ibi_handler = handler;
}

/**
 * @brief I3C controller ISR — call from platform interrupt vector.
 */
void I3C_IRQHandler(void)
{
    uint32_t status = i3c_hal_read_reg(g_ctrl->regs, I3C_REG_INT_STATUS);

    if (status & I3C_INT_IBI) {
        uint8_t ibi_addr;
        uint8_t ibi_data[8];
        uint8_t ibi_len = 0;

        /* Read the IBI address + optional payload from FIFO */
        i3c_hal_ibi_read(g_ctrl->regs, &ibi_addr, ibi_data, &ibi_len);

        if (g_ibi_handler) {
            g_ibi_handler(ibi_addr, ibi_data, ibi_len);
        }

        i3c_hal_write_reg(g_ctrl->regs, I3C_REG_INT_STATUS, I3C_INT_IBI);
    }

    if (status & I3C_INT_HJ) {
        /* Hot-Join: a new device wants to join. Run DAA again. */
        i3c_do_daa(g_ctrl);
        i3c_hal_write_reg(g_ctrl->regs, I3C_REG_INT_STATUS, I3C_INT_HJ);
    }
}
```

### 7.5 C++ Wrapper Class

```cpp
#include <cstdint>
#include <functional>
#include <vector>
#include <optional>

class I3CBus {
public:
    struct DeviceInfo {
        uint8_t  dynamicAddr;
        uint64_t pid;
        uint8_t  bcr;
        uint8_t  dcr;
        bool     ibiCapable;
    };

    using IbiHandler = std::function<void(uint8_t addr,
                                          const uint8_t* data,
                                          uint8_t len)>;

    explicit I3CBus(volatile uint32_t* regs)
    {
        m_ctx.regs = regs;
        i3c_init(&m_ctx);
    }

    /**
     * Run Dynamic Address Assignment.
     * @return List of discovered devices.
     */
    std::vector<DeviceInfo> runDAA()
    {
        int n = i3c_do_daa(&m_ctx);
        std::vector<DeviceInfo> result;
        for (int i = 0; i < n; i++) {
            const auto& d = m_ctx.devices[i];
            result.push_back({
                d.dynamic_addr, d.pid, d.bcr, d.dcr,
                static_cast<bool>(d.ibi_enabled)
            });
        }
        return result;
    }

    /**
     * Private write to target.
     */
    bool write(uint8_t addr, const uint8_t* data, uint16_t len)
    {
        return i3c_write(&m_ctx, addr, data, len) == 0;
    }

    /**
     * Private read from target.
     */
    bool read(uint8_t addr, uint8_t* buf, uint16_t len)
    {
        return i3c_read(&m_ctx, addr, buf, len) == 0;
    }

    /**
     * Register-based read/write helper (common for sensor drivers).
     * Writes reg address then reads len bytes.
     */
    bool readRegister(uint8_t addr, uint8_t reg, uint8_t* buf, uint16_t len)
    {
        if (!write(addr, &reg, 1)) return false;
        return read(addr, buf, len);
    }

    /**
     * Send broadcast CCC.
     */
    bool broadcastCCC(uint8_t cccId,
                      const uint8_t* data = nullptr,
                      uint8_t dataLen = 0)
    {
        i3c_ccc_t ccc{};
        ccc.ccc_id    = cccId;
        ccc.is_direct = false;
        ccc.data_len  = dataLen;
        if (data && dataLen) {
            for (int i = 0; i < dataLen; i++) ccc.data[i] = data[i];
        }
        return i3c_send_ccc(&m_ctx, &ccc) == 0;
    }

    /**
     * Enable In-Band Interrupts for a target.
     */
    bool enableIBI(uint8_t addr)
    {
        uint8_t payload = IBI_EVENT_IBI;
        i3c_ccc_t ccc{};
        ccc.ccc_id       = CCC_ENEC;
        ccc.is_direct    = true;
        ccc.target_addr  = addr;
        ccc.data[0]      = payload;
        ccc.data_len     = 1;
        return i3c_send_ccc(&m_ctx, &ccc) == 0;
    }

    void setIBIHandler(IbiHandler handler)
    {
        m_ibiHandler = std::move(handler);
        i3c_register_ibi_handler([](uint8_t addr, const uint8_t* d, uint8_t len) {
            // Note: in real code, use a global/singleton to access m_ibiHandler
            (void)addr; (void)d; (void)len;
        });
    }

private:
    i3c_controller_t m_ctx{};
    IbiHandler       m_ibiHandler;
};
```

### 7.6 Example: Migrating a Sensor Driver from I2C to I3C

```cpp
/*
 * Before migration (I2C):
 *
 *   #define SENSOR_I2C_ADDR  0x6A
 *   #define REG_ACCEL_X_L    0x28
 *
 *   int read_accel(i2c_dev_t *bus, int16_t *ax, int16_t *ay, int16_t *az)
 *   {
 *       uint8_t reg = REG_ACCEL_X_L | 0x80; // auto-increment
 *       uint8_t buf[6];
 *       i2c_write(bus, SENSOR_I2C_ADDR, &reg, 1);
 *       i2c_read(bus, SENSOR_I2C_ADDR, buf, 6);
 *       *ax = (int16_t)((buf[1] << 8) | buf[0]);
 *       *ay = (int16_t)((buf[3] << 8) | buf[2]);
 *       *az = (int16_t)((buf[5] << 8) | buf[4]);
 *       return 0;
 *   }
 *
 * After migration (I3C):
 */

#define REG_ACCEL_X_L    0x28

/**
 * After DAA, the sensor is accessed via its dynamic address.
 * The register access pattern is identical — only the bus handle changes.
 */
int read_accel_i3c(I3CBus& bus, uint8_t dyn_addr,
                   int16_t* ax, int16_t* ay, int16_t* az)
{
    uint8_t reg = REG_ACCEL_X_L; /* I3C sensors often drop the auto-increment flag */
    uint8_t buf[6];

    if (!bus.readRegister(dyn_addr, reg, buf, 6)) {
        return -1;
    }

    *ax = static_cast<int16_t>((buf[1] << 8) | buf[0]);
    *ay = static_cast<int16_t>((buf[3] << 8) | buf[2]);
    *az = static_cast<int16_t>((buf[5] << 8) | buf[4]);
    return 0;
}

/* Sensor initialization: use IBI instead of a dedicated interrupt pin */
void sensor_init_i3c(I3CBus& bus, uint8_t dyn_addr)
{
    /* Configure sensor: full-scale ±4g, ODR 104 Hz */
    uint8_t cfg[] = {0x30, 0x40}; /* CTRL1_XL register, ODR|FS setting */
    bus.write(dyn_addr, cfg, sizeof(cfg));

    /* Enable In-Band Interrupt — replaces the external INT1 wire */
    bus.enableIBI(dyn_addr);

    /* Register IBI handler: called when sensor has new data */
    bus.setIBIHandler([](uint8_t addr, const uint8_t* data, uint8_t len) {
        if (addr == dyn_addr) {
            /* New accelerometer data ready — trigger read task */
            sensor_data_ready_flag = true;
        }
    });
}
```

---

## 8. Rust Implementation

### 8.1 Crate Dependencies (Cargo.toml)

```toml
[dependencies]
# embedded-hal 1.0 provides the I2C traits; I3C traits are emerging
embedded-hal       = "1.0"
embedded-hal-async = "1.0"

# nb for non-blocking patterns
nb = "1.0"

# Optional: defmt for embedded logging
defmt = { version = "0.3", optional = true }
```

> **Note:** At the time of writing, `embedded-hal` does not yet have a stable I3C trait. The examples below define a custom trait that mirrors the emerging API and can be swapped for the official trait when available.

### 8.2 I3C Traits and Types (Rust)

```rust
//! i3c/mod.rs — Core types and trait definitions

use core::fmt;

/// 7-bit dynamic address assigned during DAA.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DynAddr(pub u8);

/// 48-bit Provisioned ID uniquely identifying an I3C target.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ProvisionedId(pub u64);

/// Describes a device discovered on the I3C bus.
#[derive(Debug, Clone, Copy)]
pub struct DeviceDesc {
    pub dyn_addr:    DynAddr,
    pub pid:         ProvisionedId,
    pub bcr:         u8,  // Bus Characteristics Register
    pub dcr:         u8,  // Device Characteristics Register
    pub is_legacy:   bool,
    pub ibi_capable: bool,
}

/// Error type for I3C bus operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I3cError {
    Nack,
    ArbitrationLost,
    Timeout,
    BusError,
    InvalidAddress,
    BufferTooSmall,
}

impl fmt::Display for I3cError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            I3cError::Nack              => write!(f, "NACK received"),
            I3cError::ArbitrationLost   => write!(f, "Arbitration lost"),
            I3cError::Timeout           => write!(f, "Bus timeout"),
            I3cError::BusError          => write!(f, "Bus error"),
            I3cError::InvalidAddress    => write!(f, "Invalid address"),
            I3cError::BufferTooSmall    => write!(f, "Buffer too small"),
        }
    }
}

/// Common Command Code identifiers (subset).
#[allow(dead_code)]
#[repr(u8)]
pub enum CccId {
    Enec   = 0x00,  // Enable Events Command (broadcast)
    Disec  = 0x01,  // Disable Events Command (broadcast)
    Rstdaa = 0x06,  // Reset Dynamic Address Assignment (broadcast)
    Entdaa = 0x07,  // Enter Dynamic Address Assignment (broadcast)
    Setmwl = 0x09,  // Set Max Write Length (direct)
    Setmrl = 0x0A,  // Set Max Read Length (direct)
    Getpid = 0x8D,  // Get Provisioned ID (direct)
    Getbcr = 0x8E,  // Get BCR (direct)
    Getdcr = 0x8F,  // Get DCR (direct)
}

/// I3C controller trait — blocking variant.
pub trait I3cController {
    /// Write bytes to an I3C target's private address space.
    fn write(&mut self, addr: DynAddr, data: &[u8]) -> Result<(), I3cError>;

    /// Read bytes from an I3C target's private address space.
    fn read(&mut self, addr: DynAddr, buf: &mut [u8]) -> Result<(), I3cError>;

    /// Write then read (combined transfer with repeated START).
    fn write_read(
        &mut self,
        addr:    DynAddr,
        write:   &[u8],
        read:    &mut [u8],
    ) -> Result<(), I3cError>;

    /// Send a broadcast CCC.
    fn send_broadcast_ccc(
        &mut self,
        ccc:  CccId,
        data: &[u8],
    ) -> Result<(), I3cError>;

    /// Send a direct CCC to a specific target.
    fn send_direct_ccc(
        &mut self,
        ccc:  CccId,
        addr: DynAddr,
        data: &mut [u8],
        read: bool,
    ) -> Result<(), I3cError>;

    /// Run Dynamic Address Assignment.
    fn run_daa(&mut self, table: &mut [DeviceDesc]) -> Result<usize, I3cError>;

    /// Write to a legacy I2C device on the I3C bus.
    fn i2c_write(
        &mut self,
        i2c_addr: u8,
        data:     &[u8],
    ) -> Result<(), I3cError>;

    /// Read from a legacy I2C device on the I3C bus.
    fn i2c_read(
        &mut self,
        i2c_addr: u8,
        buf:      &mut [u8],
    ) -> Result<(), I3cError>;
}
```

### 8.3 Hardware Abstraction Layer Implementation (Rust)

```rust
//! i3c/hal_impl.rs — Platform-specific I3C controller driver

use super::{CccId, DeviceDesc, DynAddr, I3cController, I3cError,
            ProvisionedId};

/// Register offsets (example for a generic I3C controller peripheral).
mod regs {
    pub const CTRL:        usize = 0x00;
    pub const STATUS:      usize = 0x04;
    pub const INT_STATUS:  usize = 0x08;
    pub const INT_ENABLE:  usize = 0x0C;
    pub const DATA_TXFIFO: usize = 0x10;
    pub const DATA_RXFIFO: usize = 0x14;
    pub const CMD_QUEUE:   usize = 0x18;
    pub const RESP_QUEUE:  usize = 0x1C;
    pub const SCL_TIMING:  usize = 0x20;
    pub const IBI_CTRL:    usize = 0x24;
    pub const DAA_CTRL:    usize = 0x28;
}

mod ctrl_bits {
    pub const ENABLE:  u32 = 1 << 0;
    pub const RESET:   u32 = 1 << 1;
    pub const RESUME:  u32 = 1 << 3;
}

mod status_bits {
    pub const CMD_DONE:  u32 = 1 << 0;
    pub const RESP_AVAIL: u32 = 1 << 1;
    pub const IBI_AVAIL: u32 = 1 << 2;
    pub const HJ_AVAIL:  u32 = 1 << 3;
    pub const ERROR:     u32 = 1 << 8;
}

/// Platform I3C controller wrapping a memory-mapped peripheral.
pub struct I3cHalController {
    base: usize,
    free_addr: u8,
}

impl I3cHalController {
    /// # Safety
    /// `base` must be the correct MMIO base address of the I3C peripheral.
    pub unsafe fn new(base: usize) -> Self {
        let mut ctrl = Self { base, free_addr: 0x08 };
        ctrl.hw_init();
        ctrl
    }

    /// Read a 32-bit register.
    #[inline(always)]
    fn read_reg(&self, offset: usize) -> u32 {
        // SAFETY: caller guarantees base is a valid MMIO address
        unsafe {
            core::ptr::read_volatile((self.base + offset) as *const u32)
        }
    }

    /// Write a 32-bit register.
    #[inline(always)]
    fn write_reg(&self, offset: usize, val: u32) {
        // SAFETY: caller guarantees base is a valid MMIO address
        unsafe {
            core::ptr::write_volatile((self.base + offset) as *mut u32, val);
        }
    }

    fn hw_init(&mut self) {
        // Software reset
        self.write_reg(regs::CTRL, ctrl_bits::RESET);
        // Wait for reset complete (simple spin — use timeout in production)
        while self.read_reg(regs::STATUS) & (1 << 16) == 0 {}

        // SDR timing: push-pull high=2, low=2 cycles; OD high=8, low=16
        self.write_reg(regs::SCL_TIMING, (2 << 24) | (2 << 16) | (8 << 8) | 16);

        // Accept IBIs and Hot-Join requests
        self.write_reg(regs::IBI_CTRL, 0b11);

        // Enable interrupts for IBI and Hot-Join
        self.write_reg(regs::INT_ENABLE,
            status_bits::IBI_AVAIL | status_bits::HJ_AVAIL);

        // Enable the controller
        self.write_reg(regs::CTRL, ctrl_bits::ENABLE);
    }

    fn wait_cmd_done(&self) -> Result<(), I3cError> {
        let mut timeout = 100_000u32;
        loop {
            let status = self.read_reg(regs::STATUS);
            if status & status_bits::ERROR != 0 {
                return Err(I3cError::BusError);
            }
            if status & status_bits::CMD_DONE != 0 {
                return Ok(());
            }
            timeout -= 1;
            if timeout == 0 {
                return Err(I3cError::Timeout);
            }
        }
    }

    fn enqueue_private_write(&self, addr: u8, data: &[u8]) -> Result<(), I3cError> {
        // Fill TX FIFO
        for &b in data {
            self.write_reg(regs::DATA_TXFIFO, b as u32);
        }
        // Enqueue command word: [addr:7][RnW:1][len:16][cmd_type:8]
        let cmd = ((addr as u32) << 25)
            | (0 << 24) // write
            | ((data.len() as u32) << 8)
            | 0x01; // private transfer
        self.write_reg(regs::CMD_QUEUE, cmd);
        self.wait_cmd_done()
    }

    fn enqueue_private_read(&self, addr: u8, buf: &mut [u8]) -> Result<(), I3cError> {
        let cmd = ((addr as u32) << 25)
            | (1 << 24) // read
            | ((buf.len() as u32) << 8)
            | 0x01;
        self.write_reg(regs::CMD_QUEUE, cmd);
        self.wait_cmd_done()?;

        for b in buf.iter_mut() {
            *b = self.read_reg(regs::DATA_RXFIFO) as u8;
        }
        Ok(())
    }
}

impl I3cController for I3cHalController {
    fn write(&mut self, addr: DynAddr, data: &[u8]) -> Result<(), I3cError> {
        self.enqueue_private_write(addr.0, data)
    }

    fn read(&mut self, addr: DynAddr, buf: &mut [u8]) -> Result<(), I3cError> {
        self.enqueue_private_read(addr.0, buf)
    }

    fn write_read(
        &mut self,
        addr:  DynAddr,
        write: &[u8],
        read:  &mut [u8],
    ) -> Result<(), I3cError> {
        // Write phase (no STOP between write and read)
        for &b in write {
            self.write_reg(regs::DATA_TXFIFO, b as u32);
        }
        let cmd_write = ((addr.0 as u32) << 25)
            | (0 << 24)
            | ((write.len() as u32) << 8)
            | 0x03; // combined transfer flag
        self.write_reg(regs::CMD_QUEUE, cmd_write);

        // Read phase
        let cmd_read = ((addr.0 as u32) << 25)
            | (1 << 24)
            | ((read.len() as u32) << 8)
            | 0x03;
        self.write_reg(regs::CMD_QUEUE, cmd_read);
        self.wait_cmd_done()?;

        for b in read.iter_mut() {
            *b = self.read_reg(regs::DATA_RXFIFO) as u8;
        }
        Ok(())
    }

    fn send_broadcast_ccc(
        &mut self,
        ccc:  CccId,
        data: &[u8],
    ) -> Result<(), I3cError> {
        if !data.is_empty() {
            for &b in data {
                self.write_reg(regs::DATA_TXFIFO, b as u32);
            }
        }
        let cmd = ((ccc as u32) << 16) | (data.len() as u32) | (0x02 << 8);
        self.write_reg(regs::CMD_QUEUE, cmd);
        self.wait_cmd_done()
    }

    fn send_direct_ccc(
        &mut self,
        ccc:  CccId,
        addr: DynAddr,
        data: &mut [u8],
        read: bool,
    ) -> Result<(), I3cError> {
        if !read {
            for &b in data.iter() {
                self.write_reg(regs::DATA_TXFIFO, b as u32);
            }
        }
        let cmd = ((addr.0 as u32) << 25)
            | ((ccc as u32) << 16)
            | ((read as u32) << 24)
            | (data.len() as u32)
            | (0x04 << 8); // direct CCC type
        self.write_reg(regs::CMD_QUEUE, cmd);
        self.wait_cmd_done()?;

        if read {
            for b in data.iter_mut() {
                *b = self.read_reg(regs::DATA_RXFIFO) as u8;
            }
        }
        Ok(())
    }

    fn run_daa(
        &mut self,
        table: &mut [DeviceDesc],
    ) -> Result<usize, I3cError> {
        // Reset existing dynamic addresses
        self.send_broadcast_ccc(CccId::Rstdaa, &[])?;

        let mut count = 0usize;

        // Hardware-assisted DAA loop
        loop {
            // Trigger one ENTDAA step
            self.write_reg(regs::DAA_CTRL, 0x01);
            self.wait_cmd_done()?;

            let status = self.read_reg(regs::STATUS);
            if status & (1 << 20) == 0 {
                // No more devices responding
                break;
            }

            // Read the 8-byte PID+BCR+DCR response from RX FIFO
            let mut raw = [0u8; 8];
            for b in raw.iter_mut() {
                *b = self.read_reg(regs::DATA_RXFIFO) as u8;
            }

            let pid = u64::from_be_bytes({
                let mut arr = [0u8; 8];
                arr[2..].copy_from_slice(&raw[0..6]);
                arr
            });
            let bcr = raw[6];
            let dcr = raw[7];

            // Assign dynamic address
            let dyn_addr = self.free_addr;
            self.free_addr += 1;
            self.write_reg(regs::DAA_CTRL, (dyn_addr as u32) | (1 << 8));

            if count < table.len() {
                table[count] = DeviceDesc {
                    dyn_addr:    DynAddr(dyn_addr),
                    pid:         ProvisionedId(pid),
                    bcr,
                    dcr,
                    is_legacy:   false,
                    ibi_capable: (bcr & 0x02) != 0,
                };
            }
            count += 1;
        }

        Ok(count)
    }

    fn i2c_write(&mut self, i2c_addr: u8, data: &[u8]) -> Result<(), I3cError> {
        for &b in data {
            self.write_reg(regs::DATA_TXFIFO, b as u32);
        }
        // I2C transfer type = 0x08
        let cmd = ((i2c_addr as u32) << 25)
            | (0 << 24)
            | ((data.len() as u32) << 8)
            | 0x08;
        self.write_reg(regs::CMD_QUEUE, cmd);
        self.wait_cmd_done()
    }

    fn i2c_read(&mut self, i2c_addr: u8, buf: &mut [u8]) -> Result<(), I3cError> {
        let cmd = ((i2c_addr as u32) << 25)
            | (1 << 24)
            | ((buf.len() as u32) << 8)
            | 0x08;
        self.write_reg(regs::CMD_QUEUE, cmd);
        self.wait_cmd_done()?;
        for b in buf.iter_mut() {
            *b = self.read_reg(regs::DATA_RXFIFO) as u8;
        }
        Ok(())
    }
}
```

### 8.4 Device Driver Example: Accelerometer (Rust)

```rust
//! drivers/accel_i3c.rs — I3C-native accelerometer driver

use super::i3c::{DynAddr, I3cController, I3cError};

const REG_WHO_AM_I: u8 = 0x0F;
const REG_CTRL1_XL: u8 = 0x10;
const REG_OUTX_L:   u8 = 0x28;

const ODR_104HZ:  u8 = 0x40;
const FS_4G:      u8 = 0x08;

pub struct Accelerometer<B> {
    bus:  B,
    addr: DynAddr,
}

impl<B: I3cController> Accelerometer<B> {
    /// Create driver; does NOT initialize the device.
    pub fn new(bus: B, addr: DynAddr) -> Self {
        Self { bus, addr }
    }

    /// Initialize and verify device identity.
    pub fn init(&mut self) -> Result<(), I3cError> {
        let mut who = [0u8];
        self.bus.write_read(self.addr, &[REG_WHO_AM_I], &mut who)?;

        if who[0] != 0x6C {
            // Expected WHO_AM_I value for this sensor
            return Err(I3cError::BusError);
        }

        // Set ODR=104 Hz, FS=±4g
        self.bus.write(self.addr, &[REG_CTRL1_XL, ODR_104HZ | FS_4G])
    }

    /// Read a single accelerometer sample.
    /// Returns (x, y, z) in raw ADC counts (16-bit signed).
    pub fn read_accel(&mut self) -> Result<(i16, i16, i16), I3cError> {
        let mut buf = [0u8; 6];
        self.bus.write_read(self.addr, &[REG_OUTX_L], &mut buf)?;

        let x = i16::from_le_bytes([buf[0], buf[1]]);
        let y = i16::from_le_bytes([buf[2], buf[3]]);
        let z = i16::from_le_bytes([buf[4], buf[5]]);

        Ok((x, y, z))
    }

    /// Convert raw count to milli-g (for FS=±4g, sensitivity=0.122 mg/LSB).
    pub fn to_mg(raw: i16) -> f32 {
        raw as f32 * 0.122
    }
}
```

### 8.5 IBI Handler (Rust, interrupt-driven)

```rust
//! i3c/ibi.rs — In-Band Interrupt handling

use core::sync::atomic::{AtomicBool, Ordering};
use super::i3c::DynAddr;

/// Shared flag set by ISR, consumed by application task.
static ACCEL_DATA_READY: AtomicBool = AtomicBool::new(false);

/// Call from the I3C peripheral interrupt handler.
///
/// # Safety
/// Must only be called from the I3C IRQ context.
#[no_mangle]
pub unsafe extern "C" fn I3C0_IRQHandler() {
    // Platform-specific: read IBI status register
    let ibi_addr = read_ibi_addr(); // HAL function
    let ibi_data = read_ibi_data(); // HAL function (optional MDB byte)

    // Dispatch based on dynamic address
    match DynAddr(ibi_addr) {
        DynAddr(0x08) => {
            // Accelerometer signalled new data via IBI
            let _ = ibi_data; // Mandatory Data Byte may encode event type
            ACCEL_DATA_READY.store(true, Ordering::Release);
        }
        _ => {
            // Unknown IBI source — NACK it in hardware
            nack_ibi(); // HAL function
        }
    }

    // Clear interrupt flag
    clear_ibi_flag(); // HAL function
}

/// Application task polls this instead of a GPIO interrupt line.
pub fn is_accel_data_ready() -> bool {
    ACCEL_DATA_READY.swap(false, Ordering::Acquire)
}
```

### 8.6 Complete Application Example (Rust)

```rust
//! main.rs — Full I3C initialization and sensor loop

use hal_impl::I3cHalController;
use i3c::{CccId, DeviceDesc, I3cController};
use drivers::accel_i3c::Accelerometer;
use ibi::is_accel_data_ready;

fn main() -> ! {
    // 1. Initialize I3C controller (MMIO base: platform-specific)
    let mut bus = unsafe { I3cHalController::new(0x4000_5000) };

    // 2. Run DAA to discover all targets
    let mut devices = [DeviceDesc::default(); 16];
    let count = bus.run_daa(&mut devices).expect("DAA failed");

    // 3. Enable IBI on all capable devices
    for dev in &devices[..count] {
        if dev.ibi_capable {
            let mut payload = [0x01u8]; // Enable IBI events
            bus.send_direct_ccc(CccId::Enec, dev.dyn_addr, &mut payload, false)
               .expect("ENEC failed");
        }
    }

    // 4. Find accelerometer by DCR (example DCR = 0x44)
    let accel_addr = devices[..count]
        .iter()
        .find(|d| d.dcr == 0x44)
        .expect("Accelerometer not found")
        .dyn_addr;

    // 5. Initialize accelerometer driver
    let mut accel = Accelerometer::new(bus, accel_addr);
    accel.init().expect("Accel init failed");

    // 6. Main loop: wait for IBI then read data
    loop {
        if is_accel_data_ready() {
            match accel.read_accel() {
                Ok((x, y, z)) => {
                    let xmg = Accelerometer::to_mg(x);
                    let ymg = Accelerometer::to_mg(y);
                    let zmg = Accelerometer::to_mg(z);
                    // Process xmg, ymg, zmg...
                    let _ = (xmg, ymg, zmg);
                }
                Err(e) => {
                    // Handle error, e.g. reset bus
                    let _ = e;
                }
            }
        }

        // Low-power wait for interrupt (WFI on ARM Cortex-M)
        cortex_m::asm::wfi();
    }
}
```

---

## 9. Backward Compatibility: I2C Legacy Devices on I3C Bus

One of I3C's key strengths is the ability to operate legacy I2C devices alongside I3C targets on the same bus. The I3C controller handles the protocol difference transparently.

### Rules for Mixed Bus Operation

1. **Legacy I2C devices use static addresses** — they must be registered with the controller before DAA to prevent address collision.
2. **Legacy I2C devices are restricted to I2C speed** — the controller reverts to open-drain timing when addressing them.
3. **No IBIs from legacy devices** — the external interrupt approach is preserved for them.
4. **Broadcast CCCs are I3C-only** — legacy devices do not respond to CCCs.

### C Example: Registering a Legacy Device

```c
/**
 * @brief Register a legacy I2C device with the I3C controller
 *        so it is excluded from dynamic address assignment
 *        and addressed with I2C timing.
 */
int i3c_register_legacy_i2c(i3c_controller_t *ctrl,
                              uint8_t i2c_addr,
                              uint32_t max_speed_hz)
{
    /* Tell hardware to never assign this address during DAA */
    i3c_hal_reserve_static_addr(ctrl->regs, i2c_addr);

    /* Record in device table */
    i3c_device_t *dev = &ctrl->devices[ctrl->device_count++];
    dev->static_addr   = i2c_addr;
    dev->dynamic_addr  = i2c_addr; /* Use static addr for lookup */
    dev->is_i2c_legacy = true;
    dev->ibi_enabled   = false;

    return 0;
}

/* Usage: system has an I2C EEPROM at 0x50 */
void system_init(i3c_controller_t *ctrl)
{
    i3c_register_legacy_i2c(ctrl, 0x50, 400000); /* 400 kHz I2C */

    /* Now run DAA for I3C devices; 0x50 is excluded */
    i3c_do_daa(ctrl);
}
```

---

## 10. In-Band Interrupts (IBI)

IBI replaces per-device interrupt GPIO lines, saving PCB routing and MCU pins.

### How IBI Works

1. Target pulls SDA low at the start of a new frame.
2. Controller arbitrates — it sees SDA low before it could drive SCL.
3. Controller grants the IBI by clocking data from the target.
4. Target sends: `[dynamic_addr << 1 | 1][mandatory_data_byte][optional_bytes]`
5. Controller ACKs or NACKs the IBI.

### IBI vs. Dedicated Interrupt Pin

| Aspect | Dedicated GPIO IRQ | I3C IBI |
|---|---|---|
| Wiring | Extra GPIO per device | No extra wires |
| Latency | Very low (~ns) | One I3C frame (~µs) |
| Source identification | By pin number | By dynamic address in payload |
| Payload | None | Up to 8 optional data bytes |
| Priority | Hardware-controlled | Controller arbitrates |

### Enabling IBI on a Target (C)

```c
/* Send direct CCC ENEC to a specific device, enabling its IBI */
void enable_ibi_for_device(i3c_controller_t *ctrl, uint8_t dyn_addr)
{
    i3c_ccc_t ccc = {
        .ccc_id      = CCC_ENEC,
        .is_direct   = true,
        .target_addr = dyn_addr,
        .data        = { 0x01 },  /* Bit 0 = enable IBI */
        .data_len    = 1
    };
    i3c_send_ccc(ctrl, &ccc);
}
```

---

## 11. Dynamic Address Assignment (DAA)

### DAA Sequence

```
Controller                      Target(s)
   |                               |
   |-- BROADCAST: RSTDAA --------> |  (clear all dynamic addrs)
   |                               |
   |-- BROADCAST: ENTDAA --------> |  (enter DAA mode)
   |                               |
   |<-- [48-bit PID + BCR + DCR] --| (device N responds)
   |-- [assign addr 0x08] -------> |
   |                               |
   |<-- [48-bit PID + BCR + DCR] --| (device N+1 responds)
   |-- [assign addr 0x09] -------> |
   |                               |
   |  (no more devices)            |
   |-- STOP                        |
```

### Address Space Management

```c
#define I3C_RESERVED_ADDR_MIN  0x00
#define I3C_RESERVED_ADDR_MAX  0x07
#define I3C_BROADCAST_ADDR     0x7E
#define I3C_FIRST_FREE_ADDR    0x08

/**
 * Check if an address is valid for assignment.
 */
bool i3c_addr_is_assignable(uint8_t addr)
{
    if (addr <= I3C_RESERVED_ADDR_MAX)  return false;
    if (addr == I3C_BROADCAST_ADDR)     return false;
    return true;
}
```

---

## 12. Hot-Join Support

Hot-Join allows a device to connect to the bus after it is already operational and request a dynamic address.

### Hot-Join Sequence

1. New device pulls SDA low during a bus-idle period.
2. Controller detects Hot-Join request (SDA low without SCL activity).
3. Controller runs a targeted DAA for the new device.
4. Device is registered and can participate in normal transactions.

### C: Hot-Join Handler

```c
/**
 * Called from ISR when Hot-Join event detected.
 * Deferred to task context for safety in RTOS environments.
 */
void i3c_hotjoin_handler(i3c_controller_t *ctrl)
{
    /* Run DAA to pick up the newly joined device */
    int new_devices = i3c_do_daa(ctrl);

    if (new_devices > 0) {
        /* Notify application that a new device joined */
        app_on_device_joined(&ctrl->devices[ctrl->device_count - new_devices]);
    }
}
```

---

## 13. Testing and Validation

### 13.1 Unit Testing the Driver (C, with mock HAL)

```c
#include "unity.h"
#include "i3c.h"
#include "mock_i3c_hal.h"

static i3c_controller_t ctrl;

void setUp(void)
{
    i3c_hal_reset_mock();
    ctrl.regs         = (volatile uint32_t *)0xDEAD0000;
    ctrl.device_count = 0;
    ctrl.free_addr    = 0x08;
}

void test_daa_discovers_single_device(void)
{
    /* Arrange: mock HAL returns one device with known PID */
    i3c_hal_mock_daa_device(0xABCDEF012345ULL, 0x06, 0x44);

    /* Act */
    int count = i3c_do_daa(&ctrl);

    /* Assert */
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(0x08, ctrl.devices[0].dynamic_addr);
    TEST_ASSERT_EQUAL_HEX64(0xABCDEF012345ULL, ctrl.devices[0].pid);
    TEST_ASSERT_EQUAL_HEX8(0x06, ctrl.devices[0].bcr);
}

void test_write_returns_error_on_nack(void)
{
    /* Arrange: mock returns NACK */
    i3c_hal_mock_set_nack(0x08);

    /* Act */
    uint8_t data[] = {0x10, 0x20};
    int result = i3c_write(&ctrl, 0x08, data, sizeof(data));

    /* Assert */
    TEST_ASSERT_EQUAL(-1, result);
}
```

### 13.2 Unit Testing the Driver (Rust)

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use crate::i3c::{DynAddr, I3cError};

    /// Mock I3C bus for unit testing.
    struct MockI3c {
        pub last_write: Option<(DynAddr, Vec<u8>)>,
        pub read_data:  Vec<u8>,
        pub force_error: Option<I3cError>,
    }

    impl MockI3c {
        fn new() -> Self {
            Self { last_write: None, read_data: vec![], force_error: None }
        }
    }

    impl I3cController for MockI3c {
        fn write(&mut self, addr: DynAddr, data: &[u8]) -> Result<(), I3cError> {
            if let Some(e) = self.force_error { return Err(e); }
            self.last_write = Some((addr, data.to_vec()));
            Ok(())
        }

        fn read(&mut self, _addr: DynAddr, buf: &mut [u8]) -> Result<(), I3cError> {
            if let Some(e) = self.force_error { return Err(e); }
            let n = buf.len().min(self.read_data.len());
            buf[..n].copy_from_slice(&self.read_data[..n]);
            Ok(())
        }

        fn write_read(&mut self, addr: DynAddr, w: &[u8], r: &mut [u8]) -> Result<(), I3cError> {
            self.write(addr, w)?;
            self.read(addr, r)
        }

        // Other trait methods omitted for brevity
        fn send_broadcast_ccc(&mut self, _: CccId, _: &[u8]) -> Result<(), I3cError> { Ok(()) }
        fn send_direct_ccc(&mut self, _: CccId, _: DynAddr, _: &mut [u8], _: bool) -> Result<(), I3cError> { Ok(()) }
        fn run_daa(&mut self, _: &mut [DeviceDesc]) -> Result<usize, I3cError> { Ok(0) }
        fn i2c_write(&mut self, _: u8, _: &[u8]) -> Result<(), I3cError> { Ok(()) }
        fn i2c_read(&mut self, _: u8, _: &mut [u8]) -> Result<(), I3cError> { Ok(()) }
    }

    #[test]
    fn test_accel_init_sends_correct_config() {
        let mut mock = MockI3c::new();
        mock.read_data = vec![0x6C]; // WHO_AM_I response
        let mut accel = Accelerometer::new(&mut mock, DynAddr(0x08));
        assert!(accel.init().is_ok());
        // After init(), last write should be the config register write
        assert_eq!(
            mock.last_write,
            Some((DynAddr(0x08), vec![REG_CTRL1_XL, ODR_104HZ | FS_4G]))
        );
    }

    #[test]
    fn test_read_accel_parses_correctly() {
        let mut mock = MockI3c::new();
        // Simulate x=100, y=-200, z=300 in little-endian
        mock.read_data = vec![0x64, 0x00, 0x38, 0xFF, 0x2C, 0x01];
        let mut accel = Accelerometer::new(&mut mock, DynAddr(0x08));
        let (x, y, z) = accel.read_accel().unwrap();
        assert_eq!(x,  100);
        assert_eq!(y, -200);
        assert_eq!(z,  300);
    }

    #[test]
    fn test_read_error_propagates() {
        let mut mock = MockI3c::new();
        mock.force_error = Some(I3cError::Timeout);
        let mut accel = Accelerometer::new(&mut mock, DynAddr(0x08));
        assert_eq!(accel.read_accel(), Err(I3cError::Timeout));
    }
}
```

### 13.3 Protocol Analyzer Validation Points

When capturing traffic with a logic analyzer or dedicated I3C protocol decoder:

- Verify `RSTDAA` broadcast issued before `ENTDAA`.
- Confirm assigned dynamic addresses do not overlap with reserved range (0x00–0x07) or broadcast (0x7E).
- Check parity bits on HDR-DDR frames.
- Validate IBI sequence: target arbitration, controller grants, payload bytes received.
- Confirm legacy I2C devices receive open-drain frames (not push-pull).

---

## 14. Common Migration Pitfalls

### 14.1 Address Collision During DAA

**Problem:** A legacy I2C device has a static address that gets assigned to an I3C target during DAA.

**Fix:** Reserve all static I2C addresses in the controller before running DAA. Most I3C controllers have a `SETAASA` (Set All Addresses to Static Address) CCC or a reserved address register.

### 14.2 Missing Pull-up on Mixed Bus

**Problem:** I3C operates push-pull but the START condition requires an open-drain pull-up. Removing pull-ups breaks the bus.

**Fix:** Keep weak pull-ups (2.2 kΩ–3.3 kΩ) on the mixed bus even when pure push-pull I3C devices are present.

### 14.3 Speed Mismatch for Legacy Devices

**Problem:** Controller switches to SDR 12.5 MHz during a transaction but a legacy I2C device on the same bus cannot tolerate the signal.

**Fix:** Configure the controller to use I2C-compatible OD (open-drain) timing for addresses registered as legacy. Never broadcast ENTDAA timing changes to the whole bus without excluding legacy devices.

### 14.4 IBI Flooding

**Problem:** A misbehaving target continuously asserts IBI, starving normal bus traffic.

**Fix:** Use `DISEC` CCC to disable IBI for the offending device; implement a rate-limit in the IBI handler and NACK after a threshold.

### 14.5 Hot-Join Race Condition

**Problem:** A Hot-Join request arrives during an ongoing CCC, corrupting the transaction.

**Fix:** Process Hot-Join only when the bus is idle. Most controllers queue the request and assert an interrupt after the current transfer completes.

---

## 15. Summary

Migrating from I2C to I3C is a multi-faceted engineering effort that spans hardware, firmware, and system architecture. The key takeaways are:

**Protocol advantages worth migrating for:**
- 12.5× speed improvement over I2C Fast-mode (SDR vs. 1 MHz FM+) with HDR-DDR doubling that further.
- In-Band Interrupts eliminate per-device interrupt GPIO lines, saving PCB area and MCU pins.
- Dynamic Address Assignment via Provisioned ID enables robust plug-and-play device enumeration.
- Hot-Join support enables modular, field-expandable sensor networks.
- Standardized Common Command Codes reduce vendor-specific driver fragmentation.

**Migration strategy summary:**
1. Audit existing I2C devices and identify I3C-native replacements.
2. Choose a migration mode: pure I3C, mixed I3C/I2C, or parallel buses.
3. Re-evaluate pull-up resistors, bus capacitance, and voltage levels for the chosen configuration.
4. Implement the I3C driver in layers: HAL → bus abstraction → device driver → application.
5. Register all legacy I2C static addresses before running DAA to prevent address collisions.
6. Replace hardware interrupt lines with IBI where supported, removing external GPIO dependencies.
7. Validate with a protocol analyzer, unit tests (using mock buses in Rust), and integration tests on hardware.

**Both C/C++ and Rust** provide clean abstractions for I3C: C through struct-based context and HAL function pointers, and Rust through trait objects that enforce correct usage at compile time. The Rust approach particularly benefits from the type system — the `I3cController` trait prevents accidentally using an I2C function on an I3C-only path, and `Result<_, I3cError>` forces error handling at every transfer site.

I3C is the clear long-term path for high-speed, low-pin-count sensor buses in embedded systems, and the backward compatibility story makes the migration incremental rather than a full system redesign.

---

*Document version 1.0 — Covers MIPI I3C Basic v1.1 specification features.*