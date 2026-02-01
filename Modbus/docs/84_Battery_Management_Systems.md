# Battery Management Systems (BMS) via Modbus

## Overview

Battery Management Systems monitor and control rechargeable battery packs, particularly lithium-ion batteries used in electric vehicles, energy storage systems, and UPS applications. BMS units protect batteries from operating outside safe parameters, balance cell voltages, calculate state of charge (SOC), and report health metrics. Modbus connectivity allows industrial controllers, SCADA systems, and monitoring software to access critical battery data for energy management and predictive maintenance.

## Key BMS Parameters Accessible via Modbus

**State of Charge (SOC)**
- Percentage of remaining capacity (0-100%)
- Typically read as 16-bit register, scaled by 10 or 100
- Critical for energy management decisions

**Voltage Measurements**
- Total pack voltage (V)
- Individual cell voltages (mV)
- Minimum/maximum cell voltages
- Voltage differential across cells

**Current Measurements**
- Charge/discharge current (A)
- Signed values (positive = charging, negative = discharging)
- Peak current limits

**Temperature Monitoring**
- Multiple temperature sensors across battery pack
- Minimum/maximum temperatures
- Average pack temperature
- Thermal runaway detection

**State of Health (SOH)**
- Battery degradation percentage
- Remaining capacity vs. original capacity
- Cycle count
- Expected remaining lifetime

**Protection Status**
- Over-voltage protection triggered
- Under-voltage protection triggered
- Over-current protection
- Over-temperature protection
- Short circuit detection
- Fault and alarm registers

## Common Register Mappings

While BMS register maps vary by manufacturer, typical layouts include:

| Register | Parameter | Data Type | Unit | Scale |
|----------|-----------|-----------|------|-------|
| 40001 | Pack Voltage | INT16 | V | 0.1 |
| 40002 | Pack Current | INT16 | A | 0.1 |
| 40003 | SOC | UINT16 | % | 0.1 |
| 40004 | SOH | UINT16 | % | 0.1 |
| 40005-40020 | Cell Voltages | UINT16 | mV | 1 |
| 40021 | Max Cell Voltage | UINT16 | mV | 1 |
| 40022 | Min Cell Voltage | UINT16 | mV | 1 |
| 40023-40026 | Temperatures | INT16 | °C | 0.1 |
| 40027 | Charge Cycles | UINT16 | - | 1 |
| 40028 | Protection Status | UINT16 | Bitmap | - |
| 40029 | Alarm Status | UINT16 | Bitmap | - |

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdlib.h>
#include <modbus/modbus.h>
#include <errno.h>
#include <unistd.h>

#define BMS_SLAVE_ID 1
#define BMS_IP "192.168.1.100"
#define BMS_PORT 502

// Register addresses
#define REG_PACK_VOLTAGE    0x0000
#define REG_PACK_CURRENT    0x0001
#define REG_SOC             0x0002
#define REG_SOH             0x0003
#define REG_CELL_VOLTAGE_1  0x0004
#define REG_MAX_CELL_VOLT   0x0014
#define REG_MIN_CELL_VOLT   0x0015
#define REG_TEMP_1          0x0016
#define REG_TEMP_2          0x0017
#define REG_TEMP_3          0x0018
#define REG_TEMP_4          0x0019
#define REG_CYCLES          0x001A
#define REG_PROTECTION      0x001B
#define REG_ALARM           0x001C

typedef struct {
    float pack_voltage;      // Volts
    float pack_current;      // Amps
    float soc;               // Percentage
    float soh;               // Percentage
    float cell_voltages[16]; // mV
    float max_cell_voltage;  // mV
    float min_cell_voltage;  // mV
    float temperatures[4];   // Celsius
    uint16_t cycles;
    uint16_t protection_status;
    uint16_t alarm_status;
} bms_data_t;

// Convert signed 16-bit to float with scaling
float int16_to_float(uint16_t raw, float scale) {
    int16_t signed_val = (int16_t)raw;
    return signed_val * scale;
}

