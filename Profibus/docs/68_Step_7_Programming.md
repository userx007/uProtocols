# Step 7 Programming - Accessing Profibus Data

## Detailed Description

Step 7 is Siemens' programming software for SIMATIC S7 PLCs (Programmable Logic Controllers), which supports multiple programming languages including Ladder Logic (LAD), Function Block Diagram (FBD), and Structured Text (STL/SCL). When working with Profibus networks in Step 7, you access connected devices through the Process Image Input/Output (PII/PIQ) tables or directly through peripheral access.

### Key Concepts:

**1. Process Image vs. Direct I/O Access:**
- **Process Image (PI)**: Profibus data is cyclically transferred to/from the process image during the PLC scan cycle. This provides consistent data throughout the program execution.
- **Direct I/O Access**: Immediate read/write operations bypass the process image, useful for time-critical operations.

**2. Addressing Scheme:**
- Profibus devices are configured in the hardware configuration (HW Config)
- Each device receives a logical address range (e.g., I 0.0 to I 15.7 for inputs)
- Addresses can be byte (IB), word (IW), or double word (ID) oriented

**3. Data Types:**
- Digital I/O: Individual bits or bytes
- Analog values: Words (16-bit) or double words (32-bit)
- Complex data: Data blocks (DBs) for structured information

## Programming Examples

### Ladder Logic (LAD) Example

In Ladder Logic, you typically work with symbolic or absolute addresses:

```
// Reading a digital input from Profibus device
Network 1: Start Motor based on Profibus Input
      |                                                    |
      |  I 10.0              M 0.0                   Q 4.0 |
      |--| |----------------| |------------------------( )-|
      |  Start_Button        Motor_Running           Motor |
      |                                                    |

// Reading analog value from Profibus
Network 2: Scale Temperature Value
      |                                                    |
      | CALL FC 105 (SCALE)                                |
      |   IN    := IW 12        // Profibus analog input   |
      |   HI_LIM := 27648       // High limit              |
      |   LO_LIM := 0           // Low limit               |
      |   BIPOLAR:= FALSE                                  |
      |   RET_VAL=> MW 20       // Scaled output           |
      |                                                    |
```

### Structured Text (SCL) Example

```pascal
// Step 7 SCL (Structured Control Language)
FUNCTION_BLOCK FB_ProfibusDataHandler
VAR_INPUT
    Enable : BOOL;
END_VAR

VAR_OUTPUT
    MotorSpeed : INT;
    Temperature : REAL;
    Status : WORD;
END_VAR

VAR
    RawTemperature : INT;
    ScaledSpeed : INT;
END_VAR

BEGIN
    IF Enable THEN
        // Read digital inputs from Profibus slave at address 10
        Status := IW 10;  // Read status word
        
        // Read analog temperature (16-bit value)
        RawTemperature := PIW 12;
        
        // Scale temperature from raw value (0-27648) to engineering units
        Temperature := INT_TO_REAL(RawTemperature) * 100.0 / 27648.0;
        
        // Write motor speed to Profibus output
        QW 20 := MotorSpeed;
        
        // Direct peripheral access for time-critical data
        // P prefix forces immediate I/O access
        Status := PIW 10;  // Peripheral input word
    END_IF;
END_FUNCTION_BLOCK
```

### Statement List (STL) Example

```assembly
// Step 7 STL (Statement List)
FUNCTION FC_ReadProfibusData : VOID

VAR_TEMP
    TempValue : INT;
    StatusByte : BYTE;
END_VAR

BEGIN
    // Load input byte from Profibus device
    L     IB 10          // Load Input Byte 10
    T     #StatusByte    // Transfer to temporary variable
    
    // Load input word (analog value)
    L     IW 12          // Load Input Word 12
    T     #TempValue     // Transfer to temp
    
    // Conditional output based on input
    A     I 10.0         // Check input bit 10.0
    JCN   NoOutput       // Jump if not set
    
    // Set output
    S     Q 4.0          // Set output 4.0
    
NoOutput:
    // Write word to output
    L     MW 100         // Load memory word
    T     QW 20          // Transfer to output word 20
    
END_FUNCTION
```

## C/C++ Implementation (via S7 Communication Library)

While Step 7 runs on the PLC itself, you might need to access Profibus data from a PC application:

```cpp
// C++ using Snap7 library to communicate with S7 PLC
#include "snap7.h"
#include <iostream>
#include <cstdint>

class ProfibusDataAccess {
private:
    TS7Client* client;
    
public:
    ProfibusDataAccess(const char* plcIP) {
        client = new TS7Client();
        int result = client->ConnectTo(plcIP, 0, 2); // Rack 0, Slot 2
        
        if (result != 0) {
            std::cerr << "Connection failed: " << result << std::endl;
        }
    }
    
    ~ProfibusDataAccess() {
        client->Disconnect();
        delete client;
    }
    
    // Read Profibus input data (mapped to PLC input area)
    bool readDigitalInput(int byteAddr, int bitAddr, bool& value) {
        uint8_t buffer;
        int result = client->ReadArea(S7AreaPE, 0, byteAddr, 1, 
                                       S7WLByte, &buffer);
        
        if (result == 0) {
            value = (buffer & (1 << bitAddr)) != 0;
            return true;
        }
        return false;
    }
    
    // Read analog input word from Profibus
    bool readAnalogInput(int wordAddr, int16_t& value) {
        uint8_t buffer[2];
        int result = client->ReadArea(S7AreaPE, 0, wordAddr, 2, 
                                       S7WLByte, buffer);
        
        if (result == 0) {
            // S7 uses big-endian byte order
            value = (buffer[0] << 8) | buffer[1];
            return true;
        }
        return false;
    }
    
    // Write to Profibus output
    bool writeDigitalOutput(int byteAddr, int bitAddr, bool value) {
        uint8_t buffer;
        
        // Read-modify-write to preserve other bits
        int result = client->ReadArea(S7AreaPA, 0, byteAddr, 1, 
                                       S7WLByte, &buffer);
        
        if (result == 0) {
            if (value) {
                buffer |= (1 << bitAddr);
            } else {
                buffer &= ~(1 << bitAddr);
            }
            
            result = client->WriteArea(S7AreaPA, 0, byteAddr, 1, 
                                        S7WLByte, &buffer);
            return (result == 0);
        }
        return false;
    }
    
    // Read data block containing Profibus data
    bool readDataBlock(int dbNumber, int start, int size, uint8_t* buffer) {
        int result = client->ReadArea(S7AreaDB, dbNumber, start, size,
                                       S7WLByte, buffer);
        return (result == 0);
    }
};

// Example usage
int main() {
    ProfibusDataAccess profibus("192.168.0.1");
    
    // Read digital input from Profibus slave
    bool startButton;
    if (profibus.readDigitalInput(10, 0, startButton)) {
        std::cout << "Start button: " << startButton << std::endl;
    }
    
    // Read analog temperature
    int16_t temperature;
    if (profibus.readAnalogInput(12, temperature)) {
        float tempCelsius = temperature * 100.0f / 27648.0f;
        std::cout << "Temperature: " << tempCelsius << "°C" << std::endl;
    }
    
    // Write output
    profibus.writeDigitalOutput(4, 0, true); // Start motor
    
    return 0;
}
```

## Rust Implementation

