# 75. Wake-on-SPI Mechanisms


**Concept & Architecture**
- Why pure SPI cannot self-wake (master-clocked, peripheral powered down)
- The two primary patterns: CS#-as-wake-GPIO and the two-pin WAKE/READY handshake
- Full state machine diagram (SLEEP → WAKING UP → ACTIVE → SLEEP)
- Hardware pin table with pull-up and Hi-Z requirements

**C/C++ Code Examples**
- A platform-agnostic HAL layer (`WakeSpiHalOps` ops table + `WakeSpiContext` state machine) reusable across any MCU
- Full **STM32L4 Stop mode 2** implementation using EXTI on CS#, DMA SPI, and clock recovery after wake
- **Nordic nRF52** implementation using GPIOTE edge detection and SPIS peripheral
- **Linux kernel driver** fragment with `enable_irq_wake()`, `pm_stay_awake()`, and a device tree snippet

**Rust Code Examples**
- **RTIC 2.0** on nRF52840 with type-safe peripheral ownership and priority-based interrupt tasks
- **Embassy async/await** on STM32L4 — the `cs_in.wait_for_falling_edge().await` maps directly to a WFE low-power wait
- **Linux userspace daemon** using `spidev` + `gpiocdev` + `tokio`

**Engineering Details**
- Protocol framing with CRC-8, sequence numbers, and timing tables
- Power budget calculation showing ~27 µA average → 3.4 year battery life
- Debugging table covering the most common failure modes with fixes

## Using SPI Events to Wake a System from Deep Sleep States

---

## Table of Contents

