# 54. Hardware CS Control vs GPIO — SPI Chip Select Strategies

**Concept & Theory**
- What Chip Select does and its critical timing parameters (`t_CSS`, `t_CSH`, `t_CSW`)
- How hardware NSS works (peripheral-driven, sub-clock-cycle precision)
- How GPIO CS works (software-driven, fully flexible)
- A detailed trade-off table across 10 criteria

**C/C++ Examples**
- Linux `spidev` with hardware CS — including multi-segment transfers using `SPI_IOC_MESSAGE`
- Linux `spidev` + `libgpiod` GPIO CS — with `SPI_NO_CS` mode and a register read pattern
- STM32 HAL with `SPI_NSS_HARD_OUTPUT` — hardware NSS including a DMA variant
- STM32 HAL with `SPI_NSS_SOFT` — GPIO CS supporting multi-device bus access

**Rust Examples**
- `linux-embedded-hal` hardware CS using the `SpiDevice::transaction()` trait
- `rppal` GPIO CS wrapper with `write_then_read()` and per-byte delay support
- A generic `GpioSpiDevice<BUS, CS>` implementing the `embedded-hal 1.0` `SpiDevice` trait — portable across Linux and bare-metal targets, with mock-based unit tests

**Practical Guidance**
- Multi-device wiring diagram with GPIO CS fan-out
- Five common pitfalls (CS glitch at boot, NSS deassert between bytes, RTOS race conditions, `t_CSH` violations, missing bus flush)
- A decision flowchart to guide the choice
- Summary comparison table


> **Trade-offs between automatic chip select and manual GPIO control**

---

## Table of Contents