int read_bms_data(modbus_t *ctx, bms_data_t *data) {
    uint16_t registers[32];
    int rc;
    
    // Read main parameters (pack voltage, current, SOC, SOH)
    rc = modbus_read_holding_registers(ctx, REG_PACK_VOLTAGE, 4, registers);
    if (rc == -1) {
        fprintf(stderr, "Failed to read main parameters: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    data->pack_voltage = int16_to_float(registers[0], 0.1);
    data->pack_current = int16_to_float(registers[1], 0.1);
    data->soc = registers[2] * 0.1;
    data->soh = registers[3] * 0.1;
    
    // Read cell voltages (16 cells)
    rc = modbus_read_holding_registers(ctx, REG_CELL_VOLTAGE_1, 16, registers);
    if (rc == -1) {
        fprintf(stderr, "Failed to read cell voltages: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    for (int i = 0; i < 16; i++) {
        data->cell_voltages[i] = registers[i]; // Already in mV
    }
    
    // Read min/max cell voltages, temperatures, cycles, status
    rc = modbus_read_holding_registers(ctx, REG_MAX_CELL_VOLT, 9, registers);
    if (rc == -1) {
        fprintf(stderr, "Failed to read additional parameters: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    data->max_cell_voltage = registers[0];
    data->min_cell_voltage = registers[1];
    data->temperatures[0] = int16_to_float(registers[2], 0.1);
    data->temperatures[1] = int16_to_float(registers[3], 0.1);
    data->temperatures[2] = int16_to_float(registers[4], 0.1);
    data->temperatures[3] = int16_to_float(registers[5], 0.1);
    data->cycles = registers[6];
    data->protection_status = registers[7];
    data->alarm_status = registers[8];
    
    return 0;
}

void print_bms_data(const bms_data_t *data) {
    printf("\n=== Battery Management System Data ===\n");
    printf("Pack Voltage:        %.1f V\n", data->pack_voltage);
    printf("Pack Current:        %.1f A ", data->pack_current);
    printf("%s\n", data->pack_current > 0 ? "(Charging)" : "(Discharging)");
    printf("State of Charge:     %.1f %%\n", data->soc);
    printf("State of Health:     %.1f %%\n", data->soh);
    printf("Charge Cycles:       %u\n", data->cycles);
    
    printf("\nCell Voltages (mV):\n");
    for (int i = 0; i < 16; i++) {
        printf("  Cell %2d: %.0f mV", i + 1, data->cell_voltages[i]);
        if ((i + 1) % 4 == 0) printf("\n");
    }
    
    printf("  Max Cell: %.0f mV\n", data->max_cell_voltage);
    printf("  Min Cell: %.0f mV\n", data->min_cell_voltage);
    printf("  Differential: %.0f mV\n", 
           data->max_cell_voltage - data->min_cell_voltage);
    
    printf("\nTemperatures:\n");
    for (int i = 0; i < 4; i++) {
        printf("  Sensor %d: %.1f °C\n", i + 1, data->temperatures[i]);
    }
    
    printf("\nStatus Flags:\n");
    printf("  Protection: 0x%04X\n", data->protection_status);
    printf("  Alarms:     0x%04X\n", data->alarm_status);
    
    // Decode protection bits (example)
    if (data->protection_status & 0x0001) printf("  - Over-voltage protection\n");
    if (data->protection_status & 0x0002) printf("  - Under-voltage protection\n");
    if (data->protection_status & 0x0004) printf("  - Over-current charge\n");
    if (data->protection_status & 0x0008) printf("  - Over-current discharge\n");
    if (data->protection_status & 0x0010) printf("  - Over-temperature\n");
    if (data->protection_status & 0x0020) printf("  - Under-temperature\n");
}

int main() {
    modbus_t *ctx;
    bms_data_t bms_data;
    
    // Create Modbus TCP context
    ctx = modbus_new_tcp(BMS_IP, BMS_PORT);
    if (ctx == NULL) {
        fprintf(stderr, "Failed to create Modbus context\n");
        return -1;
    }
    
    // Set slave ID
    modbus_set_slave(ctx, BMS_SLAVE_ID);
    
    // Connect to BMS
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    
    printf("Connected to BMS at %s:%d\n", BMS_IP, BMS_PORT);
    
    // Continuous monitoring loop
    while (1) {
        if (read_bms_data(ctx, &bms_data) == 0) {
            print_bms_data(&bms_data);
            
            // Example: Check for critical conditions
            if (bms_data.soc < 20.0) {
                printf("\n*** WARNING: Low battery (%.1f%%) ***\n", bms_data.soc);
            }
            
            if (bms_data.max_cell_voltage - bms_data.min_cell_voltage > 100) {
                printf("\n*** WARNING: High cell imbalance (%.0f mV) ***\n",
                       bms_data.max_cell_voltage - bms_data.min_cell_voltage);
            }
            
            float max_temp = data->temperatures[0];
            for (int i = 1; i < 4; i++) {
                if (bms_data.temperatures[i] > max_temp) {
                    max_temp = bms_data.temperatures[i];
                }
            }
            if (max_temp > 45.0) {
                printf("\n*** WARNING: High temperature (%.1f °C) ***\n", max_temp);
            }
        }
        
        sleep(5); // Poll every 5 seconds
    }
    
    modbus_close(ctx);
    modbus_free(ctx);
    return 0;
}
```

## Rust Implementation

```rust
use tokio_modbus::prelude::*;
use std::time::Duration;
use tokio::time;

// Register addresses
const REG_PACK_VOLTAGE: u16 = 0x0000;
const REG_PACK_CURRENT: u16 = 0x0001;
const REG_SOC: u16 = 0x0002;
const REG_SOH: u16 = 0x0003;
const REG_CELL_VOLTAGE_1: u16 = 0x0004;
const REG_MAX_CELL_VOLT: u16 = 0x0014;
const REG_MIN_CELL_VOLT: u16 = 0x0015;
const REG_TEMP_1: u16 = 0x0016;
const REG_CYCLES: u16 = 0x001A;
const REG_PROTECTION: u16 = 0x001B;
const REG_ALARM: u16 = 0x001C;

#[derive(Debug, Clone)]
struct BmsData {
    pack_voltage: f32,
    pack_current: f32,
    soc: f32,
    soh: f32,
    cell_voltages: Vec<f32>,
    max_cell_voltage: f32,
    min_cell_voltage: f32,
    temperatures: Vec<f32>,
    cycles: u16,
    protection_status: u16,
    alarm_status: u16,
}

impl BmsData {
    fn cell_imbalance(&self) -> f32 {
        self.max_cell_voltage - self.min_cell_voltage
    }
    
    fn max_temperature(&self) -> f32 {
        self.temperatures.iter()
            .copied()
            .fold(f32::NEG_INFINITY, f32::max)
    }
    
    fn min_temperature(&self) -> f32 {
        self.temperatures.iter()
            .copied()
            .fold(f32::INFINITY, f32::min)
    }
    
    fn avg_temperature(&self) -> f32 {
        self.temperatures.iter().sum::<f32>() / self.temperatures.len() as f32
    }
}

// Convert u16 to i16 for signed values
fn u16_to_i16(value: u16) -> i16 {
    value as i16
}

// Read and parse BMS data
async fn read_bms_data(ctx: &mut Context) -> Result<BmsData, Box<dyn std::error::Error>> {
    // Read main parameters
    let main_params = ctx.read_holding_registers(REG_PACK_VOLTAGE, 4).await?;
    
    let pack_voltage = u16_to_i16(main_params[0]) as f32 * 0.1;
    let pack_current = u16_to_i16(main_params[1]) as f32 * 0.1;
    let soc = main_params[2] as f32 * 0.1;
    let soh = main_params[3] as f32 * 0.1;
    
    // Read 16 cell voltages
    let cell_regs = ctx.read_holding_registers(REG_CELL_VOLTAGE_1, 16).await?;
    let cell_voltages: Vec<f32> = cell_regs.iter()
        .map(|&v| v as f32)
        .collect();
    
    // Read additional parameters
    let additional = ctx.read_holding_registers(REG_MAX_CELL_VOLT, 9).await?;
    
    let max_cell_voltage = additional[0] as f32;
    let min_cell_voltage = additional[1] as f32;
    let temperatures: Vec<f32> = (2..6)
        .map(|i| u16_to_i16(additional[i]) as f32 * 0.1)
        .collect();
    let cycles = additional[6];
    let protection_status = additional[7];
    let alarm_status = additional[8];
    
    Ok(BmsData {
        pack_voltage,
        pack_current,
        soc,
        soh,
        cell_voltages,
        max_cell_voltage,
        min_cell_voltage,
        temperatures,
        cycles,
        protection_status,
        alarm_status,
    })
}

fn print_bms_data(data: &BmsData) {
    println!("\n=== Battery Management System Data ===");
    println!("Pack Voltage:        {:.1} V", data.pack_voltage);
    print!("Pack Current:        {:.1} A ", data.pack_current);
    println!("{}", if data.pack_current > 0.0 { "(Charging)" } else { "(Discharging)" });
    println!("State of Charge:     {:.1} %", data.soc);
    println!("State of Health:     {:.1} %", data.soh);
    println!("Charge Cycles:       {}", data.cycles);
    
    println!("\nCell Voltages (mV):");
    for (i, voltage) in data.cell_voltages.iter().enumerate() {
        print!("  Cell {:2}: {:.0} mV", i + 1, voltage);
        if (i + 1) % 4 == 0 {
            println!();
        }
    }
    println!("  Max Cell: {:.0} mV", data.max_cell_voltage);
    println!("  Min Cell: {:.0} mV", data.min_cell_voltage);
    println!("  Differential: {:.0} mV", data.cell_imbalance());
    
    println!("\nTemperatures:");
    for (i, temp) in data.temperatures.iter().enumerate() {
        println!("  Sensor {}: {:.1} °C", i + 1, temp);
    }
    println!("  Average: {:.1} °C", data.avg_temperature());
    
    println!("\nStatus Flags:");
    println!("  Protection: 0x{:04X}", data.protection_status);
    println!("  Alarms:     0x{:04X}", data.alarm_status);
    
    // Decode protection bits
    if data.protection_status & 0x0001 != 0 { println!("  - Over-voltage protection"); }
    if data.protection_status & 0x0002 != 0 { println!("  - Under-voltage protection"); }
    if data.protection_status & 0x0004 != 0 { println!("  - Over-current charge"); }
    if data.protection_status & 0x0008 != 0 { println!("  - Over-current discharge"); }
    if data.protection_status & 0x0010 != 0 { println!("  - Over-temperature"); }
    if data.protection_status & 0x0020 != 0 { println!("  - Under-temperature"); }
}

fn check_warnings(data: &BmsData) {
    if data.soc < 20.0 {
        println!("\n*** WARNING: Low battery ({:.1}%) ***", data.soc);
    }
    
    if data.cell_imbalance() > 100.0 {
        println!("\n*** WARNING: High cell imbalance ({:.0} mV) ***", 
                 data.cell_imbalance());
    }
    
    let max_temp = data.max_temperature();
    if max_temp > 45.0 {
        println!("\n*** WARNING: High temperature ({:.1} °C) ***", max_temp);
    }
    
    if max_temp < 0.0 {
        println!("\n*** WARNING: Below freezing ({:.1} °C) ***", max_temp);
    }
    
    if data.soh < 80.0 {
        println!("\n*** NOTICE: Battery health degraded ({:.1}%) ***", data.soh);
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let socket_addr = "192.168.1.100:502".parse()?;
    let slave = Slave(1);
    
    println!("Connecting to BMS at {}", socket_addr);
    let mut ctx = tcp::connect_slave(socket_addr, slave).await?;
    println!("Connected successfully\n");
    
    // Monitoring loop
    let mut interval = time::interval(Duration::from_secs(5));
    
    loop {
        interval.tick().await;
        
        match read_bms_data(&mut ctx).await {
            Ok(data) => {
                print_bms_data(&data);
                check_warnings(&data);
            }
            Err(e) => {
                eprintln!("Error reading BMS data: {}", e);
            }
        }
    }
}
```

### Additional Rust Example: Data Logging

```rust
use chrono::{DateTime, Utc};
use serde::{Serialize, Deserialize};
use std::fs::OpenOptions;
use std::io::Write;

#[derive(Serialize, Deserialize)]
struct BmsLogEntry {
    timestamp: DateTime<Utc>,
    pack_voltage: f32,
    pack_current: f32,
    soc: f32,
    soh: f32,
    max_cell_voltage: f32,
    min_cell_voltage: f32,
    cell_imbalance: f32,
    max_temperature: f32,
    avg_temperature: f32,
    cycles: u16,
    protection_status: u16,
    alarm_status: u16,
}

impl From<&BmsData> for BmsLogEntry {
    fn from(data: &BmsData) -> Self {
        BmsLogEntry {
            timestamp: Utc::now(),
            pack_voltage: data.pack_voltage,
            pack_current: data.pack_current,
            soc: data.soc,
            soh: data.soh,
            max_cell_voltage: data.max_cell_voltage,
            min_cell_voltage: data.min_cell_voltage,
            cell_imbalance: data.cell_imbalance(),
            max_temperature: data.max_temperature(),
            avg_temperature: data.avg_temperature(),
            cycles: data.cycles,
            protection_status: data.protection_status,
            alarm_status: data.alarm_status,
        }
    }
}

fn log_bms_data(data: &BmsData, filename: &str) -> Result<(), Box<dyn std::error::Error>> {
    let entry = BmsLogEntry::from(data);
    let json = serde_json::to_string(&entry)?;
    
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(filename)?;
    
    writeln!(file, "{}", json)?;
    Ok(())
}
```

## Summary

**Battery Management Systems via Modbus** provide critical access to energy storage data for monitoring, control, and predictive maintenance. Key capabilities include:

- **Real-time Monitoring**: SOC, SOH, voltage, current, and temperature tracking
- **Cell-Level Visibility**: Individual cell voltages for balancing and health assessment
- **Protection Status**: Over-voltage, under-voltage, over-current, and thermal protection flags
- **Predictive Maintenance**: Cycle counting, capacity fade tracking, and health degradation metrics
- **Safety Management**: Temperature monitoring, cell imbalance detection, and fault reporting

The C/C++ implementation using libmodbus demonstrates efficient polling of BMS parameters with proper data scaling and error handling. The Rust implementation showcases async operations, type-safe data structures, and idiomatic warning detection. Both examples include protection status decoding and critical condition monitoring for battery safety. These patterns enable integration of battery systems into SCADA platforms, energy management systems, and IoT monitoring solutions for applications ranging from electric vehicles to grid-scale energy storage.