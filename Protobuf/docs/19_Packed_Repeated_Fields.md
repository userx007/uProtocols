# Packed Repeated Fields in Protocol Buffers

## Overview

Packed repeated fields are an optimization technique in Protocol Buffers that significantly reduces the wire format size when encoding repeated fields of primitive types. Instead of encoding each element with its own tag and value, packed encoding stores all values contiguously with a single tag and length prefix.

## How It Works

**Standard (Unpacked) Encoding:**
- Each element is encoded as: `tag + value + tag + value + tag + value...`
- Each element requires its own field tag (typically 1-2 bytes)

**Packed Encoding:**
- All elements encoded as: `tag + length + value + value + value...`
- Single tag followed by length prefix, then all values concatenated

This optimization is particularly effective for:
- Large arrays of integers, booleans, enums
- Numeric data with many small values
- Any repeated primitive type field

## Protocol Buffer Definition

```protobuf
syntax = "proto3";

package examples;

message SensorData {
  // Packed by default in proto3
  repeated int32 temperature_readings = 1;
  repeated double coordinates = 2;
  repeated bool status_flags = 3;
  
  // Explicitly unpacked (rare use case)
  repeated int32 sparse_data = 4 [packed = false];
}

// Proto2 example - requires explicit packed annotation
syntax = "proto2";

message LegacySensorData {
  // Must explicitly specify packed=true in proto2
  repeated int32 readings = 1 [packed = true];
  repeated fixed32 timestamps = 2 [packed = true];
}
```

**Key Points:**
- **Proto3**: Packed encoding is the default for repeated primitive fields
- **Proto2**: Requires explicit `[packed = true]` annotation
- Only applies to primitive types (integers, floats, bools, enums)
- Not applicable to strings, bytes, or message types

## C/C++ Examples

### Writing Packed Data

```cpp
#include "sensor_data.pb.h"
#include <iostream>
#include <fstream>

void write_sensor_data() {
    examples::SensorData sensor;
    
    // Add multiple temperature readings
    // These will be packed automatically in proto3
    for (int i = 0; i < 100; i++) {
        sensor.add_temperature_readings(20 + (i % 10));
    }
    
    // Add coordinate data
    sensor.add_coordinates(40.7128);
    sensor.add_coordinates(-74.0060);
    sensor.add_coordinates(100.5);
    
    // Add status flags
    for (int i = 0; i < 50; i++) {
        sensor.add_status_flags(i % 2 == 0);
    }
    
    // Serialize to string
    std::string serialized;
    if (sensor.SerializeToString(&serialized)) {
        std::cout << "Serialized size: " << serialized.size() 
                  << " bytes" << std::endl;
        
        // Write to file
        std::ofstream output("sensor_data.bin", std::ios::binary);
        output.write(serialized.data(), serialized.size());
        output.close();
    }
}

void read_sensor_data() {
    examples::SensorData sensor;
    
    // Read from file
    std::ifstream input("sensor_data.bin", std::ios::binary);
    std::string serialized(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    input.close();
    
    if (sensor.ParseFromString(serialized)) {
        std::cout << "Temperature readings count: " 
                  << sensor.temperature_readings_size() << std::endl;
        
        // Access packed repeated fields
        for (int i = 0; i < sensor.temperature_readings_size(); i++) {
            std::cout << "Reading " << i << ": " 
                      << sensor.temperature_readings(i) << "°C" << std::endl;
        }
        
        // Range-based iteration
        std::cout << "\nCoordinates:" << std::endl;
        for (const auto& coord : sensor.coordinates()) {
            std::cout << coord << " ";
        }
        std::cout << std::endl;
    }
}
```

### Comparing Packed vs Unpacked Size

```cpp
#include "sensor_data.pb.h"
#include <iostream>

void compare_encoding_sizes() {
    examples::SensorData packed_sensor;
    examples::SensorData unpacked_sensor;
    
    // Add same data to both
    for (int i = 0; i < 1000; i++) {
        packed_sensor.add_temperature_readings(i);
    }
    
    // Manually create unpacked version (for demonstration)
    // In practice, you'd use proto2 with packed=false
    std::string packed_data;
    packed_sensor.SerializeToString(&packed_data);
    
    std::cout << "Packed encoding size: " << packed_data.size() 
              << " bytes" << std::endl;
    
    // The unpacked version would be significantly larger
    // Each value would have ~2 bytes overhead for the tag
    size_t unpacked_estimate = packed_data.size() + (1000 * 2);
    std::cout << "Estimated unpacked size: " << unpacked_estimate 
              << " bytes" << std::endl;
    std::cout << "Space savings: " 
              << (100.0 * (unpacked_estimate - packed_data.size()) / unpacked_estimate)
              << "%" << std::endl;
}
```

