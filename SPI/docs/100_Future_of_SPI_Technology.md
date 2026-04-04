# 100. Future of SPI Technology

- **Limitations of Classical SPI** — a structured analysis of why evolution was needed (GPIO waste, no ACK, no multi-master, etc.)
- **Enhanced SPI Variants** — Dual, Quad, Octal, and DDR SPI with throughput figures and use cases
- **MIPI I3C deep-dive** — feature comparison table, dynamic addressing, In-Band Interrupts, HDR modes, and target application domains (DDR5, 5G RFFE, CSI-3)
- **DMA & Hardware Security offload** — how modern SoCs transform SPI into a high-performance data path

**Code examples (5 in C/C++, 5 in Rust):**

| # | Language | Example |
|---|---|---|
| 1 | C | STM32 HAL full-duplex SPI + sensor register read |
| 2 | C | QSPI W25Q128 fast-read + memory-mapped XIP mode |
| 3 | C | Linux `spidev` JEDEC ID read via ioctl |
| 4 | C | DMA-driven SPI with D-cache coherency (Cortex-M7) |
| 5 | C | Linux I3C `i3cdev` write-read + CCC send |
| 6 | Rust | `embedded-hal` 1.0 generic SPI sensor driver (no_std) |
| 7 | Rust | Async QSPI on Embassy/STM32H7 |
| 8 | Rust | Linux `spidev` crate |
| 9 | Rust | I3C via raw `nix` ioctl bindings |
| 10 | Rust | `embedded-hal-bus` shared SPI bus pattern |

The **summary** ties it together: QSPI/OSPI owns high-bandwidth memory; I3C owns multi-sensor buses; and portable abstractions (embedded-hal, Zephyr) make the choice a deployment detail rather than an architectural lock-in.


> **Topic:** Evolution of SPI, emerging standards, and alternatives like I3C

---

## Table of Contents

