# Embedded Systems and Nanopb

## Overview

Nanopb is a lightweight Protocol Buffers implementation specifically designed for embedded systems and microcontrollers with severe resource constraints. Unlike the standard Protocol Buffers libraries (protobuf-c, C++, etc.), nanopb generates extremely small code with minimal memory footprint, making it ideal for systems with limited RAM, flash memory, and processing power.

## Key Features

**Memory Efficiency**: Nanopb can serialize/deserialize messages without dynamic memory allocation, using stack-based or static buffers instead. This is critical for embedded systems where heap allocation is unreliable or unavailable.

**Small Code Size**: The generated code is compact, typically adding only a few kilobytes to your binary, which is essential when working with microcontrollers that may have only 32-256KB of flash memory.

**No Dependencies**: Nanopb has no external dependencies beyond standard C libraries, making it easy to integrate into bare-metal or RTOS-based systems.

**Callback-Based Processing**: For very large messages or streaming data, nanopb supports callback-based encoding/decoding, allowing you to process data in chunks without loading entire messages into memory.

## How Nanopb Works

Nanopb takes a `.proto` file and generates C structures and helper functions. Unlike standard protobuf implementations, nanopb uses fixed-size structures when possible and provides fine-grained control over memory allocation through generator options.

## Code Examples

### Proto Definition

```protobuf
// sensor_data.proto
syntax = "proto3";

message SensorReading {
    uint32 sensor_id = 1;
    float temperature = 2;
    float humidity = 3;
    int64 timestamp = 4;
}

message SensorReadings {
    repeated SensorReading readings = 1;
}
```

### C/C++ Implementation

**Generator Options** (sensor_data.options):
```
SensorReadings.readings max_count:10
```

This tells nanopb to create a fixed-size array of maximum 10 readings instead of using dynamic allocation.

**Encoding Example** (C):
```c
#include <pb_encode.h>
#include "sensor_data.pb.h"

bool encode_sensor_reading(uint8_t *buffer, size_t buffer_size, 
                           size_t *message_length) {
    // Create the message structure
    SensorReading message = SensorReading_init_zero;
    
    // Populate the message
    message.sensor_id = 42;
    message.temperature = 23.5f;
    message.humidity = 65.0f;
    message.timestamp = 1234567890;
    
    // Create output stream
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    
    // Encode the message
    bool status = pb_encode(&stream, SensorReading_fields, &message);
    *message_length = stream.bytes_written;
    
    return status;
}
```

**Decoding Example** (C):
```c
#include <pb_decode.h>
#include "sensor_data.pb.h"

bool decode_sensor_reading(const uint8_t *buffer, size_t message_length,
                           SensorReading *message) {
    // Initialize the message structure
    *message = SensorReading_init_zero;
    
    // Create input stream
    pb_istream_t stream = pb_istream_from_buffer(buffer, message_length);
    
    // Decode the message
    bool status = pb_decode(&stream, SensorReading_fields, message);
    
    return status;
}
```

**Using Callbacks for Repeated Fields** (C):
```c
#include <pb_encode.h>
#include <pb_decode.h>
#include "sensor_data.pb.h"

// Callback for encoding repeated readings
bool encode_readings_callback(pb_ostream_t *stream, 
                               const pb_field_t *field, 
                               void * const *arg) {
    SensorReading reading = SensorReading_init_zero;
    
    // Encode 3 readings
    for (int i = 0; i < 3; i++) {
        reading.sensor_id = i;
        reading.temperature = 20.0f + i;
        reading.humidity = 60.0f + i;
        reading.timestamp = 1234567890 + i;
        
        if (!pb_encode_tag_for_field(stream, field))
            return false;
        if (!pb_encode_submessage(stream, SensorReading_fields, &reading))
            return false;
    }
    
    return true;
}

bool encode_with_callback(uint8_t *buffer, size_t buffer_size,
                          size_t *message_length) {
    SensorReadings message = SensorReadings_init_zero;
    
    // Set up callback
    message.readings.funcs.encode = encode_readings_callback;
    message.readings.arg = NULL;
    
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
    bool status = pb_encode(&stream, SensorReadings_fields, &message);
    *message_length = stream.bytes_written;
    
    return status;
}

// Callback for decoding repeated readings
bool decode_readings_callback(pb_istream_t *stream, 
                               const pb_field_t *field, 
                               void **arg) {
    SensorReading reading = SensorReading_init_zero;
    
    if (!pb_decode(stream, SensorReading_fields, &reading))
        return false;
    
    // Process the reading (print, store, etc.)
    printf("Sensor %u: temp=%.1f, humidity=%.1f\n",
           reading.sensor_id, reading.temperature, reading.humidity);
    
    return true;
}
```