### Working with Different Primitive Types

```cpp
#include "sensor_data.pb.h"
#include <vector>

void demonstrate_packed_types() {
    examples::SensorData sensor;
    
    // int32/int64 - efficient varint encoding when packed
    std::vector<int32_t> temps = {20, 21, 22, 23, 24};
    for (auto temp : temps) {
        sensor.add_temperature_readings(temp);
    }
    
    // double - fixed 8 bytes per value
    std::vector<double> coords = {1.1, 2.2, 3.3, 4.4};
    for (auto coord : coords) {
        sensor.add_coordinates(coord);
    }
    
    // bool - packed into single bytes efficiently
    for (int i = 0; i < 8; i++) {
        sensor.add_status_flags(true);
    }
    
    std::string output;
    sensor.SerializeToString(&output);
    
    std::cout << "Total message size: " << output.size() << " bytes\n";
    std::cout << "  - " << temps.size() << " int32 values\n";
    std::cout << "  - " << coords.size() << " double values\n";
    std::cout << "  - " << 8 << " bool values\n";
}
```

## Rust Examples

### Basic Packed Field Usage

```rust
// Assuming generated code from sensor_data.proto
use examples::SensorData;

fn write_sensor_data() -> Result<(), Box<dyn std::error::Error>> {
    let mut sensor = SensorData::default();
    
    // Add temperature readings - automatically packed
    sensor.temperature_readings = (0..100)
        .map(|i| 20 + (i % 10))
        .collect();
    
    // Add coordinates
    sensor.coordinates = vec![40.7128, -74.0060, 100.5];
    
    // Add status flags
    sensor.status_flags = (0..50)
        .map(|i| i % 2 == 0)
        .collect();
    
    // Serialize using prost
    use prost::Message;
    let mut buf = Vec::new();
    sensor.encode(&mut buf)?;
    
    println!("Serialized size: {} bytes", buf.len());
    
    // Write to file
    std::fs::write("sensor_data.bin", &buf)?;
    
    Ok(())
}

fn read_sensor_data() -> Result<(), Box<dyn std::error::Error>> {
    use prost::Message;
    
    // Read from file
    let buf = std::fs::read("sensor_data.bin")?;
    
    // Deserialize
    let sensor = SensorData::decode(&buf[..])?;
    
    println!("Temperature readings count: {}", 
             sensor.temperature_readings.len());
    
    // Iterate over packed fields
    for (i, reading) in sensor.temperature_readings.iter().enumerate() {
        println!("Reading {}: {}°C", i, reading);
    }
    
    // Access coordinates
    println!("\nCoordinates:");
    for coord in &sensor.coordinates {
        print!("{} ", coord);
    }
    println!();
    
    Ok(())
}
```

### Performance Comparison

```rust
use examples::SensorData;
use prost::Message;

fn benchmark_packed_encoding() {
    let mut sensor = SensorData::default();
    
    // Generate large dataset
    let data_size = 10_000;
    sensor.temperature_readings = (0..data_size).collect();
    
    // Measure encoding time and size
    let start = std::time::Instant::now();
    let mut buf = Vec::new();
    sensor.encode(&mut buf).unwrap();
    let duration = start.elapsed();
    
    println!("Encoded {} values in {:?}", data_size, duration);
    println!("Total size: {} bytes", buf.len());
    println!("Bytes per value: {:.2}", buf.len() as f64 / data_size as f64);
    
    // Decode benchmark
    let start = std::time::Instant::now();
    let decoded = SensorData::decode(&buf[..]).unwrap();
    let duration = start.elapsed();
    
    println!("Decoded in {:?}", duration);
    println!("Values match: {}", 
             decoded.temperature_readings == sensor.temperature_readings);
}
```

### Working with Different Data Types

