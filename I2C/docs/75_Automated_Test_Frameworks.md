# 75. Automated Test Frameworks for I2C Drivers

**Structure & Depth:**
- Full layered test architecture diagram (unit → integration → CI)
- Explanation of all five test categories with a comparison table

**C/C++ Examples:**
- HAL abstraction via function pointers (C) and virtual classes (C++)
- **Unity + CMock** unit tests for an EEPROM driver
- **CppUTest** integration tests with a software-emulated TMP102 sensor
- **Google Test + GMock** with parameterized multi-address tests

**Rust Examples:**
- HDC1080 sensor driver written against `embedded-hal` traits
- **embedded-hal-mock** unit tests with transaction verification
- **defmt-test** for on-target hardware testing via probe-rs
- **proptest** property-based fuzzing for encoding/decoding routines

**CI Pipeline:**
- Complete GitHub Actions workflow covering C++ tests, Rust tests, static analysis (cppcheck + clang-tidy), and a gating "all tests passed" job

**Additional Topics:**
- Coverage targets by type (line, branch, function, MC/DC)
- Test double taxonomy (stub/mock/fake/spy) with a RAM-backed EEPROM fake
- Fault injection framework in both C and Rust, including retry-logic validation

## Building Comprehensive Test Suites for Continuous Integration of I2C Drivers

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Automated Testing Matters for I2C](#why-automated-testing-matters-for-i2c)
3. [Test Framework Architecture](#test-framework-architecture)
4. [Categories of I2C Tests](#categories-of-i2c-tests)
5. [Hardware Abstraction and Mocking](#hardware-abstraction-and-mocking)
6. [C/C++ Implementation](#cc-implementation)
   - [Unit Tests with Unity/CMock](#unit-tests-with-unitycmock)
   - [Integration Tests with CppUTest](#integration-tests-with-cpputest)
   - [Google Test (gtest) for I2C Drivers](#google-test-gtest-for-i2c-drivers)
7. [Rust Implementation](#rust-implementation)
   - [Unit Tests with embedded-hal-mock](#unit-tests-with-embedded-hal-mock)
   - [Integration Tests with defmt-test](#integration-tests-with-defmt-test)
   - [Property-Based Testing with proptest](#property-based-testing-with-proptest)
8. [Continuous Integration Pipeline](#continuous-integration-pipeline)
9. [Code Coverage and Metrics](#code-coverage-and-metrics)
10. [Test Doubles: Fakes, Stubs, and Mocks](#test-doubles-fakes-stubs-and-mocks)
11. [Fault Injection Testing](#fault-injection-testing)
12. [Summary](#summary)

---

## Introduction

I2C (Inter-Integrated Circuit) drivers are foundational components in embedded systems, managing communication between a microcontroller and peripheral devices such as sensors, EEPROMs, RTCs, and display controllers. Despite their seemingly straightforward protocol, I2C drivers are susceptible to a wide range of subtle bugs: incorrect addressing, missing ACK/NACK handling, clock stretching issues, bus contention, and improper state machine transitions.

**Automated Test Frameworks** provide a systematic, repeatable way to validate these drivers before integration into larger systems. When combined with Continuous Integration (CI) pipelines, they ensure that every code change is vetted against a comprehensive suite of tests — on both host machines (using mocks/emulation) and real hardware (using hardware-in-the-loop testing).

This document covers the architecture of such frameworks, concrete implementations in C/C++ and Rust, and the CI pipeline strategies that tie them all together.

---

## Why Automated Testing Matters for I2C

I2C testing without automation is:

- **Slow**: Manual oscilloscope probing and logic analyzer captures are time-consuming.
- **Inconsistent**: Human testers miss edge cases and cannot reproduce timing-sensitive bugs reliably.
- **Non-scalable**: As device count grows, regression testing becomes infeasible manually.

Automated testing provides:

- **Repeatability**: The same test runs identically every time.
- **Speed**: Hundreds of test cases execute in seconds on a host machine via mocking.
- **Regression safety**: Any new commit that breaks existing behavior is caught immediately.
- **Documentation**: Tests serve as living documentation of expected driver behavior.
- **Coverage metrics**: Tools can quantify what percentage of driver code is exercised.

---

## Test Framework Architecture

A well-structured I2C test framework is organized into layers:

```
┌─────────────────────────────────────────────────┐
│              CI/CD Pipeline (GitHub Actions, etc)│
├─────────────────────────────────────────────────┤
│           Test Runner & Reporting Layer          │
│     (JUnit XML, lcov HTML, defmt output, etc.)  │
├──────────────────┬──────────────────────────────┤
│  Unit Tests      │  Integration Tests            │
│  (Host, mocked)  │  (QEMU / Hardware-in-Loop)   │
├──────────────────┴──────────────────────────────┤
│            Hardware Abstraction Layer (HAL)      │
│   (Mock I2C bus / real peripheral registers)    │
├─────────────────────────────────────────────────┤
│         I2C Driver Under Test (DUT)              │
└─────────────────────────────────────────────────┘
```

The critical enabler is the **Hardware Abstraction Layer (HAL)**. By programming the driver against an interface (not a concrete hardware implementation), the test framework can swap in a mock or a real bus implementation without modifying the driver code.

---

## Categories of I2C Tests

| Category | Description | Environment |
|---|---|---|
| **Unit Tests** | Test individual functions in isolation with mocked I2C bus | Host (no hardware) |
| **Integration Tests** | Test driver against a real or emulated peripheral | QEMU / HIL |
| **Regression Tests** | Re-run all tests on each commit to detect breakage | CI Pipeline |
| **Fault Injection Tests** | Deliberately inject NACK, timeout, bus errors | Host + HIL |
| **Timing Tests** | Validate clock stretching, setup/hold times | Logic analyzer + HIL |
| **Property-Based Tests** | Fuzz arbitrary addresses/data to find edge cases | Host |

---

## Hardware Abstraction and Mocking

The foundation of host-side testing is abstracting the I2C bus behind an interface. The driver calls the interface; tests inject a mock implementation.

**C — Interface via function pointers:**

```c
// i2c_hal.h — Hardware Abstraction Interface
#ifndef I2C_HAL_H
#define I2C_HAL_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    I2C_OK        = 0,
    I2C_ERR_NACK  = -1,
    I2C_ERR_TIMEOUT = -2,
    I2C_ERR_BUS   = -3,
} i2c_status_t;

// Function pointer table — the "interface"
typedef struct {
    i2c_status_t (*write)(uint8_t addr, const uint8_t *data, size_t len);
    i2c_status_t (*read)(uint8_t addr, uint8_t *buf, size_t len);
    i2c_status_t (*write_read)(uint8_t addr,
                               const uint8_t *wr, size_t wr_len,
                               uint8_t *rd, size_t rd_len);
} i2c_hal_t;

#endif // I2C_HAL_H
```

**C++ — Interface via virtual class:**

```cpp
// i2c_interface.hpp
#pragma once
#include <cstdint>
#include <cstddef>

enum class I2CStatus { OK, NACK, Timeout, BusError };

class II2CBus {
public:
    virtual ~II2CBus() = default;
    virtual I2CStatus write(uint8_t addr, const uint8_t* data, size_t len) = 0;
    virtual I2CStatus read(uint8_t addr, uint8_t* buf, size_t len) = 0;
    virtual I2CStatus writeRead(uint8_t addr,
                                const uint8_t* wr, size_t wr_len,
                                uint8_t* rd, size_t rd_len) = 0;
};
```

---

## C/C++ Implementation

### Unit Tests with Unity/CMock

[Unity](https://github.com/ThrowTheSwitch/Unity) is a lightweight C unit test framework commonly used in embedded projects. [CMock](https://github.com/ThrowTheSwitch/CMock) auto-generates mock implementations from header files.

**Directory structure:**

```
project/
├── src/
│   ├── i2c_hal.h
│   ├── eeprom_driver.h
│   └── eeprom_driver.c
├── test/
│   ├── unity/          # Unity framework source
│   ├── mocks/          # CMock-generated mocks
│   ├── test_eeprom.c
│   └── CMakeLists.txt
└── CMakeLists.txt
```

**The driver under test (`eeprom_driver.c`):**

```c
// eeprom_driver.c
#include "eeprom_driver.h"
#include "i2c_hal.h"
#include <string.h>

#define EEPROM_ADDR       0x50
#define EEPROM_PAGE_SIZE  8

// Injected HAL — pointer set at init time
static const i2c_hal_t *s_hal = NULL;

void eeprom_init(const i2c_hal_t *hal) {
    s_hal = hal;
}

i2c_status_t eeprom_read_byte(uint16_t mem_addr, uint8_t *out) {
    if (!s_hal || !out) return I2C_ERR_BUS;

    uint8_t addr_buf[2] = {
        (uint8_t)(mem_addr >> 8),
        (uint8_t)(mem_addr & 0xFF)
    };

    return s_hal->write_read(EEPROM_ADDR, addr_buf, 2, out, 1);
}

i2c_status_t eeprom_write_byte(uint16_t mem_addr, uint8_t value) {
    if (!s_hal) return I2C_ERR_BUS;

    uint8_t buf[3] = {
        (uint8_t)(mem_addr >> 8),
        (uint8_t)(mem_addr & 0xFF),
        value
    };

    return s_hal->write(EEPROM_ADDR, buf, 3);
}
```

**Unity test file (`test_eeprom.c`):**

```c
// test_eeprom.c — Unity + CMock based tests
#include "unity.h"
#include "eeprom_driver.h"
#include "mock_i2c_hal.h"   // CMock-generated from i2c_hal.h

// --- Mock tracking state ---
static uint8_t  mock_rx_byte      = 0xAB;
static uint8_t  mock_written_data[32];
static size_t   mock_write_len    = 0;

// --- Mock implementations ---
static i2c_status_t mock_write_read(uint8_t addr,
                                    const uint8_t *wr, size_t wr_len,
                                    uint8_t *rd,       size_t rd_len) {
    TEST_ASSERT_EQUAL_HEX8(0x50, addr);
    TEST_ASSERT_EQUAL(2, wr_len);
    TEST_ASSERT_EQUAL(1, rd_len);
    *rd = mock_rx_byte;
    return I2C_OK;
}

static i2c_status_t mock_write(uint8_t addr,
                               const uint8_t *data, size_t len) {
    TEST_ASSERT_EQUAL_HEX8(0x50, addr);
    mock_write_len = len;
    memcpy(mock_written_data, data, len);
    return I2C_OK;
}

static i2c_status_t mock_read(uint8_t addr, uint8_t *buf, size_t len) {
    (void)addr; (void)buf; (void)len;
    return I2C_OK;
}

static const i2c_hal_t test_hal = {
    .write      = mock_write,
    .read       = mock_read,
    .write_read = mock_write_read,
};

// --- setUp / tearDown ---
void setUp(void) {
    eeprom_init(&test_hal);
    mock_rx_byte   = 0xAB;
    mock_write_len = 0;
    memset(mock_written_data, 0, sizeof(mock_written_data));
}

void tearDown(void) {}

// --- Tests ---
void test_eeprom_read_byte_sends_correct_address(void) {
    uint8_t result = 0;
    i2c_status_t status = eeprom_read_byte(0x0123, &result);

    TEST_ASSERT_EQUAL(I2C_OK, status);
    TEST_ASSERT_EQUAL_HEX8(0xAB, result);
}

void test_eeprom_write_byte_packs_address_and_data(void) {
    i2c_status_t status = eeprom_write_byte(0x01FF, 0x5A);

    TEST_ASSERT_EQUAL(I2C_OK, status);
    TEST_ASSERT_EQUAL(3, mock_write_len);
    TEST_ASSERT_EQUAL_HEX8(0x01, mock_written_data[0]); // addr high byte
    TEST_ASSERT_EQUAL_HEX8(0xFF, mock_written_data[1]); // addr low byte
    TEST_ASSERT_EQUAL_HEX8(0x5A, mock_written_data[2]); // data
}

void test_eeprom_read_byte_null_pointer_returns_error(void) {
    i2c_status_t status = eeprom_read_byte(0x0000, NULL);
    TEST_ASSERT_EQUAL(I2C_ERR_BUS, status);
}

void test_eeprom_uninit_returns_error(void) {
    eeprom_init(NULL);  // Deliberately unset HAL
    uint8_t val;
    i2c_status_t status = eeprom_read_byte(0x0000, &val);
    TEST_ASSERT_EQUAL(I2C_ERR_BUS, status);
}

// --- Test runner ---
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_eeprom_read_byte_sends_correct_address);
    RUN_TEST(test_eeprom_write_byte_packs_address_and_data);
    RUN_TEST(test_eeprom_read_byte_null_pointer_returns_error);
    RUN_TEST(test_eeprom_uninit_returns_error);
    return UNITY_END();
}
```

---

### Integration Tests with CppUTest

[CppUTest](https://cpputest.github.io/) is widely used for C/C++ embedded integration tests. Here it is used to test a full I2C temperature sensor driver against a software emulator of the sensor.

```cpp
// test_tmp102_integration.cpp — CppUTest integration test

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "tmp102_driver.hpp"
#include "i2c_interface.hpp"

// --- Software emulator of TMP102 sensor ---
class TMP102Emulator : public II2CBus {
public:
    int16_t  raw_temp = 0x0C80;  // 25.0 °C in TMP102 format
    bool     nack_on_next = false;
    uint32_t write_count = 0;
    uint32_t read_count  = 0;

    I2CStatus write(uint8_t addr, const uint8_t* data, size_t len) override {
        if (nack_on_next) { nack_on_next = false; return I2CStatus::NACK; }
        (void)addr; (void)data; (void)len;
        write_count++;
        return I2CStatus::OK;
    }

    I2CStatus read(uint8_t addr, uint8_t* buf, size_t len) override {
        if (nack_on_next) { nack_on_next = false; return I2CStatus::NACK; }
        if (len >= 2) {
            buf[0] = (raw_temp >> 4) & 0xFF;
            buf[1] = (raw_temp << 4) & 0xF0;
        }
        read_count++;
        return I2CStatus::OK;
    }

    I2CStatus writeRead(uint8_t addr,
                        const uint8_t* wr, size_t wr_len,
                        uint8_t* rd, size_t rd_len) override {
        write(addr, wr, wr_len);
        return read(addr, rd, rd_len);
    }
};

// --- Test group ---
TEST_GROUP(TMP102DriverTests) {
    TMP102Emulator* emulator;
    TMP102Driver*   driver;

    void setup() override {
        emulator = new TMP102Emulator();
        driver   = new TMP102Driver(*emulator, 0x48);
    }

    void teardown() override {
        delete driver;
        delete emulator;
        mock().clear();
    }
};

TEST(TMP102DriverTests, ReadTemperatureReturns25Celsius) {
    float temp = 0.0f;
    auto  status = driver->readTemperatureCelsius(temp);

    CHECK_EQUAL(I2CStatus::OK, status);
    DOUBLES_EQUAL(25.0, temp, 0.1);
}

TEST(TMP102DriverTests, NackReturnsError) {
    emulator->nack_on_next = true;
    float temp = 0.0f;
    auto  status = driver->readTemperatureCelsius(temp);

    CHECK_EQUAL(I2CStatus::NACK, status);
}

TEST(TMP102DriverTests, NegativeTemperatureEncodedCorrectly) {
    // -10 °C in TMP102 12-bit two's complement: 0xFF60
    emulator->raw_temp = static_cast<int16_t>(0xFF60);
    float temp = 0.0f;
    driver->readTemperatureCelsius(temp);

    DOUBLES_EQUAL(-10.0, temp, 0.1);
}

TEST(TMP102DriverTests, ReadCountIsIncrementedOnEachRead) {
    float temp;
    driver->readTemperatureCelsius(temp);
    driver->readTemperatureCelsius(temp);
    driver->readTemperatureCelsius(temp);

    CHECK_EQUAL(3u, emulator->read_count);
}
```

---

### Google Test (gtest) for I2C Drivers

Google Test is the de-facto standard for C++ projects with extensive CI support.

```cpp
// test_i2c_driver_gtest.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "i2c_interface.hpp"
#include "sensor_driver.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArrayArgument;
using ::testing::DoAll;

// --- GMock implementation of the I2C bus interface ---
class MockI2CBus : public II2CBus {
public:
    MOCK_METHOD(I2CStatus, write,
                (uint8_t addr, const uint8_t* data, size_t len), (override));
    MOCK_METHOD(I2CStatus, read,
                (uint8_t addr, uint8_t* buf, size_t len), (override));
    MOCK_METHOD(I2CStatus, writeRead,
                (uint8_t addr,
                 const uint8_t* wr, size_t wr_len,
                 uint8_t* rd, size_t rd_len), (override));
};

// --- Test fixture ---
class SensorDriverTest : public ::testing::Test {
protected:
    MockI2CBus    mock_bus;
    SensorDriver* driver = nullptr;

    void SetUp() override {
        driver = new SensorDriver(mock_bus, 0x48);
    }

    void TearDown() override {
        delete driver;
    }
};

// --- Tests ---
TEST_F(SensorDriverTest, ReadSensorReturnsOKOnSuccess) {
    uint8_t fake_response[] = {0x19, 0x00};  // 25 °C

    EXPECT_CALL(mock_bus, writeRead(0x48, _, 1, _, 2))
        .WillOnce(DoAll(
            SetArrayArgument<3>(fake_response, fake_response + 2),
            Return(I2CStatus::OK)
        ));

    float result;
    EXPECT_EQ(I2CStatus::OK, driver->readCelsius(result));
    EXPECT_NEAR(25.0f, result, 0.1f);
}

TEST_F(SensorDriverTest, NACKPropagatesAsError) {
    EXPECT_CALL(mock_bus, writeRead(_, _, _, _, _))
        .WillOnce(Return(I2CStatus::NACK));

    float result;
    EXPECT_EQ(I2CStatus::NACK, driver->readCelsius(result));
}

TEST_F(SensorDriverTest, WriteIsCalledWithCorrectRegisterByte) {
    uint8_t fake_response[] = {0x00, 0x00};
    uint8_t expected_reg = 0x00;  // Temperature register

    EXPECT_CALL(mock_bus, writeRead(0x48,
        ::testing::Pointee(expected_reg), 1,
        _, 2))
        .WillOnce(DoAll(
            SetArrayArgument<3>(fake_response, fake_response + 2),
            Return(I2CStatus::OK)
        ));

    float result;
    driver->readCelsius(result);
}

// --- Parameterized test for multiple addresses ---
class MultiAddressTest
    : public SensorDriverTest,
      public ::testing::WithParamInterface<uint8_t> {};

TEST_P(MultiAddressTest, DriverUsesCorrectAddress) {
    uint8_t addr = GetParam();
    delete driver;
    driver = new SensorDriver(mock_bus, addr);

    uint8_t fake_response[] = {0x19, 0x00};
    EXPECT_CALL(mock_bus, writeRead(addr, _, 1, _, 2))
        .WillOnce(DoAll(
            SetArrayArgument<3>(fake_response, fake_response + 2),
            Return(I2CStatus::OK)
        ));

    float result;
    driver->readCelsius(result);
}

INSTANTIATE_TEST_SUITE_P(ValidAddresses,
    MultiAddressTest,
    ::testing::Values(0x48, 0x49, 0x4A, 0x4B));  // TMP102 valid addresses
```

---

## Rust Implementation

Rust's type system and trait model make it exceptionally well-suited for testable I2C drivers. The `embedded-hal` crate defines standard traits; `embedded-hal-mock` provides implementations for testing.

### Unit Tests with embedded-hal-mock

```toml
# Cargo.toml
[dependencies]
embedded-hal = "1.0"

[dev-dependencies]
embedded-hal-mock = { version = "0.11", features = ["eh1"] }
```

**The I2C driver (sensor.rs):**

```rust
// src/sensor.rs — HDC1080 humidity/temperature sensor driver

use embedded_hal::i2c::{I2c, SevenBitAddress};

const HDC1080_ADDR: u8          = 0x40;
const REG_TEMPERATURE: u8       = 0x00;
const REG_HUMIDITY: u8          = 0x01;
const REG_CONFIG: u8            = 0x02;

#[derive(Debug, PartialEq)]
pub enum SensorError {
    I2CError,
    InvalidData,
}

pub struct Hdc1080<I2C> {
    i2c: I2C,
    addr: u8,
}

impl<I2C: I2c<SevenBitAddress>> Hdc1080<I2C> {
    pub fn new(i2c: I2C) -> Self {
        Self { i2c, addr: HDC1080_ADDR }
    }

    pub fn init(&mut self) -> Result<(), SensorError> {
        // Write config register: 14-bit resolution, heater off
        let config: [u8; 3] = [REG_CONFIG, 0x00, 0x00];
        self.i2c.write(self.addr, &config)
            .map_err(|_| SensorError::I2CError)
    }

    pub fn read_temperature_celsius(&mut self) -> Result<f32, SensorError> {
        let mut buf = [0u8; 2];

        self.i2c
            .write_read(self.addr, &[REG_TEMPERATURE], &mut buf)
            .map_err(|_| SensorError::I2CError)?;

        let raw = ((buf[0] as u16) << 8) | (buf[1] as u16);
        // HDC1080 formula: T = (raw / 65536) * 165 - 40
        let temp = (raw as f32 / 65536.0) * 165.0 - 40.0;
        Ok(temp)
    }

    pub fn read_humidity_percent(&mut self) -> Result<f32, SensorError> {
        let mut buf = [0u8; 2];

        self.i2c
            .write_read(self.addr, &[REG_HUMIDITY], &mut buf)
            .map_err(|_| SensorError::I2CError)?;

        let raw = ((buf[0] as u16) << 8) | (buf[1] as u16);
        // HDC1080 formula: RH = (raw / 65536) * 100
        let rh = (raw as f32 / 65536.0) * 100.0;
        Ok(rh)
    }
}
```

**Test file:**

```rust
// src/sensor.rs (continued) — #[cfg(test)] module

#[cfg(test)]
mod tests {
    use super::*;
    use embedded_hal_mock::eh1::i2c::{Mock as I2cMock, Transaction};

    const ADDR: u8 = HDC1080_ADDR;

    // Helper: encode temperature to HDC1080 raw bytes
    fn celsius_to_raw(temp_c: f32) -> [u8; 2] {
        let raw = ((temp_c + 40.0) / 165.0 * 65536.0) as u16;
        [(raw >> 8) as u8, (raw & 0xFF) as u8]
    }

    // Helper: encode humidity to HDC1080 raw bytes
    fn rh_to_raw(rh: f32) -> [u8; 2] {
        let raw = (rh / 100.0 * 65536.0) as u16;
        [(raw >> 8) as u8, (raw & 0xFF) as u8]
    }

    #[test]
    fn test_init_writes_correct_config() {
        let expectations = [
            Transaction::write(ADDR, vec![REG_CONFIG, 0x00, 0x00]),
        ];
        let mut i2c = I2cMock::new(&expectations);
        let mut sensor = Hdc1080::new(i2c.clone());

        sensor.init().expect("init should succeed");
        i2c.done();  // Verifies all expected transactions occurred
    }

    #[test]
    fn test_read_temperature_25_celsius() {
        let raw = celsius_to_raw(25.0);
        let expectations = [
            Transaction::write_read(
                ADDR,
                vec![REG_TEMPERATURE],
                raw.to_vec(),
            ),
        ];
        let mut i2c = I2cMock::new(&expectations);
        let mut sensor = Hdc1080::new(i2c.clone());

        let temp = sensor.read_temperature_celsius().expect("read should succeed");
        assert!((temp - 25.0).abs() < 0.1, "Expected ~25°C, got {}", temp);
        i2c.done();
    }

    #[test]
    fn test_read_temperature_negative() {
        let raw = celsius_to_raw(-10.0);
        let expectations = [
            Transaction::write_read(
                ADDR,
                vec![REG_TEMPERATURE],
                raw.to_vec(),
            ),
        ];
        let mut i2c = I2cMock::new(&expectations);
        let mut sensor = Hdc1080::new(i2c.clone());

        let temp = sensor.read_temperature_celsius().unwrap();
        assert!((temp - (-10.0)).abs() < 0.1, "Expected ~-10°C, got {}", temp);
        i2c.done();
    }

    #[test]
    fn test_read_humidity_50_percent() {
        let raw = rh_to_raw(50.0);
        let expectations = [
            Transaction::write_read(
                ADDR,
                vec![REG_HUMIDITY],
                raw.to_vec(),
            ),
        ];
        let mut i2c = I2cMock::new(&expectations);
        let mut sensor = Hdc1080::new(i2c.clone());

        let rh = sensor.read_humidity_percent().unwrap();
        assert!((rh - 50.0).abs() < 0.2, "Expected ~50%, got {}", rh);
        i2c.done();
    }

    #[test]
    fn test_i2c_error_propagates() {
        use embedded_hal_mock::eh1::i2c::Transaction;

        let expectations = [
            Transaction::write_read(ADDR, vec![REG_TEMPERATURE], vec![])
                .with_error(embedded_hal_mock::eh1::i2c::MockError::Io(
                    std::io::ErrorKind::TimedOut.into()
                )),
        ];
        let mut i2c = I2cMock::new(&expectations);
        let mut sensor = Hdc1080::new(i2c.clone());

        let result = sensor.read_temperature_celsius();
        assert_eq!(result, Err(SensorError::I2CError));
        i2c.done();
    }
}
```

---

### Integration Tests with defmt-test

For on-target testing with real hardware, [`defmt-test`](https://github.com/knurling-rs/defmt) from the Knurling project provides a `#[defmt_test::tests]` macro that runs on embedded targets and reports results via RTT.

```rust
// tests/on_target.rs — runs on real STM32 hardware via probe-rs

#![no_std]
#![no_main]

use defmt_test as _;
use hal::{pac, prelude::*, i2c::I2c};

#[defmt_test::tests]
mod tests {
    use super::*;
    use our_crate::sensor::Hdc1080;

    struct TestState {
        sensor: Hdc1080<I2c<pac::I2C1>>,
    }

    #[init]
    fn init() -> TestState {
        let dp  = pac::Peripherals::take().unwrap();
        let rcc = dp.RCC.constrain();
        let clocks = rcc.cfgr.freeze();

        let gpiob = dp.GPIOB.split();
        let scl   = gpiob.pb6.into_alternate_open_drain();
        let sda   = gpiob.pb7.into_alternate_open_drain();

        let i2c = I2c::new(dp.I2C1, (scl, sda), 400.kHz(), &clocks);
        let mut sensor = Hdc1080::new(i2c);
        sensor.init().unwrap();

        TestState { sensor }
    }

    #[test]
    fn temperature_in_plausible_range(state: &mut TestState) {
        let temp = state.sensor.read_temperature_celsius().unwrap();
        // Temperature in a lab should be between 10°C and 40°C
        defmt::assert!(temp > 10.0 && temp < 40.0,
            "Temperature {} out of plausible range", temp);
    }

    #[test]
    fn humidity_in_plausible_range(state: &mut TestState) {
        let rh = state.sensor.read_humidity_percent().unwrap();
        defmt::assert!(rh > 5.0 && rh < 95.0,
            "Humidity {} out of plausible range", rh);
    }
}
```

---

### Property-Based Testing with proptest

Property-based testing generates thousands of random inputs to find edge cases. For I2C drivers, this is excellent for validating encoding/decoding routines.

```rust
// tests/property_tests.rs

use proptest::prelude::*;
use our_crate::codec::{encode_temp, decode_temp};

proptest! {
    // Property: encode then decode is identity (within sensor resolution)
    #[test]
    fn roundtrip_temperature(temp_c in -40.0f32..=125.0f32) {
        let encoded = encode_temp(temp_c);
        let decoded = decode_temp(encoded);
        // HDC1080 resolution is ~0.0025°C per LSB
        prop_assert!((decoded - temp_c).abs() < 0.01,
            "roundtrip failed: {} -> {:?} -> {}", temp_c, encoded, decoded);
    }

    // Property: raw values always decode to valid temperature range
    #[test]
    fn raw_always_in_range(raw in 0u16..=u16::MAX) {
        let decoded = decode_temp(raw);
        prop_assert!(decoded >= -40.0 && decoded <= 125.0,
            "raw=0x{:04X} decoded to out-of-range {}", raw, decoded);
    }

    // Property: I2C address must be 7-bit (0–127)
    #[test]
    fn address_encoding_7bit(addr in 0u8..=127u8) {
        // Constructing the driver should always succeed with valid address
        let result = std::panic::catch_unwind(|| {
            // SensorDriver::new validates address range
            our_crate::sensor::SensorDriver::with_address(addr)
        });
        prop_assert!(result.is_ok(),
            "Valid 7-bit address {} caused panic", addr);
    }
}
```

---

## Continuous Integration Pipeline

The following GitHub Actions workflow runs the full test suite on every push and pull request.

```yaml
# .github/workflows/i2c_ci.yml

name: I2C Driver CI

on: [push, pull_request]

env:
  CARGO_TERM_COLOR: always

jobs:
  # ─── C/C++ Tests ──────────────────────────────────────────────────────────
  cpp_tests:
    name: C/C++ Unit & Integration Tests
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake ninja-build lcov gcovr \
            libgtest-dev libgmock-dev cpputest

      - name: Configure CMake
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DENABLE_COVERAGE=ON \
            -DBUILD_TESTS=ON

      - name: Build
        run: cmake --build build

      - name: Run Unity unit tests
        run: ./build/test/unity_tests --verbose

      - name: Run CppUTest integration tests
        run: ./build/test/cpputest_integration

      - name: Run Google Test suite
        run: ./build/test/gtest_suite --gtest_output=xml:test-results.xml

      - name: Generate coverage report
        run: |
          lcov --capture --directory build --output-file coverage.info \
            --ignore-errors gcov
          lcov --remove coverage.info '*/test/*' '/usr/*' \
            --output-file coverage_filtered.info
          genhtml coverage_filtered.info --output-directory coverage_html

      - name: Upload coverage to Codecov
        uses: codecov/codecov-action@v4
        with:
          files: coverage_filtered.info

      - name: Upload JUnit results
        uses: actions/upload-artifact@v4
        with:
          name: cpp-test-results
          path: test-results.xml

  # ─── Rust Tests ───────────────────────────────────────────────────────────
  rust_tests:
    name: Rust Unit Tests (host)
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v4

      - name: Install Rust toolchain
        uses: dtolnay/rust-toolchain@stable
        with:
          components: clippy, rustfmt, llvm-tools-preview

      - name: Install cargo tools
        run: |
          cargo install cargo-llvm-cov
          cargo install cargo-nextest

      - name: Lint
        run: |
          cargo fmt --check
          cargo clippy -- -D warnings

      - name: Run tests with nextest
        run: cargo nextest run --all-features

      - name: Run property-based tests
        run: cargo test --test property_tests -- --test-threads=4

      - name: Generate coverage
        run: |
          cargo llvm-cov nextest \
            --all-features \
            --lcov \
            --output-path lcov.info

      - name: Upload coverage to Codecov
        uses: codecov/codecov-action@v4
        with:
          files: lcov.info
          flags: rust

  # ─── Static Analysis ──────────────────────────────────────────────────────
  static_analysis:
    name: Static Analysis
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v4

      - name: Run cppcheck on C sources
        run: |
          cppcheck --enable=all \
                   --suppress=missingIncludeSystem \
                   --error-exitcode=1 \
                   --xml \
                   src/ 2> cppcheck.xml

      - name: Run clang-tidy
        run: |
          clang-tidy src/*.c \
            --checks='clang-analyzer-*,cert-*,bugprone-*' \
            -- -Isrc/

  # ─── Results gate ─────────────────────────────────────────────────────────
  all_tests_pass:
    name: All Tests Passed
    needs: [cpp_tests, rust_tests, static_analysis]
    runs-on: ubuntu-latest
    if: always()

    steps:
      - name: Check results
        run: |
          if [[ "${{ needs.cpp_tests.result }}" != "success" || \
                "${{ needs.rust_tests.result }}" != "success" || \
                "${{ needs.static_analysis.result }}" != "success" ]]; then
            echo "One or more test jobs failed!"
            exit 1
          fi
          echo "All CI jobs passed."
```

---

## Code Coverage and Metrics

Coverage is not a goal in itself, but a diagnostic tool. For I2C drivers, aim for:

| Coverage Type | Target | Notes |
|---|---|---|
| Line coverage | ≥ 90% | Core driver logic should be nearly fully exercised |
| Branch coverage | ≥ 80% | All error paths (NACK, timeout, bus error) must be covered |
| Function coverage | 100% | Every exported function must have at least one test |
| MC/DC coverage | ≥ 70% | Required for safety-critical (IEC 61508 SIL-2+) |

**CMakeLists.txt fragment for coverage:**

```cmake
# CMakeLists.txt — coverage configuration
if(ENABLE_COVERAGE)
    target_compile_options(i2c_driver PRIVATE
        --coverage
        -fprofile-arcs
        -ftest-coverage
        -O0
        -g
    )
    target_link_options(i2c_driver PRIVATE --coverage)
endif()
```

**Rust: cargo-llvm-cov configuration (`.cargo/config.toml`):**

```toml
[alias]
cov = "llvm-cov nextest --all-features --open"
cov-html = "llvm-cov nextest --all-features --html --output-dir coverage/"
```

---

## Test Doubles: Fakes, Stubs, and Mocks

Understanding the correct tool to use for each situation:

| Type | Description | When to Use |
|---|---|---|
| **Stub** | Returns fixed values, no validation | Simple happy-path tests |
| **Mock** | Validates calls (count, args, order) | Protocol conformance tests |
| **Fake** | Working simplified implementation (e.g. RAM EEPROM) | Integration tests with multiple drivers |
| **Spy** | Records calls for later assertion | Verifying side effects |

**C — RAM-backed EEPROM fake:**

```c
// fake_eeprom_i2c.c — Fake I2C bus backed by a RAM buffer

#include "i2c_hal.h"
#include <string.h>

#define FAKE_EEPROM_SIZE 256

static uint8_t eeprom_ram[FAKE_EEPROM_SIZE] = {0};
static uint8_t last_mem_addr = 0;

static i2c_status_t fake_write(uint8_t addr,
                               const uint8_t *data, size_t len) {
    (void)addr;
    if (len == 1) {
        // Set address pointer only
        last_mem_addr = data[0];
    } else if (len >= 2) {
        // Write data starting at address
        last_mem_addr = data[0];
        for (size_t i = 1; i < len; i++) {
            eeprom_ram[(last_mem_addr + i - 1) % FAKE_EEPROM_SIZE] = data[i];
        }
    }
    return I2C_OK;
}

static i2c_status_t fake_read(uint8_t addr, uint8_t *buf, size_t len) {
    (void)addr;
    for (size_t i = 0; i < len; i++) {
        buf[i] = eeprom_ram[(last_mem_addr + i) % FAKE_EEPROM_SIZE];
    }
    return I2C_OK;
}

static i2c_status_t fake_write_read(uint8_t addr,
                                    const uint8_t *wr, size_t wr_len,
                                    uint8_t *rd,       size_t rd_len) {
    fake_write(addr, wr, wr_len);
    return fake_read(addr, rd, rd_len);
}

const i2c_hal_t fake_eeprom_hal = {
    .write      = fake_write,
    .read       = fake_read,
    .write_read = fake_write_read,
};

// Reset helper for test setUp
void fake_eeprom_reset(void) {
    memset(eeprom_ram, 0xFF, FAKE_EEPROM_SIZE);
    last_mem_addr = 0;
}
```

---

## Fault Injection Testing

I2C drivers must handle errors gracefully. Fault injection tests verify this systematically.

**C — Programmable fault injector:**

```c
// fault_injector.h — Configurable fault injection wrapper

#include "i2c_hal.h"

typedef struct {
    uint32_t     nack_on_call_n;  // 0 = never, N = inject on Nth call
    uint32_t     call_count;
    i2c_status_t injected_error;
    const i2c_hal_t *real_hal;
} fault_injector_t;

static fault_injector_t g_injector;

static i2c_status_t fi_write(uint8_t addr,
                             const uint8_t *data, size_t len) {
    g_injector.call_count++;
    if (g_injector.nack_on_call_n &&
        g_injector.call_count == g_injector.nack_on_call_n) {
        return g_injector.injected_error;
    }
    return g_injector.real_hal->write(addr, data, len);
}

static i2c_status_t fi_read(uint8_t addr, uint8_t *buf, size_t len) {
    g_injector.call_count++;
    if (g_injector.nack_on_call_n &&
        g_injector.call_count == g_injector.nack_on_call_n) {
        return g_injector.injected_error;
    }
    return g_injector.real_hal->read(addr, buf, len);
}

static const i2c_hal_t fault_hal = {
    .write      = fi_write,
    .read       = fi_read,
    .write_read = fi_write,  // simplified
};

void fault_injector_setup(const i2c_hal_t *real,
                          uint32_t fail_on,
                          i2c_status_t error) {
    g_injector.real_hal        = real;
    g_injector.nack_on_call_n  = fail_on;
    g_injector.injected_error  = error;
    g_injector.call_count      = 0;
}

// Usage in test:
void test_driver_recovers_from_nack_on_third_write(void) {
    fault_injector_setup(&fake_eeprom_hal, 3, I2C_ERR_NACK);
    eeprom_init(&fault_hal);

    i2c_status_t s = eeprom_write_byte(0x0010, 0xAA); // call 1 — OK
    TEST_ASSERT_EQUAL(I2C_OK, s);

    s = eeprom_write_byte(0x0011, 0xBB); // call 2 — OK
    TEST_ASSERT_EQUAL(I2C_OK, s);

    s = eeprom_write_byte(0x0012, 0xCC); // call 3 — NACK injected
    TEST_ASSERT_EQUAL(I2C_ERR_NACK, s);  // driver must propagate
}
```

**Rust — Error injection with mock transactions:**

```rust
#[cfg(test)]
mod fault_tests {
    use super::*;
    use embedded_hal_mock::eh1::i2c::{Mock as I2cMock, Transaction, MockError};
    use std::io;

    #[test]
    fn test_nack_on_init_returns_error() {
        let expectations = [
            Transaction::write(HDC1080_ADDR, vec![REG_CONFIG, 0x00, 0x00])
                .with_error(MockError::Io(io::Error::new(
                    io::ErrorKind::ConnectionRefused, "NACK"
                ))),
        ];
        let mut i2c = I2cMock::new(&expectations);
        let mut sensor = Hdc1080::new(i2c.clone());

        let result = sensor.init();
        assert_eq!(result, Err(SensorError::I2CError));
        i2c.done();
    }

    #[test]
    fn test_timeout_on_temperature_read() {
        let expectations = [
            Transaction::write_read(
                HDC1080_ADDR, vec![REG_TEMPERATURE], vec![]
            ).with_error(MockError::Io(io::Error::new(
                io::ErrorKind::TimedOut, "clock stretch timeout"
            ))),
        ];
        let mut i2c = I2cMock::new(&expectations);
        let mut sensor = Hdc1080::new(i2c.clone());

        let result = sensor.read_temperature_celsius();
        assert_eq!(result, Err(SensorError::I2CError));
        i2c.done();
    }

    // Verify retry logic: fail twice, succeed on third attempt
    #[test]
    fn test_driver_retries_on_transient_nack() {
        let fake_response = vec![0x65, 0x80]; // ~25°C
        let expectations = [
            Transaction::write_read(HDC1080_ADDR, vec![REG_TEMPERATURE], vec![])
                .with_error(MockError::Io(io::Error::new(
                    io::ErrorKind::ConnectionRefused, "NACK"
                ))),
            Transaction::write_read(HDC1080_ADDR, vec![REG_TEMPERATURE], vec![])
                .with_error(MockError::Io(io::Error::new(
                    io::ErrorKind::ConnectionRefused, "NACK"
                ))),
            Transaction::write_read(
                HDC1080_ADDR, vec![REG_TEMPERATURE], fake_response
            ),
        ];
        let mut i2c = I2cMock::new(&expectations);
        let mut sensor = Hdc1080WithRetry::new(i2c.clone(), 3); // max 3 retries

        let result = sensor.read_temperature_celsius();
        assert!(result.is_ok(), "Expected success after retry, got {:?}", result);
        i2c.done();
    }
}
```

---

## Summary

Automated test frameworks for I2C drivers transform an error-prone, manual verification process into a fast, reliable, and repeatable workflow. The key principles are:

**Architecture:** Structure your driver against a hardware abstraction interface (function pointers in C, virtual classes in C++, traits in Rust). This single decision unlocks all forms of automated testing.

**C/C++ toolchain:** Use Unity + CMock for lightweight embedded unit tests, CppUTest for integration tests with emulators, and Google Test + GMock for richer mock-based testing in host environments. CMake with `--coverage` feeds lcov/gcovr for coverage reports.

**Rust toolchain:** The `embedded-hal` trait ecosystem combined with `embedded-hal-mock` provides first-class support for host-side I2C driver testing without any hardware. `cargo nextest` accelerates test execution; `cargo-llvm-cov` provides precise coverage data. `defmt-test` extends the same patterns to on-target hardware via probe-rs.

**CI pipeline:** GitHub Actions (or equivalent) should run unit tests on every commit, integration tests on pull requests, static analysis continuously, and coverage gated to a minimum threshold. JUnit XML output ensures test results appear directly in PR dashboards.

**Test categories matter:** Unit tests catch logic bugs; integration tests with fakes catch protocol bugs; fault injection tests catch error-handling gaps; property-based tests catch encoding edge cases; on-target tests catch hardware-specific timing bugs. A complete framework exercises all five categories.

**Coverage as a diagnostic:** Pursue branch and MC/DC coverage for safety-critical paths. High coverage without meaningful assertions is false confidence — always assert on both the return value and the observable side effects of each I2C transaction.

By investing in this infrastructure early in a project, teams dramatically reduce the cost of finding and fixing I2C driver bugs, confidently integrate new peripheral drivers, and maintain a stable baseline as hardware revisions occur.

---

*Document: 75 — Automated Test Frameworks for I2C Drivers*
*Part of the I2C Comprehensive Programming Reference*