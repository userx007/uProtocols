# 71. Low-Power SPI Modes

**Conceptual coverage** — five root causes of SPI power waste (dynamic switching, peripheral quiescent current, floating lines, controller leakage, CPU polling overhead), each with quantified guidance.

**Eight techniques** with hardware and firmware strategies including clock gating, dynamic frequency scaling, CS management, burst batching, DMA+sleep, peripheral power-down sequencing, CPOL/pull-resistor matching, and reference-counted bus managers.

**C/C++ examples (5):**
- STM32 HAL clock gating wrapper
- W25Qxx flash deep power-down / wake sequence
- DMA-driven SPI transfer with `__WFI()` CPU sleep
- ICM-42688 FIFO batch read with C++ RAII `CsGuard`
- Reference-counted SPI bus power manager

**Rust examples (4):**
- `embedded-hal` SPI flash driver with `read_auto_sleep()`
- RAII `SpiClockGuard` with `Drop` for automatic gating (nRF52840)
- Embassy async/await DMA-driven FIFO read with `WFI` yield
- State-machine `SpiBusManager` with idle timeout enforcement

**Summary table** quantifies savings per layer — peripheral deep-sleep alone gives 99% idle current reduction; combining DMA+sleep with batching can extend battery life 5–20×.

## Minimizing Power Consumption in Battery-Operated SPI Systems

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why SPI Power Matters](#why-spi-power-matters)
3. [Sources of Power Consumption in SPI Systems](#sources-of-power-consumption-in-spi-systems)
4. [Key Low-Power Techniques](#key-low-power-techniques)
   - 4.1 [Clock Gating and Dynamic Frequency Scaling](#41-clock-gating-and-dynamic-frequency-scaling)
   - 4.2 [Chip Select Management](#42-chip-select-management)
   - 4.3 [Bus Idle and Tristating](#43-bus-idle-and-tristating)
   - 4.4 [Burst Transfers and Batching](#44-burst-transfers-and-batching)
   - 4.5 [DMA-Driven Transfers with CPU Sleep](#45-dma-driven-transfers-with-cpu-sleep)
   - 4.6 [Peripheral Power-Down Sequences](#46-peripheral-power-down-sequences)
   - 4.7 [SPI Clock Phase/Polarity (CPOL/CPHA) and Power](#47-spi-clock-phasepotarity-cpolcpha-and-power)
   - 4.8 [Power-Aware SPI Driver Design](#48-power-aware-spi-driver-design)
5. [C/C++ Code Examples](#cc-code-examples)
6. [Rust Code Examples](#rust-code-examples)
7. [Hardware Considerations](#hardware-considerations)
8. [Measurement and Profiling](#measurement-and-profiling)
9. [Summary](#summary)

---

## Introduction

The Serial Peripheral Interface (SPI) is ubiquitous in embedded systems — driving flash memories, sensor ICs, display controllers, ADCs, and RF transceivers. In battery-powered products (wearables, IoT nodes, medical devices, remote sensors), the SPI bus and its attached peripherals can be one of the dominant sources of dynamic and static power draw.

Low-power SPI design is not a single technique; it is a **holistic discipline** spanning hardware topology, firmware architecture, OS driver design, and peripheral selection. This document covers all these layers with concrete, production-grade code examples in both C/C++ and Rust.

---

## Why SPI Power Matters

A typical embedded MCU in active mode at 80 MHz draws 10–20 mA. When you add:

| Component | Typical Active Current |
|-----------|----------------------|
| SPI Flash (e.g. W25Q32) | 15–25 mA (read) |
| BLE SoC (e.g. nRF52840) SPI peripheral | 0.5–3 mA |
| IMU (e.g. ICM-42688) | 0.5–1 mA |
| Display (e.g. SPI e-paper) | 15–30 mA (refresh) |
| Pull-up resistors on idle bus | 0.1–1 mA (continuous) |

...the system current budget is exhausted rapidly. A CR2032 coin cell provides roughly 225 mAh; careless SPI management can reduce battery life from years to weeks.

---

## Sources of Power Consumption in SPI Systems

Understanding *where* power is consumed is prerequisite to reducing it:

**1. Active transfer dynamic power** — `P = C × V² × f`. Every clock edge charges and discharges trace and pin capacitance. Higher clock frequency and voltage mean more power per unit time.

**2. Peripheral quiescent current** — Many SPI peripherals have a significant supply current even when CS is deasserted but power is applied. Flash chips, for example, draw 1–5 mA in standby unless explicitly sent a deep-power-down command.

**3. Bus contention and floating lines** — Undriven MISO lines with pull-up/pull-down resistors consume DC current. Multiple slaves sharing a bus all drive MISO weakly until CS selects them.

**4. SPI controller leakage** — The MCU's SPI peripheral block has register state, clock trees, and I/O buffers that draw current even when idle unless clock-gated.

**5. DMA and CPU overhead** — Polling transfers keep the CPU in active state. Poorly designed ISR-driven transfers cause unnecessary wake-ups.

**6. Bus speed mismatches** — Running SPI faster than a peripheral requires forces that peripheral to draw more active current for a shorter time, but may not save energy if the MCU cannot enter sleep during transfers.

---

## Key Low-Power Techniques

### 4.1 Clock Gating and Dynamic Frequency Scaling

The SPI peripheral clock should be disabled when not in use. Most MCUs expose a peripheral clock enable register (e.g., `APB1ENR` on STM32, `PCLKSR` on SAM series, `SPIEN` bit on AVR). Gating saves static leakage in the SPI controller's clock tree.

**Dynamic Frequency Scaling (DFS):** Run SPI at the *minimum* frequency that meets your latency budget:

- Burst sensor reads may not need 10 MHz; 1 MHz may suffice.
- Display refresh at 8 MHz costs 8× more dynamic power than 1 MHz.
- Reduce `SCLK` before a transfer, restore afterwards if needed.

Energy for a transfer of `N` bytes:

```
E = (N × 8 × C_bus × V² ) + (P_peripheral × T_transfer)
```

Since `T_transfer = N × 8 / f_clk`, running at lower `f_clk` keeps `T_transfer` longer but reduces dynamic power. For capacitive loads > ~100 pF, lower frequencies are almost always more energy-efficient for bursts.

---

### 4.2 Chip Select Management

CS must be deasserted promptly after each transaction. Many peripherals enter a lower-power state when CS is high (e.g., flash enters standby mode). Leaving CS asserted keeps the peripheral's output driver active.

**Best practices:**
- Deassert CS within microseconds of the final clock edge.
- For SPI flash: always issue a `DEEP POWER DOWN` command (opcode `0xB9`) after bulk operations.
- Use hardware CS (NSS pin) rather than software GPIO when the SPI controller supports automatic NSS management — this guarantees sub-microsecond timing.

---

### 4.3 Bus Idle and Tristating

When no transfer is in progress:
- Configure MOSI as a high-impedance input or drive it low to avoid DC current through pull resistors.
- Disable the SPI controller's output drivers (set MOSI/SCK as GPIO input or analog).
- On multi-slave buses, ensure non-selected slaves' MISO outputs are tristated (rely on CS deassert).

**Pull resistor selection:** Weak pull-ups (100 kΩ) on MISO/MOSI/SCK reduce idle current from hundreds of µA to single µA. Verify timing margins still hold at your max clock frequency.

---

### 4.4 Burst Transfers and Batching

Waking the SPI controller, asserting CS, and performing the clock/data synchronisation all have a fixed overhead energy cost. Amortise this cost by:

- **Batching reads**: Instead of reading one register byte 10× per second, read 10 bytes in one burst.
- **FIFO/buffer aggregation**: Accumulate sensor samples in the peripheral's onboard FIFO (most IMUs and ADCs support this), then burst-read the entire FIFO in one transaction.
- **Write combining**: Coalesce multiple configuration writes into one CS assertion.

The energy savings are typically 3–10× for small transactions (1–4 bytes) combined into a single burst.

---

### 4.5 DMA-Driven Transfers with CPU Sleep

The most impactful single optimisation in most embedded systems is to let the DMA controller handle SPI transfers while the CPU enters a low-power sleep state:

```
CPU initiates transfer → CPU enters WFI/WFE → DMA drives SPI → DMA raises IRQ → CPU wakes
```

This is effective only when:
- The transfer is long enough that sleep-entry/wake overhead (typically 1–10 µs) is small relative to transfer time.
- The CPU has no other work to do while data moves.
- The sleep state chosen does not power down the DMA or SPI peripherals (check your MCU's power domain topology).

On nRF52840, STM32L4, and RP2040, DMA + SPI + CPU sleep (WFI) can reduce active power for a 256-byte read by 40–60% compared to busy-poll.

---

### 4.6 Peripheral Power-Down Sequences

Most SPI peripherals support explicit low-power states. Always use them:

| Peripheral Type | Low-Power Command/Method |
|-----------------|--------------------------|
| SPI NOR Flash | `0xB9` Deep Power Down; wake with `0xAB` |
| IMU / Accelerometer | Write power-mode register over SPI |
| Display (e-paper) | Send sleep command after refresh |
| ADC | Send power-down byte or deassert PWDN pin |
| BLE co-processor | Use vendor sleep protocol |

The sequence should always be:
1. Complete pending transfers.
2. Deassert CS.
3. Send power-down command.
4. Wait tPD (datasheet power-down time, often 1–3 µs).
5. Optionally disable SPI controller and pull resistors.

---

### 4.7 SPI Clock Phase/Polarity (CPOL/CPHA) and Power

CPOL determines the idle state of the clock line:

- `CPOL=0`: SCK idles **low**. With a pull-down resistor, idle current is near zero.
- `CPOL=1`: SCK idles **high**. With a pull-up resistor, idle current is near zero.

Mismatches (e.g., CPOL=0 with a pull-up) cause continuous DC current through the pull resistor during idle. Always match CPOL to your pull resistor polarity, or use no pull resistors on SCK if the MCU's output driver holds the line firmly.

---

### 4.8 Power-Aware SPI Driver Design

A well-designed driver:
- Uses **reference counting**: the SPI controller clock is only enabled when at least one device has an open transaction.
- Implements **automatic suspend**: after a configurable idle timeout (e.g., 10 ms), the driver disables the controller clock without application intervention.
- Exposes **power hints**: allows callers to declare "I will not use SPI for 100 ms" so the driver can issue peripheral sleep commands proactively.

---

## C/C++ Code Examples

### Example 1: Clock Gating the SPI Peripheral (STM32 HAL)

```c
#include "stm32l4xx_hal.h"

/* Disable SPI1 peripheral clock when idle */
static void spi_clock_gate_enable(void) {
    __HAL_RCC_SPI1_CLK_ENABLE();
}

static void spi_clock_gate_disable(void) {
    __HAL_RCC_SPI1_CLK_DISABLE();
}

/* Wrapper: gate clock around every transaction */
HAL_StatusTypeDef spi_transfer_low_power(SPI_HandleTypeDef *hspi,
                                          uint8_t *tx_buf,
                                          uint8_t *rx_buf,
                                          uint16_t len,
                                          uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;

    spi_clock_gate_enable();

    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET); /* CS low */

    status = HAL_SPI_TransmitReceive(hspi, tx_buf, rx_buf, len, timeout_ms);

    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);   /* CS high */

    spi_clock_gate_disable();

    return status;
}
```

---

### Example 2: SPI Flash Deep Power Down / Wake Sequence (C)

```c
#include <stdint.h>
#include <stdbool.h>
#include "spi_driver.h"   /* Platform SPI HAL abstraction */
#include "delay.h"

#define FLASH_CMD_DEEP_POWERDOWN   0xB9u
#define FLASH_CMD_RELEASE_POWERDOWN 0xABu
#define FLASH_CMD_READ_ID          0x9Fu

/* Minimum deep-power-down time before CS can be deasserted (µs) */
#define FLASH_tDP_US  3u
/* Minimum release-from-powerdown time before first command (µs) */
#define FLASH_tRES_US 3u

static void flash_cs_assert(void)   { spi_cs_low();  }
static void flash_cs_deassert(void) { spi_cs_high(); }

/**
 * @brief  Send W25Qxx into deep power-down mode (~1 µA supply current).
 *         Call after all flash operations are complete.
 */
void flash_deep_powerdown(void)
{
    uint8_t cmd = FLASH_CMD_DEEP_POWERDOWN;

    flash_cs_assert();
    spi_write(&cmd, 1);
    flash_cs_deassert();

    delay_us(FLASH_tDP_US);   /* Must wait tDP before de-powering VCC */
}

/**
 * @brief  Wake W25Qxx from deep power-down.
 *         Must be called before any read/write/erase commands.
 */
void flash_release_powerdown(void)
{
    uint8_t cmd = FLASH_CMD_RELEASE_POWERDOWN;

    flash_cs_assert();
    spi_write(&cmd, 1);
    flash_cs_deassert();

    delay_us(FLASH_tRES_US);  /* Wait tRES1 before sending next command */
}

/**
 * @brief  Demonstrate power-aware flash read with explicit sleep/wake.
 */
void flash_read_with_power_management(uint32_t addr,
                                       uint8_t  *buf,
                                       uint32_t  len)
{
    /* 1. Wake from deep power down */
    flash_release_powerdown();

    /* 2. Perform bulk read */
    flash_read(addr, buf, len);      /* Platform-specific read function */

    /* 3. Re-enter deep power down immediately */
    flash_deep_powerdown();
}
```

---

### Example 3: DMA-Driven SPI Transfer with CPU WFI Sleep (STM32 C)

```c
#include "stm32l4xx_hal.h"
#include <string.h>

extern SPI_HandleTypeDef hspi1;

static volatile bool spi_dma_done = false;

/* Called from DMA transfer-complete IRQ (via HAL callback) */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        spi_dma_done = true;
    }
}

/**
 * @brief  Initiate SPI DMA transfer, then sleep CPU until completion.
 *         Saves ~40% active current vs. busy-poll on STM32L4 @ 4 MHz SPI.
 *
 * @param  tx_buf   Transmit buffer (must remain valid until callback fires)
 * @param  rx_buf   Receive buffer
 * @param  len      Number of bytes
 */
void spi_dma_transfer_and_sleep(const uint8_t *tx_buf,
                                 uint8_t       *rx_buf,
                                 uint16_t       len)
{
    spi_dma_done = false;

    __HAL_RCC_SPI1_CLK_ENABLE();

    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_RESET);

    /* Start DMA — returns immediately */
    HAL_SPI_TransmitReceive_DMA(&hspi1, (uint8_t *)tx_buf, rx_buf, len);

    /* Sleep until DMA fires the completion IRQ.
     * __WFI() enters Sleep mode; the SPI/DMA clock domain stays active
     * because it is in a different power domain on STM32L4.
     * CPU active current drops from ~4 mA to ~0.1 mA during transfer. */
    while (!spi_dma_done) {
        __WFI();
    }

    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);

    __HAL_RCC_SPI1_CLK_DISABLE();
}
```

---

### Example 4: Batched IMU FIFO Read (C++ with RAII CS Guard)

```cpp
#include <cstdint>
#include <array>
#include <span>
#include "spi_bus.hpp"

/**
 * RAII guard: asserts CS on construction, deasserts on destruction.
 * Guarantees CS is always deasserted even on early returns/exceptions.
 */
class CsGuard {
public:
    explicit CsGuard(GpioPin &cs_pin) : cs_(cs_pin) {
        cs_.set_low();
    }
    ~CsGuard() {
        cs_.set_high();
        // Optional: explicit read-back to ensure GPIO register is flushed
        // before peripheral enters standby.
        (void)cs_.read();
    }
    CsGuard(const CsGuard&) = delete;
    CsGuard& operator=(const CsGuard&) = delete;

private:
    GpioPin &cs_;
};

/**
 * @brief  Read all samples from ICM-42688 1024-byte FIFO in a single SPI
 *         transaction, then power down the SPI controller.
 *
 *         Energy: 1 transaction per batch vs. N transactions for N samples.
 *         For 100 samples @ 6 bytes each: ~10x fewer CS toggle overheads.
 */
class Icm42688Driver {
public:
    static constexpr uint8_t REG_FIFO_COUNT_H = 0x2Eu;
    static constexpr uint8_t REG_FIFO_DATA    = 0x30u;
    static constexpr uint8_t REG_PWR_MGMT0    = 0x4Eu;
    static constexpr uint8_t ACCEL_GYRO_OFF   = 0x00u;
    static constexpr uint8_t ACCEL_LN_GYRO_LN = 0x0Fu;

    static constexpr std::size_t SAMPLE_SIZE_BYTES = 6u; /* accel XYZ 16-bit */
    static constexpr std::size_t FIFO_MAX_BYTES    = 2048u;

    explicit Icm42688Driver(SpiBus &bus, GpioPin &cs)
        : bus_(bus), cs_(cs) {}

    /**
     * @brief  Burst-read entire FIFO; returns number of samples read.
     */
    std::size_t read_fifo_batch(std::span<uint8_t> out_buf)
    {
        /* Step 1: read FIFO byte count in one transaction */
        uint16_t fifo_bytes = read_fifo_count();
        if (fifo_bytes == 0 || fifo_bytes > FIFO_MAX_BYTES) {
            return 0;
        }
        fifo_bytes = std::min<uint16_t>(fifo_bytes,
                                        static_cast<uint16_t>(out_buf.size()));

        /* Step 2: burst-read FIFO data in a single CS assertion */
        {
            CsGuard guard(cs_);

            uint8_t cmd = REG_FIFO_DATA | 0x80u; /* read bit */
            bus_.write(&cmd, 1);
            bus_.read(out_buf.data(), fifo_bytes);
        }   /* CS deasserts here */

        return fifo_bytes / SAMPLE_SIZE_BYTES;
    }

    /** Enter low-power mode: gyro + accel off (~7 µA) */
    void set_low_power(void)
    {
        write_register(REG_PWR_MGMT0, ACCEL_GYRO_OFF);
    }

    /** Return to low-noise mode for acquisition */
    void set_active(void)
    {
        write_register(REG_PWR_MGMT0, ACCEL_LN_GYRO_LN);
    }

private:
    uint16_t read_fifo_count(void)
    {
        CsGuard guard(cs_);
        uint8_t cmd[3] = { REG_FIFO_COUNT_H | 0x80u, 0x00u, 0x00u };
        uint8_t rsp[3] = {};
        bus_.transfer(cmd, rsp, 3);
        return (static_cast<uint16_t>(rsp[1]) << 8u) | rsp[2];
    }

    void write_register(uint8_t reg, uint8_t value)
    {
        CsGuard guard(cs_);
        uint8_t cmd[2] = { reg, value };
        bus_.write(cmd, 2);
    }

    SpiBus  &bus_;
    GpioPin &cs_;
};
```

---

### Example 5: SPI Bus Power Manager with Reference Counting (C)

```c
#include <stdint.h>
#include <stdbool.h>
#include "spi_hw.h"
#include "rtc_timer.h"  /* Provides rtc_get_ms() */

#define SPI_IDLE_TIMEOUT_MS  10u   /* Disable clock after 10 ms idle */

static volatile uint8_t  refcount     = 0u;
static volatile uint64_t last_use_ms  = 0u;
static          bool      clk_enabled = false;

/**
 * @brief  Acquire the SPI bus; enables peripheral clock on first acquire.
 */
void spi_bus_acquire(void)
{
    if (refcount == 0u) {
        spi_hw_clock_enable();
        clk_enabled = true;
    }
    refcount++;
    last_use_ms = rtc_get_ms();
}

/**
 * @brief  Release the SPI bus; does NOT immediately disable clock —
 *         the idle timer will do that, allowing cheap re-acquisition.
 */
void spi_bus_release(void)
{
    if (refcount > 0u) {
        refcount--;
    }
    last_use_ms = rtc_get_ms();
}

/**
 * @brief  Call this from a periodic low-priority task (e.g., 5 ms tick).
 *         Disables the SPI clock if the bus has been idle for the timeout.
 */
void spi_bus_idle_task(void)
{
    if (clk_enabled && refcount == 0u) {
        uint64_t now = rtc_get_ms();
        if ((now - last_use_ms) >= SPI_IDLE_TIMEOUT_MS) {
            spi_hw_clock_disable();
            clk_enabled = false;
        }
    }
}

/**
 * @brief  Example usage: sensor read with automatic bus management.
 */
int sensor_read_temperature(uint8_t *out_temp)
{
    uint8_t tx[2] = { 0x80u | 0x00u, 0x00u }; /* read reg 0x00 */
    uint8_t rx[2] = {};

    spi_bus_acquire();
    int rc = spi_hw_transfer(tx, rx, 2);
    spi_bus_release();

    if (rc == 0) {
        *out_temp = rx[1];
    }
    return rc;
}
```

---

## Rust Code Examples

### Example 6: Low-Power SPI Flash Driver (Rust, `embedded-hal`)

```rust
use embedded_hal::spi::SpiDevice;
use embedded_hal::digital::OutputPin;

const CMD_DEEP_POWERDOWN:    u8 = 0xB9;
const CMD_RELEASE_POWERDOWN: u8 = 0xAB;
const CMD_READ_DATA:         u8 = 0x03;
const CMD_JEDEC_ID:          u8 = 0x9F;

/// Represents an SPI NOR flash with explicit low-power management.
pub struct SpiFlash<SPI> {
    spi: SPI,
    is_sleeping: bool,
}

impl<SPI, E> SpiFlash<SPI>
where
    SPI: SpiDevice<Error = E>,
{
    /// Create a new driver instance. The device is assumed awake.
    pub fn new(spi: SPI) -> Self {
        Self { spi, is_sleeping: false }
    }

    /// Send the device into deep power-down (~1 µA).
    ///
    /// # Errors
    /// Returns an SPI bus error if the transaction fails.
    pub fn deep_powerdown(&mut self) -> Result<(), E> {
        self.spi.write(&[CMD_DEEP_POWERDOWN])?;
        self.is_sleeping = true;
        // Caller is responsible for a ≥3 µs delay before power removal.
        Ok(())
    }

    /// Release device from deep power-down.
    ///
    /// # Errors
    /// Returns an SPI bus error if the transaction fails.
    pub fn release_powerdown(&mut self) -> Result<(), E> {
        // A dummy 4-byte transaction with CS asserted wakes the device.
        // The AB command also returns the Electronic ID in byte 3.
        let mut buf = [CMD_RELEASE_POWERDOWN, 0x00, 0x00, 0x00];
        self.spi.transfer_in_place(&mut buf)?;
        self.is_sleeping = false;
        // Caller must wait ≥3 µs (tRES) before issuing next command.
        Ok(())
    }

    /// Read `buf.len()` bytes from the flash starting at `addr`.
    ///
    /// Automatically wakes the device if sleeping, performs the read,
    /// then returns to deep power-down — optimal for infrequent access.
    ///
    /// # Errors
    /// Returns an SPI bus error if any transaction fails.
    pub fn read_auto_sleep(
        &mut self,
        addr: u32,
        buf: &mut [u8],
    ) -> Result<(), E> {
        let was_sleeping = self.is_sleeping;

        if was_sleeping {
            self.release_powerdown()?;
        }

        // Build READ command: opcode + 24-bit address
        let cmd = [
            CMD_READ_DATA,
            ((addr >> 16) & 0xFF) as u8,
            ((addr >>  8) & 0xFF) as u8,
            ( addr        & 0xFF) as u8,
        ];

        // Use SpiDevice::transaction for atomic CS management
        self.spi.transaction(&mut [
            embedded_hal::spi::Operation::Write(&cmd),
            embedded_hal::spi::Operation::Read(buf),
        ])?;

        if was_sleeping {
            self.deep_powerdown()?;
        }

        Ok(())
    }

    /// Consume the driver, returning the inner SPI device.
    pub fn release(self) -> SPI {
        self.spi
    }
}
```

---

### Example 7: RAII SPI Bus Guard with Clock Gating (Rust, `cortex-m`)

```rust
use core::marker::PhantomData;

/// Marker trait for MCU-specific SPI clock control.
pub trait SpiClockGate {
    fn enable_clock();
    fn disable_clock();
}

/// RAII guard that enables the SPI peripheral clock on creation and
/// disables it on drop — even if the caller panics or returns early.
///
/// Usage:
/// ```rust
/// let _guard = SpiClockGuard::<MySpiClock>::new();
/// // perform SPI transfers here
/// // clock is automatically disabled when _guard goes out of scope
/// ```
pub struct SpiClockGuard<G: SpiClockGate> {
    _phantom: PhantomData<G>,
}

impl<G: SpiClockGate> SpiClockGuard<G> {
    /// Enable the SPI peripheral clock.
    pub fn new() -> Self {
        G::enable_clock();
        Self { _phantom: PhantomData }
    }
}

impl<G: SpiClockGate> Drop for SpiClockGuard<G> {
    fn drop(&mut self) {
        G::disable_clock();
    }
}

// ----- Platform-specific implementation (example: nRF52840) -----

use nrf52840_hal::pac::Peripherals;

pub struct Nrf52Spi0Clock;

impl SpiClockGate for Nrf52Spi0Clock {
    fn enable_clock() {
        // Safety: single-threaded context; no other core accesses SPIM0.
        unsafe {
            let p = Peripherals::steal();
            p.SPIM0.enable.write(|w| w.enable().enabled());
        }
    }

    fn disable_clock() {
        unsafe {
            let p = Peripherals::steal();
            p.SPIM0.enable.write(|w| w.enable().disabled());
        }
    }
}

/// Example: perform a sensor read with automatic clock gating.
pub fn sensor_read_with_clock_gate<SPI, E>(
    spi: &mut SPI,
    reg: u8,
) -> Result<u8, E>
where
    SPI: embedded_hal::spi::SpiDevice<Error = E>,
{
    let _guard = SpiClockGuard::<Nrf52Spi0Clock>::new();
    // Clock is enabled for the duration of this function.

    let mut buf = [reg | 0x80, 0x00u8];
    spi.transfer_in_place(&mut buf)?;

    Ok(buf[1])
    // _guard dropped here → clock disabled automatically
}
```

---

### Example 8: DMA-Driven SPI with Async/Await for CPU Sleep (Rust, `embassy`)

```rust
use embassy_nrf::spim::{self, Spim};
use embassy_nrf::{bind_interrupts, peripherals};
use embassy_time::{Duration, Timer};

bind_interrupts!(struct Irqs {
    SPIM3 => spim::InterruptHandler<peripherals::SPI3>;
});

/// Sensor configuration for low-power acquisition.
const IMU_FIFO_READ_REG: u8 = 0x30 | 0x80; // read bit set
const IMU_SLEEP_REG:     u8 = 0x4E;
const IMU_SLEEP_VAL:     u8 = 0x00;
const IMU_WAKE_VAL:      u8 = 0x0F;

/// Read IMU FIFO using async SPI (DMA-backed on nRF52840).
///
/// While the DMA transfer is in progress, the executor can yield to other
/// tasks or enter WFI, reducing active CPU current.
///
/// Returns the number of bytes read.
pub async fn read_imu_fifo_async(
    spi: &mut Spim<'_, peripherals::SPI3>,
    cs:  &mut embassy_nrf::gpio::Output<'_>,
    buf: &mut [u8],
) -> usize {
    // 1. Wake IMU from low-power mode
    {
        cs.set_low();
        let wake_cmd = [IMU_SLEEP_REG & 0x7F, IMU_WAKE_VAL];
        spi.write(&wake_cmd).await.ok();
        cs.set_high();
    }

    // Short stabilisation delay — yields CPU to executor/WFI
    Timer::after(Duration::from_micros(200)).await;

    // 2. Read FIFO in a single async DMA transaction.
    //    CPU is free to sleep (WFI) or run other tasks during the transfer.
    let read_len = buf.len().min(2048);
    {
        cs.set_low();
        let cmd = [IMU_FIFO_READ_REG];
        spi.write(&cmd).await.ok();
        spi.read(&mut buf[..read_len]).await.ok();
        cs.set_high();
    }

    // 3. Return IMU to low-power mode immediately
    {
        cs.set_low();
        let sleep_cmd = [IMU_SLEEP_REG & 0x7F, IMU_SLEEP_VAL];
        spi.write(&sleep_cmd).await.ok();
        cs.set_high();
    }

    read_len
}

/// Embassy task: wakes every 100 ms to batch-read sensor FIFO.
/// Between wake-ups the executor calls WFI, dropping system current to ~10 µA.
#[embassy_executor::task]
pub async fn sensor_task(
    mut spi: Spim<'static, peripherals::SPI3>,
    mut cs:  embassy_nrf::gpio::Output<'static>,
) {
    let mut fifo_buf = [0u8; 512];

    loop {
        let n = read_imu_fifo_async(&mut spi, &mut cs, &mut fifo_buf).await;

        if n > 0 {
            // Process samples (e.g., push to ring buffer, run filter)
            process_samples(&fifo_buf[..n]);
        }

        // Sleep for 100 ms — executor enters WFI during this time.
        Timer::after(Duration::from_millis(100)).await;
    }
}

fn process_samples(_buf: &[u8]) {
    // Application-specific sample processing
}
```

---

### Example 9: Power-Aware SPI Manager with State Machine (Rust)

```rust
use core::time::Duration;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BusState {
    Disabled,   // Clock gated; peripherals in deep power-down
    Idle,       // Clock enabled; peripherals in standby
    Active,     // Transfer in progress
}

pub struct SpiBusManager<SPI, TIMER> {
    spi:         SPI,
    timer:       TIMER,
    state:       BusState,
    idle_timeout: Duration,
    last_activity: u64,  // Milliseconds since boot
}

impl<SPI, TIMER, E> SpiBusManager<SPI, TIMER>
where
    SPI:   embedded_hal::spi::SpiDevice<Error = E>,
    TIMER: FnMut() -> u64,  // Returns ms since boot
{
    pub fn new(spi: SPI, timer: TIMER, idle_timeout: Duration) -> Self {
        Self {
            spi,
            timer,
            state: BusState::Disabled,
            idle_timeout,
            last_activity: 0,
        }
    }

    /// Execute a closure with an active SPI bus, handling power transitions.
    ///
    /// State transitions:
    ///   Disabled → Idle  (enable clock + wake peripherals)
    ///   Idle     → Active (begin transfer)
    ///   Active   → Idle  (transfer complete)
    ///   Idle     → Disabled (idle timeout, handled by `poll_idle`)
    pub fn with_bus<F, R>(&mut self, f: F) -> Result<R, E>
    where
        F: FnOnce(&mut SPI) -> Result<R, E>,
    {
        match self.state {
            BusState::Disabled => {
                self.enable_bus();
            }
            BusState::Idle | BusState::Active => {}
        }

        self.state = BusState::Active;
        let result = f(&mut self.spi);
        self.state = BusState::Idle;
        self.last_activity = (self.timer)();

        result
    }

    /// Call from a periodic task to enforce the idle timeout.
    ///
    /// Returns `true` if the bus was disabled this call.
    pub fn poll_idle(&mut self) -> bool {
        if self.state != BusState::Idle {
            return false;
        }

        let now = (self.timer)();
        let elapsed_ms = now.saturating_sub(self.last_activity);
        let timeout_ms = self.idle_timeout.as_millis() as u64;

        if elapsed_ms >= timeout_ms {
            self.disable_bus();
            return true;
        }

        false
    }

    pub fn state(&self) -> BusState {
        self.state
    }

    fn enable_bus(&mut self) {
        // Platform-specific: enable SPI peripheral clock
        // self.spi.enable_clock();
        self.state = BusState::Idle;
    }

    fn disable_bus(&mut self) {
        // Platform-specific: disable SPI peripheral clock
        // self.spi.disable_clock();
        self.state = BusState::Disabled;
    }
}
```

---

## Hardware Considerations

Beyond firmware, hardware design choices heavily influence SPI power:

**1. Voltage selection:** Dynamic power scales as `V²`. Dropping from 3.3 V to 1.8 V reduces capacitive switching energy by 70%. Use level-shifters only when necessary — they themselves consume current.

**2. Trace and pin capacitance:** Shorter PCB traces reduce capacitive load. Keep SPI traces under 10 cm; use 50 Ω controlled impedance only for very high-speed (>20 MHz) designs.

**3. Pull resistor values:**
- SCK/MOSI/CS: 100 kΩ pull-downs (CPOL=0) → ~33 µA at 3.3 V each
- MISO: 100 kΩ pull-up → ~33 µA when driven low by peripheral
- No pull resistors + MCU output drive: 0 static current

**4. Load switch on peripheral VCC:** Gate the power supply to high-power peripherals (flash, display, radio) with a P-FET or dedicated power switch. When gated off, leakage current is typically < 1 µA, vs. 1–5 mA in device standby.

**5. Shared vs. dedicated SPI buses:** Multiple peripherals sharing a bus cannot independently enter deep power-down if the bus is shared with an active device. For devices with very different duty cycles, consider a dedicated SPI bus per high-power device, allowing its bus and controller to be gated independently.

---

## Measurement and Profiling

Effective low-power development requires measurement, not estimation:

**Energy profiling tools:**
- **Nordic PPK2** (Power Profiler Kit): 1 µA resolution, 1 MHz sample rate; ideal for nRF52 systems.
- **Otii Arc**: High-resolution current measurement with software timeline correlation.
- **Segger J-Trace + SystemView**: Correlates RTOS task scheduling with energy traces.
- **Oscilloscope + shunt resistor**: Quick sanity-check; 1 Ω shunt gives 1 mV/mA.

**Key metrics to measure:**
1. Peak current during SPI transfer (validates trace/driver impedance)
2. Average current during `n` transfers per second (validates DMA/sleep effectiveness)
3. Idle current after bus release (validates clock gating and peripheral power-down)
4. Wake latency from deep power-down (validates `tRES`/`tDP` handling)

**Profiling pattern in firmware:**

```c
/* Toggle a spare GPIO before/after each transfer for oscilloscope correlation */
#define PROFILE_BEGIN()  HAL_GPIO_WritePin(DBG_GPIO_Port, DBG_Pin, GPIO_PIN_SET)
#define PROFILE_END()    HAL_GPIO_WritePin(DBG_GPIO_Port, DBG_Pin, GPIO_PIN_RESET)

void spi_profiled_transfer(uint8_t *tx, uint8_t *rx, uint16_t len)
{
    PROFILE_BEGIN();
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, len, 100);
    PROFILE_END();
}
```

---

## Summary

Low-power SPI design requires attention across five layers:

| Layer | Technique | Typical Saving |
|-------|-----------|---------------|
| **Peripheral** | Deep power-down commands (Flash, IMU) | 99% idle current reduction |
| **Bus** | Clock gating of SPI controller | 50–90% static leakage in controller |
| **Transfer** | DMA + CPU WFI sleep | 40–60% CPU active-time reduction |
| **Protocol** | FIFO batching; burst transfers | 3–10× fewer CS toggle overheads |
| **Hardware** | Lower voltage, load switches, weak pull resistors | 30–70% total system saving |

The most impactful single change in most embedded systems is to **send SPI peripherals into their deep power-down states whenever they are not actively needed**. The second most impactful is to **use DMA-driven transfers paired with CPU sleep**. Together, these two techniques can extend battery life by a factor of 5–20× compared to a naive always-on, busy-poll SPI implementation.

Correctness demands careful sequencing: always respect `tDP`/`tRES` timing requirements from device datasheets, always deassert CS promptly, and always ensure the SPI controller clock is gated after — never during — an active transfer. RAII patterns in C++ and Rust make this reliability easier to achieve without sacrificing readability.

---

*Document: `71_Low_Power_SPI_Modes.md` | Topic 71 of Embedded SPI Programming Series*