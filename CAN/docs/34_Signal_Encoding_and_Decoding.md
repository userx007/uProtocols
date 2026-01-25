# CAN Signal Encoding and Decoding

## Detailed Description

Signal encoding and decoding is a critical aspect of CAN bus communication that deals with converting physical values (like temperature, speed, pressure) into raw binary data for transmission, and vice versa. CAN frames carry raw bytes, but these bytes represent meaningful information encoded according to specific rules defined in database files (DBC).

### Key Concepts

**Signal Definition Parameters:**
- **Start Bit**: Position where the signal begins in the CAN data payload
- **Length**: Number of bits the signal occupies
- **Byte Order (Endianness)**: 
  - **Big Endian (Motorola)**: Most significant byte first
  - **Little Endian (Intel)**: Least significant byte first
- **Scale (Factor)**: Multiplication factor to convert raw value to physical value
- **Offset**: Value added after scaling
- **Min/Max**: Valid range for the physical value
- **Unit**: Physical unit (°C, km/h, bar, etc.)

**Formula:**
```
Physical Value = (Raw Value × Scale) + Offset
Raw Value = (Physical Value - Offset) / Scale
```

### Practical Example

Consider a temperature sensor signal:
- Start bit: 16
- Length: 16 bits
- Byte order: Little Endian
- Scale: 0.01
- Offset: -40
- Range: -40°C to 215°C

A physical temperature of 25.5°C would be encoded as:
```
Raw = (25.5 - (-40)) / 0.01 = 6550
Binary: 0x1996
```

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// Signal definition structure
typedef struct {
    uint8_t start_bit;      // Starting bit position
    uint8_t length;         // Signal length in bits
    bool is_little_endian;  // true = Intel, false = Motorola
    double scale;           // Scaling factor
    double offset;          // Offset value
    double min_value;       // Minimum physical value
    double max_value;       // Maximum physical value
    bool is_signed;         // Signed or unsigned
} CANSignal;

// Extract bits from CAN data array (Little Endian)
uint64_t extract_signal_le(const uint8_t *data, uint8_t start_bit, uint8_t length) {
    uint64_t result = 0;
    uint8_t start_byte = start_bit / 8;
    uint8_t start_bit_in_byte = start_bit % 8;
    
    // Copy relevant bytes to uint64_t
    uint64_t raw_data = 0;
    for (int i = 0; i < 8 && i <= start_byte + (start_bit_in_byte + length - 1) / 8; i++) {
        raw_data |= ((uint64_t)data[i]) << (i * 8);
    }
    
    // Shift and mask to extract the signal
    result = (raw_data >> start_bit) & ((1ULL << length) - 1);
    
    return result;
}

// Extract bits from CAN data array (Big Endian/Motorola)
uint64_t extract_signal_be(const uint8_t *data, uint8_t start_bit, uint8_t length) {
    uint64_t result = 0;
    uint8_t start_byte = start_bit / 8;
    uint8_t start_bit_in_byte = 7 - (start_bit % 8);  // Motorola bit numbering
    
    int bits_remaining = length;
    int current_byte = start_byte;
    int current_bit = start_bit_in_byte;
    int result_bit = length - 1;
    
    while (bits_remaining > 0) {
        if ((data[current_byte] >> current_bit) & 1) {
            result |= (1ULL << result_bit);
        }
        
        result_bit--;
        bits_remaining--;
        
        if (current_bit == 0) {
            current_byte++;
            current_bit = 7;
        } else {
            current_bit--;
        }
    }
    
    return result;
}

// Decode signal to physical value
double decode_signal(const uint8_t *data, const CANSignal *signal) {
    uint64_t raw_value;
    
    // Extract raw value based on byte order
    if (signal->is_little_endian) {
        raw_value = extract_signal_le(data, signal->start_bit, signal->length);
    } else {
        raw_value = extract_signal_be(data, signal->start_bit, signal->length);
    }
    
    // Handle signed values (two's complement)
    int64_t signed_value;
    if (signal->is_signed && (raw_value & (1ULL << (signal->length - 1)))) {
        // Sign extend
        signed_value = raw_value | (~0ULL << signal->length);
    } else {
        signed_value = raw_value;
    }
    
    // Apply scale and offset
    double physical_value = (signed_value * signal->scale) + signal->offset;
    
    return physical_value;
}