1. [Introduction](#introduction)
2. [Limitations of Classical SPI](#limitations-of-classical-spi)
3. [Evolution of SPI: Enhanced Variants](#evolution-of-spi-enhanced-variants)
   - Dual SPI
   - Quad SPI (QSPI)
   - Octal SPI
   - DDR SPI
4. [Emerging Standards and Protocols](#emerging-standards-and-protocols)
   - MIPI I3C
   - SPI with DMA and Hardware Offload
   - Open-Source Hardware Bus Standardization
5. [Programming Examples in C/C++](#programming-examples-in-cc)
   - Standard SPI HAL usage (STM32)
   - QSPI Flash Access (STM32 HAL)
   - Linux SPI via spidev
   - DMA-driven SPI
6. [Programming Examples in Rust](#programming-examples-in-rust)
   - Embedded HAL SPI Trait
   - QSPI on Embassy (async)
   - Linux SPI via spidev crate
   - I3C conceptual binding in Rust
7. [Comparison Table: SPI vs Alternatives](#comparison-table)
8. [Migration Strategies](#migration-strategies)
9. [Summary](#summary)

---

## Introduction

The Serial Peripheral Interface (SPI), originally developed by Motorola in the mid-1980s, remains one of the most prevalent short-distance communication protocols in embedded systems. Despite its age, SPI has proven remarkably resilient—largely because of its simplicity, full-duplex capability, and deterministic timing. However, as modern embedded and IoT applications demand higher bandwidth, lower power, and multi-master topologies, SPI's limitations are becoming more apparent.

The future of SPI is not a single endpoint but rather a spectrum of evolution: enhanced multi-lane variants (Dual/Quad/Octal SPI), tighter integration with DMA engines and hardware security modules (HSMs), and—for many use cases—replacement by fundamentally new bus standards such as **MIPI I3C**, **SoundWire**, and **RFFE**. Understanding these trends is essential for engineers designing next-generation products.

---

## Limitations of Classical SPI

Before surveying alternatives, it is worth cataloguing the key constraints that drive the search for better solutions:

| Limitation | Detail |
|---|---|
| **No standard addressing** | Each device requires a dedicated Chip Select (CS) line; large fan-outs waste GPIOs |
| **No acknowledgement mechanism** | Master cannot natively detect a missing or failed peripheral |
| **No multi-master support** | Arbitration is not defined in the base protocol |
| **Unidirectional bandwidth ceiling** | Single MOSI/MISO pair limits throughput to `fCLK` bits/second per direction |
| **No interrupt or in-band signalling** | Peripherals cannot alert the host without a separate IRQ pin |
| **Power management** | No standardised low-power or sleep negotiation |
| **Clocking variability** | CPOL/CPHA permutations cause interoperability confusion |

These limitations are not hypothetical; they directly affect bill-of-materials cost (extra GPIO expanders), PCB layout complexity, and firmware portability.

---

## Evolution of SPI: Enhanced Variants

### Dual SPI

Dual SPI repurposes MOSI and MISO as two bidirectional I/O lines (`IO0`, `IO1`), effectively doubling throughput in one direction at a time.

```
Standard SPI:   CLK  CS  MOSI  MISO        (1 bit/cycle per direction)
Dual SPI:       CLK  CS  IO0   IO1         (2 bits/cycle, half-duplex)
```

**Use cases:** Serial Flash memories (e.g. Winbond W25Q series fast-read commands), display framebuffer streaming.

### Quad SPI (QSPI)

Quad SPI adds two more data lines (`IO2`, `IO3`) for 4-bit-per-cycle transfers. Modern Flash memories extensively support QSPI for page reads and program operations.

```
QSPI:  CLK  CS  IO0  IO1  IO2  IO3        (4 bits/cycle)
```

At 133 MHz, QSPI delivers **532 Mbit/s**—more than enough for execute-in-place (XIP) on microcontrollers with on-chip QSPI controllers (STM32, nRF9160, ESP32).

### Octal SPI (OPI)

Octal SPI extends to 8 data lines for 8-bit-per-cycle transfers, commonly found on high-density PSRAM and HyperBus flash (e.g., used in STM32H7 with external Octal OSPI peripheral).

```
OPI:   CLK  CS  IO0..IO7                  (8 bits/cycle)
```

At 200 MHz DDR, Octal SPI reaches **3.2 Gbit/s**—approaching the throughput of eMMC 5.1 while retaining the interface simplicity of SPI.

### DDR SPI (Double Data Rate)

DDR SPI samples data on both rising and falling clock edges, effectively doubling the data rate without increasing the clock frequency. This is increasingly standard in OSPI/HyperBus implementations and is part of the **JESD251** / HyperBus standard.

---

## Emerging Standards and Protocols

### MIPI I3C

**MIPI I3C** (Improved Inter-Integrated Circuit) is the most significant protocol competitor to SPI for sensor-hub and IoT applications. Ratified by the MIPI Alliance in 2017 (I3C v1.0) and updated to **I3C Basic v1.1.1** (2021), it is designed to supersede both I²C and SPI in many use cases.

#### Key I3C Features vs SPI

| Feature | SPI | I3C |
|---|---|---|
| Wires | 4+ | 2 (SDA, SCL) |
| Speed | Up to ~200 MHz | 12.5 MHz SDR, 25 MHz HDR-DDR |
| Effective throughput | Up to 3.2 Gbps (OPI DDR) | Up to 100 Mbps (HDR-TSP) |
| Multi-master | No | Yes (dynamic address assignment) |
| In-band interrupt | No | Yes (IBI - In-Band Interrupt) |
| Power management | None | Yes (GETACCMST, ENEC/DISEC) |
| Hot-plug | No | Yes |
| Error detection | None | Parity + CRC |
| Addressing | CS pins | 7-bit dynamic, 48-bit static UID |

I3C uses **dynamic address assignment (DAA)**, where devices have a 48-bit provisioned ID and receive a 7-bit working address at runtime. This eliminates the address-conflict problem of I²C and the GPIO-per-device problem of SPI.

I3C's **In-Band Interrupt (IBI)** mechanism allows any slave to interrupt the master by pulling SDA low during the free bus time—without a separate IRQ line. This is transformative for sensor-fusion applications where a dozen devices must signal data-ready events.

#### I3C Modes

- **SDR (Single Data Rate):** Up to 12.5 MHz — backward-compatible with I²C devices
- **HDR-DDR:** Double Data Rate, up to 25 MHz (50 Mbps effective)
- **HDR-TSP / HDR-TSL:** Ternary Symbol encoding for up to 100 Mbps

#### I3C Target Domains

I3C is being standardised for:
- **MIPI RFFE v3** — RF front-end control in 5G/6G modems
- **JEDEC SPD5 Hub** — DDR5 memory module management (replaces I²C)
- **MIPI SoundWire** — Multi-lane audio bus (replaces I²S + SPI combos)
- **MIPI CSI-3** — Camera sensor interfaces alongside CSI-2

### SPI with DMA and Hardware Security Modules

Modern SoCs integrate DMA controllers, hardware CRC engines, and cryptographic accelerators directly into the SPI peripheral bus. This means that SPI of the future is not the software-polled, byte-at-a-time protocol of the 1980s, but a **scatter-gather, DMA-chained, CRC-protected, and optionally AES-encrypted** data path.

The STM32H7 family, for example, couples OCTOSPI with a **DLYB (Delay Block)** for calibration, a **MDMA (Master DMA)** for background transfers, and the **OTFDEC (On-The-Fly Decryption)** engine for XIP of encrypted firmware from external flash.

### Open Hardware Bus Standardisation

The **Open Compute Project (OCP)** and **CHIPS Alliance** are pushing for unified register maps and driver models across SPI, QSPI, and I3C. Projects like:

- **OpenTitan** — open-source root-of-trust with standardised SPI/I3C interfaces
- **RISC-V platform spec** — defining standard QSPI Flash boot interfaces
- **Zephyr RTOS** — unified `spi_transceive()` API across all SPI variants

…are converging toward a world where the physical interface (SPI vs I3C) is an abstraction detail, and the application layer programs against a portable driver model.

---

## Programming Examples in C/C++

### 1. Standard SPI HAL Usage (STM32, C)

The following demonstrates a full-duplex SPI transaction using the STM32 HAL library, the most common pattern in production embedded firmware today.

```c
#include "stm32h7xx_hal.h"

extern SPI_HandleTypeDef hspi1;

/**
 * @brief  Perform a full-duplex SPI transfer.
 * @param  tx_buf  Pointer to transmit buffer
 * @param  rx_buf  Pointer to receive buffer
 * @param  len     Number of bytes to transfer
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef spi_transfer(const uint8_t *tx_buf,
                                uint8_t       *rx_buf,
                                uint16_t       len)
{
    HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_RESET);

    HAL_StatusTypeDef status =
        HAL_SPI_TransmitReceive(&hspi1,
                                (uint8_t *)tx_buf,
                                rx_buf,
                                len,
                                HAL_MAX_DELAY);

    HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET);
    return status;
}

/**
 * @brief  Read a register from an SPI sensor (e.g. BMI088 IMU).
 */
uint8_t sensor_read_register(uint8_t reg_addr)
{
    uint8_t tx[2] = { reg_addr | 0x80, 0x00 }; /* set read bit */
    uint8_t rx[2] = { 0 };

    spi_transfer(tx, rx, 2);
    return rx[1];
}
```

### 2. QSPI Flash Access (STM32 HAL, C)

This shows QSPI (Quad SPI) fast-read of a W25Q128 NOR Flash using the STM32 QUADSPI peripheral.

```c
#include "stm32h7xx_hal.h"

extern QSPI_HandleTypeDef hqspi;

#define W25Q_FAST_READ_QUAD_IO  0xEB
#define DUMMY_CYCLES            6

/**
 * @brief  Read data from QSPI NOR Flash using quad I/O fast read.
 * @param  address   24-bit flash address
 * @param  buf       Output buffer
 * @param  size      Number of bytes to read
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef qspi_read(uint32_t address, uint8_t *buf, uint32_t size)
{
    QSPI_CommandTypeDef cmd = {
        .InstructionMode   = QSPI_INSTRUCTION_1_LINE,
        .Instruction       = W25Q_FAST_READ_QUAD_IO,
        .AddressMode       = QSPI_ADDRESS_4_LINES,
        .AddressSize       = QSPI_ADDRESS_24_BITS,
        .Address           = address,
        .AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES,
        .AlternateBytesSize= QSPI_ALTERNATE_BYTES_8_BITS,
        .AlternateBytes    = 0xFF,          /* mode byte: continuous read */
        .DataMode          = QSPI_DATA_4_LINES,
        .DummyCycles       = DUMMY_CYCLES,
        .NbData            = size,
        .DdrMode           = QSPI_DDR_MODE_DISABLE,
        .SIOOMode          = QSPI_SIOO_INST_EVERY_CMD,
    };

    if (HAL_QSPI_Command(&hqspi, &cmd, HAL_QSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
        return HAL_ERROR;

    return HAL_QSPI_Receive(&hqspi, buf, HAL_QSPI_TIMEOUT_DEFAULT_VALUE);
}

/**
 * @brief  Enable QSPI Memory-Mapped mode for XIP (Execute-In-Place).
 *         After this call, the flash contents are directly readable
 *         via the AHB bus at the mapped address (e.g. 0x90000000).
 */
HAL_StatusTypeDef qspi_enable_memory_mapped(void)
{
    QSPI_CommandTypeDef      cmd;
    QSPI_MemoryMappedTypeDef cfg;

    cmd.InstructionMode    = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction        = W25Q_FAST_READ_QUAD_IO;
    cmd.AddressMode        = QSPI_ADDRESS_4_LINES;
    cmd.AddressSize        = QSPI_ADDRESS_24_BITS;
    cmd.AlternateByteMode  = QSPI_ALTERNATE_BYTES_4_LINES;
    cmd.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
    cmd.AlternateBytes     = 0xFF;
    cmd.DataMode           = QSPI_DATA_4_LINES;
    cmd.DummyCycles        = DUMMY_CYCLES;
    cmd.DdrMode            = QSPI_DDR_MODE_DISABLE;
    cmd.SIOOMode           = QSPI_SIOO_INST_ONLY_FIRST_CMD;

    cfg.TimeOutActivation  = QSPI_TIMEOUT_COUNTER_DISABLE;

    return HAL_QSPI_MemoryMapped(&hqspi, &cmd, &cfg);
}
```

### 3. Linux SPI via spidev (C, userspace)

For Linux-based embedded platforms (Raspberry Pi, Beaglebone, Yocto targets), the `spidev` driver exposes SPI as a character device.

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define SPI_DEVICE  "/dev/spidev0.0"
#define SPI_SPEED   10000000   /* 10 MHz */
#define SPI_MODE    SPI_MODE_0

/**
 * @brief  Open and configure a Linux spidev device.
 * @return File descriptor, or -1 on error.
 */
int spi_open(void)
{
    int fd = open(SPI_DEVICE, O_RDWR);
    if (fd < 0) { perror("open"); return -1; }

    uint8_t  mode  = SPI_MODE;
    uint8_t  bits  = 8;
    uint32_t speed = SPI_SPEED;

    if (ioctl(fd, SPI_IOC_WR_MODE,           &mode)  < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD,  &bits)  < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ,   &speed) < 0)
    {
        perror("ioctl config"); close(fd); return -1;
    }
    return fd;
}

/**
 * @brief  Execute a full-duplex SPI transfer via spidev ioctl.
 */
int spi_transfer(int fd, const uint8_t *tx, uint8_t *rx, size_t len)
{
    struct spi_ioc_transfer xfer = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = (uint32_t)len,
        .speed_hz      = SPI_SPEED,
        .delay_usecs   = 0,
        .bits_per_word = 8,
        .cs_change     = 0,
    };
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &xfer);
    if (ret < 0) perror("SPI_IOC_MESSAGE");
    return ret;
}

int main(void)
{
    int fd = spi_open();
    if (fd < 0) return 1;

    /* Example: read JEDEC ID from a SPI flash */
    uint8_t tx[4] = { 0x9F, 0x00, 0x00, 0x00 }; /* JEDEC READ ID command */
    uint8_t rx[4] = { 0 };

    spi_transfer(fd, tx, rx, 4);
    printf("JEDEC ID: %02X %02X %02X\n", rx[1], rx[2], rx[3]);

    close(fd);
    return 0;
}
```

### 4. DMA-Driven SPI (STM32, C)

DMA-driven SPI is essential for high-throughput or low-latency applications where software polling is unacceptable.

```c
#include "stm32h7xx_hal.h"

extern SPI_HandleTypeDef  hspi1;
extern DMA_HandleTypeDef  hdma_spi1_tx;
extern DMA_HandleTypeDef  hdma_spi1_rx;

static volatile uint8_t spi_dma_complete = 0;

/* Callback invoked by HAL when DMA RX is complete */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET);
        spi_dma_complete = 1;
    }
}

/**
 * @brief  Begin a non-blocking DMA SPI transfer.
 *         Caller must wait for spi_dma_complete or use RTOS signalling.
 */
HAL_StatusTypeDef spi_dma_transfer(const uint8_t *tx_buf,
                                    uint8_t       *rx_buf,
                                    uint16_t       len)
{
    spi_dma_complete = 0;

    /* Ensure D-cache coherency on Cortex-M7 */
    SCB_CleanDCache_by_Addr((uint32_t *)tx_buf, len);
    SCB_InvalidateDCache_by_Addr((uint32_t *)rx_buf, len);

    HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_RESET);

    return HAL_SPI_TransmitReceive_DMA(&hspi1,
                                       (uint8_t *)tx_buf,
                                       rx_buf,
                                       len);
}

/**
 * @brief  Blocking wrapper that waits for DMA completion.
 *         In a real RTOS application, replace the spin-wait
 *         with a semaphore take / event flag wait.
 */
HAL_StatusTypeDef spi_dma_transfer_blocking(const uint8_t *tx_buf,
                                              uint8_t       *rx_buf,
                                              uint16_t       len)
{
    HAL_StatusTypeDef ret = spi_dma_transfer(tx_buf, rx_buf, len);
    if (ret != HAL_OK) return ret;

    /* Spin-wait — replace with RTOS semaphore in production */
    uint32_t timeout = HAL_GetTick() + 1000;
    while (!spi_dma_complete) {
        if (HAL_GetTick() > timeout) return HAL_TIMEOUT;
    }
    return HAL_OK;
}
```

### 5. I3C Basic Transfer (Linux kernel / libI3C concept, C)

Linux I3C support was merged starting with kernel 5.0. The following illustrates userspace I3C device interaction via the `i3cdev` character device interface (similar to `spidev`).

```c
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i3c/i3cdev.h>   /* requires kernel ≥ 5.18 + i3cdev module */

#define I3C_DEVICE   "/dev/i3c-0-1e"   /* bus 0, dynamic address 0x1E */

/**
 * @brief  Perform an I3C Private SDR write followed by a read (CCC or data).
 */
int i3c_write_read(int fd,
                   const uint8_t *wr_buf, size_t wr_len,
                   uint8_t       *rd_buf, size_t rd_len)
{
    struct i3c_ioc_priv_xfer xfers[2] = {
        {
            .data  = (uintptr_t)wr_buf,
            .len   = (uint16_t)wr_len,
            .rnw   = 0,   /* write */
        },
        {
            .data  = (uintptr_t)rd_buf,
            .len   = (uint16_t)rd_len,
            .rnw   = 1,   /* read */
        },
    };

    return ioctl(fd, I3C_IOC_PRIV_XFER(2), xfers);
}

/**
 * @brief  Send a Common Command Code (CCC) — e.g. SETMWL (0x09).
 */
int i3c_send_ccc(int fd, uint8_t ccc_id, uint8_t value)
{
    uint8_t buf[2] = { ccc_id, value };

    struct i3c_ioc_priv_xfer xfer = {
        .data = (uintptr_t)buf,
        .len  = 2,
        .rnw  = 0,
    };
    return ioctl(fd, I3C_IOC_PRIV_XFER(1), &xfer);
}

int main(void)
{
    int fd = open(I3C_DEVICE, O_RDWR);
    if (fd < 0) { perror("open i3cdev"); return 1; }

    /* Read 2-byte device ID from register 0x00 */
    uint8_t reg  = 0x00;
    uint8_t id[2] = { 0 };

    if (i3c_write_read(fd, &reg, 1, id, 2) < 0)
        perror("i3c_write_read");
    else
        printf("Device ID: 0x%02X%02X\n", id[0], id[1]);

    close(fd);
    return 0;
}
```

---

## Programming Examples in Rust

### 1. Embedded HAL SPI Trait (Rust, no_std)

The `embedded-hal` crate defines a portable SPI abstraction used across all major Rust embedded targets (STM32, nRF, RP2040, ESP32, etc.).

```rust
//! Generic SPI sensor driver using embedded-hal 1.0 traits.
//! Compatible with any MCU that implements embedded_hal::spi::SpiDevice.

#![no_std]

use embedded_hal::spi::SpiDevice;

/// Errors returned by the sensor driver.
#[derive(Debug)]
pub enum SensorError<E> {
    Spi(E),
    InvalidId(u8),
}

/// Minimal SPI sensor driver (e.g. BMI088 IMU).
pub struct SpiSensor<SPI> {
    spi: SPI,
}

impl<SPI, E> SpiSensor<SPI>
where
    SPI: SpiDevice<Error = E>,
{
    pub fn new(spi: SPI) -> Self {
        Self { spi }
    }

    /// Read a single register. MSB set = read transaction.
    pub fn read_register(&mut self, addr: u8) -> Result<u8, SensorError<E>> {
        let mut buf = [addr | 0x80, 0x00];
        self.spi.transfer_in_place(&mut buf).map_err(SensorError::Spi)?;
        Ok(buf[1])
    }

    /// Write a single register.
    pub fn write_register(&mut self, addr: u8, value: u8) -> Result<(), SensorError<E>> {
        let buf = [addr & 0x7F, value];
        self.spi.write(&buf).map_err(SensorError::Spi)
    }

    /// Read the CHIP_ID register and validate.
    pub fn init(&mut self) -> Result<(), SensorError<E>> {
        let id = self.read_register(0x00)?;
        if id != 0x1E {
            return Err(SensorError::InvalidId(id));
        }
        Ok(())
    }

    /// Burst-read N bytes starting at `start_addr`.
    pub fn burst_read<const N: usize>(
        &mut self,
        start_addr: u8,
    ) -> Result<[u8; N], SensorError<E>> {
        let mut buf = [0u8; N];
        // embedded-hal 1.0 transaction() manages CS internally
        self.spi.transaction(&mut [
            embedded_hal::spi::Operation::Write(&[start_addr | 0x80]),
            embedded_hal::spi::Operation::Read(&mut buf),
        ])
        .map_err(SensorError::Spi)?;
        Ok(buf)
    }
}
```

### 2. Async QSPI on Embassy (Rust, no_std)

Embassy is the leading async embedded Rust framework. The `embassy-stm32` crate provides native QSPI/OSPI support.

```rust
//! Async QSPI Flash read using Embassy on STM32H7.

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::qspi::{
    self, AddressSize, ChipSelectHighTime, FIFOThresholdLevel,
    MemorySize, Qspi, QspiWidth, TransferConfig,
};
use embassy_stm32::Config;
use {defmt_rtt as _, panic_probe as _};

/// W25Q128 JEDEC READ ID command
const CMD_JEDEC_ID: u8 = 0x9F;
/// W25Q128 Fast Read Quad IO command
const CMD_FAST_READ_QIO: u8 = 0xEB;

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = embassy_stm32::init(Config::default());

    // Configure QSPI peripheral
    let qspi_config = qspi::Config {
        memory_size:           MemorySize::_128MiB,
        address_size:          AddressSize::_24bit,
        prescaler:             2,                          // AHB/3 → ~66 MHz
        cs_high_time:          ChipSelectHighTime::_5Cycle,
        fifo_threshold:        FIFOThresholdLevel::_16Bytes,
    };

    let mut qspi = Qspi::new_bank1(
        p.QUADSPI, p.PB6, p.PD11, p.PD12, p.PE2, p.PA1, p.PB2,
        qspi_config,
    );

    // Read JEDEC ID (1-line instruction + 1-line data)
    let jedec_cfg = TransferConfig {
        iwidth:  QspiWidth::SING,
        awidth:  QspiWidth::NONE,
        dwidth:  QspiWidth::SING,
        instruction: CMD_JEDEC_ID,
        address: None,
        dummy:   0,
    };

    let mut jedec = [0u8; 3];
    qspi.read(&mut jedec, jedec_cfg).await;
    defmt::info!("JEDEC ID: {:02X} {:02X} {:02X}", jedec[0], jedec[1], jedec[2]);

    // Fast Read Quad IO (1-line instruction, 4-line address + data)
    let read_cfg = TransferConfig {
        iwidth:  QspiWidth::SING,
        awidth:  QspiWidth::QUAD,
        dwidth:  QspiWidth::QUAD,
        instruction: CMD_FAST_READ_QIO,
        address: Some(0x0000_0000),
        dummy:   6,   // 6 dummy cycles as per W25Q128 datasheet
    };

    let mut page = [0u8; 256];
    qspi.read(&mut page, read_cfg).await;
    defmt::info!("First 4 bytes: {:02X} {:02X} {:02X} {:02X}",
        page[0], page[1], page[2], page[3]);
}
```

### 3. Linux spidev in Rust (std)

The `spidev` crate wraps the Linux `spidev` ioctl interface.

```rust
//! Linux SPI via spidev crate.
//! Cargo.toml: spidev = "0.5"

