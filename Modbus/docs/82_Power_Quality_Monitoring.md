# Power Quality Monitoring via Modbus

## Overview

Power quality monitoring involves measuring and analyzing electrical parameters to ensure power supply meets required standards. Using Modbus, you can read critical power metrics from intelligent power meters, energy analyzers, and power quality analyzers. This enables real-time monitoring of electrical systems to detect anomalies, prevent equipment damage, and optimize energy consumption.

## Key Parameters Monitored

**Voltage Metrics:**
- RMS voltage (phase-to-neutral, phase-to-phase)
- Voltage sags, swells, and interruptions
- Voltage unbalance

**Current Metrics:**
- RMS current per phase
- Current unbalance
- Neutral current

**Power Measurements:**
- Active power (kW)
- Reactive power (kVAR)
- Apparent power (kVA)
- Power factor (PF)

**Harmonic Analysis:**
- Total Harmonic Distortion (THD) for voltage and current
- Individual harmonic components (2nd through 50th harmonics)
- Harmonic spectrum analysis

**Frequency:**
- Line frequency deviation from nominal (50/60 Hz)

## Typical Modbus Register Mapping

Most power quality devices use holding registers (function code 03) or input registers (function code 04). Here's a common register layout:

```
Address | Parameter              | Data Type | Unit  | Scale
--------|------------------------|-----------|-------|-------
40001   | Voltage L1-N          | Float32   | V     | 0.1
40003   | Voltage L2-N          | Float32   | V     | 0.1
40005   | Voltage L3-N          | Float32   | V     | 0.1
40007   | Current L1            | Float32   | A     | 0.01
40009   | Current L2            | Float32   | A     | 0.01
40011   | Current L3            | Float32   | A     | 0.01
40013   | Active Power Total    | Float32   | kW    | 0.001
40015   | Reactive Power Total  | Float32   | kVAR  | 0.001
40017   | Apparent Power Total  | Float32   | kVA   | 0.001
40019   | Power Factor Total    | Float32   | -     | 0.001
40021   | Frequency             | Float32   | Hz    | 0.01
40023   | Voltage THD L1        | Float32   | %     | 0.01
40025   | Current THD L1        | Float32   | %     | 0.01
```

## C/C++ Implementation

### Using libmodbus Library

```c
#include <stdio.h>
#include <stdlib.h>
#include <modbus/modbus.h>
#include <errno.h>

typedef struct {
    float voltage_l1;
    float voltage_l2;
    float voltage_l3;
    float current_l1;
    float current_l2;
    float current_l3;
    float active_power;
    float reactive_power;
    float apparent_power;
    float power_factor;
    float frequency;
    float voltage_thd_l1;
    float current_thd_l1;
} PowerQualityData;

// Convert two 16-bit Modbus registers to float32 (big-endian)
float registers_to_float(uint16_t *registers, int index) {
    uint32_t temp = ((uint32_t)registers[index] << 16) | registers[index + 1];
    return *((float*)&temp);
}

int read_power_quality(modbus_t *ctx, int slave_id, PowerQualityData *data) {
    uint16_t registers[26]; // 13 float32 values = 26 registers
    int rc;
    
    // Set slave device ID
    modbus_set_slave(ctx, slave_id);
    
    // Read 26 holding registers starting from address 0 (40001 in Modicon notation)
    rc = modbus_read_registers(ctx, 0, 26, registers);
    
    if (rc == -1) {
        fprintf(stderr, "Failed to read registers: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    // Parse the registers into power quality data
    data->voltage_l1 = registers_to_float(registers, 0);
    data->voltage_l2 = registers_to_float(registers, 2);
    data->voltage_l3 = registers_to_float(registers, 4);
    data->current_l1 = registers_to_float(registers, 6);
    data->current_l2 = registers_to_float(registers, 8);
    data->current_l3 = registers_to_float(registers, 10);
    data->active_power = registers_to_float(registers, 12);
    data->reactive_power = registers_to_float(registers, 14);
    data->apparent_power = registers_to_float(registers, 16);
    data->power_factor = registers_to_float(registers, 18);
    data->frequency = registers_to_float(registers, 20);
    data->voltage_thd_l1 = registers_to_float(registers, 22);
    data->current_thd_l1 = registers_to_float(registers, 24);
    
    return 0;
}

void print_power_quality(const PowerQualityData *data) {
    printf("\n=== Power Quality Monitoring ===\n");
    printf("Voltages:\n");
    printf("  L1-N: %.2f V\n", data->voltage_l1);
    printf("  L2-N: %.2f V\n", data->voltage_l2);
    printf("  L3-N: %.2f V\n", data->voltage_l3);
    
    printf("\nCurrents:\n");
    printf("  L1: %.2f A\n", data->current_l1);
    printf("  L2: %.2f A\n", data->current_l2);
    printf("  L3: %.2f A\n", data->current_l3);
    
    printf("\nPower:\n");
    printf("  Active: %.3f kW\n", data->active_power);
    printf("  Reactive: %.3f kVAR\n", data->reactive_power);
    printf("  Apparent: %.3f kVA\n", data->apparent_power);
    printf("  Power Factor: %.3f\n", data->power_factor);
    
    printf("\nFrequency: %.2f Hz\n", data->frequency);
    
    printf("\nHarmonics (THD):\n");
    printf("  Voltage L1: %.2f %%\n", data->voltage_thd_l1);
    printf("  Current L1: %.2f %%\n", data->current_thd_l1);
}

int main() {
    modbus_t *ctx;
    PowerQualityData pq_data;
    
    // Create Modbus RTU context (adjust port and parameters as needed)
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
    
    printf("Connected to power meter\n");
    
    // Continuous monitoring loop
    for (int i = 0; i < 10; i++) {
        if (read_power_quality(ctx, 1, &pq_data) == 0) {
            print_power_quality(&pq_data);
            
            // Check for power quality issues
            if (pq_data.voltage_thd_l1 > 5.0) {
                printf("\n*** WARNING: High voltage THD detected! ***\n");
            }
            if (pq_data.power_factor < 0.85) {
                printf("\n*** WARNING: Low power factor detected! ***\n");
            }
        }
        
        sleep(5); // Read every 5 seconds
    }
    
    modbus_close(ctx);
    modbus_free(ctx);
    
    return 0;
}
```

