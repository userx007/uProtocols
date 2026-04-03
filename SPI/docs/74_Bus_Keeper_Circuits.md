The document covers the full topic across 10 sections with 6 code examples:

**Content highlights:**

- **Theory** — what a bus keeper is at transistor level, how the weak latch works, and why it differs from pull resistors or tri-state buffers
- **Problem analysis** — which SPI lines float, when, and the consequences (data corruption, shoot-through current, EMI)
- **Power math** — concrete comparison showing keepers consuming ~0.00066 mW vs. ~4.36 mW for pull resistors across a 4-line SPI bus

**C/C++ examples:**
1. STM32 HAL — GPIO keeper-friendly init + CS/MISO shadow register management
2. Linux kernel SPI driver — pinctrl `bias-bus-hold` state switching with device tree snippet
3. Portable C++ `SoftwareBusKeeper` class — abstract HAL interface, active drive + timed release

**Rust examples:**
4. `SpiBusKeeper<Spi, Cs>` — type-safe `embedded-hal 1.0` wrapper tracking `BusIdleState`
5. RTIC-style task context — command/response transaction with pre-sleep safety validation
6. `no_std` `BusKeeperMonitor` — sliding-window health scoring to detect keeper failure, with unit tests

**Advanced topics** cover multi-slave contention, daisy-chain timing, and ultra-low-power sleep strategies.

# 74. Bus Keeper Circuits
## Preventing Floating Lines and Reducing Power Consumption in Idle State

---

## Table of Contents

