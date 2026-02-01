# Weighing and Load Cell Systems via Modbus

## Detailed Description

### Overview
Weighing and load cell systems are critical components in industrial automation for measuring force, weight, and load in applications ranging from simple platform scales to complex batching systems. When integrated with Modbus communication, these systems enable real-time weight data acquisition, remote calibration, and automated process control.

### System Components

**Load Cells:**
- Strain gauge-based transducers that convert mechanical force into electrical signals
- Common types: compression, tension, shear beam, single-point, and multi-point configurations
- Output signals are typically low-level analog (mV/V) that require amplification

**Weight Indicators/Transmitters:**
- Digitize and process load cell signals
- Perform calculations (gross weight, net weight, tare)
- Provide Modbus RTU/TCP interface for data communication
- Handle calibration, zeroing, and filtering operations

**Common Applications:**
- Platform scales and checkweighers
- Batching and dosing systems
- Truck and rail scales
- Hopper and tank weighing
- Material handling and conveyor systems
- Force measurement and testing equipment

### Modbus Data Organization

Typical Modbus register mapping for weighing systems:

**Input Registers (Read-only):**
- Gross weight (raw weight including container)
- Net weight (gross minus tare)
- Tare weight (container weight)
- Weight status flags (stable, overload, underload, zero)
- Peak weight values
- Rate of weight change
- Diagnostic information

**Holding Registers (Read/Write):**
- Calibration parameters (span, zero offset)
- Tare commands and values
- Zero adjustment
- Filter settings
- Unit selection (kg, lb, ton, etc.)
- Decimal point position
- Operating modes

### Key Operations

1. **Zero Calibration**: Setting the zero point with no load applied
2. **Span Calibration**: Setting the full-scale reading using known weights
3. **Tare Operations**: Subtracting container weight to get net product weight
4. **Weight Reading**: Continuous monitoring of current weight values
5. **Motion Detection**: Identifying when weight is stable vs. in motion

### Communication Characteristics

- **Baud Rates**: Typically 9600-115200 bps for Modbus RTU
- **Data Format**: 16-bit or 32-bit integers (often scaled by 10, 100, or 1000 for decimal precision)
- **Update Rates**: 10-100 Hz depending on application requirements
- **Accuracy Classes**: OIML R76 classifications (III, IIII) for legal-for-trade applications

---

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <modbus/modbus.h>
#include <unistd.h>
#include <string.h>

// Modbus register addresses (example mapping)
#define REG_GROSS_WEIGHT_MSW    0x0000  // Most significant word
#define REG_GROSS_WEIGHT_LSW    0x0001  // Least significant word
#define REG_NET_WEIGHT_MSW      0x0002
#define REG_NET_WEIGHT_LSW      0x0003
#define REG_TARE_WEIGHT_MSW     0x0004
#define REG_TARE_WEIGHT_LSW     0x0005
#define REG_WEIGHT_STATUS       0x0006
#define REG_WEIGHT_UNIT         0x0007
#define REG_DECIMAL_PLACES      0x0008
#define REG_PEAK_WEIGHT_MSW     0x0009
#define REG_PEAK_WEIGHT_LSW     0x000A

// Control registers
#define REG_ZERO_COMMAND        0x0100
#define REG_TARE_COMMAND        0x0101
#define REG_CALIBRATION_MODE    0x0102
#define REG_CALIBRATION_WEIGHT  0x0103
#define REG_FILTER_SETTING      0x0104

// Status flags
#define STATUS_STABLE           0x0001
#define STATUS_OVERLOAD         0x0002
#define STATUS_UNDERLOAD        0x0004
#define STATUS_ZERO             0x0008
#define STATUS_TARE_ACTIVE      0x0010
#define STATUS_CAL_MODE         0x0020
#define STATUS_ERROR            0x0040

// Weight units
typedef enum {
    UNIT_KG = 0,
    UNIT_LB = 1,
    UNIT_GRAM = 2,
    UNIT_OUNCE = 3,
    UNIT_TON = 4
} weight_unit_t;

// Structure to hold weight data
typedef struct {
    float gross_weight;
    float net_weight;
    float tare_weight;
    float peak_weight;
    uint16_t status;
    weight_unit_t unit;
    uint8_t decimal_places;
    bool is_stable;
    bool is_overload;
    bool is_tare_active;
} weight_data_t;