### Advanced C++ Implementation with Error Handling

```cpp
#include <iostream>
#include <modbus/modbus.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iomanip>

class PowerQualityMonitor {
private:
    std::unique_ptr<modbus_t, decltype(&modbus_free)> ctx_;
    int slave_id_;
    
    static float registersToFloat(const std::vector<uint16_t>& registers, size_t index) {
        if (index + 1 >= registers.size()) {
            throw std::out_of_range("Register index out of range");
        }
        uint32_t temp = (static_cast<uint32_t>(registers[index]) << 16) | 
                        registers[index + 1];
        return *reinterpret_cast<float*>(&temp);
    }
    
public:
    struct PowerMetrics {
        float voltage[3];      // L1, L2, L3
        float current[3];      // L1, L2, L3
        float active_power;
        float reactive_power;
        float apparent_power;
        float power_factor;
        float frequency;
        float voltage_thd[3];  // L1, L2, L3
        float current_thd[3];  // L1, L2, L3
    };
    
    PowerQualityMonitor(const std::string& device, int baud, int slave_id)
        : ctx_(modbus_new_rtu(device.c_str(), baud, 'N', 8, 1), modbus_free),
          slave_id_(slave_id) {
        
        if (!ctx_) {
            throw std::runtime_error("Failed to create Modbus context");
        }
        
        modbus_set_response_timeout(ctx_.get(), 1, 0);
        
        if (modbus_connect(ctx_.get()) == -1) {
            throw std::runtime_error(std::string("Connection failed: ") + 
                                     modbus_strerror(errno));
        }
        
        modbus_set_slave(ctx_.get(), slave_id_);
    }
    
    PowerMetrics readMetrics() {
        std::vector<uint16_t> registers(32);
        PowerMetrics metrics{};
        
        int rc = modbus_read_registers(ctx_.get(), 0, registers.size(), 
                                        registers.data());
        
        if (rc == -1) {
            throw std::runtime_error(std::string("Read failed: ") + 
                                     modbus_strerror(errno));
        }
        
        // Parse voltage and current
        for (int i = 0; i < 3; i++) {
            metrics.voltage[i] = registersToFloat(registers, i * 2);
            metrics.current[i] = registersToFloat(registers, 6 + i * 2);
        }
        
        // Parse power measurements
        metrics.active_power = registersToFloat(registers, 12);
        metrics.reactive_power = registersToFloat(registers, 14);
        metrics.apparent_power = registersToFloat(registers, 16);
        metrics.power_factor = registersToFloat(registers, 18);
        metrics.frequency = registersToFloat(registers, 20);
        
        // Parse THD values
        for (int i = 0; i < 3; i++) {
            metrics.voltage_thd[i] = registersToFloat(registers, 22 + i * 2);
            metrics.current_thd[i] = registersToFloat(registers, 28 + i * 2);
        }
        
        return metrics;
    }
    
    void printMetrics(const PowerMetrics& m) {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\n=== Power Quality Report ===\n";
        
        std::cout << "\nVoltage (V):";
        for (int i = 0; i < 3; i++) {
            std::cout << "\n  L" << (i+1) << ": " << m.voltage[i];
        }
        
        std::cout << "\n\nCurrent (A):";
        for (int i = 0; i < 3; i++) {
            std::cout << "\n  L" << (i+1) << ": " << m.current[i];
        }
        
        std::cout << "\n\nPower:\n";
        std::cout << "  Active: " << m.active_power << " kW\n";
        std::cout << "  Reactive: " << m.reactive_power << " kVAR\n";
        std::cout << "  Apparent: " << m.apparent_power << " kVA\n";
        std::cout << "  Power Factor: " << m.power_factor << "\n";
        std::cout << "  Frequency: " << m.frequency << " Hz\n";
        
        std::cout << "\nTHD (%):";
        for (int i = 0; i < 3; i++) {
            std::cout << "\n  Voltage L" << (i+1) << ": " << m.voltage_thd[i];
            std::cout << " | Current L" << (i+1) << ": " << m.current_thd[i];
        }
        std::cout << "\n";
    }
    
    ~PowerQualityMonitor() {
        if (ctx_) {
            modbus_close(ctx_.get());
        }
    }
};

int main() {
    try {
        PowerQualityMonitor monitor("/dev/ttyUSB0", 9600, 1);
        
        for (int i = 0; i < 10; i++) {
            auto metrics = monitor.readMetrics();
            monitor.printMetrics(metrics);
            
            // Alert on power quality issues
            if (metrics.voltage_thd[0] > 5.0 || 
                metrics.voltage_thd[1] > 5.0 || 
                metrics.voltage_thd[2] > 5.0) {
                std::cout << "\n⚠️  WARNING: High voltage THD detected!\n";
            }
            
            if (metrics.power_factor < 0.85) {
                std::cout << "\n⚠️  WARNING: Low power factor!\n";
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Rust Implementation

### Using tokio-modbus Crate

```rust
use tokio_modbus::prelude::*;
use tokio_serial::SerialStream;
use std::time::Duration;

