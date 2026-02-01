# Modbus in Smart Meters

## Overview

Modbus has become a fundamental protocol for smart metering applications across utilities infrastructure. Smart meters for electricity, gas, and water consumption leverage Modbus due to its simplicity, widespread support, and ability to operate over various physical layers (RS-485, TCP/IP, and cellular networks). These meters enable utilities to remotely read consumption data, monitor meter health, detect tampering, and implement demand-response programs.

## Key Applications in Smart Metering

### 1. **Electric Smart Meters**
- Real-time energy consumption monitoring
- Time-of-use (TOU) billing data
- Power quality measurements (voltage, current, power factor, harmonics)
- Demand response integration
- Outage detection and restoration verification

### 2. **Gas Smart Meters**
- Volume consumption tracking
- Pressure and temperature compensation
- Leak detection through flow analysis
- Remote valve control (shutoff capability)

### 3. **Water Smart Meters**
- Flow rate and total volume measurement
- Leak detection through continuous monitoring
- Pressure monitoring
- Backflow detection

## Modbus Register Mapping for Smart Meters

Typical register layouts follow industry standards like DLMS/COSEM but are often mapped to Modbus for backward compatibility:

**Common Register Categories:**
- **0x0000-0x00FF**: Meter identification (serial number, firmware version)
- **0x0100-0x01FF**: Instantaneous measurements (current consumption)
- **0x0200-0x02FF**: Accumulated energy/volume (billing registers)
- **0x0300-0x03FF**: Demand values (peak demand, time stamps)
- **0x0400-0x04FF**: Power quality data (for electric meters)
- **0x0500-0x05FF**: Status and alarm registers
- **0x0600-0x06FF**: Configuration parameters

## C/C++ Implementation

Here's a comprehensive example for reading data from an electric smart meter:

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <modbus/modbus.h>

// Register definitions for electric smart meter
#define METER_ADDR              1
#define REG_VOLTAGE_L1          0x0100
#define REG_CURRENT_L1          0x0102
#define REG_ACTIVE_POWER        0x0104
#define REG_REACTIVE_POWER      0x0106
#define REG_POWER_FACTOR        0x0108
#define REG_FREQUENCY           0x010A
#define REG_TOTAL_ENERGY        0x0200  // kWh累計
#define REG_PEAK_DEMAND         0x0300
#define REG_METER_STATUS        0x0500

typedef struct {
    float voltage;          // Volts
    float current;          // Amperes
    float active_power;     // Watts
    float reactive_power;   // VAR
    float power_factor;     // 0-1
    float frequency;        // Hz
    uint32_t total_energy;  // Wh
    uint16_t status;
} SmartMeterData;

// Convert two 16-bit registers to float (IEEE 754)
float registers_to_float(uint16_t *regs) {
    uint32_t combined = ((uint32_t)regs[0] << 16) | regs[1];
    float result;
    memcpy(&result, &combined, sizeof(float));
    return result;
}