1. [Introduction](#introduction)
2. [Background: Deep Sleep States and Wake Sources](#background)
3. [SPI as a Wake Source — Concepts](#spi-as-a-wake-source)
4. [Hardware Architecture](#hardware-architecture)
5. [Software Architecture](#software-architecture)
6. [Implementation in C/C++](#implementation-in-c-cpp)
   - [Platform-Agnostic HAL Layer](#platform-agnostic-hal-layer)
   - [STM32 Low-Power Wake-on-SPI Example](#stm32-example)
   - [Nordic nRF52 Wake-on-SPI Example](#nordic-example)
   - [Linux Kernel Driver Fragment](#linux-kernel-driver)
7. [Implementation in Rust](#implementation-in-rust)
   - [Embedded Rust with RTIC and nRF52](#embedded-rust-rtic)
   - [Rust async/await with Embassy](#rust-embassy)
   - [Rust Linux Userspace Daemon](#rust-linux-userspace)
8. [Protocol Design Considerations](#protocol-design)
9. [Power Budgeting](#power-budgeting)
10. [Debugging Wake-on-SPI Systems](#debugging)
11. [Summary](#summary)

---

## 1. Introduction <a name="introduction"></a>

In battery-powered and energy-constrained embedded systems — such as IoT sensors, wearables, industrial controllers, and wireless modules — the ability to place the main processor into a deep sleep state and wake it precisely when needed is critical to extending battery life. While common wake sources include timers (RTC), GPIO level changes, or UART break signals, **Wake-on-SPI** refers to the ability to wake the system based on activity on the SPI bus itself.

Wake-on-SPI is used when:

- A peripheral (e.g., a radio IC, IMU, or flash device) needs to signal the host over the SPI interface.
- An external master initiates a transaction with a sleeping SPI slave.
- The system must service SPI data without polling, using deep sleep between bursts.

This document covers the concept, hardware design, and both C/C++ and Rust software implementations in detail.

---

## 2. Background: Deep Sleep States and Wake Sources <a name="background"></a>

Modern microcontrollers support several power modes:

| Mode | CPU | Peripherals | Wake Sources | Current |
|------|-----|-------------|--------------|---------|
| Active | Running | All on | N/A | ~10–100 mA |
| Sleep | Halted | Most on | Interrupt | ~1–10 mA |
| Deep Sleep / Stop | Halted | Most off | Limited IRQ, GPIO | ~10–500 µA |
| Standby / Hibernate | Halted | Nearly all off | RTC, specific pins | ~1–50 µA |
| Shutdown | Off | Off | NRST, WKUP pins | < 1 µA |

In **deep sleep** (Stop/Standby on STM32, System OFF on nRF52, etc.), the SPI peripheral is often powered down. The SPI clock line itself cannot wake the CPU in these states, because the SPI block is not running. Instead, the system must use **auxiliary wake mechanisms** that are active even in deep sleep.

### Common Wake-on-SPI Strategies

1. **CS# (Chip Select) as GPIO wake pin** — The falling edge of CS# triggers a GPIO interrupt that wakes the CPU, which then re-initialises the SPI peripheral.
2. **Dedicated WAKE pin** — A separate GPIO from the SPI master signals intent to communicate; the slave wakes, re-enables SPI, then asserts a READY signal.
3. **Always-on SPI with low-power mode** — Some SoCs support a low-power SPI mode that can receive a byte and generate a wake event.
4. **DMA-assisted reception** — SPI + DMA configured so that the first received byte completes and triggers a wake-up interrupt via DMA complete callback.

---

## 3. SPI as a Wake Source — Concepts <a name="spi-as-a-wake-source"></a>

### The Core Problem

Pure SPI is synchronous: the master drives the clock. A sleeping slave cannot know the master wants to communicate unless an out-of-band signal is available. This is the fundamental challenge Wake-on-SPI mechanisms must solve.

### The CS# Wake Pattern (Most Common)

```
Master                                  Slave (sleeping)
  |                                        |
  |--- CS# LOW (GPIO edge) --------------> |
  |                                    [WAKE IRQ]
  |                                    [Re-init SPI]
  |                                    [Assert READY]
  |<-- READY/MISO signal ------------------|
  |--- SPI CLK --------------------------> |
  |--- MOSI data ------------------------> |
  |<-- MISO data --------------------------|
  |--- CS# HIGH -------------------------> |
  |                                    [Return to sleep]
```

### The Dedicated WAKE Pin Pattern

```
Master                                  Slave (sleeping)
  |                                         |
  |--- WAKE LOW (dedicated GPIO) ---------->|
  |                                    [WAKE IRQ]
  |                                    [Boot & init]
  |<-- READY LOW (dedicated GPIO) ----------|
  |--- CS# LOW + SPI CLK + MOSI ----------->|
  |<-- MISO --------------------------------|
  |--- WAKE HIGH -------------------------->|
  |<-- READY HIGH --------------------------|
  |                                    [Sleep again]
```

The two-pin handshake (WAKE + READY) is more robust but requires extra pins.

---

## 4. Hardware Architecture <a name="hardware-architecture"></a>

### Pin Configuration (Typical)

| Signal | Direction | Description |
|--------|-----------|-------------|
| SPI_SCK | Input (slave) | SPI clock — NOT a wake source in deep sleep |
| SPI_MOSI | Input (slave) | Master Out, Slave In |
| SPI_MISO | Output (slave) | Master In, Slave Out — driven Hi-Z during sleep |
| SPI_CS# | Input (slave) | **Chip Select — primary wake source via GPIO EXTI** |
| SPI_WAKE | Input (slave) | Optional dedicated wake line from master |
| SPI_READY | Output (slave) | Optional ready handshake to master |

### Key Hardware Requirements

- CS# must be routed to a **wake-capable GPIO pin** (typically EXTI-capable on STM32, GPIO sense on nRF52).
- Pull-up on CS# is mandatory (active-low signal; must not float during sleep).
- MISO pin must be configured as **Hi-Z / open-drain** while the SPI peripheral is off, to avoid bus contention.
- If using WAKE + READY pins, the READY pin must be driven from the slave GPIO (not SPI peripheral).
- Decoupling capacitors and series resistors (22–47 Ω) on SCK and MOSI help reduce glitches that could cause spurious wake events.

---

## 5. Software Architecture <a name="software-architecture"></a>

A Wake-on-SPI system typically follows this state machine:

```
         ┌──────────────────────────────────────────┐
         │                                          │
         ▼                                          │
   ┌──────────┐  CS# edge IRQ   ┌──────────────┐    │
   │  SLEEP   │ ──────────────► │  WAKING UP   │    │
   └──────────┘                 └──────┬───────┘    │
                                       │            │
                              Re-init SPI + DMA     │
                                       │            │
                                       ▼            │
                               ┌──────────────┐     │
                               │   ACTIVE     │     │
                               │ (SPI running)│     │
                               └──────┬───────┘     │
                                      │ CS# HIGH +  │
                                      │ idle timeout│
                                      └─────────────┘
```

### Software Components

1. **Wake interrupt handler** — minimal ISR that sets a flag and wakes the scheduler.
2. **SPI re-initialisation** — re-enables clocks, reconfigures GPIO alternate functions, starts DMA.
3. **Transaction processing** — interprets received data and dispatches to application.
4. **Sleep entry** — drains pending SPI transactions, disables SPI peripheral, configures CS# as wake GPIO, enters low-power mode.

---

## 6. Implementation in C/C++ <a name="implementation-in-c-cpp"></a>

### Platform-Agnostic HAL Layer <a name="platform-agnostic-hal-layer"></a>

First, define a clean HAL interface to decouple wake logic from hardware:

```c
/* wake_spi_hal.h — Platform-agnostic Wake-on-SPI HAL */

#ifndef WAKE_SPI_HAL_H
#define WAKE_SPI_HAL_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum SPI transaction payload in bytes */
#define WAKE_SPI_MAX_PAYLOAD  256

/* System power states */
typedef enum {
    POWER_STATE_ACTIVE      = 0,
    POWER_STATE_SLEEP       = 1,
    POWER_STATE_DEEP_SLEEP  = 2,
    POWER_STATE_STANDBY     = 3,
} PowerState;

/* Wake event type */
typedef enum {
    WAKE_EVENT_CS_ASSERT    = (1u << 0),  /* CS# falling edge */
    WAKE_EVENT_WAKE_PIN     = (1u << 1),  /* Dedicated WAKE pin */
    WAKE_EVENT_RTC          = (1u << 2),  /* RTC timeout (unrelated) */
} WakeEvent;

/* SPI transaction descriptor */
typedef struct {
    uint8_t  tx_buf[WAKE_SPI_MAX_PAYLOAD];
    uint8_t  rx_buf[WAKE_SPI_MAX_PAYLOAD];
    uint16_t length;
    bool     complete;
    int      error;
} SpiTransaction;

/* Callback types */
typedef void (*WakeSpiRxCallback)(const SpiTransaction *txn, void *ctx);
typedef void (*WakeSpiErrorCallback)(int error_code, void *ctx);

/* HAL operations — platform must implement these */
typedef struct {
    int  (*init)(void);
    int  (*spi_reinit)(void);
    int  (*spi_deinit)(void);
    int  (*start_dma_rx)(uint8_t *buf, uint16_t len);
    int  (*enter_deep_sleep)(void);
    void (*set_ready_pin)(bool asserted);
    bool (*is_cs_asserted)(void);
    void (*disable_wake_irq)(void);
    void (*enable_wake_irq)(void);
} WakeSpiHalOps;

/* Main context */
typedef struct {
    const WakeSpiHalOps  *ops;
    WakeSpiRxCallback     rx_cb;
    WakeSpiErrorCallback  err_cb;
    void                 *user_ctx;
    PowerState            power_state;
    volatile WakeEvent    pending_events;
    SpiTransaction        current_txn;
} WakeSpiContext;

/* API */
int  wake_spi_init(WakeSpiContext *ctx, const WakeSpiHalOps *ops,
                   WakeSpiRxCallback rx_cb, WakeSpiErrorCallback err_cb,
                   void *user_ctx);
void wake_spi_run(WakeSpiContext *ctx);          /* Main event loop step */
int  wake_spi_sleep(WakeSpiContext *ctx);        /* Request deep sleep */
void wake_spi_notify_cs_event(WakeSpiContext *ctx);   /* Call from ISR */

#endif /* WAKE_SPI_HAL_H */
```

```c
/* wake_spi.c — Platform-agnostic Wake-on-SPI state machine */

#include "wake_spi_hal.h"
#include <string.h>

/* Settle time after waking before SPI is ready (platform-dependent, µs) */
#define WAKE_SETTLE_US  50

/* Simple busy-wait stub — replace with platform timer */
static void delay_us(uint32_t us) {
    volatile uint32_t i = us * 8; /* crude approximation at ~8 MHz */
    while (i--) { __asm__ volatile ("nop"); }
}

int wake_spi_init(WakeSpiContext *ctx, const WakeSpiHalOps *ops,
                  WakeSpiRxCallback rx_cb, WakeSpiErrorCallback err_cb,
                  void *user_ctx)
{
    if (!ctx || !ops) return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->ops        = ops;
    ctx->rx_cb      = rx_cb;
    ctx->err_cb     = err_cb;
    ctx->user_ctx   = user_ctx;
    ctx->power_state = POWER_STATE_ACTIVE;

    return ops->init();
}

/* Called from CS# EXTI ISR — keep minimal */
void wake_spi_notify_cs_event(WakeSpiContext *ctx)
{
    ctx->pending_events |= WAKE_EVENT_CS_ASSERT;
    /* In an RTOS context, signal a task here instead of polling */
}

/* Main state machine step — call from task or super-loop */
void wake_spi_run(WakeSpiContext *ctx)
{
    if (ctx->pending_events & WAKE_EVENT_CS_ASSERT) {
        ctx->pending_events &= ~WAKE_EVENT_CS_ASSERT;

        if (ctx->power_state == POWER_STATE_DEEP_SLEEP ||
            ctx->power_state == POWER_STATE_SLEEP) {

            /* Re-initialise SPI peripheral and DMA */
            ctx->ops->spi_reinit();
            delay_us(WAKE_SETTLE_US);

            /* Signal readiness to master */
            ctx->ops->set_ready_pin(true);

            ctx->power_state = POWER_STATE_ACTIVE;
        }

        /* Start DMA receive for incoming transaction */
        memset(&ctx->current_txn, 0, sizeof(ctx->current_txn));
        ctx->ops->start_dma_rx(ctx->current_txn.rx_buf,
                               WAKE_SPI_MAX_PAYLOAD);
    }

    /* Check if a DMA transfer completed (set by DMA ISR) */
    if (ctx->current_txn.complete) {
        ctx->current_txn.complete = false;
        ctx->ops->set_ready_pin(false);

        if (ctx->rx_cb) {
            ctx->rx_cb(&ctx->current_txn, ctx->user_ctx);
        }
    }
}

/* Prepare for deep sleep */
int wake_spi_sleep(WakeSpiContext *ctx)
{
    /* Wait for any pending transaction to finish */
    while (ctx->ops->is_cs_asserted()) { /* spin or yield */ }

    ctx->ops->set_ready_pin(false);
    ctx->ops->spi_deinit();           /* Power down SPI peripheral */
    ctx->ops->enable_wake_irq();      /* Re-arm CS# edge interrupt */
    ctx->power_state = POWER_STATE_DEEP_SLEEP;

    return ctx->ops->enter_deep_sleep();
}
```

---

### STM32 Low-Power Wake-on-SPI Example <a name="stm32-example"></a>

This example targets an STM32L4 in Stop mode 2 (deep sleep), woken by the CS# falling edge on PA4.

```c
/* stm32_wake_spi.c — STM32L4 HAL implementation */

#include "stm32l4xx_hal.h"
#include "wake_spi_hal.h"

/* Peripheral handles */
static SPI_HandleTypeDef  hspi1;
static DMA_HandleTypeDef  hdma_spi1_rx;

/* Flag set by DMA complete callback — polled in wake_spi_run() */
static volatile bool      dma_rx_complete = false;
static SpiTransaction     *active_txn     = NULL;

/* ------------------------------------------------------------------ */
/* GPIO and pin definitions                                             */
/* ------------------------------------------------------------------ */
#define SPI_CS_PIN        GPIO_PIN_4
#define SPI_CS_PORT       GPIOA
#define SPI_CS_EXTI_LINE  EXTI_LINE_4

#define SPI_READY_PIN     GPIO_PIN_3
#define SPI_READY_PORT    GPIOA

/* ------------------------------------------------------------------ */
/* HAL init                                                            */
/* ------------------------------------------------------------------ */
static int stm32_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Enable clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* READY pin — output, initially de-asserted (high) */
    gpio.Pin   = SPI_READY_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SPI_READY_PORT, &gpio);
    HAL_GPIO_WritePin(SPI_READY_PORT, SPI_READY_PIN, GPIO_PIN_SET);

    /* CS# pin — input with pull-up; configure EXTI separately */
    gpio.Pin  = SPI_CS_PIN;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SPI_CS_PORT, &gpio);

    HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);

    return stm32_spi_reinit();
}

/* ------------------------------------------------------------------ */
/* SPI + DMA initialisation (called at boot and after deep sleep wake) */
/* ------------------------------------------------------------------ */
static int stm32_spi_reinit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_SPI1_CLK_ENABLE();

    /* SPI pins: SCK=PA5, MISO=PA6, MOSI=PA7 */
    gpio.Pin       = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_SLAVE;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;   /* We manage CS# as GPIO */
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK) return -1;

    /* DMA RX channel */
    hdma_spi1_rx.Instance                 = DMA1_Channel2;
    hdma_spi1_rx.Init.Request             = DMA_REQUEST_1;
    hdma_spi1_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_spi1_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi1_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi1_rx.Init.Mode                = DMA_NORMAL;
    hdma_spi1_rx.Init.Priority            = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&hdma_spi1_rx) != HAL_OK) return -1;

    __HAL_LINKDMA(&hspi1, hdmarx, hdma_spi1_rx);

    HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);

    return 0;
}

static int stm32_spi_deinit(void)
{
    HAL_SPI_DeInit(&hspi1);
    HAL_DMA_DeInit(&hdma_spi1_rx);

    /* Reconfigure SPI pins as analog to minimise leakage */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin  = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_SPI1_CLK_DISABLE();
    return 0;
}

static int stm32_start_dma_rx(uint8_t *buf, uint16_t len)
{
    active_txn = NULL;  /* updated by caller to current transaction */
    return (HAL_SPI_Receive_DMA(&hspi1, buf, len) == HAL_OK) ? 0 : -1;
}

static int stm32_enter_deep_sleep(void)
{
    /* Suspend SysTick to prevent it waking us */
    HAL_SuspendTick();

    /* Enter STM32L4 Stop 2 mode — lowest leakage with SRAM retention */
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /* Execution resumes here after wake */
    HAL_ResumeTick();

    /* Reconfigure system clock (HSI → PLL), as Stop mode disables it */
    SystemClock_Config();

    return 0;
}

static void stm32_set_ready(bool asserted)
{
    HAL_GPIO_WritePin(SPI_READY_PORT, SPI_READY_PIN,
                      asserted ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static bool stm32_is_cs_asserted(void)
{
    return HAL_GPIO_ReadPin(SPI_CS_PORT, SPI_CS_PIN) == GPIO_PIN_RESET;
}

static void stm32_disable_wake_irq(void)
{
    HAL_NVIC_DisableIRQ(EXTI4_IRQn);
}

static void stm32_enable_wake_irq(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(SPI_CS_PIN);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
}

/* HAL ops table */
const WakeSpiHalOps stm32_wake_spi_ops = {
    .init            = stm32_init,
    .spi_reinit      = stm32_spi_reinit,
    .spi_deinit      = stm32_spi_deinit,
    .start_dma_rx    = stm32_start_dma_rx,
    .enter_deep_sleep = stm32_enter_deep_sleep,
    .set_ready_pin   = stm32_set_ready,
    .is_cs_asserted  = stm32_is_cs_asserted,
    .disable_wake_irq = stm32_disable_wake_irq,
    .enable_wake_irq  = stm32_enable_wake_irq,
};

/* ------------------------------------------------------------------ */
/* Interrupt handlers                                                  */
/* ------------------------------------------------------------------ */

/* Defined in the application — forwards to wake_spi_notify_cs_event() */
extern WakeSpiContext g_wake_spi_ctx;

void EXTI4_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(SPI_CS_PIN);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SPI_CS_PIN) {
        wake_spi_notify_cs_event(&g_wake_spi_ctx);
    }
}

void DMA1_Channel2_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_spi1_rx);
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        /* Mark transaction complete — wake_spi_run() will process it */
        g_wake_spi_ctx.current_txn.complete = true;
        /* In FreeRTOS: xTaskNotifyFromISR() or semaphore give here */
    }
}

/* ------------------------------------------------------------------ */
/* Application entry point                                             */
/* ------------------------------------------------------------------ */
WakeSpiContext g_wake_spi_ctx;

static void on_spi_rx(const SpiTransaction *txn, void *ctx)
{
    (void)ctx;
    /* Process received bytes — txn->rx_buf[0..txn->length-1] */
    /* E.g.: dispatch command byte, read sensor register, etc. */
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    wake_spi_init(&g_wake_spi_ctx, &stm32_wake_spi_ops,
                  on_spi_rx, NULL, NULL);

    for (;;) {
        wake_spi_run(&g_wake_spi_ctx);

        /* If no more work, go back to sleep */
        if (!g_wake_spi_ctx.current_txn.complete &&
            !(g_wake_spi_ctx.pending_events)) {
            wake_spi_sleep(&g_wake_spi_ctx);
        }
    }
}
```

---

### Nordic nRF52 Wake-on-SPI Example <a name="nordic-example"></a>

The nRF52 uses the `GPIOTE` module for edge detection and `SPIS` peripheral for slave mode.

```c
/* nrf52_wake_spi.c — nRF52 SDK implementation using nRF5 SDK 17 */

#include "nrf_gpio.h"
#include "nrf_gpiote.h"
#include "nrfx_spis.h"
#include "nrf_power.h"
#include "nrf_pwr_mgmt.h"

#define PIN_SPI_SCK    8
#define PIN_SPI_MOSI   7
#define PIN_SPI_MISO   6
#define PIN_SPI_CS     5   /* Must be wake-capable GPIOTE pin */
#define PIN_SPI_READY  4

static nrfx_spis_t m_spis = NRFX_SPIS_INSTANCE(2);

static uint8_t m_rx_buf[WAKE_SPI_MAX_PAYLOAD];
static uint8_t m_tx_buf[WAKE_SPI_MAX_PAYLOAD];

/* Volatile flag set in SPIS done callback */
static volatile bool m_transfer_done = false;

/* ------------------------------------------------------------------ */
/* SPIS event handler                                                  */
/* ------------------------------------------------------------------ */
static void spis_event_handler(nrfx_spis_evt_t const *p_event,
                               void                  *p_context)
{
    if (p_event->evt_type == NRFX_SPIS_XFER_DONE) {
        m_transfer_done = true;
        /* Signal main loop or RTOS task */
    }
}

/* ------------------------------------------------------------------ */
/* GPIOTE handler for CS# falling edge                                */
/* ------------------------------------------------------------------ */
void nrfx_gpiote_evt_handler(nrfx_gpiote_pin_t pin,
                              nrf_gpiote_polarity_t action)
{
    if (pin == PIN_SPI_CS && action == NRF_GPIOTE_POLARITY_HITOLO) {
        /* CS# falling — master wants to talk, abort sleep */
        /* nRF52: returning from this ISR resumes WFE/WFI */
    }
}

/* ------------------------------------------------------------------ */
/* Initialise GPIOTE for CS# wake detection                           */
/* ------------------------------------------------------------------ */
static void cs_wake_init(void)
{
    nrfx_gpiote_init(NRFX_GPIOTE_DEFAULT_CONFIG_IRQ_PRIORITY);

    nrfx_gpiote_in_config_t config = NRFX_GPIOTE_CONFIG_IN_SENSE_HITOLO(true);
    config.pull = NRF_GPIO_PIN_PULLUP;

    nrfx_gpiote_in_init(PIN_SPI_CS, &config, nrfx_gpiote_evt_handler);
    nrfx_gpiote_in_event_enable(PIN_SPI_CS, true);
}

/* ------------------------------------------------------------------ */
/* Initialise SPIS                                                     */
/* ------------------------------------------------------------------ */
static int nrf52_spis_init(void)
{
    nrfx_spis_config_t config = {
        .sck_pin   = PIN_SPI_SCK,
        .mosi_pin  = PIN_SPI_MOSI,
        .miso_pin  = PIN_SPI_MISO,
        .csn_pin   = PIN_SPI_CS,
        .mode      = NRF_SPIS_MODE_0,
        .bit_order = NRF_SPIS_BIT_ORDER_MSB_FIRST,
        .csn_pullup       = NRF_GPIO_PIN_PULLUP,
        .miso_drive       = NRF_GPIO_PIN_S0S1,
        .def              = 0xFF,
        .orc              = 0xFF,
        .irq_priority     = NRFX_SPIS_DEFAULT_CONFIG_IRQ_PRIORITY,
    };

    nrfx_err_t err = nrfx_spis_init(&m_spis, &config, spis_event_handler, NULL);
    if (err != NRFX_SUCCESS) return -1;

    /* Pre-arm the receive buffer */
    return nrfx_spis_buffers_set(&m_spis,
                                  m_tx_buf, sizeof(m_tx_buf),
                                  m_rx_buf, sizeof(m_rx_buf));
}

/* ------------------------------------------------------------------ */
/* Enter System OFF (deepest sleep on nRF52; wake via GPIO sense)     */
/* ------------------------------------------------------------------ */
static void nrf52_deep_sleep(void)
{
    /* Uninitialise SPIS to save power */
    nrfx_spis_uninit(&m_spis);

    /* Configure CS# as GPIO sense — wakes System OFF */
    nrf_gpio_cfg_sense_input(PIN_SPI_CS,
                             NRF_GPIO_PIN_PULLUP,
                             NRF_GPIO_PIN_SENSE_LOW);

    /* Flush any pending events, then power off */
    __SEV(); __WFE(); /* clear event latch */
    nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);
    /* Never returns — reset vector on wake */
}

/* ------------------------------------------------------------------ */
/* Alternatively, enter System ON low-power (WFE loop)               */
/* ------------------------------------------------------------------ */
static void nrf52_light_sleep_loop(void)
{
    while (!m_transfer_done) {
        /* Idle with CPU off, peripherals on */
        nrf_pwr_mgmt_run();   /* calls __WFE() internally */
    }

    m_transfer_done = false;

    /* Process received data in m_rx_buf */

    /* Re-arm buffers for next transaction */
    nrfx_spis_buffers_set(&m_spis,
                           m_tx_buf, sizeof(m_tx_buf),
                           m_rx_buf, sizeof(m_rx_buf));
}

int main(void)
{
    nrf_pwr_mgmt_init();
    cs_wake_init();
    nrf52_spis_init();

    /* Assert READY — slave is prepared to receive */
    nrf_gpio_pin_clear(PIN_SPI_READY);  /* Active-low READY */

    /* Main event loop using light sleep */
    for (;;) {
        nrf52_light_sleep_loop();
    }

    /* For deepest sleep (System OFF), call nrf52_deep_sleep() */
}
```

---

### Linux Kernel Driver Fragment <a name="linux-kernel-driver"></a>

On Linux-based systems (e.g., Raspberry Pi, i.MX8), Wake-on-SPI for a slave device is typically handled via device tree and a kernel driver that uses `spi_async()` or `regmap_spi` with a wake IRQ:

```c
/* wake_spi_linux.c — Linux SPI slave driver fragment (simplified) */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/pm_wakeup.h>
#include <linux/suspend.h>

struct wake_spi_dev {
    struct spi_device   *spi;
    struct gpio_desc    *cs_gpio;
    struct gpio_desc    *ready_gpio;
    int                  wake_irq;
    struct work_struct   rx_work;
    u8                   rx_buf[256];
    struct wakeup_source *ws;
};

/* Wake IRQ — fires on CS# falling edge */
static irqreturn_t wake_spi_irq_handler(int irq, void *dev_id)
{
    struct wake_spi_dev *dev = dev_id;

    /* Keep system awake while we process the transaction */
    pm_stay_awake(&dev->spi->dev);

    /* Defer SPI work to a workqueue (cannot do SPI in hardirq context) */
    schedule_work(&dev->rx_work);

    return IRQ_HANDLED;
}

static void wake_spi_rx_work(struct work_struct *work)
{
    struct wake_spi_dev *dev =
        container_of(work, struct wake_spi_dev, rx_work);

    struct spi_transfer xfer = {
        .rx_buf = dev->rx_buf,
        .len    = 64,
        .speed_hz = 1000000,
    };

    struct spi_message msg;
    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);

    /* Assert READY before transfer */
    gpiod_set_value(dev->ready_gpio, 1);

    int ret = spi_sync(dev->spi, &msg);

    gpiod_set_value(dev->ready_gpio, 0);

    if (!ret) {
        /* Process dev->rx_buf here */
        dev_dbg(&dev->spi->dev, "Received %d bytes, cmd=0x%02x\n",
                64, dev->rx_buf[0]);
    }

    /* Allow system to suspend again */
    pm_relax(&dev->spi->dev);
}

static int wake_spi_probe(struct spi_device *spi)
{
    struct wake_spi_dev *dev;
    int ret;

    dev = devm_kzalloc(&spi->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;

    dev->spi = spi;

    dev->ready_gpio = devm_gpiod_get(&spi->dev, "ready", GPIOD_OUT_LOW);
    if (IS_ERR(dev->ready_gpio))
        return PTR_ERR(dev->ready_gpio);

    dev->cs_gpio = devm_gpiod_get(&spi->dev, "cs-wake", GPIOD_IN);
    if (IS_ERR(dev->cs_gpio))
        return PTR_ERR(dev->cs_gpio);

    dev->wake_irq = gpiod_to_irq(dev->cs_gpio);
    if (dev->wake_irq < 0) return dev->wake_irq;

    INIT_WORK(&dev->rx_work, wake_spi_rx_work);

    /* Register as a wakeup source — device can wake system from suspend */
    device_init_wakeup(&spi->dev, true);

    ret = devm_request_irq(&spi->dev, dev->wake_irq,
                           wake_spi_irq_handler,
                           IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                           "wake-spi-cs", dev);
    if (ret) return ret;

    /* Allow this IRQ to wake the system from Linux suspend */
    enable_irq_wake(dev->wake_irq);

    spi_set_drvdata(spi, dev);
    dev_info(&spi->dev, "Wake-on-SPI driver bound\n");
    return 0;
}

static const struct of_device_id wake_spi_of_match[] = {
    { .compatible = "vendor,wake-spi-slave" },
    {}
};
MODULE_DEVICE_TABLE(of, wake_spi_of_match);

static struct spi_driver wake_spi_driver = {
    .driver = {
        .name           = "wake-spi",
        .of_match_table = wake_spi_of_match,
    },
    .probe  = wake_spi_probe,
};
module_spi_driver(wake_spi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Wake-on-SPI slave driver");
```

**Corresponding device tree node:**
```dts
/* Device tree overlay fragment */
&spi1 {
    wake_spi_slave@0 {
        compatible = "vendor,wake-spi-slave";
        reg = <0>;
        spi-max-frequency = <1000000>;
        ready-gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>;
        cs-wake-gpios = <&gpio0 4 GPIO_ACTIVE_LOW>;
        wakeup-source;
    };
};
```

---

## 7. Implementation in Rust <a name="implementation-in-rust"></a>

### Embedded Rust with RTIC and nRF52 <a name="embedded-rust-rtic"></a>

RTIC (Real-Time Interrupt-driven Concurrency) provides a safe, priority-based interrupt framework for Cortex-M systems.

```rust
// wake_spi_rtic.rs — nRF52840 Wake-on-SPI using RTIC 2.0

#![no_std]
#![no_main]

use nrf52840_hal::{
    gpio::{self, Input, Output, Pin, PullUp, PushPull},
    gpiote::Gpiote,
    pac,
    spis::{self, Spis, Transfer},
    prelude::*,
};
use rtic::app;

/// Maximum SPI payload size in bytes
const MAX_PAYLOAD: usize = 256;

/// Application-level SPI command byte
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpiCommand {
    ReadSensor  = 0x01,
    WriteConfig = 0x02,
    Status      = 0x03,
    Unknown,
}

impl From<u8> for SpiCommand {
    fn from(b: u8) -> Self {
        match b {
            0x01 => Self::ReadSensor,
            0x02 => Self::WriteConfig,
            0x03 => Self::Status,
            _    => Self::Unknown,
        }
    }
}

#[app(device = nrf52840_hal::pac, peripherals = true, dispatchers = [SWI0_EGU0])]
mod app {
    use super::*;
    use nrf52840_hal::pac::SPIS2;

    /// Shared resources
    #[shared]
    struct Shared {
        /// SPIS handle (with ownership of rx/tx buffers)
        spis: Option<Spis<SPIS2>>,
        /// READY output pin (active-low)
        ready_pin: Pin<Output<PushPull>>,
        /// Indicates a transfer is in progress
        transfer_pending: bool,
    }

    /// Local (non-shared, per-task) resources
    #[local]
    struct Local {
        gpiote: Gpiote,
        rx_buf: [u8; MAX_PAYLOAD],
        tx_buf: [u8; MAX_PAYLOAD],
    }

    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        let p = cx.device;
        let port0 = gpio::p0::Parts::new(p.P0);

        // Configure pins
        let cs_pin   = port0.p0_05.into_pullup_input().degrade();
        let ready_pin = port0.p0_04.into_push_pull_output(gpio::Level::High).degrade();
        let sck  = port0.p0_08.into_floating_input().degrade();
        let mosi = port0.p0_07.into_floating_input().degrade();
        let miso = port0.p0_06.into_push_pull_output(gpio::Level::Low).degrade();

        // Configure GPIOTE channel 0 for CS# falling edge
        let gpiote = Gpiote::new(p.GPIOTE);
        gpiote.channel0()
            .input_pin(&cs_pin)
            .hi_to_lo()
            .enable_interrupt();

        // Initialise SPIS
        let spis_config = spis::Config::default()
            .mode(spis::Mode::Mode0)
            .order(spis::Order::MsbFirst);

        let spis = Spis::new(p.SPIS2, sck, mosi, miso, cs_pin, spis_config);

        let rx_buf = [0u8; MAX_PAYLOAD];
        let tx_buf = [0u8; MAX_PAYLOAD];

        (
            Shared { spis: Some(spis), ready_pin, transfer_pending: false },
            Local  { gpiote, rx_buf, tx_buf },
        )
    }

    /// CS# falling edge — master is asserting chip select
    #[task(binds = GPIOTE, local = [gpiote, rx_buf, tx_buf],
           shared = [spis, ready_pin, transfer_pending],
           priority = 2)]
    fn cs_assert(cx: cs_assert::Context) {
        cx.local.gpiote.channel0().reset_events();

        let (mut spis, mut ready, mut pending) = (
            cx.shared.spis,
            cx.shared.ready_pin,
            cx.shared.transfer_pending,
        );

        (&mut spis, &mut ready, &mut pending).lock(|spis, ready, pending| {
            if *pending { return; }  // Already in a transfer

            // Prepare a response in tx_buf (application-specific)
            cx.local.tx_buf[0] = 0xAC; // ACK byte
            cx.local.tx_buf[1] = 0x00;

            if let Some(s) = spis.take() {
                // Assert READY (active-low) to tell master we're awake
                ready.set_low().ok();

                // Start DMA transfer — returns Transfer<SPIS2> (moved ownership)
                // Note: in real nRF HAL use `transfer` or `transfer_split`
                *pending = true;
                // Re-arm with buffers
                *spis = Some(s);  // simplified; real code: start_transfer here
            }
        });
    }

    /// SPIS END event — DMA transfer complete
    #[task(binds = SPIM2_SPIS2_SPI2, local = [rx_buf],
           shared = [spis, ready_pin, transfer_pending],
           priority = 2)]
    fn spis_end(cx: spis_end::Context) {
        let (mut spis, mut ready, mut pending) = (
            cx.shared.spis,
            cx.shared.ready_pin,
            cx.shared.transfer_pending,
        );

        (&mut spis, &mut ready, &mut pending).lock(|_spis, ready, pending| {
            ready.set_high().ok();  // De-assert READY
            *pending = false;

            // Dispatch the received command
            let cmd = SpiCommand::from(cx.local.rx_buf[0]);
            process_command::spawn(cmd, cx.local.rx_buf[1]).ok();
        });
    }

    /// Software task — process decoded SPI command
    #[task(priority = 1, capacity = 4)]
    async fn process_command(_cx: process_command::Context, cmd: SpiCommand, arg: u8) {
        match cmd {
            SpiCommand::ReadSensor  => { /* read sensor, populate next tx_buf */ }
            SpiCommand::WriteConfig => { /* apply config from arg */ }
            SpiCommand::Status      => { /* prepare status reply */ }
            SpiCommand::Unknown     => { /* log error */ }
        }
        let _ = (cmd, arg); // suppress unused warning in example
    }
}
```

---

### Rust async/await with Embassy <a name="rust-embassy"></a>

Embassy provides async/await-native embedded Rust, with built-in support for low-power wait states.

```rust
// wake_spi_embassy.rs — Embassy async Wake-on-SPI on STM32L4

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::{
    bind_interrupts,
    exti::ExtiInput,
    gpio::{Input, Level, Output, Pull, Speed},
    peripherals,
    spi::{self, Spi},
    time::Hertz,
};
use embassy_stm32::spi::Config as SpiConfig;
use embassy_time::{Duration, Timer};
use defmt::info;

bind_interrupts!(struct Irqs {
    SPI1 => spi::InterruptHandler<peripherals::SPI1>;
    EXTI4 => embassy_stm32::exti::InterruptHandler;
});

const MAX_PAYLOAD: usize = 64;

/// Prepare a response buffer based on received command
fn prepare_response(cmd: u8, tx: &mut [u8]) {
    tx[0] = match cmd {
        0x01 => 0xDA,  // sensor data
        0x02 => 0xAC,  // config acknowledged
        _    => 0xFF,  // unknown
    };
    // Fill remaining bytes as needed
    for b in &mut tx[1..] { *b = 0; }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    // SPI1 slave on PA5/PA6/PA7
    let mut spi_config = SpiConfig::default();
    spi_config.frequency = Hertz(4_000_000);

    let spi = Spi::new(
        p.SPI1,
        p.PA5,   // SCK
        p.PA7,   // MOSI
        p.PA6,   // MISO
        p.DMA1_CH3,
        p.DMA1_CH2,
        spi_config,
    );

    // CS# as EXTI input — falling edge wakes the async task
    let cs_in = ExtiInput::new(p.PA4, p.EXTI4, Pull::Up);

    // READY output (active-low)
    let mut ready = Output::new(p.PA3, Level::High, Speed::Low);

    spawner.spawn(spi_slave_task(spi, cs_in, ready)).unwrap();
}

#[embassy_executor::task]
async fn spi_slave_task(
    mut spi: Spi<'static, peripherals::SPI1,
                 peripherals::DMA1_CH3, peripherals::DMA1_CH2>,
    mut cs_in: ExtiInput<'static, peripherals::PA4>,
    mut ready: Output<'static, peripherals::PA3>,
)
{
    let mut rx_buf = [0u8; MAX_PAYLOAD];
    let mut tx_buf = [0u8; MAX_PAYLOAD];

    loop {
        // ── SLEEP phase ──────────────────────────────────────────────
        // await() suspends the task; Embassy's executor calls WFE,
        // placing the CPU in low-power mode until the EXTI fires.
        info!("Waiting for CS# assertion (low-power wait)...");
        cs_in.wait_for_falling_edge().await;

        // ── WAKE phase ───────────────────────────────────────────────
        info!("CS# asserted — waking up");

        // Small settle delay for SPI clock to stabilise
        Timer::after(Duration::from_micros(50)).await;

        // Assert READY to master
        ready.set_low();

        // Receive data via DMA
        rx_buf.fill(0);
        if let Err(e) = spi.read(&mut rx_buf[..MAX_PAYLOAD]).await {
            defmt::error!("SPI receive error: {:?}", e);
            ready.set_high();
            continue;
        }

        info!("Received cmd=0x{:02X}", rx_buf[0]);

        // Prepare and send response
        prepare_response(rx_buf[0], &mut tx_buf);
        if let Err(e) = spi.write(&tx_buf[..MAX_PAYLOAD]).await {
            defmt::error!("SPI transmit error: {:?}", e);
        }

        // De-assert READY; wait for CS# to go high (master done)
        ready.set_high();
        cs_in.wait_for_rising_edge().await;

        info!("CS# de-asserted — transaction complete");

        // ── Return to sleep phase (loop back to await) ────────────────
    }
}
```

---

### Rust Linux Userspace Daemon <a name="rust-linux-userspace"></a>

For Linux-based systems, a Rust userspace daemon can manage a SPI slave device with wake-from-suspend support via `spidev` and `gpio-cdev`.

```rust
// wake_spi_daemon.rs — Linux userspace Wake-on-SPI daemon in Rust
// Cargo.toml deps: spidev = "0.6", gpiocdev = "0.6", tokio = { features = ["full"] }

use std::io::{Read, Write};
use spidev::{Spidev, SpidevOptions, SpiModeFlags, SpidevTransfer};
use gpiocdev::{Request, line::{EdgeDetection, EdgeKind, Offset, Value}};
use tokio::sync::watch;

const SPI_DEV: &str    = "/dev/spidev1.0";
const GPIO_CHIP: &str  = "/dev/gpiochip0";
const CS_LINE:  Offset = 4;   // GPIO line for CS# (wake source)
const READY_LINE: Offset = 3; // GPIO line for READY output

fn open_spi() -> anyhow::Result<Spidev> {
    let mut spi = Spidev::open(SPI_DEV)?;
    spi.configure(&SpidevOptions::new()
        .bits_per_word(8)
        .max_speed_hz(4_000_000)
        .mode(SpiModeFlags::SPI_MODE_0)
        .build())?;
    Ok(spi)
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    // Open GPIO chip for CS# edge detection
    let chip = gpiocdev::Chip::open(GPIO_CHIP)?;

    // Request CS# line with falling-edge detection
    let cs_req = chip.request_lines(
        gpiocdev::Options::input([CS_LINE])
            .with_edge_detection(EdgeDetection::FallingEdge)
            .with_pull_up(true)
            .consumer("wake-spi-cs"),
    )?;

    // Request READY output line (active-low, initially de-asserted = high)
    let ready_req = chip.request_lines(
        gpiocdev::Options::output([READY_LINE])
            .with_values([Value::Active])
            .consumer("wake-spi-ready"),
    )?;

    let spi = open_spi()?;
    let mut rx_buf = vec![0u8; 256];
    let mut tx_buf = vec![0u8; 256];

    println!("Wake-on-SPI daemon running. Waiting for CS# edge...");

    loop {
        // Block until CS# falling edge (system may suspend while blocking)
        let edge = cs_req.read_edge_event()?;

        if edge.kind == EdgeKind::Falling {
            println!("CS# asserted — processing SPI transaction");

            // Assert READY (active-high signal; pull low for active-low READY)
            ready_req.set_values([READY_LINE], [Value::Inactive])?; // LOW

            // Perform full-duplex SPI transfer
            rx_buf.fill(0);
            tx_buf[0] = 0xAC; // pre-fill acknowledge byte

            let mut xfer = SpidevTransfer::read_write(&tx_buf, &mut rx_buf);
            spi.transfer(&mut xfer)?;

            println!("Received: cmd=0x{:02X} arg=0x{:02X}", rx_buf[0], rx_buf[1]);

            // De-assert READY
            ready_req.set_values([READY_LINE], [Value::Active])?; // HIGH

            // Process command asynchronously
            let cmd = rx_buf[0];
            tokio::spawn(async move {
                handle_spi_command(cmd).await;
            });
        }
    }
}

async fn handle_spi_command(cmd: u8) {
    match cmd {
        0x01 => println!("Handling ReadSensor command"),
        0x02 => println!("Handling WriteConfig command"),
        0x03 => println!("Handling Status request"),
        _    => eprintln!("Unknown SPI command: 0x{:02X}", cmd),
    }
    // In a real daemon: interact with kernel drivers, databases, etc.
}
```

---

## 8. Protocol Design Considerations <a name="protocol-design"></a>

### Framing and Command Structure

A minimal Wake-on-SPI protocol should include:

```
Byte 0:  Command / opcode
Byte 1:  Payload length (N)
Byte 2:  Flags / sequence number
Byte 3..3+N-1: Payload data
Last byte: CRC-8 or checksum
```

Example C struct:

```c
typedef struct __attribute__((packed)) {
    uint8_t  cmd;           /* Operation code */
    uint8_t  length;        /* Payload bytes following header */
    uint8_t  seq;           /* Rolling sequence number for dedup */
    uint8_t  payload[252];  /* Up to 252 bytes of payload */
    uint8_t  crc;           /* CRC-8/MAXIM of cmd+length+seq+payload */
} WakeSpiFrame;

/* CRC-8 (Dallas/Maxim polynomial 0x31) */
uint8_t crc8_maxim(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
    }
    return crc;
}
```

### Timing Requirements

| Parameter | Typical Value | Notes |
|-----------|--------------|-------|
| CS# assert to READY assert | 100–500 µs | CPU wake + SPI init time |
| READY assert to first SCK | ≥ 10 µs | Master must wait for READY |
| Min CS# high time between transactions | 10–100 µs | Slave re-arming time |
| Max CS# assertion before timeout | 50–200 ms | Abort if READY never comes |

---

## 9. Power Budgeting <a name="power-budgeting"></a>

Accurate power budgeting requires accounting for:

```
Average current (I_avg) = 
  (I_active × t_wake + I_sleep × t_sleep) / (t_wake + t_sleep)

Example (STM32L4, 1 transaction/second, 5 ms wake duration):
  I_active  = 5 mA  (run mode @ 4 MHz + SPI DMA)
  I_sleep   = 2 µA  (Stop 2 mode with SRAM retention)
  t_wake    = 5 ms
  t_sleep   = 995 ms

  I_avg = (5000 µA × 5 + 2 µA × 995) / 1000
        = (25000 + 1990) / 1000
        ≈ 27 µA average

Battery life (1000 mAh, 80% efficiency):
  = 1000 mAh × 0.80 / 0.027 mA ≈ 29,600 hours ≈ 3.4 years
```

Key power optimisations:

- Minimise wake time: pre-compute TX data before CS#, use DMA not polling.
- Use the lowest power sleep mode that still supports the wake source.
- On nRF52: System OFF gives ~0.4 µA but requires full re-boot on wake.
- Disable unused peripherals (ADC, UART, USB) before entering sleep.
- Pull MISO to Hi-Z (not driven) during sleep to avoid driving the bus.

---

## 10. Debugging Wake-on-SPI Systems <a name="debugging"></a>

### Common Issues and Solutions

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| System does not wake | CS# EXTI not configured as wake source | Check `HAL_NVIC_EnableIRQ` + EXTI mask |
| Spurious wake events | CS# line floating or noise on SCK | Add pull-up + RC filter on CS# |
| First byte corrupted | Master sends before READY asserted | Ensure master waits for READY |
| MISO bus contention | MISO not Hi-Z when SPI is off | Set MISO to analog/input when SPI disabled |
| Deadlock after wake | SPI reinit fails silently | Log SPI init return codes; check clock gating |
| RTOS task not unblocking | Semaphore give in ISR not working | Use `FromISR` variants in FreeRTOS |
| nRF52 System OFF no wake | GPIO sense not configured | `nrf_gpio_cfg_sense_input()` required |

### Debug Instrumentation

```c
/* Minimal logic-analyser trigger output — toggle debug pin in ISR */
#define DBG_TOGGLE_CS_WAKE()  \
    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0)  /* Connect to LA channel */

/* Timestamping wake events */
typedef struct {
    uint32_t cs_assert_tick;
    uint32_t spi_ready_tick;
    uint32_t dma_done_tick;
    uint32_t sleep_entry_tick;
} WakeSpiTimings;

void wake_spi_record_timing(WakeSpiTimings *t, WakePhase phase)
{
    uint32_t now = DWT->CYCCNT;  /* ARM Cortex-M cycle counter */
    switch (phase) {
        case WAKE_PHASE_CS_ASSERT:  t->cs_assert_tick  = now; break;
        case WAKE_PHASE_READY:      t->spi_ready_tick  = now; break;
        case WAKE_PHASE_DMA_DONE:   t->dma_done_tick   = now; break;
        case WAKE_PHASE_SLEEP:      t->sleep_entry_tick = now; break;
    }
}
```

---

## 11. Summary <a name="summary"></a>

Wake-on-SPI is an essential technique for ultra-low-power embedded system design, enabling a microcontroller to spend most of its time in deep sleep while remaining responsive to external SPI masters. The key takeaways are:

**Hardware design** — Because standard SPI is master-clocked and most SPI peripherals power down in deep sleep, a dedicated wake signal (CS# edge or a WAKE pin) must be routed to a wake-capable GPIO. The slave must safely tri-state MISO and re-initialise the SPI peripheral quickly upon waking. A READY handshake pin prevents data loss during the wake-up window.

**C/C++ implementation** — The pattern separates concerns into a platform-agnostic state machine (`WakeSpiContext`) and a hardware-specific HAL ops table. On STM32, Stop mode 2 with EXTI on CS# and DMA-based SPI reception achieves microamp-level average current. On nRF52, GPIOTE sense and SPIS/DMA achieve similar results with the Nordic SDK.

**Rust implementation** — Embedded Rust shines here through memory-safe peripheral ownership (no two tasks can hold the SPI at once), RTIC's compile-time checked concurrency, and Embassy's async/await model where `cs_in.wait_for_falling_edge().await` cleanly maps to a low-power WFE instruction. The borrow checker prevents entire classes of race conditions common in C ISR-driven code.

**Protocol design** — Wake-on-SPI requires careful framing, sequence numbering, and CRC integrity checking because the first bytes arrive immediately after an unreliable wake transition. The READY handshake is strongly recommended over CS#-only designs.

**Power budgeting** — With a typical 5 ms active window and 1 transaction/second, average current drops to ~27 µA on STM32L4, extending battery life from hours to years.

The combination of thoughtful hardware pin assignment, a clean state machine, and DMA-backed SPI makes Wake-on-SPI both robust and highly power-efficient in production systems.

---

*Document: 75_Wake_On_SPI_Mechanisms.md — Part of the SPI Programming Reference Series*