#[derive(Debug)]
struct PowerQualityData {
    voltage: [f32; 3],      // L1, L2, L3
    current: [f32; 3],      // L1, L2, L3
    active_power: f32,
    reactive_power: f32,
    apparent_power: f32,
    power_factor: f32,
    frequency: f32,
    voltage_thd: [f32; 3],  // L1, L2, L3
    current_thd: [f32; 3],  // L1, L2, L3
}

// Convert two 16-bit registers to float32 (big-endian)
fn registers_to_float(registers: &[u16], index: usize) -> f32 {
    let high = registers[index] as u32;
    let low = registers[index + 1] as u32;
    let bits = (high << 16) | low;
    f32::from_bits(bits)
}

async fn read_power_quality(
    ctx: &mut Context,
    start_addr: u16,
    count: u16,
) -> Result<PowerQualityData, Box<dyn std::error::Error>> {
    // Read holding registers
    let registers = ctx.read_holding_registers(start_addr, count).await?;
    
    Ok(PowerQualityData {
        voltage: [
            registers_to_float(&registers, 0),
            registers_to_float(&registers, 2),
            registers_to_float(&registers, 4),
        ],
        current: [
            registers_to_float(&registers, 6),
            registers_to_float(&registers, 8),
            registers_to_float(&registers, 10),
        ],
        active_power: registers_to_float(&registers, 12),
        reactive_power: registers_to_float(&registers, 14),
        apparent_power: registers_to_float(&registers, 16),
        power_factor: registers_to_float(&registers, 18),
        frequency: registers_to_float(&registers, 20),
        voltage_thd: [
            registers_to_float(&registers, 22),
            registers_to_float(&registers, 24),
            registers_to_float(&registers, 26),
        ],
        current_thd: [
            registers_to_float(&registers, 28),
            registers_to_float(&registers, 30),
            registers_to_float(&registers, 32),
        ],
    })
}

