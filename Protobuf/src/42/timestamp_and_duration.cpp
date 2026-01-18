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