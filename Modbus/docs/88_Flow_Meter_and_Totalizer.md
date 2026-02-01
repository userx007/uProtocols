# Flow Meter and Totalizer - Modbus Communication

## Overview

Flow meters measure the rate of fluid flow through a pipe or channel, while totalizers accumulate the total volume of fluid that has passed through over time. In industrial applications, these devices commonly use Modbus protocol to communicate flow rate, accumulated volume, calibration factors, and alarm conditions to SCADA systems and PLCs.

## Common Modbus Register Layout

Flow meters typically expose the following data via Modbus:

### Input Registers (Read-Only, Function Code 0x04)
- **Flow Rate (Instantaneous)**: Current flow measurement (e.g., liters/min, gallons/min)
- **Totalized Volume**: Accumulated volume since last reset
- **Temperature**: Fluid temperature (if sensor equipped)
- **Pressure**: Fluid pressure (if sensor equipped)
- **Flow Velocity**: Calculated velocity based on pipe diameter

### Holding Registers (Read/Write, Function Code 0x03/0x06/0x10)
- **Calibration Factor**: K-factor or scaling multiplier
- **Unit Selection**: Flow rate units (0=L/min, 1=GPM, etc.)
- **Totalizer Reset**: Write to reset accumulated volume
- **Low Flow Cutoff**: Minimum flow threshold
- **Pipe Diameter**: For velocity calculation
- **Alarm Setpoints**: High/low flow alarm thresholds

### Coils/Discrete Inputs
- **Alarm Status Bits**: Low flow, high flow, sensor fault
- **Totalizer Reset Command**: Discrete output to reset counter

## Data Encoding

Flow meters often use **IEEE 754 floating-point** format (2 registers per value) or **scaled integers** for precision:

- **Float32**: Two consecutive 16-bit registers (big-endian or little-endian)
- **Scaled Integer**: Integer value with implied decimal point (e.g., value 12345 = 123.45 L/min with scale factor 100)

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <modbus/modbus.h>
#include <string.h>
#include <unistd.h>

// Flow meter register definitions
#define FLOW_RATE_REG           0x0000  // Input register (2 registers, float)
#define TOTALIZER_REG           0x0002  // Input register (2 registers, float)
#define TEMPERATURE_REG         0x0004  // Input register (1 register, scaled int)
#define CALIBRATION_FACTOR_REG  0x1000  // Holding register (2 registers, float)
#define UNIT_SELECTION_REG      0x1002  // Holding register (1 register)
#define TOTALIZER_RESET_REG     0x1003  // Holding register (write 1 to reset)
#define ALARM_SETPOINT_HIGH_REG 0x1004  // Holding register (2 registers, float)

// Convert two 16-bit registers to float (big-endian/ABCD format)
float registers_to_float_be(uint16_t reg_high, uint16_t reg_low) {
    uint32_t combined = ((uint32_t)reg_high << 16) | reg_low;
    float result;
    memcpy(&result, &combined, sizeof(float));
    return result;
}

// Convert float to two 16-bit registers (big-endian/ABCD format)
void float_to_registers_be(float value, uint16_t *reg_high, uint16_t *reg_low) {
    uint32_t combined;
    memcpy(&combined, &value, sizeof(float));
    *reg_high = (combined >> 16) & 0xFFFF;
    *reg_low = combined & 0xFFFF;
}

