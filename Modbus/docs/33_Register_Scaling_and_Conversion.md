# Register Scaling and Conversion in Modbus

## Overview

Register scaling and conversion is a fundamental technique in Modbus communication that transforms raw binary values read from or written to registers into meaningful engineering units. Since Modbus registers are 16-bit integer values (0-65535 for holding registers, -32768 to 32767 for input registers), they cannot directly represent floating-point measurements like temperature in degrees Celsius, pressure in PSI, or flow rates in liters per minute. Scaling and conversion bridges this gap using mathematical transformations with scale factors and offsets.

## Core Concepts

### The Linear Transformation Formula

The most common conversion method uses a linear transformation:

**Engineering Value = (Raw Value × Scale Factor) + Offset**

Conversely, to write an engineering value back to a register:

**Raw Value = (Engineering Value - Offset) / Scale Factor**

### Common Scaling Scenarios

**Temperature Sensor Example:**
- Raw register range: 0-10000
- Engineering range: -40°C to +85°C
- Scale factor: 0.0125
- Offset: -40

**Pressure Transmitter Example:**
- Raw register range: 0-32000
- Engineering range: 0-100 PSI
- Scale factor: 0.003125
- Offset: 0

### Data Type Considerations

Modbus registers can represent various data types:
- **Unsigned 16-bit**: 0 to 65535
- **Signed 16-bit**: -32768 to 32767
- **32-bit values**: Stored across two consecutive registers
- **Floating-point**: IEEE 754 format across two registers

## C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Structure to hold scaling parameters
typedef struct {
    float scale_factor;
    float offset;
    int is_signed;
} ScalingConfig;

// Convert raw register value to engineering units
float raw_to_engineering(uint16_t raw_value, ScalingConfig *config) {
    float value;
    
    if (config->is_signed) {
        // Treat as signed 16-bit integer
        int16_t signed_value = (int16_t)raw_value;
        value = (signed_value * config->scale_factor) + config->offset;
    } else {
        // Treat as unsigned 16-bit integer
        value = (raw_value * config->scale_factor) + config->offset;
    }
    
    return value;
}

// Convert engineering value to raw register value
uint16_t engineering_to_raw(float eng_value, ScalingConfig *config) {
    float raw_float = (eng_value - config->offset) / config->scale_factor;
    
    if (config->is_signed) {
        int16_t signed_raw = (int16_t)raw_float;
        return (uint16_t)signed_raw;
    } else {
        return (uint16_t)raw_float;
    }
}

// Convert two registers to 32-bit float (IEEE 754)
float registers_to_float32(uint16_t reg_high, uint16_t reg_low) {
    uint32_t combined = ((uint32_t)reg_high << 16) | reg_low;
    float result;
    memcpy(&result, &combined, sizeof(float));
    return result;
}

// Convert 32-bit float to two registers
void float32_to_registers(float value, uint16_t *reg_high, uint16_t *reg_low) {
    uint32_t temp;
    memcpy(&temp, &value, sizeof(float));
    *reg_high = (uint16_t)(temp >> 16);
    *reg_low = (uint16_t)(temp & 0xFFFF);
}

// Convert two registers to 32-bit signed integer
int32_t registers_to_int32(uint16_t reg_high, uint16_t reg_low) {
    return (int32_t)((uint32_t)reg_high << 16) | reg_low;
}

