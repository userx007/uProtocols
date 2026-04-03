# 59. Event Groups for SPI Status

**Structure:**
- **Background** — why SPI + RTOS creates a synchronisation challenge and why polling/semaphores fall short
- **Event Groups theory** — bit definitions, AND/OR wait semantics, ISR-safe vs task-context API
- **Architecture diagram** — ASCII art showing the ISR → event group → task signal flow

**C/C++ (FreeRTOS) examples:**
- Full bit definitions header (`spi_events.h`)
- SPI driver init with event group creation (including static allocation variant)
- `SPI_TransferDMA()` showing the clear-before-transfer → start DMA → `xEventGroupWaitBits()` pattern
- All four HAL DMA callbacks (`TxCplt`, `RxCplt`, `TxRxCplt`, `ErrorCallback`) with correct `FromISR` + `portYIELD_FROM_ISR` usage
- Multi-device pattern using distinct bit groups per device

**Rust examples:**
- **Embassy** — native async `spi.transfer()` with `with_timeout`
- **Embassy** — manual `SpiEventGroup` struct over `AtomicU32` + `AtomicWaker` for explicit multi-bit semantics
- **RTIC** — typed channel bridging a DMA hardware task to a software task

**Pitfalls section** highlights the most common mistakes: using the non-ISR variant in interrupts, forgetting to clear stale bits, infinite waits, and bit exhaustion.

## Using RTOS Event Groups to Signal SPI Transaction Completion

---

## Table of Contents

