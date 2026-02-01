# Solar Inverter Communication via Modbus

## Overview

Solar inverter communication via Modbus enables monitoring and control of photovoltaic (PV) inverters, which convert DC power from solar panels into AC power for the grid or local consumption. Most commercial solar inverters support Modbus RTU (serial) or Modbus TCP (Ethernet) protocols, providing access to real-time performance data, configuration parameters, and control functions.

## Key Parameters Monitored

Solar inverters typically expose hundreds of Modbus registers containing:

**Power and Energy Metrics:**
- AC power output (W/kW)
- DC power input (W/kW)
- Daily/total energy production (kWh)
- Conversion efficiency (%)

**Electrical Measurements:**
- AC voltage, current, frequency (per phase)
- DC voltage and current (per string)
- Power factor
- Reactive power

**Status and Diagnostics:**
- Operating state (standby, running, fault, etc.)
- Fault codes and warnings
- Temperature readings
- Inverter model and firmware version

**Grid Parameters:**
- Grid voltage and frequency
- Grid connection status
- Anti-islanding protection status

## Common Register Layouts

While specific register addresses vary by manufacturer (SMA, Fronius, Huawei, SolarEdge, etc.), many follow the SunSpec Alliance standard for interoperability. SunSpec defines common models for inverter data organized into blocks starting at register 40000.

Typical holding registers include:
- `40000-40002`: SunSpec ID and model
- `40070-40090`: AC measurements
- `40100-40120`: DC measurements
- `40200+`: Status and events

## Programming Considerations

**Polling Strategy:**
- Real-time data: Poll every 1-5 seconds
- Cumulative energy: Poll every 30-60 seconds
- Configuration: Read once at startup

**Error Handling:**
- Implement timeout recovery for communication failures
- Validate data ranges (solar production can't be negative at night)
- Handle inverter sleep mode (no response when PV voltage is low)

**Data Scaling:**
- Many values use integer registers with scale factors
- Example: Power reading of 5234 with scale factor -1 = 523.4W

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <modbus/modbus.h>
#include <errno.h>
#include <unistd.h>

// SunSpec-compatible inverter register definitions
#define INVERTER_SLAVE_ID 1
#define SUNSPEC_BASE_ADDR 40000

// Common Model registers (offsets from base)
#define REG_SUNSPEC_ID 0        // Should read "SunS" (0x53756e53)
#define REG_AC_POWER 14         // AC Power (W)
#define REG_AC_VOLTAGE_A 71     // AC Voltage Phase A
#define REG_AC_CURRENT_A 72     // AC Current Phase A
#define REG_AC_FREQUENCY 33     // Grid Frequency
#define REG_DC_VOLTAGE 10       // DC Voltage
#define REG_DC_CURRENT 11       // DC Current
#define REG_TEMP_CABINET 15     // Cabinet Temperature
#define REG_STATUS 4            // Operating Status
#define REG_TOTAL_ENERGY 24     // Lifetime Energy (kWh)

typedef struct {
    modbus_t *ctx;
    int slave_id;
    uint16_t base_addr;
} inverter_t;

typedef struct {
    float ac_power;          // Watts
    float ac_voltage;        // Volts
    float ac_current;        // Amps
    float ac_frequency;      // Hz
    float dc_voltage;        // Volts
    float dc_current;        // Amps
    float temperature;       // Celsius
    uint16_t status;         // Status code
    uint32_t total_energy;   // kWh
    float efficiency;        // Percent
} inverter_data_t;

// Initialize Modbus connection to solar inverter
inverter_t* inverter_init_tcp(const char *ip, int port) {
    inverter_t *inv = malloc(sizeof(inverter_t));
    if (!inv) return NULL;
    
    inv->ctx = modbus_new_tcp(ip, port);
    if (!inv->ctx) {
        free(inv);
        return NULL;
    }
    
    inv->slave_id = INVERTER_SLAVE_ID;
    inv->base_addr = SUNSPEC_BASE_ADDR;
    
    // Set timeout for inverter communication
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    modbus_set_response_timeout(inv->ctx, &timeout);
    
    if (modbus_connect(inv->ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(inv->ctx);
        free(inv);
        return NULL;
    }
    
    modbus_set_slave(inv->ctx, inv->slave_id);
    
    // Verify SunSpec compatibility
    uint16_t sunspec_id[2];
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_SUNSPEC_ID, 
                                     2, sunspec_id) == 2) {
        uint32_t id = (sunspec_id[0] << 16) | sunspec_id[1];
        if (id == 0x53756e53) {
            printf("SunSpec-compatible inverter detected\n");
        }
    }
    
    return inv;
}

