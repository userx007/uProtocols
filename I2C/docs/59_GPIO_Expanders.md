# 59. GPIO Expanders — Using I2C Port Expanders for Additional I/O Pins with Interrupt Handling


- **How GPIO expanders work** — the register-mapped I2C model for controlling I/O pins
- **Common ICs** — MCP23017, PCF8574, TCA9534/9555, and others compared in a table
- **Hardware setup** — address selection, wiring diagrams, and the full MCP23017 register map
- **C/C++ code** — a full bare-metal STM32 HAL driver (header + source) and an Arduino `Wire` implementation, both with clean API layers over the raw register access
- **Rust code** — a `no_std` `embedded-hal` driver (works on STM32) and a Linux/Raspberry Pi variant using `linux-embedded-hal`, both sharing the same driver struct
- **Interrupt handling** — in-depth explanation of INTCAP vs GPIO reads, open-drain mirroring, multi-expander scanning in C and RTIC-based Rust
- **Advanced topics** — cascading 8 chips for 128 pins, output latching, I2C timing, power budgeting, and PCF8574 vs MCP23017 trade-offs
- **Summary** — key takeaways distilled into actionable bullet points


---

## Table of Contents

1. [Introduction](#introduction)
2. [How GPIO Expanders Work](#how-gpio-expanders-work)
3. [Common I2C GPIO Expander ICs](#common-i2c-gpio-expander-ics)
4. [Hardware Setup and Addressing](#hardware-setup-and-addressing)
5. [Internal Register Architecture](#internal-register-architecture)
6. [Programming in C/C++](#programming-in-cc)
   - [Bare-Metal C (STM32 HAL)](#bare-metal-c-stm32-hal)
   - [Arduino C++](#arduino-c)
7. [Programming in Rust](#programming-in-rust)
   - [Embedded Rust (no_std)](#embedded-rust-no_std)
   - [Rust on Linux (std)](#rust-on-linux-std)
8. [Interrupt Handling](#interrupt-handling)
   - [Interrupt Handling in C/C++](#interrupt-handling-in-cc)
   - [Interrupt Handling in Rust](#interrupt-handling-in-rust)
9. [Advanced Topics](#advanced-topics)
10. [Summary](#summary)

---

## Introduction

Microcontrollers often expose a limited number of GPIO (General-Purpose Input/Output) pins. When a project grows to need more buttons, LEDs, relays, or sensors than the MCU can natively support, **GPIO expanders** provide an elegant solution. Rather than switching to a larger, more expensive MCU, an I2C GPIO expander adds 8, 16, or even 32 additional I/O lines with only two wires (SDA and SCL) shared across all devices.

GPIO expanders sit on the I2C bus as peripherals. The host MCU reads and writes their internal registers over I2C to control pin direction, output state, pull-up resistors, and interrupt behavior. Because multiple expanders can coexist on a single bus (differentiated by their 3-bit or 4-bit address pins), a design can scale dramatically without adding bus wires.

---

## How GPIO Expanders Work

At their core, GPIO expanders are **I2C-addressed register files** where each bit in a register corresponds to a physical pin. The fundamental operations are:

- **Write** to an output register → drives the corresponding pins HIGH or LOW
- **Read** from an input register → samples the current logic level on each pin
- **Write** to a direction register → configures each pin as input or output
- **Read** from an interrupt-flag register → identifies which pin caused an interrupt

The MCU communicates with the expander using standard I2C transactions:

```
START → [Device Address + R/W] → [Register Address] → [Data bytes] → STOP
```

Most expanders also provide a dedicated **INT pin** (active-low, open-drain) that asserts when any monitored input changes state, allowing the MCU to avoid polling and instead react with an ISR (Interrupt Service Routine).

---

## Common I2C GPIO Expander ICs

| IC | Pins | Address Range | Key Features |
|----|------|--------------|-------------|
| **MCP23008** | 8 | 0x20–0x27 | Interrupts, pull-ups, open-drain INT |
| **MCP23017** | 16 | 0x20–0x27 | Two 8-bit ports (PORTA/B), dual INT lines |
| **PCF8574** | 8 | 0x20–0x27 or 0x38–0x3F | Very simple, quasi-bidirectional I/O |
| **PCF8575** | 16 | 0x20–0x27 | 16-bit, two-byte read/write |
| **TCA9534** | 8 | 0x20–0x27 | Low voltage (1.65 V–5.5 V), latched INT |
| **TCA9555** | 16 | 0x20–0x27 | Low power, dual port, latched INT |
| **PCA9685** | 16 PWM | 0x40–0x7F | PWM outputs only, used for servos/LEDs |

The **MCP23017** is the most widely used expander in embedded design — it is well-documented, inexpensive, supports interrupts robustly, and is available from multiple vendors. Most code examples in this document target the MCP23017 but the concepts apply universally.

---

## Hardware Setup and Addressing

### Address Selection

The MCP23017 has three address pins (`A0`, `A1`, `A2`). Each can be tied HIGH or LOW, yielding 8 unique addresses (0x20 through 0x27). Up to eight MCP23017 chips can thus coexist on one bus — providing up to **128 additional GPIO pins** with two wires.

```
MCP23017 A2 A1 A0  →  I2C Address
         0  0  0   →  0x20
         0  0  1   →  0x21
         0  1  0   →  0x22
         ...
         1  1  1   →  0x27
```

### Minimal Wiring

```
MCU SDA ────────────────┬──── MCP23017 SDA (pin 13)
MCU SCL ────────────────┼──── MCP23017 SCL (pin 12)
4.7kΩ pull-up to VCC ───┘

MCU GPIO (input) ───────────── MCP23017 INTA (pin 20)  [optional interrupt]
MCU GPIO (input) ───────────── MCP23017 INTB (pin 19)  [optional interrupt]

A0, A1, A2 → GND or VCC to set address
RESET → VCC (or MCU GPIO for hard reset)
VSS → GND,  VDD → 3.3V or 5V
```

---

## Internal Register Architecture

The MCP23017 uses **16 registers** when in the default BANK=0 mode (registers are paired for Port A and Port B):

| Register | Address | Name | Function |
|----------|---------|------|----------|
| IODIRA | 0x00 | I/O Direction A | 1=input, 0=output (per bit) |
| IODIRB | 0x01 | I/O Direction B | Same for Port B |
| IPOLA | 0x02 | Input Polarity A | 1=inverted input |
| IPOLB | 0x03 | Input Polarity B | |
| GPINTENA | 0x04 | Interrupt Enable A | 1=enable interrupt on pin |
| GPINTENB | 0x05 | Interrupt Enable B | |
| DEFVALA | 0x06 | Default Value A | Comparison value for INT |
| DEFVALB | 0x07 | Default Value B | |
| INTCONA | 0x08 | Interrupt Control A | 0=change, 1=compare to DEFVAL |
| INTCONB | 0x09 | Interrupt Control B | |
| IOCON | 0x0A | Configuration | Mirroring, open-drain, polarity |
| GPPUA | 0x0C | Pull-Up A | 1=100kΩ pull-up enabled |
| GPPUB | 0x0D | Pull-Up B | |
| INTFA | 0x0E | Interrupt Flag A | 1=pin caused interrupt |
| INTFB | 0x0F | Interrupt Flag B | |
| INTCAPA | 0x10 | Interrupt Capture A | Pin state at time of interrupt |
| INTCAPB | 0x11 | Interrupt Capture B | |
| GPIOA | 0x12 | GPIO Port A | Read inputs / write outputs |
| GPIOB | 0x13 | GPIO Port B | |
| OLATA | 0x14 | Output Latch A | Reflects output latch state |
| OLATB | 0x15 | Output Latch B | |

---

## Programming in C/C++

### Bare-Metal C (STM32 HAL)

This example targets an STM32 MCU using the STM32 HAL I2C driver. It initializes Port A as all-outputs and Port B as all-inputs with pull-ups and interrupt-on-change enabled.

```c
/* gpio_expander_mcp23017.h */
#ifndef GPIO_EXPANDER_MCP23017_H
#define GPIO_EXPANDER_MCP23017_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* Default I2C address with A2=A1=A0=0 */
#define MCP23017_ADDR       (0x20 << 1)   /* STM32 HAL uses 8-bit address */

/* Register addresses (BANK=0, default) */
#define MCP23017_IODIRA     0x00
#define MCP23017_IODIRB     0x01
#define MCP23017_IPOLA      0x02
#define MCP23017_IPOLB      0x03
#define MCP23017_GPINTENA   0x04
#define MCP23017_GPINTENB   0x05
#define MCP23017_DEFVALA    0x06
#define MCP23017_DEFVALB    0x07
#define MCP23017_INTCONA    0x08
#define MCP23017_INTCONB    0x09
#define MCP23017_IOCON      0x0A
#define MCP23017_GPPUA      0x0C
#define MCP23017_GPPUB      0x0D
#define MCP23017_INTFA      0x0E
#define MCP23017_INTFB      0x0F
#define MCP23017_INTCAPA    0x10
#define MCP23017_INTCAPB    0x11
#define MCP23017_GPIOA      0x12
#define MCP23017_GPIOB      0x13
#define MCP23017_OLATA      0x14
#define MCP23017_OLATB      0x15

/* IOCON bits */
#define IOCON_BANK          (1 << 7)
#define IOCON_MIRROR        (1 << 6)  /* Mirror INTA/INTB pins */
#define IOCON_SEQOP         (1 << 5)  /* Sequential operation disable */
#define IOCON_DISSLW        (1 << 4)  /* Slew rate disable */
#define IOCON_HAEN          (1 << 3)  /* Hardware address enable (MCP23S17) */
#define IOCON_ODR           (1 << 2)  /* INT pin open-drain */
#define IOCON_INTPOL        (1 << 1)  /* INT pin active-high */

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint16_t           dev_addr;
    uint8_t            output_latch_a;
    uint8_t            output_latch_b;
} MCP23017_Handle;

HAL_StatusTypeDef MCP23017_Init(MCP23017_Handle *dev, I2C_HandleTypeDef *hi2c, uint8_t addr_pins);
HAL_StatusTypeDef MCP23017_WriteReg(MCP23017_Handle *dev, uint8_t reg, uint8_t value);
HAL_StatusTypeDef MCP23017_ReadReg(MCP23017_Handle *dev, uint8_t reg, uint8_t *value);
HAL_StatusTypeDef MCP23017_SetDirection(MCP23017_Handle *dev, bool port_b, uint8_t dir_mask);
HAL_StatusTypeDef MCP23017_WritePortA(MCP23017_Handle *dev, uint8_t value);
HAL_StatusTypeDef MCP23017_WritePortB(MCP23017_Handle *dev, uint8_t value);
HAL_StatusTypeDef MCP23017_SetPin(MCP23017_Handle *dev, bool port_b, uint8_t pin, bool state);
HAL_StatusTypeDef MCP23017_ReadPortA(MCP23017_Handle *dev, uint8_t *value);
HAL_StatusTypeDef MCP23017_ReadPortB(MCP23017_Handle *dev, uint8_t *value);
HAL_StatusTypeDef MCP23017_EnableInterrupts(MCP23017_Handle *dev, bool port_b,
                                             uint8_t pin_mask, bool mirror);
HAL_StatusTypeDef MCP23017_GetInterruptSource(MCP23017_Handle *dev, bool port_b,
                                               uint8_t *int_flags, uint8_t *capture);

#endif /* GPIO_EXPANDER_MCP23017_H */
```

```c
/* gpio_expander_mcp23017.c */
#include "gpio_expander_mcp23017.h"

#define I2C_TIMEOUT_MS  10

/* Write a single register */
HAL_StatusTypeDef MCP23017_WriteReg(MCP23017_Handle *dev, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return HAL_I2C_Master_Transmit(dev->hi2c, dev->dev_addr,
                                   buf, 2, I2C_TIMEOUT_MS);
}

/* Read a single register */
HAL_StatusTypeDef MCP23017_ReadReg(MCP23017_Handle *dev, uint8_t reg, uint8_t *value)
{
    HAL_StatusTypeDef ret;
    ret = HAL_I2C_Master_Transmit(dev->hi2c, dev->dev_addr,
                                   &reg, 1, I2C_TIMEOUT_MS);
    if (ret != HAL_OK) return ret;
    return HAL_I2C_Master_Receive(dev->hi2c, dev->dev_addr,
                                   value, 1, I2C_TIMEOUT_MS);
}

/**
 * @brief  Initialize MCP23017.
 * @param  addr_pins  Lower 3 bits select I2C address offset (A2:A1:A0)
 */
HAL_StatusTypeDef MCP23017_Init(MCP23017_Handle *dev,
                                  I2C_HandleTypeDef *hi2c,
                                  uint8_t addr_pins)
{
    dev->hi2c          = hi2c;
    dev->dev_addr      = (uint16_t)((0x20 | (addr_pins & 0x07)) << 1);
    dev->output_latch_a = 0x00;
    dev->output_latch_b = 0x00;

    HAL_StatusTypeDef ret;

    /* Configure IOCON: sequential operation on, INT open-drain */
    ret = MCP23017_WriteReg(dev, MCP23017_IOCON, IOCON_ODR);
    if (ret != HAL_OK) return ret;

    /* Default: all pins as inputs */
    ret = MCP23017_WriteReg(dev, MCP23017_IODIRA, 0xFF);
    if (ret != HAL_OK) return ret;
    ret = MCP23017_WriteReg(dev, MCP23017_IODIRB, 0xFF);
    if (ret != HAL_OK) return ret;

    /* Disable all interrupts initially */
    ret = MCP23017_WriteReg(dev, MCP23017_GPINTENA, 0x00);
    if (ret != HAL_OK) return ret;
    ret = MCP23017_WriteReg(dev, MCP23017_GPINTENB, 0x00);

    return ret;
}

/* Set pin direction: dir_mask bit=1 means input, bit=0 means output */
HAL_StatusTypeDef MCP23017_SetDirection(MCP23017_Handle *dev, bool port_b, uint8_t dir_mask)
{
    uint8_t reg = port_b ? MCP23017_IODIRB : MCP23017_IODIRA;
    return MCP23017_WriteReg(dev, reg, dir_mask);
}

/* Write entire Port A (output pins only) */
HAL_StatusTypeDef MCP23017_WritePortA(MCP23017_Handle *dev, uint8_t value)
{
    dev->output_latch_a = value;
    return MCP23017_WriteReg(dev, MCP23017_OLATA, value);
}

/* Write entire Port B */
HAL_StatusTypeDef MCP23017_WritePortB(MCP23017_Handle *dev, uint8_t value)
{
    dev->output_latch_b = value;
    return MCP23017_WriteReg(dev, MCP23017_OLATB, value);
}

/* Set or clear a single output pin without affecting others */
HAL_StatusTypeDef MCP23017_SetPin(MCP23017_Handle *dev, bool port_b,
                                    uint8_t pin, bool state)
{
    uint8_t *latch = port_b ? &dev->output_latch_b : &dev->output_latch_a;
    uint8_t reg    = port_b ? MCP23017_OLATB : MCP23017_OLATA;

    if (state)
        *latch |=  (1u << pin);
    else
        *latch &= ~(1u << pin);

    return MCP23017_WriteReg(dev, reg, *latch);
}

/* Read Port A input state */
HAL_StatusTypeDef MCP23017_ReadPortA(MCP23017_Handle *dev, uint8_t *value)
{
    return MCP23017_ReadReg(dev, MCP23017_GPIOA, value);
}

/* Read Port B input state */
HAL_StatusTypeDef MCP23017_ReadPortB(MCP23017_Handle *dev, uint8_t *value)
{
    return MCP23017_ReadReg(dev, MCP23017_GPIOB, value);
}

/**
 * @brief  Enable interrupt-on-change for selected pins.
 * @param  pin_mask  Bitmask of pins to enable; each bit = one pin
 * @param  mirror    If true, INTA and INTB are OR'd together on both INT pins
 */
HAL_StatusTypeDef MCP23017_EnableInterrupts(MCP23017_Handle *dev, bool port_b,
                                              uint8_t pin_mask, bool mirror)
{
    HAL_StatusTypeDef ret;
    uint8_t iocon = IOCON_ODR;          /* Open-drain INT pin */
    if (mirror) iocon |= IOCON_MIRROR;

    ret = MCP23017_WriteReg(dev, MCP23017_IOCON, iocon);
    if (ret != HAL_OK) return ret;

    /* Enable pull-ups on input pins */
    uint8_t pu_reg = port_b ? MCP23017_GPPUB : MCP23017_GPPUA;
    ret = MCP23017_WriteReg(dev, pu_reg, pin_mask);
    if (ret != HAL_OK) return ret;

    /* Interrupt-on-change (INTCON bit=0 means compare to previous state) */
    uint8_t intcon_reg = port_b ? MCP23017_INTCONB : MCP23017_INTCONA;
    ret = MCP23017_WriteReg(dev, intcon_reg, 0x00);
    if (ret != HAL_OK) return ret;

    /* Enable interrupts on the requested pins */
    uint8_t gpinten_reg = port_b ? MCP23017_GPINTENB : MCP23017_GPINTENA;
    return MCP23017_WriteReg(dev, gpinten_reg, pin_mask);
}

/**
 * @brief  After an INT fires, read which pins triggered it.
 * @param  int_flags  Output: bitmask of pins that caused the interrupt
 * @param  capture    Output: pin states captured at time of interrupt
 */
HAL_StatusTypeDef MCP23017_GetInterruptSource(MCP23017_Handle *dev, bool port_b,
                                               uint8_t *int_flags, uint8_t *capture)
{
    uint8_t intf_reg   = port_b ? MCP23017_INTFB   : MCP23017_INTFA;
    uint8_t intcap_reg = port_b ? MCP23017_INTCAPB : MCP23017_INTCAPA;

    HAL_StatusTypeDef ret;
    ret = MCP23017_ReadReg(dev, intf_reg,   int_flags);
    if (ret != HAL_OK) return ret;
    return MCP23017_ReadReg(dev, intcap_reg, capture);
}
```

```c
/* main.c — usage example */
#include "main.h"
#include "gpio_expander_mcp23017.h"

extern I2C_HandleTypeDef hi2c1;

static MCP23017_Handle expander;
static volatile bool int_pending = false;

/* Called from HAL EXTI interrupt handler for the MCU pin wired to MCP23017 INTA */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == EXPANDER_INT_Pin) {
        int_pending = true;
    }
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_I2C1_Init();

    /* Initialize expander at address 0x20 (A2=A1=A0=GND) */
    if (MCP23017_Init(&expander, &hi2c1, 0x00) != HAL_OK) {
        Error_Handler();
    }

    /* Port A = all outputs (LEDs), Port B = all inputs (buttons) */
    MCP23017_SetDirection(&expander, false, 0x00);  /* Port A: all outputs */
    MCP23017_SetDirection(&expander, true,  0xFF);  /* Port B: all inputs  */

    /* Enable interrupt-on-change for all Port B pins, mirror INTA/INTB */
    MCP23017_EnableInterrupts(&expander, true, 0xFF, true);

    /* Turn on LED on Port A pin 0 */
    MCP23017_SetPin(&expander, false, 0, true);

    while (1) {
        if (int_pending) {
            int_pending = false;

            uint8_t flags, capture;
            MCP23017_GetInterruptSource(&expander, true, &flags, &capture);

            /* Mirror button state on LEDs (Port A reflects Port B) */
            /* capture holds the state when INT fired */
            MCP23017_WritePortA(&expander, ~capture);  /* LEDs active-low */
        }

        HAL_Delay(10);
    }
}
```

---

### Arduino C++

Arduino's `Wire` library makes I2C expander access concise and cross-platform (Uno, Mega, ESP32, RP2040, etc.):

```cpp
// MCP23017_Arduino.ino

#include <Wire.h>

/* ---- Register map ---- */
constexpr uint8_t MCP23017_ADDR    = 0x20;
constexpr uint8_t REG_IODIRA       = 0x00;
constexpr uint8_t REG_IODIRB       = 0x01;
constexpr uint8_t REG_GPPUA        = 0x0C;
constexpr uint8_t REG_GPPUB        = 0x0D;
constexpr uint8_t REG_GPINTENA     = 0x04;
constexpr uint8_t REG_GPINTENB     = 0x05;
constexpr uint8_t REG_INTCONA      = 0x08;
constexpr uint8_t REG_INTCONB      = 0x09;
constexpr uint8_t REG_IOCON        = 0x0A;
constexpr uint8_t REG_INTFA        = 0x0E;
constexpr uint8_t REG_INTFB        = 0x0F;
constexpr uint8_t REG_INTCAPA      = 0x10;
constexpr uint8_t REG_INTCAPB      = 0x11;
constexpr uint8_t REG_GPIOA        = 0x12;
constexpr uint8_t REG_GPIOB        = 0x13;
constexpr uint8_t REG_OLATA        = 0x14;
constexpr uint8_t REG_OLATB        = 0x15;

constexpr uint8_t IOCON_MIRROR     = 0x40;
constexpr uint8_t IOCON_ODR        = 0x04;

constexpr int     INT_PIN          = 2;   /* MCU interrupt pin wired to MCP23017 INTA */

/* ---- Low-level helpers ---- */

void writeReg(uint8_t addr, uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t readReg(uint8_t addr, uint8_t reg)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);           /* Repeated START */
    Wire.requestFrom(addr, (uint8_t)1);
    return Wire.read();
}

/* ---- Higher-level API ---- */

void expanderInit(uint8_t addr)
{
    /* Open-drain INT, mirror INTA/INTB so either port fires both pins */
    writeReg(addr, REG_IOCON, IOCON_ODR | IOCON_MIRROR);

    /* Port A = outputs, Port B = inputs */
    writeReg(addr, REG_IODIRA, 0x00);
    writeReg(addr, REG_IODIRB, 0xFF);

    /* Enable pull-ups on Port B */
    writeReg(addr, REG_GPPUB, 0xFF);

    /* Interrupt on change for all Port B pins */
    writeReg(addr, REG_INTCONB,  0x00);   /* Compare to previous value */
    writeReg(addr, REG_GPINTENB, 0xFF);   /* Enable all Port B INT */
}

void setOutputA(uint8_t addr, uint8_t value)
{
    writeReg(addr, REG_OLATA, value);
}

uint8_t readInputB(uint8_t addr)
{
    return readReg(addr, REG_GPIOB);
}

/* Read which Port B pin caused the interrupt and the captured state */
void handleInterrupt(uint8_t addr, uint8_t *flagsOut, uint8_t *captureOut)
{
    *flagsOut   = readReg(addr, REG_INTFB);    /* Which pins fired */
    *captureOut = readReg(addr, REG_INTCAPB);  /* State when INT fired */
    /* Reading INTCAP clears the interrupt automatically */
}

/* ---- ISR and state ---- */

volatile bool intFired = false;

void IRAM_ATTR onExpanderInt()
{
    intFired = true;   /* Keep ISR minimal — do work in loop() */
}

/* ---- Arduino setup / loop ---- */

void setup()
{
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(400000);   /* Fast mode 400 kHz */

    expanderInit(MCP23017_ADDR);

    pinMode(INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), onExpanderInt, FALLING);

    Serial.println("MCP23017 initialized — waiting for button presses on Port B");
}

void loop()
{
    if (intFired) {
        intFired = false;

        uint8_t flags, capture;
        handleInterrupt(MCP23017_ADDR, &flags, &capture);

        Serial.print("INT flags:   0b");
        Serial.println(flags,   BIN);
        Serial.print("Captured:    0b");
        Serial.println(capture, BIN);

        /* Mirror button states as LED patterns on Port A (active-low LEDs) */
        setOutputA(MCP23017_ADDR, ~capture);
    }
}
```

---

## Programming in Rust

### Embedded Rust (no_std)

The Rust embedded ecosystem uses the `embedded-hal` traits for hardware abstraction. The `mcp23017` crate wraps the chip natively, but below is a hand-rolled implementation demonstrating the trait approach clearly.

```toml
# Cargo.toml
[package]
name = "gpio_expander_demo"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal = "1.0"
cortex-m = "0.7"
cortex-m-rt = "0.7"
stm32f4xx-hal = { version = "0.21", features = ["stm32f411"] }
panic-halt = "0.2"
```

```rust
// src/mcp23017.rs  — no_std driver using embedded-hal 1.0 traits

use embedded_hal::i2c::{I2c, SevenBitAddress};

// ─── Register addresses ───────────────────────────────────────────────────────

#[allow(dead_code)]
pub mod reg {
    pub const IODIRA:   u8 = 0x00;
    pub const IODIRB:   u8 = 0x01;
    pub const IPOLA:    u8 = 0x02;
    pub const IPOLB:    u8 = 0x03;
    pub const GPINTENA: u8 = 0x04;
    pub const GPINTENB: u8 = 0x05;
    pub const DEFVALA:  u8 = 0x06;
    pub const DEFVALB:  u8 = 0x07;
    pub const INTCONA:  u8 = 0x08;
    pub const INTCONB:  u8 = 0x09;
    pub const IOCON:    u8 = 0x0A;
    pub const GPPUA:    u8 = 0x0C;
    pub const GPPUB:    u8 = 0x0D;
    pub const INTFA:    u8 = 0x0E;
    pub const INTFB:    u8 = 0x0F;
    pub const INTCAPA:  u8 = 0x10;
    pub const INTCAPB:  u8 = 0x11;
    pub const GPIOA:    u8 = 0x12;
    pub const GPIOB:    u8 = 0x13;
    pub const OLATA:    u8 = 0x14;
    pub const OLATB:    u8 = 0x15;
}

// ─── IOCON bitflags ───────────────────────────────────────────────────────────

pub const IOCON_MIRROR: u8 = 1 << 6;
pub const IOCON_ODR:    u8 = 1 << 2;  // INT pin: open-drain
pub const IOCON_INTPOL: u8 = 1 << 1;  // INT polarity: active-high

// ─── Port selector ────────────────────────────────────────────────────────────

#[derive(Clone, Copy, Debug)]
pub enum Port { A, B }

impl Port {
    fn iodir_reg(self)   -> u8 { match self { Port::A => reg::IODIRA,   Port::B => reg::IODIRB   } }
    fn gppu_reg(self)    -> u8 { match self { Port::A => reg::GPPUA,    Port::B => reg::GPPUB    } }
    fn gpinten_reg(self) -> u8 { match self { Port::A => reg::GPINTENA, Port::B => reg::GPINTENB } }
    fn intcon_reg(self)  -> u8 { match self { Port::A => reg::INTCONA,  Port::B => reg::INTCONB  } }
    fn intf_reg(self)    -> u8 { match self { Port::A => reg::INTFA,    Port::B => reg::INTFB    } }
    fn intcap_reg(self)  -> u8 { match self { Port::A => reg::INTCAPA,  Port::B => reg::INTCAPB  } }
    fn gpio_reg(self)    -> u8 { match self { Port::A => reg::GPIOA,    Port::B => reg::GPIOB    } }
    fn olat_reg(self)    -> u8 { match self { Port::A => reg::OLATA,    Port::B => reg::OLATB    } }
}

// ─── Driver struct ────────────────────────────────────────────────────────────

pub struct Mcp23017<I2C> {
    i2c:         I2C,
    address:     SevenBitAddress,
    latch_a:     u8,
    latch_b:     u8,
}

impl<I2C: I2c> Mcp23017<I2C> {
    /// Construct a new driver. `addr_pins` is the 3-bit A2:A1:A0 value.
    pub fn new(i2c: I2C, addr_pins: u8) -> Self {
        Mcp23017 {
            i2c,
            address: 0x20 | (addr_pins & 0x07),
            latch_a: 0,
            latch_b: 0,
        }
    }

    // ── Register access ──────────────────────────────────────────────────────

    pub fn write_reg(&mut self, reg: u8, value: u8) -> Result<(), I2C::Error> {
        self.i2c.write(self.address, &[reg, value])
    }

    pub fn read_reg(&mut self, reg: u8) -> Result<u8, I2C::Error> {
        let mut buf = [0u8];
        self.i2c.write_read(self.address, &[reg], &mut buf)?;
        Ok(buf[0])
    }

    // ── Initialization ───────────────────────────────────────────────────────

    pub fn init(&mut self) -> Result<(), I2C::Error> {
        // Open-drain INT, mirror ports
        self.write_reg(reg::IOCON, IOCON_ODR | IOCON_MIRROR)?;
        // All pins as inputs by default
        self.write_reg(reg::IODIRA, 0xFF)?;
        self.write_reg(reg::IODIRB, 0xFF)?;
        // All interrupts disabled
        self.write_reg(reg::GPINTENA, 0x00)?;
        self.write_reg(reg::GPINTENB, 0x00)
    }

    // ── Direction ────────────────────────────────────────────────────────────

    /// `dir_mask` bit=1 → input, bit=0 → output
    pub fn set_direction(&mut self, port: Port, dir_mask: u8) -> Result<(), I2C::Error> {
        self.write_reg(port.iodir_reg(), dir_mask)
    }

    // ── Pull-ups ─────────────────────────────────────────────────────────────

    pub fn set_pullups(&mut self, port: Port, mask: u8) -> Result<(), I2C::Error> {
        self.write_reg(port.gppu_reg(), mask)
    }

    // ── Output ───────────────────────────────────────────────────────────────

    pub fn write_port(&mut self, port: Port, value: u8) -> Result<(), I2C::Error> {
        match port {
            Port::A => self.latch_a = value,
            Port::B => self.latch_b = value,
        }
        self.write_reg(port.olat_reg(), value)
    }

    /// Set or clear a single output pin without disturbing the others.
    pub fn set_pin(&mut self, port: Port, pin: u8, high: bool) -> Result<(), I2C::Error> {
        let latch = match port {
            Port::A => &mut self.latch_a,
            Port::B => &mut self.latch_b,
        };
        if high {
            *latch |=  1 << pin;
        } else {
            *latch &= !(1 << pin);
        }
        let val = *latch;
        self.write_reg(port.olat_reg(), val)
    }

    // ── Input ────────────────────────────────────────────────────────────────

    pub fn read_port(&mut self, port: Port) -> Result<u8, I2C::Error> {
        self.read_reg(port.gpio_reg())
    }

    pub fn read_pin(&mut self, port: Port, pin: u8) -> Result<bool, I2C::Error> {
        let val = self.read_port(port)?;
        Ok((val >> pin) & 1 != 0)
    }

    // ── Interrupts ───────────────────────────────────────────────────────────

    /// Enable interrupt-on-change for pins given by `pin_mask`.
    pub fn enable_interrupts(&mut self, port: Port, pin_mask: u8) -> Result<(), I2C::Error> {
        // INTCON = 0 → compare to previous value (change detection)
        self.write_reg(port.intcon_reg(), 0x00)?;
        self.write_reg(port.gpinten_reg(), pin_mask)
    }

    /// Returns (interrupt_flags, captured_state). Reading INTCAP clears the INT.
    pub fn interrupt_source(&mut self, port: Port) -> Result<(u8, u8), I2C::Error> {
        let flags   = self.read_reg(port.intf_reg())?;
        let capture = self.read_reg(port.intcap_reg())?;
        Ok((flags, capture))
    }

    /// Consume the driver and return the underlying I2C bus.
    pub fn release(self) -> I2C {
        self.i2c
    }
}
```

```rust
// src/main.rs  — application entry point (STM32F411 target)

#![no_std]
#![no_main]

use cortex_m::interrupt;
use cortex_m_rt::entry;
use panic_halt as _;
use stm32f4xx_hal::{pac, prelude::*, i2c::I2c};
use core::cell::RefCell;
use cortex_m::interrupt::Mutex;

mod mcp23017;
use mcp23017::{Mcp23017, Port};

// Shared flag between ISR and main thread
static INT_PENDING: Mutex<RefCell<bool>> = Mutex::new(RefCell::new(false));

// EXTI1 fires when MCP23017 INTA goes low (connected to PA1 on MCU)
#[cortex_m_rt::interrupt]
fn EXTI1() {
    let dp = unsafe { pac::Peripherals::steal() };
    dp.EXTI.pr.write(|w| w.pr1().set_bit()); // clear pending bit

    interrupt::free(|cs| {
        *INT_PENDING.borrow(cs).borrow_mut() = true;
    });
}

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();
    let cp = cortex_m::Peripherals::take().unwrap();

    let rcc    = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

    let gpiob = dp.GPIOB.split();
    let scl   = gpiob.pb6.into_alternate::<4>().set_open_drain();
    let sda   = gpiob.pb7.into_alternate::<4>().set_open_drain();

    let i2c = I2c::new(dp.I2C1, (scl, sda), 400.kHz(), &clocks);

    // Build driver (address pins A2=A1=A0=0 → 0x20)
    let mut expander = Mcp23017::new(i2c, 0x00);
    expander.init().unwrap();

    // Port A = all outputs (LEDs), Port B = all inputs (buttons)
    expander.set_direction(Port::A, 0x00).unwrap();
    expander.set_direction(Port::B, 0xFF).unwrap();
    expander.set_pullups(Port::B, 0xFF).unwrap();

    // Enable interrupts on all Port B pins
    expander.enable_interrupts(Port::B, 0xFF).unwrap();

    // Configure MCU EXTI1 for PA1 (INTA) falling edge — omitted for brevity:
    // setup_exti_pa1_falling_edge(&dp.EXTI, &dp.SYSCFG);

    loop {
        let pending = interrupt::free(|cs| {
            let mut flag = INT_PENDING.borrow(cs).borrow_mut();
            let val = *flag;
            *flag = false;
            val
        });

        if pending {
            let (flags, capture) = expander.interrupt_source(Port::B).unwrap();

            // Determine which pin changed
            for pin in 0..8 {
                if (flags >> pin) & 1 != 0 {
                    let pressed = (capture >> pin) & 1 == 0; // active-low button
                    // Mirror to Port A LED
                    expander.set_pin(Port::A, pin, pressed).unwrap();
                }
            }
        }

        cortex_m::asm::wfi(); // Wait For Interrupt to save power
    }
}
```

---

### Rust on Linux (std)

On Linux (e.g., Raspberry Pi), the `linux-embedded-hal` crate bridges the `embedded-hal` traits to `/dev/i2c-*` kernel drivers:

```toml
# Cargo.toml
[dependencies]
linux-embedded-hal = "0.4"
embedded-hal = "1.0"
```

```rust
// src/main.rs — runs on Raspberry Pi / any Linux SBC

use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;

mod mcp23017;
use mcp23017::{Mcp23017, Port};

use std::thread;
use std::time::Duration;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open I2C bus (bus 1 on Raspberry Pi = /dev/i2c-1)
    let i2c = I2cdev::new("/dev/i2c-1")?;
    let mut expander = Mcp23017::new(i2c, 0x00);

    expander.init()?;
    expander.set_direction(Port::A, 0x00)?;  // Port A: outputs
    expander.set_direction(Port::B, 0xFF)?;  // Port B: inputs
    expander.set_pullups(Port::B, 0xFF)?;
    expander.enable_interrupts(Port::B, 0xFF)?;

    // On Linux, poll the INT GPIO via sysfs or gpiod in a separate thread.
    // Here we demonstrate simple polling of the input register instead.
    let running = Arc::new(AtomicBool::new(true));
    let r = running.clone();

    ctrlc::set_handler(move || { r.store(false, Ordering::SeqCst); })?;

    println!("Monitoring Port B buttons. Press Ctrl+C to quit.");

    let mut last_state: u8 = 0xFF;

    while running.load(Ordering::SeqCst) {
        let current = expander.read_port(Port::B)?;

        if current != last_state {
            let changed = current ^ last_state;
            for pin in 0..8u8 {
                if (changed >> pin) & 1 != 0 {
                    let pressed = (current >> pin) & 1 == 0;
                    println!("Pin B{} {}", pin, if pressed { "PRESSED" } else { "RELEASED" });
                    expander.set_pin(Port::A, pin, pressed)?;
                }
            }
            last_state = current;
        }

        thread::sleep(Duration::from_millis(10));
    }

    Ok(())
}
```

---

## Interrupt Handling

Polling a GPIO expander works but wastes CPU cycles and introduces latency. The preferred approach is to use the expander's **INT** output pin to signal the host MCU. The MCU pin is configured as an external interrupt input, and on each falling edge (INT is active-low, open-drain) the ISR sets a flag for the main loop to process.

### Key Points for Reliable Interrupt Handling

1. **Always read INTCAP, not GPIO** after an interrupt fires. Reading GPIO clears the INT flag but the register reflects the *current* state, which may have changed again. INTCAP holds the state frozen at the moment of interruption.

2. **Reading INTCAP clears the interrupt.** You do not need to separately clear the INT flag — the read is atomic from the MCP23017's perspective.

3. **Mirror mode (IOCON.MIRROR = 1)** is usually recommended: both INTA and INTB wire out to the same MCU pin. Any change on either port triggers both INT outputs, simplifying the circuit.

4. **Open-drain INT (IOCON.ODR = 1)** allows wiring multiple expanders' INT pins together to a single MCU pin with a pull-up — the bus is only low when any expander needs attention.

5. **Debouncing.** GPIO expanders do not debounce mechanical contacts. Implement debouncing in software (e.g., confirm the state is stable after 10–20 ms) or use external RC circuits on the button inputs.

### Interrupt Handling in C/C++

```c
/*
 * Multi-expander interrupt handling.
 * Two MCP23017 devices share a single INT line (open-drain wired together).
 * On INT, we scan all devices to find which pin triggered.
 */

#define NUM_EXPANDERS  2

static MCP23017_Handle expanders[NUM_EXPANDERS];
static volatile bool int_pending = false;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SHARED_INT_Pin)
        int_pending = true;
}

void process_expander_interrupt(void)
{
    for (int i = 0; i < NUM_EXPANDERS; i++) {
        uint8_t flags_a, cap_a, flags_b, cap_b;

        MCP23017_GetInterruptSource(&expanders[i], false, &flags_a, &cap_a);
        MCP23017_GetInterruptSource(&expanders[i], true,  &flags_b, &cap_b);

        if (flags_a) {
            /* Handle Port A events for expanders[i] */
            for (int pin = 0; pin < 8; pin++) {
                if ((flags_a >> pin) & 1) {
                    bool high = (cap_a >> pin) & 1;
                    on_pin_change(i, 'A', pin, high);
                }
            }
        }

        if (flags_b) {
            for (int pin = 0; pin < 8; pin++) {
                if ((flags_b >> pin) & 1) {
                    bool high = (cap_b >> pin) & 1;
                    on_pin_change(i, 'B', pin, high);
                }
            }
        }
    }
}

/* In the main loop */
while (1) {
    if (int_pending) {
        int_pending = false;
        process_expander_interrupt();
    }
    /* ... other application work ... */
}
```

### Interrupt Handling in Rust

```rust
// Expanded interrupt handler example for Rust embedded
// Using RTIC (Real-Time Interrupt-driven Concurrency) framework

#[rtic::app(device = stm32f4xx_hal::pac, peripherals = true, dispatchers = [USART1])]
mod app {
    use stm32f4xx_hal::{pac, prelude::*, i2c::I2c, gpio::{Input, PA1}};
    use crate::mcp23017::{Mcp23017, Port};

    #[shared]
    struct Shared {
        expander: Mcp23017<I2c<pac::I2C1>>,
    }

    #[local]
    struct Local {
        int_pin: PA1<Input>,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        let dp = ctx.device;
        let rcc    = dp.RCC.constrain();
        let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

        let gpioa = dp.GPIOA.split();
        let gpiob = dp.GPIOB.split();

        let scl = gpiob.pb6.into_alternate::<4>().set_open_drain();
        let sda = gpiob.pb7.into_alternate::<4>().set_open_drain();
        let i2c = I2c::new(dp.I2C1, (scl, sda), 400.kHz(), &clocks);

        let int_pin = gpioa.pa1.into_input(); // INTA wired to PA1

        let mut expander = Mcp23017::new(i2c, 0x00);
        expander.init().unwrap();
        expander.set_direction(Port::A, 0x00).unwrap();
        expander.set_direction(Port::B, 0xFF).unwrap();
        expander.set_pullups(Port::B, 0xFF).unwrap();
        expander.enable_interrupts(Port::B, 0xFF).unwrap();

        // Configure EXTI for PA1 falling edge (not shown: SYSCFG + EXTI registers)
        handle_expander_event::spawn().ok();

        (Shared { expander }, Local { int_pin })
    }

    // Hardware interrupt triggered by MCP23017 INTA falling edge
    #[task(binds = EXTI1, priority = 2, shared = [expander])]
    fn exti1(mut ctx: exti1::Context) {
        // Clear EXTI pending bit
        unsafe { (*pac::EXTI::PTR).pr.write(|w| w.pr1().set_bit()); }

        ctx.shared.expander.lock(|exp| {
            let (flags, capture) = exp.interrupt_source(Port::B).unwrap();
            for pin in 0..8u8 {
                if (flags >> pin) & 1 != 0 {
                    let pressed = (capture >> pin) & 1 == 0;
                    exp.set_pin(Port::A, pin, pressed).unwrap();
                }
            }
        });
    }

    #[idle]
    fn idle(_: idle::Context) -> ! {
        loop {
            cortex_m::asm::wfi();
        }
    }
}
```

---

## Advanced Topics

### Cascading Multiple Expanders

Up to **8 MCP23017 chips** can coexist on a single I2C bus. Address pins set each chip's unique address. With 8 chips × 16 pins = **128 additional GPIOs** from two wires.

```
MCU ──── I2C bus ────┬── MCP23017 @ 0x20 (A2=0, A1=0, A0=0) ── 16 LEDs
                     ├── MCP23017 @ 0x21 (A2=0, A1=0, A0=1) ── 16 buttons
                     ├── MCP23017 @ 0x22 (A2=0, A1=1, A0=0) ── 16 relay drivers
                     └── ... up to 0x27
```

When multiple expanders share the INT line (open-drain), the ISR must scan all expanders — the INTF register will be zero for expanders that did not fire.

### Output Latching and Read-Modify-Write

Always write outputs using the **OLAT register**, not GPIOA/GPIOB. Writing to GPIO applies immediately but does not update the latch; subsequent reads of OLAT (or subsequent writes based on OLAT) will reflect stale data. The driver patterns above maintain a shadow register (`latch_a`, `latch_b`) in software to avoid an extra I2C read per bit-manipulation.

### I2C Clock Speed and Timing

The MCP23017 supports standard mode (100 kHz) and fast mode (400 kHz). At 400 kHz, a typical register write takes approximately 60 µs (START + address + register + data + STOP), and a read takes about 100 µs (write register address + repeated START + read). At 100 kHz these values are roughly four times longer.

### Power Considerations

Each MCP23017 pin can source or sink up to 25 mA, with a maximum of 125 mA total per chip. When driving many LEDs simultaneously, ensure adequate decoupling capacitors (100 nF ceramic close to VDD) and check total current draw against the expander's package thermal ratings.

### PCF8574 vs. MCP23017 Trade-offs

The PCF8574 uses quasi-bidirectional I/O: pins simultaneously function as weak pull-ups when configured as inputs. It is simpler (no direction register — set a pin HIGH to make it an input, LOW to drive it) and has smaller I2C overhead. However, it lacks explicit interrupt-on-change support, dedicated pull-up registers, and separate latch registers. For new designs requiring reliable interrupts and clear input/output separation, the MCP23017 family is the better choice.

---

## Summary

GPIO expanders bridge the gap between a resource-constrained MCU and a project's growing I/O demands. By placing an MCP23017 (or similar chip) on the I2C bus, a designer adds 16 independently configurable GPIO pins controlled through just a handful of register writes, leaving the MCU's native pins free for time-critical or high-speed tasks.

**Key takeaways:**

- GPIO expanders are **register-mapped I/O** accessed over I2C; every pin-level operation reduces to a byte read or write to a named register.
- The **direction register** (IODIR) governs whether a pin is an input or output; the **output latch** (OLAT) drives output pins; and the **GPIO register** samples input pins.
- **Interrupt-on-change** (via GPINTEN, INTCON, and the INT output pin) removes the need for polling and minimizes CPU overhead, making it practical to detect button presses or other events without continuous I2C bus activity.
- Reading **INTCAP** (not GPIO) after an interrupt fires guarantees the captured state reflects the moment the interrupt occurred, not the (possibly already changed) current state.
- **Open-drain INT with mirroring** enables wiring multiple expanders to a single MCU interrupt pin — the simplest multi-expander interrupt topology.
- In **C/C++**, the MCP23017 is managed through a small set of HAL-wrapped register-access functions, with a shadow latch for efficient single-pin operations.
- In **Rust**, the `embedded-hal` I2C trait provides a hardware-agnostic interface; the same driver struct compiles for both bare-metal (STM32) and Linux (Raspberry Pi) targets by swapping the concrete I2C type.
- Always debounce mechanical inputs in software, use OLAT for outputs, and ensure I2C pull-up resistors are appropriate for the chosen bus speed.

With these fundamentals, a single MCU can manage hundreds of digital I/O lines while maintaining clean, interrupt-driven code and minimal CPU overhead.

---

*Document generated for the I2C Embedded Programming series — Topic 59: GPIO Expanders*