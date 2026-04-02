# 80. I2C Power Sequencing

**Conceptual Coverage:**
- Why power sequencing matters — electrical hazards (back-powering, latch-up), protocol hazards (bus hangs, false START/STOP), and data integrity risks
- I2C bus electrical fundamentals and pull-up topology
- Voltage rail ordering rules (VCORE → VCC_IO → peripherals)
- Startup timing parameters from datasheets (`t_POR`, `t_startup`, `t_reset_recovery`)
- Bus initialization and 9-pulse bus recovery sequences
- Graceful power-down with EEPROM ACK polling
- Hot-plug considerations and dynamic power management (sleep/wake)
- A comprehensive common failure modes reference table

**C/C++ Code:**
- A platform-agnostic HAL struct with power-up, power-down, bus recovery, and EEPROM poll functions
- A Linux userspace implementation using `libgpiod` + `i2c-dev`
- A C++17 RAII `I2cPowerSequencer` class with automatic teardown on scope exit

**Rust Code:**
- An `embedded-hal 1.0` generic `I2cPowerSequencer` struct with full power-up/power-down methods and a `Drop` implementation guaranteeing safe teardown
- A Linux/Raspberry Pi userspace example using `rppal`
- An async Embassy/Tokio variant that yields to the scheduler during delays instead of busy-waiting

