# Timestamp and Duration Types in Protocol Buffers

## Overview

Protocol Buffers provides two well-known types for handling temporal data: `google.protobuf.Timestamp` and `google.protobuf.Duration`. These types are part of the Protocol Buffers standard library and offer platform-independent, language-neutral representations of points in time and time intervals.

## google.protobuf.Timestamp

**Timestamp** represents a point in time independent of any time zone or calendar, encoded as a count of seconds and fractions of seconds at nanosecond resolution.

### Definition
```protobuf
message Timestamp {
  int64 seconds = 1;  // Seconds since Unix epoch (1970-01-01T00:00:00Z)
  int32 nanos = 2;    // Non-negative fractions of a second at nanosecond resolution
}
```

### Key Characteristics
- Represents absolute points in time
- Range: 0001-01-01T00:00:00Z to 9999-12-31T23:59:59.999999999Z
- `seconds`: Can be negative for times before Unix epoch
- `nanos`: Always non-negative, range [0, 999,999,999]

## google.protobuf.Duration

**Duration** represents a signed, fixed-length span of time at nanosecond resolution.

### Definition
```protobuf
message Duration {
  int64 seconds = 1;  // Signed seconds of the span
  int32 nanos = 2;    // Signed fractions of a second at nanosecond resolution
}
```

### Key Characteristics
- Represents time intervals
- Range: approximately ±10,000 years
- Both fields must have the same sign (or be zero)
- `nanos`: Range [-999,999,999, 999,999,999]

## C/C++ Code Examples