1. [Introduction](#introduction)
2. [What Is Chip Select?](#what-is-chip-select)
3. [Hardware CS (Automatic / Peripheral-Controlled)](#hardware-cs-automatic--peripheral-controlled)
4. [GPIO CS (Manual / Software-Controlled)](#gpio-cs-manual--software-controlled)
5. [Detailed Trade-off Analysis](#detailed-trade-off-analysis)
6. [Code Examples — C/C++](#code-examples--cc)
   - [Hardware CS with Linux spidev](#hardware-cs-with-linux-spidev)
   - [GPIO CS with Linux spidev + libgpiod](#gpio-cs-with-linux-spidev--libgpiod)
   - [Bare-metal STM32 — Hardware CS (NSS)](#bare-metal-stm32--hardware-cs-nss)
   - [Bare-metal STM32 — GPIO CS](#bare-metal-stm32--gpio-cs)
7. [Code Examples — Rust](#code-examples--rust)
   - [Hardware CS with linux-embedded-hal](#hardware-cs-with-linux-embedded-hal)
   - [GPIO CS with embedded-hal + rppal](#gpio-cs-with-embedded-hal--rppal)
   - [GPIO CS — Custom ChipSelect wrapper](#gpio-cs--custom-chipselect-wrapper)
8. [Multi-Device Scenarios](#multi-device-scenarios)
9. [Common Pitfalls](#common-pitfalls)
10. [Decision Guide](#decision-guide)
11. [Summary](#summary)

---

## Introduction

Every SPI transaction requires the master to assert a **Chip Select (CS)** signal — also called **Slave Select (SS)** or **nCS** — to activate exactly one peripheral device on a shared bus. There are two fundamentally different ways to drive this signal:

- **Hardware CS**: The SPI peripheral controller manages CS automatically, asserting it at the start of a transfer and deasserting it when the shift register is empty.
- **GPIO CS**: Application code manually drives a general-purpose I/O pin high or low, giving full software control over the CS timing window.

Choosing the wrong strategy for your use case can cause data corruption, incomplete transactions, timing violations, or bus contention. This document explores both approaches in depth.

---

## What Is Chip Select?

```
Master             Device A          Device B
  |                   |                 |
  |---SCLK----------->|---------------->|  (shared clock)
  |---MOSI----------->|---------------->|  (shared data out)
  |<--MISO------------|<----------------|  (shared data in)
  |---CS_A----------->|                 |  (active-low, selects A)
  |---CS_B------------------------>|    |  (active-low, selects B)
```

CS is **active-low** by convention (assert = pull to GND = logic 0). Only the device whose CS is asserted participates in the transaction. All others must tri-state their MISO line.

### CS Timing Requirements

Most SPI peripherals specify several critical timing parameters:

| Parameter | Meaning                                    |
|-----------|--------------------------------------------|
| `t_CSS`   | CS setup time before first SCLK edge       |
| `t_CSH`   | CS hold time after last SCLK edge          |
| `t_CSW`   | Minimum CS high (deasserted) time between transactions |
| `t_CSDLY` | CS-to-first-clock delay                    |

Violating these parameters leads to undefined device behaviour.

---

## Hardware CS (Automatic / Peripheral-Controlled)

The SPI hardware block contains a dedicated **NSS (Negative Slave Select)** output pin that the controller drives automatically when DMA or CPU-initiated transfers start and complete.

### How It Works

```
CPU writes to SPI TX register / starts DMA
        │
        ▼
  SPI peripheral asserts CS (pulls NSS low)
        │
        ▼
  Clock runs, data shifts in/out
        │
        ▼
  TX FIFO/shift register empty → transfer done
        │
        ▼
  SPI peripheral deasserts CS (pulls NSS high)
```

The CS transition happens at the hardware level — often **within a single clock cycle** of the first/last data bit.

### Advantages

- **Precise timing**: CS asserts/deasserts in lock-step with the shift register, leaving no software jitter.
- **DMA-friendly**: Works seamlessly with DMA transfers; no CPU involvement needed.
- **Lower CPU load**: No ISR or thread wakeup required to toggle a GPIO.
- **Atomic 8/16/32-bit word transfers**: Hardware guarantees that CS stays low for the entire word.

### Disadvantages

- **One CS per SPI peripheral** (on most microcontrollers): Adding a second device requires a second SPI peripheral or GPIO CS.
- **No CS control across multiple words**: Between back-to-back 8-bit transfers, hardware may momentarily deassert CS unless special continuous-mode registers exist.
- **Platform-specific**: Each MCU vendor implements NSS behaviour differently.
- **Multi-byte transactions are tricky**: Many peripherals require CS to remain low across multiple bytes (e.g., reading a 24-bit ADC), which hardware CS often cannot guarantee without special "keep CS asserted" modes.

---

## GPIO CS (Manual / Software-Controlled)

The application drives a regular GPIO pin to control CS:

```c
gpio_set_low(CS_PIN);       // Assert CS
spi_transfer(data, len);    // Transfer data
gpio_set_high(CS_PIN);      // Deassert CS
```

### Advantages

- **Full timing control**: Insert arbitrary delays between CS assert, clock, and deassert.
- **Unlimited CS lines**: Any GPIO can serve as CS for a new device on the same bus.
- **Multi-byte transactions**: Hold CS low across multiple `spi_transfer()` calls easily.
- **Non-standard protocols**: Some peripherals require CS toggling mid-transaction; only GPIO CS can do this.
- **Portable**: The same pattern works across any MCU or SoC with a GPIO.

### Disadvantages

- **Software jitter**: Interrupt latency and OS scheduling can introduce variable CS timing.
- **CPU overhead**: Thread or ISR must be active to toggle the pin.
- **Race conditions**: In a multi-threaded environment, another thread may call into the SPI bus between your CS assert and deassert if you don't hold a bus lock.
- **Slightly slower**: Each GPIO toggle adds at least one or two bus cycles of overhead.

---

## Detailed Trade-off Analysis

| Criterion                    | Hardware CS           | GPIO CS                  |
|------------------------------|-----------------------|--------------------------|
| Timing precision             | ✅ Sub-cycle           | ⚠️ Jitter possible        |
| DMA compatibility            | ✅ Native              | ⚠️ Needs careful ordering |
| Number of CS lines           | ❌ Usually 1           | ✅ Unlimited (any GPIO)   |
| Multi-byte CS hold           | ⚠️ MCU-dependent       | ✅ Trivial                |
| CPU load                     | ✅ Minimal             | ⚠️ Higher                 |
| Thread-safety                | ✅ Atomic per word     | ❌ Needs explicit locking |
| Non-standard CS toggling     | ❌ Not possible        | ✅ Fully flexible         |
| Portability                  | ❌ MCU-specific        | ✅ Highly portable        |
| OS/driver support (Linux)    | ✅ spidev default      | ✅ Via spidev + GPIO      |
| High-speed (>50 MHz)         | ✅ Preferred           | ⚠️ GPIO toggle may lag    |

---

## Code Examples — C/C++

### Hardware CS with Linux spidev

On Linux, `spidev` drives the kernel SPI driver's native CS pin automatically when you call `ioctl(SPI_IOC_MESSAGE)`.

```c
/*
 * hardware_cs_spidev.c
 * SPI transfer using kernel hardware CS via spidev
 * Compile: gcc -o hw_cs hardware_cs_spidev.c
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <unistd.h>

#define SPI_DEVICE  "/dev/spidev0.0"   // CS0 → hardware CS pin
#define SPI_SPEED   1000000            // 1 MHz
#define SPI_MODE    SPI_MODE_0

int spi_fd = -1;

int spi_init(void) {
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) { perror("open"); return -1; }

    uint8_t  mode  = SPI_MODE;
    uint8_t  bits  = 8;
    uint32_t speed = SPI_SPEED;

    if (ioctl(spi_fd, SPI_IOC_WR_MODE,           &mode)  < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD,  &bits)  < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ,   &speed) < 0) {
        perror("ioctl setup"); return -1;
    }
    return 0;
}

/*
 * Full-duplex transfer.
 * Hardware CS is asserted by the kernel before the first byte
 * and deasserted automatically after the last byte of the transfer.
 */
int spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = len,
        .speed_hz      = SPI_SPEED,
        .bits_per_word = 8,
        .delay_usecs   = 0,
        /* cs_change = 0 → CS stays asserted across the whole message */
        .cs_change     = 0,
    };
    int ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) { perror("SPI_IOC_MESSAGE"); return -1; }
    return 0;
}

/*
 * Multi-segment transfer — CS remains LOW across all segments.
 * Useful for command + data sequences (e.g., SSD1306, SD cards).
 */
int spi_transfer_segments(struct spi_ioc_transfer *segs, int n_segs) {
    /* Ensure cs_change=0 on all but the last segment */
    for (int i = 0; i < n_segs - 1; i++) segs[i].cs_change = 0;
    int ret = ioctl(spi_fd, SPI_IOC_MESSAGE(n_segs), segs);
    if (ret < 0) { perror("SPI_IOC_MESSAGE segments"); return -1; }
    return 0;
}

int main(void) {
    if (spi_init() < 0) return 1;

    /* Example: read WHO_AM_I register from an IMU (register 0x0F | 0x80) */
    uint8_t tx[2] = { 0x8F, 0x00 };
    uint8_t rx[2] = { 0x00, 0x00 };

    if (spi_transfer(tx, rx, 2) == 0) {
        printf("WHO_AM_I = 0x%02X\n", rx[1]);
    }

    close(spi_fd);
    return 0;
}
```

---

### GPIO CS with Linux spidev + libgpiod

When you need more than one SPI device on the same bus, use `/dev/spidev0.1` (if the kernel supports it) or — more portably — manually drive a GPIO pin and use `/dev/spidev0.0` with `SPI_NO_CS` to suppress the hardware CS.

```c
/*
 * gpio_cs_spidev.c
 * Manual GPIO chip select using libgpiod + spidev (SPI_NO_CS mode)
 * Compile: gcc -o gpio_cs gpio_cs_spidev.c -lgpiod
 *
 * Wiring: BCM GPIO 24 (physical pin 18) → device CS pin
 */
#include <fcntl.h>
#include <gpiod.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <unistd.h>

#define SPI_DEVICE   "/dev/spidev0.0"
#define SPI_SPEED    2000000
#define GPIO_CHIP    "/dev/gpiochip0"
#define GPIO_CS_LINE 24          /* BCM 24 */

static int                    spi_fd = -1;
static struct gpiod_chip     *gchip  = NULL;
static struct gpiod_line     *cs_line = NULL;

/* ── GPIO helpers ──────────────────────────────────────────────── */
static inline void cs_assert(void)   { gpiod_line_set_value(cs_line, 0); }
static inline void cs_deassert(void) { gpiod_line_set_value(cs_line, 1); }

int gpio_cs_init(void) {
    gchip = gpiod_chip_open(GPIO_CHIP);
    if (!gchip) { perror("gpiod_chip_open"); return -1; }

    cs_line = gpiod_chip_get_line(gchip, GPIO_CS_LINE);
    if (!cs_line) { perror("gpiod_chip_get_line"); return -1; }

    /* Request as output, initially deasserted (high) */
    if (gpiod_line_request_output(cs_line, "spi-gpio-cs", 1) < 0) {
        perror("gpiod_line_request_output"); return -1;
    }
    return 0;
}

/* ── SPI init with SPI_NO_CS ───────────────────────────────────── */
int spi_init(void) {
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) { perror("open"); return -1; }

    /* SPI_NO_CS tells the kernel NOT to touch its hardware CS pin */
    uint8_t  mode  = SPI_MODE_0 | SPI_NO_CS;
    uint8_t  bits  = 8;
    uint32_t speed = SPI_SPEED;

    if (ioctl(spi_fd, SPI_IOC_WR_MODE,           &mode)  < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD,  &bits)  < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ,   &speed) < 0) {
        perror("ioctl setup"); return -1;
    }
    return 0;
}

/*
 * Transfer with manual GPIO CS.
 *
 * IMPORTANT: On a multi-threaded system, wrap the entire
 * cs_assert → transfer → cs_deassert sequence in a mutex
 * to prevent another thread from stealing the bus.
 */
int spi_transfer_gpio_cs(const uint8_t *tx, uint8_t *rx, size_t len) {
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = len,
        .speed_hz      = SPI_SPEED,
        .bits_per_word = 8,
    };

    cs_assert();                                   /* GPIO → LOW  */
    int ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    cs_deassert();                                 /* GPIO → HIGH */

    if (ret < 0) { perror("SPI_IOC_MESSAGE"); return -1; }
    return 0;
}

/*
 * Multi-byte read: CS held low across a write (command) + read (data).
 * This is the typical pattern for register-based SPI devices.
 */
int spi_read_register(uint8_t reg_addr, uint8_t *buf, size_t len) {
    uint8_t cmd = reg_addr | 0x80;   /* set read bit (device-specific) */

    struct spi_ioc_transfer segs[2] = {
        {   /* Write command byte */
            .tx_buf = (unsigned long)&cmd,
            .rx_buf = 0,
            .len    = 1,
        },
        {   /* Read response bytes */
            .tx_buf = 0,
            .rx_buf = (unsigned long)buf,
            .len    = len,
        },
    };

    cs_assert();
    int ret = ioctl(spi_fd, SPI_IOC_MESSAGE(2), segs);
    cs_deassert();

    return (ret < 0) ? -1 : 0;
}

int main(void) {
    if (gpio_cs_init() < 0 || spi_init() < 0) return 1;

    uint8_t data[4];
    if (spi_read_register(0x0F, data, 4) == 0) {
        printf("Read: 0x%02X 0x%02X 0x%02X 0x%02X\n",
               data[0], data[1], data[2], data[3]);
    }

    /* Cleanup */
    gpiod_line_release(cs_line);
    gpiod_chip_close(gchip);
    close(spi_fd);
    return 0;
}
```

---

### Bare-metal STM32 — Hardware CS (NSS)

On STM32, when `NSS` is configured as **hardware output mode**, the SPI peripheral asserts PA4 automatically.

```c
/*
 * stm32_hardware_nss.c  (STM32F4, HAL library)
 *
 * SPI1 pin mapping:
 *   PA4 = NSS  (hardware CS, driven by SPI peripheral)
 *   PA5 = SCK
 *   PA6 = MISO
 *   PA7 = MOSI
 */
#include "stm32f4xx_hal.h"

SPI_HandleTypeDef hspi1;

void SPI1_HW_NSS_Init(void) {
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Configure PA4/PA5/PA6/PA7 as SPI alternate function */
    GPIO_InitTypeDef gpio = {
        .Pin       = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7,
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_NOPULL,
        .Speed     = GPIO_SPEED_FREQ_HIGH,
        .Alternate = GPIO_AF5_SPI1,
    };
    HAL_GPIO_Init(GPIOA, &gpio);

    hspi1 = (SPI_HandleTypeDef){
        .Instance               = SPI1,
        .Init.Mode              = SPI_MODE_MASTER,
        .Init.Direction         = SPI_DIRECTION_2LINES,
        .Init.DataSize          = SPI_DATASIZE_8BIT,
        .Init.CLKPolarity       = SPI_POLARITY_LOW,
        .Init.CLKPhase          = SPI_PHASE_1EDGE,
        /*
         * NSS_SIGNAL: peripheral drives NSS pin automatically.
         * NSS goes LOW when SPI is enabled and TX FIFO written;
         * NSS goes HIGH when TX FIFO empties.
         */
        .Init.NSS               = SPI_NSS_HARD_OUTPUT,
        .Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16,
        .Init.FirstBit          = SPI_FIRSTBIT_MSB,
        .Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE,
    };
    HAL_SPI_Init(&hspi1);
}

/*
 * Transfer using hardware NSS.
 * NSS is asserted when HAL_SPI_TransmitReceive() starts and
 * deasserted when it returns. No GPIO code needed.
 *
 * LIMITATION: If you call this twice in a row, NSS will pulse
 * HIGH between calls. Use DMA or concatenate buffers if you
 * need an unbroken CS across multiple logical transfers.
 */
HAL_StatusTypeDef spi_hw_transfer(uint8_t *tx, uint8_t *rx, uint16_t len) {
    return HAL_SPI_TransmitReceive(&hspi1, tx, rx, len, HAL_MAX_DELAY);
}

/*
 * Workaround for continuous CS across multi-byte sequences:
 * Use DMA double-buffer or pack all bytes into a single HAL call.
 */
HAL_StatusTypeDef spi_hw_transfer_dma(uint8_t *tx, uint8_t *rx, uint16_t len) {
    /* With DMA, the NSS stays low for the entire DMA transfer */
    return HAL_SPI_TransmitReceive_DMA(&hspi1, tx, rx, len);
}
```

---

### Bare-metal STM32 — GPIO CS

```c
/*
 * stm32_gpio_cs.c  (STM32F4, HAL library)
 *
 * SPI1: SCK=PA5, MISO=PA6, MOSI=PA7
 * CS:   PB0  (software-controlled GPIO — can add unlimited CS pins)
 */
#include "stm32f4xx_hal.h"

SPI_HandleTypeDef hspi1;

#define CS_PORT   GPIOB
#define CS_PIN    GPIO_PIN_0

static inline void CS_Assert(void)   { HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET); }
static inline void CS_Deassert(void) { HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);   }

void SPI1_GPIO_CS_Init(void) {
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* SPI data pins */
    GPIO_InitTypeDef gpio_spi = {
        .Pin       = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7,
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_NOPULL,
        .Speed     = GPIO_SPEED_FREQ_HIGH,
        .Alternate = GPIO_AF5_SPI1,
    };
    HAL_GPIO_Init(GPIOA, &gpio_spi);

    /* GPIO CS pin — initially deasserted */
    GPIO_InitTypeDef gpio_cs = {
        .Pin   = CS_PIN,
        .Mode  = GPIO_MODE_OUTPUT_PP,
        .Pull  = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_HIGH,
    };
    HAL_GPIO_Init(CS_PORT, &gpio_cs);
    CS_Deassert();

    hspi1 = (SPI_HandleTypeDef){
        .Instance               = SPI1,
        .Init.Mode              = SPI_MODE_MASTER,
        .Init.Direction         = SPI_DIRECTION_2LINES,
        .Init.DataSize          = SPI_DATASIZE_8BIT,
        .Init.CLKPolarity       = SPI_POLARITY_LOW,
        .Init.CLKPhase          = SPI_PHASE_1EDGE,
        /* Software NSS management — SPI peripheral ignores the NSS pin */
        .Init.NSS               = SPI_NSS_SOFT,
        .Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16,
        .Init.FirstBit          = SPI_FIRSTBIT_MSB,
        .Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE,
    };
    /* With NSS_SOFT, set SSOE=0 and SSI=1 so peripheral doesn't conflict */
    __HAL_SPI_ENABLE(&hspi1);
    HAL_SPI_Init(&hspi1);
}

/*
 * Generic GPIO-CS transfer — CS held low across any number of bytes.
 * Extend this to hold CS for multiple logical transfers by not calling
 * CS_Deassert() between them.
 */
HAL_StatusTypeDef spi_gpio_transfer(uint8_t *tx, uint8_t *rx, uint16_t len) {
    CS_Assert();
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, len, HAL_MAX_DELAY);
    CS_Deassert();
    return status;
}

/*
 * Write-then-read with CS held low throughout.
 * Typical for register-based SPI sensors.
 */
HAL_StatusTypeDef spi_write_read(uint8_t *write_buf, uint16_t write_len,
                                  uint8_t *read_buf,  uint16_t read_len) {
    HAL_StatusTypeDef s;
    CS_Assert();
    s = HAL_SPI_Transmit(&hspi1, write_buf, write_len, HAL_MAX_DELAY);
    if (s == HAL_OK)
        s = HAL_SPI_Receive(&hspi1, read_buf, read_len, HAL_MAX_DELAY);
    CS_Deassert();
    return s;
}

/*
 * Multiple devices on the same bus — each gets its own CS GPIO.
 * The SPI peripheral is shared; only CS lines differ.
 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} SpiDevice;

static const SpiDevice device_a = { GPIOB, GPIO_PIN_0 };
static const SpiDevice device_b = { GPIOB, GPIO_PIN_1 };
static const SpiDevice device_c = { GPIOC, GPIO_PIN_4 };

static inline void device_assert(const SpiDevice *dev) {
    HAL_GPIO_WritePin(dev->port, dev->pin, GPIO_PIN_RESET);
}
static inline void device_deassert(const SpiDevice *dev) {
    HAL_GPIO_WritePin(dev->port, dev->pin, GPIO_PIN_SET);
}

HAL_StatusTypeDef spi_device_transfer(const SpiDevice *dev,
                                        uint8_t *tx, uint8_t *rx, uint16_t len) {
    device_assert(dev);
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(&hspi1, tx, rx, len, HAL_MAX_DELAY);
    device_deassert(dev);
    return s;
}
```

---

## Code Examples — Rust

### Hardware CS with linux-embedded-hal

```rust
// Cargo.toml dependencies:
// linux-embedded-hal = "0.4"
// embedded-hal = "1.0"

use linux_embedded_hal::{SpidevBus, SpidevDevice, SpidevOptions};
use embedded_hal::spi::SpiDevice as _;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open /dev/spidev0.0 — CS0 is the hardware CS managed by the kernel
    let mut spi = SpidevDevice::open("/dev/spidev0.0")?;

    let options = SpidevOptions::new()
        .bits_per_word(8)
        .max_speed_hz(1_000_000)
        .mode(linux_embedded_hal::spidev::SpiModeFlags::SPI_MODE_0)
        .build();
    spi.configure(&options)?;

    // Transaction: the trait implementation handles CS assert/deassert
    // around the entire transaction block.
    let mut rx = [0u8; 2];
    let tx = [0x8Fu8, 0x00]; // Read WHO_AM_I register

    spi.transaction(&mut [
        embedded_hal::spi::Operation::Transfer(&mut rx, &tx),
    ])?;

    println!("WHO_AM_I = 0x{:02X}", rx[1]);
    Ok(())
}
```

---

### GPIO CS with embedded-hal + rppal

```rust
// Cargo.toml dependencies:
// rppal = "0.18"
// embedded-hal = "1.0"

use rppal::gpio::{Gpio, OutputPin};
use rppal::spi::{Bus, Mode, SlaveSelect, Spi};
use std::time::Duration;
use std::thread;

/// Wrapper that pairs an SPI bus with a manually controlled GPIO CS pin.
/// Implements the classic assert → transfer → deassert pattern.
pub struct SpiWithGpioCs {
    spi: Spi,
    cs:  OutputPin,
}

impl SpiWithGpioCs {
    /// Create a new instance.
    /// `cs_bcm_pin`: BCM GPIO number for the CS signal.
    pub fn new(clock_hz: u32, cs_bcm_pin: u8) -> Result<Self, Box<dyn std::error::Error>> {
        // Open SPI0 with SlaveSelect::Ss2 disabled so rppal doesn't touch hardware CS
        let spi = Spi::new(
            Bus::Spi0,
            SlaveSelect::Ss2,   // unused placeholder
            clock_hz,
            Mode::Mode0,
        )?;

        let gpio = Gpio::new()?;
        let mut cs = gpio.get(cs_bcm_pin)?.into_output();
        cs.set_high(); // deasserted initially

        Ok(Self { spi, cs })
    }

    /// Assert CS, perform a full-duplex transfer, then deassert CS.
    pub fn transfer(&mut self, read: &mut [u8], write: &[u8])
        -> Result<(), Box<dyn std::error::Error>>
    {
        self.cs.set_low();                         // Assert CS
        self.spi.transfer(read, write)?;
        self.cs.set_high();                        // Deassert CS
        Ok(())
    }

    /// Assert CS, write `cmd`, then read `len` bytes, then deassert CS.
    /// CS stays low across both operations — essential for register reads.
    pub fn write_then_read(&mut self, cmd: &[u8], response: &mut [u8])
        -> Result<(), Box<dyn std::error::Error>>
    {
        let dummy_rx = &mut vec![0u8; cmd.len()];
        let dummy_tx = &vec![0u8; response.len()];

        self.cs.set_low();
        self.spi.transfer(dummy_rx, cmd)?;         // send command
        self.spi.transfer(response, dummy_tx)?;    // read response
        self.cs.set_high();
        Ok(())
    }

    /// Transfer with a user-configurable inter-byte delay (rare, but needed
    /// by some slow peripherals like certain EEPROMs).
    pub fn transfer_with_delay(
        &mut self,
        write: &[u8],
        read:  &mut [u8],
        delay: Duration,
    ) -> Result<(), Box<dyn std::error::Error>> {
        self.cs.set_low();
        let dummy = &mut vec![0u8; 1];
        for (w, r) in write.iter().zip(read.iter_mut()) {
            self.spi.transfer(dummy, std::slice::from_ref(w))?;
            *r = dummy[0];
            thread::sleep(delay);
        }
        self.cs.set_high();
        Ok(())
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut device = SpiWithGpioCs::new(500_000, 24)?; // 500 kHz, BCM GPIO 24

    // Read 4 bytes starting at register 0x10
    let cmd = [0x90u8]; // read command (device-specific)
    let mut buf = [0u8; 4];
    device.write_then_read(&cmd, &mut buf)?;

    println!("Response: {:02X?}", buf);
    Ok(())
}
```

---

### GPIO CS — Custom ChipSelect wrapper

For projects using `embedded-hal 1.0` traits and targeting multiple platforms (Linux, bare-metal RTIC, etc.), you can build a generic `GpioCs` wrapper that implements `embedded_hal::spi::SpiDevice`:

```rust
// generic_gpio_cs.rs
// Works with any embedded-hal 1.0 compliant SPI bus + output pin

use embedded_hal::digital::OutputPin;
use embedded_hal::spi::{ErrorType, Operation, SpiBus, SpiDevice};
use core::fmt;

/// Combines a shared SPI bus with a GPIO CS pin into an `SpiDevice`.
///
/// On bare-metal, you typically place the `SpiBus` behind a `Mutex` or
/// use RTIC's resource sharing to ensure exclusive access.
pub struct GpioSpiDevice<BUS, CS> {
    bus: BUS,
    cs:  CS,
}

#[derive(Debug)]
pub enum GpioSpiError<BusErr, PinErr> {
    Bus(BusErr),
    Cs(PinErr),
}

impl<BUS, CS> fmt::Display for GpioSpiError<BUS::Error, CS::Error>
where
    BUS: ErrorType,
    CS:  OutputPin,
    BUS::Error: fmt::Debug,
    CS::Error:  fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl<BUS, CS> GpioSpiDevice<BUS, CS>
where
    BUS: SpiBus,
    CS:  OutputPin,
{
    pub fn new(bus: BUS, mut cs: CS) -> Result<Self, CS::Error> {
        cs.set_high()?;   // deasserted by default
        Ok(Self { bus, cs })
    }

    pub fn free(self) -> (BUS, CS) { (self.bus, self.cs) }
}

impl<BUS, CS> ErrorType for GpioSpiDevice<BUS, CS>
where
    BUS: SpiBus,
    CS:  OutputPin,
{
    type Error = GpioSpiError<BUS::Error, CS::Error>;
}

impl<BUS, CS> SpiDevice for GpioSpiDevice<BUS, CS>
where
    BUS: SpiBus,
    CS:  OutputPin,
{
    /// Execute a transaction: assert CS, run all operations, deassert CS.
    /// If any operation fails, CS is still deasserted (no bus lockout).
    fn transaction(&mut self, operations: &mut [Operation<'_, u8>])
        -> Result<(), Self::Error>
    {
        // Assert CS
        self.cs.set_low().map_err(GpioSpiError::Cs)?;

        let result = operations.iter_mut().try_for_each(|op| {
            match op {
                Operation::Read(buf) =>
                    self.bus.read(buf).map_err(GpioSpiError::Bus),
                Operation::Write(buf) =>
                    self.bus.write(buf).map_err(GpioSpiError::Bus),
                Operation::Transfer(read, write) =>
                    self.bus.transfer(read, write).map_err(GpioSpiError::Bus),
                Operation::TransferInPlace(buf) =>
                    self.bus.transfer_in_place(buf).map_err(GpioSpiError::Bus),
                Operation::DelayNs(ns) => {
                    // Portable: use cortex_m::asm::delay or std::thread::sleep
                    // depending on the target. Shown as a no-op placeholder:
                    let _ = ns;
                    Ok(())
                }
            }
        });

        // Always deassert CS, even on error
        self.cs.set_high().map_err(GpioSpiError::Cs)?;

        // Flush after deassert to ensure all bytes have been sent
        self.bus.flush().map_err(GpioSpiError::Bus)?;

        result
    }
}

// ── Usage example ─────────────────────────────────────────────────────────────
#[cfg(test)]
mod tests {
    use super::*;
    use embedded_hal_mock::eh1::spi::{Mock as SpiBusMock, Transaction};
    use embedded_hal_mock::eh1::digital::Mock as PinMock;
    use embedded_hal_mock::eh1::digital::State;
    use embedded_hal_mock::eh1::digital::Transaction as PinTransaction;

    #[test]
    fn test_gpio_cs_transaction() {
        let spi_expectations = [
            Transaction::transfer(vec![0x8F, 0x00], vec![0x00, 0x41]),
        ];
        let pin_expectations = [
            PinTransaction::set(State::Low),    // CS assert
            PinTransaction::set(State::High),   // CS deassert
        ];

        let spi_mock = SpiBusMock::new(&spi_expectations);
        let pin_mock = PinMock::new(&pin_expectations);

        let mut device = GpioSpiDevice::new(spi_mock, pin_mock).unwrap();

        let mut rx = [0u8; 2];
        device.transaction(&mut [
            Operation::Transfer(&mut rx, &[0x8F, 0x00]),
        ]).unwrap();

        assert_eq!(rx[1], 0x41); // expected WHO_AM_I response
    }
}
```

---

## Multi-Device Scenarios

One of the most common motivations for switching from hardware CS to GPIO CS is connecting **multiple SPI peripherals** to a single bus.

```
                    ┌──────────────────────────────┐
                    │         MCU / SoC            │
                    │                              │
                    │  SPI_MOSI ───────────────────┼────────┬──────────┬──────────┐
                    │  SPI_MISO ───────────────────┼────────┤          │          │
                    │  SPI_SCLK ───────────────────┼────────┤          │          │
                    │                              │        │          │          │
                    │  GPIO_A (CS) ────────────────┼──┐     │          │          │
                    │  GPIO_B (CS) ────────────────┼──┼─────┼──┐       │          │
                    │  GPIO_C (CS) ────────────────┼──┼─────┼──┼───────┼──┐       │
                    └──────────────────────────────┘  │     │  │       │  │       │
                                                      ▼     ▼  ▼       ▼  ▼       ▼
                                                   [IMU]    [Flash] [ADC] [DAC] [LCD]
```

With GPIO CS, adding a new device is simply:
1. Connect its CS pin to any free GPIO.
2. Initialise that GPIO as output-high.
3. Assert it exclusively during transfers to that device.

> ⚠️ **Never assert two CS lines simultaneously** on the same bus — both devices will try to drive MISO, causing bus contention and potential hardware damage.

---

## Common Pitfalls

### 1. CS glitches at power-up

GPIO pins are often in a high-impedance floating state during MCU reset. A pull-up resistor (typically 10 kΩ) on each CS line prevents false assertion during boot.

```c
/* Always initialise CS GPIO before enabling the SPI peripheral */
CS_Deassert();          // set high
SPI1_GPIO_CS_Init();    // now enable SPI
```

### 2. CS deasserted between bytes (hardware CS trap)

On many STM32 parts, hardware NSS deasserts between back-to-back 8-bit `HAL_SPI_Transmit()` calls. If your device requires CS to stay low for a 16+ bit sequence, either:
- Use GPIO CS, or
- Use a 16-bit data frame (`SPI_DATASIZE_16BIT`), or
- Use DMA so all bytes are clocked in one hardware transaction.

### 3. Race conditions in RTOS / multi-threaded environments

```c
/* WRONG — another task may interleave between these calls */
CS_Assert();
// ← task switch here! Another SPI user asserts a different CS
SPI_Transfer(data, len);
CS_Deassert();

/* CORRECT — wrap in a mutex */
xSemaphoreTake(spi_mutex, portMAX_DELAY);
CS_Assert();
SPI_Transfer(data, len);
CS_Deassert();
xSemaphoreGive(spi_mutex);
```

### 4. Violating `t_CSH` (CS high time)

Some flash memories require CS to remain high for ≥ 100 ns between transactions. Back-to-back GPIO toggles may violate this. Insert a small delay or use hardware timers if speed is critical.

```c
CS_Deassert();
__DSB();         /* data sync barrier — at least one bus cycle */
/* For stricter timing: */
DWT_Delay_us(1); /* or use a hardware timer */
CS_Assert();
```

### 5. Forgetting to flush the SPI bus (Rust)

In Rust with `embedded-hal`, always call `bus.flush()` after deasserting CS to ensure the FIFO has been fully clocked out:

```rust
self.cs.set_high()?;
self.bus.flush()?;  // ensure shift register is empty
```

---

## Decision Guide

```
Is your clock frequency > 50 MHz?
├── YES → Use HARDWARE CS (GPIO toggle latency can exceed t_CSS)
└── NO  ──┐
          │
          ├─ Do you need CS to span multiple spi_transfer() calls?
          │  ├── YES → Use GPIO CS
          │  └── NO  ───┐
          │             │
          │             ├─ Do you need > 1 SPI device on this bus?
          │             │  ├── YES → Use GPIO CS for all extra devices
          │             │  └── NO  ───┐
          │             │             │
          │             │             ├─ Is DMA used without OS?
          │             │             │  ├── YES → Use HARDWARE CS
          │             │             │  └── NO  → Either works; prefer GPIO CS
          │             │             │            for portability
          │             │             └─ END
          │             └─ END
          └─ END
```

---

## Summary

| | Hardware CS | GPIO CS |
|---|---|---|
| **Best for** | High-speed DMA, single device, latency-critical | Multi-device bus, multi-byte transactions, portable drivers |
| **Precision** | Sub-clock-cycle | Software-dependent (μs jitter possible) |
| **Flexibility** | Low | High |
| **Complexity** | Low | Medium (needs locking in RTOS) |
| **Linux spidev** | Default (`/dev/spidev0.X`) | `SPI_NO_CS` mode + libgpiod |
| **Rust** | `linux-embedded-hal::SpidevDevice` | `GpioSpiDevice<BUS, CS>` wrapper |
| **STM32 HAL** | `SPI_NSS_HARD_OUTPUT` | `SPI_NSS_SOFT` + `HAL_GPIO_WritePin` |

**Hardware CS** is the right choice when you need the tightest possible timing, are using DMA for maximum throughput, and have only one peripheral per SPI bus. It removes CS management from your application code entirely.

**GPIO CS** is the pragmatic choice for the vast majority of real-world designs: it scales to any number of devices on a shared bus, gives you full control over multi-byte transactions and non-standard CS timing, and produces portable code that works identically from a Raspberry Pi to a bare-metal Cortex-M.

In practice, many embedded systems use **both**: hardware CS for the highest-priority, high-bandwidth peripheral (e.g., external flash via DMA), and GPIO CS for all secondary devices (sensors, DACs, displays) on the same clock lines.

---

*Document: 54_Hardware_CS_Control_Vs_GPIO.md — Part of the SPI Programming Reference Series*