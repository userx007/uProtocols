# 66. IMU and Gyroscope SPI

- **Architecture** — How accelerometers, gyroscopes, and magnetometers work, their typical FSR/resolution specs, and why raw data alone is insufficient
- **SPI Protocol** — Bit-level transaction diagrams, the read/write bit convention, burst reads, and electrical considerations (decoupling, voltage levels, trace length)
- **Register Map** — ICM-42688-P register layout and raw-to-physical unit conversion formulas
- **C/C++ Implementation** — Full HAL abstraction via function pointers, complete ICM-42688-P driver (`init`, `reg_read`, `burst_read`, `read`), complementary filter, FIFO burst-read pattern, and an STM32 application example
- **Rust Implementation** — `embedded-hal 1.0` trait-based driver with proper error types, enum-driven FSR config, Madgwick quaternion AHRS filter with Euler conversion, and an RTIC real-time application skeleton
- **Sensor Fusion** — Algorithm comparison table (Complementary → EKF), drift sources, and mitigation strategies
- **Interrupt Handling** — DRDY-driven sampling in both C and Rust/RTIC
- **Calibration** — Gyroscope static bias averaging and 6-position accelerometer calibration outline
- **Troubleshooting** — Seven common failure modes with root causes and fixes
- **Summary** — Four-layer mental model tying it all together

## Interfacing with Inertial Measurement Units via SPI for Motion Tracking

---

## Table of Contents