// Read instantaneous power data
int read_meter_instantaneous(modbus_t *ctx, SmartMeterData *data) {
    uint16_t regs[12];
    
    // Read 12 consecutive registers starting from voltage
    if (modbus_read_holding_registers(ctx, REG_VOLTAGE_L1, 12, regs) == -1) {
        fprintf(stderr, "Failed to read instantaneous data: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    data->voltage = registers_to_float(&regs[0]);
    data->current = registers_to_float(&regs[2]);
    data->active_power = registers_to_float(&regs[4]);
    data->reactive_power = registers_to_float(&regs[6]);
    data->power_factor = registers_to_float(&regs[8]);
    data->frequency = registers_to_float(&regs[10]);
    
    return 0;
}

// Read accumulated energy
int read_total_energy(modbus_t *ctx, uint32_t *energy) {
    uint16_t regs[2];
    
    if (modbus_read_holding_registers(ctx, REG_TOTAL_ENERGY, 2, regs) == -1) {
        fprintf(stderr, "Failed to read energy: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    *energy = ((uint32_t)regs[0] << 16) | regs[1];
    return 0;
}

// Read meter status and alarms
int read_meter_status(modbus_t *ctx, uint16_t *status) {
    if (modbus_read_holding_registers(ctx, REG_METER_STATUS, 1, status) == -1) {
        fprintf(stderr, "Failed to read status: %s\n", modbus_strerror(errno));
        return -1;
    }
    return 0;
}

// Check for specific alarm conditions
void check_alarms(uint16_t status) {
    if (status & 0x0001) printf("ALARM: Overvoltage detected\n");
    if (status & 0x0002) printf("ALARM: Undervoltage detected\n");
    if (status & 0x0004) printf("ALARM: Overcurrent detected\n");
    if (status & 0x0008) printf("ALARM: Reverse power flow\n");
    if (status & 0x0010) printf("ALARM: Tamper detection\n");
    if (status & 0x0020) printf("ALARM: Battery low\n");
}

int main() {
    modbus_t *ctx;
    SmartMeterData meter_data;
    uint32_t total_energy;
    uint16_t status;
    
    // Create Modbus RTU context (RS-485)
    ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create Modbus context\n");
        return -1;
    }
    
    // Set slave address
    modbus_set_slave(ctx, METER_ADDR);
    
    // Connect
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    
    // Set response timeout
    modbus_set_response_timeout(ctx, 1, 0);
    
    // Read meter data
    printf("Reading smart meter data...\n\n");
    
    if (read_meter_instantaneous(ctx, &meter_data) == 0) {
        printf("=== Instantaneous Measurements ===\n");
        printf("Voltage:        %.2f V\n", meter_data.voltage);
        printf("Current:        %.2f A\n", meter_data.current);
        printf("Active Power:   %.2f W\n", meter_data.active_power);
        printf("Reactive Power: %.2f VAR\n", meter_data.reactive_power);
        printf("Power Factor:   %.3f\n", meter_data.power_factor);
        printf("Frequency:      %.2f Hz\n\n", meter_data.frequency);
    }
    
    if (read_total_energy(ctx, &total_energy) == 0) {
        printf("=== Energy Consumption ===\n");
        printf("Total Energy: %.2f kWh\n\n", total_energy / 1000.0);
    }
    
    if (read_meter_status(ctx, &status) == 0) {
        printf("=== Meter Status ===\n");
        printf("Status Register: 0x%04X\n", status);
        check_alarms(status);
    }
    
    // Cleanup
    modbus_close(ctx);
    modbus_free(ctx);
    
    return 0;
}
```

**Advanced C++ Class-Based Implementation:**

```cpp
#include <modbus/modbus.h>
#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <iomanip>

class SmartMeter {
private:
    modbus_t* ctx;
    int slave_address;
    
    struct MeterReading {
        std::chrono::system_clock::time_point timestamp;
        float voltage;
        float current;
        float power;
        uint32_t energy;
    };
    
    std::vector<MeterReading> reading_history;

public:
    SmartMeter(const std::string& device, int baud, int addr) 
        : slave_address(addr) {
        ctx = modbus_new_rtu(device.c_str(), baud, 'N', 8, 1);
        if (!ctx) {
            throw std::runtime_error("Failed to create Modbus context");
        }
        modbus_set_slave(ctx, slave_address);
    }
    
    ~SmartMeter() {
        if (ctx) {
            modbus_close(ctx);
            modbus_free(ctx);
        }
    }
    
    bool connect() {
        return modbus_connect(ctx) != -1;
    }
    
    MeterReading readCurrent() {
        MeterReading reading;
        reading.timestamp = std::chrono::system_clock::now();
        
        uint16_t regs[8];
        if (modbus_read_holding_registers(ctx, 0x0100, 8, regs) == -1) {
            throw std::runtime_error("Read failed");
        }
        
        reading.voltage = registersToFloat(&regs[0]);
        reading.current = registersToFloat(&regs[2]);
        reading.power = registersToFloat(&regs[4]);
        reading.energy = ((uint32_t)regs[6] << 16) | regs[7];
        
        reading_history.push_back(reading);
        return reading;
    }
    
    float calculateAveragePower(int minutes) {
        auto now = std::chrono::system_clock::now();
        auto cutoff = now - std::chrono::minutes(minutes);
        
        float sum = 0;
        int count = 0;
        
        for (const auto& reading : reading_history) {
            if (reading.timestamp >= cutoff) {
                sum += reading.power;
                count++;
            }
        }
        
        return count > 0 ? sum / count : 0;
    }

private:
    float registersToFloat(uint16_t* regs) {
        uint32_t combined = ((uint32_t)regs[0] << 16) | regs[1];
        float result;
        memcpy(&result, &combined, sizeof(float));
        return result;
    }
};
```

## Rust Implementation

Rust provides memory safety and concurrency features ideal for reliable meter reading systems:

```rust
use tokio_modbus::prelude::*;
use tokio_modbus::client::rtu;
use tokio_serial::SerialStream;
use std::time::Duration;
use anyhow::{Result, Context};

#[derive(Debug, Clone)]
pub struct SmartMeterData {
    pub voltage: f32,
    pub current: f32,
    pub active_power: f32,
    pub reactive_power: f32,
    pub power_factor: f32,
    pub frequency: f32,
    pub total_energy: u32,
    pub status: u16,
}

pub struct SmartMeter {
    client: rtu::Context,
    slave_id: u8,
}

impl SmartMeter {
    pub async fn new(port: &str, baud_rate: u32, slave_id: u8) -> Result<Self> {
        let builder = tokio_serial::new(port, baud_rate);
        let port = SerialStream::open(&builder)
            .context("Failed to open serial port")?;
        
        let client = rtu::connect_slave(port, Slave(slave_id))
            .await
            .context("Failed to connect to slave")?;
        
        Ok(SmartMeter { client, slave_id })
    }
    
    /// Read instantaneous measurements
    pub async fn read_instantaneous(&mut self) -> Result<SmartMeterData> {
        // Read 12 registers starting from 0x0100
        let regs = self.client
            .read_holding_registers(0x0100, 12)
            .await
            .context("Failed to read instantaneous data")?;
        
        Ok(SmartMeterData {
            voltage: Self::registers_to_f32(&regs[0..2]),
            current: Self::registers_to_f32(&regs[2..4]),
            active_power: Self::registers_to_f32(&regs[4..6]),
            reactive_power: Self::registers_to_f32(&regs[6..8]),
            power_factor: Self::registers_to_f32(&regs[8..10]),
            frequency: Self::registers_to_f32(&regs[10..12]),
            total_energy: 0,
            status: 0,
        })
    }
    
    /// Read total accumulated energy
    pub async fn read_total_energy(&mut self) -> Result<u32> {
        let regs = self.client
            .read_holding_registers(0x0200, 2)
            .await
            .context("Failed to read energy")?;
        
        Ok(((regs[0] as u32) << 16) | (regs[1] as u32))
    }
    
    /// Read meter status and alarm flags
    pub async fn read_status(&mut self) -> Result<u16> {
        let regs = self.client
            .read_holding_registers(0x0500, 1)
            .await
            .context("Failed to read status")?;
        
        Ok(regs[0])
    }
    
    /// Read complete meter snapshot
    pub async fn read_all(&mut self) -> Result<SmartMeterData> {
        let mut data = self.read_instantaneous().await?;
        data.total_energy = self.read_total_energy().await?;
        data.status = self.read_status().await?;
        Ok(data)
    }
    
    /// Convert two 16-bit registers to f32 (IEEE 754)
    fn registers_to_f32(regs: &[u16]) -> f32 {
        let combined = ((regs[0] as u32) << 16) | (regs[1] as u32);
        f32::from_bits(combined)
    }
    
    /// Check and print alarm conditions
    pub fn check_alarms(status: u16) {
        if status & 0x0001 != 0 { println!("ALARM: Overvoltage detected"); }
        if status & 0x0002 != 0 { println!("ALARM: Undervoltage detected"); }
        if status & 0x0004 != 0 { println!("ALARM: Overcurrent detected"); }
        if status & 0x0008 != 0 { println!("ALARM: Reverse power flow"); }
        if status & 0x0010 != 0 { println!("ALARM: Tamper detection"); }
        if status & 0x0020 != 0 { println!("ALARM: Battery low"); }
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let mut meter = SmartMeter::new("/dev/ttyUSB0", 9600, 1)
        .await
        .context("Failed to initialize meter")?;
    
    println!("Reading smart meter data...\n");
    
    let data = meter.read_all().await?;
    
    println!("=== Instantaneous Measurements ===");
    println!("Voltage:        {:.2} V", data.voltage);
    println!("Current:        {:.2} A", data.current);
    println!("Active Power:   {:.2} W", data.active_power);
    println!("Reactive Power: {:.2} VAR", data.reactive_power);
    println!("Power Factor:   {:.3}", data.power_factor);
    println!("Frequency:      {:.2} Hz\n", data.frequency);
    
    println!("=== Energy Consumption ===");
    println!("Total Energy: {:.2} kWh\n", data.total_energy as f32 / 1000.0);
    
    println!("=== Meter Status ===");
    println!("Status Register: 0x{:04X}", data.status);
    SmartMeter::check_alarms(data.status);
    
    Ok(())
}
```

**Advanced Rust: Multi-Meter Polling System**

```rust
use tokio::time::{interval, Duration};
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct MeterNetwork {
    meters: Vec<Arc<Mutex<SmartMeter>>>,
}

impl MeterNetwork {
    pub fn new() -> Self {
        MeterNetwork { meters: Vec::new() }
    }
    
    pub async fn add_meter(&mut self, port: &str, baud: u32, slave: u8) -> Result<()> {
        let meter = SmartMeter::new(port, baud, slave).await?;
        self.meters.push(Arc::new(Mutex::new(meter)));
        Ok(())
    }
    
    /// Poll all meters concurrently
    pub async fn poll_all(&self) -> Vec<Result<SmartMeterData>> {
        let mut handles = vec![];
        
        for meter in &self.meters {
            let meter_clone = Arc::clone(meter);
            let handle = tokio::spawn(async move {
                let mut m = meter_clone.lock().await;
                m.read_all().await
            });
            handles.push(handle);
        }
        
        let mut results = vec![];
        for handle in handles {
            match handle.await {
                Ok(result) => results.push(result),
                Err(e) => results.push(Err(anyhow::anyhow!("Task failed: {}", e))),
            }
        }
        
        results
    }
    
    /// Continuous monitoring loop
    pub async fn monitor_continuous(&self, interval_secs: u64) {
        let mut ticker = interval(Duration::from_secs(interval_secs));
        
        loop {
            ticker.tick().await;
            
            let results = self.poll_all().await;
            
            for (idx, result) in results.iter().enumerate() {
                match result {
                    Ok(data) => {
                        println!("Meter {}: {:.2} kWh, {:.2} W", 
                                 idx + 1, 
                                 data.total_energy as f32 / 1000.0,
                                 data.active_power);
                    }
                    Err(e) => {
                        eprintln!("Meter {} error: {}", idx + 1, e);
                    }
                }
            }
        }
    }
}
```

## Summary

Modbus remains a critical protocol for smart metering despite the emergence of newer standards. Its key advantages include:

- **Simplicity**: Easy to implement and debug
- **Interoperability**: Works with legacy infrastructure
- **Flexibility**: Supports RTU (RS-485), TCP/IP, and ASCII variants
- **Cost-effectiveness**: Lower implementation costs than complex protocols

**C/C++ implementations** provide maximum performance and direct hardware access, making them ideal for embedded meter firmware and concentrator systems. **Rust implementations** offer memory safety guarantees and fearless concurrency, making them excellent for reliable data collection infrastructure and AMI (Advanced Metering Infrastructure) headend systems.

Smart meter deployments typically combine Modbus at the field level with higher-level protocols (DLMS/COSEM, DNP3) at the utility network level, creating hybrid architectures that balance legacy compatibility with modern capabilities.