```cpp
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/util/time_util.h>
#include <iostream>
#include <chrono>

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using google::protobuf::util::TimeUtil;

void timestamp_examples() {
    std::cout << "=== Timestamp Examples ===" << std::endl;
    
    // Create current timestamp
    Timestamp now = TimeUtil::GetCurrentTime();
    std::cout << "Current time: " << TimeUtil::ToString(now) << std::endl;
    
    // Create timestamp from seconds and nanos
    Timestamp custom_time;
    custom_time.set_seconds(1609459200); // 2021-01-01 00:00:00 UTC
    custom_time.set_nanos(500000000);    // 0.5 seconds
    std::cout << "Custom time: " << TimeUtil::ToString(custom_time) << std::endl;
    
    // Convert from string (RFC 3339 format)
    Timestamp parsed_time;
    if (TimeUtil::FromString("2024-06-15T10:30:45.123456789Z", &parsed_time)) {
        std::cout << "Parsed time: " << TimeUtil::ToString(parsed_time) << std::endl;
        std::cout << "  Seconds: " << parsed_time.seconds() << std::endl;
        std::cout << "  Nanos: " << parsed_time.nanos() << std::endl;
    }
    
    // Convert to/from std::chrono
    auto time_point = std::chrono::system_clock::now();
    Timestamp from_chrono = TimeUtil::TimeTToTimestamp(
        std::chrono::system_clock::to_time_t(time_point)
    );
    std::cout << "From chrono: " << TimeUtil::ToString(from_chrono) << std::endl;
    
    // Convert back to time_t
    time_t converted = TimeUtil::TimestampToTimeT(from_chrono);
    std::cout << "Converted time_t: " << converted << std::endl;
}

void duration_examples() {
    std::cout << "\n=== Duration Examples ===" << std::endl;
    
    // Create duration from seconds
    Duration duration1 = TimeUtil::SecondsToDuration(3661); // 1 hour, 1 minute, 1 second
    std::cout << "Duration 1: " << TimeUtil::ToString(duration1) << std::endl;
    
    // Create duration from milliseconds
    Duration duration2 = TimeUtil::MillisecondsToDuration(5500); // 5.5 seconds
    std::cout << "Duration 2: " << TimeUtil::ToString(duration2) << std::endl;
    
    // Create duration from microseconds
    Duration duration3 = TimeUtil::MicrosecondsToDuration(1234567);
    std::cout << "Duration 3: " << TimeUtil::ToString(duration3) << std::endl;
    
    // Create duration from nanoseconds
    Duration duration4 = TimeUtil::NanosecondsToDuration(999999999);
    std::cout << "Duration 4: " << TimeUtil::ToString(duration4) << std::endl;
    
    // Manual creation
    Duration custom_duration;
    custom_duration.set_seconds(120);
    custom_duration.set_nanos(750000000); // 0.75 seconds
    std::cout << "Custom duration: " << TimeUtil::ToString(custom_duration) << std::endl;
    
    // Parse from string
    Duration parsed_duration;
    if (TimeUtil::FromString("3.5s", &parsed_duration)) {
        std::cout << "Parsed duration: " << TimeUtil::ToString(parsed_duration) << std::endl;
    }
    
    // Convert to different units
    int64_t seconds = TimeUtil::DurationToSeconds(duration1);
    int64_t millis = TimeUtil::DurationToMilliseconds(duration2);
    int64_t micros = TimeUtil::DurationToMicroseconds(duration3);
    int64_t nanos = TimeUtil::DurationToNanoseconds(duration4);
    
    std::cout << "Conversions:" << std::endl;
    std::cout << "  " << seconds << " seconds" << std::endl;
    std::cout << "  " << millis << " milliseconds" << std::endl;
    std::cout << "  " << micros << " microseconds" << std::endl;
    std::cout << "  " << nanos << " nanoseconds" << std::endl;
}

void timestamp_operations() {
    std::cout << "\n=== Timestamp Operations ===" << std::endl;
    
    Timestamp t1 = TimeUtil::GetCurrentTime();
    Duration offset = TimeUtil::SecondsToDuration(3600); // 1 hour
    
    // Add duration to timestamp
    Timestamp t2 = t1 + offset;
    std::cout << "Original: " << TimeUtil::ToString(t1) << std::endl;
    std::cout << "Plus 1 hour: " << TimeUtil::ToString(t2) << std::endl;
    
    // Subtract duration from timestamp
    Timestamp t3 = t2 - offset;
    std::cout << "Minus 1 hour: " << TimeUtil::ToString(t3) << std::endl;
    
    // Calculate difference between timestamps
    Duration diff = t2 - t1;
    std::cout << "Difference: " << TimeUtil::ToString(diff) << std::endl;
}

void duration_operations() {
    std::cout << "\n=== Duration Operations ===" << std::endl;
    
    Duration d1 = TimeUtil::SecondsToDuration(100);
    Duration d2 = TimeUtil::SecondsToDuration(50);
    
    // Add durations
    Duration sum = d1 + d2;
    std::cout << "100s + 50s = " << TimeUtil::ToString(sum) << std::endl;
    
    // Subtract durations
    Duration difference = d1 - d2;
    std::cout << "100s - 50s = " << TimeUtil::ToString(difference) << std::endl;
    
    // Negative duration
    Duration negative = TimeUtil::SecondsToDuration(-30);
    std::cout << "Negative duration: " << TimeUtil::ToString(negative) << std::endl;
}

int main() {
    timestamp_examples();
    duration_examples();
    timestamp_operations();
    duration_operations();
    
    return 0;
}

// Compile with:
// g++ -std=c++17 timestamp_duration.cpp -lprotobuf -o timestamp_duration
```

## Rust Code Examples