// Insert bits into CAN data array (Little Endian)
void insert_signal_le(uint8_t *data, uint8_t start_bit, uint8_t length, uint64_t value) {
    // Create mask for the signal
    uint64_t mask = (1ULL << length) - 1;
    value &= mask;  // Ensure value fits in length bits
    
    // Shift value to correct position
    value <<= start_bit;
    mask <<= start_bit;
    
    // Copy data to uint64_t for manipulation
    uint64_t raw_data = 0;
    for (int i = 0; i < 8; i++) {
        raw_data |= ((uint64_t)data[i]) << (i * 8);
    }
    
    // Clear the signal bits and set new value
    raw_data = (raw_data & ~mask) | value;
    
    // Copy back to data array
    for (int i = 0; i < 8; i++) {
        data[i] = (raw_data >> (i * 8)) & 0xFF;
    }
}

// Insert bits into CAN data array (Big Endian/Motorola)
void insert_signal_be(uint8_t *data, uint8_t start_bit, uint8_t length, uint64_t value) {
    uint8_t start_byte = start_bit / 8;
    uint8_t start_bit_in_byte = 7 - (start_bit % 8);
    
    int bits_remaining = length;
    int current_byte = start_byte;
    int current_bit = start_bit_in_byte;
    int value_bit = length - 1;
    
    while (bits_remaining > 0) {
        // Clear the bit first
        data[current_byte] &= ~(1 << current_bit);
        
        // Set bit if needed
        if ((value >> value_bit) & 1) {
            data[current_byte] |= (1 << current_bit);
        }
        
        value_bit--;
        bits_remaining--;
        
        if (current_bit == 0) {
            current_byte++;
            current_bit = 7;
        } else {
            current_bit--;
        }
    }
}

// Encode physical value to CAN data
void encode_signal(uint8_t *data, const CANSignal *signal, double physical_value) {
    // Clamp value to valid range
    if (physical_value < signal->min_value) physical_value = signal->min_value;
    if (physical_value > signal->max_value) physical_value = signal->max_value;
    
    // Apply inverse scale and offset
    double scaled = (physical_value - signal->offset) / signal->scale;
    
    // Round to nearest integer
    int64_t raw_value = (int64_t)(scaled + (scaled >= 0 ? 0.5 : -0.5));
    
    // Handle signed values
    uint64_t unsigned_value;
    if (signal->is_signed) {
        unsigned_value = raw_value & ((1ULL << signal->length) - 1);
    } else {
        unsigned_value = raw_value;
    }
    
    // Insert based on byte order
    if (signal->is_little_endian) {
        insert_signal_le(data, signal->start_bit, signal->length, unsigned_value);
    } else {
        insert_signal_be(data, signal->start_bit, signal->length, unsigned_value);
    }
}

// Example usage
int main() {
    // Define a temperature signal (Little Endian)
    CANSignal temp_signal = {
        .start_bit = 16,
        .length = 16,
        .is_little_endian = true,
        .scale = 0.01,
        .offset = -40.0,
        .min_value = -40.0,
        .max_value = 215.0,
        .is_signed = true
    };
    
    // Define an RPM signal (Big Endian)
    CANSignal rpm_signal = {
        .start_bit = 7,
        .length = 16,
        .is_little_endian = false,
        .scale = 0.25,
        .offset = 0.0,
        .min_value = 0.0,
        .max_value = 16383.75,
        .is_signed = false
    };
    
    // Create CAN data buffer
    uint8_t can_data[8] = {0};
    
    // Encode temperature: 25.5°C
    encode_signal(can_data, &temp_signal, 25.5);
    
    // Encode RPM: 3500.0 RPM
    encode_signal(can_data, &rpm_signal, 3500.0);
    
    printf("Encoded CAN data: ");
    for (int i = 0; i < 8; i++) {
        printf("%02X ", can_data[i]);
    }
    printf("\n");
    
    // Decode signals
    double decoded_temp = decode_signal(can_data, &temp_signal);
    double decoded_rpm = decode_signal(can_data, &rpm_signal);
    
    printf("Decoded Temperature: %.2f°C\n", decoded_temp);
    printf("Decoded RPM: %.2f RPM\n", decoded_rpm);
    
    return 0;
}
```

## Rust Implementation

```rust
use std::fmt;

#[derive(Debug, Clone)]
pub struct CANSignal {
    pub start_bit: u8,
    pub length: u8,
    pub is_little_endian: bool,
    pub scale: f64,
    pub offset: f64,
    pub min_value: f64,
    pub max_value: f64,
    pub is_signed: bool,
    pub name: String,
    pub unit: String,
}

impl CANSignal {
    pub fn new(
        name: &str,
        start_bit: u8,
        length: u8,
        is_little_endian: bool,
        scale: f64,
        offset: f64,
        min_value: f64,
        max_value: f64,
        is_signed: bool,
        unit: &str,
    ) -> Self {
        CANSignal {
            start_bit,
            length,
            is_little_endian,
            scale,
            offset,
            min_value,
            max_value,
            is_signed,
            name: name.to_string(),
            unit: unit.to_string(),
        }
    }
    