// Example usage
int main() {
    // Temperature sensor configuration (-40°C to +125°C)
    ScalingConfig temp_config = {
        .scale_factor = 0.0125f,
        .offset = -40.0f,
        .is_signed = 0
    };
    
    // Example: raw value 4000 from temperature sensor
    uint16_t temp_raw = 4000;
    float temperature = raw_to_engineering(temp_raw, &temp_config);
    printf("Temperature: %.2f°C (raw: %u)\n", temperature, temp_raw);
    
    // Convert back to raw
    float target_temp = 25.5f;
    uint16_t temp_raw_write = engineering_to_raw(target_temp, &temp_config);
    printf("Target temperature: %.2f°C -> raw: %u\n", target_temp, temp_raw_write);
    
    // Pressure sensor (0-100 PSI)
    ScalingConfig pressure_config = {
        .scale_factor = 0.003125f,
        .offset = 0.0f,
        .is_signed = 0
    };
    
    uint16_t pressure_raw = 16000;
    float pressure = raw_to_engineering(pressure_raw, &pressure_config);
    printf("Pressure: %.2f PSI (raw: %u)\n", pressure, pressure_raw);
    
    // 32-bit float example (IEEE 754)
    uint16_t float_reg_high = 0x4248;  // Example registers
    uint16_t float_reg_low = 0xF5C3;
    float flow_rate = registers_to_float32(float_reg_high, float_reg_low);
    printf("Flow rate: %.4f L/min\n", flow_rate);
    
    // Convert float back to registers
    float new_flow = 123.456f;
    uint16_t new_high, new_low;
    float32_to_registers(new_flow, &new_high, &new_low);
    printf("New flow: %.4f -> registers: 0x%04X 0x%04X\n", 
           new_flow, new_high, new_low);
    
    return 0;
}
```

### Advanced C++ Class-Based Implementation

```cpp
#include <iostream>
#include <cstdint>
#include <cstring>
#include <stdexcept>

class ModbusScaler {
private:
    float scale_factor_;
    float offset_;
    bool is_signed_;
    float min_value_;
    float max_value_;

public:
    ModbusScaler(float scale, float offset, bool is_signed = false,
                 float min_val = -INFINITY, float max_val = INFINITY)
        : scale_factor_(scale), offset_(offset), is_signed_(is_signed),
          min_value_(min_val), max_value_(max_val) {}

    float toEngineering(uint16_t raw_value) const {
        float value;
        
        if (is_signed_) {
            int16_t signed_value = static_cast<int16_t>(raw_value);
            value = (signed_value * scale_factor_) + offset_;
        } else {
            value = (raw_value * scale_factor_) + offset_;
        }
        
        return value;
    }

    uint16_t toRaw(float eng_value) const {
        // Validate range
        if (eng_value < min_value_ || eng_value > max_value_) {
            throw std::out_of_range("Engineering value out of valid range");
        }
        
        float raw_float = (eng_value - offset_) / scale_factor_;
        
        if (is_signed_) {
            int16_t signed_raw = static_cast<int16_t>(raw_float);
            return static_cast<uint16_t>(signed_raw);
        } else {
            return static_cast<uint16_t>(raw_float);
        }
    }
    
    // Convert array of raw values
    void convertArray(const uint16_t* raw, float* eng, size_t count) const {
        for (size_t i = 0; i < count; ++i) {
            eng[i] = toEngineering(raw[i]);
        }
    }
};

// Specialized class for 32-bit float handling
class ModbusFloat32Converter {
public:
    enum class ByteOrder {
        BIG_ENDIAN,     // ABCD
        LITTLE_ENDIAN,  // DCBA
        BIG_ENDIAN_SWAP, // BADC
        LITTLE_ENDIAN_SWAP // CDAB
    };

private:
    ByteOrder byte_order_;

public:
    ModbusFloat32Converter(ByteOrder order = ByteOrder::BIG_ENDIAN)
        : byte_order_(order) {}

    float toFloat(uint16_t reg_high, uint16_t reg_low) const {
        uint32_t combined;
        
        switch (byte_order_) {
            case ByteOrder::BIG_ENDIAN:
                combined = (static_cast<uint32_t>(reg_high) << 16) | reg_low;
                break;
            case ByteOrder::LITTLE_ENDIAN:
                combined = (static_cast<uint32_t>(reg_low) << 16) | reg_high;
                break;
            case ByteOrder::BIG_ENDIAN_SWAP:
                combined = (static_cast<uint32_t>(reg_low) << 16) | reg_high;
                std::swap(reinterpret_cast<uint8_t*>(&combined)[0], 
                         reinterpret_cast<uint8_t*>(&combined)[1]);
                std::swap(reinterpret_cast<uint8_t*>(&combined)[2], 
                         reinterpret_cast<uint8_t*>(&combined)[3]);
                break;
            case ByteOrder::LITTLE_ENDIAN_SWAP:
                combined = (static_cast<uint32_t>(reg_high) << 16) | reg_low;
                std::swap(reinterpret_cast<uint8_t*>(&combined)[0], 
                         reinterpret_cast<uint8_t*>(&combined)[1]);
                std::swap(reinterpret_cast<uint8_t*>(&combined)[2], 
                         reinterpret_cast<uint8_t*>(&combined)[3]);
                break;
        }
        
        float result;
        std::memcpy(&result, &combined, sizeof(float));
        return result;
    }