use spidev::{SpiModeFlags, Spidev, SpidevOptions, SpidevTransfer};
use std::io;

/// Open and configure a spidev device.
fn open_spi(path: &str, speed_hz: u32) -> io::Result<Spidev> {
    let mut spi = Spidev::open(path)?;
    let opts = SpidevOptions::new()
        .bits_per_word(8)
        .max_speed_hz(speed_hz)
        .mode(SpiModeFlags::SPI_MODE_0)
        .build();
    spi.configure(&opts)?;
    Ok(spi)
}

/// Full-duplex SPI transfer.
fn spi_transfer(spi: &mut Spidev, tx: &[u8], rx: &mut [u8]) -> io::Result<()> {
    let mut transfer = SpidevTransfer::read_write(tx, rx);
    spi.transfer(&mut transfer)
}

fn main() -> io::Result<()> {
    let mut spi = open_spi("/dev/spidev0.0", 10_000_000)?;

    // Read JEDEC ID from SPI NOR Flash
    let tx = [0x9F_u8, 0x00, 0x00, 0x00];
    let mut rx = [0u8; 4];
    spi_transfer(&mut spi, &tx, &mut rx)?;

    println!(
        "Manufacturer: {:02X}, Memory Type: {:02X}, Capacity: {:02X}",
        rx[1], rx[2], rx[3]
    );
    Ok(())
}
```

### 4. I3C Device Interaction (Rust, Linux, conceptual)

Linux I3C userspace access is still maturing. The following demonstrates the approach using raw `ioctl` bindings, mirroring the C example.

```rust
//! I3C userspace access via raw ioctl (Linux kernel ≥ 5.18).
//! Requires i3cdev kernel module and nix = "0.27" in Cargo.toml.

