#include <iostream>
#include <vector>
#include <string>
#include "optimization.pb.h"

using namespace optimization;

void demonstrateMessageSizes() {
    // Example 1: Field numbering impact
    UnoptimizedUser unopt_user;
    unopt_user.set_biography("Software engineer with 10 years experience");
    unopt_user.set_small_counter(42);
    unopt_user.set_user_id(12345);
    unopt_user.set_nickname("dev_user");
    
    OptimizedUser opt_user;
    opt_user.set_biography("Software engineer with 10 years experience");
    opt_user.set_small_counter(42);
    opt_user.set_user_id(12345);
    opt_user.set_nickname("dev_user");
    
    std::cout << "Unoptimized size: " << unopt_user.ByteSizeLong() << " bytes\n";
    std::cout << "Optimized size: " << opt_user.ByteSizeLong() << " bytes\n\n";
    
    // Example 2: Packed repeated fields
    DataPoints packed_data;
    for (int i = 0; i < 100; ++i) {
        packed_data.add_measurements(i);
    }
    
    std::cout << "100 integers (packed): " << packed_data.ByteSizeLong() << " bytes\n\n";
    
    // Example 3: Type selection impact
    MetricsData metrics;
    
    // Small positive number (varint efficient)
    metrics.set_count(50);  // ~2 bytes (1 tag + 1 value)
    
    // Negative number with zigzag
    metrics.set_temperature(-25);  // ~2 bytes (1 tag + 1 value)
    
    // Large fixed number
    metrics.set_timestamp_nanos(1234567890123456789ULL);  // 9 bytes (1 tag + 8 value)
    
    metrics.set_is_active(true);  // 2 bytes
    metrics.set_precision_value(3.14159f);  // 5 bytes (1 tag + 4 value)
    
    std::cout << "Metrics data size: " << metrics.ByteSizeLong() << " bytes\n\n";
    
    // Example 4: Oneof efficiency
    Notification notif1, notif2, notif3;
    
    notif1.set_text_message("Hello!");
    notif1.set_timestamp(1234567890);
    
    notif2.set_video_url("https://example.com/video.mp4");
    notif2.set_timestamp(1234567890);
    
    std::cout << "Notification (text): " << notif1.ByteSizeLong() << " bytes\n";
    std::cout << "Notification (video url): " << notif2.ByteSizeLong() << " bytes\n\n";
    
    // Example 5: Optional field optimization
    OptimizedUser sparse_user;
    sparse_user.set_user_id(999);  // Only set one field
    
    std::cout << "Sparse user (1 field): " << sparse_user.ByteSizeLong() << " bytes\n";
    
    OptimizedUser full_user;
    full_user.set_biography("Full profile with all fields populated");
    full_user.set_small_counter(100);
    full_user.set_user_id(999);
    full_user.set_nickname("power_user");
    for (int i = 0; i < 10; ++i) {
        full_user.add_scores(90 + i);
    }
    
    std::cout << "Full user (all fields): " << full_user.ByteSizeLong() << " bytes\n\n";
}

// Helper function to analyze message size breakdown
void analyzeFieldSizes() {
    OptimizedUser user;
    
    std::cout << "Field-by-field size analysis:\n";
    std::cout << "Empty message: " << user.ByteSizeLong() << " bytes\n";
    
    user.set_biography("Test bio");
    std::cout << "After biography: " << user.ByteSizeLong() << " bytes\n";
    
    user.set_small_counter(42);
    std::cout << "After counter: " << user.ByteSizeLong() << " bytes\n";
    
    user.set_user_id(12345);
    std::cout << "After user_id: " << user.ByteSizeLong() << " bytes\n";
    
    for (int i = 0; i < 5; ++i) {
        user.add_scores(i * 10);
    }
    std::cout << "After 5 scores: " << user.ByteSizeLong() << " bytes\n";
}

// Comparison of integer encoding efficiency
void compareIntegerTypes() {
    MetricsData data1, data2, data3;
    
    // Same value, different types
    int32_t value = 1000000;
    
    data1.set_count(value);  // uint32 with varint
    std::cout << "uint32 (varint) for " << value << ": " 
              << data1.ByteSizeLong() << " bytes\n";
    
    // If we used fixed32 (hypothetically)
    // fixed32 would always be 5 bytes (1 tag + 4 value)
    std::cout << "fixed32 would be: 5 bytes (always)\n";
    
    // For small values
    MetricsData data_small;
    data_small.set_count(10);
    std::cout << "uint32 (varint) for 10: " 
              << data_small.ByteSizeLong() << " bytes\n";
    std::cout << "fixed32 for 10 would be: 5 bytes (always)\n\n";
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    std::cout << "=== Protocol Buffer Message Size Optimization ===\n\n";
    
    demonstrateMessageSizes();
    analyzeFieldSizes();
    compareIntegerTypes();
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}