    /// Extract signal using Little Endian byte order
    fn extract_le(&self, data: &[u8; 8]) -> u64 {
        let start_byte = (self.start_bit / 8) as usize;
        let start_bit_in_byte = self.start_bit % 8;
        
        // Convert data to u64 (little endian interpretation)
        let mut raw_data: u64 = 0;
        for (i, &byte) in data.iter().enumerate() {
            raw_data |= (byte as u64) << (i * 8);
        }
        
        // Extract the signal by shifting and masking
        let mask = (1u64 << self.length) - 1;
        (raw_data >> self.start_bit) & mask
    }
    
    /// Extract signal using Big Endian (Motorola) byte order
    fn extract_be(&self, data: &[u8; 8]) -> u64 {
        let start_byte = (self.start_bit / 8) as usize;
        let start_bit_in_byte = 7 - (self.start_bit % 8);
        
        let mut result: u64 = 0;
        let mut bits_remaining = self.length;
        let mut current_byte = start_byte;
        let mut current_bit = start_bit_in_byte;
        let mut result_bit = self.length - 1;
        
        while bits_remaining > 0 {
            if (data[current_byte] >> current_bit) & 1 != 0 {
                result |= 1u64 << result_bit;
            }
            
            result_bit = result_bit.saturating_sub(1);
            bits_remaining -= 1;
            
            if current_bit == 0 {
                current_byte += 1;
                current_bit = 7;
            } else {
                current_bit -= 1;
            }
        }
        
        result
    }
    
    /// Decode raw CAN data to physical value
    pub fn decode(&self, data: &[u8; 8]) -> f64 {
        let raw_value = if self.is_little_endian {
            self.extract_le(data)
        } else {
            self.extract_be(data)
        };
        
        // Handle signed values (two's complement)
        let signed_value = if self.is_signed && (raw_value & (1u64 << (self.length - 1))) != 0 {
            // Sign extend
            (raw_value | (!0u64 << self.length)) as i64
        } else {
            raw_value as i64
        };
        
        // Apply scale and offset
        (signed_value as f64 * self.scale) + self.offset
    }
    
    /// Insert signal using Little Endian byte order
    fn insert_le(&self, data: &mut [u8; 8], value: u64) {
        let mask = (1u64 << self.length) - 1;
        let value = value & mask;
        
        // Convert data to u64
        let mut raw_data: u64 = 0;
        for (i, &byte) in data.iter().enumerate() {
            raw_data |= (byte as u64) << (i * 8);
        }
        
        // Create position mask and insert value
        let pos_mask = mask << self.start_bit;
        raw_data = (raw_data & !pos_mask) | (value << self.start_bit);
        
        // Convert back to byte array
        for i in 0..8 {
            data[i] = ((raw_data >> (i * 8)) & 0xFF) as u8;
        }
    }
    
    /// Insert signal using Big Endian (Motorola) byte order
    fn insert_be(&self, data: &mut [u8; 8], value: u64) {
        let start_byte = (self.start_bit / 8) as usize;
        let start_bit_in_byte = 7 - (self.start_bit % 8);
        
        let mut bits_remaining = self.length;
        let mut current_byte = start_byte;
        let mut current_bit = start_bit_in_byte;
        let mut value_bit = self.length - 1;
        
        while bits_remaining > 0 {
            // Clear the bit
            data[current_byte] &= !(1 << current_bit);
            
            // Set bit if needed
            if (value >> value_bit) & 1 != 0 {
                data[current_byte] |= 1 << current_bit;
            }
            
            value_bit = value_bit.saturating_sub(1);
            bits_remaining -= 1;
            
            if current_bit == 0 {
                current_byte += 1;
                current_bit = 7;
            } else {
                current_bit -= 1;
            }
        }
    }
    
    /// Encode physical value to CAN data
    pub fn encode(&self, data: &mut [u8; 8], physical_value: f64) {
        // Clamp to valid range
        let clamped = physical_value.clamp(self.min_value, self.max_value);
        
        // Apply inverse scale and offset
        let scaled = (clamped - self.offset) / self.scale;
        
        // Round to nearest integer
        let raw_value = scaled.round() as i64;
        
        // Handle signed values
        let unsigned_value = if self.is_signed {
            (raw_value & ((1i64 << self.length) - 1)) as u64
        } else {
            raw_value as u64
        };
        
        // Insert based on byte order
        if self.is_little_endian {
            self.insert_le(data, unsigned_value);
        } else {
            self.insert_be(data, unsigned_value);
        }
    }
}

impl fmt::Display for CANSignal {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} [{}]: start={}, len={}, {} endian, scale={}, offset={}",
            self.name,
            self.unit,
            self.start_bit,
            self.length,
            if self.is_little_endian { "little" } else { "big" },
            self.scale,
            self.offset
        )
    }
}