    void toRegisters(float value, uint16_t& reg_high, uint16_t& reg_low) const {
        uint32_t temp;
        std::memcpy(&temp, &value, sizeof(float));
        
        switch (byte_order_) {
            case ByteOrder::BIG_ENDIAN:
                reg_high = static_cast<uint16_t>(temp >> 16);
                reg_low = static_cast<uint16_t>(temp & 0xFFFF);
                break;
            case ByteOrder::LITTLE_ENDIAN:
                reg_low = static_cast<uint16_t>(temp >> 16);
                reg_high = static_cast<uint16_t>(temp & 0xFFFF);
                break;
            // Add other byte order cases as needed
            default:
                reg_high = static_cast<uint16_t>(temp >> 16);
                reg_low = static_cast<uint16_t>(temp & 0xFFFF);
        }
    }
};

int main() {
    // Temperature sensor example
    ModbusScaler temp_scaler(0.0125f, -40.0f, false, -40.0f, 125.0f);
    
    uint16_t temp_raw = 4000;
    float temperature = temp_scaler.toEngineering(temp_raw);
    std::cout << "Temperature: " << temperature << "°C\n";
    
    // Float32 example
    ModbusFloat32Converter float_conv;
    uint16_t regs[2] = {0x4248, 0xF5C3};
    float flow = float_conv.toFloat(regs[0], regs[1]);
    std::cout << "Flow rate: " << flow << " L/min\n";
    
    return 0;
}
```

## Rust Implementation

```rust
use std::mem;

/// Configuration for linear scaling conversion
#[derive(Debug, Clone, Copy)]
pub struct ScalingConfig {
    pub scale_factor: f32,
    pub offset: f32,
    pub is_signed: bool,
}

impl ScalingConfig {
    pub fn new(scale_factor: f32, offset: f32, is_signed: bool) -> Self {
        Self {
            scale_factor,
            offset,
            is_signed,
        }
    }
    
    /// Convert raw register value to engineering units
    pub fn to_engineering(&self, raw_value: u16) -> f32 {
        let value = if self.is_signed {
            // Treat as signed 16-bit integer
            let signed_value = raw_value as i16;
            (signed_value as f32 * self.scale_factor) + self.offset
        } else {
            // Treat as unsigned 16-bit integer
            (raw_value as f32 * self.scale_factor) + self.offset
        };
        
        value
    }
    
    /// Convert engineering value to raw register value
    pub fn to_raw(&self, eng_value: f32) -> u16 {
        let raw_float = (eng_value - self.offset) / self.scale_factor;
        
        if self.is_signed {
            let signed_raw = raw_float as i16;
            signed_raw as u16
        } else {
            raw_float as u16
        }
    }
    
    /// Convert array of raw values to engineering units
    pub fn convert_array(&self, raw_values: &[u16]) -> Vec<f32> {
        raw_values.iter()
            .map(|&raw| self.to_engineering(raw))
            .collect()
    }
}

/// Byte order for 32-bit conversions
#[derive(Debug, Clone, Copy)]
pub enum ByteOrder {
    BigEndian,        // ABCD
    LittleEndian,     // DCBA
    BigEndianSwap,    // BADC
    LittleEndianSwap, // CDAB
}

/// Converter for 32-bit float values across two registers
pub struct Float32Converter {
    byte_order: ByteOrder,
}

impl Float32Converter {
    pub fn new(byte_order: ByteOrder) -> Self {
        Self { byte_order }
    }
    