```rust
// Cargo.toml dependencies:
// [dependencies]
// prost = "0.12"
// prost-types = "0.12"
// chrono = "0.4"

use prost_types::{Timestamp, Duration};
use chrono::{DateTime, Utc, NaiveDateTime};
use std::time::{SystemTime, UNIX_EPOCH};

fn timestamp_examples() {
    println!("=== Timestamp Examples ===");
    
    // Create timestamp from current time
    let now = SystemTime::now();
    let duration_since_epoch = now.duration_since(UNIX_EPOCH).unwrap();
    let timestamp_now = Timestamp {
        seconds: duration_since_epoch.as_secs() as i64,
        nanos: duration_since_epoch.subsec_nanos() as i32,
    };
    println!("Current timestamp: {:?}", timestamp_now);
    
    // Create custom timestamp
    let custom_timestamp = Timestamp {
        seconds: 1609459200, // 2021-01-01 00:00:00 UTC
        nanos: 500_000_000,  // 0.5 seconds
    };
    println!("Custom timestamp: {:?}", custom_timestamp);
    
    // Convert to DateTime (using chrono)
    let datetime = timestamp_to_datetime(&custom_timestamp);
    println!("As DateTime: {}", datetime);
    
    // Convert from DateTime
    let dt = Utc::now();
    let timestamp_from_dt = datetime_to_timestamp(&dt);
    println!("From DateTime: {:?}", timestamp_from_dt);
    
    // Create from specific date components
    let specific_date = chrono::NaiveDate::from_ymd_opt(2024, 6, 15)
        .unwrap()
        .and_hms_opt(10, 30, 45)
        .unwrap();
    let specific_datetime = DateTime::<Utc>::from_naive_utc_and_offset(specific_date, Utc);
    let specific_timestamp = datetime_to_timestamp(&specific_datetime);
    println!("Specific date as timestamp: {:?}", specific_timestamp);
}

fn duration_examples() {
    println!("\n=== Duration Examples ===");
    
    // Create duration from seconds
    let duration1 = Duration {
        seconds: 3661, // 1 hour, 1 minute, 1 second
        nanos: 0,
    };
    println!("Duration 1: {:?}", duration1);
    println!("  In seconds: {}", duration1.seconds);
    
    // Create duration from milliseconds
    let millis = 5500;
    let duration2 = Duration {
        seconds: millis / 1000,
        nanos: ((millis % 1000) * 1_000_000) as i32,
    };
    println!("Duration 2 (5500ms): {:?}", duration2);
    
    // Create duration from nanoseconds
    let nanos_total: i64 = 1_234_567_890;
    let duration3 = Duration {
        seconds: nanos_total / 1_000_000_000,
        nanos: (nanos_total % 1_000_000_000) as i32,
    };
    println!("Duration 3 (from nanos): {:?}", duration3);
    
    // Negative duration
    let negative_duration = Duration {
        seconds: -30,
        nanos: -500_000_000,
    };
    println!("Negative duration: {:?}", negative_duration);
    
    // Convert to std::time::Duration (only for positive durations)
    if duration1.seconds >= 0 && duration1.nanos >= 0 {
        let std_duration = std::time::Duration::new(
            duration1.seconds as u64,
            duration1.nanos as u32,
        );
        println!("As std::time::Duration: {:?}", std_duration);
    }
}

fn timestamp_operations() {
    println!("\n=== Timestamp Operations ===");
    
    let timestamp1 = Timestamp {
        seconds: 1000,
        nanos: 500_000_000,
    };
    
    let duration = Duration {
        seconds: 100,
        nanos: 250_000_000,
    };
    
    // Add duration to timestamp
    let timestamp2 = add_duration_to_timestamp(&timestamp1, &duration);
    println!("Original: {:?}", timestamp1);
    println!("Plus duration: {:?}", timestamp2);
    
    // Subtract duration from timestamp
    let timestamp3 = subtract_duration_from_timestamp(&timestamp2, &duration);
    println!("Minus duration: {:?}", timestamp3);
    
    // Calculate difference between timestamps
    let diff = timestamp_difference(&timestamp2, &timestamp1);
    println!("Difference: {:?}", diff);
}

fn duration_operations() {
    println!("\n=== Duration Operations ===");
    
    let duration1 = Duration {
        seconds: 100,
        nanos: 500_000_000,
    };
    
    let duration2 = Duration {
        seconds: 50,
        nanos: 250_000_000,
    };
    
    // Add durations
    let sum = add_durations(&duration1, &duration2);
    println!("Duration1: {:?}", duration1);
    println!("Duration2: {:?}", duration2);
    println!("Sum: {:?}", sum);
    
    // Subtract durations
    let difference = subtract_durations(&duration1, &duration2);
    println!("Difference: {:?}", difference);
    
    // Compare durations
    if compare_durations(&duration1, &duration2) > 0 {
        println!("Duration1 is greater than Duration2");
    }
}

// Helper functions

fn timestamp_to_datetime(timestamp: &Timestamp) -> DateTime<Utc> {
    let naive = NaiveDateTime::from_timestamp_opt(
        timestamp.seconds,
        timestamp.nanos as u32,
    ).unwrap();
    DateTime::<Utc>::from_naive_utc_and_offset(naive, Utc)
}

fn datetime_to_timestamp(datetime: &DateTime<Utc>) -> Timestamp {
    Timestamp {
        seconds: datetime.timestamp(),
        nanos: datetime.timestamp_subsec_nanos() as i32,
    }
}

fn add_duration_to_timestamp(timestamp: &Timestamp, duration: &Duration) -> Timestamp {
    let mut seconds = timestamp.seconds + duration.seconds;
    let mut nanos = timestamp.nanos + duration.nanos;
    
    // Normalize nanoseconds
    if nanos >= 1_000_000_000 {
        seconds += 1;
        nanos -= 1_000_000_000;
    } else if nanos < 0 {
        seconds -= 1;
        nanos += 1_000_000_000;
    }
    
    Timestamp { seconds, nanos }
}

fn subtract_duration_from_timestamp(timestamp: &Timestamp, duration: &Duration) -> Timestamp {
    let inverse_duration = Duration {
        seconds: -duration.seconds,
        nanos: -duration.nanos,
    };
    add_duration_to_timestamp(timestamp, &inverse_duration)
}

fn timestamp_difference(t1: &Timestamp, t2: &Timestamp) -> Duration {
    let mut seconds = t1.seconds - t2.seconds;
    let mut nanos = t1.nanos - t2.nanos;
    
    // Normalize
    if nanos < 0 {
        seconds -= 1;
        nanos += 1_000_000_000;
    }
    
    Duration { seconds, nanos }
}

fn add_durations(d1: &Duration, d2: &Duration) -> Duration {
    let mut seconds = d1.seconds + d2.seconds;
    let mut nanos = d1.nanos + d2.nanos;
    
    // Normalize nanoseconds
    if nanos >= 1_000_000_000 {
        seconds += 1;
        nanos -= 1_000_000_000;
    } else if nanos <= -1_000_000_000 {
        seconds -= 1;
        nanos += 1_000_000_000;
    }
    
    // Ensure same sign
    if seconds > 0 && nanos < 0 {
        seconds -= 1;
        nanos += 1_000_000_000;
    } else if seconds < 0 && nanos > 0 {
        seconds += 1;
        nanos -= 1_000_000_000;
    }
    
    Duration { seconds, nanos }
}

fn subtract_durations(d1: &Duration, d2: &Duration) -> Duration {
    let inverse = Duration {
        seconds: -d2.seconds,
        nanos: -d2.nanos,
    };
    add_durations(d1, &inverse)
}

fn compare_durations(d1: &Duration, d2: &Duration) -> i32 {
    if d1.seconds != d2.seconds {
        if d1.seconds > d2.seconds { 1 } else { -1 }
    } else if d1.nanos != d2.nanos {
        if d1.nanos > d2.nanos { 1 } else { -1 }
    } else {
        0
    }
}

fn main() {
    timestamp_examples();
    duration_examples();
    timestamp_operations();
    duration_operations();
}
```

