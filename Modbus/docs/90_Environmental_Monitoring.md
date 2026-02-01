# Environmental Monitoring with Modbus

## Detailed Description

Environmental monitoring via Modbus involves collecting data from sensors that measure atmospheric and environmental conditions such as temperature, humidity, barometric pressure, air quality (CO2, VOCs, particulate matter), and other environmental parameters. These sensors typically connect to field devices, PLCs, or RTUs that communicate using the Modbus protocol (RTU over RS-485 or TCP/IP).

### Common Applications

- **HVAC Systems**: Monitoring indoor climate for comfort and energy efficiency
- **Industrial Safety**: Tracking air quality in manufacturing facilities
- **Greenhouse/Agriculture**: Controlling growing conditions
- **Clean Rooms**: Maintaining precise environmental conditions
- **Weather Stations**: Collecting meteorological data
- **Data Centers**: Monitoring temperature and humidity for equipment protection
- **Smart Buildings**: Optimizing building management systems

### Typical Modbus Register Mapping

Environmental sensors typically expose their data through holding registers or input registers:

| Parameter | Register Type | Address | Data Type | Unit | Scale Factor |
|-----------|--------------|---------|-----------|------|--------------|
| Temperature | Input Register | 0 | INT16 | °C | 0.1 |
| Humidity | Input Register | 1 | UINT16 | %RH | 0.1 |
| Pressure | Input Register | 2-3 | UINT32 | Pa | 1 |
| CO2 Level | Input Register | 4 | UINT16 | ppm | 1 |
| VOC Level | Input Register | 5 | UINT16 | ppb | 1 |
| PM2.5 | Input Register | 6 | UINT16 | µg/m³ | 1 |
| PM10 | Input Register | 7 | UINT16 | µg/m³ | 1 |

---

## C/C++ Implementation

### Example using libmodbus

```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <modbus/modbus.h>
#include <errno.h>
#include <unistd.h>

#define SENSOR_ADDRESS 1
#define TEMP_REG 0
#define HUMIDITY_REG 1
#define PRESSURE_REG 2
#define CO2_REG 4
#define VOC_REG 5
#define PM25_REG 6
#define PM10_REG 7

typedef struct {
    float temperature;    // °C
    float humidity;       // %RH
    uint32_t pressure;    // Pa
    uint16_t co2;         // ppm
    uint16_t voc;         // ppb
    uint16_t pm25;        // µg/m³
    uint16_t pm10;        // µg/m³
} EnvironmentalData;

// Read environmental sensor data
int read_environmental_data(modbus_t *ctx, int slave_addr, EnvironmentalData *data) {
    uint16_t registers[8];
    int rc;
    
    // Set slave address
    modbus_set_slave(ctx, slave_addr);
    
    // Read all sensor registers (0-7)
    rc = modbus_read_input_registers(ctx, 0, 8, registers);
    if (rc == -1) {
        fprintf(stderr, "Failed to read registers: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    // Parse temperature (scaled by 0.1)
    data->temperature = (int16_t)registers[TEMP_REG] / 10.0f;
    
    // Parse humidity (scaled by 0.1)
    data->humidity = registers[HUMIDITY_REG] / 10.0f;
    
    // Parse pressure (32-bit value, big-endian)
    data->pressure = ((uint32_t)registers[PRESSURE_REG] << 16) | 
                     registers[PRESSURE_REG + 1];
    
    // Parse air quality parameters
    data->co2 = registers[CO2_REG];
    data->voc = registers[VOC_REG];
    data->pm25 = registers[PM25_REG];
    data->pm10 = registers[PM10_REG];
    
    return 0;
}

// Print environmental data
void print_environmental_data(const EnvironmentalData *data) {
    printf("\n=== Environmental Data ===\n");
    printf("Temperature:  %.1f °C\n", data->temperature);
    printf("Humidity:     %.1f %%RH\n", data->humidity);
    printf("Pressure:     %u Pa (%.2f hPa)\n", data->pressure, data->pressure / 100.0);
    printf("CO2:          %u ppm\n", data->co2);
    printf("VOC:          %u ppb\n", data->voc);
    printf("PM2.5:        %u µg/m³\n", data->pm25);
    printf("PM10:         %u µg/m³\n", data->pm10);
    printf("==========================\n\n");
}

// Check if values are within safe ranges
int check_environmental_thresholds(const EnvironmentalData *data) {
    int warnings = 0;
    
    if (data->temperature < 15.0 || data->temperature > 30.0) {
        printf("WARNING: Temperature out of comfort range (15-30°C)\n");
        warnings++;
    }
    
    if (data->humidity < 30.0 || data->humidity > 70.0) {
        printf("WARNING: Humidity out of comfort range (30-70%%RH)\n");
        warnings++;
    }
    
    if (data->co2 > 1000) {
        printf("WARNING: High CO2 level (>1000 ppm)\n");
        warnings++;
    }
    
    if (data->pm25 > 35) {
        printf("WARNING: High PM2.5 particulate level (>35 µg/m³)\n");
        warnings++;
    }
    
    return warnings;
}

int main(int argc, char *argv[]) {
    modbus_t *ctx;
    EnvironmentalData data;
    
    // Create Modbus RTU context (RS-485)
    ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Failed to create Modbus context\n");
        return -1;
    }
    
    // Alternative: Create Modbus TCP context
    // ctx = modbus_new_tcp("192.168.1.100", 502);
    
    // Set response timeout
    modbus_set_response_timeout(ctx, 1, 0);
    
    // Connect to device
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    
    printf("Connected to environmental sensor\n");
    
    // Continuous monitoring loop
    for (int i = 0; i < 10; i++) {
        if (read_environmental_data(ctx, SENSOR_ADDRESS, &data) == 0) {
            print_environmental_data(&data);
            check_environmental_thresholds(&data);
        }
        
        sleep(5);  // Poll every 5 seconds
    }
    
    // Cleanup
    modbus_close(ctx);
    modbus_free(ctx);
    
    return 0;
}
```

