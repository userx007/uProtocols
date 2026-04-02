# 78. Wake-Up Mechanisms — Using I2C Events to Wake Systems from Low-Power States

**Architecture & Concepts** — A comparison table of low-power states (Sleep → System OFF) with current draw and wake latency, plus an ASCII diagram of the three wake-up topologies.

**Hardware Design** — Pull-up resistor power domain rules, SMBALERT# wiring, and I2C bus behaviour during deep sleep.

**Four C/C++ Examples:**
- STM32L4 I2C slave with `WUPEN` bit for address-match wake from STOP2
- STM32 SMBALERT# handler with full ARA (Alert Response Address) cycle
- Arduino/AVR + MPU-6050 motion interrupt wake from `PWR_DOWN` sleep
- Zephyr RTOS I2C slave with `pm_state_force` power management

**Three Rust Examples:**
- Embassy-STM32 async I2C slave with `wakeup_from_stop: true`
- RTIC v2 on nRF52840 with GPIOTE SMBALERT# handler and ARA
- nRF52840 System OFF wake (~0.4 µA) using sensor INT pin + reset reason detection

**Power Analysis** — A worked duty-cycle calculation showing how a ~7.5 µA average draw can yield multi-decade battery life.

**Pitfalls section** covering the most common implementation mistakes (pull-up supply, clock restoration, listen re-arming, ARA timing, Rust borrow checker, System OFF reset semantics).

---

## Table of Contents