// Read comprehensive inverter data
int inverter_read_data(inverter_t *inv, inverter_data_t *data) {
    uint16_t regs[50];
    int16_t signed_val;
    
    // Read AC power (signed 16-bit)
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_AC_POWER, 
                                     1, regs) != 1) {
        return -1;
    }
    signed_val = (int16_t)regs[0];
    data->ac_power = signed_val; // Usually has scale factor
    
    // Read AC voltage
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_AC_VOLTAGE_A, 
                                     1, regs) != 1) {
        return -1;
    }
    data->ac_voltage = regs[0] * 0.1; // Example scale factor
    
    // Read AC current
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_AC_CURRENT_A, 
                                     1, regs) != 1) {
        return -1;
    }
    data->ac_current = regs[0] * 0.01;
    
    // Read AC frequency
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_AC_FREQUENCY, 
                                     1, regs) != 1) {
        return -1;
    }
    data->ac_frequency = regs[0] * 0.01;
    
    // Read DC voltage
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_DC_VOLTAGE, 
                                     1, regs) != 1) {
        return -1;
    }
    data->dc_voltage = regs[0] * 0.1;
    
    // Read DC current
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_DC_CURRENT, 
                                     1, regs) != 1) {
        return -1;
    }
    data->dc_current = regs[0] * 0.01;
    
    // Read temperature
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_TEMP_CABINET, 
                                     1, regs) != 1) {
        return -1;
    }
    signed_val = (int16_t)regs[0];
    data->temperature = signed_val * 0.1;
    
    // Read status
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_STATUS, 
                                     1, &data->status) != 1) {
        return -1;
    }
    
    // Read total energy (32-bit value)
    if (modbus_read_input_registers(inv->ctx, inv->base_addr + REG_TOTAL_ENERGY, 
                                     2, regs) != 2) {
        return -1;
    }
    data->total_energy = (regs[0] << 16) | regs[1];
    
    // Calculate efficiency
    float dc_power = data->dc_voltage * data->dc_current;
    if (dc_power > 0) {
        data->efficiency = (data->ac_power / dc_power) * 100.0;
    } else {
        data->efficiency = 0.0;
    }
    
    return 0;
}

// Decode inverter status
const char* inverter_status_string(uint16_t status) {
    switch(status) {
        case 1: return "OFF";
        case 2: return "SLEEPING";
        case 3: return "STARTING";
        case 4: return "MPPT";
        case 5: return "THROTTLED";
        case 6: return "SHUTTING_DOWN";
        case 7: return "FAULT";
        case 8: return "STANDBY";
        default: return "UNKNOWN";
    }
}

// Monitor inverter with periodic updates
void inverter_monitor_loop(inverter_t *inv, int interval_sec) {
    inverter_data_t data;
    
    while(1) {
        if (inverter_read_data(inv, &data) == 0) {
            printf("\n=== Solar Inverter Status ===\n");
            printf("Status: %s\n", inverter_status_string(data.status));
            printf("AC Power: %.2f W\n", data.ac_power);
            printf("AC Voltage: %.1f V\n", data.ac_voltage);
            printf("AC Current: %.2f A\n", data.ac_current);
            printf("AC Frequency: %.2f Hz\n", data.ac_frequency);
            printf("DC Voltage: %.1f V\n", data.dc_voltage);
            printf("DC Current: %.2f A\n", data.dc_current);
            printf("Temperature: %.1f °C\n", data.temperature);
            printf("Efficiency: %.2f %%\n", data.efficiency);
            printf("Total Energy: %u kWh\n", data.total_energy);
        } else {
            fprintf(stderr, "Failed to read inverter data\n");
        }
        
        sleep(interval_sec);
    }
}

void inverter_close(inverter_t *inv) {
    if (inv) {
        if (inv->ctx) {
            modbus_close(inv->ctx);
            modbus_free(inv->ctx);
        }
        free(inv);
    }
}