### C++ Object-Oriented Approach

```cpp
#include <iostream>
#include <iomanip>
#include <memory>
#include <chrono>
#include <thread>
#include <modbus/modbus.h>
#include <stdexcept>

class EnvironmentalSensor {
private:
    modbus_t* ctx;
    int slave_address;
    
    struct SensorData {
        float temperature;
        float humidity;
        uint32_t pressure;
        uint16_t co2;
        uint16_t voc;
        uint16_t pm25;
        uint16_t pm10;
        std::chrono::system_clock::time_point timestamp;
    };
    
    SensorData latest_data;
    
public:
    EnvironmentalSensor(const std::string& device, int baud, int slave_addr)
        : slave_address(slave_addr) {
        
        ctx = modbus_new_rtu(device.c_str(), baud, 'N', 8, 1);
        if (!ctx) {
            throw std::runtime_error("Failed to create Modbus context");
        }
        
        modbus_set_response_timeout(ctx, 1, 0);
        
        if (modbus_connect(ctx) == -1) {
            modbus_free(ctx);
            throw std::runtime_error("Failed to connect to device");
        }
        
        modbus_set_slave(ctx, slave_address);
    }
    
    ~EnvironmentalSensor() {
        if (ctx) {
            modbus_close(ctx);
            modbus_free(ctx);
        }
    }
    
    bool readSensorData() {
        uint16_t registers[8];
        
        int rc = modbus_read_input_registers(ctx, 0, 8, registers);
        if (rc == -1) {
            std::cerr << "Read error: " << modbus_strerror(errno) << std::endl;
            return false;
        }
        
        latest_data.temperature = static_cast<int16_t>(registers[0]) / 10.0f;
        latest_data.humidity = registers[1] / 10.0f;
        latest_data.pressure = (static_cast<uint32_t>(registers[2]) << 16) | registers[3];
        latest_data.co2 = registers[4];
        latest_data.voc = registers[5];
        latest_data.pm25 = registers[6];
        latest_data.pm10 = registers[7];
        latest_data.timestamp = std::chrono::system_clock::now();
        
        return true;
    }
    
    void displayData() const {
        std::cout << "\n=== Environmental Monitoring ===" << std::endl;
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "Temperature:  " << latest_data.temperature << " °C" << std::endl;
        std::cout << "Humidity:     " << latest_data.humidity << " %RH" << std::endl;
        std::cout << "Pressure:     " << latest_data.pressure << " Pa ("
                  << latest_data.pressure / 100.0 << " hPa)" << std::endl;
        std::cout << "CO2:          " << latest_data.co2 << " ppm" << std::endl;
        std::cout << "VOC:          " << latest_data.voc << " ppb" << std::endl;
        std::cout << "PM2.5:        " << latest_data.pm25 << " µg/m³" << std::endl;
        std::cout << "PM10:         " << latest_data.pm10 << " µg/m³" << std::endl;
        std::cout << "===============================" << std::endl;
    }
    
    float getTemperature() const { return latest_data.temperature; }
    float getHumidity() const { return latest_data.humidity; }
    uint16_t getCO2() const { return latest_data.co2; }
};

int main() {
    try {
        EnvironmentalSensor sensor("/dev/ttyUSB0", 9600, 1);
        
        for (int i = 0; i < 10; ++i) {
            if (sensor.readSensorData()) {
                sensor.displayData();
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

---

## Rust Implementation

### Using tokio-modbus crate

```rust
use tokio_modbus::prelude::*;
use tokio::time::{sleep, Duration};
use std::error::Error;