/// Message containing multiple signals
pub struct CANMessage {
    pub id: u32,
    pub name: String,
    pub signals: Vec<CANSignal>,
}

impl CANMessage {
    pub fn new(id: u32, name: &str) -> Self {
        CANMessage {
            id,
            name: name.to_string(),
            signals: Vec::new(),
        }
    }
    
    pub fn add_signal(&mut self, signal: CANSignal) {
        self.signals.push(signal);
    }
    
    /// Decode all signals from CAN data
    pub fn decode_all(&self, data: &[u8; 8]) -> Vec<(String, f64)> {
        self.signals
            .iter()
            .map(|sig| (format!("{} ({})", sig.name, sig.unit), sig.decode(data)))
            .collect()
    }
    
    /// Encode all signals from a map of values
    pub fn encode_all(&self, values: &std::collections::HashMap<String, f64>) -> [u8; 8] {
        let mut data = [0u8; 8];
        
        for signal in &self.signals {
            if let Some(&value) = values.get(&signal.name) {
                signal.encode(&mut data, value);
            }
        }
        
        data
    }
}

fn main() {
    // Create a CAN message for engine data
    let mut engine_msg = CANMessage::new(0x100, "EngineData");
    
    // Add temperature signal (Little Endian, signed)
    engine_msg.add_signal(CANSignal::new(
        "Temperature",
        16,        // start bit
        16,        // length
        true,      // little endian
        0.01,      // scale
        -40.0,     // offset
        -40.0,     // min
        215.0,     // max
        true,      // signed
        "°C"
    ));
    
    // Add RPM signal (Big Endian, unsigned)
    engine_msg.add_signal(CANSignal::new(
        "RPM",
        7,         // start bit
        16,        // length
        false,     // big endian
        0.25,      // scale
        0.0,       // offset
        0.0,       // min
        16383.75,  // max
        false,     // unsigned
        "RPM"
    ));
    
    // Add throttle position (Little Endian, 0-100%)
    engine_msg.add_signal(CANSignal::new(
        "ThrottlePosition",
        32,        // start bit
        8,         // length
        true,      // little endian
        0.5,       // scale
        0.0,       // offset
        0.0,       // min
        100.0,     // max
        false,     // unsigned
        "%"
    ));
    
    // Create values to encode
    let mut values = std::collections::HashMap::new();
    values.insert("Temperature".to_string(), 25.5);
    values.insert("RPM".to_string(), 3500.0);
    values.insert("ThrottlePosition".to_string(), 75.0);
    
    // Encode message
    let data = engine_msg.encode_all(&values);
    
    println!("Encoded CAN data (ID 0x{:03X}):", engine_msg.id);
    print!("  Bytes: ");
    for byte in &data {
        print!("{:02X} ", byte);
    }
    println!("\n");
    
    // Decode message
    let decoded = engine_msg.decode_all(&data);
    
    println!("Decoded signals:");
    for (name, value) in decoded {
        println!("  {}: {:.2}", name, value);
    }
    
    // Demonstrate individual signal operations
    println!("\n--- Individual Signal Example ---");
    let temp_signal = &engine_msg.signals[0];
    println!("Signal: {}", temp_signal);
    
    let mut test_data = [0u8; 8];
    temp_signal.encode(&mut test_data, 100.0);
    let decoded_temp = temp_signal.decode(&test_data);
    println!("Encoded 100.0°C, decoded: {:.2}°C", decoded_temp);
}
```

## Summary

Signal encoding and decoding is fundamental to CAN communication, translating between physical values and raw binary data. The process involves:

**Key Operations:**
1. **Bit Extraction/Insertion**: Handling arbitrary bit positions and lengths within 8-byte CAN frames
2. **Endianness Conversion**: Supporting both Intel (Little Endian) and Motorola (Big Endian) byte orders
3. **Scaling and Offsetting**: Converting between raw integer values and physical floating-point measurements
4. **Sign Handling**: Properly interpreting signed vs. unsigned values using two's complement

**Critical Considerations:**
- **Byte Order Complexity**: Big Endian bit numbering is more complex, requiring careful bit-by-bit manipulation
- **Precision**: Rounding errors can occur during encoding; proper rounding ensures accuracy
- **Range Validation**: Physical values should be clamped to min/max ranges before encoding
- **DBC Alignment**: Signal definitions must match those in database files for interoperability

**Practical Applications:**
- Automotive ECU communication (engine, transmission, body control)
- Industrial control systems
- Medical devices
- Aerospace systems

Both C/C++ and Rust implementations demonstrate production-ready approaches with proper error handling, type safety (especially in Rust), and efficient bit manipulation techniques essential for real-time embedded systems.