// Read flow rate from flow meter
int read_flow_rate(modbus_t *ctx, int slave_id, float *flow_rate) {
    uint16_t regs[2];
    
    modbus_set_slave(ctx, slave_id);
    
    // Read 2 input registers starting at FLOW_RATE_REG
    if (modbus_read_input_registers(ctx, FLOW_RATE_REG, 2, regs) == -1) {
        fprintf(stderr, "Failed to read flow rate: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    *flow_rate = registers_to_float_be(regs[0], regs[1]);
    return 0;
}

// Read totalizer (accumulated volume)
int read_totalizer(modbus_t *ctx, int slave_id, float *total_volume) {
    uint16_t regs[2];
    
    modbus_set_slave(ctx, slave_id);
    
    if (modbus_read_input_registers(ctx, TOTALIZER_REG, 2, regs) == -1) {
        fprintf(stderr, "Failed to read totalizer: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    *total_volume = registers_to_float_be(regs[0], regs[1]);
    return 0;
}

// Read temperature (scaled integer, e.g., value 2531 = 25.31°C)
int read_temperature(modbus_t *ctx, int slave_id, float *temperature) {
    uint16_t reg;
    
    modbus_set_slave(ctx, slave_id);
    
    if (modbus_read_input_registers(ctx, TEMPERATURE_REG, 1, &reg) == -1) {
        fprintf(stderr, "Failed to read temperature: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    *temperature = reg / 100.0f;  // Scale factor of 100
    return 0;
}

// Write calibration factor
int write_calibration_factor(modbus_t *ctx, int slave_id, float k_factor) {
    uint16_t regs[2];
    
    modbus_set_slave(ctx, slave_id);
    float_to_registers_be(k_factor, &regs[0], &regs[1]);
    
    if (modbus_write_registers(ctx, CALIBRATION_FACTOR_REG, 2, regs) == -1) {
        fprintf(stderr, "Failed to write calibration factor: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    printf("Calibration factor set to: %.4f\n", k_factor);
    return 0;
}

// Reset totalizer
int reset_totalizer(modbus_t *ctx, int slave_id) {
    uint16_t reset_value = 1;
    
    modbus_set_slave(ctx, slave_id);
    
    if (modbus_write_register(ctx, TOTALIZER_RESET_REG, reset_value) == -1) {
        fprintf(stderr, "Failed to reset totalizer: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    printf("Totalizer reset successfully\n");
    return 0;
}

// Set high flow alarm setpoint
int set_alarm_setpoint_high(modbus_t *ctx, int slave_id, float setpoint) {
    uint16_t regs[2];
    
    modbus_set_slave(ctx, slave_id);
    float_to_registers_be(setpoint, &regs[0], &regs[1]);
    
    if (modbus_write_registers(ctx, ALARM_SETPOINT_HIGH_REG, 2, regs) == -1) {
        fprintf(stderr, "Failed to set alarm setpoint: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    printf("High flow alarm setpoint set to: %.2f\n", setpoint);
    return 0;
}

int main() {
    modbus_t *ctx;
    float flow_rate, total_volume, temperature;
    
    // Create Modbus RTU context (RS485)
    ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create Modbus context\n");
        return -1;
    }
    
    // Set response timeout
    modbus_set_response_timeout(ctx, 1, 0);
    
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    
    int slave_id = 1;
    
    // Configure flow meter
    write_calibration_factor(ctx, slave_id, 0.8752);
    set_alarm_setpoint_high(ctx, slave_id, 500.0);
    
    // Continuous monitoring loop
    for (int i = 0; i < 10; i++) {
        if (read_flow_rate(ctx, slave_id, &flow_rate) == 0) {
            printf("Flow Rate: %.2f L/min\n", flow_rate);
        }
        
        if (read_totalizer(ctx, slave_id, &total_volume) == 0) {
            printf("Total Volume: %.2f L\n", total_volume);
        }
        
        if (read_temperature(ctx, slave_id, &temperature) == 0) {
            printf("Temperature: %.2f °C\n", temperature);
        }
        
        printf("---\n");
        sleep(2);
    }
    
    // Reset totalizer after monitoring
    reset_totalizer(ctx, slave_id);
    
    modbus_close(ctx);
    modbus_free(ctx);
    
    return 0;
}
```

## Rust Implementation

```rust
use tokio_modbus::prelude::*;
use tokio_modbus::client::sync::Context;
use std::io;
use std::time::Duration;

// Flow meter register definitions
const FLOW_RATE_REG: u16 = 0x0000;
const TOTALIZER_REG: u16 = 0x0002;
const TEMPERATURE_REG: u16 = 0x0004;
const CALIBRATION_FACTOR_REG: u16 = 0x1000;
const UNIT_SELECTION_REG: u16 = 0x1002;
const TOTALIZER_RESET_REG: u16 = 0x1003;
const ALARM_SETPOINT_HIGH_REG: u16 = 0x1004;

#[derive(Debug)]
struct FlowMeterData {
    flow_rate: f32,
    total_volume: f32,
    temperature: f32,
}

// Convert two 16-bit registers to f32 (big-endian/ABCD format)
fn registers_to_f32_be(reg_high: u16, reg_low: u16) -> f32 {
    let combined: u32 = ((reg_high as u32) << 16) | (reg_low as u32);
    f32::from_bits(combined)
}

// Convert f32 to two 16-bit registers (big-endian/ABCD format)
fn f32_to_registers_be(value: f32) -> (u16, u16) {
    let bits = value.to_bits();
    let reg_high = ((bits >> 16) & 0xFFFF) as u16;
    let reg_low = (bits & 0xFFFF) as u16;
    (reg_high, reg_low)
}

// Read flow rate from flow meter
fn read_flow_rate(ctx: &mut Context, slave_id: u8) -> io::Result<f32> {
    ctx.set_slave(Slave(slave_id));
    
    let regs = ctx.read_input_registers(FLOW_RATE_REG, 2)?;
    
    if regs.len() != 2 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Expected 2 registers for flow rate",
        ));
    }
    
    Ok(registers_to_f32_be(regs[0], regs[1]))
}

// Read totalizer (accumulated volume)
fn read_totalizer(ctx: &mut Context, slave_id: u8) -> io::Result<f32> {
    ctx.set_slave(Slave(slave_id));
    
    let regs = ctx.read_input_registers(TOTALIZER_REG, 2)?;
    
    if regs.len() != 2 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "Expected 2 registers for totalizer",
        ));
    }
    
    Ok(registers_to_f32_be(regs[0], regs[1]))
}

// Read temperature (scaled integer)
fn read_temperature(ctx: &mut Context, slave_id: u8) -> io::Result<f32> {
    ctx.set_slave(Slave(slave_id));
    
    let regs = ctx.read_input_registers(TEMPERATURE_REG, 1)?;
    
    if regs.is_empty() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "No temperature data received",
        ));
    }
    
    // Scale factor of 100
    Ok(regs[0] as f32 / 100.0)
}

// Write calibration factor
fn write_calibration_factor(ctx: &mut Context, slave_id: u8, k_factor: f32) -> io::Result<()> {
    ctx.set_slave(Slave(slave_id));
    
    let (reg_high, reg_low) = f32_to_registers_be(k_factor);
    let regs = vec![reg_high, reg_low];
    
    ctx.write_multiple_registers(CALIBRATION_FACTOR_REG, &regs)?;
    
    println!("Calibration factor set to: {:.4}", k_factor);
    Ok(())
}

// Reset totalizer
fn reset_totalizer(ctx: &mut Context, slave_id: u8) -> io::Result<()> {
    ctx.set_slave(Slave(slave_id));
    
    ctx.write_single_register(TOTALIZER_RESET_REG, 1)?;
    
    println!("Totalizer reset successfully");
    Ok(())
}

// Set high flow alarm setpoint
fn set_alarm_setpoint_high(ctx: &mut Context, slave_id: u8, setpoint: f32) -> io::Result<()> {
    ctx.set_slave(Slave(slave_id));
    
    let (reg_high, reg_low) = f32_to_registers_be(setpoint);
    let regs = vec![reg_high, reg_low];
    
    ctx.write_multiple_registers(ALARM_SETPOINT_HIGH_REG, &regs)?;
    
    println!("High flow alarm setpoint set to: {:.2}", setpoint);
    Ok(())
}

// Read all flow meter data
fn read_flow_meter_data(ctx: &mut Context, slave_id: u8) -> io::Result<FlowMeterData> {
    Ok(FlowMeterData {
        flow_rate: read_flow_rate(ctx, slave_id)?,
        total_volume: read_totalizer(ctx, slave_id)?,
        temperature: read_temperature(ctx, slave_id)?,
    })
}

fn main() -> io::Result<()> {
    // Create Modbus RTU context
    let mut ctx = tokio_modbus::client::sync::rtu::connect_slave(
        "/dev/ttyUSB0",
        tokio_serial::SerialPortSettings {
            baud_rate: 9600,
            data_bits: tokio_serial::DataBits::Eight,
            flow_control: tokio_serial::FlowControl::None,
            parity: tokio_serial::Parity::None,
            stop_bits: tokio_serial::StopBits::One,
            timeout: Duration::from_secs(1),
        },
        Slave(1),
    )?;
    
    let slave_id = 1;
    
    // Configure flow meter
    write_calibration_factor(&mut ctx, slave_id, 0.8752)?;
    set_alarm_setpoint_high(&mut ctx, slave_id, 500.0)?;
    
    // Continuous monitoring loop
    for i in 0..10 {
        match read_flow_meter_data(&mut ctx, slave_id) {
            Ok(data) => {
                println!("Reading #{}", i + 1);
                println!("Flow Rate: {:.2} L/min", data.flow_rate);
                println!("Total Volume: {:.2} L", data.total_volume);
                println!("Temperature: {:.2} °C", data.temperature);
                println!("---");
            }
            Err(e) => {
                eprintln!("Error reading flow meter: {}", e);
            }
        }
        
        std::thread::sleep(Duration::from_secs(2));
    }
    
    // Reset totalizer after monitoring
    reset_totalizer(&mut ctx, slave_id)?;
    
    Ok(())
}
```

### Async Rust Version (Tokio)

```rust
use tokio_modbus::prelude::*;
use tokio_serial::SerialStream;
use std::time::Duration;

async fn async_monitor_flow_meter() -> Result<(), Box<dyn std::error::Error>> {
    let tty_path = "/dev/ttyUSB0";
    let slave = Slave(1);
    
    // Create serial port
    let builder = tokio_serial::new(tty_path, 9600);
    let port = SerialStream::open(&builder)?;
    
    // Create Modbus RTU client
    let mut ctx = rtu::attach_slave(port, slave);
    
    // Configure flow meter
    let (reg_high, reg_low) = f32_to_registers_be(0.8752);
    ctx.write_multiple_registers(CALIBRATION_FACTOR_REG, &[reg_high, reg_low]).await?;
    
    // Monitor loop
    for _ in 0..10 {
        let flow_regs = ctx.read_input_registers(FLOW_RATE_REG, 2).await?;
        let flow_rate = registers_to_f32_be(flow_regs[0], flow_regs[1]);
        
        let total_regs = ctx.read_input_registers(TOTALIZER_REG, 2).await?;
        let total_volume = registers_to_f32_be(total_regs[0], total_regs[1]);
        
        println!("Flow: {:.2} L/min, Total: {:.2} L", flow_rate, total_volume);
        
        tokio::time::sleep(Duration::from_secs(2)).await;
    }
    
    Ok(())
}
```

## Summary

Flow meters and totalizers communicate critical process data via Modbus, enabling real-time monitoring and control of fluid systems. Key points include:

- **Flow Rate Measurement**: Instantaneous flow typically stored as IEEE 754 float across two consecutive registers
- **Volume Totalization**: Accumulated volume tracking with reset capability for batch processes
- **Calibration**: K-factors and scaling parameters configurable via holding registers
- **Data Encoding**: Float32 (two registers) or scaled integers common for precision measurements
- **Alarm Management**: Configurable high/low flow setpoints for process safety
- **Multi-parameter Monitoring**: Temperature, pressure, and velocity often available alongside flow data

Both C/C++ (using libmodbus) and Rust (using tokio-modbus) provide robust interfaces for flow meter communication, with Rust offering memory safety advantages and modern async/await patterns for industrial IoT applications.