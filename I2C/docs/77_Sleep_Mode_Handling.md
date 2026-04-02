# 77. I2C Sleep Mode Handling

**Structure:**
- Why sleep transitions are dangerous for I2C (mid-transaction bus lockup, register loss, floating pins)
- Sleep state taxonomy (ACPI S0ix/S3/S4 and ARM bare-metal Sleep/Stop/Standby)
- The 8 core design principles for safe sleep/wake handling

**C/C++ Examples:**
- **Linux kernel client driver** — using `dev_pm_ops`, `SET_SYSTEM_SLEEP_PM_OPS`, and `regmap` to save/restore slave configuration
- **Linux kernel adapter driver** — full context save/restore with the 9-clock bus recovery bit-bang sequence
- **Bare-metal C++ (STM32/FreeRTOS)** — an `I2CSleepHandler` class integrating with `vPortSuppressTicksAndSleep`, handling DMA abort, pin reconfiguration, and forced reset

**Rust Examples:**
- **`no_std` / embassy** — async task with register-level context save/restore and GPIO bit-bang recovery
- **Linux userspace** — D-Bus `PrepareForSleep` signal handling with retry-on-open after wake

**Also included:** a platform comparison table (STM32, i.MX, Raspberry Pi, RISC-V), a common pitfalls table with symptoms and fixes, and a testing strategy section.