```rust
use prost::Message;

#[derive(Clone, PartialEq, Message)]
pub struct MetricsData {
    // Packed int32 - variable length encoding
    #[prost(int32, repeated, tag = "1")]
    pub counters: Vec<i32>,
    
    // Packed fixed32 - always 4 bytes each
    #[prost(fixed32, repeated, tag = "2")]
    pub timestamps: Vec<u32>,
    
    // Packed sfixed64 - always 8 bytes each, signed
    #[prost(sfixed64, repeated, tag = "3")]
    pub measurements: Vec<i64>,
    
    // Packed bool - very compact
    #[prost(bool, repeated, tag = "4")]
    pub flags: Vec<bool>,
    
    // Packed enum values
    #[prost(enumeration = "Status", repeated, tag = "5")]
    pub statuses: Vec<i32>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
#[repr(i32)]
pub enum Status {
    Unknown = 0,
    Active = 1,
    Inactive = 2,
}

fn demonstrate_type_efficiency() {
    let mut metrics = MetricsData::default();
    
    // Small integers - very efficient with varint
    metrics.counters = vec![1, 2, 3, 4, 5];
    
    // Fixed size timestamps
    metrics.timestamps = vec![1000000, 1000001, 1000002];
    
    // Large signed values
    metrics.measurements = vec![-1000000, 2000000, -3000000];
    
    // Booleans - packed into bytes
    metrics.flags = vec![true, false, true, true, false];
    
    // Enum values
    metrics.statuses = vec![
        Status::Active as i32,
        Status::Inactive as i32,
        Status::Active as i32,
    ];
    
    let mut buf = Vec::new();
    metrics.encode(&mut buf).unwrap();
    
    println!("Total encoded size: {} bytes", buf.len());
    
    // Show per-field sizes (approximate)
    let counter_size = metrics.counters.len() * 1; // ~1 byte per small varint
    let timestamp_size = metrics.timestamps.len() * 4; // 4 bytes each
    let measurement_size = metrics.measurements.len() * 8; // 8 bytes each
    let flag_size = (metrics.flags.len() + 7) / 8; // packed into bytes
    
    println!("Breakdown:");
    println!("  Counters: ~{} bytes", counter_size);
    println!("  Timestamps: ~{} bytes", timestamp_size);
    println!("  Measurements: ~{} bytes", measurement_size);
    println!("  Flags: ~{} bytes", flag_size);
}
```

### Stream Processing with Packed Fields

```rust
use prost::Message;
use examples::SensorData;

fn process_sensor_stream() -> Result<(), Box<dyn std::error::Error>> {
    // Simulate receiving multiple sensor messages
    let sensor_count = 100;
    let mut total_readings = 0;
    let mut total_bytes = 0;
    
    for i in 0..sensor_count {
        let mut sensor = SensorData::default();
        
        // Each sensor has varying number of readings
        let reading_count = 10 + (i % 20);
        sensor.temperature_readings = (0..reading_count)
            .map(|j| 20 + ((i + j) % 15) as i32)
            .collect();
        
        // Encode
        let mut buf = Vec::new();
        sensor.encode(&mut buf)?;
        
        total_readings += reading_count;
        total_bytes += buf.len();
        
        // Process the sensor data
        let avg_temp: f64 = sensor.temperature_readings.iter()
            .map(|&x| x as f64)
            .sum::<f64>() / sensor.temperature_readings.len() as f64;
        
        if i < 5 {
            println!("Sensor {}: {} readings, avg temp: {:.1}°C, size: {} bytes",
                     i, reading_count, avg_temp, buf.len());
        }
    }
    
    println!("\nSummary:");
    println!("Total sensors: {}", sensor_count);
    println!("Total readings: {}", total_readings);
    println!("Total bytes: {}", total_bytes);
    println!("Average bytes per reading: {:.2}", 
             total_bytes as f64 / total_readings as f64);
    
    Ok(())
}
```

## Summary

**Packed repeated fields** provide automatic compression for arrays of primitive types in Protocol Buffers:

- **Automatic in Proto3**: All repeated primitive fields use packed encoding by default
- **Significant Space Savings**: Reduces overhead from ~2 bytes per element to a single tag + length prefix for the entire array
- **Best for**: Large arrays of integers, floats, booleans, and enums
- **Not applicable to**: Strings, bytes, or nested messages (these can't be packed)
- **Backward Compatible**: Proto2 and Proto3 messages can interoperate; Proto2 readers handle packed fields correctly
- **Performance**: Slightly faster serialization/deserialization due to reduced parsing overhead
- **Typical Savings**: 30-60% size reduction for numeric arrays, depending on value distribution and field tag sizes

The optimization is transparent to application code—you work with repeated fields normally while the encoding layer handles the efficient wire format automatically.