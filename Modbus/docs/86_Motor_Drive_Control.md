# Motor Drive Control via Modbus

## Overview

Motor Drive Control using Modbus involves communicating with Variable Frequency Drives (VFDs) to control AC motor parameters such as speed, torque, acceleration, and operational modes. VFDs are essential in industrial automation for efficient motor control, energy savings, and process optimization.

## Technical Details

### Common Modbus Registers in VFDs

Most VFDs implement a standard set of Modbus registers, though specific addresses vary by manufacturer:

**Control Registers (Holding Registers):**
- **Command Word** (typically 0x2000): Start/Stop, direction, reset faults
- **Frequency Setpoint** (0x2001): Target motor speed (0.01 Hz resolution)
- **Acceleration Time** (0x2002): Ramp-up time in seconds
- **Deceleration Time** (0x2003): Ramp-down time in seconds
- **Torque Limit** (0x2004): Maximum torque percentage

**Status Registers (Input Registers):**
- **Status Word** (0x3000): Running state, fault status, ready status
- **Output Frequency** (0x3001): Current motor frequency
- **Output Current** (0x3002): Motor current in 0.1A units
- **DC Bus Voltage** (0x3003): Internal DC link voltage
- **Motor Torque** (0x3004): Current torque percentage

### Control Word Bit Structure (Typical)

```
Bit 0: Run/Stop (1=Run, 0=Stop)
Bit 1: Direction (1=Reverse, 0=Forward)
Bit 2: Fault Reset
Bit 3: Emergency Stop
Bits 4-7: Reserved
```

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <modbus/modbus.h>
#include <unistd.h>

// VFD Register Addresses (example addresses - check your VFD manual)
#define VFD_CMD_WORD        0x2000
#define VFD_FREQ_SETPOINT   0x2001
#define VFD_ACCEL_TIME      0x2002
#define VFD_DECEL_TIME      0x2003
#define VFD_STATUS_WORD     0x3000
#define VFD_OUTPUT_FREQ     0x3001
#define VFD_OUTPUT_CURRENT  0x3002
#define VFD_DC_BUS_VOLTAGE  0x3003

// Command Word Bits
#define CMD_RUN             0x0001
#define CMD_STOP            0x0000
#define CMD_FORWARD         0x0000
#define CMD_REVERSE         0x0002
#define CMD_FAULT_RESET     0x0004

// Status Word Bits
#define STATUS_RUNNING      0x0001
#define STATUS_FAULT        0x0008
#define STATUS_READY        0x0010

typedef struct {
    modbus_t *ctx;
    int slave_id;
} VFD;

