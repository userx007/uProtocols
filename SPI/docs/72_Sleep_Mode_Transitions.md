# 72. Sleep Mode Transitions — SPI Peripheral State During MCU Sleep & Wake Cycles

**Conceptual coverage** — sleep mode taxonomy (WFI through Shutdown), why SPI state matters, clock-gating vs. power-off, register retention, DMA interactions, GPIO electrical state, and external device power management.

**Two operational checklists** — a 10-step sleep-entry sequence and a 10-step wake-restore sequence, usable directly as a review checklist in firmware review.

**C/C++ code examples (4 variants):**
- STM32 HAL — full `SPI_PrepareForSleep()` / `SPI_RestoreFromSleep()` with DMA abort, BSY polling, register backup, GPIO reconfiguration, and W25Q128 flash power-down commands
- nRF52 SDK — `nrfx_spim_uninit()` pattern with the power management handler registered via `NRF_PWR_MGMT_HANDLER_REGISTER`
- ESP-IDF — light-sleep (gpio hold) vs. deep-sleep (full de-init) variants
- Generic bare-metal — portable ARM Cortex-M pattern with no vendor HAL dependency

**Rust code examples (3 variants):**
- Embassy async — `SpiSleepGuard` ownership pattern using drop semantics
- RTIC v2 — idle-task suspend with register save/restore
- nrf-hal — idiomatic drop-to-disable / reconstruct-on-wake pattern

**Reference material** — timing diagrams for sleep entry and wake sequences, a 10-row pitfalls table, and a platform quick-reference table covering STM32, nRF52, ESP32, RP2040, and SAMD21.

---

## Table of Contents