## Managing I2C Transactions During System Sleep and Wake Transitions

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Sleep Mode Matters for I2C](#why-sleep-mode-matters-for-i2c)
3. [System Sleep States Overview](#system-sleep-states-overview)
4. [I2C Bus State During Sleep](#i2c-bus-state-during-sleep)
5. [Key Challenges](#key-challenges)
6. [Design Principles](#design-principles)
7. [C/C++ Implementation](#cc-implementation)
   - [Linux Kernel Driver (C)](#linux-kernel-driver-c)
   - [Bare-Metal / RTOS (C++)](#bare-metal--rtos-c)
8. [Rust Implementation](#rust-implementation)
   - [Embedded HAL (no_std)](#embedded-hal-nostd)
   - [Linux Userspace (std)](#linux-userspace-std)
9. [Platform-Specific Considerations](#platform-specific-considerations)
10. [Common Pitfalls and How to Avoid Them](#common-pitfalls-and-how-to-avoid-them)
11. [Testing Sleep/Wake I2C Behaviour](#testing-sleepwake-i2c-behaviour)
12. [Summary](#summary)

---

## Introduction

I2C (Inter-Integrated Circuit) is a synchronous, multi-master serial communication protocol widely used to connect sensors, EEPROMs, power management ICs, display controllers, and other peripherals to microcontrollers and application processors. While the protocol itself is well-defined during normal operation, **system sleep and wake transitions introduce a class of subtle bugs** that are often difficult to reproduce and even harder to debug.

Sleep mode handling encompasses everything that must happen to the I2C subsystem — the hardware controller (peripheral), the software driver stack, the bus lines (SDA/SCL), and the connected slave devices — before a host processor enters a low-power state, and everything that must be restored before I2C transactions can resume reliably after wake-up.

This document covers the theory, architectural considerations, and concrete programming patterns — with examples in **C/C++** (both Linux kernel space and bare-metal/RTOS) and **Rust** (both `no_std` embedded and Linux userspace) — for correctly handling I2C across sleep/wake cycles.

---

## Why Sleep Mode Matters for I2C

Modern embedded systems and SoCs aggressively manage power. A system may enter sleep tens or hundreds of times per second (tickless idle) or remain asleep for hours (deep sleep / hibernation). Every transition has consequences for the I2C bus:

| Concern | Impact if Ignored |
|---|---|
| Incomplete transactions at sleep entry | Bus stuck in mid-transfer; SDA or SCL held low (bus lockup) |
| Controller register state lost | After power-gating, controller needs full re-initialisation |
| Pull-up power lost | Bus lines float; noise causes spurious start conditions |
| Clock stretching mid-byte | Slave holds SCL low; master wakes to a locked bus |
| Slave device loses context | Slave state machines reset; register addresses, FIFO levels lost |
| Race between wake IRQ and I2C ready | Driver tries transactions before controller is ready |

---

## System Sleep States Overview

Different platforms use different sleep state nomenclature. The ACPI model (used on Linux/Windows PCs) and the ARM/RISC-V embedded model are described below.

### ACPI Sleep States (Linux, PC/SoC)

| State | Name | CPU | RAM | Peripherals | I2C Impact |
|---|---|---|---|---|---|
| S0ix / C-states | Runtime idle | Clock-gated | Retained | Partial power-gate | Controller may lose state |
| S3 | Suspend-to-RAM | Off | Retained | Powered off | Full re-init required |
| S4 | Suspend-to-Disk | Off | Saved to disk | Powered off | Full re-init required |

### Embedded Sleep States (ARM Cortex-M / RISC-V)

| State | Description | I2C Impact |
|---|---|---|
| Sleep (WFI) | Core halted, peripherals running | Usually none |
| Deep Sleep / Stop | Core + most peripherals halted | Controller registers lost |
| Standby | Only RTC/wakeup logic active | Full re-init required |
| Shutdown | Everything off | Full re-init required |

---

## I2C Bus State During Sleep

Before entering sleep, the I2C bus **must** be in an idle state:

- **SCL = HIGH** (released by master)
- **SDA = HIGH** (released by all devices)
- No ongoing START condition
- No pending ACK/NACK

This is called the **bus free** state. Any deviation from this at sleep entry can cause:

1. A slave device to interpret wake activity as continuation of a previous (corrupt) transaction.
2. The bus to be permanently stuck (requiring a power cycle of the slave).
3. A 9-clock recovery sequence to be needed at wake time.

---

## Key Challenges

### 1. Ensuring Bus Idle Before Sleep

The driver must wait for any in-progress transaction to complete or abort it safely before asserting sleep. Simply cutting power mid-transaction leaves the bus in an undefined state.

### 2. Register Context Save/Restore

I2C controller registers (baud rate divider, addressing mode, interrupt masks, DMA configuration, FIFO thresholds) are typically in a power domain that is switched off during deep sleep. These must be saved before sleep and restored after wake, before the first transaction.

### 3. GPIO Pin State

During deep sleep, I2C pins may revert to GPIO mode. The pull-up resistors (internal or external) may lose power. The driver must reconfigure pins to I2C function after wake.

### 4. Clock Recovery (9-Clock Sequence)

If a slave is stuck mid-byte (holding SDA low), the master must send up to 9 SCL pulses followed by a STOP condition to unstick it. This recovery must be done in GPIO-bit-bang mode, before re-enabling the hardware I2C controller.

### 5. Power Sequencing of Slave Devices

Some slaves (e.g., inertial sensors, power management ICs) have their own sleep/wake sequences. The master must coordinate I2C transactions with the slave's own power state.

---

## Design Principles

1. **Drain the queue** — complete or cancel all pending transactions before sleep.
2. **Save context** — preserve all controller register state.
3. **Release the bus** — ensure SCL and SDA are HIGH before entering low-power mode.
4. **Recover on wake** — perform bus recovery if lines are not idle.
5. **Restore context** — reload saved register state and re-initialise the controller.
6. **Re-configure pins** — set GPIO alternate function back to I2C after wake.
7. **Verify bus ready** — confirm bus is free before issuing the first post-wake transaction.
8. **Use layered callbacks** — in OS-managed systems, hook into the suspend/resume framework cleanly.

---

## C/C++ Implementation

### Linux Kernel Driver (C)

Linux provides a PM (Power Management) framework with `suspend`/`resume` callbacks. An I2C client driver registers these callbacks via `struct dev_pm_ops`.

#### Basic I2C Client Driver with PM Callbacks

```c
// i2c_sensor_driver.c
// Example: I2C temperature sensor driver with sleep/wake handling

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/delay.h>

#define SENSOR_REG_CONFIG       0x00
#define SENSOR_REG_TEMP_MSB     0x01
#define SENSOR_REG_TEMP_LSB     0x02
#define SENSOR_REG_SHUTDOWN     0x01  // bit in CONFIG to enter device shutdown

struct sensor_data {
    struct i2c_client   *client;
    struct regmap       *regmap;
    u8                   saved_config;  // context saved across sleep
    bool                 suspended;
};

/* -----------------------------------------------------------------------
 * Device suspend: called before host enters sleep.
 * Goal: put the I2C slave into its lowest-power mode and record state.
 * ----------------------------------------------------------------------- */
static int sensor_suspend(struct device *dev)
{
    struct sensor_data *data = dev_get_drvdata(dev);
    unsigned int val;
    int ret;

    /* 1. Read current configuration so we can restore it on resume. */
    ret = regmap_read(data->regmap, SENSOR_REG_CONFIG, &val);
    if (ret) {
        dev_err(dev, "suspend: failed to read config: %d\n", ret);
        return ret;
    }
    data->saved_config = (u8)val;

    /* 2. Assert the device's own shutdown bit to minimise its power draw. */
    ret = regmap_update_bits(data->regmap, SENSOR_REG_CONFIG,
                             SENSOR_REG_SHUTDOWN, SENSOR_REG_SHUTDOWN);
    if (ret) {
        dev_err(dev, "suspend: failed to assert shutdown: %d\n", ret);
        return ret;
    }

    /* 3. Wait for the transaction to fully complete (belt-and-suspenders).
     *    The I2C adapter's i2c_transfer() is synchronous, but if DMA is in
     *    use the controller may not have flushed yet. */
    usleep_range(500, 1000);

    data->suspended = true;
    dev_dbg(dev, "suspended (saved config=0x%02x)\n", data->saved_config);
    return 0;
}

/* -----------------------------------------------------------------------
 * Device resume: called after host wakes from sleep.
 * Goal: re-initialise the I2C controller (handled by the adapter driver),
 *       then restore the slave's register state.
 * ----------------------------------------------------------------------- */
static int sensor_resume(struct device *dev)
{
    struct sensor_data *data = dev_get_drvdata(dev);
    int ret;

    /*
     * At this point the Linux I2C adapter driver (e.g. i2c-designware,
     * i2c-imx, i2c-rk3x) has already run its own resume callback and
     * re-initialised the hardware controller.  We only need to deal with
     * the slave device's state.
     */

    /* 1. Restore the configuration register (clears the shutdown bit). */
    ret = regmap_write(data->regmap, SENSOR_REG_CONFIG, data->saved_config);
    if (ret) {
        dev_err(dev, "resume: failed to restore config: %d\n", ret);
        return ret;
    }

    /* 2. Give the sensor time to leave its shutdown state. */
    usleep_range(2000, 5000);

    data->suspended = false;
    dev_dbg(dev, "resumed (restored config=0x%02x)\n", data->saved_config);
    return 0;
}

/* -----------------------------------------------------------------------
 * Adapter-level bus recovery (called by the I2C core if the bus is stuck).
 * Implement i2c_bus_recovery_info for the adapter if you control it.
 * ----------------------------------------------------------------------- */
static void sensor_prepare_recovery(struct i2c_adapter *adap)
{
    /* Switch SDA/SCL pins to GPIO mode for bit-banging. */
    /* Platform-specific: gpiod_direction_output(), pinctrl_select_state() */
    dev_info(&adap->dev, "prepare_recovery: switching pins to GPIO\n");
}

static void sensor_unprepare_recovery(struct i2c_adapter *adap)
{
    /* Switch pins back to I2C alt-function. */
    dev_info(&adap->dev, "unprepare_recovery: switching pins to I2C\n");
}

/* -----------------------------------------------------------------------
 * Standard driver probe / remove
 * ----------------------------------------------------------------------- */
static int sensor_probe(struct i2c_client *client)
{
    struct sensor_data *data;
    static const struct regmap_config rcfg = {
        .reg_bits = 8,
        .val_bits = 8,
    };

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    data->regmap = devm_regmap_init_i2c(client, &rcfg);
    if (IS_ERR(data->regmap))
        return PTR_ERR(data->regmap);

    i2c_set_clientdata(client, data);

    /* Enable runtime PM so the framework calls our suspend/resume on idle. */
    pm_runtime_enable(&client->dev);
    pm_runtime_set_active(&client->dev);

    dev_info(&client->dev, "sensor probed at 0x%02x\n", client->addr);
    return 0;
}

static void sensor_remove(struct i2c_client *client)
{
    pm_runtime_disable(&client->dev);
}

static const struct dev_pm_ops sensor_pm_ops = {
    /* System sleep (S3/S4 / freeze) */
    SET_SYSTEM_SLEEP_PM_OPS(sensor_suspend, sensor_resume)
    /* Runtime PM (S0ix / autosuspend) */
    SET_RUNTIME_PM_OPS(sensor_suspend, sensor_resume, NULL)
};

static const struct i2c_device_id sensor_id[] = {
    { "my-temp-sensor", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
    .driver = {
        .name = "my-temp-sensor",
        .pm   = &sensor_pm_ops,
    },
    .probe   = sensor_probe,
    .remove  = sensor_remove,
    .id_table = sensor_id,
};
module_i2c_driver(sensor_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C temperature sensor with sleep/wake handling");
```

#### I2C Adapter Driver: Bus Recovery at Wake

When controlling the I2C adapter (controller) itself, you must also handle context save/restore and bus recovery:

```c
// i2c_adapter_pm.c — skeleton for adapter-level PM
// (Simplified; real adapters use platform clock/reset APIs)

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/gpio/consumer.h>

struct my_i2c_adapter {
    struct i2c_adapter   adap;
    void __iomem        *base;
    struct clk          *clk;
    struct reset_control *rst;
    struct gpio_desc    *scl_gpio;
    struct gpio_desc    *sda_gpio;

    /* Saved register context */
    u32  saved_cr1;   /* Control register 1  */
    u32  saved_cr2;   /* Control register 2  */
    u32  saved_timingr; /* Timing register   */
    u32  saved_oar1;  /* Own address 1       */
};

#define REG_CR1     0x00
#define REG_CR2     0x04
#define REG_TIMINGR 0x10
#define REG_OAR1    0x08

static int my_i2c_suspend(struct device *dev)
{
    struct my_i2c_adapter *priv = dev_get_drvdata(dev);

    /*
     * 1. Drain: the I2C core lock ensures no new transfers are started
     *    once we hold the adapter lock. Lock is acquired automatically by
     *    the PM framework on kernels >= 5.16 via i2c_mark_adapter_suspended().
     */
    i2c_mark_adapter_suspended(&priv->adap);

    /* 2. Save hardware register context before power is removed. */
    priv->saved_cr1      = readl(priv->base + REG_CR1);
    priv->saved_cr2      = readl(priv->base + REG_CR2);
    priv->saved_timingr  = readl(priv->base + REG_TIMINGR);
    priv->saved_oar1     = readl(priv->base + REG_OAR1);

    /* 3. Disable the controller to release SCL/SDA. */
    writel(priv->saved_cr1 & ~BIT(0), priv->base + REG_CR1); /* PE bit */

    /* 4. Gate the peripheral clock. */
    clk_disable_unprepare(priv->clk);

    return 0;
}

/* 9-clock bus recovery: bit-bang SCL while SDA is stuck low */
static int my_i2c_recover_bus(struct my_i2c_adapter *priv)
{
    int i;

    /* Switch to GPIO mode */
    gpiod_direction_output(priv->scl_gpio, 1);
    gpiod_direction_input(priv->sda_gpio);

    for (i = 0; i < 9; i++) {
        if (gpiod_get_value(priv->sda_gpio))
            break;  /* SDA released — slave has recovered */
        /* Toggle SCL */
        gpiod_set_value(priv->scl_gpio, 0);
        udelay(5);
        gpiod_set_value(priv->scl_gpio, 1);
        udelay(5);
    }

    /* Issue a STOP condition (SDA low→high while SCL high) */
    gpiod_direction_output(priv->sda_gpio, 0);
    udelay(5);
    gpiod_set_value(priv->scl_gpio, 1);
    udelay(5);
    gpiod_set_value(priv->sda_gpio, 1);
    udelay(5);

    /* Return to I2C alt-function — platform-specific pinctrl call here */
    return gpiod_get_value(priv->sda_gpio) ? 0 : -EBUSY;
}

static int my_i2c_resume(struct device *dev)
{
    struct my_i2c_adapter *priv = dev_get_drvdata(dev);
    int ret;

    /* 1. Re-enable the peripheral clock. */
    ret = clk_prepare_enable(priv->clk);
    if (ret)
        return ret;

    /* 2. Optionally de-assert reset. */
    reset_control_deassert(priv->rst);

    /* 3. Bus recovery: if SDA is low the slave is stuck mid-byte. */
    if (!gpiod_get_value(priv->sda_gpio)) {
        dev_warn(dev, "resume: SDA stuck low — attempting bus recovery\n");
        ret = my_i2c_recover_bus(priv);
        if (ret)
            dev_err(dev, "resume: bus recovery failed\n");
    }

    /* 4. Restore controller register context. */
    writel(priv->saved_timingr, priv->base + REG_TIMINGR);
    writel(priv->saved_oar1,    priv->base + REG_OAR1);
    writel(priv->saved_cr2,     priv->base + REG_CR2);
    writel(priv->saved_cr1,     priv->base + REG_CR1); /* Re-enables PE */

    /* 5. Allow new transfers. */
    i2c_mark_adapter_resumed(&priv->adap);

    return 0;
}

static const struct dev_pm_ops my_i2c_pm_ops = {
    SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(my_i2c_suspend, my_i2c_resume)
};
```

---

### Bare-Metal / RTOS (C++)

In bare-metal or RTOS environments (FreeRTOS, Zephyr, ThreadX), there is no OS PM framework. Sleep/wake hooks must be wired manually to the power management state machine.

```cpp
// i2c_sleep_handler.hpp
// Bare-metal / RTOS I2C sleep handler (STM32-style HAL, C++17)

#pragma once
#include <cstdint>
#include <atomic>
#include <array>

// Platform HAL types (e.g. STM32 HAL or custom BSP)
extern "C" {
    #include "stm32l4xx_hal.h"  // Replace with your target's HAL
}

class I2CSleepHandler {
public:
    explicit I2CSleepHandler(I2C_HandleTypeDef &hi2c)
        : hi2c_(hi2c), busy_(false) {}

    // ----------------------------------------------------------------
    // Call this from your sleep entry hook (e.g. vPortSuppressTicksAndSleep
    // in FreeRTOS, or a Zephyr PM_DEVICE hook).
    // Returns false if the bus could not be made idle and sleep should
    // be aborted.
    // ----------------------------------------------------------------
    bool prepareSleep()
    {
        // 1. Abort any ongoing DMA/IT transfer
        if (HAL_I2C_GetState(&hi2c_) != HAL_I2C_STATE_READY) {
            HAL_I2C_Master_Abort_IT(&hi2c_, hi2c_.Devaddress);
            // Poll until abort completes (with timeout)
            uint32_t timeout = HAL_GetTick() + 10;
            while (HAL_I2C_GetState(&hi2c_) != HAL_I2C_STATE_READY) {
                if (HAL_GetTick() > timeout) {
                    // Force reset of the I2C peripheral
                    forceReset();
                    break;
                }
            }
        }

        // 2. Verify bus lines are idle
        if (!isBusIdle()) {
            if (!recoverBus()) {
                return false;   // Cannot sleep: bus is stuck
            }
        }

        // 3. Save register context
        saveContext();

        // 4. Disable the I2C peripheral clock (optional: reduces leakage)
        __HAL_RCC_I2C1_FORCE_RESET();
        __HAL_RCC_I2C1_RELEASE_RESET();
        __HAL_RCC_I2C1_CLK_DISABLE();

        // 5. Configure SDA/SCL as high-impedance inputs to avoid locking bus
        GPIO_InitTypeDef gpio = {};
        gpio.Mode  = GPIO_MODE_INPUT;
        gpio.Pull  = GPIO_NOPULL;
        gpio.Pin   = GPIO_PIN_6 | GPIO_PIN_7;   // Adapt to your pinout
        HAL_GPIO_Init(GPIOB, &gpio);

        busy_.store(false, std::memory_order_release);
        return true;
    }

    // ----------------------------------------------------------------
    // Call this from your wake handler, before any I2C transaction.
    // ----------------------------------------------------------------
    void restoreAfterWake()
    {
        // 1. Re-enable peripheral clock
        __HAL_RCC_I2C1_CLK_ENABLE();

        // 2. Reconfigure pins to I2C alternate function
        GPIO_InitTypeDef gpio = {};
        gpio.Mode      = GPIO_MODE_AF_OD;
        gpio.Pull      = GPIO_PULLUP;
        gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF4_I2C1;
        gpio.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
        HAL_GPIO_Init(GPIOB, &gpio);

        // 3. Bus recovery if SDA is stuck low
        if (!isBusIdle()) {
            recoverBus();
        }

        // 4. Re-initialise the I2C peripheral using saved context
        restoreContext();

        busy_.store(true, std::memory_order_release);
    }

private:
    struct I2CContext {
        uint32_t CR1;
        uint32_t CR2;
        uint32_t OAR1;
        uint32_t OAR2;
        uint32_t TIMINGR;
        uint32_t TIMEOUTR;
    };

    I2C_HandleTypeDef &hi2c_;
    std::atomic<bool>  busy_;
    I2CContext         ctx_{};

    void saveContext() {
        I2C_TypeDef *i2c = hi2c_.Instance;
        ctx_.CR1      = i2c->CR1;
        ctx_.CR2      = i2c->CR2;
        ctx_.OAR1     = i2c->OAR1;
        ctx_.OAR2     = i2c->OAR2;
        ctx_.TIMINGR  = i2c->TIMINGR;
        ctx_.TIMEOUTR = i2c->TIMEOUTR;
    }

    void restoreContext() {
        I2C_TypeDef *i2c = hi2c_.Instance;
        // Write timing before enabling PE
        i2c->TIMINGR  = ctx_.TIMINGR;
        i2c->TIMEOUTR = ctx_.TIMEOUTR;
        i2c->OAR1     = ctx_.OAR1;
        i2c->OAR2     = ctx_.OAR2;
        i2c->CR2      = ctx_.CR2;
        i2c->CR1      = ctx_.CR1;  // PE bit last
    }

    bool isBusIdle() {
        // Read the GPIO pins directly (before alt-function is restored)
        // This example assumes GPIOB pins 6 (SCL) and 7 (SDA)
        uint32_t pins = GPIOB->IDR;
        bool scl_high = (pins & GPIO_PIN_6) != 0;
        bool sda_high = (pins & GPIO_PIN_7) != 0;
        return scl_high && sda_high;
    }

    bool recoverBus() {
        // Bit-bang 9 clocks to unstick slave
        GPIO_InitTypeDef gpio = {};
        gpio.Mode  = GPIO_MODE_OUTPUT_OD;
        gpio.Pull  = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        gpio.Pin   = GPIO_PIN_6;   // SCL
        HAL_GPIO_Init(GPIOB, &gpio);

        gpio.Mode  = GPIO_MODE_INPUT;
        gpio.Pin   = GPIO_PIN_7;   // SDA — input only to sense
        HAL_GPIO_Init(GPIOB, &gpio);

        for (int i = 0; i < 9; i++) {
            // Check if SDA is released
            if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET)
                break;
            // Toggle SCL
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
            HAL_Delay(1);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
            HAL_Delay(1);
        }

        // Issue STOP: SDA low -> high while SCL high
        gpio.Mode = GPIO_MODE_OUTPUT_OD;
        gpio.Pin  = GPIO_PIN_7;
        HAL_GPIO_Init(GPIOB, &gpio);

        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);  // SDA low
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);    // SCL high
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);    // SDA high (STOP)
        HAL_Delay(1);

        return isBusIdle();
    }

    void forceReset() {
        __HAL_RCC_I2C1_FORCE_RESET();
        HAL_Delay(1);
        __HAL_RCC_I2C1_RELEASE_RESET();
        HAL_I2C_Init(&hi2c_);
    }
};

// -----------------------------------------------------------------------
// Example integration with FreeRTOS tickless idle
// -----------------------------------------------------------------------

#include "FreeRTOS.h"
#include "task.h"

extern I2C_HandleTypeDef hi2c1;
static I2CSleepHandler g_i2c_sleep(hi2c1);

// Override FreeRTOS pre-sleep hook
extern "C" void vPortSuppressTicksAndSleep(TickType_t xExpectedIdleTime)
{
    // Only enter deep sleep if I2C can be safely suspended
    if (!g_i2c_sleep.prepareSleep()) {
        // I2C bus stuck — skip deep sleep, use shallow idle only
        __WFI();
        return;
    }

    // Enter deep sleep (platform-specific)
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    // --- WAKE-UP POINT ---
    // Re-initialise system clocks first (platform-specific)
    SystemClock_Config();

    // Then restore I2C
    g_i2c_sleep.restoreAfterWake();
}
```

---

## Rust Implementation

### Embedded HAL (`no_std`)

Rust's embedded ecosystem uses `embedded-hal` traits. The following example targets a Cortex-M MCU using the `embassy` async framework, which has built-in support for sleep/wake power management.

```rust
// src/i2c_sleep.rs
// no_std I2C sleep handler using embassy-stm32 and embedded-hal-async
//
// Cargo.toml dependencies:
//   embassy-stm32 = { version = "0.1", features = ["stm32l476rg", "time-driver-any"] }
//   embassy-time   = "0.3"
//   embassy-sync   = "0.5"
//   embedded-hal-async = "1.0"
//   cortex-m       = "0.7"
//   defmt          = "0.3"

#![no_std]
#![no_main]

use embassy_stm32::i2c::{self, I2c};
use embassy_stm32::gpio::{AnyPin, Flex, Pull, Speed};
use embassy_stm32::peripherals::I2C1;
use embassy_stm32::rcc::low_power_ready;
use embassy_time::{Duration, Timer};
use defmt::{info, warn, error};

/// Saved I2C controller register context for sleep/wake.
#[derive(Default, Clone, Copy)]
pub struct I2cContext {
    pub cr1:      u32,
    pub cr2:      u32,
    pub oar1:     u32,
    pub oar2:     u32,
    pub timingr:  u32,
    pub timeoutr: u32,
}

/// Reads the raw I2C peripheral registers via pointer arithmetic.
/// Safety: caller must ensure no concurrent access.
unsafe fn save_i2c_context(base: *const u32) -> I2cContext {
    I2cContext {
        cr1:      base.offset(0x00 / 4).read_volatile(),
        cr2:      base.offset(0x04 / 4).read_volatile(),
        oar1:     base.offset(0x08 / 4).read_volatile(),
        oar2:     base.offset(0x0C / 4).read_volatile(),
        timingr:  base.offset(0x10 / 4).read_volatile(),
        timeoutr: base.offset(0x14 / 4).read_volatile(),
    }
}

unsafe fn restore_i2c_context(base: *mut u32, ctx: &I2cContext) {
    // Write timing and own-address registers before enabling PE
    base.offset(0x10 / 4).write_volatile(ctx.timingr);
    base.offset(0x14 / 4).write_volatile(ctx.timeoutr);
    base.offset(0x08 / 4).write_volatile(ctx.oar1);
    base.offset(0x0C / 4).write_volatile(ctx.oar2);
    base.offset(0x04 / 4).write_volatile(ctx.cr2);
    // Write CR1 last — enables PE bit which starts the controller
    base.offset(0x00 / 4).write_volatile(ctx.cr1);
}

/// Performs a 9-clock bus recovery sequence via GPIO bit-bang.
///
/// `scl` and `sda` are configured as open-drain outputs.
/// Returns `true` if the bus is free after recovery.
async fn recover_i2c_bus(scl: &mut Flex<'_, AnyPin>,
                          sda: &mut Flex<'_, AnyPin>) -> bool {
    // Configure as open-drain outputs with pull-up
    scl.set_as_output(Speed::Low);
    sda.set_as_input(Pull::Up);  // Input: sense SDA

    for _ in 0..9 {
        if sda.is_high() {
            break;  // Slave released SDA
        }
        // Clock one bit
        scl.set_low();
        Timer::after(Duration::from_micros(5)).await;
        scl.set_high();
        Timer::after(Duration::from_micros(5)).await;
    }

    // Issue STOP condition: SDA low -> high while SCL high
    sda.set_as_output(Speed::Low);
    sda.set_low();
    Timer::after(Duration::from_micros(5)).await;
    scl.set_high();
    Timer::after(Duration::from_micros(5)).await;
    sda.set_high();   // SDA rising while SCL high = STOP
    Timer::after(Duration::from_micros(5)).await;

    sda.is_high() && scl.is_high()
}

/// High-level async task that manages I2C across sleep transitions.
///
/// This task cooperates with the embassy executor's low-power support:
/// when all tasks are `.await`ing, the executor calls `cortex_m::asm::wfi()`
/// or a deeper sleep depending on the board's `low_power_ready()` hook.
#[embassy_executor::task]
async fn i2c_manager_task(
    mut i2c: I2c<'static, I2C1>,
    mut scl: Flex<'static, AnyPin>,
    mut sda: Flex<'static, AnyPin>,
) {
    const I2C1_BASE: *mut u32 = 0x4000_5400 as *mut u32;
    let mut ctx = I2cContext::default();
    let mut is_sleeping = false;

    loop {
        // ---- NORMAL OPERATION ----------------------------------------
        if !is_sleeping {
            // Example: read temperature register 0x00 from slave 0x48
            let mut buf = [0u8; 2];
            match i2c.read(0x48, &mut buf).await {
                Ok(_) => {
                    let raw = i16::from_be_bytes(buf) >> 4;
                    info!("Temperature: {} °C", raw / 16);
                }
                Err(e) => warn!("I2C read error: {:?}", e),
            }
            Timer::after(Duration::from_millis(1000)).await;
        }

        // ---- SLEEP PREPARATION ---------------------------------------
        // Check if the system is ready to enter low-power mode
        if low_power_ready() && !is_sleeping {
            info!("Preparing I2C for sleep...");

            // Save hardware context before clock-gating
            ctx = unsafe { save_i2c_context(I2C1_BASE as *const u32) };

            // Bus recovery check: confirm both lines are idle
            if !scl.is_high() || !sda.is_high() {
                warn!("Bus not idle before sleep — attempting recovery");
                if !recover_i2c_bus(&mut scl, &mut sda).await {
                    error!("Bus recovery failed — cannot sleep");
                    continue;
                }
            }

            // Disable I2C controller peripheral enable (PE bit)
            let cr1 = unsafe { (I2C1_BASE as *const u32).read_volatile() };
            unsafe { (I2C1_BASE as *mut u32).write_volatile(cr1 & !1u32) };

            is_sleeping = true;
            info!("I2C suspended. Entering low-power state.");
        }

        // ---- WAKE RESTORATION ----------------------------------------
        if is_sleeping && !low_power_ready() {
            info!("Waking up — restoring I2C...");

            // Check for stuck bus before re-enabling hardware controller
            if !scl.is_high() || !sda.is_high() {
                warn!("Bus stuck after wake — running 9-clock recovery");
                if !recover_i2c_bus(&mut scl, &mut sda).await {
                    error!("Post-wake bus recovery failed");
                }
            }

            // Restore controller registers (includes PE bit)
            unsafe { restore_i2c_context(I2C1_BASE, &ctx) };

            // Small settling time before first transaction
            Timer::after(Duration::from_micros(500)).await;

            is_sleeping = false;
            info!("I2C resumed.");
        }

        // Yield to executor (enables WFI when all tasks idle)
        Timer::after(Duration::from_millis(10)).await;
    }
}
```

---

### Linux Userspace (std)

For Rust applications running on Linux (e.g., on a Raspberry Pi or BeagleBone), I2C is accessed via `/dev/i2c-N`. Sleep/wake coordination can use `epoll`/`signalfd` to listen for system suspend notifications from `logind` or `upower`.

```rust
// src/linux_i2c_sleep.rs
// Linux userspace I2C sleep handler in Rust (std)
//
// Cargo.toml dependencies:
//   i2cdev       = "0.6"
//   dbus         = "0.9"
//   nix          = { version = "0.28", features = ["signal"] }
//   log          = "0.4"
//   env_logger   = "0.11"

use i2cdev::core::I2CDevice;
use i2cdev::linux::LinuxI2CDevice;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

/// Shared I2C device state, protected by a Mutex.
struct I2CState {
    device:    Option<LinuxI2CDevice>,
    suspended: bool,
    /// Saved slave configuration (application-level context)
    saved_reg: u8,
}

impl I2CState {
    fn new(bus: u8, addr: u16) -> Self {
        let path = format!("/dev/i2c-{}", bus);
        let dev = LinuxI2CDevice::new(&path, addr)
            .expect("Failed to open I2C device");
        I2CState {
            device:    Some(dev),
            suspended: false,
            saved_reg: 0,
        }
    }

    /// Save slave configuration register before sleep.
    fn prepare_sleep(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        if self.suspended {
            return Ok(());
        }
        let dev = self.device.as_mut().ok_or("No I2C device")?;

        // Read current configuration from slave (register 0x00)
        self.saved_reg = dev.smbus_read_byte_data(0x00)?;

        // Assert shutdown bit in slave (bit 0 of config register)
        dev.smbus_write_byte_data(0x00, self.saved_reg | 0x01)?;

        // Give slave time to enter shutdown
        thread::sleep(Duration::from_millis(5));

        // Close the file descriptor — kernel I2C adapter will
        // complete its own suspend when the FD is released.
        self.device = None;
        self.suspended = true;
        log::info!("I2C suspended (saved config=0x{:02X})", self.saved_reg);
        Ok(())
    }

    /// Restore slave configuration register after wake.
    fn restore_after_wake(&mut self, bus: u8, addr: u16)
        -> Result<(), Box<dyn std::error::Error>>
    {
        if !self.suspended {
            return Ok(());
        }

        // Re-open the I2C bus (adapter has been re-initialised by kernel)
        let path = format!("/dev/i2c-{}", bus);

        // Retry loop: the I2C adapter may not be ready immediately after wake
        let dev = (0..10).find_map(|attempt| {
            match LinuxI2CDevice::new(&path, addr) {
                Ok(d) => Some(d),
                Err(e) => {
                    log::warn!("I2C open attempt {}: {}", attempt, e);
                    thread::sleep(Duration::from_millis(50));
                    None
                }
            }
        }).ok_or("Failed to re-open I2C device after wake")?;

        self.device = Some(dev);

        // Restore slave configuration (clears shutdown bit)
        self.device.as_mut().unwrap()
            .smbus_write_byte_data(0x00, self.saved_reg)?;

        // Give slave time to exit shutdown
        thread::sleep(Duration::from_millis(10));

        self.suspended = false;
        log::info!("I2C resumed (restored config=0x{:02X})", self.saved_reg);
        Ok(())
    }
}

/// Listen for system suspend/resume events via D-Bus (logind).
/// Requires the process to hold a "sleep inhibitor lock" to receive
/// PrepareForSleep signals before the system goes to sleep.
fn run_sleep_monitor(state: Arc<Mutex<I2CState>>, bus: u8, addr: u16) {
    use dbus::blocking::Connection;
    use std::time::Duration as StdDuration;

    let conn = Connection::new_system().expect("D-Bus connection failed");

    // Acquire a delay inhibitor: system will wait for us to release it
    // before completing the suspend.
    let inhibitor_proxy = conn.with_proxy(
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        StdDuration::from_secs(5),
    );

    use dbus::blocking::stdintf::org_freedesktop_dbus::Properties;
    // (Inhibitor lock acquisition omitted for brevity — see systemd docs)

    // Listen for PrepareForSleep(true/false) signal
    let state_clone = Arc::clone(&state);
    let _rule = conn.add_match(
        dbus::message::MatchRule::new_signal(
            "org.freedesktop.login1.Manager",
            "PrepareForSleep",
        ),
        move |sig: (bool,), _conn, _msg| {
            let going_to_sleep = sig.0;
            let mut s = state_clone.lock().unwrap();
            if going_to_sleep {
                if let Err(e) = s.prepare_sleep() {
                    log::error!("Sleep preparation failed: {}", e);
                }
            } else {
                if let Err(e) = s.restore_after_wake(bus, addr) {
                    log::error!("Wake restoration failed: {}", e);
                }
            }
            true
        },
    ).expect("D-Bus match failed");

    loop {
        conn.process(StdDuration::from_millis(1000))
            .expect("D-Bus process error");
    }
}

fn main() {
    env_logger::init();

    let bus  = 1u8;
    let addr = 0x48u16;

    let state = Arc::new(Mutex::new(I2CState::new(bus, addr)));

    // Spawn sleep monitor thread
    let state_clone = Arc::clone(&state);
    thread::spawn(move || run_sleep_monitor(state_clone, bus, addr));

    // Main loop: perform periodic I2C reads
    loop {
        {
            let mut s = state.lock().unwrap();
            if !s.suspended {
                if let Some(dev) = s.device.as_mut() {
                    match dev.smbus_read_word_data(0x01) {
                        Ok(raw) => {
                            let temp = (raw as i16) >> 4;
                            log::info!("Temperature: {:.1} °C", temp as f32 / 16.0);
                        }
                        Err(e) => log::warn!("I2C read error: {}", e),
                    }
                }
            }
        }
        thread::sleep(Duration::from_secs(1));
    }
}
```

---

## Platform-Specific Considerations

### STM32 (ARM Cortex-M)

- Use `HAL_I2C_Master_Abort_IT()` to cancel in-progress DMA transfers before sleep.
- `__HAL_RCC_I2Cx_FORCE_RESET()` / `RELEASE_RESET()` clears the I2C peripheral cleanly.
- The `TIMINGR` register must be re-written before enabling PE (CR1 bit 0).
- STM32L4/L5 supports `STOP2` mode; I2C wakeup from STOP1 can be configured via `WUPEN` bit.

### i.MX (NXP)

- The `i2c-imx` Linux driver implements `suspend_noirq` / `resume_noirq`.
- Pinmux must be restored via `pinctrl` before the first transaction.
- For i.MX8, the I2C controller may be in a different power domain than the CPU.

### Raspberry Pi (BCM2835/BCM2711)

- The BCM BSC (Broadband Serial Controller) does not save state across warm reboot; userspace processes must close and re-open `/dev/i2c-N` after resume.
- Kernel `drivers/i2c/busses/i2c-bcm2835.c` does not implement PM ops — the hardware is kept powered.

### RISC-V (ESP32-C3/C6, GD32VF103)

- ESP-IDF provides `i2c_driver_delete()` before `esp_light_sleep_start()` and `i2c_driver_install()` after wake.
- GD32VF103 requires re-configuring GPIO alternate functions after waking from standby.

---

## Common Pitfalls and How to Avoid Them

| Pitfall | Symptom | Fix |
|---|---|---|
| Sleep entered mid-transaction | SDA stuck low after wake | Always wait for `HAL_I2C_STATE_READY` before sleep |
| Controller registers not restored | First post-wake transaction times out | Save/restore CR1, CR2, TIMINGR, OAR1 |
| Pins not reconfigured to I2C mode | All transactions return EIO | Reconfigure GPIO alternate function in resume handler |
| Missing bus recovery | Slave frozen; reads return 0xFF | Implement 9-clock recovery in resume path |
| Pull-up power domain removed | Bus lines float; spurious STARTs | Keep pull-up supply or use internal pull-ups in sleep |
| Race: I2C used before adapter resumes | Kernel WARN, transaction timeout | Use `i2c_mark_adapter_suspended()` / `i2c_mark_adapter_resumed()` |
| Slave state machine reset unexpectedly | Reads return stale or wrong data | Re-program slave configuration registers on resume |
| DMA channel not re-configured | DMA transactions hang | Re-initialise DMA channel in resume |

---

## Testing Sleep/Wake I2C Behaviour

### Automated Testing Strategy

```bash
# Linux: stress-test suspend/resume cycle (requires root)
for i in $(seq 1 100); do
    echo "Suspend/resume cycle $i"
    systemctl suspend
    sleep 5
    # Verify I2C bus is functional after wake
    i2cdetect -y 1 | grep "48" || echo "FAIL: device 0x48 not found"
done
```

### Logic Analyser Checklist

1. **Pre-sleep**: Confirm final STOP condition; SDA=HIGH, SCL=HIGH.
2. **Sleep period**: Confirm no spurious START conditions on bus.
3. **Post-wake**: Confirm first START is clean; clock frequency is correct.
4. **Recovery path**: If fault injected (pull SDA low), confirm 9 clocks appear before STOP.

### Unit-Testable Rust Recovery Logic

```rust
#[cfg(test)]
mod tests {
    use super::*;

    /// Simulate a bus recovery scenario with a mock GPIO.
    #[test]
    fn test_recovery_releases_bus_after_5_clocks() {
        // MockFlex releases SDA after 5 SCL pulses
        let mut scl = MockFlex::new();
        let mut sda = MockFlex::stuck_low(5);  // releases on 5th pulse

        let result = futures::executor::block_on(
            recover_i2c_bus_mock(&mut scl, &mut sda)
        );
        assert!(result, "Bus should be free after recovery");
        assert_eq!(scl.pulse_count(), 5);
    }
}
```

---

## Summary

Managing I2C transactions across system sleep and wake transitions is a multi-layered problem that spans hardware, driver, and application concerns. The key takeaways are:

**Before Sleep:**
- Ensure all in-progress I2C transactions are completed or cleanly aborted.
- Verify the bus is in the idle state (both SDA and SCL held HIGH).
- Save the I2C controller's register context if the power domain will be removed.
- Optionally put slave devices into their own low-power/shutdown mode.

**During Sleep:**
- Ensure I2C pins do not inadvertently drive the bus. Configure as high-impedance inputs or ensure pull-ups remain powered.

**After Wake:**
- Perform bus recovery (9-clock sequence) if SDA is found low.
- Restore clock, GPIO pin alternate functions, and controller register context.
- Restore slave device configuration registers as needed.
- Gate new I2C transactions until the controller is confirmed ready.

**In Linux:** Use `dev_pm_ops` with `SET_SYSTEM_SLEEP_PM_OPS` for client drivers and `SET_NOIRQ_SYSTEM_SLEEP_PM_OPS` for adapter drivers. Use `i2c_mark_adapter_suspended()` and `i2c_mark_adapter_resumed()` to coordinate safely with the I2C core.

**In Rust (embedded):** The `embassy` async executor cooperates naturally with low-power sleep by calling `WFI` when all tasks are awaiting. Context save/restore is done via raw register access in `unsafe` blocks, with recovery logic testable in isolation.

**In Rust (Linux userspace):** Listen for `PrepareForSleep` D-Bus signals, close the I2C file descriptor before sleep, and retry `open()` with a back-off loop after wake.

Getting this right prevents the most common class of field failures in battery-powered and always-on embedded products: I2C lockups that require a hard power cycle to resolve.

---

*Document: 77_Sleep_Mode_Handling.md | I2C Programming Series*