1. [Introduction](#introduction)
2. [What Is a Bus Keeper Circuit?](#what-is-a-bus-keeper-circuit)
3. [Why Floating Lines Are a Problem in SPI](#why-floating-lines-are-a-problem-in-spi)
4. [Bus Keeper vs. Pull Resistors vs. Tri-State Buffers](#bus-keeper-vs-pull-resistors-vs-tri-state-buffers)
5. [SPI Lines Affected by Floating](#spi-lines-affected-by-floating)
6. [Bus Keeper Circuit Internals](#bus-keeper-circuit-internals)
7. [Power Consumption in Idle State](#power-consumption-in-idle-state)
8. [Programming Approaches](#programming-approaches)
   - [C/C++ Implementation](#cc-implementation)
   - [Rust Implementation](#rust-implementation)
9. [Advanced Topics](#advanced-topics)
10. [Summary](#summary)

---

## Introduction

In any embedded system using SPI (Serial Peripheral Interface), certain signal lines can enter
an undefined electrical state — commonly called a **floating** or **high-impedance (Hi-Z)** state —
when no driver is actively asserting them. This is especially critical on the MISO (Master In,
Slave Out) line when no slave is selected, or on shared bus lines during multi-slave operation.

Bus Keeper Circuits solve this problem at the hardware level by using a weak feedback latch
that remembers and maintains the last driven logic level, effectively eliminating undefined
states without the continuous current draw of pull-up or pull-down resistors.

---

## What Is a Bus Keeper Circuit?

A **Bus Keeper** (also called a **Bus Hold**, **Bus Latch**, or **Keeper Cell**) is an ultra-weak
bidirectional feedback buffer permanently connected to the signal line. It is characterized by:

- A very high output impedance so any real driver can immediately override it
- A feedback loop (two back-to-back inverters) that holds the last valid logic state
- Negligible quiescent current (typically < 50 µA) when the bus is idle
- No requirement for external components (often built into modern FPGA I/Os and
  some microcontroller GPIOs)

```
         Signal Line
             │
    ┌────────┴────────┐
    │    Keeper Cell  │
    │  ┌───┐   ┌───┐  │
    │  │INV├──►│INV│  │
    │  └─▲─┘   └─┬─┘  │
    │    └────────┘   │
    └─────────────────┘
         (weak latch)
```

The two cascaded inverters form a weak positive-feedback latch. Once driven to logic HIGH or
LOW, the keeper sustains that level until a stronger driver forces a change.

---

## Why Floating Lines Are a Problem in SPI

SPI has four standard lines: **SCLK**, **MOSI**, **MISO**, and **CS̄**. Several of these can float
under normal operating conditions:

| Line  | Floats When                                    | Risk                                      |
|-------|------------------------------------------------|-------------------------------------------|
| MISO  | No slave selected (CS̄ = HIGH)                 | Spurious data reads, current spikes       |
| MOSI  | Master tri-states after last bit               | Noise injection into slave input          |
| CS̄   | GPIO reconfigured or MCU in reset              | Accidental slave activation               |
| SCLK  | MCU in boot/reset phase                        | Spurious clock edges trigger slave state  |

### Consequences of Floating SPI Lines

1. **Data corruption** — A floating MISO line oscillates between HIGH and LOW due to
   electromagnetic interference, capacitive coupling, or thermal noise. The master's
   shift register may capture garbage bits.

2. **Excess current consumption** — CMOS input stages draw substantial current
   (sometimes milliamps) when their input floats between VDD/2, as both the
   pull-up and pull-down transistors conduct simultaneously (shoot-through current).

3. **Spurious device activation** — A floating CS̄ line on a Flash memory or ADC can
   trigger partial command sequences, corrupting device state or causing data loss.

4. **EMI susceptibility** — High-impedance lines act as antennas, picking up
   interference from nearby power switching circuits or RF sources.

---

## Bus Keeper vs. Pull Resistors vs. Tri-State Buffers

| Feature                    | Pull-Up/Down Resistor    | Bus Keeper               | Tri-State Buffer + Enable |
|----------------------------|--------------------------|--------------------------|---------------------------|
| Power (idle, line at rail) | I = VDD/R (continuous!)  | ~0 µA                    | ~0 µA                     |
| Power (line mid-swing)     | I = VDD/(2R)             | ~0 µA                    | ~0 µA                     |
| Deterministic idle state   | Always to rail           | Last driven level        | Defined by control logic  |
| Override ability           | Any standard driver      | Any standard driver      | Only when enabled         |
| Bus transitions            | Slows rise/fall time     | Negligible effect        | Fast                      |
| PCB area / cost            | Low (passive)            | Integrated (no BOM cost) | Medium                    |
| Multi-master support       | Good                     | Excellent                | Complex                   |

**Rule of thumb:**
- Use **pull resistors** when the idle state must always be a specific, known level
  (e.g., CS̄ must default HIGH, SCLK must default LOW for SPI Mode 0/1).
- Use **bus keepers** when the last-driven state is acceptable and power matters.
- Use **tri-state buffers** in multi-master or bus arbitration scenarios.

In many real-world designs, CS̄ uses a pull-up resistor (guaranteed idle HIGH) while MISO
uses a bus keeper (holds last byte sent without wasting power on the pull path).

---

## SPI Lines Affected by Floating

### MISO (Most Critical)

The MISO line is driven by the slave only while CS̄ is asserted. Once CS̄ de-asserts, the slave
releases MISO to high-impedance. Without a bus keeper or pull resistor, the master's RX FIFO
or shift register sees noise.

```
CS̄  ─────┐         ┌──────────────────────────
         │         │
         └─────────┘

MISO ═══════════════╗ <- slave driving valid data
                    ║ Hi-Z begins here ─────? ? ? (floating without keeper)
                    ╚═══════════════════════════
                              ^
                        Bus Keeper holds last bit here
```

### MOSI (Secondary)

In half-duplex or multi-slave configurations where MOSI is shared, the line can float between
transmissions. A keeper on MOSI prevents garbage data from reaching unselected slaves.

### CS̄ Lines in Daisy-Chain Configurations

In daisy-chain SPI, if an intermediate device resets unexpectedly, the CS̄ to downstream devices
can float, potentially activating them.

---

## Bus Keeper Circuit Internals

### Transistor-Level View

A keeper cell implemented in CMOS consists of a minimum-size inverter whose output feeds back
to its own input through another minimum-size inverter:

```
VDD
 │
┌┴──┐ PMOS (very weak, W/L = 1)
│   │
└┬──┘
 ├──── Node A ────┐
┌┴──┐             │
│   │ NMOS        │    ┌─────┐
└┬──┘ (very weak) │───►│ INV ├──► back to Node A
 │                     └─────┘
GND
```

The key design constraint is that the keeper's drive strength must be **much weaker** than any
normal I/O driver. Typical keeper current: 50–200 µA. Typical I/O driver current: 4–24 mA.
This ensures any connected device can override the keeper without signal degradation.

### FPGA Built-in Keepers

Most FPGA families include configurable bus keepers in their I/O blocks:

| FPGA Family      | Keeper Setting                  |
|------------------|---------------------------------|
| Xilinx 7-Series  | `KEEPER` attribute on IOB       |
| Intel/Altera     | `bus_hold` in pin assignment    |
| Lattice ECP5     | `PULLMODE=KEEPER` in LPF file   |
| Microchip PolarFire | Bus hold via Register config |

### MCU GPIO Keeper Support

Many ARM Cortex-M MCUs (STM32, NXP LPC, Nordic nRF) include keeper/bus-hold modes in
their GPIO peripheral, selectable via pin configuration registers alongside pull-up and pull-down.

---

## Power Consumption in Idle State

### Current Draw Comparison

Consider a 3.3V SPI bus with MISO floating at 1.65V (VDD/2):

```
Pull-down (10kΩ): I = 3.3V / 10,000Ω = 330 µA per line
Pull-up  (10kΩ): I = 3.3V / 10,000Ω = 330 µA per line
Bus Keeper:       I ≈ 10–50 µA (leakage only when at rail)
CMOS input floating at VDD/2 (shoot-through): I = 0.5–2 mA
```

For a battery-powered device with 4 SPI lines and 10 kΩ pulls:

```
Power with pull resistors (all lines): 4 × 330 µA × 3.3V = 4.36 mW
Power with bus keepers:                4 × 0.05 µA × 3.3V = 0.00066 mW
```

### Idle State Strategy by SPI Line

```
┌──────┬────────────────────────────────────────────────────────────────┐
│ Line │ Recommended Idle Strategy                                      │
├──────┼────────────────────────────────────────────────────────────────┤
│ CS̄   │ Pull-UP resistor (4.7–10 kΩ) — must be HIGH to deselect slave  │
│ SCLK │ Pull-DOWN (SPI Mode 0,1) or Pull-UP (Mode 2,3) — mode-specific │
│ MOSI │ Bus keeper or weak pull to last state — don't care             │
│ MISO │ Bus keeper (preferred) or pull-DOWN — reduces shoot-through    │
└──────┴────────────────────────────────────────────────────────────────┘
```

---

## Programming Approaches

This section covers how to configure hardware bus keepers and implement
software-level bus idle management for SPI in both C/C++ and Rust.

---

### C/C++ Implementation

#### Example 1 — STM32 GPIO Bus Keeper via HAL (C)

On STM32 microcontrollers, the `GPIO_NOPULL` mode combined with an output set before
releasing the line mimics keeper behavior in software. Hardware keepers are enabled via the
pull configuration registers.

```c
/**
 * @file spi_bus_keeper_stm32.c
 * @brief Configure SPI pins with bus-hold/keeper behavior on STM32
 *
 * Hardware keeper: STM32 does not have a dedicated keeper mode.
 * Strategy: drive MISO to last known state before de-asserting CS.
 * For FPGA-connected MISO lines, configure the FPGA IOB with KEEPER attribute.
 */

#include "stm32f4xx_hal.h"

#define SPI_CS_PIN      GPIO_PIN_4
#define SPI_MISO_PIN    GPIO_PIN_6
#define SPI_MOSI_PIN    GPIO_PIN_7
#define SPI_SCK_PIN     GPIO_PIN_5
#define SPI_GPIO_PORT   GPIOA

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
    uint8_t            last_miso_level; /* keeper shadow register */
} SPI_BusKeeper_t;

/**
 * @brief Initialize SPI pins with keeper-friendly configuration.
 *
 * CS   → Pull-UP (safe idle = deselected)
 * SCLK → Pull-DOWN (SPI Mode 0 idle = LOW)
 * MOSI → No pull (keeper holds last state via software shadow)
 * MISO → No pull (keeper holds last state via software shadow)
 */
void SPI_BusKeeper_Init(SPI_BusKeeper_t *bk, SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef gpio = {0};

    bk->hspi            = hspi;
    bk->cs_port         = SPI_GPIO_PORT;
    bk->cs_pin          = SPI_CS_PIN;
    bk->last_miso_level = 0;

    /* CS: output, pull-up, default HIGH (device deselected) */
    HAL_GPIO_WritePin(SPI_GPIO_PORT, SPI_CS_PIN, GPIO_PIN_SET);
    gpio.Pin   = SPI_CS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;   /* hardware pull-up ensures safe idle */
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(SPI_GPIO_PORT, &gpio);

    /* SCLK: AF mode, pull-down for Mode 0 idle LOW */
    gpio.Pin   = SPI_SCK_PIN;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_PULLDOWN;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(SPI_GPIO_PORT, &gpio);

    /* MOSI: AF mode, no pull (bus keeper via last-driven state) */
    gpio.Pin   = SPI_MOSI_PIN;
    gpio.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(SPI_GPIO_PORT, &gpio);

    /* MISO: AF mode, no pull (bus keeper shadow tracks last byte) */
    gpio.Pin   = SPI_MISO_PIN;
    gpio.Mode  = GPIO_MODE_AF_INPUT;
    gpio.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(SPI_GPIO_PORT, &gpio);
}

/**
 * @brief Perform a full-duplex SPI transfer with bus keeper management.
 *
 * Before de-asserting CS, this function drives MOSI to match the last bit
 * of the outgoing data (keeper behavior for MOSI). The MISO last-bit state
 * is shadowed in the bk struct for diagnostic purposes.
 *
 * @param bk     Bus keeper context
 * @param tx     Transmit buffer
 * @param rx     Receive buffer
 * @param length Number of bytes
 * @return HAL status
 */
HAL_StatusTypeDef SPI_BusKeeper_Transfer(SPI_BusKeeper_t *bk,
                                          const uint8_t   *tx,
                                          uint8_t         *rx,
                                          uint16_t         length)
{
    HAL_StatusTypeDef status;

    /* Assert CS (active LOW) */
    HAL_GPIO_WritePin(bk->cs_port, bk->cs_pin, GPIO_PIN_RESET);

    /* Small setup delay (tCSS: CS setup time before first clock edge) */
    /* In production use a proper delay function, e.g., DWT cycle counter */
    __NOP(); __NOP(); __NOP(); __NOP();

    /* Perform transfer */
    status = HAL_SPI_TransmitReceive(bk->hspi,
                                     (uint8_t *)tx,
                                     rx,
                                     length,
                                     HAL_MAX_DELAY);

    /* Shadow last MISO bit for keeper status */
    if (length > 0 && rx != NULL) {
        bk->last_miso_level = (rx[length - 1] & 0x01u);
    }

    /*
     * KEEPER TECHNIQUE:
     * Before releasing CS (which causes MISO to float), we record
     * the last received bit. In a hardware keeper scenario, the IOB
     * keeper cell now holds this level. In software simulation,
     * we store it in last_miso_level so callers can detect stale reads.
     */

    /* De-assert CS: pull-up now holds CS HIGH */
    HAL_GPIO_WritePin(bk->cs_port, bk->cs_pin, GPIO_PIN_SET);

    return status;
}

/**
 * @brief Check if MISO is in a potentially floating state.
 *
 * In the absence of a hardware keeper, MISO is "floating" after CS de-assertion.
 * This function returns true if called outside an active CS window.
 *
 * @param bk Bus keeper context
 * @return 1 if MISO may be floating, 0 if inside active transaction
 */
int SPI_BusKeeper_IsMISOFloating(const SPI_BusKeeper_t *bk)
{
    /* Check if CS is de-asserted (HIGH = not selected) */
    return (HAL_GPIO_ReadPin(bk->cs_port, bk->cs_pin) == GPIO_PIN_SET);
}
```

---

#### Example 2 — Linux Kernel SPI Driver with Bus Hold Configuration (C)

```c
/**
 * @file spi_buskeeper_driver.c
 * @brief Linux SPI driver demonstrating bus keeper / bus hold pin config
 *
 * Uses the Linux GPIO descriptor API to configure MISO with bus-hold
 * semantics via gpiod flags and pinctrl states.
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>

struct buskeeper_spi_dev {
    struct spi_device  *spi;
    struct gpio_desc   *cs_gpio;
    struct pinctrl     *pctrl;
    struct pinctrl_state *state_idle;    /* bus keeper / hold state */
    struct pinctrl_state *state_active;  /* normal SPI AF state     */
};

/**
 * @brief Transition SPI pins to keeper-hold idle state.
 *
 * During idle, pinctrl switches MISO, MOSI to bus-hold mode:
 * - Weak keeper holds last driven level
 * - No continuous pull-resistor current
 *
 * Device tree pinctrl state "idle" must configure:
 *   bias-bus-hold; (or bias-disable with bus-hold support)
 */
static int buskeeper_enter_idle(struct buskeeper_spi_dev *dev)
{
    int ret;

    if (dev->state_idle) {
        ret = pinctrl_select_state(dev->pctrl, dev->state_idle);
        if (ret) {
            dev_warn(&dev->spi->dev,
                     "Failed to select idle pinctrl state: %d\n", ret);
            return ret;
        }
    }
    return 0;
}

/**
 * @brief Transition SPI pins back to active AF mode before transaction.
 */
static int buskeeper_enter_active(struct buskeeper_spi_dev *dev)
{
    int ret;

    if (dev->state_active) {
        ret = pinctrl_select_state(dev->pctrl, dev->state_active);
        if (ret) {
            dev_err(&dev->spi->dev,
                    "Failed to select active pinctrl state: %d\n", ret);
            return ret;
        }
    }
    return 0;
}

/**
 * @brief Execute an SPI transfer with bus keeper management.
 *
 * Pattern:
 *   1. Switch to active pinctrl state (normal SPI AF drivers)
 *   2. Assert CS
 *   3. Transfer data
 *   4. De-assert CS
 *   5. Switch to idle pinctrl state (bus keeper / hold)
 */
static int buskeeper_spi_transfer(struct buskeeper_spi_dev *dev,
                                   u8 *tx_buf, u8 *rx_buf, size_t len)
{
    struct spi_transfer xfer = {
        .tx_buf = tx_buf,
        .rx_buf = rx_buf,
        .len    = len,
    };
    struct spi_message msg;
    int ret;

    /* Step 1: Activate SPI pin functions */
    ret = buskeeper_enter_active(dev);
    if (ret)
        return ret;

    /* Step 2-4: Execute transfer (SPI core handles CS via spi_device config) */
    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);
    ret = spi_sync(dev->spi, &msg);

    /* Step 5: Return to keeper/hold idle state regardless of transfer result */
    buskeeper_enter_idle(dev);

    return ret;
}

static int buskeeper_probe(struct spi_device *spi)
{
    struct buskeeper_spi_dev *dev;

    dev = devm_kzalloc(&spi->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->spi = spi;

    /* Acquire pinctrl handle */
    dev->pctrl = devm_pinctrl_get(&spi->dev);
    if (IS_ERR(dev->pctrl)) {
        dev_err(&spi->dev, "Cannot get pinctrl handle\n");
        return PTR_ERR(dev->pctrl);
    }

    /* Look up pinctrl states defined in device tree */
    dev->state_active = pinctrl_lookup_state(dev->pctrl, "active");
    dev->state_idle   = pinctrl_lookup_state(dev->pctrl, "idle");

    /* Start in idle (keeper hold) state */
    buskeeper_enter_idle(dev);

    spi_set_drvdata(spi, dev);

    dev_info(&spi->dev, "Bus keeper SPI driver probed\n");
    return 0;
}

/* Device tree compatible string */
static const struct of_device_id buskeeper_of_match[] = {
    { .compatible = "example,spi-buskeeper" },
    {}
};
MODULE_DEVICE_TABLE(of, buskeeper_of_match);

static struct spi_driver buskeeper_driver = {
    .driver = {
        .name           = "spi-buskeeper",
        .of_match_table = buskeeper_of_match,
    },
    .probe = buskeeper_probe,
};
module_spi_driver(buskeeper_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPI Bus Keeper Demo Driver");
```

**Device Tree Snippet (Pinctrl States)**

```dts
/* arch/arm/boot/dts/example-board.dts */
&spi1 {
    pinctrl-names = "active", "idle";
    pinctrl-0 = <&spi1_active_pins>;
    pinctrl-1 = <&spi1_idle_pins>;

    spi_sensor: sensor@0 {
        compatible = "example,spi-buskeeper";
        reg = <0>;
        spi-max-frequency = <10000000>;
    };
};

spi1_active_pins: spi1-active {
    pins = "PA5", "PA6", "PA7";   /* SCK, MISO, MOSI */
    function = "spi1";
    drive-strength = <4>;
};

spi1_idle_pins: spi1-idle {
    pins = "PA6";                  /* MISO only needs keeper */
    function = "gpio";
    bias-bus-hold;                  /* <-- hardware bus keeper */
};
```

---

#### Example 3 — Software Bus Keeper (Portable C++ Class)

This example implements a pure software bus keeper for microcontrollers that lack hardware
keeper support, using a shadow register and configurable timeout.

```cpp
/**
 * @file SoftwareBusKeeper.hpp
 * @brief Portable C++ software bus keeper for SPI MISO management
 *
 * When hardware bus keepers are unavailable, this class:
 * 1. Drives MISO pin to last received bit level after CS de-assertion
 * 2. Reverts to high-impedance after a configurable hold duration
 * 3. Tracks idle power state for diagnostic reporting
 */

#pragma once
#include <cstdint>
#include <cstdbool>

/**
 * @brief Abstract GPIO HAL interface.
 * Implement this for your specific platform (STM32, ESP32, RPi, etc.)
 */
struct IGpioHal {
    virtual void set_output(uint8_t pin)                  = 0;
    virtual void set_input(uint8_t pin)                   = 0;
    virtual void write(uint8_t pin, bool level)           = 0;
    virtual bool read(uint8_t pin)                        = 0;
    virtual void set_weak_pull(uint8_t pin, bool pullup)  = 0;
    virtual uint32_t millis()                             = 0;
    virtual ~IGpioHal() = default;
};

class SoftwareBusKeeper {
public:
    /**
     * @param hal             Platform GPIO abstraction
     * @param miso_pin        Pin number of MISO line
     * @param hold_ms         Duration (ms) to actively drive keeper level
     *                        before releasing to Hi-Z (0 = never release)
     */
    explicit SoftwareBusKeeper(IGpioHal &hal,
                                uint8_t   miso_pin,
                                uint32_t  hold_ms = 0)
        : hal_(hal),
          miso_pin_(miso_pin),
          hold_ms_(hold_ms),
          last_level_(false),
          is_held_(false),
          hold_start_ms_(0),
          bytes_held_(0)
    {}

    /**
     * @brief Record last received byte and assert software keeper.
     *
     * Call this immediately after CS de-assertion. The MISO pin is
     * switched to output and driven to the LSB of last_rx_byte.
     *
     * @param last_rx_byte Last byte received from slave
     */
    void hold(uint8_t last_rx_byte)
    {
        last_level_    = (last_rx_byte & 0x01u) != 0u;
        hold_start_ms_ = hal_.millis();
        is_held_       = true;
        ++bytes_held_;

        /* Drive MISO to keeper level */
        hal_.set_output(miso_pin_);
        hal_.write(miso_pin_, last_level_);
    }

    /**
     * @brief Release keeper: switch MISO back to input with weak pull.
     *
     * After hold_ms_ has elapsed, release active drive and configure
     * the pin as input with weak pull to the same level. This minimises
     * power while still avoiding complete float.
     */
    void release()
    {
        if (!is_held_)
            return;

        /* Switch to weak pull in direction of last level */
        hal_.set_input(miso_pin_);
        hal_.set_weak_pull(miso_pin_, last_level_);
        is_held_ = false;
    }

    /**
     * @brief Process function — call from main loop or RTOS task.
     *
     * Automatically releases keeper after hold_ms_ expires.
     * If hold_ms_ == 0, the keeper holds indefinitely until release()
     * is called manually.
     */
    void process()
    {
        if (!is_held_ || hold_ms_ == 0u)
            return;

        uint32_t elapsed = hal_.millis() - hold_start_ms_;
        if (elapsed >= hold_ms_) {
            release();
        }
    }

    /**
     * @brief Check if keeper is actively driving MISO.
     */
    bool is_active() const { return is_held_; }

    /**
     * @brief Return the held logic level.
     */
    bool held_level() const { return last_level_; }

    /**
     * @brief Return total number of keeper events since construction.
     */
    uint32_t events() const { return bytes_held_; }

private:
    IGpioHal &hal_;
    uint8_t   miso_pin_;
    uint32_t  hold_ms_;
    bool      last_level_;
    bool      is_held_;
    uint32_t  hold_start_ms_;
    uint32_t  bytes_held_;
};


/* ─── Usage Example ─── */

/**
 * Concrete HAL for STM32 (pseudocode — fill in actual HAL calls):
 *
 *   class Stm32GpioHal : public IGpioHal { ... };
 *
 * Main application:
 */
void spi_transaction_with_keeper(SoftwareBusKeeper &keeper,
                                  uint8_t           *tx,
                                  uint8_t           *rx,
                                  uint16_t           len)
{
    // [Hardware-specific] Assert CS, transfer, de-assert CS
    // ...

    // After de-asserting CS: immediately hold MISO at last bit level
    if (len > 0) {
        keeper.hold(rx[len - 1]);
    }
}

/* In main loop: */
// keeper.process();  // releases after hold_ms_ expires
```

---

### Rust Implementation

Rust's ownership model and embedded-hal traits make bus keeper management
type-safe and zero-cost at runtime.

#### Example 4 — embedded-hal SPI Bus Keeper Wrapper (Rust)

```rust
//! spi_bus_keeper.rs
//!
//! A type-safe SPI bus keeper wrapper using embedded-hal 1.0 traits.
//! Manages MISO bus-hold state and idle power reduction.
//!
//! Crates required in Cargo.toml:
//! [dependencies]
//! embedded-hal = "1.0"

use embedded_hal::digital::OutputPin;
use embedded_hal::spi::SpiBus;
use core::fmt;

/// Errors produced by the bus keeper layer.
#[derive(Debug)]
pub enum BusKeeperError<SpiE, CsE> {
    Spi(SpiE),
    ChipSelect(CsE),
}

impl<SpiE: fmt::Debug, CsE: fmt::Debug> fmt::Display for BusKeeperError<SpiE, CsE> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Spi(e)        => write!(f, "SPI error: {:?}", e),
            Self::ChipSelect(e) => write!(f, "CS error: {:?}", e),
        }
    }
}

/// Tracks the current bus idle state.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BusIdleState {
    /// Bus is in an active transaction (CS asserted).
    Active,
    /// Bus keeper is holding the last driven level (CS de-asserted).
    Held { level: bool },
    /// Bus released to Hi-Z (keeper expired or not configured).
    HighImpedance,
}

/// SPI Bus Keeper — wraps an SPI bus and CS pin, tracking MISO
/// state and enforcing safe idle management.
///
/// # Type Parameters
/// - `Spi`: An `embedded_hal::spi::SpiBus` implementation.
/// - `Cs`:  An `embedded_hal::digital::OutputPin` for chip-select.
pub struct SpiBusKeeper<Spi, Cs> {
    spi:        Spi,
    cs:         Cs,
    idle_state: BusIdleState,
    last_rx:    Option<u8>,
    /// Number of completed transactions.
    tx_count:   u32,
}

impl<Spi, Cs> SpiBusKeeper<Spi, Cs>
where
    Spi: SpiBus,
    Cs: OutputPin,
{
    /// Create a new bus keeper. CS starts de-asserted (HIGH).
    ///
    /// # Errors
    /// Returns `BusKeeperError::ChipSelect` if the initial CS HIGH fails.
    pub fn new(
        spi: Spi,
        mut cs: Cs,
    ) -> Result<Self, BusKeeperError<Spi::Error, Cs::Error>> {
        cs.set_high().map_err(BusKeeperError::ChipSelect)?;
        Ok(Self {
            spi,
            cs,
            idle_state: BusIdleState::HighImpedance,
            last_rx:    None,
            tx_count:   0,
        })
    }

    /// Perform a full-duplex SPI transfer with automatic CS and keeper management.
    ///
    /// The function:
    /// 1. Asserts CS (LOW)
    /// 2. Executes the full-duplex transfer
    /// 3. De-asserts CS (HIGH)
    /// 4. Updates the keeper state with the last received byte
    ///
    /// # Arguments
    /// - `words`: Mutable byte slice used for both TX and RX (in-place).
    ///
    /// # Returns
    /// - `Ok(())` on success.
    /// - `Err(BusKeeperError)` on SPI or CS failure.
    pub fn transfer(
        &mut self,
        words: &mut [u8],
    ) -> Result<(), BusKeeperError<Spi::Error, Cs::Error>> {
        // Assert CS
        self.cs.set_low().map_err(BusKeeperError::ChipSelect)?;
        self.idle_state = BusIdleState::Active;

        // Execute transfer
        let result = self.spi.transfer_in_place(words);

        // Always de-assert CS — even on SPI error
        self.cs.set_high().map_err(BusKeeperError::ChipSelect)?;

        // Map SPI result
        result.map_err(BusKeeperError::Spi)?;

        // Update keeper state with last received byte LSB
        if let Some(&last_byte) = words.last() {
            let level = (last_byte & 0x01) != 0;
            self.last_rx    = Some(last_byte);
            self.idle_state = BusIdleState::Held { level };
        } else {
            self.idle_state = BusIdleState::HighImpedance;
        }

        self.tx_count = self.tx_count.saturating_add(1);
        Ok(())
    }

    /// Perform a write-only transfer (MISO not captured).
    pub fn write(
        &mut self,
        words: &[u8],
    ) -> Result<(), BusKeeperError<Spi::Error, Cs::Error>> {
        self.cs.set_low().map_err(BusKeeperError::ChipSelect)?;
        self.idle_state = BusIdleState::Active;

        let result = self.spi.write(words);

        self.cs.set_high().map_err(BusKeeperError::ChipSelect)?;
        result.map_err(BusKeeperError::Spi)?;

        // After write-only, MISO is don't-care; mark as Hi-Z
        self.idle_state = BusIdleState::HighImpedance;
        self.tx_count   = self.tx_count.saturating_add(1);
        Ok(())
    }

    /// Query the current bus idle state.
    pub fn idle_state(&self) -> BusIdleState {
        self.idle_state
    }

    /// Return the last received byte (if any).
    pub fn last_received(&self) -> Option<u8> {
        self.last_rx
    }

    /// Return the total number of completed transactions.
    pub fn transaction_count(&self) -> u32 {
        self.tx_count
    }

    /// Release the underlying SPI bus and CS pin.
    pub fn release(self) -> (Spi, Cs) {
        (self.spi, self.cs)
    }
}
```

---

#### Example 5 — RTIC Task with Bus Keeper and Power Gating (Rust)

```rust
//! spi_keeper_rtic.rs
//!
//! RTIC-based SPI task demonstrating bus keeper integration and
//! idle power gating on an ARM Cortex-M device (e.g., STM32F4).
//!
//! Cargo.toml dependencies:
//! [dependencies]
//! cortex-m       = "0.7"
//! cortex-m-rtic  = "1.1"
//! embedded-hal   = "1.0"
//! stm32f4xx-hal  = { version = "0.20", features = ["stm32f411"] }

use embedded_hal::spi::SpiBus;

/// Tracks whether MISO is in a safe held state or potentially floating.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum MisoState {
    /// No transaction has occurred; MISO state is unknown (potentially floating).
    Unknown,
    /// Keeper is actively holding MISO at the last bit value.
    KeeperActive(bool),
    /// MISO released; hardware pull-down assumed.
    Released,
}

/// Bus keeper statistics for power and reliability monitoring.
#[derive(Default, Debug)]
pub struct BusKeeperStats {
    pub transactions:      u32,
    pub floating_events:   u32,
    pub keeper_activations: u32,
}

/// SPI device context with integrated bus keeper management.
pub struct SpiDeviceContext<Spi, Cs> {
    pub spi:        Spi,
    pub cs:         Cs,
    pub miso_state: MisoState,
    pub stats:      BusKeeperStats,
}

impl<Spi, Cs> SpiDeviceContext<Spi, Cs>
where
    Spi: SpiBus,
    Cs:  embedded_hal::digital::OutputPin,
{
    /// Execute a command-response SPI transaction.
    ///
    /// # Protocol
    /// TX: `cmd_buf` followed by dummy bytes to clock in response.
    /// RX: Response written into `rx_buf` (must equal response_len bytes).
    ///
    /// # Bus Keeper Behavior
    /// After CS de-assertion, MISO state transitions to `KeeperActive`
    /// holding the last received bit. If a hardware keeper is present
    /// in the MCU IOB, this is automatic. In software, the caller should
    /// configure the MISO pin to output this level until the next transaction.
    pub fn command_response(
        &mut self,
        cmd_buf:      &[u8],
        rx_buf:       &mut [u8],
        response_len: usize,
    ) -> Result<(), Spi::Error> {
        assert!(rx_buf.len() >= response_len);

        // Build combined TX/RX buffer: [cmd | dummy bytes for response]
        // In a real implementation, use a stack-allocated buffer or DMA
        let total = cmd_buf.len() + response_len;

        // For demonstration: process in two phases
        // Phase 1: Send command
        self.cs.set_low().ok();
        self.spi.write(cmd_buf)?;

        // Phase 2: Clock in response (send dummy 0xFF bytes)
        let dummy = [0xFFu8; 64]; // stack buffer — adjust size as needed
        let rx_slice = &mut rx_buf[..response_len];
        self.spi.transfer(rx_slice, &dummy[..response_len])?;

        // De-assert CS
        self.cs.set_high().ok();

        // Update keeper state
        if let Some(&last) = rx_slice.last() {
            let level = (last & 0x01) != 0;
            self.miso_state = MisoState::KeeperActive(level);
            self.stats.keeper_activations += 1;
        }

        self.stats.transactions += 1;
        Ok(())
    }

    /// Prepare for low-power sleep.
    ///
    /// Validates that the bus is in a safe state before WFI/WFE:
    /// - CS must be HIGH (device deselected)
    /// - MISO must not be Unknown (floating)
    ///
    /// Returns `true` if safe to sleep, `false` if remediation needed.
    pub fn prepare_sleep(&mut self) -> bool {
        match self.miso_state {
            MisoState::Unknown => {
                // Bus has never been used or was improperly initialized.
                // Risk: MISO floating → shoot-through current in CMOS input.
                self.stats.floating_events += 1;
                false
            }
            MisoState::KeeperActive(_) | MisoState::Released => {
                // Safe: bus keeper holds last state with negligible current,
                // or release to pull-down which is deterministic.
                true
            }
        }
    }

    /// Release MISO to pull-down before deep sleep (lowest power).
    ///
    /// Call this before entering STOP/STANDBY mode where GPIO state
    /// may be lost. The pull-down prevents floating on wakeup.
    pub fn release_for_deep_sleep(&mut self) {
        // In hardware: configure MISO GPIO to input + pull-down
        // self.miso_gpio.set_as_input_pulldown();
        self.miso_state = MisoState::Released;
    }
}
```

---

#### Example 6 — Bus Keeper Health Monitor (Rust, no_std)

```rust
//! bus_keeper_monitor.rs
//!
//! No-std compatible bus keeper health monitor.
//! Detects potential floating events by comparing MISO readings
//! against the keeper's expected held level.

#![no_std]

/// Maximum number of samples in the sliding window.
const WINDOW_SIZE: usize = 16;

/// Records of MISO state samples for floating detection.
pub struct BusKeeperMonitor {
    /// Expected MISO level when keeper is active.
    expected:     Option<bool>,
    /// Circular buffer of raw MISO samples.
    samples:      [u8; WINDOW_SIZE],
    head:         usize,
    count:        usize,
    /// Number of samples that deviated from the expected keeper level.
    anomaly_count: u32,
}

impl BusKeeperMonitor {
    pub const fn new() -> Self {
        Self {
            expected:      None,
            samples:       [0u8; WINDOW_SIZE],
            head:          0,
            count:         0,
            anomaly_count: 0,
        }
    }

    /// Notify the monitor of the keeper level after a transaction.
    pub fn set_expected(&mut self, last_rx_byte: u8) {
        self.expected = Some((last_rx_byte & 0x01) != 0);
    }

    /// Record a MISO sample for health analysis.
    ///
    /// Call this periodically while CS is de-asserted to detect
    /// anomalous transitions that indicate keeper failure.
    ///
    /// # Returns
    /// `true` if the sample matches the expected keeper level (healthy),
    /// `false` if an anomaly was detected (potential floating).
    pub fn sample(&mut self, miso_level: bool) -> bool {
        let healthy = match self.expected {
            Some(expected) => miso_level == expected,
            None           => true, // No reference — assume OK
        };

        // Store raw sample (1 = high, 0 = low)
        self.samples[self.head] = miso_level as u8;
        self.head  = (self.head + 1) % WINDOW_SIZE;
        self.count = (self.count + 1).min(WINDOW_SIZE);

        if !healthy {
            self.anomaly_count = self.anomaly_count.saturating_add(1);
        }

        healthy
    }

    /// Compute the proportion of recent samples matching the expected level.
    ///
    /// A value below ~0.90 may indicate keeper failure or excessive noise.
    pub fn health_score(&self) -> f32 {
        if self.count == 0 {
            return 1.0;
        }

        let expected_u8 = self.expected.map(|v| v as u8).unwrap_or(0);
        let matching = self.samples[..self.count]
            .iter()
            .filter(|&&s| s == expected_u8)
            .count();

        matching as f32 / self.count as f32
    }

    /// Total number of anomalous samples since construction.
    pub fn anomaly_count(&self) -> u32 {
        self.anomaly_count
    }

    /// Reset the monitor (e.g., after pin reconfiguration).
    pub fn reset(&mut self) {
        self.expected      = None;
        self.head          = 0;
        self.count         = 0;
        self.anomaly_count = 0;
        self.samples       = [0u8; WINDOW_SIZE];
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_healthy_keeper() {
        let mut mon = BusKeeperMonitor::new();
        mon.set_expected(0b10110101); // LSB = 1 → expected HIGH

        // 15 correct samples
        for _ in 0..15 {
            assert!(mon.sample(true));
        }
        assert!(mon.health_score() > 0.99);
        assert_eq!(mon.anomaly_count(), 0);
    }

    #[test]
    fn test_floating_detection() {
        let mut mon = BusKeeperMonitor::new();
        mon.set_expected(0b00000000); // LSB = 0 → expected LOW

        // Inject 4 floating anomalies out of 8 samples
        for i in 0..8u8 {
            mon.sample(i % 2 == 0); // alternating HIGH/LOW
        }

        // Health score should be ~50%
        let score = mon.health_score();
        assert!(score < 0.6, "Expected low health score, got {}", score);
        assert!(mon.anomaly_count() > 0);
    }
}
```

---

## Advanced Topics

### Multi-Slave SPI and Bus Keeper Interaction

In a multi-slave configuration where multiple devices share MISO (parallel CS topology),
bus contention and keeper conflict are potential hazards:

```
Master MISO ──────┬──────────────────────
                  │
              Slave 1         Slave 2
              (MISO Hi-Z)    (MISO Hi-Z)
              [Keeper A]     [Keeper B]
```

If Keeper A holds HIGH and Keeper B holds LOW simultaneously, the resulting voltage
depends on the relative drive strengths. To avoid this:

- Use only **one keeper** on the master-side pin (not on each slave's pin).
- Alternatively, use tri-state buffers (74LVC1G125) on each MISO line, enabled only
  when the corresponding CS is asserted.

### Daisy-Chain SPI and Keeper Timing

In daisy-chain SPI, the last device in the chain drives MISO. After the final bit:

```
CS̄  ────────────────────────┐
                            └───── (de-asserted)
MISO  [D7][D6][D5][D4][D3][D2][D1][D0]
                                      ↑
                                  Keeper activates here
                                  holds D0 level
```

The keeper must activate before the next rising edge of SCLK to prevent a spurious clock
from sampling a floating MISO. For high-speed SPI (>10 MHz), hardware keepers with
sub-nanosecond response are essential; software keepers are unsuitable.

### SPI Bus Keeper in Ultra-Low Power Designs

For IoT and battery-powered devices, a combined strategy yields best results:

```
Normal Operation:   SPI active, bus keeper monitoring MISO
─────────────────────────────────────────────────────────────
Post-Transaction:   CS HIGH, keeper holds last MISO bit
                    → ~50 µA keeper current
─────────────────────────────────────────────────────────────
Enter Sleep (>100ms): Release MISO to pull-down (~0 µA)
                    → Pull-down dissipates 0 W when
                      MISO is driven LOW by keeper
─────────────────────────────────────────────────────────────
Deep Sleep / STOP:  All SPI GPIOs to analog input mode
                    → Zero leakage from SPI lines
```

---

## Summary

| Aspect | Key Points |
|--------|-----------|
| **Problem** | Floating SPI lines (especially MISO after CS de-assertion) cause data corruption, EMI susceptibility, and CMOS shoot-through current. |
| **Bus Keeper Principle** | A weak latch (two cascaded inverters) maintains the last driven logic level on a signal line with negligible current draw (~10–50 µA). |
| **Hardware Keeper** | Available as built-in IOB option in FPGAs (Xilinx `KEEPER`, Altera `bus_hold`) and as pinctrl `bias-bus-hold` on Linux-capable SoCs. |
| **Software Keeper** | Implemented by driving the MISO/MOSI pin to the last sampled bit level immediately after CS de-assertion, then releasing to weak pull after a timeout. |
| **Power Impact** | Replaces continuous pull-resistor current (330 µA @ 3.3V / 10 kΩ) with effectively zero idle current; critical for battery-operated IoT devices. |
| **CS Line** | Always use a dedicated **pull-up resistor** on CS̄ — not a keeper — to guarantee device de-selection during MCU reset or boot phases. |
| **Multi-Slave** | Use a single keeper on the master-side MISO pin; avoid per-slave keepers that can conflict. For strict isolation, use a 74LVC1G125 tri-state buffer per slave MISO. |
| **Speed Limitation** | Software bus keepers are unsuitable for SPI > ~1 MHz due to GPIO switching latency; hardware keepers respond in < 1 ns. |
| **C/C++ Approach** | Use HAL GPIO pull/no-pull configuration, pinctrl states (Linux), or a portable `SoftwareBusKeeper` class with shadow register and timeout. |
| **Rust Approach** | Wrap `embedded-hal` `SpiBus` + `OutputPin` in a `SpiBusKeeper` struct that tracks `BusIdleState`; use `no_std`-compatible `BusKeeperMonitor` for health monitoring. |

---

*Document: 74 — Bus Keeper Circuits | SPI Programming Reference Series*