## Summary

**Timestamp and Duration** are essential well-known types in Protocol Buffers for handling temporal data in a platform-independent manner:

### Key Points:

1. **google.protobuf.Timestamp**
   - Represents absolute points in time since Unix epoch (1970-01-01)
   - Stores seconds (int64) and nanoseconds (int32, non-negative)
   - Language-neutral, timezone-independent representation
   - Easily converts to/from native time types (time_t, DateTime, SystemTime)

2. **google.protobuf.Duration**
   - Represents signed time intervals
   - Stores seconds (int64) and nanoseconds (int32) with matching signs
   - Supports negative durations for reverse time spans
   - Range of approximately ±10,000 years

3. **Benefits**
   - **Interoperability**: Works across all Protocol Buffers supported languages
   - **Precision**: Nanosecond resolution for high-precision timing
   - **Standardization**: RFC 3339 format for timestamps ensures consistency
   - **Rich APIs**: Helper functions for conversions, arithmetic, and formatting

4. **Common Operations**
   - Adding/subtracting durations to/from timestamps
   - Calculating differences between timestamps
   - Converting between protobuf types and native time representations
   - Arithmetic operations on durations

5. **Use Cases**
   - Event logging with precise timestamps
   - API request/response timing
   - Distributed system synchronization
   - Time-series data storage
   - Scheduling and timeout management

These types are essential for building robust, time-aware distributed systems with Protocol Buffers, providing a standardized way to handle temporal data across different platforms and programming languages.