> **Proper power-up and power-down sequences for I2C devices**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Power Sequencing Matters](#why-power-sequencing-matters)
3. [I2C Bus Electrical Fundamentals](#i2c-bus-electrical-fundamentals)
4. [Power-Up Sequencing](#power-up-sequencing)
   - [Voltage Rail Order](#voltage-rail-order)
   - [Startup Timing Requirements](#startup-timing-requirements)
   - [Bus Initialization After Power-Up](#bus-initialization-after-power-up)
5. [Power-Down Sequencing](#power-down-sequencing)
   - [Graceful Shutdown Procedures](#graceful-shutdown-procedures)
   - [Reverse-Order Power-Down](#reverse-order-power-down)
6. [Hot-Plugging and Dynamic Power Management](#hot-plugging-and-dynamic-power-management)
7. [Reset Handling](#reset-handling)
8. [Common Failure Modes](#common-failure-modes)
9. [Implementation in C/C++](#implementation-in-cc)
10. [Implementation in Rust](#implementation-in-rust)
11. [Summary](#summary)

---

## Introduction

Power sequencing refers to the controlled order and timing in which voltage rails are applied to (or removed from) a system containing I2C devices. In mixed-voltage and multi-rail systems — which are ubiquitous in modern embedded electronics — improper power sequencing is one of the most common root causes of I2C bus hangs, device lock-ups, latch-up events, and permanent silicon damage.

The I2C protocol itself has no built-in concept of power states; it assumes that all devices sharing a bus are properly powered and initialized before communication begins. It is therefore entirely the responsibility of the system designer and firmware developer to enforce a correct power sequence.

---

## Why Power Sequencing Matters

### Electrical Hazards

When a device's I/O pins receive voltage before its core supply is stable, current can flow through ESD protection diodes into the unpowered supply rail. This is known as **back-powering** or **phantom powering**. Depending on the device, this can:

- Cause undefined logic states on SDA/SCL lines.
- Permanently damage ESD protection structures.
- Trigger **latch-up** in CMOS devices, causing destructive high currents.

### Protocol Hazards

I2C uses open-drain lines pulled up to VCC (or VCCIO). If pull-up resistors are referenced to a supply that is not yet stable, the lines may float or be held at incorrect voltages, causing:

- False START or STOP conditions detected by other devices.
- Partial byte transmission causing devices to enter a stuck mid-transaction state (the "bus hang").
- Clock stretching that never resolves.

### Data Integrity Hazards

Powering down an I2C device mid-transaction — especially an EEPROM or sensor with internal write cycles — can corrupt stored data or leave registers in undefined states.

---

## I2C Bus Electrical Fundamentals

Before examining sequencing, it is essential to understand the bus topology:

```
VCC_IO (e.g., 3.3V)
   |        |
  Rp       Rp        (Pull-up resistors, typically 1k–10kΩ)
   |        |
SCL --------+------- Device A (SCL)
                     Device B (SCL)

SDA --------+------- Device A (SDA)
                     Device B (SDA)
```

Key rules:
- **SCL and SDA must be HIGH (released) when the bus is idle.**
- Pull-up voltage **must be within the VIH specification** of all connected devices.
- If VCC_IO rises before device core supply (VCORE), input buffers may be unpowered while being driven.

---

## Power-Up Sequencing

### Voltage Rail Order

The canonical safe power-up order for I2C systems is:

```
1. GND / Reference (always first)
2. Core supply (VCORE) — digital logic supply
3. I/O supply (VCC_IO) — the rail to which pull-ups are referenced
4. Peripheral / analog supplies (VANA)
5. I2C bus pull-ups become active (VCCIO must be stable)
6. Assert RESET_N HIGH (release reset)
7. Wait for device startup time (t_startup per datasheet)
8. Begin I2C communication
```

> **Rule of thumb:** Never apply VCC_IO to I/O pins before VCORE is stable. For devices that use a single supply, this is handled automatically; for separate-rail devices, sequencing logic or a power sequencing IC (e.g., TPS3431, LTC2937) is required.

### Startup Timing Requirements

Each I2C device specifies timing parameters in its datasheet:

| Parameter        | Description                                      | Typical Value  |
|------------------|--------------------------------------------------|----------------|
| `t_POR`          | Power-On Reset hold time                         | 1 ms – 200 ms  |
| `t_startup`      | Time from VCC stable to first valid command      | 1 ms – 100 ms  |
| `t_reset`        | Reset pulse minimum width                        | 10 µs – 10 ms  |
| `t_reset_recovery` | Time after reset release to first I2C command  | 1 ms – 50 ms   |
| `f_SCL`          | Maximum clock frequency after power-up           | Per mode       |

Always use the **most conservative** (largest) timing value among all devices sharing a bus.

### Bus Initialization After Power-Up

After all supplies are stable and reset is de-asserted, the I2C bus must be verified to be in a known-good idle state before sending any commands:

1. **Check SCL and SDA are HIGH.** If SDA is LOW, a device is mid-transaction from a previous session. Perform a bus recovery sequence (9 clock pulses).
2. **Issue a General Call Reset** (`0x00 0x06`) if all devices on the bus support it.
3. **Verify device presence** using address ACK probing before issuing functional commands.

---

## Power-Down Sequencing

### Graceful Shutdown Procedures

Before removing power:

1. **Complete all pending I2C transactions.** Never cut power mid-write.
2. **For EEPROM / NVM devices:** Wait for the internal write cycle to complete (`t_WR`, typically 5–10 ms). Poll the device with an ACK check loop.
3. **Disable interrupts/alerts** from I2C devices (write to configuration registers) to prevent spurious signals during shutdown.
4. **Set output devices** (DACs, LED drivers, motor drivers) to a safe state (zero output).
5. **Place I2C master in high-impedance** or drive SCL/SDA LOW before removing pull-up supply.

### Reverse-Order Power-Down

Power-down should mirror power-up in reverse:

```
1. Stop I2C transactions
2. Assert RESET_N LOW (reset all devices)
3. Disable VCC_IO (pull-ups go inactive — lines float LOW via series resistance)
4. Disable peripheral/analog supplies
5. Disable VCORE
6. (GND remains)
```

Removing VCC_IO before asserting reset prevents the scenario where devices see SCL/SDA go LOW (pulled to GND through resistors) and interpret it as activity.

---

## Hot-Plugging and Dynamic Power Management

Hot-plugging I2C devices (adding or removing devices while the bus is powered) requires:

- **Current-limiting resistors** in series with SCL/SDA on the pluggable module to limit transient currents.
- **Power switch with slew-rate control** (e.g., TPS22965) to ramp VCC slowly.
- **Software detection:** After hot-plug detection (GPIO interrupt), perform bus scan before issuing commands.
- **Isolation switches** (e.g., PCA9306) that keep the bus lines at defined levels before the device powers up.

### Dynamic Power Management (Sleep / Resume)

Many I2C devices support low-power sleep modes. The sequence for entering and exiting sleep must also be treated as a mini power sequence:

- **Enter sleep:** Write sleep command register → wait for ACK → device draws minimal current.
- **Exit sleep:** Assert wake pin (if available) or write wake command → wait `t_wakeup` → issue normal commands.

Do **not** attempt I2C communication during the wakeup time.

---

## Reset Handling

Reset and power sequencing are closely intertwined:

- **Hardware reset (RESET_N pin):** Preferred method. Hold asserted during power ramp. Release only after all supplies are stable.
- **Software reset via I2C General Call:** Write `0x00 0x06`. Useful for runtime recovery but does not reinitialize analog blocks.
- **Bus recovery (stuck SDA):** Clock 9 times on SCL while SDA is released, then issue START + STOP. Required when a device was interrupted mid-byte.

```
Bus Recovery Sequence:
SCL: _‾_‾_‾_‾_‾_‾_‾_‾_‾_‾_ (9 pulses)
SDA: (released / HIGH)
         Then: S-STOP
```

---

## Common Failure Modes

| Failure                    | Cause                                              | Prevention                              |
|----------------------------|----------------------------------------------------|-----------------------------------------|
| Bus stuck LOW (SDA=0)      | Power cut mid-byte; device waiting for more clocks | Bus recovery; proper power-down seq     |
| Bus stuck LOW (SCL=0)      | Device clock-stretching with VCORE missing         | Ensure VCORE before enabling bus        |
| Back-powering via SDA/SCL  | VCC_IO applied before VCORE                        | Enforce rail order; use isolation FETs  |
| Device not responding      | t_startup not respected                            | Add post-reset delay per datasheet      |
| Data corruption (EEPROM)   | Power removed during write cycle                   | Poll ACK before power-down              |
| Latch-up                   | Large di/dt or reverse current during power ramp   | Slow rail ramp; sequencing IC           |
| Ghost ACKs on empty bus    | Floating SDA pulled LOW by capacitance             | Ensure pull-ups to stable VCC_IO        |

---

## Implementation in C/C++

### Platform-Agnostic Power Sequencing Framework

```c
/**
 * i2c_power_seq.h
 * I2C Power Sequencing - Platform-Agnostic Framework
 * Targets: Linux (ioctl), bare-metal MCU (HAL abstraction)
 */

#ifndef I2C_POWER_SEQ_H
#define I2C_POWER_SEQ_H

#include <stdint.h>
#include <stdbool.h>

/* Timing constants (milliseconds) */
#define T_VCORE_STABLE_MS       5U    /* Time to wait after VCORE enable */
#define T_VIO_STABLE_MS         2U    /* Time to wait after VCC_IO enable */
#define T_RESET_ASSERT_MS       10U   /* RESET_N assert duration           */
#define T_RESET_RECOVERY_MS     50U   /* Time after reset release           */
#define T_EEPROM_WRITE_MS       10U   /* Max EEPROM internal write cycle    */
#define T_BUS_RECOVERY_HALF_US  5U    /* Half-period for bus recovery clock */

/* Return codes */
typedef enum {
    PS_OK               = 0,
    PS_ERR_TIMEOUT      = -1,
    PS_ERR_BUS_STUCK    = -2,
    PS_ERR_RAIL_FAIL    = -3,
    PS_ERR_NO_ACK       = -4,
} ps_result_t;

/* Platform HAL — implement these for your target */
typedef struct {
    void     (*set_vcore_en)(bool on);
    void     (*set_vio_en)(bool on);
    void     (*set_reset_n)(bool high);
    bool     (*read_scl)(void);
    bool     (*read_sda)(void);
    void     (*drive_scl)(bool high);
    void     (*drive_sda)(bool high);
    void     (*delay_ms)(uint32_t ms);
    void     (*delay_us)(uint32_t us);
    /* I2C write: returns true on ACK */
    bool     (*i2c_write_byte)(uint8_t addr, uint8_t *data, uint16_t len);
} i2c_power_hal_t;

#endif /* I2C_POWER_SEQ_H */
```

```c
/**
 * i2c_power_seq.c
 * I2C Power Sequencing Implementation
 */

#include "i2c_power_seq.h"

/* ------------------------------------------------------------------ */
/* Bus Recovery                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Perform I2C bus recovery (SMBUS / NXP AN10216 method).
 *        Clocks SCL 9 times to free a device stuck mid-byte.
 *        Issues a STOP condition after clocking.
 *
 * @param hal  Platform HAL pointer
 * @return PS_OK if bus cleared, PS_ERR_BUS_STUCK if SDA still LOW
 */
ps_result_t i2c_bus_recovery(const i2c_power_hal_t *hal)
{
    /* Release SDA first */
    hal->drive_sda(true);
    hal->delay_us(T_BUS_RECOVERY_HALF_US);

    /* Clock 9 times — enough to complete any partial transaction */
    for (int i = 0; i < 9; i++) {
        hal->drive_scl(false);
        hal->delay_us(T_BUS_RECOVERY_HALF_US);
        hal->drive_scl(true);
        hal->delay_us(T_BUS_RECOVERY_HALF_US);

        /* Check if slave released SDA */
        if (hal->read_sda()) {
            break;
        }
    }

    /* Issue STOP: SDA LOW -> HIGH while SCL HIGH */
    hal->drive_sda(false);
    hal->delay_us(T_BUS_RECOVERY_HALF_US);
    hal->drive_scl(true);
    hal->delay_us(T_BUS_RECOVERY_HALF_US);
    hal->drive_sda(true);
    hal->delay_us(T_BUS_RECOVERY_HALF_US);

    /* Verify bus is now idle */
    if (!hal->read_sda() || !hal->read_scl()) {
        return PS_ERR_BUS_STUCK;
    }

    return PS_OK;
}

/* ------------------------------------------------------------------ */
/* Power-Up Sequence                                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Full I2C system power-up sequence.
 *
 * Sequence:
 *   1. Assert RESET_N (hold devices in reset)
 *   2. Enable VCORE, wait for stability
 *   3. Enable VCC_IO, wait for stability (pull-ups now active)
 *   4. Wait minimum reset assert time
 *   5. De-assert RESET_N
 *   6. Wait reset recovery time
 *   7. Verify bus is idle; recover if necessary
 *   8. Issue General Call Reset (optional)
 *
 * @param hal         Platform HAL pointer
 * @param gen_call_reset  Send General Call Reset 0x00 0x06 after power-up
 * @return PS_OK on success, error code otherwise
 */
ps_result_t i2c_power_up(const i2c_power_hal_t *hal, bool gen_call_reset)
{
    ps_result_t rc;

    /* Step 1: Assert reset before enabling rails */
    hal->set_reset_n(false);

    /* Step 2: Enable core supply */
    hal->set_vcore_en(true);
    hal->delay_ms(T_VCORE_STABLE_MS);

    /* Step 3: Enable I/O supply — pull-ups become active */
    hal->set_vio_en(true);
    hal->delay_ms(T_VIO_STABLE_MS);

    /* Step 4: Hold reset asserted for minimum pulse width */
    hal->delay_ms(T_RESET_ASSERT_MS);

    /* Step 5: Release reset */
    hal->set_reset_n(true);

    /* Step 6: Wait for all devices to complete POR / startup */
    hal->delay_ms(T_RESET_RECOVERY_MS);

    /* Step 7: Verify bus idle state */
    if (!hal->read_scl() || !hal->read_sda()) {
        /* Bus is not idle — attempt recovery */
        rc = i2c_bus_recovery(hal);
        if (rc != PS_OK) {
            return rc;
        }
    }

    /* Step 8: Optional General Call Reset */
    if (gen_call_reset) {
        uint8_t gc_reset[2] = { 0x00, 0x06 };
        /* General Call address is 0x00; ignore ACK/NAK */
        hal->i2c_write_byte(0x00, gc_reset, 2);
        hal->delay_ms(1);
    }

    return PS_OK;
}

/* ------------------------------------------------------------------ */
/* Power-Down Sequence                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Wait for an EEPROM write cycle to complete by polling ACK.
 *
 * @param hal       Platform HAL
 * @param dev_addr  7-bit I2C device address
 * @param timeout_ms  Maximum time to wait
 * @return PS_OK when ACK received, PS_ERR_TIMEOUT otherwise
 */
ps_result_t i2c_wait_eeprom_ready(const i2c_power_hal_t *hal,
                                   uint8_t dev_addr,
                                   uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    uint8_t dummy = 0;

    while (elapsed < timeout_ms) {
        if (hal->i2c_write_byte(dev_addr, &dummy, 0)) {
            return PS_OK;  /* ACK received — device ready */
        }
        hal->delay_ms(1);
        elapsed++;
    }

    return PS_ERR_TIMEOUT;
}

/**
 * @brief Full I2C system power-down sequence.
 *
 * Sequence:
 *   1. Wait for any pending I2C transactions (caller responsibility)
 *   2. Wait for EEPROM write cycles (if eeprom_addr != 0xFF)
 *   3. Assert RESET_N (halts I2C state machines gracefully)
 *   4. Disable VCC_IO (pull-ups go inactive)
 *   5. Disable VCORE
 *
 * @param hal           Platform HAL pointer
 * @param eeprom_addr   7-bit address of EEPROM to poll (0xFF = skip)
 * @return PS_OK on success, error code otherwise
 */
ps_result_t i2c_power_down(const i2c_power_hal_t *hal, uint8_t eeprom_addr)
{
    ps_result_t rc = PS_OK;

    /* Step 1: Wait for EEPROM write completion if applicable */
    if (eeprom_addr != 0xFF) {
        rc = i2c_wait_eeprom_ready(hal, eeprom_addr, T_EEPROM_WRITE_MS * 3);
        if (rc != PS_OK) {
            /* Proceed with power-down even on timeout —
             * data may be corrupt but hardware must be protected */
        }
    }

    /* Step 2: Assert reset — freezes device state cleanly */
    hal->set_reset_n(false);
    hal->delay_ms(1);

    /* Step 3: Disable VCC_IO — pull-ups go high-Z */
    hal->set_vio_en(false);
    hal->delay_ms(1);

    /* Step 4: Disable core supply */
    hal->set_vcore_en(false);

    return rc;
}
```

### Linux Userspace Example with `libgpiod` + `i2c-dev`

```c
/**
 * linux_i2c_power.c
 * Linux userspace I2C power sequencing using libgpiod and i2c-dev
 *
 * Compile: gcc -o linux_i2c_power linux_i2c_power.c -lgpiod
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <gpiod.h>

#define GPIO_CHIP       "/dev/gpiochip0"
#define GPIO_VCORE_EN   10   /* GPIO line for VCORE enable  */
#define GPIO_VIO_EN     11   /* GPIO line for VCC_IO enable */
#define GPIO_RESET_N    12   /* GPIO line for RESET_N       */
#define I2C_BUS         "/dev/i2c-1"

static void delay_ms(uint32_t ms)
{
    struct timespec ts = {
        .tv_sec  = ms / 1000,
        .tv_nsec = (ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);
}

/**
 * @brief Probe an I2C device by attempting a 0-byte write.
 *        Returns true if the device ACKs its address.
 */
static bool i2c_probe(int fd, uint8_t addr)
{
    if (ioctl(fd, I2C_SLAVE, addr) < 0) return false;
    return (write(fd, NULL, 0) == 0);
}

/**
 * @brief Wait until an I2C device ACKs or timeout expires.
 */
static bool i2c_wait_ack(int fd, uint8_t addr, uint32_t timeout_ms)
{
    for (uint32_t i = 0; i < timeout_ms; i++) {
        if (i2c_probe(fd, addr)) return true;
        delay_ms(1);
    }
    return false;
}

int main(void)
{
    int rc = EXIT_SUCCESS;

    /* Open GPIO chip */
    struct gpiod_chip *chip = gpiod_chip_open(GPIO_CHIP);
    if (!chip) {
        perror("gpiod_chip_open");
        return EXIT_FAILURE;
    }

    /* Acquire GPIO lines */
    struct gpiod_line *vcore_en = gpiod_chip_get_line(chip, GPIO_VCORE_EN);
    struct gpiod_line *vio_en   = gpiod_chip_get_line(chip, GPIO_VIO_EN);
    struct gpiod_line *reset_n  = gpiod_chip_get_line(chip, GPIO_RESET_N);

    gpiod_line_request_output(vcore_en, "power-seq", 0);
    gpiod_line_request_output(vio_en,   "power-seq", 0);
    gpiod_line_request_output(reset_n,  "power-seq", 0);  /* Assert reset */

    printf("[PWR] Asserting reset, enabling VCORE...\n");
    gpiod_line_set_value(vcore_en, 1);
    delay_ms(5);

    printf("[PWR] Enabling VCC_IO...\n");
    gpiod_line_set_value(vio_en, 1);
    delay_ms(10);

    printf("[PWR] Releasing reset...\n");
    gpiod_line_set_value(reset_n, 1);
    delay_ms(50);

    /* Open I2C bus */
    int i2c_fd = open(I2C_BUS, O_RDWR);
    if (i2c_fd < 0) {
        perror("open i2c");
        rc = EXIT_FAILURE;
        goto cleanup_gpio;
    }

    /* Wait for device at address 0x50 (example EEPROM) to respond */
    printf("[I2C] Waiting for device at 0x50...\n");
    if (!i2c_wait_ack(i2c_fd, 0x50, 200)) {
        fprintf(stderr, "[I2C] ERROR: Device at 0x50 did not respond after 200ms\n");
        rc = EXIT_FAILURE;
        goto cleanup_i2c;
    }
    printf("[I2C] Device ready. Beginning communication.\n");

    /* --- Normal operation would happen here --- */

    /* POWER-DOWN SEQUENCE */
    printf("[PWR] Starting power-down sequence...\n");

    /* Wait for EEPROM write cycle completion */
    printf("[I2C] Polling EEPROM for write completion...\n");
    if (!i2c_wait_ack(i2c_fd, 0x50, 30)) {
        fprintf(stderr, "[I2C] WARNING: EEPROM not ready, proceeding anyway\n");
    }

    printf("[PWR] Asserting reset...\n");
    gpiod_line_set_value(reset_n, 0);
    delay_ms(1);

    printf("[PWR] Disabling VCC_IO...\n");
    gpiod_line_set_value(vio_en, 0);
    delay_ms(1);

    printf("[PWR] Disabling VCORE...\n");
    gpiod_line_set_value(vcore_en, 0);

    printf("[PWR] Power-down complete.\n");

cleanup_i2c:
    close(i2c_fd);

cleanup_gpio:
    gpiod_line_release(vcore_en);
    gpiod_line_release(vio_en);
    gpiod_line_release(reset_n);
    gpiod_chip_close(chip);

    return rc;
}
```

### C++ RAII Power Sequencer Class

```cpp
/**
 * I2cPowerSequencer.hpp
 * RAII-based I2C Power Sequencer for C++17
 * Ensures power-down is always executed on scope exit.
 */

#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <chrono>
#include <thread>

class I2cPowerSequencer {
public:
    struct Config {
        uint32_t t_vcore_stable_ms  = 5;
        uint32_t t_vio_stable_ms    = 2;
        uint32_t t_reset_assert_ms  = 10;
        uint32_t t_reset_recovery_ms = 50;
        uint32_t t_eeprom_poll_ms   = 30;
        uint8_t  eeprom_addr        = 0xFF;  /* 0xFF = no EEPROM polling */
    };

    using GpioFn    = std::function<void(bool)>;
    using I2CProbe  = std::function<bool(uint8_t)>;

    I2cPowerSequencer(GpioFn vcore_en,
                      GpioFn vio_en,
                      GpioFn reset_n,
                      I2CProbe probe_fn,
                      Config cfg = {})
        : vcore_en_(std::move(vcore_en))
        , vio_en_(std::move(vio_en))
        , reset_n_(std::move(reset_n))
        , probe_fn_(std::move(probe_fn))
        , cfg_(cfg)
        , powered_(false)
    {}

    /* Destructor ensures safe power-down even on exceptions */
    ~I2cPowerSequencer() {
        if (powered_) {
            powerDown();
        }
    }

    /* Non-copyable, movable */
    I2cPowerSequencer(const I2cPowerSequencer&) = delete;
    I2cPowerSequencer& operator=(const I2cPowerSequencer&) = delete;

    /**
     * @brief Execute power-up sequence.
     * @param target_addr  I2C address to probe for readiness (0xFF = skip)
     * @throws std::runtime_error if device does not respond after power-up
     */
    void powerUp(uint8_t target_addr = 0xFF) {
        reset_n_(false);                          /* Assert reset          */

        vcore_en_(true);                          /* Enable core supply    */
        delay(cfg_.t_vcore_stable_ms);

        vio_en_(true);                            /* Enable I/O supply     */
        delay(cfg_.t_vio_stable_ms);

        delay(cfg_.t_reset_assert_ms);            /* Hold reset            */
        reset_n_(true);                           /* Release reset         */
        delay(cfg_.t_reset_recovery_ms);          /* Device startup time   */

        /* Verify device responds */
        if (target_addr != 0xFF) {
            if (!waitForAck(target_addr, 200)) {
                powerDown();  /* Safe cleanup before throwing */
                throw std::runtime_error("I2C device did not respond after power-up");
            }
        }

        powered_ = true;
    }

    /**
     * @brief Execute power-down sequence.
     *        Safe to call even if powerUp() failed partway through.
     */
    void powerDown() noexcept {
        /* Poll EEPROM completion if configured */
        if (cfg_.eeprom_addr != 0xFF) {
            waitForAck(cfg_.eeprom_addr, cfg_.t_eeprom_poll_ms);
        }

        reset_n_(false);    /* Assert reset — freeze device state */
        delay(1);
        vio_en_(false);     /* Remove pull-up supply              */
        delay(1);
        vcore_en_(false);   /* Remove core supply                 */

        powered_ = false;
    }

    bool isPowered() const noexcept { return powered_; }

private:
    GpioFn   vcore_en_, vio_en_, reset_n_;
    I2CProbe probe_fn_;
    Config   cfg_;
    bool     powered_;

    static void delay(uint32_t ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    bool waitForAck(uint8_t addr, uint32_t timeout_ms) {
        for (uint32_t i = 0; i < timeout_ms; ++i) {
            if (probe_fn_(addr)) return true;
            delay(1);
        }
        return false;
    }
};

/* Usage example:
 *
 * I2cPowerSequencer seq(
 *     [](bool on) { gpio_write(VCORE_EN_PIN, on); },
 *     [](bool on) { gpio_write(VIO_EN_PIN, on); },
 *     [](bool on) { gpio_write(RESET_N_PIN, on); },
 *     [](uint8_t addr) { return i2c_probe(addr); }
 * );
 *
 * seq.powerUp(0x50);   // Powers up and waits for EEPROM at 0x50
 * // ... use I2C bus ...
 * // seq.powerDown() called automatically on scope exit
 */
```

---

## Implementation in Rust

### Rust Power Sequencing with `embedded-hal` Traits

```rust
//! i2c_power_seq.rs
//! I2C Power Sequencing for Rust embedded targets
//! Uses embedded-hal 1.0 traits for portability.
//!
//! Add to Cargo.toml:
//!   embedded-hal = "1.0"
//!   [target.'cfg(target_os = "linux")'.dependencies]
//!   rppal = "0.18"   # Raspberry Pi HAL (example)

use core::fmt;
use embedded_hal::delay::DelayNs;
use embedded_hal::digital::OutputPin;
use embedded_hal::i2c::I2c;

/// Timing parameters for the power sequencing state machine (milliseconds).
#[derive(Clone, Debug)]
pub struct PowerSeqConfig {
    pub t_vcore_stable_ms: u32,
    pub t_vio_stable_ms: u32,
    pub t_reset_assert_ms: u32,
    pub t_reset_recovery_ms: u32,
    pub t_device_probe_timeout_ms: u32,
    /// I2C address of EEPROM to poll before power-down. None = skip.
    pub eeprom_addr: Option<u8>,
}

impl Default for PowerSeqConfig {
    fn default() -> Self {
        Self {
            t_vcore_stable_ms: 5,
            t_vio_stable_ms: 2,
            t_reset_assert_ms: 10,
            t_reset_recovery_ms: 50,
            t_device_probe_timeout_ms: 200,
            eeprom_addr: None,
        }
    }
}

/// Errors returned by power sequencing operations.
#[derive(Debug)]
pub enum PowerSeqError<GpioErr, I2cErr> {
    GpioError(GpioErr),
    I2cError(I2cErr),
    DeviceNotReady,
    BusStuck,
    EepromTimeout,
}

impl<G: fmt::Debug, I: fmt::Debug> fmt::Display for PowerSeqError<G, I> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::GpioError(e)  => write!(f, "GPIO error: {:?}", e),
            Self::I2cError(e)   => write!(f, "I2C error: {:?}", e),
            Self::DeviceNotReady => write!(f, "Device did not respond after power-up"),
            Self::BusStuck      => write!(f, "I2C bus stuck after recovery attempt"),
            Self::EepromTimeout => write!(f, "EEPROM write cycle timed out"),
        }
    }
}

/// I2C Power Sequencer
///
/// Generic over GPIO pin types, I2C bus type, and delay provider.
/// Owns the GPIO pins used for power control, ensuring they are
/// driven to safe states when dropped.
pub struct I2cPowerSequencer<VcorePin, VioPin, ResetPin, Bus, Delay> {
    vcore_en: VcorePin,
    vio_en: VioPin,
    reset_n: ResetPin,
    i2c: Bus,
    delay: Delay,
    config: PowerSeqConfig,
    powered: bool,
}

impl<VcorePin, VioPin, ResetPin, Bus, Delay>
    I2cPowerSequencer<VcorePin, VioPin, ResetPin, Bus, Delay>
where
    VcorePin: OutputPin,
    VioPin: OutputPin<Error = VcorePin::Error>,
    ResetPin: OutputPin<Error = VcorePin::Error>,
    Bus: I2c,
    Delay: DelayNs,
{
    pub fn new(
        vcore_en: VcorePin,
        vio_en: VioPin,
        reset_n: ResetPin,
        i2c: Bus,
        delay: Delay,
        config: PowerSeqConfig,
    ) -> Self {
        Self {
            vcore_en,
            vio_en,
            reset_n,
            i2c,
            delay,
            config,
            powered: false,
        }
    }

    /// Execute the full power-up sequence.
    ///
    /// # Arguments
    /// * `probe_addr` - Optional I2C address to probe for readiness after power-up.
    pub fn power_up(
        &mut self,
        probe_addr: Option<u8>,
    ) -> Result<(), PowerSeqError<VcorePin::Error, Bus::Error>> {
        // Step 1: Assert reset before enabling any rail
        self.reset_n
            .set_low()
            .map_err(PowerSeqError::GpioError)?;

        // Step 2: Enable core voltage supply
        self.vcore_en
            .set_high()
            .map_err(PowerSeqError::GpioError)?;
        self.delay.delay_ms(self.config.t_vcore_stable_ms);

        // Step 3: Enable I/O voltage supply (pull-ups become active)
        self.vio_en
            .set_high()
            .map_err(PowerSeqError::GpioError)?;
        self.delay.delay_ms(self.config.t_vio_stable_ms);

        // Step 4: Minimum reset assertion time
        self.delay.delay_ms(self.config.t_reset_assert_ms);

        // Step 5: Release reset
        self.reset_n
            .set_high()
            .map_err(PowerSeqError::GpioError)?;

        // Step 6: Wait for device POR / startup completion
        self.delay.delay_ms(self.config.t_reset_recovery_ms);

        // Step 7: Verify device responds if address provided
        if let Some(addr) = probe_addr {
            self.wait_for_ack(addr, self.config.t_device_probe_timeout_ms)
                .map_err(|_| PowerSeqError::DeviceNotReady)?;
        }

        self.powered = true;
        Ok(())
    }

    /// Execute the full power-down sequence.
    ///
    /// Polls EEPROM for write completion if configured, asserts reset,
    /// then removes rails in reverse order.
    pub fn power_down(
        &mut self,
    ) -> Result<(), PowerSeqError<VcorePin::Error, Bus::Error>> {
        // Step 1: Wait for EEPROM write cycle completion
        if let Some(addr) = self.config.eeprom_addr {
            // Non-fatal: log but continue even on timeout
            let _ = self.wait_for_ack(addr, 30);
        }

        // Step 2: Assert reset — halts I2C state machines cleanly
        self.reset_n
            .set_low()
            .map_err(PowerSeqError::GpioError)?;
        self.delay.delay_ms(1);

        // Step 3: Remove I/O supply (pull-ups go inactive)
        self.vio_en
            .set_low()
            .map_err(PowerSeqError::GpioError)?;
        self.delay.delay_ms(1);

        // Step 4: Remove core supply
        self.vcore_en
            .set_low()
            .map_err(PowerSeqError::GpioError)?;

        self.powered = false;
        Ok(())
    }

    /// Send I2C General Call Reset (address 0x00, data 0x06).
    /// Not all devices support this; responses are not expected.
    pub fn general_call_reset(&mut self) -> Result<(), PowerSeqError<VcorePin::Error, Bus::Error>> {
        self.i2c
            .write(0x00, &[0x06])
            .map_err(PowerSeqError::I2cError)?;
        self.delay.delay_ms(1);
        Ok(())
    }

    /// Returns true if the bus is currently powered.
    pub fn is_powered(&self) -> bool {
        self.powered
    }

    /// Consume the sequencer and return the I2C bus.
    pub fn release(mut self) -> Bus {
        // Ensure safe state before releasing
        if self.powered {
            let _ = self.power_down();
        }
        self.i2c
    }

    // ----------------------------------------------------------------
    // Private helpers
    // ----------------------------------------------------------------

    /// Poll an I2C address with 0-byte writes until ACK or timeout.
    fn wait_for_ack(
        &mut self,
        addr: u8,
        timeout_ms: u32,
    ) -> Result<(), Bus::Error> {
        for _ in 0..timeout_ms {
            if self.i2c.write(addr, &[]).is_ok() {
                return Ok(());
            }
            self.delay.delay_ms(1);
        }
        // Return last error from the I2C bus
        self.i2c.write(addr, &[])
    }
}

/// Implement Drop to guarantee safe power-down on scope exit.
/// This mirrors the RAII pattern in the C++ implementation.
impl<VcorePin, VioPin, ResetPin, Bus, Delay> Drop
    for I2cPowerSequencer<VcorePin, VioPin, ResetPin, Bus, Delay>
where
    VcorePin: OutputPin,
    VioPin: OutputPin<Error = VcorePin::Error>,
    ResetPin: OutputPin<Error = VcorePin::Error>,
    Bus: I2c,
    Delay: DelayNs,
{
    fn drop(&mut self) {
        if self.powered {
            // Best-effort power-down; ignore errors in Drop
            let _ = self.vcore_en.set_low();
            let _ = self.vio_en.set_low();
            let _ = self.reset_n.set_low();
            self.powered = false;
        }
    }
}
```

### Rust Linux Userspace Example

```rust
//! linux_i2c_power.rs
//! Linux userspace I2C power sequencing with rppal (Raspberry Pi)
//!
//! [dependencies]
//! rppal = "0.18"
//!
//! cargo run --bin linux_i2c_power

use std::thread;
use std::time::Duration;

#[cfg(target_os = "linux")]
fn main() -> Result<(), Box<dyn std::error::Error>> {
    use rppal::gpio::{Gpio, OutputPin};
    use rppal::i2c::I2c;

    const GPIO_VCORE_EN: u8  = 10;
    const GPIO_VIO_EN: u8    = 11;
    const GPIO_RESET_N: u8   = 12;
    const EEPROM_ADDR: u8    = 0x50;

    let gpio = Gpio::new()?;
    let mut vcore_en = gpio.get(GPIO_VCORE_EN)?.into_output_low();
    let mut vio_en   = gpio.get(GPIO_VIO_EN)?.into_output_low();
    let mut reset_n  = gpio.get(GPIO_RESET_N)?.into_output_low();

    // --- POWER-UP SEQUENCE ---
    println!("[PWR] Asserting reset, enabling VCORE...");
    // reset_n is already LOW
    vcore_en.set_high();
    thread::sleep(Duration::from_millis(5));

    println!("[PWR] Enabling VCC_IO...");
    vio_en.set_high();
    thread::sleep(Duration::from_millis(10));

    println!("[PWR] Releasing reset...");
    reset_n.set_high();
    thread::sleep(Duration::from_millis(50));

    // Open I2C bus
    let i2c = I2c::new()?;

    // Wait for EEPROM to respond
    println!("[I2C] Waiting for EEPROM at 0x{:02X}...", EEPROM_ADDR);
    let mut ready = false;
    for _ in 0..200 {
        if i2c.smbus_send_byte(EEPROM_ADDR, 0).is_ok() {
            ready = true;
            break;
        }
        thread::sleep(Duration::from_millis(1));
    }

    if !ready {
        eprintln!("[I2C] ERROR: EEPROM not responding!");
        // Safe power-down before exit
        reset_n.set_low();
        vio_en.set_low();
        vcore_en.set_low();
        return Err("device not ready".into());
    }

    println!("[I2C] EEPROM ready. Performing operations...");

    // --- Normal I2C operations would occur here ---
    // e.g., read/write sensor registers, configure ADCs, etc.

    // --- POWER-DOWN SEQUENCE ---
    println!("[PWR] Starting power-down...");

    // Poll for EEPROM write completion
    println!("[I2C] Polling EEPROM for write completion...");
    for _ in 0..30 {
        if i2c.smbus_send_byte(EEPROM_ADDR, 0).is_ok() {
            break;
        }
        thread::sleep(Duration::from_millis(1));
    }

    println!("[PWR] Asserting reset...");
    reset_n.set_low();
    thread::sleep(Duration::from_millis(1));

    println!("[PWR] Disabling VCC_IO...");
    vio_en.set_low();
    thread::sleep(Duration::from_millis(1));

    println!("[PWR] Disabling VCORE...");
    vcore_en.set_low();

    println!("[PWR] Power-down complete.");
    Ok(())
}

#[cfg(not(target_os = "linux"))]
fn main() {
    eprintln!("This example targets Linux (Raspberry Pi) with rppal.");
}
```

### Rust Async Power Sequencing (Tokio / Embassy)

```rust
//! async_power_seq.rs
//! Asynchronous I2C power sequencing using Embassy (embedded async)
//!
//! Suitable for RTOS-style embedded targets where blocking delay
//! wastes CPU time that could serve other tasks.

use embassy_time::{Duration, Timer};
use embedded_hal_async::i2c::I2c;

pub struct AsyncPowerSeq<VcorePin, VioPin, ResetPin, Bus> {
    vcore_en: VcorePin,
    vio_en: VioPin,
    reset_n: ResetPin,
    i2c: Bus,
}

impl<VcorePin, VioPin, ResetPin, Bus> AsyncPowerSeq<VcorePin, VioPin, ResetPin, Bus>
where
    VcorePin: embedded_hal::digital::OutputPin,
    VioPin: embedded_hal::digital::OutputPin,
    ResetPin: embedded_hal::digital::OutputPin,
    Bus: I2c,
{
    pub fn new(vcore_en: VcorePin, vio_en: VioPin, reset_n: ResetPin, i2c: Bus) -> Self {
        Self { vcore_en, vio_en, reset_n, i2c }
    }

    /// Asynchronous power-up — yields to scheduler during delays.
    pub async fn power_up(&mut self, probe_addr: Option<u8>) -> Result<(), Bus::Error> {
        let _ = self.reset_n.set_low();
        let _ = self.vcore_en.set_high();
        Timer::after(Duration::from_millis(5)).await;

        let _ = self.vio_en.set_high();
        Timer::after(Duration::from_millis(10)).await;

        let _ = self.reset_n.set_high();
        Timer::after(Duration::from_millis(50)).await;

        if let Some(addr) = probe_addr {
            for _ in 0..200u32 {
                if self.i2c.write(addr, &[]).await.is_ok() {
                    return Ok(());
                }
                Timer::after(Duration::from_millis(1)).await;
            }
            // Return last error if device never responded
            self.i2c.write(probe_addr.unwrap(), &[]).await?;
        }

        Ok(())
    }

    /// Asynchronous power-down — yields to scheduler during delays.
    pub async fn power_down(&mut self) {
        let _ = self.reset_n.set_low();
        Timer::after(Duration::from_millis(1)).await;
        let _ = self.vio_en.set_low();
        Timer::after(Duration::from_millis(1)).await;
        let _ = self.vcore_en.set_low();
    }
}
```

---

## Summary

I2C power sequencing is a critical yet frequently overlooked discipline in embedded systems design. The key takeaways are:

**Electrical safety:** Always enable VCORE before VCC_IO to prevent back-powering through ESD diodes and I/O pin structures. The reverse sequence can cause latch-up and permanent device damage.

**Timing compliance:** Each device specifies minimum power-on reset times, startup delays, and reset recovery times. These must be respected — particularly the most conservative values when multiple devices share a bus.

**Bus state verification:** After every power-up cycle, verify that SCL and SDA are both HIGH before sending commands. If SDA is stuck LOW, execute a 9-pulse bus recovery sequence followed by a STOP condition.

**Graceful power-down:** Never remove power mid-transaction. For EEPROM and NVM devices, poll for write cycle completion (ACK polling) before proceeding with shutdown. Assert reset before removing supply rails.

**Reverse-order power-down:** Mirror the power-up sequence in reverse — assert reset, remove VCC_IO, then VCORE. This ensures devices see a clean shutdown rather than interpreting rail removal as bus activity.

**RAII and Drop in software:** In both C++ and Rust, use RAII patterns to guarantee power-down executes even in error paths. This prevents the system from being left in an undefined power state when exceptions or early returns occur.

**Firmware architecture:** Treat the power sequencer as a distinct state machine or hardware abstraction layer, separate from application I2C logic. This enables reuse, testing, and replacement of the HAL across hardware revisions.

By combining hardware sequencing discipline (rail order, timing, reset signals) with robust firmware design (bus recovery, ACK polling, RAII teardown), systems achieve reliable I2C operation from cold boot through every power cycle throughout the product's lifetime.

---

*Document: 80_Power_Sequencing.md | I2C Topic Series*