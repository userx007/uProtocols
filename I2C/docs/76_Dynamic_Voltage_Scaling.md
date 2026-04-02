# 76. Dynamic Voltage Scaling (DVS) for I2C Bus

**Conceptual depth** — the physics behind voltage-power quadratic relationship, I2C open-drain electrical constraints, pull-up resistor sizing at different voltages, and POR thresholds.

**C/C++ examples** — four progressively richer examples:
- A C header defining the DVS controller structure and operating point table
- A full C implementation with slew-rate-limited voltage ramping via PMBus
- A C++ `DvsManager` class with RAII, `std::mutex` thread safety, pre/post transition callbacks, and workload-predictive scaling using a moving average
- A usage/integration demo

**Rust examples** — four examples targeting both `std` and `no_std` environments:
- Trait definitions (`PmicDriver`, `I2cBusController`, `DelayUs`) enabling hardware-agnostic code
- A generic `DvsController<P, B, D>` with slew-rate ramp and bus arbitration
- Mock implementations and a full test suite covering ramp monotonicity, timeout handling, error propagation, and guaranteed bus release on fault
- An `AdaptiveDvs` wrapper using Q8 fixed-point exponential moving average for workload classification

**Safety section** covering brownout detection, atomic transitions, per-device voltage floors, thermal hysteresis, and watchdog interaction — plus a benchmarking table of expected power savings.

## Adjusting I2C Bus Voltage Levels Dynamically for Power Optimization

---

## Table of Contents