fn print_power_quality(data: &PowerQualityData) {
    println!("\n=== Power Quality Monitoring ===");
    
    println!("\nVoltages:");
    for (i, &v) in data.voltage.iter().enumerate() {
        println!("  L{}: {:.2} V", i + 1, v);
    }
    
    println!("\nCurrents:");
    for (i, &c) in data.current.iter().enumerate() {
        println!("  L{}: {:.2} A", i + 1, c);
    }
    
    println!("\nPower:");
    println!("  Active: {:.3} kW", data.active_power);
    println!("  Reactive: {:.3} kVAR", data.reactive_power);
    println!("  Apparent: {:.3} kVA", data.apparent_power);
    println!("  Power Factor: {:.3}", data.power_factor);
    
    println!("\nFrequency: {:.2} Hz", data.frequency);
    
    println!("\nHarmonics (THD):");
    for i in 0..3 {
        println!("  L{} - Voltage: {:.2}% | Current: {:.2}%", 
                 i + 1, data.voltage_thd[i], data.current_thd[i]);
    }
}

fn check_power_quality_alerts(data: &PowerQualityData) {
    // Check voltage THD limits (IEEE 519 recommends < 5%)
    for (i, &thd) in data.voltage_thd.iter().enumerate() {
        if thd > 5.0 {
            println!("\n⚠️  WARNING: High voltage THD on L{}: {:.2}%", i + 1, thd);
        }
    }
    
    // Check current THD limits
    for (i, &thd) in data.current_thd.iter().enumerate() {
        if thd > 15.0 {
            println!("\n⚠️  WARNING: High current THD on L{}: {:.2}%", i + 1, thd);
        }
    }
    
    // Check power factor (typically should be > 0.85)
    if data.power_factor < 0.85 {
        println!("\n⚠️  WARNING: Low power factor: {:.3}", data.power_factor);
    }
    
    // Check voltage levels (example: 230V ±10%)
    for (i, &v) in data.voltage.iter().enumerate() {
        if v < 207.0 || v > 253.0 {
            println!("\n⚠️  WARNING: Voltage out of range on L{}: {:.2}V", i + 1, v);
        }
    }
    
    // Check frequency deviation (±1 Hz for 50Hz systems)
    if (data.frequency - 50.0).abs() > 1.0 {
        println!("\n⚠️  WARNING: Frequency deviation: {:.2} Hz", data.frequency);
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Configure serial port
    let builder = tokio_serial::new("/dev/ttyUSB0", 9600)
        .data_bits(tokio_serial::DataBits::Eight)
        .parity(tokio_serial::Parity::None)
        .stop_bits(tokio_serial::StopBits::One);
    
    let port = SerialStream::open(&builder)?;
    
    // Create Modbus RTU context
    let slave = Slave(1);
    let mut ctx = rtu::connect_slave(port, slave).await?;
    
    println!("Connected to power quality meter");
    
    // Monitoring loop
    for iteration in 1..=10 {
        println!("\n========== Iteration {} ==========", iteration);
        
        match read_power_quality(&mut ctx, 0, 34).await {
            Ok(data) => {
                print_power_quality(&data);
                check_power_quality_alerts(&data);
            }
            Err(e) => {
                eprintln!("Error reading power quality data: {}", e);
            }
        }
        
        tokio::time::sleep(Duration::from_secs(5)).await;
    }
    
    Ok(())
}
```

### Advanced Rust Implementation with Async Monitoring

```rust
use tokio_modbus::prelude::*;
use tokio_serial::SerialStream;
use std::time::Duration;
use chrono::{DateTime, Local};
use serde::{Serialize, Deserialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
struct PowerQualitySnapshot {
    timestamp: DateTime<Local>,
    voltage: [f32; 3],
    current: [f32; 3],
    active_power: f32,
    reactive_power: f32,
    apparent_power: f32,
    power_factor: f32,
    frequency: f32,
    voltage_thd: [f32; 3],
    current_thd: [f32; 3],
}

struct PowerQualityMonitor {
    context: Context,
    slave_id: u8,
    history: Vec<PowerQualitySnapshot>,
}

impl PowerQualityMonitor {
    async fn new(port_path: &str, baud_rate: u32, slave_id: u8) 
        -> Result<Self, Box<dyn std::error::Error>> {
        let builder = tokio_serial::new(port_path, baud_rate)
            .data_bits(tokio_serial::DataBits::Eight)
            .parity(tokio_serial::Parity::None)
            .stop_bits(tokio_serial::StopBits::One);
        
        let port = SerialStream::open(&builder)?;
        let slave = Slave(slave_id);
        let context = rtu::connect_slave(port, slave).await?;
        
        Ok(Self {
            context,
            slave_id,
            history: Vec::new(),
        })
    }
    
    async fn read_snapshot(&mut self) -> Result<PowerQualitySnapshot, Box<dyn std::error::Error>> {
        let registers = self.context.read_holding_registers(0, 34).await?;
        
        let snapshot = PowerQualitySnapshot {
            timestamp: Local::now(),
            voltage: [
                Self::registers_to_float(&registers, 0),
                Self::registers_to_float(&registers, 2),
                Self::registers_to_float(&registers, 4),
            ],
            current: [
                Self::registers_to_float(&registers, 6),
                Self::registers_to_float(&registers, 8),
                Self::registers_to_float(&registers, 10),
            ],
            active_power: Self::registers_to_float(&registers, 12),
            reactive_power: Self::registers_to_float(&registers, 14),
            apparent_power: Self::registers_to_float(&registers, 16),
            power_factor: Self::registers_to_float(&registers, 18),
            frequency: Self::registers_to_float(&registers, 20),
            voltage_thd: [
                Self::registers_to_float(&registers, 22),
                Self::registers_to_float(&registers, 24),
                Self::registers_to_float(&registers, 26),
            ],
            current_thd: [
                Self::registers_to_float(&registers, 28),
                Self::registers_to_float(&registers, 30),
                Self::registers_to_float(&registers, 32),
            ],
        };
        
        self.history.push(snapshot.clone());
        Ok(snapshot)
    }
    
    fn registers_to_float(registers: &[u16], index: usize) -> f32 {
        let high = registers[index] as u32;
        let low = registers[index + 1] as u32;
        let bits = (high << 16) | low;
        f32::from_bits(bits)
    }
    
    fn analyze_trends(&self) {
        if self.history.len() < 2 {
            return;
        }
        
        println!("\n=== Trend Analysis ===");
        let recent = &self.history[self.history.len() - 1];
        let previous = &self.history[self.history.len() - 2];
        
        let pf_change = recent.power_factor - previous.power_factor;
        if pf_change.abs() > 0.05 {
            println!("Power factor changed significantly: {:+.3}", pf_change);
        }
        
        for i in 0..3 {
            let thd_change = recent.voltage_thd[i] - previous.voltage_thd[i];
            if thd_change.abs() > 1.0 {
                println!("Voltage THD L{} changed: {:+.2}%", i + 1, thd_change);
            }
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut monitor = PowerQualityMonitor::new("/dev/ttyUSB0", 9600, 1).await?;
    
    println!("Power Quality Monitor Started");
    
    loop {
        match monitor.read_snapshot().await {
            Ok(snapshot) => {
                println!("\n[{}]", snapshot.timestamp.format("%Y-%m-%d %H:%M:%S"));
                println!("Active Power: {:.3} kW | PF: {:.3} | Freq: {:.2} Hz",
                         snapshot.active_power, snapshot.power_factor, snapshot.frequency);
                
                monitor.analyze_trends();
            }
            Err(e) => {
                eprintln!("Error: {}", e);
            }
        }
        
        tokio::time::sleep(Duration::from_secs(10)).await;
    }
}
```

## Summary

Power quality monitoring via Modbus enables real-time tracking of electrical parameters critical for maintaining reliable power systems. By reading voltage, current, power factor, and harmonic data from intelligent meters, you can detect issues like voltage sags, poor power factor, and harmonic distortion before they cause equipment damage or energy waste.

The implementations shown demonstrate reading multi-phase electrical parameters, converting Modbus register data to engineering units, detecting anomalies based on industry standards like IEEE 519, and maintaining historical data for trend analysis. Both C/C++ and Rust examples provide robust error handling, structured data parsing, and continuous monitoring capabilities suitable for industrial applications ranging from building energy management to industrial process control.