1. [Overview](#overview)
2. [Why SPI State Management During Sleep Matters](#why-spi-state-management-during-sleep-matters)
3. [MCU Sleep Mode Taxonomy](#mcu-sleep-mode-taxonomy)
4. [SPI Peripheral State Considerations](#spi-peripheral-state-considerations)
   - 4.1 [Clock Gating vs. Power-Off](#clock-gating-vs-power-off)
   - 4.2 [Register Retention](#register-retention)
   - 4.3 [DMA and Interrupt Interactions](#dma-and-interrupt-interactions)
   - 4.4 [GPIO / Pin State](#gpio--pin-state)
   - 4.5 [External Device State](#external-device-state)
5. [Sleep Entry Checklist for SPI](#sleep-entry-checklist-for-spi)
6. [Wake-Up Checklist for SPI](#wake-up-checklist-for-spi)
7. [Code Examples — C/C++](#code-examples--cc)
   - 7.1 [STM32 HAL — SPI Suspend / Resume Around STOP Mode](#71-stm32-hal--spi-suspend--resume-around-stop-mode)
   - 7.2 [nRF52 SDK — SPI Before Deep Sleep](#72-nrf52-sdk--spi-before-deep-sleep)
   - 7.3 [ESP-IDF — SPI Bus De-Init Before Light Sleep](#73-esp-idf--spi-bus-de-init-before-light-sleep)
   - 7.4 [Generic Bare-Metal Pattern (ARM Cortex-M)](#74-generic-bare-metal-pattern-arm-cortex-m)
8. [Code Examples — Rust](#code-examples--rust)
   - 8.1 [Embassy (STM32) — Async SPI with Sleep Token](#81-embassy-stm32--async-spi-with-sleep-token)
   - 8.2 [RTIC + STM32 — Resource Suspend in Idle](#82-rtic--stm32--resource-suspend-in-idle)
   - 8.3 [nRF52 in Rust (nrf-hal) — SPI Drop Before WFI](#83-nrf52-in-rust-nrf-hal--spi-drop-before-wfi)
9. [Common Pitfalls and Anti-Patterns](#common-pitfalls-and-anti-patterns)
10. [Timing Diagrams](#timing-diagrams)
11. [Platform Quick-Reference Table](#platform-quick-reference-table)
12. [Summary](#summary)

---

## Overview

Modern microcontrollers (MCUs) expose a hierarchy of low-power sleep modes — from simple clock-halting (WFI/WFE) to deep-sleep states where most peripheral power domains are cut entirely. The Serial Peripheral Interface (SPI) bus, as a synchronous full-duplex peripheral, has intricate state that must be carefully managed before the MCU enters sleep and correctly restored upon wake-up. Failure to do so leads to:

- Corrupted in-flight transactions
- Mismatched CS# (chip-select) lines leaving external devices in undefined states
- Register loss in deep-power-off modes, requiring full peripheral re-initialisation
- Spurious interrupts on wake-up from MISO/MOSI line noise
- Increased average current due to leakage through un-tristated GPIO pins

This document provides a thorough treatment of the problem domain, platform-specific considerations, and production-ready code patterns in both **C/C++** and **Rust**.

---

## Why SPI State Management During Sleep Matters

```
 Normal Operation          Sleep Entry            Deep Sleep          Wake-Up
 ─────────────────        ─────────────          ───────────         ─────────────
 CS# ─┐   ┌──────        CS# ─────────          CS# ─────────       CS# ─┐  ┌────
      └───┘               (must be HIGH)         (must be HIGH)          └──┘
 CLK ─┐┌┐┌┐┌──           CLK ─────────          CLK ─────────       CLK ─┐┌┐┌┐┌──
      └┘└┘└┘             (must be idle)         (clock gated)           └┘└┘└┘
 MOSI  data...           MOSI─────────          MOSI─────────       MOSI  data...
 MISO  data...           MISO─────────          MISO─────────       MISO  data...
```

Any unfinished transaction at sleep entry leaves `CS#` asserted (low), causing the connected peripheral to remain powered and responsive — drawing several milliamps from a rail you're trying to idle. The goal of sleep-mode SPI management is deterministic quiescence before sleep and deterministic readiness after wake.

---

## MCU Sleep Mode Taxonomy

| Mode | Typical Name | CPU | Peripherals | RAM | Registers | Wake Source |
|------|-------------|-----|-------------|-----|-----------|-------------|
| 0 | Sleep / WFI | Halted | Running | Retained | Retained | Any interrupt |
| 1 | Stop / Light Sleep | Halted | Clock-gated | Retained | Retained | GPIO, RTC, LPUART |
| 2 | Deep Sleep / STOP2 | Halted | Power-gated | Retained | **Lost** | GPIO, RTC |
| 3 | Standby / Hibernate | Off | Off | **Lost** | **Lost** | WKUP pin, RTC |
| 4 | Shutdown | Off | Off | **Lost** | **Lost** | WKUP pin only |

> **Key insight:** Modes 0–1 often require only *quiescing* the SPI transaction. Modes 2–4 require full *re-initialisation* on wake-up.

---

## SPI Peripheral State Considerations

### 4.1 Clock Gating vs. Power-Off

In light-sleep modes, the APB/AHB bus clock to the SPI peripheral is gated. The peripheral's configuration registers are preserved in flip-flops, so on wake-up you merely re-enable the clock and the peripheral resumes with the same configuration (baud rate, CPOL/CPHA, data size, etc.).

In deep-sleep/standby modes, the entire power domain containing the SPI peripheral is removed. All register state is lost. Re-initialisation (equivalent to calling `SPI_Init()` from scratch) is mandatory.

### 4.2 Register Retention

Registers to check on resume after deep sleep:

| Register | Content | Action on Deep-Sleep Resume |
|----------|---------|----------------------------|
| CR1/CR2 (STM32) | Mode, baud, CPOL, CPHA, SSM | Re-write |
| DR (data register) | Last byte sent/received | Discard / re-flush |
| SR (status) | BSY, OVR, MODF flags | Read-clear before use |
| DMA channel config | Peripheral address, buffer, length | Re-configure |
| GPIO alternate function | MOSI/MISO/CLK pin mux | Re-configure if power domain lost |

### 4.3 DMA and Interrupt Interactions

DMA transfers must complete (or be aborted) before entering sleep, otherwise:
- The DMA controller may still hold the bus.
- On deep sleep, DMA registers are lost mid-transfer causing data corruption.
- Incomplete transfers can leave `CS#` asserted.

Pattern: **poll for BSY=0, abort DMA, disable DMA channel, then proceed to sleep**.

### 4.4 GPIO / Pin State

During deep sleep, GPIO configuration may be reset to the default (input floating) depending on the platform. Floating `MOSI`, `CLK` lines can capacitively couple to adjacent traces and inject noise. Best practice:

- Configure `MOSI`, `CLK` as **output low** or **input pull-down** before deep sleep.
- Configure `CS#` as **output high** (deasserted).
- `MISO` should be **input pull-down** (prevents device from sinking current through a high-Z line).

### 4.5 External Device State

External SPI devices (flash, sensors, displays) have their own power-management requirements:

- **NOR Flash** (e.g., W25Q128): issue a `Power-Down` command (0xB9) before deep sleep; issue `Release Power-Down` (0xAB) on wake-up.
- **OLED displays** (SSD1306): issue `Display OFF` command, then power off the VDDIO rail.
- **IMU sensors** (ICM-42688): put into `SLEEP` or `ACCEL-LP` mode via SPI register write.

The MCU SPI peripheral must be operational long enough to issue these device sleep commands, then be quiesced itself.

---

## Sleep Entry Checklist for SPI

```
1.  [ ] Complete or abort any in-flight DMA/interrupt-driven transfer
2.  [ ] Wait for SPI BSY flag to clear (hardware SR.BSY = 0)
3.  [ ] Flush RX FIFO (read DR until RXNE = 0)
4.  [ ] Issue sleep command to all connected SPI peripheral devices
5.  [ ] Deassert all CS# lines (set HIGH)
6.  [ ] Disable SPI peripheral (SPE = 0 on STM32)
7.  [ ] Disable DMA channels associated with SPI
8.  [ ] Disable SPI interrupt in NVIC (optional for modes 0–1)
9.  [ ] For deep sleep: configure MOSI, CLK as output-low; MISO as input-pull-down
10. [ ] For deep sleep: disable SPI peripheral clock (RCC clock gate)
```

## Wake-Up Checklist for SPI

```
1.  [ ] Restore SPI peripheral clock (RCC enable)
2.  [ ] For deep sleep: re-initialise GPIO alternate functions
3.  [ ] For deep sleep: re-initialise SPI registers (CR1, CR2, baud, mode)
4.  [ ] Re-configure DMA channels if needed
5.  [ ] Re-enable NVIC SPI interrupt if used
6.  [ ] Issue wake command to SPI peripheral devices (e.g., Release Power-Down)
7.  [ ] Allow tRES recovery time for external devices
8.  [ ] Assert / deassert CS# as required by the first transaction
9.  [ ] Enable SPI peripheral (SPE = 1)
10. [ ] Resume normal operation
```

---

## Code Examples — C/C++

### 7.1 STM32 HAL — SPI Suspend / Resume Around STOP Mode

```c
/* spi_sleep.h */
#ifndef SPI_SLEEP_H
#define SPI_SLEEP_H

#include "stm32l4xx_hal.h"

typedef struct {
    uint32_t CR1;
    uint32_t CR2;
} SPI_RegBackup_t;

HAL_StatusTypeDef SPI_PrepareForSleep(SPI_HandleTypeDef *hspi,
                                       SPI_RegBackup_t   *backup,
                                       bool               deep_sleep);
HAL_StatusTypeDef SPI_RestoreFromSleep(SPI_HandleTypeDef *hspi,
                                        const SPI_RegBackup_t *backup,
                                        bool               deep_sleep);
#endif /* SPI_SLEEP_H */
```

```c
/* spi_sleep.c */
#include "spi_sleep.h"
#include "main.h"   /* for CS_GPIO_Port, CS_Pin */

#define SPI_TIMEOUT_MS      100U
#define FLASH_CMD_POWERDOWN 0xB9U
#define FLASH_CMD_RELEASE   0xABU
#define FLASH_TRES_US       3U    /* tRES1 = 3 µs for W25Q128 */

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void flash_send_cmd(SPI_HandleTypeDef *hspi, uint8_t cmd)
{
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(hspi, &cmd, 1, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
}

static void spi_gpio_sleep_config(void)
{
    GPIO_InitTypeDef g = {0};

    /* MOSI and CLK → output low to prevent floating */
    g.Pin   = SPI1_MOSI_PIN | SPI1_CLK_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SPI1_GPIO_PORT, &g);
    HAL_GPIO_WritePin(SPI1_GPIO_PORT, SPI1_MOSI_PIN | SPI1_CLK_PIN, GPIO_PIN_RESET);

    /* MISO → input with pull-down */
    g.Pin  = SPI1_MISO_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(SPI1_GPIO_PORT, &g);
}

static void spi_gpio_restore_config(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin       = SPI1_MOSI_PIN | SPI1_MISO_PIN | SPI1_CLK_PIN;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(SPI1_GPIO_PORT, &g);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief  Prepare SPI and attached flash for MCU sleep entry.
 * @param  hspi       HAL SPI handle
 * @param  backup     caller-supplied register backup struct (for deep sleep)
 * @param  deep_sleep true = STOP2/Standby (registers lost); false = STOP1
 * @retval HAL_OK on success
 */
HAL_StatusTypeDef SPI_PrepareForSleep(SPI_HandleTypeDef *hspi,
                                       SPI_RegBackup_t   *backup,
                                       bool               deep_sleep)
{
    uint32_t tickstart = HAL_GetTick();

    /* 1. Abort any active DMA transfer */
    if (hspi->hdmatx && (hspi->hdmatx->State == HAL_DMA_STATE_BUSY)) {
        HAL_DMA_Abort(hspi->hdmatx);
    }
    if (hspi->hdmarx && (hspi->hdmarx->State == HAL_DMA_STATE_BUSY)) {
        HAL_DMA_Abort(hspi->hdmarx);
    }

    /* 2. Wait for BSY flag to clear */
    while (__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_BSY)) {
        if ((HAL_GetTick() - tickstart) > SPI_TIMEOUT_MS) {
            return HAL_TIMEOUT;
        }
    }

    /* 3. Flush RX FIFO */
    while (__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_RXNE)) {
        (void)hspi->Instance->DR;   /* read & discard */
    }

    /* 4. Issue Power-Down command to external SPI Flash */
    flash_send_cmd(hspi, FLASH_CMD_POWERDOWN);

    /* 5. CS# already HIGH after flash_send_cmd — confirm */
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);

    /* 6. Disable SPI */
    __HAL_SPI_DISABLE(hspi);

    if (deep_sleep) {
        /* 7. Back up configuration registers */
        backup->CR1 = hspi->Instance->CR1;
        backup->CR2 = hspi->Instance->CR2;

        /* 8. Reconfigure GPIO for minimal leakage */
        spi_gpio_sleep_config();

        /* 9. Gate SPI peripheral clock */
        __HAL_RCC_SPI1_CLK_DISABLE();
    }

    return HAL_OK;
}

/**
 * @brief  Restore SPI and attached flash after MCU wake-up.
 * @param  hspi       HAL SPI handle
 * @param  backup     register backup from SPI_PrepareForSleep()
 * @param  deep_sleep must match value passed to SPI_PrepareForSleep()
 * @retval HAL_OK on success
 */
HAL_StatusTypeDef SPI_RestoreFromSleep(SPI_HandleTypeDef    *hspi,
                                        const SPI_RegBackup_t *backup,
                                        bool                   deep_sleep)
{
    if (deep_sleep) {
        /* 1. Re-enable SPI peripheral clock */
        __HAL_RCC_SPI1_CLK_ENABLE();

        /* 2. Restore GPIO alternate-function configuration */
        spi_gpio_restore_config();

        /* 3. Restore SPI registers */
        hspi->Instance->CR1 = backup->CR1 & ~SPI_CR1_SPE; /* SPE=0 during config */
        hspi->Instance->CR2 = backup->CR2;
    }

    /* 4. Re-enable SPI */
    __HAL_SPI_ENABLE(hspi);

    /* 5. Release Flash from power-down (tRES1 guard) */
    flash_send_cmd(hspi, FLASH_CMD_RELEASE);
    /* tRES1 delay — 3 µs minimum */
    uint32_t t = DWT->CYCCNT;
    uint32_t cycles = (SystemCoreClock / 1000000U) * FLASH_TRES_US;
    while ((DWT->CYCCNT - t) < cycles) { __NOP(); }

    return HAL_OK;
}
```

```c
/* main.c — usage in the sleep / wake loop */
#include "spi_sleep.h"

extern SPI_HandleTypeDef hspi1;
static SPI_RegBackup_t spi_backup;

void enter_stop2_mode(void)
{
    if (SPI_PrepareForSleep(&hspi1, &spi_backup, true) != HAL_OK) {
        Error_Handler();
    }

    /* Configure wake-up source — e.g. RTC alarm */
    HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1);
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* ---- MCU is now asleep ---- */
    /* Execution resumes here after wake-up */

    /* Restore system clock after STOP2 */
    SystemClock_Config();

    if (SPI_RestoreFromSleep(&hspi1, &spi_backup, true) != HAL_OK) {
        Error_Handler();
    }
}
```

---

### 7.2 nRF52 SDK — SPI Before Deep Sleep

```c
/* nrf52_spi_sleep.c — using nRF5 SDK nrfx_spim */
#include "nrfx_spim.h"
#include "nrf_gpio.h"
#include "nrf_pwr_mgmt.h"

#define SPI_INSTANCE   0
#define CS_PIN         NRF_GPIO_PIN_MAP(0, 6)
#define SCK_PIN        NRF_GPIO_PIN_MAP(0, 27)
#define MOSI_PIN       NRF_GPIO_PIN_MAP(0, 26)
#define MISO_PIN       NRF_GPIO_PIN_MAP(0, 2)

static nrfx_spim_t m_spi = NRFX_SPIM_INSTANCE(SPI_INSTANCE);
static bool        m_spi_initialised = false;

void spi_init(void)
{
    nrfx_spim_config_t cfg = NRFX_SPIM_DEFAULT_CONFIG;
    cfg.sck_pin    = SCK_PIN;
    cfg.mosi_pin   = MOSI_PIN;
    cfg.miso_pin   = MISO_PIN;
    cfg.ss_pin     = NRFX_SPIM_PIN_NOT_USED; /* manual CS */
    cfg.frequency  = NRF_SPIM_FREQ_8M;
    cfg.mode       = NRF_SPIM_MODE_0;
    cfg.bit_order  = NRF_SPIM_BIT_ORDER_MSB_FIRST;

    nrfx_err_t err = nrfx_spim_init(&m_spi, &cfg, NULL, NULL);
    APP_ERROR_CHECK(err);

    nrf_gpio_pin_set(CS_PIN);
    nrf_gpio_cfg_output(CS_PIN);
    m_spi_initialised = true;
}

/**
 * Uninitialise SPIM peripheral and place pins in sleep-safe state.
 * The nrfx_spim_uninit() call disables the SPIM peripheral and reclaims
 * the pins; we then explicitly configure them for minimal leakage.
 */
void spi_sleep_prepare(void)
{
    if (!m_spi_initialised) return;

    /* Ensure CS is high before we lose peripheral control */
    nrf_gpio_pin_set(CS_PIN);

    /* nrfx uninit stops the peripheral & disconnects pin mux */
    nrfx_spim_uninit(&m_spi);
    m_spi_initialised = false;

    /*
     * nRF52: uninit leaves pins as GPIO input (floating).
     * Override to pull-down to prevent leakage via MISO/MOSI lines.
     */
    nrf_gpio_cfg(MOSI_PIN,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_S0S1,
                 NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_pin_clear(MOSI_PIN);  /* drive low */

    nrf_gpio_cfg(SCK_PIN,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_S0S1,
                 NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_pin_clear(SCK_PIN);   /* drive low */

    nrf_gpio_cfg_input(MISO_PIN, NRF_GPIO_PIN_PULLDOWN);
}

/** Re-initialise after wake-up from System OFF or deep sleep. */
void spi_wake_restore(void)
{
    spi_init();
}

/* Power management handler — called by sd_app_evt_wait() / nrf_pwr_mgmt */
static bool sleep_prepare_handler(nrf_pwr_mgmt_evt_t event)
{
    switch (event) {
        case NRF_PWR_MGMT_EVT_PREPARE_SYSOFF:
        case NRF_PWR_MGMT_EVT_PREPARE_WAKEUP:
            spi_sleep_prepare();
            return true;   /* OK to proceed to sleep */
        default:
            return true;
    }
}

NRF_PWR_MGMT_HANDLER_REGISTER(sleep_prepare_handler, 0);
```

---

### 7.3 ESP-IDF — SPI Bus De-Init Before Light Sleep

```c
/* esp_spi_sleep.c */
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "spi_sleep";

#define PIN_NUM_MISO  19
#define PIN_NUM_MOSI  23
#define PIN_NUM_CLK   18
#define PIN_NUM_CS     5

static spi_device_handle_t s_spi_dev   = NULL;
static spi_bus_config_t    s_bus_cfg   = {0};
static spi_device_interface_config_t s_dev_cfg = {0};

esp_err_t spi_app_init(void)
{
    s_bus_cfg = (spi_bus_config_t){
        .miso_io_num     = PIN_NUM_MISO,
        .mosi_io_num     = PIN_NUM_MOSI,
        .sclk_io_num     = PIN_NUM_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };

    s_dev_cfg = (spi_device_interface_config_t){
        .clock_speed_hz = 10 * 1000 * 1000,  /* 10 MHz */
        .mode           = 0,
        .spics_io_num   = PIN_NUM_CS,
        .queue_size     = 7,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &s_bus_cfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &s_dev_cfg, &s_spi_dev));
    return ESP_OK;
}

/**
 * Prepare SPI for ESP32 light sleep.
 *
 * ESP-IDF automatically retains SPI peripheral registers in light sleep
 * (MODEM_SLEEP / AUTO_LIGHT_SLEEP) when the bus is registered with the
 * peripheral manager. However, we must still ensure:
 *   - No transaction is in flight
 *   - CS# is high
 *   - Optionally release the bus for WL_SLEEP (Wi-Fi coexistence)
 */
esp_err_t spi_prepare_light_sleep(void)
{
    esp_err_t ret;

    /* Acquire bus mutex — ensures no other task is mid-transaction */
    ret = spi_device_acquire_bus(s_spi_dev, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire SPI bus: %d", ret);
        return ret;
    }

    /* Release immediately — we just wanted the mutex held, then released
     * so that the bus is idle (not held by any handle) before sleep.      */
    spi_device_release_bus(s_spi_dev);

    /*
     * For deep sleep (esp_deep_sleep_start), we must free the device and bus
     * because drivers are not preserved across deep sleep. Re-init on wake.
     *
     * For light sleep (esp_light_sleep_start), the peripheral is retained;
     * no de-init is required. We only configure hold states for the pins.
     */

    /* Prevent GPIO latch issue: hold pins in current state during sleep */
    gpio_hold_en(PIN_NUM_CS);    /* holds CS# HIGH */
    gpio_hold_en(PIN_NUM_CLK);   /* holds CLK LOW  */
    gpio_hold_en(PIN_NUM_MOSI);  /* holds last MOSI state */

    ESP_LOGI(TAG, "SPI ready for light sleep");
    return ESP_OK;
}

esp_err_t spi_restore_after_sleep(void)
{
    /* Release hold so the SPI driver can control pins again */
    gpio_hold_dis(PIN_NUM_CS);
    gpio_hold_dis(PIN_NUM_CLK);
    gpio_hold_dis(PIN_NUM_MOSI);

    ESP_LOGI(TAG, "SPI pins released from hold, resuming");
    return ESP_OK;
}

/**
 * Full deep-sleep variant — de-init and re-init required.
 */
esp_err_t spi_prepare_deep_sleep(void)
{
    if (s_spi_dev) {
        spi_bus_remove_device(s_spi_dev);
        s_spi_dev = NULL;
    }
    spi_bus_free(SPI2_HOST);

    /* Reconfigure pins as input/pull-down for minimal leakage */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_NUM_MOSI) |
                        (1ULL << PIN_NUM_CLK)  |
                        (1ULL << PIN_NUM_MISO),
        .mode         = GPIO_MODE_INPUT,
        .pull_down_en = 1,
        .pull_up_en   = 0,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* CS# → output high */
    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_CS, 1);

    return ESP_OK;
}

/* After deep sleep wake-up (full re-init because RAM was lost) */
esp_err_t spi_restore_after_deep_sleep(void)
{
    return spi_app_init();
}
```

---

### 7.4 Generic Bare-Metal Pattern (ARM Cortex-M)

```c
/* spi_power.c — portable pattern independent of vendor HAL */
#include <stdint.h>
#include <stdbool.h>

/* 
 * Vendor-specific register map — replace with your platform's CMSIS header.
 * Shown here as a generic illustration.
 */
typedef volatile struct {
    uint32_t CR1;    /* Control register 1  */
    uint32_t CR2;    /* Control register 2  */
    uint32_t SR;     /* Status register     */
    uint32_t DR;     /* Data register       */
    uint32_t CRCPR;  /* CRC polynomial      */
    uint32_t RXCRCR; /* RX CRC register     */
    uint32_t TXCRCR; /* TX CRC register     */
} SPI_TypeDef_Generic;

#define SPI_CR1_SPE     (1U << 6)   /* SPI Enable         */
#define SPI_SR_BSY      (1U << 7)   /* Busy flag          */
#define SPI_SR_RXNE     (1U << 0)   /* RX buffer not empty*/
#define SPI_SR_TXE      (1U << 1)   /* TX buffer empty    */

typedef struct {
    uint32_t CR1;
    uint32_t CR2;
    uint32_t CRCPR;
} SPI_SavedState_t;

/**
 * @brief Wait for SPI idle and save register state.
 *
 * @param spi         Pointer to SPI register block
 * @param saved       Output: saved register state for deep-sleep restore
 * @param timeout_ms  Busy-wait timeout
 * @return 0 on success, -1 on timeout
 */
int spi_save_and_disable(SPI_TypeDef_Generic *spi,
                         SPI_SavedState_t    *saved,
                         uint32_t             timeout_ms)
{
    extern uint32_t HAL_GetTick(void);   /* or your systick counter */
    uint32_t t0 = HAL_GetTick();

    /* Wait for current transfer to complete */
    while (spi->SR & SPI_SR_BSY) {
        if ((HAL_GetTick() - t0) >= timeout_ms) return -1;
    }

    /* Flush any stale RX data */
    while (spi->SR & SPI_SR_RXNE) {
        (void)spi->DR;
    }

    /* Save configuration before disabling */
    saved->CR1   = spi->CR1;
    saved->CR2   = spi->CR2;
    saved->CRCPR = spi->CRCPR;

    /* Disable SPI (clears SPE, keeps config) */
    spi->CR1 &= ~SPI_CR1_SPE;

    return 0;
}

/**
 * @brief Restore SPI from saved state after sleep.
 *
 * @param spi    Pointer to SPI register block
 * @param saved  Previously saved state
 */
void spi_restore_and_enable(SPI_TypeDef_Generic *spi,
                             const SPI_SavedState_t *saved)
{
    /* Restore config — ensure SPE=0 while writing */
    spi->CR1   = saved->CR1 & ~SPI_CR1_SPE;
    spi->CR2   = saved->CR2;
    spi->CRCPR = saved->CRCPR;

    /* Re-enable */
    spi->CR1 |= SPI_CR1_SPE;
}
```

---

## Code Examples — Rust

### 8.1 Embassy (STM32) — Async SPI with Sleep Token

Embassy provides `embassy-stm32` with native async SPI. The approach is to use
`embassy_time::Timer` for periodic wake and drop/re-acquire the SPI peripheral
across low-power waits.

```rust
// Cargo.toml dependencies:
// embassy-stm32 = { version = "0.1", features = ["stm32l476rg", "time-driver-any"] }
// embassy-time  = { version = "0.3" }
// embassy-executor = { version = "0.5", features = ["arch-cortex-m"] }

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::{
    gpio::{Level, Output, Speed},
    spi::{self, Spi},
    time::mhz,
};
use embassy_time::{Duration, Timer};

/// Represents a "sleep token" that proves SPI has been quiesced.
/// The borrow of `Spi` prevents it from being used while sleeping.
struct SpiSleepGuard<'d, T: spi::Instance> {
    _spi: Spi<'d, T, spi::Async>,
}

impl<'d, T: spi::Instance> SpiSleepGuard<'d, T> {
    /// Consume the Spi peripheral and return a guard.
    /// The peripheral is disabled when Spi is dropped.
    fn enter(spi: Spi<'d, T, spi::Async>) -> Self {
        // Spi::drop() in embassy-stm32 disables SPE and the peripheral clock.
        // By moving the Spi into this struct we ensure it remains
        // alive (not dropped early) until we explicitly release it.
        SpiSleepGuard { _spi: spi }
    }

    /// Release the Spi peripheral back to the caller.
    fn exit(self) -> Spi<'d, T, spi::Async> {
        self._spi
    }
}

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    // Configure SPI1
    let mut spi_config = spi::Config::default();
    spi_config.frequency = mhz(8);

    let mut cs = Output::new(p.PA4, Level::High, Speed::High);

    // Acquire SPI peripheral
    let spi = Spi::new(
        p.SPI1,
        p.PA5,  // SCK
        p.PA7,  // MOSI
        p.PA6,  // MISO
        p.DMA1_CH3,
        p.DMA1_CH2,
        spi_config,
    );

    // Pin the SPI in a block so we can yield ownership
    let mut spi = spi;

    loop {
        // ── Active phase ──────────────────────────────────────────
        cs.set_low();
        let tx_buf: [u8; 4] = [0xDE, 0xAD, 0xBE, 0xEF];
        let mut rx_buf = [0u8; 4];
        spi.transfer(&mut rx_buf, &tx_buf).await.unwrap();
        cs.set_high();

        // ── Sleep preparation ─────────────────────────────────────
        // Move SPI into guard; this drops SPE when guard scope ends.
        // For deep sleep we would also need to reconfigure GPIOs here.
        let guard = SpiSleepGuard::enter(spi);

        // Timer::after() puts the executor to sleep (WFI in the
        // idle loop) for the duration — Embassy handles low-power
        // WFI automatically in the executor idle hook.
        Timer::after(Duration::from_secs(5)).await;

        // ── Wake-up & restore ─────────────────────────────────────
        spi = guard.exit();
        // Embassy re-enables the peripheral clock automatically when
        // Spi is in active use; no manual register restore required
        // for STOP1 / light-sleep modes.
    }
}
```

---

### 8.2 RTIC + STM32 — Resource Suspend in Idle

```rust
//! rtic_spi_sleep.rs
//! RTIC v2 application — SPI is suspended in the idle task and
//! restored on demand in software tasks.

#![no_std]
#![no_main]

use cortex_m::asm;
use rtic::app;
use stm32l4xx_hal::{
    pac,
    prelude::*,
    spi::{Mode, Phase, Polarity, Spi},
    gpio::{Output, PushPull, PA4},
};

/// Saved SPI configuration for deep-sleep restore.
#[derive(Clone, Copy, Default)]
struct SpiConfig {
    cr1: u32,
    cr2: u32,
}

#[app(device = pac, peripherals = true, dispatchers = [USART1])]
mod app {
    use super::*;

    #[shared]
    struct Shared {
        spi_config: SpiConfig,
    }

    #[local]
    struct Local {
        spi: Option<Spi<pac::SPI1>>,
        cs: PA4<Output<PushPull>>,
        deep_sleep_active: bool,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        let dp = ctx.device;
        let mut rcc = dp.RCC.constrain();
        let clocks = rcc.cfgr.freeze();

        let gpioa = dp.GPIOA.split();
        let cs    = gpioa.pa4.into_push_pull_output();
        
        let sck  = gpioa.pa5.into_alternate::<5>();
        let miso = gpioa.pa6.into_alternate::<5>();
        let mosi = gpioa.pa7.into_alternate::<5>();

        let mode = Mode {
            polarity: Polarity::IdleLow,
            phase: Phase::CaptureOnFirstTransition,
        };

        let spi = Spi::spi1(dp.SPI1, (sck, miso, mosi), mode, 8.MHz(), clocks);

        (
            Shared { spi_config: SpiConfig::default() },
            Local {
                spi: Some(spi),
                cs,
                deep_sleep_active: false,
            },
        )
    }

    /// Idle task: entered when no other task is runnable.
    /// We use this opportunity to enter low-power sleep.
    #[idle(local = [spi, cs, deep_sleep_active], shared = [spi_config])]
    fn idle(mut ctx: idle::Context) -> ! {
        loop {
            // ── Prepare SPI for sleep ──────────────────────────────
            if let Some(ref spi) = ctx.local.spi {
                // Read CR1/CR2 and save them before disabling.
                let spi_regs = unsafe { &*pac::SPI1::ptr() };
                
                // Wait for BSY to clear
                while spi_regs.sr.read().bsy().bit_is_set() {}

                ctx.shared.spi_config.lock(|cfg| {
                    cfg.cr1 = spi_regs.cr1.read().bits();
                    cfg.cr2 = spi_regs.cr2.read().bits();
                });

                // Ensure CS is deasserted
                ctx.local.cs.set_high();

                // Disable SPI peripheral clock via RCC
                unsafe {
                    let rcc = &*pac::RCC::ptr();
                    rcc.apb2enr.modify(|_, w| w.spi1en().disabled());
                }
                *ctx.local.deep_sleep_active = true;
            }

            // ── Enter low-power mode ───────────────────────────────
            // cortex_m WFI — RTIC's PEND mechanism will wake us.
            asm::wfi();

            // ── Restore SPI after wake ─────────────────────────────
            if *ctx.local.deep_sleep_active {
                let spi_regs = unsafe { &*pac::SPI1::ptr() };

                // Re-enable clock
                unsafe {
                    let rcc = &*pac::RCC::ptr();
                    rcc.apb2enr.modify(|_, w| w.spi1en().enabled());
                }

                // Restore CR1 and CR2 (SPE=0 during write)
                ctx.shared.spi_config.lock(|cfg| {
                    spi_regs.cr1.write(|w| unsafe {
                        w.bits(cfg.cr1 & !(1 << 6))  /* SPE=0 */
                    });
                    spi_regs.cr2.write(|w| unsafe { w.bits(cfg.cr2) });
                });

                // Re-enable SPI
                spi_regs.cr1.modify(|r, w| unsafe { w.bits(r.bits() | (1 << 6)) });

                *ctx.local.deep_sleep_active = false;
            }
        }
    }

    /// Example software task that wakes the system and uses SPI.
    #[task(local = [spi, cs], priority = 2)]
    async fn spi_transfer_task(ctx: spi_transfer_task::Context) {
        let spi = ctx.local.spi.as_mut().expect("SPI not initialised");
        ctx.local.cs.set_low();
        let data: [u8; 2] = [0xAB, 0xCD];
        spi.write(&data).ok();
        ctx.local.cs.set_high();
    }
}
```

---

### 8.3 nRF52 in Rust (nrf-hal) — SPI Drop Before WFI

```rust
//! nrf52_spi_sleep.rs
//! Uses nrf-hal. The idiomatic approach is to *drop* the SPIM peripheral
//! before entering low-power wait, forcing the hardware into reset.
//! On wake-up, reconstruct it from the PAC peripheral token.

#![no_std]
#![no_main]

use cortex_m::asm;
use nrf52840_hal::{
    self as hal,
    gpio::{p0, Level, Output, PushPull},
    pac::SPIM0,
    spim::{self, Frequency, Mode, Phase, Polarity, Spim, TransferSplit},
    Clocks,
};
use cortex_m_rt::entry;

/// Wrapper that owns the SPIM PAC token so we can reconstruct Spim after sleep.
struct SpiManager {
    spim0: Option<SPIM0>,
}

impl SpiManager {
    fn new(spim0: SPIM0) -> Self {
        SpiManager { spim0: Some(spim0) }
    }

    /// Build a live Spim instance — consumes the PAC token internally.
    fn acquire<PINS>(&mut self, pins: PINS) -> Spim<SPIM0>
    where
        PINS: spim::Pins,
    {
        let spim0 = self.spim0.take().expect("SPIM0 already in use");
        Spim::new(
            spim0,
            pins,
            Frequency::M8,
            Mode {
                polarity: Polarity::IdleLow,
                phase: Phase::CaptureOnFirstTransition,
            },
            0x00,
        )
    }

    /// Disable the Spim and recover the PAC token for later re-use.
    fn release(&mut self, spi: Spim<SPIM0>) -> SPIM0 {
        // nrf-hal's free() disables the SPIM and returns the PAC peripheral.
        let (spim0, _pins) = spi.free();
        self.spim0 = Some(spim0);
        self.spim0.as_ref().unwrap();  /* satisfy borrow checker */
        // Return token to caller is implicit via self.spim0 being Some
        self.spim0.take().unwrap()
    }
}

#[entry]
fn main() -> ! {
    let dp = hal::pac::Peripherals::take().unwrap();
    let _clocks = Clocks::new(dp.CLOCK).enable_ext_hfosc();

    let p0 = p0::Parts::new(dp.P0);

    // Pins
    let sck  = p0.p0_27.into_push_pull_output(Level::Low).degrade();
    let mosi = p0.p0_26.into_push_pull_output(Level::Low).degrade();
    let miso = p0.p0_02.into_floating_input().degrade();
    let mut cs = p0.p0_06.into_push_pull_output(Level::High);

    let pins = spim::Pins {
        sck:  Some(sck),
        miso: Some(miso),
        mosi: Some(mosi),
    };

    let mut mgr = SpiManager::new(dp.SPIM0);

    loop {
        // ── Active: acquire SPI and transact ──────────────────────
        {
            let mut spi = mgr.acquire(pins.clone());  // re-init peripheral
            let tx = [0xDE, 0xAD];
            let mut rx = [0u8; 2];

            cs.set_low();
            spi.transfer(&mut rx, &tx).unwrap();
            cs.set_high();

            // Drop Spim at end of scope — nrf-hal disables SPIM0 on drop.
            // This is equivalent to calling spi.free() + forgetting the token.
            // mgr.release() gives us back the PAC token if needed.
            drop(spi);
        }

        // ── Sleep: configure pins for minimal leakage ──────────────
        // (pins are already owned above; in real code, reconfigure them here)
        // cs is already HIGH. MOSI/SCK go low via the Output init above.

        // Enter WFI — nRF52 enters System-ON sleep automatically
        asm::wfi();

        // ── Wake: loop continues, SPI re-acquired at top of loop ──
    }
}
```

> **Note:** In the pattern above, `pins.clone()` requires the pin types to
> implement `Clone`. In practice you would store the pins in a `static` or
> pass them via a singleton. The key idiom — **drop the `Spim` to disable the
> peripheral, reconstruct it to resume** — remains the idiomatic Rust/nrf-hal
> approach.

---

## Common Pitfalls and Anti-Patterns

| # | Pitfall | Symptom | Fix |
|---|---------|---------|-----|
| 1 | Entering sleep with CS# asserted | External device never powers down; high quiescent current | Always check CS# HIGH before sleep |
| 2 | Not waiting for BSY=0 | Corrupted final byte; SPI locked on wake | Poll BSY, add timeout |
| 3 | Skipping DMA abort | DMA holds bus across sleep; corrupt memory on deep sleep | `HAL_DMA_Abort()` before sleep |
| 4 | Re-enabling SPE before writing CR1/CR2 | Config registers ignored while SPE=1 on some devices | Clear SPE, write config, then set SPE |
| 5 | Floating MOSI/CLK during deep sleep | Capacitive leakage; increased current; noise on bus | Drive low or pull-down before sleep |
| 6 | Forgetting external device wake delay | First transaction corrupted (device still waking) | Respect tRES / power-up timing in datasheet |
| 7 | Restoring from wrong sleep depth | Assuming registers retained when they weren't | Track sleep depth; conditionally re-init |
| 8 | Clearing RX FIFO after re-enable | OVR flag causes dropped bytes on first transfer | Flush before disabling, then check SR on wake |
| 9 | Race between DMA complete ISR and sleep entry | ISR fires after sleep check, SPI still busy | Disable IRQ around BSY check → sleep entry |
| 10 | Not accounting for SPI clock startup time | Glitch on first edge after deep sleep | Add brief delay after clock re-enable |

---

## Timing Diagrams

```
Sleep Entry Sequence
─────────────────────────────────────────────────────────────
 Application        │  Last SPI Transfer  │ Sleep Prep        │ WFI
                    │                     │                   │
 CS# ───────────────┐                     ┌───────────────────│────────
                    └─────────────────────┘  (HIGH)
 CLK ─────┐┌┐┌┐┌┐┌┐└─────────────────────────────────────────│────────
           └┘└┘└┘└┘ (idle)
 BSY ──────┐────────┐                     ┌───────────────────│────────
           │  HIGH  └─────────────────────┘  (wait for LOW)
 SPE  ─────────────────────────────────── X ─ (disabled)
 RCC  ─────────────────────────────────────── X ─ (gated)     │
                                                               WFI

Wake-Up Restore Sequence
─────────────────────────────────────────────────────────────
 Event     │ RCC re-enable │ GPIO restore │ SPE enable │ tRES  │ First TX
           │               │              │            │       │
 CLK  ─────────────────────┼──────────────────────────│───────┼──┐┌┐┌┐
                                                               │  └┘└┘
 CS#  ─────────────────────┼──────────────────────────│───────┼──┐
                                                               │  └──────
           ← registers       GPIO AF re-               SPE=1  tRES   data
             restored          applied
```

---

## Platform Quick-Reference Table

| Platform | Light Sleep Behavior | Deep Sleep Reg Retention | Recommended API |
|----------|---------------------|--------------------------|-----------------|
| STM32L4 (STOP1) | Clock gated, regs retained | ✗ in STOP2/Standby | `HAL_SPI_DeInit` / `HAL_SPI_Init` |
| STM32H7 (DSTANDBY) | Full power-off | ✗ | Manual CR1/CR2 backup |
| nRF52840 System-ON | SPIM auto-disabled when not active | n/a (no deep sleep regs) | `nrfx_spim_uninit` / re-init |
| nRF52840 System-OFF | Full power-off, RAM retained (opt) | ✗ | Full re-init on wake |
| ESP32 Light Sleep | Peripheral clock gated, regs retained | ✓ with RTC domain | `gpio_hold_en()` for pins |
| ESP32 Deep Sleep | Full power-off | ✗ | `spi_bus_free()` + re-init |
| RP2040 Dormant | ROSC/XOSC off, peripherals off | ✗ | Full re-init |
| SAMD21 Standby | APB clocks optional, SPI can run in standby | ✓ if RUNSTDBY set | Set RUNSTDBY bit in CTRLA |

---

## Summary

Managing SPI peripheral state across MCU sleep and wake cycles is a multi-layered discipline that touches hardware clocking, DMA coherence, GPIO electrical state, and the power management requirements of every attached external device.

**The core principle is deterministic quiescence before sleep and deterministic readiness after wake.** This means: wait for all in-flight transfers to complete, drain receive FIFOs, deassert all chip-select lines, instruct connected peripherals to enter their own low-power modes, save any register state that will be lost, reconfigure GPIO pins to minimise leakage current, and only then assert the WFI/sleep instruction.

On wake, the inverse sequence must be followed with attention to the depth of sleep that was entered. For light-sleep modes (clock-gated but powered), peripheral registers are retained and only the clock and SPE bit need restoration. For deep-sleep and standby modes (power domains removed), a full re-initialisation equivalent to cold-boot start-up is required, including GPIO alternate-function remapping, baud-rate register reconfiguration, and re-connection of DMA channels. External devices require their own wake-up commands and must be given manufacturer-specified recovery time before the first transaction is attempted.

In **C/C++**, vendor HAL libraries (STM32 HAL, nrfx, ESP-IDF) provide the primitives; the developer's responsibility is to compose them in the correct order with appropriate guards against race conditions between ISRs and the sleep-entry sequence.

In **Rust**, the ownership and drop semantics of peripheral abstractions (embassy-stm32, nrf-hal, RTIC resources) provide compile-time enforcement that a peripheral cannot be used while quiesced, turning runtime errors into type errors. The idiomatic patterns — dropping `Spi<T>` to disable the hardware, using sleep guards or scoped ownership to represent quiesced state, and re-constructing from the PAC token on wake — align naturally with the language's ownership model and produce both safe and efficient code.

Across all platforms, the key metrics to optimise are: total time spent in the sleep-entry and wake sequences (directly reduces effective sleep depth), current leakage through un-driven GPIO pins (often dominates over the idle SPI peripheral itself), and correctness of the first SPI transaction after wake (the most common source of subtle bugs in production firmware).

---

*Document version 1.0 — SPI Topic Series #72*