1. [Introduction](#introduction)
2. [Fundamentals of I2C Voltage and Power](#fundamentals-of-i2c-voltage-and-power)
3. [Why Dynamic Voltage Scaling on I2C?](#why-dynamic-voltage-scaling-on-i2c)
4. [DVS Architecture and Hardware Components](#dvs-architecture-and-hardware-components)
5. [I2C Electrical Constraints During Voltage Transitions](#i2c-electrical-constraints-during-voltage-transitions)
6. [DVS Strategies and Algorithms](#dvs-strategies-and-algorithms)
7. [Programming DVS in C/C++](#programming-dvs-in-cc)
8. [Programming DVS in Rust](#programming-dvs-in-rust)
9. [Safety and Reliability Considerations](#safety-and-reliability-considerations)
10. [Performance Benchmarking and Measurement](#performance-benchmarking-and-measurement)
11. [Summary](#summary)

---

## Introduction

Dynamic Voltage Scaling (DVS) is a power management technique that adjusts the supply voltage of a circuit or subsystem at runtime based on the current workload demands. In the context of the I2C (Inter-Integrated Circuit) bus, DVS refers to the practice of varying the bus voltage level — and sometimes the clock speed in tandem (DVFS: Dynamic Voltage and Frequency Scaling) — to minimise energy consumption while maintaining reliable communication.

Modern embedded systems face a fundamental tension between performance and power efficiency. The I2C bus, though a low-speed protocol by modern standards, is ubiquitous in battery-powered IoT nodes, wearables, industrial sensor networks, and automotive subsystems. Even modest reductions in I2C bus operating voltage can yield measurable improvements in battery life when aggregated over millions of bus cycles.

DVS on I2C is closely related to, and often a sub-function of, the broader system-level power management framework that also governs CPU, memory, and peripheral domains. However, it introduces unique challenges: I2C is a shared bus with open-drain topology, multiple devices with heterogeneous voltage tolerances, and strict timing constraints that must be preserved across voltage transitions.

---

## Fundamentals of I2C Voltage and Power

### Standard I2C Voltage Levels

The I2C specification (NXP UM10204) defines logic-level thresholds relative to VDD:

| Voltage Level | Condition              | Value (typical)          |
|---------------|------------------------|--------------------------|
| VDD           | Supply voltage         | 1.2 V – 5.5 V            |
| V_IH          | Input High threshold   | 0.7 × VDD                |
| V_IL          | Input Low threshold    | 0.3 × VDD                |
| V_OH          | Output High (pull-up)  | VDD − 0.4 V (open drain) |
| V_OL          | Output Low             | 0.4 V (sink current)     |

### Power Consumption in I2C

Dynamic power in CMOS circuits follows:

```
P_dynamic = α · C · V² · f
```

Where:
- `α` — activity factor (fraction of clock cycles with switching)
- `C` — node capacitance (I2C bus trace + device input capacitances)
- `V` — supply/bus voltage
- `f` — clock frequency

The quadratic dependence on voltage is the key insight: reducing VDD from 3.3 V to 1.8 V reduces dynamic power by a factor of (1.8/3.3)² ≈ 0.30, i.e., a **70% reduction** in dynamic power.

Static (leakage) power also decreases with lower VDD, though the relationship is more complex.

### Pull-up Resistor Power

On an I2C open-drain bus, current flows through the pull-up resistors whenever SDA or SCL is held LOW:

```
P_pullup = V² / R_pullup  (per line held LOW)
```

Reducing VDD reduces this dissipation as well. However, the pull-up resistor value must be re-evaluated when the voltage changes (see timing constraints below).

---

## Why Dynamic Voltage Scaling on I2C?

### Use Cases

**1. Sensor Polling in IoT Nodes**
A microcontroller wakes periodically, reads sensors over I2C, and returns to deep sleep. During active reading, the I2C bus can run at a lower voltage matched to the sensor's minimum operating voltage, then scale up if higher performance is needed.

**2. Multi-Domain Power Islands**
SoCs often partition peripherals into power domains. The I2C bus bridging two domains must track the lower of the two domain voltages during light-load conditions.

**3. Temperature-Aware Scaling**
At low temperatures, transistor threshold voltages rise, requiring slightly higher VDD for reliable logic. DVS can compensate.

**4. Battery Discharge Tracking**
As a LiPo cell discharges from 4.2 V to 3.0 V, a boost/buck regulator maintains VDD. DVS can track optimal voltage points along the battery's discharge curve.

**5. Workload Scheduling**
If the processor knows the next 500 ms will involve only light I2C sensor reads (not high-speed burst transfers), it can pre-scale voltage down.

---

## DVS Architecture and Hardware Components

### High-Level System Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Application Processor                │
│  ┌────────────┐   ┌────────────────┐   ┌─────────────┐  │
│  │  DVS       │   │  I2C Master    │   │  Power      │  │
│  │  Controller│──▶│  Controller    │   │  Monitor    │  │
│  └────┬───────┘   └───────┬────────┘   └──────┬──────┘  │
│       │                   │                   │         │
└───────┼───────────────────┼───────────────────┼─────────┘
        │                   │                   │
        │ PMBus/I2C         │ I2C Bus           │ ADC
        ▼                   ▼                   ▼
  ┌──────────┐        ┌──────────┐        ┌──────────┐
  │  DC/DC   │        │  I2C     │        │  Voltage │
  │ Regulator│        │  Slave   │        │  Sensor  │
  │ (PMIC)   │        │  Devices │        │          │
  └──────────┘        └──────────┘        └──────────┘
```

### Key Hardware Components

**1. Programmable DC/DC Regulator (PMIC)**
The voltage is set via a control interface (often PMBus over I2C, or a DAC output, or a GPIO-controlled resistor ladder). Common parts: Texas Instruments TPS62840, Renesas ISL9122A, Maxim MAX77654.

**2. Level Translators**
When the I2C bus spans different voltage domains (e.g., 3.3 V MCU talking to 1.8 V sensors), bidirectional level shifters (e.g., NXP PCA9306, TI TXS0102) are used. During DVS transitions, level translators must remain functional.

**3. Voltage Monitors**
A dedicated supervisor or ADC samples VDD to provide feedback for closed-loop DVS control.

**4. Soft-Start and Slew Rate Control**
PMICs implement configurable slew rates (mV/µs) to prevent supply glitches during voltage transitions.

---

## I2C Electrical Constraints During Voltage Transitions

DVS is not simply a matter of telling a PMIC "go to 1.8 V". Several I2C-specific constraints must be respected:

### 1. Bus Must Be Idle During Transitions

A voltage transition mid-transaction corrupts data. The DVS controller must:
- Wait for I2C bus STOP condition
- Assert bus busy flag (prevent new transactions)
- Execute voltage change
- Wait for VDD to settle (typically 100–500 µs)
- Optionally re-initialise devices if they reset due to voltage dip
- Release bus busy flag

### 2. Pull-up Resistor Sizing

The I2C rise time constant is:

```
τ_rise = R_pullup × C_bus
```

The I2C spec requires:

```
t_rise < 1000 ns  (Standard Mode, 100 kHz)
t_rise < 300 ns   (Fast Mode, 400 kHz)
t_rise < 120 ns   (Fast-Mode Plus, 1 MHz)
```

If the bus voltage drops, the pull-up resistor may need to be reduced (lower resistance = faster rise time). Conversely, pull-up current increases. This trade-off is often handled by:
- Switched pull-up banks (multiple resistors switched by GPIOs)
- Active pull-up circuits (current sources)
- Accepting slightly reduced I2C speed at lower voltages

### 3. Clock Speed Must Track Voltage

Most I2C slave devices have a minimum VDD for operation at a given speed. For example:
- A device rated for 400 kHz at 1.8 V may only support 100 kHz at 1.2 V

The DVS system must scale I2C clock frequency in coordination with voltage.

### 4. Device Reset Thresholds

Some I2C slaves have a POR (Power-On Reset) threshold (e.g., 1.0 V). Voltage dips below this threshold will reset the device, causing loss of configuration. DVS must either avoid this threshold or account for re-initialisation.

---

## DVS Strategies and Algorithms

### Strategy 1: Static Voltage Table

A lookup table maps operating modes to voltage/frequency pairs:

```
Mode         | VDD    | I2C Clock
-------------|--------|----------
Deep Sleep   | OFF    | N/A
Low Power    | 1.0 V  | 100 kHz
Normal       | 1.8 V  | 400 kHz
Performance  | 3.3 V  | 1 MHz
```

### Strategy 2: Workload-Predictive DVS

The scheduler analyses upcoming I2C operations and pre-scales voltage. Uses a moving average of transaction load to decide the operating point.

### Strategy 3: Closed-Loop DVS

A feedback loop continuously monitors timing margins (e.g., ACK latency, rise/fall times) and adjusts voltage to maintain a small margin above the minimum reliable operating point.

### Strategy 4: Cooperative Multi-Device DVS

In multi-master or complex bus topologies, all masters agree on a common voltage setpoint through a negotiation protocol before commanding the PMIC.

---

## Programming DVS in C/C++

The following examples demonstrate DVS concepts using a POSIX-like embedded API with a Linux I2C dev interface as the underlying model. Real implementations will differ based on the target RTOS and hardware abstraction layer.

### Example 1: Basic DVS Controller Structure (C)

```c
/*
 * dvs_i2c.h - Dynamic Voltage Scaling for I2C Bus
 *
 * Targets a system with:
 *   - Linux I2C dev interface (/dev/i2c-N)
 *   - PMBus-compatible PMIC on I2C bus 0
 *   - Sensor cluster on I2C bus 1
 */

#ifndef DVS_I2C_H
#define DVS_I2C_H

#include <stdint.h>
#include <stdbool.h>

/* Voltage operating points in millivolts */
#define DVS_VOLTAGE_OFF         0
#define DVS_VOLTAGE_LOW         1000   /* 1.000 V */
#define DVS_VOLTAGE_NOMINAL     1800   /* 1.800 V */
#define DVS_VOLTAGE_HIGH        3300   /* 3.300 V */

/* I2C clock frequencies in Hz */
#define DVS_I2C_CLK_SLOW        100000   /* 100 kHz Standard Mode  */
#define DVS_I2C_CLK_FAST        400000   /* 400 kHz Fast Mode       */
#define DVS_I2C_CLK_FAST_PLUS  1000000   /* 1 MHz Fast-Mode Plus    */

/* Slew rate limit (mV per microsecond) */
#define DVS_SLEW_RATE_MV_PER_US  5

/* Settling time after voltage change (microseconds) */
#define DVS_SETTLE_TIME_US       500

typedef enum {
    DVS_MODE_SLEEP      = 0,
    DVS_MODE_LOW_POWER  = 1,
    DVS_MODE_NORMAL     = 2,
    DVS_MODE_PERFORMANCE = 3,
} dvs_mode_t;

typedef struct {
    dvs_mode_t  mode;
    uint32_t    voltage_mv;
    uint32_t    i2c_clk_hz;
    const char *description;
} dvs_operating_point_t;

typedef struct {
    int          pmic_fd;          /* File descriptor for PMIC I2C bus */
    int          sensor_bus_fd;    /* File descriptor for sensor I2C bus */
    uint8_t      pmic_addr;        /* PMIC I2C address */
    uint32_t     current_voltage;  /* Current bus voltage in mV */
    dvs_mode_t   current_mode;
    bool         bus_busy;
} dvs_controller_t;

/* Function prototypes */
int  dvs_init(dvs_controller_t *ctrl, const char *pmic_bus,
              const char *sensor_bus, uint8_t pmic_addr);
int  dvs_set_mode(dvs_controller_t *ctrl, dvs_mode_t mode);
int  dvs_set_voltage(dvs_controller_t *ctrl, uint32_t voltage_mv);
bool dvs_is_bus_idle(dvs_controller_t *ctrl);
void dvs_cleanup(dvs_controller_t *ctrl);

#endif /* DVS_I2C_H */
```

### Example 2: DVS Controller Implementation (C)

```c
/*
 * dvs_i2c.c - DVS Controller Implementation
 */

#include "dvs_i2c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/* Operating point table */
static const dvs_operating_point_t dvs_op_table[] = {
    { DVS_MODE_SLEEP,        DVS_VOLTAGE_OFF,     0,                    "Sleep"       },
    { DVS_MODE_LOW_POWER,    DVS_VOLTAGE_LOW,     DVS_I2C_CLK_SLOW,    "Low Power"   },
    { DVS_MODE_NORMAL,       DVS_VOLTAGE_NOMINAL, DVS_I2C_CLK_FAST,    "Normal"      },
    { DVS_MODE_PERFORMANCE,  DVS_VOLTAGE_HIGH,    DVS_I2C_CLK_FAST_PLUS,"Performance"},
};

/*
 * Microsecond sleep helper
 */
static void sleep_us(uint32_t us)
{
    struct timespec ts = {
        .tv_sec  = us / 1000000,
        .tv_nsec = (us % 1000000) * 1000,
    };
    nanosleep(&ts, NULL);
}

/*
 * Write a byte to a PMBus register over I2C.
 * PMBus VOUT_COMMAND (0x21) sets output voltage.
 * Encoding is device-specific; here we use a linear format:
 *   raw = voltage_mv * 256 / 1000  (Q8 fixed-point, 1 V = 256)
 */
static int pmic_set_voltage_raw(dvs_controller_t *ctrl, uint32_t voltage_mv)
{
    uint8_t buf[3];
    uint16_t raw;

    if (voltage_mv == 0) {
        /* Turn off output via OPERATION register (0x01) */
        buf[0] = 0x01;  /* OPERATION command */
        buf[1] = 0x00;  /* OFF */
        if (write(ctrl->pmic_fd, buf, 2) != 2) {
            perror("pmic_set_voltage_raw: write OFF");
            return -EIO;
        }
        return 0;
    }

    /* Enable output if it was off */
    buf[0] = 0x01;  /* OPERATION */
    buf[1] = 0x80;  /* ON */
    if (write(ctrl->pmic_fd, buf, 2) != 2) {
        perror("pmic_set_voltage_raw: write ON");
        return -EIO;
    }

    /* Set voltage: VOUT_COMMAND (0x21), Q8 format */
    raw = (uint16_t)((voltage_mv * 256UL) / 1000UL);
    buf[0] = 0x21;              /* VOUT_COMMAND */
    buf[1] = (uint8_t)(raw & 0xFF);
    buf[2] = (uint8_t)(raw >> 8);
    if (write(ctrl->pmic_fd, buf, 3) != 3) {
        perror("pmic_set_voltage_raw: write VOUT_COMMAND");
        return -EIO;
    }

    return 0;
}

/*
 * Initialise the DVS controller.
 */
int dvs_init(dvs_controller_t *ctrl, const char *pmic_bus,
             const char *sensor_bus, uint8_t pmic_addr)
{
    memset(ctrl, 0, sizeof(*ctrl));

    ctrl->pmic_fd = open(pmic_bus, O_RDWR);
    if (ctrl->pmic_fd < 0) {
        perror("dvs_init: open pmic_bus");
        return -errno;
    }

    if (ioctl(ctrl->pmic_fd, I2C_SLAVE, pmic_addr) < 0) {
        perror("dvs_init: ioctl I2C_SLAVE (PMIC)");
        close(ctrl->pmic_fd);
        return -errno;
    }

    ctrl->sensor_bus_fd = open(sensor_bus, O_RDWR);
    if (ctrl->sensor_bus_fd < 0) {
        perror("dvs_init: open sensor_bus");
        close(ctrl->pmic_fd);
        return -errno;
    }

    ctrl->pmic_addr      = pmic_addr;
    ctrl->current_voltage = DVS_VOLTAGE_NOMINAL;
    ctrl->current_mode    = DVS_MODE_NORMAL;
    ctrl->bus_busy        = false;

    printf("[DVS] Initialised. PMIC at 0x%02X on %s\n", pmic_addr, pmic_bus);
    return 0;
}

/*
 * Wait until the I2C sensor bus is idle.
 * On Linux, poll the bus status via I2C_SMBUS or by attempting
 * a zero-byte read; real hardware uses a dedicated bus-busy signal.
 */
bool dvs_is_bus_idle(dvs_controller_t *ctrl)
{
    /* Simplified: in a real system check hardware bus-busy line
     * or RTOS semaphore. Here we use the software flag. */
    return !ctrl->bus_busy;
}

/*
 * Gradually ramp the voltage from current to target,
 * respecting the slew rate limit.
 */
static int dvs_ramp_voltage(dvs_controller_t *ctrl, uint32_t target_mv)
{
    int32_t  step;
    uint32_t current = ctrl->current_voltage;
    uint32_t step_mv = DVS_SLEW_RATE_MV_PER_US * 10; /* 10 µs steps */
    int      ret;

    if (current == target_mv) return 0;

    step = (target_mv > current) ? (int32_t)step_mv : -(int32_t)step_mv;

    printf("[DVS] Ramping voltage: %u mV -> %u mV (step %d mV / 10 µs)\n",
           current, target_mv, step);

    while (current != target_mv) {
        int32_t remaining = (int32_t)target_mv - (int32_t)current;
        if (abs(remaining) < abs(step)) {
            current = target_mv;
        } else {
            current = (uint32_t)((int32_t)current + step);
        }

        ret = pmic_set_voltage_raw(ctrl, current);
        if (ret < 0) return ret;

        sleep_us(10);
    }

    /* Wait for output to settle */
    sleep_us(DVS_SETTLE_TIME_US);
    ctrl->current_voltage = target_mv;
    printf("[DVS] Voltage settled at %u mV\n", target_mv);
    return 0;
}

/*
 * Set a new DVS operating mode.
 * Acquires bus, changes voltage, updates I2C clock speed.
 */
int dvs_set_mode(dvs_controller_t *ctrl, dvs_mode_t mode)
{
    const dvs_operating_point_t *op;
    int ret;
    uint32_t timeout_us = 50000; /* 50 ms timeout waiting for bus idle */

    if (mode >= (dvs_mode_t)(sizeof(dvs_op_table)/sizeof(dvs_op_table[0]))) {
        fprintf(stderr, "[DVS] Invalid mode %d\n", mode);
        return -EINVAL;
    }

    op = &dvs_op_table[mode];

    /* Step 1: Wait for I2C bus to be idle */
    while (!dvs_is_bus_idle(ctrl) && timeout_us > 0) {
        sleep_us(100);
        timeout_us -= 100;
    }
    if (!dvs_is_bus_idle(ctrl)) {
        fprintf(stderr, "[DVS] Timeout waiting for I2C bus idle\n");
        return -ETIMEDOUT;
    }

    /* Step 2: Mark bus as busy to prevent new transactions */
    ctrl->bus_busy = true;

    printf("[DVS] Switching to mode: %s (VDD=%u mV, CLK=%u Hz)\n",
           op->description, op->voltage_mv, op->i2c_clk_hz);

    /* Step 3: Ramp voltage to new level */
    ret = dvs_ramp_voltage(ctrl, op->voltage_mv);
    if (ret < 0) {
        ctrl->bus_busy = false;
        return ret;
    }

    /* Step 4: Update I2C clock frequency
     * On Linux, this would be set via /sys/bus/i2c/devices/.../speed
     * or via RTOS I2C driver API. Here shown as a placeholder. */
    if (op->i2c_clk_hz > 0) {
        printf("[DVS] Updating I2C clock to %u Hz\n", op->i2c_clk_hz);
        /* ioctl(ctrl->sensor_bus_fd, I2C_SET_CLOCK, op->i2c_clk_hz); */
        /* -- Platform-specific call here -- */
    }

    /* Step 5: Release bus */
    ctrl->current_mode = mode;
    ctrl->bus_busy      = false;

    printf("[DVS] Mode transition complete.\n");
    return 0;
}

/*
 * Clean up resources.
 */
void dvs_cleanup(dvs_controller_t *ctrl)
{
    if (ctrl->pmic_fd >= 0)        close(ctrl->pmic_fd);
    if (ctrl->sensor_bus_fd >= 0)  close(ctrl->sensor_bus_fd);
    memset(ctrl, 0, sizeof(*ctrl));
}
```

### Example 3: C++ DVS Manager with RAII and Callbacks

```cpp
/*
 * DvsManager.hpp - C++ DVS Manager for I2C Bus
 *
 * Features:
 *   - RAII resource management
 *   - Transition callbacks (pre/post hooks)
 *   - Workload-predictive scaling
 *   - Thread-safe operation (C++17 mutex)
 */

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>
#include <chrono>
#include <array>
#include <stdexcept>

namespace dvs {

enum class Mode : uint8_t {
    Sleep       = 0,
    LowPower    = 1,
    Normal      = 2,
    Performance = 3,
};

struct OperatingPoint {
    Mode        mode;
    uint32_t    voltage_mv;
    uint32_t    i2c_clk_hz;
    const char* label;
};

constexpr std::array<OperatingPoint, 4> kOperatingPoints = {{
    { Mode::Sleep,       0,    0,       "Sleep"       },
    { Mode::LowPower,    1000, 100'000, "Low Power"   },
    { Mode::Normal,      1800, 400'000, "Normal"      },
    { Mode::Performance, 3300, 1'000'000, "Performance"},
}};

/* Callback types for voltage transition hooks */
using PreTransitionCb  = std::function<void(Mode from, Mode to)>;
using PostTransitionCb = std::function<void(Mode current, bool success)>;

class DvsManager {
public:
    explicit DvsManager(int pmic_fd, int sensor_bus_fd,
                        uint32_t slew_rate_mv_per_us = 5)
        : pmic_fd_(pmic_fd)
        , sensor_bus_fd_(sensor_bus_fd)
        , slew_rate_(slew_rate_mv_per_us)
        , current_mode_(Mode::Normal)
        , current_voltage_mv_(1800)
    {}

    ~DvsManager() = default;

    /* Non-copyable, non-movable (owns file descriptors) */
    DvsManager(const DvsManager&)            = delete;
    DvsManager& operator=(const DvsManager&) = delete;

    void register_pre_transition(PreTransitionCb cb) {
        std::lock_guard<std::mutex> lk(mtx_);
        pre_cbs_.push_back(std::move(cb));
    }

    void register_post_transition(PostTransitionCb cb) {
        std::lock_guard<std::mutex> lk(mtx_);
        post_cbs_.push_back(std::move(cb));
    }

    /*
     * Transition to a new operating mode.
     * Returns true on success.
     */
    bool set_mode(Mode mode) {
        std::lock_guard<std::mutex> lk(mtx_);

        auto idx = static_cast<size_t>(mode);
        if (idx >= kOperatingPoints.size()) {
            throw std::invalid_argument("Invalid DVS mode");
        }

        const auto& op = kOperatingPoints[idx];
        Mode old_mode  = current_mode_;

        /* Fire pre-transition callbacks */
        for (auto& cb : pre_cbs_) cb(old_mode, mode);

        /* Acquire I2C bus (simplified: real system uses semaphore) */
        if (!acquire_bus(std::chrono::milliseconds(50))) {
            fire_post(mode, false);
            return false;
        }

        bool ok = true;
        try {
            ramp_voltage(op.voltage_mv);
            set_i2c_clock(op.i2c_clk_hz);
            current_mode_       = mode;
            current_voltage_mv_ = op.voltage_mv;
        } catch (const std::exception& e) {
            ok = false;
        }

        release_bus();
        fire_post(mode, ok);
        return ok;
    }

    Mode    current_mode()    const { return current_mode_;       }
    uint32_t voltage_mv()     const { return current_voltage_mv_; }

    /*
     * Workload hint: call before a burst of I2C transactions.
     * The manager decides whether to scale up based on history.
     */
    void hint_workload(size_t expected_transactions) {
        std::lock_guard<std::mutex> lk(mtx_);
        workload_history_.push_back(expected_transactions);
        if (workload_history_.size() > kHistoryWindow) {
            workload_history_.erase(workload_history_.begin());
        }
        auto best = compute_optimal_mode();
        if (best != current_mode_) {
            // Unlock before set_mode (which re-locks)
            mtx_.unlock();
            set_mode(best);
            mtx_.lock();
        }
    }

private:
    static constexpr size_t kHistoryWindow = 8;

    int      pmic_fd_;
    int      sensor_bus_fd_;
    uint32_t slew_rate_;    /* mV/µs */
    Mode     current_mode_;
    uint32_t current_voltage_mv_;
    bool     bus_acquired_{false};
    std::mutex mtx_;

    std::vector<PreTransitionCb>  pre_cbs_;
    std::vector<PostTransitionCb> post_cbs_;
    std::vector<size_t>           workload_history_;

    void fire_post(Mode m, bool ok) {
        for (auto& cb : post_cbs_) cb(m, ok);
    }

    bool acquire_bus(std::chrono::milliseconds timeout) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (bus_acquired_) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            /* In a real RTOS: pend on semaphore with timeout */
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        bus_acquired_ = true;
        return true;
    }

    void release_bus() { bus_acquired_ = false; }

    void ramp_voltage(uint32_t target_mv) {
        /* Slew-rate limited ramp */
        const uint32_t step_us = 10;
        uint32_t step_mv = slew_rate_ * step_us;
        uint32_t cur = current_voltage_mv_;

        while (cur != target_mv) {
            if (target_mv > cur) {
                cur = std::min(cur + step_mv, target_mv);
            } else {
                cur = (cur > step_mv) ? (cur - step_mv) : target_mv;
            }
            write_pmic_voltage(cur);
            /* Platform sleep: std::this_thread::sleep_for(...) */
        }
        /* Settling time */
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    void write_pmic_voltage(uint32_t voltage_mv) {
        /* PMBus VOUT_COMMAND write — platform-specific */
        uint16_t raw = static_cast<uint16_t>((voltage_mv * 256UL) / 1000UL);
        uint8_t buf[3] = { 0x21,
                           static_cast<uint8_t>(raw & 0xFF),
                           static_cast<uint8_t>(raw >> 8) };
        if (::write(pmic_fd_, buf, sizeof(buf)) != sizeof(buf)) {
            throw std::runtime_error("PMIC write failed");
        }
    }

    void set_i2c_clock(uint32_t hz) {
        if (hz == 0) return;
        /* Platform-specific I2C clock configuration:
         * e.g., ioctl(sensor_bus_fd_, I2C_SET_CLOCK, hz);
         *       or sysfs write to /sys/bus/i2c/.../speed
         */
        (void)hz;
    }

    Mode compute_optimal_mode() const {
        if (workload_history_.empty()) return Mode::Normal;
        size_t total = 0;
        for (auto n : workload_history_) total += n;
        size_t avg = total / workload_history_.size();

        if (avg == 0)    return Mode::Sleep;
        if (avg < 10)    return Mode::LowPower;
        if (avg < 100)   return Mode::Normal;
        return Mode::Performance;
    }
};

} // namespace dvs
```

### Example 4: C++ Usage / Integration Test

```cpp
/*
 * dvs_demo.cpp - Demonstration of DvsManager
 */

#include "DvsManager.hpp"
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

int main()
{
    /* Open I2C buses */
    int pmic_bus   = open("/dev/i2c-0", O_RDWR);
    int sensor_bus = open("/dev/i2c-1", O_RDWR);

    if (pmic_bus < 0 || sensor_bus < 0) {
        perror("open i2c bus");
        return 1;
    }

    dvs::DvsManager mgr(pmic_bus, sensor_bus, /*slew=*/5);

    /* Register lifecycle callbacks */
    mgr.register_pre_transition([](dvs::Mode from, dvs::Mode to) {
        printf("[CB] Preparing transition: %d -> %d\n",
               static_cast<int>(from), static_cast<int>(to));
    });

    mgr.register_post_transition([](dvs::Mode current, bool ok) {
        printf("[CB] Transition to mode %d: %s\n",
               static_cast<int>(current), ok ? "OK" : "FAILED");
    });

    /* Workload-predictive scaling */
    mgr.hint_workload(2);   /* Light sensor poll → expects Low Power */
    mgr.hint_workload(200); /* Burst transfer   → expects Performance */
    mgr.hint_workload(50);  /* Moderate          → expects Normal     */

    /* Explicit mode transitions */
    mgr.set_mode(dvs::Mode::LowPower);
    /* ... perform light I2C reads ... */

    mgr.set_mode(dvs::Mode::Performance);
    /* ... perform high-speed burst transfer ... */

    mgr.set_mode(dvs::Mode::Sleep);

    close(pmic_bus);
    close(sensor_bus);
    return 0;
}
```

---

## Programming DVS in Rust

Rust's ownership model, type system, and zero-cost abstractions make it particularly well suited to embedded DVS: bugs in resource management (double-close, use-after-free on file descriptors) are caught at compile time, and the `embedded-hal` trait ecosystem enables hardware-agnostic DVS drivers.

### Example 5: DVS Types and Traits (Rust)

```rust
//! dvs_i2c/src/lib.rs — Dynamic Voltage Scaling for I2C Bus

#![no_std]  // Compatible with bare-metal targets; remove for std targets

use core::fmt;

/// Voltage operating point in millivolts.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct Millivolts(pub u32);

/// I2C clock frequency in Hz.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Hertz(pub u32);

/// Discrete operating modes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DvsMode {
    Sleep,
    LowPower,
    Normal,
    Performance,
}

/// An operating point pairs a voltage with an I2C clock speed.
#[derive(Debug, Clone, Copy)]
pub struct OperatingPoint {
    pub mode:    DvsMode,
    pub voltage: Millivolts,
    pub clk:     Hertz,
}

impl OperatingPoint {
    pub const SLEEP: Self = Self {
        mode: DvsMode::Sleep, voltage: Millivolts(0), clk: Hertz(0),
    };
    pub const LOW_POWER: Self = Self {
        mode: DvsMode::LowPower, voltage: Millivolts(1000), clk: Hertz(100_000),
    };
    pub const NORMAL: Self = Self {
        mode: DvsMode::Normal, voltage: Millivolts(1800), clk: Hertz(400_000),
    };
    pub const PERFORMANCE: Self = Self {
        mode: DvsMode::Performance, voltage: Millivolts(3300), clk: Hertz(1_000_000),
    };
}

/// Errors that can arise during a DVS transition.
#[derive(Debug)]
pub enum DvsError<E> {
    /// Underlying PMIC I2C transaction failed.
    PmicComms(E),
    /// I2C bus did not become idle within the timeout.
    BusBusyTimeout,
    /// Requested voltage is outside the permitted range.
    VoltageOutOfRange(Millivolts),
    /// Invalid operating mode requested.
    InvalidMode,
}

impl<E: fmt::Debug> fmt::Display for DvsError<E> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

/// Trait that the PMIC driver must implement.
pub trait PmicDriver {
    type Error;
    /// Set the output voltage in millivolts.
    fn set_voltage(&mut self, voltage: Millivolts) -> Result<(), Self::Error>;
    /// Read the actual output voltage in millivolts.
    fn read_voltage(&mut self) -> Result<Millivolts, Self::Error>;
    /// Enable or disable the regulator output.
    fn set_enabled(&mut self, enabled: bool) -> Result<(), Self::Error>;
}

/// Trait that the I2C bus driver must implement for DVS.
pub trait I2cBusController {
    type Error;
    /// Returns true when no transaction is in progress.
    fn is_idle(&self) -> bool;
    /// Acquire the bus mutex; returns false on timeout.
    fn acquire(&mut self, timeout_us: u32) -> bool;
    /// Release the bus mutex.
    fn release(&mut self);
    /// Reconfigure the clock frequency.
    fn set_clock(&mut self, clk: Hertz) -> Result<(), Self::Error>;
}

/// Trait for a microsecond-resolution delay.
pub trait DelayUs {
    fn delay_us(&mut self, us: u32);
}
```

### Example 6: DVS Controller Implementation (Rust)

```rust
//! dvs_i2c/src/controller.rs

use crate::{
    DelayUs, DvsError, DvsMode, Hertz, I2cBusController,
    Millivolts, OperatingPoint, PmicDriver,
};

/// Configuration for the DVS controller.
pub struct DvsConfig {
    /// Maximum slew rate in mV per µs.
    pub slew_rate_mv_per_us: u32,
    /// Step interval in µs (how often voltage is incremented).
    pub step_interval_us: u32,
    /// Settling time after the target voltage is reached.
    pub settle_time_us: u32,
    /// Bus acquisition timeout in µs.
    pub bus_timeout_us: u32,
}

impl Default for DvsConfig {
    fn default() -> Self {
        Self {
            slew_rate_mv_per_us: 5,
            step_interval_us: 10,
            settle_time_us: 500,
            bus_timeout_us: 50_000,
        }
    }
}

/// The DVS controller manages voltage and I2C clock transitions.
pub struct DvsController<P, B, D> {
    pmic:            P,
    bus:             B,
    delay:           D,
    config:          DvsConfig,
    current_mode:    DvsMode,
    current_voltage: Millivolts,
}

impl<P, B, D, PE, BE> DvsController<P, B, D>
where
    P: PmicDriver<Error = PE>,
    B: I2cBusController<Error = BE>,
    D: DelayUs,
    PE: core::fmt::Debug,
    BE: core::fmt::Debug,
{
    /// Create a new DVS controller.
    pub fn new(pmic: P, bus: B, delay: D, config: DvsConfig) -> Self {
        Self {
            pmic,
            bus,
            delay,
            config,
            current_mode: DvsMode::Normal,
            current_voltage: Millivolts(1800),
        }
    }

    /// Transition to a new operating point.
    pub fn set_mode(&mut self, mode: DvsMode) -> Result<(), DvsError<PE>> {
        let op = Self::operating_point(mode);

        // Step 1: Acquire the I2C bus
        if !self.bus.acquire(self.config.bus_timeout_us) {
            return Err(DvsError::BusBusyTimeout);
        }

        // Step 2: Ramp voltage (slew-rate limited)
        let result = self.ramp_voltage(op.voltage);

        // Step 3: Update I2C clock regardless of ramp success
        if result.is_ok() && op.clk.0 > 0 {
            // Ignore bus clock errors for now (log in real system)
            let _ = self.bus.set_clock(op.clk);
        }

        // Step 4: Always release the bus
        self.bus.release();

        match result {
            Ok(()) => {
                self.current_mode    = mode;
                self.current_voltage = op.voltage;
                Ok(())
            }
            Err(e) => Err(e),
        }
    }

    /// Query the current operating mode.
    pub fn current_mode(&self) -> DvsMode { self.current_mode }

    /// Query the current voltage.
    pub fn current_voltage(&self) -> Millivolts { self.current_voltage }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    fn operating_point(mode: DvsMode) -> OperatingPoint {
        match mode {
            DvsMode::Sleep       => OperatingPoint::SLEEP,
            DvsMode::LowPower    => OperatingPoint::LOW_POWER,
            DvsMode::Normal      => OperatingPoint::NORMAL,
            DvsMode::Performance => OperatingPoint::PERFORMANCE,
        }
    }

    /// Slew-rate limited voltage ramp from current to target.
    fn ramp_voltage(&mut self, target: Millivolts) -> Result<(), DvsError<PE>> {
        let step_mv = self.config.slew_rate_mv_per_us
            * self.config.step_interval_us;

        let mut current = self.current_voltage.0;
        let target_mv   = target.0;

        while current != target_mv {
            current = if target_mv > current {
                core::cmp::min(current.saturating_add(step_mv), target_mv)
            } else {
                core::cmp::max(current.saturating_sub(step_mv), target_mv)
            };

            self.pmic
                .set_voltage(Millivolts(current))
                .map_err(DvsError::PmicComms)?;

            self.delay.delay_us(self.config.step_interval_us);
        }

        // Settling delay
        self.delay.delay_us(self.config.settle_time_us);
        Ok(())
    }
}
```

### Example 7: Mock PMIC Driver and Integration Test (Rust)

```rust
//! dvs_i2c/src/mock.rs — Mock hardware for unit testing

use crate::{DelayUs, Hertz, I2cBusController, Millivolts, PmicDriver};

/// Mock PMIC that records voltage commands.
pub struct MockPmic {
    pub voltage_log: std::vec::Vec<Millivolts>,
    pub current:     Millivolts,
    pub fail_at:     Option<Millivolts>,  /* Inject failure */
}

impl MockPmic {
    pub fn new(initial: Millivolts) -> Self {
        Self { voltage_log: std::vec::Vec::new(), current: initial, fail_at: None }
    }
}

impl PmicDriver for MockPmic {
    type Error = &'static str;

    fn set_voltage(&mut self, v: Millivolts) -> Result<(), Self::Error> {
        if self.fail_at == Some(v) {
            return Err("injected PMIC fault");
        }
        self.voltage_log.push(v);
        self.current = v;
        Ok(())
    }

    fn read_voltage(&mut self) -> Result<Millivolts, Self::Error> {
        Ok(self.current)
    }

    fn set_enabled(&mut self, _: bool) -> Result<(), Self::Error> { Ok(()) }
}

/// Mock I2C bus that tracks acquire/release and clock changes.
pub struct MockBus {
    pub acquired:   bool,
    pub clk_log:    std::vec::Vec<Hertz>,
    pub deny_after: u32,   /* Refuse acquire after N calls */
    call_count:     u32,
}

impl MockBus {
    pub fn new() -> Self {
        Self { acquired: false, clk_log: std::vec::Vec::new(),
               deny_after: u32::MAX, call_count: 0 }
    }
}

impl I2cBusController for MockBus {
    type Error = &'static str;

    fn is_idle(&self) -> bool { !self.acquired }

    fn acquire(&mut self, _timeout_us: u32) -> bool {
        self.call_count += 1;
        if self.call_count > self.deny_after { return false; }
        self.acquired = true;
        true
    }

    fn release(&mut self) { self.acquired = false; }

    fn set_clock(&mut self, clk: Hertz) -> Result<(), Self::Error> {
        self.clk_log.push(clk);
        Ok(())
    }
}

/// Mock delay that counts total microseconds waited.
pub struct MockDelay(pub u64);

impl DelayUs for MockDelay {
    fn delay_us(&mut self, us: u32) { self.0 += u64::from(us); }
}

// ─── Integration Tests ────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{DvsConfig, DvsController, DvsMode, Millivolts};

    fn make_controller(initial_mv: u32)
        -> DvsController<MockPmic, MockBus, MockDelay>
    {
        DvsController::new(
            MockPmic::new(Millivolts(initial_mv)),
            MockBus::new(),
            MockDelay(0),
            DvsConfig::default(),
        )
    }

    #[test]
    fn voltage_increases_monotonically_when_scaling_up() {
        let mut ctrl = make_controller(1000);
        ctrl.set_mode(DvsMode::Normal).expect("set_mode failed");

        let log = &ctrl.pmic.voltage_log;
        // Every step should be >= the previous
        for w in log.windows(2) {
            assert!(w[1] >= w[0], "voltage decreased during ramp-up: {:?}", w);
        }
        assert_eq!(*log.last().unwrap(), Millivolts(1800));
    }

    #[test]
    fn voltage_decreases_monotonically_when_scaling_down() {
        let mut ctrl = make_controller(3300);
        ctrl.set_mode(DvsMode::LowPower).expect("set_mode failed");

        let log = &ctrl.pmic.voltage_log;
        for w in log.windows(2) {
            assert!(w[1] <= w[0], "voltage increased during ramp-down: {:?}", w);
        }
        assert_eq!(*log.last().unwrap(), Millivolts(1000));
    }

    #[test]
    fn returns_bus_busy_timeout_when_acquire_denied() {
        let mut ctrl = make_controller(1800);
        ctrl.bus.deny_after = 0;  // Deny first acquire
        let result = ctrl.set_mode(DvsMode::LowPower);
        assert!(matches!(result, Err(crate::DvsError::BusBusyTimeout)));
    }

    #[test]
    fn i2c_clock_is_updated_on_transition() {
        let mut ctrl = make_controller(1000);
        ctrl.set_mode(DvsMode::Performance).expect("set_mode failed");
        assert_eq!(ctrl.bus.clk_log.last(), Some(&crate::Hertz(1_000_000)));
    }

    #[test]
    fn pmic_fault_is_propagated() {
        let mut ctrl = make_controller(1800);
        ctrl.pmic.fail_at = Some(Millivolts(1600)); // Fault at mid-ramp
        let result = ctrl.set_mode(DvsMode::LowPower);
        assert!(matches!(result, Err(crate::DvsError::PmicComms(_))));
        // Bus must be released even on fault
        assert!(!ctrl.bus.acquired);
    }
}
```

### Example 8: Workload-Adaptive DVS in Rust (no_std)

```rust
//! Workload-predictive DVS using an exponential moving average (EMA)

use crate::{DvsController, DvsMode, DelayUs, I2cBusController, PmicDriver};

pub struct AdaptiveDvs<P, B, D> {
    ctrl:        DvsController<P, B, D>,
    ema_load:    u32,     /* Q8 fixed-point EMA of transaction count */
    alpha_q8:    u32,     /* Smoothing factor in Q8 (e.g. 0.25 → 64) */
}

impl<P, B, D, PE, BE> AdaptiveDvs<P, B, D>
where
    P: PmicDriver<Error = PE>,
    B: I2cBusController<Error = BE>,
    D: DelayUs,
    PE: core::fmt::Debug,
    BE: core::fmt::Debug,
{
    /// `alpha` is the EMA smoothing factor in Q8 format (0–256).
    /// alpha=64 corresponds to α=0.25 (relatively slow adaptation).
    pub fn new(ctrl: DvsController<P, B, D>, alpha_q8: u32) -> Self {
        Self { ctrl, ema_load: 0, alpha_q8 }
    }

    /// Update load estimate and optionally switch mode.
    /// Call after each burst of `n_transactions` I2C transactions.
    pub fn update_load(&mut self, n_transactions: u32) {
        // EMA: ema = α·x + (1−α)·ema  (Q8 arithmetic)
        self.ema_load =
            (self.alpha_q8 * n_transactions * 256
             + (256 - self.alpha_q8) * self.ema_load) / 256;

        let desired = self.classify_load(self.ema_load);
        if desired != self.ctrl.current_mode() {
            let _ = self.ctrl.set_mode(desired);
        }
    }

    fn classify_load(&self, ema_q8: u32) -> DvsMode {
        // ema_q8 is transactions × 256; thresholds:
        // <  5 txn → LowPower,  5-50 → Normal,  >50 → Performance
        if ema_q8 < 5 * 256 {
            DvsMode::LowPower
        } else if ema_q8 < 50 * 256 {
            DvsMode::Normal
        } else {
            DvsMode::Performance
        }
    }

    pub fn inner(&mut self) -> &mut DvsController<P, B, D> {
        &mut self.ctrl
    }
}
```

---

## Safety and Reliability Considerations

### 1. Brownout Detection

If the PMIC fails mid-ramp (short circuit, overload), the voltage may drop below the processor's minimum operating voltage. A hardware brownout detector (BOD) should be configured to hold the system in reset rather than allow unpredictable behaviour. In software, monitor the PMIC status register after every voltage step.

### 2. Atomic Voltage Transitions

Never allow a new I2C transaction to begin between voltage steps. Use RTOS mutexes or hardware bus-busy signals. In the Rust examples, the `I2cBusController::acquire()` trait method enforces this contract.

### 3. Device-Specific Minimum Voltages

Before issuing a DVS command, validate that all devices on the bus support the target voltage. Maintain a compile-time or NVM-stored table of device voltage ratings. Refuse transitions that violate any device's minimum VDD.

### 4. Thermal Runaway Prevention

Rapid voltage cycling can increase regulator switching losses and raise PMIC temperature. Implement a hysteresis window: do not switch modes unless the EMA load has exceeded a threshold for multiple consecutive windows.

### 5. Power-On Sequencing

Some multi-device buses require a specific voltage power-up sequence. DVS must replicate this sequence when transitioning out of Sleep mode.

### 6. Watchdog Interaction

A voltage transition can temporarily stall the CPU (if it shares the regulated rail). Ensure the watchdog timer is either suspended during DVS transitions or has a long enough timeout to tolerate the expected ramp duration.

---

## Performance Benchmarking and Measurement

### Measuring DVS Benefit

Key metrics to instrument:

| Metric                         | How to Measure                                         |
|--------------------------------|--------------------------------------------------------|
| Active I2C bus current (mA)    | Series sense resistor + ADC or current probe           |
| Idle I2C bus leakage (µA)      | µA-range ammeter during sleep with pull-ups disabled   |
| Transition overhead (µs)       | GPIO toggle at start/end of `dvs_set_mode()`           |
| Energy per transaction (nJ)    | Oscilloscope current × voltage × time integral         |
| Worst-case rise time (ns)      | Oscilloscope: SDA/SCL edge triggered on start condition|

### Expected Power Savings

Illustrative figures for a 400 kHz I2C bus, 10 nF bus capacitance, 10 kΩ pull-ups:

| Transition          | Dynamic Power Saving | Pull-up Power Saving |
|---------------------|----------------------|----------------------|
| 3.3 V → 1.8 V       | ≈ 70%                | ≈ 70%                |
| 3.3 V → 1.0 V       | ≈ 91%                | ≈ 91%                |
| 1.8 V → 1.0 V       | ≈ 69%                | ≈ 69%                |

---

## Summary

Dynamic Voltage Scaling applied to the I2C bus is a powerful but nuanced power optimisation technique that can yield substantial energy savings — sometimes exceeding 70–90% of dynamic bus power — by reducing VDD in proportion to the current workload.

**Key engineering takeaways:**

- **Voltage scales quadratically with power**: even modest reductions (e.g., 3.3 V → 1.8 V) have large impact. DVS targets this relationship directly.

- **I2C electrical constraints must be respected**: the bus must be idle during transitions, pull-up resistors and clock speeds must be co-scaled with voltage, and device POR thresholds define a hard lower voltage floor.

- **Hardware and software co-design is essential**: a programmable PMIC (PMBus/DAC-controlled), a slew-rate limiter, bus-busy arbitration, and settling delays must all be in place before DVS can operate safely.

- **C/C++ implementations** benefit from clear state machine separation, RAII wrappers for file descriptors and bus locks, and callback hooks for pre/post transition actions. Workload prediction via moving averages enables proactive scaling.

- **Rust implementations** leverage the type system and trait abstraction to create hardware-agnostic, memory-safe DVS drivers. The ownership model guarantees that bus resources are always released — even in the error path — eliminating a common class of embedded power management bugs. The `no_std` compatibility makes Rust DVS drivers suitable for bare-metal MCU targets.

- **Adaptive strategies** (EMA-based workload classification, closed-loop margin tracking) outperform static voltage tables in real-world workloads by eliminating unnecessary over-provisioning of voltage.

- **Safety considerations** — brownout detection, watchdog management, device voltage rating validation, and thermal hysteresis — are non-negotiable in a production DVS system.

When implemented correctly, I2C Dynamic Voltage Scaling is a transparent optimisation: applications continue to read sensors, configure peripherals, and exchange data over I2C exactly as before, while the power subsystem quietly adapts voltage and frequency to match only what each workload phase actually requires.

---

*Document version 1.0 — I2C Topic 76: Dynamic Voltage Scaling*