1. [Introduction](#introduction)
2. [Low-Power States Overview](#low-power-states-overview)
3. [I2C Wake-Up Architecture](#i2c-wake-up-architecture)
4. [Hardware Considerations](#hardware-considerations)
5. [Wake-Up Methods](#wake-up-methods)
   - [SMBus Alert Line (SMBALERT#)](#smbus-alert-line-smbalert)
   - [Dedicated Interrupt Pin from I2C Device](#dedicated-interrupt-pin-from-i2c-device)
   - [I2C Address Match Wake-Up](#i2c-address-match-wake-up)
   - [General Call Wake-Up](#general-call-wake-up)
6. [C/C++ Implementation Examples](#cc-implementation-examples)
   - [STM32: I2C Address Match Wake-Up from STOP Mode](#stm32-i2c-address-match-wake-up-from-stop-mode)
   - [STM32: SMBALERT# Wake-Up Handler](#stm32-smbalert-wake-up-handler)
   - [AVR/Arduino: INT Pin Wake from I2C Sensor](#avrarduino-int-pin-wake-from-i2c-sensor)
   - [Zephyr RTOS: I2C Slave Wake-Up](#zephyr-rtos-i2c-slave-wake-up)
7. [Rust Implementation Examples](#rust-implementation-examples)
   - [embassy-stm32: I2C Address Match Wake-Up](#embassy-stm32-i2c-address-match-wake-up)
   - [RTIC + embedded-hal: Alert Pin Wake-Up](#rtic--embedded-hal-alert-pin-wake-up)
   - [nrf52840: I2C-Triggered Wake from System OFF](#nrf52840-i2c-triggered-wake-from-system-off)
8. [Power Consumption Analysis](#power-consumption-analysis)
9. [Common Pitfalls and Troubleshooting](#common-pitfalls-and-troubleshooting)
10. [Summary](#summary)

---

## Introduction

In embedded and IoT systems, power management is critical. Microcontrollers (MCUs) and SoCs spend the majority of their operational life in low-power sleep states, waking only when there is meaningful work to do. I2C — the ubiquitous two-wire serial bus — plays a central role in triggering these wake-up events, because peripherals such as sensors, PMICs (Power Management ICs), RTCs (Real-Time Clocks), and accelerometers are inherently I2C-connected and need to signal the host MCU when attention is required.

This topic covers the mechanisms, protocols, hardware design choices, and firmware strategies for using I2C-related events to wake a system from various low-power states, with full working examples in C/C++ and Rust.

---

## Low-Power States Overview

Modern MCUs offer a hierarchy of low-power modes. Understanding them is essential for choosing the right wake-up strategy:

| Mode | Typical Name (STM32) | CPU | Clocks | RAM | Wake Latency | Current |
|---|---|---|---|---|---|---|
| Sleep | SLEEP | Off | Running | Retained | Microseconds | ~1–10 mA |
| Stop (shallow) | STOP0/STOP1 | Off | Halted | Retained | ~5–50 µs | ~10–300 µA |
| Stop (deep) | STOP2 | Off | Halted | Retained | ~50–300 µs | ~2–20 µA |
| Standby | STANDBY | Off | Off | Lost (SRAM2 opt.) | ~300 µs–ms | ~1–5 µA |
| Shutdown | SHUTDOWN | Off | Off | Lost | ms | <1 µA |

Wake-up from the deepest modes typically requires an external signal — either a dedicated interrupt pin, an SMBus alert line, or (on newer MCUs) an I2C address match that operates independently from the main CPU clock.

---

## I2C Wake-Up Architecture

The fundamental challenge is that I2C is a **synchronous, clock-driven protocol**. When an MCU is asleep, its I2C peripheral's clock may not be running. There are three architectural approaches to handling this:

```
┌─────────────────────────────────────────────────────────────┐
│                   Wake-Up Architecture                      │
│                                                             │
│  ┌──────────┐   SDA/SCL    ┌────────────────────────────┐   │
│  │ I2C      │◄────────────►│  MCU I2C Peripheral        │   │
│  │ Sensor / │              │  (Address Match Logic)      │   │
│  │ PMIC     │              │         │                   │   │
│  │          │   ALERT#     │         ▼                   │   │
│  │          ├─────────────►│  EXTI / NVIC Wake Engine   │   │
│  └──────────┘              │         │                   │   │
│                            │         ▼                   │   │
│                            │      CPU Core               │   │
│                            └────────────────────────────┘   │
│                                                             │
│  Option A: Dedicated INT/ALERT# pin → EXTI → Wake           │
│  Option B: I2C Address Match (LSI/LSE clocked peripheral)   │
│  Option C: General Call on I2C bus → Wake all slaves        │
└─────────────────────────────────────────────────────────────┘
```

**Option A** (interrupt pin) is the simplest and most universally supported — the sensor asserts a GPIO, which routes through the MCU's external interrupt controller to wake the CPU. The I2C communication happens *after* wake-up.

**Option B** (address match wake-up) is more sophisticated: the I2C peripheral keeps its address comparator powered from a low-power clock (LSI or LSE), detects a START + address frame on the bus, and wakes the CPU before the transfer completes. Supported on STM32L, STM32U, STM32WB, nRF52, i.MX RT, and others.

**Option C** (General Call) sends the reserved address 0x00 to broadcast a wake signal to all I2C slaves simultaneously.

---

## Hardware Considerations

### Pull-up Resistors
Pull-up resistors must remain powered during sleep. If they are connected to a switched supply that powers down during sleep, the I2C bus will not be functional as a wake source.

```
VCC (always-on) ──┬─── 4.7kΩ ─── SDA
                  └─── 4.7kΩ ─── SCL
```

### SMBALERT# Pin
The SMBus alert line is an **open-drain, active-low** signal. Multiple devices share a single line (wired-OR). The MCU's EXTI input must be configured with a pull-up and falling-edge trigger.

```
VCC (always-on) ──── 10kΩ ────┬─── SMBALERT# (to MCU EXTI)
                               │
             Device A ────────┤ (open-drain)
             Device B ────────┘
```

### I2C Bus State During Deep Sleep
In deep sleep / standby modes, the I2C peripheral is typically clock-gated. Ensure:
- The I2C peripheral's low-power clock source (LSE, LSI) is configured and enabled before entering sleep.
- The `WUPEN` (wake-up enable) bit is set in the I2C control register.
- SDA/SCL pins are configured in **alternate function mode**, not GPIO, before sleep entry.

---

## Wake-Up Methods

### SMBus Alert Line (SMBALERT#)

The SMBALERT# mechanism (defined in SMBus 2.0 spec) allows a device to signal the host asynchronously. Upon detecting the falling edge of SMBALERT#, the host issues an ARA (Alert Response Address = 0x0C) transaction. The alerting device responds with its own address.

**Sequence:**
```
1. Sensor asserts SMBALERT# LOW
2. MCU EXTI fires → wakes from STOP mode
3. MCU restores clocks, I2C peripheral
4. MCU sends: START + 0x0C (ARA, read) + STOP
5. Sensor responds with its 7-bit address
6. MCU reads sensor register to clear alert
```

### Dedicated Interrupt Pin from I2C Device

Most modern I2C sensors (accelerometers, temperature sensors, etc.) include one or more configurable interrupt output pins (INT1, INT2). These are GPIO outputs on the sensor, connected to GPIO/EXTI inputs on the MCU. The INT pin is configured via I2C registers before sleep, then the MCU can enter sleep knowing the sensor will wake it.

### I2C Address Match Wake-Up

Some MCUs implement an always-on address comparator in their I2C peripheral. The peripheral monitors the SDA/SCL lines using a low-power clock and wakes the MCU when it detects its configured slave address. This is useful when the MCU acts as an **I2C slave** and a master needs to initiate a transfer.

Supported on: STM32L0/L1/L4/L5/U5, STM32WB, nRF52840, i.MX RT106x.

### General Call Wake-Up

Broadcasting to address 0x00 wakes all I2C slaves on the bus simultaneously. Rarely used in practice but useful in multi-node systems where a dedicated master coordinates wake-up of all slave MCUs.

---

## C/C++ Implementation Examples

### STM32: I2C Address Match Wake-Up from STOP Mode

This example configures an STM32L4 as an I2C slave. When a master addresses it while in STOP2 mode, the I2C peripheral (clocked by LSE) wakes the CPU.

```c
/* wake_i2c_stm32l4.c
 * STM32L4 I2C Address-Match Wake-Up from STOP2 Mode
 * Uses HAL + low-level register access for WUPEN configuration
 */

#include "stm32l4xx_hal.h"
#include <string.h>

#define I2C_SLAVE_ADDRESS   0x52   /* 7-bit address */
#define I2C_TIMEOUT_MS      100

I2C_HandleTypeDef hi2c1;
volatile uint8_t rx_buffer[16];
volatile uint8_t wake_flag = 0;

/* ──────────────────────────────────────────────
 * 1. System Clock Configuration
 *    Must keep LSE alive for I2C wake-up clock
 * ────────────────────────────────────────────── */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Enable LSE for I2C low-power clock source */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI | RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.MSIState       = RCC_MSI_ON;
    RCC_OscInitStruct.MSIClockRange  = RCC_MSIRANGE_6;   /* 4 MHz */
    RCC_OscInitStruct.LSEState       = RCC_LSE_ON;        /* 32.768 kHz */
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_MSI;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);

    /* Route LSE to I2C1 kernel clock for low-power operation */
    __HAL_RCC_I2C1_CONFIG(RCC_I2C1CLKSOURCE_HSI);
    /* Note: For STOP wake-up, some STM32 variants need the I2C
       clocked from LSE. Check your specific device reference manual. */
}

/* ──────────────────────────────────────────────
 * 2. I2C Peripheral Initialization as Slave
 * ────────────────────────────────────────────── */
void MX_I2C1_Init(void)
{
    hi2c1.Instance              = I2C1;
    hi2c1.Init.Timing           = 0x00300F38; /* ~100 kHz @ 4 MHz HSI */
    hi2c1.Init.OwnAddress1      = I2C_SLAVE_ADDRESS << 1;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);

    /* Enable Wake-Up from STOP mode on address match.
     * WUPEN bit in I2C_CR1 — available on STM32L4/L5/U5/WB.
     * The I2C peripheral continues monitoring SDA/SCL with
     * its own clock even when the CPU is halted. */
    SET_BIT(hi2c1.Instance->CR1, I2C_CR1_WUPEN);
}

/* ──────────────────────────────────────────────
 * 3. Configure and Enter STOP2 Low-Power Mode
 * ────────────────────────────────────────────── */
void Enter_Stop2_Mode(void)
{
    /* Ensure I2C listen mode is active — peripheral is waiting
       for its own slave address on the bus */
    HAL_I2C_EnableListen_IT(&hi2c1);

    /* Configure PWR for STOP2 (deepest stop mode with RAM retention) */
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* ─── EXECUTION RESUMES HERE AFTER WAKE-UP ─── */

    /* Restore system clocks after wake-up from STOP2.
     * MSI is automatically re-enabled by hardware, but
     * we may need to reconfigure PLL if used. */
    SystemClock_Config();

    wake_flag = 1;
}

/* ──────────────────────────────────────────────
 * 4. I2C Event Callbacks (ISR context)
 * ────────────────────────────────────────────── */

/* Called when our slave address is matched on the bus */
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c,
                          uint8_t TransferDirection,
                          uint16_t AddrMatchCode)
{
    if (TransferDirection == I2C_DIRECTION_TRANSMIT)
    {
        /* Master is writing to us — receive data */
        HAL_I2C_Slave_Seq_Receive_IT(hi2c,
                                     (uint8_t *)rx_buffer,
                                     sizeof(rx_buffer),
                                     I2C_NEXT_FRAME);
    }
    else
    {
        /* Master is reading from us — transmit data */
        uint8_t response[] = {0xDE, 0xAD, 0xBE, 0xEF};
        HAL_I2C_Slave_Seq_Transmit_IT(hi2c,
                                      response,
                                      sizeof(response),
                                      I2C_NEXT_FRAME);
    }
}

/* Called when slave receive transfer completes */
void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    /* Process received data, re-arm listen mode */
    HAL_I2C_EnableListen_IT(hi2c);
}

/* Called on I2C error (NACK, arbitration loss, etc.) */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    uint32_t err = HAL_I2C_GetError(hi2c);
    if (err == HAL_I2C_ERROR_AF)
    {
        /* NACK — normal after last byte; re-arm */
        HAL_I2C_EnableListen_IT(hi2c);
    }
}

/* ──────────────────────────────────────────────
 * 5. Main Application Loop
 * ────────────────────────────────────────────── */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_I2C1_Init();

    while (1)
    {
        /* Do application work while awake */
        if (wake_flag)
        {
            wake_flag = 0;
            /* Process rx_buffer, update state, communicate upstream, etc. */
        }

        /* Enter low-power STOP2 mode.
         * The I2C peripheral will wake us when our address appears on bus. */
        Enter_Stop2_Mode();
    }
}
```

---

### STM32: SMBALERT# Wake-Up Handler

This example configures an EXTI line connected to SMBALERT#, wakes from STOP mode, then performs the ARA (Alert Response Address) cycle to identify the alerting device.

```c
/* smbalert_wake_stm32.c
 * SMBALERT# Wake-Up with ARA (Alert Response Address) Handling
 * Assumes SMBALERT# connected to PA0 (EXTI0)
 */

#include "stm32l4xx_hal.h"

#define ARA_ADDRESS         0x0C   /* SMBus Alert Response Address (7-bit) */
#define SMBALERT_PIN        GPIO_PIN_0
#define SMBALERT_PORT       GPIOA

I2C_HandleTypeDef  hi2c1;
EXTI_HandleTypeDef hexti0;

/* Stores the I2C address of the device that raised ALERT# */
volatile uint8_t alerting_device_addr = 0xFF;
volatile uint8_t alert_received       = 0;

/* ──────────────────────────────────────────────
 * Configure PA0 as EXTI falling-edge input
 * with internal pull-up (SMBALERT# is active-low)
 * ────────────────────────────────────────────── */
void Configure_SMBALERT_EXTI(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin   = SMBALERT_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_IT_FALLING;  /* Trigger on falling edge */
    GPIO_InitStruct.Pull  = GPIO_PULLUP;            /* Pull-up: bus is high when idle */
    HAL_GPIO_Init(SMBALERT_PORT, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

/* ──────────────────────────────────────────────
 * EXTI0 ISR — called when SMBALERT# asserted
 * ────────────────────────────────────────────── */
void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(SMBALERT_PIN);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SMBALERT_PIN)
    {
        alert_received = 1;
        /* CPU will return here from WFI after EXTI fires */
    }
}

/* ──────────────────────────────────────────────
 * Perform Alert Response Address (ARA) cycle
 * Returns the 7-bit address of the alerting device,
 * or 0xFF on failure.
 * ────────────────────────────────────────────── */
uint8_t SMBus_ARA_Read(void)
{
    uint8_t raw_byte = 0;

    /* ARA: master sends START + 0x0C with R/W=1 (read).
     * The alerting device responds with [its_address << 1 | alert_bit].
     * HAL_I2C_Master_Receive uses 8-bit DevAddress (7-bit << 1 | R/W).
     */
    HAL_StatusTypeDef status = HAL_I2C_Master_Receive(
        &hi2c1,
        (ARA_ADDRESS << 1) | 0x01,  /* 0x19 */
        &raw_byte,
        1,
        I2C_TIMEOUT_MS);

    if (status != HAL_OK)
        return 0xFF;   /* No device responded or bus error */

    /* SMBus ARA response: bits [7:1] = device address, bit 0 = alert latch */
    return (raw_byte >> 1) & 0x7F;
}

/* ──────────────────────────────────────────────
 * Main Loop: sleep → wake on SMBALERT# → ARA
 * ────────────────────────────────────────────── */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_I2C1_Init();
    Configure_SMBALERT_EXTI();

    while (1)
    {
        if (alert_received)
        {
            alert_received = 0;

            /* Identify which device raised the alert */
            alerting_device_addr = SMBus_ARA_Read();

            if (alerting_device_addr != 0xFF)
            {
                /* Now read the device's status/data register
                 * to clear its alert condition */
                uint8_t status_reg = 0;
                HAL_I2C_Mem_Read(&hi2c1,
                                 alerting_device_addr << 1,
                                 0x02,           /* Example: status register */
                                 I2C_MEMADD_SIZE_8BIT,
                                 &status_reg, 1,
                                 I2C_TIMEOUT_MS);

                /* Handle the alert condition based on device + status */
                handle_device_alert(alerting_device_addr, status_reg);
            }
        }

        /* Re-enter STOP1 mode — EXTI0 (SMBALERT#) can wake us */
        HAL_PWREx_EnterSTOP1Mode(PWR_STOPENTRY_WFI);
        SystemClock_Config();   /* Restore clocks after wake */
    }
}
```

---

### AVR/Arduino: INT Pin Wake from I2C Sensor

This example uses an MPU-6050 IMU (I2C) with its INT pin connected to the MCU's external interrupt to wake from power-down sleep.

```cpp
/* mpu6050_wake_arduino.ino
 * Wake Arduino Uno from power-down sleep using MPU-6050 motion interrupt.
 * INT pin of MPU-6050 → Arduino D2 (INT0)
 */

#include <Wire.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/interrupt.h>

/* MPU-6050 I2C address and key registers */
#define MPU6050_ADDR         0x68
#define REG_PWR_MGMT_1       0x6B
#define REG_INT_PIN_CFG      0x37
#define REG_INT_ENABLE       0x38
#define REG_INT_STATUS       0x3A
#define REG_MOT_THR          0x1F   /* Motion detection threshold */
#define REG_MOT_DUR          0x20   /* Motion detection duration  */
#define REG_MOT_DETECT_CTRL  0x69
#define INT_PIN              2      /* D2 = INT0 */

volatile bool motion_detected = false;

/* ──────────────────────────────────────────────
 * Low-level I2C helpers (avoid HAL overhead)
 * ────────────────────────────────────────────── */
void mpu_write(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t mpu_read(uint8_t reg)
{
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);          /* Repeated START */
    Wire.requestFrom(MPU6050_ADDR, 1);
    return Wire.available() ? Wire.read() : 0;
}

/* ──────────────────────────────────────────────
 * Configure MPU-6050 motion wake interrupt
 * Threshold: ~160 mg (value * 2 mg/LSB)
 * Duration:  ~20 ms (value * 1 ms)
 * ────────────────────────────────────────────── */
void mpu6050_configure_motion_wake(void)
{
    /* Wake MPU-6050 from sleep, use internal 8 MHz oscillator */
    mpu_write(REG_PWR_MGMT_1, 0x00);
    delay(100);

    /* INT pin: active-high, push-pull, pulse, cleared on any read */
    mpu_write(REG_INT_PIN_CFG, 0x20);

    /* Set motion threshold = 80 * 2 mg = 160 mg */
    mpu_write(REG_MOT_THR, 80);

    /* Set motion duration = 20 * 1 ms = 20 ms */
    mpu_write(REG_MOT_DUR, 20);

    /* Enable motion detection hardware unit */
    mpu_write(REG_MOT_DETECT_CTRL, 0x15);

    /* Enable only motion interrupt */
    mpu_write(REG_INT_ENABLE, 0x40);

    /* Clear any pending interrupt by reading status */
    mpu_read(REG_INT_STATUS);
}

/* ──────────────────────────────────────────────
 * External interrupt service routine (INT0)
 * ────────────────────────────────────────────── */
ISR(INT0_vect)
{
    motion_detected = true;
    /* Disable INT0 until we re-arm to avoid repeated wake */
    EIMSK &= ~(1 << INT0);
}

/* ──────────────────────────────────────────────
 * Enter AVR power-down sleep mode.
 * I2C (TWI) and other peripherals are halted.
 * Only external interrupts can wake the device.
 * ────────────────────────────────────────────── */
void enter_power_down(void)
{
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);

    /* Disable ADC to save power */
    power_adc_disable();

    /* Re-arm INT0 on rising edge (MPU-6050 INT is active-high) */
    EICRA |= (1 << ISC01) | (1 << ISC00);   /* Rising edge trigger */
    EIMSK |= (1 << INT0);                    /* Enable INT0 */

    sleep_enable();
    sei();              /* Interrupts must be enabled for wake-up */
    sleep_cpu();        /* MCU halts here until INT0 fires */

    /* ─── RESUMES HERE AFTER INT0 ─── */
    sleep_disable();
    power_adc_enable();
}

void setup(void)
{
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(400000);   /* 400 kHz I2C */

    pinMode(INT_PIN, INPUT);
    mpu6050_configure_motion_wake();

    Serial.println("MPU-6050 configured. Entering sleep...");
    delay(100);   /* Let UART flush before sleep */
}

void loop(void)
{
    enter_power_down();

    /* Woke up — handle motion event */
    if (motion_detected)
    {
        motion_detected = false;

        uint8_t int_status = mpu_read(REG_INT_STATUS);
        if (int_status & 0x40)
        {
            Serial.println("Motion detected! Processing data...");
            /* Read accelerometer data, log, transmit, etc. */
        }

        /* Clear interrupt and re-configure */
        mpu6050_configure_motion_wake();
    }
}
```

---

### Zephyr RTOS: I2C Slave Wake-Up

```c
/* zephyr_i2c_slave_wake.c
 * Zephyr RTOS: I2C slave wake-up using device tree + pm_device API
 * Assumes board with PM and I2C slave support (e.g. nRF52840 DK)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_wake, LOG_LEVEL_INF);

#define MY_I2C_SLAVE_ADDR   0x42

static const struct device *i2c_dev = DEVICE_DT_GET(DT_ALIAS(i2c0));

static uint8_t tx_buf[4] = {0xCA, 0xFE, 0xBA, 0xBE};
static uint8_t rx_buf[16];
static K_SEM_DEFINE(wake_sem, 0, 1);

/* ──────────────────────────────────────────────
 * I2C slave callbacks (called from ISR)
 * ────────────────────────────────────────────── */
static int slave_write_requested(struct i2c_slave_config *config)
{
    /* Master wants to write — prepare receive buffer */
    return 0;
}

static int slave_read_requested(struct i2c_slave_config *config,
                                uint8_t *val)
{
    /* Master wants to read — return first byte */
    *val = tx_buf[0];
    return 0;
}

static int slave_write_received(struct i2c_slave_config *config,
                                uint8_t val)
{
    static uint8_t idx = 0;
    rx_buf[idx++ % sizeof(rx_buf)] = val;
    return 0;
}

static int slave_read_processed(struct i2c_slave_config *config,
                                uint8_t *val)
{
    /* Master clocked out a byte — provide next */
    static uint8_t idx = 1;
    *val = tx_buf[idx++ % sizeof(tx_buf)];
    return 0;
}

static int slave_stop(struct i2c_slave_config *config)
{
    /* Transfer complete — signal main thread */
    k_sem_give(&wake_sem);
    return 0;
}

static struct i2c_slave_callbacks slave_cb = {
    .write_requested = slave_write_requested,
    .read_requested  = slave_read_requested,
    .write_received  = slave_write_received,
    .read_processed  = slave_read_processed,
    .stop            = slave_stop,
};

static struct i2c_slave_config slave_cfg = {
    .address  = MY_I2C_SLAVE_ADDR,
    .callbacks = &slave_cb,
};

/* ──────────────────────────────────────────────
 * Main thread: register slave, then sleep/wake loop
 * ────────────────────────────────────────────── */
int main(void)
{
    if (!device_is_ready(i2c_dev))
    {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    /* Register as I2C slave */
    int ret = i2c_slave_register(i2c_dev, &slave_cfg);
    if (ret < 0)
    {
        LOG_ERR("Failed to register I2C slave: %d", ret);
        return ret;
    }

    LOG_INF("I2C slave registered at 0x%02X. Waiting for master...",
            MY_I2C_SLAVE_ADDR);

    while (1)
    {
        /* Suspend CPU — Zephyr PM will enter the lowest
         * feasible power state (configured in prj.conf).
         * The I2C peripheral's address match logic keeps
         * monitoring the bus and will raise an interrupt
         * when addressed, which wakes the kernel. */
        pm_state_force(0, &(struct pm_state_info){
            .state = PM_STATE_SUSPEND_TO_IDLE,
        });

        /* Block until I2C transaction completes */
        k_sem_take(&wake_sem, K_FOREVER);

        LOG_INF("Woke via I2C! Received: %02X %02X %02X %02X",
                rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);

        /* Do application work */
        process_i2c_command(rx_buf);
    }

    return 0;
}
```

**Zephyr `prj.conf` for power management:**

```ini
# prj.conf — I2C slave wake-up power configuration
CONFIG_I2C=y
CONFIG_I2C_SLAVE=y
CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_PM_DEVICE_RUNTIME=y
CONFIG_LOG=y
CONFIG_SERIAL=n          # Disable UART to reduce power
```

---

## Rust Implementation Examples

### embassy-stm32: I2C Address Match Wake-Up

```rust
// embassy_i2c_wake.rs
// Embassy async I2C slave on STM32L4 with address-match wake-up.
// Cargo.toml: embassy-stm32, embassy-executor, embassy-sync

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::{
    bind_interrupts,
    i2c::{self, I2c, SlaveAddrMatch},
    peripherals,
    rcc::{self, LseConfig, LseOscillatorDrive},
    time::Hertz,
    Config,
};
use embassy_stm32::low_power::{executor::Executor, stop_with_rtc};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;
use defmt::*;
use {defmt_rtt as _, panic_probe as _};

bind_interrupts!(struct Irqs {
    I2C1_EV => i2c::EventInterruptHandler<peripherals::I2C1>;
    I2C1_ER => i2c::ErrorInterruptHandler<peripherals::I2C1>;
});

const SLAVE_ADDRESS: u8 = 0x42;

/// Signal fired from I2C callback when a transfer completes
static I2C_WAKE: Signal<CriticalSectionRawMutex, [u8; 8]> = Signal::new();

/// I2C slave task — runs the slave receive/transmit loop
#[embassy_executor::task]
async fn i2c_slave_task(
    mut i2c: I2c<'static, peripherals::I2C1>,
) {
    let mut rx_buf = [0u8; 8];
    let tx_buf = [0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03];

    loop {
        // Wait for our slave address to appear on the I2C bus.
        // With WUPEN enabled in embassy-stm32, the MCU enters
        // STOP mode between calls and is woken by address match.
        match i2c.slave_transaction(&mut rx_buf, &tx_buf).await {
            Ok(SlaveAddrMatch::Write(n)) => {
                info!("I2C write: {} bytes received", n);
                I2C_WAKE.signal(rx_buf);
            }
            Ok(SlaveAddrMatch::Read) => {
                info!("I2C read: {} bytes sent", tx_buf.len());
            }
            Err(e) => {
                warn!("I2C error: {:?}", e);
            }
        }
    }
}

/// Main application task — processes wake events
#[embassy_executor::task]
async fn app_task() {
    loop {
        // Block until an I2C transaction wakes us
        let data = I2C_WAKE.wait().await;
        info!("Woke from I2C! Data: {:02X}", data);

        // Process command, update state, etc.
        process_command(&data);
    }
}

fn process_command(data: &[u8; 8]) {
    // Application logic here
    match data[0] {
        0x01 => info!("CMD: Read sensor"),
        0x02 => info!("CMD: Update config"),
        _    => info!("CMD: Unknown 0x{:02X}", data[0]),
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    // Configure STM32L4 with LSE enabled for low-power I2C operation
    let mut config = Config::default();
    config.rcc.lse = Some(LseConfig {
        frequency: Hertz(32_768),
        drive: LseOscillatorDrive::MediumLow,
    });
    // Route LSE to I2C1 kernel clock for STOP-mode wake capability
    config.rcc.mux.i2c1sel = rcc::mux::I2c1sel::LSE;

    let p = embassy_stm32::init(config);

    // Initialize I2C1 as slave with wake-up enabled
    let i2c_config = i2c::Config {
        slave_address: Some(SLAVE_ADDRESS),
        wakeup_from_stop: true,   // Sets I2C_CR1.WUPEN
        ..Default::default()
    };

    let i2c = I2c::new(
        p.I2C1,
        p.PB8,   // SCL
        p.PB9,   // SDA
        Irqs,
        p.DMA1_CH6,
        p.DMA1_CH7,
        Hertz(100_000),
        i2c_config,
    );

    spawner.spawn(i2c_slave_task(i2c)).unwrap();
    spawner.spawn(app_task()).unwrap();
}
```

---

### RTIC + embedded-hal: Alert Pin Wake-Up

```rust
// rtic_smbalert_wake.rs
// RTIC v2 app on nRF52840: SMBALERT# on P0.13 wakes from SystemOFF via GPIOTE.
// After wake, performs I2C ARA to identify the alerting device.

#![no_std]
#![no_main]

use rtic::app;
use nrf52840_hal::{
    self as hal,
    gpiote::Gpiote,
    pac,
    ppi::Ppi,
    twim::{Frequency, Pins, Twim},
};
use cortex_m::asm;

const ARA_ADDR: u8     = 0x0C;   // SMBus Alert Response Address
const I2C_FREQ: Frequency = Frequency::K100;

pub struct AlertEvent {
    pub device_addr: u8,
    pub status:      u8,
}

#[app(device = nrf52840_hal::pac, peripherals = true, dispatchers = [SWI0_EGU0])]
mod app {
    use super::*;

    #[shared]
    struct Shared {
        twim: Twim<pac::TWIM0>,
    }

    #[local]
    struct Local {
        gpiote: Gpiote,
    }

    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        let dp = cx.device;

        // Configure P0.13 (SMBALERT#) as input with pull-up
        let port0 = hal::gpio::p0::Parts::new(dp.P0);
        let alert_pin = port0.p0_13
            .into_pullup_input()
            .degrade();

        // GPIOTE channel 0: fire event on falling edge of SMBALERT#
        let gpiote = Gpiote::new(dp.GPIOTE);
        gpiote
            .channel0()
            .input_pin(&alert_pin)
            .lo_to_hi()     // Actually hi_to_lo for active-low
            .enable_interrupt();

        // Initialize I2C master on nRF52840
        let scl = port0.p0_27.into_floating_input().degrade();
        let sda = port0.p0_26.into_floating_input().degrade();
        let twim = Twim::new(
            dp.TWIM0,
            Pins { scl, sda },
            I2C_FREQ,
        );

        (
            Shared { twim },
            Local  { gpiote },
        )
    }

    /// GPIOTE interrupt — SMBALERT# falling edge
    #[task(binds = GPIOTE, local = [gpiote], shared = [twim])]
    fn gpiote_handler(mut cx: gpiote_handler::Context) {
        cx.local.gpiote.channel0().reset_events();

        // Perform ARA to find out which device raised alert
        cx.shared.twim.lock(|twim| {
            let mut response = [0u8; 1];

            // ARA read: master sends START + (0x0C << 1 | 1)
            if twim.read(ARA_ADDR, &mut response).is_ok() {
                let device_addr = (response[0] >> 1) & 0x7F;

                // Read device's status register to clear alert
                let mut status = [0u8; 1];
                let status_reg = [0x02u8]; // Status register address
                let _ = twim.write_then_read(device_addr, &status_reg, &mut status);

                // Dispatch alert handling as async software task
                handle_alert::spawn(AlertEvent {
                    device_addr,
                    status: status[0],
                }).ok();
            }
        });
    }

    /// Software task: handle the alert event (runs in thread mode)
    #[task(shared = [twim], priority = 1)]
    async fn handle_alert(_cx: handle_alert::Context, event: AlertEvent) {
        defmt::info!(
            "Alert from device 0x{:02X}, status=0x{:02X}",
            event.device_addr,
            event.status
        );

        // Application-specific handling
        match event.device_addr {
            0x48 => { /* TMP117 temperature alert — read temperature */ }
            0x4A => { /* Another sensor alert */ }
            _    => { defmt::warn!("Unknown alerting device") }
        }
    }

    /// Idle task: enter WFE (wait-for-event) low power state
    #[idle]
    fn idle(_: idle::Context) -> ! {
        loop {
            // cortex-m WFE: CPU halts, wakes on any NVIC-pending event.
            // For deeper sleep (System ON sleep in nRF52840),
            // use nrf52840_hal::pwr::SystemOnSleepMode.
            asm::wfe();
        }
    }
}
```

---

### nrf52840: I2C-Triggered Wake from System OFF

```rust
// nrf52840_system_off_wake.rs
// Demonstrate waking nRF52840 from System OFF using a GPIOTE-latched
// I2C sensor INT pin. System OFF has ~0.4 µA current draw.
// The device resets on wake — this shows pre-sleep setup and
// post-wake detection.

#![no_std]
#![no_main]

use nrf52840_hal::{
    self as hal,
    gpio::{Input, Pin, PullUp, p0},
    pac,
    pwr::PowerExt,
    twim::{Frequency, Pins, Twim},
};
use hal::prelude::*;

// Sensor register map (example: LIS2DW12 accelerometer)
const SENSOR_ADDR:      u8 = 0x19;
const REG_CTRL4:        u8 = 0x23;   // INT1 pin control
const REG_CTRL5:        u8 = 0x24;   // INT2 pin control  
const REG_CTRL7:        u8 = 0x3F;   // Interrupt enable
const REG_WAKE_UP_THS:  u8 = 0x34;   // Wake-up threshold
const REG_WAKE_UP_DUR:  u8 = 0x35;   // Wake-up duration
const REG_ALL_INT_SRC:  u8 = 0x3B;   // Interrupt source (clear)

#[cortex_m_rt::entry]
fn main() -> ! {
    let dp  = pac::Peripherals::take().unwrap();
    let p0  = p0::Parts::new(dp.P0);

    // INT1 of sensor connected to P0.11
    let int_pin: Pin<Input<PullUp>> = p0.p0_11
        .into_pullup_input()
        .degrade();

    // Check if we woke from System OFF via DETECT signal
    // nRF52840: RESETREAS register bit 16 = GPIO wake
    let reset_reason = dp.POWER.resetreas.read();
    let woke_from_system_off = reset_reason.off().bit_is_set();

    // Initialize I2C
    let scl = p0.p0_27.into_floating_input().degrade();
    let sda = p0.p0_26.into_floating_input().degrade();
    let mut i2c = Twim::new(dp.TWIM0, Pins { scl, sda }, Frequency::K100);

    if woke_from_system_off {
        // Clear reset reason register
        dp.POWER.resetreas.write(|w| w.off().set_bit());

        // Read sensor interrupt source to clear the INT pin
        let src_reg = [REG_ALL_INT_SRC];
        let mut int_src = [0u8; 1];
        i2c.write_then_read(SENSOR_ADDR, &src_reg, &mut int_src)
            .expect("Failed to clear interrupt source");

        defmt::info!("Woke from System OFF! INT source: 0x{:02X}", int_src[0]);

        // Process wake event (transmit data, update state, etc.)
        handle_wake_event(&mut i2c, int_src[0]);
    }
    else {
        defmt::info!("Cold boot — configuring sensor wake-up interrupt");
    }

    // (Re-)configure the sensor motion wake-up interrupt
    configure_sensor_wakeup(&mut i2c);

    // Configure GPIOTE to sense INT1 pin for System OFF wake
    // In System OFF, only GPIOTE DETECT can wake the device.
    let gpiote = dp.GPIOTE;
    gpiote.config[0].write(|w| unsafe {
        w.mode().event()            // Generate event on pin change
         .psel().bits(11)           // P0.11
         .polarity().lo_to_hi()     // Rising edge (INT1 is active-high)
         .outinit().low()
    });
    gpiote.intenset.write(|w| w.in0().set());

    // Enable DETECT signal to wake from System OFF
    dp.P0.detectmode.write(|w| w.detectmode().ldetect());

    defmt::info!("Entering System OFF (~0.4 µA). Sensor will wake on motion.");

    // Enter System OFF — the device is effectively dead until
    // the DETECT signal fires from the sensor INT1 pin.
    // After wake, the MCU performs a full reset and re-enters main().
    dp.POWER.systemoff.write(|w| w.systemoff().set_bit());

    // Unreachable: System OFF is a one-way trip until reset
    loop { cortex_m::asm::wfe(); }
}

fn configure_sensor_wakeup(i2c: &mut Twim<pac::TWIM0>) {
    // Enable accelerometer, 12.5 Hz ODR, low-power mode 1
    i2c.write(SENSOR_ADDR, &[0x20, 0x10]).ok();   // CTRL1

    // Route wake-up interrupt to INT1 pin
    i2c.write(SENSOR_ADDR, &[REG_CTRL4, 0x20]).ok();

    // Wake-up threshold: ~250 mg (value * 1/64 of full-scale)
    i2c.write(SENSOR_ADDR, &[REG_WAKE_UP_THS, 0x10]).ok();

    // Wake-up duration: 2 samples
    i2c.write(SENSOR_ADDR, &[REG_WAKE_UP_DUR, 0x02]).ok();

    // Enable wake-up interrupt generation
    i2c.write(SENSOR_ADDR, &[REG_CTRL7, 0x20]).ok();
}

fn handle_wake_event(i2c: &mut Twim<pac::TWIM0>, int_src: u8) {
    if int_src & 0x20 != 0 {
        defmt::info!("Wake-up event (motion) detected");
    }
    // Additional processing: read acceleration data, transmit, etc.
}
```

---

## Power Consumption Analysis

Understanding the power budget is essential for designing effective wake-up systems.

| Configuration | Typical Current | Wake Latency |
|---|---|---|
| MCU active @ 80 MHz | 10–20 mA | — |
| STOP1 (STM32L4, LSE on) | ~5–15 µA | ~5 µs |
| STOP2 (STM32L4, RAM retained) | ~1–5 µA | ~50–300 µs |
| Standby (SRAM2 off) | ~0.3–1 µA | ~300 µs |
| System OFF (nRF52840) | ~0.4 µA | full reset (ms) |
| I2C peripheral (WUPEN, LSE) | +0.5–2 µA overhead | address match |
| Sensor (motion detect, low ODR) | 1–5 µA | INT latency |

**Key insight:** With a sensor drawing 3 µA and an MCU in STOP2 at 2 µA, the total system draws only ~5 µA between events — orders of magnitude less than the 10–20 mA of active operation.

### Duty-Cycle Example

A motion-triggered sensor node that wakes for 10 ms every 60 seconds:

```
Active time fraction = 10 ms / 60,000 ms = 0.017%
Average current = (15 mA × 0.017%) + (5 µA × 99.983%)
               ≈ 2.5 µA + 5 µA
               ≈ 7.5 µA average
```

On a 2000 mAh battery: runtime ≈ 2000 mAh / 0.0075 mA ≈ **30 years** (ignoring self-discharge).

---

## Common Pitfalls and Troubleshooting

**Pull-ups powered down during sleep**
If pull-up resistors are connected to a switched rail that turns off in sleep, SCL/SDA float, making wake-up via address match impossible. Always connect pull-ups to an always-on supply.

**Clock not configured before sleep entry**
For I2C address-match wake-up, the peripheral must be clocked from LSE or LSI, which must be started and stable before entering STOP mode. Verify LSE status flags before sleep.

**Forgetting to re-enable listen mode**
On HAL-based STM32 code, `HAL_I2C_EnableListen_IT()` must be called after each completed transaction (including in error callbacks), or the peripheral stops listening.

**Clock restoration after wake-up**
When waking from STOP/STOP2, the system clock reverts to the default (MSI on STM32). Your application must call `SystemClock_Config()` (or equivalent) to restore the desired clock frequency before performing any time-sensitive operations.

**Debouncing SMBALERT# in hardware**
If the SMBALERT# line is susceptible to noise, add a 100 nF capacitor from SMBALERT# to GND. Software debouncing in an ISR is unreliable for async wake sources.

**ARA timing**
After waking from deep sleep, allow time for clocks and the I2C peripheral to stabilize before issuing the ARA transaction. A delay of ~100–500 µs may be necessary depending on the MCU.

**Rust borrow checker and shared peripheral access**
In RTIC, I2C peripherals shared between tasks must be wrapped in `#[shared]` resources with proper locking. Attempting direct concurrent access will fail at compile time — this is a feature, not a bug.

**System OFF wake on nRF52840 is a full reset**
Code must detect the wake cause via `RESETREAS` on every cold boot, not just the first one. Design the firmware as a state machine that checks reset reason at startup.

---

## Summary

I2C wake-up mechanisms enable embedded systems to achieve ultra-low power consumption by keeping the MCU in deep sleep and using peripheral events to trigger selective wake-up. The three primary approaches are:

**1. Dedicated Interrupt/Alert Pin (Simplest)**
A sensor or PMIC asserts a GPIO line (often open-drain, shared via SMBALERT#), which fires an EXTI interrupt to wake the MCU. I2C communication occurs entirely *after* wake-up. This approach works with virtually any MCU and any I2C device that has an INT pin.

**2. I2C Address Match Wake-Up (Most Elegant for Slave MCUs)**
The I2C peripheral's address comparator runs from a low-power clock (LSE/LSI) while the CPU is halted. When a master initiates a transfer to the slave MCU's address, the peripheral wakes the CPU before the transaction is complete. This enables the MCU to act as a smart I2C slave that is effectively asleep but immediately responsive.

**3. General Call / SMBus Alert Response Address**
General Call (0x00) wakes all slaves simultaneously. The ARA cycle (0x0C read) identifies a specific alerting device on a shared SMBALERT# bus, allowing multiple sensors to share one interrupt wire.

In C/C++, STM32 HAL's `I2C_CR1.WUPEN` bit and `HAL_I2C_EnableListen_IT()` are the key APIs for slave address-match wake-up. In Rust, Embassy provides async-first I2C slave APIs with power management integration, while RTIC enables interrupt-driven, zero-overhead peripheral sharing with compile-time race-condition prevention.

Careful attention to pull-up power domains, clock restoration post-wake, and peripheral re-arming is essential for reliable operation. When implemented correctly, I2C-triggered wake-up enables battery lifetimes measured in years rather than days.

---

*Document: 78_Wake_Up_Mechanisms.md | I2C Topics Series*