1. [Introduction](#introduction)
2. [IMU Architecture and Sensor Fusion](#imu-architecture-and-sensor-fusion)
3. [SPI Protocol Fundamentals for IMUs](#spi-protocol-fundamentals-for-imus)
4. [Register Map and Communication Model](#register-map-and-communication-model)
5. [C/C++ Implementation](#cc-implementation)
6. [Rust Implementation](#rust-implementation)
7. [Sensor Fusion Algorithms](#sensor-fusion-algorithms)
8. [Interrupt Handling and Data-Ready Signals](#interrupt-handling-and-data-ready-signals)
9. [Calibration Techniques](#calibration-techniques)
10. [Common Pitfalls and Troubleshooting](#common-pitfalls-and-troubleshooting)
11. [Summary](#summary)

---

## Introduction

An **Inertial Measurement Unit (IMU)** is an electronic device that uses a combination of accelerometers, gyroscopes, and sometimes magnetometers to measure a body's specific force, angular rate, and magnetic field orientation. IMUs are central to motion tracking in applications such as drones, robotics, wearable devices, automotive systems, and virtual reality headsets.

Modern IMUs expose their data through **SPI (Serial Peripheral Interface)**, a synchronous full-duplex serial communication protocol that offers:

- High clock speeds (up to 10–20 MHz on most IMUs)
- Deterministic, low-latency data access
- Simple 4-wire interface: SCLK, MOSI, MISO, CS̄
- No addressing overhead (unlike I²C), making it ideal for time-critical sensor reads

Representative IMU devices commonly accessed via SPI include:

| Device | Axes | Interface Speed | Notes |
|--------|------|----------------|-------|
| MPU-6000 (InvenSense) | 6-axis (Accel + Gyro) | Up to 20 MHz | Very common, legacy |
| ICM-42688-P (TDK) | 6-axis | Up to 24 MHz | High precision, modern |
| BMI088 (Bosch) | 6-axis | Up to 10 MHz | Vibration-robust |
| LSM6DSO (ST) | 6-axis | Up to 10 MHz | Embedded ML core |
| ICM-20948 (TDK) | 9-axis (+ Magnetometer) | Up to 7 MHz | Includes AK09916 mag |

---

## IMU Architecture and Sensor Fusion

### Accelerometer

Measures linear acceleration (including gravity) along three orthogonal axes (X, Y, Z) in units of **g** (9.81 m/s²). MEMS accelerometers use a proof mass suspended by springs; deflection under acceleration is detected capacitively.

**Full-scale ranges:** typically ±2g, ±4g, ±8g, ±16g  
**Resolution:** 16-bit ADC → at ±2g, LSB = 0.061 mg/LSB

### Gyroscope

Measures angular velocity (rate of rotation) around three axes in **degrees per second (dps)** or **radians per second**. MEMS gyroscopes exploit the Coriolis effect using a vibrating proof mass.

**Full-scale ranges:** typically ±125, ±250, ±500, ±1000, ±2000 dps  
**Resolution:** 16-bit ADC → at ±250 dps, LSB ≈ 0.0076 °/s/LSB

### Magnetometer (optional, 9-axis)

Measures magnetic field strength along three axes in **microtesla (µT)** or **milligauss**. Used primarily for absolute heading (yaw) reference to prevent gyroscope drift in the yaw axis.

### Sensor Fusion Overview

Raw sensor data alone is insufficient for accurate orientation. Integration of gyroscope data drifts over time; accelerometers are noisy and sensitive to vibration. Fusion algorithms combine both:

```
Orientation ← f(Gyroscope, Accelerometer [, Magnetometer], Δt)
```

Common fusion approaches:

- **Complementary Filter** — Lightweight, embeds well on microcontrollers
- **Mahony Filter** — PI controller-based, very popular in flight controllers
- **Madgwick Filter** — Gradient-descent quaternion-based, accurate and efficient
- **Kalman Filter (EKF/UKF)** — Optimal statistically but computationally expensive

---

## SPI Protocol Fundamentals for IMUs

Most IMUs use **SPI Mode 0** (CPOL=0, CPHA=0) or **SPI Mode 3** (CPOL=1, CPHA=1). Always check the datasheet — MPU-6000 uses Mode 0/3 selectively, while ICM-42688-P uses Mode 0 and Mode 3.

### Typical SPI Read Transaction (8-bit register protocol)

```
CS̄  ‾\___________________________________________/‾
CLK  _/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_ ... _/‾\_
MOSI [R/W|A6|A5|A4|A3|A2|A1|A0][D7|D6|D5|D4|D3|D2|D1|D0]
MISO [  don't care first byte  ][D7|D6|D5|D4|D3|D2|D1|D0]
```

- **Bit 7 of the first byte** = R/W̄: `1` = Read, `0` = Write
- **Bits 6:0** = register address
- For multi-byte (burst) reads, CS̄ is held low and the address auto-increments

### SPI Electrical Considerations

- **CS̄ setup time**: Most IMUs require a minimum CS̄ assertion time (e.g., 5 µs for MPU-9250) before the first clock edge
- **Voltage levels**: 3.3 V logic is standard; level shifting required for 5 V MCUs
- **Decoupling**: 100 nF + 10 µF capacitors close to VDD/VDDIO pins
- **SPI clock**: Start at ≤1 MHz during bring-up; increase once communication is verified

---

## Register Map and Communication Model

Using the **ICM-42688-P** as a canonical modern IMU example:

```
Bank 0 Registers (default bank):
  0x00  DEVICE_CONFIG
  0x14  INT_CONFIG
  0x1D  FIFO_CONFIG
  0x1F  TEMP_DATA1       (high byte)
  0x20  TEMP_DATA0       (low byte)
  0x21  ACCEL_DATA_X1    (high)
  0x22  ACCEL_DATA_X0    (low)
  0x23  ACCEL_DATA_Y1
  0x24  ACCEL_DATA_Y0
  0x25  ACCEL_DATA_Z1
  0x26  ACCEL_DATA_Z0
  0x27  GYRO_DATA_X1
  0x28  GYRO_DATA_X0
  0x29  GYRO_DATA_Y1
  0x2A  GYRO_DATA_Y0
  0x2B  GYRO_DATA_Z1
  0x2C  GYRO_DATA_Z0
  0x2D  INT_STATUS
  0x4E  PWR_MGMT0
  0x4F  GYRO_CONFIG0
  0x50  ACCEL_CONFIG0
  0x76  WHO_AM_I         (should read 0x47)
```

### Data Conversion

Raw 16-bit signed integers are converted to physical units using sensitivity scale factors:

```
accel_g     = raw_accel  / accel_sensitivity    [e.g., 2048 LSB/g at ±16g]
gyro_dps    = raw_gyro   / gyro_sensitivity     [e.g., 16.4 LSB/dps at ±2000dps]
temp_degC   = (raw_temp / 132.48) + 25.0
```

---

## C/C++ Implementation

### Hardware Abstraction Layer (HAL) for SPI

```c
// imu_spi_hal.h — Platform-agnostic SPI HAL interface
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    void (*cs_assert)(void);           // Drive CS̄ LOW
    void (*cs_deassert)(void);         // Drive CS̄ HIGH
    void (*delay_us)(uint32_t us);     // Microsecond delay
    int  (*spi_transfer)(const uint8_t *tx, uint8_t *rx, size_t len);
} imu_spi_hal_t;
```

### ICM-42688-P Driver

```c
// icm42688.h
#pragma once
#include "imu_spi_hal.h"
#include <stdbool.h>

// Register addresses (Bank 0)
#define ICM42688_REG_DEVICE_CONFIG   0x00
#define ICM42688_REG_TEMP_DATA1      0x1F
#define ICM42688_REG_ACCEL_DATA_X1   0x21
#define ICM42688_REG_PWR_MGMT0       0x4E
#define ICM42688_REG_GYRO_CONFIG0    0x4F
#define ICM42688_REG_ACCEL_CONFIG0   0x50
#define ICM42688_REG_WHO_AM_I        0x75
#define ICM42688_WHO_AM_I_VAL        0x47

// Full-scale range configurations
typedef enum {
    GYRO_FS_2000DPS  = 0,
    GYRO_FS_1000DPS  = 1,
    GYRO_FS_500DPS   = 2,
    GYRO_FS_250DPS   = 3,
    GYRO_FS_125DPS   = 4,
    GYRO_FS_62_5DPS  = 5,
    GYRO_FS_31_25DPS = 6,
    GYRO_FS_15_625DPS= 7,
} gyro_fsr_t;

typedef enum {
    ACCEL_FS_16G = 0,
    ACCEL_FS_8G  = 1,
    ACCEL_FS_4G  = 2,
    ACCEL_FS_2G  = 3,
} accel_fsr_t;

// Sensitivity lookup tables [LSB per unit]
static const float GYRO_SENSITIVITY[] = {
    16.4f, 32.8f, 65.5f, 131.0f, 262.0f, 524.3f, 1048.6f, 2097.2f
};
static const float ACCEL_SENSITIVITY[] = {
    2048.0f, 4096.0f, 8192.0f, 16384.0f
};

typedef struct {
    float ax, ay, az;  // acceleration [g]
    float gx, gy, gz;  // angular rate [deg/s]
    float temp_c;       // temperature [°C]
} imu_data_t;

typedef struct {
    imu_spi_hal_t hal;
    gyro_fsr_t  gyro_fsr;
    accel_fsr_t accel_fsr;
} icm42688_dev_t;
```

```c
// icm42688.c
#include "icm42688.h"
#include <string.h>

// --- Low-level SPI primitives ---

static uint8_t reg_read(icm42688_dev_t *dev, uint8_t reg) {
    uint8_t tx[2] = { (reg | 0x80), 0x00 };  // Set read bit
    uint8_t rx[2] = { 0 };
    dev->hal.cs_assert();
    dev->hal.spi_transfer(tx, rx, 2);
    dev->hal.cs_deassert();
    return rx[1];
}

static void reg_write(icm42688_dev_t *dev, uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { (reg & 0x7F), val };   // Clear read bit
    uint8_t rx[2] = { 0 };
    dev->hal.cs_assert();
    dev->hal.spi_transfer(tx, rx, 2);
    dev->hal.cs_deassert();
}

// Burst read: read `len` bytes starting from `reg`
static void burst_read(icm42688_dev_t *dev, uint8_t reg,
                        uint8_t *buf, size_t len) {
    uint8_t tx[len + 1];
    uint8_t rx[len + 1];
    memset(tx, 0, sizeof(tx));
    tx[0] = reg | 0x80;       // Read bit set; address auto-increments

    dev->hal.cs_assert();
    dev->hal.spi_transfer(tx, rx, len + 1);
    dev->hal.cs_deassert();

    memcpy(buf, &rx[1], len); // Skip first dummy byte
}

// --- Driver API ---

bool icm42688_init(icm42688_dev_t *dev,
                   gyro_fsr_t  gyro_fsr,
                   accel_fsr_t accel_fsr) {
    dev->gyro_fsr  = gyro_fsr;
    dev->accel_fsr = accel_fsr;

    // Soft reset
    reg_write(dev, ICM42688_REG_DEVICE_CONFIG, 0x01);
    dev->hal.delay_us(1000);  // Wait 1 ms for reset to complete

    // Verify WHO_AM_I
    uint8_t who = reg_read(dev, ICM42688_REG_WHO_AM_I);
    if (who != ICM42688_WHO_AM_I_VAL) return false;

    // Enable accel + gyro in low-noise mode
    // PWR_MGMT0: GYRO_MODE=11 (low noise), ACCEL_MODE=11 (low noise)
    reg_write(dev, ICM42688_REG_PWR_MGMT0, 0x0F);
    dev->hal.delay_us(200);   // Wait for mode switch

    // Gyroscope config: FSR | ODR=1kHz (0x06)
    reg_write(dev, ICM42688_REG_GYRO_CONFIG0,
              (uint8_t)((gyro_fsr << 5) | 0x06));

    // Accelerometer config: FSR | ODR=1kHz
    reg_write(dev, ICM42688_REG_ACCEL_CONFIG0,
              (uint8_t)((accel_fsr << 5) | 0x06));

    dev->hal.delay_us(10000); // Wait 10 ms for sensor startup
    return true;
}

bool icm42688_read(icm42688_dev_t *dev, imu_data_t *out) {
    // Burst-read 14 bytes: TEMP(2) + ACCEL_XYZ(6) + GYRO_XYZ(6)
    uint8_t raw[14];
    burst_read(dev, ICM42688_REG_TEMP_DATA1, raw, 14);

    // Reconstruct signed 16-bit values
    int16_t raw_temp = (int16_t)((raw[0]  << 8) | raw[1]);
    int16_t raw_ax   = (int16_t)((raw[2]  << 8) | raw[3]);
    int16_t raw_ay   = (int16_t)((raw[4]  << 8) | raw[5]);
    int16_t raw_az   = (int16_t)((raw[6]  << 8) | raw[7]);
    int16_t raw_gx   = (int16_t)((raw[8]  << 8) | raw[9]);
    int16_t raw_gy   = (int16_t)((raw[10] << 8) | raw[11]);
    int16_t raw_gz   = (int16_t)((raw[12] << 8) | raw[13]);

    // Check for invalid data (0x8000 = no new data on ICM-42688)
    if (raw_ax == (int16_t)0x8000) return false;

    float a_sens = ACCEL_SENSITIVITY[dev->accel_fsr];
    float g_sens = GYRO_SENSITIVITY[dev->gyro_fsr];

    out->ax     = (float)raw_ax / a_sens;
    out->ay     = (float)raw_ay / a_sens;
    out->az     = (float)raw_az / a_sens;
    out->gx     = (float)raw_gx / g_sens;
    out->gy     = (float)raw_gy / g_sens;
    out->gz     = (float)raw_gz / g_sens;
    out->temp_c = ((float)raw_temp / 132.48f) + 25.0f;

    return true;
}
```

### Complementary Filter for Roll/Pitch

```c
// complementary_filter.h
#pragma once

typedef struct {
    float roll;   // degrees, rotation around X axis
    float pitch;  // degrees, rotation around Y axis
    float alpha;  // filter coefficient (e.g., 0.98)
} comp_filter_t;

void comp_filter_init(comp_filter_t *f, float alpha) {
    f->roll  = 0.0f;
    f->pitch = 0.0f;
    f->alpha = alpha;
}

// dt: time delta in seconds
void comp_filter_update(comp_filter_t *f,
                        float ax, float ay, float az,
                        float gx, float gy,
                        float dt) {
    // Accelerometer-derived angles (degrees)
    float accel_roll  = atan2f(ay, az) * (180.0f / 3.14159265f);
    float accel_pitch = atan2f(-ax, sqrtf(ay*ay + az*az))
                        * (180.0f / 3.14159265f);

    // Complementary filter: trust gyro short-term, accel long-term
    f->roll  = f->alpha * (f->roll  + gx * dt) + (1.0f - f->alpha) * accel_roll;
    f->pitch = f->alpha * (f->pitch + gy * dt) + (1.0f - f->alpha) * accel_pitch;
}
```

### Main Application Example (STM32 / bare-metal)

```c
// main.c — example usage on STM32 with HAL
#include "icm42688.h"
#include "complementary_filter.h"
#include "stm32f4xx_hal.h"
#include <math.h>
#include <stdio.h>

// Platform-specific SPI callbacks
static SPI_HandleTypeDef hspi1;

static void spi_cs_assert(void)   { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET); }
static void spi_cs_deassert(void) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET); }
static void delay_us(uint32_t us) { /* implement via DWT or timer */ }

static int spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(&hspi1,
        (uint8_t *)tx, rx, (uint16_t)len, HAL_MAX_DELAY);
    return (s == HAL_OK) ? 0 : -1;
}

int main(void) {
    HAL_Init();
    // ... SystemClock_Config, MX_GPIO_Init, MX_SPI1_Init ...

    icm42688_dev_t imu = {
        .hal = {
            .cs_assert   = spi_cs_assert,
            .cs_deassert = spi_cs_deassert,
            .delay_us    = delay_us,
            .spi_transfer = spi_transfer,
        }
    };

    if (!icm42688_init(&imu, GYRO_FS_500DPS, ACCEL_FS_4G)) {
        Error_Handler(); // WHO_AM_I mismatch
    }

    comp_filter_t filter;
    comp_filter_init(&filter, 0.98f);

    imu_data_t data;
    uint32_t   last_tick = HAL_GetTick();

    while (1) {
        uint32_t now = HAL_GetTick();
        float    dt  = (now - last_tick) / 1000.0f;  // ms → seconds
        last_tick    = now;

        if (icm42688_read(&imu, &data)) {
            comp_filter_update(&filter,
                data.ax, data.ay, data.az,
                data.gx, data.gy,
                dt);

            printf("Roll: %6.2f°  Pitch: %6.2f°  Temp: %.1f°C\r\n",
                   filter.roll, filter.pitch, data.temp_c);
        }

        HAL_Delay(5); // ~200 Hz loop
    }
}
```

### FIFO Burst Read (High-Performance Pattern)

```c
// FIFO register addresses
#define ICM42688_REG_FIFO_CONFIG   0x1D
#define ICM42688_REG_FIFO_COUNT_H  0x2E
#define ICM42688_REG_FIFO_COUNT_L  0x2F
#define ICM42688_REG_FIFO_DATA     0x30
#define ICM42688_FIFO_PACKET_SIZE  16   // bytes per FIFO packet

void icm42688_fifo_enable(icm42688_dev_t *dev) {
    // Route sensor data to FIFO, stream mode
    reg_write(dev, ICM42688_REG_FIFO_CONFIG, 0x40);
}

int icm42688_fifo_read(icm42688_dev_t *dev,
                        imu_data_t *samples, int max_samples) {
    // Read FIFO count
    uint8_t cnt_h = reg_read(dev, ICM42688_REG_FIFO_COUNT_H);
    uint8_t cnt_l = reg_read(dev, ICM42688_REG_FIFO_COUNT_L);
    int fifo_count = ((cnt_h & 0x0F) << 8) | cnt_l;
    int n_packets  = fifo_count / ICM42688_FIFO_PACKET_SIZE;
    if (n_packets > max_samples) n_packets = max_samples;

    for (int i = 0; i < n_packets; i++) {
        uint8_t pkt[ICM42688_FIFO_PACKET_SIZE];
        burst_read(dev, ICM42688_REG_FIFO_DATA, pkt,
                   ICM42688_FIFO_PACKET_SIZE);
        // Parse packet: header(1), accel(6), gyro(6), temp(2), timestamp(...)
        // (packet format depends on FIFO_CONFIG1 settings)
        int16_t raw_ax = (int16_t)((pkt[1] << 8) | pkt[2]);
        int16_t raw_ay = (int16_t)((pkt[3] << 8) | pkt[4]);
        int16_t raw_az = (int16_t)((pkt[5] << 8) | pkt[6]);
        int16_t raw_gx = (int16_t)((pkt[7] << 8) | pkt[8]);
        int16_t raw_gy = (int16_t)((pkt[9] << 8) | pkt[10]);
        int16_t raw_gz = (int16_t)((pkt[11]<< 8) | pkt[12]);

        float a_sens = ACCEL_SENSITIVITY[dev->accel_fsr];
        float g_sens = GYRO_SENSITIVITY[dev->gyro_fsr];
        samples[i].ax = (float)raw_ax / a_sens;
        samples[i].ay = (float)raw_ay / a_sens;
        samples[i].az = (float)raw_az / a_sens;
        samples[i].gx = (float)raw_gx / g_sens;
        samples[i].gy = (float)raw_gy / g_sens;
        samples[i].gz = (float)raw_gz / g_sens;
    }
    return n_packets;
}
```

---

## Rust Implementation

Rust's type system and ownership model make IMU driver development safer and more expressive. The `embedded-hal` traits provide a platform-agnostic SPI abstraction.

### Cargo.toml

```toml
[package]
name = "icm42688-driver"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal    = "1.0"
embedded-hal-nb = "1.0"
nb              = "1"
libm            = "0.2"   # no_std math functions

[profile.release]
opt-level = "s"
lto       = true
```

### Error Type and Configuration

```rust
// src/lib.rs
#![no_std]

use embedded_hal::spi::SpiDevice;
use libm::{atan2f, sqrtf};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImuError<E> {
    Spi(E),
    InvalidDevice,
    NoNewData,
}

impl<E> From<E> for ImuError<E> {
    fn from(e: E) -> Self { ImuError::Spi(e) }
}

/// Gyroscope full-scale range
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum GyroFsr {
    Dps2000  = 0,
    Dps1000  = 1,
    Dps500   = 2,
    Dps250   = 3,
    Dps125   = 4,
}

/// Accelerometer full-scale range
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum AccelFsr {
    G16 = 0,
    G8  = 1,
    G4  = 2,
    G2  = 3,
}

impl GyroFsr {
    pub fn sensitivity(self) -> f32 {
        match self {
            GyroFsr::Dps2000 => 16.4,
            GyroFsr::Dps1000 => 32.8,
            GyroFsr::Dps500  => 65.5,
            GyroFsr::Dps250  => 131.0,
            GyroFsr::Dps125  => 262.0,
        }
    }
}

impl AccelFsr {
    pub fn sensitivity(self) -> f32 {
        match self {
            AccelFsr::G16 => 2048.0,
            AccelFsr::G8  => 4096.0,
            AccelFsr::G4  => 8192.0,
            AccelFsr::G2  => 16384.0,
        }
    }
}
```

### ICM-42688-P Driver Struct

```rust
// src/icm42688.rs
use super::*;

// Register map constants
const REG_DEVICE_CONFIG: u8  = 0x00;
const REG_TEMP_DATA1:    u8  = 0x1F;
const REG_ACCEL_DATA_X1: u8  = 0x21;
const REG_PWR_MGMT0:     u8  = 0x4E;
const REG_GYRO_CONFIG0:  u8  = 0x4F;
const REG_ACCEL_CONFIG0: u8  = 0x50;
const REG_WHO_AM_I:      u8  = 0x75;
const WHO_AM_I_VAL:      u8  = 0x47;

const READ_BIT: u8  = 0x80;
const WRITE_BIT: u8 = 0x7F;

/// Raw sensor data (integer)
#[derive(Debug, Default, Clone, Copy)]
pub struct RawImuData {
    pub accel: [i16; 3],
    pub gyro:  [i16; 3],
    pub temp:  i16,
}

/// Calibrated sensor data (floating point)
#[derive(Debug, Default, Clone, Copy)]
pub struct ImuData {
    /// Acceleration [g]
    pub accel: [f32; 3],
    /// Angular rate [°/s]
    pub gyro:  [f32; 3],
    /// Temperature [°C]
    pub temp_c: f32,
}

/// ICM-42688-P driver
pub struct Icm42688<SPI> {
    spi:       SPI,
    gyro_fsr:  GyroFsr,
    accel_fsr: AccelFsr,
}

impl<SPI, E> Icm42688<SPI>
where
    SPI: SpiDevice<u8, Error = E>,
{
    /// Create and initialize the driver.
    /// `spi` must be configured for SPI Mode 0, CPOL=0, CPHA=0.
    pub fn new(
        mut spi:   SPI,
        gyro_fsr:  GyroFsr,
        accel_fsr: AccelFsr,
    ) -> Result<Self, ImuError<E>> {
        // Soft reset — bit 0 of DEVICE_CONFIG
        spi.write(&[REG_DEVICE_CONFIG & WRITE_BIT, 0x01])?;
        // In a real system: block for ~1 ms here (platform delay)

        // Verify WHO_AM_I
        let mut buf = [REG_WHO_AM_I | READ_BIT, 0x00];
        spi.transfer_in_place(&mut buf)?;
        if buf[1] != WHO_AM_I_VAL {
            return Err(ImuError::InvalidDevice);
        }

        // Enable accel + gyro in low-noise mode
        spi.write(&[REG_PWR_MGMT0 & WRITE_BIT, 0x0F])?;

        // Gyroscope: FSR | ODR=1kHz
        let gyro_cfg = ((gyro_fsr as u8) << 5) | 0x06;
        spi.write(&[REG_GYRO_CONFIG0 & WRITE_BIT, gyro_cfg])?;

        // Accelerometer: FSR | ODR=1kHz
        let accel_cfg = ((accel_fsr as u8) << 5) | 0x06;
        spi.write(&[REG_ACCEL_CONFIG0 & WRITE_BIT, accel_cfg])?;

        Ok(Icm42688 { spi, gyro_fsr, accel_fsr })
    }

    /// Read raw 16-bit sensor values via burst SPI transaction.
    pub fn read_raw(&mut self) -> Result<RawImuData, ImuError<E>> {
        // 1 address byte + 14 data bytes = 15 total
        let mut buf = [0u8; 15];
        buf[0] = REG_TEMP_DATA1 | READ_BIT;
        self.spi.transfer_in_place(&mut buf)?;

        // ICM-42688 signals no new data with 0x8000
        let raw_ax = i16::from_be_bytes([buf[3], buf[4]]);
        if raw_ax == i16::MIN {
            return Err(ImuError::NoNewData);
        }

        Ok(RawImuData {
            temp:  i16::from_be_bytes([buf[1],  buf[2]]),
            accel: [
                raw_ax,
                i16::from_be_bytes([buf[5],  buf[6]]),
                i16::from_be_bytes([buf[7],  buf[8]]),
            ],
            gyro: [
                i16::from_be_bytes([buf[9],  buf[10]]),
                i16::from_be_bytes([buf[11], buf[12]]),
                i16::from_be_bytes([buf[13], buf[14]]),
            ],
        })
    }

    /// Read calibrated (physical-unit) sensor data.
    pub fn read(&mut self) -> Result<ImuData, ImuError<E>> {
        let raw     = self.read_raw()?;
        let a_sens  = self.accel_fsr.sensitivity();
        let g_sens  = self.gyro_fsr.sensitivity();

        Ok(ImuData {
            accel: raw.accel.map(|v| v as f32 / a_sens),
            gyro:  raw.gyro.map(|v|  v as f32 / g_sens),
            temp_c: (raw.temp as f32 / 132.48) + 25.0,
        })
    }

    /// Consume the driver and return the SPI bus.
    pub fn release(self) -> SPI { self.spi }
}
```

### Complementary Filter in Rust

```rust
// src/filter.rs
use libm::{atan2f, sqrtf};
use core::f32::consts::PI;

pub struct CompFilter {
    pub roll:  f32,
    pub pitch: f32,
    alpha:     f32,
}

impl CompFilter {
    pub fn new(alpha: f32) -> Self {
        CompFilter { roll: 0.0, pitch: 0.0, alpha }
    }

    /// Update filter with new IMU data.
    /// `gyro_*` in °/s, `accel_*` in g, `dt` in seconds.
    pub fn update(
        &mut self,
        ax: f32, ay: f32, az: f32,
        gx: f32, gy: f32,
        dt: f32,
    ) {
        let accel_roll  = atan2f(ay, az) * (180.0 / PI);
        let accel_pitch = atan2f(-ax, sqrtf(ay*ay + az*az)) * (180.0 / PI);

        self.roll  = self.alpha * (self.roll  + gx * dt)
                   + (1.0 - self.alpha) * accel_roll;
        self.pitch = self.alpha * (self.pitch + gy * dt)
                   + (1.0 - self.alpha) * accel_pitch;
    }
}
```

### Madgwick Quaternion Filter (Rust)

```rust
// src/madgwick.rs
//! Madgwick AHRS algorithm — gradient descent quaternion update.
//! Reference: Madgwick, S.O.H. (2010).

use libm::{sqrtf, fabsf};

#[derive(Clone, Copy, Debug)]
pub struct Quaternion {
    pub w: f32,
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Quaternion {
    pub fn identity() -> Self { Quaternion { w: 1.0, x: 0.0, y: 0.0, z: 0.0 } }

    fn normalize(mut self) -> Self {
        let norm = sqrtf(self.w*self.w + self.x*self.x
                       + self.y*self.y + self.z*self.z);
        if norm > 1e-6 {
            self.w /= norm; self.x /= norm;
            self.y /= norm; self.z /= norm;
        }
        self
    }

    /// Convert to Euler angles (roll, pitch, yaw) in degrees.
    pub fn to_euler(&self) -> (f32, f32, f32) {
        let roll  = libm::atan2f(
            2.0*(self.w*self.x + self.y*self.z),
            1.0 - 2.0*(self.x*self.x + self.y*self.y),
        ) * (180.0 / core::f32::consts::PI);

        let sinp  = 2.0*(self.w*self.y - self.z*self.x);
        let pitch = if fabsf(sinp) >= 1.0 {
            libm::copysignf(90.0, sinp)
        } else {
            libm::asinf(sinp) * (180.0 / core::f32::consts::PI)
        };

        let yaw   = libm::atan2f(
            2.0*(self.w*self.z + self.x*self.y),
            1.0 - 2.0*(self.y*self.y + self.z*self.z),
        ) * (180.0 / core::f32::consts::PI);

        (roll, pitch, yaw)
    }
}

pub struct MadgwickFilter {
    pub q:    Quaternion,
    beta:     f32,  // Filter gain (0.01–0.1 typical)
}

impl MadgwickFilter {
    pub fn new(beta: f32) -> Self {
        MadgwickFilter { q: Quaternion::identity(), beta }
    }

    /// 6-DOF update (no magnetometer).
    /// gyro: [°/s], accel: [g], dt: seconds
    pub fn update_6dof(
        &mut self,
        gyro:  [f32; 3],
        accel: [f32; 3],
        dt: f32,
    ) {
        let q = self.q;

        // Convert gyro to rad/s
        let [gx, gy, gz] = gyro.map(|v| v * (core::f32::consts::PI / 180.0));

        // Normalize accelerometer measurement
        let norm_a = sqrtf(accel[0]*accel[0] + accel[1]*accel[1] + accel[2]*accel[2]);
        if norm_a < 1e-6 { return; }
        let [ax, ay, az] = accel.map(|v| v / norm_a);

        // Gradient descent step (gravity direction only)
        let f1 = 2.0*(q.x*q.z - q.w*q.y) - ax;
        let f2 = 2.0*(q.w*q.x + q.y*q.z) - ay;
        let f3 = 1.0 - 2.0*(q.x*q.x + q.y*q.y) - az;

        let j11 = 2.0*q.z;  let j12 = -2.0*q.w;
        let j13 = 2.0*q.x;  let j14 =  2.0*q.y;  // unused for 6dof, kept for clarity

        let step_w = -j12*f1 + j11*f3;   // simplified gradient
        let step_x =  j11*f1 + j13*f2 - 2.0*q.x*f3;
        let step_y = -j11*f2 + j13*f1 - 2.0*q.y*f3;
        let step_z =  j12*f2 + j14*f1;

        let norm_s = sqrtf(step_w*step_w + step_x*step_x
                         + step_y*step_y + step_z*step_z);
        let (sw, sx, sy, sz) = if norm_s > 1e-6 {
            (step_w/norm_s, step_x/norm_s, step_y/norm_s, step_z/norm_s)
        } else { (0.0, 0.0, 0.0, 0.0) };

        // Rate of change of quaternion from gyroscope
        let qdot_w = 0.5*(-q.x*gx - q.y*gy - q.z*gz) - self.beta*sw;
        let qdot_x = 0.5*( q.w*gx + q.y*gz - q.z*gy) - self.beta*sx;
        let qdot_y = 0.5*( q.w*gy - q.x*gz + q.z*gx) - self.beta*sy;
        let qdot_z = 0.5*( q.w*gz + q.x*gy - q.y*gx) - self.beta*sz;

        self.q = Quaternion {
            w: q.w + qdot_w * dt,
            x: q.x + qdot_x * dt,
            y: q.y + qdot_y * dt,
            z: q.z + qdot_z * dt,
        }.normalize();
    }
}
```

### Application Entry Point (Rust + `rtic` on STM32)

```rust
// src/main.rs — using RTIC for real-time scheduling
#![no_std]
#![no_main]

use rtic::app;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    spi::{Mode, Phase, Polarity, Spi},
    gpio::NoPin,
};
use embedded_hal_bus::spi::ExclusiveDevice;

mod icm42688;
mod filter;
mod madgwick;

use icm42688::{Icm42688, AccelFsr, GyroFsr};
use madgwick::MadgwickFilter;

#[app(device = pac, peripherals = true)]
mod app {
    use super::*;

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        imu:    Icm42688</* SpiDevice type */>,
        filter: MadgwickFilter,
    }

    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        let dp   = cx.device;
        let rcc  = dp.RCC.constrain();
        let clocks = rcc.cfgr.sysclk(168.MHz()).freeze();

        let gpioa = dp.GPIOA.split();
        let sclk  = gpioa.pa5.into_alternate::<5>();
        let miso  = gpioa.pa6.into_alternate::<5>();
        let mosi  = gpioa.pa7.into_alternate::<5>();
        let cs    = gpioa.pa4.into_push_pull_output();

        let spi = Spi::new(
            dp.SPI1,
            (sclk, miso, mosi),
            Mode { polarity: Polarity::IdleLow, phase: Phase::CaptureOnFirstTransition },
            1.MHz(),
            &clocks,
        );

        let spi_dev = ExclusiveDevice::new(spi, cs,
            stm32f4xx_hal::delay::SystickDelay::new(cx.core.SYST, &clocks));

        let imu    = Icm42688::new(spi_dev, GyroFsr::Dps500, AccelFsr::G4)
            .expect("IMU init failed");
        let filter = MadgwickFilter::new(0.05);

        (Shared {}, Local { imu, filter })
    }

    #[task(binds = TIM2, local = [imu, filter])]
    fn sample(cx: sample::Context) {
        let imu    = cx.local.imu;
        let filter = cx.local.filter;

        if let Ok(data) = imu.read() {
            filter.update_6dof(data.gyro, data.accel, 0.005); // 200 Hz → 5 ms
            let (roll, pitch, yaw) = filter.q.to_euler();
            // Log or transmit roll, pitch, yaw over UART/USB
        }
    }
}
```

---

## Sensor Fusion Algorithms

### Algorithm Comparison

| Algorithm | CPU Cost | Memory | Accuracy | Yaw Drift | Best For |
|-----------|----------|--------|----------|-----------|----------|
| Complementary | Very low | Tiny | Moderate | Yes | Simple roll/pitch only |
| Mahony | Low | Small | Good | Reduced with mag | Embedded flight controllers |
| Madgwick | Low-Medium | Small | Good | Reduced with mag | General AHRS |
| EKF | High | Medium | Excellent | Minimal | Robotics, navigation |
| UKF | Very high | Large | Best | Minimal | High-precision systems |

### Drift Sources and Mitigation

**Gyroscope Bias Drift**: The dominant error source in MEMS gyros. Mitigated by:

- Static calibration at startup (averaging N samples while still)
- Temperature compensation (bias is temperature-dependent)
- Online bias estimation (Kalman filter state)

**Accelerometer Noise**: Vibration contaminates the gravity vector reference. Mitigated by:

- Low-pass filtering (digital filter in the IMU or software)
- Adaptive filter weight (reduce accel trust during high-g maneuvers)

**Magnetic Interference**: Hard and soft iron distortions corrupt the magnetometer heading. Mitigated by:

- Ellipsoid calibration using `magcal` sphere-fitting algorithms

---

## Interrupt Handling and Data-Ready Signals

Polling the IMU wastes CPU cycles and risks missing samples. The `INT1`/`INT2` pins signal data readiness.

### C — Interrupt-Driven Sampling

```c
// Triggered by IMU INT1 pin (configured as rising edge GPIO interrupt)
// Configure IMU: INT_CONFIG = 0x18 (active high, push-pull, pulsed)
// Register INT_SOURCE0 bit[3] = UI_DRDY_INT1_EN = 1

volatile bool g_imu_data_ready = false;

// GPIO interrupt handler (platform-specific)
void IMU_INT1_IRQHandler(void) {
    g_imu_data_ready = true;
    // Clear EXTI pending bit (STM32 example)
    EXTI->PR1 = (1 << IMU_INT_PIN);
}

// In main loop or RTOS task:
void imu_task(void) {
    while (1) {
        if (g_imu_data_ready) {
            g_imu_data_ready = false;
            imu_data_t data;
            icm42688_read(&imu_dev, &data);
            // process data ...
        }
    }
}
```

### Rust — Interrupt-Driven with RTIC

```rust
// In RTIC app — INT1 mapped to EXTI4 interrupt
#[task(binds = EXTI4, local = [imu, filter])]
fn imu_drdy(cx: imu_drdy::Context) {
    // Clear interrupt pending
    cx.device.EXTI.pr1.write(|w| w.pr4().set_bit());

    if let Ok(data) = cx.local.imu.read() {
        cx.local.filter.update_6dof(data.gyro, data.accel, 0.001);
    }
}
```

---

## Calibration Techniques

### Gyroscope Bias Calibration

```c
// Collect N samples while the sensor is perfectly still
#define CALIB_SAMPLES 2000

typedef struct { float gx, gy, gz; } gyro_bias_t;

gyro_bias_t calibrate_gyro(icm42688_dev_t *dev) {
    float sum_gx = 0, sum_gy = 0, sum_gz = 0;
    imu_data_t d;

    for (int i = 0; i < CALIB_SAMPLES; i++) {
        icm42688_read(dev, &d);
        sum_gx += d.gx;
        sum_gy += d.gy;
        sum_gz += d.gz;
        HAL_Delay(1); // 1 ms = 1 kHz samples
    }

    return (gyro_bias_t){
        .gx = sum_gx / CALIB_SAMPLES,
        .gy = sum_gy / CALIB_SAMPLES,
        .gz = sum_gz / CALIB_SAMPLES,
    };
}

// Apply bias: corrected = raw - bias
// Store in NVM (Flash, EEPROM) for persistence across power cycles
```

### Accelerometer Multi-Point Calibration (6-position)

```c
// Place sensor on each of 6 faces aligned with gravity (+X,-X,+Y,-Y,+Z,-Z)
// Collect ~500 samples per position, average each axis.
// Solve for scale factor and bias per axis:
//   corrected = (raw - bias) / scale
// Expected: each principal axis should read ±1.0 g when aligned with gravity.
```

---

## Common Pitfalls and Troubleshooting

### 1. WHO_AM_I Always Returns 0xFF or 0x00

- **Cause**: CS̄ not asserted, or SPI clock polarity/phase mismatch
- **Fix**: Check CPOL/CPHA setting; verify CS̄ is driven LOW before transfer; check voltage levels

### 2. Data Reads Are Consistent But Wrong Scale

- **Cause**: Wrong full-scale range used in sensitivity divisor
- **Fix**: Ensure software `gyro_fsr` / `accel_fsr` match the register configuration

### 3. Gyroscope Output Has Large Constant Offset

- **Cause**: Missing bias calibration; sensor not at rest during startup
- **Fix**: Implement static calibration; ensure device is motionless for first 2 seconds

### 4. Accelerometer Values Fluctuate Even When Still

- **Cause**: SPI clock too high, or power supply noise coupling into analog domain
- **Fix**: Reduce SPI clock; add decoupling capacitors; use separate analog/digital power rails if available

### 5. Roll/Pitch Estimate Drifts Over Time

- **Cause**: Complementary filter alpha too high (too much gyro weight) or bias not removed
- **Fix**: Remove gyro bias before filter; lower alpha; switch to Mahony/Madgwick

### 6. Communication Fails at High SPI Speeds

- **Cause**: PCB trace impedance mismatch, missing series resistors, or long wires
- **Fix**: Use ≤10 cm wires for >5 MHz; add 33 Ω series resistors on SCLK/MOSI; use ground plane

### 7. Burst Read Returns Stale Data

- **Cause**: Data latency — reading faster than ODR; no `DRDY` synchronization
- **Fix**: Respect ODR; use interrupt-driven reads keyed to `DRDY`

---

## Summary

Interfacing an IMU via SPI requires mastery of four interconnected layers:

**1. Physical Layer**: Correct SPI mode (CPOL/CPHA), CS̄ timing, voltage levels, and decoupling. Errors here manifest as garbled data or communication failure. Always start at ≤1 MHz and verify WHO_AM_I before increasing speed.

**2. Protocol Layer**: The read/write bit convention (bit 7 of address byte), burst read for efficiency, bank selection on multi-bank devices, and FIFO usage for high-rate data without CPU polling overhead.

**3. Driver Layer**: A well-abstracted HAL in C (function pointers) or Rust (`embedded-hal` traits) enables portable, testable drivers. Correct conversion of raw 16-bit ADC values to physical units (g, °/s, °C) via sensitivity tables is essential for meaningful output.

**4. Application Layer**: Raw IMU data is only useful after sensor fusion. Complementary filters are sufficient for simple roll/pitch on microcontrollers; Madgwick or Mahony filters provide full 3D orientation with good accuracy at low CPU cost; EKF is the standard for navigation-grade systems. Gyroscope bias calibration is non-negotiable for stable long-term orientation estimates.

Both C/C++ and Rust support clean, efficient IMU integration. Rust's ownership model and the `embedded-hal` ecosystem make type-safe, platform-portable drivers straightforward to write, while C remains indispensable for legacy embedded ecosystems and vendor-supplied HALs. Interrupt-driven data acquisition synchronized to the IMU's DRDY signal is strongly preferred over polling for deterministic, efficient operation in real-time systems.

---

*Document: 66 — IMU and Gyroscope SPI | Revision 1.0*