#[derive(Debug, Clone)]
struct EnvironmentalData {
    temperature: f32,    // °C
    humidity: f32,       // %RH
    pressure: u32,       // Pa
    co2: u16,            // ppm
    voc: u16,            // ppb
    pm25: u16,           // µg/m³
    pm10: u16,           // µg/m³
}

impl EnvironmentalData {
    fn from_registers(registers: &[u16]) -> Result<Self, Box<dyn Error>> {
        if registers.len() < 8 {
            return Err("Insufficient register data".into());
        }
        
        Ok(EnvironmentalData {
            temperature: (registers[0] as i16) as f32 / 10.0,
            humidity: registers[1] as f32 / 10.0,
            pressure: ((registers[2] as u32) << 16) | (registers[3] as u32),
            co2: registers[4],
            voc: registers[5],
            pm25: registers[6],
            pm10: registers[7],
        })
    }
    
    fn display(&self) {
        println!("\n=== Environmental Data ===");
        println!("Temperature:  {:.1} °C", self.temperature);
        println!("Humidity:     {:.1} %RH", self.humidity);
        println!("Pressure:     {} Pa ({:.2} hPa)", self.pressure, self.pressure as f32 / 100.0);
        println!("CO2:          {} ppm", self.co2);
        println!("VOC:          {} ppb", self.voc);
        println!("PM2.5:        {} µg/m³", self.pm25);
        println!("PM10:         {} µg/m³", self.pm10);
        println!("==========================\n");
    }
    
    fn check_thresholds(&self) -> Vec<String> {
        let mut warnings = Vec::new();
        
        if self.temperature < 15.0 || self.temperature > 30.0 {
            warnings.push(format!(
                "Temperature out of range: {:.1}°C (safe: 15-30°C)",
                self.temperature
            ));
        }
        
        if self.humidity < 30.0 || self.humidity > 70.0 {
            warnings.push(format!(
                "Humidity out of range: {:.1}%RH (safe: 30-70%RH)",
                self.humidity
            ));
        }
        
        if self.co2 > 1000 {
            warnings.push(format!("High CO2 level: {} ppm (threshold: 1000 ppm)", self.co2));
        }
        
        if self.pm25 > 35 {
            warnings.push(format!(
                "High PM2.5 particulate: {} µg/m³ (threshold: 35 µg/m³)",
                self.pm25
            ));
        }
        
        warnings
    }
}