1. [Introduction](#introduction)
2. [Background: SPI and RTOS Challenges](#background)
3. [What Are Event Groups?](#what-are-event-groups)
4. [Architecture Overview](#architecture-overview)
5. [Implementation in C/C++ (FreeRTOS)](#implementation-c)
6. [Implementation in Rust (Embassy / RTIC)](#implementation-rust)
7. [Advanced Patterns](#advanced-patterns)
8. [Pitfalls and Best Practices](#pitfalls)
9. [Summary](#summary)

---

## 1. Introduction 

In embedded systems using a Real-Time Operating System (RTOS), SPI (Serial Peripheral Interface) transactions are typically initiated by one task but completed asynchronously via an interrupt or DMA callback. Coordinating this producer-consumer relationship cleanly — without busy-waiting or polling — is a core challenge in real-time firmware design.

**Event Groups** (also called event flags or event sets) are a lightweight RTOS synchronization primitive perfectly suited to this problem. They allow one or more tasks to *wait* for specific bit-level conditions signalled by ISRs or other tasks, making them ideal for signalling SPI transaction completion, errors, or readiness.

This document explores the concept in depth with complete, annotated code examples in **C/C++ (FreeRTOS)** and **Rust (Embassy + RTIC)**.

---

## 2. Background: SPI and RTOS Challenges 

### SPI Recap

SPI is a synchronous, full-duplex serial bus protocol with four signals:

| Signal | Direction | Description               |
|--------|-----------|---------------------------|
| SCLK   | Master→Slave | Clock signal           |
| MOSI   | Master→Slave | Master Out Slave In    |
| MISO   | Slave→Master | Master In Slave Out    |
| CS/NSS | Master→Slave | Chip Select (active low)|

Transfers can be polled, interrupt-driven, or DMA-assisted. In RTOS contexts, **DMA-driven SPI** is most common for efficiency — the CPU initiates the transfer and is notified on completion.

### The Synchronization Problem

```
Task A:  [initiate SPI TX/RX] -------- wait ------> [process result]
                                           ^
ISR/DMA:                            [signal completion]
```

Naive solutions include:

- **Busy-wait / polling** — wastes CPU cycles, defeats RTOS scheduling.
- **Global flags** — race conditions without proper atomic access.
- **Semaphores** — work, but limited to a single binary signal.
- **Event Groups** — multiple status bits, multiple waiters, atomic operations. ✅

---

## 3. What Are Event Groups? 

An **event group** is essentially a fixed-width bitmask (typically 24 usable bits in FreeRTOS) stored in an RTOS object. Each bit represents an independent event or status condition.

### Key Operations

| Operation | Description |
|-----------|-------------|
| `xEventGroupCreate()` | Allocate and initialise the event group |
| `xEventGroupSetBits()` | Set one or more bits (from task or ISR) |
| `xEventGroupClearBits()` | Clear one or more bits |
| `xEventGroupWaitBits()` | Block until specified bits are set (with timeout) |
| `xEventGroupGetBits()` | Non-blocking read of current bits |

### Advantages Over Semaphores for SPI

- **Multiple status bits in one object**: TX complete, RX complete, error, bus busy — all in a single event group.
- **Multi-task wake**: Multiple tasks can wait on the same group for different bits.
- **AND / OR semantics**: Wait for *all* specified bits (AND) or *any* of them (OR).
- **ISR-safe variants**: `xEventGroupSetBitsFromISR()` for DMA/interrupt callbacks.

### Bit Assignment Convention (SPI Example)

```c
#define SPI_EVT_TX_COMPLETE   (1 << 0)   // Bit 0: TX DMA done
#define SPI_EVT_RX_COMPLETE   (1 << 1)   // Bit 1: RX DMA done
#define SPI_EVT_ERROR         (1 << 2)   // Bit 2: Bus/DMA error
#define SPI_EVT_BUS_FREE      (1 << 3)   // Bit 3: SPI bus released
#define SPI_EVT_TRANSFER_DONE (SPI_EVT_TX_COMPLETE | SPI_EVT_RX_COMPLETE)
```

---

## 4. Architecture Overview 

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Tasks                        │
│                                                                 │
│  ┌──────────────────┐        ┌──────────────────────────────┐   │
│  │   Sensor Task    │        │   Display Task               │   │
│  │                  │        │                              │   │
│  │ spi_transfer()   │        │ spi_transfer()               │   │
│  │   ↓              │        │   ↓                          │   │
│  │ xEventGroupWait  │        │ xEventGroupWait              │   │
│  │   (BLOCKED)      │        │   (BLOCKED)                  │   │
│  └──────────────────┘        └──────────────────────────────┘   │
│              ↑                           ↑                      │
│              └───────────────────────────┘                      │
│                        Event Group                              │
│                    [TX|RX|ERR|FREE|...]                         │
│                              ↑                                  │
│                    xEventGroupSetBitsFromISR()                  │
│                              │                                  │
│  ┌───────────────────────────┴─────────────────────────────┐    │
│  │              SPI DMA Complete ISR / Callback            │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

The event group acts as a **shared signalling hub** between the hardware interrupt layer and application tasks. No shared memory, no race conditions on the signalling path.

---

## 5. Implementation in C/C++ (FreeRTOS) 

### 5.1 Header and Bit Definitions

```c
// spi_events.h
#ifndef SPI_EVENTS_H
#define SPI_EVENTS_H

#include "FreeRTOS.h"
#include "event_groups.h"

// ---------------------------------------------------------------
// Event bit definitions for SPI status event group
// FreeRTOS reserves the top 8 bits for internal use in 32-bit builds
// → 24 usable bits (bits 0–23)
// ---------------------------------------------------------------
#define SPI_EVT_TX_COMPLETE   ( 1UL << 0 )   // Transmit DMA finished
#define SPI_EVT_RX_COMPLETE   ( 1UL << 1 )   // Receive DMA finished
#define SPI_EVT_ERROR         ( 1UL << 2 )   // Hardware/DMA error
#define SPI_EVT_BUS_FREE      ( 1UL << 3 )   // CS deasserted, bus idle
#define SPI_EVT_OVERRUN       ( 1UL << 4 )   // Overrun error flag

// Compound: full-duplex transfer needs both TX and RX done
#define SPI_EVT_TRANSFER_DONE ( SPI_EVT_TX_COMPLETE | SPI_EVT_RX_COMPLETE )
#define SPI_EVT_ANY_ERROR     ( SPI_EVT_ERROR | SPI_EVT_OVERRUN )

// Timeout for waiting on SPI completion (ms → ticks)
#define SPI_WAIT_TIMEOUT_MS   100
#define SPI_WAIT_TICKS        pdMS_TO_TICKS( SPI_WAIT_TIMEOUT_MS )

// Shared event group handle (defined in spi_driver.c)
extern EventGroupHandle_t xSpiEventGroup;

#endif // SPI_EVENTS_H
```

---

### 5.2 SPI Driver Initialisation

```c
// spi_driver.c
#include "spi_events.h"
#include "stm32f4xx_hal.h"  // Example: STM32 HAL

// Global event group handle - created once, used by all SPI tasks/ISRs
EventGroupHandle_t xSpiEventGroup = NULL;

// SPI peripheral handle (HAL)
static SPI_HandleTypeDef hspi1;

/**
 * @brief  Initialise SPI peripheral and create the RTOS event group.
 * @retval pdPASS on success, pdFAIL on error
 */
BaseType_t SPI_Driver_Init(void)
{
    // --- Create the event group ---
    xSpiEventGroup = xEventGroupCreate();
    if (xSpiEventGroup == NULL) {
        // Insufficient heap memory
        return pdFAIL;
    }

    // --- Configure SPI1 for DMA-driven full-duplex ---
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        return pdFAIL;
    }

    return pdPASS;
}
```

---

### 5.3 Initiating a Transfer and Waiting for Completion

```c
/**
 * @brief  Perform a full-duplex SPI transfer using DMA, then block on
 *         the event group until TX+RX complete or a timeout/error occurs.
 *
 * @param  pTxData  Pointer to transmit buffer
 * @param  pRxData  Pointer to receive buffer (may be NULL for TX-only)
 * @param  size     Number of bytes to transfer
 * @retval HAL_OK on success, HAL_TIMEOUT or HAL_ERROR on failure
 */
HAL_StatusTypeDef SPI_TransferDMA(
    uint8_t *pTxData,
    uint8_t *pRxData,
    uint16_t size)
{
    EventBits_t uxBitsToWait;
    EventBits_t uxBitsReturned;
    HAL_StatusTypeDef status;

    // --- Clear any stale bits before starting ---
    xEventGroupClearBits(xSpiEventGroup,
                         SPI_EVT_TX_COMPLETE  |
                         SPI_EVT_RX_COMPLETE  |
                         SPI_EVT_ERROR        |
                         SPI_EVT_OVERRUN);

    // --- Determine which completion events we expect ---
    if (pRxData != NULL) {
        // Full-duplex: need both TX and RX complete
        uxBitsToWait = SPI_EVT_TRANSFER_DONE;
        status = HAL_SPI_TransmitReceive_DMA(&hspi1, pTxData, pRxData, size);
    } else {
        // TX-only
        uxBitsToWait = SPI_EVT_TX_COMPLETE;
        status = HAL_SPI_Transmit_DMA(&hspi1, pTxData, size);
    }

    if (status != HAL_OK) {
        return status;
    }

    // --- Block this task until the desired bits are set ---
    // Parameters:
    //   xEventGroup      - the group to wait on
    //   uxBitsToWaitFor  - bitmask of required bits
    //   xClearOnExit     - pdTRUE: auto-clear the bits after waking
    //   xWaitForAllBits  - pdTRUE: AND semantics (all bits required)
    //   xTicksToWait     - timeout
    uxBitsReturned = xEventGroupWaitBits(
        xSpiEventGroup,
        uxBitsToWait | SPI_EVT_ANY_ERROR,  // Also wake on error
        pdTRUE,                             // Clear matching bits on exit
        pdFALSE,                            // OR: wake on any matching bit
        SPI_WAIT_TICKS
    );

    // --- Evaluate result ---
    if ((uxBitsReturned & SPI_EVT_ANY_ERROR) != 0) {
        return HAL_ERROR;
    }

    if ((uxBitsReturned & uxBitsToWait) == uxBitsToWait) {
        return HAL_OK;   // All expected bits were set → success
    }

    // Bits not set within timeout → timed out
    HAL_SPI_Abort(&hspi1);
    return HAL_TIMEOUT;
}
```

---

### 5.4 ISR / DMA Callbacks (Setting Bits from Interrupt Context)

```c
// -------------------------------------------------------------------
// HAL DMA callbacks — called from interrupt context
// MUST use *FromISR variants of FreeRTOS API
// -------------------------------------------------------------------

/**
 * @brief  TX DMA complete callback (called by HAL from DMA IRQ handler).
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1) return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Set the TX complete bit; if a higher-priority task was unblocked,
    // request a context switch at the end of the ISR.
    xEventGroupSetBitsFromISR(
        xSpiEventGroup,
        SPI_EVT_TX_COMPLETE,
        &xHigherPriorityTaskWoken
    );

    // Yield to the newly unblocked task if it has higher priority
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  RX DMA complete callback.
 */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1) return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xEventGroupSetBitsFromISR(
        xSpiEventGroup,
        SPI_EVT_RX_COMPLETE,
        &xHigherPriorityTaskWoken
    );

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  TX+RX DMA complete callback (full-duplex transfers).
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1) return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Set both TX and RX complete bits atomically
    xEventGroupSetBitsFromISR(
        xSpiEventGroup,
        SPI_EVT_TX_COMPLETE | SPI_EVT_RX_COMPLETE,
        &xHigherPriorityTaskWoken
    );

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  SPI error callback.
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1) return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    uint32_t errorCode = HAL_SPI_GetError(hspi);
    EventBits_t bitsToSet = SPI_EVT_ERROR;

    if (errorCode & HAL_SPI_ERROR_OVR) {
        bitsToSet |= SPI_EVT_OVERRUN;
    }

    xEventGroupSetBitsFromISR(
        xSpiEventGroup,
        bitsToSet,
        &xHigherPriorityTaskWoken
    );

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

---

### 5.5 Application Task Example

```c
// sensor_task.c — reads a temperature sensor over SPI

#include "spi_events.h"
#include "FreeRTOS.h"
#include "task.h"

#define SENSOR_READ_CMD    0x03   // Example read command byte
#define SENSOR_DATA_LEN    4      // 4 bytes response

static uint8_t txBuf[SENSOR_DATA_LEN] = { SENSOR_READ_CMD, 0x00, 0x00, 0x00 };
static uint8_t rxBuf[SENSOR_DATA_LEN];

void vSensorTask(void *pvParameters)
{
    HAL_StatusTypeDef result;

    for (;;)
    {
        // Initiate transfer and block until done (or timeout/error)
        result = SPI_TransferDMA(txBuf, rxBuf, SENSOR_DATA_LEN);

        switch (result)
        {
            case HAL_OK:
                // Process received sensor data
                ProcessSensorData(rxBuf, SENSOR_DATA_LEN);
                break;

            case HAL_TIMEOUT:
                LogError("SPI sensor read timed out");
                break;

            case HAL_ERROR:
                LogError("SPI sensor read error");
                // Optionally re-initialise SPI here
                break;

            default:
                break;
        }

        // Wait before next sample (250 ms)
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
```

---

### 5.6 Multi-Device Pattern: Per-Device Event Bits

When multiple SPI devices share one bus, assign unique bit pairs to each:

```c
// Multi-device event bit layout
#define SPI_DEV_A_TX_DONE   ( 1UL << 0 )
#define SPI_DEV_A_RX_DONE   ( 1UL << 1 )
#define SPI_DEV_A_ERROR     ( 1UL << 2 )

#define SPI_DEV_B_TX_DONE   ( 1UL << 3 )
#define SPI_DEV_B_RX_DONE   ( 1UL << 4 )
#define SPI_DEV_B_ERROR     ( 1UL << 5 )

#define SPI_DEV_C_TX_DONE   ( 1UL << 6 )
#define SPI_DEV_C_RX_DONE   ( 1UL << 7 )
#define SPI_DEV_C_ERROR     ( 1UL << 8 )

// Each device's task waits only on its own bits
// ISR determines which device's transfer completed and sets the right bits
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    BaseType_t xHPTW = pdFALSE;
    EventBits_t bits = 0;

    // Identify which CS was active to determine device
    if (GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_RESET)
        bits = SPI_DEV_A_TX_DONE | SPI_DEV_A_RX_DONE;
    else if (GPIO_ReadPin(GPIOA, GPIO_PIN_5) == GPIO_PIN_RESET)
        bits = SPI_DEV_B_TX_DONE | SPI_DEV_B_RX_DONE;

    xEventGroupSetBitsFromISR(xSpiEventGroup, bits, &xHPTW);
    portYIELD_FROM_ISR(xHPTW);
}
```

---

## 6. Implementation in Rust (Embassy + RTIC) 

Rust offers two major async/RTOS frameworks for embedded: **Embassy** (async/await model) and **RTIC** (resource and task model). Both address the SPI event-group pattern elegantly.

### 6.1 Embassy — Async SPI with Signal (Equivalent to Event Group)

Embassy uses `Signal<T>` and `Channel<T,N>` for inter-task communication. For event-group semantics, `embassy_sync::signal::Signal` or custom bit-flag structures over `AtomicU32` work well.

```rust
// Cargo.toml dependencies (excerpt):
// embassy-executor = { version = "0.5", features = ["arch-cortex-m", "executor-thread"] }
// embassy-stm32   = { version = "0.1", features = ["stm32f401re", "time-driver-any"] }
// embassy-sync    = { version = "0.5" }
// embassy-time    = { version = "0.3" }

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::spi::{self, Spi};
use embassy_stm32::time::Hertz;
use embassy_sync::signal::Signal;
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_time::{Duration, Timer, with_timeout};

// ---------------------------------------------------------------
// SPI Status type — mirrors event group bit pattern
// ---------------------------------------------------------------
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum SpiStatus {
    TxComplete,
    RxComplete,
    TransferDone,   // TX + RX
    Error(u8),      // Error code
}

// Shared signal: ISR/DMA notifies waiting tasks
// Signal<M, T> stores the latest value; waiters block until signalled.
static SPI_SIGNAL: Signal<CriticalSectionRawMutex, SpiStatus> = Signal::new();

// ---------------------------------------------------------------
// Async SPI transfer helper
// Initiates the transfer and awaits completion with a timeout.
// ---------------------------------------------------------------
async fn spi_transfer_async<'d, T: spi::Instance>(
    spi: &mut Spi<'d, T, spi::Async>,
    tx: &[u8],
    rx: &mut [u8],
) -> Result<(), &'static str>
{
    // Clear any previous signal value
    SPI_SIGNAL.reset();

    // Start DMA transfer (Embassy SPI is async-native; awaiting drives DMA)
    match with_timeout(
        Duration::from_millis(100),
        spi.transfer(rx, tx)  // Embassy: transfer(read_buf, write_buf)
    ).await
    {
        Ok(Ok(_))  => Ok(()),
        Ok(Err(_)) => Err("SPI hardware error"),
        Err(_)     => Err("SPI transfer timeout"),
    }
}

// ---------------------------------------------------------------
// Sensor task — reads data from an SPI sensor
// ---------------------------------------------------------------
#[embassy_executor::task]
async fn sensor_task(mut spi: Spi<'static, embassy_stm32::peripherals::SPI1, spi::Async>) {
    let tx_buf: [u8; 4] = [0x03, 0x00, 0x00, 0x00];
    let mut rx_buf: [u8; 4] = [0u8; 4];

    loop {
        match spi_transfer_async(&mut spi, &tx_buf, &mut rx_buf).await {
            Ok(()) => {
                // Process rx_buf: e.g. parse temperature
                let raw_value = u16::from_be_bytes([rx_buf[2], rx_buf[3]]);
                defmt::info!("Sensor raw: {}", raw_value);
            }
            Err(e) => {
                defmt::error!("SPI error: {}", e);
            }
        }

        Timer::after(Duration::from_millis(250)).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    let spi_config = spi::Config {
        frequency: Hertz(1_000_000),
        mode: spi::Mode { polarity: spi::Polarity::IdleLow, phase: spi::Phase::CaptureOnFirstTransition },
        ..Default::default()
    };

    let spi = Spi::new(
        p.SPI1, p.PA5, p.PA7, p.PA6,  // SCK, MOSI, MISO
        p.DMA2_CH3, p.DMA2_CH0,        // TX DMA, RX DMA
        spi_config,
    );

    spawner.spawn(sensor_task(spi)).unwrap();
}
```

---

### 6.2 Embassy — Manual Event Group with AtomicU32 Bit Flags

For explicit multi-bit event group semantics matching FreeRTOS behaviour:

```rust
use core::sync::atomic::{AtomicU32, Ordering};
use core::task::Waker;
use embassy_sync::waitqueue::AtomicWaker;

// ---------------------------------------------------------------
// Event Group: bit definitions
// ---------------------------------------------------------------
pub const EVT_TX_COMPLETE : u32 = 1 << 0;
pub const EVT_RX_COMPLETE : u32 = 1 << 1;
pub const EVT_ERROR       : u32 = 1 << 2;
pub const EVT_OVERRUN     : u32 = 1 << 3;
pub const EVT_TRANSFER_DONE: u32 = EVT_TX_COMPLETE | EVT_RX_COMPLETE;

// ---------------------------------------------------------------
// SpiEventGroup — thread-safe, async-compatible
// ---------------------------------------------------------------
pub struct SpiEventGroup {
    bits: AtomicU32,
    waker: AtomicWaker,
}

impl SpiEventGroup {
    pub const fn new() -> Self {
        Self {
            bits: AtomicU32::new(0),
            waker: AtomicWaker::new(),
        }
    }

    /// Set one or more bits. Safe to call from ISR context.
    pub fn set_bits(&self, bits: u32) {
        self.bits.fetch_or(bits, Ordering::Release);
        self.waker.wake();   // Unblock any waiting async task
    }

    /// Clear one or more bits.
    pub fn clear_bits(&self, bits: u32) {
        self.bits.fetch_and(!bits, Ordering::AcqRel);
    }

    /// Read current bits without blocking.
    pub fn get_bits(&self) -> u32 {
        self.bits.load(Ordering::Acquire)
    }

    /// Async: wait until `required_bits` are ALL set (AND semantics).
    /// Returns the full bit value at the moment the condition was met.
    pub async fn wait_bits_all(&self, required_bits: u32) -> u32 {
        core::future::poll_fn(|cx| {
            // Register waker BEFORE checking bits to avoid race
            self.waker.register(cx.waker());

            let current = self.bits.load(Ordering::Acquire);
            if (current & required_bits) == required_bits {
                core::task::Poll::Ready(current)
            } else {
                core::task::Poll::Pending
            }
        })
        .await
    }

    /// Async: wait until ANY of `mask_bits` is set (OR semantics).
    pub async fn wait_bits_any(&self, mask_bits: u32) -> u32 {
        core::future::poll_fn(|cx| {
            self.waker.register(cx.waker());

            let current = self.bits.load(Ordering::Acquire);
            if (current & mask_bits) != 0 {
                core::task::Poll::Ready(current)
            } else {
                core::task::Poll::Pending
            }
        })
        .await
    }
}

// Global event group instance
static SPI_EVENTS: SpiEventGroup = SpiEventGroup::new();

// ---------------------------------------------------------------
// ISR-style callback (e.g., from DMA interrupt handler in RTIC)
// ---------------------------------------------------------------
fn on_spi_tx_rx_complete() {
    SPI_EVENTS.set_bits(EVT_TRANSFER_DONE);
}

fn on_spi_error(overrun: bool) {
    let bits = if overrun { EVT_ERROR | EVT_OVERRUN } else { EVT_ERROR };
    SPI_EVENTS.set_bits(bits);
}

// ---------------------------------------------------------------
// Task waiting on the event group
// ---------------------------------------------------------------
async fn wait_for_spi_transfer() -> Result<(), &'static str> {
    // Clear stale bits before initiating
    SPI_EVENTS.clear_bits(EVT_TX_COMPLETE | EVT_RX_COMPLETE | EVT_ERROR | EVT_OVERRUN);

    // Wait: wake on transfer done OR any error
    let bits = with_timeout(
        Duration::from_millis(100),
        SPI_EVENTS.wait_bits_any(EVT_TRANSFER_DONE | EVT_ERROR)
    )
    .await
    .map_err(|_| "SPI timeout")?;

    if (bits & EVT_ERROR) != 0 {
        return Err("SPI error");
    }
    if (bits & EVT_TRANSFER_DONE) == EVT_TRANSFER_DONE {
        return Ok(());
    }
    Err("Unexpected SPI state")
}
```

---

### 6.3 RTIC — Software Tasks with Event Channels

RTIC uses a resource-sharing model. Here, a `Channel` bridges the ISR hardware task and a software task:

```rust
#![no_std]
#![no_main]

use rtic::app;
use rtic_monotonics::systick::Systick;
use rtic_sync::channel::{Channel, Sender, Receiver};

// SPI status message passed from ISR to task
#[derive(Clone, Copy, Debug)]
pub enum SpiEvent {
    TxComplete,
    RxComplete,
    TransferDone,
    Error,
}

const SPI_CHAN_CAP: usize = 4;

#[app(device = stm32f4xx_hal::pac, peripherals = true, dispatchers = [EXTI0])]
mod app {
    use super::*;
    use stm32f4xx_hal::{pac, prelude::*, spi::Spi};

    // RTIC channel: ISR → software task
    static SPI_CHANNEL: Channel<SpiEvent, SPI_CHAN_CAP> = Channel::new();

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        spi: Spi<pac::SPI1>,
        spi_tx: Sender<'static, SpiEvent, SPI_CHAN_CAP>,
        spi_rx: Receiver<'static, SpiEvent, SPI_CHAN_CAP>,
    }

    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        // Initialise clocks, SPI, DMA ...
        let (spi_tx, spi_rx) = SPI_CHANNEL.split();

        // Spawn sensor processing task
        sensor_process::spawn().unwrap();

        (Shared {}, Local { spi: todo!(), spi_tx, spi_rx })
    }

    // ---------------------------------------------------------------
    // Hardware task: DMA complete interrupt (highest priority)
    // ---------------------------------------------------------------
    #[task(binds = DMA2_STREAM3, local = [spi_tx], priority = 7)]
    fn dma_spi_tx_complete(cx: dma_spi_tx_complete::Context) {
        // Signal completion to the software task via channel
        // try_send never blocks — returns error if channel full
        let _ = cx.local.spi_tx.try_send(SpiEvent::TransferDone);
    }

    // ---------------------------------------------------------------
    // Software task: processes SPI results
    // ---------------------------------------------------------------
    #[task(local = [spi_rx], priority = 2)]
    async fn sensor_process(cx: sensor_process::Context) {
        loop {
            // Await next SPI event from the ISR channel
            match cx.local.spi_rx.recv().await {
                Ok(SpiEvent::TransferDone) => {
                    defmt::info!("SPI transfer complete — processing data");
                    // Read from shared RX buffer and process
                }
                Ok(SpiEvent::Error) => {
                    defmt::error!("SPI error received");
                }
                _ => {}
            }
        }
    }
}
```

---

### 6.4 Timeout Wrapping (Embassy)

Always wrap event-group waits with a timeout in production firmware:

```rust
use embassy_time::{Duration, with_timeout};

async fn spi_wait_with_timeout(required_bits: u32) -> Result<u32, &'static str> {
    with_timeout(
        Duration::from_millis(100),
        SPI_EVENTS.wait_bits_all(required_bits)
    )
    .await
    .map_err(|_| "SPI wait timed out")
}
```

---

## 7. Advanced Patterns 

### 7.1 Bus Arbitration with Event Groups

When multiple tasks share one SPI bus, use an additional bit for bus ownership:

```c
// C/FreeRTOS: acquire the SPI bus before transfer
void SPI_AcquireBus(void) {
    // Wait until BUS_FREE bit is set, then clear it (take ownership)
    xEventGroupWaitBits(xSpiEventGroup, SPI_EVT_BUS_FREE, pdTRUE, pdTRUE, portMAX_DELAY);
}

void SPI_ReleaseBus(void) {
    xEventGroupSetBits(xSpiEventGroup, SPI_EVT_BUS_FREE);
}
```

### 7.2 Chaining Transactions

Signal a "pipeline" of SPI operations by waiting on one completion bit and immediately starting the next transfer, then waiting on the next bit:

```c
// Stage 1: Send command
SPI_TransferDMA(cmdBuf, NULL, CMD_LEN);
// (blocks here until TX complete)

// Stage 2: Read response (CS stays asserted between stages)
SPI_TransferDMA(dummyBuf, respBuf, RESP_LEN);
// (blocks here until RX complete)
```

### 7.3 Broadcast Completion to Multiple Tasks

Since multiple tasks can call `xEventGroupWaitBits()` on the same group, a single ISR signal can unblock several tasks simultaneously — useful for logging, display updates, and data processing running in parallel.

```c
// Task 1 waits for TX complete
xEventGroupWaitBits(xSpiEventGroup, SPI_EVT_TX_COMPLETE, pdFALSE, pdTRUE, portMAX_DELAY);

// Task 2 also waits for TX complete (independent, not consuming the bit)
xEventGroupWaitBits(xSpiEventGroup, SPI_EVT_TX_COMPLETE, pdFALSE, pdTRUE, portMAX_DELAY);
// Set xClearOnExit = pdFALSE so both tasks can read the same bit
```

---

## 8. Pitfalls and Best Practices

### Always Use `*FromISR` Variants in Interrupts

Calling `xEventGroupSetBits()` (non-ISR variant) from an interrupt handler is **undefined behaviour** in FreeRTOS and will corrupt the scheduler.

```c
// ❌ WRONG — never call this from an ISR
xEventGroupSetBits(xSpiEventGroup, SPI_EVT_TX_COMPLETE);

// ✅ CORRECT — ISR-safe variant with yield request
BaseType_t xHPTW = pdFALSE;
xEventGroupSetBitsFromISR(xSpiEventGroup, SPI_EVT_TX_COMPLETE, &xHPTW);
portYIELD_FROM_ISR(xHPTW);
```

### Clear Bits Before Starting a Transfer

Stale bits from a previous transfer can cause the next wait to return immediately with incorrect status:

```c
// ✅ Always clear relevant bits BEFORE initiating the DMA transfer
xEventGroupClearBits(xSpiEventGroup, SPI_EVT_TX_COMPLETE | SPI_EVT_RX_COMPLETE | SPI_EVT_ERROR);
HAL_SPI_TransmitReceive_DMA(...);
```

### Always Use Timeouts

Never wait with `portMAX_DELAY` in production unless you have a watchdog. A stuck DMA or disconnected device will deadlock the task forever.

### `xClearOnExit` Semantics

Setting `xClearOnExit = pdTRUE` clears **all bits that matched** the wait mask, not just the ones the task waited for. Be careful when multiple tasks share bits.

### Bit Exhaustion

FreeRTOS event groups have 24 usable bits. If you need more, use multiple event groups or restructure using queues/message buffers.

### Memory Allocation Failure

`xEventGroupCreate()` allocates from the FreeRTOS heap. Always check for `NULL` return and handle gracefully. Consider `xEventGroupCreateStatic()` with a pre-allocated `StaticEventGroup_t` for deterministic memory layout.

```c
static StaticEventGroup_t xSpiEventGroupBuffer;
xSpiEventGroup = xEventGroupCreateStatic(&xSpiEventGroupBuffer);
// Never returns NULL — no heap allocation
```

---

## 9. Summary

Event groups are the ideal RTOS primitive for signalling SPI transaction status because they combine the simplicity of semaphores with the expressiveness of multi-bit status flags.

| Aspect | Key Point |
|--------|-----------|
| **Why event groups** | Multiple status bits (TX done, RX done, error) in a single synchronisation object |
| **ISR safety** | Always use `xEventGroupSetBitsFromISR()` + `portYIELD_FROM_ISR()` |
| **Clear before transfer** | Prevent stale bits causing false-positive wake-ups |
| **AND vs OR** | Use `xWaitForAllBits = pdTRUE` for "both TX and RX done"; `pdFALSE` for "any error or done" |
| **Timeouts** | Always set a finite timeout; never use `portMAX_DELAY` in production |
| **Static allocation** | Use `xEventGroupCreateStatic()` for deterministic, heap-free creation |
| **Rust / Embassy** | Async/await with `AtomicU32` + `AtomicWaker` provides identical semantics natively |
| **Rust / RTIC** | Use typed channels (`rtic_sync::channel`) for ISR → task event delivery |
| **Multi-device** | Assign distinct bit groups per SPI device within one event group |
| **Scalability** | 24 usable bits in FreeRTOS; use multiple groups if needed |

Event groups elegantly decouple hardware interrupt timing from application task logic, resulting in clean, composable, and race-condition-free SPI drivers across both C/C++ RTOS and modern Rust async embedded ecosystems.

---

*Document: Topic 59 — Event Groups for SPI Status | RTOS Embedded Systems Series*