    /// Convert two registers to f32
    pub fn to_float(&self, reg_high: u16, reg_low: u16) -> f32 {
        let combined: u32 = match self.byte_order {
            ByteOrder::BigEndian => {
                ((reg_high as u32) << 16) | (reg_low as u32)
            }
            ByteOrder::LittleEndian => {
                ((reg_low as u32) << 16) | (reg_high as u32)
            }
            ByteOrder::BigEndianSwap => {
                let mut temp = ((reg_low as u32) << 16) | (reg_high as u32);
                let bytes = temp.to_ne_bytes();
                u32::from_ne_bytes([bytes[1], bytes[0], bytes[3], bytes[2]])
            }
            ByteOrder::LittleEndianSwap => {
                let mut temp = ((reg_high as u32) << 16) | (reg_low as u32);
                let bytes = temp.to_ne_bytes();
                u32::from_ne_bytes([bytes[1], bytes[0], bytes[3], bytes[2]])
            }
        };
        
        f32::from_bits(combined)
    }
    
    /// Convert f32 to two registers
    pub fn to_registers(&self, value: f32) -> (u16, u16) {
        let bits = value.to_bits();
        
        match self.byte_order {
            ByteOrder::BigEndian => {
                let reg_high = (bits >> 16) as u16;
                let reg_low = (bits & 0xFFFF) as u16;
                (reg_high, reg_low)
            }
            ByteOrder::LittleEndian => {
                let reg_low = (bits >> 16) as u16;
                let reg_high = (bits & 0xFFFF) as u16;
                (reg_high, reg_low)
            }
            _ => {
                // Simplified for other byte orders
                let reg_high = (bits >> 16) as u16;
                let reg_low = (bits & 0xFFFF) as u16;
                (reg_high, reg_low)
            }
        }
    }
}

/// Convert two registers to i32
pub fn registers_to_i32(reg_high: u16, reg_low: u16) -> i32 {
    let combined = ((reg_high as u32) << 16) | (reg_low as u32);
    combined as i32
}

/// Convert i32 to two registers
pub fn i32_to_registers(value: i32) -> (u16, u16) {
    let unsigned = value as u32;
    let reg_high = (unsigned >> 16) as u16;
    let reg_low = (unsigned & 0xFFFF) as u16;
    (reg_high, reg_low)
}

// Example usage
fn main() {
    // Temperature sensor configuration
    let temp_config = ScalingConfig::new(0.0125, -40.0, false);
    
    let temp_raw = 4000u16;
    let temperature = temp_config.to_engineering(temp_raw);
    println!("Temperature: {:.2}°C (raw: {})", temperature, temp_raw);
    
    let target_temp = 25.5f32;
    let temp_raw_write = temp_config.to_raw(target_temp);
    println!("Target temperature: {:.2}°C -> raw: {}", target_temp, temp_raw_write);
    
    // Pressure sensor (0-100 PSI)
    let pressure_config = ScalingConfig::new(0.003125, 0.0, false);
    
    let pressure_raw = 16000u16;
    let pressure = pressure_config.to_engineering(pressure_raw);
    println!("Pressure: {:.2} PSI (raw: {})", pressure, pressure_raw);
    
    // 32-bit float example
    let float_conv = Float32Converter::new(ByteOrder::BigEndian);
    let flow_rate = float_conv.to_float(0x4248, 0xF5C3);
    println!("Flow rate: {:.4} L/min", flow_rate);
    
    // Convert float back to registers
    let new_flow = 123.456f32;
    let (new_high, new_low) = float_conv.to_registers(new_flow);
    println!("New flow: {:.4} -> registers: 0x{:04X} 0x{:04X}", 
             new_flow, new_high, new_low);
    
    // Array conversion example
    let raw_temps = vec![4000u16, 4800, 5600, 6400];
    let temperatures = temp_config.convert_array(&raw_temps);
    println!("Temperature array: {:?}", temperatures);
}
```

## Summary

Register scaling and conversion is essential for translating Modbus register data into usable engineering values. The linear transformation formula `Engineering Value = (Raw Value × Scale Factor) + Offset` forms the foundation of most conversions. Key considerations include handling signed versus unsigned integers, managing 32-bit values across register pairs, dealing with IEEE 754 floating-point representations, and accounting for different byte ordering conventions.

The implementations in C/C++ and Rust demonstrate both basic scaling operations and advanced features like class-based abstractions, byte order handling, and array processing. Proper error handling, range validation, and clear documentation of scaling parameters are critical for robust industrial applications where measurement accuracy directly impacts process control and safety.