// Initialize VFD connection
VFD* vfd_init(const char* device, int baud, int slave_id) {
    VFD *vfd = (VFD*)malloc(sizeof(VFD));
    
    vfd->ctx = modbus_new_rtu(device, baud, 'N', 8, 1);
    if (vfd->ctx == NULL) {
        fprintf(stderr, "Failed to create Modbus context\n");
        free(vfd);
        return NULL;
    }
    
    vfd->slave_id = slave_id;
    modbus_set_slave(vfd->ctx, slave_id);
    
    if (modbus_connect(vfd->ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(vfd->ctx);
        free(vfd);
        return NULL;
    }
    
    // Set response timeout
    modbus_set_response_timeout(vfd->ctx, 1, 0);
    
    return vfd;
}

// Set motor frequency (Hz)
int vfd_set_frequency(VFD *vfd, float frequency_hz) {
    // Convert Hz to register value (0.01 Hz resolution)
    uint16_t freq_value = (uint16_t)(frequency_hz * 100);
    
    if (modbus_write_register(vfd->ctx, VFD_FREQ_SETPOINT, freq_value) == -1) {
        fprintf(stderr, "Failed to set frequency: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    printf("Frequency set to %.2f Hz\n", frequency_hz);
    return 0;
}

// Start motor
int vfd_start_motor(VFD *vfd, int direction) {
    uint16_t cmd = CMD_RUN;
    
    if (direction == 1) {
        cmd |= CMD_REVERSE;
    }
    
    if (modbus_write_register(vfd->ctx, VFD_CMD_WORD, cmd) == -1) {
        fprintf(stderr, "Failed to start motor: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    printf("Motor started (%s)\n", direction ? "Reverse" : "Forward");
    return 0;
}

// Stop motor
int vfd_stop_motor(VFD *vfd) {
    if (modbus_write_register(vfd->ctx, VFD_CMD_WORD, CMD_STOP) == -1) {
        fprintf(stderr, "Failed to stop motor: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    printf("Motor stopped\n");
    return 0;
}

// Read VFD status
int vfd_read_status(VFD *vfd, uint16_t *status, float *freq, float *current) {
    uint16_t registers[4];
    
    // Read multiple input registers
    if (modbus_read_input_registers(vfd->ctx, VFD_STATUS_WORD, 4, registers) == -1) {
        fprintf(stderr, "Failed to read status: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    *status = registers[0];
    *freq = registers[1] / 100.0f;  // Convert to Hz
    *current = registers[2] / 10.0f; // Convert to Amps
    
    return 0;
}

// Reset fault
int vfd_reset_fault(VFD *vfd) {
    uint16_t cmd = CMD_FAULT_RESET;
    
    if (modbus_write_register(vfd->ctx, VFD_CMD_WORD, cmd) == -1) {
        fprintf(stderr, "Failed to reset fault: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    // Clear reset bit after a moment
    usleep(100000); // 100ms
    modbus_write_register(vfd->ctx, VFD_CMD_WORD, CMD_STOP);
    
    printf("Fault reset\n");
    return 0;
}

// Set acceleration/deceleration times
int vfd_set_ramp_times(VFD *vfd, uint16_t accel_sec, uint16_t decel_sec) {
    uint16_t values[2] = {accel_sec, decel_sec};
    
    if (modbus_write_registers(vfd->ctx, VFD_ACCEL_TIME, 2, values) == -1) {
        fprintf(stderr, "Failed to set ramp times: %s\n", modbus_strerror(errno));
        return -1;
    }
    
    printf("Ramp times set: Accel=%u s, Decel=%u s\n", accel_sec, decel_sec);
    return 0;
}

// Monitor VFD operation
void vfd_monitor(VFD *vfd, int duration_sec) {
    printf("\nMonitoring VFD for %d seconds...\n", duration_sec);
    printf("Time\tStatus\tFreq(Hz)\tCurrent(A)\n");
    printf("----------------------------------------\n");
    
    for (int i = 0; i < duration_sec; i++) {
        uint16_t status;
        float freq, current;
        
        if (vfd_read_status(vfd, &status, &freq, &current) == 0) {
            printf("%d\t0x%04X\t%.2f\t\t%.1f\n", i, status, freq, current);
            
            if (status & STATUS_FAULT) {
                printf("FAULT DETECTED!\n");
                break;
            }
        }
        
        sleep(1);
    }
}

// Cleanup
void vfd_close(VFD *vfd) {
    if (vfd) {
        if (vfd->ctx) {
            modbus_close(vfd->ctx);
            modbus_free(vfd->ctx);
        }
        free(vfd);
    }
}

// Example usage
int main() {
    VFD *vfd = vfd_init("/dev/ttyUSB0", 9600, 1);
    if (!vfd) {
        return 1;
    }
    
    // Configure ramp times
    vfd_set_ramp_times(vfd, 10, 10); // 10 seconds each
    
    // Set target frequency to 50 Hz
    vfd_set_frequency(vfd, 50.0);
    
    // Start motor in forward direction
    vfd_start_motor(vfd, 0);
    
    // Monitor for 30 seconds
    vfd_monitor(vfd, 30);
    
    // Stop motor
    vfd_stop_motor(vfd);
    
    vfd_close(vfd);
    return 0;
}
```

## Rust Implementation

```rust
use tokio_modbus::prelude::*;
use tokio_serial::SerialStream;
use std::time::Duration;
use tokio::time::sleep;

// VFD Register Addresses
const VFD_CMD_WORD: u16 = 0x2000;
const VFD_FREQ_SETPOINT: u16 = 0x2001;
const VFD_ACCEL_TIME: u16 = 0x2002;
const VFD_DECEL_TIME: u16 = 0x2003;
const VFD_STATUS_WORD: u16 = 0x3000;
const VFD_OUTPUT_FREQ: u16 = 0x3001;
const VFD_OUTPUT_CURRENT: u16 = 0x3002;

// Command Word Bits
const CMD_RUN: u16 = 0x0001;
const CMD_STOP: u16 = 0x0000;
const CMD_REVERSE: u16 = 0x0002;
const CMD_FAULT_RESET: u16 = 0x0004;

// Status Word Bits
const STATUS_RUNNING: u16 = 0x0001;
const STATUS_FAULT: u16 = 0x0008;
const STATUS_READY: u16 = 0x0010;

#[derive(Debug)]
pub struct VFDStatus {
    pub status_word: u16,
    pub output_frequency: f32,
    pub output_current: f32,
    pub is_running: bool,
    pub has_fault: bool,
    pub is_ready: bool,
}

pub struct VFDController {
    ctx: tokio_modbus::client::Context,
}

impl VFDController {
    pub async fn new(port: &str, baud_rate: u32, slave_id: u8) -> Result<Self, Box<dyn std::error::Error>> {
        let builder = tokio_serial::new(port, baud_rate)
            .data_bits(tokio_serial::DataBits::Eight)
            .parity(tokio_serial::Parity::None)
            .stop_bits(tokio_serial::StopBits::One)
            .timeout(Duration::from_millis(1000));
        
        let serial_stream = SerialStream::open(&builder)?;
        let slave = Slave(slave_id);
        
        let ctx = tokio_modbus::client::rtu::attach_slave(serial_stream, slave);
        
        Ok(VFDController { ctx })
    }
    
    /// Set motor frequency in Hz
    pub async fn set_frequency(&mut self, frequency_hz: f32) -> Result<(), Box<dyn std::error::Error>> {
        // Convert Hz to register value (0.01 Hz resolution)
        let freq_value = (frequency_hz * 100.0) as u16;
        
        self.ctx.write_single_register(VFD_FREQ_SETPOINT, freq_value).await?;
        println!("Frequency set to {:.2} Hz", frequency_hz);
        
        Ok(())
    }
    
    /// Start motor
    pub async fn start_motor(&mut self, reverse: bool) -> Result<(), Box<dyn std::error::Error>> {
        let mut cmd = CMD_RUN;
        
        if reverse {
            cmd |= CMD_REVERSE;
        }
        
        self.ctx.write_single_register(VFD_CMD_WORD, cmd).await?;
        println!("Motor started ({})", if reverse { "Reverse" } else { "Forward" });
        
        Ok(())
    }
    
    /// Stop motor
    pub async fn stop_motor(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        self.ctx.write_single_register(VFD_CMD_WORD, CMD_STOP).await?;
        println!("Motor stopped");
        
        Ok(())
    }
    
    /// Emergency stop
    pub async fn emergency_stop(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        self.ctx.write_single_register(VFD_CMD_WORD, 0x0008).await?;
        println!("EMERGENCY STOP activated");
        
        Ok(())
    }
    
    /// Reset fault condition
    pub async fn reset_fault(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        self.ctx.write_single_register(VFD_CMD_WORD, CMD_FAULT_RESET).await?;
        sleep(Duration::from_millis(100)).await;
        self.ctx.write_single_register(VFD_CMD_WORD, CMD_STOP).await?;
        println!("Fault reset");
        
        Ok(())
    }
    
    /// Set acceleration and deceleration times
    pub async fn set_ramp_times(&mut self, accel_sec: u16, decel_sec: u16) -> Result<(), Box<dyn std::error::Error>> {
        let values = vec![accel_sec, decel_sec];
        self.ctx.write_multiple_registers(VFD_ACCEL_TIME, &values).await?;
        println!("Ramp times set: Accel={} s, Decel={} s", accel_sec, decel_sec);
        
        Ok(())
    }
    
    /// Read VFD status
    pub async fn read_status(&mut self) -> Result<VFDStatus, Box<dyn std::error::Error>> {
        let registers = self.ctx.read_input_registers(VFD_STATUS_WORD, 3).await?;
        
        let status_word = registers[0];
        let output_frequency = registers[1] as f32 / 100.0;
        let output_current = registers[2] as f32 / 10.0;
        
        Ok(VFDStatus {
            status_word,
            output_frequency,
            output_current,
            is_running: (status_word & STATUS_RUNNING) != 0,
            has_fault: (status_word & STATUS_FAULT) != 0,
            is_ready: (status_word & STATUS_READY) != 0,
        })
    }
    
    /// Monitor VFD operation
    pub async fn monitor(&mut self, duration_sec: u64) -> Result<(), Box<dyn std::error::Error>> {
        println!("\nMonitoring VFD for {} seconds...", duration_sec);
        println!("Time\tStatus\tFreq(Hz)\tCurrent(A)\tRunning\tFault");
        println!("------------------------------------------------------------");
        
        for i in 0..duration_sec {
            match self.read_status().await {
                Ok(status) => {
                    println!(
                        "{}\t{:#06X}\t{:.2}\t\t{:.1}\t\t{}\t{}",
                        i,
                        status.status_word,
                        status.output_frequency,
                        status.output_current,
                        status.is_running,
                        status.has_fault
                    );
                    
                    if status.has_fault {
                        println!("FAULT DETECTED!");
                        break;
                    }
                }
                Err(e) => {
                    eprintln!("Error reading status: {}", e);
                }
            }
            
            sleep(Duration::from_secs(1)).await;
        }
        
        Ok(())
    }
    
    /// Ramp speed from current to target
    pub async fn ramp_to_speed(&mut self, target_hz: f32, step_hz: f32, interval_ms: u64) -> Result<(), Box<dyn std::error::Error>> {
        let status = self.read_status().await?;
        let mut current_freq = status.output_frequency;
        
        println!("Ramping from {:.2} Hz to {:.2} Hz", current_freq, target_hz);
        
        while (current_freq - target_hz).abs() > step_hz {
            if current_freq < target_hz {
                current_freq += step_hz;
            } else {
                current_freq -= step_hz;
            }
            
            self.set_frequency(current_freq).await?;
            sleep(Duration::from_millis(interval_ms)).await;
        }
        
        self.set_frequency(target_hz).await?;
        println!("Target speed reached");
        
        Ok(())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut vfd = VFDController::new("/dev/ttyUSB0", 9600, 1).await?;
    
    // Configure ramp times
    vfd.set_ramp_times(10, 10).await?;
    
    // Set target frequency
    vfd.set_frequency(50.0).await?;
    
    // Start motor in forward direction
    vfd.start_motor(false).await?;
    
    // Monitor for 30 seconds
    vfd.monitor(30).await?;
    
    // Gradually ramp down
    vfd.ramp_to_speed(0.0, 5.0, 500).await?;
    
    // Stop motor
    vfd.stop_motor().await?;
    
    Ok(())
}
```

## Summary

Motor Drive Control via Modbus enables precise control of VFDs for industrial motor applications. Key capabilities include setting motor speed (frequency), controlling direction, managing acceleration/deceleration profiles, monitoring operational parameters (current, voltage, torque), and handling fault conditions.

The implementations demonstrate essential VFD operations: frequency control with 0.01 Hz resolution, bidirectional motor control, configurable ramp times for smooth acceleration/deceleration, real-time status monitoring, and fault detection/reset mechanisms. Both C/C++ and Rust examples provide robust error handling, timeout management, and practical monitoring functions suitable for production environments.

Common applications include conveyor systems, pumps, fans, HVAC systems, and any industrial process requiring variable-speed motor control with precise speed regulation and energy efficiency.