```rust
// Rust implementation using s7client crate (wrapper for Snap7)
use std::error::Error;

// Note: This assumes a Rust binding to Snap7 exists
// You may need to create FFI bindings or use an existing crate

pub struct ProfibusClient {
    ip_address: String,
    rack: i32,
    slot: i32,
}

impl ProfibusClient {
    pub fn new(ip: &str, rack: i32, slot: i32) -> Self {
        ProfibusClient {
            ip_address: ip.to_string(),
            rack,
            slot,
        }
    }
    
    // Read digital input from Profibus device
    pub fn read_digital_input(&self, byte_addr: i32, bit_addr: u8) 
        -> Result<bool, Box<dyn Error>> {
        
        let mut buffer = vec![0u8; 1];
        
        // Simulate S7 read operation
        // In real implementation, use FFI to Snap7 or native implementation
        self.read_input_area(byte_addr, &mut buffer)?;
        
        let value = (buffer[0] & (1 << bit_addr)) != 0;
        Ok(value)
    }
    
    // Read analog input word
    pub fn read_analog_input(&self, word_addr: i32) 
        -> Result<i16, Box<dyn Error>> {
        
        let mut buffer = vec![0u8; 2];
        self.read_input_area(word_addr, &mut buffer)?;
        
        // S7 uses big-endian
        let value = i16::from_be_bytes([buffer[0], buffer[1]]);
        Ok(value)
    }
    
    // Write digital output
    pub fn write_digital_output(&self, byte_addr: i32, bit_addr: u8, value: bool) 
        -> Result<(), Box<dyn Error>> {
        
        let mut buffer = vec![0u8; 1];
        
        // Read current state
        self.read_output_area(byte_addr, &mut buffer)?;
        
        // Modify bit
        if value {
            buffer[0] |= 1 << bit_addr;
        } else {
            buffer[0] &= !(1 << bit_addr);
        }
        
        // Write back
        self.write_output_area(byte_addr, &buffer)?;
        Ok(())
    }
    
    // Write analog output
    pub fn write_analog_output(&self, word_addr: i32, value: i16) 
        -> Result<(), Box<dyn Error>> {
        
        let buffer = value.to_be_bytes(); // Big-endian
        self.write_output_area(word_addr, &buffer)?;
        Ok(())
    }
    
    // Helper functions (simplified - actual implementation would use FFI)
    fn read_input_area(&self, start: i32, buffer: &mut [u8]) 
        -> Result<(), Box<dyn Error>> {
        // FFI call to S7 client library
        // s7_read_area(S7_AREA_PE, 0, start, buffer.len(), buffer)
        Ok(())
    }
    
    fn read_output_area(&self, start: i32, buffer: &mut [u8]) 
        -> Result<(), Box<dyn Error>> {
        // FFI call to S7 client library
        Ok(())
    }
    
    fn write_output_area(&self, start: i32, buffer: &[u8]) 
        -> Result<(), Box<dyn Error>> {
        // FFI call to S7 client library
        Ok(())
    }
}

// Data structure for Profibus device data
#[derive(Debug, Clone)]
pub struct ProfibusDeviceData {
    pub digital_inputs: Vec<bool>,
    pub digital_outputs: Vec<bool>,
    pub analog_inputs: Vec<i16>,
    pub analog_outputs: Vec<i16>,
}

impl ProfibusDeviceData {
    pub fn new() -> Self {
        ProfibusDeviceData {
            digital_inputs: Vec::new(),
            digital_outputs: Vec::new(),
            analog_inputs: Vec::new(),
            analog_outputs: Vec::new(),
        }
    }
    
    // Scale analog value to engineering units
    pub fn scale_temperature(&self, raw_value: i16) -> f32 {
        (raw_value as f32) * 100.0 / 27648.0
    }
    
    // Scale engineering units to raw value
    pub fn unscale_temperature(&self, celsius: f32) -> i16 {
        (celsius * 27648.0 / 100.0) as i16
    }
}

// Example usage
fn main() -> Result<(), Box<dyn Error>> {
    let client = ProfibusClient::new("192.168.0.1", 0, 2);
    
    // Read digital input from Profibus slave
    let start_button = client.read_digital_input(10, 0)?;
    println!("Start button: {}", start_button);
    
    // Read analog temperature
    let temp_raw = client.read_analog_input(12)?;
    let data = ProfibusDeviceData::new();
    let temp_celsius = data.scale_temperature(temp_raw);
    println!("Temperature: {:.2}°C", temp_celsius);
    
    // Write digital output
    client.write_digital_output(4, 0, true)?;
    
    // Write analog output (speed control)
    let speed_percent = 75.0;
    let speed_raw = (speed_percent * 27648.0 / 100.0) as i16;
    client.write_analog_output(20, speed_raw)?;
    
    Ok(())
}
```

## Summary

**Step 7 Programming with Profibus** enables PLCs to communicate with distributed I/O devices, drives, and field instruments over the Profibus network. Key aspects include:

- **Multiple Programming Languages**: Supports Ladder Logic (LAD), Function Block Diagram (FBD), Structured Text (SCL), and Statement List (STL)
- **Two Access Methods**: Process Image (cyclic, consistent) and Direct I/O (immediate, time-critical)
- **Flexible Addressing**: Byte, word, and double-word addressing for various data types
- **Hardware Configuration**: Devices configured in HW Config with automatic address assignment
- **External Access**: PC applications can access Profibus data via S7 communication libraries (Snap7, Libnodave) using protocols like S7comm
- **Data Scaling**: Raw sensor values typically require scaling to engineering units (temperature, pressure, speed, etc.)

The integration allows seamless data exchange between the PLC program and Profibus field devices, enabling sophisticated automation systems with real-time control and monitoring capabilities.