int main() {
    // Connect to inverter at IP 192.168.1.100 on default Modbus TCP port
    inverter_t *inverter = inverter_init_tcp("192.168.1.100", 502);
    if (!inverter) {
        fprintf(stderr, "Failed to initialize inverter connection\n");
        return 1;
    }
    
    printf("Connected to solar inverter\n");
    
    // Monitor inverter every 5 seconds
    inverter_monitor_loop(inverter, 5);
    
    inverter_close(inverter);
    return 0;
}
```

## Rust Implementation

```rust
use tokio_modbus::prelude::*;
use tokio::time::{sleep, Duration};
use std::error::Error;

// SunSpec register definitions
const INVERTER_SLAVE_ID: u8 = 1;
const SUNSPEC_BASE: u16 = 40000;
const REG_AC_POWER: u16 = 14;
const REG_AC_VOLTAGE_A: u16 = 71;
const REG_AC_CURRENT_A: u16 = 72;
const REG_AC_FREQUENCY: u16 = 33;
const REG_DC_VOLTAGE: u16 = 10;
const REG_DC_CURRENT: u16 = 11;
const REG_TEMP_CABINET: u16 = 15;
const REG_STATUS: u16 = 4;
const REG_TOTAL_ENERGY: u16 = 24;

#[derive(Debug, Clone)]
pub struct InverterData {
    pub ac_power: f32,
    pub ac_voltage: f32,
    pub ac_current: f32,
    pub ac_frequency: f32,
    pub dc_voltage: f32,
    pub dc_current: f32,
    pub temperature: f32,
    pub status: InverterStatus,
    pub total_energy: u32,
    pub efficiency: f32,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum InverterStatus {
    Off,
    Sleeping,
    Starting,
    Mppt,
    Throttled,
    ShuttingDown,
    Fault,
    Standby,
    Unknown(u16),
}

impl From<u16> for InverterStatus {
    fn from(value: u16) -> Self {
        match value {
            1 => InverterStatus::Off,
            2 => InverterStatus::Sleeping,
            3 => InverterStatus::Starting,
            4 => InverterStatus::Mppt,
            5 => InverterStatus::Throttled,
            6 => InverterStatus::ShuttingDown,
            7 => InverterStatus::Fault,
            8 => InverterStatus::Standby,
            _ => InverterStatus::Unknown(value),
        }
    }
}

impl std::fmt::Display for InverterStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            InverterStatus::Off => write!(f, "OFF"),
            InverterStatus::Sleeping => write!(f, "SLEEPING"),
            InverterStatus::Starting => write!(f, "STARTING"),
            InverterStatus::Mppt => write!(f, "MPPT (Operating)"),
            InverterStatus::Throttled => write!(f, "THROTTLED"),
            InverterStatus::ShuttingDown => write!(f, "SHUTTING DOWN"),
            InverterStatus::Fault => write!(f, "FAULT"),
            InverterStatus::Standby => write!(f, "STANDBY"),
            InverterStatus::Unknown(v) => write!(f, "UNKNOWN({})", v),
        }
    }
}

pub struct SolarInverter {
    client: tokio_modbus::client::Context,
    base_addr: u16,
}

impl SolarInverter {
    /// Connect to a solar inverter via Modbus TCP
    pub async fn connect(addr: &str) -> Result<Self, Box<dyn Error>> {
        let socket_addr = addr.parse()?;
        let client = tcp::connect_slave(socket_addr, Slave(INVERTER_SLAVE_ID)).await?;
        
        println!("Connected to solar inverter at {}", addr);
        
        Ok(Self {
            client,
            base_addr: SUNSPEC_BASE,
        })
    }
    
    /// Read a single input register with error handling
    async fn read_input_register(&mut self, offset: u16) -> Result<u16, Box<dyn Error>> {
        let addr = self.base_addr + offset;
        let result = self.client.read_input_registers(addr, 1).await?;
        Ok(result[0])
    }
    
    /// Read multiple input registers
    async fn read_input_registers(&mut self, offset: u16, count: u16) -> Result<Vec<u16>, Box<dyn Error>> {
        let addr = self.base_addr + offset;
        let result = self.client.read_input_registers(addr, count).await?;
        Ok(result)
    }
    
