# Temperature Controller Integration via Modbus

## Detailed Description

Temperature Controller Integration via Modbus involves communicating with industrial temperature control devices (typically PID controllers) to monitor and regulate thermal processes. These controllers are commonly used in manufacturing, HVAC systems, ovens, furnaces, and various industrial heating/cooling applications.

### Key Concepts

**PID Controllers:**
- **Proportional-Integral-Derivative (PID)** controllers maintain a process variable (temperature) at a desired setpoint
- They continuously calculate an error value and apply corrections based on proportional, integral, and derivative terms
- Modbus provides the communication interface to read sensor values and adjust control parameters

**Common Operations:**
1. **Reading Temperature Values:**
   - Process Value (PV): Current measured temperature
   - Setpoint (SP): Target temperature
   - Output Value: Control output percentage (0-100%)

2. **Setting Parameters:**
   - Setpoint adjustment
   - PID tuning parameters (Kp, Ki, Kd)
   - Control mode (Auto/Manual)
   - Alarm thresholds

3. **Status Monitoring:**
   - Alarm states
   - Controller mode
   - Sensor health/faults

**Typical Modbus Register Layout:**
Most temperature controllers follow a similar pattern:
- **Holding Registers (Read/Write):** Setpoints, PID parameters, operating modes
- **Input Registers (Read-only):** Process values, alarm states, status information

### Register Addressing Examples

Common register mappings (varies by manufacturer):
- `40001` (0x0000): Process Value (PV) - Current Temperature
- `40002` (0x0001): Setpoint (SP) - Target Temperature  
- `40003` (0x0002): Output Value - Control output %
- `40010` (0x0009): Proportional Band (P)
- `40011` (0x000A): Integral Time (I)
- `40012` (0x000B): Derivative Time (D)
- `40020` (0x0013): Control Mode (0=Manual, 1=Auto)

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <modbus/modbus.h>
#include <unistd.h>
#include <errno.h>

// Temperature controller configuration
#define CONTROLLER_SLAVE_ID     1
#define CONTROLLER_IP           "192.168.1.100"
#define CONTROLLER_PORT         502

// Register addresses (adjust based on your controller's manual)
#define REG_PROCESS_VALUE       0x0000  // Current temperature (read-only)
#define REG_SETPOINT            0x0001  // Target temperature (read/write)
#define REG_OUTPUT_VALUE        0x0002  // Control output % (read-only)
#define REG_ALARM_STATUS        0x0003  // Alarm bits (read-only)
#define REG_CONTROL_MODE        0x0013  // 0=Manual, 1=Auto
#define REG_PROPORTIONAL        0x0009  // P parameter
#define REG_INTEGRAL            0x000A  // I parameter
#define REG_DERIVATIVE          0x000B  // D parameter

// Temperature scaling (depends on controller - e.g., 0.1°C per unit)
#define TEMP_SCALE              10.0

typedef struct {
    float process_value;      // Current temperature
    float setpoint;           // Target temperature
    float output_percentage;  // Control output
    uint16_t alarm_status;    // Alarm bits
    uint16_t control_mode;    // Operating mode
} TempControllerStatus;

typedef struct {
    float proportional;
    float integral;
    float derivative;
} PIDParameters;