async fn read_environmental_sensor(
    ctx: &mut Context,
    slave_addr: u8,
) -> Result<EnvironmentalData, Box<dyn Error>> {
    // Set slave address
    ctx.set_slave(Slave(slave_addr));
    
    // Read input registers 0-7
    let registers = ctx.read_input_registers(0, 8).await?;
    
    EnvironmentalData::from_registers(&registers)
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Create Modbus RTU context
    let slave = Slave(1);
    let mut ctx = rtu::connect_slave(
        "/dev/ttyUSB0",
        rtu::SerialConfig {
            baud_rate: 9600,
            data_bits: rtu::DataBits::Eight,
            parity: rtu::Parity::None,
            stop_bits: rtu::StopBits::One,
        },
        slave,
    ).await?;
    
    // Alternative: Create Modbus TCP context
    // let socket_addr = "192.168.1.100:502".parse()?;
    // let mut ctx = tcp::connect(socket_addr).await?;
    
    println!("Connected to environmental sensor");
    
    // Monitoring loop
    for iteration in 1..=10 {
        println!("Reading #{}", iteration);
        
        match read_environmental_sensor(&mut ctx, 1).await {
            Ok(data) => {
                data.display();
                
                let warnings = data.check_thresholds();
                if !warnings.is_empty() {
                    println!("⚠️  WARNINGS:");
                    for warning in warnings {
                        println!("  - {}", warning);
                    }
                }
            }
            Err(e) => {
                eprintln!("Error reading sensor: {}", e);
            }
        }
        
        sleep(Duration::from_secs(5)).await;
    }
    
    Ok(())
}
```

### Advanced Rust Implementation with Async Monitoring

```rust
use tokio_modbus::prelude::*;
use tokio::time::{sleep, interval, Duration};
use std::sync::Arc;
use tokio::sync::RwLock;
use std::error::Error;

struct EnvironmentalMonitor {
    ctx: Arc<RwLock<Context>>,
    slave_address: u8,
    polling_interval: Duration,
}

impl EnvironmentalMonitor {
    async fn new(
        device: &str,
        baud_rate: u32,
        slave_address: u8,
        polling_interval: Duration,
    ) -> Result<Self, Box<dyn Error>> {
        let slave = Slave(slave_address);
        let ctx = rtu::connect_slave(
            device,
            rtu::SerialConfig {
                baud_rate,
                data_bits: rtu::DataBits::Eight,
                parity: rtu::Parity::None,
                stop_bits: rtu::StopBits::One,
            },
            slave,
        ).await?;
        
        Ok(Self {
            ctx: Arc::new(RwLock::new(ctx)),
            slave_address,
            polling_interval,
        })
    }
    
    async fn read_sensors(&self) -> Result<EnvironmentalData, Box<dyn Error>> {
        let mut ctx = self.ctx.write().await;
        ctx.set_slave(Slave(self.slave_address));
        
        let registers = ctx.read_input_registers(0, 8).await?;
        EnvironmentalData::from_registers(&registers)
    }
    
    async fn start_monitoring<F>(&self, callback: F) -> Result<(), Box<dyn Error>>
    where
        F: Fn(EnvironmentalData) + Send + Sync + 'static,
    {
        let mut interval = interval(self.polling_interval);
        
        loop {
            interval.tick().await;
            
            match self.read_sensors().await {
                Ok(data) => callback(data),
                Err(e) => eprintln!("Sensor read error: {}", e),
            }
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let monitor = EnvironmentalMonitor::new(
        "/dev/ttyUSB0",
        9600,
        1,
        Duration::from_secs(5),
    ).await?;
    
    println!("Starting environmental monitoring...");
    
    monitor.start_monitoring(|data| {
        data.display();
        
        let warnings = data.check_thresholds();
        if !warnings.is_empty() {
            for warning in warnings {
                println!("⚠️  {}", warning);
            }
        }
    }).await?;
    
    Ok(())
}
```

---

## Summary

**Environmental Monitoring with Modbus** enables real-time collection and analysis of environmental sensor data including temperature, humidity, pressure, and air quality metrics. This technology is essential for:

- **Industrial Automation**: Maintaining optimal conditions in manufacturing
- **Building Management**: HVAC control and energy efficiency
- **Safety Systems**: Air quality monitoring and ventilation control
- **Agriculture**: Greenhouse climate management
- **Data Centers**: Equipment protection through environmental control

### Key Implementation Points:

1. **Register Mapping**: Understanding sensor-specific register layouts and scaling factors
2. **Data Types**: Handling 16-bit and 32-bit values, signed/unsigned integers
3. **Polling Strategy**: Balancing update frequency with network load
4. **Threshold Monitoring**: Implementing alert systems for out-of-range conditions
5. **Error Handling**: Robust communication error management for reliability

Both C/C++ (using libmodbus) and Rust (using tokio-modbus) provide excellent support for environmental monitoring applications, with Rust offering memory safety and async capabilities particularly suited for concurrent monitoring of multiple sensors.