    /// Read comprehensive inverter data
    pub async fn read_data(&mut self) -> Result<InverterData, Box<dyn Error>> {
        // Read AC power (signed 16-bit)
        let ac_power_raw = self.read_input_register(REG_AC_POWER).await?;
        let ac_power = ac_power_raw as i16 as f32; // Convert to signed
        
        // Read AC voltage
        let ac_voltage_raw = self.read_input_register(REG_AC_VOLTAGE_A).await?;
        let ac_voltage = ac_voltage_raw as f32 * 0.1;
        
        // Read AC current
        let ac_current_raw = self.read_input_register(REG_AC_CURRENT_A).await?;
        let ac_current = ac_current_raw as f32 * 0.01;
        
        // Read AC frequency
        let ac_freq_raw = self.read_input_register(REG_AC_FREQUENCY).await?;
        let ac_frequency = ac_freq_raw as f32 * 0.01;
        
        // Read DC voltage
        let dc_voltage_raw = self.read_input_register(REG_DC_VOLTAGE).await?;
        let dc_voltage = dc_voltage_raw as f32 * 0.1;
        
        // Read DC current
        let dc_current_raw = self.read_input_register(REG_DC_CURRENT).await?;
        let dc_current = dc_current_raw as f32 * 0.01;
        
        // Read temperature (signed)
        let temp_raw = self.read_input_register(REG_TEMP_CABINET).await?;
        let temperature = temp_raw as i16 as f32 * 0.1;
        
        // Read status
        let status_raw = self.read_input_register(REG_STATUS).await?;
        let status = InverterStatus::from(status_raw);
        
        // Read total energy (32-bit)
        let energy_regs = self.read_input_registers(REG_TOTAL_ENERGY, 2).await?;
        let total_energy = ((energy_regs[0] as u32) << 16) | (energy_regs[1] as u32);
        
        // Calculate efficiency
        let dc_power = dc_voltage * dc_current;
        let efficiency = if dc_power > 0.0 {
            (ac_power / dc_power) * 100.0
        } else {
            0.0
        };
        
        Ok(InverterData {
            ac_power,
            ac_voltage,
            ac_current,
            ac_frequency,
            dc_voltage,
            dc_current,
            temperature,
            status,
            total_energy,
            efficiency,
        })
    }
    
    /// Verify SunSpec compatibility
    pub async fn verify_sunspec(&mut self) -> Result<bool, Box<dyn Error>> {
        let id_regs = self.read_input_registers(0, 2).await?;
        let sunspec_id = ((id_regs[0] as u32) << 16) | (id_regs[1] as u32);
        
        // "SunS" in ASCII = 0x53756e53
        if sunspec_id == 0x53756e53 {
            println!("SunSpec-compatible inverter detected");
            Ok(true)
        } else {
            Ok(false)
        }
    }
    
    /// Monitor inverter with periodic updates
    pub async fn monitor(&mut self, interval: Duration) -> Result<(), Box<dyn Error>> {
        loop {
            match self.read_data().await {
                Ok(data) => {
                    println!("\n=== Solar Inverter Status ===");
                    println!("Status: {}", data.status);
                    println!("AC Power: {:.2} W", data.ac_power);
                    println!("AC Voltage: {:.1} V", data.ac_voltage);
                    println!("AC Current: {:.2} A", data.ac_current);
                    println!("AC Frequency: {:.2} Hz", data.ac_frequency);
                    println!("DC Voltage: {:.1} V", data.dc_voltage);
                    println!("DC Current: {:.2} A", data.dc_current);
                    println!("Temperature: {:.1} °C", data.temperature);
                    println!("Efficiency: {:.2} %", data.efficiency);
                    println!("Total Energy: {} kWh", data.total_energy);
                }
                Err(e) => {
                    eprintln!("Failed to read inverter data: {}", e);
                }
            }
            
            sleep(interval).await;
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Connect to inverter
    let mut inverter = SolarInverter::connect("192.168.1.100:502").await?;
    
    // Verify SunSpec compatibility
    inverter.verify_sunspec().await?;
    
    // Monitor inverter every 5 seconds
    inverter.monitor(Duration::from_secs(5)).await?;
    
    Ok(())
}
```

## Summary

Solar inverter communication via Modbus provides essential functionality for PV system monitoring and management. The SunSpec Alliance has standardized register layouts across manufacturers, simplifying integration. Key implementation aspects include handling scale factors for measurement values, managing inverter sleep states during low light conditions, and implementing robust error handling for communication timeouts. Both C/C++ (using libmodbus) and Rust (using tokio-modbus) offer reliable solutions for reading real-time power generation data, electrical parameters, and status information. This enables applications ranging from simple monitoring dashboards to sophisticated energy management systems that optimize self-consumption, battery charging, and grid interaction. Proper polling strategies balance data freshness with network efficiency, typically using 1-5 second intervals for real-time metrics and longer intervals for cumulative energy readings.