### Rust Implementation

For Rust embedded systems, you can use the `prost` library with `no_std` support or use FFI bindings to nanopb's C code. Here's an example using prost:

**Cargo.toml**:
```toml
[dependencies]
prost = { version = "0.12", default-features = false }
prost-types = { version = "0.12", default-features = false }

[build-dependencies]
prost-build = "0.12"
```

**build.rs**:
```rust
fn main() {
    prost_build::Config::new()
        .compile_protos(&["sensor_data.proto"], &["."])
        .unwrap();
}
```

**Encoding/Decoding** (Rust with no_std):
```rust
#![no_std]

use prost::Message;

// Include generated code
include!(concat!(env!("OUT_DIR"), "/_.rs"));

pub fn encode_sensor_reading(buffer: &mut [u8]) -> Result<usize, prost::EncodeError> {
    let reading = SensorReading {
        sensor_id: 42,
        temperature: 23.5,
        humidity: 65.0,
        timestamp: 1234567890,
    };
    
    let mut buf = &mut buffer[..];
    reading.encode(&mut buf)?;
    
    Ok(reading.encoded_len())
}

pub fn decode_sensor_reading(buffer: &[u8]) -> Result<SensorReading, prost::DecodeError> {
    SensorReading::decode(buffer)
}
```

**Using nanopb via FFI in Rust** (for tighter control):
```rust
#![no_std]

use core::mem::MaybeUninit;

// FFI bindings to nanopb C code
#[repr(C)]
pub struct SensorReading {
    pub sensor_id: u32,
    pub temperature: f32,
    pub humidity: f32,
    pub timestamp: i64,
}

extern "C" {
    fn encode_sensor_reading(
        buffer: *mut u8,
        buffer_size: usize,
        message_length: *mut usize
    ) -> bool;
    
    fn decode_sensor_reading(
        buffer: *const u8,
        message_length: usize,
        message: *mut SensorReading
    ) -> bool;
}

pub fn encode_reading(buffer: &mut [u8]) -> Option<usize> {
    let mut length: usize = 0;
    
    unsafe {
        if encode_sensor_reading(
            buffer.as_mut_ptr(),
            buffer.len(),
            &mut length as *mut usize
        ) {
            Some(length)
        } else {
            None
        }
    }
}

pub fn decode_reading(buffer: &[u8]) -> Option<SensorReading> {
    let mut reading = MaybeUninit::<SensorReading>::uninit();
    
    unsafe {
        if decode_sensor_reading(
            buffer.as_ptr(),
            buffer.len(),
            reading.as_mut_ptr()
        ) {
            Some(reading.assume_init())
        } else {
            None
        }
    }
}
```

## Practical Embedded Use Cases

**IoT Sensor Networks**: Transmit sensor data from battery-powered devices with minimal overhead.

**Industrial Control Systems**: Communicate between PLCs, microcontrollers, and HMI systems using a standardized format.

**Automotive ECUs**: Exchange diagnostic data and control messages between electronic control units with limited resources.

**Robotics**: Serialize control commands and telemetry data on resource-constrained robot controllers.

## Summary

Nanopb brings the benefits of Protocol Buffers to embedded systems by providing a minimal-footprint implementation that works without dynamic memory allocation. Its key advantages include:

- **Tiny memory footprint**: Suitable for microcontrollers with as little as 2-4KB RAM
- **No dynamic allocation**: Uses stack-based or static buffers for predictable memory usage
- **Small code size**: Adds minimal flash memory overhead
- **Callback support**: Process large or streaming messages without loading them entirely into memory
- **Full protobuf compatibility**: Messages encoded with nanopb can be decoded by any protobuf implementation

For embedded developers working with C/C++, nanopb is the go-to solution for Protocol Buffers. Rust developers can use prost with no_std support or create FFI bindings to nanopb for maximum control and efficiency. The result is efficient, maintainable serialization that works reliably on resource-constrained hardware.