// Convert two 16-bit registers to 32-bit integer
int32_t registers_to_int32(uint16_t msw, uint16_t lsw) {
    return ((int32_t)msw << 16) | lsw;
}

// Convert 32-bit integer to two 16-bit registers
void int32_to_registers(int32_t value, uint16_t *msw, uint16_t *lsw) {
    *msw = (uint16_t)((value >> 16) & 0xFFFF);
    *lsw = (uint16_t)(value & 0xFFFF);
}

// Read weight data from scale
int read_weight_data(modbus_t *ctx, int slave_addr, weight_data_t *data) {
    uint16_t registers[16];
    int rc;
    
    // Set slave address
    modbus_set_slave(ctx, slave_addr);
    
    // Read weight registers
    rc = modbus_read_input_registers(ctx, REG_GROSS_WEIGHT_MSW, 11, registers);
    if (rc == -1) {
        fprintf(stderr, "Failed to read weight registers: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    // Extract gross weight (32-bit signed)
    int32_t gross_raw = registers_to_int32(registers[0], registers[1]);
    int32_t net_raw = registers_to_int32(registers[2], registers[3]);
    int32_t tare_raw = registers_to_int32(registers[4], registers[5]);
    
    // Status and configuration
    data->status = registers[6];
    data->unit = (weight_unit_t)registers[7];
    data->decimal_places = registers[8];
    
    // Peak weight
    int32_t peak_raw = registers_to_int32(registers[9], registers[10]);
    
    // Convert to floating point with decimal scaling
    float scale_factor = 1.0f;
    for (int i = 0; i < data->decimal_places; i++) {
        scale_factor *= 10.0f;
    }
    
    data->gross_weight = (float)gross_raw / scale_factor;
    data->net_weight = (float)net_raw / scale_factor;
    data->tare_weight = (float)tare_raw / scale_factor;
    data->peak_weight = (float)peak_raw / scale_factor;
    
    // Parse status flags
    data->is_stable = (data->status & STATUS_STABLE) != 0;
    data->is_overload = (data->status & STATUS_OVERLOAD) != 0;
    data->is_tare_active = (data->status & STATUS_TARE_ACTIVE) != 0;
    
    return 0;
}

// Zero the scale
int zero_scale(modbus_t *ctx, int slave_addr) {
    modbus_set_slave(ctx, slave_addr);
    
    uint16_t zero_cmd = 1;
    int rc = modbus_write_register(ctx, REG_ZERO_COMMAND, zero_cmd);
    if (rc == -1) {
        fprintf(stderr, "Failed to send zero command: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    printf("Zero command sent successfully\n");
    return 0;
}

// Tare the scale
int tare_scale(modbus_t *ctx, int slave_addr) {
    modbus_set_slave(ctx, slave_addr);
    
    uint16_t tare_cmd = 1;
    int rc = modbus_write_register(ctx, REG_TARE_COMMAND, tare_cmd);
    if (rc == -1) {
        fprintf(stderr, "Failed to send tare command: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    printf("Tare command sent successfully\n");
    return 0;
}

// Perform span calibration with known weight
int calibrate_span(modbus_t *ctx, int slave_addr, float calibration_weight) {
    modbus_set_slave(ctx, slave_addr);
    
    // Enter calibration mode
    int rc = modbus_write_register(ctx, REG_CALIBRATION_MODE, 1);
    if (rc == -1) {
        fprintf(stderr, "Failed to enter calibration mode: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    usleep(500000); // Wait 500ms for mode change
    
    // Write calibration weight (assume 2 decimal places: kg * 100)
    uint16_t cal_weight = (uint16_t)(calibration_weight * 100);
    rc = modbus_write_register(ctx, REG_CALIBRATION_WEIGHT, cal_weight);
    if (rc == -1) {
        fprintf(stderr, "Failed to write calibration weight: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    printf("Span calibration with %.2f kg initiated\n", calibration_weight);
    
    // Exit calibration mode
    usleep(1000000); // Wait 1s for calibration
    modbus_write_register(ctx, REG_CALIBRATION_MODE, 0);
    
    return 0;
}

// Wait for stable weight reading
int wait_for_stable(modbus_t *ctx, int slave_addr, 
                    weight_data_t *data, int timeout_ms) {
    int elapsed = 0;
    int poll_interval = 100; // 100ms
    
    while (elapsed < timeout_ms) {
        if (read_weight_data(ctx, slave_addr, data) == 0) {
            if (data->is_stable && !data->is_overload) {
                return 0; // Success
            }
        }
        usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }
    
    return -1; // Timeout
}

// Get unit name as string
const char* get_unit_name(weight_unit_t unit) {
    switch (unit) {
        case UNIT_KG: return "kg";
        case UNIT_LB: return "lb";
        case UNIT_GRAM: return "g";
        case UNIT_OUNCE: return "oz";
        case UNIT_TON: return "ton";
        default: return "unknown";
    }
}

// Main demonstration function
int main(int argc, char *argv[]) {
    modbus_t *ctx;
    weight_data_t weight_data;
    
    // Create Modbus RTU context
    ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Failed to create Modbus context\n");
        return -1;
    }
    
    // Set response timeout
    modbus_set_response_timeout(ctx, 1, 0);
    
    // Connect
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    
    printf("Connected to weighing system\n\n");
    
    int slave_addr = 1;
    
    // Zero the scale
    printf("=== Zeroing Scale ===\n");
    if (zero_scale(ctx, slave_addr) == 0) {
        sleep(1);
        if (read_weight_data(ctx, slave_addr, &weight_data) == 0) {
            printf("After zero - Gross: %.3f %s\n\n", 
                   weight_data.gross_weight, 
                   get_unit_name(weight_data.unit));
        }
    }
    
    // Simulate placing container and taring
    printf("=== Place container and press Enter for tare ===\n");
    getchar();
    
    if (tare_scale(ctx, slave_addr) == 0) {
        sleep(1);
        if (read_weight_data(ctx, slave_addr, &weight_data) == 0) {
            printf("After tare:\n");
            printf("  Gross: %.3f %s\n", weight_data.gross_weight, 
                   get_unit_name(weight_data.unit));
            printf("  Tare:  %.3f %s\n", weight_data.tare_weight, 
                   get_unit_name(weight_data.unit));
            printf("  Net:   %.3f %s\n\n", weight_data.net_weight, 
                   get_unit_name(weight_data.unit));
        }
    }
    
    // Continuous weight monitoring
    printf("=== Continuous Weight Monitoring (10 samples) ===\n");
    for (int i = 0; i < 10; i++) {
        if (read_weight_data(ctx, slave_addr, &weight_data) == 0) {
            printf("Sample %d: Net=%.3f %s [%s%s%s]\n", 
                   i + 1,
                   weight_data.net_weight,
                   get_unit_name(weight_data.unit),
                   weight_data.is_stable ? "STABLE" : "MOTION",
                   weight_data.is_overload ? " OVERLOAD" : "",
                   weight_data.is_tare_active ? " TARE" : "");
        }
        sleep(1);
    }
    
    // Wait for stable reading
    printf("\n=== Waiting for Stable Weight ===\n");
    if (wait_for_stable(ctx, slave_addr, &weight_data, 5000) == 0) {
        printf("Stable weight achieved: %.3f %s\n", 
               weight_data.net_weight, 
               get_unit_name(weight_data.unit));
    } else {
        printf("Timeout waiting for stable weight\n");
    }
    
    // Cleanup
    modbus_close(ctx);
    modbus_free(ctx);
    
    return 0;
}
```

---

## Rust Implementation

```rust
use tokio_modbus::prelude::*;
use std::error::Error;
use std::time::Duration;
use tokio::time::sleep;

// Modbus register addresses
const REG_GROSS_WEIGHT_MSW: u16 = 0x0000;
const REG_GROSS_WEIGHT_LSW: u16 = 0x0001;
const REG_NET_WEIGHT_MSW: u16 = 0x0002;
const REG_NET_WEIGHT_LSW: u16 = 0x0003;
const REG_TARE_WEIGHT_MSW: u16 = 0x0004;
const REG_TARE_WEIGHT_LSW: u16 = 0x0005;
const REG_WEIGHT_STATUS: u16 = 0x0006;
const REG_WEIGHT_UNIT: u16 = 0x0007;
const REG_DECIMAL_PLACES: u16 = 0x0008;
const REG_PEAK_WEIGHT_MSW: u16 = 0x0009;
const REG_PEAK_WEIGHT_LSW: u16 = 0x000A;

// Control registers
const REG_ZERO_COMMAND: u16 = 0x0100;
const REG_TARE_COMMAND: u16 = 0x0101;
const REG_CALIBRATION_MODE: u16 = 0x0102;
const REG_CALIBRATION_WEIGHT: u16 = 0x0103;
const REG_FILTER_SETTING: u16 = 0x0104;

// Status flags
const STATUS_STABLE: u16 = 0x0001;
const STATUS_OVERLOAD: u16 = 0x0002;
const STATUS_UNDERLOAD: u16 = 0x0004;
const STATUS_ZERO: u16 = 0x0008;
const STATUS_TARE_ACTIVE: u16 = 0x0010;
const STATUS_CAL_MODE: u16 = 0x0020;
const STATUS_ERROR: u16 = 0x0040;

#[derive(Debug, Clone, Copy)]
pub enum WeightUnit {
    Kilogram,
    Pound,
    Gram,
    Ounce,
    Ton,
}

impl WeightUnit {
    fn from_u16(value: u16) -> Self {
        match value {
            0 => WeightUnit::Kilogram,
            1 => WeightUnit::Pound,
            2 => WeightUnit::Gram,
            3 => WeightUnit::Ounce,
            4 => WeightUnit::Ton,
            _ => WeightUnit::Kilogram,
        }
    }
    
    fn as_str(&self) -> &str {
        match self {
            WeightUnit::Kilogram => "kg",
            WeightUnit::Pound => "lb",
            WeightUnit::Gram => "g",
            WeightUnit::Ounce => "oz",
            WeightUnit::Ton => "ton",
        }
    }
}

#[derive(Debug, Clone)]
pub struct WeightData {
    pub gross_weight: f32,
    pub net_weight: f32,
    pub tare_weight: f32,
    pub peak_weight: f32,
    pub status: u16,
    pub unit: WeightUnit,
    pub decimal_places: u8,
    pub is_stable: bool,
    pub is_overload: bool,
    pub is_tare_active: bool,
}

impl WeightData {
    fn new() -> Self {
        WeightData {
            gross_weight: 0.0,
            net_weight: 0.0,
            tare_weight: 0.0,
            peak_weight: 0.0,
            status: 0,
            unit: WeightUnit::Kilogram,
            decimal_places: 0,
            is_stable: false,
            is_overload: false,
            is_tare_active: false,
        }
    }
}

pub struct WeighingSystem {
    client: tokio_modbus::client::Context,
    slave_addr: u8,
}

impl WeighingSystem {
    pub async fn new_rtu(
        port: &str,
        baud_rate: u32,
        slave_addr: u8,
    ) -> Result<Self, Box<dyn Error>> {
        use tokio_serial::SerialStream;
        
        let builder = tokio_serial::new(port, baud_rate);
        let stream = SerialStream::open(&builder)?;
        
        let slave = Slave(slave_addr);
        let client = rtu::connect_slave(stream, slave).await?;
        
        Ok(WeighingSystem {
            client,
            slave_addr,
        })
    }
    
    pub async fn new_tcp(
        addr: &str,
        slave_addr: u8,
    ) -> Result<Self, Box<dyn Error>> {
        let socket_addr = addr.parse()?;
        let client = tcp::connect(socket_addr).await?;
        
        Ok(WeighingSystem {
            client,
            slave_addr,
        })
    }
    
    // Helper: Convert two u16 registers to i32
    fn registers_to_i32(msw: u16, lsw: u16) -> i32 {
        ((msw as i32) << 16) | (lsw as i32)
    }
    
    // Helper: Convert i32 to two u16 registers
    fn i32_to_registers(value: i32) -> (u16, u16) {
        let msw = ((value >> 16) & 0xFFFF) as u16;
        let lsw = (value & 0xFFFF) as u16;
        (msw, lsw)
    }
    
    /// Read current weight data from the scale
    pub async fn read_weight_data(&mut self) -> Result<WeightData, Box<dyn Error>> {
        // Read 11 consecutive input registers
        let registers = self.client
            .read_input_registers(REG_GROSS_WEIGHT_MSW, 11)
            .await?;
        
        let mut data = WeightData::new();
        
        // Parse 32-bit weight values
        let gross_raw = Self::registers_to_i32(registers[0], registers[1]);
        let net_raw = Self::registers_to_i32(registers[2], registers[3]);
        let tare_raw = Self::registers_to_i32(registers[4], registers[5]);
        let peak_raw = Self::registers_to_i32(registers[9], registers[10]);
        
        // Status and configuration
        data.status = registers[6];
        data.unit = WeightUnit::from_u16(registers[7]);
        data.decimal_places = registers[8] as u8;
        
        // Calculate scale factor
        let scale_factor = 10_f32.powi(data.decimal_places as i32);
        
        // Convert to floating point
        data.gross_weight = gross_raw as f32 / scale_factor;
        data.net_weight = net_raw as f32 / scale_factor;
        data.tare_weight = tare_raw as f32 / scale_factor;
        data.peak_weight = peak_raw as f32 / scale_factor;
        
        // Parse status flags
        data.is_stable = (data.status & STATUS_STABLE) != 0;
        data.is_overload = (data.status & STATUS_OVERLOAD) != 0;
        data.is_tare_active = (data.status & STATUS_TARE_ACTIVE) != 0;
        
        Ok(data)
    }
    
    /// Zero the scale
    pub async fn zero_scale(&mut self) -> Result<(), Box<dyn Error>> {
        self.client.write_single_register(REG_ZERO_COMMAND, 1).await?;
        println!("Zero command sent");
        Ok(())
    }
    
    /// Tare the scale
    pub async fn tare_scale(&mut self) -> Result<(), Box<dyn Error>> {
        self.client.write_single_register(REG_TARE_COMMAND, 1).await?;
        println!("Tare command sent");
        Ok(())
    }
    
    /// Perform span calibration with known weight
    pub async fn calibrate_span(&mut self, calibration_weight: f32) 
        -> Result<(), Box<dyn Error>> {
        // Enter calibration mode
        self.client.write_single_register(REG_CALIBRATION_MODE, 1).await?;
        sleep(Duration::from_millis(500)).await;
        
        // Write calibration weight (assume 2 decimal places)
        let cal_weight = (calibration_weight * 100.0) as u16;
        self.client.write_single_register(REG_CALIBRATION_WEIGHT, cal_weight).await?;
        
        println!("Span calibration with {:.2} kg initiated", calibration_weight);
        
        // Wait for calibration to complete
        sleep(Duration::from_secs(1)).await;
        
        // Exit calibration mode
        self.client.write_single_register(REG_CALIBRATION_MODE, 0).await?;
        
        Ok(())
    }
    
    /// Wait for stable weight reading
    pub async fn wait_for_stable(&mut self, timeout: Duration) 
        -> Result<WeightData, Box<dyn Error>> {
        let start = std::time::Instant::now();
        
        loop {
            if start.elapsed() > timeout {
                return Err("Timeout waiting for stable weight".into());
            }
            
            let data = self.read_weight_data().await?;
            
            if data.is_stable && !data.is_overload {
                return Ok(data);
            }
            
            sleep(Duration::from_millis(100)).await;
        }
    }
    
    /// Set filter level (0-9, higher = more filtering)
    pub async fn set_filter(&mut self, level: u16) -> Result<(), Box<dyn Error>> {
        if level > 9 {
            return Err("Filter level must be 0-9".into());
        }
        self.client.write_single_register(REG_FILTER_SETTING, level).await?;
        Ok(())
    }
    
    /// Monitor weight continuously
    pub async fn monitor_weight(&mut self, samples: usize, interval: Duration) 
        -> Result<Vec<WeightData>, Box<dyn Error>> {
        let mut readings = Vec::with_capacity(samples);
        
        for i in 0..samples {
            let data = self.read_weight_data().await?;
            
            println!(
                "Sample {}: Net={:.3} {} [{}{}{}]",
                i + 1,
                data.net_weight,
                data.unit.as_str(),
                if data.is_stable { "STABLE" } else { "MOTION" },
                if data.is_overload { " OVERLOAD" } else { "" },
                if data.is_tare_active { " TARE" } else { "" }
            );
            
            readings.push(data);
            sleep(interval).await;
        }
        
        Ok(readings)
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    println!("=== Weighing System Demo ===\n");
    
    // Create weighing system (choose RTU or TCP)
    // RTU example:
    // let mut scale = WeighingSystem::new_rtu("/dev/ttyUSB0", 9600, 1).await?;
    
    // TCP example:
    let mut scale = WeighingSystem::new_tcp("192.168.1.100:502", 1).await?;
    
    println!("Connected to weighing system\n");
    
    // Zero the scale
    println!("=== Zeroing Scale ===");
    scale.zero_scale().await?;
    sleep(Duration::from_secs(1)).await;
    
    let data = scale.read_weight_data().await?;
    println!("After zero - Gross: {:.3} {}\n", 
             data.gross_weight, data.unit.as_str());
    
    // Simulate taring (in real scenario, wait for user input)
    println!("=== Performing Tare ===");
    sleep(Duration::from_secs(2)).await; // Simulate placing container
    scale.tare_scale().await?;
    sleep(Duration::from_secs(1)).await;
    
    let data = scale.read_weight_data().await?;
    println!("After tare:");
    println!("  Gross: {:.3} {}", data.gross_weight, data.unit.as_str());
    println!("  Tare:  {:.3} {}", data.tare_weight, data.unit.as_str());
    println!("  Net:   {:.3} {}\n", data.net_weight, data.unit.as_str());
    
    // Continuous monitoring
    println!("=== Continuous Weight Monitoring ===");
    scale.monitor_weight(10, Duration::from_secs(1)).await?;
    
    // Wait for stable weight
    println!("\n=== Waiting for Stable Weight ===");
    match scale.wait_for_stable(Duration::from_secs(5)).await {
        Ok(data) => {
            println!("Stable weight achieved: {:.3} {}", 
                     data.net_weight, data.unit.as_str());
            println!("Peak weight: {:.3} {}", 
                     data.peak_weight, data.unit.as_str());
        }
        Err(e) => println!("Error: {}", e),
    }
    
    // Example: Span calibration with 10 kg test weight
    println!("\n=== Span Calibration Example ===");
    println!("Place 10.00 kg calibration weight on scale...");
    sleep(Duration::from_secs(3)).await;
    scale.calibrate_span(10.0).await?;
    
    Ok(())
}
```

---

## Summary

### Key Concepts

**Weighing and load cell systems via Modbus** enable industrial automation applications to:
- Read real-time weight measurements with high precision
- Perform remote calibration (zero and span adjustments)
- Execute tare operations for net weight calculations
- Monitor weight stability and detect motion
- Track peak values and rate of change
- Integrate scales into batching, checkweighing, and process control systems

### Common Register Patterns

Most weighing systems use **32-bit signed integers** for weight values (split across two consecutive 16-bit registers) with decimal scaling factors. Status flags indicate measurement conditions (stable, overload, tare active), while control registers enable commands for zeroing, taring, and calibration.

### Implementation Best Practices

1. **Always check stability flags** before using weight readings in control logic
2. **Handle overload/underload conditions** to prevent system errors
3. **Use appropriate filtering** to reduce noise while maintaining responsiveness
4. **Implement proper calibration procedures** with certified test weights
5. **Consider update rates** - high-speed applications may need 100+ Hz sampling
6. **Scale decimal values correctly** based on the configured decimal places
7. **Validate tare operations** to ensure accurate net weight calculations

### Application Examples

- **Batching systems**: Fill containers to precise target weights with automatic shut-off
- **Checkweighers**: Verify product weights meet specifications in production lines
- **Truck scales**: Automated vehicle weighing with data logging and reporting
- **Process control**: Dynamic weight monitoring for level control and material tracking
- **Quality assurance**: Statistical weight analysis and trend monitoring

The combination of Modbus communication with modern weighing technology provides flexible, accurate, and cost-effective solutions for industrial measurement and control applications.