use nix::ioctl_readwrite;
use std::fs::{File, OpenOptions};
use std::os::unix::io::AsRawFd;

/// Mirror of `struct i3c_ioc_priv_xfer` from <linux/i3c/i3cdev.h>
#[repr(C)]
pub struct I3cPrivXfer {
    pub data: u64,   /* pointer to buffer */
    pub len:  u16,
    pub rnw:  u8,    /* 0 = write, 1 = read */
    pub pad:  [u8; 5],
}

/// I3C ioctl magic number and command number (from kernel header).
const I3C_IOC_MAGIC: u8 = 0x07;

ioctl_readwrite!(i3c_ioc_priv_xfer_1, I3C_IOC_MAGIC, 30, I3cPrivXfer);

/// Write then read from an I3C device.
pub fn i3c_write_read(
    file: &File,
    wr_buf: &[u8],
    rd_buf: &mut [u8],
) -> nix::Result<()> {
    let mut xfers = [
        I3cPrivXfer {
            data: wr_buf.as_ptr() as u64,
            len:  wr_buf.len() as u16,
            rnw:  0,
            pad:  [0; 5],
        },
        I3cPrivXfer {
            data: rd_buf.as_mut_ptr() as u64,
            len:  rd_buf.len() as u16,
            rnw:  1,
            pad:  [0; 5],
        },
    ];

    // Safety: file is a valid I3C device fd; xfers layout matches kernel ABI.
    unsafe {
        i3c_ioc_priv_xfer_1(file.as_raw_fd(), &mut xfers[0])?;
    }
    Ok(())
}

