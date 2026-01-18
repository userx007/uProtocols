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