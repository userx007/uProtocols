# 99. Device Driver Documentation

The document is organized into 13 sections:

- **Hardware Interface Documentation** — what to capture about electrical constraints, addresses, speeds, pull-ups, and power sequencing before any code is written
- **API Documentation** — the 7-element contract every public function needs (description, params, returns, pre/postconditions, thread safety, side effects, example)
- **C/C++ Examples** — full Doxygen-style documentation for file headers, constants/macros, structs/enums, and functions, illustrated with a BME280 sensor driver
- **Rust Examples** — `rustdoc`-style module, enum, struct, and `impl` block documentation with Markdown tables and doctests
- **Register Maps** — bit-field documentation in both languages, including encoding macros and `const` helpers
- **Error Handling** — fully documented C enum and Rust `Error<E>` type with troubleshooting tables
- **Full Driver Fragments** — a complete documented AT24Cx EEPROM driver in both languages showing all conventions together
- **Changelog** — a `CHANGELOG.md` template with emphasis on I2C-specific behavioral changes
- **Summary** — six key principles distilled from the whole chapter

## Best Practices for Documenting I2C Drivers, APIs, and Hardware Interfaces

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Documentation Matters for I2C Drivers](#why-documentation-matters)
3. [Documentation Structure and Hierarchy](#documentation-structure-and-hierarchy)
4. [Hardware Interface Documentation](#hardware-interface-documentation)
5. [API Documentation](#api-documentation)
6. [C/C++ Code Documentation Examples](#cc-code-documentation-examples)
7. [Rust Code Documentation Examples](#rust-code-documentation-examples)
8. [Register Maps and Bit Field Documentation](#register-maps-and-bit-field-documentation)
9. [Error Handling Documentation](#error-handling-documentation)
10. [Example: Full Driver Documentation in C/C++](#example-full-driver-documentation-in-cc)
11. [Example: Full Driver Documentation in Rust](#example-full-driver-documentation-in-rust)
12. [Changelog and Versioning](#changelog-and-versioning)
13. [Summary](#summary)

---

## Introduction

Documenting I2C device drivers is a critical engineering discipline that bridges the gap between hardware specifications, firmware behavior, and software consumers. An I2C driver exposes a hardware peripheral — such as a sensor, EEPROM, or display controller — to higher-level application code. Without clear documentation, even a well-written driver becomes a source of confusion, bugs, and integration failures.

This chapter covers the best practices for documenting I2C drivers comprehensively: from hardware interface constraints and register maps, to API contracts, error handling, and usage examples. Code samples are provided in both **C/C++** and **Rust** to illustrate documentation conventions in each ecosystem.

---

## Why Documentation Matters for I2C Drivers

I2C drivers sit at the intersection of hardware and software. They must accurately reflect:

- **Physical hardware behavior**: timing constraints, voltage levels, pull-up requirements, address conflicts.
- **Protocol semantics**: START/STOP conditions, ACK/NACK handling, repeated starts, clock stretching.
- **Device-specific quirks**: some devices require specific byte sequences, mandatory delays, or power-on initialization.
- **Software contracts**: what the API guarantees, what it does not, thread safety, interrupt context restrictions.

Poor documentation leads to:
- Incorrect bus configuration (wrong speed, wrong address)
- Data corruption from misunderstood register layouts
- Race conditions from undocumented thread-safety assumptions
- Wasted debugging time re-discovering hardware quirks

---

## Documentation Structure and Hierarchy

A complete I2C driver documentation package should be organized as follows:

```
docs/
├── hardware/
│   ├── schematic_notes.md        # Pull-up resistors, power rails, address pins
│   ├── timing_requirements.md    # Setup/hold times, max clock speed
│   └── register_map.md           # All device registers, bit fields
├── api/
│   ├── initialization.md         # Bus and device init functions
│   ├── read_write.md             # Data transfer functions
│   └── error_codes.md            # All possible error returns
├── examples/
│   ├── basic_usage.c / .rs
│   └── advanced_usage.c / .rs
├── CHANGELOG.md
└── README.md
```

Each layer answers a different question:
- **hardware/** answers: "What does the silicon do?"
- **api/** answers: "What does the driver do?"
- **examples/** answers: "How do I use this?"

---

## Hardware Interface Documentation

Always begin with a hardware section that documents the physical and electrical constraints. This prevents fundamental misconfiguration before a single line of driver code is read.

### Template: Hardware Interface Header

```
Device:       BME280 (Bosch Environmental Sensor)
I2C Address:  0x76 (SDO=GND) or 0x77 (SDO=VDD)
Bus Speed:    Standard (100 kHz), Fast (400 kHz) supported
              Fast+ (1 MHz) NOT supported
VDD:          1.71V – 3.6V
Pull-ups:     4.7kΩ recommended for 100 kHz; 2.2kΩ for 400 kHz
Clock Stretch: Device may stretch clock during measurement conversion (up to 2 ms)
Max Burst:    Up to 256 bytes per transaction
Power-on Reset: Required. Allow 2 ms after VDD stable before first access.
```

### Key Hardware Facts to Document

- **I2C address(es)** — including how hardware address pins change them
- **Supported bus speeds** — always note the maximum clearly
- **Clock stretching** — does the device use it? For how long?
- **Power sequencing** — required delays before first communication
- **Electrical quirks** — if pull-up values matter beyond the norm

---

## API Documentation

Each public function in an I2C driver must be documented with:

1. **Brief description** — one sentence, what it does
2. **Parameters** — name, type, valid range, units
3. **Return value** — all possible values and what they mean
4. **Preconditions** — what must be true before calling
5. **Postconditions** — what will be true after calling
6. **Thread safety** — is it safe to call concurrently?
7. **Side effects** — does it modify hardware state?
8. **Example** — at least one usage snippet

---

## C/C++ Code Documentation Examples

### File Header Documentation

Every C/C++ driver file should begin with a structured file header:

```c
/**
 * @file    bme280_i2c.h
 * @brief   I2C driver for the Bosch BME280 temperature/pressure/humidity sensor.
 *
 * @details This driver provides a portable, blocking I2C interface for the BME280
 *          environmental sensor. It abstracts register-level access and presents
 *          a clean API for initialization, configuration, and measurement retrieval.
 *
 *          Supported modes:
 *            - Forced mode (single-shot measurement)
 *            - Normal mode (continuous measurement with configurable standby time)
 *
 *          Hardware requirements:
 *            - I2C bus initialized at 100 kHz or 400 kHz
 *            - 4.7 kΩ pull-up resistors on SDA/SCL (for 100 kHz)
 *            - Device address: 0x76 or 0x77 (see BME280_ADDR_* constants)
 *
 * @note    This driver is NOT interrupt-safe. Do not call from ISR context.
 *          For RTOS environments, protect all calls with a mutex.
 *
 * @version 2.1.0
 * @date    2025-03-10
 * @author  Embedded Systems Team
 *
 * @copyright Copyright (c) 2025 Example Corp. All rights reserved.
 *            SPDX-License-Identifier: MIT
 */
```

### Constant and Macro Documentation

```c
/**
 * @defgroup BME280_ADDR I2C Device Addresses
 * @brief    I2C addresses determined by the SDO pin level.
 * @{
 */

/** I2C address when SDO pin is connected to GND. Default configuration. */
#define BME280_ADDR_LOW   0x76U

/** I2C address when SDO pin is connected to VDD (3.3V). */
#define BME280_ADDR_HIGH  0x77U

/** @} */

/**
 * @defgroup BME280_REGS Register Addresses
 * @brief    Internal register map of the BME280.
 *           See datasheet section 5.3 for full register descriptions.
 * @{
 */

#define BME280_REG_CHIP_ID      0xD0U  /**< Chip ID register. Read-only. Expected value: 0x60 */
#define BME280_REG_RESET        0xE0U  /**< Soft-reset register. Write 0xB6 to reset. */
#define BME280_REG_CTRL_HUM     0xF2U  /**< Humidity oversampling control. Must be set before CTRL_MEAS. */
#define BME280_REG_STATUS       0xF3U  /**< Device status (measuring/updating bits). Read-only. */
#define BME280_REG_CTRL_MEAS    0xF4U  /**< Temperature/pressure oversampling and device mode. */
#define BME280_REG_CONFIG       0xF5U  /**< Standby time, IIR filter, SPI 3-wire enable. */
#define BME280_REG_PRESS_MSB    0xF7U  /**< Pressure data MSB (bits [19:12]). Read-only. */
#define BME280_REG_TEMP_MSB     0xFAU  /**< Temperature data MSB (bits [19:12]). Read-only. */
#define BME280_REG_HUM_MSB      0xFDU  /**< Humidity data MSB (bits [15:8]). Read-only. */

/** @} */
```

### Struct and Enum Documentation

```c
/**
 * @brief Oversampling settings for temperature, pressure, and humidity.
 *
 * Higher oversampling improves noise immunity at the cost of conversion time
 * and current consumption. See datasheet Table 4 for conversion times.
 *
 * @note  Oversampling of 0 (BME280_OS_SKIP) disables that measurement entirely.
 */
typedef enum {
    BME280_OS_SKIP = 0x00U, /**< Measurement skipped (output set to 0x80000) */
    BME280_OS_1X   = 0x01U, /**< 1x oversampling (lowest noise reduction, fastest) */
    BME280_OS_2X   = 0x02U, /**< 2x oversampling */
    BME280_OS_4X   = 0x03U, /**< 4x oversampling */
    BME280_OS_8X   = 0x04U, /**< 8x oversampling */
    BME280_OS_16X  = 0x05U  /**< 16x oversampling (highest noise reduction, slowest) */
} bme280_oversampling_t;

/**
 * @brief Driver configuration structure.
 *
 * Populate this structure and pass it to bme280_init() to configure the device.
 * All fields must be set; there are no default values.
 *
 * @example
 * @code
 * bme280_config_t cfg = {
 *     .i2c_addr    = BME280_ADDR_LOW,
 *     .os_temp     = BME280_OS_2X,
 *     .os_pressure = BME280_OS_4X,
 *     .os_humidity = BME280_OS_1X,
 * };
 * bme280_err_t err = bme280_init(&dev, &cfg);
 * @endcode
 */
typedef struct {
    uint8_t              i2c_addr;     /**< I2C device address (BME280_ADDR_LOW or BME280_ADDR_HIGH) */
    bme280_oversampling_t os_temp;     /**< Temperature oversampling setting */
    bme280_oversampling_t os_pressure; /**< Pressure oversampling setting */
    bme280_oversampling_t os_humidity; /**< Humidity oversampling setting. NOTE: set before os_temp/os_pressure (hardware requirement) */
} bme280_config_t;

/**
 * @brief Measurement result structure.
 *
 * All values are compensated using the device's factory calibration data.
 * Raw ADC values are NOT exposed by this API.
 */
typedef struct {
    int32_t  temperature;  /**< Temperature in 0.01 °C units. e.g., 2134 = 21.34 °C */
    uint32_t pressure;     /**< Pressure in Pa * 256. e.g., 24674867 / 256 = 96386 Pa */
    uint32_t humidity;     /**< Relative humidity in % * 1024. e.g., 47445 / 1024 = 46.33 %RH */
} bme280_data_t;
```

### Function Documentation

```c
/**
 * @brief  Initialize the BME280 driver and verify device communication.
 *
 * @details This function performs the following steps in order:
 *          1. Validates the @p cfg parameters.
 *          2. Issues a soft-reset to the device (write 0xB6 to REG_RESET).
 *          3. Waits 2 ms for the device to complete reset.
 *          4. Reads and validates the chip ID register (must read 0x60).
 *          5. Reads factory calibration data from device NVM.
 *          6. Writes the provided oversampling configuration.
 *          7. Puts the device in sleep mode (measurement not started).
 *
 * @param[out] dev  Pointer to the driver handle to be initialized.
 *                  Must not be NULL. The caller owns this memory.
 * @param[in]  cfg  Pointer to configuration parameters.
 *                  Must not be NULL. Copied internally; caller may free after call.
 *
 * @return BME280_OK               on success.
 * @return BME280_ERR_NULL_PTR     if @p dev or @p cfg is NULL.
 * @return BME280_ERR_I2C_NACK    if the device does not acknowledge its address.
 *                                 Check wiring, power, and I2C address setting.
 * @return BME280_ERR_CHIP_ID      if the chip ID register returns an unexpected value.
 *                                 Indicates a communication error or wrong device.
 * @return BME280_ERR_CALIB_READ   if calibration data cannot be read from device NVM.
 *
 * @pre  The underlying I2C peripheral must be initialized and running.
 * @pre  VDD must have been stable for at least 2 ms before calling this function.
 *
 * @note This function is blocking. It will occupy the I2C bus for approximately
 *       5 ms due to reset delay and multi-register reads.
 * @note NOT safe to call from ISR context.
 * @note NOT thread-safe. Protect with a mutex in RTOS environments.
 *
 * @see  bme280_read_forced() to trigger a single measurement after initialization.
 * @see  bme280_deinit() to release driver resources.
 */
bme280_err_t bme280_init(bme280_dev_t *dev, const bme280_config_t *cfg);

/**
 * @brief  Trigger a forced-mode measurement and retrieve compensated data.
 *
 * @details In forced mode, the device:
 *          1. Wakes from sleep.
 *          2. Performs one measurement cycle.
 *          3. Returns to sleep automatically.
 *
 *          This function blocks until the measurement is complete.
 *          Measurement duration depends on oversampling settings.
 *          At OS_1X for all channels, this is approximately 9 ms.
 *          At OS_16X for all channels, this is approximately 113 ms.
 *
 * @param[in]  dev   Pointer to an initialized driver handle. Must not be NULL.
 * @param[out] data  Pointer to a result structure. Must not be NULL.
 *                   On success, populated with compensated measurement values.
 *                   On error, contents are undefined.
 *
 * @return BME280_OK               on success, @p data is valid.
 * @return BME280_ERR_NULL_PTR     if @p dev or @p data is NULL.
 * @return BME280_ERR_NOT_INIT     if @p dev was not successfully initialized.
 * @return BME280_ERR_I2C_NACK    if communication fails during measurement.
 * @return BME280_ERR_TIMEOUT      if the device does not complete measurement
 *                                 within 500 ms (indicates hardware fault).
 *
 * @note Blocks for the duration of the measurement (typ. 9–113 ms).
 * @note This function acquires and releases the I2C bus internally.
 */
bme280_err_t bme280_read_forced(bme280_dev_t *dev, bme280_data_t *data);
```

---

## Rust Code Documentation Examples

Rust has a first-class documentation system built into the language via `rustdoc`. Documentation comments (`///` for items, `//!` for modules) support Markdown and are tested automatically with `cargo test`.

### Module-Level Documentation

```rust
//! # BME280 I2C Driver
//!
//! A portable, blocking I2C driver for the Bosch BME280 environmental sensor,
//! supporting temperature, pressure, and humidity measurements.
//!
//! ## Hardware Requirements
//!
//! - I2C bus speed: 100 kHz (Standard) or 400 kHz (Fast mode)
//! - Pull-up resistors: 4.7 kΩ at 100 kHz, 2.2 kΩ at 400 kHz
//! - Device address: [`Address::Low`] (SDO=GND) or [`Address::High`] (SDO=VDD)
//! - Power-on delay: allow 2 ms after VDD stable before first access
//!
//! ## Quick Start
//!
//! ```rust,no_run
//! use bme280_i2c::{Bme280, Config, Address, Oversampling};
//! use embedded_hal::i2c::I2c;
//!
//! fn read_sensor<I2C>(i2c: I2C)
//! where
//!     I2C: I2c,
//! {
//!     let config = Config::new(Address::Low)
//!         .with_temp_oversampling(Oversampling::X2)
//!         .with_pressure_oversampling(Oversampling::X4)
//!         .with_humidity_oversampling(Oversampling::X1);
//!
//!     let mut sensor = Bme280::new(i2c, config).expect("Failed to initialize BME280");
//!     let data = sensor.read_forced().expect("Failed to read measurement");
//!
//!     // Temperature is in units of 0.01 °C
//!     println!("Temperature: {:.2} °C", data.temperature as f32 / 100.0);
//! }
//! ```
//!
//! ## Thread Safety
//!
//! This driver is **not** thread-safe. In multi-threaded environments,
//! wrap the driver instance in a `Mutex` before sharing across threads.
//!
//! ## Feature Flags
//!
//! - `defmt`: Enables [`defmt`](https://docs.rs/defmt) logging for `no_std` targets.
//! - `std`: Enables `std::error::Error` implementation on [`Error`].
```

### Enum Documentation

```rust
/// I2C device address of the BME280, determined by the SDO pin.
///
/// The SDO pin selects the least-significant bit of the 7-bit I2C address.
///
/// # Schematic Note
///
/// If SDO is left floating, behavior is undefined. Always connect it
/// explicitly to either GND or VDD.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Address {
    /// Address `0x76`: SDO pin connected to GND.
    ///
    /// This is the default configuration in most breakout boards.
    Low = 0x76,

    /// Address `0x77`: SDO pin connected to VDD.
    ///
    /// Use this when two BME280 sensors share the same I2C bus.
    High = 0x77,
}

/// Oversampling multiplier for temperature, pressure, and humidity channels.
///
/// Higher oversampling reduces noise at the cost of longer conversion time
/// and higher average current consumption. See BME280 datasheet Table 4.
///
/// # Conversion Time Impact
///
/// At 100 Hz measurement rate in Normal mode, oversampling directly affects
/// achievable update rates. Use [`Skip`] to disable a channel entirely.
///
/// [`Skip`]: Oversampling::Skip
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Oversampling {
    /// Disables this measurement channel. Output register returns `0x80000`.
    Skip  = 0b000,
    /// 1x oversampling (fastest, lowest noise reduction).
    X1    = 0b001,
    /// 2x oversampling.
    X2    = 0b010,
    /// 4x oversampling.
    X4    = 0b011,
    /// 8x oversampling.
    X8    = 0b100,
    /// 16x oversampling (slowest, highest noise reduction).
    X16   = 0b101,
}
```

### Struct Documentation

```rust
/// Compensated measurement data from a single conversion cycle.
///
/// All values are compensated using the factory calibration coefficients
/// stored in the device's NVM. Raw ADC values are not exposed.
///
/// # Units
///
/// | Field         | Unit              | Example                      |
/// |---------------|-------------------|------------------------------|
/// | `temperature` | 0.01 °C           | `2134` → 21.34 °C            |
/// | `pressure`    | Pa × 256          | `24674867` → 96386.2 Pa      |
/// | `humidity`    | % RH × 1024       | `47445` → 46.33 %RH          |
///
/// # Conversion Helpers
///
/// ```rust
/// # let data = bme280_i2c::MeasurementData { temperature: 2134, pressure: 24674867, humidity: 47445 };
/// let temp_celsius   = data.temperature as f32 / 100.0;
/// let pressure_pa    = data.pressure    as f32 / 256.0;
/// let humidity_pct   = data.humidity    as f32 / 1024.0;
/// ```
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MeasurementData {
    /// Temperature in units of 0.01 °C.
    /// Valid range: -4000 (−40 °C) to +8500 (+85 °C).
    pub temperature: i32,

    /// Pressure in units of Pa × 256.
    /// Valid range: 30000 Pa to 110000 Pa (at 1x oversampling).
    /// Returns 0 if pressure oversampling is set to [`Oversampling::Skip`].
    pub pressure: u32,

    /// Relative humidity in units of % × 1024.
    /// Valid range: 0 to 102400 (0% to 100% RH).
    /// Returns 0 if humidity oversampling is set to [`Oversampling::Skip`].
    pub humidity: u32,
}
```

### Method Documentation

```rust
impl<I2C> Bme280<I2C>
where
    I2C: embedded_hal::i2c::I2c,
{
    /// Creates and initializes a new BME280 driver instance.
    ///
    /// This constructor performs the following initialization sequence:
    ///
    /// 1. Issues a soft-reset command (`0xB6` to register `0xE0`).
    /// 2. Waits 2 ms for the device to complete its internal reset.
    /// 3. Reads the chip ID register (`0xD0`) and verifies it equals `0x60`.
    /// 4. Reads factory calibration data from NVM (registers `0x88`–`0x9F` and `0xE1`–`0xF0`).
    /// 5. Applies the provided oversampling configuration.
    /// 6. Leaves the device in **sleep mode** (no measurement running).
    ///
    /// # Arguments
    ///
    /// * `i2c` – A value implementing [`embedded_hal::i2c::I2c`]. The driver
    ///   takes ownership and holds the bus for exclusive use.
    /// * `config` – Configuration specifying I2C address and oversampling settings.
    ///
    /// # Errors
    ///
    /// Returns [`Error::I2c`] if any I2C transaction fails (NACK, bus error, timeout).
    /// Returns [`Error::InvalidChipId`] if the chip ID register does not return `0x60`.
    ///
    /// # Timing
    ///
    /// Blocking. Occupies the I2C bus for approximately **5 ms** due to the
    /// mandatory reset delay and multi-register calibration reads.
    ///
    /// # Example
    ///
    /// ```rust,no_run
    /// use bme280_i2c::{Bme280, Config, Address};
    ///
    /// # fn example<I2C: embedded_hal::i2c::I2c>(i2c: I2C) {
    /// let config = Config::new(Address::Low);
    /// let sensor = Bme280::new(i2c, config).expect("BME280 init failed");
    /// # }
    /// ```
    pub fn new(i2c: I2C, config: Config) -> Result<Self, Error<I2C::Error>> {
        // ... implementation
        todo!()
    }

    /// Triggers a single forced-mode measurement and returns compensated data.
    ///
    /// In **forced mode**, the device wakes from sleep, performs one complete
    /// measurement cycle across all enabled channels, then returns to sleep.
    /// This is recommended for low-power, on-demand measurements.
    ///
    /// # Measurement Duration
    ///
    /// The function blocks until the hardware signals completion.
    /// Typical blocking times at common oversampling configurations:
    ///
    /// | Temp OS | Pressure OS | Humidity OS | Duration |
    /// |---------|-------------|-------------|----------|
    /// | 1x      | 1x          | 1x          | ~9 ms    |
    /// | 2x      | 4x          | 1x          | ~23 ms   |
    /// | 16x     | 16x         | 16x         | ~113 ms  |
    ///
    /// # Errors
    ///
    /// Returns [`Error::I2c`] if any I2C transaction fails.
    /// Returns [`Error::Timeout`] if the measurement does not complete within 500 ms,
    /// which indicates a hardware fault (device may need power cycling).
    ///
    /// # Example
    ///
    /// ```rust,no_run
    /// # use bme280_i2c::{Bme280, Config, Address};
    /// # fn example<I2C: embedded_hal::i2c::I2c>(i2c: I2C) {
    /// let mut sensor = Bme280::new(i2c, Config::new(Address::Low)).unwrap();
    ///
    /// let data = sensor.read_forced().unwrap();
    /// let temp = data.temperature as f32 / 100.0;
    /// let hum  = data.humidity    as f32 / 1024.0;
    /// println!("Temp: {:.2} °C, Humidity: {:.1} %", temp, hum);
    /// # }
    /// ```
    pub fn read_forced(&mut self) -> Result<MeasurementData, Error<I2C::Error>> {
        // ... implementation
        todo!()
    }
}
```

---

## Register Maps and Bit Field Documentation

Register maps are often the most referenced section of driver documentation. Always document them exhaustively.

### C/C++ Register Map with Bit Field Comments

```c
/*
 * BME280 CTRL_MEAS Register (0xF4) - R/W
 *
 *  Bit(s)  Name          Description
 *  ------  ----          -----------
 *  [7:5]   osrs_t        Temperature oversampling
 *                          000 = Skipped (output 0x80000)
 *                          001 = x1
 *                          010 = x2
 *                          011 = x4
 *                          100 = x8
 *                          101-111 = x16
 *  [4:2]   osrs_p        Pressure oversampling (same encoding as osrs_t)
 *  [1:0]   mode          Device mode
 *                          00 = Sleep mode
 *                          01 or 10 = Forced mode (device returns to sleep after measurement)
 *                          11 = Normal mode (continuous)
 *
 *  Reset value: 0x00 (all channels skipped, sleep mode)
 *
 *  IMPORTANT: This register must be written AFTER CTRL_HUM (0xF2).
 *             Changes to CTRL_HUM only take effect after a write to CTRL_MEAS.
 */
#define BME280_CTRL_MEAS_OSRS_T_SHIFT   5U
#define BME280_CTRL_MEAS_OSRS_P_SHIFT   2U
#define BME280_CTRL_MEAS_MODE_SHIFT     0U
#define BME280_CTRL_MEAS_OSRS_T_MASK    (0x07U << BME280_CTRL_MEAS_OSRS_T_SHIFT)
#define BME280_CTRL_MEAS_OSRS_P_MASK    (0x07U << BME280_CTRL_MEAS_OSRS_P_SHIFT)
#define BME280_CTRL_MEAS_MODE_MASK      (0x03U << BME280_CTRL_MEAS_MODE_SHIFT)

/** Encode a CTRL_MEAS register value from individual field values */
#define BME280_ENCODE_CTRL_MEAS(osrs_t, osrs_p, mode) \
    (((osrs_t) << BME280_CTRL_MEAS_OSRS_T_SHIFT) | \
     ((osrs_p) << BME280_CTRL_MEAS_OSRS_P_SHIFT) | \
     ((mode)   << BME280_CTRL_MEAS_MODE_SHIFT))
```

### Rust Register Map with Bit Field Documentation

```rust
/// BME280 CTRL_MEAS register layout (address `0xF4`).
///
/// This register controls temperature and pressure oversampling, and the
/// device operating mode.
///
/// # Important
///
/// This register **must be written after** [`CTRL_HUM`] (`0xF2`).
/// The BME280 only applies changes to `CTRL_HUM` when `CTRL_MEAS` is written.
///
/// # Bit Layout
///
/// ```text
/// Bit:  7   6   5   4   3   2   1   0
///       [osrs_t  ] [osrs_p  ] [mode ]
/// ```
///
/// [`CTRL_HUM`]: Self::CTRL_HUM
pub mod ctrl_meas {
    /// Temperature oversampling bits [7:5].
    ///
    /// Encoding: 0b000 = Skip, 0b001 = 1x, ..., 0b101–0b111 = 16x.
    pub const OSRS_T_SHIFT: u8 = 5;
    pub const OSRS_T_MASK:  u8 = 0b111 << OSRS_T_SHIFT;

    /// Pressure oversampling bits [4:2]. Same encoding as temperature.
    pub const OSRS_P_SHIFT: u8 = 2;
    pub const OSRS_P_MASK:  u8 = 0b111 << OSRS_P_SHIFT;

    /// Operating mode bits [1:0].
    ///
    /// - `0b00` = Sleep mode
    /// - `0b01` or `0b10` = Forced mode (single measurement, then sleep)
    /// - `0b11` = Normal mode (continuous)
    pub const MODE_SHIFT: u8 = 0;
    pub const MODE_MASK:  u8 = 0b11 << MODE_SHIFT;

    /// Encode a complete `CTRL_MEAS` byte from field values.
    #[inline]
    pub const fn encode(osrs_t: u8, osrs_p: u8, mode: u8) -> u8 {
        ((osrs_t & 0b111) << OSRS_T_SHIFT)
            | ((osrs_p & 0b111) << OSRS_P_SHIFT)
            | ((mode & 0b11) << MODE_SHIFT)
    }
}
```

---

## Error Handling Documentation

Documenting error conditions explicitly is as important as documenting success paths. Every I2C driver should have a complete, documented error type.

### C Error Codes

```c
/**
 * @brief Driver error codes.
 *
 * All driver functions return a value of this type. Check for @c BME280_OK
 * before using output parameters, as they are undefined on error.
 *
 * | Error Code              | Likely Cause                               | Recovery                        |
 * |-------------------------|--------------------------------------------|---------------------------------|
 * | BME280_OK               | No error.                                  | N/A                             |
 * | BME280_ERR_NULL_PTR     | NULL passed where pointer required.        | Fix calling code.               |
 * | BME280_ERR_NOT_INIT     | bme280_init() not called successfully.     | Call bme280_init() first.       |
 * | BME280_ERR_I2C_NACK     | Device did not ACK. Wrong address/power.   | Check wiring, address, VDD.     |
 * | BME280_ERR_I2C_BUS      | Arbitration loss or bus stuck.             | Reset I2C peripheral.           |
 * | BME280_ERR_CHIP_ID      | Unexpected chip ID read.                   | Verify correct device on bus.   |
 * | BME280_ERR_CALIB_READ   | Calibration NVM read failed.               | Power-cycle device.             |
 * | BME280_ERR_TIMEOUT      | Measurement did not complete in 500 ms.    | Power-cycle device.             |
 */
typedef enum {
    BME280_OK              =  0, /**< Success. No error. */
    BME280_ERR_NULL_PTR    = -1, /**< A required pointer argument was NULL. */
    BME280_ERR_NOT_INIT    = -2, /**< Driver handle not initialized. Call bme280_init() first. */
    BME280_ERR_I2C_NACK    = -3, /**< I2C NACK received. Device did not acknowledge its address. */
    BME280_ERR_I2C_BUS     = -4, /**< I2C bus error (arbitration loss, bus stuck low, etc.). */
    BME280_ERR_CHIP_ID     = -5, /**< Chip ID register returned unexpected value (expected 0x60). */
    BME280_ERR_CALIB_READ  = -6, /**< Failed to read calibration data from device NVM. */
    BME280_ERR_TIMEOUT     = -7, /**< Measurement did not complete within the 500 ms timeout. */
} bme280_err_t;
```

### Rust Error Type

```rust
/// Errors that can occur during BME280 driver operations.
///
/// All public driver methods return `Result<T, Error<E>>` where `E` is the
/// error type from the underlying [`embedded_hal::i2c::I2c`] implementation.
///
/// # Troubleshooting
///
/// | Variant           | Likely Cause                              | Recovery                     |
/// |-------------------|-------------------------------------------|------------------------------|
/// | `I2c(_)`          | NACK, bus error, or timeout at HAL level  | Check wiring; reset bus      |
/// | `InvalidChipId`   | Wrong device or severe communication fault| Verify correct device        |
/// | `Timeout`         | Measurement stuck; hardware fault         | Power-cycle the sensor       |
#[derive(Debug)]
pub enum Error<E> {
    /// An error returned by the underlying I2C HAL implementation.
    ///
    /// The inner value `E` is the platform-specific I2C error type, typically
    /// containing information about whether the error was a NACK, bus conflict,
    /// or timeout at the physical layer.
    I2c(E),

    /// The chip ID register (`0xD0`) returned an unexpected value.
    ///
    /// The BME280 should always return `0x60`. Any other value indicates either
    /// a communication error or a different device is at the configured address.
    ///
    /// Contains the actual value read from the register.
    InvalidChipId(u8),

    /// A forced-mode measurement did not complete within the 500 ms timeout.
    ///
    /// This indicates a hardware fault. The device may need to be power-cycled.
    Timeout,
}

impl<E: core::fmt::Display> core::fmt::Display for Error<E> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Error::I2c(e)             => write!(f, "I2C error: {}", e),
            Error::InvalidChipId(id)  => write!(f, "Invalid chip ID: expected 0x60, got {:#04x}", id),
            Error::Timeout            => write!(f, "Measurement timeout: device did not complete within 500 ms"),
        }
    }
}
```

---

## Example: Full Driver Documentation in C/C++

The following illustrates a complete, production-ready documented C driver fragment for an I2C EEPROM:

```c
/**
 * @file    at24cx_i2c.h
 * @brief   I2C driver for Microchip AT24Cx series EEPROMs.
 *
 * Supports AT24C02 (256 B) through AT24C512 (64 KB) devices.
 * All page-boundary handling is managed internally.
 *
 * @note    Write operations are limited by the device's page write buffer.
 *          This driver handles page splitting transparently.
 * @note    After any write, the device requires up to 5 ms to complete
 *          the internal programming cycle. During this time, it will NACK
 *          all I2C transactions. This driver polls for completion using
 *          acknowledge polling.
 */

/**
 * @brief  Write arbitrary-length data to EEPROM, handling page boundaries.
 *
 * @details Splits writes that cross page boundaries into multiple I2C
 *          transactions automatically. Uses acknowledge polling to wait for
 *          each page write to complete (max 5 ms per page per AT24C datasheet).
 *
 *          Maximum write latency (worst case, full 64 KB write to AT24C512):
 *            64KB / 128B page = 512 pages × 5 ms = ~2.56 seconds total.
 *
 * @param[in] dev     Pointer to initialized driver handle.
 * @param[in] addr    Starting EEPROM byte address. Must be < device capacity.
 * @param[in] buf     Pointer to source data buffer. Must not be NULL.
 * @param[in] len     Number of bytes to write. Must be > 0.
 *                    Writing beyond device capacity is an error.
 *
 * @return AT24CX_OK                  All bytes written successfully.
 * @return AT24CX_ERR_ADDR_OVERFLOW   addr + len exceeds device capacity.
 * @return AT24CX_ERR_I2C             I2C transaction failed.
 * @return AT24CX_ERR_WRITE_TIMEOUT   Device did not complete programming within
 *                                    the 10 ms extended timeout.
 *
 * @warning Do not power-cycle the device during a write operation.
 *          This will corrupt the page being written.
 *
 * @example Write a 10-byte string starting at address 0:
 * @code
 * const uint8_t msg[] = "HelloWorld";
 * at24cx_err_t err = at24cx_write(&eeprom, 0, msg, sizeof(msg));
 * if (err != AT24CX_OK) {
 *     handle_error(err);
 * }
 * @endcode
 */
at24cx_err_t at24cx_write(at24cx_dev_t *dev,
                           uint16_t      addr,
                           const uint8_t *buf,
                           uint16_t      len);
```

---

## Example: Full Driver Documentation in Rust

```rust
//! # AT24Cx EEPROM I2C Driver
//!
//! Supports AT24C02 (256 B) through AT24C512 (64 KB) devices.
//!
//! ## Write Performance
//!
//! Writes are limited by the device's internal programming cycle of up to 5 ms
//! per page. This driver uses acknowledge polling to wait for completion.
//! In the worst case (full 64 KB write to AT24C512), total write time is ~2.56 s.

impl<I2C> At24cx<I2C>
where
    I2C: embedded_hal::i2c::I2c,
{
    /// Writes data to the EEPROM, transparently handling page boundaries.
    ///
    /// The AT24Cx page write buffer limits a single I2C write transaction to
    /// one page (64–128 bytes depending on variant). This method splits larger
    /// writes into multiple page-aligned transactions and polls for completion
    /// of each before proceeding.
    ///
    /// # Arguments
    ///
    /// * `addr` – Starting EEPROM byte address. Panics in debug builds if
    ///   `addr + data.len()` overflows the device capacity.
    /// * `data` – Bytes to write. May be any length; page splits are handled internally.
    ///
    /// # Errors
    ///
    /// * [`Error::I2c`] if any I2C transaction fails.
    /// * [`Error::AddressOverflow`] if `addr + data.len()` exceeds the device capacity.
    /// * [`Error::WriteTimeout`] if acknowledge polling exceeds 10 ms for any page.
    ///
    /// # Timing
    ///
    /// Blocking. Time proportional to number of pages written × (I2C time + ≤5 ms programming).
    ///
    /// # Warning
    ///
    /// Do **not** power-cycle the device during a write. The page currently
    /// being programmed will be corrupted.
    ///
    /// # Example
    ///
    /// ```rust,no_run
    /// # use at24cx_i2c::{At24cx, Config, Variant};
    /// # fn example<I2C: embedded_hal::i2c::I2c>(i2c: I2C) {
    /// let mut eeprom = At24cx::new(i2c, Config::new(Variant::At24c256)).unwrap();
    ///
    /// let payload = b"Hello, EEPROM!";
    /// eeprom.write(0x0000, payload).expect("Write failed");
    ///
    /// // Verify the write
    /// let mut buf = [0u8; 14];
    /// eeprom.read(0x0000, &mut buf).expect("Read failed");
    /// assert_eq!(&buf, payload);
    /// # }
    /// ```
    pub fn write(&mut self, addr: u16, data: &[u8]) -> Result<(), Error<I2C::Error>> {
        // ... implementation
        todo!()
    }
}
```

---

## Changelog and Versioning

Always include a `CHANGELOG.md` in your driver repository. For I2C drivers, document behavior changes carefully — even minor changes to timing, error handling, or I2C transaction structure can break existing integrations.

```markdown
# Changelog

All notable changes to the BME280 I2C driver are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

## [2.1.0] – 2025-03-10

### Added
- `bme280_set_normal_mode()`: configure standby time and IIR filter coefficient.
- `BME280_ERR_TIMEOUT` error code for stuck measurement detection.

### Changed
- `bme280_read_forced()` now uses acknowledge polling instead of a fixed delay.
  **Behavior change**: blocking time is now measurement-duration-accurate rather
  than worst-case padded. Update RTOS task stack sizing if previously relying
  on the fixed delay.

### Fixed
- Humidity calibration coefficient `dig_H4` was incorrectly parsed for devices
  with a sign bit in the upper nibble. Affected humidity readings on some device
  batches. Upgrade strongly recommended.

## [2.0.0] – 2024-11-01

### Breaking Changes
- `bme280_read()` renamed to `bme280_read_forced()` to clarify operating mode.
- `bme280_config_t.address` renamed to `bme280_config_t.i2c_addr` for consistency.
```

---

## Summary

Documenting an I2C device driver is a multi-layered task spanning hardware, protocol, API, and operational concerns. The key principles are:

**Hardware first.** Document the physical constraints — address, speed, pull-ups, power sequencing, and clock stretching — before any code. These facts prevent the most frustrating integration failures.

**Contracts over implementation.** API documentation should tell users what a function guarantees, not how it works internally. Focus on parameters, return values, preconditions, postconditions, thread safety, and timing.

**Document every error.** Every error code or error variant should explain its likely cause and the recommended recovery action. Silent or undocumented errors waste enormous debugging time.

**Register maps are essential.** Each hardware register must document its purpose, bit fields, reset value, and any hardware-specific sequencing quirks (such as the BME280's requirement to write `CTRL_HUM` before `CTRL_MEAS`).

**Tested examples.** In Rust, `rustdoc` compiles and runs code examples in `///` comments automatically. In C/C++, include examples in `@example` blocks and verify them separately. Broken examples in documentation are worse than no examples at all.

**Maintain a changelog.** I2C driver consumers often cannot easily test upgrades. A clear, behavior-focused changelog enables informed upgrade decisions and rapid diagnosis of regressions.

Both C/C++ (using Doxygen conventions) and Rust (using `rustdoc`) provide powerful, first-class documentation tooling. The investment in thorough documentation pays dividends every time a new engineer integrates the driver, a bug is traced to a hardware quirk, or a regression is caught by a documented contract.

---

*This document is part of the I2C Programming Guide series. See also:*
*[98. Testing and Validation](98_Testing_and_Validation.md) | [Index](README.md)*