fn main() -> nix::Result<()> {
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .open("/dev/i3c-0-1e")
        .expect("Failed to open I3C device");

    let reg_addr = [0x00_u8];
    let mut device_id = [0u8; 2];

    i3c_write_read(&file, &reg_addr, &mut device_id)?;
    println!("I3C Device ID: 0x{:02X}{:02X}", device_id[0], device_id[1]);

    Ok(())
}
```

### 5. Future: Portable Bus Abstraction (Rust, embedded-hal-bus)

The `embedded-hal-bus` crate enables safe sharing of SPI buses between multiple drivers—a key step toward the multi-master abstractions that I3C provides natively.

```rust
//! Shared SPI bus with embedded-hal-bus (mutex-protected sharing).
//! Cargo.toml: embedded-hal-bus = "0.2"

use embedded_hal_bus::spi::MutexDevice;
use std::sync::Mutex;

// Suppose `raw_spi` is a concrete SPI bus implementation.
// Wrap it in a Mutex for shared access:
fn setup_shared_bus<SPI, CS1, CS2>(
    raw_spi: SPI,
    cs1: CS1,
    cs2: CS2,
) where
    SPI: embedded_hal::spi::SpiBus,
    CS1: embedded_hal::digital::OutputPin,
    CS2: embedded_hal::digital::OutputPin,
{
    let bus = Mutex::new(raw_spi);

    // Two independent devices sharing one physical SPI bus,
    // each with their own CS pin. MutexDevice handles CS assertion
    // and ensures exclusive access — no separate arbitration logic needed.
    let _device1 = MutexDevice::new(&bus, cs1);
    let _device2 = MutexDevice::new(&bus, cs2);

    // Each device can now be passed to independent drivers
    // without any awareness of the shared bus.
}
```

---

## Comparison Table

| Protocol | Wires | Max Speed | Duplex | Multi-master | IBI | Power Mgmt | Typical Use |
|---|---|---|---|---|---|---|---|
| SPI (classic) | 4+ | ~50 MHz | Full | No | No | No | Flash, ADC, sensors |
| QSPI | 6 | ~200 MHz | Half | No | No | No | NOR Flash, XIP |
| OSPI DDR | 10 | ~400 MHz | Half | No | No | No | PSRAM, HyperFlash |
| I²C | 2 | 5 MHz | Half | Yes | No | Partial | Low-speed sensors |
| I3C SDR | 2 | 12.5 MHz | Half | Yes | Yes | Yes | Sensor hub, IMUs |
| I3C HDR-DDR | 2 | 25 MHz | Half | Yes | Yes | Yes | High-speed sensors |
| I3C HDR-TSP | 2 | ~100 Mbps | Half | Yes | Yes | Yes | Camera, 5G RFFE |
| SoundWire | 2 | 19.2 Mbps | Half | Yes | Yes | Yes | Audio codecs |
| MIPI RFFE | 2 | 52 Mbps | Half | Yes | Partial | Yes | RF front-end |

---

## Migration Strategies

### When to Stay with SPI

- Point-to-point high-speed Flash or SRAM access (QSPI/OSPI is unmatched for raw throughput)
- Designs with already-qualified PCB layouts and component libraries
- Ultra-low BOM cost, single peripheral, no need for in-band interrupts
- Legacy sensor libraries with no I3C equivalent

### When to Move to I3C

- Four or more sensors sharing a bus (GPIO savings become significant)
- Battery-powered applications needing IBI for event-driven wake
- 5G/6G designs requiring MIPI RFFE or CSI-3 compliance
- New product lines targeting JEDEC DDR5 (SPD5 hub mandates I3C)
- Any design currently using both I²C (slow config) and SPI (fast data) — I3C HDR unifies both

### Incremental Path

Many SoCs now include both SPI and I3C peripherals. A practical migration path:

1. Keep existing SPI Flash and display interfaces (QSPI performance is irreplaceable here).
2. Replace the I²C sensor bus with I3C for new sensor revisions — I3C is backward-compatible with legacy I²C devices in SDR mode.
3. Adopt `embedded-hal` / `embedded-hal-bus` abstractions in firmware so that the physical interface is an implementation detail.
4. Evaluate MIPI SoundWire as the I2S + SPI combo replacement for audio subsystems.

---

## Summary

SPI's future is bifurcated: **enhancement** for high-bandwidth memory interfaces, and **replacement** for multi-device sensor buses.

On the enhancement side, **Quad SPI and Octal SPI with DDR clocking** have transformed Flash and PSRAM into near-DRAM-speed storage, enabling execute-in-place for complex applications on small microcontrollers. Tight integration with **DMA, cache coherency management, on-the-fly decryption, and memory-mapped addressing** elevates modern QSPI far beyond the original 4-wire protocol.

On the replacement side, **MIPI I3C** represents the most credible successor for the multi-sensor bus role currently split between I²C and SPI. With two wires, dynamic addressing, in-band interrupts, and speeds up to 100 Mbps (HDR-TSP), I3C addresses nearly every structural weakness of SPI while adding power management and multi-master arbitration. Its adoption in DDR5 memory modules (SPD5), 5G RF front-ends (RFFE v3), and emerging camera interfaces (CSI-3) ensures industrial momentum.

From a programming perspective, the ecosystem is maturing rapidly:
- **C/C++ on bare metal:** STM32 HAL QSPI, OSPI, and the nascent Linux `i3cdev` ioctl interface.
- **Rust:** `embedded-hal` 1.0's SPI traits provide portable, zero-cost driver abstractions; `embassy` delivers async QSPI; `embedded-hal-bus` solves shared-bus safety—all pointing toward a future where the physical protocol is a plug-in implementation detail.

Engineers designing products for 2026 and beyond should default to **QSPI/OSPI for Flash/RAM** and **I3C for sensor buses**, while keeping SPI in the toolkit for legacy compatibility and niche use cases where its simplicity and full-duplex capability remain advantageous.

---

*Document: 100_Future_of_SPI_Technology.md — Part of the SPI Technology Reference Series*