// Read current temperature and status
int read_temperature_status(modbus_t *ctx, TempControllerStatus *status) {
    uint16_t registers[4];
    
    // Read multiple registers at once for efficiency
    int rc = modbus_read_input_registers(ctx, REG_PROCESS_VALUE, 4, registers);
    if (rc == -1) {
        fprintf(stderr, "Failed to read temperature status: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    // Convert raw values to engineering units
    status->process_value = (int16_t)registers[0] / TEMP_SCALE;
    status->setpoint = (int16_t)registers[1] / TEMP_SCALE;
    status->output_percentage = registers[2] / 10.0;  // Assume 0.1% resolution
    status->alarm_status = registers[3];
    
    return 0;
}

// Set temperature setpoint
int set_temperature_setpoint(modbus_t *ctx, float temperature) {
    // Convert temperature to raw register value
    int16_t raw_value = (int16_t)(temperature * TEMP_SCALE);
    
    int rc = modbus_write_register(ctx, REG_SETPOINT, (uint16_t)raw_value);
    if (rc == -1) {
        fprintf(stderr, "Failed to set setpoint: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    printf("Setpoint set to %.1f°C\n", temperature);
    return 0;
}

// Read PID parameters
int read_pid_parameters(modbus_t *ctx, PIDParameters *pid) {
    uint16_t registers[3];
    
    int rc = modbus_read_holding_registers(ctx, REG_PROPORTIONAL, 3, registers);
    if (rc == -1) {
        fprintf(stderr, "Failed to read PID parameters: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    // Scaling depends on controller - check manual
    pid->proportional = registers[0] / 10.0;
    pid->integral = registers[1];  // seconds
    pid->derivative = registers[2];  // seconds
    
    return 0;
}

// Write PID parameters
int write_pid_parameters(modbus_t *ctx, const PIDParameters *pid) {
    uint16_t registers[3];
    
    registers[0] = (uint16_t)(pid->proportional * 10.0);
    registers[1] = (uint16_t)(pid->integral);
    registers[2] = (uint16_t)(pid->derivative);
    
    int rc = modbus_write_registers(ctx, REG_PROPORTIONAL, 3, registers);
    if (rc == -1) {
        fprintf(stderr, "Failed to write PID parameters: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    printf("PID parameters updated: P=%.1f, I=%.0f, D=%.0f\n", 
           pid->proportional, pid->integral, pid->derivative);
    return 0;
}

// Set control mode (Auto/Manual)
int set_control_mode(modbus_t *ctx, int auto_mode) {
    uint16_t mode = auto_mode ? 1 : 0;
    
    int rc = modbus_write_register(ctx, REG_CONTROL_MODE, mode);
    if (rc == -1) {
        fprintf(stderr, "Failed to set control mode: %s\n", 
                modbus_strerror(errno));
        return -1;
    }
    
    printf("Control mode set to: %s\n", auto_mode ? "AUTO" : "MANUAL");
    return 0;
}

// Monitor temperature with continuous logging
void monitor_temperature(modbus_t *ctx, int duration_seconds) {
    TempControllerStatus status;
    time_t start_time = time(NULL);
    
    printf("Starting temperature monitoring for %d seconds...\n", duration_seconds);
    printf("Time\t\tPV(°C)\tSP(°C)\tOutput(%%)\tAlarms\n");
    printf("========================================================\n");
    
    while (time(NULL) - start_time < duration_seconds) {
        if (read_temperature_status(ctx, &status) == 0) {
            printf("%.0f\t\t%.1f\t%.1f\t%.1f\t\t0x%04X\n",
                   difftime(time(NULL), start_time),
                   status.process_value,
                   status.setpoint,
                   status.output_percentage,
                   status.alarm_status);
        }
        
        sleep(1);  // Update every second
    }
}

int main() {
    modbus_t *ctx;
    
    // Create Modbus TCP context
    ctx = modbus_new_tcp(CONTROLLER_IP, CONTROLLER_PORT);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create Modbus context\n");
        return -1;
    }
    
    // Set slave ID
    modbus_set_slave(ctx, CONTROLLER_SLAVE_ID);
    
    // Connect to controller
    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    
    printf("Connected to temperature controller\n");
    
    // Example 1: Read current status
    TempControllerStatus status;
    if (read_temperature_status(ctx, &status) == 0) {
        printf("\nCurrent Status:\n");
        printf("  Process Value: %.1f°C\n", status.process_value);
        printf("  Setpoint: %.1f°C\n", status.setpoint);
        printf("  Output: %.1f%%\n", status.output_percentage);
        printf("  Alarms: 0x%04X\n", status.alarm_status);
    }
    
    // Example 2: Set new setpoint
    set_temperature_setpoint(ctx, 75.0);
    
    // Example 3: Read PID parameters
    PIDParameters pid;
    if (read_pid_parameters(ctx, &pid) == 0) {
        printf("\nCurrent PID Parameters:\n");
        printf("  P: %.1f\n", pid.proportional);
        printf("  I: %.0f seconds\n", pid.integral);
        printf("  D: %.0f seconds\n", pid.derivative);
    }
    
    // Example 4: Update PID parameters (use with caution!)
    PIDParameters new_pid = {
        .proportional = 5.0,
        .integral = 120.0,
        .derivative = 30.0
    };
    // write_pid_parameters(ctx, &new_pid);  // Uncomment to actually write
    
    // Example 5: Set to Auto mode
    set_control_mode(ctx, 1);
    
    // Example 6: Monitor for 10 seconds
    monitor_temperature(ctx, 10);
    
    // Cleanup
    modbus_close(ctx);
    modbus_free(ctx);
    
    return 0;
}
```

## Rust Implementation

```rust
use tokio_modbus::prelude::*;
use tokio_modbus::client::tcp;
use std::net::SocketAddr;
use std::error::Error;
use std::time::Duration;
use tokio::time::sleep;

// Register addresses
const REG_PROCESS_VALUE: u16 = 0x0000;
const REG_SETPOINT: u16 = 0x0001;
const REG_OUTPUT_VALUE: u16 = 0x0002;
const REG_ALARM_STATUS: u16 = 0x0003;
const REG_CONTROL_MODE: u16 = 0x0013;
const REG_PROPORTIONAL: u16 = 0x0009;
const REG_INTEGRAL: u16 = 0x000A;
const REG_DERIVATIVE: u16 = 0x000B;

const TEMP_SCALE: f32 = 10.0;

#[derive(Debug, Clone)]
struct TempControllerStatus {
    process_value: f32,
    setpoint: f32,
    output_percentage: f32,
    alarm_status: u16,
}

#[derive(Debug, Clone)]
struct PIDParameters {
    proportional: f32,
    integral: f32,
    derivative: f32,
}

struct TemperatureController {
    client: tcp::Client,
}

impl TemperatureController {
    async fn new(addr: SocketAddr, slave_id: u8) -> Result<Self, Box<dyn Error>> {
        let mut ctx = tcp::connect_slave(addr, Slave(slave_id)).await?;
        
        println!("Connected to temperature controller at {}", addr);
        
        Ok(Self { client: ctx })
    }
    
    /// Read current temperature and controller status
    async fn read_status(&mut self) -> Result<TempControllerStatus, Box<dyn Error>> {
        // Read multiple input registers
        let registers = self.client.read_input_registers(REG_PROCESS_VALUE, 4).await?;
        
        let status = TempControllerStatus {
            process_value: (registers[0] as i16) as f32 / TEMP_SCALE,
            setpoint: (registers[1] as i16) as f32 / TEMP_SCALE,
            output_percentage: registers[2] as f32 / 10.0,
            alarm_status: registers[3],
        };
        
        Ok(status)
    }
    
    /// Set temperature setpoint
    async fn set_setpoint(&mut self, temperature: f32) -> Result<(), Box<dyn Error>> {
        let raw_value = (temperature * TEMP_SCALE) as i16 as u16;
        
        self.client.write_single_register(REG_SETPOINT, raw_value).await?;
        
        println!("Setpoint set to {:.1}°C", temperature);
        Ok(())
    }
    
    /// Read PID parameters
    async fn read_pid(&mut self) -> Result<PIDParameters, Box<dyn Error>> {
        let registers = self.client.read_holding_registers(REG_PROPORTIONAL, 3).await?;
        
        let pid = PIDParameters {
            proportional: registers[0] as f32 / 10.0,
            integral: registers[1] as f32,
            derivative: registers[2] as f32,
        };
        
        Ok(pid)
    }
    
    /// Write PID parameters
    async fn write_pid(&mut self, pid: &PIDParameters) -> Result<(), Box<dyn Error>> {
        let registers = vec![
            (pid.proportional * 10.0) as u16,
            pid.integral as u16,
            pid.derivative as u16,
        ];
        
        self.client.write_multiple_registers(REG_PROPORTIONAL, &registers).await?;
        
        println!(
            "PID parameters updated: P={:.1}, I={:.0}, D={:.0}",
            pid.proportional, pid.integral, pid.derivative
        );
        
        Ok(())
    }
    
    /// Set control mode (true = Auto, false = Manual)
    async fn set_control_mode(&mut self, auto_mode: bool) -> Result<(), Box<dyn Error>> {
        let mode = if auto_mode { 1 } else { 0 };
        
        self.client.write_single_register(REG_CONTROL_MODE, mode).await?;
        
        println!("Control mode set to: {}", if auto_mode { "AUTO" } else { "MANUAL" });
        Ok(())
    }
    
    /// Monitor temperature continuously
    async fn monitor(&mut self, duration_secs: u64) -> Result<(), Box<dyn Error>> {
        println!("Starting temperature monitoring for {} seconds...", duration_secs);
        println!("Time\t\tPV(°C)\tSP(°C)\tOutput(%)\tAlarms");
        println!("========================================================");
        
        let start = std::time::Instant::now();
        let mut count = 0;
        
        while start.elapsed().as_secs() < duration_secs {
            match self.read_status().await {
                Ok(status) => {
                    println!(
                        "{}\t\t{:.1}\t{:.1}\t{:.1}\t\t0x{:04X}",
                        count,
                        status.process_value,
                        status.setpoint,
                        status.output_percentage,
                        status.alarm_status
                    );
                    count += 1;
                }
                Err(e) => eprintln!("Read error: {}", e),
            }
            
            sleep(Duration::from_secs(1)).await;
        }
        
        Ok(())
    }
    
    /// Check for alarm conditions
    fn check_alarms(&self, status: &TempControllerStatus) -> Vec<String> {
        let mut alarms = Vec::new();
        
        // Example alarm bit definitions (check your controller's manual)
        if status.alarm_status & 0x0001 != 0 {
            alarms.push("High Temperature Alarm".to_string());
        }
        if status.alarm_status & 0x0002 != 0 {
            alarms.push("Low Temperature Alarm".to_string());
        }
        if status.alarm_status & 0x0004 != 0 {
            alarms.push("Sensor Fault".to_string());
        }
        if status.alarm_status & 0x0008 != 0 {
            alarms.push("Control Output Fault".to_string());
        }
        
        alarms
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let addr = "192.168.1.100:502".parse()?;
    let slave_id = 1;
    
    let mut controller = TemperatureController::new(addr, slave_id).await?;
    
    // Example 1: Read current status
    println!("\n=== Reading Current Status ===");
    let status = controller.read_status().await?;
    println!("Process Value: {:.1}°C", status.process_value);
    println!("Setpoint: {:.1}°C", status.setpoint);
    println!("Output: {:.1}%", status.output_percentage);
    println!("Alarms: 0x{:04X}", status.alarm_status);
    
    // Check for active alarms
    let alarms = controller.check_alarms(&status);
    if !alarms.is_empty() {
        println!("\nActive Alarms:");
        for alarm in alarms {
            println!("  - {}", alarm);
        }
    }
    
    // Example 2: Set new setpoint
    println!("\n=== Setting New Setpoint ===");
    controller.set_setpoint(80.0).await?;
    
    // Example 3: Read PID parameters
    println!("\n=== Reading PID Parameters ===");
    let pid = controller.read_pid().await?;
    println!("P: {:.1}", pid.proportional);
    println!("I: {:.0} seconds", pid.integral);
    println!("D: {:.0} seconds", pid.derivative);
    
    // Example 4: Update PID parameters (commented out for safety)
    /*
    println!("\n=== Updating PID Parameters ===");
    let new_pid = PIDParameters {
        proportional: 5.0,
        integral: 120.0,
        derivative: 30.0,
    };
    controller.write_pid(&new_pid).await?;
    */
    
    // Example 5: Set to Auto mode
    println!("\n=== Setting Control Mode ===");
    controller.set_control_mode(true).await?;
    
    // Example 6: Monitor temperature
    println!("\n=== Monitoring Temperature ===");
    controller.monitor(10).await?;
    
    println!("\nTemperature controller integration complete!");
    
    Ok(())
}
```

## Summary

**Temperature Controller Integration via Modbus** enables industrial automation systems to interface with PID temperature controllers for precise thermal process control. This integration allows:

**Key Capabilities:**
- **Real-time Monitoring:** Read process values, setpoints, and control outputs
- **Setpoint Management:** Remotely adjust target temperatures
- **PID Tuning:** Read and modify proportional, integral, and derivative parameters
- **Mode Control:** Switch between automatic and manual control modes
- **Alarm Monitoring:** Detect and respond to temperature limit violations and sensor faults

**Implementation Considerations:**
- Always consult the controller's Modbus register map (varies by manufacturer)
- Temperature values typically require scaling (e.g., 0.1°C or 0.01°C per unit)
- Signed 16-bit integers are common for temperature representation
- PID parameter changes should be made cautiously and tested thoroughly
- Implement proper error handling for communication failures
- Consider implementing data logging for trend analysis and troubleshooting

**Common Applications:**
- Industrial ovens and furnaces
- Plastic injection molding
- Chemical process control
- HVAC systems
- Food processing equipment
- Laboratory equipment control

This integration provides the foundation for automated temperature control systems, enabling supervisory control, data acquisition (SCADA), and integration with broader manufacturing execution systems (MES).